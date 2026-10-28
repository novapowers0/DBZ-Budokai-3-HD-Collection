// dbz3 - Minijuegos de botones de Infinite World (frente R14, ronda 4, 2026-10-16). Ver docs/14_frentes/RONDA4_PULIDO.md
// ("R14"). Registrados en iw_minijuegos (RegistrarModulo): elegir su casilla del mapa de Dragon Mission los empieza.
//
// RE (ELF USA SLUS_218.42 con E:/DBZ3HD_trabajo/P4/iwdis.py; nombres del decomp EU iw_sles55347.c, codigo EU = USA +
// 0x330 en esta zona, datos EU = USA + 0x7B0). Todos usan la libreria QTE comun (EU 0x2EE6D0-0x2EF290: QTE_SetSequence
// copia N codigos de boton; codigo i = bit (1 << i) del mando de PS2, tabla 0x497E90 USA). Dos familias:
//  A) "Dos carriles" (EU QteMg_* 0x32E8D0-0x333080 = MSN_NB014; clones en NB013 0x3513D0 y NB006 0x376F00 USA):
//     - Por dificultad 0..5 dos secuencias de 50 (tabla S + d*0x70; carril 1 en +0x32): carril 0 = botones de la
//       derecha (12 triangulo, 13 circulo, 14 equis, 15 cuadrado), carril 1 = gatillos (8 L2, 9 R2, 10 L1, 11 R1).
//     - Separacion entre iconos en cuadros: S + 0x2A0 + d*0x190 + carril*200, 50 ints (QteMg_UpdatePlayerButtons:
//       al bajar a 0 sale el siguiente en x = -16 - velocidad y la cuenta vuelve a cargar la siguiente separacion).
//     - Los iconos van de izquierda a derecha a `velocidad` px por cuadro (pantalla de 640: carril 0 en y 366,
//       carril 1 en y 409): se aceptan pulsaciones con x > W/2 + 50, fallo si pasan de W/2 + 150, se quitan en 495.
//     - Vida 1000 los dos (QteMg_SetCharStatsByLevel). Acierto en el carril 0 = el rival pierde el ataque del jugador;
//       fallo en el carril 1 = el jugador pierde el ataque del rival (QteMg_ApplyHitDamage). Acaba al gastar las dos
//       secuencias o con una vida a 0.
//     - Nota (SkillEdit_ComputeStatPercent) = aciertos / iconos que llegaron a la zona x 100. Rango (QteMg_ClassifyResult
//       con los umbrales de s_MSN_NB0xx_Updata): 95-100 -> 5, 81-94 -> 4, 69-80 -> 3, 56-68 -> 2, 1-55 -> 1; con la vida
//       del jugador a 0 o nota 0 -> 0 (no superado). Puntos (QteMg_FinalizeResult) = tabla[rango] x multiplicador[d]
//       (0.6 0.8 1.0 1.3 1.6 2.0), redondeado a la centena (Math_RoundUp100).
//  B) "Rondas" (NB007 0x292BB0, NB012 0x3124F0 USA: secuencias por ronda tabla + ronda*10, longitud de +0xB8) y MSN019
//     (0x2EAA20-0x2EC6A0, sin QTE_SetSequence): RE en el documento, sin portar aun.
#include "reloj_prueba.h"
#include "escenarios.h"
#include "iw_minijuegos.h"
#include "iw_mini_extra.h"

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <random>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "generated/dbz3_init.h"
#include "anim_iw.h"
#include "cine.h"
#include "combate.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "launcher/launcher_state.h"
#include "pasarela.h"
#include "pasarela_iw.h"
#include "ps2_modelo.h"

namespace dbz3::hub {
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
}
namespace dbz3::net {
void InyectarPad(uint32_t usuario, int32_t botones);   // net/netplay_hooks.cpp (-1 = el mando de verdad)
}
namespace dbz3::anim {
// 06 (iw_eventos_y_mezcla): true si las animaciones de IW estan disparando sus eventos por
// cuadro (lineas AP del #BSK) y uno de sonido sono hace menos de 30 cuadros; entonces los
// retardos fijos de abajo no piden el suyo (para no doblarlo). Definida en anim_iw.cpp.
bool EventosSonandoReciente();
}  // namespace dbz3::anim
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3

namespace dbz3::iwmini {
namespace {

namespace P = dbz3::pasarela;
namespace S = dbz3::ingame::style;

constexpr dbz3::GA kMgrPtr = 0x823D6CF4, kPads = 0x8238B780;   // iw_minijuegos.cpp (P4)
std::atomic<bool> g_nave880{false};   // NB007 en el campo 880 (la nave por dentro)
std::atomic<float> g_nave_lado{1.0f};   // lado del pivote por minijuego, contrastado con la cámara original
static float NaveLado() {
  static const char* e = std::getenv("DBZ3_S9_NAVE_LADO");   // sonda temporal para contrastar la cara del personaje
  return e ? float(std::atof(e)) : g_nave_lado.load();
}
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kVida = 6080, kVidaMax = 6084, kFin = 35569;
// Registro del mando 0 (+12, botones mantenidos) con los bits del mando de PS2, medido con el teclado de pruebas: X del
// mando de Xbox -> 0x8000 cuadrado, Y -> 0x1000 triangulo, A -> 0x4000 equis, B -> 0x2000 circulo, LB -> 0x0400 L1,
// RB -> 0x0800 R1, LT -> 0x0100 L2, RT -> 0x0200 R2: los mismos bits que los codigos de boton de IW (1 << codigo).
constexpr uint32_t kPadBotones = 12;
// Prueba (auto): el boton de Xbox que da cada codigo de IW, para pulsarlo de verdad (net::InyectarPad). L2/R2 son
// gatillos (el mando virtual no los lleva): esos se dan por pulsados dentro del minijuego.
int32_t XboxDe(int boton) {
  switch (boton) {
    case 15: return 0x4000;   // X
    case 12: return 0x8000;   // Y
    case 14: return 0x1000;   // A
    case 13: return 0x2000;   // B
    case 10: return 0x0100;   // LB
    case 11: return 0x0200;   // RB
    default: return 0;
  }
}

// Altura a la que luchan NB013 y NB014: la cadera de sus poses de pelea esta a y = 38-41 (la guardia de B3, a 8): en el aire.
constexpr float kAire = 38;
constexpr float kAlto0 = -6;   // camara al empezar (NB013/NB014): los dos de pie en el suelo, antes de que el salto los suba

// ---- Poses de IW (R14, 2026-10-17) ----
// Los dos luchadores no usan las poses de B3 sino las de IW (anim_iw.cpp CodigoQte: cada uno trae las suyas en su #BSK de
// DATA_CMN). Todo del ELF: QteMg_PlayHitPoses (EU 0x330410; en NB013 Mg_SetClashStepPoses 0x352D40; en NB006
// Mg_SetRandomHitPose 0x378DD0) elige las poses en una tabla de 4 filas (USA 0x4804D0, 0x483000 y 0x485600, iguales: [+0]
// 4/7/10/13, [+4] 6/9/12/15, [+8] 5/8/11/14, [+0xC] 3/6/9/12, [+0x10] 5/8/11/14, [+0x14] 4/7/10/13) con un contador que
// da la vuelta de 0 a 3 (+0x2F4; NB006 lo saca al azar, Memory() & 3) y que sube con cada golpe:
//   carril 0 (el jugador pega): jugador [+0]; rival, si acierta [+0x10] (herido) y si no [+0x14] (esquiva);
//   carril 1 (el rival pega): rival [+0xC]; jugador, si acierta [+8] (esquiva) y si no [+4] (herido).
// Cada numero es un estado de pose (Char_SetPoseState / _P2: tablas USA 0x499820 y 0x499950 en NB014, 0x49A5B0 y 0x499F30
// en NB013, 0x49B370 y 0x49AE00 en NB006) que pone una animacion del #BSK; la tabla de abajo ya trae los codigos.
// Tambien: intro (pose 0), READY (jugador 2 / rival 1), START (jugador 3 / rival 2: la espera de pelea, en bucle),
// resultado (jugador 0x12 / rival 0x11 si gana, 0x13 / 0x12 si no) y final del jugador (0x14 / 0x15). Scripts de lectura
// en E:\DBZ3HD_trabajo\R14\poses.
// Resuelto 2026-10-18 (studies 4, frente 04), contra el ELF USA (SLUS_218.42) y el decomp EU: la tabla de estados de
// Mg_SetRandomHitPose es EU 0x485DB0 = USA 0x485600, 4 filas x 6 columnas [j_ataca, j_herido, j_esquiva, r_ataca,
// r_herido, r_esquiva]; el jugador va por Char_DispatchPoseTable (EU 0x3763E0, tabla EU 0x49B5B0 = USA 0x49AE00:
// estado 6 -> 0x277) y el rival por Char_SetPoseStateDispatch (EU 0x380720, tabla EU 0x49BB20 = USA 0x49B370). El
// original SI pide 0x277 en NB006: Mg_ProcessQteHit (EU 0x379500, L467727) con el carril 1 fallado llama a
// Mg_SetRandomHitPose(1, 0, -1) -> jugador = columna 1 de la fila k (estados 6/9/12/15) -> 0x277/0x27A/0x27D/0x280.
// Los cuatro traen evento tipo 1 valor 70 en su cuadro 7-9 (impacto) y tipo 3 (voz): el caso 0x277 de la prueba 031237.
struct Poses {
  int cmn[2];                           // DATA_CMN del jugador y del rival
  uint16_t ini[2], listo[2], pelea[2];  // pose 0, READY, START
  uint16_t j_ataca[4], j_esquiva[4], j_herido[4];
  uint16_t r_ataca[4], r_herido[4], r_esquiva[4];
  uint16_t gana[2], pierde[2];          // [jugador, rival]
  uint16_t fin_bien, fin_mal;           // el jugador al acabar el resultado
  bool azar;                            // NB006: el contador sale al azar
  // aire/dist/sep: altura de la cadera, distancia de la camara y separacion de los dos. IW no los mueve: los pone en el
  // ORIGEN (Mg_InitCharactersForRound EU 0x356D80 escribe +0x40/+0x44/+0x48 = 0 en los dos) y no aplica la raiz de la
  // pose (el modo +0x18E8 solo lo escriben los estados de combate -Dash/Slide-, nunca los minijuegos), asi que la
  // separacion sale de la cadera que trae la propia pose; los valores de abajo son los de sus raices en la copia.
  float aire, dist, sep;
  // Camaras de IW (clips del #AMC de DATA_CMN 915, ver "Camaras de IW"; -1 = la propia). READY (dur_intro cuadros, los del
  // clip), juego (plano fijo), resultado (gana / pierde) y final del jugador (rango > 0 / 0). ancla = {x, z del centro del par
  // y eje jugador -> rival} en el mundo de IW (de las raices de sus poses: docs/14_frentes/iw_camaras_minijuegos.md).
  int cam_ini, cam_juego, cam_gana, cam_pierde, cam_fin_bien, cam_fin_mal, dur_intro;
  float ancla[5];   // + separacion de los objetos con la camara de IW (la del par de IW: 11-18 u entre las caderas)
  // 05_iw_minijuegos_pendientes: choque de NB013. En IW Mg_StartClashSequence pone la pose 0x11 (jugador) / 0x21 (rival) y
  // el clip 0x188 y Mg_SetClashResultPoses los 0x18A/0x18C; aqui, la pose del choque de cada uno y su camara (-1 sin choque).
  uint16_t choque[2];
  int cam_choque;
  // studies 8 (B), medido en PCSX2 (IW SLES-55347, medir_mini_iw.py; studies 8\ENTREGAS\B_minijuegos_iw\INFORME.md):
  //  - entrada: el #AMC propio de la escena (DATA_CMN amc_entrada, hijo de nadie: la entrada ES el #AMC; ranura 1 de
  //    la camara, UiAnim_Start(0, 1, 0, ...) de Mg_Init) con n_entrada clips seguidos; -1 = sin medir
  //  - presentacion: el clip cam_presenta (dur_presenta cuadros) con el jugador en su pose 1 (= la animacion de la 0)
  //  - giro (+0x1834) de cada luchador, fijo todo el minijuego: jugador 0x4000 (mira a +X), rival 0xC002 (a -X); los
  //    dos objetos en (0,0,0) (+0x40) y la raiz de las poses sin aplicar (+0x18E8 = 0): la separacion es la del hueso
  //    raiz de cada pose (cadera del jugador x -4..-9, del rival +6..+8; altura de pelea y 42-44)
  int amc_entrada, n_entrada, cam_presenta, dur_presenta;
  uint16_t yaw[2];
};
// R7 (05_iw_minijuegos_pendientes): pausas (cuadros) del clip 0x98 de NB006 al ganar un punto y del choque de NB013.
constexpr int kPausaPunto = 30, kPausaChoque = 40;
// R7 (05_iw_minijuegos_pendientes): la camara de juego de MSN019 es el #AMC propio de DATA_CMN 879 (clip 0, 300 cuadros;
// w_Camera_LoadMotionFile carga el motion 0x36F y Minigame_SetupPlayerModels monta la escena). Id reservado en g_clips
// para no chocar con los indices de clips de 915 (1456).
constexpr int kCam879 = 0x8000;
constexpr Poses kPosesNB014 = {{922, 920},
                               {0x5C8, 0x5CD}, {0x578, 0x579}, {0x57A, 0x57B},
                               {0x582, 0x585, 0x588, 0x58B}, {0x598, 0x59B, 0x59E, 0x5A1}, {0x597, 0x59A, 0x59D, 0x5A0},
                               {0x596, 0x599, 0x59C, 0x59F}, {0x583, 0x586, 0x589, 0x58C}, {0x584, 0x587, 0x58A, 0x58D},
                               {0x5BE, 0x5BF}, {0x5C0, 0x5C1}, 0x5C9, 0x5CA, false, kAire, 62, 26,
                               0x1A4, 0x1A5, 0x1AE, 0x1AF, 0x4B1, 0x4B2, 230, {1, 0, 1, 0, 17}, {0, 0}, -1,   // QteMg_* (0x32E8D0-0x333080 EU)
                               877, 1, 0x4B0, 300, {0x4000, 0xC002}};   // s8: entrada 877 (300 c), 0x4B0 (300 c)
constexpr Poses kPosesNB013 = {{931, 928},
                               {0x564, 0x569}, {0x578, 0x579}, {0x57A, 0x57B},
                               {0x582, 0x585, 0x588, 0x58B}, {0x598, 0x59B, 0x59E, 0x5A1}, {0x597, 0x59A, 0x59D, 0x5A0},
                               {0x596, 0x599, 0x59C, 0x59F}, {0x583, 0x586, 0x589, 0x58C}, {0x584, 0x587, 0x58A, 0x58D},
                               {0x5BE, 0x5BF}, {0x5C0, 0x5C1}, 0x565, 0x566, false, kAire, 62, 26,
                               0x186, 0x187, 0x190, 0x191, 0x47F, 0x480, 230, {1, 0, 1, 0, 17},
                               {0x543, 0x552}, 0x188,   // Mg_UpdateIntroPhase / Mg_StartRoundSequence / Mg_StartClashSequence
                               876, 1, 0x47E, 360, {0x4000, 0xC002}};   // s8: entrada 876 (300 c), 0x47E (360 c)
constexpr Poses kPosesNB006 = {{921, 925},
                               {0x2AD, 0x2A8}, {0x258, 0x259}, {0x258, 0x259},
                               {0x262, 0x265, 0x268, 0x26B}, {0x278, 0x27B, 0x27E, 0x281}, {0x277, 0x27A, 0x27D, 0x280},
                               {0x276, 0x279, 0x27C, 0x27F}, {0x263, 0x266, 0x269, 0x26C}, {0x264, 0x267, 0x26A, 0x26D},
                               {0x289, 0x28A}, {0x287, 0x288}, 0x2A9, 0x2AA, true, 14, 50, 15,   // NB006 pelea en el suelo (cadera a y = 4-10)
                               0x96, 0x97, 0x9A, 0x9A, 0x322, 0x323, 100, {-15, -0.5f, 0, 1, 7}, {0, 0}, -1,   // Mg_UpdateIntroSequence / EnterResultPhase
                               871, 2, 0x321, 300, {0x4000, 0xC002}};  // s8: entrada 871 (2 clips, 630 cuadros; L02)
// R7 quinta tanda (NB006): el resultado es 0x9A gane o pierda (Mg_EnterResultPhase EU L467302/467309) y el final del
// jugador 0x322 con rango > 0 / 0x323 con rango 0 (Mg_StartResultAnim L466793-466901, por iRam0049cebc + 0x1C).
// 05_iw_minijuegos_pendientes: 0x98 = el clip del punto ganado (Mg_ResetRoundState L467467: jugador con la pose 0x10);
// en Paso, al acertar en el carril 0, se reproduce el clip y hay una pausa (kPausaPunto) entre puntos.
// (el final del rango 0 de NB006 es 0x2AA para los dos y el del resto 0x2A9: Mg_StartResultAnim; aqui el jugador)


// Familia A por indice del #DMD (constantes del codigo de cada modulo, ELF USA; tablas leidas de la copia del jugador).
struct Modulo2 {
  int indice, clave;
  const char* nombre;
  uint32_t seq, puntos;          // S (secuencias) y tabla de puntos por rango (+0x20: multiplicadores)
  float velocidad[6];            // +0x35C/+0x360 (QteMg_SetSpeedByLevel)
  int ataque_jugador[6], ataque_rival[6];   // +0x228C / +0x435C (QteMg_SetCharStatsByLevel)
  // Personajes (ID de IW = ID de B3 hasta el 43) y formas del bloque de partida que rellena el modulo al empezar
  // (Mg_AllocContexts: +0x10/+0x12 personajes, +0x14/+0x16 formas) y escenario de IW de su w_Stage_BeginLoad
  // (0 BUDO, 1 MIND & TIME, 4 ROCK; pasarela::MapaIW). NB014 no escribe +0x10: queda el del mapa (Goku).
  int jugador, rival, forma_jugador, forma_rival, escenario_iw;
  const Poses* poses;   // R14: poses de IW de sus dos luchadores
};
constexpr Modulo2 kModulos[] = {
    {5, 0x7D4, "MSN_NB006", 0x484720, 0x485670, {2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 4.7f}, {15, 15, 10, 10, 5, 5},
     {35, 45, 55, 65, 75, 85}, 2, 11, 0, 0, 4, &kPosesNB006},
    // EU 0x3581A0 s_MSN_NB013_Updata
    {12, 0x7DB, "MSN_NB013", 0x483060, 0x483F40, {2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 4.7f}, {20, 20, 15, 15, 10, 10},
     {25, 35, 45, 55, 65, 75}, 7, 8, 1, 1, 1, &kPosesNB013},   // s8: Trunks SSJ (captura del original, iw_nb013_c)
    // s8: NB014 en el original (capturas nb014_020/036): Goku SSJ (forma 2 de B3) y Gohan adolescente SSJ (forma 1),
    // pasarela.cpp kFormas (medido). El bloque de IW dice formas 0/0: su forma 0 de estos dos ya es la SSJ.
    // EU 0x333E40 s_MSN_NB014_Updata
    {13, 0x7DC, "MSN_NB014", 0x480530, 0x481170, {1.7f, 2.1f, 2.6f, 3.2f, 3.8f, 4.4f}, {50, 35, 25, 20, 15, 10},
     {15, 25, 35, 45, 55, 65}, 0, 3, 2, 1, 1, &kPosesNB014},
};
// EU 0x37CFC0 s_MSN_NB006_Updata (entrada de NB006): ponen la tabla de rangos, llaman a Mg_SetLevelScaleValue /
// Mg_SetLevelTimerValue / Mg_InitParamTable / Mg_SetupRoundParams / Mg_CountPositiveCells / Mg_CountNegativeCells y
// GameState_SetFlag2(0). Sus constantes son las de kModulos (velocidad, ataques) y las de arriba: `coincide`.
// EU 0x333E40 s_MSN_NB014_Updata y 0x3581A0 s_MSN_NB013_Updata escriben justo esta tabla en el contexto del minijuego:
// +0x310 = 0x5f (95), +0x314 = 100, +0x318 = 0x51 (81), +0x31c = 0x5e (94), +0x320 = 0x45 (69), +0x324 = 0x50 (80),
// +0x328 = 0x38 (56), +0x32c = 0x44 (68), +0x330 = 1, +0x334 = 0x37 (55) y +0x338 = 0. Los pares (min, max) son los de
// abajo: `coincide`.
// EU 0x350B60 Mg_InitDifficultyParams / 0x350C40 Mg_SetupRoundTargetsByLevel (NB013) y 0x376790 Mg_InitParamTable /
// 0x3768F0 Mg_SetupRoundParams (NB006) dejan los ataques por dificultad en +0x228c (jugador) y +0x435c (rival), y
// 0x350F70 Mg_SetupRoundSpeedByLevel / 0x376CE0 Mg_SetLevelScaleValue la velocidad en +0x35c/+0x360 (2.0, 2.5, 3.0,
// 3.5, 4.0, 4.7 = 0x40000000, 0x40200000, 0x40400000, 0x40600000, 0x40800000, 0x40966666); los valores de kModulos
// salen de ahi. +0x354/+0x358 (Mg_SetupRoundTimingByLevel 0x350EC0 y Mg_SetLevelTimerValue 0x376C30: 24, 30 y 80 en
// NB006) no se leen en este decompilado: [H].
// EU 0x32EA90 QteMg_ResetField354ByLevel (NB014/NB013): +0x354 y +0x358 a 0 en todas las dificultades; el port no
// usa esos campos (ver [H] de arriba).
// EU 0x377390 Mg_CountPositiveCells (NB006): por dificultad cuenta las celdas no negativas de las dos filas de 0x32
// entradas de la tabla de botones (una por carril) y las deja en +0x34/+0x38; es el numero de pulsaciones de cada
// carril, que aqui se cuenta al vuelo (nota = aciertos / iconos x 100): `coincide`.
// EU 0x3609F0 MinigameHideNSeekPiccolo_sCPU_PiccoloJumpAnim y 0x363D10 ..._sCPU: la CPU de Piccolo del pilla-pilla
// (estados de pose 0..0xf por palanca, Char_CheckGroundProximity y Char_EnterStateHitReaction2). En el port el rival de
// NB006 es el pivote invisible y no hay pilla-pilla: `motor` (lo cubre la IA/pivote de B3); [H] si se quiere el original.
// EU 0x333E40 s_MSN_NB014_Updata / 0x3581A0 s_MSN_NB013_Updata / 0x37CFC0 s_MSN_NB006_Updata
constexpr int kUmbral[6][2] = {{0, 0}, {1, 55}, {56, 68}, {69, 80}, {81, 94}, {95, 100}};   // +0x310..+0x338

// Pantalla de IW (640 x 448) y medidas del codigo
constexpr float kW = 640, kH = 448, kCarrilY[2] = {366, 409}, kIcono = 32;
constexpr float kAcepta = kW / 2 + 50, kFalla = kW / 2 + 150, kQuita = 495;
// El objetivo de los QTE de DOS carriles (familia A: NB006/NB013/NB014) son los objetos 1 (jugador) y 2 (rival) del
// modulo: Mg_SetupUiObjects EU 0x3575B0 -> UI_SetObjectPosVec(pantalla/2 + 150, 366 / 409, 0, 1 / 2) = x 470, con caja
// UI_SetObjectRect(-28, -28, 7, 28) (35 x 56). El fallo es pasarse de ese x (QteMg_UpdatePlayerButtons EU 0x332270:
// pantalla/2 + 150 < x -> QteMg_PlayHitPoses(...,0)), que es justo kFalla = 470; los carriles van en y = 43 x jugador +
// 366 (misma funcion), que es kCarrilY.
constexpr float kObjetivo = kW / 2 + 150;
constexpr float kAroX = 480;          // s8: centro del aro medido en la captura del original (600 de 800 -> 480 de 640)
constexpr float kAutoX = kAroX - 40;  // prueba (auto): pulsa con el icono casi en el aro (borde izquierdo en 440 < kFalla)

// EU 0x2EEE80 QTE_GetPressedButton: con la tabla 0x498690 del ELF EU ({1,2,4,...,0x8000}) el original devuelve el
// ULTIMO bit pulsado (el mas alto) y -1 si la mascara es 0. Es la misma resolucion que QTE_CheckPressedMatch compara
// con el boton esperado de la secuencia. `mascara` es el registro PS2 del mando (bits 0..15).
int BotonPulsado(uint16_t mascara) {
  for (int i = 15; i >= 0; --i)
    if (mascara & (1u << i)) return i;
  return -1;
}

struct Icono {
  int boton = 0;
  float x = 0;
  bool vivo = false, resuelto = false, contado = false, pedido = false;
};
struct Carril {
  uint8_t seq[50] = {};
  int hueco[50] = {};
  Icono ic[50];
  int n = 50;   // iconos del carril: hasta la primera separacion negativa (-256 en el carril 1 de NB006)
  int sale = 0, actual = 0, cuenta = 0, aciertos = 0, llegaron = 0;
};
struct Juego {
  const Modulo2* m = nullptr;
  int clave = 0, fila = 0, dif = 2;
  Carril c[2];
  // EU 0x32E8D0 QteMg_SetCharStatsByLevel
  float vel = 2, vida[2] = {1000, 1000};
  int fase = 0, t = 0, cuadros = 0, rango = 0, puntos = 100, nota = 0;   // fase 0 espera, 1 READY, 2 juego, 3 fin
  int zeni = 0, saldo = 0;   // premio ZENI y monedero tras sumarlo (QteMg_FinalizeResult); `nota` = puntuacion
  int aviso_t = 0;
  int pausa = 0;      // R7: cuadros de pausa (clip 0x98 de NB006 al punto ganado; choque de NB013)
  bool punto = false; // R7: NB006 -> al ganar un punto se reproduce el clip 0x98 (Mg_ResetRoundState) y hay pausa
  const Poses* poses = nullptr;   // R14
  int ent = 0;                    // s8: cuadros de la entrada de la escena (#AMC amc_entrada); 0 = sin ella
  int k = 0;                      // contador de las filas de poses (QteMg_PlayHitPoses: +0x2F4)
  bool aviso_bien = false, auto_ = false, fallos = false;   // prueba: fallos = deja pasar 1 de cada 5 del carril 1
  float pts[6] = {}, mult[6] = {};
  uint16_t antes = 0, pendiente = 0;
  int32_t inyecta = 0;   // prueba: botones de Xbox que se pulsan ahora
  int inyecta_t = 0;
  P::Imagen ready, start, hit, miss, clear, failed;   // DATA_USA 5/1 t08 t00 t07 t02 t09 t10
  P::Imagen objetivo;                                 // DATA_USA 0/1 t10: aro rojo y amarillo con la flecha
  P::Imagen aro, relleno, barra;                       // DATA_USA 6/0 t00, t01, t15 (HUD de P4)
  P::Imagen barra06;                                  // I03: DATA_USA 4/0 t42 (480x8): barra roja de NB006
  // s9: el HUD propio de los minijuegos de botones de IW (DATA_USA 4/0, cotejado con nb013_030 de PCSX2): banda azul
  // t38 (800x168), siluetas t39..t50, aro con engranaje t17, marco t18 y relleno verde t19 de la vida.
  P::Imagen banda, sil[2], aro4, marco4, verde4, caras;   // caras: t53, 3x3 de 64 (ver CaraIW)
  int cara[2] = {-1, -1};
  // s9: el panel de resultado del minijuego de IW (cotejado con iw_nb013_d de PCSX2): rotulos 5/7 t20-22 (OFFENCE /
  // DEFENSE SUCCESS RATE, TOTAL SCORE), cifras cian 5/5 t05 (5x4, % en la fila 2), cifras naranjas 5/5 t04 (11 de 32)
  // y EARNED! / TOTAL / ZENI de 333/0 (t19, t20, t07)
  P::Imagen rot[3], cif_c, cif_n, earned, total, zeni_img;
  P::Imagen titulo;   // s1 F04.3: titulo del minijuego (DATA_USA 335 + clave - 0x7D0, hijo 1)
  P::Imagen cif_e;    // s1 F04.3: 5/5 t06, cifras cian de 32 x 40 (EARNED en iw_nb013_d)
  // DS72: panel de resultado con el arte de interfaz de IW (DATA_USA 334/1): t00 relleno azul oscuro y t08 el
  // triangulo azul que marca cada fila (los tres rotulos ya eran 5/7). La banda clara de EARNED/TOTAL es 5/5 t03
  // (560x48), no una caja propia. El marco fino azul se queda como trazo medido [H]: 334/1 t03 es el bisel grueso
  // del panel del mapa y al estirarlo a 640x448 engorda ~6x.
  P::Imagen panel_fill, row_arrow, row_band;
  // DS156: fila RANK del panel de dos carriles. QteMg_FinalizeResult (EU 0x32F0F0) escribe la fila de rango (tipo 2,
  // +0x40: rank +0x48 y premio +0x4C) y el original la ensena entre TOTAL SCORE y EARNED. Rotulo y letra del mismo
  // #AMA que el panel del mapa (334/1 t17 fila 1 "RANK" y t24 "C B A S Z").
  P::Imagen row_labels, row_ranks;
};

int Azar();   // mas abajo
std::mutex g_mu;
std::shared_ptr<Juego> g_j;
std::atomic<bool> g_corre{false};
std::atomic<uint16_t> g_raw12{0};   // botones (+12) tal como los leyo el juego (MandoBotones)
std::atomic<bool> g_raw_ok{false};
std::atomic<float> g_cam_alto{14}, g_cam_dist{62};
std::atomic<bool> g_cam_reset{true};
std::atomic<int> g_fase_a{0};   // s9: fase del minijuego A (0 espera, 1 READY, 2 juego, 3 fin) para Colocar
std::atomic<bool> g_fondo_off{false};   // NB006: fondo del escenario apagado mientras dure el minijuego
std::atomic<float> g_cam_lado{1};   // 1: la camara a un lado; -1: al otro (la pose final, de frente)   // lo que pide el panel a la camara (PasoBotones la acerca poco a poco)

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

// ELF de IW del jugador (carpeta o ISO, USA o europeo). Europeo SLES_553.47 (medido con las dos copias, 2026-10-19): las
// tablas de los minijuegos van 0x7B0 mas adelante (las de NB007, 0x800). Los huecos de NB012 se leen tal cual: la tabla EU
// ya esta en cuadros (2026-10-22). Devuelve el puntero al dato de la direccion USA `a` (o nullptr).
struct ElfIW {
  std::vector<uint8_t> e;
  bool eu = false;
  const uint8_t* Off(uint32_t a) const {
    const size_t o = size_t(a) - 0x100000 + 0x80 + (eu ? (a < 0x47D000 ? 0x800 : 0x7B0) : 0);
    return o + 0x400 <= e.size() ? e.data() + o : nullptr;
  }
};
bool AbrirElfIW(ElfIW& x, std::string& error) {
  x.e = P::LeerElf(P::BuscarIW());
  x.eu = x.e.size() == 3786800;   // SLUS_218.42 = 3784752 B
  if (x.e.empty()) error = "sin el ejecutable de Infinite World (SLUS_218.42 o SLES_553.47)";
  return !x.e.empty();
}

// Tablas del ELF de IW del jugador (little endian; direccion = desplazamiento - 0x100000 + 0x80).
bool LeerElf(Juego& j, std::string& error) {
  ElfIW elf;
  if (!AbrirElfIW(elf, error)) return false;
  auto off = [&](uint32_t a) { return elf.Off(a); };
  const Modulo2& m = *j.m;
  for (int l = 0; l < 2; ++l) {
    const uint8_t* s = off(m.seq + uint32_t(j.dif) * 0x70 + uint32_t(l) * 0x32);
    const uint8_t* h = off(m.seq + 0x2A0 + uint32_t(j.dif) * 0x190 + uint32_t(l) * 200);
    if (!s || !h) {
      error = "ELF corto";
      return false;
    }
    for (int i = 0; i < 50; ++i) {
      // EU 0x2EEC60 QTE_SetSequence
      j.c[l].seq[i] = s[i];
      std::memcpy(&j.c[l].hueco[i], h + 4 * i, 4);
      const bool ok = l == 0 ? (s[i] >= 12 && s[i] <= 15) : (s[i] >= 8 && s[i] <= 11);
      // El negativo de la tabla de separaciones es el centinela del final del carril (relleno de la tabla): el
      // QteMg_UpdateButtonDisplay (0x32EDF0) recorre los 50 codigos (iVar7 < 0x32) y la ronda la cierra
      // QTE_IsSequenceDone, no el hueco. Aqui solo sirve para no pintar los huecos de cola.
      if (j.c[l].hueco[i] < 0 && i > 0 && j.c[l].n == 50) j.c[l].n = i;
      if (!ok || (j.c[l].hueco[i] < 0 && j.c[l].n == 50) || j.c[l].hueco[i] > 2000) {
        error = "las tablas del ELF no son las de IW USA";
        return false;
      }
    }
  }
  const uint8_t* p = off(m.puntos);
  if (!p) return false;
  for (int k = 0; k < 6; ++k) {
    int32_t v;
    std::memcpy(&v, p + 4 * k, 4);
    j.pts[k] = float(v);
    std::memcpy(&j.mult[k], p + 0x20 + 4 * k, 4);
  }
  return true;
}

// Carga (hilo del mapa): ELF y piezas de la UI de IW (DATA_USA de la copia del jugador).
bool Cargar(Juego& j, std::string& error) {
  if (!LeerElf(j, error)) return false;
  P::AfsPs2 usa;
  if (!usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) {
    error = "sin DATA_USA.AFS";
    return false;
  }
  j.objetivo = P::TexturaAmt(P::HijoAmb(usa.Entrada(0), 1), 10);
  const std::vector<uint8_t> t51 = P::HijoAmb(usa.Entrada(5), 1);
  j.start = P::TexturaAmt(t51, 0);   // "START!"
  j.miss = P::TexturaAmt(t51, 2);    // "MISS!"
  j.hit = P::TexturaAmt(t51, 7);     // "HIT!"
  j.ready = P::TexturaAmt(t51, 8);   // "READY?"
  j.clear = P::TexturaAmt(t51, 9);    // "CLEAR!"
  j.failed = P::TexturaAmt(t51, 10);  // "FAILED!"
  const std::vector<uint8_t> hud = P::HijoAmb(usa.Entrada(6), 0);
  if (P::CuantasAmt(hud) > 38) {
    j.aro = P::TexturaAmt(hud, 0);
    j.relleno = P::TexturaAmt(hud, 1);
    j.barra = P::TexturaAmt(hud, 15);
  }
  {
    const std::vector<uint8_t> e5 = usa.Entrada(5), r7 = P::HijoAmb(e5, 7), r5 = P::HijoAmb(e5, 5);
    if (P::CuantasAmt(r7) > 22 && P::CuantasAmt(r5) > 5) {
      for (int i = 0; i < 3; ++i) j.rot[i] = P::TexturaAmt(r7, 20 + i);
      j.cif_c = P::TexturaAmt(r5, 5);
      j.cif_n = P::TexturaAmt(r5, 4);
      if (P::CuantasAmt(r5) > 6) j.cif_e = P::TexturaAmt(r5, 6);   // s1 F04.3: cifras cian grandes de EARNED
      if (P::CuantasAmt(r5) > 3) j.row_band = P::TexturaAmt(r5, 3);   // DS72: banda clara 560x48 de EARNED/TOTAL
    }
    // DS72: el panel del resultado usa el arte de interfaz de IW (334/1 #AMT): t00 relleno y t08 triangulo.
    const std::vector<uint8_t> ui334 = P::HijoAmb(usa.Entrada(334), 1);
    if (P::CuantasAmt(ui334) > 8) {
      j.panel_fill = P::TexturaAmt(ui334, 0);
      j.row_arrow = P::TexturaAmt(ui334, 8);
    }
    // DS156: rotulo RANK (t17, 152x160: cinco filas de 32) y letras de rango (t24, 96x80: C B A / S Z -).
    if (P::CuantasAmt(ui334) > 24) {
      j.row_labels = P::TexturaAmt(ui334, 17);
      j.row_ranks = P::TexturaAmt(ui334, 24);
    }
    const std::vector<uint8_t> r333 = P::HijoAmb(usa.Entrada(333), 0);
    if (P::CuantasAmt(r333) > 20) {
      j.earned = P::TexturaAmt(r333, 19);
      j.total = P::TexturaAmt(r333, 20);
      j.zeni_img = P::TexturaAmt(r333, 7);
    }
    if (j.clave >= 0x7D0 && j.clave <= 0x7E1)   // s1 F04.3
      j.titulo = P::TexturaAmt(P::HijoAmb(usa.Entrada(size_t(P::kTituloMini0 + j.clave - 0x7D0)), 1), 0);
  }
  const std::vector<uint8_t> h4 = P::HijoAmb(usa.Entrada(4), 0);
  if (P::CuantasAmt(h4) > 50) {
    j.banda = P::TexturaAmt(h4, 38);
    j.aro4 = P::TexturaAmt(h4, 17);
    j.marco4 = P::TexturaAmt(h4, 18);
    j.verde4 = P::TexturaAmt(h4, 19);
    // Siluetas de cada par, del atlas DATA_USA 4/0 (h4): NB013 cotejado con la captura del original (Vegeta t43 a la
    // izquierda, Trunks t44 a la derecha). Las otras dos parejas se sacan del propio atlas: NB006 = t47 (Gohan nino,
    // 48x72, la figura mas baja) y t48 (Piccolo, 48x64, cabeza con el turbante), NB014 = t39 (Goku, 72x96, pelo en pico)
    // y t49 (Gohan adolescente, 72x96). t50 (80x88) no es de estos tres pares y no se pinta.
    const int s0 = j.m->poses == &kPosesNB013 ? 43 : j.m->poses == &kPosesNB006 ? 47 : 39;
    const int s1 = j.m->poses == &kPosesNB013 ? 44 : j.m->poses == &kPosesNB006 ? 48 : 49;
    j.sil[0] = P::TexturaAmt(h4, s0);
    j.sil[1] = P::TexturaAmt(h4, s1);
    // caras de t53 (3x3 de 64): 0 Goku SSJ, 1 Vegeta SSJ, 2 Gohan joven SSJ, 3 Trunks SSJ, 4 Piccolo, 5 Krilin,
    // 6 Gohan nino, 7 Goku (cotejado con nb013_030 / nb006_040: las de los aros de IW)
    j.caras = P::TexturaAmt(h4, 53);
    const bool b13 = j.m->poses == &kPosesNB013, b06 = j.m->poses == &kPosesNB006;
    j.cara[0] = b13 ? 1 : b06 ? 6 : 0;
    j.cara[1] = b13 ? 3 : b06 ? 4 : 2;
    if (b06) j.barra06 = P::TexturaAmt(h4, 0x2A);   // I03: barra roja de NB006 (Mg_LayoutHud, fila 0, y 366)
  }
  return true;
}

// Boton de B3 HD para cada codigo de IW (la HD cambio los del mando de PS2 por los de la 360): triangulo Y, circulo B,
// equis A, cuadrado X, L1 LB, R1 RB, L2 LT, R2 RT. Recortes de la tira LB LT RB RT Y B A X de data_usi 2062 (los de
// style::Glyph, la ayuda de botones de nuestros menus).
constexpr const char* kNombre[16] = {"", "", "", "", "", "", "", "", "LT", "RT", "LB", "RB", "Y", "B", "A", "X"};
ImVec4 Glifo(int boton) {
  switch (boton) {
    case 12: return {249, 2, 284, 46};   // Y
    case 13: return {297, 2, 332, 46};   // B
    case 14: return {346, 2, 381, 46};   // A
    case 15: return {393, 2, 428, 46};   // X
    case 10: return {4, 2, 56, 46};      // LB
    case 11: return {126, 2, 178, 46};   // RB
    case 8: return {74, 2, 108, 46};     // LT
    default: return {194, 2, 233, 46};   // 9 RT
  }
}

uint32_t AliasPose(const Poses& p, int rol, uint32_t codigo) {
  if (!codigo) return 0;
  const uint32_t cmn = codigo >= 0x57A && codigo <= 0x57D ? 915u : uint32_t(p.cmn[rol]);   // las de espera son de 915
  return dbz3::anim::CodigoQte(cmn, codigo);
}
// El luchador del rol (0 jugador, 1 rival) del combate en curso (0 si no hay).
uint32_t Luchador(int rol) {
  uint8_t* base = Base();
  const uint32_t mgr = base ? REX_LOAD_U32(kMgrPtr) : 0;
  return mgr ? mgr + kX0 + kXPaso * uint32_t(rol) : 0;
}
// Una pose una vez y luego la espera de pelea en bucle; `espera` = true: solo la espera (bucle).
void Poner(const Poses& p, int rol, uint32_t codigo, bool bucle, uint32_t luego = 0) {
  const uint32_t x = Luchador(rol), a = AliasPose(p, rol, codigo);
  if (!x || !a) return;
  if (bucle) dbz3::anim::Bucle(x, a);
  else dbz3::anim::Reproducir(x, a, false, 0, luego ? AliasPose(p, rol, luego) : 0);
}

// ---- Camaras de IW (familias A y B) ----
// UiAnim_Start(0, 0, clip, ...) del ELF no es una animacion de interfaz: reproduce un clip del #AMC de camara del minijuego
// (Camera_FinishMotionLoads: el hijo 2 del #AMB general de DATA_CMN 915, +0x7EC) con ojo / objetivo / giro / fov a 60 Hz
// (formato en docs/03_formatos/CAMARA_ACC.md), y el ultimo clip se queda en su ultimo cuadro. Los clips estan en el mundo de
// IW con el luchador en el origen mirando a +Z (MgPly: +0x40/+0x44/+0x48 = 0): aqui se suman a la posicion del luchador
// (con el rival-pivote delante, a +Z, para que mire igual) y el fov se escribe en mgr+0x1C4 (cine.cpp).
struct ClipCam {
  int nf = 0;
  std::vector<std::array<float, 4>> ojo, obj;   // {cuadro, x, y, z}
  std::vector<std::array<float, 2>> fov;        // {cuadro, radianes}
  std::vector<std::array<float, 2>> roll;       // {cuadro, radianes} (3a pista del clip; antes se ignoraba)
};
std::vector<uint8_t> g_amc;
std::map<int, ClipCam> g_clips;
std::atomic<int> g_cb_clip{-1}, g_cb_frame{0};   // clip en curso y cuadros desde que empezo (hilo de la interfaz -> del juego)
std::atomic<int> g_cb_amc{915}, g_cb_amc_entrada{0};   // AMC real de la camara (entrada, general 915 o MSN019 879)
std::atomic<bool> g_cb_comparable{false};        // clip de juego incluido en la serie del comparador, no entrada/READY/resultado
std::atomic<int> g_cb_ultimo_clip_log{-1};        // una muestra por ronda, como los dorados del original
std::atomic<bool> g_cb_quieto{false};
std::atomic<const Poses*> g_poses_a{nullptr};   // las poses (y camaras) del minijuego de la familia A en curso            // se queda en su primer cuadro (UiAnim_Start con play = 0)
std::atomic<bool> g_cb_bucle{false};   // s8: el clip vuelve a empezar al acabar (la camara de juego de IW: 900 cuadros en bucle)
// s8: origen comun de IW en el mundo de B3 (familia A): en IW los dos objetos estan en (0,0,0) y los clips de camara son
// coordenadas de ese mundo (medido: el ojo del clip 0x187 en el cuadro 27,6 = el ojo leido en PCSX2). Se toma el punto
// medio de los dos luchadores de B3 al empezar su combate (hilo del juego) y todo se suma a el sin girar.
std::atomic<bool> g_origen_ok{false};
float g_origen[3] = {0, 0, 0};
// Separacion de los objetos de B3 en el eje X: IW los tiene a 0, pero B3 necesita una distancia > 0 para girarlos el uno
// hacia el otro (y no dividir por 0 al empujarlos). 0,5 u a cada lado = 1 u en total (los cuerpos van a ~11 u por la raiz).
constexpr float kMedio = 0.5f;
constexpr uint32_t kMiradaA = 6062, kMiradaB = 6074;   // P4: X+6062 = angulo - 15759, X+6074 = angulo - 15968
constexpr int kCamEntrada = 0x9000;   // s8: id en g_clips del #AMC de entrada de la escena (DATA_CMN 876/877)
constexpr int kFinRes = 240;          // s8: el clip de resultado (0x191 / 0x1AF) dura 240 cuadros y despues el final (medido)

// Un #AMC de camara: lee los clips `idx` (indices dentro del #AMC) y los guarda como `base` + indice. Devuelve el nº de
// clips del #AMC (0 si no es un #AMC).
uint32_t LeerAmc(const std::vector<uint8_t>& amc, int base, const std::vector<int>& idx) {
  auto u32 = [&](size_t o) {
    uint32_t v = 0;
    if (o + 4 <= amc.size()) std::memcpy(&v, amc.data() + o, 4);
    return v;
  };
  auto f32 = [&](size_t o) {
    const uint32_t v = u32(o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  };
  if (amc.size() < 0x20 || std::memcmp(amc.data(), "#AMC", 4) != 0) return 0;
  const uint32_t n = u32(0x10), tab = u32(0x14);
  for (int id : idx) {
    if (id < 0 || uint32_t(id) >= n || g_clips.count(base + id)) continue;
    ClipCam c;
    c.nf = int(u32(tab + 16 * size_t(id) + 8));
    const uint32_t off = u32(tab + 16 * size_t(id) + 12);
    for (int k = 0; k < 4; ++k) {
      const uint32_t t = u32(off + 4 * size_t(k)), nk = u32(t + 8);
      if (nk == 0 || nk > 5000) continue;
      for (uint32_t i = 0; i < nk; ++i) {
        if (k < 2) {
          const size_t o = t + 12 + 16 * size_t(i);
          (k == 0 ? c.ojo : c.obj).push_back({float(u32(o)), f32(o + 4), f32(o + 8), f32(o + 12)});
        } else {
          const size_t o = t + 12 + 8 * size_t(i);
          (k == 2 ? c.roll : c.fov).push_back({float(u32(o)), f32(o + 4)});
        }
      }
    }
    if (c.nf > 0 && !c.ojo.empty() && !c.obj.empty()) g_clips[base + id] = std::move(c);
  }
  return n;
}
void PrecargarClips(const std::vector<int>& ids) {
  if (g_amc.empty()) {
    P::AfsPs2 cmn;
    if (!cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS")) return;
    g_amc = P::HijoAmb(cmn.Entrada(915), 2);
  }
  uint32_t n = 0;
  if (g_amc.size() >= 0x14) std::memcpy(&n, g_amc.data() + 0x10, 4);
  if (g_amc.size() < 0x20 || std::memcmp(g_amc.data(), "#AMC", 4) != 0) {
    g_amc.clear();
    return;
  }
  LeerAmc(g_amc, 0, ids);
  // 05_iw_minijuegos_pendientes: MSN019 lleva su propia camara (#AMC de DATA_CMN 879, clip 0, 300 cuadros), no la de 915.
  if (std::any_of(ids.begin(), ids.end(), [](int i) { return i >= kCam879; }) && !g_clips.count(kCam879)) {
    P::AfsPs2 cmn;
    if (cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS") && LeerAmc(cmn.Entrada(879), kCam879, {0})) {
      const auto it = g_clips.find(kCam879);
      REXLOG_INFO("dbz3 r7: camara de MSN019: #AMC de DATA_CMN 879 (clip 0, {} cuadros)",
                  it == g_clips.end() ? 0 : it->second.nf);
    }
  }
  REXLOG_INFO("dbz3 r14: camaras de IW: {} clips (AMC de DATA_CMN 915, {} clips en total)", g_clips.size(), n);
}
// s8: el #AMC de entrada de la escena (DATA_CMN `entrada`, sus `n` clips) en g_clips[kCamEntrada + i]. Devuelve los
// cuadros totales (0 si no esta).
int PrecargarEntrada(int entrada, int n) {
  if (entrada < 0 || n <= 0) return 0;
  static int leida = -1;
  if (leida != entrada) {
    for (int i = 0; i < 4; ++i) g_clips.erase(kCamEntrada + i);
    P::AfsPs2 cmn;
    if (!cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS")) return 0;
    std::vector<int> ids;
    for (int i = 0; i < n; ++i) ids.push_back(i);
    LeerAmc(cmn.Entrada(entrada), kCamEntrada, ids);
    leida = entrada;
  }
  int total = 0;
  for (int i = 0; i < n; ++i)
    if (const auto it = g_clips.find(kCamEntrada + i); it != g_clips.end()) total += it->second.nf;
  REXLOG_INFO("dbz3 s8: entrada de la escena: #AMC de DATA_CMN {} ({} clips, {} cuadros)", entrada, n, total);
  return total;
}
// Valor de una pista de claves {cuadro, v...} en el cuadro f (lineal, como el juego).
template <size_t N>
void Muestra(const std::vector<std::array<float, N>>& k, float f, float* out) {
  size_t i = 0;
  while (i + 1 < k.size() && k[i + 1][0] <= f) ++i;
  const size_t j = std::min(i + 1, k.size() - 1);
  const float d = k[j][0] - k[i][0], t = d > 0 ? std::clamp((f - k[i][0]) / d, 0.0f, 1.0f) : 0.0f;
  for (size_t c = 1; c < N; ++c) out[c - 1] = k[i][c] + (k[j][c] - k[i][c]) * t;
}
void CamB(int clip, bool quieto = false, bool bucle = false, bool comparable = false) {   // hilo de la interfaz: este clip desde el cuadro 0
  g_cb_comparable = comparable;
  // s10 PRUEBA (DBZ3_S10_CLIP=BB): fuerza un clip concreto (p. ej. la ronda 8 de NB007) para medir su camara sin
  // recorrer las diez rondas. Solo actua si el clip existe en la copia del jugador.
  if (const char* c = std::getenv("DBZ3_S10_CLIP")) {
    const int v = int(std::strtol(c, nullptr, 16));
    if (g_clips.count(v)) clip = v;
  }
  if (clip < 0 || !g_clips.count(clip)) return;
  g_cb_amc = clip >= kCamEntrada ? g_cb_amc_entrada.load() : clip >= kCam879 ? 879 : 915;
  SerieClip(clip);
  g_cb_clip = clip;
  g_cb_frame = 0;
  g_cb_quieto = quieto;
  g_cb_bucle = bucle;
  REXLOG_INFO("dbz3 r14: camara de IW: clip 0x{:X} ({} cuadros{})", clip, g_clips[clip].nf,
              quieto ? ", quieto" : bucle ? ", en bucle" : "");
}
// s8: cuadro del clip en curso (en bucle: vuelve a 0 al acabar, como la camara de juego de IW)
float CuadroClip(const ClipCam& c) {
  const float f = float(g_cb_frame.load());
  if (g_cb_bucle.load() && c.nf > 1) return std::fmod(f, float(c.nf));
  return std::min(f, float(c.nf - 1));
}

// ---- Sonidos de golpe ----
// El codigo de IW de estos minijuegos no tiene un efecto de impacto propio: sus voces (Hit_PlayHealthTierSound,
// Minigame_PlayEventVoice, Audio_PlayCharacterVoice) salen por el sistema de ids del IOP de la PS2, sin traducir a las ADX de
// la copia. Suenan los equivalentes de B3, por el mismo gestor de efectos que usa su combate (sub_820922A0; ids medidos con
// DBZ3_TRAZA_SE en un combate normal: banco 2 id 2 = impacto, banco 4 ids 42/43/50 = grito de ataque del luchador 1 y
// 19/20/25 = su grito al ser golpeado). Se piden desde el hilo de la interfaz con un retardo (el puno llega unos cuadros
// despues de empezar la pose) y los reproduce el hilo del juego (SeBotones, gancho de iw_minijuegos.cpp).
// 06 (iw_eventos_y_mezcla): las animaciones de IW que traen lineas de eventos (las poses de NB006 y el #BSK comun) ya
// disparan su sonido por su cuadro desde anim_iw.cpp; cuando uno de estos retardos vence y ha sonado un evento hace poco,
// se descarta (no se dobla el golpe). El resto (NB013/NB014, cuyas poses no traen eventos) siguen con estos ids.
constexpr int kSeImpacto[2] = {2, 2}, kSeAtaque[3] = {42, 43, 50}, kSeHerido[3] = {19, 20, 25};
struct SePend {
  int t = 0, banco = 0, id = 0;
};
SePend g_cola_se[8];
std::atomic<int> g_se_listo{-1};   // banco * 1000 + id, para el hilo del juego
void PedirSe(int cuadros, int banco, int id) {
  for (SePend& e : g_cola_se)
    if (e.t <= 0) {
      e = SePend{std::max(cuadros, 1), banco, id};
      return;
    }
}
void CuadroSe() {   // un cuadro de 60 Hz (hilo de la interfaz)
  for (SePend& e : g_cola_se)
    if (e.t > 0 && --e.t == 0) {
      // 06: si la animacion de IW ya trajo su evento de sonido para este golpe, no se repite
      if (dbz3::anim::EventosSonandoReciente()) {
        REXLOG_INFO("dbz3 r14: sonido banco {} id {} descartado (la animacion de IW ya sono su evento)", e.banco, e.id);
        continue;
      }
      g_se_listo = e.banco * 1000 + e.id;
      REXLOG_INFO("dbz3 r14: sonido banco {} id {}", e.banco, e.id);
    }
}
// Solo suenan las voces del banco del jugador (el del rival no esta cargado): su grito al atacar y al ser golpeado.
void SeGolpe(int atacante, bool conecta) {   // atacante 0 = el jugador; conecta: el golpe da (si no, lo esquivan)
  if (atacante == 0) PedirSe(2, 4, kSeAtaque[Azar() % 3]);
  if (!conecta) return;
  PedirSe(12, 2, kSeImpacto[0]);
  if (atacante == 1) PedirSe(14, 4, kSeHerido[Azar() % 3]);
}


// QteMg_PlayHitPoses(carril, acierto): las dos poses del golpe; despues cada uno vuelve a su espera de pelea.
void PosesGolpe(Juego& j, int carril, bool acierto) {
  const Poses& p = *j.poses;
  const int k = j.k;
  if (carril == 0) {
    Poner(p, 0, p.j_ataca[k], false, p.pelea[0]);
    Poner(p, 1, acierto ? p.r_herido[k] : p.r_esquiva[k], false, p.pelea[1]);
    SeGolpe(0, acierto);
  } else {
    Poner(p, 1, p.r_ataca[k], false, p.pelea[1]);
    Poner(p, 0, acierto ? p.j_esquiva[k] : p.j_herido[k], false, p.pelea[0]);
    SeGolpe(1, !acierto);
  }
  j.k = p.azar ? Azar() & 3 : (k + 1) & 3;
}

// Fallo del carril 1: el rival golpea (vida del jugador; la pose la pone PosesGolpe).
// EU 0x3301C0 QteMg_ApplyHitDamage
void Golpe(Juego& j) { j.vida[0] -= float(j.m->ataque_rival[j.dif]); }

// Un cuadro de 60 Hz de la logica de IW (QteMg_UpdatePlayerButtons / ProcessPlayerInput / ApplyHitDamage).
// EU 0x332270 QteMg_UpdatePlayerButtons
void Paso(Juego& j, uint16_t pulsado) {
  ++j.cuadros;
  if (j.aviso_t > 0) --j.aviso_t;
  if (j.pausa > 0) {   // R7: pausa del clip 0x98 de NB006 (punto ganado) o del choque de NB013: los iconos no avanzan
    --j.pausa;
    return;
  }
  bool golpe[2] = {false, false};   // R7: acierto en cada carril este cuadro (para el choque de NB013)
  for (int l = 0; l < 2; ++l) {
    Carril& c = j.c[l];
    if (--c.cuenta <= 0 && c.sale < c.n) {   // sale el siguiente icono
      c.ic[c.sale] = Icono{c.seq[c.sale], -16 - j.vel, true};
      ++c.sale;
      c.cuenta = c.sale < c.n ? c.hueco[c.sale] : 0;
    }
    for (int i = 0; i < c.sale; ++i) {
      Icono& k = c.ic[i];
      if (!k.vivo) continue;
      k.x += j.vel;
      if (!k.resuelto && !k.contado && std::fabs(k.x - kObjetivo) < kIcono) {   // entra en el objetivo
        k.contado = true;
        ++c.llegaron;
      }
      if (!k.resuelto && k.x > kFalla) {   // se paso: fallo
        k.resuelto = true;
        if (!k.contado) {
          k.contado = true;
          ++c.llegaron;
        }
        PosesGolpe(j, l, false);   // QteMg_UpdatePlayerButtons: PlayHitPoses(carril, 0) al pasarse
        if (l == 1) Golpe(j);
        j.aviso_t = 30;
        j.aviso_bien = false;
      }
      if (k.x >= kQuita) k.vivo = false;
    }
    while (c.actual < c.sale && c.ic[c.actual].resuelto) ++c.actual;
    // prueba (auto): pulsa el boton correcto cuando el icono esta en el objetivo
    // EU 0x2EEE80 QTE_GetPressedButton: la tabla de mascaras 0x498690 es 1<<i (comprobado en el ELF EU), la recorre
    // 0..15 y se queda con el ULTIMO bit pulsado; -1 si no hay ninguno. Aqui se aplica esa resolucion al mando del
    // cuadro, separando por carril (0 = botones, 1 = gatillos). El estado de la QTE (habilitada +0x110 y flags de
    // slot +0x100) no se replica: la fase del port ya filtra cuando no se acepta entrada (estructura).
    uint16_t p = pulsado & (l == 0 ? 0xF000 : 0x0F00);
    if (j.auto_ && c.actual < c.sale && c.ic[c.actual].x >= kAutoX && !c.ic[c.actual].pedido) {
      Icono& k = c.ic[c.actual];
      k.pedido = true;
      if (j.fallos && l == 1 && c.actual % 5 == 2) continue;   // prueba: fallo a proposito (el rival golpea)
      if (const int32_t x = XboxDe(k.boton)) {   // pulsado de verdad: llega por el mando como el del jugador
        j.inyecta |= x;
        j.inyecta_t = 4;
      } else {
        p = uint16_t(1u << k.boton);
      }
    }
    if (p && c.actual < c.sale && c.ic[c.actual].x > kAcepta) {
      Icono& k = c.ic[c.actual];
      k.resuelto = true;
      if (!k.contado) {
        k.contado = true;
        ++c.llegaron;
      }
      j.aviso_t = 30;
      if (BotonPulsado(p) == k.boton) {   // QTE_CheckPressedMatch: el boton resuelto, no la mera pertenencia
        ++c.aciertos;
        k.vivo = false;
        golpe[l] = true;
        if (l == 0) j.vida[1] -= float(j.m->ataque_jugador[j.dif]);
        PosesGolpe(j, l, true);
        j.aviso_bien = true;
        if (l == 0 && j.punto) {   // R7: NB006, cada punto ganado: pausa de Mg_ResetRoundState. La 0x98 es una
          // ANIMACION (pose del jugador), no la camara: el original mantiene la camara de juego 0x97 durante todo el
          // minijuego (medido en PCSX2: la 0x98 tiene pista de camara pero UiAnim no la usa como camara).
          j.pausa = kPausaPunto;
        }
      } else {
        PosesGolpe(j, l, false);   // boton malo: PlayHitPoses(carril, 0)
        if (l == 1) Golpe(j);
        j.aviso_bien = false;
      }
    }
  }
  // R7 (05_iw_minijuegos_pendientes): choque de NB013 (Mg_StartClashSequence 0x352A50 / Mg_CheckClashOutcome
  // 0x352760). Empieza con las poses 0x11 (jugador) y 0x21 (rival) y el clip 0x188 (Mg_StartClashSequence); luego es un
  // pulso de botones (Mg_UpdateClashPhase 0x353090) y gana quien llega a la pose 3 contra la 2 (Mg_CheckClashOutcome:
  // si jugador 3 y rival 2, efecto + UiAnim 0x187 y reinicia la ronda). Al montar el choque pone la ENERGIA de los dos
  // (+0x185C, el medidor del scatto, no la vida) a 100 (0x42C80000). Aqui, sin fase de pulso, se mantiene el choque
  // visual (poses del choque + clip) y no cambia la vida; falta la fase de pulsos y el reinicio de ronda.
  static const int choque_t = std::getenv("DBZ3_IWMINI_CHOQUE") ? std::atoi(std::getenv("DBZ3_IWMINI_CHOQUE")) : 0;   // prueba
  if (choque_t && j.cuadros == choque_t) golpe[0] = golpe[1] = true;   // prueba: fuerza el choque en el cuadro choque_t
  if (golpe[0] && golpe[1] && j.poses->cam_choque >= 0) {
    const Poses& q = *j.poses;
    Poner(q, 0, q.choque[0], false, q.pelea[0]);
    Poner(q, 1, q.choque[1], false, q.pelea[1]);
    CamB(q.cam_choque);
    j.pausa = kPausaChoque;
    REXLOG_INFO("dbz3 r7: choque (los dos carriles aciertan): clip 0x{:X}", q.cam_choque);
  }
  // EU 0x2EE9E0 QTE_IsSequenceDone
  const bool gastado = j.c[0].actual >= j.c[0].n && j.c[1].actual >= j.c[1].n;
  if (gastado || j.vida[0] <= 0 || j.vida[1] <= 0) {   // QteMg_ClassifyResult + FinalizeResult
    const int ac = j.c[0].aciertos + j.c[1].aciertos, ll = j.c[0].llegaron + j.c[1].llegaron;
    // EU 0x32FA20 QteMg_ClassifyResult
    j.nota = ll > 0 ? int(float(ac) / float(ll) * 100.0f) : 0;
    j.rango = 0;
    if (j.vida[0] > 0)
      for (int r = 5; r >= 1; --r)
        if (j.nota >= kUmbral[r][0]) {
          j.rango = r;
          break;
        }
    const float v = j.rango == 0 ? 100.0f : j.pts[j.rango] * j.mult[j.dif];
    j.puntos = (int(v) + 99) / 100 * 100;
    // EU 0x32F0F0 QteMg_FinalizeResult: `puntos` es el premio ZENI (tabla del rango x dificultad); la puntuacion de
    // la pantalla es `nota` (0..100), separada. TOTAL ZENI = lo que queda en el monedero al sumarlo.
    j.zeni = j.puntos;
    j.saldo = P::LeerProgreso("iw_zeni", 0) + j.zeni;
    j.fase = 3;
    if (j.poses == &kPosesNB006) g_fondo_off = false;
    j.t = 0;
    {   // QteMg_StartResultPhase: poses de victoria o derrota (la del jugador al acabar sale en el panel)
      const Poses& p = *j.poses;
      Poner(p, 0, j.rango > 0 ? p.gana[0] : p.pierde[0], false);
      Poner(p, 1, j.rango > 0 ? p.gana[1] : p.pierde[1], false);
    }
    REXLOG_INFO("dbz3 r14: fin: aciertos {}/{} (carril 0 {}/{}, carril 1 {}/{}), vida {} / {}, nota {}, rango {}, "
                "puntos {}",
                ac, ll, j.c[0].aciertos, j.c[0].llegaron, j.c[1].aciertos, j.c[1].llegaron, int(j.vida[0]),
                int(j.vida[1]), j.nota, j.rango, j.puntos);
  }
}

class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!g_corre.load()) return;
    std::shared_ptr<Juego> j;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      j = g_j;
    }
    if (!j) return;
    uint8_t* base = Base();
    // el final de B3 (estados 5-8: su RING OUT, la pose del ganador y su menu azul "Luchar otra vez?") corre a la vez que el
    // resultado (fase 3) y se tapa con negro: sin esto el menu de B3 salia ~1,7 s entre el resultado y el mapa
    const int est = P::EstadoCombate();
    if (!base || (est != 4 && !(j->fase == 3 && est >= 5 && est <= 8))) return;
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (!mgr) return;
    if (j.get() != hecho_) Preparar(*j);
    // los dos luchadores de B3 no caen: la vida es la del minijuego
    for (uint32_t x : {mgr + kX0, mgr + kX0 + kXPaso}) StoreF(base, x + kVida, LoadF(base, x + kVidaMax));
    // camara: al ras del suelo en la presentacion, a la altura del combate aereo al empezar, y mas abierta en el resultado
    // (las poses de victoria y derrota se van lejos: raiz de hasta 96 u) y despues mas cerca para la pose final
    {
      const float aire = j->poses->aire, d = j->poses->dist;
      g_cam_alto = j->fase == 0 ? (aire > 20 ? kAlto0 : aire) : j->fase == 1 ? (aire > 20 ? kAlto0 + 0.5f * (aire - kAlto0) : aire) : j->fase == 2 ? aire
                   : j->t < kFinRes ? (aire > 20 ? aire + 8 : aire - 4) : std::min(18.0f, aire);
      g_cam_dist = j->fase == 3 ? (j->t < kFinRes ? d * 1.5f : d * 0.65f) : d;
    }
    g_cam_lado = j->fase == 3 && j->t >= kFinRes ? -1.0f : 1.0f;
    if (const char* e = std::getenv("DBZ3_R14_CAMD")) g_cam_dist = float(std::atof(e));   // prueba
    const bool pausa = dbz3::GameDimmed() || dbz3::OverlayOpen();
    // el mando que ve el minijuego (MandoBotones guarda lo que leyo el juego antes de quitarle los botones a B3)
    const uint16_t ahora = g_raw_ok.load() ? g_raw12.load() : REX_LOAD_U16(kPads + kPadBotones);
    j->pendiente = uint16_t(j->pendiente | (ahora & ~j->antes));   // hasta el siguiente cuadro de la logica
    if (ahora != j->antes && sondas_ < 40) {   // registro: los 40 primeros cambios del mando (sonda de teclas)
      ++sondas_;
      REXLOG_INFO("dbz3 r14: mando +12 {:04X}", ahora);
    }
    j->antes = ahora;
    acum_ += pausa ? 0.0f : dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
    while (acum_ >= 1.0f / 60) {   // logica a 60 cuadros por segundo, como en la PS2
      acum_ -= 1.0f / 60;
      ++j->t;
      SerieJuego(j->fase == 2);
      CuadroSe();
      if (g_cb_clip.load() >= 0 && !g_cb_quieto.load()) ++g_cb_frame;
      // s8 (medido en PCSX2): la entrada es [#AMC de la escena (amc_entrada, sus clips seguidos; poses 0/0)] ->
      // [cam_presenta (dur_presenta cuadros; el jugador en su pose 1)] -> READY. R7: NB006 abre con el clip 0x321 entero
      // (Mg_UpdateIntroSequence EU L468128-468176), que es su cam_presenta.
      const Poses& q0 = *j->poses;
      const bool pre = q0.cam_presenta >= 0 && g_clips.count(q0.cam_presenta);
      const int t_pre = pre ? q0.dur_presenta : 30;
      if (j->fase == 0) {
        if (j->t == 1 && j->ent > 0) CamB(kCamEntrada);   // primer clip de la entrada
        for (int i = 1, acum = 0; i < q0.n_entrada && j->ent > 0; ++i) {   // los siguientes, seguidos
          const auto it = g_clips.find(kCamEntrada + i - 1);
          acum += it == g_clips.end() ? 0 : it->second.nf;
          if (j->t == acum + 1 && g_clips.count(kCamEntrada + i)) CamB(kCamEntrada + i);
        }
        if (j->t == j->ent + 1) {   // presentacion: su clip y la pose 1 del jugador (la animacion de la 0, una vez)
          if (pre) CamB(q0.cam_presenta);
          else if (j->ent == 0) CamB(q0.cam_ini, true);   // sin clip: el plano de salida, quieto
          if (const uint32_t x = Luchador(0), a = AliasPose(q0, 0, q0.ini[0]); x && a)
            dbz3::anim::Reproducir(x, a, true);   // sujeta en su ultimo cuadro hasta el READY
        }
      }
      if (j->fase <= 1) {
        // R14 (respaldo): Juntar ponia al rival a una distancia fija. s8: los coloca PasoBotones como IW (Colocar);
        // DBZ3_R14_SEP=<n> vuelve al Juntar viejo para comparar
        static const float sep_env = std::getenv("DBZ3_R14_SEP") ? float(std::atof(std::getenv("DBZ3_R14_SEP"))) : 0.0f;   // prueba
        if (sep_env > 0) Juntar(base, mgr, sep_env);
        j->pendiente = 0;   // lo pulsado antes de empezar no cuenta
      }
      if (j->fase == 0 && j->t >= j->ent + t_pre) {
        j->fase = 1;
        j->t = 0;
        REXLOG_INFO("dbz3 r14: en juego ({} dificultad {})", j->m->nombre, j->dif);
        CamB(j->poses->cam_ini);   // READY?: el clip entero (UpdateCountdownPhase)
        // READY?: jugador pose 2 / rival pose 1 (QteMg_UpdateCountdownPhase); pasan a la espera al acabar
        Poner(*j->poses, 0, j->poses->listo[0], false, j->poses->pelea[0]);
        Poner(*j->poses, 1, j->poses->listo[1], false, j->poses->pelea[1]);
      } else if (j->fase == 1 && j->t >= (j->poses->cam_ini >= 0 && g_clips.count(j->poses->cam_ini) ? j->poses->dur_intro : 60)) {
        j->fase = 2;
        j->t = 0;
        // s8: la camara de juego de IW NO esta quieta: el clip (0x187 / 0x1A5 / 0x97, 900 cuadros) se mueve despacio y
        // vuelve a empezar (medido: cuadro 885 -> 1,2 en PCSX2)
        CamB(j->poses->cam_juego, false, true, true);
        Poner(*j->poses, 0, j->poses->pelea[0], true);   // START!: la espera de pelea (jugador 3 / rival 2)
        Poner(*j->poses, 1, j->poses->pelea[1], true);
      } else if (j->fase == 2 && std::getenv("DBZ3_R14_POSES")) {
        // PRUEBA (DBZ3_R14_POSES=1): en vez de jugar, cada 90 cuadros la pose siguiente de cada luchador (la lista del
        // modulo) y una rafaga de capturas (dbz3_shot_NNN.bmp, una cada 45 pintados) para verlas
        const Poses& p = *j->poses;
        std::vector<uint16_t> lj{p.ini[0], p.listo[0], p.pelea[0]}, lr{p.ini[1], p.listo[1], p.pelea[1]};
        for (int q = 0; q < 4; ++q) {
          lj.push_back(p.j_ataca[q]), lr.push_back(p.r_ataca[q]);
          lj.push_back(p.j_esquiva[q]), lr.push_back(p.r_esquiva[q]);
          lj.push_back(p.j_herido[q]), lr.push_back(p.r_herido[q]);
        }
        lj.insert(lj.end(), {p.gana[0], p.pierde[0], p.fin_bien, p.fin_mal});
        lr.insert(lr.end(), {p.gana[1], p.pierde[1]});
        if (j->t == 1) std::ofstream("dbz3_shot.req") << int(lj.size() * 2) << " 45";
        if ((j->t - 1) % 90 == 45) {   // la cadera de cada uno a media pose (mundo de B3) y la posicion del objeto
          for (int rol = 0; rol < 2; ++rol) {
            float r[3] = {0, 0, 0};
            const uint32_t x = Luchador(rol);
            const bool ok = x && dbz3::anim::RaizActual(base, x, r);
            REXLOG_INFO("dbz3 r14: prueba de poses: rol {} cadera {} ({:.1f}, {:.1f}, {:.1f}) objeto ({:.1f}, {:.1f}, {:.1f})", rol,
                        ok, r[0], r[1], r[2], LoadF(base, x + 64), LoadF(base, x + 68), LoadF(base, x + 72));
          }
        }
        if ((j->t - 1) % 90 == 0) {
          const size_t n = size_t((j->t - 1) / 90);
          if (n < lj.size()) {
            REXLOG_INFO("dbz3 r14: prueba de poses {}: jugador 0x{:X} rival 0x{:X}", n, lj[n], lr[n % lr.size()]);
            Poner(p, 0, lj[n], false, p.pelea[0]);
            Poner(p, 1, lr[n % lr.size()], false, p.pelea[1]);
          } else if (n == lj.size()) {
            REXLOG_INFO("dbz3 r14: prueba de poses acabada");
          }
        }
      } else if (j->fase == 2 && j->t == 60 && std::getenv("DBZ3_R14_SONDA")) {   // SONDA: el rumbo de los luchadores en X
        const uint32_t xa = mgr + kX0, xb = xa + kXPaso;
        const float ax = LoadF(base, xa + 64), az = LoadF(base, xa + 72), bx = LoadF(base, xb + 64), bz = LoadF(base, xb + 72);
        REXLOG_INFO("dbz3 r14: sonda: A ({:.2f}, {:.2f}, {:.2f}) B ({:.2f}, {:.2f}, {:.2f})", ax, LoadF(base, xa + 68), az, bx,
                    LoadF(base, xb + 68), bz);
        for (uint32_t o = 0; o < 0x400; o += 4) {
          const float v = LoadF(base, xa + o), w = LoadF(base, xb + o);
          if (std::fabs(v) > 0.2f && std::fabs(v) <= 1.0001f && std::fabs(v - std::round(v)) > 0.001f)
            REXLOG_INFO("dbz3 r14: sonda: A+{} = {:.4f}  B+{} = {:.4f}", o, v, o, w);
        }
        Paso(*j, j->pendiente);
        j->pendiente = 0;
      } else if (j->fase == 2) {
        Paso(*j, j->pendiente);
        j->pendiente = 0;   // una pulsacion vale para un solo cuadro de la logica
        if (j->auto_) {
          dbz3::net::InyectarPad(0, j->inyecta_t > 0 ? j->inyecta : 0);
          if (j->inyecta_t > 0 && --j->inyecta_t == 0) j->inyecta = 0;
        }
      } else if (j->fase == 3) {
        if (j->t == 1) {
          if (std::getenv("DBZ3_IWMINI_RAFAGA_FIN")) std::ofstream("dbz3_shot.req") << "40 15";   // prueba
          if (j->auto_) dbz3::net::InyectarPad(0, -1);
          if (!fin_) {   // el combate de B3 acaba ya (como el ring out de P4): su final (~11 s) corre bajo el resultado
            fin_ = true;
            REX_STORE_U8(mgr + kFin, uint8_t(j->rango > 0 ? 1 : 2));
          }
          CamB(j->rango > 0 ? j->poses->cam_gana : j->poses->cam_pierde);   // QteMg_StartResultPhase
        }
        if (j->t == kFinRes) {   // QteMg_FinalizeResult: la pose final del jugador (s8: a los 240 cuadros, medido)
          Poner(*j->poses, 0, j->rango > 0 ? j->poses->fin_bien : j->poses->fin_mal, false);
          CamB(j->rango > 0 ? j->poses->cam_fin_bien : j->poses->cam_fin_mal);
        }
      }
    }
    Dibujar(io, *j);
  }

 private:
  void Preparar(const Juego& j) {
    hecho_ = &j;
    fin_ = false;
    acum_ = 0;
    g_cam_reset = true;
    Poner(*j.poses, 0, j.poses->ini[0], true);   // pose 0 de la presentacion (los dos quietos)
    Poner(*j.poses, 1, j.poses->ini[1], true);
    objetivo_ = ps2::Crear(j.objetivo);
    failed_ = ps2::Crear(j.failed);
    ready_ = ps2::Crear(j.ready);
    start_ = ps2::Crear(j.start);
    hit_ = ps2::Crear(j.hit);
    miss_ = ps2::Crear(j.miss);
    clear_ = ps2::Crear(j.clear);
    aro_ = ps2::Crear(j.aro);
    relleno_ = ps2::Crear(j.relleno);
    barra_ = ps2::Crear(j.barra);
    barra06_ = ps2::Crear(j.barra06);
    banda_ = ps2::Crear(j.banda);
    aro4_ = ps2::Crear(j.aro4);
    marco4_ = ps2::Crear(j.marco4);
    verde4_ = ps2::Crear(j.verde4);
    for (int i = 0; i < 2; ++i) sil_[i] = ps2::Crear(j.sil[i]);
    caras_ = ps2::Crear(j.caras);
    for (int i = 0; i < 3; ++i) rot_[i] = ps2::Crear(j.rot[i]);
    cif_c_ = ps2::Crear(j.cif_c);
    cif_n_ = ps2::Crear(j.cif_n);
    earned_ = ps2::Crear(j.earned);
    total_ = ps2::Crear(j.total);
    zeni_ = ps2::Crear(j.zeni_img);
    titulo_ = ps2::Crear(j.titulo);
    cif_e_ = ps2::Crear(j.cif_e);
    panel_fill_ = ps2::Crear(j.panel_fill);
    row_arrow_ = ps2::Crear(j.row_arrow);
    row_band_ = ps2::Crear(j.row_band);
    row_labels_ = ps2::Crear(j.row_labels);   // DS156
    row_ranks_ = ps2::Crear(j.row_ranks);
  }
  static void Pieza(ImDrawList* dl, const ps2::Tex& t, ImVec2 c, float k) {
    if (t) dl->AddImage(t.id(), ImVec2(c.x - t.w * k / 2, c.y - t.h * k / 2), ImVec2(c.x + t.w * k / 2, c.y + t.h * k / 2));
  }
  // s9: panel de resultado de IW (iw_nb013_d, 800x600 -> 640x448): marco 16..624 x 10..438, rotulos en x 68 (centros y
  // 73 / 138 / 202) con su % a la derecha (x 604, 22 mas abajo), EARNED! en y 347 y TOTAL en 403 con la cifra naranja
  // hasta x 470 y ZENI en x 532. El titulo del minijuego, arriba (s1 F04.3)
  template <class At>
  void DibujarResultado(ImDrawList* fg, At at, const Juego& j) {
    // DS72: el relleno del panel es el arte de interfaz de IW (DATA_USA 334/1 t00), no un color propio.
    // t00 se pinta con alfa 175 como la caja anterior: el original deja ver al luchador por detras (iw_nb013_d).
    if (panel_fill_)
      fg->AddImageRounded(panel_fill_.id(), at(16, 10), at(624, 438), ImVec2(0, 0), ImVec2(1, 1),
                          IM_COL32(255, 255, 255, 175), 26.0f);
    else
      fg->AddRectFilled(at(16, 10), at(624, 438), IM_COL32(8, 40, 72, 175), 14);
    // El marco fino azul del original no es un sprite unico: el unico marco grande de 334/1 (t03) es el bisel grueso del
    // panel del mapa y aqui quedaria 6 veces mas grueso. Se conserva el trazo medido [H].
    fg->AddRect(at(16, 10), at(624, 438), IM_COL32(110, 200, 255, 230), 14, 0, 3);
    // s1 F04.3: el titulo del minijuego arriba, centrado (medido en iw_nb013_d, 800x600: la letra azul de la textura 346/1
    // (visible x 2..338, y 11..43 de 340x46) ocupa x 180..620 e y 34..65 -> en 640x448, 1,065 de ancho por 0,91 de alto
    // y centro de la parte visible en (320, 37))
    if (titulo_) {
      const float w = titulo_.w * 1.065f, h = titulo_.h * 0.91f, y0 = 37 - 27 * 0.91f;
      fg->AddImage(titulo_.id(), at(320 - w / 2, y0), at(320 + w / 2, y0 + h));
    }
    auto img = [&](const ps2::Tex& tx, float x, float cy, float alto = 0) {   // alto > 0: escalado a esa altura
      if (!tx) return;
      const float h = alto > 0 ? alto : float(tx.h), w = tx.w * h / float(tx.h);
      fg->AddImage(tx.id(), at(x, cy - h / 2), at(x + w, cy + h / 2));
    };
    // cifras cian (5x4 de 25,6x22): g 0-9, 10 = %
    auto cian = [&](int g, float xd, float cy) {   // xd = borde derecho; devuelve el ancho
      const float cw = 25.6f, ch = 22.0f;
      if (g == 10) {   // s1 F04.3: el % no esta en la rejilla (alfa medido en 5/5 t05: x 48..74, y 49..70)
        fg->AddImage(cif_c_.id(), at(xd - 27, cy - 11), at(xd, cy + 11), ImVec2(48 / 128.0f, 49 / 88.0f),
                     ImVec2(75 / 128.0f, 71 / 88.0f));
        return 25.0f;
      }
      // s1 F04.3: las celdas medidas por el alfa (la rejilla de 25,6 x 22 cortaba los glifos y metia trozos del vecino):
      // centros x 13 / 36 / 60,5 / 85 / 109,5, filas y 0..23 y 24..47
      constexpr float kCx[5] = {13.0f, 36.0f, 60.5f, 85.0f, 109.5f};
      const int col = g % 5, row = g / 5;
      const float u0 = (kCx[col] - 12) / 128, u1 = (kCx[col] + 12) / 128, v0 = row * 24 / 88.0f, v1 = (row * 24 + 23) / 88.0f;
      fg->AddImage(cif_c_.id(), at(xd - 24, cy - ch / 2), at(xd, cy + ch / 2), ImVec2(u0, v0), ImVec2(u1, v1));
      (void)cw;
      return 19.0f;   // avance del glifo (la celda trae margen; IW las junta)
    };
    const ps2::Tex* grandes = &cif_n_;   // s1 F04.3: EARNED con las cifras cian grandes (5/5 t06), TOTAL naranjas (t04)
    auto naranja = [&](int g, float xd, float cy) {   // 11 cifras de 32 x 40
      fg->AddImage(grandes->id(), at(xd - 32, cy - 20), at(xd, cy + 20), ImVec2(g * 32 / 352.0f, 0), ImVec2((g + 1) * 32 / 352.0f, 1));
      return 32.0f;
    };
    auto numero = [&](int v, float xd, float cy, bool pct, int min_dig = 1) {
      if (pct) xd -= cian(10, xd, cy);
      int ds[12], n = 0;
      do {
        ds[n++] = v % 10;
        v /= 10;
      } while (v > 0);
      while (n < min_dig && n < 12) ds[n++] = 0;   // DS156: las cifras aun ocultas de EARNED/TOTAL dejan su hueco
      for (int i = 0; i < n; ++i) xd -= pct ? cian(ds[i], xd, cy) : naranja(ds[i], xd, cy);
    };
    auto pct = [&](const Carril& c) { return c.llegaron > 0 ? c.aciertos * 100 / c.llegaron : 0; };
    const float cy[3] = {73, 138, 202};
    const int val[3] = {pct(j.c[0]), pct(j.c[1]), j.nota};
    for (int i = 0; i < 3; ++i) {
      if (row_arrow_)   // DS72: el triangulo azul es 334/1 t08 (24x24), no un triangulo dibujado
        fg->AddImage(row_arrow_.id(), at(38, cy[i] - 10), at(58, cy[i] + 10));
      else
        fg->AddTriangleFilled(at(42, cy[i] - 8), at(42, cy[i] + 8), at(54, cy[i]), IM_COL32(110, 200, 255, 255));
      img(rot_[i], 68, cy[i]);
      numero(val[i], 604, cy[i] + 22, true);
      fg->AddLine(at(40, cy[i] + 40), at(604, cy[i] + 40), IM_COL32(110, 200, 255, 200), 2);
    }
    // DS156: RANK. QteMg_FinalizeResult (EU 0x32F0F0) escribe la fila de rango en el bloque de resultados (tipo 2 en
    // +0x40, indice en +0x48 y premio en +0x4C): el original la pinta entre TOTAL SCORE y EARNED. Solo sale al superar
    // el minijuego: en las capturas del original con 0 % (iw_nb013_d, nb006_100, nb014_108) no hay fila. Arte del
    // mismo #AMA que el panel del mapa (334/1 t17 fila 1 RANK y t24 C B A S Z).
    if (j.rango > 0) {
      const float yr = 267;
      if (row_arrow_)   // el mismo triangulo de las otras filas (334/1 t08)
        fg->AddImage(row_arrow_.id(), at(38, yr - 10), at(58, yr + 10));
      else
        fg->AddTriangleFilled(at(42, yr - 8), at(42, yr + 8), at(54, yr), IM_COL32(110, 200, 255, 255));
      if (row_labels_) {   // 334/1 t17: 152x160, cinco filas de 32 (0 TIME, 1 RANK, 2 BRICKS, 3 SCORE, 4 TOTAL SCORE)
        const float alto = 34, sy = 32, an = 152.0f * alto / 32.0f;   // misma altura que los rotulos 5/7
        fg->AddImage(row_labels_.id(), at(68, yr - alto / 2), at(68 + an, yr + alto / 2),
                     ImVec2(0, sy / row_labels_.h), ImVec2(152.0f / row_labels_.w, (sy + 32.0f) / row_labels_.h));
      }
      if (row_ranks_) {   // 334/1 t24 (96x80): C B A / S Z -; j.rango 1..5 -> 0..4 (C..Z)
        const int rg = std::clamp(j.rango - 1, 0, 4);
        const int col = rg >= 3 ? rg - 3 : rg, fil = rg >= 3 ? 1 : 0;
        const float alto = 34, an = alto * 32.0f / 40.0f;
        fg->AddImage(row_ranks_.id(), at(604 - an, yr - alto / 2), at(604, yr + alto / 2),
                     ImVec2(col * 32.0f / row_ranks_.w, fil * 40.0f / row_ranks_.h),
                     ImVec2((col + 1) * 32.0f / row_ranks_.w, (fil + 1) * 40.0f / row_ranks_.h));
      }
      fg->AddLine(at(40, yr + 40), at(604, yr + 40), IM_COL32(110, 200, 255, 200), 2);
    }
    for (float y : {347.0f, 403.0f}) {   // DS72: franjas claras de EARNED y TOTAL con 5/5 t03 (560x48)
      if (row_band_)
        fg->AddImage(row_band_.id(), at(28, y - 24), at(612, y + 24), ImVec2(0, 0), ImVec2(1, 1),
                     IM_COL32(255, 255, 255, 200));
      else
        fg->AddRectFilled(at(28, y - 24), at(612, y + 24), IM_COL32(200, 215, 230, 70), 18);
    }
    // EARNED y TOTAL ZENI: `puntos` es el premio (QteMg_FinalizeResult), `nota` la puntuacion. Ritmo del original
    // (EU 0x285F30 LoadWaitRewardScreen): EARNED empieza en el cuadro 120 y TOTAL en el 320, y cada cifra aparece 20
    // cuadros despues que la de su derecha (los nodos de cifras van de unidades hacia arriba). La captura d_fin del
    // 11:32:16 (2,3 s despues del fin) pillo el conteo a medias: 5760 de 10800 con TOTAL aun sin empezar, no un
    // monedero menor que el premio. Aqui las cifras se escriben ya con su valor final (como el original) y solo se
    // descubren de derecha a izquierda.
    auto digitos = [](int v) {
      int n = 1;
      while (v >= 10) {
        v /= 10;
        ++n;
      }
      return n;
    };
    auto revelar = [&](int valor, int desde, float y, const ps2::Tex* cif) {
      if (j.fase != 3 || j.t < desde) return;
      const int vis = std::min(1 + (j.t - desde) / 20, digitos(valor));
      int pot = 1;
      for (int i = 0; i < vis; ++i) pot *= 10;
      grandes = cif;
      numero(valor % pot, 470, y, false, vis);
    };
    img(earned_, 38, 347, 34);
    revelar(j.zeni, 120, 347, cif_e_ ? &cif_e_ : &cif_n_);
    img(zeni_, 520, 347, 28);
    img(total_, 38, 403, 34);
    revelar(j.saldo, 320, 403, &cif_n_);
    img(zeni_, 520, 403, 28);
  }
  void Dibujar(ImGuiIO& io, const Juego& j) {
    g_fase_a = j.fase;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const S::Area a = S::GameArea(io);
    auto at = [&](float x, float y) { return a.At(x * 1200 / kW, y * 900 / kH); };   // pantalla de IW -> 1200x900
    const float k = a.s * 900 / kH;
    // Vida de los dos (UiFade 0 y 1 del modulo). s9: el HUD propio de estos minijuegos (DATA_USA 4/0) es el que se
    // pinta cuando esta (banda t38, marcos t17/t18, relleno t19 y caras t53, medido en nb013_030); si faltara, se cae
    // al de los minijuegos de campo (6/0: aro con la cara, marco t15 y relleno amarillo t01).
    // s9: con el HUD de IW (4/0), sus piezas donde las pone IW (medido en nb013_030, 800x600 -> 640x448): aro con la cara
    // centrado en (42, 49), marco t18 en (80, 22) y relleno verde t19 en (96, 26); el rival en espejo (x -> 640 - x).
    auto rect = [&](const ps2::Tex& t, float x0, float y0, float w, float h, bool espejo) {
      if (!t) return;
      const float xa = espejo ? kW - x0 - w : x0;
      fg->AddImage(t.id(), at(xa, y0), at(xa + w, y0 + h), ImVec2(espejo ? 1.0f : 0.0f, 0), ImVec2(espejo ? 0.0f : 1.0f, 1));
    };
    const bool hud4 = banda_ && aro4_ && marco4_ && verde4_;
    const bool panel_res = rot_[0] && cif_c_ && cif_n_ && j.fase == 3;   // IW quita el HUD en el resultado
    for (int i = 0; hud4 && !panel_res && i < 2; ++i) {
      const float v = std::clamp(j.vida[i] / 1000.0f, 0.0f, 1.0f);
      rect(marco4_, 80, 22, 216, 40, i != 0);
      if (v > 0) {
        const float x0 = i == 0 ? 96 : kW - 96 - 176 * v;
        fg->AddImage(verde4_.id(), at(x0, 26), at(x0 + 176 * v, 42), ImVec2(i ? 1.0f - v : 0.0f, 0), ImVec2(i ? 1.0f : v, 1));
      }
      if (caras_ && j.cara[i] >= 0) {   // la cara de IW (t53), 60 px dentro del aro
        const float u = (j.cara[i] % 3) / 3.0f, w = (j.cara[i] / 3) / 3.0f, cx = i ? kW - 42 : 42;
        fg->AddImage(caras_.id(), at(cx - 30, 49 - 30), at(cx + 30, 49 + 30), ImVec2(i ? u + 1 / 3.0f : u, w),
                     ImVec2(i ? u : u + 1 / 3.0f, w + 1 / 3.0f));
      } else {
        const ImVec2 c0 = at(i ? kW - 42 : 42, 49);
        dbz3::iwmini::PintarCara(fg, c0.x - 30 * k, c0.y - 30 * k, 60 * k, i ? j.m->rival : j.m->jugador,
                                 i ? j.m->forma_rival : j.m->forma_jugador, i != 0);
      }
      rect(aro4_, 42 - 44, 49 - 40, 88, 80, i != 0);
    }
    for (int i = 0; !hud4 && i < 2; ++i) {
      const float v = std::clamp(j.vida[i] / 1000.0f, 0.0f, 1.0f), kk = 1.6f * a.s;
      auto x = [&](float px, float w) { return i == 0 ? px : 1200 - px - w; };   // px de 1200x900
      const ImVec2 b0 = a.At(x(150, 240 * 1.6f), 92);
      if (barra_) fg->AddImage(barra_.id(), b0, ImVec2(b0.x + 240 * kk, b0.y + 40 * kk), ImVec2(i ? 1.0f : 0.0f, 0),
                               ImVec2(i ? 0.0f : 1.0f, 1));
      if (relleno_ && v > 0) {
        const float x0 = i == 0 ? b0.x + 22 * kk : b0.x + (218 - 200 * v) * kk;
        fg->AddImage(relleno_.id(), ImVec2(x0, b0.y + 12 * kk), ImVec2(x0 + 200 * v * kk, b0.y + 28 * kk), ImVec2(0, 0),
                     ImVec2(v, 1));
      }
      {   // F1: cara del personaje que juega (y del rival) en su forma (iwmini::PintarCara)
        const ImVec2 c0 = a.At(x(28, 64 * 1.6f), 30);
        dbz3::iwmini::PintarCara(fg, c0.x, c0.y, 64 * 1.6f * a.s, i ? j.m->rival : j.m->jugador,
                                 i ? j.m->forma_rival : j.m->forma_jugador, i != 0);
      }
      if (aro_) fg->AddImage(aro_.id(), a.At(x(6, 88 * 1.6f), 8), a.At(x(6, 88 * 1.6f) + 88 * 1.6f, 8 + 88 * 1.6f),
                             ImVec2(i ? 1.0f : 0.0f, 0), ImVec2(i ? 0.0f : 1.0f, 1));
      if (!barra_) {   // sin el HUD de IW: barra lisa
        const ImVec2 p1(b0.x + 240 * kk, b0.y + 20 * kk);
        fg->AddRectFilled(b0, p1, IM_COL32(20, 20, 40, 200));
        fg->AddRectFilled(b0, ImVec2(b0.x + (p1.x - b0.x) * v, p1.y), IM_COL32(255, 210, 40, 255));
      }
    }
    if (j.fase == 1) Pieza(fg, ready_, at(kW / 2, 200), k);
    if (j.fase == 2 && j.t < 60 && j.cuadros < 60) Pieza(fg, start_, at(kW / 2, 200), k);
    if (j.fase >= 1 && hud4 && !panel_res) {   // s9: la banda azul de IW abajo (ya en el READY, como IW) (t38 a lo ancho, 640 x 134) y las dos siluetas
      fg->AddImage(banda_.id(), at(0, kH - 134), at(kW, kH));
      for (int i = 0; i < 2; ++i)
        if (sil_[i]) {
          const float cx = i ? 592 : 52, cy = 384, w = float(sil_[i].w), h = float(sil_[i].h);
          fg->AddImage(sil_[i].id(), at(cx - w / 2, cy - h / 2), at(cx + w / 2, cy + h / 2));
        }
    }
    if (j.fase >= 2 && !panel_res && j.m->poses == &kPosesNB006) {
      // I03: barra roja de NB006 (Mg_LayoutHud 0x376FB0: fila 0, y 366, largo vel*240-48). Va debajo de los
      // iconos, sobre la banda azul; el original la apaga en el resultado (panel_res). Se dibuja con primitivas
      // (la textura 4/0 t42 es el carril gris y al estirarla solo asomaba una linea): relleno rojo, borde azul y
      // la muesca blanca del boton entrante.
      const float len = std::min(kW, j.vel * 240.0f - 48.0f);
      const ImVec2 r0 = at(0, 376), r1 = at(len, 416);
      // DS72: la barra roja de NB006 NO tiene sprite: UiBar_DrawGauge (EU 0x372950) pinta un rectangulo de color con
      // el borde y la muesca; el 4/0 t42 (480x8) es el carril gris translucido, no el relleno. Se deja como esta [H].
      fg->AddRectFilled(r0, r1, IM_COL32(198, 62, 88, 255), 7 * k);
      fg->AddRect(r0, r1, IM_COL32(38, 78, 148, 255), 7 * k, 0, 2 * k);
      fg->AddRectFilled(at(len - 42, 378), at(len - 30, 414), IM_COL32(245, 245, 245, 235), 2 * k);
    }
    if (j.fase >= 2 && !panel_res) {
      fg->PushClipRect(at(0, 0), at(kW, kH), true);   // s9: los iconos entran desde el borde de la pantalla de IW (4:3)
      for (int l = 0; l < 2; ++l) {
        // s8: medido en la captura del original (nb013_030, 800x600 de 640x448): los aros en x 480 y en y 366 / 409,
        // es decir, kCarrilY son los CENTROS de los carriles (antes se dibujaban 16 px mas abajo y el aro en 420)
        const float y = kCarrilY[l];
        if (!hud4) fg->AddRectFilled(at(0, y - 18), at(kW, y + 18), IM_COL32(0, 0, 0, 110));
        if (objetivo_) {   // el aro de IW (72 x 56; el centro del aro en (38, 28))
          const ImVec2 c = at(kAroX, y);
          fg->AddImage(objetivo_.id(), ImVec2(c.x - 38 * k, c.y - 28 * k), ImVec2(c.x + 34 * k, c.y + 28 * k));
        } else {
          fg->AddCircle(at(kAroX, y), 20 * k, IM_COL32(255, 230, 80, 255), 24, 3 * a.s);
        }
        const S::Pic pic = S::Img("botones");
        for (int i = 0; i < j.c[l].sale; ++i) {
          const Icono& ic = j.c[l].ic[i];
          if (!ic.vivo) continue;
          const ImU32 col = ic.resuelto ? IM_COL32(120, 120, 120, 200) : IM_COL32(255, 255, 255, 255);
          const ImVec2 c = at(ic.x + kIcono / 2, kCarrilY[l]);
          if (!pic) {   // sin la pieza de la HD: el nombre del boton
            S::Glyph(fg, kNombre[ic.boton & 15], ImVec2(c.x - kIcono * k / 2, c.y), kIcono * k);
            continue;
          }
          const ImVec4 r = Glifo(ic.boton);
          const float h = kIcono * k * 1.1f, w = (r.z - r.x) * h / (r.w - r.y);
          S::DrawPic(fg, pic, ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), r, col);
        }
      }
      fg->PopClipRect();
      if (j.aviso_t > 0) Pieza(fg, j.aviso_bien ? hit_ : miss_, at(kAroX, 330), k * 0.8f);
    }
    if (j.fase == 3) {
      const float t = std::min(1.0f, j.t / 30.0f);
      fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(160 * t)));
      // CLEAR del juego (como P4); la nota, el rango y los puntos los ensena el mapa (PantallaDM::Resultado)
      if (!panel_res || j.t < 90) Pieza(fg, j.rango > 0 ? clear_ : failed_, at(kW / 2, 200), k * 1.5f * (0.8f + 0.2f * t));
      if (panel_res && j.t >= 90) DibujarResultado(fg, at, j);
      // s8 (medido): resultado 240 cuadros + final del jugador 240; despues a negro (tapa lo que quede del final de B3
      // hasta el mapa o la tabla de resultados)
      if (const float neg = std::clamp((j.t - 2 * kFinRes + 12) / 36.0f, 0.0f, 1.0f); neg > 0)
        fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * neg)));
    }
  }
  // El rival se pone a `sep` delante del jugador (en la linea que los une).
  static void Juntar(uint8_t* base, uint32_t mgr, float sep) {
    const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
    const float ax = LoadF(base, x0 + 64), az = LoadF(base, x0 + 72), bx = LoadF(base, x1 + 64), bz = LoadF(base, x1 + 72);
    const float dx = bx - ax, dz = bz - az, l = std::hypot(dx, dz);
    if (l < 1e-3f || std::fabs(l - sep) < 0.5f) return;
    for (uint32_t o : {48u, 64u, 80u}) {   // la posicion esta tres veces (tagteam.cpp StepSlide)
      StoreF(base, x1 + o, ax + dx / l * sep);
      StoreF(base, x1 + o + 8, az + dz / l * sep);
    }
  }
  const Juego* hecho_ = nullptr;
  int sondas_ = 0;
  bool fin_ = false;
  float acum_ = 0;
  ps2::Tex objetivo_, ready_, start_, hit_, miss_, clear_, failed_, aro_, relleno_, barra_, barra06_;
  ps2::Tex banda_, aro4_, marco4_, verde4_, sil_[2], caras_;   // s9: HUD de IW (DATA_USA 4/0)
  ps2::Tex rot_[3], cif_c_, cif_n_, earned_, total_, zeni_;     // s9: panel de resultado de IW
  ps2::Tex titulo_, cif_e_;                                     // s1 F04.3
  ps2::Tex panel_fill_, row_arrow_, row_band_;    // DS72: relleno/triangulo/banda nativos
  ps2::Tex row_labels_, row_ranks_;               // DS156: rotulo RANK y letras de rango (334/1 t17/t24)
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

bool Empezar(int clave, int fila, int dificultad) {
  const Modulo2* m = nullptr;
  for (const Modulo2& x : kModulos)
    if (x.clave == clave) m = &x;
  if (!m) return false;
  auto j = std::make_shared<Juego>();
  j->m = m;
  j->clave = clave;
  j->fila = fila;
  j->dif = std::clamp(dificultad, 0, 5);
  j->vel = m->velocidad[j->dif];
  j->poses = m->poses;
  g_poses_a = m->poses;
  g_cb_amc_entrada = m->poses->amc_entrada;
  g_cb_clip = -1;
  SeriePorClip(false);
  if (m->poses->cam_ini >= 0 && !std::getenv("DBZ3_IWMINI_CAMARA_B3")) {   // DBZ3_IWMINI_CAMARA_B3: la camara propia
    const Poses& q = *m->poses;
    PrecargarClips({q.cam_ini, q.cam_juego, q.cam_gana, q.cam_pierde, q.cam_fin_bien, q.cam_fin_mal,
                    m->poses == &kPosesNB006 ? 0x321 : -1,   // NB006 abre con el clip 0x321 (Mg_UpdateIntroSequence)
                    m->poses == &kPosesNB006 ? 0x98 : -1,    // NB006 punto ganado (Mg_ResetRoundState)
                    m->poses == &kPosesNB013 ? 0x188 : -1,   // NB013 choque (Mg_StartClashSequence)
                    q.cam_presenta});                        // s8: la presentacion (0x47E / 0x4B0 / 0x321)
    j->ent = PrecargarEntrada(q.amc_entrada, q.n_entrada);  // s8: entrada de la escena (DATA_CMN 876 / 877)
  }
  g_origen_ok = false;   // s8: el origen comun se toma al empezar el combate (PasoBotones)
  const char* t = std::getenv("DBZ3_TEST_IWMINI");
  j->auto_ = t && std::strstr(t, "auto");
  j->fallos = t && std::strstr(t, "fallos");
  // R7: NB006 pausa entre puntos con el clip 0x98 (DBZ3_IWMINI_NB006_SIN_PUNTO=1 lo desactiva)
  j->punto = m->poses == &kPosesNB006 && !std::getenv("DBZ3_IWMINI_NB006_SIN_PUNTO");
  std::string error;
  if (!Cargar(*j, error)) {
    REXLOG_WARN("dbz3 r14: {}: {}", m->nombre, error);
    return false;
  }
  j->c[0].cuenta = j->c[0].hueco[0];
  j->c[1].cuenta = j->c[1].hueco[0];
  REXLOG_INFO("dbz3 r14: {} (clave 0x{:X}, dificultad {}): carril 0 empieza {} {} {}, carril 1 {} {} {}; velocidad {}",
              m->nombre, clave, j->dif, j->c[0].seq[0], j->c[0].seq[1], j->c[0].seq[2], j->c[1].seq[0], j->c[1].seq[1],
              j->c[1].seq[2], j->vel);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_j = j;
  }
  dbz3::launcher::DeferUi(&CreatePanel);
  const int escenario = std::max(P::EscenarioPorMapa(P::MapaIW(m->escenario_iw)), 0);
  if (!dbz3::hub::RequestBattleEx(m->jugador, m->rival, escenario, true, -1, 0, 0, m->forma_jugador, m->forma_rival)) {
    REXLOG_WARN("dbz3 r14: el conector no acepto el combate");
    return false;
  }
  g_fondo_off = (m->poses == &kPosesNB006);   // NB006: el original sale sobre negro
  dbz3::combate::SetMuneco(1);       // el rival quieto, sin IA (le pegan los aciertos del minijuego)
  dbz3::combate::OcultarHud(true);   // sin marcador de B3
  g_corre = true;
  P::Reglas r;
  r.capsulas = false;
  // NB013 (Vegeta contra Trunks en la Sala del Tiempo). s8, medido en el original (PCSX2, captura iw_nb013_c): Vegeta
  // con la armadura saiyan (traje 1 de B3) y Trunks con su chaqueta morada y la espada (el traje de serie: 0; el 2 de
  // B3 es la armadura, que NO lleva en IW). DBZ3_R14_TRAJE=j,r lo cambia para probar
  if (m->poses == &kPosesNB013) {
    r.traje_jugador = 1, r.traje_rival = 0;
    if (const char* e = std::getenv("DBZ3_R14_TRAJE")) std::sscanf(e, "%d,%d", &r.traje_jugador, &r.traje_rival);
  }
  P::VigilarCombate("r14", r, [j](const P::Fin&) {
    g_corre = false;
    g_fondo_off = false;
    if (j->poses == &kPosesNB006) dbz3::escenarios::OcultarFondo(false);
    g_cb_clip = -1;
    if (j->auto_) dbz3::net::InyectarPad(0, -1);
    dbz3::combate::SetMuneco(-1);
    dbz3::combate::OcultarHud(false);
    Resultado res;
    res.clave = j->clave;
    res.fila = j->fila;
    // EU 0x28AA40 GameState_GetConfigBlock
    // EU 0x28A9E0 GameState_CopyConfigBlock
    res.superado = j->fase == 3 && j->rango > 0;
    res.centesimas = j->cuadros * 100 / 60;
    res.puntos = j->fase == 3 ? j->puntos : 0;
    res.score = j->fase == 3 ? j->nota : 0;   // TOTAL SCORE del panel (0..100), separado del premio
    res.zeni = j->fase == 3 ? j->zeni : 0;
    res.rango = j->rango > 0 ? j->rango - 1 : -1;   // IW 1..5 -> 0 C .. 4 Z; sin superar = sin rango (DS156)
    res.monedas = j->c[0].aciertos + j->c[1].aciertos;
    res.dificultad = j->dif;
    // DS153: los tres valores del bloque (EU 0x32F0F0 QteMg_FinalizeResult: +0x4C/+0x50/+0x54) para el panel de
    // OFFENCE/DEFENCE/TOTAL SCORE (el modulo los pinta en vivo; esto queda en el resultado por si lo usa el mapa).
    auto pct = [](const Carril& c) { return c.llegaron > 0 ? c.aciertos * 100 / c.llegaron : 0; };
    res.offence = pct(j->c[0]), res.defence = pct(j->c[1]);
    res.con_tiempo = false;   // el panel de dos carriles no lleva TIME
    REXLOG_INFO("dbz3 r14: resultado {} superado {} rango {} score {} zeni {}", j->m->nombre, res.superado, res.rango,
                res.score, res.zeni);
    Terminar(res);
  });
  return true;
}

// El resto de profundidad 1 de esta familia (motor de UI/poses/QTE de la PS2) esta en
// MAPAS_CODIGO/SUSTITUIDAS_IW_MINIJUEGOS.txt, con el archivo:funcion del port que lo cubre.
// ---- Familia B, "10 rondas" (11 MSN_NB012; la lista 1-10 de la captura 88) ----
// RE (decomp EU, nombres Mg_*; USA = EU - 0x7B0 en los datos):
//  - 10 rondas: orden barajado de 20 filas (Mg_ShuffleRoundOrder: 5 de las filas 0-9 y 5 de las 10-19). Cada fila son
//    10 botones (tabla S + d*0xD0 + fila*10) y 10 separaciones en cuadros (tabla H + d*0x320 + fila*0x28).
//  - Botones por ronda +0xBC y en la ultima +0xC0 (Mg_SetHitThresholdsByLevel); velocidad +0x104 px por cuadro
//    (Mg_InitParamsByLevel). Un solo carril en y 395 (Mg_UpdateScrollingIcons); el icono sale al acabar la cuenta y
//    la cuenta carga la separacion de ese icono; se quita en W + 56.
//  - Juicio por solape con el objetivo (Mg_JudgeIconOverlap): dentro del todo 0, lo que sobresale < 25 % del icono 1,
//    < 50 % 2, si no 3. Al pulsar el boton bueno con el icono a la derecha de objetivo - 80 (Mg_UpdateQteInput) la ronda
//    suma +0x108[juicio] / botones de la ronda (100, 70, 50; 3 = 0; division entera) (Mg_ApplyQteJudgeScore); el boton
//    malo marca el icono como fallado.
//  - Total = suma de las 10 rondas (Mg_ComputeTotalAndRank, umbrales +0xCC..+0xF4): 901-1000 -> 5, 801-900 -> 4,
//    751-800 -> 3, 601-750 -> 2, 301-600 -> 1, si no 0. Puntos (Mg_FinishAndWriteResults) = tabla[rango] x
//    multiplicador[d] redondeado a la centena; rango 0 -> 100.
//  - Personaje: el del mapa si es uno de 7, 12, 10, 13 (tabla 0x47F310 USA; Mg_ValidateCharSettings), si no 7 (Vegeta);
//    escenario de IW 4 (ROCK), 12 con 10 o 13 (Mg_InitStageByChar). Un solo luchador: el otro es el pivote invisible.
// NB007 (6, Goku hacia Namek) es la otra variante ("teclear", MiniGame_* del decomp EU 0x2928C0-0x295C10): la fila de
// N botones esta quieta y se teclea en orden lo antes posible (MiniGame_CheckInputStep: el boton malo no avanza); el
// tiempo de la QTE de cada ronda (QTE_StartTimer / QTE_GetTimer, segundos) va a la lista. Rango por la suma de los 10
// tiempos (MiniGame_CalcRank, umbrales de s_MSN_NB007_Update): <= 25 -> 5, <= 35 -> 4, <= 40 -> 3, <= 45 -> 2,
// <= 55 -> 1, si no 0; y 0 si las 10 rondas pasan de 7 s (+0xF8). Botones por ronda {3,5,6,8,9,9} y la ultima
// {4,6,7,9,10,10} (Misc_SetParamTripleByType). Goku solo (MiniGame_AllocState pone +0x10 = 0).
// MSN019 (18, Videl) es la tercera ("barrido", decomp EU 0x2E92A0-0x2EC8B0, todo constantes de Minigame_InitConfigTable):
// 10 filas; en cada una N objetos (4 5 7 8 9 10 por dificultad) en huecos de una barra centrada (ancho 0,5 .. 0,9 de la
// pantalla, con algo de azar: Results_RespawnItem) con un boton al azar de la mascara de la dificultad (F000 botones,
// FC00 + L1 R1, FF00 + L2 R2; Results_ItemTypeToMask). Un cursor rebota de punta a punta (Bounce, 2,0 .. 5,2 px por
// cuadro) y hay que pulsar el boton de cada objeto mientras lo pisa. La fila acaba al cogerlos todos o al pasar de 7
// rebotes; nota por rebotes (Results_StateEvaluate): <= 2 -> 10, <= 4 -> 5, <= 7 -> 3, si no 0. Total de las 10 filas
// (max. 100) -> rango por 60/70/80/90/100 (GetRankIndexByThresholds) y premio {6500 .. 7700} x multiplicador.
enum Tipo { kDesplazar, kTeclear, kBarrido };
// R14 familia B: las poses de IW del luchador (maquinas de estados MgPly / MgPlyB del ELF: MgPly_SetPoseState 0x311DF0 y
// MgPlyB_ChangeState 0x309940 en NB012 y NB007; MgPly_ChangeState 0x308BD0 en MSN019). Los tres comparten el esquema:
// 0 intro (READY), 3 espera en bucle, 4 acierto, 5-6-7 fallo en cadena (se cae, se queda, se levanta), 8 final bueno, 1 y 2
// resultado (NB012: Mg_FinishAndWriteResults; NB007: MiniGame_ShowResult). MSN019: estados 0/2/3 = espera por tramo de
// filas (0-2, 3-5, 6-9; Results_TriggerReactionAnim), 1 = fila completa, 6 = fila fallada, 4 = intro, 5 = final malo, 7 = bueno.
// Los codigos salen del ELF (los `sw ..., 0xB60` de cada Enter); las de pose compartida de NB007 son de 915.
struct PosesB {
  int cmn;
  uint16_t intro, espera[3], entra[3], golpe[3], mal[3], gana, pierde, fin[2];
  int dur_golpe, dur_mal[2];   // cuadros (de las propias animaciones)
  // Camaras de IW (UiAnim_Start = reproducir un clip del #AMC de DATA_CMN 915, hijo 2): -1 = la de B3. Estatica al empezar,
  // la del READY (dur_intro cuadros), una por ronda (2 cuadros: un plano fijo; ronda r >= 1 usa cam_ronda[r - 1]) y las
  // del final (el ultimo clip se queda en su ultimo cuadro, como el juego). cam_juego: la del juego (fase 2); -1 = la que
  // hubiera (MSN019 la saca del #AMC de DATA_CMN 879, no de 915: id kCam879).
  int cam_ini, cam_intro, cam_ronda[9], cam_gana, cam_pierde, dur_intro, cam_juego;
};
constexpr PosesB kPosesNB012 = {931, 0x500, {0x4B0, 0x4B0, 0x4B0}, {0, 0, 0}, {0x4BA, 0x4BA, 0x4BA}, {0x4B1, 0x4B2, 0x4B3},
                                0x4C4, 0x4B1, {0x501, 0x502}, 198, {25, 39},
                                // Mg_UpdateGameIntro 0x14A / 0x44C (Vegeta; 0x44F Ten, 0x452 Krilin, 0x455 Yamcha), Mg_StartNextRound
                                // (tabla 0x47E290 EU), Mg_CheckLastRoundEnd 0x154, Mg_StartResultAnimByChar 0x44E
                                0x14A, 0x44C, {0x14B, 0x14C, 0x14D, 0x14E, 0x14F, 0x150, 0x151, 0x152, 0x153}, 0x154, 0x44E, 180, -1};
constexpr PosesB kPosesNB007 = {922, 0x30C, {0x2BC, 0x2BC, 0x2BC}, {0, 0, 0}, {0x2C6, 0x2C6, 0x2C6}, {0x2BD, 0x2BE, 0x2BF},
                                0x2D0, 0x2BD, {0x30D, 0x30E}, 130, {25, 40},
                                // MiniGame_UpdateCountdown 0xB4 / 0x352, MiniGame_StartNextRound (tabla USA 0x472F00), 0xBE, ShowResult 0x354
                                0xB4, 0x352, {0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD}, 0xBE, 0x354, 180, -1};
constexpr PosesB kPosesMSN019 = {930, 0x7BC, {0x76C, 0x76E, 0x770}, {0, 0x76D, 0x76F}, {0x776, 0x777, 0x778}, {0x780, 0, 0},
                                 0x7BC, 0x7BC, {0x7BC, 0x7BC}, 45, {0, 0},   // el final es el estado 4 (Results_BeginOutcome / FinishOutcome)
                                 -1, -1, {-1, -1, -1, -1, -1, -1, -1, -1, -1}, 0x5AA, 0x5AA, 60, kCam879};
// MSN019: el clip de resultado de Videl (Results_StateFinishOutcome: tabla USA 0x497DA0 = 0x5AA..0x5AF, el de Videl el primero;
// plano fijo alrededor de ella en el origen). Camara de juego: el #AMC de DATA_CMN 879 (clip 0, 300 cuadros) que carga
// w_Camera_LoadMotionFile (motion 0x36F) y monta Minigame_SetupPlayerModels. El ancla +0x840 sale de #ITM +0x20 (punto 0,
// la salida del jugador) y se copia a la camara (+0x90) y a la escena (+0x1620/0x1630): IW la ancla a la SALIDA, no al
// luchador; en el barrido Videl no se mueve, asi que anclarla al luchador da lo mismo (PasoCamaraB).
struct ModuloB {
  int indice, clave;
  const char* nombre;
  int tipo;
  bool teclear;
  uint32_t seq, huecos, puntos;
  float velocidad[6];
  int por_ronda[6], ultima[6];
  int jugador, escenario_iw;
  const PosesB* poses;
};
// EU 0x32EB30 QteMg_SetSpeedByLevel
// EU 0x350C40 Mg_SetupRoundTargetsByLevel
// EU 0x350EC0 Mg_SetupRoundTimingByLevel
// EU 0x350F70 Mg_SetupRoundSpeedByLevel
// EU 0x376790 Mg_InitParamTable
// EU 0x3768F0 Mg_SetupRoundParams
// EU 0x376C30 Mg_SetLevelTimerValue
// EU 0x376CE0 Mg_SetLevelScaleValue
constexpr ModuloB kModulosB[] = {
    // Escenario 4 (ROCK): Mg_InitStageByChar (EU 0x3169E0) escribe +0x7C6 = 12 si el personaje es 13 o 10 (Ten/Krilin)
    // y 4 en cualquier otro caso; NB012 juega con Vegeta (7) en el original -> 4. No era una suposicion.
    {11, 0x7DA, "MSN_NB012", kDesplazar, false, 0x47DB10, 0x47DFF0, 0x47F2D0, {1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f},
     {3, 4, 5, 6, 7, 9}, {4, 5, 6, 7, 8, 10}, 7, 4, &kPosesNB012},
    // RE/PINE (2026-10-25): la escena de IW es el interior de la nave (DATA_CMN 880). Con el mod Campo IW 880
    // instalado se usa esa alta (log: "escenario 31"); sin el, cae a MapaIW(0). Escenario del modulo = 0 (BUDO).
    // EU 0x295C10 s_MSN_NB007_Update
    {6, 0x7D5, "MSN_NB007", kTeclear, true, 0x472F30, 0, 0x473680, {0, 0, 0, 0, 0, 0}, {3, 5, 6, 8, 9, 9}, {4, 6, 7, 9, 10, 10}, 0,
     0, &kPosesNB007},
    // RE PCSX2/PINE EU (SLES-55347): w_Stage_BeginLoad pasa el ID IW 5 (PLANE; global 0x4DB280). La misma ruta va a
    // MapaIW(5) -> mapa 62 de B3; captura del original con las lomas y molinos cotejada con la escena del port. IW no trae
    // cara de Videl en 6/0: va la nativa de B3 (PintarCara).
    // EU 0x2EC440 s_MSN019_UPDATE: la entrada del minijuego 18 pone el trabajo, arranca el clip 0 de la UI
    // (UiAnim_Start(0,1,0,1,0)), escribe +0x30 = 2, limpia el flag de estado (GameState_SetFlag2(0)) y no toca el
    // mando. Aqui la entrada es la fase 0 (camara de entrada / cam_ini y la espera de la pose); el clip 0 de la UI y el
    // campo +0x30 no estan identificados en los datos del jugador: `[H]`. El juego en si es kBarrido.
    {18, 0x7E1, "MSN019", kBarrido, false, 0, 0, 0, {2.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.2f}, {4, 5, 7, 8, 9, 10},
     {4, 5, 7, 8, 9, 10}, 15, 5, &kPosesMSN019},
};
constexpr uint16_t kMascaraBarrido[6] = {0xF000, 0xF000, 0xF000, 0xFC00, 0xFC00, 0xFF00};
constexpr float kAnchoBarrido[6] = {0.5f, 0.6f, 0.75f, 0.8f, 0.85f, 0.9f};
constexpr int kTipoBoton[8] = {15, 12, 13, 14, 10, 11, 8, 9};   // tipo de objeto -> codigo de boton (0x8000 = 1 << 15...)
constexpr float kUmbralT[6] = {1e9f, 55.0f, 45.0f, 40.0f, 35.0f, 25.0f};   // NB007: total de segundos <= umbral
// EU 0x295C10 s_MSN_NB007_Update: +0xF8 = 0x40E00000 (7,0 s); es el limite de la QTE de cada ronda
// (QTE_SetTimeLimit 0x2EE700 lo copia a +0xfc) y el corte de "ronda lenta" de MiniGame_CalcRank.
constexpr float kLimiteTeclear = 7.0f;
// Objetivo de la familia B (un carril): el objeto 0x20 de la UI de Mg_UI_CreateObjects (EU 0x316060),
// UI_SetObjectPosVec(pantalla/2, 395, 0, 0x20) con UI_SetObjectRect(-28, -28, 28, 28, 0x20): centro x 320 y caja de 56.
// El juego lo juzga por solape (UI_CheckTwoObjectsOverlap(0x20, icono) en Mg_UpdateScrollingIcons EU 0x315540) y da el
// fallo al pasar de pantalla/2 + 150 (Mg_UpdateQteInput EU 0x314060).
constexpr float kYB = 395, kObjB = kW / 2;
constexpr float kObjAncho = 56;
constexpr int kJuicioPts[3] = {100, 70, 50};       // +0x108, +0x10C, +0x110
constexpr int kUmbralB[6] = {0, 301, 601, 751, 801, 901};

// Azar del juego (Memory() de IW) para barajar las rondas y colocar los objetos: semilla del reloj.
int Azar() {
  static std::mt19937 g(uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()));
  return int(g() & 0x7FFFFFFF);
}

struct JuegoB {
  const ModuloB* m = nullptr;
  const PosesB* pb = nullptr;
  int clave = 0, fila = 0, dif = 2;
  uint8_t seq[20][10] = {};
  int hueco[20][10] = {};
  int orden[10] = {};
  float pts[6] = {}, mult[6] = {};
  float vel = 2;
  int ronda = 0, n = 0, sale = 0, cuenta = 0, actual = -1;
  Icono ic[10];
  bool pulsado[10] = {};
  float nota[10] = {};
  // DS153: suma por juicio (EU 0x313100 Mg_ApplyQteJudgeScore: +0x48/+0x4C/+0x50 += puntos[juicio]/botones de la ronda;
  // Mg_FinishAndWriteResults 0x312D90 las vuelca como GREAT/GOOD/NORMAL). Solo las usa NB012 (0x7DA).
  float cat[3] = {};
  int fase = 0, t = 0, cuadros = 0, rango = 0, puntos = 100, total = 0;
  int zeni = 0, saldo = 0;   // premio ZENI y monedero tras sumarlo; `total` = TOTAL SCORE (Results_ComputeTotalsAndRewards)
  int juicio = -1, juicio_t = 0;
  // EU 0x312280 Mg_BuildShuffledCharIdTable (NB012) y MiniGame_UpdateCountdown 0x294D60 (NB007, tabla 0x473700):
  // el original BAR AJA los 9 clips de camara de ronda (9 IDs consecutivos desde el del personaje: 0x14B Vegeta,
  // 0x155 Yamcha, 0x15F Trunks, 0x169 Krilin) con 8 intercambios `i <-> (Memory() & 7)`; no salen en orden. Aqui el
  // mismo barajado sobre `cam_r`; MSN019 no tiene camara por ronda (cam_ronda = -1) y se queda igual.
  int cam_r[9] = {};
  bool auto_ = false;
  uint16_t antes = 0, pendiente = 0;
  int32_t inyecta = 0;
  int inyecta_t = 0;
  float cursor = 0, sentido = 1;   // barrido: x del cursor y hacia donde va
  int rebotes = 0;
  float b0 = 0, b1 = 0;             // barrido: extremos de la barra
  P::Imagen objetivo, ready, start, clear, failed, aro;
  P::Imagen banda, caja_fila, cifras;   // s9: DATA_USA 4/0 t38 banda, t16 caja de fila, t06 cifras blancas (5x4 de 25,6x22)
  P::Imagen palabra[4];   // DATA_USA 4/0: t15 GREAT!, t08 GOOD!, t09 NORMAL, t10 BAD (juicio 0..3)
  bool fallos = false, mal_hecho = false;   // prueba: el piloto falla una vez en la ronda 2 (poses del fallo)
  int pose_t = 0, cola_t = 0, cola_k = 0, nivel = -1, variante = 0;   // poses de IW (PosesB): cuadros, cadena del fallo, tramo
};
std::shared_ptr<JuegoB> g_jb;
std::atomic<bool> g_corre_b{false};
// s8: NB007 en IW (PCSX2, logs\nb007_fin_s8.tsv): entrada = #AMC de DATA_CMN 872 (1 clip, 300 cuadros, la camara recorre la
// nave mirando a su centro (55, y, 0)) -> 0x352 (READY, 180) -> 0xB4 en la primera ronda -> un plano fijo por ronda ->
// 0x354 (240) al fallar. Cuadros de la entrada (0 = sin ella: el plano 0xB4 quieto 30 cuadros, como antes).
std::atomic<int> g_entrada_b{0};

// ---- poses de IW de la familia B ----
uint32_t AliasB(const PosesB& p, uint32_t codigo) {
  const uint32_t cmn = codigo >= 0x2BC && codigo <= 0x2D0 ? 915u : uint32_t(p.cmn);   // las compartidas de NB007 son de 915
  return codigo ? dbz3::anim::CodigoQte(cmn, codigo) : 0;
}
// Una pose una vez y luego `luego` en bucle; `bucle`: solo en bucle.
void PonerB(const PosesB& p, uint32_t codigo, bool bucle, uint32_t luego = 0) {
  const uint32_t x = Luchador(0), a = AliasB(p, codigo);
  if (!x || !a) return;
  if (bucle) dbz3::anim::Bucle(x, a);
  else dbz3::anim::Reproducir(x, a, false, 0, AliasB(p, luego));
}
enum EventoB { kEvIntro, kEvNivel, kEvGolpe, kEvMal, kEvFin };
void EventoPose(JuegoB& j, int ev) {
  if (!j.pb) return;
  const PosesB& p = *j.pb;
  const int n = j.m->tipo == kBarrido ? (j.ronda < 3 ? 0 : j.ronda < 6 ? 1 : 2) : 0;
  switch (ev) {
    case kEvIntro:   // READY: la pose 0 y luego la espera
      PonerB(p, p.intro, false, p.espera[0]);
      j.pose_t = 90;
      break;
    case kEvNivel:   // MSN019: sube el tramo de filas (Results_TriggerReactionAnim: filas 0, 3 y 6)
      if (n == j.nivel) break;
      j.nivel = n;
      if (p.entra[n]) PonerB(p, p.entra[n], false, p.espera[n]);
      else PonerB(p, p.espera[n], true);
      break;
    case kEvGolpe:   // acierto (la fila entera en MSN019); mientras dura la pose no se repite
      if (j.pose_t > 0) break;
      PonerB(p, p.golpe[j.variante++ % 3], false, p.espera[n]);
      j.pose_t = p.dur_golpe;
      if (j.m->tipo != kBarrido) SeGolpe(0, true);   // NB012 y NB007 pelean (MSN019 vuela: sin golpes)
      break;
    case kEvMal:     // fallo: cae (5), se queda (6) y se levanta (7); luego la espera
      PonerB(p, p.mal[0], false, p.espera[n]);
      if (j.m->tipo != kBarrido) SeGolpe(1, true);   // el fallo: le dan
      j.pose_t = p.dur_mal[0] + p.dur_mal[1] + 40;
      j.cola_k = p.mal[1] ? 1 : 0;
      j.cola_t = j.cola_k ? p.dur_mal[0] : 0;
      break;
    case kEvFin:     // resultado: el final (8) y luego el 1 o el 2 en bucle
      j.cola_t = j.cola_k = 0;
      PonerB(p, j.rango > 0 ? p.gana : p.pierde, false, p.fin[j.rango > 0 ? 0 : 1]);
      break;
  }
}
void PrecargarCamaras(const PosesB& p) {
  std::vector<int> ids = {p.cam_ini, p.cam_intro, p.cam_gana, p.cam_pierde, p.cam_juego};
  ids.insert(ids.end(), std::begin(p.cam_ronda), std::end(p.cam_ronda));
  PrecargarClips(ids);
}
// Un cuadro de 60 Hz: la cadena del fallo y el temporizador de la pose.
void CuadroPose(JuegoB& j) {
  CuadroSe();
  if (g_cb_clip.load() >= 0 && !g_cb_quieto.load()) ++g_cb_frame;
  if (!j.pb) return;
  if (j.pose_t > 0) --j.pose_t;
  if (j.cola_t > 0 && --j.cola_t == 0 && j.cola_k) {
    const PosesB& p = *j.pb;
    const int n = j.m->tipo == kBarrido ? (j.ronda < 3 ? 0 : j.ronda < 6 ? 1 : 2) : 0;
    PonerB(p, p.mal[j.cola_k], false, p.espera[n]);
    if (j.cola_k == 1) {
      j.cola_k = 2;
      j.cola_t = p.dur_mal[1];
    } else {
      j.cola_k = 0;
    }
  }
}

// EU 0x37D150 RAM_OpenDataCMNMinigameAnims: abre los #AMM/#BSK de animacion de los minijuegos desde DATA_CMN (por
// eso los codigos de pose salen de ahi). En el port los carga anim_iw (CodigoQte) a partir del #BSK de cada personaje:
// `motor`.
bool CargarB(JuegoB& j, std::string& error) {
  const ModuloB& m0 = *j.m;
  if (m0.tipo == kBarrido) {   // todo constantes del codigo; los pts del premio tambien
    constexpr float kPremio[6] = {100, 6500, 6800, 7100, 7400, 7700}, kMult[6] = {0.6f, 0.8f, 1.0f, 1.3f, 1.6f, 2.0f};
    for (int k = 0; k < 6; ++k) j.pts[k] = kPremio[k], j.mult[k] = kMult[k];
  }
  ElfIW elf;
  if (!AbrirElfIW(elf, error)) return false;
  auto off = [&](uint32_t a) { return elf.Off(a); };
  const ModuloB& m = *j.m;
  for (int r = 0; r < 20 && m.tipo != kBarrido; ++r) {
    const uint8_t* s = off(m.seq + uint32_t(j.dif) * 0xD0 + uint32_t(r) * 10);
    const uint8_t* h = m.teclear ? s : off(m.huecos + uint32_t(j.dif) * 0x320 + uint32_t(r) * 0x28);
    if (!s || !h) return false;
    for (int i = 0; i < 10; ++i) {
      j.seq[r][i] = s[i];
      // La tabla EU ya viene en cuadros (medido 2026-10-22 contra SLES_553.47: 1177/1200 coinciden con USA*10+12 y
      // los 23 retocados por el americano -122->9 en 15, 130->12 en 7, 220->19 en 1- se leen tal cual). No convertir.
      if (!m.teclear) std::memcpy(&j.hueco[r][i], h + 4 * i, 4);
      // NB007 lleva tambien la cruceta (4 arriba, 5 derecha, 6 abajo, 7 izquierda, desde la dificultad 1)
      if (s[i] < (m.teclear ? 4 : 8) || s[i] > 15 || j.hueco[r][i] < 0 || j.hueco[r][i] > 3000) {
        error = "las tablas del ELF no son las de IW USA";
        return false;
      }
    }
  }
  const uint8_t* p = m.tipo == kBarrido ? nullptr : off(m.puntos);
  if (!p && m.tipo != kBarrido) return false;
  for (int k = 0; p && k < 6; ++k) {
    int32_t v;
    std::memcpy(&v, p + 4 * k, 4);
    j.pts[k] = float(v);
    std::memcpy(&j.mult[k], p + 0x20 + 4 * k, 4);
  }
  P::AfsPs2 usa;
  if (!usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) {
    error = "sin DATA_USA.AFS";
    return false;
  }
  j.objetivo = P::TexturaAmt(P::HijoAmb(usa.Entrada(0), 1), 10);
  const std::vector<uint8_t> t51 = P::HijoAmb(usa.Entrada(5), 1), t40 = P::HijoAmb(usa.Entrada(4), 0);
  j.start = P::TexturaAmt(t51, 0);
  j.ready = P::TexturaAmt(t51, 8);
  j.clear = P::TexturaAmt(t51, 9);
  j.failed = P::TexturaAmt(t51, 10);
  constexpr int kPalabra[4] = {15, 8, 9, 10};
  for (int k = 0; k < 4; ++k) j.palabra[k] = P::TexturaAmt(t40, kPalabra[k]);
  if (P::CuantasAmt(t40) > 50) {
    j.banda = P::TexturaAmt(t40, 38);
    j.caja_fila = P::TexturaAmt(t40, 16);
    j.cifras = P::TexturaAmt(t40, 6);
  }
  const std::vector<uint8_t> hud = P::HijoAmb(usa.Entrada(6), 0);
  if (P::CuantasAmt(hud) > 38) {
    j.aro = P::TexturaAmt(hud, 0);
  }
  if (j.m->tipo == kTeclear) {
    // EU 0x292F80 MiniGame_ShuffleOrderTable: la tabla de NB007 son 20 filas 0..19; se barajan las 10 primeras y
    // las 10 siguientes (cada una con `i <-> Memory()%10`) y despues las filas 10..14 se copian sobre 5..9. Las
    // rondas leen las 10 primeras: 5 de la mitad 0..9 y 5 de la mitad 10..19. Solo cambia el generador: `Memory()`
    // (EU 0x127CA0, registro de desplazamiento con semilla del motor) no existe en el port; `Azar()` es un mt19937
    // propio. El reparto y la copia son los del original (estructura, DS68).
    uint8_t t[20];
    for (int i = 0; i < 20; ++i) t[i] = uint8_t(i);
    for (int i = 0; i < 10; ++i) std::swap(t[i], t[Azar() % 10]);
    for (int i = 0; i < 10; ++i) std::swap(t[10 + i], t[10 + Azar() % 10]);
    for (int i = 0; i < 5; ++i) t[5 + i] = t[10 + i];
    for (int i = 0; i < 10; ++i) j.orden[i] = t[i];
  } else {
    // Mg_ShuffleRoundOrder: 0-9 barajadas (rondas 1-5) y 10-19 barajadas (rondas 6-10)
    // EU 0x312900 Mg_ShuffleRoundOrder
    int a[10], b[10];
    for (int i = 0; i < 10; ++i) a[i] = i, b[i] = 10 + i;
    for (int i = 0; i < 10; ++i) {
      std::swap(a[i], a[Azar() % 10]);
      std::swap(b[i], b[Azar() % 10]);
    }
    for (int i = 0; i < 5; ++i) j.orden[i] = a[i], j.orden[5 + i] = b[i];
  }
  // Camaras de ronda: copia de las 9 del modulo y el barajado de 0x312280 / 0x294D60 (`i <-> (azar & 7)`, 9 veces).
  for (int i = 0; i < 9; ++i) j.cam_r[i] = j.pb ? j.pb->cam_ronda[i] : -1;
  if (j.pb && j.pb->cam_ronda[0] >= 0)
    for (int i = 0; i < 9; ++i) std::swap(j.cam_r[i], j.cam_r[Azar() & 7]);
  return true;
}

// EU 0x312760 Mg_SetupRoundQteSequence: lo mismo que SetPatternByType para el contexto +0x5d9460 (el indice de la
// fila sale de `[+0x54*4 + 0x5d9460]`), con las tablas 0x47E2C0..0x47E6D0 por tipo. En el port el indice de la fila es
// j.orden[ronda] y las tablas m.seq: `coincide`.
// EU 0x292DE0 MiniGame_SetPatternByType: elige el patron por TIPO (byte +0x14 del trabajo: la tabla de su minijuego) y
// fija la longitud de la secuencia de la ronda en +0xB8 = +0xBC (rondas normales) o +0xC0 (ultima, Minigame_IsLastRound_).
// Aqui el patron son las tablas m.seq del modulo (0x472F30 de NB007 / 0x47DB10 de NB012, leidas en CargarB) y la
// longitud es por_ronda[dificultad] / ultima[dificultad]: `coincide`.
void EmpezarRonda(JuegoB& j) {
  // EU 0x292DE0 MiniGame_SetPatternByType
  j.n = j.ronda == 9 ? j.m->ultima[j.dif] : j.m->por_ronda[j.dif];
  j.sale = 0;
  j.cuenta = 0;
  j.actual = -1;
  for (int i = 0; i < 10; ++i) {
    j.ic[i] = Icono{};
    j.pulsado[i] = false;
  }
  if (j.m->tipo != kBarrido) return;
  // Results_RespawnItem: hueco i de la barra (ancho/N) con un poco de azar (4 pasos, sin los 2 ultimos) y boton al azar
  // de la mascara de la dificultad
  const float medio = kAnchoBarrido[j.dif] * kW / 2, hueco = 2 * medio / j.n;
  j.b0 = kW / 2 - medio;
  j.b1 = kW / 2 + medio;
  for (int i = 0; i < j.n; ++i) {
    int tipo = 0;
    do tipo = Azar() % 8;
    while (!((1u << kTipoBoton[tipo]) & kMascaraBarrido[j.dif]));
    j.ic[i] = Icono{kTipoBoton[tipo], j.b0 + i * hueco + hueco / 4 * float(Azar() % 2), true};
  }
  j.sale = j.n;
  j.cursor = j.b0;
  j.sentido = 1;
  j.rebotes = 0;
}

// EU 0x315350 Mg_JudgeIconOverlap: compara la caja del icono con la del objetivo 0x20 y deja 0 si esta dentro, si no
// el porcentaje del borde que falta (25 %/50 % -> 1/2, mas -> 3). Es la funcion de este Juicio. Su auxiliar de
// solape 0x2F3770 Math_RectsOverlap se tradujo en src/iw_mini_extra.cpp y se usa como salida temprana (el resultado
// no cambia: fuera del todo ya daba 3).
// Juicio del icono k contra el objetivo (Mg_JudgeIconOverlap).
int Juicio(float x) {
  const float i0 = x, i1 = x + kIcono, o0 = kObjB - kObjAncho / 2, o1 = kObjB + kObjAncho / 2;
  const float ir[4] = {i0, 0, i1, 0}, objetivo[4] = {o0, 0, o1, 0};
  // EU 0x2F3770 Math_RectsOverlap (traduccion en src/iw_mini_extra.cpp): sin solape en X, el icono quedo fuera.
  if (!extra::MathRectsOverlap(ir, objetivo, 0)) return 3;
  if (i0 >= o0 && i1 <= o1) return 0;
  const int pct = int((i0 < o0 ? o0 - i0 : i1 - o1) / kIcono * 100);
  return pct < 25 ? 1 : pct < 50 ? 2 : 3;
}

// NB007: la fila quieta; se teclea en orden y cuenta el tiempo de la ronda.
void PasoTeclear(JuegoB& j, uint16_t pulsado) {
  const int fila = j.orden[j.ronda];
  if (j.sale == 0) {   // la fila entera de una vez (MiniGame_UpdateSlotIcons)
    for (int i = 0; i < j.n; ++i) j.ic[i] = Icono{j.seq[fila][i], 0, true};
    j.sale = j.n;
    j.actual = 0;
  }
  // EU 0x293AB0/0x294300 MiniGame_FinishRound: al cerrar la ronda guarda el tiempo de su QTE en la lista
  // (`+0x54` = ronda: `work+0x1c + ronda*4`) y en `UI_GetObjectDataPtr(0x16)`; si se agoto el tiempo pone el tiempo
  // limite (+0xf8) y pasa a la ronda siguiente (+0x54 < 9) o calcula el rango. Este `j.nota[]` es la lista de tiempos:
  // una casilla por ronda, como el array +0x1c del original (`Timer_SetSlot` EU 0x2EF9D0: `[base + 0x30 + slot*4]`).
  // EU 0x2EEFB0 QTE_Update
  // EU 0x293D10 MiniGame_UpdateTimer
  // EU 0x294300 MiniGame_FinishRound
  j.nota[j.ronda] += 1.0f / 60;   // QTE_Update: segundos
  // Limite de la QTE (7,0 s en NB007, +0xF8) y MiniGame_FinishRound (EU 0x294300): al agotarse la QTE la ronda se
  // cierra igual y a la lista va el limite, no el tiempo que se tardo. El original tiene ademas el setter
  // QTE_SetTimeLimit (EU 0x2EE700, copia el valor a +0xFC para QTE_IsTimeUp 0x2EE6D0); el port no tiene objeto QTE y
  // aplica el mismo limite con esta comparacion (estructura, DIFIERE de LUNA_IWMINI3_0; no se pone a ojo).
  if (j.nota[j.ronda] >= kLimiteTeclear) {
    j.nota[j.ronda] = kLimiteTeclear;
    j.actual = j.n;
  }
  uint16_t p = pulsado & 0xFFF0;
  if (j.auto_ && j.actual < j.n && j.inyecta_t == 0 && j.t % 12 == 6) {   // prueba: una pulsacion cada 12 cuadros
    int bt = j.ic[j.actual].boton;
    if (j.fallos && j.ronda == 1 && !j.mal_hecho) {   // prueba: un boton malo
      j.mal_hecho = true;
      bt = bt == 15 ? 14 : 15;
    }
    if (const int32_t x = XboxDe(bt)) {
      j.inyecta = x;
      j.inyecta_t = 4;
    } else {
      p = uint16_t(1u << bt);
    }
  }
  if (p && j.actual < j.n) {
    // EU 0x293EF0 MiniGame_CheckInputStep
    if (BotonPulsado(p) == j.ic[j.actual].boton) {
      j.ic[j.actual].resuelto = true;
      ++j.actual;
      j.juicio = -1;
      EventoPose(j, kEvGolpe);
    } else {
      j.juicio = 3;   // error: el boton malo no avanza
      j.juicio_t = 30;
      EventoPose(j, kEvMal);
    }
  }
  if (j.actual < j.n) return;
  // EU 0x294750 MiniGame_StartNextRound
  REXLOG_INFO("dbz3 r14: ronda {} (fila {}): {:.2f} s", j.ronda + 1, fila, j.nota[j.ronda]);
  if (++j.ronda < 10) {
    EmpezarRonda(j);
    j.fase = 1;
    j.t = 0;
    return;
  }
  float total = 0;
  int lentas = 0;
  for (float v : j.nota) {
    total += v;
    lentas += v >= 7.0f;
  }
  j.total = int(total * 100);   // centesimas
  j.rango = 0;
  if (lentas < 10)
    for (int r = 5; r >= 1; --r)
      // EU 0x293820 MiniGame_CalcRank
      if (total <= kUmbralT[r]) {
        j.rango = r;
        break;
      }
  const float v = j.rango == 0 ? 100.0f : j.pts[j.rango] * j.mult[j.dif];
  j.puntos = (int(v) + 99) / 100 * 100;
  j.zeni = j.puntos;
  j.saldo = P::LeerProgreso("iw_zeni", 0) + j.zeni;
  j.fase = 3;
  j.t = 0;
  REXLOG_INFO("dbz3 r14: fin: total {:.2f} s, rango {}, zeni {} (monedero {})", total, j.rango, j.zeni, j.saldo);
}

void PasoBarrido(JuegoB& j, uint16_t pulsado) {
  j.cursor += j.sentido * j.vel;   // Bounce_UpdateMove
  if (j.cursor >= j.b1 || j.cursor <= j.b0) {
    j.cursor = std::clamp(j.cursor, j.b0, j.b1);
    j.sentido = -j.sentido;
    ++j.rebotes;
  }
  int bajo = -1;   // el objeto que pisa el cursor
  for (int i = 0; i < j.n; ++i) {
    if (j.ic[i].vivo && !j.ic[i].resuelto && std::fabs(j.ic[i].x + kIcono / 2 - j.cursor) < kIcono / 2) bajo = i;
    else j.ic[i].pedido = false;   // prueba: si se le escapo, lo vuelve a pedir en la pasada siguiente
  }
  uint16_t p = pulsado & 0xFF00;
  if (j.auto_ && bajo >= 0 && !j.ic[bajo].pedido && !(j.fallos && j.ronda == 1)) {   // fallos: la fila 2 no se coge
    j.ic[bajo].pedido = true;
    if (const int32_t x = XboxDe(j.ic[bajo].boton)) {
      j.inyecta = x;
      j.inyecta_t = 4;
    } else {
      p = uint16_t(1u << j.ic[bajo].boton);
    }
  }
  if (p && bajo >= 0 && BotonPulsado(p) == j.ic[bajo].boton) {
    j.ic[bajo].resuelto = true;
    j.juicio = -1;
  }
  bool quedan = false;
  for (int i = 0; i < j.n; ++i) quedan |= !j.ic[i].resuelto;
  if (quedan && j.rebotes <= 7) return;
  const int juicio = j.rebotes <= 2 ? 0 : j.rebotes <= 4 ? 1 : j.rebotes <= 7 ? 2 : 3;
  constexpr int kPts[4] = {10, 5, 3, 0};
  j.nota[j.ronda] = float(kPts[juicio]);
  j.juicio = juicio;
  j.juicio_t = 40;
  EventoPose(j, quedan ? kEvMal : kEvGolpe);   // fila completa (estado 1) o fallada (estado 6)
  REXLOG_INFO("dbz3 r14: fila {}: {} rebotes, {} puntos", j.ronda + 1, j.rebotes, kPts[juicio]);
  if (++j.ronda < 10) {
    EmpezarRonda(j);
    j.fase = 1;
    j.t = 0;
    return;
  }
  j.total = 0;
  for (float v : j.nota) j.total += int(v);
  // EU 0x2E92F0 GetRankIndexByThresholds y EU 0x2E9AF0 Results_ComputeTotalsAndRewards (tablas 0x5D919A premio y
  // 0x5D91A4 multiplicador por dificultad): el mismo calculo que usa el original para esta pantalla.
  j.rango = RangoResultadoIW(j.total) + 1;   // -1 (sin rango) -> 0; 0..4 -> 1..5
  j.puntos = PremioResultadoIW(j.total, j.dif);
  j.zeni = j.puntos;
  j.saldo = P::LeerProgreso("iw_zeni", 0) + j.zeni;
  j.fase = 3;
  j.t = 0;
  REXLOG_INFO("dbz3 r14: fin: total {} rango {} zeni {} (monedero {})", j.total, j.rango, j.zeni, j.saldo);
}

void PasoB(JuegoB& j, uint16_t pulsado) {
  ++j.cuadros;
  if (j.juicio_t > 0) --j.juicio_t;
  if (j.m->tipo == kBarrido) {
    PasoBarrido(j, pulsado);
    return;
  }
  if (j.m->teclear) {
    PasoTeclear(j, pulsado);
    return;
  }
  const int fila = j.orden[j.ronda];
  if (--j.cuenta <= 0 && j.sale < j.n) {   // Mg_UpdateScrollingIcons
    j.ic[j.sale] = Icono{j.seq[fila][j.sale], -16 - j.vel, true};
    j.cuenta = j.hueco[fila][j.sale];
    ++j.sale;
  }
  bool quedan = false;
  float mejor = 1e9f;
  j.actual = -1;
  for (int i = 0; i < j.sale; ++i) {
    Icono& k = j.ic[i];
    if (!k.vivo) continue;
    k.x += j.vel;
    if (k.x >= kW + 56) {
      k.vivo = false;
      continue;
    }
    quedan = true;
    const float d = std::fabs(k.x + kIcono / 2 - kObjB);
    if (!k.resuelto && d < mejor) {   // el mas cercano al objetivo sin juzgar
      mejor = d;
      j.actual = i;
    }
  }
  uint16_t p = pulsado & 0xFF00;
  if (j.auto_ && j.actual >= 0 && !j.ic[j.actual].pedido && Juicio(j.ic[j.actual].x) == 0 &&
      j.ic[j.actual].x + kIcono / 2 >= kObjB - 2) {
    Icono& k = j.ic[j.actual];
    k.pedido = true;
    int bt = k.boton;
    if (j.fallos && j.ronda == 1 && !j.mal_hecho) {   // prueba: un boton malo
      j.mal_hecho = true;
      bt = bt == 15 ? 14 : 15;
    }
    if (const int32_t x = XboxDe(bt)) {
      j.inyecta = x;
      j.inyecta_t = 4;
    } else {
      p = uint16_t(1u << bt);
    }
  }
  if (p && j.actual >= 0 && j.ic[j.actual].x >= kObjB - kObjAncho / 2 - 80) {   // Mg_UpdateQteInput
    Icono& k = j.ic[j.actual];
    k.resuelto = true;
    if (BotonPulsado(p) == k.boton) {
      j.juicio = Juicio(k.x);
      // EU 0x312680 Mg_StoreRoundScore y EU 0x313100 Mg_ApplyQteJudgeScore: el golpe suma a la ronda y a su categoria
      // (+0x48/+0x4C/+0x50 = GREAT/GOOD/NORMAL; DS153).
      if (j.juicio < 3) {
        j.nota[j.ronda] += float(kJuicioPts[j.juicio] / j.n);
        j.cat[j.juicio] += float(kJuicioPts[j.juicio] / j.n);
      }
      k.vivo = false;
      EventoPose(j, kEvGolpe);
    } else {
      j.juicio = 3;   // boton malo: fallado
      EventoPose(j, kEvMal);
    }
    j.juicio_t = 30;
  }
  if (!quedan && j.sale >= j.n) {   // fin de la ronda (Mg_StoreRoundScore)
    REXLOG_INFO("dbz3 r14: ronda {} (fila {}): {}", j.ronda + 1, fila, int(j.nota[j.ronda]));
    if (++j.ronda < 10) {
      EmpezarRonda(j);
      j.fase = 1;   // presentacion de la ronda siguiente
      j.t = 0;
      return;
    }
    // EU 0x312D90 Mg_FinishAndWriteResults
    // EU 0x313500 Mg_ComputeTotalAndRank
    j.total = 0;
    for (float v : j.nota) j.total += int(v);
    j.rango = 0;
    for (int r = 5; r >= 1; --r)
      if (j.total >= kUmbralB[r]) {
        j.rango = r;
        break;
      }
    // EU 0x32F0F0 QteMg_FinalizeResult
    const float v = j.rango == 0 ? 100.0f : j.pts[j.rango] * j.mult[j.dif];
    j.puntos = (int(v) + 99) / 100 * 100;
    j.zeni = j.puntos;
    j.saldo = P::LeerProgreso("iw_zeni", 0) + j.zeni;
    j.fase = 3;
    j.t = 0;
    REXLOG_INFO("dbz3 r14: fin: total {} rango {} zeni {} (monedero {})", j.total, j.rango, j.zeni, j.saldo);
  }
}

// Flecha de la cruceta (codigo 4 arriba, 5 derecha, 6 abajo, 7 izquierda): la de la izquierda de "flechas" (5,8-37,42)
// girada. Sin la pieza, un triangulo.
void Flecha(ImDrawList* dl, int boton, ImVec2 c, float r, ImU32 col) {
  constexpr float kAng[4] = {1.5707963f, 3.1415927f, -1.5707963f, 0.0f};   // apuntando a la izquierda = 0
  const float a = kAng[(boton - 4) & 3], ca = std::cos(a), sa = std::sin(a);
  auto rot = [&](float x, float y) { return ImVec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca); };
  const S::Pic p = S::Img("flechas");
  if (!p) {
    dl->AddTriangleFilled(rot(-r, 0), rot(r, -r), rot(r, r), col);
    return;
  }
  const ImVec2 uv0(5 / p.w, 8 / p.h), uv1(37 / p.w, 42 / p.h);
  dl->AddImageQuad(p.tex, rot(-r, -r), rot(r, -r), rot(r, r), rot(-r, r), uv0, ImVec2(uv1.x, uv0.y), uv1,
                   ImVec2(uv0.x, uv1.y), col);
}

void PiezaB(ImDrawList* dl, const ps2::Tex& t, ImVec2 c, float k) {
  if (t) dl->AddImage(t.id(), ImVec2(c.x - t.w * k / 2, c.y - t.h * k / 2), ImVec2(c.x + t.w * k / 2, c.y + t.h * k / 2));
}

// EU 0x3136B0 Mg_UpdateEndPhase (NB012): la fase final: a los 60 cuadros saca el READY, y a los 180 (o con
// Camera_IsTimerElapsed) llama a Mg_FinishAndWriteResults; despues solo queda QTE_Update y los premios cuando el combate
// esta en 3/4. Son los mismos 60/180 de CheckReadyStart/UpdateMain: `coincide` (fases 1 y 3 de PanelB).
// EU 0x293AB0 MiniGame_UpdateMain / 0x293D10 MiniGame_UpdateTimer / 0x294990 MiniGame_UpdateIntroPhase /
// 0x295030 MiniGame_UpdateDispatch: la maquina de estados del minijuego de rondas. Dispatch reparte por +0x0/+0x4
// (0 = MiniGame_UpdateCountdown, 1 = rondas: 0 intro -> 1 input -> 2 reloj -> 3 espera, 2 = UpdateMain) y
// UpdateIntroPhase lleva la intro con tiempos de 30.0 (0x41f00000), 50.0 (0x42480000) y 1.0 (0x3f800000) por fase,
// llamando a MiniGame_StartNextRound. UpdateTimer resta el reloj y, con +0x8 <= 0 y el combate en los estados 3/4/8, lo
// vuelve a 30.0 (no termina el minijuego). UpdateMain, con +0xc == 1, da el resultado a los 180.0 cuadros (o con
// Camera_IsTimerElapsed) y aplica premios cuando el combate esta en 3/4.
// En el port esta maquina es la de PanelB (fase 0 espera, 1 READY, 2 juego, 3 fin) y la de PasoTeclear/PasoBarrido (la
// ronda acaba al gastar la secuencia o al llegar a los 10, y el reloj por ronda es el de la QTE: 7.0 s, +0xF8 de
// s_MSN_NB007_Update 0x295C10). Coincide en estados y tiempos; los 180 cuadros de UpdateMain son del minijuego de un
// solo reto (el de 10 rondas no los usa): `coincide`.
class PanelB : public rex::ui::ImGuiDialog {
 public:
  explicit PanelB(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!g_corre_b.load()) return;
    std::shared_ptr<JuegoB> j;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      j = g_jb;
    }
    if (!j) return;
    uint8_t* base = Base();
    const int est = P::EstadoCombate();   // el final de B3 (5-8) corre bajo el resultado y se tapa con negro (como PanelA)
    if (!base || (est != 4 && !(j->fase == 3 && est >= 5 && est <= 8))) return;
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (!mgr) return;
    if (j.get() != hecho_) Preparar(*j);
    const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
    for (uint32_t x : {x0, x1}) StoreF(base, x + kVida, LoadF(base, x + kVidaMax));
    REX_STORE_U8(x1 + 3261, 1);   // el pivote sin cuerpo (como P4: sub_82109D90, tagteam.cpp Veil)
    REX_STORE_U8(x1 + 4925, 1);
    // NB007 en la camara de gravedad (campo 880): B3 pone a los luchadores lejos del centro y quedaban fuera de la sala.
    // En IW los dos objetos estan en el ORIGEN (Mg_InitCharactersForRound EU 0x356D80: +0x40/+0x44/+0x48 = 0), que es el
    // centro de la sala del campo 880 (el mod lo instala 1:1, sin recentrar: "mod center hd/campos_iw.py", campo 880 con
    // las piezas en sus coordenadas de IW). DBZ3_R14_POS=x,z sigue sirviendo para moverlo a mano si hiciera falta.
    if (j->m->indice == 6 && g_nave880) {
      static float px = 0, pz = 0;
      static const bool leido = [] {
        if (const char* e = std::getenv("DBZ3_R14_POS")) std::sscanf(e, "%f,%f", &px, &pz);
        return true;
      }();
      (void)leido;
      for (uint32_t o : {48u, 64u, 80u}) {
        StoreF(base, x0 + o, px), StoreF(base, x0 + o + 8, pz);
        StoreF(base, x1 + o, px + 30 * NaveLado()), StoreF(base, x1 + o + 8, pz);   // el pivote invisible, delante
      }
    }
    const bool pausa = dbz3::GameDimmed() || dbz3::OverlayOpen();
    // botones (+12) y, para la cruceta de NB007, la direccion de la palanca de +108 (0x10 arriba, 0x20 derecha, 0x40
    // abajo, 0x80 izquierda, medido por P4: los mismos bits que los codigos 4..7 de IW). La cruceta de la HD llega como
    // palanca en los minijuegos (net/netplay_hooks.cpp); +12 no lleva direcciones (sonda de R14).
    const uint16_t ahora = uint16_t(REX_LOAD_U16(kPads + kPadBotones) | (REX_LOAD_U16(kPads + 108) & 0x00F0));
    j->pendiente = uint16_t(j->pendiente | (ahora & ~j->antes));
    j->antes = ahora;
    acum_ += pausa ? 0.0f : dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
    while (acum_ >= 1.0f / 60) {
      acum_ -= 1.0f / 60;
      ++j->t;
      // MSN019: el 879/0 es la camara de juego y dura 300 cuadros (los mismos que el original, medido: 52,0-57,0 s);
      // despues IW pasa al plano de resultado. La serie solo mide ese tramo, no la camara retenida.
      const auto it879 = g_clips.find(kCam879);
      SerieJuego(j->fase == 2 && !(j->m->tipo == kBarrido && it879 != g_clips.end() && g_cb_frame.load() >= it879->second.nf));
      CuadroPose(*j);
      if (j->pb) {   // poses de IW: espera, READY y (MSN019) el tramo de filas; y sus camaras
        if (j->fase == 0 && j->t == 1) {
          CamB(g_entrada_b.load() > 0 ? kCamEntrada : j->pb->cam_ini);
          if (j->m->tipo == kBarrido) EventoPose(*j, kEvNivel);
          else PonerB(*j->pb, j->pb->espera[0], true);
        } else if (j->fase == 1 && j->ronda == 0 && j->t == 1) {
          CamB(j->pb->cam_intro);
          EventoPose(*j, kEvIntro);
        } else if (j->fase == 1 && j->ronda > 0 && j->ronda <= 9 && j->t == 1) {
          CamB(j->cam_r[j->ronda - 1], false, false, true);
        } else if (j->fase == 2 && j->t == 1 && j->m->tipo == kBarrido && j->ronda > 0) {
          EventoPose(*j, kEvNivel);
        }
      }
      // EU 0x295030 MiniGame_UpdateDispatch
      if (j->fase <= 1) j->pendiente = 0;
      // EU 0x294990 MiniGame_UpdateIntroPhase
      if (j->fase == 0 && j->t >= std::max(30, g_entrada_b.load())) {
        // EU 0x293970 MiniGame_CheckReadyStart: a los 60.0 cuadros del reloj del minijuego (+8) saca el READY
        // (s_MSN_CV_ReadyStart: 2, o 1 si +0x88 != 0) y pasa a +0xc = 1. Aqui el READY es la fase 1: 180 cuadros de
        // clip en la primera ronda (dur_intro, medido en el TSV del original) y 60 en las siguientes, y la salida sin
        // clip espera 30 cuadros como el clip 0xB4 quieto. `coincide`.
        j->fase = 1;
        j->t = 0;
        REXLOG_INFO("dbz3 r14: en juego ({} dificultad {})", j->m->nombre, j->dif);
      } else if (j->fase == 1 && j->t >= ((j->ronda == 0 && j->pb) ? j->pb->dur_intro : 60)) {   // READY? (la primera) o el numero de ronda
        j->fase = 2;
        j->t = 0;
        // 05: camara de juego de IW (MSN019: DATA_CMN 879, un solo clip de 300 cuadros para TODAS las filas: no se
        // reinicia en cada fila, que antes la dejaba siempre en el primer tramo (ojo a 154) y no bajaba nunca).
        if (j->pb && j->pb->cam_juego >= 0 &&
            !(j->pb->cam_juego == kCam879 && g_cb_clip.load() == kCam879))
          CamB(j->pb->cam_juego, false, false, true);
        else if (j->pb && g_entrada_b.load() > 0 && j->ronda == 0)
          CamB(j->pb->cam_ini, false, false, true);   // s8 NB007: 0xB4 en la ronda 1
        else if (j->pb && j->ronda == 0 && j->pb->cam_ronda[0] >= 0)
          CamB(j->cam_r[0], false, false, true);   // NB012: 0x14B (barajado) ya en la ronda 1
      } else if (j->fase == 2) {
        PasoB(*j, j->pendiente);
        j->pendiente = 0;
        if (j->auto_) {
          dbz3::net::InyectarPad(0, j->inyecta_t > 0 ? j->inyecta : 0);
          if (j->inyecta_t > 0 && --j->inyecta_t == 0) j->inyecta = 0;
        }
      } else if (j->fase == 3 && j->t == 1) {
        if (std::getenv("DBZ3_IWMINI_RAFAGA_FIN")) std::ofstream("dbz3_shot.req") << "40 15";   // prueba
        if (j->auto_) dbz3::net::InyectarPad(0, -1);
        // EU 0x293530 MiniGame_ShowResult
        if (j->pb) CamB(j->rango > 0 ? j->pb->cam_gana : j->pb->cam_pierde);
        EventoPose(*j, kEvFin);
        if (!fin_) {
          fin_ = true;
          REX_STORE_U8(mgr + kFin, uint8_t(j->rango > 0 ? 1 : 2));
        }
      }
    }
    Dibujar(io, *j);
  }

 private:
  void Preparar(const JuegoB& j) {
    hecho_ = &j;
    fin_ = false;
    acum_ = 0;
    objetivo_ = ps2::Crear(j.objetivo);
    ready_ = ps2::Crear(j.ready);
    start_ = ps2::Crear(j.start);
    clear_ = ps2::Crear(j.clear);
    failed_ = ps2::Crear(j.failed);
    aro_ = ps2::Crear(j.aro);
    for (int k = 0; k < 4; ++k) palabra_[k] = ps2::Crear(j.palabra[k]);
    banda_ = ps2::Crear(j.banda);
    fila_ = ps2::Crear(j.caja_fila);
    cifras_ = ps2::Crear(j.cifras);
  }
  void Dibujar(ImGuiIO& io, const JuegoB& j) {
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const S::Area a = S::GameArea(io);
    auto at = [&](float x, float y) { return a.At(x * 1200 / kW, y * 900 / kH); };
    const float k = a.s * 900 / kH;
    // s9: NB007 con las piezas de IW (cotejado con la ronda 1 de PCSX2): sin aro ni cara arriba; la tabla de las 10
    // rondas a la izquierda (caja t16 y cifras t06, filas cada 30 px como MiniGame_SetupSprites) y la banda t38 abajo
    const bool iw4 = j.m->teclear && banda_ && fila_ && cifras_;
    auto cifra = [&](int g, float x, float y, float h) {   // g: 0-9, 10 = ', 11 = "
      const float cw = 128 / 5.0f, ch = 88 / 4.0f;
      const int col = g < 10 ? g % 5 : 3, row = g < 10 ? g / 5 : g == 10 ? 2 : 3;
      const float f = g < 10 ? 1.0f : 0.55f;   // ' y " ocupan la mitad izquierda de su celda
      const float w = h * cw * f / ch;
      fg->AddImage(cifras_.id(), at(x, y), at(x + w, y + h), ImVec2(col * cw / 128, row * ch / 88),
                   ImVec2((col + f) * cw / 128, (row + 1) * ch / 88));
      return w;
    };
    if (iw4) {
      for (int r = 0; r < 10; ++r) {
        const float y = 22 + 30 * r;
        fg->AddImage(fila_.id(), at(14, y), at(14 + 144, y + 28), ImVec2(0, 0), ImVec2(1, 1),
                     r == j.ronda && j.fase < 3 ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 200));
        float x = 20;
        if (r == 9) x += cifra(1, x, y + 5, 18);
        x += cifra((r + 1) % 10, x, y + 5, 18) + 14;
        const bool hecha = r < j.ronda || (r == j.ronda && j.fase == 3);
        const int cs = hecha ? int(std::lround(j.nota[r] * 100)) : 0;   // centesimas: SS"CC como IW
        const int d[4] = {cs / 1000 % 10, cs / 100 % 10, cs / 10 % 10, cs % 10};
        x += cifra(d[0], x, y + 5, 18);
        x += cifra(d[1], x, y + 5, 18);
        x += cifra(11, x, y + 5, 18);
        x += cifra(d[2], x, y + 5, 18);
        cifra(d[3], x, y + 5, 18);
      }
      if (j.fase == 2) fg->AddImage(banda_.id(), at(0, kH - 134), at(kW, kH));
    }
    for (int r = 0; !iw4 && r < 10; ++r) {
      char txt[32];
      if ((r < j.ronda || (r == j.ronda && j.fase == 3)) && j.m->teclear)
        std::snprintf(txt, sizeof txt, "%2d  %5.2f\"", r + 1, j.nota[r]);
      else if (r < j.ronda || (r == j.ronda && j.fase == 3))
        std::snprintf(txt, sizeof txt, "%2d   %3d", r + 1, int(j.nota[r]));
      else
        std::snprintf(txt, sizeof txt, "%2d   ---", r + 1);
      S::MenuText(fg, a.At(1060, 150 + 44 * r), 34 * a.s, txt, r == j.ronda && j.fase < 3, r > j.ronda);
    }
    if (j.fase == 1) {
      if (j.ronda == 0) PiezaB(fg, ready_, at(kW / 2, 200), k);   // las demas: la fila de la lista se ilumina
    }
    if (j.fase == 2 && j.t < 60) PiezaB(fg, start_, at(kW / 2, 200), k);
    if (j.fase == 2) {
      const float y = kYB + kIcono / 2;
      if (!iw4) fg->AddRectFilled(at(0, y - 18), at(kW, y + 18), IM_COL32(0, 0, 0, 110));
      if (j.m->tipo == kBarrido) {   // la barra y el cursor (objeto 0): el aro de IW
        fg->AddRectFilled(at(j.b0, y - 4), at(j.b1, y + 4), IM_COL32(255, 255, 255, 120));
        if (objetivo_) {
          const ImVec2 c = at(j.cursor, y);
          fg->AddImage(objetivo_.id(), ImVec2(c.x - 38 * k, c.y - 28 * k), ImVec2(c.x + 34 * k, c.y + 28 * k));
        }
      }
      if (objetivo_ && j.m->tipo == kDesplazar) {
        const ImVec2 c = at(kObjB, y);
        fg->AddImage(objetivo_.id(), ImVec2(c.x - 38 * k, c.y - 28 * k), ImVec2(c.x + 34 * k, c.y + 28 * k));
      }
      const S::Pic pic = S::Img("botones");
      for (int i = 0; i < j.sale; ++i) {
        const Icono& ic = j.ic[i];
        if (!ic.vivo) continue;
        if (j.m->tipo == kBarrido && ic.resuelto) continue;   // cogido: se va
        const ImU32 col = ic.resuelto ? IM_COL32(120, 120, 120, 200) : IM_COL32(255, 255, 255, 255);
        // NB007: la fila quieta centrada (48 px de paso) y el aro en el que toca
        const float x = j.m->teclear ? kW / 2 + (i - (j.n - 1) / 2.0f) * 48 : ic.x + kIcono / 2;
        const ImVec2 c = at(x, y);
        if (j.m->teclear && i == j.actual && objetivo_)
          fg->AddImage(objetivo_.id(), ImVec2(c.x - 38 * k, c.y - 28 * k), ImVec2(c.x + 34 * k, c.y + 28 * k));
        if (!pic) {
          S::Glyph(fg, kNombre[ic.boton & 15], ImVec2(c.x - kIcono * k / 2, c.y), kIcono * k);
          continue;
        }
        if (ic.boton < 8) {   // cruceta: la flecha de los menus de B3 HD ("flechas", data_usi 2062) girada
          Flecha(fg, ic.boton, c, kIcono * k * 0.5f, col);
          continue;
        }
        const ImVec4 r = Glifo(ic.boton);
        const float h = kIcono * k * 1.1f, w = (r.z - r.x) * h / (r.w - r.y);
        S::DrawPic(fg, pic, ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), r, col);
      }
      if (j.juicio_t > 0 && j.juicio >= 0) PiezaB(fg, palabra_[j.juicio], at(j.m->teclear ? kW / 2 : kObjB, kYB - 40), k);
    }
    if (j.fase == 3) {
      const float t = std::min(1.0f, j.t / 30.0f);
      fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(160 * t)));
      const bool panel = j.m->tipo == kBarrido && j.t >= 60;   // MSN019: la tabla de Results_* (GREAT/GOOD/NORMAL)
      if (!panel) PiezaB(fg, j.rango > 0 ? clear_ : failed_, at(kW / 2, 200), k * 1.5f * (0.8f + 0.2f * t));
      if (panel) {
        Resultado r;
        r.clave = j.clave;
        r.fila = j.fila;
        r.superado = j.rango > 0;
        r.centesimas = j.cuadros * 100 / 60;
        r.puntos = j.puntos;   // valor historico del mapa; la pantalla usa score/zeni separados
        r.score = j.total;
        r.zeni = j.zeni;
        r.saldo = j.saldo;
        r.rango = j.rango > 0 ? j.rango - 1 : -1;   // DS156: sin superar (rango 0) no hay fila RANK
        r.dificultad = j.dif;
        r.con_tiempo = false;
        int grande = 0, bueno = 0, normal = 0;
        for (float v : j.nota) {
          const int nota = int(v);
          if (nota >= 10) grande += 10;
          else if (nota >= 5) bueno += 5;
          else if (nota >= 3) normal += 3;
        }
        r.great = grande, r.good = bueno, r.normal = normal;
        const ImVec2 q0 = at(0, 0), q1 = at(kW, kH);
        DibujarResultadoIW(fg, q0.x, q0.y, q1.x - q0.x, q1.y - q0.y, r, float(j.t - 60) / 30.0f, false);
      }
      const int fin = j.m->tipo == kBarrido ? 420 : 228;   // 30 cuadros/fila en el juego: la tabla cabe antes del final de B3
      if (const float neg = std::clamp((j.t - fin) / 36.0f, 0.0f, 1.0f); neg > 0)   // a negro hasta el mapa
        fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * neg)));
    }
  }
  const JuegoB* hecho_ = nullptr;
  bool fin_ = false;
  float acum_ = 0;
  ps2::Tex objetivo_, ready_, start_, clear_, failed_, aro_, palabra_[4], banda_, fila_, cifras_;
};

void CreatePanelB() {
  static PanelB* panel = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new PanelB(rt->imgui_drawer());
}

bool EmpezarRondas(int clave, int fila, int dificultad) {
  const ModuloB* m = nullptr;
  for (const ModuloB& x : kModulosB)
    if (x.clave == clave) m = &x;
  if (!m) return false;
  auto j = std::make_shared<JuegoB>();
  j->m = m;
  j->clave = clave;
  j->fila = fila;
  j->dif = std::clamp(dificultad, 0, 5);
  j->vel = m->velocidad[j->dif];
  j->pb = m->poses;
  g_nave_lado = (m->indice == 6 || m->indice == 18) ? -1.0f : 1.0f;
  g_cb_amc_entrada = m->indice == 6 ? 872 : 0;
  const char* t = std::getenv("DBZ3_TEST_IWMINI");
  j->auto_ = t && std::strstr(t, "auto");
  j->fallos = t && std::strstr(t, "fallos");
  std::string error;
  if (!CargarB(*j, error)) {
    REXLOG_WARN("dbz3 r14: {}: {}", m->nombre, error);
    return false;
  }
  EmpezarRonda(*j);
  g_cb_clip = -1;
  if (m->poses && !std::getenv("DBZ3_IWMINI_CAMARA_B3")) PrecargarCamaras(*m->poses);   // (DBZ3_IWMINI_CAMARA_B3: sin ellas)
  // NB012/NB007 usan una camara fija por ronda: una fila por ronda. MSN019 usa un clip animado (879/0): por tiempo.
  SeriePorClip(m->tipo != kBarrido);
  SerieClip(-1);
  g_entrada_b = (m->indice == 6 && m->poses && !std::getenv("DBZ3_IWMINI_CAMARA_B3")) ? PrecargarEntrada(872, 1) : 0;
  REXLOG_INFO("dbz3 r14: {} (clave 0x{:X}, dificultad {}): rondas {} {} {} {} {} {} {} {} {} {}, {} botones, velocidad {}",
              m->nombre, clave, j->dif, j->orden[0], j->orden[1], j->orden[2], j->orden[3], j->orden[4], j->orden[5],
              j->orden[6], j->orden[7], j->orden[8], j->orden[9], j->n, j->vel);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_jb = j;
  }
  dbz3::launcher::DeferUi(&CreatePanelB);
  // NB007: la camara de gravedad de la nave de IW (DATA_CMN 880, mod center hd/campos_iw.py) si esta instalada
  const int nave = m->indice == 6 ? dbz3::escenarios::CampoIW(880) : -1;
  g_nave880 = nave >= 0;
  const int escenario = nave >= 0 ? nave : std::max(P::EscenarioPorMapa(P::MapaIW(m->escenario_iw)), 0);
  if (!dbz3::hub::RequestBattleEx(m->jugador, 18, escenario, true, -1, 0, 0, 0, 0)) {   // 18: pivote invisible (P4)
    REXLOG_WARN("dbz3 r14: el conector no acepto el combate");
    return false;
  }
  // Contrato de llegada de tools/pruebas.py escena: las escenas IW de campo lo emiten al poner la salida;
  // para estos minijuegos la escena queda lista al aceptar el combate en su escenario real.
  REXLOG_INFO("dbz3 iwmini: salida puesta (minijuego 0x{:X}, escenario {})", clave, escenario);
  dbz3::combate::SetMuneco(1);
  dbz3::combate::OcultarHud(true);
  g_corre_b = true;
  P::Reglas r;
  r.capsulas = false;
  P::VigilarCombate("r14", r, [j](const P::Fin&) {
    g_corre_b = false;
    g_cb_clip = -1;
    if (j->auto_) dbz3::net::InyectarPad(0, -1);
    dbz3::combate::SetMuneco(-1);
    dbz3::combate::OcultarHud(false);
    Resultado res;
    res.clave = j->clave;
    res.fila = j->fila;
    res.superado = j->fase == 3 && j->rango > 0;
    res.centesimas = j->cuadros * 100 / 60;
    res.puntos = j->fase == 3 ? j->puntos : 0;
    res.score = j->fase == 3 ? j->total : 0;
    res.zeni = j->fase == 3 ? j->zeni : 0;
    res.rango = j->rango > 0 ? j->rango - 1 : -1;   // DS156: sin superar (rango 0) no hay fila RANK
    res.dificultad = j->dif;
    res.monedas = j->total;
    res.con_tiempo = j->m->tipo != kBarrido;   // MSN019 no ensena TIME; NB007/NB012 si
    if (j->m->tipo == kBarrido) {   // Results_ComputeTotalsAndRewards: GREAT = 10, GOOD = 5, NORMAL = 3 por fila
      int grande = 0, bueno = 0, normal = 0;
      for (float v : j->nota) {
        const int nota = int(v);
        if (nota >= 10) grande += 10;
        else if (nota >= 5) bueno += 5;
        else if (nota >= 3) normal += 3;
      }
      res.great = grande, res.good = bueno, res.normal = normal;
    } else if (j->m->tipo == kDesplazar) {   // NB012: Mg_FinishAndWriteResults 0x312D90 (+0x48/+0x4C/+0x50, DS153)
      res.great = int(j->cat[0]), res.good = int(j->cat[1]), res.normal = int(j->cat[2]);
    }
    REXLOG_INFO("dbz3 r14: resultado {} superado {} rango {} score {} zeni {}", j->m->nombre, res.superado, res.rango,
                res.score, res.zeni);
    Terminar(res);
  });
  return true;
}

const bool g_registro = [] {
  for (const Modulo2& m : kModulos) RegistrarModulo(m.clave, &Empezar);
  for (const ModuloB& m : kModulosB) RegistrarModulo(m.clave, &EmpezarRondas);
  return true;
}();

}  // namespace

// Gancho del mando (iw_minijuegos.cpp Mando, antes y despues de leer el mando 0): los botones los lleva el minijuego (los
// ve por g_raw12) y el luchador de B3 no los ve (ni golpea ni anda: solo START y SELECT); las poses son las de IW. Antes
// de cada lectura se le devuelve al juego el registro como lo dejo (como MandoVuelo, iw_mini_vuelo.cpp).
// Hilo del juego: el sonido de golpe que toca (iw_minijuegos.cpp, gancho de sub_82114910).
bool SeBotones(int* banco, int* id) {
  const int v = g_se_listo.exchange(-1);
  if (v < 0) return false;
  *banco = v / 1000;
  *id = v % 1000;
  return true;
}

void MandoBotones(uint8_t* base, uint32_t usuario, bool leido) {
  struct Dejado {
    bool hay = false;
    uint16_t b12 = 0, b14 = 0, d108 = 0, d110 = 0;
    uint8_t p26 = 0, p27 = 0;
    uint32_t b16 = 0, cuenta[16] = {};
  };
  static Dejado g;
  if (usuario != 0) return;
  const uint32_t r = kPads;
  if (!leido) {
    if (g.hay) {
      REX_STORE_U16(r + 12, g.b12);
      REX_STORE_U16(r + 14, g.b14);
      REX_STORE_U16(r + 108, g.d108);
      REX_STORE_U16(r + 110, g.d110);
      REX_STORE_U8(r + 26, g.p26);
      REX_STORE_U8(r + 27, g.p27);
      REX_STORE_U32(r + 16, g.b16);
      for (uint32_t k = 0; k < 16; ++k) REX_STORE_U32(r + 0x28 + 4 * k, g.cuenta[k]);
      g.hay = false;
    }
    return;
  }
  if (!g_corre.load()) {
    g_raw_ok = false;
    return;
  }
  g.hay = true;
  g.b12 = REX_LOAD_U16(r + 12);
  g.b14 = REX_LOAD_U16(r + 14);
  g.d108 = REX_LOAD_U16(r + 108);
  g.d110 = REX_LOAD_U16(r + 110);
  g.p26 = REX_LOAD_U8(r + 26);
  g.p27 = REX_LOAD_U8(r + 27);
  g.b16 = REX_LOAD_U32(r + 16);
  for (uint32_t k = 0; k < 16; ++k) g.cuenta[k] = REX_LOAD_U32(r + 0x28 + 4 * k);
  // EU 0x2EE790 DragonMissionQTEInputs
  g_raw12 = g.b12;
  g_raw_ok = true;
  constexpr uint16_t kDeja = 0x0009;   // SELECT 0x01 y START 0x08 (la pausa); el resto son golpes, ki, guardia...
  REX_STORE_U16(r + 12, uint16_t(g.b12 & kDeja));
  REX_STORE_U16(r + 14, uint16_t(g.b14 & kDeja));
  REX_STORE_U16(r + 108, 0);
  REX_STORE_U16(r + 110, 0);
  REX_STORE_U8(r + 26, 0);
  REX_STORE_U8(r + 27, 0);
  REX_STORE_U32(r + 16, g.b16 & (uint32_t(kDeja) << 16 | kDeja));
  for (uint32_t k = 0; k < 16; ++k)   // los botones quitados, "sueltos" (+92 = -1, como P4 con el circulo)
    if (!(kDeja >> k & 1) && int32_t(g.cuenta[k]) > 0) REX_STORE_U32(r + 0x28 + 4 * k, uint32_t(-1));
}

// Hilo del juego (Fotograma de iw_minijuegos.cpp, al empezar el dibujo, cuando los luchadores y la camara estan puestos): la
// camara de los botones. IW lleva una propia (UiAnim 0x4B0/0x1A4, sin leer): de lado, con los dos luchadores a los lados
// de la pantalla (el jugador a la izquierda) y a la altura a la que luchan en el aire (kAire).
// s8 SONDA (DBZ3_S8_SONDA=1): cada 30 cuadros, en el hilo del juego, la posicion de los dos objetos de B3, su giro
// (X+6062 + 15759 = el de IW), la cadera de la pose de IW (anim::RaizActual) y la camara. Para comparar con la tabla
// medida en PCSX2 (studies 8\ENTREGAS\B_minijuegos_iw\INFORME.md).
static float g_cam_d[6];   // s9: la ultima camara pedida (tras el fov, antes de compensar el arrastre de B3)
static float g_cam_fov_iw = 0, g_cam_fov_b3 = 0, g_cam_k = 1;   // s10: fov del clip, fov leido de B3 y factor aplicado
// s10 (DBZ3_S10_MATRIZ=1): busca en la memoria del guest la matriz 4x4 de proyeccion que el juego tiene preparada
// antes de dibujar. La proyeccion D3D deja m11 = 1/tan(fov/2) (o tan(fov/2) segun la convencion); se buscan las
// dos y se marca si el fov implicito es ~0,785 (el que se lee en mgr+0x1C4) o ~0,602 (el que explicaria el 1,3x de
// NB007). Solo lee; no escribe nada. Coste: un barrido de 2 MB cada 30 cuadros, solo con la variable puesta.
static void SondaMatriz(uint8_t* base, uint32_t mgr) {
  static const bool on = std::getenv("DBZ3_S10_MATRIZ") != nullptr;
  if (!on) return;
  constexpr uint32_t k0 = 0x82300000, k1 = 0x82500000;
  const float inv785 = 1.0f / std::tan(0.7853981634f * 0.5f);
  const float inv602 = 1.0f / std::tan(0.602416f * 0.5f);
  static bool volcado = false;
  if (!volcado) {   // una vez: el gestor de camara crudo (para mirar sus 4x4 fuera del juego)
    volcado = true;
    if (std::FILE* f = std::fopen("dbz3_s10_mgr.bin", "wb")) {
      const uint32_t cab[2] = {mgr, 0x400};
      std::fwrite(cab, 4, 2, f);
      std::fwrite(base + mgr, 1, 0x400, f);
      std::fclose(f);
      REXLOG_INFO("dbz3 s10 matriz: gestor volcado en dbz3_s10_mgr.bin (mgr 0x{:08X})", mgr);
    }
  }
  // 1b) El anillo de comandos del GPU, por su alias virtual 0xBBD00000 (= fisico 0x1BD00000 a traves
  // del heap vA0000000). Buscar ahi la proyeccion que se sube como constantes c0..c3.
  static uint32_t n_scan = 0;
  if (n_scan++ < 3) {
    constexpr uint32_t c0 = 0xBBD00000, c1 = 0xBBE00000;
    int cmd_hits = 0;
    for (uint32_t a = c0; a + 64 <= c1; a += 4) {
      const float m11 = LoadF(base, a + 20);
      float fov = 0;
      if (m11 > 1.2f && m11 < 5.0f) fov = 2.0f * std::atan(1.0f / m11);
      else if (m11 > 0.15f && m11 < 0.9f) fov = 2.0f * std::atan(m11);
      else continue;
      if (!(fov > 0.25f && fov < 1.0f)) continue;
      float m[16];
      for (int i = 0; i < 16; ++i) m[i] = LoadF(base, a + 4 * uint32_t(i));
      const bool fila = std::fabs(m[1]) < 1e-3f && std::fabs(m[2]) < 1e-3f && std::fabs(m[3]) < 1e-3f &&
                        std::fabs(m[4]) < 1e-3f && std::fabs(m[6]) < 1e-3f && std::fabs(m[7]) < 1e-3f &&
                        std::fabs(m[8]) < 1e-3f && std::fabs(m[9]) < 1e-3f && std::fabs(m[12]) < 1e-3f &&
                        std::fabs(m[13]) < 1e-3f && std::fabs(m[11] - 1.0f) < 1e-3f && std::fabs(m[15]) < 1e-3f;
      const bool col = std::fabs(m[1]) < 1e-3f && std::fabs(m[3]) < 1e-3f && std::fabs(m[4]) < 1e-3f &&
                       std::fabs(m[7]) < 1e-3f && std::fabs(m[8]) < 1e-3f && std::fabs(m[11]) < 1e-3f &&
                       std::fabs(m[12]) < 1e-3f && std::fabs(m[13]) < 1e-3f && std::fabs(m[14] - 1.0f) < 1e-3f &&
                       std::fabs(m[15]) < 1e-3f;
      if (!fila && !col) continue;
      if (cmd_hits < 8)
        REXLOG_INFO("dbz3 s10 matriz: comando 0x{:08X} {} fov {:.4f} = [{:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} "
                    "{:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f}]",
                    a, fila ? "fila" : "columna", fov, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9],
                    m[10], m[11], m[12], m[13], m[14], m[15]);
      ++cmd_hits;
    }
    REXLOG_INFO("dbz3 s10 matriz: barrido comandos 0x{:08X}..0x{:08X}: {} matrices de proyeccion", c0, c1, cmd_hits);
  }
  // 1) Los 4x4 del propio gestor de camara (vista en +0x60; la proyeccion suele estar en otro bloque): solo se
  // aceptan los que tienen forma de proyeccion (fila/columna 3 casi nula salvo el 1 de perspectiva).
  for (uint32_t off = 0; off <= 0x180; off += 0x20) {
    float m[16];
    for (int i = 0; i < 16; ++i) m[i] = LoadF(base, mgr + off + 4 * uint32_t(i));
    if (!(m[5] > 0.2f && m[5] < 5.0f)) continue;
    const bool fila = std::fabs(m[1]) < 1e-3f && std::fabs(m[2]) < 1e-3f && std::fabs(m[3]) < 1e-3f &&
                      std::fabs(m[4]) < 1e-3f && std::fabs(m[6]) < 1e-3f && std::fabs(m[7]) < 1e-3f &&
                      std::fabs(m[8]) < 1e-3f && std::fabs(m[9]) < 1e-3f && std::fabs(m[12]) < 1e-3f &&
                      std::fabs(m[13]) < 1e-3f && std::fabs(m[11] - 1.0f) < 1e-3f && std::fabs(m[15]) < 1e-3f;
    const bool col = std::fabs(m[1]) < 1e-3f && std::fabs(m[3]) < 1e-3f && std::fabs(m[4]) < 1e-3f &&
                     std::fabs(m[7]) < 1e-3f && std::fabs(m[8]) < 1e-3f && std::fabs(m[11]) < 1e-3f &&
                     std::fabs(m[12]) < 1e-3f && std::fabs(m[13]) < 1e-3f && std::fabs(m[14] - 1.0f) < 1e-3f &&
                     std::fabs(m[15]) < 1e-3f;
    if (!fila && !col) continue;
    const float fov = m[5] > 1.2f ? 2.0f * std::atan(1.0f / m[5]) : 2.0f * std::atan(m[5]);
    REXLOG_INFO("dbz3 s10 matriz: mgr+0x{:03X} {} fov {:.4f} = [{:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} "
                "{:.4f} | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f}]",
                off, fila ? "fila" : "columna", fov, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10],
                m[11], m[12], m[13], m[14], m[15]);
  }
  int hits = 0;
  for (uint32_t a = k0; a + 64 <= k1; a += 4) {
    const float m11 = LoadF(base, a + 20);
    float fov = 0;
    if (m11 > 1.2f && m11 < 5.0f) fov = 2.0f * std::atan(1.0f / m11);
    else if (m11 > 0.15f && m11 < 0.9f) fov = 2.0f * std::atan(m11);
    else continue;
    if (!(fov > 0.25f && fov < 1.0f)) continue;
    float m[16];
    for (int i = 0; i < 16; ++i) m[i] = LoadF(base, a + 4 * uint32_t(i));
    const bool fila = std::fabs(m[1]) < 1e-3f && std::fabs(m[2]) < 1e-3f && std::fabs(m[3]) < 1e-3f &&
                      std::fabs(m[4]) < 1e-3f && std::fabs(m[6]) < 1e-3f && std::fabs(m[7]) < 1e-3f &&
                      std::fabs(m[8]) < 1e-3f && std::fabs(m[9]) < 1e-3f && std::fabs(m[12]) < 1e-3f &&
                      std::fabs(m[13]) < 1e-3f && std::fabs(m[11] - 1.0f) < 1e-3f && std::fabs(m[15]) < 1e-3f;
    const bool col = std::fabs(m[1]) < 1e-3f && std::fabs(m[3]) < 1e-3f && std::fabs(m[4]) < 1e-3f &&
                     std::fabs(m[7]) < 1e-3f && std::fabs(m[8]) < 1e-3f && std::fabs(m[11]) < 1e-3f &&
                     std::fabs(m[12]) < 1e-3f && std::fabs(m[13]) < 1e-3f && std::fabs(m[14] - 1.0f) < 1e-3f &&
                     std::fabs(m[15]) < 1e-3f;
    if (!fila && !col) continue;
    const float d785 = std::fabs(m11 - inv785), d602 = std::fabs(m11 - inv602);
    if (d785 > 0.03f * inv785 && d602 > 0.03f * inv602) continue;
    REXLOG_INFO("dbz3 s10 matriz: addr 0x{:08X} {} fov {:.4f} ({}); m11 {:.4f} m23 {:.4f} m32 {:.4f} m33 {:.4f}", a,
                fila ? "fila" : "columna", fov, d602 < d785 ? "~0.602" : "~0.785", m11, m[11], m[14], m[15]);
    if (hits == 0)   // la primera: los 16 floats, para identificar la convencion
      REXLOG_INFO("dbz3 s10 matriz: completa addr 0x{:08X} = [{:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f}"
                  " | {:.4f} {:.4f} {:.4f} {:.4f} | {:.4f} {:.4f} {:.4f} {:.4f}]",
                  a, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14],
                  m[15]);
    if (++hits >= 12) break;
  }
  REXLOG_INFO("dbz3 s10 matriz: mgr 0x{:08X} fov B3 {:.4f}; barrido 0x{:08X}..0x{:08X}: {} coincidencias", mgr,
              LoadF(base, mgr + 0x1C4), k0, k1, hits);
}
static void Sonda(uint8_t* base, uint32_t mgr, const char* quien) {
  static const bool on = std::getenv("DBZ3_S8_SONDA") != nullptr;
  static const bool mat = std::getenv("DBZ3_S10_MATRIZ") != nullptr;
  static const bool serie = std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  static uint32_t n = 0;
  const bool clip_continuo = serie && g_cb_comparable.load() && g_cb_amc.load() == 879;
  if ((!on && !mat && !serie) || (!clip_continuo && (n++ % 30) != 0)) return;
  if (mat) SondaMatriz(base, mgr);
  float ojo[3] = {0, 0, 0}, obj[3] = {0, 0, 0};
  dbz3::cine::LeerCamara(base, ojo, obj);
  if (on) {
    for (int rol = 0; rol < 2; ++rol) {
      const uint32_t x = mgr + kX0 + kXPaso * uint32_t(rol);
      float r[3] = {0, 0, 0};
      const bool ok = dbz3::anim::RaizActual(base, x, r);
      REXLOG_INFO("dbz3 s8 sonda {}: rol {} pos ({:.2f}, {:.2f}, {:.2f}) giro 0x{:04X} cadera {} ({:.2f}, {:.2f}, {:.2f})"
                  " clip 0x{:X} cuadro {} dibujo ({:.2f}, {:.2f}, {:.2f}) m00 {:.2f} m02 {:.2f}",
                  quien, rol, LoadF(base, x + 64), LoadF(base, x + 68), LoadF(base, x + 72),
                  uint16_t(REX_LOAD_U16(x + kMiradaA) + 15759), ok, r[0], r[1], r[2], g_cb_clip.load(), g_cb_frame.load(),
                  LoadF(base, x + 48), LoadF(base, x + 52), LoadF(base, x + 56), LoadF(base, x + 0), LoadF(base, x + 8));
    }
    REXLOG_INFO("dbz3 s8 sonda {}: camara ojo ({:.2f}, {:.2f}, {:.2f}) obj ({:.2f}, {:.2f}, {:.2f}) fov {:.3f} origen ({:.2f}, "
                "{:.2f}, {:.2f})",
                quien, ojo[0], ojo[1], ojo[2], obj[0], obj[1], obj[2], LoadF(base, mgr + 0x1C4), g_origen[0], g_origen[1],
                g_origen[2]);
  }
  const uint32_t jugador = mgr + kX0;
  bool clip_activo = true;
  if (g_cb_amc.load() == 879) {
    const int clip = g_cb_clip.load();
    const auto it = g_clips.find(clip);
    clip_activo = it != g_clips.end() && g_cb_frame.load() <= it->second.nf;
  }
  const int clip = g_cb_clip.load();
  bool emitir = false;
  if (serie && g_cb_comparable.load() && clip_activo) {
    if (g_cb_bucle.load() || g_cb_amc.load() == 879) {
      emitir = true;
    } else {
      emitir = clip >= 0 && g_cb_ultimo_clip_log.exchange(clip) != clip;
    }
  } else if (!g_cb_comparable.load()) {
    g_cb_ultimo_clip_log = -1;
  }
  if (emitir)
    REXLOG_INFO("dbz3 iwmini: jugador ({:.2f}, {:.2f}, {:.2f}) clip 0x{:X} cuadro {} ojo ({:.2f}, {:.2f}, {:.2f}) fov {:.4f} "
                "fov_clip {:.4f} #AMC {}",
                LoadF(base, jugador + 64), LoadF(base, jugador + 68), LoadF(base, jugador + 72), clip,
                g_cb_frame.load(), ojo[0], ojo[1], ojo[2], LoadF(base, mgr + 0x1C4), g_cam_fov_iw, g_cb_amc.load());
  if (!on) return;
  REXLOG_INFO("dbz3 s9 sonda {}: camara pedida ojo ({:.2f}, {:.2f}, {:.2f}) obj ({:.2f}, {:.2f}, {:.2f})", quien, g_cam_d[0],
              g_cam_d[1], g_cam_d[2], g_cam_d[3], g_cam_d[4], g_cam_d[5]);
  // s10 (NB007 ronda 8, clip 0xBB): fov del clip 0,3295 (medido en el #AMC), k = tan(fov/2)/tan(0,785/2) = 0,401. Si
  // el tamano sale 1,3x, el dibujo NO usa el 0,785 leido: el fov implicito es ~0,602 (0,785/0,602 = 1,30). Comparar
  // esta linea con el vuelco de constantes del draw (sonda siguiente, ver studies 2 G01_iw_cam).
  const float dp = std::hypot(g_cam_d[0] - g_cam_d[3], std::hypot(g_cam_d[1] - g_cam_d[4], g_cam_d[2] - g_cam_d[5]));
  const float dl = std::hypot(ojo[0] - obj[0], std::hypot(ojo[1] - obj[1], ojo[2] - obj[2]));
  REXLOG_INFO("dbz3 s10 sonda {}: clip 0x{:X} cuadro {} fov clip {:.4f} fov B3 {:.4f} k {:.4f}; distancia pedida {:.2f} "
              "leida {:.2f} ({:.3f}x)",
              quien, g_cb_clip.load(), g_cb_frame.load(), g_cam_fov_iw, g_cam_fov_b3, g_cam_k, dp, dl,
              dl > 0.01f ? dp / dl : 0.0f);
}

// s8: los dos luchadores de la familia A como en IW (medido en PCSX2): objetos en el origen comun (aqui a +-kMedio en X
// para que B3 los gire el uno hacia el otro) y el giro fijo de IW (jugador 0x4000 = mira a +X, rival 0xC002 = a -X).
// La separacion de los cuerpos sale entera de la raiz de sus poses de IW, como en el original (+0x18E8 = 0: el objeto
// no se mueve). Hilo del juego, cada cuadro, al empezar el dibujo.
static void Colocar(uint8_t* base, uint32_t mgr, const Poses& p) {
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  if (!g_origen_ok.load()) {
    g_origen[0] = (LoadF(base, x0 + 64) + LoadF(base, x1 + 64)) / 2;
    g_origen[1] = LoadF(base, x0 + 68);
    g_origen[2] = (LoadF(base, x0 + 72) + LoadF(base, x1 + 72)) / 2;
    if (const char* e = std::getenv("DBZ3_S8_ORIGEN")) std::sscanf(e, "%f,%f", &g_origen[0], &g_origen[2]);   // prueba
    g_origen_ok = true;
    REXLOG_INFO("dbz3 s8: origen comun de IW en B3 ({:.2f}, {:.2f}, {:.2f}); giros 0x{:X} / 0x{:X}", g_origen[0], g_origen[1],
                g_origen[2], p.yaw[0], p.yaw[1]);
  }
  static const bool sin_comp = std::getenv("DBZ3_S8_SIN_RAIZ") != nullptr;   // prueba: sin la correccion de la raiz
  static int conv = -1;   // s9: convencion de la matriz de mundo (0: fila, x' = x M00 + z M20; 1: columna), ver abajo
  for (int rol = 0; rol < 2; ++rol) {
    const uint32_t x = rol ? x1 : x0;
    const float px = g_origen[0] + (rol ? kMedio : -kMedio);
    for (uint32_t o : {48u, 64u, 80u}) {   // la posicion esta tres veces (tagteam.cpp StepSlide)
      StoreF(base, x + o, px);
      StoreF(base, x + o + 4, g_origen[1]);
      StoreF(base, x + o + 8, g_origen[2]);
    }
    // s9: giro de DIBUJO del rival = el de IW + DBZ3_S9_GIRO_RIVAL (hex; la posicion sigue con el giro de IW). Con el de
    // IW Trunks salia de espaldas a la camara del READY y golpeaba hacia fuera (cotejado con la captura de IW).
    // s9 (cotejado con IW, nb013_030/050 de PCSX2): los dos cuerpos salen 90 grados tarde respecto a IW (el mismo cambio
    // de base de 90 grados que la cadera, ver abajo). Con +0x4000 Trunks mira a Vegeta y a la camara del READY y Vegeta
    // queda de espaldas a la camara del juego, como en IW; con 0 o 0xC000, de espaldas o hacia fuera.
    // DBZ3_S9_GIRO_RIVAL / DBZ3_S9_GIRO_JUG (hex) para comparar.
    static const char* env_r = std::getenv("DBZ3_S9_GIRO_RIVAL");
    static const char* env_j = std::getenv("DBZ3_S9_GIRO_JUG");
    const char* env = rol ? env_r : env_j;
    // NB006: presentacion y READY al reves (cotejado con nb006_032: Gohan de espaldas, Piccolo de frente con los brazos
    // cruzados) = 0xC000; en el juego 0x4000 (nb006_040: Gohan salta sobre Piccolo, en primer plano a la derecha)
    const bool nb006_pre = &p == &kPosesNB006 && g_fase_a.load() < 2;
    const uint16_t extra_r = env ? uint16_t(std::strtoul(env, nullptr, 16)) : nb006_pre ? 0xC000 : 0x4000;
    const uint16_t giro_d = uint16_t(p.yaw[rol] + extra_r);
    REX_STORE_U16(x + kMiradaA, uint16_t(giro_d - 15759));
    REX_STORE_U16(x + kMiradaB, uint16_t(giro_d - 15968));
    // Raiz de la pose: medido con la sonda (DBZ3_S8_SONDA) contra PCSX2, la cadera que pone anim_iw (RaizActual, p) va
    // girada 90 grados respecto a la de IW (q): p = (8,9; 8,0; 0,1) frente a q = (0,13; 10,8; -8,9) en la presentacion
    // de NB013, es decir q = (p.z, p.y, -p.x); en el mundo T(a) con a = giro (x' = lx cos a + lz sin a,
    // z' = -lx sin a + lz cos a). Sin corregir, los cuerpos se separaban en profundidad (uno delante del otro) en vez de
    // en el eje del par. Se corrige SOLO la posicion de dibujo (fila 3 de la matriz, X+48): la logica de B3 (X+64/X+80)
    // sigue en el eje X y los gira el uno hacia el otro.
    // s8 (NB006, medido): la cadera en el mundo de IW es W = T(giro - 90) (-p.x, p.z): con giro 0x4000 (-p.x, p.z) y con
    // 0xC002 (p.x, -p.z). NB006: Gohan p = (0,23; -8,69) -> IW (0,2; -8,5) y Piccolo p = (0,74; -7,32) -> IW (-0,7; 7,3)
    // (PCSX2 +0x70 en t = 70 s, cotejado con la captura: Gohan a la izquierda, Piccolo a la derecha con la camara en -X).
    // La formula de antes, T(giro) (p.z, -p.x), daba la z al reves: en NB013/NB014 no se notaba (z ~ 0), en NB006 los
    // cambiaba de lado y la camara del punto (0x98, mira a z = -16) no veia a nadie. B3 dibuja la cadera en X+48 + T(giro) p:
    // se pone X+48 = origen + W - T(giro) p.
    float r[3];
    if (sin_comp || !dbz3::anim::RaizActual(base, x, r)) continue;
    const float a = float(giro_d) / 65536.0f * 6.2831853f, ca = std::cos(a), sa = std::sin(a);
    const float b = float(p.yaw[rol]) / 65536.0f * 6.2831853f - 1.5707963f, cb = std::cos(b), sb = std::sin(b);
    const float wx = -r[0] * cb + r[2] * sb, wz = r[0] * sb + r[2] * cb;   // T(b) (-p.x, p.z)
    const float bx = r[0] * ca + r[2] * sa, bz = -r[0] * sa + r[2] * ca;   // T(a) p (lo que pone B3)
    StoreF(base, x + 48, px + wx - bx);
    StoreF(base, x + 56, g_origen[2] + wz - bz);
    // s9: el giro de dibujo de verdad es la matriz de mundo (X+0, filas 0..2, como gigantes.cpp); X+6062 no gira el cuerpo
    // (Trunks salia de espaldas igual con 0 que con 180 grados extra). Con giro extra, se escriben las filas con Ry(a) en
    // la convencion que tenga el juego (se deduce una vez de la matriz del jugador, que debe dar su giro de IW).
    if (extra_r) {
      if (conv < 0) {
        const float m00 = LoadF(base, x0 + 0), m02 = LoadF(base, x0 + 8);
        const float ref = float(p.yaw[0]) / 65536.0f * 6.2831853f;
        auto dif = [&](float v) { return std::fabs(std::remainder(v - ref, 6.2831853f)); };
        conv = dif(std::atan2(-m02, m00)) <= dif(std::atan2(m02, m00)) ? 0 : 1;
        REXLOG_INFO("dbz3 s9: matriz del jugador m00 {:.3f} m02 {:.3f}: convencion {}", m00, m02, conv);
      }
      const float esc = std::hypot(LoadF(base, x + 0), LoadF(base, x + 8));
      const float c = ca * esc, sn = sa * esc;
      StoreF(base, x + 0, c), StoreF(base, x + 4, 0), StoreF(base, x + 8, conv ? sn : -sn);
      StoreF(base, x + 32, conv ? -sn : sn), StoreF(base, x + 36, 0), StoreF(base, x + 40, c);
    }
  }
}

// s8: el fov de IW. Escribir mgr+0x1C4 al empezar el dibujo NO cambia nada (probado: DBZ3_S8_FOV=0.35 da la misma imagen;
// la proyeccion ya esta hecha con el 0,785 de B3). Se reproduce el encuadre acercando el ojo al objetivo en la proporcion
// tan(fov_iw/2) / tan(fov_b3/2): el sujeto del objetivo sale del mismo tamano que en IW (fov 0,52-0,62 en los clips
// medidos frente a 0,785: los luchadores salian ~1,5 veces mas pequenos). DBZ3_S8_SIN_FOV lo quita (comparar).
static void CamaraFov(uint8_t* base, uint32_t mgr, const float ojo[3], const float obj[3], float fov, float roll = 0.0f) {
  static const bool sin = std::getenv("DBZ3_S8_SIN_FOV") != nullptr;
  fov = std::clamp(fov, 0.08726647f, 3.0543263f);   // B2: los mismos limites que Camera_SetupFieldOfView (5..175 grados)
  const float b3 = LoadF(base, mgr + 0x1C4);
  // El fov de IW (0,60) es mas estrecho que el de B3 (0,785): se compensa acercando el ojo. Las familias que ya
  // cuadran (NB013/NB014) conservan la compensacion original; NB006 y la familia B usan el clip tal cual (abajo).
  float k = sin || !(b3 > 0.3f && b3 < 1.4f) ? 1.0f : std::tan(fov * 0.5f) / std::tan(b3 * 0.5f);
  // NB006: la 0x97 de IW esta medida con la camara real (ojo -57,4 / objetivo -15): el acercamiento por el fov (0,60
  // frente a 0,785) la dejaba a 44-46 de los 53,7 del original. Aqui se usa el clip tal cual y el fov ancho de B3.
  // Familia A menos NB006: se mantiene la compensacion (sus clips ya cuadran). Familia B y NB006: el clip tal cual, que
  // es la camara real medida en IW; el fov ancho de B3 encoge un poco a los luchadores, pero la posicion es la original.
  if (g_poses_a.load() == &kPosesNB006 || g_corre_b.load()) k = 1.0f;
  g_cam_fov_iw = fov, g_cam_fov_b3 = b3, g_cam_k = k;
  float e[3] = {obj[0] + (ojo[0] - obj[0]) * k, obj[1] + (ojo[1] - obj[1]) * k, obj[2] + (ojo[2] - obj[2]) * k};
  float t[3] = {obj[0], obj[1], obj[2]};
  // prueba: camara fija en el mundo de B3 (DBZ3_S8_CAMDBG=ojo.x,y,z,obj.x,y,z), p. ej. una vista desde arriba
  if (const char* cd = std::getenv("DBZ3_S8_CAMDBG"))
    std::sscanf(cd, "%f,%f,%f,%f,%f,%f", &e[0], &e[1], &e[2], &t[0], &t[1], &t[2]);
  std::memcpy(g_cam_d, e, sizeof e), std::memcpy(g_cam_d + 3, t, sizeof t);   // s9 sonda: la camara pedida
  // s8: B3 arrastra la camara puesta un 20 % hacia la suya cada fotograma y DIBUJA esa (medido con la sonda en NB013:
  // objetivo leido = 0,8 x el puesto + 0,2 x (0, 13, 0)). s9: la estimacion de su camara (T = (R - 0,8 W) / 0,2) no
  // llegaba: la sonda leia el ojo a ~30 del objetivo con 21,6 pedido (los luchadores ~25 % mas pequenos que en IW). Ahora
  // en lazo cerrado: cada cuadro se suma a lo puesto la mitad de lo que falta entre lo leido (lo que dibuja) y lo pedido,
  // y converge a lo pedido sea cual sea su arrastre (error x0,6 por cuadro). DBZ3_S8_ALFA=0 lo quita (comparar).
  static const bool sin_lazo = std::getenv("DBZ3_S8_ALFA") && std::atof(std::getenv("DBZ3_S8_ALFA")) < 0.01;
  static float wo[3], wt[3], co[3], ct[3];
  static bool hay = false;
  // Al cambiar de clip el objetivo salta de golpe: el acumulador del lazo se reinicia (si no, en NB012 oscilaba
  // cientos de unidades al entrar en 0x14B y la camara tardaba segundos en estabilizarse).
  static int ult_clip = -2;
  if (g_cb_clip.load() != ult_clip) {
    ult_clip = g_cb_clip.load();
    hay = false;
    for (int i = 0; i < 3; ++i) co[i] = ct[i] = 0.0f;
  }
  // Familia B: sus clips son coordenadas ABSOLUTAS del mundo de IW y el lazo de correccion (pensado para el arrastre de
  // B3 en la familia A) se desbocaba en la ronda 1 de NB012 (0x14B, ojo a 1,9 de altura): el juego ignora la camara
  // escrita mientras dura el cambio de ronda y el acumulador se iba a -80. En B se usa el clip tal cual cada cuadro.
  if (!sin_lazo && !g_corre_b.load()) {
    float ro[3], rt[3];
    if (hay && dbz3::cine::LeerCamara(base, ro, rt)) {
      float lejos = 0;   // otra camara (otra escena o la del juego entre medias): empezar de nuevo
      for (int i = 0; i < 3; ++i) lejos = std::max({lejos, std::fabs(ro[i] - wo[i]), std::fabs(rt[i] - wt[i])});
      for (int i = 0; i < 3; ++i) {
        co[i] = lejos > 150.0f ? 0.0f : std::clamp(co[i] + 0.5f * (g_cam_d[i] - ro[i]), -80.0f, 80.0f);
        ct[i] = lejos > 150.0f ? 0.0f : std::clamp(ct[i] + 0.5f * (g_cam_d[3 + i] - rt[i]), -80.0f, 80.0f);
      }
    }
    for (int i = 0; i < 3; ++i) e[i] += co[i], t[i] += ct[i];
  }
  dbz3::cine::Camara(base, e, t, roll);
  std::memcpy(wo, e, sizeof wo);
  std::memcpy(wt, t, sizeof wt);
  hay = true;
}

// Familia B: la camara de IW del clip en curso (CamB) alrededor del luchador, que mira a +X como en IW (s8, medido).
static void PasoCamaraB(uint8_t* base) {
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr) return;
  const uint32_t st = REX_LOAD_U32(mgr + 35128);
  if (st > 8) return;   // s9: el giro tambien antes del estado 3 (READY); la camara, solo de 3 a 8
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  const float px = LoadF(base, x0 + 64), py = LoadF(base, x0 + 68), pz = LoadF(base, x0 + 72);
  // s8: el rival-pivote a +X: en IW el luchador de NB007 mira a +X (+0x1834 = 0x4000, medido en PCSX2; antes iba a +Z,
  // 90 grados de error, y el panel lo ponia a +X: los dos se peleaban cada cuadro)
  // s8 (NB007, cotejado con IW): con el giro 0x4000 de IW Goku salia de espaldas a la camara del READY (0x352: ojo en
  // (-14,4; 17,1; 12,7)), que en IW le ve de frente con la consola (centro de la nave, x = 55) detras. Las poses de 922
  // vienen escritas a 180 grados (anim_iw.cpp): el pivote va a -X y el giro de B3 es 0xC000.
  for (uint32_t o : {48u, 64u, 80u}) {
    StoreF(base, x1 + o, px + 30.0f * NaveLado());
    StoreF(base, x1 + o + 8, pz);
  }
  const uint16_t giro = NaveLado() < 0 ? 0xC000 : 0x4000;
  REX_STORE_U16(x0 + kMiradaA, uint16_t(giro - 15759));
  REX_STORE_U16(x0 + kMiradaB, uint16_t(giro - 15968));
  {   // s9: el cuerpo se gira con su matriz de mundo (X+6062 no lo gira; ver Colocar): el giro de IW (0x4000, medido) mas
      // los 90 grados de diferencia de base medidos en NB013 (convencion 0: fila 0 = (c, 0, -s), fila 2 = (s, 0, c)).
      // DBZ3_S9_NAVE_GIRO (hex) para comparar.
    // cotejado con IW (READY 0x352: Goku de frente en primer plano): 0 en el READY (sus poses las gira anim_iw 180
    // grados) y DBZ3_S9_NAVE_GIRO en las rondas (las de familia B no se giran)
    static const uint16_t g_r = std::getenv("DBZ3_S9_NAVE_GIRO")
                                    ? uint16_t(std::strtoul(std::getenv("DBZ3_S9_NAVE_GIRO"), nullptr, 16)) : 0xC000;   // cotejado: ronda 1 de frente como IW
    const uint16_t g = g_cb_clip.load() == 0x352 || g_cb_clip.load() < 0 ? 0 : g_r;
    const float an = float(g) / 65536.0f * 6.2831853f, esc = std::hypot(LoadF(base, x0 + 0), LoadF(base, x0 + 8));
    const float c = std::cos(an) * esc, sn = std::sin(an) * esc;
    if (esc > 0.1f) {
      StoreF(base, x0 + 0, c), StoreF(base, x0 + 4, 0), StoreF(base, x0 + 8, -sn);
      StoreF(base, x0 + 32, sn), StoreF(base, x0 + 36, 0), StoreF(base, x0 + 40, c);
    }
  }
  // s9: el giro va antes de mirar el clip y del estado 3: en el READY Goku salia de espaldas (sonda: X+6062 sin poner)
  static uint32_t st_log = 99;
  if (st != st_log) {
    st_log = st;
    REXLOG_INFO("dbz3 s9 nave: estado {} clip 0x{:X}", st, g_cb_clip.load());
  }
  if (st < 3) return;
  const int id = g_cb_clip.load();
  const auto it = id >= 0 ? g_clips.find(id) : g_clips.end();
  if (it == g_clips.end()) return;
  const ClipCam& c = it->second;
  const float f = CuadroClip(c);
  float e[3], t[3], fov = 0.6f;
  Muestra(c.ojo, f, e);
  Muestra(c.obj, f, t);
  if (!c.fov.empty()) Muestra(c.fov, f, &fov);
  float roll = 0.0f;
  if (!c.roll.empty()) Muestra(c.roll, f, &roll);
  const float ojo[3] = {px + e[0], py + e[1], pz + e[2]}, obj[3] = {px + t[0], py + t[1], pz + t[2]};
  CamaraFov(base, mgr, ojo, obj, fov, roll);
}

// Familia A: la camara de IW del clip en curso, anclada a los dos luchadores. En IW los dos objetos estan en el origen y su
// separacion sale de la raiz de las poses; el clip se hizo para ese par (centro y eje jugador -> rival en `ancla`). Se lleva
// al mundo de B3 con el centro = punto medio de los objetos, el eje = jugador -> rival y el suelo = y del jugador.
// s8: con el origen comun (Colocar) los clips van tal cual, sumados al origen: el mundo de IW y el de B3 solo se
// diferencian en la traslacion, porque los luchadores tienen el mismo giro que en IW (medido) y estan en su eje X.
static bool PasoCamaraA(uint8_t* base, uint32_t mgr, const Poses& p) {
  const int id = g_cb_clip.load();
  const auto it = id >= 0 ? g_clips.find(id) : g_clips.end();
  if (it == g_clips.end()) return false;
  const ClipCam& c = it->second;
  const float f = CuadroClip(c);
  if (g_origen_ok.load() && !std::getenv("DBZ3_R14_ANCLA")) {   // DBZ3_R14_ANCLA: la camara vieja (ancla), para comparar
    float e[3], t[3], fov = 0.6f;
    Muestra(c.ojo, f, e);
    Muestra(c.obj, f, t);
    if (!c.fov.empty()) Muestra(c.fov, f, &fov);
    float roll = 0.0f;
    if (!c.roll.empty()) Muestra(c.roll, f, &roll);
    if (const char* ff = std::getenv("DBZ3_S8_FOV")) fov = float(std::atof(ff));   // prueba: fov forzado
    const float ojo[3] = {g_origen[0] + e[0], g_origen[1] + e[1], g_origen[2] + e[2]};
    const float obj[3] = {g_origen[0] + t[0], g_origen[1] + t[1], g_origen[2] + t[2]};
    CamaraFov(base, mgr, ojo, obj, fov, roll);
    return true;
  }
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  const float ax = LoadF(base, x0 + 64), ay = LoadF(base, x0 + 68), az = LoadF(base, x0 + 72), bx = LoadF(base, x1 + 64),
              bz = LoadF(base, x1 + 72);
  float ux = bx - ax, uz = bz - az;
  const float l = std::hypot(ux, uz);
  if (l < 1e-4f) return false;
  ux /= l, uz /= l;
  static const float rot = std::getenv("DBZ3_R14_ROT") ? float(std::atof(std::getenv("DBZ3_R14_ROT"))) * 3.14159265f / 180 : 0.0f;
  const float mx = (ax + bx) / 2, mz = (az + bz) / 2;
  // giro alrededor de Y que lleva el eje de IW al eje del par (el angulo de un vector, de +X hacia +Z, baja lo que sube el giro)
  const float th = std::atan2(p.ancla[3], p.ancla[2]) - std::atan2(uz, ux) + rot, ct = std::cos(th), st = std::sin(th);
  auto mundo = [&](const float v[3], float out[3]) {
    const float dx = v[0] - p.ancla[0], dz = v[2] - p.ancla[1];
    out[0] = mx + dx * ct + dz * st;
    out[1] = ay + v[1];
    out[2] = mz - dx * st + dz * ct;
  };
  float e[3], t[3], ojo[3], obj[3], fov = 0.6f;
  Muestra(c.ojo, f, e);
  Muestra(c.obj, f, t);
  if (!c.fov.empty()) Muestra(c.fov, f, &fov);
  float roll = 0.0f;
  if (!c.roll.empty()) Muestra(c.roll, f, &roll);
  mundo(e, ojo);
  mundo(t, obj);
  CamaraFov(base, mgr, ojo, obj, fov, roll);
  return true;
}

void PasoBotones(uint8_t* base) {
  if (g_poses_a.load() == &kPosesNB006) dbz3::escenarios::OcultarFondo(g_fondo_off.load());
  if (g_corre_b.load()) {
    if (const uint32_t mgr = REX_LOAD_U32(kMgrPtr)) Sonda(base, mgr, "B");
    PasoCamaraB(base);
    return;
  }
  if (!g_corre.load()) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr) return;
  const uint32_t st = REX_LOAD_U32(mgr + 35128);
  if (st < 3 || st > 8) return;   // el final de B3 (5-8) tambien: la camara del resultado se mantiene
  if (const Poses* pp = g_poses_a.load(); pp && !std::getenv("DBZ3_R14_SEP")) Colocar(base, mgr, *pp);
  Sonda(base, mgr, "A");
  if (const Poses* pp = g_poses_a.load(); pp && g_cb_clip.load() >= 0 && PasoCamaraA(base, mgr, *pp)) return;
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  const float ax = LoadF(base, x0 + 64), az = LoadF(base, x0 + 72), bx = LoadF(base, x1 + 64), bz = LoadF(base, x1 + 72);
  float dx = bx - ax, dz = bz - az;
  const float l = std::hypot(dx, dz);
  if (l < 1e-3f) return;
  dx /= l, dz /= l;
  // lado de la camara: f = -s mirando al punto medio; la derecha de la pantalla (f x arriba) debe ir de A a B
  float sx = -dz, sz = dx;
  const float fx = -sx, fz = -sz;
  if ((-fz) * dx + fx * dz < 0) sx = -sx, sz = -sz;   // f x (0,1,0) = (-fz, 0, fx)
  const bool foco = g_cam_lado.load() < 0;   // la pose final: el jugador solo
  const float mx = foco ? ax : (ax + bx) / 2, mz = foco ? az : (az + bz) / 2, y = LoadF(base, x0 + 68);
  static float alto = 14, dist = 62;
  if (g_cam_reset.exchange(false)) alto = g_cam_alto.load(), dist = g_cam_dist.load();
  alto += (g_cam_alto.load() - alto) * 0.06f;
  dist += (g_cam_dist.load() - dist) * 0.06f;
  const float lado = g_cam_lado.load();
  const float ojo[3] = {mx + sx * dist * lado, y + alto + 6, mz + sz * dist * lado}, obj[3] = {mx, y + alto, mz};
  dbz3::cine::Camara(base, ojo, obj);
}
}  // namespace dbz3::iwmini
