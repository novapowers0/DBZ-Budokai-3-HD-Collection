// dbz3 - Minijuegos del mapa de Dragon Mission de Infinite World (frente P4, 2026-10-16). Ver
// docs/14_frentes/PORT_EXACTO.md ("P4") y iw_minijuegos.h. Desensamblado: E:/DBZ3HD_trabajo/P4/iwdis.py (ELF USA).
//
// Del ELF de IW USA (SLUS_218.42):
//  - Los 18 "?" del #DMD de DATA_CMN 1093 (claves 0x7D0-0x7E1) llevan en +0x1C el indice 0..18 de 6 tablas de 19
//    clases (0x472B10...). MSNMW (campo a pie, codigo 0x316FF0-0x324000) = indices 0, 1, 2, 10, 14 y 16.
//  - MSNMW por indice: campo (0x296BC0: 0 -> DATA_CMN 903+904, 1 -> 897+898, 2 -> 884+885, 10 -> 900+901,
//    14 -> 906+907, 16 -> 909+910; #MCF + modelo y su #WCF de colision, todos propios de IW), paquete de objetos
//    (0x323A40: 887, 888, 889, 893, 894, 896) y limite (0x2984D0, minutos x 60 x fps en +0x7CC: 5, 4, 5, 5, 6, 5).
//  - Objetos (0x3224B0, por dificultad 0..5 del descriptor +0xC): A = {6,8,10,12,14,16} (indice 2: 10 - 2d),
//    B = {16,16,24,24,32,32}, C = {8,8,8,10,12,16}. Su sitio en el #ITM del paquete (0x31EF90, [ITM] POST EXEC):
//    meta +0x30 (punto 1), A +0x40 (puntos 2..), B +0x140 (18..), C +0x380 (54..), D +0x4A0 (72..), salida punto 0.
//    Se cogen al acercarse: A y la meta con distancia^2 < 600, B y C < 180 (unidades de IW).
//  - Total (0x3237D0) = A + B + C + D + 2. Rango (0x322640) por cogidos / total: >= 0,95 Z, >= 0,8 S, >= 0,65 A,
//    >= 0,5 B, si no C. Puntos (0x322E60): bonus del rango {200, 300, 500, 750, 1000} x (d + 1)^2 + segundos que
//    sobran x (d + 1) x 10, redondeado a la centena, + los puntos de los objetos especiales.
//
// Recreacion en el motor de B3 (modo libre): combate propio (hub::RequestBattleEx) sin tiempo en el escenario de B3
// mas parecido al campo; el otro luchador es el muneco quieto de combate.cpp, invisible (X+3261/X+4925, como el
// relevo de tagteam.cpp) e invulnerable, y hace de "pivote": se pone delante del jugador en la direccion de la
// palanca relativa a la camara, y la palanca del jugador se cambia por "hacia el rival" (Mando), asi el luchador anda
// hacia donde se empuja. La camara va detras del jugador (Fotograma, mgr+0x60 via cine::Camara). Objetos proyectados
// con esa camara y dibujados con sus texturas originales de la copia del jugador; HUD de IW (DATA_USA 6/0).
#include "reloj_prueba.h"
#include "iw_ame.h"
#include "iw_amc.h"
#include "iw_minijuegos.h"
#include "iw_mips.h"
#define IWC_SOLO_DECL
#include "iw_mips_campo.inl"   // tanda3: MSNMW con la logica original (solo declaraciones)
#undef IWC_SOLO_DECL
#include "iw_ap_eventos.h"

#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <random>
#include <vector>

#include "generated/dbz3_init.h"
#include "anim_iw.h"
#include "iw_azar.h"
#include "iw_bubbles_gp2.h"
#include "iw_bubbles_pose.h"
#include "iw_bubbles_audio.h"
#include "iw_camaras_gp2.h"
#include "iw_seguimiento.h"
#include "iw_estados.h"
#include "iw_lava.h"
#include "cine.h"
#include "combate.h"
#include "eu_map.h"
#include "escenarios.h"
#include "ingame/game_style.h"
#include "launcher/launcher_state.h"
#include "launcher/ui_kit.h"
#include "pasarela.h"
#include "pasarela_iw.h"
#include "ps2_modelo.h"
#include "suelo_iw.h"
#include "iw_recompensa.h"

namespace dbz3::hub {
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
}
namespace dbz3::net {
void InyectarPad(uint32_t usuario, int32_t botones);   // net/netplay_hooks.cpp (-1 = el mando de verdad)
}
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3
namespace dbz3::ingame {
int GameScreen();
}
// suelo_iw.cpp (D34): la malla que se DIBUJA del campo, para la camara y su medida (el #WCF da altura donde se ve una
// cara vertical). Se declaran aqui para no tocar suelo_iw.h (no estaba en la lista del encargo).
namespace dbz3::suelo_iw {
float AlturaMalla(float x, float z, float sin_suelo);
bool RayoMalla(const float desde[3], const float hasta[3], float corte[3]);
bool CercaMalla(const float p[3], float radio);
}  // namespace dbz3::suelo_iw

namespace dbz3::iwmini {
std::atomic<float> g_panel_campo{-1.0f};
std::atomic<float> g_panel_campo_anim{0.0f};
std::atomic<bool> g_serie_juego{false}, g_serie_por_clip{false};
std::atomic<int> g_serie_clip{-1};
void SerieJuego(bool activa) { g_serie_juego = activa; }
void SeriePorClip(bool activa) { g_serie_por_clip = activa; }
void SerieClip(int) { static std::atomic<int> n{0}; g_serie_clip = ++n; }

namespace {

namespace P = dbz3::pasarela;
namespace S = dbz3::ingame::style;

// Las funciones de este modo que hace el motor de B3, un cargador nuestro o son PS2 puro estan en
// MAPAS_CODIGO/SUSTITUIDAS_IW_MINIJUEGOS.txt (una linea por direccion, con el archivo:funcion que la cubre).
constexpr dbz3::GA kMgrPtr = 0x823D6CF4, kPads = 0x8238B780;
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kVista = 0x60, kFov = 0x1C4, kFin = 35569, kEstado = 35128;
std::atomic<float> g_fov_campo{FovVerticalCampoIW(40.0f * 0.01745329252f)};
constexpr uint32_t kVida = 6080, kVidaMax = 6084, kOcultoA = 3261, kOcultoB = 4925;
constexpr uint32_t kScreenX = 4960 + 48, kRivalScreenX = 4960 + 56;   // combate.cpp
constexpr uint32_t kCopias[3] = {48, 64, 80};   // la posicion esta tres veces (tagteam.cpp StepSlide)
constexpr uint16_t kArriba = 0x0010, kDerecha = 0x0020, kAbajo = 0x0040, kIzquierda = 0x0080;   // registro +108
constexpr int kMainScreen = 5;
constexpr int kGoku = 0, kCell = 33, kPiccolo = 11, kRaditz = 18;   // kCell: forma 0 = primera forma (imperfecto, como el original: captura PCSX2 ee/i14_2 y aviso del usuario 31/10)
constexpr int kLlanuras = 5, kCasaAbuelo = 6, kRocoso = 4, kNamek = 7;   // escenarios de B3 (escenarios.cpp)

// Ficha de cada indice (DMD +0x1C).
struct Ficha {
  int indice;
  const char* modulo;
  int pack, campo;        // DATA_CMN: objetos (#ITM) y campo de IW (modelo + #WCF; sin port a B3 todavia)
  float minutos;          // limite (0x2984D0)
  int jugador, pivote;    // IDs de B3: el que se lleva y el muneco invisible
  int escenario;          // escenario de B3 parecido al campo
  bool hecho;
  const char* frase;      // DATA_USA 334/6 de su fila (ingles del juego)
};
constexpr Ficha kFichas[] = {
    {0, "MSNMW", 887, 903, 5, kGoku, kRaditz, kCasaAbuelo, true, "Raditz kidnapped Gohan! Go after them!"},
    {1, "MSNMW", 888, 897, 4, kGoku, kRaditz, kLlanuras, true, "Capture Bubbles under heavy gravity! Watch out for bananas!"},
    {2, "MSNMW", 889, 884, 5, kGoku, kRaditz, kLlanuras, true, "Snake Way is very long, but don't give up!"},
    {3, "SHOOT", -1, -1, 0, -1, -1, -1, false, "Training with King Kai! Use Ki Blasts to destroy the bricks!"},
    {5, "MSN_NB006", -1, -1, 0, -1, -1, -1, false, "Go Gohan! Training with Piccolo will make you stronger!"},
    {6, "MSN_NB007", -1, -1, 0, -1, -1, -1, false, "Keep training until you reach Planet Namek!"},
    {7, "FLIGHT", -1, -1, 0, -1, -1, -1, false, "Go to Guru's House! Don't get caught by Vegeta!"},
    {8, "MSN009", -1, -1, 0, -1, -1, -1, false, "Attack Frieza with Destructo-Discs!"},
    {9, "MSN010", -1, -1, 0, -1, -1, -1, false, "Fire beams at Piccolo! Go easy on him, though!"},
    {10, "MSNMW", 893, 900, 5, kGoku, kRaditz, kNamek, true, "Find the spaceship! You have to get out of here, fast!"},
    {11, "MSN_NB012", -1, -1, 0, -1, -1, -1, false, "Why do you want to fight an android...?"},
    {12, "MSN_NB013", -1, -1, 0, -1, -1, -1, false, "This is the Time Chamber. You really love training, don't you?"},
    {13, "MSN_NB014", -1, -1, 0, -1, -1, -1, false, "I know you like training, but don't overdo it."},
    // DS56: el indice 14 lo juega Cell semiperfecto (PINE 0x5C0DC0+0x10 = 33 en el 14; SONNET1 §4 y DS52 §3), no
    // Gohan nino. Su forma de B3 es la 1 y sus animaciones son las de DATA_CMN 914 (anim::kCell*, kVariantes 0x62C-0x62E).
    {14, "MSNMW", 894, 906, 6, kCell, kPiccolo, kLlanuras, true, "Gotta run from Piccolo! He'll catch up eventually, though..."},
    // EU 0x30CCF0 s_MSN016_UPDATE / 0x30CC40 Minigame_SetupScene (minijuego 15, MSN016): su preparacion la pone el
    // script del juego: UiAnim_Start(0,1,0,1,0) (clip 0), +0x14cd = 1 y +0x359d = 1 en el trabajo, CameraCtrl_SetFlagWord
    // (0), Timer_SetSlot(2.0, trabajo, 0) y GameState_SetFlag2(0); la escena carga camara y modelos (CameraCtrl_LoadModel
    // + s_SHLCR_MAIN + CameraCtrl_SetFlagWord(1)), engancha camara->jugador y pone la pose 10 al jugador y la 0 al rival
    // desde auRam005d92f0 / auRam005d9320. Este indice va por el modulo de disparo (iw_mini_disparo.cpp, clave 0x7DE,
    // Cuadro15): el camino propio de P4 no se usa (hecho = false). El slot 0 del temporizador es 2 CUADROS (lo descuenta
    // Timer_TickSlot EU 0x2EF990) y los dos flags son los de ocultar a los luchadores de B3: `coincide` ahi.
    {15, "MSN016", -1, -1, 0, -1, -1, -1, false, "Tien! Stop Cell! It's your duty!"},
    {16, "MSNMW", 896, 909, 5, kGoku, kRaditz, kRocoso, true, "Use my Dragon Radar to find the Dragon Ball!"},
    {17, "FLIGHT", -1, -1, 0, -1, -1, -1, false, "Race with Goten? Well, do your best!"},
    {18, "MSN019", -1, -1, 0, -1, -1, -1, false, "Flight training for the Tournament? Well, do your best!"},
};
// 31/10 Battle_TriggerProgressCutins (EU 0x2A63B0) y s_PLY_CUTIN (0x2A53C0): avisos con voz y recuadro de los campos a pie.
// disparo: A mitad del tiempo (restante <= limite/2), B ultimo minuto (restante <= 60 s), E 500 cuadros tras empezar
// (solo Bubbles, +0x1E0), C mitad de los aros, D todos los aros. perfil = MinigameAudioTriggerFilter (0x0B Piccolo -> 9,
// 0x4D Bulma -> 0x11, 0x6C Kaio -> 0x13); linea = linea ADX; texto = textura de DATA_USA 5/11 (= el indice del sprite de
// UI_SprPlaceByIndex, medido: 6 = "Hurry! There's no time!"); retrato = DATA_CMN 936/1 (0 Piccolo, 3 Bulma, 10 Kaio).
struct Cutin {
  int indice;
  char disparo;
  int perfil, linea, texto, retrato;
};
constexpr Cutin kCutins[] = {
    {0, 'B', 9, 1, 6, 0},  {0, 'A', 9, 0, 5, 0},  {0, 'C', 9, 2, 7, 0},  {0, 'D', 9, 3, 8, 0},
    {1, 'B', 19, 2, 24, 10}, {1, 'A', 19, 1, 23, 10}, {1, 'E', 19, 0, 22, 10},
    {2, 'B', 19, 4, 19, 10}, {2, 'A', 19, 3, 18, 10}, {2, 'C', 19, 5, 20, 10}, {2, 'D', 19, 6, 21, 10},
    {10, 'B', 19, 0x17, 28, 10}, {10, 'A', 19, 0x16, 27, 10}, {10, 'C', 19, 0x18, 29, 10}, {10, 'D', 19, 0x19, 30, 10},
    {14, 'C', 9, 0x39, 14, 0}, {14, 'D', 9, 0x3A, 15, 0},
    {16, 'B', 17, 7, 32, 3}, {16, 'A', 17, 6, 31, 3},
    {16, 'C', 17, 8, 33, 3}, {16, 'D', 17, 9, 34, 3},
};
// solPUL: EU2A63B0 texto21/22, Bulma8/9. Falta pareja original/port:
// apagado en juego normal; el arnes puede comprobar el consumo real.
constexpr bool kAvisosCD16 = false;
bool AvisosCD16() {
  return kAvisosCD16 || (std::getenv("DBZ3_TEST_IWMINI") && std::getenv("DBZ3_TEST_PUL_CUTINS"));
}
constexpr int kNumCutins = int(sizeof(kCutins) / sizeof(kCutins[0]));
int IndiceDeClave(int clave) {   // el 4 no tiene casilla
  constexpr int k[18] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
  return clave >= 0x7D0 && clave <= 0x7E1 ? k[clave - 0x7D0] : -1;
}
const Ficha* FichaDe(int indice) {
  for (const Ficha& f : kFichas)
    if (f.indice == indice) return &f;
  return nullptr;
}

// Reglas del MSNMW (ver arriba). Dificultad: la de la ficha del mapa (0 Very Easy .. 4 Very Hard; 5 Z).
std::atomic<int> g_dificultad{2};   // 0..5 (Very Easy .. Z) del MSNMW; Dificultad() la pone desde la ficha del mapa
std::atomic<int> g_personaje_clave{-1}, g_personaje{-1};
constexpr int kCuantosA[6] = {6, 8, 10, 12, 14, 16}, kCuantosB[6] = {16, 16, 24, 24, 32, 32},
              kCuantosC[6] = {8, 8, 8, 10, 12, 16};
constexpr int kBonusRango[5] = {200, 300, 500, 750, 1000};
constexpr float kCogerA = 24.5f, kCogerB = 13.4f;   // sqrt(600), sqrt(180) en unidades de IW (= las de B3)
// Grupo D (GroundMinigameObjectsAroundTheMap EU 0x3227A0 y recogida en 0x320...): el radio va al cuadrado y vale 140
// (11,83 u) en general y 600 (24,5 u) en el Radar (0x10), como A.
constexpr float kCogerD = 11.83f;
// Escala del curso de IW al escenario de B3. Los modelos de IW miden lo mismo que en B3 (mismo motor: el aro mide 24
// de radio, la nave 13) y se dibujan a su tamano, pero el campo de IW mide ~3000 y en un escenario de B3 se corre hasta
// ~255 del centro (medido): sin campo, las posiciones se juntan (curso centrado, punto mas lejano a kRadio). Con el
// campo de IW instalado (R8: la alta 5060+n de tools/campos_iw.py) el curso va en coordenadas 1:1 (ver LeerCurso).
constexpr float kRadio = 230.0f, kCogerMin = 2.5f;
// DS172: altura de la aureola sobre el HUESO de la cabeza. En el modelo de Goku de IW (DATA_CMN 409/0) GOK_RING es
// hijo de GOK_HEAD con desplazamiento local (0, 4,189, 0) (amo2awo/ringpos: eje del hueso 23 = [0, 4.189, 0]); el
// hueso de la cabeza cae a 5,442 del origen del modelo y el pelo acaba en 8,14..9,74, asi que el anillo va justo
// encima del pelo. La escala es la misma en B3: el GOK_HEAD del Goku HD (us/data_cmn.afs 264) tambien esta a 5,442
// y sus pies a -10,446 (los dos modelos miden 18,6-20,2), por eso no se reescala.
// DS175: se mide desde el punto de cuerpo 3 (que va con GOK_HEAD) y por la VERTICAL DEL MUNDO. La malla del anillo se
// centra con y minima = 0 (ps2_modelo Centrar), asi que estos 4,189 son la CARA DE ABAJO del aro: su plano queda a
// cabeza+4,30 y la punta del pelo de B3 a cabeza+4,30 (9,74 - 5,442). Si en alguna pose el aro queda metido entre los
// pinchos del pelo, DBZ3_IWMINI_HALO_ALTO=4,6 lo sube 0,4 u sin recompilar.
// DS183: la vertical del mundo solo vale con la cabeza erguida. El punto 3 es la ESFERA DE LA CARA, no la coronilla:
// con Goku inclinado (carrera/guardia) su vertical cae delante del pecho y el aro sale a la altura de los brazos
// cruzados. El original (GOK_RING, hijo de GOK_HEAD con (0,4,189,0) en 409/0) cuelga del HUESO: se usa su POSICION y
// la fila 1 de su matriz = +Y LOCAL (GOK_HEAD del Goku HD tiene el +Y del bind en (0,1,0), DS174). Ver HuesoMundoEje.
constexpr float kHaloSobreCabeza = 4.189f;
// Modo libre: pivote delante del jugador y camara detras (unidades de B3; un luchador mide ~12).
// R16: la camara de los minijuegos de campo de IW (Camera_InitFieldDefault EU 0x297140 + Camera_UpdateFollow 0x297DE0):
// ojo 70 detras y 15 arriba del punto seguido, mira a ese punto + 5 de alto; el punto seguido se acerca al jugador
// 0.12 por cuadro (0.3 en la variante 2, Minigame_UpdateFollowCamera 0x299A70); el ojo no se suaviza.
// R17 (medido en el original con PCSX2, *(0x49CBBC)): el punto seguido de IW es el pecho, 9 sobre la raiz del jugador
// (ojo = raiz + 24, objetivo = raiz + 14; distancia 74, se repite en 0x7D0/0x7D1/0x7DD/0x7DF). Antes el port seguia la
// raiz y la camara iba 9 mas baja que en IW.
// EU 0x297140 Camera_InitFieldDefault
constexpr float kPivote = 40.0f, kCamAtras = 70.0f, kCamAlto = 24.0f, kCamMiraAlto = 14.0f;
// R17: giro inicial de la camara de campo en IW (azimut del ojo alrededor del jugador medido en el original, en grados;
// el delante de la camara es ese + 180). Sin mando la camara no gira, asi que es el giro de todo el minijuego. Se midio
// con PCSX2 PINE: 0x7D1 42,97; 0x7DD 150,61; 0x7DF -39,73. -1000 = sin medida (se usa salida -> meta).
constexpr float kGrados = 3.14159265f / 180.0f, kSinYawIW = -1000.0f;
// solIW8: LoadWaitMinigameIntro fija el vector (-0.4,0,0.92) del indice0.
// PINE campo_040: ojo(135.9108,55.9631,-496.1949), objetivo(108,45.9631,-432).
// solIWZ PINE7D2 READY: ojo-objetivo=(0,10,70); EU29BB70 fija (0,0,-1).
constexpr float kYawOjoIW[19] = {156.50143f, 42.97f, 0.0f, kSinYawIW, kSinYawIW, kSinYawIW, kSinYawIW, kSinYawIW,
                                 kSinYawIW, kSinYawIW, -97.2f, kSinYawIW, kSinYawIW, kSinYawIW, 150.61f, kSinYawIW,   // 31/10 indice 10: palanca arriba en el original va a 82,8 grados
                                 -39.73f, kSinYawIW, kSinYawIW};

// HUD de los minijuegos: DATA_USA 6/0 (#AMT; igual en 7/0). TIME y cifras lila/azul (t03), aro plateado con cola
// (t00) con la cara (t29 Goku, t32 Krilin, t33 Vegeta, t35 Piccolo) y su barra (marco t15, relleno amarillo t01);
// cifras cian "01/08" (t12) y radar (t4-t11, t14) para el del Radar. Medidas: E:/DBZ3HD_trabajo/P4/hud.
constexpr int kHudAro = 0, kHudRelleno = 1, kHudTime = 3, kHudBarra = 15;
// Contador de puertas/aros del MSNMW (compuerta hexagonal + cifras cian): DATA_USA 6/0 t27 y t12. El original lo
// pinta bajo TIME con Minigame_UpdateScoreDigits (decomp EU 0x30ECD0): +0x166e aros cogidos / (+0x166f + 1), donde
// +0x166f es el numero de aros A de la dificultad (GroundMinigameObjectsAroundTheMap 0x3227A0) y el +1 es la meta.
constexpr int kHudPuerta = 27;
// Radar del Dragon (DATA_USA 6/0): marco con asa t04 (208x232), cuadricula t05 y pantalla t06 (152x160), triangulo del
// jugador t07, punto naranja t08 (la bola), flecha cian t09, rombo rosa t14 (las marcas); cifras cian t12 ("01/08").
constexpr int kRadarMarco = 4, kRadarRejilla = 5, kRadarPantalla = 10, kRadarYo = 7, kRadarBola = 8, kRadarFlecha = 9,
              kRadarCifras = 12, kRadarMarca = 14;
struct Glifo {
  float x, y, w, h;
};
constexpr Glifo kCifra[10] = {{3, 0, 20, 23},  {27, 0, 16, 23},  {51, 0, 20, 23},  {75, 0, 20, 23},  {100, 0, 20, 23},
                              {4, 24, 19, 23}, {27, 24, 20, 23}, {52, 24, 19, 22}, {76, 24, 20, 24}, {100, 24, 20, 24}};
constexpr Glifo kTime = {0, 48, 72, 24}, kMinuto = {81, 50, 12, 14}, kSegundo = {80, 67, 15, 12};
constexpr Glifo kCian[10] = {{2, 0, 19, 23},  {28, 0, 15, 22},  {51, 0, 19, 23},  {75, 0, 19, 23},  {99, 0, 19, 22},
                             {2, 24, 19, 23}, {25, 24, 20, 23}, {52, 24, 18, 23}, {75, 24, 19, 23}, {99, 24, 19, 23}};
constexpr Glifo kBarra = {41, 48, 16, 24};

// Modelos 3D de PS2: src/ps2_modelo.{h,cpp} (compartido con R2 y R11).
using ps2::AnimIW;
using ps2::Camara;
using ps2::Esqueleto;
using ps2::Giro;
using ps2::M34;
using ps2::Malla;
using ps2::Modelo;
using ps2::Tex;
using ps2::V3;
using ps2::CargarModelo;
using ps2::CargarSuelto;
using ps2::Crear;
using ps2::Posar;

// Camaras de IW de los minijuegos de campo: clips del #AMC general DATA_CMN 915 hijo 2 (Camera_PlayCameraAnim_ EU
// 0x2972D0; el mismo lector de iw_mini_botones.cpp). Formato de docs/03_formatos/CAMARA_ACC.md:
//  - cabecera #AMC +0x10 = nº clips, +0x14 = tabla de 16 B por clip; clip +8 = cuadros, +12 = offset.
//  - 4 pistas: 0 ojo, 1 objetivo, 2 giro, 3 fov. Claves de 16 B {cuadro, x, y, z} (0/1) o 8 B {cuadro, valor}.
// Clips usados por la maquina de estados de tierra (decomp EU):
//   intro  0x296  MgPly_Enter_State0A 0x3185A0 (junto con la animacion 0x11D)             [absoluto]
//   exito  0x259 (indice 0) / 0x291 (1) / 0x2C3 (2) / 0x41B (10) / 0x4E3 (14) / 0x556 (16)
//   fallo  0x25A (indice 0) / 0x28C (1) / 0x2BE (2) / 0x41C (10) / 0x4E4 (14) / 0x548 (16)
//   (Minigame_State30_EnterResultAnim_ 0x29AA90 y Battle_CharState2eInit 0x29B3B0; las ramas por +0xB8C son la pose
//    de resultado; aqui se usa la rama 0xD para el exito, que es la que el original pone en State2F para el tipo 1, y
//    la rama 0 para el fallo. Las otras ramas quedan [H] hasta mapear +0xB8C al rango del port.)
// dia 1A: el lector y el muestreo del #AMC son los de iw_amc.h (FUN_00127280), compartidos con las demas familias.
using ClipCamIa = dbz3::iwamc::Clip;
uint32_t LeerAmcMinijuego(const std::vector<uint8_t>& amc, const std::vector<int>& ids,
                          std::map<int, ClipCamIa>& out) {
  for (int id : ids) {
    ClipCamIa c;
    if (!out.count(id) && dbz3::iwamc::Leer(amc, id, c)) out[id] = std::move(c);
  }
  return dbz3::iwamc::NumClips(amc);
}

struct Objeto {
  V3 p;
  int grupo = 0;   // 0 A (aro RING_A01), 1 B (moneda), 2 C (bebida), 3 D (marca del radar, ITEMA20)
  float radio = 1;
  int valor = 0;   // bebida: energia que da (#ITM +0x480 + 2k, s16; 0x3237D0 la suma a la del jugador +0x185C)
  int k = 0;       // indice dentro de su grupo en el #ITM (con los sin sitio contados): banderas de G en la logica original
};
struct Datos {
  int indice = -1, clave = 0, fila = 0, total = 0, num_a = 0;   // num_a: aros con sitio (abren la meta)
  int dificultad = 2;                                           // 0..5 (descriptor +0xC del MSNMW)
  float escala = 1;                                             // posiciones de IW -> B3 (curso juntado)
  bool campo = false;   // el campo de IW instalado (R8): posiciones y velocidades de IW tal cual
  bool halo_nativo = false;   // traje4 Goku B3, cotejado en7D1/7D2; evita duplicar el anillo
  float k = 0.0825f;    // por unidad de +0xB4 de IW: 0.4 en el campo; si no, 0.33 / 4 (el andar de IW = el de B3)
  float limite = 300;
  float altura_camara = 9;   // respaldo Goku; descriptor EU +0xC para el indice 14
  V3 salida, meta;
  // Meta de fin (s_SPACE 0x31F280): +0x1630. Se fija abajo segun el indice (ver LeerCurso).
  V3 llegada;
  float radio_llegada = 13.4f;
  // 0x7D9: la nave de Freezer es una pieza estatica del campo 900 en world-space (900/7, horneada en sus huesos); su
  // centro xz y su base y NO son el +0x30 del #ITM (ese es el disparador de la meta). Se dibuja ahi desde el principio.
  V3 meta_modelo;
  bool meta_fija = false;
  float radio_meta = 2;
  std::vector<Objeto> objetos;
  std::vector<uint8_t> itm_crudo;   // #ITM del campo tal cual (la logica original de s_SPACE lo lee en el invitado)
  P::Imagen aro, moneda, bebida;   // texturas del paquete (#AMT hijos 0, 4 y 8)
  Modelo m_aro, m_moneda, m_bebida, m_meta;   // modelos 3D de IW (paquete y campo); m_meta = la nave de Raditz
  // 0x7D9: geiseres de lava del campo 900 (DATA_CMN 900/14 modelo + 900/20 texturas + 900/26 #AMM; el #MIF 900/33
  // trae seis LAVAFIRE_TOWER1..6 con su sitio en world-space). Ver Cargar y Dibujar.
  Modelo m_geiser;
  Modelo m_roca[3];   // 30/10: rocas que caen (900/3, una variante por tamano; EU 0x2A9BF0)
  AnimIW anim_geiser;
  std::vector<float> uv_geiser_dv;
  std::vector<V3> geiseres;
  // polvo del sprint (auditoria7/O: Effect 0x34 -> tipo 0x1A del paquete de los minijuegos de campo, DATA_CMN 934):
  // el #AME 934/26 (un cono de 4 particulas de modelo 934/1 y un rayo) y el atlas 934/0
  dbz3::iwame::ame::Efecto polvo;
  Modelo m_polvo;
  std::vector<P::Imagen> atlas_polvo;
  // DS91: efectos de la tabla de nombres del ELF EU (0x493C48, entradas de 16 B). Los 16 primeros nombres
  // (Mtn Ame, Mtn Amo, Hang, Grow, SBound, Shot, Aura, W Break, W Hit0, W Hit1, Dash, D Start, Slide, Bound, Step,
  // Foot) son los #AME 3..18 del paquete 934 del campo (uno por hijo, en ese orden). Aqui solo se cargan los que
  // pide la maquina de estados de tierra; el polvo sigue siendo el 934/26 que ya usaba R10.
  enum { kFxAura = 0, kFxDash, kFxDashStart, kFxSBound, kFxHang, kFxGrow, kFxRayoGolpe, kFxCuenta };
  std::array<dbz3::iwame::ame::Efecto, kFxCuenta> fx;
  std::array<dbz3::iwame::ame::Efecto,4> pasos_ap; // paquete campo+2, hijos3/4/9/A
  std::vector<P::Imagen> atlas_pasos_ap;
  std::array<dbz3::iwame::ame::Efecto,2> impactos_ap; //1FC290: DATA_CMN813; subtipos0/1 -> hijos1/6
  std::vector<P::Imagen> atlas_impactos_ap;
  std::array<dbz3::iwame::ame::Efecto,3> kami_fx;  // Namek: paquete campo+2, hijos 18/19/20 (ids 0x12/0x13/0x14)
  std::vector<ps2::Malla> kami_modelos;           // #AMO hijo 1 (particulas tipo 4)
  dbz3::iwame::ame::AuraCuerpo aura_base;    // aura de cuerpo (paquete de efectos del personaje)
  std::vector<P::Imagen> atlas_aura;
  Modelo m_marca;                             // Radar: las marcas (grupo D)
  // Grupo D que no es del Radar (indice 10, 893): MgDraw_CharModels_Field120 (EU 0x321350) lo pinta con la pieza del
  // slot 3 del modulo (MgDraw_SetupSlots_C 0x322BC0: 0x12 = hijo 18, ITEMA05) y elige la malla por el valor +0x5A0
  // (<50 variante 0, 50-99 la 1, >=100 la 2).
  Modelo m_d;
  // Personaje que se mueve solo (NPC del MSNMW, decomp EU s_BBR_POST_EXEC 0x3172E0): 1 = huye (Bubbles, modelo de IW
  // 118/119), 2 = persigue (Piccolo, 573/574); y los platanos de Bubbles (888/9, grupo D del 888: no cuentan)
  int npc = 0;
  V3 npc_inicio;
  Modelo m_npc, m_platano;
  std::array<AnimIW,0xE3> bubbles_clips;
  std::shared_ptr<iwbubbles::poses::Recursos> bubbles_poses;
  std::array<iwbubbles::audio::Recurso,2> bubbles_audio; // USA/JPN del jugador
  bool bubbles_clips_ok = false;
  std::vector<uint8_t> bubbles_wcf; // campo897, colision RAW898/0; solo candidato Bubbles.
  std::vector<uint8_t> piccolo_wcf; // P1: campo906, colision RAW907/0 (rayo de visibilidad de la CPU de Piccolo, 0x7DD)
  dbz3::iwmips::persecucion::RaizDatos piccolo_raiz;   // iw_7dd_raiz: #BSK/#AMM de Piccolo (925) y comunes (915)
  std::vector<V3> platanos;
  P::Imagen clear;                 // DATA_USA 334/3 t69 "CLEAR"
  P::Imagen ready, start;          // DATA_USA 5/1 t08 "READY?" y t00 "START!" (R16; los de R14)
  anim::ama::Obra avisos;
  std::vector<P::Imagen> aviso_atlas;
  anim::ama::Obra cutin_ama;          // DATA_CMN 936/0: ventana de dialogo (la del vuelo): AMA 0 marco, 15 entrada, 16 salida
  std::vector<P::Imagen> cutin_retratos, cutin_textos;   // 936/1 y DATA_USA 5/11
  float cutin_marco[2] = {0, 0}, cutin_pos[2] = {195, 312};
  std::vector<int> cutin_voz;         // entrada ADX de cada fila de kCutins (-1 = sin voz)
  int voz_arranque = -1, voz_2e = -1, voz_2f = -1, voz_30 = -1;   // voces de pose del jugador (Char_PlayAttackVoice*): s_SPACE, 2E, 2F, 30
  int voz_intro = -1, voz_intro_cuadro = 0;   // eIW8: voz de la presentacion de 0x7DD (cuadro 243, linea 1) y 0x7DF (206, linea 0x42)
  std::vector<P::Imagen> hud;      // DATA_USA 6/0 (kHud*)
  int cara = kGoku;               // ID de B3 del que juega (su cara del HUD: PintarCara)
  // DS93: camaras de IW de entrada y resultado (ver ClipCamIa).
  std::map<int, ClipCamIa> camaras;
  int cam_intro = 0, cam_bien = 0, cam_mal = 0;
  // DS169: clip del estado 0x2F (pose de resultado). EU 0x29B120 Battle_CharState2fInit pone SIEMPRE 0x1E (no el de
  // la variante): camara baja detras del luchador, la del fotograma de resultado del original.
  int cam_resultado = 0x1E;
  // DS112: clip de ARRANQUE de s_SPACE (EU 0x29BFF0), uno por indice (rama +0xB8C = 0); va con la animacion de
  // `Variante::comienzo`. No confundir con cam_intro: ese (0x296) solo lo pone el indice 1 (MgPly_Enter_State0A).
  int cam_arranque = 0;
  bool cam_final_rel = false;     // el clip de resultado va en coordenadas del luchador (indice 0) o del campo
};

// EU 0x1DD4F0 Anim_StepUvAnim: lee el incremento constante del descriptor 5 de cada canal #MTA.
std::vector<float> LeerUvMta(const std::vector<uint8_t>& mta) {
  std::vector<float> dv(16, 0.0f);
  if (mta.size() < 0x0C || std::memcmp(mta.data(), "#MTA", 4) != 0) return dv;
  auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= mta.size()) std::memcpy(&v, &mta[o], 4); return v; };
  const size_t n = std::min<size_t>(u32(4), dv.size()), tabla = u32(8);
  for (size_t c = 0; c < n; ++c) {
    const size_t o = tabla + 0x80 * c;
    if (o + 0x64 > mta.size()) break;
    const int32_t nk = int32_t(u32(o + 0x50));
    const size_t ptr = u32(o + 0x58);
    float valor = 0;
    std::memcpy(&valor, &mta[o + 0x5C], 4);
    if (nk > 0 && ptr + 8 <= mta.size()) std::memcpy(&valor, &mta[ptr + 4], 4);
    dv[c] = valor;
  }
  return dv;
}

void AplicarDerivaUv(Malla& malla, const std::vector<float>& canales, float cuadros) {
  // EU 0x1DD4F0 Anim_StepUvAnim: sumar la deriva y envolver el triángulo completo, nunca vértices aislados.
  for (Malla::Tri& tr : malla.tris) {
    const size_t canal = size_t(10 + tr.parte);
    if (canal >= canales.size()) continue;
    const float delta = canales[canal] * cuadros;
    float menor = tr.uv[0].y + delta, mayor = menor;
    for (int c = 1; c < 3; ++c) {
      menor = std::min(menor, tr.uv[c].y + delta);
      mayor = std::max(mayor, tr.uv[c].y + delta);
    }
    const float ajuste = menor > 1.0f ? -1.0f : mayor < 0.0f ? 1.0f : 0.0f;
    for (int c = 0; c < 3; ++c) tr.uv[c].y += delta + ajuste;
  }
}

std::mutex g_mu;
std::shared_ptr<Datos> g_d;           // del minijuego en curso
// P1 (revision 7/11): numero de partida, como iw_mini_disparo.cpp (eANI2): make_shared puede devolver la direccion de la
// partida anterior y `hecho_` solo no basta (no se preparaba: Piccolo del 0x7DD seguia en el interprete).
std::atomic<uint32_t> g_partida{0};
std::atomic<int> g_estado{0};         // 0 nada, 1 cargando, 2 combate, 3 resultado listo
// Minijuegos de otros frentes (RegistrarModulo): clave 0x7D0 + i. Ceros en la inicializacion estatica (antes de que
// los modulos se registren en la dinamica). g_modulo: corre uno (los ganchos de aqui no hacen nada).
Modulo g_modulos[32] = {};
std::atomic<bool> g_modulo{false};
std::atomic<int> g_clave{0};          // clave del modulo en curso (la necesitan las series de los de vuelo)
Resultado g_res;
// Rellena los campos separados que las pantallas nuevas usan (score/zeni/saldo) sin cambiar `puntos`, que es el valor
// historico del mapa. Los modulos que ya calculan el premio de IW lo ponen ellos; los que no (vuelo) conservan el
// comportamiento anterior (zeni = puntos).
void CompletarResultado(Resultado& r) {
  if (r.score < 0) r.score = r.puntos;
  if (r.zeni < 0) r.zeni = r.puntos;
  if (r.saldo < 0) r.saldo = P::LeerProgreso("iw_zeni", 0) + r.zeni;
}
bool g_auto = false;                  // prueba: el jugador salta de objeto en objeto solo
bool g_meta_abierta = false;          // prueba ("meta"): la meta abierta desde el principio (para verla)
// Modo libre (hilo del juego): direccion de la marcha (palanca relativa a la camara) y orientacion de la camara.
// g_dir_* va normalizada (giro del jugador, camara, efectos); g_avance_* es la MISMA direccion con la palanca cruda,
// sin normalizar, que es lo que suma Minigame_MovePlayerByStick (EU 0x299EE0: derecha*(sx*v) + delante*(sy*v), sin
// normalizar: en diagonal la palanca entera da 1,41x y el port perdia ese 29 %).
std::atomic<float> g_dir_x{0}, g_dir_z{1}, g_yaw{0}, g_avance_x{0}, g_avance_z{1};
// Inclinacion de la camara de campo (grados). IW Camera_InitFieldDefault (EU 0x297140) pone camara+0xE4 = 15.0
// (0x41700000); Camera_UpdateStickOrbit (EU 0x297330) la mueve 2.0 por cuadro y la acota a [-30, 30 + camara+0x128]
// (0x128 = 70.0 de Camera_InitFieldDefault, o sea 100). En el original la orbita solo corre con camara+0x208 = 1
// (triangulo mantenido, EU 0x299520), y eso no pasa en el minijuego 1 ni en el 0xE; en el port va con la palanca
// derecha (DS181) y con el teclado (flechas = dbz3_keybind_rstick_*) no se exige triangulo.
std::atomic<float> g_pitch{15.0f};
std::atomic<bool> g_cuadrado{false}; // EU299520: carga, no golpe/guardia de B3
std::atomic<bool> g_radar{false};   // EU 0x299520: triangulo mantenido, flag +0x208
std::atomic<int> g_rx{0};   // palanca derecha (XInput, s16): camara (PalancaDerecha)
std::atomic<int> g_ry{0};
std::atomic<bool> g_anda{false}, g_colocado{false};
// IW Char_GetNextActionCode (EU 0x2A9440): |x| o |y| de la palanca > 0.9 corre (codigo 3, +0xB4 = 4.0); si no, anda
// (codigo 2, 1.8). El teclado manda la palanca entera y por eso corre; medido en el original (2026-10-25, 0x7D0): 1,63
// u/cuadro al correr, que es 4,0 x 0,4 y lo que usa el port.
std::atomic<bool> g_corre{true};
bool g_cae = false;   // cayendo de una cornisa o en un salto (hilo del juego, Fotograma)
std::atomic<bool> g_pide_salto{false};
std::atomic<int> g_polvo_n{0};   // arranques del sprint (polvo): Fotograma lo sube, el panel pinta uno por cada
std::atomic<float> g_polvo_x{0}, g_polvo_y{0}, g_polvo_z{0}, g_polvo_dx{0}, g_polvo_dz{1};
// DS91: efectos con nombre de la tabla del ELF (ver Datos::fx). g_fx_tipo = Datos::kFx*; el panel los instancia.
std::atomic<int> g_fx_n{0}, g_fx_tipo{0};
std::atomic<float> g_fx_x{0}, g_fx_y{0}, g_fx_z{0}, g_fx_dx{0}, g_fx_dz{1};
struct PasoAp { int hijo; V3 p; };
std::mutex g_pasos_ap_mu;
std::vector<PasoAp> g_pasos_ap;
struct ImpactoAp {int tipo;V3 p;std::array<float,4> q;const Datos* campo;};
std::mutex g_impactos_ap_mu;
std::vector<ImpactoAp> g_impactos_ap;
void LanzarFx(int tipo, float x, float y, float z, float dx, float dz) {
  g_fx_tipo = tipo, g_fx_x = x, g_fx_y = y, g_fx_z = z, g_fx_dx = dx, g_fx_dz = dz;
  ++g_fx_n;
}
// solIWT: EU 0x22EBB0 s_PLY_VIB_CTRL crea la tarea 0x22CF20: emite
// eventos 0x1000 del MANDO (0x22D094 -> 0x1BB7B0), no mueve la camara.
// La sacudida de proyeccion de DS114 (3 * 30 px con reloj de pared) no era
// parte de IW y separaba el campo/objetos del cuerpo que dibuja B3.
// D34: ojo de la camara del minijuego de campo (lo pone Fotograma; lo lee Dibujar para no pintar un geiser con la
// camara dentro, como IW con sus efectos).
std::atomic<float> g_ojo_x{0}, g_ojo_y{0}, g_ojo_z{0};
std::atomic<bool> g_ojo_ok{false};
std::atomic<bool> g_resbalando{false};   // pisó un platano de Bubbles: sin mando un momento
// Reaparicion al caerse del mundo (IW ExplorationMinigameBodyAnimationsMerge EU 0x2AA0A0: con y < -1000 o > 1000 el
// jugador vuelve a +0x1620, que el juego pone en el ultimo objeto recogido -s_SPACE 0x31F280: +0x1620 = su posicion al
// cogerlo- y al empezar en +0x840 = la salida; despues y += 5, fVar21). g_resp_* = +0x1620; g_fundido reproduce el
// TransitionFilter (0.7, 0x1000, 0x00B09000) que IW lanza al empezar a caer (Char_UpdateMovementPhysics 0x2A8E20;
// 30 cuadros aqui: sube y baja).
std::atomic<float> g_resp_x{0}, g_resp_z{0};
std::atomic<bool> g_resp_hay{false};
std::atomic<int> g_fundido{0};
// R16: entrada y final propios de IW (decomp EU). Entrada: MgPly_Enter_State0A (0x3185A0) pone 0x11D al jugador y,
// despues, s_SPACE (0x29BFF0) la de su variante (= indice del minijuego, campo +4 de LoadWaitMinigamesFlag); el cartel
// del objetivo (LoadWaitGroundMinigameObjective 0x299900, parametro 0: 140 cuadros) para el reloj (+0x8D21). Final:
// estados 0x2F (superado, MgResult_ComputeScore(+1)) y 0x30 (no superado, (-1); anim 0x53/0x11A/... por variante).
std::atomic<bool> g_sin_mando{false};   // entrada o final en curso: ni andar ni scatto
std::atomic<int> g_bubbles_intro{0}; // 0:s_SPACE, 1:11D, 2:READY/juego; publicado por el hilo guest.
std::atomic<bool> g_reloj_parado{false};   // entrada o cartel del objetivo: ni reloj ni NPC
std::atomic<int> g_cartel{0};           // cuadros del cartel del objetivo que quedan
// 30/10: vida de Namek (+0x1850): la bajan rocas y lava, y a 0 el minijuego se pierde (Minigame_TriggerObjective EU 0x2A6260).
std::atomic<float> g_vida{100};
std::atomic<int> g_golpe{0};    // la roca que pega: lo pide el panel y lo aplica Fotograma
std::atomic<int> g_gracia{0};   // +0x1AC: cuadros sin poder recibir otro golpe
std::atomic<int> g_muerte{0};   // 31/10: PINE: vida 0: 0x2A (0x46) y 0x2B (0x47), 2,56 s, y solo entonces 0x30
std::atomic<bool> g_sin_hud{false};     // 30/10: sobrevuelo y presentacion (estados 0x31 y 0x2C): IW no pinta el HUD
std::atomic<bool> g_salta_sobre{false}; // 30/10: START en el sobrevuelo
std::atomic<bool> g_sin_aura{false};    // 31/10: en 0x31 el original pone +0xCCD = 1 (PINE): sin aura
constexpr int kClipSobre = 0x10000;     // clave en Datos::camaras de los clips del sobrevuelo (AMC del campo + 1)
std::atomic<bool> g_libre{false};       // andar libre de IW en curso (AnularAndar: sin el andar de combate de B3)

// DS100: una linea "iwmini: anim" por CAMBIO de estado de animacion (reposo/andar/correr/freno/salto/dash/...).
// Se llama justo al pedir la animacion, con el nombre del estado y el codigo alias que va al luchador (X+2928).
const char* g_anim_estado = "";
uint32_t g_anim_codigo = ~0u;
void AnimoLog(const char* estado, uint32_t codigo) {
  if (std::strcmp(g_anim_estado, estado) == 0 && g_anim_codigo == codigo) return;
  g_anim_estado = estado;
  g_anim_codigo = codigo;
  REXLOG_INFO("dbz3 iwmini: anim {} codigo {:03X}", estado, codigo);
}
// DS114: nombre del estado de la maquina de tierra de IW (tabla de estados 0x497230 del ELF EU: 0x00..0x31 ->
// las funciones Minigame_StateXX_* / Char_EnterStateX de iw_sles55347.c). Con esto la linea "iwmini: accion" dice
// en que estado esta el jugador, no solo el numero de animacion.
const char* NombreEstadoIW(int e) {
  static const char* kEstados[0x32] = {
      "ready/start",  // 0x00 GroundMinigames_MapExploration"Ready?Start!"ScreenDrawFunc 0x2A4BC0
      "reposo",       // 0x01 Char_EnterState1 0x2A4820 (anim 1)
      "andar+aura",   // 0x02 Char_EnterState2Aura 0x2A4580 (anim 0x15, +0xB4 = 1.8)
      "correr+aura",  // 0x03 Char_EnterState3Aura 0x2A42A0 (anim 0x16, +0xB4 = 4.0)
      "frenada",      // 0x04 Char_EnterState4 0x2A3DE0 (anim 0x18)
      "carga+aura",   // 0x05 Char_EnterState5Aura 0x2A3B10 (anim 2)
      "transformar",  // 0x06 Char_EnterState6Transform 0x2A37D0 (anim 4)
      "agotado",      // 0x07 Char_EnterState7Transform 0x2A34D0 (anim 5; ES el descanso)
      "recuperacion", // 0x08 Minigame_State08_Enter 0x2A3300 (anim 7)
      "descanso+aura",// 0x09 Minigame_State09_AuraEnter_ 0x2A30B0 (anim 5 + AuraBurstGlow)
      "entrada",      // 0x0A Minigame_State0A_Enter 0x2A2ED0 (anim 0x44)
      "aura",         // 0x0B Minigame_State0B_AuraEnter_ 0x2A2D40 (anim 0x604)
      "salto-prep",   // 0x0C Minigame_State0C_JumpPrepEnter 0x2A2BF0 (anim 0x1F)
      "salto-sube",   // 0x0D Minigame_State0D_JumpRiseEnter 0x2A29A0 (anim 0x20/0x103)
      "salto-aire",   // 0x0E Minigame_State0E_AirEnter 0x2A2700 (anim 0x21)
      "salto-cae",    // 0x0F Minigame_State0F_FallEnter 0x2A2180 (anim 0x22/0x105)
      "aterrizar",    // 0x10 Minigame_State10_LandEnter 0x2A1CA0 (anim 0x23/0x106)
      "vuelo-ini",    // 0x11 Minigame_State11_FlyStartEnter 0x2A19C0
      "despegue",     // 0x12 Minigame_State12_TakeOffEnter 0x2A1760
      "salto-doble",  // 0x13 Minigame_State13_JumpEnter 0x2A1420 (anim 0x29)
      "dash-ini",     // 0x14 Minigame_State14_DashStartEnter 0x2A0FC0 (anim 0x19/0x1C)
      "dash",         // 0x15 Minigame_State15_DashEnter 0x2A0CD0 (anim 0x1A/0x1D)
      "dash-fin",     // 0x16 Minigame_State16_Enter 0x2A0940 (anim 0x1B/0x1E)
      "ataque-aura0", // 0x17 Minigame_State17_Enter 0x2A0260 (anim 0x2A)
      "ataque-aura1", // 0x18 Minigame_State18_Enter 0x29FFD0 (anim 0x2B)
      "dash-aereo0",  // 0x19 Minigame_State19_Enter 0x29FC60 (anim 0x3D/0x3E)
      "golpe-aereo",  // 0x1A Minigame_State1A_Enter 0x29F600 (anim 8)
      "golpe-1B",     // 0x1B Minigame_State1B_Enter 0x29F3D0 (anim 0x0A; el decomp la escribe 10 decimal)
      "golpe-1C",     // 0x1C Minigame_State1C_Enter 0x29F1C0 (anim 200)
      "golpe-1D",     // 0x1D Minigame_State1D_Enter 0x29EDD0 (anim 0xC9)
      "proyectil",    // 0x1E Minigame_State1E_Enter 0x29E9A0 (anim 0xCA)
      "pared-salto",  // 0x1F Minigame_State1F_Enter 0x29E5D0 (anim 0x3F, wall kick)
      "pared-rebote", // 0x20 Minigame_State20_Enter 0x29E260 (anim 0x40 suelo / 0x41 aire)
      "golpe-aereo2", // 0x21 Minigame_State21_Enter 0x29DD80 (anim 0x622)
      "golpe-aereo3", // 0x22 Minigame_State22_Enter 0x29DA00 (anim 0x628 + aura)
      "caida-23",     // 0x23 Battle_CharState23Init 0x29D780
      "combate-24",   // 0x24 Battle_CharState24Init 0x29D5B0
      "combate-25",   // 0x25 Battle_CharState25Init 0x29D2F0
      "combate-26",   // 0x26 Battle_CharState26Init 0x29D0F0
      "combate-27",   // 0x27 Battle_CharState27Init 0x29CC90
      "combate-28",   // 0x28 Battle_CharState28Init 0x29CA70
      "combate-29",   // 0x29 Battle_CharState29Init 0x29C840
      "combate-2A",   // 0x2A Battle_CharState2aInit 0x29C6A0
      "combate-2B",   // 0x2B Battle_CharState2bInit 0x29C530
      "espacio",      // 0x2C s_SPACE 0x29BFF0
      "estado-2D",    // 0x2D Battle_CharState2dInit 0x29BA70
      "estado-2E",    // 0x2E Battle_CharState2eInit 0x29B3B0
      "resultado-bien", // 0x2F Battle_CharState2fInit 0x29B120
      "resultado-mal",  // 0x30 Minigame_State30_EnterResultAnim 0x29AA90
      "estado-31",    // 0x31 Minigame_State31_Enter? 0x29A7C0
  };
  return e >= 0 && e < 0x32 ? kEstados[e] : "?";
}
// Lo consume tambien el selector del ancla de camara. Las reacciones tienen
// subestados por codigo; no basta con conservar el nombre del primer golpe.
int EstadoIWDeAnim(const char* nombre) {
  return dbz3::iwestado::DeAnim(nombre,g_anim_codigo);
}
std::atomic<int> g_pad_prueba{0};     // PadPrueba
std::atomic<int> g_final{0};            // 0 jugando, 1 superado, 2 no superado (lo pide el panel), 3 combate acabado
std::atomic<bool> g_panel_confirmar{false}; // flanco real antes de que Mando tape A al anfitrion
struct Variante {
  int indice;
  uint32_t comienzo, bien, mal;   // s_SPACE; estado 0x2F; estado 0x30
};
// cada variante trae tres seguidas en el #BSK del jugador (922/914: 0x51 180 cuadros, 0x52 240, 0x53 240...): comienzo,
// superado y no superado (antes "superado" usaba la de no superado: Goku se hundia al ganar; auditoria5/C y videos)
constexpr Variante kVariantes[] = {{0, 0x51, 0x52, 0x53},      {1, 0x118, 0x119, 0x11A}, {2, 0x17C, 0x17D, 0x17E},
                                   {10, 0x49C, 0x49D, 0x49E}, {14, 0x62C, 0x62D, 0x62E}, {16, 0x6F4, 0x6F5, 0x6F6}};
// DS56: la 14 (0x62C-0x62E) solo existe en el #BSK de DATA_CMN 914 (bloques 0x62C/0x62D/0x62E -> animaciones
// 53/54/55), el de Cell semiperfecto (huesos CEL_*). Ahora que la ficha 14 es Cell (no Gohan) se usa tambien.
const Variante* VarianteDe(int indice,int personaje) {
  struct Alternativa { int personaje; Variante v; };
  static constexpr Alternativa alternativas[]={
    {11,{1,0xA40,0xA41,0xA42}},{12,{1,0xA46,0xA47,0xA48}},{13,{1,0xA4C,0xA4D,0xA4E}},
    {11,{2,0xA43,0xA44,0xA45}},{12,{2,0xA49,0xA4A,0xA4B}},{13,{2,0xA4F,0xA50,0xA51}},
    {3,{16,0xA52,0xA53,0xA54}},{10,{16,0xA55,0xA56,0xA57}},
    {8,{16,0xA58,0xA59,0xA5A}},{7,{16,0xA5B,0xA5C,0xA5D}}};
  for(const auto& a:alternativas) if(a.personaje==personaje&&a.v.indice==indice) return &a.v;
  for (const Variante& v : kVariantes)
    if (v.indice == indice) return &v;
  return nullptr;
}
// 30/10: la maquina de tierra de IW, codigos por minijuego (Minigame_StateXX_Enter y sus Update, EU 0x2A1420-0x2A4BC0):
// estado 0 ready, 1 reposo -> reposo2, 2 andar, 3 correr -> correr2, 4 freno, 0x0C prep, 0x0D sube, 0x0E aire, 0x0F cae,
// 0x10 aterrizar, 0x13 doble, 0x11 vuelo, 0x12 despegue (0x11/0x12 = saltar en marcha). t_*: cuadros de esos clips, de los
// que depende la vertical: +0xB0 baja 5/t en 0x0D (EU 0x2A27C0) y 7,5/t en 0x12 (EU 0x2A1580).
struct Loco {
  uint32_t ready, reposo, reposo2, andar, correr, correr2, freno, prep, sube, aire, cae, aterrizar, doble, vuelo, despegue;
  int t_sube, t_despegue;
};
const Loco& LocoDe(int indice) {
  using namespace dbz3::anim;
  static const Loco comun{0x8F1, kReposo, kReposo2, kAndar, kCorrer, kCorrer2, kParar, kSaltoPrep, kSaltoSube, kSaltoAire,
                          kSaltoCae, kAterrizar, kSubir, kSaltoVuelo, kDespegueVuelo, 11, 16};
  static const Loco kaio{kKaioReady, kKaioReposo, kKaioReposo2, kKaioAndar, kKaioCorrer, kKaioCorrer2, kKaioParar, kKaioPrep,
                         kKaioSube, kKaioAire, kKaioCae, kKaioAterrizar, kSubir, kKaioVuelo, kKaioDespegue, 11, 16};
  static const Loco namek{0x8F1, kNamekReposo, kNamekReposo2, kAndar, kCorrer, kCorrer2, kParar, kSaltoPrep, kSaltoSube, kSaltoAire,
                          kSaltoCae, kAterrizar, kSubir, kSaltoVuelo, kDespegueVuelo, 11, 16};
  static const Loco cell{kCellQuieto, kCellReposo, kCellReposo2, kCellAndar, kCellCorrer, kCellCorrer2, kCellParar, kCellPrep,
                         kCellSube, kCellAire, kCellSalto, kCellAterrizar, kCellDespegue, kCellDespegueVuelo, kCellDespegue2, 11, 19};
  return indice == 14 ? cell : indice == 1 ? kaio : indice == 10 ? namek : comun;
}
constexpr int kCartelCuadros = 140;
// Huir de Piccolo (14): el rival de B3 ES Piccolo (no el pivote oculto). Su sitio lo pone la IA del panel (MoverNpc),
// corre con anim::Corredor (R10) y lleva el mando quieto del muneco; el jugador mira hacia su marcha, no hacia el rival.
std::atomic<float> g_npc_x{0}, g_npc_y{0}, g_npc_z{0};
// Hacia donde mira el luchador (u16, 1/65536 de vuelta), medido con DBZ3_SONDA_GIRO (pivote girando alrededor):
// X+6062 = angulo - 15759 y X+6074 = angulo - 15968, con angulo = atan2(dx, dz) hacia el rival; lo rehace cada cuadro.
constexpr uint32_t kMiradaA = 6062, kMiradaB = 6074;
// Scatto del jugador (IW Minigame_State14/15/16, decomp EU 0x2A0FC0 / 0x2A0CD0 / 0x2A0940): circulo (B del mando)
// con energia (+0x185C, 0..100) > 0; 0x14 arranque quieto, 0x15 carrera a 35, 0x16 frenada, 7 cansado (sin mando).
std::atomic<float> g_energia{100};
std::atomic<int> g_fase{0}, g_bebida{0}, g_curacion{0};
std::atomic<bool> g_pide_scatto{false};
constexpr uint16_t kBotonScatto = 0x2000;   // registro del mando +12/+14 (disposicion de PS2: circulo)
constexpr uint16_t kBotonSalto = 0x4000;    // la cruz: salto (IW Minigame_ReadPlayerButtons EU 0x299520)
constexpr uint16_t kBotonStart = 0x0008;    // START en el mismo registro (PS2: select 1, L3 2, R3 4, start 8)
// Velocidades de IW: +0xB4 x Datos::k. En el campo de IW, k = 0.4 (Minigame_MovePlayerByStick con +0xB54 = 0); en el
// escenario de B3 (curso juntado), el andar de IW (4.0) = el andar de B3 (0.33 por cuadro, medido): k = 0.33 / 4, y asi
// se guardan las proporciones andar : IA (4.5) : scatto (35).
constexpr float kAndarB3 = 0.33f;
// Salto de IW medido en el original (PCSX2 PINE 2026-10-25, 0x7D0 sobre Campo IW 903): 41,1 u de alto y ~0,72 s en
// el aire (subida ~18 cuadros, bajada ~26). En unidades del campo (k = 0,4); fuera del campo se escala con k/0,4,
// igual que la marcha, para que la proporcion del salto no cambie al juntar el curso en un escenario de B3.
// 30/10: leyes de los estados, en vez de una parabola ajustada a esa medida. 0x0D: +0xB0 = 5 y baja 5/cuadros (EU 0x2A27C0);
// 0x12: 5 y baja 7,5/cuadros (EU 0x2A1580); 0x0E: de 0 con +0,15 (EU 0x2A2260); 0x0F: +1 (EU 0x2A1DB0).
constexpr float kJumpV = 5.0f, kGravAire = 0.15f, kGravCae = 1.0f;
// DS180: doble salto de IW, tal cual lo escriben sus estados (no es el salto simple escalado):
// EU 0x2A1420 Minigame_State13_JumpEnter pone +0xB0 = 0x40E00000 (7,0) y EU 0x2A10A0 Minigame_State13_JumpMove
// le resta 0,5 por cuadro (al bajar, State13 sigue con la misma ley: +0,5 por cuadro; no pasa a la gravedad de
// 0x0E). Apex a los 14 cuadros y +49 u de subida. Se escala con la misma `esc_salto` que el salto simple (1,0
// con el campo de IW instalado, que es el caso de 0x7D2).
constexpr float kDobleV = 7.0f, kDobleGrav = 0.5f;

int64_t NowMs() { return dbz3::RelojMs(); }
uint8_t* Base() {
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->virtual_membase() : nullptr;
}
float LoadF(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void StoreF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}
void Poner(uint8_t* base, uint32_t x, const V3& p) {
  for (uint32_t o : kCopias) {
    StoreF(base, x + o, p.x);
    StoreF(base, x + o + 4, p.y);
    StoreF(base, x + o + 8, p.z);
  }
}
V3 Leer(uint8_t* base, uint32_t x) { return {LoadF(base, x + 64), LoadF(base, x + 68), LoadF(base, x + 72)}; }
// B54 sustituye0.4; k/0.4 solo convierte el curso al escenario anfitrion.
// No aplicar ese factor a gravedad, texturas, vibracion ni dimensiones del mundo.
float VelocidadCampoAp(uint32_t x,float velocidad,float k) {
  return dbz3::anim::VelocidadAP(x,velocidad)*(k/.4f);
}
float DistH(const V3& a, const V3& b) {
  float x=a.x,z=a.z;
  dbz3::suelo_iw::EnvolverReferencia(x,z,b.x,b.z);
  return std::hypot(x-b.x,z-b.z);
}
// DS172: punto de cuerpo del luchador en coordenadas de MUNDO (X+1616 + 16*i: 0 cintura, 1 pecho, 2 cuello,
// 3 cabeza...). El juego los saca del esqueleto animado CADA cuadro con la tabla de anclajes X+2956 (sub_820F9548,
// ver iw_mini_disparo.cpp HuesoMundo y gigantes.cpp): es donde se DIBUJA el muñeco, no su X+48. false si aun no hay
// modelo/tabla o el punto es nulo.
bool PuntoCuerpo(uint8_t* base, uint32_t x, int i, V3& p) {
  if (!REX_LOAD_U32(x + 2996) && !REX_LOAD_U32(x + 2952)) return false;
  const uint32_t a = x + 1616 + 16u * uint32_t(i);
  p = {LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)};
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z) > 1e-3f;
}
// DS175: proyeccion a pantalla con la MISMA camara que dibuja la escena 3D (Escena::Poner, ps2::Camara::P). `dentro`
// = dentro de la imagen que se esta dibujando (cam.c +- cam.h, la imagen del juego); `ok` = delante del plano cercano
// y finita. Se usa solo en el registro de prueba.
struct Proy {
  float x = 0, y = 0, k = 0;   // px, px y px por unidad (k) a la distancia del punto
  bool ok = false, dentro = false;
};
Proy Proyectar(const Camara& cam, const V3& p) {
  Proy r;
  ImVec2 o;
  float k = 0;
  if (!cam.P(p, o, k)) return r;
  r.x = o.x, r.y = o.y, r.k = k, r.ok = true;
  r.dentro = std::fabs(o.x - cam.c.x) <= cam.h.x && std::fabs(o.y - cam.c.y) <= cam.h.y;
  return r;
}
// DS175/DS183: hueso de la tabla de la logica ([X+2952]+1032, 64 B; 3x4 de fila: filas 0..2 = ejes, fila 3 =
// POSICION en +48/+52/+56; espacio del MODELO) pasado a mundo con la matriz del luchador X+0 (filas 0..2, traslacion
// X+48). La convencion NO es la que suponia DS175: el codigo recompilado del guest (sub_8208A8E8) transforma un punto
// local con  out = x*fila0 + y*fila1 + z*fila2 + fila3  (out.x = x*m[0] + y*m[16] + z*m[32] + m[48]). DS175 hacia el
// producto por columnas (dot(fila_i, v)); con el hueso rotado (cabeza inclinada) eso lo mandaba 3-4 u fuera, que es el
// `ancla3 d` grande de su registro. La POSICION del hueso no se transforma con su propia matriz: es la fila 3 tal cual
// (sub_820F9548 copia +48/+52/+56 y le suma el desplazamiento comun, sub_8208A818). `eje_y` = fila 1 = +Y LOCAL del
// hueso (GOK_HEAD en el bind de B3 lo tiene en (0,1,0), DS174) en mundo.
bool HuesoMundoEje(uint8_t* base, uint32_t x, int idx, V3& p, V3& eje_y) {
  if (idx < 0 || idx > 255) return false;
  const uint32_t mi = REX_LOAD_U32(x + 2952);
  const uint32_t tb = mi >= 0x40000000u && mi < 0xA0000000u ? REX_LOAD_U32(mi + 1032) : 0;
  if (tb < 0x40000000u || tb >= 0xA0000000u) return false;
  const uint32_t h = tb + 64u * uint32_t(idx);
  if (h < 0x40000000u || h + 64u > 0xA0000000u) return false;
  auto f = [&](uint32_t a) { return LoadF(base, a); };
  // filas del modelo (X+0) y posicion del hueso (fila 3 de su matriz, espacio del MODELO)
  const V3 m0{f(x), f(x + 4), f(x + 8)}, m1{f(x + 16), f(x + 20), f(x + 24)},
      m2{f(x + 32), f(x + 36), f(x + 40)};
  const V3 t{f(h + 48), f(h + 52), f(h + 56)};
  // hueso -> mundo (fila 3 de la matriz del hueso, pasada por las filas del luchador)
  p = {t.x * m0.x + t.y * m1.x + t.z * m2.x + f(x + 48), t.x * m0.y + t.y * m1.y + t.z * m2.y + f(x + 52),
       t.x * m0.z + t.y * m1.z + t.z * m2.z + f(x + 56)};
  // +Y local del hueso (fila 1) en el espacio del modelo y en mundo
  const V3 yl{f(h + 16), f(h + 20), f(h + 24)};
  eje_y = {yl.x * m0.x + yl.y * m1.x + yl.z * m2.x, yl.x * m0.y + yl.y * m1.y + yl.z * m2.y,
           yl.x * m0.z + yl.y * m1.z + yl.z * m2.z};
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z) > 1e-3f;
}
bool HuesoMundo(uint8_t* base, uint32_t x, int idx, V3& p) {
  V3 eje{};
  return HuesoMundoEje(base, x, idx, p, eje);
}
// DS175: escala a la que se dibuja el luchador: longitud de la fila Y de su matriz de mundo (X+0). 1,00 de serie; el
// gigante de gigantes.cpp escala esas filas. El +4,189 del anillo son unidades de MODELO: se multiplican por esto.
float EscalaLuchador(uint8_t* base, uint32_t x) {
  const float a = LoadF(base, x + 16), b = LoadF(base, x + 20), c = LoadF(base, x + 24);
  const float l = std::sqrt(a * a + b * b + c * c);
  return l > 0.3f && l < 3.0f ? l : 1.0f;
}
// suelo_iw::Mover y, si el suelo del destino queda por encima de la cabeza (una loma empinada sin pared en el #WCF),
// no se avanza: IW lo trata como pared (Char_UpdateMovementPhysics: el rayo de suelo sale de la cabeza); sin esto el
// sprint metia al luchador por debajo de la colina y la camara veia el terreno desde abajo (captura 162252)
void MoverCampo(const float a0[3], const float a1[3], float s[3], float escala = 1.0f) {
  dbz3::suelo_iw::Mover(a0, a1, s, escala);
  const float arriba = dbz3::suelo_iw::Altura(s[0], s[2], -1e30f);
  if (arriba > -1e29f && arriba > a0[1] + 12.0f) std::copy(a0, a0 + 3, s);
}
std::shared_ptr<Datos> Actual() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_d;
}

// #ITM del paquete -> objetos en unidades de B3 (centrados y escalados a kRadio).
bool LeerCurso(const std::vector<uint8_t>& itm, Datos& d) {
  if (itm.size() < 0x20 + 99 * 16 || std::memcmp(itm.data(), "#ITM", 4) != 0) return false;
  auto pt = [&](int k) {
    V3 p;
    std::memcpy(&p, &itm[0x20 + size_t(k) * 16], 12);
    return p;
  };
  auto vacio = [](const V3& p) { return p.x == 0 && p.y == 0 && p.z == 0; };
  const int dif = d.dificultad;
  // EU 0x3227A0 GroundMinigameObjectsAroundTheMap
  int na = d.indice == 2 ? 10 - 2 * dif : d.indice == 16 ? 0 : kCuantosA[dif];
  int nb = kCuantosB[dif], nc = kCuantosC[dif];
  {   // fase 1 (7/11): EU 0x323840 recorta A/B/C a los contadores del #ITM (+0x04/+0x08/+0x0C): min(tabla, #ITM)
    auto i32 = [&](size_t o) { int32_t v = 0; std::memcpy(&v, &itm[o], 4); return v; };
    if (i32(4) < na) na = int(int8_t(i32(4)));
    if (i32(8) < nb) nb = int(int8_t(i32(8)));
    if (i32(12) < nc) nc = int(int8_t(i32(12)));
  }
  // D (G+0x16B9 = #ITM +0x10; GroundMinigameObjectsAroundTheMap EU 0x3227A0 = USA 0x323...): el contador es SIEMPRE
  // el +0x10 del #ITM y se suma al total (0x323A40) salvo en el 1 (Bubbles, sus platanos no cuentan). El Radar (0x10)
  // lo cambia por 2d + 6 (y deja A = 0). El 893 (indice 10, "Find the spaceship") trae 16 y SI cuentan.
  uint32_t mas10 = 0;
  std::memcpy(&mas10, &itm[0x10], 4);
  const int nd = d.indice == 1 ? 0 : d.indice == 16 ? std::min<int>(2 * dif + 6, int(mas10)) : int(mas10);
  struct Crudo {
    V3 p;
    int grupo, valor = 0;
  };
  auto s16 = [&](size_t o) {
    int16_t v = 0;
    std::memcpy(&v, &itm[o], 2);
    return int(v);
  };
  std::vector<Crudo> crudos;
  for (int k = 0; k < na; ++k) crudos.push_back({pt(2 + k), 0});    // +0x40
  // EU s_SPACE 0x31F280: cada moneda B añade el s16 de #ITM+0x340+2*k a +0x1658.
  for (int k = 0; k < nb; ++k) crudos.push_back({pt(18 + k), 1, s16(0x340 + 2 * size_t(k))});   // +0x140
  for (int k = 0; k < nc; ++k) crudos.push_back({pt(54 + k), 2, s16(0x480 + 2 * size_t(k))});   // +0x380
  // D (+0x4A0): en el 16 son las marcas del Radar (modelo ITEMA20); en el 10 (893) son 16 objetos de energia: su
  // valor (que ademas elige la variante del modelo) sale de +0x5A0 + 2k, s16, como el de las bebidas (+0x480).
  for (int k = 0; k < nd; ++k)
    crudos.push_back({pt(72 + k), d.indice == 16 ? 3 : 4, d.indice == 16 ? 0 : s16(0x5A0 + 2 * size_t(k))});
  // EU 0x323840 (414338-414358): +0x165A = A + B + C (+ D salvo el 1) + 1 (siempre) + 1 (el 16; o el objeto oculto
  // +0x16A4 si sigue pendiente). Medido por PINE en el original (7/11, tools/iw_mips/sonda_meta.py, dificultad 2, partida
  // sin bolas: mascara 0x5C1D78 = 0): 0x7D0 44 = 42 + 2, 0x7D2 40 = 38 + 2, 0x7D9 60 = 58 + 2 (oculto pendiente,
  // +0x16A4 = 1) y 0x7DF 44 = 42 + 2 (+0x16A4 = 2). El port no tiene el oculto (bola del #ITM 90 + slot): S + 2 en todos.
  d.total = na + nb + nc + nd + 2;
  const V3 salida = pt(0), meta = pt(1);
  float x0 = std::min(salida.x, meta.x), x1 = std::max(salida.x, meta.x);
  float z0 = std::min(salida.z, meta.z), z1 = std::max(salida.z, meta.z);
  for (const Crudo& c : crudos) {
    if (vacio(c.p)) continue;
    x0 = std::min(x0, c.p.x), x1 = std::max(x1, c.p.x), z0 = std::min(z0, c.p.z), z1 = std::max(z1, c.p.z);
  }
  // Con el campo de IW instalado (R8: colision x4.2 en coordenadas y unidades de IW) todo va en su sitio de IW; sin el,
  // juntado al escenario de B3 (radio ~255).
  const float cx = d.campo ? 0.0f : (x0 + x1) * 0.5f, cz = d.campo ? 0.0f : (z0 + z1) * 0.5f;
  const float s = d.campo ? 1.0f : kRadio / std::max({(x1 - x0) * 0.5f, (z1 - z0) * 0.5f, 1.0f});
  auto b3 = [&](const V3& p) {
    return d.campo ? p : V3{(p.x - cx) * s, std::clamp(p.y * s, 0.0f, 12.0f), (p.z - cz) * s};
  };
  d.salida = b3(salida);
  d.meta = b3(meta);
  d.meta_modelo = d.meta;
  // EU 0x323840 LoadWaitDragonMissionGroundMinigameItems: la UNICA escritura estatica de +0x1630 es
  // `*(+0x1630) = *(+0x840)` y +0x840 se acaba de copiar del #ITM punto 0 (la salida); no hay otra en
  // 0x29xxxx-0x34xxxx. s_SPACE 0x31F280 termina al tocarla (radio^2 < 180; +0x165c++ y +0x16bb++). Luego la
  // meta del P4 es VOLVER AL PUNTO DE SALIDA, no el punto 1 (+0x30): ese es el disparador/objetivo, y en el
  // indice 2 (Camino de la Serpiente) es el unico que esta activo (+0x16bc != 2). Para 0x7DF el punto 1 es la
  // bola del Radar (modelo de meta) y para 0x7D1 el sitio de Bubbles; se conservan como hasta ahora.
  // [H] el script MMI2 (ExecuteScript_MMI2) podria mover +0x1630; no se ve otra escritura en el decompilado.
  // Nave de Freezer (900/7): AABB real del #AMO, centro xz (-54.254, -1002.505) y base y 8.302 (hueso 1 en -150,0,-1050).
  // En el #ITM el +0x30 es el disparador (206,55,-832), a 311 u del casco: es el objetivo de mision, no la meta de fin.
  if (d.indice == 10) d.meta_modelo = b3({-54.254f, 8.302f, -1002.505f}), d.meta_fija = true;
  if (!d.campo) d.salida.y = d.meta.y = 0;
  // fase 1 (7/11): tambien el 2 acaba en +0x1630 = la salida (medido por PINE tras el script MMI2: +0x1630 = (0, 50, 3416)
  // = punto 0 + 15 en y; el punto 1 (20, 47, -3401) es el disparador +0x16BC, que no termina). Radio^2 180 en todos.
  d.llegada = (d.indice == 0 || d.indice == 2 || d.indice == 10 || d.indice == 14) ? d.salida : d.meta;
  d.radio_llegada = kCogerB;   // +0x1630: radio^2 < 180 -> 13,4
  d.radio_meta = kCogerB;   // meta de IW: radio^2 < 180 (s_SPACE 0x31F280); antes kCogerA (24,5) era el de los aros
  d.escala = s;
  int vacios = 0;
  d.itm_crudo = itm;
  int kg[5] = {0, 0, 0, 0, 0};   // indice dentro del grupo (los sin sitio cuentan: son las banderas de G)
  for (const Crudo& c : crudos) {
    const int kk = kg[c.grupo]++;
    if (vacio(c.p)) {
      ++vacios;
      continue;
    }
    const float radio = c.grupo == 0 ? kCogerA : c.grupo == 3 ? kCogerA : c.grupo == 4 ? kCogerD : kCogerB;
    d.objetos.push_back({b3(c.p), c.grupo, std::max(radio, kCogerMin), c.valor, kk});
    if (c.grupo == 0 || c.grupo == 3) ++d.num_a;   // los que abren la meta (aros; en el Radar, sus marcas)
  }
  if (std::getenv("DBZ3_IWMINI_ARO_CERCA")) {   // prueba: el primer aro a 45 delante de la salida (verlo de cerca)
    const float dx = d.meta.x - d.salida.x, dz = d.meta.z - d.salida.z, l = std::max(std::hypot(dx, dz), 1e-3f);
    for (Objeto& o : d.objetos)
      if (o.grupo == 0) {
        o.p = {d.salida.x + dx / l * 45.0f + dz / l * 12.0f, d.salida.y + 8.0f, d.salida.z + dz / l * 45.0f - dx / l * 12.0f};
        break;
      }
  }
  if (d.indice == 1) {   // Bubbles sale del punto 1 (el unico del grupo 0 del 888); los platanos, del grupo D
    d.npc = 1;
    // el punto 1 del 888 es (100, 500, 100): 500 de alto (el 1 no tiene meta). En el suelo: y = 0 y despues la altura
    // del campo (MoverNpc)
    d.npc_inicio = {d.meta.x, 0, d.meta.z};
    if (std::getenv("DBZ3_IWMINI_NPC_QUIETO")) {   // prueba: quieto a 22 delante y a un lado (verle de cerca)
      const float dx = d.meta.x - d.salida.x, dz = d.meta.z - d.salida.z, l = std::max(std::hypot(dx, dz), 1e-3f);
      const float c = std::cos(0.8f), s = std::sin(0.8f);   // lejos de Goku en pantalla (sus esferas tapan)
      d.npc_inicio = {d.salida.x + (dx * c + dz * s) / l * 28.0f, 0, d.salida.z + (-dx * s + dz * c) / l * 28.0f};
    }
    // EU3227A0/31F280 usa ITM+10 entradas desde +4A0; +5A0 ya son valores s16.
    // Leer 18 posiciones incluia esos valores como dos platanos junto al origen.
    for (int k = 72; k < 72 + int(std::min(mas10, 16u)); ++k)
      if (!vacio(pt(k))) d.platanos.push_back(b3(pt(k)));
    REXLOG_INFO("dbz3 iwmini: platanos de la mision (grupo D): {}", d.platanos.size());
  } else if (d.indice == 14) {   // Piccolo persigue VOLANDO detras del jugador (medido en el original, PCSX2 PINE
    d.npc = 2;                   // 2026-10-25, 0x7DD/indice 14): al cargar esta en (-9, 124, -875) y sube/baja entre
                                 // 115 y 146 mientras se acerca; el jugador va a ras de suelo. Antes: 720 detras y a
                                 // y = 0 (andando por el suelo), que no es lo que hace IW.
    const float dx = d.meta.x - d.salida.x, dz = d.meta.z - d.salida.z, l = std::max(std::hypot(dx, dz), 1e-3f);
    const float atras = d.campo ? 875.0f : 450.0f * 4.0f * d.k;
    // Medido en el original: salida (0,0,0) y Piccolo (-9, 124, -875): casi -Z, que es hacia donde mira la camara
    // del port (detras del jugador, mirando a la meta -484,-243), asi que se ve de frente. No va "detras" (+Z).
    V3 n{d.campo ? d.salida.x - 9.0f : d.salida.x - dx / l * atras, d.campo ? 124.0f : d.salida.y,
        d.campo ? d.salida.z - 875.0f : d.salida.z - dz / l * atras};
    if (const float r = std::hypot(n.x, n.z); !d.campo && r > kRadio) n = {n.x * kRadio / r, 0, n.z * kRadio / r};
    d.npc_inicio = n;
  }
  REXLOG_INFO("dbz3 iwmini: curso (dificultad {}): A {} B {} C {} D {} (sin sitio {}), total {}, escala {:.4f}, "
              "salida ({:.0f}, {:.0f}), meta ({:.0f}, {:.0f})",
              dif, na, nb, nc, nd, vacios, d.total, s, d.salida.x, d.salida.z, d.meta.x, d.meta.z);
  return true;
}

// EU 0x28A320 s_MSN_WAIT_LD: el original es el estado de espera de carga de la mision (con +0x5c == 0: si el estado
// es 1, EnableBattleWait_; si +0x24 != 0, READ_BattleMain; y en los dos casos READ_SYSTEMFunctions -> +0x24). No hay
// logica que portar: el port carga de una vez y en el hilo de la interfaz (DATA_CMN y DATA_USA del jugador) y la
// pantalla de carga es la del propio B3 (RequestBattleEx); `motor`.
// P1 (revision 7/11): la misma tabla de descriptores de personaje que iwseguimiento::AlturaDescriptorEU en el ELF USA
// SLUS_218.42 (datos EU - 0x800, comprobado: tabla 0x44F610, Goku 0x44DE70, Cell 0x44E8F0, +0xC = 9 y 10). Con el ELF USA
// la lectura EU fallaba y el 0x7DD usaba la altura de Goku (9) en vez de la de Cell (10).
bool AlturaDescriptorUsa(const std::vector<uint8_t>& elf, int jugador, float& altura) {
  if (elf.size() < 52 || std::memcmp(elf.data(), "\x7f" "ELF\x01\x01", 6) || (jugador != 0 && jugador != 33)) return false;
  auto u = [&](size_t o) -> uint32_t {
    uint32_t v = 0;
    if (o + 4 <= elf.size()) std::memcpy(&v, elf.data() + o, 4);
    return v;
  };
  const size_t ph = u(28), paso = elf[42] | (unsigned(elf[43]) << 8), n = elf[44] | (unsigned(elf[45]) << 8);
  if (paso < 32 || n > 1024 || uint64_t(ph) + uint64_t(paso) * n > elf.size()) return false;
  auto leer = [&](uint32_t va, uint32_t& valor) {
    for (size_t i = 0; i < n; ++i) {
      const size_t h = ph + i * paso;
      if (u(h) != 1 || va < u(h + 8) || uint64_t(va) + 4 > uint64_t(u(h + 8)) + u(h + 16)) continue;
      const uint64_t o = uint64_t(u(h + 4)) + va - u(h + 8);
      if (o + 4 > elf.size()) return false;
      valor = u(size_t(o));
      return true;
    }
    return false;
  };
  constexpr uint32_t kTabla = 0x44F610;
  uint32_t goku = 0, cell = 0, desc = 0, bits = 0;
  if (!leer(kTabla, goku) || goku != 0x44DE70 || !leer(kTabla + 33 * 4, cell) || cell != 0x44E8F0 ||
      !leer(kTabla + uint32_t(jugador) * 4, desc) || !leer(desc + 12, bits)) return false;
  float v;
  std::memcpy(&v, &bits, 4);
  if (!std::isfinite(v) || v <= 0 || v > 64) return false;
  altura = v;
  return true;
}

bool Cargar(Datos& d, std::string& error) {
  const std::filesystem::path iw = P::BuscarIW();
  P::AfsPs2 cmn, usa;
  if (iw.empty() || !cmn.Abrir(iw, "DATA_CMN.AFS") || !usa.Abrir(iw, "DATA_USA.AFS")) {
    error = "sin la copia de Infinite World en ps2_games";
    return false;
  }
  const auto elf_campo=P::LeerElf(iw);
  iwazar::Global().Iniciar(elf_campo); // idempotente; no reinicia al entrar en otro campo
  const Ficha* f = FichaDe(d.indice);
  if(d.indice==14) {
    bool leida=dbz3::iwseguimiento::AlturaDescriptorEU(elf_campo,f->jugador,d.altura_camara);
    if(!leida) leida=AlturaDescriptorUsa(elf_campo,f->jugador,d.altura_camara);   // P1 (revision 7/11): ELF USA
    REXLOG_INFO("dbz3 iwmini: altura camara descriptor jugador={} altura={} leida={}",f->jugador,d.altura_camara,leida);
  }
  // EU 0x298760 LoadWaitMinigameLink
  d.limite = f->minutos * 60.0f;
  const std::vector<uint8_t> pack = cmn.Entrada(size_t(f->pack));
  std::vector<uint8_t> itm;
  for (uint32_t c = 0; c < 32 && itm.empty(); ++c) {
    std::vector<uint8_t> h = P::HijoAmb(pack, c);
    if (h.size() >= 4 && std::memcmp(h.data(), "#ITM", 4) == 0) itm = std::move(h);
  }
  d.campo = dbz3::escenarios::CampoIW(f->campo) >= 0;
  { // EU3028C0 -> cargas31F030/31B310: el paquete de efectos es campo+2,
    // NO934. Mantiene sus propios atlas y emisiones; no modifica el aura.
    const auto fx=cmn.Entrada(size_t(f->campo+2)),at=P::HijoAmb(fx,0);
    constexpr int hijos[]={3,4,9,10};
    for(size_t i=0;i<4;++i)d.pasos_ap[i]=dbz3::iwame::ame::Leer(P::HijoAmb(fx,hijos[i]),true);
    for(int i=0,n=P::CuantasAmt(at);i<n;++i)d.atlas_pasos_ap.push_back(P::TexturaAmt(at,i));
    if(d.indice==10) {
      for(size_t i=0;i<3;++i)d.kami_fx[i]=dbz3::iwame::ame::Leer(P::HijoAmb(fx,uint32_t(18+i)),true);
      const auto amo=P::HijoAmb(fx,1);
      uint32_t nm=0;
      if(amo.size()>=0x20)std::memcpy(&nm,amo.data()+0x18,4);
      d.kami_modelos.resize(std::min<uint32_t>(nm,64));
      for(size_t i=0;i<d.kami_modelos.size();++i)ps2::LeerAmo(amo,int(i),d.kami_modelos[i],nullptr,true);
    }
  }
  { // AP5:1FC290 carga813;1FB870/1FB720 seleccionan1/6 y atlas0.
    const auto hit=cmn.Entrada(813),atlas=P::HijoAmb(hit,0);
    d.impactos_ap[0]=dbz3::iwame::ame::Leer(P::HijoAmb(hit,1),true);
    d.impactos_ap[1]=dbz3::iwame::ame::Leer(P::HijoAmb(hit,6),true);
    for(int i=0,n=P::CuantasAmt(atlas);i<n;++i)d.atlas_impactos_ap.push_back(P::TexturaAmt(atlas,i));
  }
  if (d.campo) {
    // 31/10: EU 0x1D5610 Effect_OpenPlayerFile (tabla EU 0x449D80, personaje -> DATA_CMN) y 0x1D3010: paquete/1 trae
    // #AMT en 0 y los #AME de aura despues; EU 0x1D4680 elige los dos segun la fila de la forma.
    static constexpr int kPaquete[48] = {22, -1, 19, 18, 17, 24, 25, 45, 40, 39, 28, 31, 41, 47, 37, 42, -1, -1, 33, 29, 21, 34, -1, -1,
                                         -1, -1, -1, 14, 0, 1, 2, -1, 4, 10, 7, 8, 9, 13, 12, 5, 6, 3, 36, -1, 20, 43, 35, 27};
    const int pq_aura = d.cara >= 0 && d.cara < 48 ? kPaquete[d.cara] : -1;
    namespace ame = dbz3::iwame::ame;
    if (pq_aura >= 0) {
      const int forma = (d.indice == 10 || d.indice == 16) && d.cara == 0 ? 2 : 0;
      const auto ef = P::HijoAmb(cmn.Entrada(size_t(pq_aura)), 1);
      const int* f = ame::kAuraForma[forma];
      const auto a1 = ame::Leer(P::HijoAmb(ef, uint32_t(2 * (f[0] - 2) + 1)), true), a2 = ame::Leer(P::HijoAmb(ef, uint32_t(2 * (f[0] - 2) + 2)), true);
      const auto at_a = P::HijoAmb(ef, 0);
      if (!a1.raices.empty() && !a2.raices.empty()) {
        d.aura_base.n[0] = a1.raices[0];
        d.aura_base.n[1] = a2.raices[0];
        d.aura_base.pr = ame::kAuraParam[f[1]];
        d.aura_base.al[0] = ame::kAuraAlfa[f[2]];
        d.aura_base.al[1] = ame::kAuraAlfa[f[3]];
        for (int i = 0, n = P::CuantasAmt(at_a); i < n; ++i) d.atlas_aura.push_back(P::TexturaAmt(at_a, i));
      }
    }
  }
  d.k = d.campo ? 0.4f : 0.0825f;   // Minigame_MovePlayerByStick: +0xB4 x 0.4 (+0xB54 = 0)
  if (!LeerCurso(itm, d)) {
    error = "sin #ITM en DATA_CMN " + std::to_string(f->pack);
    return false;
  }
  // texturas del paquete: hijo 0 = RING_A01 (t01 la banda azul), 4 = ITEMA01_COIN (t00 oro), 8 = ITEMA06 (bebida)
  d.aro = P::TexturaAmt(P::HijoAmb(pack, 0), 1);
  d.moneda = P::TexturaAmt(P::HijoAmb(pack, 4), 0);
  d.bebida = P::TexturaAmt(P::HijoAmb(pack, 8), 0);
  // modelos 3D: paquete 887 = aro RING_A01 (hijos 1/0), moneda ITEMA01_COIN (5/4, variante 0 = oro), bebida ITEMA06
  // (9/8); la meta del 0 es la nave de Raditz del campo 903 (hijos 7/8: C2_SAS, PSAS01, SASBALL01, PODKAGE)
  if (f->pack == 887 || f->pack == 889 || f->pack == 893 || f->pack == 894 || f->pack == 896) {
    if (f->pack != 896) d.m_aro = CargarModelo(pack, 1, 0, 0);
    d.m_moneda = CargarModelo(pack, 5, 4, 0);
    d.m_bebida = CargarModelo(pack, 9, 8, 0);
  } else if (f->pack == 888) {   // 888 (Bubbles) no trae aro: moneda 888/1 (888/0) y bebida 888/5 (888/4)
    d.m_moneda = CargarModelo(pack, 1, 0, 0);
    d.m_bebida = CargarModelo(pack, 5, 4, 0);
  }
  if (d.indice == 0) d.m_meta = CargarModelo(cmn.Entrada(size_t(f->campo)), 7, 8, 0);
  // H03 P0 (0x7D9, Namek): la meta es la nave de Freezer, DATA_CMN 900 hijo 7 (#AMO) y 8 (#AMT). Si no se puede leer,
  // m_meta queda vacio y la meta sigue con el anillo 2D de DibujarMeta (fallback).
  if (d.indice == 10) {
    d.m_meta = CargarModelo(cmn.Entrada(900), 7, 8, 0);
    if (!d.m_meta.malla)
      REXLOG_WARN("dbz3 iwmini: 0x7D9 sin modelo de la nave de Freezer (DATA_CMN 900/7,8): anillo 2D");
    else
      REXLOG_INFO("dbz3 iwmini: 0x7D9 nave en ({:.1f}, {:.1f}, {:.1f}) y disparador en ({:.1f}, {:.1f}, {:.1f}) (a {:.0f} u)",
                  d.meta_modelo.x, d.meta_modelo.y, d.meta_modelo.z, d.meta.x, d.meta.y, d.meta.z,
                  std::hypot(d.meta_modelo.x - d.meta.x, d.meta_modelo.z - d.meta.z));
    // Geiseres de lava: el campo 900 trae el modelo 14 (#AMO), sus texturas 20 (#AMT), la animacion 26 (#AMM) y el
    // #MIF 33 con las seis torres (LAVAFIRE_TOWER1..6). Sitios: entrada de 0x30 B desde +0x10; +0x04/+0x08/+0x0C
    // son x, y, z en world-space del campo (1:1, como la nave). Si falta algo, no se pinta (queda el campo tal cual).
    const std::vector<uint8_t> campo900 = cmn.Entrada(900);
    d.m_geiser = CargarSuelto(P::HijoAmb(campo900, 14), P::HijoAmb(campo900, 20), true);
    if (d.m_geiser.esq) d.anim_geiser = ps2::LeerAnim(P::HijoAmb(campo900, 26), 0, d.m_geiser.esq->padre.size());
    d.uv_geiser_dv = LeerUvMta(P::HijoAmb(campo900, 32));
    for (int i = 0; i < 3; ++i) {   // [H] rocas: hijo 16 del paquete, variante 0 (la 1 y la 2 traen 4-6 triangulos); el original elige la variante por tamano
      d.m_roca[i] = CargarModelo(pack, 16, 15, 0);
      REXLOG_INFO("dbz3 iwmini: roca {}: paquete {}, {} tri", i, f->pack, d.m_roca[i].malla ? d.m_roca[i].malla.tris.size() : 0);
    }
    const std::vector<uint8_t> mif = P::HijoAmb(campo900, 33);
    if (mif.size() > 0x10 && std::memcmp(mif.data(), "#MIF", 4) == 0) {
      for (size_t o = 0x10; o + 0x10 <= mif.size(); o += 0x30) {
        float p[3];
        std::memcpy(p, &mif[o + 4], sizeof p);
        if (p[0] != 0 || p[1] != 0 || p[2] != 0) d.geiseres.push_back({p[0], p[1], p[2]});
      }
    }
    REXLOG_INFO("dbz3 iwmini: 0x7D9 geiseres: {} torres del #MIF 900/33, modelo {} tri, animacion {} cuadros",
                d.geiseres.size(), d.m_geiser.malla.tris.size(), d.anim_geiser.cuadros);
  }
  {
    const std::vector<uint8_t> pq = cmn.Entrada(934);
    d.polvo = dbz3::iwame::ame::Leer(P::HijoAmb(pq, 26));
    d.m_polvo = CargarModelo(pq, 1, 0, 0);
    const std::vector<uint8_t> at = P::HijoAmb(pq, 0);
    for (int j = 0, n = P::CuantasAmt(at); j < n; ++j) d.atlas_polvo.push_back(P::TexturaAmt(at, j));
    float ext = 0;
    for (const auto& tr : d.m_polvo.malla.tris)
      for (const V3& q : tr.p) ext = std::max({ext, std::fabs(q.x), std::fabs(q.y), std::fabs(q.z)});
    REXLOG_INFO("dbz3 iwmini: polvo del sprint (934/26): {} grupos, modelo {} tri (medio lado {:.1f}), atlas {}",
                d.polvo.grupos.size(), d.m_polvo.malla.tris.size(), ext, d.atlas_polvo.size());
    // DS91: nombres 0x493C48 -> hijos 3..18 del 934 (mismo orden). Aura=9, Dash=13, D Start=14, SBound=7, Hang=5,
    // Grow=6 (indice en el ELF 2/3/4/6/10/11 + 3). Son los efectos que pide la maquina de estados de tierra:
    // estado 0x14/0x15 (aura y arranque del dash), 0x1F (wall kick, efecto 0x0D), 0x20 (rebote, efecto 6) y el
    // StageEvt_SpawnHangEffect (EU1E84D0, tabla combate4582C0 builtin121).
    // Los SPX993/994 son de capsulas y su tabla484930 NO es la de combate.
    using F = Datos;
    d.fx[F::kFxAura] = dbz3::iwame::ame::Leer(P::HijoAmb(pq, 9));
    d.fx[F::kFxDash] = dbz3::iwame::ame::Leer(P::HijoAmb(pq, 13));
    d.fx[F::kFxDashStart] = dbz3::iwame::ame::Leer(P::HijoAmb(pq, 14));
    d.fx[F::kFxSBound] = dbz3::iwame::ame::Leer(P::HijoAmb(pq, 7));
    d.fx[F::kFxHang] = dbz3::iwame::ame::Leer(P::HijoAmb(pq, 5));
    d.fx[F::kFxGrow] = dbz3::iwame::ame::Leer(P::HijoAmb(pq, 6));
    d.fx[F::kFxRayoGolpe] = dbz3::iwame::ame::Leer(P::HijoAmb(pq, 29));   // EU 0x2F7020: tipo 0x1D
    REXLOG_INFO("dbz3 iwmini: efectos 934: aura {} dash {} dstart {} sbound {} hang {} grow {} grupos",
                d.fx[F::kFxAura].grupos.size(), d.fx[F::kFxDash].grupos.size(), d.fx[F::kFxDashStart].grupos.size(),
                d.fx[F::kFxSBound].grupos.size(), d.fx[F::kFxHang].grupos.size(), d.fx[F::kFxGrow].grupos.size());
  }
  if (d.npc == 1) {   // Bubbles (BBR_*, con cola: DATA_CMN 118 + texturas 119) y el platano BANANA_A02 (888/9 + 888/8)
    d.m_npc = CargarSuelto(cmn.Entrada(118), cmn.Entrada(119), true);
    // su carrera: el codigo 0xE2 del #BSK de Bubbles (DATA_CMN 912 hijo 0: lista +0x14, bloque [anim u16][almacen u16])
    // en su #AMM. El segundo u16 del bloque (3) es el TIPO de almacen (PS2: 3 = el propio; lo exige altura.code_anim),
    // no el hijo del #AMB: vale el #AMM que tenga mas animaciones que el indice `anim` del bloque. En 912 es el hijo 1
    // (18 animaciones de los 47 huesos de BBR_*); los hijos 2 y 3 solo traen 1 animacion de 53 huesos y no sirven.
    const std::vector<uint8_t> bb = cmn.Entrada(912), bsk = P::HijoAmb(bb, 0);
    uint32_t lst = 0, blk = 0;
    if (bsk.size() > 0x20 && std::memcmp(bsk.data(), "#BSK", 4) == 0) {
      std::memcpy(&lst, &bsk[0x14], 4);
      if (lst + 4 * 0xE3 <= bsk.size()) std::memcpy(&blk, &bsk[lst + 4 * 0xE2], 4);
    }
    if (d.m_npc.esq && blk && blk + 2 <= bsk.size()) {
      const uint32_t cod = uint32_t(bsk[blk] | bsk[blk + 1] << 8);
      for (uint32_t c = 1; c < 8 && !d.m_npc.anim; ++c) {
        const std::vector<uint8_t> amm = P::HijoAmb(bb, c);
        uint32_t n = 0;
        if (amm.size() < 0x14 || std::memcmp(amm.data(), "#AMM", 4) != 0) continue;
        std::memcpy(&n, &amm[0x10], 4);
        if (cod < n) d.m_npc.anim = ps2::LeerAnim(amm, cod, d.m_npc.esq->padre.size());
      }
    }
    REXLOG_INFO("dbz3 iwmini: Bubbles: carrera 0xE2 = {} cuadros", d.m_npc.anim.cuadros);
    if (iwbubbles::Prueba(d.clave) && d.m_npc.esq) {
      d.bubbles_wcf=P::HijoAmb(cmn.Entrada(898),0);
      // BSK912/0: codigo->bloque->indice; almacen3 es propio, NO hijo3.
      const auto amm = P::HijoAmb(bb,1);
      d.bubbles_clips_ok = lst != 0 && bsk.size() >= 0x20;
      for (unsigned codigo : iwbubbles::kCodigos) {
        uint32_t bloque = 0;
        if (size_t(lst)+4*size_t(codigo)+4 <= bsk.size())
          std::memcpy(&bloque,bsk.data()+lst+4*codigo,4);
        if (!bloque || size_t(bloque)+4>bsk.size()) { d.bubbles_clips_ok=false;continue; }
        const unsigned indice = unsigned(bsk[bloque]) | unsigned(bsk[bloque+1])<<8;
        const unsigned almacen = unsigned(bsk[bloque+2]) | unsigned(bsk[bloque+3])<<8;
        if (almacen != 3) { d.bubbles_clips_ok=false;continue; }
        d.bubbles_clips[codigo]=ps2::LeerAnim(amm,indice,d.m_npc.esq->padre.size());
        d.bubbles_clips_ok=d.bubbles_clips_ok && bool(d.bubbles_clips[codigo]);
        REXLOG_INFO("dbz3 gp2 bubbles clip codigo={:X} indice={} almacen={} cuadros={} huesos={}",
                    codigo,indice,almacen,d.bubbles_clips[codigo].cuadros,d.m_npc.esq->padre.size());
      }
      d.bubbles_clips_ok=d.bubbles_clips_ok && d.m_npc.esq->padre.size()==47 && d.m_npc.esq->padre[1]==0;
      if(d.bubbles_clips_ok)try{
        const auto facial=cmn.Entrada(120);
        auto recursos=std::make_shared<iwbubbles::poses::Recursos>();
        recursos->Iniciar(cmn.Entrada(118),amm,bsk,P::HijoAmb(facial,0),P::HijoAmb(facial,1),P::LeerElf(iw));
        for(unsigned codigo:iwbubbles::kCodigos){
          recursos->Muestrear(codigo,0);
          recursos->Muestrear(codigo,float(d.bubbles_clips[codigo].cuadros-1));
        }
        recursos->Muestrear(0x11D,0);
        for(unsigned i=0;i<12;++i)recursos->cara.Muestrear(i,float(recursos->cara.Cuadros(i)-1),recursos->lut);
        d.bubbles_poses=std::move(recursos);
        REXLOG_INFO("dbz3 gp2 bubbles LIVE47+facial8 recursos=118/912/120 LUT=ELF intro=11D normal=false");
      }catch(const std::exception& e){
        d.bubbles_clips_ok=false;REXLOG_WARN("dbz3 gp2 bubbles poses bloqueadas: {}",e.what());
      }
      // No confundir el SE1460 con voz ADX: EU318410 pide linea0 del perfil
      // filtrado16. Tabla y bancoAMK se leen del ELF/AFS de esta copia.
      const auto elf=P::LeerElf(iw);
      const auto voz=anim::apev::ResolverAdx(elf,912,0,0);
      const int amk=anim::apev::BancoBoca(elf,912);
      for(size_t idioma=0;idioma<d.bubbles_audio.size();++idioma){
        auto& a=d.bubbles_audio[idioma];P::AfsPs2 afs;
        const char* nombre=idioma?"ADX_JPN.AFS":"ADX_USA.AFS";
        if(afs.Abrir(iw,nombre)){
          if(amk>=0)a.boca.Cargar(afs.Entrada(size_t(amk)));
          if(voz.ruta==anim::apev::Ruta::Adx){
            a.entrada=voz.banco;a.pcm=iwadx::Leer(afs.Entrada(size_t(a.entrada)));
          }
          for(uint32_t l=1;l<a.lineas.size();++l){
            const auto r=anim::apev::ResolverAdx(elf,912,l,0);
            if(r.ruta==anim::apev::Ruta::Adx)a.lineas[l]=iwadx::Leer(afs.Entrada(size_t(r.banco)));
            REXLOG_INFO("dbz3 gp2 bubbles voz linea {} {} entrada={} muestras={}",l,nombre,r.banco,a.lineas[l].muestras.size()/2);
          }
        }
        REXLOG_INFO("dbz3 gp2 bubbles voz banco={} entrada={} hz={} muestras={} AMK={} clips={} normal=false",
          nombre,a.entrada,a.pcm.hz,a.pcm.muestras.size()/2,amk,a.boca.Cantidad());
      }
      REXLOG_INFO("dbz3 gp2 bubbles prueba banco={} wcf={} normal=false oraculo/runtime/fin9 pendientes",
                  d.bubbles_clips_ok,d.bubbles_wcf.size());
    }
    d.m_platano = CargarModelo(pack, 9, 8, 0);
  }   // Piccolo (14) es el luchador de B3 (el rival del combate), no un modelo pintado
  // P1: #WCF 907/0, solo con la CPU original del 0x7DD encendida (cvars dbz3_iw_7dd_original, de serie apagada (decision del usuario 7/11 21h), y
  // dbz3_iw_logica_original); apagada no se lee nada y Piccolo sigue con el port
  if (d.npc == 2 && d.campo && dbz3::iwmips::persecucion::g_tick && dbz3::iwmips::persecucion::g_activa &&
      dbz3::iwmips::persecucion::g_activa())
  {
    d.piccolo_wcf = P::HijoAmb(cmn.Entrada(size_t(f->campo) + 1), 0);
    const std::vector<uint8_t> c915 = cmn.Entrada(915), c925 = cmn.Entrada(925);
    auto& r = d.piccolo_raiz;
    r.bsk[0] = P::HijoAmb(c925, 0); r.bsk[1] = P::HijoAmb(c915, 3);
    r.amm[0] = P::HijoAmb(c915, 0); r.amm[1] = P::HijoAmb(c915, 1); r.amm[3] = P::HijoAmb(c925, 1); r.amm[4] = P::HijoAmb(c925, 2);
    REXLOG_INFO("dbz3 iwmini: 0x7DD raiz de Piccolo: bsk {}/{} amm {}/{}/{}/{} B", r.bsk[0].size(), r.bsk[1].size(), r.amm[0].size(),
                r.amm[1].size(), r.amm[3].size(), r.amm[4].size());
  }
  if (f->pack == 896) {   // Radar: marcas ITEMA20 (896/1, texturas 896/0) y la bola de dragon DB_BLV_BALL (896/16, 896/15)
    d.m_marca = CargarModelo(pack, 1, 0, 0);
    d.m_meta = CargarModelo(pack, 16, 15, 0);
  }
  if (f->pack == 893) {   // D del "Find the spaceship" (indice 10): ITEMA05 (893/18, texturas 17), 16 objetos de
                          // energia de +0x5A0 = 50 -> variante 1 (0.5 < v/100 < 1 -> malla 1; 0x321350)
    int16_t vd = 0;
    if (itm.size() >= 0x5A2) std::memcpy(&vd, &itm[0x5A0], 2);
    d.m_d = CargarModelo(pack, 18, 17, vd >= 100 ? 2 : vd > 49 ? 1 : 0);
    REXLOG_INFO("dbz3 iwmini: objetos D del 893: {} tri (valor {} -> variante {})", d.m_d.malla.tris.size(), vd,
                vd >= 100 ? 2 : vd > 49 ? 1 : 0);
  }
  float alto_npc = 0, ancho_npc = 0;
  for (const auto& t : d.m_npc.malla.tris)
    for (const V3& p : t.p) alto_npc = std::max(alto_npc, p.y), ancho_npc = std::max(ancho_npc, std::fabs(p.x));
  REXLOG_INFO("dbz3 iwmini: modelos de IW: aro {} tri, moneda {}, bebida {}, meta {}, npc {} (alto {:.1f}, ancho {:.1f}), "
              "platano {}",
              d.m_aro.malla.tris.size(), d.m_moneda.malla.tris.size(), d.m_bebida.malla.tris.size(),
              d.m_meta.malla.tris.size(), d.m_npc.malla.tris.size(), alto_npc, ancho_npc, d.m_platano.malla.tris.size());
  const std::vector<uint8_t> ui = P::HijoAmb(usa.Entrada(6), 0);
  // La seleccion debe estar resuelta ANTES de leer sus AMC y poses.
  d.hud.resize(size_t(std::max(P::CuantasAmt(ui), 0)));
  for (int j : {kHudAro, kHudRelleno, kHudTime, kHudBarra, kHudPuerta, kRadarMarco, kRadarRejilla, kRadarPantalla,
                kRadarYo, kRadarBola, kRadarFlecha, kRadarCifras, kRadarMarca,
                // DS68: HUD del 0x7D9 (16 vida, 17 energia, 18 marco, 19 retrato) y del 0x7DD (20 relleno, 21 cursor,
                // 22 SAFE, 23 DANGER, 24 marco, 25 escuadras, 26 marcas, 39 cabeza de Piccolo); 29 es la cara.
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 29, 39})
    if (j < int(d.hud.size())) d.hud[size_t(j)] = P::TexturaAmt(ui, j);
  d.clear = P::TexturaAmt(P::HijoAmb(usa.Entrada(334), 3), 69);
  const std::vector<uint8_t> t51 = P::HijoAmb(usa.Entrada(5), 1);
  d.ready = P::TexturaAmt(t51, 8);
  d.start = P::TexturaAmt(t51, 0);
  d.avisos.Cargar(P::HijoAmb(usa.Entrada(5),0));
  {   // 31/10: ventana, retratos, textos y voces de los avisos de progreso (kCutins)
    const std::vector<uint8_t> paq = cmn.Entrada(936), obra = P::HijoAmb(paq, 0);
    d.cutin_ama.Cargar(obra);
    auto f32 = [&](size_t o) { float v = 0; if (o + 4 <= obra.size()) std::memcpy(&v, obra.data() + o, 4); return v; };
    auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= obra.size()) std::memcpy(&v, obra.data() + o, 4); return v; };
    if (const int n = d.cutin_ama.Objeto(0); n >= 0) {
      if (const int seg = d.cutin_ama.Nodo(n).anim; seg >= 0) {
        int clave = -1;
        float pos[4] = {};
        anim::ama::MuestrearPosicion(d.cutin_ama.Seg(seg).a.c2, 0, clave, pos);
        d.cutin_marco[0] = pos[0], d.cutin_marco[1] = pos[1];
      }
    }
    if (const size_t tabla = u32(0x1C); tabla + 35 * 4 <= obra.size()) {   // centro del texto: secuencia 34 (como MgFly_State_ShowAndLoadNext)
      const size_t seg = u32(tabla + 34 * 4), pista = u32(seg + 4), claves = u32(pista + 0x1C);
      if (claves + 16 <= obra.size()) d.cutin_pos[0] = f32(claves), d.cutin_pos[1] = f32(claves + 4);
    }
    const std::vector<uint8_t> retr = P::HijoAmb(paq, 1), txt = P::HijoAmb(usa.Entrada(5), 11);
    for (int i = 0, n = P::CuantasAmt(retr); i < n; ++i) d.cutin_retratos.push_back(P::TexturaAmt(retr, i));
    for (int i = 0, n = P::CuantasAmt(txt); i < n; ++i) d.cutin_textos.push_back(P::TexturaAmt(txt, i));
    const std::vector<uint8_t> elf = P::LeerElf(iw);
    for (const Cutin& c : kCutins) d.cutin_voz.push_back(anim::apev::EntradaAdxPerfil(elf, c.perfil, c.linea));
    // EU 0x29BB70 LoadWaitMinigameIntro: PlayHit_KiBlastsAudios3(jugador, linea) con el reloj de la presentacion en 243 (tipo 0xE = 0x7DD,
    // linea 1) o 206 (tipo 0x10 = 0x7DF, linea 0x42); una sola vez. El perfil es el del cuerpo del jugador (PerfilAdx).
    if (d.indice == 14 || d.indice == 16) {
      const Ficha* fj = FichaDe(d.indice);
      const int perfil = fj ? anim::apev::PerfilAdx(anim::apev::CmnActor(uint32_t(fj->jugador))) : -1;
      d.voz_intro = anim::apev::EntradaAdxPerfil(elf, perfil, d.indice == 14 ? 1 : 0x42);
      d.voz_intro_cuadro = d.indice == 14 ? 243 : 206;
      if (const char* e = std::getenv("DBZ3_IW_VOZ_INTRO"); e && e[0] == '0') d.voz_intro = -1;   // interruptor: 0 = sin la voz
      REXLOG_INFO("dbz3 iwmini: voz de presentacion indice {}: perfil {} -> ADX {} en el cuadro {}", d.indice, perfil, d.voz_intro, d.voz_intro_cuadro);
    }
  }
  if (const Ficha* fj = FichaDe(d.indice); fj && fj->jugador >= 0) {
    // Voz de la pose del jugador al empezar (s_SPACE EU 0x29BFF0 -> Char_PlayAttackVoiceAndMouth3 0x2A80B0) y en el final:
    // 0x2E exito (Mode1 0x2A75E0, solo el tipo 1), 0x2F pose de resultado (0x2A7730) y 0x30 fallo (0x2A7120). Cada estado la
    // pide UNA vez (flag +0x1D4). Lineas del jugador (rama +0xB8C = 0 Goku; Cell en el 14), medidas en la tabla del ELF.
    // [H] arranque 0x7D0 (1) y 0x7D2 (14), y 0x30 del tipo 1 (13): el decompilado no trae el argumento; es el valor que a1 deja
    // el codigo de la comparacion (EU 0x2A80E0/0x2A8104 y 0x2A7218).
    static constexpr int kV[19][4] = {   // por indice: arranque, 2E, 2F, 30
        {1,-1,4,6}, {8,10,12,13}, {14,-1,0x13,0x14}, {}, {}, {}, {}, {}, {}, {}, {0x27,-1,0x2C,0x2D}, {}, {}, {}, {0,-1,4,6}, {}, {0x41,-1,0x46,0x47}, {}, {}};
    if (d.indice >= 0 && d.indice < 19 && (kV[d.indice][2] | kV[d.indice][3]) != 0) {
      const std::vector<uint8_t> elf = P::LeerElf(iw);
      const int perfil = anim::apev::PerfilAdx(anim::apev::CmnActor(uint32_t(fj->jugador)));
      int* dest[4] = {&d.voz_arranque, &d.voz_2e, &d.voz_2f, &d.voz_30};
      for (int k = 0; k < 4; ++k) *dest[k] = kV[d.indice][k] >= 0 ? anim::apev::EntradaAdxPerfil(elf, perfil, kV[d.indice][k]) : -1;
      REXLOG_INFO("dbz3 iwmini: voces de pose indice {} perfil {}: arranque ADX {} / 2E {} / 2F {} / 30 {}", d.indice, perfil,
                  d.voz_arranque, d.voz_2e, d.voz_2f, d.voz_30);
    }
  }
  for (int i=0,n=P::CuantasAmt(t51);i<n;++i) d.aviso_atlas.push_back(P::TexturaAmt(t51,i));
  // DS93 - camaras de la maquina de estados de tierra (ver ClipCamIa). El original las reproduce con
  // Camera_PlayCameraAnim_ (EU 0x2972D0) sobre el #AMC general DATA_CMN 915 hijo 2: entrada 0x296 (180 cuadros) y
  // resultado por indice/tipo (Minigame_State30_EnterResultAnim_ 0x29AA90 / Battle_CharState2eInit 0x29B3B0).
  //  - exito: rama 0xD de State2E (la que State2F fija para el tipo 1); fallo: rama 0 de State30.
  //  - indice 0: State2E/30 no ramifican; 0x259 exito y 0x25A fallo.
  //  - el clip de resultado del indice 0 esta centrado en el luchador (coordenadas ~0); los de los demas campos
  //    vienen en coordenadas del campo (p. ej. el 2 acaba en z=-3332). Se guarda el anclaje por indice.
  // Lo que no esta aqui (intro de mision de 8-25 s, ramas +0xB8C del resultado) queda [H] en el informe: no son
  // clips de este visor.
  // DS112: camara de ARRANQUE de cada indice. s_SPACE (EU 0x29BFF0) llama a Camera_PlayCameraAnim_ con un clip
  // distinto por indice y rama de +0xB8C; el puerto no tiene +0xB8C, asi que se usa la rama 0 (el personaje esta en la
  // pose 0 al pulsar START: asi entra el estado 0x2C desde la pose de espera). El 600 del decomp es DECIMAL: 0x258.
  //   indice 0  -> 600 (0x258)                  sin ramas
  //   indice 1  -> 0x28A (rama 0; 0xD/C/B -> 0x290/0x28D/0x293)                    [H] la rama
  //   indice 2  -> 700 (0x2BC, rama 0; 0xD/C/B -> 0x2C2/0x2BF/0x2C5)               [H] la rama
  //   indice 10 -> 0x41A                          sin ramas (280 cuadros = la animacion 0x49C)
  //   indice 14 -> 0x4E2                          sin ramas
  //   indice 16 -> 0x546 (rama 0; 8/D/C/A/7/B/3 -> 0x552/0x555/0x54C/0x558/0x55B/0x549/0x54F)   [H] la rama
  switch (d.indice) {
    case 0: d.cam_bien = 0x259; d.cam_mal = 0x25A; d.cam_final_rel = true; d.cam_arranque = 0x258; break;
    case 1: d.cam_arranque=d.cara==11 ? 0x293 : d.cara==12 ? 0x28D : d.cara==13 ? 0x290 : 0x28A;
            d.cam_bien=d.cam_arranque+1;d.cam_mal=d.cam_arranque+2;break;
    case 2: d.cam_arranque=d.cara==11 ? 0x2C5 : d.cara==12 ? 0x2BF : d.cara==13 ? 0x2C2 : 0x2BC;
            d.cam_bien=d.cam_arranque+1;d.cam_mal=d.cam_arranque+2;break;
    case 10: d.cam_bien = 0x41B; d.cam_mal = 0x41C; d.cam_arranque = 0x41A; break;
    case 14: d.cam_bien = 0x4E3; d.cam_mal = 0x4E4; d.cam_arranque = 0x4E2; break;
    case 16: d.cam_arranque=d.cara==3 ? 0x54F : d.cara==10 ? 0x558 : d.cara==8 ? 0x552 : d.cara==7 ? 0x55B : 0x546;
             d.cam_bien=d.cam_arranque+1;d.cam_mal=d.cam_arranque+2;break;
    default: break;
  }
  // 30/10: sobrevuelo del estado 0x31 (EU 0x29A7C0 + Camera_PlayCameraAnim_(+0x25C)): +0x25C es el hijo 1 de la entrada que sigue
  // al campo (903 -> 904: 3 clips de 300; PINE: 15 s en 0x7D0, 10 s en Bubbles = 898, 2 clips). Muestra el escenario y sus cristales.
  if (d.campo) {
    std::map<int, ClipCamIa> sobre;
    LeerAmcMinijuego(P::HijoAmb(cmn.Entrada(size_t(f->campo) + 1), 1), {0, 1, 2, 3}, sobre);
    for (auto& kv : sobre) d.camaras[kClipSobre + kv.first] = std::move(kv.second);
    REXLOG_INFO("dbz3 iwmini: sobrevuelo de entrada: {} clips (DATA_CMN {})", sobre.size(), f->campo + 1);
  }
  if (d.cam_bien) {
    d.cam_intro = 0x296;
    const std::vector<uint8_t> amc = P::HijoAmb(cmn.Entrada(915), 2);
    const uint32_t n = LeerAmcMinijuego(amc, {d.cam_intro, d.cam_bien, d.cam_mal, d.cam_arranque, d.cam_resultado},
                                       d.camaras);
    REXLOG_INFO("dbz3 iwmini: camaras de IW (AMC 915/2, {} clips): intro 0x{:X} ({} cuadros, solo indice 1), "
                "arranque 0x{:X} ({}), exito 0x{:X} ({}), fallo 0x{:X} ({}); resultado {}",
                n, d.cam_intro, d.camaras.count(d.cam_intro) ? d.camaras[d.cam_intro].nf : 0, d.cam_arranque,
                d.camaras.count(d.cam_arranque) ? d.camaras[d.cam_arranque].nf : 0, d.cam_bien,
                d.camaras.count(d.cam_bien) ? d.camaras[d.cam_bien].nf : 0, d.cam_mal,
                d.camaras.count(d.cam_mal) ? d.camaras[d.cam_mal].nf : 0,
                d.cam_final_rel ? "anclado al luchador" : "en coordenadas del campo");
  } else {
    REXLOG_INFO("dbz3 iwmini: indice {} sin camara de resultado en State2E/30: queda [H]", d.indice);
  }
  return true;
}

// ---- Pantalla (hilo de la interfaz) ----
// TIME con las posiciones EXACTAS de los nodos del #AMA 6/1 de DATA_USA (objetos 0x00/0x01, lienzo 640x480):
// etiqueta t03 72x24 en (376,43) y las ocho celdas de 24x24 en x 451,468,484,493,510,528,541,558 con y 43.
// El original dibuja MM'SS"CC; se estira al area 4:3 como el resto del HUD (factor 1200/640 = 1,875).
void Tiempo6(ImDrawList* dl, const Tex& t, const char* s, const S::Area& a) {
  const float kc = 2.0f, k = kc * a.s; // AMA6/1: lienzo original 600x450
  auto glifo = [&](const Glifo& g, float x, float y) {
    const ImVec2 p = a.At(x * kc, y * kc);
    dl->AddImage(t.id(), p, ImVec2(p.x + g.w * k, p.y + g.h * k), ImVec2(g.x / t.w, g.y / t.h),
                 ImVec2((g.x + g.w) / t.w, (g.y + g.h) / t.h));
  };
  if (!t) {
    dl->AddText(nullptr, 24 * k, a.At(376 * kc, 43 * kc), IM_COL32(200, 160, 255, 255), s);
    return;
  }
  glifo(kTime, 376, 43);
  static const float xs[8] = {451, 468, 484, 493, 510, 528, 541, 558};
  for (int i = 0; i < 8 && s[i]; ++i) {
    const char c = s[i];
    const Glifo& g = c >= '0' && c <= '9' ? kCifra[c - '0'] : (c == '"' ? kSegundo : kMinuto);
    glifo(g, xs[i], 43);
  }
}
// HUD de IW: TIME y contador arriba a la derecha; abajo a la izquierda el aro con la cara, la vida y la energia
// (`e` 0..1). DS115: todas las posiciones/tamanos salen del #AMA 6/1 de DATA_USA, no de una captura.
//
// DS68 - HUD de los indices 10 (0x7D9) y 14 (0x7DD), con los datos de DATA_USA 6/1 (#AMA0) y 6/0 (#AMT):
// el #AMA usa el lienzo de UI de 640x480 (ELF: fRam0049c85c = fRam0049c84c*4/3 con 640x480); se estira al area 4:3. Las posiciones y tamanos
// salen de los nodos del #AMA (objeto -> tabla +0x14, posicion del nodo y rect del hijo) y no se retocan a ojo:
//  - obj 0x12 (grupo del medidor): t26 marcas (307,406,400x16), t22 SAFE (23,386,88x32), t23 DANGER (546,401,88x32)
//    y los dos t25 (32,408) y (568,408) de 40x40.
//  - obj 0x0E (relleno del medidor): t20 392x16 en (113,387); su escala va de 0 a 1 con el valor 0..60.
//  - obj 0x0F (marco): t24 696x96 en (300,415), alfa 0,71 (su pista de color).
//  - obj 0x13 (cursor): t21 48x64 en (114,428) y la cabeza de Piccolo t39 40x40 en (113,430); la raiz mueve x de
//    0 a 389 con el valor 0..60 (ASI QUE el cursor va de 114 a 503).
//  - obj 0x1D (retrato/energia del 0x7DD): raiz y -70; t0 88x88 (2,288), t1 200x16 (87,340), t29 cara 64x64
//    (6,299) y marco t15 240x40 (67,330).
//  - obj 0x0C (HUD del 0x7D9): raiz y 355; t19 88x80 (1,365), marco t18 280x48 (54,369), vida t16 224x16
//    (87,375) y energia t17 160x8 (75,395) (el codigo dibuja 215 y 148 px dentro de esas texturas:
//    Minigame_DrawGaugeBar1/2 0x30DB90/0x30DF10), cara t29 64x64 (5,376).
// El medidor SAFE/DANGER es GroundMinigameIcons 0x30F4B0: f = 1 - min(dist/1400, 1) con dist = CharacterOutlines
// (jugador, rival) y el valor 60*(1 - dist/1400) mueve el relleno y el cursor.
void ImgIW(ImDrawList* dl, const Tex& t, const S::Area& a, float x, float y, float w, float h, float u0 = 0.0f,
           float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f, ImU32 col = IM_COL32_WHITE,
            float lienzo = 2.0f) {
  if (!t) return;
  // AMA6/1 y7/1: lienzo600x450 ->1200x900. No confundirlo con la
  // resolucion de salida640x480: las coordenadas de los nodos son originales.
  const float kx = lienzo, ky = lienzo;
  const ImVec2 p0 = a.At(x * kx, y * ky);
  dl->AddImage(t.id(), p0, ImVec2(p0.x + w * kx * a.s, p0.y + h * ky * a.s), ImVec2(u0, v0), ImVec2(u1, v1), col);
}
// EU 0x30DB90 Minigame_DrawGaugeBar1 / EU 0x30DF10 Minigame_DrawGaugeBar2.
// SOL1: cuadrilatero y UV del dibujado custom; el nodo #AMA solo da el ancla. Las texturas 16/17 son BLANCAS:
// su color sale de la pista RGBA (#AMA 6/1 nodos 0x8A0: verde, 0x8C0: amarillo), no de la textura.
void BarraCampoIW(ImDrawList* dl, const Tex& t, const S::Area& a, float x, float y, float fraccion,
                  bool namek, bool vida = false) {
  if (!t || fraccion <= 0) return;
  const float f = std::clamp(fraccion, 0.0f, 1.0f);
  const float ancho = namek ? (vida ? 215.0f : 148.0f) : 190.0f;
  const float sx = namek ? (vida ? 7.0f : 8.0f) : 2.0f;
  const float sy = namek ? (vida ? 2.0f : 0.0f) : 3.0f;
  const float sesgo = namek ? (vida ? -6.0f : -4.0f) : 6.0f;
  const float alto = namek ? (vida ? 10.0f : 7.0f) : 9.0f;
  constexpr float kc = 2.0f;
  auto p = [&](float dx, float dy) { return a.At((x + dx) * kc, (y + dy) * kc); };
  auto uv = [&](float dx, float dy) { return ImVec2(dx / t.w, dy / t.h); };
  dl->AddImageQuad(t.id(), p(sx, sy), p(sx + ancho * f, sy), p(sx + sesgo + ancho * f, sy + alto),
                   p(sx + sesgo, sy + alto), uv(sx, sy), uv(sx + ancho, sy), uv(sx + sesgo + ancho, sy + alto),
                   uv(sx + sesgo, sy + alto), namek ? (vida ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 255, 0, 255))
                                                  : IM_COL32_WHITE);
}
void PintarHudCampo(ImDrawList* fg, const S::Area& a, int indice, const std::vector<Tex>& t, const Tex& time,
                    float segundos, float energia, float vida, float dist, int cara, int forma) {
  char txt[16];   // el TIME se queda con la misma posicion/formato (Minigame_UpdateTimerDigits 0x30EED0)
  const int cuadros = int(std::max(segundos, 0.0f) * 60.0f);
  std::snprintf(txt, sizeof txt, "%02d'%02d\"%02d", std::min(cuadros / 3600, 99), cuadros / 60 % 60,
                cuadros % 60 * 99 / 60);
  Tiempo6(fg, time, txt, a);
  static const Tex vacio;
  auto tex = [&](int j) -> const Tex& { return j >= 0 && j < int(t.size()) ? t[size_t(j)] : vacio; };
  if (indice == 10) {   // 0x7D9: dos barras (vida 215 px, energia 148 px) dentro del marco t18
    ImgIW(fg, tex(19), a, 1, 365, 88, 80);                                  // aro/retrato
    ImgIW(fg, tex(18), a, 54, 369, 280, 48);                                // marco
    BarraCampoIW(fg, tex(16), a, 87, 375, vida, true, true);
    BarraCampoIW(fg, tex(17), a, 75, 395, energia, true);
    {   // cara del que juega (CaraIW: el 0x7D9 va en SSJ -> t30, no la t29 del #AMA)
      const ImVec2 p = a.At(5 * 2.0f, 376 * 2.0f);
      PintarCara(fg, p.x, p.y, 64 * 2.0f * a.s, cara, forma);
    }
    return;
  }
  if (indice != 14) return;
  const float d = std::max(dist, 0.0f);
  const float frac = 1.0f - std::min(d / 1400.0f, 1.0f);   // 0 = SAFE, 1 = DANGER
  // EU 0x30F4B0 GroundMinigameIcons_ChargeBarEffects_ObjectsDisplayTopRight_: objetos 0x0E/0x0F/0x12/0x13.
  // SOL1: #AMA 6/1: posicion del nodo + RECT, no posicion como esquina. El marco y el cursor estaban desplazados
  // media textura, cortados por abajo/derecha. El relleno reduce tambien su geometria (no solo las UV).
  ImgIW(fg, tex(24), a, 300 - 348, 415 - 48, 696, 96, 0, 0, 1, 1, IM_COL32(255, 255, 255, 181));
  if (frac > 0) ImgIW(fg, tex(20), a, 113, 387, 392 * frac, 16, 0, 0, frac, 1);
  {
    const Tex& m = tex(26);   // marcas: rect 400x16 con la ventana de textura [400,800]
    const float u = m ? 400.0f / m.w : 0.5f;
    ImgIW(fg, m, a, 307 - 200, 406 - 8, 400, 16, u, 0, 1, 1);
  }
  ImgIW(fg, tex(22), a, 23, 386, 88, 32);     // SAFE
  ImgIW(fg, tex(23), a, 546 - 44, 401 - 16, 88, 32);    // DANGER
  ImgIW(fg, tex(25), a, 32 - 20, 408 - 20, 40, 40);
  ImgIW(fg, tex(25), a, 568 - 20, 408 - 20, 40, 40);
  const float xm = 114.0f + 389.0f * frac;    // obj 0x13: la raiz mueve x 0..389 con el valor
  ImgIW(fg, tex(21), a, xm - 24, 428 - 32, 48, 64);     // cursor
  ImgIW(fg, tex(39), a, xm - 1.0f - 20, 430 - 20, 40, 40);   // cabeza de Piccolo
  // retrato/energia del 0x7DD (obj 0x1D, subido 70 px por el medidor)
  ImgIW(fg, tex(0), a, 2, 288, 88, 88);
  ImgIW(fg, tex(15), a, 67, 330, 240, 40);
  BarraCampoIW(fg, tex(1), a, 87, 340, energia, false);
  {   // cara del 0x7DD: Cell semiperfecto (CaraIW(33, 1) = t38), no la t29 del #AMA
    const ImVec2 p = a.At(6 * 2.0f, 299 * 2.0f);
    PintarCara(fg, p.x, p.y, 64 * 2.0f * a.s, cara, forma);
  }
}

// EU 0x324110 s_MSNMW_MAIN: activa los objetos de UI del campo (slots +0x18 y +0x1c del contexto, con
// UiScreen_SetObjectField158 0x00/0x01, flag2 a 0 y field40 0/1) y, si hay sprite, Spr_ActivateSlot(+0x5c). En el
// port esos objetos de la UI de PS2 no existen: el HUD de IW se pinta con ImGui (aquí) y los aros/monedas son modelos
// 3D de ps2::Escena: `motor`.
void PintarHud(ImDrawList* fg, const S::Area& a, const std::vector<Tex>& ui, const Tex& time, int cara, int forma,
               float segundos, float e) {
  // EU 0x30EED0 Minigame_UpdateTimerDigits: las cifras NO salen de centesimas sino de los cuadros que quedan a 60 Hz
  // (`iRam0049c7e4` = 60): minutos = (cuadros/60)/60, segundos = (cuadros/60)%60 y centesimas = (cuadros%60)*99/60
  // (99 por segundo, no 100: 30 cuadros = 49, no 50). Antes se hacia segundos*100. Ademas, con <= 60 s el original
  // pinta el reloj con OTRO juego de objetos (raiz +0x370 delante de la +0x20): [H], esos objetos no estan
  // identificados en DATA_USA 6/0 y aqui se sigue con el mismo atlas.
  const int cuadros = int(std::max(segundos, 0.0f) * 60.0f);
  char txt[16];
  std::snprintf(txt, sizeof txt, "%02d'%02d\"%02d", std::min(cuadros / 3600, 99), cuadros / 60 % 60,
                cuadros % 60 * 99 / 60);
  Tiempo6(fg, time, txt, a);
  // EU 0x30F4B0 GroundMinigameIcons_ChargeBarEffects_ObjectsDisplayTopRight_: indice 10 -> objeto 0x0C,
  // indice 14 -> 0x1D; TODOS los demas -> 0x18 (energia sola). SOL1: DS115 extendia el HUD de dos barras de Namek
  // a Raditz/Bubbles/Camino/Radar. #AMA 6/1 objeto 0x18: t0(2,358), t1(87,410), t29(6,369), t15(67,400).
  auto tex = [&](int j) -> const Tex& {
    static const Tex vacio;
    return j >= 0 && j < int(ui.size()) ? ui[size_t(j)] : vacio;
  };
  constexpr float kc = 2.0f;
  ImgIW(fg, tex(15), a, 67, 400, 240, 40);
  {
    const ImVec2 c0 = a.At(6 * kc, 369 * kc);
    PintarCara(fg, c0.x, c0.y, 64 * kc * a.s, cara, forma);
  }
  // AMA6/1 objeto24: cara/marco Z50, aro Z30 (orden descendente).
  // El aro contiene grises OPACOS que cubren la cara; no invertir las capas.
  ImgIW(fg, tex(0), a, 2, 358, 88, 88);
  // EU30DF10: el callback retira el rectangulo de t1 y dibuja este quad
  // personalizado despues de consumir toda la lista, con ancho190 y sesgo6.
  BarraCampoIW(fg, tex(1), a, 87, 410, e, false);
}

// DS68/DS84 - HUD de vuelo (DATA_USA 7/1 #AMA0; posiciones y tamanos de sus 12 objetos, lienzo 600x450):
//  - obj 0x03 marco t10 696x96 en (300,415), alfa 0,71; obj 0x02 relleno t12 392x16 en (113,387) (escala 0..1);
//  - obj 0x06 t8 marcas 400x16 en (307,406) (ventana de textura [400,800]), t7 SAFE 88x32 en (23,386), t6 DANGER
//    88x32 en (546,401) y los dos t11 40x40 en (32,408) y (568,408);
//  - obj 0x07 (Vegeta) / 0x08 (Goten): cursor t3 48x64 en (114,428) y la cabeza del rival t4/t5 40x40 en (114,430);
//    la raiz mueve x de 0 a 389 con el avance del medidor (0..60), o sea el cursor va de 114 a 503.
// Es el sprite que `LoadWaitFlyingMinigameSpecialScripts` (EU 0x345CE0) anade como `(v+7)|0x8000`; en el vuelo el
// rival NO tiene cuerpo 3D (0x33C660 solo mueve este medidor). El TIME se queda como en los demas (6/0 t03).
void HudVueloImpl(float segundos, float medidor, int variante, int aros, int total, int estado) {
  static bool leido = false;
  static Tex time, cifras;
  static std::vector<Tex> t7;
  static std::vector<uint8_t> ui6, ui7;
  if (!leido) {   // una vez, hilo de la interfaz
    leido = true;
    P::AfsPs2 usa;
    if (usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) {
      ui6 = P::HijoAmb(usa.Entrada(6), 0);
      ui7 = P::HijoAmb(usa.Entrada(7), 0);
    }
    time = Crear(P::CuantasAmt(ui6) > kHudTime ? P::TexturaAmt(ui6, kHudTime) : P::Imagen{});
    cifras = Crear(P::CuantasAmt(ui6) > kRadarCifras ? P::TexturaAmt(ui6, kRadarCifras) : P::Imagen{});
    t7.resize(16);
    for (int j : {3, 4, 5, 6, 7, 8, 10, 11, 12, 13})
      t7[size_t(j)] = Crear(j < P::CuantasAmt(ui7) ? P::TexturaAmt(ui7, j) : P::Imagen{});
  }
  const ImGuiIO& io = ImGui::GetIO();
  const S::Area a = S::GameArea(io);
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  char txt[16];
  const int cuadros = int(std::max(segundos, 0.0f) * 60.0f);
  std::snprintf(txt, sizeof txt, "%02d'%02d\"%02d", std::min(cuadros / 3600, 99), cuadros / 60 % 60,
                cuadros % 60 * 99 / 60);
  // DS81/DS83/DS84/DS85: TIME y contador con las posiciones EXACTAS de los nodos del #AMA 7/1, no con avances
  // medios. `Area::At` habla px de 1200x900 y el #AMA 7/1 usa lienzo 600x450 (centro 300,225): factor 2.
  //  - obj 0x00 (TIME): etiqueta t1 72x24 en (376,343); cifras t1 en x = 451,468,484,493,510,528,541,558
  //    (MM'SS"CC), todas con la celda en y 343.  - obj 0x01 (contador): icono t13 40x48 en (439,90); cifras t2
  //    en x = 482,499,[516 = "/"],531,548 con y 103 (el "/" en y 102).
  // La textura del TIME (6/0 t03) y la de las cifras cian (6/0 t12) son identicas pixel a pixel a 7/0 t1 y t2
  // (comprobado con el #AMT del jugador), asi que los glifos kCifra/kCian valen tal cual.
  const float kLienzo = 2.0f;   // #AMA 7/1: lienzo 600x450 (centro 300,225) -> 1200x900
  const float k = kLienzo * a.s;
  auto tx = [&](int j) -> const Tex& {
    static const Tex vacio;
    return j >= 0 && j < int(t7.size()) ? t7[size_t(j)] : vacio;
  };
  auto pon = [&](const Tex& t, const Glifo& g, float x, float y) {
    if (!t) return;
    const ImVec2 p = a.At(x * kLienzo, y * kLienzo);
    fg->AddImage(t.id(), p, ImVec2(p.x + g.w * k, p.y + g.h * k), ImVec2(g.x / t.w, g.y / t.h),
                 ImVec2((g.x + g.w) / t.w, (g.y + g.h) / t.h));
  };
  if (time) {
    pon(time, kTime, 376, 343);
    static const float xt[8] = {451, 468, 484, 493, 510, 528, 541, 558};
    for (int i = 0; i < 8 && txt[i]; ++i) {
      const char c = txt[i];
      if (i == 2) pon(time, kMinuto, xt[i], 343);
      else if (i == 5) pon(time, kSegundo, xt[i], 343);
      else if (c >= '0' && c <= '9') pon(time, kCifra[c - '0'], xt[i], 343);
    }
  } else {
    fg->AddText(nullptr, 24 * k, a.At(376 * kLienzo, 343 * kLienzo), IM_COL32(200, 160, 255, 255), txt);
  }
  const float v = std::clamp(medidor, 0.0f, 1.0f);
  // DS83: el #AMA 7/1 da el rect LOCAL de cada nodo; ImgIW pinta desde la esquina superior izquierda. El marco
  // (obj 0x03) esta en (300,415) con rect [-348,-48,348,48] -> (-48,367), no (300,415): el desplazamiento era el
  // "panel turquesa" de abajo a la derecha. Lo mismo con las marcas, DANGER y el cursor.
  ImgIW(fg, tx(10), a, -48, 367, 696, 96, 0, 0, 1, 1, IM_COL32(255, 255, 255, 181), kLienzo);   // marco
  if (v > 0) ImgIW(fg, tx(12), a, 113, 387, 392, 16, 0, 0, v, 1, IM_COL32_WHITE, kLienzo);   // relleno
  {
    const Tex& m = tx(8);   // marcas, ventana [400,800]; nodo (307,406) rect [-200,-8,200,8] -> (107,398)
    const float u = m ? 400.0f / m.w : 0.5f;
    ImgIW(fg, m, a, 107, 398, 400, 16, u, 0, 1, 1, IM_COL32_WHITE, kLienzo);
  }
  ImgIW(fg, tx(7), a, 23, 386, 88, 32, 0, 0, 1, 1, IM_COL32_WHITE, kLienzo);      // SAFE (rect 0,0,88,32)
  ImgIW(fg, tx(6), a, 502, 385, 88, 32, 0, 0, 1, 1, IM_COL32_WHITE, kLienzo);     // DANGER: nodo (546,401) rect [-44,-16,44,16]
  ImgIW(fg, tx(11), a, 12, 388, 40, 40, 0, 0, 1, 1, IM_COL32_WHITE, kLienzo);     // nodo (32,408) rect [-20,-20,20,20]
  ImgIW(fg, tx(11), a, 548, 388, 40, 40, 0, 0, 1, 1, IM_COL32_WHITE, kLienzo);    // nodo (568,408) idem
  const float xm = 114.0f + 389.0f * v;      // cursor + cabeza del rival (ambos a x 114 en el #AMA 7/1)
  ImgIW(fg, tx(3), a, xm - 24, 396, 48, 64, 0, 0, 1, 1, IM_COL32_WHITE, kLienzo); // nodo (114,428) rect [-24,-32,24,32]
  ImgIW(fg, tx(variante ? 5 : 4), a, xm - 20, 410, 40, 40, 0, 0, 1, 1, IM_COL32_WHITE, kLienzo);   // nodo (114,430) rect [-20,-20,20,20]
  // DS81/DS83/DS85: contador de aros. El #AMA 7/1 obj 0x01 lo pone en el icono hexagonal t13 40x48 en (439,90)
  // y las cifras cian (6/0 t12 = 7/0 t2) en las x EXACTAS de sus cinco nodos (482,499,516,531,548): el slash
  // (nodo con rect 20x24) ocupa 516 y va en y 102; las cifras, en 103. Los avances reales son 17,17,15,17, no
  // 17,17,20,17. El total es +0x166f + 1 (aros de control del #MSF + la meta), no solo +0x166f.
  if (total > 0) {
    ImgIW(fg, tx(13), a, 439, 90, 40, 48, 0, 0, 1, 1, IM_COL32_WHITE, kLienzo);
    if (!cifras) return;
    char n[16];
    std::snprintf(n, sizeof n, "%02d/%02d", std::clamp(aros, 0, 99), std::clamp(total, 0, 99));
    static const float xc[5] = {482, 499, 516, 531, 548};
    for (int i = 0; i < 5 && n[i]; ++i) {
      if (n[i] == '/') pon(cifras, kBarra, xc[i], 102);
      else if (n[i] >= '0' && n[i] <= '9') pon(cifras, kCian[n[i] - '0'], xc[i], 103);
    }
  }
  (void)estado;
}

const ps2::Tex* TituloResultadoIW(int clave);
class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    {   // eIW8: el domo del 884 sigue al ojo (SDK, cvar dbz3_sky_follow); apagado fuera del Camino de la Serpiente
      static bool domo = false;
      const std::shared_ptr<Datos> d0 = g_estado.load() == 2 ? Actual() : nullptr;
      const bool quiere = d0 && d0->indice == 2 && d0->campo && !std::getenv("DBZ3_IW_CIELO_FIJO");
      if (quiere != domo) domo = quiere, rex::cvar::SetFlagByName("dbz3_sky_follow", quiere ? "true" : "false");
    }
    if (g_estado.load() != 2) return;
    const std::shared_ptr<Datos> d = Actual();
    if (!d) return;
    if (d.get() != hecho_ || g_partida.load() != partida_) Preparar(*d);
    uint8_t* base = Base();
    const int st = P::EstadoCombate();
    // iw_fix: hasta el final del combate (estado 8 incluido): al llegar a el, B3 pinta su menu azul "Luchar otra vez?" sobre un
    // primer plano del ganador (~1,7 s hasta que el HUB lo cierra) y el fundido a negro dejaba de dibujarse
    if (!base || st < 3) return;
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (!mgr) return;
    // 30/10: la presentacion de combate de B3 (estado 3, luchador en guardia) y los primeros cuadros, hasta que la pose de IW entra:
    // IW aqui esta en negro, cargando. Antes se veia a Goku en la guardia de B3 con el HUD 4 s.
    static int cubre = 0;
    if (st == 3) cubre = 24;
    if (cubre > 0) {
      if (st == 4) --cubre;
      ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 255));
      if (st == 3) return;
    }
    const bool pausa = dbz3::GameDimmed() || dbz3::OverlayOpen();
    if (st == 4 && !fin_ && g_colocado.load()) Jugar(io, base, mgr, *d, pausa);
    if (pausa) return;
    // solIW7: la UI0/campo158 selecciona AMA5/0 objeto0 (READY+START).
    // No es un cartel de objetivo ni un segundo START de reloj de pared.
    if (!fin_ && st >= 5) {   // acabo por otra via (no deberia: el pivote es invulnerable)
      fin_ = true;
      fin_t_ = float(dbz3::RelojT(ImGui::GetTime()));
    }
    Dibujar(io, base, mgr, *d);
    if (fin_ && g_final.load() == 3 && fin_t_ > 1e29f) fin_t_ = float(dbz3::RelojT(ImGui::GetTime()));
    if (g_cartel.load() > 0 && !fin_) DibujarCartel(io, *d);
    if (!fin_) DibujarCutin(io, *d);
    rotulo_ama_ = d->avisos.Valida();
    if (const int ff = g_final.load(); ff == 1 || ff == 2) {
      // UI_SetObjectsField158Dispatch(0,0,4,0) al coger la bola (decomp 0x31F280): "DRAGON BALL OBTAINED!" = objeto 4 del AMA 5/0
      if (ff == 1 && d->indice == 16) DibujarRotuloFin(io, *d, 4, cuadro_rotulo_);
      DibujarRotuloFin(io, *d, ff == 1 ? 7 : 8, cuadro_rotulo_++);
    }
    if (const float rc = g_panel_campo.load(); rc >= 0.0f && g_final.load() != 0 && g_final.load() != 3) {
      Resultado r;
      {
        std::lock_guard<std::mutex> lk(g_mu);
        r = g_res;
      }
      const S::Area a = S::GameArea(io);
      const ImVec2 q0 = a.At(0, 0), q1 = a.At(1200, 900);
      const float ra=g_panel_campo_anim.load();
      if(!recompensa::Dibujar(ImGui::GetForegroundDrawList(),q0.x,q0.y,q1.x-q0.x,q1.y-q0.y,r,rc,TituloResultadoIW(r.clave),ra))
        DibujarResultadoIW(ImGui::GetForegroundDrawList(), q0.x, q0.y, q1.x - q0.x, q1.y - q0.y, r, rc / 60.0f, false);
      if(std::getenv("DBZ3_TEST_IWMINI") && ra>=450 && rc==420) {
        static bool medido=false;
        if(!medido) {medido=true;REXLOG_INFO("dbz3 IW campo9c: AMA450 dibujada con conteo420 retenido");}
      }
    }
    else if (g_final.load() == 0) cuadro_rotulo_ = 0;
    if (fin_) DibujarFin(io, float(dbz3::RelojT(ImGui::GetTime())));
    if (const int f = g_fundido.load(); f > 0) {   // TransitionFilter de IW al reaparecer (0.7, 0x00B09000)
      g_fundido = f - 1;
      const float t = f > 15 ? float(30 - f) / 15.0f : float(f) / 15.0f;   // sube 15 cuadros y baja otros 15
      ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
                                                    IM_COL32(0x00, 0x90, 0xB0, int(0.7f * 255 * t)));
    }
  }

 private:
  void Preparar(const Datos& d) {
    hecho_ = &d;
    partida_ = g_partida.load();
    aro_ = Crear(d.aro);
    moneda_ = Crear(d.moneda);
    bebida_ = Crear(d.bebida);
    auto hud = [&](int j) { return Crear(j < int(d.hud.size()) ? d.hud[size_t(j)] : P::Imagen{}); };
    time_ = hud(kHudTime);
    puerta_ = hud(kHudPuerta);
    campo_.clear();   // DS68: las 40 piezas de 6/0 por indice (las que no trae el HUD quedan vacias)
    campo_.resize(d.hud.size());
    for (size_t j = 0; j < d.hud.size(); ++j) campo_[j] = hud(int(j));
    clear_ = Crear(d.clear);
    ready_ = Crear(d.ready);
    start_ = Crear(d.start);
    aviso_tx_.clear();
    for (const auto& im:d.aviso_atlas) aviso_tx_.push_back(Crear(im));
    cutin_tx_.clear(), cutin_txt_.clear();
    for (const auto& im : d.cutin_retratos) cutin_tx_.push_back(Crear(im));
    for (const auto& im : d.cutin_textos) cutin_txt_.push_back(Crear(im));
    auto texs = [](const Modelo& m) { return ps2::Texturas(m); };   // R15: con el z-buffer del juego si se puede
    tex_aro_ = texs(d.m_aro);
    tex_moneda_ = texs(d.m_moneda);
    tex_bebida_ = texs(d.m_bebida);
    tex_meta_ = texs(d.m_meta);
    tex_geiser_ = texs(d.m_geiser);
    tex_roca_ = texs(d.m_roca[0]);
    rocas_.clear();
    {   // los conos del polvo suman (efecto de IW): alfa = el canal mas brillante, como el atlas de abajo
      ps2::Modelo su = d.m_polvo;
      for (P::Imagen& im : su.tex)
        for (size_t i = 0; i + 3 < im.rgba.size(); i += 4)
          im.rgba[i + 3] = uint8_t(std::max({im.rgba[i], im.rgba[i + 1], im.rgba[i + 2]}) * im.rgba[i + 3] / 255);
      tex_polvo_.clear();   // sin la prueba de profundidad: el efecto va por encima, como en IW (sin escribir Z)
      for (const P::Imagen& im : su.tex) tex_polvo_.push_back(ps2::Crear(im, false));
    }
    tx_atlas_.clear();
    tx_suma_.clear();
    for (const P::Imagen& im : d.atlas_polvo) {   // como iw_mini_disparo: la suma con alfa = el canal mas brillante
      tx_atlas_.push_back(ps2::Crear(im));
      P::Imagen su = im;
      for (size_t i = 0; i + 3 < su.rgba.size(); i += 4)
        su.rgba[i + 3] = uint8_t(std::max({su.rgba[i], su.rgba[i + 1], su.rgba[i + 2]}) * su.rgba[i + 3] / 255);
      tx_suma_.push_back(ps2::Crear(su));
    }
    polvo_.clear();
    polvo_visto_ = g_polvo_n.load();
    fx_.clear();
    fx_visto_ = g_fx_n.load();
    tx_aura_.clear();aura_=d.aura_base;
    for(const auto& im:d.atlas_aura)tx_aura_.push_back(ps2::Crear(im,true));
    pasos_ap_.clear();tx_pasos_ap_.clear();
    for(const auto& im:d.atlas_pasos_ap)tx_pasos_ap_.push_back(ps2::Crear(im));
    impactos_ap_.clear();tx_impactos_ap_.clear();
    for(const auto& im:d.atlas_impactos_ap)tx_impactos_ap_.push_back(ps2::Crear(im));
    {std::lock_guard<std::mutex> lk(g_pasos_ap_mu);g_pasos_ap.clear();}
    tex_marca_ = texs(d.m_marca);
    tex_d_ = texs(d.m_d);
    tex_npc_ = texs(d.m_npc);
    tex_platano_ = texs(d.m_platano);
    // DS169: aureola de Goku muerto (0x7D1/0x7D2). En el modelo de Goku de IW (DATA_CMN 409/0 + texturas 410) el
    // anillo es la parte del hueso GOK_RING; de sus 23 mallas se elige la que es un anillo fino (radio 1,68 y grosor
    // 0,22 en 409/0). Respaldo: el traje0 de B3 no trae el anillo; el4 si (fid282).
    halo_ok_ = false;
    halo_.malla.tris.clear();
    tex_halo_.clear();
    if ((d.indice == 1 || d.indice == 2) && !d.halo_nativo) {
      P::AfsPs2 cmn;
      if (cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS")) {
        const ps2::Modelo g = CargarSuelto(cmn.Entrada(409), cmn.Entrada(410), false);
        std::map<int, std::vector<ps2::Malla::Tri>> partes;
        for (const auto& tr : g.malla.tris) partes[tr.parte].push_back(tr);
        int elegida = -1, tex_elegida = 0;
        float mejor = 1e9f;
        for (const auto& [parte, tris] : partes) {
          ps2::V3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
          for (const auto& tr : tris)
            for (const ps2::V3& q : tr.p) {
              lo.x = std::min(lo.x, q.x), lo.y = std::min(lo.y, q.y), lo.z = std::min(lo.z, q.z);
              hi.x = std::max(hi.x, q.x), hi.y = std::max(hi.y, q.y), hi.z = std::max(hi.z, q.z);
            }
          const float dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z, cy = (lo.y + hi.y) * 0.5f;
          // anillo: casi plano (dy < 0,6), ancho y fondo de 2 a 6 (radio 1,68 -> 3,4) y en la mitad alta del modelo
          if (dy < 0.6f && dx > 2.0f && dx < 6.0f && dz > 2.0f && dz < 6.0f && cy > 15.0f) {
            const float err = std::fabs(dx - dz) + std::fabs(dy - 0.22f);
            if (err < mejor && !tris.empty()) elegida = parte, tex_elegida = tris[0].tex, mejor = err;
          }
        }
        if (elegida >= 0 && tex_elegida >= 0 && size_t(tex_elegida) < g.tex.size()) {
          ps2::Malla anillo;
          ps2::V3 c{};
          int n = 0;
          for (auto tr : partes[elegida]) {
            for (const ps2::V3& q : tr.p) c.x += q.x, c.y += q.y, c.z += q.z, ++n;
            tr.tex = 0;
            anillo.tris.push_back(tr);
          }
          c = {c.x / n, c.y / n, c.z / n};
          for (auto& tr : anillo.tris)
            for (ps2::V3& q : tr.p) q = {q.x - c.x, q.y - c.y, q.z - c.z};
          halo_.malla = std::move(anillo);
          tex_halo_.push_back(ps2::Crear(g.tex[size_t(tex_elegida)]));
          halo_alto_ = c.y;   // tras Centrar, y = altura sobre los pies (409/0: 20,08)
          halo_ok_ = bool(tex_halo_[0]);
          REXLOG_INFO("dbz3 iwmini: aureola de IW (409/0 parte {}, tex {}): {} triangulos, {:.2f} sobre los pies", elegida,
                      tex_elegida, int(halo_.malla.tris.size()), halo_alto_);
        } else {
          REXLOG_WARN("dbz3 iwmini: aureola de IW: no se encontro el anillo GOK_RING en 409/0");
        }
      }
    }
    for (int j : {kRadarMarco, kRadarRejilla, kRadarPantalla, kRadarYo, kRadarBola, kRadarFlecha, kRadarCifras, kRadarMarca})
      radar_[j] = hud(j);
    // EU 0x323840 LoadWaitDragonMissionGroundMinigameItems
    cogido_.assign(d.objetos.size(), false);
    campo_ini_ = true, camp_ok_ = false;   // tanda3: la logica original de s_SPACE arranca de nuevo con la partida
    restante_ = d.limite;
    cogidos_ = aros_ = 0;
    cutin_hechos_ = 0, cutin_c_ = cutin_d_ = cutin_mitad_ = false, cutin_act_ = -1;
    puntos_objetos_ = 0;
    if (g_meta_abierta) aros_ = d.num_a;
    npc_p_ = d.npc_inicio;
    npc_dir_ = {};
    npc_t_ = resbalon_ = 0;
    bubbles_acum_ = 0;
    std::array<float,0xE3> largos_bubbles{};
    for (unsigned c : iwbubbles::kCodigos) largos_bubbles[c]=float(d.bubbles_clips[c].cuadros);
    bubbles_.Iniciar({npc_p_.x,npc_p_.y,npc_p_.z},largos_bubbles);
    // Solo pruebas: misma semilla Memory(n) que el original para parear por cuadro.
    if (const char* sem=std::getenv("DBZ3_TEST_IW_SEMILLA"); sem && *sem && iwbubbles::Prueba(d.clave))
      iwazar::Global().Cambiar(uint32_t(std::strtoul(sem,nullptr,0)));
    PrepararBubbles(d);
    // P1 (7/11): 0x7DD con la CPU original (iw_mips.h persecucion); el #WCF del campo 906 da el rayo de visibilidad.
    // La rejilla del campo (Node_WrapPositionInGrid: 3000 x 3000 en 10 x 10 desde -1350, medida en el savestate del dorado).
    piccolo_ini_ = false, piccolo_acum_ = 0, piccolo_t_ = 0;
    piccolo_wcf_ok_ = !d.piccolo_wcf.empty() &&
                      piccolo_wcf_.Load(d.piccolo_wcf.data(), d.piccolo_wcf.size(), {-1500, -1500, 1500, 1500, 3});
    if (!d.piccolo_wcf.empty())
      REXLOG_INFO("dbz3 iwmini: 0x7DD CPU original de Piccolo: wcf={} nodos={} error={}", piccolo_wcf_ok_,
                  piccolo_wcf_.Nodes(), piccolo_wcf_.Error());
    if (d.npc==1 && d.bubbles_clips_ok && iwbubbles::Prueba(d.clave))
      REXLOG_INFO("dbz3 gp2 bubbles tick=0 init estado=0 codigo=D2 cuadro=0 flags210/214/218=0/0/0 pos={},{},{}",
                  npc_p_.x,npc_p_.y,npc_p_.z);
    platanos_ = d.platanos;
    semilla_ = uint32_t(NowMs());
    g_resbalando = false;
    fin_ = meta_ = false;
    auto_ms_ = NowMs();
    auto_i_ = 0;
  }

  // EU 0x2A63B0 Battle_TriggerProgressCutins: cada disparador una sola vez y solo si no hay otro aviso en curso
  // (+0x17C, 200 cuadros). Orden del original: B, A, E, C, D. Voz de s_PLY_CUTIN y recuadro (DibujarCutin).
  void PasoCutins(ImGuiIO& io, uint8_t* base, const Datos& d) {
    if (cutin_act_ >= 0) {
      cutin_t_ += 60.0f * dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
      if (cutin_t_ >= 200.0f) cutin_act_ = -1;
      return;
    }
    if (fin_ || g_final.load() != 0) return;
    if (std::getenv("DBZ3_TEST_IWMINI"))   // solo pruebas.py: adelanta el reloj para ver el aviso B (ultimo minuto) sin esperar 4 minutos
      if (const char* f = std::getenv("DBZ3_TEST_CUTIN"))
        if (f[0] == 'B' && d.limite - restante_ >= 3.0f && restante_ > 61.0f) restante_ = 60.5f;
    for (char orden : {'B', 'A', 'E', 'C', 'D'}) {
      for (int i = 0; i < kNumCutins; ++i) {
        const Cutin& c = kCutins[i];
        if (c.indice != d.indice || c.disparo != orden || (cutin_hechos_ >> i & 1)) continue;
        if (d.indice == 16 && (orden == 'C' || orden == 'D') && !AvisosCD16()) continue;
        const bool sale = orden == 'B' ? restante_ <= 60.0f
                        : orden == 'A' ? restante_ <= d.limite * 0.5f
                        : orden == 'E' ? d.limite - restante_ >= 500.0f / 60.0f
                        : orden == 'C' ? cutin_c_ : cutin_d_;
        if (!sale) continue;
        cutin_hechos_ |= 1u << i;
        if (orden == 'C') cutin_c_ = false;
        if (orden == 'D') cutin_d_ = false;
        cutin_act_ = i, cutin_t_ = 0;
        const int voz = i < int(d.cutin_voz.size()) ? d.cutin_voz[size_t(i)] : -1;
        REXLOG_INFO("dbz3 iwmini: aviso {} del indice {} (texto {}, linea {} perfil {} -> ADX {}) quedando {:.1f} s",
                    orden, d.indice, c.texto, c.linea, c.perfil, voz, restante_);
        if (voz >= 0) {
          const int byte = base ? int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9)) : 1;   // 0 = voces japonesas
          const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
          P::OirAdxPs2(P::BuscarIW(), byte == 0 ? "ADX_JPN.AFS" : "ADX_USA.AFS", voz, vol);
        }
        return;
      }
    }
  }
  // Recuadro: la ventana de dialogo de DATA_CMN 936 (la misma del vuelo; centro del texto en secuencia 34). El retrato
  // del AMA se cambia por el del personaje del aviso (el que no es el arco azul de la textura 1).
  void DibujarCutin(ImGuiIO& io, const Datos& d) {
    if (cutin_act_ < 0 || !d.cutin_ama.Valida() || g_sin_hud.load()) return;
    const Cutin& c = kCutins[cutin_act_];
    auto largo = [&](int obj, int def) {
      const int n = d.cutin_ama.Objeto(uint32_t(obj));
      return n >= 0 ? int(anim::ama::MaxCuadros(anim::ama::CrearArbol(d.cutin_ama, n))) : def;
    };
    const int entrada = largo(15, 26), salida = largo(16, 27);
    const float t = cutin_t_, t_sal = 200.0f - float(salida);
    const S::Area a = S::GameArea(io);
    auto at = [&](float x, float y) { return a.At(x * 2, y * 2); };
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto pinta = [&](int obj, float tiempo, float dx, float dy) {
      const int nodo = d.cutin_ama.Objeto(uint32_t(obj));
      if (nodo < 0) return;
      auto arbol = anim::ama::CrearArbol(d.cutin_ama, nodo, tiempo);
      anim::ama::MarcarArbol(arbol, tiempo);
      std::vector<anim::ama::Entrada> entradas;
      anim::ama::Contexto ctx;
      ctx.pos[0] = dx, ctx.pos[1] = dy;
      anim::ama::ActualizarNodo(arbol, arbol.raiz, ctx, entradas);
      for (const auto& e : entradas) {
        if (e.SinTextura()) continue;
        size_t ti = e.textura;
        if (ti != 1 && ti < cutin_tx_.size()) ti = size_t(c.retrato);   // retrato del AMA -> el del aviso
        if (ti >= cutin_tx_.size() || !cutin_tx_[ti]) continue;
        const auto& tx = cutin_tx_[ti];
        anim::ama::Quad q;
        anim::ama::DibujarEntrada(e, q);
        auto color = [&](int i) { return std::clamp(int(e.tinte[i] * 255 + e.rgba[i] + .5f), 0, 255); };
        dl->AddImageQuad(tx.id(), at(q.xy[0][0], q.xy[0][1]), at(q.xy[1][0], q.xy[1][1]), at(q.xy[2][0], q.xy[2][1]),
                         at(q.xy[3][0], q.xy[3][1]), ImVec2(q.uv[0][0] / tx.w, q.uv[0][1] / tx.h),
                         ImVec2(q.uv[1][0] / tx.w, q.uv[1][1] / tx.h), ImVec2(q.uv[2][0] / tx.w, q.uv[2][1] / tx.h),
                         ImVec2(q.uv[3][0] / tx.w, q.uv[3][1] / tx.h), IM_COL32(color(0), color(1), color(2), color(3)));
      }
    };
    pinta(0, t, 0, 0);   // medido en la foto del original: el marco va sin el offset de la secuencia (que lo bajaba 175)
    pinta(t < t_sal ? 15 : 16, t < t_sal ? t : t - t_sal, 0, 0);
    if (t >= float(entrada) && t < t_sal && c.texto < int(cutin_txt_.size()) && cutin_txt_[size_t(c.texto)]) {
      const auto& tx = cutin_txt_[size_t(c.texto)];
      const float x = d.cutin_pos[0], y = d.cutin_pos[1];
      dl->AddImage(tx.id(), at(x - tx.w * .5f, y - tx.h * .5f), at(x + tx.w * .5f, y + tx.h * .5f));
    }
  }

  void Acabar(uint8_t* base, uint32_t mgr, const Datos& d, bool meta) {
    fin_ = true;
    meta_ = meta;
    fin_t_ = 1e30f;   // R16: el fundido empieza cuando acaba la animacion final de IW (Fotograma pone kFin)
    fin0_ = float(dbz3::RelojT(ImGui::GetTime()));
    g_final = meta ? 1 : 2;
    const int dif1 = d.dificultad + 1;
    // G+0x165C / G+0x165A: la meta tambien suma (s_SPACE 0x31F280: +0x165C++ y +0x16BB++ al tocarla)
    // EU 0x322930 MgLogic_GaugeRatioTier: el rango sale del cociente recogidos/total (+0x165c / +0x165a, s16 en el
    // original) con los umbrales 0.95, 0.8, 0.65 y 0.5 -> 4..0. Es exactamente esta escala: `coincide`.
    const float r = float(cogidos_ + (meta ? 1 : 0)) / float(std::max(d.total, 1));
    const int rango = r >= 0.95f ? 4 : r >= 0.8f ? 3 : r >= 0.65f ? 2 : r >= 0.5f ? 1 : 0;
    // EU s_SPACE 0x31F280 y MgResult_ComputeScore 0x323150: las monedas B ya
    // suman +0x1658 al recogerlas; tocar la nave añade el bono por dificultad.
    const int bonus_meta = !meta ? 0 : d.dificultad == 5 ? 10000 : d.dificultad == 4 ? 4000
                              : d.dificultad == 3 ? 1500 : dif1 * 200;
    const int puntos_extra = puntos_objetos_ + bonus_meta;
    // EU 0x323150 MgResult_ComputeScore
    const int puntos = puntos_extra +
        (kBonusRango[rango] * dif1 * dif1 + int(std::max(restante_, 0.0f)) * dif1 * 10) / 100 * 100;
    Resultado res;
    res.clave = d.clave;
    res.fila = d.fila;
    res.superado = meta;
    res.centesimas = int((d.limite - std::max(restante_, 0.0f)) * 100.0f);
    res.puntos = meta ? puntos : 0;
    res.rango = meta ? rango : -1;   // el de MgLogic_GaugeRatioTier, no el de los puntos (salia sin rango con 98 %)
    // EU 0x323150 MgResult_ComputeScore: +0x13 (premio ZENI) empieza en 100 y solo se recalcula con rango; +0x11
    // guarda el tiempo y +0x9 el premio/10. Se separan de `puntos` (valor historico del mapa) para las pantallas.
    res.score = res.puntos;
    res.zeni = meta ? puntos : 100;
    res.score = std::min(res.zeni / 10, 9999); // EU323150 +9: SCORE, no el premio ZENI
    // EU 0x31F280 s_SPACE: tocar la meta incrementa +0x165C antes de calcular el porcentaje (EU 0x323150).
    // SOL1: el rango ya incluia esa unidad, pero la fila de objetos no: mostraba 93 % para un rango calculado con 97 %.
    res.objetos = cogidos_ + (meta ? 1 : 0);
    res.objetos_total = d.total;   // DS153: la fila de MSNMW es el % (EU 0x323150: +0x165c / +0x165a * 100)
    res.dificultad = d.dificultad;
    res.con_tiempo = true;
    CompletarResultado(res);
    {
      std::lock_guard<std::mutex> lk(g_mu);
      g_res = res;
    }
    REXLOG_INFO("dbz3 iwmini: {}: tiempo {:.2f} s, cogidos {}/{} ({:.0f} %), rango {}, puntos {}",
                meta ? "META" : "TIEMPO", d.limite - restante_, cogidos_, d.total, r * 100.0f,
                "CBASZ"[rango], res.puntos);
  }

  void Jugar(ImGuiIO& io, uint8_t* base, uint32_t mgr, const Datos& d, bool pausa) {
    const uint32_t x0 = mgr + kX0;
    // EU 0x299900 LoadWaitGroundMinigameObjective
    // El panel puede abrirse antes del primer Fotograma guest. No ejecutar un
    // tick de IA con las banderas iniciales aun sin publicar la introduccion.
    const bool espera_bb = d.bubbles_poses && iwbubbles::Prueba(d.clave) && bubbles_pose_.fase < 2;
    const bool parado = g_reloj_parado.load() || espera_bb;   // R16: entrada y cartel del objetivo de IW
    if(!pausa && parado && d.bubbles_poses && iwbubbles::Prueba(d.clave) && !g_final.load())
      IntroBubbles(io,d);
    // EU 0x2F3BE0 GroundMinigameTimer: el original acumula lo andado (+0x8b9c += +0x8b80) y lo TOPA al limite
    // (`if (0 < +0x7cc && limite < +0x8b9c) +0x8b9c = limite`), asi que el reloj nunca pasa de 0. Solo corre si
    // (+0x8c & 1) o (+0x8c & 4) (minijuego cargado/activo) y si el cartel del objetivo (+0x8d21) esta apagado, que es
    // `parado`. El segundo contador +0x8bb0 (con +0x8d26) no se usa en estos minijuegos: [H].
    if (!pausa && !parado) restante_ = std::max(0.0f, restante_ - dbz3::RelojDt(std::min(io.DeltaTime, 0.1f)));
    if (!pausa && !parado && g_final.load() == 0) {
      PasoRocas(base, x0, d);
      PasoKami(base, x0, d);
      if (d.indice == 10 && g_vida.load() <= 0.0f) {   // EU 0x2A6260: sin vida, estado 0x30
        if (g_muerte.load() == 0) {
          g_muerte = 1;
          g_sin_mando = true;
          REXLOG_INFO("dbz3 iwmini: sin vida");
        }
        if (g_muerte.fetch_add(1) > 154) {
          Acabar(base, mgr, d, false);
          return;
        }
      }
    }
    // Escena de medida de finales, opt-in y solo con el arnes: el resultado
    // pasa por Acabar/PasoFinal reales, sin esperar el limite de todo el curso.
    if (!pausa&&!parado&&std::getenv("DBZ3_TEST_IWMINI")) {
      const char* final=std::getenv("DBZ3_SOLIW6_FINAL");
      if (final&&final[0]=='2'&&d.limite-restante_>=2.0f) {
        REXLOG_INFO("dbz3 solIW6: escena controlada de fracaso de campo (sin prueba de logica del curso)");
        Acabar(base,mgr,d,false);
        return;
      }
    }
    // Prueba opt-in: con DBZ3_TEST_IWMINI_PLATANO=<ms> el piloto automatico
    // espera <ms> (para que Bubbles tire platanos naturales), recorre esa lista
    // pisandolos y no toca los aros. Sin la variable, el piloto de siempre.
    static const char* plat_auto = std::getenv("DBZ3_TEST_IWMINI_PLATANO");
    static const uint64_t plat_t0 = NowMs();
    const bool plat_auto_on = plat_auto && *plat_auto;
    if (g_auto && plat_auto_on && !parado && NowMs() - auto_ms_ > 700) {
      if (NowMs() - plat_t0 >= uint64_t(std::atol(plat_auto)) && !platanos_.empty()) {
        auto_ms_ = NowMs();
        Poner(base, x0, platanos_.front());   // el resbalon lo aplica el bucle de abajo, igual que al andar
      }
    } else if (g_auto && !plat_auto_on && !parado && NowMs() - auto_ms_ > 700) {   // prueba: a cada aro y bebida (y despues a la meta)
      auto_ms_ = NowMs();
      while (auto_i_ < d.objetos.size() && cogido_[auto_i_]) ++auto_i_;
      V3 p = auto_i_ < d.objetos.size() ? d.objetos[auto_i_].p : d.npc == 1 ? npc_p_ : d.llegada;
      if (!d.campo) p.y = 0;   // en el campo de IW, a su altura (el Camino de la Serpiente va por arriba)
      Poner(base, x0, p);
    }
    const V3 yo = Leer(base, x0);
    // EU 0x31F280 s_SPACE
    // tanda3: con dbz3_iw_7d0_original (apagada de serie) y el campo de IW instalado, la recogida la decide s_SPACE ORIGINAL
    // ejecutado del ELF (banderas de G: 2 = cogido; radios, grupos y bola oculta del original). Si no, la cuenta del port.
    camp_ok_ = false;
    if (campo_ini_) REXLOG_INFO("dbz3 iwmini: s_SPACE original: campo {} itm {} g_campo {} activa {}", d.campo, d.itm_crudo.size(),
                                dbz3::iwmips::campo::g_campo != nullptr, dbz3::iwmips::campo::g_activa && dbz3::iwmips::campo::g_activa());
    if (d.campo && !d.itm_crudo.empty() && dbz3::iwmips::campo::g_activa && dbz3::iwmips::campo::g_activa()) {
      dbz3::iwmips::campo::Entrada ce;
      ce.inicio = campo_ini_;
      ce.indice = d.indice, ce.dif = d.dificultad;
      ce.itm = d.itm_crudo.data(), ce.itm_n = uint32_t(d.itm_crudo.size());
      ce.slot = std::rand() & 7;
      ce.anc[0] = yo.x, ce.anc[1] = yo.y + 6.0f, ce.anc[2] = yo.z;   // CharacterHitboxes: raiz + 6 (la cadera)
      ce.jug[0] = yo.x, ce.jug[1] = yo.y, ce.jug[2] = yo.z;
      dbz3::suelo_iw::Periodos(base, ce.ex, ce.ez);
      ce.t = uint64_t(NowMs());
      uint32_t semilla_campo = 0;
      camp_ok_ = dbz3::iwmips::campo::g_campo(ce, camp_, semilla_campo);
      if (camp_ok_) {
        if (campo_ini_)
          REXLOG_INFO("dbz3 iwmini: s_SPACE ORIGINAL activo: total {} (port {}), bola oculta slot {}", dbz3::iwmips::campo::Total(camp_),
                      d.total, dbz3::iwmips::campo::Flag(camp_, 0x16A6));
        campo_ini_ = false;
      }
    }
    if (camp_ok_ && g_auto && NowMs() - diag_ms_ > 2000) {   // solo pruebas: el objetivo del piloto y la bandera de G
      diag_ms_ = NowMs();
      if (auto_i_ < d.objetos.size()) {
        static const int bg[5] = {0x165E, 0x1670, 0x1692, 0x16A8, 0x16A8};
        const Objeto& q = d.objetos[auto_i_];
        const float qx = yo.x - q.p.x, qy = yo.y + 6.0f - q.p.y, qz = yo.z - q.p.z;
        REXLOG_INFO("dbz3 iwmini: diag piloto i {} grupo {} k {} bandera {} d2 {:.1f} pos ({:.1f},{:.1f},{:.1f}) yo ({:.1f},{:.1f},{:.1f})", auto_i_, q.grupo, q.k,
                    dbz3::iwmips::campo::Flag(camp_, bg[q.grupo] + q.k), qx * qx + qy * qy + qz * qz, q.p.x, q.p.y, q.p.z, yo.x, yo.y, yo.z);
      }
    }
    for (size_t k = 0; k < d.objetos.size(); ++k) {
      const Objeto& o = d.objetos[k];
      // IW recoge con CharacterHitboxes: distancia 3D entre el cuerpo (raiz + 6, la cadera) y el objeto, no en el plano.
      // Antes se medía solo en XZ y se cogian objetos a traves del suelo o en pleno salto.
      // EU 0x142470 CharacterHitboxes
      V3 objeto=o.p;
      dbz3::suelo_iw::EnvolverReferencia(objeto.x,objeto.z,yo.x,yo.z);
      const float ox = yo.x - objeto.x, oy = yo.y + 6.0f - objeto.y, oz = yo.z - objeto.z;
      // EU 0x304D90 ObjectsDisappearingAfterCollection
      if (camp_ok_) {   // original: bandera 2 del objeto k de su grupo (A 0x165E, B 0x1670, C 0x1692, D 0x16A8)
        static const int base_g[5] = {0x165E, 0x1670, 0x1692, 0x16A8, 0x16A8};
        if (cogido_[k] || dbz3::iwmips::campo::Flag(camp_, base_g[o.grupo] + o.k) != 2) continue;
      } else if (cogido_[k] || std::sqrt(ox * ox + oy * oy + oz * oz) > o.radio) continue;
      cogido_[k] = true;
      ++cogidos_;
      if (d.indice == 16 && AvisosCD16() && (o.grupo == 1 || o.grupo == 3)) {
        // s_SPACE EU31F280: C al completar B (+1690 >= +1691),
        // D al completar D (+16B8 >= +16B9), NO mitad/todos de A (A=0).
        bool completo = true;
        for (size_t q = 0; q < d.objetos.size(); ++q)
          if (d.objetos[q].grupo == o.grupo && !cogido_[q]) completo = false;
        if (completo) {
          if (o.grupo == 1) cutin_c_ = true;
          else cutin_d_ = true;
        }
      }
      if (o.grupo == 1) puntos_objetos_ += o.valor;  // EU s_SPACE 0x31F280: +0x1658 += #ITM+0x340+2*k
      g_resp_x = o.p.x, g_resp_z = o.p.z, g_resp_hay = true;   // IW +0x1620 = posicion del ultimo objeto recogido
      if (o.grupo == 2) {   // bebida: energia (tope 100)
        g_bebida += o.valor;
        REXLOG_INFO("dbz3 iwmini: bebida +{} de energia", o.valor);
      } else if (o.grupo == 4 && d.indice == 10) {   // EU31F280: G+2050 = cuerpo+1850 (VIDA); energia = +205C
        g_curacion += o.valor;
        REXLOG_INFO("dbz3 iwmini: objeto D +{} de vida", o.valor);
      }
      if (o.grupo == 0 || o.grupo == 3)
      {
        REXLOG_INFO("dbz3 iwmini: aro {} cogido ({} objetos de {}) quedando {:.1f} s", ++aros_, cogidos_, d.total,
                    restante_);
        // s_SPACE 0x31F280: D al coger el ultimo aro; C al llegar a la mitad (+0x16C0 lo deja pasar una vez; en el 2 el
        // original usa una posicion clave que el port no tiene: aqui tambien la mitad de los aros).
        if (d.indice != 16 || !AvisosCD16()) {
          if (aros_ >= d.num_a) cutin_d_ = true;
          else if (!cutin_mitad_ && aros_ >= d.num_a / 2) cutin_mitad_ = cutin_c_ = true;
        }
      }
    }
    PasoCutins(io, base, d);
    // D34: el PILOTO AUTOMATICO devuelve al jugador al suelo en el mismo cuadro (el objeto se coge arriba, con su y del
    // #ITM: los aros flotan 20-40 u; dos del 900 estan en -5 y con el jugador ahi 2 s el ojo entraba en la roca, fotos
    // c051/c052). Solo la prueba: el jugador real anda con el #WCF (el ajuste general esta en Fotograma, al nacer/quieto);
    // tampoco actua en el aire ni en el primer plano final.
    if (d.campo && g_auto && !g_cae && g_final.load() == 0) {
      const V3 yf = Leer(base, x0);
      const float g = dbz3::suelo_iw::Altura(yf.x, yf.z, -1e30f);
      if (g > -1e29f && std::fabs(yf.y - g) > 0.05f) Poner(base, x0, {yf.x, g, yf.z});
    }
    // la meta se abre al coger todos los aros (0x31F520: G+0x166E >= G+0x166F -> +0x1DC = 1; en el 10, la nave)
    if (d.npc && !pausa && !parado) {   // en la pausa (y la entrada) no se mueve
      MoverNpc(io, base, mgr, d, yo);
      if (fin_) return;
    }
    // s_SPACE 0x31F280: la meta se abre al recoger todos los objetos (0x165a-1 <= 0x165c; el Radar, -2), no solo los
    // aros A. En el original +0x165c incluye tambien el objetivo oculto +0x5C0 (que el port no tiene: ver d.total, caso
    // "ya cogido"). Tocar la meta es +0x1630
    // (0x323840: copia de +0x840 = salida) salvo el 2 (disparador +0x30, radio^2 600) y el 16 (bola del Radar).
    // [H] el original abre con 0x165A - 1 <= 0x165C (el 16: - 2): con el oculto pendiente exige S + 1 (todos y la bola
    // oculta). El port no tiene la bola: abre con S (total - 2) en todos.
    if (d.npc != 1 && cogidos_ >= d.total - 2 && DistH(yo, d.llegada) < d.radio_llegada) Acabar(base, mgr, d, true);
    else if (restante_ <= 0) Acabar(base, mgr, d, false);
  }

  // EU 0x3172E0 s_BBR_POST_EXEC
  // EU 0x2A8460 s_SPACE
  // EU 0x363D10 MinigameHideNSeekPiccolo'sCPU
  // NPC del MSNMW (decomp EU s_BBR_POST_EXEC 0x3172E0 y MgPly_Calc*Vel 0x316B90-0x3170D0). Huir (Bubbles): cada
  // decision elige el modo 1..3 al azar y dura 1: azar % (20d + 100) + 35 cuadros, 2: azar % 100 + 100 (y a veces deja
  // un platano: +0x62), 3: azar % (20d + 100) + 60; la velocidad es la de alejarse del jugador desviada hacia un lado un
  // azar % 10 x 0.1, a 4.5 x 0.4 por cuadro (+0xB54 = 0 -> 0.4). Perseguir (Piccolo, modo 4): hacia el jugador.
  // Los dos van a 4.5 (estado 9), algo mas que el andar del jugador (4.0): hay que hacer el scatto (35) para
  // alcanzar a Bubbles o huir de Piccolo, y la energia se acaba. El +0xCAC se aplica en IW (MgPly_Calc*Vel:
  // +0xB4 x 0x8B80 x 0xCAC x (+0xB54 ?: 0,4)) y es el multiplicador de carga del personaje: lo escribe
  // BattleScr_SetChargeMultiplierByLevel (EU 0x23DAF0) desde su nivel de carga (+0x1B5A) con valores 1,4 (1,16 en el modo
  // 3) / 1,0 / 0,6 (0,5) y los estados de combate lo ponen a 1,0 (SsjtransformationBumpAway 0x13..., TransitionScript);
  // los minijuegos no corren ese comando de script, asi que se queda en el 1,0 de la ficha.
  // DS181 (verificado en el decomp EU): los modos 1-3 de Bubbles usan MgPly_CalcRandomEvadeVel (EU 0x3170D0), cuyo
  // desvio lateral es `azar % 10 x 0,1` (0..0,9); el `azar % 7 x 0,1` es de MgPly_CalcRandomVelToWorkPos (EU 0x316EB0,
  // el modo 5 de otras misiones), no de Bubbles. El modo 2 llama ademas a CameraAdjustment(25.0) sobre +0x40 (y=0) y
  // suma la posicion del actor: deja el PUNTO DE TRABAJO a 25 u en la direccion de huida, sin altura; no es un salto
  // (y el consumo de ese punto por el task de la mision es [H]). Por eso aqui no hay salto y se queda el %10.
  void PrepararBubbles(const Datos& d) {
    if(bubbles_pose_.canal_voz)++bubbles_pose_.canal_voz->generacion;
    bubbles_pose_={};bubbles_pose_.datos=&d;bubbles_pose_.maquina=&bubbles_;
    bubbles_backend_ok_=false;bubbles_oraculo_ok_=true;bubbles_bloqueo_=nullptr;
    if(d.npc!=1 || !iwbubbles::Prueba(d.clave))return;
    if(d.bubbles_poses)bubbles_pose_.Iniciar();
    // Cada selftest usa SEIS backends nuevos: no altera caches del NPC, pose ni RNG.
    const char* oracle=std::getenv("TEST_GP2_BB_ORACULO");
    if(oracle && !std::strcmp(oracle,"1")) {
      std::array<iwbubbles::ResultadoGolden,6> resultados;
      uint32_t semilla_antes=0,semilla_despues=0;
      iwazar::Global().ConEstado([&](uint32_t& semilla){
        semilla_antes=semilla;resultados=iwbubbles::SelfTestWcf(d.bubbles_wcf);semilla_despues=semilla;
      });
      int pasadas=0;
      for(size_t i=0;i<resultados.size();++i){
        const auto& r=resultados[i];const auto& g=iwbubbles::kGoldens[i];pasadas+=r.pasa?1:0;
        REXLOG_INFO("dbz3 gp2 bubbles SELFTEST_CPP {} {} errP={} errN={} hit={} p={},{},{} normal={},{},{} mat={} tipo={} error={}",
          g.nombre,r.pasa?"PASS":"FAIL",r.error_punto,r.error_normal,r.actual.hit,r.actual.point.x,r.actual.point.y,r.actual.point.z,
          r.actual.contact.normal.x,r.actual.contact.normal.y,r.actual.contact.normal.z,r.actual.contact.material,r.actual.contact.type,r.error);
      }
      bubbles_oraculo_ok_=pasadas==6&&semilla_antes==semilla_despues;
      REXLOG_INFO("dbz3 gp2 bubbles SELFTEST_CPP total={}/6 rng_inalterado={} antes={:X} despues={:X} visual=false normal=false",
        pasadas,semilla_antes==semilla_despues,semilla_antes,semilla_despues);
      const auto clock=iwbubbles::SelfTestClock(std::getenv("TEST_GP2_BB_CLOCK_FIXTURE"));
      REXLOG_INFO("dbz3 gp2 bubbles CLOCK_CPP total={}/{} primerEE={} esperado={:08X} actual={:08X} error={} normal=false",
        clock.pasadas,clock.total,clock.primerEE,clock.esperado,clock.actual,clock.error);
      if(std::getenv("TEST_GP2_BB_CLOCK_FIXTURE"))bubbles_oraculo_ok_=bubbles_oraculo_ok_&&clock.total==99&&clock.pasadas==99&&clock.error.empty();
    }
    bubbles_backend_ok_=bubbles_backend_.Load(d.bubbles_wcf.data(),d.bubbles_wcf.size());
    if(d.bubbles_clips_ok){
      bubbles_.EvaluarPose(bubbles_pose_,0);bubbles_.ActualizarAncla();
      bubbles_.work.anterior90=bubbles_.anclas.actual640;
    }
    REXLOG_INFO("dbz3 gp2 bubbles backend={} nodos={} triangulos={} campo897={} error={} bloqueo=mezcla136A60_target42C_live45C_fase207180_fin9 normal=false",
      bubbles_backend_ok_,bubbles_backend_.Nodes(),bubbles_backend_.Triangles(),d.campo,bubbles_backend_.Error());
    ++bubbles_sesion_;
  }
  void IntroBubbles(ImGuiIO& io,const Datos& d){
    const int fase=g_bubbles_intro.load();
    if(fase==2 && bubbles_pose_.fase!=2){
      // EU318400 al terminar11D: escrituras actor+40/+44/+48, no el puntoITM.
      // Se conserva LIVE: la primeraD2 aun contiene la raiz de la presentacion.
      // PRUEBA (DBZ3_IWMINI_NPC_QUIETO=1): mismo encuadre que el punto real,
      // pero a 28 delante, para ver el modelo de cerca. No cambia nada normal.
      if (std::getenv("DBZ3_IWMINI_NPC_QUIETO")) {
        const float dx = d.meta.x - d.salida.x, dz = d.meta.z - d.salida.z, l = std::max(std::hypot(dx, dz), 1e-3f);
        const float c = std::cos(0.8f), s = std::sin(0.8f);
        bubbles_.p = {d.salida.x + (dx * c + dz * s) / l * 28.0f, 0.f,
                      d.salida.z + (-dx * s + dz * c) / l * 28.0f};
      } else {
        bubbles_.p = {-464.f, 0.f, -502.f};
      }
      bubbles_.raiz.posicion40=bubbles_.p;
      bubbles_.raiz.rotacion={};bubbles_.frente={0,0,1};npc_dir_={0,0,1};
      npc_p_={bubbles_.p.x,bubbles_.p.y,bubbles_.p.z};
    }
    bubbles_acum_+=dbz3::RelojDt(std::min(io.DeltaTime,.1f))*60.f;
    while(bubbles_acum_>=1.2f){
      bubbles_acum_-=1.2f;
      bubbles_pose_.Intro(fase);
      bubbles_.raiz.local50=bubbles_pose_.Raiz();bubbles_.ActualizarAncla();
      // READY sigue mostrando la relajacion de11D aD2 con el mismo LIVE.
      if(fase==2){bubbles_.cuadro=bubbles_pose_.cuadro;bubbles_.pose_cuadro=bubbles_pose_.cuadro;}
    }
  }
  // PlayHit_KiBlastsAudios3 (EU 0x22FD20) en juego: corta la voz del actor y pide la linea del perfil 16.
  void VozBubbles(const Datos& d,int linea){
    const auto* base=Base();
    const bool japones=base && REX_LOAD_U8(dbz3::GA(0x824BA110)+0x2C0C9)==0;
    const auto& pcm=d.bubbles_audio[japones?1:0].lineas[size_t(linea)];
    iwsonido::Opciones o;o.canal=bubbles_pose_.canal_voz;
    const auto h=pcm.muestras.empty()?0:iwadx::Oir(pcm,std::move(o));
    REXLOG_INFO("dbz3 gp2 bubbles voz tick={} estado={} linea={} handle={}",bubbles_.tick,bubbles_.estado,linea,h);
  }
  void CsvBubbles(const iwbubbles::Entrada& e) {
    if(!bubbles_csv_intentado_){
      bubbles_csv_intentado_=true;
      if(const char* path=std::getenv("TEST_GP2_BB_CSV");path&&*path){
        bubbles_csv_.f=std::fopen(path,"wx");
        if(bubbles_csv_.f)std::fputs("sesion,tick,estado,codigo,cuadro,pose_codigo,pose_cuadro,x,y,z,rootx,rooty,rootz,anclax,anclay,anclaz,f210,f214,f218,cc4,flags18e8_consumidos,tarea,sweep_hit,mat,tipo,nx,ny,nz,ray_hit,raymat,raytipo,altura,nueva5,c0x,c0y,c0z,jugadorx,jugadory,jugadorz,estado_jugador,distancia,frente,seed\n",bubbles_csv_.f);
        else REXLOG_WARN("dbz3 gp2 bubbles CSV no creado (existe o no escribible): {}",path);
      }
    }
    if(!bubbles_csv_.f)return;
    const auto& m=bubbles_;const auto& c=m.barrido.contact;
    std::fprintf(bubbles_csv_.f,"%llu,%llu,%d,%u,%.9g,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%d,%d,%u,%llu,%d,%u,%u,%.9g,%.9g,%.9g,%d,%u,%u,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%.9g,%.9g,%u\n",
      static_cast<unsigned long long>(bubbles_sesion_),static_cast<unsigned long long>(m.tick),m.estado,m.codigo,m.cuadro,m.pose_codigo,m.pose_cuadro,
      m.p.x,m.p.y,m.p.z,m.raiz.local50.x,m.raiz.local50.y,m.raiz.local50.z,m.anclas.actual640.x,m.anclas.actual640.y,m.anclas.actual640.z,
      int(m.flag210),int(m.flag214),int(m.flag218),int(m.cc4),m.flags_consumidos,static_cast<unsigned long long>(m.tarea_actual),int(m.barrido.hit),unsigned(c.material),unsigned(c.type),c.normal.x,c.normal.y,c.normal.z,
      int(m.rayo.hit),unsigned(m.rayo.contact.material),unsigned(m.rayo.contact.type),m.work.altura18d0,int(m.nueva_tarea5),e.camaraC0.x,e.camaraC0.y,e.camaraC0.z,
      e.jugador.x,e.jugador.y,e.jugador.z,e.estado_jugador,m.Distancia(e),m.Frente(e),unsigned(iwazar::Global().Estado()));
    std::fflush(bubbles_csv_.f);
  }
  // P1 (7/11): Piccolo del 0x7DD decidido por su CPU original (iw_mips.cpp Tick7DD: MinigameHideNSeekPiccolo_sCPU,
  // Char_EnterPoseStateByIndex y MgObj_UpdateTrajectory del ELF), una pasada por campo (1,2 cuadros PAL; 1,0 con el ELF
  // USA). Entradas del port: raiz y ancla del jugador (hueso 0), su estado de pose de IW (iwestado::DeAnim), el tipo de
  // suelo bajo el y el rayo de visibilidad contra el #WCF del campo. false (cvar apagada, sin campo, sin #WCF o fallo del
  // interprete) = el camino de antes. El invitado guarda el estado de Piccolo; tras un fallo se vuelve a empezar.
  bool Piccolo7DD(uint8_t* base, uint32_t mgr, const Datos& d, const V3& yo, float cuadros) {
    namespace pz = dbz3::iwmips::persecucion;
    if (!pz::g_tick || !piccolo_wcf_ok_ || !d.campo) return false;
    const float paso = dbz3::iwmips::freezer::g_ntsc ? 1.0f : 1.2f;
    V3 ancla = yo;
    PuntoCuerpo(base, mgr + kX0, 0, ancla);
    int tipo = dbz3::suelo_iw::TipoSuelo(yo.x, yo.z, yo.y + 2.0f);
    // Campo periodico (revision 7/11): el invitado envuelve a Piccolo en la rejilla (Node_WrapPositionInGrid). Al
    // invitado se le da la copia del jugador mas cercana a su Piccolo, y el port dibuja y mide con la copia de Piccolo
    // mas cercana al jugador.
    if (!piccolo_ini_) piccolo_g_ = npc_p_;
    float jx = yo.x, jz = yo.z;
    dbz3::suelo_iw::EnvolverReferencia(jx, jz, piccolo_g_.x, piccolo_g_.z);
    const float ox = jx - yo.x, oz = jz - yo.z;
    piccolo_acum_ += cuadros;
    while (piccolo_acum_ >= paso) {
      piccolo_acum_ -= paso;
      pz::Entrada in;
      pz::Salida out;
      in.inicio = !piccolo_ini_;
      in.pic0[0] = npc_p_.x, in.pic0[1] = npc_p_.y, in.pic0[2] = npc_p_.z;
      in.jug40[0] = yo.x + ox, in.jug40[1] = yo.y, in.jug40[2] = yo.z + oz;
      in.jug640[0] = ancla.x + ox, in.jug640[1] = ancla.y, in.jug640[2] = ancla.z + oz;
      in.jcbc = EstadoIWDeAnim(g_anim_estado);
      in.dif = d.dificultad;
      in.tipo25a = unsigned(tipo > 0 ? tipo : 1);
      in.t = ++piccolo_t_;
      in.host = &piccolo_wcf_;
      if (!d.piccolo_raiz.amm[3].empty() && !d.piccolo_raiz.bsk[0].empty()) in.raiz = &d.piccolo_raiz;
      in.rayo = [](void* h, const float* a, const float* b, unsigned m, float* p, pz::Contacto& k) {
        const auto r = static_cast<iwbubbles::wcf::Backend*>(h)->Ray({a[0], a[1], a[2]}, {b[0], b[1], b[2]}, m);
        if (!r.hit) return false;
        p[0] = r.point.x, p[1] = r.point.y, p[2] = r.point.z;
        k.normal[0] = r.contact.normal.x, k.normal[1] = r.contact.normal.y, k.normal[2] = r.contact.normal.z;
        k.d = r.contact.d, k.material = r.contact.material, k.tipo = r.contact.type;
        return true;
      };
      in.barrido = [](void* h, const float* a, const float* b, float radio, unsigned m, float* p, pz::Contacto& k) {
        const auto r = static_cast<iwbubbles::wcf::Backend*>(h)->Sweep({a[0], a[1], a[2]}, {b[0], b[1], b[2]}, radio, m);
        if (!r.hit) return false;
        p[0] = r.point.x, p[1] = r.point.y, p[2] = r.point.z;
        k.normal[0] = r.contact.normal.x, k.normal[1] = r.contact.normal.y, k.normal[2] = r.contact.normal.z;
        k.d = r.contact.d, k.material = r.contact.material, k.tipo = r.contact.type;
        return true;
      };
      bool ok = false;
      iwazar::Global().ConEstado([&](uint32_t& s) { ok = pz::g_tick(in, out, s); return ok; });
      if (!ok) {
        const bool habia = piccolo_ini_;
        piccolo_ini_ = false;   // el invitado puede quedar a medias: la siguiente pasada vuelve a empezar
        return habia;
      }
      piccolo_ini_ = true;
      piccolo_g_ = {out.p[0], out.p[1], out.p[2]};
      npc_p_ = piccolo_g_;
      dbz3::suelo_iw::EnvolverReferencia(npc_p_.x, npc_p_.z, yo.x, yo.z);
      if (out.frente[0] != 0 || out.frente[2] != 0) npc_dir_ = {out.frente[0], 0, out.frente[2]};
      npc_modo_ = out.estado;
      if (out.voz >= 0) REXLOG_INFO("dbz3 iwmini: 0x7DD voz de Piccolo linea {:X} (t={})", out.voz, in.t);
    }
    return true;
  }
  void MoverNpc(ImGuiIO& io, uint8_t* base, uint32_t mgr, const Datos& d, const V3& yo) {
    const float cuadros = dbz3::RelojDt(std::min(io.DeltaTime, 0.1f)) * 60.0f;
    // eIW6: en juego normal (sin DBZ3_TEST_GP2_BUBBLES) si falta un recurso del BBR se vuelve a la aproximacion
    // antigua en vez de dejar a Bubbles quieto (el minijuego no se podria acabar).
    const bool bb_recursos=d.bubbles_clips_ok && bubbles_backend_ok_ && d.campo && bubbles_oraculo_ok_;
    const bool bb_test=std::getenv("DBZ3_TEST_GP2_BUBBLES")!=nullptr;
    const bool bubbles_gp2=d.npc==1 && d.bubbles_clips_ok && iwbubbles::Prueba(d.clave) && (bb_recursos || bb_test);
    V3 ancla_jugador_bb=yo;
    const bool ancla_bb=!bubbles_gp2||PuntoCuerpo(base,mgr+kX0,0,ancla_jugador_bb);
    if(d.npc==1&&iwbubbles::Prueba(d.clave)&&(bb_recursos||bb_test)){
      const char* motivo=!d.bubbles_clips_ok?"clips_BBR47":!bubbles_backend_ok_?"WCF897":!d.campo?"campo897_no_instalado":!bubbles_oraculo_ok_?"SELFTEST_CPP_FAIL":
        (!g_ojo_ok.load()||g_sin_mando.load()||g_final.load()!=0)?"C0_seguimiento_no_publicado":!ancla_bb?"ancla_player0_no_publicada":nullptr;
      if(motivo){
        if(bubbles_bloqueo_!=motivo){bubbles_bloqueo_=motivo;REXLOG_WARN("dbz3 gp2 bubbles BLOQUEADO {} normal=false",motivo);}
        return; //No ejecutar la aproximacion antigua ni consumir RNG bajo una prueba incompleta.
      }
      bubbles_bloqueo_=nullptr;
    }
    if (bubbles_gp2) {
      // Jugar ya bloquea pausa/objetivo. Pasadas discretas PAL1.2, NO una por render.
      bubbles_acum_ += cuadros;
      while (bubbles_acum_ >= 1.2f) {
        bubbles_acum_ -= 1.2f;
        iwbubbles::Entrada entrada;
        entrada.jugador={ancla_jugador_bb.x,ancla_jugador_bb.y,ancla_jugador_bb.z};
        entrada.estado_jugador=EstadoIWDeAnim(g_anim_estado);
        entrada.dificultad=d.dificultad;
        entrada.paso=1.2f;entrada.factor_velocidad=d.k*entrada.paso;
        //297DE0 seguimiento: C0 se copia a ojo80 al FINAL de sus colisiones.
        //Solo gameplay sin AMC/entrada/final: no equiparar ojo y C0 durante clips.
        entrada.camaraC0={g_ojo_x.load(),g_ojo_y.load(),g_ojo_z.load()};entrada.c0_valida=true;
        bubbles_.Paso(entrada,iwazar::Global(),bubbles_pose_,bubbles_backend_);
        for (int i=0;i<bubbles_.n_cambios;++i) {
          const auto& c=bubbles_.cambios[size_t(i)];
          REXLOG_INFO("dbz3 gp2 bubbles tick={} fase=UPDATE {}->{} codigo={:X} cuadro={} flags210/214/218={}/{}/{} pos={},{},{} raiz={},{},{} raiz_antes={},{},{} pose={:X}/{} flags18E8_consumidos={} tarea={} CONTACT_prev={}/{} normal={},{},{}",
            bubbles_.tick,c.desde,c.hasta,c.codigo,c.cuadro,c.f210,c.f214,c.f218,c.p.x,c.p.y,c.p.z,c.raiz.x,c.raiz.y,c.raiz.z,c.raiz_antes.x,c.raiz_antes.y,c.raiz_antes.z,c.pose_codigo,c.pose_cuadro,c.flags_raiz,c.tarea,c.contacto.material,c.contacto.type,c.contacto.normal.x,c.contacto.normal.y,c.contacto.normal.z);
        }
        if(bubbles_.n_cambios||bubbles_.nueva_tarea5){
          const auto& m=bubbles_;const auto& c=m.barrido.contact;
          REXLOG_INFO("dbz3 gp2 bubbles tick={} fase=TICK estado={} pos={},{},{} raiz={},{},{} flags18E8_consumidos={} tarea={} CONTACT hit={} mat={} tipo={} normal={},{},{} nodo={:X} plano={:X} grupo={:X} triangulo={:X} nueva5={} ray_hit={} suelo={} C0={},{},{} fuente=follow_ojo80",
            m.tick,m.estado,m.p.x,m.p.y,m.p.z,m.raiz.local50.x,m.raiz.local50.y,m.raiz.local50.z,m.flags_consumidos,m.tarea_actual,m.barrido.hit,c.material,c.type,c.normal.x,c.normal.y,c.normal.z,c.node,c.plane,c.group,c.triangle,m.nueva_tarea5,m.rayo.hit,m.work.altura18d0,entrada.camaraC0.x,entrada.camaraC0.y,entrada.camaraC0.z);
          if(m.nueva_tarea5)REXLOG_INFO("dbz3 gp2 bubbles tarea5 normal238={},{},{} mat={} tipo={} (antes de Ray)",m.contacto_tarea5.normal.x,m.contacto_tarea5.normal.y,m.contacto_tarea5.normal.z,m.contacto_tarea5.material,m.contacto_tarea5.type);
        }
        CsvBubbles(entrada);
        if(bubbles_.voz>0)VozBubbles(d,bubbles_.voz);
        for (int i=0;i<bubbles_.n_eventos;++i) {
          const auto& e=bubbles_.eventos[size_t(i)];
          REXLOG_INFO("dbz3 gp2 bubbles evento tick={} estado={} codigo={:X} cuadro={} tipo={} pos={},{},{}",
                      bubbles_.tick,bubbles_.estado,bubbles_.codigo,bubbles_.cuadro,e.tipo,e.p.x,e.p.y,e.p.z);
          if (e.tipo==0 || e.tipo==1 || e.tipo==2) {
            platanos_.push_back({e.p.x,e.p.y,e.p.z});
            REXLOG_INFO("dbz3 iwmini: platano tirado (evento) en ({:.1f},{:.1f},{:.1f})", e.p.x,e.p.y,e.p.z);
          }
          // Extras3/4 solo trazados: falta su trayectoria, NO platanos inmediatos.
        }
      }
      npc_p_={bubbles_.p.x,bubbles_.p.y,bubbles_.p.z};
      npc_dir_={bubbles_.frente.x,bubbles_.frente.y,bubbles_.frente.z};
      npc_modo_=bubbles_.modo;
    } else if (d.npc == 2 && Piccolo7DD(base, mgr, d, yo, cuadros)) {
      // P1: la CPU original movio a Piccolo (npc_p_, npc_dir_, npc_modo_ = estado de pose de IW)
    } else {
    auto azar = [this]() {
      semilla_ = semilla_ * 1103515245u + 12345u;
      return int((semilla_ >> 16) & 0x7FFF);
    };
    float dx = npc_p_.x - yo.x, dz = npc_p_.z - yo.z, l = std::max(std::hypot(dx, dz), 1e-3f);
    dx /= l;
    dz /= l;
    float vel = 0;
    if (d.npc == 1) {
      npc_t_ -= cuadros;
      if (npc_t_ <= 0) {
        npc_modo_ = azar() % 3 + 1;
        const int dif = d.dificultad;
        npc_t_ = float(npc_modo_ == 2 ? azar() % 100 + 100 : azar() % (dif * 20 + 100) + (npc_modo_ == 1 ? 35 : 60));
        const float dev = float(azar() % 10) * 0.1f, lado = azar() & 1 ? 1.0f : -1.0f;
        const float px = -dz * lado, pz = dx * lado;   // perpendicular
        npc_dir_ = {dx + (px - dx) * dev, 0, dz + (pz - dz) * dev};
        const float m = std::max(std::hypot(npc_dir_.x, npc_dir_.z), 1e-3f);
        npc_dir_ = {npc_dir_.x / m, 0, npc_dir_.z / m};
        if (npc_modo_ == 2 && (azar() & 1)) {   // el platano que tira
          platanos_.push_back(npc_p_);
          REXLOG_INFO("dbz3 iwmini: platano tirado en ({:.1f},{:.1f},{:.1f})", npc_p_.x,npc_p_.y,npc_p_.z);
        }
      }
      vel = VelocidadCampoAp(mgr+kX0+kXPaso,4.5f,d.k);
    } else {
      npc_modo_ = 4;   // Piccolo (luchador 1 del 0xE): el mismo estado 9 hacia el jugador
      npc_dir_ = {-dx, 0, -dz};
      vel = VelocidadCampoAp(mgr+kX0+kXPaso,4.5f,d.k);
    }
    static const bool quieto = std::getenv("DBZ3_IWMINI_NPC_QUIETO") != nullptr;   // prueba: verle de cerca
    if (quieto) vel = 0;
    npc_p_ = {npc_p_.x + npc_dir_.x * vel * cuadros, npc_p_.y, npc_p_.z + npc_dir_.z * vel * cuadros};
    // Bubbles va por el suelo; Piccolo vuela a la altura medida en el original (0x7DD: 124, oscila 115..146)
    if (d.campo) npc_p_.y = d.npc == 2 ? 124.0f : dbz3::suelo_iw::Altura(npc_p_.x, npc_p_.z, npc_p_.y);
    const float r = std::hypot(npc_p_.x, npc_p_.z);
    if (d.campo && (std::fabs(npc_p_.x) > 1476.0f || std::fabs(npc_p_.z) > 1476.0f)) {   // borde del campo de R8
      npc_p_ = {std::clamp(npc_p_.x, -1476.0f, 1476.0f), npc_p_.y, std::clamp(npc_p_.z, -1476.0f, 1476.0f)};
      npc_t_ = 0;
    } else if (!d.campo && r > kRadio + 15.0f) {   // no sale del campo (Char_WrapMoveInGrid): vuelve hacia dentro
      npc_p_ = {npc_p_.x * (kRadio + 15.0f) / r, 0, npc_p_.z * (kRadio + 15.0f) / r};
      npc_t_ = 0;
    }
    }
    g_npc_x = npc_p_.x;   // Piccolo de B3 (Fotograma lo pone ahi)
    g_npc_y = npc_p_.y;
    g_npc_z = npc_p_.z;
    // Platanos de Bubbles: al pisarlos el 888 (modulo 1) pone work+0x1D0 = 1 (0x320... del decomp EU). El siguiente
    // Char_GetNextActionCode (0x2A9440) devuelve 0x1E y Minigame_State1E_Enter (0x29E9A0) reproduce la animacion 0xCA
    // (DATA_CMN 915: bloque 0xCA -> anim 44, 60 cuadros), pone la velocidad 1.8 (0x3FE66666), gasta 25 de energia y
    // limpia el flag: el resbalon dura la animacion entera, 60 cuadros = 1,0 s (no era una suposicion).
    for (size_t i = 0; i < platanos_.size(); ++i) {
      if (DistH(yo, platanos_[i]) < 6.0f) {
        platanos_.erase(platanos_.begin() + long(i));
        resbalon_ = 1.0f;   // 60 cuadros de la animacion 0xCA (MoverNpc los descuenta a 60/s)
        g_energia = std::max(0.0f, g_energia.load() - 25.0f);
        REXLOG_INFO("dbz3 iwmini: platano: resbala (60 cuadros, -25 de energia; queda {:.0f})", g_energia.load());
        break;
      }
    }
    if (NowMs() - npc_log_ > 2000) {
      npc_log_ = NowMs();
      REXLOG_INFO("dbz3 iwmini: npc en ({:.1f}, {:.1f}) modo {} a {:.1f} del jugador", npc_p_.x, npc_p_.z, npc_modo_,
                  DistH(yo, npc_p_));
    }
    resbalon_ = std::max(0.0f, resbalon_ - cuadros / 60.0f);
    g_resbalando = resbalon_ > 0;
    if (DistH(yo, npc_p_) < (d.npc == 1 ? 15.0f : 14.0f)) {
      REXLOG_INFO("dbz3 iwmini: {}", d.npc == 1 ? "Bubbles atrapado" : "Piccolo te alcanza");
      Acabar(base, mgr, d, true);
    }
  }

  Camara Cam(const ImGuiIO& io, uint8_t* base, uint32_t) const {
    Camara cam = ps2::CamaraCombate(base, io.DisplaySize);   // R15: la misma, con la profundidad del juego (Camara::Prof)
    // DS177: la escena 3D cuelga de la camara con la que el juego DIBUJA este cuadro, no de mgr+0x60/+0x1C4. El gancho
    // de sub_820F4300 (iw_mini_vuelo.cpp) aplica la camara de IW a la vista (mgr+0x60, la que usa sub_82087500) y
    // publica la foto: matriz de vista + fov de la struct activa (sub_82082A08 lo lee de ahi; mgr+0x1C4 solo lo leen
    // los overlays). Asi la aureola, platanos, monedas, polvo y geiseres caen exactamente donde B3 pinta a Goku y el
    // mundo. Sin foto (EU o antes del primer dibujo) queda CamaraCombate, como antes.
    float m[16], fov = 0.0f;
    if (CamaraDelRenderIw(m, &fov)) {
      for (int i = 0; i < 16; ++i) cam.m[i] = m[i];
      if (fov > 0.1f && fov < 3.0f) cam.fov = fov;
    }
    // La misma proyeccion que el cuerpo: s_PLY_VIB_CTRL es vibracion del
    // mando. No introducir una camara 2D distinta para terreno y objetos.
    return cam;
  }

  void Dibujar(ImGuiIO& io, uint8_t* base, uint32_t mgr, const Datos& d) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Camara cam = Cam(io, base, mgr);
    const float t = float(dbz3::RelojT(ImGui::GetTime()));
    struct Cosa {
      float k;
      size_t i;   // objetos; d.objetos.size() = la meta
      int tipo;   // 0 objeto, 1 nave fija (0x7D9), 2 meta (modelo o anillo del disparador)
    };
    std::vector<Cosa> cosas;
    ImVec2 o;
    float k;
    for (size_t i = 0; i < d.objetos.size(); ++i)
      if (!cogido_[i] && cam.P(d.objetos[i].p, o, k)) cosas.push_back({k, i, 0});
    // La nave de Freezer (0x7D9) es una pieza del campo: se dibuja en su sitio de world-space desde el principio.
    if (d.meta_fija && d.m_meta.malla && cam.P(d.meta_modelo, o, k)) cosas.push_back({k, d.objetos.size(), 1});
    // alcanzada (fin_), la meta ya no se pinta: con el jugador quieto dentro (pose final de IW) la camara quedaba dentro
    // de su modelo y la pantalla entera salia azul (la "pantalla azul" de R16)
    if (d.npc != 1 && aros_ >= d.num_a && !fin_ && cam.P(d.llegada, o, k)) cosas.push_back({k, d.objetos.size(), 2});
    std::sort(cosas.begin(), cosas.end(), [](const Cosa& a, const Cosa& b) { return a.k < b.k; });
    // con los modelos 3D de IW: todos los triangulos de lejos a cerca; sin ellos, los dibujos planos de antes
    // EU 0x320800 MgEffect_SpawnSlots: rellena los handles de efecto del campo (tabla de recursos AFS, offsets
    // 0x20/0x24/0x2c/0x34/0x44/0x88) con Aura_AuraDash_ObjectsCountsCollection(mask 0x4000010): son los efectos de
    // ambiente de cada minijuego (y el propio del tipo 10/16). Aqui los efectos son iwame (#AME) y los geiseres del
    // 900, montados en Cargar/Dibujar: `motor`.
    escena_.Empezar();
    // 0x7D9: geiseres de lava del campo 900. Las seis torres LAVAFIRE_TOWER1..6 del #MIF 900/33 se pintan en su sitio
    // de world-space con el modelo 900/14 y su animacion 900/26 (bucle de 60 Hz, cada torre con su desfase).
    // DBZ3_IW_SIN_GEISER=1 los quita para comparar (solo pruebas).
    static const bool con_geiser = std::getenv("DBZ3_IW_SIN_GEISER") == nullptr;
    if (con_geiser && d.indice == 10 && d.m_geiser.malla) {
      // D34: la camara no entra en el volumen de un geiser. De las dos salidas que da el encargo (apartar la camara o
      // no dibujar el efecto con el ojo dentro) se toma la segunda: la torre es un modelo animado, no una cara del #WCF,
      // y meterla en la colision de la camara falsearia el resto de la columna. Se salta la torre cuyo cilindro (radio y
      // alto de su malla posada, +6 de margen) contiene el ojo; antes se pintaba encima y las fotos c049/c050 salian con
      // la camara dentro de la lava.
      const bool ojo_hay = g_ojo_ok.load();
      const V3 ojo{g_ojo_x.load(), g_ojo_y.load(), g_ojo_z.load()};
      auto dentro = [&](const Malla& m, const V3& g) {
        if (!ojo_hay || !m) return false;
        float r = 0, y0 = 1e30f, y1 = -1e30f;
        for (const auto& tr : m.tris)
          for (const V3& q : tr.p) {
            r = std::max(r, std::hypot(q.x, q.z));
            y0 = std::min(y0, q.y), y1 = std::max(y1, q.y);
          }
        const float dx = ojo.x - g.x, dz = ojo.z - g.z;
        return std::hypot(dx, dz) < r + 6.0f && ojo.y > g.y + y0 - 8.0f && ojo.y < g.y + y1 + 8.0f;
      };
      for (size_t i = 0; i < d.geiseres.size(); ++i) {
        const V3& g = d.geiseres[i];
        if (!d.anim_geiser || !d.m_geiser.esq) {
          if (!dentro(d.m_geiser.malla, g)) Poner3D(cam, d.m_geiser, tex_geiser_, g, 0.0f, 1.0f);
          continue;
        }
        Posar(*d.m_geiser.esq, d.anim_geiser, t + 0.7f * float(i), geiser_pose_.malla);
        AplicarDerivaUv(geiser_pose_.malla, d.uv_geiser_dv, t * 60.0f);
        if (!dentro(geiser_pose_.malla, g)) Poner3D(cam, geiser_pose_, tex_geiser_, g, 0.0f, 1.0f);
      }
    }
    for (const Roca& c : rocas_) Poner3D(cam, d.m_roca[c.tam], tex_roca_, c.p, c.giro, 1.0f);
    for (const Cosa& c : cosas) {
      if (c.tipo) {
        if (c.tipo == 1) {
          Poner3D(cam, d.m_meta, tex_meta_, d.meta_modelo, 0.0f, 1.0f);
        } else if (d.meta_fija || !d.m_meta.malla) {
          DibujarMeta(dl, cam, d.llegada, d.radio_llegada, t);   // guia 2D en la meta de fin (+0x1630)
        } else {
          Poner3D(cam, d.m_meta, tex_meta_, d.llegada, 0.0f, 1.0f);
        }
        continue;
      }
      const Objeto& ob = d.objetos[c.i];
      const Modelo& m = ob.grupo == 0   ? d.m_aro
                        : ob.grupo == 1 ? d.m_moneda
                        : ob.grupo == 3 ? d.m_marca
                        : ob.grupo == 4 ? d.m_d
                                        : d.m_bebida;
      const std::vector<Tex>& tx = ob.grupo == 0   ? tex_aro_
                                   : ob.grupo == 1 ? tex_moneda_
                                   : ob.grupo == 3 ? tex_marca_
                                   : ob.grupo == 4 ? tex_d_
                                                   : tex_bebida_;
      // giro de su #AMM (todos a la vez, como el cuadro comun del juego: MgDraw_* de 0x320F30 en el decomp EU)
      const float giro = m.giro.cuadros ? m.giro.En(t) : t * 2.0f;
      if (m.malla) Poner3D(cam, m, tx, ob.p, giro, 1.0f);
      else if (ob.grupo == 0) DibujarAro(dl, cam, ob.p, t);
      else DibujarMoneda(dl, cam, ob, t);
    }
    if (d.npc && d.m_npc.malla && !(d.npc==1 && iwbubbles::Prueba(d.clave) && !d.bubbles_clips_ok)) {   //modeloBBR propio; nunca fallback E2 en prueba sin banco.
      const bool prueba_bb=d.npc==1 && d.bubbles_clips_ok && iwbubbles::Prueba(d.clave);
      const bool anima = d.m_npc.esq && (prueba_bb?bubbles_pose_.cuerpo.iniciada:bool(d.m_npc.anim));
      if(anima){
        if(prueba_bb)bubbles_pose_.Posar(npc_pose_.malla);
        else Posar(*d.m_npc.esq,d.m_npc.anim,t,npc_pose_.malla);
      }
      V3 dibujo=npc_p_;
      if(prueba_bb&&anima){
        const auto& c=d.m_npc.esq->centro;
        const bool intro=g_bubbles_intro.load()<2;
        const auto origen=iwbubbles::noche::OrigenDibujoAnimado(intro?iwbubbles::V{}:bubbles_.p,
          intro?iwbubbles::noche::R3{}:bubbles_.raiz.rotacion,{c.x,0,c.z});
        dibujo={origen.x,origen.y,origen.z};
      }
      Poner3D(cam, anima ? npc_pose_ : d.m_npc, tex_npc_, dibujo,
        prueba_bb&&g_bubbles_intro.load()<2?0.f:std::atan2(npc_dir_.x,npc_dir_.z),1.f);
      if(prueba_bb&&anima)bubbles_pose_.Medir(npc_pose_.malla,dibujo,
        g_bubbles_intro.load()<2?0.f:std::atan2(npc_dir_.x,npc_dir_.z),cam);
    }
    for (const V3& pl : platanos_)
      if (d.m_platano.malla) Poner3D(cam, d.m_platano, tex_platano_, pl, 0.0f, 1.0f);
    if (halo_ok_) {
      // DS169: el anillo va colgado de GOK_RING (hijo de GOK_HEAD en 409/0).
      // DS172: se ancla al HUESO DE LA CABEZA, no a X+0. El medido por el coordinador (c_fin: flotaba ~120 px sobre
      // el pelo; b_npc: a la altura del pecho) salia de sumar 20,08 (el anillo sobre los pies, o sea sobre la PUNTA
      // DEL PELO) a X+0 mas la altura de la pista WAIST de IW (r[1]-10,265), que no es la de la pose que se dibuja:
      // da -8 u al correr y +4 en la pose 0xF0.
      // DS174: la base es el punto de cuerpo de la CABEZA (X+1616+48, la esfera que el juego recalcula del esqueleto).
      // DS175: el "arriba" del hueso de EjeCabeza NO VALE: en la corrida del 27/10 19:00 devolvio huesos distintos cada
      // muestra (12, 6, 13, 11, 9; el de la cabeza casi nunca) con la fila Y tumbada o al reves (y -0,50), y cuando
      // pasaba el filtro el +4,189 se iba 2,8-4,1 u AL LADO (el aro junto al puño/cadera). Los datos de esa corrida
      // dicen cual es la cabeza: el punto 3 va con el hueso GOK_HEAD (pose 0xF0: hueso 0 dibujado a y 9,89 + GOK_HEAD
      // 5,442 = 15,33 y el punto 3 en 15,2; encima del cuello, punto 2, por 0,4-0,7). Como el original cuelga el anillo
      // de GOK_HEAD por su +Y (0, 4,189, 0) y el muñeco se dibuja SIN inclinar el mundo, se usa la VERTICAL DEL MUNDO
      // desde el punto 3 (verificado: en Goku B3 el hueso de la cabeza esta a 5,442 del origen del modelo y la punta
      // del pelo a 9,74, o sea 4,30 encima: 4,189 deja el aro en la punta del pelo, como en el original).
      V3 cab{}, pos{}, hueso{}, eje{};
      // DBZ3_IWMINI_HALO_ALTO=<u> lo ajusta sin recompilar (prueba de encuadre).
      static const float sobre_cabeza = [] {
        const char* e = std::getenv("DBZ3_IWMINI_HALO_ALTO");
        return e ? float(std::atof(e)) : kHaloSobreCabeza;
      }();
      bool anclada = false, con_hueso = false;
      const bool con_cabeza = PuntoCuerpo(base, mgr + kX0, 3, cab);
      const float esc = EscalaLuchador(base, mgr + kX0);
      // DS183: el aro cuelga de GOK_HEAD, como GOK_RING (hijo de GOK_HEAD con (0,4,189,0) en 409/0). El indice del
      // hueso de la cabeza es el del anclaje 3 de la tabla del juego (X+2956, la MISMA que usa sub_820F9548 para el
      // punto de cuerpo 3; en Goku HD = 18 = GOK_HEAD). De su matriz se toman la POSICION y la fila 1 (+Y local); con
      // la cabeza inclinada el +4,189 va por ese eje y el aro queda sobre la coronilla, no delante del pecho.
      const uint32_t anc = REX_LOAD_U32(mgr + kX0 + 2956);
      const int idx_cab = anc >= 0x40000000u && anc < 0xA0000000u ? int(int16_t(REX_LOAD_U16(anc + 6))) : -1;
      const bool hay_hueso = HuesoMundoEje(base, mgr + kX0, idx_cab, hueso, eje);
      if (hay_hueso) {
        const float le = std::sqrt(eje.x * eje.x + eje.y * eje.y + eje.z * eje.z);
        if (le > 1e-3f && std::isfinite(le)) {
          eje = {eje.x / le, eje.y / le, eje.z / le};
          // Con un modelo portado la tabla de anclajes puede apuntar a otro hueso: si el "de la cabeza" cae lejos de
          // la esfera de la cabeza, no se usa (se vuelve a la vertical del punto 3, que al menos no sale disparada).
          const float dx = hueso.x - cab.x, dy = hueso.y - cab.y, dz = hueso.z - cab.z;
          const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
          con_hueso = !con_cabeza || d < 8.0f * std::max(1.0f, esc);
          if (con_hueso) {
            pos = {hueso.x + eje.x * sobre_cabeza * esc, hueso.y + eje.y * sobre_cabeza * esc,
                   hueso.z + eje.z * sobre_cabeza * esc};
            anclada = true;
          }
        }
      }
      if (!anclada && con_cabeza) {
        pos = {cab.x, cab.y + sobre_cabeza * esc, cab.z};
        anclada = true;
      }
      if (!anclada) {
        const V3 yo = Leer(base, mgr + kX0);
        float r[3] = {0, 0, 0};
        const bool hay = dbz3::anim::RaizActual(base, mgr + kX0, r);
        const float sube = hay ? r[1] - 10.265f : 0.0f;
        pos = {yo.x, yo.y + std::max(sube, -halo_alto_ + 0.1f) + halo_alto_, yo.z};
      }
      if (static const bool hlog = std::getenv("DBZ3_TEST_IWMINI_HALOLOG"); hlog && ImGui::GetFrameCount() % 60 == 0) {
        // DS175: proyeccion a pantalla de todo lo que hace falta para saber si el aro se dibuja y DONDE cae, con la
        // MISMA camara que pinta la escena (Escena::Poner). `!` = fuera de la imagen; `tierra` = el terreno del campo
        // de IW corta la visual ojo->punto (0 si no, - sin campo; desde DS179 con `@(x,y,z)` el punto de corte y el
        // ojo de verdad); `cuerpo` = una esfera del luchador tapa el centro del aro con la MISMA regla que
        // Escena::Tapar (circulo en pantalla + dentro de la esfera en 3D); `!`/`-` = sin dato.
        const V3 x64 = Leer(base, mgr + kX0);
        const V3 x48{LoadF(base, mgr + kX0 + 48), LoadF(base, mgr + kX0 + 52), LoadF(base, mgr + kX0 + 56)};
        V3 p0{};
        const bool hay0 = PuntoCuerpo(base, mgr + kX0, 0, p0);
        V3 b0{}, ancla{};
        const bool hay_b0 = HuesoMundo(base, mgr + kX0, 0, b0);
        const uint32_t anc = REX_LOAD_U32(mgr + kX0 + 2956);
        const bool anc_ok = anc >= 0x40000000u && anc < 0xA0000000u;
        const int idx_anc3 = anc_ok ? int(int16_t(REX_LOAD_U16(anc + 6))) : -1;
        const bool hay_anc = HuesoMundo(base, mgr + kX0, idx_anc3, ancla);
        // DS179: ojo de la camara DIBUJADA = -R^T·t de la vista de este cuadro. La version de DS175 mezclaba fila y
        // columna (-R·t): con la camara girada (yaw/roll) el ojo salia al OTRO lado del luchador y el rayo
        // ojo->aureola cruzaba medio campo -> `tierra 1` en plena carrera aunque el camino sea llano. Sin giro
        // (mirando a +z) las dos formulas coinciden, y por eso paso desapercibido en el final (c_fin).
        // Camara::P usa xv = m[0]*x + m[4]*y + m[8]*z + m[12], o sea V[i][j] = m[j*4+i] (fila i = m[i],m[i+4],
        // m[i+8]); la inversa rigida es e_i = -(columna i de R)·t:
        const V3 ojo{-(cam.m[0] * cam.m[12] + cam.m[1] * cam.m[13] + cam.m[2] * cam.m[14]),
                     -(cam.m[4] * cam.m[12] + cam.m[5] * cam.m[13] + cam.m[6] * cam.m[14]),
                     -(cam.m[8] * cam.m[12] + cam.m[9] * cam.m[13] + cam.m[10] * cam.m[14])};
        auto tierra = [&](const V3& q, V3* corte = nullptr) {
          if (!d.campo) return -1;
          // DS179: que el ojo sea un ojo. Si la camara queda por debajo de la malla DIBUJADA del campo (pegada a una
          // cuesta o enterrada tras un salto del piloto automatico) el segmento nace dentro del terreno y corta con lo
          // primero que encuentra: eso no es un tapado. En ese caso no se cuenta.
          const float suelo_ojo = dbz3::suelo_iw::AlturaMalla(ojo.x, ojo.z, -1e30f);
          if (suelo_ojo > -1e29f && ojo.y < suelo_ojo + 0.5f) return 0;
          const float a[3] = {ojo.x, ojo.y, ojo.z}, b[3] = {q.x, q.y, q.z};
          float c[3];
          if (!dbz3::suelo_iw::Rayo(a, b, 7, c)) return 0;
          // y el corte tiene que estar de verdad entre los dos: pegado al propio objeto (< 0,5 u) no lo tapa.
          const float dx = c[0] - b[0], dy = c[1] - b[1], dz = c[2] - b[2];
          if (dx * dx + dy * dy + dz * dz <= 0.25f) return 0;
          if (corte) *corte = {c[0], c[1], c[2]};   // DS179: donde corta (para el registro)
          return 1;
        };
        auto cuerpo = [&](const V3& q) {
          ImVec2 o;
          float k = 0;
          if (!cam.P(q, o, k)) return false;
          const uint32_t rt = REX_LOAD_U32(mgr + kX0 + 2996);
          for (uint32_t i = 0; i < 22; ++i) {
            const uint32_t a = mgr + kX0 + 1616 + 16 * i;
            const V3 c{LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)};
            const float r = rt ? LoadF(base, rt + 24 + 4 * i) : 0.0f;
            ImVec2 oc;
            float kc = 0;
            if (r <= 0.0f || r > 20.0f || !cam.P(c, oc, kc)) continue;
            const float rr = r * 1.3f * kc, dx = o.x - oc.x, dy = o.y - oc.y;
            // DS179: la MISMA regla que Escena::Tapar (circulo en pantalla + dentro de la esfera en 3D)
            const float d3x = q.x - c.x, d3y = q.y - c.y, d3z = q.z - c.z;
            if (k < kc && dx * dx + dy * dy < rr * rr && d3x * d3x + d3y * d3y + d3z * d3z < r * r)
              return true;
          }
          return false;
        };
        auto px = [&](const V3& q) {
          const Proy p = Proyectar(cam, q);
          return p.ok ? fmt::format("({:.0f},{:.0f}) k {:.0f}{}", p.x, p.y, p.k, p.dentro ? "" : " !")
                      : std::string("(-)");
        };
        std::string s, rs;
        for (int i = 0; i < 8; ++i) {
          V3 q{};
          const bool ok = PuntoCuerpo(base, mgr + kX0, i, q);
          s += fmt::format(" {}{}:({:.1f},{:.1f},{:.1f})", i, ok ? "" : "!", q.x, q.y, q.z);
          const uint32_t rt = REX_LOAD_U32(mgr + kX0 + 2996);
          rs += fmt::format(" {:.1f}", rt ? LoadF(base, rt + 24 + 4 * uint32_t(i)) : 0.0f);
        }
        const Proy ph = Proyectar(cam, pos), pc = Proyectar(cam, cab);
        // DS179: donde corta el rayo de la aureola (si corta): sin esto, un `tierra 1` no dice si el corte esta en
        // una loma lejana, en una copa (material 6) o al lado del ojo.
        V3 corte{};
        const int th = tierra(pos, &corte);
        // una sola linea: cada bloque con sus datos (ver la cabecera del bloque para leerla)
        std::string linea = fmt::format(
            "halo ({:.1f},{:.1f},{:.1f}) px {} dentro {} tierra {}{} cuerpo {}", pos.x, pos.y, pos.z, px(pos),
            ph.dentro ? 1 : 0, th,
            th == 1 ? fmt::format("@({:.1f},{:.1f},{:.1f})", corte.x, corte.y, corte.z) : std::string(),
            cuerpo(pos) ? 1 : 0);
        linea += fmt::format(" | cabeza ({:.1f},{:.1f},{:.1f}) px {} dentro {} tierra {}", cab.x, cab.y, cab.z, px(cab),
                             pc.dentro ? 1 : 0, tierra(cab));
        linea += fmt::format(" | X64 ({:.1f},{:.1f},{:.1f}) px {}", x64.x, x64.y, x64.z, px(x64));
        linea += fmt::format(" | X48 ({:.1f},{:.1f},{:.1f}) px {}", x48.x, x48.y, x48.z, px(x48));
        linea += fmt::format(" | p0 ({:.1f},{:.1f},{:.1f}) px {}", p0.x, p0.y, p0.z, hay0 ? px(p0) : std::string("(-)"));
        linea += hay_b0 ? fmt::format(" | b0 ({:.1f},{:.1f},{:.1f}) px {}", b0.x, b0.y, b0.z, px(b0))
                        : std::string(" | b0 (-)");
        linea += fmt::format(" | ancla3 {} px {} d {:.2f}", idx_anc3, hay_anc ? px(ancla) : std::string("(-)"),
                             hay_anc ? std::sqrt((ancla.x - cab.x) * (ancla.x - cab.x) +
                                                 (ancla.y - cab.y) * (ancla.y - cab.y) +
                                                 (ancla.z - cab.z) * (ancla.z - cab.z))
                                     : -1.0f);
        // DS183: el hueso con el que se ancla el aro (el del anclaje 3 = GOK_HEAD), su +Y local en mundo y el angulo
        // con la vertical (0 = cabeza erguida; crece al inclinarla). `d` = separacion hueso<->esfera de la cabeza.
        const float dh = std::sqrt((hueso.x - cab.x) * (hueso.x - cab.x) + (hueso.y - cab.y) * (hueso.y - cab.y) +
                                   (hueso.z - cab.z) * (hueso.z - cab.z));
        const float ang = hay_hueso ? std::acos(std::clamp(eje.y, -1.0f, 1.0f)) * 57.29578f : -1.0f;
        linea += fmt::format(" | GOK_HEAD {} px {} eje ({:.2f},{:.2f},{:.2f}) ang {:.0f} d {:.2f} anclada {}",
                             idx_cab, hay_hueso ? px(hueso) : std::string("(-)"), eje.x, eje.y, eje.z, ang,
                             hay_hueso && con_cabeza ? dh : -1.0f, con_hueso ? 1 : 0);
        linea += fmt::format(" | esc {:.2f} alto {:.2f} | puntos{} radios{}", esc, sobre_cabeza, s, rs);
        // DS177: con que camara se proyecta: `render` = la foto del cuadro que dibuja el juego (vista + fov de la
        // struct activa), `combate` = respaldo CamaraCombate (mgr+0x60/+0x1C4). Si el aro y el pelo no cuadran, este
        // campo dice cual se uso.
        float rm[16], rfov = 0.0f;
        const bool foto = CamaraDelRenderIw(rm, &rfov);
        linea += fmt::format(" | cam {} fov {:.3f} mgr {:.3f}", foto ? "render" : "combate", cam.fov,
                             LoadF(base, mgr + kFov));
        // DS179: el ojo con el que se lanzan los rayos de `tierra`/`cuerpo` (inversa corregida). Si el aro sale
        // `tierra 1` con el camino llano, aqui se ve de donde sale el rayo.
        linea += fmt::format(" | ojo ({:.1f},{:.1f},{:.1f})", ojo.x, ojo.y, ojo.z);
        REXLOG_INFO("dbz3 iwmini: {}", linea);
      }
      // DS179: el aro es parte del luchador (GOK_RING cuelga de GOK_HEAD): el propio luchador no lo puede tapar. Con
      // la prueba de esferas sola, la de la cabeza (radio 3,2 x 1,3 = 4,16) llegaba justo al aro (4,19) y se borraba
      // entero o a trozos segun el cuadro; por eso se veia en c_fin (camara cerca) y no en la carrera.
      escena_.Tapable(false);
      escena_.Poner(cam, halo_.malla, tex_halo_, pos, 0.0f, 1.0f);
      escena_.Tapable(true);
    }
    Polvo(dl, cam, d);
    Efectos(dl, cam, d);
    PasosAp(dl,cam,d);
    ImpactosAp(dl,cam,d);
    if(d.campo)AuraDeCuerpo(cam,base,mgr+kX0);
    Tapas(cam, base, mgr + kX0);
    Pintar3D(dl);
    // DS179: si la aureola no aparece pero su `px` esta en pantalla, hay que saber si `Escena` la tira (esferas del
    // luchador o tope de la lista de dibujo). Esta linea da el recuento REAL del cuadro: puestos / pintados / tapados
    // / fuera por el tope. `tapados` = los que quita Escena::Tapar (esferas 3D) y `fuera` = los que sobran de la lista.
    if (static const bool hlog = std::getenv("DBZ3_TEST_IWMINI_HALOLOG") != nullptr;
        hlog && ImGui::GetFrameCount() % 60 == 0)
      REXLOG_INFO("dbz3 iwmini: escena 3D puestos {} pintados {} tapados {} fuera {}", escena_.Cuantos(),
                  escena_.Pintados(), escena_.Ocultos(), escena_.Descartados());
    // HUD de IW: TIME y el contador arriba a la derecha, radar abajo a la derecha y retrato/vida/energia abajo a la
    // izquierda, con los nodos del #AMA 6/1 (DS115). DS114 dibujaba aqui un cartel "REST" inventado: el original no
    // tiene sprite ni texto en pantalla al descansar (solo la voz), asi que se quita.
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const S::Area a = S::GameArea(io);
    // 0x7D9 va en Super Saiyan (Goku SSJ, forma 2 de B3 = SSJ; OJO: aqui `forma` es la de IW, donde 1 = SSJ): la cara
    // del aro es la de IW t30 (NO la base t29) y el luchador empieza transformado (EmpezarCombate). S1: el 0x7DF
    // tambien es SSJ; DS56: la 14 es Cell semiperfecto (t38). El resto de MSNMW, en su forma 0.
    const int forma = d.indice == 10 || d.indice == 14 || d.indice == 16 ? 1 : 0;
    if (g_sin_hud.load()) {
      // 30/10: sobrevuelo y presentacion: IW no pinta HUD hasta el estado 0
    } else if (d.indice == 10 || d.indice == 14) {   // DS68: HUD propio de esos dos indices (6/1); el resto sigue igual
      const V3 yo = Leer(base, mgr + kX0);
      const float dy = d.npc == 2 ? npc_p_.y - yo.y : 0.0f;
      const float dh = DistH(yo, npc_p_);
      const float dist_iw = std::sqrt(dh * dh + dy * dy) / std::max(d.escala, 1e-3f);   // unidades de IW (1400 = 0)
      PintarHudCampo(fg, a, d.indice, campo_, time_, restante_, g_energia.load() / 100.0f, d.indice == 10 ? std::max(0.0f, g_vida.load()) / 100.0f : 1.0f, dist_iw, d.cara, forma);
    } else {
      PintarHud(fg, a, campo_, time_, d.cara, forma, restante_, g_energia.load() / 100.0f);   // (+0x185C)
    }
    if (!g_sin_hud.load() && (d.indice == 16 || d.num_a > 0)) DibujarPuerta(fg, a, d);
    // EU 0x29F600 Minigame_State1A_Enter carga los slots del radar SOLO al mantener triangulo;
    // EU 0x299520 Minigame_ReadPlayerButtons -> EU 0x2A54C0 FreeAllResourceSlots los libera al soltarlo.
    // SOL1: se estaba pintando siempre en 10/16, tapando el escenario incluso durante la entrada y el final.
    if (g_radar.load() && !g_reloj_parado.load() && g_final.load() == 0)
      DibujarRadar(fg, a, d, Leer(base, mgr + kX0));
  }

  // Contador de puertas del MSNMW (Minigame_UpdateScoreDigits, EU 0x30ECD0): icono hexagonal t27 y cifras cian t12
  // con los aros cogidos / (aros de la dificultad + 1), +0x166e / (+0x166f + 1). DS115: posicion EXACTA del #AMA 6/1
  // objeto 0x02 (lienzo 640x480): icono t27 en (459,114) con rect [-20,-24,20,24] -> esquina (439,90) 40x48, y las
  // cinco celdas de las cifras en x 482,499,516 ("/"),531,548 con y 103. Antes iba a ojo a 1165/164.
  void DibujarPuerta(ImDrawList* fg, const S::Area& a, const Datos& d) {
    const Tex& tc = radar_[kRadarCifras];
    if (!puerta_ && !tc) return;
    int hechas = aros_, total = d.num_a + 1;
    if (d.indice == 16) {   // 0x7DF: bolas del grupo D (las que marca el Dragon Radar), sin el +1 de la meta
      // EU 0x30ECD0 Minigame_UpdateScoreDigits: en el Radar (0x10) las cifras son +0x16B8 / +0x16B9 (bolas del
      // grupo D cogidas / total del #ITM+0x10); el +1 de +0x166F no se usa en este indice.
      hechas = total = 0;
      for (size_t i = 0; i < d.objetos.size(); ++i)
        if (d.objetos[i].grupo == 3) {
          ++total;
          if (cogido_[i]) ++hechas;
        }
    }
    char s[8];
    std::snprintf(s, sizeof s, "%02d/%02d", std::min(hechas, 99), std::min(total, 99));
    constexpr float kc = 2.0f; // AMA6/1:600x450
    // EU 0x30ECD0 Minigame_UpdateScoreDigits: indice 0x10 quita las dos entradas de dibujo del hexagono
    // (primer hijo y ultimo hermano del #AMA 6/1 obj 2). El original del Radar solo muestra NN/TT.
    if (puerta_ && d.indice != 16) {
      const ImVec2 p = a.At(439 * kc, 90 * kc);
      fg->AddImage(puerta_.id(), p, ImVec2(p.x + 40 * kc * a.s, p.y + 48 * kc * a.s));
    }
    if (!tc) return;
    static const float xs[5] = {482, 499, 516, 531, 548};
    for (int i = 0; i < 5 && s[i]; ++i) {
      const char c = s[i];
      const Glifo& g = c == '/' ? kBarra : kCian[c - '0'];
      const ImVec2 p = a.At(xs[i] * kc, 103 * kc);
      fg->AddImage(tc.id(), p, ImVec2(p.x + g.w * kc * a.s, p.y + g.h * kc * a.s), ImVec2(g.x / tc.w, g.y / tc.h),
                   ImVec2((g.x + g.w) / tc.w, (g.y + g.h) / tc.h));
    }
  }

  // Radar del Dragon: abajo a la derecha, pantalla rosa con su cuadricula, marcas en rombo rosa, la bola en punto
  // naranja, el jugador en el centro (triangulo) y arriba la flecha. Alcance del codigo: d^2 < 2.89e6 (1700 de IW),
  // aqui x la escala del curso.
  // DS115: posiciones EXACTAS del #AMA 6/1 (objetos 0x03/0x20, lienzo 640x480): marco t04 208x232 en (503,341)
  // (rect [-104,-116,104,116]) y pantalla/rejilla t10/t05 152x160 en (503,351) (rect [-76,-80,76,80]). El contador
  // de marcas va arriba a la derecha con el objeto 0x02 (DibujarPuerta), no encima del radar.
  // EU 0x3225E0 MgLogic_ResolveSlotByMask: en el Radar (tipo 0x10) elige el objeto objetivo girando desde el indice
  // +0x16a6 hasta encontrar uno ya cogido en la mascara global 0x5c1d78, y marca +0x16a4 = 2. Esa mascara no esta en
  // los datos del jugador: el port pinta todas las marcas y no elige objetivo: [H].
  void DibujarRadar(ImDrawList* fg, const S::Area& a, const Datos& d, const V3& yo) {
    constexpr float kc = 1200.0f / 640.0f;
    const float k = kc * a.s;
    const ImVec2 cen = a.At(503 * kc, 351 * kc);   // centro de la pantalla redonda
    auto pon = [&](int j, ImVec2 c, float w, float h, ImU32 tinte = IM_COL32_WHITE) {
      if (radar_[j]) fg->AddImage(radar_[j].id(), ImVec2(c.x - w * k * 0.5f, c.y - h * k * 0.5f),
                                  ImVec2(c.x + w * k * 0.5f, c.y + h * k * 0.5f), ImVec2(0, 0), ImVec2(1, 1), tinte);
    };
    pon(kRadarPantalla, cen, 152, 160, IM_COL32(220, 70, 110, 255));   // la pantalla del Radar es rosa en la captura
    pon(kRadarRejilla, cen, 152, 160);
    const float radio = 62.0f * k, alcance = 1700.0f * d.escala, yaw = g_yaw.load();
    const float fx = std::sin(yaw), fz = std::cos(yaw);
    auto marca = [&](const V3& p, int j, float w) {
      V3 q = p;   // mundo periodico: la marca mas proxima al jugador, no la copia literal
      dbz3::suelo_iw::EnvolverReferencia(q.x, q.z, yo.x, yo.z);
      const float dx = q.x - yo.x, dz = q.z - yo.z;
      const float ade = dx * fx + dz * fz, der = -(dx * fz - dz * fx);   // delante = arriba
      float u = der / alcance, v = ade / alcance;
      const float l = std::hypot(u, v);
      if (l > 1.0f) return;   // fuera de alcance: el juego no lo apunta
      pon(j, ImVec2(cen.x + u * radio, cen.y - v * radio), w, w);
    };
    if (d.indice == 16) {   // el Dragon Radar del 0x7DF: marcas del grupo D y la bola
      for (size_t i = 0; i < d.objetos.size(); ++i)
        if (d.objetos[i].grupo == 3 && !cogido_[i]) marca(d.objetos[i].p, kRadarMarca, 16);
      marca(d.llegada, kRadarBola, 16);
    } else {   // 0x7D9: el radar de la mision "Find the spaceship!" marca las puertas A y la nave (0x31F280)
      for (size_t i = 0; i < d.objetos.size(); ++i)
        if (d.objetos[i].grupo == 0 && !cogido_[i]) marca(d.objetos[i].p, kRadarMarca, 16);
      // EU 0x30ECD0 Minigame_UpdateScoreDigits
      // la nave del 0x7D9 (pieza del campo) se marca en su sitio; en los demas, la meta de fin
      if (!fin_) marca(d.meta_fija ? d.meta_modelo : d.llegada, kRadarBola, 16);
    }
    pon(kRadarYo, cen, 24, 24);
    pon(kRadarFlecha, ImVec2(cen.x, cen.y - radio - 4 * k), 24, 24);
    pon(kRadarMarco, a.At(503 * kc, 341 * kc), 208, 232);   // centro del marco t04
  }

  // Pintado 3D: ps2::Escena (triangulos de lejos a cerca, tapados por las esferas del luchador)
  ps2::Escena escena_;
  // Polvo del sprint: una instancia del #AME 934/26 por arranque (Fotograma sube g_polvo_n), en los pies y hacia atras
  std::vector<std::shared_ptr<dbz3::iwame::ame::Instancia>> polvo_;
  int polvo_visto_ = 0;
  uint32_t semilla_polvo_ = 7;
  // DS91: instancias de los #AME con nombre (aura, dash, dstart, sbound, hang, grow) en el mismo atlas del 934/0.
  std::vector<std::shared_ptr<dbz3::iwame::ame::Instancia>> fx_;
  int fx_visto_ = 0;
  uint32_t semilla_fx_ = 11;
  std::vector<Tex> tex_polvo_, tx_atlas_, tx_suma_;
  std::vector<Tex> tx_pasos_ap_, tx_aura_;
  dbz3::iwame::ame::AuraCuerpo aura_;
  std::vector<std::shared_ptr<dbz3::iwame::ame::Instancia>> pasos_ap_;
  struct ImpactoVivo {std::shared_ptr<dbz3::iwame::ame::Instancia> in;V3 p;std::array<float,4> q;};
  std::vector<ImpactoVivo> impactos_ap_;
  std::vector<Tex> tx_impactos_ap_;
  void ImpactosAp(ImDrawList* dl,const Camara& cam,const Datos& d) {
    namespace ame=dbz3::iwame::ame;
    std::vector<ImpactoAp> nuevos;
    {std::lock_guard<std::mutex> lk(g_impactos_ap_mu);nuevos.swap(g_impactos_ap);}
    for(const auto& p:nuevos) {
      if(p.campo!=&d || p.tipo<0 || p.tipo>1 || !d.impactos_ap[size_t(p.tipo)])continue;
      auto in=std::make_shared<ame::Instancia>();in->ef=&d.impactos_ap[size_t(p.tipo)];in->mezcla_gs=true;
      // Simular en el espacio LOCAL del efecto; al dibujar aplicar la rotacion
      //13C690/Y->camara y la posicion fijadas por1FA330, no reanclar cada tick.
      impactos_ap_.push_back({std::move(in),p.p,p.q});
    }
    iwazar::Global().ConEstado([&](uint32_t& sem){for(auto& in:impactos_ap_)ame::Cuadro(*in.in,sem);});
    impactos_ap_.erase(std::remove_if(impactos_ap_.begin(),impactos_ap_.end(),[](const auto& i){return !i.in->vivo;}),impactos_ap_.end());
    for(const auto& i:impactos_ap_) {
      ame::Instancia visual=*i.in;
      for(auto& lista:visual.ps)for(auto& p:lista) {
        p.p=dbz3::iwame::Mas(i.p,ame::Rotar(i.q,p.p));p.v=ame::Rotar(i.q,p.v);
      }
      ame::Pintar(visual,cam,tx_impactos_ap_,tx_impactos_ap_,dl,std::numeric_limits<float>::max(),true);
    }
  }
  void AuraDeCuerpo(const Camara& cam,uint8_t* base,uint32_t x) {
    namespace ame=dbz3::iwame::ame;
    if(!aura_ || g_sin_aura.load())return;
    V3 q[8];
    static constexpr int kIdx[8]={0,1,4,5,6,7,3,2};
    for(int i=0;i<8;++i)if(!PuntoCuerpo(base,x,kIdx[i],q[i]))return;
    const V3 ojo{-(cam.m[0]*cam.m[12]+cam.m[1]*cam.m[13]+cam.m[2]*cam.m[14]),
                 -(cam.m[4]*cam.m[12]+cam.m[5]*cam.m[13]+cam.m[6]*cam.m[14]),
                 -(cam.m[8]*cam.m[12]+cam.m[9]*cam.m[13]+cam.m[10]*cam.m[14])};
    float alfa=1;
    ame::AuraPaso(aura_,q,ojo,alfa);
    const ps2::Malla m=ame::AuraMalla(aura_,q[0],ojo,alfa);
    if(std::getenv("DBZ3_IW_EVENTOS_LOG") && ImGui::GetFrameCount()%60==0)
      REXLOG_INFO("dbz3 iwmini: aura vivas {} {} alfa {:.3f} w0 ({:.2f},{:.2f},{:.2f})",aura_.rayos[0].size(),aura_.rayos[1].size(),alfa,aura_.w0.x,aura_.w0.y,aura_.w0.z);
    if(m)escena_.Tapable(false),escena_.Poner(cam,m,tx_aura_,V3{},0.0f,1.0f),escena_.Tapable(true);
  }
  void PasosAp(ImDrawList* dl,const Camara& cam,const Datos& d) {
    namespace ame=dbz3::iwame::ame;
    std::vector<PasoAp> nuevos;
    {std::lock_guard<std::mutex> lk(g_pasos_ap_mu);nuevos.swap(g_pasos_ap);}
    for(const auto& p:nuevos) {
      const size_t i=p.hijo<9?size_t(p.hijo-3):size_t(p.hijo-7);
      const ame::Efecto* ef=p.hijo>=18?&d.kami_fx[size_t(p.hijo-18)%3]:i<d.pasos_ap.size()?&d.pasos_ap[i]:nullptr;
      if(!ef || !*ef)continue;
      auto in=std::make_shared<ame::Instancia>();in->ef=ef;
      in->origen=p.p;in->esc=d.escala;in->mezcla_gs=true;in->modelos=p.hijo>=18&&!d.kami_modelos.empty();
      pasos_ap_.push_back(std::move(in));
    }
    iwazar::Global().ConEstado([&](uint32_t& sem){for(auto& in:pasos_ap_)ame::Cuadro(*in,sem);});
    pasos_ap_.erase(std::remove_if(pasos_ap_.begin(),pasos_ap_.end(),[](const auto& in){return !in->vivo;}),pasos_ap_.end());
    for(const auto& in:pasos_ap_) {
      ame::Pintar(*in,cam,tx_pasos_ap_,tx_pasos_ap_,dl,160.0f*ImGui::GetIO().DisplaySize.y/720.0f);
      if(in->modelos)ame::PintarModelos(*in,cam,d.kami_modelos,tx_pasos_ap_,dl);
    }
  }
  void Polvo(ImDrawList* dl, const Camara& cam, const Datos& d) {
    namespace ame = dbz3::iwame::ame;
    if (const int n = g_polvo_n.load(); n != polvo_visto_) {
      polvo_visto_ = n;
      // El modelo SI esta identificado: es el unico #AMO del paquete (934/1, MODEL_L00_EFT). El #AME 934/26 trae cuatro
      // particulas de tipo 4 (modelo) con su tamano en +0x70/+0x74 = 0.20, 0.22, 0.25 y 0.30 (leido del #AME), y un
      // rayo (tipo 2, textura 30). El 120 de medio lado del modelo es el de la malla base: el tamano real del cono es
      // 934/1 x (+0x70 x k). Sigue apagado por defecto (DBZ3_IW_POLVO=1) porque no hay captura que lo valide.
      static const bool con_polvo = std::getenv("DBZ3_IW_POLVO") != nullptr;
      if (d.polvo && con_polvo) {
        auto in = std::make_shared<ame::Instancia>();
        in->ef = &d.polvo;
        in->origen = {g_polvo_x.load(), g_polvo_y.load(), g_polvo_z.load()};
        in->dir = {-g_polvo_dx.load(), 0, -g_polvo_dz.load()};
        in->esc = d.k;
        in->modelos = true;
        polvo_.push_back(in);
        REXLOG_INFO("dbz3 iwmini: polvo del sprint en ({:.0f}, {:.0f}, {:.0f})", in->origen.x, in->origen.y, in->origen.z);
      }
    }
    for (auto& in : polvo_) ame::Cuadro(*in, semilla_polvo_);
    polvo_.erase(std::remove_if(polvo_.begin(), polvo_.end(), [](const auto& in) { return !in->vivo; }), polvo_.end());
    for (const auto& in : polvo_) {
      ame::Pintar(*in, cam, tx_atlas_, tx_suma_, dl, 160.0f * ImGui::GetIO().DisplaySize.y / 720.0f);
      if (!d.m_polvo.malla) continue;
      for (const auto& lista : in->ps)   // las de modelo (tipo 4): la malla 934/1, su -y a lo largo de su velocidad
        for (const auto& p : lista) {
          if (p.n->tipo != 4 || p.edad <= 0) continue;
          // en las de modelo el tamano va en +0x70/+0x74 (0.20-0.30, auditoria7/O), donde las otras llevan el angulo:
          // ame::Cuadro lo dejo en p.ang en radianes
          static const float mult = std::getenv("DBZ3_POLVO_X") ? float(std::atof(std::getenv("DBZ3_POLVO_X"))) : 1.0f;
          const float esc = p.ang / 0.017453292f * in->esc * mult;   // DBZ3_POLVO_X: escala de prueba
          const float lv = dbz3::iwame::Largo(p.v);
          const V3 f = lv > 1e-3f ? dbz3::iwame::Por(p.v, 1.0f / lv) : in->dir;
          V3 r{f.z, 0, -f.x};
          const float lr = dbz3::iwame::Largo(r);
          r = lr > 1e-3f ? dbz3::iwame::Por(r, 1.0f / lr) : V3{1, 0, 0};
          const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
          // el color y el alfa los pone la particula al dibujar (la PS2 modula la malla con su RGBAQ; auditoria8/H): sin
          // eso la malla salia con el alfa de su vertice y no se veia
          const float tt = std::clamp(p.edad / p.vida, 0.0f, 1.0f), *Fc = p.n->f;
          float cc[4];
          for (int i = 0; i < 4; ++i)
            cc[i] = std::clamp((p.n->flags & 0x10) ? Fc[12 + i] + (Fc[16 + i] - Fc[12 + i]) * tt : Fc[12 + i], 0.0f, 1.0f);
          // la PS2 los suma (Cs*As + Cd): con la mezcla por alfa, el tono a tope y el brillo en el alfa (lo oscuro no
          // suma nada: transparente, no una mancha negra)
          const float br = std::max({cc[0], cc[1], cc[2], 1e-3f});
          const ImU32 col = IM_COL32(int(cc[0] / br * 255), int(cc[1] / br * 255), int(cc[2] / br * 255),
                                     int(std::min(1.0f, br * cc[3]) * 255));
          ps2::Malla w = d.m_polvo.malla;
          for (auto& tr : w.tris) {
            tr.con_color = true;
            tr.trans = true;
            tr.col[0] = tr.col[1] = tr.col[2] = col;
          }
          for (auto& tr : w.tris)
            for (V3& q : tr.p)
              q = {p.p.x + (r.x * q.x - f.x * q.y + u.x * q.z) * esc, p.p.y + (r.y * q.x - f.y * q.y + u.y * q.z) * esc,
                   p.p.z + (r.z * q.x - f.z * q.y + u.z * q.z) * esc};
          escena_.Poner(cam, w, tex_polvo_, {0, 0, 0}, 0, 1.0f);
          static int vistos = 0;
          if (vistos++ < 3) REXLOG_INFO("dbz3 iwmini: polvo: cono en ({:.0f}, {:.0f}, {:.0f}) escala {:.2f} color {:.2f} {:.2f} {:.2f} {:.2f}", p.p.x, p.p.y, p.p.z, esc, cc[0], cc[1], cc[2], cc[3]);
        }
    }
  }
  // DS91: efectos con nombre del #AME 934 (aura, dash, d start, sbound, hang, grow). Se instancian con el mismo
  // reproductor reducido que el polvo; las particulas de sprite/rayo las pinta ame::Pintar con el atlas 934/0.
  // Los modelos (tipo 4) no se pintan (el polvo ya tiene su camino propio con la malla 934/1).
  void Efectos(ImDrawList* dl, const Camara& cam, const Datos& d) {
    namespace ame = dbz3::iwame::ame;
    if (const int n = g_fx_n.load(); n != fx_visto_) {
      fx_visto_ = n;
      const int tipo = std::clamp(g_fx_tipo.load(), 0, int(Datos::kFxCuenta) - 1);
      const ame::Efecto* ef = &d.fx[size_t(tipo)];
      if (*ef) {
        auto in = std::make_shared<ame::Instancia>();
        in->ef = ef;
        in->origen = {g_fx_x.load(), g_fx_y.load(), g_fx_z.load()};
        in->dir = {-g_fx_dx.load(), 0, -g_fx_dz.load()};
        in->esc = d.k;
        fx_.push_back(in);
      }
    }
    for (auto& in : fx_) ame::Cuadro(*in, semilla_fx_);
    fx_.erase(std::remove_if(fx_.begin(), fx_.end(), [](const auto& in) { return !in->vivo; }), fx_.end());
    for (const auto& in : fx_) ame::Pintar(*in, cam, tx_atlas_, tx_suma_, dl, 160.0f * ImGui::GetIO().DisplaySize.y / 720.0f);
  }
  // EU 0x296370 MiniGame_TransformToCamera: pasar un punto a espacio de camara (resta camara+0xC0), pasarlo por
  // ObjectsCountEffect (cuenta/ordena los efectos de esa posicion) y devolverlo sumando otra vez camara+0xC0. Es la
  // colocacion de los efectos de los minijuegos en la PS2 (VU + lista de objetos); en el port eso lo hace ps2::Escena
  // (proyeccion con Camara::P y orden por profundidad antes de pintar los triangulos): `motor`.
  void Poner3D(const Camara& cam, const Modelo& m, const std::vector<Tex>& tx, const V3& pos, float giro, float esc) {
    // toon como IW y B3 (3 tonos y borde negro, ps2::Escena); borde ~1,5 % del alto del modelo
    static ps2::Escena::Toon toon;
    toon.borde = 0.12f;
    escena_.Poner(cam, m.malla, tx, pos, giro, esc, toon);
  }
  void Tapas(const Camara& cam, uint8_t* base, uint32_t x) { escena_.Tapar(cam, base, x); }
  void Pintar3D(ImDrawList* dl) { escena_.Pintar(dl); }

  // Aro hexagonal RING_A01 de pie, de cara a la camara (gira en su eje como en IW), con la banda de su textura y la
  // barrera translucida (BARRIER_A02).
  void DibujarAro(ImDrawList* dl, const Camara& cam, const V3& c0, float t) {
    constexpr float kR = 4.0f;
    const V3 c{c0.x, std::max(c0.y, kR + 0.5f), c0.z};
    const float g = t * 1.2f + c.x * 0.1f;
    const float sx = std::cos(g), sz = std::sin(g);
    ImVec2 v[6];
    float kk = 0;
    for (int j = 0; j < 6; ++j) {
      const float a = 3.14159265f / 3.0f * float(j) + 3.14159265f / 6.0f;
      const V3 p{c.x + sx * std::cos(a) * kR, c.y + std::sin(a) * kR, c.z + sz * std::cos(a) * kR};
      float k;
      if (!cam.P(p, v[j], k)) return;
      kk = std::max(kk, k);
    }
    if (2.0f * kR * kk > 1.6f * cam.h.y) return;   // pegado a la camara: taparia la pantalla
    const float ancho = std::max(2.0f, 0.9f * kk);
    const ImVec2 cen((v[0].x + v[3].x) * 0.5f, (v[0].y + v[3].y) * 0.5f);
    dl->AddConvexPolyFilled(v, 6, IM_COL32(120, 200, 255, 40));
    for (int j = 0; j < 6; ++j) {
      const ImVec2 a = v[j], b = v[(j + 1) % 6];
      auto hacia = [&](ImVec2 p, float f) {
        const float dx = cen.x - p.x, dy = cen.y - p.y, n = std::max(std::hypot(dx, dy), 1e-3f);
        return ImVec2(p.x + dx / n * f, p.y + dy / n * f);
      };
      if (aro_)
        dl->AddImageQuad(aro_.id(), a, b, hacia(b, ancho), hacia(a, ancho), ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1),
                         ImVec2(0, 1));
      else
        dl->AddQuadFilled(a, b, hacia(b, ancho), hacia(a, ancho), IM_COL32(40, 120, 255, 220));
    }
  }

  // Moneda de Capsule Corp (B) o bebida (C): la textura es la de la cara del modelo; gira y se recorta en disco.
  void DibujarMoneda(ImDrawList* dl, const Camara& cam, const Objeto& ob, float t) {
    ImVec2 o;
    float k;
    const V3 p{ob.p.x, std::max(ob.p.y, 1.2f), ob.p.z};
    if (!cam.P(p, o, k)) return;
    const float r = (ob.grupo == 1 ? 1.1f : 0.9f) * k, giro = std::cos(t * 4.0f + p.x);
    if (r > 0.25f * cam.h.y) return;   // pegada a la camara
    const float w = r * std::max(0.15f, std::fabs(giro));
    const Tex& tx = ob.grupo == 1 ? moneda_ : bebida_;
    if (!tx) {
      dl->AddCircleFilled(o, r, ob.grupo == 1 ? IM_COL32(240, 190, 40, 255) : IM_COL32(80, 200, 255, 255));
      return;
    }
    const float u0 = ob.grupo == 1 && giro < 0 ? 0.5f : 0.0f, u1 = ob.grupo == 1 ? u0 + 0.5f : 1.0f;
    dl->AddImageRounded(tx.id(), ImVec2(o.x - w, o.y - r), ImVec2(o.x + w, o.y + r), ImVec2(u0, 0), ImVec2(u1, 1),
                        IM_COL32_WHITE, w);
  }

  // Meta abierta: si el modulo trae modelo (nave de Raditz 903/7 en el 0, nave de Freezer 900/7 en el 10) se pinta el
  // modelo; el circulo es solo el respaldo cuando no hay modelo o el disparador no coincide con el casco (0x7D9).
  void DibujarMeta(ImDrawList* dl, const Camara& cam, const V3& m, float radio, float t) {
    const float pul = 0.6f + 0.4f * std::sin(t * 4.0f);
    ImVec2 v[32];
    for (int j = 0; j < 32; ++j) {
      const float a = 6.2831853f * float(j) / 32.0f;
      float k;
      if (!cam.P(V3{m.x + std::cos(a) * radio, 0.2f, m.z + std::sin(a) * radio}, v[j], k)) return;
    }
    dl->AddConvexPolyFilled(v, 32, IM_COL32(255, 210, 60, int(60 * pul)));
    dl->AddPolyline(v, 32, IM_COL32(255, 230, 120, int(220 * pul)), ImDrawFlags_Closed, 3.0f);
  }

  // Al acabar (GOAL o sin tiempo): fundido a negro sobre el final de combate de B3 (su "RING OUT!" y las poses) con
  // CLEAR del juego (DATA_USA 334/3 t69) si llego a la meta. El resultado lo ensena el mapa (PantallaDM::Resultado).
  // EU299900 llama UI_SetObjectsField158Dispatch(0,0,0,0): 158 es el campo
  // de seleccion de UI0. PINE confirma que apunta al AMA de9 objetos de5/0,
  // no al archivo158 ni a una frase del objetivo. El temporizador es140.
  void DibujarCartel(ImGuiIO& io, const Datos& d) {
    if (d.avisos.Valida()) {
      const int nodo=d.avisos.Objeto(0);
      if (nodo<0) return;
      const int cuadro=kCartelCuadros-g_cartel.load();
      auto arbol=anim::ama::CrearArbol(d.avisos,nodo,float(cuadro));
      anim::ama::MarcarArbol(arbol,float(cuadro));
      std::vector<anim::ama::Entrada> entradas;
      anim::ama::ActualizarNodo(arbol,arbol.raiz,{},entradas);
      const auto a=S::GameArea(io);
      auto at=[&](float x,float y) { return a.At(x*2,y*2); };
      auto* dl=ImGui::GetForegroundDrawList();
      for (const auto& e:entradas) {
        if (e.SinTextura()||e.textura>=aviso_tx_.size()||!aviso_tx_[e.textura]) continue;
        const auto& tx=aviso_tx_[e.textura];
        anim::ama::Quad q;anim::ama::DibujarEntrada(e,q);
        auto color=[&](int i) { return std::clamp(int(e.tinte[i]*255+e.rgba[i]+.5f),0,255); };
        dl->AddImageQuad(tx.id(),at(q.xy[0][0],q.xy[0][1]),at(q.xy[1][0],q.xy[1][1]),
            at(q.xy[2][0],q.xy[2][1]),at(q.xy[3][0],q.xy[3][1]),
            ImVec2(q.uv[0][0]/tx.w,q.uv[0][1]/tx.h),ImVec2(q.uv[1][0]/tx.w,q.uv[1][1]/tx.h),
            ImVec2(q.uv[2][0]/tx.w,q.uv[2][1]/tx.h),ImVec2(q.uv[3][0]/tx.w,q.uv[3][1]/tx.h),
            IM_COL32(color(0),color(1),color(2),color(3)));
      }
      return;
    }
    const Ficha* f = FichaDe(d.indice);
    if (!f || !f->frase) return;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const S::Area a = S::GameArea(io);
    const float k = std::clamp(float(g_cartel.load()) / 15.0f, 0.0f, 1.0f);   // se va en el ultimo cuarto de segundo
    // banda oscura a todo el ancho y la frase con la letra de los menus del juego (con su perfil), como los demas
    // avisos del port; antes iba con la letra por defecto de ImGui, fina y poco legible (revision 2026-10-16)
    const ImVec2 p0 = a.At(0, 320), p1 = a.At(1200, 420);
    fg->AddRectFilled(ImVec2(0, p0.y), ImVec2(io.DisplaySize.x, p1.y), IM_COL32(0, 0, 0, int(190 * k)));
    if (k > 0.3f) S::MenuText(fg, a.At(600, 370), 36 * a.s, f->frase, true, false, 1100 * a.s);
  }

  // DS115: se quito el cartel flotante "REST" que dibujaba DS114. Comprobado en el video original (Aq0WlA3UZiU
  // 02:18-02:24, Gemini con 4 fps): al agotarse la energia Goku solo DICE "Time to rest" por voz; NO hay texto ni
  // icono en pantalla (ni sobre la cabeza ni en ventana), y en los AFS no existe sprite ni cadena "REST" (barrido
  // DATA_USA/DATA_CMN de DS114). Era contenido inventado por el port; no se sustituye por otro texto.

  // 30/10: rotulo del final: EU 0x299900 con parametro 1 (superado, EU 0x31F280) o 2 (no superado, EU 0x2A6260): objeto 7 u 8 del
  // #AMA 5/0, 150 cuadros. El mismo pintor que el READY. Con el final forzado en PCSX2 no sale porque se salta esa llamada.
  void DibujarRotuloFin(ImGuiIO& io, const Datos& d, int objeto, int cuadro) {
    if (!d.avisos.Valida() || cuadro > 150) return;
    const int nodo = d.avisos.Objeto(objeto);
    if (nodo < 0) return;
    auto arbol = anim::ama::CrearArbol(d.avisos, nodo, float(cuadro));
    anim::ama::MarcarArbol(arbol, float(cuadro));
    std::vector<anim::ama::Entrada> entradas;
    anim::ama::ActualizarNodo(arbol, arbol.raiz, {}, entradas);
    const auto a = S::GameArea(io);
    auto at = [&](float x, float y) { return a.At(x * 2, y * 2); };
    auto* dl = ImGui::GetForegroundDrawList();
    for (const auto& e : entradas) {
      if (e.SinTextura() || e.textura >= aviso_tx_.size() || !aviso_tx_[e.textura]) continue;
      const auto& tx = aviso_tx_[e.textura];
      anim::ama::Quad q;
      anim::ama::DibujarEntrada(e, q);
      auto color = [&](int i) { return std::clamp(int(e.tinte[i] * 255 + e.rgba[i] + .5f), 0, 255); };
      dl->AddImageQuad(tx.id(), at(q.xy[0][0], q.xy[0][1]), at(q.xy[1][0], q.xy[1][1]), at(q.xy[2][0], q.xy[2][1]),
                       at(q.xy[3][0], q.xy[3][1]), ImVec2(q.uv[0][0] / tx.w, q.uv[0][1] / tx.h),
                       ImVec2(q.uv[1][0] / tx.w, q.uv[1][1] / tx.h), ImVec2(q.uv[2][0] / tx.w, q.uv[2][1] / tx.h),
                       ImVec2(q.uv[3][0] / tx.w, q.uv[3][1] / tx.h), IM_COL32(color(0), color(1), color(2), color(3)));
    }
  }
  void DibujarFin(ImGuiIO& io, float t) {
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const float k = std::clamp((t - fin_t_) / 0.6f, 0.0f, 1.0f);
    fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * k)));
    if (rotulo_ama_ || !meta_ || !clear_ || t - fin_t_ > 1.2f) return;   // CLEAR sobre el fundido un momento; despues solo negro hasta el mapa
    const S::Area a = S::GameArea(io);
    const float s = 6.0f * a.s * (0.8f + 0.2f * std::min(1.0f, (t - fin0_) * 2.0f));   // desde la meta, sobre la pose
    const ImVec2 c = a.At(600, 600);   // bajo la cara del primer plano final (antes 430 la tapaba)
    fg->AddImage(clear_.id(), ImVec2(c.x - clear_.w * s * 0.5f, c.y - clear_.h * s * 0.5f),
                 ImVec2(c.x + clear_.w * s * 0.5f, c.y + clear_.h * s * 0.5f));
  }

  const Datos* hecho_ = nullptr;
  uint32_t partida_ = 0;
  Tex aro_, moneda_, bebida_, time_, clear_, ready_, start_, puerta_;
  std::vector<Tex> campo_;   // DS68: DATA_USA 6/0 por indice, para el HUD propio del 0x7D9 (10) y el 0x7DD (14)
  std::vector<Tex> aviso_tx_;
  float empieza_t_ = -1;   // cuando empezo la pelea (START! un segundo)
  std::vector<Tex> tex_aro_, tex_moneda_, tex_bebida_, tex_meta_, tex_marca_, tex_d_;   // texturas de los modelos 3D
  std::vector<Tex> tex_geiser_, tex_roca_;
  bool rotulo_ama_ = false;
  int cuadro_rotulo_ = 0;
  struct Roca { V3 p, v; float vida; int tam; float giro; };
  std::vector<Roca> rocas_;
  std::mt19937 rng_rocas_{std::random_device{}()};
  // 30/10: EU 0x2A63B0 pide una roca con 1000 < 100 cada cuadro en el indice 10 y EU 0x2A57A0 la crea, hasta (dificultad+1)*4+8 a la vez
  // (EU 0x29A450): 300 sobre el jugador, 25 % delante de la camara. La mitad (limite/200*100 %) van a por el; el resto caen cerca.
  void PasoRocas(uint8_t* base, uint32_t x0, const Datos& d) {
    if (d.indice != 10 || !d.m_roca[0].malla) return;
    const V3 yo = Leer(base, x0);
    auto r = [&](int n) { return int(rng_rocas_() % uint32_t(n)); };
    auto mas = [&](int n) { return (rng_rocas_() & 1 ? 1.0f : -1.0f) * float(r(n)); };
    const int limite = (d.dificultad + 1) * 4 + 8;
    if (r(1000) < 100 && int(rocas_.size()) < limite) {
      Roca n{};
      n.p = {yo.x, yo.y + 300.0f, yo.z};
      n.vida = 600.0f, n.tam = r(3);
      if (r(100) < 25) {
        const float dist = 500.0f + mas(200), yw = g_yaw.load();
        n.p.x += std::sin(yw) * dist + mas(200), n.p.z += std::cos(yw) * dist + mas(200);
      } else {
        n.p.x += mas(500), n.p.z += mas(500);
      }
      auto hacia = [&](const V3& a, float v) {
        const float dx = a.x - n.p.x, dy = a.y - n.p.y, dz = a.z - n.p.z, l = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 1e-3f);
        n.v = {dx / l * v, dy / l * v, dz / l * v};
      };
      // eIW3: s_BG_RCK_DRAW (EU 0x2A57A0): con el jugador en los estados 0x26..0x29 (golpe) la proporcion que va a por el es 1, no
      // limite/200*100. Interruptor DBZ3_IW_ROCAS_ESTADO=1 (apagado: como antes).
      static const bool rocas_estado = std::getenv("DBZ3_IW_ROCAS_ESTADO") != nullptr;
      const int fase_j = g_fase.load();
      const int f = rocas_estado && fase_j > 0x25 && fase_j < 0x2A ? 1 : int(float(limite) / 200.0f * 100.0f), q = r(100);
      if (q < f) hacia(yo, 4.0f);
      else if (q < f + 1 || q >= f + (d.dificultad + 1) * 5) n.v = {float(r(20)) * 0.1f, -5.0f, float(r(20)) * 0.1f};
      else hacia({yo.x + mas(100), yo.y, yo.z + mas(100)}, 4.5f);
      rocas_.push_back(n);
    }
    for (size_t i = 0; i < rocas_.size();) {
      Roca& c = rocas_[i];
      c.p = {c.p.x + c.v.x, c.p.y + c.v.y, c.p.z + c.v.z};
      c.giro += 0.1745f;   // +0x5C += 0x71C
      c.vida -= 1.0f;
      const float radio = c.tam == 2 ? 15.0f : c.tam == 1 ? 12.0f : 8.0f;
      V3 q = c.p;   // mundo periodico: la copia mas proxima al jugador
      dbz3::suelo_iw::EnvolverReferencia(q.x, q.z, yo.x, yo.z);
      const float dx = q.x - yo.x, dy = q.y - (yo.y + 10.0f), dz = q.z - yo.z;
      const bool pega = g_gracia.load() <= 0 && dx * dx + dy * dy + dz * dz < radio * radio;
      if (pega) {
        g_vida = g_vida.load() - (float(c.tam) * 5.0f + 15.0f);
        g_golpe = 1;
      }
      const float suelo = dbz3::suelo_iw::Altura(c.p.x, c.p.z, -1e30f);
      if (pega || c.vida <= 0 || (suelo > -1e29f && c.p.y <= suelo)) {
        rocas_[i] = rocas_.back();
        rocas_.pop_back();
      } else {
        ++i;
      }
    }
  }
  // 31/10: rayos de Namek. EU 0x2A63B0 (indice 10): Memory % 1000 < suelo / 20 cada cuadro -> EU 0x2AAB60 s_BG_KAMI_DRAW, uno
  // a la vez, fuera de 0x1A..0x29. EU 0x2A9AE0: 200 cuadros de espera; efecto 0x10 (AME 19) en el suelo del jugador.
  // EU 0x2A97D0: a 50 cuadros cae en la posicion del jugador en ese momento: AME 18 en el suelo, o AME 20 a y - 30 si esta a
  // mas de 80 del suelo; de 56 a 80 pega a menos de 15 (y del rayo): -20 de vida, estado 0x20 y AME 934/29 en la raiz.
  struct Kami { int fase = 0; float t = 0; V3 p; bool cayo = false; } kami_;
  std::mt19937 rng_kami_{std::random_device{}()};
  void PasoKami(uint8_t* base, uint32_t x0, const Datos& d) {
    if (d.indice != 10) return;
    const V3 yo = Leer(base, x0);
    float suelo = dbz3::suelo_iw::Altura(yo.x, yo.z, -1e30f);
    if (suelo < -1e29f) suelo = yo.y;
    if (kami_.fase == 0) {
      if (g_fase.load() >= 0x1A && g_fase.load() <= 0x29) return;
      static const float kami_div = std::getenv("DBZ3_TEST_IWKAMI") ? 1.0f : 20.0f;   // prueba: como el parche de EU 0x2A6B48 en el original
      if (int(rng_kami_() % 1000u) < int(suelo / kami_div)) {
        kami_ = {};
        kami_.fase = 1;
        REXLOG_INFO("dbz3 iwmini: rayo: pedido (suelo {:.1f})", suelo);
      }
      return;
    }
    kami_.t += 1;
    if (kami_.fase == 1) {
      if (kami_.t > 200) {
        kami_.fase = 2, kami_.t = 0;
        kami_.p = {yo.x, suelo, yo.z};
        std::lock_guard<std::mutex> lk(g_pasos_ap_mu);
        g_pasos_ap.push_back({19, kami_.p});
      }
      return;
    }
    if (kami_.t > 80) { kami_.fase = 0; return; }
    if (kami_.t > 50 && !kami_.cayo) {
      kami_.cayo = true;
      const bool alto = yo.y - suelo > 80;
      kami_.p = {yo.x, alto ? yo.y - 30.0f : suelo, yo.z};
      std::lock_guard<std::mutex> lk(g_pasos_ap_mu);
      g_pasos_ap.push_back({alto ? 20 : 18, kami_.p});
      REXLOG_INFO("dbz3 iwmini: rayo: cae en ({:.1f},{:.1f},{:.1f}) {}", kami_.p.x, kami_.p.y, kami_.p.z, alto ? "aire" : "suelo");
    }
    static const bool kami_sin_golpe = [] { const char* e = std::getenv("DBZ3_TEST_IWKAMI"); return e && *e == '2'; }();   // prueba: como +0x1AC alto en el original
    if (kami_.cayo && kami_.t > 56 && g_gracia.load() <= 0 && !kami_sin_golpe) {
      V3 q = kami_.p;   // mundo periodico: la copia mas proxima al jugador
      dbz3::suelo_iw::EnvolverReferencia(q.x, q.z, yo.x, yo.z);
      const float dx = yo.x - q.x, dz = yo.z - q.z;
      if (dx * dx + dz * dz < 15.0f * 15.0f) {
        g_vida = g_vida.load() - 20.0f;
        g_golpe = 2;
        kami_.fase = 0;
        LanzarFx(Datos::kFxRayoGolpe, yo.x, yo.y, yo.z, 0, 1);
        REXLOG_INFO("dbz3 iwmini: rayo: pega, vida {:.0f}", g_vida.load());
      }
    }
  }
  Modelo geiser_pose_;   // el geiser en el cuadro de su animacion (Posar)
  Tex radar_[16];                                                              // piezas de 6/0 del Radar
  std::vector<bool> cogido_;
  int puntos_objetos_ = 0;
  int cogidos_ = 0, aros_ = 0;
  int64_t diag_ms_ = 0;
  bool campo_ini_ = true, camp_ok_ = false;   // tanda3: s_SPACE original (iw_mips_campo.inl); camp_ = G+0x1620..0x1700 del invitado
  dbz3::iwmips::campo::Salida camp_;
  // Avisos de progreso (kCutins): bits ya disparados, C/D pendientes, el activo (fila) y su reloj en cuadros de 60.
  uint32_t cutin_hechos_ = 0;
  bool cutin_c_ = false, cutin_d_ = false, cutin_mitad_ = false;
  int cutin_act_ = -1;
  float cutin_t_ = 0;
  std::vector<Tex> cutin_tx_, cutin_txt_;
  // NPC (Bubbles o Piccolo) y platanos
  V3 npc_p_, npc_dir_;
  iwbubbles::Maquina bubbles_;
  float bubbles_acum_ = 0;
  //Instancia mutable SOLO del BBR. Nunca cambiar el clip compartido ni el esqueleto.
  struct PoseBubbles {
    const Datos* datos=nullptr;const iwbubbles::Maquina* maquina=nullptr;
    iwbubbles::poses::Instancia<47> cuerpo;
    iwbubbles::poses::Instancia<8> cara;
    unsigned codigo=~0u;std::uint64_t revision=~std::uint64_t{0};float cuadro=0;int fase=-1;
    iwbubbles::audio::Intro voz_intro;
    std::shared_ptr<iwsonido::Canal> canal_voz=std::make_shared<iwsonido::Canal>();
    std::uint32_t publicaciones=0,ultima_medida=~0u,medidas=0;
    bool medida_intentada=false;std::shared_ptr<FILE> medida;
    void Iniciar(){
      cuerpo.Iniciar(datos->bubbles_poses->Muestrear(0xD2,0));
      cara.Iniciar(datos->bubbles_poses->cara.Reposo());
    }
    iwbubbles::V Raiz()const{const auto& p=cuerpo.live[1].p;return {p.x,p.y,p.z};}
    void Facial(){
      const auto& r=*datos->bubbles_poses;
      // 206EE0 sin pista de voz: Char_PushAnimIds(0,0), relajacion facial .6.
      cara.AplicarFactor(r.cara.Muestrear(0,float(r.cara.Cuadros(0)-1),r.lut),.6f);
      r.PublicarCara(cuerpo,cara);
    }
    iwbubbles::V Evaluar(unsigned nuevo,float frame){
      if(revision!=maquina->revision){
        revision=maquina->revision;
        std::memcpy(&cuerpo.u,&maquina->mezcla.cb4,4);std::memcpy(&cuerpo.rate,&maquina->mezcla.cb8,4);
      }
      codigo=nuevo;cuadro=frame;
      cuerpo.Aplicar(datos->bubbles_poses->Muestrear(nuevo,frame),1.2f);Facial();++publicaciones;return Raiz();
    }
    void AplicarRaiz(iwbubbles::V r){
      auto& h=cuerpo.live[1];h.p={r.x,r.y,r.z,1};iwbubbles::poses::Matriz(h);
    }
    void Intro(int nueva){
      const bool cambio=nueva!=fase;
      if(cambio){
        fase=nueva;cuadro=0;codigo=fase==1?0x11D:0xD2;
        cuerpo.u=fase==2?.005f:1.f;cuerpo.rate=.03f;
        revision=maquina->revision;
      }
      const auto& r=*datos->bubbles_poses;
      if(!cambio||fase==1){
        const float n=iwbubbles::noche::ClockAdd(cuadro,1.2f);
        const float largo=float(r.cuerpo.Cuadros(r.clips[codigo]));
        // AnimationFrames conserva el ultimo cuadro fraccionario al terminar.
        // En PAL11D es179.999603, no largo-1 (179).
        cuadro=fase==1?(n<largo?n:cuadro):(n>=largo?0.f:n);
      }
      cuerpo.Aplicar(r.Muestrear(codigo,cuadro),1.2f);
      if(fase!=1)Facial(); // estado10 NO publica los siete huesos de25FA90.
      const auto* base=Base();
      const bool japones=base && REX_LOAD_U8(dbz3::GA(0x824BA110)+0x2C0C9)==0;
      const auto& voz=datos->bubbles_audio[japones?1:0];
      if(voz_intro.Paso(fase,codigo,cuadro,voz.boca)){
        auto& salida=iwsonido::Salida();
        salida.UsarProveedor(+[]()->iwsonido::Ajustes{
          return {float(settings::MasterVolume()),settings::AudioMute()};
        });
        iwsonido::Opciones o;o.canal=canal_voz;
        const auto handle=iwadx::Oir(voz.pcm,std::move(o));
        REXLOG_INFO("dbz3 gp2 bubbles voz_inicio fase={} codigo={:X} cuadro={} perfil=16 linea=0 entrada={} idioma={} hz={} muestras={} boca={:X} handle={} normal=false",
          fase,codigo,cuadro,voz.entrada,japones?"JPN":"USA",voz.pcm.hz,voz.pcm.muestras.size()/2,voz_intro.boca.id,handle);
      }
      ++publicaciones;
    }
    void Posar(Malla& out)const{
      std::vector<ps2::M34> locales(47);
      for(size_t i=0;i<47;++i){const auto& h=cuerpo.live[i];auto& m=locales[i].m;
        const auto& r=datos->m_npc.esq->local[i].m;
        const auto dibujo=iwbubbles::poses::MatrizDibujo(h,{r[3],r[7],r[11],1});
        std::copy(dibujo.begin(),dibujo.end(),m);
      }
      ps2::PosarMatrices(*datos->m_npc.esq,locales,false,out);
    }
    void Medir(const Malla& m,V3 dibujo,float yaw,const ps2::Camara& cam){
      // Solo la focal explicita. Registra el LIVE y los vertices que recibe Poner3D,
      // sin modificar pose, reloj, camara ni RNG. Formato versionado IWPB1.
      if(!medida_intentada){
        medida_intentada=true;
        if(const char* path=std::getenv("TEST_GP2_BB_POSE");path&&*path){
          medida=std::shared_ptr<FILE>(std::fopen(path,"wbx"),[](FILE* f){if(f)std::fclose(f);});
          if(!medida)REXLOG_WARN("dbz3 gp2 bubbles POSE no creado: {}",path);
        }
      }
      if(!medida||ultima_medida==publicaciones||medidas>=700||m.tris.size()>4096)return;
      if(fase<=0 && publicaciones%10!=0)return;
      ultima_medida=publicaciones;
      const std::uint32_t h[8]={0x42505749u,1,medidas++,unsigned(fase),codigo,
        unsigned(maquina->estado),unsigned(maquina->tick),unsigned(m.tris.size())};
      const auto root=Raiz();const auto c=datos->m_npc.esq->centro;
      const float meta[16]={cuadro,cuerpo.u,cuerpo.rate,maquina->p.x,maquina->p.y,maquina->p.z,
        root.x,root.y,root.z,dibujo.x,dibujo.y,dibujo.z,yaw,c.x,c.y,c.z};
      const float pantalla[5]={cam.fov,cam.c.x,cam.c.y,cam.h.x,cam.h.y};
      std::vector<float> vertices;vertices.reserve(m.tris.size()*18);
      for(const auto& tr:m.tris)for(int i=0;i<3;++i){const auto p=tr.p[i],n=tr.n[i];
        vertices.insert(vertices.end(),{p.x,p.y,p.z,n.x,n.y,n.z});}
      auto escribir=[&](const void* p,size_t n){return std::fwrite(p,1,n,medida.get())==n;};
      const bool ok=escribir(h,sizeof h)&&escribir(meta,sizeof meta)&&
        escribir(cuerpo.live.data(),sizeof cuerpo.live)&&escribir(cara.live.data(),sizeof cara.live)&&
        escribir(cam.m,sizeof cam.m)&&escribir(pantalla,sizeof pantalla)&&
        escribir(vertices.data(),vertices.size()*sizeof(float));
      if(!ok){REXLOG_WARN("dbz3 gp2 bubbles POSE escritura incompleta");medida.reset();}
    }
  } bubbles_pose_;
  iwbubbles::wcf::Backend bubbles_backend_;
  iwbubbles::wcf::Backend piccolo_wcf_;   // P1: 0x7DD, rayo de la CPU original de Piccolo
  bool piccolo_wcf_ok_ = false, piccolo_ini_ = false;
  V3 piccolo_g_;   // Piccolo +0x40 en el invitado (sin envolver al jugador)
  float piccolo_acum_ = 0;
  uint64_t piccolo_t_ = 0;
  bool bubbles_backend_ok_=false,bubbles_oraculo_ok_=true,bubbles_csv_intentado_=false;
  const char* bubbles_bloqueo_=nullptr;
  uint64_t bubbles_sesion_=0;
  struct CsvBubblesArchivo {FILE* f=nullptr;~CsvBubblesArchivo(){if(f)std::fclose(f);}} bubbles_csv_;
  float npc_t_ = 0, resbalon_ = 0;
  int npc_modo_ = 0;
  Modelo npc_pose_;   // Bubbles en el cuadro de su carrera (Posar)
  uint32_t semilla_ = 12345;
  int64_t npc_log_ = 0;
  std::vector<V3> platanos_;
  std::vector<Tex> tex_npc_, tex_platano_;
  // DS169: aureola de Goku en el mas alla (0x7D1 Bubbles y 0x7D2 Camino de la Serpiente). En IW es una malla del
  // hueso GOK_RING del modelo de Goku (DATA_CMN 409/0: 84 verts, radio 1,68, centro a 20,08 del suelo); la de B3 no la
  // trae. Se carga una vez y se pinta en la escena 3D, encima de la cabeza.
  ps2::Modelo halo_;
  std::vector<Tex> tex_halo_;
  float halo_alto_ = 0;
  bool halo_ok_ = false;
  float restante_ = 0, fin_t_ = 0, fin0_ = 0;
  bool fin_ = false, meta_ = false;
  int64_t auto_ms_ = 0;
  size_t auto_i_ = 0;
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

bool EmpezarCombate(const std::shared_ptr<Datos>& d) {
  const Ficha* f = FichaDe(d->indice);
  // el campo de IW del minijuego (alta 5060+n de tools/campos_iw.py, frente R8); sin instalar, el escenario de B3
  const int campo = dbz3::escenarios::CampoIW(f->campo);
  REXLOG_INFO("dbz3 iwmini: escenario {} (campo de IW {}: {})", campo >= 0 ? campo : f->escenario, f->campo,
              campo >= 0 ? "instalado" : "sin instalar");
  // 0x7D9 ("Find the spaceship!", saga de Freezer): en el original Goku va en Super Saiyan (Gemini/OPUS y contexto de
  // la saga; la fila del mapa no lleva la forma). Las formas de Goku en B3 son 0 normal, **1 Kaioken**, 2 SSJ, 3 SSJ2,
  // 4 SSJ3 (mod center hd/roster_build.py + cac/vista_heroe.py:835; iw_mini_botones.cpp NB014 "Goku SSJ (forma 2 de
  // B3)"). Con la 1 salia **Kaioken**: cuerpo rojo y aura morada (fotos c000/c010/c019). El resto de MSNMW, forma 0.
  // [VERIFICAR] traje: el original lleva el dogi roto; aqui no se cambia (no hay ID de traje medido para esa ropa).
  // S1 (sonnet1): 0x7DF (indice 16, Radar) tambien va en SSJ. Dato: el bloque de estado de la mision de IW (0x5C0DC0,
  // lectura de memoria del original con el minijuego forzado) trae en +0x18 = 2 en los indices 10 y 16 y 0 en el 0
  // (Goku base); +0x10 (personaje) = 0 (Goku) en 0, 10 y 16 y 33 (Cell) en el 14. [H] el significado exacto de +0x18.
  // En B3 Cell 0 = imperfecto, 1 = semiperfecto y 2 = perfecto; el de IW es el imperfecto (captura original ee/i14_2; DS56 decia semiperfecto, mal).
  const int forma1 = (d->indice == 10 || d->indice == 16) && d->cara==0 ? 2 : 0;
  if (!dbz3::hub::RequestBattleEx(d->cara, f->pivote, campo >= 0 ? campo : f->escenario, true, -1, 0, 0, forma1, 0))
    return false;
  // el rival lleva el mando virtual del muneco (sin IA y "conectado": sin el, el juego pausa por el mando 2); el pivote
  // no se mueve ni ataca y a Piccolo (14) Mando le inyecta andar hacia el jugador en ese mando
  dbz3::combate::SetMuneco(1);
  dbz3::combate::OcultarHud(true);  // sin marcador de combate de B3: el HUD es el de IW
  P::Reglas r;
  if (d->halo_nativo) r.traje_jugador = 4;   // B3 Goku Angelic, fid282 con XGOK_RING1
  if (d->indice == 10 && d->cara == 0) r.traje_jugador = 2;   // 0x7D9: Goku con el gi roto (B3 fid 278), como el original
  r.capsulas = false;
  P::VigilarCombate("iwmini", r, [d](const P::Fin& fin) {
    dbz3::combate::SetMuneco(-1);
    dbz3::combate::OcultarHud(false);
    if (uint8_t* base = Base())   // scatto, cansado y corredor apagados al acabar (R10)
      if (const uint32_t mgr = REX_LOAD_U32(kMgrPtr)) {
        dbz3::anim::Scatto(mgr + kX0, dbz3::anim::kApagado);
        dbz3::anim::Cansado(mgr + kX0, false);
        if (d->npc == 2) dbz3::anim::Corredor(mgr + kX0 + kXPaso, false);
      }
    {
      bool falta = false;
      Resultado r;
      {
        std::lock_guard<std::mutex> lk(g_mu);
        falta = g_res.clave != d->clave;   // salio desde la pausa: no superado
      }
      if (falta) {
        r.clave = d->clave;
        r.fila = d->fila;
        r.score = 0;
        r.zeni = 0;
        CompletarResultado(r);   // lee el monedero fuera del candado de g_res
        std::lock_guard<std::mutex> lk(g_mu);
        g_res = r;
      }
    }
    REXLOG_INFO("dbz3 iwmini: fin del combate (ganador {}): resultado listo", fin.ganador);
    {   // s1 F04.4: la misma linea que los modulos (Terminar), para que la prueba en automatico del campo (MSNMW) la
        // espere igual que la de los demas (antes el automatico acababa en META pero la prueba esperaba esta linea)
      std::lock_guard<std::mutex> lk(g_mu);
      REXLOG_INFO("dbz3 iwmini: modulo 0x{:X}: resultado listo (superado {}, rango {}, puntos {})", g_res.clave,
                  g_res.superado, g_res.rango, g_res.puntos);
    }
    g_estado = 3;
  });
  return true;
}

}  // namespace

void RegistrarModulo(int clave, Modulo m) {
  if (clave >= 0x7D0 && clave < 0x7D0 + 32) g_modulos[clave - 0x7D0] = m;
}

void Terminar(const Resultado& r) {
  Resultado res = r;
  CompletarResultado(res);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_res = res;
  }
  REXLOG_INFO("dbz3 iwmini: modulo 0x{:X}: resultado listo (superado {}, rango {}, score {}, zeni {})", res.clave,
              res.superado, res.rango, res.score >= 0 ? res.score : res.puntos,
              res.zeni >= 0 ? res.zeni : res.puntos);
  g_modulo = false;
  g_clave = 0;
  g_estado = 3;
}

// Cara del aro del HUD de los minijuegos. IW (6/0 t29..t38, hoja en E:\DBZ3HD_trabajo\R14\caras_6_0.png) solo trae
// Goku, Goku SSJ, Gohan SSJ2, Krilin, Vegeta SSJ, Trunks SSJ, Piccolo, Yamcha (t36, no Gohan), Ten y Cell: se usa la de
// IW solo si es del mismo personaje y forma; si no, la nativa de B3 (caras/<id>, native_ui) recortada en circulo.
// Formas de B3 (pasarela.cpp:390): Gohan adolescente 1 = SSJ (2 = SSJ2); Cell 1 = semiperfecto (el de IW, DS56),
// 2 = perfecto.
int CaraIW(int id, int forma) {
  switch (id) {
    case 0: return forma == 1 ? 30 : forma == 0 ? 29 : -1;    // Goku base (t29) / SSJ (t30)
    case 3: return forma == 2 ? 31 : -1;    // Gohan adolescente SSJ2 (t31)
    case 7: return forma == 1 ? 33 : -1;    // Vegeta SSJ
    case 8: return forma == 1 ? 34 : -1;    // Trunks SSJ
    case 10: return forma == 0 ? 32 : -1;   // Krilin
    case 11: return forma == 0 ? 35 : -1;   // Piccolo
    case 12: return forma == 0 ? 37 : -1;   // Ten
    case 13: return forma == 0 ? 36 : -1;   // Yamcha
    case 33: return forma >= 1 ? 38 : -1;   // Cell (t38; IW lo juega semiperfecto, forma 1)
  }
  return -1;
}

void PintarCara(ImDrawList* dl, float x0, float y0, float lado, int id, int forma, bool espejo) {
  static std::map<int, Tex> iw;   // por indice de 6/0 (hilo de la interfaz)
  static std::vector<uint8_t> ui;
  static bool leido = false;
  const ImVec2 p0(x0, y0), p1(x0 + lado, y0 + lado), uv0(espejo ? 1.0f : 0.0f, 0), uv1(espejo ? 0.0f : 1.0f, 1);
  if (const int t = CaraIW(id, forma); t >= 0) {
    if (!leido) {
      leido = true;
      P::AfsPs2 usa;
      if (usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) ui = P::HijoAmb(usa.Entrada(6), 0);
    }
    auto it = iw.find(t);
    if (it == iw.end()) it = iw.emplace(t, Crear(t < P::CuantasAmt(ui) ? P::TexturaAmt(ui, t) : P::Imagen{})).first;
    if (it->second) {
      dl->AddImage(it->second.id(), p0, p1, uv0, uv1);
      return;
    }
  }
  if (const S::Pic c = S::Img(("caras/" + std::to_string(id)).c_str()))
    dl->AddImageRounded(c.tex, p0, p1, uv0, uv1, IM_COL32_WHITE, lado * 0.5f);
}

void HudIW(float segundos, float barra, int cara, int forma) {
  static bool leido = false;
  static Tex time;
  static std::vector<Tex> ui_tex;
  if (!leido) {   // DATA_USA 6/0 de la copia del jugador, una vez (hilo de la interfaz)
    leido = true;
    P::AfsPs2 usa;
    std::vector<uint8_t> ui;
    if (usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) ui = P::HijoAmb(usa.Entrada(6), 0);
    const int n = P::CuantasAmt(ui);
    if (n > 19) {   // el HUD de tierra necesita hasta t19 (aro/retrato del grupo 0x09..0x0D)
      ui_tex.resize(size_t(n));
      for (int j = 0; j < n; ++j) ui_tex[size_t(j)] = Crear(P::TexturaAmt(ui, j));
      time = Crear(P::TexturaAmt(ui, kHudTime));
    }
  }
  const ImGuiIO& io = ImGui::GetIO();
  PintarHud(ImGui::GetForegroundDrawList(), S::GameArea(io), ui_tex, time, cara, forma, segundos, barra);
}

void HudVuelo(float segundos, float medidor, int variante, int aros, int total, int estado) {
  HudVueloImpl(segundos, medidor, variante, aros, total, estado);
}

bool Empezar(int clave, int fila) {
  if (g_estado.load() == 1 || g_estado.load() == 2) return false;
  int personaje = g_personaje_clave.exchange(-1) == clave ? g_personaje.exchange(-1) : -1;
  if(std::getenv("DBZ3_TEST_IWMINI"))
    if(const char* p=std::getenv("DBZ3_SOLIW9_PERSONAJE")) personaje=std::atoi(p);
  g_personaje = -1;
  if (personaje >= 0) {
    if (const char* motivo = MotivoPersonaje(clave, personaje)) {
      REXLOG_WARN("dbz3 iw seleccion: {:#x}, personaje {}: {}; no se sustituye el actor", clave, personaje, motivo);
      return false;
    }
    REXLOG_INFO("dbz3 iw seleccion: modulo {:#x} recibe personaje {}", clave, personaje);
  }
  if (const Modulo m = clave >= 0x7D0 && clave < 0x7D0 + 32 ? g_modulos[clave - 0x7D0] : nullptr) {
    if (g_estado.load() == 1 || g_estado.load() == 2) return false;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      g_d.reset();
      g_res = Resultado{};
      g_res.clave = clave;
      g_res.fila = fila;
    }
    g_modulo = true;
    g_clave = clave;
    g_estado = 2;
    const int dif = std::clamp(g_dificultad.load(), 0, 5);
    REXLOG_INFO("dbz3 iwmini: casilla 0x{:X} (fila {}): modulo de otro frente (dificultad {})", clave, fila, dif);
    if (!m(clave, fila, dif, personaje)) {
      g_modulo = false;
      g_estado = 0;
      return false;
    }
    return true;
  }
  const int indice = IndiceDeClave(clave);
  const Ficha* f = FichaDe(indice);
  REXLOG_INFO("dbz3 iwmini: casilla 0x{:X} (fila {}) = minijuego {} ({}): {}", clave, fila, indice,
              f ? f->modulo : "?", f && f->hecho ? "empieza" : "aun no portado");
  if (!f || !f->hecho || g_estado.load() == 1 || g_estado.load() == 2) return false;
  auto d = std::make_shared<Datos>();
  d->indice = indice;
  d->clave = clave;
  d->fila = fila;
  d->dificultad = std::clamp(g_dificultad.load(), 0, 5);
  d->cara=personaje>=0 ? personaje : f->jugador;
  constexpr bool kHaloGokuNativoValidado=true;   // solIWX3/11: RAM fid282 + fotos de ambas misiones
  d->halo_nativo=d->cara==0 && (indice==1 || indice==2) &&
    (kHaloGokuNativoValidado || (std::getenv("DBZ3_TEST_IWMINI") &&
      std::getenv("DBZ3_TEST_IWMINI_HALO_NATIVO")));
  if(d->halo_nativo)REXLOG_INFO("dbz3 iwmini: halo nativo B3 traje=4 fid=282 overlay=false mini={}",indice);
  std::string error;
  if (!Cargar(*d, error)) {
    REXLOG_WARN("dbz3 iwmini: {}", error);
    return false;
  }
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_d = d;
    ++g_partida;
    g_res = Resultado{};
  }
  g_clave = clave;
  g_colocado = false;
  g_anda = false;
  const float dx = d->meta.x - d->salida.x, dz = d->meta.z - d->salida.z, l = std::max(std::hypot(dx, dz), 1e-3f);
  const float yaw_ojo = d->indice >= 0 && d->indice < 19 ? kYawOjoIW[d->indice] : kSinYawIW;
  g_dir_x = dx / l;
  g_dir_z = dz / l;
  g_avance_x = g_dir_x.load();
  g_avance_z = g_dir_z.load();
  // la camara mira de la salida a la meta, salvo en los minijuegos de campo cuya camara inicial se midio en el original
  g_yaw = yaw_ojo > kSinYawIW ? (yaw_ojo + 180.0f) * kGrados : std::atan2(dx, dz);
  g_pitch = 15.0f;   // Camera_InitFieldDefault (EU 0x297140): camara+0xE4 = 15 grados
  g_radar = false;
  dbz3::launcher::DeferUi(&CreatePanel);
  g_estado = 2;
  if (!EmpezarCombate(d)) {
    g_estado = 0;
    REXLOG_WARN("dbz3 iwmini: el conector no acepto el combate");
    return false;
  }
  return true;
}

bool Activo() { return g_estado.load() != 0; }
int EscenaApCampo() {return g_estado.load()==2 && !g_modulo.load()?0:-1;}
bool EventoApImpacto(uint8_t* base,uint32_t x,int tipo,const float posicion[3]) {
  const auto d=Actual();const uint32_t mgr=REX_LOAD_U32(kMgrPtr);
  if(!d || EscenaApCampo()!=0 || !mgr || (x!=mgr+kX0 && x!=mgr+kX0+kXPaso) || tipo<0 || tipo>1)return false;
  if(!d->impactos_ap[size_t(tipo)] || d->atlas_impactos_ap.empty())return false;
  for(int i=0;i<3;++i)if(!std::isfinite(posicion[i]))return false;
  float m[16],fov;if(!CamaraDelRenderIw(m,&fov))return false;
  //1EBC60 devuelve objetivo-ojo. En una vista de B3 equivale a menos la Z.
  V3 f{-m[2],-m[6],-m[10]};const float l=std::sqrt(f.x*f.x+f.y*f.y+f.z*f.z);
  if(!(l>0) || !std::isfinite(l))return false;f={f.x/l,f.y/l,f.z/l};
  V3 mitad{f.x,f.y+1,f.z};const float h=std::sqrt(mitad.x*mitad.x+mitad.y*mitad.y+mitad.z*mitad.z);
  if(!(h>0) || !std::isfinite(h))return false;mitad={mitad.x/h,mitad.y/h,mitad.z/h};
  //44BB90=(0,1,0,1), no Z: q=[Y x normalizar(Y+f),Y dot mitad].
  const std::array<float,4> q{mitad.z,0,-mitad.x,mitad.y};
  std::lock_guard<std::mutex> lk(g_impactos_ap_mu);
  g_impactos_ap.push_back({tipo,{posicion[0],posicion[1],posicion[2]},q,d.get()});
  return true;
}
bool EventoApCampo(uint8_t* base,uint32_t x,int valor,int& sonido938) {
  sonido938=-1;
  const auto d=Actual();
  const uint32_t mgr=REX_LOAD_U32(kMgrPtr);
  if(!d || g_modulo.load() || g_estado.load()!=2 || !mgr || x!=mgr+kX0)return false;
  const V3 yo=Leer(base,x);
  const int superficie=dbz3::suelo_iw::TipoSuelo(yo.x,yo.z,yo.y+2.0f);
  if(valor==5 || valor==6)sonido938=anim::apev::SonidoCampo(valor,d->indice,superficie,0);
  else if(valor>=1 && valor<=4) {
    const int hijo=anim::apev::EfectoCampo(valor,d->indice,superficie,0);
    if(hijo>=0) {
      V3 p;
      if(!PuntoCuerpo(base,x,(valor==1 || valor==3)?12:13,p))return false;
      const float suelo=dbz3::suelo_iw::Altura(yo.x,yo.z,-1e30f);
      if(suelo< -1e29f)return false;
      p.y=suelo;
      {std::lock_guard<std::mutex> lk(g_pasos_ap_mu);g_pasos_ap.push_back({hijo,p});}
    }
  }
  if(std::getenv("DBZ3_IW_EVENTOS_LOG"))
    REXLOG_INFO("dbz3 AP campo9c: mini {} material {} valor {} -> AME {} sonido938 {}",d->indice,superficie,valor,
        anim::apev::EfectoCampo(valor,d->indice,superficie,0),sonido938);
  return true;
}
void PadPrueba(int dir) { g_pad_prueba = dir; }

bool AnularAndar(uint8_t* base, uint32_t x, uint32_t accion) {
  if ((accion != 2 && accion != 3) || !g_libre.load() || g_estado.load() != 2) return false;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  return mgr && x == mgr + kX0;
}

void Dificultad(int ficha) { g_dificultad = std::clamp(ficha, 0, 5); }   // F5: la de la ficha, tal cual

const char* MotivoPersonaje(int clave, int personaje) {
  // Actores fijos de los 18 modulos. Los permisos los lee la ficha del #DMD;
  // esta tabla solo expresa la cobertura REAL del port, no desbloquea personajes.
  static constexpr int fijos[] = {0, 0, 0, 0, 2, 0, 10, 10, 27, 0, 7, 7, 0, 33, 12, 0, 4, 15};
  if (clave < 0x7D0 || clave > 0x7E1 || personaje < 0) return "seleccion de minijuego invalida";
  if (personaje == fijos[clave - 0x7D0]) return nullptr;
  if ((clave == 0x7D6 && personaje == 2) || (clave == 0x7E0 && personaje == 9)) return nullptr;
  // DestroyTheBricks es una galeria: IW oculta al actor y no reproduce poses
  // suyas; no hay manos de Goku que sustituir (FotogramaDisparo, juego 3).
  if (clave == 0x7D3 && (personaje == 11 || personaje == 12 || personaje == 13)) return nullptr;
  if ((clave==0x7D1||clave==0x7D2)&&(personaje==11||personaje==12||personaje==13)) return nullptr;
  if (clave==0x7DF&&(personaje==3||personaje==10||personaje==8||personaje==7)) return nullptr;
  if (clave==0x7DA&&(personaje==10||personaje==12||personaje==13)) return nullptr;
  if (clave==0x7E1&&personaje==14) return nullptr;
  if (clave == 0x7D1 || clave == 0x7D2)
    return "faltan las animaciones propias de entrada/resultado (BSK 0x118-11A o 0x17C-17E); solo esta portado Goku/922";
  if (clave == 0x7DF)
    return "faltan las animaciones propias del Radar (BSK 0x6F4-6F6); solo esta portado Goku/922";
  if (clave == 0x7DA)
    return "faltan los alias de NB012 para Krilin/924, Ten/929 y Yamcha/932 (BSK 0x4B0-4C4, 0x500-502); solo Vegeta/931";
  if (clave == 0x7E1)
    return "faltan los alias de Satan/926 para MSN019 (BSK 0x76C-780, 0x7BC); solo esta portada Videl/930";
  return "el modulo no tiene animaciones portadas para ese personaje";
}

bool PersonajeDelMapa(int clave, int personaje) {
  g_personaje_clave = -1;
  g_personaje = -1;
  if (const char* motivo = MotivoPersonaje(clave, personaje)) {
    REXLOG_WARN("dbz3 iw seleccion: {:#x}, personaje {}: {}; no se sustituye el actor", clave, personaje, motivo);
    return false;
  }
  g_personaje = personaje;
  g_personaje_clave = clave;
  return true;
}

void PalancaDerecha(int16_t x, int16_t y) {
  g_rx = x;
  g_ry = y;
}
int PalancaDerechaY() { return g_ry.load(); }
// El visor de vuelo tambien mira la X de la derecha: Pad_GetStickAxes (EU 0x3394F0) usa la de mayor
// magnitud de las dos (iw_mini_vuelo.cpp).
int PalancaDerechaX() { return g_rx.load(); }

bool TomarResultado(Resultado& r) {
  if (g_estado.load() != 3) return false;
  std::lock_guard<std::mutex> lk(g_mu);
  r = g_res;
  g_d.reset();
  g_estado = 0;
  return true;
}

// Hilo del juego, al empezar el dibujo (clima.cpp): salida, pivote invisible delante del jugador y camara detras.
// SONDA (DBZ3_SONDA_HUD=<carpeta>): vuelca el objeto del marcador (*0x82324064, 0x1400 B) y el gestor del combate
// (0x8C00 B) en la presentacion (estados 1-2, sin marcador) y en la pelea (3-4, con el), para buscar que lo oculta.
void SondaHud(uint8_t* base, uint32_t estado) {
  // DBZ3_SONDA_HUD_PON=hud|mgr:desplazamiento:bytes:valor[,...] (hex): lo escribe en cada cuadro de la pelea (estado 4)
  static const std::vector<std::array<uint32_t, 4>> pon = [] {
    std::vector<std::array<uint32_t, 4>> v;
    const char* e = std::getenv("DBZ3_SONDA_HUD_PON");
    for (const char* p = e; p && *p;) {
      const uint32_t que = std::strncmp(p, "mgr", 3) == 0 ? 1 : 0;
      char* q = nullptr;
      const uint32_t off = uint32_t(std::strtoul(p + 4, &q, 16));
      const uint32_t tam = uint32_t(std::strtoul(q + 1, &q, 16));
      const uint32_t val = uint32_t(std::strtoul(q + 1, &q, 16));
      v.push_back({que, off, tam, val});
      p = *q == ',' ? q + 1 : q;
    }
    return v;
  }();
  if (estado == 4 && !pon.empty()) {
    const uint32_t m = REX_LOAD_U32(kMgrPtr), h = REX_LOAD_U32(dbz3::GA(0x82324064));
    for (const auto& w : pon) {
      const uint32_t a = (w[0] ? m : h) + w[1];
      if (!(w[0] ? m : h)) continue;
      if (w[2] == 1) REX_STORE_U8(a, uint8_t(w[3]));
      else if (w[2] == 2) REX_STORE_U16(a, uint16_t(w[3]));
      else REX_STORE_U32(a, w[3]);
    }
  }
  static const char* dir = std::getenv("DBZ3_SONDA_HUD");
  if (!dir) return;
  static uint32_t ultimo = ~0u, n = 0;
  n = estado == ultimo ? n + 1 : 0;
  ultimo = estado;
  if (estado < 1 || estado > 4 || (n != 20 && n != 60 && n != 100 && n != 300)) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr), hud = REX_LOAD_U32(dbz3::GA(0x82324064));
  auto volcar = [&](const char* que, uint32_t a, uint32_t tam) {
    if (!a) return;
    char nombre[512];
    std::snprintf(nombre, sizeof nombre, "%s/%s_e%u_%u.bin", dir, que, estado, n);
    if (std::FILE* f = std::fopen(nombre, "wb")) {
      std::vector<uint8_t> b(tam);
      for (uint32_t i = 0; i < tam; ++i) b[i] = REX_LOAD_U8(a + i);
      std::fwrite(b.data(), 1, b.size(), f);
      std::fclose(f);
    }
  };
  volcar("hud", hud, 0x1400);
  volcar("mgr", mgr, 0x8C00);
  REXLOG_INFO("dbz3 iwmini: sonda del marcador: estado {} cuadro {} (hud 0x{:08X}, mgr 0x{:08X})", estado, n, hud, mgr);
}

// S1 (sonnet1): el rayo de la camara de IW es contra la colision #WCF del campo con la mascara de materiales 1
// (Camera_UpdateFollow EU 0x297DE0: MiniGame_DrawPromptSingle -> Collision_Raycast, mascara 1; DS34). DS34 lo cambio a la
// malla dibujada (hijo 5 del #AMO), que no trae los arboles ni las rocas sueltas del campo (0x7D1: el ojo quedaba dentro
// del tronco). Aqui se usan las dos y gana el corte mas cercano a `desde` (el objetivo): la regla de IW y la de DS34.
static bool RayoCamara(const float desde[3], const float hasta[3], float corte[3]) {
  float c1[3] = {}, c2[3] = {};
  const bool a = dbz3::suelo_iw::RayoMalla(desde, hasta, c1), b = dbz3::suelo_iw::Rayo(desde, hasta, 1, c2);
  auto d2 = [&](const float c[3]) {
    const float x = c[0] - desde[0], y = c[1] - desde[1], z = c[2] - desde[2];
    return x * x + y * y + z * z;
  };
  if (!a && !b) return false;
  const float* g = a && (!b || d2(c1) <= d2(c2)) ? c1 : c2;
  std::copy(g, g + 3, corte);
  return true;
}

// Campo de vision del scatto: IW (camara +0x120) sale en 40 grados (Camera_InitFieldDefault EU 0x297140 = 0x42200000);
// el scatto (Minigame_State15_DashMove 0x2A0A30) sube +2.5 por cuadro hasta 50 (0x42480000) y, ya en el tope, baja
// -0.38 hasta 40, y la frenada (Minigame_State16_Move 0x2A0370) sigue bajando -0.38 hasta 35 (0x420C0000). Fuera de
// esas dos fases IW NO toca el fov (no lo repone al andar). Se conserva ese
// parametro, convirtiendolo a vertical450/480 para B3; iw<=0 restaura el juego.
static void FovIW(uint8_t* base, float iw) {
  static float fov_juego = 0;
  static bool tocado = false;
  if (iw > 0) g_fov_campo = FovVerticalCampoIW(iw * kGrados);
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr || (iw <= 0 && !tocado)) return;
  if (iw <= 0) {
    StoreF(base, mgr + kFov, fov_juego);
    tocado = false;
    return;
  }
  const float fov = LoadF(base, mgr + kFov);
  if (!tocado) fov_juego = fov;
  if (!(fov_juego > 0.3f && fov_juego < 1.2f)) return;
  // Igualar la proyeccion de126F50, no copiar su parametro como si ya fuera
  // el angulo vertical de B3. Ojo/objetivo se mantienen en sus puntos originales.
  StoreF(base, mgr + kFov, g_fov_campo.load());
  tocado = true;
}

// Final del minijuego (g_final 1 superado / 2 no superado): la animacion entera y medio segundo quieto en su ultimo
// cuadro (tope 5 s), como el estado 0x2F/0x30; despues el fundido a negro (g_final = 3: lo lee el panel).
// EU 0x29A7C0 Minigame_State31_Enter: la entrada del estado 0x31 pone la camara animada (`Camera_PlayCameraAnim_` con
// el clip de +0x25c), guarda el estado anterior, +0xb60 = 0, BonesMovement, +0xcb4 = 1.0 (0x3f800000), +0xcb8 = 0.01
// (0x3c23d70a), +0xcc8 = 0, limpia +0x206c/+0x2065/+0x18a8, velocidad +0xB4 = 1.8 (0x3fe66666), +0x204 = 1,
// +0x1a0 = 0, +0x1b0 = 2, +0xccd = 1, GameState_SetFlag2(0) y, si es el jugador 0 y el tipo no es 1, Audio_PlaySlot(0).
// EU 0x29A670 Minigame_State31_Update: si se pulsa START (bit 8 del mando) cancela el estado (+0xcc4 = 0, objeto+0x1b0
// = 0, camara+300 = 0) y, con objeto+0x1b0 < 2, rearma y llama a s_SPACE; en el tipo 0xE mete la pose 0x16
// (`Char_EnterPoseStateByIndex`). EU 0x29A920 Minigame_State30_Update: al acabar la animacion (+0xc68) resta el paso,
// y con +0x1b0 == 2 -> GameState_SetFlag2(0), +0x1b0 = 3 y MgResult_ComputeScore(-1); en el tipo 10 con +0xc68 > 102 y
// +0x228 -> energia = 100 (0x42c80000).
// En el port el estado del luchador es de B3 y la pose final la lleva anim_iw (VarianteDe + g_final); lo que falta de
// estos tres: la pose 0x16 del indice 0xE y la energia a 100 del 10 en su final: [H].
int g_t_fin = 0;
// DS169: fase del final de IW. 0 = estado 0x2E (animacion de la variante 0x52/0x119/... con su clip de camara);
// 1 = estado 0x2F (pose 0xF0 con la camara 0x1E de Battle_CharState2fInit 0x29B120). IW pasa de una a otra cuando
// acaba la animacion de 0x2E (Battle_CharState2eAnimUpdate 0x29BA00) y vuelve a poner al actor en el origen cada
// cuadro (Battle_CharStateUpdateWrapper_29b270 -> Minigame_ResetPlayerTransform 0x2997F0).
std::atomic<int> g_fase_fin{0};
// IW9c PINE: la tabla EU285F30 sale al acabar la pose (0x53:238.799,
// 202 campos PAL desde deteccion), no a un retraso escogido de180 cuadros.
// Retiene la escena y la pose hasta confirmar. Reloj en el hilo del juego.
int g_t_panel = 0;
uint16_t g_panel_antes = 0xFFFF;
RelojRecompensaIW g_reloj_campo;
V3 g_ancla_fin;
float g_y_buena = 0;       // y del suelo de IW bajo el jugador, la ultima fiable (jugador de campo)
V3 g_ancla_pres;            // 31/10: salida guardada mientras la presentacion 0x2C pone al actor en el origen
bool g_en_presenta = false;
int g_t_entrada = 0;   // cuadros desde que empieza la entrada (clip 0x296, solo indice 1)
// DS112: fase de la entrada del port (0 sin empezar, 1 posa 0x11D del indice 1, 2 arranque s_SPACE, 3 mando al
// jugador) y cuadros desde que empezo la fase 2, que es el reloj de su clip de camara (cam_arranque).
int g_paso_entrada = 0;
int g_clip_sobre = 0;   // 30/10: clip del sobrevuelo en curso y su cuadro
float g_t_sobre = 0;
int g_t_arranque = 0;
// Muestra un clip de camara de IW. `anclada` = el clip va en coordenadas del luchador (se suma `p`); si no, es
// absoluto en el campo. El FOV del clip se conserva acercando el ojo (DS92: B3 ya construyo la matriz con su 45).
bool CamaraIw(uint8_t* base, uint32_t mgr, const Datos& d, int clip, float cuadro, bool anclada, const V3& p,
              float ojo[3], float obj[3], float* balanceo = nullptr) {
  const auto it = d.camaras.find(clip);
  if (it == d.camaras.end()) return false;
  const ClipCamIa& c = it->second;
  const float f = std::clamp(cuadro, 0.0f, float(std::max(c.nf - 1, 0)));
  const dbz3::iwamc::Pose pose = dbz3::iwamc::Muestrea(c, f);
  float a[3] = {pose.ojo[0], pose.ojo[1], pose.ojo[2]}, b[3] = {pose.obj[0], pose.obj[1], pose.obj[2]};
  const float fov = pose.fov, roll = pose.roll;
  if (anclada) {
    a[0] += p.x, a[1] += p.y, a[2] += p.z;
    b[0] += p.x, b[1] += p.y, b[2] += p.z;
  }
  if (balanceo) *balanceo = roll;
  // Ojo/objetivo del recurso intactos. Su angulo tambien pasa por126F50 al
  // reproducirse en campo: convertir una vez, sin compensar la distancia.
  if (fov > 0.1f && fov < 1.4f) {
    g_fov_campo = FovVerticalCampoIW(fov);
    StoreF(base, mgr + kFov, g_fov_campo.load());
  }
  std::copy(a, a + 3, ojo);
  std::copy(b, b + 3, obj);
  if (dbz3::gp2cam::Activo()) {
    const bool extra = clip >= kClipSobre;
    const Ficha* ficha = FichaDe(d.indice);
    dbz3::gp2cam::Evaluada("campo", extra && ficha ? ficha->campo + 1 : 915,
        extra ? 1 : 2, extra ? 1 : 0, extra ? clip - kClipSobre : clip, f, c.nf,
        g_final.load(), g_paso_entrada, ojo, obj, fov, roll, clip);
  }
  return true;
}
// EU 0x29A920 Minigame_State30_Update
// 30/10: EU 0x2997F0 Minigame_ResetPlayerTransform: en los finales el actor va con matriz identidad (+Z). Las miradas de B3 no
// fijan la matriz dibujada: sin esto la pose de resultado salia girada respecto a su #AMC.
static void MatrizFinal(uint8_t* base, uint32_t x0) {
  static constexpr float kId[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  for (uint32_t i = 0; i < 12; ++i) StoreF(base, x0 + 4 * i, kId[i]);
}
// Voz de la pose del jugador (ADX de la copia del jugador; DBZ3_IW_VOZ_POSE=0 la apaga).
void HablarPose(uint8_t* base, const char* estado, int entrada, int indice) {
  if (entrada < 0) return;
  if (const char* e = std::getenv("DBZ3_IW_VOZ_POSE"); e && e[0] == '0') return;
  const int byte = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9));   // 0 = voces japonesas
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  REXLOG_INFO("dbz3 iwmini: voz de pose {} (indice {}, ADX {})", estado, indice, entrada);
  P::OirAdxPs2(P::BuscarIW(), byte == 0 ? "ADX_JPN.AFS" : "ADX_USA.AFS", entrada, vol);
}

void PasoFinal(uint8_t* base, uint32_t mgr, const Variante* v, int fin) {
  ++g_t_fin;
  ++g_t_panel;
  if (dbz3::anim::EstadoPose()==2 && (fin!=1 || !std::getenv("DBZ3_IWMINI_F0") || g_fase_fin.load()==1)) {
    const uint16_t b = REX_LOAD_U16(kPads + 12);
    const bool pasos=std::getenv("DBZ3_TEST_IWMINI") && std::getenv("DBZ3_SOLIW9C_CAMPO_PASOS");
    int destino=100000;
    if(pasos) {std::ifstream req("iw9c_campo_edad.req");if(!(req>>destino))destino=0;}
    const bool inicio=g_panel_campo.load()<0;
    const bool confirmar=g_panel_confirmar.exchange(false) || (b & ~g_panel_antes & 0x4000) != 0 ||
        (g_auto && !pasos && g_reloj_campo.cuadro>=420);
    const float avance=!pasos || g_reloj_campo.cuadro<float(destino)?1.0f:0.0f;
    const float antes=g_reloj_campo.cuadro;
    g_reloj_campo.Avanzar(avance,confirmar);
    float animacion=inicio?0:g_panel_campo_anim.load();
    if(confirmar && antes>0 && antes<420 && g_reloj_campo.cuadro>=420)animacion=420;
    else animacion+=avance;
    g_panel_campo_anim=animacion;
    g_panel_antes = b;
    g_panel_campo = g_reloj_campo.cuadro;
    if(inicio)REXLOG_INFO("dbz3 IW campo9c: tabla sobre escena al terminar pose; final {} cuadro {}",fin,g_t_fin);
    if(pasos && g_reloj_campo.cuadro>=float(destino)) {
      static int anterior=-1;
      if(anterior!=destino) {
        anterior=destino;
        REXLOG_INFO("dbz3 IW campo9c: tabla detenida edad {} cerrado {}",destino,g_reloj_campo.cerrado);
      }
    }
    if(g_reloj_campo.cerrado)REXLOG_INFO("dbz3 IW campo9c: tabla confirmada edad {:.1f}",g_reloj_campo.cuadro);
  }
  if (g_fase_fin.load() == 0) {
    // EU 0x29BA00 Battle_CharState2eAnimUpdate: al acabar la animacion de resultado (0x52, 240 cuadros) el estado pasa
    // a 0x2F. La 0x52 acaba con la raiz en y 68,5 / z 50 (GOK_WAIST, medido en 922/1) y su clip 0x259 termina con el
    // ojo en y 4,4 mirando a y 9,9: el luchador salia del encuadre y se quedaba fuera todo el rato que el port mantenia
    // el CLEAR (defecto del 0x7D0 a los 200 s). En 0x2F IW lo devuelve al origen y pone la pose de pie 0xF0.
    // EU29BA00/29A920: termina por la animacion del actor, sin ocho cuadros
    // de confirmacion ni un tope260 ajeno al recurso (Radar solo dura180).
    if (v && dbz3::anim::EstadoPose() != 2) return;
    if (std::getenv("DBZ3_TEST_IWMINI")) {
      dbz3::anim::EstadoCuerpo e;
      dbz3::anim::LeerEstadoCuerpo(base, mgr + kX0, 0, e);
      REXLOG_INFO("dbz3 solIW6: campo final fase0 t {} codigo {:X} cuadro {:.1f}/{}",g_t_fin,e.codigo,e.cuadro,e.cuadros);
    }
    // 31/10: PCSX2 (0x7D2, 0x7D0): con la tabla abierta el actor sigue en 0x2E/0x30, retenido en su ultimo cuadro; no pasa a
    // 0xF0 hasta confirmar. La salida se hace al cerrar la tabla, sin la pose 0xF0 (el plano dentro de la pared era eso).
    if (fin != 1 || !std::getenv("DBZ3_IWMINI_F0")) {   // no superado: IW va del estado 0x30 al 0x31 (clip +0x25C, EU 0x29A7C0), no al 0x2F
      if (!g_reloj_campo.cerrado) return;
      REX_STORE_U8(mgr + kFin, uint8_t(fin == 1 ? 1 : 2));
      g_final = 3;
      return;
    }
    g_fase_fin = 1;
    if (const auto dd = Actual()) HablarPose(base, "2F", dd->voz_2f, dd->indice);
    g_t_fin = 1;   // no 0: es el mismo contador que t_fin de Fotograma, y a 0 volvia a empezar el final (bucle de la 0x52)
    if (v) {
      // EU29B120: F0 del BSK comun915/3, AMM915/0 indice47,180cuadros.
      // El enlace por SUFIJO conserva el esqueleto destino tambien con Cell.
      // EU 0x29B2A0 Battle_CharState2fUpdate: retrocede el reloj al llegar al
      // ultimo cuadro; no devuelve al personaje a la guardia de combate.
      dbz3::anim::Reproducir(mgr + kX0, 0x8F0, true);
      REXLOG_INFO("dbz3 iwmini: resultado 0x2F: pose 0xF0 + camara 0x1E (EU 0x29B120 Battle_CharState2fInit)");
    }
    return;
  }
  // EU29B2A0: el estado0x2F sale a dur-1 de la pose0xF0, NO al final
  // de la camara0x1E ni tras150 cuadros. El motor conserva ese ultimo cuadro.
  if (!v || dbz3::anim::EstadoPose() == 2) {
    if (std::getenv("DBZ3_TEST_IWMINI")) {
      dbz3::anim::EstadoCuerpo e;
      dbz3::anim::LeerEstadoCuerpo(base, mgr + kX0, 0, e);
      REXLOG_INFO("dbz3 solIW6: campo final fase1 t {} codigo {:X} cuadro {:.1f}/{}",g_t_fin,e.codigo,e.cuadro,e.cuadros);
    }
    if (!g_reloj_campo.cerrado) return;
    REX_STORE_U8(mgr + kFin, uint8_t(fin == 1 ? 1 : 2));
    g_final = 3;
  }
}

// El final del combate de B3 (estados 5 y 6) corre mientras sigue el nuestro: se mantienen la camara en primer plano y el
// temporizador del final; la pose la mantiene anim_iw (reafirma el codigo cada cuadro).
void FinB3(uint8_t* base, uint32_t mgr) {
  const std::shared_ptr<Datos> d = Actual();
  if (!d) return;
  const Variante* v = VarianteDe(d->indice,d->cara);
  if (g_final.load() != 3) PasoFinal(base, mgr, v, g_final.load());
  // EU 0x2997F0 mantiene el transform de la secuencia. B3 recoloca al ganador en (0,0,0)
  // al pasar 5->6: conservar el ancla hasta que termine el fundido, no seguir esa recolocacion.
  const V3 p = g_ancla_fin;
  Poner(base, mgr + kX0, p);
  MatrizFinal(base, mgr + kX0);
  if (std::getenv("DBZ3_TEST_IWMINI_CAMLOG") && g_t_fin % 30 == 0) {
    const uint32_t x = mgr + kX0;
    float r[3] = {};
    dbz3::anim::RaizActual(base, x, r);
    REXLOG_INFO("dbz3 iwmini: SOL4 final t {} st {} p ({:.2f},{:.2f},{:.2f}) raiz ({:.2f},{:.2f},{:.2f}) oculto {}/{} mundo ({:.2f},{:.2f},{:.2f})",
                g_t_fin, REX_LOAD_U32(mgr + 35128), p.x,p.y,p.z,r[0],r[1],r[2],
                REX_LOAD_U8(x+kOcultoA),REX_LOAD_U8(x+kOcultoB),LoadF(base,x+48),LoadF(base,x+52),LoadF(base,x+56));
  }
  const float dx = g_dir_x.load(), dz = g_dir_z.load();
  float r[3] = {0, 0, 0};   // la camara sigue el salto de superado (0x52: 69 de alto y 50 hacia delante)
  const bool hay = dbz3::anim::RaizActual(base, mgr + kX0, r);
  const float sube = hay && r[1] > 12.0f ? (r[1] - 12.0f) * 0.9f : 0.0f, delante = hay && r[2] > 0 ? r[2] * 0.9f : 0.0f;
  const float tx = p.x + dx * delante, tz = p.z + dz * delante;
  float ojo[3] = {tx - dx * 45.0f, p.y + 14.0f + sube, tz - dz * 45.0f};
  float obj[3] = {tx, p.y + 12.0f + sube, tz};
  // DS93: la camara del resultado es el clip de IW (State2E/30); si no se pudo cargar, queda el primer plano del port.
  const int fin = g_final.load();
  // DS169: 0x2E usa el clip de la variante (0x259...); 0x2F, el 0x1E de Battle_CharState2fInit (EU 0x29B120).
  const int clip_fin = g_fase_fin.load() ? d->cam_resultado : fin == 1 ? d->cam_bien : d->cam_mal;
  float roll = 0;
  if ((fin == 1 || fin == 2) &&
      CamaraIw(base, mgr, *d, clip_fin, LoadF(base, mgr + kX0 + 0x650 + 0x608), d->cam_final_rel, p, ojo, obj, &roll)) {
    // camara del clip
  }
  // DS177: esta camara es la que MANDA en el dibujo: el gancho de sub_820F4300 la escribe en la struct activa (la
  // fuente de la vista; DS163) y despues publica la foto para el panel. El fov es el de FovIW (mgr+0x1C4), que es el
  // que se usa para el render mientras dura el minijuego.
  CamaraMinijuego(true, ojo, obj, g_fov_campo.load(), roll);
  dbz3::cine::Camara(base, ojo, obj, roll);
}

void Fotograma(uint8_t* base, uint32_t estado) {
  // DS189: las trazas de cuerpos/camara son de depuracion. Solo se escriben en las pruebas (tools/pruebas.py pone
  // DBZ3_TEST_IWMINI al lanzar un minijuego); en el juego normal no ensucian el log.
  static const bool traza_log = std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  // DS177: cada cuadro se suelta la camara del minijuego; si este cuadro se calcula (entrada, carrera o final) se
  // vuelve a publicar mas abajo. Asi, al acabar (o si el modulo no dibuja), el juego recupera su camara.
  CamaraMinijuego(false, nullptr, nullptr, 0.0f);
  { void FotogramaDisparo(uint8_t*, uint32_t); FotogramaDisparo(base, estado); }   // R13: disparo (iw_mini_disparo.cpp)
  { void PasoVuelo(uint8_t*); PasoVuelo(base); }   // R12: vuelo (iw_mini_vuelo.cpp; sin vuelo en marcha no hace nada)
  { void PasoBotones(uint8_t*); PasoBotones(base); }   // R14: botones (iw_mini_botones.cpp): su camara
  SondaHud(base, estado);
  if (g_estado.load() == 2 && !g_modulo.load() && (estado == 5 || estado == 6) && g_final.load() != 0 &&
      !std::getenv("DBZ3_IWMINI_FIN_TARDE"))
    if (const uint32_t mgr = REX_LOAD_U32(kMgrPtr)) FinB3(base, mgr);
  if (g_estado.load() != 2 || (estado != 3 && estado != 4)) {
    g_libre = false;
    if (g_cae) g_cae = false, dbz3::suelo_iw::SueloFijo(0, false);
    FovIW(base, 0);
    return;
  }
  const std::shared_ptr<Datos> d = Actual();
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!d || !mgr) {
    // B1c: serie comun jugador/ojo para los 12 minijuegos que no son de campo (botones, disparo, vuelo). El P4 ya
    // escribe la suya al final de Fotograma. Es la linea que consume tools/lector_camara.py (serie_log_port) y, con
    // ella, `comparar_modo.py iw <clave>` deja de dar "sin muestras" en esos 12.
    // R17: el vuelo (iw_mini_vuelo.cpp, que no toco) no llama a SerieJuego; se enciende aqui por su clave para que
    // comparar_modo lo pueda medir (la camara y el jugador los escribe el propio vuelo cada cuadro).
    if (g_modulo.load() && (g_clave.load() == 0x7D6 || g_clave.load() == 0x7E0))
      g_serie_juego = (g_estado.load() == 2);
    if (g_modulo.load() && mgr && (estado == 3 || estado == 4) && g_serie_juego.load()) {
      static int64_t traza_mod = 0;
      static int ultimo_clip = -2;
      bool escribe = false;
      if (g_serie_por_clip.load()) {   // familia B: una fila por camara de ronda (como el TSV del original)
        const int c = g_serie_clip.load();
        if (c != ultimo_clip) ultimo_clip = c, escribe = true;
      } else if (NowMs() - traza_mod > 250) {   // 0,25 s: el dorado del original se remuestrea a ese paso
        traza_mod = NowMs();
        escribe = true;
      }
      if (escribe) {
        float ojo[3], obj[3];
        if (dbz3::cine::LeerCamara(base, ojo, obj)) {
          const V3 p = Leer(base, mgr + kX0);
          // fase 1 (7/11): tambien el objetivo, para medir hacia donde mira (diferencial.giro_reposo_iw)
          REXLOG_INFO("dbz3 iwmini: jugador ({:.1f}, {:.1f}, {:.1f}) marcha (0.00, 0.00) anda 0 ojo ({:.1f}, {:.1f}, {:.1f}) "
                      "objetivo ({:.2f}, {:.2f}, {:.2f})",
                      p.x, p.y, p.z, ojo[0], ojo[1], ojo[2], obj[0], obj[1], obj[2]);
        }
      }
    }
    return;
  }
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  dbz3::anim::ConsumirImpactosAP(base); // post-logica; no muestrear anclas durante la lectura del mando
  // EU 0x29A450 Minigame_StartPlayerWork: prepara el trabajo de los dos jugadores (matrices de +0x40/+0x60/+0x70 a
  // escala 1), engancha camara->jugador (`+0x80`, `+0x40`, `+0x60` del bloque), pone +0x19c = 0x20 arranca el cartel
  // (+0x8d21 = 1, que aqui es g_cartel) y oculta los objetos de la UI. Ademas, indice 10 -> +0x19c = (dificultad+1)*4+8
  // y indice 0xE -> +0x18 = 1. El port hace el enganche con cine::Camara y el cartel con g_cartel; +0x19c (velocidad de
  // giro de la camara del Radar) y +0x18 no se usan: [H].
  if (estado == 3 || !g_colocado.load()) {   // todo el READY y el primer cuadro de la pelea: en la salida
    // En el campo de IW la y del #ITM no es la del suelo (900: salida (-26, 72) con y 17,8 y suelo 32 -> el luchador
    // salia 14 u hundido y la camara de la entrada (p.y + 4) quedaba dentro del terreno: fotogramas negros de c000).
    // IW coloca al luchador con su colision (Stage_BeginLoad/MSNMW); aqui se hace con suelo_iw::Altura.
    V3 s = d->salida;
    if (d->campo) {
      const float g = dbz3::suelo_iw::Altura(s.x, s.z, -1e30f);
      if (g > -1e29f) s.y = g;
    }
    Poner(base, x0, s);
    g_y_buena = s.y;
    if (estado == 4) {
      g_colocado = true;
      g_final = 0;
      g_fase_fin = 0;
      REXLOG_INFO("dbz3 iwmini: salida puesta ({:.0f}, {:.0f}); meta en ({:.0f}, {:.0f})", d->salida.x, d->salida.z,
                  d->meta.x, d->meta.z);
    }
  }
  V3 p = Leer(base, x0);
  if (g_en_presenta) p = g_ancla_pres;
  if (static const bool poslog = std::getenv("DBZ3_TEST_IW_POSLOG") != nullptr; poslog && estado >= 3) {   // eIW5: posicion por cuadro
    static int n = 0;
    if (n++ < 4000) REXLOG_INFO("dbz3 iwpos: {} est {} libre {} col {} p ({:.1f}, {:.1f}, {:.1f}) cae {} anda {}", n, estado,
                                g_libre.load() ? 1 : 0, g_colocado.load() ? 1 : 0, p.x, p.y, p.z, g_cae ? 1 : 0, g_anda.load());
  }
  // D34: al nacer (y quieto) los pies en el SUELO de IW. DS33 2.4 ponia la y del suelo al colocar, pero la pose/intro de
  // B3 la devolvia a la del #ITM a los 2 s (log del clip de DS33: 17:45:07 (-26, 32, 72) -> 17:45:09 (-26, 17,8, 72));
  // con el jugador 14 u hundido la camara de la entrada nacia dentro del terreno. Solo si el suelo esta a <= 30 u de la y
  // actual (asi un techo del #WCF sobre una cueva no sube al luchador) y fuera del salto/caida y del primer plano final.
  // 31/10: el techo se juzga contra la ultima y buena, no contra la de B3: quieto, B3 lo deja en SU suelo (0 en 0x7D0, -8,5 en 0x7D2), a mas de
  // 30 u del de IW, y sin esto se quedaba bajo el terreno hasta andar (suelo negro bajo START).
  if (d->campo && estado == 4 && g_colocado.load() && g_final.load() == 0) {
    const float g = dbz3::suelo_iw::Altura(p.x, p.z, -1e30f);
    if (g_cae) {
      g_y_buena = p.y;
    } else if (g > -1e29f && std::fabs(g - g_y_buena) < 30.0f) {
      g_y_buena = g;
      // Cell READY: el original conserva los pies en el suelo (EE18236..18296).
      // No retener aqui el margen de .05u del suelo nativo de B3: tambien
      // desplaza el ancla y la camara. El resto de indices conserva su margen.
      if (!g_anda.load() && (d->indice == 14 || std::fabs(g - p.y) > 0.05f)) {
        if (d->indice == 14 && std::getenv("DBZ3_TEST_IWMINI_CAMLOG") && std::fabs(g-p.y) > 0.0001f) {
          static unsigned medidas_cell = 0;
          if (medidas_cell++ < 8)
            REXLOG_INFO("dbz3 iwmini: Cell suelo pies_antes={} suelo={} delta={}",p.y,g,g-p.y);
        }
        p.y = g;
        Poner(base, x0, p);
      }
    }
  }
  {   // R16: entrada (0x11D y la de la variante, sin mando, reloj parado + cartel 140 cuadros) y final (0x2F / 0x30)
    // DS112: IW solo pone la pose 0x11D (y el clip 0x296) al indice 1: MgPly_Enter_State0A (EU 0x3185A0) se alcanza
    // solo desde la rama `if (index == 1)` de LoadWaitMinigameIntro (EU 0x29BB70); los demas indices pasan directamente
    // a s_SPACE (EU 0x29BFF0), que pone la animacion de su variante y el clip de camara de su indice (0x41A en el 10).
    // Antes se reproducia 0x11D + 0x296 en todos: en el 0x7D9 ese clip es del campo 897 (Kaio, suelo 0) y en el 900
    // nacia bajo el terreno (ojo y 9,4 con el #WCF a 21,3 en su columna; log 20261027-003436).
    static int paso = 0, t_paso = 0, sobre_i = 0;
    static bool sobre_hecho = false;
    int& t_fin = g_t_fin;
    const Variante* v = VarianteDe(d->indice,d->cara);
    static bool voz_intro_hecha = false;
    if (estado == 3) paso = 0, t_fin = 0, g_cartel = 0, g_t_entrada = 0, g_t_arranque = 0, g_paso_entrada = 0, sobre_hecho = false, sobre_i = 0, voz_intro_hecha = false;
    if (estado == 4 && g_colocado.load()) {
      ++t_paso;
      const bool idx1 = d->indice == 1, fin_hud = g_final.load() != 0;
      if (paso == 0 && !sobre_hecho && d->camaras.count(kClipSobre)) {
        // estado 0x31: pose 0 y sobrevuelo, clip a clip; START lo salta (EU 0x29A670)
        paso = 5, t_paso = 0, sobre_i = 0, g_salta_sobre = false;
        dbz3::anim::Bucle(x0, LocoDe(d->indice).ready);
        AnimoLog("sobrevuelo", 0);
      } else if (paso == 5) {
        const auto it = d->camaras.find(kClipSobre + sobre_i);
        const bool salta = g_salta_sobre.exchange(false);
        if (salta || it == d->camaras.end() || t_paso >= it->second.nf) {
          t_paso = 0, ++sobre_i;
          if (salta || !d->camaras.count(kClipSobre + sobre_i)) sobre_hecho = true, paso = 0;
        }
      } else if (paso == 0) {
        t_paso = 0, g_cartel = 0, g_t_arranque = 0, g_t_entrada = 0;
        if (idx1) {
          // DS121: orden de IW. El estado de carga (s_SPACE, EU 0x29BFF0) pone la animacion de la variante (0x118) y
          // su clip de camara (0x28A); al acabar (o START), LoadWaitMinigameIntro (EU 0x29BB70, rama index==1)
          // entra en la pose 10 = MgPly_Enter_State0A (EU 0x3185A0: clip 0x296 + anim 0x11D) y de ahi al juego.
          // Antes se hacian los dos al reves (0x11D/0x296 primero), que es lo que DS112 dejo como [H].
          paso = 1;
          if (v) dbz3::anim::Reproducir(x0, v->comienzo, true);
          AnimoLog("entrada", v ? v->comienzo : 0);
          REXLOG_INFO("dbz3 iwmini: entrada de IW (indice 1, anim 0x{:X}, camara 0x{:X})", v ? v->comienzo : 0,
                      d->cam_arranque);
          HablarPose(base, "s_SPACE", d->voz_arranque, d->indice);
        } else if (v) {
          paso = 2;
          dbz3::anim::Reproducir(x0, v->comienzo, true);
          AnimoLog("entrada-2", v->comienzo);
          REXLOG_INFO("dbz3 iwmini: arranque de IW (indice {}, anim 0x{:X}, camara 0x{:X})", d->indice, v->comienzo,
                      d->cam_arranque);
          HablarPose(base, "s_SPACE", d->voz_arranque, d->indice);
        } else {
          paso = 3;
          g_cartel = kCartelCuadros;
          REXLOG_INFO("dbz3 iwmini: entrada acabada: mando al jugador");
        }
      } else if (paso == 1 && (dbz3::anim::EstadoPose() == 2 || t_paso > 480)) {   // tope: la mas larga (0x62C) dura 420
        paso = 2, t_paso = 0;
        if (idx1) {
          // 30/10, PINE en Bubbles: tras 0x118 el jugador entra en el estado 0x2D (EU 0x29BA70: codigo 0, nunca 0x11D) 180 cuadros
          // con la camara 0x296, y de ahi al estado 0 con 0xFA.
          dbz3::anim::Bucle(x0, 0x8F1);
          AnimoLog("entrada-2d", 0x8F1);
          REXLOG_INFO("dbz3 iwmini: estado 0x2D de IW (codigo 0, camara 0x{:X})", d->cam_intro);
        } else if (v) {
          dbz3::anim::Reproducir(x0, v->comienzo, true), AnimoLog("entrada-2", v->comienzo);
        }
      } else if (paso == 2 && (idx1 ? t_paso >= 180 : !v || dbz3::anim::EstadoPose() == 2 || t_paso > 480)) {
        paso = 3, t_paso = 0;
        // EU29BB70: primero termina la presentacion; vuelve a la salida,
        // cancela su camara y llama a299900(0). READY no corre sobre258.
        g_cartel = kCartelCuadros;
        // EU 0x2A4BC0 tras la presentacion. Indice 2: la presentacion acaba en el aire y aterriza con 0x23 (PINE: 0x21 -> 0x23 -> 0).
        if (d->indice == 2) dbz3::anim::Reproducir(x0, LocoDe(2).aterrizar, false, 0, LocoDe(2).ready);
        else dbz3::anim::Bucle(x0, LocoDe(d->indice).ready);
        // 30/10: el actor mira donde la camara (capturas PCSX2 de los 6 campos); el indice 0 tiene su direccion medida, abajo
        if (d->indice != 0) {
          const float yw = g_yaw.load();
          g_dir_x = std::sin(yw), g_dir_z = std::cos(yw);
        }
        if (d->indice == 0) {
          const float l = std::hypot(-0.4f, 0.92f);
          g_dir_x = -0.4f/l;
          g_dir_z = 0.92f/l;
        }
        REXLOG_INFO("dbz3 solIW8: campo READY tras presentacion; indice {} reloj {}",d->indice,kCartelCuadros);
        REXLOG_INFO("dbz3 iwmini: entrada acabada: mando al jugador");
      }
      g_paso_entrada = paso;
      if(d->indice==1)g_bubbles_intro=paso==2?1:(paso==3?2:0);
      g_sin_hud = paso == 5 || paso == 1 || paso == 2 || fin_hud;   // EU 0x29B3B0 / 0x29AA90: los finales tampoco llevan HUD
      g_t_arranque = (idx1 ? paso == 1 : paso == 2) ? g_t_arranque + 1 : 0;
      g_t_entrada = paso == 2 ? g_t_entrada + 1 : 0;
      if (paso == 2 && !voz_intro_hecha && d->voz_intro >= 0 && t_paso >= d->voz_intro_cuadro) {
        voz_intro_hecha = true;
        const int byte = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9));   // 0 = voces japonesas
        const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
        P::OirAdxPs2(P::BuscarIW(), byte == 0 ? "ADX_JPN.AFS" : "ADX_USA.AFS", d->voz_intro, vol);
        REXLOG_INFO("dbz3 iwmini: voz de presentacion (indice {}, ADX {}) en el cuadro {}", d->indice, d->voz_intro, t_paso);
      }
      if (paso == 3 && g_cartel.load() > 0) {
        const int antes=g_cartel.load();g_cartel=antes-1;
        if(antes==1 && idx1 && iwbubbles::Prueba(d->clave))
          REXLOG_INFO("dbz3 gp2 bubbles READY terminado: mando y reloj liberados");
      }
      const int fin = g_final.load();
      if ((fin == 1 || fin == 2) && t_fin == 0) {
        t_fin = 1;
        g_fase_fin = 0;   // DS169: el final empieza en el estado 0x2E (anim + clip de la variante)
        HablarPose(base, fin == 1 ? "2E" : "30", fin == 1 ? d->voz_2e : d->voz_30, d->indice);
        // EU 0x2997F0 Minigame_ResetPlayerTransform: actor al origen y direccion +Z.
        // El AMM conserva las coordenadas del campo, igual que el AMC emparejado.
        g_ancla_fin = {};
        // iw_fix: el combate de B3 acaba YA (como un ring out, pasarela.cpp) y su final (estados 5 y 6: 3 + 6,5 s) corre a la
        // vez que el nuestro (la animacion y CLEAR): antes se esperaba a acabar la animacion y la pantalla se quedaba en
        // negro con el cartel ~11 s mas. Fotograma sigue con la camara y el fundido durante esos estados.
        // SOL4: IW reproduce la pose ANTES de salir del campo. Activar kFin aqui hacia
        // que B3 recolocase al actor y aplicase su fisica mientras la camara seguia el clip IW.
        // PRUEBA (DBZ3_IWMINI_RAFAGA_FIN=1): 40 capturas (una cada 15 pintados) desde que acaba el minijuego
        if (std::getenv("DBZ3_IWMINI_RAFAGA_FIN")) std::ofstream("dbz3_shot.req") << "40 15";
        dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
        // Cierre de la marca de la "pantalla azul" (R16): los codigos 0x62C-0x62E solo existen en el #BSK de DATA_CMN
        // 914 (bloques 0x62C/0x62D/0x62E -> animaciones 53/54/55), que es el de Cell semiperfecto (huesos CEL_*): con
        // Gohan deformaban. DS56: la ficha 14 ya es Cell, asi que VarianteDe(14) los usa. La pantalla azul no era la animacion: era la prueba
        // automatica (teletransporte a la meta, con la camara dentro del modelo). La final va como en IW (primer plano
        // del personaje con CLEAR!/FAILED!). DBZ3_IWMINI_FINAL=0 la apaga; =2 pone la de comienzo (prueba).
        static const char* env_final = std::getenv("DBZ3_IWMINI_FINAL");
        static const bool con_final = !env_final || env_final[0] != '0';
        // prueba de la pantalla azul: DBZ3_IWMINI_FINAL=2 pone la animacion de comienzo (que se ve bien) en el final
        static const bool con_comienzo = env_final && env_final[0] == '2';
        if (v && con_final) dbz3::anim::Reproducir(x0, con_comienzo ? v->comienzo : fin == 1 ? v->bien : v->mal, true);
        if (v && con_final) AnimoLog(fin == 1 ? "final-bien" : "final-mal", con_comienzo ? v->comienzo : fin == 1 ? v->bien : v->mal);
        REXLOG_INFO("dbz3 iwmini: final de IW ({}, anim 0x{:X})", fin == 1 ? "superado" : "no superado",
                    v ? (fin == 1 ? v->bien : v->mal) : 0);
      } else if (t_fin > 0 && fin != 3) {
        PasoFinal(base, mgr, v, fin);
      }
    }
    g_sin_mando = estado == 4 && (paso == 5 || paso == 1 || paso == 2 || g_cartel.load() > 0 || g_final.load() != 0);
    g_reloj_parado = estado != 4 || paso == 5 || paso == 1 || paso == 2 || g_cartel.load() > 0;
    g_clip_sobre = paso == 5 ? kClipSobre + sobre_i : 0;
    g_sin_aura = paso == 5;
    g_t_sobre = float(t_paso);
  }
  const float dx = g_dir_x.load(), dz = g_dir_z.load();
  // EU 0x299EE0 Minigame_MovePlayerByStick: la marcha se compone con la palanca CRUDA (sin normalizar); en diagonal
  // con el teclado son 1,41x, no 1,0x. Los efectos, el giro y la camara siguen usando la normalizada (dx, dz).
  const float avx = g_avance_x.load(), avz = g_avance_z.load();
  // Andar de IW (estados 1-4, +0xB4 = 4.0): sin campo de IW es el de B3 (0.33 por cuadro); en el campo, 4.0 x 0.4 = 1.6
  // en unidades de IW (se anade lo que le falta al de B3). Piccolo (14): el rival de B3 va detras y el juego llevaria al
  // jugador hacia el: la marcha entera es esta, desde donde quedo el cuadro anterior.
  // EU 0x299EE0 Minigame_MovePlayerByStick
  const float andar = VelocidadCampoAp(x0,g_corre.load()?4.0f:1.8f,d->k);
  const bool picc = d->npc == 2;
  static V3 propio;
  // DS91: saliente (Hang+Grow) y pared (0x1F/0x20). Se declaran antes de la marcha porque la condicion de andar
  // los consulta; se reinician al empezar el minijuego (estado 3).
  static int hang_t = 0, pared_t = 0;
  static bool prep_quieto = false;   // 30/10: estado 0x11 (EU 0x2A1860): al saltar en marcha, 5 cuadros sin mover
  static float hang_y = 0;
  // SONDA (DBZ3_SONDA_PICC=1, R16): por cuadro, lo que el juego deja en los dos luchadores entre un dibujo y el siguiente
  if (static const bool sonda = std::getenv("DBZ3_SONDA_PICC") != nullptr; sonda && picc && estado == 4) {
    static int n = 0;
    if (n++ < 900 && (n < 300 || n % 4 == 0)) {
      auto xz = [&](uint32_t x, uint32_t o) { return std::pair{LoadF(base, x + o), LoadF(base, x + o + 8)}; };
      const auto a48 = xz(x0, 48), a64 = xz(x0, 64), a80 = xz(x0, 80), b64 = xz(x1, 64);
      REXLOG_INFO("dbz3 iwmini: sonda picc {} t {} j0 48({:.2f},{:.2f}) 64({:.2f},{:.2f}) 80({:.2f},{:.2f}) propio "
                  "({:.2f},{:.2f}) j1 ({:.2f},{:.2f}) npc ({:.2f},{:.2f}) mira {} {} accion {} anda {} fase {}",
                  n, NowMs() % 100000, a48.first, a48.second, a64.first, a64.second, a80.first, a80.second, propio.x,
                  propio.z, b64.first, b64.second, g_npc_x.load(), g_npc_z.load(), REX_LOAD_U16(x0 + kMiradaA),
                  REX_LOAD_U16(x0 + kMiradaB), REX_LOAD_U16(x0 + 3244), g_anda.load(), g_fase.load());
    }
  }
  if (picc) {
    dbz3::suelo_iw::EnvolverReferencia(propio.x, propio.z, p.x, p.z);
    // Solo el recorrido focal lleva al actor desde suelo_iw::Prueba; no
    // cambiar la fuente de movimiento normal (B3 acerca los dos actores).
    if(std::getenv("DBZ3_TEST_IW_REJILLA_ARMAR") && g_colocado.load()) propio={p.x,0,p.z};
    if (estado == 3 || !g_colocado.load()) {
      propio = d->salida;
      g_npc_x = d->npc_inicio.x;
      g_npc_y = d->npc_inicio.y;
      g_npc_z = d->npc_inicio.z;
    } else if (estado == 4 && g_anda.load() && g_fase.load() == 0) {
      // No se relee del luchador: B3 lo acerca al rival (distancia maxima entre luchadores; medido: con relectura
      // Piccolo "alcanzaba" a 433 en 1,7 s). Las paredes del #WCF: suelo_iw::Mover (R11, Collision_MoveSphere de IW).
      const float a0[3] = {propio.x, p.y, propio.z}, a1[3] = {propio.x + avx * andar, p.y, propio.z + avz * andar};
      float s[3];
      MoverCampo(a0, a1, s);
      propio = {s[0], 0, s[2]};
    }
    p = {propio.x, p.y, propio.z};
  } else if (estado == 4 && g_colocado.load() && g_anda.load() && g_fase.load() == 0 && hang_t == 0 &&
             pared_t == 0 && !prep_quieto) {
    // con el andar libre (g_libre) B3 no anda: la marcha entera es esta; si no, se anade lo que le falta al de B3
    const float paso = g_libre.load() ? andar : std::max(0.0f, andar - kAndarB3);
    const float a0[3] = {p.x, p.y, p.z}, a1[3] = {p.x + avx * paso, p.y, p.z + avz * paso};
    float s[3];
    MoverCampo(a0, a1, s);   // contra las paredes del #WCF (R11); sin campo de IW, s = a1
    p = {s[0], p.y, s[2]};
    Poner(base, x0, p);
  }
  // Scatto (decomp EU: State14_DashStartEnter 0x2A0FC0, State15_DashEnter 0x2A0CD0 + DashMove 0x2A0A30, State16_Enter
  // 0x2A0940 + Update 0x2A04A0; el boton en Minigame_ReadPlayerButtons 0x299520). Por cuadro la energia sube 0.02
  // (tope 100, ExplorationMinigameBodyAnimationsMerge, siempre) y baja 0.5 en las 3 fases. 0x14 y 0x16 no mueven
  // (solo la fisica); 0x15 va a 35 por la palanca (sin palanca, hacia donde mira la camara). Con <= 0 en la frenada:
  // cansado (State16_Update -> Char_EnterState7Transform EU 0x2A34D0: energia 1, +0x1C0 = 0, codigo 0x05 (31 cuadros)
  // y 0x06 (30), luego State08 con 0x07 (31): 92 cuadros sin mando). Despues el boton vale otra vez con lo que haya
  // (1 + 0.02 por cuadro: otro scatto lo vuelve a cansar enseguida). Animaciones: anim::Scatto y anim::Cansado (R10).
  static int t = 0;
  static float fov_iw = 40.0f;
  static bool sube = false;
  // DS89: salto de la maquina de estados de tierra de IW. `salto` = fase de la animacion (1 flexion 0x1F, 2 subida
  // 0x20, 3 aire/caida 0x21/0x22, 4 aterrizaje 0x23); doble = ya se gasto el segundo salto (estado 0x13, 0x29).
  static float y_cae = 0, v_cae = 0, y_antes = 0;
  static int salto = 0, salto_t = 0, parar = 0;
  static bool doble = false, andaba = false;
  // 30/10: estado 0/1 (t_quieto: 300 cuadros en 0 pasan a 1, EU 0x2A49D0), carrera en curso (0x16 -> 0x17), salto en marcha
  // y lo que ha avanzado la cadera en el clip (+0x18E8 = 1: se pasa a la posicion al acabar, EU 0x2A04A0 / 0x2A3CE0)
  static bool est1 = false, carrera = false, salto_marcha = false, g_salto_choque = false;
  static int t_quieto = 0;
  static float raiz_z = 0;
  const Loco& loco = LocoDe(d->indice);
  auto Hornear = [&] {
    const float a0[3] = {p.x, p.y, p.z}, a1[3] = {p.x + dx * raiz_z, p.y, p.z + dz * raiz_z};
    float s[3];
    MoverCampo(a0, a1, s);
    p = {s[0], p.y, s[2]};
    Poner(base, x0, p);
    raiz_z = 0;
  };
  int fase = g_fase.load();
  if (estado == 3) {
    g_energia = 100.0f;
    g_pide_scatto = false;
    g_bebida = 0;
    g_curacion = 0;
    fase = 0;
    t = 0;
    fov_iw = 40.0f;
    y_cae = v_cae = y_antes = 0;
    salto = salto_t = parar = 0;
    hang_t = pared_t = 0;
    hang_y = 0;
    doble = andaba = false;
    prep_quieto = est1 = carrera = salto_marcha = false;
    g_vida = 100.0f, g_golpe = 0, g_gracia = 0, g_muerte = 0;
    g_panel_campo = -1.0f, g_t_panel = 0, g_reloj_campo = {}, g_panel_antes = 0xFFFF;
    g_panel_campo_anim=0;
    g_panel_confirmar=false;
    t_quieto = 0, raiz_z = 0;
    dbz3::anim::ScattoCell(d->indice == 14);
    g_anim_estado = "";
    g_anim_codigo = ~0u;   // DS100: la primera animacion del minijuego sale siempre en el log
  }
  if (estado == 4 && g_colocado.load()) {
    float e = std::min(100.0f, g_energia.load() + 0.02f + float(g_bebida.exchange(0)));
    if (const int g = g_gracia.load(); g > 0) g_gracia = g - 1;
    if (d->indice == 10 && g_final.load() == 0) {
      // 31/10: EU +0x1850 += 0,015 por cuadro de 50 Hz, tope 100 (PINE: 0,015 exacto) = 0,0125 a 60 Hz. Faltaba: el port
      // no recuperaba nunca. En 0x28/0x29 sobre superficie 3 (EU 0x2A5F40) resta 0,07 por cuadro de 50 Hz.
      float v = std::min(100.0f, g_vida.load() + 0.0125f + float(g_curacion.exchange(0)));
      if (fase == 0x27 && t >= 1 && g_gracia.load() <= 0 && dbz3::suelo_iw::TipoSuelo(p.x, p.z, p.y + 2.0f) == 3) v -= 0.0583f;
      if (g_vida.load() > 0.0f) g_vida = v;
    }
    if (g_muerte.load() > 0 && fase != 0x2A) {
      fase = 0x2A, t = 0;
      dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
      dbz3::anim::Reproducir(x0, 0x846, true);
      AnimoLog("muerte", 0x846);
    }
    if (const int golpe = g_golpe.exchange(0); golpe && fase != 0x27) {   // EU 0x29CC90: 0x43 + 50 de gracia; en el aire, EU 0x29E260 con 0x41
      fase = 0x27, t = 0;
      g_gracia = golpe == 2 ? 76 : 102;
      est1 = carrera = andaba = false, parar = 0, raiz_z = 0;
      if (salto != 3 && salto != 4) salto = g_cae ? 3 : 0;
      prep_quieto = false;
      dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
      // golpe 2: rayo, EU 0x29E260 Minigame_State20_Enter: 0x40 en el suelo, 0x41 en el aire
      const uint32_t codigo_golpe=g_cae ? dbz3::anim::kParedReboteAire : golpe == 2 ? dbz3::anim::kParedRebote : 0x843;
      dbz3::anim::Reproducir(x0, codigo_golpe, true);
      AnimoLog("golpe", codigo_golpe);
      LanzarFx(Datos::kFxSBound, p.x, p.y, p.z, dx, dz);
    }
    const bool pide = g_pide_scatto.exchange(false);
    // EU 0x2A0FC0 Minigame_State14_DashStartEnter
    if (fase == 0 && pide && e > 0 && hang_t == 0 && pared_t == 0 && salto != 1) {   // cansado (fase 7) no acepta el boton
      fase = 0x14;
      t = 0;
      est1 = carrera = false, t_quieto = 0, parar = 0, raiz_z = 0;
      doble = false;   // DS180: el scatto sale del estado 0x13; su ley de 0,5/cuadro no sigue en 0x14-0x16
      // DS89: en el aire el scatto de IW es el 0x1C-0x1E (State14/15/16 con +0x2061 != 0), no el 0x19-0x1B de tierra.
      dbz3::anim::Scatto(x0, dbz3::anim::kInicio, g_cae);
      AnimoLog(g_cae ? "dash-aereo-ini" : "dash-ini", g_cae ? 0x1C : 0x19);
      REXLOG_INFO("dbz3 iwmini: scatto (energia {:.1f}, aire {})", e, g_cae);
    }
    static bool cansa = false;   // 31/10: EU 0x29FC1C: energia a 0 en el dash: +0x1C0 = 1; el cansancio llega al acabar el choque o la frenada
    if (estado == 3) cansa = false;
    if (fase >= 0x14 && fase <= 0x16) e -= 0.5f;
    if (fase >= 0x14 && fase <= 0x16 && e <= 0) cansa = true;
    if (fase == 7) {
      // EU 0x2A04A0 Minigame_State16_Update (energia <= 0 al acabar la frenada) -> EU 0x2A34D0
      // Char_EnterState7Transform: estado 7, animacion 5 (31 cuadros). EU 0x2A33F0 Char_UpdateAnimToggle pasa el
      // codigo a 6 (30 cuadros) al acabar 5, y EU 0x2A3300 Minigame_State08_Enter pone el 7 (31 cuadros). Aqui lo
      // lleva anim::Cansado (fase 1 = 0x805, fase 2 = 0x806) y a los 61 cuadros se pide el 0x807, igual.
      // EU 0x2A3300 Minigame_State08_Enter (estado 8, animacion 7: la recuperacion)
      // 30/10: cuadros reales de 0x05 + 0x06 en el banco de campo (24 + 38; Cell 25 + 39), y 0x07 = 34
      const int t_rec = d->indice == 14 ? 64 : 62;
      if (++t == t_rec) dbz3::anim::Reproducir(x0, 0x807, true), AnimoLog("recuperacion", 0x807);
      // EU 0x1D55A0 AuraBurstGlowEffect (+ EU 0x2A30B0 Minigame_State09_AuraEnter_): aura del cansancio. El 934/9
      // del paquete de campo esta hecho de niebla (t34) y remolino (t37): es el vapor que se ve en el video. Se
      // emite cada 12 cuadros, como el aura del dash (el original la crea con AuraBurstGlow en el estado 9).
      if (t % 12 == 0) LanzarFx(Datos::kFxAura, p.x, p.y, p.z, dx, dz);
      if (t >= t_rec + 34) fase = 0;
    } else if (fase == 0x2A) {
      if (t == 0 && dbz3::anim::EstadoPose() == 2) {
        t = 1;
        dbz3::anim::Reproducir(x0, 0x847, true);
        AnimoLog("muerte", 0x847);
      }
    } else if (fase == 0x27) {
      // EU 0x29CF30 / 0x29CBE0 / 0x29C940: 0x43 -> 0x44 -> 0x45, cada una al acabar su clip (t = paso)
      if (dbz3::anim::EstadoPose() == 2) {
        if (g_cae) {
          fase = 0;
        } else if (t == 0) {
          t = 1;
          dbz3::anim::Reproducir(x0, 0x844, true);
          AnimoLog("golpe", 0x844);
        } else if (t == 1) {
          t = 2;
          dbz3::anim::Reproducir(x0, 0x845, true);
          AnimoLog("golpe", 0x845);
        } else {
          fase = 0;
        }
      }
    } else if (fase == 0x19) {
      // EU 0x29F8C0: acaba con su clip, o al tocar suelo si iba en el aire (entonces aterriza con 0x23)
      ++t;
      if (!g_cae && g_salto_choque) salto = 4, salto_t = 0, fase = 0, g_salto_choque = false;
      else if (dbz3::anim::EstadoPose() == 2 && t > 2) {
        fase = 0;
        if (g_cae) salto = 3, salto_t = 0;
        // 31/10: PINE, palanca y dash contra pared con energia 0: 0x3D -> estado 7 (0x5, 0x6) -> 8 (0x7). EU 0x29FC1C: +0x1C0 = 1.
        if (!g_cae && cansa) {
          cansa = false, e = 1.0f, fase = 7, t = 0;
          dbz3::anim::Cansado(x0, true);
          AnimoLog("cansado", 0x805);
        }
      }
    } else if (fase == 0x14) {
      // EU 0x2A0CD0 Minigame_State15_DashEnter
      if (++t >= dbz3::anim::CuadrosScatto(dbz3::anim::kInicio, g_cae)) {   // fin de la animacion (0x19: 15; EU 0x2A0EE0)
        fase = 0x15;
        t = 0;
        sube = true;
        // el polvo del arranque (IW State15_DashEnter: Effect 0x34 en el jugador, hacia donde mira la camara)
        g_polvo_x = p.x, g_polvo_y = p.y, g_polvo_z = p.z, g_polvo_dx = dx, g_polvo_dz = dz;
        if (!g_cae) ++g_polvo_n;
        // EU 0x2A0CD0 Minigame_State15_DashEnter: DispatchJumpTable_4984b8(0) (EU 0x2E8A80: AMO en el hueso del
        // jugador, tipo 4 = aura) + Effect_DispatchCommandById(0x34) (EU 0x2F70B0 EffectCmd_AtTarget_Minigame, AMO
        // tipo 0x1A a 10 u del objetivo) + DispatchJumpTable_4984b8(1) (flag 0x100 de aura). El port usa los #AME
        // con nombre del 934: D Start (934/14) y Dash (934/13); el aura va en 934/9 durante 0x14-0x16.
        LanzarFx(Datos::kFxDashStart, p.x, g_cae ? y_cae : p.y, p.z, dx, dz);
        LanzarFx(Datos::kFxDash, p.x, g_cae ? y_cae : p.y, p.z, dx, dz);
        dbz3::anim::Scatto(x0, dbz3::anim::kBucle, g_cae);
        AnimoLog(g_cae ? "dash-aereo" : "dash", g_cae ? 0x1D : 0x1A);
      }
    } else if (fase == 0x15) {
      // EU 0x2A0CD0 Minigame_State15_DashEnter (llama a Minigame_MovePlayerByStick con +0xB4 = 35: la palanca cruda
      // tambien manda en el scatto, asi que en diagonal da 35 x 1,41)
      const float v = VelocidadCampoAp(x0,35.0f,d->k);
      if (d->campo) {   // contra las paredes del #WCF con la esfera del scatto (escala 0.5, R11)
        const float a0[3] = {p.x, p.y, p.z}, a1[3] = {p.x + avx * v, p.y, p.z + avz * v};
        float s[3];
        MoverCampo(a0, a1, s, 0.5f);
        p = {s[0], p.y, s[2]};
        // 30/10: EU 0x2A8E20: el scatto (estado 0x15) que toca pared entra en el 0x19 (EU 0x29FC60): choque 0x3D en suelo, 0x3E en el aire.
        // PINE: 0x1A -> 0x3D (44 cuadros) -> 0, sin gastar energia. [H] los efectos: se usan vibracion y SBound, los de pared.
        if (std::hypot(s[0] - a1[0], s[2] - a1[2]) > 0.1f * std::fabs(v) + 0.1f) {
          fase = 0x19;
          t = 0;
          const uint32_t c = d->indice == 14 ? (g_cae ? dbz3::anim::kCellChoqueAire : dbz3::anim::kCellChoque)
                                             : (g_cae ? dbz3::anim::kDashAereo : dbz3::anim::kDashAereoIni);
          dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
          dbz3::anim::Reproducir(x0, c, true);
          AnimoLog("choque", c);
          g_salto_choque = g_cae;
          LanzarFx(Datos::kFxSBound, p.x, p.y, p.z, dx, dz);
        }
      } else {
        p = {p.x + avx * v, p.y, p.z + avz * v};
      }
      Poner(base, x0, p);
      if (t % 12 == 0) LanzarFx(Datos::kFxAura, p.x, g_cae ? y_cae : p.y, p.z, dx, dz);   // 934/9: aura del dash
      if (sube) {   // +0x250 = 0: sube +2.5 hasta 50 y pasa a bajar (State15_DashMove)
        fov_iw += 2.5f;
        if (fov_iw >= 50.0f) fov_iw = 50.0f, sube = false;
      } else {      // +0x250 = 1: baja -0.38 hasta 40 (no hasta 35; el 35 es de la frenada)
        fov_iw = std::max(40.0f, fov_iw - 0.38f);
      }
      if (++t >= dbz3::anim::CuadrosScatto(dbz3::anim::kBucle, g_cae)) {   // fin de la animacion (0x1A: 15, medido; EU 0x2A0B90)
        fase = 0x16;
        t = 0;
        dbz3::anim::Scatto(x0, dbz3::anim::kFin, g_cae);
        AnimoLog(g_cae ? "dash-aereo-fin" : "dash-fin", g_cae ? 0x1E : 0x1B);
        // EU 0x2A0940 Minigame_State16_Enter: DispatchJumpTable_4984b8(0) vuelve a crear el AMO de aura en el
        // hueso del jugador al entrar en la frenada (aqui, el aura 934/9 una vez mas).
        LanzarFx(Datos::kFxAura, p.x, g_cae ? y_cae : p.y, p.z, dx, dz);
      }
    } else {
      // EU 0x2A0940 Minigame_State16_Enter
      if (fase == 0x16) fov_iw = std::max(35.0f, fov_iw - 0.38f);   // frenada: -0.38 hasta 35 (State16_Move); fuera de ella IW no lo toca
      if (fase == 0x16) {
        // la frenada (0x1B, 22 cuadros) acaba tambien en anim_iw (si no, andar la rearranca); sin energia, el cansancio
        // viene despues de la frenada entera (IW State16_Update lo mira al acabar 0x1B; auditoria7/K)
        const bool acabada = (++t >= dbz3::anim::CuadrosScatto(dbz3::anim::kFin, g_cae) && dbz3::anim::FaseScatto() == dbz3::anim::kApagado) || t >= 90;
        if (float r[3]; dbz3::anim::RaizActual(base, x0, r)) raiz_z = std::hypot(r[0], r[2]);   // 0x1B avanza 18 u: se quedan al acabar
        if (acabada) {
          Hornear();
          if (g_cae) {   // EU 0x2A04A0: en el aire vuelve al estado 0x0E
            salto = 3, salto_t = 0;
            dbz3::anim::Reproducir(x0, loco.aire, true);
            AnimoLog("salto-aire", loco.aire);
          }
        }
        if (acabada && e <= 0) {
          e = 1.0f;
          fase = 7;
          t = 0;
          dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
          // EU 0x2A34D0 Char_EnterState7Transform (estado 7, animacion 5; anim::Cansado lleva 5 y 6)
          dbz3::anim::Cansado(x0, true);
          AnimoLog("cansado", 0x805);
          // DS91: el estado 7/9 de IW lanza AuraBurstGlowEffect + Effect_DispatchSetupByType(6); el aura 934/9 es
          // lo unico que trae el paquete de campo para ese momento. El vapor no esta en el #AME del jugador
          // (ver INFORME §4: [H]); el cartel "REST" era inventado y se quito en DS115.
          LanzarFx(Datos::kFxAura, p.x, p.y, p.z, dx, dz);
          REXLOG_INFO("dbz3 iwmini: scatto: sin energia (cansado)");
        } else if (acabada) {
          if (t >= 90) dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
          fase = 0;
        }
      }
    }
    // DS89: el scatto aereo tambien cae. En IW la fisica de State0F/State0E sigue corriendo en State14-16 (solo cambia
    // el codigo de animacion); aqui g_libre es false durante el scatto, asi que la gravedad se aplica en este bloque.
    if (estado == 4 && g_colocado.load() && !g_cae && fase >= 0x14 && fase <= 0x16 && d->campo) {
      // 30/10: EU 0x2A8E20: en el scatto, un desnivel de mas de 20 lo deja en el aire (PINE: 0x1A -> 0x1E -> 0x21 -> 0x23)
      const float hs0 = dbz3::suelo_iw::Altura(p.x, p.z, -1e30f);
      if (hs0 > -1e29f && y_antes - hs0 > 11.0f) g_cae = true, y_cae = y_antes, v_cae = 0;
      else if (hs0 > -1e29f) y_antes = hs0;
    }
    if (estado == 4 && g_colocado.load() && g_cae && fase >= 0x14) {
      const float esc = d->k / 0.4f;
      const float hs = d->campo ? dbz3::suelo_iw::Altura(p.x, p.z, -1e30f) : -1e30f;
      if (hs > -1e29f) {
        v_cae += kGravAire * esc;
        y_cae -= v_cae;
        if (y_cae <= hs) {
          y_cae = hs;
          g_cae = false;
          salto = 4;
          salto_t = 0;
          dbz3::suelo_iw::SueloFijo(0, false);
        } else {
          dbz3::suelo_iw::SueloFijo(y_cae, true);
        }
        y_antes = g_cae ? y_cae : hs;
      }
    }
    g_energia = std::max(0.0f, e);
    FovIW(base, fov_iw);
  }
  g_fase = fase;
  // EU29E9A0: el bloqueo del platano ya dura su clip CA (60 cuadros),
  // pero faltaba reproducirlo y quedaba puesta la carrera/repose anterior.
  static bool anim_resbalon = false;
  if (estado == 3) anim_resbalon = false;
  const bool resbalon = estado == 4 && g_resbalando.load() && g_final.load() == 0 && !g_sin_mando.load();
  if (resbalon && !anim_resbalon) {
    est1 = carrera = andaba = false, parar = 0, t_quieto = 0;
    dbz3::anim::Reproducir(x0, 0x8CA, true);
    AnimoLog("resbalon", 0x8CA);
  }
  anim_resbalon = resbalon;
  // solIWZ EU2A3B10/2A3940/2A37D0:2 termina,3 en bucle y +0.5
  // de energia por cuadro; al llenarse4 (32cuadros). Duraciones del BSK
  // original915:16/18/32,914:17/19/32 (tools/iw_datos_Z.py).
  static int carga=0,carga_t=0;
  if(estado==3 || g_final.load()!=0 || g_sin_mando.load() || resbalon || g_cae || fase!=0) carga=0;
  const uint32_t carga_base=d->indice==14?0x8D3u:0x8D0u;
  if(estado==4 && !g_sin_mando.load() && !g_final.load() && !resbalon && !g_cae && fase==0) {
    // EU2A9440 descarta cuadrado si +185C ya esta a100; no reiniciar
    // 2/3/4 repetidamente al mantener el boton despues de llenar la energia.
    if(!carga && g_cuadrado.load() && !g_anda.load() && g_energia.load()<100.0f) {
      carga=1;carga_t=0;andaba=carrera=false;parar=0;t_quieto=0;
      dbz3::anim::Reproducir(x0,carga_base,true);
      AnimoLog("carga",carga_base);
    } else if(carga==1 && ++carga_t>=(d->indice==14?17:16)) {
      carga=2;carga_t=0;dbz3::anim::Bucle(x0,carga_base+1);
      AnimoLog("carga-bucle",carga_base+1);
    } else if(carga==2) {
      if(!g_cuadrado.load() || g_anda.load())carga=0;
      else {
        g_energia=std::min(100.0f,g_energia.load()+0.5f);
        if(g_energia.load()>=100.0f) {
          carga=3;carga_t=0;dbz3::anim::Reproducir(x0,carga_base+2,true);
          AnimoLog("carga-fin",carga_base+2);
        }
      }
    } else if(carga==3 && ++carga_t>=32)carga=0;
  }
  // Andar libre de IW (auditoria6, decomp EU Char_GetNextActionCode 0x2A9440 y RotatePlayerToMoveDir 0x299D80): en el
  // campo IW no hay guardia ni pasos de combate. Sin scatto ni cansancio, con la entrada acabada y sin final, el
  // luchador corre con la carrera de IW al andar y queda de pie con su reposo al parar (el pivote ya lo orienta).
  // IW anda (codigo 2, 1.8, +0xB4) con |x| o |y| < 0,9 y corre (3, 4.0) por encima (Char_GetNextActionCode 0x2A9440):
  // ya esta en g_corre/andar. Medido en el original (2026-10-25, 0x7D0): 1,63 u/cuadro al correr (= 4,0 x 0,4).
  g_libre = estado == 4 && g_colocado.load() && fase == 0 && carga==0 && !g_sin_mando.load() && g_final.load() == 0 &&
            !g_resbalando.load();
  if (!g_libre.load()) g_pide_salto = false;   // pulsado fuera del andar libre: no salta despues
  if (!g_libre.load() && g_cae && fase < 0x14) {   // la caida no sigue fuera del andar libre (final, fin del minijuego)
    g_cae = false;
    dbz3::suelo_iw::SueloFijo(0, false);
  }
  if (g_libre.load()) {
    // DBZ3_IW_CORRER=808 (hex): otra carrera de IW de las candidatas (0x807/0x808/0x815/0x816 = codigos 7/8/0x15/0x16)
    // DS56: la ficha 14 es Cell semiperfecto y usa SU locomocion de DATA_CMN 914 (anim::kCell*), no la comun GOK_*.
    const bool cell = d->indice == 14;
    // DS89: salto de IW (estados 0x0C-0x13). La bajada de una cornisa y el salto comparten la gravedad de
    // Char_UpdateMovementPhysics; la animacion va por fases, como los estados 0x0C (0x1F), 0x0D (0x20), 0x0E (0x21),
    // 0x0F (0x22) y 0x10 (0x23). El segundo salto en el aire es el estado 0x13 (0x29, +0xB4 = 7.0: mas alto).
    bool cae = g_cae;
    const float esc_salto = d->k / 0.4f;   // 1 con el campo de IW; 0,206 en un escenario de B3 (curso juntado)
    const float hs = d->campo ? dbz3::suelo_iw::Altura(p.x, p.z, -1e30f) : -1e30f;
    // DS91: pared y saliente. El original no usa un estado nuevo para el saliente: los guiones de escenario
    // de combate llaman a StageEvt_SpawnHangEffect (EU 0x1E84D0, efecto 934/5 "Hang Effect") y el
    // StageEvt_SpawnGroundEffect / Grow (934/6). La pose es la del estado de aire que ya hay (0x21), sin inventar
    // ninguna. La pared si tiene estados propios: 0x1F (wall kick, 0x3F) y 0x20 (rebote 0x40 en suelo / 0x41 en aire).
    // Cell (914, huesos CEL_*) NO tiene 0x3F/0x40/0x41: los clips de pared del 204 son GOK_* y deformarian su
    // modelo. En el minijuego 14 no se dispara la reaccion de pared (queda [H] en el informe).
    // 30/10: no eran reacciones a chocar con una pared. EU 0x2A5DE0 / 0x2A5F40 (Char_CheckActionAllowed/2) solo actuan en el indice 10
    // y por el TIPO de la superficie bajo los pies: 3 (0x1F, -10 de vida) y 4 (0x20, -25), con 50 cuadros de gracia (+0x1AC).
    const int tipo_pies = d->indice == 10 && d->campo && g_gracia.load() <= 0 && hang_t == 0 && pared_t == 0
                              ? dbz3::suelo_iw::TipoSuelo(p.x, p.z, (g_cae ? y_cae : p.y) + 18.0f) : 0;
    // 31/10: causa de la lava (PINE con sondas en EU 0x2A91B4): Char_CheckActionAllowed2 exige |y - suelo| <= 0,01 y, al tocar pared,
    // el deslizamiento de Collision_MoveSphere deja y fuera del suelo en ese momento del cuadro (dy 0,06 a 0,65, 25 de 25): junto a una pared no
    // quema. El rayo sale de pies + 18 (cadera + H, H = 9). ponytail: tocar = no quema; el original pega en los pocos cuadros en que el deslizamiento no sube.
    const int toque_cuerpo = d->indice == 10 && d->campo && g_gracia.load() <= 0 && hang_t == 0 && pared_t == 0
                                 ? dbz3::suelo_iw::ToqueEsfera(p.x, (g_cae ? y_cae : p.y) + 9.0f, p.z, 8.0f) : 0;
    const int estado_lava=EstadoIWDeAnim(g_anim_estado);
    const float y_lava=g_cae ? y_cae : p.y;
    const bool quema3=dbz3::iwlava::Superficie3(estado_lava,tipo_pies,y_lava,hs,g_cae);
    // EU2A5DE0 conserva el contacto de cuerpo con torres aun estando en el aire.
    const bool quema4=dbz3::iwlava::Superficie4(estado_lava,tipo_pies,toque_cuerpo,y_lava,hs);
    if(d->indice==10 && std::getenv("DBZ3_TEST_IWMINI"))
      dbz3::iwlava::Medir(std::getenv("TEST_IWX_LAVA"),estado_lava,p.x,y_lava,p.z,hs,
          tipo_pies,toque_cuerpo,g_cae,g_gracia.load(),quema3,quema4,
          tipo_pies==3&&!g_cae&&y_antes-hs<=11.0f&&toque_cuerpo==0,g_vida.load());
    if (tipo_pies == 3 || tipo_pies == 4 || toque_cuerpo == 4) {
      const bool topa = quema4 || quema3;
      if (topa) {
        g_gracia = 76;   // duracion del clip + 50 (EU 0x29E5D0)
        g_vida = g_vida.load() - (quema4 ? 25.0f : 10.0f);
        const float ya = g_cae ? y_cae : p.y;   // en el aire, p.y sigue siendo el suelo; la altura real es y_cae
        float suelo = dbz3::suelo_iw::Altura(p.x, p.z, -1e30f);   // EU 0x1DBE20/0x1DBCE0: efectos al suelo
        if (suelo < -1e29f) suelo = p.y;
        const float h = dbz3::suelo_iw::Altura(p.x + 3.0f * dx, p.z + 3.0f * dz, -1e30f);
        // solIW7: EU1E84D0/1DB7B0 no cambia el estado del jugador: engancha
        // un EFECTO a un hueso del escenario. El nombre Hang no demuestra
        // una mecanica de agarrarse a salientes. No conectar el builtin75
        // de993/994 con este gancho sin medir un estado real del personaje.
        constexpr bool kHangPorGuion = false;
        const bool saliente = kHangPorGuion && g_cae && h > ya + 2.0f && h < ya + 35.0f;
        // DS114: la pose del saliente es la del estado de aire (0x0E, anim 0x21) y el efecto Hang lo cuelga el
        // guion del stage de un hueso del decorado (EU 0x1E84D0 StageEvt_SpawnHangEffect -> EU 0x1DB7B0 s_Hang_Effect,
        // efecto 934/5): se lanza en el punto de agarre (0,9 u dentro de la pared), no en el centro del luchador.
        if (saliente) {
          hang_t = 1, hang_y = ya, v_cae = 0, salto = 3, salto_t = 0;
          doble = false;   // DS180: el saliente es estado 0x0E, no 0x13
          dbz3::anim::Reproducir(x0, cell ? dbz3::anim::kCellAire : dbz3::anim::kSaltoAire, true);
          AnimoLog("saliente", cell ? dbz3::anim::kCellAire : dbz3::anim::kSaltoAire);
          // EU1E84D0: builtin121 de combate, no75 de993/994 -> EU1DB7B0.
          LanzarFx(Datos::kFxHang, p.x + dx * 0.9f, ya, p.z + dz * 0.9f, dx, dz);
          REXLOG_INFO("dbz3 iwmini: saliente: colgado en ({:.1f}, {:.1f}, {:.1f}), borde {:.1f}", p.x, ya, p.z, h);
        } else if (!quema4) {
          // EU 0x29E5D0 Minigame_State1F_Enter
          // 0x1F: wall kick. EU 0x29E5D0 Minigame_State1F_Enter: animacion 0x3F, +0x1850 -= 10 (VIDA; en el port
          // el luchador es invulnerable, +kVida = +kVidaMax cada cuadro), s_PLY_VIB_CTRL(3.0, jugador, 0x80, 0x20)
          // y Effect_DispatchSetupByType(0x0D) -> EU 0x301CB0 Effect_SetupAmoType0F (AMO tipo 0xF billboard en el
          // suelo del jugador: +0x700 y +0x18D0). La comprobacion es la superficie 3 de Char_CheckActionAllowed2
          // (EU 0x2A5F40) con el jugador sobre el suelo. El SBound del 934 es el efecto con nombre mas cercano; va
          // al suelo, como el AMO del original.
          // DS180: EU 0x29E5D0 Minigame_State1F_Enter NO escribe +0xB0 y su Move (0x29E430) solo mantiene la y
          // (o la pega al suelo si no esta en el aire): la vertical la pone la animacion 0x3F. Antes se inventaba un
          // -1,15 x salto. Ahora no se toca v_cae: con pared_t > 0 el bloque de fisica no corre y la y queda
          // congelada esos cuadros, como en el original. [H] la altura real del clip 0x3F (DATA_CMN 915).
          // EU 0x29E5D0 Minigame_State1F_Enter
          salto = 0, salto_t = 0;   // PINE: 0x3F -> 0x16 -> 0x17; con 2, al acabar el clip pasaba a aire y aterrizaje
          doble = false;   // DS180: 0x1F sale del 0x13 y no hereda su ley vertical
          dbz3::anim::Reproducir(x0, dbz3::anim::kParedKick, true);
          AnimoLog("pared-kick", dbz3::anim::kParedKick);
          LanzarFx(Datos::kFxSBound, p.x, suelo, p.z, dx, dz);
          pared_t = 1;
          carrera = est1 = andaba = false;   // 31/10: PINE 0x3F -> 0x16 -> 0x17: al acabar vuelve a arrancar la carrera
          REXLOG_INFO("dbz3 iwmini: pared: wall kick (0x3F); en IW cuesta 10 de vida (aqui invulnerable)");
        } else {
          // EU 0x29E260 Minigame_State20_Enter
          // 0x20: rebote contra la pared. Se entra desde Char_CheckActionAllowed (EU 0x2A5DE0, superficie 4 con el
          // jugador a <= 2 del suelo), que ademas resta 25 de VIDA (el port es invulnerable) y lanza
          // Effect_DispatchSetupByType(0x0E) -> EU 0x301C50 Effect_SetupAmoType10 + CommandById(0x39) (EU 0x2F6FF0)
          // en la posicion del jugador. EU 0x29E260 Minigame_State20_Enter: animacion 0x40 en suelo / 0x41 en el
          // aire, s_PLY_VIB_CTRL(3.0, ...) y Effect_DispatchSetupByType(6) -> EU 0x301F20 Effect_UpdateTimeoutKill
          // (mata a los 30 cuadros el efecto que lanza el evento de la propia animacion). Aqui lo aproxima SBound.
          dbz3::anim::Reproducir(x0, g_cae ? dbz3::anim::kParedReboteAire : dbz3::anim::kParedRebote, true);
          AnimoLog(g_cae ? "pared-rebote-aire" : "pared-rebote", g_cae ? dbz3::anim::kParedReboteAire : dbz3::anim::kParedRebote);
          // EU 0x1DBE20 s_SBound_Effect: el efecto se pega al SUELO en la x/z recibida (Stage_GetGroundHeight).
          LanzarFx(Datos::kFxSBound, p.x, suelo, p.z, dx, dz);
          doble = false;   // DS180: 0x20 tambien sale del 0x13
          pared_t = 1;
          carrera = est1 = andaba = false;   // 31/10: PINE 0x3F -> 0x16 -> 0x17: al acabar vuelve a arrancar la carrera
          REXLOG_INFO("dbz3 iwmini: pared: rebote ({})", g_cae ? "aire 0x41" : "suelo 0x40");
        }
      }
    }
    if (pared_t > 0) {
      if (dbz3::anim::EstadoPose()==2) pared_t=0;   // EU29E430/29E0E0: fin real del clip
      cae = g_cae;
    } else if (hang_t > 0) {
      ++hang_t;
      y_cae = hang_y, v_cae = 0;
      cae = true;
      dbz3::suelo_iw::SueloFijo(hang_y, true);
      // EU1E85A0: builtin120 de combate, no74 de993/994 -> EU1DBCE0.
      // el "Grow" se coloca en el SUELO de la x/z recibida (Stage_GetGroundHeight), no a la altura del saliente.
      if (hang_t == 18) {
        float suelo_grow = dbz3::suelo_iw::Altura(p.x, p.z, -1e30f);
        if (suelo_grow < -1e29f) suelo_grow = p.y;
        // EU 0x1DBCE0 s_Grow_Effect (efecto 934/6)
        LanzarFx(Datos::kFxGrow, p.x, suelo_grow, p.z, dx, dz);
      }
      if (hang_t >= 30) {
        // El guion de IW sube al personaje con el evento de escenario; no hay clip de escalada en +0xB60 (la pose es
        // la de aire 0x0E/0x21). Aqui se aproximo con el mismo borde que detecto la sonda: se le deja sobre el
        // saliente y se entra en el aterrizaje 0x23. [VERIFICAR] la curva.
        const float borde = dbz3::suelo_iw::Altura(p.x + 3.0f * dx, p.z + 3.0f * dz, -1e30f);
        hang_t = 0;
        dbz3::suelo_iw::SueloFijo(0, false);
        if (borde > hang_y + 1.0f && borde < hang_y + 35.0f) {
          p.x += dx * 2.0f, p.z += dz * 2.0f;
          Poner(base, x0, p);
          y_cae = borde;
        }
        salto = 4, salto_t = 0;
      }
      y_antes = hang_y;
    } else if (hs > -1e29f) {
      const bool pide = g_pide_salto.exchange(false);
      // DS180: EU 0x2A8E20 Char_UpdateMovementPhysics: solo se suelta de la cornisa si el suelo NUEVO queda mas
      // de 20 u por debajo (fVar8 = suelo_viejo + c - suelo_nuevo > c + 20; la c se cancela) y el estado es <= 6
      // (andar/correr/freno; no los saltos 0x0C-0x13 ni el aterrizaje 0x10). Antes: > 6*k = 2,4 u, inventado.
      // EU 0x2A8E20 Char_UpdateMovementPhysics
      if (!cae && salto == 0 && y_antes - hs > 11.0f) {   // EU 0x2A8E20: rayo desde cadera + H, cae si (y + cadera + H) - suelo > H + 20: y - suelo > 20 - 9 (cadera a pies + 9, PINE). ponytail: 9 fijo, Goku corriendo (8,4 a 9)
        cae = true, y_cae = y_antes, v_cae = 0, carrera = false;   /* al aterrizar 0x10 -> 0x16 -> 0x17; sin esto se quedaba en 0xA05 */
        doble = false;   // DS180: esta caida es el estado 0x0E, no el 0x13
        salto = 3, salto_t = 0;
        dbz3::anim::Reproducir(x0, loco.aire, true);   // estado 0x0E: su clip es 0x21; 0x22 es de cuando este acaba
        AnimoLog("salto-aire", loco.aire);
      }
      if (pide && !cae && salto == 0) {   // salto: 0x0C -> 0x0D, o en marcha 0x11 -> 0x12 (Char_GetNextActionCode EU 0x2A9440)
        salto_marcha = g_anda.load();
        prep_quieto = salto_marcha;
        doble = false, salto = 1, salto_t = 0, andaba = false, parar = 0;
        est1 = carrera = false, t_quieto = 0, raiz_z = 0;
        dbz3::anim::Reproducir(x0, salto_marcha ? loco.vuelo : loco.prep, true);
        AnimoLog(salto_marcha ? "salto-vuelo" : "salto-prep", salto_marcha ? loco.vuelo : loco.prep);
        g_polvo_x = p.x, g_polvo_y = p.y, g_polvo_z = p.z, g_polvo_dx = dx, g_polvo_dz = dz;
        ++g_polvo_n;
      } else if (salto == 1 && !cae) {
        // EU 0x2A2AC0 / 0x2A1890: al acabar la flexion despega. +0x18E8 = 3 (EU 0x2A29A0 / 0x2A1760): la altura de la cadera
        // pasa a la posicion: los clips del aire llevan la cadera a 1 u, no a 10.
        if (float r[3]; dbz3::anim::RaizActual(base, x0, r)) raiz_z = r[1];
        if (dbz3::anim::EstadoPose() == 2) {
          cae = true, y_cae = hs + raiz_z * esc_salto, v_cae = -kJumpV * esc_salto;
          salto = 2, salto_t = 0, prep_quieto = false, raiz_z = 0;
          dbz3::anim::Reproducir(x0, salto_marcha ? loco.despegue : loco.sube, true);
          AnimoLog(salto_marcha ? "despegue" : "salto-sube", salto_marcha ? loco.despegue : loco.sube);
        }
      } else if (pide && cae && !doble) {   // estado 0x13 (State13_JumpEnter 0x2A1420): 0x29, +0xB0 = 7.0
        // EU 0x2A1420 Minigame_State13_JumpEnter
        v_cae = -kDobleV * esc_salto, doble = true, salto = 2, salto_t = 0, andaba = false, parar = 0;
        dbz3::anim::Reproducir(x0, loco.doble, true);
        AnimoLog("salto-doble", loco.doble);
        g_polvo_x = p.x, g_polvo_y = p.y, g_polvo_z = p.z, g_polvo_dx = dx, g_polvo_dz = dz;
        ++g_polvo_n;
      }
      if (cae) {
        // EU 0x2A10A0 Minigame_State13_JumpMove
        // DS180: EU 0x2A10A0 mueve primero (y += +0xB0) y despues frena (+0xB0 -= 0,5);
        // al bajar sigue con la misma ley (+0,5/cuadro), no pasa a la gravedad de 0x0E. Se hace tal cual para que
        // la subida discreta sea la del original (52,5 u en 14 cuadros; la continua 7^2/(2*0,5) = 49 es solo la
        // aproximacion). El salto simple conserva su orden (gravedad antes de mover), que es el calibrado a 41,1 u.
        if (doble) {
          y_cae -= v_cae;
          v_cae += kDobleGrav * esc_salto;
        } else if (salto == 2) {   // 0x0D / 0x12: mueve y despues frena
          y_cae -= v_cae;
          v_cae += (salto_marcha ? 7.5f / float(loco.t_despegue) : kJumpV / float(loco.t_sube)) * esc_salto;
        } else {                   // 0x0E y, cuando su clip de 40 acaba, 0x0F
          y_cae -= v_cae;
          v_cae += (salto_t >= 40 ? kGravCae : kGravAire) * esc_salto;
        }
        static const bool log_salto = std::getenv("DBZ3_IWMINI_SALTO_LOG") != nullptr;
        if (log_salto) REXLOG_INFO("dbz3 iwmini: salto y {:.2f} v {:.3f} fase {}", y_cae, v_cae, salto);
        if (y_cae < hs + 3.0f && (salto != 2 || doble) && v_cae >= 0) {   // EU 0x2A2300: posicion < suelo + 3
          cae = false;
          salto = 4, salto_t = 0;
          doble = false;   // DS180: al aterrizar se sale del estado 0x13 (la proxima caida usa 0x0E)
          dbz3::suelo_iw::SueloFijo(0, false);
          g_polvo_x = p.x, g_polvo_y = p.y, g_polvo_z = p.z, g_polvo_dx = dx, g_polvo_dz = dz;
          ++g_polvo_n;
        } else {
          dbz3::suelo_iw::SueloFijo(y_cae, true);
        }
      }
      y_antes = cae ? y_cae : hs;
    }
    g_cae = cae;
    if (pared_t > 0) {
      // DS91: la pose 0x3F/0x40/0x41 de la pared se mantiene los cuadros de `pared_t`; el salto no la pisa.
    } else if (salto == 1) {
      // flexion: la lleva el bloque de arriba, sin pasar por el reposo
    } else if (cae) {
      ++salto_t;
      if (salto == 0) {
        salto = 3, salto_t = 0;
        dbz3::anim::Reproducir(x0, loco.aire, true);
        AnimoLog("salto-aire", loco.aire);
      } else if (salto == 2 && !doble && dbz3::anim::EstadoPose() == 2) {   // EU 0x2A28B0 / 0x2A1670: fin de 0x20 / 0x25
        salto = 3, salto_t = 0, v_cae = 0;
        dbz3::anim::Reproducir(x0, loco.aire, true);
        AnimoLog("salto-aire", loco.aire);
      } else if (salto == 3 && salto_t == 40) {   // EU 0x2A2300: fin de 0x21 sin tocar suelo -> 0x0F
        dbz3::anim::Reproducir(x0, loco.cae, true);
        AnimoLog("salto-cae", loco.cae);
      }
    } else if (salto == 4) {
      // aterrizaje: se ve la pose entera (0x23) y luego se vuelve al reposo/andar
      ++salto_t;
      if (salto_t == 1) {
        dbz3::anim::Reproducir(x0, loco.aterrizar, true);
        AnimoLog("aterrizar", loco.aterrizar);
      }
      // State10_LandUpdate acepta una accion nueva ya en el cuadro 1: si se sigue andando, el aterrizaje no espera los 23.
      if (dbz3::anim::EstadoPose() == 2 || (salto_t >= 2 && g_anda.load())) salto = 0;
    } else {
      salto = 0;
      const bool anda = g_anda.load(), corre = anda && g_corre.load();
      // EU 0x2A3F50 / 0x2A44C0: solo la carrera frena (estado 3 -> 4, codigo 0x18); andar pasa directo al reposo.
      if (corre) {
        andaba = true;
      } else if (andaba && !anda) {
        andaba = false, carrera = false, parar = 1;
        dbz3::anim::Reproducir(x0, loco.freno, true);
        AnimoLog("freno", loco.freno);
      } else {
        andaba = false;
      }
      if (parar > 0) {
        ++parar;
        if (float r[3]; dbz3::anim::RaizActual(base, x0, r)) raiz_z = std::hypot(r[0], r[2]);
        // EU 0x2A3CE0: acaba solo al terminar el clip; +0x18E8 = 1 (la cadera avanzada, a la posicion)
        if (parar > 2 && dbz3::anim::EstadoPose() == 2) parar = 0, Hornear();
      }
      if (parar == 0) {
        if (anda) {
          est1 = false, t_quieto = 0;
          if (corre && !carrera) {   // EU 0x2A42A0 + 0x2A3F50: 0x16 una vez y 0x17 en bucle
            carrera = true;
            dbz3::anim::Reproducir(x0, loco.correr, false, 0, loco.correr2);
            AnimoLog("correr", loco.correr);
          } else if (!corre) {
            carrera = false;
            dbz3::anim::Bucle(x0, loco.andar);
            AnimoLog("andar", loco.andar);
          }
        } else if (est1) {
          // estado 1: sigue su 0x01 -> 0x0B
        } else if (++t_quieto > 300) {   // EU 0x2A49D0: +0x1A0 > 300 -> Char_EnterState1
          est1 = true, carrera = false;
          dbz3::anim::Reproducir(x0, loco.reposo, false, 0, loco.reposo2);
          AnimoLog("reposo-1", loco.reposo);
        } else {
          carrera = false;
          dbz3::anim::Bucle(x0, loco.ready);
          AnimoLog("reposo", loco.ready);
        }
      }
    }
  }
  // Caida fuera del mundo. Dos piezas de IW: Char_UpdateMovementPhysics (EU 0x2A8E20) enciende +0x1CC y lanza el fundido
  // TransitionFilter(0.7, 0x1000, 0x00B09000) cuando el desnivel pasa de 500 o no hay suelo; y
  // ExplorationMinigameBodyAnimationsMerge (EU 0x2AA0A0) devuelve al jugador a +0x1620 cuando su y sale de
  // [-1000, 1000] (fVar22 = -1000 y tambien y > 1000; +5 de alto, fVar21), con +0x1620 = posicion del ultimo objeto
  // recogido (s_SPACE 0x31F280 lo escribe al cogerlo) o +0x840 = la salida al empezar (LoadWaitDragonMissionGround...
  // 0x323840). Aqui: el mismo respawn y el mismo umbral, mas el caso "sin suelo" del port (no hay caida por gravedad sin
  // suelo, asi que se corta al momento); el fundido va en el panel (g_fundido). `con_suelo` queda de reserva si aun no hay
  // punto de reaparicion.
  static V3 con_suelo;
  if (estado == 3 || !g_colocado.load()) {
    con_suelo = d->salida;
    // EU 0x2AA0A0 ExplorationMinigameBodyAnimationsMerge
    g_resp_x = d->salida.x, g_resp_z = d->salida.z, g_resp_hay = true;   // +0x1620 = +0x840 al empezar
  }
  if (d->campo && estado == 4 && g_colocado.load()) {
    constexpr float kSin = -1.0e9f;
    // 7D1 (medido 4/11, andando de verdad y sin piloto automatico): el #WCF del
    // 897 es una malla gruesa y su consulta puntual falla en celdas vacias; el
    // respawn devolvia al jugador al inicio cada ~0,85 s en (-56,-60), con el
    // suelo a la vista (22 reapariciones en 20 s de W). Solo en esta mision se
    // exige ademas que falte la malla DIBUJADA para dar la caida por buena; en
    // los demas campos el criterio queda exactamente como estaba hasta medirlos.
    const bool malla_tb_falta = d->clave != 0x7D1 ||
        dbz3::suelo_iw::AlturaMalla(p.x, p.z, kSin) == kSin;
    const bool sin_suelo = dbz3::suelo_iw::Altura(p.x, p.z, kSin) == kSin && malla_tb_falta;
    if (p.y < -1000.0f || p.y > 1000.0f || sin_suelo) {
      V3 r = g_resp_hay.load() ? V3{g_resp_x.load(), 0.0f, g_resp_z.load()} : con_suelo;
      // D34: IW suma +5 al ultimo objeto/salida (fVar21), pero alli el +0x1620 es una posicion CON suelo; en el port
      // (y con dos aros del 900 en -5) el +5 dejaba al jugador en y = 5 bajo un terreno de 100. Se respeta el +5 solo si
      // queda por encima del terreno; si no, al suelo.
      const float gs = dbz3::suelo_iw::Altura(r.x, r.z, kSin);
      r.y = gs != kSin ? std::max(gs, r.y) + 5.0f : r.y + 5.0f;
      REXLOG_INFO("dbz3 iwmini: caida en ({:.0f}, {:.0f}, y {:.0f}): vuelve a ({:.0f}, {:.0f}, {:.0f})", p.x, p.y, p.z,
                  r.x, r.y, r.z);
      p = r;
      g_fundido = 30;
      Poner(base, x0, p);
    } else {
      con_suelo = p;
    }
  }
  if (g_final.load() != 0) {
    p = g_ancla_fin;
    Poner(base, x0, p);
    REX_STORE_U16(x0 + kMiradaA, uint16_t(-15759));
    REX_STORE_U16(x0 + kMiradaB, uint16_t(-15968));
    MatrizFinal(base, x0);
  }
  // 31/10: PINE 0x7D2: en 0x2C +0x40 = 0 y la raiz sale del clip (0,9, 136, 3522), como en los finales; el clip de camara
  // es del campo. Sin esto el actor quedaba en la salida, fuera del encuadre del clip.
  if (estado == 3) g_en_presenta = false;
  if (estado == 4 && g_final.load() == 0 && (g_paso_entrada == 1 || g_paso_entrada == 2) && d->campo) {
    if (!g_en_presenta) g_ancla_pres = p, g_en_presenta = true;
    Poner(base, x0, V3{});
    REX_STORE_U16(x0 + kMiradaA, uint16_t(-15759));
    REX_STORE_U16(x0 + kMiradaB, uint16_t(-15968));
    MatrizFinal(base, x0);
  } else if (g_en_presenta) {
    g_en_presenta = false;
    p = g_ancla_pres;
    Poner(base, x0, p);
  }
  if (picc) {   // el jugador donde lo lleva la marcha de IW y mirando hacia ella (no hacia Piccolo)
    propio = {p.x, 0, p.z};
    if (!g_en_presenta) Poner(base, x0, p);
    if (estado == 4 && !g_en_presenta) {
      const int32_t ang = int32_t(std::lround(std::atan2(dx, dz) / 6.2831853f * 65536.0f));
      REX_STORE_U16(x0 + kMiradaA, uint16_t(ang - 15759));
      REX_STORE_U16(x0 + kMiradaB, uint16_t(ang - 15968));
    }
    // Piccolo: donde lo pone la IA (MoverNpc), corriendo con el corredor de la IA de IW (anim::Corredor, R10: el ciclo
    // de carrera de Piccolo de DATA_CMN 925) desde que empieza la pelea; lo apaga el final (VigilarCombate)
    static bool corre = false;
    if (estado == 3) corre = false;
    if (estado == 4 && g_colocado.load() && !g_reloj_parado.load() && !corre) corre = true, dbz3::anim::Corredor(x1, true);
    Poner(base, x1, {g_npc_x.load(), g_npc_y.load(), g_npc_z.load()});
    StoreF(base, x0 + kVida, LoadF(base, x0 + kVidaMax));
  } else {
    Poner(base, x1, {p.x + dx * kPivote, 0, p.z + dz * kPivote});
    REX_STORE_U8(x1 + kOcultoA, 1);   // sin cuerpo A ni B (sub_82109D90; tagteam.cpp Veil)
    REX_STORE_U8(x1 + kOcultoB, 1);
  }
  if (estado == 4 && g_colocado.load() && !g_sin_hud.load() && !g_final.load()) {
    // EU299CA0/2AA0A0: +Z local se lleva a la direccion normalizada
    // (-.4,0,.92). Las miradas de B3 no fijan la matriz dibujada.
    // 30/10: en todo el campo y cada cuadro (Minigame_RotatePlayerToMoveDir EU 0x299D80): al dejar de mandarle "andar" a
    // B3, su motor ya no gira al luchador.
    const float rot[12] = {dz,0,-dx,0, 0,1,0,0, dx,0,dz,0};
    for (uint32_t i=0;i<12;++i) StoreF(base,x0+4*i,rot[i]);
    if (std::getenv("DBZ3_TEST_IWMINI_CAMLOG") && g_cartel.load() > 0 && g_cartel.load()%30==0)
      REXLOG_INFO("dbz3 iw campo: READY matriz ({:.6f},0,{:.6f}) (0,1,0) ({:.6f},0,{:.6f})",
                  dz,-dx,dx,dz);
  }
  StoreF(base, x1 + kVida, LoadF(base, x1 + kVidaMax));   // invulnerable: el combate no acaba por KO
  // camara: como IW (auditoria7/H) no la arrastra la marcha (eso, con la marcha relativa a la camara, la hacia girar sin
  // fin con la palanca de lado y temblar hacia atras); la giran la palanca izquierda de lado (Mando) y la derecha
  const float yaw = g_yaw.load();
  // R16: en la entrada la camara da media vuelta alrededor del jugador, de cara a el hasta detras. Las camaras de IW son
  // clips del #AMC general (*(iRam0049ca70 + 0x7EC)): entrada = clip 0x296 (MgPly_Enter_State0A, EU 0x3185A0, que pone
  // ademas la animacion 0x11D al jugador), arranque = un clip por indice (s_SPACE, EU 0x29BFF0) y final = los de
  // State2E/30 (EU 0x29B3B0 / 0x29AA90). Camera_PlayCameraAnim_ (EU 0x2972D0) solo deja el motion y el clip en
  // +0x110/+0x114 y enciende +0x12C; los datos (ojo/objetivo/giro/fov por cuadro) son el #AMC 915 hijo 2.
  // DS112: el clip 0x296 NO es la entrada de todos los campos. MgPly_Enter_State0A solo se alcanza desde la rama
  // `if (index == 1)` de LoadWaitMinigameIntro (EU 0x29BB70), asi que 0x296 (y la pose 0x11D) es SOLO del indice 1
  // (Capture Bubbles!, campo 897 de Kaio, suelo 0: su ojo a 5,5 y su objetivo a 3,0 quedan 5,5/3,0 sobre el suelo).
  // Los demas indices no encienden ninguna camara en la intro y usan Camera_InitFieldDefault (EU 0x297140) +
  // Camera_UpdateFollow (EU 0x297DE0), que es la camara de seguimiento de aqui abajo. En el arranque, s_SPACE si pone
  // clip por indice: 0x258 (0), 0x28A (1), 0x2BC (2), 0x41A (10), 0x4E2 (14), 0x546 (16), todos en coordenadas de su
  // campo y por encima del #WCF (comprobado con el #WCF de DATA_CMN: 0/28, 0/18, 0/42, 0/1 frames enterrados).
  // La media vuelta a mano (orbita) queda solo como respaldo del indice 1 si su copia no trae el #AMC.
  // [VERIFICAR] visual: que la entrada y el resultado queden como los clips de 915/2.
  // GP2: EU29BB70 termina296 ANTES de READY. La RAM de READY+60 tiene
  // ojo(47.7123,24,51.2205), objetivo(0,14,0): seguimiento fijo, no otra
  // media vuelta sintetica. GP2: READY+60 medido en ejecutable: error
  // del ojo <.002u contra RAM PS2; no certifica la pose ni la IA del NPC.
  constexpr bool kCamaraReadyBubblesProbada = true;
  const bool ready_bb_iw = d->indice == 1 && (kCamaraReadyBubblesProbada ||
      (std::getenv("DBZ3_TEST_IWMINI") && std::getenv("DBZ3_TEST_GP2_BUBBLES")));
  const float orbita =
      g_final.load() == 0 && d->indice == 1 && !ready_bb_iw ?
          3.14159265f * float(g_cartel.load()) / float(kCartelCuadros) : 0.0f;
  const float fx = std::sin(yaw + orbita), fz = std::cos(yaw + orbita);
  static V3 seguido;
  const char* fase_csv=std::getenv("TEST_IWX_SEGUIMIENTO");
  const bool medir_seguimiento=d->indice<=2 && fase_csv && *fase_csv && std::getenv("DBZ3_TEST_IWMINI");
  // EU299A70, RAM7D1 autenticada por VSync: tambien .12 usa la referencia
  // previa. Opt-in separado: escribir un CSV no activa el candidato de Goku.
  const bool previa_goku=d->indice<=1 && medir_seguimiento && std::getenv("TEST_IWX_SEGUIMIENTO_PREVIO");
  const bool fase_previa=medir_seguimiento && (d->indice==2 || previa_goku);
  static dbz3::iwseguimiento::Memoria memoria_seguimiento;
  if(!fase_previa)memoria_seguimiento={};
  // Solo cambio de copia periodica (EU299A70); la politica de seguimiento
  // de GP2 queda debajo. Implementacion y periodos en suelo_iw.
  if(!fase_previa)dbz3::suelo_iw::EnvolverReferencia(seguido.x, seguido.z, p.x, p.z);
  // GP2 noche: otra via, sin cambiar coeficiente ni colision. EU299A70
  // toma ancla640 en freno/salto/dash segun work1C8; no siempre pies+9.
  constexpr bool kAnclaSerpienteVerificada = false;
  const bool ancla_original = d->indice==2 && (kAnclaSerpienteVerificada ||
      (std::getenv("DBZ3_TEST_IWMINI") && std::getenv("TEST_GP2_ANCLA")));
  static bool cam_usa_raiz=false;
  if(estado==3 || !g_colocado.load())cam_usa_raiz=false;
  V3 ancla_proxy=p;
  if(ancla_original || fase_previa) {
    const int e=EstadoIWDeAnim(g_anim_estado);
    cam_usa_raiz=dbz3::gp2cam::ModoAnclaOriginal(e,cam_usa_raiz);
    V3 cache;
    if(cam_usa_raiz && PuntoCuerpo(base,x0,0,cache)) {
      V3 ancla=cache;
      // EU208390 y B3 820F9548: punto0 = hueso indicado por el primer s16
      // de la tabla corporal. El cache B3 se calculo ANTES de que este tick
      // cambie posicion/giro con Poner: medido hasta49.47u de desfase en dash.
      // Recalcular desde la matriz que dibuja el modelo, no desde AMM sin
      // jerarquia/mezcla. Sigue bajo el candidato de seguimiento, no normal.
      const uint32_t tabla=REX_LOAD_U32(x0+2956);
      const int indice=tabla>=0x40000000u && tabla<0xA0000000u ?
          int(int16_t(REX_LOAD_U16(tabla))) : -1;
      V3 mundo;
      if(HuesoMundo(base,x0,indice,mundo)) {
        ancla=mundo;
        const char* ruta=medir_seguimiento ? std::getenv("TEST_IWX_ANCLA") : nullptr;
        if(ruta&&*ruta) {
          const uint32_t modelo=REX_LOAD_U32(x0+2952);
          const uint32_t huesos=REX_LOAD_U32(modelo+1032);
          const uint32_t h=huesos+64u*uint32_t(indice);
          float matriz[16];
          for(uint32_t i=0;i<16;++i)matriz[i]=LoadF(base,x0+4*i);
          dbz3::iwseguimiento::MedirAncla(ruta,e,indice,{ancla.x,ancla.y,ancla.z},
              {cache.x,cache.y,cache.z},{LoadF(base,h+48),LoadF(base,h+52),LoadF(base,h+56)},matriz);
        }
      }
      ancla_proxy={ancla.x,ancla.y-9.0f,ancla.z};
    }
    bool local_aplicada=false;
    if(cam_usa_raiz && std::getenv("TEST_GP2_ANCLA_LOCAL")) {
      float r[3];
      if(dbz3::anim::RaizActual(base,x0,r)) {
        ancla_proxy={p.x+dz*r[0]+dx*r[2],p.y+r[1]-9.0f,p.z-dx*r[0]+dz*r[2]};
        local_aplicada=true;
      }
    }
    static int ultimo=-99;
    if(e!=ultimo) {
      ultimo=e;
      REXLOG_INFO("dbz3 gp2 ancla indice={} estado={} raiz={} local={} pies={},{},{} proxy={},{},{}",
          d->indice,e,cam_usa_raiz,local_aplicada,p.x,p.y,p.z,ancla_proxy.x,ancla_proxy.y,ancla_proxy.z);
    }
  }
  const V3 referencia_actual=ancla_proxy;
  const bool reiniciar_seguimiento=estado==3 || !g_colocado.load();
  if(fase_previa) {
    const auto usada=memoria_seguimiento.Elegir({ancla_proxy.x,ancla_proxy.y,ancla_proxy.z},reiniciar_seguimiento);
    ancla_proxy={usada.x,usada.y,usada.z};
    dbz3::suelo_iw::EnvolverReferencia(seguido.x,seguido.z,ancla_proxy.x,ancla_proxy.z);
  }
  // solGP2: EU299A70 usa .3 constante en Serpiente. Candidato apagado
  // hasta cotejar la curva; solo el arnes puede solicitarlo explicitamente.
  constexpr bool kSeguimientoSerpienteProbado = false;
  const bool seguir_iw2 = d->indice == 2 && (kSeguimientoSerpienteProbado ||
      fase_previa || (std::getenv("DBZ3_TEST_IWMINI") && std::getenv("DBZ3_TEST_GP2_SEGUIR")));
  // un salto de sitio (la prueba auto, un reinicio) no se sigue: la camara cruzaria colinas y paredes por el camino
  // eIW7: DBZ3_IW_SEG_ORIGINAL=1 (apagado, sin medir): regla exacta de Minigame_UpdateFollowCamera (EU 0x299A70): coeficiente
  // constante 0,12 (0,3 con +4==2) sin acercamiento extra ni salto a >150 u; el port anade ambos para el scatto.
  static const bool seg_original = std::getenv("DBZ3_IW_SEG_ORIGINAL") != nullptr;
  if (estado == 3 || !g_colocado.load() || (!seg_original && !seguir_iw2 && !previa_goku && DistH(seguido, p) > 150.0f)) seguido = ancla_proxy;
  // con el scatto (35 u por cuadro) se quedaba atras hasta pasar de 150 y saltaba de golpe (cruzando lomas, el ojo
  // dentro del terreno, rafaga 204507): mas alla de 40 u alcanza mas deprisa; corriendo (~33 u de retraso), igual
  const float lejos = DistH(seguido, p);
  // EU 0x299A70 Minigame_UpdateFollowCamera
  const float kseg = seguir_iw2 ? 0.3f : previa_goku ? 0.12f : seg_original ? (d->indice == 2 ? 0.3f : 0.12f) :
      std::min(1.0f, (d->indice == 2 ? 0.3f : 0.12f) + std::max(0.0f, lejos - 40.0f) / 60.0f);
  if(medir_seguimiento) {
    const auto antes=seguido;
    const auto nueva=dbz3::iwseguimiento::Mezclar({seguido.x,seguido.y,seguido.z},
        {ancla_proxy.x,ancla_proxy.y,ancla_proxy.z},kseg);
    seguido={nueva.x,nueva.y,nueva.z};
    dbz3::iwseguimiento::Medir(fase_csv,EstadoIWDeAnim(g_anim_estado),cam_usa_raiz,reiniciar_seguimiento,
        {referencia_actual.x,referencia_actual.y,referencia_actual.z},
        {ancla_proxy.x,ancla_proxy.y,ancla_proxy.z},{antes.x,antes.y,antes.z},nueva);
  } else {
    seguido = {seguido.x + (ancla_proxy.x - seguido.x) * kseg, seguido.y + (ancla_proxy.y - seguido.y) * kseg,
               seguido.z + (ancla_proxy.z - seguido.z) * kseg};
  }
  // entrada (cartel): empieza en primer plano, cerca y desde abajo (referencia del usuario 4_goku_pose_entrada: Goku
  // llena el cuadro) y se aleja hasta la de seguir mientras da la media vuelta (auditoria10/C)
  // EU299A70 sigue pies+descriptor[3]; EU297DE0 suma ojo+15 y objetivo+5.
  // Nuestro seguido descuenta las 9 unidades de Goku. Cell usa 10, no 9:
  // RAM 7DD EE18236..18296 y descriptor del ELF, no una escala del modelo HD.
  const float altura_descriptor=d->altura_camara-9.0f;
  float atras = kCamAtras, alto = kCamAlto+altura_descriptor, mira = kCamMiraAlto+altura_descriptor;
  if (orbita > 0.0f) {
    const float s0 = 1.0f - float(g_cartel.load()) / float(kCartelCuadros), s = s0 * s0 * (3.0f - 2.0f * s0);
    // EU 0x29BFF0 s_SPACE
    atras = 11.0f + (kCamAtras - 11.0f) * s;
    alto = 4.0f + (kCamAlto - 4.0f) * s;
    mira = 7.0f + (kCamMiraAlto - 7.0f) * s;
  }
  // EU 0x297DE0 Camera_UpdateFollow
  float ojo[3] = {seguido.x - fx * atras, seguido.y + alto, seguido.z - fz * atras};
  float obj[3] = {seguido.x, seguido.y + mira, seguido.z};
  {   // Inclinacion de IW (Camera_UpdateStickOrbit EU 0x297330 sobre camara+0xE4, 15 grados de Camera_InitFieldDefault
      // 0x297140): girar el vector ojo-objetivo alrededor del eje derecho. Con la palanca quieta (15 grados) no cambia
      // nada de lo medido en R17. [VERIFICAR] el eje exacto (Camera_UpdateFollow 0x297DE0 compone +0xC0/+0xE0/+0xF0).
    const float a = (g_pitch.load() - 15.0f) * kGrados;
    if (std::fabs(a) > 1e-4f) {
      const float hl = atras, vv = alto - mira;
      const float hl2 = hl * std::cos(a) - vv * std::sin(a), vv2 = hl * std::sin(a) + vv * std::cos(a);
      ojo[0] = obj[0] - fx * hl2;
      ojo[1] = obj[1] + vv2;
      ojo[2] = obj[2] - fz * hl2;
    }
  }
  // final (superado o no): primer plano de frente, como la camara animada de IW (Camera_PlayCameraAnim 0x1E en
  // Battle_CharState2fInit; en los videos el personaje de cara, cerca, con CLEAR!/FAILED! encima)
  const bool cerca = g_final.load() == 1 || g_final.load() == 2;
  if (cerca) {   // las poses de resultado de IW miran al reves que la marcha (capturas 134219): el ojo, detras de ella
    float r[3] = {0, 0, 0};   // sigue el salto (0x52: 69 de alto y 50 hacia delante)
    const bool hay = dbz3::anim::RaizActual(base, x0, r);
    const float sube = hay && r[1] > 12.0f ? (r[1] - 12.0f) * 0.9f : 0.0f, delante = hay && r[2] > 0 ? r[2] * 0.9f : 0.0f;
    const float tx = p.x + dx * delante, tz = p.z + dz * delante;
    ojo[0] = tx - dx * 45.0f, ojo[1] = p.y + 14.0f + sube, ojo[2] = tz - dz * 45.0f;
    obj[0] = tx, obj[1] = p.y + 12.0f + sube, obj[2] = tz;
  }
  // DS93: camaras reales de IW del visor de campo. Entrada = clip 0x296 solo en el indice 1 (DS112) y clip de arranque
  // de s_SPACE (cam_arranque) en la fase 2 para todos; resultado = clip de State2E/30 (anclado al luchador en el
  // indice 0 y absoluto en los demas campos). Si el clip no esta en la copia del jugador, se queda la camara del port.
  // DS103: camara de SEGUIMIENTO (la segura) antes de que el clip de IW la sustituya: es el respaldo si el clip
  // nace dentro de la loma y no hay ningun punto del segmento fuera de la malla.
  const float ojo_seg[3] = {ojo[0], ojo[1], ojo[2]}, obj_seg[3] = {obj[0], obj[1], obj[2]};
  bool cam_iw = false;
  float roll_iw = 0;
  {
    float co[3], cb[3];
    // EU 0x3185A0 MgPly_Enter_State0A (solo indice 1): Camera_PlayCameraAnim_(amc, 0x296) + anim 0x11D.
    // EU 0x29BFF0 s_SPACE (todos los indices): Camera_PlayCameraAnim_ con el clip de su indice, sincronizado con la
    // animacion de `Variante::comienzo` (el 0x41A y el 0x49C duran los dos 280 cuadros).
    // DS121: el indice 1 hace PRIMERO s_SPACE (0x28A + 0x118) y DESPUES la pose 10 (0x296 + 0x11D), como
    // LoadWaitMinigameIntro (EU 0x29BB70) -> MgPly_Enter_State0A (EU 0x3185A0). Los demas no tienen pose 10.
    int clip_entrada = 0;
    float cuadro_entrada = 0;
    if (g_final.load() == 0 && g_sin_mando.load()) {
      if (g_clip_sobre) {
        clip_entrada = g_clip_sobre;
        cuadro_entrada = g_t_sobre;
      } else if (g_paso_entrada == 2 && d->indice == 1) {
        clip_entrada = d->cam_intro;
        cuadro_entrada = float(std::max(g_t_entrada - 1, 0));
      } else if (g_paso_entrada == 1 || g_paso_entrada == 2) {
        clip_entrada = d->cam_arranque;
        cuadro_entrada = float(std::max(g_t_arranque - 1, 0));
      }
    }
    if (clip_entrada && CamaraIw(base, mgr, *d, clip_entrada, cuadro_entrada, false, p, co, cb, &roll_iw)) {
      std::copy(co, co + 3, ojo);
      std::copy(cb, cb + 3, obj);
      cam_iw = true;
    }
    if (cerca && CamaraIw(base, mgr, *d,
                          g_fase_fin.load() ? d->cam_resultado : g_final.load() == 1 ? d->cam_bien : d->cam_mal,
                          LoadF(base, x0 + 0x650 + 0x608), d->cam_final_rel, p, co, cb, &roll_iw)) {
      std::copy(co, co + 3, ojo);
      std::copy(cb, cb + 3, obj);
      cam_iw = true;
    }
  }
  // En el campo de IW, como Camera_UpdateFollow (EU 0x297DE0): (1) rayo de T (el punto mirado) al ojo contra lo que se
  // VE, MiniGame_DrawPromptSingle -> Collision_Raycast, mascara 1 (la de la camara; R11), y si corta el ojo queda en el
  // corte; (2) segundo pase de ESFERA (MiniGame_DrawText -> Collision_MoveSphere, mascara 1) con radio = media diagonal
  // de pantalla a la distancia del ojo. Direccion: NO sigue la marcha ni el ultimo aro; es el vector de orbita
  // (iRam0049cbc0+0x40), que Minigame_ReadPlayerStick (EU 0x299250) gira con la palanca izquierda de lado; minijuego a
  // minijuego sale de su camara inicial (kYawOjoIW, medido en el original). Distancia 70 y +15 de alto
  // (Camera_InitFieldDefault 0x297140: +0xC0 = (0,30,-50), +0xE0 = (0,15,0), +0x128 = 70; mira +5: +0xF0).
  // Sin distancia minima: a menos de 10 u IW pinta al jugador con alfa 0,1 (CharacterOutlines(jugador, camara) < 10 ->
  // VisualEffects(-1,8) + Gfx_SetGlobalAlpha(0x3DCCCCCD), fuera de los estados 0x2C..0x31). El port no puede cambiar el
  // alfa del luchador (lo dibuja el guest de B3), asi que ese fundido no esta; queda [VERIFICAR] solo lo visual (que el
  // jugador se vea entero al arrimarse la camara).
  // R17: sin subir el ojo a la altura del terreno de debajo (regla de R16): IW no la tiene y desviaba la camara de la
  // del original (en 0x7DF la dejaba a 29,4 de alto en vez de 24). La loma se resuelve con el rayo de abajo, como IW.
  const float cab[3] = {obj[0], obj[1], obj[2]};
  // D34: TODO se mide contra la MALLA QUE SE DIBUJA (suelo_iw::RayoMalla / AlturaMalla / CercaMalla), no contra el
  // #WCF: en los cantos de acantilado el #WCF da una altura que en pantalla no existe (cara vertical). Ademas el orden
  // importa: DS33 subia el ojo POR ENCIMA del terreno DESPUES del rayo y del barrido. Medido en su propia prueba (log
  // 17:45:20,026, aro 5 del 900): jugador en (1284, -5, -974) y ojo en (1266,6, 127,6, -906,2): la subida de 8 u sobre el
  // #WCF (119,3 en esa columna, donde la malla dibujada no tiene superficie: es el canto de un pozo) dejaba la camara en
  // el borde del pozo, 132 u por encima del jugador; toda la pantalla era la pared del pozo (marron liso, c001). La malla
  // dibujada no corta el segmento (el pozo esta abierto por arriba): no era "pared entre medias" sino el ojo fuera del
  // pozo. Ahora: subir solo 2 u SOBRE LA MALLA en su columna, rayo, barrido, cerca de una cara -> acercarse al objetivo
  // y VOLVER A GARANTIZAR el rayo; nada despues puede volver a meter el ojo en la roca.
  // DS100: esta seguridad se aplica TAMBIEN a los clips de camara de IW (cam_iw). Antes se saltaba entera con
  // `!cam_iw` y los clips absolutos (p. ej. la entrada 0x296 del 0x7D9) podian nacer dentro de una pared: el
  // primer fotograma del clip es interior de terreno con un trozo blanco. Para el clip se hace solo lo que pide el
  // original (Camera_UpdateFollow): rayo contra la malla DIBUJADA (#WCF/malla, suelo_iw::RayoMalla) y salir del
  // volumen; no se aplican el barrido de esfera ni el alejamiento a 10 u, que son del seguimiento y desviarian el
  // encuadre de la cinematica.
  // solIWZ EU297DE0: con camara+12C=1 se ejecuta SOLO127280 (AMC),
  // sin colision. Serpiente tampoco usa las elevaciones de20..120 de esta
  // red del port: READY medido (0,59.2796,3486.095), no (-9,82.8,3484.8).
  if (d->campo && !cam_iw && !cerca && !g_clip_sobre && d->indice != 2) {
    auto alto_dib = [](float x, float z) {
      const float y = dbz3::suelo_iw::AlturaMalla(x, z, -1e30f);
      return y > -1e29f ? y : dbz3::suelo_iw::Altura(x, z, -1e30f);
    };
    // (0) ojo FUERA del campo: Altura/AlturaMalla solo miran dentro de su ventana (900: x/z -1500..1500); fuera no hay
    // dato y detras del borde del terreno no se ve nada (negro o el color de limpieza). Se acerca el ojo al objetivo.
    if (alto_dib(cab[0], cab[2]) > -1e29f)
      for (int i = 0; i < 12 && alto_dib(ojo[0], ojo[2]) <= -1e29f; ++i)
        for (int c = 0; c < 3; ++c) ojo[c] = cab[c] + (ojo[c] - cab[c]) * 0.8f;
    // DS103: clips de camara de IW (entrada 0x296). El clip es ABSOLUTO en el campo y nace dentro de la loma
    // (ojo 14,6 con la malla dibujada a 21,1): subir el ojo a +2 de la malla lo mete en el techo, el rayo T->ojo
    // corta y "recorta" lo devuelve al hueco (la captura: camara en la pared y bloque blanco). Para los clips NO se
    // sube: se acerca al objetivo hasta un punto FUERA de la malla dibujada (por encima de su columna) y con el rayo
    // libre. Si no existe, se usa la camara de seguimiento, que si tiene la seguridad completa de abajo.
    if (cam_iw) {
      auto dentro = [&](const float q[3]) {
        const float h = alto_dib(q[0], q[2]);
        if (h > -1e29f && q[1] < h + 2.0f) return true;   // dentro de la loma (o del plano cercano)
        float c[3];
        return RayoCamara(cab, q, c);                      // pared entre el objetivo y el ojo
      };
      if (dentro(ojo)) {
        bool fuera = false;
        for (int i = 1; i <= 12 && !fuera; ++i) {   // 8 % por paso hacia el objetivo
          const float t = 1.0f - 0.08f * float(i);
          const float q[3] = {cab[0] + (ojo[0] - cab[0]) * t, cab[1] + (ojo[1] - cab[1]) * t,
                              cab[2] + (ojo[2] - cab[2]) * t};
          if (!dentro(q)) {
            std::copy(q, q + 3, ojo);
            fuera = true;
          }
        }
        if (!fuera) {   // el clip entero esta soterrado: camara de seguimiento (segura)
          std::copy(ojo_seg, ojo_seg + 3, ojo);
          std::copy(obj_seg, obj_seg + 3, obj);
        }
      }
    } else {
      // (1) el ojo, por encima de la superficie dibujada de su columna (2 u: el plano cercano de B3). ANTES de la colision.
      const float h_ojo = alto_dib(ojo[0], ojo[2]);
      if (h_ojo > -1e29f && ojo[1] < h_ojo + 2.0f) ojo[1] = h_ojo + 2.0f;
    }
    // (2) rayo T -> ojo contra la malla dibujada (IW: MiniGame_DrawPromptSingle). Si corta, primero se prueba a mirar
    // desde mas alto (hasta 120 u, red del PORT: IW no la tiene); si tampoco, el ojo 2 u antes del corte por T (IW lo
    // deja EN el corte, +0,1 %, pero B3 no funde al luchador y su plano cercano entra en la roca).
    float corte[3];
    bool tapado = RayoCamara(cab, ojo, corte);
    for (float sube = 20.0f; !cam_iw && tapado && sube <= 120.0f; sube += 20.0f) {
      const float alto[3] = {ojo[0], ojo[1] + sube, ojo[2]};
      if (float c2[3]; !RayoCamara(cab, alto, c2)) {
        ojo[1] = alto[1];
        tapado = false;
      }
    }
    auto recorta = [&](const float c[3]) {   // ojo 2 u antes del corte, por el lado del objetivo
      const float vx = c[0] - cab[0], vy = c[1] - cab[1], vz = c[2] - cab[2];
      const float l = std::sqrt(vx * vx + vy * vy + vz * vz);
      const float k2 = l > 2.0f ? (l - 2.0f) / l : 0.5f;
      for (int i = 0; i < 3; ++i) ojo[i] = cab[i] + (c[i] - cab[i]) * k2;
    };
    if (tapado) recorta(corte);
    // (3) segundo pase de IW: barrido de esfera (Collision_MoveSphere, mascara 1). suelo_iw::Mover centra la esfera en
    // desde.y + kBajoEsfera (8) + radio (4 x 2,5 = 10) = desde.y + 18: se le da un desde a la altura del ojo para que el
    // barrido sea el de la camara. Solo x/z; la y la fija la malla en el paso (1). En los clips de IW no se usa.
    if (!cam_iw) {
      const float desde[3] = {cab[0], ojo[1] - 18.0f, cab[2]};
      float s[3];
      dbz3::suelo_iw::Mover(desde, ojo, s, 2.5f);
      ojo[0] = s[0], ojo[2] = s[2];
    }
    // (4) garantia final: si el barrido dejo el ojo detras de una cara, otra vez 2 u antes del corte. Nada lo deshace.
    if (RayoCamara(cab, ojo, corte)) recorta(corte);
    // (4b) el plano cercano de B3 (~1-2 u) no puede quedar dentro de una ladera: si el ojo esta a menos de 3 u de una
    // cara de la malla dibujada (sin que el rayo la corte: la cara esta al lado/por detras), se acerca al objetivo.
    // Es el "plano cercano dentro de una ladera" de las fotos marrones (la textura del terreno llena la pantalla).
    for (int i = 0; i < 10 && dbz3::suelo_iw::CercaMalla(ojo, 3.0f); ++i)
      for (int c = 0; c < 3; ++c) ojo[c] = cab[c] + (ojo[c] - cab[c]) * 0.7f;
    if (RayoCamara(cab, ojo, corte)) recorta(corte);
    // (5) a menos de 10 u IW funde al luchador (alfa 0,1); B3 no, y se veia el interior de la cabeza. Subir de 4 en 4
    // hasta pasar de 10, pero solo si eso no vuelve a tapar el ojo. Solo para la camara de seguimiento.
    auto dist3 = [&] {
      const float ax = ojo[0] - cab[0], ay = ojo[1] - cab[1], az = ojo[2] - cab[2];
      return std::sqrt(ax * ax + ay * ay + az * az);
    };
    for (int i = 0; !cam_iw && i < 8 && dist3() < 10.0f; ++i) {
      const float alto[3] = {ojo[0], ojo[1] + 4.0f, ojo[2]};
      if (float c3[3]; RayoCamara(cab, alto, c3)) break;
      ojo[1] = alto[1];
    }
  }
  // DS105: pasada FINAL con la malla de COLISION del campo 900 (suelo_iw::Rayo, el Collision_Raycast de IW), como el
  // original: EU 0x297DE0 Camera_UpdateFollow -> MiniGame_DrawPromptSingle (EU 0x2966B0) -> Collision_Raycast(escena+0x1E0,
  // objetivo, ojo, mascara 1); si corta, el ojo queda EN el corte y 0,1 % hacia el objetivo (FUN_001424e0 con
  // 0x3A83126F = 0,001). El port medía la malla DIBUJADA y podia dejar el ojo dentro de la roca (a_salida / b_corriendo:
  // solo pared). Con la malla de colision el ojo sale siempre al lado libre del corte, tambien en los clips de entrada.
  // El margen se sube a 1,2 u (plano cercano de B3) para que el near plane no quede dentro de la cara; es el segundo
  // pase de esfera de IW (MiniGame_DrawText, EU 0x2964F0 -> Collision_MoveSphere, radio = diagonal del plano cercano).
  if (d->campo && !cam_iw) {
    float corte[3];
    if (dbz3::suelo_iw::Rayo(cab, ojo, 1, corte)) {
      const float vx = cab[0] - corte[0], vy = cab[1] - corte[1], vz = cab[2] - corte[2];
      const float l = std::sqrt(vx * vx + vy * vy + vz * vz);
      const float k = l > 1.2f ? 1.2f / l : 1.0f;   // 1,2 u desde la cara, del lado del objetivo
      for (int i = 0; i < 3; ++i) ojo[i] = corte[i] + (cab[i] - corte[i]) * k;
    }
  }
  {   // D34: el ojo de la camara para el culling de los geiseres (Dibujar) y la medida de la traza de abajo.
    g_ojo_x = ojo[0], g_ojo_y = ojo[1], g_ojo_z = ojo[2];
    g_ojo_ok = g_estado.load() == 2;
  }
  // DS177: la camara de la carrera/entrada es la que se DIBUJA: el gancho de sub_820F4300 la escribe en la struct
  // activa antes de construir la vista (DS163) y publica la foto para la escena 3D del panel. Antes solo se escribia
  // mgr+0x60, que no es la entrada del render: el panel veia una camara y Goku se dibujaba con otra (fov/ojo distintos).
  CamaraMinijuego(true, ojo, obj, g_fov_campo.load(), roll_iw);
  if (ready_bb_iw && std::getenv("DBZ3_TEST_GP2_BUBBLES") && g_cartel.load()==80)
    REXLOG_INFO("dbz3 gp2 bb-ready edad=60 codigo={:X} cuadro={} ojo={},{},{} objetivo={},{},{}",
        REX_LOAD_U32(x0+0x650+0x520)&0xFFF,LoadF(base,x0+0x650+0x608),
        ojo[0],ojo[1],ojo[2],obj[0],obj[1],obj[2]);
  if (std::getenv("DBZ3_TEST_IWMINI_CAMLOG") && g_cartel.load() > 0 && g_cartel.load()%30 == 0) {
    REXLOG_INFO("dbz3 solIW8: campo camara READY {} ojo ({:.4f},{:.4f},{:.4f}) obj ({:.4f},{:.4f},{:.4f}) fov {:.6f}",
        kCartelCuadros-g_cartel.load(),ojo[0],ojo[1],ojo[2],obj[0],obj[1],obj[2],g_fov_campo.load());
    if (d->indice == 14)
      REXLOG_INFO("dbz3 iwmini: Cell READY pies_y={} seguido_y={} altura={}",p.y,seguido.y,d->altura_camara);
  }
  dbz3::cine::Camara(base, ojo, obj);
  {   // registro de la accion (X+3244) y el ataque/animacion (X+2928 & 0xFFF) del jugador al cambiar: andar, correr...
    // DS114: ademas del numero, el NOMBRE del estado (el de anim_iw: reposo, correr, pared-kick, saliente, dash,
    // cansado...) y el estado de IW que reproduce (tabla 0x497230 del ELF) para poder seguir las pruebas. Se publica
    // tambien cuando cambia solo el nombre (misma accion y codigo, p. ej. dash -> dash-fin en el mismo cuadro).
    static uint32_t visto = ~0u;
    static const char* nombre_visto = nullptr;
    const uint32_t ac = REX_LOAD_U16(x0 + 3244), an = REX_LOAD_U32(x0 + 2928) & 0xFFF;
    if (estado == 4 && ((ac << 16 | an) != visto || g_anim_estado != nombre_visto)) {
      visto = ac << 16 | an;
      nombre_visto = g_anim_estado;
      const int iw = EstadoIWDeAnim(g_anim_estado);
      REXLOG_INFO("dbz3 iwmini: accion {} codigo {:03X} estado {} iw {} en ({:.1f}, {:.1f}) anda {} fase 0x{:X}",
                  ac, an, *g_anim_estado ? g_anim_estado : "?", iw < 0 ? "?" : NombreEstadoIW(iw), p.x, p.z,
                  g_anda.load(), unsigned(g_fase.load()));
    }
  }
  // DS103: traza de 0,5 s de lo que el MOTOR tiene puesto en los dos cuerpos (A = X+0x650, B = X+0xCD0), no lo que
  // pedimos: codigo (A+0x520), clip resuelto por el #CSK (A+0x604 = almacen<<16|anim), duracion (A+0x60C), reloj
  // (A+0x608, el que avanza 0x820FA4B8), congelado (A+0x66C), #CSK instalados (A+0x2C0..) y traslacion del hueso 0.
  // DS105: A+0x63C NO es "el cuadro que se dibuja": es el reloj del TERCER clip (A+0x638). La pose que llega al
  // skinning es la que el MODELO tiene en su estado 0 (modelo+0x41C clip / +0x424 cuadro, los consume 0x8208DC60 ->
  // 0x8208CAA0/0x8208D518 -> huesos) y se comprueba con el mapa por almacen (0x8208D2C8) y con un hash de
  // las matrices de hueso (modelo+0x408, el array que 0x8208D380 entrega al render). `extra` = A+0x62C/A+0x638, los
  // clips secundarios que 0x820FAF40 aplica al canal 0 del modelo.
  static int64_t traza_cuerpos = 0;
  if (traza_log && estado == 4 && NowMs() - traza_cuerpos > 500) {
    traza_cuerpos = NowMs();
    auto hueso0 = [&](uint32_t A, float out[3]) -> bool {
      const uint32_t mod = REX_LOAD_U32(A + 0x538), bl = REX_LOAD_U32(A + 0x53C);
      if (mod < 0x10000000u || mod >= 0xF0000000u || bl < 0x10000000u || bl >= 0xF0000000u) return false;
      const uint32_t mb = REX_LOAD_U32(mod + 0x408);
      const int16_t b = int16_t(REX_LOAD_U16(bl));
      if (b < 0 || b > 200 || mb < 0x10000000u || mb >= 0xF0000000u) return false;
      for (int i = 0; i < 3; ++i) {
        const uint32_t u = REX_LOAD_U32(mb + 64u * uint32_t(b) + 0x30u + 4u * uint32_t(i));
        std::memcpy(&out[i], &u, 4);
      }
      return true;
    };
    auto cuerpo = [&](int c) {
      dbz3::anim::EstadoCuerpo e;
      const bool hay = dbz3::anim::LeerEstadoCuerpo(base, x0, c, e);
      float h[3] = {0, 0, 0};
      const bool bh = hay && hueso0(e.objeto, h);
      // DS105: estado 0 del modelo (modelo+0x41C..): el clip y el cuadro que 0x8208DC60 evalua en los huesos, y el
      // mapa por almacen que elige la pista de cada hueso (lo construye 0x8208D2C8 en modelo+0x4B0+almacen*0x200).
      // Si el clip del modelo no es el del cuerpo, el skinning usa otra pose aunque A+0x604 avance.
      uint32_t mclip = 0, mclip1 = 0, extra6 = 0, extra7 = 0;
      float mframe = 0.0f, mlast = 0.0f, mframe1 = 0.0f;
      float reloj2 = 0.0f;
      int mapa_ok = -1, mapa_n = 0;
      uint32_t mapa_m[4] = {0, 0, 0, 0}, mat = 0;
      if (hay) {
        const uint32_t mod = REX_LOAD_U32(e.objeto + 0x538);
        if (mod >= 0x10000000u && mod < 0xF0000000u) {
          mclip = REX_LOAD_U32(mod + 0x41C);
          const uint32_t uf = REX_LOAD_U32(mod + 0x424);
          std::memcpy(&mframe, &uf, 4);
          const uint32_t ul = REX_LOAD_U32(mod + 0x428);
          std::memcpy(&mlast, &ul, 4);
          mclip1 = REX_LOAD_U32(mod + 0x43C);
          const uint32_t uf1 = REX_LOAD_U32(mod + 0x444);
          std::memcpy(&mframe1, &uf1, 4);
          const uint32_t sk = REX_LOAD_U32(mod + 4);
          const uint32_t nb = sk >= 0x10000000u && sk < 0xF0000000u ? REX_LOAD_U32(sk + 16) : 0;
          const uint32_t esqueleto = REX_LOAD_U32(mod + 0x408);
          if (nb > 0 && nb <= 128 && esqueleto >= 0x10000000u && esqueleto < 0xF0000000u) {
            uint32_t f = 2166136261u;   // FNV-1a de las matrices de hueso que van al render
            for (uint32_t k = 0; k < 64u * nb; ++k) f = (f ^ REX_LOAD_U8(esqueleto + k)) * 16777619u;
            mat = f;
          }
          // DS105: el mapa NO es por clip: 0x8208D2C8(modelo, banco, ACM) lo construye por ALMACEN y lo deja en
          // modelo+0x2B0 + (almacen+1)*0x200 (0x8208CAA0 lee `lha` de la mitad alta del clip = almacen+1). Mapea
          // hueso del modelo -> indice de pista del banco (ese es el indice del bloque desc+0x0C que escribimos).
          // Por eso anadir filas al #ACM no invalida el mapa; solo hay que mirar que el banco este asignado.
          const uint32_t alm1 = mclip >> 16;   // almacen + 1
          if (nb > 0 && nb <= 128 && alm1 >= 1 && alm1 <= 16) {
            const uint32_t banco = alm1 - 1;
            const uint32_t acm_b = REX_LOAD_U32(mod + 4u * (283u + banco));
            if (acm_b >= 0x10000000u && acm_b < 0xF0000000u) {
              const uint32_t nb_banco = REX_LOAD_U32(acm_b + 0x18);
              const uint32_t mapa = mod + 0x2B0u + alm1 * 0x200u;
              mapa_n = int(nb_banco > 0 && nb_banco <= 256 ? nb_banco : nb);
              mapa_ok = 0;
              for (uint32_t k = 0; k < nb && k < 64; ++k) {
                const int16_t v = int16_t(REX_LOAD_U16(mapa + 2 * k));
                if (v >= -1 && v < int16_t(mapa_n)) ++mapa_ok;
                if (k < 4) mapa_m[k] = uint32_t(int32_t(v)) & 0xFFFFu;
              }
            } else {
              mapa_ok = -3;   // banco de #ACM no asignado en el modelo
            }
          }
        }
        extra6 = REX_LOAD_U32(e.objeto + 0x62C);
        extra7 = REX_LOAD_U32(e.objeto + 0x638);
        const uint32_t u2 = REX_LOAD_U32(e.objeto + 0x630);
        std::memcpy(&reloj2, &u2, 4);
      }
      REXLOG_INFO("dbz3 iwmini: motor {} hay {} oculto {}/{} code {:03X} clip {:X}:{:03X} cuadro {:.1f} reloj3 {:.1f}/{:.1f} "
                  "vueltas {} free {} bloque {:08X} hueso0 ({:.2f},{:.2f},{:.2f}) | modelo clip {:X}:{:03X} cuadro {:.1f} "
                  "ult {:.1f} +1 {:X}:{:03X}/{:.1f} extra {:X}/{:X} reloj2 {:.1f} mapa {}/{} banco {} [{:X} {:X} {:X} {:X}] mat {:08X}",
                  c ? "B" : "A", hay, e.oculto1, e.oculto2, e.codigo, e.clip >> 16, e.clip & 0xFFFF, e.cuadro, e.reloj,
                  float(int32_t(e.cuadros)), e.vueltas, unsigned(e.congelado), e.bloque, bh ? h[0] : 0.0f,
                  bh ? h[1] : 0.0f, bh ? h[2] : 0.0f, mclip >> 16, mclip & 0xFFFF, mframe, mlast, mclip1 >> 16,
                  mclip1 & 0xFFFF, mframe1, extra6, extra7, reloj2, mapa_ok, mapa_n, mclip >> 16 ? (mclip >> 16) - 1 : 0u,
                  mapa_m[0], mapa_m[1], mapa_m[2], mapa_m[3], mat);
    };
    cuerpo(0);
    cuerpo(1);
  }
  static int64_t traza = 0;
  if (traza_log && estado == 4 && NowMs() - traza > 2000) {
    traza = NowMs();
    REXLOG_INFO("dbz3 iwmini: jugador ({:.1f}, {:.1f}, {:.1f}) marcha ({:.2f}, {:.2f}) anda {} ojo ({:.1f}, {:.1f}, {:.1f}) "
                "objetivo ({:.2f}, {:.2f}, {:.2f})",   // fase 1 (7/11): hacia donde mira (diferencial.giro_reposo_iw)
                p.x, p.y, p.z, dx, dz, g_anda.load(), ojo[0], ojo[1], ojo[2], obj[0], obj[1], obj[2]);
    // D34 (medida del encargo): ojo vs malla DIBUJADA. `malla` = superficie dibujada mas alta en la columna del ojo
    // (AlturaMalla; "sin" en un canto vertical), `suelo` = altura del #WCF (Altura), `tapado` = el segmento T->ojo corta
    // la malla dibujada (con `d` = distancia de T al corte, `fraccion` del segmento), `pegada` = el ojo esta a menos de
    // 3 u de una cara. Casos: (a) `malla` sin dato y `suelo` alto -> el #WCF daba altura donde se ve una cara vertical;
    // (b) tapado -> pared entre el jugador y el ojo (el marron es la cara); (c) pegada -> plano cercano en la ladera.
    if (d->campo) {
      const float hs = dbz3::suelo_iw::Altura(ojo[0], ojo[2], -1e30f);
      const float hm = dbz3::suelo_iw::AlturaMalla(ojo[0], ojo[2], -1e30f);
      float corte[3];
      const bool tap = RayoCamara(cab, ojo, corte);
      const float dxt = corte[0] - cab[0], dyt = corte[1] - cab[1], dzt = corte[2] - cab[2];
      const float dt = std::sqrt(dxt * dxt + dyt * dyt + dzt * dzt);
      const float lx = ojo[0] - cab[0], ly = ojo[1] - cab[1], lz = ojo[2] - cab[2];
      const float lt = std::sqrt(lx * lx + ly * ly + lz * lz);
      const float hjug = dbz3::suelo_iw::Altura(p.x, p.z, -1e30f);
      // DS100: que cuerpo se dibuja (oculto A = X+3261, B = X+4925) y que codigo de animacion lleva cada uno
      // (cuerpo A = X+2928, cuerpo B = X+0x11F0). Sirve para comprobar en el log que la carrera llega a los dos.
      const uint32_t anim_a = REX_LOAD_U32(x0 + 2928) & 0xFFF, anim_b = REX_LOAD_U32(x0 + 0x11F0) & 0xFFF;
      REXLOG_INFO("dbz3 iwmini: campo ojo suelo {:.1f} malla {:.1f} jugador suelo {:.1f} (y {:.1f}) tapado {} d {:.1f} "
                  "frac {:.2f} pegada {} cuerpos A{} B{} codigo {:03X}/{:03X}",
                  hs, hm, hjug, p.y, tap, tap ? dt : -1.0f, tap && lt > 1e-3f ? dt / lt : -1.0f,
                  dbz3::suelo_iw::CercaMalla(ojo, 3.0f), REX_LOAD_U8(x0 + kOcultoA), REX_LOAD_U8(x0 + kOcultoB), anim_a,
                  anim_b);
    }
  }
}

// Hilo del juego, al leer el mando (combate.cpp sub_8208FD58): la palanca relativa a la camara elige la marcha y el
// juego recibe "hacia el rival" (el pivote ya esta en esa direccion). El juego mantiene las direcciones (+108) de un
// cuadro a otro (las suelta al ver soltar la tecla): antes de leer se le devuelve el registro como lo dejo el,
// sin lo inyectado, y despues se guarda ese y se inyecta.
// EU 0x299520 Minigame_ReadPlayerButtons
void Mando(uint8_t* base, uint32_t usuario, bool leido) {
  { void MandoVuelo(uint8_t*, uint32_t, bool); MandoVuelo(base, usuario, leido); }   // R12: el vuelo se queda el mando
  { void MandoBotones(uint8_t*, uint32_t, bool); MandoBotones(base, usuario, leido); }   // R14: los de botones, tambien
  if (leido && usuario == 0) {   // PRUEBA (PadPrueba): palanca izquierda de lado, en cualquier combate
    static uint16_t antes_pp = 0;
    const int d = g_pad_prueba.load();
    const uint16_t bits = d > 0 ? kDerecha : d < 0 ? kIzquierda : uint16_t(0);
    if (d != 0) {
      const uint32_t r = kPads;
      REX_STORE_U16(r + 108, bits);
      REX_STORE_U16(r + 110, antes_pp == bits ? uint16_t(0) : bits);
      REX_STORE_U8(r + 26, d > 0 ? 0x7F : 0x81);
      REX_STORE_U8(r + 27, 0);
    }
    antes_pp = bits;
  }
  struct Guardado {
    bool hay = false;
    uint16_t d108 = 0, d110 = 0;
    uint8_t p26 = 0, p27 = 0;
    uint16_t b12 = 0, b14 = 0;   // botones pulsados / recien pulsados
    uint32_t c92 = 0;            // cuadros pulsado del boton 5 (circulo, 0x100 << 5)
  };
  static Guardado g;
  static uint32_t g_cuentas[8];   // 30/10: cuadros pulsado de cada boton 0x100 << i (r + 72 + 4 * i)
  static bool g_cuentas_puestas = false;
  if (usuario != 0) return;   // el luchador 1 lleva el mando 0 (registro 0, medido)
  const uint32_t r = kPads + 184 * usuario;
  if (!leido) {
    if (g.hay) {
      REX_STORE_U16(r + 108, g.d108);
      REX_STORE_U16(r + 110, g.d110);
      REX_STORE_U8(r + 26, g.p26);
      REX_STORE_U8(r + 27, g.p27);
      REX_STORE_U16(r + 12, g.b12);
      REX_STORE_U16(r + 14, g.b14);
      REX_STORE_U32(r + 92, g.c92);
      if (g_cuentas_puestas) {
        for (uint32_t i = 0; i < 8; ++i) REX_STORE_U32(r + 72 + 4 * i, g_cuentas[i]);
        g_cuentas_puestas = false;
      }
      g.hay = false;
    }
    return;
  }
  // R16: IW no tiene la presentacion de combate de B3 (vuelo por el escenario y entradas, estados 1-2): en todos los
  // minijuegos (los modulos tambien) START recien pulsado la salta por el camino del juego (hub.cpp: "START la salta")
  if (const uint32_t m = g_estado.load() == 2 ? REX_LOAD_U32(kMgrPtr) : 0; m && REX_LOAD_U32(m + kEstado) - 1 < 2) {
    g = {true, REX_LOAD_U16(r + 108), REX_LOAD_U16(r + 110), REX_LOAD_U8(r + 26), REX_LOAD_U8(r + 27),
         REX_LOAD_U16(r + 12), REX_LOAD_U16(r + 14), REX_LOAD_U32(r + 92)};
    REX_STORE_U16(r + 12, uint16_t(g.b12 | kBotonStart));
    REX_STORE_U16(r + 14, uint16_t(g.b14 | kBotonStart));
    return;
  }
  if (g_estado.load() != 2 || g_modulo.load()) return;   // los modulos de otros frentes llevan su mando
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr || REX_LOAD_U32(mgr + kEstado) != 4) return;
  g = {true,
       REX_LOAD_U16(r + 108),
       REX_LOAD_U16(r + 110),
       REX_LOAD_U8(r + 26),
       REX_LOAD_U8(r + 27),
       REX_LOAD_U16(r + 12),
       REX_LOAD_U16(r + 14),
       REX_LOAD_U32(r + 92)};
  // eIW3 (solo pruebas): DBZ3_TEST_IW_GUION=<fichero> mete entradas cuadro a cuadro desde que empieza el juego (estado 4).
  // Lineas "desde hasta x y b12 b14" (cuadros del port a 60 Hz, [desde,hasta); x/y = palanca s8, arriba = -127; b12 se
  // mantiene todo el tramo, b14 solo en el primer cuadro). Sin la variable no hace nada.
  if (static const char* guion = std::getenv("DBZ3_TEST_IW_GUION"); guion && *guion) {
    static std::vector<std::array<int, 6>> tramos;
    static int cuadro = 0;
    static bool leido_f = false;
    if (!leido_f) {
      leido_f = true;
      std::ifstream f(guion);
      std::array<int, 6> t{};
      while (f >> t[0] >> t[1] >> t[2] >> t[3] >> std::hex >> t[4] >> t[5] >> std::dec) tramos.push_back(t);
      REXLOG_INFO("dbz3 iwmini: guion de prueba: {} tramos", tramos.size());
    }
    // cuadro 0 = primer cuadro con el mando ya en manos del jugador (fin del cartel/entrada)
    static bool con_mando = false;
    if (g_libre.load()) con_mando = true;   // latch: el primer cuadro libre del jugador
    if (con_mando) for (const auto& t : tramos)
      if (cuadro >= t[0] && cuadro < t[1]) {
        g.d108 = 0, g.p26 = uint8_t(int8_t(t[2])), g.p27 = uint8_t(int8_t(t[3]));
        g.b12 = uint16_t(g.b12 | t[4]);
        if (cuadro == t[0]) g.b14 = uint16_t(g.b14 | t[5]);
      }
    if (con_mando) ++cuadro;
  }
  if(g_panel_campo.load()>=0 && (g.b14 & 0x4000))g_panel_confirmar=true;
  // circulo (B del mando de Xbox): el scatto de IW (Minigame_ReadPlayerButtons: recien pulsado 0x2000). El luchador
  // de B3 no lo ve (seria su rafaga de ki).
  // La cruceta tampoco: en la HD lanza las tecnicas (la flecha derecha del teclado sacaba un Kamehameha, accion 15,
  // codigo 0x253/0x24A, con +108 = 0x2000); para andar ya va en +108 (la cruceta << 8).
  // triangulo (0x1000) va en IW a Camera_UpdateStickOrbit (EU 0x297330): fuera del luchador de B3, como circulo y cruz.
  // 30/10: el luchador de B3 no recibe NADA del mando en el campo (solo START/select). Causa de "solo hace defensas": cruz es
  // la guardia de B3 y esta la lee tambien de la cuenta de cuadros pulsado (r + 72 + 4 * i), que no se tapaba: al saltar entraba
  // en la accion 1 (guardia, codigo 2) y la pose de IW competia con la del motor. Igual cuadrado/L1/R1/L2/R2: sus golpes.
  if ((g.b14 & kBotonScatto) && !g_sin_mando.load()) g_pide_scatto = true;
  if ((g.b14 & kBotonSalto) && !g_sin_mando.load()) g_pide_salto = true;
  g_cuadrado=(g.b12&0x8000)!=0 && !g_sin_mando.load();
  const uint16_t pasan = g_sin_hud.load() ? 0x0007 : 0x000F;   // en la presentacion, START no abre la pausa de B3
  if ((g.b14 & kBotonStart) && g_paso_entrada == 5) g_salta_sobre = true;
  REX_STORE_U16(r + 12, uint16_t(g.b12 & pasan));
  REX_STORE_U16(r + 14, uint16_t(g.b14 & pasan));
  for (uint32_t i = 0; i < 8; ++i) {
    g_cuentas[i] = REX_LOAD_U32(r + 72 + 4 * i);
    REX_STORE_U32(r + 72 + 4 * i, uint32_t(-1));
  }
  g_cuentas_puestas = true;
  // EU 0x299250 Minigame_ReadPlayerStick: lee la palanca izquierda (0x4adeda/0x4adedb; el volcado de los bytes
  // 0x4ade84..0x4ade87 del mando PS2 la separa de la derecha en 0x4aded8/0x4aded9) y deriva el vector de marcha
  // 0.008 por cuadro hacia su lateral
  // (`dir += (lado - dir) * f * 0.008`, _qmtc2 0x3c03126f). El lateral se recalcula en CADA llamada a partir del
  // vector actual (lineas 316077-316093), asi que NO es un lerp hacia un blanco fijo: mientras se mantiene la palanca
  // la camara gira ~0,46 grados por cuadro (0.008 rad) sin parar; al soltarla se queda donde este. Va SIEMPRE (fuera
  // del gate de triangulo). [VERIFICAR] el sentido en pantalla: derecha = gira a la derecha (mismo signo que la
  // marcha); si sale al reves, cambiar el signo de tx/tz en la deriva y en la orbita.
  // EU 0x297330 Camera_UpdateStickOrbit: orbita 0.04 por cuadro con la MISMA zona muerta (+-0x20, /96 | /95) e
  // inclinacion 2.0 grados por cuadro, tope [-30, 100]. En el original lo hace tambien con la palanca izquierda y solo
  // con camara+0x208 = 1 (triangulo mantenido, Minigame_ReadPlayerButtons EU 0x299520, minijuego != 1 y != 0xE); en el
  // port (DS181) la orbita va en la palanca DERECHA, que es la que en B3 gira la camara. Con el teclado del juego
  // (flechas = dbz3_keybind_rstick_*) no se exige triangulo, para no tener que mantener dos teclas.
  const int lx = int8_t(g.p26);
  auto zona = [](int v, float& f) {   // f = (v -/+ 0x20) / 95|96, 0 dentro de la zona muerta
    if (v < -0x20) f = float(v + 0x20) / 96.0f;
    else if (v > 0x20) f = float(v - 0x20) / 95.0f;
    else f = 0.0f;
  };
  auto zona16 = [&](int v, float& f) {   // la palanca derecha llega en s16 (-32768..32767)
    zona(std::clamp(int(std::lround(float(v) * 127.0f / 32767.0f)), -127, 127), f);
  };
  const std::shared_ptr<Datos> datos = Actual();
  const int indice = datos ? datos->indice : -1;
  const bool camara_208 = indice >= 0 && indice != 1 && indice != 14 &&
                           (g.b12 & 0x1000) != 0;   // camara+0x208: triangulo mantenido (Minigame_ReadPlayerButtons)
  // EU 0x299520 Minigame_ReadPlayerButtons: +0x208 solo en suelo (+0x2061 == 0).
  g_radar = camara_208 && !g_cae && !g_sin_mando.load() && g_final.load() == 0;
  // Deriva de la palanca izquierda (EU 0x299250): lerp del vector de la camara hacia su lateral, 0.008 por cuadro. El
  // lateral se recalcula cada cuadro a partir del vector actual, asi que el giro es continuo (~0,46 grados/cuadro
  // mientras se mantiene); al soltar se queda donde este.
  float f_giro = 0;
  zona(lx, f_giro);
  if (std::abs(f_giro) > 0.01f && !g_sin_mando.load()) {
    const float y = g_yaw.load(), ax = std::sin(y), az = std::cos(y);
    const float bx = -az, bz = ax;   // lado derecho de la camara (cine::Camara: d = f x arriba)
    const float t = std::abs(f_giro) * 0.008f;
    const float tx = f_giro > 0 ? bx : -bx, tz = f_giro > 0 ? bz : -bz;
    g_yaw = std::atan2(ax + (tx - ax) * t, az + (tz - az) * t);
  }
  // Orbita con la palanca derecha (EU 0x297330): 0.04 por cuadro (mismo lerp, ~2,29 grados/cuadro) + 2.0 grados de
  // inclinacion. Con el teclado del juego no se exige triangulo (dbz3_mnk_mode = flechas -> palanca derecha).
  const bool teclado = dbz3::settings::MnkMode();
  float f_rx = 0, f_inclina = 0;
  zona16(g_rx.load(), f_rx);
  zona16(-g_ry.load(), f_inclina);   // IW: sVar2 = -LY (arriba positivo en su cuenta)
  if ((camara_208 || teclado) && !g_sin_mando.load()) {
    if (std::abs(f_rx) > 0.01f) {
      const float y = g_yaw.load(), ax = std::sin(y), az = std::cos(y);
      const float bx = -az, bz = ax;
      const float t = std::abs(f_rx) * 0.04f;
      const float tx = f_rx > 0 ? bx : -bx, tz = f_rx > 0 ? bz : -bz;
      g_yaw = std::atan2(ax + (tx - ax) * t, az + (tz - az) * t);
    }
    // arriba baja la inclinacion (tope -30), abajo la sube (30 + 70)
    if (f_inclina > 0.01f) g_pitch = std::max(-30.0f, g_pitch.load() - f_inclina * 2.0f);
    else if (f_inclina < -0.01f) g_pitch = std::min(100.0f, g_pitch.load() - f_inclina * 2.0f);
  }
  static uint16_t botones = 0;
  if (g.b12 != botones) {
    botones = g.b12;
    if (botones) REXLOG_INFO("dbz3 iwmini: mando: botones 0x{:04X}", botones);
  }
  // +108 (medido con el teclado de pruebas): palanca 0x10 arriba, 0x20 derecha, 0x40 abajo, 0x80 izquierda; cruceta
  // lo mismo << 8 (Up = 0x1000, Right = 0x2000); +26/+27 = palanca izquierda x/y (s8, arriba negativo)
  // 30/10: solo la cruceta va en digital (+108 >> 8). La palanca tambien enciende +108 a poco que se incline, y como eso se miraba
  // primero, un mando real siempre corria. EU 0x299250: cada eje, (v -/+ 0x20) / 95|96 con zona muerta +-0x20.
  const uint16_t dd = uint16_t(g.d108 >> 8);
  // EU 0x299520 Minigame_ReadPlayerButtons
  float sx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
  float sy = float((dd & kArriba) != 0) - float((dd & kAbajo) != 0);
  if (sx == 0 && sy == 0) {
    int ax = int8_t(g.p26), ay = int8_t(g.p27);
    if (static const char* tope = std::getenv("DBZ3_TEST_IW_PALANCA"); tope) {   // prueba: la palanca a medias (el teclado da el tope)
      const int t = std::atoi(tope);
      ax = std::clamp(ax, -t, t), ay = std::clamp(ay, -t, t);
    }
    zona(ax, sx);
    zona(ay, sy);
    sy = -sy;
  }
  static uint32_t visto = ~0u;
  const uint32_t clave = uint32_t(g.d108) | uint32_t(g.p26) << 16 | uint32_t(g.p27) << 24;
  if (clave != visto) {
    visto = clave;
    REXLOG_INFO("dbz3 iwmini: mando: +108 0x{:04X}, palanca {} {}", g.d108, int8_t(g.p26), int8_t(g.p27));
  }
  const bool palanca = sx != 0 || sy != 0;   // EU 0x2A9440: cualquier valor fuera de la zona muerta
  // EU 0x2A9440 Char_GetNextActionCode
  g_corre = std::fabs(sx) > 0.9f || std::fabs(sy) > 0.9f;
  const float yaw = g_yaw.load(), fx = std::sin(yaw), fz = std::cos(yaw);
  const float rx = -fz, rz = fx;   // derecha de la camara (cine::Camara: d = f x arriba)
  const int fase = g_fase.load();
  // El giro de la camara con la palanca de lado ya esta arriba (Minigame_ReadPlayerStick 0x299250 = 0.008 y
  // Camera_UpdateStickOrbit 0x297330 = 0.04); aqui solo la marcha.
  if (palanca && (fase == 0 || fase == 0x15)) {   // en el arranque y la frenada IW no gira (solo en 0x15)
    const float wx = fx * sy + rx * sx, wz = fz * sy + rz * sx, l = std::max(std::hypot(wx, wz), 1e-3f);
    // Minigame_State15_DashMove (EU 0x2A0A30) -> Minigame_RotatePlayerToMoveDir (0x299D80): la direccion es la de la
    // palanca (media de delante y derecha, FUN_0013c690) y se aplica ENTERA cada cuadro; no hay tope de giro. El
    // 0,08 rad/cuadro que habia era una suposicion y hacia que el sprint girase tarde.
    g_dir_x = wx / l;
    g_dir_z = wz / l;
    // EU 0x299EE0: la marcha no normaliza (derecha*(sx*v) + delante*(sy*v)); en diagonal la palanca entera da 1,41x
    g_avance_x = wx;
    g_avance_z = wz;
  } else if (fase == 0x15) {   // scatto sin palanca: hacia delante de la camara (Minigame_MovePlayerByStick)
    g_dir_x = fx;
    g_dir_z = fz;
    g_avance_x = fx;
    g_avance_z = fz;
  }
  // La marcha entera la pone Fotograma (g_anda): 30/10, ya no se le inyecta "hacia el rival" al luchador de B3. Eso le
  // hacia entrar en su accion 13 (carrera de combate), que pisaba el codigo y le sumaba su propio avance.
  g_anda = palanca && fase == 0 && !g_resbalando.load() && !g_sin_mando.load() ? true : fase == 0x15;
  REX_STORE_U16(r + 108, 0);
  REX_STORE_U16(r + 110, 0);
  REX_STORE_U8(r + 26, 0);
  REX_STORE_U8(r + 27, 0);
}

}  // namespace dbz3::iwmini

// EU IW2F3FE0: los minijuegos de campo fijan la opacidad de la sombra en0.3,
// no la curva por altura/escenario de combate que calcula B3 en820FC1A8.
// Pareja PPC verificada por tools/iw_sol9c.py pares:30 instrucciones, dos
// llamadas coincidentes; LR82101624 -> EU82101724, sin diferencias funcionales.
// Campo_F:2F6630 borra el contexto, incluido su puntero+8 de direccion de
// sombra;2F3FE0 llama Gfx_SetLightDirection(0), que2636DC fija a(0,-1,0,1).
// No es la direccion de iluminacion del actor. El getter nativo de sombra
// US82139760/EU82139868 devuelve el MISMO dato8234D3D0 en ambas imagenes.
#if defined(DBZ3_DUAL_REGION)
REX_EXTERN(dbz3eu_sub_82139680);
REX_EXTERN(__imp__dbz3eu_sub_82139680);
#endif
DBZ3_HOOK(sub_82139578,dbz3eu_sub_82139680) {
  using namespace dbz3::iwmini;
  const auto d=Actual();
  const uint32_t sombra=ctx.r3.u32;
  const bool campo=d && g_estado.load()==2 && !g_modulo.load() &&
      ctx.lr==(dbz3::g_guest_eu?0x82101724u:0x82101624u) && sombra>=0x40000000u && sombra<0xA0000000u;
  uint32_t antes=0;
  if(campo && !std::getenv("DBZ3_IW_SOMBRA_COMBATE")) {
    constexpr uint32_t direccion_sombra=0x8234D3D0u; // pareja de getters, no desplazamiento EU supuesto
    std::array<uint32_t,4> direccion_antes;
    constexpr uint32_t vertical[4]={0,0xBF800000u,0,0x3F800000u};
    for(uint32_t i=0;i<4;++i) {
      direccion_antes[i]=REX_LOAD_U32(direccion_sombra+4*i);
      REX_STORE_U32(direccion_sombra+4*i,vertical[i]);
    }
    antes=REX_LOAD_U32(sombra+0xEC);
    StoreF(base,sombra+0xEC,0.3f);
    if(std::getenv("DBZ3_TEST_IWMINI")) {
      static bool medido=false;
      if(!medido) {
        float valor;std::memcpy(&valor,&antes,4);
        REXLOG_INFO("dbz3 IW sombra9c: consumidor nativo {:.6f} -> 0.300000 (IW2F3FE0)",valor);
        medido=true;
      }
    }
    orig(ctx,base);
    REX_STORE_U32(sombra+0xEC,antes);
    for(uint32_t i=0;i<4;++i)REX_STORE_U32(direccion_sombra+4*i,direccion_antes[i]);
  } else orig(ctx,base);
}

// R16: tope de distancia entre luchadores de B3 (sub_82114910, cada cuadro en los estados 3, 4, 5 y 8): con los dos a
// mas de 150 (0x8201B41C) acerca uno o los dos (la mitad cada uno) lo que sobra, en X+64. En los minijuegos los dos
// sitios son de IW (Piccolo a 433 detras, el pivote, los blancos): medido con DBZ3_SONDA_PICC, Gohan puesto en (0, 0)
// y Piccolo en (643, 323) aparecian en (255, 128) y (389, 195), y el dibujo alternaba los dos sitios. IW no tiene
// ese tope: mientras corre un minijuego no acerca a nadie, pero hace lo que el hace tras mover (sin ello el cuerpo
// dibujado, X+1616, se quedaba donde estaba: medido, Gohan fuera de cuadro): sub_820F9548(X+1616, 1) y sub_82110A38(X)
// de los dos luchadores, con X+64 tal como lo dejan los minijuegos.
namespace dbz3::iwmini {
bool SeBotones(int* banco, int* id);   // iw_mini_botones.cpp
}
namespace dbz3::escenarios {
void ComprobarZonaB(PPCContext& ctx, uint8_t* base);   // escenarios.cpp: ring out a la zona B (mod opcional)
}
DBZ3_HOOK_EU(sub_82114910) {
  dbz3::escenarios::ComprobarZonaB(ctx, base);
  // La VM B2 es externa: mismo retorno temprano que el original con35204!=0.
  // No arrastrar al atacante con el límite del combate mientras vuela la víctima.
  if (dbz3::escenarios::TransicionB2Activa()) return;
  if (!dbz3::iwmini::Activo()) return orig(ctx, base);
  if (int banco = 0, id = 0; dbz3::iwmini::SeBotones(&banco, &id)) {   // R14: el sonido de golpe que pidio el minijuego
    const PPCContext guardado_se = ctx;
    ctx.r3.u64 = uint64_t(banco);
    ctx.r4.u64 = 0;
    ctx.r5.u64 = uint64_t(id);
    ctx.r6.u64 = 0;
    DBZ3_CALL_EU(sub_820922A0)(ctx, base);
    ctx = guardado_se;
  }
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(0x823D6CF4));
  if (!mgr) return;
  const uint32_t st = REX_LOAD_U32(mgr + 35128);
  if (st != 3 && st != 4 && st != 5 && st != 8) return;
  const PPCContext guardado = ctx;
  for (const uint32_t x : {mgr + 2096, mgr + 2096 + 8240}) {
    ctx.r3.u64 = x + 1616;
    ctx.r4.u64 = 1;
    DBZ3_CALL_EU(sub_820F9548)(ctx, base);
    ctx = guardado;
    ctx.r3.u64 = x;
    DBZ3_CALL_EU(sub_82110A38)(ctx, base);
    ctx = guardado;
  }
}

namespace dbz3::iwmini {
namespace {
const bool g_registro = (dbz3::pasarela::RegistrarMinijuegoIW(&Empezar), true);

// s8 equipo B (punto 3): el resultado de IW cuando el minijuego se lanza FUERA del mapa (fila < 0, p. ej. la prueba
// DBZ3_TEST_IWMINI). En el mapa lo ensena pasarela_misiones.cpp con PantallaDM::Resultado; fuera de el nadie lo recogia y
// se volvia al menu principal sin tabla. Aqui se dibuja la pantalla propia de los minijuegos (DibujarResultadoIW:
// titulo de F04.3, filas de 334/1 t17, letra de rango 334/1 t24, objetos o GREAT/GOOD/NORMAL, EARNED! y TOTAL de 333/0)
// sobre el menu principal, con el mando bloqueado para el menu; A salta el conteo y, acabado, lo cierra (en la prueba
// automatica se cierra sola a los 12 s). No suma Zenie ni guarda record: eso es del mapa.
std::mutex g_rf_mu;
Resultado g_rf_res;
std::atomic<bool> g_rf_ver{false};

class PanelResFuera : public rex::ui::ImGuiDialog {
 public:
  explicit PanelResFuera(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const bool ver = g_rf_ver.load() && dbz3::ingame::GameScreen() == kMainScreen && !dbz3::GameDimmed() &&
                     !dbz3::OverlayOpen();
    auto* rt = rex::Runtime::instance();
    auto* in = rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
    if (in && ver != bloqueo_) {   // el menu principal no recibe la A que cierra la tabla
      if (ver) in->AddUIInputBlocker(); else in->RemoveUIInputBlocker();
      bloqueo_ = ver;
    }
    if (!ver) {
      t0_ = -1;
      return;
    }
    Resultado r;
    {
      std::lock_guard<std::mutex> lk(g_rf_mu);
      r = g_rf_res;
    }
    uint16_t b = 0;
    if (in) {
      rex::input::X_INPUT_STATE st = {};
      if (in->GetStateForUI(0, &st) == 0) b = uint16_t(st.gamepad.buttons);
    }
    const float t = float(dbz3::RelojT(ImGui::GetTime()));
    if (t0_ < 0) t0_ = t, antes_ = b, saltar_ = false;   // lo que ya estaba pulsado (la A del minijuego) no cuenta
    uint16_t pulsado = uint16_t(b & ~antes_);
    antes_ = b;
    if (g_auto && t - t0_ > 12.0f) saltar_ = true;   // prueba: salta el conteo
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
    const S::Area a = S::GameArea(io);
    const ImVec2 p0 = a.At(280, 226), p1 = a.At(920, 674);   // lienzo 640x448 del original, centrado
    const bool completo = DibujarResultadoIW(dl, p0.x, p0.y, p1.x - p0.x, p1.y - p0.y, r, t - t0_, saltar_);
    if (pulsado & 0x1000) {   // A: primero salta el conteo; con el conteo acabado, cierra
      if (saltar_ || completo) g_rf_ver = false;
      else saltar_ = true;
    }
    if (g_auto && t - t0_ > 12.5f) g_rf_ver = false;
    if (!g_rf_ver) REXLOG_INFO("dbz3 iwmini: resultado fuera del mapa: cerrado");
  }

 private:
  bool bloqueo_ = false;
  bool saltar_ = false;
  float t0_ = -1;
  uint16_t antes_ = 0;
};

void CrearPanelResFuera() {
  static PanelResFuera* panel = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new PanelResFuera(rt->imgui_drawer());
}

// Lo mismo que hace el mapa con el Resultado del minijuego (pasarela_misiones.cpp), sin premio ni record. Hilo propio:
// las texturas #AMA las carga DibujarResultadoIW la primera vez.
void MostrarResultadoFuera(const Resultado& mr) {
  {
    std::lock_guard<std::mutex> lk(g_rf_mu);
    g_rf_res = mr;
  }
  g_rf_ver = true;
  dbz3::launcher::DeferUi(&CrearPanelResFuera);
  REXLOG_INFO("dbz3 iwmini: resultado fuera del mapa: {} cs, score {}, zeni {}, rango {} (minijuego 0x{:X})",
              mr.centesimas, mr.score >= 0 ? mr.score : mr.puntos, mr.zeni >= 0 ? mr.zeni : mr.puntos, mr.rango,
              mr.clave);
}

// PRUEBA: DBZ3_TEST_IWMINI=<clave>[,auto] (p. ej. 0x7D0,auto): empieza el minijuego cuando el menu principal lleva 3 s
// en pantalla; con "auto" el jugador salta solo de aro en aro y despues a la meta.
const bool g_test = [] {
  const char* t = std::getenv("DBZ3_TEST_IWMINI");
  if (!t) return false;
  const int clave = int(std::strtol(t, nullptr, 0));
  // SOL1: comparar la dificultad del video sin cambiar la ficha ni los valores de una partida real.
  // Solo existe dentro de DBZ3_TEST_IWMINI; la herramienta de pruebas registra el valor en escenario.toml.
  if (const char* dif = std::getenv("DBZ3_TEST_IWMINI_DIFICULTAD"); dif && *dif)
    g_dificultad = std::clamp(int(std::strtol(dif, nullptr, 0)), 0, 5);
  g_auto = std::strstr(t, "auto") != nullptr;
  g_meta_abierta = std::strstr(t, "meta") != nullptr;
  // eANI2: ",serie=0x7D7:0x7D8:..." (solo pruebas) juega esas claves una tras otra en la misma sesion.
  std::vector<int> serie{clave};
  if (const char* sr = std::strstr(t, "serie=")) {
    serie.clear();
    for (const char* q = sr + 6; *q;) {
      char* fin = nullptr;
      serie.push_back(int(std::strtol(q, &fin, 0)));
      if (!fin || *fin != ':') break;
      q = fin + 1;
    }
  }
  std::thread([serie] {
   for (size_t ronda = 0; ronda < serie.size(); ++ronda) {
    const int clave = serie[ronda];
    for (int quieto = 0; quieto < 120;) {
      quieto = dbz3::ingame::GameScreen() == kMainScreen ? quieto + 1 : 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    REXLOG_INFO("dbz3 iwmini: prueba: serie {}/{} clave 0x{:X}", ronda + 1, serie.size(), clave);
    REXLOG_INFO("dbz3 iwmini: prueba: {}", Empezar(clave, -1) ? "empieza" : "no empieza");
    for (int i = 0; i < 8000 && g_estado.load() != 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Resultado r;
    if (TomarResultado(r)) {
      REXLOG_INFO("dbz3 iwmini: prueba: resultado superado {} tiempo {} cs puntos {} rango {} cogidos {}", r.superado,
                  r.centesimas, r.puntos, r.rango, r.monedas);
      // s8 (punto 3): fuera del mapa, su tabla de IW al volver al menu principal
      for (int i = 0; i < 400 && dbz3::ingame::GameScreen() != kMainScreen; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      if (!ClaveConPanelPropioIW(r.clave))
        MostrarResultadoFuera(r);   // disparo ya espero y confirmo su recompensa
    }
    // que se cierre el panel y el minijuego quede libre antes del siguiente
    for (int i = 0; ronda + 1 < serie.size() && i < 1200 && (g_estado.load() != 0 || dbz3::ingame::GameScreen() != kMainScreen); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (ronda + 1 < serie.size()) std::this_thread::sleep_for(std::chrono::seconds(12));
   }
  }).detach();
  return true;
}();
}  // namespace

// ---- Tablas y pantalla de resultados de IW (Results_* del decompilado; sprites #AMA) ----
// Filas por minijuego (DS153). Cada familia escribe su bloque de resultados en GameState_GetConfigBlock():
//  - MSNMW (0x7D0/0x7D1/0x7D2/0x7D9/0x7DD/0x7DF): EU 0x323150 MgResult_ComputeScore = TIME (tipo 1), objetos % (tipo 3),
//    SCORE (tipo 4) + rango/premio. La referencia del original (lista_minijuegos) da rango, tiempo, % de objetos,
//    puntuacion y Zeni.
//  - 0x7D3 (ladrillos): MgFly_StateSegmentProgress / MgFly_FinishAndApplyResult = RANK + BRICKS + EARNED + TOTAL.
//  - 0x7D4/0x7DB/0x7DC (QTE dos carriles): EU 0x32F0F0 QteMg_FinalizeResult = OFFENCE / DEFENCE SUCCESS RATE +
//    TOTAL SCORE (rotulos 5/7 t20-22, `iw_mini_botones.cpp`) + rango/premio.
//  - 0x7D5 (NB007): EU 0x293530 MiniGame_ShowResult = TIME (tipo 1) + rango/premio.
//  - 0x7D6/0x7E0 (vuelo): EU 0x3675F0 MgFly_FinishAndApplyResult = TIME + rango/premio (referencia: TIME/RANGO/EARNED/
//    TOTAL, vuelo_krilin_0x7D6 y vuelo_goten_0x7E0).
//  - 0x7D7/0x7D8 (MSN009/MSN010): Viewer_CommitShootResult 0x3496B0 / Viewer_StoreResultToConfig 0x324C50 = TIME +
//    rango/premio (DS152; referencia 15:00-15:09 y 15:58-16:07).
//  - 0x7DA (NB012): EU 0x312D90 Mg_FinishAndWriteResults = GREAT/GOOD/NORMAL/TOTAL SCORE + rango/premio (referencia
//    "Tough Training!").
//  - 0x7DE (MSN016): MgShoot_ApplyResult 0x30A200 = rango/premio, sin TIME (DS152; referencia 28:06-28:15).
//  - 0x7E1 (MSN019): Results_ComputeTotalsAndRewards 0x2E9AF0 = GREAT/GOOD/NORMAL/TOTAL SCORE + rango/premio
//    (flight_training_videl: sin TIME).
// EARNED y TOTAL ZENI van en todas: LoadWaitRewardScreen 0x285F30 (EARNED con +0x168 y TOTAL con el monedero
// iRam005c154c); la referencia del vuelo lo muestra explicito (TOTAL 999.900 ZENI) y las pantallas propias de los
// modulos tambien lo pintan.
unsigned FilasResultadoIW(int clave) {
  using F = unsigned;
  const F kBase = kResEarned | kResTotal;
  switch (clave) {
    case 0x7D0:
    case 0x7D1:
    case 0x7D2:
    case 0x7D9:
    case 0x7DD:
    case 0x7DF:
      // DS169: el original (fotogramas 630 s / 1070 s / 500 s) NO tiene fila RANK: el rango es la letra grande del fondo
      // (Results_ShowRankBanner EU 0x2EAE50 -> objeto 5, sub-animaciones 0x12/0x13/0x14). Filas: TIME, ITEM COLLECTION
      // %, SCORE, EARNED, TOTAL.
      return kResTime | kResObjects | kResScore | kBase;
    case 0x7D3:
      return kResRank | kResObjects | kBase;   // BRICKS DESTROYED
    case 0x7D4:
    case 0x7DB:
    case 0x7DC:
      return kResRank | kResOffence | kResDefence | kResScore | kBase;
    case 0x7D5:
    case 0x7D6:
    case 0x7D7:
    case 0x7D8:
    case 0x7E0:
      return kResTime | kResRank | kBase;
    case 0x7DA:
    case 0x7E1:
      return kResRank | kResGreat | kResGood | kResNormal | kResScore | kBase;
    case 0x7DE:
      return kResRank | kBase;
    default:   // sin clave (pruebas): el comportamiento anterior por los campos del Resultado
      return kResTime | kResRank | kBase;
  }
}

// Minijuegos cuyo modulo ya pinta su propia pantalla de resultados al acabar: el mapa no debe repetirla.
//  0x7D3/0x7D7/0x7D8/0x7DE: iw_mini_disparo.cpp (DibujarResultadoIW en kFinal; DS129/DS152).
//  0x7D4/0x7DB/0x7DC: iw_mini_botones.cpp (panel de OFFENCE/DEFENCE/TOTAL, s9/DS72).
//  0x7E1: iw_mini_botones.cpp (Results_* de MSN019, GREAT/GOOD/NORMAL).
bool ClaveConPanelPropioIW(int clave) {
  switch (clave) {
    case 0x7D0:   // 31/10: campo a pie, encima de la pose (PasoFinal)
    case 0x7D1:
    case 0x7D2:
    case 0x7D9:
    case 0x7DD:
    case 0x7DF:
    case 0x7D6:
    case 0x7E0:
    case 0x7D5:   // 31/10: EU285F30 encima de la pose final (iw_mini_botones.cpp)
    case 0x7DA:
    case 0x7D3:
    case 0x7D4:
    case 0x7D7:
    case 0x7D8:
    case 0x7DB:
    case 0x7DC:
    case 0x7DE:
    case 0x7E1:
      return true;
    default:
      return false;
  }
}

// 0 < p <= 60 -> 0 (C), <=70 -> 1 (B), <=80 -> 2 (A), <=90 -> 3 (S), <=100 -> 4 (Z); fuera = -1 (premio de fallo
// 100).
// EU 0x2E92F0 GetRankIndexByThresholds
int RangoResultadoIW(int puntuacion) {
  if (puntuacion <= 0) return -1;
  if (puntuacion <= 60) return 0;
  if (puntuacion <= 70) return 1;
  if (puntuacion <= 80) return 2;
  if (puntuacion <= 90) return 3;
  if (puntuacion <= 100) return 4;
  return -1;
}

// premio[rango] (0x5D919A: 6500/6800/7100/7400/7700) x multiplicador[dificultad] (0x5D91A4: 0,6/0,8/1,0/1,3/1,6/2,0),
// redondeado a la centena (resto < 50 abajo, si no arriba). Sin rango (0x5D9198) = 100.
// EU 0x2E9AF0 Results_ComputeTotalsAndRewards
int PremioResultadoIW(int puntuacion, int dificultad) {
  const int rango = RangoResultadoIW(puntuacion);
  if (rango < 0) return 100;
  static const int kPremio[5] = {6500, 6800, 7100, 7400, 7700};
  static const float kMult[6] = {0.6f, 0.8f, 1.0f, 1.3f, 1.6f, 2.0f};
  int v = int(float(kPremio[rango]) * kMult[std::clamp(dificultad, 0, 5)]);
  v = (v % 100 < 50 ? v / 100 : v / 100 + 1) * 100;
  return v;
}

namespace {

int ScoreResultado(const Resultado& r) { return r.score >= 0 ? r.score : r.puntos; }
int ZeniResultado(const Resultado& r) { return r.zeni >= 0 ? r.zeni : r.puntos; }
int SaldoResultado(const Resultado& r) {
  return r.saldo >= 0 ? r.saldo : P::LeerProgreso("iw_zeni", 0) + ZeniResultado(r);
}

// #AMA de la pantalla de resultados: DATA_USA 334/1 (t00 relleno, t08 triangulo, t17 filas TIME/RANK/BRICKS/SCORE/
// TOTAL SCORE, t19 cifras amarillas grandes, t24 letras de rango), 333/0 (t19 EARNED!, t20 TOTAL, t07 ZENI) y 4/0
// (t15 GREAT!, t08 GOOD!, t09 NORMAL). Se cargan una vez, en el hilo de la interfaz.
struct TexResultadoIW {
  ps2::Tex panel, arrow, labels, ranks, dig, earned, total, zeni, great, good, normal;
};
TexResultadoIW& TexturasResultadoIW() {
  static TexResultadoIW t;
  static std::once_flag una;
  std::call_once(una, [] {
    P::AfsPs2 usa;
    if (!usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) return;
    const std::vector<uint8_t> ui = P::HijoAmb(usa.Entrada(334), 1);
    if (P::CuantasAmt(ui) > 24) {
      t.panel = ps2::Crear(P::TexturaAmt(ui, 0));
      t.arrow = ps2::Crear(P::TexturaAmt(ui, 8));
      t.labels = ps2::Crear(P::TexturaAmt(ui, 17));
      t.dig = ps2::Crear(P::TexturaAmt(ui, 19));
      t.ranks = ps2::Crear(P::TexturaAmt(ui, 24));
    }
    const std::vector<uint8_t> road = P::HijoAmb(usa.Entrada(333), 0);
    if (P::CuantasAmt(road) > 20) {
      t.earned = ps2::Crear(P::TexturaAmt(road, 19));
      t.total = ps2::Crear(P::TexturaAmt(road, 20));
      t.zeni = ps2::Crear(P::TexturaAmt(road, 7));
    }
    const std::vector<uint8_t> words = P::HijoAmb(usa.Entrada(4), 0);
    if (P::CuantasAmt(words) > 15) {
      t.great = ps2::Crear(P::TexturaAmt(words, 15));   // GREAT!
      t.good = ps2::Crear(P::TexturaAmt(words, 8));     // GOOD!
      t.normal = ps2::Crear(P::TexturaAmt(words, 9));   // NORMAL
    }
  });
  return t;
}

const ps2::Tex* TituloResultadoIW(int clave) {
  static std::mutex mu;
  static int cargada = -1;
  static ps2::Tex t;
  if (clave < 0x7D0 || clave > 0x7E1) return nullptr;
  std::lock_guard<std::mutex> lk(mu);
  if (cargada != clave) {
    cargada = clave;
    t = ps2::Tex{};
    P::AfsPs2 usa;
    if (usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) {
      const std::vector<uint8_t> tit = P::HijoAmb(usa.Entrada(size_t(P::kTituloMini0 + clave - 0x7D0)), 1);
      if (P::CuantasAmt(tit) > 0) t = ps2::Crear(P::TexturaAmt(tit, 0));
    }
  }
  return t ? &t : nullptr;
}

}  // namespace

// EU 0x285F30 LoadWaitRewardScreen: las filas de estadisticas tienen su valor
// final desde la entrada; EARNED/TOTAL fijan las cifras desde la derecha a
// 120/320, separadas por 20 cuadros. 420 habilita salir, no cierra por tiempo.
bool DibujarResultadoIW(ImDrawList* dl, float x0, float y0, float w, float h, const Resultado& r, float t,
                        bool saltar) {
  if (!dl || w <= 0 || h <= 0) return true;
  if (recompensa::Dibujar(dl, x0, y0, w, h, r, saltar ? 420 : std::max(t, 0.0f) * 60,
                          TituloResultadoIW(r.clave))) return saltar || t * 60 >= 420;
  const TexResultadoIW& tx = TexturasResultadoIW();
  auto X = [&](float x) { return x0 + x * w / 640.0f; };
  auto Y = [&](float y) { return y0 + y * h / 448.0f; };
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(0, 0, 0, 120));
  if (tx.panel)
    dl->AddImageRounded(tx.panel.id(), ImVec2(X(20), Y(6)), ImVec2(X(620), Y(442)), ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, 150), 20.0f);
  else
    dl->AddRectFilled(ImVec2(X(20), Y(6)), ImVec2(X(620), Y(442)), IM_COL32(8, 40, 72, 190), 14.0f);
  dl->AddRect(ImVec2(X(20), Y(6)), ImVec2(X(620), Y(442)), IM_COL32(110, 200, 255, 230), 14.0f, 0, 2.0f);
  if (const ps2::Tex* ti = TituloResultadoIW(r.clave)) {
    float alto = 38.0f, an = ti->w * alto / ti->h;
    if (an > 552.0f) an = 552.0f, alto = ti->h * an / ti->w;
    dl->AddImage(ti->id(), ImVec2(X(320 - an / 2), Y(38 - alto / 2)),
                 ImVec2(X(320 + an / 2), Y(38 + alto / 2)));
  }
  // DS169: letra de rango grande del fondo, como Results_ShowRankBanner (EU 0x2EAE50, objeto 5 sub-animacion 0x12/0x13/
  // 0x14). En los fotogramas del original (0x7D1 630 s, 0x7D2 500 s, 0x7D9 1070 s) ocupa media pantalla detras de las
  // filas. El atlas es el de la letra pequena (334/1 t24, 32x40 por glifo) ampliado; el tinte se toma de esos fotogramas.
  // fase 1 (7/11): -2 = fallo sin rango (vuelo, EU 0x33C420 Viewer_StateResultFail): no hay letra
  const int rg_grande = r.rango == -2 ? -1 : r.rango >= 0 ? std::min(r.rango, 4) : RangoResultadoIW(ScoreResultado(r));
  if (tx.ranks && rg_grande >= 0) {
    const int col = rg_grande >= 3 ? rg_grande - 3 : rg_grande, fila = rg_grande >= 3 ? 1 : 0;
    const float alto = 232.0f, an = alto * 32.0f / 40.0f;
    const ImU32 tinte = rg_grande >= 3 ? IM_COL32(255, 250, 225, 240) : IM_COL32(170, 235, 170, 235);
    dl->AddImage(tx.ranks.id(), ImVec2(X(320 - an / 2), Y(232 - alto / 2)), ImVec2(X(320 + an / 2), Y(232 + alto / 2)),
                 ImVec2(float(col) * 32.0f / tx.ranks.w, float(fila) * 40.0f / tx.ranks.h),
                 ImVec2(float(col + 1) * 32.0f / tx.ranks.w, float(fila + 1) * 40.0f / tx.ranks.h), tinte);
  }
  auto flecha = [&](float y) {   // el triangulo cian de las filas (334/1 t08), a la izquierda
    if (!tx.arrow) return;
    const float a = 18.0f;
    dl->AddImage(tx.arrow.id(), ImVec2(X(28), Y(y - a / 2)), ImVec2(X(28 + a), Y(y + a / 2)));
  };
  auto etiqueta = [&](int fila, float y, float ancho = 210.0f, float alto = 30.0f) {
    flecha(y);
    if (!tx.labels) return;
    const float sy = 32.0f * float(fila);
    dl->AddImage(tx.labels.id(), ImVec2(X(48), Y(y - alto / 2)), ImVec2(X(48 + ancho), Y(y + alto / 2)),
                 ImVec2(0, sy / tx.labels.h), ImVec2(152.0f / tx.labels.w, (sy + 32.0f) / tx.labels.h));
  };
  auto palabra = [&](const ps2::Tex& p, float y, float alto = 27.0f) {
    flecha(y);
    if (!p) return;
    const float an = p.w * alto / p.h;
    dl->AddImage(p.id(), ImVec2(X(50), Y(y - alto / 2)), ImVec2(X(50 + an), Y(y + alto / 2)));
  };
  auto rango = [&](int rg, float y) {
    if (!tx.ranks || rg < 0) return;
    const int col = rg >= 3 ? rg - 3 : rg, fila = rg >= 3 ? 1 : 0;
    const float alto = 34.0f, an = alto * 32.0f / 40.0f;
    dl->AddImage(tx.ranks.id(), ImVec2(X(566), Y(y - alto / 2)), ImVec2(X(566 + an), Y(y + alto / 2)),
                 ImVec2(float(col) * 32.0f / tx.ranks.w, float(fila) * 40.0f / tx.ranks.h),
                 ImVec2(float(col + 1) * 32.0f / tx.ranks.w, float(fila + 1) * 40.0f / tx.ranks.h));
  };
  // Cifras de 334/1 t19 (24 x 44): 0-4 fila 0, 5-9 fila 1 y ' / " en la columna 5 (media altura). xd = borde derecho.
  auto digito = [&](char c, float& xr, float y, float alto) {
    int col = 0, fila = 0;
    float y0 = 0, hh = 44.0f;
    if (c >= '0' && c <= '4') col = c - '0';
    else if (c >= '5' && c <= '9') col = c - '5', fila = 1;
    else if (c == '%') col = 5, fila = 1;   // 334/1 t19: la columna 5 de abajo es el %
    else if (c == '\'') col = 5, y0 = 0, hh = 22;
    else if (c == '"') col = 5, fila = 1, y0 = 22, hh = 22;
    else {
      xr -= alto * 0.45f;
      return;
    }
    if (!tx.dig) return;
    const float an = alto * 24.0f / 44.0f;
    const float sx = float(col) * 24.0f, sy = float(fila) * 44.0f + y0;
    dl->AddImage(tx.dig.id(), ImVec2(X(xr - an), Y(y - alto / 2 + alto * y0 / 44.0f)),
                 ImVec2(X(xr), Y(y - alto / 2 + alto * (y0 + hh) / 44.0f)),
                 ImVec2(sx / tx.dig.w, sy / tx.dig.h),
                 ImVec2((sx + 24.0f) / tx.dig.w, (sy + hh) / tx.dig.h));
    xr -= an * 0.9f;
  };
  auto numero = [&](int v, float xd, float y, float alto) {
    const std::string s = std::to_string(std::max(v, 0));
    float xr = xd;
    for (int i = int(s.size()) - 1; i >= 0; --i) digito(s[size_t(i)], xr, y, alto);
  };
  auto tiempo = [&](int cs, float xd, float y, float alto) {
    cs = std::max(cs, 0);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%d'%02d\"%02d", cs / 6000, (cs / 100) % 60, cs % 100);
    const std::string s = buf;
    float xr = xd;
    for (int i = int(s.size()) - 1; i >= 0; --i) digito(s[size_t(i)], xr, y, alto);
  };

  enum Fila { kTime, kRank, kObjects, kGreat, kGood, kNormal, kScore, kEarned, kTotal, kOffence, kDefence };
  struct F { Fila tipo; int valor; };
  // DS153: las filas salen de la tabla por clave (FilasResultadoIW), no de "si hay dato". Asi 0x7D3/0x7D6/0x7D7/0x7D8/
  // 0x7DE/0x7E0 no ensenan SCORE/TOTAL SCORE y 0x7DA/0x7E1 si ensenan GREAT/GOOD/NORMAL.
  std::vector<F> filas;
  const unsigned fk = FilasResultadoIW(r.clave);
  if (fk & kResTime) filas.push_back({kTime, r.centesimas});
  if (fk & kResRank) filas.push_back({kRank, 0});
  if (fk & kResObjects) {
    // MSNMW: la fila es el % de objetos recogidos (EU 0x323150: +0x165c / +0x165a * 100). En 0x7D3 es el numero de
    // ladrillos. DS159: la unica etiqueta del atlas (334/1 t17 fila 2) es BRICKS DESTROYED (la de 0x7D3); para MSNMW se
    // rotula "OBJECTS" con la fuente del juego (ver el case kObjects).
    int obj = r.objetos;
    if (r.objetos >= 0 && r.objetos_total > 0) obj = r.objetos * 100 / r.objetos_total;
    filas.push_back({kObjects, obj});
  }
  if (fk & kResGreat) filas.push_back({kGreat, std::max(r.great, 0)});
  if (fk & kResGood) filas.push_back({kGood, std::max(r.good, 0)});
  if (fk & kResNormal) filas.push_back({kNormal, std::max(r.normal, 0)});
  if (fk & kResOffence) filas.push_back({kOffence, std::max(r.offence, 0)});
  if (fk & kResDefence) filas.push_back({kDefence, std::max(r.defence, 0)});
  if (fk & kResScore) filas.push_back({kScore, ScoreResultado(r)});
  if (fk & kResEarned) filas.push_back({kEarned, ZeniResultado(r)});
  if (fk & kResTotal) filas.push_back({kTotal, SaldoResultado(r)});

  const int n = int(filas.size());
  RelojRecompensaIW recompensa;
  recompensa.cuadro = saltar ? 420.0f : std::max(t, 0.0f) * 60.0f;
  // DS169: los MSNMW (0x7D0/1/2/9/DD/DF) son 5 filas sin RANK, como el original (fotogramas 630/1070/500 s): mas aire
  // entre filas (48) y arranque mas bajo (92) para que la letra grande del fondo se lea entre ellas.
  const bool msnmw = fk == (kResTime | kResObjects | kResScore | kResEarned | kResTotal);
  const float paso = msnmw ? 48.0f : (n >= 8 ? 30.0f : 34.0f), y0f = msnmw ? 92.0f : 88.0f;
  const int rg = r.rango == -2 ? -1 : r.rango >= 0 ? std::min(r.rango, 4) : RangoResultadoIW(ScoreResultado(r));
  for (int i = 0; i < n; ++i) {
    const float y = y0f + paso * float(i);
    const int v = std::max(filas[size_t(i)].valor, 0);
    auto cifras_recompensa = [&](bool total, float xd) {
      const int nvis = recompensa.Cifras(v, total);
      int valor = v;
      float xr = xd;
      for (int k = 0; k < nvis; ++k) {
        digito(char('0' + valor % 10), xr, y, 30);
        valor /= 10;
      }
    };
    switch (filas[size_t(i)].tipo) {
      case kTime:
        etiqueta(0, y);
        tiempo(v, 580, y, 30);
        break;
      case kRank:
        etiqueta(1, y);
        rango(rg, y);
        break;
      case kObjects:
        // DS159: la fila 2 de 334/1 t17 SOLO es BRICKS DESTROYED, la de 0x7D3 (MgFly_StateSegmentProgress). Los MSNMW
        // (0x7D0/1/2/9/DD/DF) llevan el % de objetos (EU 0x323150: +0x165c/+0x165a*100); el rotulo del original es
        // "ITEM COLLECTION %" (fotogramas 630/1070/500 s) y no esta en el atlas: se escribe con la fuente del juego.
        if (r.clave == 0x7D3) {
          etiqueta(2, y);
          numero(v, 580, y, 30);
        } else {
          flecha(y);
          // DS172: el rotulo NO existe como sprite en DATA_USA: se comparo la fila TIME de 334/1 t17 (su tinta, 52x27)
          // contra todas las texturas de DATA_USA y ninguna la repite; el original pinta los rotulos con la fuente
          // del juego (en sus fotogramas TIME/ITEM COLLECTION %/SCORE/EARNED/TOTAL tienen el mismo estilo). Antes
          // iba a 19 u (un tercio de la tinta de las filas de sprite: 11 px frente a 19) y CENTRADO en X(52), asi que
          // se salia por la izquierda. Ahora: 33 u = la misma tinta que las filas de t17 dibujadas a 30 u de caja, y
          // alineado a la izquierda en X(48) como TIME/SCORE.
          const auto& fuentes = dbz3::launcher::ui::GetFonts();
          ImFont* f = fuentes.menu ? fuentes.menu : ImGui::GetFont();
          const float alto_txt = 33.0f * (h / 448.0f);
          const float ancho_txt = f->CalcTextSizeA(alto_txt, FLT_MAX, 0, "ITEM COLLECTION %").x;
          S::MenuTextB3(dl, ImVec2(X(48) + ancho_txt * 0.5f, Y(y)), alto_txt, "ITEM COLLECTION %", false, false);
          const std::string s = std::to_string(std::clamp(v, 0, 100)) + "%";
          float xr = 580;
          for (int i = int(s.size()) - 1; i >= 0; --i) digito(s[size_t(i)], xr, y, 30);
        }
        break;
      case kGreat:
        palabra(tx.great, y);
        numero(v, 580, y, 30);
        break;
      case kGood:
        palabra(tx.good, y);
        numero(v, 580, y, 30);
        break;
      case kNormal:
        palabra(tx.normal, y);
        numero(v, 580, y, 30);
        break;
      case kOffence:   // [H] rotulo de la pantalla propia del QTE (5/7 t20); aqui solo si se pinta esta tabla
        flecha(y);
        S::MenuText(dl, ImVec2(X(48 + 70), Y(y)), 22.0f * (w / 640.0f), "OFFENCE", false, false);
        numero(v, 580, y, 30);
        break;
      case kDefence:
        flecha(y);
        S::MenuText(dl, ImVec2(X(48 + 70), Y(y)), 22.0f * (w / 640.0f), "DEFENCE", false, false);
        numero(v, 580, y, 30);
        break;
      case kScore:
        etiqueta(r.great >= 0 ? 4 : 3, y);   // TOTAL SCORE en las pantallas de GREAT/GOOD/NORMAL
        numero(v, 580, y, 30);
        break;
      case kEarned:
        if (tx.earned) {
          const float alto = 28.0f, an = tx.earned.w * alto / tx.earned.h;
          dl->AddImage(tx.earned.id(), ImVec2(X(48), Y(y - alto / 2)), ImVec2(X(48 + an), Y(y + alto / 2)));
        }
        cifras_recompensa(false, 520);
        if (tx.zeni) {
          const float alto = 22.0f, an = tx.zeni.w * alto / tx.zeni.h;
          dl->AddImage(tx.zeni.id(), ImVec2(X(526), Y(y - alto / 2)), ImVec2(X(526 + an), Y(y + alto / 2)));
        }
        break;
      case kTotal:
        if (tx.total) {
          const float alto = 28.0f, an = tx.total.w * alto / tx.total.h;
          dl->AddImage(tx.total.id(), ImVec2(X(48), Y(y - alto / 2)), ImVec2(X(48 + an), Y(y + alto / 2)));
        }
        cifras_recompensa(true, 520);
        if (tx.zeni) {
          const float alto = 22.0f, an = tx.zeni.w * alto / tx.zeni.h;
          dl->AddImage(tx.zeni.id(), ImVec2(X(526), Y(y - alto / 2)), ImVec2(X(526 + an), Y(y + alto / 2)));
        }
        break;
    }
  }
  return recompensa.cuadro >= 420.0f;
}

}  // namespace dbz3::iwmini
