// dbz3 - Modo Heroe, Tecnicas v2 (DISENO.md s9 de E:\DBZ3HD_trabajo\heroe_rediseno, pasos T1-T6; ajuste dbz3_heroe_tecnicas_v2,
// APAGADO de serie: apagado, InventarioTecnicas/Equipar/combate hacen exactamente lo de antes). Se incluye al final de heroe.cpp.
//
// T1 aprender con los maestros (UT): las tecnicas de cada maestro (#SKC duenyo = el maestro, con tipo en tecnicas_tipo.txt) se
//    ordenan especiales por ki y luego definitivas por ki; la 1.a se da al elegirlo, la 2.a al 20 %, las de en medio al 60 % y la
//    ultima al 100 %. Sin datos por personaje: vale para cualquier maestro. El estilo propio (si no es maestro) da las suyas
//    como hasta ahora; las de otros luchadores solo como premio (CapsulasPremiadas, heroe_recompensas.inl).
// T2 hueco de esquiva (g_equipo[3], tipo 3). RE (12/11, catalogo #SKC del juego + texturas de descripcion de data_usi 2079+ID):
//    B3 no tiene una clase "esquiva"; las capsulas que actuan al esquivar/teletransportarse o desviar son las de APOYO (clase
//    0x1N, nibble = casillas) con efecto +20 = 0x43: 409 Gero's Deflection R&D (0x430F, desvia ki al cubrirse), 410 Gero's
//    Deflect-Back R&D (0x4310, lo devuelve), 411 Gero's Energy R&D (0x4311, ki al desviar: solo vale con 409/410 -> fuera),
//    444 Turtle Shell (0x4318, x2 ki al esquivar/teletransportarse) y 445 Concentration (0x4319, la mitad). Todas "de todos"
//    (duenyos = 0..43); se lanzan solas como las de apoyo (lista de capsulas del combate, igual que la de maestro de Krilin, 445).
//    Se abren por datos: res/heroe/desbloqueos.toml tipo "tecnica" con maestro + entreno.
// T6 niveles: victorias con la tecnica equipada (5 -> Nv 2, 15 -> Nv 3) y estado por ficha.
//    TC-07 opcional: +2/+4 puntos al multiplicador global de dano, tope +6; no es dano por capsula del motor.

REXCVAR_DEFINE_BOOL(dbz3_heroe_maestria_dano, false, "DBZ3/Mods",
                    "Hero: global damage bonus for equipped mastered attacks");

bool TecnicasV2On() { return rex::cvar::GetFlagByName("dbz3_heroe_tecnicas_v2") == "true"; }

namespace {
// Registro #SKC (40 B) de la capsula `cap` en la memoria del juego, o nullptr.
const uint8_t* RegistroSkc(int cap) {
  uint8_t* b = Base();
  if (!b || cap <= 0) return nullptr;
  const uint32_t pc = Be32(b + 0x82375608), pr = Be32(b + 0x8237560C);
  if (pc < 0x82000000 || pr < 0x82000000) return nullptr;
  const uint32_t n = std::min<uint32_t>(Be32(b + pc + 0x10), 2048);
  return uint32_t(cap) < n ? b + pr + 40 * uint32_t(cap) : nullptr;
}

struct EstadoTec {
  bool vista = false;
  int victorias = 0;
};
std::map<int, EstadoTec> g_tec_estado;
int g_tec_estado_ficha = -1;
std::mutex g_tec_mu;
std::filesystem::path TecEstadoRuta() {
  return dbz3::settings::UserDataRoot() / "heroe" / ("tecnicas_v2_" + std::to_string(FichaNumero()) + ".txt");
}
void TecEstadoCargar() {   // con g_tec_mu
  if (g_tec_estado_ficha == FichaNumero()) return;
  g_tec_estado.clear();
  g_tec_estado_ficha = FichaNumero();
  std::ifstream f(TecEstadoRuta());
  int cap = 0, vista = 0, vic = 0;
  while (f >> cap >> vista >> vic) g_tec_estado[cap] = {vista != 0, vic};
}
void TecEstadoGuardar() {   // con g_tec_mu
  std::error_code ec;
  std::filesystem::create_directories(TecEstadoRuta().parent_path(), ec);
  std::ofstream f(TecEstadoRuta(), std::ios::trunc);
  for (const auto& [cap, e] : g_tec_estado) f << cap << ' ' << (e.vista ? 1 : 0) << ' ' << e.victorias << '\n';
}
bool MaestroElegido(int i) { return i >= 0 && i < kNumMaestros && (g_entreno[i] > 0 || ((g_maestros >> i) & 1) || MaestroActual() == i); }
}  // namespace

bool EsEsquiva(int cap) {
  const uint8_t* r = RegistroSkc(cap);
  return r && (r[8] >> 4) == 1 && r[20] == 0x43 && r[21] != 0x11;   // 0x4311 (Energy R&D) solo suma a 409/410
}
REXCVAR_DEFINE_BOOL(dbz3_heroe_tecnicas_forma, true, "DBZ3/Mods",
                    "Hero v2: gate technique equip by known mounted combat forms");

static bool TecnicasSinRaza() {
  return REXCVAR_GET(dbz3_heroe_tecnicas_sin_raza) && TecnicasV2On() && REXCVAR_GET(dbz3_heroe_tecnicas_forma);
}

int FormasTecnica(int cap) {
  const uint8_t* r = RegistroSkc(cap);
  return r ? r[14] : 0xFF;
}
bool TecnicaFormaBloqueada(int cap) {
  if (!REXCVAR_GET(dbz3_heroe_tecnicas_forma) || EsEsquiva(cap)) return false;
  const uint8_t* skc = RegistroSkc(cap);
  if (!skc) return TecnicasSinRaza();
  const uint16_t requiere = Be16(skc + 16);
  const unsigned permitidas = skc[14];
  if (!requiere && (permitidas & 1)) return false;
  const uint8_t* base = Base();
  const auto& propio = Propio();
  // Sin la ficha montada con su donante no se extrapolan formas de otro cuerpo/moveset.
  if (!base || propio.id < 0 || propio.id >= 64 || propio.donante != DonanteEfectivo()) return TecnicasSinRaza();
  const uint8_t* r = base + kChar372 + uint32_t(propio.id) * 372;
  const unsigned n = Be32(r + 0xD0);
  if (n < 1 || n > 8) return TecnicasSinRaza();
  uint16_t caps[8] = {};
  for (unsigned f = 1; f < n; ++f) caps[f] = Be16(r + 212 + 20 * f);
  const unsigned abiertas = 1 | FormasCombate(caps, int(n));
  for (unsigned f = 0; f < n; ++f)
    if ((abiertas & permitidas & (1u << f)) && (!requiere || caps[f] == requiere)) return false;
  return true;
}
int CapsulaRequerida(int cap) {
  const uint8_t* r = RegistroSkc(cap);
  return r ? int(Be16(r + 16)) : 0;
}

REXCVAR_DEFINE_BOOL(dbz3_heroe_escuelas, true, "DBZ3/Mods",
                    "Hero: unlocked native movesets teach techniques");

// Lista completa (abiertas y por abrir) de la pantalla v2. Los ataques salen de los maestros + el estilo + premios; la
// esquiva, de las entradas "tecnica" de desbloqueos.toml cuya capsula es de esquiva.
std::vector<TecnicaV2> TecnicasV2() {
  std::vector<TecnicaV2> v;
  auto meter = [&v](int cap, int tipo, bool abierta, int maestro, int entreno) {
    if (!TecnicaPermitida(cap)) return;
    for (TecnicaV2& x : v)
      if (x.cap == cap) {   // repetida: vale la mejor condicion
        if (abierta && !x.abierta) x.abierta = true, x.tipo = tipo, x.maestro = maestro, x.entreno = entreno;
        return;
      }
    TecnicaV2 x;
    x.cap = cap, x.tipo = tipo, x.abierta = abierta, x.maestro = maestro, x.entreno = entreno;
    v.push_back(x);
  };
  // T1: maestros
  for (int i = 0; i < kNumMaestros; ++i) {
    const int id = kEstilos[i].id;
    std::vector<Tecnica> esp, def;
    for (const Tecnica& t : CatalogoTecnicas()) {
      if ((t.donante != id && !(REXCVAR_GET(dbz3_heroe_tc) && t.Dueno(id))) || t.cap >= kPrimeraCapsulaMod) continue;
      const int tipo = TipoTecnica(t.cap, id);
      if (tipo == 0) def.push_back(t);
      else if (tipo == 1 || tipo == 2) esp.push_back(t);
    }
    auto porKi = [](const Tecnica& x, const Tecnica& y) { return x.ki != y.ki ? x.ki < y.ki : x.cap < y.cap; };
    std::sort(esp.begin(), esp.end(), porKi);
    std::sort(def.begin(), def.end(), porKi);
    esp.insert(esp.end(), def.begin(), def.end());
    const int n = int(esp.size());
    for (int k = 0; k < n; ++k) {
      const int pct = k == n - 1 && n > 1 ? 100 : k == 0 ? 0 : k == 1 ? 20 : 60;
      const bool ok = MaestroElegido(i) && (g_entreno[i] >= pct || ((g_maestros >> i) & 1));
      meter(esp[size_t(k)].cap, TipoTecnica(esp[size_t(k)].cap, id), ok, id, pct);
    }
  }
  // el estilo propio (si no es maestro) da las suyas, como hasta ahora
  if (MaestroIndice(EstiloId()) < 0)
    for (const Tecnica& t : CatalogoTecnicas())
      if ((t.donante == EstiloId() || (REXCVAR_GET(dbz3_heroe_tc) && t.Dueno(EstiloId()))) &&
          t.cap < kPrimeraCapsulaMod && TipoTecnica(t.cap, EstiloId()) >= 0)
        meter(t.cap, TipoTecnica(t.cap, EstiloId()), true, -1, 0);
  for (int cap : CapsulasPremiadas()) meter(cap, TipoTecnica(cap), true, -1, 0);
  if (REXCVAR_GET(dbz3_heroe_escuelas)) {
    const int activo = DonanteEfectivo();
    auto escuelas = Movesets();   // validos y desbloqueados; -1 es el estilo propio
    if (const auto it = std::find(escuelas.begin(), escuelas.end(), activo); it != escuelas.end())
      std::rotate(escuelas.begin(), it, it + 1);
    for (int id : escuelas) {
      if (id < 0 || id >= 44 || MaestroIndice(id) >= 0) continue;
      for (const Tecnica& t : CatalogoTecnicas()) {
        if (t.cap >= kPrimeraCapsulaMod || (t.donante != id && !(REXCVAR_GET(dbz3_heroe_tc) && t.Dueno(id)))) continue;
        const int tipo = TipoTecnica(t.cap, id);
        if (tipo < 0 || tipo > 2) continue;
        const auto it = std::find_if(v.begin(), v.end(), [&](const TecnicaV2& x) { return x.cap == t.cap; });
        if (it != v.end()) {
          if (!it->abierta) it->abierta = true, it->tipo = tipo, it->escuela = id, it->maestro = -1, it->entreno = 0;
        } else {
          meter(t.cap, tipo, true, -1, 0);
          if (!v.empty() && v.back().cap == t.cap) v.back().escuela = id;
        }
      }
    }
  }
  // T2: esquiva por datos (desbloqueos.toml tipo "tecnica" con capsula de esquiva, maestro y entreno)
  for (const Desbloqueo& d : TablaDesbloqueos()) {
    if (d.tipo != "tecnica" || !EsEsquiva(d.capsula)) continue;
    const int m = MaestroIndice(d.maestro);
    const bool ok = TieneDesbloqueo(d.id) || (m >= 0 && (g_entreno[m] >= d.entreno || ((g_maestros >> m) & 1)));
    meter(d.capsula, 3, ok, d.maestro, d.entreno);
  }
  std::lock_guard<std::mutex> lk(g_tec_mu);
  TecEstadoCargar();
  for (TecnicaV2& x : v) {
    const uint8_t* r = RegistroSkc(x.cap);
    x.ki = r ? r[15] : 0;
    x.formas = r ? r[14] : 0xFF;
    x.requiere = r ? int(Be16(r + 16)) : 0;
    for (const Tecnica& t : CatalogoTecnicas()) {
      if (t.cap != x.cap) continue;
      const int activo = DonanteEfectivo();
      x.donante = REXCVAR_GET(dbz3_heroe_tc) && t.Dueno(activo) ? activo
                 : x.escuela >= 0 ? x.escuela
                 : REXCVAR_GET(dbz3_heroe_tc) && x.maestro >= 0 && t.Dueno(x.maestro) ? x.maestro
                 : t.donante;
      break;
    }
    const auto it = g_tec_estado.find(x.cap);
    x.victorias = it == g_tec_estado.end() ? 0 : it->second.victorias;
    x.nueva = x.abierta && (it == g_tec_estado.end() || !it->second.vista);
    x.nivel = x.victorias >= 15 ? 3 : x.victorias >= 5 ? 2 : 1;
  }
  return v;
}
std::vector<int> InventarioV2() {
  std::vector<int> inv;
  for (const TecnicaV2& x : TecnicasV2())
    if (x.abierta && x.tipo >= 0) inv.push_back(x.cap);
  return inv;
}
int BonoMaestria(const int* caps, int n) {
  if (!REXCVAR_GET(dbz3_heroe_maestria_dano) || !TecnicasV2On() || !caps || n <= 0) return 0;
  std::lock_guard<std::mutex> lk(g_tec_mu);
  TecEstadoCargar();
  const int donante = DonanteEfectivo();
  int puntos = 0;
  for (int k = 0; k < 3; ++k) {
    const int cap = g_equipo[k];
    if (cap <= 0 || !TecnicaPermitida(cap) || !TecnicaActiva(cap) || TipoTecnica(cap, donante) != k ||
        std::find(caps, caps + n, cap) == caps + n ||
        (TecnicasSinRaza() && TecnicaFormaBloqueada(cap))) continue;
    const auto it = g_tec_estado.find(cap);
    if (it == g_tec_estado.end()) continue;
    puntos += it->second.victorias >= 15 ? 2 : it->second.victorias >= 5 ? 1 : 0;
  }
  return 2 * std::min(puntos, 3);
}
void TecnicaVista(int cap) {
  std::lock_guard<std::mutex> lk(g_tec_mu);
  TecEstadoCargar();
  EstadoTec& e = g_tec_estado[cap];
  if (e.vista) return;
  e.vista = true;
  TecEstadoGuardar();
}
// Una victoria del mapa: +1 a cada tecnica equipada que se pudo usar (la de esquiva siempre).
void TecnicasV2Victoria() {
  if (!TecnicasV2On()) return;
  std::lock_guard<std::mutex> lk(g_tec_mu);
  TecEstadoCargar();
  std::string s;
  for (int k = 0; k < 4; ++k) {
    const int cap = g_equipo[k];
    if (cap <= 0 || (k < 3 && !TecnicaActiva(cap))) continue;
    EstadoTec& e = g_tec_estado[cap];
    ++e.victorias;
    s += " " + std::to_string(cap) + ":" + std::to_string(e.victorias);
  }
  if (s.empty()) return;
  TecEstadoGuardar();
  REXLOG_INFO("dbz3 heroe: tecnicas v2: victoria con las equipadas (capsula:victorias){}", s);
}
