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
#include "launcher/settings.h"
#include "reloj_prueba.h"
#include "escenarios.h"
#include "iw_minijuegos.h"
#include "iw_amc.h"
#include "iw_chr_fade.h"
#include "iw_tinte_original.h"   // PUL-IW-FX-05
#include "iw_recompensa.h"
#include "iw_msn019_estados.h"
#include "iw_mips.h"
#include "iw_gs_traductor.h"
#define IWB_SOLO_DECL
#include "iw_mips_botones.inl"   // tanda3: nb013 (7DB) y las que siguen
#undef IWB_SOLO_DECL
#include "iw_gs.h"
#include "iw_iop.h"
#include "iw_mini_extra.h"

REXCVAR_DECLARE(bool, dbz3_iw_botones_ui_anim_d4);   // PAQ_2: definido en iw_mips_botones.inl (impl, iw_mips.cpp)
REXCVAR_DECLARE(bool, dbz3_iw_r23_eventos);           // IW-06: definido en iw_mini_disparo.cpp
#include <rex/input/input_system.h>
#include <thread>

#include "modo_original.h"
#include <rex/logging.h>
#include <rex/cvar.h>
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
#include "iw_anim_intro7db.h"
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
#include "iw_camaras_gp2.h"
#include "cine.h"
#include "combate.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "launcher/launcher_state.h"
#include "pasarela.h"
#include "iw_ap_eventos.h"
#include "pasarela_iw.h"
#include "ps2_modelo.h"
#include "iw_ame.h"

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
int PalancaDerechaX();
int PalancaDerechaY();
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
// eIW5: la banda azul de IW es el objeto AMA 4/1 0x2E: t38 de 800x168 a escala 1 centrada en (320, 390) (volcado
// DBZ3_TEST_AMA_DUMP), es decir -80..720 x 306..474 en 640x448: los remates metalicos laterales quedan fuera de la
// pantalla, como en el original. DBZ3_IW_BANDA_ANCHA=0 vuelve al ajuste anterior (640x134, marco visible a ~7 px).
static const bool g_sil_viejas = std::getenv("DBZ3_IW_SIL_VIEJAS") != nullptr;
template<class Fg, class At, class Tx>

void DibujarBandaIW(Fg* fg, At at, const Tx& banda_) {
  // R16 HUD: cotejado con el volcado (G02_0x7DB_INI c04000) la banda t38 (800x168) se pinta entera a 640x134 en el
  // borde inferior: sus hombros caen al 18 % y 82 % del ancho, como en el original. El ajuste "ancha" (AMA a -80..720)
  // dejaba los hombros desplazados y cortaba la banda; queda solo como comparacion (DBZ3_IW_BANDA_ANCHA=1).
  static const bool ancha = []{ const char* v = std::getenv("DBZ3_IW_BANDA_ANCHA"); return v && v[0] == '1'; }();
  if (ancha) { fg->AddImage(banda_.id(), at(-80, 306), at(720, 474)); return; }
  // `at` cubre el ancho completo de la imagen del juego: en 4:3 queda la banda original y en 16:9 llega de borde a
  // borde (antes se cortaba en seco a los 3/4 del ancho, el lado derecho).
  fg->AddImage(banda_.id(), at(0, 448 - 134), at(640, 448));
}
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
                                {0x258, 0x29E}, {0x29F, 0x2A0}, 0x2A9, 0x2AA, true, 14, 50, 15,
                                // EU 0x377F10 Mg_IntroScreenSetup (resultado global), NO Mg_EnterResultPhase (punto).
                                 0x96, 0x97, 0x97, 0x97, 0x322, 0x323, 230, {-15, -0.5f, 0, 1, 7}, {0, 0}, -1,
                               871, 2, 0x321, 300, {0x4000, 0xC002}};  // s8: entrada 871 (2 clips, 630 cuadros; L02)
// NB006: 0x9A es el resultado de UN PUNTO (Mg_EnterResultPhase), no el cierre del minijuego.
// Mg_IntroScreenSetup (EU 0x377F10) conserva 0x97 en bucle con poses 0x16/0x12 o 0x17/0x13;
// Mg_UpdateReadyPhase (0x3781B0) espera 180 cuadros al perder o fin de la animacion 0x29E (180) al ganar.
// El final del
// jugador 0x322 con rango > 0 / 0x323 con rango 0 (Mg_StartResultAnim L466793-466901, por iRam0049cebc + 0x1C).
// 0x98 es la camara del especial (Mg_ResetRoundState); no entra por cada acierto.
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
// fase 1 (7/11): fRam0049c85c (el "pantalla" de las formulas de arriba) vale 600, no 640: leido por PINE en el savestate
// del 0x7DB (tools/iw_mips/sonda_meta.py --solo-ancho: 600,0 en 0x49C85C y 0x49C84C). Las x de los iconos estan en esas
// unidades de UI. Con 600 el objetivo es x 450 (caja 422..457) y la regla del decompilado da justo lo medido en PCSX2
// (llega en x 405,7 -> int(x+16) >= 422; el fallo, en 471-474). kObjetivo/kAcepta (640) se quedan para el dibujo y las
// marcas/especiales de NB006/NB013, que estan calibrados aparte.
constexpr float kAnchoUI = 600, kObjetivoUI = kAnchoUI / 2 + 150, kAceptaUI = kObjetivoUI - 100;
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

REXCVAR_DEFINE_BOOL(dbz3_iw_qte_palanca_ps2, true, "DBZ3/IW",
                    "IW 7D4: zona muerta de Masenko en unidades del mando PS2");

uint16_t PalancaMasenko(int x, int y) {
  if (REXCVAR_GET(dbz3_iw_qte_palanca_ps2)) {
    // EU 0x14BAA0/0x14BFF0: primero byte centrado, luego zona muerta de 0x40 inclusive.
    // XInput: Y positivo arriba; PS2: byte Y positivo abajo.
    const int rx = (x + 32768) / 256 - 128;
    const int ry = (std::clamp(-y, -32768, 32767) + 32768) / 256 - 128;
    return uint16_t((rx > 64 ? 0x2000 : rx < -64 ? 0x8000 : 0) | (ry > 64 ? 0x4000 : ry < -64 ? 0x1000 : 0));
  }
  return uint16_t((x > 16384 ? 0x2000 : x < -16384 ? 0x8000 : 0) | (y > 16384 ? 0x1000 : y < -16384 ? 0x4000 : 0));
}

struct Icono {
  int boton = 0;
  float x = 0;
  bool vivo = false, resuelto = false, contado = false, pedido = false;
  bool salida = false, cruz = false;
  int anim_t = 0;
  bool especial = false, preparado = false;
};
struct Carril {
  uint8_t seq[50] = {};
  int hueco[50] = {};
  Icono ic[50];
  int n = 50;   // iconos del carril: hasta la primera separacion negativa (-256 en el carril 1 de NB006)
  int sale = 0, actual = 0, cuenta = 0, aciertos = 0, llegaron = 0;
  int llegaron_raw = 0;   // ruta original de NB013/NB006: ultimo +0x2C/+0x30 visto (recuento de la ronda)
};
struct Juego {
  const Modulo2* m = nullptr;
  int clave = 0, fila = 0, dif = 2;
  Carril c[2];
  // EU 0x32E8D0 QteMg_SetCharStatsByLevel
  float vel = 2, vida[2] = {1000, 1000};
  int fase = 0, t = 0, cuadros = 0, rango = 0, puntos = 100, nota = 0;   // fase 0 espera, 1 READY, 2 juego, 3 fin
  int zeni = 0, saldo = 0;   // premio ZENI y monedero tras sumarlo (QteMg_FinalizeResult); `nota` = puntuacion
  RelojRecompensaIW recompensa;
  float recompensa_anim_salto = 0;
  int aviso_t = 0;
  // Maquinas NB013/NB006: -1=carriles,0=ventana,1=evaluar,2=desenlace.
  int especial_fase = -1, especial_lado = 0, especial_indice = -1, pulsos = 0, especial_t = 0;
  int objetivo_pulsos = 0, rondas = 0, ciclos = 0;
  bool ultimo_pulsado = false;   // fase 1 (7/11): NB014, el ultimo icono se resolvio con una pulsacion (no al pasar)
  uint8_t especiales[2][50] = {};
  float marca[3] = {}, marca_ancho = 0;
  bool marca_activa = false, marca_entrada = false, marca_gate = false;
  bool especial_bien = false, dano_aplicado = false, reaccion_pulso = false;
  bool aac13 = false;   // PUL-IW-FX-04: Vegeta +0x1AAC (Mg_UpdateClashPhase 0x353090: 1 al machacar bien o al llegar al objetivo)
  int pose_estado[2] = {3,2};
  uint32_t pose_codigo[2] = {};
  uint16_t palanca_antes = 0;
  uint16_t palanca_auto = 0;   // PUL-IW-FX-03: piloto de pruebas, palanca derecha del Masenko (7D4 logica original)
  int remate_boton = -1, remate_t = 0;
  float remate_x = 0;
  bool remate_resuelto = false;
  int espera_doble = -1, entrada_reposo = 0;
  const Poses* poses = nullptr;   // R14
  bool presenta_7db = false;
  bool anima_intro = false;
  uint64_t campo_trad7db = ~uint64_t(0);
  uint32_t actor_animado[2]{};
  anim7db::Reloj reloj_intro;
  int paso_anim_ee = -1, lineas_ee[2] = {-1,-1};
  float cuerpo_ee[2] = {-1.f,-1.f};
  int codigo_ee[2]{};
  uint64_t campo_velo_7db = 0;   // t_dc (ya incrementado) al recibir I: paisaje 876/0
  int apertura_trad = -1, voces_apertura = 0;
  int ent = 0;                    // s8: cuadros de la entrada de la escena (#AMC amc_entrada); 0 = sin ella
  int k = 0;                      // contador de las filas de poses (QteMg_PlayHitPoses: +0x2F4)
  bool aviso_bien = false, auto_ = false, fallos = false, machaca = false, pierde = false;   // pierde: IW-FX-04, el piloto no pulsa los carriles (derrota)   // IW-FX-04: machaca = choque a 1 pulso cada 2 cuadros   // prueba: fallos = deja pasar 1 de cada 5 del carril 1
  float pts[6] = {}, mult[6] = {};
  uint16_t antes = 0, pendiente = 0;
  int32_t inyecta = 0;   // prueba: botones de Xbox que se pulsan ahora
  int inyecta_t = 0;
  P::Imagen ready, start, hit, miss, clear, failed;   // DATA_USA 5/1 t08 t00 t07 t02 t09 t10
  P::Imagen objetivo;                                 // DATA_USA 0/1 t10: aro rojo y amarillo con la flecha
  P::Imagen aro, relleno, barra;                       // DATA_USA 6/0 t00, t01, t15 (HUD de P4)
  P::Imagen barra06;                                  // DATA_USA 4/0 t42 (480x8): guia translucida de NB006
  P::Imagen botones06, objetivo06;   // solIW3: glifos y objetivo propios de NB006 (4/0 t01/t02)
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
  anim::ama::Obra resultado_ama;
  std::vector<P::Imagen> resultado_atlas;
  // DS156: fila RANK del panel de dos carriles. QteMg_FinalizeResult (EU 0x32F0F0) escribe la fila de rango (tipo 2,
  // +0x40: rank +0x48 y premio +0x4C) y el original la ensena entre TOTAL SCORE y EARNED. Rotulo y letra del mismo
  // #AMA que el panel del mapa (334/1 t17 fila 1 "RANK" y t24 "C B A S Z").
  P::Imagen row_labels, row_ranks;
  iwame::ame::Efecto antenas;
  ps2::Modelo antena_modelo;
  std::vector<P::Imagen> antena_atlas;
  // IW-F15a (dbz3_iw_botones_aura_original): aura de cada rol (0 jugador, 1 rival) con sus filas del ELF y su atlas
  iwame::ame::AuraCuerpo aura[2];
  std::vector<P::Imagen> aura_atlas[2];
  float aura_tasa100[2][2]{};   // NB013: ambos emisores con actor+185C=100
  float aura_pr[2][20] = {}, aura_al[2][2][4] = {};
  std::vector<iwame::ame::Instancia> antena_fx;
  uint32_t antena_sem = 29;
  int antena_pendiente = -1;
  uint32_t antena_codigo = 0;
  anim::ama::Obra botones_ama;
  std::vector<P::Imagen> botones_atlas;
  int salida_dur = 1, destello[2] = {-1, -1}, aviso_carril = 0;
  std::array<uint32_t,20> luz06{};
  std::array<uint32_t,5> niebla06{};
  bool ambiente06 = false;
  iwame::ame::Efecto combo_ef;
  std::vector<iwame::ame::Instancia> combo_fx;
  // IW-FX-02: efectos de las poses de NB006 (Effect_DispatchCommandById desde las tablas de estados del ELF; op = hijo de 933)
  std::map<int, iwame::ame::Efecto> fx933;
  std::vector<ps2::Malla> fx_modelos;   // IW-FX-05: modelos de 933/1 por selector (particulas tipo 4, EU 0x2716E0)
  std::vector<iwame::ame::Instancia> fx_pose;
  int fx_paso[2] = {}, fx_cuenta = 0;
  int pose_previo[2] = {-1, -1};   // IW-FX-03: estado anterior (puertas tras 0x11/0x10 de NB013)
  // IW-FX-04 (resto de IW-F02): objetos UI del original (CampoBot/CampoDC) para los efectos de acierto/fallo
  dbz3::iwmips::ui::Tabla ui;
  float ui_t[dbz3::iwmips::ui::kN] = {};
  bool ui_ok = false;
  float campo_original = 1;
  iwiop::Voz combo_voz;
  // T2B (7/11): 0x7DC con la logica original (iw_mips nb014, cvar dbz3_iw_7dc_original)
  bool orig = false, ini_dc = true;
  int campo_dc = 0;
  bool trad_entrada = false, trad_ok = false;   // TRAD/6: la ronda es la del original arrancado desde el ELF
  int fondo_original = -1;   // IW-08: ultimo bit de escenario visible del original (0x4DB2CC bit 0)
  uint16_t dir_piloto = 0;
  int dir_piloto_t = 0;
  uint32_t semilla_dc = 0;
  uint64_t t_dc = 0, pedido_dc[2] = {};
  int llegaron_dc = 0;   // PUL-IW-FX-04: iconos resueltos (+0x2C + +0x30) del campo anterior: si bajan, ronda nueva y el piloto olvida lo pedido
};

int Azar();   // mas abajo
std::mutex g_mu;
std::shared_ptr<Juego> g_j;
std::atomic<bool> g_corre{false};
std::atomic<bool> g_start_apertura_7db{false};
std::atomic<uint16_t> g_raw12{0};   // botones (+12) tal como los leyo el juego (MandoBotones)
std::atomic<bool> g_raw_ok{false};
std::atomic<float> g_cam_alto{14}, g_cam_dist{62};
std::atomic<bool> g_cam_reset{true};
std::atomic<int> g_fase_a{0};   // s9: fase del minijuego A (0 espera, 1 READY, 2 juego, 3 fin) para Colocar
std::atomic<float> g_cam_lado{1};   // 1: la camara a un lado; -1: al otro (la pose final, de frente)   // lo que pide el panel a la camara (PasoBotones la acerca poco a poco)
// EU330410 -> 2EDC70/2EDBA0: delta10 grados/duracion5 en A y B;
// 2EDF00 resta primero y2EDDD0 repone despues. No son clips5 y10.
std::atomic<int> g_combo_zoom{-1};
std::atomic<float> g_combo_fov{-1},g_combo_delta{1};
std::atomic<int> g_chr_fade[2]{};
std::atomic<float> g_chr_color[2]{};
std::atomic<bool> g_chr_activo[2]{};
std::atomic<int> g_chr_total{15};
std::atomic<uint32_t> g_chr_paleta[2]{iwchrfade::kNeutro,iwchrfade::kNeutro};
// PUL-IW-FX-05: tinte de golpe IDENTICO al original (detalle en iw_tinte_original.h). Apagado: todo como antes.
REXCVAR_DEFINE_BOOL(dbz3_iw_tinte_original, true, "DBZ3/IW",
                    "IW: tinte de golpe como el original (QTE blanco 15 cuadros solo a quien recibe; Piccolo DC903C 20)");
static bool TinteIW() { static const bool v = REXCVAR_GET(dbz3_iw_tinte_original); return v; }
// RESCATE IW-F15a: voz del rotulo CLEAR!/FAILED! de 7DB/7DC como el original (s_MSN_CV_ReadyStart(1/2)). Apagado: nada cambia.
REXCVAR_DEFINE_BOOL(dbz3_iw_botones_voz_resultado, true,  // 12/11 encendido de serie (HOJA_DECISIONES_0800 A)
    "DBZ3/IW",
                    "IW 7DB/7DC: voz del rotulo CLEAR!/FAILED! como el original (Menu_CharVoice_PlayA_/PlayB_, ELF EU)");
static bool VozResultadoIW() { static const bool v = REXCVAR_GET(dbz3_iw_botones_voz_resultado); return v; }
// IW-F15a: aura de 7DB/7DC como el original: Player_InitAuraAndWeatherEffects EU 0x2E8670 -> Auras EU 0x1D4680 (fila
// EU 0x448840 + 42 personaje + 7 forma; parametros 0x448460, alfas 0x448780; #AME del paquete 0x449D80) con Aura_UpdateAll /
// Aura_DrawAll (iw_ame.h AuraPaso/AuraMalla, el mismo consumidor que los campos). Sustituye a la de B3: sus efectos del
// bloque de aura se sueltan cada cuadro. Apagado: la de B3, como antes.
REXCVAR_DEFINE_BOOL(dbz3_iw_botones_aura_original, true,  // 12/11 encendido de serie (POST: ki 0 del minijuego, cotejado con G02)
                    "DBZ3/IW",
                    "IW 7DB/7DC: aura SSJ del original (rayos #AME del paquete del personaje) en lugar de la de B3");
static bool AuraIW() { static const bool v = REXCVAR_GET(dbz3_iw_botones_aura_original); return v; }

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
  ::dbz3::original::Fuente fuente() const { return eu ? ::dbz3::original::Fuente::IwEu : ::dbz3::original::Fuente::IwUsa; }
  const uint8_t* Off(uint32_t a) const {
    const size_t o = size_t(a) - 0x100000 + 0x80 + (eu ? (a < 0x47D000 ? 0x800 : 0x7B0) : 0);
    return o + 0x400 <= e.size() ? e.data() + o : nullptr;
  }
};
bool AbrirElfIW(ElfIW& x, std::string& error) {
  x.e = P::LeerElf(P::BuscarIW());
  x.eu = ::dbz3::original::FuenteIwElf(x.e.size()) == ::dbz3::original::Fuente::IwEu;   // SLES_553.47 = 3786800 B, SLUS_218.42 = 3784752 B
  if (x.e.empty()) error = "sin el ejecutable de Infinite World (SLUS_218.42 o SLES_553.47)";
  return !x.e.empty();
}

// Tablas del ELF de IW del jugador (little endian; direccion = desplazamiento - 0x100000 + 0x80).
bool LeerElf(Juego& j, std::string& error) {
  ElfIW elf;
  if (!AbrirElfIW(elf, error)) return false;
  j.campo_original=::dbz3::original::Delta(elf.fuente());
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
      // eIW7: en la ISO EU la logica del QTE corre a 50 campos/s y los huecos de la tabla son campos (medido en PCSX2: 40 campos =
      // 0,80 s), mientras el avance del icono lleva x1,2 (misma velocidad en px/s). El port corre a 60 pasos/s: sin esto los iconos
      // salian 1,2 veces mas juntos y el minijuego duraba un 20 % menos. DBZ3_IW_QTE_NTSC=1 lo desactiva (USA = 60 campos/s, tal cual).
      static const bool ntsc = std::getenv("DBZ3_IW_QTE_NTSC") != nullptr;
      if (!ntsc && j.c[l].hueco[i] > 0) j.c[l].hueco[i] = int(std::lround(::dbz3::original::CamposAPasos(float(j.c[l].hueco[i]), elf.fuente())));
    }
  }
  if (j.poses == &kPosesNB013 || j.poses == &kPosesNB006) {
    const uint32_t tabla = j.poses == &kPosesNB013 ? 0x483C60u : 0x485320u;
    for (int l = 0; l < 2; ++l) {
      const auto* especiales = off(tabla + uint32_t(j.dif)*0x70 + uint32_t(l)*0x32);
      if (!especiales) { error = "ELF corto (celdas especiales)"; return false; }
      std::memcpy(j.especiales[l], especiales, 50);
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
// IW-FX-05: EU 0x2716E0 Effect_DrawModelParticles -> FUN_00138830 con el selector +0x5C del nodo (la misma tabla
// de 933/1 que las antenas: fila +0x14 -> +4, 16 B por selector, +8 = ejes de la variante AMG). Indice = selector.
void CargarModelosFx(const std::vector<uint8_t>& amo, std::vector<ps2::Malla>& out) {
  auto u = [&](size_t o) { uint32_t v = 0; if (o + 4 <= amo.size()) std::memcpy(&v, amo.data()+o, 4); return v; };
  out.clear();
  for (uint32_t sel = 0; sel < 32; ++sel) {
    const uint32_t ejes = u(u(u(0x14) + 4) + sel * 16 + 8);
    int variante = -1;
    for (uint32_t i = 0; i < u(0x18) && i < 64; ++i) {
      const uint32_t a = u(u(0x1C) + i * 4);
      if (a + u(a + 0x14) == ejes) variante = int(i);
    }
    if (variante < 0) break;
    out.emplace_back();
    ps2::LeerAmo(amo, variante, out.back(), nullptr, true);
  }
  REXLOG_INFO("dbz3 iwfx05: modelos de efecto 933/1: {} selectores", out.size());
}
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
    j.resultado_ama.Cargar(P::HijoAmb(e5,4));
    for (int i=0,n=P::CuantasAmt(r5);i<n;++i) j.resultado_atlas.push_back(P::TexturaAmt(r5,i));
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
    if (std::getenv("DBZ3_TEST_IWMINI"))   // medidas del HUD de IW (4/0) en el log de la prueba
      REXLOG_INFO("dbz3 iw hud: banda {}x{} aro {}x{} marco {}x{} verde {}x{}", j.banda.w, j.banda.h, j.aro4.w,
                  j.aro4.h, j.marco4.w, j.marco4.h, j.verde4.w, j.verde4.h);
    // Siluetas de cada par, del atlas DATA_USA 4/0 (h4): NB013 cotejado con la captura del original (Vegeta t43 a la
    // izquierda, Trunks t44 a la derecha). Las otras dos parejas se sacan del propio atlas: NB006 = t47 (Gohan nino,
    // 48x72, la figura mas baja) y t48 (Piccolo, 48x64, cabeza con el turbante), NB014 = t39 (Goku, 72x96, pelo en pico)
    // y t49 (Gohan adolescente, 72x96). t50 (80x88) no es de estos tres pares y no se pinta.
    // eIW5: los objetos AMA 4/1 de feedback fijan que figura es de cada juego (0x31-33 t39, 0x34-36 t43, 0x37-3C t44, 0x3D-42 t47,
    // 0x4B-4D t48, 0x4E-53 t49; QteMg_PlayHitFeedbackUi EU 0x32ED60): NB014 = t39/t47, NB006 = t48/t49. DBZ3_IW_SIL_VIEJAS=1: antes.
    static const bool vieja_sil = std::getenv("DBZ3_IW_SIL_VIEJAS") != nullptr;
    const int s0 = j.m->poses == &kPosesNB013 ? 43 : j.m->poses == &kPosesNB006 ? (vieja_sil ? 47 : 48) : 39;
    const int s1 = j.m->poses == &kPosesNB013 ? 44 : j.m->poses == &kPosesNB006 ? (vieja_sil ? 48 : 49) : (vieja_sil ? 49 : 47);
    j.sil[0] = P::TexturaAmt(h4, s0);
    j.sil[1] = P::TexturaAmt(h4, s1);
    // caras de t53 (3x3 de 64): 0 Goku SSJ, 1 Vegeta SSJ, 2 Gohan joven SSJ, 3 Trunks SSJ, 4 Piccolo, 5 Krilin,
    // 6 Gohan nino, 7 Goku (cotejado con nb013_030 / nb006_040: las de los aros de IW)
    j.caras = P::TexturaAmt(h4, 53);
    if (std::getenv("DBZ3_TEST_IWMINI"))
      REXLOG_INFO("dbz3 iw hud: sil {}x{} / {}x{} caras {}x{}", j.sil[0].w, j.sil[0].h, j.sil[1].w, j.sil[1].h,
                  j.caras.w, j.caras.h);
    const bool b13 = j.m->poses == &kPosesNB013, b06 = j.m->poses == &kPosesNB006;
    j.cara[0] = b13 ? 1 : b06 ? 6 : 0;
    j.cara[1] = b13 ? 3 : b06 ? 4 : 2;
    if (std::getenv("DBZ3_TEST_IWMINI"))   // T1-IW 11/11: retratos rubios del marcador de 7D4 (solo en pruebas)
      REXLOG_INFO("dbz3 t1iw: retratos cara {} / {} (b13 {} b06 {})", j.cara[0], j.cara[1], b13, b06);
    {
      j.barra06 = P::TexturaAmt(h4, 0x2A);   // guia translucida, no el rectangulo rojo provisional
      j.botones06 = P::TexturaAmt(h4, 1);
      j.objetivo06 = P::TexturaAmt(h4, 2);
      j.botones_ama.Cargar(P::HijoAmb(usa.Entrada(4), 1));
      for (int i = 0, n = P::CuantasAmt(h4); i < n; ++i)
        j.botones_atlas.push_back(P::TexturaAmt(h4, i));
      if (j.botones_ama.Valida() && std::getenv("DBZ3_TEST_AMA_DUMP"))   // eIW5: volcado temporal de entradas con t38/17/18/19
        for (uint32_t id = 0; id < 0x100; ++id) {
          const int nodo = j.botones_ama.Objeto(id);
          if (nodo < 0) continue;
          auto arbol = anim::ama::CrearArbol(j.botones_ama, nodo, 0.0f);
          anim::ama::Contexto ctx;
          std::vector<anim::ama::Entrada> en;
          anim::ama::ActualizarNodo(arbol, arbol.raiz, ctx, en);
          for (const auto& e : en)
            REXLOG_INFO("eIW5 ama obj {:X} tex {} rect {:.1f} {:.1f} {:.1f} {:.1f} uv {:.0f} {:.0f} {:.0f} {:.0f} giro {:.1f} pos {:.1f} {:.1f} {:.1f} {:.1f} fl {:X} md {}", id, e.textura,
                        e.rect[0], e.rect[1], e.rect[2], e.rect[3], e.uv[0], e.uv[1], e.uv[2], e.uv[3], e.giro, e.pos[0], e.pos[1], e.pos[2], e.pos[3], e.flags, e.modo);
        }
      if (j.botones_ama.Valida()) {   // IWFIN/IW-F14: duraciones de la UI para el UI_UpdateFrame del interprete
        std::string ds;
        for (uint32_t id = 0; id < 256; ++id) {
          const int nodo = j.botones_ama.Objeto(id);
          dbz3::iwmips::ui::g_dur[id] = nodo < 0 ? -1.f : float(anim::ama::MaxCuadros(anim::ama::CrearArbol(j.botones_ama, nodo)));
          if (nodo >= 0) { char t[24]; std::snprintf(t, sizeof t, "%X:%.0f ", id, dbz3::iwmips::ui::g_dur[id]); ds += t; }
        }
        dbz3::iwmips::ui::g_dur_ok = true;
        if (std::getenv("DBZ3_TEST_IWMINI")) REXLOG_INFO("dbz3 iwfin ui duraciones: {}", ds);
      }
      if (j.botones_ama.Valida())
        j.salida_dur = std::max(1, int(anim::ama::MaxCuadros(anim::ama::CrearArbol(j.botones_ama, j.botones_ama.Objeto(0x0C)))));
    }
  }
  if (j.poses == &kPosesNB014 || j.poses == &kPosesNB013) {
    P::AfsPs2 cmn;
    if(!cmn.Abrir(P::BuscarIW(),"DATA_CMN.AFS")) { error="sin banco de efectos de combo IW";return false; }
    const auto amb=cmn.Entrada(933),amt=P::HijoAmb(amb,0);
    j.combo_ef=iwame::ame::Leer(P::HijoAmb(amb,15),true);
    j.combo_voz=iwiop::Leer(cmn.Entrada(954),0x21);
    if (std::getenv("DBZ3_TEST_QTE_LOG")) {   // eIW7: que tipos y mezclas lleva el efecto 0x1D del combo
      int tp[6] = {}, es[6] = {};
      auto cuenta = [&](const iwame::ame::Nodo& n) { ++tp[std::clamp(n.tipo, 0, 5)]; ++es[std::clamp(n.estado, 0, 5)]; };
      for (const auto& g : j.combo_ef.grupos) { cuenta(g.emisor); for (const auto& n : g.parts) cuenta(n); }
      for (const auto& n : j.combo_ef.raices) cuenta(n);
      REXLOG_INFO("dbz3 eIW7 combo nodos: tipo0..5 {} {} {} {} {} {} estado0..5 {} {} {} {} {} {}", tp[0], tp[1], tp[2], tp[3], tp[4], tp[5],
                  es[0], es[1], es[2], es[3], es[4], es[5]);
    }
    for(int i=0,n=P::CuantasAmt(amt);i<n;++i) j.antena_atlas.push_back(P::TexturaAmt(amt,i));
    REXLOG_INFO("dbz3 iw combo9: billboard0F=933/15, {} grupos; atlas933/0 {}; reloj original {}",
                j.combo_ef.grupos.size(),j.antena_atlas.size(),j.campo_original);
    // IW-FX-03: efectos de las poses de NB013/NB014 (kFxNB013/kFxNB014), los mismos hijos de 933
    for (int op : {13, 14, 20, 21, 22, 30, 31, 32, 33, 34, 35, 36, 41}) j.fx933[op] = iwame::ame::Leer(P::HijoAmb(amb, op), true);
    CargarModelosFx(P::HijoAmb(amb, 1), j.fx_modelos);   // IW-FX-05: 933/20 y 32 llevan particulas de modelo
  }
  if (j.poses == &kPosesNB006) {
    ElfIW elf;
    if (!AbrirElfIW(elf, error)) return false;
    auto palabra = [&](uint32_t a) { uint32_t v=0; if (const auto* p=elf.Off(a)) std::memcpy(&v,p,4); return v; };
    const uint32_t li = palabra(0x44A3CC + 4 * 0x5C), ni = palabra(0x44A3D0 + 4 * 0x5C);
    if (li >= 64 || ni >= 64) { error="tabla de ambiente de IW fuera de rango"; return false; }
    for (uint32_t i=0;i<20;++i) j.luz06[i]=palabra(0x4497A0 + li*80 + i*4);
    for (uint32_t i=0;i<5;++i) j.niebla06[i]=palabra(0x449F20 + ni*20 + i*4);
    j.ambiente06 = true;
    P::AfsPs2 cmn;
    if (!cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS")) {
      error = "sin DATA_CMN.AFS para las antenas de Piccolo";
      return false;
    }
    const auto amb = cmn.Entrada(933), amt = P::HijoAmb(amb, 0);
    j.antenas = iwame::ame::Leer(P::HijoAmb(amb, 29), true);
    // EU138830 ->139DC0:5 es el SELECTOR del nodo, no indice de AMG.
    // Tabla del nodo0:selector5 -> ejes de AMG3 (violeta); clamping5 aAMG4
    // dibujaba una llama amarilla de otro efecto.
    const auto amo = P::HijoAmb(amb, 1);
    auto u = [&](size_t o) { uint32_t v = 0; if (o + 4 <= amo.size()) std::memcpy(&v, amo.data()+o, 4); return v; };
    const uint32_t ejes = u(u(u(0x14) + 4) + 5 * 16 + 8);
    int variante = -1;
    for (uint32_t i = 0; i < u(0x18); ++i) {
      const uint32_t a = u(u(0x1C) + i * 4);
      if (a + u(a + 0x14) == ejes) variante = int(i);
    }
    if (variante >= 0) ps2::LeerAmo(amo, variante, j.antena_modelo.malla, nullptr, true);
    CargarModelosFx(amo, j.fx_modelos);   // IW-FX-05: Masenko acertado 933/24 (selector 10 = AMG 4), 20 y 28
    ps2::V3 minimo{1e9f,1e9f,1e9f}, maximo{-1e9f,-1e9f,-1e9f};
    for (const auto& tr:j.antena_modelo.malla.tris) for (const auto& v:tr.p) {
      minimo={std::min(minimo.x,v.x),std::min(minimo.y,v.y),std::min(minimo.z,v.z)};
      maximo={std::max(maximo.x,v.x),std::max(maximo.y,v.y),std::max(maximo.z,v.z)};
    }
    REXLOG_INFO("dbz3 solIW4: modelo selector5 -> AMG{}, limites ({:.2f},{:.2f},{:.2f})..({:.2f},{:.2f},{:.2f})",variante,
                minimo.x,minimo.y,minimo.z,maximo.x,maximo.y,maximo.z);
    for (int i = 0, n = P::CuantasAmt(amt); i < n; ++i)
      j.antena_atlas.push_back(P::TexturaAmt(amt, i));
    REXLOG_INFO("dbz3 solIW4: antenas IW933/29: {} raices, modelo selector5 {} triangulos, atlas {}",
                j.antenas.raices.size(), j.antena_modelo.malla.tris.size(), j.antena_atlas.size());
    if (j.antenas.raices.size() != 6 || !j.antena_modelo.malla || j.antena_atlas.size() <= 62) {
      error = "recurso original de las antenas incompleto (933/29, 933/1, 933/0)";
      return false;
    }
    // IW-FX-02: los demas efectos de las poses de NB006 (Masenko 25, chispas 12/14/37, ...): mismos hijos de 933
    for (int op : {12, 14, 20, 23, 24, 25, 26, 27, 28, 37, 38}) {
      j.fx933[op] = iwame::ame::Leer(P::HijoAmb(amb, op), true);
      REXLOG_INFO("dbz3 iwfx02: 933/{}: {} grupos, {} raices", op, j.fx933[op].grupos.size(), j.fx933[op].raices.size());
    }
  }
  return true;
}
// IW-F15a: Auras EU 0x1D4680 para los dos luchadores (pj = personaje de IW = de B3, forma de B3 = la de la tabla de IW).
// +0x59 elige los #AME del paquete (k -> 2k-3, 2k-2; 0/1 sin aura), +0x5A la fila de parametros y +0x5B/+0x5C las de alfa.
void CargarAura(Juego& j, const int (&pj)[2], const int (&forma)[2]) {
  if (!AuraIW()) return;
  namespace ame = iwame::ame;
  ElfIW elf;
  std::string error;
  P::AfsPs2 cmn;
  if (!AbrirElfIW(elf, error) || !cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS")) {
    REXLOG_WARN("dbz3 r14: aura original: sin ELF o DATA_CMN de IW");
    return;
  }
  auto eu = [&](uint32_t a) { return elf.Off(a - 0x800); };   // ElfIW::Off toma la direccion USA (EU - 0x800 aqui)
  auto f32 = [&](uint32_t a) { float v = 0; if (const auto* p = eu(a)) std::memcpy(&v, p, 4); return v; };
  for (int r = 0; r < 2; ++r) {
    if (pj[r] < 0 || pj[r] >= 48 || forma[r] < 0 || forma[r] > 5) continue;
    const uint8_t* fila = eu(0x448840 + 42 * uint32_t(pj[r]) + 7 * uint32_t(forma[r]));
    int32_t pq = -1;
    if (const auto* p = eu(0x449D80 + 4 * uint32_t(pj[r]))) std::memcpy(&pq, p, 4);
    if (!fila || fila[0] < 2 || fila[1] > 9 || fila[2] > 5 || fila[3] > 5 || pq < 0) continue;
    for (uint32_t i = 0; i < 20; ++i) j.aura_pr[r][i] = f32(0x448460 + 0x50 * fila[1] + 4 * i);
    // Modo minijuego 2: EU 2E8030 -> 1BBFD0 -> 1BB9A0 -> 2E8B30.
    // Conservar la tasa para ki 100; el camino anterior sigue usando solo la base.
    j.aura_tasa100[r][0] = j.aura_pr[r][0] + j.aura_pr[r][1];
    j.aura_tasa100[r][1] = j.aura_pr[r][10] + j.aura_pr[r][11];
    j.aura_pr[r][1] = j.aura_pr[r][11] = 0;
    for (uint32_t e = 0; e < 2; ++e)
      for (uint32_t i = 0; i < 4; ++i) j.aura_al[r][e][i] = f32(0x448780 + 0x10 * fila[2 + e] + 4 * i);
    const auto ef = P::HijoAmb(cmn.Entrada(size_t(pq)), 1);
    const auto a1 = ame::Leer(P::HijoAmb(ef, uint32_t(2 * fila[0] - 3)), true);
    const auto a2 = ame::Leer(P::HijoAmb(ef, uint32_t(2 * fila[0] - 2)), true);
    if (a1.raices.empty() || a2.raices.empty()) continue;
    auto& a = j.aura[r];
    a.n[0] = a1.raices[0], a.n[1] = a2.raices[0];
    a.pr = j.aura_pr[r], a.al[0] = j.aura_al[r][0], a.al[1] = j.aura_al[r][1];
    const auto at = P::HijoAmb(ef, 0);
    for (int i = 0, n = P::CuantasAmt(at); i < n; ++i) j.aura_atlas[r].push_back(P::TexturaAmt(at, i));
    REXLOG_INFO("dbz3 r14: aura original rol {}: personaje {} forma {} fila ({},{},{},{}) paquete {} atlas {}", r, pj[r],
                forma[r], fila[0], fila[1], fila[2], fila[3], pq, j.aura_atlas[r].size());
    for (const auto& n : a.n)
      REXLOG_INFO("dbz3 r14: aura original nodo tipo {} tex {} estado {} flags {:X} color {:.2f} {:.2f} {:.2f} {:.2f} / {:.2f} {:.2f} {:.2f} {:.2f} / "
                  "{:.2f} {:.2f} {:.2f} {:.2f} / {:.2f} {:.2f} {:.2f} {:.2f}", n.tipo, n.tex, n.estado, n.flags, n.f[12], n.f[13],
                  n.f[14], n.f[15], n.f[16], n.f[17], n.f[18], n.f[19], n.f[20], n.f[21], n.f[22], n.f[23], n.f[24], n.f[25],
                  n.f[26], n.f[27]);
  }
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
  if (&p==&kPosesNB013 && codigo==0x57A) return 0xF20; // copia sin yaw; NB014 conserva su alias anterior
  const uint32_t cmn = codigo >= 0x57A && codigo <= 0x57D ? 915u : uint32_t(p.cmn[rol]);   // las de espera son de 915
  return dbz3::anim::CodigoQte(cmn, codigo);
}
// El luchador del rol (0 jugador, 1 rival) del combate en curso (0 si no hay).
uint32_t Luchador(int rol) {
  uint8_t* base = Base();
  const uint32_t mgr = base ? REX_LOAD_U32(kMgrPtr) : 0;
  return mgr ? mgr + kX0 + kXPaso * uint32_t(rol) : 0;
}
extern float g_origen[3];
// Pos_GetNodeVec (EU 0x372250): nodo logico `nodo` del luchador = hueso *(short*)(+0xB7C + 2*nodo) de la tabla
// *(+0xB78)+0x408 (en B3 +0xB88/+0xB8C). Matriz en el mundo de IW (sin g_origen).
// IW-F02: una animacion del #AMA 4/1 en (x, y) del espacio de la UI del original (600 x 448, estirado a 640 como los iconos).
// El reloj no se corta al acabar la animacion: el original la sigue pintando (canales con tope) mientras el objeto este
// visible. boton >= 0: el glifo del icono (textura 1, EU373630); con iconos Xbox/Nintendo se pinta el del ajuste en su sitio.
template <class At>
void DibujarAmaB(ImDrawList* dl, At at, const anim::ama::Obra& ama, const std::vector<ps2::Tex>& tx, int id, float t, float x,
                 float y, int boton = -1) {
  if (!ama.Valida()) return;
  const int nodo = ama.Objeto(uint32_t(id));
  if (nodo < 0) return;
  auto arbol = anim::ama::CrearArbol(ama, nodo, t);
  anim::ama::Contexto ctx;
  ctx.pos[0] = x;
  ctx.pos[1] = y;
  std::vector<anim::ama::Entrada> entradas;
  anim::ama::ActualizarNodo(arbol, arbol.raiz, ctx, entradas);
  auto p = [&](float qx, float qy) { return at(qx * kW / 600.0f, qy); };
  const bool ps = dbz3::settings::InputGlyphs() == "playstation";
  // R23 IW-05 (7D4): Sprite_DrawEntry EU 0x1B2E00 elige ALPHA por el modo de la entrada (+0x08): 1 = 0x44 (alfa normal),
  // 2 = 0x48 (Cs*As + Cd, suma), 3 = 0x42 (Cd - Cs*As, resta); 0 = sin ABE. Antes todo iba con alfa: los discos de los
  // iconos (modo de suma sobre el panel azul) salian oscuros en vez de azul-violeta.
  int mezcla = 1;
  auto poner_mezcla = [&](int m) {
    if (m == 0) m = 1;
    if (m == mezcla) return;
    mezcla = m;
    dl->AddCallback(m == 2 ? rex::ui::kImDrawBlendAdditive : m == 3 ? iwgs::Resta : rex::ui::kImDrawBlendAlpha, nullptr);
  };
  for (auto e : entradas) {
    if (e.SinTextura() || e.textura >= tx.size() || !tx[e.textura]) continue;
    poner_mezcla(int(e.modo & 3));
    anim::ama::Quad q;
    if (boton >= 0 && e.textura == 1) {
      if (!ps) {
        anim::ama::DibujarEntrada(e, q);
        const ImVec2 a = p(q.xy[0][0], q.xy[0][1]), b = p(q.xy[2][0], q.xy[2][1]);
        S::Glyph(dl, kNombre[boton & 15], ImVec2(std::min(a.x, b.x), (a.y + b.y) / 2), std::fabs(b.x - a.x));
        continue;
      }
      const int celda = boton >= 12 ? (boton == 15 ? 0 : boton == 12 ? 1 : boton == 13 ? 2 : 3)
                                   : (boton == 10 ? 0 : boton == 11 ? 1 : boton == 8 ? 2 : 3);
      e.uv[0] = float(celda * 32);
      e.uv[1] = boton >= 12 ? 64.0f : 100.0f;
      e.uv[2] = e.uv[0] + 32;
      e.uv[3] = e.uv[1] + 32;
    }
    anim::ama::DibujarEntrada(e, q);
    const auto& t0 = tx[e.textura];
    auto color = [&](int c) { return std::clamp(int(e.tinte[c] * 255 + e.rgba[c] + 0.5f), 0, 255); };
    dl->AddImageQuad(t0.id(), p(q.xy[0][0], q.xy[0][1]), p(q.xy[1][0], q.xy[1][1]), p(q.xy[2][0], q.xy[2][1]),
                     p(q.xy[3][0], q.xy[3][1]), ImVec2(q.uv[0][0] / t0.w, q.uv[0][1] / t0.h), ImVec2(q.uv[1][0] / t0.w, q.uv[1][1] / t0.h),
                     ImVec2(q.uv[2][0] / t0.w, q.uv[2][1] / t0.h), ImVec2(q.uv[3][0] / t0.w, q.uv[3][1] / t0.h),
                     IM_COL32(color(0), color(1), color(2), color(3)));
  }
  poner_mezcla(1);
}

bool MatrizNodo(uint8_t* base, int rol, int nodo, ps2::M34& out) {
  const uint32_t x = Luchador(rol);
  auto valido = [](uint32_t p) { return p >= 0x40000000u && p < 0xA0000000u - 4096; };
  if (!base || !x || nodo < 0 || nodo > 63) return false;
  const uint32_t mi = REX_LOAD_U32(x + 2952), anc = REX_LOAD_U32(x + 2956);
  if (!valido(mi) || !valido(anc)) return false;
  const uint32_t tb = REX_LOAD_U32(mi + 1032);
  const int idx = int16_t(REX_LOAD_U16(anc + 2 * uint32_t(nodo)));
  if (!valido(tb) || idx < 0 || idx > 255) return false;
  const uint32_t h = tb + uint32_t(idx) * 64;
  for (int i = 0; i < 3; ++i) {
    for (int c = 0; c < 3; ++c) out.m[c*4+i]=LoadF(base,h+uint32_t(i*16+c*4));
    out.m[i*4+3]=LoadF(base,h+48+uint32_t(i*4));
  }
  for (float f : out.m) if (!std::isfinite(f)) return false;
  return true;
}
// EU1F0100: NH (selector logico21), matriz completa hueso * objeto.
// Colocar fija IDENTIDAD + origen en el pase de dibujo deNB006. El bucle
// de combate vuelve a girar X entre pases; leer X desde la UI tomaba esa
// matriz transitoria (medida: yaw90, x2.855), distinta de la que se dibuja.
bool AnclaAntenas(uint8_t* base, ps2::M34& out) {
  if (!MatrizNodo(base, 1, 21, out)) return false;
  for (int i = 0; i < 3; ++i) out.m[i*4+3] += g_origen[i];
  return true;
}
void CuadroAntenas(Juego& j) {
  if (j.poses != &kPosesNB006) return;
  if (j.antena_pendiente >= 0) {
    anim::EstadoCuerpo e;
    if (anim::LeerEstadoCuerpo(Base(), Luchador(1), 0, e)) {
      if (e.codigo == j.antena_codigo && e.cuadro >= float(j.antena_pendiente) &&
          (j.antena_pendiente != 0 || e.cuadro <= 3)) {
        ps2::M34 ancla;
        if (AnclaAntenas(Base(), ancla)) {
          iwame::ame::Instancia in;
          in.ef = &j.antenas;
          in.modelos = true;
          j.antena_fx.push_back(std::move(in));
          REXLOG_INFO("dbz3 solIW4: antenas comando14 NH, pose 0x{:X}, cuadro {:.1f}, origen ({:.3f},{:.3f},{:.3f})",
                      e.codigo, e.cuadro, ancla.m[3], ancla.m[7], ancla.m[11]);
          j.antena_pendiente = -1;
        }
      }
    }
  }
  for (auto& f : j.antena_fx) iwame::ame::Cuadro(f, j.antena_sem);
  j.antena_fx.erase(std::remove_if(j.antena_fx.begin(), j.antena_fx.end(), [](const auto& f) { return !f.vivo; }), j.antena_fx.end());
}
// Una pose una vez y luego la espera de pelea en bucle; `espera` = true: solo la espera (bucle).
void Poner(const Poses& p, int rol, uint32_t codigo, bool bucle, uint32_t luego = 0, uint32_t tope = 0) {
  const uint32_t x = Luchador(rol), a = AliasPose(p, rol, codigo);
  if (!x || !a) return;
  if (bucle) dbz3::anim::Bucle(x, a);
  // EU 0x334530/0x336740 Char_PoseTickFinishAnim: las poses sin sucesora mantienen
  // el ultimo cuadro. Volver a la guardia de B3 cambiaba la pose durante el resultado.
  // La sucesora se ejecuta tambien en una pose sujeta. Sujetarla hasta ese
  // corte usa solo el reloj nativo (el respaldo no inventa otro avance).
  else dbz3::anim::Reproducir(x, a, true, tope, luego ? AliasPose(p, rol, luego) : 0);
}

// ---- Camaras de IW (familias A y B) ----
// UiAnim_Start(0, 0, clip, ...) del ELF no es una animacion de interfaz: reproduce un clip del #AMC de camara del minijuego
// (Camera_FinishMotionLoads: el hijo 2 del #AMB general de DATA_CMN 915, +0x7EC) con ojo / objetivo / giro / fov a 60 Hz
// (formato en docs/03_formatos/CAMARA_ACC.md), y el ultimo clip se queda en su ultimo cuadro. Los clips estan en el mundo de
// IW con el luchador en el origen mirando a +Z (MgPly: +0x40/+0x44/+0x48 = 0): aqui se suman a la posicion del luchador
// (con el rival-pivote delante, a +Z, para que mire igual) y el fov se escribe en mgr+0x1C4 (cine.cpp).
using ClipCam = dbz3::iwamc::Clip;   // dia 1A: lector y muestreo unicos (iw_amc.h)
std::vector<uint8_t> g_amc;
std::map<int, ClipCam> g_clips;
// eCAM 6/11: la presentacion de combate de B3 (estado 3: Goku y Raditz en guardia) se veia ~0,6 s antes de la camara de IW. En
// el original ese tramo es la pantalla de carga/negro. DBZ3_IW_SIN_CUBRIR_B3=1 = como antes.
// PAL 27DA90 / 27D610: el reloj de la transicion resta delta * 0,01 por campo.
// 7DB/gs.jsonl: RGB=FFFFFF, ALPHA=44, As de 128 a 0. Misma expansion que Pintura.
static constexpr int AlfaCampo7DB(uint64_t edad, float delta) {
  const float peso = std::max(0.0f, 1.0f - float(edad) * (0.01f * delta));
  return std::min(255, 2 * int(128.0f * peso));
}
static int AlfaVelo7DB(const Juego& j) {
  if (j.clave != 0x7DB || !j.presenta_7db || !j.orig || j.fase != 0 || !anim7db::Habilitada()) return -1;
  if (dbz3::iwtrad::Activo()) {
    // El traductor pinta el velo original y prolonga sus dos bordes a los laterales.
    return j.trad_ok ? 0 : -1;
  }
  if (!j.campo_velo_7db) return -1;
  return AlfaCampo7DB(j.t_dc - j.campo_velo_7db, dbz3::iwmips::freezer::g_ntsc ? 1.0f : 1.2f);
}
static bool CubrirPresentacionB3(int est, const Juego* j = nullptr) {
  static const bool off = [] { const char* e = std::getenv("DBZ3_IW_SIN_CUBRIR_B3"); return e && e[0] == '1'; }();
  static int resto = 0;   // primeros cuadros del estado 4: la camara de IW aun no esta puesta (se ve el B3); negro DETRAS del HUD
  if (off) return false;
  // IWINI-12 (12/11): el original abre el minijuego fundiendo desde BLANCO sobre el paisaje (G02_0x7DB/7DC/7D4_INI:
  // blanco, casi blanco, velo; ~1,6 s). Aqui se veian 2 fotogramas de la camara de B3 con el luchador delante y un
  // fundido cruzado de ~0,3 s (4 cuadros de negro no bastaban). Blanco opaco 20 cuadros y 70 de fundido: medido
  // sobre fotos cada 0,8 s, no sobre el codigo. ponytail: afinar con la serie densa del original.
  constexpr int kOpaco = 20, kFundido = 70;
  if (est == 3) {
    resto = kOpaco + kFundido;
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, 255));
    return true;
  }
  if (est == 4 && j) {
    const int alfa = AlfaVelo7DB(*j);
    if (alfa >= 0) {
      resto = 0;
      if (alfa) ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(255, 255, 255, alfa));
      return false;
    }
  }
  if (est == 4 && resto > 0) {
    --resto;
    const int alfa = resto >= kFundido ? 255 : 255 * resto / kFundido;
    // R16 IWINI: el velo tambien cubre las capas propias del escenario.
    // En el fondo dejaba asomar el plano anterior durante el primer dibujo.
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(255, 255, 255, alfa));
  }
  return false;
}
std::atomic<bool> g_cb_videl{false};   // 31/10: MSN019 en juego: camara fija del original
std::atomic<int> g_cb_clip{-1}, g_cb_frame{0};   // clip en curso y cuadros desde que empezo (hilo de la interfaz -> del juego)
std::atomic<int> g_cb_fase_b{-1};  // GP2: fase real del controlador B, nunca del actor.
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
// El panel publica clip Y cuadro juntos. El hilo de dibujo no toca el EE.
struct MuestraIntro7 { int clip = -1; float f = 0; };
std::atomic<MuestraIntro7> g_cam_intro7{MuestraIntro7{}};

constexpr int kFinRes = 240;          // s8: el clip de resultado (0x191 / 0x1AF) dura 240 cuadros y despues el final (medido)

// Un #AMC de camara: lee los clips `idx` (indices dentro del #AMC) y los guarda como `base` + indice. Devuelve el nº de
// clips del #AMC (0 si no es un #AMC).
uint32_t LeerAmc(const std::vector<uint8_t>& amc, int base, const std::vector<int>& idx) {
  for (int id : idx) {
    ClipCam c;
    if (!g_clips.count(base + id) && dbz3::iwamc::Leer(amc, id, c)) g_clips[base + id] = std::move(c);
  }
  return dbz3::iwamc::NumClips(amc);
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
// Camara del clip en el cuadro f (FUN_00127280, iw_amc.h). fov/roll solo se tocan si el clip los trae.
void MuestraClip(const ClipCam& c, float f, float e[3], float t[3], float* fov, float* roll) {
  const dbz3::iwamc::Pose p = dbz3::iwamc::Muestrea(c, f);
  std::copy(p.ojo, p.ojo + 3, e);
  std::copy(p.obj, p.obj + 3, t);
  if (fov && p.hay_fov) *fov = p.fov;
  if (roll && p.hay_roll) *roll = p.roll;
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
  if (anim7db::Habilitada() && !dbz3::iwtrad::Activo()) {
    // Espejo: su UiAnim_Start es un puente; reinicio incluso al relanzar 186.
    const bool apertura = g_start_apertura_7db.load() &&
        ((clip == kCamEntrada && g_cb_amc_entrada.load() == 876) || clip == 0x47E || clip == 0x186);
    const bool sigue_ready = clip == 0x186 && g_cam_intro7.load().clip == clip;
    g_cam_intro7.store(apertura || sigue_ready ? MuestraIntro7{clip, 0} : MuestraIntro7{});
  }
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
// RITMO (R16): el #AMC del IW esta autorado por CAMPO PAL (50 por segundo) y `g_cb_frame` cuenta pasos del panel
// (1/60 s por defecto). Sin convertir, el recorrido de la camara y sus cortes iban un 20 % adelantados respecto al
// original. El paso de la logica manda: a 1/50 el clip va 1:1.
float CuadroClip(const ClipCam& c, int id = g_cb_clip.load()) {
  if (anim7db::Habilitada()) {
    const auto m = g_cam_intro7.load();
    if (m.clip >= 0 && m.clip == id) return std::min(m.f, float(c.nf - 1));
  }
  static const float kCampos = [] {
    const char* p = std::getenv("DBZ3_IW_QTE_PAL");
    return p && *p ? 1.0f : 50.0f / 60.0f;
  }();
  static const bool kD2Paisaje = [] {
    const char* p = std::getenv("DBZ3_IW_D2_PAISAJE");
    return p && std::strcmp(p, "1") == 0;
  }();
  // NB013 876/0: EU 0x2EE040 suma 1,2 por campo a 50 Hz (1 a 60 Hz). Solo el paisaje de la apertura V2.
  const bool paisaje = kD2Paisaje && g_start_apertura_7db.load() &&
                       g_cb_clip.load() == kCamEntrada && g_cb_amc.load() == 876;
  const float paso = paisaje ? (std::getenv("DBZ3_IW_QTE_PAL") ? 1.2f : 1.0f) : kCampos;
  const float f = float(g_cb_frame.load()) * paso;
  if (g_cb_bucle.load() && c.nf > 1) return std::fmod(f, float(c.nf));
  return std::min(f, float(c.nf - 1));
}
// IW-FX-02/04: pinta efectos de pose (en el espacio de los huesos) con la camara del minijuego; tambien PanelB (7DA).
void PintarFxPose(ImGuiIO& io, const std::vector<iwame::ame::Instancia>& fx_pose, const std::vector<ps2::Tex>& antena_tx_,
                const std::vector<ps2::Tex>& antena_suma_, const std::vector<ps2::Malla>* modelos = nullptr,
                const std::vector<ps2::Tex>* atlas_z = nullptr) {
  if (fx_pose.empty()) return;
  {
    ps2::Camara cam = ps2::CamaraCombate(Base(), io.DisplaySize);
    if (const auto it=g_clips.find(g_cb_clip.load()); it!=g_clips.end() && !it->second.fov.empty()) {
      float e_[3], t_[3];
      MuestraClip(it->second, CuadroClip(it->second), e_, t_, &cam.fov, nullptr);
    }
    for (const auto& in : fx_pose) {
      auto mundo = in;
      // Effect_DrawAmoMovingWorld (0x1F06A0) -> FUN_0013C690: giro de (0,0,1) a dir: h = |a + d|, q = (a x h, a . h)
      std::array<float, 4> q{0, 0, 0, 1};
      {
        ps2::V3 h{in.dir.x, in.dir.y, in.dir.z + 1};
        const float l = iwame::Largo(h);
        if (l > 1e-4f) { h = iwame::Por(h, 1 / l); q = {-h.y, h.x, 0, h.z}; }
      }
      ps2::M34 hueso;
      const bool pegado = in.hueso_rol >= 0;
      if (pegado && !MatrizNodo(Base(), in.hueso_rol, in.hueso_nodo, hueso)) continue;
      for (size_t g = 0; g < mundo.ps.size(); ++g) for (auto& p : mundo.ps[g]) {
        if (pegado) p.p = hueso.por(p.p);   // IW-FX-04: Effect_DrawAmoAttachedToBone, todo el efecto en el hueso
        else if (g == in.ef->grupos.size() && !in.ef->raices.empty()) p.p = iwame::Mas(iwame::ame::Rotar(q, p.p), in.origen);
        p.p.x += g_origen[0]; p.p.y += g_origen[1]; p.p.z += g_origen[2];
      }
      // IW-FX-LAVADO: la mezcla de cada nodo como el GS (EU 0x272FA0/0x273C10/0x274990: ALPHA = tabla 0x472480 por
      // +0xA0/+0xC0: 0x44 normal, 0x48 suma Cs*As+Cd, 0x42 resta, 0x06) con el alfa real de la textura. Sin esto la suma
      // se pintaba como mezcla normal (Cd*(1-A)+Cs*A): cada capa blanca tapaba la escena y el pico salia lavado.
      // IW-FX-LAVADO2: y los nodos con la bandera 0x1000000 contra la Z de los luchadores (atlas_z, ver iw_ame.h Pintar)
      iwame::ame::Pintar(mundo, cam, antena_tx_, antena_suma_, ImGui::GetBackgroundDrawList(), io.DisplaySize.y, true,
                         atlas_z);
      // IW-FX-05: particulas de modelo (EU 0x2716E0) con el atlas de las antenas (933/0). ponytail: pegadas a un hueso
      // solo se mueve su posicion (la orientacion no gira con el hueso); ningun efecto pegado lleva modelo hoy.
      if (modelos && in.modelos && !modelos->empty()) {
        const size_t n = iwame::ame::PintarModelos(mundo, cam, *modelos, antena_suma_, ImGui::GetBackgroundDrawList());
        static int avisos = 0;
        if (n && avisos < 4) { ++avisos; REXLOG_INFO("dbz3 iwfx05: modelos de efecto pintados {} triangulos (edad {:.0f})", n, in.edad); }
      }
    }
  }
}


// EU 0x32FB40 QteMg_UpdateResultPhase / 0x3520D0 Mg_UpdateResultPhase:
// el segundo plano empieza al acabar SU clip. NB006 usa su propio temporizador
// de 180 cuadros (0x3781B0), no el clip 0x9A de los puntos. NB014 malo dura 300.
int CuadrosResultado(const Juego& j, bool final = false) {
  if (!final && j.poses == &kPosesNB006) return 180;
  const Poses& p = *j.poses;
  const int clip = final ? (j.rango > 0 ? p.cam_fin_bien : p.cam_fin_mal)
                         : (j.rango > 0 ? p.cam_gana : p.cam_pierde);
  const auto it = g_clips.find(clip);
  if (it != g_clips.end()) return it->second.nf;
  if (!final) return kFinRes;
  return j.poses == &kPosesNB006 && j.rango > 0 ? 220
       : j.poses == &kPosesNB014 && j.rango == 0 ? 300 : kFinRes;
}
int InicioPanelResultado(const Juego& j) {
  // PINE NB014: la tabla aparece al terminar TAMBIEN la pose final5C9/5CA,
  // no al entrar en ella (EU32FB40: +C74 del actor, despues de FinalizeResult).
  return CuadrosResultado(j)+(j.poses==&kPosesNB013 ? 0 : CuadrosResultado(j,true));
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
  if (static const bool on = std::getenv("DBZ3_TEST_QTE_LOG") != nullptr; on)   // eIW5: contrasta con las poses del original
    REXLOG_INFO("dbz3 qte: golpe cuadro {} carril {} acierto {} k {}", j.cuadros, carril, acierto ? 1 : 0, k);
  // EU352D40: tambien NB013 ejecuta CHRFADE sobre quien recibe el golpe.
  if (TinteIW()) {
    if (j.poses == &kPosesNB013 && ((!acierto && carril != 0) || (acierto && carril == 0)))
      iwtinte::Qte(g_chr_paleta, g_chr_fade, g_chr_total, acierto ? 1 : 0, "NB013");
  } else if (j.poses == &kPosesNB013 && ((!acierto && carril != 0) || (acierto && carril == 0)))
    g_chr_total=15;g_chr_fade[acierto ? 1 : 0] = 15;
  if (carril == 2 && acierto) {
    // EU330410 / EU352D40: una sola accion conjunta, estados10/F del banco915.
    Poner(p, 0, 0x57C, false, p.pelea[0]);
    Poner(p, 1, 0x57D, false, p.pelea[1]);
    iwiop::Oir(j.combo_voz);
    // Consumidor comun de Effect_DispatchCommandById(1D): EU352D40 y
    // EU330410 piden el mismo efecto, no dos impactos independientes.
    g_combo_zoom=0;g_combo_fov=-1;g_combo_delta=j.campo_original;
    iwame::ame::Instancia in;
    in.ef=&j.combo_ef;in.mezcla_gs=true;
    in.origen={1.423900008f,41.864269257f,0};
    j.combo_fx.push_back(std::move(in));
    REXLOG_INFO("dbz3 iw combo9: comando1D -> billboard0F en (1.423900,41.864269,0), zoom A/B5/5");
  } else if (carril == 0) {
    Poner(p, 0, p.j_ataca[k], false, p.pelea[0]);
    Poner(p, 1, acierto ? p.r_herido[k] : p.r_esquiva[k], false, p.pelea[1]);
    SeGolpe(0, acierto);
  } else {
    if (j.poses == &kPosesNB006) {
      j.antena_pendiente = 0;
      j.antena_codigo = AliasPose(p, 1, p.r_ataca[k]);
    }
    Poner(p, 1, p.r_ataca[k], false, p.pelea[1]);
    Poner(p, 0, acierto ? p.j_esquiva[k] : p.j_herido[k], false, p.pelea[0]);
    SeGolpe(1, !acierto);
  }
  j.k = p.azar ? Azar() & 3 : (k + 1) & 3;
}

// Fallo del carril 1: el rival golpea (vida del jugador; la pose la pone PosesGolpe).
// EU 0x3301C0 QteMg_ApplyHitDamage
void Golpe(Juego& j) { j.vida[0] -= float(j.m->ataque_rival[j.dif]); }

// ---- Efectos de acierto/fallo (aviso del usuario del 8/11: solo funcionaban en 7DB) ----
// Original, por minijuego:
//   NB006 (7D4): Mg_ProcessQteHit 0x379500 -> al acertar UI_SetObjectField158AndEnable(0, carril+3, 0x2A) (destello en el
//     aro) y al icono 0x0C (salida); al fallar UI_SetObjectField158AndEnable(0x41A00000, 0xB+(slot%3)+carril*3, 7) (cruz/
//     chispas en el icono). Mg_ProcessLaneHits 0x37ADE0 hace lo mismo con el otro carril.
//   NB013 (7DB): Mg_SetClashStepPoses 0x352D40 -> s_UTL_CHRFADE sobre quien recibe el golpe (se engancha en el .inl y
//     llega como evento 'F'; ver PasoOriginalDB).
//   NB014 (7DC): QteMg_PlayHitPoses 0x330410 -> s_UTL_CHRFADE sobre quien recibe; QteMg_PlayHitFeedbackUi 0x32ED60 ->
//     objetos 6/7 con 0x40/0x42 (carril 0) y 0x33/0x32 (carril 1), 0x3E en el combo; reloj 0x1E.
// El aviso (HIT/MISS sobre la silueta) ya lo pinta Dibujar con los ids de arriba; aqui se disparan las tres cosas desde
// las rutas de logica ORIGINAL (que antes solo ponian aviso_t). Todo detras de dbz3_iw_efectos_original (apagado).
REXCVAR_DEFINE_BOOL(dbz3_iw_efectos_original, true, "DBZ3/IW",
                    "IW botones: efectos de acierto/fallo (aviso HIT/MISS, CHRFADE del que recibe y destello del aro) "
                    "tambien con la logica original (7D4/7DB/7DC); sin esto solo los ponia la logica del port");
static bool EfectosIW() { static const bool v = REXCVAR_GET(dbz3_iw_efectos_original); return v; }
// R23: Mg_UpdateClashPhase EU 0x353090 (decomp l.443381-443387): con objetivo - pulsos <= 0 pone +0x1AAC = 1 CADA cuadro,
// asi que desde ahi todos los pasos del choque usan la reaccion "con pulso" (0x1A/0x21), no solo el que sigue a un acierto.
REXCVAR_DEFINE_BOOL(dbz3_iw_choque13_original, true, "DBZ3/IW",
                    "IW NB013: en el choque, objetivo alcanzado = reaccion con pulso cada paso (+0x1AAC del original)");
static bool Choque13IW() { static const bool v = REXCVAR_GET(dbz3_iw_choque13_original); return v; }
REXCVAR_DEFINE_BOOL(dbz3_iw_videl_ready_original, true,  // 12/11 encendido de serie (HOJA_DECISIONES_0800 A)
    "DBZ3/IW",
                    "IW 0x7E1 Videl: el clip 0x5AA es la presentacion (sin tabla ni cartel) y la cuenta READY?/START! va "
                    "despues sobre el plano frontal fijo, como el original (G02_0x7E1_INI) (pruebas)");
}  // namespace
bool EfectosOriginalIW() { return EfectosIW(); }   // PUL-IW-FX-02: el mismo ajuste para iw_ame.h (disparo, vuelo, campos)
namespace {
// carril: 0/1 = el que pulso, 2 = combo; receptor: 0 jugador, 1 rival, -1 ninguno.
void EfectoGolpe(Juego& j, int carril, bool acierto, int receptor) {
  // 12/11 (usuario: «la parte de abajo no se ilumina ni pone las X»): el aviso de la silueta (QteMg_PlayHitFeedbackUi
  // EU 0x32ED60 / Mg_ProcessLaneHits) es del panel y va siempre; el resto (CHRFADE, destello) sigue tras el ajuste.
  j.aviso_t = 30;
  j.aviso_bien = acierto;
  j.aviso_carril = std::clamp(carril, 0, 2);
  if (!EfectosIW()) return;
  if (j.poses == &kPosesNB006) {          // el destello del aro (objeto 0x2A) solo lo lleva NB006
    if (acierto) j.destello[std::clamp(carril, 0, 1)] = 0;
    return;
  }
  if (j.poses == &kPosesNB013) return;   // NB013 solo hace CHRFADE en el choque: llega del original (evento 'F')
  if (j.poses == &kPosesNB014) {   // CHRFADE del que recibe (EU2EF640, argumentos 15/0,95)
    if (receptor < 0 || receptor > 1) return;
    if (TinteIW()) { iwtinte::Qte(g_chr_paleta, g_chr_fade, g_chr_total, receptor, "NB014"); return; }
    g_chr_paleta[receptor] = iwchrfade::kImpacto;
    g_chr_total = 15;
    g_chr_fade[receptor] = 15;
  }
}

void CerrarBotones(Juego& j);
void FinalizarBotones(Juego& j) {
  const int ac = j.c[0].aciertos+j.c[1].aciertos, ll=j.c[0].llegaron+j.c[1].llegaron;
  j.nota = ll ? int(float(ac)/float(ll)*100.0f) : 0;
  j.rango = 0;
  if (j.vida[0]>0)
    for (int r=5;r>=1;--r) if (j.nota>=kUmbral[r][0]) { j.rango=r; break; }
  CerrarBotones(j);
}

// Nota y rango ya puestos: premio, fase 3 y poses del resultado (QteMg_FinalizeResult).
void CerrarBotones(Juego& j) {
  const int ac = j.c[0].aciertos+j.c[1].aciertos, ll=j.c[0].llegaron+j.c[1].llegaron;
  // PAQ_5: cotejo del cierre del 0x7DC contra QteMg_FinalizeResult (no sustituye el premio).
  if (std::getenv("DBZ3_TEST_IW_7DC_CFG")) {
    dbz3::iwtrad::EstadoBot x;
    if (dbz3::iwtrad::LeerBot(0x7DC, x))
      REXLOG_INFO("dbz3 iwmini 7dc: cfg nivel {} puntos [{},{},{},{},{}]", x.cfg_nivel, x.cfg_puntos[0],
                  x.cfg_puntos[1], x.cfg_puntos[2], x.cfg_puntos[3], x.cfg_puntos[4]);
  }
  const float v=j.rango ? j.pts[j.rango]*j.mult[j.dif] : 100.0f;
  // NB013 Mg_ApplyRoundOutcome -> Math_RoundUp100 (EU 0x371FA0): centena mas cercana.
  j.puntos=(int(v)+(j.clave == 0x7DB ? 50 : 99))/100*100;
  j.zeni=j.puntos;
  j.saldo=P::LeerProgreso("iw_zeni",0)+j.zeni;
  j.fase=3;
  j.t=0;
  const Poses& p=*j.poses;
  const uint32_t ultimo=uint32_t(CuadrosResultado(j)-1);
  Poner(p,0,j.rango ? p.gana[0] : p.pierde[0],false,0,ultimo);
  Poner(p,1,j.rango ? p.gana[1] : p.pierde[1],false,0,ultimo);
  if (j.poses==&kPosesNB006 && !j.rango) {
    j.antena_pendiente=4;
    j.antena_codigo=AliasPose(p,1,0x2A0);
  }
  REXLOG_INFO("dbz3 r14: fin: aciertos {}/{} (carril 0 {}/{}, carril 1 {}/{}), vida {} / {}, nota {}, rango {}, puntos {}",
              ac,ll,j.c[0].aciertos,j.c[0].llegaron,j.c[1].aciertos,j.c[1].llegaron,
              int(j.vida[0]),int(j.vida[1]),j.nota,j.rango,j.puntos);
}

// T2B (7/11): un cuadro de 60 Hz de NB014 con la logica ORIGINAL (iw_mips nb014::g_campo), una vez por campo PAL como el
// 0x7DA. Del invitado: fases, aparicion y avance de los iconos de los dos carriles, carril activo, combo, juicio, danos,
// vidas, nota y rango. Aqui: dibujo, camaras (UiAnim_Start) y poses (Char_SetPoseState: estado de IW -> clip del #BSK con
// la tabla de Poses). false = el original no pudo correr: la partida sigue con el port.
// IW-FX-04 (resto de IW-F02): efectos de acierto/fallo de NB006/NB013/NB014 desde los objetos UI del original, en vez del
// aviso fijo de 30 cuadros: 0x2A en los aros 3/4 (QteMg_ProcessPlayerInput 0x330E30, Mg_ResolvePlayerInput 0x353A50), el
// golpe/esquiva de QteMg_PlayHitFeedbackUi 0x32ED60 en 6/7 (solo NB014) y 0x1F/0x20 de *_ApplyHitDamage (0x13-0x15).
bool EfectoUiA(const Juego& j, int k, uint32_t anim) {
  if (k == 3 || k == 4) return anim == 0x2A;
  if (k == 6 || k == 7) return j.poses == &kPosesNB014;
  if (k >= 0x13 && k <= 0x15) return anim == 0x1F || anim == 0x20;
  // 12/11: la X del fallo es otro objeto UI (QteMg_UpdatePlayerButtons EU 0x332270 / Mg_ProcessLaneHits): anim 7 desde
  // t 20 (0x41A00000) en el punto del aro donde se paso el icono, y anim 8 cuando el icono sale (x >= 495).
  if (anim == 7 || anim == 8) return true;
  return false;
}
void AvanzarUiA(Juego& j) {   // un campo: el reloj como AvanzarUiB (UI_UpdateFrame: +paso con el bit 4)
  namespace u = dbz3::iwmips::ui;
  j.ui = u::g_tabla;
  j.ui_ok = j.ui.n > 0;
  const float paso = dbz3::iwmips::freezer::g_ntsc ? 1.0f : 1.2f;
  for (int k = 0; k < j.ui.n; ++k) {
    const u::Obj& o = j.ui.o[k];
    if (o.puesto) {
      j.ui_t[k] = o.t0;
      if (static const bool traza = std::getenv("DBZ3_TEST_IWMINI") != nullptr; traza && EfectoUiA(j, k, o.anim))
        REXLOG_INFO("dbz3 iwfx04 ui {:X} obj={:X} anim={:X} t0={:.1f} pos600=({:.1f},{:.1f})", j.clave, k, o.anim, o.t0, o.x, o.y);
    }
    if (o.puesto != 2 && (o.flags & 5) == 5) j.ui_t[k] += paso;
  }
}
uint16_t PoseDC(const Poses& p, int rol, int n) {
  // IW-FX-02: en NB006 los estados altos no son los de NB014 (0x57C/gana/pierde): tablas de estados del ELF EU 0x49B5E0
  // (Gohan) y 0x49BB50 (Piccolo), el +0xB60 que escribe cada Char_EnterPoseState. 0x289 = Masenko, 0x28A su respuesta.
  if (&p == &kPosesNB006 && EfectosIW()) {
    static constexpr uint16_t kJ06[10] = {0x285, 0x286, 0x287, 0x286, 0x289, 0x5C9, 0x258, 0x29F, 0x2A9, 0x2AA};
    static constexpr uint16_t kR06[7] = {0x28A, 0x288, 0x5C9, 0x29E, 0x2A0, 0x2A9, 0x2AA};
    if (rol == 0 && n >= 0x10 && n <= 0x19) return kJ06[n - 0x10];
    if (rol == 1 && n >= 0xF && n <= 0x15) return kR06[n - 0xF];
  }
  if (rol == 0) {
    if (n == 0) return p.ini[0];
    if (n == 2) return p.listo[0];
    if (n == 3) return p.pelea[0];
    if (n >= 4 && n <= 15) { const int k = (n - 4) / 3, c = (n - 4) % 3; return c == 0 ? p.j_ataca[k] : c == 1 ? p.j_esquiva[k] : p.j_herido[k]; }
    if (n == 0x10) return 0x57C;
    if (n == 0x12) return p.gana[0];
    if (n == 0x13) return p.pierde[0];
  } else {
    if (n == 0) return p.ini[1];
    if (n == 1) return p.listo[1];
    if (n == 2) return p.pelea[1];
    if (n >= 3 && n <= 14) { const int k = (n - 3) / 3, c = (n - 3) % 3; return c == 0 ? p.r_ataca[k] : c == 1 ? p.r_esquiva[k] : p.r_herido[k]; }
    if (n == 0xF) return 0x57D;
    if (n == 0x11) return p.gana[1];
    if (n == 0x12) return p.pierde[1];
  }
  return 0;
}
// TRAD/6: los campos de dos carriles de la salida del port se toman del original arrancado desde el ELF
template <class S>
bool TomaOriginalBot(Juego& j, S& o, const dbz3::iwtrad::EstadoBot* vista = nullptr) {
  if (!(j.clave == 0x7DC || j.clave == 0x7DB || j.clave == 0x7D4) || !dbz3::iwtrad::Activo()) return false;
  dbz3::iwtrad::EstadoBot local;
  if (!vista && !dbz3::iwtrad::LeerBot(j.clave, local)) return false;
  const auto& x = vista ? *vista : local;
  if (j.presenta_7db && anim7db::Habilitada() && (j.fase == 0 || g_cam_intro7.load().clip == 0x186)) {
    const int id = x.cam_intro == 0 ? kCamEntrada : x.cam_intro;
    g_cam_intro7.store({id, x.cuadro_intro});
  }
  if (!j.trad_ok) { j.trad_ok = true; REXLOG_INFO("dbz3 r14: {:X}: la ronda es la del original arrancado desde el ELF", j.clave); }
  o.fase = x.fase; o.sub = x.sub; o.activo = x.activo; o.rango = x.rango; o.nota = x.nota;
  if constexpr (requires { o.choque_fase; }) {
    if (j.clave == 0x7DB) {
      if (j.anima_intro) {
        j.paso_anim_ee = x.paso_anim7db; std::copy_n(x.lineas7db, 2, j.lineas_ee);
        std::copy_n(x.cuadro7db, 2, j.cuerpo_ee);
        std::copy_n(x.codigo7db, 2, j.codigo_ee);
      }
      o.choque_fase = x.choque_fase; o.choque_lado = x.choque_lado;
      o.pulsos = x.pulsos; o.objetivo = x.objetivo;
      o.marca_indice = x.marca_indice; o.marca_lado = x.choque_lado; o.marca_y = x.marca_y;
      std::copy(x.marca_x, x.marca_x + 3, o.marca_x);
      o.qte_boton = x.choque_boton;
      if (std::getenv("DBZ3_TEST_IWMINI") && j.fase == 3)   // PAQ_4: las seis banderas de +0x2F8..+0x2FD
        REXLOG_INFO("dbz3 paq4: 7DB banderas w+2F8..2FD = {:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
                    x.banderas[0], x.banderas[1], x.banderas[2], x.banderas[3], x.banderas[4], x.banderas[5]);
      if (std::getenv("DBZ3_TEST_IWMINI") && x.sub == 3 && x.pulsos != j.pulsos)
        REXLOG_INFO("dbz3 soliw: choque original lado {} fase {} boton {} pulsos {} objetivo {}", x.choque_lado,
                    x.choque_fase, x.choque_boton, x.pulsos, x.objetivo);
    }
  }
  // IW-08: el choque de 7DB apaga el escenario (Mg_StartClashSequence EU 0x352A50 -> RAMSwitch_Background-1 0x1E10D0,
  // bit 0 de 0x4DB2CC) y solo pinta dos cuadros 2D (franja y centro; traza DBZ3_TEST_IW_QUAD_GRANDE). En B3 el mismo
  // efecto es G+0x4C bit 0: lo leen solo las funciones de dibujo del escenario (sub_820C37C0 y otras 12, que salen sin
  // dibujar), no los luchadores. (La pantalla negra de 093550 era de antes del choque: capturas 1,5 min antes del log.)
  if (x.fondo >= 0 && x.fondo != j.fondo_original) {
    j.fondo_original = x.fondo;
    REXLOG_INFO("dbz3 r14: {:X} original: escenario {}", j.clave, x.fondo ? "visible" : "oculto");
    dbz3::escenarios::OcultarFondo(x.fondo == 0);
  }
  o.vida[0] = x.vida[0]; o.vida[1] = x.vida[1];
  for (int l = 0; l < 2; ++l) {
    o.llegaron[l] = x.llegaron[l]; o.intentos[l] = x.intentos[l]; o.n_ic[l] = x.n_ic[l];
    if constexpr (requires { o.aciertos; }) o.aciertos[l] = x.aciertos[l];
    if constexpr (requires { o.golpes; }) o.golpes[l] = x.aciertos[l];
    for (int i = 0; i < x.n_ic[l]; ++i) { auto& d = o.ic[l][i]; d.k = x.ic[l][i].k; d.boton = x.ic[l][i].boton; d.x = x.ic[l][i].x; d.vivo = x.ic[l][i].vivo; d.fallado = x.ic[l][i].fallado; }
  }
  // R23 IW-06: camara y vibracion del choque desde el original traducido (antes la ruta traducida no mandaba eventos y
  // el choque se quedaba con la camara de la ronda); solo 7DB y con dbz3_iw_r23_eventos (la cola solo se llena asi)
  if constexpr (requires { o.choque_fase; })
  if (j.clave == 0x7DB && REXCVAR_GET(dbz3_iw_r23_eventos)) {
    int m = 0;   // la camara y la vibracion del espejo auxiliar no son las del original: fuera
    for (int k = 0; k < o.n_ev; ++k) if (o.ev[k].tipo != 'C' && o.ev[k].tipo != 'V') o.ev[m++] = o.ev[k];
    o.n_ev = m;
    for (char t = 0; o.n_ev < int(std::size(o.ev));) {
      int n = 0;
      if (!dbz3::iwtrad::TomarEvBot(t, n)) break;
      o.ev[o.n_ev++] = {t, n};
    }
  }
  if constexpr (requires { o.choque_fase; }) {
    if (j.presenta_7db && j.fase == 0) {
      // La fase publicada es del ELF entero; el espejo auxiliar no tiene su reloj de camara.
      o.n_ev = 0;
      auto ev = [&](char tipo, int n) { o.ev[o.n_ev++] = {tipo, n}; };
      if (x.fase == 0 && x.paso_apertura >= 1 && j.apertura_trad < 0) ev('I', 0);
      if (x.fase == 0 && x.paso_apertura == 3 && j.apertura_trad != 3) {
        ev('C', 0x47E); ev('J', 1); ev('R', 0);
      }
      if (x.fase == 1 && x.sub == 0) {
        ev('C', 0x10186); ev('J', 2); ev('R', 1);
      }
      j.apertura_trad = x.paso_apertura >= 1 ? x.paso_apertura : j.apertura_trad;
    } else if (j.anima_intro && j.fase == 1 && x.fase == 1 && x.sub <= 1) {
      const auto t = anim7db::Secuencia(x.fase, x.sub, x.paso_anim7db);
      int m = 0;
      for (int k = 0; k < o.n_ev; ++k) if (o.ev[k].tipo != 'C') o.ev[m++] = o.ev[k];
      o.n_ev = m;
      if (t > j.reloj_intro.tramo && (t == anim7db::Relanza1 || t == anim7db::Relanza2 || t == anim7db::Juego) &&
          o.n_ev < int(std::size(o.ev))) o.ev[o.n_ev++] = {'C', t == anim7db::Juego ? 0x187 : 0x186};
    }
  }
  static int ult = -1;
  const int clave = (x.fase * 16 + x.sub) * 64 + x.activo;
  if (clave != ult) { ult = clave; REXLOG_INFO("dbz3 r14: {:X} original: fase {} sub {} activo {} vida {:.0f} {:.0f} rango {} nota {:.1f}", j.clave, x.fase, x.sub, x.activo, x.vida[0], x.vida[1], x.rango, x.nota); }
  return true;
}

bool PasoOriginalDC(Juego& j) {
  if (j.clave == 0x7DC && dbz3::iwtrad::Activo() && dbz3::iwtrad::Arrancando()) return true;
  namespace nb = dbz3::iwmips::nb014;
  j.campo_dc += dbz3::iwmips::freezer::g_ntsc ? 6 : 5;
  if (j.campo_dc < 6) return true;   // cuadro sin campo PAL
  j.campo_dc -= 6;
  const Poses& p = *j.poses;
  nb::Entrada in;
  in.inicio = j.ini_dc;
  in.dif = j.dif;
  const uint16_t pulsado = j.pendiente;
  j.pendiente = 0;
  in.boton = BotonPulsado(uint16_t(pulsado & 0xFFF0));
  in.flanco = pulsado;
  in.mantenido = j.antes;   // lo apretado en el ultimo cuadro leido (PasoBotones)
  {
    const int clip = g_cb_clip.load();
    const auto it = g_clips.find(clip);
    // EU 331C70: corte por fin de camara. En un clip once, leer el mismo reloj que el sampler.
    const bool reloj_clip = j.clave == 0x7DC && std::getenv("DBZ3_IW_ANI2_7DC") && !g_cb_bucle.load();
    in.cam_fin = clip >= kCamEntrada && clip < kCamEntrada + 0x100 ? j.t >= j.ent
                  : it != g_clips.end() && (reloj_clip ? CuadroClip(it->second) : float(g_cb_frame.load())) >= it->second.nf - 1;
  }
  in.t = j.t_dc++;
  if (!j.semilla_dc) j.semilla_dc = uint32_t(Azar()) | 1u;
  nb::Salida o;
  if (!nb::g_campo || !nb::g_campo(in, o, j.semilla_dc)) return false;
  j.ini_dc = false;
  TomaOriginalBot(j, o);
  AvanzarUiA(j);   // IW-FX-04; 12/11: siempre (destello del aro 0x2A, siluetas 6/7 de QteMg_PlayHitFeedbackUi): panel como el original
  const float vida0_antes = j.vida[0];
  const int aciertos_antes[2] = {j.c[0].aciertos, j.c[1].aciertos};
  j.vida[0] = o.vida[0];
  j.vida[1] = o.vida[1];
  for (int k = 0; k < o.n_ev; ++k) {
    const nb::Evento& e = o.ev[k];
    if (e.tipo == 'C') {
      const int clip = e.n & 0xFFFF;
      if (clip != 0) CamB(clip, (e.n & 0x10000) != 0, clip == p.cam_juego, clip == p.cam_juego);   // 0: la entrada (ya puesta)
    } else if (e.tipo == 'J' || e.tipo == 'R') {
      const int rol = e.tipo == 'J' ? 0 : 1;
      if (const uint16_t cod = PoseDC(p, rol, e.n)) {
        const bool espera = cod == p.pelea[rol];
        const bool golpe = (rol == 0 && e.n >= 4 && e.n <= 0x10) || (rol == 1 && e.n >= 3 && e.n <= 0xF);
        Poner(p, rol, cod, espera, golpe ? p.pelea[rol] : 0);
        if (EfectosIW()) {   // IW-FX-03: el estado de la pose para kFxNB014 (Char_EnterPoseState de nuevo)
          if (j.pose_estado[rol] != e.n) j.pose_previo[rol] = j.pose_estado[rol];
          j.pose_estado[rol] = e.n;
          j.fx_paso[rol] = 0;
          j.pose_codigo[rol] = cod;
        }
        if (golpe && ((rol == 0 && (e.n - 4) % 3 == 0) || (rol == 1 && (e.n - 3) % 3 == 0)) && e.n < 0xF)
          SeGolpe(rol, true);
      }
    } else if (e.tipo == 'V') {
      j.aviso_t = 30;
      j.aviso_bien = false;
    }
  }
  // Efectos de acierto/fallo de QteMg_PlayHitPoses 0x330410 + QteMg_PlayHitFeedbackUi 0x32ED60: el que recibe el golpe
  // (rival si el jugador acierta en el carril 0; jugador si falla el carril 1) hace CHRFADE y sale el aviso del carril.
  // Combo: los dos carriles aciertan en el mismo campo -> carril 2, sin CHRFADE (el original manda el efecto 0x1D).
  {
    const int d0 = o.aciertos[0] - aciertos_antes[0], d1 = o.aciertos[1] - aciertos_antes[1];
    // 12/11: el aviso de la silueta va siempre (EfectoGolpe solo deja tras el ajuste el CHRFADE)
    if (d0 > 0 && d1 > 0) EfectoGolpe(j, 2, true, -1);
    else for (int l = 0; l < 2; ++l) if ((l ? d1 : d0) > 0) EfectoGolpe(j, l, true, 1 - l);
    if (o.vida[0] < vida0_antes - 0.5f) EfectoGolpe(j, (d0 > 0 && d1 > 0) ? 2 : 1, false, 0);
  }
  for (int l = 0; l < 2; ++l) {
    // OFFENCE/DEFENCE del original = +0x3C/+0x44 y +0x40/+0x48 de toda la partida (SkillEdit_ComputeStatPercent)
    j.c[l].aciertos = o.aciertos[l]; j.c[l].llegaron = o.intentos[l];
  }
  const int fase = o.fase == 0 ? 0 : o.fase == 2 ? 3 : o.sub == 0 ? 1 : 2;
  if (fase == 3) {   // QteMg_StartResultPhase: nota (+0x20) y rango (+0x1C) del original; el resultado es del port
    j.nota = int(o.nota);
    j.rango = o.rango;
    REXLOG_INFO("dbz3 r14: original 7DC: nota {} rango {} vida {} / {}", j.nota, j.rango, int(j.vida[0]), int(j.vida[1]));
    CerrarBotones(j);
    return true;
  }
  if (fase != j.fase) {
    if (j.fase == 0) REXLOG_INFO("dbz3 r14: en juego ({} dificultad {}, logica original)", j.m->nombre, j.dif);
    j.fase = fase;
    j.t = 0;
  }
  // La X del fallo y el aviso de las siluetas son objetos UI del original (AvanzarUiA / EfectoUiA), no del icono.
  for (int l = 0; l < 2; ++l) {
    Carril& c = j.c[l];
    c.sale = o.n_ic[l];
    for (int i = 0; i < c.sale; ++i) {
      const nb::Icono& ic = o.ic[l][i];
      c.ic[i] = Icono{ic.boton, ic.x * kW / 600.0f - kIcono / 2, ic.vivo};   // centro en 600 de ancho -> borde en 640
      c.ic[i].resuelto = ic.fallado;
    }
  }
  // PUL-IW-FX-04: pedido_dc va por celda (0..49) y la ronda siguiente reusa las celdas: sin esto el piloto no pulsaba
  // nada desde la 2.a ronda (7DB: Vegeta perdia sin choque; el choque 0,1 sale de una celda especial ACERTADA, +0x3C = 1).
  if (o.llegaron[0] + o.llegaron[1] < j.llegaron_dc) j.pedido_dc[0] = j.pedido_dc[1] = 0;
  j.llegaron_dc = o.llegaron[0] + o.llegaron[1];
  // piloto de pruebas (DBZ3_TEST_IWMINI=auto): el icono del carril activo al entrar en el objetivo (caja 422..457 de 600:
  // entero dentro con x 438..441; se pide en 430..440 por los 2-3 campos que tarda en llegar la pulsacion)
  if (j.auto_ && j.fase == 2 && j.inyecta_t == 0) {
    const int l = o.activo == 1 ? 1 : 0;
    const nb::Icono* mejor = nullptr;
    for (int i = 0; i < o.n_ic[l]; ++i) {
      const nb::Icono& ic = o.ic[l][i];
      if (ic.vivo && !ic.fallado && !((j.pedido_dc[l] >> ic.k) & 1) && ic.x >= 428.f && ic.x <= 440.f &&
          (!mejor || std::fabs(ic.x - 450.f) < std::fabs(mejor->x - 450.f)))
        mejor = &ic;
    }
    if (mejor) {
      j.pedido_dc[l] |= 1ull << mejor->k;
      int bt = mejor->boton;
      if (j.fallos && l == 1 && mejor->k % 5 == 4) bt = bt == 15 ? 14 : 15;   // prueba: un boton malo de cada 5
      if (const int32_t x = XboxDe(bt)) { j.inyecta = x; j.inyecta_t = 4; }
      else { j.pendiente = uint16_t(j.pendiente | (1u << bt)); if (j.trad_ok) { j.dir_piloto = uint16_t(1u << bt); j.dir_piloto_t = 4; } }
    }
  }
  return true;
}

// T2B tanda3 (8/11): un cuadro de 60 Hz de NB013 (0x7DB) con la logica ORIGINAL (iw_mips nb013::g_campo), igual que
// PasoOriginalDC. Del invitado: fases, iconos de los dos carriles, marca de choque, choque de machacar boton, vidas, nota y
// rango. Aqui: dibujo, camaras (UiAnim_Start) y poses (el estado de IW -> Estado13). El final (fase 3) es del port.
void Estado13(Juego& j,int rol,int s);
bool FinPoseBotones(const Juego& j,int rol);
float CuadroPoseBotones(const Juego& j,int rol);
uint32_t CodigoEstado13(int rol,int s,bool b06=false);
// IW-06 (dbz3_iw_r23_eventos): s_UTL_ACTUATE(a0, a1, a2) EU 0x2EFA10 guarda a0 (motor, 0 = nada), a1 (cuadros) y a2
// (fuerza) en su tarea; el ajuste de vibracion del original (0x5C5499 por mando) = dbz3_rumble.
// ponytail: a1 = campos PAL y a2 = % de fuerza leidos de la estructura, no medidos en un mando real; un hilo por pulsacion.
std::atomic<uint32_t> g_vibra_gen{0};
void VibrarR23(int n) {
  const int motor = n & 0xFF, campos = int8_t((n >> 8) & 0xFF), fuerza = (n >> 16) & 0xFFFF;
  REXLOG_INFO("dbz3 r23 ev 7DB: vibracion s_UTL_ACTUATE({}, {}, {}) (EU 0x2EFA10; dbz3_rumble {})", motor, campos, fuerza,
              dbz3::settings::RumbleEnabled() ? 1 : 0);
  rex::Runtime* rt = rex::Runtime::instance();
  auto* in = rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
  if (!motor || campos <= 0 || !in || !dbz3::settings::RumbleEnabled()) return;
  rex::input::X_INPUT_VIBRATION v{};
  const uint16_t vel = uint16_t(65535 * std::min(fuerza, 100) / 100);
  v.left_motor_speed = vel;
  v.right_motor_speed = vel;
  in->SetState(0, &v);
  const uint32_t gen = ++g_vibra_gen;
  std::thread([in, gen, ms = campos * 20] {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    if (g_vibra_gen.load() != gen) return;   // otra pulsacion la alargo
    rex::input::X_INPUT_VIBRATION cero{};
    in->SetState(0, &cero);
  }).detach();
}
static int g_conv_t0_7db = -1;   // t_dc en que entro J1 (conversacion de 7DB)
bool PasoOriginalDB(Juego& j) {
  namespace nb = dbz3::iwmips::nb013;
  const bool b06 = j.clave == 0x7D4;   // NB006: mismo cuerpo con nb006::g_campo y su tabla de poses
  if (dbz3::iwtrad::Activo() && dbz3::iwtrad::Arrancando()) return true;   // TRAD/6
  bool desde_vista = false;
  dbz3::iwtrad::EstadoBot vista;
  if (j.anima_intro && dbz3::iwtrad::Activo()) {
    if (!dbz3::iwtrad::LeerBot(j.clave, vista)) return true;
    if (vista.campo_intro7db != ~uint64_t(0)) {
      if (vista.campo_intro7db == j.campo_trad7db) return true;
      j.campo_trad7db = vista.campo_intro7db;
      desde_vista = true;
    }
  }
  if (!desde_vista) {
    j.campo_dc += dbz3::iwmips::freezer::g_ntsc ? 6 : 5;
    if (j.campo_dc < 6) return true;
    j.campo_dc -= 6;
  }
  if (j.presenta_7db && anim7db::Habilitada() && !dbz3::iwtrad::Activo() && !g_cb_quieto.load()) {
    // Unicamente al consumir un campo del espejo, nunca una vez por dibujo.
    // CampoBot escribe este mismo delta en kPasoT (PAL 1,2 / USA 1).
    auto m = g_cam_intro7.load();
    if (m.clip >= 0 && m.clip == g_cb_clip.load()) {
      m.f += dbz3::iwmips::freezer::g_ntsc ? 1.f : 1.2f;
      g_cam_intro7.store(m);
    }
  }
  for (int& t : j.destello) if (t >= 0) ++t;   // destello del aro de NB006 (efecto 0x2A): reloj desde el acierto
  const Poses& p = *j.poses;
  nb::Entrada in;
  in.inicio = j.ini_dc;
  in.dif = j.dif;
  const uint16_t pulsado = j.pendiente;
  j.pendiente = 0;
  in.boton = BotonPulsado(uint16_t(pulsado & 0xFFF0));
  in.flanco = pulsado;
  in.mantenido = j.antes;
  if (b06 && EfectosIW()) {   // PUL-IW-FX-03: palanca derecha (machaque del Masenko, ver iw_mips_botones.inl); piloto: palanca_auto
    const int x = PalancaDerechaX(), y = PalancaDerechaY();
    in.palanca = uint16_t(PalancaMasenko(x, y) | j.palanca_auto);
  }
  {
    const int clip = g_cb_clip.load();
    const auto it = g_clips.find(clip);
    // RITMO (R16): Camera_IsTimerElapsed del original cuenta CAMPOS PAL. Se compara con CuadroClip (el reloj del clip ya
    // convertido a 50/s) y no con el contador crudo del panel (60/s): antes el fin de camara llegaba un 20 % antes que el
    // ultimo cuadro dibujado (el READY de 0x186 duraba 3,8 s en vez de los 4,6 del original).
    in.cam_fin = clip >= kCamEntrada && clip < kCamEntrada + 0x100 ? j.t >= j.ent
                 : it != g_clips.end() && CuadroClip(it->second) >= float(it->second.nf - 1);
  }
  if (j.presenta_7db && g_cb_clip.load() == kCamEntrada) {
    const auto it = g_clips.find(kCamEntrada);
    in.cam_fin = it != g_clips.end() && CuadroClip(it->second) >= float(it->second.nf - 1);
  }
  for (int r = 0; r < 2; ++r) in.fin_pose[r] = j.pose_codigo[r] && FinPoseBotones(j, r);
  in.t = j.t_dc++;
  if (!j.semilla_dc) j.semilla_dc = uint32_t(Azar()) | 1u;
  nb::Salida o;
  in.presenta_7db = j.presenta_7db;
  const auto campo = b06 ? dbz3::iwmips::nb006::g_campo : nb::g_campo;
  if (!campo || !campo(in, o, j.semilla_dc)) return false;
  j.ini_dc = false;
  // No mezclar un subestado del espejo auxiliar con una ronda del ELF aun sin lectura valida.
  if (dbz3::iwtrad::Activo() && !TomaOriginalBot(j, o, desde_vista ? &vista : nullptr)) return true;
  AvanzarUiA(j);   // IW-FX-04; 12/11: siempre (destello del aro 0x2A, siluetas 6/7 de QteMg_PlayHitFeedbackUi): panel como el original
  j.vida[0] = o.vida[0];
  j.vida[1] = o.vida[1];
  const bool dirige = j.anima_intro && j.reloj_intro.tramo != anim7db::Juego;
  float cuadro_dialogo = -1.f;
  if (dirige) {
    const auto t = anim7db::Secuencia(o.fase, o.sub, j.trad_ok ? j.paso_anim_ee : o.paso);
    const bool cambio = j.reloj_intro.Entrar(t, j.t_dc);
    for (int r = 0; r < 2 && t != anim7db::Fuera; ++r) {
      const int estado = anim7db::Estado(t,r);
      const uint32_t cod = CodigoEstado13(r,estado), alias = AliasPose(p,r,cod), x = Luchador(r);
      if (cambio || j.actor_animado[r] != x) {
        j.actor_animado[r] = x;
        if (j.pose_estado[r] != estado) j.pose_previo[r] = j.pose_estado[r];
        j.pose_estado[r] = estado; j.pose_codigo[r] = cod; j.fx_paso[r] = 0;
        if (x && alias) {
          if (t == anim7db::Juego) anim::BucleForzado(x,alias);
          else anim::ReiniciarPose(x,alias,true);
        }
      }
      const float f = j.reloj_intro.Muestra(j.t_dc, dbz3::iwmips::freezer::g_ntsc ? 1.f : 1.2f,
          j.trad_ok, j.codigo_ee[r] == int(cod) ? j.cuerpo_ee[r] : -1.f, t <= anim7db::Conversacion ? 359.f : 229.f);
      anim7db::Publicar(r,x,alias,f);
      if (!r && t == anim7db::Conversacion) cuadro_dialogo = f;
    }
    if (t == anim7db::Juego || (t == anim7db::Fuera && (o.fase > 1 || o.sub != 0))) {
      anim7db::Limpiar(); j.anima_intro = false;
    }
    if (cambio && t >= anim7db::Corte) for (auto& m : anim7db::cara) m = 0;
  }
  for (int k = 0; k < o.n_ev; ++k) {
    const nb::Evento& e = o.ev[k];
    if (dirige && (e.tipo == 'J' || e.tipo == 'R') && e.n <= (e.tipo == 'J' ? 3 : 2)) continue;
    REXLOG_INFO("dbz3 db-ev t {} {} {:x} cod {:x}", j.t_dc, e.tipo, e.n, (e.tipo == 'J' || e.tipo == 'R') ? CodigoEstado13(e.tipo == 'J' ? 0 : 1, e.n, b06) : 0u);
    if (e.tipo == 'I' && j.presenta_7db) {
      if (j.clave == 0x7DB && anim7db::Habilitada() && !dbz3::iwtrad::Activo() && !(e.n & 0x10000) && !j.campo_velo_7db)
        j.campo_velo_7db = j.t_dc;
      REXLOG_INFO("dbz3 v2 7DB: I t_dc {} g_cb_frame {} clip {:X}", j.t_dc, g_cb_frame.load(), g_cb_clip.load());
      g_conv_t0_7db = -1;
      CamB(kCamEntrada, (e.n & 0x10000) != 0);
    } else if (e.tipo == 'C') {
      const int clip = e.n & 0xFFFF;
      if (j.presenta_7db) REXLOG_INFO("dbz3 v2 7DB: C 0x{:X} t_dc {} g_cb_frame {} clip_actual {:X} j.t {} ent {} cam_fin {} flanco {:X} trad {}", clip, j.t_dc, g_cb_frame.load(), g_cb_clip.load(), j.t, j.ent, in.cam_fin, in.flanco, j.trad_ok);
      const bool adelanta_espera = !j.anima_intro && !b06 && (!j.presenta_7db || j.trad_ok) && clip == p.cam_ini && !(e.n & 0x10000) &&
                                   j.fase == 1 && j.t < 30 && g_cb_clip.load() == clip && g_cb_quieto.load();
      if (clip != 0 && !adelanta_espera)
        CamB(clip, (e.n & 0x10000) != 0, clip == p.cam_juego, clip == p.cam_juego);
      if (!b06 && clip >= 0x187 && clip <= 0x18C && REXCVAR_GET(dbz3_iw_r23_eventos))   // IW-06: encuadre del choque
        REXLOG_INFO("dbz3 r23 ev 7DB: camara del choque 0x{:X} play {} (Mg_StartClashSequence 0x352A50 / Mg_SetClashResultPoses "
                    "0x352890 / Mg_CheckClashOutcome 0x352760)", clip, (e.n & 0x10000) ? 0 : 1);
    } else if (e.tipo == 'V') {   // IW-06: vibracion (solo con dbz3_iw_r23_eventos, ver iw_mips_botones.inl)
      VibrarR23(e.n);
    } else if (e.tipo == 'J' || e.tipo == 'R') {
      const int rol_ev = e.tipo == 'J' ? 0 : 1;
      const uint32_t cod_ev = CodigoEstado13(rol_ev, e.n, b06);
      // 7DB V1/V2: J0 -> J1 (0x564) y R0 -> R0 (0x569) son el MISMO clip, pero el original reinicia la pose al entrar en
      // la conversacion (Char_EnterPoseStateByIndex 0x350460 / 0x35C460); Reproducir descarta el pedido repetido y el
      // cuadro 0 de Vegeta (voz) y el 200 (Trunks) salian tarde. Se fuerza el reinicio (apagar: DBZ3_IW_V2_7DB=0).
      const bool reinicia_conv = cod_ev && V2_7DB() && j.presenta_7db && j.fase == 0 && j.pose_codigo[rol_ev] == cod_ev;
      if (cod_ev) Estado13(j, rol_ev, e.n);   // sin clip propio: se deja la pose
      if (V2_7DB() && j.presenta_7db && j.fase == 0 && rol_ev == 0 && e.n == 1) g_conv_t0_7db = j.t_dc;
      if (reinicia_conv) dbz3::anim::ReiniciarPose(Luchador(rol_ev), AliasPose(*j.poses, rol_ev, cod_ev), true);
    } else if (e.tipo == 'F') {   // s_UTL_CHRFADE(actor): Mg_SetClashStepPoses marca a quien recibe el golpe del choque
      if (TinteIW() && e.n >= 0 && e.n <= 1) iwtinte::Qte(g_chr_paleta, g_chr_fade, g_chr_total, e.n, "352D40");
      else if (EfectosIW() && e.n >= 0 && e.n <= 1) {
        g_chr_paleta[e.n] = iwchrfade::kImpacto; g_chr_total = 15; g_chr_fade[e.n] = 15;
      }
    }
  }
  // Juicio (aviso del aro + aciertos): el original lo lleva en el contador de resueltos (+0x2C/+0x30) y en el icono marcado
  // como fallado. Se toma el recuento del campo ANTERIOR (c.ic[].resuelto = +0x1F8 del original) antes de rehacer los iconos.
  int fallados_antes[2] = {};
  for (int l = 0; l < 2; ++l)
    for (int i = 0; i < j.c[l].sale; ++i) fallados_antes[l] += j.c[l].ic[i].resuelto ? 1 : 0;
  // Iconos del campo: se conserva la animacion del MISMO icono del campo anterior (salida 0x0C al acertar, cruz 7/8 al
  // fallar: Mg_ProcessQteHit / Mg_ProcessLaneHits). Se empareja por boton y x (avanza j.vel por campo).
  for (int l = 0; l < 2; ++l) {
    Carril& c = j.c[l];
    Icono prev[50];
    const int n_prev = std::min(c.sale, 50);
    std::copy(c.ic, c.ic + n_prev, prev);
    c.sale = o.n_ic[l];
    for (int i = 0; i < c.sale; ++i) {
      const nb::Icono& ic = o.ic[l][i];
      const float x = ic.x * kW / 600.0f - kIcono / 2;
      Icono d{ic.boton, x, ic.vivo};
      d.resuelto = ic.fallado;
      // mismo icono = mismo boton y el que mas cerca queda por detras (avanza ~vel x campo; los demas van a >20 px)
      int mejor = -1; float avance = 1e9f;
      for (int q = 0; q < n_prev; ++q) {
        if (prev[q].boton != ic.boton) continue;
        const float dd = x - prev[q].x;
        if (dd < -0.1f || dd > 6.0f) continue;
        if (dd < avance) { avance = dd; mejor = q; }
      }
      if (mejor >= 0) { d.cruz = prev[mejor].cruz; d.salida = prev[mejor].salida; d.anim_t = prev[mejor].anim_t; }
      if (d.cruz || d.salida) ++d.anim_t;
      c.ic[i] = d;
    }
  }
  for (int l = 0; l < 2; ++l) {
    Carril& c = j.c[l];
    int ahora = 0;
    for (int i = 0; i < o.n_ic[l]; ++i) ahora += o.ic[l][i].fallado;
    // 12/11: +0x2C/+0x30 del original es el recuento de la RONDA (vuelve a 0 en cada una): solo dispara el aviso. Los
    // contadores del panel (OFFENCE/DEFENCE = +0x3C/+0x44 y +0x40/+0x48 de toda la partida, SkillEdit_ComputeStatPercent
    // EU 0x371C..) se copian del original; antes salian 107/1 en 7D4 y 76/29 en 7DB.
    const int nuevos = o.llegaron[l] >= c.llegaron_raw ? o.llegaron[l] - c.llegaron_raw : o.llegaron[l];
    c.llegaron_raw = o.llegaron[l];
    c.aciertos = o.golpes[l];
    c.llegaron = o.intentos[l];
    if (nuevos > 0) {
      const bool bien = ahora <= fallados_antes[l];
      // 12/11: el aviso y la X salen de los objetos UI del original (AvanzarUiA, siempre); aqui solo la salida del icono
      EfectoGolpe(j, l, bien, bien ? 1 - l : l);
      if (EfectosIW()) {
        int mejor = -1; float dist = 1e9f;
        for (int i = 0; i < c.sale; ++i) {
          if (c.ic[i].salida || c.ic[i].cruz) continue;
          const float dd = std::fabs(float(kObjetivoUI) - (c.ic[i].x + kIcono / 2) * 600.0f / kW);
          if (dd < dist) { dist = dd; mejor = i; }
        }
        if (mejor >= 0 && bien) c.ic[mejor].salida = true, c.ic[mejor].anim_t = 0;   // la X la pone el objeto UI (anim 7)
      }
    }
  }
  const int fase = o.fase == 0 ? 0 : o.fase == 2 ? 3 : o.sub == 0 ? 1 : 2;
  const bool voz_del_elf = dirige && j.trad_ok;
  if (voz_del_elf || (j.presenta_7db && fase == 0 && (dirige || g_cb_clip.load() == p.cam_presenta) && j.pose_estado[0] == 1)) {
    // Tick de la pose 1 de Vegeta, EU 0x350310: cuadro 0 sortea 9/A;
    // al cruzar 200 habla Trunks (linea 0). Ninguna voz condiciona el avance.
    // En el modo anterior se contaban campos desde J1. En PAL cada campo son 1,2 unidades del clip.
    const float cuadro = dirige ? cuadro_dialogo : g_conv_t0_7db >= 0 ? float(j.t_dc - g_conv_t0_7db) : -1.f;
    auto voz = [&](int rol, int linea) {
      if (dirige) anim7db::PedirCara(rol,Luchador(rol),linea);
      static const auto elf = P::LeerElf(P::BuscarIW());
      const int entrada = anim::apev::EntradaAdxPerfil(elf, anim::apev::PerfilAdx(rol ? 928 : 931), linea);
      REXLOG_INFO("dbz3 v2 7DB: voz lado {} linea {:X} ADX {} cuadro {:.1f}", rol, linea, entrada, cuadro);
      const auto* base = Base();
      if (!base) return;
      const bool jp = REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9) == 0;
      if (entrada >= 0 && !dbz3::settings::AudioMute())
        P::OirAdxPs2(P::BuscarIW(), jp ? "ADX_JPN.AFS" : "ADX_USA.AFS", entrada, float(dbz3::settings::MasterVolume()));
    };
    if (j.voces_apertura == 0 && (voz_del_elf ? j.lineas_ee[0] >= 0 : cuadro >= 0.f)) {
      const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
      voz(0, voz_del_elf ? j.lineas_ee[0] : 9 + int((t >> 10) & 1));
      ++j.voces_apertura;
    }
    if (j.voces_apertura == 1 && (voz_del_elf ? j.lineas_ee[1] == 0 : cuadro >= 200.f)) { voz(1, 0); ++j.voces_apertura; }
  }
  if (j.presenta_7db && fase != 0) g_start_apertura_7db = false;
  if (fase == 3) {   // Mg_EvaluateTimingRank: nota (+0x20) y rango (+0x1C) del original; el resultado es del port
    j.nota = int(o.nota);
    j.rango = o.rango;
    REXLOG_INFO("dbz3 r14: original 7DB: nota {} rango {} vida {} / {} choques {} (campo {})", j.nota, j.rango, int(j.vida[0]),
                int(j.vida[1]), o.choques, j.t_dc);
    CerrarBotones(j);
    return true;
  }
  if (fase != j.fase) {
    if (j.fase == 0) REXLOG_INFO("dbz3 r14: en juego ({} dificultad {}, logica original)", j.m->nombre, j.dif);
    if (std::getenv("DBZ3_TEST_IWMINI")) REXLOG_INFO("dbz3 r14: 7DB fase {} -> {} en el campo {}", j.fase, fase, j.t_dc);
    // NB013 ya publica camara y poses en los eventos del original. En particular,
    // Mg_UpdateOpeningPhase pide 0x186 quieto antes de arrancarlo: reiniciarlo
    // aqui hacia que se moviese durante la espera previa a READY.
    // NB006 conserva su respaldo mientras no se haya contrastado esa secuencia.
    if (b06 && fase == 1) {
      CamB(p.cam_ini);
      Poner(p, 0, p.listo[0], false, p.pelea[0]);
      Poner(p, 1, p.listo[1], false, p.pelea[1]);
    } else if (b06 && fase == 2 && j.fase < 2) {
      CamB(p.cam_juego, false, true, true);
      Poner(p, 0, p.pelea[0], true);
      Poner(p, 1, p.pelea[1], true);
    }
    j.fase = fase;
    j.t = 0;
  }
  // Mg_UpdateOpeningPhase deja el plano 0x186 quieto durante 30 cuadros;
  // Mg_UpdateIntroPhase lo pone en movimiento al terminar esa espera.
  if (!j.anima_intro && !b06 && (!j.presenta_7db || j.trad_ok) && fase == 1 && j.t >= 30 && g_cb_clip.load() == p.cam_ini && g_cb_quieto.load())
    CamB(p.cam_ini);
  // (los iconos ya se rehicieron arriba, conservando la animacion de acierto/fallo)
  // marca de choque (objetos 0x2A izquierdo, 0x2C medio, 0x2B derecho): visible mientras recorre el carril
  j.marca_activa = o.marca_y > 300.f && o.marca_x[0] < 600.f && o.marca_x[1] > -60.f;
  if (j.marca_activa) {
    j.especial_lado = o.marca_y > 390.f ? 1 : 0;
    j.especial_indice = std::clamp(o.marca_indice, 0, 49);
    j.marca[0] = o.marca_x[0] * kW / 600.0f; j.marca[1] = o.marca_x[1] * kW / 600.0f; j.marca[2] = o.marca_x[2] * kW / 600.0f;
  }
  // PUL-IW-FX-04 (dbz3_iw_efectos_original, solo 7DB): la cadena de poses del choque, que en el original es de la maquina de
  // poses (fin de animacion, +0xC74) y no de Mg_*: el invitado solo da el arranque (Mg_StartClashSequence 0x352A50: Vegeta 0x11
  // en el carril 0, Trunks 0x10 en el 1) y sin esto los dos se quedaban en esa pose todo el choque (sin 0x12-0x19 ni 933/34-35).
  // Carril 0 (tabla 0x49A730): fin de 0x11 -> 0x12 y Trunks 0x19 (0x34EC40); fin de s -> s+1 (0x19 -> 0x12); al entrar en
  // 0x13..0x19 Trunks = (+0x1AAC ? 0x1A : 0x22) + (s-0x13) (0x34E820..0x34DCE0). Carril 1 (tabla 0x49AD90): fin de 0x10 ->
  // 0x11 y Vegeta 0x1A (0x35ADA0); fin de s -> s+1 (0x18 -> 0x11); al entrar en 0x12..0x18 Vegeta = (+0x1AAC ? 0x23 : 0x1B) +
  // (s-0x12) (0x35A950..0x359E20). Cada entrada pone +0x1AAC a 0.
  if (!b06 && EfectosIW() && o.sub == 3 && o.choque_fase == 0) {
    if (o.pulsos > j.pulsos || (o.objetivo > 0 && o.pulsos >= o.objetivo)) j.aac13 = true;
    const int s0 = j.pose_estado[0], s1 = j.pose_estado[1];
    if (s0 >= 0x11 && s0 <= 0x19 && FinPoseBotones(j, 0)) {
      const int n = s0 == 0x19 ? 0x12 : s0 + 1;
      Estado13(j, 0, n);
      if (s0 == 0x11) Estado13(j, 1, 0x19);
      else if (n >= 0x13) Estado13(j, 1, (j.aac13 ? 0x1A : 0x22) + n - 0x13);
      REXLOG_INFO("dbz3 pulfx04: 7DB choque carril 0: Vegeta 0x{:X} -> 0x{:X}, Trunks 0x{:X} (1AAC {})", s0, n, j.pose_estado[1], j.aac13);
      j.aac13 = false;
    } else if (s1 >= 0x10 && s1 <= 0x18 && FinPoseBotones(j, 1)) {
      const int n = s1 == 0x18 ? 0x11 : s1 + 1;
      Estado13(j, 1, n);
      if (s1 == 0x10) Estado13(j, 0, 0x1A);
      else if (n >= 0x12) Estado13(j, 0, (j.aac13 ? 0x23 : 0x1B) + n - 0x12);
      REXLOG_INFO("dbz3 pulfx04: 7DB choque carril 1: Trunks 0x{:X} -> 0x{:X}, Vegeta 0x{:X} (1AAC {})", s1, n, j.pose_estado[0], j.aac13);
      j.aac13 = false;
    }
  } else if (o.sub != 3) j.aac13 = false;
  j.especial_fase = o.sub == 3 ? (o.choque_fase == 0 ? 0 : 1) : -1;
  j.pulsos = o.pulsos;
  j.objetivo_pulsos = o.objetivo;
  // PUL-IW-FX-04: pedido_dc va por celda (0..49) y la ronda siguiente reusa las celdas: sin esto el piloto no pulsaba
  // nada desde la 2.a ronda (7DB/7D4: Vegeta perdia sin choque; el choque 0,1 sale de una celda especial ACERTADA, +0x3C = 1).
  if (o.llegaron[0] + o.llegaron[1] < j.llegaron_dc) j.pedido_dc[0] = j.pedido_dc[1] = 0;
  j.llegaron_dc = o.llegaron[0] + o.llegaron[1];
  // Piloto de pruebas: icono del carril activo; en el choque NB013, solo la celda especial.
  if (j.auto_ && j.fase == 2 && j.inyecta_t == 0) {
    int bt = -1;
    if (b06 && j.machaca && o.sub == 3) {   // IW-FX-04: NB006: primero machacar (Mg_PollPlayerButton 0x378F80 cuenta ced4+8, cualquier boton
      // 12-15) y luego UN boton (Mg_CheckQtePress: el del objeto 0x2F al solapar con el 1; otro = fallo, 0x289/933/25)
      // IW-FX-05: pulsa 2 campos y suelta 2 (t_dc & 3): con t_dc & 1 e inyecta_t = 2 el mando quedaba apretado sin
      // flanco (QTE_GetPressedButton solo ve flancos; Mg_CheckQtePress solo mira mientras 0x2F solapa con el 1)
      // PUL-IW-FX-03: el machaque NO son botones: Mg_PollPlayerButton cuenta la palanca DERECHA (pad +0x6E). Con botones el
      // contador se quedaba en 0 y Mg_SpawnQteButton 0x378B20 saltaba al resultado sin boton (0x86 = 0 -> 0x289 fallado).
      // Fase 0: un cuarto de vuelta cada 3 campos (70 de 40 en el banco); fase 2: SOLO el boton del objeto 0x2F (otro = fallo).
      j.palanca_auto = o.choque_fase == 0 ? uint16_t(0x1000u << ((j.t_dc / 3) & 3)) : 0;
      if (o.choque_fase == 2 && (j.t_dc & 3) == 1 && o.qte_boton >= 12 && o.qte_boton <= 15) bt = o.qte_boton;
    } else if (!b06 && o.sub == 3 && o.choque_fase == 0) {
      // Repetir la misma tecla requiere soltarla entre dos flancos.
      if (!j.pierde && !(j.fallos && o.choque_lado == 1) && (j.t_dc & 3) == 1 &&
          o.qte_boton >= 8 && o.qte_boton <= 15) bt = o.qte_boton;
    } else if (o.sub == 3 && o.choque_fase == 0 && (j.t_dc & 1)) bt = 15 - int((j.t_dc >> 1) & 3);
    else if (o.sub == 1 && !j.pierde) {
      const int l = o.activo == 1 ? 1 : 0;
      const nb::Icono* mejor = nullptr;
      for (int i = 0; i < o.n_ic[l]; ++i) {
        const nb::Icono& ic = o.ic[l][i];
        if (ic.vivo && !ic.fallado && !((j.pedido_dc[l] >> ic.k) & 1) && ic.x >= 428.f && ic.x <= 440.f &&
            (!mejor || std::fabs(ic.x - 450.f) < std::fabs(mejor->x - 450.f)))
          mejor = &ic;
      }
      if (mejor) {
        j.pedido_dc[l] |= 1ull << mejor->k;
        bt = mejor->boton;
        if (j.fallos && l == 1 && mejor->k % 5 == 4) bt = bt == 15 ? 14 : 15;
      }
    }
    if (bt >= 0) {
      if (const int32_t x = XboxDe(bt)) { j.inyecta = x; j.inyecta_t = o.sub == 3 && (!b06 || j.machaca) ? 2 : 4; }
      else { j.pendiente = uint16_t(j.pendiente | (1u << bt)); if (j.trad_ok) { j.dir_piloto = uint16_t(1u << bt); j.dir_piloto_t = !b06 && o.sub == 3 ? 2 : 4; } }
    }
  }
  return true;
}

// Colision del original EU2F03C0: rectangulos truncados a enteros,
// objetivo(-28..7) en470; NO la ventana aproximada370..470 del port antiguo.
bool SolapaObjetivo(float x, float izq, float der) {
  return int(x+der)>=442 && int(x+izq)<=477;
}
// NIWF: el remate de NB006 tiene una opcion propia; no cambia el solape de las marcas ni el de los otros cursos.
REXCVAR_DEFINE_BOOL(dbz3_iw_7d4_ventana_remate_pal, true, "DBZ3/IW",
                    "IW 7D4: ventana PAL del remate de Masenko en la ruta de respaldo");
bool SolapaRematePal06(float x) {
  // EU 0x2F03C0 -> 0x2F3770 usa sumas y comparaciones float; no trunca los bordes.
  return x + 16.f >= kObjetivoUI - 28.f && x - 16.f <= kObjetivoUI + 7.f;
}

// eIW5: ventana de los ICONOS normales, medida en el original (7DB/7D4/7DC, campos de PCSX2): el icono "llega" (+0x44) con
// x = 405,7 y la pose de fallo sale con x = 471 (con 426 / 493 salia ~7 campos tarde). El objetivo esta en ~450 (el aro
// dibujado en 480 de pantalla = 450 de la UI de 600), no en 470 (470 es el umbral de fallo, fRam0049c85c/2+150): ventana
// 421..450 (ajustada a +-2 campos con el 7DC). Las marcas y el remate de NB006/NB013 siguen con la ventana anterior
// (con ella tambien el piloto fallaba el especial de NB006). DBZ3_IW_QTE_VENTANA_VIEJA=1 devuelve 442..477 a los iconos.
bool SolapaIcono(float x) {
  static const bool vieja = std::getenv("DBZ3_IW_QTE_VENTANA_VIEJA") != nullptr;
  // fase 1 (7/11): la regla del original con el ancho real (600): UI_CheckTwoObjectsOverlap del icono (-16..16) con el
  // objetivo (-28..7) en x 450 = 422..457. DBZ3_IW_QTE_VENTANA_VIEJA=1: la ventana medida a mano (421..450).
  return vieja ? (int(x+16)>=421 && int(x-16)<=450) : (int(x+16)>=int(kObjetivoUI)-28 && int(x-16)<=int(kObjetivoUI)+7);
}
bool SolapaMarca(const Juego& j) {
  return SolapaObjetivo(j.marca[0],-12,12) || SolapaObjetivo(j.marca[1],-12,12) ||
         SolapaObjetivo(j.marca[2],-4,j.marca_ancho);
}
void ReiniciarMarca(Juego& j, int l, int indice) {
  const bool nb13=j.poses==&kPosesNB013;
  const int a=int(std::nearbyint(j.vel*(nb13 ? 180.0f : 240.0f))), b=int(std::nearbyint(j.vel*60));
  j.marca[0]=float(-a-b-48);
  j.marca[1]=float(-b-72);
  j.marca[2]=j.marca[0]+16;
  j.marca_ancho=float(a-48);
  j.marca_activa=true;
  j.marca_entrada=false;
  j.especial_lado=l;
  j.especial_indice=indice;
  if (nb13 && l==1) for (float& x:j.marca) x-=50;
}
uint32_t CodigoEstado13(int rol,int s,bool b06) {
  if (b06) return PoseDC(kPosesNB006,rol,s);   // NB006: ataque/esquiva/herido por pares con su tabla
  // NB013: codigo escrito en actor+0xB60 al entrar por 0x49A730 (Vegeta) / 0x49AD90 (Trunks), ELF EU.
  static constexpr uint16_t kVegeta[] = {
    0x564, 0x564, 0x578, 0x57A, 0x582, 0x598, 0x597, 0x585,
    0x59B, 0x59A, 0x588, 0x59E, 0x59D, 0x58B, 0x5A1, 0x5A0,
    0x57C, 0x541, 0x543, 0x544, 0x545, 0x546, 0x547, 0x548,
    0x549, 0x54A, 0x54B, 0x54C, 0x54D, 0x54E, 0x54F, 0x550,
    0x551, 0x552, 0x553, 0x554, 0x555, 0x556, 0x557, 0x558,
    0x559, 0x55A, 0x55B, 0x55D, 0x55F, 0x55F, 0x55C, 0x55E,
    0x5C9, 0x5BE, 0x5C0, 0x565, 0x566,
  };
  static constexpr uint16_t kTrunks[] = {
    0x569, 0x579, 0x57B, 0x596, 0x584, 0x583, 0x599, 0x587,
    0x586, 0x59C, 0x58A, 0x589, 0x59F, 0x58D, 0x58C, 0x57D,
    0x541, 0x543, 0x544, 0x545, 0x546, 0x547, 0x548, 0x549,
    0x54A, 0x54B, 0x54C, 0x54D, 0x54E, 0x54F, 0x550, 0x551,
    0x552, 0x553, 0x554, 0x555, 0x556, 0x557, 0x558, 0x559,
    0x55A, 0x55B, 0x55D, 0x560, 0x560, 0x55C, 0x55E, 0x5C9,
    0x5BF, 0x5C1,
  };
  if (s < 0) return 0;
  if (rol) return s < int(std::size(kTrunks)) ? kTrunks[s] : 0;
  return s < int(std::size(kVegeta)) ? kVegeta[s] : 0;
}
void Estado13(Juego& j,int rol,int s) {
  if (j.pose_estado[rol]!=s) j.pose_previo[rol]=j.pose_estado[rol];
  j.pose_estado[rol]=s;
  j.fx_paso[rol]=0;   // IW-FX-02: Char_EnterPoseState de nuevo: sus efectos vuelven a empezar
  const uint32_t c=CodigoEstado13(rol,s,j.clave==0x7D4);
  j.pose_codigo[rol]=c;
  Poner(*j.poses,rol,c,s==(rol ? 2 : 3));
}
bool FinPoseBotones(const Juego& j,int rol) {
  anim::EstadoCuerpo e;
  for (int cuerpo=0;cuerpo<2;++cuerpo)
    if (anim::LeerEstadoCuerpo(Base(),Luchador(rol),cuerpo,e) && e.hay &&
        e.codigo==AliasPose(*j.poses,rol,j.pose_codigo[rol]) && e.cuadros && e.cuadro>=float(e.cuadros-1)) return true;
  return false;
}
float CuadroPoseBotones(const Juego& j,int rol) {
  anim::EstadoCuerpo e;
  for (int cuerpo=0;cuerpo<2;++cuerpo)
    if (anim::LeerEstadoCuerpo(Base(),Luchador(rol),cuerpo,e) && e.hay &&
        e.codigo==AliasPose(*j.poses,rol,j.pose_codigo[rol])) return e.cuadro;
  return -1;
}

// ---- IW-FX-02: efectos de las poses de NB006 ----
// En el original los lanza la maquina de poses de cada luchador (Char_EnterPoseState* / Char_TickPose*, tablas EU
// 0x49B5E0/0x49B6C0 Gohan y 0x49BB50/0x49BC10 Piccolo) con Effect_DispatchCommandById (0x2F7950, tabla 0x4987B0):
// comando -> op (Cmd_EmitOp*) -> recurso op de la tabla 0x49CDA8 = hijo op de DATA_CMN 933. Aqui la maquina de poses es
// del port (Estado13) y el interprete calla 0x2F7950: sin esta tabla no salia ninguno (ni el Masenko ni los rayos).
// modo: 0 nodo propio + vista de camara, 1 nodo propio con dir = nodo 0x12 - (nodo - 100 z) (Moving, velocidad 0),
// 2 antenas 933/29 (consumidor de siempre, CuadroAntenas), 3 nodo del OTRO + vista, 4 ojo + vista * 60.
// matar: 1 Effect_DispatchCommandById(0) antes, 2 solo si ya van mas de 1 (iRam005d9e80 > 1).
// IW-FX-03: 5 ojo + vista * 150 (FUN_001424e0(150.0) de EnterPoseState_0x12 de NB013); 6/7 como 1 pero con el vector
// de referencia nodo 0x12 + 100 x (Vegeta) / - 100 x y la posicion con x - 8 (Trunks). op 0 = solo matar.
// IW-FX-04: 8 Pos_DiffTwoNodes: pos nodo propio, dir = nodo propio - nodo 0x12 del otro (Moving, op 22 de NB013 0x2A);
// 9 Pos_DiffNodeOffsetVec(6, 4): pos nodo 4, dir = nodo 6 - nodo 4 (Trunks 0x29); 10 Effect_SpawnAmoOnBone (op 20): pegado
// al nodo del OTRO actor (indice +0x1C: Vegeta 1, Trunks 0, rival de NB014 0); Effect_DrawAmoAttachedToBone lo pinta
// con la matriz entera del hueso. IW-FX-05: Trunks 0x2F y rival de NB014 0x10 (el struct se pone a cero: nodo 0, actor 0).
// tras: solo si el estado anterior propio era ese (la puerta iRam005d9be0/9cd0 que abre el 0x11/0x10 al acabar).
struct FxPose { uint8_t rol, estado; float cuadro; uint8_t op; int8_t nodo; uint8_t modo, matar; int8_t salvo = -1, tras = -1; };
constexpr FxPose kFxNB006[] = {
    // Gohan (tabla 0x49B5E0): 0x10 Char_PoseTickCameraFx, 0x11 Char_EnterPoseState11, 0x12 Char_PoseTickDrainFx,
    // 0x14 Char_PoseTickFx14 (Masenko, cmd 0x10 = op 0x19), 0x17 Char_PoseTickTransformFx
    {0, 0x10, 30, 38, 0, 4, 1, -1}, {0, 0x10, 30, 27, 0, 0, 0, -1}, {0, 0x10, 245, 28, 0x12, 0, 0, -1},
    {0, 0x11, 0, 23, 0xB, 0, 0, -1}, {0, 0x12, 8, 24, 0x13, 1, 1, -1}, {0, 0x14, 14, 25, 0x13, 1, 0, -1},
    {0, 0x17, 6, 37, 0x12, 0, 0, -1},
    // Piccolo (tabla 0x49BB50): ataques 3/6/9/C = antenas al entrar (cmd 0x14) + chispa 37 en Gohan (cmd 0x15,
    // Char_TickPoseEffect15*: no si Gohan esquiva); heridas/guardias = 12 (cmd 0x1A) / 14 (cmd 0x1C) en su cuerpo
    {1, 3, 0, 29, 21, 2, 0, -1}, {1, 3, 6, 37, 0x12, 3, 0, 5},
    {1, 4, 10, 14, 0xF, 0, 2, -1}, {1, 4, 15, 14, 0xF, 0, 2, -1}, {1, 4, 20, 14, 0xF, 0, 2, -1},
    {1, 5, 10, 12, 0xB, 0, 2, -1}, {1, 5, 15, 12, 0x13, 0, 2, -1}, {1, 5, 20, 12, 0xB, 0, 2, -1},
    {1, 6, 0, 29, 21, 2, 0, -1}, {1, 6, 6, 37, 0x12, 3, 0, 8},
    {1, 7, 8, 14, 10, 0, 2, -1}, {1, 8, 8, 12, 0x13, 0, 2, -1},
    {1, 9, 0, 29, 21, 2, 0, -1}, {1, 9, 10, 37, 0x12, 3, 0, 0xB},
    {1, 0xA, 10, 14, 0xE, 0, 2, -1}, {1, 0xA, 18, 14, 0xE, 0, 2, -1}, {1, 0xA, 22, 14, 0xE, 0, 2, -1},
    {1, 0xB, 10, 12, 10, 0, 2, -1}, {1, 0xB, 18, 12, 0x13, 0, 2, -1}, {1, 0xB, 22, 12, 10, 0, 2, -1},
    {1, 0xC, 0, 29, 21, 2, 0, -1}, {1, 0xC, 6, 37, 0x12, 3, 0, 0xE},
    {1, 0xD, 10, 14, 6, 0, 2, -1}, {1, 0xE, 10, 12, 4, 0, 2, -1},
    {1, 0x10, 8, 26, 0x12, 1, 0, -1}, {1, 0x11, 0, 20, 0x12, 0, 0, -1},
};
// IW-FX-03: 7DB (NB013, Vegeta 0x49A730/0x49A8F0 N=53, Trunks 0x49AD90/0x49AF30 N=50), sacada con fxpose.py (salidas
// E:/DBZ3HD_trabajo/iw-fx-02/nb013_j.txt / nb013_r.txt) y leida en el decompilado donde la posicion es de camara.
// El 0x28/0x29 de los ciclos depende de actor+0x1AAC, que en el minijuego solo se pone a 0 (lo sube solo
// Battle_KiChargeTickWithDamage): Vegeta 0x29 (op 35), Trunks 0x28 (op 34). Pendiente: 0x2A/0x29 (op 22 Moving con
// Pos_DiffTwoNodes), 0x31 y Trunks 0x2F (op 20, Effect_SpawnAmoOnBone).
constexpr FxPose kFxNB013[] = {
    {0, 3, 35, 14, 0x12, 0, 1}, {0, 3, 45, 14, 0x12, 0, 1}, {0, 3, 55, 14, 0x12, 0, 1},   // Char_PoseTickTransformFxB
    {0, 5, 0, 31, 0x12, 0, 1}, {0, 8, 0, 31, 0x12, 0, 1}, {0, 0xB, 0, 31, 0x12, 0, 1}, {0, 0xE, 0, 31, 0x12, 0, 1},
    {0, 6, 0, 13, 0x12, 0, 1}, {0, 9, 0, 13, 0x12, 0, 1}, {0, 0xC, 0, 13, 0x12, 0, 1}, {0, 0xF, 0, 13, 0x12, 0, 1},
    {0, 0x11, 0, 0, 0, 0, 1}, {0, 0x11, 16, 33, 0x12, 0, 1},
    {0, 0x12, 0, 35, 0, 3, 0}, {0, 0x12, 0, 21, 0, 5, 0, -1, 0x11}, {0, 0x12, 0, 32, 0x12, 6, 0, -1, 0x11},
    {0, 0x13, 0, 35, 0, 3, 0}, {0, 0x14, 0, 35, 0, 3, 0}, {0, 0x15, 0, 35, 0, 3, 0}, {0, 0x16, 0, 35, 0, 3, 0},
    {0, 0x17, 0, 35, 0, 3, 0}, {0, 0x18, 0, 35, 0, 3, 0}, {0, 0x19, 0, 35, 0, 3, 0},
    {0, 0x2C, 14, 36, 8, 0, 1},
    {0, 0x2A, 36, 22, 10, 8, 1}, {0, 0x31, 50, 20, 0x14, 10, 1},   // IW-FX-04: Char_TickShotImpact, Char_SpawnChargeEffect
    {1, 2, 5, 14, 0x12, 0, 1}, {1, 2, 15, 14, 0x12, 0, 1}, {1, 2, 25, 14, 0x12, 0, 1},
    {1, 4, 0, 14, 0x12, 0, 1}, {1, 7, 0, 14, 0x12, 0, 1}, {1, 0xA, 0, 14, 0x12, 0, 1}, {1, 0xD, 0, 14, 0x12, 0, 1},
    {1, 5, 0, 30, 0x12, 0, 1}, {1, 5, 0, 13, 0x12, 0, 1}, {1, 8, 0, 30, 0x12, 0, 1}, {1, 8, 0, 13, 0x12, 0, 1},
    {1, 0xB, 0, 30, 0x12, 0, 1}, {1, 0xB, 0, 13, 0x12, 0, 1}, {1, 0xE, 0, 30, 0x12, 0, 1},
    {1, 0x10, 0, 0, 0, 0, 1}, {1, 0x10, 16, 33, 0x12, 0, 1},
    {1, 0x11, 0, 34, 0, 3, 0}, {1, 0x11, 0, 41, 0, 5, 0, -1, 0x10}, {1, 0x11, 0, 32, 0x12, 7, 0, -1, 0x10},
    {1, 0x12, 0, 34, 0, 3, 0}, {1, 0x13, 0, 34, 0, 3, 0}, {1, 0x14, 0, 34, 0, 3, 0}, {1, 0x15, 0, 34, 0, 3, 0},
    {1, 0x16, 0, 34, 0, 3, 0}, {1, 0x17, 0, 34, 0, 3, 0}, {1, 0x18, 0, 34, 0, 3, 0},
    {1, 0x2B, 0, 0, 0, 0, 1}, {1, 0x2B, 14, 36, 8, 0, 1},
    {1, 0x29, 36, 22, 4, 9, 1}, {1, 0x31, 50, 20, 0x14, 10, 1},   // IW-FX-04: Char_PoseTickKiDrain, 0x358520
    // IW-FX-05: Char_EnterPoseState0x2f (0x358950): ExecuteScript_MMI2 (0x11DEF8) = memset(param, 0, 0x20) antes de
    // rellenar pos/dir, asi que OnBone (Cmd_EmitOp14 0x2F7460) lee nodo +0xC = 0 y actor +0x14 = 0 (Vegeta); 0 = matar
    {1, 0x2F, 0, 20, 0, 10, 1},
};
// IW-FX-03: 7DC (NB014): jugador 0x49A150/0x49A1B0 N=22, rival 0x49A020/0x49A0C0 N=19 (salidas
// E:/DBZ3HD_trabajo/iw-fx-03/nb014_j.txt = rival, nb014_r.txt = jugador). Pendiente: rival 0x10 y 0x12 (op 20, OnBone).
constexpr FxPose kFxNB014[] = {
    {0, 5, 0, 31, 0x12, 0, 1}, {0, 8, 0, 31, 0x12, 0, 1}, {0, 0xB, 0, 31, 0x12, 0, 1}, {0, 0xE, 0, 31, 0x12, 0, 1},
    {0, 6, 0, 13, 0x12, 0, 1}, {0, 9, 0, 13, 0x12, 0, 1}, {0, 0xC, 0, 13, 0x12, 0, 1}, {0, 0xF, 0, 13, 0x12, 0, 1},
    {1, 2, 5, 14, 0x12, 0, 1}, {1, 2, 15, 14, 0x12, 0, 1}, {1, 2, 25, 14, 0x12, 0, 1},
    {1, 4, 0, 14, 0x12, 0, 1}, {1, 7, 0, 14, 0x12, 0, 1}, {1, 0xA, 0, 14, 0x12, 0, 1}, {1, 0xD, 0, 14, 0x12, 0, 1},
    {1, 5, 0, 30, 0x12, 0, 1}, {1, 8, 0, 30, 0x12, 0, 1}, {1, 0xB, 0, 30, 0x12, 0, 1}, {1, 0xE, 0, 30, 0x12, 0, 1},
    {1, 0x12, 50, 20, 0x14, 10, 1},   // IW-FX-04: Char_SpawnEffectAtFrame50 (0x3341B0)
    {1, 0x10, 0, 20, 0, 10, 1},   // IW-FX-05: Char_EnterPoseState0x10 (0x334590): memset 0x20 -> nodo 0 del actor 0
};
// Camera_GetViewDir / ojo: los de la camara de IW en curso (clip del #AMC, mundo de IW)
bool VistaIW(ps2::V3& ojo, ps2::V3& dir) {
  const auto it = g_clips.find(g_cb_clip.load());
  if (it == g_clips.end()) return false;
  float e[3], t[3];
  MuestraClip(it->second, CuadroClip(it->second), e, t, nullptr, nullptr);
  ojo = {e[0], e[1], e[2]};
  dir = {t[0] - e[0], t[1] - e[1], t[2] - e[2]};
  const float l = iwame::Largo(dir);
  if (l < 1e-4f) return false;
  dir = iwame::Por(dir, 1 / l);
  return true;
}
void LanzarFxPose(Juego& j, const FxPose& e) {
  if (e.modo == 2) {   // la misma ruta de las antenas de siempre (933/29 con su modelo)
    j.antena_pendiente = 0;
    j.antena_codigo = AliasPose(*j.poses, 1, j.pose_codigo[1]);
    return;
  }
  if (e.salvo >= 0 && j.pose_estado[1 - e.rol] == e.salvo) return;
  if (e.tras >= 0 && j.pose_previo[e.rol] != e.tras) return;
  // ponytail: Effect_KillByOwnerFlags mata todos los de dueno 0; aqui solo los de esta tabla (las antenas siguen).
  if (e.matar == 1 || (e.matar == 2 && j.fx_cuenta > 1)) { j.fx_pose.clear(); j.fx_cuenta = 0; }
  const auto it = j.fx933.find(e.op);
  if (it == j.fx933.end() || (it->second.grupos.empty() && it->second.raices.empty())) return;
  ps2::V3 ojo, vista{0, 0, 1}, pos;
  const bool cam = VistaIW(ojo, vista);
  ps2::M34 m;
  if (e.modo == 4 || e.modo == 5) {   // Char_PoseTickCameraFx: Camera_GetMatrixPtr (ojo) + Camera_GetViewDir * 60, en el espacio de los huesos
    (void)ojo; (void)cam;
    const ps2::Camara cb = ps2::CamaraCombate(Base(), ImGui::GetIO().DisplaySize);
    if (!cb.ok) return;
    const float* v = cb.m;
    ps2::V3 eye;
    for (int i = 0; i < 3; ++i) (&eye.x)[i] = -(v[12] * v[4*i] + v[13] * v[4*i+1] + v[14] * v[4*i+2]) - g_origen[i];
    ps2::V3 fwd{v[2], v[6], v[10]};
    if (MatrizNodo(Base(), e.rol, 0, m)) {   // el eje de la vista mira hacia el luchador
      const ps2::V3 a{m.m[3] - eye.x, m.m[7] - eye.y, m.m[11] - eye.z};
      if (a.x * fwd.x + a.y * fwd.y + a.z * fwd.z < 0) fwd = iwame::Por(fwd, -1);
    }
    const float l = iwame::Largo(fwd);
    if (l < 1e-4f) return;
    vista = iwame::Por(fwd, 1 / l);
    pos = iwame::Mas(eye, iwame::Por(vista, e.modo == 5 ? 150.0f : 60.0f));
  } else if (e.modo == 10) {
    if (!MatrizNodo(Base(), 1 - e.rol, e.nodo, m)) return;
    pos = {0, 0, 0};
  } else {
    if (!MatrizNodo(Base(), e.modo == 3 ? 1 - e.rol : e.rol, e.modo == 9 ? 4 : e.nodo, m)) return;
    pos = {m.m[3], m.m[7], m.m[11]};
    if (e.modo == 7) pos.x -= 8;
  }
  ps2::V3 dir = vista;
  if (e.modo == 1) {   // Pos_DiffNodeVec(nodo 0x12, nodo - 100 en z): EU 0x374408 lwc1 72(sp) = z del vector de sp+64
    ps2::M34 m12;
    if (!MatrizNodo(Base(), e.rol, 0x12, m12)) return;
    dir = {m12.m[3] - pos.x, m12.m[7] - pos.y, m12.m[11] - (pos.z - 100)};
    const float l = iwame::Largo(dir);
    dir = l > 1e-4f ? iwame::Por(dir, 1 / l) : vista;
  } else if (e.modo == 6 || e.modo == 7) {   // NB013: Pos_DiffNodeVec(nodo 0x12, nodo 0x12 +/- 100 en x)
    dir = {e.modo == 6 ? -1.0f : 1.0f, 0, 0};
  }
  else if (e.modo == 8 || e.modo == 9) {   // IW-FX-04: Pos_DiffTwoNodes / Pos_DiffNodeOffsetVec (EU 0x372070 / 0x372160)
    ps2::M34 m2;
    if (!MatrizNodo(Base(), e.modo == 8 ? 1 - e.rol : e.rol, e.modo == 8 ? 0x12 : 6, m2)) return;
    dir = e.modo == 8 ? ps2::V3{pos.x - m2.m[3], pos.y - m2.m[7], pos.z - m2.m[11]}
                      : ps2::V3{m2.m[3] - pos.x, m2.m[7] - pos.y, m2.m[11] - pos.z};
    const float l = iwame::Largo(dir);
    dir = l > 1e-4f ? iwame::Por(dir, 1 / l) : vista;
  } else if (e.modo == 10) {
    dir = {0, 0, 1};   // local al hueso
  }
  iwame::ame::Instancia in;
  in.ef = &it->second;
  in.origen = pos;   // como las antenas: sin mezcla_gs (con ella un emisor de duracion < 0 no se apaga nunca)
  in.dir = dir;
  in.encadenar = true;   // IW-FX-03: particulas hijas al morir (933/25 y 26 de los 43 hijos de 933 las llevan)
  in.modelos = in.fiel = true;   // IW-FX-05: modelos (933/24 Masenko acertado), emisor caja y afectores como el original
  if (e.modo == 10) { in.hueso_rol = 1 - e.rol; in.hueso_nodo = e.nodo; }
  j.fx_pose.push_back(std::move(in));
  ++j.fx_cuenta;
  REXLOG_INFO("dbz3 iwfx02: rol {} estado 0x{:X} pose 0x{:X} cuadro {:.0f} -> 933/{} nodo 0x{:X} en ({:.2f},{:.2f},{:.2f}) dir ({:.2f},{:.2f},{:.2f}) {}g {}r",
              e.rol, e.estado, j.pose_codigo[e.rol], e.cuadro, e.op, e.nodo, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z,
              it->second.grupos.size(), it->second.raices.size());
}
void CuadroFxPose(Juego& j) {
  if (!EfectosIW() || j.fx933.empty()) return;
  const FxPose *t0 = nullptr, *t1 = nullptr;
  if (j.poses == &kPosesNB006) { t0 = std::begin(kFxNB006); t1 = std::end(kFxNB006); }
  else if (j.poses == &kPosesNB013) { t0 = std::begin(kFxNB013); t1 = std::end(kFxNB013); }
  else if (j.poses == &kPosesNB014) { t0 = std::begin(kFxNB014); t1 = std::end(kFxNB014); }
  else return;
  for (int rol = 0; rol < 2; ++rol) {
    const float f = CuadroPoseBotones(j, rol);
    if (f < 0) continue;
    int k = 0;
    for (const FxPose* pe = t0; pe != t1; ++pe) {
      const FxPose& e = *pe;
      if (e.rol != rol || e.estado != j.pose_estado[rol]) continue;
      if (k++ < j.fx_paso[rol]) continue;
      if (f < e.cuadro) break;
      ++j.fx_paso[rol];
      LanzarFxPose(j, e);
    }
  }
  for (auto& in : j.fx_pose) {
    iwame::ame::Cuadro(in, j.antena_sem);
    if (in.edad == 10 || in.edad == 40 || !in.vivo) {   // diagnostico: particulas vivas (Masenko 933/25 sin ver)
      size_t n = 0;
      for (const auto& l : in.ps) n += l.size();
      REXLOG_INFO("dbz3 iwfx02: vida edad {:.0f} vivo {} particulas {} grupos {}", in.edad, in.vivo ? 1 : 0, n, in.ef->grupos.size());
    }
  }
  j.fx_pose.erase(std::remove_if(j.fx_pose.begin(), j.fx_pose.end(), [](const auto& in) { return !in.vivo; }), j.fx_pose.end());
}
// Un cuadro de todos los efectos 3D del minijuego (antenas, combo y los de las poses).
void CuadroEfectos(Juego& j) {
  CuadroAntenas(j);
  CuadroFxPose(j);
  for (auto& in : j.combo_fx) iwame::ame::Cuadro(in, j.antena_sem);
  j.combo_fx.erase(std::remove_if(j.combo_fx.begin(), j.combo_fx.end(), [](const auto& in) { return !in.vivo; }), j.combo_fx.end());
}
void IniciarChoque13(Juego& j,int l,int variante) {
  static constexpr int objetivos[6]={10,15,15,20,20,25}; // EU350B60
  j.especial_fase=0;
  j.especial_lado=l;
  j.objetivo_pulsos=objetivos[j.dif];
  j.pulsos=0;
  j.especial_t=0;
  j.reaccion_pulso=false;
  if (l) {
    ++j.c[1].llegaron; // StartClashSequence incrementa+48, aparte del icono
    Estado13(j,0,variante ? 0x22 : 0x1A);
    Estado13(j,1,0x10);
  } else {
    Estado13(j,0,0x11);
    if (!variante) Estado13(j,1,0x21);
  }
  CamB(l ? (variante ? 0x18B : 0x188) : (variante ? 0x188 : 0x18A));
  REXLOG_INFO("dbz3 solIW5: NB013 choque inicio lado {} variante {} celda {} objetivo {} marca {:.1f}/{:.1f}",
              l,variante,j.especial_indice,j.objetivo_pulsos,j.marca[0],j.marca[1]);
}
void PosesChoque13(Juego& j) {
  const int rol = j.especial_lado, s = j.pose_estado[rol];
  const int ini = rol ? 0x10 : 0x11, ciclo = ini + 1;
  if (s < ini || s >= ciclo + 8 || !FinPoseBotones(j, rol)) return;
  const int n = s == ciclo + 7 ? ciclo : s + 1;
  Estado13(j, rol, n);
  if (s == ini) {
    Estado13(j, 1 - rol, rol ? 0x1A : 0x19);
  } else if (n > ciclo) {
    // Entradas 0x34E820..0x34DCE0 (carril 0) / 0x35A950..0x359E20 (carril 1).
    // El reinicio del ciclo no elige otra reaccion ni consume la marca +0x1AAC.
    const int base = rol ? (j.reaccion_pulso ? 0x23 : 0x1B) : (j.reaccion_pulso ? 0x1A : 0x22);
    Estado13(j, 1 - rol, base + n - ciclo - 1);
    j.reaccion_pulso = false;
  }
}
void ResolverChoque13(Juego& j) {
  j.especial_bien=j.pulsos>=j.objetivo_pulsos;
  const bool b=j.especial_bien;
  const int l=j.especial_lado;
  Estado13(j,0,l ? (b ? 0x2D : 0x2E) : (b ? 0x2A : 0x2C));
  Estado13(j,1,l ? (b ? 0x2B : 0x29) : (b ? 0x2D : 0x2C));
  CamB(l ? (b ? 0x18A : 0x18C) : (b ? 0x189 : 0x18A),false,!l&&!b);
  if (b) ++j.c[l].aciertos;
  j.especial_fase=2;
  j.dano_aplicado=false;
  ++j.rondas;
  REXLOG_INFO("dbz3 solIW5: NB013 choque resultado lado {} pulsos {}/{} acierto {} ronda {}",
              l,j.pulsos,j.objetivo_pulsos,b,j.rondas);
}
static bool QtePal() { static const bool v = std::getenv("DBZ3_IW_QTE_PAL") != nullptr; return v; }
static float PasoLogica() { return QtePal() ? 1.0f / 50 : 1.0f / 60; }

// eIW3 (solo pruebas, DBZ3_TEST_QTE_LOG=1): una linea por icono que sale, para contrastar la cadencia con el original.
static void LogSpawn(const Juego& j, int l, int i, int boton) {
  static const bool on = std::getenv("DBZ3_TEST_QTE_LOG") != nullptr;
  if (on) REXLOG_INFO("dbz3 qte: spawn cuadro {} carril {} indice {} boton {}", j.cuadros, l, i, boton);
}

void PasoChoque13(Juego& j,uint16_t pulsado) {
  ++j.especial_t;
  if (j.especial_fase==0) {
    for (float& x:j.marca) x+=j.vel;
    j.marca_gate=SolapaMarca(j);
    const int boton=j.c[j.especial_lado].seq[j.especial_indice];
    // QTE_CheckPressedMatch compara el ULTIMO bit del pad completo.
    if (j.marca_gate && (BotonPulsado(pulsado)==boton ||
        (j.auto_ && !j.pierde && !(j.fallos && j.especial_lado==1) && j.especial_t%4==0))) {
      ++j.pulsos;
      j.reaccion_pulso=true;
    }
    if (Choque13IW() && j.pulsos>=j.objetivo_pulsos && !j.reaccion_pulso) {
      j.reaccion_pulso=true;   // medidor UI 0x2F a 0 -> +0x1AAC = 1 (EU 0x353090)
      if (j.especial_t%60==0) REXLOG_INFO("dbz3 choque13: objetivo {} alcanzado ({} pulsos), 1AAC fijo",j.objetivo_pulsos,j.pulsos);
    }
    PosesChoque13(j);
    if (!j.marca_gate && j.marca[0]>=kObjetivo) {
      j.especial_fase=1;
      REXLOG_INFO("dbz3 solIW5: NB013 choque salida solape pulsos {} marca {:.1f}",j.pulsos,j.marca[0]);
    }
  } else if (j.especial_fase==1) ResolverChoque13(j);
  else {
    const bool golpe=j.especial_lado ? !j.especial_bien : j.especial_bien;
    const int atacante=j.especial_lado;
    if (golpe && !j.dano_aplicado && CuadroPoseBotones(j,atacante)>=36) {
      static constexpr int dano[2][6]={{300,300,200,200,100,100},{100,200,200,300,300,300}};
      j.vida[1-atacante]-=float(dano[atacante][j.dif]);
      j.dano_aplicado=true;
      REXLOG_INFO("dbz3 solIW5: NB013 dano especial rol {} cuadro {:.1f} vida {:.0f}/{:.0f}",
                  atacante,CuadroPoseBotones(j,atacante),j.vida[0],j.vida[1]);
    }
    for (int rol=0;rol<2;++rol) if (FinPoseBotones(j,rol)) {
      const int s=j.pose_estado[rol];
      if (s==(rol ? 0x29 : 0x2A)) {
        // 191C es KO y solo se publica al acabar el golpe55B.
        if (j.vida[1-rol]<=0) { FinalizarBotones(j); return; }
        Estado13(j,rol,rol ? 0x2A : 0x2B);
      } else if (s==(rol ? 0x2D : 0x2E)) Estado13(j,rol,rol ? 0x2E : 0x2F);
      else if (s>=0x29) Estado13(j,rol,rol ? 2 : 3);
    }
    if (j.pose_estado[0]==3 && j.pose_estado[1]==2) {
      Icono& ic=j.c[j.especial_lado].ic[j.especial_indice];
      ic.resuelto=true;
      ic.salida=true;
      ic.anim_t=0;
      j.especial_fase=-1;
      j.marca_activa=false;
      CamB(0x187,false,true,true);
      REXLOG_INFO("dbz3 solIW5: NB013 vuelve187 ronda {} vida {:.0f}/{:.0f}",j.rondas,j.vida[0],j.vida[1]);
    }
  }
}

void PasoNB013(Juego& j,uint16_t pulsado) {
  ++j.cuadros;
  if (j.entrada_reposo>0) --j.entrada_reposo;
  if (j.aviso_t>0) --j.aviso_t;
  for (int& t:j.destello) if (t>=0) ++t;
  if (j.especial_fase>=0) { PasoChoque13(j,pulsado); return; }
  if (j.vida[0]<=0 || j.vida[1]<=0) { FinalizarBotones(j); return; }
  const bool barra=j.marca_activa && j.marca[1]>=0 && j.marca[0]<=kW;
  bool especial_vivo=false;
  for (auto& c:j.c) for (const auto& ic:c.ic) especial_vivo|=ic.vivo&&ic.especial&&!ic.resuelto;
  for (int l=0;l<2;++l) {
    Carril& c=j.c[l];
    if (!barra && !especial_vivo) --c.cuenta;
    if (c.cuenta<=0 && c.sale<c.n) {
      Icono& ic=c.ic[c.sale];
      LogSpawn(j,l,c.sale,c.seq[c.sale]);
      ic=Icono{c.seq[c.sale],-16-j.vel,true};
      ic.especial=j.especiales[l][c.sale]!=0;
      if (ic.especial) ReiniciarMarca(j,l,c.sale);
      ++c.sale;
      c.cuenta=c.sale<c.n ? c.hueco[c.sale] : 0;
    }
    for (int i=0;i<c.sale;++i) {
      Icono& ic=c.ic[i];
      if (!ic.vivo) continue;
      if (ic.salida) { if (++ic.anim_t>=j.salida_dur) ic.vivo=false; continue; }
      if (ic.cruz) ++ic.anim_t;
      ic.x+=j.vel;
      const bool solapa=SolapaIcono(ic.x);
      if (!ic.contado && solapa && !ic.resuelto) { ic.contado=true; ++c.llegaron; }
      if (!ic.resuelto && !solapa && ic.x>kObjetivoUI) {   // EU 0x332270: pantalla/2 + 150 < x (pantalla = 600)
        if (!ic.contado) { ic.contado=true; ++c.llegaron; }
        if (ic.especial && l==0 && ic.preparado) { IniciarChoque13(j,0,1); return; }
        ic.resuelto=true;
        ic.cruz=true;
        ic.anim_t=20;
        if (l==1 || !ic.especial) {
          PosesGolpe(j,l,false);
          if (l==1) Golpe(j);
        } else j.marca_activa=false;
      }
      if (ic.x>=kQuita && !ic.especial) { ic.salida=true; ic.anim_t=0; }
      if (ic.x>=kW+43) ic.vivo=false;
    }
    // Mg_FindNearestMarker: el activo mas cercano a x_objetivo+7.
    float distancia=1e9f;
    c.actual=c.sale;
    for (int i=0;i<c.sale;++i) if (c.ic[i].vivo&&!c.ic[i].resuelto) {
      const float d=std::fabs(kObjetivo+7-c.ic[i].x);
      if (d<distancia) distancia=d,c.actual=i;
    }
  }
  // Solo el carril mas cercano resuelve la presion; dos iconos solapados>=10
  // requieren las DOS teclas mantenidas (Mg_UpdateHeldInputResolution).
  Icono* ic[2]={j.c[0].actual<j.c[0].sale ? &j.c[0].ic[j.c[0].actual] : nullptr,
                j.c[1].actual<j.c[1].sale ? &j.c[1].ic[j.c[1].actual] : nullptr};
  int l=!ic[0] ? 1 : !ic[1] ? 0 : std::fabs(477-ic[0]->x)<std::fabs(477-ic[1]->x) ? 0 : 1;
  const bool doble=ic[0]&&ic[1]&&std::fabs(ic[0]->x-ic[1]->x)<=22;
  const bool gate=ic[l]&&SolapaIcono(ic[l]->x);
  uint16_t entrada=pulsado;
  if (j.auto_ && gate && !(j.fallos && l==1 && j.c[l].actual%5==2)) {
    entrada=uint16_t(1u<<ic[l]->boton);
    if (doble) entrada|=uint16_t(1u<<ic[1-l]->boton);
  }
  // EU353490: dos teclas mantenidas, tolerancia10cuadros desde el primer
  // solape y5cuadros de enfriamiento. No exigir dos flancos simultaneos.
  if (doble) entrada|=uint16_t(j.antes&0xFF00);
  const bool bien=gate&&(doble ? ((j.antes|entrada)&(1u<<ic[0]->boton))&&((j.antes|entrada)&(1u<<ic[1]->boton))
                              : ic[l]&&BotonPulsado(entrada)==ic[l]->boton);
  bool esperar=false;
  if (doble&&gate&&!bien&&entrada) {
    if (j.espera_doble<0) j.espera_doble=0;
    esperar=j.espera_doble++<=10;
  } else if (!doble) j.espera_doble=-1;
  if (ic[l] && ic[l]->x>kAceptaUI && entrada && !ic[l]->preparado && !esperar && !j.entrada_reposo) {   // EU 422950: (600/2+150)-100 < x
    const bool combo = doble && !ic[0]->especial && !ic[1]->especial;
    const float vida_antes[2] = {j.vida[0],j.vida[1]};
    for (int q=0;q<(doble ? 2 : 1);++q) {
      const int lado=doble ? q : l;
      Icono& k=*ic[lado];
      if (!k.contado) { k.contado=true; ++j.c[lado].llegaron; }
      if (k.especial && lado==0) {
        if (bien) k.preparado=true;
        else k.resuelto=true,j.marca_activa=false;
      } else {
        k.resuelto=true;
        k.salida=bien;
        k.cruz=!bien;
        k.anim_t=bien ? 0 : 20;
        if (bien) ++j.c[lado].aciertos;
        // Mg_ApplyHitDamage(2,1) NO resta HP por acertar un combo.
        if (lado==0 && bien && !combo) j.vida[1]-=float(j.m->ataque_jugador[j.dif]);
        if (lado==1 && !bien) Golpe(j);
        if (!combo) PosesGolpe(j,lado,bien);
      }
      j.destello[lado]=bien ? 0 : j.destello[lado];
      j.aviso_t=30;j.aviso_bien=bien;j.aviso_carril=lado;
    }
    if (combo) {
      PosesGolpe(j,2,bien);
      j.aviso_carril=2;
      if (std::getenv("DBZ3_TEST_IWMINI"))
        REXLOG_INFO("dbz3 solIWA: NB013 combo acierto {} vida {:.0f}/{:.0f} -> {:.0f}/{:.0f}",
                    bien,vida_antes[0],vida_antes[1],j.vida[0],j.vida[1]);
    }
    j.espera_doble=-1;
    j.entrada_reposo=5;
  }
  if (j.marca_activa) {
    for (float& x:j.marca) x+=j.vel;
    const float trigger=kObjetivo-j.vel*60-j.marca_ancho-48;
    if (j.especial_lado==1 && !j.marca_entrada && j.marca[0]>trigger) {
      j.marca_entrada=true;
      IniciarChoque13(j,1,1);
      return;
    }
    if (j.marca[0]>kW) j.marca_activa=false;
  }
  bool gastado=true;
  for (auto& c:j.c) {
    gastado&=c.sale>=c.n;
    for (const auto& k:c.ic) gastado&=!k.vivo;
  }
  if (gastado) {
    // Mg_AdvanceQteSequence recarga, no concede victoria por agotar50 iconos.
    ++j.ciclos;
    for (auto& c:j.c) { c.sale=c.actual=0;c.cuenta=c.hueco[0];for(auto& k:c.ic) k={}; }
    REXLOG_INFO("dbz3 solIW5: NB013 recarga secuencias ciclo {}",j.ciclos);
  }
}

void Pose06(Juego& j,int rol,uint32_t codigo) {
  j.pose_codigo[rol]=codigo;
  Poner(*j.poses,rol,codigo,false);
}
void IniciarRonda06(Juego& j) {
  static constexpr int objetivos[6]={30,35,40,50,50,70}; // EU376790
  j.especial_fase=0;
  j.pulsos=0;
  j.objetivo_pulsos=objetivos[j.dif];
  j.especial_t=0;
  j.remate_boton=-1;
  j.remate_resuelto=false;
  j.especial_bien=false;
  ++j.c[0].llegaron;
  Pose06(j,0,0x285); // estado10; al rival no se le cambia el codigo
  CamB(0x98);
  REXLOG_INFO("dbz3 solIW5: NB006 ronda98 celda {} objetivo {} marca {:.1f}/{:.1f}",
              j.especial_indice,j.objetivo_pulsos,j.marca[0],j.marca[1]);
}
void PasoRonda06(Juego& j,uint16_t pulsado,uint16_t palanca) {
  ++j.especial_t;
  if (j.remate_resuelto) ++j.remate_t;
  if (j.especial_fase==0) {
    for (float& x:j.marca) x+=j.vel;
    j.marca_gate=SolapaMarca(j);
    if (j.marca_gate && (palanca || (j.auto_&&!j.fallos&&j.especial_t%3==0))) ++j.pulsos;
    if (j.pose_codigo[0]==0x285 && FinPoseBotones(j,0) && j.pulsos) Pose06(j,0,0x286);
    if (!j.marca_gate && j.marca[0]>=kObjetivo) {
      j.especial_fase=1;
      REXLOG_INFO("dbz3 solIW5: NB006 salida solape pulsos {}/{}",j.pulsos,j.objetivo_pulsos);
    }
  } else if (j.especial_fase==1) {
    if (j.pulsos<j.objetivo_pulsos) j.especial_fase=3;
    else {
      static constexpr int botones[4]={13,15,12,14}; // EU485E10 comprobadaUSA/EU
      static constexpr int anticipa[6]={80,70,60,50,40,30};
      j.remate_boton=botones[Azar()&3];
      j.remate_x=kObjetivo-float(anticipa[j.dif])*j.vel;
      j.remate_t=0;
      j.especial_fase=2;
      REXLOG_INFO("dbz3 solIW5: NB006 remate boton {} x {:.1f}",j.remate_boton,j.remate_x);
    }
  } else if (j.especial_fase==2) {
    ++j.remate_t;
    j.remate_x+=j.vel;
    const bool solapa = REXCVAR_GET(dbz3_iw_7d4_ventana_remate_pal)
                          ? SolapaRematePal06(j.remate_x) : SolapaObjetivo(j.remate_x,-16,16);
    uint16_t p=pulsado;
    if (j.auto_&&solapa&&!j.fallos) p=uint16_t(1u<<j.remate_boton);
    if ((solapa&&p) || (!solapa&&j.remate_x>kObjetivo)) {
      j.especial_bien=solapa&&BotonPulsado(p)==j.remate_boton;
      j.remate_resuelto=true;
      j.remate_t=0;
      if (j.especial_bien) { ++j.c[0].aciertos;j.destello[0]=0; }
      j.especial_fase=3;
    }
  } else if (j.especial_fase==3) {
    if (j.remate_resuelto || FinPoseBotones(j,0)) {
      Pose06(j,0,j.especial_bien ? 0x287 : 0x289);
      Pose06(j,1,j.especial_bien ? 0x288 : 0x28A);
      j.especial_fase=4;
      j.dano_aplicado=false;
      ++j.rondas;
      CamB(0x9A);
      REXLOG_INFO("dbz3 solIW5: NB006 ronda9A acierto {} pulsos {}/{} ronda {}",
                  j.especial_bien,j.pulsos,j.objetivo_pulsos,j.rondas);
    }
  } else if (j.especial_fase==4) {
    if (j.especial_bien&&!j.dano_aplicado&&CuadroPoseBotones(j,0)>=8) {
      static constexpr int dano[6]={400,350,300,250,200,150};
      j.vida[1]-=float(dano[j.dif]);
      j.dano_aplicado=true;
      REXLOG_INFO("dbz3 solIW5: NB006 dano especial cuadro {:.1f} vida {:.0f}/{:.0f}",CuadroPoseBotones(j,0),j.vida[0],j.vida[1]);
    }
    if (FinPoseBotones(j,0)&&FinPoseBotones(j,1)) {
      if (j.vida[0]<=0||j.vida[1]<=0) { FinalizarBotones(j);return; }
      j.especial_fase=-1;
      j.marca_activa=false;
      Poner(*j.poses,0,0x258,true);
      Poner(*j.poses,1,0x259,true);
      CamB(0x97,false,true,true);
      REXLOG_INFO("dbz3 solIW5: NB006 vuelve97 ronda {} vida {:.0f}/{:.0f}",j.rondas,j.vida[0],j.vida[1]);
    }
  }
}

// Ensayo causal opt-in A/B/A, solo en la copia de pruebas. Lee una peticion
// explicita, confirma la cvar CONSUMIDA porGPU y restaura su valor al salir.
void EnsayoLuzNB006(bool activo) {
  static const char* ruta=std::getenv("DBZ3_SOLIW5_RIM");
  static bool guardado=false;
  static double anterior=1, aplicado=-1;
  if (!ruta) return;
  if (!activo) {
    if (guardado) rex::cvar::SetFlagByName("dbz3_rim_light_scale",std::to_string(anterior));
    guardado=false;
    aplicado=-1;
    return;
  }
  if (!guardado) { anterior=rex::cvar::Query<double>("dbz3_rim_light_scale");guardado=true; }
  double v=-1;
  if (std::ifstream in(ruta);in>>v && (v==0||v==1) && v!=aplicado) {
    rex::cvar::SetFlagByName("dbz3_rim_light_scale",std::to_string(v));
    const double real=rex::cvar::Query<double>("dbz3_rim_light_scale");
    REXLOG_INFO("dbz3 solIW5: luz rim solicitado {:.1f} efectivo {:.1f} clip 0x{:X} cuadro {}",
                v,real,g_cb_clip.load(),g_cb_frame.load());
    if (real==v) aplicado=v;
  }
}

// NB014, EU332270/332CA0/330760: primero mueve ambos carriles, elige
// el icono mas proximo y resuelve UNA accion (individual o combo mantenido).
void PasoNB014(Juego& j, uint16_t pulsado) {
  ++j.cuadros;
  if (j.aviso_t > 0) --j.aviso_t;
  if (j.entrada_reposo > 0) --j.entrada_reposo;
  for (int& t:j.destello) if (t>=0) ++t;
  Icono* elegidos[2]={};
  for (int l=0;l<2;++l) {
    Carril& c=j.c[l];
    if (--c.cuenta<=0 && c.sale<c.n) {
      LogSpawn(j,l,c.sale,c.seq[c.sale]);
      c.ic[c.sale]=Icono{c.seq[c.sale],-16-j.vel,true};
      ++c.sale;
      c.cuenta=c.sale<c.n ? c.hueco[c.sale] : 0;
    }
    float cerca=1e9f;
    c.actual=c.sale;
    for (int i=0;i<c.sale;++i) {
      Icono& k=c.ic[i];
      if (!k.vivo) continue;
      if (k.salida) { if (++k.anim_t>=j.salida_dur) k.vivo=false; continue; }
      k.x+=j.vel;
      if (k.cruz) ++k.anim_t;
      if (!k.contado&&!k.resuelto&&SolapaIcono(k.x)) { k.contado=true;++c.llegaron; }
      if (k.x>=kQuita) { k.salida=true;k.anim_t=0; }
      if (!k.resuelto) {
        const float d=std::fabs(477-k.x);
        if (d<cerca) { cerca=d;c.actual=i;elegidos[l]=&k; }
      }
    }
  }
  int lado=!elegidos[0] ? 1 : !elegidos[1] ? 0 :
      std::fabs(477-elegidos[0]->x)<std::fabs(477-elegidos[1]->x) ? 0 : 1;
  const bool doble=elegidos[0]&&elegidos[1]&&std::fabs(elegidos[0]->x-elegidos[1]->x)<=22;
  Icono* k=elegidos[lado];
  uint16_t entrada=pulsado;
  const bool gate=k&&SolapaIcono(k->x);
  if (j.auto_&&gate&&!(j.fallos&&lado==1&&j.c[lado].actual%5==2)) {
    entrada=uint16_t(1u<<k->boton);
    if (doble) entrada|=uint16_t(1u<<elegidos[1-lado]->boton);
  }
  const uint16_t held=uint16_t(j.antes|entrada);
  const bool bien=gate&&(doble ? SolapaIcono(elegidos[1-lado]->x)&&
      (held&(1u<<elegidos[0]->boton))&&(held&(1u<<elegidos[1]->boton)) :
      BotonPulsado(entrada)==k->boton);
  const bool pasada=k&&!gate&&k->x>kObjetivoUI;   // EU 0x332270: sin solape y pantalla/2 + 150 < x (pantalla = 600)
  bool esperar=false;
  if (doble&&gate&&!bien&&(held&0xFF00)) {
    if (j.espera_doble<0) j.espera_doble=0;
    esperar=j.espera_doble++<=10;
  } else if (!doble) j.espera_doble=-1;
  if (k && (pasada || (k->x>kAceptaUI&&(doble ? (held&0xFF00)!=0 : entrada!=0))) &&
      !esperar&&!j.entrada_reposo) {
    const bool acierto=!pasada&&bien;
    for (int q=0;q<(doble ? 2 : 1);++q) {
      const int l=doble ? q : lado;
      Icono& ic=*elegidos[l];
      if (!ic.contado) { ic.contado=true;++j.c[l].llegaron; }
      ic.resuelto=true;ic.salida=acierto;ic.cruz=!acierto;ic.anim_t=acierto ? 0 : 20;
      j.ultimo_pulsado=!pasada;
      if (acierto) { ++j.c[l].aciertos;j.destello[l]=0; }
    }
    // EU3301C0: un combo acertado NO resta HP; el fallo conjunto resta
    // un solo ataque del rival, no uno por carril.
    if (!acierto&&(doble||lado==1)) Golpe(j);
    if (acierto&&!doble&&lado==0) j.vida[1]-=float(j.m->ataque_jugador[j.dif]);
    PosesGolpe(j,doble ? 2 : lado,acierto);
    if (TinteIW()) {
      if ((!acierto&&(doble||lado==1)) || (acierto&&!doble&&lado==0))
        iwtinte::Qte(g_chr_paleta, g_chr_fade, g_chr_total, acierto ? 1 : 0, "330410");
    } else if ((!acierto&&(doble||lado==1)) || (acierto&&!doble&&lado==0))
      g_chr_total=15;g_chr_fade[acierto ? 1 : 0]=15; // EU330410: no se aplica al combo acertado
    j.aviso_t=30;j.aviso_bien=acierto;j.aviso_carril=doble ? 2 : lado;
    j.espera_doble=-1;j.entrada_reposo=doble ? 10 : 0;
    if (doble&&std::getenv("DBZ3_TEST_IWMINI"))
      REXLOG_INFO("dbz3 solIW6: NB014 combo acierto {} cuadros {} vida {:.0f}/{:.0f} poses57C/57D {}",acierto,j.cuadros,j.vida[0],j.vida[1],acierto);
  }
  // fase 1 (7/11): EU QteMg_ProcessPlayerInput (422818/422892/422972) cierra la partida solo cuando el ultimo icono de
  // los dos carriles se resuelve con una PULSACION; si los ultimos pasan sin pulsar, QteMg_AdvanceRoundIfBothDone
  // (0x32FCE0) recarga las dos secuencias y se juega otro ciclo (vida y aciertos siguen). Antes el port terminaba siempre.
  bool gastado=true;
  for (const auto& c:j.c) for (int i=0;i<c.n;++i) gastado&=i<c.sale&&c.ic[i].resuelto;
  if (j.vida[0]<=0||j.vida[1]<=0||(gastado&&j.ultimo_pulsado)) { FinalizarBotones(j); return; }
  bool fuera=true;
  for (const auto& c:j.c) { fuera&=c.sale>=c.n; for (const auto& k:c.ic) fuera&=!k.vivo; }
  if (gastado&&fuera) {
    ++j.ciclos;
    for (auto& c:j.c) { c.sale=c.actual=0;c.cuenta=c.hueco[0];for(auto& k:c.ic) k={}; }
    REXLOG_INFO("dbz3 fase1: NB014 recarga secuencias ciclo {} (los ultimos pasaron sin pulsar)",j.ciclos);
  }
}

// Un cuadro de 60 Hz de la logica de IW (QteMg_UpdatePlayerButtons / ProcessPlayerInput / ApplyHitDamage).
// EU 0x332270 QteMg_UpdatePlayerButtons
void Paso(Juego& j, uint16_t pulsado) {
  if (j.poses==&kPosesNB013) { PasoNB013(j,pulsado); return; }
  if (j.poses==&kPosesNB014) { PasoNB014(j,pulsado); return; }
  ++j.cuadros;
  if (j.aviso_t > 0) --j.aviso_t;
  for (int& t : j.destello) if (t >= 0) ++t;
  const bool nb06=j.poses==&kPosesNB006;
  uint16_t palanca=0;
  if (nb06) {
    const int x = PalancaDerechaX(), y = PalancaDerechaY();
    const uint16_t dir = PalancaMasenko(x, y);
    palanca=uint16_t(dir&~j.palanca_antes);
    j.palanca_antes=dir;
    if (j.especial_fase>=0) { PasoRonda06(j,pulsado,palanca);return; }
  }
  // eIW4: Mg_AdvanceLaneInput (EU 0x37B410) solo descuenta la cuenta si Mg_HasActiveInputInWindow (EU 0x37A7A0) es 0: un icono
  // especial (tabla de rondas) vivo y sin resolver entre los 10 ultimos de cualquiera de los dos carriles congela los dos
  // desde que sale. Medido en PCSX2 (7D4 dif 2): congela en el cuadro de salida del icono 13, no 84 pasos despues (barra).
  bool especial06=false;
  if (nb06)
    for (int q=0;q<2;++q)
      for (int i=std::max(0,j.c[q].sale-10);i<j.c[q].sale;++i)
        especial06|=j.especiales[q][i] && j.c[q].ic[i].vivo && !j.c[q].ic[i].resuelto;
  for (int l = 0; l < 2; ++l) {
    Carril& c = j.c[l];
    const bool barra=nb06&&j.marca_activa&&j.marca[1]>=0&&j.marca[0]<=kW;
    if (!barra && !especial06) --c.cuenta;
    if (c.cuenta <= 0 && c.sale < c.n) {   // sale el siguiente icono
      LogSpawn(j, l, c.sale, c.seq[c.sale]);
      c.ic[c.sale] = Icono{c.seq[c.sale], -16 - j.vel, true};
      if (nb06 && j.especiales[l][c.sale]) ReiniciarMarca(j,l,c.sale);
      ++c.sale;
      c.cuenta = c.sale < c.n ? c.hueco[c.sale] : 0;
    }
    for (int i = 0; i < c.sale; ++i) {
      Icono& k = c.ic[i];
      if (!k.vivo) continue;
      if (k.salida) {
        if (++k.anim_t >= j.salida_dur) k.vivo = false;
        continue;
      }
      if (k.cruz) ++k.anim_t;
      k.x += j.vel;
      const bool solapa=nb06 ? SolapaIcono(k.x) : std::fabs(k.x-kObjetivo)<kIcono;
      if (!k.resuelto && !k.contado && solapa && (!nb06||i==c.actual)) {   // solo la celda actual suma oportunidad
        k.contado = true;
        ++c.llegaron;
      }
      if (!k.resuelto && k.x > kFalla && (!nb06||!solapa)) {   // se paso: fallo
        k.resuelto = true;
        if (!k.contado) {
          k.contado = true;
          ++c.llegaron;
        }
        PosesGolpe(j, l, false);   // QteMg_UpdatePlayerButtons: PlayHitPoses(carril, 0) al pasarse
        if (l == 1) Golpe(j);
        j.aviso_t = 30;
        j.aviso_bien = false;
        j.aviso_carril = l;
        k.cruz = j.poses == &kPosesNB006;
        k.anim_t = 20;   // EU37ADE0: cruz Objeto7 desde cuadro20
      }
      if (k.x >= kQuita) {
        if (j.poses == &kPosesNB006) k.salida = true, k.anim_t = 0;
        else k.vivo = false;
      }
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
      j.aviso_carril = l;
      if (BotonPulsado(p) == k.boton && (!nb06||SolapaIcono(k.x))) {   // QTE_CheckPressedMatch
        ++c.aciertos;
        if (j.poses == &kPosesNB006) {
          k.salida = true;
          k.anim_t = 0;
          j.destello[l] = 0;
        } else k.vivo = false;
        if (l == 0) j.vida[1] -= float(j.m->ataque_jugador[j.dif]);
        PosesGolpe(j, l, true);
        j.aviso_bien = true;
      } else {
        PosesGolpe(j, l, false);   // boton malo: PlayHitPoses(carril, 0)
        if (l == 1) Golpe(j);
        j.aviso_bien = false;
        k.cruz = j.poses == &kPosesNB006;
        k.anim_t = 20;
      }
    }
  }
  if (nb06 && j.marca_activa) {
    for (float& x:j.marca) x+=j.vel;
    if (!j.marca_entrada&&SolapaObjetivo(j.marca[1],-12,12)) {
      j.marca_entrada=true;
      IniciarRonda06(j);
      return;
    }
    if (j.marca[0]>kW) j.marca_activa=false;
  }
  // EU 0x2EE9E0 QTE_IsSequenceDone
  const bool gastado = j.c[0].actual >= j.c[0].n && j.c[1].actual >= j.c[1].n;
  if (nb06&&gastado&&j.vida[0]>0&&j.vida[1]>0) {
    ++j.ciclos;
    for (auto& c:j.c) { c.sale=c.actual=0;c.cuenta=c.hueco[0];for(auto& k:c.ic) k={}; }
    REXLOG_INFO("dbz3 solIW5: NB006 recarga secuencias ciclo {}",j.ciclos);
    return;
  }
  if (gastado || j.vida[0] <= 0 || j.vida[1] <= 0) {   // QteMg_ClassifyResult + FinalizeResult
    FinalizarBotones(j);
  }
}

class Panel : public iwgs::Drawer {
 public:
  Panel() = default;

 protected:
  bool Visible() const override { return g_corre.load(); }
  void OnDraw(ImGuiIO& io) override {
    EnsayoLuzNB006(g_corre.load()&&g_poses_a.load()==&kPosesNB006);
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
    if (CubrirPresentacionB3(est, j.get())) return;
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
                   : j->t < CuadrosResultado(*j) ? (aire > 20 ? aire + 8 : aire - 4) : std::min(18.0f, aire);
      g_cam_dist = j->fase == 3 ? (j->t < CuadrosResultado(*j) ? d * 1.5f : d * 0.65f) : d;
    }
    g_cam_lado = j->fase == 3 && j->t >= CuadrosResultado(*j) ? -1.0f : 1.0f;
    if (const char* e = std::getenv("DBZ3_R14_CAMD")) g_cam_dist = float(std::atof(e));   // prueba
    const bool pausa = dbz3::GameDimmed() || dbz3::OverlayOpen();
    // el mando que ve el minijuego (MandoBotones guarda lo que leyo el juego antes de quitarle los botones a B3)
    const uint16_t ahora = g_raw_ok.load() ? g_raw12.load() : REX_LOAD_U16(kPads + kPadBotones);
    // Conservar el flanco hasta un tick logico, tambien con presentaciones
    // mas rapidas que60Hz o con el reloj de medida retenido.
    if (j->anima_intro && j->fase == 0 && !pausa && dbz3::iwtrad::Activo() && !dbz3::iwtrad::Arrancando()) {
      std::lock_guard lk(anim7db::mando_mu);
      anim7db::mando.Nivel((ahora & 8) != 0);
    }
    bool confirma_resultado = ((j->pendiente | (ahora & ~j->antes)) & 0x4000) != 0;
    j->pendiente = uint16_t(j->pendiente | (ahora & ~j->antes));   // hasta el siguiente cuadro de la logica
    if (ahora != j->antes && sondas_ < 40) {   // registro: los 40 primeros cambios del mando (sonda de teclas)
      ++sondas_;
      REXLOG_INFO("dbz3 r14: mando +12 {:04X}", ahora);
    }
    j->antes = ahora;
    // RITMO (R16 dsIW7DB_RITMO): reparto del tiempo de un cuadro pintado. DBZ3_IW_RITMO=1 -> una linea cada 60 cuadros
    // con los pasos de logica ejecutados, su coste y el del cuadro entero (sin la variable no se registra nada).
    static const bool kRitmo = std::getenv("DBZ3_IW_RITMO") != nullptr;
    static auto t_ritmo_ini = std::chrono::steady_clock::now();
    static int ritmo_cuadros = 0, ritmo_pasos = 0, ritmo_atrasos = 0;
    static double ritmo_us_paso = 0;
    // RITMO (R16): presupuesto de logica por cuadro presentado. Con el original traducido cada paso cuesta ~38 ms (medido:
    // 971k instrucciones por vblank a ~25 MIPS/s) y el acumulador subia hasta 0,1 s: se hacian 6 pasos por cuadro y el
    // dibujado caia a 4 fps. El ritmo de la logica no cambia (es 1/tiempo de paso), pero sin cadena el cuadro se presenta
    // en cuanto acaba un paso. Con pasos baratos (el espejo NB013: 0,2 ms) el tope no llega a alcanzarse nunca.
    static const double kPresupuestoUs = [] {
      const char* e = std::getenv("DBZ3_IW_PRESUPUESTO_MS");
      return (e && *e ? std::atof(e) : 8.0) * 1000.0;
    }();
    const auto t_cuadro = std::chrono::steady_clock::now();
    const auto t_log0 = t_cuadro;
    int pasos_cuadro = 0;
    acum_ += pausa ? 0.0f : dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
    // eIW3: DBZ3_IW_QTE_PAL=1 -> la logica corre a 50 pasos/s con avance x1,2 (fRam0049c7dc del original PAL: los huecos entre
    // iconos son pasos, no segundos). Medido: 7DC dificultad 2, hueco 295 pasos = 295 campos PAL. Apagado: 60 pasos/s (NTSC).
    while (acum_ >= PasoLogica()) {   // logica a 60 cuadros por segundo, como en la PS2
      if (pasos_cuadro && std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_log0).count() >
                              kPresupuestoUs) {
        acum_ = 0.0f;   // no puede recuperar el atraso: se descarta y se presenta ya
        ++ritmo_atrasos;
        break;
      }
      ++pasos_cuadro;
      if(j->fase==3&&std::getenv("DBZ3_SOLIW9_PANEL_PASOS")) {
        int objetivo=0;std::ifstream f("iw9_panel_cuadro.req");f>>objetivo;
        const int edad=j->t-InicioPanelResultado(*j);
        if(edad>=objetivo && !confirma_resultado) {
          static int anterior=-1;
          if(anterior!=edad) {anterior=edad;REXLOG_INFO("dbz3 iw panel9: detenido edad {}",edad);}
          acum_=0;break;
        }
      }
      acum_ -= PasoLogica();
      ++j->t;
      // EU2EDC70/2EDBA0: dos fases de5, conversion angular original.
      // El primer campo del cuerpo aun conserva el FOV previo. PAL tiene
      // campos1.2; no se avanza una vez por presentacion del host.
      static float combo_acum=0;
      static int combo_anterior=-1;
      if(g_combo_zoom.load()==0 && combo_anterior!=0) combo_acum=0;
      if(g_combo_zoom.load()>=0) {
        combo_acum+=1;
        if(combo_acum>=g_combo_delta.load()) {
          combo_acum-=g_combo_delta.load();
          if(++g_combo_zoom>=11) g_combo_zoom=-1;
        }
      }
      combo_anterior=g_combo_zoom.load();
      if (std::getenv("DBZ3_TEST_IWMINI") && ((j->fase==1&&j->t==60)||
          (j->fase==3&&(j->t==115||j->t==CuadrosResultado(*j)))))
        REXLOG_INFO("dbz3 solIW6: QTE clave {:X} fase {} cuadro {} HUD {} READY {} panel {}",j->clave,j->fase,j->t,
                    j->fase==1,j->fase==1&&j->t>=60,j->fase==3&&j->t>=CuadrosResultado(*j));
      if (j->clave == 0x7D4) {   // NB006: voz del jugador (PlayHit_KiBlastsAudios3 de Mg_UpdateRoundState/Mg_UpdateReadyPhase, EU 0x379E30/0x3781B0)
        static int fase_vista = -1;
        const bool entra_ready = j->fase == 1 && fase_vista != 1;
        fase_vista = j->fase;
        const int linea = entra_ready ? 0x2E : (j->fase == 3 && j->t == (j->rango > 0 ? 50 : 25)) ? (j->rango > 0 ? 0x33 : 0x1E) : -1;
        if (linea >= 0 && !(std::getenv("DBZ3_IW_VOZ_POSE") && std::getenv("DBZ3_IW_VOZ_POSE")[0] == '0')) {
          static const std::vector<uint8_t> elf = P::LeerElf(P::BuscarIW());
          const int perfil = anim::apev::PerfilAdx(anim::apev::CmnActor(uint32_t(j->m->rival)));   // iRam0049ced0 = el segundo luchador (Piccolo); el perfil de Gohan (28 lineas) no llega a 0x2E
          const int entrada = anim::apev::EntradaAdxPerfil(elf, perfil, linea);
          const int idioma = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9));   // 0 = voces japonesas
          const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
          REXLOG_INFO("dbz3 r14: voz NB006 linea {:X} perfil {} -> ADX {} (fase {} cuadro {})", linea, perfil, entrada, j->fase, j->t);
          if (entrada >= 0) P::OirAdxPs2(P::BuscarIW(), idioma == 0 ? "ADX_JPN.AFS" : "ADX_USA.AFS", entrada, vol);
        }
      }
      // RESCATE IW-F15a (dbz3_iw_botones_voz_resultado): Mg_UpdateResultPhase EU 0x3520D0 (7DB) y QteMg_UpdateResultPhase
      // EU 0x32FB40 (7DC): con el reloj +0xC (suma el delta DOS veces por cuadro) >= 228, el cuadro 115, el mismo del
      // rotulo: rango +0x1C (Mg_EvaluateTimingRank EU 0x351FB0) 0 -> UiScreen_SetObjectField158(0,8) + s_MSN_CV_ReadyStart(2)
      // = Menu_CharVoice_PlayA_ (EU 0x381DA0); rango > 0 -> (0,7) + ReadyStart(1) = Menu_CharVoice_PlayB_ (EU 0x381E80).
      // Cada una elige al azar (bit 0) una de dos voces por minijuego (LoadWaitMinigamesFlag +4: 0xC = 7DB, 0xD = 7DC),
      // tabla EU 0x49C5B8..0x49C5D4 (lwc1 desde gp, leida en el SLES_553.47), y Audio_PlayCharacterVoice(i) = ADX 1248 + i
      // (tabla EU 0x480450, ver iw_mini_disparo VozAnuncio). 7DB: CLEAR 5/7, FAILED 9/13; 7DC: CLEAR 20/21, FAILED 22/23.
      if ((j->clave == 0x7DB || j->clave == 0x7DC) && j->fase == 3 && j->t == 115 && VozResultadoIW()) {
        static const int kVozRes[2][2][2] = {{{9, 13}, {5, 7}}, {{22, 23}, {20, 21}}};   // [7DC][rango > 0][azar]
        const int azar = int((std::chrono::steady_clock::now().time_since_epoch().count() >> 10) & 1);
        const int id = kVozRes[j->clave == 0x7DC][j->rango > 0][azar];
        const int idioma = int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9));   // 0 = voces japonesas
        const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
        REXLOG_INFO("dbz3 r14: voz resultado {:X} rango {} -> Audio_PlayCharacterVoice({}) = ADX {} (cuadro {})", j->clave,
                    j->rango, id, 1248 + id, j->t);
        P::OirAdxPs2(P::BuscarIW(), idioma == 0 ? "ADX_JPN.AFS" : "ADX_USA.AFS", 1248 + id, vol);
      }
      SerieJuego(j->fase == 2);
      CuadroSe();
      if (g_cb_clip.load() >= 0 && !g_cb_quieto.load()) ++g_cb_frame;
      // s8 (medido en PCSX2): la entrada es [#AMC de la escena (amc_entrada, sus clips seguidos; poses 0/0)] ->
      // [cam_presenta (dur_presenta cuadros; el jugador en su pose 1)] -> READY. R7: NB006 abre con el clip 0x321 entero
      // (Mg_UpdateIntroSequence EU L468128-468176), que es su cam_presenta.
      const Poses& q0 = *j->poses;
      const bool pre = q0.cam_presenta >= 0 && g_clips.count(q0.cam_presenta);
      const int t_pre = pre ? q0.dur_presenta : 30;
      if (j->fase == 0 && !j->presenta_7db) {
        if (j->t == 1 && j->ent > 0) {
          CamB(kCamEntrada);
          Poner(q0,0,q0.ini[0],false);
          Poner(q0,1,q0.ini[1],false);
        }
        for (int i = 1, acum = 0; i < q0.n_entrada && j->ent > 0; ++i) {   // los siguientes, seguidos
          const auto it = g_clips.find(kCamEntrada + i - 1);
          acum += it == g_clips.end() ? 0 : it->second.nf;
          if (j->t == acum + 1 && g_clips.count(kCamEntrada + i)) CamB(kCamEntrada + i);
        }
        if (j->t == j->ent + 1) {   // presentacion: su clip y la pose 1 del jugador (la animacion de la 0, una vez)
          // IWINI-11 (12/11): con la logica del ELF de NB006 la 0x321 la pide el propio ELF al acabar el paisaje
          // (Mg_UpdateIntroSequence); pedirla aqui tambien la hacia salir dos veces (5 s de mas; G02_0x7D4_INI: una).
          const bool elf321 = j->orig && j->clave == 0x7D4;
          if (pre) { if (!elf321) CamB(q0.cam_presenta); }
          else if (j->ent == 0) CamB(q0.cam_ini, true);   // sin clip: el plano de salida, quieto
          if (const uint32_t x = Luchador(0), a = AliasPose(q0, 0, q0.ini[0]); x && a)
            dbz3::anim::Reproducir(x, a, true);   // sujeta en su ultimo cuadro hasta el READY
          // EU 0x37A2D0, paso3: ambos actores se reinician con la 0x321.
          // Piccolo no debe continuar el reloj de los 630 cuadros de paisaje.
          Poner(q0, 1, q0.ini[1], false);
        }
      }
      if ((j->clave == 0x7DC || j->clave == 0x7DB || j->clave == 0x7D4) && j->orig && dbz3::iwtrad::Activo()) {   // TRAD/6
        if (!j->trad_entrada) { j->trad_entrada = true; dbz3::iwtrad::PonerClave(j->clave); dbz3::iwtrad::NuevaEntrada(); }
        dbz3::iwtrad::CuadroBucle(uint16_t(j->antes | (j->dir_piloto_t > 0 ? j->dir_piloto : 0)));
        if (j->dir_piloto_t > 0) --j->dir_piloto_t;
      }
      if (j->orig && j->fase < 3) {   // T2B: 0x7DC / 0x7DB con la logica original (PasoOriginalDC / DB); el final es del port
        if (j->clave == 0x7DB || j->clave == 0x7D4 ? PasoOriginalDB(*j) : PasoOriginalDC(*j)) {
          if (j->auto_) {
            dbz3::net::InyectarPad(0, j->inyecta_t > 0 ? j->inyecta : 0);
            if (j->inyecta_t > 0 && --j->inyecta_t == 0) j->inyecta = 0;
          }
          // 12/11: el reloj del aviso de la silueta (30 cuadros) solo bajaba en la logica del port: en la original se quedaba en
          // 30 y la animacion no pasaba de su cuadro 0 (invisible): «la parte de abajo no se ilumina».
          if (j->aviso_t > 0) --j->aviso_t;
          CuadroEfectos(*j);   // IW-FX-02: este `continue` dejaba sin avanzar los efectos en la ruta original
          continue;
        }
        j->orig = false;
        if (j->presenta_7db) {
          g_cam_intro7.store(MuestraIntro7{});
          g_start_apertura_7db = false;
          if (j->anima_intro) { anim7db::Limpiar(); j->anima_intro = false; }
          j->presenta_7db = false;
          if (j->fase == 0) { j->t = 0; CamB(kCamEntrada, true); }
        }
        REXLOG_WARN("dbz3 r14: {:X}: la logica original no pudo seguir; la partida continua con el port", j->clave);
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
          // B3 no debe entrar aun en su victoria/derrota: al cambiar 5->6
          // reinicia el reloj del ganador y pisa el ultimo cuadro de IW.
          CamB(j->rango > 0 ? j->poses->cam_gana : j->poses->cam_pierde,
               false, j->poses == &kPosesNB006);   // NB006 conserva 0x97 en bucle (Mg_IntroScreenSetup)
        }
        if (j->t == CuadrosResultado(*j)) {   // acaba el primer clip -> QteMg_FinalizeResult / Mg_StartResultAnim
          // Duraciones de sus #AMM propios: NB006 220/240, NB013 240/240,
          // NB014 240/300. anim_iw acota tambien DESPUES del avance de B3,
          // en ambos cuerpos: el ultimo cuadro es dur-1, como IW.
          const uint32_t dur = uint32_t(CuadrosResultado(*j, true));
          const uint32_t codigo = j->rango > 0 ? j->poses->fin_bien : j->poses->fin_mal;
          Poner(*j->poses, 0, codigo, false, 0, dur - 1);
          // EU 0x377A00 Mg_StartResultAnim: NB006 cambia AMBOS actores a los
          // estados 0x18/0x14 (0x2A9) o 0x19/0x15 (0x2AA). NB013/014 solo el jugador.
          if (j->poses == &kPosesNB006)
            Poner(*j->poses, 1, j->rango > 0 ? j->poses->fin_bien : j->poses->fin_mal, false, 0, dur - 1);
          CamB(j->rango > 0 ? j->poses->cam_fin_bien : j->poses->cam_fin_mal);
          REXLOG_INFO("dbz3 r14: pose final de IW (clave 0x{:X}, jugador 0x{:X}, ambos {}, sujeta, inicio {} duracion {})",
                      j->clave, j->rango > 0 ? j->poses->fin_bien : j->poses->fin_mal,
                      j->poses == &kPosesNB006, j->t, dur);
        }
        const int panel = j->t-InicioPanelResultado(*j);
        if(panel<0) j->pendiente=uint16_t(j->pendiente&~0x4000);
        const bool propio = j->poses==&kPosesNB006 || j->poses==&kPosesNB014;
        if (propio && panel >= 0) {
          const bool piloto = j->auto_ && j->recompensa.cuadro >= 420;
          const float antes = j->recompensa.cuadro;
          j->recompensa.Avanzar(panel ? 1.0f : 0.0f, confirma_resultado || piloto);
          j->pendiente=uint16_t(j->pendiente&~0x4000);
          confirma_resultado = false;
          if (antes < 420 && j->recompensa.cuadro >= 420 && panel < 420)
            j->recompensa_anim_salto = 420-float(panel);
          if (!panel || panel==120 || panel==320 || panel==420)
            REXLOG_INFO("dbz3 iw panel: clave {:#x} edad {} reloj {:.0f} filas tipo3 {},{},{} retenido {}",
                        j->clave,panel,j->recompensa.cuadro,j->c[0].aciertos,j->c[1].aciertos,j->nota,
                        !j->recompensa.cerrado);
        }
        const bool salir_resultado = propio ? j->recompensa.cerrado :
            j->t >= CuadrosResultado(*j) + CuadrosResultado(*j, true) + 8;
        if (!fin_ && salir_resultado) {
          fin_ = true;
          REX_STORE_U8(mgr + kFin, uint8_t(j->rango > 0 ? 1 : 2));
          REXLOG_INFO("dbz3 r14: secuencia de resultado de IW completa; sale al resultado del mapa");
        }
      }
      CuadroEfectos(*j);
    }
    if (kRitmo) {
      const auto t_fin_log = std::chrono::steady_clock::now();
      const double us_total_cuadro = std::chrono::duration<double, std::micro>(t_fin_log - t_cuadro).count();
      ritmo_us_paso += us_total_cuadro;
      ritmo_pasos += pasos_cuadro;
      if (++ritmo_cuadros >= 60) {
        const double seg = std::chrono::duration<double>(t_fin_log - t_ritmo_ini).count();
        REXLOG_INFO("dbz3 ritmo: cuadros {} en {:.2f} s ({:.1f} fps pintados) pasos/cuadro {:.2f} "
                    "us/paso {:.1f} ms/cuadro {:.2f} atrasos {} fase {} t {}",
                    ritmo_cuadros, seg, double(ritmo_cuadros) / seg, double(ritmo_pasos) / ritmo_cuadros,
                    ritmo_pasos ? ritmo_us_paso / ritmo_pasos : 0.0, ritmo_us_paso / ritmo_cuadros / 1000.0,
                    ritmo_atrasos, j->fase, j->t);
        ritmo_cuadros = ritmo_pasos = ritmo_atrasos = 0;
        ritmo_us_paso = 0;
        t_ritmo_ini = t_fin_log;
      }
    }
    Dibujar(io, *j);
  }

 private:
  void Preparar(const Juego& j) {
    hecho_ = &j;
    ComenzarAtlas();
    for(int rol=0;rol<2;++rol) {g_chr_fade[rol]=0;g_chr_activo[rol]=false;g_chr_color[rol]=0;g_chr_paleta[rol]=iwchrfade::kNeutro;}
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
    botones06_ = ps2::Crear(j.botones06);
    objetivo06_ = ps2::Crear(j.objetivo06);
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
    antena_tx_.clear();
    antena_suma_.clear();
    antena_z_.clear();
    for (const auto& im : j.antena_atlas) {
      antena_tx_.push_back(ps2::Crear(im));
      if (EfectosIW()) antena_z_.push_back(ps2::Crear(im, true));   // IW-FX-LAVADO2: gemela con prueba de Z
      RegistrarTextura(antena_tx_.back().id(),im);
      P::Imagen su = im;
      for (size_t i = 0; i + 3 < su.rgba.size(); i += 4)
        su.rgba[i + 3] = uint8_t(std::max({su.rgba[i], su.rgba[i + 1], su.rgba[i + 2]}) * su.rgba[i + 3] / 255);
      antena_suma_.push_back(ps2::Crear(su));
    }
    for (int r = 0; r < 2; ++r) {   // IW-F15a: aura original, con prueba de Z (como la de los campos, iw_minijuegos)
      aura_[r] = j.aura[r];
      if (aura_[r]) {
        std::copy_n(aura_[r].pr,20,aura_param_[r]);
        aura_[r].pr = aura_param_[r]; // copia del panel; nunca modificar la tabla de Juego
      }
      aura_tx_[r].clear();
      for (const auto& im : j.aura_atlas[r]) aura_tx_[r].push_back(ps2::Crear(im, true));
    }
    botones_tx_.clear();
    resultado_tx_.clear();
    for (const auto& im:j.resultado_atlas) resultado_tx_.push_back(ps2::Crear(im));
    for (const auto& im : j.botones_atlas) botones_tx_.push_back(ps2::Crear(im));
  }
  static void Pieza(ImDrawList* dl, const ps2::Tex& t, ImVec2 c, float k) {
    if (t) dl->AddImage(t.id(), ImVec2(c.x - t.w * k / 2, c.y - t.h * k / 2), ImVec2(c.x + t.w * k / 2, c.y + t.h * k / 2));
  }
  // s9: panel de resultado de IW (iw_nb013_d, 800x600 -> 640x448): marco 16..624 x 10..438, rotulos en x 68 (centros y
  // 73 / 138 / 202) con su % a la derecha (x 604, 22 mas abajo), EARNED! en y 347 y TOTAL en 403 con la cifra naranja
  // hasta x 470 y ZENI en x 532. El titulo del minijuego, arriba (s1 F04.3)
  template <class At>
  void DibujarResultado(ImDrawList* fg, At at, const Juego& j) {
    Resultado r;
    r.clave = j.clave;
    r.offence = j.c[0].llegaron > 0 ? j.c[0].aciertos * 100 / j.c[0].llegaron : 0;
    r.defence = j.c[1].llegaron > 0 ? j.c[1].aciertos * 100 / j.c[1].llegaron : 0;
    r.score = j.nota; r.zeni = j.zeni; r.saldo = j.saldo; r.rango = j.rango - 1;
    const ImVec2 p0 = at(0, 0), p1 = at(640, 448);
    if (recompensa::Dibujar(fg, p0.x, p0.y, p1.x-p0.x, p1.y-p0.y, r,
                          j.recompensa.cuadro, &titulo_,
                          float(std::max(0, j.t-InicioPanelResultado(j)))+j.recompensa_anim_salto)) return;
    const bool marco_nativo=MarcoResultado(fg,at,j);
    // DS72: el relleno del panel es el arte de interfaz de IW (DATA_USA 334/1 t00), no un color propio.
    // t00 se pinta con alfa 175 como la caja anterior: el original deja ver al luchador por detras (iw_nb013_d).
    if (!marco_nativo&&panel_fill_)
      fg->AddImageRounded(panel_fill_.id(), at(16, 10), at(624, 438), ImVec2(0, 0), ImVec2(1, 1),
                          IM_COL32(255, 255, 255, 175), 26.0f);
    else if (!marco_nativo)
      fg->AddRectFilled(at(16, 10), at(624, 438), IM_COL32(8, 40, 72, 175), 14);
    // El marco fino azul del original no es un sprite unico: el unico marco grande de 334/1 (t03) es el bisel grueso del
    // panel del mapa y aqui quedaria 6 veces mas grueso. Se conserva el trazo medido [H].
    if (!marco_nativo) fg->AddRect(at(16, 10), at(624, 438), IM_COL32(110, 200, 255, 230), 14, 0, 3);
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
    // NB006: PINE, plano terminal323/239. Esta pantalla solo muestra las
    // tasas; EARNED/TOTAL pertenecen al paso de recompensa, tras confirmar.
    // Ese paso lo entrega el conector al resultado comun del mapa.
    if (j.poses == &kPosesNB006) return;
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
      const int cuadro_panel=j.t-InicioPanelResultado(j);
      if (j.fase != 3 || cuadro_panel < desde) return;
      const int vis = std::min(1 + (cuadro_panel - desde) / 20, digitos(valor));
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
  template<class At>
  bool MarcoResultado(ImDrawList* dl,At at,const Juego& j) {
    const int nodo=j.resultado_ama.Objeto(0);
    if (nodo<0||resultado_tx_.empty()) return false;
    auto arbol=anim::ama::CrearArbol(j.resultado_ama,nodo,float(std::max(0,j.t-InicioPanelResultado(j))));
    anim::ama::MarcarArbol(arbol,float(std::max(0,j.t-InicioPanelResultado(j))));
    std::vector<anim::ama::Entrada> entradas;
    anim::ama::ActualizarNodo(arbol,arbol.raiz,{},entradas);
    auto punto=[&](float x,float y) { return at(x*640/600,y*448/450); };
    for (const auto& e:entradas) {
      anim::ama::Quad q;anim::ama::DibujarEntrada(e,q);
      auto c=[&](int i) { return std::clamp(int(e.tinte[i]*255+e.rgba[i]+.5f),0,255); };
      const ImU32 col=IM_COL32(c(0),c(1),c(2),c(3));
      // La entrada sin textura es el sitio del titulo, que el consumidor
      // sustituye por DATA_USA335+clave. No es una banda blanca de fondo.
      if (!e.SinTextura()&&e.textura<resultado_tx_.size()&&resultado_tx_[e.textura]) {
        const auto& tx=resultado_tx_[e.textura];
        dl->AddImageQuad(tx.id(),punto(q.xy[0][0],q.xy[0][1]),punto(q.xy[1][0],q.xy[1][1]),
            punto(q.xy[2][0],q.xy[2][1]),punto(q.xy[3][0],q.xy[3][1]),
            ImVec2(q.uv[0][0]/tx.w,q.uv[0][1]/tx.h),ImVec2(q.uv[1][0]/tx.w,q.uv[1][1]/tx.h),
            ImVec2(q.uv[2][0]/tx.w,q.uv[2][1]/tx.h),ImVec2(q.uv[3][0]/tx.w,q.uv[3][1]/tx.h),col);
      }
    }
    return true;
  }
  template<class At>
  void BotonAma(ImDrawList* dl, At at, const Juego& j, int id, float t, float x = 0, float y = 0, int boton = -1) {
    if (!j.botones_ama.Valida()) return;
    const int nodo = j.botones_ama.Objeto(uint32_t(id));
    if (nodo < 0) return;
    auto arbol = anim::ama::CrearArbol(j.botones_ama, nodo, t);
    if (t >= float(anim::ama::MaxCuadros(arbol)) && id != 0x0B) return;
    anim::ama::Contexto ctx;
    ctx.pos[0] = x;
    ctx.pos[1] = y;
    std::vector<anim::ama::Entrada> entradas;
    anim::ama::ActualizarNodo(arbol, arbol.raiz, ctx, entradas);
    for (auto e : entradas) {
      if (e.SinTextura() || e.textura >= botones_tx_.size() || !botones_tx_[e.textura]) continue;
      if (id==0x44) e.rect[2]=e.rect[0]+j.marca_ancho; // consumidor EU372950
      // EU373630 cambia UV del glifo, manteniendo escala/color/giro de0B/0C.
      if (boton >= 0 && e.textura == 1) {
        const int celda = boton >= 12 ? (boton == 15 ? 0 : boton == 12 ? 1 : boton == 13 ? 2 : 3)
                                     : (boton == 10 ? 0 : boton == 11 ? 1 : boton == 8 ? 2 : 3);
        e.uv[0] = float(celda * 32);
        e.uv[1] = boton >= 12 ? 64.0f : 100.0f;
        e.uv[2] = e.uv[0] + 32;
        e.uv[3] = e.uv[1] + 32;
      }
      anim::ama::Quad q;
      anim::ama::DibujarEntrada(e, q);
      const auto& tx = botones_tx_[e.textura];
      auto color = [&](int c) { return std::clamp(int(e.tinte[c] * 255 + e.rgba[c] + 0.5f), 0, 255); };
      const ImU32 col = IM_COL32(color(0), color(1), color(2), color(3));
      dl->AddImageQuad(tx.id(), at(q.xy[0][0],q.xy[0][1]), at(q.xy[1][0],q.xy[1][1]),
                       at(q.xy[2][0],q.xy[2][1]), at(q.xy[3][0],q.xy[3][1]),
                       ImVec2(q.uv[0][0]/tx.w,q.uv[0][1]/tx.h), ImVec2(q.uv[1][0]/tx.w,q.uv[1][1]/tx.h),
                       ImVec2(q.uv[2][0]/tx.w,q.uv[2][1]/tx.h), ImVec2(q.uv[3][0]/tx.w,q.uv[3][1]/tx.h), col);
    }
  }
  void DibujarAntenas(ImGuiIO& io, const Juego& j) {
    if(!j.combo_fx.empty()) {
      ps2::Camara cam=ps2::CamaraCombate(Base(),io.DisplaySize);
      float vista[16],fov=0;
      if(CamaraDelRenderIw(vista,&fov)) {std::copy(vista,vista+16,cam.m);cam.fov=fov;cam.ok=true;}
      for(const auto& in:j.combo_fx) {
        auto mundo=in;
        for(auto& lista:mundo.ps) for(auto& p:lista) {
          p.p.x+=g_origen[0];p.p.y+=g_origen[1];p.p.z+=g_origen[2];
        }
        iwame::ame::Pintar(mundo,cam,antena_tx_,antena_suma_,ImGui::GetBackgroundDrawList(),160*io.DisplaySize.y/720,true);
      }
    }
    // IW-FX-02: los efectos de las poses van en el espacio de los huesos, como las antenas: su misma camara.
    // Las raices nacen en local (Cuadro): se giran con la matriz del lanzador (Moving: de +Z a dir) y se llevan a su
    // origen. ponytail: el billboard (Effect_SpawnAmoBillboardMatrix) usa el mismo giro con dir = vista de camara.
    PintarFxPose(io, j.fx_pose, antena_tx_, antena_suma_, &j.fx_modelos, &antena_z_);
    if (j.antena_fx.empty()) return;
    ps2::M34 ancla;
    if (!AnclaAntenas(Base(), ancla)) return;
    ps2::Camara cam = ps2::CamaraCombate(Base(), io.DisplaySize);
    // DS177: el renderer toma FOV de CamaraMinijuego, no mgr+1C4.
    // Usar el45grados nativo desplazaba NH decenas de pixeles hacia el centro.
    if (const auto it=g_clips.find(g_cb_clip.load()); it!=g_clips.end() && !it->second.fov.empty()) {
      float e_[3], t_[3];
      MuestraClip(it->second, CuadroClip(it->second), e_, t_, &cam.fov, nullptr);
    }
    if (!cam.ok) return;
    auto* dl = ImGui::GetBackgroundDrawList();
    antena_escena_.Empezar();
    for (const auto& in : j.antena_fx) {
      auto mundo = in;
      for (auto& lista : mundo.ps)
        for (auto& p : lista) p.p = ancla.por(p.p);
      iwame::ame::Pintar(mundo, cam, antena_tx_, antena_suma_, dl, io.DisplaySize.y);
      for (const auto& lista : in.ps)
        for (const auto& p : lista) {
          if (p.n->tipo != 4 || p.edad <= 0) continue;
          const auto& n = *p.n;
          const float t = std::clamp(p.edad / p.vida, 0.0f, 1.0f);
          const ps2::V3 sc{n.f[8] + (n.f[12] - n.f[8]) * t,
                           n.f[9] + (n.f[13] - n.f[9]) * t,
                           n.f[10] + (n.f[14] - n.f[10]) * t};
          const float x = n.q[0], y = n.q[1], z = n.q[2], w = n.q[3];
          const ps2::M34 rot{{1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), 0,
                             2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), 0,
                             2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), 0}};
          ps2::Malla m = j.antena_modelo.malla;
          float c[4];
          for (int i = 0; i < 4; ++i) c[i] = std::clamp(n.f[16+i] + (n.f[20+i]-n.f[16+i])*t, 0.0f, 1.0f);
          for (auto& tr : m.tris) {
            tr.trans = true;
            tr.con_color = true;
            for (int i = 0; i < 3; ++i) {
              const auto v = tr.p[i];
              const auto r = rot.gira({v.x * sc.x, v.y * sc.y, v.z * sc.z});
              tr.p[i] = ancla.por(iwame::Mas(p.p, r));
              tr.uv[i].x += n.f[25] * p.edad / 60;
              tr.uv[i].y += n.f[26] * p.edad / 60;
              const ImU32 vc = tr.col[i];
              tr.col[i] = IM_COL32(int(c[0]*float(vc & 255)), int(c[1]*float((vc >> 8) & 255)),
                                  int(c[2]*float((vc >> 16) & 255)), int(c[3]*float(vc >> 24)));
            }
          }
          antena_escena_.Poner(cam, m, antena_suma_, {}, 0, 1);
        }
    }
    antena_escena_.Pintar(dl);
  }
  // IW-F15a: Aura_UpdateAll EU 0x1D3BC0 / Aura_DrawAll EU 0x1D3930 en los dos luchadores, con la camara del render (la
  // de los combos) y la Z del juego. Puntos de cuerpo X+1616 + 16 i en el mundo (como PuntoCuerpo de iw_minijuegos).
  static int KiAuraIntro(const Juego& j) {
    if (!j.anima_intro) return -1;
    switch (j.reloj_intro.tramo) {
      case anim7db::Conversacion: case anim7db::Corte: return 0;
      case anim7db::Paisaje: case anim7db::Relanza1: case anim7db::Relanza2: return 100;
      default: return -1;
    }
  }
  void DibujarAura(ImGuiIO& io, const Juego& j, bool pausa) {
    if (!aura_[0] && !aura_[1]) return;
    uint8_t* base = Base();
    ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
    float vista[16], fov = 0;
    if (CamaraDelRenderIw(vista, &fov)) {
      std::copy(vista, vista + 16, cam.m);
      if (fov > 0.1f && fov < 3.0f) cam.fov = fov;
    } else if (const auto it = g_clips.find(g_cb_clip.load()); it != g_clips.end() && !it->second.fov.empty()) {
      float e_[3], t_[3];
      MuestraClip(it->second, CuadroClip(it->second), e_, t_, &cam.fov, nullptr);
    }
    if (!cam.ok) return;
    const ps2::V3 ojo{-(cam.m[0] * cam.m[12] + cam.m[1] * cam.m[13] + cam.m[2] * cam.m[14]),
                      -(cam.m[4] * cam.m[12] + cam.m[5] * cam.m[13] + cam.m[6] * cam.m[14]),
                      -(cam.m[8] * cam.m[12] + cam.m[9] * cam.m[13] + cam.m[10] * cam.m[14])};
    static constexpr int kIdx[8] = {0, 1, 4, 5, 6, 7, 3, 2};   // raiz y tramos de EU 0x449D50
    const int ki = KiAuraIntro(j);
    aura_escena_.Empezar();
    for (int r = 0; r < 2; ++r) {
      if (!aura_[r]) continue;
      // AuraPaso suma los pares [0,1] y [10,11]; las pendientes del respaldo son cero.
      for (int e = 0; e < 2; ++e)
        aura_param_[r][10*e] = ki < 0 ? j.aura_pr[r][10*e] : ki == 0 ? 0.f : j.aura_tasa100[r][e];
      // Aura_BindBonesAltModel EU 0x1D2690: modelo +0xCE0 + 16 * nodo. Aqui el mismo nodo por la tabla de anclajes del
      // luchador (MatrizNodo, X+2956), con la matriz de DIBUJO (identidad + origen, Colocar): X+1616 lo calcula la logica
      // de B3 con el giro transitorio de X y salia desplazado (medido: el aura al lado del cuerpo).
      ps2::V3 q[8];
      bool ok = true;
      for (int i = 0; i < 8 && ok; ++i) {
        ps2::M34 h;
        ok = MatrizNodo(base, r, kIdx[i], h);
        if (ok) q[i] = {h.m[3] + g_origen[0], h.m[7] + g_origen[1], h.m[11] + g_origen[2]};
      }
      if (!ok) continue;
      float alfa = 1;
      if (pausa) {   // en pausa no nacen ni envejecen rayos: solo el alfa de este cuadro
        const float d = iwame::Largo(iwame::Mas(ojo, iwame::Por(q[0], -1)));
        alfa = d < 30 ? (d / 30) * (d / 30) : 1.0f;
      } else
        iwame::ame::AuraPaso(aura_[r], q, ojo, alfa);
      if (ki == 0) continue; // alpha 0 incluso para rayos anteriores; han envejecido si no hay pausa
      // AuraMalla da los rayos en blanco; el color es el del nodo de cada emisor (tipo 2, EU 0x272FA0): base +0x30 -> +0x40
      // y punta +0x50 -> +0x60 con la edad (bandera 0x10); el alfa sigue siendo el de la tabla 0x448780.
      ps2::Malla m;
      for (int e = 0; e < 2; ++e) {
        iwame::ame::AuraCuerpo solo = aura_[r];
        solo.rayos[1 - e].clear();
        ps2::Malla me = iwame::ame::AuraMalla(solo, q[0], ojo, alfa);
        const float* F = aura_[r].n[e].f;
        const bool cambia = (aura_[r].n[e].flags & 16) != 0;
        size_t k = 0;
        for (const auto& ry : solo.rayos[e]) {   // mismos rayos y en el mismo orden que AuraMalla (2 triangulos cada uno)
          const float vida = aura_[r].pr[e ? 19 : 9];
          if (ry.edad <= 0 || iwame::Largo(ry.v) < 1e-5f) continue;
          if (k + 2 > me.tris.size()) break;
          const float t = cambia ? std::clamp(ry.edad / vida, 0.0f, 1.0f) : 0.0f;
          auto rgb = [&](int i0) {
            return IM_COL32(int(std::clamp(F[i0] + (F[i0 + 4] - F[i0]) * t, 0.0f, 1.0f) * 255),
                            int(std::clamp(F[i0 + 1] + (F[i0 + 5] - F[i0 + 1]) * t, 0.0f, 1.0f) * 255),
                            int(std::clamp(F[i0 + 2] + (F[i0 + 6] - F[i0 + 2]) * t, 0.0f, 1.0f) * 255), 0);
          };
          const ImU32 base = rgb(12), punta = rgb(20);
          for (int tr = 0; tr < 2; ++tr, ++k)   // triangulos {punta, punta, base} y {base, punta, base}
            for (int i = 0; i < 3; ++i) {
              const bool es_punta = tr == 0 ? i < 2 : i == 1;
              auto& c = me.tris[k].col[i];
              c = (c & IM_COL32_A_MASK) | (es_punta ? punta : base);
            }
        }
        if (k != me.tris.size()) continue;   // no cuadra con AuraMalla (degenerados): mejor sin este emisor que en blanco
        m.tris.insert(m.tris.end(), me.tris.begin(), me.tris.end());
      }
      if (std::getenv("DBZ3_TEST_IWMINI") && ImGui::GetFrameCount() % 120 == 0)
        REXLOG_INFO("dbz3 r14: aura original rol {} rayos {}+{} alfa {:.3f} raiz ({:.1f},{:.1f},{:.1f}) tris {}", r,
                    aura_[r].rayos[0].size(), aura_[r].rayos[1].size(), alfa, q[0].x, q[0].y, q[0].z, m.tris.size());
      if (m) aura_escena_.Tapable(false), aura_escena_.Poner(cam, m, aura_tx_[r], {}, 0.0f, 1.0f), aura_escena_.Tapable(true);
    }
    aura_escena_.Pintar(ImGui::GetBackgroundDrawList());
  }
  void Dibujar(ImGuiIO& io, const Juego& j) {
    g_fase_a = j.fase;
    DibujarAura(io, j, dbz3::GameDimmed() || dbz3::OverlayOpen());
    DibujarAntenas(io, j);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    // R16 HUD (widescreen): la pantalla de IW (640x448) ocupa TODA la imagen del juego, no solo el recuadro 4:3.
    // En 16:9 el original estira su interfaz hasta el borde; metida en el 4:3, la banda inferior acababa cortada en
    // seco a los 3/4 del ancho (lado derecho). Las piezas redondas (aros, caras, siluetas) usan la escala vertical
    // `k` para no ovalarse; el panel del resultado se queda en el 4:3 (su arte es cuadrado).
    const S::Imagen gi = S::GuestImage(io);
    const float sx = (gi.p1.x - gi.p0.x) / kW, sy = (gi.p1.y - gi.p0.y) / kH;
    auto at = [&](float x, float y) { return ImVec2(gi.p0.x + x * sx, gi.p0.y + y * sy); };
    auto at12 = [&](float x, float y) { return ImVec2(gi.p0.x + x * sx * kW / 1200.0f, gi.p0.y + y * sy * kH / 900.0f); };
    const float k = sy, s900 = (gi.p1.y - gi.p0.y) / 900.0f;
    const S::Area a4 = S::GameArea(io);
    auto at4 = [&](float x, float y) { return a4.At(x * 1200 / kW, y * 900 / kH); };
    // NB006/PINE: los dos clips de paisaje871 y la presentacion321 no llevan
    // HUD. Mg_SetupOpeningUI lo descubre al comenzar la cuenta de READY.
    // IWINI-13 (12/11): el marcador entra con el clip de la cuenta en marcha (QteMg_UpdateCountdownPhase estado 2:
    // UI_SetObjectFlag + UiAnim_Start a la vez), no al cambiar de fase: con la logica del ELF la fase 1 empieza hasta
    // 32 cuadros antes (estados 0/1, clip «quieto»), y la vista va 1-2 cuadros por detras de la interfaz (se veia el
    // marcador sobre el primer plano de la presentacion). Sin clip, como antes.
    const bool sin_clip = g_cb_clip.load() < 0;
    const int t_cuenta = sin_clip ? j.t : g_cb_quieto.load() ? -1 : g_cb_frame.load();
    if (j.fase == 0 || (j.fase == 1 && t_cuenta < 3)) return;
    // EU377F10 oculta los48 objetos al entrar en29F/2A0; el efecto de
    // antenas se ve sin FAILED ni panel durante esos180 cuadros.
    if (j.poses == &kPosesNB006 && j.fase == 3 && j.t < CuadrosResultado(j)) return;
    // Vida de los dos (UiFade 0 y 1 del modulo). s9: el HUD propio de estos minijuegos (DATA_USA 4/0) es el que se
    // pinta cuando esta (banda t38, marcos t17/t18, relleno t19 y caras t53, medido en nb013_030); si faltara, se cae
    // al de los minijuegos de campo (6/0: aro con la cara, marco t15 y relleno amarillo t01).
    // s9: con el HUD de IW (4/0), sus piezas donde las pone IW (medido en nb013_030, 800x600 -> 640x448): aro con la cara
    // centrado en (42, 49), marco t18 en (80, 14) (R16: corregido desde 22; ver el cotejo con c03200) y relleno verde t19
    // en (96, 26); el rival en espejo (x -> 640 - x).
    auto rect = [&](const ps2::Tex& t, float x0, float y0, float w, float h, bool espejo) {
      if (!t) return;
      const float xa = espejo ? kW - x0 - w : x0;
      fg->AddImage(t.id(), at(xa, y0), at(xa + w, y0 + h), ImVec2(espejo ? 1.0f : 0.0f, 0), ImVec2(espejo ? 0.0f : 1.0f, 1));
    };
    // Pieza cuadrada/redonda: el centro se coloca con el reparto a lo ancho y el tamano usa la escala vertical.
    auto pieza = [&](const ps2::Tex& t, float cx, float cy, float w, float h) {
      if (!t) return;
      const ImVec2 c = at(cx, cy);
      fg->AddImage(t.id(), ImVec2(c.x - w * k / 2, c.y - h * k / 2), ImVec2(c.x + w * k / 2, c.y + h * k / 2));
    };
    auto rectC = [&](const ps2::Tex& t, float cx, float cy, float w, float h, bool espejo) {
      if (!t) return;
      const ImVec2 c = at(cx, cy);
      fg->AddImage(t.id(), ImVec2(c.x - w * k / 2, c.y - h * k / 2), ImVec2(c.x + w * k / 2, c.y + h * k / 2),
                   ImVec2(espejo ? 1.0f : 0.0f, 0), ImVec2(espejo ? 0.0f : 1.0f, 1));
    };
    const bool panel_res = rot_[0] && cif_c_ && cif_n_ && j.fase == 3;   // IW quita el HUD en el resultado
    const bool hud_traducido = j.orig && (j.clave == 0x7DB || j.clave == 0x7D4 || j.clave == 0x7DC) && dbz3::iwtrad::Activo();
    if (!hud_traducido) {
    const bool hud4 = banda_ && aro4_ && marco4_ && verde4_;
    for (int i = 0; hud4 && !panel_res && i < 2; ++i) {
      if (static int t1n = 0; t1n < 2 && std::getenv("DBZ3_TEST_IWMINI")) {   // T1-IW 11/11 (solo en pruebas)
        ++t1n;
        REXLOG_INFO("dbz3 t1iw: retrato pintado lado {} cara {} t53 {}", i, j.cara[i], caras_ ? 1 : 0);
      }
      const float v = std::clamp(j.vida[i] / 1000.0f, 0.0f, 1.0f);
      // R16: el marco t18 va 8 px mas arriba (medido en c03200/c04000 del volcado: contenido y 10..25 de 240; el
      // 22 anterior dejaba el relleno pegado al borde superior y un hueco negro enorme debajo).
      rect(marco4_, 80, 14, 216, 40, i != 0);
      if (v > 0) {
        const float x0 = i == 0 ? 96 : kW - 96 - 176 * v;
        fg->AddImage(verde4_.id(), at(x0, 26), at(x0 + 176 * v, 42), ImVec2(i ? 1.0f - v : 0.0f, 0), ImVec2(i ? 1.0f : v, 1));
      }
      // 11/11: el aro t17 trae ya una cara (Goku SSJ) y la tapaba; con EfectosIW la cara va encima del aro.
      // IWINI (12/11): probado poner la cara encima SIEMPRE y retirado: de serie el aro encima ya deja ver la cara
      // correcta (grabado en 7DB/7DC) y la cara encima tapa el borde interior del aro.
      if (EfectosIW()) rectC(aro4_, i ? kW - 42 : 42, 49, 88, 80, i != 0);
      if (caras_ && j.cara[i] >= 0) {   // la cara de IW (t53), 60 px dentro del aro
        const float u = (j.cara[i] % 3) / 3.0f, w = (j.cara[i] / 3) / 3.0f, cx = i ? kW - 42 : 42;
        const ImVec2 c0 = at(cx, 49);
        fg->AddImage(caras_.id(), ImVec2(c0.x - 30 * k, c0.y - 30 * k), ImVec2(c0.x + 30 * k, c0.y + 30 * k),
                     ImVec2(i ? u + 1 / 3.0f : u, w), ImVec2(i ? u : u + 1 / 3.0f, w + 1 / 3.0f));
      } else {
        const ImVec2 c0 = at(i ? kW - 42 : 42, 49);
        dbz3::iwmini::PintarCara(fg, c0.x - 30 * k, c0.y - 30 * k, 60 * k, i ? j.m->rival : j.m->jugador,
                                 i ? j.m->forma_rival : j.m->forma_jugador, i != 0);
      }
      if (!EfectosIW()) rectC(aro4_, i ? kW - 42 : 42, 49, 88, 80, i != 0);
    }
    for (int i = 0; !hud4 && i < 2; ++i) {
      const float v = std::clamp(j.vida[i] / 1000.0f, 0.0f, 1.0f), kk = 1.6f * s900;
      auto x = [&](float px, float w) { return i == 0 ? px : 1200 - px - w; };   // px de 1200x900
      const ImVec2 b0 = at12(x(150, 240 * 1.6f), 92);
      if (barra_) fg->AddImage(barra_.id(), b0, ImVec2(b0.x + 240 * kk, b0.y + 40 * kk), ImVec2(i ? 1.0f : 0.0f, 0),
                               ImVec2(i ? 0.0f : 1.0f, 1));
      if (relleno_ && v > 0) {
        const float x0 = i == 0 ? b0.x + 22 * kk : b0.x + (218 - 200 * v) * kk;
        fg->AddImage(relleno_.id(), ImVec2(x0, b0.y + 12 * kk), ImVec2(x0 + 200 * v * kk, b0.y + 28 * kk), ImVec2(0, 0),
                     ImVec2(v, 1));
      }
      {   // F1: cara del personaje que juega (y del rival) en su forma (iwmini::PintarCara)
        const ImVec2 c0 = at12(x(28, 64 * 1.6f), 30);
        dbz3::iwmini::PintarCara(fg, c0.x, c0.y, 64 * 1.6f * s900, i ? j.m->rival : j.m->jugador,
                                 i ? j.m->forma_rival : j.m->forma_jugador, i != 0);
      }
      if (aro_) fg->AddImage(aro_.id(), at12(x(6, 88 * 1.6f), 8), at12(x(6, 88 * 1.6f) + 88 * 1.6f, 8 + 88 * 1.6f),
                             ImVec2(i ? 1.0f : 0.0f, 0), ImVec2(i ? 0.0f : 1.0f, 1));
      if (!barra_) {   // sin el HUD de IW: barra lisa
        const ImVec2 p1(b0.x + 240 * kk, b0.y + 20 * kk);
        fg->AddRectFilled(b0, p1, IM_COL32(20, 20, 40, 200));
        fg->AddRectFilled(b0, ImVec2(b0.x + (p1.x - b0.x) * v, p1.y), IM_COL32(255, 210, 40, 255));
      }
    }
    // EU 0x379E30: 60 cuadros previos y luego170 de anuncio, no100 en total.
    // IWINI-14 (12/11): el anuncio de 170 cuadros (s_MSN_CV_ReadyStart a los 60) lleva los DOS carteles: READY? no
    // sale al instante y START! sale ANTES de empezar; en juego ya no hay cartel (G02_0x7D4/7DB/7DC_INI: marcador solo,
    // READY? 2 fotos, START! 1 foto, juego sin cartel). 34/130 medidos sobre fotos cada 40 campos (+-5 cuadros).
    // ponytail: afinar con la serie densa del original. DBZ3_IWINI_CARTEL_VIEJO=1: como antes (READY 60-230, START en juego).
    static const bool cartel_viejo = std::getenv("DBZ3_IWINI_CARTEL_VIEJO") != nullptr;
    const int t_anuncio = t_cuenta - 60;
    if (cartel_viejo) {
      if (j.fase == 1 && t_cuenta >= 60) Pieza(fg, ready_, at(kW / 2, 200), k);
      if (j.fase == 2 && j.t < 60 && j.cuadros < 60) Pieza(fg, start_, at(kW / 2, 200), k);
    } else if (j.fase == 1 && t_anuncio >= 34) {
      Pieza(fg, t_anuncio < 130 ? ready_ : start_, at(kW / 2, 200), k);
    }
    if (j.fase >= 1 && hud4 && !panel_res) {   // s9: la banda azul de IW abajo (ya en el READY, como IW) (t38 a lo ancho, 640 x 134) y las dos siluetas
      DibujarBandaIW(fg, at, banda_);
      for (int i = 0; i < 2; ++i)
        if (sil_[i]) pieza(sil_[i], i ? 592 : 52, 384, float(sil_[i].w), float(sil_[i].h));
    }
    // La franja roja opaca anterior era una aproximacion que tapaba el carril1.
    // NB006/PINE muestra las dos guias del propio atlas, incluso antes del START.
    if (j.fase >= 1 && !panel_res && barra06_)
      for (float y : kCarrilY)
        fg->AddImage(barra06_.id(), at(80, y - 4), at(560, y + 4));
    if (j.fase >= 1 && !panel_res) {
      fg->PushClipRect(at(0, 0), at(kW, kH), true);   // s9: los iconos entran desde el borde de la pantalla de IW (4:3)
      for (int l = 0; l < 2; ++l) {
        // s8: medido en la captura del original (nb013_030, 800x600 de 640x448): los aros en x 480 y en y 366 / 409,
        // es decir, kCarrilY son los CENTROS de los carriles (antes se dibujaban 16 px mas abajo y el aro en 420)
        const float y = kCarrilY[l];
        if (!hud4) fg->AddRectFilled(at(0, y - 18), at(kW, y + 18), IM_COL32(0, 0, 0, 110));
        if (objetivo06_) {
          // 4/0 t02: disco(0..56), objetivo con flechas(56..112), cruz(112..152).
          // El aro rojo de 0/1 t10 pertenece a otra interfaz, no a NB006.
          const ImVec2 c0 = at(kAroX, y);
          fg->AddImage(objetivo06_.id(), ImVec2(c0.x - 28 * k, c0.y - 28 * k), ImVec2(c0.x + 28 * k, c0.y + 28 * k),
                       ImVec2(56.0f / 152, 0), ImVec2(112.0f / 152, 1));
          // IW-F02: el 0x2A del #AMA ya trae (300, 393) de la UI de 600; el original lo pone en los objetos 3/4 de NB006 en
          // (150, -28) / (150, 19,5) (dorado 7D4): centro (450, 365) / (450, 412,5) -> x 480 de 640. Antes se sumaba el aro
          // (kAroX, y) y quedaba fuera de la pantalla.
          if (j.destello[l] >= 0 && !j.ui_ok)   // IW-FX-04: con la UI del original lo pinta DibujarUiA
            BotonAma(fg, at, j, 0x2A, float(j.destello[l]), EfectosIW() ? kAroX - 300.0f : kAroX,
                     EfectosIW() ? (l ? 19.5f : -28.0f) : y);
        } else if (objetivo_) {   // el aro de IW (72 x 56; el centro del aro en (38, 28))
          const ImVec2 c = at(kAroX, y);
          fg->AddImage(objetivo_.id(), ImVec2(c.x - 38 * k, c.y - 28 * k), ImVec2(c.x + 34 * k, c.y + 28 * k));
        } else {
          fg->AddCircle(at(kAroX, y), 20 * k, IM_COL32(255, 230, 80, 255), 24, 3 * s900);
        }
        const S::Pic pic = S::Img("botones");
        for (int i = 0; i < j.c[l].sale; ++i) {
          const Icono& ic = j.c[l].ic[i];
          if (!ic.vivo) continue;
          const ImU32 col = ic.resuelto ? IM_COL32(120, 120, 120, 200) : IM_COL32(255, 255, 255, 255);
          const ImVec2 c = at(ic.x + kIcono / 2, kCarrilY[l]);
          if (botones06_) {
            if (j.botones_ama.Valida() && dbz3::settings::InputGlyphs() != "playstation" && !ic.salida) {
              // Ajuste "Iconos de botones" Xbox/Nintendo: el icono lo dibuja style::Glyph; los efectos de acierto/fallo (cruz) y la
              // salida siguen siendo los de IW. Con PlayStation se queda el atlas IW de la ISO del jugador (rama de abajo).
              S::Glyph(fg, kNombre[ic.boton & 15], ImVec2(c.x - kIcono * k / 2, c.y), kIcono * k);
              if (ic.cruz) BotonAma(fg, at, j, 7, float(ic.anim_t), ic.x, y);
              continue;
            }
            if (j.botones_ama.Valida()) {
              // Objeto 0B: 30 cuadros de aparicion (alfa 0 hasta ~t 9; medido 8/11): su reloj corre desde que el icono sale (x = -16),
              // no desde 0 (con t=0 fijo no se veia nunca). Edad = recorrido / velocidad.
              BotonAma(fg, at, j, ic.salida ? 0x0C : 0x0B,
                       ic.salida ? float(ic.anim_t) : std::max(0.0f, (ic.x + 16.0f) / std::max(j.vel, 1.0f)), ic.x, y, ic.boton);
              if (ic.cruz) BotonAma(fg, at, j, ic.salida ? 8 : 7, float(ic.anim_t), ic.x, y);
              continue;
            }
            // Glifos del propio atlas IW, con los mismos codigos PS2 que la secuencia.
            // Fila64: cuadrado,triangulo,circulo,equis; fila100: L1,R1,L2,R2.
            const int boton = ic.boton & 15;
            const int celda = boton >= 12 ? (boton == 15 ? 0 : boton == 12 ? 1 : boton == 13 ? 2 : 3)
                                        : (boton == 10 ? 0 : boton == 11 ? 1 : boton == 8 ? 2 : 3);
            const float yy = boton >= 12 ? 64.0f : 100.0f;
            // UI_SetObjectPosVec usa el CENTRO (rect -16..16), no su borde.
            const ImVec2 c0 = at(ic.x, y);
            fg->AddImage(botones06_.id(), ImVec2(c0.x - 16 * k, c0.y - 16 * k), ImVec2(c0.x + 16 * k, c0.y + 16 * k),
                         ImVec2(celda * 32.0f / 168, yy / 208),
                         ImVec2((celda + 1) * 32.0f / 168, (yy + 32) / 208), col);
            continue;
          }
          if (!pic) {   // sin la pieza de la HD: el nombre del boton
            S::Glyph(fg, kNombre[ic.boton & 15], ImVec2(c.x - kIcono * k / 2, c.y), kIcono * k);
            continue;
          }
          const ImVec4 r = Glifo(ic.boton);
          const float h = kIcono * k * 1.1f, w = (r.z - r.x) * h / (r.w - r.y);
          S::DrawPic(fg, pic, ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), r, col);
        }
      }
      if (j.marca_activa && j.poses!=&kPosesNB014 && j.especial_fase<=1) {
        const float y=kCarrilY[j.especial_lado];
        BotonAma(fg,at,j,0x43,0,j.marca[0],y);
        BotonAma(fg,at,j,0x44,0,j.marca[2],y);
        BotonAma(fg,at,j,0x45,0,j.marca[1],y);
        if (j.poses==&kPosesNB013) {
          const int boton=j.c[j.especial_lado].seq[j.especial_indice];
          BotonAma(fg,at,j,0x0B,0,j.marca[0],y,boton);
          BotonAma(fg,at,j,0x0B,0,j.marca[1],y,boton);
        }
        if (j.especial_fase==0) {
          BotonAma(fg,at,j,j.poses==&kPosesNB006 ? 0x54 : 0x56,float(j.especial_t),300,325);
          if (j.poses==&kPosesNB013) BotonAma(fg,at,j,0x47,float(std::max(0,j.objetivo_pulsos-j.pulsos)),kObjetivo,y);
        }
      }
      if (j.poses==&kPosesNB006 && j.remate_boton>=0 && j.especial_fase>=2 &&
          (!j.remate_resuelto||j.remate_t<j.salida_dur)) {
        BotonAma(fg,at,j,j.remate_resuelto ? 0x0C : 0x46,float(j.remate_resuelto ? j.remate_t : 0),
                 j.remate_x,kCarrilY[0],j.remate_boton);
      }
      fg->PopClipRect();
      if (j.ui_ok) {   // IW-FX-04: los objetos UI del original (orden de la tabla)
        for (int o = 0; o < j.ui.n; ++o) {
          const auto& u = j.ui.o[o];
          if ((u.flags & 3) == 3 && EfectoUiA(j, o, u.anim)) {
            DibujarAmaB(fg, at, j.botones_ama, botones_tx_, int(u.anim), j.ui_t[o], u.x, u.y);
            if (static const bool traza = std::getenv("DBZ3_TEST_IWMINI") != nullptr; traza && u.anim == 7)
              REXLOG_INFO("dbz3 r14: X del fallo obj {:X} t {:.1f} en ({:.0f},{:.0f})", o, j.ui_t[o], u.x, u.y);
          }
        }
      } else if (j.aviso_t > 0) {
        if (j.botones_ama.Valida()) {
          const int id=j.poses==&kPosesNB014 ?
              (j.aviso_carril==2 ? 0x3E : g_sil_viejas ? (j.aviso_carril==0 ? (j.aviso_bien ? 0x40 : 0x42) : (j.aviso_bien ? 0x31 : 0x32)) :
               // QteMg_PlayHitFeedbackUi: carril 0 -> fallo 0x40 (esquiva) / acierto 0x42 (impacto); carril 1 -> fallo 0x33 (impacto) / acierto 0x32 (esquiva)
               j.aviso_carril==0 ? (j.aviso_bien ? 0x42 : 0x40) : (j.aviso_bien ? 0x32 : 0x33)) :
              j.poses==&kPosesNB013 ?
              (j.aviso_carril==0 ? (j.aviso_bien ? 0x3C : 0x3A) : (j.aviso_bien ? 0x35 : 0x36)) :
              (j.aviso_carril==0 ? (j.aviso_bien ? 0x53 : 0x51) : (j.aviso_bien ? 0x4C : 0x4D));
          BotonAma(fg,at,j,id,float(30-j.aviso_t),j.aviso_carril==0 ? 592.0f : 52.0f,384);
        } else Pieza(fg, j.aviso_bien ? hit_ : miss_, at(kAroX, 330), k * 0.8f);
      }
    }
    }   // TRAD/6: tabla, banda, rotulos y carriles los pinta solo el original
    if (j.fase == 3) {
      const float t = std::min(1.0f, j.t / 30.0f);
      // CLEAR del juego (como P4); la nota, el rango y los puntos los ensena el mapa (PantallaDM::Resultado)
      // EU32FB40 suma delta DOS veces a+0xC: el umbral228 no equivale
      // a228 cuadros. La tabla espera la camara final, no90 inventados.
      const int anuncio = j.poses!=&kPosesNB006 ? 115 : CuadrosResultado(j);
      if (!hud_traducido && j.t>=anuncio && j.t<CuadrosResultado(j))
        Pieza(fg, j.rango > 0 ? clear_ : failed_, at(kW / 2, 200), k * 1.5f * (0.8f + 0.2f * t));
      if (panel_res && j.t >= InicioPanelResultado(j)) DibujarResultado(fg, at4, j);
      // IW mantiene la pose detras de la tabla (PINE NB006: 0x2AA/239).
      // Solo se tapa DESPUES de la secuencia completa, al salir por B3 (5+),
      // cuyo cambio de estado recoloca el actor y reinicia el reloj del ganador.
      if (P::EstadoCombate() >= 5)
        fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 255));
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
  ps2::Tex botones06_, objetivo06_;
  std::vector<ps2::Tex> antena_tx_, antena_suma_;
  std::vector<ps2::Tex> antena_z_;   // IW-FX-LAVADO2: antena_tx_ con prueba de Z (solo con dbz3_iw_efectos_original)
  ps2::Escena antena_escena_;
  float aura_param_[2][20]{};
  iwame::ame::AuraCuerpo aura_[2];   // IW-F15a
  std::vector<ps2::Tex> aura_tx_[2];
  ps2::Escena aura_escena_;
  std::vector<ps2::Tex> botones_tx_;
  ps2::Tex banda_, aro4_, marco4_, verde4_, sil_[2], caras_;   // s9: HUD de IW (DATA_USA 4/0)
  ps2::Tex rot_[3], cif_c_, cif_n_, earned_, total_, zeni_;     // s9: panel de resultado de IW
  ps2::Tex titulo_, cif_e_;                                     // s1 F04.3
  ps2::Tex panel_fill_, row_arrow_, row_band_;    // DS72: relleno/triangulo/banda nativos
  std::vector<ps2::Tex> resultado_tx_;
  ps2::Tex row_labels_, row_ranks_;               // DS156: rotulo RANK y letras de rango (334/1 t17/t24)
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer() && rt->graphics_system() && rt->graphics_system()->presenter()) {
    panel=new Panel();rt->graphics_system()->presenter()->AddUIDrawerFromUIThread(panel,63);
  }
}

bool Empezar(int clave, int fila, int dificultad, int personaje) {
  const Modulo2* m = nullptr;
  for (const Modulo2& x : kModulos)
    if (x.clave == clave) m = &x;
  if (!m) return false;
  auto j = std::make_shared<Juego>();
  j->m = m;
  j->clave = clave;
  j->fila = fila;
  j->dif = std::clamp(dificultad, 0, 5);
  j->vel = m->velocidad[j->dif] * (QtePal() ? 1.2f : 1.0f);
  j->poses = m->poses;
  g_poses_a = m->poses;
  g_cb_amc_entrada = m->poses->amc_entrada;
  g_cam_intro7.store(MuestraIntro7{});
  g_cb_clip = -1;
  g_combo_zoom = -1;
  SeriePorClip(false);
  if (m->poses->cam_ini >= 0 && !std::getenv("DBZ3_IWMINI_CAMARA_B3")) {   // DBZ3_IWMINI_CAMARA_B3: la camara propia
    const Poses& q = *m->poses;
    PrecargarClips({q.cam_ini, q.cam_juego, q.cam_gana, q.cam_pierde, q.cam_fin_bien, q.cam_fin_mal,
                    m->poses == &kPosesNB006 ? 0x321 : -1,   // NB006 abre con el clip 0x321 (Mg_UpdateIntroSequence)
                    m->poses == &kPosesNB006 ? 0x98 : -1,    // NB006 punto ganado (Mg_ResetRoundState)
                    m->poses == &kPosesNB006 ? 0x9A : -1,
                    m->poses == &kPosesNB013 ? 0x188 : -1,   // NB013 choque (Mg_StartClashSequence)
                    m->poses == &kPosesNB013 ? 0x189 : -1,
                    m->poses == &kPosesNB013 ? 0x18A : -1,
                    m->poses == &kPosesNB013 ? 0x18B : -1,
                    m->poses == &kPosesNB013 ? 0x18C : -1,
                    q.cam_presenta});                        // s8: la presentacion (0x47E / 0x4B0 / 0x321)
    j->ent = PrecargarEntrada(q.amc_entrada, q.n_entrada);  // s8: entrada de la escena (DATA_CMN 876 / 877)
    // R16 IWINI: EU 331C70/351*: primero el paisaje, despues los actores.
    // PasoBotones ya aplica camara en estado 3, antes del primer tick del panel
    // (t == 1). Mantener aqui el cuadro 0 evita su camara de respaldo de B3.
    if (j->ent > 0 && (clave == 0x7DB || clave == 0x7DC)) CamB(kCamEntrada, true);
  }
  g_origen_ok = false;   // s8: el origen comun se toma al empezar el combate (PasoBotones)
  const char* t = std::getenv("DBZ3_TEST_IWMINI");
  j->auto_ = t && std::strstr(t, "auto");
  j->fallos = t && std::strstr(t, "fallos");
  j->machaca = t && std::strstr(t, "machaca");
  j->pierde = t && std::strstr(t, "pierde");
  std::string error;
  if (!Cargar(*j, error)) {
    REXLOG_WARN("dbz3 r14: {}: {}", m->nombre, error);
    return false;
  }
  if (m->poses == &kPosesNB013 || m->poses == &kPosesNB014)
    CargarAura(*j, {personaje >= 0 ? personaje : m->jugador, m->rival}, {m->forma_jugador, m->forma_rival});
  j->c[0].cuenta = j->c[0].hueco[0];
  j->c[1].cuenta = j->c[1].hueco[0];
  // T2B: 0x7DC con la logica original si la cvar dbz3_iw_7dc_original esta encendida (de serie apagada)
  j->orig = clave == 0x7DC && j->poses && dbz3::iwmips::nb014::g_activa && dbz3::iwmips::nb014::g_activa();
  if (j->orig) REXLOG_INFO("dbz3 r14: 7DC con la logica original del ELF (dbz3_iw_7dc_original)");
  if (!j->orig && clave == 0x7DB && j->poses && dbz3::iwmips::nb013::g_activa && dbz3::iwmips::nb013::g_activa()) {
    j->orig = true;
    REXLOG_INFO("dbz3 r14: 7DB con la logica original del ELF (dbz3_iw_7db_original)");
  }
  if (!j->orig && clave == 0x7D4 && j->poses && dbz3::iwmips::nb006::g_activa && dbz3::iwmips::nb006::g_activa()) {
    j->orig = true;
    REXLOG_INFO("dbz3 r14: 7D4 con la logica original del ELF (dbz3_iw_7d4_original)");
    REXLOG_INFO("dbz3 iwmini: 7d4 ui_anim_d4={} carril0={} carril1={}", int(REXCVAR_GET(dbz3_iw_botones_ui_anim_d4)),
                j->c[0].seq[0], j->c[1].seq[0]);   // PAQ_2: medir la extension de la animacion de UI a 7D4
  }
  REXLOG_INFO("dbz3 r14: {} (clave 0x{:X}, dificultad {}): carril 0 empieza {} {} {}, carril 1 {} {} {}; velocidad {}",
              m->nombre, clave, j->dif, j->c[0].seq[0], j->c[0].seq[1], j->c[0].seq[2], j->c[1].seq[0], j->c[1].seq[1],
              j->c[1].seq[2], j->vel);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_j = j;
  }
  dbz3::launcher::DeferUi(&CreatePanel);
  const int escenario = std::max(P::EscenarioPorMapa(P::MapaIW(m->escenario_iw)), 0);
  if (!dbz3::hub::RequestBattleEx(personaje >= 0 ? personaje : m->jugador, m->rival, escenario, true, -1, 0, 0,
                                 m->forma_jugador, m->forma_rival)) {
    REXLOG_WARN("dbz3 r14: el conector no acepto el combate");
    return false;
  }
  if (m->poses == &kPosesNB006)
    REXLOG_INFO("dbz3 solIW3: NB006 conserva el escenario visible en intro, READY, juego y resultado");
  dbz3::combate::SetMuneco(1);       // el rival quieto, sin IA (le pegan los aciertos del minijuego)
  dbz3::combate::OcultarHud(true);   // sin marcador de B3
  j->presenta_7db = clave == 0x7DB && j->orig && j->ent > 0 && g_clips.count(m->poses->cam_presenta) &&
                   g_clips.count(m->poses->cam_ini) && V2_7DB();
  j->anima_intro = anim7db::Solicitada(clave, j->presenta_7db);
  if (j->anima_intro) { anim7db::Limpiar(); anim7db::escena = true; }
  g_start_apertura_7db = j->presenta_7db;
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
    if (j->anima_intro) anim7db::Limpiar();
    g_corre = false;
    g_cam_intro7.store(MuestraIntro7{});
    g_start_apertura_7db = false;
    dbz3::escenarios::OcultarFondo(false);   // NB006 y el choque de 7DB (sin nada oculto no hace nada)
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
  int cam_fin_bien=-1,cam_fin_mal=-1;
  int dur_recupera=0;
};
constexpr PosesB kPosesNB012 = {931, 0x500, {0x4B0, 0x4B0, 0x4B0}, {0, 0, 0}, {0x4BA, 0x4BA, 0x4BA}, {0x4B1, 0x4B2, 0x4B3},
                                0x4C4, 0x4B1, {0x501, 0x502}, 198, {24, 120},
                                // Mg_UpdateGameIntro 0x14A / 0x44C (Vegeta; 0x44F Ten, 0x452 Krilin, 0x455 Yamcha), Mg_StartNextRound
                                // (tabla 0x47E290 EU), Mg_CheckLastRoundEnd 0x154, Mg_StartResultAnimByChar 0x44E
                                0x14A, 0x44C, {0x14B, 0x14C, 0x14D, 0x14E, 0x14F, 0x150, 0x151, 0x152, 0x153}, 0x154, 0x44E, 180, -1,0x44D,0x44E,34};
constexpr PosesB kPosesNB007 = {922, 0x30C, {0x2BC, 0x2BC, 0x2BC}, {0, 0, 0}, {0x2C6, 0x2C6, 0x2C6}, {0x2BD, 0x2BE, 0x2BF},
                                0x2D0, 0x2BD, {0x30D, 0x30E}, 130, {24, 83},
                                // MiniGame_UpdateCountdown 0xB4 / 0x352, MiniGame_StartNextRound (tabla USA 0x472F00), 0xBE, ShowResult 0x354
                                0xB4, 0x352, {0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD}, 0xBE, 0x354, 180, -1, 0x353, 0x354, 33};
constexpr PosesB kPosesMSN019 = {930, 0x7BC, {0x76C, 0x76E, 0x770}, {0, 0x76D, 0x76F}, {0x776, 0x777, 0x778}, {0x780, 0, 0},
                                 0x7BD, 0x7BE, {0x7BD, 0x7BE}, 45, {0, 0},
                                 kCam879, 0x5AA, {-1, -1, -1, -1, -1, -1, -1, -1, -1}, 0x5AB, 0x5AC, 200, -1};
const PosesB* PosesAlternativasB(int clave,int personaje) {
  struct VarianteB { int personaje; PosesB poses; };
  static const std::array<VarianteB,3> nb012=[] {
    std::array<VarianteB,3> a{};
    constexpr int actores[]={10,12,13}, bancos[]={924,929,932}, ready[]={0x452,0x44F,0x455}, rondas[]={0x169,0x15F,0x155};
    for(int i=0;i<3;++i) {
      a[i]={actores[i],kPosesNB012};auto& p=a[i].poses;p.cmn=bancos[i];
      p.cam_intro=ready[i];p.cam_gana=rondas[i]+9;p.cam_pierde=ready[i]+2;
      p.cam_fin_bien=ready[i]+1;p.cam_fin_mal=ready[i]+2;
      for(int r=0;r<9;++r) p.cam_ronda[r]=rondas[i]+r;
      p.dur_golpe=i==0 ? 66 : i==1 ? 123 : 185;
      p.dur_mal[0]=i==0 ? 36 : 25;p.dur_mal[1]=i==0 ? 36 : 39;
      p.dur_recupera=i==0 ? 36 : 34;
    }
    return a;
  }();
  static const PosesB satan=[] {auto p=kPosesMSN019;p.cmn=926;p.cam_intro=0x5AD;p.cam_gana=0x5AE;p.cam_pierde=0x5AF;return p;}();
  if(clave==0x7DA) for(const auto& a:nb012) if(a.personaje==personaje) return &a.poses;
  if(clave==0x7E1&&personaje==14) return &satan;
  return nullptr;
}
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
     {3, 4, 5, 6, 7, 9}, {4, 5, 6, 7, 8, 10}, 7, 0, &kPosesNB012},   // 31/10: PINE 0x4DB280 = 0 (la misma sala que NB007)
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
REXCVAR_DEFINE_BOOL(dbz3_iw_7d5_rango_pal, true, "DBZ3/IW",
                    "IW 0x7D5: aplicar los intervalos PAL del rango a los tiempos del port de respaldo");

int RangoNB007Pal(float total, int lentas) {
  // EU 0x293820 / 0x295C10: limites inclusivos; por debajo de 55.01, un hueco da rango 1.
  if (lentas >= 10 || total >= 55.01f) return 0;
  if (total >= 45.01f && total <= 55.0f) return 1;
  if (total >= 40.01f && total <= 45.0f) return 2;
  if (total >= 35.01f && total <= 40.0f) return 3;
  if (total >= 25.01f && total <= 35.0f) return 4;
  if (total >=  0.01f && total <= 25.0f) return 5;
  return 1;
}

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

// Candidato aislado: CC8 portado por cuadro guest, nunca por tiempo de ImGui.
constexpr bool kMSN019EstadosNormal = false;
struct PruebaMSN019 {
  std::mutex mutex; // serializa el paso guest y la proyeccion del panel
  bool activa = false, iniciada = false, bloqueada = false;
  iwmsn019::Actor actor;
  iwmsn019::Resultados resultados;
  std::array<iwmsn019::Objeto, 10> objetos{};
  uint32_t alias = 0, clip = 0, objeto_cuerpo = 0, vueltas = 0;
  bool instalada = false, fin_actor = false;
  bool hay_cuadro_guest = false;
  uint32_t cuadro_guest = 0;
  float cuadro = 0, ancla_cursor = 0, sentido_ancla = 1;
};
struct JuegoB {
  uint32_t semilla19 = 0;                         // dia1E3: LFSR de Memory() del reparto original de MSN019
  dbz3::iwmips::msn019::Objeto obj19[10]{};      // objetos de la fila anterior tal como los dejo el original
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
  bool final_pendiente = false;
  int espera_ronda = 0;           // 31/10: tiempo agotado: la cadena de fallo antes de la ronda siguiente (PINE 2,35 s)
  int bloqueo_mal = 0;            // fase 1 (7/11): boton malo -> QTE apagada durante la reaccion 5-6-7 (EU 0x293EF0)
  RelojRecompensaIW recompensa;   // 31/10: EU285F30 sobre la pose sujeta (NB007/NB012)
  bool confirma_fin = false;
  bool final_activo = false;
  int final_t=0,final_dur=0;
  bool d5_fin_por_camara = false;
  PruebaMSN019 msn019;
  // T2B (7/11): 0x7DA con la logica original (iw_mips nb012, cvar dbz3_iw_7da_original): el invitado decide fases, rondas,
  // iconos, juicio y puntos; el port dibuja, pone camaras y poses y devuelve el estado de pose de IW.
  bool orig = false, ini_da = true, pedido_da[10] = {};
  bool d5_trad = false;   // TRAD/5c: la ronda de 0x7D5 ya es la del original arrancado desde el ELF
  uint16_t dir_piloto = 0; int dir_piloto_t = 0;
  bool trad_entrada = false;   // TRAD/5e: ya se pidio la ronda nueva del original en esta entrada   // TRAD/5c, pruebas: cruceta del piloto para el original
  int campo_da = 0, pose_iw = 3, pose_iw_t = 0, pose_iw_dur = 0, aciertos_da = 0, fallos_da = 0, ronda_da = -1;
  uint32_t semilla_da = 0;
  uint64_t t_da = 0;
  // IW-F02 (dbz3_iw_efectos_original): efectos de acierto/fallo de 7DA/7D5 desde los objetos UI del invitado
  // (iw_mips.h namespace ui) con el #AMA 4/1 de DATA_USA (el mismo de NB006/NB013/NB014: 0x2A en (300, 393) = objetivo 0x20).
  anim::ama::Obra botones_ama;
  std::vector<P::Imagen> botones_atlas;
  dbz3::iwmips::ui::Tabla ui;
  float ui_t[dbz3::iwmips::ui::kN] = {};
  bool ui_vis[dbz3::iwmips::ui::kN] = {}, ui_ok = false;
  uint8_t w93[10] = {}, w80 = 0;
  int sub_d5 = 0;
  // IW-FX-04: Mg_SpawnFinishEffectOnce (EU 0x313920, solo NB012): 933/10 una vez al acabar la ultima ronda.
  // NB007 (0x7D5) no lanza ninguno: MgPlyB_* y MiniGame_* no llaman a Effect_DispatchCommandById (barrido fxpose_b.py).
  iwame::ame::Efecto fx_fin;
  std::vector<iwame::ame::Instancia> fx_pose;
  std::vector<P::Imagen> fx_atlas;
  bool fx_fin_hecho = false;
  // IW-FX-05: 7E1 (MSN019) con el #AMA 4/1 (Minigame_WaitLoadResource 0x2EC570: Tween 7,1,4, el mismo que NB006/13/14):
  // objeto 0 = cursor (anim 5 / 0x1C, Results_UpdateCursorOverlap 0x2E9620) y la salida 0x0C de cada objeto cogido
  // (Results_StateEvaluate 0x2EA1F0). {x de 600, boton, reloj}
  int cursor19 = 5;
  float cursor19_t = 0;
  std::vector<std::array<float, 3>> salidas19;
  // PUL-IW-FX-01: el resto de la UI de 7E1 (Results_InitRound 0x2EB460 / Results_StateEvaluate 0x2EA1F0 / Results_StateRecheck
  // 0x2EA140 / Results_UpdateTally): objeto 1 (0 fila nueva, 3 fila cerrada, 2 fallo, 1 vuelta del fallo), objeto 2 (7 fallo en
  // el sitio del cursor, 8 vuelta), puntero 4 (0x25 en y = 30 * fila, oculto tras la ultima) y filas 7+i (0x26-0x28 con el reloj
  // a 0 mientras se juega; 0x17-0x19 / 0x11 al cerrarla). fila19 0 = sin pintar; obj2_19 -1 = oculto.
  int obj1_19 = 0, obj2_19 = -1, puntero19 = 0, fila19[10] = {}, ini19 = -1, recheck19 = 0, fallos19 = 0, intentos19 = 0;
  float obj1_19_t = 0, obj2_19_t = 0, obj2_19_x = 0, puntero19_t = 0, fila19_t[10] = {};
  // PUL-IW-FX-04: cadena19 = paso de la reaccion -1 del actor (0x776/0x777/0x778, -1 nada); objeto 5 (Results_ShowRankBanner
  // 0x12-0x14, -1 oculto) y objeto 6 (0x16 desde Results_InitRound); repite19: el piloto `fallos` repite un cogido (1 vez).
  int cadena19 = -1, obj5_19 = -1, repite19 = 0;
  float obj5_19_t = 0, obj6_19_t = 0;
  uint32_t fx_sem = 10;
};
REXCVAR_DEFINE_BOOL(dbz3_iw_7d5_fin_camara_original, true, "DBZ3/IW",
                    "IW NB007: espera el fin de la camara 0xBE para pasar a 0x353 y a la pose de victoria (pruebas)");
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
bool UsaEstadosMSN019(const JuegoB& j) {
  return j.clave == 0x7E1 && j.m->tipo == kBarrido && j.msn019.activa;
}
int PersonajeEstadosMSN019(const JuegoB& j) { return j.pb->cmn == 926 ? 14 : 15; }
// IWINI-15 (12/11, dbz3_iw_videl_ready_original): en 7E1 la fase 1 de la primera ronda empieza con el 0x5AA entero
// (dur_intro, 200) como PRESENTACION, sin tabla ni cartel; despues el plano frontal fijo y la cuenta (G02_0x7E1_INI:
// 2800-2920 el clip, 2960 tabla, 3000 READY?, 3040 START!, 3080 juego). Devuelve esos cuadros de presentacion (0 = no).
int PresentaVidel(const JuegoB& j) {
  static const bool on = REXCVAR_GET(dbz3_iw_videl_ready_original);
  return on && j.pb && j.m->tipo == kBarrido && j.ronda == 0 ? j.pb->dur_intro : 0;
}
void BloquearEstadosMSN019(JuegoB& j, const char* motivo) {
  if (!j.msn019.bloqueada) REXLOG_WARN("dbz3 MSN019 estados: candidato detenido: {}", motivo);
  j.msn019.bloqueada = true;
  j.msn019.fin_actor = false;
}
void ReiniciarClipMSN019(JuegoB& j) {
  auto& s = j.msn019;
  s.instalada = false;
  s.objeto_cuerpo = s.clip = s.vueltas = 0;
  // Fuerza ambos cuerpos y AP aun con igual codigo. No apagar+reproducir.
  // Sujetar evita perder el fin entre muestras del hilo guest; el consumidor
  // decide si reentra en estado o si empieza otra vuelta SIN reiniciar CC8.
  anim::ReiniciarPose(Luchador(0), s.alias, true);
}

void AplicarActorMSN019(JuegoB& j, const iwmsn019::OrdenActor& orden) {
  auto& s = j.msn019;
  // Camara, audio y efectos quedan en sus propietarios; no ejecutar orden.camara.
  if (!orden.reproducir) return;
  const uint32_t alias = AliasB(*j.pb, s.actor.codigo), x = Luchador(0);
  if (!x || !alias) { BloquearEstadosMSN019(j, "sin actor o alias IW"); return; }
  // EntrarActor ya puso CC8=0; toda entrada fuerza reinicio, incluso mismo alias.
  s.fin_actor = false;
  s.alias = alias;
  ReiniciarClipMSN019(j);
  REXLOG_INFO("dbz3 MSN019 estados: actor={} anterior={} repeticion={} destino={} codigo={:#x} generacion={}",
              s.actor.estado, s.actor.anterior, s.actor.repeticion, s.actor.destino,
              s.actor.codigo, s.actor.generacion);
}
void ReaccionMSN019(JuegoB& j, int peticion) {
  AplicarActorMSN019(j, iwmsn019::PedirReaccion(j.msn019.actor, peticion,
                                              j.msn019.resultados.fila, PersonajeEstadosMSN019(j)));
}
void CuadroActorMSN019(JuegoB& j) {
  auto& s = j.msn019;
  s.fin_actor = false;
  if (!s.iniciada || s.bloqueada) return;
  const uint32_t x = Luchador(0);
  if (!Base() || !x) return;
  anim::EstadoCuerpo e;
  bool encontrada = false;
  for (int cuerpo = 0; cuerpo < 2; ++cuerpo) {
    if (anim::LeerEstadoCuerpo(Base(), x, cuerpo, e) && e.hay && e.codigo == s.alias && e.cuadros &&
        std::isfinite(e.cuadro) && (!s.instalada || e.objeto == s.objeto_cuerpo)) {
      encontrada = true; break;
    }
  }
  if (!encontrada) return; // recurso ausente no equivale a fin
  if (!s.instalada) {
    // Esperar el arranque efectivo: alias correcto, sin vueltas y antes del
    // ultimo cuadro. Un final retenido anterior NO confirma el nuevo pedido.
    if (anim::EstadoPose() != 1 || e.vueltas != 0 || e.cuadro < 0 ||
        e.cuadro >= float(e.cuadros - 1)) return;
    s.instalada = true; s.objeto_cuerpo = e.objeto; s.clip = e.clip;
    s.vueltas = e.vueltas; s.cuadro = e.cuadro;
    return;
  }
  if (e.clip != s.clip) { BloquearEstadosMSN019(j, "cambio de clip dentro de la generacion"); return; }
  const bool ciclo = e.vueltas > s.vueltas;
  const bool retenida = anim::EstadoPose() == 2 && e.cuadro >= float(e.cuadros - 1);
  if (e.vueltas < s.vueltas) { BloquearEstadosMSN019(j, "reinicio de ciclos sin acuse"); return; }
  s.vueltas = e.vueltas; s.cuadro = e.cuadro;
  iwmsn019::FinAnimacion fin;
  fin.generacion = s.actor.generacion; fin.codigo = s.actor.codigo;
  fin.terminada = ciclo || retenida; fin.cc8 = s.actor.cc8; fin.cc8_valido = true;
  const auto generacion = s.actor.generacion;
  if (fin.terminada)
    REXLOG_INFO("dbz3 MSN019 estados: fin real codigo={:#x} cc8={} cac={} cuadro={} longitud={} vueltas={} guest={}",
                s.actor.codigo, s.actor.cc8, s.actor.cac, e.cuadro, e.cuadros, e.vueltas, s.cuadro_guest);
  const auto orden = iwmsn019::ActualizarActor(s.actor, fin, PersonajeEstadosMSN019(j));
  s.fin_actor = fin.terminada && generacion == s.actor.generacion;
  AplicarActorMSN019(j, orden);
  if (s.fin_actor && (s.actor.estado == 0 || s.actor.estado == 4 ||
                     (s.actor.estado == 1 && s.actor.codigo == 0x777 && s.actor.destino == 7))) {
    // AnimationFramesLink original vuelve a dar ciclos sin ejecutar Enter.
    // En777 aun no autorizado: reiniciar SOLO clip; conservar CC0/CC4/CC8.
    ReiniciarClipMSN019(j);
    s.fin_actor = false;
  }
}
void EventoPose(JuegoB& j, int ev) {
  if (!j.pb) return;
  if (UsaEstadosMSN019(j)) {
    // Results es el unico dueno de reacciones/niveles/final. La presentacion
    // anterior se conserva hasta entrar en juego, fuera de este candidato.
    if (ev == kEvIntro && !j.msn019.iniciada) PonerB(*j.pb, j.pb->intro, true);
    return;
  }
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
      j.pose_t = p.dur_mal[0] + p.dur_mal[1] + (p.dur_recupera ? p.dur_recupera : 40);
      j.cola_k = p.mal[1] ? 1 : 0;
      j.cola_t = j.cola_k ? p.dur_mal[0] : 0;
      break;
    case kEvFin:     // resultado: el final (8) y luego el 1 o el 2 en bucle
      j.cola_t = j.cola_k = 0;
      if(j.m->tipo==kBarrido) {
        // la pose de la ultima fila sigue 3 s (CuadroPose t 180)
      } else if (j.rango > 0) {
        // EU293AB0: en victoria espera el fin de 0xBE, no el fin de la pose 0x2D0.
        // Solo el respaldo C++: el ELF completo y el espejo tienen su propia secuencia.
        const uint32_t x = Luchador(0);
        j.d5_fin_por_camara = j.clave == 0x7D5 && !j.orig && !dbz3::iwtrad::Activo() &&
            REXCVAR_GET(dbz3_iw_7d5_fin_camara_original) && x && AliasB(p, p.gana) && AliasB(p, p.fin[0]) &&
            p.cam_gana != p.cam_fin_bien && g_cb_clip.load() == p.cam_gana && !g_cb_quieto.load() && !g_cb_bucle.load() &&
            g_clips.count(p.cam_gana) && g_clips.count(p.cam_fin_bien);
        if (j.d5_fin_por_camara) dbz3::anim::ReiniciarPose(x, AliasB(p, p.gana), true);
        else PonerB(p, p.gana, false, p.fin[0]);
      } else {   // EU Mg_UpdateEndPhase / MiniGame_UpdateMain: 5, 6 y a los 180 de +8 el estado 2
        PonerB(p, p.mal[0], false, p.mal[1]);
      }
      break;
  }
}
void PrecargarCamaras(const PosesB& p) {
  std::vector<int> ids = {p.cam_ini, p.cam_intro, p.cam_gana, p.cam_pierde, p.cam_juego,p.cam_fin_bien,p.cam_fin_mal};
  if(p.cmn==926||p.cmn==930) ids.push_back(p.cmn==926 ? 0x5AD : 0x5AA);
  ids.insert(ids.end(), std::begin(p.cam_ronda), std::end(p.cam_ronda));
  PrecargarClips(ids);
}
// Un cuadro de 60 Hz: la cadena del fallo y el temporizador de la pose.
void CuadroPose(JuegoB& j) {
  g_cb_fase_b = j.fase;
  CuadroSe();
  if (g_cb_clip.load() >= 0 && !g_cb_quieto.load()) ++g_cb_frame;
  // 31/10: EU 0x2EBE20 Results_SetupCameraLight: en juego MSN019 usa la camara 0 de su tabla (EU 0x5D9120/0x5D9140/0x5D91C8),
  // PINE: ojo (0; 10,816; 28,07), objetivo (0; 8,253; 0), 36 grados. La aplica CamaraFov.
  g_cb_videl = j.pb && j.m->tipo == kBarrido && g_cb_clip.load() == j.pb->cam_intro &&
               (j.fase >= 2 || (j.fase == 1 && PresentaVidel(j) > 0 && j.t >= PresentaVidel(j)));   // IWINI-15   // PINE: 0x5AA en su ultimo cuadro hasta 0x7BE/0x5AB; la cambia CamB del final
  if (!j.pb) return;
  if (UsaEstadosMSN019(j)) return; // actor/CC8 avanzan solo en PasoLogicoMSN019
  if (j.fase == 3 && j.d5_fin_por_camara && !j.final_activo && g_cb_clip.load() == j.pb->cam_gana) {
    const auto it = g_clips.find(j.pb->cam_gana);
    if (it != g_clips.end() && CuadroClip(it->second) < float(it->second.nf - 1)) return;
    // EU293530: la camara 0x353 y la pose 0x30D entran en el mismo paso.
    CamB(j.pb->cam_fin_bien);
    dbz3::anim::ReiniciarPose(Luchador(0), AliasB(*j.pb, j.pb->fin[0]), true);
    if (g_cb_clip.load() != j.pb->cam_fin_bien) j.d5_fin_por_camara = false;
    return;
  }
  if(j.final_pendiente && anim::EstadoPose()==2) {
    const auto& p=*j.pb;
    PonerB(p,j.rango>0 ? p.gana : p.pierde,true);
    CamB(j.rango>0 ? p.cam_gana : p.cam_pierde);
    j.final_pendiente=false;
    REXLOG_INFO("dbz3 iw alternativa9: MSN019 banco {} desenlace codigo {:#x}",p.cmn,j.rango>0 ? p.gana : p.pierde);
  }
  if(j.fase==3&&j.t==214&&j.rango<=0&&j.m->tipo!=kBarrido) PonerB(*j.pb,j.pb->fin[1],false);
  if(j.fase==3&&j.t==180&&j.m->tipo==kBarrido) {
    PonerB(*j.pb,j.rango>0 ? j.pb->gana : j.pb->pierde,false);
    CamB(j.rango>0 ? j.pb->cam_gana : j.pb->cam_pierde);
  }
  if(j.fase==3&&!j.final_activo&&(j.m->tipo==kDesplazar||j.m->tipo==kBarrido||j.m->tipo==kTeclear)) {
    const auto& p=*j.pb;
    const uint32_t codigo=j.m->tipo==kBarrido ? (j.rango>0 ? p.gana : p.pierde) : p.fin[j.rango>0 ? 0 : 1];
    const uint32_t alias=AliasB(p,codigo);
    anim::EstadoCuerpo e;
    for(int cuerpo=0;cuerpo<2;++cuerpo)
      if(anim::LeerEstadoCuerpo(Base(),Luchador(0),cuerpo,e)&&e.hay&&e.codigo==alias&&e.cuadros>0) {
        j.final_activo=true;j.final_t=j.t;j.final_dur=int(e.cuadros);
        dbz3::anim::Reproducir(Luchador(0),alias,true,uint32_t(std::max(int(e.cuadros)-1,1)));   // 31/10: sujeta en dur-1 bajo la tabla
        if (j.m->tipo != kBarrido && !j.d5_fin_por_camara) CamB(j.rango > 0 ? p.cam_fin_bien : p.cam_fin_mal);
        REXLOG_INFO("dbz3 iw alternativa9: final real banco {} BSK{:#x} alias{:#x} duracion {} t {}",
                    p.cmn,codigo,alias,e.cuadros,j.t);
        break;
      }
  }
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
      // 31/10: medido contra PCSX2: fila NB012 de 5 botones 6,7 s; leida tal cual duraba 20 s. La USA esta en decimas
      // de cuadro (262; EU 25 cuadros de 50 Hz). A cuadros de 60 Hz:
      if (!m.teclear && j.hueco[r][i] > 0) j.hueco[r][i] = elf.eu ? ::dbz3::original::CamposAPasos(j.hueco[r][i], elf.fuente()) : j.hueco[r][i] / 10;
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
  if (EfectosIW()) {   // IW-F02: el #AMA de los efectos (DATA_USA 4/1) y su atlas 4/0, de la ISO del jugador
    j.botones_ama.Cargar(P::HijoAmb(usa.Entrada(4), 1));
    for (int i = 0, n = P::CuantasAmt(t40); i < n; ++i) j.botones_atlas.push_back(P::TexturaAmt(t40, i));
    REXLOG_INFO("dbz3 iwfx: #AMA 4/1 {} ({} texturas)", j.botones_ama.Valida() ? "cargado" : "NO valido", j.botones_atlas.size());
    P::AfsPs2 cmn;
    if (j.clave == 0x7DA && cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS")) {   // IW-FX-04: 933/10 y su atlas 933/0
      const auto amb = cmn.Entrada(933), amt = P::HijoAmb(amb, 0);
      j.fx_fin = iwame::ame::Leer(P::HijoAmb(amb, 10), true);
      for (int i = 0, n = P::CuantasAmt(amt); i < n; ++i) j.fx_atlas.push_back(P::TexturaAmt(amt, i));
      REXLOG_INFO("dbz3 iwfx04: 933/10: {} grupos, {} raices, atlas {}", j.fx_fin.grupos.size(), j.fx_fin.raices.size(), j.fx_atlas.size());
    }
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
// dia1E3: la fila de MSN019 con el reparto ORIGINAL (Memory(32) % N = objeto oculto y Results_RespawnItem 0x2EB000)
// ejecutado del ELF por src/iw_mips.cpp, tras la cvar dbz3_iw_logica_original (apagada). El LFSR y los objetos de la
// fila anterior (el original mira el solape con ellos) viven aqui, no en el invitado. false -> el reparto del port.
// El oculto (estado 3) no se dibuja y cuenta como hecho. Results_StateBeginRoll oculta uno en la fila 0 y Results_UpdateTally
// en la siguiente si (fila que acaba) + 1 < *(0x5D917C) = 5: filas 0..4 con oculto, 5..9 sin (medido en la captura).
constexpr int kFilasConOculto19 = 5;    // *(u16)0x5D917C medido (cap_7e1)
bool FilaOriginalMSN019(JuegoB& j, bool excluir, bool primera) {
  namespace m19 = dbz3::iwmips::msn019;
  if (!m19::g_fila || j.clave != 0x7E1 || j.n < 1 || j.n > 10) return false;
  m19::EntradaFila in;
  in.n = j.n; in.dif = j.dif; in.mascara = kMascaraBarrido[j.dif]; in.ancho = kAnchoBarrido[j.dif];
  in.excluir = excluir; in.primera = primera; in.fila = uint64_t(j.ronda);
  if (primera) for (auto& o : j.obj19) o = m19::Objeto{};
  for (int i = 0; i < j.n; ++i) in.prev[i] = j.obj19[i];
  if (!j.semilla19) j.semilla19 = uint32_t(Azar()) | 1u;
  uint32_t s = j.semilla19;
  m19::SalidaFila o;
  if (!m19::g_fila(in, o, s)) return false;
  j.semilla19 = s;
  for (int i = 0; i < j.n; ++i) {
    j.obj19[i] = o.obj[i];
    const bool oculto = o.obj[i].estado == 3;
    const int tipo = int(o.obj[i].tipo) & 7;   // Results_ItemTypeToMask 0..7 = kTipoBoton (8 = 0xF0 no entra en las mascaras)
    j.ic[i] = Icono{kTipoBoton[tipo], o.obj[i].x * kW / 600.0f, !oculto};   // el original reparte en 600 de ancho (0x49C84C)
    j.ic[i].resuelto = oculto;
  }
  return true;
}

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
  if (FilaOriginalMSN019(j, j.ronda < kFilasConOculto19, j.ronda == 0)) {   // UpdateTally: fila anterior + 1 < *(0x5D917C)
    j.sale = j.n; j.cursor = j.b0; j.sentido = 1; j.rebotes = 0;
    return;
  }
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
  // fase 1 (7/11): x es el CENTRO del icono (sale en -16 - vel y se retira en kW + 56, como el original): caja x +- 16
  const float i0 = x - kIcono / 2, i1 = x + kIcono / 2, o0 = kObjB - kObjAncho / 2, o1 = kObjB + kObjAncho / 2;
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
  // EU 0x293EF0 MiniGame_CheckInputStep: tras un boton malo pone +0x81 = 1 y QTE_SetEnabled(0): ninguna entrada cuenta
  // y el reloj de la QTE no corre hasta que la cadena de reaccion (MiniGame_UpdateInputCue 0x295140: estados 5 -> 6 -> 7
  // -> 3) devuelve al jugador. La cadena es la misma que la del tiempo agotado: 141 cuadros (PINE 2,35 s).
  if (j.bloqueo_mal > 0) {
    --j.bloqueo_mal;
    return;
  }
  if (j.espera_ronda <= 0) j.nota[j.ronda] += 1.0f / 60;   // QTE_Update: segundos
  // Limite de la QTE (7,0 s en NB007, +0xF8) y MiniGame_FinishRound (EU 0x294300): al agotarse la QTE la ronda se
  // cierra igual y a la lista va el limite, no el tiempo que se tardo. El original tiene ademas el setter
  // QTE_SetTimeLimit (EU 0x2EE700, copia el valor a +0xFC para QTE_IsTimeUp 0x2EE6D0); el port no tiene objeto QTE y
  // aplica el mismo limite con esta comparacion (estructura, DIFIERE de LUNA_IWMINI3_0; no se pone a ojo).
  if (j.espera_ronda > 0) {
    if (--j.espera_ronda > 0) return;
    j.actual = j.n;
  } else if (j.nota[j.ronda] >= kLimiteTeclear) {
    j.nota[j.ronda] = kLimiteTeclear;
    if (j.ronda < 9) {   // EU 0x294300: +4 = 3: la cadena 5-6-7 y luego la ronda siguiente (PINE: 2,35 s)
      EventoPose(j, kEvMal);
      j.espera_ronda = 141;
      return;
    }
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
      j.bloqueo_mal = 141;   // EU 0x293EF0: QTE apagada hasta acabar la reaccion 5-6-7 (ver arriba)
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
  if (REXCVAR_GET(dbz3_iw_7d5_rango_pal)) {
    j.rango = RangoNB007Pal(total, lentas);
  } else if (lentas < 10) {
    for (int r = 5; r >= 1; --r)
      if (total <= kUmbralT[r]) {
        j.rango = r;
        break;
      }
  }
  const float v = j.rango == 0 ? 100.0f : j.pts[j.rango] * j.mult[j.dif];
  j.puntos = (int(v) + 99) / 100 * 100;
  j.zeni = j.puntos;
  j.saldo = P::LeerProgreso("iw_zeni", 0) + j.zeni;
  j.fase = 3;
  j.t = 0;
  REXLOG_INFO("dbz3 r14: fin: total {:.2f} s, rango {}, zeni {} (monedero {})", total, j.rango, j.zeni, j.saldo);
}

void PrepararFilaMSN019(JuegoB& j, bool excluir) {
  auto& s = j.msn019;
  j.n = iwmsn019::kObjetos[j.dif]; j.sale = j.n;
  const float medio = kAnchoBarrido[j.dif] * kW / 2, hueco = 2 * medio / j.n;
  j.b0 = kW / 2 - medio; j.b1 = kW / 2 + medio;
  const int azar = Azar(), omitido = excluir ? azar % j.n : -1;
  // Se conserva el distribuidor espacial del port. No se afirma que mt19937 sea
  // Memory() de IW; solo se restaura el objeto excluido y el estado de cada item.
  for (int i = 0; i < j.n; ++i) {
    int tipo;
    do tipo = Azar() % 8; while (!(iwmsn019::MascaraTipo(tipo) & s.resultados.mascara));
    j.ic[i] = Icono{kTipoBoton[tipo], j.b0 + i * hueco + hueco / 4 * float(Azar() % 2), i != omitido};
    s.objetos[i] = iwmsn019::Objeto{};
    s.objetos[i].estado = i == omitido ? 3 : 0; s.objetos[i].tipo = tipo;
  }
}
void ActivarBounceMSN019(JuegoB& j) {
  j.rebotes = 1;
  j.msn019.ancla_cursor = j.cursor;
  j.msn019.sentido_ancla = j.sentido;
}
bool SolapaMSN019(const JuegoB& j, int i) {
  return j.ic[i].vivo && std::fabs(j.ic[i].x + kIcono / 2 - j.cursor) < kIcono / 2;
}
void PasoBarridoEstadosMSN019(JuegoB& j, uint16_t pulsado) {
  auto& s = j.msn019;
  if (s.bloqueada) return;
  if (!s.iniciada) {
    if (!iwmsn019::IniciarResultados(s.resultados, j.dif)) return;
    // La entrada/READY existente ya termino. No modificar la secuencia de GP2
    // ni anadir otra espera100: este candidato empieza en la frontera de juego.
    s.resultados.fase = iwmsn019::Fase::Evaluar;
    s.iniciada = true;
    PrepararFilaMSN019(j, true);
    ActivarBounceMSN019(j);
    s.actor.destino = -1;
    ReaccionMSN019(j, 0);
    return;
  }
  iwmsn019::EntradaResultados entrada;
  entrada.pulsaciones = pulsado;
  entrada.estado_actor = s.instalada ? s.actor.estado : -1;
  entrada.fin_actor = s.fin_actor;
  entrada.vueltas_antes_mover = j.rebotes;
  const auto fase = s.resultados.fase;
  if (iwmsn019::ActualizaInterfaz(fase)) {
    entrada.objetos_terminados = true;
    for (int i = 0; i < j.n; ++i) {
      const auto orden = iwmsn019::ActualizarObjeto(s.objetos[i], SolapaMSN019(j, i),
                                                   fase == iwmsn019::Fase::Recheck);
      entrada.objetos_terminados &= !orden.pendiente;
      if (orden.ocultar) j.ic[i].vivo = false;
    }
  }
  if (iwmsn019::DebeMoverCursor(fase, entrada.objetos_terminados, entrada.vueltas_antes_mover)) {
    const float paso = j.vel * kW / 600.0f, borde = 24.0f * kW / 600.0f;
    j.cursor += j.sentido * paso;
    if (j.cursor > kW - borde || j.cursor < borde) {
      j.cursor = std::clamp(j.cursor, borde, kW - borde); j.sentido = -j.sentido;
    }
    // Bounce+40 cuenta el retorno al ancla en la MISMA direccion, no cada borde.
    if (j.sentido == s.sentido_ancla && std::fabs(j.cursor - s.ancla_cursor) <= paso / 2) ++j.rebotes;
  }
  std::array<iwmsn019::SolapeEntrada, 10> solapes{};
  for (int i = 0; i < j.n; ++i) {
    solapes[i].hay_datos = true;
    solapes[i].solapa = SolapaMSN019(j, i);
    solapes[i].tipo = s.objetos[i].tipo;
    if (!solapes[i].solapa) j.ic[i].pedido = false;
  }
  entrada.objetos = solapes.data(); entrada.cantidad = static_cast<size_t>(j.n);
  // Las mascaras originales normales no habilitan F0; no inventar un ABI de ejes.
  if ((s.resultados.mascara & 0xF0) != 0) {
    BloquearEstadosMSN019(j, "mascara analogica sin ABI de ejes"); return;
  }
  if (j.auto_ && fase == iwmsn019::Fase::Evaluar && !(j.fallos && j.ronda == 1)) {
    for (int i = 0; i < j.n; ++i) if (solapes[i].solapa && !j.ic[i].pedido &&
                                     (s.objetos[i].estado == 0 || s.objetos[i].estado == 4)) {
      j.ic[i].pedido = true;
      if (const int32_t pad = XboxDe(j.ic[i].boton)) { j.inyecta = pad; j.inyecta_t = 4; }
      else entrada.pulsaciones |= iwmsn019::MascaraTipo(s.objetos[i].tipo);
      break;
    }
  }
  const int fila_anterior = s.resultados.fila;
  const auto orden = iwmsn019::PasoResultados(s.resultados, entrada);
  if (orden.objeto_acertado >= 0) {
    iwmsn019::AcertarObjeto(s.objetos[orden.objeto_acertado]);
    j.ic[orden.objeto_acertado].resuelto = true;
    j.juicio = -1;
  }
  if (orden.error_individual) { j.juicio = 3; j.juicio_t = 30; }
  if (orden.fila_cerrada) {
    j.nota[fila_anterior] = float(iwmsn019::kPuntos[orden.categoria]);
    j.juicio = orden.mostrar_banner ? orden.categoria : -1; j.juicio_t = 40;
  }
  if (orden.ocultar_pendientes) for (int i = 0; i < j.n; ++i) {
    s.objetos[i].estado = 3; j.ic[i].vivo = false;
  }
  if (orden.preparar_objetos) PrepararFilaMSN019(j, orden.excluir_un_objeto);
  if (orden.activar_bounce) ActivarBounceMSN019(j);
  j.ronda = s.resultados.fila;
  if (orden.reaccion != iwmsn019::kSinPeticion) ReaccionMSN019(j, orden.reaccion);
  if (orden.desenlace) {
    j.total = iwmsn019::TotalPuntos(s.resultados);
    j.rango = RangoResultadoIW(j.total) + 1;
    j.fase = 3; j.t = 0;
    for (int i = 0; i < j.n; ++i) j.ic[i].vivo = false;
  }
  if (orden.calcular_premios) {
    j.puntos = PremioResultadoIW(j.total, j.dif); j.zeni = j.puntos;
    j.saldo = P::LeerProgreso("iw_zeni", 0) + j.zeni;
    // El panel existente puede abrirse AHORA; cero es un plazo ya satisfecho,
    // no la duracion inventada de una animacion.
    j.final_activo = true; j.final_t = j.t; j.final_dur = 0;
  }
  if (orden.error_individual || orden.fila_cerrada || orden.desenlace || orden.calcular_premios)
    REXLOG_INFO("dbz3 MSN019 estados: fase={} fila={} reaccion={} intentos={} aciertos={} total={}",
                int(s.resultados.fase), s.resultados.fila, s.resultados.reaccion,
                s.resultados.intentos, s.resultados.aciertos, iwmsn019::TotalPuntos(s.resultados));
}
// Llamado por PasoBotones desde el fotograma guest del escenario, no OnDraw.
void PasoLogicoMSN019(uint8_t* base) {
  std::shared_ptr<JuegoB> j;
  { std::lock_guard<std::mutex> lk(g_mu); j = g_jb; }
  if (!j || !UsaEstadosMSN019(*j)) return;
  std::lock_guard<std::mutex> lk(j->msn019.mutex);
  if (!g_corre_b.load()) return;
  auto& s = j->msn019;
  // Contador del bucle guest sub_820921E0, mismo dato US/EU (eu_map_gen).
  // Dedupe, NO reloj de pared ni multiplicacion por el numero de renders.
  const uint32_t cuadro = REX_LOAD_U32(dbz3::GA(0x8238D6B4));
  if (s.hay_cuadro_guest && s.cuadro_guest == cuadro) return;
  s.hay_cuadro_guest = true; s.cuadro_guest = cuadro;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr) return;
  const uint32_t est = REX_LOAD_U32(mgr + 35128);
  if (j->fase < 2 || (est != 4 && !(j->fase == 3 && est >= 5 && est <= 8)) ||
      dbz3::GameDimmed() || dbz3::OverlayOpen() || s.bloqueada ||
      s.resultados.fase == iwmsn019::Fase::Entregada) return;
  // Un cuadro logico del port: dt=1. CAC portado de MSN019 inicia en1;
  // ninguno de sus Enter modifica CAC. CB4/AP/velocidad de clip NO son CAC.
  constexpr float dt = 1.0f;
  const uint16_t ahora = uint16_t(REX_LOAD_U16(kPads + kPadBotones) | (REX_LOAD_U16(kPads + 108) & 0xF0));
  const uint16_t pulsado = uint16_t(ahora & ~j->antes);
  j->antes = ahora;
  CuadroActorMSN019(*j); // Update: fin real y contador anterior; puede ejecutar Enter
  if (s.bloqueada) return;
  ++j->cuadros;
  if (j->juicio_t > 0) --j->juicio_t;
  PasoBarridoEstadosMSN019(*j, pulsado); // Results puede pedir otra entrada
  if (s.bloqueada) return;
  // PreExec: Tick ve el CC8 reiniciado si hubo Enter; Animations2 suma AL FINAL.
  iwmsn019::TickActor(s.actor, s.actor.cc8, dt, PersonajeEstadosMSN019(*j));
  if (!iwmsn019::AvanzarContadorActor(s.actor, dt, s.actor.cac))
    BloquearEstadosMSN019(*j, "incremento logico dt*CAC invalido");
  if (j->auto_) {
    if (j->fase == 3) dbz3::net::InyectarPad(0, -1);
    else {
      dbz3::net::InyectarPad(0, j->inyecta_t > 0 ? j->inyecta : 0);
      if (j->inyecta_t > 0 && --j->inyecta_t == 0) j->inyecta = 0;
    }
  }
}
// PUL-IW-FX-01: un cuadro logico de los relojes de los objetos UI de 7E1 (todas las fases; el de las filas en juego lo vuelve a
// poner a 0 PasoBarrido, como UI_SetObjectField158AndEnable cada cuadro en Results_StateEvaluate).
void Reloj19(JuegoB& j) {
  if (!EfectosIW() || j.clave != 0x7E1) return;
  j.obj1_19_t += 1; j.obj2_19_t += 1; j.puntero19_t += 1; j.obj5_19_t += 1; j.obj6_19_t += 1;
  for (float& t : j.fila19_t) t += 1;
}
// PUL-IW-FX-04: Results_StateRecheck (EU 0x2EA140) sale cuando el actor deja el estado 1 (+0x14BC != 1 -> +0x290 = 0).
// Results_TriggerReactionAnim(-1, fila) (0x2EACD0) = MgPly_SetNextState(tramo) + MgPly_ChangeState(1); el estado 1 encadena
// 0x776 -> 0x777 -> 0x778 por repeticion (EU 0x308610 / 0x3084B0) y al acabar la 0x778 entra en el destino (la espera del tramo).
// True al salir. recheck19 es solo un tope (ponytail: si el actor no tiene la animacion, no se queda colgado; 1200 cuadros).
bool FinRecheck19(JuegoB& j) {
  if (--j.recheck19 <= 0) return true;
  const uint32_t x = Luchador(0);
  if (!j.pb || !Base() || !x || j.cadena19 < 0 || j.cadena19 > 2) return false;
  const uint32_t a = AliasB(*j.pb, j.pb->golpe[j.cadena19]);
  anim::EstadoCuerpo e;
  bool fin = false;
  for (int c = 0; c < 2 && !fin; ++c)
    fin = anim::LeerEstadoCuerpo(Base(), x, c, e) && e.hay && e.codigo == a && e.cuadros &&
          (e.vueltas > 0 || e.cuadro >= float(e.cuadros - 1));
  if (!fin) return false;
  if (++j.cadena19 <= 2) {
    PonerB(*j.pb, j.pb->golpe[j.cadena19], false);
    REXLOG_INFO("dbz3 pulfx04: 7E1 recheck: actor estado 1 repeticion {} -> {:#x}", j.cadena19, j.pb->golpe[j.cadena19]);
    return false;
  }
  const int n = j.ronda < 3 ? 0 : j.ronda < 6 ? 1 : 2;
  PonerB(*j.pb, j.pb->espera[n], true);
  j.cadena19 = -1;
  return true;
}
void PasoBarrido(JuegoB& j, uint16_t pulsado) {
  if (UsaEstadosMSN019(j)) { PasoBarridoEstadosMSN019(j, pulsado); return; }
  // 31/10: PINE: Bounce rebota en los bordes de la pantalla (24..576 de 600, velocidad 4,8 a 50 Hz) y cuenta una vez por vuelta
  // completa (fila sin pulsar: 35,7 s en dificultad 2; antes 16 s).
  const float borde = 24.0f * kW / 600.0f;
  j.cursor += j.sentido * j.vel * kW / 600.0f;   // Bounce_UpdateMove
  if (j.cursor >= kW - borde || j.cursor <= borde) {
    if (j.cursor >= kW - borde) ++j.rebotes;
    j.cursor = std::clamp(j.cursor, borde, kW - borde);
    j.sentido = -j.sentido;
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
      j.inyecta_t = EfectosIW() && j.fallos && j.ronda == 0 ? 2 : 4;   // PUL-IW-FX-04: suelta antes para repetir sobre el cogido
    } else {
      p = uint16_t(1u << j.ic[bajo].boton);
    }
  }
  // PUL-IW-FX-04: Results_StateEvaluate recorre TODOS los 0x11+ (UI_CheckTwoObjectsOverlap con el 0): pulsar el boton de uno ya
  // cogido cuenta como acierto (+0x7C / +0x6C) sin repetir la salida (estado ya 1) y no es fallo. cogido = ese objeto.
  int cogido = -1;
  for (int i = 0; i < j.n; ++i)
    if (j.ic[i].vivo && j.ic[i].resuelto && std::fabs(j.ic[i].x + kIcono / 2 - j.cursor) < kIcono / 2) cogido = i;
  if (EfectosIW() && j.auto_ && j.fallos && j.ronda == 0 && j.repite19 != 1) {   // prueba: pulsa otra vez un objeto ya cogido (fila 1)
    // el mando venia apretado con el mismo boton del cogido: sin un cuadro suelto no hay flanco (ahora & ~antes) ni pulsado
    if (j.inyecta_t > 0 || bajo >= 0 || cogido < 0) j.repite19 = 0;
    else if (j.repite19 == 0) j.repite19 = 2;   // este cuadro suelta
    else {
      j.repite19 = 1;
      if (const int32_t x = XboxDe(j.ic[cogido].boton)) { j.inyecta = x; j.inyecta_t = 2; }
      else p = uint16_t(1u << j.ic[cogido].boton);
    }
  }
  if (EfectosIW() && j.auto_ && j.fallos && j.ronda == 1 && !j.mal_hecho && bajo < 0) {   // prueba: una pulsacion sin objeto
    j.mal_hecho = true;
    p = uint16_t(1u << 14);
  }
  if (EfectosIW()) {   // IW-FX-05: Results_UpdateCursorOverlap: 0 -> 1 al pisar un objeto (anim 0x1C), 1 -> 0 al salir (5)
    // ponytail: "pisar" con el `bajo` del port (sin los ya cogidos); el original mira el rectangulo de todos los 0x11+
    j.cursor19_t += 1;
    const int a = bajo >= 0 ? 0x1C : 5;
    if (a != j.cursor19) { j.cursor19 = a; j.cursor19_t = 0; }
    for (auto& s : j.salidas19) s[2] += 1;
    j.salidas19.erase(std::remove_if(j.salidas19.begin(), j.salidas19.end(), [](const auto& s) { return s[2] > 600; }), j.salidas19.end());
  }
  const bool fx19 = EfectosIW() && j.clave == 0x7E1 && j.ronda >= 0 && j.ronda < 10;
  if (fx19) {
    if (j.ini19 != j.ronda) {   // Results_InitRound / Results_UpdateTally: fila nueva -> objeto 1 anim 0
      if (j.ini19 < 0) j.obj6_19_t = 0;   // PUL-IW-FX-04: Results_InitRound: UI_SetObjectFlag2(6, 1) (anim 0x16 de Results_SetupUIObjects)
      j.ini19 = j.ronda;
      j.obj1_19 = 0;
      j.obj1_19_t = 0;
      REXLOG_INFO("dbz3 pulfx01: 7E1 fila {} empieza: objeto 1 anim 0, puntero 4 en y {}", j.ronda + 1, 30 * j.puntero19);
    }
    if (j.recheck19 > 0 && FinRecheck19(j)) {   // Results_StateRecheck al salir: objeto 2 anim 8, objeto 1 anim 1
      j.recheck19 = 0;
      j.obj2_19 = 8; j.obj2_19_t = 0;
      j.obj1_19 = 1; j.obj1_19_t = 0;
      REXLOG_INFO("dbz3 pulfx01: 7E1 fila {} vuelta del fallo: objeto 2 anim 8, objeto 1 anim 1", j.ronda + 1);
    }
    if (j.recheck19 > 0) {
      p = 0;   // Results_StateRecheck no lee el pad ni pinta la fila
    } else {   // Results_StateEvaluate: Bounce_GetField40 contra 0x5D917E/80/82 (2/4/7) -> fila 7+i anim 0x26-0x28 (rango 3: sin cambio)
      const int rango = j.rebotes <= 2 ? 0 : j.rebotes <= 4 ? 1 : j.rebotes <= 7 ? 2 : 3;
      if (rango < 3) { j.fila19[j.ronda] = 0x26 + rango; j.fila19_t[j.ronda] = 0; }
    }
    if (p) ++j.intentos19;   // +0x78 de la fila / +0x6E: toda pulsacion de la mascara cuenta
  }
  if (p && bajo >= 0 && BotonPulsado(p) == j.ic[bajo].boton) {
    j.ic[bajo].resuelto = true;
    j.juicio = -1;
    // IW-FX-05: Results_StateEvaluate, acierto: UI_SetObjectField158AndEnable(0, objeto, 0xC) (salida) y estado 1
    if (EfectosIW()) {
      j.salidas19.push_back({(j.ic[bajo].x + kIcono / 2) * 600.0f / kW, float(j.ic[bajo].boton), 0.0f});
      REXLOG_INFO("dbz3 iwfx05: 7E1 cogido fila {} objeto {} boton {} -> salida 0x0C en x {:.1f}; cursor anim 0x{:X}", j.ronda + 1,
                  bajo, j.ic[bajo].boton, j.salidas19.back()[0], j.cursor19);
    }
  } else if (fx19 && p && cogido >= 0 && BotonPulsado(p) == j.ic[cogido].boton) {
    REXLOG_INFO("dbz3 pulfx04: 7E1 fila {} pulsacion sobre objeto {} ya cogido -> acierto (sin salida nueva ni fallo)", j.ronda + 1, cogido);
  } else if (fx19 && p) {
    // PUL-IW-FX-01: Results_StateEvaluate sin objeto valido bajo el cursor: el objeto 2 a la posicion del 0 con anim 7,
    // objeto 1 anim 2 y Results_StateRecheck hasta que acaba la reaccion -1 del actor.
    // PUL-IW-FX-04: la vuelta la marca el actor (FinRecheck19), como el original; recheck19 = tope.
    ++j.fallos19;
    j.obj2_19 = 7; j.obj2_19_t = 0; j.obj2_19_x = j.cursor * 600.0f / kW;
    j.obj1_19 = 2; j.obj1_19_t = 0;
    j.recheck19 = 1200;
    j.cadena19 = 0;
    if (j.pb) PonerB(*j.pb, j.pb->golpe[0], false);   // MgPly_ChangeState(1), repeticion 0
    REXLOG_INFO("dbz3 pulfx01: 7E1 fallo fila {} pulsacion {:#x} (fallos {} de {} pulsaciones) -> objeto 2 anim 7 en x {:.1f}, "
                "objeto 1 anim 2, recheck {} cuadros", j.ronda + 1, p, j.fallos19, j.intentos19, j.obj2_19_x, j.recheck19);
  }
  bool quedan = false;
  for (int i = 0; i < j.n; ++i) quedan |= !j.ic[i].resuelto;
  if (quedan && j.rebotes <= 7) return;
  const int juicio = j.rebotes <= 2 ? 0 : j.rebotes <= 4 ? 1 : j.rebotes <= 7 ? 2 : 3;
  constexpr int kPts[4] = {10, 5, 3, 0};
  j.nota[j.ronda] = float(kPts[juicio]);
  j.juicio = juicio;
  j.juicio_t = 40;
  if (fx19) {   // PUL-IW-FX-01: Results_StateEvaluate al cerrar la fila (completa o por tiempo)
    if (j.recheck19 > 0) { j.recheck19 = 0; j.cadena19 = -1; j.obj2_19 = 8; j.obj2_19_t = 0; }
    if (!quedan && juicio < 3) { j.obj5_19 = 0x12 + juicio; j.obj5_19_t = 0; }   // PUL-IW-FX-04: Results_ShowRankBanner 0x2EAE50
    j.fila19[j.ronda] = juicio == 3 ? 0x11 : 0x17 + juicio;   // 0x17-0x19 / 0x11
    j.fila19_t[j.ronda] = 0;
    j.obj1_19 = 3; j.obj1_19_t = 0;
    j.puntero19 = j.ronda + 1 < 10 ? j.ronda + 1 : -1;   // UI_SetObjectPosVec(0, 30 * (fila+1)) / UI_SetObjectFlag2(4, 0)
    if (quedan)   // los que quedan (estado 0 o 4) salen con 0x0C
      for (int i = 0; i < j.n; ++i)
        if (j.ic[i].vivo && !j.ic[i].resuelto) j.salidas19.push_back({(j.ic[i].x + kIcono / 2) * 600.0f / kW, float(j.ic[i].boton), 0.0f});
    REXLOG_INFO("dbz3 pulfx01: 7E1 fila {} cerrada: rebotes {} -> objeto {} anim 0x{:X}; objeto 1 anim 3; puntero 4 {} ; "
                "fallos {} de {} pulsaciones", j.ronda + 1, j.rebotes, j.ronda + 7, j.fila19[j.ronda],
                j.puntero19 >= 0 ? 30 * j.puntero19 : -1, j.fallos19, j.intentos19);
  }
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

// Total y rango ya puestos: premio y paso al final (fase 3).
void CerrarB(JuegoB& j) {
  // EU 0x32F0F0 QteMg_FinalizeResult
  const float v = j.rango == 0 ? 100.0f : j.pts[j.rango] * j.mult[j.dif];
  j.puntos = (int(v) + 99) / 100 * 100;
  j.zeni = j.puntos;
  j.saldo = P::LeerProgreso("iw_zeni", 0) + j.zeni;
  j.fase = 3;
  j.t = 0;
  REXLOG_INFO("dbz3 r14: fin: total {} rango {} zeni {} (monedero {})", j.total, j.rango, j.zeni, j.saldo);
}

// IW-FX-04: EU 0x313920 Mg_SpawnFinishEffectOnce (NB012). Tras Mg_CheckLastRoundEnd (+0x10 = 1, 30 campos de
// sub-fase 2), la primera vez que la camara pasa del cuadro 14: comando 0x19 -> Cmd_EmitOp0A_B -> billboard 933/10 en el
// hueso del nodo 3 del jugador (actor+0xB7C, short +6), dir = Camera_GetViewDir.
void CuadroFxB(JuegoB& j) {
  if (j.fx_fin.grupos.empty() && j.fx_fin.raices.empty()) return;
  if (!j.fx_fin_hecho && j.fase == 3 && j.t <= 36) {
    const auto it = g_clips.find(g_cb_clip.load());
    ps2::M34 m;
    ps2::V3 ojo, vista{0, 0, 1};
    if (it != g_clips.end() && CuadroClip(it->second) >= 14 && MatrizNodo(Base(), 0, 3, m)) {
      VistaIW(ojo, vista);
      iwame::ame::Instancia in;
      in.ef = &j.fx_fin;
      in.origen = {m.m[3], m.m[7], m.m[11]};
      in.dir = vista;
      in.encadenar = true;
      j.fx_pose.push_back(std::move(in));
      j.fx_fin_hecho = true;
      REXLOG_INFO("dbz3 iwfx04: 7DA fin -> 933/10 nodo 3 en ({:.2f},{:.2f},{:.2f}) clip 0x{:X}", m.m[3], m.m[7], m.m[11], it->first);
    }
  }
  for (auto& in : j.fx_pose) iwame::ame::Cuadro(in, j.fx_sem);
  j.fx_pose.erase(std::remove_if(j.fx_pose.begin(), j.fx_pose.end(), [](const auto& in) { return !in.vivo; }), j.fx_pose.end());
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
    const float d = std::fabs(k.x - kObjB);   // EU 404440: |pos - 320| (pos = centro)
    if (!k.resuelto && d < mejor) {   // el mas cercano al objetivo sin juzgar
      mejor = d;
      j.actual = i;
    }
  }
  uint16_t p = pulsado & 0xFF00;
  if (j.auto_ && j.actual >= 0 && !j.ic[j.actual].pedido && Juicio(j.ic[j.actual].x) == 0 &&
      j.ic[j.actual].x >= kObjB - 2) {
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
  if (p && j.actual >= 0 && j.ic[j.actual].x >= kObjB - 80) {   // EU 0x314060 Mg_UpdateQteInput: (int)(320 - 80) <= pos
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
    // EU 0x312D90 Mg_FinishAndWriteResults: TOTAL SCORE (+0x44) = (int)+0x48 + (int)+0x4C + (int)+0x50, las tres
    // categorias GREAT/GOOD/NORMAL, NO la suma de las 10 rondas (con las rondas salia 956 donde el original escribe 940).
    j.total = int(j.cat[0]) + int(j.cat[1]) + int(j.cat[2]);
    j.rango = 0;   // Mg_ComputeTotalAndRank 0x313500: umbrales +0xCC..+0xF4 sobre ese total
    for (int r = 5; r >= 1; --r)
      if (j.total >= kUmbralB[r]) {
        j.rango = r;
        break;
      }
    CerrarB(j);
  }
}

// T2B (7/11): un cuadro de 60 Hz de NB012 con la logica ORIGINAL (iw_mips nb012::g_campo). El original corre una vez por
// campo PAL (5 de cada 6 cuadros, paso 1,2; con el ELF USA en todos, paso 1,0), como Freezer y Piccolo. Lo que decide
// (fases, rondas, aparicion y avance de iconos, juicio, puntos, fallo, total y rango) sale del invitado; aqui se dibuja,
// se ponen las camaras (UiAnim_Start) y las poses (MgPly_SetPoseState) y se devuelve el estado de pose de IW que la logica
// lee (+0x14BC: 3 espera, 4 golpe, 5 cae, 6 tendido, 7 se levanta) con las duraciones de los clips del port. false = el
// original no pudo correr: la partida sigue con el port desde aqui.
// IW-F02: animaciones de efecto que se dibujan desde la UI del invitado (las demas las pinta el port).
//   7DA: 7 cruz (0x15+i), 0x0C salida del icono (1+i), 0x12/0x13/0x14 GREAT/GOOD/NORMAL (0x2D-0x2F, Mg_ApplyQteJudgeScore),
//        0x2A destello del objetivo (0x3C, Mg_ApplyQteJudgeFeedback). 7D5: 7/8 cruz del fallo (0x15, MiniGame_CheckInputStep /
//        MiniGame_UpdateInputCue).
bool EfectoUiB(const JuegoB& j, int k, uint32_t anim) {
  if (j.clave == 0x7D5) return k == 0x15 && (anim == 7 || anim == 8);
  if (k >= 0x15 && k < 0x1F) return anim == 7;
  if (k >= 1 && k <= 10) return anim == 0x0C;
  if (k >= 0x2D && k <= 0x2F) return anim >= 0x12 && anim <= 0x14;
  return k == 0x3C && anim == 0x2A;
}
// Visible como en el original: bits 1|2 de +0x150; la cruz de 7DA ademas con Mg_UI_UpdateQteIcons 0x312A60 (+0x9D, +0x93 y
// +0x89 del icono) y en la posicion del icono.
bool VisibleUiB(const JuegoB& j, int k, float& x, float& y) {
  const auto& o = j.ui.o[k];
  if ((o.flags & 3) != 3 || !EfectoUiB(j, k, o.anim)) return false;
  x = o.x; y = o.y;
  if (j.clave == 0x7DA && k >= 0x15 && k < 0x1F) {
    const int i = k - 0x15;
    if (!j.ic[i].resuelto || !j.w93[i] || !j.ic[i].vivo) return false;
    x = j.ui.o[i + 1].x; y = j.ui.o[i + 1].y;
  }
  if (j.clave == 0x7DA && k >= 1 && k <= 10 && !(k - 1 < j.n && j.ic[k - 1].vivo)) return false;
  return true;
}
// 7D5: la cruz 0x15 la ensena y coloca MiniGame_UpdateSlotIcons 0x2930E0 (dibujo, no corre en el invitado): visible con +0x80 y
// +4 == 1, en (+0x5C * 50 + 55, 395) (INT 0x473EC0/0x473EC4; huecos 1..10 igual: 55 + 50 * i, dorado 7D5).
bool VisibleUiD5(const JuegoB& j, int k, float& x, float& y) {
  const auto& o = j.ui.o[k];
  if (k != 0x15 || !EfectoUiB(j, k, o.anim) || !j.w80 || j.sub_d5 != 1) return false;
  x = float(j.actual) * 50.0f + 55.0f;
  y = 395.0f;
  return true;
}
// Un campo: UI_UpdateFrame suma el paso (1,2 PAL / 1,0 NTSC) al reloj +0x1C de los objetos con el bit 4; lo que la logica
// pone despues del UI_UpdateFrame (puesto 2) arranca en su t0 sin sumar. Registro de inicio/fin por objeto (prueba).
void AvanzarUiB(JuegoB& j, uint64_t campo) {
  namespace u = dbz3::iwmips::ui;
  j.ui = u::g_tabla;
  j.ui_ok = j.ui.n > 0;
  const float paso = dbz3::iwmips::freezer::g_ntsc ? 1.0f : 1.2f;
  for (int k = 0; k < j.ui.n; ++k) {
    const u::Obj& o = j.ui.o[k];
    float& t = j.ui_t[k];
    if (o.puesto) t = o.t0;
    if (o.puesto != 2 && (o.flags & 5) == 5) t += paso;
    float x = 0, y = 0;
    const bool vis = j.clave == 0x7D5 ? VisibleUiD5(j, k, x, y) : VisibleUiB(j, k, x, y);
    if (vis && (!j.ui_vis[k] || o.puesto))
      REXLOG_INFO("dbz3 iwfx {:X} campo={} obj={:X} anim={:X} inicio t0={:.1f} pos600=({:.1f},{:.1f}) pos640=({:.2f},{:.1f})", j.clave,
                  campo, k, o.anim, o.t0, x, y, x * kW / 600.0f, y);
    else if (!vis && j.ui_vis[k])
      REXLOG_INFO("dbz3 iwfx {:X} campo={} obj={:X} anim={:X} fin t={:.1f}", j.clave, campo, k, o.anim, t);
    j.ui_vis[k] = vis;
  }
}

bool PasoOriginal19(JuegoB& j, bool bucle_ok) {
  if (!bucle_ok) return false;
  if (dbz3::iwtrad::Arrancando()) return true;
  dbz3::iwtrad::Estado19 s;
  if (!dbz3::iwtrad::Leer19(s)) return true;   // Estado transitorio: esperar y volver a leer el siguiente cuadro.
  if (!j.d5_trad) {
    j.d5_trad = true;
    REXLOG_INFO("dbz3 r14: 7E1: la ronda es la del original arrancado desde el ELF");
  }
  j.pendiente = 0;
  for (int i = 0; i < 10; ++i) j.nota[i] = float(s.puntos[i]);
  if (s.fin) {
    j.total = s.total; j.rango = s.rango;
    REXLOG_INFO("dbz3 r14: 7E1 panel recibe total {} rango {} del ELF", j.total, j.rango);   // PAQ_6/18.B
    // Premios y monedero del port, igual que en PasoBarrido; no aplicar dos veces los del ELF.
    j.puntos = PremioResultadoIW(j.total, j.dif); j.zeni = j.puntos;
    j.saldo = P::LeerProgreso("iw_zeni", 0) + j.zeni;
    j.fase = 3; j.t = 0;
    REXLOG_INFO("dbz3 r14: original 7E1: fin: total {} rango {} zeni {}", j.total, j.rango, j.zeni);
    return true;
  }
  const int fila_anterior = j.ronda;
  const bool fila_elf = std::getenv("DBZ3_IW_ANI2_7E1") != nullptr;
  // EU 2EACD0: el tramo se elige con la fila de la peticion, no la anterior.
  if (fila_elf) j.ronda = s.fila;
  const bool jugando = s.estado == 0x2EA1F0 || s.estado == 0x2EA140 || s.estado == 0x2E9D40 || s.estado == 0x2E9920;
  const int fase = jugando ? 2 : s.estado == 0x2EAA90 || s.estado == 0x2EA980 ? 1 : 0;
  if (fase != j.fase) {
    j.fase = fase; j.t = 0;
    if (fase == 1) { CamB(j.pb->cam_intro); EventoPose(j, kEvIntro); }
    if (fase == 2) { CamB(j.pb->cam_juego, false, false, true); EventoPose(j, kEvNivel); }
  }
  if (s.fila != fila_anterior || s.reaccion != j.pose_iw) {
    REXLOG_INFO("dbz3 r14: 7E1 original: estado {:X} fila {} aciertos {} intentos {} reaccion {}", s.estado, s.fila,
                s.aciertos, s.intentos, s.reaccion);
    if (s.reaccion == -1) EventoPose(j, kEvMal);
    else if (s.reaccion == 1) EventoPose(j, kEvGolpe);
    else if (s.reaccion == 0 && s.fila != fila_anterior) EventoPose(j, kEvNivel);
    j.pose_iw = s.reaccion;
  }
  j.ronda = s.fila;
  j.n = s.n;
  // El piloto introduce botones reales; Results_StateEvaluate decide el acierto, sin escribir RAM.
  if (j.auto_ && s.estado == 0x2EA1F0 && j.dir_piloto_t == 0 && !(j.fallos && s.fila == 1)) {
    for (int i = 0; i < s.n; ++i) {
      const auto& o = s.obj[i];
      if (!o.visible || (o.estado != 0 && o.estado != 4) || o.tipo < 0 || o.tipo >= 8) continue;
      if (std::fabs(o.x - s.cursor) > 8.0f) continue;
      j.dir_piloto = uint16_t(1u << kTipoBoton[o.tipo]);
      j.dir_piloto_t = 2;
      break;
    }
  }
  return true;
}

bool PasoOriginalDA(JuegoB& j) {
  namespace nb = dbz3::iwmips::nb012;
  const bool trad = j.clave == 0x7DA && dbz3::iwtrad::Activo();   // TRAD/6: la ronda es la del original arrancado desde el ELF
  if (trad && dbz3::iwtrad::Arrancando()) return true;
  const bool ntsc = dbz3::iwmips::freezer::g_ntsc;
  j.campo_da += ntsc ? 6 : 5;
  if (j.campo_da < 6) return true;   // cuadro sin campo PAL: el original no corre
  j.campo_da -= 6;
  const PosesB& p = *j.pb;
  auto campos = [ntsc](int cuadros) { return ntsc ? cuadros : (cuadros * 5 + 5) / 6; };   // clip a 60 Hz -> campos de 1,2
  // cadena de pose que el original no lleva (la maquina MgPly del actor): fin del clip -> siguiente estado
  ++j.pose_iw_t;
  if (j.pose_iw_dur > 0 && --j.pose_iw_dur == 0) {
    if (j.pose_iw == 5) j.pose_iw = 6;   // cae -> tendido (PonerB ya encadeno el clip)
    else if (j.pose_iw == 4 || j.pose_iw == 7 || j.pose_iw == 0) j.pose_iw = 3;
    j.pose_iw_t = 0;
  }
  nb::Entrada in;
  in.inicio = j.ini_da;
  in.dif = j.dif;
  in.personaje = j.pb == &kPosesNB012 ? 7 : j.pb->cmn == 924 ? 10 : j.pb->cmn == 929 ? 12 : 13;
  const uint16_t pulsado = j.pendiente;
  j.pendiente = 0;
  in.boton = BotonPulsado(uint16_t(pulsado & 0xFFF0));   // sin START/SELECT: no son botones de la secuencia
  in.flanco = pulsado;
  {
    const auto it = g_clips.find(g_cb_clip.load());
    // RITMO (R17): el reloj del clip va a 50/s (CuadroClip, igual que lo que dibuja PasoCamaraB) y `g_cb_frame`
    // cuenta pasos del panel a 60/s. Camera_IsTimerElapsed del original cuenta CUADROS DEL CLIP: sin convertir,
    // el fin de camara llegaba un 20 % antes que el ultimo cuadro dibujado (los cortes de 0x44C/0x14B y del
    // resultado se adelantaban). Mismo criterio que 0x7DB/0x7DC en R16.
    in.cam_fin = it != g_clips.end() && CuadroClip(it->second) >= float(it->second.nf - 1);
  }
  in.pose = j.pose_iw;
  in.pose_t = j.pose_iw_t;
  in.t = j.t_da++;
  if (!j.semilla_da) j.semilla_da = uint32_t(Azar()) | 1u;
  nb::Salida o;
  if (!nb::g_campo || !nb::g_campo(in, o, j.semilla_da)) return false;
  j.ini_da = false;
  if (dbz3::iwtrad::EstadoD5 x; trad) {
    if (!dbz3::iwtrad::LeerD5(x, 0x49CDC8)) return true; // esperar al estado real, sin cerrar con el auxiliar
    if (!j.d5_trad) { j.d5_trad = true; REXLOG_INFO("dbz3 r14: 7DA: la ronda es la del original arrancado desde el ELF"); }
    o.fase = x.fase; o.sub = x.sub; o.ronda = x.ronda; o.botones = x.n; o.actual = x.cuenta; o.total = x.total; o.rango = x.rango;
    o.juicio = x.juicio;
    for (int q = 0; q < 10; ++q) {
      o.seq[q] = x.seq[q]; o.ronda_pts[q] = x.pts[q]; o.x[q] = x.x[q]; o.vivo[q] = x.vivo[q]; o.fallado[q] = x.fallado[q];
      o.acierto[q] = x.acierto[q]; o.w93[q] = x.w93[q];
    }
    for (int q = 0; q < 3; ++q) o.cat[q] = x.cat[q];
    static int ult = -1;
    const int clave = ((x.fase * 16 + x.sub) * 64 + x.ronda) * 64 + x.cuenta;
    if (clave != ult) {
      ult = clave;
      REXLOG_INFO("dbz3 r14: 7DA original: fase {} sub {} ronda {} fila {} cuenta {} total {:.2f} rango {} seq {} {} {} pulsado {:X}", x.fase,
                  x.sub, x.ronda, x.n, x.cuenta, x.total, x.rango, x.seq[0], x.seq[1], x.seq[2], j.antes);
    }
  }
  if (trad && j.d5_trad && o.fase == 2) {   // TRAD/6: el original acabo (fase 2): total y rango suyos; el panel es del port
    if (std::getenv("DBZ3_TEST_IWMINI"))   // PAQ_4: cotejo del fin del 0x7DA (pw = 0x49CDC8)
      REXLOG_INFO("dbz3 paq4: 7DA fin ELF ronda {} total {} rango {} cat {:.2f}/{:.2f}/{:.2f}", o.ronda, int(o.total),
                  o.rango, o.cat[0], o.cat[1], o.cat[2]);
    j.total = int(o.total);
    j.rango = o.rango;
    for (int r = 0; r < 10; ++r) j.nota[r] = o.ronda_pts[r];
    for (int k = 0; k < 3; ++k) j.cat[k] = o.cat[k]; // categorias finales del ELF antes de cerrar
    REXLOG_INFO("dbz3 r18: 7DA categorias ELF great {:.2f} good {:.2f} normal {:.2f}; tabla {} {} {}",
                o.cat[0], o.cat[1], o.cat[2], int(j.cat[0]), int(j.cat[1]), int(j.cat[2]));
    REXLOG_INFO("dbz3 r14: original 7DA: fin en la ronda {}: total {} rango {}", o.ronda, j.total, j.rango);
    CerrarB(j);
    return true;
  }
  std::copy(std::begin(o.w93), std::end(o.w93), std::begin(j.w93));
  for (int k = 0; k < o.n_ev; ++k) {
    const nb::Evento& e = o.ev[k];
    if (e.tipo == 'C') {   // n | 0x10000 = UiAnim_Start con play 0: el clip se queda en su primer cuadro
      const int clip = e.n & 0xFFFF;
      CamB(clip, (e.n & 0x10000) != 0, false, clip != p.cam_ini && clip != p.cam_intro);
    } else if (e.tipo == 'P') {
      if (!trad && (e.n == 8 || (e.n == 5 && o.ronda >= 9 && o.sub == 2))) {   // en traducido solo decide el final del ELF
        j.total = int(o.total);
        j.rango = o.rango;
        for (int r = 0; r < 10; ++r) j.nota[r] = o.ronda_pts[r];
        REXLOG_INFO("dbz3 r14: original 7DA: fin en la ronda {}: total {} rango {}", o.ronda + 1, j.total, j.rango);
        CerrarB(j);
        return true;
      }
      j.pose_iw = e.n;
      j.pose_iw_t = 0;
      j.pose_iw_dur = 0;
      switch (e.n) {
        case 0: PonerB(p, p.intro, false, p.espera[0]); j.pose_iw_dur = campos(90); break;
        case 3: PonerB(p, p.espera[0], true); break;
        case 4:
          PonerB(p, p.golpe[j.variante++ % 3], false, p.espera[0]);
          SeGolpe(0, true);
          j.pose_iw_dur = campos(p.dur_golpe);
          break;
        case 5:
          PonerB(p, p.mal[0], false, p.mal[1]);
          SeGolpe(1, true);
          // EU 310D20 / 310CC0: 4B1 -> 4B2 al fin del clip (931/1, 25 cuadros).
          j.pose_iw_dur = campos(&p == &kPosesNB012 && std::getenv("DBZ3_IW_ANI2_7DA") ? 25 : p.dur_mal[0]);
          break;
        case 7:
          PonerB(p, p.mal[2], false, p.espera[0]);
          j.pose_iw_dur = campos(p.dur_recupera ? p.dur_recupera : 40);
          break;
        default: break;
      }
    }
  }
  const int fase = o.fase == 0 ? 0 : o.sub == 0 ? 1 : 2;
  if (fase != j.fase) {
    if (j.fase == 0) REXLOG_INFO("dbz3 r14: en juego ({} dificultad {}, logica original)", j.m->nombre, j.dif);
    j.fase = fase;
    j.t = 0;
  }
  if (o.ronda != j.ronda_da) {   // ronda nueva: iconos y piloto a cero
    j.ronda_da = o.ronda;
    j.aciertos_da = j.fallos_da = 0;
    std::fill(std::begin(j.pedido_da), std::end(j.pedido_da), false);
  }
  j.ronda = std::clamp(o.ronda, 0, 9);
  j.n = std::clamp(o.botones, 0, 10);
  j.sale = j.n;
  j.actual = o.actual;
  int aciertos = 0, fallos = 0;
  for (int i = 0; i < 10; ++i) {
    j.ic[i] = Icono{int(o.seq[i]), o.x[i] * kW / 600.0f, o.vivo[i] != 0};   // el original mide en 600 de ancho
    j.ic[i].resuelto = o.fallado[i] != 0;
    aciertos += o.acierto[i] != 0;
    fallos += o.fallado[i] != 0;
  }
  for (int r = 0; r < 10; ++r) j.nota[r] = o.ronda_pts[r];
  for (int k = 0; k < 3; ++k) j.cat[k] = o.cat[k];
  if (aciertos > j.aciertos_da) { j.juicio = std::clamp(o.juicio, 0, 3); j.juicio_t = 30; }
  else if (fallos > j.fallos_da) { j.juicio = 3; j.juicio_t = 30; }
  if (EfectosIW()) AvanzarUiB(j, in.t);   // IW-F02: despues de copiar iconos (la cruz mira +0x9D/+0x89)
  j.aciertos_da = aciertos;
  j.fallos_da = fallos;
  // piloto de pruebas (DBZ3_TEST_IWMINI=auto): pulsa el boton del icono actual al entrar entero en el objetivo
  if (j.auto_ && j.fase == 2 && o.actual >= 0 && o.actual < 10) {
    const int i = o.actual;
    if (o.vivo[i] && !o.fallado[i] && !j.pedido_da[i] && o.x[i] >= 298.f && o.x[i] <= 312.f) {
      j.pedido_da[i] = true;
      int bt = o.seq[i];
      if (j.fallos && j.ronda == 1 && !j.mal_hecho) { j.mal_hecho = true; bt = bt == 15 ? 14 : 15; }
      if (const int32_t x = XboxDe(bt)) { j.inyecta = x; j.inyecta_t = 4; }
      else { j.pendiente = uint16_t(j.pendiente | (1u << bt)); if (trad) { j.dir_piloto = uint16_t(1u << bt); j.dir_piloto_t = 4; } }
    }
  }
  return true;
}

// T2B tanda3 (8/11): un campo PAL de NB007 (0x7D5, "teclear") con la logica ORIGINAL (iw_mips nb007::g_campo), como
// PasoOriginalDA. Del invitado: intro, fila, tecleo en orden, limite de tiempo, aciertos/fallos, rondas, total y rango.
// Aqui: dibujo, camaras y poses (MgPlyB_ChangeState -> PonerB).
bool PasoOriginalD5(JuegoB& j) {
  namespace nb = dbz3::iwmips::nb007;
  // TRAD/5c: con dbz3_iw_traducido el original arranca desde el ELF; mientras va por sus menus, la ronda del port espera
  const bool trad = dbz3::iwtrad::Activo();
  if (trad && dbz3::iwtrad::Arrancando()) return true;
  const bool ntsc = dbz3::iwmips::freezer::g_ntsc;
  j.campo_da += ntsc ? 6 : 5;
  if (j.campo_da < 6) return true;
  j.campo_da -= 6;
  const PosesB& p = *j.pb;
  auto campos = [ntsc](int cuadros) { return ntsc ? cuadros : (cuadros * 5 + 5) / 6; };
  ++j.pose_iw_t;
  if (j.pose_iw_dur > 0 && --j.pose_iw_dur == 0) {   // la cadena de pose que la logica no lleva: fin del clip -> espera
    if (j.pose_iw == 5) j.pose_iw = 6;
    else if (j.pose_iw == 4 || j.pose_iw == 7 || j.pose_iw == 0) j.pose_iw = 3;
    j.pose_iw_t = 0;
  }
  nb::Entrada in;
  in.inicio = j.ini_da;
  in.dif = j.dif;
  const uint16_t pulsado = j.pendiente;
  j.pendiente = 0;
  in.boton = BotonPulsado(uint16_t(pulsado & 0xFFF0));
  in.flanco = pulsado;
  in.mantenido = j.antes;
  // 9/11: la logica lee la pose del actor (cadena del fallo y cierre de ronda). Tiempos del dorado de PCSX2 (campos PAL):
  // tendido (6) 68 campos hasta la 2a vuelta del clip, caida (5) 21, levantarse (7) 28.
  in.pose = j.pose_iw;
  in.vueltas = j.pose_iw == 6 ? j.pose_iw_t * 2 / campos(81) : 0;
  in.anim_fin = j.pose_iw_dur == 0;
  {
    const auto it = g_clips.find(g_cb_clip.load());
    // RITMO (R17): igual que 0x7DA (y 0x7DB/0x7DC en R16): Camera_IsTimerElapsed cuenta CUADROS DEL CLIP, que
    // van a 50/s (CuadroClip); `g_cb_frame` cuenta pasos del panel a 60/s. Sin la conversion el fin del clip
    // 0xB4/0xB5-0xBD/0x352 se adelantaba un 20 % y el corte caia antes del ultimo cuadro dibujado.
    in.cam_fin = it != g_clips.end() && CuadroClip(it->second) >= float(it->second.nf - 1);
  }
  in.t = j.t_da++;
  if (!j.semilla_da) j.semilla_da = uint32_t(Azar()) | 1u;
  nb::Salida o;
  if (!nb::g_campo || !nb::g_campo(in, o, j.semilla_da)) return false;
  j.ini_da = false;
  // TRAD/5c: la ronda que se juega es la del original (ELF entero, botones del jugador): fase, ronda, fila, cuenta, total y
  // rango salen de su trabajo; la logica pequena solo da las camaras y poses del port (cosmetico).
  if (dbz3::iwtrad::EstadoD5 x; trad) {
    if (!dbz3::iwtrad::LeerD5(x)) return true; // no usar un final auxiliar cuando el estado original no esta listo
    if (!j.d5_trad) { j.d5_trad = true; REXLOG_INFO("dbz3 r14: 7D5: la ronda es la del original arrancado desde el ELF"); }
    o.fase = x.fase; o.sub = x.sub; o.ronda = x.ronda; o.n = x.n; o.cuenta = x.cuenta; o.total = x.total; o.rango = x.rango;
    o.w80 = x.w80;
    for (int q = 0; q < 10; ++q) o.seq[q] = x.seq[q];
    static int ult = -1;   // registro: cada cambio de fase/sub/ronda/cuenta del original
    const int clave = ((x.fase * 16 + x.sub) * 64 + x.ronda) * 64 + x.cuenta;
    if (clave != ult) {
      ult = clave;
      REXLOG_INFO("dbz3 r14: 7D5 original: fase {} sub {} ronda {} fila {} cuenta {} w80 {} total {:.2f} seq {} {} {} pulsado {:X}", x.fase, x.sub,
                  x.ronda, x.n, x.cuenta, x.w80, x.total, x.seq[0], x.seq[1], x.seq[2], j.antes);
    }
  }
  for (int k = 0; k < o.n_ev; ++k) {
    const nb::Evento& e = o.ev[k];
    if (e.tipo == 'C') {
      const int clip = e.n & 0xFFFF;
      CamB(clip, (e.n & 0x10000) != 0, false, clip != p.cam_ini && clip != p.cam_intro);
    } else if (e.tipo == 'P') {
      j.pose_iw = e.n;
      j.pose_iw_t = 0;
      j.pose_iw_dur = 0;
      switch (e.n) {
        case 0: PonerB(p, p.intro, false, p.espera[0]); j.pose_iw_dur = campos(90); break;
        case 3: PonerB(p, p.espera[0], true); break;
        case 4: PonerB(p, p.golpe[j.variante++ % 3], false, p.espera[0]); SeGolpe(0, true); j.pose_iw_dur = campos(p.dur_golpe); break;
        case 5: PonerB(p, p.mal[0], false, p.mal[1]); SeGolpe(1, true); j.pose_iw_dur = campos(p.dur_mal[0]) + 1; break;
        case 7: PonerB(p, p.mal[2], false, p.espera[0]); j.pose_iw_dur = campos(p.dur_recupera ? p.dur_recupera : 40); break;
        default: break;
      }
    }
  }
  if (o.fase == 2) {   // final: total (+0x44) y rango (+0x88) del original; el panel de resultado es del port
    j.total = int(o.total);
    j.rango = o.rango;
    REXLOG_INFO("dbz3 r14: original 7D5: fin: total {} rango {} ronda {}", j.total, j.rango, o.ronda + 1);
    CerrarB(j);
    return true;
  }
  const int fase = o.fase == 0 ? 0 : o.sub == 0 ? 1 : 2;
  if (fase != j.fase) {
    if (j.fase == 0) REXLOG_INFO("dbz3 r14: en juego ({} dificultad {}, logica original)", j.m->nombre, j.dif);
    j.fase = fase;
    j.t = 0;
  }
  j.ronda = std::clamp(o.ronda, 0, 9);
  j.n = std::clamp(o.n, 0, 10);
  j.sale = j.n;
  j.actual = o.cuenta;
  for (int i = 0; i < 10; ++i) {
    j.ic[i] = Icono{int(o.seq[i]), 0, i < j.n};
    j.ic[i].resuelto = i < o.cuenta;
    j.pulsado[i] = i < o.cuenta;
  }
  j.w80 = o.w80;
  j.sub_d5 = o.sub;
  if (EfectosIW()) AvanzarUiB(j, in.t);   // IW-F02
  if (j.auto_ && j.fase == 2 && o.sub == 1 && j.inyecta_t == 0 && o.cuenta < j.n && (j.t_da & 7) == 0) {   // piloto: la siguiente de la fila
    const int bt = o.seq[o.cuenta];
    if (const int32_t x = XboxDe(bt)) { j.inyecta = x; j.inyecta_t = 4; }
    else {
      j.pendiente = uint16_t(j.pendiente | (1u << bt));
      if (trad) { j.dir_piloto = uint16_t(1u << bt); j.dir_piloto_t = 4; }   // TRAD/5c: la cruceta no va por el mando virtual
    }
  }
  return true;
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
    std::unique_lock<std::mutex> lk_msn019(j->msn019.mutex, std::defer_lock);
    if (UsaEstadosMSN019(*j)) lk_msn019.lock();
    if (CubrirPresentacionB3(est)) return;
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
    if (!UsaEstadosMSN019(*j) || j->fase < 2 || j->msn019.resultados.fase == iwmsn019::Fase::Entregada) {
      j->pendiente = uint16_t(j->pendiente | (ahora & ~j->antes));
      j->antes = ahora;
    }
    acum_ += pausa ? 0.0f : dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
    while (acum_ >= 1.0f / 60) {
      acum_ -= 1.0f / 60;
      ++j->t;
      // MSN019: el 879/0 es la camara de juego y dura 300 cuadros (los mismos que el original, medido: 52,0-57,0 s);
      // despues IW pasa al plano de resultado. La serie solo mide ese tramo, no la camara retenida.
      const auto it879 = g_clips.find(kCam879);
      SerieJuego(j->fase == 2 && !(j->m->tipo == kBarrido && it879 != g_clips.end() && g_cb_frame.load() >= it879->second.nf));
      CuadroPose(*j);
      CuadroFxB(*j);   // IW-FX-04
      // TRAD/3: el bucle principal del IW original corre a la par y su dibujo va al traductor GS (dbz3_iw_traducido)
      bool bucle_ok = true;
      if ((j->clave == 0x7D5 || j->clave == 0x7DA || (j->clave == 0x7E1 && j->orig)) && dbz3::iwtrad::Activo()) {
        if (!j->trad_entrada) { j->trad_entrada = true; dbz3::iwtrad::PonerClave(j->clave); dbz3::iwtrad::NuevaEntrada(); }
        bucle_ok = dbz3::iwtrad::CuadroBucle(uint16_t(j->antes | (j->dir_piloto_t > 0 ? j->dir_piloto : 0)));
        if (j->dir_piloto_t > 0) --j->dir_piloto_t;
      }
      if (j->orig && j->fase < 3) {   // T2B: 0x7DA con la logica original (PasoOriginalDA); el final (fase 3) es el del port
        if (j->fase == 0 && j->t == 1) {
          CamB(g_entrada_b.load() > 0 ? kCamEntrada : j->pb->cam_ini);
          PonerB(*j->pb, j->pb->espera[0], true);
        }
        if (j->clave == 0x7E1 ? PasoOriginal19(*j, bucle_ok) : j->clave == 0x7D5 ? PasoOriginalD5(*j) : PasoOriginalDA(*j)) {
          if (j->auto_) {
            dbz3::net::InyectarPad(0, j->inyecta_t > 0 ? j->inyecta : 0);
            if (j->inyecta_t > 0 && --j->inyecta_t == 0) j->inyecta = 0;
          }
          continue;
        }
        j->orig = false;
        REXLOG_WARN("dbz3 r14: {:X}: la logica original no pudo seguir; la partida continua con el port", j->clave);
      }
      if (j->pb) {   // poses de IW: espera, READY y (MSN019) el tramo de filas; y sus camaras
        if (j->fase == 0 && j->t == 1) {
          CamB(g_entrada_b.load() > 0 ? kCamEntrada : j->pb->cam_ini);
          if (j->m->tipo == kBarrido) PonerB(*j->pb, j->pb->intro, false, j->pb->intro);   // PINE: 0x7BC durante 879/0
          else PonerB(*j->pb, j->pb->espera[0], true);
        } else if (j->fase == 1 && j->ronda == 0 && j->t == 1) {
          CamB(j->pb->cam_intro);
          EventoPose(*j, kEvIntro);
        } else if (j->fase == 1 && j->ronda == 0 && j->m->teclear && j->t == 181) {
          CamB(j->pb->cam_ini, false, false, true);   // PINE: 0xB4 3,1 s antes de contar
          PonerB(*j->pb, j->pb->espera[0], true);
        } else if (j->fase == 1 && j->ronda > 0 && j->ronda <= 9 && j->t == 1) {
          CamB(j->cam_r[j->ronda - 1], false, false, true);
        } else if (j->fase == 2 && j->t == 1 && j->m->tipo == kBarrido && j->ronda > 0) {
          EventoPose(*j, kEvNivel);
        }
      }
      // EU 0x295030 MiniGame_UpdateDispatch
      if (j->fase <= 1) j->pendiente = 0;
      Reloj19(*j);
      // EU 0x294990 MiniGame_UpdateIntroPhase
      if (j->fase == 0 && j->t >= (j->m->tipo == kBarrido ? 300 : std::max(30, g_entrada_b.load()))) {
        // EU 0x293970 MiniGame_CheckReadyStart: a los 60.0 cuadros del reloj del minijuego (+8) saca el READY
        // (s_MSN_CV_ReadyStart: 2, o 1 si +0x88 != 0) y pasa a +0xc = 1. Aqui el READY es la fase 1: 180 cuadros de
        // clip en la primera ronda (dur_intro, medido en el TSV del original) y 60 en las siguientes, y la salida sin
        // clip espera 30 cuadros como el clip 0xB4 quieto. `coincide`.
        j->fase = 1;
        j->t = 0;
        REXLOG_INFO("dbz3 r14: en juego ({} dificultad {})", j->m->nombre, j->dif);
      } else if (j->fase == 1 && j->t >= (j->m->teclear ? (j->ronda == 0 ? 180 + 186 : 37)   // 31/10: PINE NB007 3 s + 3,1 s y 0,62 s
                                          : PresentaVidel(*j) > 0 ? PresentaVidel(*j) + 230   // IWINI-15: + cuenta (60 + 170)
                                          : (j->ronda == 0 && j->pb) ? j->pb->dur_intro : 60)) {   // READY? (la primera) o el numero de ronda
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
      } else if (j->fase == 2 || (UsaEstadosMSN019(*j) && j->fase == 3 &&
                                 j->msn019.resultados.fase != iwmsn019::Fase::Entregada)) {
        if (!UsaEstadosMSN019(*j)) {
          PasoB(*j, j->pendiente);
          j->pendiente = 0;
          if (j->auto_) {
            dbz3::net::InyectarPad(0, j->inyecta_t > 0 ? j->inyecta : 0);
            if (j->inyecta_t > 0 && --j->inyecta_t == 0) j->inyecta = 0;
          }
        }
      } else if (j->fase == 3 && j->t == 1) {
        if (std::getenv("DBZ3_IWMINI_RAFAGA_FIN")) std::ofstream("dbz3_shot.req") << "40 15";   // prueba
        if (j->auto_) dbz3::net::InyectarPad(0, -1);
        // EU 0x293530 MiniGame_ShowResult
        // 31/10: PINE: en derrota sigue el clip de la ultima ronda hasta la pose (cam_fin_mal)
        if (j->pb && j->rango > 0 && j->m->tipo != kBarrido) CamB(j->pb->cam_gana);
        EventoPose(*j, kEvFin);
      }
      // 31/10: EU285F30 al acabar la pose final (PINE: 0x30E/0x502 240, 0x30D/0x501 300/240), encima de ella. A avanza y cierra
      if (!fin_ && j->fase == 3 && j->final_activo && j->t >= j->final_t + j->final_dur) {
        const bool prueba = j->auto_ || std::getenv("DBZ3_TEST_IWMINI");
        j->recompensa.Avanzar(1.0f, ((j->pendiente & 0x4000) != 0) || (prueba && j->recompensa.cuadro >= 420));
        j->pendiente = uint16_t(j->pendiente & ~0x4000);
        if (j->recompensa.cerrado) {
          fin_ = true;
          REXLOG_INFO("dbz3 iw alternativa9: tabla cerrada; entrega al conector");
          REX_STORE_U8(mgr + kFin, uint8_t(j->rango > 0 ? 1 : 2));
        }
      }
      if(false) {
        fin_=true;
        REXLOG_INFO("dbz3 iw alternativa9: desenlace completo banco {} duracion {} retenida; entrega al conector",
                    j->pb->cmn,j->final_dur);
        REX_STORE_U8(mgr+kFin,uint8_t(j->rango>0 ? 1 : 2));
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
    ama_tx_.clear();
    for (const auto& im : j.botones_atlas) ama_tx_.push_back(ps2::Crear(im));
    fx_tx_.clear();
    fx_suma_.clear();
    fx_z_.clear();
    for (const auto& im : j.fx_atlas) {   // IW-FX-04: como antena_tx_/antena_suma_ de PanelA
      fx_tx_.push_back(ps2::Crear(im));
      if (EfectosIW()) fx_z_.push_back(ps2::Crear(im, true));   // IW-FX-LAVADO2
      P::Imagen su = im;
      for (size_t i = 0; i + 3 < su.rgba.size(); i += 4)
        su.rgba[i + 3] = uint8_t(std::max({su.rgba[i], su.rgba[i + 1], su.rgba[i + 2]}) * su.rgba[i + 3] / 255);
      fx_suma_.push_back(ps2::Crear(su));
    }
    banda_ = ps2::Crear(j.banda);
    fila_ = ps2::Crear(j.caja_fila);
    cifras_ = ps2::Crear(j.cifras);
  }
  void Dibujar(ImGuiIO& io, const JuegoB& j) {
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    PintarFxPose(io, j.fx_pose, fx_tx_, fx_suma_, nullptr, &fx_z_);   // IW-FX-04; LAVADO2: Z
    const S::Area a = S::GameArea(io);
    auto at = [&](float x, float y) { return a.At(x * 1200 / kW, y * 900 / kH); };
    const float k = a.s * 900 / kH;
    // s9: NB007 con las piezas de IW (cotejado con la ronda 1 de PCSX2): sin aro ni cara arriba; la tabla de las 10
    // rondas a la izquierda (caja t16 y cifras t06, filas cada 30 px como MiniGame_SetupSprites) y la banda t38 abajo
    const bool iw4 = banda_ && fila_ && cifras_;   // NB012 tambien usa tabla izquierda y banda originales
    auto cifra = [&](int g, float x, float y, float h) {   // g: 0-9, 10 = ', 11 = "
      const float cw = 128 / 5.0f, ch = 88 / 4.0f;
      const int col = g < 10 ? g % 5 : 3, row = g < 10 ? g / 5 : g == 10 ? 2 : 3;
      const float f = g < 10 ? 1.0f : 0.55f;   // ' y " ocupan la mitad izquierda de su celda
      const float w = h * cw * f / ch;
      fg->AddImage(cifras_.id(), at(x, y), at(x + w, y + h), ImVec2(col * cw / 128, row * ch / 88),
                   ImVec2((col + f) * cw / 128, (row + 1) * ch / 88));
      return w;
    };
    const bool hud_fuera = j.fase == 3 && j.t >= 60;   // Mg_UpdateEndPhase: 60 del reloj +8
    // IWINI (11/11): la tabla sale al empezar la cuenta de READY (Mg_SetupOpeningUI), no durante el plano de
    // presentacion (fase 0): en 7E1 se veia sobre el paisaje 5 s antes (original 31:03: a la vez que READY?).
    // IWINI-15: con dbz3_iw_videl_ready_original, nada durante el 0x5AA (presentacion); t_b = cuadros de la cuenta
    const int pres_v = j.fase == 1 ? PresentaVidel(j) : 0;
    const int t_b = j.t - pres_v - (pres_v > 0 ? 60 : 0);   // misma cuenta que PanelA: 60 previos y el anuncio
    const bool hud_dentro = j.fase >= 1 && !(pres_v > 0 && j.t < pres_v);
    // TRAD/4: con dbz3_iw_traducido el HUD de 7D5 lo pinta el original traducido (iw_trad_bucle.cpp); aqui no, para no verlo doble
    const bool hud_traducido = (j.clave == 0x7D5 || j.clave == 0x7DA || (j.clave == 0x7E1 && j.orig)) && dbz3::iwtrad::Activo();
    if (!hud_traducido) {
    if (iw4 && hud_dentro && !hud_fuera) {
      for (int r = 0; r < 10; ++r) {
        const float y = 22 + 30 * r;
        fg->AddImage(fila_.id(), at(14, y), at(14 + 144, y + 28), ImVec2(0, 0), ImVec2(1, 1),
                     r == j.ronda && j.fase < 3 ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 200));
        float x = 20;
        if (r == 9) x += cifra(1, x, y + 5, 18);
        x += cifra((r + 1) % 10, x, y + 5, 18) + 14;
        const bool hecha = r < j.ronda || (r == j.ronda && j.fase == 3);
        const int cs = hecha ? int(std::lround(j.nota[r] * 100)) : 0;   // centesimas: SS"CC como IW
        if (!j.m->teclear) {   // NB012/MSN019: puntos, no centesimas de NB007
          if (hecha) { const int n = int(j.nota[r]); if(n>=100)x+=cifra(n/100%10,x,y+5,18); if (n >= 10) x += cifra(n / 10 % 10, x, y + 5, 18); cifra(n % 10, x, y + 5, 18); }
          continue;
        }
        const int d[4] = {cs / 1000 % 10, cs / 100 % 10, cs / 10 % 10, cs % 10};
        x += cifra(d[0], x, y + 5, 18);
        x += cifra(d[1], x, y + 5, 18);
        x += cifra(11, x, y + 5, 18);
        x += cifra(d[2], x, y + 5, 18);
        cifra(d[3], x, y + 5, 18);
      }
      if (j.fase == 1 || j.fase == 2) DibujarBandaIW(fg, at, banda_);   // IWINI-14: con la tabla, desde READY (G02_0x7DA_INI)
    }
    for (int r = 0; !iw4 && hud_dentro && !hud_fuera && r < 10; ++r) {
      char txt[32];
      if ((r < j.ronda || (r == j.ronda && j.fase == 3)) && j.m->teclear)
        std::snprintf(txt, sizeof txt, "%2d  %5.2f\"", r + 1, j.nota[r]);
      else if (r < j.ronda || (r == j.ronda && j.fase == 3))
        std::snprintf(txt, sizeof txt, "%2d   %3d", r + 1, int(j.nota[r]));
      else
        std::snprintf(txt, sizeof txt, "%2d   ---", r + 1);
      S::MenuText(fg, a.At(1060, 150 + 44 * r), 34 * a.s, txt, r == j.ronda && j.fase < 3, r > j.ronda);
    }
    // IWINI-14: mismo anuncio que en PanelA (aqui empieza con la fase 1): READY? de 34 a 130 y START! hasta el juego.
    static const bool cartel_viejo_b = std::getenv("DBZ3_IWINI_CARTEL_VIEJO") != nullptr;
    if (cartel_viejo_b) {
      if (j.fase == 1 && j.ronda == 0) PiezaB(fg, ready_, at(kW / 2, 200), k);   // las demas: la fila de la lista se ilumina
      if (j.fase == 2 && j.t < 60 && j.ronda == 0) PiezaB(fg, start_, at(kW / 2, 200), k);
    } else if (j.fase == 1 && j.ronda == 0 && (pres_v > 0 ? t_b : j.t) >= 34) {
      PiezaB(fg, (pres_v > 0 ? t_b : j.t) < 130 ? ready_ : start_, at(kW / 2, 200), k);
    }
    // PUL-IW-FX-01: filas 7+i, puntero 4, objeto 1 y objeto 2 del #AMA 4/1 (7E1 sin estados), en su sitio del #AMA mas la
    // posicion de UI_SetObjectPosVec (filas y puntero: y = 30 * fila; objeto 2: la del cursor al fallar). Hasta Results_HideAllUI.
    if (EfectosIW() && j.clave == 0x7E1 && j.m->tipo == kBarrido && !UsaEstadosMSN019(j) && j.botones_ama.Valida() &&
        !ama_tx_.empty() && j.fase >= 1 && !hud_fuera) {
      for (int r = 0; r < 10; ++r)
        if (j.fila19[r]) DibujarAmaB(fg, at, j.botones_ama, ama_tx_, j.fila19[r], j.fila19_t[r], 0.0f, 30.0f * float(r));
      if (j.puntero19 >= 0) DibujarAmaB(fg, at, j.botones_ama, ama_tx_, 0x25, j.puntero19_t, 0.0f, 30.0f * float(j.puntero19));
      DibujarAmaB(fg, at, j.botones_ama, ama_tx_, j.obj1_19, j.obj1_19_t, 0.0f, 0.0f);
      if (j.ini19 >= 0) DibujarAmaB(fg, at, j.botones_ama, ama_tx_, 0x16, j.obj6_19_t, 0.0f, 0.0f);   // PUL-IW-FX-04: objeto 6
      if (j.obj5_19 >= 0) DibujarAmaB(fg, at, j.botones_ama, ama_tx_, j.obj5_19, j.obj5_19_t, 0.0f, 0.0f);   // objeto 5
      if (j.obj2_19 >= 0) DibujarAmaB(fg, at, j.botones_ama, ama_tx_, j.obj2_19, j.obj2_19_t, j.obj2_19_x, kYB + kIcono / 2);
    }
    const bool fx = EfectosIW() && j.ui_ok && !ama_tx_.empty() && (j.clave == 0x7DA || j.clave == 0x7D5);
    if (j.fase == 2) {
      const float y = fx ? kYB : kYB + kIcono / 2;   // IW-F02: con los efectos, la fila en la y del original (395)
      if (!iw4) fg->AddRectFilled(at(0, y - 18), at(kW, y + 18), IM_COL32(0, 0, 0, 110));
      const bool fx19 = EfectosIW() && j.clave == 0x7E1 && j.m->tipo == kBarrido && !UsaEstadosMSN019(j) &&
                        j.botones_ama.Valida() && !ama_tx_.empty() && j.botones_ama.Objeto(uint32_t(j.cursor19)) >= 0;
      if (fx19) {   // IW-FX-05: el cursor (objeto 0) y las salidas 0x0C de los cogidos con el #AMA del original
        fg->AddRectFilled(at(j.b0, y - 4), at(j.b1, y + 4), IM_COL32(255, 255, 255, 120));
        DibujarAmaB(fg, at, j.botones_ama, ama_tx_, j.cursor19, j.cursor19_t, j.cursor * 600.0f / kW, y);
        static int dur = 0;
        if (!dur) dur = std::max(1, int(anim::ama::MaxCuadros(anim::ama::CrearArbol(j.botones_ama, j.botones_ama.Objeto(0x0C)))));
        for (const auto& s : j.salidas19)
          if (s[2] < float(dur)) DibujarAmaB(fg, at, j.botones_ama, ama_tx_, 0x0C, s[2], s[0], y, int(s[1]));
      } else if (j.m->tipo == kBarrido) {   // la barra y el cursor (objeto 0): el aro de IW
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
        if (fx && j.clave == 0x7DA && j.ui.o[i + 1].anim == 0x0C) continue;   // IW-F02: la salida 0x0C la pinta el #AMA
        const ImU32 col = ic.resuelto ? IM_COL32(120, 120, 120, 200) : IM_COL32(255, 255, 255, 255);
        // NB007: la fila quieta centrada (48 px de paso) y el aro en el que toca
        const float x = j.m->teclear ? (fx ? (55.0f + 50.0f * float(i)) * kW / 600.0f : kW / 2 + (i - (j.n - 1) / 2.0f) * 48)
                        : j.m->tipo == kBarrido ? ic.x + kIcono / 2 : ic.x;   // NB012: x ya es el centro
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
      if (!(fx && j.clave == 0x7DA) && j.juicio_t > 0 && j.juicio >= 0)
        PiezaB(fg, palabra_[j.juicio], at(j.m->teclear ? kW / 2 : kObjB, kYB - 40), k);
      if (fx)   // IW-F02: los efectos del original desde sus objetos UI (orden de la tabla = el de UI_DrawObjects por +0x18)
        for (int o = 0; o < j.ui.n; ++o) {
          float x = 0, y = 0;
          if (!(j.clave == 0x7D5 ? VisibleUiD5(j, o, x, y) : VisibleUiB(j, o, x, y))) continue;
          const int boton = o >= 1 && o <= 10 ? j.ic[o - 1].boton : -1;
          DibujarAmaB(fg, at, j.botones_ama, ama_tx_, int(j.ui.o[o].anim), j.ui_t[o], x, y, boton);
        }
    }
    }   // TRAD/4
    if (j.fase == 3) {
      // 31/10: PINE (NB007/NB012): sin velo; a los 60 cuadros se va el HUD y sale CLEAR!/FAILED! hasta la pose final;
      // al acabar esta, la tabla encima de ella.
      if (!hud_traducido && j.t >= 60 && !j.final_activo) PiezaB(fg, j.rango > 0 ? clear_ : failed_, at(kW / 2, 200), k);
      if (j.final_activo && j.t >= j.final_t + j.final_dur) {
        Resultado r;
        r.clave = j.clave;
        r.fila = j.fila;
        r.superado = j.rango > 0;
        r.centesimas = j.cuadros * 100 / 60;
        r.puntos = j.puntos;
        r.score = j.total;
        r.zeni = j.zeni;
        r.saldo = j.saldo;
        r.rango = j.rango > 0 ? j.rango - 1 : -1;
        r.monedas = j.total;
        r.dificultad = j.dif;
        r.con_tiempo = j.m->tipo != kBarrido;
        if (j.m->tipo == kDesplazar) r.great = int(j.cat[0]), r.good = int(j.cat[1]), r.normal = int(j.cat[2]);
        if (j.m->tipo == kBarrido) {
          int grande = 0, bueno = 0, normal = 0;
          for (float v : j.nota) {
            const int nota = int(v);
            if (nota >= 10) grande += 10;
            else if (nota >= 5) bueno += 5;
            else if (nota >= 3) normal += 3;
          }
          r.great = grande, r.good = bueno, r.normal = normal;
        }
        const ImVec2 q0 = at(0, 0), q1 = at(kW, kH);
        DibujarResultadoIW(fg, q0.x, q0.y, q1.x - q0.x, q1.y - q0.y, r, j.recompensa.cuadro / 60.0f, false);
      }
    } else if (false) {
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
      const int fin=j.final_activo ? j.final_t+j.final_dur : 1000000;
      if (const float neg = std::clamp((j.t - fin) / 36.0f, 0.0f, 1.0f); neg > 0)   // a negro hasta el mapa
        fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, int(255 * neg)));
    }
  }
  const JuegoB* hecho_ = nullptr;
  bool fin_ = false;
  float acum_ = 0;
  ps2::Tex objetivo_, ready_, start_, clear_, failed_, aro_, palabra_[4], banda_, fila_, cifras_;
  std::vector<ps2::Tex> ama_tx_;   // IW-F02: atlas 4/0 del #AMA de efectos
  std::vector<ps2::Tex> fx_tx_, fx_suma_, fx_z_;   // IW-FX-04: atlas 933/0 (7DA); LAVADO2: fx_z_ con prueba de Z
};

void CreatePanelB() {
  static PanelB* panel = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new PanelB(rt->imgui_drawer());
}

bool EmpezarRondas(int clave, int fila, int dificultad, int personaje) {
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
  const int jugador=personaje>=0 ? personaje : m->jugador;
  if(const auto* p=PosesAlternativasB(clave,jugador)) j->pb=p;
  REXLOG_INFO("dbz3 iw alternativa9: clave {:#x} actor {} banco {} intro {:#x}",clave,jugador,j->pb->cmn,j->pb->intro);
  g_nave_lado = (m->indice == 6 || m->indice == 18) ? -1.0f : 1.0f;
  g_cb_amc_entrada = m->indice == 6 ? 872 : m->indice == 11 ? 875 : 0;   // 31/10: PINE NB012: 875/0, 300 cuadros
  const char* t = std::getenv("DBZ3_TEST_IWMINI");
  j->auto_ = t && std::strstr(t, "auto");
  j->fallos = t && std::strstr(t, "fallos");
  std::string error;
  if (!CargarB(*j, error)) {
    REXLOG_WARN("dbz3 r14: {}: {}", m->nombre, error);
    return false;
  }
  if (clave == 0x7E1) {
    const char* estados = std::getenv("DBZ3_TEST_MSN019_ESTADOS");
    const bool solicitado = t && *t && std::strcmp(t, "0") != 0 && estados && std::strcmp(estados, "1") == 0;
    j->msn019.activa = !dbz3::iwtrad::Activo() && (kMSN019EstadosNormal || solicitado);
    if (j->msn019.activa)
      REXLOG_INFO("dbz3 MSN019 estados: prueba activa; CC8 logico dt*CAC y ReiniciarPose; pendiente de verificacion");
  }
  EmpezarRonda(*j);
  // T2B: 0x7DA con la logica original si la cvar dbz3_iw_7da_original esta encendida (de serie apagada)
  j->orig = clave == 0x7DA && j->pb && dbz3::iwmips::nb012::g_activa && dbz3::iwmips::nb012::g_activa();
  if (j->orig) REXLOG_INFO("dbz3 r14: 7DA con la logica original del ELF (dbz3_iw_7da_original)");
  if (!j->orig && clave == 0x7D5 && j->pb && dbz3::iwmips::nb007::g_activa && dbz3::iwmips::nb007::g_activa()) {
    j->orig = true;
    REXLOG_INFO("dbz3 r14: 7D5 con la logica original del ELF (dbz3_iw_7d5_original)");
  }
  if (clave == 0x7E1 && j->pb && dbz3::iwtrad::Activo()) j->orig = true;
  g_cb_clip = -1;
  if (j->pb && !std::getenv("DBZ3_IWMINI_CAMARA_B3")) PrecargarCamaras(*j->pb);
  // NB012/NB007 usan una camara fija por ronda: una fila por ronda. MSN019 usa un clip animado (879/0): por tiempo.
  SeriePorClip(m->tipo != kBarrido);
  SerieClip(-1);
  g_entrada_b = ((m->indice == 6 || m->indice == 11) && m->poses && !std::getenv("DBZ3_IWMINI_CAMARA_B3")) ? PrecargarEntrada(m->indice == 6 ? 872 : 875, 1) : 0;
  REXLOG_INFO("dbz3 r14: {} (clave 0x{:X}, dificultad {}): rondas {} {} {} {} {} {} {} {} {} {}, {} botones, velocidad {}",
              m->nombre, clave, j->dif, j->orden[0], j->orden[1], j->orden[2], j->orden[3], j->orden[4], j->orden[5],
              j->orden[6], j->orden[7], j->orden[8], j->orden[9], j->n, j->vel);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_jb = j;
  }
  dbz3::launcher::DeferUi(&CreatePanelB);
  // NB007: la camara de gravedad de la nave de IW (DATA_CMN 880, mod center hd/campos_iw.py) si esta instalada
  const int nave = m->indice == 6 || (m->indice == 11 && jugador != 10 && jugador != 13) ? dbz3::escenarios::CampoIW(880) : -1;
  g_nave880 = nave >= 0;
  const int mapa=clave==0x7DA&&(jugador==10||jugador==13) ? 12 : m->escenario_iw;
  const int escenario = nave >= 0 ? nave : std::max(P::EscenarioPorMapa(P::MapaIW(mapa)), 0);
  if (!dbz3::hub::RequestBattleEx(jugador, 18, escenario, true, -1, 0, 0, 0, 0)) {
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
  if (clave == 0x7E1 && personaje < 0) r.traje_jugador = 1;   // Videl de IW: camiseta blanca de Orange Star (B3 fid 411)
  P::VigilarCombate("r14", r, [j](const P::Fin&) {
    g_corre_b = false;
    std::unique_lock<std::mutex> lk_msn019(j->msn019.mutex, std::defer_lock);
    if (UsaEstadosMSN019(*j)) lk_msn019.lock();
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
    if (lk_msn019.owns_lock()) lk_msn019.unlock();
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
  // Durante 7DB fase 0, START llega a IW por g_raw12, no al menu de pausa de B3.
  const bool apertura = g_start_apertura_7db.load();
  static thread_local bool start_iw = false;
  start_iw = (g.b12 & 8) && anim7db::escena.load() && (apertura || start_iw);
  const uint16_t kDeja = apertura || start_iw ? 0x0001 : 0x0009;
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
  if (static const bool cl=std::getenv("DBZ3_TEST_CAM_RES")!=nullptr; cl && g_cb_clip.load()>=0)   // eIW5
    REXLOG_INFO("dbz3 eIW5 cam: clip 0x{:X} cuadro {} ojo ({:.1f} {:.1f} {:.1f}) obj ({:.1f} {:.1f} {:.1f}) origen ({:.1f} {:.1f} {:.1f})", g_cb_clip.load(), g_cb_frame.load(), ojo[0],ojo[1],ojo[2],obj[0],obj[1],obj[2],g_origen[0],g_origen[1],g_origen[2]);
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
  if (&p == &kPosesNB006 || &p == &kPosesNB013 || &p == &kPosesNB014) {
    // PINE SLES-55347, sol-IW2: ambos objetos tienen MATRIZ IDENTIDAD,
    // posicion0 y las pistas de WAIST ya dan el mundo (final Gohan0,2.701,-4.568;
    // Piccolo-0.678,13.993,7.209). El yaw1834 NO es la matriz de dibujo.
    // solIW5 confirma NB013; solIW7 lo mide tambien en NB014 (intro,READY,
    // golpe597/596 y final): las32 componentes de ambas matrices = identidad.
    // No girar las pistas ni sumar una compensacion empirica.
    for (int rol = 0; rol < 2; ++rol) {
      const uint32_t x = rol ? x1 : x0;
      for (uint32_t o : {64u, 80u}) {
        StoreF(base, x + o, g_origen[0] + (rol ? kMedio : -kMedio));
        StoreF(base, x + o + 4, g_origen[1]);
        StoreF(base, x + o + 8, g_origen[2]);
      }
      for (uint32_t i = 0; i < 16; ++i)
        StoreF(base, x + i * 4, i == 0 || i == 5 || i == 10 || i == 15 ? 1.0f : 0.0f);
      StoreF(base, x + 48, g_origen[0]);
      StoreF(base, x + 52, g_origen[1]);
      StoreF(base, x + 56, g_origen[2]);
    }
    return;
  }
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
static void CamaraFov(uint8_t* base, uint32_t mgr, const float ojo[3], const float obj[3], float fov, float roll = 0.0f,
    float cuadro_evaluado = -1.0f, int duracion_evaluada = -1, int clave_evaluada = -1) {
  if (dbz3::gp2cam::Activo()) {
    // Pasados por quien acaba de muestrear: no releer un reloj que el panel
    // puede haber avanzado ya entre Muestra() y esta publicacion.
    const int id = clave_evaluada, cmn = g_cb_amc.load();
    dbz3::gp2cam::Evaluada("qte", cmn, cmn == 915 ? 2 : -1,
        id >= kCamEntrada ? 1 : 0, id >= kCamEntrada ? id - kCamEntrada : id >= kCam879 ? id - kCam879 : id,
        cuadro_evaluado, duracion_evaluada,
        g_corre_b.load() ? g_cb_fase_b.load() : g_fase_a.load(), -1, ojo, obj, fov, roll, id);
  }
  static const bool sin = std::getenv("DBZ3_S8_SIN_FOV") != nullptr;
  if (g_cb_videl.load()) {
    static constexpr float ojo_v[3] = {0, 10.816f, 28.07f}, obj_v[3] = {0, 8.253f, 0};
    CamaraMinijuego(true, ojo_v, obj_v, 0.62832f);
    dbz3::cine::Camara(base, ojo_v, obj_v, 0.0f);
    return;
  }
  fov = std::clamp(fov, 0.08726647f, 3.0543263f);   // B2: los mismos limites que Camera_SetupFieldOfView (5..175 grados)
  if (g_poses_a.load() == &kPosesNB014) {
    const int t = g_combo_zoom.load();
    if (t >= 0) {
      if(g_combo_fov.load()<0) g_combo_fov=fov;
      const float grados = t<=5 ? -2.0f*float(t) : -10.0f+2.0f*float(t-5);
      fov=std::clamp(g_combo_fov.load()+grados*0.017444444444444446f,0.08726647f,3.0543263f);
    }
  }
  if (g_corre_b.load() || g_poses_a.load() == &kPosesNB006 || g_poses_a.load()==&kPosesNB013 || g_poses_a.load()==&kPosesNB014) {
    // 31/10: familia B tambien (NB007/NB012/MSN019). sol-IW2: cine::Camara escribe una entrada que B3 mezcla, NO su camara
    // activa de dibujo. DS177 ya ofrece la via exacta antes de sub_820F4300:
    // mismo ojo/objetivo/FOV del #AMC, sin acercamiento ni lazo empirico.
    CamaraMinijuego(true, ojo, obj, fov, roll);
    dbz3::cine::Camara(base, ojo, obj, roll);
    return;
  }
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
  float roll = 0.0f;
  MuestraClip(c, f, e, t, &fov, &roll);
  const float ojo[3] = {px + e[0], py + e[1], pz + e[2]}, obj[3] = {px + t[0], py + t[1], pz + t[2]};
  CamaraFov(base, mgr, ojo, obj, fov, roll, f, c.nf, id);
}

// Familia A: la camara de IW del clip en curso, anclada a los dos luchadores. En IW los dos objetos estan en el origen y su
// separacion sale de la raiz de las poses; el clip se hizo para ese par (centro y eje jugador -> rival en `ancla`). Se lleva
// al mundo de B3 con el centro = punto medio de los objetos, el eje = jugador -> rival y el suelo = y del jugador.
// s8: con el origen comun (Colocar) los clips van tal cual, sumados al origen: el mundo de IW y el de B3 solo se
// diferencian en la traslacion, porque los luchadores tienen el mismo giro que en IW (medido) y estan en su eje X.
static bool PasoCamaraA(uint8_t* base, uint32_t mgr, const Poses& p) {
  // No mezclar el clip antes del corte con el reloj de la siguiente instantanea.
  const auto m = &p == &kPosesNB013 && anim7db::Habilitada() && !std::getenv("DBZ3_S10_CLIP")
      ? g_cam_intro7.load() : MuestraIntro7{};
  const int id = m.clip >= 0 ? m.clip : g_cb_clip.load();
  const auto it = id >= 0 ? g_clips.find(id) : g_clips.end();
  if (it == g_clips.end()) return false;
  const ClipCam& c = it->second;
  const float f = m.clip >= 0 ? std::min(m.f, float(c.nf - 1)) : CuadroClip(c, id);
  if (g_origen_ok.load() && !std::getenv("DBZ3_R14_ANCLA")) {   // DBZ3_R14_ANCLA: la camara vieja (ancla), para comparar
    float e[3], t[3], fov = 0.6f;
    float roll = 0.0f;
    MuestraClip(c, f, e, t, &fov, &roll);
    if (const char* ff = std::getenv("DBZ3_S8_FOV")) fov = float(std::atof(ff));   // prueba: fov forzado
    const float ojo[3] = {g_origen[0] + e[0], g_origen[1] + e[1], g_origen[2] + e[2]};
    const float obj[3] = {g_origen[0] + t[0], g_origen[1] + t[1], g_origen[2] + t[2]};
    CamaraFov(base, mgr, ojo, obj, fov, roll, f, c.nf, id);
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
  float roll = 0.0f;
  MuestraClip(c, f, e, t, &fov, &roll);
  mundo(e, ojo);
  mundo(t, obj);
  CamaraFov(base, mgr, ojo, obj, fov, roll, f, c.nf, id);
  return true;
}

// EU1E11C0/1E0DA0: comprobar el ambiente autorado, no corregir el color
// con un filtro empirico. Medido IW/B3: los20 campos de luz son identicos.
void AmbienteNB006(uint8_t* base) {
  static const Juego* visto=nullptr;
  std::shared_ptr<Juego> j;
  { std::lock_guard<std::mutex> lk(g_mu); j=g_j; }
  const bool activo=g_corre.load() && j && j->poses==&kPosesNB006 && j->ambiente06;
  if (!activo) { visto=nullptr; return; }
  if (visto==j.get()) return;
  const uint32_t g=GA(0x823A3D80), id=REX_LOAD_U32(g), var=REX_LOAD_U32(g+64);
  if (id>=17 || var>1) return;
  const uint32_t rec=uint32_t(GA(0x82323760))+id*92;
  const uint32_t li=REX_LOAD_U32(rec+0x1C+var*32), ni=REX_LOAD_U32(rec+0x20+var*32);
  if (li>=64 || ni>=64) return;
  // US820C1DA8 / EU820C1D88: misma tabla82322B20, a780h de
  // la tabla de niebla ya verificada en eu_map_gen.h.
  const uint32_t luz=uint32_t(GA(0x823232A0))-0x780u+li*80;
  const uint32_t niebla=uint32_t(GA(0x823232A0))+ni*20;
  int distintos=0, fog_distintos=0;
  for (uint32_t i=0;i<20;++i) distintos+=REX_LOAD_U32(luz+4*i)!=j->luz06[i];
  // B3 usa +4/+8 como fondo; IW los complementa: no compararlos como
  // si fueran los mismos parametros ni escribirlos en la tabla nativa.
  for (uint32_t i:{0u,3u,4u}) fog_distintos+=REX_LOAD_U32(niebla+4*i)!=j->niebla06[i];
  REXLOG_INFO("dbz3 solIW4: ambiente IW/B3: luz {} de20 distinta, color/distancias niebla {} de3 distintos; A ({:.3f},{:.3f},{:.3f}), fog {:08X} {:.1f}..{:.1f}",
              distintos,fog_distintos,LoadF(base,luz),LoadF(base,luz+4),LoadF(base,luz+8),REX_LOAD_U32(niebla),
              LoadF(base,niebla+12),LoadF(base,niebla+16));
  visto=j.get();
}
// IW-F15a: con el aura original, fuera la de B3 en 7DB/7DC: los efectos del bloque de aura de cada jugador (420 B en
// *0x82322AEC, 48 en +212) se sueltan con sub_82089C10, como pasarela.cpp ApagarAuraFinal y tagteam.cpp SwapAura.
void SoltarAuraB3(uint8_t* base) {
  const Poses* pp = g_poses_a.load();
  if (!AuraIW() || (pp != &kPosesNB013 && pp != &kPosesNB014)) return;
  const uint32_t g = REX_LOAD_U32(0x82322AEC);
  if (!g) return;
  PPCContext ctx{};
  int n = 0;
  for (uint32_t k = 0; k < 2; ++k)
    for (uint32_t i = 0; i < 48; ++i) {
      const uint32_t a = g + 420 * k + 212 + 4 * i, p = REX_LOAD_U32(a);
      if (!p) continue;
      ctx.r3.u64 = p;
      DBZ3_CALL_EU(sub_82089C10)(ctx, base);
      REX_STORE_U32(a, 0);
      ++n;
    }
  static int avisos = 0;
  if (n && avisos < 6) {
    ++avisos;
    REXLOG_INFO("dbz3 r14: aura original: {} efectos del aura de B3 soltados", n);
  }
}
void PasoBotones(uint8_t* base) {
  AmbienteNB006(base);
  if (g_corre_b.load()) {
    PasoLogicoMSN019(base);
    if (const uint32_t mgr = REX_LOAD_U32(kMgrPtr)) {
      Sonda(base, mgr, "B");
    }
    PasoCamaraB(base);
    if (const uint32_t mgr = REX_LOAD_U32(kMgrPtr)) {
      // 31/10: PINE MSN019: el actor en el origen con matriz identidad en toda la escena; sin esto salia de perfil.
      const uint32_t x0 = mgr + kX0;
      static constexpr float kId[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
      for (uint32_t i = 0; i < 12; ++i) StoreF(base, x0 + 4 * i, kId[i]);
      for (uint32_t o : {48u, 64u, 80u}) StoreF(base, x0 + o, 0), StoreF(base, x0 + o + 4, 0), StoreF(base, x0 + o + 8, 0);
    }
    return;
  }
  if (!g_corre.load()) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr) return;
  PasoFade(base,mgr);
  SoltarAuraB3(base);
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
void FadeRival(int cuadros) {
  g_chr_paleta[1]=iwchrfade::kImpacto; g_chr_total=cuadros; g_chr_fade[1]=cuadros;
  if (TinteIW()) REXLOG_INFO("dbz3 pulfx05: tinte 32B2A0 actor 1 color {:08X} cuadros {}", iwchrfade::kImpacto, cuadros);
}
void PasoFade(uint8_t* base,uint32_t mgr) {
  for(int rol=0;rol<2;++rol) {
    const int t=g_chr_fade[rol].load();
    if(t<=0) {g_chr_activo[rol]=false;continue;}
    const uint32_t x=mgr+kX0+rol*kXPaso;
    const float intensidad=.95f*float(t-1)/float(g_chr_total.load());
    g_chr_color[rol]=intensidad;g_chr_activo[rol]=true;
    StoreF(base,x+6260,intensidad);
    const uint32_t paleta=g_chr_paleta[rol].load();
    REX_STORE_U32(x+6264,0);REX_STORE_U32(x+6268,paleta);REX_STORE_U32(x+6272,paleta);
    g_chr_fade[rol]=t-1;
    if(TinteIW()&&std::getenv("DBZ3_TEST_IWMINI"))
      REXLOG_INFO("dbz3 pulfx05: tinte cuadro actor {} restante {} intensidad {:.6f} k {:.6f} color {:08X}",rol,t-1,intensidad,intensidad*intensidad,paleta);
    if(std::getenv("DBZ3_TEST_IWMINI"))
      REXLOG_INFO("dbz3 iw CHRFADE9: actor {} restante {} intensidad {:.6f} campos6260/6264/6268/6272",rol,t-1,intensidad);
  }
}
// Se reaplica DESPUES de la transicion de color nativa y ANTES de que el
// dibujado del personaje lea los campos. Es el consumidor de CHRFADE, no
// una captura de geometria pasada (EU2F2D70 ->2F4450).
void AplicarCHRFADE9(uint8_t* base,uint32_t x) {
  if(!g_chr_activo[0].load()&&!g_chr_activo[1].load()) return;
  const uint32_t mgr=REX_LOAD_U32(kMgrPtr);
  for(int rol=0;rol<2;++rol) if(x==mgr+kX0+rol*kXPaso&&g_chr_activo[rol].load()) {
    const float v=g_chr_color[rol].load();
    StoreF(base,x+6260,v);REX_STORE_U32(x+6264,0);
    const uint32_t paleta=g_chr_paleta[rol].load();
    REX_STORE_U32(x+6268,paleta);REX_STORE_U32(x+6272,paleta);
    if(std::getenv("DBZ3_TEST_IWMINI"))
      REXLOG_INFO("dbz3 iw CHRFADE9: consumidor dibujo actor {} intensidad {:.6f} cuadrado {:.6f} colores {:08X}/{:08X}",rol,v,v*v,REX_LOAD_U32(x+6268),REX_LOAD_U32(x+6272));
  }
}
}  // namespace dbz3::iwmini

// Pareja contrastada en los llamadores US82109D90/EU82109E90.
DBZ3_HOOK(sub_82111D58,dbz3eu_sub_82111E58) {
  const uint32_t x=ctx.r3.u32;
  orig(ctx,base);
  dbz3::iwmini::AplicarCHRFADE9(base,x);
}
