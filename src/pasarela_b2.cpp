// dbz3 - Dragon World de Budokai 2 (port con sus graficos originales; ver docs/14_frentes/PORT_EXACTO.md).
// El tablero de la Pasarela: pasarela_misiones.cpp solo llama a Entrar/Dibujar/Resultado (siempre con su g_mu).
// Todo se lee en marcha de la ISO de B2 del jugador (DATA_CMN.AFS con AfsPs2); nada de B2 va en la version.
//
// Fase k (0..8): tablero #YDW 387+k, mapa cenital 346+5k (piezas de 256x256 en filas de <ancho de textura>/256;
// la fase I son 5x3, la IX 5x5), fichas 342+5k (128x128 en parejas retrato + dorso con la Z; retrato = 2 * indice
// de la pieza; el impar, el dorso con la Z = peana), adornos 345+5k (sprites ya pintados, sus recortes en el #AMA
// 344+5k), interfaz 398 (comun: tramos verdes de vida, cifras, bolas, !!, ?!, radar, nubes de aturdido...), GAME OVER
// 404 #0; en el idioma del juego (DATA_UK/SP/FR/IT/DU.AFS, mismas entradas): 146+k sprites y textos de la fase
// ("Stage N", objetivo, Saibaman que asoma del agujero) y 155 (Goku de cuerpo entero, 64x88, el de la intro). Comprobado 2026-10-16 (P2) con E:/DBZ3HD_trabajo/P2/ (scan*.py, png/): la cabecera del #YDW da el tamano
// de la textura del mapa y cuadra con el numero de piezas de cada 346+5k, y las fichas de 342 son los 13 companeros
// en el orden de las piezas del #YDW + Raditz (indice 13) y Nappa (14).
// Formato #YDW: +0x0C ancho y alto visibles del mapa, +0x14 ancho y alto de su textura, +0x20 8 pares (cuantos, donde):
//   T1 casillas de 52 B: x, y (float, px del mapa), 4 vecinas (u16, 0 = ninguna), +0x10 tipo (2 Dende cura, 3 alfombra de
//      Mr. Popo que lleva a la casilla +0x14, 4 sitio al que lleva), +0x19 condicion del combate en esa casilla (1 empiezan
//      con media vida, 2 con 0 de ki, 3 la vida se va gastando, 4 el ki se va gastando, 5 sin bloqueo), +0x1C 0x0A =
//      casilla de combate (boton con el kanji), +0x18 escenario de B2 del combate (0-12, kEscenarioB2; medido en PCSX2, R7),
//      +0x1F (17, 1, 4, 6, 20, 21, 7, 15, 22 por fase) NO elige escenario (sin uso conocido); la 0 nula. Comprobado con los 9 mapas de la guia
//      de PAndrews (GameFAQs, "Maps and Charts"): mismas casillas, caminos, objetos, Dende, Popo y condiciones.
//   T2 piezas de 40 B: +0 personaje (ID de B2: los 25 primeros = orden de B1), +1 indice de ficha, +2 bando (0 companero
//      elegible, 4 del jugador, bit 2 rival), +6 barras, +0xB barras maximas, +0xC vida, +0x14 casilla (u16).
//   T7 adornos: punteros a {objeto (registro del #AMA 344+5k), tipo (2 fijo, 0 animado, 4 capa de fondo), x, y}.
//   T8 objetos: punteros a {u16 casilla, u8 enterrado, u8 a la vista, u8 tipo, u8 valor, u16 ?, i32 cuantas banderas,
//      banderas (como las de T4)}. ELF 0x27CD40: lo no enterrado sale al suelo si esta "a la vista" o si se cumplen sus
//      banderas (las capsulas de las fases III, VI y VIII); 0x27C090: cada busqueda saca el primer enterrado de la casilla
//      que aun queda y cumple sus banderas (la fase IX apila 5 o 6 por casilla: uno por busqueda).
//      Tipos (comprobados con los mapas de la guia): 0 sitio posible de Bola (ELF 0x27BD80: el juego pone
//      min(fase, 6) - Bolas que tienes + 1 Bolas, como mucho 3, en sitios al azar, cada una con un numero de Bola que aun
//      no tienes; "a la vista" = sobre el suelo, como la de la fase I), 1 daga (+valor % de ataque), 3 armadura (+valor % de defensa), 5 cebolla (mueve
//      2 casillas por turno), 7 capsula al azar, 8 zenie (valor x 100), 9 Radar del Dragon (a la vista), 6 Mr. Popo.
// #AMA (sprites): +8 cuantos, +0xC donde; registros de 0x40: rect x0,y0 (+0) x1,y1 (+0x10) respecto al ancla, +0x38/+0x3C
//   cuadros (0x30 B: +4 textura, +0x20 u0 v0 u1 v1 en px, inclusivos).
// Reglas (FAQ de GameFAQs, E:/DBZ3HD_trabajo/estudio_ports/b2_reglas.md): en tu turno cada personaje mueve 1 casilla
// (la cebolla da +1), buscar gasta el movimiento (una vez por casilla), tocar a un rival = combate 1 contra 1 (el que se
// movio contra el rival), ganar le quita una barra, la vida se conserva entre combates, Saibaman y Dr. Gero no se mueven.
#include "reloj_prueba.h"
#include "escenarios.h"
#include "pasarela.h"
#include "ps2_modelo.h"

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <array>
#include <atomic>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"
#include "launcher/settings.h"
#include "mods_opcionales.h"
#include "video_sfd.h"

namespace dbz3::rpg {
void Premio(int zeni);   // Zenie al monedero del HUB (modos.h)
}
namespace dbz3::pasarela::nave {
int CapsulaDeB2(int id_b2);   // pasarela_nave.cpp
// Orden 70 (studies 8, E): la misma ventana "Get skill!" que la nave (FUN_002a44c0 + FUN_002a4410)
int CapsulaAlAzar(int grupo, int personaje_b2);   // FUN_002835F0: 0 = ninguna
bool CapsulaNueva(int id_b2);
void GanarCapsula(int id_b2);
bool DibujarFiguraB2(ImDrawList* dl, int id_b2, ImVec2 pie, float alto_px, float giro, float t, int traje = 0, int pbank = 0, int panim = 0, const float* cb = nullptr);   // A2c: el luchador de B2 con su pose (FigDe)
void DesbloquearEvento(int ev);   // pasarela_nave.cpp: sucesos de desbloqueo de b2.c (0x1CD..0x1E9 luchador, 0x1FE..0x206 escenario)
bool VentanaPremio(ImDrawList* dl, const dbz3::ingame::style::Area& a, int id_b2, float cuadros, uint16_t pressed, bool auto_on);
const std::vector<uint8_t>& ElfB2();   // S1-F01.2: el SLES/SLUS de la ISO del jugador (leido una vez)
}
namespace dbz3::pasarela::b2 {
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kLB = 0x0100,
                   kRB = 0x0200, kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
constexpr int kFases = 9;
constexpr int kGoku = 0, kSaibaman = 22, kGero = 25;   // IDs de B2

struct Texto {
  const char *es, *en;
  const char* operator()() const { return T::T(es, en); }
};
const Texto kSagas[kFases] = {{"Fase I: saga Saiyan", "Stage I: Saiyan saga"},
                              {"Fase II: Freezer en la Tierra", "Stage II: Frieza on Earth"},
                              {"Fase III: Namek", "Stage III: Namek"},
                              {"Fase IV: los androides y Cell", "Stage IV: the androids and Cell"},
                              {"Fase V: la resurrección de Bu", "Stage V: Buu's revival"},
                              {"Fase VI: Babidi y Majin Bu", "Stage VI: Babidi and Majin Buu"},
                              {"Fase VII: la sala del tiempo", "Stage VII: the time chamber"},
                              {"Fase VIII: la ciudad", "Stage VIII: the city"},
                              {"Fase IX: Kid Bu", "Stage IX: Kid Buu"}};
// Escenario de B3 (entrada de data_cmn de su mapa) para los combates de cada fase.
const int kMapaB3[kFases] = {64, 62, 60, 50, 64, 68, 58, 68, 54};

// ID de B3 de un personaje de B2: los 25 primeros siguen el orden de B1; los nuevos, por sus fichas (342+5k).
int B2aB3(int c) {
  switch (c) {
    case 25: return 32;   // Dr. Gero
    case 26: return 6;    // Goten
    case 27: return 9;    // Trunks nino
    case 28: return 4;    // Gohan
    case 29: return 34;   // Majin Bu
    case 30: return 35;   // Super Bu
    case 31: return 36;   // Kid Bu
    case 32: return 37;   // Dabura
    case 33: return 15;   // Videl
    case 35: return 16;   // Kaio-shin
    case 36: return 64;   // Gotenks (B3 64, forma 3)
    case 38: return 72;   // Vegito (B3 72, forma 2)
    case 40: return 16;   // Kibitoshin (cabeza 40 de CMN 1196, fase V p19) = Kaio-shin de B3 forma 1 (ver FormaTableroB3Impl)
    case 47: case 48: case 50: case 51: return 35;   // ponytail: formas de Super Bu de las fases VII-VIII, sin separar
  }
  return c >= 0 && c < 25 ? B1aB3(c) : -1;
}

// Forma inicial de B3 para una pieza del tablero. El T2 de B2 trae forma=0 en las 9 fases; sin una orden 52,
// Cell sale siempre perfecto (B3 2) y Freezer siempre en su forma final (B3 3), como en el tablero original.
// Si la orden 52 trae una forma explicita, se traduce con la tabla de B1/B3 (Freezer 2 = al 100 %, B3 4).
int FormaTableroB3Impl(int id_b3, int forma_b2) {
  if (id_b3 == 33) {   // Cell
    if (forma_b2 >= 0) return forma_b2 >= 2 ? 2 : forma_b2 == 1 ? 1 : 0;
    return 2;          // perfecto
  }
  if (id_b3 == 27) {   // Freezer
    if (forma_b2 >= 0) return forma_b2 == 2 ? 4 : forma_b2 >= 1 ? 3 : 0;
    return 3;          // forma final
  }
  if (id_b3 == 16 && forma_b2 == 1000) return 1;   // Kibitoshin (c40 de B2)
  if (id_b3 == 64) return 3;   // Gotenks (B3 64)
  if (id_b3 == 72) return 2;   // Vegito (B3 72)
  return forma_b2 > 0 ? FormaB3(id_b3, forma_b2) : 0;
}

struct Casilla {
  float x = 0, y = 0;
  int vec[4] = {};
  int tipo = 0, destino = 0, condicion = 0;   // ver T1 arriba
  int escenario = 0;                          // +0x18: escenario de B2 del combate (0-12)
  int musica = -1;                            // +0x1F: pista del combate de la casilla (M4): 17, 1, 4, 6, 20, 21,
                                              // 7, 15, 22 por fase I-IX; indice de la lista de PistaCombateB2
  bool lucha = false;
};
// Escenario de B2 (T1+0x18 -> 0x699391 -> FUN_001b2790) -> grupo de DATA_CMN que carga (tabla del ELF 0x372AB0 + 0x9C * i:
// +0 #MDD 8g+7, +4 #MAD, +8 #MAS; 9-12 = 0, 1, 4, 5 en su variante 1) -> carpeta del alta que monta mod center hd/campos_iw.py
// (B12) desde la ISO. Sin carpeta (grupos 1 y 5 no se instalan, o no esta), el escenario de B3 de la fase (kMapaB3).
const char* const kEscenarioB2[13] = {"Llanura B2", "Calle de la ciudad B2", nullptr /* g5 */, "Torneo B2" /* g1, entrada 5001 de B12 */,
                                      "Base Red Ribbon B2", "Montanas nevadas", "Templo de Kami", "Islas B2", "Namek B2",
                                      "Llanura B2", "Calle de la ciudad B2", "Base Red Ribbon B2", "Montanas nevadas"};
struct Objeto {
  int casilla = 0, tipo = 0, valor = 0;
  bool visible = false, enterrado = false, hecho = false;
  std::vector<int> banderas;   // las que pide (T8 +8); vacia = ninguna
};
struct Ficha {
  int c = 0, idx = 0, bando = 0, barras = 1, barras_max = 1, vida_max = 3000, casilla = 0;
  int vida = 0;                   // la que le queda (0 = llena)
  int movs = 0, movs_turno = 1;
  bool fuera = false, jugador = false, rival = false, oculta = false;
  int slot = 0, slot_ant = 0;     // hueco en la casilla (+0xC del original): 0 centro, 1 desplazado (FUN_00273950)
  int aturdido = 0;               // turnos rivales que no se mueve (nube con remolino); vencerlo asi quita 2 barras
  int ataque = 0, defensa = 0;    // % de la daga y la armadura recogidas (valen hasta acabar la fase). Son los campos
                                  // +0x10 / +0x12 del registro de la pieza (0x69B7A0 + 0x24 * n): la daga y la armadura
                                  // los suben (FUN_0027A980 casos 1/3 -> FUN_00276E10/00276DE0, b2.c:224592-224601) y
                                  // tambien las ordenes 35/36 del guion tras un combate ganado (studies 8, E).
  int ki_b2 = -1;                 // orden 53: +0x16 del registro (ki con el que empieza su combate; -1 = sin tocar)
  int vida_b2 = -1;               // orden 54: +0x14 del registro (vida con la que empieza; -1 = sin tocar)
  // studies 2 G04 (ordenes 51/52 del guion; b2.c): campos del registro de la pieza que el combate de B2 copia a su
  // bloque. Se guardan como indices de B2 sin traducir (el mapeo a B3 no esta medido: [H] en INFORME.md).
  int forma_b2 = -1;              // orden 52: +0x19 (byte; a1 del guion)
  int traje_b2 = -1;              // orden 52: +0x1A (word; a0 del guion)
  int com_b2 = -1;                // orden 51: +0x18 (byte; va al COM del rival, 0x699365)
  // studies 2 G04 (ordenes 65/66, FUN_002774F0): la pieza va a un punto (x, y) del mapa a 4 px/cuadro sin tocar su
  // casilla logica. 0 = quieta, 1 = andando, 2 = llego (se queda en el punto hasta otra orden de colocacion).
  int punto = 0, punto_orden = 0;
  float punto_x = 0, punto_y = 0, punto_x0 = 0, punto_y0 = 0, punto_t0 = 0, punto_dur = 0;
  int n = 0;                      // indice de la pieza en el #YDW (el que usan los scripts)
  int mov = -1, mov_p = 0;        // tipo de movimiento (T2 +3/+4, o el que pone la orden 50) y su dato
  int modo = 0;                   // Saibaman: fila de sprites del #AMA (T2 +0x07 -> +0x1E; FUN_002791a0 b2.c:223478)
  int desde = 0;
  float t_mov = -10;              // animacion del ultimo paso
  // R7 (ELF 0x275EB0, 0x274720, 0x273630): la pieza gira hacia la casilla 0x800/0x10000 por cuadro (11,25 grados), anda
  // en linea recta a 4 px del mapa por cuadro y, al llegar, vuelve a mirar al frente (angulo 0) al mismo ritmo. La del
  // jugador se pone mirando sin girar (0x277DD0). giro0 -> giro1 = el angulo al empezar y el del paso (radianes).
  float giro0 = 0, giro1 = 0, t_giro = 0, t_dur = 0.35f;
  // H02 (ordenes 61/62, FUN_00277630/FUN_002775f0): angulo +0x24 del registro visual, en 1/65536 de vuelta. Se gira a
  // 0x800 por cuadro (FUN_00274670) y se mantiene; 62 espera a llegar (tabla 0x37A020) y 61 no.
  bool giro_obj_activo = false;
  float giro_obj0 = 0, giro_obj1 = 0, giro_obj_t0 = -10, giro_obj_dur = 0;
  // H02 (ordenes 58-60, FUN_00277700/FUN_00277740/FUN_00277780): estado 0xF/0x10/0x11 y clip +0x1C del banco #AMM 334.
  // L01: el clip se carga bajo demanda y lo pinta DibFicha3D (antes se guardaba pero no se dibujaba).
  int anim_b2 = -1, anim_estado = 0;
  float anim_t0 = -10;
  int acciones = 0;   // rival: acciones que le quedan en la fase enemiga (+2 del registro; 0x278EE0 las recarga a 1)
  int estado0 = 0;                // +5 del registro: 0 en juego, 1 sembrado (sale con su efecto), 2 y 6 escondido hasta su guion
  float brote_t = -10;            // cuando aparecio (brota del suelo o se materializa)
  // columna de vida (FUN_00274210, b2.c:220302): +0x78 barras que se ven (van hacia `barras` de una en una, al acabar la
  // anim 0x36 ganar / 0x37 perder del #AMA 397) y +0x7C su temporizador (aqui, cuando empezo; < 0 = sin empezar)
  mutable int barras_vis = -1;
  mutable float vida_t0 = -1;
};
struct Datos {
  int fase = -1;
  float ancho = 0, alto = 0;
  int cols = 1;
  std::vector<Casilla> casillas;  // [0] nula
  std::vector<Objeto> objetos;
  std::vector<Ficha> piezas;
  // Scripts de la fase (#YDW T3 y T6, interprete del ELF 0x295F20): T3 = disparadores {pieza, otra, turno, script, ?}
  // (-1, 0, N: al empezar el turno N); T6 = scripts: u32 cuantas + ordenes de 24 B {espera, op, 4 datos}.
  std::vector<std::vector<int>> t3;   // {A, B, C, guion, N banderas, banderas...} (FUN_0027F9A0 / 0027F7E0)
  // T4 (ELF 0x27F510): contactos {pieza A (0 = cualquiera), pieza B, casilla (0 = cualquiera), script, ?, ?, cuantas banderas,
  // banderas...}: vale si los personajes de A y B son los de las dos fichas que se tocan; numero > 0 = encendida,
  // < 0 = apagada (bitfield del ELF 0x699260). T5 (0x27F3A0): {fila de T4 del ultimo contacto, suceso (5 = cualquiera),
  // script, cuantas banderas, banderas...}: lo que pasa tras ese combate.
  std::vector<std::vector<int>> t4, t5;
  // s9b: banderas de la fase (ELF 0x234930, b2.c:188418): al empezar la fase de cero, las banderas 0..cuantas-1 se ponen
  // al valor de la tabla de bytes del #YDW (+0xB0); las demas (hasta 960) siguen de las fases anteriores.
  std::vector<uint8_t> band_ini;
  std::vector<std::vector<std::array<int, 6>>> guiones;
  std::vector<Imagen> mapa, fichas, iconos, adornos, loc;
  std::vector<Imagen> shen_frases, shen_marco;   // Shenron: frases (DATA_<idioma> 537) y ventana (CMN 850)
  std::vector<Imagen> deseos;   // deseos de Shenron (DATA_<idioma> 540, 539, 538: los carga dw_shenglong.c, ELF 0x37A600)
  Imagen game_over, goku, hoyo, cabezas;   // Goku de cuerpo entero (155), agujero y cabezas de Saibaman (146 #13/#16)
  struct Sprite {
    int tex = -1;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float px = 0, py = 0;   // sitio de reposo (ultimo fotograma de la lista A del #AMA, pantalla de 640x448)
    float ex = 1, ey = 1;   // su escala (la caja del menu va a 0,95 x 0,8)
    std::vector<std::array<float, 7>> claves;   // lista A: {cuadro, x, y, alfa, escala x, escala y, giro en grados}
    float dur = 0;                              // cuadros que dura (FUN_00245C00: max(+0x20, +0x24))
    struct Recorte { float cuadro; int tex; float u0, v0, u1, v1; };
    std::vector<Recorte> recortes;   // lista B; usada solo por las piezas nativas del encargo 01
  };
  std::vector<Sprite> sprites;             // #AMA 344+5k: primer cuadro de cada objeto
  std::vector<Sprite> textos;              // #AMA 343+5k: ventanas de mensaje de la fase (texturas de 146+k)
  std::vector<Sprite> shen_ama;            // Shenron: disposicion de su ventana (CMN 849)
  // Marcador y ventanas del tablero (ELF 0x2350C0: fijos CMN 402 = #AMA y DATA_<idioma> 188 = sus texturas, los
  // uRam00698dc8/db0 de las tareas D_TURN, D_RADAR, D_RADARBASE, D_CLEAR, D_DBGET, DBALL y del menu DWU_MMAIN/DWU_YN*):
  // 0-2 barra de madera, 3-5 "MUNDO DRAGON", 6-15 Bolas, 16-24 radar, 38-50 rombo del turno y cifras, 51-74 ventana de
  // mensaje (8 piezas: entrar 0x33+3k, reposo +1, salir +2), 87-89 flecha, punto y brillo del radar, 97-120 menu y Si/No.
  std::vector<Imagen> hud;
  std::vector<Sprite> hud_ama;
  std::vector<Sprite> life_ama;   // CMN 397 (uRam00698dcc): 0x34 celda vacia, 0x35 llena, 0x36 ganar (81), 0x37 perder (41)
  std::vector<Imagen> aliados;
  std::vector<Sprite> aliados_ama;   // B2 EU: tabla 0x3A4010 = DATA_<idioma> 184 + CMN 399 (NO 402)
  // s9: tarjeta "Battle!" que sale tras el contacto: DATA_<idioma> 190 (12 texturas: pergamino, titulo...) + 189 (los 6 textos
  // de condicion, uno por T1+0x19) y la disposicion en CMN 408 (7 = pergamino, 6 = titulo; CMN 410 = pose del texto)
  std::vector<Imagen> tarjeta, tarjeta_cond;
  std::vector<Sprite> tarjeta_ama;
  struct Adorno {
    int obj = 0, tipo = 0;
    float x = 0, y = 0;
  };
  std::vector<Adorno> colocados;           // T7 (todos los tipos; 4 = capa de fondo)
  int stage = -1, objetivo = -1;           // en loc: "Stage N" (240x72) y el objetivo (primer 560x128)
  int musica0 = -1;                        // primera orden 71 de los guiones (la del guion de inicio, 301)
};

enum Estado : int { kPrepara, kJuego, kGanada, kPerdida, kDeseo };

Datos g_d;
int g_estado = kPrepara, g_comp = 0, g_sel = 0, g_dest = -1, g_turno = 1, g_bolas = 0, g_zenie = 0, g_jefe = -1;
std::vector<std::array<int, 2>> g_bolas_fase;   // Bolas puestas en esta fase: {casilla, numero de Bola 0..6}
int g_bolas_ini = 0;                           // las que tenias al empezar la fase
std::vector<Ficha> g_f;
std::vector<Objeto> g_obj;
std::vector<bool> g_buscada;
std::vector<int> g_cand;            // piezas elegibles como companero
// Orden 69 {cuantos} (ELF 0x2938A0 -> 0x2A3DE0: tarea 0x2A2550 con el contador 0x8F2C = a0): el guion de inicio (301) abre
// la eleccion de aliados a mitad de la presentacion; cuantos = 1, y 2 en las fases IV, V y IX (medido en los 9 #YDW,
// igual que la FAQ). El guion espera hasta que se eligen todos.
int g_elegir = 0;
// A2 (studies 5), medido en PCSX2 (fases II-IX, savestates A2\sstates): la lista de aliados (FUN_002A1E30) NO saca al elegido: X lo
// marca (la casilla pasa de negra a naranja y el cursor se queda), Triangulo desmarca la fila del cursor marcada, y al marcar el
// ultimo sale la ventana "Is this okay?" (FUN_002A1640: "This is okay." / "No, wait a minute.", cursor en el No por defecto, arriba
// y abajo lo cambian). Solo "This is okay." (X) cierra la eleccion; el No (o Triangulo) desmarca el ultimo. g_elegir = aliados que
// hay que dejar marcados; quedan = g_elegir - g_marcadas.size().
std::vector<int> g_marcadas;   // indices de g_f marcados, en orden
int g_conf = -1;               // -1 sin ventana; 0 "This is okay." 1 "No, wait a minute."
float g_conf_t = 0;            // segundos desde que se abrio la ventana
// Orden 41 {fase} (0x294660: guarda a0 en la fase de la partida, 0x693A96) = fase superada, la siguiente es a0; orden 42
// (0x294510) = final del Mundo Dragon (fase IX); orden 43 (0x2944A0) = derrota (p. ej. fase I: Raditz o Nappa en la Bola).
int g_fase_sig = -1;
std::string g_msg;
int g_msg_icono = -1;
float g_msg_t = -10, g_ahora = 0;
// Ventanas de mensaje de los scripts (orden 38 {a, b}: las animaciones a y b del #AMA 343+5k con las texturas de
// 146+k, como la ventana del ELF 0x26F280/0x26EFC0). Las que reposan fuera de la pantalla son las de salida: no se
// ponen. Se quitan con A o a los 3 s (ponytail: el original espera a su ventana; el tiempo es medido a ojo).
std::vector<std::array<int, 2>> g_mensajes;
// studies 8 (E), ordenes de guion que bloquean y no estaban portadas (RE en b2.c, ELF SLES-51839):
// - Orden 70 {grupo, pieza} (FUN_00293740, b2.c:238078): FUN_00278DE0 da el personaje de la pieza; FUN_002835F0 elige al
//   azar una capsula del #SKA (DATA_CMN 851) con +0x14 == grupo, usable por ese personaje (mascara +0 == 0 o con su bit)
//   y, si puede, de la que aun no tengas el maximo (+0x0D); 0 = ninguna y la orden acaba. Si hay, abre la ventana "Get
//   skill!" (FUN_002a44c0 + FUN_002a4410(nueva)) y espera a que se cierre (FUN_002a42b0 == -1); entonces suma 1 al
//   inventario si no esta al maximo. 403 usos: el premio de capsula tras los combates del tablero.
int g_premio_id = 0;
float g_premio_t = -10;
// - Orden 39 {0} (0x2947B0, sin funcion en Ghidra; desensamblada): la primera vez marca +0x34 = 1; despues espera a que
//   el mando del jugador (0x3B43AE + 0xB8 * puerto) traiga el bit 0x4000 (la X de PS2 = A) y acaba. Sin dibujo propio.
bool g_espera_x = false;
// - Ordenes 83/84 {pieza, signo, valor} (FUN_00292EA0 / FUN_00292C00, b2.c:237706-237851): el "攻 +N" / "防 +N" sobre la
//   pieza tras ganar un combate (van con las 35/36 que suben su ataque/defensa). Lanzan la tarea de la cifra
//   (FUN_0027BD00 -> FUN_0027BAB0: pose del #AMA 397 anim 0x4D; imagenes 0x5A a +48, unidades a +24, decenas a 0,
//   centenas a -24 y el signo 0x58 "+" / 0x59 "-" a -48) y dibujan las 4 animaciones de la tabla 0x37A080 (83: 116-119,
//   el kanji 攻) o 0x37A090 (84: 120-123, 防) hasta que las 4 llegan a su ultimo cuadro; el sonido canal 3 id 0x0C (83) /
//   0x12 (84) a los 110 cuadros. Con signo 0: sin las 4 animaciones, sonido 0x0D al empezar y 120 cuadros.
struct Subida {
  int n = 0, signo = 1, valor = 0;
  bool defensa = false, sonado = false;
  float t0 = 0;
};
std::vector<Subida> g_subidas;
// Banderas de la fase (ELF 0x699260; ordenes 1 encender, 2 apagar, 3 cambiar) que piden los disparadores T4/T5.
std::vector<uint32_t> g_banderas(64, 0);
// Se guardan con la fase superada (orden 41) y se recuperan al empezar la siguiente; el reinicio y la orden 42 las borran.
void GuardarBanderas(bool borrar) {
  for (size_t i = 0; i < g_banderas.size(); ++i)
    GuardarProgreso("tablero_b2_band" + std::to_string(i), borrar ? 0 : int(g_banderas[i]));
}
void LeerBanderas() {
  for (size_t i = 0; i < g_banderas.size(); ++i)
    g_banderas[i] = uint32_t(LeerProgreso("tablero_b2_band" + std::to_string(i), 0));
}
bool Bandera(int n) { return n >= 0 && n < 2048 && (g_banderas[size_t(n) >> 5] >> (n & 31) & 1u); }
bool Condiciones(const std::vector<int>& r, size_t desde) {   // todas las banderas de r[desde..] como se piden
  for (size_t k = desde; k < r.size(); ++k)
    if (r[k] > 0 ? !Bandera(r[k]) : Bandera(-r[k])) return false;
  return true;
}
// Objeto sobre el suelo (ELF 0x27CD40) o enterrado que aun puede salir al buscar (0x27C090).
bool EnSuelo(const Objeto& o) { return !o.hecho && !o.enterrado && (o.visible || Condiciones(o.banderas, 0)); }
bool Escondido(const Objeto& o) { return !o.hecho && o.enterrado && Condiciones(o.banderas, 0); }
int BolaEn(int casilla) {
  for (size_t i = 0; i < g_bolas_fase.size(); ++i)
    if (g_bolas_fase[i][0] == casilla) return int(i);
  return -1;
}
float g_mensaje_t = -10;
int g_pend_j = -1, g_pend_r = -1;   // combate pendiente (fichas de g_f)
int g_com_rival_b2 = -1;   // orden 51: +0x18 del rival (0x699365); -1 = sin orden (ver ComRivalB2)
int g_pend_mov = -1;   // la ficha que se movio al contacto (uGpffff88d4: la camara la sigue, FUN_00235780/0x236420)
float g_pend_t = 0;
bool g_luchando = false;
// Radar del Dragon recogido (bit 1 de 0x6992DC: D_RADAR solo dibuja con el), marcador de abajo escondido con L1 (bit 8,
// 0x2700E0) y menu del tablero (triangulo/Start, ELF 0x2A35F0: "Salir" y "Reiniciar Mundo Dragon", cada uno con su Si/No
// de 0x2A2C90; 0 cerrado, 1 menu, 2 Si/No).
bool g_radar = false, g_hud_oculto = false;
bool g_shen_no = false;   // el deseo pedido no se puede cumplir en B3 (Shenron dice su frase 537 #3)
// Cuando entro o salio cada parte del marcador (entradas y salidas animadas de DibHud) y cuando se dibujo por ultima vez
float g_hud_t = -10, g_radar_t = -10, g_rombo_t = -10, g_hud_visto = -10;
bool g_radar_visto = false;
float g_bola_t = -10;   // D_DBGET: la Bola recien cogida (g_bola_n, 0..6) entra en la barra
int g_bola_n = -1;
int g_menu = 0, g_menu_sel = 0, g_yn_sel = 1;
float g_menu_t = 0;
int g_marca = -1, g_marca_icono = 0;   // "?!" o "!!" sobre una casilla
float g_marca_t = -10;
float g_cam_x = 0, g_cam_y = 0, g_auto_t = 0;
bool g_cam_ini = false;   // la camara del original no se reinicia al cargar la fase: la primera vez vale (0,0)
std::string g_iso_b2, g_loc_afs;   // de Cargar: para leer escenas en marcha
// Musica del tablero (R7, ELF 0x2460D0/0x246190): la pista es la variable 0x6992DB que pone la orden 71 de los guiones
// (0x293700), no la fase; indice -> tabla .data 0x377E90 (u32 0x20000 | entrada de ADX_US.AFS). Medido en el ELF y en
// los 9 #YDW: fases I-IX empiezan con 0, 2, 1, 3, 4 (luego 7), 7, 6, 5, 8.
int g_musica = -1;
int PistaB2(int i) {
  // las 19 entradas de la tabla (0x377E90; la 20.a es 0)
  static const int kTabla[19] = {265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 262, 1158, 1159, 1155, 1133,
                                 1160, 1134, 1156, 1148};
  return i >= 0 && i < 19 ? kTabla[i] : -1;
}

// Audio de eventos del tablero. FUN_002350C0 carga los bancos DATA 185/186/187 (0x10000|185.. en 0x6993A0/A4/A8,
// b2.c:188732-188742) junto al HUD 397/398; los efectos del tablero se piden por el canal 3 con FUN_00147260(3,0,id,0)
// y cada uno corta al anterior. [H] el enlace canal 3 -> banco 185 necesita la captura en PCSX2 (runbook en VERIFICACION.md).
void SonidoEventoB2(const char* evento, int id) {
  if (g_iso_b2.empty() || id < 0 || id >= 22) return;
  const bool prueba = std::getenv("DBZ3_TEST_MUSICA") != nullptr;
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.7f;
  REXLOG_INFO("dbz3 tablero b2: audio evento {}: IECS cuerpo 185 id {} tipo 5 volumen {:.2f} solo_decodificar {}",
              evento, id, vol, prueba);
  OirSonidoPs2(g_iso_b2, g_loc_afs, 185, id, 5, prueba ? 1.0f : vol, 0, prueba);   // tipo 5 = eventos (no corta musica)
}
// Canal 3 del tablero (FUN_00147260(3, 0, id, 0)): el banco general DATA_<idioma> 142 (cuerpo) + 143 (IECS, 26 sonidos),
// el mismo que el port ya usa para la orden 75 (tipo 3; R7 re_escenario_sonidos). Lo usan las ordenes 83/84.
void SonidoCanal3(int id) {
  if (g_iso_b2.empty()) return;
  const bool prueba = std::getenv("DBZ3_TEST_MUSICA") != nullptr;
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.7f;
  REXLOG_INFO("dbz3 tablero b2: audio canal 3: cuerpo 142 id {}", id);
  OirSonidoPs2(g_iso_b2, g_loc_afs, 142, id, 3, prueba ? 1.0f : vol, 0, prueba);
}
// Cruces de los cuadros de pisada del original (FUN_00275EB0: Goku id 5 en los cuadros 5 y 23; el resto id 6 en el 13).
// [H] la repeticion del clip en caminos largos (>24 cuadros) puede pedir mas cruces; captura pendiente.
void SonidosPasosB2(float anterior, float ahora) {
  if (anterior < 0 || ahora < anterior) return;
  for (const Ficha& f : g_f) {
    if (f.fuera || f.oculta || f.desde == f.casilla) continue;
    const float inicio = f.t_mov + f.t_giro;
    auto cruce = [&](float cuadro, int id) {
      const float cuando = inicio + cuadro / 60.0f;
      if (cuadro / 60.0f < f.t_dur && anterior < cuando && cuando <= ahora) SonidoEventoB2("paso", id);
    };
    if (f.c == kGoku) {
      cruce(5, 5);
      cruce(23, 5);
    } else {
      cruce(13, 6);
    }
  }
}

void DarCapsulaB3(int c);   // abajo (inventario de capsulas de la partida)
void VarianteGoku();        // orden 82 (abajo): Goku del tablero con CMN 339/340
void Aviso(const std::string& m, int icono = -1) {
  g_msg = m;
  g_msg_icono = icono;
  g_msg_t = g_ahora;
  REXLOG_INFO("dbz3 tablero b2: {}", m);
}
std::string Nombre(const Ficha& f) {
  const int id = B2aB3(f.c);
  return id >= 0 ? NombreDe(id) : "#" + std::to_string(f.c);
}

// ---- Lectura de la ISO ----
bool Cargar(int fase) {
  if (g_d.fase == fase) return true;
  const auto iso = BuscarJuego("budokai 2");
  AfsPs2 cmn;
  if (iso.empty() || !cmn.Abrir(iso, "DATA_CMN.AFS")) {
    REXLOG_WARN("dbz3 tablero b2: sin ISO de Budokai 2 ({})", iso.string());
    return false;
  }
  const std::vector<uint8_t> y = cmn.Entrada(387 + size_t(fase));
  auto u32 = [&](size_t o) -> uint32_t {
    return o + 4 <= y.size() ? y[o] | y[o + 1] << 8 | y[o + 2] << 16 | uint32_t(y[o + 3]) << 24 : 0;
  };
  auto u16 = [&](size_t o) -> int { return o + 2 <= y.size() ? y[o] | y[o + 1] << 8 : 0; };
  auto f32 = [&](size_t o) {
    float v = 0;
    if (o + 4 <= y.size()) std::memcpy(&v, &y[o], 4);
    return v;
  };
  const uint32_t n1 = u32(0x20), o1 = u32(0x24), n2 = u32(0x28), o2 = u32(0x2C), n8 = u32(0x58), o8 = u32(0x5C);
  if (y.size() < 0x70 || std::memcmp(y.data(), "#YDW", 4) != 0 || n1 < 3 || n1 > 128 || n2 > 128 ||
      size_t(o1) + 52 * n1 > y.size() || size_t(o2) + 40 * n2 > y.size() || size_t(o8) + 4 * n8 > y.size()) {
    REXLOG_WARN("dbz3 tablero b2: #YDW {} no valido", 387 + fase);
    return false;
  }
  Datos d;
  d.fase = fase;
  d.ancho = float(u32(0x0C));
  d.alto = float(u32(0x10));
  d.cols = std::max(1, int(u32(0x14) / 256));
  for (int k = 0, cuantas = u16(0x1C); k < cuantas && k < 960 && 0xB0 + size_t(k) < y.size(); ++k)
    d.band_ini.push_back(y[0xB0 + size_t(k)]);
  for (uint32_t k = 0; k < n1; ++k) {
    const size_t o = o1 + 52 * size_t(k);
    Casilla c;
    c.x = f32(o);
    c.y = f32(o + 4);
    for (int j = 0; j < 4; ++j) c.vec[j] = u16(o + 8 + 2 * j) < int(n1) ? u16(o + 8 + 2 * j) : 0;
    c.tipo = int(u32(o + 0x10));
    c.destino = int(u32(o + 0x14)) < int(n1) ? int(u32(o + 0x14)) : 0;
    c.escenario = y[o + 0x18];
    c.condicion = y[o + 0x19];
    c.musica = y[o + 0x1F];   // M4: la pista del combate de la casilla (ver PistaCombateB2)
    c.lucha = y[o + 0x1C] == 0x0A;
    d.casillas.push_back(c);
  }
  for (uint32_t p = 1; p < n2; ++p) {
    const size_t o = o2 + 40 * size_t(p);
    Ficha f;
    f.c = y[o];
    f.idx = y[o + 1];
    f.bando = y[o + 2];
    f.barras_max = std::max(1, int(y[o + 0xB]));
    f.barras = std::clamp(int(y[o + 6]), 1, f.barras_max);
    f.vida_max = u16(o + 0xC) > 0 ? u16(o + 0xC) : 3000;
    f.casilla = u16(o + 0x14);
    f.n = int(p);
    // +3/+4: tipo de movimiento inicial y su dato (ELF 0x2791A0 los copia a +0x1C/+0x1D, los mismos que cambia la orden 50)
    f.mov = y[o + 3] <= 8 ? y[o + 3] : 0;
    f.mov_p = y[o + 4];
    f.estado0 = y[o + 5];
    f.modo = y[o + 7];      // Saibaman: +0x07 del registro T2 (40 B), copiado a +0x1E por FUN_002791a0
                             // (b2.c:223478); elige su fila de sprites del #AMA (FUN_002762b0 b2.c:221416).
                             // La cambia la orden 56 (FUN_00276cd0 b2.c:221735). Antes se leia +0x2F, 7 bytes
                             // dentro del registro siguiente: los colores de fase IV/IX salian desplazados.
    if (f.c == 40) f.forma_b2 = 1000;   // Kibitoshin = Kaio-shin de B3 en su forma 1 (capsula 197; N100): marca para FormaTableroB3Impl
    if (f.casilla <= 0 || f.casilla >= int(n1)) continue;
    f.desde = f.casilla;
    d.piezas.push_back(f);
  }
  // H02: c36 (Gotenks) y c38 (Vegito) ya estan mapeados a B3 (64 forma 3 y 72 forma 2). c40 y cualquier otro ID sin
  // equivalencia medida no se inventan: se avisa una vez por fase y el consumidor cae al comportamiento seguro
  // (sin nombre/retrato/combate de ese personaje).
  for (const Ficha& f : d.piezas) {
    if (f.c == kSaibaman || B2aB3(f.c) >= 0) continue;
    bool repetido = false;
    for (const Ficha& q : d.piezas) {
      if (&q == &f) break;
      if (q.c == f.c) repetido = true;
    }
    if (repetido) continue;
    REXLOG_WARN("dbz3 tablero b2: [H] personaje B2 c={} (fase {}) sin mapeo a B3: sin nombre, retrato ni combate",
                f.c, fase + 1);
  }
  for (uint32_t i = 0; i < n8; ++i) {
    const size_t p = u32(o8 + 4 * size_t(i));
    if (p + 12 > y.size()) continue;
    Objeto ob;
    ob.casilla = u16(p);
    ob.enterrado = y[p + 2] != 0;
    ob.visible = y[p + 3] != 0;
    ob.tipo = y[p + 4];
    ob.valor = y[p + 5];
    for (uint32_t b = 0; b < u32(p + 8) && b < 16 && p + 16 + 4 * size_t(b) <= y.size(); ++b)
      ob.banderas.push_back(int32_t(u32(p + 12 + 4 * size_t(b))));
    if (ob.casilla <= 0 || ob.casilla >= int(n1)) continue;
    d.objetos.push_back(ob);
  }
  {   // scripts (T3 disparadores y T6 ordenes)
    const uint32_t n3 = u32(0x30), o3 = u32(0x34), n6 = u32(0x48), o6 = u32(0x4C);
    for (uint32_t i = 0; i < n3 && i < 4096; ++i) {
      const size_t q = u32(o3 + 4 * size_t(i));
      std::vector<int> r(5, 0);
      for (int k = 0; k < 5 && q + 20 <= y.size(); ++k) r[size_t(k)] = int32_t(u32(q + 4 * size_t(k)));
      for (int k = 0; k < r[4] && k < 16 && q + 24 + 4 * size_t(k) <= y.size(); ++k)
        r.push_back(int32_t(u32(q + 20 + 4 * size_t(k))));
      d.t3.push_back(std::move(r));
    }
    const uint32_t n4 = u32(0x38), o4 = u32(0x3C);
    for (uint32_t i = 0; i < n4 && i < 4096; ++i) {
      const size_t q = u32(o4 + 4 * size_t(i));
      if (q + 28 > y.size()) continue;
      std::vector<int> r;
      for (int k = 0; k < 7; ++k) r.push_back(int32_t(u32(q + 4 * size_t(k))));
      for (int k = 0; k < r[6] && k < 16 && q + 28 + 4 * size_t(k) + 4 <= y.size(); ++k)
        r.push_back(int32_t(u32(q + 28 + 4 * size_t(k))));
      d.t4.push_back(std::move(r));
    }
    const uint32_t n5 = u32(0x40), o5 = u32(0x44);
    for (uint32_t i = 0; i < n5 && i < 4096; ++i) {
      const size_t q = u32(o5 + 4 * size_t(i));
      if (q + 16 > y.size()) continue;
      std::vector<int> r;
      for (int k = 0; k < 4; ++k) r.push_back(int32_t(u32(q + 4 * size_t(k))));
      for (int k = 0; k < r[3] && k < 16 && q + 16 + 4 * size_t(k) + 4 <= y.size(); ++k)
        r.push_back(int32_t(u32(q + 16 + 4 * size_t(k))));
      d.t5.push_back(std::move(r));
    }
    for (uint32_t i = 0; i < n6 && i < 4096; ++i) {
      const size_t q = u32(o6 + 4 * size_t(i));
      std::vector<std::array<int, 6>> g;
      const uint32_t cuantas = q + 4 <= y.size() ? u32(q) : 0;
      for (uint32_t c = 0; c < cuantas && c < 256 && q + 4 + 24 * (size_t(c) + 1) <= y.size(); ++c) {
        std::array<int, 6> o{};
        for (int k = 0; k < 6; ++k) o[size_t(k)] = int32_t(u32(q + 4 + 24 * size_t(c) + 4 * size_t(k)));
        g.push_back(o);
      }
      for (const auto& o : g)
        if (o[1] == 71 && d.musica0 < 0) d.musica0 = o[2];
      d.guiones.push_back(std::move(g));
    }
  }
  const uint32_t n7 = u32(0x50), o7 = u32(0x54);
  for (uint32_t i = 0; i < n7 && size_t(o7) + 4 * (size_t(i) + 1) <= y.size(); ++i) {
    const size_t p = u32(o7 + 4 * size_t(i));
    if (p + 16 > y.size()) continue;
    const int tipo = int(u32(p + 4));
    if (tipo < 0 || tipo > 4) continue;   // 0 animado, 1 grupo, 2 fijo, 3 fijo, 4 capa de fondo
    d.colocados.push_back({int(u32(p)), tipo, f32(p + 8), f32(p + 12)});
  }
  // H02: diagnostico para medir el z-order de las capas T7 (no cambia el orden de dibujo). z = 0 es antes del mapa
  // (tipo 4) y z = 1 despues del mapa y antes de caminos y fichas (tipos 0-3); la x/y es la del registro del #YDW.
  for (const auto& ad : d.colocados) {
    if (ad.tipo != 1 && ad.tipo != 4) continue;
    REXLOG_INFO("dbz3_b2_t7 tipo={} obj={} z={} x={:.0f} y={:.0f} fase={}", ad.tipo, ad.obj, ad.tipo == 4 ? 0 : 1,
                ad.x, ad.y, fase + 1);
  }
  auto amt = [&](AfsPs2& afs, size_t k, std::vector<Imagen>& out) {
    const std::vector<uint8_t> e = afs.Entrada(k);
    for (int j = 0; j < std::min(CuantasAmt(e), 64); ++j) out.push_back(TexturaAmt(e, j));
  };
  amt(cmn, 346 + 5 * size_t(fase), d.mapa);
  amt(cmn, 342 + 5 * size_t(fase), d.fichas);
  amt(cmn, 345 + 5 * size_t(fase), d.adornos);
  // #AMA: 344+5k recortes de los adornos; 343+5k ventanas de mensaje de la fase (orden 38 de los scripts)
  auto leer_ama = [&](size_t k, std::vector<Datos::Sprite>& out) {
    const std::vector<uint8_t> am = cmn.Entrada(k);
    auto a32 = [&](size_t o) -> uint32_t {
      return o + 4 <= am.size() ? am[o] | am[o + 1] << 8 | am[o + 2] << 16 | uint32_t(am[o + 3]) << 24 : 0;
    };
    auto af = [&](size_t o) {
      float v = 0;
      if (o + 4 <= am.size()) std::memcpy(&v, &am[o], 4);
      return v;
    };
    if (am.size() < 0x10 || std::memcmp(am.data(), "#AMA", 4) != 0) return;
    for (uint32_t i = 0; i < a32(8) && size_t(a32(12)) + 0x40 * (size_t(i) + 1) <= am.size(); ++i) {
      const size_t o = a32(12) + 0x40 * size_t(i), q = a32(o + 0x3C);
      Datos::Sprite sp{-1, 0, 0, 0, 0, af(o), af(o + 4), af(o + 0x10) + 1, af(o + 0x14) + 1};
      if (a32(o + 0x38) > 0 && q + 0x30 <= am.size()) {
        sp.tex = int(a32(q + 4));
        sp.u0 = af(q + 0x20);
        sp.v0 = af(q + 0x24);
        sp.u1 = af(q + 0x28) + 1;
        sp.v1 = af(q + 0x2C) + 1;
      }
      const uint32_t nb = a32(o + 0x38);
      if (nb < 256 && q <= am.size() && size_t(nb) <= (am.size() - q) / 0x30) {
        for (uint32_t j = 0; j < nb; ++j) {
          const size_t b = q + 0x30 * size_t(j);
          sp.recortes.push_back({float(a32(b)), int32_t(a32(b + 4)), af(b + 0x20), af(b + 0x24),
                                 af(b + 0x28) + 1, af(b + 0x2C) + 1});
        }
      }
      const uint32_t na = a32(o + 0x30), oa = a32(o + 0x34);
      if (na > 0 && na < 256 && oa + 0x20 * size_t(na) <= am.size()) {
        sp.px = af(oa + 0x20 * (size_t(na) - 1) + 0x10);
        sp.py = af(oa + 0x20 * (size_t(na) - 1) + 0x14);
        sp.ex = af(oa + 0x20 * (size_t(na) - 1) + 4);
        sp.ey = af(oa + 0x20 * (size_t(na) - 1) + 8);
        for (uint32_t c = 0; c < na; ++c) {
          const size_t q2 = oa + 0x20 * size_t(c);
          sp.claves.push_back({float(a32(q2)), af(q2 + 0x10), af(q2 + 0x14), af(q2 + 0x1C), af(q2 + 4), af(q2 + 8), af(q2 + 0xC)});
        }
      }
      auto num = [&](size_t q) {   // float o entero, segun venga
        const float f = af(q);
        return f >= 0.5f && f < 100000.0f ? f : float(a32(q) < 100000 ? a32(q) : 0);
      };
      sp.dur = std::max(num(o + 0x20), num(o + 0x24));
      out.push_back(sp);
    }
  };
  leer_ama(344 + 5 * size_t(fase), d.sprites);
  leer_ama(343 + 5 * size_t(fase), d.textos);
  const std::string lc = T::LangCode();
  AfsPs2 loc;
  if (!loc.Abrir(iso, lc == "es" ? "DATA_SP.AFS" : lc == "fr" ? "DATA_FR.AFS" : lc == "it" ? "DATA_IT.AFS"
                                   : lc == "de" ? "DATA_DU.AFS" : "DATA_UK.AFS"))
    loc.Abrir(iso, "DATA_UK.AFS");
  amt(loc, 146 + size_t(fase), d.loc);
  amt(loc, 184, d.aliados);
  leer_ama(399, d.aliados_ama);
  amt(loc, 190, d.tarjeta);
  amt(loc, 189, d.tarjeta_cond);
  leer_ama(408, d.tarjeta_ama);
  g_iso_b2 = iso.string();
  g_loc_afs = lc == "es" ? "DATA_SP.AFS" : lc == "fr" ? "DATA_FR.AFS" : lc == "it" ? "DATA_IT.AFS" : lc == "de" ? "DATA_DU.AFS"
                                                                                                 : "DATA_UK.AFS";
  // Deseos de Shenron, en el orden de sus archivos: 540 (#0 "Portal de la nave de Babidi", trajes de Bulma), 539
  // (fusion y Pothala) y 538 ("Avance" de cada personaje).
  amt(loc, 540, d.deseos);
  amt(loc, 539, d.deseos);
  amt(loc, 538, d.deseos);
  amt(loc, 537, d.shen_frases);   // "Te concedere un deseo.", "Si, eso es aceptable.", ..., "Ahora debo irme..."
  amt(loc, 535, d.shen_frases);   // #6: "Tu, que has reunido las 7 Bolas..." (la pide el #SPX 329: builtin 0x157, loc 535)
  amt(cmn, 850, d.shen_marco);
  leer_ama(849, d.shen_ama);
  // Encargo 03/3: "Stage N" (240x72) y el primer objetivo (560x128) del #AMA 343+5k tienen su textura
  // en un indice FIJO dentro de DATA_<idioma> 146+k (medido: datos/ama343.json; record 0 = objetivo,
  // ultimos records = el rotulo de la fase). Antes se elegian por tamano y, con dos imagenes iguales
  // (fases IV/V/VI/VII tienen varias de 560x128), salia la primera en vez del indice de la fase.
  static const int kLocStage[kFases] = {35, 16, 22, 36, 16, 9, 7, 7, 31};
  static const int kLocObjetivo[kFases] = {28, 1, 0, 0, 0, 0, 0, 0, 0};
  auto elige = [&](int idx, int w, int h) {
    return idx >= 0 && idx < int(d.loc.size()) && d.loc[size_t(idx)].w == w && d.loc[size_t(idx)].h == h;
  };
  d.stage = elige(kLocStage[fase], 240, 72) ? kLocStage[fase] : -1;
  d.objetivo = elige(kLocObjetivo[fase], 560, 128) ? kLocObjetivo[fase] : -1;
  REXLOG_INFO("dbz3 tablero b2: fase {}: imagenes de fase: Stage {} (240x72), objetivo {} (560x128)", fase + 1,
              d.stage, d.objetivo);
  // M9 (mod opcional b2.goku_variante): con el mod activo, el tablero carga ya la variante de Goku CMN 339/340
  // (reutiliza VarianteGoku() del encargo 03; la orden 82 de los guiones sigue funcionando igual).
  if (std::getenv("DBZ3_TEST_B2_VARIANTE") || dbz3::opcional::Activo("b2.goku_variante"))
    VarianteGoku();   // PRUEBA: fuerza la variante de Goku (orden 82)
  if (d.stage < 0 || d.objetivo < 0)   // otra ISO/idioma: respaldo por tamano (como antes)
    for (size_t j = 0; j < d.loc.size(); ++j) {
      if (d.stage < 0 && d.loc[j].w == 240 && d.loc[j].h == 72) d.stage = int(j);
      if (d.objetivo < 0 && d.loc[j].w == 560 && d.loc[j].h == 128) d.objetivo = int(j);
    }
  if (g_d.iconos.empty()) {
    amt(loc, 188, d.hud);
    leer_ama(402, d.hud_ama);
    leer_ama(397, d.life_ama);
    amt(cmn, 398, d.iconos);
    std::vector<Imagen> v;
    amt(cmn, 404, v);
    if (!v.empty()) d.game_over = std::move(v[0]);
    v.clear();
    amt(loc, 146, v);   // el agujero y las cabezas verdes de la fase I valen para todas
    if (v.size() > 16 && v[13].w == 96 && v[13].h == 48 && v[16].w == 192 && v[16].h == 64) {
      d.hoyo = std::move(v[13]);
      d.cabezas = std::move(v[16]);
    }
    v.clear();
    amt(loc, 155, v);   // ponytail: se decodifica la entrada entera (una vez) para quedarse con el Goku de 64x88
    for (Imagen& im : v) {
      if (im.w == 64 && im.h == 88) d.goku = std::move(im);
    }
  } else {
    d.iconos = std::move(g_d.iconos);
    d.hud = std::move(g_d.hud);
    d.hud_ama = std::move(g_d.hud_ama);
    d.life_ama = std::move(g_d.life_ama);
    d.game_over = std::move(g_d.game_over);
    d.goku = std::move(g_d.goku);
    d.hoyo = std::move(g_d.hoyo);
    d.cabezas = std::move(g_d.cabezas);
  }
  REXLOG_INFO("dbz3 tablero b2: fase {} leida de {}: mapa {}x{} ({} piezas en {} columnas), {} casillas, {} piezas, "
              "{} objetos, {} fichas, {} iconos, {} adornos ({} sprites), {} del idioma {}, Goku {}", fase + 1,
              iso.filename().string(), d.ancho, d.alto, d.mapa.size(), d.cols, d.casillas.size() - 1, d.piezas.size(),
              d.objetos.size(), d.fichas.size(), d.iconos.size(), d.colocados.size(), d.sprites.size(), d.loc.size(), lc,
              d.goku ? "si" : "no");
  if (std::getenv("DBZ3_TEST_B2_DUMP")) {   // PRUEBA: vuelca piezas, disparadores y guiones de la fase
    for (const Ficha& f : d.piezas)
      REXLOG_INFO("dbz3 b2 dump: pieza {} pj {} ficha {} bando {} mov {} ({}) casilla {} estado0 {} barras {}", f.n, f.c,
                  f.idx, f.bando, f.mov, f.mov_p, f.casilla, f.estado0, f.barras_max);
    for (const auto& t : d.t3) {
      std::string l;
      for (int v : t) l += std::to_string(v) + " ";
      REXLOG_INFO("dbz3 b2 dump: t3 {}", l);
    }
    for (const auto& t : d.t4) {
      std::string l;
      for (int v : t) l += std::to_string(v) + " ";
      REXLOG_INFO("dbz3 b2 dump: t4 {}", l);
    }
    for (const auto& t : d.t5) {
      std::string l;
      for (int v : t) l += std::to_string(v) + " ";
      REXLOG_INFO("dbz3 b2 dump: t5 {}", l);
    }
    for (size_t k = 0; k < d.guiones.size(); ++k) {
      std::string l;
      for (const auto& o : d.guiones[k])
        l += "[" + std::to_string(o[0]) + ":" + std::to_string(o[1]) + " " + std::to_string(o[2]) + "," +
             std::to_string(o[3]) + "," + std::to_string(o[4]) + "," + std::to_string(o[5]) + "] ";
      REXLOG_INFO("dbz3 b2 dump: guion {} {}", k, l);
    }
  }
  if (d.mapa.empty() || d.iconos.size() < 30) {
    if (g_d.iconos.empty()) {   // devuelve lo prestado a la fase en curso
      g_d.iconos = std::move(d.iconos);
      g_d.hud = std::move(d.hud);
      g_d.hud_ama = std::move(d.hud_ama);
      g_d.life_ama = std::move(d.life_ama);
      g_d.game_over = std::move(d.game_over);
      g_d.goku = std::move(d.goku);
      g_d.hoyo = std::move(d.hoyo);
      g_d.cabezas = std::move(d.cabezas);
    }
    return false;
  }
  g_d = std::move(d);
  return true;
}

// ---- Pasos y giros de las piezas (R7) ----
constexpr float kVelGiro = 0.19635f * 60.0f;   // 0x800 de 0x10000 por cuadro de 60 Hz, en radianes por segundo
float Dif(float a, float b) { return std::remainder(b - a, 6.2831853f); }
float GiroDe(const Ficha& f, float t) {
  const float e = t - f.t_mov;
  if (e < 0) return f.giro0;
  if (e < f.t_giro) return f.giro0 + Dif(f.giro0, f.giro1) * (e / f.t_giro);
  const float vuelta = std::fabs(Dif(f.giro1, 0.0f)) / kVelGiro, b = e - f.t_giro - f.t_dur;
  if (b <= 0) return f.giro1;
  return b >= vuelta ? 0.0f : f.giro1 + Dif(f.giro1, 0.0f) * (b / vuelta);
}
// Ordenes 61/62: angulo fijo del registro visual (no vuelve al frente). El paso es 0x800/0x10000 por cuadro, el mismo
// kVelGiro de los pasos normales. 62 devuelve lo que tarda en llegar; 61 no bloquea.
float GiroDeOrden(const Ficha& f, float t) {
  if (!f.giro_obj_activo) return GiroDe(f, t);
  const float e = (t - f.giro_obj_t0) * kVelGiro, d = Dif(f.giro_obj0, f.giro_obj1);
  if (e <= 0) return f.giro_obj0;
  if (e >= std::fabs(d)) return f.giro_obj1;
  return f.giro_obj0 + (d < 0 ? -e : e);
}

bool Andando(const Ficha& f, float t) {
  if (f.punto == 1) return true;   // ordenes 65/66: anda al punto aunque su casilla no cambie
  const float e = t - f.t_mov - f.t_giro;
  return f.desde != f.casilla && e >= 0 && e < f.t_dur;
}
// La pieza empieza a andar de su casilla a `dest`: gira (si no es del jugador) y anda L / 4 cuadros.
// A2b: dos fichas del mismo bando comparten casilla: la que llega va al hueco 1 (FUN_00278A30, b2.c:223092); con el hueco 1 ya
// ocupado la casilla esta llena y no se puede elegir como destino (FUN_00278970, b2.c:223062; lo usa FUN_002793E0).
bool CasillaLlena(const Ficha& f, int casilla) {
  for (const Ficha& o : g_f)
    if (&o != &f && !o.fuera && !o.oculta && o.casilla == casilla && o.rival == f.rival && o.slot == 1) return true;
  return false;
}
void RecalcularSlots() {   // en orden de la tabla: la primera ficha de la casilla en el hueco 0, la siguiente en el 1
  for (size_t i = 0; i < g_f.size(); ++i) {
    g_f[i].slot = 0;
    if (g_f[i].fuera || g_f[i].oculta) continue;
    for (size_t j = 0; j < i; ++j)
      if (!g_f[j].fuera && !g_f[j].oculta && g_f[j].casilla == g_f[i].casilla && g_f[j].rival == g_f[i].rival) g_f[i].slot = 1;
    g_f[i].slot_ant = g_f[i].slot;
  }
}
void EmpezarPaso(Ficha& f, int dest, float ahora) {
  f.punto = 0;   // un paso por casillas anula el punto de las ordenes 65/66
  const float g = GiroDeOrden(f, ahora);   // continua desde el angulo fijo de 61/62 si lo habia
  f.giro_obj_activo = false;   // andar vuelve a mandar sobre el angulo fijo de las ordenes 61/62
  const auto &a = g_d.casillas[size_t(f.casilla)], &b = g_d.casillas[size_t(dest)];
  const float dx = b.x - a.x, dy = b.y - a.y;
  f.giro0 = g;
  f.giro1 = dx != 0 || dy != 0 ? std::atan2(dx, dy) : 0.0f;
  f.t_giro = f.jugador ? 0.0f : std::fabs(Dif(g, f.giro1)) / kVelGiro;
  if (f.jugador) f.giro0 = f.giro1;
  f.t_dur = std::max(0.1f, std::hypot(dx, dy) / 240.0f);
  f.t_mov = ahora;
  f.desde = f.casilla;
  f.casilla = dest;
  const int ant = f.slot;   // el hueco sale del orden de la tabla (medido: Goku, p1, queda en el 0 y Gohan, p5, pasa al 1)
  RecalcularSlots();
  f.slot_ant = ant;
}
float TiempoPaso(const Ficha& f) { return f.t_giro + f.t_dur; }

// ---- Reglas ----
bool Activa(const Ficha& f) { return !f.fuera && !f.oculta; }
int FichaEn(int casilla, bool rival) {
  for (size_t i = 0; i < g_f.size(); ++i) {
    if (Activa(g_f[i]) && g_f[i].casilla == casilla && (rival ? g_f[i].rival : g_f[i].jugador)) return int(i);
  }
  return -1;
}
bool QuedanMovs() {
  for (const Ficha& f : g_f) {
    if (Activa(f) && f.jugador && f.movs > 0) return true;
  }
  return false;
}
void SiguienteSel() {
  for (size_t k = 1; k <= g_f.size(); ++k) {
    const size_t i = (size_t(g_sel) + k) % g_f.size();
    if (Activa(g_f[i]) && g_f[i].jugador && g_f[i].movs > 0) {
      g_sel = int(i);
      g_dest = -1;
      return;
    }
  }
}

void Preparar() {
  g_estado = kPrepara;
  g_cand.clear();
  for (size_t i = 0; i < g_d.piezas.size(); ++i) {
    if (g_d.piezas[i].bando == 0 && g_d.piezas[i].c != kSaibaman && B2aB3(g_d.piezas[i].c) >= 0) g_cand.push_back(int(i));
  }
  // s9: el panel del original recorre la tabla 0x37A830 (FUN_002a1230), no el orden del #YDW: Gohan, Goten, Kid Trunks,
  // Piccolo, Krillin... (medido en B2: la lista de la fase I empieza por Gohan)
  static constexpr int kOrdenAliados[] = {28, 26, 27, 6, 4, 7, 8, 3, 5, 35, 24, 21, 33, 2};
  auto rango = [&](int i) {
    for (int j = 0; j < 14; ++j)
      if (kOrdenAliados[j] == g_d.piezas[size_t(i)].c) return j;
    return 99;
  };
  std::stable_sort(g_cand.begin(), g_cand.end(), [&](int x, int y) { return rango(x) < rango(y); });
  g_comp = g_cand.empty() ? 0 : std::clamp(g_comp, 0, int(g_cand.size()) - 1);
  g_elegir = 0;
  g_marcadas.clear();
  g_conf = -1;
  g_fase_sig = -1;
  g_pend_j = g_pend_r = g_pend_mov = -1;
  g_luchando = false;
}

void Guion(int k, int prof);
void AbrirEscena(int letra, int n);
void Empezar() {
  g_f = g_d.piezas;
  g_obj = g_d.objetos;
  g_buscada.assign(g_d.casillas.size(), false);
  g_jefe = -1;
  for (size_t i = 0; i < g_f.size(); ++i) {
    Ficha& f = g_f[i];
    f.rival = (f.bando & 2) != 0;
    f.jugador = f.bando == 4 && !f.rival;
    // las de estado 1/2/6 no estan en el tablero hasta que su guion las saca (sembrar, orden 32, orden 55)
    // A2b: FUN_002791A0 (b2.c:223450): el bit 4 de T2 +2 es el estado 2 (en el tablero) y el 2 el bando rival: bando 6 = rival ya
    // en el tablero (IX: los 5 Saibaman de colores y Kid Bu, medido en PCSX2 con la lista abierta); bando 2 = fuera hasta su guion.
    f.oculta = (f.bando == 0 && f.c == kSaibaman) || (f.rival && !(f.bando & 4));
    if (f.bando == 0 && f.c != kSaibaman) f.fuera = true;
    if (f.rival && (g_jefe < 0 || f.barras_max >= g_f[size_t(g_jefe)].barras_max)) g_jefe = int(i);
  }
  // los aliados salen en la orden 69 del guion de inicio (g_elegir), no aqui
  RecalcularSlots();
  g_sel = 0;
  for (size_t i = 0; i < g_f.size(); ++i) {
    Ficha& f = g_f[i];
    if (f.jugador) f.movs = f.movs_turno = 1;
    if (f.jugador && f.c == kGoku && f.bando == 4) g_sel = int(i);
  }
  {   // Bolas de la fase (ELF 0x27BD80)
    std::vector<int> sitios, nums;
    for (const Objeto& o : g_obj)
      if (o.tipo == 0) sitios.push_back(o.casilla);
    g_bolas &= 0x7F;
    g_bolas_ini = g_bolas;
    for (int b = 0; b < 7; ++b)
      if (!(g_bolas >> b & 1)) nums.push_back(b);
    const int tiene = 7 - int(nums.size()), n = tiene >= 7 ? 0 : std::clamp(std::min(g_d.fase, 6) - tiene + 1, 0, 3);
    std::minstd_rand rnd(unsigned(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::shuffle(sitios.begin(), sitios.end(), rnd);
    std::shuffle(nums.begin(), nums.end(), rnd);
    g_bolas_fase.clear();
    for (size_t k = 0; k < size_t(n) && k < sitios.size() && k < nums.size(); ++k) g_bolas_fase.push_back({sitios[k], nums[k]});
    for (const auto& b : g_bolas_fase) REXLOG_INFO("dbz3 tablero b2: Bola {} en la casilla {}", b[1] + 1, b[0]);
  }
  g_turno = 1;
  g_radar = false;
  // R7: la banda (D_RADARBASE, bit 0x02 de 0x6992DC) no sale sola al empezar: solo si el jugador la dejo puesta con L1
  // (bit 0x08, guardado con la partida; FUN_0026FFF0 al empezar el turno), al coger una Bola (0x26F340) o el Radar
  g_hud_oculto = LeerProgreso("tablero_b2_marcador", 0) == 0;
  // PRUEBA: DBZ3_TEST_B2_RADAR=1 empieza con el Radar del Dragon; DBZ3_TEST_B2_BOLAS=N (7 bits) con esas Bolas en la barra
  if (std::getenv("DBZ3_TEST_B2_RADAR")) g_radar = true;
  if (const char* b = std::getenv("DBZ3_TEST_B2_BOLAS")) g_bolas |= std::atoi(b) & 0x7F;
  g_menu = 0;
  g_mensajes.clear();
  g_premio_id = 0;
  g_espera_x = false;
  g_subidas.clear();
  LeerBanderas();   // R7: las de las fases anteriores siguen (FUN_00238870 copia 0x693A1C -> 0x699260)
  // s9b: ...salvo las 0..cuantas-1 de ESTA fase, que arrancan en el valor del #YDW (ELF 0x234930). Sin esto la 302 que
  // dejaba la fase IV hacia perder la V al segundo rival (script 306: orden 43).
  for (size_t k = 0; k < g_d.band_ini.size(); ++k) {
    uint32_t& w = g_banderas[k >> 5];
    if (g_d.band_ini[k]) w |= 1u << (k & 31);
    else w &= ~(1u << (k & 31));
  }
  g_musica = g_d.musica0;
  for (const auto& t : g_d.t3)   // disparadores del turno 1 (al empezar la fase)
    if (t[0] == -1 && t[1] == 0 && t[2] == 1 && Condiciones(t, 5)) Guion(t[3], 0);
  if (const char* ev = std::getenv("DBZ3_TEST_B2_EVENTO")) {   // PRUEBA (X0e): arranca ademas el guion N de la fase
    REXLOG_INFO("dbz3 tablero b2: prueba: evento (guion) {} forzado", std::atoi(ev));
    Guion(std::atoi(ev), 0);
  }
  g_dest = -1;
  g_estado = kJuego;
  g_msg.clear();
  if (g_d.fase == 0) AbrirEscena('T', 155);   // la intro del Mundo Dragon: Goku se lanza a la batalla
  // La camara del original (0x699400/0x699404) NO se reinicia al cargar la fase: sus unicos escritores son las funciones
  // de camara (FUN_0023ACB0 b2.c:191716, FUN_0023AF30 b2.c:191776 y FUN_0023B080 b2.c:191819), asi que sigue donde la
  // dejo la fase anterior y la primera vez vale (0,0) (BSS). La orden 9 del guion de inicio la lleva a su casilla
  // (fase I: casilla 7). [H] el (0,0) inicial: se confirma con la sonda (RUNBOOK 02, caso B).
  if (!g_cam_ini) {
    g_cam_x = g_cam_y = 0;
    g_cam_ini = true;
  }
  REXLOG_INFO("dbz3 tablero b2: fase {} empieza: {}, {} candidatos a aliado, jefe {}", g_d.fase + 1,
              Nombre(g_f[size_t(g_sel)]), g_cand.size(), g_jefe >= 0 ? Nombre(g_f[size_t(g_jefe)]) : std::string("ninguno"));
}

void Guion(int k, int prof);
// Disparadores de contacto: T3 {pieza, pieza, 0, script} y T4 por personaje con banderas (el primero que vale).
int g_suceso = -1;   // fila de T4 del ultimo contacto (iGpffff8d8c del ELF)
int PersonajeDe(int n) {   // personaje (+0 del registro de T2) de la pieza n
  for (const Ficha& f : g_d.piezas)
    if (f.n == n) return f.c;
  return -1;
}
void SucesosDeContacto(const Ficha& a, const Ficha& b) {
  g_suceso = -1;
  for (size_t i = 1; i < g_d.t4.size(); ++i) {   // como el ELF, desde la fila 1
    const auto& r = g_d.t4[i];
    const int ca = r[0] ? PersonajeDe(r[0]) : -1, cb = r[1] ? PersonajeDe(r[1]) : -1;
    bool vale;
    if (ca < 0 && cb < 0) vale = false;
    else if (cb < 0) vale = ca == a.c || ca == b.c;
    else if (ca < 0) vale = cb == a.c || cb == b.c;
    else vale = (ca == a.c && cb == b.c) || (ca == b.c && cb == a.c);
    // r[2]: casilla del contacto (+0x0A de la pieza que se mueve, su destino); 0 = cualquiera
    if (!vale || (r[2] != 0 && r[2] != a.casilla && r[2] != b.casilla) || !Condiciones(r, 7)) continue;
    g_suceso = int(i);
    REXLOG_INFO("dbz3 tablero b2: contacto {} y {}: suceso {} (script {})", Nombre(a), Nombre(b), i, r[3]);
    Guion(r[3], 0);
    break;
  }
}
// Tras el combate del contacto: T5 de esa fila con el suceso (ELF 0x235410 -> 0x27F3A0). R7: el codigo es 1 gana el
// jugador, 4 gana y el rival queda fuera (FUN_0027A0D0), 2 gana el rival, 3 gana el rival y la ficha del jugador queda
// fuera; 5 = cualquiera. Comprobado con los datos: fase IV, T4 #27 Goku contra Cell, suceso 4 -> guion 413 -> 306 (orden
// 41, fase superada).
void SucesosDeCombate(bool gana, bool fuera) {
  if (g_suceso < 0) return;
  const int cod = gana ? (fuera ? 4 : 1) : (fuera ? 3 : 2);
  REXLOG_INFO("dbz3 tablero b2: resultado del suceso {}: codigo {} (banderas 301={} 302={} 305={})", g_suceso, cod,
              Bandera(301), Bandera(302), Bandera(305));
  for (const auto& r : g_d.t5) {
    if (r[0] != g_suceso || (r[1] != 5 && r[1] != cod) || !Condiciones(r, 4)) continue;
    REXLOG_INFO("dbz3 tablero b2: tras el suceso {}: script {}", g_suceso, r[2]);
    Guion(r[2], 0);
    break;
  }
  g_suceso = -1;
}
void Contacto(int j, int r) {
  SucesosDeContacto(g_f[size_t(j)], g_f[size_t(r)]);
  g_pend_j = j;
  g_pend_r = r;
  g_pend_t = g_ahora;
  g_marca = g_f[size_t(r)].casilla;
  g_marca_icono = 8;   // "!!"
  g_marca_t = g_ahora;
  if (B2aB3(g_f[size_t(r)].c) < 0 || B2aB3(g_f[size_t(j)].c) < 0) {
    g_pend_j = g_pend_r = g_pend_mov = -1;
    g_f[size_t(j)].punto = 0;
    Aviso(Nombre(g_f[size_t(r)]) + T::T(" no puede luchar aquí (personaje solo de Budokai 2).",
                                         " can't fight here (Budokai 2-only character)."));
    return;
  }
  SonidoEventoB2("contacto", 7);   // FUN_00235780 (b2.c:188927-188928): efecto del contacto antes del combate
  Aviso(Nombre(g_f[size_t(j)]) + T::T(" contra ", " versus ") + Nombre(g_f[size_t(r)]) + "!", 8);
}

std::vector<int> Distancias(int c);
// s9: coger la daga / la armadura. El original no pone ningun cartel (medido en PCSX2, savestate 3, casilla de la espada):
// la pieza muestra "!" y el objeto, y sobre ella sale "+010%" con el kanji 攻 (daga) / 防 (armadura): son las mismas
// animaciones de las ordenes 83/84 (tareas 0x27BAB0 y 0x27BD00); aqui se lanzan igual, con el sonido del 110.
void SubidaDeObjeto(const Ficha& f, int valor, bool defensa) {
  Subida s;
  s.n = f.n;
  s.signo = 1;
  s.valor = valor & 0xFF;
  s.defensa = defensa;
  s.t0 = g_ahora;
  g_subidas.push_back(s);
  REXLOG_INFO("dbz3 tablero b2: {}: {} +{} % (animacion de las ordenes 83/84)", Nombre(f), defensa ? "armadura" : "daga", valor);
}
void Coger(Ficha& f, Objeto& o) {
  o.hecho = true;
  char t[160];
  switch (o.tipo) {
    case 0: {
      const int b = BolaEn(o.casilla);
      if (b < 0) return Aviso(T::T("No hay nada.", "Nothing here."));
      g_bola_n = g_bolas_fase[size_t(b)][1];
      g_bolas |= 1 << g_bolas_fase[size_t(b)][1];
      g_bolas_fase.erase(g_bolas_fase.begin() + b);
      GuardarProgreso("tablero_b2_bolas", g_bolas);
      g_bola_t = g_ahora;   // D_DBGET: sin letrero; la Bola entra en la barra del marcador
      if (g_hud_oculto) {   // la banda entra con la Bola (0x26F340 -> 0x26FFF0)
        g_hud_oculto = false;
        g_hud_t = g_radar_t = g_ahora;
      }
      REXLOG_INFO("dbz3 tablero b2: ¡Una Bola de Dragón! (bolas {:#x})", g_bolas);
      return;
    }
    case 9: {   // el radar dice hacia donde esta la Bola mas cercana por los caminos (ELF 0x27CC30)
      g_radar = true;   // el radar del marcador (D_RADAR) se enciende con sus flechas hacia las Bolas
      g_hud_oculto = false;
      if (g_bolas_fase.empty()) return Aviso(T::T("Radar del Dragón: no queda Bola en esta fase.", "Dragon Radar: no Ball left here."), 26);
      const std::vector<int> dist = Distancias(f.casilla);
      int cerca = g_bolas_fase[0][0];
      for (const auto& bo : g_bolas_fase)
        if (dist[size_t(bo[0])] < dist[size_t(cerca)]) cerca = bo[0];
      const Casilla &a = g_d.casillas[size_t(f.casilla)], &b = g_d.casillas[size_t(cerca)];
      const float dx = b.x - a.x, dy = b.y - a.y;
      const char* dir = std::abs(dx) > 2 * std::abs(dy) ? (dx > 0 ? T::T("la derecha", "the right") : T::T("la izquierda", "the left"))
                        : std::abs(dy) > 2 * std::abs(dx) ? (dy > 0 ? T::T("abajo", "down") : T::T("arriba", "up"))
                        : dy < 0 ? (dx > 0 ? T::T("arriba a la derecha", "up and right") : T::T("arriba a la izquierda", "up and left"))
                                 : (dx > 0 ? T::T("abajo a la derecha", "down and right") : T::T("abajo a la izquierda", "down and left"));
      std::snprintf(t, sizeof(t), T::T("Radar del Dragón: la Bola está hacia %s.", "Dragon Radar: the Ball is %s."), dir);
      return Aviso(t, 26);
    }
    case 8:   // al monedero del HUB (antes se sumaba a g_zenie y se perdia: auditoria8/C)
      g_zenie += o.valor * 100;
      dbz3::rpg::Premio(o.valor * 100);
      std::snprintf(t, sizeof(t), T::T("%d zenie.", "%d zenie."), o.valor * 100);
      return Aviso(t, 25);
    case 5:
      f.movs_turno = 2;
      return Aviso(Nombre(f) + T::T(": cebolla, mueve 2 casillas por turno.", ": onion, moves 2 spaces per turn."), 24);
    case 1:   // daga: +valor % al dano que hace en sus combates (Reglas::ataque). ponytail: que duren toda la fase y
              // se sumen es lo simple; el efecto exacto esta en el ELF 0x26FE80 sin descifrar
      f.ataque += o.valor;
      return SubidaDeObjeto(f, o.valor, false);
    case 3:   // armadura: se anula valor % del dano que recibe (Reglas::defensa)
      f.defensa += o.valor;
      return SubidaDeObjeto(f, o.valor, true);
    case 7: {   // la capsula de B3 equivalente (o una al azar de las que tienen equivalente); antes solo avisaba
      int c = dbz3::pasarela::nave::CapsulaDeB2(o.valor);
      if (!c) {
        static const int kHay[] = {180, 179, 443, 380, 187, 440, 381, 194, 182, 181, 398, 213, 458, 382, 188, 214, 416,
                                   445, 426, 383, 215, 193};
        c = dbz3::pasarela::nave::CapsulaDeB2(kHay[std::rand() % int(std::size(kHay))]);
      }
      DarCapsulaB3(c);
      REXLOG_INFO("dbz3 tablero b2: cápsula al azar: n.º {}", c);   // el número va al registro: al jugador no le dice nada
      return Aviso(T::T("Cápsula al azar: ya está en tu inventario.", "Random capsule: now in your inventory."), 22);
    }
    case 6:
      return Aviso(T::T("Mr. Popo.", "Mr. Popo."), 16);
  }
  std::snprintf(t, sizeof(t), T::T("Objeto de tipo %d.", "Item of type %d."), o.tipo);
  Aviso(t);
}

// Casillas que hay de `c` a cada casilla por los caminos, sin mirar las fichas (la tabla n x n de bytes del original,
// iGpffff8d24 en el ELF europeo; 255 = sin camino).
std::vector<int> Distancias(int c) {
  std::vector<int> d(g_d.casillas.size(), 255);
  std::vector<int> cola{c};
  d[size_t(c)] = 0;
  for (size_t q = 0; q < cola.size(); ++q) {
    for (int v : g_d.casillas[size_t(cola[q])].vec) {
      if (v <= 0 || d[size_t(v)] != 255) continue;
      d[size_t(v)] = d[size_t(cola[q])] + 1;
      cola.push_back(v);
    }
  }
  return d;
}
// Paso de un rival como el tipo 4 del original (ELF 0x273F40 + 0x273180 + 0x276B80): elige la ficha del jugador mas
// cercana por los caminos (la primera con menos casillas), va a la vecina (en el orden 0..3 de la casilla, la primera)
// que deja menos casillas hasta ella y, si en esa vecina hay otro rival, se queda. -1 = no se mueve.
int PasoA(int desde, int meta);
int PasoHacia(int desde) {
  const std::vector<int> dd = Distancias(desde);
  int meta = -1, mejor = 255;
  for (const Ficha& f : g_f) {
    if (Activa(f) && f.jugador && dd[size_t(f.casilla)] < mejor) {
      mejor = dd[size_t(f.casilla)];
      meta = f.casilla;
    }
  }
  return PasoA(desde, meta);
}
// Paso de `desde` hacia la casilla `meta` (ELF 0x273180): la primera vecina con menos casillas hasta ella; si en esa
// vecina hay otro rival, se queda (0x276B80). -1 = no se mueve.
int PasoA(int desde, int meta) {
  if (meta <= 0 || meta == desde) return -1;
  const std::vector<int> dm = Distancias(meta);
  int paso = -1, menor = 255;
  for (int v : g_d.casillas[size_t(desde)].vec) {
    if (v > 0 && dm[size_t(v)] < menor) {
      menor = dm[size_t(v)];
      paso = v;
    }
  }
  // 0x276B80 solo descarta el paso si en la vecina hay una pieza del mismo bando con +0xC == 1, y FUN_002791A0 deja +0xC a
  // 0 al cargar: los rivales pueden pasar por donde hay otro (R7; antes un Saibaman sembrado dejaba a Raditz parado)
  return paso;
}

// Turno de los rivales. En el original cada pieza tiene un tipo de movimiento (pieza +0x1C, tabla 0x378FE0: 0 quieto,
// 2 ir a una casilla, 3 al azar, 4 perseguir a la mas cercana, 5 seguir a una pieza, 6 perseguir a la de menos +1, 7/8
// ir a un objetivo o perseguir) que ponen los scripts de cada fase (orden de script 0x293EE0 -> 0x276D80). ponytail:
// hasta descifrar esos scripts, todos los que se mueven usan el tipo 4, cada dos turnos (FAQ); Saibaman y Dr. Gero
// quietos (tipo 0).
// Script k de la fase: de sus ordenes se portea la 50 (tipo de movimiento de una pieza: pieza, tipo, dato; ELF 0x293EE0)
// y la 44 (salta a otro script, 0x294450). ponytail: las demas (dialogos, camara, apariciones, combates, objetos) siguen
// sin portear; su lista y lo que llama cada una: E:/DBZ3HD_trabajo/R2/scr_ops.txt.
bool EscenaAbierta();
float OrdenEscena(int k, const std::array<int, 6>& o);
ImVec2 PosFicha(const Ficha& f);
float IrAPunto(Ficha& f, int orden, float x, float y, float ahora);   // ordenes 65/66
void ActualizarPuntos();                                              // llegada de las ordenes 65/66
float Orden(int k, const std::array<int, 6>& o) {
  // X0 (studies 5): traza de TODA orden ejecutada, solo en pruebas; tools/comparar_modo.py b2 la compara con el original.
  static const bool traza_ordenes = std::getenv("DBZ3_TEST_B2_ORDENES") != nullptr;
  if (traza_ordenes) REXLOG_INFO("dbz3 tablero b2: orden ejecutada {}", o[1]);
  float espera = 0;
  {
    if (o[1] >= 1 && o[1] <= 3 && o[2] >= 0 && o[2] < 2048) {   // banderas
      uint32_t& w = g_banderas[size_t(o[2]) >> 5];
      const uint32_t b = 1u << (o[2] & 31);
      w = o[1] == 1 ? (w | b) : o[1] == 2 ? (w & ~b) : (w ^ b);
      return espera;
    }
    // 58-62 (ELF 0x293CE0/0x293CA0/0x293C60/0x293C20/0x293BE0, tabla de ordenes 0x379EC0): 58-60 fijan el estado de
    // dibujo 0xF/0x10/0x11 y el clip +0x1C (L01: se carga del banco 334 y lo pinta DibFicha3D); 61/62 el angulo +0x24
    // (1/65536 de vuelta) y giran 0x800 por cuadro. 62 espera a llegar (0x37A020[62] = 1); 61 no.
    if (o[1] >= 58 && o[1] <= 62) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        if (o[1] <= 60) {
          f.anim_estado = o[1] == 58 ? 0x0F : o[1] == 60 ? 0x10 : 0x11;
          f.anim_b2 = o[3];
          f.anim_t0 = g_ahora;
          static bool aviso[3] = {};
          if (!aviso[o[1] - 58]) {
            aviso[o[1] - 58] = true;
            REXLOG_INFO("dbz3 tablero b2: B2 orden {}: estado 0x{:X}, clip +0x1C={}", o[1], f.anim_estado, f.anim_b2);
          }
        } else {
          f.giro_obj0 = GiroDeOrden(f, g_ahora);
          f.giro_obj1 = float(o[3] & 0xFFFF) * 6.2831853f / 65536.0f;
          f.giro_obj_activo = true;
          f.giro_obj_t0 = g_ahora;
          f.giro_obj_dur = std::fabs(Dif(f.giro_obj0, f.giro_obj1)) / kVelGiro;
          REXLOG_INFO("dbz3 tablero b2: B2 orden {}: pieza={} gira a {}/65536 ({:.1f} grados, {:.0f} cuadros){}", o[1],
                      o[2], o[3], f.giro_obj1 * 57.29578f, f.giro_obj_dur * 60.0f, o[1] == 62 ? ", espera" : "");
          return o[1] == 62 ? f.giro_obj_dur : 0.0f;
        }
      }
      return espera;
    }
    // 63/64 {pieza, casilla}: la pieza va andando a esa casilla (ELF 0x277540; -1 = la suya). 58-62 animan el modelo 3D de
    // la pieza (58-60 animacion +0x1C, 61/62 la giran hacia un angulo +0x24 en pasos de 0x800/0x10000) y 65/66 la llevan
    // a un punto del mapa (abajo, studies 2 G04).
    if ((o[1] == 63 || o[1] == 64) && o[3] > 0 && o[3] < int(g_d.casillas.size())) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        EmpezarPaso(f, o[3], g_ahora);
        REXLOG_INFO("dbz3 tablero b2: script {}: {} anda a la casilla {}", k, Nombre(f), o[3]);
        espera = TiempoPaso(f);
      }
      return espera;
    }
    // studies 2 G04: 65/66 {pieza, x, y} (FUN_002774F0): la pieza anda a un punto del mapa a 4 px por cuadro (240 px/s),
    // sin cambiar su casilla logica. 65 no bloquea (tabla 0x37A020[65] = 0); 66 bloquea hasta llegar. Al llegar la
    // funcion original devuelve 2; aqui, si ya esta en el punto, se esperan esos 2 cuadros.
    if (o[1] == 65 || o[1] == 66) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        const float resta = IrAPunto(f, o[1], float(o[3]), float(o[4]), g_ahora);
        REXLOG_INFO("dbz3 tablero b2: B2 orden {}: pieza={} va a ({}, {}){}{}", o[1], o[2], o[3], o[4],
                    o[1] == 66 ? ", espera" : "", resta > 0.0f ? " (" + std::to_string(resta) + " s)" : "");
        return o[1] == 66 ? resta : 0.0f;
      }
      return espera;
    }
    if (o[1] == 55 || o[1] == 57 || o[1] == 72 || o[1] == 73) {   // colocar pieza / cambiar su ficha (ELF 0x27A370,
      // 0x27A3F0, 0x276B30, 0x276AD0): 55 {pieza, casilla}, 57 {pieza, otra} a la casilla de la otra, 72 {pieza, ficha},
      // 73 {pieza, otra}: la ficha de la otra
      Ficha* pa = nullptr;
      const Ficha* pb = nullptr;
      for (Ficha& f : g_f) {
        if (f.n == o[2]) pa = &f;
        if (f.n == o[3]) pb = &f;
      }
      if (!pa) return espera;
      if (o[1] == 55 && o[3] > 0 && o[3] < int(g_d.casillas.size())) pa->casilla = pa->desde = o[3];
      if (o[1] == 57 && pb) pa->casilla = pa->desde = pb->casilla;
      if (o[1] == 55 || o[1] == 57) pa->punto = 0;   // una colocacion nueva anula el punto de las ordenes 65/66
      if (o[1] == 72) pa->idx = o[3];
      if (o[1] == 73 && pb) pa->idx = pb->idx;
      REXLOG_INFO("dbz3 tablero b2: script {}: orden {} ({}, {}) a {}", k, o[1], o[2], o[3], Nombre(*pa));
      return espera;
    }
    if (o[1] == 38) {
      auto dentro = [](int k) {
        return k >= 0 && k < int(g_d.textos.size()) && g_d.textos[size_t(k)].tex >= 0 && g_d.textos[size_t(k)].py > 0 &&
               g_d.textos[size_t(k)].py < 448;
      };
      if (dentro(o[2]) || dentro(o[3])) {
        g_mensajes.push_back({o[2], o[3]});
        REXLOG_INFO("dbz3 tablero b2: script {}: mensaje {} y {}", k, o[2], o[3]);
      }
      return espera;
    }
    if (o[1] == 39) {   // espera a la X (A) del jugador (0x2947B0; ver g_espera_x)
      g_espera_x = true;
      REXLOG_INFO("dbz3 tablero b2: script {}: orden 39, espera a que pulses A", k);
      return espera;
    }
    if (o[1] == 70 || o[1] == 8) {   // capsula de premio con su ventana (ver g_premio_id)
      // 8 {grupo} (FUN_00295C10, b2.c:239395): igual que la 70 pero sin pieza (mascara = todos tus personajes); la usan
      // los guiones de despues de vencer a un Saibaman (fase I: 62/64 -> grupo 35).
      const int pj = o[1] == 8 ? -1 : PersonajeDe(o[3]);
      const int id = dbz3::pasarela::nave::CapsulaAlAzar(o[2], pj);
      REXLOG_INFO("dbz3 tablero b2: script {}: orden {} (grupo {}, pieza {} = personaje {}): capsula {}{}", k, o[1], o[2],
                  o[1] == 8 ? 0 : o[3], pj, id, id ? (dbz3::pasarela::nave::CapsulaNueva(id) ? " nueva" : "") : " (ninguna: no hay ventana)");
      if (id > 0) {
        g_premio_id = id;
        g_premio_t = -10;
      }
      return espera;
    }
    if (o[1] == 83 || o[1] == 84) {   // "攻/防 +N" sobre la pieza (ver Subida)
      Subida s;
      s.n = o[2];
      s.signo = o[3];
      s.valor = o[4] & 0xFF;   // FUN_0027BD00: (u16)(a2 & 0xFF)
      s.defensa = o[1] == 84;
      s.t0 = g_ahora;
      const auto& la = g_d.life_ama;
      float cuadros = 120;   // signo 0: 120 cuadros (b2.c:237739)
      if (s.signo != 0) {
        cuadros = 0;
        for (int j = 0; j < 4; ++j) {
          const int an = (s.defensa ? 120 : 116) + j;   // tablas 0x37A090 / 0x37A080 del ELF
          if (an < int(la.size())) cuadros = std::max(cuadros, la[size_t(an)].dur - 1);
        }
      } else {
        SonidoCanal3(13);   // 0x0D al empezar (b2.c:237736)
        s.sonado = true;
      }
      g_subidas.push_back(s);
      REXLOG_INFO("dbz3 tablero b2: script {}: orden {} ({} {}{}) en la pieza {}: {:.0f} cuadros", k, o[1],
                  s.defensa ? "defensa" : "ataque", s.signo ? "+" : "-", s.valor, s.n, cuadros);
      return cuadros / 60.0f;
    }
    if (o[1] == 33 || o[1] == 35 || o[1] == 36 || o[1] == 53 || o[1] == 54) {
      // 33 {pieza} (FUN_0027A4B0): estado de la pieza (+6 & 0xF) = 1, fuera del tablero hasta que la 32 la vuelva a
      //   poner (= 2, FUN_0027A6E0). 35/36 {pieza, n} (FUN_00276E10/00276DE0): +0x10 / +0x12 += n (ataque / defensa, los
      //   mismos que la daga y la armadura). 53/54 {pieza, n} (FUN_00276D10/00276CF0): +0x16 / +0x14 = n (ki y vida del
      //   siguiente combate: FUN_00279050 los copia al bloque del combate 0x69932A/0x69932C, b2.c:223404). [H] el combate
      //   de B3 no recibe aun el ki ni la vida de la pieza (pasarela_misiones.cpp no es de este equipo): se guardan.
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        if (o[1] == 33) {
          f.oculta = true;
          REXLOG_INFO("dbz3 tablero b2: script {}: orden 33: {} sale del tablero", k, Nombre(f));
        } else if (o[1] == 35 || o[1] == 36) {
          (o[1] == 35 ? f.ataque : f.defensa) += int16_t(o[3]);
          REXLOG_INFO("dbz3 tablero b2: script {}: orden {}: {} {} {:+d} (ahora {})", k, o[1], Nombre(f),
                      o[1] == 35 ? "ataque" : "defensa", int16_t(o[3]), o[1] == 35 ? f.ataque : f.defensa);
        } else {
          (o[1] == 53 ? f.ki_b2 : f.vida_b2) = int16_t(o[3]);
          REXLOG_INFO("dbz3 tablero b2: script {}: orden {}: {} {} = {}", k, o[1], Nombre(f), o[1] == 53 ? "ki" : "vida",
                      int16_t(o[3]));
        }
      }
      return espera;
    }
    if (o[1] == 41 || o[1] == 42 || o[1] == 43) {   // fin de la fase (ver g_fase_sig)
      if (o[1] == 43) {
        g_estado = kPerdida;
        REXLOG_INFO("dbz3 tablero b2: script {}: orden 43, fase {} perdida", k, g_d.fase + 1);
      } else {
        g_fase_sig = o[1] == 41 ? std::clamp(o[2], 0, kFases - 1) : -1;
        g_estado = kGanada;
        if (o[1] == 41) GuardarProgreso("tablero_b2_fase", g_fase_sig);
        GuardarBanderas(o[1] == 42);   // 0x238320 las guarda; la 42 borra la partida del Mundo Dragon
        REXLOG_INFO("dbz3 tablero b2: script {}: orden {}, fase {} completada (siguiente {}, bolas {:#x})", k, o[1],
                    g_d.fase + 1, g_fase_sig + 1, g_bolas);
      }
      return espera;
    }
    if (o[1] == 69) {
      int libres = 0;
      for (int c : g_cand) libres += g_f[size_t(c)].fuera;
      g_elegir = std::min(o[2], libres);
      g_marcadas.clear();
      g_conf = -1;
      g_comp = 0;
      REXLOG_INFO("dbz3 tablero b2: script {}: eleccion de {} aliado(s) entre {}", k, g_elegir, libres);
      return espera;
    }
    if (o[1] >= 74 && o[1] <= 79 && !g_iso_b2.empty()) {   // sonido {id, cuadros}: ELF 0x293630..0x2933B0
      // 74/75/76 una vez, 77/78/79 en bucle cada a1 cuadros; tipo 0/3/4. Tipo 4 = banco de la fase (DATA_<idioma> 156+3k:
      // cuerpo, 157+3k indice de 19 sonidos); tipo 3 = banco general (142/143, 26 sonidos; el id 22 de la orden 75 no cabe
      // en los 19 de la fase). Tipo 0 (nunca aparece) = el de la fase. R7 b2_sonido.
      const int tipo = (o[1] - 74) % 3 == 1 ? 3 : (o[1] - 74) % 3 == 2 ? 4 : 0;
      const int cuerpo = tipo == 3 ? 142 : 156 + 3 * std::max(g_d.fase, 0);
      const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.7f;
      REXLOG_INFO("dbz3 tablero b2: script {}: orden {}: sonido tipo {} id {} ({})", k, o[1], tipo, o[2],
                  o[1] >= 77 ? "bucle" : "una vez");
      const bool prueba = std::getenv("DBZ3_TEST_MUSICA") != nullptr;
      OirSonidoPs2(g_iso_b2, g_loc_afs, cuerpo, o[2], tipo, prueba ? 1.0f : vol,
                   o[1] >= 77 ? std::max(o[3], 1) * 1000 / 60 : 0, prueba);
      return espera;
    }
    if (o[1] == 71) {   // musica (uRam006992DB = a0)
      g_musica = o[2];
      REXLOG_INFO("dbz3 tablero b2: script {}: musica {} (ADX_US {})", k, o[2], PistaB2(o[2]));
      return espera;
    }
    if (o[1] == 82) {   // variante del Goku del tablero (ELF 0x293140): CMN 339/340 + bit 0x04 de 0x6992DC
      VarianteGoku();
      REXLOG_INFO("dbz3 tablero b2: script {}: orden 82 (variante de Goku)", k);
      return espera;
    }
    // studies 2 G04: orden 52 {pieza, a0, a1} (FUN_00276D30): +0x19 (byte) = a1 (forma) y +0x1A (word) = a0 (traje),
    // los que el combate copia a 0x699322/0x699342. Se conserva la anchura del registro (byte/word); los indices son de
    // B2 y no se traducen a B3 aqui (mapeo sin medir; ver INFORME.md [H]).
    if (o[1] == 52) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        f.forma_b2 = o[4] & 0xFF;
        f.traje_b2 = o[3] & 0xFFFF;
        REXLOG_INFO("dbz3 tablero b2: B2 orden 52: pieza={} forma={} traje={}", o[2], f.forma_b2, f.traje_b2);
      }
      return espera;
    }
    // studies 2 G04: orden 51 {pieza, a} (FUN_00276D60): +0x18 (byte) = a. El combate copia el +0x18 del rival a
    // 0x699365 (COM del rival). Se guarda por ficha y se entrega a pasarela_misiones con b2::ComRivalB2().
    if (o[1] == 51) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        f.com_b2 = o[3] & 0xFF;
        REXLOG_INFO("dbz3 tablero b2: B2 orden 51: pieza={} com={}", o[2], f.com_b2);
      }
      return espera;
    }
    if (o[1] == 50) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        f.mov = o[3];
        f.mov_p = o[4];
        REXLOG_INFO("dbz3 tablero b2: script {}: {} pasa al movimiento {} ({})", k, Nombre(f), f.mov, f.mov_p);
      }
      return espera;
    }
    if (o[1] == 56) {   // modo del Saibaman (FUN_00293d20 -> FUN_00276cd0, b2.c:238336-238344): fila de sprites del #AMA
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        f.modo = o[3];
        REXLOG_INFO("dbz3 tablero b2: script {}: {} pasa al modo de Saibaman {}", k, Nombre(f), f.modo);
      }
      return espera;
    }
    return OrdenEscena(k, o);
  }
}

// Los guiones corren como hilos (ELF 0x295F20): las ordenes con el mismo paso (primer campo) a la vez y el paso siguiente
// cuando acaban las que esperan (orden 7, efectos 28, andar, camara, ventanas). La 44 salta a otro guion sin volver.
struct Hilo {
  int guion = 0;
  size_t i = 0;
  float hasta = 0;
};
std::vector<Hilo> g_hilos;
void Guion(int k, int prof) {
  (void)prof;
  if (k < 0 || k >= int(g_d.guiones.size()) || g_d.guiones[size_t(k)].empty()) return;
  if (g_hilos.size() < 32) g_hilos.push_back({k, 0, g_ahora});
}
bool HilosOcupados() { return !g_hilos.empty(); }

// ---- Escenas, efectos y camara de los guiones (escenas: tras Tex) ----
void AbrirEscena(int letra, int n);
void AbrirMensajeSpx(int orden, int a0);
// Efectos de los guiones (18/27 en una casilla, 19/28 en una pieza: los 29-43 y 59-73 son el Saibaman que brota) y la
// camara (9, 11: a una pieza). ponytail: los efectos #SPX del original (banco uRam00698DC4) no estan portados; aqui la pieza
// sembrada sale de su agujero (DibPieza) y las demas aparecen con un destello.
// Camara de los guiones (ELF: ordenes 9-12 = 0x295BD0.. con paso lineal FUN_0023ACB0, 5 px por cuadro y por eje hasta
// llegar; 13-16 = suave FUN_0023AF30, 1/12 de la distancia por cuadro, durante a1 cuadros). 9/10/13/14 van a la casilla
// a0 (la tabla de 0x34 B de la cabecera +0x24 = T1), 11/12/15/16 a la pieza a0. Se queda alli mientras corren los guiones.
int g_cam_pieza = -1, g_cam_casilla = 0;
bool g_cam_script = false, g_cam_lineal = false;
float g_cam_hasta = -10;
ImVec2 PosFicha(const Ficha& f);
// R7: ordenes 17/20/23 (en x, y del mapa; a3 = cuadros de retraso), 26/29 (igual, y el guion espera a que acabe), 18/27
// (en la casilla a1) y 19/28 (en la pieza a1): efecto a0 del banco #AMA 343+5k (la llegada de las capsulas de Raditz y
// Nappa, el crater, el brote de los Saibaman...), con su animacion y su duracion (FUN_0023B900, pool de 500).
struct Efecto {
  int k = 0;
  float x = 0, y = 0, t0 = 0, dur = 0;
};
std::vector<Efecto> g_efectos;
float PonerEfecto(int k, float x, float y, float retraso) {
  if (k < 0 || k >= int(g_d.textos.size())) return 0;
  const float dur = std::max(1.0f, g_d.textos[size_t(k)].dur) / 60.0f;
  if (g_efectos.size() < 500) g_efectos.push_back({k, x, y, g_ahora + retraso, dur});
  return retraso + dur;
}
void Aparecer(Ficha& f, const char* por) {
  if (!f.oculta) return;
  f.oculta = false;
  RecalcularSlots();
  f.brote_t = g_ahora;
  REXLOG_INFO("dbz3 tablero b2: aparece {} en la casilla {} ({})", Nombre(f), f.casilla, por);
}
float OrdenEscena(int k, const std::array<int, 6>& o) {
  (void)k;
  switch (o[1]) {
    case 4: case 5: case 6:   // FUN_002929A0: #SPX de CMN 450 / 612 / 943 + a0 = ventana de mensaje M/P/T (AbrirMensajeSpx)
      // M11 (mod opcional b2.efectos_spx): las ventanas de mensaje de las ordenes 4/5/6 las ensena el port desde
      // R7/b2/INFORME_shenron.md; el mod solo las enciende o apaga. Sin el mod instalado vale el defecto del port
      // (salen); con el instalado manda su casilla (activado: salen; desactivado: no, se espera su tiempo).
      // (Para que el defecto sin mod sea no verlas: cambiar por `if (!dbz3::opcional::Activo("b2.efectos_spx")) return 0.3f;`.)
      if (dbz3::opcional::Desactivado("b2.efectos_spx")) return 0.3f;
      AbrirMensajeSpx(o[1], o[2]);
      return 0.3f;
    case 7:   // espera (cuadros de 60 Hz)
      return std::clamp(float(o[2]) / 60.0f, 0.0f, 5.0f);
    case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16: {
      const bool pieza = o[1] == 11 || o[1] == 12 || o[1] == 15 || o[1] == 16;
      ImVec2 meta(-1, -1);
      g_cam_pieza = -1;
      if (pieza) {
        for (size_t i = 0; i < g_f.size(); ++i)
          if (g_f[i].n == o[2]) {
            g_cam_pieza = int(i);
            meta = PosFicha(g_f[i]);
          }
      } else if (o[2] > 0 && o[2] < int(g_d.casillas.size())) {
        g_cam_casilla = o[2];
        meta = ImVec2(g_d.casillas[size_t(o[2])].x, g_d.casillas[size_t(o[2])].y);
      }
      if (meta.x < 0) return 0;
      g_cam_script = true;
      g_cam_lineal = o[1] <= 12;
      if (g_cam_lineal)   // espera a llegar: 5 px por cuadro en el eje mas largo
        return std::min(4.0f, std::max(std::fabs(meta.x - g_cam_x), std::fabs(meta.y - g_cam_y)) / 300.0f + 0.05f);
      return std::clamp(float(o[3]) / 60.0f, 0.0f, 5.0f);
    }
    case 17: case 20: case 23: case 26: case 29: {
      const float d = PonerEfecto(o[2], float(o[3]), float(o[4]), float(std::max(0, o[5])) / 60.0f);
      return o[1] == 26 || o[1] == 29 ? std::min(d, 6.0f) : 0;
    }
    // studies 8 (E): 21/24/30 (casilla, FUN_00273AD0) y 22/25/31 (pieza, FUN_0027A790) son las mismas llamadas que
    // 18/27 y 19/28 (scr_ops: FUN_0023B900 + FUN_00245C00 con a0 = efecto del banco uRam00698DC4; 30/31 bloquean como
    // 27/28). 22 sale 2 veces en los 9 guiones; 21/24/25/30/31, ninguna, pero se tratan por si un #YDW las trae.
    case 21: case 24: case 30: {
      float d = 0;
      if (o[3] > 0 && o[3] < int(g_d.casillas.size()))
        d = PonerEfecto(o[2], g_d.casillas[size_t(o[3])].x, g_d.casillas[size_t(o[3])].y, 0);
      return o[1] == 30 ? std::min(d, 6.0f) : 0;
    }
    case 22: case 25: case 31: {
      float d = 0;
      for (Ficha& f : g_f)
        if (f.n == o[3]) d = PonerEfecto(o[2], PosFicha(f).x, PosFicha(f).y, 0);
      return o[1] == 31 ? std::min(d, 6.0f) : 0;
    }
    case 18: case 27: {   // efecto en una casilla (FUN_00295780, b2.c:239192-239214) = el #AMA se dibuja alli (p. ej.,
      // el Saibaman asomando: efectos 29..43 del guion 301). La PIEZA no sale aqui: la saca su orden 32 (FUN_00294ab0 ->
      // FUN_0027a6e0, b2.c:224200-224221, la que pone sus banderas en juego); antes el port la aparecia ya sembrada.
      float d = 0;
      if (o[3] > 0 && o[3] < int(g_d.casillas.size()))
        d = PonerEfecto(o[2], g_d.casillas[size_t(o[3])].x, g_d.casillas[size_t(o[3])].y, 0);
      return o[1] == 27 ? std::min(d, 6.0f) : 0;
    }
    case 19: case 28: {
      float d = 0;
      for (Ficha& f : g_f)
        if (f.n == o[3]) {
          Aparecer(f, "brota");
          d = PonerEfecto(o[2], g_d.casillas[size_t(f.casilla)].x, g_d.casillas[size_t(f.casilla)].y, 0);
        }
      return o[1] == 28 ? std::min(d, 6.0f) : 0;
    }
    case 32:
      for (Ficha& f : g_f)
        if (f.n == o[2]) {
          Aparecer(f, "orden 32");   // la charla de Raditz y Nappa es la orden 4 siguiente del guion 301 (M001 = loc 207)
          // A2b: FUN_0027A6E0 pone el estado 2 (en el tablero) tambien a las piezas de reserva del jugador (T2 +2 = 0): en la
          // fase III Goku y Vegeta salen asi en el guion 301 (medido en PCSX2: estado 0x12 con la lista abierta).
          if (!f.rival && f.fuera) {
            f.fuera = false;
            f.jugador = true;
            f.movs = f.movs_turno = 1;
            f.brote_t = g_ahora;
            RecalcularSlots();
            if (f.n == 1) g_sel = int(&f - g_f.data());
          }
        }
      return 0.2f;
  }
  return 0;
}

// Disparadores de casilla (T3 {A, B, 0, guion, N, banderas}, ELF 0x27F7E0, tras mover la pieza activa, del jugador o
// rival): pieza A (0 = cualquiera) en la casilla B (0 = cualquiera). R7: las filas sin banderas tambien (fase I: una pieza
// del jugador en la casilla 15 de la Bola = guion 303 = orden 41, fase superada; Raditz o Nappa alli = 304, derrota).
void SucesosDeCasilla(const Ficha& f) {
  for (const auto& t : g_d.t3) {
    if (t[2] != 0 || t[0] < 0 || (t[0] != 0 && t[0] != f.n) || (t[1] != 0 && t[1] != f.casilla)) continue;
    if (!Condiciones(t, 5)) continue;
    REXLOG_INFO("dbz3 tablero b2: {} en la casilla {}: script {}", Nombre(f), f.casilla, t[3]);
    Guion(t[3], 0);
    break;
  }
}


void CorrerHilos() {
  struct Fin {   // registro: los guiones acaban (la prueba "tablero" espera a esto para mover)
    ~Fin() {
      if (antes && g_hilos.empty()) REXLOG_INFO("dbz3 tablero b2: guiones terminados (turno {})", g_turno);
    }
    bool antes = !g_hilos.empty();
  } fin;
  for (size_t n = 0; n < g_hilos.size();) {
    if (g_ahora < g_hilos[n].hasta || !g_mensajes.empty() || EscenaAbierta() || g_elegir > 0 || g_premio_id > 0 ||
        g_espera_x) {
      ++n;
      continue;
    }
    Hilo h = g_hilos[n];
    const auto& g = g_d.guiones[size_t(h.guion)];
    if (h.i >= g.size()) {
      g_hilos.erase(g_hilos.begin() + std::ptrdiff_t(n));
      continue;
    }
    const int paso = g[h.i][0];
    float espera = 0;
    for (int vueltas = 0; h.i < g.size() && g[h.i][0] == paso && vueltas < 256; ++vueltas) {
      const std::array<int, 6> o = g[h.i++];
      if (o[1] == 44) {   // salta (no vuelve)
        if (o[2] >= 0 && o[2] < int(g_d.guiones.size())) {
          h.guion = o[2];
          h.i = 0;
        } else {
          h.i = g.size();
        }
        break;
      }
      espera = std::max(espera, Orden(h.guion, o));
      if (g_estado != kJuego) {   // orden 41/42/43: la fase acabo
        g_hilos.clear();
        return;
      }
      if (g_elegir > 0 || g_premio_id > 0 || g_espera_x) break;
    }
    h.hasta = g_ahora + espera;
    g_hilos[n] = h;
    ++n;
  }
}
// Paso de un rival segun su tipo de movimiento (ELF 0x378FE0): 0 quieto, 2 a la casilla mov_p, 3 al azar, 4 (y los
// demas, ponytail) persigue a la ficha del jugador mas cercana.
// R7 (ELF 0x274110, 0x273ED0, 0x273E00, 0x273D60, 0x273C30, 0x27CC30; desensamblado): 1 encara (si hay una ficha del
// jugador en una vecina, va a ella = contacto), 5 sigue a la pieza mov_p (o a la 1 si no esta en juego), 6 va a por la
// ficha contraria en juego con menos barras (+1 del registro, < 10; si no hay, la pieza 1), 7 va al
// objeto no enterrado y sin coger mas cercano (sin ninguno, paso al azar), 8 igual pero solo objetos de tipo mov_p y,
// sin ninguno, persigue a la ficha contraria mas cercana.
const Ficha* PorN(int n) {
  for (const Ficha& f : g_f)
    if (f.n == n) return &f;
  return nullptr;
}
int Marca(int tipo, int desde) {   // FUN_0027CC30: casilla del objeto valido mas cercano, 0 = ninguno
  const std::vector<int> d = Distancias(desde);
  int mejor = 0, cota = 255;
  for (const Objeto& o : g_obj) {
    if (o.hecho || o.enterrado || (tipo >= 0 && o.tipo != tipo)) continue;
    if (d[size_t(o.casilla)] < cota) {
      cota = d[size_t(o.casilla)];
      mejor = o.casilla;
    }
  }
  return mejor;
}
int PasoAlAzar(int desde) {
  const int v = g_d.casillas[size_t(desde)].vec[size_t(std::rand() & 3)];
  return v > 0 ? v : -1;
}
int PasoRival(const Ficha& r) {
  if (r.mov == 0) return -1;
  if (r.mov == 1) {
    for (int v : g_d.casillas[size_t(r.casilla)].vec)
      if (v > 0 && FichaEn(v, false) >= 0) return v;
    return -1;
  }
  if (r.mov == 5) {
    const Ficha* o = PorN(r.mov_p);
    if (!o || !Activa(*o)) o = PorN(1);
    return o ? PasoA(r.casilla, o->casilla) : -1;
  }
  if (r.mov == 6) {
    const Ficha* best = PorN(1);
    int cota = 10;
    for (const Ficha& f : g_f)
      if (Activa(f) && f.jugador && f.barras < cota) {   // +1 del registro = T2 +6, las barras (FUN_002791A0)
        cota = f.barras;
        best = &f;
      }
    return best ? PasoA(r.casilla, best->casilla) : -1;
  }
  if (r.mov == 7 || r.mov == 8) {
    if (const int m = Marca(r.mov == 7 ? -1 : r.mov_p, r.casilla); m > 0) return PasoA(r.casilla, m);
    return r.mov == 7 ? PasoAlAzar(r.casilla) : PasoHacia(r.casilla);
  }
  if (r.mov == 2) return PasoA(r.casilla, r.mov_p);
  if (r.mov == 3) {
    const int v = g_d.casillas[size_t(r.casilla)].vec[size_t(std::chrono::steady_clock::now().time_since_epoch().count() & 3)];
    return v > 0 ? v : -1;
  }
  return PasoHacia(r.casilla);
}

// Fase enemiga como el original: una pieza rival tras otra, en el orden de sus registros (FUN_00279360/00279E30), la camara
// la sigue (0x235930 llama cada cuadro a 0x276FF0 = camara suave hacia ella), 25 cuadros mirandola antes de decidir
// (0x235E70), su paso (0x276B80) y la espera a que acabe de andar; si toca una ficha del jugador, combate y despues sigue
// con la siguiente. Al acabar, turno nuevo del jugador (0x237F70: contador + disparadores del turno).
bool g_fase_rival = false;
size_t g_rival_i = 0;
float g_rival_espera = 0;
int g_rival_lucha = -1;   // ficha del jugador en la casilla a la que entra el rival (combate al llegar)   // lo que tarda el paso del rival que se mueve (giro + andar)
int g_rival_paso = 0;
float g_rival_t = 0;
int g_cam_rival = -1;   // pieza que sigue la camara en la fase enemiga
float g_turno_t = -10;   // empieza el turno del jugador: el rombo entra con su numero y hasta entonces no se juega
void TurnoRival() {
  g_fase_rival = true;
  g_rival_i = 0;
  g_rival_paso = -1;   // 0x235FD0: la fase enemiga arranca pasados 24 cuadros
  g_rival_t = g_ahora;
  for (Ficha& f : g_f)
    if (f.rival && f.c != kSaibaman) f.acciones = 1;
  g_dest = -1;
  REXLOG_INFO("dbz3 tablero b2: fase enemiga del turno {}", g_turno);
}
void TurnoJugador() {
  g_fase_rival = false;
  g_cam_rival = -1;
  ++g_turno;
  g_rombo_t = g_turno_t = g_ahora;   // el rombo (D_TURN) vuelve a entrar con el turno nuevo (0x237F70 espera a que acabe)
  REXLOG_INFO("dbz3 tablero b2: turno {}", g_turno);
  for (const auto& t : g_d.t3)   // disparadores del turno (-1, 0, turno, script)
    if (t[0] == -1 && t[1] == 0 && t[2] == g_turno && Condiciones(t, 5)) Guion(t[3], 0);
  for (Ficha& f : g_f) {
    if (f.jugador) f.movs = f.movs_turno;
    if (f.rival && f.aturdido > 0) --f.aturdido;
  }
  for (size_t i = 0; i < g_f.size(); ++i) {
    if (Activa(g_f[i]) && g_f[i].jugador && g_f[i].c == kGoku) g_sel = int(i);
  }
  g_dest = -1;
}
// Un paso de la fase enemiga (cada cuadro, si no hay guion, escena ni combate pendiente).
void FaseRival() {
  // 0x235E70: 25 cuadros siguiendo al que acaba y 25 mas con el nuevo antes de decidir (0x32 en total)
  constexpr float kMira = 50.0f / 60.0f, kAnda = 0.40f;
  if (g_rival_paso < 0) {
    if (g_ahora - g_rival_t < 24.0f / 60.0f) return;
    g_rival_paso = 0;
  }
  for (;;) {
    if (g_rival_i >= g_f.size()) return TurnoJugador();
    Ficha& r = g_f[g_rival_i];
    // actua el rival en juego con acciones (+2 del registro): 0x278EE0 se las recarga a todos al empezar la fase enemiga
    // menos a los Saibaman (id 0x16), que no actuan (FUN_00279E30 pide estado 2 y +2 != 0)
    if (!Activa(r) || !r.rival || r.c == kSaibaman || r.acciones <= 0) {
      ++g_rival_i;
      continue;
    }
    if (g_rival_paso == 0) {   // 0x235E70: 25 cuadros mas en el anterior y luego la camara va a este
      g_rival_t = g_ahora;
      g_rival_paso = 1;
      g_rival_lucha = -1;
      return;
    }
    if (g_rival_paso == 1) {
      if (g_ahora - g_rival_t >= kMira * 0.5f) g_cam_rival = int(g_rival_i);
      if (g_ahora - g_rival_t < kMira) return;
      const int p = PasoRival(r);
      g_rival_paso = 2;
      g_rival_t = g_ahora;
      g_rival_espera = 0;
      // s9b: el rival que no se mueve tambien pasa por el final de su accion (FUN_00235c30 -> FUN_00236830, caso 6 ->
      // FUN_0027F7E0): los disparadores de casilla (T3) se miran en la casilla en la que se queda. Asi el Cell de la fase V,
      // puesto en la 7 por el guion 303, arranca su patrulla (guion 333) y deja libre el unico paso hacia la 22.
      if (p <= 0) {
        SucesosDeCasilla(r);
        return;
      }
      g_rival_lucha = FichaEn(p, false);   // entra en la casilla y, al llegar, combate
      EmpezarPaso(r, p, g_ahora);
      g_rival_espera = TiempoPaso(r);
      REXLOG_INFO("dbz3 tablero b2: {} a la casilla {}", Nombre(r), p);
      if (g_rival_lucha < 0) SucesosDeCasilla(r);
      return;
    }
    if (g_ahora - g_rival_t < std::max(kAnda, g_rival_espera)) return;
    r.acciones = 0;   // 0x27A180: gasta su accion
    g_rival_paso = 0;
    ++g_rival_i;
    if (g_rival_lucha >= 0) {
      const int j = g_rival_lucha;
      g_rival_lucha = -1;
      g_pend_mov = int(&r - g_f.data());   // se movio el rival: la camara del contacto lo sigue a el
      return Contacto(j, int(&r - g_f.data()));
    }
  }
}
void FinDeAccion() {
  if (g_pend_j >= 0 || g_estado != kJuego || g_fase_rival) return;
  if (!QuedanMovs()) TurnoRival();
  else if (g_f[size_t(g_sel)].movs <= 0) SiguienteSel();
}

int g_cont_j = -1, g_cont_r = -1;   // contacto pendiente: la pieza anda hasta pegarse al rival y entonces sale el guion
float g_cont_t = 0;
void Mover(int i, int dest) {
  Ficha& f = g_f[size_t(i)];
  --f.movs;
  g_dest = -1;
  if (const int r = FichaEn(dest, true); r >= 0) {
    g_pend_mov = i;   // se movio el jugador: la camara del contacto lo sigue a el (FUN_002365D0 b2.c:189388)
    // A2c: en el original la pieza anda hasta pegarse al rival (a ~70 px de el) y SOLO entonces salen el "!!" y el guion de contacto
    const ImVec2 O = PosFicha(f), R = PosFicha(g_f[size_t(r)]);
    const float dx = R.x - O.x, dy = R.y - O.y, d = std::hypot(dx, dy);
    if (d > 71.0f) {
      const float k = (d - 70.0f) / d, dur = IrAPunto(f, 65, O.x + dx * k, O.y + dy * k, g_ahora);
      g_cont_j = i;
      g_cont_r = r;
      g_cont_t = g_ahora + dur + 0.15f;
      return;
    }
    return Contacto(i, r);
  }
  // studies 8 (E): encuentro con un aliado. Los contactos de T4 van por personaje sin mirar el bando (FUN_0027F510) y los
  // #YDW traen filas entre dos piezas del jugador (fase I: {1, 2, 0, 401, .., -101}: Goku con Piccolo -> guion 401 =
  // giro, "攻 +10%" con las ordenes 35 + 83, bandera 101 para que no se repita). [H] que el original lo dispare al pisar la
  // casilla del aliado (la unica forma de que dos piezas del jugador se toquen); la pieza sigue andando como antes.
  if (const int al = FichaEn(dest, false); al >= 0 && al != i) {
    SucesosDeContacto(f, g_f[size_t(al)]);
    g_suceso = -1;   // sin combate: que no lo herede el siguiente
  }
  EmpezarPaso(f, dest, g_ahora);
  g_dest = -1;
  REXLOG_INFO("dbz3 tablero b2: turno {}: {} a la casilla {}", g_turno, Nombre(f), dest);
  const Casilla& c = g_d.casillas[size_t(dest)];
  if (c.tipo == 2) {   // Dende: vida llena y una barra mas (FAQ: "+1")
    f.vida = 0;
    f.barras = std::min(f.barras_max, f.barras + 1);
    Aviso(T::T("Dende te cura.", "Dende heals you."));
  } else if (c.tipo == 3 && c.destino > 0 && FichaEn(c.destino, true) < 0) {   // alfombra de Mr. Popo
    f.desde = c.destino;   // ponytail: aparece alli (sin vuelo)
    f.casilla = c.destino;
    Aviso(T::T("Mr. Popo te lleva en su alfombra.", "Mr. Popo carries you on his carpet."), 16);
    REXLOG_INFO("dbz3 tablero b2: Mr. Popo lleva a {} a la casilla {}", Nombre(f), c.destino);
  }
  for (Objeto& o : g_obj) {
    if (EnSuelo(o) && o.casilla == f.casilla) Coger(f, o);
  }
  SucesosDeCasilla(f);
  FinDeAccion();
}

void Buscar(int i) {
  Ficha& f = g_f[size_t(i)];
  if (g_buscada[size_t(f.casilla)]) return Aviso(T::T("Aquí ya has buscado.", "You already searched here."));
  --f.movs;
  g_dest = -1;   // sale del modo destino: la camara vuelve a la ficha (FUN_00236AD0 b2.c:189566)
  g_marca = f.casilla;
  g_marca_icono = 7;   // "?!"
  g_marca_t = g_ahora;
  SonidoEventoB2("buscar", f.movs > 0 ? 8 : 20);   // FUN_00277BB0 (b2.c:222476-222495); el 20/9 lo elige +0x69b7a4 [H]
  REXLOG_INFO("dbz3 tablero b2: turno {}: {} busca en la casilla {}", g_turno, Nombre(f), f.casilla);
  for (size_t k = 0; k < g_f.size(); ++k) {
    Ficha& s = g_f[k];
    if (s.oculta && !s.fuera && s.casilla == f.casilla) {
      s.oculta = false;
      s.rival = true;
      g_pend_mov = i;   // el que se movio (buscar) es el que sigue la camara del contacto
      return Contacto(i, int(k));
    }
  }
  // una busqueda = el primer enterrado que queda (ELF 0x27C090); sin mas, la casilla queda buscada (en gris)
  auto queda = [&](const Objeto& o) { return o.casilla == f.casilla && Escondido(o); };
  if (auto it = std::find_if(g_obj.begin(), g_obj.end(), queda); it != g_obj.end()) Coger(f, *it);
  else Aviso(T::T("No hay nada.", "Nothing here."));
  g_buscada[size_t(f.casilla)] = std::none_of(g_obj.begin(), g_obj.end(), queda);
  FinDeAccion();
}

// s9b (solo para la prueba automatica): disparadores de casilla (T3) que ahora valen para la pieza n. Una "trampa" saca a
// la pieza del tablero o la recoloca (ordenes 33 / 55 / 57 de su propio guion: fase V, casilla 12 sin la bandera 305 =
// guion 312, vuelve a la 24); un "final" acaba la fase (ordenes 41 / 42, siguiendo los saltos 44: fase V, casilla 22 con
// la 305). El automatico las evita / las busca; sin esto la fase V daba vueltas hasta agotar la prueba.
bool GuionHace(int k, int n, int prof, bool final_) {
  if (k < 0 || k >= int(g_d.guiones.size()) || prof > 4) return false;
  for (const auto& o : g_d.guiones[size_t(k)]) {
    if (final_ ? (o[1] == 41 || o[1] == 42) : ((o[1] == 33 || o[1] == 55 || o[1] == 57) && o[2] == n)) return true;
    if (o[1] == 44 && GuionHace(o[2], n, prof + 1, final_)) return true;
  }
  return false;
}
// s9b: el combate de `a` contra el rival `r`, si lo gana, acaba en un guion que PIERDE la fase (orden 43)? (fase V: eliminar al
// tercer subordinado con las banderas 301 y 302 = guion 306, Majin Bu). El automatico no da ese golpe.
bool GuionPierde(int k, int prof) {
  if (k < 0 || k >= int(g_d.guiones.size()) || prof > 4) return false;
  for (const auto& o : g_d.guiones[size_t(k)]) {
    if (o[1] == 43) return true;
    if (o[1] == 44 && GuionPierde(o[2], prof + 1)) return true;
  }
  return false;
}
// Banderas cuyo conjunto hace perder: las de las condiciones de los sucesos de combate (T5) que acaban en un guion con orden 43.
// Un contacto "cuesta" si su guion enciende una de ellas (fase V: 301 y 302; el tercer contacto con un subordinado pierde).
// El lider (Goku) no acepta ese coste (los de los aliados solo cuentan al eliminar, T5 con codigo 4: que peleen ellos) y los
// aliados lo aceptan mientras quede otra bandera sin encender.
bool GuionEnciende(int k, const std::vector<int>& L, int prof) {
  if (k < 0 || k >= int(g_d.guiones.size()) || prof > 4) return false;
  for (const auto& o : g_d.guiones[size_t(k)]) {
    if (o[1] == 1 && std::find(L.begin(), L.end(), o[2]) != L.end() && !Bandera(o[2])) return true;
    if (o[1] == 44 && GuionEnciende(o[2], L, prof + 1)) return true;
  }
  return false;
}
bool Peligroso(const Ficha& a, const Ficha& r) {
  int suceso = -1;
  for (size_t i = 1; i < g_d.t4.size() && suceso < 0; ++i) {   // la misma busqueda que SucesosDeContacto
    const auto& t = g_d.t4[i];
    const int ca = t[0] ? PersonajeDe(t[0]) : -1, cb = t[1] ? PersonajeDe(t[1]) : -1;
    bool vale;
    if (ca < 0 && cb < 0) vale = false;
    else if (cb < 0) vale = ca == a.c || ca == r.c;
    else if (ca < 0) vale = cb == a.c || cb == r.c;
    else vale = (ca == a.c && cb == r.c) || (ca == r.c && cb == a.c);
    if (vale && (t[2] == 0 || t[2] == a.casilla || t[2] == r.casilla) && Condiciones(t, 7)) suceso = int(i);
  }
  if (suceso < 0) return false;
  const int cod = r.barras - 1 <= 0 ? 4 : 1;   // gana el jugador, con o sin eliminar al rival
  int guion = -1;
  for (const auto& t : g_d.t5)
    if (t[0] == suceso && (t[1] == 5 || t[1] == cod) && Condiciones(t, 4)) {
      guion = t[2];
      break;
    }
  if (guion < 0) return false;
  static std::string ultimo;   // s9b: traza (solo cuando cambia) de por que un contacto se da por peligroso
  const std::string clave = std::to_string(a.n) + ">" + std::to_string(r.n) + ":" + std::to_string(suceso) + "/" + std::to_string(guion) + "/" +
                            std::to_string(Bandera(301)) + std::to_string(Bandera(302));
  if (GuionPierde(guion, 0)) {
    if (ultimo != clave) REXLOG_INFO("dbz3 tablero b2: peligro: {} contra {}: suceso {} cod {} -> guion {} PIERDE", Nombre(a), Nombre(r), suceso, cod, guion);
    ultimo = clave;
    return true;
  }
  std::vector<int> L;   // banderas que juntas hacen perder
  for (const auto& t : g_d.t5)
    if (GuionPierde(t[2], 0))
      for (size_t k = 4; k < t.size(); ++k)
        if (t[k] > 0 && std::find(L.begin(), L.end(), t[k]) == L.end()) L.push_back(t[k]);
  if (L.empty() || !GuionEnciende(guion, L, 0)) return false;
  int sin_encender = 0;
  for (int b : L) sin_encender += !Bandera(b);
  const bool peligro = (a.c == kGoku && a.bando == 4) || sin_encender <= 1;
  if (peligro && ultimo != clave) REXLOG_INFO("dbz3 tablero b2: peligro: {} contra {}: suceso {} cod {} -> guion {} enciende banderas de derrota (sin encender {})", Nombre(a), Nombre(r), suceso, cod, guion, sin_encender);
  if (peligro) ultimo = clave;
  return peligro;
}
bool DisparadorDeCasilla(int c, const Ficha& f, bool final_) {
  for (const auto& t : g_d.t3) {
    if (t[2] != 0 || t[0] < 0 || (t[0] != 0 && t[0] != f.n) || t[1] != c || !Condiciones(t, 5)) continue;
    return GuionHace(t[3], f.n, 0, final_);   // el primero que vale es el que corre (SucesosDeCasilla)
  }
  return false;
}

// Prueba (auto): la ficha elegida del jugador va primero a la Bola de la fase (y la busca si esta enterrada), despues al
// jefe (por su casilla) y, si no se puede llegar, al rival mas cercano que pueda luchar en B3 (los que solo son de B2 se
// saltan).
void Auto() {
  if (g_estado == kPrepara) return Empezar();
  if (g_estado != kJuego || g_pend_j >= 0 || g_fase_rival || g_elegir > 0) return;
  Ficha& f = g_f[size_t(g_sel)];
  if (!f.jugador || f.movs <= 0 || B2aB3(f.c) < 0) {
    f.movs = 0;
    return FinDeAccion();
  }
  // PRUEBA: DBZ3_TEST_B2_JEFE=1 va antes al jefe que a la Bola (para ver los guiones de vencerlo: orden 70)
  static const bool jefe_antes = std::getenv("DBZ3_TEST_B2_JEFE") != nullptr;
  const bool bola = !g_bolas_fase.empty() && !(jefe_antes && g_jefe >= 0 && Activa(g_f[size_t(g_jefe)]));
  if (bola && BolaEn(f.casilla) >= 0 && !g_buscada[size_t(f.casilla)]) return Buscar(g_sel);
  const bool jefe = g_jefe >= 0 && Activa(g_f[size_t(g_jefe)]) && B2aB3(g_f[size_t(g_jefe)].c) >= 0;
  // primer paso del camino mas corto hasta la primera casilla que cumple `meta` (-1 si no hay)
  auto paso = [&](auto meta) {
    const size_t n = g_d.casillas.size();
    std::vector<int> padre(n, -1);
    std::vector<int> cola{f.casilla};
    padre[size_t(f.casilla)] = f.casilla;
    for (size_t q = 0; q < cola.size(); ++q) {
      const int c = cola[q];
      if (c != f.casilla && meta(c)) {
        int p = c;
        while (padre[size_t(p)] != f.casilla) p = padre[size_t(p)];
        return p;
      }
      // s9b: pisar la alfombra de Mr. Popo (tipo 3) te lleva a su destino (Mover): desde ella solo se sigue por ahi. Sin esto el
      // camino mas corto de la fase IX pasaba por la 26 y volvia a la 11 para siempre.
      std::vector<int> alfombra;
      const Casilla& cc = g_d.casillas[size_t(c)];
      if (c != f.casilla && cc.tipo == 3 && cc.destino > 0 && FichaEn(cc.destino, true) < 0) alfombra.push_back(cc.destino);
      for (int v : alfombra.empty() ? std::vector<int>(std::begin(cc.vec), std::end(cc.vec)) : alfombra) {
        if (v <= 0 || padre[size_t(v)] >= 0 || DisparadorDeCasilla(v, f, false)) continue;
        // un rival que solo es de B2 (Babidi en la fase V, inalcanzable como en el original) tapa su casilla
        if (const int r = FichaEn(v, true); r >= 0 && (B2aB3(g_f[size_t(r)].c) < 0 || Peligroso(f, g_f[size_t(r)]))) continue;
        padre[size_t(v)] = c;
        cola.push_back(v);
      }
    }
    return -1;
  };
  int p = -1;
  // s9b: una casilla que acaba la fase para esta pieza (y cuyas banderas ya valen) va antes que nada
  if (!jefe_antes) p = paso([&](int c) { return DisparadorDeCasilla(c, f, true); });
  // s9b: los aliados despejan el camino del lider: el rival visible que tapa su ruta (sin contar rivales) hasta la casilla final
  const bool lider = f.c == kGoku && f.bando == 4;
  if (p < 0 && !lider && !jefe_antes) {
    const Ficha* gk = nullptr;
    for (const Ficha& x : g_f)
      if (Activa(x) && x.jugador && x.c == kGoku && x.bando == 4) gk = &x;
    int bloqueo = -1;
    if (gk) {
      const size_t n = g_d.casillas.size();
      std::vector<int> padre(n, -1), cola{gk->casilla};
      padre[size_t(gk->casilla)] = gk->casilla;
      int meta = -1;
      for (size_t q = 0; q < cola.size() && meta < 0; ++q) {
        const int c = cola[q];
        if (c != gk->casilla && DisparadorDeCasilla(c, *gk, true)) meta = c;
        for (int v : g_d.casillas[size_t(c)].vec)
          if (v > 0 && padre[size_t(v)] < 0 && !DisparadorDeCasilla(v, *gk, false)) {
            padre[size_t(v)] = c;
            cola.push_back(v);
          }
      }
      for (int c = meta; c > 0 && c != gk->casilla; c = padre[size_t(c)]) {
        const int r = FichaEn(c, true);
        if (r >= 0 && B2aB3(g_f[size_t(r)].c) >= 0) bloqueo = c;   // el mas cercano al lider queda al final
      }
    }
    if (bloqueo > 0) p = paso([&](int c) { return c == bloqueo; });
  }
  if (p < 0 && bola) p = paso([](int c) { return BolaEn(c) >= 0 && !g_buscada[size_t(c)]; });
  if (p < 0 && jefe) p = paso([](int c) { return c == g_f[size_t(g_jefe)].casilla; });
  if (p < 0) p = paso([&](int c) {
    const int r = FichaEn(c, true);
    return r >= 0 && B2aB3(g_f[size_t(r)].c) >= 0 && !Peligroso(f, g_f[size_t(r)]);
  });
  // s9: sin Bola, jefe ni rival visible, el original obliga a BUSCAR: los rivales escondidos (Ginyu, Freezer en la fase 2)
  // solo salen al buscar en su casilla. Antes el automatico se quedaba quieto para siempre (128 turnos en la fase 2).
  if (p < 0) {
    // s9b: primero los escondidos de bando 6 (el "ayudante": Kaio-shin de la fase V, que abre el camino con su derrota); los
    // demas despues. Buscar antes al subordinado mas cercano llevaba a eliminar a los tres y perder la fase V (guion 306).
    for (int pasada = 0; pasada < 2 && p < 0; ++pasada) {
      auto escondido = [&](int c) {
        if (g_buscada[size_t(c)]) return false;
        for (const Ficha& x : g_f)
          if (!x.jugador && x.oculta && !x.fuera && x.casilla == c && !Peligroso(f, x) && (pasada == 1 || x.bando == 6))
            return true;
        return false;
      };
      if (escondido(f.casilla)) return Buscar(g_sel);
      p = paso(escondido);
    }
    if (p < 0) {
      std::string e;   // s9b: estado para saber por que (rivales y si son peligrosos para esta pieza)
      for (const Ficha& x : g_f)
        if (!x.fuera && (x.rival || x.oculta)) e += " " + Nombre(x) + "@" + std::to_string(x.casilla) + (x.oculta ? "(oculto)" : "") +
                                                 (x.rival && !x.oculta && Peligroso(f, x) ? "(peligro)" : "");
      REXLOG_INFO("dbz3 tablero b2: automatico: {} en {} no tiene a donde ir;{}", Nombre(f), f.casilla, e);
    }
  }
  if (p > 0) return Mover(g_sel, p);
  f.movs = 0;
  FinDeAccion();
}

// ---- Dibujo ----
struct Tex {
  std::unique_ptr<rex::ui::ImmediateTexture> t;
  float w = 0, h = 0;
  ImTextureID id() const { return reinterpret_cast<ImTextureID>(t.get()); }
  explicit operator bool() const { return t != nullptr; }
};
std::vector<Tex> g_tmapa, g_tfichas, g_ticonos, g_tadornos, g_tloc, g_thud;
Tex g_tgo, g_tgoku, g_thoyo, g_tcabezas;
// S1-F07.2: cierre intermitente al empezar el tablero (0xC0000005 en D3D12Core+0x8A119, RSI = texto "t\mods\o..."):
// la escena (T155, M207...) se cerraba A MITAD de cuadro (`g_esc = Escena{}` tras sus AddImage) y destruia sus
// ImmediateTexture; ImGui pintaba despues ese cuadro con el puntero ya liberado (D3D12ImmediateDrawer::Draw lee
// texture->resource() de memoria reutilizada -> CreateShaderResourceView con basura). Las texturas que se sueltan
// durante el dibujo van a esta papelera y se destruyen 3 cuadros despues, cuando ningun cuadro pendiente las usa.
std::vector<std::pair<uint64_t, std::vector<Tex>>> g_papelera;
uint64_t g_cuadro_b2 = 0;
void Diferir(std::vector<Tex>&& v) {
  if (!v.empty()) g_papelera.emplace_back(g_cuadro_b2, std::move(v));
}
void VaciarPapelera() {   // al principio de cada cuadro del tablero
  ++g_cuadro_b2;
  std::erase_if(g_papelera, [](const auto& p) { return g_cuadro_b2 - p.first >= 3; });
}
// ---- Escenas, efectos y camara de los guiones ----
// Escenas de texto de DATA_<idioma>.AFS del jugador (comprobado con la ISO europea): 155 = la intro del Mundo Dragon
// (narracion, Shenron, el mapa y Goku), 207 = la charla de Nappa y Raditz al llegar (5 lineas). Las ordenes 4/5/6 NO son
// escenas: FUN_002929A0 carga un efecto #SPX de DATA_CMN (943 / 612 / 450 + id). ponytail: las charlas de las demas fases
// (visor de sucesos, orden 41, FUN_00270A50) siguen sin su tabla; aqui solo las dos comprobadas.
struct Escena {
  bool abierta = false, teatro = false, comic = false, bustos = false;   // comic: panel de las ventanas P/T (CMN 793 + 791)
  std::vector<Tex> img;
  std::vector<int> textos, fondos;   // indices de img: lineas de texto y fondos (teatro)
  size_t paso = 0;
  float t0 = 0;
  int numero = 0;                 // entrada de DATA_<idioma>
  std::vector<int> hablan;        // charla: ID de B2 de quien dice cada linea (-1 = sin cara)
} g_esc;
bool EscenaAbierta() { return g_esc.abierta; }

void AbrirEscena(int letra, int n) {
  auto* dr = dbz3::launcher::PreviewDrawer();
  AfsPs2 loc;
  if (!dr || n <= 0 || g_iso_b2.empty() || !loc.Abrir(g_iso_b2, g_loc_afs)) return;
  const std::vector<uint8_t> e = loc.Entrada(size_t(n));
  const int cuantas = CuantasAmt(e);
  if (cuantas <= 0) {
    REXLOG_INFO("dbz3 tablero b2: escena {}{}: no es un #AMT (sin poner)", char(letra), n);
    return;
  }
  Escena esc;
  bool solo_lineas = true;
  for (int j = 0; j < std::min(cuantas, 64); ++j) {
    const Imagen im = TexturaAmt(e, j);
    Tex x;
    if (im) {
      x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
      x.w = float(im.w);
      x.h = float(im.h);
      size_t opacos = 0;
      for (size_t q = 3; q < im.rgba.size(); q += 4) opacos += im.rgba[q] > 100;
      const float cubre = float(opacos) / float(std::max(1, im.w * im.h));
      // textos: anchos y casi transparentes (letras); fondos: grandes y llenos (Shenron, el mapa, Goku). Las cintas,
      // el logo y las Bolas (anchos pero bajos, o pequenos) se quedan fuera.
      if (im.w >= 300 && im.h >= 40 && cubre < 0.45f) esc.textos.push_back(j);
      else if (im.w * im.h >= 100000 && cubre >= 0.4f) esc.fondos.push_back(j);
      if (im.h > 130 || im.w > 520) solo_lineas = false;
    }
    esc.img.push_back(std::move(x));
  }
  // A2b: los mensajes P/T cortos ("Gikhikhi!", 216x43) no llegan a los 300 px de ancho de una linea de charla: sin ninguna,
  // todas las imagenes son sus lineas, en su orden (323: "Gikhikhi!" y la respuesta de Goku; antes la escena no salia y el combate arrancaba sin ella).
  if (esc.textos.empty() && solo_lineas)
    for (size_t j = 0; j < esc.img.size() && esc.textos.empty(); ++j)   // solo la primera: la P de la fase IV muestra "Gikhikhi!" y no la respuesta
      if (esc.img[j]) esc.textos.push_back(int(j));
  esc.teatro = !esc.fondos.empty() && !esc.textos.empty();
  esc.comic = !esc.teatro && (letra == 'P' || letra == 'T');
  esc.bustos = esc.comic && letra == 'P';   // las P llevan a Goku y al rival en 3D (sin portar el rival)
  if (!esc.teatro && !solo_lineas) {
    REXLOG_INFO("dbz3 tablero b2: escena {}{}: {} imagenes que no son una charla (sin poner)", char(letra), n, cuantas);
    return;
  }
  if (esc.textos.empty()) return;
  esc.abierta = true;
  esc.t0 = g_ahora;
  esc.numero = n;
  // quien habla en la charla de la llegada (capturas del original: Raditz, Nappa, Nappa, Nappa, Nappa, Raditz)
  if (n == 207) esc.hablan = {9, 10, 10, 10, 10, 9};   // ID de B2: 9 Raditz, 10 Nappa
  REXLOG_INFO("dbz3 tablero b2: escena {}{}: {} ({} lineas, {} fondos)", char(letra), n, esc.teatro ? "teatro" : "charla",
              esc.textos.size(), esc.fondos.size());
  Diferir(std::move(g_esc.img));   // S1-F07.2: la escena anterior pudo dibujarse en este mismo cuadro
  g_esc = std::move(esc);
}

// Ordenes 4/5/6 (FUN_002929A0, a1 = 'M'/'P'/'T'): corren el #SPX de DATA_CMN 450/612/943 + a0, que es la ventana de
// mensaje de la escena M###/P###/T### (rutas /SCR/P_MSG/M001MSG.AMT, voz P_ADX/M001_00.adx). Del bytecode (VM del 05,
// medido 2026-10-18 en el emulador: M001 = CMN 451 = la charla de Raditz y Nappa) se sacan:
//  - el texto: `push #id; push #1; mov #343, r2; builtin r2` = builtin 0x157 (FUN_002249A0) de (1 << 16 | id) = la
//    entrada id del AFS de idioma (#AMT, una imagen por linea; M001 -> 207, M002 -> 208, M155 -> 281, T001 -> 1160);
//  - quien dice cada linea (solo M; en las P ese `switch` vale 0/1 por linea, el "motion id" de su escena 3D, no una
//    cara: van sin cara): el `switch` del guion `push #linea ... jz; push #cara; pop r0; mov r0, [r13+4]`
//    con la cara = ID de B2 de la hoja T_CHARA (CMN 1196): M001 = {9, 10, 10, 10, 10, 9}, M002 = {10}.
// ponytail: sin las voces (ADX_US.AFS, falta su tabla), sin las animaciones de la ventana (#AMA 790) y sin los modelos
// 3D de las escenas P ("motion id"); solo el texto con su cara.
void AbrirMensajeSpx(int orden, int a0) {
  static std::map<int, std::pair<int, std::vector<int>>> memo;   // CMN -> (texto, caras)
  const int id = (orden == 4 ? 450 : orden == 5 ? 612 : 943) + a0;
  auto it = memo.find(id);
  if (it == memo.end()) {
    std::pair<int, std::vector<int>> r{-1, {}};
    AfsPs2 cmn;
    std::vector<uint8_t> d;
    if (cmn.Abrir(BuscarJuego("budokai 2"), "DATA_CMN.AFS")) d = cmn.Entrada(size_t(id));
    if (d.size() > 0x24 && std::memcmp(d.data(), "#SPX", 4) == 0) {
      static const uint8_t txt[] = {0x08, 0x10, 0x01, 0x01, 0x20, 0x57, 0x01, 0x02, 0x80, 0x02};   // push #1; builtin 0x157
      static const uint8_t sw[] = {0x0a, 0x01, 0x0a, 0x00, 0x13, 0x01, 0x00, 0x51, 0x00, 0x33, 0x00, 0x08, 0x00, 0x0a,
                                   0x00, 0x01, 0x30};   // ...linea: comparacion y salto (i32) a la siguiente
      for (size_t i = 4; i + sizeof(txt) <= d.size() && r.first < 0; ++i)
        if (d[i - 4] == 0x08 && d[i - 3] == 0x20 && std::memcmp(&d[i], txt, sizeof(txt)) == 0)
          r.first = d[i - 2] | d[i - 1] << 8;   // push #id (i16)
      for (size_t i = 3; orden == 4 && i + sizeof(sw) + 14 <= d.size(); ++i) {
        if (d[i - 3] != 0x08 || d[i - 2] != 0x10 || std::memcmp(&d[i], sw, sizeof(sw)) != 0) continue;
        const uint8_t* c = &d[i + sizeof(sw) + 4];   // jz r0, r2; push #cara; pop r0
        if (c[0] == 0x02 && c[1] == 0x72 && c[4] == 0x08 && c[5] == 0x10 && c[7] == 0x0a && d[i - 1] == r.second.size())
          r.second.push_back(c[6]);
      }
    }
    it = memo.emplace(id, std::move(r)).first;
  }
  REXLOG_INFO("dbz3 tablero b2: orden {} ({}): #SPX CMN {} -> mensaje loc {}, {} caras", orden, a0, id, it->second.first,
              it->second.second.size());
  if (it->second.first <= 0) return;
  AbrirEscena("MPT"[orden - 4], it->second.first);
  if (g_esc.abierta && g_esc.numero == it->second.first) g_esc.hablan = it->second.second;
}


// Cabeza de B2 del personaje `c` (ID de B2): CMN 1196, 55 de 136x136 en el orden de los ID (comprobado con la hoja).
ImTextureID CabezaB2(int c) {
  static std::vector<Tex> cabezas;
  static bool leidas = false;
  if (!leidas) {
    auto* dr = dbz3::launcher::PreviewDrawer();
    AfsPs2 cmn;
    if (!dr) return ImTextureID{};
    leidas = true;
    if (cmn.Abrir(BuscarJuego("budokai 2"), "DATA_CMN.AFS")) {
      const std::vector<uint8_t> en = cmn.Entrada(1196);
      for (int j = 0; j < std::min(CuantasAmt(en), 64); ++j) {
        const Imagen im = TexturaAmt(en, j);
        Tex x;
        if (im && im.w > 16) {
          x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
          x.w = float(im.w);
          x.h = float(im.h);
        }
        cabezas.push_back(std::move(x));
      }
    }
  }
  return c >= 0 && c < int(cabezas.size()) && cabezas[size_t(c)] ? cabezas[size_t(c)].id() : ImTextureID{};
}
bool g_esc_auto = false;   // modo automatico del tablero (DibujarTablero): las ventanas avanzan solas
bool DibGoku3DTarjeta(ImDrawList* dl, ImVec2 pie, float alto_px, float t, float giro);
ImVec2 g_foco_scr(0, 0);   // pantalla (antes del estiramiento) de la casilla del contacto
bool g_foco_ok = false;
int g_v_ini = 0;   // primer vertice del tablero en el ImDrawList del cuadro (Dibujar)
// Texturas de una entrada #AMT de DATA_CMN (CMN 793 = fondo comico y logo, 791 = emblema, caja, nube, logo pequeno).
ImTextureID CmnTex(int entrada, int j, ImVec2* tam = nullptr) {
  static std::map<int, std::vector<Tex>> cache;
  auto it = cache.find(entrada);
  if (it == cache.end()) {
    std::vector<Tex> v;
    auto* dr = dbz3::launcher::PreviewDrawer();
    AfsPs2 cmn;
    if (dr && !g_iso_b2.empty() && cmn.Abrir(g_iso_b2, "DATA_CMN.AFS")) {
      const std::vector<uint8_t> en = cmn.Entrada(size_t(entrada));
      for (int k = 0; k < std::min(CuantasAmt(en), 16); ++k) {
        const Imagen im = TexturaAmt(en, k);
        Tex x;
        if (im && im.w > 8) {
          x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
          x.w = float(im.w);
          x.h = float(im.h);
        }
        v.push_back(std::move(x));
      }
    }
    it = cache.emplace(entrada, std::move(v)).first;
  }
  if (j < 0 || j >= int(it->second.size()) || !it->second[size_t(j)]) return ImTextureID{};
  if (tam) *tam = ImVec2(it->second[size_t(j)].w, it->second[size_t(j)].h);
  return it->second[size_t(j)].id();
}
void DibEscena(ImDrawList* dl, const S::Area& a, float t, uint16_t& pressed) {
  if (!g_esc.abierta) return;
  Escena& e = g_esc;
  const ImVec2 W = ImGui::GetIO().DisplaySize;
  const float desde = t - e.t0;
  // lienzo de 640x448 del original, centrado en el area
  const float ls = std::min(1200.0f / 640.0f, 900.0f / 448.0f) * a.s;
  const ImVec2 lo(a.p0.x + (1200.0f * a.s - 640.0f * ls) * 0.5f, a.p0.y + (900.0f * a.s - 448.0f * ls) * 0.5f);
  auto P = [&](float x, float y) { return ImVec2(lo.x + x * ls, lo.y + y * ls); };
  auto img = [&](int j, float x, float y, float w, float h, ImU32 col = IM_COL32_WHITE) {
    if (j < 0 || j >= int(e.img.size()) || !e.img[size_t(j)]) return;
    dl->AddImage(e.img[size_t(j)].id(), P(x, y), P(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), col);
  };
  const float alfa = std::clamp(desde * 2.0f, 0.0f, 1.0f);
  const ImU32 blanco = IM_COL32(255, 255, 255, int(255 * alfa));
  if (e.teatro && e.numero == 155 && e.img.size() >= 18) {
    // La intro del Mundo Dragon como en B2 (capturas del original): fondo de tablones (1, 640x80 en mosaico), la cinta
    // morada arriba (0) con "DRAGON" (4) y "BALL Z" (5), un cuadro con marco de madera con Shenron (9) y las Bolas (6-8)
    // flotando, o el mapa (10) con Goku (12) en la nube (13) y su estela; el ultimo texto con el retrato de Goku (11).
    dl->AddRectFilled(ImVec2(0, 0), W, IM_COL32(96, 150, 150, 255));
    for (float y = 0; y < 448; y += 80) img(1, 0, y, 640, 80);
    const size_t k = e.paso;   // 0-1 Shenron, 2 el mapa con Goku volando, 3 el retrato de Goku
    const int fondo = k < 2 ? 9 : 10;
    const float fx = 40, fy = 92, fw = 560, fh = 288;
    dl->AddRectFilled(P(fx - 8, fy - 8), P(fx + fw + 8, fy + fh + 8), IM_COL32(120, 72, 30, 255), 3 * ls);
    dl->AddRectFilled(P(fx - 4, fy - 4), P(fx + fw + 4, fy + fh + 4), IM_COL32(176, 112, 48, 255), 2 * ls);
    dl->AddRectFilled(P(fx, fy), P(fx + fw, fy + fh), IM_COL32(18, 22, 12, 255));   // Shenron va sobre negro
    img(fondo, fx, fy, fw, fh);
    if (k < 2) {   // las Bolas flotan sobre Shenron
      // las tres tiras de Bolas (6-8) con su forma, repartidas por el cuadro
      const float b[3][2] = {{60, 112}, {400, 300}, {470, 140}};
      for (int q = 0; q < 3; ++q) {
        const Tex& x = e.img[size_t(6 + q)];
        const float d = std::sin(t * 1.3f + float(q)) * 6.0f;
        if (x) img(6 + q, b[q][0], b[q][1] + d, x.w * 0.8f, x.h * 0.8f, blanco);
      }
    } else if (k == 2) {   // Goku cruza el mapa en la nube dejando su estela
      const float u = std::fmod(desde * 0.18f, 1.0f), gx = fx + 30 + u * 420, gy = fy + 120;
      dl->AddRectFilled(P(fx, gy + 52), P(gx + 20, gy + 58), IM_COL32(255, 236, 120, 200));
      img(13, gx, gy + 30, 112, 80);
      img(12, gx + 26, gy - 30, 64, 88);
    }
    img(0, 0, 0, 640, 104);
    img(4, 48, 6, 288, 80);
    img(5, 346, 6, 264, 80);
    if (k >= 3) img(11, -20, 160, 336, 400);
    if (k < e.textos.size()) {
      const Tex& x = e.img[size_t(e.textos[k])];
      const float esc = std::min(1.0f, std::min(500.0f / x.w, 260.0f / x.h)), cx = k >= 3 ? 400 : 320;
      if (x) dl->AddImage(x.id(), P(cx - x.w * esc * 0.5f, fy + fh * 0.5f - x.h * esc * 0.5f),
                          P(cx + x.w * esc * 0.5f, fy + fh * 0.5f + x.h * esc * 0.5f), ImVec2(0, 0), ImVec2(1, 1), blanco);
    }
  } else if (e.comic) {
    // A2c: panel comico de las ventanas P/T (medido sobre original_fase9_p2.png, lienzo 640x448): fondo CMN 793 #3 (tablon con
    // llama y recuadro morado, hueco transparente), logo verde 793 #1, el tablero en el recuadro con la pieza del contacto en su
    // centro, emblema 791 #0 con la cabeza del que habla (CMN 1196), logo 791 #2, nubes 791 #4, caja blanca 791 #3, ":Next".
    // ponytail: las P (fase IV) llevan en el original a Goku y al rival en 3D (CMN 74-317, sin portar): aqui, solo el emblema.
    const ImVec2 mover = g_foco_ok ? ImVec2(P(405, 158).x - g_foco_scr.x, P(405, 158).y - g_foco_scr.y) : ImVec2(85.0f * ls, -66.0f * ls);
    // el tablero ya dibujado, ampliado 1,25 (medido: el Saibaman del recuadro mide 50 px frente a 40) alrededor del contacto, que
    // queda en el centro del recuadro
    const ImVec2 fo = g_foco_ok ? g_foco_scr : ImVec2(P(320, 224).x, P(320, 224).y), fc(fo.x + mover.x, fo.y + mover.y);
    constexpr float kZoomPanel = 1.25f;
    for (int i = g_v_ini; i < dl->VtxBuffer.Size; ++i) {
      dl->VtxBuffer[i].pos.x = fc.x + (dl->VtxBuffer[i].pos.x - fo.x) * kZoomPanel;
      dl->VtxBuffer[i].pos.y = fc.y + (dl->VtxBuffer[i].pos.y - fo.y) * kZoomPanel;
    }
    auto tx = [&](int ent, int j, float x0, float y0, float x1, float y1, ImU32 col = IM_COL32_WHITE) {
      if (const ImTextureID id = CmnTex(ent, j)) dl->AddImage(id, P(x0, y0), P(x1, y1), ImVec2(0, 0), ImVec2(1, 1), col);
    };
    tx(793, 3, 0, 0, 640, 448);
    tx(793, 1, -22, -20, 362, 132);   // tamano nativo 384x152 (medido contra la captura)
    if (e.bustos) {   // P: Goku grande a la izquierda (el modelo de la tarjeta), de cara al rival
      dl->PushClipRect(P(0, 0), P(640, 448), true);
      const int ij = g_pend_j >= 0 && g_pend_j < int(g_f.size()) ? g_f[size_t(g_pend_j)].c : 0;
      const int ir = g_pend_r >= 0 && g_pend_r < int(g_f.size()) ? g_f[size_t(g_pend_r)].c : -1;
      static const float gp = std::getenv("DBZ3_TEST_FIG_GIRO_P") ? float(std::atof(std::getenv("DBZ3_TEST_FIG_GIRO_P"))) : 0.5f;
      (void)ij;
      (void)gp;
      // A1c: Goku y el rival con la CAMARA de B2 (medido en PCSX2, fase IV p2: objetos de camara en 0x691490 / 0x691540): Goku = #AMC
      // 826 clip 2, rival Saibaman = #AMC 843 clip 0 (ojo, objetivo, fov 0,6109); centro de proyeccion = (320, 224) + (-240, 100) y
      // + (100, -10) en el lienzo de 640x448; los modelos van en el mundo a escala 1 (medido: Saibaman en (-0,05, -0,12, 0); Goku (0,25, -0,54, 0) girado -0,25 rad; cuadros: Goku 0 en la
      // 1a linea, Saibaman ~34 y en bucle).
      // ponytail: la camara del rival no Saibaman es la de 843 [H] (cada banco de historia trae su #AMC en banco - 1).
      static const float kEsc = std::getenv("DBZ3_TEST_CB_S") ? float(std::atof(std::getenv("DBZ3_TEST_CB_S"))) : 1.0f;
      const float hy = 224.0f * ls;
      const float cg[14] = {-5.377f, 7.024f, 21.458f, -0.046f, 3.923f, -0.328f, 0.6109f, P(80, 324).x, P(80, 324).y, hy, kEsc, -0.54f, -0.25f, 0.25f};
      const float cs[14] = {0.0f, 6.731f, 29.331f, 0.0f, 1.643f, -0.381f, 0.6109f, P(420, 214).x, P(420, 214).y, hy, kEsc, -0.12f, 0.0f, -0.05f};
      if (std::getenv("DBZ3_TEST_GOKU_VIEJO") || !dbz3::pasarela::nave::DibujarFiguraB2(dl, 0, P(80, 610), 420.0f * ls, 0.0f, e.paso == 0 ? 0.0f : desde, 0, 827, 4, cg))
        DibGoku3DTarjeta(dl, P(84, 566), 350.0f * ls, desde, 0.62f);   // sin el banco 827 (otra ISO): la tarjeta de antes
      (void)ij; (void)gp;
      if (ir >= 0 && !std::getenv("DBZ3_TEST_SIN_FIG_RIVAL")) dbz3::pasarela::nave::DibujarFiguraB2(
          dl, ir, P(424, 378), 313.0f * ls, 0.0f, 34.0f / 60.0f + desde, ir == kSaibaman ? g_f[size_t(g_pend_r)].modo : 0,
          ir == kSaibaman ? 844 : 0, 0, ir == kSaibaman ? cs : nullptr);
      dl->PopClipRect();
    } else {
      tx(791, 4, -4, 361, 60, 401);
      tx(791, 4, 116, 387, 180, 427);
      tx(791, 0, 24, 295, 144, 407);
      const int quien = e.paso < e.hablan.size() && e.hablan[e.paso] >= 0 ? e.hablan[e.paso] : 0;   // sin dato: Goku
      if (const ImTextureID cab = CabezaB2(quien)) dl->AddImage(cab, P(24, 265), P(144, 385));
      tx(791, 2, 32, 379, 144, 427);
    }
    tx(791, 3, 148, 278, 620, 390, IM_COL32(255, 255, 255, 225));
    const Tex& x = e.img[size_t(e.textos[e.paso])];
    if (x) {
      const float esc = std::min(1.3f, std::min(440.0f / x.w, 90.0f / x.h));
      dl->AddImage(x.id(), P(384 - x.w * esc * 0.5f, 336 - x.h * esc * 0.5f), P(384 + x.w * esc * 0.5f, 336 + x.h * esc * 0.5f),
                   ImVec2(0, 0), ImVec2(1, 1), IM_COL32(40, 40, 50, 255));
    }
    dl->AddCircleFilled(P(484, 410), 10 * ls, IM_COL32(96, 104, 128, 255), 24);
    dl->AddLine(P(479, 405), P(489, 415), IM_COL32(40, 46, 70, 255), 2.4f * ls);
    dl->AddLine(P(489, 405), P(479, 415), IM_COL32(40, 46, 70, 255), 2.4f * ls);
    const float fs = 21.0f * ls;
    dl->AddText(ImGui::GetFont(), fs, ImVec2(P(500, 399).x + 1.5f, P(500, 399).y + 1.5f), IM_COL32(40, 30, 10, 255), ":Next");
    dl->AddText(ImGui::GetFont(), fs, P(500, 399), IM_COL32(255, 255, 255, 255), ":Next");
  } else if (e.teatro) {   // otras escenas de teatro: el fondo de cada texto y el texto encima
    dl->AddRectFilled(ImVec2(0, 0), W, IM_COL32(0, 0, 0, 255));
    const size_t nf = e.fondos.size(), nt = e.textos.size();
    const Tex& f = e.img[size_t(e.fondos[std::min(nf - 1, e.paso * nf / std::max<size_t>(1, nt))])];
    if (f) {
      const float esc = std::min(560.0f / f.w, 300.0f / f.h);
      dl->AddImage(f.id(), P(320 - f.w * esc * 0.5f, 200 - f.h * esc * 0.5f), P(320 + f.w * esc * 0.5f, 200 + f.h * esc * 0.5f),
                   ImVec2(0, 0), ImVec2(1, 1), blanco);
    }
    const Tex& x = e.img[size_t(e.textos[e.paso])];
    if (x) {
      const float esc = std::min(1.0f, std::min(560.0f / x.w, 90.0f / x.h));
      dl->AddImage(x.id(), P(320 - x.w * esc * 0.5f, 390 - x.h * esc * 0.5f), P(320 + x.w * esc * 0.5f, 390 + x.h * esc * 0.5f),
                   ImVec2(0, 0), ImVec2(1, 1), blanco);
    }
  } else {
    // charla como en B2: banda morada abajo, la cara de quien habla en su emblema rojo a la izquierda y la linea en una
    // caja blanca redondeada a su derecha. ponytail: la cara es la de B3 (CaraDe); los bustos de B2 no estan localizados.
    dl->AddRectFilled(P(0, 350), P(640, 448), IM_COL32(64, 40, 120, 235));
    dl->AddRectFilled(P(150, 300), P(625, 398), IM_COL32(244, 244, 244, 250), 14 * ls);
    dl->AddRect(P(150, 300), P(625, 398), IM_COL32(200, 200, 210, 255), 14 * ls, 0, 1.5f * ls);
    const int quien = e.paso < e.hablan.size() ? e.hablan[e.paso] : -1;
    // R7: la cara es el retrato de B2 de esa pieza (su ficha, 342+5k #2*indice, de la ISO del jugador). Los bustos propios
    // de las charlas no estan localizados (R7/b2/INFORME.md: ni en DATA_CMN ni en DATA_<idioma> hay un banco de bustos)
    // R7: las cabezas de B2 (CMN 1196: 55 de 136x136, una por ID de personaje de B2: 0 Goku, 9 Raditz, 10 Nappa...)
    if (const ImTextureID cab = CabezaB2(quien)) {
      dl->AddCircleFilled(P(75, 350), 64 * ls, IM_COL32(200, 40, 40, 255));
      dl->AddCircle(P(75, 350), 64 * ls, IM_COL32(255, 220, 120, 255), 0, 3 * ls);
      dl->AddImage(cab, P(18, 290), P(132, 404));
    }
    const Tex& x = e.img[size_t(e.textos[e.paso])];
    if (x) {
      const float esc = std::min(1.3f, std::min(450.0f / x.w, 80.0f / x.h));
      dl->AddImage(x.id(), P(388 - x.w * esc * 0.5f, 349 - x.h * esc * 0.5f), P(388 + x.w * esc * 0.5f, 349 + x.h * esc * 0.5f),
                   ImVec2(0, 0), ImVec2(1, 1), IM_COL32(30, 30, 40, 255));
    }
  }
  if (!e.comic) S::HelpLine(dl, a.At(600, 878), 34 * a.s, {{"A", T::T("Siguiente", "Next")}, {"B", T::T("Saltar", "Skip")}});
  // A2b: las ventanas M/P/T del original esperan a X (medido en PCSX2: la P de la fase IV sigue en pantalla 9 s despues del
  // contacto); solo la intro del Mundo Dragon (155) pasa sola y el modo automatico da su tiempo a cada linea.
  const bool sigue = (pressed & kA) || (g_esc_auto && desde > 3.0f) || (e.numero == 155 && desde > 6.0f);
  if (pressed & kB) e.paso = e.textos.size();
  else if (sigue && desde > 0.25f) {
    ++e.paso;
    e.t0 = t;
  }
  pressed = 0;
  if (e.paso >= e.textos.size()) {   // S1-F07.2: sus AddImage de este cuadro aun no se han pintado
    Diferir(std::move(e.img));
    g_esc = Escena{};
  }
}
// Goku del tablero en 3D, como en B2: los archivos fijos del tablero (ELF 0x2350C0, tabla 0x377C80) son CMN 335 (#AMO de
// 21 huesos) + 334 (sus 26 animaciones), 337 + 338 (Goku, #AMO de 44 huesos y 21 variantes; con la bandera 4 de
// 0x6992DC, 339 + 340) y 336 (las 19 animaciones de Goku). Los demas personajes del tablero de B2 son fichas (los
// retratos de 342+5k), como aqui. Animacion de espera y de andar: DBZ3_TEST_GOKU_ANIM="espera,andar" (indices de 336).
ps2::Modelo g_goku3d;
std::vector<ps2::Tex> g_goku3d_tx;
ps2::Anim g_goku_espera, g_goku_andar;
// R7: tabla DAT_00379010 (anim -> clip de CMN 336): 0 reposo -> 0, 1 andar -> 4, 2 combate -> 5 (contacto, estados
// 0x0B/0x0C: una vez y se queda), 3 alerta -> 6 (al buscar, 36 cuadros: FUN_00277D40)
ps2::Anim g_goku_lucha, g_goku_alerta;
// tras el combate (FUN_00277BB0, a la ficha que pierde): anim 5 -> clip 8 (golpeado) o, sin vidas, anim 4 -> clip 7
ps2::Anim g_goku_herido, g_goku_ko;
float g_goku_reac_t = -10;
bool g_goku_reac_ko = false;
float g_goku_alto = 0, g_goku_giro = 0;
bool g_goku3d_cargado = false;
bool g_goku_variante = false;   // orden 82 (ELF 0x6992DC bit 0x04): Goku del tablero con CMN 339/340
// Ficha del tablero en 3D (CMN 335: #AMO de 21 huesos y 3 partes; su textura 0 es el retrato y la 1 el dorso con la Z de
// cada pieza, de 342+5k: 2 * indice y 2 * indice + 1) con sus 26 animaciones (CMN 334). Las texturas de las fichas de la
// fase como ps2::Tex (las mismas que g_tfichas). ponytail: la espera es la animacion 0 (72 cuadros), sin confirmar.
ps2::Modelo g_ficha3d;
ps2::Anim g_ficha_espera, g_ficha_andar, g_ficha_lucha, g_ficha_alerta;   // CMN 334: clips 0, 1, 2 y 3 (el slot manda)
std::vector<uint8_t> g_ficha_amm;                     // L01: banco #AMM 334 para el clip +0x1C de las ordenes 58-60
std::map<int, ps2::Anim> g_ficha_clips;               // cache de esos clips por indice
float g_ficha_alto = 0;
// R7: la carta del CMN 335 tiene el retrato hacia +-x (con giro 0 se veia de canto, "mirando de lado"); en B2 mira al
// frente en reposo. Giro de base = el que lleva la normal media de las caras del retrato (textura 0) a +z (la camara).
float g_ficha_base = 0;
std::vector<ps2::Tex> g_ps2_fichas;
int g_ps2_fichas_fase = -1;
void CargarGoku3D() {
  if (g_goku3d_cargado) return;
  g_goku3d_cargado = true;
  g_goku_alto = 0;
  AfsPs2 cmn;
  if (!cmn.Abrir(BuscarJuego("budokai 2"), "DATA_CMN.AFS")) return;
  // Encargo 03/2: con el bit 0x04 de 0x6992DC (orden 82) el cargador usa CMN 339/340 en vez de 337/338.
  const int goku_amo = g_goku_variante ? 339 : 337, goku_amt = g_goku_variante ? 340 : 338;
  g_goku3d = ps2::CargarSuelto(cmn.Entrada(goku_amo), cmn.Entrada(goku_amt), true, true);   // con colores: el pelo negro
  if (!g_goku3d.esq) return;
  int ie = 0, ia = 4;   // reposo (99 cuadros) y carrera (36), en absoluto (auditoria10/B: la 5 es una guardia)
  if (const char* v = std::getenv("DBZ3_TEST_GOKU_ANIM")) std::sscanf(v, "%d,%d", &ie, &ia);
  const std::vector<uint8_t> amm = cmn.Entrada(336);
  g_goku_espera = ps2::LeerAnim(amm, uint32_t(ie), g_goku3d.esq->padre.size());
  g_goku_andar = ps2::LeerAnim(amm, uint32_t(ia), g_goku3d.esq->padre.size());
  g_goku_lucha = ps2::LeerAnim(amm, 5, g_goku3d.esq->padre.size());
  g_goku_alerta = ps2::LeerAnim(amm, 6, g_goku3d.esq->padre.size());
  g_goku_herido = ps2::LeerAnim(amm, 8, g_goku3d.esq->padre.size());
  g_goku_ko = ps2::LeerAnim(amm, 7, g_goku3d.esq->padre.size());
  g_goku_lucha.relativa = g_goku_alerta.relativa = g_goku_herido.relativa = g_goku_ko.relativa = false;
  g_goku_espera.relativa = g_goku_andar.relativa = false;   // en relativo salia roto (un brazo arriba, de lado)
  for (const auto& tr : g_goku3d.malla.tris)
    for (const auto& v : tr.p) g_goku_alto = std::max(g_goku_alto, v.y);
  g_goku3d_tx = ps2::Texturas(g_goku3d);
  REXLOG_INFO("dbz3 tablero b2: Goku 3D: {} triangulos, alto {:.1f}, espera {} ({} cuadros), andar {} ({} cuadros)",
              g_goku3d.malla.tris.size(), g_goku_alto, ie, g_goku_espera.cuadros, ia, g_goku_andar.cuadros);
}
// Orden 82 (ELF 0x293140, guion 309 de la fase VIII): el tablero cambia el Goku 3D a la variante
// CMN 339 (#AMO) + 340 (#AMT) y enciende el bit 0x04 de 0x6992DC. El cargador del tablero elige
// 337/338 con ese bit apagado y 339/340 encendido; la marca g_goku_variante hace lo mismo.
void VarianteGoku() {
  if (g_goku_variante) return;
  g_goku_variante = true;
  g_goku3d_cargado = false;   // CargarGoku3D (DibGoku3D) la recarga con 339/340 al siguiente cuadro
  g_goku3d_tx.clear();
  REXLOG_INFO("dbz3 tablero b2: orden 82: Goku del tablero con la variante (CMN 339/340)");
}
void CargarFicha3D() {
  static bool hecho = false;
  if (!hecho) {
    hecho = true;
    AfsPs2 cmn;
    if (cmn.Abrir(BuscarJuego("budokai 2"), "DATA_CMN.AFS")) {
      g_ficha3d = ps2::CargarSuelto(cmn.Entrada(335), {}, true, true);   // B2: la carta opaca con el alfa del retrato
      if (g_ficha3d.esq) {
        const int ie = std::getenv("DBZ3_TEST_FICHA_ANIM") ? std::atoi(std::getenv("DBZ3_TEST_FICHA_ANIM")) : 0;
        const std::vector<uint8_t> amm334 = cmn.Entrada(334);
        g_ficha_amm = amm334;   // L01: copia para cargar cualquier clip del banco bajo demanda
        g_ficha_clips.clear();
        g_ficha_espera = ps2::LeerAnim(amm334, uint32_t(ie), g_ficha3d.esq->padre.size());
        // R7 (b2.c:221432-221454): fuera de Goku el slot del registro (+0x1C) ES el clip del banco CMN 334; los estados
        // de la pieza piden 1 al andar (FUN_00275eb0 b2.c:221269), 2 al contacto (FUN_00277890 b2.c:222321) y 3 al
        // buscar (FUN_00277d40 b2.c:222532). Antes solo se cargaba el de reposo (las fichas no se movian al andar).
        // L01: anim-id 1/2/3 -> tabla 0x379010 -> clip 4/5/6 (el +0x1C NO es indice de clip).
        g_ficha_andar = ps2::LeerAnim(amm334, 4, g_ficha3d.esq->padre.size());
        g_ficha_lucha = ps2::LeerAnim(amm334, 5, g_ficha3d.esq->padre.size());
        g_ficha_alerta = ps2::LeerAnim(amm334, 6, g_ficha3d.esq->padre.size());
        g_ficha_espera.relativa = g_ficha_andar.relativa = g_ficha_lucha.relativa = g_ficha_alerta.relativa = true;
      }
      for (const auto& tr : g_ficha3d.malla.tris)
        for (const auto& v : tr.p) g_ficha_alto = std::max(g_ficha_alto, v.y);
      // plano del retrato en la pose de espera (cuadro 0): normal geometrica CON SIGNO de sus triangulos (dice hacia
      // donde mira el retrato, no solo cual es su plano). La ficha se pinta girada `giro` (Escena::Poner: +giro lleva
      // +x hacia -z) con la camara en +z, asi que el giro de base es el que deja esa normal mirando a +z; el signo
      // importa: con el cuarto de vuelta al reves el retrato sale MIRADO DEL REVES (su cara por detras; se veia asi
      // hasta la ronda 4: el parche del 2026-10-17 solo miraba |nx| > |nz|).
      ps2::Malla p0;
      if (g_ficha3d.esq && g_ficha_espera) ps2::Posar(*g_ficha3d.esq, g_ficha_espera, 0.0f, p0);
      float nx = 0, nz = 0;
      for (const auto& tr : (p0 ? p0 : g_ficha3d.malla).tris) {
        if (tr.tex != 0) continue;
        const auto &a = tr.p[0], &b = tr.p[1], &c = tr.p[2];
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        nx += uy * vz - uz * vy;
        nz += ux * vy - uy * vx;
      }
      if (std::getenv("DBZ3_TEST_FICHA_BASE")) g_ficha_base = float(std::atof(std::getenv("DBZ3_TEST_FICHA_BASE")));
      else if (std::fabs(nx) >= std::fabs(nz)) g_ficha_base = nx < 0 ? 1.5707963f : -1.5707963f;   // de canto hacia +-x
      else g_ficha_base = nz < 0 ? 3.14159265f : 0.0f;                                             // ya mira a +-z
      REXLOG_INFO("dbz3 tablero b2: ficha 3D: normal del retrato ({:.2f}, {:.2f}), giro de base {:.2f}", nx, nz, g_ficha_base);
    }
    REXLOG_INFO("dbz3 tablero b2: ficha 3D: {} triangulos, alto {:.1f}, espera {} cuadros", g_ficha3d.malla.tris.size(),
                g_ficha_alto, g_ficha_espera.cuadros);
  }
  if (g_ps2_fichas_fase != g_d.fase) {   // las texturas de las fichas de esta fase
    g_ps2_fichas_fase = g_d.fase;
    g_ps2_fichas.clear();
    for (const Imagen& im : g_d.fichas) g_ps2_fichas.push_back(ps2::Crear(im));
  }
}
// Camara de una figura del tablero: casi de frente, 20 grados desde arriba, el pie en `pie` y `alto_px` de alto.
ps2::Camara CamaraFigura(ImVec2 pie, float alto_modelo, float alto_px) {
  ps2::Camara cam;
  cam.fov = 0.785f;
  cam.c = pie;
  cam.h = ImVec2(450, 450);
  const float f = 450.0f / std::tan(cam.fov * 0.5f), d = alto_modelo * f / std::max(alto_px, 1.0f);
  const float th = 0.35f, cth = std::cos(th), sth = std::sin(th);
  cam.m[0] = 1;
  cam.m[5] = cth, cam.m[9] = -sth;
  cam.m[6] = sth, cam.m[10] = cth;
  cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  return cam;
}
// Ficha 3D de la pieza `idx` (su retrato y su dorso) de pie en `pie`. La animacion es el clip del CMN 334 que pide su
// estado (0 reposo, 1 andar, 2 combate y 3 buscar; el original usa el slot del registro como indice del banco).
// L01: clip del banco #AMM 334 pedido por la orden 58-60 (+0x1C). Se carga una vez y se cachea; si el indice no
// existe, nullptr y la ficha sigue con sus clips de estado (reposo/andar/combate/alerta).
static const int kAnimClip[19] = {0, 4, 5, 6, 7, 8, 9, 0xB, 0xA, 0x10, 0xC, 0xD, 0xE, 0x10, 1, 2, 3, 0x11, 0x12};
int ClipDeAnim(int id) { return (id >= 0 && id < 19) ? kAnimClip[id] : -1; }   // tabla 0x379010 del ELF
const ps2::Anim* ClipFicha(int anim) {
  const int i = ClipDeAnim(anim);   // L01: +0x1C es anim-id, no clip
  if (i < 0 || g_ficha_amm.empty() || !g_ficha3d.esq) return nullptr;
  auto it = g_ficha_clips.find(i);
  if (it != g_ficha_clips.end()) return it->second ? &it->second : nullptr;
  ps2::Anim a = ps2::LeerAnim(g_ficha_amm, uint32_t(i), g_ficha3d.esq->padre.size());
  a.relativa = true;
  ps2::Anim& e = g_ficha_clips[i];
  e = a;
  return e ? &e : nullptr;
}
bool DibFicha3D(ImDrawList* dl, int idx, ImVec2 pie, float alto_px, float t, float giro = 0, bool anda = false,
                bool lucha = false, float busca = -1, int clip = -1, float clip_t = 0, int estado = 0) {
  CargarFicha3D();
  const size_t r = size_t(idx) * 2, dorso = r + 1;
  if (!g_ficha3d.malla || g_ficha_alto <= 0 || dorso >= g_ps2_fichas.size() || !g_ps2_fichas[r] || !g_ps2_fichas[dorso])
    return false;
  // combate: una vez y se queda en el ultimo cuadro (estados 0x0B/0x0C, FUN_00277890 -> FUN_00275db0 b2.c:221205);
  // buscar: el clip en bucle mientras dure la alerta; andar: en bucle mientras anda
  const ps2::Anim* fan = &g_ficha_espera;
  float ta = t;
  if (const ps2::Anim* fc = ClipFicha(clip)) {   // L01: 0x10 = bucle, 0x0F/0x11 = se queda
    fan = fc;
    if (estado == 0x10) ta = std::fmod(std::max(0.0f, clip_t), float(fc->cuadros) / 60.0f);
    else ta = std::min(std::max(0.0f, clip_t), float(fc->cuadros - 1) / 60.0f);
  } else if (lucha && g_ficha_lucha) {
    fan = &g_ficha_lucha;
    ta = std::min(std::max(0.0f, g_ahora - g_pend_t), float(g_ficha_lucha.cuadros - 1) / 60.0f);
  } else if (busca >= 0 && g_ficha_alerta) {
    fan = &g_ficha_alerta;
    ta = std::fmod(busca, std::max(1.0f, float(g_ficha_alerta.cuadros)) / 60.0f);
  } else if (anda && g_ficha_andar) {
    fan = &g_ficha_andar;
  }
  static ps2::Malla pose;
  const bool anima = g_ficha3d.esq && bool(*fan);
  if (anima) ps2::Posar(*g_ficha3d.esq, *fan, ta, pose);
  ps2::Malla m = anima ? pose : g_ficha3d.malla;
  float ymin = 1e9f;
  for (auto& tr : m.tris) {
    tr.tex = tr.tex == 0 ? int(r) : int(dorso);
    for (const auto& v : tr.p) ymin = std::min(ymin, v.y);
  }
  const ps2::Camara cam = CamaraFigura(pie, g_ficha_alto, alto_px);
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;   // cel shading y borde de P4 (la carta y la peana traen normales)
  toon.borde = g_ficha_alto * 0.009f;
  esc.Poner(cam, m, g_ps2_fichas, {0, -ymin, 0}, g_ficha_base + giro, 1.0f, toon);
  esc.Pintar(dl);
  return true;
}
// Goku 3D de pie en `pie` (pantalla), de `alto_px` de alto, mirando hacia (dx, dy) del mapa si anda.
bool DibGoku3D(ImDrawList* dl, ImVec2 pie, float alto_px, float t, bool anda, float dx, float dy) {
  CargarGoku3D();
  if (!g_goku3d.malla || g_goku_alto <= 0) return false;
  // dx > 0: pose de combate (dx s desde el contacto); dy > 0: alerta de la busqueda (dy s); si no, andar o reposo
  const bool lucha = dx > 0 && g_goku_lucha, alerta = !lucha && dy > 0 && g_goku_alerta;
  const ps2::Anim& reac = g_goku_reac_ko ? g_goku_ko : g_goku_herido;
  const float tr = g_ahora - g_goku_reac_t;
  const bool reacciona = !lucha && !alerta && !anda && reac && tr >= 0 && tr < float(reac.cuadros) / 60.0f + 0.5f;
  const ps2::Anim& an = lucha ? g_goku_lucha : alerta ? g_goku_alerta : reacciona ? reac : anda ? g_goku_andar : g_goku_espera;
  if (lucha || alerta) t = std::min(lucha ? dx : dy, float(an.cuadros - 1) / 60.0f);   // una vez y se queda en el ultimo
  if (reacciona) t = std::min(tr, float(an.cuadros - 1) / 60.0f);
  static ps2::Malla pose;
  const bool anima = bool(an);
  if (anima) ps2::Posar(*g_goku3d.esq, an, t, pose);
  // Posar deja la y del modelo: el pie mas bajo al suelo en cada cuadro (con la del primero, al pasar de esperar a
  // andar flotaba o se hundia; auditoria7/G)
  float ymin = 1e9f;
  for (const auto& tr : (anima ? pose : g_goku3d.malla).tris)
    for (const auto& v : tr.p) ymin = std::min(ymin, v.y);
  ps2::Camara cam;   // casi de frente y un poco desde arriba (20 grados), el pie de Goku en `pie`
  cam.fov = 0.785f;
  cam.c = pie;
  cam.h = ImVec2(450, 450);
  const float f = 450.0f / std::tan(cam.fov * 0.5f), d = g_goku_alto * f / std::max(alto_px, 1.0f);
  const float th = 0.35f, cth = std::cos(th), sth = std::sin(th);
  cam.m[0] = 1;
  cam.m[5] = cth, cam.m[9] = -sth;
  cam.m[6] = sth, cam.m[10] = cth;
  cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;   // cel shading y borde negro de P4 (las partes de Goku traen normales, vtype & 1)
  toon.borde = g_goku_alto * 0.009f;
  esc.Poner(cam, anima ? pose : g_goku3d.malla, g_goku3d_tx, {0, -ymin, 0}, g_goku_giro, 1.0f, toon);
  esc.Pintar(dl);
  return true;
}
// Goku para la tarjeta "Battle!": el modelo de combate de B2 (CMN 171 = #AMB[#AMO de 42 huesos, #AMT]; el SPX 407 carga
// ER_GOK.amc/.amm = CMN 411/412, 42 huesos) con la animacion de ER_GOK, de frente a la camara y girado `giro`. B2 guarda el
// pelo y la cara en modelos aparte (CMN 176-179, #AMB[#AMO,#AMT]) en el espacio local del hueso de la cabeza: se atan al
// hueso mas alto del cuerpo en reposo (el #AMO no trae nombres de hueso).
ps2::Modelo g_goku_er, g_goku_er_cab;
std::vector<ps2::Tex> g_goku_er_tx;
ps2::Anim g_goku_er_an;
float g_goku_er_alto = 0;
int g_goku_er_hueso = -1;
bool g_goku_er_cargado = false;
std::vector<uint8_t> g_er_amm;   // banco de animaciones de la tarjeta (para el barrido de pruebas)
size_t g_er_nb = 0;
void CargarGokuER() {
  if (g_goku_er_cargado) return;
  g_goku_er_cargado = true;
  AfsPs2 cmn;
  if (!cmn.Abrir(BuscarJuego("budokai 2"), "DATA_CMN.AFS")) return;
  const std::vector<uint8_t> amb = cmn.Entrada(171);
  g_goku_er = ps2::CargarSuelto(HijoAmb(amb, 0), HijoAmb(amb, 1), true, true, true);
  if (!g_goku_er.esq) return;
  const int ia = std::getenv("DBZ3_TEST_ER_ANIM") ? std::atoi(std::getenv("DBZ3_TEST_ER_ANIM")) : 0;
  // la animacion: la postura de combate de la bolsa comun de B2 (CMN 134 hijo 0: 275 animaciones de los 42 huesos del
  // cuerpo; la 0 es la guardia en reposo, medido contra la tarjeta del original); el ER_GOK.amm (CMN 412) no encaja con
  // este esqueleto (la postura sale rota)
  const std::vector<uint8_t> amm = std::getenv("DBZ3_TEST_ER_AMM") ? std::atoi(std::getenv("DBZ3_TEST_ER_AMM")) == 412 ? cmn.Entrada(412) : HijoAmb(cmn.Entrada(size_t(std::atoi(std::getenv("DBZ3_TEST_ER_AMM")))), 0)
                                                                    : HijoAmb(cmn.Entrada(134), 0);
  g_er_amm = amm;
  g_er_nb = std::getenv("DBZ3_TEST_ER_NB") ? size_t(std::atoi(std::getenv("DBZ3_TEST_ER_NB"))) : g_goku_er.esq->padre.size();
  g_goku_er_an = ps2::LeerAnim(amm, uint32_t(ia), g_er_nb);
  g_goku_er_an.relativa = std::getenv("DBZ3_TEST_ER_REL") != nullptr;
  for (const auto& tr : g_goku_er.malla.tris)
    for (const auto& v : tr.p) g_goku_er_alto = std::max(g_goku_er_alto, v.y);
  g_goku_er_tx = ps2::Texturas(g_goku_er);
  const int cab = std::getenv("DBZ3_TEST_ER_CABEZA") ? std::atoi(std::getenv("DBZ3_TEST_ER_CABEZA")) : 176;
  if (cab > 0) {
    const std::vector<uint8_t> ac = cmn.Entrada(size_t(cab));
    g_goku_er_cab = ps2::CargarSuelto(HijoAmb(ac, 0), HijoAmb(ac, 1), true, true, true);
  }
  float ymax = -1e9f;
  const auto& esq = *g_goku_er.esq;
  {   // posicion de cada hueso en reposo (cadena de ejes locales; inv_mundo lleva escala y su inversa rigida no vale)
    std::vector<ps2::M34> wb(esq.padre.size());
    std::string tp;
    for (size_t b = 0; b < esq.padre.size(); ++b) {
      wb[b] = esq.padre[b] >= 0 ? wb[size_t(esq.padre[b])] * esq.local[b] : esq.local[b];
      const float y = wb[b].m[7];
      tp += std::to_string(b) + ":" + std::to_string(y).substr(0, 5) + " ";
      if (y > ymax) ymax = y, g_goku_er_hueso = int(b);
    }
    REXLOG_INFO("dbz3 er: y de reposo de cada hueso: {}", tp);
    if (g_goku_er_hueso >= 0 && esq.padre[size_t(g_goku_er_hueso)] >= 0) g_goku_er_hueso = esq.padre[size_t(g_goku_er_hueso)];   // 26 (la punta) -> 18 (la cabeza)
  }
  if (std::getenv("DBZ3_TEST_ER_HUESO")) g_goku_er_hueso = std::atoi(std::getenv("DBZ3_TEST_ER_HUESO"));
  if (g_goku_er_cab.esq)
    for (auto& x : ps2::Texturas(g_goku_er_cab)) g_goku_er_tx.push_back(std::move(x));
  REXLOG_INFO("dbz3 tablero b2: Goku de la tarjeta (CMN 171 + ER_GOK 412 anim {}): {} triangulos, {} huesos, alto {:.1f}, {} cuadros; "
              "cabeza {} triangulos, hueso {}",
              ia, g_goku_er.malla.tris.size(), esq.padre.size(), g_goku_er_alto, g_goku_er_an.cuadros,
              g_goku_er_cab.esq ? g_goku_er_cab.esq->crudo.tris.size() : 0, g_goku_er_hueso);
}
// Ejes de mundo de cada hueso en el cuadro de `an` en `t` (el mismo calculo que ps2::Posar, solo para colocar la cabeza)
std::vector<ps2::M34> EjesDeMundo(const ps2::Esqueleto& esq, const ps2::Anim& an, float t) {
  const size_t nb = esq.padre.size();
  const float fc = std::fmod(t * 60.0f, float(std::max(an.cuadros, 1)));
  std::vector<ps2::M34> w(nb);
  for (size_t i = 0; i < nb; ++i) {
    ps2::M34 l = esq.local[i];
    if (i < an.ang.size() && !an.ang[i].empty()) {
      const auto& ks = an.ang[i];
      size_t ia = 0;
      while (ia + 1 < ks.size() && float(ks[ia + 1][0]) <= fc) ++ia;
      const size_t ib = ia + 1 < ks.size() ? ia + 1 : ia;
      const float fa = float(ks[ia][0]), fb = float(ks[ib][0]);
      const float u = fb > fa ? std::clamp((fc - fa) / (fb - fa), 0.0f, 1.0f) : 0.0f;
      auto ang = [&](int e) {
        const int d = int(int16_t(uint16_t(ks[ib][size_t(e)] - ks[ia][size_t(e)])));
        return (float(ks[ia][size_t(e)]) + u * float(d)) * 9.5873799e-5f;
      };
      const float x = ang(1), y = ang(2), z = ang(3);
      const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
      const float r[9] = {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
                          sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
                          -sy,     cy * sx,                cy * cx};
      if (an.relativa) {
        const ps2::M34& rep = esq.local[i];
        for (int f = 0; f < 3; ++f)
          for (int k = 0; k < 3; ++k)
            l.m[f * 4 + k] = rep.m[f * 4] * r[k] + rep.m[f * 4 + 1] * r[3 + k] + rep.m[f * 4 + 2] * r[6 + k];
      } else {
        for (int f = 0; f < 3; ++f)
          for (int k = 0; k < 3; ++k) l.m[f * 4 + k] = r[f * 3 + k];
      }
    }
    if (i < an.pos.size() && !an.pos[i].empty()) {
      const auto& ks = an.pos[i];
      size_t ia = 0;
      while (ia + 1 < ks.size() && ks[ia + 1][0] <= fc) ++ia;
      const size_t ib = ia + 1 < ks.size() ? ia + 1 : ia;
      const float u = ks[ib][0] > ks[ia][0] ? std::clamp((fc - ks[ia][0]) / (ks[ib][0] - ks[ia][0]), 0.0f, 1.0f) : 0.0f;
      float c[4];
      for (int e = 1; e < 4; ++e) c[e] = ks[ia][size_t(e)] + u * (ks[ib][size_t(e)] - ks[ia][size_t(e)]);
      if (an.relativa) {
        l.m[3] = esq.local[i].m[3] + c[1], l.m[7] = esq.local[i].m[7] + c[2], l.m[11] = esq.local[i].m[11] + c[3];
      } else {
        l.m[3] = c[1], l.m[7] = c[2], l.m[11] = c[3];
      }
    }
    w[i] = esq.padre[i] >= 0 ? w[size_t(esq.padre[i])] * l : l;
  }
  return w;
}
bool DibGokuER(ImDrawList* dl, ImVec2 pie, float alto_px, float t, float giro, const ps2::Anim& an) {
  CargarGokuER();
  if (!g_goku_er.malla || g_goku_er_alto <= 0) return false;
  static ps2::Malla pose;
  const bool anima = bool(an);
  if (anima) ps2::Posar(*g_goku_er.esq, an, t, pose);
  ps2::Malla m = anima ? pose : g_goku_er.malla;
  float ymin = 1e9f, ymax_p = -1e9f;
  for (const auto& tr : m.tris)
    for (const auto& v : tr.p) ymin = std::min(ymin, v.y), ymax_p = std::max(ymax_p, v.y);
  if (g_goku_er_cab.esq && g_goku_er_hueso >= 0 && anima) {   // la cabeza, atada al hueso
    const auto w = EjesDeMundo(*g_goku_er.esq, an, t);
    static const bool modo1 = std::getenv("DBZ3_TEST_ER_CABMODO") != nullptr;
    const ps2::M34 W = modo1 ? w[size_t(g_goku_er_hueso)] * g_goku_er.esq->inv_mundo[size_t(g_goku_er_hueso)] : w[size_t(g_goku_er_hueso)];
    const int base_tex = int(g_goku_er_tx.size()) - int(g_goku_er_cab.tex.size());
    for (const auto& tr0 : g_goku_er_cab.esq->crudo.tris) {
      ps2::Malla::Tri tr = tr0;
      for (int c = 0; c < 3; ++c) {
        tr.p[c] = W.por(tr0.p[c]);
        tr.p[c].x -= g_goku_er.esq->centro.x;
        tr.p[c].z -= g_goku_er.esq->centro.z;
        tr.n[c] = W.gira(tr0.n[c]);
      }
      tr.tex = tr0.tex + base_tex;
      m.tris.push_back(tr);
    }
  }
  ps2::Camara cam;
  cam.fov = 0.785f;
  cam.c = pie;
  cam.h = ImVec2(450, 450);
  const float f = 450.0f / std::tan(cam.fov * 0.5f), d = (ymax_p - ymin + 1.1f) * f / std::max(alto_px, 1.0f);   // alto_px = lo que mide la pose (con el pelo)
  cam.m[0] = cam.m[5] = cam.m[10] = cam.m[15] = 1;
  cam.m[14] = -d;
  cam.ok = true;
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;
  toon.borde = g_goku_er_alto * 0.009f;
  esc.Poner(cam, m, g_goku_er_tx, {0, -ymin, 0}, giro, 1.0f, toon);
  esc.Pintar(dl);
  return true;
}
bool DibGoku3DTarjeta(ImDrawList* dl, ImVec2 pie, float alto_px, float t, float giro) {
  // la postura de la tarjeta es fija en B2 (se ve igual en las 10 capturas seguidas): el cuadro 0 de la animacion
  static const float fijo = std::getenv("DBZ3_TEST_ER_T") ? float(std::atof(std::getenv("DBZ3_TEST_ER_T"))) / 60.0f : 0.0f;
  (void)t;
  // PRUEBA (A2c): DBZ3_TEST_ER_ARCHIVO=fichero con el indice de animacion del banco (barrido de poses contra el original)
  if (const char* f = std::getenv("DBZ3_TEST_ER_ARCHIVO")) {
    static int ult = -1, cuenta = 0;
    if (++cuenta % 6 == 0) {
      int k = -1;
      if (FILE* fp = std::fopen(f, "r")) {
        if (std::fscanf(fp, "%d", &k) != 1) k = -1;
        std::fclose(fp);
      }
      if (k >= 0 && k != ult && !g_er_amm.empty()) {
        g_goku_er_an = k == 9999 ? ps2::Anim{} : ps2::LeerAnim(g_er_amm, uint32_t(k % 1000), g_er_nb);
        g_goku_er_an.relativa = k >= 1000;   // 1000+ = animacion relativa
        ult = k;
        REXLOG_INFO("dbz3 er barrido: animacion {} ({} cuadros)", k, g_goku_er_an.cuadros);
      }
    }
  }
  return DibGokuER(dl, pie, alto_px, fijo, giro, g_goku_er_an);
}
std::vector<Tex> g_tdeseos, g_tshen_frases, g_tshen_marco;
int g_deseo = 0;
// Shenron en 3D: el modelo de B2 (CMN 327: #AMO hijo 0 con su #AMT hijo 1; el 328 es el sitio, las Bolas y la luz)
// pintado con el lector de modelos de PS2 de P4 (ps2_modelo.h) con una camara fija que lo encuadra entero.
ps2::Modelo g_shen_mod;
std::vector<ps2::Tex> g_shen_tx;
ps2::Tex g_shen_nubes;   // nubes del cielo de la escena del deseo (CMN 328 hijo 1, textura 0)
ps2::Modelo g_shen_sitio;   // la escena del deseo (CMN 328 hijo 0 + #AMT hijo 1), en las coordenadas de Shenron
std::vector<ps2::Tex> g_shen_sitio_tx;
ps2::Modelo g_shen_bolas;   // CMN 328 hijos 7 (#AMO de 57 huesos), 8 (#AMT) y 9 (#AMM): las 7 Bolas de la aparicion
std::vector<ps2::Tex> g_shen_bolas_tx;
bool g_shen_cargado = false;
int g_shen = 0;   // 0 "Te concedere un deseo", 1 la lista, 2 "Si, eso es aceptable", 3 "Ahora debo irme..."
// Deseo -> capsula de B3 (por nombre: SkillList.dson). Orden de la lista: 540 #0-5 (portal, trajes de Bulma 2-6), 539 #0-4
// (Fusion <Tiencha>, Pothala <Vegetto>(Vegeta), Pothala <Gokutan>(Goku), Pothala <Gokutan>(Mr. Satan), Pothala
// <Kibitoshin>), 538 #0-28 ("Avance" de cada personaje). Solo Pothala <Vegetto>(Vegeta) tiene equivalente: la 192
// (Potara <Vegito> de Vegeta). Sin equivalente en B3: Tiencha, Gokutan, Kibitoshin, los trajes de Bulma (no es
// luchadora en B3) y los 29 "Avance" (B3 no tiene esas capsulas).
int DeseoB3(int k) { return k == 7 ? 192 : 0; }
void DarCapsulaB3(int c) {   // inventario de capsulas de la partida (u8 por ID en 0x824BA110 + 0x2AFE9; hasta 9)
  uint8_t* base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (!c || !base) return;
  constexpr dbz3::GA kPartida = 0x824BA110;
  const auto a = kPartida + 0x2AFE9 + c;
  const uint8_t n = REX_LOAD_U8(a);
  if (n < 9) REX_STORE_U8(a, uint8_t(n + 1));
  REXLOG_INFO("dbz3 tablero b2: deseo: capsula {} de B3 ({} -> {})", c, n, std::min(9, n + 1));
}
int g_tfase = -1;

void Texturas() {
  if (g_tfase == g_d.fase) return;
  auto* dr = dbz3::launcher::PreviewDrawer();
  if (!dr) return;
  auto crear = [&](const Imagen& im) {
    Tex x;
    if (!im) return x;
    x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false,
                            im.rgba.data());
    x.w = float(im.w);
    x.h = float(im.h);
    return x;
  };
  auto lista = [&](const std::vector<Imagen>& v, std::vector<Tex>& out) {
    out.clear();
    for (const Imagen& im : v) out.push_back(crear(im));
  };
  lista(g_d.mapa, g_tmapa);
  lista(g_d.fichas, g_tfichas);
  lista(g_d.adornos, g_tadornos);
  lista(g_d.loc, g_tloc);
  if (g_ticonos.empty()) {
    lista(g_d.hud, g_thud);
    lista(g_d.iconos, g_ticonos);
    g_tgo = crear(g_d.game_over);
    g_tgoku = crear(g_d.goku);
    g_thoyo = crear(g_d.hoyo);
    g_tcabezas = crear(g_d.cabezas);
    lista(g_d.deseos, g_tdeseos);
    lista(g_d.shen_frases, g_tshen_frases);
    lista(g_d.shen_marco, g_tshen_marco);
  }
  g_tfase = g_d.fase;
}
const Tex* Icono(int k) { return k >= 0 && k < int(g_ticonos.size()) && g_ticonos[size_t(k)] ? &g_ticonos[size_t(k)] : nullptr; }
// Recorte (px de la textura) de un icono en p0..p1.
void DibIcono(ImDrawList* dl, int k, ImVec2 p0, ImVec2 p1, ImVec4 src = ImVec4(0, 0, 0, 0), ImU32 col = IM_COL32_WHITE) {
  const Tex* x = Icono(k);
  if (!x) return;
  if (src.z <= 0) src = ImVec4(0, 0, x->w, x->h);
  dl->AddImage(x->id(), p0, p1, ImVec2(src.x / x->w, src.y / x->h), ImVec2(src.z / x->w, src.w / x->h), col);
}

// Pantalla de 640x448 del original dentro del area: origen y escala.
struct Lienzo {
  ImVec2 o;
  float s = 1;
  ImVec2 P(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s); }
};
Lienzo LienzoDe(const S::Area& a) {
  Lienzo L;
  L.s = std::min(1200.0f / 640.0f, 900.0f / 448.0f) * a.s;
  L.o = ImVec2(a.p0.x + (1200.0f * a.s - 640.0f * L.s) * 0.5f, a.p0.y + (900.0f * a.s - 448.0f * L.s) * 0.5f);
  return L;
}
// Animacion k de un #AMA del original (texturas tx) en su sitio: c = cuadro de su lista A a 60 por segundo (< 0 = el de
// reposo; posicion lineal entre claves), (dx, dy) = desplazamiento, giro = radianes alrededor de su ancla.
// Pose de la animacion de un sprite en el cuadro c (< 0 = reposo): posicion, escala y alfa de la lista A, lineales entre
// claves; pasado el ultimo cuadro, el reposo (ultima clave).
struct Pose {
  float px, py, ex, ey, al;
};
Pose PoseDe(const Datos::Sprite& sp, float c) {
  Pose o{sp.px, sp.py, sp.ex, sp.ey, 1.0f};
  for (size_t i = 1; c >= 0 && i < sp.claves.size(); ++i) {
    const auto &k0 = sp.claves[i - 1], &k1 = sp.claves[i];
    if (c >= k1[0]) continue;
    const float f = k1[0] > k0[0] ? std::clamp((c - k0[0]) / (k1[0] - k0[0]), 0.0f, 1.0f) : 1.0f;
    o.px = k0[1] + (k1[1] - k0[1]) * f;
    o.py = k0[2] + (k1[2] - k0[2]) * f;
    o.al = k0[3] + (k1[3] - k0[3]) * f;
    o.ex = k0[4] + (k1[4] - k0[4]) * f;
    o.ey = k0[5] + (k1[5] - k0[5]) * f;
    break;
  }
  return o;
}
void DibSp(ImDrawList* dl, const std::vector<Datos::Sprite>& v, const std::vector<Tex>& tx, int k, const Lienzo& L,
           float c = -1, ImU32 col = IM_COL32_WHITE, float dx = 0, float dy = 0, float giro = 0) {
  if (k < 0 || k >= int(v.size())) return;
  const Datos::Sprite& sp = v[size_t(k)];
  if (sp.tex < 0 || sp.tex >= int(tx.size()) || !tx[size_t(sp.tex)]) return;
  const Tex& x = tx[size_t(sp.tex)];
  const Pose po = PoseDe(sp, c);
  float px = po.px + dx, py = po.py + dy;
  if (po.al < 1.0f)   // alfa de la clave (0..1) sobre el del color
    col = (col & 0x00FFFFFFu) | (ImU32(float(col >> 24) * std::clamp(po.al, 0.0f, 1.0f)) << 24);
  const ImVec2 uv0(sp.u0 / x.w, sp.v0 / x.h), uv1(sp.u1 / x.w, sp.v1 / x.h);
  const float x0 = sp.x0 * po.ex, x1 = sp.x1 * po.ex, y0 = sp.y0 * po.ey, y1 = sp.y1 * po.ey;
  if (giro == 0) return dl->AddImage(x.id(), L.P(px + x0, py + y0), L.P(px + x1, py + y1), uv0, uv1, col);
  const float cs = std::cos(giro), sn = std::sin(giro);
  auto r = [&](float lx, float ly) { return L.P(px + lx * cs - ly * sn, py + lx * sn + ly * cs); };
  dl->AddImageQuad(x.id(), r(x0, y0), r(x1, y0), r(x1, y1), r(x0, y1), uv0, ImVec2(uv1.x, uv0.y), uv1, ImVec2(uv0.x, uv1.y), col);
}
void DibHudSp(ImDrawList* dl, int k, const Lienzo& L, float c = -1, ImU32 col = IM_COL32_WHITE, float dx = 0, float dy = 0,
              float giro = 0) {
  DibSp(dl, g_d.hud_ama, g_thud, k, L, c, col, dx, dy, giro);
}

// Encargo 01: listas A (pose) y B (recorte) independientes. No cambia DibSp ni las barras de vida.
// `imagen` permite la composicion original: pose de 0x2D + etiqueta de 0x33+j en el panel.
bool DibNativa01(ImDrawList* dl, const std::vector<Datos::Sprite>& v, const std::vector<Tex>& tx,
                 int anim, const Lienzo& L, float cuadro, ImU32 col = IM_COL32_WHITE, float dy = 0, int imagen = -1) {
  if (anim < 0 || anim >= int(v.size())) return false;
  if (imagen < 0) imagen = anim;
  if (imagen >= int(v.size())) return false;
  const auto& sp = v[size_t(anim)];
  const auto& im = v[size_t(imagen)];
  const Datos::Sprite::Recorte* rec = nullptr;
  for (const auto& r : im.recortes) {
    if (r.cuadro > cuadro && rec) break;
    rec = &r;
  }
  if (!rec || rec->tex < 0 || rec->tex >= int(tx.size()) || !tx[size_t(rec->tex)]) return false;
  const Tex& x = tx[size_t(rec->tex)];
  const Pose po = PoseDe(sp, cuadro);
  const float x0 = im.x0 * po.ex, x1 = im.x1 * po.ex, y0 = im.y0 * po.ey, y1 = im.y1 * po.ey;
  col = (col & 0x00FFFFFFu) | (ImU32(float(col >> 24) * std::clamp(po.al, 0.0f, 1.0f)) << 24);
  dl->AddImage(x.id(), L.P(po.px + x0, po.py + dy + y0), L.P(po.px + x1, po.py + dy + y1),
               ImVec2(rec->u0 / x.w, rec->v0 / x.h), ImVec2(rec->u1 / x.w, rec->v1 / x.h), col);
  return true;
}

// FUN_00239690: tabla 0x377CC0 (condicion +0x1D de T1). Base 0x11 y emblemas 0x12..0x16.
bool DibCasilla01(ImDrawList* dl, const Lienzo& L, int condicion, float cuadro, ImU32 col) {
  if (condicion < 0 || condicion > 5 || g_d.life_ama.size() <= 22) return false;
  if (!DibNativa01(dl, g_d.life_ama, g_ticonos, 17, L, 0, col)) return false;
  if (condicion) {
    const int anim = 17 + condicion;
    const float c = std::fmod(std::max(0.0f, cuadro), std::max(1.0f, g_d.life_ama[size_t(anim)].dur));
    DibNativa01(dl, g_d.life_ama, g_ticonos, anim, L, c, col);
  }
  return true;
}

// Orden real del panel (DAT_0037A830, 14 bytes); no es FUN_0027CD40 (objetos T8).
constexpr int kAliados01[] = {28, 26, 27, 6, 4, 7, 8, 3, 5, 35, 24, 21, 33, 2};
bool DibAliados01(ImDrawList* dl, const S::Area& a, float t, const std::vector<int>& libres) {
  static std::vector<Tex> tx;
  static int fase = -1;
  static float visto = -10, inicio = 0;
  if (fase != g_d.fase) {
    tx.clear();
    auto* dr = dbz3::launcher::PreviewDrawer();
    if (!dr) return false;
    for (const Imagen& im : g_d.aliados) {
      Tex x;
      if (im) {
        x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
        x.w = float(im.w); x.h = float(im.h);
      }
      tx.push_back(std::move(x));
    }
    fase = g_d.fase;
    visto = -10;
  }
  if (g_d.aliados_ama.size() < 71 || tx.size() < 8 || !tx[0] || !tx[1] || !tx[6] || !tx[7]) return false;
  if (t - visto > 0.25f) inicio = t;
  visto = t;
  const float c = std::max(0.0f, (t - inicio) * 60.0f);
  const Lienzo L = LienzoDe(a);
  auto dib = [&](int anim, float cuadro, float dy = 0, int im = -1, ImU32 col = IM_COL32_WHITE) {
    return DibNativa01(dl, g_d.aliados_ama, tx, anim, L, cuadro, col, dy, im);
  };
  // 0 es un velo sin textura: se conserva el oscurecido local, no se inventa un sprite.
  dl->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 125, 130));   // A2: velo azul medido (nieve 215 -> 105/100/175)
  dib(c < 31 ? 3 : 4, c < 31 ? c : 0);
  // s9: la frase de la nube ("Please select an ally for a battle.") es la tarea de 0x2452a0 de FUN_002a2550: anim 3 (la
  // pose de la nube) con la imagen del registro 0x40 (tex 8, 392x48). Medido en B2 (PCSX2): su texto queda 11 px sobre
  // el centro de la nube; faltaba y la nube salia en blanco.
  dib(c < 31 ? 3 : 4, c < 31 ? c : 0, 0, 64);
  dib(c < 21 ? 7 : 8, c < 21 ? c : 0);
  dib(13, std::min(c, 33.0f));
  const int quedan = std::max(0, g_elegir - int(g_marcadas.size()));
  if (quedan > 0) dib(16, std::min(c, 45.0f), 0, 19 + std::clamp(quedan, 1, 2));
  const int primero = std::clamp(g_comp - 2, 0, std::max(0, int(libres.size()) - 5));
  for (int fila = 0; fila < 5 && primero + fila < int(libres.size()); ++fila) {
    const int i = primero + fila, id = g_f[size_t(libres[size_t(i)])].c;
    const ImU32 color = i == g_comp ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255);
    {   // casilla: 0x27 (39) sin marcar / 0x2B (43) marcada (FUN_002A1E30); 38 es la entrada. [H] el cuadro final de 43 = marcada del todo
      const bool marcada = std::find(g_marcadas.begin(), g_marcadas.end(), libres[size_t(i)]) != g_marcadas.end();
      if (marcada) dib(43, 99.0f, 32.0f * fila);
      else dib(38, 0, 32.0f * fila);
    }
    int etiqueta = -1;
    for (int j = 0; j < 14; ++j) if (kAliados01[j] == id) etiqueta = 51 + j;
    if (etiqueta >= 0) dib(45, 0, 32.0f * fila, etiqueta, color);
    else S::MenuText(dl, L.P(345, 207 + 32.0f * fila), 16 * L.s, Nombre(g_f[size_t(libres[size_t(i)])]).c_str(), true, false);
  }
  if (primero + 5 < int(libres.size())) {   // flecha naranja de "hay mas filas" bajo la lista (original: centro (320,378) de 448)
    const ImVec2 q = L.P(320, 380);
    const float r = 9 * L.s;
    dl->AddTriangleFilled(ImVec2(q.x - r, q.y - r * 0.7f), ImVec2(q.x + r, q.y - r * 0.7f), ImVec2(q.x, q.y + r * 0.7f), IM_COL32(235, 120, 30, 255));
  }
  if (g_conf >= 0) {   // "Is this okay?": marco 10, tres textos 65/66/67 con la animacion 68 y el cursor 35 sobre el elegido
    const float cc = std::max(0.0f, g_conf_t * 60.0f);
    dib(10, cc);
    // medido en CMN 399: 65 = titulo (320,140), 66 = "This is okay." (330,195), 67 = "No, wait a minute." (330,235), cada uno con su
    // propia pose; el cursor (35) esta en (180,195) y baja 40 hasta el No
    dib(65, cc, 0, 65);
    dib(66, cc, 0, 66, g_conf == 0 ? IM_COL32_WHITE : IM_COL32(190, 190, 190, 255));
    dib(67, cc, 0, 67, g_conf == 1 ? IM_COL32_WHITE : IM_COL32(190, 190, 190, 255));
    dib(35, cc, 40.0f * float(g_conf));
  }
  {   // barra de ayuda de abajo (original: x 40-565, y 394-424 de 448, negra al 30 %): Seleccionar / Confirmar / Cancelar
    const ImVec2 b0 = L.P(40, 394), b1 = L.P(580, 424);
    dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 90));
    if (g_conf >= 0 || !g_marcadas.empty())
      S::HelpLine(dl, L.P(310, 409), 24 * L.s, {{"PAD", T::T("Seleccionar", "Select")}, {"A", T::T("Confirmar", "Confirm")}, {"B", T::T("Cancelar", "Cancel")}});
    else
      S::HelpLine(dl, L.P(310, 409), 24 * L.s, {{"PAD", T::T("Seleccionar", "Select")}, {"A", T::T("Confirmar", "Confirm")}});
  }
  return true;
}

// ---- Tarjeta "Battle!" (s9) ----
// Tras el contacto B2 enseña esta pantalla (savestate 5 del emulador; textos y capas medidos con el CMN 408 / 410 y
// DATA_<idioma> 189-190): degradado azul (22,8,84) -> (63,91,209) de arriba a abajo; a los 60-80 cuadros el titulo (anim 6,
// textura 2) entra de la derecha hasta x = 5, y a los 70-80 el pergamino (anim 7, textura 0) entra de la izquierda hasta
// x = 258, y = 240, con el texto de la condicion (189 + condicion, 352x96) llegando a x = 234; abajo, la barra oscura
// (x 33-597, y 394-427, 30 % negro) con "X: Next / START: Start battle". El personaje del jugador va a la derecha (cabeza a
// y = 19, de pie hasta salirse por abajo). [H] la entrada del personaje y su pose: solo se ve la tarjeta ya asentada.
void DibFicha(ImDrawList* dl, const Ficha& f, ImVec2 base, float w, float t, ImU32 col);
bool DibGoku3DTarjeta(ImDrawList* dl, ImVec2 pie, float alto_px, float t, float giro);
bool DibTarjeta(ImDrawList* dl, const S::Area& a, float t, float c, int condicion, const Ficha& j) {
  static std::vector<Tex> tx, tc;
  static bool hecho = false;
  if (!hecho) {
    auto* dr = dbz3::launcher::PreviewDrawer();
    if (!dr) return false;
    hecho = true;
    auto crear = [&](const std::vector<Imagen>& v, std::vector<Tex>& out) {
      for (const Imagen& im : v) {
        Tex x;
        if (im) {
          x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
          x.w = float(im.w);
          x.h = float(im.h);
        }
        out.push_back(std::move(x));
      }
    };
    crear(g_d.tarjeta, tx);
    crear(g_d.tarjeta_cond, tc);
  }
  if (g_d.tarjeta_ama.size() < 8 || tx.size() < 3 || !tx[0] || !tx[2]) return false;
  const Lienzo L = LienzoDe(a);
  const ImVec2 W = ImGui::GetIO().DisplaySize;
  dl->AddRectFilledMultiColor(ImVec2(0, 0), W, IM_COL32(22, 8, 84, 255), IM_COL32(22, 8, 84, 255), IM_COL32(63, 91, 209, 255),
                              IM_COL32(63, 91, 209, 255));
  DibNativa01(dl, g_d.tarjeta_ama, tx, 7, L, c);   // pergamino
  DibNativa01(dl, g_d.tarjeta_ama, tx, 6, L, c);   // titulo
  const size_t ic = size_t(std::clamp(condicion, 0, 5));
  if (ic < tc.size() && tc[ic]) {   // el texto de la condicion: x de -252 a 234 entre los cuadros 70 y 80 (CMN 410)
    const float u = std::clamp((c - 70.0f) / 10.0f, 0.0f, 1.0f), x = -252.0f + 486.0f * u;
    dl->AddImage(tc[ic].id(), L.P(x - tc[ic].w * 0.5f, 240 - tc[ic].h * 0.5f), L.P(x + tc[ic].w * 0.5f, 240 + tc[ic].h * 0.5f));
  }
  if (c >= 80.0f) {
    // personaje: a la derecha, cabeza en y = 19 (448), pies fuera de la pantalla
    const ImVec2 pie = L.P(518.0f, 452.0f);
    // A2c: el modelo de B2 con la pose de B2 (el cargador de la rueda de la nave: registro 0x389540 + id * 0x2C8 y banco CMN 931)
    static const float giro_fig = std::getenv("DBZ3_TEST_FIG_GIRO") ? float(std::atof(std::getenv("DBZ3_TEST_FIG_GIRO"))) : -0.45f;
    static const float alto_fig = std::getenv("DBZ3_TEST_FIG_ALTO") ? float(std::atof(std::getenv("DBZ3_TEST_FIG_ALTO"))) : 433.0f;
    // [H] la pose de CMN 931 no es la de la tarjeta (la nave tiene el mismo problema abierto: sus 7 animaciones dan brazos arriba o el
    // cuerpo plegado): el modelo de B2 con esa pose solo con DBZ3_TEST_FIG_TARJETA=1; por defecto el de la tarjeta (CMN 171, guardia).
    static const bool fig_b2 = std::getenv("DBZ3_TEST_SIN_FIG_TARJETA") == nullptr;   // pose de 931 ya absoluta (medida en PCSX2, 23/10)
    if (!(fig_b2 && dbz3::pasarela::nave::DibujarFiguraB2(dl, j.c, pie, alto_fig * L.s, giro_fig, 0.0f)) &&
        (j.c != kGoku || !DibGoku3DTarjeta(dl, pie, 413.0f * L.s, t, std::getenv("DBZ3_TEST_ER_GIRO") ? float(std::atof(std::getenv("DBZ3_TEST_ER_GIRO"))) : -0.55f)))
      DibFicha(dl, j, pie, 300.0f * L.s, t, IM_COL32_WHITE);
    // barra de ayuda: 30 % de negro con el borde izquierdo desvanecido
    const ImVec2 b0 = L.P(33, 394), b1 = L.P(597, 427), bm = L.P(66, 427);
    dl->AddRectFilledMultiColor(b0, ImVec2(bm.x, b1.y), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 77), IM_COL32(0, 0, 0, 77),
                                IM_COL32(0, 0, 0, 0));
    dl->AddRectFilled(ImVec2(bm.x, b0.y), b1, IM_COL32(0, 0, 0, 77));
    // A2c: ayuda del original (medida en el lienzo 640x448): icono X gris en (269, 410) + ":Next", icono START + ":Start battle"
    auto boton = [&](float x, bool start, const char* txt) {
      const ImVec2 c = L.P(x, 410);
      if (!start) {
        dl->AddCircleFilled(c, 10 * L.s, IM_COL32(96, 104, 128, 255), 24);
        dl->AddLine(ImVec2(c.x - 5 * L.s, c.y - 5 * L.s), ImVec2(c.x + 5 * L.s, c.y + 5 * L.s), IM_COL32(40, 46, 70, 255), 2.4f * L.s);
        dl->AddLine(ImVec2(c.x + 5 * L.s, c.y - 5 * L.s), ImVec2(c.x - 5 * L.s, c.y + 5 * L.s), IM_COL32(40, 46, 70, 255), 2.4f * L.s);
      } else {   // START: el triangulo gris con "START" debajo
        const ImVec2 q[3] = {ImVec2(c.x - 6 * L.s, c.y - 8 * L.s), ImVec2(c.x - 6 * L.s, c.y + 4 * L.s), ImVec2(c.x + 7 * L.s, c.y - 2 * L.s)};
        dl->AddConvexPolyFilled(q, 3, IM_COL32(150, 154, 170, 255));
        dl->AddText(ImGui::GetFont(), 8 * L.s, ImVec2(c.x - 14 * L.s, c.y + 3 * L.s), IM_COL32(210, 214, 226, 255), "START");
      }
      const float fs = 21.0f * L.s;
      const ImVec2 tp = L.P(x + 16, 399);
      dl->AddText(ImGui::GetFont(), fs, ImVec2(tp.x + 1.5f * L.s, tp.y + 1.5f * L.s), IM_COL32(20, 20, 60, 255), txt);
      dl->AddText(ImGui::GetFont(), fs, tp, IM_COL32(255, 255, 255, 255), txt);
    };
    boton(269, false, ":Next");
    boton(413, true, T::T(":Empezar combate", ":Start battle"));
  }
  return true;
}

// Camara del original (FUN_0023ACB0 b2.c:191716, FUN_0023AF30 b2.c:191776): el tablero es 2D puro, SIN perspectiva ni
// zoom; el mapa va a pantalla como (mapa - camara) (FUN_0023B900 b2.c:192056; la resta esta en b2.c:190830-190856).
// 1 px del mapa = 1 px de la pantalla de 640x448 del original (FUN_00130410 b2.c:30597 fija 0x3A43B0/B4; la misma
// pantalla que usa el marcador del port, pasarela_b2.cpp:2599). Aqui `e` es la escala del port (alto de ventana /
// kTableroAlto) y `c`
// el centro de la ventana. Las constantes viejas (d=900, f=1800, sn=0.97, cs=0.24) eran un 3D falso puesto a ojo: no
// existen en el ELF.
constexpr float kTableroAlto = 448.0f;   // alto de la pantalla del original (0x3A43B4). [H]: confirmar con la sonda
                                         // (RUNBOOK 02, caso B); si midiera otro, cambiar solo este valor.
struct Cam {
  float cx = 0, cy = 0, e = 1;
  ImVec2 c;
  bool P(float mx, float my, ImVec2& out, float* esc = nullptr) const {
    out = ImVec2(c.x + (mx - cx) * e, c.y + (my - cy) * e);
    if (esc) *esc = e;
    return true;
  }
};

// Retrato hexagonal de una ficha (342+5k: 2 * indice), Saibaman (398 #10, 3 cuadros) o Goku (sin ficha en B2:
// hexagono azul como los de los companeros con su cara de B3; ponytail, en el original seguramente es su modelo 3D).
void DibFicha(ImDrawList* dl, const Ficha& f, ImVec2 base, float w, float t, ImU32 col) {
  if (f.c == kSaibaman) {
    const int k = int(t * 5) % 3;
    const float h = w * 80.0f / 104.0f;
    DibIcono(dl, 10, ImVec2(base.x - w * 0.5f, base.y - h), ImVec2(base.x + w * 0.5f, base.y),
             ImVec4(104.0f * float(k), 0, 104.0f * float(k + 1), 80), col);
    return;
  }
  const size_t r = size_t(f.idx) * 2;
  if (!(f.c == kGoku && f.bando == 4) && r < g_tfichas.size() && g_tfichas[r]) {
    dl->AddImage(g_tfichas[r].id(), ImVec2(base.x - w * 0.5f, base.y - w), ImVec2(base.x + w * 0.5f, base.y), ImVec2(0, 0),
                 ImVec2(1, 1), col);
    return;
  }
  const ImVec2 c(base.x, base.y - w * 0.43f);
  const float R = w * 0.40f;
  ImVec2 v[6];
  for (int k = 0; k < 6; ++k) {
    const float a = 3.14159265f / 3.0f * float(k);
    v[k] = ImVec2(c.x + R * std::cos(a), c.y + R * std::sin(a) * 0.95f);
  }
  dl->AddConvexPolyFilled(v, 6, IM_COL32(70, 150, 230, (col >> 24) & 0xFF));
  if (ImTextureID cara = CaraDe(B2aB3(f.c))) {
    dl->PushTexture(ImTextureRef(cara));
    const ImDrawIdx b = ImDrawIdx(dl->_VtxCurrentIdx);
    dl->PrimReserve(18, 7);
    auto uv = [&](ImVec2 p) { return ImVec2(0.5f + (p.x - c.x) / (2.3f * R), 0.5f + (p.y - c.y) / (2.3f * R)); };
    dl->PrimWriteVtx(c, uv(c), col);
    for (int k = 0; k < 6; ++k) dl->PrimWriteVtx(v[k], uv(v[k]), col);
    for (int k = 0; k < 6; ++k) {
      dl->PrimWriteIdx(b);
      dl->PrimWriteIdx(ImDrawIdx(b + 1 + k));
      dl->PrimWriteIdx(ImDrawIdx(b + 1 + (k + 1) % 6));
    }
    dl->PopTexture();
  }
  dl->AddPolyline(v, 6, IM_COL32(40, 50, 220, (col >> 24) & 0xFF), ImDrawFlags_Closed, std::max(2.0f, w * 0.05f));
}

// Tramos de vida (398 #9: 16x16 verde claro y oscuro) apilados en vertical junto a la ficha: uno por barra.
void Barras(ImDrawList* dl, const Ficha& f, ImVec2 base, float e) {
  // R7: columna contigua de 5 bloques (las vidas, +1 del registro, van de 0 a 5: FUN_00279F60), llenos los que le quedan;
  // celdas de 398 #9 (16x16, llena / vacia) a su tamano del original
  // aplicar_estudios (entrega 17 sec. 6): la celda que cambia hace la anim 0x36 (sube, 81 cuadros) o 0x37 (baja, 41)
  // del #AMA 397 y la barra se cuenta al acabar (FUN_00274210 b2.c:220366-220390); las demas, 0x35 llena / 0x34 vacia.
  const auto& la = g_d.life_ama;
  const bool ama = la.size() > 0x37;
  // PRUEBA: DBZ3_TEST_B2_VIDA=1 quita y devuelve una barra (solo en pantalla) cada 3 s para ver las dos animaciones
  static const bool prueba = std::getenv("DBZ3_TEST_B2_VIDA") != nullptr;
  const int meta = prueba ? std::max(0, f.barras - int(g_ahora / 3.0f) % 2) : f.barras;
  if (f.barras_vis < 0 || !ama) f.barras_vis = meta;
  int anim = 0, cambia = -1;
  float c = 0;
  if (f.barras_vis != meta) {
    anim = meta > f.barras_vis ? 0x36 : 0x37;
    cambia = anim == 0x36 ? f.barras_vis : f.barras_vis - 1;
    if (f.vida_t0 < 0) f.vida_t0 = g_ahora;
    c = (g_ahora - f.vida_t0) * 60.0f;
    if (c > la[size_t(anim)].dur) {
      f.barras_vis += anim == 0x36 ? 1 : -1;
      f.vida_t0 = -1, anim = 0, cambia = -1;
    }
  }
  for (int k = 0; k < 5; ++k) {
    const float y1 = base.y - float(k) * 12 * e;   // las celdas traen 2 px de margen: pegadas, sin hueco
    ImVec2 p0(base.x - 6 * e, y1 - 16 * e), p1(base.x + 10 * e, y1);
    if (!ama) {
      DibIcono(dl, 9, p0, p1, k < f.barras ? ImVec4(0, 0, 16, 16) : ImVec4(16, 0, 32, 16));
      continue;
    }
    const Datos::Sprite& sp = la[size_t(k == cambia ? anim : k < f.barras_vis ? 0x35 : 0x34)];
    if (k == cambia) {   // escala y sacudida de las claves, alrededor del centro de la celda
      const Pose po = PoseDe(sp, c);
      const ImVec2 m((p0.x + p1.x) * 0.5f + (po.px - sp.px) * e, (p0.y + p1.y) * 0.5f + (po.py - sp.py) * e);
      const float hx = (p1.x - p0.x) * 0.5f * po.ex, hy = (p1.y - p0.y) * 0.5f * po.ey;
      p0 = ImVec2(m.x - hx, m.y - hy), p1 = ImVec2(m.x + hx, m.y + hy);
    }
    DibIcono(dl, sp.tex >= 0 ? sp.tex : 9, p0, p1, ImVec4(sp.u0, sp.v0, sp.u1, sp.v1));
  }
}
// Cifras azules del original (398 #6: "123450"); las que no tiene (6-9), con las de 398 #29.
void CifrasAzules(ImDrawList* dl, int n, ImVec2 c, float h) {
  const std::string s = std::to_string(std::max(0, n));
  float x = c.x - float(s.size()) * h * 0.4f;
  for (char ch : s) {
    const int d = ch - '0';
    if (d <= 5) {
      const int celda = d == 0 ? 5 : d - 1;
      DibIcono(dl, 6, ImVec2(x - h * 0.1f, c.y - h * 0.5f), ImVec2(x + h * 0.9f, c.y + h * 0.5f),
               ImVec4(32.0f * float(celda), 0, 32.0f * float(celda + 1), 32));
    } else {
      DibIcono(dl, 29, ImVec2(x + h * 0.1f, c.y - h * 0.6f), ImVec2(x + h * 0.85f, c.y + h * 0.4f),
               ImVec4(24.0f * float(d), 0, 24.0f * float(d + 1), 32), IM_COL32(120, 160, 255, 255));
    }
    x += h * 0.8f;
  }
}
// Cifras: 398 #28 (0-9 de 24x24).
void Cifras(ImDrawList* dl, int n, ImVec2 p, float h) {
  const std::string s = std::to_string(std::max(0, n));
  for (size_t i = 0; i < s.size(); ++i) {
    const int d = s[i] - '0';
    DibIcono(dl, 28, ImVec2(p.x + float(i) * h * 0.85f, p.y), ImVec2(p.x + float(i) * h * 0.85f + h, p.y + h),
             ImVec4(24.0f * float(d), 0, 24.0f * float(d + 1), 24));
  }
}

// Fila de sprites del Saibaman en el #AMA 343+5k. ELF: tabla 0x379080 (fase I: 2 filas), 0x3790B0 (fase IV: 5) y
// 0x379110 (fase IX: 5); FUN_0027a820 (b2.c:224266-224281) elige la tabla por la fase y FUN_002762b0
// (b2.c:221415-221430) la fila por el modo del registro (+0x1E) y la mitad por su anim (4 = brote: FUN_002758e0
// b2.c:221021). Cada fila = 9 shorts: los indices hasta el -1 (reposo, 3) y los siguientes (brote, 4).
const int* FilaSaibaman(int fase, int modo, int& n_reposo, int& n_brote) {
  static const int kFI[2][7] = {{43, 42, 41, 47, 46, 45, 44}, {73, 72, 71, 77, 76, 75, 74}};
  static const int kFIV[5][7] = {{20, 19, 18, 159, 158, 157, 156}, {35, 34, 33, 163, 162, 161, 160},
                                 {50, 49, 48, 167, 166, 165, 164}, {65, 64, 63, 171, 170, 169, 168},
                                 {80, 79, 78, 175, 174, 173, 172}};
  static const int kFIX[5][7] = {{18, 17, 16, 82, 81, 80, 79}, {33, 32, 31, 86, 85, 84, 83},
                                 {48, 47, 46, 90, 89, 88, 87}, {63, 62, 61, 94, 93, 92, 91},
                                 {78, 77, 76, 98, 97, 96, 95}};
  const int(*t)[7] = fase == 0 ? kFI : fase == 3 ? kFIV : fase == 8 ? kFIX : nullptr;
  const int n = fase == 0 ? 2 : 5;
  if (!t || modo < 0 || modo >= n) return nullptr;
  n_reposo = 3;
  n_brote = 4;
  return t[modo];
}
// Una pieza sobre el tablero; devuelve la y de pantalla de su parte de arriba.
//   Saibaman: sus capas de sprites del #AMA 343+5k (fila de la fase y el modo; ver FilaSaibaman).
//   Goku: de cuerpo entero (idioma 155, el sprite de 64x88 de la intro). Los demas personajes del original son modelos 3D;
//   ponytail: aqui, su retrato hexagonal de pie sobre la peana (el dorso con la Z de su ficha, plano en el suelo).
float DibPieza(ImDrawList* dl, const Cam& k, const Ficha& f, ImVec2 pos, float t, ImU32 col) {
  ImVec2 p;
  float e = 1;
  if (!k.P(pos.x, pos.y, p, &e)) return -1;
  float arriba = p.y - 68 * e;
  // R7: el brote dura lo que el primer efecto de su fila (FUN_002762B0, tablas 0x379080/0x3790B0/0x379110 sobre el banco
  // 343+5k): 48 cuadros (66 con movimiento tipo 4)
  const float brote = std::clamp((g_ahora - f.brote_t) / ((f.mov == 4 ? 66.0f : 48.0f) / 60.0f), 0.0f, 1.0f);
  if (brote < 1.0f && f.c != kSaibaman) {   // aparece: destello que se cierra sobre la pieza
    const float r = (1.0f - brote) * 70.0f * e;
    dl->AddCircleFilled(ImVec2(p.x, p.y - 30 * e), r, IM_COL32(255, 255, 220, int(160 * (1.0f - brote))));
  }
  // estado para las animaciones de las fichas (CMN 334): en combate (FUN_00277890 b2.c:222312) y buscando
  // (FUN_00277d40 b2.c:222523; el "?!" sobre su casilla)
  const bool en_lucha = g_pend_j >= 0 && (&f == &g_f[size_t(g_pend_j)] || &f == &g_f[size_t(g_pend_r)]);
  const float busca = g_marca_icono == 7 && g_marca == f.casilla ? g_ahora - g_marca_t : -1;
  if (f.c == kSaibaman) {
    // R7 (b2.c:221415-221430): el Saibaman son CAPAS de sprites del #AMA 343+5k (d.textos, con las texturas de 146+k):
    // brotando, la segunda mitad de su fila (fase I: 47,46,45,44) hasta que acaba la duracion del primero (47 = 66
    // cuadros); en reposo, la primera (43,42,41) en bucle. Antes: un agujero y una cabeza propios (idioma 146 #13/#16).
    int nr = 0, nb = 0;
    const int* fila = FilaSaibaman(g_d.fase, f.modo, nr, nb);
    auto en_ama = [&](int q) { return q >= 0 && q < int(g_d.textos.size()); };
    if (fila && en_ama(fila[nr])) {
      Lienzo L;
      L.o = p;
      L.s = e;
      const float tr = (g_ahora - f.brote_t) * 60.0f;
      const bool brotando = f.brote_t >= 0 && g_d.textos[size_t(fila[nr])].dur > 0 &&
                            tr < g_d.textos[size_t(fila[nr])].dur;
      const float cuadro = brotando ? tr : std::fmod(std::max(0.0f, tr), std::max(1.0f, g_d.textos[size_t(fila[0])].dur));
      const int desde = brotando ? nr : 0, cuantas = brotando ? nb : nr;
      for (int j = 0; j < cuantas; ++j) {
        const int q = fila[desde + j];
        if (en_ama(q) && g_d.textos[size_t(q)].tex >= 0)
          DibSp(dl, g_d.textos, g_tloc, q, L, cuadro, col, -g_d.textos[size_t(q)].px, -g_d.textos[size_t(q)].py);
      }
    } else if (g_thoyo && g_tcabezas) {   // sin el #AMA (otra ISO): los sprites propios de antes
      dl->AddImage(g_thoyo.id(), ImVec2(p.x - 36 * e, p.y - 18 * e), ImVec2(p.x + 36 * e, p.y + 18 * e));
      const int n = int(t * 3) % 3;
      const float alto = 56 * e * brote;
      dl->AddImage(g_tcabezas.id(), ImVec2(p.x - 28 * e, p.y + 8 * e - alto), ImVec2(p.x + 28 * e, p.y + 8 * e),
                   ImVec2(float(n) / 3, 0), ImVec2(float(n + 1) / 3, brote), col);
      if (brote < 1.0f)
        for (int q = 0; q < 6; ++q) {
          const float an = float(q) * 1.047f + brote * 2.0f, d = brote * 40 * e;
          dl->AddCircleFilled(ImVec2(p.x + std::cos(an) * d, p.y - std::fabs(std::sin(an)) * d * 0.6f),
                              4 * e * (1.0f - brote), IM_COL32(120, 80, 40, 220));
        }
    }
    arriba = p.y - 48 * e;
  } else if (f.c == kGoku && f.bando == 4 && (g_tgoku || g_goku3d.malla)) {
    dl->AddEllipseFilled(ImVec2(p.x, p.y), ImVec2(22 * e, 7 * e), IM_COL32(0, 0, 0, 90));
    // en 3D (su modelo del tablero de B2, andando durante el paso); sin el, el sprite de la intro
    g_goku_giro = GiroDeOrden(f, g_ahora);   // gira solo al moverse (o al angulo fijo de 61/62) y vuelve a mirar al frente
    if (!DibGoku3D(dl, p, 104 * e, t, Andando(f, g_ahora), en_lucha ? std::max(0.001f, g_ahora - g_pend_t) : 0,
                   busca >= 0 && busca < 36.0f / 60.0f ? std::max(0.001f, busca) : 0) && g_tgoku)
      dl->AddImage(g_tgoku.id(), ImVec2(p.x - 29 * e, p.y - 80 * e), ImVec2(p.x + 29 * e, p.y), ImVec2(0, 0), ImVec2(1, 1), col);
    arriba = p.y - 104 * e;
  } else if (DibFicha3D(dl, f.idx, p, 96 * e, t + float(f.n) * 0.37f, GiroDeOrden(f, g_ahora), Andando(f, g_ahora), en_lucha,
                        busca, f.anim_b2, g_ahora - f.anim_t0, f.anim_estado)) {
    arriba = p.y - 96 * e;   // la ficha 3D del original (CMN 335) con el retrato y el dorso de su pieza
  } else {
    const size_t dorso = size_t(f.idx) * 2 + 1;
    ImVec2 v[7];
    bool ok = f.c != kSaibaman && dorso < g_tfichas.size() && g_tfichas[dorso] && k.P(pos.x, pos.y, v[0]);
    for (int i = 0; ok && i < 6; ++i) {
      const float an = 3.14159265f / 3.0f * float(i);
      ok = k.P(pos.x + 30 * std::cos(an), pos.y + 22 * std::sin(an), v[i + 1]);
    }
    if (ok) {   // el hexagono de la Z (centro 0.41, radio 0.36 de la textura 128x128)
      dl->PushTexture(ImTextureRef(g_tfichas[dorso].id()));
      const ImDrawIdx b = ImDrawIdx(dl->_VtxCurrentIdx);
      dl->PrimReserve(18, 7);
      dl->PrimWriteVtx(v[0], ImVec2(0.41f, 0.41f), col);
      for (int i = 0; i < 6; ++i) {
        const float an = 3.14159265f / 3.0f * float(i);
        dl->PrimWriteVtx(v[i + 1], ImVec2(0.41f + 0.36f * std::cos(an), 0.41f + 0.36f * std::sin(an)), col);
      }
      for (int i = 0; i < 6; ++i) {
        dl->PrimWriteIdx(b);
        dl->PrimWriteIdx(ImDrawIdx(b + 1 + i));
        dl->PrimWriteIdx(ImDrawIdx(b + 1 + (i + 1) % 6));
      }
      dl->PopTexture();
    }
    DibFicha(dl, f, ImVec2(p.x, p.y - 6 * e), 70 * e, t, col);   // A2c: 1,12x (medido contra el original)
  }
  Barras(dl, f, ImVec2(p.x - 40 * e, p.y), e);
  if (f.rival && f.aturdido > 0) {   // aturdido: la nube de pensar (398 #3, la derecha) con el remolino (398 #4)
    const ImVec2 c(p.x + 34 * e, arriba - 18 * e);
    DibIcono(dl, 3, ImVec2(c.x - 28 * e, c.y - 24 * e), ImVec2(c.x + 28 * e, c.y + 24 * e), ImVec4(56, 0, 112, 48));
    const int n = int(t * 4) % 2;
    DibIcono(dl, 4, ImVec2(c.x - 14 * e, c.y - 16 * e), ImVec2(c.x + 14 * e, c.y + 12 * e),
             ImVec4(32.0f * float(n), 0, 32.0f * float(n + 1), 32));
  }
  return arriba;
}

ImVec2 PosFicha(const Ficha& f) {
  if (f.punto != 0) {   // ordenes 65/66: punto del mapa, sin tocar la casilla logica
    const float k = f.punto_dur > 0 ? std::clamp((g_ahora - f.punto_t0) / f.punto_dur, 0.0f, 1.0f) : 1.0f;
    return ImVec2(f.punto_x0 + (f.punto_x - f.punto_x0) * k, f.punto_y0 + (f.punto_y - f.punto_y0) * k);
  }
  const Casilla &a = g_d.casillas[size_t(f.desde)], &b = g_d.casillas[size_t(f.casilla)];
  // lineal, tras el giro (0x273630: inicio + direccion * 4 * cuadros)
  const float k = std::clamp((g_ahora - f.t_mov - f.t_giro) / std::max(f.t_dur, 0.01f), 0.0f, 1.0f);
  // hueco 1 de la casilla: (+60, -20) (FUN_00273950 -> 0x2739A8)
  const float ax = a.x + (f.slot_ant ? 60.0f : 0.0f), ay = a.y + (f.slot_ant ? -20.0f : 0.0f);
  const float bx = b.x + (f.slot ? 60.0f : 0.0f), by = b.y + (f.slot ? -20.0f : 0.0f);
  return ImVec2(ax + (bx - ax) * k, ay + (by - ay) * k);
}

// studies 2 G04: ordenes 65/66 (FUN_002774F0). Arranca el movimiento al punto (x, y) a 4 px por cuadro (240 px/s) o,
// si ya va a ese mismo punto, devuelve lo que le queda. Al llegar devuelve 2 cuadros (retorno 2 del original). El hilo
// de la orden 66 espera exactamente ese tiempo; la 65 lo ignora y la pieza sigue andando sola.
float IrAPunto(Ficha& f, int orden, float x, float y, float ahora) {
  if (f.punto == 1 && f.punto_orden == orden && f.punto_x == x && f.punto_y == y) {
    const float resta = f.punto_t0 + f.punto_dur - ahora;
    return resta > 0.0f ? resta : 2.0f / 60.0f;
  }
  const ImVec2 p = PosFicha(f);
  const float dx = x - p.x, dy = y - p.y, d = std::hypot(dx, dy);
  const float g_ant = GiroDeOrden(f, ahora);   // continua desde el angulo fijo de 61/62 si lo habia
  f.giro_obj_activo = false;   // andar al punto vuelve a mandar sobre el angulo de 61/62
  f.punto = 1;
  f.punto_orden = orden;
  f.punto_x = x;
  f.punto_y = y;
  f.punto_x0 = p.x;
  f.punto_y0 = p.y;
  f.punto_t0 = ahora;
  f.punto_dur = d / 240.0f;   // 4 px por cuadro de 60 Hz
  if (d > 0.001f) {   // mira hacia el punto y anda (el giro del original es el mismo de los pasos por casilla)
    const float g = g_ant;
    f.giro0 = g;
    f.giro1 = std::atan2(dx, dy);
    f.t_giro = f.jugador ? 0.0f : std::fabs(Dif(g, f.giro1)) / kVelGiro;
    if (f.jugador) f.giro0 = f.giro1;
    f.t_mov = ahora;
    f.t_dur = f.punto_dur;
  }
  return d > 0.001f ? f.punto_dur : 2.0f / 60.0f;
}

// Pasa a "llego" las piezas de las ordenes 65/66 y deja el registro una sola vez. Se llama cada cuadro antes de los
// guiones, asi la 66 se libera en el primer cuadro en que ya esta en el punto (y no antes).
void ActualizarPuntos() {
  for (Ficha& f : g_f) {
    if (f.punto != 1 || g_ahora < f.punto_t0 + f.punto_dur) continue;
    f.punto = 2;
    REXLOG_INFO("dbz3 tablero b2: B2 orden {}: pieza={} llego a ({:.0f}, {:.0f})", f.punto_orden, f.n, f.punto_x,
                f.punto_y);
  }
}

void DibTablero(ImDrawList* dl, const S::Area& a, float t, bool prepara) {
  const ImVec2 W = ImGui::GetIO().DisplaySize;
  // Camara (RE sobre b2.elf, tabla de ordenes 0x379EC0): la del guion mientras corre (ordenes 9 y 11, los dos casos
  // lineales que usan los 9 #YDW: FUN_00295BD0 b2.c:239377 y FUN_00295B50 b2.c:239345); eligiendo destino, el punto
  // medio entre la ficha y la casilla resaltada (FUN_00276F50 b2.c:221885, llamado en FUN_00236EA0 b2.c:189750); en la
  // fase enemiga y mientras una ficha anda, la ficha que se mueve (FUN_00276FF0 b2.c:221903; FUN_00235E70 b2.c:189142,
  // FUN_002365D0 b2.c:189388); en el contacto, la ficha que se movio (FUN_00235780 b2.c:188931 y FUN_00236420
  // b2.c:189366), no el punto medio. La camara del original es su esquina superior izquierda y esta acotada cada cuadro
  // a [0, mapa - vista] (FUN_0023ACB0 b2.c:191749-191766); aqui g_cam_x/y es el centro de la vista.
  ImVec2 obj(g_d.ancho * 0.5f, g_d.alto * 0.5f);
  if (!HilosOcupados()) g_cam_script = false;
  if (!prepara && g_sel < int(g_f.size())) obj = PosFicha(g_f[size_t(g_sel)]);
  if (!prepara && g_dest > 0 && g_dest < int(g_d.casillas.size())) {   // eligiendo destino (FUN_00276F50)
    const ImVec2 a = PosFicha(g_f[size_t(g_sel)]);
    const Casilla& c = g_d.casillas[size_t(g_dest)];
    obj = ImVec2((a.x + c.x) * 0.5f, (a.y + c.y) * 0.5f);
  }
  if (!prepara && g_cam_rival >= 0 && g_cam_rival < int(g_f.size())) obj = PosFicha(g_f[size_t(g_cam_rival)]);
  if (!prepara && g_pend_j >= 0 && g_pend_r >= 0 && g_pend_mov >= 0 && g_pend_mov < int(g_f.size()))
    obj = PosFicha(g_f[size_t(g_pend_mov)]);   // el contacto sigue a la ficha que se movio (uGpffff88d4)
  if (!prepara && g_cam_script) {
    if (g_cam_pieza >= 0 && g_cam_pieza < int(g_f.size())) obj = PosFicha(g_f[size_t(g_cam_pieza)]);
    else if (g_cam_casilla > 0 && g_cam_casilla < int(g_d.casillas.size()))
      obj = ImVec2(g_d.casillas[size_t(g_cam_casilla)].x, g_d.casillas[size_t(g_cam_casilla)].y);
  }
  Cam k;
  // I01 (studies 4): el mundo usa el MISMO lienzo que el marcador, la ayuda y los menus (640x448 del
  // original centrado en el area 4:3, ver LienzoDe(a)). Antes usaba la ventana entera: en 16:9 se veian
  // 796 px de mapa en vez de 640 (+24 %) y el mapa salia un 7 % mas grande que su propio HUD.
  k.c = ImVec2(a.p0.x + 600.0f * a.s, a.p0.y + 450.0f * a.s);
  k.e = 1200.0f * a.s / 640.0f;
  // Vista en px del mapa = la pantalla del original (640x448), no la ventana: mismo campo de vision que
  // el original. Los laterales que sobran en 16:9 los rellena el propio mapa con las copias en espejo de
  // abajo (ox = -1..1), y la esquina se acota a [0, mapa - vista] (FUN_0023ACB0 b2.c:191754-191765).
  const float vista_x = 640.0f, vista_y = kTableroAlto;
  const float max_x = std::max(0.0f, g_d.ancho - vista_x), max_y = std::max(0.0f, g_d.alto - vista_y);
  obj.x = std::clamp(obj.x - vista_x * 0.5f, 0.0f, max_x) + vista_x * 0.5f;
  obj.y = std::clamp(obj.y - vista_y * 0.5f, 0.0f, max_y) + vista_y * 0.5f;
  const float dt = dbz3::RelojDt(std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f));
  if (g_cam_script && g_cam_lineal) {   // FUN_0023ACB0: 5 px por cuadro y por eje, y se pega al llegar
    const float v = 300.0f * dt;
    g_cam_x = std::fabs(obj.x - g_cam_x) <= v ? obj.x : g_cam_x + (obj.x > g_cam_x ? v : -v);
    g_cam_y = std::fabs(obj.y - g_cam_y) <= v ? obj.y : g_cam_y + (obj.y > g_cam_y ? v : -v);
  } else {   // FUN_0023AF30: 1/12 de lo que falta en cada cuadro de 60 Hz
    const float f = 1.0f - std::pow(11.0f / 12.0f, dt * 60.0f);
    g_cam_x += (obj.x - g_cam_x) * f;
    g_cam_y += (obj.y - g_cam_y) * f;
  }
  // el original acota la camara cada cuadro (no solo el destino): mientras viaja tampoco sale del mapa
  g_cam_x = std::clamp(g_cam_x - vista_x * 0.5f, 0.0f, max_x) + vista_x * 0.5f;
  g_cam_y = std::clamp(g_cam_y - vista_y * 0.5f, 0.0f, max_y) + vista_y * 0.5f;
  k.cx = g_cam_x;
  k.cy = g_cam_y;
  g_foco_ok = g_pend_r >= 0 && g_pend_r < int(g_f.size());
  if (g_foco_ok) {   // A2c: el panel comico P/T centra el contacto en su recuadro
    const Casilla& cf = g_d.casillas[size_t(g_f[size_t(g_pend_r)].casilla)];
    k.P(cf.x, cf.y, g_foco_scr);
  }
  dl->AddRectFilledMultiColor(ImVec2(0, 0), W, IM_COL32(18, 36, 80, 255), IM_COL32(18, 36, 80, 255),
                              IM_COL32(6, 14, 34, 255), IM_COL32(6, 14, 34, 255));
  // T7 tipo 4: capa de fondo a pantalla completa, DEBAJO del mapa (las fases I, III, V, VI y VII la traen).
  // El mapa es opaco salvo en sus huecos; si el original la pinta encima, se ve el efecto en las capturas.
  if (!std::getenv("DBZ3_TEST_B2_NO_CAPAS")) {
    for (const auto& ad : g_d.colocados) {
      if (ad.tipo != 4 || ad.obj < 0 || ad.obj >= int(g_d.sprites.size())) continue;
      const Datos::Sprite& sp = g_d.sprites[size_t(ad.obj)];
      if (sp.tex < 0 || sp.tex >= int(g_tadornos.size()) || !g_tadornos[size_t(sp.tex)]) continue;
      const Tex& x = g_tadornos[size_t(sp.tex)];
      const float u0 = std::clamp(sp.u0, 0.0f, x.w), v0 = std::clamp(sp.v0, 0.0f, x.h);
      const float u1 = std::clamp(sp.u1, u0, x.w), v1 = std::clamp(sp.v1, v0, x.h);
      dl->AddImage(x.id(), ImVec2(0, 0), W, ImVec2(u0 / x.w, v0 / x.h), ImVec2(u1 / x.w, v1 / x.h));
    }
  }
  // terreno: las piezas del mapa en 4x4 trozos (la perspectiva de AddImageQuad es por triangulo). Con la camara
  // inclinada se ve mas ancho arriba que abajo: alrededor del mapa van copias en espejo (izquierda, derecha y detras)
  // para que no asomen esquinas vacias; los bordes casan porque el espejo repite el mismo pixel.
  constexpr int kTrozos = 4;
  for (int oy = -1; oy <= 0; ++oy)
  for (int ox = -1; ox <= 1; ++ox)
  for (size_t j = 0; j < g_tmapa.size(); ++j) {
    if (!g_tmapa[j]) continue;
    const float x0 = 256.0f * float(int(j) % g_d.cols), y0 = 256.0f * float(int(j) / g_d.cols);
    const float pw = std::min(256.0f, g_d.ancho - x0), ph = std::min(256.0f, g_d.alto - y0);
    if (pw <= 0 || ph <= 0) continue;
    auto espejo = [&](float mx, float my, ImVec2& out) {
      mx = ox < 0 ? -mx : ox > 0 ? 2 * g_d.ancho - mx : mx;
      my = oy < 0 ? -my : my;
      return k.P(mx, my, out);
    };
    for (int a = 0; a < kTrozos; ++a) {
      for (int b = 0; b < kTrozos; ++b) {
        const float u0 = pw * float(a) / kTrozos, u1 = pw * float(a + 1) / kTrozos;
        const float v0 = ph * float(b) / kTrozos, v1 = ph * float(b + 1) / kTrozos;
        ImVec2 p[4];
        if (!espejo(x0 + u0, y0 + v0, p[0]) || !espejo(x0 + u1, y0 + v0, p[1]) || !espejo(x0 + u1, y0 + v1, p[2]) ||
            !espejo(x0 + u0, y0 + v1, p[3]))
          continue;
        if (std::max({p[0].y, p[1].y, p[2].y, p[3].y}) < 0 || std::min({p[0].y, p[1].y, p[2].y, p[3].y}) > W.y ||
            std::max({p[0].x, p[1].x, p[2].x, p[3].x}) < 0 || std::min({p[0].x, p[1].x, p[2].x, p[3].x}) > W.x)
          continue;
        dl->AddImageQuad(g_tmapa[j].id(), p[0], p[1], p[2], p[3], ImVec2(u0 / 256, v0 / 256), ImVec2(u1 / 256, v0 / 256),
                         ImVec2(u1 / 256, v1 / 256), ImVec2(u0 / 256, v1 / 256));
      }
    }
  }
  // adornos de la fase (345+5k con su recorte del #AMA 344+5k) en sus sitios de la tabla 7; bajo los caminos.
  // Tipos 0/2/3 y 1 (grupo): se dibujan todos; 0/1/2/3 usan la lista A (pose) y la lista B (fotograma/textura).
  for (const auto& ad : g_d.colocados) {
    if (ad.tipo == 4) continue;   // capa de fondo: va antes del mapa
    if (ad.obj < 0 || ad.obj >= int(g_d.sprites.size())) continue;
    const Datos::Sprite& sp = g_d.sprites[size_t(ad.obj)];
    const float cuadro = sp.dur > 0 ? std::fmod(std::max(0.0f, t) * 60.0f, sp.dur) : -1.0f;
    const Datos::Sprite::Recorte* rec = nullptr;
    for (const auto& r : sp.recortes) {
      if (r.cuadro > cuadro) break;   // recorte ya empezado; el +1 de u1/v1 del #AMA es inclusivo (datos medidos)
      rec = &r;
    }
    const int tex = rec ? rec->tex : sp.tex;
    if (tex < 0 || tex >= int(g_tadornos.size()) || !g_tadornos[size_t(tex)]) continue;
    const Tex& x = g_tadornos[size_t(tex)];
    ImVec2 p;
    float e = 1;
    if (!k.P(ad.x, ad.y, p, &e)) continue;
    const Pose po = PoseDe(sp, cuadro);
    // H02: dejar el recorte dentro de la textura (datos de las 9 fases medidos; el clamp es defensa para otras ISOs).
    const float u0 = std::clamp(rec ? rec->u0 : sp.u0, 0.0f, x.w);
    const float v0 = std::clamp(rec ? rec->v0 : sp.v0, 0.0f, x.h);
    const float u1 = std::clamp(rec ? rec->u1 : sp.u1, u0, x.w);
    const float v1 = std::clamp(rec ? rec->v1 : sp.v1, v0, x.h);
    const float x0 = sp.x0 * po.ex, x1 = sp.x1 * po.ex, y0 = sp.y0 * po.ey, y1 = sp.y1 * po.ey;
    dl->AddImage(x.id(), ImVec2(p.x + x0 * e, p.y + y0 * e), ImVec2(p.x + x1 * e, p.y + y1 * e),
                 ImVec2(u0 / x.w, v0 / x.h), ImVec2(u1 / x.w, v1 / x.h));
  }
  const int sel_c = prepara || g_sel >= int(g_f.size()) ? -1 : g_f[size_t(g_sel)].casilla;
  // caminos. s9, medido en B2 (PCSX2, capturas del slot 3/4): banda de ~8 px del mapa, verde (85,255,138) al ~48 % en el
  // centro con borde suave (FWHM 5 px, pie 11 px); el camino que vas a recorrer, blanco menta (245,255,250), 7 px, ~50 %,
  // sin pulso. Antes: 3 px, (150,255,200), muy fina.
  for (size_t i = 1; i < g_d.casillas.size(); ++i) {
    for (int v : g_d.casillas[i].vec) {
      if (v <= int(i)) continue;
      ImVec2 a, b;
      float ea = 1, eb = 1;
      if (!k.P(g_d.casillas[i].x, g_d.casillas[i].y, a, &ea) || !k.P(g_d.casillas[size_t(v)].x, g_d.casillas[size_t(v)].y, b, &eb))
        continue;
      const bool activo = (int(i) == sel_c && v == g_dest) || (v == sel_c && int(i) == g_dest);
      const float e = (ea + eb) * 0.5f;
      if (activo) {
        // medido (slot 3, destino = casilla del Saibaman): si en el destino hay un rival, el camino sale naranja
        // (228,190,132 en el centro sobre hierba (91,121,16)); si no, blanco menta
        const int dest_c = int(i) == sel_c ? v : int(i);
        const bool enemigo = FichaEn(dest_c, true) >= 0;
        const int cr = enemigo ? 250 : 245, cg = enemigo ? 200 : 255, cb = enemigo ? 140 : 250, k1 = enemigo ? 100 : 38, k2 = enemigo ? 100 : 50,
                  k3 = enemigo ? 100 : 64;
        dl->AddLine(a, b, IM_COL32(cr, cg, cb, k1), 9.0f * e);
        dl->AddLine(a, b, IM_COL32(cr, cg, cb, k2), 7.0f * e);
        dl->AddLine(a, b, IM_COL32(cr, cg, cb, k3), 4.0f * e);
      } else {
        dl->AddLine(a, b, IM_COL32(85, 255, 138, 38), 11.0f * e);
        dl->AddLine(a, b, IM_COL32(85, 255, 138, 55), 8.0f * e);
        dl->AddLine(a, b, IM_COL32(85, 255, 138, 55), 5.0f * e);
      }
    }
  }
  // casillas (398): disco azul pequeno (#0), Dende (#11), Mr. Popo en su alfombra (#16, #18), combate (#14 con el icono de
  // su condicion). Ya buscadas: en gris.
  for (size_t i = 1; i < g_d.casillas.size(); ++i) {
    const Casilla& c = g_d.casillas[i];
    ImVec2 p;
    float e = 1;
    if (!k.P(c.x, c.y, p, &e)) continue;
    const bool buscada = !prepara && i < g_buscada.size() && g_buscada[i];
    const ImU32 col = buscada ? IM_COL32(140, 140, 140, 255) : IM_COL32_WHITE;
    if (c.tipo == 2) {   // Dende: el disco grande con aro dorado
      DibIcono(dl, 11, ImVec2(p.x - 24 * e, p.y - 16 * e), ImVec2(p.x + 24 * e, p.y + 16 * e), ImVec4(0, 0, 0, 0), col);
    } else if (c.tipo == 3) {   // Mr. Popo sobre su alfombra (4 cuadros de 104x56)
      const int n = int(t * 6) % 4;
      DibIcono(dl, 18, ImVec2(p.x - 52 * e, p.y - 28 * e), ImVec2(p.x + 52 * e, p.y + 28 * e), ImVec4(0, 56.0f * float(n), 104, 56.0f * float(n + 1)), col);
      DibIcono(dl, 16, ImVec2(p.x - 30 * e, p.y - 84 * e), ImVec2(p.x + 30 * e, p.y - 6 * e), ImVec4(0, 0, 0, 0), col);
    } else if (c.lucha || c.condicion) {   // casilla de combate: boton morado, kanji y en el hexagono su condicion
      Lienzo L; L.o = p; L.s = e;
      if (DibCasilla01(dl, L, c.condicion, t * 60.0f, col)) continue;
      DibIcono(dl, 14, ImVec2(p.x - 24 * e, p.y - 10 * e), ImVec2(p.x + 48 * e, p.y + 70 * e), ImVec4(0, 0, 0, 0), col);
      // 1 media vida (corazon a medias), 2 ki a 0 (el 0 con llama), 3 vida que baja (corazon gris), 4 ki que baja
      // (llama), 5 sin bloqueo (la X roja): corazones 398 #15 (32x32, 8 niveles) y 398 #19 (filas de 32x32)
      const ImVec4 src[6] = {ImVec4(0, 0, 0, 0), ImVec4(96, 0, 128, 32), ImVec4(0, 64, 32, 96), ImVec4(96, 32, 128, 64),
                             ImVec4(128, 64, 160, 96), ImVec4(160, 32, 192, 64)};
      if (c.condicion >= 1 && c.condicion <= 5)
        DibIcono(dl, c.condicion == 1 || c.condicion == 3 ? 15 : 19, ImVec2(p.x + 10 * e, p.y + 20 * e),
                 ImVec2(p.x + 38 * e, p.y + 48 * e), src[c.condicion], col);
    } else {
      DibIcono(dl, 0, ImVec2(p.x - 12 * e, p.y - 12 * e), ImVec2(p.x + 12 * e, p.y + 12 * e), ImVec4(0, 0, 0, 0), col);
    }
  }
  // objetos a la vista y piezas, de atras adelante
  struct Cosa {
    float y;
    int ficha, objeto;
  };
  std::vector<Cosa> cosas;
  if (!prepara) {
    for (size_t i = 0; i < g_obj.size(); ++i) {
      if (EnSuelo(g_obj[i])) cosas.push_back({g_d.casillas[size_t(g_obj[i].casilla)].y - 1, -1, int(i)});
    }
    for (size_t i = 0; i < g_f.size(); ++i) {
      if (Activa(g_f[i])) cosas.push_back({PosFicha(g_f[i]).y, int(i), -1});
    }
  } else {
    for (size_t i = 0; i < g_d.piezas.size(); ++i) {
      const Ficha& f = g_d.piezas[i];
      if (f.bando != 0) cosas.push_back({g_d.casillas[size_t(f.casilla)].y, -2 - int(i), -1});
    }
  }
  std::sort(cosas.begin(), cosas.end(), [](const Cosa& a, const Cosa& b) { return a.y < b.y; });
  auto casilla_de = [&](const Cosa& q) {
    return q.objeto >= 0 ? -1 : q.ficha <= -2 ? g_d.piezas[size_t(-2 - q.ficha)].casilla : g_f[size_t(q.ficha)].casilla;
  };
  std::vector<int> cuantas(g_d.casillas.size(), 0);
  for (const Cosa& q : cosas) {
    if (casilla_de(q) >= 0) ++cuantas[size_t(casilla_de(q))];
  }
  std::vector<int> vistas(g_d.casillas.size(), 0);
  for (const Cosa& q : cosas) {
    if (q.objeto >= 0) {   // de pie junto a la casilla, a su tamano del original (398)
      const Objeto& o = g_obj[size_t(q.objeto)];
      const Casilla& c = g_d.casillas[size_t(o.casilla)];
      ImVec2 p;
      float e = 1;
      if (!k.P(c.x, c.y, p, &e)) continue;
      if (o.tipo == 0) {
        const int b = BolaEn(o.casilla);
        if (b < 0) continue;
        const int n = g_bolas_fase[size_t(b)][1];
        DibIcono(dl, 2, ImVec2(p.x - 18 * e, p.y - 40 * e), ImVec2(p.x + 18 * e, p.y - 4 * e), ImVec4(40.0f * float(n), 0, 40.0f * float(n + 1), 40));
        continue;
      }
      // #AMA 397, registro 0x43 + tipo (FUN_0027B740) -> textura de 398: 1 daga 20, 3 armadura 21, 5 cebolla 24, 6 Mr. Popo
      // 23, 7 capsula 22, 8 zenie 25, 9 radar 26 (su primer cuadro; la daga y la armadura, la mitad izquierda)
      const int ic = o.tipo == 9 ? 26 : o.tipo == 1 ? 20 : o.tipo == 3 ? 21 : o.tipo == 8 ? 25 : o.tipo == 7 ? 22 : o.tipo == 6 ? 23 : o.tipo == 5 ? 24 : 17;
      const Tex* x = Icono(ic);
      if (!x) continue;
      const float w = (ic == 20 || ic == 21) ? x->w * 0.5f : x->w, h = x->h;   // daga y armadura: la primera de dos
      // el rect del #AMA va centrado en el punto de la casilla (p. ej. la daga -20..19 x -28..27)
      DibIcono(dl, ic, ImVec2(p.x - w * 0.5f * e, p.y - h * 0.5f * e), ImVec2(p.x + w * 0.5f * e, p.y + h * 0.5f * e), ImVec4(0, 0, w, h));
      continue;
    }
    const bool en_prep = q.ficha <= -2;
    const Ficha& f = en_prep ? g_d.piezas[size_t(-2 - q.ficha)] : g_f[size_t(q.ficha)];
    ImVec2 pos = en_prep ? ImVec2(g_d.casillas[size_t(f.casilla)].x, g_d.casillas[size_t(f.casilla)].y) : PosFicha(f);
    const int n_aqui = cuantas[size_t(f.casilla)], k_aqui = vistas[size_t(f.casilla)]++;
    pos.x += (float(k_aqui) - float(n_aqui - 1) * 0.5f) * 40.0f;
    const bool hecha = !en_prep && f.jugador && f.movs <= 0;
    const float arriba = DibPieza(dl, k, f, pos, t, hecha ? IM_COL32(170, 170, 180, 255) : IM_COL32_WHITE);
    ImVec2 p;
    float e = 1;
    // cifra azul junto a cada pieza (como en las capturas del original): las del jugador, los movimientos que le quedan;
    // las rivales, sus acciones de la fase enemiga (+2). La elegida da saltitos.
    if (!en_prep && f.c != kSaibaman && arriba >= 0 && k.P(pos.x, pos.y, p, &e)) {
      const float bote = q.ficha == g_sel ? 3 * e * std::sin(t * 4.0f) : 0.0f;
      CifrasAzules(dl, f.jugador ? f.movs : f.acciones, ImVec2(p.x + 34 * e, arriba + 6 * e + bote), 26 * e);
    }
  }
  // efectos de los guiones, en su punto del mapa (1 px del #AMA = 1 px del mapa)
  for (size_t i = 0; i < g_efectos.size();) {
    const Efecto& ef = g_efectos[i];
    if (t > ef.t0 + ef.dur) {
      g_efectos.erase(g_efectos.begin() + std::ptrdiff_t(i));
      continue;
    }
    ImVec2 p;
    float e = 1;
    if (t >= ef.t0 && k.P(ef.x, ef.y, p, &e)) {
      Lienzo L;
      L.o = p;
      L.s = e;
      const Datos::Sprite& sp = g_d.textos[size_t(ef.k)];
      DibSp(dl, g_d.textos, g_tloc, ef.k, L, (t - ef.t0) * 60.0f, IM_COL32_WHITE, -sp.px, -sp.py);
    }
    ++i;
  }
  // studies 8 (E), ordenes 83/84: "攻/防 +N%" sobre la pieza (ver Subida). El #AMA 397 se dibuja con las texturas de 398
  // (uRam00698DCC / uRam00698DB4), con su ancla en el punto de la pieza en el mapa (FUN_0027A790), como el original.
  for (size_t i = 0; i < g_subidas.size();) {
    Subida& s = g_subidas[i];
    const auto& la = g_d.life_ama;
    const float c = (t - s.t0) * 60.0f;
    float fin = s.signo ? 0.0f : 120.0f;
    for (int j = 0; s.signo && j < 4; ++j) {
      const int an = (s.defensa ? 120 : 116) + j;
      if (an < int(la.size())) fin = std::max(fin, la[size_t(an)].dur - 1);
    }
    const float dur_cifra = la.size() > 0x4D ? la[0x4D].dur : 0.0f;   // FUN_0027BD00: +0x34 = FUN_00245C00(397, 0x4D)
    if (c > std::max(fin, dur_cifra) || la.size() <= 123) {
      g_subidas.erase(g_subidas.begin() + std::ptrdiff_t(i));
      continue;
    }
    ++i;
    if (!s.sonado && c >= 110) {   // b2.c:237756-237759 / 237832-237835
      SonidoCanal3(s.defensa ? 0x12 : 0x0C);
      s.sonado = true;
    }
    const Ficha* f = PorN(s.n);
    ImVec2 p;
    float e = 1;
    if (!f || !k.P(PosFicha(*f).x, PosFicha(*f).y, p, &e)) continue;
    Lienzo L;
    L.o = p;
    L.s = e;
    for (int j = 0; s.signo && j < 4; ++j) {
      const int an = (s.defensa ? 120 : 116) + j;
      DibNativa01(dl, la, g_ticonos, an, L, std::min(c, la[size_t(an)].dur - 1));
    }
    if (c < dur_cifra) {   // FUN_0027BAB0: la pose de 0x4D con cada imagen en su sitio
      auto pieza = [&](float dx, int imagen) {
        Lienzo l2 = L;
        l2.o.x += dx * e;
        DibNativa01(dl, la, g_ticonos, 0x4D, l2, c, IM_COL32_WHITE, 0, imagen);
      };
      pieza(48, 0x5A);   // "%"
      int v = s.valor;
      for (int d = 1; d < 4; ++d) {
        pieza(-(float(d) * 24.0f - 48.0f), 0x4E + v % 10);
        v /= 10;
      }
      pieza(-48, 0x59 - (s.signo & 1));   // 0x58 "+", 0x59 "-"
    }
  }
  // cursor del original (FUN_00275440, medido en PCSX2 R7): la flecha roja = animacion 92 del #AMA 402 (textura
  // DATA_<idioma> 188 #26), sin giro, en la casilla elegida (el destino o, si no hay, la del personaje); su punto se le
  // acerca 1/6 por cuadro y la lista A la balancea (30,-30) -> (24,-25) en 20 cuadros con escala x -1 (volteada: apunta
  // abajo a la izquierda, a la casilla, como en la captura flecha_roja_fase1.png).
  static float cur_x = -1, cur_y = 0, cur_t = 0;
  const int cur_c = prepara || g_estado != kJuego || g_fase_rival || HilosOcupados() || EscenaAbierta() ? -1
                    : g_dest > 0 ? g_dest : sel_c;
  if (cur_c > 0 && g_d.hud_ama.size() > 92) {
    const Casilla& c = g_d.casillas[size_t(cur_c)];
    const float f = cur_x < 0 ? 1.0f : 1.0f - std::pow(5.0f / 6.0f, std::clamp(t - cur_t, 0.0f, 0.5f) * 60.0f);
    cur_x += (c.x - cur_x) * f;
    cur_y += (c.y - cur_y) * f;
    const Datos::Sprite& sp = g_d.hud_ama[92];
    ImVec2 p;
    float e = 1;
    if (sp.tex >= 0 && sp.tex < int(g_thud.size()) && g_thud[size_t(sp.tex)] && k.P(cur_x, cur_y, p, &e)) {
      const Tex& x = g_thud[size_t(sp.tex)];
      const Pose po = PoseDe(sp, std::fmod(t * 60.0f, std::max(1.0f, sp.dur - 1)));
      const float ex = po.ex, ey = po.ey;   // ex = -1: la textura va volteada (como en el original)
      dl->AddImage(x.id(), ImVec2(p.x + (po.px + sp.x0 * ex) * e, p.y + (po.py + sp.y0 * ey) * e),
                   ImVec2(p.x + (po.px + sp.x1 * ex) * e, p.y + (po.py + sp.y1 * ey) * e), ImVec2(sp.u0 / x.w, sp.v0 / x.h),
                   ImVec2(sp.u1 / x.w, sp.v1 / x.h));
    }
  } else if (cur_c <= 0) {
    cur_x = -1;   // al volver, aparece ya en su casilla
  }
  cur_t = t;
  // "!!" del contacto o "?!" de la busqueda (398 #8 / #7)
  if (!prepara && g_marca > 0 && t - g_marca_t < 1.2f) {
    ImVec2 p;
    float e = 1;
    if (k.P(g_d.casillas[size_t(g_marca)].x, g_d.casillas[size_t(g_marca)].y, p, &e)) {
      // FUN_00274880 estado 3: entrada 0x2A (51 cuadros), bucle 0x2B (21). El evento conserva su vida local.
      bool puesto = false;
      if (g_marca_icono == 8 && g_d.life_ama.size() > 43) {
        const float c = std::max(0.0f, (t - g_marca_t) * 60.0f), entrada = g_d.life_ama[42].dur;
        Lienzo L; L.o = p; L.s = e;
        puesto = DibNativa01(dl, g_d.life_ama, g_ticonos, c < entrada ? 42 : 43, L,
                             c < entrada ? c : std::fmod(c - entrada, std::max(1.0f, g_d.life_ama[43].dur)));
      }
      if (!puesto) {
        const float s = 46 * e * (1.0f + 0.3f * std::max(0.0f, 0.2f - (t - g_marca_t)) * 5.0f);
        DibIcono(dl, g_marca_icono, ImVec2(p.x - s * 0.5f + 30 * e, p.y - 66 * e - s * 1.4f), ImVec2(p.x + s * 0.5f + 30 * e, p.y - 66 * e - s * 0.4f));
      }
    }
  }
}

// Marcador del original (CMN 402 + DATA_<idioma> 188), en la pantalla de 640x448:
//   D_TURN (ELF 0x26E830/0x26E6F0): rombo (anim 39) con el turno (hasta 99): cifra d = anim 41+d, puesta en el sitio de la
//   41 (una cifra) o de la 42 (unidades) y la 43 (decenas).
//   D_RADARBASE / DBALL: barra de madera (1), "MUNDO DRAGON" (4) y cada Bola conseguida en su hueco (Bola 1 = 8, las
//   demas 9+n). D_DBGET: la recien cogida entra con las claves de la 7 (20 cuadros).
//   D_RADAR (0x26F940, solo con el Radar del Dragon recogido): radar (17), brillo (89) y, por cada Bola de la fase, la
//   flecha (87) girada hacia ella si esta a mas de 300 px del mapa o el punto (88) a 1/10 de la distancia.
//   L1 esconde y saca la barra y el radar (0x2700E0).
//   Entradas y salidas (60 cuadros por segundo, claves del #AMA): barra 0 entra (20) / 1 reposo / 2 sale (15) y "MUNDO
//   DRAGON" 3 / 4 / 5 (D_RADARBASE, 0x26F5D0; los huecos de las Bolas van con la barra), radar 16 entra (20) / 17 / 18 sale
//   (15) y el rombo del turno, 38 al llegar al tablero (10: de nada a 1,3 y a 1; D_TURN 0x26EA10). Todo entra al empezar
//   el tablero o volver de un combate y al sacar el marcador con L1. El radar roto (24-37, "BOOM": estado 5 de D_RADAR) no
//   lo pide ninguna parte del juego (ninguna escritura de ese estado en el ELF europeo): sin portar.
// Sin los archivos (otra ISO), el marcador de antes con colores propios.
// PRUEBA: DBZ3_TEST_B2_LENTO=N alarga N veces las entradas y salidas del marcador y del menu para verlas en una captura
float Lento() {
  static const float l = std::getenv("DBZ3_TEST_B2_LENTO") ? std::max(1.0f, float(std::atof(std::getenv("DBZ3_TEST_B2_LENTO")))) : 1.0f;
  return l;
}
void DibHud(ImDrawList* dl, const S::Area& a, float t) {
  if (g_f.empty()) return;
  if (g_d.hud_ama.size() > 89 && g_thud.size() > 23) {
    const Lienzo L = LienzoDe(a);
    const float lento = Lento();
    if (t - g_hud_visto > 0.4f) {   // el tablero empieza o vuelve de un combate: todo entra (el marcador escondido sigue asi)
      g_rombo_t = t;
      if (!g_hud_oculto) g_hud_t = g_radar_t = t;
    }
    g_hud_visto = t;
    if (g_radar && !g_radar_visto) g_radar_t = t;   // recien cogido el Radar del Dragon
    g_radar_visto = g_radar;
    const float ch = (t - g_hud_t) * 60.0f / lento, cr = (t - g_radar_t) * 60.0f / lento;
    if (!g_hud_oculto || ch < 15) {
      // barra y titulo: entrando, en reposo o saliendo; las Bolas se mueven con la barra (posicion de ahora - reposo)
      const int ab = g_hud_oculto ? 2 : ch < 20 ? 0 : 1;
      const float cb = ab == 1 ? -1.0f : ch;
      DibHudSp(dl, ab, L, cb);
      DibHudSp(dl, ab + 3, L, cb);
      const float bx = PoseDe(g_d.hud_ama[size_t(ab)], cb).px - g_d.hud_ama[1].px;
      for (int n = 0; n < 7; ++n) {
        if (!(g_bolas >> n & 1)) continue;
        const int k = n == 0 ? 8 : 9 + n;
        const float c = (t - g_bola_t) * 60.0f;
        if (n == g_bola_n && c < 20) {   // entra desde la izquierda (claves de la 7, llevadas a su hueco)
          const Datos::Sprite& e = g_d.hud_ama[7];
          DibHudSp(dl, k, L, -1, IM_COL32_WHITE, bx + PoseDe(e, c).px - e.px, 0);
          continue;
        }
        DibHudSp(dl, k, L, -1, IM_COL32_WHITE, bx, 0);
      }
      if (g_radar && g_sel < int(g_f.size())) {
        const float rc = g_hud_oculto ? ch : cr;   // 18 sale, 16 entra
        if (g_hud_oculto || cr < 20) {
          DibHudSp(dl, g_hud_oculto ? 18 : 16, L, rc);
        } else {
          DibHudSp(dl, 17, L);
          DibHudSp(dl, 89, L, std::fmod(t * 60.0f, 120.0f));
          const Datos::Sprite& centro = g_d.hud_ama[89];
          const Casilla& yo = g_d.casillas[size_t(g_f[size_t(g_sel)].casilla)];
          for (size_t i = 0; i < g_bolas_fase.size() && i < 3; ++i) {
            const Casilla& b = g_d.casillas[size_t(g_bolas_fase[i][0])];
            const float dx = b.x - yo.x, dy = b.y - yo.y;
            if (dx * dx + dy * dy > 90000.0f)
              DibHudSp(dl, 87, L, -1, IM_COL32_WHITE, centro.px - g_d.hud_ama[87].px, centro.py - g_d.hud_ama[87].py,
                       std::atan2(dx, -dy));
            else
              DibHudSp(dl, 88, L, -1, IM_COL32_WHITE, centro.px - g_d.hud_ama[88].px + dx / 10, centro.py - g_d.hud_ama[88].py + dy / 10);
          }
        }
      }
    }
    const float cd = (t - g_rombo_t) * 60.0f / lento;
    DibHudSp(dl, cd < 10 ? 38 : 39, L, cd < 10 ? cd : -1.0f);
    const int n = std::clamp(g_turno, 0, 99);
    auto cifra = [&](int d, int sitio) {
      const Datos::Sprite &s = g_d.hud_ama[size_t(41 + d)], &p = g_d.hud_ama[size_t(sitio)];
      DibHudSp(dl, 41 + d, L, -1, IM_COL32_WHITE, p.px - s.px, p.py - s.py);
    };
    if (n < 10) cifra(n, 41);
    else {
      cifra(n % 10, 42);
      cifra(n / 10, 43);
    }
    return;
  }
  const ImVec2 c = a.At(1110, 84);
  const float r = 54 * a.s;
  const ImVec2 q[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
  dl->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(238, 228, 206, 245));
  dl->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(160, 36, 30, 255), 9 * a.s);
  dl->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(90, 16, 12, 255), 2 * a.s);
  CifrasAzules(dl, g_turno, c, 46 * a.s);
  dl->AddRectFilled(a.At(70, 800), a.At(560, 858), IM_COL32(150, 96, 48, 240), 26 * a.s);
  dl->AddRect(a.At(70, 800), a.At(560, 858), IM_COL32(70, 40, 16, 255), 26 * a.s, 0, 4 * a.s);
  for (int b = 0; b < 7; ++b) {
    const bool tiene = (g_bolas >> b) & 1;
    const float x = 175 + 52.0f * float(b);
    dl->AddCircleFilled(a.At(x, 829), 21 * a.s, IM_COL32(90, 56, 26, 255));
    if (tiene) DibIcono(dl, 2, a.At(x - 21, 808), a.At(x + 21, 850), ImVec4(40.0f * float(b), 0, 40.0f * float(b + 1), 40));
  }
  DibIcono(dl, 26, a.At(16, 720), a.At(116, 860));
  S::MenuText(dl, a.At(320, 880), 30 * a.s, T::T("MUNDO DRAGÓN", "DRAGON WORLD"), true, false);
}

void DibAviso(ImDrawList* dl, const S::Area& a, float t) {
  if (g_msg.empty() || t - g_msg_t > 4.0f) return;
  dl->AddRectFilled(a.At(250, 702), a.At(950, 770), IM_COL32(10, 24, 70, 215), 12 * a.s);
  dl->AddRect(a.At(250, 702), a.At(950, 770), IM_COL32(120, 170, 255, 220), 12 * a.s, 0, 2 * a.s);
  float x = 600;
  if (g_msg_icono >= 0) {
    if (g_msg_icono == 2) {
      const int n = g_d.fase % 7;
      DibIcono(dl, 2, a.At(266, 714), a.At(310, 758), ImVec4(40.0f * float(n), 0, 40.0f * float(n + 1), 40));
    } else {
      DibIcono(dl, g_msg_icono, a.At(266, 714), a.At(310, 758));
    }
    x = 620;
  }
  S::MenuText(dl, a.At(x, 736), 30 * a.s, g_msg.c_str(), true, false, 600 * a.s);
}

}  // namespace

// Forma inicial B3 de una pieza del tablero (Cell perfecto / Freezer final por defecto; orden 52 si la hay).
int FormaTableroB3(int id_b3, int forma_b2) { return FormaTableroB3Impl(id_b3, forma_b2); }

// studies 2 G04: COM del rival de la orden 51 (+0x18 del rival -> 0x699365). -1 = ese combate no trae orden 51.
// pasarela_misiones.cpp lo declara en su propio ambito (no se toca pasarela.h) y lo lleva a Reglas::nivel_com.
int ComRivalB2() { return g_com_rival_b2; }

// Cabeza de B2 (CMN 1196) del luchador `id_b3` de B3 (el primer ID de B2 que le corresponde); nada si no hay.
ImTextureID CabezaDeB3(int id_b3) {
  for (int c = 0; c < 55; ++c)
    if (B2aB3(c) == id_b3) return CabezaB2(c);
  return ImTextureID{};
}

// ---- API (pasarela_misiones.cpp, con su g_mu) ----
bool Disponible() {
  // una vez encontrada, se queda; mientras falte se vuelve a mirar cada 2 s (el jugador la puede elegir con el juego
  // abierto: ElegirIsoPs2)
  static std::atomic<bool> hay{false};
  static std::atomic<int64_t> visto{-1000000};
  if (hay) return true;
  const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count();
  if (ms - visto.load() < 2000) return false;
  visto = ms;
  hay = !BuscarJuego("budokai 2").empty();
  return hay;
}

// M1 (mod opcional b2.video_mundo_dragon): el video de entrada al Mundo Dragon. El original lanza
// /USR/MOVIE_EU.SFD al abrir el tablero (FUN_002a47a0 lo reproduce con FUN_001461f0(.., 0x3a3770, 1), b2.c:246837;
// se salta con START, mascara 0x8 en FUN_002a46f0, b2.c:246769-246777). Una vez por sesion. El fichero sale de la
// copia del jugador (carpeta o .iso: opcional::ArchivoDeJuegoPs2 extrae de la ISO a la cache). A/START lo saltan
// (lo lee el reproductor, video_sfd.h).
void VideoMundoDragon() {
  if (!dbz3::opcional::Activo("b2.video_mundo_dragon")) return;
  static bool hecho = false;
  if (hecho) return;
  const std::string ruta = dbz3::opcional::Texto("b2.video_mundo_dragon", "ruta_iso", "USR/MOVIE_EU.SFD");
  const std::filesystem::path p = dbz3::opcional::ArchivoDeJuegoPs2("b2", ruta);
  if (p.empty()) return;   // sin copia de B2 (o sin el fichero): no se da por hecho (se reintenta al reentrar)
  hecho = true;
  const bool ok = dbz3::video::Reproducir(p);
  REXLOG_INFO("dbz3 tablero b2: video de entrada (mod b2.video_mundo_dragon): {} ({})", ruta,
              ok ? "reproduciendo" : "no se pudo abrir");
}

void Entrar() {
  g_bolas = LeerProgreso("tablero_b2_bolas", 0);
  const int fase = std::clamp(LeerProgreso("tablero_b2_fase", 0), 0, kFases - 1);
  if (g_luchando) {   // se salio desde la tarjeta del combate
    g_luchando = false;
    g_pend_j = g_pend_r = g_pend_mov = -1;
  }
  if (g_estado == kJuego && g_d.fase == fase) return;   // sigue la partida de esta sesion
  if (Cargar(fase)) Preparar();
  if (std::getenv("DBZ3_TEST_B2_DESEO")) {   // PRUEBA: la pantalla de los deseos sin jugar las 9 fases
    g_estado = kDeseo;
    g_deseo = 0;
    g_shen = 0;
  }
  VideoMundoDragon();   // M1 (mod opcional b2.video_mundo_dragon), una vez por sesion
}

std::atomic<bool> g_iris_pend{false};   // S1-F01.2: el iris de la Bola al volver del combate (lo dibuja DibIris)
void Resultado(int ganador, int vida) {
  g_iris_pend = true;   // S1-F01.2 (B2: FUN_0024A080(0xB, 0x2382A0) al volver del combate)
  g_luchando = false;
  const int pj = g_pend_j, pr = g_pend_r;
  g_pend_j = g_pend_r = g_pend_mov = -1;
  if (pj >= 0) g_f[size_t(pj)].punto = 0;   // vuelve a su casilla (andaba hasta el rival)
  if (pj >= 0 && pr >= 0 && ganador >= 0) {
    const Ficha &fj = g_f[size_t(pj)], &fr = g_f[size_t(pr)];
    SucesosDeCombate(ganador == 0, ganador == 0 ? fr.barras - 1 <= 0 : fj.barras - 1 <= 0);
  }
  if (pj < 0 || pr < 0 || ganador < 0) return FinDeAccion();
  Ficha &j = g_f[size_t(pj)], &r = g_f[size_t(pr)];
  REXLOG_INFO("dbz3 tablero b2: combate {} contra {}: gana {}, vida {}", Nombre(j), Nombre(r), ganador == 0 ? "el jugador" : "el rival", vida);
  if (ganador == 0) {
    j.vida = vida > 0 && vida < j.vida_max ? vida : 0;
    // FAQ: si estaba aturdido, el golpe quita 2 barras. ponytail: perder aturde al rival hasta el siguiente turno rival
    r.barras -= 1;   // R7: sin "aturdido" (de la FAQ; el ELF no lo tiene)
    if (r.barras <= 0) {
      r.fuera = true;
      Aviso(Nombre(r) + T::T(" ha caído.", " is down."));
    } else {
      Aviso(Nombre(r) + T::T(" pierde una barra.", " loses a bar."), 4);
    }
  } else {   // ponytail (sin confirmar en la FAQ): perder quita una barra al tuyo, que vuelve con la vida llena
    if (j.c == kGoku && j.bando == 4) {   // FUN_00277BB0: la pieza que pierde, golpeada o derribada
      g_goku_reac_t = g_ahora;
      g_goku_reac_ko = j.barras - 1 <= 0;
    }
    j.vida = 0;
    if (--j.barras <= 0) {
      j.fuera = true;
      bool queda = false;
      for (const Ficha& f : g_f) queda |= Activa(f) && f.jugador && f.c == kGoku;
      if (!queda) {
        g_estado = kPerdida;
        REXLOG_INFO("dbz3 tablero b2: fase {} perdida", g_d.fase + 1);
        return Aviso(T::T("Goku ha caído.", "Goku is down."));
      }
      Aviso(Nombre(j) + T::T(" queda fuera del tablero.", " is out of the board."));
    } else {
      Aviso(Nombre(j) + T::T(" pierde una barra.", " loses a bar."), 15);
    }
  }
  if (g_sel >= 0 && !Activa(g_f[size_t(g_sel)])) SiguienteSel();
  FinDeAccion();
}

// s9b: la ayuda de abajo del tablero del original (medida en PCSX2, savestates 3 y 4 de la fase I): una banda oscura y
// difuminada de ~24 px de alto (y 394-417 del lienzo de 448) a todo el ancho, con las frases en blanco que pasan de derecha a
// izquierda sin parar (~72 px/s = 1,2 px por cuadro; medido: 125 px en 1,7 s) y su boton delante. Cambia con el modo: elegir
// ficha o elegir casilla destino. El marcador de madera ("DRAGON WORLD") la tapa por la izquierda cuando esta puesto.
void AyudaMarquesina(ImDrawList* dl, const S::Area& a, float t) {
  struct Frase {
    const char* boton;
    std::string texto;
  };
  const bool destino = g_dest > 0;
  // A2c: las frases del original (capturas): paso 1 "Select a Play Character / L1 button : Dragon Radar on/off / R1 button : Set up
  // Skills / R2 button : Display Status / START : Menu"; paso 2 "Select the Map Point Where You are Going" y las mismas, mas
  // "(O) No Activity but Standing by", "([]) Search Map Points", "(X) Final Selection on the Map Point where You are Going".
  const std::vector<Frase> frases =
      destino ? std::vector<Frase>{{"", T::T("Elegir la casilla a la que ir", "Select the Map Point Where You are Going")},
                                   {"", T::T("L1: Radar del Dragón sí/no", "L1 button : Dragon Radar on/off")},
                                   {"", T::T("R1: Preparar habilidades", "R1 button : Set up Skills")},
                                   {"", T::T("R2: Ver estado", "R2 button : Display Status")},
                                   {"B", T::T("Quedarse quieto", "No Activity but Standing by")},
                                   {"X", T::T("Buscar en la casilla", "Search Map Points")},
                                   {"A", T::T("Ir a la casilla elegida", "Final Selection on the Map Point where You are Going")}}
              : std::vector<Frase>{{"", T::T("Elegir ficha", "Select a Play Character")},
                                   {"", T::T("L1: Radar del Dragón sí/no", "L1 button : Dragon Radar on/off")},
                                   {"", T::T("R1: Preparar habilidades", "R1 button : Set up Skills")},
                                   {"", T::T("R2: Ver estado", "R2 button : Display Status")},
                                   {"", T::T("START: Menú", "START : Menu")},
                                   {"A", T::T("Elegir la ficha", "Final Selection on a Play Character")}};
  const Lienzo L = LienzoDe(a);
  // I01 (studies 4): la banda va del borde izquierdo al derecho de la pantalla del original (640), no de la
  // ventana: en 16:9 el texto asomaba fuera del area 4:3 (a la izquierda del marcador).
  const float xl = (!g_hud_oculto && g_radar) ? L.P(272, 0).x : L.P(0, 0).x;
  const float xr = L.P(640, 0).x;
  const float y0 = 394.0f, y1 = 418.0f, h = 20.0f * L.s;
  ImFont* fuente = ImGui::GetFont();
  const float tam = 18.0f * L.s;
  // banda: 3 franjas (borde de arriba, centro, borde de abajo) para el difuminado
  const float ya = L.P(0, y0).y, yb = L.P(0, y1).y, borde = 3.0f * L.s;
  dl->PushClipRect(ImVec2(xl, ya - borde), ImVec2(xr, yb + borde), true);
  dl->AddRectFilledMultiColor(ImVec2(xl, ya - borde), ImVec2(xr, ya + borde), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 96), IM_COL32(0, 0, 0, 96));
  dl->AddRectFilled(ImVec2(xl, ya + borde), ImVec2(xr, yb - borde), IM_COL32(0, 0, 0, 96));
  dl->AddRectFilledMultiColor(ImVec2(xl, yb - borde), ImVec2(xr, yb + borde), IM_COL32(0, 0, 0, 96), IM_COL32(0, 0, 0, 96), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
  // largo de una vuelta
  const float hueco = 70.0f * L.s;
  std::vector<float> ancho;
  float total = 0;
  for (const Frase& f : frases) {
    float w = fuente->CalcTextSizeA(tam, FLT_MAX, 0, f.texto.c_str()).x;
    if (f.boton[0]) w += S::Glyph(nullptr, f.boton, ImVec2(), h) + 4.0f * L.s;
    ancho.push_back(w);
    total += w + hueco;
  }
  const float vel = 72.0f * L.s;
  const float visible = xr - xl;
  const float desde = xr - std::fmod(t * vel, total + visible);
  const float cy = L.P(0, (y0 + y1) * 0.5f).y;
  for (int vuelta = 0; vuelta < 2; ++vuelta) {
    float x = desde + float(vuelta) * (total + visible);
    for (size_t i = 0; i < frases.size(); ++i) {
      const Frase& f = frases[i];
      float xt = x;
      if (x + ancho[i] > xl && x < xr) {
        if (f.boton[0]) xt += S::Glyph(dl, f.boton, ImVec2(x, cy), h) + 4.0f * L.s;
        const ImVec2 ts = fuente->CalcTextSizeA(tam, FLT_MAX, 0, f.texto.c_str());
        const ImVec2 pos(xt, cy - ts.y * 0.5f);
        dl->AddText(fuente, tam, ImVec2(pos.x + 1.2f * L.s, pos.y + 1.2f * L.s), IM_COL32(0, 0, 0, 200), f.texto.c_str());
        dl->AddText(fuente, tam, pos, IM_COL32(245, 245, 245, 255), f.texto.c_str());
      }
      x += ancho[i] + hueco;
    }
  }
  dl->PopClipRect();
}

// s9b: el menu del tablero (DWU_MMAIN, ELF 0x2A35F0) y su Si/No (DWU_YN*, 0x2A2C90), piezas del CMN 402. Sale de la funcion de
// dibujo para poder abrirse en CUALQUIER momento de la fase (guiones, ventanas, eleccion de aliados, fase enemiga, tarjeta del
// combate): antes solo se abria con el tablero libre y el jugador se quedaba sin poder salir. 1 = salir del modo.
int MenuTablero(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed) {
    const Lienzo L = LienzoDe(a);
    const float c = (t - g_menu_t) * 60.0f / Lento();
    const ImU32 apagado = IM_COL32(110, 110, 130, 255);
    dl->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, 90));
    if (g_menu == 1 && c < 10) {   // la caja se abre (97: su alto de 0 a 0,8 en 10 cuadros)
      DibHudSp(dl, 97, L, c);
    } else if (g_menu == 2 && c < 10) {   // y la del Si/No (110: su ancho de 0 a 1)
      DibHudSp(dl, 110, L, c);
    } else if (g_menu == 1) {   // caja (98) con "Salir" (106) y "¿Reiniciar Mundo Dragon?" (107)
      DibHudSp(dl, 98, L);
      for (int i = 0; i < 2; ++i) DibHudSp(dl, 106 + i, L, -1, i == g_menu_sel ? IM_COL32_WHITE : apagado);
    } else {   // caja (111), pregunta (113 + mensaje: salir / empezar de nuevo), "Si" (116), "No" (117) y la flecha (118)
      DibHudSp(dl, 111, L);
      DibHudSp(dl, 113 + g_menu_sel, L);
      for (int i = 0; i < 2; ++i) DibHudSp(dl, 116 + i, L, -1, i == g_yn_sel ? IM_COL32_WHITE : apagado);
      if (g_d.hud_ama.size() > 118) {   // ponytail: la flecha a la izquierda de la opcion (su ancla exacta, sin leer)
        const Datos::Sprite &op = g_d.hud_ama[size_t(116 + g_yn_sel)], &fl = g_d.hud_ama[118];
        DibHudSp(dl, 118, L, std::fmod(c, 15.0f), IM_COL32_WHITE, op.px + op.x0 - 18 - fl.px, op.py - fl.py);
      }
    }
    if (g_d.hud_ama.size() <= 118 || g_thud.size() < 1) {   // sin las piezas del CMN 402 (otra ISO): el menu en texto,
      // si no se abria invisible y el jugador no veia como salir
      const bool si_no = g_menu == 2;
      const char* op[2] = {si_no ? T::T("Sí", "Yes") : T::T("Salir", "Quit"),
                           si_no ? T::T("No", "No") : T::T("Reiniciar Mundo Dragón", "Restart Dragon World")};
      if (si_no) S::MenuText(dl, a.At(600, 360), 38 * a.s, g_menu_sel == 0 ? T::T("¿Salir del Mundo Dragón?", "Quit Dragon World?")
                                                                         : T::T("¿Empezar de nuevo?", "Start over?"), true, false);
      for (int i = 0; i < 2; ++i)
        S::MenuText(dl, a.At(600, 440 + 70 * i), 44 * a.s, op[i], i == (si_no ? g_yn_sel : g_menu_sel), i != (si_no ? g_yn_sel : g_menu_sel));
    }
    S::HelpLine(dl, a.At(600, 878), 38 * a.s, {{"A", T::T("Aceptar", "OK")}, {"Y", T::T("Volver", "Back")}});
    if (pressed & (kUp | kDown)) {
      (g_menu == 1 ? g_menu_sel : g_yn_sel) ^= 1;
    } else if (pressed & kA) {
      if (g_menu == 1) {
        g_menu = 2;
        g_yn_sel = 1;   // el original empieza en "No" (+0x44 = 1)
        g_menu_t = t;
      } else if (g_yn_sel == 1) {
        g_menu = 1;
      } else if (g_menu_sel == 0) {
        REXLOG_INFO("dbz3 tablero b2: menu: salir del Mundo Dragon");
        g_menu = 0;
        return 1;
      } else {   // empezar de nuevo: el original borra la partida del Mundo Dragon (0x693A1C, 0x9C bytes)
        REXLOG_INFO("dbz3 tablero b2: menu: reiniciar el Mundo Dragon");
        g_menu = 0;
        g_bolas = 0;
        GuardarProgreso("tablero_b2_bolas", 0);
        GuardarProgreso("tablero_b2_fase", 0);  GuardarBanderas(true);
        if (Cargar(0)) Preparar();
      }
    } else if (pressed & (kY | kStart | kB)) {
      g_menu = g_menu == 2 ? 1 : 0;
    }
    return 0;
}

// 0 nada, 1 salir, 2 combate: out = {jugador, rival (IDs de B3), mapa de B3, vida que le queda al jugador, vida del
// rival (< 0 = solo la inicial), condicion de la casilla (1-5, ver T1)}.
bool IrisActivo(float t);   // S1-F01.2 (mas abajo, con DibIris)
void IrisCerrar(float t);   // S1-F01.1
bool IrisCerrado(float t);  // S1-F01.1
int DibujarTablero(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, int out[14]) {
  g_ahora = t;
  ActualizarPuntos();   // ordenes 65/66: llegada (libera la 66 en el cuadro exacto)
  VaciarPapelera();   // S1-F07.2
  if (g_d.fase < 0) {
    S::MenuText(dl, a.At(600, 400), 40 * a.s, T::T("No se pudo leer el tablero de tu Budokai 2.",
                                                   "Couldn't read the board from your Budokai 2."), true, false, 1000 * a.s);
    S::MenuText(dl, a.At(600, 470), 30 * a.s, T::T("Comprueba que has elegido bien la ISO de Budokai 2.",
                                                   "Check that you picked the right Budokai 2 ISO."), false, true, 1000 * a.s);
    S::HelpLine(dl, a.At(600, 878), 34 * a.s, {{"B", T::T("Salir", "Quit")}});
    return (pressed & kB) ? 1 : 0;
  }
  Texturas();
  if (const char* v = std::getenv("DBZ3_TEST_B2_TARJETA")) {   // PRUEBA: ensena solo la tarjeta "Battle!" (condicion v)
    static float t0 = -1;
    if (t0 < 0) t0 = t;
    Ficha goku;
    DibTarjeta(dl, a, t, (t - t0) * 60.0f, std::atoi(v), goku);
    return 0;
  }
  uint16_t mando = pressed;   // s9b: con el menu abierto el resto de la pantalla no recibe pulsaciones ni avanza
  if (g_menu > 0) pressed = 0;
  if (g_cont_j >= 0 && g_ahora >= g_cont_t) {
    const int cj = g_cont_j, cr = g_cont_r;
    g_cont_j = g_cont_r = -1;
    Contacto(cj, cr);
  }
  if (g_estado == kJuego && g_menu == 0) CorrerHilos();
  if (auto_on && g_menu == 0 && !HilosOcupados() && !EscenaAbierta() && t - g_auto_t > 1.4f) {
    g_auto_t = t;
    if (g_estado == kGanada || g_estado == kPerdida) pressed |= kA;
    else Auto();
  }
  const bool prepara = g_estado == kPrepara;
  DibTablero(dl, a, t, prepara);
  if (!prepara && !g_mensajes.empty()) {   // ventana de mensaje del script, en la pantalla de 640x448 del original
    if (g_mensaje_t < 0) g_mensaje_t = t;
    const Lienzo L = LienzoDe(a);
    // entrada del original: las claves de la lista A a 60 cuadros por segundo (posicion lineal entre claves).
    // D_CLEAR (ELF 0x26F280 -> 0x26EFC0 -> 0x26ED50): primero las 8 piezas de la ventana del CMN 402 (entrar = 0x33+3k:
    // "Borrar cond.", que tapa la nube, la nube de la cabecera, los 4 lados y el fondo azul; la 8.a, sin textura, oscurece
    // la pantalla: ponytail, aqui un velo negro al 40 %) y encima los dos textos de la fase (343+5k con 146+k).
    const float c = (t - g_mensaje_t) * 60.0f;
    if (g_d.hud_ama.size() > 74) {
      dl->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, int(100 * std::min(1.0f, c / 20.0f))));
      for (int p = 0; p < 7; ++p) DibHudSp(dl, 0x33 + 3 * p, L, c);
    }
    for (int k : g_mensajes.front()) DibSp(dl, g_d.textos, g_tloc, k, L, c);
    // s9: en B2 la ventana de fase ("Stage 1 / Get the Dragon Balls before Nappa!") se queda hasta que pulsas X: la orden
    // 39 que le sigue en el guion es esa espera (medido: 14 s en pantalla hasta pulsar). Si el guion sigue con una 39, la
    // ventana no se quita sola y esa misma X la cierra y acaba la espera; el temporizador de 3 s queda para la prueba
    // automatica y para las ventanas sin 39.
    bool sigue39 = false;
    for (const Hilo& h : g_hilos) {
      const auto& g = g_d.guiones[size_t(h.guion)];
      if (h.i < g.size() && g[h.i][1] == 39) sigue39 = true;
    }
    if ((pressed & kA) || (t - g_mensaje_t > 3.0f && (!sigue39 || auto_on))) {
      if (pressed & kA)
        for (Hilo& h : g_hilos) {
          const auto& g = g_d.guiones[size_t(h.guion)];
          if (h.i < g.size() && g[h.i][1] == 39) ++h.i;
        }
      g_mensajes.erase(g_mensajes.begin());
      g_mensaje_t = -10;
      pressed &= uint16_t(~kA);
    }
  }
  if (!prepara && g_mensajes.empty() && g_premio_id > 0 && !IrisActivo(t)) {   // orden 70: ventana "Get skill!" (la de la nave); S1-F01.2: tras el iris
    if (g_premio_t < 0) {
      g_premio_t = t;
      // S1-F01.4, PRUEBA: DBZ3_TEST_B2_PREMIO=<ID de B2> cambia la capsula del premio (193 = la ficha dorada)
      if (const char* p = std::getenv("DBZ3_TEST_B2_PREMIO")) g_premio_id = std::max(1, std::atoi(p));
    }
    const float c = (t - g_premio_t) * 60.0f / Lento();   // S1-F01.4: DBZ3_TEST_B2_LENTO tambien la frena (capturas)
    // s9b: cartel "Get skill!" y despues la ficha de la capsula (Skill Z Sheet), como en el original (medido en PCSX2): A salta el
    // cartel, X / cruceta da la vuelta a la ficha y A / Y la cierran. [H] el minimo de 30 cuadros antes de aceptar A: el cartel
    // pide su segundo sonido a los 30 (FUN_002A3E90).
    // PRUEBA: DBZ3_TEST_B2_VENTANA=s deja la ventana s segundos en la prueba automatica (para capturarla; 4 por defecto)
    static const float auto_s = std::getenv("DBZ3_TEST_B2_VENTANA") ? float(std::atof(std::getenv("DBZ3_TEST_B2_VENTANA")))
                                                                     : 4.0f;
    const bool cierra = dbz3::pasarela::nave::VentanaPremio(dl, a, g_premio_id, c, pressed, auto_on && t - g_premio_t > auto_s - 1.2f);
    if (cierra) {
      dbz3::pasarela::nave::GanarCapsula(g_premio_id);
      dbz3::pasarela::nave::DesbloquearEvento(g_premio_id);   // A-nave: el tablero dispara los sucesos de desbloqueo (FUN_002831F0)
      REXLOG_INFO("dbz3 tablero b2: orden 8/70: ventana cerrada, capsula {} al inventario", g_premio_id);
      g_premio_id = 0;
      g_premio_t = -10;
      pressed &= uint16_t(~kA);
    }
  }
  if (!prepara && g_mensajes.empty() && g_premio_id == 0 && g_espera_x) {   // orden 39: espera a A, sin dibujo
    static float desde = -10;
    if (desde < 0) desde = t;
    if ((pressed & kA) || (auto_on && t - desde > 1.0f)) {
      g_espera_x = false;
      desde = -10;
      pressed &= uint16_t(~kA);
      REXLOG_INFO("dbz3 tablero b2: orden 39: A pulsado, sigue el guion");
    }
  }
  if (prepara) {
    // eleccion de fase (arriba/abajo) y de companero (izquierda/derecha) con las fichas del original
    dl->AddRectFilled(a.At(90, 200), a.At(1110, 660), IM_COL32(8, 18, 54, 225), 16 * a.s);
    dl->AddRect(a.At(90, 200), a.At(1110, 660), IM_COL32(120, 170, 255, 230), 16 * a.s, 0, 3 * a.s);
    // aviso de que el modo aun se esta haciendo
    S::MenuText(dl, a.At(600, 170), 30 * a.s, T::T("BETA: en construcción", "BETA: work in progress"), true, false,
                1000 * a.s);
    auto img = [&](int j, float cy, float esc) {
      if (j < 0 || j >= int(g_tloc.size()) || !g_tloc[size_t(j)]) return false;
      const Tex& x = g_tloc[size_t(j)];
      dl->AddImage(x.id(), a.At(600 - x.w * esc * 0.5f, cy - x.h * esc * 0.5f), a.At(600 + x.w * esc * 0.5f, cy + x.h * esc * 0.5f));
      return true;
    };
    if (!img(g_d.stage, 248, 1.2f)) S::MenuText(dl, a.At(600, 248), 44 * a.s, kSagas[g_d.fase](), true, false, 900 * a.s);
    img(g_d.objetivo, 338, 1.0f);
    S::HelpLine(dl, a.At(600, 878), 34 * a.s, {{"A", T::T("Empezar", "Start")}, {"Y", T::T("Salir", "Quit")}});
    if (pressed & kY) return 1;
    // Fases en orden, como B2: el Mundo Dragon carga la fase de su partida guardada (ELF 0x2350C0 con 0x6992DA, copiado
    // de la partida 0x693A96 y limitado a 0-8) y no tiene pantalla para elegir otra. Antes, arriba/abajo saltaban a
    // cualquiera de las 9; ya no. Para volver a la I: menu del tablero > "¿Reiniciar Mundo Dragon?".
    if (pressed & (kA | kStart)) Empezar();
    return 0;
  }
  if (!(g_esc.abierta && g_esc.comic)) {   // el panel comico no lleva el marcador ni los avisos
    DibHud(dl, a, t);
    DibAviso(dl, a, t);
  }
  g_esc_auto = auto_on;
  DibEscena(dl, a, t, pressed);
  if (g_estado == kJuego && !g_luchando) {   // s9b: salida siempre posible: Y (triangulo) abre el menu en cualquier momento;
    // START tambien donde no es "aceptar" (guiones, ventanas, fase enemiga)
    const bool start_libre = g_fase_rival || HilosOcupados() || EscenaAbierta() || !g_mensajes.empty() || g_premio_id > 0 ||
                             g_espera_x;
    if (g_menu == 0 && ((mando & kY) || ((mando & kStart) && start_libre)) && t - g_turno_t >= 0.6f) {
      g_menu = 1;
      g_menu_sel = 0;
      g_menu_t = t;
      REXLOG_INFO("dbz3 tablero b2: menu abierto");
      pressed = 0;
      mando = 0;
    }
    if (g_menu > 0) return MenuTablero(dl, a, t, mando) ? 1 : 0;
    if (g_menu == 0 && (start_libre || g_elegir > 0)) {   // recordatorio de como salir donde la ayuda de abajo no sale
      S::HelpLine(dl, a.At(1040, 878), 30 * a.s, {{"Y", T::T("Menú / Salir", "Menu / Quit")}});
    }
  }
  if (g_estado == kJuego && g_elegir > 0 && !EscenaAbierta() && g_mensajes.empty()) {
    // orden 69: como el original (tarea 0x2A2550), las fichas libres en fila y "quedan N"; A elige, las elegidas salen
    // al tablero en su casilla del #YDW. ponytail: el panel es propio (la ventana DWU del original, CMN 402, sin portar)
    std::vector<int> libres;
    for (int c : g_cand)
      if (g_f[size_t(c)].fuera) libres.push_back(c);
    if (libres.empty()) {
      g_elegir = 0;
      return 0;
    }
    const int n = int(libres.size());
    g_comp = std::clamp(g_comp, 0, n - 1);
    // Encargo 01: la ventana de CMN 399 + DATA_<idioma> 184 sustituye al recuadro propio. Si el banco no esta
    // (otra ISO), DibAliados01 devuelve false y se queda el panel de antes: nunca se pierde la eleccion.
    const bool nativo01 = DibAliados01(dl, a, t, libres);
    if (!nativo01) {
      dl->AddRectFilled(a.At(90, 300), a.At(1110, 700), IM_COL32(8, 18, 54, 225), 16 * a.s);
      dl->AddRect(a.At(90, 300), a.At(1110, 700), IM_COL32(120, 170, 255, 230), 16 * a.s, 0, 3 * a.s);
      for (int i = 0; i < n; ++i) {
        const float w = i == g_comp ? 118.0f : 70.0f, x = 600 + (float(i) - float(g_comp)) * 76.0f +
                                                           (i > g_comp ? 26.0f : i < g_comp ? -26.0f : 0.0f);
        if (x < 170 || x > 1030) continue;
        DibFicha(dl, g_f[size_t(libres[size_t(i)])], a.At(x, i == g_comp ? 565.0f : 540.0f), w * a.s, t,
                 i == g_comp ? IM_COL32_WHITE : IM_COL32(150, 150, 170, 230));
      }
    }
    char q[96];
    std::snprintf(q, sizeof(q), T::T("Elige aliado (quedan %d)", "Choose an ally (%d left)"), g_elegir);
    // s9: con la ventana nativa el aviso va ya en la nube (frase + "quedan N"): el texto propio la tapaba
    if (!nativo01) S::MenuText(dl, a.At(600, 350), 38 * a.s, q, true, false);
    if (!nativo01)
      S::MenuText(dl, a.At(600, 640), 36 * a.s, Nombre(g_f[size_t(libres[size_t(g_comp)])]).c_str(), true, false);
    if (!nativo01) S::HelpLine(dl, a.At(600, 878), 34 * a.s, {{"LB", ""}, {"RB", T::T("Aliado", "Ally")}, {"A", T::T("Elegir", "Choose")}});
    const bool marcado = std::find(g_marcadas.begin(), g_marcadas.end(), libres[size_t(g_comp)]) != g_marcadas.end();
    const int quedan = g_elegir - int(g_marcadas.size());
    if (g_conf >= 0) {                                   // "Is this okay?" (FUN_002A1640)
      g_conf_t += 1.0f / 60.0f;
      if (pressed & (kUp | kDown | kLeft | kRight)) g_conf = 1 - g_conf;
      if (auto_on && g_conf_t > 0.8f) g_conf = 0, pressed |= kA;
      if ((pressed & kB) || ((pressed & (kA | kStart)) && g_conf == 1)) {   // No / cancelar: se desmarca el ultimo
        g_marcadas.pop_back();
        g_conf = -1;
      } else if (pressed & (kA | kStart)) {              // This is okay.
        g_conf = -1;
        std::vector<int> ms;
        ms.swap(g_marcadas);
        g_elegir = 0;
        g_comp = 0;
        int orden_aliado = 0;
        for (int im : ms) {
          Ficha& c = g_f[size_t(im)];
          // A2b: el aliado sale en la casilla 1, 2... SEGUN EL ORDEN EN QUE SE ELIGIO, no en la de su T2 (medido en PCSX2 en las
          // fases IV-VII: Gohan 2 -> 1, Goten 2 -> 2; fase VII Gohan 3 -> 1)
          if (++orden_aliado < int(g_d.casillas.size())) c.casilla = c.desde = orden_aliado;
          c.fuera = false;
          c.jugador = true;
          c.movs = c.movs_turno = 1;
          c.brote_t = t;
          REXLOG_INFO("dbz3 tablero b2: aliado elegido: {} (casilla {}), quedan {}", Nombre(c), c.casilla, 0);
          // A2: S5-A1 disparaba aqui el contacto {Goku, aliado} y dejaba a Goku sin movimientos; era un error de lectura del
          // savestate 3 (Goku ya se habia movido). Medido en las fases I-IX (A2\sstates): al confirmar, ningun guion; Goku, con su
          // "1" y la flecha roja, es la ficha seleccionada; el contacto sale cuando una ficha se mueve a la casilla de la otra.
        }
        RecalcularSlots();
        for (size_t i = 0; i < g_f.size(); ++i)
          if (g_f[i].n == 1 && !g_f[i].fuera) g_sel = int(i);
      }
      return 0;
    }
    // lista: arriba/abajo (tambien izquierda/derecha y LB/RB), sin dar la vuelta; X marca, Triangulo desmarca la fila marcada
    if ((pressed & (kLeft | kLB | kUp)) && g_comp > 0) --g_comp;
    if ((pressed & (kRight | kRB | kDown)) && g_comp < n - 1) ++g_comp;
    if (auto_on) pressed |= kA;
    if ((pressed & (kA | kStart)) && !marcado && quedan > 0) {
      g_marcadas.push_back(libres[size_t(g_comp)]);
      REXLOG_INFO("dbz3 tablero b2: aliado marcado: {}, quedan {}", Nombre(g_f[size_t(libres[size_t(g_comp)])]), g_elegir - int(g_marcadas.size()));
      if (int(g_marcadas.size()) == g_elegir) {
        g_conf = 1;
        g_conf_t = 0;
      }
    } else if ((pressed & kB) && marcado) {
      g_marcadas.erase(std::find(g_marcadas.begin(), g_marcadas.end(), libres[size_t(g_comp)]));
    }
    return 0;
  }
  if (g_estado == kJuego && (HilosOcupados() || EscenaAbierta())) return 0;   // el guion manda
  if (g_estado == kJuego && g_fase_rival && g_pend_j < 0 && g_menu == 0) {
    FaseRival();
    if (g_fase_rival && g_pend_j < 0) return 0;
  }
  if (g_estado == kGanada && g_fase_sig >= 0 && !std::getenv("DBZ3_TEST_B2_ROTULO")) {
    // S1-F01.1: orden 41 como el original (FUN_00294660, b2.c): sin rotulo; guardada la fase (orden 41, ya hecho), la Bola
    // se cierra hasta el negro (FUN_0024A120(0xB, 0x2382C0): 100 cuadros) y se pasa a la fase siguiente, que entra con el
    // iris que abre (FUN_0024A080(0xB, 0, 0) al empezar la fase). [H] el visor de sucesos de antes del cierre
    // (FUN_00270A50(0x1500): texto de DATA_<idioma> 289 con la VM de mensajes, FUN_0026E020(4, ...)) y el fundido de la
    // musica (FUN_00246190(.., -2)): sin portar.
    IrisCerrar(t);
    if (IrisCerrado(t)) {
      REXLOG_INFO("dbz3 tablero b2: fin de fase {}: iris cerrado, a la fase {}", g_d.fase + 1, g_fase_sig + 1);
      if (Cargar(g_fase_sig)) Preparar();
      g_iris_pend = true;
    }
    return 0;
  }
  if (g_estado == kGanada || g_estado == kPerdida) {
    const bool gana = g_estado == kGanada;
    dl->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, 120));
    if (!gana && g_tgo) {   // "GAME OVER" del original (404 #0)
      const float w = 860, h = w * g_tgo.h / g_tgo.w;
      dl->AddImage(g_tgo.id(), a.At(600 - w * 0.5f, 380 - h * 0.5f), a.At(600 + w * 0.5f, 380 + h * 0.5f));
    } else {
      S::MenuText(dl, a.At(600, 380), 64 * a.s, gana ? T::T("¡Fase completada!", "Stage complete!") : "GAME OVER", true, false);
    }
    if (gana) {   // las Bolas conseguidas en esta fase
      float x = 560;
      for (int n = 0; n < 7; ++n) {
        if (!((g_bolas & ~g_bolas_ini) >> n & 1)) continue;
        DibIcono(dl, 2, a.At(x, 440), a.At(x + 80, 520), ImVec4(40.0f * float(n), 0, 40.0f * float(n + 1), 40));
        x += 90;
      }
    }
    S::HelpLine(dl, a.At(600, 878), 34 * a.s, {{"A", gana ? T::T("Siguiente fase", "Next stage") : T::T("Reintentar", "Retry")},
                                               {"Y", T::T("Salir", "Quit")}});
    if (pressed & kY) return 1;
    if (pressed & (kA | kStart)) {
      if (gana && g_fase_sig < 0 && (g_bolas & 0x7F) == 0x7F) {   // orden 42 (fase IX) con las 7 Bolas   // las 7 Bolas tras la fase IX: Shenron
        g_estado = kDeseo;
        g_deseo = 0;
        g_shen = 0;
        REXLOG_INFO("dbz3 tablero b2: Shenron: {} deseos", g_tdeseos.size());
        return 0;
      }
      if (gana && g_fase_sig < 0) {   // orden 42 sin las 7 Bolas: el original borra la partida del Mundo Dragon
        g_bolas = 0;
        GuardarProgreso("tablero_b2_bolas", 0);
        GuardarProgreso("tablero_b2_fase", 0);  GuardarBanderas(true);
      }
      const int f = gana ? (g_fase_sig >= 0 ? g_fase_sig : 0) : g_d.fase;
      if (Cargar(f)) Preparar();
    }
    return 0;
  }
  if (g_estado == kDeseo) {   // Shenron (dw_shenglong.c): su ventana (CMN 850 en el sitio del #AMA 849) con sus frases
    // (537) y la lista de deseos en las 3 filas del #AMA (284, 320, 356), delante de Shenron en 3D (modelo de B2, CMN 327).
    // ponytail: sin sus efectos #SPX (329/331/332) ni el modelo del sitio (328 hijo 0): de fondo, sus nubes en 2D.
    if (!g_shen_cargado) {   // una vez: Shenron (CMN 327: #AMO 0 + #AMT 1) con su animacion (CMN 328 hijo 3, #AMM de 8
      // huesos: 290, 240 y 400 cuadros; la 2, la de 400, en bucle) y el cielo de la escena del deseo (CMN 328 hijo 1, #AMT
      // del sitio: nubes y suelo)
      g_shen_cargado = true;
      AfsPs2 cmn;
      if (cmn.Abrir(BuscarJuego("budokai 2"), "DATA_CMN.AFS")) {
        const std::vector<uint8_t> a327 = cmn.Entrada(327), a328 = cmn.Entrada(328);
        g_shen_mod = ps2::CargarSuelto(HijoAmb(a327, 0), HijoAmb(a327, 1), true);
        // Las claves de B2 van sobre el eje de reposo (Anim::relativa; sustituyendo, el cuerpo quedaba en columna: medido
        // 2026-10-16, RONDA4_PULIDO R2 octava vuelta). La 2 (400 cuadros) en bucle. PRUEBA: DBZ3_TEST_SHEN_ANIM=N (-1 quieto)
        const int na = std::getenv("DBZ3_TEST_SHEN_ANIM") ? std::atoi(std::getenv("DBZ3_TEST_SHEN_ANIM")) : 2;
        if (g_shen_mod.esq && na >= 0) {
          g_shen_mod.anim = ps2::LeerAnim(HijoAmb(a328, 3), uint32_t(na), g_shen_mod.esq->padre.size());
          g_shen_mod.anim.relativa = true;
        }
        if (g_shen_mod.esq) {   // instrumento: listas de pesos sin peso (con Posar iban al origen) y huesos fuera de rango
          const auto& e = *g_shen_mod.esq;
          size_t sin_peso = 0, fuera = 0, verts = 0, usados = 0;
          for (const auto& l : e.pesos) {
            float sw = 0;
            for (const auto& q : l) {
              sw += q.w;
              fuera += q.hueso >= e.padre.size();
            }
            sin_peso += sw <= 1e-6f;
          }
          for (int32_t lp : e.pesos_de) {
            ++verts;
            usados += lp >= 0 && e.pesos[size_t(lp)].size() > 0;
          }
          REXLOG_INFO("dbz3 tablero b2: Shenron 3D: {} listas de pesos ({} sin peso, {} pesos con hueso fuera), {} de {} "
                      "vertices pesados", e.pesos.size(), sin_peso, fuera, usados, verts);
        }
        g_shen_tx = ps2::Texturas(g_shen_mod);
        const std::vector<uint8_t> sitio = HijoAmb(a328, 1);
        if (CuantasAmt(sitio) > 0) g_shen_nubes = ps2::Crear(TexturaAmt(sitio, 0));
        // el sitio sin centrar y movido como Shenron (Posar le quita a el su centro x/z): quedan en el mismo sitio que en B2
        if (ps2::LeerAmo(HijoAmb(a328, 0), 0, g_shen_sitio.malla, nullptr, true) && g_shen_mod.esq) {   // con sus colores
          for (auto& tr : g_shen_sitio.malla.tris)
            for (auto& v : tr.p) v = {v.x - g_shen_mod.esq->centro.x, v.y, v.z - g_shen_mod.esq->centro.z};
          for (int j = 0, n = CuantasAmt(sitio); j < n; ++j) g_shen_sitio.tex.push_back(TexturaAmt(sitio, j));
          g_shen_sitio_tx = ps2::Texturas(g_shen_sitio);
        }
        g_shen_bolas = ps2::CargarSuelto(HijoAmb(a328, 7), HijoAmb(a328, 8), true);
        if (g_shen_bolas.esq) {
          g_shen_bolas.anim = ps2::LeerAnim(HijoAmb(a328, 9), 0, g_shen_bolas.esq->padre.size());
          g_shen_bolas.anim.relativa = true;
        }
        g_shen_bolas_tx = ps2::Texturas(g_shen_bolas);
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
        for (const auto& tr : g_shen_bolas.malla.tris)
          for (const auto& v : tr.p) {
            x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
            z0 = std::min(z0, v.z); z1 = std::max(z1, v.z);
          }
        REXLOG_INFO("dbz3 tablero b2: Shenron: Bolas 3D {} triangulos, x {:.1f}..{:.1f} y {:.1f}..{:.1f} z {:.1f}..{:.1f}, anim {} cuadros",
                    g_shen_bolas.malla.tris.size(), x0, x1, y0, y1, z0, z1, g_shen_bolas.anim.cuadros);
      }
      REXLOG_INFO("dbz3 tablero b2: Shenron 3D: {} triangulos, {} texturas, animacion de {} cuadros, nubes {}, sitio {} "
                  "triangulos", g_shen_mod.malla.tris.size(), g_shen_tx.size(), g_shen_mod.anim.cuadros, bool(g_shen_nubes),
                  g_shen_sitio.malla.tris.size());
    }
    // La aparicion (dw_shenglong.c + #SPX 329; medida en el emulador 2026-10-18, PCSX2 + savestate en "Show Results" con
    // las 7 Bolas, capturas en E:\DBZ3HD_trabajo\R7\b2\shenron): tiempos desde la pantalla 0xC sin su carga. 1,5-11 s las
    // 7 Bolas en el suelo del sitio (CMN 328 hijo 0) vistas desde arriba, saliendo de un blanco y brillando cada vez mas;
    // 9-13 s una columna de luz amarilla verdosa que acaba en blanco; 13-17 s blanco; 17-24 s Shenron entre niebla, sin
    // suelo: de lado junto a la cabeza, de frente muy cerca y alejandose hasta el plano final (cabeza arriba, el cuerpo
    // llenando la pantalla, fondo azul noche); desde 24 s la frase 535 y luego la 537 #0 y la lista. X a partir de 1,5 s
    // salta al plano final (FUN_0029B020: cuenta < 1410 y boton). ponytail: la luz y el brillo de las Bolas van en 2D (en el
    // original son el emisor 0x100 del #SPX 329, sin portar) y la camara son 3 planos medidos a ojo, no la de su timeline.
    static float shen_t0 = 0, shen_visto = -10;
    static bool shen_535 = true;   // "Tu, que has reunido las 7 Bolas..." antes de la 537 #0
    if (t - shen_visto > 0.5f) {   // entrada en la escena
      // PRUEBA: DBZ3_TEST_B2_DESEO=1 (prueba r2_deseo) empieza en la frase 537 #0, sin la aparicion; =2 con ella
      const char* p = std::getenv("DBZ3_TEST_B2_DESEO");
      const bool directo = p && p[0] == '1';
      shen_t0 = directo ? t - 24.0f : t;
      shen_535 = !directo;
    }
    shen_visto = t;
    float te = t - shen_t0;
    // PRUEBA: DBZ3_TEST_SHEN_T="s[:giro],..." congela la escena en esos segundos (giro = grados de la camara); A pasa al
    // siguiente sin tocar la logica (para capturas de cada fase con la prueba r2_deseo)
    static std::vector<std::pair<float, float>> fijos = [] {
      std::vector<std::pair<float, float>> v;
      if (const char* e = std::getenv("DBZ3_TEST_SHEN_T"))
        for (const char* q = e; *q;) {
          char* fin = nullptr;
          const float s0 = std::strtof(q, &fin);
          float g = -1000;
          if (fin && *fin == ':') g = std::strtof(fin + 1, &fin);
          if (!fin || fin == q) break;
          v.push_back({s0, g});
          q = *fin == ',' ? fin + 1 : fin;
        }
      return v;
    }();
    static size_t fijo = 0;
    float giro_prueba = -1000;
    if (fijo < fijos.size()) {
      te = fijos[fijo].first;
      giro_prueba = fijos[fijo].second;
      if (pressed & (kA | kStart)) ++fijo;
      pressed = 0;
      shen_535 = te < 24.0f;
    } else if (te < 24.0f) {
      if (te > 1.5f && (pressed & (kA | kStart))) shen_t0 = t - 24.0f;
      pressed &= uint16_t(~(kA | kStart));
    }
    auto tramo = [](float x, float x0, float x1) { return std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f); };
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const bool bolas = te < 13.0f, ve_shen = te >= 17.0f;
    {   // fondo: oscuro sobre las Bolas; con Shenron niebla gris que pasa a azul noche (con las nubes del sitio, 328 hijo 1)
      const float n = bolas ? 0.0f : tramo(te, 20.0f, 23.0f);
      auto mezcla = [&](int c0, int c1) { return int(float(c0) + (float(c1) - float(c0)) * n); };
      const ImU32 base = bolas ? IM_COL32(16, 20, 30, 255) : IM_COL32(mezcla(150, 22), mezcla(158, 26), mezcla(170, 50), 255);
      dl->AddRectFilled(ImVec2(0, 0), ds, base);
      if (g_shen_nubes && ve_shen) {
        const float lado = 256 * a.s * 1.6f, desp = std::fmod(t * 6.0f, 1.0f);
        dl->AddImage(g_shen_nubes.id(), ImVec2(0, 0), ds, ImVec2(desp, 0), ImVec2(desp + ds.x / lado, ds.y / lado),
                     IM_COL32(70, 80, 120, int(60 + 60 * n)));
      }
    }
    if (g_shen_mod.malla) {   // encuadre (una vez): del 2 % al 98 % de los vertices; Shenron llena la escena por encima de la
      // ventana (alto ~ 520 de los 900 del area), sin salirse
      static float cy = 0, dist = 0;
      if (dist <= 0) {
        std::vector<float> ys, xs;
        ps2::Malla p0;
        if (g_shen_mod.esq && g_shen_mod.anim) ps2::Posar(*g_shen_mod.esq, g_shen_mod.anim, 0.0f, p0);
        for (const auto& tr : (p0 ? p0 : g_shen_mod.malla).tris)
          for (const auto& v : tr.p) {
            ys.push_back(v.y);
            xs.push_back(std::max(std::abs(v.x), std::abs(v.z)));
          }
        auto q = [](std::vector<float>& v, float f) {
          const size_t k = size_t(f * float(v.size() - 1));
          std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
          return v[k];
        };
        const float y0 = q(ys, 0.02f), y1 = q(ys, 0.98f), xr = q(xs, 0.98f), tg = std::tan(0.785f * 0.5f);
        cy = (y0 + y1) * 0.5f;
        // alto (y1 - y0) en 480 px del area de 900 (f = 450 / tg) y ancho (2 xr) en 1000 px
        dist = std::max((y1 - y0) * 450.0f / (480.0f * tg), 2.0f * xr * 450.0f / (1000.0f * tg));
        REXLOG_INFO("dbz3 tablero b2: Shenron 3D: alto {:.1f}..{:.1f}, radio {:.1f}, distancia {:.1f}", y0, y1, xr, dist);
      }
      (void)cy;
      ps2::Camara cam;
      cam.fov = 0.785f;
      cam.c = ImVec2(a.p0.x + 600 * a.s, a.p0.y + 450 * a.s);
      cam.h = ImVec2(600 * a.s, 450 * a.s);
      // camara mirando a `obj` desde `giro` grados alrededor de y (0 = desde +z), `alza` grados por encima, a `d`
      auto mirar = [&cam](ps2::V3 obj, float giro, float alza, float d) {
        const float g = giro * 0.0174533f, h = alza * 0.0174533f;
        const ps2::V3 ojo{obj.x + d * std::sin(g) * std::cos(h), obj.y + d * std::sin(h), obj.z + d * std::cos(g) * std::cos(h)};
        ps2::V3 f{obj.x - ojo.x, obj.y - ojo.y, obj.z - ojo.z};
        const float lf = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        f = {f.x / lf, f.y / lf, f.z / lf};
        ps2::V3 r{-f.z, 0, f.x};   // f x (0, 1, 0)
        const float lr = std::max(1e-5f, std::sqrt(r.x * r.x + r.z * r.z));
        r = {r.x / lr, 0, r.z / lr};
        const ps2::V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
        auto pt = [](const ps2::V3& p, const ps2::V3& q) { return p.x * q.x + p.y * q.y + p.z * q.z; };
        const float m[16] = {r.x, u.x, -f.x, 0, r.y, u.y, -f.y, 0, r.z, u.z, -f.z, 0, -pt(r, ojo), -pt(u, ojo), pt(f, ojo), 1};
        std::memcpy(cam.m, m, sizeof(m));
        cam.ok = true;
      };
      // de frente = giro 0 (medido con DBZ3_TEST_SHEN_T: 90 = de perfil, 180 = la cara tapada por el cuerpo)
      static const float frente = std::getenv("DBZ3_TEST_SHEN_GIRO") ? float(std::atof(std::getenv("DBZ3_TEST_SHEN_GIRO"))) : 0.0f;
      const float gf = giro_prueba > -999 ? giro_prueba : frente;
      static ps2::Malla pose_bolas;
      if (bolas) {   // las 7 Bolas (centradas por CargarSuelto) desde arriba, acercandose
        // su animacion (420 cuadros: el brillo que crece) empieza a los 4 s, como el brillo del original
        if (g_shen_bolas.esq && g_shen_bolas.anim)
          ps2::Posar(*g_shen_bolas.esq, g_shen_bolas.anim, std::clamp(te - 4.0f, 0.0f, 6.9f), pose_bolas);
        // PRUEBA: DBZ3_TEST_SHEN_BOLAS=d distancia de la camara a las Bolas
        static const float prueba_d = std::getenv("DBZ3_TEST_SHEN_BOLAS") ? float(std::atof(std::getenv("DBZ3_TEST_SHEN_BOLAS"))) : 45.0f;
        mirar({0, 1.0f, 0}, gf, 55.0f, prueba_d * (1.0f - 0.15f * tramo(te, 1.5f, 11.0f)));
      } else {   // de lado junto a la cabeza (17 s), de frente muy cerca (21 s) y el plano final (24 s)
        const float k1 = tramo(te, 17.0f, 21.0f), k2 = tramo(te, 21.0f, 24.0f);
        // la cabeza esta en (-1, 14.9, 0) mirando hacia +z y abajo: los planos van desde debajo de ella
        const float giro = gf + 75.0f * (1 - k1), alto = 14.5f - 1.2f * k2, d = 8.0f - 1.0f * k1 + 3.0f * k2;
        mirar({-1.0f, alto, 0}, giro, -10.0f - 20.0f * k1 + 10.0f * k2, d);   // plano final: la cabeza arriba
      }
      static ps2::Malla pose;
      const bool anima = g_shen_mod.esq && g_shen_mod.anim;
      if (anima) ps2::Posar(*g_shen_mod.esq, g_shen_mod.anim, t, pose);
      ps2::Escena esc;
      esc.Empezar();
      if (bolas && g_shen_sitio.malla) esc.Poner(cam, g_shen_sitio.malla, g_shen_sitio_tx, {0, 0, 0}, 0.0f, 1.0f);
      if (bolas && g_shen_bolas.malla)
        esc.Poner(cam, pose_bolas ? pose_bolas : g_shen_bolas.malla, g_shen_bolas_tx, {0, 0, 0}, 0.0f, 1.0f);
      // sin toon: con el toon de P4 (Toon{}, borde 0.15) Shenron animado salia roto (triangulos enormes por toda la
      // pantalla; captura D:/DBZ3HD/_pruebas/20261016-074244_escenarios_test3_r2_deseo). Avisado a P4 en RONDA4_PULIDO.
      // PRUEBA (P4): DBZ3_TEST_SHEN_TOON=1 lo pinta con el toon de ps2_modelo (normales basura de B2 -> sin normal)
      static const char* toon_prueba = std::getenv("DBZ3_TEST_SHEN_TOON");   // "2": sin borde
      ps2::Escena::Toon toon_p;
      if (toon_prueba && toon_prueba[0] == '2') toon_p.borde = 0;
      if (!ve_shen) {
      } else if (toon_prueba) {
        esc.Poner(cam, anima ? pose : g_shen_mod.malla, g_shen_tx, {0, 0, 0}, 0.0f, 1.0f, toon_p);
      } else {
        esc.Poner(cam, anima ? pose : g_shen_mod.malla, g_shen_tx, {0, 0, 0}, 0.0f, 1.0f);
      }
      esc.Pintar(dl);
    }
    {   // brillo de las Bolas, columna de luz, blancos y negro de la carga (2D, sobre la escena)
      const ImVec2 c(ds.x * 0.5f, ds.y * 0.5f);
      const float brillo = tramo(te, 4.0f, 9.0f) * (1 - tramo(te, 12.0f, 13.0f));
      for (int k = 6; k >= 1 && brillo > 0; --k)
        dl->AddCircleFilled(c, ds.y * (0.06f + 0.05f * float(k)) * (0.6f + 0.6f * brillo), IM_COL32(255, 240, 170, int(26 * brillo)), 48);
      const float haz = tramo(te, 9.0f, 11.5f) * (1 - tramo(te, 13.0f, 13.5f));
      if (haz > 0) {
        const float w = ds.x * (0.05f + 0.25f * haz);
        const ImU32 borde = IM_COL32(200, 230, 120, 0), medio = IM_COL32(250, 255, 210, int(255 * haz));
        dl->AddRectFilledMultiColor(ImVec2(c.x - w, 0), ImVec2(c.x, ds.y), borde, medio, medio, borde);
        dl->AddRectFilledMultiColor(ImVec2(c.x, 0), ImVec2(c.x + w, ds.y), medio, borde, borde, medio);
      }
      const float blanco = std::max({1 - tramo(te, 1.5f, 3.5f), tramo(te, 11.0f, 13.0f) * (1 - tramo(te, 17.0f, 18.5f))});
      if (te < 1.5f)
        dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 255));
      else if (blanco > 0)
        dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(255, 255, 255, int(255 * blanco)));
    }
    const float s = std::min(1200.0f / 640.0f, 900.0f / 448.0f) * a.s;
    const ImVec2 o0(a.p0.x + (1200.0f * a.s - 640.0f * s) * 0.5f, a.p0.y + (900.0f * a.s - 448.0f * s) * 0.5f);
    auto P = [&](float x, float y) { return ImVec2(o0.x + x * s, o0.y + y * s); };
    // el original solo pone la ventana con la lista; las frases van sueltas abajo, en blanco (capturas del emulador)
    if (te >= 24.0f && g_shen == 1 && !g_d.shen_ama.empty() && !g_tshen_marco.empty() && g_tshen_marco[0]) {
      const Datos::Sprite& m = g_d.shen_ama[0];
      dl->AddImage(g_tshen_marco[0].id(), P(m.px + m.x0, m.py + m.y0 * 1.1f), P(m.px + m.x1, m.py + m.y1 * 1.1f));
    }
    auto texto = [&](const Tex& x, float cy, float alto, ImU32 col) {   // imagen centrada en x 320, como mucho 344 de ancho
      const float esc = std::min(alto / x.h, 344.0f / x.w);
      dl->AddImage(x.id(), P(320 - x.w * esc * 0.5f, cy - x.h * esc * 0.5f), P(320 + x.w * esc * 0.5f, cy + x.h * esc * 0.5f),
                   ImVec2(0, 0), ImVec2(1, 1), col);
    };
    const int n = int(g_tdeseos.size());
    if (te < 24.0f) {
    } else if (g_shen == 1 && n > 0) {
      if (pressed & kUp) g_deseo = (g_deseo + n - 1) % n;
      if (pressed & kDown) g_deseo = (g_deseo + 1) % n;
      const float filas[3] = {284, 320, 356};
      dl->AddRectFilled(P(146, filas[1] - 15), P(494, filas[1] + 15), IM_COL32(40, 110, 220, 230));   // barra de la elegida
      for (int k = -1; k <= 1; ++k) {
        const int i = (g_deseo + k + n) % n;
        if (g_tdeseos[size_t(i)]) texto(g_tdeseos[size_t(i)], filas[k + 1], 26, k == 0 ? IM_COL32(255, 230, 90, 255) : IM_COL32(170, 170, 190, 220));
      }
    } else {
      // tras pedir: "Si, eso es aceptable." (#1) o, si el deseo no se puede cumplir aqui, la frase del original "No puedo
      // conceder ese deseo..." (537 #3; antes un aviso propio en letra de ImGui y despues el #1, que se contradecian)
      const int f = g_shen == 0 ? (shen_535 ? 6 : 0) : g_shen == 2 ? (g_shen_no ? 3 : 1) : 5;
      if (f < int(g_tshen_frases.size()) && g_tshen_frases[size_t(f)])
        texto(g_tshen_frases[size_t(f)], 392, g_tshen_frases[size_t(f)].h, IM_COL32_WHITE);   // a su tamano, abajo
    }
    if (g_shen == 0 && shen_535 && te >= 24.0f && (pressed & (kA | kStart))) {
      shen_535 = false;
      pressed = 0;
    }
    S::HelpLine(dl, a.At(600, 878), 38 * a.s, {{"A", g_shen == 1 ? T::T("Pedir", "Wish") : T::T("Seguir", "Continue")},
                                               {"Y", T::T("Salir", "Quit")}});
    if (auto_on && te < 24.0f && fijo >= fijos.size()) shen_t0 = t - 24.0f;
    if (auto_on && te >= 24.0f) pressed |= kA;
    if (pressed & kY) return 1;
    if (pressed & (kA | kStart)) {
      if (g_shen == 1) {
        g_shen_no = false;
        if (g_deseo == 0) {
          GuardarProgreso("tablero_b2_nave", 1);
          REXLOG_INFO("dbz3 tablero b2: deseo: portal de la nave de Babidi (Nave abierta)");
        } else if (DeseoB3(g_deseo)) {
          DarCapsulaB3(DeseoB3(g_deseo));
        } else {   // ponytail: sin equivalente en B3 (lista arriba); ahora el jugador lo ve
          REXLOG_INFO("dbz3 tablero b2: deseo {} sin equivalente en B3", g_deseo);
          g_shen_no = true;
        }
      }
      if (g_shen < 3) {
        ++g_shen;
        return 0;
      }
      g_shen = 0;
      g_bolas = 0;   // las Bolas se dispersan y se vuelve a la fase I
      GuardarProgreso("tablero_b2_bolas", 0);
      GuardarProgreso("tablero_b2_fase", 0);  GuardarBanderas(true);
      if (Cargar(0)) Preparar();
    }
    return 0;
  }

  // combate pendiente: el "!!" se ve un momento y se pide el combate
  if (g_pend_j >= 0 && !g_luchando) {
    if (t - g_pend_t < 0.5f || HilosOcupados() || EscenaAbierta()) return 0;   // ~30 cuadros (0x235780)
    const Ficha &j = g_f[size_t(g_pend_j)], &r = g_f[size_t(g_pend_r)];
    {   // s9: la tarjeta "Battle!" del original (condicion de la casilla; START o A empieza el combate)
      static const int cond_forzada = std::getenv("DBZ3_TEST_B2_CONDICION") ? std::atoi(std::getenv("DBZ3_TEST_B2_CONDICION")) : 0;
      static float t_tarjeta = -1;
      if (t_tarjeta < 0) t_tarjeta = t;
      const float c = (t - t_tarjeta) * 60.0f;
      const int cond = cond_forzada > 0 ? cond_forzada : g_d.casillas[size_t(r.casilla)].condicion;
      if (DibTarjeta(dl, a, t, c, cond, j)) {
        // la prueba automatica la acepta cuando ya esta asentada (cuadro 80 + medio segundo)
        if (c < 80.0f || !((pressed & (kStart | kA)) || (auto_on && c > 110.0f))) return 0;
      }
      t_tarjeta = -1;
      pressed = 0;
    }
    out[0] = B2aB3(j.c);
    out[1] = B2aB3(r.c);
    {   // escenario de B2 de la casilla (alta instalada: -(registro + 1); si no, el mapa de B3 de la fase)
      static const int forzado = std::getenv("DBZ3_TEST_B2_ESCENARIO") ? std::atoi(std::getenv("DBZ3_TEST_B2_ESCENARIO")) : -1;
      const int e = forzado >= 0 ? forzado : g_d.casillas[size_t(r.casilla)].escenario;   // PRUEBA: fuerza el 0-12
      const int reg = e >= 0 && e < 13 && kEscenarioB2[e] ? dbz3::escenarios::PorMod(kEscenarioB2[e]) : -1;
      out[2] = reg >= 0 ? -(reg + 1) : kMapaB3[g_d.fase];
      REXLOG_INFO("dbz3 tablero b2: combate en el escenario {} de B2 ({}) -> {}", e, e >= 0 && e < 13 && kEscenarioB2[e] ?
                  kEscenarioB2[e] : "sin alta", reg >= 0 ? "registro " + std::to_string(reg) : "mapa B3 " + std::to_string(out[2]));
    }
    out[3] = j.vida;
    out[4] = r.vida_max != 3000 ? r.vida_max : 0;
    out[5] = g_d.casillas[size_t(r.casilla)].condicion;
    out[6] = j.ataque;
    out[7] = j.defensa;
    // studies 2 G04: contrato ampliado a 14 enteros (0-7 conservan su significado). Son indices CRUDOS de B2; la
    // traduccion a B3 no esta medida ([H]). El consumidor debe reservar int[14] (pasarela_misiones.cpp).
    out[8] = j.forma_b2;    // orden 52: +0x19 (byte)
    out[9] = j.traje_b2;    // orden 52: +0x1A (word)
    out[10] = r.forma_b2;
    out[11] = r.traje_b2;
    out[12] = j.ki_b2;      // orden 53: +0x16 (u16; -1 = sin tocar)
    out[13] = r.ki_b2;
    // Orden 54 (+0x14, i16): la vida inicial va por los indices de vida que ya existen (3 jugador, 4 rival < 0).
    if (j.vida_b2 >= 0) out[3] = j.vida_b2;
    if (r.vida_b2 > 0) out[4] = -r.vida_b2;
    // Orden 51: el +0x18 del rival (0x699365). Sin indice libre en el contrato de 14: se entrega con ComRivalB2().
    g_com_rival_b2 = r.com_b2;
    // PRUEBA: DBZ3_TEST_B2_CONDICION=N fuerza la condicion N (1-5) en todos los combates del tablero
    static const int forzada = std::getenv("DBZ3_TEST_B2_CONDICION") ? std::atoi(std::getenv("DBZ3_TEST_B2_CONDICION")) : 0;
    if (forzada > 0) out[5] = forzada;
    if (out[5] == 1) {   // empiezan con media vida (la maxima, la de siempre)
      out[3] = std::min(j.vida > 0 ? j.vida : j.vida_max, j.vida_max / 2);
      out[4] = -(r.vida_max / 2);
    }
    g_luchando = true;
    REXLOG_INFO("dbz3 tablero b2: combate: {} (id {}) contra {} (id {}), turno {}, condicion {}", Nombre(j), out[0],
                Nombre(r), out[1], g_turno, out[5]);
    return 2;
  }
  if (g_pend_j >= 0) return 0;
  if (g_menu > 0) return 0;   // el menu ya se atendio arriba (MenuTablero)
  // botones de B2 (ELF, control del tablero): cruz mueve, cuadrado busca, circulo (B) pasa, triangulo o Start (Y) abren el
  // menu, L1 esconde o saca el marcador de abajo. RB cambia de ficha (propio).
  AyudaMarquesina(dl, a, t);
  if (t - g_turno_t < 0.6f) return 0;   // entrada del rombo del turno: sin mandos (asi no se encadenan turnos con B)
  if (pressed & (kY | kStart)) {
    g_menu = 1;
    g_menu_sel = 0;
    g_menu_t = t;
    REXLOG_INFO("dbz3 tablero b2: menu abierto");
    return 0;
  }
  Ficha& f = g_f[size_t(g_sel)];
  if (pressed & kLB) {
    SonidoEventoB2("marcador", 3);   // FUN_00236EA0 (b2.c:189786-189789): L1 esconde o saca el marcador
    g_hud_oculto = !g_hud_oculto;
    g_hud_t = g_radar_t = t;   // sale o entra con su animacion
    GuardarProgreso("tablero_b2_marcador", g_hud_oculto ? 0 : 1);   // bit 0x08 de 0x6992DC, va con la partida
  }
  if (pressed & kRB) SiguienteSel();
  // A2: el original tiene DOS pasos (FUN_002798A0 y FUN_002793E0, medido en PCSX2): "Select a Play Character" (las flechas pasan a
  // la ficha del jugador mas cercana en esa direccion; X la elige) y "Select the Map Point Where You are Going" (el destino arranca
  // en la primera vecina de la casilla, las flechas cambian a otra vecina, X mueve, Circulo/Triangulo vuelve al paso 1).
  // g_dest > 0 = paso 2.
  const bool paso2 = g_dest > 0;
  if (pressed & (kUp | kDown | kLeft | kRight)) {
    const int h = (pressed & kRight) ? 1 : (pressed & kLeft) ? -1 : 0;
    const int v = (pressed & kDown) ? 1 : (pressed & kUp) ? -1 : 0;
    // A2b: la regla exacta de FUN_002798A0 / FUN_002793E0 (b2.c:223522, 223682): el candidato mas cercano EN EL EJE pulsado
    // (dx2 / dy2, o (dx2+dy2)/2 en diagonal); sin ninguno en esa direccion, el mas lejano en ese eje (vuelta). En el paso 2 las
    // distancias se miden desde el destino ACTUAL (no desde la ficha) y los candidatos son las vecinas menos el destino actual.
    // ponytail: la exclusion de vecinas con ficha aliada "+0xC == 1" del original no esta medida [H].
    auto elegir = [&](int actual, const std::vector<int>& cand, auto pos, bool dest) {
      const ImVec2 p0 = pos(actual);
      float mejor = 1e7f, lejos = 0;
      int ib = actual, il = actual;
      for (int id : cand) {
        const ImVec2 p = pos(id);
        const float dx = p.x - p0.x, dy = p.y - p0.y;
        const bool hm = (h > 0 && dx > 0) || (h < 0 && (dest ? dx <= 0 : dx < 0));
        const bool vm = (v > 0 && dy > 0) || (v < 0 && (dest ? dy <= 0 : dy < 0));
        const bool vs = (v > 0 && dy > 0) || (v < 0 && dy < 0);
        float s = -1;
        if (hm) s = (dest ? vm : vs) ? (dx * dx + dy * dy) / 2 : dx * dx;
        else if (vs) s = dy * dy;
        if (s >= 0 && (dest ? s < mejor : s <= mejor)) {
          mejor = s;
          ib = id;
        }
        const float lj = v == 0 ? dx * dx : h == 0 ? dy * dy : dx * dx + dy * dy;
        if (lj > lejos) {
          lejos = lj;
          il = id;
        }
      }
      return ib == actual ? il : ib;
    };
    if (!paso2) {   // otra ficha del jugador
      std::vector<int> cand;
      for (size_t i = 0; i < g_f.size(); ++i)
        if (int(i) != g_sel && Activa(g_f[i]) && g_f[i].jugador && g_f[i].casilla > 0) cand.push_back(int(i));
      const int nueva = elegir(g_sel, cand, [&](int i) {
        const Casilla& c = g_d.casillas[size_t(g_f[size_t(i)].casilla)];
        return ImVec2(c.x, c.y);
      }, false);
      if (nueva != g_sel) {
        g_sel = nueva;
        SonidoEventoB2("cursor", 0);   // FUN_002798A0 (b2.c:223825-223827)
      }
    } else {
      std::vector<int> cand;
      for (int vv : g_d.casillas[size_t(f.casilla)].vec)
        if (vv > 0 && vv != g_dest && !CasillaLlena(f, vv)) cand.push_back(vv);
      const int nuevo = elegir(g_dest, cand, [&](int i) { return ImVec2(g_d.casillas[size_t(i)].x, g_d.casillas[size_t(i)].y); }, true);
      if (nuevo != g_dest) {
        g_dest = nuevo;
        SonidoEventoB2("cursor", 0);
      }
    }
  }
  if ((pressed & kA) && paso2 && f.movs > 0) Mover(g_sel, g_dest);
  else if ((pressed & kA) && !paso2 && f.movs > 0) {   // paso 1 -> 2: el destino empieza en la primera vecina
    for (int v : g_d.casillas[size_t(f.casilla)].vec)
      if (v > 0 && !CasillaLlena(f, v)) {   // FUN_00278970: sin casillas llenas (dos fichas del mismo bando)
        g_dest = v;
        break;
      }
    if (g_dest > 0) SonidoEventoB2("cursor", 0);
  } else if ((pressed & kX) && f.movs > 0) Buscar(g_sel);
  else if (pressed & kB) {
    if (paso2) {
      g_dest = -1;
    } else {
      f.movs = 0;
      FinDeAccion();
    }
  }
  return 0;
}

// ---- Mods opcionales de audio del tablero (encargo 13) ----
// M4 (mod opcional b2.musica_combate): la pista del combate de una casilla. La lista de 24 posiciones (20 pistas
// validas; -1 = sin musica) es la del ELF (tabla PTR_DAT_003749A4 -> VA 0x3888A0, valores 68..87, 0 al final;
// b2.c:137069-137071) y el indice es el byte T1+0x1F de la casilla: 17, 1, 4, 6, 20, 21, 7, 15, 22 en las fases
// I-IX (medido en los 9 #YDW, 2026-10-18). Si el opcional.toml no trae la lista, vale la de aqui (verificada).
int PistaCombateB2(int indice) {
  if (!dbz3::opcional::Activo("b2.musica_combate") || indice < 0) return -1;
  static const int kLista[24] = {68, 69, 70, 71, 72, 73, 74, 75, 76, -1, -1, -1, 83, 84, 85, 86, 87, 77, 78, 79, 80,
                                 81, 82, 0};
  std::vector<int> lista = dbz3::opcional::Lista("b2.musica_combate", "lista");
  if (lista.size() < 24) lista.assign(kLista, kLista + 24);
  return indice < int(lista.size()) ? lista[size_t(indice)] : -1;
}
// M5 (mod opcional b2.musica_menus): indice de la tabla de musica del tablero (0x377E90) del menu abierto. Los
// indices de menu son 13 (MNU_BGM), 15 (MNT_BGM) y 17 (MNO_SYS): los piden FUN_0023f4b0, FUN_002422a0 y
// FUN_0025db40 y su nombre de recurso esta en b2.elf VA 0x3A20F0, 0x3A2228 y 0x3A27D0 (re-verificado 2026-10-18);
// la lista sale de [indices_menu] del toml. Reparto pantalla->indice [H] (las pantallas del original no son las del
// port): menu del tablero (Salir/Reiniciar y su Si/No) = 13, eleccion de aliados = 15, pantalla de los deseos = 17.
// -1 = sin menu (la pista de la fase de siempre).
int PistaMenuB2() {
  if (!dbz3::opcional::Activo("b2.musica_menus")) return -1;
  std::vector<int> idx = dbz3::opcional::Lista("b2.musica_menus", "indices_menu");
  if (idx.size() < 3) idx = {13, 15, 17};
  if (g_menu > 0) return idx[0];
  if (g_estado == kJuego && g_elegir > 0 && !EscenaAbierta() && g_mensajes.empty()) return idx[1];
  if (g_estado == kDeseo) return idx[2];
  return -1;
}

// La musica del Mundo Dragon: la pista de cada fase de la tabla de B2 (ELF .data 0x377E90, u32 0x20000 | entrada de
// ADX_US.AFS: los indices 0-9 -> 265..274, que el tablero pide por fase; R6 2026-10-17). Se corta al ir a un combate o al
// salir y vuelve al regresar al tablero. R7: el indice lo pone la orden 71 (g_musica), no la fase (ver PistaB2).
// ---- S1-F01.2: iris de la Bola de Dragon al volver de un combate (y al empezar la fase) ----
// B2 abre el tablero con FUN_0024A080(0xB, ...) (b2.c: vuelta del combate 0x2382A0 y entrada de la fase): tipo 0xB = al
// azar 1..8 (rand & 7) + 1, dibujado por FUN_00248990 (tabla 0x377EE0) con p de 1 a 0 a 0,01 por cuadro (100 cuadros,
// FUN_00249CB0). En el lienzo de 640x448, centro (320, 224), y por 0,94:
//  - la Bola: abanico de radio 551 (1 - p); centro naranja (1, 0,5, 0) con alfa p; borde (k, k/2, k/10) (tope 1) con
//    k = (cos(2 pi i / 64) * 0,5 + 0,5) * 1,5 + 0,75 (claro arriba a la izquierda) y alfa min(1, 2p);
//  - tipos 1-7: tantas estrellas de 5 puntas (radios R y R * 0,381966, primera punta a 260 grados) de color (1, 0,5, 0) y
//    alfa min(1, 2p); R y los centros de la tabla del ELF (PTR 0x377F2C: {R, x0, y0, ...}) * 1,5 * (1 - p), girando
//    -0,5 grados por cuadro; tipo 8: la "Z" (tira de 16 puntos de la misma tabla), sin girar;
//  - el brillo: abanico blanco de radio 60 (1 - p), centro alfa min(1, 2p) y borde p / 2, desplazado
//    ((1 - p) + (1 - p)^2) * 100 arriba a la izquierda;
//  - fuera de la Bola, negro (el cuadro de toda la pantalla con z = 3 detras de las figuras).
// Las tablas se leen del ELF del jugador buscando la de 2 estrellas {75, -75, -75, 75, 75}; sin ellas, la Bola sin estrellas.
struct IrisTablas {
  bool ok = false;
  std::vector<float> t[9];   // t[n] = {R, x0, y0, ...}; t[8] = 16 puntos (la Z)
};
const IrisTablas& TablasIris() {
  static const IrisTablas it = [] {
    IrisTablas r;
    const std::vector<uint8_t>& e = dbz3::pasarela::nave::ElfB2();
    if (e.size() < 0x100 || std::memcmp(e.data(), "\x7F" "ELF", 4) != 0) return r;
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= e.size() ? uint32_t(e[o]) | uint32_t(e[o + 1]) << 8 | uint32_t(e[o + 2]) << 16 | uint32_t(e[o + 3]) << 24 : 0; };
    const uint32_t phoff = u32(0x1C);
    const int phn = e[0x2C] | e[0x2D] << 8;
    auto va2off = [&](uint32_t va) -> size_t {   // direccion virtual -> posicion en el fichero (PT_LOAD)
      for (int i = 0; i < phn; ++i) {
        const size_t h = phoff + 32 * size_t(i);
        const uint32_t off = u32(h + 4), v = u32(h + 8), fs = u32(h + 16);
        if (u32(h) == 1 && va >= v && va < v + fs) return off + (va - v);
      }
      return SIZE_MAX;
    };
    auto off2va = [&](size_t o) -> uint32_t {
      for (int i = 0; i < phn; ++i) {
        const size_t h = phoff + 32 * size_t(i);
        const uint32_t off = u32(h + 4), v = u32(h + 8), fs = u32(h + 16);
        if (u32(h) == 1 && o >= off && o < size_t(off) + fs) return uint32_t(v + (o - off));
      }
      return 0;
    };
    auto f = [&](size_t o) { const uint32_t u = u32(o); float x; std::memcpy(&x, &u, 4); return x; };
    const float firma[5] = {75, -75, -75, 75, 75};
    const auto* fb = reinterpret_cast<const uint8_t*>(firma);
    const auto it2 = std::search(e.begin(), e.end(), fb, fb + sizeof(firma));
    if (it2 == e.end()) return r;
    const uint32_t va2 = off2va(size_t(it2 - e.begin()));
    for (size_t o = 0; va2 && o + 4 <= e.size(); o += 4) {   // la tabla de punteros: [2] apunta a la de 2 estrellas
      if (u32(o) != va2 || o < 8) continue;
      bool bien = true;
      for (int n = 1; n <= 8 && bien; ++n) {
        const size_t q = va2off(u32(o + 4 * size_t(n - 2)));
        const int cuantos = n == 8 ? 32 : 1 + 2 * n;
        if (q == SIZE_MAX || q + 4 * size_t(cuantos) > e.size()) { bien = false; break; }
        r.t[n].clear();
        for (int k = 0; k < cuantos; ++k) r.t[n].push_back(f(q + 4 * size_t(k)));
      }
      if (bien && r.t[1][0] > 0 && r.t[1][0] < 1000) { r.ok = true; break; }
    }
    REXLOG_INFO("dbz3 tablero b2: iris: tablas de estrellas del ELF {}", r.ok ? "leidas" : "no encontradas (Bola sin estrellas)");
    return r;
  }();
  return it;
}
float g_iris_t0 = -1, g_iris_ang = 0, g_iris_ult = -1;
int g_iris_tipo = 1;
bool g_iris_cierra = false;   // S1-F01.1: FUN_0024A120 (p de 0 a 1: la Bola se encoge hasta el negro) en vez de abrir
bool IrisActivo(float t) {
  return g_iris_pend || (g_iris_t0 >= 0 && (g_iris_cierra || (t - g_iris_t0) * 60.0f / Lento() < 100.0f));
}
void IrisTipo() {
  static std::mt19937 rng{std::random_device{}()};
  g_iris_tipo = int(rng() & 7) + 1;
  if (const char* v = std::getenv("DBZ3_TEST_B2_IRIS")) g_iris_tipo = std::clamp(std::atoi(v), 1, 8);   // PRUEBA
}
// S1-F01.1: empieza el cierre (orden 41); IrisCerrado dice cuando ha llegado al negro (100 cuadros).
void IrisCerrar(float t) {
  if (g_iris_cierra && g_iris_t0 >= 0) return;
  g_iris_pend = false;
  g_iris_cierra = true;
  g_iris_t0 = t;
  g_iris_ult = -1;
  IrisTipo();
  REXLOG_INFO("dbz3 tablero b2: iris de la Bola que cierra (tipo {})", g_iris_tipo);
}
bool IrisCerrado(float t) { return g_iris_cierra && g_iris_t0 >= 0 && (t - g_iris_t0) * 60.0f / Lento() >= 100.0f; }
void DibIris(ImDrawList* dl, const S::Area& a, float t) {
  if (g_iris_pend.exchange(false)) {
    g_iris_t0 = t;
    g_iris_ult = -1;
    g_iris_cierra = false;
    IrisTipo();
    REXLOG_INFO("dbz3 tablero b2: iris de la Bola (tipo {})", g_iris_tipo);
  }
  if (g_iris_t0 < 0) return;
  const float cuadros = (t - g_iris_t0) * 60.0f / Lento();
  const float p = g_iris_cierra ? std::min(1.0f, cuadros * 0.01f) : 1.0f - cuadros * 0.01f;
  if (p <= 0 && !g_iris_cierra) {
    g_iris_t0 = -1;
    return;
  }
  if (g_iris_ult >= 0) g_iris_ang -= 0.008726644f * std::max(0.0f, (t - g_iris_ult) * 60.0f / Lento());
  g_iris_ult = t;
  const ImVec2 W = ImGui::GetIO().DisplaySize;
  const float ls = std::min(1200.0f / 640.0f, 900.0f / 448.0f) * a.s;
  const ImVec2 lo(a.p0.x + (1200.0f * a.s - 640.0f * ls) * 0.5f, a.p0.y + (900.0f * a.s - 448.0f * ls) * 0.5f);
  auto P = [&](float x, float y) { return ImVec2(lo.x + x * ls, lo.y + y * ls); };
  auto col = [](float r, float g, float b, float al) {
    return IM_COL32(int(255 * std::clamp(r, 0.0f, 1.0f)), int(255 * std::clamp(g, 0.0f, 1.0f)), int(255 * std::clamp(b, 0.0f, 1.0f)),
                    int(255 * std::clamp(al, 0.0f, 1.0f)));
  };
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  auto tri = [&](ImVec2 p0, ImU32 c0, ImVec2 p1, ImU32 c1, ImVec2 p2, ImU32 c2) {
    dl->PrimReserve(3, 3);
    dl->PrimVtx(p0, uv, c0);
    dl->PrimVtx(p1, uv, c1);
    dl->PrimVtx(p2, uv, c2);
  };
  const float q = 1.0f - p, a2 = std::min(1.0f, 2.0f * p), cx = 320, cy = 224;
  const float R = q * 551.0f;
  constexpr float kPi = 3.141592f;
  // la Bola
  if (p < 1.0f) {
    const ImU32 centro = col(1, 0.5f, 0, p);
    ImVec2 ant;
    ImU32 cant = 0;
    for (int i = 0; i <= 64; ++i) {
      const float an = float(i) * kPi * 2.0f / 64.0f + 3.92699f;
      const float k = (std::cos(float(i) * kPi * 2.0f / 64.0f) * 0.5f + 0.5f) * 1.5f + 0.75f;
      const ImVec2 v = P(cx + std::cos(an) * R, cy + std::sin(an) * R * 0.94f);
      const ImU32 c = col(k, k * 0.5f, k * 0.1f, a2);
      if (i > 0) tri(P(cx, cy), centro, ant, cant, v, c);
      ant = v;
      cant = c;
    }
    // las estrellas (1-7) o la Z (8)
    const IrisTablas& tb = TablasIris();
    const ImU32 naranja = col(1, 0.5f, 0, a2);
    if (tb.ok && g_iris_tipo == 8) {
      const std::vector<float>& z = tb.t[8];
      std::array<ImVec2, 16> pz;
      for (int i = 0; i < 16; ++i) pz[size_t(i)] = P(cx + z[size_t(2 * i)] * 1.5f * q, cy + z[size_t(2 * i + 1)] * 1.5f * q * 0.94f);
      for (int i = 0; i + 2 < 16; ++i) tri(pz[size_t(i)], naranja, pz[size_t(i + 1)], naranja, pz[size_t(i + 2)], naranja);
    } else if (tb.ok) {
      const std::vector<float>& s = tb.t[g_iris_tipo];
      const float re = s[0] * 1.5f * q, ca = std::cos(g_iris_ang), sa = std::sin(g_iris_ang);
      for (int n = 0; n < g_iris_tipo; ++n) {
        const float X = s[size_t(1 + 2 * n)] * 1.5f, Y = s[size_t(2 + 2 * n)] * 1.5f;
        const float ex = cx + (X * ca - Y * sa) * q, ey = cy + (Y * ca + X * sa) * q * 0.94f;
        ImVec2 ant2;
        for (int i = 0; i <= 10; ++i) {
          const float an = float(i) * kPi * 2.0f / 10.0f + 4.537855f, rr = (i & 1) ? re * 0.381966f : re;
          const ImVec2 v = P(ex + std::cos(an) * rr, ey + std::sin(an) * rr * 0.94f);
          if (i > 0) tri(P(ex, ey), naranja, ant2, naranja, v, naranja);
          ant2 = v;
        }
      }
    }
    // el brillo
    const float d = (q + q * q) * 100.0f, rb = q * 60.0f, bx = cx - d, by = cy - d * 0.94f;
    const ImU32 bc = col(1, 1, 1, a2), bb = col(1, 1, 1, p * 0.5f);
    for (int i = 0; i < 32; ++i) {
      const float a0 = float(i) * kPi * 2.0f / 32.0f + 3.92699f, a1 = float(i + 1) * kPi * 2.0f / 32.0f + 3.92699f;
      tri(P(bx, by), bc, P(bx + std::cos(a0) * rb, by + std::sin(a0) * rb * 0.94f), bb,
          P(bx + std::cos(a1) * rb, by + std::sin(a1) * rb * 0.94f), bb);
    }
  }
  // negro fuera de la Bola: 64 cuadrilateros del borde a lo lejos (toda la pantalla si aun no ha crecido)
  const ImU32 negro = IM_COL32(0, 0, 0, 255);
  if (R < 1.0f) {
    dl->AddRectFilled(ImVec2(0, 0), W, negro);
    return;
  }
  const float lejos = 4000.0f;
  for (int i = 0; i < 64; ++i) {
    const float a0 = float(i) * kPi * 2.0f / 64.0f, a1 = float(i + 1) * kPi * 2.0f / 64.0f;
    const ImVec2 c0 = P(cx + std::cos(a0) * R, cy + std::sin(a0) * R * 0.94f), c1 = P(cx + std::cos(a1) * R, cy + std::sin(a1) * R * 0.94f);
    const ImVec2 f0 = P(cx + std::cos(a0) * lejos, cy + std::sin(a0) * lejos), f1 = P(cx + std::cos(a1) * lejos, cy + std::sin(a1) * lejos);
    tri(c0, negro, f0, negro, f1, negro);   // sin suavizado: AddQuadFilled dejaba rayas entre los trozos
    tri(c0, negro, f1, negro, c1, negro);
  }
}

int Dibujar(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, int out[14]) {
  static int sonando = -1;
  static float audio_anterior = -1;
  if (IrisActivo(t)) pressed = 0;   // S1-F01.2: mientras se abre el iris no se juega
  const int v_ini = dl->VtxBuffer.Size;
  g_v_ini = v_ini;
  const int r = DibujarTablero(dl, a, t, pressed, auto_on, out);
  if (r == 0) DibIris(dl, a, t);   // S1-F01.2
  // A2c: la pantalla de 640x448 del original sale en 4:3 (PS2 la estira): cada punto es 900/448 alto y 1200/640 ancho. El port dibuja
  // con escala uniforme; este estiramiento vertical (medido por correlacion de bordes original/port: sy 1,05-1,07) lo iguala y quita las
  // bandas de arriba y abajo.
  {
    const float cy = a.p0.y + 450.0f * a.s, k = (900.0f / 448.0f) / (1200.0f / 640.0f);
    for (int i = v_ini; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].pos.y = cy + (dl->VtxBuffer[i].pos.y - cy) * k;
  }
  if (r == 0 && g_estado == kJuego && !EscenaAbierta()) SonidosPasosB2(audio_anterior, t);
  audio_anterior = r == 0 ? t : -1;
  const bool en_video = dbz3::video::Activo();   // M1: su video va encima y deja sin musica al port mientras dura
  if (r != 0 || en_video) {
    CallarMusicaPs2();
    CallarSonidosPs2();
    sonando = -1;
    // M4 (mod opcional b2.musica_combate): al pedirse un combate de casilla (r == 2) suena su pista (T1+0x1F);
    // la de la fase vuelve sola al volver al tablero (sonando != pista de la fase).
    if (r == 2 && g_pend_r >= 0 && g_pend_r < int(g_f.size())) {
      const size_t casilla = size_t(g_f[size_t(g_pend_r)].casilla);
      const int pista = casilla < g_d.casillas.size() ? PistaCombateB2(g_d.casillas[casilla].musica) : -1;
      if (pista > 0 && !g_iso_b2.empty()) {
        sonando = pista;
        const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.8f;
        REXLOG_INFO("dbz3 tablero b2: musica del combate de la casilla {}: ADX_US {} (volumen {:.2f}, mod b2.musica_combate)",
                    g_f[size_t(g_pend_r)].casilla, pista, vol);
        // PRUEBA: DBZ3_TEST_MUSICA=1 la decodifica aunque el juego este en silencio (registro con su duracion)
        OirMusicaPs2(g_iso_b2, "ADX_US.AFS", pista, std::getenv("DBZ3_TEST_MUSICA") ? 0.001f : vol);
      }
    }
  } else if (const int menu = PistaMenuB2(); menu >= 0 || g_estado == kJuego || g_estado == kPrepara) {
    // M5 (mod opcional b2.musica_menus): con un menu abierto suena su indice (13/15/17); sin menu o sin mod, la
    // pista de la fase como siempre. El menu de los deseos (kDeseo) solo suena con el mod.
    const int pista = PistaB2(menu >= 0 ? menu : (g_estado == kJuego ? g_musica : g_d.musica0));
    if (g_d.fase >= 0 && pista > 0 && sonando != pista && !g_iso_b2.empty()) {
      sonando = pista;
      const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.8f;
      if (menu >= 0)
        REXLOG_INFO("dbz3 tablero b2: musica del menu {} (indice de la tabla de B2): ADX_US {} (volumen {:.2f}, mod b2.musica_menus)",
                    menu, pista, vol);
      else
        REXLOG_INFO("dbz3 tablero b2: musica de la fase {}: ADX_US {} (volumen {:.2f})", g_d.fase + 1, pista, vol);
      // PRUEBA: DBZ3_TEST_MUSICA=1 la decodifica aunque el juego este en silencio (registro con su duracion)
      OirMusicaPs2(g_iso_b2, "ADX_US.AFS", pista, std::getenv("DBZ3_TEST_MUSICA") ? 0.001f : vol);
    }
  }
  return r;
}

}  // namespace dbz3::pasarela::b2


