// dbz3 - Modo Heroe, recompensas y Coleccion (HEROE-REC R1-R3; DISENO.md de heroe_rediseno §8; ajuste dbz3_heroe_recompensas,
// APAGADO de serie). Se incluye en heroe.cpp (namespace dbz3::heroe, despues de ComprobarDesbloqueos). Datos: res/heroe/
// recompensas.toml (aparte de desbloqueos.toml, que tocan Evolucion y Tecnicas). Lo conseguido va a la ficha (g_desbloqueos ->
// cac.toml `desbloqueos`, ids "r_..."); lo "visto" y el "Desbloquear todo" del lanzador, a user_data/dbz3/coleccion.toml (global,
// .tmp + renombrar y la anterior en .bak; nunca el guardado nativo de B3). Con el ajuste apagado nada de esto se evalua.

bool RecompensasOn() { return REXCVAR_GET(dbz3_heroe_recompensas); }
bool ColeccionVueltasOn() { return REXCVAR_GET(dbz3_heroe_coleccion_vueltas); }

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

// ---- coleccion.toml global (visto/NUEVO, cuenta NHER4 y "Desbloquear todo") ----
struct ColeccionGlobal {
  bool cargada = false;
  bool lectura_fallida = false;   // NHER4: si el archivo en disco era ilegible, no sobrescribirlo despues
  col_io::DatosColeccion datos;
};
ColeccionGlobal g_col;
std::mutex g_col_mu;
std::filesystem::path ColeccionRuta() { return dbz3::settings::UserDataRoot() / "coleccion.toml"; }   // = user_data\dbz3\coleccion.toml
void ColeccionCargar() {   // con g_col_mu
  if (g_col.cargada) return;
  g_col.cargada = true;
  g_col.lectura_fallida = false;
  std::string err;
  if (!col_io::Cargar(ColeccionRuta(), g_col.datos, &err)) {
    g_col.lectura_fallida = true;
    REXLOG_WARN("dbz3 heroe: coleccion.toml no se puede leer ({}); no se sobrescribira", err);
    return;
  }
  REXLOG_INFO("dbz3 heroe: coleccion.toml leida: todo={}, {} vistos, {} conseguidos, {} finales",
              g_col.datos.todo, g_col.datos.vistos.size(), g_col.datos.conseguidos.size(), g_col.datos.finales.size());
}
void ColeccionGuardar() {   // con g_col_mu: .tmp + flush + .bak + reemplazo atomico; nunca pisa archivo corrupto
  if (g_col.lectura_fallida) {
    REXLOG_WARN("dbz3 heroe: coleccion.toml ilegible al cargar; guardado omitido para conservar el archivo");
    return;
  }
  std::string err;
  if (!col_io::GuardarAtomico(ColeccionRuta(), g_col.datos, ColeccionVueltasOn(), &err))
    REXLOG_WARN("dbz3 heroe: coleccion.toml: no se pudo guardar ({})", err);
}
void ColeccionSincronizarConFichas() {
  if (!RecompensasOn() || !ColeccionVueltasOn()) return;
  const std::vector<Recompensa>& tabla = TablaRecompensas();
  if (tabla.empty()) return;
  std::vector<col_io::ReglaRecompensa> reglas;
  reglas.reserve(tabla.size());
  for (const Recompensa& r : tabla) {
    const bool solo_col = r.coleccion >= 0 && r.tramo < 0 && r.vence < 0 && r.marca.empty() && r.maestro < 0;
    reglas.push_back({r.id, r.coleccion, solo_col});
  }
  std::vector<std::vector<std::string>> listas;
  listas.push_back(g_desbloqueos);
  for (int i = 0; i < dbz3::cac::NumSlots(); ++i) listas.push_back(dbz3::cac::Slot(i).desbloqueos);
  for (const dbz3::cac::Heroe& a : dbz3::cac::Archivados()) listas.push_back(a.desbloqueos);
  std::lock_guard<std::mutex> lk(g_col_mu);
  ColeccionCargar();
  if (g_col.lectura_fallida) return;
  if (col_io::SincronizarConFichas(g_col.datos, reglas, listas)) {
    ColeccionGuardar();
    REXLOG_INFO("dbz3 heroe: coleccion.toml sincronizada con las fichas ({} conseguidos)", g_col.datos.conseguidos.size());
  }
}
void RegistrarFinalColeccion(const std::string& fin) {
  if (!ColeccionVueltasOn() || !col_io::IdValido(fin)) return;
  std::lock_guard<std::mutex> lk(g_col_mu);
  ColeccionCargar();
  if (!g_col.lectura_fallida && g_col.datos.finales.insert(fin).second) ColeccionGuardar();
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
  if (g_col.datos.todo) return true;
  return ColeccionVueltasOn() && g_col.datos.conseguidos.count(r.id) != 0;
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
  std::vector<std::string> nuevos_cuenta;
  bool otorgado = true;
  for (int vuelta = 0; otorgado && vuelta < 4; ++vuelta) {   // los hitos de la Coleccion encadenan (un premio sube el %)
    otorgado = false;
    const int pct = ColeccionPorcentaje();
    // El premio final se gana con todas las demás recompensas; no puede exigirse a sí mismo.
    int base_ok = 0, base_tot = 0;
    for (const Recompensa& x : TablaRecompensas()) {
      if (x.coleccion >= 100) continue;
      ++base_tot;
      base_ok += PremioConseguido(x);
    }
    for (const Recompensa& r : TablaRecompensas()) {
      if (r.tramo < 0 && r.vence < 0 && r.marca.empty() && r.maestro < 0 && r.coleccion < 0) continue;   // sin condicion: nunca solo
      if (r.tramo >= 0 && r.tramo != tramo) continue;
      if (r.vence >= 0 && r.vence != rival) continue;
      if (!r.marca.empty() && !MarcasHeroe().count(r.marca)) continue;
      if (r.maestro >= 0) {
        const int m = MaestroIndice(r.maestro);
        if (m < 0 || (g_entreno[m] < r.entreno && !((g_maestros >> m) & 1))) continue;
      }
      if (r.coleccion == 100 && (base_tot == 0 || base_ok != base_tot)) continue;
      if (r.coleccion >= 0 && r.coleccion != 100 && pct < r.coleccion) continue;
      const bool suceso = r.tramo >= 0 || r.vence >= 0;
      if (PremioConseguido(r)) {
        if (!suceso) continue;
        // NHER4: si esta ficha acaba de ganarlo y ya estaba en la cuenta, deja constancia en ficha (en_ficha=true)
        // para conservarlo aunque se apague el cvar, manteniendo el +500 Zenie de premio ya poseido.
        if (ColeccionVueltasOn() && !TieneDesbloqueo(r.id)) g_desbloqueos.push_back(r.id);
        g_aviso_premios.push_back({r.id, true});
        dbz3::rpg::Premio(kZenieRepetido);
        REXLOG_INFO("dbz3 heroe: premio repetido {} ({}): +{} Zenie", r.id, r.tipo, kZenieRepetido);
        continue;
      }
      if (!TieneDesbloqueo(r.id)) g_desbloqueos.push_back(r.id);
      if (ColeccionVueltasOn()) nuevos_cuenta.push_back(r.id);
      g_aviso_premios.push_back({r.id, false});
      otorgado = true;
      REXLOG_INFO("dbz3 heroe: premio {} ({}{}): tramo {} rival {} marca '{}' maestro {} coleccion {}%", r.id, r.tipo,
                  r.tipo == "tecnica" ? " capsula " + std::to_string(r.capsula) : std::string(), tramo, rival, r.marca, r.maestro, pct);
    }
    tramo = rival = -1;   // los sucesos solo valen para la primera pasada
  }
  if (!nuevos_cuenta.empty() && ColeccionVueltasOn()) {
    std::lock_guard<std::mutex> lk(g_col_mu);
    ColeccionCargar();
    if (!g_col.lectura_fallida) {
      bool cambio = false;
      for (const std::string& id : nuevos_cuenta) cambio |= g_col.datos.conseguidos.insert(id).second;
      if (cambio) ColeccionGuardar();
    }
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
  i.en_ficha = TieneDesbloqueo(r.id) || PremiosDePrueba().count(r.id) != 0;
  {
    std::lock_guard<std::mutex> lk(g_col_mu);
    ColeccionCargar();
    i.nuevo = i.conseguido && !g_col.datos.vistos.count(r.id);
  }
  return i;
}
}  // namespace
bool RecompensasActivas() { return RecompensasOn(); }
std::vector<std::string> FinalesCompletados() {
  if (!ColeccionVueltasOn()) return {};
  std::set<std::string> todos = g_finales;
  {
    std::lock_guard<std::mutex> lk(g_col_mu);
    ColeccionCargar();
    todos.insert(g_col.datos.finales.begin(), g_col.datos.finales.end());
  }
  return std::vector<std::string>(todos.begin(), todos.end());
}
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
  if (!g_col.lectura_fallida && g_col.datos.vistos.insert(id).second) ColeccionGuardar();
}
void ColeccionTodoVisto() {   // "NUEVO" fuera en todo lo conseguido (al salir de la pantalla)
  std::vector<std::string> ids;
  for (const Recompensa& r : TablaRecompensas())
    if (PremioConseguido(r)) ids.push_back(r.id);   // antes del candado: PremioConseguido lo toma
  std::lock_guard<std::mutex> lk(g_col_mu);
  ColeccionCargar();
  if (g_col.lectura_fallida) return;
  bool cambio = false;
  for (const std::string& id : ids) cambio |= g_col.datos.vistos.insert(id).second;
  if (cambio) ColeccionGuardar();
}
std::vector<InfoExtraCol> CapsulasColeccion() {
  std::vector<InfoExtraCol> v;
  if (!ColeccionVueltasOn()) return v;
  std::set<int> vistos_cap;
  // 6 capsulas pasivas de maestro (kEstilos[0..5].hab); paridad exacta con Reglas() y TecnicasV2() (sin falsear por todo)
  for (int i = 0; i < kNumMaestros; ++i) {
    const Estilo& e = kEstilos[i];
    if (e.hab <= 0 || !vistos_cap.insert(e.hab).second) continue;
    InfoExtraCol x;
    x.id = "cap_m_" + std::to_string(e.hab);
    x.nombre = e.hab_n ? e.hab_n : (std::string(T::T("Cápsula ", "Capsule ")) + std::to_string(e.hab));
    x.cap = e.hab;
    x.cara = e.id;
    const bool es_krilin_445 = (e.hab == 445);
    x.condicion = std::string(T::T("Maestro ", "Master ")) + M::Nombre(e.id) +
                  (es_krilin_445 ? T::T(" al 60 % / 100 %", " 60% / 100%") : T::T(" al 100 %", " 100%"));
    const bool pasiva_ok = ((g_maestros >> i) & 1) != 0;
    const bool esquiva_445_ok = es_krilin_445 && TecnicasV2On() &&
                                (TieneDesbloqueo("esquiva_concentracion_krilin") || g_entreno[i] >= 60);
    x.conseguido = pasiva_ok || esquiva_445_ok;
    v.push_back(std::move(x));
  }
  // 3 capsulas de esquiva nativas restantes de desbloqueos.toml (409, 410, 444; 445 ya deduplicada arriba)
  for (const Desbloqueo& d : TablaDesbloqueos()) {
    if (d.tipo != "tecnica" || d.capsula <= 0) continue;
    if (d.capsula != 409 && d.capsula != 410 && d.capsula != 444 && d.capsula != 445) continue;
    if (!vistos_cap.insert(d.capsula).second) continue;
    const int m = MaestroIndice(d.maestro);
    InfoExtraCol x;
    x.id = d.id;
    x.nombre = T::T(d.nombre_es.c_str(), d.nombre_en.c_str());
    x.cap = d.capsula;
    x.cara = d.maestro;
    x.condicion = m >= 0 ? (std::string(T::T("Maestro ", "Master ")) + M::Nombre(d.maestro) + " " + std::to_string(d.entreno) + " %")
                         : std::string(T::T("Desbloqueo de maestro", "Master unlock"));
    // Paridad exacta con TecnicasV2() (heroe_tecnicas_v2.inl:173) y Reglas() (g_equipo[3] requiere TecnicasV2On)
    x.conseguido = TecnicasV2On() &&
                   (TieneDesbloqueo(d.id) || (m >= 0 && (g_entreno[m] >= d.entreno || ((g_maestros >> m) & 1) != 0)));
    v.push_back(std::move(x));
  }
  return v;
}
std::vector<InfoExtraCol> TrajesColeccion() {
  std::vector<InfoExtraCol> v;
  if (!ColeccionVueltasOn()) return v;
  // 6 estilos nativos de los maestros B3: paridad exacta con Estilos() / Desbloqueado(id)
  for (int i = 0; i < kNumMaestros; ++i) {
    const int id = kEstilos[i].id;
    InfoExtraCol x;
    x.id = "estilo_m_" + std::to_string(id);
    x.nombre = std::string(T::T("Estilo de ", "Style of ")) + M::Nombre(id);
    x.cara = id;
    const int sg = SagaDelMaestro(i);
    x.condicion = i == 0 ? std::string(T::T("Disponible desde el inicio", "Available from start"))
                         : (std::string(T::T("Desbloquea al maestro ", "Unlock master ")) + M::Nombre(id) +
                            (sg >= 0 ? (std::string(" (") + NombreSaga(sg) + ")") : std::string()));
    x.conseguido = Desbloqueado(id);
    v.push_back(std::move(x));
  }
  // 3 cosmeticos nativos alcanzables (aura / ki de maestros activos en kEstilos): paridad exacta con Desbloqueos() (TieneDesbloqueo)
  for (const Desbloqueo& d : TablaDesbloqueos()) {
    if (d.tipo != "aura" && d.tipo != "ki") continue;
    const int m = MaestroIndice(d.maestro);
    if (m < 0 && !TieneDesbloqueo(d.id)) continue;
    InfoExtraCol x;
    x.id = d.id;
    x.nombre = T::T(d.nombre_es.c_str(), d.nombre_en.c_str());
    x.cara = d.maestro;
    x.condicion = m >= 0 ? (std::string(T::T("Maestro ", "Master ")) + M::Nombre(d.maestro) + " " + std::to_string(d.entreno) + " %")
                         : std::string(T::T("Desbloqueo especial", "Special unlock"));
    x.conseguido = TieneDesbloqueo(d.id);
    v.push_back(std::move(x));
  }
  return v;
}
