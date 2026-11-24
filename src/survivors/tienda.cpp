// Dragon Budokai Survivors: progreso entre partidas y tienda (tienda.h).
#include "tienda.h"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <fstream>
#include <sstream>

#include <toml++/toml.hpp>

namespace dbz3::survivors {
namespace {
namespace fs = std::filesystem;

bool Leer(const fs::path& p, Progreso* out, std::string* aviso) {
  std::error_code ec;
  if (!fs::exists(p, ec)) return false;
  toml::table t;
  try {
    t = toml::parse_file(p.string());
  } catch (const toml::parse_error& e) {
    *aviso += p.string() + ": " + std::string(e.description()) + "\n";
    return false;
  }
  Progreso g;
  g.zeni = std::max(0, int(t["zeni"].value_or<int64_t>(0)));
  g.jefes = int(t["jefes"].value_or<int64_t>(0)), g.partidas = int(t["partidas"].value_or<int64_t>(0));
  g.victorias = int(t["victorias"].value_or<int64_t>(0));
  if (const toml::array* a = t["mejoras"].as_array())
    for (size_t k = 0; k < a->size() && k < size_t(kMejorasPerm); ++k) g.mejoras[k] = std::clamp(int(a->get(k)->value_or<int64_t>(0)), 0, kNivelMax);
  if (const toml::array* a = t["medallas"].as_array())
    for (size_t k = 0; k < a->size() && k < size_t(kMaxEscenarios); ++k) g.medalla[k] = uint8_t(std::clamp(int(a->get(k)->value_or<int64_t>(0)), 0, int(kPlatino)));
  // P2 (claves opcionales)
  bool mascaras_validas = true;
  const auto u64 = [&](const char* k, uint64_t maximo = UINT64_MAX) -> uint64_t {
    if (!t.contains(k)) return 0;
    uint64_t v = 0;
    bool valido = false;
    if (const auto s = t[k].value_exact<std::string>()) {
      std::string_view texto(*s);
      if (texto.starts_with("0x") || texto.starts_with("0X")) texto.remove_prefix(2);
      const auto r = std::from_chars(texto.data(), texto.data() + texto.size(), v, 16);
      valido = r.ec == std::errc{} && r.ptr == texto.data() + texto.size();
    } else if (const auto n = t[k].value_exact<int64_t>()) {
      v = uint64_t(*n);
      valido = *n >= 0;
    }
    if (valido && v <= maximo) return v;
    *aviso += p.string() + ": mascara invalida: " + k + "\n";
    mascaras_validas = false;
    return 0;
  };
  g.vistos_tec = u64("vistos_tec"), g.vistos_pas = u64("vistos_pas"), g.logros = u64("logros");
  g.vistos_enem = uint32_t(u64("vistos_enem", UINT32_MAX)), g.vistos_jefe = uint32_t(u64("vistos_jefe", UINT32_MAX)),
  g.luchadores = uint32_t(u64("luchadores", UINT32_MAX));
  g.desbloqueados = uint32_t(u64("desbloqueados", UINT32_MAX)) | 1u;
  if (!mascaras_validas) return false;
  g.cofres = int(t["cofres"].value_or<int64_t>(0)), g.total_derrotados = int(t["total_derrotados"].value_or<int64_t>(0));
  g.total_zeni = int(t["total_zeni"].value_or<int64_t>(0)), g.evoluciones = int(t["evoluciones"].value_or<int64_t>(0));
  g.mejor_pts = int(t["mejor_pts"].value_or<int64_t>(0));
  g.opc_volumen = std::clamp(int(t["opc_volumen"].value_or<int64_t>(100)), 0, 100), g.opc_zoom10 = std::clamp(int(t["opc_zoom10"].value_or<int64_t>(15)), 10, 22);
  g.opc_sacudida = int(t["opc_sacudida"].value_or<int64_t>(1)) != 0;
  g.opc_autoapunte = int(t["opc_autoapunte"].value_or<int64_t>(1)) != 0;   // SUR-8
  if (const toml::array* a = t["records"].as_array())
    for (size_t k = 0; k < a->size() && k < size_t(kMaxEscenarios) * 2; ++k) g.record[k / 2][k % 2] = std::max(0, int(a->get(k)->value_or<int64_t>(0)));
  *out = g;
  return true;
}
}  // namespace

bool LeerProgreso(const fs::path& p, Progreso* out, std::string* aviso) {
  *out = Progreso{};
  aviso->clear();
  if (Leer(p, out, aviso)) return true;
  if (Leer(fs::path(p.string() + ".bak"), out, aviso)) {
    *aviso += "se usa la copia " + p.string() + ".bak\n";
    return true;
  }
  *out = Progreso{};
  return false;
}

bool GuardarProgreso(const fs::path& p, const Progreso& g) {
  std::ostringstream o;
  o << "# Dragon Budokai Survivors: progreso entre partidas (lo escribe el juego; .bak = la version anterior)\n";
  o << "zeni = " << g.zeni << "\ndesbloqueados = " << g.desbloqueados << "\njefes = " << g.jefes << "\npartidas = " << g.partidas
    << "\nvictorias = " << g.victorias << "\nmejoras = [";
  for (int k = 0; k < kMejorasPerm; ++k) o << (k ? ", " : "") << g.mejoras[k];
  o << "]\nmedallas = [";
  for (int k = 0; k < kMaxEscenarios; ++k) o << (k ? ", " : "") << int(g.medalla[k]);
  char hx[3][24];
  std::snprintf(hx[0], sizeof hx[0], "0x%llx", (unsigned long long)g.vistos_tec), std::snprintf(hx[1], sizeof hx[1], "0x%llx", (unsigned long long)g.vistos_pas);
  std::snprintf(hx[2], sizeof hx[2], "0x%llx", (unsigned long long)g.logros);
  o << "]\nvistos_tec = \"" << hx[0] << "\"\nvistos_pas = \"" << hx[1] << "\"\nvistos_enem = " << g.vistos_enem << "\nvistos_jefe = " << g.vistos_jefe
    << "\nlogros = \"" << hx[2] << "\"\nopc_volumen = " << g.opc_volumen << "\nopc_zoom10 = " << g.opc_zoom10 << "\nopc_sacudida = " << (g.opc_sacudida ? 1 : 0) << "\nopc_autoapunte = " << (g.opc_autoapunte ? 1 : 0)
    << "\nluchadores = " << g.luchadores << "\ncofres = " << g.cofres << "\ntotal_derrotados = " << g.total_derrotados
    << "\ntotal_zeni = " << g.total_zeni << "\nevoluciones = " << g.evoluciones << "\nmejor_pts = " << g.mejor_pts << "\nrecords = [";
  for (int k = 0; k < kMaxEscenarios * 2; ++k) o << (k ? ", " : "") << g.record[k / 2][k % 2];
  o << "]\n";
  std::error_code ec;
  if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  const fs::path tmp = p.string() + ".tmp", bak = p.string() + ".bak";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << o.str();
    if (!f.flush()) return false;
  }
  ec.clear();
  if (fs::exists(p, ec)) {   // auditoria #7: solo pasa a .bak si el actual se lee bien (si no, se conserva la ultima copia buena)
    Progreso anterior;
    std::string aviso;
    if (Leer(p, &anterior, &aviso)) fs::copy_file(p, bak, fs::copy_options::overwrite_existing, ec);
  }
  ec.clear();
  fs::rename(tmp, p, ec);
  return !ec;
}

int Precio(const Datos& d, const Progreso& g, int m) {
  if (m < 0 || m >= int(d.mejoras.size()) || m >= kMejorasPerm || g.mejoras[m] >= kNivelMax) return -1;
  return d.mejoras[size_t(m)].precio[g.mejoras[m]];
}

bool Comprar(const Datos& d, int m, Progreso* g) {
  const int p = Precio(d, *g, m);
  if (p < 0 || g->zeni < p) return false;
  g->zeni -= p, ++g->mejoras[m];
  return true;
}

Config ConfigDe(const Progreso& g, int escenario, int dificultad) {
  Config c;
  c.escenario = escenario, c.dificultad = dificultad;
  for (int k = 0; k < kMejorasPerm; ++k) c.mejoras[k] = g.mejoras[k];
  return c;
}

Medalla MedallaDe(bool victoria, float minuto, int dificultad) {
  if (victoria) return dificultad > 0 ? kPlatino : kOro;
  return minuto >= 7.5f ? kPlata : kBronce;
}

int Anotar(const Juego& j, int n, Progreso* g) {
  const bool gana = j.estado == Estado::Victoria;
  const int e = j.config.escenario;
  g->zeni += std::max(0, j.zeni), g->jefes += j.jefes_derrotados, ++g->partidas, g->victorias += gana;
  if (e >= 0 && e < kMaxEscenarios) g->medalla[e] = std::max(g->medalla[e], uint8_t(MedallaDe(gana, j.Minuto(), j.config.dificultad)));
  // P2: totales y record (puntuacion de la formula del ranking; abandonar cuenta igual que perder)
  g->total_derrotados += j.derrotados, g->total_zeni += std::max(0, j.zeni), g->ultimo_record = false;
  for (int i = 0; i < j.nTecnicas; ++i) {
    const Poseida& t = j.tecnicas[i];
    if (t.def >= 0 && j.datos && size_t(t.def) < j.datos->tecnicas.size() && j.datos->tecnicas[size_t(t.def)].evoluciona_de >= 0) ++g->evoluciones;
  }
  if (e >= 0 && e < kMaxEscenarios && j.config.dificultad >= 0 && j.config.dificultad < 2) {
    const int pts = PuntosPartida(int(j.tiempo), j.derrotados, gana);
    int& r = g->record[e][j.config.dificultad];
    if (pts > r) g->ultimo_record = r > 0, r = pts;   // la primera partida de un escenario no es "record": no habia con que compararla
    g->mejor_pts = std::max(g->mejor_pts, pts);
  }
  const int sig = e + 1;
  if ((gana || j.Minuto() >= 10.0f) && sig < n && sig < 32 && !g->Desbloqueado(sig)) {
    g->desbloqueados |= 1u << sig;
    return sig;
  }
  return -1;
}

int PuntosPartida(int segundos, int bajas, bool victoria) { return bajas * 10 + segundos * 30 + (victoria ? 3000 : 0); }

int64_t TotalGastado(const Datos& d, const Progreso& g) {
  int64_t t = 0;
  for (int m = 0; m < int(d.mejoras.size()) && m < kMejorasPerm; ++m)
    for (int l = 0; l < g.mejoras[m] && l < kNivelMax; ++l) t += d.mejoras[size_t(m)].precio[l];
  return t;
}

int DevolverTodo(const Datos& d, Progreso* g) {
  const int64_t t = TotalGastado(d, *g);
  if (t > int64_t(std::numeric_limits<int>::max()) - g->zeni) return -1;
  g->zeni += int(t);
  for (size_t m = 0; m < std::min(d.mejoras.size(), size_t(kMejorasPerm)); ++m) g->mejoras[m] = 0;
  return int(t);
}

// ---------------------------------------------------------------- logros.toml
bool LeerLogros(const fs::path& p, std::vector<Logro>* out, std::string* aviso) {
  out->clear();
  toml::table t;
  try {
    t = toml::parse_file(p.string());
  } catch (const toml::parse_error& e) {
    *aviso += p.string() + ": " + std::string(e.description()) + "\n";
    return false;
  }
  const toml::array* a = t["logro"].as_array();
  if (!a) return false;
  static const char* const k[kIdiomas] = {"es", "en", "fr", "de", "it", "pt"};
  for (const toml::node& n : *a) {
    const toml::table* q = n.as_table();
    if (!q || out->size() >= size_t(kMaxLogros)) continue;
    Logro l;
    l.id = (*q)["id"].value_or<std::string>(""), l.cond = (*q)["cond"].value_or<std::string>("derrotados");
    l.icono = (*q)["icono"].value_or<std::string>("ki_01"), l.desbloquea = (*q)["desbloquea"].value_or<std::string>("");
    l.valor = int((*q)["valor"].value_or<int64_t>(1)), l.zeni = int((*q)["zeni"].value_or<int64_t>(0));
    if (const size_t c = l.cond.find(':'); c != std::string::npos) l.arg = l.cond.substr(c + 1), l.cond.resize(c);
    for (int i = 0; i < kIdiomas; ++i) {
      l.txt[i] = (*q)[std::string("txt_") + k[i]].value_or<std::string>("");
      l.desc[i] = (*q)[std::string("desc_") + k[i]].value_or<std::string>("");
    }
    if (l.id.empty() || l.txt[0].empty()) {
      *aviso += "logro sin id o sin texto\n";
      continue;
    }
    out->push_back(std::move(l));
  }
  return !out->empty();
}

const std::string& TextoLogro(const Logro& l, bool desc, const char* idioma) {
  static const char* const k[kIdiomas] = {"es", "en", "fr", "de", "it", "pt"};
  const std::string* v = desc ? l.desc : l.txt;
  for (int i = 0; i < kIdiomas; ++i)
    if (idioma && !std::strcmp(idioma, k[i]) && !v[i].empty()) return v[i];
  return v[1].empty() ? v[0] : v[1];
}

namespace {
int IdxPersonaje(const Datos& d, const std::string& id) {
  for (size_t i = 0; i < d.personajes.size(); ++i)
    if (d.personajes[i].id == id) return int(i);
  return -1;
}
int IdxEscenario(const Datos& d, const std::string& id) {
  for (size_t i = 0; i < d.escenarios.size(); ++i)
    if (d.escenarios[i].id == id) return int(i);
  return -1;
}
bool Cumple(const Datos& d, const Logro& l, const Juego& j, int cofres, bool fin, const Progreso& g) {
  const std::string& c = l.cond;
  const bool gana = fin && j.estado == Estado::Victoria;
  if (c == "derrotados") return j.derrotados >= l.valor;
  if (c == "tiempo") return j.tiempo >= float(l.valor);
  if (c == "nivel") return j.jugador.nivel >= l.valor;
  if (c == "jefes") return j.jefes_derrotados >= l.valor;
  if (c == "racha") return j.racha_max >= l.valor;
  if (c == "rotos") return j.rotos >= l.valor;
  if (c == "zeni") return j.zeni >= l.valor;
  if (c == "cofres") return cofres >= l.valor;
  if (c == "evoluciones" || c == "tecnicas_max" || c == "evolucion") {
    int n = 0;
    for (int i = 0; i < j.nTecnicas; ++i) {
      const Poseida& t = j.tecnicas[i];
      if (t.def < 0 || size_t(t.def) >= d.tecnicas.size()) continue;
      const DefTecnica& q = d.tecnicas[size_t(t.def)];
      if (c == "evoluciones") n += q.evoluciona_de >= 0;
      else if (c == "tecnicas_max") n += t.nivel >= kNivelMax;
      else if (q.id == l.arg) return true;
    }
    return c != "evolucion" && n >= l.valor;
  }
  if (c == "derrotar_jefe") return j.jefes_derrotados > 0 && j.DJ().id == l.arg;
  if (c == "victoria") return gana;
  if (c == "victoria_dificil" || c == "medalla_platino") return gana && j.config.dificultad > 0;
  if (c == "victoria_personaje") return gana && IdxPersonaje(d, l.arg) == j.personaje;
  if (c == "victoria_escenario") return gana && IdxEscenario(d, l.arg) == j.config.escenario;
  if (!fin) return false;   // de toda la vida: al acabar la partida (ya sumada por Anotar)
  if (c == "total_derrotados") return g.total_derrotados >= l.valor;
  if (c == "total_zeni") return g.total_zeni >= l.valor;
  if (c == "total_partidas") return g.partidas >= l.valor;
  if (c == "total_victorias") return g.victorias >= l.valor;
  if (c == "total_jefes") return g.jefes >= l.valor;
  if (c == "total_cofres") return g.cofres >= l.valor;
  if (c == "total_logros") {
    int n = 0;
    for (int i = 0; i < kMaxLogros; ++i) n += int((g.logros >> i) & 1u);
    return n >= l.valor;
  }
  return false;
}
}  // namespace

int EvaluarLogros(const Datos& d, const std::vector<Logro>& ls, const Juego& j, int cofres, bool fin, Progreso* g, std::vector<int>* nuevos) {
  int n = 0;
  for (size_t i = 0; i < ls.size() && i < size_t(kMaxLogros); ++i) {
    if ((g->logros >> i) & 1u || !Cumple(d, ls[i], j, cofres, fin, *g)) continue;
    g->logros |= uint64_t(1) << i, g->zeni += ls[i].zeni, ++n;
    const std::string& u = ls[i].desbloquea;
    if (u.rfind("luchador:", 0) == 0)
      if (const int p = IdxPersonaje(d, u.substr(9)); p >= 0 && p < 32) g->luchadores |= 1u << p;
    if (nuevos) nuevos->push_back(int(i));
  }
  return n;
}

int LogroQueAbre(const std::vector<Logro>& ls, const Datos& d, const Progreso& g, int personaje) {
  if (personaje >= 0 && personaje < 32 && (g.luchadores >> personaje) & 1u) return -1;
  for (size_t i = 0; i < ls.size(); ++i)
    if (ls[i].desbloquea.rfind("luchador:", 0) == 0 && IdxPersonaje(d, ls[i].desbloquea.substr(9)) == personaje) return int(i);
  return -1;
}

}  // namespace dbz3::survivors
