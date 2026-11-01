#include "src/tactics/partida.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>

#include <toml++/toml.hpp>

namespace dbz3::tactics {
namespace {

struct Lector {
  std::string& err;
  int Num(const toml::table& t, const char* clave, int lo, int hi, const std::string& donde) {
    auto v = t[clave].value<int64_t>();
    if (!v) { err += donde + ": falta '" + clave + "' (entero)\n"; return lo; }
    if (*v < lo || *v > hi) err += donde + ": '" + clave + "' fuera de rango " + std::to_string(lo) + ".." + std::to_string(hi) + "\n";
    return static_cast<int>(*v);
  }
  std::vector<int> Lista(const toml::table& t, const char* clave, int lo, int hi, const std::string& donde) {
    std::vector<int> r;
    const toml::array* a = t[clave].as_array();
    if (!a) { err += donde + ": falta '" + clave + "' (lista)\n"; return r; }
    for (auto& n : *a) {
      int64_t v = n.value_or<int64_t>(lo - 1);
      if (v < lo || v > hi) err += donde + ": '" + clave + "' tiene un valor fuera de " + std::to_string(lo) + ".." + std::to_string(hi) + "\n";
      r.push_back(int(v));
    }
    return r;
  }
};

constexpr int kColumnasCentro[6] = {2, 3, 1, 4, 0, 5};   // de dentro hacia fuera
}  // namespace

bool CargarEconomia(const std::string& ruta, const Catalogo& cat, Economia* out, std::string* error) {
  std::string err;
  toml::table raiz;
  try {
    raiz = toml::parse_file(ruta);
  } catch (const toml::parse_error& e) {
    *error = ruta + ": " + std::string(e.description()) + "\n";
    return false;
  }
  if (raiz["version"].value_or<int64_t>(0) != 1) err += "version debe ser 1\n";
  Lector l{err};
  Economia e;
  const toml::table* ec = raiz["economia"].as_table();
  if (!ec) {
    *error = "falta [economia]\n";
    return false;
  }
  const std::string d = "economia";
  e.jugadores = l.Num(*ec, "jugadores", 2, kMaxJug, d);
  e.calidad_bots = l.Lista(*ec, "calidad_bots", 0, 100, d);
  if (int(e.calidad_bots.size()) + 1 < e.jugadores) err += "economia: 'calidad_bots' necesita " + std::to_string(e.jugadores - 1) + " valores\n";
  e.oro_inicial = l.Num(*ec, "oro_inicial", 0, 100, d);
  e.ingreso_ronda = l.Num(*ec, "ingreso_ronda", 0, 50, d);
  e.ingreso_victoria = l.Num(*ec, "ingreso_victoria", 0, 50, d);
  e.neutral_desde = l.Num(*ec, "neutral_desde", 1, 1000, d);
  e.neutral_cada = l.Num(*ec, "neutral_cada", 0, 1000, d);
  e.neutral_oro = l.Num(*ec, "neutral_oro", 0, 50, d);
  e.neutral_pct = l.Num(*ec, "neutral_pct", 10, 300, d);
  e.neutral_pct_ronda = l.Num(*ec, "neutral_pct_ronda", 0, 50, d);
  e.racha_desde = l.Num(*ec, "racha_desde", 1, 100, d);
  e.racha_cada = l.Num(*ec, "racha_cada", 1, 100, d);
  e.racha_tope = l.Num(*ec, "racha_tope", 0, 20, d);
  e.interes_cada = l.Num(*ec, "interes_cada", 1, 100, d);
  e.interes_tope = l.Num(*ec, "interes_tope", 0, 20, d);
  e.tienda = l.Num(*ec, "tienda", 1, kMaxTienda, d);
  e.precio_actualizar = l.Num(*ec, "precio_actualizar", 0, 20, d);
  e.precio_exp = l.Num(*ec, "precio_exp", 1, 50, d);
  e.exp_compra = l.Num(*ec, "exp_compra", 1, 50, d);
  e.exp_ronda = l.Num(*ec, "exp_ronda", 0, 50, d);
  e.capsula_cada = l.Num(*ec, "capsula_cada", 0, 20, d);
  e.precio_capsula = l.Num(*ec, "precio_capsula", 1, 20, d);
  e.vida_entrenador = l.Num(*ec, "vida_entrenador", 1, 1000, d);
  e.dano_base = l.Num(*ec, "dano_base", 1, 100, d);
  e.dano_cada_rondas = l.Num(*ec, "dano_cada_rondas", 1, 100, d);
  e.dano_superviviente = l.Num(*ec, "dano_superviviente", 0, 100, d);
  e.dano_tope = l.Num(*ec, "dano_tope", 1, 1000, d);
  e.exp_subir = l.Lista(*ec, "exp_subir", 1, 1000, d);
  e.tope_unidades = l.Lista(*ec, "tope_unidades", 1, kTablero, d);
  const std::vector<int> copias = l.Lista(*ec, "copias_coste", 1, 100, d), mult = l.Lista(*ec, "mult_estrella", 100, 1000, d);
  if (copias.size() == kMaxCoste) for (int i = 0; i < kMaxCoste; ++i) e.copias_coste[i] = copias[size_t(i)];
  else err += "economia: 'copias_coste' debe tener 4 valores\n";
  if (mult.size() == kMaxEstrellas) for (int i = 0; i < kMaxEstrellas; ++i) e.mult_estrella[i] = mult[size_t(i)];
  else err += "economia: 'mult_estrella' debe tener 4 valores\n";
  if (e.tope_unidades.empty() || e.exp_subir.size() + 1 != e.tope_unidades.size()) err += "economia: exp_subir debe tener un valor menos que tope_unidades\n";
  if (const toml::array* pc = (*ec)["peso_coste"].as_array()) {
    for (auto& fila : *pc) {
      std::array<int, kMaxCoste> w{};
      const toml::array* f = fila.as_array();
      if (!f || f->size() != kMaxCoste) { err += "economia: cada fila de 'peso_coste' lleva 4 pesos\n"; continue; }
      for (int i = 0; i < kMaxCoste; ++i) w[size_t(i)] = int(f->at(size_t(i)).value_or<int64_t>(0));
      e.peso_coste.push_back(w);
    }
  }
  if (e.peso_coste.size() != e.tope_unidades.size()) err += "economia: 'peso_coste' debe tener una fila por nivel\n";

  std::set<std::string> ids;
  {   // capsulas: tactics_capsulas.toml, junto a este fichero
    const std::string rc = (std::filesystem::path(ruta).parent_path() / "tactics_capsulas.toml").string();
    toml::table cr;
    try {
      cr = toml::parse_file(rc);
    } catch (const toml::parse_error& pe) {
      err += rc + ": " + std::string(pe.description()) + "\n";
    }
    if (cr["version"].value_or<int64_t>(0) != 1) err += "tactics_capsulas.toml: version debe ser 1\n";
    std::set<std::string> cids;
    if (auto* a = cr["capsula"].as_array())
      for (auto& n : *a) {
        const toml::table* t = n.as_table();
        if (!t) continue;
        CapsulaDef c;
        c.id = (*t)["id"].value_or<std::string>("");
        c.es = (*t)["es"].value_or<std::string>(std::string(c.id)), c.en = (*t)["en"].value_or<std::string>(std::string(c.id));
        const std::string dc = "capsula " + c.id;
        if (c.id.empty() || !cids.insert(c.id).second) { err += "capsula: id vacio o repetido '" + c.id + "'\n"; continue; }
        const auto opc = [&](const char* k, int lo, int hi) { return t->contains(k) ? l.Num(*t, k, lo, hi, dc) : 0; };
        c.vida = opc("vida", -50, 300), c.ataque = opc("ataque", -50, 300), c.defensa = opc("defensa", -50, 300), c.velocidad = opc("velocidad", -50, 100);
        c.ki_inicial = opc("ki_inicial", 0, 100), c.ki_max = opc("ki_max", -50, 0), c.alcance = opc("alcance", 0, 2), c.tecnica = opc("tecnica", -50, 300);
        if (!(c.vida || c.ataque || c.defensa || c.velocidad || c.ki_inicial || c.ki_max || c.alcance || c.tecnica)) err += dc + ": no hace nada\n";
        e.capsulas.push_back(c);
      }
    if (e.capsulas.size() < 8 || e.capsulas.size() > size_t(kMaxCaps)) err += "tactics_capsulas.toml: debe haber entre 8 y " + std::to_string(kMaxCaps) + " capsulas\n";
  }
  if (auto* a = raiz["sinergia"].as_array())
    for (auto& n : *a) {
      const toml::table* t = n.as_table();
      if (!t) continue;
      SinergiaDef s;
      s.id = (*t)["id"].value_or<std::string>("");
      s.es = (*t)["es"].value_or<std::string>(std::string(s.id));
      s.en = (*t)["en"].value_or<std::string>(std::string(s.id));
      if (s.id.empty() || !ids.insert(s.id).second) { err += "sinergia: id vacio o repetido '" + s.id + "'\n"; continue; }
      if (auto* u = (*t)["umbral"].as_array())
        for (auto& m : *u)
          if (const toml::table* ut = m.as_table()) {
            Umbral b;
            b.n = l.Num(*ut, "n", 2, 6, "sinergia " + s.id);
            b.vida = l.Num(*ut, "vida", 0, 300, "sinergia " + s.id);
            b.ataque = l.Num(*ut, "ataque", 0, 300, "sinergia " + s.id);
            b.defensa = l.Num(*ut, "defensa", 0, 300, "sinergia " + s.id);
            s.umbrales.push_back(b);
          }
      if (s.umbrales.empty()) err += "sinergia " + s.id + ": sin umbrales\n";
      e.sinergias.push_back(s);
    }
  std::set<std::string> vistos;
  if (auto* a = raiz["unidad"].as_array())
    for (auto& n : *a) {
      const toml::table* t = n.as_table();
      if (!t) continue;
      UnidadEco u;
      u.id = (*t)["id"].value_or<std::string>("");
      if (!vistos.insert(u.id).second) { err += "unidad repetida '" + u.id + "'\n"; continue; }
      for (size_t i = 0; i < cat.unidades.size(); ++i) if (cat.unidades[i].id == u.id) u.cat = int(i);
      if (u.cat < 0) err += "unidad '" + u.id + "' no esta en tactics_units.toml\n";
      u.coste = l.Num(*t, "coste", 1, kMaxCoste, "unidad " + u.id);
      u.copias = e.copias_coste[u.coste - 1];
      const toml::array* s = (*t)["sinergias"].as_array();
      if (!s || s->size() != 2) { err += "unidad " + u.id + ": 'sinergias' debe tener exactamente 2\n"; e.unidades.push_back(u); continue; }
      for (size_t i = 0; i < 2; ++i) {
        const std::string sid = s->at(i).value_or<std::string>("");
        for (size_t k = 0; k < e.sinergias.size(); ++k) if (e.sinergias[k].id == sid) u.sin[i] = int(k);
        if (u.sin[i] < 0) err += "unidad " + u.id + ": sinergia desconocida '" + sid + "'\n";
      }
      if (u.sin[0] >= 0 && u.sin[0] == u.sin[1]) err += "unidad " + u.id + ": las dos sinergias son la misma\n";
      e.unidades.push_back(u);
    }
  for (const auto& cu : cat.unidades) if (!vistos.count(cu.id)) err += "la unidad '" + cu.id + "' del catalogo no tiene coste ni sinergias\n";
  for (size_t k = 0; k < e.sinergias.size(); ++k) {   // cada umbral necesita suficientes unidades DISTINTAS
    int miembros = 0;
    for (const UnidadEco& u : e.unidades) miembros += u.sin[0] == int(k) || u.sin[1] == int(k);
    int ant = 0;
    for (const Umbral& b : e.sinergias[k].umbrales) {
      if (b.n > miembros) err += "sinergia " + e.sinergias[k].id + ": umbral " + std::to_string(b.n) + " pero solo hay " + std::to_string(miembros) + " unidades distintas\n";
      if (b.n <= ant) err += "sinergia " + e.sinergias[k].id + ": los umbrales deben crecer\n";
      ant = b.n;
    }
  }
  if (!err.empty()) { *error = err; return false; }
  *out = std::move(e);
  return true;
}

Partida::Partida(const Catalogo& cat, const Economia& eco, uint64_t semilla, int jugadores)
    : cat_(&cat), eco_(&eco), semilla_(semilla), n_(jugadores > 0 ? std::min(jugadores, kMaxJug) : eco.jugadores), azar_(sim::Flujo(semilla, 5)) {
  for (const UnidadEco& u : eco.unidades) libres_.push_back(u.copias);
  for (int p = 0; p < n_; ++p) {
    tienda_[p] = sim::Flujo(semilla, p == 0 ? 2 : p == 1 ? 3 : 10 + uint32_t(p));
    bot_[p] = sim::Flujo(semilla, p == 1 ? 4 : 20 + uint32_t(p));
    j_[p].bot = p > 0;
    j_[p].calidad = p > 0 && size_t(p) <= eco.calidad_bots.size() ? eco.calidad_bots[size_t(p - 1)] : 100;
    j_[p].oro = eco.oro_inicial;
    j_[p].ganado = eco.oro_inicial;
    j_[p].vida = eco.vida_entrenador;
  }
  IniciarPreparacion();
}

int Partida::Vivos() const {
  int v = 0;
  for (int p = 0; p < n_; ++p) v += j_[p].vivo;
  return v;
}

int Partida::Valor(int p) const {
  int v = 0;
  for (const Pieza& z : j_[p].pz) if (z.hay()) v += eco_->unidades[size_t(z.unidad)].coste * Peso(z.estrellas);
  return v;
}
int Partida::EnTablero(int p) const {
  int n = 0;
  for (int s = kBanco; s < kSlots; ++s) n += j_[p].pz[s].hay();
  return n;
}
int Partida::Sitio(int p, int) const {
  for (int s = 0; s < kBanco; ++s) if (!j_[p].pz[s].hay()) return s;
  return -1;
}

std::vector<SinActiva> Partida::Sinergias(int p) const {
  std::vector<std::set<int>> miembros(eco_->sinergias.size());
  for (int s = kBanco; s < kSlots; ++s)
    if (j_[p].pz[s].hay()) for (int k : eco_->unidades[size_t(j_[p].pz[s].unidad)].sin) miembros[size_t(k)].insert(j_[p].pz[s].unidad);
  std::vector<SinActiva> r;
  for (size_t k = 0; k < miembros.size(); ++k) {
    if (miembros[k].empty()) continue;
    SinActiva a{int(k), int(miembros[k].size()), -1};
    const auto& um = eco_->sinergias[k].umbrales;
    for (size_t i = 0; i < um.size(); ++i) if (um[i].n <= a.cuenta) a.umbral = int(i);
    r.push_back(a);
  }
  return r;
}

sim::DefUnidad Partida::Def(int, const Pieza& pz, const std::vector<SinActiva>& sin) const {
  const UnidadEco& ue = eco_->unidades[size_t(pz.unidad)];
  sim::DefUnidad d = cat_->unidades[size_t(ue.cat)];
  const int m = eco_->mult_estrella[pz.estrellas - 1];
  int bv = 0, ba = 0, bd = 0;
  for (const SinActiva& a : sin)
    if (a.umbral >= 0 && (a.sinergia == ue.sin[0] || a.sinergia == ue.sin[1])) {
      const Umbral& u = eco_->sinergias[size_t(a.sinergia)].umbrales[size_t(a.umbral)];
      bv += u.vida, ba += u.ataque, bd += u.defensa;
    }
  int vel = 0, tec = 0;
  for (int c : pz.caps) {
    if (c < 0) continue;
    const CapsulaDef& k = eco_->capsulas[size_t(c)];
    bv += k.vida, ba += k.ataque, bd += k.defensa, vel += k.velocidad, tec += k.tecnica;
    d.ki_inicial += k.ki_inicial, d.ki_max = std::max(20, d.ki_max * (100 + k.ki_max) / 100), d.alcance += k.alcance;
  }
  d.vida = d.vida * m / 100 * (100 + bv) / 100;
  d.ataque = d.ataque * m / 100 * (100 + ba) / 100;
  d.defensa = d.defensa * (100 + bd) / 100;
  d.velocidad = std::max(1, d.velocidad * (100 + vel) / 100);
  d.tecnica.dano = d.tecnica.dano * (100 + tec) / 100;
  return d;
}

void Partida::DevolverTienda(int p) {
  for (int& c : j_[p].tienda) {
    if (c >= 0) ++libres_[size_t(c)];
    c = -1;
  }
}

void Partida::Surtir(int p) {
  Jugador& j = j_[p];
  const auto& w = eco_->peso_coste[size_t(j.nivel - 1)];
  for (int c = 0; c < eco_->tienda; ++c) {
    int disp[kMaxCoste] = {};   // copias libres por coste
    for (size_t u = 0; u < libres_.size(); ++u) disp[eco_->unidades[u].coste - 1] += libres_[u];
    int total = 0;
    for (int t = 0; t < kMaxCoste; ++t) total += disp[t] ? w[size_t(t)] : 0;
    bool uniforme = total == 0;   // ningun coste con peso tiene copias: cualquiera que quede
    if (uniforme) for (int t = 0; t < kMaxCoste; ++t) total += disp[t] ? 1 : 0;
    if (total == 0) { j.tienda[c] = -1; continue; }
    int r = int(tienda_[p].Hasta(uint32_t(total))), coste = 0;
    for (int t = 0; t < kMaxCoste; ++t) {
      const int pt = !disp[t] ? 0 : uniforme ? 1 : w[size_t(t)];
      if (r < pt) { coste = t + 1; break; }
      r -= pt;
    }
    r = int(tienda_[p].Hasta(uint32_t(disp[coste - 1])));
    for (size_t u = 0; u < libres_.size(); ++u) {
      if (eco_->unidades[u].coste != coste) continue;
      if (r < libres_[u]) { --libres_[u]; j.tienda[c] = int(u); break; }
      r -= libres_[u];
    }
  }
}

void Partida::IniciarPreparacion() {
  fase_ = Fase::Preparacion;
  sim_.reset();
  comb_.clear();
  duelos_.clear();
  res_.clear();
  duelo_h_ = -1;
  for (int k = 0; k < n_; ++k) {   // el orden de reparto rota cada ronda: nadie elige siempre primero
    const int p = (ronda_ + k) % n_;
    if (!j_[p].vivo) continue;
    if (j_[p].bloq) continue;   // tienda bloqueada: se queda como estaba
    DevolverTienda(p);
    Surtir(p);
  }
  for (int p = 1; p < n_; ++p) if (j_[p].vivo) Jugar(p);
  neutral_ = EsNeutral(ronda_);
  if (neutral_) EmparejarNeutral();
  else Emparejar();
}

// H10: ronda neutral. Todos los vivos luchan contra el mismo equipo de personajes de coste 1 (los mas debiles del catalogo), mas fuerte cada vez;
// el equipo es un tablero sin dueno (kNeutral) y se deduce solo de la semilla y la ronda. Ni da ni recibe dano: ganar da oro y una capsula.
void Partida::EmparejarNeutral() {
  Jugador& nj = j_[kNeutral];
  nj = Jugador{};
  std::vector<int> bajos;
  for (size_t u = 0; u < eco_->unidades.size(); ++u) if (eco_->unidades[u].coste == 1) bajos.push_back(int(u));
  sim::Rng r = sim::Flujo(semilla_, 500 + uint32_t(ronda_));
  const int n = std::min(6, 3 + ronda_ / 10);
  int saibaman = -1;   // H11: la pandilla neutral es de Saibaman (uno de cada dos huecos; el resto, otros de coste 1)
  for (size_t u = 0; u < eco_->unidades.size(); ++u) if (eco_->unidades[u].id == "saibaman" && eco_->unidades[u].coste == 1) saibaman = int(u);
  for (int i = 0; i < n && !bajos.empty(); ++i) {
    Pieza z;
    z.unidad = bajos[r.Hasta(uint32_t(bajos.size()))];
    if (saibaman >= 0 && i % 2 == 0) z.unidad = saibaman;
    nj.pz[kBanco + (6 - n) / 2 + i] = z;
  }
  for (int p = 0; p < n_; ++p) if (j_[p].vivo) duelos_.push_back({p, kNeutral, true});
  for (size_t k = 0; k < duelos_.size(); ++k) if (duelos_[k].a == 0) duelo_h_ = int(k);
}

// Parejas al azar sin repetir rival seguido (se prueban hasta 48 mezclas y se queda la de menos repeticiones). Con un numero impar de vivos
// sobra uno (el que menos veces ha luchado contra una copia): juega contra una copia del tablero de otro vivo, que no se lleva dano.
void Partida::Emparejar() {
  std::vector<int> v;
  for (int p = 0; p < n_; ++p) if (j_[p].vivo) v.push_back(p);
  int solo = -1;
  if (v.size() % 2) {
    int minimo = 1 << 30;
    for (int p : v) minimo = std::min(minimo, j_[p].copias_vistas);
    std::vector<int> c;
    for (int p : v) if (j_[p].copias_vistas == minimo) c.push_back(p);
    solo = c[azar_.Hasta(uint32_t(c.size()))];
    v.erase(std::find(v.begin(), v.end(), solo));
  }
  std::vector<int> mejor = v;
  int pen_mejor = 1 << 30;
  for (int t = 0; t < 48 && pen_mejor > 0; ++t) {
    std::vector<int> w = v;
    for (size_t i = w.size(); i > 1; --i) std::swap(w[i - 1], w[azar_.Hasta(uint32_t(i))]);
    int pen = 0;
    for (size_t i = 0; i + 1 < w.size(); i += 2) pen += j_[w[i]].ultimo_rival == w[i + 1] || j_[w[i + 1]].ultimo_rival == w[i];
    if (pen < pen_mejor) pen_mejor = pen, mejor = w;
  }
  for (size_t i = 0; i + 1 < mejor.size(); i += 2) {
    const int a = std::min(mejor[i], mejor[i + 1]), b = std::max(mejor[i], mejor[i + 1]);
    duelos_.push_back({a, b, false});
    j_[a].ultimo_rival = b, j_[b].ultimo_rival = a;
  }
  if (solo >= 0) {
    std::vector<int> c;
    for (int p = 0; p < n_; ++p) if (j_[p].vivo && p != solo && p != j_[solo].ultimo_rival) c.push_back(p);
    if (c.empty()) for (int p = 0; p < n_; ++p) if (j_[p].vivo && p != solo) c.push_back(p);
    const int origen = c[azar_.Hasta(uint32_t(c.size()))];
    duelos_.push_back({solo, origen, true});
    j_[solo].ultimo_rival = origen;
    ++j_[solo].copias_vistas;
  }
  std::sort(duelos_.begin(), duelos_.end(), [](const Duelo& x, const Duelo& y) { return x.a < y.a; });
  for (size_t k = 0; k < duelos_.size(); ++k) if (duelos_[k].a == 0) duelo_h_ = int(k);
}

void Partida::Devolver(Jugador& j, int slot) {
  for (int& c : j.pz[slot].caps) {
    if (c >= 0) ++j.inv[c];
    c = -1;
  }
}

// La pieza que desaparece al combinar deja sus capsulas en los huecos libres de la que se queda; lo que no cabe vuelve al inventario.
void Partida::Absorber(Jugador& j, int queda, int se_va) {
  for (int& c : j.pz[se_va].caps) {
    if (c < 0) continue;
    int* libre = nullptr;
    for (int& h : j.pz[queda].caps) if (h < 0) { libre = &h; break; }
    if (libre) *libre = c;
    else ++j.inv[c];
  }
  j.pz[se_va] = Pieza{};
}

void Partida::DarCapsula(int p) {
  const int c = int(tienda_[p].Hasta(uint32_t(eco_->capsulas.size())));
  ++j_[p].inv[c], ++j_[p].caps_total;
}

Res Partida::ComprarCapsula(int p) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  Jugador& j = j_[p];
  if (j.oro < eco_->precio_capsula) return Res::SinOro;
  j.oro -= eco_->precio_capsula;
  j.gastado += eco_->precio_capsula;
  DarCapsula(p);
  return Res::Ok;
}

Res Partida::Equipar(int p, int slot, int hueco, int capsula) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  Jugador& j = j_[p];
  if (slot < 0 || slot >= kSlots || !j.pz[slot].hay() || hueco < 0 || hueco >= kHuecosCaps || capsula < 0 || capsula >= int(eco_->capsulas.size()) || j.inv[capsula] == 0) return Res::Invalido;
  int& h = j.pz[slot].caps[hueco];
  --j.inv[capsula];
  if (h >= 0) ++j.inv[h];
  h = capsula;
  return Res::Ok;
}

Res Partida::Quitar(int p, int slot, int hueco) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  Jugador& j = j_[p];
  if (slot < 0 || slot >= kSlots || !j.pz[slot].hay() || hueco < 0 || hueco >= kHuecosCaps || j.pz[slot].caps[hueco] < 0) return Res::Invalido;
  ++j.inv[j.pz[slot].caps[hueco]];
  j.pz[slot].caps[hueco] = -1;
  return Res::Ok;
}

void Partida::SubirNivel(Jugador& j) {
  while (j.nivel < eco_->NivelMax() && j.exp >= eco_->exp_subir[size_t(j.nivel - 1)]) j.exp -= eco_->exp_subir[size_t(j.nivel - 1)], ++j.nivel;
}

// Tres piezas iguales (misma unidad y estrellas) pasan a una de mas estrellas: se queda la del tablero (la de menor casilla), si no la
// del banquillo; las otras dos desaparecen. El peso en copias se conserva (3 x 1 = 1 x 3), asi que la reserva no cambia.
void Partida::Combinar(int p) {
  Jugador& j = j_[p];
  for (bool cambio = true; cambio;) {
    cambio = false;
    for (int u = 0; u < int(eco_->unidades.size()) && !cambio; ++u)
      for (int est = 1; est < kMaxEstrellas && !cambio; ++est) {
        std::vector<int> v;
        for (int s = kBanco; s < kSlots; ++s) if (j.pz[s].unidad == u && j.pz[s].estrellas == est) v.push_back(s);
        for (int s = 0; s < kBanco; ++s) if (j.pz[s].unidad == u && j.pz[s].estrellas == est) v.push_back(s);
        if (v.size() < 3) continue;
        Absorber(j, v[0], v[1]), Absorber(j, v[0], v[2]);
        ++j.pz[v[0]].estrellas;
        cambio = true;
      }
  }
}

Res Partida::Comprar(int p, int carta) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  Jugador& j = j_[p];
  if (carta < 0 || carta >= eco_->tienda || j.tienda[carta] < 0) return Res::Invalido;
  const int u = j.tienda[carta], coste = eco_->unidades[size_t(u)].coste;
  if (coste > j.oro) return Res::SinOro;
  const int hueco = Sitio(p, u);
  std::vector<int> iguales;   // piezas de 1 estrella de esa unidad: con dos, esta compra completa una mejora y no necesita hueco
  for (int s = kBanco; s < kSlots; ++s) if (j.pz[s].unidad == u && j.pz[s].estrellas == 1) iguales.push_back(s);
  for (int s = 0; s < kBanco; ++s) if (j.pz[s].unidad == u && j.pz[s].estrellas == 1) iguales.push_back(s);
  if (hueco < 0 && iguales.size() < 2) return Res::SinSitio;
  j.oro -= coste;
  j.tienda[carta] = -1;
  if (hueco >= 0) {
    j.pz[hueco] = Pieza{u, 1};
  } else {
    Absorber(j, iguales[0], iguales[1]);
    j.pz[iguales[0]].estrellas = 2;
  }
  Combinar(p);
  return Res::Ok;
}

int Partida::PonerPrueba(int p, int unidad, int estrellas) {
  Jugador& j = j_[p];
  if (unidad < 0 || unidad >= int(eco_->unidades.size()) || estrellas < 1 || estrellas > kMaxEstrellas || libres_[size_t(unidad)] < Peso(estrellas)) return -1;
  for (int s = kBanco; s < kSlots; ++s)
    if (!j.pz[s].hay()) {
      libres_[size_t(unidad)] -= Peso(estrellas);
      j.pz[s] = Pieza{unidad, estrellas};
      return s;
    }
  return -1;
}

bool Partida::EstrellasPrueba(int p, int slot, int estrellas) {
  Jugador& j = j_[p];
  if (slot < 0 || slot >= kSlots || !j.pz[slot].hay() || estrellas < 1 || estrellas > kMaxEstrellas) return false;
  const int u = j.pz[slot].unidad, dif = Peso(estrellas) - Peso(j.pz[slot].estrellas);
  if (libres_[size_t(u)] < dif) return false;
  libres_[size_t(u)] -= dif;
  j.pz[slot].estrellas = estrellas;
  return true;
}

Res Partida::Vender(int p, int slot) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  Jugador& j = j_[p];
  if (slot < 0 || slot >= kSlots || !j.pz[slot].hay()) return Res::Invalido;
  const int peso = Peso(j.pz[slot].estrellas);
  j.oro += eco_->unidades[size_t(j.pz[slot].unidad)].coste * peso;
  libres_[size_t(j.pz[slot].unidad)] += peso;
  Devolver(j, slot);
  j.pz[slot] = Pieza{};
  return Res::Ok;
}

Res Partida::Mover(int p, int de, int a) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  Jugador& j = j_[p];
  if (de < 0 || de >= kSlots || a < 0 || a >= kSlots || de == a || !j.pz[de].hay()) return Res::Invalido;
  if (de < kBanco && a >= kBanco && !j.pz[a].hay() && EnTablero(p) >= TopeTablero(p)) return Res::Tope;
  std::swap(j.pz[de], j.pz[a]);
  return Res::Ok;
}

Res Partida::Actualizar(int p) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  Jugador& j = j_[p];
  if (j.oro < eco_->precio_actualizar) return Res::SinOro;
  j.oro -= eco_->precio_actualizar;
  j.gastado += eco_->precio_actualizar;
  j.bloq = false;
  DevolverTienda(p);
  Surtir(p);
  return Res::Ok;
}

Res Partida::Bloquear(int p) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  j_[p].bloq = !j_[p].bloq;
  return Res::Ok;
}

int Partida::OroRacha(int racha) const {
  const int n = std::abs(racha);
  return n < eco_->racha_desde ? 0 : std::min(eco_->racha_tope, 1 + (n - eco_->racha_desde) / eco_->racha_cada);
}

Res Partida::ComprarExp(int p) {
  if (fase_ != Fase::Preparacion) return Res::FaseIncorrecta;
  Jugador& j = j_[p];
  if (j.nivel >= eco_->NivelMax()) return Res::Tope;
  if (j.oro < eco_->precio_exp) return Res::SinOro;
  j.oro -= eco_->precio_exp;
  j.gastado += eco_->precio_exp;
  j.exp += eco_->exp_compra;
  SubirNivel(j);
  return Res::Ok;
}

void Partida::Listo() {
  if (fase_ != Fase::Preparacion) return;
  for (Jugador& j : j_) j.oro_cierre = j.oro;
  comb_.clear();
  res_.assign(duelos_.size(), sim::Resultado{});
  std::vector<std::vector<SinActiva>> sin(static_cast<size_t>(kMaxJug) + 1);
  for (int p = 0; p < n_; ++p) if (j_[p].vivo) sin[size_t(p)] = Sinergias(p);
  for (size_t k = 0; k < duelos_.size(); ++k) {   // el duelo del humano se queda en sim_ para dibujarlo; los demas se juegan enteros ya, sin dibujar
    const Duelo& d = duelos_[k];
    sim::Sim s(sim::Flujo(semilla_, 1000 + uint32_t(ronda_) * 16 + uint32_t(k)).Siguiente());
    const bool humano = int(k) == duelo_h_;
    const int duenos[2] = {d.a, d.b};
    for (int e = 0; e < 2; ++e) {
      const int p = duenos[e];
      for (int sl = kBanco; sl < kSlots; ++sl) {
        const Pieza& z = j_[p].pz[sl];
        if (!z.hay()) continue;
        const int idx = sl - kBanco, fila = idx / sim::kAncho, col = idx % sim::kAncho;
        sim::DefUnidad def = Def(p, z, sin[size_t(p)]);
        if (p == kNeutral) def.vida = def.vida * NeutralPct() / 100, def.ataque = def.ataque * NeutralPct() / 100;
        if (s.Anadir(def, e, {e == 0 ? col : sim::kAncho - 1 - col, e == 0 ? 3 + fila : 2 - fila}) >= 0 && humano) comb_.push_back({e, sl, z.unidad, z.estrellas});
      }
    }
    if (humano) {
      sim_.emplace(std::move(s));
    } else {
      while (!s.Terminada()) s.Paso();
      res_[k] = s.Resumen();
    }
  }
  fase_ = Fase::Combate;
}

// Quita a un entrenador eliminado: sus unidades y ofertas vuelven a la reserva compartida.
void Partida::Retirar(int p) {
  DevolverTienda(p);
  for (Pieza& z : j_[p].pz) {
    if (z.hay()) libres_[size_t(z.unidad)] += Peso(z.estrellas);
    z = Pieza{};
  }
  std::fill(std::begin(j_[p].inv), std::end(j_[p].inv), 0);
  j_[p].caps_total = 0;
  j_[p].bloq = false;
}

void Partida::TerminarCombate() {
  if (fase_ != Fase::Combate || (sim_ && !sim_->Terminada())) return;
  if (sim_) res_[size_t(duelo_h_)] = sim_->Resumen();
  for (int p = 0; p < n_; ++p) dano_[p] = 0, j_[p].gano = 0;
  const int base = eco_->dano_base + (ronda_ - 1) / eco_->dano_cada_rondas;
  ganador_ronda_ = -1, dano_rival_ = 0;
  for (size_t k = 0; k < duelos_.size(); ++k) {
    const Duelo& d = duelos_[k];
    const sim::Resultado& r = res_[k];
    if (d.b == kNeutral) {   // ronda neutral: sin dano ni racha; ganar paga oro y una capsula
      if (r.ganador == 0) {
        j_[d.a].oro += eco_->neutral_oro, j_[d.a].ganado += eco_->neutral_oro;
        DarCapsula(d.a);
      }
      if (int(k) == duelo_h_) ganador_ronda_ = r.ganador, dano_rival_ = 0;
      continue;
    }
    int d0 = 0, d1 = 0;
    if (r.ganador < 0) d0 = d1 = std::min(base, eco_->dano_tope);
    else if (r.ganador == 0) d1 = std::min(base + eco_->dano_superviviente * r.vivas[0], eco_->dano_tope);
    else d0 = std::min(base + eco_->dano_superviviente * r.vivas[1], eco_->dano_tope);
    dano_[d.a] = d0, j_[d.a].gano = r.ganador == 0;
    if (!d.copia) dano_[d.b] = d1, j_[d.b].gano = r.ganador == 1;
    const auto racha = [&](Jugador& j, int resultado) { j.racha = resultado == 0 ? 0 : resultado > 0 ? std::max(1, j.racha + 1) : std::min(-1, j.racha - 1); };
    racha(j_[d.a], r.ganador < 0 ? 0 : r.ganador == 0 ? 1 : -1);
    if (!d.copia) racha(j_[d.b], r.ganador < 0 ? 0 : r.ganador == 1 ? 1 : -1);
    if (int(k) == duelo_h_) ganador_ronda_ = r.ganador, dano_rival_ = d1;
  }
  const int vivos_antes = Vivos();
  std::vector<std::pair<int, int>> caidos;   // (valor del equipo al caer, jugador)
  for (int p = 0; p < n_; ++p) {
    if (!j_[p].vivo) continue;
    j_[p].vida = std::max(0, j_[p].vida - dano_[p]);
    if (j_[p].vida == 0) caidos.emplace_back(Valor(p), p);
  }
  // los que caen a la vez: peor puesto el de menos valor (a igualdad, el de mayor indice)
  std::sort(caidos.begin(), caidos.end(), [](const auto& x, const auto& y) { return x.first != y.first ? x.first < y.first : x.second > y.second; });
  for (size_t i = 0; i < caidos.size(); ++i) {
    Jugador& j = j_[caidos[i].second];
    j.vivo = false, j.puesto = vivos_antes - int(i), j.ronda_baja = ronda_;
    Retirar(caidos[i].second);
  }
  if (Vivos() <= 1) {
    campeon_ = -1;
    for (int p = 0; p < n_; ++p) if (j_[p].vivo) campeon_ = p, j_[p].puesto = 1;
    if (campeon_ < 0 && !caidos.empty()) campeon_ = caidos.back().second;   // todos cayeron a la vez: gana el de mas valor
  }
  fase_ = Fase::Resultado;   // sim_ se queda: la presentacion aun lee sus eventos; se libera al abrir la siguiente preparacion
}

void Partida::Ingresos() {
  for (int p = 0; p < n_; ++p) {
    Jugador& j = j_[p];
    if (!j.vivo) continue;
    const int interes = std::min(eco_->interes_tope, j.oro_cierre / eco_->interes_cada);
    const int ingreso = eco_->ingreso_ronda + (j.gano ? eco_->ingreso_victoria : 0) + interes + OroRacha(j.racha);
    j.oro += ingreso, j.ganado += ingreso;
    j.exp += eco_->exp_ronda;
    SubirNivel(j);
    if (eco_->capsula_cada > 0 && ronda_ % eco_->capsula_cada == 0) DarCapsula(p);
  }
}

// Tras el resultado: fin si queda uno; si el humano ya cayo, el resto del torneo se juega solo (sin dibujar) para tener la clasificacion completa.
void Partida::SiguienteRonda() {
  if (fase_ != Fase::Resultado) return;
  for (;;) {
    if (Vivos() <= 1) {
      fase_ = Fase::Fin;
      return;
    }
    Ingresos();
    ++ronda_;
    IniciarPreparacion();
    if (j_[0].vivo) return;
    Listo();
    TerminarCombate();
  }
}

std::vector<int> Partida::Clasificacion() const {
  std::vector<int> v(static_cast<size_t>(n_));
  for (int p = 0; p < n_; ++p) v[size_t(p)] = p;
  std::sort(v.begin(), v.end(), [&](int x, int y) { return j_[x].puesto < j_[y].puesto; });
  return v;
}

// ---- Bot: solo mira su propio estado y sus cartas (nada del rival ni del futuro) y juega con las mismas acciones que el humano.
int Partida::Puntua(int p, int u) const {
  const Jugador& j = j_[p];
  const UnidadEco& ue = eco_->unidades[size_t(u)];
  int s = ue.coste * 10, iguales = 0, dos = 0;
  std::set<int> propias;
  for (const Pieza& z : j.pz) {
    if (!z.hay()) continue;
    propias.insert(z.unidad);
    if (z.unidad == u && z.estrellas == 1) ++iguales;
    if (z.unidad == u && z.estrellas == 2) ++dos;
  }
  s += iguales == 2 ? 100 : iguales == 1 ? 40 : 0;
  if (dos >= 2) s += 100;   // persigue la 3a estrella: con 2 copias de 2 estrellas retiene oro (s>=100 salta la reserva) y compra esa unidad
  for (int k : ue.sin) for (int o : propias) if (o != u && (eco_->unidades[size_t(o)].sin[0] == k || eco_->unidades[size_t(o)].sin[1] == k)) s += 8;
  if (EnTablero(p) + int(propias.size()) < TopeTablero(p) && iguales == 0) s += 15;
  return s;
}

// La dificultad de un bot es su calidad de decision q (0-100): con probabilidad (100-q) % compra una carta al azar en vez de la de mejor
// puntuacion y coloca a las unidades sin orden; por debajo de 30 no sube de nivel y por debajo de 50 no guarda oro para el interes.
void Partida::Jugar(int p) {
  Jugador& j = j_[p];
  sim::Rng& az = bot_[p];
  const int q = j.calidad, fallo = 100 - q;
  const auto reserva = [&] { const bool guarda = q >= 50 && ronda_ >= 4 && EnTablero(p) >= TopeTablero(p); if (reserva_prueba_[p] >= 0) return guarda ? reserva_prueba_[p] : 0; return guarda ? std::min(eco_->interes_tope, j.oro / eco_->interes_cada) * eco_->interes_cada : 0; };
  const auto comprar = [&] {
    for (bool compro = true; compro;) {
      compro = false;
      int mejor = -1, ms = 0;
      std::vector<int> baratas;
      for (int c = 0; c < eco_->tienda; ++c) {
        if (j.tienda[c] < 0) continue;
        const int s = Puntua(p, j.tienda[c]), coste = eco_->unidades[size_t(j.tienda[c])].coste;
        if (coste > j.oro) continue;
        baratas.push_back(c);
        if ((s < 100 && j.oro - coste < reserva()) || s <= ms) continue;
        mejor = c, ms = s;
      }
      if (!baratas.empty() && int(az.Hasta(100)) < fallo) mejor = baratas[az.Hasta(uint32_t(baratas.size()))];
      if (mejor >= 0 && Comprar(p, mejor) == Res::Ok) compro = true;
    }
  };
  comprar();
  for (int i = 0; i < 2 && !j.bloq && j.oro - eco_->precio_actualizar >= reserva() && int(az.Hasta(100)) < q / 2; ++i) {
    if (Actualizar(p) != Res::Ok) break;
    comprar();
  }
  while (q >= 30 && EnTablero(p) >= TopeTablero(p) && j.oro - eco_->precio_exp >= reserva() && ComprarExp(p) == Res::Ok) {}
  if (q >= 60 && EnTablero(p) >= TopeTablero(p) && j.oro - eco_->precio_capsula >= reserva()) ComprarCapsula(p);   // con el tablero lleno, una capsula por ronda
  // Colocar: las mejores al tablero (mas estrellas, mas coste, mas sinergia con el resto); cuerpo a cuerpo delante y a distancia detras.
  std::vector<std::tuple<int, int, int>> todas;   // (-valor, slot, unidad)
  std::set<int> unidades;
  for (const Pieza& z : j.pz) if (z.hay()) unidades.insert(z.unidad);
  for (int s = 0; s < kSlots; ++s) {
    if (!j.pz[s].hay()) continue;
    const UnidadEco& ue = eco_->unidades[size_t(j.pz[s].unidad)];
    int v = Peso(j.pz[s].estrellas) * ue.coste * 10;
    for (int k : ue.sin) for (int o : unidades) if (o != j.pz[s].unidad && (eco_->unidades[size_t(o)].sin[0] == k || eco_->unidades[size_t(o)].sin[1] == k)) v += 5;
    todas.emplace_back(-v - int(az.Hasta(uint32_t(fallo + 1))) * 3, s, j.pz[s].unidad);   // el ruido de un bot flojo cambia a quien pone en el tablero
  }
  std::sort(todas.begin(), todas.end());
  const size_t n = std::min<size_t>(todas.size(), size_t(TopeTablero(p)));
  std::vector<Pieza> cuerpo, distancia, banco;
  for (size_t i = 0; i < todas.size(); ++i) {
    const Pieza z = j.pz[std::get<1>(todas[i])];
    (i >= n ? banco : cat_->unidades[size_t(eco_->unidades[size_t(z.unidad)].cat)].alcance <= 1 || int(az.Hasta(100)) < fallo ? cuerpo : distancia).push_back(z);
  }
  for (Pieza& z : j.pz) z = Pieza{};
  const auto cuadros = [](bool delante) {
    std::vector<int> v;
    for (int k = 0; k < 3; ++k) for (int c : kColumnasCentro) v.push_back((delante ? k : 2 - k) * 6 + c);
    return v;
  };
  std::set<int> usadas;
  const bool mal = colocar_mal_[p];   // solo pruebas: cuerpo a cuerpo detras y a distancia delante
  for (const Pieza& z : cuerpo) for (int c : cuadros(!mal)) if (usadas.insert(c).second) { j.pz[kBanco + c] = z; break; }
  for (const Pieza& z : distancia) for (int c : cuadros(mal)) if (usadas.insert(c).second) { j.pz[kBanco + c] = z; break; }
  for (size_t i = 0; i < banco.size(); ++i) j.pz[i] = banco[i];
  // Capsulas: las del inventario, a las piezas del tablero con mas estrellas (cada una con probabilidad q %; las que no se ponen se quedan guardadas).
  std::vector<int> en_tablero;
  for (int s = kBanco; s < kSlots; ++s) if (j.pz[s].hay()) en_tablero.push_back(s);
  std::stable_sort(en_tablero.begin(), en_tablero.end(), [&](int x, int y) { return j.pz[x].estrellas > j.pz[y].estrellas; });
  for (int s : en_tablero)
    for (int h = 0; h < kHuecosCaps; ++h) {
      if (j.pz[s].caps[h] >= 0) continue;
      for (int c = 0; c < int(eco_->capsulas.size()); ++c)
        if (j.inv[c] > 0 && int(az.Hasta(100)) < q) { Equipar(p, s, h, c); break; }
    }
}

// ---- Guardado: todo el estado de la preparacion en una lista de enteros (con el estado de cada generador de azar), dentro de un TOML con
// cabecera legible (ronda, vivos, vida) y la huella FNV de la lista para detectar un fichero cortado o editado.
std::vector<int64_t> Partida::Estado() const {
  std::vector<int64_t> v{n_, int64_t(libres_.size()), ronda_, campeon_, int64_t(eco_->capsulas.size())};
  for (int p = 0; p < n_; ++p) {
    const Jugador& j = j_[p];
    for (int64_t x : {int64_t(j.bot), int64_t(j.calidad), int64_t(j.vivo), int64_t(j.puesto), int64_t(j.ronda_baja), int64_t(j.ultimo_rival), int64_t(j.copias_vistas),
                      int64_t(j.gano), int64_t(j.racha), int64_t(j.bloq), int64_t(j.oro), int64_t(j.exp), int64_t(j.nivel), int64_t(j.vida), int64_t(j.oro_cierre), int64_t(j.ganado), int64_t(j.gastado)})
      v.push_back(x);
    for (int c : j.tienda) v.push_back(c);
    v.push_back(j.caps_total);
    for (size_t c = 0; c < eco_->capsulas.size(); ++c) v.push_back(j.inv[c]);
    for (const Pieza& z : j.pz) v.push_back(z.unidad), v.push_back(z.estrellas), v.push_back(z.caps[0]), v.push_back(z.caps[1]);
  }
  for (int l : libres_) v.push_back(l);
  for (int p = 0; p < n_; ++p) v.push_back(int64_t(tienda_[p].estado)), v.push_back(int64_t(bot_[p].estado));
  v.push_back(int64_t(azar_.estado));
  v.push_back(int64_t(duelos_.size()));
  for (const Duelo& d : duelos_) v.push_back(d.a), v.push_back(d.b), v.push_back(d.copia);
  return v;
}

uint64_t Partida::Huella() const {
  uint64_t h = 1469598103934665603ull;
  const auto mezcla = [&](int64_t x) { for (int i = 0; i < 8; ++i) h = (h ^ uint64_t((x >> (8 * i)) & 0xff)) * 1099511628211ull; };
  mezcla(int64_t(semilla_)), mezcla(int64_t(fase_));
  for (int64_t x : Estado()) mezcla(x);
  return h;
}

std::string Partida::Guardar() const {
  if (fase_ != Fase::Preparacion) return {};
  const std::vector<int64_t> v = Estado();
  std::ostringstream o;
  o << "# Dragon Tactics: partida guardada (no editar a mano: se rechaza si no coincide la huella).\nversion = 2\n";
  o << "semilla = " << int64_t(semilla_) << "\nronda = " << ronda_ << "\njugadores = " << n_ << "\nvivos = " << Vivos() << "\nvida = " << j_[0].vida << "\n";
  o << "huella = " << int64_t(Huella()) << "\nestado = [";
  for (size_t i = 0; i < v.size(); ++i) o << (i ? ", " : "") << v[i];
  o << "]\n";
  return o.str();
}

bool Partida::Restaurar(const std::vector<int64_t>& v, std::string* error) {
  size_t o = 0;
  bool bien = true;
  const auto leer = [&](int64_t lo, int64_t hi) {
    if (o >= v.size()) { bien = false; return lo; }
    const int64_t x = v[o++];
    if (x < lo || x > hi) bien = false;
    return std::clamp(x, lo, hi);
  };
  const int n = int(leer(2, kMaxJug)), nu = int(leer(0, 1000)), ronda = int(leer(1, 100000));
  const int campeon = int(leer(-1, kMaxJug - 1)), nc = int(leer(0, kMaxCaps));
  const int U = int(eco_->unidades.size()), C = int(eco_->capsulas.size());
  if (!bien || n != n_ || nu != U || nc != C) {
    *error = "la partida guardada es de otra configuracion (jugadores o unidades distintos)";
    return false;
  }
  Jugador js[kMaxJug];
  for (int p = 0; p < n; ++p) {
    Jugador& j = js[p];
    j.bot = leer(0, 1), j.calidad = int(leer(0, 100)), j.vivo = leer(0, 1), j.puesto = int(leer(0, kMaxJug)), j.ronda_baja = int(leer(0, 100000));
    j.ultimo_rival = int(leer(-1, kMaxJug - 1)), j.copias_vistas = int(leer(0, 100000)), j.gano = int(leer(0, 1)), j.racha = int(leer(-100000, 100000)), j.bloq = leer(0, 1), j.oro = int(leer(0, 1000000)), j.exp = int(leer(0, 1000000));
    j.nivel = int(leer(1, eco_->NivelMax())), j.vida = int(leer(0, eco_->vida_entrenador)), j.oro_cierre = int(leer(0, 1000000));
    j.ganado = int(leer(0, 100000000)), j.gastado = int(leer(0, 100000000));
    for (int& c : j.tienda) c = int(leer(-1, U - 1));
    j.caps_total = int(leer(0, 1000000));
    int suma = 0;
    for (int c = 0; c < C; ++c) suma += j.inv[c] = int(leer(0, 1000000));
    for (Pieza& z : j.pz) {
      z.unidad = int(leer(-1, U - 1)), z.estrellas = int(leer(1, kMaxEstrellas));
      for (int& c : z.caps) suma += (c = int(leer(-1, C - 1))) >= 0;
      if (z.unidad < 0) {
        if (z.caps[0] >= 0 || z.caps[1] >= 0) bien = false;
        z = Pieza{};
      }
    }
    if (suma != j.caps_total) bien = false;   // las capsulas ni se crean ni se pierden
    if (!j.vivo && std::any_of(std::begin(j.pz), std::end(j.pz), [](const Pieza& z) { return z.hay(); })) bien = false;
  }
  std::vector<int> libres(static_cast<size_t>(U), 0);
  for (int& l : libres) l = int(leer(0, 1000000));
  sim::Rng tienda[kMaxJug], bot[kMaxJug], azar;
  for (int p = 0; p < n; ++p) tienda[p].estado = uint64_t(leer(INT64_MIN, INT64_MAX)), bot[p].estado = uint64_t(leer(INT64_MIN, INT64_MAX));
  azar.estado = uint64_t(leer(INT64_MIN, INT64_MAX));
  std::vector<Duelo> duelos(size_t(leer(0, kMaxJug)));
  for (Duelo& d : duelos) d.a = int(leer(0, n - 1)), d.b = int(leer(0, kMaxJug)), d.copia = leer(0, 1);
  if (!bien || o != v.size()) {
    *error = "la partida guardada esta incompleta o tiene valores imposibles";
    return false;
  }
  for (int u = 0; u < U; ++u) {   // la reserva compartida tiene que cuadrar
    int total = libres[size_t(u)];
    for (int p = 0; p < n; ++p) {
      for (int c : js[p].tienda) total += c == u;
      for (const Pieza& z : js[p].pz) total += z.unidad == u ? Peso(z.estrellas) : 0;
    }
    if (total != eco_->unidades[size_t(u)].copias) {
      *error = "la partida guardada no cuadra con las copias de '" + eco_->unidades[size_t(u)].id + "'";
      return false;
    }
  }
  for (int p = 0; p < n; ++p) {
    int en = 0;
    for (int s = kBanco; s < kSlots; ++s) en += js[p].pz[s].hay();
    if (en > eco_->tope_unidades[size_t(js[p].nivel - 1)]) { *error = "la partida guardada tiene mas unidades en el tablero que su nivel"; return false; }
  }
  for (int p = 0; p < n; ++p) j_[p] = js[p], tienda_[p] = tienda[p], bot_[p] = bot[p];
  libres_ = libres, azar_ = azar, ronda_ = ronda, campeon_ = campeon, duelos_ = duelos;
  fase_ = Fase::Preparacion, ganador_ronda_ = -1, dano_rival_ = 0, duelo_h_ = -1;
  for (int p = 0; p < kMaxJug; ++p) dano_[p] = 0;
  for (size_t k = 0; k < duelos_.size(); ++k) if (duelos_[k].a == 0) duelo_h_ = int(k);
  neutral_ = EsNeutral(ronda_);
  if (neutral_) { const auto d = duelos_; duelos_.clear(); EmparejarNeutral(); }   // el tablero neutral se rehace (mismo equipo: sale de la semilla)
  sim_.reset(), comb_.clear(), res_.clear();
  return true;
}

bool Partida::Cargar(const std::string& texto, const Catalogo& cat, const Economia& eco, std::optional<Partida>* out, std::string* error) {
  toml::table t;
  try {
    t = toml::parse(texto);
  } catch (const toml::parse_error& e) {
    *error = std::string("no se puede leer: ") + std::string(e.description());
    return false;
  }
  if (t["version"].value_or<int64_t>(0) != 2) {
    *error = "version desconocida";
    return false;
  }
  const toml::array* a = t["estado"].as_array();
  if (!a) {
    *error = "falta 'estado'";
    return false;
  }
  std::vector<int64_t> v;
  for (auto& n : *a) {
    auto x = n.value<int64_t>();
    if (!x) { *error = "'estado' tiene algo que no es un entero"; return false; }
    v.push_back(*x);
  }
  const int jug = int(t["jugadores"].value_or<int64_t>(0));
  if (jug < 2 || jug > kMaxJug) {
    *error = "numero de jugadores imposible";
    return false;
  }
  Partida p(cat, eco, uint64_t(t["semilla"].value_or<int64_t>(0)), jug);
  if (!p.Restaurar(v, error)) return false;
  if (int64_t(p.Huella()) != t["huella"].value_or<int64_t>(0)) {
    *error = "la huella no coincide: el fichero esta cortado o editado";
    return false;
  }
  out->emplace(std::move(p));
  return true;
}

// Escritura segura: se escribe ruta.tmp, la version anterior pasa a ruta.bak y el tmp ocupa su sitio.
bool GuardarArchivo(const std::string& ruta, const std::string& texto, std::string* error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path r(ruta), tmp = fs::path(ruta + ".tmp"), bak = fs::path(ruta + ".bak");
  fs::create_directories(r.parent_path(), ec);
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << texto;
    f.flush();
    if (!f) { *error = "no se pudo escribir " + tmp.string(); return false; }
  }
  if (fs::exists(r, ec)) {
    fs::copy_file(r, bak, fs::copy_options::overwrite_existing, ec);
    if (ec) { *error = "no se pudo copiar la partida anterior: " + ec.message(); return false; }
  }
  fs::rename(tmp, r, ec);
  if (ec) { *error = "no se pudo reemplazar " + ruta + ": " + ec.message(); return false; }
  return true;
}

// Carga la guardada; si esta rota prueba la copia anterior (.bak).
bool CargarArchivo(const std::string& ruta, const Catalogo& cat, const Economia& eco, std::optional<Partida>* out, std::string* error) {
  std::string primero;
  for (const char* sufijo : {"", ".bak"}) {
    std::ifstream f(ruta + sufijo, std::ios::binary);
    if (!f) continue;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string e;
    if (Partida::Cargar(ss.str(), cat, eco, out, &e)) return true;
    if (primero.empty()) primero = ruta + sufijo + ": " + e;
  }
  *error = primero.empty() ? "no hay partida guardada" : primero;
  return false;
}

}  // namespace dbz3::tactics
