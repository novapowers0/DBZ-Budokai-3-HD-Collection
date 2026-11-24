// Dragon Budokai Survivors: pruebas de la partida (src/survivors/juego.cpp) sin juego ni graficos. Devuelve 0 si todo pasa.
// Uso (desde la raiz del repo): survivors_juego_test [res/survivors/survivors.toml] [partidas]
//   a mano: clang++ -O2 -std=c++17 -I. -isystem rexglue/include tests/survivors_juego_test.cpp src/survivors/juego.cpp src/survivors/sim.cpp
//           src/survivors/tienda.cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "src/survivors/juego.h"
#include "src/survivors/tienda.h"

using namespace dbz3::survivors;

namespace {
int fallos = 0;
void Comprueba(bool ok, const char* que) {
  if (!ok) ++fallos, std::printf("  FALLO: %s\n", que);
}

// bot sencillo: huye de los enemigos cercanos (y del jefe y sus avisos) y va a por la gema mas cercana
struct Bot {
  uint64_t s;
  uint32_t Azar() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return uint32_t(s >> 33); }
  Entrada Mira(const Juego& j) {
    const float px = j.jugador.x, py = j.jugador.y;
    float fx = 0, fy = 0;
    for (int i = 0; i < j.NumEnemigos(); ++i) {
      const VistaEnemigo e = j.Enemigo(i);
      const float vx = px - e.x, vy = py - e.y, d2 = vx * vx + vy * vy;
      if (d2 < 25 && d2 > 1e-6f) fx += vx / d2, fy += vy / d2;
    }
    if (j.jefe.estado != EstadoJefe::Ausente && j.jefe.estado != EstadoJefe::Muerto) {
      const float vx = px - j.jefe.x, vy = py - j.jefe.y, d2 = vx * vx + vy * vy + 1e-3f;
      if (d2 < 100) fx += 4 * vx / d2, fy += 4 * vy / d2;
    }
    for (int k = 0; k < j.nEfectos; ++k) {   // balas del jefe
      const Efecto2D& e = j.efectos[k];
      if (!e.hostil || e.forma != Forma::Bola) continue;
      const float vx = px - e.x, vy = py - e.y, d2 = vx * vx + vy * vy + 1e-3f;
      if (d2 < 9) fx += vx / d2, fy += vy / d2;
    }
    int g = -1;
    float m = 15 * 15;
    for (int k = 0; k < j.nGemas; ++k) {
      const float d2 = (j.gx[k] - px) * (j.gx[k] - px) + (j.gy[k] - py) * (j.gy[k] - py);
      if (d2 < m) m = d2, g = k;
    }
    if (g >= 0) {
      const float d = std::sqrt(m) + 1e-3f;
      fx += 0.35f * (j.gx[g] - px) / d, fy += 0.35f * (j.gy[g] - py) / d;
    }
    return {fx, fy};
  }
};

struct Final { Estado estado; float minuto; int nivel, derrotados; uint64_t hash; bool sano; int rotos, zeni, racha; };
int g_recogidos[kTiposObjeto] = {};   // objetos recogidos en las partidas del bot (por tipo)

Final Jugar(const Datos& d, int pj, uint64_t semilla, float dura, std::string* tecs = nullptr, const Config& c = {}) {
  auto j = std::make_unique<Juego>();
  j->Iniciar(d, pj, semilla, dura, c);
  Bot b{semilla * 7 + 1};
  bool sano = true;
  for (int p = 0; p < 200000 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
    if (j->estado == Estado::Eligiendo) j->Elegir(int(b.Azar() % uint32_t(j->nOpciones)));
    else j->Paso(b.Mira(*j));
    for (int e = 0; e < j->nEventos; ++e)
      if (j->eventos[e].tipo == TipoEvento::Objeto && j->eventos[e].a >= 0 && j->eventos[e].a < kTiposObjeto) ++g_recogidos[j->eventos[e].a];
    if (!std::isfinite(j->jugador.x) || !std::isfinite(j->jugador.y) || j->NumEnemigos() > Sim::kMax || j->nEfectos > kMaxEfectos ||
        j->nGemas > kMaxGemas || j->nTecnicas > kMaxTecnicas || j->nPasivas > kMaxPasivas || j->nObjetos > kMaxObjetos)
      sano = false;
  }
  if (tecs)
    for (int t = 0; t < j->nTecnicas; ++t)
      *tecs += d.tecnicas[j->tecnicas[t].def].id + ":" + std::to_string(j->tecnicas[t].nivel) + " ";
  return {j->estado, j->Minuto(), j->jugador.nivel, j->derrotados, j->Hash(), sano, j->rotos, j->zeni, j->racha_max};
}

// ---------------------------------------------------------------- C10: bot de equilibrio (3 perfiles) y tabla automatica
// torpe: eleccion al azar, huye tarde y se despista; medio: el bot de siempre (azar en las cartas); experto: esquiva marcas del jefe y rayos,
// recoge cofres/imanes/capsulas, elige por sinergia (evolucion lista > subir lo que evoluciona > pasiva que pide una evolucion > resto) y re-sortea.
enum Perfil { kTorpe = 0, kMedio = 1, kExperto = 2 };
const char* kNombrePerfil[3] = {"torpe", "medio", "experto"};
#ifdef SURV_3DIF   // P1 sube DefDificultad a 3 (normal, dificil, extremo): compilar con -DSURV_3DIF
constexpr int kDif = 3;
const char* kNombreDif[3] = {" (N)", " (D)", " (E)"};
#else
constexpr int kDif = 2;
const char* kNombreDif[3] = {" (N)", " (D)", ""};
#endif

struct Bot2 {
  uint64_t s;
  int perfil;
  float desvio = 0;   // torpe: giro aleatorio que dura unos cuadros
  int t_desvio = 0;
  uint32_t Azar() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return uint32_t(s >> 33); }
  float Azar01() { return float(Azar() & 0xFFFFFF) / 16777216.0f; }
  Entrada Mira(const Juego& j) {
    const float px = j.jugador.x, py = j.jugador.y;
    const float radio2 = perfil == kTorpe ? 9.0f : perfil == kMedio ? 25.0f : 36.0f;   // desde donde huye de la horda
    float fx = 0, fy = 0;
    for (int i = 0; i < j.NumEnemigos(); ++i) {
      const VistaEnemigo e = j.Enemigo(i);
      const float vx = px - e.x, vy = py - e.y, d2 = vx * vx + vy * vy;
      if (d2 < radio2 && d2 > 1e-6f) fx += vx / d2, fy += vy / d2;
    }
    if (perfil >= kMedio && j.jefe.estado != EstadoJefe::Ausente && j.jefe.estado != EstadoJefe::Muerto) {
      const float vx = px - j.jefe.x, vy = py - j.jefe.y, d2 = vx * vx + vy * vy + 1e-3f;
      if (d2 < 100) fx += 4 * vx / d2, fy += 4 * vy / d2;
    }
    for (int k = 0; k < j.nEfectos && perfil >= kMedio; ++k) {
      const Efecto2D& e = j.efectos[k];
      if (!e.hostil) continue;
      const bool bala = e.forma == Forma::Bola, marca = e.forma == Forma::AvisoRayo || e.forma == Forma::Sombra;
      if (!bala && !(marca && perfil == kExperto)) continue;
      const float vx = px - e.x, vy = py - e.y, d2 = vx * vx + vy * vy + 1e-3f, r = marca ? (e.radio + 1.5f) * (e.radio + 1.5f) : 9.0f;
      if (d2 < r) fx += (marca ? 3.0f : 1.0f) * vx / d2, fy += (marca ? 3.0f : 1.0f) * vy / d2;
    }
    const float alcance2 = perfil == kTorpe ? 8 * 8 : perfil == kMedio ? 15 * 15 : 20 * 20;
    int g = -1;
    float m = alcance2;
    for (int k = 0; k < j.nGemas; ++k) {
      const float d2 = (j.gx[k] - px) * (j.gx[k] - px) + (j.gy[k] - py) * (j.gy[k] - py);
      if (d2 < m) m = d2, g = k;
    }
    if (g >= 0) {
      const float d = std::sqrt(m) + 1e-3f;
      fx += 0.35f * (j.gx[g] - px) / d, fy += 0.35f * (j.gy[g] - py) / d;
    }
    if (perfil == kExperto) {   // objetos utiles: cofre siempre; capsula con poca vida; iman y bomba si hay horda
      for (int k = 0; k < j.nObjetos; ++k) {
        const Objeto& o = j.objetos[k];
        const bool quiere = o.tipo == TipoObjeto::Cofre || o.tipo == TipoObjeto::Zeni || (o.tipo == TipoObjeto::Capsula && j.jugador.vida < j.jugador.vida_max * 0.6f) ||
                            o.tipo == TipoObjeto::Iman || o.tipo == TipoObjeto::Reloj || (o.tipo == TipoObjeto::Bomba && j.NumEnemigos() > 60);
        const float vx = o.x - px, vy = o.y - py, d2 = vx * vx + vy * vy;
        if (quiere && d2 < 12 * 12) fx += 0.3f * vx / (std::sqrt(d2) + 1e-3f), fy += 0.3f * vy / (std::sqrt(d2) + 1e-3f);
      }
    }
    if (perfil == kTorpe) {   // se despista: de vez en cuando gira a una direccion cualquiera durante medio segundo
      if (--t_desvio < 0 && Azar() % 90 == 0) t_desvio = 30, desvio = Azar01() * 6.2831853f;
      if (t_desvio > 0) fx += 0.6f * std::cos(desvio), fy += 0.6f * std::sin(desvio);
    }
    return {fx, fy};
  }
  // puntos de una carta: mayor = mejor
  float Puntos(const Juego& j, const Opcion& o) const {
    const Datos& d = *j.datos;
    const float rareza = 1.0f + 0.15f * float(int(o.rareza));
    switch (o.tipo) {
      case TipoOpcion::Evolucion: return 200;
      case TipoOpcion::Curar: return 5 + 40.0f * (1.0f - j.jugador.vida / std::max(1.0f, j.jugador.vida_max));
      case TipoOpcion::Tecnica: {
        const int nivel = j.NivelTecnica(o.def);
        float p = nivel > 0 ? 30.0f + 6.0f * float(nivel) : (j.nTecnicas < kMaxTecnicas ? 24.0f : 0.0f);
        for (const DefTecnica& e : d.tecnicas)   // sube lo que evoluciona: si ya tiene la pasiva que pide
          if (e.evoluciona_de == o.def && e.pasiva >= 0 && j.NivelPasiva(e.pasiva) > 0) p += nivel >= 3 ? 45.0f : 12.0f;
        return p * rareza;
      }
      case TipoOpcion::Pasiva: {
        const int nivel = j.NivelPasiva(o.def);
        if (nivel == 0 && j.nPasivas >= kMaxPasivas) return 0;
        float p = 10.0f + (nivel == 0 ? 3.0f : 0.0f);
        const Efecto e = d.pasivas[size_t(o.def)].efecto;
        if (e == Efecto::Dano || e == Efecto::Recarga || e == Efecto::Area) p += 8;
        for (int t = 0; t < j.nTecnicas; ++t)   // la pasiva que pide la evolucion de una tecnica que lleva
          for (const DefTecnica& ev : d.tecnicas)
            if (ev.evoluciona_de == j.tecnicas[t].def && ev.pasiva == o.def) p += nivel == 0 ? 40.0f : 10.0f;
        return p * rareza;
      }
    }
    return 0;
  }
  void Elige(Juego& j) {   // resuelve la subida o el cofre en curso
    if (j.CofreAbierto()) return j.CerrarCofre();
    if (perfil != kExperto) return j.Elegir(int(Azar() % uint32_t(std::max(1, j.nOpciones))));
    int mejor = 0;
    float pm = -1;
    for (int i = 0; i < j.nOpciones; ++i)
      if (const float p = Puntos(j, j.opciones[i]); p > pm) pm = p, mejor = i;
    if (pm < 40 && j.config.meta_v2 && j.PuedeResortear() && j.resorteos < 3 && j.zeni >= j.CosteResortear() + 20 && j.Resortear()) return Elige(j);
    j.Elegir(mejor);
  }
};

struct Res {   // una partida
  Estado estado = Estado::Derrota;
  float minuto = 0, primera_subida = -1, nivel_min10 = -1;
  int nivel = 0, derrotados = 0, evoluciones = 0, resorteos = 0;
  std::vector<int> tecs;     // indices de las tecnicas del final
  std::vector<double> dano;  // por tecnica
};

Res Partida2(const Datos& d, int pj, uint64_t semilla, int perfil, const Config& c, float dura = 0) {
  auto j = std::make_unique<Juego>();
  j->Iniciar(d, pj, semilla, dura, c);
  Bot2 b{semilla * 7 + 1 + uint64_t(perfil) * 99991, perfil};
  Res r;
  for (int p = 0; p < 200000 && (j->estado == Estado::Jugando || j->estado == Estado::Eligiendo); ++p) {
    if (j->estado == Estado::Eligiendo) b.Elige(*j);
    else j->Paso(b.Mira(*j));
    for (int e = 0; e < j->nEventos; ++e) {
      if (j->eventos[e].tipo == TipoEvento::Subida && r.primera_subida < 0) r.primera_subida = j->tiempo;
      if (j->eventos[e].tipo == TipoEvento::Evolucion) ++r.evoluciones;
    }
    if (r.nivel_min10 < 0 && j->tiempo >= j->duracion * 10.0f / 15.0f) r.nivel_min10 = float(j->jugador.nivel);
  }
  r.estado = j->estado, r.minuto = j->Minuto(), r.nivel = j->jugador.nivel, r.derrotados = j->derrotados, r.resorteos = j->resorteos;
  for (int t = 0; t < j->nTecnicas; ++t) r.tecs.push_back(j->tecnicas[t].def);
  r.dano.assign(d.tecnicas.size(), 0.0);
  for (size_t t = 0; t < d.tecnicas.size(); ++t) r.dano[t] = j->dano_tecnica[t];
  return r;
}

// Tabla: por luchador y perfil, y por escenario y dificultad, n partidas cada celda. Se reparte entre hilos. Escribe `ruta`.
void TablaEquilibrio(const Datos& d, int n, bool meta_v2, const std::string& ruta, bool corta) {
  const int np = int(d.personajes.size()), ne = int(d.escenarios.size());
  struct Celda { int pj, perfil, esc, dif; };
  std::vector<Celda> celdas;
  for (int p = 0; p < np; ++p)
    for (int perfil = 0; perfil < 3; ++perfil) celdas.push_back({p, perfil, -1, 0});   // esc -1 = escenarios repartidos (partida % ne)
  for (int perfil = 1; perfil < 3; ++perfil)
    for (int e = 0; e < ne; ++e)
      for (int dif = 0; dif < kDif; ++dif) celdas.push_back({-1, perfil, e, dif});   // pj -1 = luchadores repartidos
  if (corta) celdas.resize(std::min<size_t>(celdas.size(), 6));
  std::vector<std::vector<Res>> todo(celdas.size());
  std::vector<std::future<void>> fs;
  std::atomic<size_t> sig{0};
  const unsigned nh = std::max(2u, std::min(8u, std::thread::hardware_concurrency()));
  for (unsigned h = 0; h < nh; ++h)
    fs.push_back(std::async(std::launch::async, [&] {
      for (size_t k; (k = sig.fetch_add(1)) < celdas.size();) {
        const Celda& c = celdas[k];
        for (int i = 0; i < n; ++i) {
          Config cf;
          cf.meta_v2 = meta_v2;
          cf.escenario = c.esc >= 0 ? c.esc : i % ne, cf.dificultad = c.dif;
          const int pj = c.pj >= 0 ? c.pj : i % np;
          todo[k].push_back(Partida2(d, pj, 5000 + uint64_t(i) * 13 + uint64_t(k), c.perfil, cf));
        }
      }
    }));
  for (auto& f : fs) f.get();

  std::string out;
  char b[400];
  const auto pon = [&](const char* f, auto... a) { std::snprintf(b, sizeof b, f, a...); out += b; };
  pon("# Tabla de equilibrio de Dragon Budokai Survivors (C10). %d partidas por celda, meta_v2=%d, datos: %zu luchadores, %zu tecnicas, %zu pasivas, %zu enemigos, %zu escenarios.\n",
      n, int(meta_v2), d.personajes.size(), d.tecnicas.size(), d.pasivas.size(), d.enemigos.size(), d.escenarios.size());
  pon("# vict%% = victorias / partidas; min = minuto medio alcanzado (de 15); nivel = nivel final medio; sub1 = segundo de la primera subida; niv10 = nivel medio al minuto 10; evol = partidas con evolucion\n\n");
  pon("## Por luchador y perfil (escenarios repartidos, dificultad normal)\n%-14s %-8s %6s %7s %6s %6s %6s %7s %5s\n", "luchador", "perfil", "vict", "vict%", "min", "nivel", "sub1", "niv10", "evol");
  const auto fila = [&](const std::string& nombre, const char* perfil, const std::vector<Res>& rs) {
    int v = 0, ev = 0;
    double mn = 0, nv = 0, s1 = 0, n10 = 0;
    int ns1 = 0, nn10 = 0;
    for (const Res& r : rs) {
      v += r.estado == Estado::Victoria, mn += r.minuto, nv += r.nivel, ev += r.evoluciones > 0;
      if (r.primera_subida >= 0) s1 += r.primera_subida, ++ns1;
      if (r.nivel_min10 >= 0) n10 += r.nivel_min10, ++nn10;
    }
    const double k = rs.empty() ? 1.0 : double(rs.size());
    pon("%-14s %-8s %2d/%-3zu %6.0f%% %6.2f %6.1f %6.1f %7.1f %5d\n", nombre.c_str(), perfil, v, rs.size(), 100.0 * v / k, mn / k, nv / k, ns1 ? s1 / ns1 : -1.0,
        nn10 ? n10 / nn10 : -1.0, ev);
    return v;
  };
  std::vector<int> mejor_gana(size_t(np), 0);
  for (size_t k = 0; k < celdas.size(); ++k)
    if (celdas[k].esc < 0 && celdas[k].pj >= 0) {
      const int v = fila(d.personajes[size_t(celdas[k].pj)].id, kNombrePerfil[celdas[k].perfil], todo[k]);
      mejor_gana[size_t(celdas[k].pj)] = std::max(mejor_gana[size_t(celdas[k].pj)], v);
    }
  pon("\n## Por escenario y dificultad (luchadores repartidos; N = normal, D = dificil, E = extremo)\n%-14s %-8s %6s %7s %6s %6s %6s %7s %5s\n", "escenario", "perfil", "vict", "vict%", "min", "nivel", "sub1", "niv10", "evol");
  for (size_t k = 0; k < celdas.size(); ++k)
    if (celdas[k].esc >= 0) fila(d.escenarios[size_t(celdas[k].esc)].id + kNombreDif[celdas[k].dif], kNombrePerfil[celdas[k].perfil], todo[k]);
  // por tecnica: veces en el build final y victorias de los perfiles medio y experto; parte del dano total
  std::vector<int> pres(d.tecnicas.size()), gan(d.tecnicas.size());
  std::vector<double> dano(d.tecnicas.size(), 0.0);
  double dano_total = 0;
  for (size_t k = 0; k < celdas.size(); ++k)
    if (celdas[k].perfil >= kMedio)
      for (const Res& r : todo[k]) {
        for (int t : r.tecs) ++pres[size_t(t)], gan[size_t(t)] += r.estado == Estado::Victoria;
        for (size_t t = 0; t < r.dano.size(); ++t) dano[t] += r.dano[t], dano_total += r.dano[t];
      }
  pon("\n## Por tecnica (partidas de los perfiles medio y experto)\n%-20s %8s %8s %9s %7s\n", "tecnica", "en build", "ganadas", "victoria%", "dano%");
  for (size_t t = 0; t < d.tecnicas.size(); ++t)
    pon("%-20s %8d %8d %8.0f%% %6.1f%%\n", d.tecnicas[t].id.c_str(), pres[t], gan[t], pres[t] ? 100.0 * gan[t] / pres[t] : 0.0, dano_total > 0 ? 100.0 * dano[t] / dano_total : 0.0);
  pon("\n## Resumen\n");
  for (int p = 0; p < np; ++p) pon("  %-14s su mejor perfil gana %d de %d\n", d.personajes[size_t(p)].id.c_str(), mejor_gana[size_t(p)], n);
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(ruta).parent_path(), ec);
  std::ofstream(ruta, std::ios::binary | std::ios::trunc) << out;
  std::printf("%s\ntabla escrita en %s\n", out.c_str(), ruta.c_str());
  for (int p = 0; p < np; ++p) Comprueba(mejor_gana[size_t(p)] >= 1 || corta, "cada luchador gana al menos 1 partida con el bot (su mejor perfil)");
}

const char* Nombre(Estado e) { return e == Estado::Victoria ? "VICTORIA" : e == Estado::Derrota ? "derrota" : "SIN ACABAR"; }
}  // namespace

int main(int argc, char** argv) {
  const std::string ruta = argc > 1 ? argv[1] : "res/survivors/survivors.toml";
  const int partidas = argc > 2 ? std::atoi(argv[2]) : 20;
  const std::string modo = argc > 3 ? argv[3] : "";   // "equilibrio" (solo la tabla C10) o "equilibrio_corto" (prueba rapida)
  Datos d;
  std::string err;
  if (!CargarDatos(ruta, &d, &err)) {
    std::printf("no se pueden cargar los datos:\n%s", err.c_str());
    return 2;
  }
  if (!err.empty()) std::printf("avisos de los datos:\n%s", err.c_str());
  std::printf("datos: %zu personajes, %zu tecnicas (con evoluciones), %zu pasivas, %zu enemigos, %zu oleadas\n", d.personajes.size(),
              d.tecnicas.size(), d.pasivas.size(), d.enemigos.size(), d.oleadas.size());
  if (modo.rfind("equilibrio", 0) == 0) {   // C10: solo la tabla (con hilos; unos minutos). Con contenido v2: pasar survivors_v2.toml
    const char* meta = std::getenv("SURV_META_V2");
    TablaEquilibrio(d, partidas, !meta || std::atoi(meta) != 0, "E:/DBZ3HD_trabajo/tmp/survivors/equilibrio.txt", modo == "equilibrio_corto");
    std::printf(fallos ? "FALLOS: %d\n" : "TODO OK\n", fallos);
    return fallos ? 1 : 0;
  }
  Comprueba(d.tecnicas.size() >= 16 && d.pasivas.size() >= 8 && d.personajes.size() >= 3, "12+4 tecnicas, 8 pasivas, 3 personajes (como minimo)");

  // 1) determinismo: misma semilla -> mismo hash tras una partida entera
  {
    const Final a = Jugar(d, 0, 12345, 0), b = Jugar(d, 0, 12345, 0), c = Jugar(d, 0, 54321, 0);
    std::printf("1) determinismo: %016llx %016llx (otra semilla %016llx)\n", (unsigned long long)a.hash, (unsigned long long)b.hash,
                (unsigned long long)c.hash);
    Comprueba(a.hash == b.hash, "misma semilla, mismo hash");
    Comprueba(a.hash != c.hash, "otra semilla, otro hash");
    // con escenario, dificultad y mejoras permanentes (entran como parametros: mismo resultado)
    Config cf;
    cf.escenario = int(d.escenarios.size()) - 1, cf.dificultad = 1;
    for (int k = 0; k < kMejorasPerm; ++k) cf.mejoras[k] = k % 3 + 1;
    const Final e = Jugar(d, 1, 777, 0, nullptr, cf), f = Jugar(d, 1, 777, 0, nullptr, cf), g = Jugar(d, 1, 777, 0);
    std::printf("   con config: %016llx %016llx (sin config %016llx)\n", (unsigned long long)e.hash, (unsigned long long)f.hash, (unsigned long long)g.hash);
    Comprueba(e.hash == f.hash && e.hash != g.hash, "escenario, dificultad y mejoras: deterministas y distintas de la partida de serie");
  }

  // 2) partidas completas con el bot
  {
    int vict = 0, jefe = 0, cofres_vict = 0, rotos = 0, zeni = 0;
    const int ne = int(d.escenarios.size());
    int vict_esc[kMaxEscenarios] = {}, n_esc[kMaxEscenarios] = {};
    std::printf("2) %d partidas de %.0f s con el bot (normal, sin mejoras; escenario = partida %% %d)\n", partidas, d.duracion, ne);
    for (int p = 0; p < partidas; ++p) {
      std::string tecs;
      const int cofres0 = g_recogidos[int(TipoObjeto::Cofre)];
      Config cf;
      cf.escenario = p % ne;
      const Final f = Jugar(d, p % 3, 1000 + uint64_t(p), 0, &tecs, cf);
      const int cofres = g_recogidos[int(TipoObjeto::Cofre)] - cofres0;   // recogidos en esta partida (el del jefe incluido)
      std::printf("  %2d %-8s %-9s %-10s min %5.2f  nivel %2d  derrotados %5d  cofres %2d  rotos %2d  zeni %4d  racha %3d  %s\n", p + 1,
                  d.personajes[p % 3].id.c_str(), d.escenarios[size_t(cf.escenario)].id.c_str(), Nombre(f.estado), f.minuto, f.nivel, f.derrotados, cofres,
                  f.rotos, f.zeni, f.racha, tecs.c_str());
      if (f.estado == Estado::Victoria) cofres_vict += cofres;
      Comprueba(f.sano, "sin desbordes ni NaN");
      Comprueba(f.estado == Estado::Victoria || f.estado == Estado::Derrota, "la partida acaba");
      vict += f.estado == Estado::Victoria, jefe += f.minuto >= d.minuto_jefe, rotos += f.rotos, zeni += f.zeni;
      vict_esc[cf.escenario] += f.estado == Estado::Victoria, ++n_esc[cf.escenario];
    }
    std::printf("  victorias %d/%d, llegan al jefe %d/%d; por escenario:", vict, partidas, jefe, partidas);
    for (int e = 0; e < ne; ++e) std::printf(" %s %d/%d", d.escenarios[size_t(e)].id.c_str(), vict_esc[e], n_esc[e]);
    std::printf("\n  rotos por partida %.1f, zeni por partida %.0f\n", double(rotos) / partidas, double(zeni) / partidas);
    Comprueba(rotos > 0, "el bot rompe objetos");
    if (vict) std::printf("  cofres por partida ganada (15 min enteros, el del jefe incluido): %.1f\n", double(cofres_vict) / vict);
    std::printf("  objetos recogidos por el bot:");
    for (int k = 0; k < kTiposObjeto; ++k) std::printf(" %s %d", NombreObjeto(TipoObjeto(k)), g_recogidos[k]);
    std::printf("\n");
    // 2b) la partida comprimida de las pruebas del juego (DURA=120) no debe dar mas cofres que una de 15 min
    int comp = 0, comp_n = 0;
    for (int p = 0; p < 6; ++p) {
      const int c0 = g_recogidos[int(TipoObjeto::Cofre)];
      const Final f = Jugar(d, p % 3, 2000 + uint64_t(p), 120);
      if (f.estado == Estado::Victoria) comp += g_recogidos[int(TipoObjeto::Cofre)] - c0, ++comp_n;
    }
    if (comp_n) std::printf("  cofres por partida comprimida a 120 s ganada: %.1f (%d ganadas de 6)\n", double(comp) / comp_n, comp_n);
    if (comp_n && vict) Comprueba(double(comp) / comp_n <= double(cofres_vict) / vict + 1.5, "la compresion de tiempo no multiplica los cofres");
  }

  // 3) cada tecnica y evolucion se lanza y hace dano (forzadas, jugador inmortal y quieto, 40 s)
  {
    std::printf("3) cada tecnica hace dano:\n");
    for (int k = 0; k < int(d.tecnicas.size()); ++k) {
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, 0, 77 + uint64_t(k), 0);
      j->tecnicas[0] = {};   // fuera la inicial
      j->nTecnicas = 0;
      const DefTecnica& t = d.tecnicas[k];
      if (t.evoluciona_de >= 0) j->Dar(TipoOpcion::Tecnica, t.evoluciona_de, kNivelMax), j->Dar(TipoOpcion::Evolucion, k, kNivelMax);
      else j->Dar(TipoOpcion::Tecnica, k, 1);
      for (int p = 0; p < 40 * 60; ++p) {
        j->jugador.vida = j->jugador.vida_max = 1e9f;
        if (j->estado == Estado::Eligiendo) j->Elegir(0), j->tecnicas[0] = {k, j->tecnicas[0].nivel}, j->nTecnicas = 1, j->nPasivas = 0;
        else j->Paso({std::cos(p * 0.002f) * 0.3f, std::sin(p * 0.002f) * 0.3f});
      }
      std::printf("  %-18s %-13s usos %4d dano %9.0f derrotados %d\n", t.id.c_str(), t.icono.c_str(), j->usos_tecnica[k], j->dano_tecnica[k],
                  j->derrotados);
      Comprueba(j->usos_tecnica[k] > 0 && j->dano_tecnica[k] > 0, "la tecnica se usa y hace dano");
    }
    // el jefe recibe dano y ataca (partida comprimida a 300 s, jugador inmortal)
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 9, 300);
    int avisos = 0;
    for (int p = 0; p < 320 * 60 && j->estado != Estado::Victoria && j->jefe.estado != EstadoJefe::Muerto; ++p) {
      j->jugador.vida = j->jugador.vida_max = 1e9f;
      if (j->estado == Estado::Eligiendo) j->Elegir(0);
      else j->Paso({0.3f, 0});
      for (int e = 0; e < j->nEventos; ++e) avisos += j->eventos[e].tipo == TipoEvento::AvisoJefe;
    }
    std::printf("  jefe: estado %d vida %.0f/%.0f avisos %d\n", int(j->jefe.estado), j->jefe.vida, j->jefe.vida_max, avisos);
    Comprueba(j->jefe.estado != EstadoJefe::Ausente && j->jefe.vida < j->jefe.vida_max && avisos >= 2, "el jefe sale, avisa y recibe dano");
  }
  // el jefe derrotado (vida a 1 en cuanto sale) suelta su cofre y la victoria llega tras victoria_tras
  {
    auto j = std::make_unique<Juego>();
    j->Iniciar(d, 0, 10, 300);
    for (int p = 0; p < 320 * 60 && j->estado != Estado::Victoria && j->jefe.estado != EstadoJefe::Muerto; ++p) {
      j->jugador.vida = j->jugador.vida_max = 1e9f;
      if (j->jefe.estado != EstadoJefe::Ausente)   // una bomba a los pies: alcanza al jefe recien salido (lista llena: en el primer hueco)
        j->jefe.vida = std::fmin(j->jefe.vida, 1.0f), j->objetos[j->nObjetos < kMaxObjetos ? j->nObjetos++ : 0] = {j->jugador.x, j->jugador.y, TipoObjeto::Bomba};
      if (j->estado == Estado::Eligiendo) j->Elegir(0);
      else j->Paso({0.3f, 0});
    }
    bool cofre = false;
    for (int k = 0; k < j->nObjetos; ++k) cofre |= j->objetos[k].tipo == TipoObjeto::Cofre;
    const bool sigue = j->estado != Estado::Victoria;
    for (int p = 0; p < int(d.recogibles.victoria_tras * 60) + 120 && j->estado != Estado::Victoria; ++p) {
      j->jugador.vida = j->jugador.vida_max = 1e9f;
      if (j->estado == Estado::Eligiendo) j->Elegir(0);
      else j->Paso({0, 0});
    }
    std::printf("  jefe derrotado: cofre %d, sigue la partida %d, final %s\n", int(cofre), int(sigue), Nombre(j->estado));
    Comprueba(j->jefe.estado == EstadoJefe::Muerto && cofre, "el jefe derrotado suelta un cofre");
    Comprueba((sigue || d.recogibles.victoria_tras <= 0) && j->estado == Estado::Victoria, "victoria tras la caida del jefe");
  }

  // 3b) cada objeto hace lo suyo (puesto a los pies del jugador, sin tecnicas, jugador inmortal salvo en la capsula)
  {
    std::printf("3b) objetos:\n");
    const DefRecogibles& r = d.recogibles;
    auto paso = [](Juego& j, Entrada e = {}) {   // inmortal; la eleccion (subida o cofre) se resuelve sola
      j.jugador.vida = j.jugador.vida_max = 1e9f;
      if (j.estado == Estado::Eligiendo) j.Elegir(0);
      else j.Paso(e);
    };
    auto nuevo = [&](uint64_t semilla, float segundos) {   // partida con horda y sin tecnicas
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, 0, semilla, 0);
      j->nTecnicas = 0;
      for (int p = 0; p < int(segundos * 60); ++p) paso(*j);
      return j;
    };
    auto coger = [](Juego& j, TipoObjeto t) {   // lo pone donde esta el jugador y da un paso; true si salio su evento
      if (j.estado == Estado::Eligiendo) j.Elegir(0);
      j.objetos[j.nObjetos++] = {j.jugador.x, j.jugador.y, t};
      j.Paso({0, 0});
      for (int e = 0; e < j.nEventos; ++e)
        if (j.eventos[e].tipo == TipoEvento::Objeto && j.eventos[e].a == int(t)) return true;
      return false;
    };
    {   // capsula: cura una fraccion de la vida maxima
      auto j = nuevo(31, 0);
      j->jugador.vida_max = 100, j->jugador.vida = 10;
      const bool ev = coger(*j, TipoObjeto::Capsula);
      std::printf("  capsula: vida 10 -> %.1f\n", j->jugador.vida);
      Comprueba(ev && j->jugador.vida >= 10 + r.cura * 100 - 1, "la capsula cura");
    }
    {   // iman: atrae gemas lejanas (fuera del radio de recogida)
      auto j = nuevo(32, 0);
      j->nGemas = 0;
      for (int g = 0; g < 20; ++g) j->gx[g] = j->jugador.x + 25 + g, j->gy[g] = j->jugador.y - 10, j->gvalor[g] = 1, ++j->nGemas;
      const bool ev = coger(*j, TipoObjeto::Iman);
      for (int p = 0; p < int(r.iman_dura * 60) - 1; ++p) paso(*j);
      std::printf("  iman: gemas lejanas 20 -> %d\n", j->nGemas);
      Comprueba(ev && j->nGemas < 5, "el iman atrae todas las gemas");
    }
    {   // bomba: tumba a la horda cercana
      auto j = nuevo(33, 30);
      const int antes = j->derrotados, n = j->NumEnemigos();
      const bool ev = coger(*j, TipoObjeto::Bomba);
      std::printf("  bomba: enemigos %d, derrotados +%d\n", n, j->derrotados - antes);
      Comprueba(ev && n > 0 && j->derrotados - antes > n / 2, "la bomba dana a todo lo que hay en pantalla");
    }
    {   // reloj: la horda se para (solo la separacion la mueve un poco) y despues sigue; el jugador anda para que le persigan
      auto j = nuevo(34, 20);
      auto recorrido = [&](int pasos) {
        static float x0[Sim::kMax], y0[Sim::kMax];
        const int n = j->NumEnemigos();
        for (int i = 0; i < n; ++i) x0[i] = j->Enemigo(i).x, y0[i] = j->Enemigo(i).y;
        for (int p = 0; p < pasos; ++p) paso(*j, {1, 0});
        float s = 0;
        const int m = std::min(n, j->NumEnemigos());
        for (int i = 0; i < m; ++i) s += std::hypot(j->Enemigo(i).x - x0[i], j->Enemigo(i).y - y0[i]);
        return m ? s / float(m) : 0.0f;
      };
      const float libre = recorrido(30);
      const bool ev = coger(*j, TipoObjeto::Reloj);
      const float quieto = recorrido(30);
      for (int p = 0; p < int(r.reloj_dura * 60); ++p) paso(*j);
      const float despues = recorrido(30);
      std::printf("  reloj: recorrido medio en 0,5 s libre %.3f, congelado %.3f, despues %.3f\n", libre, quieto, despues);
      Comprueba(ev && quieto < libre * 0.3f && despues > libre * 0.5f, "el reloj congela a la horda unos segundos");
    }
    {   // reloj y jefe: no se congela, va a reloj_jefe (de serie, la mitad) de su ritmo mientras dure
      auto j = nuevo(36, 0);
      auto avance = [&]() {   // jefe persiguiendo, lejos, recien empezado su ciclo: lo que anda en 0,5 s
        j->jefe.estado = EstadoJefe::Persigue, j->jefe.t = 0, j->jefe.vida = j->jefe.vida_max = 1e6f;
        j->jefe.x = j->jugador.x + 20, j->jefe.y = j->jugador.y;
        const float x0 = j->jefe.x, y0 = j->jefe.y;
        for (int p = 0; p < 30; ++p) paso(*j);
        return std::hypot(j->jefe.x - x0, j->jefe.y - y0);
      };
      const float libre = avance();
      const bool ev = coger(*j, TipoObjeto::Reloj);
      const float lento = avance();
      std::printf("  reloj y jefe: avance en 0,5 s libre %.3f, con reloj %.3f (x%.2f; toml %.2f)\n", libre, lento, libre > 0 ? lento / libre : 0.0f, r.reloj_jefe);
      Comprueba(ev && libre > 0 && std::fabs(lento / libre - r.reloj_jefe) < 0.1f, "el reloj ralentiza al jefe a reloj_jefe");
    }
    {   // zeni: suma al contador
      auto j = nuevo(35, 0);
      const bool ev = coger(*j, TipoObjeto::Zeni);
      std::printf("  zeni: %d\n", j->zeni);
      Comprueba(ev && j->zeni == r.zeni_valor, "la moneda suma zeni");
    }
    {   // cofre: abre la eleccion de una mejora gratis sin subir de nivel
      auto j = nuevo(36, 0);
      const int nivel = j->jugador.nivel;
      const bool ev = coger(*j, TipoObjeto::Cofre);
      std::printf("  cofre: estado %d opciones %d nivel %d\n", int(j->estado), j->nOpciones, j->jugador.nivel);
      Comprueba(ev && j->estado == Estado::Eligiendo && j->nOpciones > 0 && j->jugador.nivel == nivel, "el cofre da una mejora a elegir");
      j->Elegir(0);
      Comprueba(j->estado == Estado::Jugando, "tras elegir sigue la partida");
    }
    {   // caidas: el elite suelta cofres y la horda los demas objetos (probabilidades del toml)
      Datos dd = d;
      for (Oleada& o : dd.oleadas) o.ritmo = 300, o.tope = 600, o.peso[5] = 0.3f;   // con elites
      auto j = std::make_unique<Juego>();
      j->Iniciar(dd, 0, 37, 0);
      j->nTecnicas = 0;
      j->Dar(TipoOpcion::Tecnica, 4, kNivelMax), j->Dar(TipoOpcion::Tecnica, 10, kNivelMax), j->Dar(TipoOpcion::Tecnica, 11, kNivelMax);
      int vistos[kTiposObjeto] = {};
      for (int p = 0; p < 90 * 60; ++p) {
        j->jugador.vida = j->jugador.vida_max = 1e9f;
        if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
        j->nObjetos = 0;   // se cuentan y se quitan (sin tope de 64 ni recogidas)
        j->Paso({0, 0});
        for (int k = 0; k < j->nObjetos; ++k) ++vistos[int(j->objetos[k].tipo)];
      }
      std::printf("  caidas en 90 s (derrotados %d):", j->derrotados);
      int tipos = 0;
      for (int k = 0; k < kTiposObjeto; ++k) std::printf(" %s %d", NombreObjeto(TipoObjeto(k)), vistos[k]), tipos += vistos[k] > 0;
      std::printf("\n");
      Comprueba(vistos[int(TipoObjeto::Cofre)] > 0 && tipos >= 4, "los enemigos sueltan objetos y el elite cofres");
    }
  }

  // 5) tanda 4: rompibles, mejoras permanentes, dificultad, tercer ataque del jefe, rayo, racha y tienda
  {
    std::printf("5) tanda 4:\n");
    std::printf("  %zu escenarios, %zu enemigos, %zu jefes, %zu rompibles, %zu mejoras permanentes\n", d.escenarios.size(), d.enemigos.size(), d.jefes.size(),
                d.rompibles.size(), d.mejoras.size());
    Comprueba(d.escenarios.size() >= 4 && d.jefes.size() >= 4 && d.rompibles.size() >= 6 && d.mejoras.size() >= 10 && d.mejoras.size() <= size_t(kMejorasPerm),
              "4 escenarios (o mas) con su jefe, 6 rompibles y 10 mejoras");
    auto inmortal = [](Juego& j, Entrada e = {}) {
      j.jugador.vida = j.jugador.vida_max = 1e9f;
      if (j.estado == Estado::Eligiendo) j.Elegir(0);
      else j.Paso(e);
    };
    {   // rompible: un golpe de aura lo rompe, queda roto en el mismo sitio y suelta su gema / objeto
      int rotos = 0, suelta = 0, mismo = 0;
      for (int intento = 0; intento < 12; ++intento) {
        auto j = std::make_unique<Juego>();
        Config cf;
        cf.escenario = intento % 4;
        j->Iniciar(d, 0, 400 + uint64_t(intento), 0, cf);
        j->nTecnicas = 0;
        j->Dar(TipoOpcion::Tecnica, 4, kNivelMax);   // aura
        if (j->nRompibles == 0) continue;
        Rompible& r = j->rompibles[0];
        r.x = j->jugador.x + 1.0f, r.y = j->jugador.y;
        const float x0 = r.x, y0 = r.y;
        const int tipo = r.tipo;
        const int gemas0 = j->nGemas, obj0 = j->nObjetos;
        bool ev = false;
        for (int p = 0; p < 90 && !ev; ++p) {
          inmortal(*j);
          for (int e = 0; e < j->nEventos; ++e) ev |= j->eventos[e].tipo == TipoEvento::Rompible;
        }
        int k = -1;   // el mismo (puede haber cambiado de hueco si se quito otro lejano: se busca por posicion)
        for (int i = 0; i < j->nRompibles; ++i)
          if (j->rompibles[i].x == x0 && j->rompibles[i].y == y0) k = i;
        rotos += ev, mismo += k >= 0 && j->rompibles[k].roto >= 0;
        suelta += j->nGemas > gemas0 || j->nObjetos > obj0 || d.rompibles[size_t(tipo)].exp <= 0;
      }
      std::printf("  rompibles: rotos %d/12, quedan rotos en su sitio %d, sueltan algo %d\n", rotos, mismo, suelta);
      Comprueba(rotos >= 10 && mismo == rotos && suelta >= rotos, "el rompible se rompe de un golpe, queda roto en su sitio y suelta su tabla");
      // determinista y por celda: dos partidas con la misma semilla ponen los mismos
      auto a = std::make_unique<Juego>(), b = std::make_unique<Juego>();
      a->Iniciar(d, 0, 99, 0), b->Iniciar(d, 0, 99, 0);
      bool igual = a->nRompibles == b->nRompibles && a->nRompibles > 0;
      for (int i = 0; igual && i < a->nRompibles; ++i) igual = a->rompibles[i].x == b->rompibles[i].x && a->rompibles[i].tipo == b->rompibles[i].tipo;
      Comprueba(igual, "los rompibles salen igual con la misma semilla");
    }
    {   // mejoras permanentes: vida, dano (Mult) y reanimar
      Config cf;
      int iv = -1, ir = -1, id = -1;
      for (int k = 0; k < int(d.mejoras.size()); ++k) {
        if (d.mejoras[size_t(k)].efecto == Efecto::VidaMax) iv = k;
        if (d.mejoras[size_t(k)].efecto == Efecto::Reanimar) ir = k;
        if (d.mejoras[size_t(k)].efecto == Efecto::Dano) id = k;
      }
      Comprueba(iv >= 0 && ir >= 0 && id >= 0, "hay mejoras de vida, dano y reanimar");
      if (iv >= 0 && ir >= 0 && id >= 0) {
        cf.mejoras[iv] = 5, cf.mejoras[ir] = 2, cf.mejoras[id] = 3;
        auto j = std::make_unique<Juego>(), s = std::make_unique<Juego>();
        j->Iniciar(d, 0, 5, 0, cf), s->Iniciar(d, 0, 5, 0);
        std::printf("  mejoras: vida %.0f (de serie %.0f), dano +%.2f, vidas extra %d\n", j->jugador.vida_max, s->jugador.vida_max, j->Mejora(Efecto::Dano),
                    j->vidas_extra);
        Comprueba(std::fabs(j->jugador.vida_max - (s->jugador.vida_max + 5 * d.mejoras[size_t(iv)].por_nivel)) < 0.01f && j->jugador.vida == j->jugador.vida_max,
                  "la mejora de vida sube la vida maxima al empezar");
        Comprueba(std::fabs(j->Mejora(Efecto::Dano) - 3 * d.mejoras[size_t(id)].por_nivel) < 1e-4f, "la mejora de dano entra en el multiplicador");
        j->jugador.vida = -5;   // muere: vuelve una vez con su parte de vida
        j->Paso({});
        bool rean = false;
        for (int e = 0; e < j->nEventos; ++e) rean |= j->eventos[e].tipo == TipoEvento::Reanimado;
        const float vida = j->jugador.vida;
        j->jugador.vida = -5, j->jugador.invulnerable = 0;
        j->Paso({});
        std::printf("  reanimar: evento %d, vida %.1f, despues %s\n", int(rean), vida, Nombre(j->estado));
        Comprueba(rean && j->vidas_extra == 0 && std::fabs(vida - j->jugador.vida_max * 2 * d.mejoras[size_t(ir)].por_nivel) < 1.0f && j->estado == Estado::Derrota,
                  "reanimar da una sola vida extra con su parte de la vida");
      }
    }
    {   // dificultad: el jefe sale con mas vida y la moneda vale mas
      auto n = std::make_unique<Juego>(), h = std::make_unique<Juego>();
      Config cf;
      cf.dificultad = 1;
      n->Iniciar(d, 0, 6, 0), h->Iniciar(d, 0, 6, 0, cf);
      n->nTecnicas = h->nTecnicas = 0;
      n->objetos[n->nObjetos++] = {n->jugador.x, n->jugador.y, TipoObjeto::Zeni}, h->objetos[h->nObjetos++] = {h->jugador.x, h->jugador.y, TipoObjeto::Zeni};
      n->Paso({}), h->Paso({});
      for (Juego* j : {n.get(), h.get()}) {
        j->tiempo = j->duracion * d.minuto_jefe / 15.0f;
        inmortal(*j);
      }
      std::printf("  dificultad: zeni %d -> %d, jefe %.0f -> %.0f\n", n->zeni, h->zeni, n->jefe.vida_max, h->jefe.vida_max);
      Comprueba(h->zeni == int(float(d.recogibles.zeni_valor) * d.dificultad[1].zeni) && n->zeni == d.recogibles.zeni_valor, "dificil: la moneda vale mas");
      Comprueba(n->jefe.estado != EstadoJefe::Ausente && std::fabs(h->jefe.vida_max - n->jefe.vida_max * d.dificultad[1].vida) < 1.0f, "dificil: el jefe tiene mas vida");
    }
    {   // tercer ataque del jefe: zona + una sombra por proyectil; caen y danan al jugador quieto en el centro
      int lluvias = 0, sombras = 0, impactos = 0, golpes = 0;
      for (int e = 0; e < int(d.escenarios.size()); ++e) {
        auto j = std::make_unique<Juego>();
        Config cf;
        cf.escenario = e;
        j->Iniciar(d, 0, 50 + uint64_t(e), 0, cf);
        j->tiempo = j->duracion * d.minuto_jefe / 15.0f;
        int esta = 0, tras = 0;   // hasta 3 s despues de la segunda lluvia (que caigan todas)
        for (int p = 0; p < 55 * 60 && (esta < 2 || ++tras < 180); ++p) {
          j->nTecnicas = 0;   // el jefe no muere
          inmortal(*j);
          j->jugador.invulnerable = 0;
          for (int k = 0; k < j->nEventos; ++k) {
            const Evento& ev = j->eventos[k];
            if (ev.tipo == TipoEvento::AvisoJefe && ev.a == 2) {
              ++lluvias, ++esta;
              for (int f = 0; f < j->nEfectos; ++f) sombras += j->efectos[f].forma == Forma::Sombra;
            }
            golpes += ev.tipo == TipoEvento::DanoJugador && esta > 0;
          }
          for (int f = 0; f < j->nEfectos; ++f) impactos += j->efectos[f].hostil && j->efectos[f].forma == Forma::Explosion && j->efectos[f].vida0 - j->efectos[f].vida < 0.02f;
        }
      }
      std::printf("  lluvia del jefe: %d ataques, %d sombras, %d impactos, dano al jugador tras la lluvia %d\n", lluvias, sombras, impactos, golpes);
      Comprueba(lluvias >= 4 && sombras >= lluvias * 8 && impactos >= sombras && golpes > 0, "el jefe lanza la lluvia avisada con sombras y los proyectiles caen");
    }
    {   // rayo (escenarios con rayo_cada): avisa, cae y dana a la horda
      int rayos = 0, con = 0;
      for (int e = 0; e < int(d.escenarios.size()); ++e) {
        if (d.escenarios[size_t(e)].rayo_cada <= 0) continue;
        ++con;
        auto j = std::make_unique<Juego>();
        Config cf;
        cf.escenario = e;
        j->Iniciar(d, 0, 70 + uint64_t(e), 0, cf);
        for (int p = 0; p < 150 * 60; ++p) {
          inmortal(*j, {0.3f, 0.1f});
          for (int k = 0; k < j->nEventos; ++k) rayos += j->eventos[k].tipo == TipoEvento::RayoCae;
        }
      }
      std::printf("  rayos en 150 s (%d escenarios con rayo): %d\n", con, rayos);
      Comprueba(con >= 1 && rayos >= 3 * con, "caen rayos avisados en los escenarios con tormenta");
    }
    {   // racha: sube derrotando y se apaga tras 'dura' s sin derrotar
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, 0, 81, 0);
      j->nTecnicas = 0;
      j->Dar(TipoOpcion::Tecnica, 4, kNivelMax), j->Dar(TipoOpcion::Tecnica, 11, kNivelMax);
      int maxr = 0;
      for (int p = 0; p < 60 * 60; ++p) inmortal(*j), maxr = std::max(maxr, j->racha);
      while (j->estado == Estado::Eligiendo) j->Elegir(0);
      j->nTecnicas = 0;
      for (int p = 0; p < int((d.racha.dura + 1.5f) * 60); ++p) {
        j->jugador.vida = 1e9f;
        if (j->estado == Estado::Eligiendo) j->Elegir(0), j->nTecnicas = 0;
        else j->Paso({1, 0});
      }
      std::printf("  racha: maxima %d, tras %.1f s sin tecnicas %d\n", maxr, d.racha.dura + 1.5f, j->racha);
      Comprueba(maxr >= 10 && j->racha == 0, "la racha sube y se apaga");
    }
    {   // tienda y guardado: precios, compra, ida y vuelta al archivo, archivo corrupto, desbloqueo
      Progreso g;
      g.zeni = 1000;
      const int p0 = Precio(d, g, 0);
      const bool c1 = Comprar(d, 0, &g), c2 = Comprar(d, 0, &g);
      std::printf("  tienda: precio %d, compras %d %d, nivel %d, zeni %d\n", p0, int(c1), int(c2), g.mejoras[0], g.zeni);
      Comprueba(c1 && c2 && g.mejoras[0] == 2 && g.zeni == 1000 - d.mejoras[0].precio[0] - d.mejoras[0].precio[1], "comprar resta el precio y sube el nivel");
      Progreso pobre;
      Comprueba(!Comprar(d, 1, &pobre) && pobre.mejoras[1] == 0, "sin zeni no se compra");
      g.mejoras[3] = kNivelMax;
      Comprueba(Precio(d, g, 3) < 0 && !Comprar(d, 3, &g), "al maximo no se compra mas");
      const std::filesystem::path ruta = std::filesystem::temp_directory_path() / "survivors_progreso_test.toml";
      std::error_code ec;
      std::filesystem::remove(ruta, ec), std::filesystem::remove(ruta.string() + ".bak", ec);
      Progreso l;
      std::string aviso;
      Comprueba(!LeerProgreso(ruta, &l, &aviso) && l.zeni == 0 && l.desbloqueados == 1, "sin archivo: progreso de serie");
      g.desbloqueados = 5, g.medalla[2] = kPlatino, g.jefes = 3;
      Comprueba(GuardarProgreso(ruta, g) && LeerProgreso(ruta, &l, &aviso), "guardar y leer");
      Comprueba(l.zeni == g.zeni && l.mejoras[0] == 2 && l.mejoras[3] == kNivelMax && l.desbloqueados == 5 && l.medalla[2] == kPlatino && l.jefes == 3,
                "el progreso vuelve igual");
      g.zeni = 4321;
      GuardarProgreso(ruta, g);   // la anterior pasa a .bak
      std::ofstream(ruta, std::ios::binary | std::ios::trunc) << "zeni = [[[ roto";
      Comprueba(LeerProgreso(ruta, &l, &aviso) && l.zeni == 1000 - d.mejoras[0].precio[0] - d.mejoras[0].precio[1], "archivo corrupto: se usa el .bak");
      std::filesystem::remove(ruta.string() + ".bak", ec);
      Comprueba(!LeerProgreso(ruta, &l, &aviso) && l.zeni == 0, "corrupto y sin .bak: progreso de serie, sin cuelgue");
      std::filesystem::remove(ruta, ec);
      // desbloqueo: ganar (o llegar al minuto 10) abre el siguiente
      auto j = std::make_unique<Juego>();
      j->Iniciar(d, 0, 3, 0);
      j->estado = Estado::Victoria, j->zeni = 120;
      Progreso q;
      const int nuevo = Anotar(*j, int(d.escenarios.size()), &q);
      Comprueba(nuevo == 1 && q.Desbloqueado(1) && !q.Desbloqueado(2) && q.zeni == 120 && q.medalla[0] == kOro, "ganar desbloquea el siguiente y suma los zeni");
      j->estado = Estado::Derrota, j->tiempo = j->duracion * 5.0f / 15.0f;
      Comprueba(Anotar(*j, int(d.escenarios.size()), &q) == -1 && q.medalla[0] == kOro, "perder antes del minuto 10 no desbloquea ni baja la medalla");
    }
  }

  // 4) tiempo por paso con ~600 enemigos y 6 tecnicas al 5 + 6 pasivas al 5 (dos grupos de 6: las 12 tecnicas)
  for (int grupo = 0; grupo < 2; ++grupo) {
    Datos dd = d;
    for (Oleada& o : dd.oleadas) o.ritmo = 300, o.tope = 600;
    auto j = std::make_unique<Juego>();
    j->Iniciar(dd, 0, 5, 0);
    j->nTecnicas = 0;
    for (int t = 0; t < 6; ++t) j->Dar(TipoOpcion::Tecnica, grupo * 6 + t, kNivelMax);
    for (int p = 0; p < 6; ++p) j->Dar(TipoOpcion::Pasiva, p, kNivelMax);
    double suma = 0, peor = 0;
    int medidos = 0, nmed = 0;
    for (int p = 0; p < 40 * 60; ++p) {
      j->jugador.vida = j->jugador.vida_max = 1e9f;
      if (j->estado == Estado::Eligiendo) { j->Elegir(0); continue; }
      const auto t0 = std::chrono::steady_clock::now();
      j->Paso({std::cos(p * 0.01f), std::sin(p * 0.01f)});
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      if (p >= 10 * 60) suma += ms, peor = ms > peor ? ms : peor, ++medidos, nmed += j->NumEnemigos();
    }
    const double media = suma / (medidos ? medidos : 1);
    std::printf("4) rendimiento grupo %d (tecnicas %d-%d): enemigos medios %d, efectos %d, media %.3f ms, peor %.3f ms\n", grupo + 1,
                grupo * 6 + 1, grupo * 6 + 6, nmed / (medidos ? medidos : 1), j->nEfectos, media, peor);
    Comprueba(media < 1.0, "media < 1 ms por paso");
  }

  std::printf(fallos ? "FALLOS: %d\n" : "TODO OK\n", fallos);
  return fallos ? 1 : 0;
}
