// Selector de escenarios del Duelo (mn_StageSel) y sus ganchos, extraido de escenarios.cpp.
// Movimiento textual: mismo espacio de nombres (dbz3::escenarios y su anonimo) y mismo orden de
// inicializacion. NO contiene la politica de transiciones (PasarZonaB, TickEscena...), que sigue en
// escenarios.cpp. Incluye las copias generadas escenarios_sel_gen(_eu).inc del mismo directorio.
// ---------------------------------------------------------------------------------------------------
// SELECTOR DE ESCENARIOS DEL DUELO (mn_StageSel): una casilla nueva por alta (13 + k -> registro 17 + k).
// Datos ampliados y tablas por casilla propias: escenarios_sel_gen.inc (tools/gen_escenarios_sel.py).
// Imagenes de la casilla nueva: el #AZT `menu` del mod (0 circulo, 1 vista, 2 rotulo, 3 bola) se carga con
// los del selector y el juego lo registra al crearle las texturas. Lo de la casilla nueva se dibuja con ese
// #AZT puesto como banco de texturas (el del selector, D+4, queda igual): circulo y bola (sub_8219D520 /
// sub_8219D6E8), y la vista y el rotulo (sprites copiados de los nativos, tabla #ACA ampliada en D+0 +28;
// se crean en sub_8219CC48 y se dibujan en sub_8219E828). Sin altas todo es el codigo original.
// ---------------------------------------------------------------------------------------------------
// valores de partida (US); Tablas() los cambia por las tablas propias antes de que corra ninguna copia Sel_/SelEu_
uint32_t dbz3_esc_f94 = 0x82020F94, dbz3_esc_fb0 = 0x82020FB0, dbz3_esc_trans = 0x82373384, dbz3_esc_limite = 11;
void dbz3_esc_instalar(PPCContext& ctx, uint8_t* base, uint32_t d);
void dbz3_esc_banco(PPCContext& ctx, uint8_t* base, uint32_t d, bool nuevo);
REX_EXTERN(sub_821DAE30);   // SLOTS_ESC-08: ADXT_SetOutVol(handle, volumen) de la libreria ADX (solo US)

namespace {
#include "escenarios_sel_gen.inc"
#if defined(DBZ3_DUAL_REGION)
#include "escenarios_sel_gen_eu.inc"   // las mismas copias del codigo EU (tools/gen_escenarios_sel.py)
#define SEL(fn) DBZ3_CALL2(Sel_##fn, SelEu_##fn)
#else
#define SEL(fn) Sel_##fn
#endif

using dbz3::escenarios::g_altas;
using dbz3::escenarios::g_visibles;
using dbz3::escenarios::g_pos_sel;
using dbz3::escenarios::g_cuantos;
using dbz3::escenarios::kCeldas;
constexpr uint32_t kCasillasSerie = 13;   // 0..11 escenarios, 12 = "?"
constexpr dbz3::GA kGlobalSel = 0x8245F25C;   // estructura del selector: +20 tarea (+48 = datos)

bool Ext() { return !g_altas.empty() && dbz3::escenarios::g_mem; }

uint32_t Alloc(uint32_t n) {
  const uint32_t a = dbz3::escenarios::g_mem->SystemHeapAlloc(n, 0x20);
  if (a) std::memset(dbz3::escenarios::g_mem->TranslateVirtual<uint8_t*>(a), 0, n);
  return a;
}

uint32_t Llamar(PPCContext& ctx, uint8_t* base, void (*fn)(PPCContext&, uint8_t*), uint32_t r3, uint32_t r4 = 0,
                uint32_t r5 = 0) {
  PPCContext c = ctx;
  c.r3.u64 = r3;
  c.r4.u64 = r4;
  c.r5.u64 = r5;
  fn(c, base);
  return c.r3.u32;
}

std::vector<uint32_t> g_cargas;       // nodo de lectura AFS del #AZT de cada alta (sub_82083968); 0 = sin pedir
std::vector<uint32_t> g_reservas;     // tabla y copias de sprite de esta entrada al selector: se liberan al entrar la vez siguiente
// Reserva de lecturas del juego (sub_82083968): nodos en uso en 0x823863BC, tope en 0x823863B0 (128 para TODO el
// juego); llena, devuelve -1. Los #AZT se piden solo mientras queden kHuecoJuego libres (el selector pide 5 suyas
// justo despues, sub_8219C7B0) y el resto al ir volviendo nodos (Recoger los devuelve), cada fotograma.
constexpr uint32_t kCargaHecha = 1;   // en g_cargas: ya recogida (o no valia); no se vuelve a pedir
constexpr uint32_t kHuecoJuego = 16;
bool Pendiente(uint32_t h) { return h > kCargaHecha && h != 0xFFFFFFFFu; }
// Solo US: 0x823863B0/BC no tienen pareja EU medida (tools/eu_map.py); en EU se piden hasta que la reserva da -1.
uint32_t LecturasUsadas(uint8_t* base) { return dbz3::g_guest_eu ? 0 : REX_LOAD_U32(0x823863BCu); }
uint32_t LecturasTope(uint8_t* base) { return dbz3::g_guest_eu ? 0 : REX_LOAD_U32(0x823863B0u); }
// SLOTS_ESC-03 (sonda, solo US y con DBZ3_SONDA_ESCENARIO): estado del asignador del juego (sub_82089C10 lo actualiza):
// 0x8238A6F0 bloques vivos, 0x8238A6F4 bloques vivos sin el bit 0 de su monton, 0x8238A6F8 bytes libres.
void SondaMonton(uint8_t* base, const char* donde) {
  if (dbz3::g_guest_eu || !std::getenv("DBZ3_SONDA_ESCENARIO")) return;
  REXLOG_INFO("dbz3 escenarios: monton {}: bloques {} / {}, libres {} B, lecturas {}", donde,
              REX_LOAD_U32(0x8238A6F0u), REX_LOAD_U32(0x8238A6F4u), REX_LOAD_U32(0x8238A6F8u), LecturasUsadas(base));
  // SLOTS_ESC-03b: montones de XMem (0x8238D708: contextos LZX y new/delete; 0x823917D8: el de 8 MB de sub_820A2780);
  // +16524 bytes en uso, +16528 bloques (sub_820A2328). Y el trabajo del descompresor LZX (sub_820A4750/sub_820A47D8):
  // objeto *(0x823958A4)+3312, +4 ocupado, +8 origen, +12 destino.
  const uint32_t dz = REX_LOAD_U32(0x823958A4u), dc = dz ? dz + 3312 : 0;
  REXLOG_INFO("dbz3 escenarios: xmem {}: {} B/{} bloques, 8MB {} B/{} bloques; lzx ocupado {} origen {:08X} destino {:08X}",
              donde, REX_LOAD_U32(0x8238D708u + 16524), REX_LOAD_U32(0x8238D708u + 16528),
              REX_LOAD_U32(0x823917D8u + 16524), REX_LOAD_U32(0x823917D8u + 16528), dc ? REX_LOAD_U8(dc + 4) : 0,
              dc ? REX_LOAD_U32(dc + 8) : 0, dc ? REX_LOAD_U32(dc + 12) : 0);
}
std::vector<uint32_t> g_azt;          // #AZT de cada alta ya en memoria y registrado (0 = no esta)
uint32_t g_datos = 0, g_tabla_nat = 0;

// DESPLAZAMIENTO CONTINUO (peticion a, 8/11; misma idea que las casillas virtuales de la rueda de personajes): cada casilla
// del carrusel (0..23) lleva un CONTENIDO: un escenario nativo (su casilla 0..11) o un alta (kAlta + indice). Con mas de 11
// altas, las casillas de la parte de atras del carrusel (fuera de la vista) cambian de contenido al mover el cursor, y la
// rueda sigue sin fin por todos los escenarios. El "?" (12) no se toca. Con 11 o menos, cada casilla lleva lo suyo: el
// selector de siempre.
constexpr int kAlta = 1000;
uint32_t Celdas() { return std::min<uint32_t>(g_visibles, uint32_t(kCeldas)); }   // casillas nuevas en la lista (13..)
bool Virtual() { return g_visibles > uint32_t(kCeldas); }
std::array<int, kSelMax> g_cont{};   // contenido de cada casilla (-1 = la casilla no se usa o es el "?")
int ContenidoPropio(uint32_t c) {
  return c < 12 ? int(c) : c >= kCasillasSerie && c - kCasillasSerie < Celdas() ? kAlta + int(c - kCasillasSerie) : -1;
}

// #AZT de la casilla c (0 si lleva un escenario del juego o su alta no tiene imagenes).
uint32_t AztDe(uint32_t c) {
  if (c >= kSelMax) return 0;
  const int it = g_cont[c];
  return it >= kAlta && size_t(it - kAlta) < g_azt.size() ? g_azt[size_t(it - kAlta)] : 0;
}

// 12b: #AZT que debe verse en la casilla `c`. mn_StageSel escribe cada fotograma el registro elegido
// (con el "flag" de variante de Circle) en el byte +30 de sus datos (global 0x824BA110). Si ese registro
// es un alta con imagenes, se ponen las suyas: asi el icono cambia al mantener Circle cuando el alta
// declara `variante` en su stage.toml. Los registros de serie 11..16 NO traen icono propio (el atlas
// data_<idioma>.afs 2043 solo tiene 13 vistas: 12 casillas + el "?"), asi que devuelven el de la casilla
// (el normal). Ver VERIFICACION.md del seguimiento.
uint32_t AztSel(uint8_t* base, uint32_t c) {
  if (c < kCasillasSerie) return AztDe(c);
  const uint32_t reg = REX_LOAD_U8(dbz3::GA(0x824BA110) + 30);
  return reg >= uint32_t(dbz3::escenarios::kSerie) && reg - dbz3::escenarios::kSerie < g_azt.size() &&
                 g_azt[reg - dbz3::escenarios::kSerie]
             ? g_azt[reg - dbz3::escenarios::kSerie]
             : AztDe(c);
}

// ---------------------------------------------------------------------------------------------------
// FASE 0 (musica, 30/10, MEDIDO con DBZ3_TEST_VOCES, smoke_a/batalla escenario 2): sub_820F3980(handle, id, situacion) es el
// sistema de VOCES (122 ids = personajes, no playlists de musica): en el arranque del combate solo abre la entrada 290 por ella
// (lr 820F3A0C). El combate de B3 NO abre ninguna pista de musica por el mezclador del juego; las 4267-4337 que se abren van
// antes, en los menus. Por tanto "Original" = sin musica de combate, y anadir una pista no sustituye nada. El texto de abajo
// que habla de playlists de musica es la hipotesis ANTERIOR, refutada; kMusicaRegistro sigue en -1 porque no hay nada que mapear.
// 12c (seguimiento, hipotesis anterior): musica de combate con vista previa en el selector. La pista la arranca EL JUEGO al
// empezar la pelea por sub_820F3980(handle, playlist, track): lee el numero de pistas de la playlist
// (*(0x82328298 + playlist*4)) y el indice ADX del item (*(*(0x823280B0 + playlist*4) + track*4)) y lo
// reproduce con sub_82081FE8(item | 0x20000, -3) (generated/dbz3_recomp.30.cpp:6253-6344). El
// (playlist, track) lo pone un comando de guion del escenario (sub_8211EF60, generated/dbz3_recomp.10.cpp:
// 8659-8750); NO es un campo del registro: el `id_SE` del registro (+0x14) es el ambiente, no la musica.
// La pista por registro de abajo es [H] (pendiente de medir en el juego; ver VERIFICACION.md §12c); un
// alta la declara en su stage.toml (`musica = <entrada de adx_usa.afs>`). Solo US.
// ---------------------------------------------------------------------------------------------------
constexpr int kMusicaRegistro[dbz3::escenarios::kSerie] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,   // 0..16: medir en el juego
};

// Indice ADX (adx_usa.afs) de la musica de combate del registro r, o -1 si no hay.
int MusicaDeRegistro(int r) {
  if (r >= 0 && r < dbz3::escenarios::kSerie) return kMusicaRegistro[r];
  if (r >= dbz3::escenarios::kSerie && r - dbz3::escenarios::kSerie < int(g_altas.size()))
    return g_altas[size_t(r - dbz3::escenarios::kSerie)].musica;
  return -1;
}

// Fase 3: eleccion de la musica de combate en el selector de escenarios. L1/R1 (bits 0x0400/0x0800 del registro del mando 0,
// medidos en IW) o las teclas [ y ] pasan a la pista anterior/siguiente (Original -> pistas). El cvar dbz3_musica_combate guarda
// la eleccion (y musica.toml); la reproduccion es la de siempre (OirMusicaPs2, fuera del mezclador: no toca el online).
// [H] que L1/R1 estan libres en esta pantalla (la sonda DBZ3_SONDA_ESCENARIO no los ha visto usados).
int64_t AhoraMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Recolocar(uint8_t* base, uint32_t d);
// Registro del escenario que mira el cursor (casilla d+604 por su tabla casilla -> registro); -1 en el "?" o sin datos.
// (+30 de 0x824BA110 no sigue al cursor en cada fotograma: medido 8/11 con la sonda.)
int RegistroCursor(uint8_t* base, uint32_t d) {
  if (!d) return -1;
  const uint32_t c = REX_LOAD_U16(d + 604);
  if (c == 12 || c >= kSelMax) return -1;
  if (dbz3_esc_trans != 0x82373384) return int(REX_LOAD_U16(dbz3_esc_trans + 4 * c));
  return c < 12 ? int(REX_LOAD_U16(dbz3::GA(0x82373384) + 4 * c)) : -1;
}
void EleccionMusical(uint8_t* base, uint32_t d) {
  dbz3::escenarios::g_selector_visto = AhoraMs();
  dbz3::escenarios::g_reg_sel = RegistroCursor(base, d);
  static uint16_t antes = 0;
  const uint16_t ahora = REX_LOAD_U16(dbz3::GA(0x8238B780) + 12);
  const uint16_t pulsado = uint16_t(ahora & ~antes);
  antes = ahora;
  int delta = (pulsado & 0x0800) ? 1 : (pulsado & 0x0400) ? -1 : 0;
  if (const int k = dbz3::musica::DeltaTeclado()) delta = k;
  if (delta) dbz3::musica::CambiarEleccion(delta);
  Recolocar(base, d);   // desplazamiento continuo: el cursor ya se ha movido en este fotograma
}

std::string g_mus_sonando;   // clave de la vista previa que esta sonando ("" = ninguna)
std::string g_mus_espera;    // SLOTS_ESC-08: clave de la pista que espera a que el cursor pare
int g_mus_quieto = 0;        // fotogramas seguidos con el cursor en la misma casilla

// SEL02: aislar la vista previa. Mientras suena la pista del escenario, `audio_gain` del SDK se guarda y se
// pone a 0: la musica del menu y los efectos del guest dejan de oirse y solo queda la pista (que sale por el
// reproductor ADX del port, fuera de la mezcla). Se devuelve el valor del jugador al cambiar de casilla, al
// apagar la cvar o al salir del selector; no se escribe nunca en el TOML.
bool g_audio_aislado = false;
std::string g_audio_antes;
bool g_adx_quiero = false, g_adx_callado = false;   // SLOTS_ESC-08

// SLOTS_ESC-08 (dbz3_selector_musica_solo_adx, solo US): en vez de audio_gain a 0, se baja a -96 dB SOLO la musica del
// menu: los canales ADX del juego (sub_82082190: cuenta i8 en 0x8238626C, tabla 0x82386270 de 24 B: +0 handle ADXT,
// +12 volumen puesto por el juego) con ADXT_SetOutVol (sub_821DAE30). Los efectos del cursor no son ADX y siguen.
// Al volver se repone el volumen que el juego dejo en +12.
void CallarAdxMenu(PPCContext& ctx, uint8_t* base, bool callar) {
  if (dbz3::g_guest_eu || callar == g_adx_callado) return;
  g_adx_callado = callar;
  const int n = int(int8_t(REX_LOAD_U8(0x8238626Cu)));
  const uint32_t tabla = REX_LOAD_U32(0x82386270u);
  for (int i = 0; tabla && i < n; ++i) {
    const uint32_t h = REX_LOAD_U32(tabla + 24 * uint32_t(i));
    if (h) Llamar(ctx, base, sub_821DAE30, h, callar ? uint32_t(-960) : REX_LOAD_U32(tabla + 24 * uint32_t(i) + 12));
  }
  REXLOG_INFO("dbz3 escenarios: musica del menu (ADX, {} canales) {}", n, callar ? "callada" : "repuesta");
}

void AislarAudioJuego(bool aislar) {
  if (REXCVAR_GET(dbz3_selector_musica_solo_adx)) {   // SLOTS_ESC-08: el ADX se calla en el gancho (necesita ctx)
    g_adx_quiero = aislar;
    return;
  }
  if (aislar == g_audio_aislado) return;
  if (aislar) {
    g_audio_antes = rex::cvar::GetFlagByName("audio_gain");
    g_audio_aislado = true;
    rex::cvar::SetFlagByName("audio_gain", "0");
    REXLOG_INFO("dbz3 escenarios: vista previa aislada (audio del juego callado; audio_gain {} -> 0)", g_audio_antes);
  } else {
    g_audio_aislado = false;
    rex::cvar::SetFlagByName("audio_gain", g_audio_antes.empty() ? "1" : g_audio_antes);
    REXLOG_INFO("dbz3 escenarios: audio del juego restaurado (audio_gain {})", g_audio_antes.empty() ? "1" : g_audio_antes);
  }
}

// Corta la vista previa (al salir del selector o al apagarla con la cvar).
void CortarVistaPreviaMusica() {
  if (!g_mus_sonando.empty()) {
    dbz3::pasarela::CallarMusicaPs2();
    dbz3::musica::Parar();
    g_mus_sonando.clear();
  }
  AislarAudioJuego(false);
}

// Suena la pista del registro elegido en vivo (0x824BA110 + 30, que mn_StageSel reescribe cada fotograma
// con el flag de variante de Circle); al cambiar de casilla se corta y entra la nueva. Si el escenario no
// declara pista propia (todos los de serie), suena la musica de combate ELEGIDA (dbz3_musica_combate, la
// misma de la tira MUSICA): una sola pista, con su titulo, sin reinicio por fotograma. Con
// dbz3_selector_musica_aislada tambien se calla la mezcla del guest mientras suena (SEL02).
void VistaPreviaMusica(uint8_t* base, uint32_t d) {
  const bool aislar = REXCVAR_GET(dbz3_selector_musica_aislada);
  if (!REXCVAR_GET(dbz3_selector_musica) || !d) {
    CortarVistaPreviaMusica();
    return;
  }
  const int reg = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 30));
  const int propia = MusicaDeRegistro(reg);
  dbz3::musica::Pista elegida;
  std::string clave;
  if (propia >= 0) {
    clave = "alta:" + std::to_string(propia);
  } else {
    elegida = dbz3::musica::ElegidaAhora();
    if (elegida.entrada < 0) {   // Original: no hay pista de escenario que suene
      CortarVistaPreviaMusica();
      return;
    }
    clave = "mus:" + elegida.fuente + ":" + std::to_string(elegida.entrada);
  }
  if (clave == g_mus_sonando) {
    AislarAudioJuego(aislar);
    return;
  }
  // SLOTS_ESC-08: antes de arrancar la pista nueva, esperar a que el cursor pare (~200 ms, 12 fotogramas) para no
  // encadenar arranques al recorrer la rueda; mientras se mueve, la pista anterior se corta (nunca suenan dos).
  if (REXCVAR_GET(dbz3_selector_musica_solo_adx)) {
    if (clave != g_mus_espera) {
      g_mus_espera = clave;
      g_mus_quieto = 0;
    }
    if (++g_mus_quieto < 12) {
      if (!g_mus_sonando.empty()) CortarVistaPreviaMusica();
      return;
    }
  } else {
    g_mus_quieto = 0;
  }
  CortarVistaPreviaMusica();
  g_mus_sonando = clave;
  AislarAudioJuego(aislar);
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.5f;
  if (propia >= 0) {
    REXLOG_INFO("dbz3 escenarios: vista previa musical del escenario {} ('{}'): adx_usa {}", reg,
                dbz3::escenarios::Nombre(reg, 1), propia);
    dbz3::pasarela::OirMusicaPs2(dbz3::HostDataRoot(), "adx_usa.afs", propia, vol);
  } else {
    REXLOG_INFO("dbz3 escenarios: vista previa musical del escenario {} ('{}'): {} (entrada {})", reg,
                dbz3::escenarios::Nombre(reg, 1), elegida.nombre.empty() ? dbz3::musica::TextoEleccion() : elegida.nombre,
                elegida.entrada);
    dbz3::musica::Escuchar(elegida, vol);
  }
}

// SEL01 ronda 6: el menu final inyecto B y el juego volvio al selector. Aqui se deja la casilla "?" y se
// pulsa A para que el juego elija otro escenario y lo cargue por el camino normal. Se hace en dos fases para
// que mn_StageSel vea la casilla antes de la confirmacion. [H] si el selector no llega a abrirse, el aviso
// queda pendiente hasta la salida y se limpia alli.
void ConfirmarAzarEnSelector(uint8_t* base, uint32_t d) {
  if (!dbz3::escenarios::g_azar_pendiente || !d) return;
  REX_STORE_U16(d + 604, uint16_t(dbz3::escenarios::kSelCasilla));   // cursor en "?"
  if (dbz3::escenarios::g_azar_fase < 3) {
    ++dbz3::escenarios::g_azar_fase;
    return;
  }
  if (dbz3::escenarios::g_azar_fase == 3) {
    dbz3::escenarios::PulsoRevancha(0x1000, 12);   // SLOTS_ESC-07: A por el mando virtual; confirma la casilla "?"
    ++dbz3::escenarios::g_azar_fase;
    return;
  }
  if (dbz3::escenarios::g_azar_fase < 9) {
    ++dbz3::escenarios::g_azar_fase;
    if (dbz3::escenarios::g_azar_fase == 9) {
      dbz3::escenarios::g_azar_pendiente = false;
      dbz3::escenarios::g_azar_fase = 0;
    }
  }
}

// SONDA del selector (frente 15; DBZ3_SONDA_ESCENARIO=1, la misma que usa el frente 07): imprime la casilla
// del cursor (+25) y el registro elegido (+30) cada vez que cambian. Sin ella, la navegacion de la prueba
// 12a hay que adivinarla; con ella, una sola ejecucion da la cuenta exacta de teclas hasta la casilla "?".
void SondaSel(uint8_t* base, uint32_t d) {
  const int casilla = d ? int(REX_LOAD_U16(d + 604)) : int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 25));
  const int reg = d ? RegistroCursor(base, d) : int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 30));
  static int ult_c = -2, ult_r = -2;
  if (casilla == ult_c && reg == ult_r) return;
  ult_c = casilla;
  ult_r = reg;
  REXLOG_INFO("dbz3 escenarios: sel casilla {} (registro {})", casilla, reg);
}

uint32_t g_spr0 = 0;   // primer sprite propio (vista y rotulo de un alta, 2 por casilla); 0 = aun no hay

// Pone en la casilla c el contenido `it`: registro (y su variante de Circle), circulo/bola y vista/rotulo. mn_StageSel lee
// estas tablas cada fotograma: el cambio vale al momento.
void PonerContenido(uint8_t* base, uint32_t c, int it) {
  g_cont[c] = it;
  if (dbz3_esc_trans == 0x82373384 || it < 0) return;   // Tablas() aun no ha corrido, o casilla sin uso
  if (it < kAlta) {   // escenario del juego: sus datos nativos
    REX_STORE_U32(dbz3_esc_trans + 4 * c, REX_LOAD_U32(dbz3::GA(0x82373384) + 4 * uint32_t(it)));
    for (uint32_t b = 0; b < 2; ++b) {
      REX_STORE_U8(dbz3_esc_f94 + 2 * c + b, REX_LOAD_U8(dbz3::GA(0x82020F94) + 2 * uint32_t(it) + b));
      REX_STORE_U8(dbz3_esc_fb0 + 2 * c + b, REX_LOAD_U8(dbz3::GA(0x82020FB0) + 2 * uint32_t(it) + b));
    }
    return;
  }
  const uint32_t k = uint32_t(it - kAlta);
  const uint16_t r = uint16_t(dbz3::escenarios::kSerie + k);
  REX_STORE_U16(dbz3_esc_trans + 4 * c, r);
  const int v = g_altas[k].variante;   // 12b: sin `variante` en el stage.toml, el mismo registro
  REX_STORE_U16(dbz3_esc_trans + 4 * c + 2, uint16_t(v >= 0 ? v : r));
  const bool img = k < g_azt.size() && g_azt[k];   // sin imagenes, circulo y bola del "?"
  REX_STORE_U8(dbz3_esc_f94 + 2 * c, img ? 0 : REX_LOAD_U8(dbz3::GA(0x82020F94) + 24));
  REX_STORE_U8(dbz3_esc_f94 + 2 * c + 1, img ? 3 : REX_LOAD_U8(dbz3::GA(0x82020F94) + 25));
  for (uint32_t b = 0; b < 2; ++b)
    REX_STORE_U8(dbz3_esc_fb0 + 2 * c + b, g_spr0 ? uint8_t(g_spr0 + 2 * c + b) : REX_LOAD_U8(dbz3::GA(0x82020FB0) + 24 + b));
}

// Cada casilla con lo suyo (lo de siempre).
void ContenidoInicial(uint8_t* base) {
  for (uint32_t c = 0; c < kSelMax; ++c) PonerContenido(base, c, ContenidoPropio(c));
}

// Desplazamiento continuo: al moverse el cursor una casilla, la posicion logica avanza una y todas las casillas pasan a
// mostrar los escenarios alrededor de ella; solo cambian de verdad las dos del fondo del carrusel (fuera de la vista).
// Lo mueve solo el cursor del juego: en el online los dos PC van igual (lockstep) y al entrar se parte del ultimo escenario
// elegido (+30, que viaja en la foto del online).
std::vector<uint32_t> g_posc;   // casillas en el orden del carrusel, sin el "?"
std::vector<int> g_items;       // todos los escenarios: los del juego (en su orden) y luego todas las altas visibles
int g_L = 0, g_q = -1;
bool g_virt_listo = false;

void Recolocar(uint8_t* base, uint32_t d) {
  if (!Ext() || !d || !Virtual() || dbz3_esc_trans == 0x82373384) return;
  const uint32_t cur = REX_LOAD_U16(d + 604);
  if (!g_virt_listo) {
    const uint32_t n = REX_LOAD_U8(d + 609);
    if (n < 2) return;
    g_posc.clear();
    g_items.clear();
    for (uint32_t i = 0; i < n && i < kSelMax; ++i) {
      const uint32_t c = REX_LOAD_U16(d + kSelLista + 2 * i);
      if (c != 12 && c < kSelMax) g_posc.push_back(c);
    }
    for (uint32_t c : g_posc)
      if (c < 12) g_items.push_back(int(c));
    for (uint32_t k = 0; k < g_visibles; ++k) g_items.push_back(kAlta + int(k));
    if (g_items.size() <= g_posc.size()) return;
    int it = cur < kSelMax ? ContenidoPropio(cur) : -1;
    const int reg = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 30)) - dbz3::escenarios::kSerie;
    if (cur != 12 && reg >= 0 && uint32_t(reg) < g_visibles) it = kAlta + reg;   // vuelve al ultimo elegido
    const auto f = std::find(g_items.begin(), g_items.end(), it);
    g_L = f == g_items.end() ? 0 : int(f - g_items.begin());
    g_q = -1;
    g_virt_listo = true;
    g_cuantos = int(g_items.size());
    REX_STORE_U16(d + 606, 0xFFFF);   // sub_8219CC48 rehace la vista y el rotulo de la casilla del cursor
    REXLOG_INFO("dbz3 escenarios: selector continuo: {} escenarios en {} casillas", g_items.size(), g_posc.size());
  }
  if (cur == 12) return;   // en el "?" no se mueve nada
  const auto fq = std::find(g_posc.begin(), g_posc.end(), cur);
  if (fq == g_posc.end()) return;
  const int q = int(fq - g_posc.begin()), P = int(g_posc.size()), M = int(g_items.size());
  if (q == g_q) return;
  if (g_q >= 0) {
    int dq = q - g_q;
    if (dq > P / 2) dq -= P;
    if (dq < -((P - 1) / 2)) dq += P;
    g_L = ((g_L + dq) % M + M) % M;
  }
  g_q = q;
  for (int k = 0; k < P; ++k) {
    int dd = k - q;
    if (dd > P / 2) dd -= P;
    if (dd < -((P - 1) / 2)) dd += P;
    const int it = g_items[size_t(((g_L + dd) % M + M) % M)];
    if (g_cont[g_posc[size_t(k)]] != it) PonerContenido(base, g_posc[size_t(k)], it);
  }
  g_pos_sel = g_L;
}

void Tablas(uint8_t* base) {
  static bool hechas = false;
  if (hechas) return;
  hechas = true;
  const uint32_t f94 = Alloc(2 * kSelMax), fb0 = Alloc(2 * kSelMax), tr = Alloc(4 * kSelMax);
  for (uint32_t c = 0; c < kSelMax; ++c) {
    const uint32_t src = c < kCasillasSerie ? c : 12;   // de relleno: las del "?"
    REX_STORE_U8(f94 + 2 * c, REX_LOAD_U8(dbz3::GA(0x82020F94) + 2 * src));
    REX_STORE_U8(f94 + 2 * c + 1, REX_LOAD_U8(dbz3::GA(0x82020F94) + 2 * src + 1));
    REX_STORE_U8(fb0 + 2 * c, REX_LOAD_U8(dbz3::GA(0x82020FB0) + 2 * src));
    REX_STORE_U8(fb0 + 2 * c + 1, REX_LOAD_U8(dbz3::GA(0x82020FB0) + 2 * src + 1));
  }
  for (uint32_t c = 0; c < 12; ++c) REX_STORE_U32(tr + 4 * c, REX_LOAD_U32(dbz3::GA(0x82373384) + 4 * c));
  dbz3_esc_f94 = f94;
  dbz3_esc_fb0 = fb0;
  dbz3_esc_trans = tr;
  dbz3_esc_limite = 12 + Celdas();
  ContenidoInicial(base);
}

// Copia de un sprite del #ACA (hasta el siguiente de la tabla) con sus punteros internos rebasados y su
// bloque de textura [tex, 3, 1.0, 0, x0, y0, x1, y1] apuntando a `tex` con el rectangulo logico dado.
uint32_t CopiaSprite(uint8_t* base, uint32_t tabla, uint32_t n, uint32_t i, uint32_t tex, float w, float h,
                     uint32_t indice) {
  const uint32_t src = REX_LOAD_U32(tabla + 4 * i);
  uint32_t tam = 0x200;
  for (uint32_t j = 0; j < n; ++j) {
    const uint32_t q = REX_LOAD_U32(tabla + 4 * j);
    if (q > src && q - src < tam) tam = q - src;
  }
  const uint32_t dst = Alloc(tam);
  if (!dst) return 0;
  g_reservas.push_back(dst);
  uint32_t bloque = 0;
  for (uint32_t o = 0; o + 4 <= tam; o += 4) {
    uint32_t v = REX_LOAD_U32(src + o);
    if (v >= src && v < src + tam) v = v - src + dst;
    REX_STORE_U32(dst + o, v);
    if (!bloque && o >= 16 && o + 32 <= tam && REX_LOAD_U32(src + o + 4) == 3 &&
        REX_LOAD_U32(src + o + 8) == 0x3F800000u && REX_LOAD_U32(src + o) < 0x100)
      bloque = o;
  }
  if (!bloque) return 0;
  auto f = [](float x) {
    uint32_t u;
    std::memcpy(&u, &x, 4);
    return u;
  };
  REX_STORE_U32(dst + bloque, tex);
  REX_STORE_U32(dst + bloque + 16, 0);
  REX_STORE_U32(dst + bloque + 20, 0);
  REX_STORE_U32(dst + bloque + 24, f(w));
  REX_STORE_U32(dst + bloque + 28, f(h));
  REX_STORE_U32(dst + 0x14, indice);
  return dst;
}

// Pone el banco de texturas `banco` como lo hace el juego (sub_8219CBB8: globales del menu + sub_82092C58).
void PonerBanco(PPCContext& ctx, uint8_t* base, uint32_t banco) {
  REX_STORE_U32(dbz3::GA(0x8239592C), 0);
  REX_STORE_U32(dbz3::GA(0x82395928), 28672);
  REX_STORE_U32(dbz3::GA(0x82395930), banco);
  Llamar(ctx, base, DBZ3_CALL_EU(sub_82092C58), banco, 28672);
  REX_STORE_U32(dbz3::GA(0x82389430), 0);
}

// Nodos de la reserva de lecturas del juego que puede usar el selector (dejando kHuecoJuego para el juego).
uint32_t LecturasLibres(uint8_t* base) {
  if (dbz3::g_guest_eu) return 0xFFFFFFFFu;   // EU: sin limite (solo el -1 para)
  const int32_t libres = int32_t(LecturasTope(base)) - int32_t(LecturasUsadas(base)) - int32_t(kHuecoJuego);
  return libres > 0 ? uint32_t(libres) : 0;
}

// SLOTS_ESC-14: el selector cargaba el #AZT de TODAS las altas. Cada uno descomprimido ocupa ~2,4 MB de memoria fisica
// (destino del LZX, 0x4xxxxxxx: 0x48642220 -> 0x4BE85EA0 con 25 #AZT) y con 50-110 se agotaba: el hilo del descompresor
// escribia en 0x8070 (cierre, lr 0x82228F40). El monton del juego (0x8238A6F8) no es el que se agota (122 MB libres al
// caerse). Ahora solo se tienen los de las altas a kVentana posiciones o menos del cursor (en el orden de la rueda sin
// fin), como mucho kAztMax (40 probado con combate) y kEnVueloMax lecturas a la vez; los que se alejan se sueltan.
constexpr int kVentana = 16;
constexpr size_t kAztMax = 36, kEnVueloMax = 8;
// Distancia (en la rueda) entre el alta k y la posicion del cursor g_L. Sin rueda aun: las altas tras los del juego.
int DistAlta(size_t k) {
  const int M = g_items.size() > g_visibles ? int(g_items.size()) : int(g_visibles) + 12;
  const int pos = M - int(g_visibles) + int(k), L = g_items.empty() ? 0 : g_L;
  const int d = std::abs(pos - L) % M;
  return std::min(d, M - d);
}
bool CabeOtroAzt() {
  size_t en_vuelo = 0, tenidas = 0;
  for (size_t k = 0; k < g_cargas.size(); ++k) {
    if (Pendiente(g_cargas[k])) ++en_vuelo;
    if (Pendiente(g_cargas[k]) || (k < g_azt.size() && g_azt[k])) ++tenidas;
  }
  return en_vuelo < kEnVueloMax && tenidas < kAztMax;
}

// Pide los #AZT de la ventana aun sin pedir (los mas cercanos al cursor primero) mientras quepan. -1 (reserva llena) se
// queda "sin pedir" y se reintenta otro fotograma.
uint32_t PedirCargas(PPCContext& ctx, uint8_t* base) {
  uint32_t pedidas = 0;
  std::vector<std::pair<int, size_t>> orden;
  for (size_t k = 0; k < g_cargas.size() && k < g_altas.size(); ++k)
    if (!g_cargas[k] && DistAlta(k) <= kVentana) orden.emplace_back(DistAlta(k), k);
  std::sort(orden.begin(), orden.end());
  for (const auto& [dist, k] : orden) {
    if (!LecturasLibres(base) || !CabeOtroAzt()) break;
    const uint32_t h = Llamar(ctx, base, DBZ3_CALL_EU(sub_82083968), g_altas[k].menu, 0, 0);
    if (!Pendiente(h)) break;
    g_cargas[k] = h;
    ++pedidas;
  }
  return pedidas;
}

// Suelta los #AZT ya cargados que han salido de la ventana y que ninguna casilla muestra (se podran pedir otra vez).
void SoltarLejanas(PPCContext& ctx, uint8_t* base) {
  for (size_t k = 0; k < g_azt.size() && k < g_cargas.size(); ++k) {
    if (!g_azt[k] || DistAlta(k) <= kVentana + 2) continue;   // +2: sin soltar y pedir otra vez al ir y volver
    if (std::find(g_cont.begin(), g_cont.end(), kAlta + int(k)) != g_cont.end()) continue;
    if (REX_LOAD_U32(dbz3::GA(0x8238CE10)) == g_azt[k]) continue;   // banco global del juego ahora mismo
    Llamar(ctx, base, DBZ3_CALL_EU(sub_82089C10), g_azt[k]);
    if (std::getenv("DBZ3_SONDA_ESCENARIO")) REXLOG_INFO("dbz3 escenarios: azt {} soltado", k + 1);
    g_azt[k] = 0;
    g_cargas[k] = 0;
  }
}
// Si la lectura del #AZT del alta k ya llego: devuelve el nodo a la reserva y registra sus texturas.
bool Recoger(PPCContext& ctx, uint8_t* base, size_t k) {
  const uint32_t h = g_cargas[k];
  if (!Pendiente(h) || REX_LOAD_U16(h + 4) != 3 || k >= g_azt.size()) return false;
  const uint32_t datos = REX_LOAD_U32(h + 212);
  REX_STORE_U32(h + 212, 0);
  Llamar(ctx, base, DBZ3_CALL_EU(sub_82083D08), h, 0);
  g_cargas[k] = kCargaHecha;
  if (!datos || REX_LOAD_U8(datos + 1) != 'A' || REX_LOAD_U8(datos + 2) != 'Z' || REX_LOAD_U32(datos + 16) < 4) {
    REXLOG_WARN("dbz3 escenarios: el #AZT del selector de '{}' (entrada {}) no vale", g_altas[k].nombre,
                g_altas[k].menu);
    return false;
  }
  Llamar(ctx, base, DBZ3_CALL_EU(sub_820800A8), datos);   // crea y registra las texturas
  g_azt[k] = datos;
  return true;
}

// Cada fotograma del selector ya instalado: recoge las lecturas que han llegado y pide las que faltan.
void CargasTardias(PPCContext& ctx, uint8_t* base, uint32_t d) {
  const uint32_t cur = REX_LOAD_U16(d + 604);
  bool falta = false;
  for (size_t k = 0; k < g_cargas.size(); ++k) {
    if (g_cargas[k] != kCargaHecha) falta = true;
    if (!Recoger(ctx, base, k)) continue;
    if (std::getenv("DBZ3_SONDA_ESCENARIO")) REXLOG_INFO("dbz3 escenarios: azt {} cargado (cursor en {})", k + 1, g_L);
    for (uint32_t c = 0; c < kSelMax; ++c)
      if (g_cont[c] == kAlta + int(k)) {
        PonerContenido(base, c, g_cont[c]);
        if (c == cur) REX_STORE_U16(d + 606, 0xFFFF);   // sub_8219CC48 rehace la vista de la casilla del cursor
      }
  }
  if (dbz3::escenarios::g_mem && REXCVAR_GET(dbz3_esc_liberar)) SoltarLejanas(ctx, base);
  if (falta) PedirCargas(ctx, base);
}

}  // namespace

// sub_8219F350 (regenerado), con los recursos ya en D: texturas de las altas y sprites nuevos.
void dbz3_esc_instalar(PPCContext& ctx, uint8_t* base, uint32_t d) {
  if (!Ext() || !d) return;
  Tablas(base);
  const size_t na = g_visibles;
  g_azt.assign(na, 0);
  for (size_t k = 0; k < na && k < g_cargas.size(); ++k) Recoger(ctx, base, k);   // las demas: CargasTardias
  const uint32_t aca = REX_LOAD_U32(d + 0);
  if (!aca) return;
  // sprites: tabla nativa + 2 por alta (vista y rotulo, copias de los de la casilla 0, texturas 1 y 2)
  const uint32_t ns = REX_LOAD_U32(aca + 24), tn = REX_LOAD_U32(aca + 28);
  if (ns + 2 * kSelMax > 255) return;
  const uint32_t t = Alloc(4 * (ns + 2 * kSelMax));
  if (!t) return;
  g_reservas.push_back(t);
  for (uint32_t i = 0; i < ns; ++i) REX_STORE_U32(t + 4 * i, REX_LOAD_U32(tn + 4 * i));
  const uint32_t vista0 = REX_LOAD_U8(dbz3::GA(0x82020FB0)), rotulo0 = REX_LOAD_U8(dbz3::GA(0x82020FB1));
  // sprites por CASILLA (no por alta), 2 en cada una de las 24 (cualquiera puede llevar un alta con el desplazamiento): la
  // vista y el rotulo usan las texturas 1 y 2 del banco, que ConBanco pone con el #AZT del alta que lleva la casilla.
  // ponytail: los #AZT de TODAS las altas visibles quedan en memoria mientras dura el selector (~260 KB cada uno); con
  // muchas decenas, guardar solo los de las casillas cercanas al cursor (T5 de ESTADO_SLOTS_ESC-02).
  const bool alguna = std::any_of(g_azt.begin(), g_azt.end(), [](uint32_t z) { return z != 0; }) ||
                      std::any_of(g_cargas.begin(), g_cargas.end(), [](uint32_t h) { return h != kCargaHecha; });
  for (uint32_t c = 0; c < kSelMax; ++c) {
    const uint32_t sv_i = ns + 2 * c;
    const uint32_t sv = alguna ? CopiaSprite(base, tn, ns, vista0, 1, 440.0f, 320.0f, sv_i) : 0;
    const uint32_t sr = alguna ? CopiaSprite(base, tn, ns, rotulo0, 2, 192.0f, 32.0f, sv_i + 1) : 0;   // fila 0 del atlas
    REX_STORE_U32(t + 4 * sv_i, sv ? sv : REX_LOAD_U32(tn + 4 * vista0));
    REX_STORE_U32(t + 4 * (sv_i + 1), sr ? sr : REX_LOAD_U32(tn + 4 * rotulo0));
  }
  g_spr0 = ns;
  for (uint32_t c = 0; c < kSelMax; ++c) PonerContenido(base, c, g_cont[c]);   // ya con imagenes y sprites
  g_datos = d;
  g_tabla_nat = tn;
  REX_STORE_U32(aca + 28, t);
  REXLOG_INFO("dbz3 escenarios: selector con {} casillas nuevas, {} escenarios nuevos{} (sprites {}+)", Celdas(), na,
              Virtual() ? ", desplazamiento continuo" : "", ns);
  SondaMonton(base, "instalado");
}

// sub_8219E828 (regenerado): banco de la casilla nueva alrededor de la vista grande y el rotulo.
void dbz3_esc_banco(PPCContext& ctx, uint8_t* base, uint32_t d, bool nuevo) {
  if (!Ext() || !d) return;
  const uint32_t z = AztSel(base, REX_LOAD_U16(d + 604));
  if (z) PonerBanco(ctx, base, nuevo ? z : REX_LOAD_U32(d + 4));
}

// Cargador del selector: los #AZT de las altas se piden ANTES que los del juego (la cola de lectura va en
// orden, asi estan cuando sub_8219F350 ve el ultimo del juego).
DBZ3_HOOK_EU(sub_8219C7B0) {
  SondaMonton(base, "entrada");
  if (Ext()) {
    g_spr0 = 0;
    g_virt_listo = false;   // el desplazamiento se prepara en el primer fotograma con la lista hecha
    Tablas(base);
    ContenidoInicial(base);
    // Tabla y copias de sprite de la entrada ANTERIOR: no se liberan al salir (sub_8219E108) porque la transicion de
    // salida aun dibuja con ellas (con Y, cierre 0xC0000005 a los ~2 s: SLOTS_ESC-03); aqui ese selector ya no existe.
    if (REXCVAR_GET(dbz3_esc_liberar))
      for (uint32_t a : g_reservas) dbz3::escenarios::g_mem->SystemHeapFree(a);
    g_reservas.clear();
    g_cargas.assign(g_visibles, 0);
    const uint32_t usadas = LecturasUsadas(base), tope = LecturasTope(base);
    const uint32_t pedidas = PedirCargas(ctx, base);
    REXLOG_INFO("dbz3 escenarios: lecturas del juego al entrar al selector: {} usadas de {}; pido {} de {} #AZT ({} "
                "usadas despues)",
                usadas, tope, pedidas, g_visibles, LecturasUsadas(base));
  }
  orig(ctx, base);
}

// Fin del selector: el juego libera lo suyo (con su tabla de sprites); luego los #AZT de las altas.
DBZ3_HOOK_EU(sub_8219E108) {
  CortarVistaPreviaMusica();   // 12c: se sale del selector; se corta la vista previa musical
  CallarAdxMenu(ctx, base, false);   // SLOTS_ESC-08: la musica del menu vuelve en cualquier salida
  if (dbz3::escenarios::g_azar_pendiente) {
    dbz3::escenarios::g_azar_pendiente = false;
    dbz3::escenarios::g_azar_fase = 0;
  }
  if (Ext() && g_datos) {
    const uint32_t g = REX_LOAD_U32(kGlobalSel), tarea = g ? REX_LOAD_U32(g + 20) : 0;
    const uint32_t d = tarea ? REX_LOAD_U32(tarea + 48) : 0;
    if (d == g_datos)
      if (const uint32_t aca = REX_LOAD_U32(d + 0)) REX_STORE_U32(aca + 28, g_tabla_nat);
    g_datos = 0;
  }
  g_spr0 = 0;
  g_virt_listo = false;
  orig(ctx, base);
  if (Ext()) {
    for (uint32_t z : g_azt)
      if (z && REXCVAR_GET(dbz3_esc_liberar)) Llamar(ctx, base, DBZ3_CALL_EU(sub_82089C10), z);
    g_azt.clear();
    for (uint32_t h : g_cargas)   // pedidos que no llegaron a usarse
      if (Pendiente(h)) Llamar(ctx, base, DBZ3_CALL_EU(sub_82083D08), h, 0);
    g_cargas.clear();
  }
  SondaMonton(base, "salida");
}

// Lista de casillas: las del juego (bits 0..11 de la mascara), las altas y el "?" (bit 12) al final, que
// es lo que espera la eleccion al azar (toma una de las n-1 primeras).
DBZ3_HOOK_EU(sub_8219E268) {
  if (!Ext()) {
    orig(ctx, base);
    return;
  }
  Tablas(base);
  const uint32_t d = ctx.r3.u32, mascara = ctx.r4.u32 & 0xFFFF;
  REX_STORE_U16(d + 576, uint16_t(mascara));
  REX_STORE_U16(d + 574, uint16_t(mascara));
  for (uint32_t i = 0; i < kSelMax; ++i) REX_STORE_U16(d + kSelLista + 2 * i, 0xFFFF);
  uint32_t n = 0;
  auto poner = [&](uint32_t c) {   // (REX_STORE_U16 es una macro: nada de n++ dentro)
    if (n >= kSelMax) return;
    REX_STORE_U16(d + kSelLista + 2 * n, uint16_t(c));
    ++n;
  };
  for (uint32_t b = 0; b < 12; ++b)
    if (mascara >> b & 1) poner(b);
  for (uint32_t k = 0; k < Celdas(); ++k) poner(kCasillasSerie + k);
  if (mascara >> 12 & 1) poner(12);
  REX_STORE_U8(d + 609, uint8_t(n));
  REX_STORE_U32(d + 632, n ? 65536u / (n * 12) : 0);
  if (REX_LOAD_U8(d + 610) == 2) Llamar(ctx, base, DBZ3_CALL_EU(sub_8219CAE8), d);
}

// Circulo (sub_8219D6E8) y bola (sub_8219D520) de la casilla r4 y sprites de la casilla actual
// (sub_8219CC48): con el #AZT de la casilla nueva en D+4 durante la llamada.
void ConBanco(Dbz3GuestFn F, PPCContext& ctx, uint8_t* base, uint32_t c) {
  // 12b: la casilla actual (la que mira el jugador) puede mostrar el icono de su variante (Circle); las
  // demas del carrusel van siempre con el suyo.
  const bool actual = c == REX_LOAD_U16(ctx.r3.u32 + 604);
  const uint32_t d = ctx.r3.u32, z = actual ? AztSel(base, c) : AztDe(c), nat = REX_LOAD_U32(d + 4);
  if (z) REX_STORE_U32(d + 4, z);
  F(ctx, base);
  if (z) {
    REX_STORE_U32(d + 4, nat);
    // F deja de banco global (0x8238CE10) el ultimo que puso, aqui el del alta: el del juego, como lo dejaria
    // el juego. Los sprites de otras tareas piden sus texturas al banco global (sub_82092C80 -> sub_82096FF0:
    // la 37 al pasar a la casilla, medido 2026-10-15); en el #AZT del alta solo hay 4 (y su indice va relleno
    // hasta 64 por si acaso: tools/escenarios_convertir.py azt_menu)
    if (REX_LOAD_U32(dbz3::GA(0x8238CE10)) == z) PonerBanco(ctx, base, nat);
  }
}

DBZ3_HOOK_EU(sub_8219D520) {
  if (Ext())
    ConBanco(SEL(8219D520), ctx, base, ctx.r4.u32 & 0xFFFF);
  else
    orig(ctx, base);
}
DBZ3_HOOK_EU(sub_8219D6E8) {
  if (Ext())
    ConBanco(SEL(8219D6E8), ctx, base, ctx.r4.u32 & 0xFFFF);
  else
    orig(ctx, base);
}
DBZ3_HOOK_EU(sub_8219E540) {   // entrada r4 del carrusel
  if (Ext())
    ConBanco(SEL(8219E540), ctx, base, REX_LOAD_U16(ctx.r3.u32 + kSelCarrusel + 8 + 12 * (ctx.r4.u32 & 0xFFFF)));
  else
    orig(ctx, base);
}
DBZ3_HOOK_EU(sub_8219CC48) {
  if (Ext())
    ConBanco(SEL(8219CC48), ctx, base, REX_LOAD_U16(ctx.r3.u32 + 604));
  else
    orig(ctx, base);
}

#define DBZ3_SEL(fn)              \
  DBZ3_HOOK_EU(sub_##fn) {        \
    if (Ext())                    \
      SEL(fn)(ctx, base);         \
    else                          \
      orig(ctx, base);            \
  }
DBZ3_SEL(8219F4F8)
DBZ3_SEL(8219C870)
DBZ3_SEL(8219CAE8)
DBZ3_SEL(8219E6C0)
DBZ3_SEL(8219F2A8)
DBZ3_SEL(8219F350)
DBZ3_SEL(8219E828)
#undef DBZ3_SEL

// 12c: al final del fotograma del selector, vista previa de la musica del escenario que se mira (funciona
// tambien sin altas: la musica no depende de ellas).
DBZ3_HOOK_EU(sub_8219ECC8) {
  // La tarea del selector SOLO esta en r3 al ENTRAR (sub_8219ECC8 empieza con `lwz r31,48(r3)`); al volver,
  // la funcion deja en r3 el resultado de su ultima llamada (sub_8219E4A0/sub_8219C8D0) y `r3+48` es basura:
  // leerlo ahi hacia saltar el juego con 0xC0000005 al activar dbz3_selector_musica (studies 4, frente 15).
  const uint32_t tarea = ctx.r3.u32;
  if (std::getenv("DBZ3_SONDA_ESCENARIO")) SondaSel(base, tarea ? REX_LOAD_U32(tarea + 48) : 0);
  if (Ext())
    SEL(8219ECC8)(ctx, base);
  else
    orig(ctx, base);
  const uint32_t dtar = tarea ? REX_LOAD_U32(tarea + 48) : 0;
  if (REXCVAR_GET(dbz3_selector_musica)) VistaPreviaMusica(base, dtar);
  CallarAdxMenu(ctx, base, g_adx_quiero);   // SLOTS_ESC-08 (solo cambia algo con dbz3_selector_musica_solo_adx)
  ConfirmarAzarEnSelector(base, dtar);
  if (Ext() && dtar && dtar == g_datos) CargasTardias(ctx, base, dtar);
  EleccionMusical(base, dtar);
}
