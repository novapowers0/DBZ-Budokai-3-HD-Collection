// SPDX-License-Identifier: MIT
// tanda4 iw-mapa (8/11): el mapa de Dragon Mission / Fighter's Road de IW con su logica ORIGINAL (familia de la plantilla
// CampoDA/CampoDC). Corre en el interprete las funciones del ELF EU, sobre un espejo propio hecho SOLO con datos de la
// copia del jugador (nada del juego va al repo):
//   - aparicion de casillas: LoadWaitGeneralDragonMissionIcons_PlayerCommands 0x2FD0F0 + Story_MapObjectsAppearTick 0x2FCCB0
//     (que llama a Story_MapObjectVisibility 0x2FD3A0 y Story_SpawnMapObjectSprite), hasta agotar la lista;
//   - cuadro de mapa libre: GeneralMapScripts_MissionDots... 0x2FE270 (palanca/cruceta, correr, A, L1/R1, B, SELECT) y
//     Story_MapCameraUpdate 0x2F9280 (topes de Goku y camara, matriz de vista y Story_PickObjectAtCursor 0x2F8200);
//   - desbloqueos: DATOS. El estado de cada casilla es un byte por capsula (0x5C1490+0xC4: 0x80 abierta, |= 0xE0 hecha) y los
//     prerrequisitos viven en el #SCD de capsulas (DATA_CMN 1127) que corre Capsule_UpdateUnlockFlagsAt0xC4 0x2AB590.
//     "ExecuteScript_MMI2" (0x11DEF8) NO es un guion: es un memset. El #DMD (DATA_CMN 1093 / 1092) se reubica como el cargador.
// Un solo #include al final de iw_mips.cpp; pasarela_iw.cpp lo incluye con IWM_SOLO_DECL (solo declaraciones).
// Banco sin juego: tools/iw_mips/mapa_banco.py (savestate PCSX2), mapa_espejo.py (desde cero) y mapa_prueba.py (los compara).
// Apagado de serie (dbz3_iw_mapa_original); solo ELF EU (el USA usa el port).
#ifndef IWM_DECL_HECHA
#define IWM_DECL_HECHA
#include <cstdint>
namespace dbz3::iwmips {
namespace mapa {
constexpr int kFilas = 160;   // registros del #DMD que caben (149 en Dragon Mission, 106 en Fighter's Road)
struct Marca { uint32_t clave = 0; uint8_t mascara = 0; };   // estado de capsula: byte[clave] |= mascara
struct Entrada {
  bool inicio = false;       // reconstruye el espejo y calcula la aparicion de las casillas
  bool de_golpe = false;     // con inicio: todas las casillas a la vez (vuelta de combate, cambio de saga, esfera recogida)
  bool road = false;         // Fighter's Road (#DMD 1092)
  int saga = 0;              // area (0..5; Road 0..3)
  const uint8_t* dmd = nullptr; uint32_t dmd_n = 0;   // #DMD crudo
  const uint8_t* scd = nullptr; uint32_t scd_n = 0;   // #SCD de capsulas crudo (DATA_CMN 1127)
  const Marca* marcas = nullptr; uint32_t n_marcas = 0;   // progreso del port traducido a bytes de capsula
  bool fijar_goku = false; float goku[2]{};              // con inicio: posicion de Goku
  float paso = 1.0f;         // fRam0049c7dc: 1,0 a 60 cuadros/s (el EU de 50 Hz usa 1,2)
  bool libre = false;        // este cuadro corre los mandos del mapa libre
  uint16_t mant = 0, pulsa = 0;   // botones de PS2: mantenidos y recien pulsados (cruz 0x4000, L1 0x400, R1 0x800, cuadrado 0x8000)
  int lx = 0, ly = 0;        // palanca izquierda con signo (-128..127; y positivo = abajo)
  uint64_t t = 0;
};
struct Orden { int cmd = 0, arg = 0; };
struct Salida {
  // aparicion (solo con inicio): cuadros (de paso 1,0) a los que sale la casilla de cada fila del #DMD; -1 = no sale
  float sale_a[kFilas]{};
  float duracion = 0;        // cuadros hasta la ultima
  // estado actual
  uint32_t flags[kFilas]{};  // bit 0 viva, bit 1 elegible, bit 2 aun sin aparecer del todo
  float goku[2]{}, cam[2]{}, scroll[2]{};
  int anim = 0;              // animacion de Goku (M+0x220): 0 quieto, 1 aburrido, 2 anda, 3 corre, 4/6/0xC tras elegir
  int sel = -1;              // fila del #DMD bajo Goku (M+0x330) o -1
  int n_ord = 0; Orden ord[16];   // ordenes empujadas a la cola del mapa (0x5D9220/0x5D9260) en este cuadro
  int conf_fila = -1, conf_tipo = -1, conf_idx = -1;   // casilla elegida con A (M+0x1C / +0x14 / +0x10)
  int area_destino = -1;     // area del comando 5 (cambio de saga con L1/R1)
  float pos_defecto[2]{};    // posicion por defecto de Goku en el area destino (0x47DB60 / 0x47DBC0)
  bool transicion = false;   // el original llamo a Transition_SetupWithParams (esfera: escena de cine)
  int trans_tipo = 0, trans_a = 0;
};
inline bool (*g_paso)(const Entrada&, Salida&) = nullptr;
inline bool (*g_activa)() = nullptr;   // cvar dbz3_iw_mapa_original (de serie apagada), dbz3_iw_logica_original y ELF EU
}  // namespace mapa
}  // namespace dbz3::iwmips
#endif

#ifndef IWM_SOLO_DECL
#ifndef IWM_IMPL_HECHA
#define IWM_IMPL_HECHA

REXCVAR_DEFINE_BOOL(dbz3_iw_mapa_original, true, "DBZ3/IW",
                    "IW mapa de Dragon Mission: casillas, palanca, seleccion y desbloqueos con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");

namespace dbz3::iwmips::bubbles {
namespace {
constexpr Dueno kDMapa = Dueno(11);   // 12 = campo/vuelo, 13-15 = botones
// Espejo propio (por encima de los de los minijuegos): gestor, objeto de estado M[0], recurso de capsulas, 0x80 nodos, #DMD y #SCD.
constexpr uint32_t kImMM = 0x11A0000, kImObj0M = 0x11A1000, kImAssetM = 0x11A2000, kImCtxM = 0x11A4000, kImDmdM = 0x11B0000, kImScdM = 0x11E0000;
constexpr uint32_t kImMaxDmdM = 0x30000, kImMaxScdM = 0x8000;
// Funciones y globales del ELF EU (nombres del decompilado).
uint32_t kImAppearM = 0x2FCCB0, kImIconsM = 0x2FD0F0, kImMandosM = 0x2FE270, kImCamaraM = 0x2F9280;
uint32_t kImCapInitM = 0x2AB8E0, kImCapUnlockM = 0x2AB590, kImCapModM = 0x2AB730, kImMiniGokuM = 0x31BFA0, kImTransM = 0x2ADD80;
uint32_t kImPMapaM = 0x49CDAC, kImPCtxM_ = 0x49CDDC, kImCapsM = 0x5CB830, kImEstadoBaseM = 0x5C1490, kImEstadoM = 0x5C1490 + 0xC4,
         kImCapIdsM = 0x5C5510, kImCapHdrM = 0x5C6010, kImCapRecM = 0x5C6030;
uint32_t kImPadM = 0x4ADECC, kImColaCmdM = 0x5D9220, kImColaArgM = 0x5D9260, kImColaNM = 0x5D92A0;
uint32_t kImPantallaDesp = 0;   // se resta a las direcciones de pantalla (0x800 en el USA)
uint32_t kImPosDM = 0x47DB60, kImPosRoadM = 0x47DBC0;
// Parametros de pantalla que deja el arranque (EU, medidos en el savestate del mapa): 512x448 reales y 600x450 virtuales.
constexpr uint32_t kImPantallaM[][2] = {{0x49C824, 1}, {0x49C82C, 0x44000000}, {0x49C830, 0x43E00000}, {0x49C834, 0x200},
                                      {0x49C838, 0x1C0}, {0x49C83C, 0x200}, {0x49C840, 0x200}, {0x49C844, 0x200},
                                      {0x49C848, 0x200}, {0x49C84C, 0x44160000}, {0x49C850, 0x43E10000},
                                      {0x49C854, 0x3F5A740E}, {0x49C858, 0x3F7EDCBB}, {0x49C85C, 0x44160000},
                                      {0x49C860, 0x43E10000}};
// Camara del mapa (M+0x188 = 45 grados la usa la proyeccion de Story_PickObjectAtCursor) y velocidad de Goku.
constexpr uint32_t kImCamaraIniM[][2] = {{0x188, 0x42340000}, {0x18C, 0x3F800000}, {0x19C, 0x3F800000}, {0x1AC, 0x3F800000},
                                       {0x1B4, 0x3F1C61AB}, {0x224, 0x3F800000}, {0x228, 0x3E23D70A}, {0x230, 0x3F800000},
                                       {0x234, 0x40000000}};
// Funciones de sprites, sonido, menus y transiciones (por nombre del decompilado): no deciden la logica del mapa.
constexpr uint32_t kImMudosM[] = {
    0x11DDF0, 0x13F0F0, 0x13F2B0, 0x140930, 0x1B1780, 0x1B1790, 0x1B17A0, 0x1B1810, 0x1B1820, 0x1B1830, 0x1B1850, 0x1B1860,
    0x1B18E0, 0x1B1960, 0x1B1BB0, 0x1B1C00, 0x1B1C80, 0x1B1D10, 0x1B1D30, 0x1B1DE0, 0x1B1F90, 0x1B2030, 0x1B21A0, 0x1B21B0,
    0x1B25E0, 0x1B2820, 0x1B2E00, 0x1B8060, 0x1B8070, 0x1B81C0, 0x1B9300, 0x1B9920, 0x1B9930, 0x1B9940, 0x1E15B0, 0x27B660,
    0x27BF70, 0x27CAF0, 0x27D570, 0x27D610, 0x27D700, 0x27D850, 0x27D8E0, 0x27DA10, 0x27DA90, 0x27DAB0, 0x27DAC0, 0x281E80,
    0x2ABA20, 0x2ADD80, 0x2ADDE0, 0x2ADE10, 0x2AE060, 0x2AE0C0, 0x2AE2C0, 0x2AEEB0, 0x2AF0B0, 0x2AF250, 0x2AF3F0, 0x2AF6D0,
    0x2AFB00, 0x2AFDE0, 0x2AFFF0, 0x2B0330, 0x2B0530, 0x2B0940, 0x2B0E10, 0x2B0F60, 0x2B7D10, 0x2B7D40, 0x2B7DC0, 0x2C4D00,
    0x2DFD80, 0x2DFDC0, 0x2DFE20, 0x2DFE60, 0x2FFD30, 0x31BAE0, 0x31BAF0, 0x368230,
};

// USA (SLUS_218.42): funciones por emparejado de instrucciones (tools/dif_mapa_iw_usa.py pc_eu_a_usa) y datos -0x800 (los
// punteros 0x49CDxx: -0x820), igual que MapaUsa(); sin dorado USA todavia, solo comprobado en ee.py contra el ELF EU.
constexpr uint32_t kImMudosU[] = {
    0x11DDF0, 0x13F0F0, 0x13F2B0, 0x140930, 0x1B1780, 0x1B1790, 0x1B17A0, 0x1B1810, 0x1B1820, 0x1B1830, 0x1B1850, 0x1B1860,
    0x1B18E0, 0x1B1960, 0x1B1BB0, 0x1B1C00, 0x1B1C80, 0x1B1D10, 0x1B1D30, 0x1B1DE0, 0x1B1F90, 0x1B2030, 0x1B21A0, 0x1B21B0,
    0x1B25E0, 0x1B2820, 0x1B2E00, 0x1B8060, 0x1B8070, 0x1B81C0, 0x1B9300, 0x1B9920, 0x1B9930, 0x1B9940, 0x1E15B0, 0x27B450,
    0x27BD60, 0x27C8E0, 0x27D360, 0x27D400, 0x27D4F0, 0x27D640, 0x27D6D0, 0x27D800, 0x27D880, 0x27D8A0, 0x27D8B0, 0x281C70,
    0x2AB530, 0x2AD890, 0x2AD8F0, 0x2AD920, 0x2ADB70, 0x2ADBD0, 0x2ADDD0, 0x2AE9C0, 0x2AEBC0, 0x2AED60, 0x2AEF00, 0x2AF1E0,
    0x2AF610, 0x2AF8F0, 0x2AFB00, 0x2AFE40, 0x2B0040, 0x2B0450, 0x2B0920, 0x2B0A70, 0x2B7A40, 0x2B7A70, 0x2B7B70, 0x2C4AD0,
    0x2DFC20, 0x2DFC60, 0x2DFCC0, 0x2DFD00, 0x2FFB20, 0x31B7F0, 0x31B800, 0x368370,
};
// Pasa las direcciones de este modulo al ELF USA (una vez; la tabla de arranque ya dice que ELF es).
bool MapaUsaIm() {
  static bool hecho = false;
  if (!hecho) {
    hecho = true;
    kImAppearM = 0x2FCAA0, kImIconsM = 0x2FCEE0, kImMandosM = 0x2FE060, kImCamaraM = 0x2F9070;
    kImCapInitM = 0x2AB3F0, kImCapUnlockM = 0x2AB0A0, kImCapModM = 0x2AB240, kImMiniGokuM = 0x31BCB0, kImTransM = 0x2AD890;
    kImPMapaM = 0x49C58C, kImPCtxM_ = 0x49C5BC, kImCapsM = 0x5CB030, kImEstadoBaseM = 0x5C0C90, kImEstadoM = 0x5C0C90 + 0xC4;
    kImCapIdsM = 0x5C4D10, kImCapHdrM = 0x5C5810, kImCapRecM = 0x5C5830;
    kImPadM = 0x4AD6CC, kImColaCmdM = 0x5D8A20, kImColaArgM = 0x5D8A60, kImColaNM = 0x5D8AA0;
    kImPantallaDesp = 0x800;
    kImPosDM = 0x47D360, kImPosRoadM = 0x47D3C0;
  }
  return true;
}

bool ActivaMapa() { return REXCVAR_GET(dbz3_iw_mapa_original) && Listo() != nullptr; }

bool PasoMapa(const mapa::Entrada& in, mapa::Salida& out) {
  if (!REXCVAR_GET(dbz3_iw_mapa_original)) return false;
  Estado* const st = Listo();
  if (!st) return false;
  const bool usa = EsUsaC();
  if (usa) MapaUsaIm();
  Ee& e = st->ee;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  out = mapa::Salida{};
  Ambito amb(*st, kDMapa);
  if (!in.inicio && !amb.continua) {   // el espejo guarda el mapa: si otro minijuego lo piso, se deja al port
    st->dueno = kNadie;
    REXLOG_WARN("dbz3 iwmips mapa: el espejo lo uso otro minijuego (t={}); el mapa usa el port", in.t);
    return false;
  }
  uint32_t trans_tipo = 0, trans_a = 0;
  bool trans = false;
  if (usa) { for (uint32_t a : kImMudosU) amb.Puente(a, [](Ee& c) { V0(c, 0); }); }
  else for (uint32_t a : kImMudosM) amb.Puente(a, [](Ee& c) { V0(c, 0); });
  // MiniGokuAnimations(nodo, anim, tabla): lo unico que hace la logica es dejar la animacion en nodo+0x48.
  amb.Puente(kImMiniGokuM, [](Ee& c) { c.W32(uint32_t(c.r[4]) + 0x48, uint32_t(c.r[5])); });
  // Transition_SetupWithParams(tipo, a, b, c): la escena de cine de una esfera (0x11); la transicion la hace el port.
  amb.Puente(kImTransM, [&](Ee& c) { trans = true; trans_tipo = uint32_t(c.r[4]); trans_a = uint32_t(c.r[5]); });
  auto Llama = [&](uint32_t dir, std::initializer_list<uint32_t> a) {
    if (e.Llamar(dir, a, 5000000)) return true;
    REXLOG_WARN("dbz3 iwmips mapa: {} en {:#x} (t={}); el mapa sigue con el port", e.error(), dir, in.t);
    st->dueno = kNadie;
    return false;
  };
  auto Nodos = [&](bool aparecidos) {
    for (uint32_t i = 0; i < 0x80; ++i) {
      const uint32_t n = kImCtxM + i * 0x30;
      const uint32_t fl = e.R32(n + 4);
      const uint32_t fila = e.R16(n);
      if (fl && fila < mapa::kFilas) {
        out.flags[fila] = fl;
        if (aparecidos && out.sale_a[fila] < 0) out.sale_a[fila] = 0;   // marca provisional (se ajusta abajo)
      }
    }
  };
  e.Wf(kPasoT, in.paso);
  if (in.inicio) {
    if (!in.dmd || in.dmd_n < 0x20 || in.dmd_n > kImMaxDmdM || !in.scd || in.scd_n < 0x40 || in.scd_n > kImMaxScdM) {
      st->dueno = kNadie;
      return false;
    }
    e.Cero(kImMM, 0x1000); e.Cero(kImObj0M, 0x100); e.Cero(kImAssetM, 0x100); e.Cero(kImCtxM, 0x2000);
    e.Cero(kImColaCmdM, 0x84);
    for (const auto& p : kImPantallaM) e.W32(p[0] - kImPantallaDesp, p[1]);
    for (uint32_t i = 0; i < in.dmd_n; i += 4) {   // #DMD reubicado: '#' -> '!' y la tabla de punteros (+0x10) pasa a absoluta
      uint32_t w = 0;
      std::memcpy(&w, in.dmd + i, std::min<uint32_t>(4, in.dmd_n - i));
      e.W32(kImDmdM + i, w);
    }
    e.W8(kImDmdM, 0x21);
    const uint32_t n_reg = e.R32(kImDmdM + 8);
    if (n_reg == 0 || n_reg > mapa::kFilas || 0x10 + 4 * n_reg > in.dmd_n) {
      REXLOG_WARN("dbz3 iwmips mapa: #DMD con {} registros; el mapa usa el port", n_reg);
      st->dueno = kNadie;
      return false;
    }
    for (uint32_t k = 0; k < n_reg; ++k) e.W32(kImDmdM + 0x10 + 4 * k, e.R32(kImDmdM + 0x10 + 4 * k) + kImDmdM);
    for (uint32_t i = 0; i < in.scd_n; i += 4) {
      uint32_t w = 0;
      std::memcpy(&w, in.scd + i, std::min<uint32_t>(4, in.scd_n - i));
      e.W32(kImScdM + i, w);
    }
    e.W32(kImPMapaM, kImMM); e.W32(kImPCtxM_, kImCtxM); e.W32(kImMM, kImObj0M);
    e.W32(kImMM + (in.road ? 0x128 : 0x118), kImDmdM);
    e.W32(kImMM + 0x4AC, in.road ? 1 : 0);
    e.W32(kImMM + 0x398, uint32_t(in.saga)); e.W32(kImMM + 0x394, uint32_t(in.saga));   // areas 0..5: identidad en la tabla 0x49C348/50
    e.W32(kImMM + 0x20, 0xFFFFFFFFu); e.W32(kImMM + 0x24, 0xFFFFFFFFu);
    for (const auto& p : kImCamaraIniM) e.W32(kImMM + p[0], p[1]);
    // capsulas: Capsule_LoadAndInitData(recurso) con las tablas globales como las deja el arranque
    const uint32_t cap[4] = {1, kImCapIdsM, kImCapHdrM, kImCapRecM};
    for (int k = 0; k < 4; ++k) e.W32(kImCapsM + 4 * uint32_t(k), cap[k]);
    e.W32(kImAssetM + 0x54, kImScdM);
    if (!Llama(kImCapInitM, {kImAssetM})) return false;
    for (uint32_t k = 0; k < in.n_marcas; ++k) {
      if (!Llama(kImCapModM, {in.marcas[k].clave})) return false;
      const uint32_t idx = uint32_t(e.r[2]) & 0xFFFF;
      if (idx < 0x580) e.W8(kImEstadoM + idx, uint8_t(e.R8(kImEstadoM + idx) | in.marcas[k].mascara));
    }
    if (!Llama(kImCapUnlockM, {kImEstadoBaseM})) return false;
    if (in.fijar_goku) {
      e.Wf(kImMM + 0x1D0, in.goku[0]); e.Wf(kImMM + 0x1D4, in.goku[1]);
      e.Wf(kImMM + 0x29C, in.goku[0]); e.Wf(kImMM + 0x2A0, in.goku[1]);
    }
    // aparicion: ICONS (comando 10; argumento 1 = de golpe) y AppearTick hasta agotar la lista
    for (int i = 0; i < mapa::kFilas; ++i) out.sale_a[i] = -1.0f;
    e.W32(kImColaArgM, in.de_golpe ? 1u : 0u);
    e.W32(kImColaNM, 1);
    if (!Llama(kImIconsM, {kImObj0M})) return false;
    Nodos(true);
    for (int i = 0; i < mapa::kFilas; ++i) if (out.sale_a[i] >= 0) out.sale_a[i] = 0;
    int k = 0;
    while (!in.de_golpe && int32_t(e.R32(kImObj0M + 0x30)) >= 0 && k < 600) {
      if (!Llama(kImAppearM, {kImObj0M})) return false;
      ++k;
      for (uint32_t i = 0; i < 0x80; ++i) {
        const uint32_t n = kImCtxM + i * 0x30, fila = e.R16(n);
        if (e.R32(n + 4) && fila < mapa::kFilas && out.sale_a[fila] < 0) out.sale_a[fila] = float(k) * in.paso;
      }
    }
    out.duracion = float(k) * in.paso;
    e.W32(kImColaNM, 0);
    if (log) {
      std::string ms;
      for (uint32_t q = 0; q < in.n_marcas; ++q) { char tmp[24]; std::snprintf(tmp, sizeof tmp, "%X:%X,", in.marcas[q].clave, in.marcas[q].mascara); ms += tmp; }
      REXLOG_INFO("dbz3 iwmips mapa ini t={} road={} saga={} de_golpe={} marcas={} paso={} cuadros={} goku={:.9g},{:.9g}", in.t, int(in.road),
                  in.saga, int(in.de_golpe), ms.empty() ? "-" : ms, in.paso, k, in.goku[0], in.goku[1]);
    }
  } else {
    e.W32(kImColaNM, 0);   // la cola real la lleva el port (ordenes_); aqui solo se recogen las que empuja el original
  }
  Nodos(false);
  if (in.libre) {
    const uint32_t M = kImMM;
    e.W16(kImPadM, in.mant); e.W16(kImPadM + 2, in.pulsa);
    e.W8(kImPadM + 0xE, uint8_t(int8_t(in.lx))); e.W8(kImPadM + 0xF, uint8_t(int8_t(in.ly)));
    const Cronometro cr(e);
    if (!Llama(kImMandosM, {}) || !Llama(kImCamaraM, {})) return false;
    const double us = cr.Us();
    st->Cuenta(us);
    out.goku[0] = e.Rf(M + 0x1D0); out.goku[1] = e.Rf(M + 0x1D4);
    out.cam[0] = e.Rf(M + 0x29C); out.cam[1] = e.Rf(M + 0x2A0);
    out.scroll[0] = e.Rf(M + 0x2A4); out.scroll[1] = e.Rf(M + 0x2A8);
    out.anim = int(e.R32(M + 0x220));
    if (const uint32_t nodo = e.R32(M + 0x330); nodo >= kImCtxM && nodo < kImCtxM + 0x80 * 0x30) {
      const uint32_t fila = e.R16(nodo);
      if (fila < mapa::kFilas) out.sel = int(fila);
    }
    const uint32_t n = std::min<uint32_t>(e.R32(kImColaNM), 16);
    for (uint32_t i = 0; i < n; ++i) out.ord[out.n_ord++] = {int(e.R32(kImColaCmdM + 4 * i)), int(e.R32(kImColaArgM + 4 * i))};
    for (int i = 0; i < out.n_ord; ++i) {
      if (out.ord[i].cmd == 9) {   // menu de mision: casilla elegida (M+0x1C nodo, +0x14 tipo, +0x10 indice)
        const uint32_t nodo = e.R32(M + 0x1C);
        if (nodo >= kImCtxM && nodo < kImCtxM + 0x80 * 0x30) out.conf_fila = int(e.R16(nodo));
        out.conf_tipo = int(e.R32(M + 0x14)); out.conf_idx = int(e.R32(M + 0x10));
      }
      if (out.ord[i].cmd == 5) out.area_destino = out.ord[i].arg;
    }
    if (trans) {   // esfera de cristal (tipo 2): 0xC0 en su byte y escena 0x11 con el indice +0x10
      out.transicion = true; out.trans_tipo = int(trans_tipo); out.trans_a = int(trans_a);
      for (int i = 0; i < out.n_ord; ++i) if (out.ord[i].cmd == 0x10 && out.conf_fila < 0) {
        const uint32_t nodo = e.R32(M + 0x1C);
        if (nodo >= kImCtxM && nodo < kImCtxM + 0x80 * 0x30) out.conf_fila = int(e.R16(nodo));
        out.conf_tipo = int(e.R32(M + 0x14)); out.conf_idx = int(e.R32(M + 0x10));
      }
    }
    if (out.area_destino >= 0 && out.area_destino < 6) {
      const uint32_t base = (in.road ? kImPosRoadM : kImPosDM) + 16u * uint32_t(out.area_destino);
      out.pos_defecto[0] = e.Rf(base); out.pos_defecto[1] = e.Rf(base + 4);
    }
    if (log) {   // solo pruebas: la casilla mas cercana a Goku y sus banderas (hace falta 3 y no 4 para poder elegirla)
      float mejor = 1e9f; uint32_t mf = 0, mfi = 0;
      for (uint32_t i = 0; i < 0x80; ++i) {
        const uint32_t n = kImCtxM + i * 0x30;
        if (!e.R32(n + 4)) continue;
        const float d = std::hypot(e.Rf(n + 0x18) - out.goku[0], e.Rf(n + 0x1C) - out.goku[1]);
        if (d < mejor) mejor = d, mf = e.R32(n + 4), mfi = e.R16(n);
      }
      REXLOG_INFO("dbz3 iwmips mapa cerca: fila {} flags {:X} dist {:.1f}", mfi, mf, mejor);
    }
    if (log) {
      REXLOG_INFO("dbz3 iwmips mapa t={} mant={:X} pulsa={:X} lx={} ly={} paso={} goku={:.9g},{:.9g} cam={:.9g},{:.9g} anim={} sel={} ord={} "
                  "conf={} instr={} us={:.1f}",
                  in.t, in.mant, in.pulsa, in.lx, in.ly, in.paso, out.goku[0], out.goku[1], out.cam[0], out.cam[1], out.anim, out.sel,
                  out.n_ord, out.conf_fila, e.n - cr.n0, us);
    }
  }
  Nodos(false);
  return true;
}

const bool kRegistradoMapa = (mapa::g_paso = &PasoMapa, mapa::g_activa = &ActivaMapa, true);
}  // namespace
}  // namespace dbz3::iwmips::bubbles
#endif
#endif
