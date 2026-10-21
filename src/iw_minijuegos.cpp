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
#include "iw_ame.h"
#include "iw_minijuegos.h"

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
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "generated/dbz3_init.h"
#include "anim_iw.h"
#include "cine.h"
#include "combate.h"
#include "eu_map.h"
#include "escenarios.h"
#include "ingame/game_style.h"
#include "launcher/launcher_state.h"
#include "pasarela.h"
#include "pasarela_iw.h"
#include "ps2_modelo.h"
#include "suelo_iw.h"

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

namespace dbz3::iwmini {
namespace {

namespace P = dbz3::pasarela;
namespace S = dbz3::ingame::style;

constexpr dbz3::GA kMgrPtr = 0x823D6CF4, kPads = 0x8238B780;
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kVista = 0x60, kFov = 0x1C4, kFin = 35569, kEstado = 35128;
constexpr uint32_t kVida = 6080, kVidaMax = 6084, kOcultoA = 3261, kOcultoB = 4925;
constexpr uint32_t kScreenX = 4960 + 48, kRivalScreenX = 4960 + 56;   // combate.cpp
constexpr uint32_t kCopias[3] = {48, 64, 80};   // la posicion esta tres veces (tagteam.cpp StepSlide)
constexpr uint16_t kArriba = 0x0010, kDerecha = 0x0020, kAbajo = 0x0040, kIzquierda = 0x0080;   // registro +108
constexpr int kMainScreen = 5;
constexpr int kGoku = 0, kGohanNino = 2, kPiccolo = 11, kRaditz = 18;
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
    {14, "MSNMW", 894, 906, 6, kGohanNino, kPiccolo, kLlanuras, true, "Gotta run from Piccolo! He'll catch up eventually, though..."},
    {15, "MSN016", -1, -1, 0, -1, -1, -1, false, "Tien! Stop Cell! It's your duty!"},
    {16, "MSNMW", 896, 909, 5, kGoku, kRaditz, kRocoso, true, "Use my Dragon Radar to find the Dragon Ball!"},
    {17, "FLIGHT", -1, -1, 0, -1, -1, -1, false, "Race with Goten? Well, do your best!"},
    {18, "MSN019", -1, -1, 0, -1, -1, -1, false, "Flight training for the Tournament? Well, do your best!"},
};
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
constexpr int kCuantosA[6] = {6, 8, 10, 12, 14, 16}, kCuantosB[6] = {16, 16, 24, 24, 32, 32},
              kCuantosC[6] = {8, 8, 8, 10, 12, 16};
constexpr int kBonusRango[5] = {200, 300, 500, 750, 1000};
constexpr float kCogerA = 24.5f, kCogerB = 13.4f;   // sqrt(600), sqrt(180) en unidades de IW (= las de B3)
// Escala del curso de IW al escenario de B3. Los modelos de IW miden lo mismo que en B3 (mismo motor: el aro mide 24
// de radio, la nave 13) y se dibujan a su tamano, pero el campo de IW mide ~3000 y en un escenario de B3 se corre hasta
// ~255 del centro (medido): las posiciones se juntan (curso centrado, punto mas lejano a kRadio). ponytail: hasta
// portar el campo de IW (DATA_CMN 903+904...), que es lo que lo arregla de verdad.
constexpr float kRadio = 230.0f, kCogerMin = 2.5f;
// Modo libre: pivote delante del jugador y camara detras (unidades de B3; un luchador mide ~12).
// R16: la camara de los minijuegos de campo de IW (Camera_InitFieldDefault EU 0x297140 + Camera_UpdateFollow 0x297DE0):
// ojo 70 detras y 15 arriba del punto seguido, mira a ese punto + 5 de alto; el punto seguido se acerca al jugador
// 0.12 por cuadro (0.3 en la variante 2, Minigame_UpdateFollowCamera 0x299A70); el ojo no se suaviza.
constexpr float kPivote = 40.0f, kCamAtras = 70.0f, kCamAlto = 15.0f, kCamMiraAlto = 5.0f;

// HUD de los minijuegos: DATA_USA 6/0 (#AMT; igual en 7/0). TIME y cifras lila/azul (t03), aro plateado con cola
// (t00) con la cara (t29 Goku, t32 Krilin, t33 Vegeta, t35 Piccolo) y su barra (marco t15, relleno amarillo t01);
// cifras cian "01/08" (t12) y radar (t4-t11, t14) para el del Radar. Medidas: E:/DBZ3HD_trabajo/P4/hud.
constexpr int kHudAro = 0, kHudRelleno = 1, kHudTime = 3, kHudBarra = 15;
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

struct Objeto {
  V3 p;
  int grupo = 0;   // 0 A (aro RING_A01), 1 B (moneda), 2 C (bebida), 3 D (marca del radar, ITEMA20)
  float radio = 1;
  int valor = 0;   // bebida: energia que da (#ITM +0x480 + 2k, s16; 0x3237D0 la suma a la del jugador +0x185C)
};
struct Datos {
  int indice = -1, clave = 0, fila = 0, total = 0, num_a = 0;   // num_a: aros con sitio (abren la meta)
  int dificultad = 2;                                           // 0..5 (descriptor +0xC del MSNMW)
  float escala = 1;                                             // posiciones de IW -> B3 (curso juntado)
  bool campo = false;   // el campo de IW instalado (R8): posiciones y velocidades de IW tal cual
  float k = 0.0825f;    // por unidad de +0xB4 de IW: 0.4 en el campo; si no, 0.33 / 4 (el andar de IW = el de B3)
  float limite = 300;
  V3 salida, meta;
  float radio_meta = 2;
  std::vector<Objeto> objetos;
  P::Imagen aro, moneda, bebida;   // texturas del paquete (#AMT hijos 0, 4 y 8)
  Modelo m_aro, m_moneda, m_bebida, m_meta;   // modelos 3D de IW (paquete y campo); m_meta = la nave de Raditz
  // polvo del sprint (auditoria7/O: Effect 0x34 -> tipo 0x1A del paquete de los minijuegos de campo, DATA_CMN 934):
  // el #AME 934/26 (un cono de 4 particulas de modelo 934/1 y un rayo) y el atlas 934/0
  dbz3::iwame::ame::Efecto polvo;
  Modelo m_polvo;
  std::vector<P::Imagen> atlas_polvo;
  Modelo m_marca;                             // Radar: las marcas (grupo D)
  // Personaje que se mueve solo (NPC del MSNMW, decomp EU s_BBR_POST_EXEC 0x3172E0): 1 = huye (Bubbles, modelo de IW
  // 118/119), 2 = persigue (Piccolo, 573/574); y los platanos de Bubbles (888/9, grupo D del 888: no cuentan)
  int npc = 0;
  V3 npc_inicio;
  Modelo m_npc, m_platano;
  std::vector<V3> platanos;
  P::Imagen clear;                 // DATA_USA 334/3 t69 "CLEAR"
  P::Imagen ready, start;          // DATA_USA 5/1 t08 "READY?" y t00 "START!" (R16; los de R14)
  std::vector<P::Imagen> hud;      // DATA_USA 6/0 (kHud*)
  int cara = kGoku;               // ID de B3 del que juega (su cara del HUD: PintarCara)
};

std::mutex g_mu;
std::shared_ptr<Datos> g_d;           // del minijuego en curso
std::atomic<int> g_estado{0};         // 0 nada, 1 cargando, 2 combate, 3 resultado listo
// Minijuegos de otros frentes (RegistrarModulo): clave 0x7D0 + i. Ceros en la inicializacion estatica (antes de que
// los modulos se registren en la dinamica). g_modulo: corre uno (los ganchos de aqui no hacen nada).
Modulo g_modulos[32] = {};
std::atomic<bool> g_modulo{false};
Resultado g_res;
bool g_auto = false;                  // prueba: el jugador salta de objeto en objeto solo
bool g_meta_abierta = false;          // prueba ("meta"): la meta abierta desde el principio (para verla)
// Modo libre (hilo del juego): direccion de la marcha (palanca relativa a la camara) y orientacion de la camara.
std::atomic<float> g_dir_x{0}, g_dir_z{1}, g_yaw{0};
std::atomic<int> g_rx{0};   // palanca derecha (XInput, s16): camara (PalancaDerecha)
std::atomic<bool> g_anda{false}, g_colocado{false};
// IW Char_GetNextActionCode (EU 0x2A9440): |x| o |y| de la palanca > 0.9 corre (codigo 3, +0xB4 = 4.0); si no, anda
// (codigo 2, 1.8). El teclado siempre corre.
std::atomic<bool> g_corre{true};
bool g_cae = false;   // cayendo de una cornisa o en un salto (hilo del juego, Fotograma)
std::atomic<bool> g_pide_salto{false};
std::atomic<int> g_polvo_n{0};   // arranques del sprint (polvo): Fotograma lo sube, el panel pinta uno por cada
std::atomic<float> g_polvo_x{0}, g_polvo_y{0}, g_polvo_z{0}, g_polvo_dx{0}, g_polvo_dz{1};
std::atomic<bool> g_resbalando{false};   // pisó un platano de Bubbles: sin mando un momento
// R16: entrada y final propios de IW (decomp EU). Entrada: MgPly_Enter_State0A (0x3185A0) pone 0x11D al jugador y,
// despues, s_SPACE (0x29BFF0) la de su variante (= indice del minijuego, campo +4 de LoadWaitMinigamesFlag); el cartel
// del objetivo (LoadWaitGroundMinigameObjective 0x299900, parametro 0: 140 cuadros) para el reloj (+0x8D21). Final:
// estados 0x2F (superado, MgResult_ComputeScore(+1)) y 0x30 (no superado, (-1); anim 0x53/0x11A/... por variante).
std::atomic<bool> g_sin_mando{false};   // entrada o final en curso: ni andar ni scatto
std::atomic<bool> g_reloj_parado{false};   // entrada o cartel del objetivo: ni reloj ni NPC
std::atomic<int> g_cartel{0};           // cuadros del cartel del objetivo que quedan
std::atomic<bool> g_libre{false};       // andar libre de IW en curso (AnularAndar: sin el andar de combate de B3)
std::atomic<int> g_pad_prueba{0};     // PadPrueba
std::atomic<int> g_final{0};            // 0 jugando, 1 superado, 2 no superado (lo pide el panel), 3 combate acabado
struct Variante {
  int indice;
  uint32_t comienzo, bien, mal;   // s_SPACE; estado 0x2F; estado 0x30
};
// cada variante trae tres seguidas en el #BSK del jugador (922/914: 0x51 180 cuadros, 0x52 240, 0x53 240...): comienzo,
// superado y no superado (antes "superado" usaba la de no superado: Goku se hundia al ganar; auditoria5/C y videos)
constexpr Variante kVariantes[] = {{0, 0x51, 0x52, 0x53},      {1, 0x118, 0x119, 0x11A}, {2, 0x17C, 0x17D, 0x17E},
                                   {10, 0x49C, 0x49D, 0x49E}, {16, 0x6F4, 0x6F5, 0x6F6}};
// ponytail: la 14 (0x62C-0x62E) solo existe en el #BSK de 914, que es Cell semiperfecto (huesos CEL_*; buscado en todo
// DATA_CMN, 2026-10-16): en Gohan lo deformaba. Sin ella, la entrada comun 0x11D y sin pose final.
const Variante* VarianteDe(int indice) {
  for (const Variante& v : kVariantes)
    if (v.indice == indice) return &v;
  return nullptr;
}
constexpr int kCartelCuadros = 140;
// Huir de Piccolo (14): el rival de B3 ES Piccolo (no el pivote oculto). Su sitio lo pone la IA del panel (MoverNpc),
// corre con anim::Corredor (R10) y lleva el mando quieto del muneco; el jugador mira hacia su marcha, no hacia el rival.
std::atomic<float> g_npc_x{0}, g_npc_z{0};
// Hacia donde mira el luchador (u16, 1/65536 de vuelta), medido con DBZ3_SONDA_GIRO (pivote girando alrededor):
// X+6062 = angulo - 15759 y X+6074 = angulo - 15968, con angulo = atan2(dx, dz) hacia el rival; lo rehace cada cuadro.
constexpr uint32_t kMiradaA = 6062, kMiradaB = 6074;
// Scatto del jugador (IW Minigame_State14/15/16, decomp EU 0x2A0FC0 / 0x2A0CD0 / 0x2A0940): circulo (B del mando)
// con energia (+0x185C, 0..100) > 0; 0x14 arranque quieto, 0x15 carrera a 35, 0x16 frenada, 7 cansado (sin mando).
std::atomic<float> g_energia{100};
std::atomic<int> g_fase{0}, g_bebida{0};
std::atomic<bool> g_pide_scatto{false};
constexpr uint16_t kBotonScatto = 0x2000;   // registro del mando +12/+14 (disposicion de PS2: circulo)
constexpr uint16_t kBotonSalto = 0x4000;    // la cruz: salto (IW Minigame_ReadPlayerButtons EU 0x299520)
constexpr uint16_t kBotonStart = 0x0008;    // START en el mismo registro (PS2: select 1, L3 2, R3 4, start 8)
// Velocidades de IW: +0xB4 x Datos::k. En el campo de IW, k = 0.4 (Minigame_MovePlayerByStick con +0xB54 = 0); en el
// escenario de B3 (curso juntado), el andar de IW (4.0) = el andar de B3 (0.33 por cuadro, medido): k = 0.33 / 4, y asi
// se guardan las proporciones andar : IA (4.5) : scatto (35).
constexpr float kAndarB3 = 0.33f;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
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
float DistH(const V3& a, const V3& b) { return std::hypot(a.x - b.x, a.z - b.z); }
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
  const int na = d.indice == 2 ? 10 - 2 * dif : d.indice == 16 ? 0 : kCuantosA[dif];
  const int nb = kCuantosB[dif], nc = kCuantosC[dif];
  // D (G+0x16B9 = #ITM +0x10, en el Radar 2d + 6 y como mucho el +0x10; 0x3224B0/0x3237D0): solo en el 16 (las
  // marcas del radar). ponytail: en los demas paquetes con +0x10 = 16 (888, 893) su modelo no esta identificado.
  uint32_t mas10 = 0;
  std::memcpy(&mas10, &itm[0x10], 4);
  const int nd = d.indice == 16 ? std::min<int>(2 * dif + 6, int(mas10)) : 0;
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
  for (int k = 0; k < nb; ++k) crudos.push_back({pt(18 + k), 1});   // +0x140
  for (int k = 0; k < nc; ++k) crudos.push_back({pt(54 + k), 2, s16(0x480 + 2 * size_t(k))});   // +0x380
  for (int k = 0; k < nd; ++k) crudos.push_back({pt(72 + k), 3});   // +0x4A0
  d.total = na + nb + nc + nd + 2;   // 0x3237D0 (+1 del 16 y +1 del objeto 16A4: los 2)
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
  if (!d.campo) d.salida.y = d.meta.y = 0;
  d.radio_meta = kCogerA;   // modelos y radios a su tamano de IW; solo las posiciones van juntadas
  d.escala = s;
  int vacios = 0;
  for (const Crudo& c : crudos) {
    if (vacio(c.p)) {
      ++vacios;
      continue;
    }
    d.objetos.push_back({b3(c.p), c.grupo, std::max(c.grupo == 0 ? kCogerA : kCogerB, kCogerMin), c.valor});
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
    for (int k = 72; k < 90; ++k)
      if (!vacio(pt(k))) d.platanos.push_back(b3(pt(k)));
  } else if (d.indice == 14) {   // Piccolo empieza detras del jugador. ponytail: su sitio de salida no esta leido;
    d.npc = 2;                   // ~450 cuadros de andar (quieto, alcanza en ~7 s; andando, ~1 min; el scatto gana)
    const float dx = d.meta.x - d.salida.x, dz = d.meta.z - d.salida.z, l = std::max(std::hypot(dx, dz), 1e-3f);
    const float atras = 450.0f * 4.0f * d.k;
    V3 n{d.salida.x - dx / l * atras, d.salida.y, d.salida.z - dz / l * atras};
    if (const float r = std::hypot(n.x, n.z); !d.campo && r > kRadio) n = {n.x * kRadio / r, 0, n.z * kRadio / r};
    d.npc_inicio = n;
  }
  REXLOG_INFO("dbz3 iwmini: curso (dificultad {}): A {} B {} C {} D {} (sin sitio {}), total {}, escala {:.4f}, "
              "salida ({:.0f}, {:.0f}), meta ({:.0f}, {:.0f})",
              dif, na, nb, nc, nd, vacios, d.total, s, d.salida.x, d.salida.z, d.meta.x, d.meta.z);
  return true;
}

bool Cargar(Datos& d, std::string& error) {
  const std::filesystem::path iw = P::BuscarIW();
  P::AfsPs2 cmn, usa;
  if (iw.empty() || !cmn.Abrir(iw, "DATA_CMN.AFS") || !usa.Abrir(iw, "DATA_USA.AFS")) {
    error = "sin la copia de Infinite World en ps2_games";
    return false;
  }
  const Ficha* f = FichaDe(d.indice);
  d.limite = f->minutos * 60.0f;
  const std::vector<uint8_t> pack = cmn.Entrada(size_t(f->pack));
  std::vector<uint8_t> itm;
  for (uint32_t c = 0; c < 32 && itm.empty(); ++c) {
    std::vector<uint8_t> h = P::HijoAmb(pack, c);
    if (h.size() >= 4 && std::memcmp(h.data(), "#ITM", 4) == 0) itm = std::move(h);
  }
  d.campo = dbz3::escenarios::CampoIW(f->campo) >= 0;
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
  }
  if (d.npc == 1) {   // Bubbles (BBR_*, con cola: DATA_CMN 118 + texturas 119) y el platano BANANA_A02 (888/9 + 888/8)
    d.m_npc = CargarSuelto(cmn.Entrada(118), cmn.Entrada(119), true);
    // su carrera: el codigo 0xE2 del #BSK de Bubbles (DATA_CMN 912 hijo 0: lista +0x14, bloque [anim u16][almacen u16])
    // en su #AMM (912 hijo 1). ponytail: el almacen (3) se toma como el hijo 1, el unico con esa animacion (18
    // animaciones de los 47 huesos de BBR_*; los hijos 2 y 3 son de 2968 B)
    const std::vector<uint8_t> bb = cmn.Entrada(912), bsk = P::HijoAmb(bb, 0);
    uint32_t lst = 0, blk = 0;
    if (bsk.size() > 0x20 && std::memcmp(bsk.data(), "#BSK", 4) == 0) {
      std::memcpy(&lst, &bsk[0x14], 4);
      if (lst + 4 * 0xE3 <= bsk.size()) std::memcpy(&blk, &bsk[lst + 4 * 0xE2], 4);
    }
    if (d.m_npc.esq && blk && blk + 2 <= bsk.size())
      d.m_npc.anim = ps2::LeerAnim(P::HijoAmb(bb, 1), uint32_t(bsk[blk] | bsk[blk + 1] << 8), d.m_npc.esq->padre.size());
    REXLOG_INFO("dbz3 iwmini: Bubbles: carrera 0xE2 = {} cuadros", d.m_npc.anim.cuadros);
    d.m_platano = CargarModelo(pack, 9, 8, 0);
  }   // Piccolo (14) es el luchador de B3 (el rival del combate), no un modelo pintado
  if (f->pack == 896) {   // Radar: marcas ITEMA20 (896/1, texturas 896/0) y la bola de dragon DB_BLV_BALL (896/16, 896/15)
    d.m_marca = CargarModelo(pack, 1, 0, 0);
    d.m_meta = CargarModelo(pack, 16, 15, 0);
  }
  float alto_npc = 0, ancho_npc = 0;
  for (const auto& t : d.m_npc.malla.tris)
    for (const V3& p : t.p) alto_npc = std::max(alto_npc, p.y), ancho_npc = std::max(ancho_npc, std::fabs(p.x));
  REXLOG_INFO("dbz3 iwmini: modelos de IW: aro {} tri, moneda {}, bebida {}, meta {}, npc {} (alto {:.1f}, ancho {:.1f}), "
              "platano {}",
              d.m_aro.malla.tris.size(), d.m_moneda.malla.tris.size(), d.m_bebida.malla.tris.size(),
              d.m_meta.malla.tris.size(), d.m_npc.malla.tris.size(), alto_npc, ancho_npc, d.m_platano.malla.tris.size());
  const std::vector<uint8_t> ui = P::HijoAmb(usa.Entrada(6), 0);
  d.cara = f->jugador;   // F1: la cara es la del que juega (PintarCara), no un indice de 6/0 supuesto
  d.hud.resize(size_t(std::max(P::CuantasAmt(ui), 0)));
  for (int j : {kHudAro, kHudRelleno, kHudTime, kHudBarra, kRadarMarco, kRadarRejilla, kRadarPantalla, kRadarYo,
                kRadarBola, kRadarFlecha, kRadarCifras, kRadarMarca})
    if (j < int(d.hud.size())) d.hud[size_t(j)] = P::TexturaAmt(ui, j);
  d.clear = P::TexturaAmt(P::HijoAmb(usa.Entrada(334), 3), 69);
  const std::vector<uint8_t> t51 = P::HijoAmb(usa.Entrada(5), 1);
  d.ready = P::TexturaAmt(t51, 8);
  d.start = P::TexturaAmt(t51, 0);
  return true;
}

// ---- Pantalla (hilo de la interfaz) ----
// TIME y la hora con las letras lila/azul de DATA_USA 6/0 t03; acaba en `dcha` (centro vertical), `k` px por px.
void Tiempo(ImDrawList* dl, const Tex& time_, const char* s, ImVec2 dcha, float k) {
  auto glifo = [&](const Glifo& g, float x, float paso) {   // centrado en su casilla; las comillas, arriba
    const float x0 = x + (paso - g.w) * 0.5f * k, y0 = dcha.y - 12 * k;
    dl->AddImage(time_.id(), ImVec2(x0, y0), ImVec2(x0 + g.w * k, y0 + g.h * k), ImVec2(g.x / time_.w, g.y / time_.h),
                 ImVec2((g.x + g.w) / time_.w, (g.y + g.h) / time_.h));
  };
  if (!time_) {
    dl->AddText(nullptr, 24 * k, ImVec2(dcha.x - 220 * k, dcha.y - 12 * k), IM_COL32(200, 160, 255, 255), s);
    return;
  }
  float ancho = 0;
  for (const char* p = s; *p; ++p) ancho += *p >= '0' && *p <= '9' ? 20 : 12;
  float x = dcha.x - ancho * k;
  glifo(kTime, x - 84 * k, 72);
  for (const char* p = s; *p; ++p) {
    if (*p >= '0' && *p <= '9') {
      glifo(kCifra[*p - '0'], x, 20);
      x += 20 * k;
    } else {
      glifo(*p == '"' ? kSegundo : kMinuto, x, 12);
      x += 12 * k;
    }
  }
}
// HUD de IW (px de 1200x900, medidos en la captura del Radar a 1024x768): TIME arriba a la derecha y abajo a la
// izquierda el aro con la cara y su barra (`e` 0..1).
void PintarHud(ImDrawList* fg, const S::Area& a, const Tex& time, const Tex& aro, int cara, int forma, const Tex& barra,
               const Tex& relleno, float segundos, float e) {
  const int cs = int(std::max(segundos, 0.0f) * 100.0f);
  char txt[16];
  std::snprintf(txt, sizeof txt, "%02d'%02d\"%02d", std::min(cs / 6000, 99), cs / 100 % 60, cs % 100);
  Tiempo(fg, time, txt, a.At(1165, 110), 1.6f * a.s);
  if (barra) {
    const float kk = 1.6f * a.s;
    const ImVec2 b0 = a.At(150, 812);
    fg->AddImage(barra.id(), b0, ImVec2(b0.x + 240 * kk, b0.y + 40 * kk));
    e = std::clamp(e, 0.0f, 1.0f);
    if (relleno && e > 0)
      fg->AddImage(relleno.id(), ImVec2(b0.x + 22 * kk, b0.y + 12 * kk), ImVec2(b0.x + (22 + 200 * e) * kk, b0.y + 28 * kk),
                   ImVec2(0, 0), ImVec2(e, 1));
  }
  const ImVec2 c0 = a.At(28, 750);
  PintarCara(fg, c0.x, c0.y, 64 * 1.6f * a.s, cara, forma);
  if (aro) fg->AddImage(aro.id(), a.At(6, 728), a.At(6 + 88 * 1.6f, 728 + 88 * 1.6f));
}

class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (g_estado.load() != 2) return;
    const std::shared_ptr<Datos> d = Actual();
    if (!d) return;
    if (d.get() != hecho_) Preparar(*d);
    uint8_t* base = Base();
    const int st = P::EstadoCombate();
    // iw_fix: hasta el final del combate (estado 8 incluido): al llegar a el, B3 pinta su menu azul "Luchar otra vez?" sobre un
    // primer plano del ganador (~1,7 s hasta que el HUB lo cierra) y el fundido a negro dejaba de dibujarse
    if (!base || st < 3) return;
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (!mgr) return;
    const bool pausa = dbz3::GameDimmed() || dbz3::OverlayOpen();
    if (st == 4 && !fin_ && g_colocado.load()) Jugar(io, base, mgr, *d, pausa);
    if (pausa) return;
    {   // R16: letreros de IW (los de R14: 5/1 t08 en el READY de B3, t00 el primer segundo de juego)
      const float t = float(ImGui::GetTime());
      // START! cuando arranca el reloj, como en IW: despues de la entrada y del cartel del objetivo (no encima)
      if (st != 4 || g_reloj_parado.load()) empieza_t_ = -1;
      else if (empieza_t_ < 0) empieza_t_ = t;
      const Tex* c = st == 3 ? &ready_ : st == 4 && t - empieza_t_ < 1.0f ? &start_ : nullptr;
      if (c && *c) {
        const S::Area a = S::GameArea(io);
        const float k = a.s * 900 / 448;
        const ImVec2 m = a.At(600, 200 * 900 / 448);
        ImGui::GetForegroundDrawList()->AddImage(c->id(), ImVec2(m.x - c->w * k / 2, m.y - c->h * k / 2),
                                                 ImVec2(m.x + c->w * k / 2, m.y + c->h * k / 2));
      }
    }
    if (!fin_ && st >= 5) {   // acabo por otra via (no deberia: el pivote es invulnerable)
      fin_ = true;
      fin_t_ = float(ImGui::GetTime());
    }
    Dibujar(io, base, mgr, *d);
    if (fin_ && g_final.load() == 3 && fin_t_ > 1e29f) fin_t_ = float(ImGui::GetTime());
    if (g_cartel.load() > 0 && !fin_) DibujarCartel(io, *d);
    if (fin_) DibujarFin(io, float(ImGui::GetTime()));
  }

 private:
  void Preparar(const Datos& d) {
    hecho_ = &d;
    aro_ = Crear(d.aro);
    moneda_ = Crear(d.moneda);
    bebida_ = Crear(d.bebida);
    auto hud = [&](int j) { return Crear(j < int(d.hud.size()) ? d.hud[size_t(j)] : P::Imagen{}); };
    time_ = hud(kHudTime);
    hud_aro_ = hud(kHudAro);
    barra_ = hud(kHudBarra);
    relleno_ = hud(kHudRelleno);
    clear_ = Crear(d.clear);
    ready_ = Crear(d.ready);
    start_ = Crear(d.start);
    auto texs = [](const Modelo& m) { return ps2::Texturas(m); };   // R15: con el z-buffer del juego si se puede
    tex_aro_ = texs(d.m_aro);
    tex_moneda_ = texs(d.m_moneda);
    tex_bebida_ = texs(d.m_bebida);
    tex_meta_ = texs(d.m_meta);
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
    tex_marca_ = texs(d.m_marca);
    tex_npc_ = texs(d.m_npc);
    tex_platano_ = texs(d.m_platano);
    for (int j : {kRadarMarco, kRadarRejilla, kRadarPantalla, kRadarYo, kRadarBola, kRadarFlecha, kRadarCifras, kRadarMarca})
      radar_[j] = hud(j);
    cogido_.assign(d.objetos.size(), false);
    restante_ = d.limite;
    cogidos_ = aros_ = 0;
    if (g_meta_abierta) aros_ = d.num_a;
    npc_p_ = d.npc_inicio;
    npc_dir_ = {};
    npc_t_ = resbalon_ = 0;
    platanos_ = d.platanos;
    semilla_ = uint32_t(NowMs());
    g_resbalando = false;
    fin_ = meta_ = false;
    auto_ms_ = NowMs();
    auto_i_ = 0;
  }

  void Acabar(uint8_t* base, uint32_t mgr, const Datos& d, bool meta) {
    fin_ = true;
    meta_ = meta;
    fin_t_ = 1e30f;   // R16: el fundido empieza cuando acaba la animacion final de IW (Fotograma pone kFin)
    fin0_ = float(ImGui::GetTime());
    g_final = meta ? 1 : 2;
    const int dif1 = d.dificultad + 1;
    const float r = float(cogidos_) / float(std::max(d.total, 1));   // G+0x165C / G+0x165A (la meta no suma)
    const int rango = r >= 0.95f ? 4 : r >= 0.8f ? 3 : r >= 0.65f ? 2 : r >= 0.5f ? 1 : 0;
    const int puntos = (kBonusRango[rango] * dif1 * dif1 + int(std::max(restante_, 0.0f)) * dif1 * 10) / 100 * 100;
    Resultado res;
    res.clave = d.clave;
    res.fila = d.fila;
    res.superado = meta;
    res.centesimas = int((d.limite - std::max(restante_, 0.0f)) * 100.0f);
    res.puntos = meta ? puntos : 0;
    res.rango = meta ? rango : 0;
    res.monedas = cogidos_;
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
    const bool parado = g_reloj_parado.load();   // R16: entrada y cartel del objetivo de IW
    if (!pausa && !parado) restante_ -= std::min(io.DeltaTime, 0.1f);
    if (g_auto && !parado && NowMs() - auto_ms_ > 700) {   // prueba: a cada aro y bebida (y despues a la meta)
      auto_ms_ = NowMs();
      while (auto_i_ < d.objetos.size() && (d.objetos[auto_i_].grupo == 1 || cogido_[auto_i_]))
        ++auto_i_;
      V3 p = auto_i_ < d.objetos.size() ? d.objetos[auto_i_].p : d.npc == 1 ? npc_p_ : d.meta;
      if (!d.campo) p.y = 0;   // en el campo de IW, a su altura (el Camino de la Serpiente va por arriba)
      Poner(base, x0, p);
    }
    const V3 yo = Leer(base, x0);
    for (size_t k = 0; k < d.objetos.size(); ++k) {
      const Objeto& o = d.objetos[k];
      if (cogido_[k] || DistH(yo, o.p) > o.radio) continue;   // ponytail: en el plano (B3 apenas salta)
      cogido_[k] = true;
      ++cogidos_;
      if (o.grupo == 2) {   // bebida: energia (tope 100)
        g_bebida += o.valor;
        REXLOG_INFO("dbz3 iwmini: bebida +{} de energia", o.valor);
      }
      if (o.grupo == 0 || o.grupo == 3)
        REXLOG_INFO("dbz3 iwmini: aro {} cogido ({} objetos de {}) quedando {:.1f} s", ++aros_, cogidos_, d.total,
                    restante_);
    }
    // la meta se abre al coger todos los aros (0x31F520: G+0x166E >= G+0x166F -> +0x1DC = 1; en el 10, la nave)
    if (d.npc && !pausa && !parado) {   // en la pausa (y la entrada) no se mueve
      MoverNpc(io, base, mgr, d, yo);
      if (fin_) return;
    }
    if (d.npc != 1 && aros_ >= d.num_a && DistH(yo, d.meta) < d.radio_meta) Acabar(base, mgr, d, true);
    else if (restante_ <= 0) Acabar(base, mgr, d, false);
  }

  // NPC del MSNMW (decomp EU s_BBR_POST_EXEC 0x3172E0 y MgPly_Calc*Vel 0x316B90-0x3170D0). Huir (Bubbles): cada
  // decision elige el modo 1..3 al azar y dura 1: azar % (20d + 100) + 35 cuadros, 2: azar % 100 + 100 (y a veces deja
  // un platano: +0x62), 3: azar % (20d + 100) + 60; la velocidad es la de alejarse del jugador desviada hacia un lado un
  // azar % 10 x 0.1, a 4.5 x 0.4 por cuadro (+0xB54 = 0 -> 0.4). Perseguir (Piccolo, modo 4): hacia el jugador.
  // Los dos van a 4.5 (estado 9), algo mas que el andar del jugador (4.0): hay que hacer el scatto (35) para
  // alcanzar a Bubbles o huir de Piccolo, y la energia se acaba. ponytail: el +0xCAC (factor por personaje) se toma 1.
  void MoverNpc(ImGuiIO& io, uint8_t* base, uint32_t mgr, const Datos& d, const V3& yo) {
    const float cuadros = std::min(io.DeltaTime, 0.1f) * 60.0f;
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
        if (npc_modo_ == 2 && (azar() & 1)) platanos_.push_back(npc_p_);   // el platano que tira
      }
      vel = 4.5f * d.k;   // corredor de la IA: estado 9, codigo 0xE2, +0xB4 = 4.5 (MgPly_Enter_State09)
    } else {
      npc_modo_ = 4;   // Piccolo (luchador 1 del 0xE): el mismo estado 9 hacia el jugador
      npc_dir_ = {-dx, 0, -dz};
      vel = 4.5f * d.k;
    }
    static const bool quieto = std::getenv("DBZ3_IWMINI_NPC_QUIETO") != nullptr;   // prueba: verle de cerca
    if (quieto) vel = 0;
    npc_p_ = {npc_p_.x + npc_dir_.x * vel * cuadros, npc_p_.y, npc_p_.z + npc_dir_.z * vel * cuadros};
    if (d.campo) npc_p_.y = dbz3::suelo_iw::Altura(npc_p_.x, npc_p_.z, npc_p_.y);   // en el suelo del campo (R11)
    const float r = std::hypot(npc_p_.x, npc_p_.z);
    if (d.campo && (std::fabs(npc_p_.x) > 1476.0f || std::fabs(npc_p_.z) > 1476.0f)) {   // borde del campo de R8
      npc_p_ = {std::clamp(npc_p_.x, -1476.0f, 1476.0f), npc_p_.y, std::clamp(npc_p_.z, -1476.0f, 1476.0f)};
      npc_t_ = 0;
    } else if (!d.campo && r > kRadio + 15.0f) {   // no sale del campo (Char_WrapMoveInGrid): vuelve hacia dentro
      npc_p_ = {npc_p_.x * (kRadio + 15.0f) / r, 0, npc_p_.z * (kRadio + 15.0f) / r};
      npc_t_ = 0;
    }
    g_npc_x = npc_p_.x;   // Piccolo de B3 (Fotograma lo pone ahi)
    g_npc_z = npc_p_.z;
    // platanos: al pisarlos, un segundo sin mando (resbala). ponytail: el tiempo del resbalon no esta leido
    for (size_t i = 0; i < platanos_.size(); ++i) {
      if (DistH(yo, platanos_[i]) < 6.0f) {
        platanos_.erase(platanos_.begin() + long(i));
        resbalon_ = 1.0f;
        REXLOG_INFO("dbz3 iwmini: platano: resbala");
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
    return ps2::CamaraCombate(base, io.DisplaySize);   // R15: la misma, con la profundidad del juego (Camara::Prof)
  }

  void Dibujar(ImGuiIO& io, uint8_t* base, uint32_t mgr, const Datos& d) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Camara cam = Cam(io, base, mgr);
    const float t = float(ImGui::GetTime());
    struct Cosa {
      float k;
      size_t i;   // objetos; d.objetos.size() = la meta
    };
    std::vector<Cosa> cosas;
    ImVec2 o;
    float k;
    for (size_t i = 0; i < d.objetos.size(); ++i)
      if (!cogido_[i] && cam.P(d.objetos[i].p, o, k)) cosas.push_back({k, i});
    // alcanzada (fin_), la meta ya no se pinta: con el jugador quieto dentro (pose final de IW) la camara quedaba dentro
    // de su modelo y la pantalla entera salia azul (la "pantalla azul" de R16)
    if (d.npc != 1 && aros_ >= d.num_a && !fin_ && cam.P(d.meta, o, k)) cosas.push_back({k, d.objetos.size()});
    std::sort(cosas.begin(), cosas.end(), [](const Cosa& a, const Cosa& b) { return a.k < b.k; });
    // con los modelos 3D de IW: todos los triangulos de lejos a cerca; sin ellos, los dibujos planos de antes
    escena_.Empezar();
    for (const Cosa& c : cosas) {
      if (c.i == d.objetos.size()) {
        if (d.m_meta.malla) Poner3D(cam, d.m_meta, tex_meta_, d.meta, 0.0f, 1.0f);
        else DibujarMeta(dl, cam, d.meta, d.radio_meta, t);
        continue;
      }
      const Objeto& ob = d.objetos[c.i];
      const Modelo& m = ob.grupo == 0 ? d.m_aro : ob.grupo == 1 ? d.m_moneda : ob.grupo == 3 ? d.m_marca : d.m_bebida;
      const std::vector<Tex>& tx = ob.grupo == 0   ? tex_aro_
                                   : ob.grupo == 1 ? tex_moneda_
                                   : ob.grupo == 3 ? tex_marca_
                                                   : tex_bebida_;
      // giro de su #AMM (todos a la vez, como el cuadro comun del juego: MgDraw_* de 0x320F30 en el decomp EU)
      const float giro = m.giro.cuadros ? m.giro.En(t) : t * 2.0f;
      if (m.malla) Poner3D(cam, m, tx, ob.p, giro, 1.0f);
      else if (ob.grupo == 0) DibujarAro(dl, cam, ob.p, t);
      else DibujarMoneda(dl, cam, ob, t);
    }
    if (d.npc && d.m_npc.malla) {   // Bubbles de IW mirando hacia donde va, con su carrera (0xE2) si se leyo
      const bool anima = d.m_npc.esq && d.m_npc.anim;
      if (anima) Posar(*d.m_npc.esq, d.m_npc.anim, t, npc_pose_.malla);
      Poner3D(cam, anima ? npc_pose_ : d.m_npc, tex_npc_, npc_p_, std::atan2(npc_dir_.x, npc_dir_.z), 1.0f);
    }
    for (const V3& pl : platanos_)
      if (d.m_platano.malla) Poner3D(cam, d.m_platano, tex_platano_, pl, 0.0f, 1.0f);
    Polvo(dl, cam, d);
    Tapas(cam, base, mgr + kX0);
    Pintar3D(dl);
    // HUD de IW (px de 1200x900, medidos en la captura del Radar a 1024x768): TIME arriba a la derecha y abajo a la
    // izquierda el aro con la cara y su barra (la energia del scatto).
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const S::Area a = S::GameArea(io);
    PintarHud(fg, a, time_, hud_aro_, d.cara, 0, barra_, relleno_, restante_, g_energia.load() / 100.0f);   // (+0x185C)
    if (d.indice == 16) DibujarRadar(fg, a, d, Leer(base, mgr + kX0));
  }

  // Radar del Dragon (captura 86 del original): abajo a la derecha, pantalla rosa con su cuadricula, marcas en rombo
  // rosa, la bola en punto naranja, el jugador en el centro (triangulo) y arriba la flecha; encima "01/08" (marcas
  // cogidas / marcas). Alcance del codigo: d^2 < 2.89e6 (1700 de IW), aqui x la escala del curso.
  void DibujarRadar(ImDrawList* fg, const S::Area& a, const Datos& d, const V3& yo) {
    const float k = 1.5f * a.s;
    const ImVec2 o = a.At(846, 475);   // esquina del marco t04 (208x232)
    auto pon = [&](int j, ImVec2 c, float w, float h, ImU32 tinte = IM_COL32_WHITE) {
      if (radar_[j]) fg->AddImage(radar_[j].id(), ImVec2(c.x - w * k * 0.5f, c.y - h * k * 0.5f),
                                  ImVec2(c.x + w * k * 0.5f, c.y + h * k * 0.5f), ImVec2(0, 0), ImVec2(1, 1), tinte);
    };
    const ImVec2 cen(o.x + 104 * k, o.y + 128 * k);   // centro de la pantalla redonda dentro del marco
    pon(kRadarPantalla, cen, 152, 160, IM_COL32(220, 70, 110, 255));   // la pantalla del Radar es rosa en la captura
    pon(kRadarRejilla, cen, 152, 160);
    const float radio = 62.0f * k, alcance = 1700.0f * d.escala, yaw = g_yaw.load();
    const float fx = std::sin(yaw), fz = std::cos(yaw);
    auto marca = [&](const V3& p, int j, float w) {
      const float dx = p.x - yo.x, dz = p.z - yo.z;
      const float ade = dx * fx + dz * fz, der = -(dx * fz - dz * fx);   // delante = arriba
      float u = der / alcance, v = ade / alcance;
      const float l = std::hypot(u, v);
      if (l > 1.0f) return;   // fuera de alcance: el juego no lo apunta
      pon(j, ImVec2(cen.x + u * radio, cen.y - v * radio), w, w);
    };
    int hechas = 0, total = 0;
    for (size_t i = 0; i < d.objetos.size(); ++i) {
      if (d.objetos[i].grupo != 3) continue;
      ++total;
      if (cogido_[i]) ++hechas;
      else marca(d.objetos[i].p, kRadarMarca, 16);
    }
    marca(d.meta, kRadarBola, 16);
    pon(kRadarYo, cen, 24, 24);
    pon(kRadarFlecha, ImVec2(cen.x, cen.y - radio - 4 * k), 24, 24);
    pon(kRadarMarco, ImVec2(o.x + 104 * k, o.y + 116 * k), 208, 232);
    // "01/08" con las cifras cian de t12, acabando en x 1142, y 230 (px de 1200x900)
    if (!radar_[kRadarCifras]) return;
    char s[8];
    std::snprintf(s, sizeof s, "%02d/%02d", std::min(hechas, 99), std::min(total, 99));
    const Tex& tc = radar_[kRadarCifras];
    const float kc = 1.6f * a.s;
    float x = a.At(1142, 0).x - (4 * 22 + 16) * kc;
    const float y = a.At(0, 230).y - 12 * kc;
    for (const char* p = s; *p; ++p) {
      const Glifo& g = *p == '/' ? kBarra : kCian[*p - '0'];
      const float paso = *p == '/' ? 16.0f : 22.0f;
      const float x0 = x + (paso - g.w) * 0.5f * kc;
      fg->AddImage(tc.id(), ImVec2(x0, y), ImVec2(x0 + g.w * kc, y + g.h * kc), ImVec2(g.x / tc.w, g.y / tc.h),
                   ImVec2((g.x + g.w) / tc.w, (g.y + g.h) / tc.h));
      x += paso * kc;
    }
  }

  // Pintado 3D: ps2::Escena (triangulos de lejos a cerca, tapados por las esferas del luchador)
  ps2::Escena escena_;
  // Polvo del sprint: una instancia del #AME 934/26 por arranque (Fotograma sube g_polvo_n), en los pies y hacia atras
  std::vector<std::shared_ptr<dbz3::iwame::ame::Instancia>> polvo_;
  int polvo_visto_ = 0;
  uint32_t semilla_polvo_ = 7;
  std::vector<Tex> tex_polvo_, tx_atlas_, tx_suma_;
  void Polvo(ImDrawList* dl, const Camara& cam, const Datos& d) {
    namespace ame = dbz3::iwame::ame;
    if (const int n = g_polvo_n.load(); n != polvo_visto_) {
      polvo_visto_ = n;
      // ponytail: sin verificar en pantalla (la malla 934/1 mide 120 de medio lado y no se ve en las capturas
      // 163101): apagado salvo con DBZ3_IW_POLVO=1 hasta confirmar malla y texturas
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

  // Meta abierta: circulo en el suelo con su radio de llegada. ponytail: en IW es la nave de 903/7 (modelo del campo).
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
  // Cartel del objetivo (IW LoadWaitGroundMinigameObjective, 140 cuadros): la frase de la fila (DATA_USA 334/6).
  // ponytail: el indice de la pieza de UI del cartel se pierde en el decomp: banda oscura con el texto del juego.
  void DibujarCartel(ImGuiIO& io, const Datos& d) {
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

  void DibujarFin(ImGuiIO& io, float t) {
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const float k = std::clamp((t - fin_t_) / 0.6f, 0.0f, 1.0f);
    fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * k)));
    if (!meta_ || !clear_ || t - fin_t_ > 1.2f) return;   // CLEAR sobre el fundido un momento; despues solo negro hasta el mapa
    const S::Area a = S::GameArea(io);
    const float s = 6.0f * a.s * (0.8f + 0.2f * std::min(1.0f, (t - fin0_) * 2.0f));   // desde la meta, sobre la pose
    const ImVec2 c = a.At(600, 600);   // bajo la cara del primer plano final (antes 430 la tapaba)
    fg->AddImage(clear_.id(), ImVec2(c.x - clear_.w * s * 0.5f, c.y - clear_.h * s * 0.5f),
                 ImVec2(c.x + clear_.w * s * 0.5f, c.y + clear_.h * s * 0.5f));
  }

  const Datos* hecho_ = nullptr;
  Tex aro_, moneda_, bebida_, time_, hud_aro_, barra_, relleno_, clear_, ready_, start_;
  float empieza_t_ = -1;   // cuando empezo la pelea (START! un segundo)
  std::vector<Tex> tex_aro_, tex_moneda_, tex_bebida_, tex_meta_, tex_marca_;   // texturas de los modelos 3D
  Tex radar_[16];                                                              // piezas de 6/0 del Radar
  std::vector<bool> cogido_;
  int cogidos_ = 0, aros_ = 0;
  // NPC (Bubbles o Piccolo) y platanos
  V3 npc_p_, npc_dir_;
  float npc_t_ = 0, resbalon_ = 0;
  int npc_modo_ = 0;
  Modelo npc_pose_;   // Bubbles en el cuadro de su carrera (Posar)
  uint32_t semilla_ = 12345;
  int64_t npc_log_ = 0;
  std::vector<V3> platanos_;
  std::vector<Tex> tex_npc_, tex_platano_;
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
  if (!dbz3::hub::RequestBattleEx(f->jugador, f->pivote, campo >= 0 ? campo : f->escenario, true, -1, 0, 0, 0, 0))
    return false;
  // el rival lleva el mando virtual del muneco (sin IA y "conectado": sin el, el juego pausa por el mando 2); el pivote
  // no se mueve ni ataca y a Piccolo (14) Mando le inyecta andar hacia el jugador en ese mando
  dbz3::combate::SetMuneco(1);
  dbz3::combate::OcultarHud(true);  // sin marcador de combate de B3: el HUD es el de IW
  P::Reglas r;
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
      std::lock_guard<std::mutex> lk(g_mu);
      if (g_res.clave != d->clave) {   // salio desde la pausa: no superado
        g_res = Resultado{};
        g_res.clave = d->clave;
        g_res.fila = d->fila;
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
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_res = r;
  }
  REXLOG_INFO("dbz3 iwmini: modulo 0x{:X}: resultado listo (superado {}, rango {}, puntos {})", r.clave, r.superado,
              r.rango, r.puntos);
  g_modulo = false;
  g_estado = 3;
}

// Cara del aro del HUD de los minijuegos. IW (6/0 t29..t38, hoja en E:\DBZ3HD_trabajo\R14\caras_6_0.png) solo trae
// Goku, Goku SSJ, Gohan SSJ2, Krilin, Vegeta SSJ, Trunks SSJ, Piccolo, Yamcha (t36, no Gohan), Ten y Cell: se usa la de
// IW solo si es del mismo personaje y forma; si no, la nativa de B3 (caras/<id>, native_ui) recortada en circulo.
// ponytail: Gohan SSJ2 (t31) y Cell (t38) sin mapear (forma de B3 sin confirmar); van con la nativa.
int CaraIW(int id, int forma) {
  switch (id) {
    case 0: return forma == 0 ? 29 : -1;    // Goku (la forma 1 de B3 no es seguro SSJ)
    case 7: return forma == 1 ? 33 : -1;    // Vegeta SSJ
    case 8: return forma == 1 ? 34 : -1;    // Trunks SSJ
    case 10: return forma == 0 ? 32 : -1;   // Krilin
    case 11: return forma == 0 ? 35 : -1;   // Piccolo
    case 12: return forma == 0 ? 37 : -1;   // Ten
    case 13: return forma == 0 ? 36 : -1;   // Yamcha
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
  static Tex time, aro, barra_t, relleno;
  static std::vector<uint8_t> ui;
  if (!leido) {   // DATA_USA 6/0 de la copia del jugador, una vez (hilo de la interfaz)
    leido = true;
    P::AfsPs2 usa;
    if (usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) ui = P::HijoAmb(usa.Entrada(6), 0);
    if (P::CuantasAmt(ui) <= kHudBarra) ui.clear();   // sin el HUD de IW: solo el texto de TIME
    time = Crear(ui.empty() ? P::Imagen{} : P::TexturaAmt(ui, kHudTime));
    aro = Crear(ui.empty() ? P::Imagen{} : P::TexturaAmt(ui, kHudAro));
    barra_t = Crear(ui.empty() ? P::Imagen{} : P::TexturaAmt(ui, kHudBarra));
    relleno = Crear(ui.empty() ? P::Imagen{} : P::TexturaAmt(ui, kHudRelleno));
  }
  const ImGuiIO& io = ImGui::GetIO();
  PintarHud(ImGui::GetForegroundDrawList(), S::GameArea(io), time, aro, cara, forma, barra_t, relleno, segundos, barra);
}

bool Empezar(int clave, int fila) {
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
    g_estado = 2;
    const int dif = std::clamp(g_dificultad.load(), 0, 5);
    REXLOG_INFO("dbz3 iwmini: casilla 0x{:X} (fila {}): modulo de otro frente (dificultad {})", clave, fila, dif);
    if (!m(clave, fila, dif)) {
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
  std::string error;
  if (!Cargar(*d, error)) {
    REXLOG_WARN("dbz3 iwmini: {}", error);
    return false;
  }
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_d = d;
    g_res = Resultado{};
  }
  g_colocado = false;
  g_anda = false;
  const float dx = d->meta.x - d->salida.x, dz = d->meta.z - d->salida.z, l = std::max(std::hypot(dx, dz), 1e-3f);
  g_dir_x = dx / l;
  g_dir_z = dz / l;
  g_yaw = std::atan2(dx, dz);   // la camara mira de la salida a la meta
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
void PadPrueba(int dir) { g_pad_prueba = dir; }

bool AnularAndar(uint8_t* base, uint32_t x, uint32_t accion) {
  if ((accion != 2 && accion != 3) || !g_libre.load() || g_estado.load() != 2) return false;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  return mgr && x == mgr + kX0;
}

void Dificultad(int ficha) { g_dificultad = std::clamp(ficha, 0, 5); }   // F5: la de la ficha, tal cual

std::atomic<int> g_ry{0};
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

// Campo de vision del scatto: IW (camara +0x120) va de 35 a 50 (+2.5 por cuadro), baja a 40 (-0.38) en la carrera y a
// 35 en la frenada. En B3 (mgr+0x1C4, 0.785 de serie) en proporcion, como gigantes.cpp; iw <= 0 devuelve el del juego.
static void FovIW(uint8_t* base, float iw) {
  static float fov_juego = 0, fov_puesto = 0;
  static bool tocado = false;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr || (iw <= 0 && !tocado)) return;
  if (iw <= 0) {
    StoreF(base, mgr + kFov, fov_juego);
    tocado = false;
    return;
  }
  const float fov = LoadF(base, mgr + kFov);
  if (!tocado || fov != fov_puesto) fov_juego = fov;   // el juego lo rehace (o es el primero): ese es el suyo
  if (!(fov_juego > 0.3f && fov_juego < 1.2f)) return;
  fov_puesto = fov_juego * iw / 35.0f;
  StoreF(base, mgr + kFov, fov_puesto);
  tocado = true;
}

// Final del minijuego (g_final 1 superado / 2 no superado): la animacion entera y medio segundo quieto en su ultimo
// cuadro (tope 5 s), como el estado 0x2F/0x30; despues el fundido a negro (g_final = 3: lo lee el panel).
int g_t_fin = 0;
void PasoFinal(uint8_t* base, uint32_t mgr, const Variante* v, int fin) {
  static int quieto = 0;
  if (g_t_fin == 1) quieto = 0;
  ++g_t_fin;
  if (!v || dbz3::anim::EstadoPose() == 2) ++quieto;
  if (quieto >= 30 || g_t_fin > 300) {
    if (std::getenv("DBZ3_IWMINI_FIN_TARDE")) REX_STORE_U8(mgr + kFin, uint8_t(fin == 1 ? 1 : 2));   // el combate acaba como un ring out
    g_final = 3;
  }
}

// El final del combate de B3 (estados 5 y 6) corre mientras sigue el nuestro: se mantienen la camara en primer plano y el
// temporizador del final; la pose la mantiene anim_iw (reafirma el codigo cada cuadro).
void FinB3(uint8_t* base, uint32_t mgr) {
  const std::shared_ptr<Datos> d = Actual();
  if (!d) return;
  const Variante* v = VarianteDe(d->indice);
  if (g_final.load() != 3) PasoFinal(base, mgr, v, g_final.load());
  const V3 p = Leer(base, mgr + kX0);
  const float dx = g_dir_x.load(), dz = g_dir_z.load();
  float r[3] = {0, 0, 0};   // la camara sigue el salto de superado (0x52: 69 de alto y 50 hacia delante)
  const bool hay = dbz3::anim::RaizActual(base, mgr + kX0, r);
  const float sube = hay && r[1] > 12.0f ? (r[1] - 12.0f) * 0.9f : 0.0f, delante = hay && r[2] > 0 ? r[2] * 0.9f : 0.0f;
  const float tx = p.x + dx * delante, tz = p.z + dz * delante;
  const float ojo[3] = {tx - dx * 45.0f, p.y + 14.0f + sube, tz - dz * 45.0f}, obj[3] = {tx, p.y + 12.0f + sube, tz};
  dbz3::cine::Camara(base, ojo, obj);
}

void Fotograma(uint8_t* base, uint32_t estado) {
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
  if (!d || !mgr) return;
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  if (estado == 3 || !g_colocado.load()) {   // todo el READY y el primer cuadro de la pelea: en la salida
    Poner(base, x0, d->salida);
    if (estado == 4) {
      g_colocado = true;
      g_final = 0;
      REXLOG_INFO("dbz3 iwmini: salida puesta ({:.0f}, {:.0f}); meta en ({:.0f}, {:.0f})", d->salida.x, d->salida.z,
                  d->meta.x, d->meta.z);
    }
  }
  V3 p = Leer(base, x0);
  {   // R16: entrada (0x11D y la de la variante, sin mando, reloj parado + cartel 140 cuadros) y final (0x2F / 0x30)
    static int paso = 0, t_paso = 0;
    int& t_fin = g_t_fin;
    const Variante* v = VarianteDe(d->indice);
    if (estado == 3) paso = 0, t_fin = 0, g_cartel = 0;
    if (estado == 4 && g_colocado.load()) {
      ++t_paso;
      if (paso == 0) {
        paso = 1, t_paso = 0, g_cartel = kCartelCuadros;
        dbz3::anim::Reproducir(x0, 0x11D, true);   // sujeta: sin la guardia de B3 entre poses (auditoria10/D)
        REXLOG_INFO("dbz3 iwmini: entrada de IW (0x11D, variante 0x{:X})", v ? v->comienzo : 0);
      } else if (paso == 1 && (dbz3::anim::EstadoPose() == 2 || t_paso > 480)) {   // tope: la mas larga (0x62C) dura 420
        paso = 2, t_paso = 0;
        if (v) dbz3::anim::Reproducir(x0, v->comienzo, true);
      } else if (paso == 2 && (!v || dbz3::anim::EstadoPose() == 2 || t_paso > 480)) {
        paso = 3, t_paso = 0;
        REXLOG_INFO("dbz3 iwmini: entrada acabada: mando al jugador");
      }
      if (paso >= 1 && g_cartel.load() > 0) g_cartel = g_cartel.load() - 1;
      const int fin = g_final.load();
      if ((fin == 1 || fin == 2) && t_fin == 0) {
        t_fin = 1;
        // iw_fix: el combate de B3 acaba YA (como un ring out, pasarela.cpp) y su final (estados 5 y 6: 3 + 6,5 s) corre a la
        // vez que el nuestro (la animacion y CLEAR): antes se esperaba a acabar la animacion y la pantalla se quedaba en
        // negro con el cartel ~11 s mas. Fotograma sigue con la camara y el fundido durante esos estados.
        if (!std::getenv("DBZ3_IWMINI_FIN_TARDE")) REX_STORE_U8(mgr + kFin, uint8_t(fin == 1 ? 1 : 2));
        // PRUEBA (DBZ3_IWMINI_RAFAGA_FIN=1): 40 capturas (una cada 15 pintados) desde que acaba el minijuego
        if (std::getenv("DBZ3_IWMINI_RAFAGA_FIN")) std::ofstream("dbz3_shot.req") << "40 15";
        dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
        // ponytail: la animacion final (0x62E...) deja la pantalla en azul liso mientras suena (medido R16, sin causa
        // aun); apagada salvo con DBZ3_IWMINI_FINAL=1 hasta encontrarla
        // 2026-10-16: la pantalla azul era de la prueba automatica (teletransporte a la meta); encendida de serie como IW
        // (videos: primer plano del personaje con CLEAR!/FAILED! sobre el escenario). DBZ3_IWMINI_FINAL=0 la apaga.
        static const char* env_final = std::getenv("DBZ3_IWMINI_FINAL");
        static const bool con_final = !env_final || env_final[0] != '0';
        // prueba de la pantalla azul: DBZ3_IWMINI_FINAL=2 pone la animacion de comienzo (que se ve bien) en el final
        static const bool con_comienzo = env_final && env_final[0] == '2';
        if (v && con_final) dbz3::anim::Reproducir(x0, con_comienzo ? v->comienzo : fin == 1 ? v->bien : v->mal, true);
        REXLOG_INFO("dbz3 iwmini: final de IW ({}, anim 0x{:X})", fin == 1 ? "superado" : "no superado",
                    v ? (fin == 1 ? v->bien : v->mal) : 0);
      } else if (t_fin > 0 && fin != 3) {
        PasoFinal(base, mgr, v, fin);
      }
    }
    g_sin_mando = estado == 4 && (paso == 1 || paso == 2 || g_final.load() != 0);
    g_reloj_parado = estado != 4 || paso == 1 || paso == 2 || g_cartel.load() > 0;
  }
  const float dx = g_dir_x.load(), dz = g_dir_z.load();
  // Andar de IW (estados 1-4, +0xB4 = 4.0): sin campo de IW es el de B3 (0.33 por cuadro); en el campo, 4.0 x 0.4 = 1.6
  // en unidades de IW (se anade lo que le falta al de B3). Piccolo (14): el rival de B3 va detras y el juego llevaria al
  // jugador hacia el: la marcha entera es esta, desde donde quedo el cuadro anterior.
  const float andar = (g_corre.load() ? 4.0f : 1.8f) * d->k;
  const bool picc = d->npc == 2;
  static V3 propio;
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
    if (estado == 3 || !g_colocado.load()) {
      propio = d->salida;
      g_npc_x = d->npc_inicio.x;
      g_npc_z = d->npc_inicio.z;
    } else if (estado == 4 && g_anda.load() && g_fase.load() == 0) {
      // No se relee del luchador: B3 lo acerca al rival (distancia maxima entre luchadores; medido: con relectura
      // Piccolo "alcanzaba" a 433 en 1,7 s). Las paredes del #WCF: suelo_iw::Mover (R11, Collision_MoveSphere de IW).
      const float a0[3] = {propio.x, p.y, propio.z}, a1[3] = {propio.x + dx * andar, p.y, propio.z + dz * andar};
      float s[3];
      MoverCampo(a0, a1, s);
      propio = {s[0], 0, s[2]};
    }
    p = {propio.x, p.y, propio.z};
  } else if (estado == 4 && g_colocado.load() && g_anda.load() && g_fase.load() == 0) {
    // con el andar libre (g_libre) B3 no anda: la marcha entera es esta; si no, se anade lo que le falta al de B3
    const float paso = g_libre.load() ? andar : std::max(0.0f, andar - kAndarB3);
    const float a0[3] = {p.x, p.y, p.z}, a1[3] = {p.x + dx * paso, p.y, p.z + dz * paso};
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
  static float fov_iw = 35.0f;
  static bool sube = false;
  int fase = g_fase.load();
  if (estado == 3) {
    g_energia = 100.0f;
    g_pide_scatto = false;
    g_bebida = 0;
    fase = 0;
    t = 0;
    fov_iw = 35.0f;
  }
  if (estado == 4 && g_colocado.load()) {
    float e = std::min(100.0f, g_energia.load() + 0.02f + float(g_bebida.exchange(0)));
    const bool pide = g_pide_scatto.exchange(false);
    if (fase == 0 && pide && e > 0) {   // cansado (fase 7) no acepta el boton
      fase = 0x14;
      t = 0;
      dbz3::anim::Scatto(x0, dbz3::anim::kInicio);
      REXLOG_INFO("dbz3 iwmini: scatto (energia {:.1f})", e);
    }
    if (fase >= 0x14) e -= 0.5f;
    if (fase == 7) {   // cansado: 0x05 + 0x06 (61 cuadros) con anim::Cansado, y 0x07 (31) ya en guardia
      // tras 0x05 y 0x06, la recuperacion 0x07 de IW (State08_Enter, 31 cuadros), no la guardia (auditoria7/K)
      if (++t == 61) dbz3::anim::Reproducir(x0, 0x807, true);
      if (t >= 92) fase = 0;
    } else if (fase == 0x14) {
      if (++t >= 12) {   // fin de la animacion 0x19 (12 cuadros)
        fase = 0x15;
        t = 0;
        sube = true;
        // el polvo del arranque (IW State15_DashEnter: Effect 0x34 en el jugador, hacia donde mira la camara)
        g_polvo_x = p.x, g_polvo_y = p.y, g_polvo_z = p.z, g_polvo_dx = dx, g_polvo_dz = dz;
        ++g_polvo_n;
        dbz3::anim::Scatto(x0, dbz3::anim::kBucle);
      }
    } else if (fase == 0x15) {
      const float v = 35.0f * d->k;
      if (d->campo) {   // contra las paredes del #WCF con la esfera del scatto (escala 0.5, R11)
        const float a0[3] = {p.x, p.y, p.z}, a1[3] = {p.x + dx * v, p.y, p.z + dz * v};
        float s[3];
        MoverCampo(a0, a1, s, 0.5f);
        p = {s[0], p.y, s[2]};
      } else {
        p = {p.x + dx * v, p.y, p.z + dz * v};
      }
      Poner(base, x0, p);
      if (sube) {
        fov_iw += 2.5f;
        if (fov_iw >= 50.0f) fov_iw = 50.0f, sube = false;
      } else {
        fov_iw = std::max(40.0f, fov_iw - 0.38f);
      }
      if (++t >= 60) {   // fin de la animacion 0x1A (60 cuadros)
        fase = 0x16;
        t = 0;
        dbz3::anim::Scatto(x0, dbz3::anim::kFin);
      }
    } else {
      fov_iw = std::max(35.0f, fov_iw - 0.38f);   // ponytail: fuera del scatto IW lo repone en otro sitio (sin leer)
      if (fase == 0x16) {
        // la frenada (0x1B, 22 cuadros) acaba tambien en anim_iw (si no, andar la rearranca); sin energia, el cansancio
        // viene despues de la frenada entera (IW State16_Update lo mira al acabar 0x1B; auditoria7/K)
        const bool acabada = (++t >= 22 && dbz3::anim::FaseScatto() == dbz3::anim::kApagado) || t >= 90;
        if (acabada && e <= 0) {
          e = 1.0f;
          fase = 7;
          t = 0;
          dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
          dbz3::anim::Cansado(x0, true);
          REXLOG_INFO("dbz3 iwmini: scatto: sin energia (cansado)");
        } else if (acabada) {
          if (t >= 90) dbz3::anim::Scatto(x0, dbz3::anim::kApagado);
          fase = 0;
        }
      }
    }
    g_energia = std::max(0.0f, e);
    FovIW(base, fov_iw);
  }
  g_fase = fase;
  // Andar libre de IW (auditoria6, decomp EU Char_GetNextActionCode 0x2A9440 y RotatePlayerToMoveDir 0x299D80): en el
  // campo IW no hay guardia ni pasos de combate. Sin scatto ni cansancio, con la entrada acabada y sin final, el
  // luchador corre con la carrera de IW al andar y queda de pie con su reposo al parar (el pivote ya lo orienta).
  // ponytail: IW anda (codigo 2, 1.8) con la palanca a menos de 0.9 y corre (3, 4.0) por encima; aqui siempre corre
  // (teclado = palanca entera); el andar lento cuando se identifique su animacion (auditoria6/4: sin candidata firme).
  g_libre = estado == 4 && g_colocado.load() && fase == 0 && !g_sin_mando.load() && g_final.load() == 0 &&
            !g_resbalando.load();
  if (!g_libre.load()) g_pide_salto = false;   // pulsado fuera del andar libre: no salta despues
  if (!g_libre.load() && g_cae) {   // la caida no sigue fuera del andar libre (scatto, final, fin del minijuego)
    g_cae = false;
    dbz3::suelo_iw::SueloFijo(0, false);
  }
  if (g_libre.load()) {
    // DBZ3_IW_CORRER=808 (hex): otra carrera de IW de las candidatas (0x807/0x808/0x815/0x816 = codigos 7/8/0x15/0x16)
    static const uint32_t correr = [] {
      const char* v = std::getenv("DBZ3_IW_CORRER");
      return v ? uint32_t(std::strtoul(v, nullptr, 16)) : dbz3::anim::kCorrer;
    }();
    static const uint32_t quieto = [] {   // DBZ3_IW_QUIETO=802: el otro reposo de IW (codigo 2)
      const char* v = std::getenv("DBZ3_IW_QUIETO");
      return v ? uint32_t(std::strtoul(v, nullptr, 16)) : dbz3::anim::kQuieto;
    }();
    // Caida al bajar de una cornisa: B3 pega el luchador al suelo de golpe (de 32 a 0 en un cuadro). Como IW
    // (Char_UpdateMovementPhysics, gravedad por cuadro), el suelo del juego baja poco a poco (suelo_iw::SueloFijo, el
    // del vuelo de R12) y suena la caida de IW (0x22) hasta llegar al de verdad. ponytail: gravedad 0.35*k por cuadro
    // a ojo (IW 1.0 en sus unidades, sin convertir)
    static float y_cae = 0, v_cae = 0, y_antes = 0;
    bool cae = g_cae;
    const float hs = d->campo ? dbz3::suelo_iw::Altura(p.x, p.z, -1e30f) : -1e30f;
    if (hs > -1e29f) {
      if (!cae && y_antes - hs > 6.0f * d->k) cae = true, y_cae = y_antes, v_cae = 0;
      // salto con la cruz (IW estados 0x0C-0x10, despegue 7.0 hacia arriba; ponytail: la gravedad de IW es 0.15 por
      // cuadro en sus unidades y aqui va sin la escala del campo, para que suba unas dos alturas del luchador)
      if (g_pide_salto.exchange(false) && !cae) cae = true, y_cae = hs, v_cae = -7.0f * d->k;
      if (cae) {
        v_cae += v_cae < 0 ? 0.15f : 0.35f * d->k;   // subiendo, la de IW; cayendo de una cornisa, la de antes
        y_cae -= v_cae;
        if (y_cae <= hs) {
          cae = false;
          dbz3::suelo_iw::SueloFijo(0, false);
        } else {
          dbz3::suelo_iw::SueloFijo(y_cae, true);
        }
      }
      y_antes = cae ? y_cae : hs;
    }
    g_cae = cae;
    static const uint32_t caer = [] {   // DBZ3_IW_CAER=<hex>: otra animacion para la caida (por defecto la 0x22 comun)
      const char* v = std::getenv("DBZ3_IW_CAER");
      return v ? uint32_t(std::strtoul(v, nullptr, 16)) : dbz3::anim::kCaer;
    }();
    // en el aire: el despegue de IW (0x29) quieto en su cuadro 6 (las rodillas recogidas; en el 12 ya va en picado; despues se lanza en picado,
    // es el arranque de un vuelo) hasta tocar suelo; las
    // poses de aire de IW (0x22, 0x104, 0x105) van inclinadas hacia delante (saltar avanzando) y quedaban raras
    static bool en_aire = false;
    if (cae) {
      if (!en_aire) dbz3::anim::Reproducir(x0, std::getenv("DBZ3_IW_CAER") ? caer : dbz3::anim::kSubir, true, 6);
      en_aire = true;
    } else {
      en_aire = false;
      dbz3::anim::Bucle(x0, !g_anda.load() ? quieto : g_corre.load() ? correr : dbz3::anim::kAndar);
    }
  }
  // Caida (IW Char_UpdateMovementPhysics EU 0x2A8E20: sin suelo debajo, o a >= 500 de el, +0x1CC = 1 y fundido): en el
  // campo de IW, sin suelo en (x, z) vuelve al ultimo sitio con suelo (Camino de la Serpiente). ponytail: sin el fundido
  // y sin leer donde reaparece IW (aqui, el ultimo sitio con suelo).
  static V3 con_suelo;
  if (estado == 3 || !g_colocado.load()) con_suelo = d->salida;
  if (d->campo && estado == 4 && g_colocado.load()) {
    constexpr float kSin = -1.0e9f;
    if (dbz3::suelo_iw::Altura(p.x, p.z, kSin) == kSin) {
      REXLOG_INFO("dbz3 iwmini: caida en ({:.0f}, {:.0f}): vuelve a ({:.0f}, {:.0f})", p.x, p.z, con_suelo.x, con_suelo.z);
      p = con_suelo;
      Poner(base, x0, p);
    } else {
      con_suelo = p;
    }
  }
  if (picc) {   // el jugador donde lo lleva la marcha de IW y mirando hacia ella (no hacia Piccolo)
    propio = {p.x, 0, p.z};
    Poner(base, x0, p);
    if (estado == 4) {
      const int32_t ang = int32_t(std::lround(std::atan2(dx, dz) / 6.2831853f * 65536.0f));
      REX_STORE_U16(x0 + kMiradaA, uint16_t(ang - 15759));
      REX_STORE_U16(x0 + kMiradaB, uint16_t(ang - 15968));
    }
    // Piccolo: donde lo pone la IA (MoverNpc), corriendo con el corredor de la IA de IW (anim::Corredor, R10: el ciclo
    // de carrera de Piccolo de DATA_CMN 925) desde que empieza la pelea; lo apaga el final (VigilarCombate)
    static bool corre = false;
    if (estado == 3) corre = false;
    if (estado == 4 && g_colocado.load() && !g_reloj_parado.load() && !corre) corre = true, dbz3::anim::Corredor(x1, true);
    Poner(base, x1, {g_npc_x.load(), 0, g_npc_z.load()});
    StoreF(base, x0 + kVida, LoadF(base, x0 + kVidaMax));
  } else {
    Poner(base, x1, {p.x + dx * kPivote, 0, p.z + dz * kPivote});
    REX_STORE_U8(x1 + kOcultoA, 1);   // sin cuerpo A ni B (sub_82109D90; tagteam.cpp Veil)
    REX_STORE_U8(x1 + kOcultoB, 1);
  }
  StoreF(base, x1 + kVida, LoadF(base, x1 + kVidaMax));   // invulnerable: el combate no acaba por KO
  // camara: como IW (auditoria7/H) no la arrastra la marcha (eso, con la marcha relativa a la camara, la hacia girar sin
  // fin con la palanca de lado y temblar hacia atras); la giran la palanca izquierda de lado (Mando) y la derecha
  const float yaw = g_yaw.load();
  // R16: en la entrada la camara da media vuelta alrededor del jugador, de cara a el hasta detras (ponytail: las camaras
  // animadas de IW, Camera_PlayCameraAnim 0x296..., son datos sin leer)
  const float orbita = g_final.load() == 0 ? 3.14159265f * float(g_cartel.load()) / float(kCartelCuadros) : 0.0f;
  const float fx = std::sin(yaw + orbita), fz = std::cos(yaw + orbita);
  static V3 seguido;
  // un salto de sitio (la prueba auto, un reinicio) no se sigue: la camara cruzaria colinas y paredes por el camino
  if (estado == 3 || !g_colocado.load() || DistH(seguido, p) > 150.0f) seguido = p;
  // con el scatto (35 u por cuadro) se quedaba atras hasta pasar de 150 y saltaba de golpe (cruzando lomas, el ojo
  // dentro del terreno, rafaga 204507): mas alla de 40 u alcanza mas deprisa; corriendo (~33 u de retraso), igual
  const float lejos = DistH(seguido, p);
  const float kseg = std::min(1.0f, (d->indice == 2 ? 0.3f : 0.12f) + std::max(0.0f, lejos - 40.0f) / 60.0f);
  seguido = {seguido.x + (p.x - seguido.x) * kseg, seguido.y + (p.y - seguido.y) * kseg,
             seguido.z + (p.z - seguido.z) * kseg};
  // entrada (cartel): empieza en primer plano, cerca y desde abajo (referencia del usuario 4_goku_pose_entrada: Goku
  // llena el cuadro) y se aleja hasta la de seguir mientras da la media vuelta (auditoria10/C)
  float atras = kCamAtras, alto = kCamAlto, mira = kCamMiraAlto;
  if (orbita > 0.0f) {
    const float s0 = 1.0f - float(g_cartel.load()) / float(kCartelCuadros), s = s0 * s0 * (3.0f - 2.0f * s0);
    atras = 11.0f + (kCamAtras - 11.0f) * s;
    alto = 4.0f + (kCamAlto - 4.0f) * s;
    mira = 7.0f + (kCamMiraAlto - 7.0f) * s;
  }
  float ojo[3] = {seguido.x - fx * atras, seguido.y + alto, seguido.z - fz * atras};
  float obj[3] = {seguido.x, seguido.y + mira, seguido.z};
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
  // En el campo de IW, como Camera_UpdateFollow: rayo de T (el punto mirado) hacia el ojo contra la colision del #WCF
  // (mascara 1, la de la camara; R11) y, si corta, el ojo en el corte. IW no tiene distancia minima (a menos de 10 pinta
  // al jugador con alfa 0.1; ponytail: ese fundido y el barrido de esfera que sigue al rayo, de radio sin leer, no
  // estan; en su lugar el ojo 3 u antes del corte, para que el plano cercano no entre en la roca).
  // cuesta arriba detras del jugador: la camara sube por encima de la loma (a su altura sobre el suelo de debajo) en
  // vez de meterse en ella o acercarse al jugador por el rayo
  if (const float h = d->campo && !cerca ? dbz3::suelo_iw::Altura(ojo[0], ojo[2], -1e30f) : -1e30f; ojo[1] < h + kCamAlto)
    ojo[1] = h + kCamAlto;
  const float cab[3] = {obj[0], obj[1], obj[2]};
  // tapado (al bajar de una cornisa la loma queda entre el ojo y el jugador): primero mas alto, mirando desde encima de
  // la loma (hasta 120 u mas); solo si tampoco se ve, el ojo 3 u antes del corte (como IW, sin entrar en la roca)
  float corte[3];
  bool tapado = d->campo && !cerca && dbz3::suelo_iw::Rayo(cab, ojo, 1 | 2, corte);   // 2: paredes (acantilados sin la 1)
  for (float sube = 20.0f; tapado && sube <= 120.0f; sube += 20.0f) {
    const float alto[3] = {ojo[0], ojo[1] + sube, ojo[2]};
    if (float c2[3]; !dbz3::suelo_iw::Rayo(cab, alto, 1 | 2, c2)) {
      ojo[1] = alto[1];
      tapado = false;
    }
  }
  if (tapado) {
    const float vx = corte[0] - cab[0], vy = corte[1] - cab[1], vz = corte[2] - cab[2];
    const float l = std::sqrt(vx * vx + vy * vy + vz * vz), k = std::max(l - 3.0f, 0.5f * l) / std::max(l, 1e-3f);
    for (int i = 0; i < 3; ++i) ojo[i] = cab[i] + (corte[i] - cab[i]) * k;
  }
  dbz3::cine::Camara(base, ojo, obj);
  {   // registro de la accion (X+3244) y el ataque/animacion (X+2928 & 0xFFF) del jugador al cambiar: andar, correr...
    static uint32_t visto = ~0u;
    const uint32_t ac = REX_LOAD_U16(x0 + 3244), an = REX_LOAD_U32(x0 + 2928) & 0xFFF;
    if (estado == 4 && (ac << 16 | an) != visto) {
      visto = ac << 16 | an;
      REXLOG_INFO("dbz3 iwmini: accion {} codigo {:03X} en ({:.1f}, {:.1f}) anda {}", ac, an, p.x, p.z, g_anda.load());
    }
  }
  static int64_t traza = 0;
  if (estado == 4 && NowMs() - traza > 2000) {
    traza = NowMs();
    REXLOG_INFO("dbz3 iwmini: jugador ({:.1f}, {:.1f}, {:.1f}) marcha ({:.2f}, {:.2f}) anda {} ojo ({:.1f}, {:.1f}, {:.1f})",
                p.x, p.y, p.z, dx, dz, g_anda.load(), ojo[0], ojo[1], ojo[2]);
  }
}

// Hilo del juego, al leer el mando (combate.cpp sub_8208FD58): la palanca relativa a la camara elige la marcha y el
// juego recibe "hacia el rival" (el pivote ya esta en esa direccion). El juego mantiene las direcciones (+108) de un
// cuadro a otro (las suelta al ver soltar la tecla): antes de leer se le devuelve el registro como lo dejo el,
// sin lo inyectado, y despues se guarda ese y se inyecta.
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
  const uint32_t x0 = mgr + kX0;
  g = {true,
       REX_LOAD_U16(r + 108),
       REX_LOAD_U16(r + 110),
       REX_LOAD_U8(r + 26),
       REX_LOAD_U8(r + 27),
       REX_LOAD_U16(r + 12),
       REX_LOAD_U16(r + 14),
       REX_LOAD_U32(r + 92)};
  // circulo (B del mando de Xbox): el scatto de IW (Minigame_ReadPlayerButtons: recien pulsado 0x2000). El luchador
  // de B3 no lo ve (seria su rafaga de ki).
  // La cruceta tampoco: en la HD lanza las tecnicas (la flecha derecha del teclado sacaba un Kamehameha, accion 15,
  // codigo 0x253/0x24A, con +108 = 0x2000); para andar ya va en +108 (la cruceta << 8).
  constexpr uint16_t kQuitar = kBotonScatto | kBotonSalto | 0x00F0;   // circulo, cruz y cruceta (0x10 arriba .. 0x80 izq.)
  if ((g.b14 & kBotonScatto) && !g_sin_mando.load()) g_pide_scatto = true;
  if ((g.b14 & kBotonSalto) && !g_sin_mando.load()) g_pide_salto = true;
  if ((g.b12 | g.b14) & kQuitar) {
    REX_STORE_U16(r + 12, uint16_t(g.b12 & ~kQuitar));
    REX_STORE_U16(r + 14, uint16_t(g.b14 & ~kQuitar));
    if ((g.b12 | g.b14) & kBotonScatto) REX_STORE_U32(r + 92, uint32_t(-1));
  }
  // palanca derecha (XInput, la toma netplay_hooks.cpp antes que el juego; el raton con mouse look; las flechas en el
  // teclado de pruebas): gira la camara alrededor del jugador, para ver a Piccolo detras (Camera_UpdateStickOrbit EU
  // 0x297330: zona muerta 0x20, (v - 0x20) / 95 x 0.04 por cuadro hacia ese lado)
  if (const int rx = g_rx.load() / 256; std::abs(rx) > 0x20)
    g_yaw = g_yaw.load() - float(rx - (rx > 0 ? 0x20 : -0x20)) / 95.0f * 0.04f;
  static uint16_t botones = 0;
  if (g.b12 != botones) {
    botones = g.b12;
    if (botones) REXLOG_INFO("dbz3 iwmini: mando: botones 0x{:04X}", botones);
  }
  // +108 (medido con el teclado de pruebas): palanca 0x10 arriba, 0x20 derecha, 0x40 abajo, 0x80 izquierda; cruceta
  // lo mismo << 8 (Up = 0x1000, Right = 0x2000); +26/+27 = palanca izquierda x/y (s8, arriba negativo)
  const uint16_t dd = uint16_t(g.d108 | (g.d108 >> 8));
  float sx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
  float sy = float((dd & kArriba) != 0) - float((dd & kAbajo) != 0);
  if (sx == 0 && sy == 0) {
    sx = float(int8_t(g.p26)) / 127.0f;
    sy = -float(int8_t(g.p27)) / 127.0f;
  }
  static uint32_t visto = ~0u;
  const uint32_t clave = uint32_t(g.d108) | uint32_t(g.p26) << 16 | uint32_t(g.p27) << 24;
  if (clave != visto) {
    visto = clave;
    REXLOG_INFO("dbz3 iwmini: mando: +108 0x{:04X}, palanca {} {}", g.d108, int8_t(g.p26), int8_t(g.p27));
  }
  const bool antes = g_anda.load();
  const bool palanca = std::hypot(sx, sy) >= 0.35f;
  g_corre = std::fabs(sx) > 0.9f || std::fabs(sy) > 0.9f;
  const float yaw = g_yaw.load(), fx = std::sin(yaw), fz = std::cos(yaw);
  const float rx = -fz, rz = fx;   // derecha de la camara (cine::Camara: d = f x arriba)
  const int fase = g_fase.load();
  // la palanca de lado gira la camara 0.008 por cuadro hacia ese lado (IW Minigame_ReadPlayerStick EU 0x299250: lerp
  // fijo de su delante hacia su derecha, sin mirar la magnitud); derecha = yaw menor (d/dyaw delante = -derecha)
  if (palanca && fase == 0 && !g_sin_mando.load() && std::fabs(sx) > 0.01f)
    g_yaw = yaw - (sx > 0 ? 0.008f : -0.008f);
  if (palanca && (fase == 0 || fase == 0x15)) {   // en el arranque y la frenada IW no gira (solo en 0x15)
    const float wx = fx * sy + rx * sx, wz = fz * sy + rz * sx, l = std::max(std::hypot(wx, wz), 1e-3f);
    if (fase == 0x15) {   // en el sprint IW gira hacia la palanca poco a poco (DashMove EU 0x2A0A30 ->
      // RotatePlayerToMoveDir; ponytail: tasa sin leer, 0.08 rad por cuadro a ojo)
      const float actual = std::atan2(g_dir_x.load(), g_dir_z.load());
      float dif = std::atan2(wx, wz) - actual;
      while (dif > 3.14159265f) dif -= 6.2831853f;
      while (dif < -3.14159265f) dif += 6.2831853f;
      const float nuevo = actual + std::clamp(dif, -0.08f, 0.08f);
      g_dir_x = std::sin(nuevo);
      g_dir_z = std::cos(nuevo);
    } else {
      g_dir_x = wx / l;
      g_dir_z = wz / l;
    }
  } else if (fase == 0x15) {   // scatto sin palanca: hacia delante de la camara (Minigame_MovePlayerByStick)
    g_dir_x = fx;
    g_dir_z = fz;
  }
  // el scatto lo mueve Fotograma (y su animacion anim_iw): el luchador de B3 no anda en ninguna de sus fases
  if (!palanca || fase != 0 || g_resbalando.load() || g_sin_mando.load()) {
    g_anda = fase == 0x15;
    REX_STORE_U16(r + 108, 0);
    REX_STORE_U16(r + 110, 0);
    REX_STORE_U8(r + 26, 0);
    REX_STORE_U8(r + 27, 0);
    return;
  }
  g_anda = true;
  const bool derecha = LoadF(base, x0 + kRivalScreenX) >= LoadF(base, x0 + kScreenX);
  const uint16_t hacia = derecha ? kDerecha : kIzquierda;
  // La palanca anda, como en IW (frente R10: andar de IW = andar de B3, accion 2, codigo 3/4); correr es el scatto.
  REX_STORE_U16(r + 108, hacia);
  REX_STORE_U16(r + 110, antes ? uint16_t(0) : hacia);
  REX_STORE_U8(r + 26, uint8_t(derecha ? 0x7F : 0x81));
  REX_STORE_U8(r + 27, 0);
}

}  // namespace dbz3::iwmini

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
// se volvia al menu principal sin tabla. Aqui la misma pantalla de IW (fondo del mapa, filas de 334/1 t17 TIME, RANK,
// [BRICKS], SCORE, TOTAL SCORE con el ritmo de Results_StateBeginRoll/UpdateTally, EARNED! ... ZENI) sobre el menu
// principal, con el mando bloqueado para el menu; A la cierra (en la prueba automatica se cierra sola a los 12 s). No
// suma Zenie ni guarda record: eso es del mapa. Sonido de cada fila: el de PantallaDM (SonidoResultado) + el propio
// (SonidoFila, abajo).
std::mutex g_rf_mu;
std::shared_ptr<P::DragonMissionIW> g_rf_dm;
P::ResultadoDM g_rf_res;
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
    std::shared_ptr<P::DragonMissionIW> dm;
    P::ResultadoDM r;
    {
      std::lock_guard<std::mutex> lk(g_rf_mu);
      dm = g_rf_dm;
      r = g_rf_res;
    }
    if (!dm) return;
    uint16_t b = 0;
    if (in) {
      rex::input::X_INPUT_STATE st = {};
      if (in->GetStateForUI(0, &st) == 0) b = uint16_t(st.gamepad.buttons);
    }
    const float t = float(ImGui::GetTime());
    if (t0_ < 0) t0_ = t, antes_ = b;   // lo que ya estaba pulsado (la A del minijuego) no cuenta
    uint16_t pulsado = uint16_t(b & ~antes_);
    antes_ = b;
    if (g_auto && t - t0_ > 12.0f) pulsado = 0x1000 | 0x0010;   // prueba: A dos veces (salta el conteo y cierra)
    P::InfoDM info;
    info.dm = dm.get();
    if (pantalla_.Resultado(ImGui::GetForegroundDrawList(), io, t, pulsado, info, r) ||
        (g_auto && t - t0_ > 12.5f)) {
      g_rf_ver = false;
      REXLOG_INFO("dbz3 iwmini: resultado fuera del mapa: cerrado");
    }
  }

 private:
  P::PantallaDM pantalla_;
  bool bloqueo_ = false;
  float t0_ = -1;
  uint16_t antes_ = 0;
};

void CrearPanelResFuera() {
  static PanelResFuera* panel = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new PanelResFuera(rt->imgui_drawer());
}

// Lo mismo que hace el mapa con el Resultado del minijuego (pasarela_misiones.cpp), sin premio ni record. Hilo propio:
// lee el Dragon Mission de la copia del jugador (paneles, cifras, fondo) la primera vez.
void MostrarResultadoFuera(const Resultado& mr) {
  static std::shared_ptr<P::DragonMissionIW> dm;
  if (!dm) {
    auto d = std::make_shared<P::DragonMissionIW>();
    std::string error;
    if (!P::LeerDragonMission(P::BuscarIW(), *d, error)) {
      REXLOG_WARN("dbz3 iwmini: resultado fuera del mapa: sin Dragon Mission ({})", error);
      return;
    }
    dm = d;
  }
  P::ResultadoDM r;
  r.segundos = mr.centesimas / 100;
  r.puntos = mr.puntos;
  r.rango = mr.rango;
  r.zeni = mr.puntos;   // el premio de IW (lo mismo que el mapa), solo ensenado
  if (mr.clave == 0x7D3) r.ladrillos = mr.monedas;
  r.clave_mini = mr.clave;   // s1 F04.3: titulo del minijuego arriba
  {
    std::lock_guard<std::mutex> lk(g_rf_mu);
    g_rf_dm = dm;
    g_rf_res = r;
  }
  g_rf_ver = true;
  dbz3::launcher::DeferUi(&CrearPanelResFuera);
  REXLOG_INFO("dbz3 iwmini: resultado fuera del mapa: {} s, {} puntos, rango {} (minijuego 0x{:X})", r.segundos, r.puntos,
              r.rango, mr.clave);
}

// PRUEBA: DBZ3_TEST_IWMINI=<clave>[,auto] (p. ej. 0x7D0,auto): empieza el minijuego cuando el menu principal lleva 3 s
// en pantalla; con "auto" el jugador salta solo de aro en aro y despues a la meta.
const bool g_test = [] {
  const char* t = std::getenv("DBZ3_TEST_IWMINI");
  if (!t) return false;
  const int clave = int(std::strtol(t, nullptr, 0));
  g_auto = std::strstr(t, "auto") != nullptr;
  g_meta_abierta = std::strstr(t, "meta") != nullptr;
  std::thread([clave] {
    for (int quieto = 0; quieto < 120;) {
      quieto = dbz3::ingame::GameScreen() == kMainScreen ? quieto + 1 : 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    REXLOG_INFO("dbz3 iwmini: prueba: {}", Empezar(clave, -1) ? "empieza" : "no empieza");
    for (int i = 0; i < 8000 && g_estado.load() != 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Resultado r;
    if (TomarResultado(r)) {
      REXLOG_INFO("dbz3 iwmini: prueba: resultado superado {} tiempo {} cs puntos {} rango {} cogidos {}", r.superado,
                  r.centesimas, r.puntos, r.rango, r.monedas);
      // s8 (punto 3): fuera del mapa, su tabla de IW al volver al menu principal
      for (int i = 0; i < 400 && dbz3::ingame::GameScreen() != kMainScreen; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      MostrarResultadoFuera(r);
    }
  }).detach();
  return true;
}();
}  // namespace

}  // namespace dbz3::iwmini
