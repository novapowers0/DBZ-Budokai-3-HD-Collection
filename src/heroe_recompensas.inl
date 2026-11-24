// dbz3 - Modo Heroe, recompensas y Coleccion (HEROE-REC R1-R3; DISENO.md de heroe_rediseno §8; ajuste dbz3_heroe_recompensas,
// APAGADO de serie). Se incluye en heroe.cpp (namespace dbz3::heroe, despues de ComprobarDesbloqueos). Datos: res/heroe/
// recompensas.toml (aparte de desbloqueos.toml, que tocan Evolucion y Tecnicas). Lo conseguido va a la ficha (g_desbloqueos ->
// cac.toml `desbloqueos`, ids "r_..."); lo "visto" y el "Desbloquear todo" del lanzador, a user_data/dbz3/coleccion.toml (global,
// .tmp + renombrar y la anterior en .bak; nunca el guardado nativo de B3). Con el ajuste apagado nada de esto se evalua.

bool RecompensasOn() { return REXCVAR_GET(dbz3_heroe_recompensas); }

struct Recompensa {
  std::string id, tipo, marca, nombre_es, nombre_en, cond_es, cond_en;
  int capsula = 0, escenario = -1, cara = -1, tramo = -1, vence = -1, maestro = -1, entreno = 0, coleccion = -1;
  int luchador = -1;   // R23 HER-06: tipo "luchador": ID nativo (0-63) que se abre en el selector de luchadores
};
const std::vector<Recompensa>& TablaRecompensas() {
  static const std::vector<Recompensa> v = [] {
    std::vector<Recompensa> out;
    for (const std::filesystem::path& ruta : {dbz3::settings::UserDataRoot() / "heroe" / "recompensas.toml",
                                              dbz3::ModsRoot().parent_path() / "res" / "heroe" / "recompensas.toml"}) {
      std::error_code ec;
      if (!std::filesystem::exists(ruta, ec)) continue;
      try {
        const toml::table t = toml::parse_file(ruta.string());
        const toml::array* a = t["recompensa"].as_array();
        if (!a) continue;
        for (const auto& e : *a) {
          const toml::table* x = e.as_table();
          if (!x) continue;
          Recompensa r;
          r.id = (*x)["id"].value_or(std::string{});
          r.tipo = (*x)["tipo"].value_or(std::string{});
          r.marca = (*x)["marca"].value_or(std::string{});
          r.nombre_es = (*x)["nombre_es"].value_or(r.id);
          r.nombre_en = (*x)["nombre_en"].value_or(r.id);
          r.cond_es = (*x)["cond_es"].value_or(std::string{});
          r.cond_en = (*x)["cond_en"].value_or(std::string{});
          r.capsula = int((*x)["capsula"].value_or(int64_t(0)));
          r.escenario = int((*x)["escenario"].value_or(int64_t(-1)));
          r.cara = int((*x)["cara"].value_or(int64_t(-1)));
          r.tramo = int((*x)["tramo"].value_or(int64_t(-1)));
          r.vence = int((*x)["vence"].value_or(int64_t(-1)));
          r.maestro = int((*x)["maestro"].value_or(int64_t(-1)));
          r.entreno = int((*x)["entreno"].value_or(int64_t(0)));
          r.coleccion = int((*x)["coleccion"].value_or(int64_t(-1)));
          r.luchador = int((*x)["luchador"].value_or(int64_t(-1)));
          if (r.id.empty() || r.id.size() >= 48 || (r.tipo != "tecnica" && r.tipo != "titulo" && r.tipo != "escenario" && r.tipo != "luchador")) continue;
          if (r.tipo == "luchador" && (r.luchador < 0 || r.luchador > 63)) continue;
          if (r.tipo == "tecnica" && r.capsula <= 0) continue;
          if (r.tipo == "escenario" && r.escenario < 5000) continue;
          out.push_back(std::move(r));
        }
        REXLOG_INFO("dbz3 heroe: {} recompensas desde {}", out.size(), ruta.string());
        return out;
      } catch (const toml::parse_error&) {
        REXLOG_WARN("dbz3 heroe: {} no se puede leer", ruta.string());
      }
    }
    return out;
  }();
  return v;
}

// ---- coleccion.toml global (visto/NUEVO y "Desbloquear todo") ----
struct ColeccionGlobal {
  bool cargada = false, todo = false;
  std::set<std::string> vistos;
};
ColeccionGlobal g_col;
std::mutex g_col_mu;
std::filesystem::path ColeccionRuta() { return dbz3::settings::UserDataRoot() / "coleccion.toml"; }   // = user_data\dbz3\coleccion.toml
void ColeccionCargar() {   // con g_col_mu
  if (g_col.cargada) return;
  g_col.cargada = true;
  std::error_code ec;
  if (!std::filesystem::exists(ColeccionRuta(), ec)) return;
  try {
    const toml::table t = toml::parse_file(ColeccionRuta().string());
    g_col.todo = t["todo"].value_or(false);
    if (const toml::array* a = t["vistos"].as_array())
      for (const auto& e : *a)
        if (auto s = e.value<std::string>(); s && !s->empty() && s->size() < 48) g_col.vistos.insert(*s);
    REXLOG_INFO("dbz3 heroe: coleccion.toml leida: todo={}, {} vistos", g_col.todo, g_col.vistos.size());
  } catch (const toml::parse_error&) {
    REXLOG_WARN("dbz3 heroe: coleccion.toml no se puede leer");
  }
}
void ColeccionGuardar() {   // con g_col_mu: .tmp + renombrar, la anterior en .bak
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(ColeccionRuta().parent_path(), ec);
  const fs::path tmp = ColeccionRuta().string() + ".tmp", bak = ColeccionRuta().string() + ".bak";
  {
    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
    o << "# Coleccion del Modo Heroe (global): lo visto y el desbloqueo total. Lo conseguido de cada heroe va en su ficha (cac.toml).\n"
      << "todo = " << (g_col.todo ? "true" : "false") << "\nvistos = [";
    const char* sep = "";
    for (const std::string& s : g_col.vistos) o << sep << '"' << s << '"', sep = ", ";
    o << "]\n";
    if (!o) { REXLOG_WARN("dbz3 heroe: no se pudo escribir coleccion.toml"); return; }
  }
  if (fs::exists(ColeccionRuta(), ec)) fs::copy_file(ColeccionRuta(), bak, fs::copy_options::overwrite_existing, ec);
  fs::rename(tmp, ColeccionRuta(), ec);
  if (ec) REXLOG_WARN("dbz3 heroe: coleccion.toml: no se pudo renombrar ({})", ec.message());
}

std::set<std::string> PremiosDePrueba() {   // PRUEBA (pruebas.py): premios ya abiertos, "r_t0_death_beam,r_h25"
  std::set<std::string> s;
  if (const char* e = std::getenv("DBZ3_TEST_HEROE_PREMIOS")) {
    std::stringstream ss(e);
    for (std::string id; std::getline(ss, id, ',');)
      if (!id.empty()) s.insert(id);
  }
  return s;
}
bool PremioConseguido(const Recompensa& r) {
  if (TieneDesbloqueo(r.id) || PremiosDePrueba().count(r.id)) return true;
  std::lock_guard<std::mutex> lk(g_col_mu);
  ColeccionCargar();
  return g_col.todo;
}
// Para el selector: las entradas sin premio y el modo OFF conservan su comportamiento.
bool EscenarioPremiadoDisponible(int entrada) {
  if (!RecompensasOn()) return true;
  bool declarado = false;
  for (const Recompensa& r : TablaRecompensas()) {
    if (r.tipo != "escenario" || r.escenario != entrada) continue;
    declarado = true;
    if (PremioConseguido(r)) return true;
  }
  return !declarado;
}
// R23 HER-06: luchadores premiados (y conseguidos) para la mascara del selector (roster_ext.cpp, hook de sub_8217A1C0). Solo
// SUMA bits: un luchador que la partida nativa ya tiene abierto sigue abierto; nada nativo se esconde.
uint64_t LuchadoresPremiados() {
  uint64_t m = 0;
  if (!RecompensasOn()) return m;
  for (const Recompensa& r : TablaRecompensas())
    if (r.tipo == "luchador" && PremioConseguido(r)) m |= uint64_t(1) << r.luchador;
  return m;
}
int ColeccionPorcentaje() {
  const std::vector<Recompensa>& t = TablaRecompensas();
  if (t.empty()) return 0;
  int n = 0;
  for (const Recompensa& r : t) n += PremioConseguido(r);
  return 100 * n / int(t.size());
}

// Lo ganado en el ultimo resultado (para el aviso de resultado y cierre); se vacia al empezar cada evaluacion.
struct PremioAviso {
  std::string id;
  bool repetido = false;
};
std::vector<PremioAviso> g_aviso_premios;
constexpr int kZenieRepetido = 500;   // 8.4: repetido -> Zenie al monedero del HUB

void PremiosAvisoVaciar() { g_aviso_premios.clear(); }

// Tras una victoria del Heroe: lo que abre `tramo` (>= 0: acaba de cerrarse ese tramo), `rival`/`forma` (combate ganado; -1: ninguno)
// y el estado (marcas de ruta, % de maestro, % de la Coleccion). Cada entrada con alguna condicion y todas cumplidas se gana una
// vez; si el disparo es un suceso (tramo/vence) y ya la tenias, da Zenie. Llamarla tras ComprobarDesbloqueos.
void EvaluarRecompensas(int tramo, int rival, int forma) {
  (void)forma;
  if (!RecompensasOn()) return;
  bool otorgado = true;
  for (int vuelta = 0; otorgado && vuelta < 4; ++vuelta) {   // los hitos de la Coleccion encadenan (un premio sube el %)
    otorgado = false;
    const int pct = ColeccionPorcentaje();
    for (const Recompensa& r : TablaRecompensas()) {
      if (r.tramo < 0 && r.vence < 0 && r.marca.empty() && r.maestro < 0 && r.coleccion < 0) continue;   // sin condicion: nunca solo
      if (r.tramo >= 0 && r.tramo != tramo) continue;
      if (r.vence >= 0 && r.vence != rival) continue;
      if (!r.marca.empty() && !MarcasHeroe().count(r.marca)) continue;
      if (r.maestro >= 0) {
        const int m = MaestroIndice(r.maestro);
        if (m < 0 || (g_entreno[m] < r.entreno && !((g_maestros >> m) & 1))) continue;
      }
      if (r.coleccion >= 0 && pct < r.coleccion) continue;
      const bool suceso = r.tramo >= 0 || r.vence >= 0;
      if (PremioConseguido(r)) {
        if (!suceso) continue;
        g_aviso_premios.push_back({r.id, true});
        dbz3::rpg::Premio(kZenieRepetido);
        REXLOG_INFO("dbz3 heroe: premio repetido {} ({}): +{} Zenie", r.id, r.tipo, kZenieRepetido);
        continue;
      }
      g_desbloqueos.push_back(r.id);
      g_aviso_premios.push_back({r.id, false});
      otorgado = true;
      REXLOG_INFO("dbz3 heroe: premio {} ({}{}): tramo {} rival {} marca '{}' maestro {} coleccion {}%", r.id, r.tipo,
                  r.tipo == "tecnica" ? " capsula " + std::to_string(r.capsula) : std::string(), tramo, rival, r.marca, r.maestro, pct);
    }
    tramo = rival = -1;   // los sucesos solo valen para la primera pasada
  }
}

// Capsulas de las tecnicas ganadas (entran en el inventario de Tecnicas aunque su duenyo no sea un donante abierto).
std::vector<int> CapsulasPremiadas() {
  std::vector<int> v;
  if (!RecompensasOn()) return v;
  for (const Recompensa& r : TablaRecompensas())
    if (r.tipo == "tecnica" && PremioConseguido(r) && (!TiposListos() || TipoTecnica(r.capsula) >= 0)) v.push_back(r.capsula);
  return v;
}

namespace {
InfoPremio InfoDe(const Recompensa& r) {
  InfoPremio i;
  i.id = r.id, i.tipo = r.tipo, i.capsula = r.capsula, i.cara = r.cara;
  i.nombre = T::T(r.nombre_es.c_str(), r.nombre_en.c_str());
  i.condicion = T::T(r.cond_es.c_str(), r.cond_en.c_str());
  i.conseguido = PremioConseguido(r);
  {
    std::lock_guard<std::mutex> lk(g_col_mu);
    ColeccionCargar();
    i.nuevo = i.conseguido && !g_col.vistos.count(r.id);
  }
  return i;
}
}  // namespace
bool RecompensasActivas() { return RecompensasOn(); }
std::vector<InfoPremio> Premios() {
  std::vector<InfoPremio> v;
  for (const Recompensa& r : TablaRecompensas()) v.push_back(InfoDe(r));
  return v;
}
std::vector<InfoPremio> PremiosAviso() {
  std::vector<InfoPremio> v;
  for (const PremioAviso& a : g_aviso_premios)
    for (const Recompensa& r : TablaRecompensas())
      if (r.id == a.id) {
        InfoPremio i = InfoDe(r);
        i.repetido = a.repetido;
        v.push_back(std::move(i));
      }
  return v;
}
void ColeccionVisto(const std::string& id) {
  std::lock_guard<std::mutex> lk(g_col_mu);
  ColeccionCargar();
  if (g_col.vistos.insert(id).second) ColeccionGuardar();
}
void ColeccionTodoVisto() {   // "NUEVO" fuera en todo lo conseguido (al salir de la pantalla)
  std::vector<std::string> ids;
  for (const Recompensa& r : TablaRecompensas())
    if (PremioConseguido(r)) ids.push_back(r.id);   // antes del candado: PremioConseguido lo toma
  std::lock_guard<std::mutex> lk(g_col_mu);
  ColeccionCargar();
  bool cambio = false;
  for (const std::string& id : ids) cambio |= g_col.vistos.insert(id).second;
  if (cambio) ColeccionGuardar();
}
