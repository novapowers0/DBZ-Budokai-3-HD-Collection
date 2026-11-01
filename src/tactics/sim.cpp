#include "sim.h"

#include <algorithm>
#include <cstdlib>
#include <tuple>

namespace dbz3::tactics::sim {
namespace {
struct Cubo { int x, y, z; };
constexpr Cubo kDir[6] = {{1, -1, 0}, {1, 0, -1}, {0, 1, -1}, {-1, 1, 0}, {-1, 0, 1}, {0, -1, 1}};

Cubo ACubo(int idx) {
  int col = idx % kAncho, fila = idx / kAncho;
  int x = col - (fila - (fila & 1)) / 2;
  return {x, -x - fila, fila};
}
int Dist(int a, int b) {
  Cubo p = ACubo(a), q = ACubo(b);
  return std::max({std::abs(p.x - q.x), std::abs(p.y - q.y), std::abs(p.z - q.z)});
}
// Cubo (x, z) -> indice de casilla, o -1 fuera del tablero.
int DeCubo(int x, int z) {
  if (z < 0 || z >= kAlto) return -1;
  int col = x + (z - (z & 1)) / 2;
  return col < 0 || col >= kAncho ? -1 : z * kAncho + col;
}
int Vec(int idx, int dir) { Cubo c = ACubo(idx); return DeCubo(c.x + kDir[dir].x, c.z + kDir[dir].z); }
}  // namespace

bool Dentro(Casilla c) { return c.col >= 0 && c.col < kAncho && c.fila >= 0 && c.fila < kAlto; }
int Distancia(Casilla a, Casilla b) { return Dist(a.fila * kAncho + a.col, b.fila * kAncho + b.col); }
int Vecino(Casilla c, int dir) { return Vec(c.fila * kAncho + c.col, dir); }

uint64_t Rng::Siguiente() {
  uint64_t z = (estado += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
uint32_t Rng::Hasta(uint32_t n) { return n ? static_cast<uint32_t>(Siguiente() % n) : 0; }
Rng Flujo(uint64_t semilla, uint32_t n) { Rng r{semilla ^ (0xD1B54A32D192ED03ull * (n + 1))}; r.Siguiente(); return r; }

Sim::Sim(uint64_t semilla) : combate_(Flujo(semilla, 0)), cosmetico_(Flujo(semilla, 1)) { std::fill(ocupa_, ocupa_ + kAncho * kAlto, -1); }

int Sim::Anadir(const DefUnidad& d, int equipo, Casilla c) {
  if ((equipo != 0 && equipo != 1) || !Dentro(c) || tick_ != 0) return -1;
  if (equipo == 0 ? c.fila < 3 : c.fila > 2) return -1;
  int idx = c.fila * kAncho + c.col;
  if (ocupa_[idx] != -1) return -1;
  Unidad u; u.def = d; u.equipo = equipo; u.casilla = idx; u.vida = d.vida;
  u.ki = std::min(d.ki_max, d.ki_max * ((d.pasiva == Pasiva::KiInicial ? d.pasiva_valor : 0) + d.ki_inicial) / 100);
  u_.push_back(u);
  pienso_hasta_.push_back(0);
  ant_.push_back(-1);
  retro_.push_back(false);
  ocupa_[idx] = static_cast<int>(u_.size()) - 1;
  return ocupa_[idx];
}

void Sim::Emitir(Tipo t, Accion a, int u, int o, int v) {
  ev_.push_back({tick_, t, a, static_cast<int16_t>(u), static_cast<int16_t>(o), v});
}

int Sim::Calcular(const Unidad& a, const Unidad& v, bool tecnica) const {
  const DefUnidad& d = a.def;
  int atk = d.ataque;
  if (d.pasiva == Pasiva::Furia && a.vida * 2 < d.vida) atk = atk * (100 + d.pasiva_valor) / 100;
  int def = v.def.defensa;
  if (!tecnica && d.pasiva == Pasiva::Perfora) def = def * (100 - d.pasiva_valor) / 100;
  int dano = atk * (tecnica ? d.tecnica.dano : 100) / 100 * 100 / (100 + def);
  int extra = 0;
  if (d.pasiva == Pasiva::Remate && v.vida * 10 < v.def.vida * 3) extra = d.pasiva_valor;
  if (d.pasiva == Pasiva::Resistentes && tecnica && v.def.vida > d.vida) extra = d.pasiva_valor;
  dano = dano * (100 + extra) / 100;
  if (v.def.pasiva == Pasiva::Coraza) dano = dano * (100 - v.def.pasiva_valor) / 100;
  return std::max(1, dano);
}

void Sim::Morir(int u) {
  Unidad& x = u_[u];
  x.estado = Estado::Muerta; x.vida = 0;
  ocupa_[x.casilla] = -1;
  if (x.destino >= 0) ocupa_[x.destino] = -1;
  x.destino = -1;
  Emitir(Tipo::Muerte, Accion::Ninguna, u, -1, 0);
}

// Todos los golpes del tick se calculan con el estado de antes del lote y se aplican a la vez: nadie ataca "primero".
void Sim::Golpear(const std::vector<Golpe>& lote) {
  std::vector<int> dano(lote.size());
  for (size_t i = 0; i < lote.size(); ++i) dano[i] = Calcular(u_[lote[i].atacante], u_[lote[i].victima], lote[i].tecnica);
  for (size_t i = 0; i < lote.size(); ++i) {
    Unidad &a = u_[lote[i].atacante], &v = u_[lote[i].victima];
    v.vida -= dano[i];
    v.ki = std::min(v.def.ki_max, v.ki + kKiPorRecibido);
    if (!lote[i].tecnica) a.ki = std::min(a.def.ki_max, a.ki + kKiPorGolpe);
    Emitir(Tipo::Impacto, lote[i].tecnica ? Accion::Tecnica : Accion::Basico, lote[i].atacante, lote[i].victima, dano[i]);
  }
  for (size_t i = 0; i < u_.size(); ++i)
    if (u_[i].viva() && u_[i].vida <= 0) Morir(static_cast<int>(i));
}

// Objetivo y primer paso. REGLA DE OBJETIVO (H13): el enemigo mas cercano; el que ya se tenia gana si no esta mas de kHisteresis casillas mas lejos
// (asi no cambia de rival por una casilla de diferencia) y el empate final es el menor id. REGLA DE MOVIMIENTO: ruta mas corta por casillas libres
// (una unidad por casilla) hasta la casilla mas cercana desde la que alcance al objetivo; entre pasos igual de cortos gana el que mas acerca en linea
// recta al objetivo, y nunca se vuelve a la casilla que se acaba de dejar si hay otra igual de buena. *paso: -1 ya en alcance, -2 sin camino, o casilla.
int Sim::Elegir(int u, int* paso) {
  const Unidad& me = u_[u];
  std::vector<int> cand;
  for (size_t i = 0; i < u_.size(); ++i)
    if (u_[i].viva() && u_[i].equipo != me.equipo) cand.push_back(static_cast<int>(i));
  auto clave = [&](int i) { return std::make_tuple(Dist(me.casilla, u_[i].casilla) - (i == me.objetivo ? kHisteresis : 0), i != me.objetivo, i); };
  std::sort(cand.begin(), cand.end(), [&](int a, int b) { return clave(a) < clave(b); });
  *paso = -2;
  if (cand.empty()) return -1;
  // subTAC (1/11): el objetivo se elige por PASOS reales hasta poder golpearlo (rodeando a los aliados que estorban), no por distancia en linea recta:
  // detras de la fila de los suyos el enemigo mas cercano en recta puede estar a un rodeo largo mientras otro esta a dos pasos.
  int mejor_c = -1, mejor_paso = -2, mejor_pasos = 1 << 20;
  for (int c : cand) {
    const int meta = u_[c].casilla, alc = me.def.alcance;
    if (Dist(me.casilla, meta) <= alc) { *paso = -1; return c; }
    int dg[kAncho * kAlto], cola[kAncho * kAlto], ini = 0, fin = 0;   // distancia en pasos desde las casillas libres que alcanzan a la meta
    std::fill(dg, dg + kAncho * kAlto, -1);
    for (int k = 0; k < kAncho * kAlto; ++k)
      if (ocupa_[k] == -1 && Dist(k, meta) <= alc) dg[k] = 0, cola[fin++] = k;
    while (ini < fin && dg[me.casilla] < 0) {
      const int cur = cola[ini++];
      for (int d = 0; d < 6; ++d) {
        const int n = Vec(cur, d);
        if (n < 0 || dg[n] != -1) continue;
        if (n == me.casilla) { dg[n] = dg[cur] + 1; break; }
        if (ocupa_[n] != -1) continue;
        dg[n] = dg[cur] + 1; cola[fin++] = n;
      }
    }
    if (dg[me.casilla] < 0) continue;
    int mejor = -1;
    auto k2 = [&](int n) { return std::make_tuple(n == ant_[u], Dist(n, meta), n); };
    for (int d = 0; d < 6; ++d) {
      const int n = Vec(me.casilla, d);
      if (n >= 0 && ocupa_[n] == -1 && dg[n] == dg[me.casilla] - 1 && (mejor < 0 || k2(n) < k2(mejor))) mejor = n;
    }
    if (mejor < 0) continue;
    const int pasos = dg[me.casilla] - (c == me.objetivo ? 2 : 0);   // el objetivo actual se conserva salvo que otro este 3 o mas pasos mas cerca
    if (pasos < mejor_pasos) mejor_pasos = pasos, mejor_c = c, mejor_paso = mejor;   // cand va ordenada: a igualdad gana el primero (mas cercano en recta)
  }
  if (mejor_c >= 0) { *paso = mejor_paso; return mejor_c; }
  // Ningun enemigo es alcanzable (cercado por los suyos): intenta al menos acercarse al mas cercano.
  int mejor = -1, md = Dist(me.casilla, u_[cand[0]].casilla);
  for (int d = 0; d < 6; ++d) {
    int n = Vec(me.casilla, d);
    if (n >= 0 && ocupa_[n] == -1 && Dist(n, u_[cand[0]].casilla) < md) { md = Dist(n, u_[cand[0]].casilla); mejor = n; }
  }
  if (mejor >= 0) *paso = mejor;
  return cand[0];
}

void Sim::Empezar(int u, Accion a, int obj) {
  Unidad& x = u_[u];
  retro_[u] = false;
  if (a == Accion::Mover) {
    x.estado = Estado::Moviendo; x.destino = obj; ocupa_[obj] = u;
    x.t_impacto = 0; x.t_fin = tick_ + std::max(1, 600 / std::max(1, x.def.velocidad));
  } else {
    bool t = a == Accion::Tecnica;
    int total = std::max(2, t ? x.def.tecnica_ticks : x.def.ataque_ticks);
    x.objetivo = obj;
    x.estado = t ? Estado::Tecnica : Estado::Basico;
    if (t) x.ki = 0;
    x.t_impacto = tick_ + std::min(total - 1, std::max(1, total * (t ? 50 : 40) / 100));
    x.t_fin = tick_ + total;
  }
  Emitir(Tipo::Inicio, a, u, a == Accion::Mover ? obj : x.objetivo, 0);
}

void Sim::Decidir(int u) {
  Unidad& x = u_[u];
  if (tick_ < static_cast<uint32_t>(pienso_hasta_[u])) return;
  if (x.ki >= x.def.ki_max) {   // tecnica: al enemigo mas cercano que este a su alcance
    int mejor = -1, md = 99;
    const bool sigue = x.objetivo >= 0 && u_[x.objetivo].viva() && Dist(x.casilla, u_[x.objetivo].casilla) <= x.def.tecnica.alcance;
    if (sigue) mejor = x.objetivo;   // H13: la tecnica va al objetivo que ya tenia si lo alcanza; si no, al mas cercano a su alcance
    else for (size_t i = 0; i < u_.size(); ++i)
      if (u_[i].viva() && u_[i].equipo != x.equipo) {
        int d = Dist(x.casilla, u_[i].casilla);
        if (d <= x.def.tecnica.alcance && d < md) { md = d; mejor = static_cast<int>(i); }
      }
    if (mejor >= 0) { Empezar(u, Accion::Tecnica, mejor); return; }
  }
  int paso, obj = Elegir(u, &paso);
  if (obj < 0) return;
  x.objetivo = obj;
  if (paso == -1) Empezar(u, Accion::Basico, obj);
  else if (paso >= 0 && paso == ant_[u] && !retro_[u]) retro_[u] = true, pienso_hasta_[u] = static_cast<int>(tick_) + kEsperaRetroceso;   // volver atras: primero se espera un poco por si se despeja el camino
  else if (paso >= 0) Empezar(u, Accion::Mover, paso);
  else pienso_hasta_[u] = static_cast<int>(tick_) + kEsperaBloqueo;
}

void Sim::Comprobar() {
  Resultado r = Resumen();
  bool tope = tick_ >= static_cast<uint32_t>(kTopeTicks);
  if (r.vivas[0] && r.vivas[1] && !tope) return;
  fin_ = true;
  int g = !r.vivas[0] && !r.vivas[1] ? -1 : !r.vivas[1] ? 0 : !r.vivas[0] ? 1 : r.vida[0] > r.vida[1] ? 0 : r.vida[1] > r.vida[0] ? 1 : -1;
  Emitir(Tipo::FinRonda, Accion::Ninguna, -1, -1, g);
}

void Sim::Paso() {
  if (fin_) return;
  ++tick_;
  // 1. impactos: el lote entero cae a la vez
  std::vector<Golpe> lote;
  for (size_t i = 0; i < u_.size(); ++i) {
    const Unidad& a = u_[i];
    if ((a.estado != Estado::Basico && a.estado != Estado::Tecnica) || a.t_impacto != tick_) continue;
    const bool tec = a.estado == Estado::Tecnica;
    const int id = static_cast<int>(i);
    const Unidad* o = a.objetivo >= 0 && u_[a.objetivo].viva() ? &u_[a.objetivo] : nullptr;
    int alc = tec ? a.def.tecnica.alcance : a.def.alcance;
    if (!o || Dist(a.casilla, o->casilla) > alc) { Emitir(Tipo::Fallo, tec ? Accion::Tecnica : Accion::Basico, id, a.objetivo, 0); continue; }
    const Forma f = tec ? a.def.tecnica.forma : Forma::Objetivo;
    const int area = a.def.tecnica.area;
    int dir = 0;
    if (f == Forma::Linea) {   // la direccion hex que mas se acerca al objetivo
      Cubo c = ACubo(a.casilla);
      int d0 = Dist(a.casilla, o->casilla), md = 99;
      for (int d = 0; d < 6; ++d) {
        int n = DeCubo(c.x + kDir[d].x * d0, c.z + kDir[d].z * d0);
        int dd = n < 0 ? 98 : Dist(n, o->casilla);
        if (dd < md) { md = dd; dir = d; }
      }
    }
    for (size_t j = 0; j < u_.size(); ++j) {
      const Unidad& v = u_[j];
      if (!v.viva() || v.equipo == a.equipo) continue;
      bool toca = static_cast<int>(j) == a.objetivo;
      if (f == Forma::Circulo) toca = Dist(o->casilla, v.casilla) <= area;
      if (f == Forma::Linea) {
        Cubo c = ACubo(a.casilla);
        for (int k = 1; k <= area; ++k) if (DeCubo(c.x + kDir[dir].x * k, c.z + kDir[dir].z * k) == v.casilla) toca = true;
      }
      if (toca) lote.push_back({id, static_cast<int>(j), tec});
    }
  }
  Golpear(lote);
  // 2. llegadas y 3. recuperaciones
  for (size_t i = 0; i < u_.size(); ++i) {
    Unidad& x = u_[i];
    if (!x.viva() || x.t_fin != tick_) continue;
    const Accion hecha = x.estado == Estado::Moviendo ? Accion::Mover : x.estado == Estado::Tecnica ? Accion::Tecnica : Accion::Basico;
    if (hecha == Accion::Mover) { ocupa_[x.casilla] = -1; ant_[i] = x.casilla; x.casilla = x.destino; x.destino = -1; }
    x.estado = Estado::Espera;
    Emitir(Tipo::Recuperacion, hecha, static_cast<int>(i), -1, x.casilla);
  }
  // 4. cada segundo: ki regular, regeneracion limitada y, tras kInicioDesgaste, desgaste igual para todos
  if (tick_ % kHz == 0) {
    for (size_t i = 0; i < u_.size(); ++i) {
      Unidad& x = u_[i];
      if (!x.viva()) continue;
      if (x.def.pasiva == Pasiva::KiRegular) x.ki = std::min(x.def.ki_max, x.ki + x.def.pasiva_valor);
      if (x.def.pasiva == Pasiva::Regenera) {
        int cura = std::max(0, std::min({x.def.vida * x.def.pasiva_valor / 100, x.def.vida / 4 - x.regenerado, x.def.vida - x.vida}));
        x.vida += cura; x.regenerado += cura;
      }
      if (tick_ > static_cast<uint32_t>(kInicioDesgaste)) {
        int d = std::max(1, x.def.vida * kDesgastePct / 100);
        x.vida -= d;
        Emitir(Tipo::Impacto, Accion::Ninguna, -1, static_cast<int>(i), d);
      }
    }
    for (size_t i = 0; i < u_.size(); ++i) if (u_[i].viva() && u_[i].vida <= 0) Morir(static_cast<int>(i));
  }
  Comprobar();
  if (fin_) return;
  // 5. decisiones en orden barajado con el azar de combate: quien reserva una casilla disputada no depende del bando
  std::vector<int> libres;
  for (size_t i = 0; i < u_.size(); ++i) if (u_[i].estado == Estado::Espera) libres.push_back(static_cast<int>(i));
  for (size_t i = libres.size(); i > 1; --i) std::swap(libres[i - 1], libres[combate_.Hasta(static_cast<uint32_t>(i))]);
  for (int id : libres) Decidir(id);
}

Resultado Sim::Resumen() const {
  Resultado r; r.terminada = fin_; r.ticks = tick_;
  for (const Unidad& u : u_) if (u.viva()) { ++r.vivas[u.equipo]; r.vida[u.equipo] += u.vida; }
  if (fin_) r.ganador = ev_.back().valor;
  return r;
}

uint32_t Sim::Huella() const {
  uint32_t h = 2166136261u;
  auto mezcla = [&h](uint32_t v) { for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 16777619u; } };
  for (const Evento& e : ev_) {
    mezcla(e.tick); mezcla(static_cast<uint32_t>(e.tipo)); mezcla(static_cast<uint32_t>(e.accion));
    mezcla(static_cast<uint32_t>(e.unidad)); mezcla(static_cast<uint32_t>(e.objetivo)); mezcla(static_cast<uint32_t>(e.valor));
  }
  return h;
}

}  // namespace dbz3::tactics::sim
