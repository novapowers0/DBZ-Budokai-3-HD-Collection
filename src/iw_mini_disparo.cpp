// dbz3 - Minijuegos de DISPARO del mapa de Dragon Mission de Infinite World (frente R13, ronda 4, 2026-10-16).
// Registrados en iwmini::RegistrarModulo (src/iw_minijuegos.h). Ver docs/14_frentes/RONDA4_PULIDO.md ("R13").
//
// Hecho: 9 (0x7D8) "Fire beams at Piccolo! Go easy on him, though!" (modulo MSN010). Del decomp EU de IW
// (E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c, Viewer_* 0x3245D0-0x327F90 y 0x32B2A0-0x32E8xx):
// DS126 (2026-10-27): 0x7D7 y 0x7D3 tenian la entrada mal puesta y el 0x7D7 ademas el escenario a 350 u de sus
// propias camaras. Las tres camaras del 0x7D7 (descriptor EU 0x49A600: 950/951/952) y las del 0x7D8/0x7DE se leen
// del #AMC 915/2 y cada una se usa en el estado que le toca; ver las constantes kOjo7/kMira7 y Empezar.
//  - Primera persona: apuntar = girar la camara (Camera_OrbitInputUpdate 0x3245D0): 0.0165 rad por cuadro (0.01 desde
//    el 3.er disparo), zona muerta 32 de la palanca o la cruceta; inclinacion entre 20 y 160 grados. Reticula en el
//    centro (DATA_USA 9/1 t01), que cambia de dibujo con un hueso de Piccolo dentro (10 px + su radio en pantalla).
//  - Disparo (bit 0x2000, circulo = B): rayo de una mano y luego de la otra, hacia el punto a 60 delante de la camara,
//    a 8 por cuadro; acierta a distancia <= 1 + radio del hueso; falla a mas de 80 de la camara (s_HBCORE_MAIN).
//  - Piccolo orbita a 60 + 10 sin(fase) (fase + 0.1 por cuadro) y +-45 grados; esquiva de lado (direccion 0..3 al azar,
//    distinta de la anterior; 2.0, o 2.25 con vida < 40 %); con vida < 25 % se queda quieto.
//  - Vida por dificultad {900, 950, 1000, 1050, 1100, 1200}; dano por grupo de huesos (tabla 0x499A60): cabeza 40,
//    tronco 3, brazos 12, piernas 10. A 0: derribo. Tiempo {180, 160, 140, 120, 110, 90} s (el "130" de la captura).
//  - Rango por segundos jugados (Menu_ClassifyValueByThreshold, tabla 999/75/60/45/30): <= 30 Z (10000),
//    <= 45 S (8000), <= 60 A (6000), <= 75 B (5000), si no C (3000); x {0.6, 0.8, 1.0, 1.3, 1.6, 2.0} por dificultad,
//    redondeado a la centena. El rango/premio sale del reloj tambien sin derribo (Viewer_StoreResultToConfig 0x324C50
//    y, en el 0x7D7, Viewer_CommitShootResult 0x3496B0); 100 solo si el valor cae fuera de la tabla (DS152).
// En el motor de B3: combate propio (hub::RequestBattleEx) con Piccolo de B3 (el rival, muneco quieto) y el jugador
// oculto (primera persona); las manos son los modelos de IW ITEMB04/ITEMB05 (DATA_CMN 892) y el HUD el de IW (DATA_USA
// 9/1). Los impactos se miden contra las 22 esferas de cuerpo del Piccolo de B3 (X+1616 + 16 i; mismo orden que los
// 22 huesos de IW en 0-3: cintura, pecho, cuello, cabeza) con los radios de la tabla de IW.
//
// Hecho tambien: 8 (0x7D7) "Attack Frieza with Destructo-Discs!" (MSN009; reglas en CuadroDisco) y 15 (0x7DE) "Tien!
// Stop Cell!" (MgShoot_* + la IA de Cell; reglas en Cuadro15). Escenarios: los de B3 que IW comparte (Namek 7 para el 8 y
// el 9, Archipielago 2 para el 15).
//
// DS123 (2026-10-27): cinematica de entrada, camara animada y cierre de los 4. Las camaras son clips REALES del #AMC
// 915/2, no reconstrucciones: Viewer_StateIntro 0x325c30 (1000) y Viewer_StateActive 0x324e30 (1001/1002) en el 9;
// MgShoot_St_PreIntro 0x30b410 (1300), St_Intro 0x30b1c0 (1303) y St_GameOver 0x30a3d0 (1302/1301) en el 15; y el
// descriptor EU 0x49A600 (950/951/952) en el 8. DS126 (2026-10-27, prueba del coordinador): el 8 usaba el 952 como
// entrada y tenia a los luchadores en (0,142.57,355.57), pero sus TRES clips estan en y ~ 72-82, z ~ 0-30: la
// entrada es el 950, su ultimo cuadro es la camara de juego y el jugador va a sus pies. El 1003 del 9 y el #AMC
// 890/5 del 3 estan en otro sitio (y=77 / casa de Kaio) y no se reproducen. En el 15 se cruzan 1301/1302 (sus
// alturas lo dicen). El 3 lee de su bloque del #MSS el ojo/objetivo/fov/giro (+0x40/+0x4c/+0x58/+0x5c).
// DBZ3_IWMINI_SIN_CINE=1 vuelve al comportamiento anterior (listo de 100 cuadros) para comparar.
#include "modo_original.h"
#include "iw_amc.h"
#include "iw_cutscene.h"
#include <rex/cvar.h>
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
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "reloj_prueba.h"
#include "generated/dbz3_init.h"
#include "anim_iw.h"
#include "iw_camaras_gp2.h"
#define IWV_SOLO_DECL
#include "iw_mips_vuelo.inl"   // tanda3 iw-vuelo: mgfly (7D3 con la logica original)
#undef IWV_SOLO_DECL
#include "cine.h"
#include "combate.h"
#include "escenarios.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "iw_ame.h"
#include "iw_minijuegos.h"
#include "launcher/launcher_state.h"
#include "pasarela.h"
#include "pasarela_iw.h"
#include "ps2_modelo.h"
#include "suelo_iw.h"
#include "iw_shoot_dialogo.h"
#include "iw_azar.h"
#include "iw_mips.h"
#include "iw_piccolo_reaccion.h"

REXCVAR_DEFINE_BOOL(dbz3_iw_amc_unico, true, "DBZ3/Mods",
                    "Disparo IW: camara de los clips #AMC con el reproductor unico y fiel al original (iw_amc.h). Apagado = copia antigua (interruptor de vuelta atras)");

namespace dbz3::hub {
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
}
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3

namespace dbz3::iwdisparo {
namespace {

namespace P = dbz3::pasarela;
namespace S = dbz3::ingame::style;
using ps2::V3;

constexpr dbz3::GA kMgrPtr = 0x823D6CF4, kPads = 0x8238B780;
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kFin = 35569, kVida = 6080, kVidaMax = 6084;
constexpr uint32_t kOcultoA = 3261, kOcultoB = 4925, kPadIdx = 5028;
constexpr uint32_t kMiradaA = 6062, kMiradaB = 6074;   // iw_mini_vuelo: X+6062/+6074 = angulo - 15759/-15968
constexpr uint32_t kCopias[3] = {48, 64, 80};
constexpr int kTrajeArmaduraKrilin = 2;   // tabla char96 de B3: Krilin fid 327 gi, 328 camiseta roja, 329 armadura de Namek
constexpr int kKrilin = 10, kPiccolo = 11, kTen = 12, kFreezer = 27, kCell = 33;
constexpr int kStageTown = 3, kStageNamek = 7;  // Stage_BeginLoad ID2 -> TOWN; ID7 loads DATA_CMN 64 (Namek, B3 7)
constexpr uint16_t kDisparo = 0x2000;                      // registro +12: circulo (B)
constexpr uint16_t kArriba = 0x10, kDerecha = 0x20, kAbajo = 0x40, kIzquierda = 0x80;   // +108

// MSN010 (Viewer_InitParamTables y tablas del ELF; ver cabecera)
constexpr int kVidaPicc[6] = {900, 950, 1000, 1050, 1100, 1200};
constexpr int kLimite[6] = {180, 160, 140, 120, 110, 90};
constexpr float kMult[6] = {0.6f, 0.8f, 1.0f, 1.3f, 1.6f, 2.0f};
constexpr int kBase[5] = {3000, 5000, 6000, 8000, 10000};   // rango 0 (C) .. 4 (Z)
constexpr float kUmbral[5] = {999, 75, 60, 45, 30};
// Huesos de IW (0x499A60): grupo y radio; dano por grupo
constexpr int kGrupo[22] = {1, 1, 0, 0, 4, 5, 4, 5, 2, 3, 2, 3, 4, 5, 2, 3, 2, 3, 1, 0, 1, 0};
constexpr float kRadioHueso[22] = {2.75f, 1.65f, 1.1f, 1.1f, 2.75f, 2.75f, 2.2f, 2.2f, 1.65f, 1.65f, 1.65f,
                                   1.65f, 2.2f,  2.2f,  1.65f, 1.65f, 1.76f, 1.76f, 2.2f, 2.53f, 2.2f, 2.53f};
constexpr int kDano[6] = {40, 3, 12, 12, 10, 10};
constexpr float kOrbita = 60.0f, kRayoVel = 8.0f, kRayoMax = 80.0f, kRayoMira = 60.0f;
// kOjo y kElevMax son el encuadre del original (PINE: ojo (0,150,37) y Piccolo a 148; ver abajo), no constantes de IW.
// Las manos las mueve Viewer_UpdateMain (0x32C7E0): +0xA0 estado del brazo 0..4, +0xA4 mano (0/1), +0x90/+0x94 angulo y
// pulso de cada mano (Viewer_GetParam989C por cuadro) y +0x140 contador de cuadros que se reinicia con la constante
// 12.0 (0x4028000000000000). El port no tiene ese motor de brazos: alterna una mano por disparo y las deja ocupadas
// kCadencia = 18 cuadros (aproximacion; [VERIFICAR] contra el original). Los limites de Piccolo estan en
// Viewer_UpdatePoseRotationInput (0x32DC80): +-0.7853982 en el giro y velocidad que decae 1/70 por cuadro; el port
// clampa az a +-0.7853982 y la elevacion a [-0.05, 0.35] por el suelo del campo. [VERIFICAR] la elevacion.
constexpr float kOjo = 9.0f, kCadencia = 18.0f, kElevMax = 0.35f;
// Camera_OrbitInputUpdate de IW (PCSX2): en 9 (rayos a Piccolo) el ojo esta a 150 (la altura de la camara del original;
// el objeto del jugador esta en (0,0,0) y Piccolo vuela a ~148); en 8 (Kienzan a Freezer) es tercera persona: el
// (DS126: el 8 NO esta en 142,57/355,57; sus clips 950/951/952 lo ponen en y ~ 72-82, z ~ 0-30. Ver kAltJug7.)
// 0x7D8/9: PINE EU en partida: ojo (0,150,37), objetivo (0,150,-1), fov 0.7854, #AMC 0; Piccolo inicia en (0,150,-23).
constexpr float kAltIW = 150.0f, kOjoZ9 = 37.0f, kRadioPicc9 = 23.0f;
// DS126: 0x7D7 (MSN009, descriptor EU 0x49A600 = tabla 0x473310[8]). Sus TRES camaras estan declaradas
// en el propio descriptor, campos +0x88/+0x8C/+0x90 = 950/951/952 del #AMC general DATA_CMN 915/2. Leidas
// del fichero, las tres giran sobre el jugador en (x ~ 0, y ~ 72-82, z ~ 0-30), NO en (0,142.57,355.57):
//   950: 520 c., ojo (-4.8, 79.4, 8.4) -> (-0.0, 78.5, 24.4) / objetivo (-1.1, 78.3, 2.3) -> (0, 77.3, 3.6)
//        = ENTRADA (retroceso desde cerca hasta la camara de juego).
//   951: 240 c., ojo (7.1, 74.5, 16.1) -> (-0.2, 75.3, 18.8) / objetivo (2.0, 72.5, 4.6) = mira [H].
//   952: 240 c., ojo (0, 81.6, 30.0) -> (-0.1, 81.5, 20.5) / objetivo (0, 81.6, 0) = cierre [H].
// (DS123 uso 952 como entrada; no lo es y ademas dejo a los luchadores a 350 u de donde estan esos clips.)
// El jugador va a los pies: el objetivo del clip esta 6 por encima (CameraFocusWhileFlying, EU 0x339AA0).
constexpr float kAltJug7 = 142.57f, kZ7 = 3.567f;  // 31/10: PINE: en juego Krilin esta a y 142,57 (en los clips, en el origen); antes   // pies (objetivo del clip 950 - 6)
// Ultimo cuadro del 950 tal cual (ojo (0, 78.548, 24.427) -> mira (0, 77.302, 3.567)); kMira7 es la referencia del
// clip y kOjo7 el respaldo de la camara del 8 mientras no se lee la cintura. DS129: la camara de juego NO usa la
// compensacion de FOV del clip (ver kSubeJ7/kDetrasJ7).
constexpr float kOjo7[3] = {0.0f, 78.548f, 24.427f}, kMira7[3] = {0.0f, 77.302f, 3.567f};
// DS129: la camara de JUEGO del 0x7D7 sale de la captura del ORIGINAL (tools/dorados/iw_0x7D7.json, PCSX2): ojo
// 20,0 u del jugador (18,57 en horizontal) y 7,43 por encima de sus PIES, mirando horizontal, fov 0,785 (el de B3:
// el 0,6024 del clip 950 es el campo de la CINEMATICA, no el del juego). Relativo a la CINTURA que se dibuja (5,76
// sobre los pies): 18,57 detras y 1,67 por encima. A 0,785 y esa distancia el encuadre es el del original (Krillin
// ~65 % del alto) y Freezer no encoge (el clip sin compensar daba 20,86 y 1,25: 11 % mas lejos).
constexpr float kSubeJ7 = 1.67f, kDetrasJ7 = 18.57f;
// 31/10 PINE en juego: Krilin (0, 142,57, 355,57), cintura y 142,36, ojo (0, 150, 337) mirando a +Z, fov 0,785, Freezer
// (-130, 150, 412). El modulo razona mirando a -Z alrededor de kZ7: al mundo se pasa girando 180 grados (T7, su propia
// inversa). Con eso el fondo es el del original; antes la camara miraba al lado contrario del escenario.
inline V3 T7(const V3& p) { return {-p.x, p.y, 359.137f - p.z}; }
constexpr int kIntro7 = 950, kMira7Clip = 951, kCierre7 = 952;   // 951/952: papel exacto [H]
// MSN009 (Viewer_InitRoundTables 0x34B800, tabla 0x5D9B44 + 0x18 r): discos, % de esquiva al apuntarle (B50), paso
// angular de la esquiva (B48); 180 s; gana con 7 aciertos; bases por banda {4000, 5000, 6000, 8000, 10000}
constexpr int kTiros[6] = {12, 12, 11, 10, 9, 8};
constexpr int kB50[6] = {45, 50, 60, 65, 70, 75};
constexpr float kB48[6] = {0.021f, 0.021f, 0.022f, 0.022f, 0.023f, 0.023f};
constexpr int kBaseDisco[5] = {4000, 5000, 6000, 8000, 10000};
// 5/11 opusBB: maquina de esquiva de Freezer del original (EU Char_PoseInputTransition 0x35DF50, tracker 0x35D770,
// Char_SelectBoneScreenDirection 0x35D3D0, Char_EnterPoseState2Transform 0x35D150). B4C (ELF 0x4836A0): esquiva LARGA
// si Memory()%100 >= B4C (tirada por cuadro en reposo). Corta: B48 por cuadro frenando B48/105 hasta acabar su clip;
// larga: B48/1,5 constante durante 40+azar%80. Medido en PCSX2 (7D7, radio 150): 0,0264/0,0176 rad por campo PAL
// (= B48 x 1,2 y /1,5), freno 0,00025. Encendido por defecto (eIW6); DBZ3_IW_ESQUIVA_ORIGINAL=0 la apaga.
constexpr int kB4C[6] = {65, 65, 70, 70, 85, 85};
// eIW6 6/11: reglas medidas en PCSX2 (globales 0x5D9D60.., tools/iwx_freezer.py): freno, velocidades, tiempo, paso
// angular, tirada por cuadro y cambio de direccion iguales. Encendida; DBZ3_IW_ESQUIVA_ORIGINAL=0 = esquiva antigua.
inline bool EsquivaOriginal7() {
  static const bool v = [] { const char* e = std::getenv("DBZ3_IW_ESQUIVA_ORIGINAL"); return !(e && *e == '0'); }();
  return v;
}
// 0x38E..0x395 -> alias B3 (anim_iw.cpp kExtras; 0x391 = 0xFAC, 0x393 = 0xFB9).
constexpr uint32_t kEsquivaFz[8] = {0xFD0, 0xFD1, 0xFD2, 0xFAC, 0xFD3, 0xFB9, 0xFD4, 0xFD5};
// MSN016 (Scene_InitDefaultParams EU 0x30D150; tablas USA 0x47D860 + 0x20 k, EU 0x47E010): tiempo, energia inicial 1,
// divisor del rango 1, campo +0xC = 3 (Cell corriendo, por cuadro), +0x10 = subida de Cell por cuadro, multiplicador;
// puntos por rango 0x5D9392; altura de Ten = 400 (controlador +0x84).
constexpr int kTiempo15[6] = {40, 60, 80, 100, 120, 160};
constexpr float kSubida15[6] = {3.0f, 3.0f, 3.5f, 3.5f, 4.0f, 4.0f};
constexpr int kBase15[5] = {3600, 4200, 4800, 5400, 6000};
constexpr float kAltura15 = 400.0f;
inline float Env15(const char* n, float def) { const char* e = std::getenv(n); return e && *e ? float(std::atof(e)) : def; }
// eIW2: manos de Ten con el sombreado de IW (3 tonos de la rampa 895/2 sobre la piel; la luz como la de B3). Apagado por defecto:
// DBZ3_IW_MANOS_TOON=1 lo enciende (y DBZ3_TEST_MANOS15=4 la pose medida).
inline bool ManosToon15() { static const bool v = std::getenv("DBZ3_IW_MANOS_TOON") != nullptr || ::dbz3::original::Arreglo("manos_toon"); return v; }
constexpr float kEsc3 = 1.0f;   // 3: unidades de IW = las de B3 (el campo 897 de R8 va a escala 1)
// R17: camara del 3 medida en el original con PCSX2 (struct del modulo de disparo, *(0x49CEAC)+0x660): ojo (0,30,100),
// objetivo (0,35,0), fov 0,785 (coincide con la camara del #MSS 890/4). Es FIJA y en IW no se ve ningun personaje
// (galeria de tiro). El port la usaba a (0,9,0) mirando 30 delante y con Goku de espaldas: ahora es la del original; el
// jugador va OCULTO en el punto mirado (0,35,0) y la serie de comparacion toma ese punto como referencia.
constexpr float kOjo3[3] = {0.0f, 30.0f, 100.0f}, kMira3[3] = {0.0f, 35.0f, 0.0f};
constexpr int kGoku = 0, kRaditz = 18, kLlanuras = 5;
// MSN009 con sus distancias de IW: R16 (sub_82114910) ya no acerca a los luchadores durante el minijuego, asi que
// no hace falta el x0.4 que compensaba el tope de B3. Los angulos y los radios de los huesos van tal cual.
constexpr float kEsc9 = 1.0f, kRadioFz = 150.0f * kEsc9, kDiscoVel = 5.0f * kEsc9, kDiscoRadio = 5.0f * kEsc9;

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
uint8_t* Base() {
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->virtual_membase() : nullptr;
}
V3 Mas(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Por(const V3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Largo(const V3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
V3 Dir(float yaw, float polar) {   // polar desde +Y (90 grados = horizontal); yaw 0 = +Z
  return {std::sin(polar) * std::sin(yaw), std::cos(polar), std::sin(polar) * std::cos(yaw)};
}
// DS129: punto de mundo del hueso `i` de los 22 de cuerpo del luchador `x` (X+1616 + 16 i; 0 cintura, 1 pecho, 2
// cuello, 3 cabeza..., gigantes.cpp). Son los puntos que el juego saca de los huesos de la logica CADA cuadro: es
// donde se DIBUJA el muñeco, no donde esta su X+48 (B3 lo pinta con la pose -vuelo/reposo- unos 6-7 u mas arriba).
// false si el modelo aun no tiene la tabla (X+2996) o el punto es nulo.
bool HuesoMundo(uint8_t* base, uint32_t x, int i, V3& p) {
  if (!REX_LOAD_U32(x + 2996) && !REX_LOAD_U32(x + 2952)) return false;   // sin modelo/tabla de cuerpo
  const uint32_t a = x + 1616 + 16 * uint32_t(i);
  p = {LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)};
  return std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z) > 1e-3f;
}

// ---- Efectos de IW (#AME v2): reproductor en iw_ame.h
namespace ame = dbz3::iwame::ame;

// Clip de camara de un #AMC (mismo formato que iw_minijuegos.cpp/iw_mini_vuelo.cpp):
// cabecera +0x10 = nº clips, +0x14 = tabla de 16 B por clip; clip +8 = cuadros, +12 = offset.
// Pistas: 0 ojo, 1 objetivo, 2 giro, 3 fov. Claves de 16 B {cuadro, x, y, z} o de 8 B {cuadro, valor}.
// DS123: las camaras de entrada/cierre de los 4 disparos son clips reales del #AMC general (DATA_CMN 915/2).
// DS126: el 3 NO usa el #AMC 890/5 como entrada (es la casa de Kaio, fuera del campo): usa la camara del #MSS.
struct ClipCam {
  int dur = 0;
  float fov = 0.0f;   // pista 3 (radianes); 0 si el clip no la trae
  std::vector<uint32_t> ojo_c, obj_c;
  std::vector<std::array<float, 3>> ojo, obj;
  dbz3::iwamc::Clip uni;   // dia 1A: el mismo clip leido por iw_amc.h (se usa con dbz3_iw_amc_unico)
};
uint32_t U32v(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
  return v;
}
float F32v(const std::vector<uint8_t>& b, size_t o) {
  const uint32_t u = U32v(b, o);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
// EU 0x3691a0 DestroyTheBricksMinigame (890/5) / EU 0x325c30 Viewer_StateIntro, 0x325ab0 Viewer_StateCountdown,
// 0x324e30 Viewer_StateActive (915/2, ids 1000..1003) / EU 0x30b410 MgShoot_St_PreIntro, 0x30b1c0 MgShoot_St_Intro,
// 0x30a3d0 MgShoot_St_GameOver (915/2, ids 1300..1303): lee del #AMC los clips que usa cada estado.
void LeerClipsDisparo(const std::vector<uint8_t>& amc, const std::vector<int>& ids, std::map<int, ClipCam>& out) {
  if (amc.size() < 0x20 || std::memcmp(amc.data(), "#AMC", 4) != 0) return;
  const uint32_t n = U32v(amc, 0x10), tab = U32v(amc, 0x14);
  for (int id : ids) {
    if (id < 0 || uint32_t(id) >= n || out.count(id)) continue;
    const size_t e = size_t(tab) + 16 * size_t(id);
    ClipCam c;
    c.dur = int(U32v(amc, e + 8));
    const size_t off = U32v(amc, e + 12);
    if (c.dur <= 0 || off + 16 > amc.size()) continue;
    for (int pista = 0; pista < 4; ++pista) {
      const size_t t = U32v(amc, off + 4 * size_t(pista));
      if (t + 12 > amc.size()) continue;
      const uint32_t nk = U32v(amc, t + 8);
      if (nk == 0 || nk > 5000) continue;
      if (pista < 2) {
        auto& dst = pista == 0 ? c.ojo : c.obj;
        auto& marcos = pista == 0 ? c.ojo_c : c.obj_c;
        for (uint32_t k = 0; k < nk; ++k) {
          const size_t o = t + 12 + 16 * size_t(k);
          if (o + 16 > amc.size()) break;
          uint32_t cuadro = U32v(amc, o);
          if (cuadro == 0) cuadro = uint32_t(c.dur);   // dur mod dur
          dst.push_back({F32v(amc, o + 4), F32v(amc, o + 8), F32v(amc, o + 12)});
          marcos.push_back(cuadro);
        }
      } else if (pista == 3) {
        const size_t o = t + 12;
        if (o + 8 <= amc.size()) c.fov = F32v(amc, o + 4);
      }
    }
    dbz3::iwamc::Leer(amc, id, c.uni);
    if (!c.ojo.empty() && !c.obj.empty()) out[id] = std::move(c);
  }
}

struct Datos {
  int clave = 0, fila = 0, dif = 2;
  int juego = 9;                       // 9: MSN010 (Piccolo), 8: MSN009 (Freezer y los Kienzan), 15: MSN016
  ps2::Modelo mano_i, mano_d;          // DATA_CMN 892/1 ITEMB04 y 892/3 ITEMB05 (texturas 892/0 y 892/2)
  // Proyectil, modelo de IW: el rayo rosa de Freezer (DATA_CMN 842/7 BEAM + CORE, texturas 842/8) o el Kienzan
  // (851/6 KIENCORE, texturas 851/2). El minijuego los pinta con los #AME del paquete 935 (Effect_OpenBundleForMode
  // 0x348110, modos 15/9/8/3): verificado en DATA_CMN.AFS: 935/0 #AMT (atlas), 935/1 #AMO (mallas MODEL_L00/L01/L03_EFT)
  // y 935/3..935/23 = 21 #AME; el port los reproduce con iw_ame.h y ademas deja el modelo de la tecnica como respaldo.
  ps2::Modelo efecto;
  // 3 (ladrillos): el bloque del #MSS 890/4 de su dificultad
  std::vector<std::array<float, 7>> tramos;
  int n_tramos = 0, umbral[5] = {}, puntos[5] = {}, fallo = 100;
  std::vector<uint8_t> mss_raw;   // T3: #MSS 890/4 crudo y bloque elegido (logica original, dbz3_iw_7d3_original)
  int mss_bdif = 0;
  float lad_s = 18.0f;   // #MSS bloque +0x08: tamano que se proyecta para el radio de fijado (18/18/18/10/6)
  float lad_duracion = 115.0f, lad_vel_mira = 10.0f;
  // EU 0x367e30 MgFly_StateIntroSetup: camara del bloque del #MSS (+0x40 ojo, +0x4c objetivo, +0x58 fov y +0x5c giro,
  // en grados). Son los mismos valores que se midieron a mano (0,30,100)->(0,35,0) y 45; ahora salen del fichero.
  float mss_ojo[3] = {kOjo3[0], kOjo3[1], kOjo3[2]}, mss_obj[3] = {kMira3[0], kMira3[1], kMira3[2]};
  // PINE EU49C84C/850 y4AB64C en SHOOT: lienzo600x450 y focal512.
  // La focal logica del visor no se deduce del angulo de la camara3D.
  float mss_fov = 0.7853982f, mss_giro = 0.0f, lad_focal = 512.0f;
  ps2::Modelo ladrillo;
  mensaje::Datos mensajes3;
  // DS123: clips de camara de la entrada y del cierre por indice de clip (ver ClipCam). El orden de la entrada y el
  // clip de cada desenlace salen de las maquinas de estado citadas en LeerClipsDisparo.
  std::map<int, ClipCam> cams;
  std::vector<int> intro, fin_bien, fin_mal;
  int intro_dur = 0, fin_dur = 0;
  // efectos de IW: el paquete de los minijuegos de disparo (DATA_CMN 935 = 0x3A7, Effect_OpenBundleForMode; en el 3 su
  // copia 890/6): atlas (hijo 0) y los #AME (hijos 3..23) por indice de hijo
  std::vector<P::Imagen> atlas;
  std::vector<ame::Efecto> efectos;
  std::vector<P::Imagen> hud;          // DATA_USA 9/1 (HUD de los minijuegos de disparo)
  P::Imagen hit;                       // DATA_USA 5/1 t07 "HIT!" (UI_SetObjectField158AndEnable(0, 11, 5) en Viewer_HitSparkUpdate)
  anim::ama::Obra hit_ama;
  std::vector<P::Imagen> avisos3_atlas;
  int hit_cuadros=0;
  P::Imagen clear;                     // DATA_USA 334/3 t69 "CLEAR" (el mismo final que los de P4)
};
std::mutex g_mu;
std::shared_ptr<Datos> g_d;
std::atomic<bool> g_on{false}, g_fin_pedido{false};
// eANI2: numero de partida. `hecho_` comparaba solo la direccion de Datos: make_shared reutiliza la de la partida
// anterior (g_d.reset() al acabar) y la segunda partida no se preparaba (fase_ kFinal: acababa al instante).
std::atomic<uint32_t> g_partida{0};
iwmini::Resultado g_res;
bool g_auto = false;   // prueba: apunta y dispara solo
// hilo del juego <-> interfaz
std::atomic<float> g_yaw{0}, g_polar{1.5708f};
std::atomic<float> g_px{0}, g_py{kOjo}, g_pz{kOrbita};
std::atomic<uint32_t> g_mando{0};   // +12 (16 bits) | +108 << 16
std::atomic<int> g_palanca{0};      // +26 | +27 << 8
std::atomic<bool> g_suelo{false};   // derribado: Piccolo cae (ya no se le sostiene en el aire)
// DS129: camara del 8 (0x7D7) anclada al cuerpo DIBUJADO del jugador. El encuadre es el de la camara de juego del
// original (kSubeJ7/kDetrasJ7, tools/dorados/iw_0x7D7.json) en coordenadas ABSOLUTAS de IW, pero B3 dibuja al muñeco
// con su pose (vuelo/reposo) unos 7 u por encima de X+48: con la camara absoluta quedaba a la altura de sus pies
// (prueba del coordinador 2026-10-27: las piernas tapaban la pantalla). Se ancla la mira a la cintura que se dibuja
// (X+1616) y el ojo conserva el desfase. Las escribe FotogramaDisparo (hilo del juego).
std::atomic<float> g_o8x{0.0f}, g_o8y{kOjo7[1]}, g_o8z{kOjo7[2]};    // ojo del 8
std::atomic<float> g_m8x{0.0f}, g_m8y{kMira7[1]}, g_m8z{kMira7[2]};  // mira del 8
// MSN016: camara libre (ojo y mirada), sitio del jugador oculto y el luchador de Cell (para anim::Corredor)
std::atomic<bool> g_cam15{false};
std::atomic<int> g_juego{9};   // el que corre (3: el jugador se ve y el rival se oculta)
std::atomic<float> g_ex{0}, g_ey{0}, g_ez{0}, g_tx{0}, g_ty{0}, g_tz{0}, g_jx{0}, g_jz{0};
// DS123: camara de un clip de IW (entrada/cierre) que manda sobre la del port mientras dura el clip.
std::atomic<bool> g_camclip{false};
std::atomic<uint32_t> g_x1{0};
// 31/10: entradas y finales (PINE en PCSX2): los dos actores en el origen con matriz identidad y sus codigos propios;
// AMM y AMC son un par en coordenadas absolutas. Bits: 4 activo, 1 se ve A, 2 se ve B.
std::atomic<int> g_cine{0};
std::atomic<float> g_fov_clip{0};   // pista FOV del clip en curso: se publica tal cual en la camara activa (DS177)

// Hilo de la interfaz: logica a 60 por segundo y dibujo.
class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::shared_ptr<Datos> d;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      d = g_d;
    }
    if (!g_on.load() || !d) return;
    if (d.get() != hecho_ || g_partida.load() != partida_) Preparar(*d);
    uint8_t* base = Base();
    const int st = P::EstadoCombate();
    if (!base || st < 3 || st > 8) return;   // iw_fix: hasta el 8 (el menu azul de B3, ~1,7 s)
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (!mgr) return;
    const bool pausa = dbz3::GameDimmed() || dbz3::OverlayOpen();
    if (st == 4 && !pausa) {
      acc_ += dbz3::RelojDt(std::min(io.DeltaTime, 0.1f));
      while (acc_ >= 1.0f / 60.0f) {
        acc_ -= 1.0f / 60.0f;
        if (fase_ == kFinal && res_.clave) {
          CuadroRecompensa(base, mgr);
          continue;
        }
        Efectos();
        if (d->juego == 3) Cuadro3(base, mgr, *d);
        else if (d->juego == 15) Cuadro15(base, mgr, *d);
        else if (d->juego == 8) CuadroDisco(base, mgr, *d);
        else Cuadro(base, mgr, *d);
      }
    }
    if (pausa) return;
    Dibujar(io, base, mgr, *d);
  }

 private:
  struct Rayo {
    V3 p, v;
    bool vivo = true;
    std::shared_ptr<ame::Instancia> nucleo;   // s_HBCORE_MAIN: Effect 0xF (935/13) que va con el rayo
  };
  enum Fase { kListo, kJuego, kDerribo, kFinal };

  // EU 0x3104B0 CameraCtrl_LoadModel
  void Preparar(const Datos& d) {
    hecho_ = &d;
    partida_ = g_partida.load();
    tx_i_ = ps2::Texturas(d.mano_i);
    ramp15_.clear();
    if (d.juego == 15)   // eIW2: la rampa de piel (64 x 64, azulada: se resta al blanco) de las manos de Ten
      for (const P::Imagen& im : d.mano_i.tex) {
        if (im.w != 64 || im.h != 64 || im.rgba.size() < 64 * 4) continue;
        long dif = 0;
        for (int i = 0; i < 64; ++i) dif += long(im.rgba[size_t(4 * i) + 2]) - long(im.rgba[size_t(4 * i)]);
        if (dif / 64 < 40) continue;
        for (int i = 0; i < 64; ++i)
          ramp15_.push_back(IM_COL32(255 - im.rgba[size_t(4 * i)], 255 - im.rgba[size_t(4 * i) + 1], 255 - im.rgba[size_t(4 * i) + 2], 255));
        break;
      }
    tx_ef_ = ps2::Texturas(d.efecto);
    tx_lad_ = ps2::Texturas(d.ladrillo);
    tx_atlas_.clear();
    tx_suma_.clear();
    for (const P::Imagen& im : d.atlas) {
      tx_atlas_.push_back(ps2::Crear(im));
      P::Imagen su = im;   // suma: alfa = el canal mas brillante (el negro no suma nada)
      for (size_t i = 0; i + 3 < su.rgba.size(); i += 4)
        su.rgba[i + 3] = uint8_t(std::max({su.rgba[i], su.rgba[i + 1], su.rgba[i + 2]}) * su.rgba[i + 3] / 255);
      tx_suma_.push_back(ps2::Crear(su));
    }
    efx_.clear();
    aura_.reset();
    datos_ = &d;
    giro_disco_ = 0;
    // DS123: entrada y cierre con los clips del #AMC; DBZ3_IWMINI_SIN_CINE=1 los deja como estaban (para comparar).
    sin_cine_ = std::getenv("DBZ3_IWMINI_SIN_CINE") != nullptr;
    fin_id_ = -1;
    fin_dur_ = 140;
    g_camclip = false;
    tx_d_ = ps2::Texturas(d.mano_d);
    hud_.clear();
    for (const P::Imagen& im : d.hud) hud_.push_back(ps2::Crear(im));
    tx_hit_ = ps2::Crear(d.hit);
    clear_ = ps2::Crear(d.clear);
    tx_avisos3_.clear();for(const auto& im:d.avisos3_atlas)tx_avisos3_.push_back(ps2::Crear(im));
    juego_ = d.juego;
    res_ = iwmini::Resultado{};
    t_fin_ = 0.0;
    recompensa_ = {};
    recompensa_antes_ = 0;
    recompensa_hitos_ = 0;
    // EU 0x327590 s_MSN010_UPDATE
    vida_ = vida_max_ = kVidaPicc[d.dif];
    pc_dif_ = d.dif; pc_estado_ = 1; pc_campo_ = 0; pc_semilla_ = pc_clip_ = pc_clip_rep_ = 0; pc_vueltas_ = ~0u;   // dia1E4
    cell_semilla_ = 0;   // N2
    pc_az_p_ = 1e9f; std::memset(pc_glob_, 0, sizeof pc_glob_); pc_d_ok_ = false;
    // P1 (revision 7/11): la esquiva de Freezer (0x7D7) tambien empieza de cero en cada partida (antes se arrastraba la
    // de la partida anterior: fz_clip_rep_ viejo podia impedir el fin de reposo toda la partida). = valores de la clase.
    fz_estado_ = 1; fz_dir_ = -1; fz_larga_ = fz_preparada_ = false; fz_vel_ = fz_t_ = 0; fz_vueltas_ = ~0u;
    fz_clip_ = fz_clip_rep_ = 0; fz_traza_ = 0; fz_semilla_ = fz_dir_prev_ = 0; fz_campo_ = 0; fz_az_o_ = 0; fz_az_p_ = 1e9f;
    fz_interp_ = fz_ant_ok_ = false; fz_az_ant_ = fz_el_ant_ = 0; esquiva_dur_ = esquiva_vivida_ = 0;
    restante_ = float(kLimite[d.dif]);
    fase_ = kListo;
    t_ = 0;
    acc_ = 0;
    disparos_ = 0;
    mano_ = 0;
    espera_[0] = espera_[1] = 0;
    retroceso_[0] = retroceso_[1] = 0;
    rayos_.clear();
    chispas_.clear();
    az_ = 0;
    elev_ = d.juego == 9 ? 0.0f : 0.12f;
    picc_fase_ = 0;
    esquiva_ = -1;
    esquiva_t_ = 60;
    reaccion_piccolo_.Reiniciar();
    probar_reaccion_piccolo_ = d.juego == 9 && std::getenv("DBZ3_TEST_IWMINI") &&
        std::getenv("DBZ3_TEST_IWX_PICCOLO_REACCION");
    probar_acuse_piccolo_ = probar_reaccion_piccolo_ &&
        std::getenv("DBZ3_TEST_IWX_PICCOLO_ACUSE");
    rol_piccolo_prueba_ = 1;
    if (probar_reaccion_piccolo_) {
      if (const char* rol = std::getenv("DBZ3_TEST_IWX_PICCOLO_PUNTO")) {
        char* fin = nullptr;
        const long valor = std::strtol(rol, &fin, 10);
        if (fin != rol && *fin == '\0' && valor >= 0 && valor < 22)
          rol_piccolo_prueba_ = int(valor);
      }
    }
    if (probar_reaccion_piccolo_)
      REXLOG_INFO("dbz3 solIWX: reaccion Piccolo candidata; normal=false, scheduler y efectos integrales pendientes");
    if (probar_acuse_piccolo_)
      REXLOG_INFO("dbz3 solIWX: Piccolo acuse candidato; normal=false, identidad tras reinicio A/B");
    semilla_ = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());

    g_yaw = d.juego == 9 ? 3.14159265f : 0.0f;
    g_polar = d.juego == 9 ? 1.57079633f : 1.5708f - 0.06f;
    g_suelo = false;
    auto_t_ = 0;
    g_cam15 = d.juego == 15 || d.juego == 3;
    g_juego = d.juego;
    if (d.juego == 3) {   // camara del #MSS: (0, 30, 100) -> (0, 35, 0)
      tiros_ = 7;
      aciertos_ = 0;
      quedan_ = d.n_tramos;
      lad_s_ = d.lad_s;
      mira_x_ = 300, mira_y_ = 225;
      tramos_disponibles_ = 0;
      orig3_ = -1; acum3_ = 0; held3_ = 0; sem3_ = 0; aviso_fin3_ = false;
      mensaje3_.Reiniciar();bloqueo_tiro3_=false;tramo_preparado3_=false;cargar_postmensaje3_=false;fin3_inicio_=-1;
      tramo_espera_ = 0;
      ladrillo_vivo_ = fijado_ = false;
      antes15_ = 0;
      // EU 0x367e30: la camara del #MSS del bloque de dificultad (antes iba a mano)
      g_ex = d.mss_ojo[0], g_ey = d.mss_ojo[1], g_ez = d.mss_ojo[2];
      g_tx = d.mss_obj[0], g_ty = d.mss_obj[1], g_tz = d.mss_obj[2];
      g_jx = 0, g_jz = 0;
      g_px = 0, g_py = 0, g_pz = -40;   // el rival (oculto) delante: Goku mira hacia los ladrillos
    }
    if (d.juego == 15) {   // MSN016: Cell en (0, 0, 0) (0x5D9320), Ten en (0, 400, 200), energia 1
      dif15_ = d.dif;
      restante_ = float(kTiempo15[d.dif]);
      energia_ = 1.0f;
      baja_usada_ = false;
      sub15_ = t15_ = kikoho_ = 0;
      mx_ = 0;
      mz_ = 200;
      cx_ = cy_ = cz_ = cvy_ = 0;
      cst_ = 0;
      cc0_ = ce_t_ = 0;
      golpe_y_ = 0;
      rumbo_ = 0;
      p15_ = -1;
      antes15_ = 0;
      Camara15();
    }
    if (d.juego == 8) {   // MSN009: 180 s, N discos; Freezer al frente (yaw pi/2, pitch 0 de IW)
      restante_ = 180.0f;
      tiros_ = kTiros[d.dif];
      aciertos_ = fallos_seguidos_ = 0;
      disco_ = 0;
      pausa_ = 0;
      esquiva_t_ = 0;
      tirada_ = false;
      elev_ = 0.1f;
      // DS126: la mira arranca horizontal y hacia -Z, que es hacia donde mira el clip 950 y donde queda
      // Freezer (delante del jugador). Antes arrancaba inclinada y el piloto se iba al suelo.
      g_yaw = 3.14159265f;
      g_polar = 1.57079633f;
    }
  }
  uint32_t Azar() {
    semilla_ = semilla_ * 1103515245u + 12345u;
    return semilla_ >> 8;
  }
  V3 Ojo() const {
    if (juego_ == 8)   // DS129: ojo del 0x7D7 anclado al cuerpo dibujado (lo escribe FotogramaDisparo)
      return {g_o8x.load(), g_o8y.load(), g_o8z.load()};
    if (juego_ == 9) return {0, kAltIW, kOjoZ9};   // PINE/#AMC: ojo (0,150,37), objetivo (0,150,-1)
    if (juego_ == 3 && datos_)   // EU 0x367e30: la camara fija del #MSS (bloque +0x40/+0x4c)
      return {datos_->mss_ojo[0], datos_->mss_ojo[1], datos_->mss_ojo[2]};
    return {0, kOjo, 0};
  }

  // ---- DS123: camaras de los clips del #AMC (entrada y cierre) ----
  static void Mezcla(float a[3], const float b[3], float t) {
    for (int i = 0; i < 3; ++i) a[i] = a[i] + (b[i] - a[i]) * t;
  }
  static bool MuestraPista(const std::vector<std::array<float, 3>>& k, const std::vector<uint32_t>& marcos, float f,
                           float out[3]) {
    if (k.empty() || k.size() != marcos.size()) return false;
    size_t i = 0;
    while (i + 1 < marcos.size() && float(marcos[i + 1]) <= f) ++i;
    const size_t j = std::min(i + 1, k.size() - 1);
    const float d = float(marcos[j]) - float(marcos[i]);
    const float t = d > 0 ? std::clamp((f - float(marcos[i])) / d, 0.0f, 1.0f) : 0.0f;
    for (int c = 0; c < 3; ++c) out[c] = k[i][size_t(c)] + (k[j][size_t(c)] - k[i][size_t(c)]) * t;
    return true;
  }
  // Posicion del clip en el cuadro f con el FOV de IW conservado acercando el ojo (el de B3 es 45): DS92 ya usa esta
  // compensacion en iw_minijuegos.cpp (Camera_PlayCameraAnim_, EU 0x2972D0) y en iw_mini_vuelo.cpp.
  void Cine(uint32_t mgr, int ver, uint32_t a, uint32_t b, bool b_sujeta = false, uint32_t b_luego = 0) {
    g_cine = 4 | ver;
    const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
    if (a) dbz3::anim::Reproducir(x0, a, true);
    if (b && b_sujeta) dbz3::anim::Reproducir(x1, b, true, 0, b_luego);
    else if (b) dbz3::anim::Bucle(x1, b);
    REXLOG_INFO("dbz3 iwdisparo: actores de IW: ver {} A {:#x} B {:#x} t {}", ver, a, b, t_);
  }
  bool ClipPose(const Datos& d, int id, float f, uint8_t* base, uint32_t mgr, float ojo[3], float obj[3]) const {
    const auto it = d.cams.find(id);
    if (it == d.cams.end()) return false;
    const ClipCam& c = it->second;
    const float q = std::clamp(f, 0.0f, float(std::max(c.dur - 1, 0)));
    float fov_u = 0.0f;
    if (REXCVAR_GET(dbz3_iw_amc_unico) && c.uni.nf > 0) {
      const dbz3::iwamc::Pose p = dbz3::iwamc::Muestrea(c.uni, q);
      std::copy(p.ojo, p.ojo + 3, ojo);
      std::copy(p.obj, p.obj + 3, obj);
      if (p.hay_fov) fov_u = p.fov;
    } else if (!MuestraPista(c.ojo, c.ojo_c, q, ojo) || !MuestraPista(c.obj, c.obj_c, q, obj)) return false;
    const float fj = LoadF(base, mgr + 0x1C4);   // kFov del combate
    // DS129: el 8 NO compensa: su 0,6024 es el campo de la cinematica y el de la camara de juego del original es el
    // de B3 (0,785, tools/dorados/iw_0x7D7.json). Compensarlo acercaba el ojo 5 u y dejaba el encuadre del clip, no
    // el del juego (Krillin 83 % del alto frente al 65 % del original) y rompia la continuidad con el juego.
    // 31/10: cine::Camara solo escribe una entrada que B3 mezcla: mandaba la camara nativa, por eso los clips no se
    // veian. Ahora va por CamaraMinijuego con su FOV, sin acercar el ojo.
    (void)fj;
    { const float fc = fov_u > 0.0f ? fov_u : c.fov; g_fov_clip = fc > 0.1f && fc < 1.4f ? fc : 0.0f; }
    // GP2: q/id son el reloj literal del sampler, no t_ ni el actor.
    if (dbz3::gp2cam::Activo())
      dbz3::gp2cam::Evaluada("disparo", 915, 2, 0, id, q, c.dur,
          int(fase_), g_cine.load(), ojo, obj, c.fov, 0.0f, id);
    return true;
  }
  // Camara de juego a la que vuelve el port al acabar la entrada (la misma que usan Ojo()/Camara15()).
  void CamaraBase(float ojo[3], float obj[3]) const {
    if (juego_ == 3 && datos_) {
      std::copy(datos_->mss_ojo, datos_->mss_ojo + 3, ojo);
      std::copy(datos_->mss_obj, datos_->mss_obj + 3, obj);
      return;
    }
    if (juego_ == 8) {   // DS129: camara de juego del 0x7D7 (clip 950) anclada a la cintura que se dibuja
      ojo[0] = g_o8x.load(), ojo[1] = g_o8y.load(), ojo[2] = g_o8z.load();
      obj[0] = g_m8x.load(), obj[1] = g_m8y.load(), obj[2] = g_m8z.load();
      return;
    }
    if (juego_ == 15) {
      ojo[0] = mx_, ojo[1] = kAltura15, ojo[2] = mz_;
      obj[0] = mx_, obj[1] = 15.0f, obj[2] = mz_ - 200.0f;
      return;
    }
    ojo[0] = 0.0f, ojo[1] = kAltIW, ojo[2] = kOjoZ9;
    obj[0] = 0.0f, obj[1] = kAltIW, obj[2] = kOjoZ9 - 38.0f;
  }
  void PonerCamara(const float ojo[3], const float obj[3]) {
    g_camclip = true;
    g_ex = ojo[0], g_ey = ojo[1], g_ez = ojo[2];
    g_tx = obj[0], g_ty = obj[1], g_tz = obj[2];
  }
  // EU 0x3691a0 DestroyTheBricksMinigame / EU 0x325c30 Viewer_StateIntro / EU 0x30b410 MgShoot_St_PreIntro: la entrada
  // encadena los clips del #AMC tal cual los pone el estado (los cuadros suman) y luego devuelve el mando a la camara
  // del juego. Devuelve true mientras dura la entrada (el cuadro ya esta puesto).
  bool HayCine(const Datos& d) const { return !sin_cine_ && !d.intro.empty() && d.intro_dur > 0; }
  bool PasoIntro(uint8_t* base, uint32_t mgr, const Datos& d) {
    if (!HayCine(d)) return false;
    const int f = t_ - 1;   // t_ empieza en 1 en el primer cuadro de kListo
    if (f < 0) return true;
    if (f >= d.intro_dur) {
      g_camclip = false;
      float ojo[3], obj[3];
      CamaraBase(ojo, obj);
      if (juego_ == 3) PonerCamara(ojo, obj), g_camclip = false;   // el 3 usa g_ex como camara fija
      return false;
    }
    // dia 1A: cadena de clips = lineas de una escena de Cutscene_GotoLine (iw_cutscene.h)
    std::vector<dbz3::iwcut::Linea> lineas;
    std::vector<int> ids;
    for (int c : d.intro)
      if (const auto it = d.cams.find(c); it != d.cams.end()) {
        dbz3::iwcut::Linea li;
        li.clip = c, li.cuadros = it->second.dur;
        lineas.push_back(li);
        ids.push_back(c);
      }
    int id = -1, lin = 0;
    float local = 0;
    if (dbz3::iwcut::Posicion(lineas, f, lin, local)) id = ids[size_t(lin)];
    if (id < 0) return false;
    float ojo[3], obj[3];
    if (!ClipPose(d, id, local, base, mgr, ojo, obj)) return false;
    // DS129: los clips estan en coordenadas de IW y B3 dibuja al muñeco con su pose ~6-7 u por encima de X+48. Se
    // traslada la camara del clip lo mismo que el cuerpo dibujado respecto de la mira del clip, para que el encuadre
    // relativo al muñeco sea el del original (sin esto la entrada tambien lo dejaba fuera de plano).
    if (juego_ == 8 && !(g_cine.load() & 4)) {
      const float cx = g_m8x.load() - kMira7[0], cy = (g_m8y.load() - kSubeJ7) - kMira7[1], cz = g_m8z.load() - kMira7[2];
      ojo[0] += cx, ojo[1] += cy, ojo[2] += cz;
      obj[0] += cx, obj[1] += cy, obj[2] += cz;
    }
    // Mezcla de la entrada con la camara del juego en los ultimos cuadros. DS129: el 8 tambien (su camara de juego
    // es la del original, 2,3 u mas cerca que el ultimo cuadro del clip) y el 9 encadena con la cuenta atras; el
    // 3/15 cortan al campo (sus clips acaban en otro sitio del escenario).
    const int mezcla = 0;   // 31/10: el original corta (PINE: clip hasta dur-1 y el actor salta a su sitio de juego)
    if (mezcla > 0 && f >= d.intro_dur - mezcla) {
      float bo[3], bm[3];
      CamaraBase(bo, bm);
      Mezcla(ojo, bo, float(f - (d.intro_dur - mezcla)) / float(mezcla));
      Mezcla(obj, bm, float(f - (d.intro_dur - mezcla)) / float(mezcla));
    }
    PonerCamara(ojo, obj);
    return true;
  }
  void ElegirFin(const Datos& d, const std::vector<int>& ids) {
    fin_id_ = -1;
    fin_dur_ = 140;
    for (int id : ids)
      if (const auto it = d.cams.find(id); it != d.cams.end()) {
        fin_id_ = id;
        // EU324E30/30A3D0: espera140 y DESPUES camara hasta dur-1.
        fin_dur_ = 140 + std::max(0,it->second.dur-1);
        break;
      }
  }
  // EU 0x324e30 Viewer_StateActive / EU 0x30a3d0 MgShoot_St_GameOver: clip de cierre (1001/1002 o 1301/1302) mientras
  // dura el estado de derribo; luego el port aplica el resultado como MgFly_FinishAndApplyResult (EU 0x3675f0).
  // EU 0x324e30 Viewer_StateActive
  // EU 0x30a3d0 MgShoot_St_GameOver
  void PasoFinal(uint8_t* base, uint32_t mgr, const Datos& d) {
    if (sin_cine_ || fin_id_ < 0 || t_ < 140) return;
    float ojo[3], obj[3];
    if (t_==140) REXLOG_INFO("dbz3 solIW6: disparo fin espera140 clip {} duracion {}",fin_id_,fin_dur_-140+1);
    if (t_==140) {   // PINE: el codigo propio del actor a la vez que el clip
      const bool bien = !d.fin_bien.empty() && fin_id_ == d.fin_bien[0];
      if (juego_ == 8) Cine(mgr, 3, bien ? 0xFA4 : 0xFA5, 0xFAA);
      else if (juego_ == 9) Cine(mgr, 3, bien ? 0xFAE : 0xFAF, bien ? 0 : 0xFB0);
      else if (juego_ == 15) Cine(mgr, 3, bien ? 0xFB6 : 0xFB7, 0x960);
      else if (juego_ == 3) Cine(mgr, 1, bien ? 0xFA1 : 0xFA2, 0);
    }
    if (ClipPose(d, fin_id_, float(t_-140), base, mgr, ojo, obj)) PonerCamara(ojo, obj);
  }

  // Un cuadro de IW (60 por segundo).
  void Cuadro(uint8_t* base, uint32_t mgr, const Datos& d) {
    ++t_;
    if(hit_t_>0)--hit_t_; // reloj del juego, no numero de presentaciones de ImGui
    dbz3::iwmini::SerieJuego(fase_ == kJuego);
    const uint32_t m = g_mando.load();
    const uint16_t bot = uint16_t(m & 0xFFFF), dirs = uint16_t(m >> 16);
    const int pal = g_palanca.load();
    const int sx = int8_t(pal & 0xFF), sy = int8_t((pal >> 8) & 0xFF);
    // EU 0x325c30 Viewer_StateIntro (clip 1000 del #AMC 915/2; DS126: el 1003 no se reproduce, esta a y=77) y despues
    // EU 0x325840 Viewer_StateReady: 100 cuadros de espera si no hay clips.
    if (fase_ == kListo) {
      MoverPiccolo(base, mgr, false);
      if (HayCine(d)) {
        const int t0 = d.cams.count(0) ? d.cams.at(0).dur : 0;
        if (t_ == 1 && t0) Cine(mgr, 0, 0, 0);
        if (t_ == t0 + 1) Cine(mgr, 3, 0xFAD, 0xFB0);                               // PINE: 0x438 y 0x3E8 con 1000
        if (t_ == t0 + 441) Cine(mgr, 2, 0, 0xFB1, true);                           // 0x43D con 1003, sin Freezer
        if (PasoIntro(base, mgr, d)) return;
      } else if (t_ < 100) {
        return;
      }
      g_cine = 0;
      dbz3::anim::Bucle(mgr + kX0 + kXPaso, 0xFB0);
      fase_ = kJuego;
      t_ = 0;
      REXLOG_INFO("dbz3 iwdisparo: juego 0x{:X} empieza", d.clave);
      return;
    }
    if (fase_ == kJuego) {
      restante_ -= 1.0f / 60.0f;
      // camara (Camera_OrbitInputUpdate): palanca fuera de la zona muerta (32) o la cruceta (+108, palanca << 8)
      const float vel = disparos_ < 3 ? 0.0165f : 0.01f;
      float gx = 0, gy = 0;
      if (std::abs(sx) > 32 || std::abs(sy) > 32) {
        gx = float(sx) / 127.0f;
        gy = -float(sy) / 127.0f;   // arriba negativo
      } else {
        const uint16_t dd = uint16_t(dirs | (dirs >> 8));
        gx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
        gy = float((dd & kArriba) != 0) - float((dd & kAbajo) != 0);
      }
      bool fuego = (bot & kDisparo) != 0;
      if (g_auto) Piloto(base, mgr, gx, gy, fuego);
      // Opt-in de la escena de medida: solo prepara HP del desenlace; la
      // espera140, el clip y la entrega siguen el camino normal del juego.
      if (juego_==9 && t_>=120 && std::getenv("DBZ3_TEST_IWMINI")) {
        const char* final=std::getenv("DBZ3_SOLIW6_FINAL");
        if (final&&final[0]=='1'&&vida_>0) {
          REXLOG_INFO("dbz3 solIW6: escena controlada de derribo (sin prueba de punteria)");
          vida_=0;
        }
      }
      g_yaw = g_yaw.load() - gx * vel;   // derecha de la camara = -X con yaw 0 (cine::Camara)
      g_polar = std::clamp(g_polar.load() - gy * vel, 0.34906587f, 2.792527f);
      // disparo: una mano y luego la otra (+0xA4), cada una ocupada un rato
      for (float& e : espera_) e = std::max(0.0f, e - 1.0f);
      for (float& r : retroceso_) r = std::max(0.0f, r - 0.15f);
      if (fuego && espera_[mano_] <= 0) {
        const V3 ojo = Ojo(), f = Dir(g_yaw.load(), g_polar.load());
        const V3 sale = Mano(mano_, 0);
        const V3 mira = Mas(ojo, Por(f, kRayoMira));
        V3 v = Mas(mira, Por(sale, -1.0f));
        v = Por(v, kRayoVel / std::max(Largo(v), 1e-3f));
        rayos_.push_back({sale, v, true, Lanzar(13, sale, v)});
        espera_[mano_] = kCadencia;
        retroceso_[mano_] = 1.0f;
        mano_ ^= 1;
        ++disparos_;
      }
      if (juego_ == 9) for (int m = 0; m < 2; ++m) {   // EU 0x32BDD0 (-2, 0): Effect 8 (935/7) siempre en cada mano, se relanza al acabar
        if (!brillo_[m] || !brillo_[m]->vivo) brillo_[m] = Lanzar(7, Mano(m, 0), Mas(Cam({0, 0, 1}), Por(Cam({0, 0, 0}), -1.0f)));
        if (brillo_[m]) brillo_[m]->origen = Mano(m, retroceso_[m]);
      }
      Rayos(base, mgr);
      MoverPiccolo(base, mgr, vida_ < 0.25f * vida_max_);
      if (vida_ <= 0) {
        fase_ = kDerribo;
        t_ = 0;
        segundos_ = float(kLimite[d.dif]) - restante_;
        g_suelo = true;
        dbz3::anim::Reproducir(mgr + kX0 + kXPaso, 0xFB2, true, 0, 0xFB0);   // PINE: 0x410 al quedarse sin vida
        ElegirFin(d, d.fin_bien);   // EU 0x324e30 Viewer_StateActive: clip 1001 (derribo)
        REXLOG_INFO("dbz3 iwdisparo: Piccolo derribado en {:.2f} s ({} disparos)", segundos_, disparos_);
      } else if (restante_ <= 0) {
        restante_ = 0;
        fase_ = kDerribo;
        t_ = 0;
        segundos_ = 0;   // Viewer_StateInput: tiempo agotado -> +0x10 = 0 -> sin rango
        ElegirFin(d, d.fin_mal);   // clip 1002 (tiempo agotado)
        REXLOG_INFO("dbz3 iwdisparo: TIEMPO (vida de Piccolo {} de {})", vida_, vida_max_);
      }
      return;
    }
    if (fase_ == kDerribo) {   // Viewer_StateActive: el clip de cierre y el resultado
      Rayos(base, mgr);
      PasoFinal(base, mgr, d);
      if (t_ >= fin_dur_) {
        fase_ = kFinal;
        Acabar(base, mgr, d);
      }
    }
  }

  // ---- MSN009: Kienzan contra Freezer (decomp EU Viewer_State_* 0x348B40-0x34A410, Effect_StartChargedHitState
  // 0x364510, Char_PoseModeUpdateAll 0x35D8D0). Camara 0.015 rad por cuadro (0.010 cargando o con el disco en vuelo),
  // giro +-65.73 grados e inclinacion -25.73..65.73. Circulo: carga (40 cuadros) y otra vez: lanza el disco hacia el
  // punto a 150 de la camara, a 5 por cuadro; si la mira tocaba un hueso (5 + radio), se guia hacia el con un peso que
  // baja 0.04 por cuadro. Acierto: disco (5) contra hueso (5 + radio). Fallo: a mas de 150. 3 fallos seguidos: 120
  // cuadros de pausa. Gana con 7 aciertos; pierde si aciertos + discos < 7 o a los 180 s.
  // EU 0x34B3F0 s_MSN009_UPDATE
  void CuadroDisco(uint8_t* base, uint32_t mgr, const Datos& d) {
    ++t_;
    dbz3::iwmini::SerieJuego(fase_ == kJuego);
    const uint32_t m = g_mando.load();
    const uint16_t bot = uint16_t(m & 0xFFFF), dirs = uint16_t(m >> 16);
    const int pal = g_palanca.load();
    const int sx = int8_t(pal & 0xFF), sy = int8_t((pal >> 8) & 0xFF);
    // DS126: entrada con el clip 950 (520 c.) del #AMC 915/2, que es el que declara el descriptor EU 0x49A600; sin
    // clip, AimStart de 100 cuadros. El 952 NO es la entrada (es la tercera camara del modulo).
    if (fase_ == kListo) {
      MoverFreezer(base, mgr, d);
      if (HayCine(d)) {
        const int t0 = d.cams.count(0) ? d.cams.at(0).dur : 0;
        if (t_ == 1 && t0) Cine(mgr, 0, 0, 0);
        // eIW6: durante la camara 0 el original ya tiene a Krillin en su estado 8 (0x3B6, PINE n16884..17250) y a
        // Freezer en el 0/1 (Char_EnterPoseState0Idle/1 EU 0x35D330/0x35D2B0: anim 900 = 0x384). Sin esto el port
        // dejaba el clip comun 0x40004, con la mirada de B3 y la cola de Freezer 24-124 grados fuera de pista (eANI3).
        // DBZ3_IW_ESPERA_COMUN=1: como antes.
        static const bool espera_comun = std::getenv("DBZ3_IW_ESPERA_COMUN") != nullptr;
        // El cuerpo de IW se instala unos cuadros despues de t 1 (un pedido antes se pierde): se pide mientras el
        // cuerpo lea otro codigo, solo hasta la camara 950.
        if (t_ <= t0 && !espera_comun) {
          const uint32_t xs[2] = {mgr + kX0, mgr + kX0 + kXPaso}, cods[2] = {0xFA6, 0xFAA};
          for (int k = 0; k < 2; ++k) {
            dbz3::anim::EstadoCuerpo e;
            const bool leido = dbz3::anim::LeerEstadoCuerpo(base, xs[k], 0, e);
            static uint32_t visto[2] = {~0u, ~0u};
            const uint32_t firma = (e.codigo << 20) ^ e.clip;
            if (std::getenv("DBZ3_TEST_IWMINI") && (firma != visto[k] || t_ == 1)) {
              visto[k] = firma;
              REXLOG_INFO("dbz3 iwdisparo: espera t {} actor {} codigo {:X} clip {:X} accion {:X} oculto {}/{}", t_, k, e.codigo,
                          e.clip, REX_LOAD_U32(xs[k] + 2928) & 0xFFF, e.oculto1, e.oculto2);
            }
            // BucleForzado: un Bucle repetido se descarta aunque el primero no entrase; en bucle (no pose sujeta) el
            // repuesto de anim_iw devuelve el codigo cuando la accion 0 de B3 lo pisa.
            if (!leido || !e.hay || e.codigo != cods[k]) dbz3::anim::BucleForzado(xs[k], cods[k]);
          }
        }
        if (t_ == t0 + 1) Cine(mgr, 3, 0xFA3, 0xFAA);   // PINE: 0x3D4 y 0x384 con 950
        if (PasoIntro(base, mgr, d)) return;
      } else if (t_ < 100) {
        return;
      }
      g_cine = 0;
      dbz3::anim::Bucle(mgr + kX0, 0xFA6);
      dbz3::anim::Bucle(mgr + kX0 + kXPaso, 0xFAA);
      (void)::dbz3::original::FuenteIw();   // dia1O: lee el ELF aqui y no en el primer freno
      fase_ = kJuego;
      t_ = 0;
      REXLOG_INFO("dbz3 iwdisparo: juego 0x{:X} empieza", d.clave);
      return;
    }
    if (fase_ != kJuego) {
      if (disco_ == 3) DiscoVuela(base, mgr);
      if (fase_ == kDerribo) {
        PasoFinal(base, mgr, d);   // EU3498B0: espera140 y951/952 completos
        if (t_ >= fin_dur_) {      // ShootRecoil
          fase_ = kFinal;
          Acabar(base, mgr, d);
        }
      }
      return;
    }
    restante_ -= 1.0f / 60.0f;
    // Escena desechable del arnes, equivalente a los contadores de PINE;
    // no es una opcion del jugador ni modifica una partida guardada.
    if (std::getenv("DBZ3_TEST_IWMINI")&&t_==120)
      if (const char* control=std::getenv("DBZ3_SOLIW7_KIENZAN")) {
        if (control[0]=='1') aciertos_=7;
        else if (control[0]=='2') tiros_=0;
        REXLOG_INFO("dbz3 solIW7: control escena MSN009 aciertos {} discos {}",aciertos_,tiros_);
      }
    float gx = 0, gy = 0;
    if (std::abs(sx) > 32 || std::abs(sy) > 32) {
      gx = float(sx) / 127.0f;
      gy = -float(sy) / 127.0f;
    } else {
      const uint16_t dd = uint16_t(dirs | (dirs >> 8));
      gx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
      gy = float((dd & kArriba) != 0) - float((dd & kAbajo) != 0);
    }
    bool fuego = (bot & kDisparo) != 0;
    if (g_auto) Piloto(base, mgr, gx, gy, fuego);
    if (pausa_ > 0) {   // AimShootC: 120 cuadros o el boton
      --pausa_;
      if (fuego) pausa_ = 0;
      gx = gy = 0;
    }
    const float vel = disco_ == 0 ? 0.015f : 0.010f;
    // DS126: el giro de +-65,73 grados es alrededor de la MIRA (que en esta escena es -Z = pi), no de 0.
    // Con el clamp viejo, el yaw inicial pi se recortaba a 1.1472 en el primer cuadro y la mira giraba 180 grados.
    constexpr float kPi = 3.14159265f;
    g_yaw = std::clamp(g_yaw.load() - gx * vel, kPi - 1.1472f, kPi + 1.1472f);
    g_polar = std::clamp(g_polar.load() - gy * vel, 1.5708f - 1.1472f, 1.5708f + 0.44907f);
    for (float& r : retroceso_) r = std::max(0.0f, r - 0.08f);
    if (pausa_ <= 0 && fuego && disco_ == 0) {   // AimShootA: carga (pose 9)
      disco_ = 1;
      carga_ = 0;
      dbz3::anim::Reproducir(mgr + kX0, 0xFA7, true, 0, 0xFA8);   // PINE: 0x3B7 y 0x3B8 cargando
    } else if (disco_ == 1 && ++carga_ >= 40) {   // fase 0x12 a los 40: listo
      disco_ = 2;
    } else if (disco_ == 2 && fuego && !fuego_antes_) {   // AimShootB: lanza (pose 10), un disco menos
      const V3 ojo = Ojo(), f = Dir(g_yaw.load(), g_polar.load());
      const V3 mira = Mas(ojo, Por(f, kRadioFz));
      disco_p_ = Mano(1, 0);
      const V3 v = Mas(mira, Por(disco_p_, -1.0f));
      disco_v_ = Por(v, kDiscoVel / std::max(Largo(v), 1e-3f));
      bloqueo_ = HuesoCerca(base, mgr, mira, kDiscoRadio);
      combustible_ = 1.0f;
      disco_ = 3;
      --tiros_;
      dbz3::anim::Reproducir(mgr + kX0, 0xFA9, true, 0, 0xFA6);   // PINE: 0x3B9 al lanzar y vuelta a 0x3B6
      retroceso_[1] = 1.0f;
      REXLOG_INFO("dbz3 iwdisparo: Kienzan lanzado (quedan {}; mira {})", tiros_,
                  bloqueo_ >= 0 ? "en un hueso" : "libre");
    }
    fuego_antes_ = fuego;
    if (disco_ == 3) DiscoVuela(base, mgr);
    MoverFreezer(base, mgr, d);
    if (disco_ == 3) return;   // AimShootD mira el resultado cuando el disco acaba
    if (aciertos_ >= 7) {
      fase_ = kDerribo;
      t_ = 0;
      segundos_ = 180.0f - restante_;
      ElegirFin(d,d.fin_bien);
      REXLOG_INFO("dbz3 iwdisparo: 7 aciertos en {:.2f} s", segundos_);
    } else if (restante_ <= 0 || aciertos_ + tiros_ < 7) {
      restante_ = std::max(restante_, 0.0f);
      fase_ = kDerribo;
      t_ = 0;
      segundos_ = 0;   // ce78+8 = -1: sin banda
      ElegirFin(d,d.fin_mal);
      REXLOG_INFO("dbz3 iwdisparo: FALLO ({} aciertos, {} discos, {:.1f} s)", aciertos_, tiros_, restante_);
    }
  }
  int HuesoCerca(uint8_t* base, uint32_t mgr, const V3& p, float radio) const {
    const uint32_t x1 = mgr + kX0 + kXPaso;
    int mejor = -1;
    float dmin = 1e9f;
    for (uint32_t i = 0; i < 22; ++i) {
      const uint32_t a = x1 + 1616 + 16 * i;
      const float dd = Largo(Mas(p, Por(V3{LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)}, -1.0f)));
      if (dd <= radio + kRadioHueso[i] && dd < dmin) dmin = dd, mejor = int(i);
    }
    return mejor;
  }
  void DiscoVuela(uint8_t* base, uint32_t mgr) {
    const uint32_t x1 = mgr + kX0 + kXPaso;
    const V3 ojo = Ojo();
    if (bloqueo_ >= 0 && combustible_ > 0 && Largo(Mas(disco_p_, Por(ojo, -1.0f))) < kRadioFz) {
      const uint32_t a = x1 + 1616 + 16 * uint32_t(bloqueo_);
      V3 h = Mas(V3{LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)}, Por(disco_p_, -1.0f));
      h = Por(h, 1.0f / std::max(Largo(h), 1e-3f));
      const V3 v = Mas(Por(disco_v_, (1.0f - combustible_) / kDiscoVel), Por(h, combustible_));
      disco_v_ = Por(v, kDiscoVel / std::max(Largo(v), 1e-3f));
      combustible_ = std::max(0.0f, combustible_ - 0.04f);
    }
    disco_p_ = Mas(disco_p_, disco_v_);
    if (const int h = HuesoCerca(base, mgr, disco_p_, kDiscoRadio); h >= 0) {
      ++aciertos_;
      fallos_seguidos_ = 0;
      disco_ = 0;
      dbz3::anim::Reproducir(mgr + kX0 + kXPaso, 0xFAB, true, 0, 0xFAA);   // PINE: 0x3AC
      chispas_.push_back({disco_p_, 0});
      REXLOG_INFO("dbz3 iwdisparo: Kienzan acierta (hueso {}): {} aciertos", h, aciertos_);
    } else if (Largo(Mas(disco_p_, Por(ojo, -1.0f))) > kRadioFz) {
      disco_ = 0;
      if (++fallos_seguidos_ >= 3) pausa_ = 120;
      REXLOG_INFO("dbz3 iwdisparo: Kienzan falla ({} seguidos)", fallos_seguidos_);
    }
  }
  // Freezer (Char_PoseModeUpdateAll / Char_PoseInputTransition): en la esfera de 150 alrededor de la camara, giro
  // +-60 grados e inclinacion -20..60. Si la camara le apunta (producto > 0.98), una tirada (B50 %) por vez le hace
  // esquivar 40 + azar % 80 cuadros hacia un lado (0..3). El paso angular es B48 de Viewer_InitRoundTables 0x34B800
  // (0.021..0.023 rad/cuadro). [VERIFICAR] en pantalla la duracion 40 + azar % 80.
  // (en el decomp ese paso se trunca a entero y el movimiento visible sale de la animacion, sin seguir).
  // 5/11 opusBB: esquiva de Freezer como el original (ver kB4C). Estados: 1 reposo (clip 0x384 en bucle), 2 esquiva.
  // dia1E3: la esquiva ORIGINAL (EU Char_PoseInputTransition 0x35DF50) ejecutada del ELF por src/iw_mips.cpp, tras la
  // cvar dbz3_iw_logica_original (apagada). Una llamada por campo PAL (5 de cada 6 cuadros, paso 1,2), como el original:
  // velocidades y tiempos en sus unidades. El estado vive aqui (lockstep), no en el invitado. false -> la del port.
  bool EsquivaMips(const dbz3::anim::EstadoCuerpo& e, bool hay, const Datos& d, float apunta, const V3& f, uint32_t x1) {
    namespace fz = dbz3::iwmips::freezer;
    (void)apunta;   // el original la calcula de la camara (f) y la posicion
    if (!fz::g_activa || !fz::g_esquiva || !fz::g_activa() || (fz_estado_ != 1 && fz_estado_ != 2)) return false;
    // +0xC74 (fin del clip) como lo mide el port: la corta acaba con su clip; el reposo, al dar la vuelta el 0x384.
    int c74 = 0;
    if (fz_estado_ == 2) {
      if (hay && !fz_clip_ && e.clip != fz_clip_rep_) fz_clip_ = e.clip;
      c74 = hay && fz_clip_ && (e.clip != fz_clip_ || e.cuadro + 1.0f >= float(e.cuadros));
    } else {
      if (hay && !fz_clip_rep_) fz_clip_rep_ = e.clip;
      if (hay && e.clip == fz_clip_rep_) {
        if (fz_vueltas_ == ~0u) fz_vueltas_ = e.vueltas;
        c74 = e.vueltas != fz_vueltas_;
      }
    }
    fz_campo_ += fz::g_ntsc ? 6 : 5;   // dia1E5: con el ELF USA (NTSC) el original corre en cada cuadro
    if (fz_campo_ < 6) return true;   // cuadro sin campo PAL: el original no corre
    fz_campo_ -= 6;
    auto bits = [](float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; };
    auto flt = [](uint32_t b) { float v; std::memcpy(&v, &b, 4); return v; };
    if (az_ != fz_az_p_) fz_az_o_ = az_ + 1.5707964f;   // alguien movio az_ por fuera: se rehace
    fz::EntradaEsq in;
    const uint32_t g[10] = {bits(fz_az_o_), bits(elev_), uint32_t(fz_larga_), 0, bits(fz_vel_), uint32_t(fz_dir_ < 0 ? 0 : fz_dir_),
                            fz_dir_prev_, bits(fz_t_), uint32_t(fz_preparada_), uint32_t(tirada_)};
    std::memcpy(in.glob, g, sizeof g);
    in.estado = fz_estado_; in.c74 = c74; in.krillin = disco_ == 3 ? 10 : 8; in.dif = d.dif;
    in.b48 = kB48[d.dif]; in.b4c = kB4C[d.dif]; in.b50 = kB50[d.dif];
    in.f[0] = -f.x; in.f[1] = f.y; in.f[2] = -f.z;   // ejes del original (T7: x y z invertidos)
    in.t = uint64_t(t_);
    if (!fz_semilla_) fz_semilla_ = semilla_ | 1u;
    uint32_t s = fz_semilla_;
    fz::SalidaEsq o;
    if (!fz::g_esquiva(in, o, s)) { fz_campo_ += 6; return false; }
    fz_semilla_ = s;
    fz_ant_ok_ = az_ == fz_az_p_; fz_az_ant_ = az_; fz_el_ant_ = elev_;   // dia1E4: campo anterior (para el dibujo)
    fz_az_o_ = flt(o.glob[0]); az_ = fz_az_o_ - 1.5707964f; fz_az_p_ = az_; elev_ = flt(o.glob[1]);
    fz_larga_ = o.glob[2] != 0; fz_vel_ = flt(o.glob[4]); fz_dir_ = int(o.glob[5]); fz_dir_prev_ = o.glob[6];
    fz_t_ = flt(o.glob[7]); fz_preparada_ = o.glob[8] != 0; tirada_ = o.glob[9] != 0;
    if (o.estado == 1 && fz_estado_ != 1) {
      fz_vueltas_ = ~0u;
      dbz3::anim::Bucle(x1, 0xFAA);
      REXLOG_INFO("dbz3 iwdisparo: esquiva(mips) fin dir {} larga {} vivida {} az {:.3f} elev {:.3f}", fz_dir_, fz_larga_,
                  esquiva_vivida_, az_, elev_);
    } else if (o.estado == 2 && fz_estado_ != 2) {
      const uint32_t k = o.b60 - 0x38E;
      if (k < 8) dbz3::anim::Reproducir(x1, kEsquivaFz[k], false, 0, 0xFAA);
      fz_clip_ = 0; esquiva_vivida_ = 0;
      REXLOG_INFO("dbz3 iwdisparo: esquiva(mips) inicio dir {} larga {} t {:.1f} vel {:.4f} codigo {:X} az {:.3f} elev {:.3f}",
                  fz_dir_, fz_larga_, fz_t_, fz_vel_, o.b60, az_, elev_);
    } else if (fz_estado_ == 2) {
      ++esquiva_vivida_;
    }
    fz_estado_ = o.estado;
    return true;
  }
  void EsquivaFz(uint8_t* base, uint32_t mgr, const Datos& d, float apunta, const V3& f) {
    if (fase_ != kJuego) return;
    const uint32_t x1 = mgr + kX0 + kXPaso;
    dbz3::anim::EstadoCuerpo e;
    const bool hay = dbz3::anim::LeerEstadoCuerpo(base, x1, 0, e) && e.hay;
    const float b48 = kB48[d.dif];
    if (EsquivaMips(e, hay, d, apunta, f, x1)) { fz_interp_ = fz_ant_ok_ && az_ == fz_az_p_; return; }   // dia1E4
    if (std::getenv("DBZ3_TEST_IWMINI") && ++fz_traza_ % 30 == 0) REXLOG_INFO("dbz3 iwdisparo: esquiva(orig) traza est {} clip {:X} cod {:X} cuadro {:.1f}/{} vueltas {} rep {:X} apunta {:.3f}",
        fz_estado_, e.clip, e.codigo, e.cuadro, e.cuadros, e.vueltas, fz_clip_rep_, apunta);
    auto reposo = [&](const char* por) {
      fz_estado_ = 1; fz_preparada_ = false; fz_vueltas_ = ~0u;
      dbz3::anim::Bucle(x1, 0xFAA);
      REXLOG_INFO("dbz3 iwdisparo: esquiva(orig) fin {} dir {} larga {} vivida {} az {:.3f} elev {:.3f}",
                  por, fz_dir_, fz_larga_, esquiva_vivida_, az_, elev_);
    };
    // Tracker EU 0x35D770 + Char_SelectBoneScreenDirection 0x35D3D0.
    auto preparar = [&]() {
      if (!fz_larga_) { fz_vel_ = b48; fz_t_ = 0; }
      else { fz_vel_ = b48 / 1.5f; fz_t_ = 40.0f + float(Azar() % 80); }
      int dir = int(Azar() & 3);
      if (apunta > 0.95f) {   // en pantalla: huye del centro hacia su lado
        const float caz = std::atan2(f.x, -f.z), cel = std::asin(std::clamp(f.y, -1.0f, 1.0f));
        const float fx = (az_ - caz) * std::cos(elev_), fy = -(elev_ - cel);
        dir = fy < fx ? (fy < 0 ? 2 : 3) : (fx < 0 ? 1 : 0);
      }
      while (dir == fz_dir_) dir = int(Azar() & 3);
      auto mala = [&](int k) { return (k == 1 && az_ < 0.7330383f - 1.5707964f) || (k == 2 && elev_ > 0.6981317f) ||
                                      (k == 3 && elev_ < -0.2327106f) || (k == 0 && az_ > 2.4085543f - 1.5707964f); };
      while (mala(dir)) dir = int(Azar() & 3);
      fz_dir_ = dir; tirada_ = false;
    };
    // eIW6 (EU Char_PoseInputTransition 0x35DF50): con Krillin en el estado 10/11 (lanza 0x3B9 / 0x3B6 hasta que el
    // disco acaba = disco_ 3 aqui) iRam5D9D6C = 1 en CUALQUIER estado de Freezer: esquiva larga; una corta en curso
    // pasa a la rama larga y acaba por tiempo (su contador es 0).
    const bool krillin_lanza = disco_ == 3;
    if (fz_estado_ == 2) {
      if (krillin_lanza) fz_larga_ = true;
      fz_t_ -= 1.0f; ++esquiva_vivida_;
      bool mover = false;
      if (elev_ < -0.34906587f || elev_ > 1.0471976f || az_ < -1.0471976f || az_ > 1.0471976f) {
        az_ = std::clamp(az_, -1.0471976f, 1.0471976f); elev_ = std::clamp(elev_, -0.34906587f, 1.0471976f);
        reposo("limite");
      } else if (apunta < 0.5f) reposo("no apunta");
      else if (!fz_larga_) {
        if (hay && !fz_clip_ && e.clip != fz_clip_rep_) fz_clip_ = e.clip;
        const bool acabo = hay && fz_clip_ && (e.clip != fz_clip_ || e.cuadro + 1.0f >= float(e.cuadros));
        if (acabo) reposo("clip"); else { mover = true; }
      } else if (fz_t_ < 0) reposo("tiempo");
      else mover = true;
      if (mover) {
        if (fz_dir_ == 0) az_ += fz_vel_;
        if (fz_dir_ == 1) az_ -= fz_vel_;
        if (fz_dir_ == 2) elev_ += fz_vel_;
        if (fz_dir_ == 3) elev_ -= fz_vel_;
        // freno: B48/105 por campo del ELF, sin Delta; en pasos de 60: B48/CamposAPasos(105) (EU 126, USA 105)
        if (!fz_larga_) fz_vel_ -= b48 / float(::dbz3::original::CamposAPasos(105, ::dbz3::original::FuenteIw()));
      }
      return;
    }
    // Reposo: tirada B4C por cuadro (EU 0x34AAC0), la que haya al preparar decide corta o larga.
    fz_larga_ = int(Azar() % 100) >= kB4C[d.dif] || krillin_lanza;
    if (hay && !fz_clip_rep_) fz_clip_rep_ = e.clip;
    bool fin_reposo = false;
    if (hay && e.clip == fz_clip_rep_) {
      if (fz_vueltas_ == ~0u) fz_vueltas_ = e.vueltas;
      fin_reposo = e.vueltas != fz_vueltas_;
    }
    if (!fz_preparada_) {
      if (fin_reposo) { preparar(); fz_preparada_ = true; }
      else if (!tirada_ && apunta > 0.98f) {
        tirada_ = true;
        const int r = int(Azar() % 100);
        if (r < kB50[d.dif]) { preparar(); fz_preparada_ = true; }
        REXLOG_INFO("dbz3 iwdisparo: esquiva(orig) tirada B50 {} ({})", kB50[d.dif], r);
      }
    } else if (apunta >= 0.5f) {
      preparar();
      fz_estado_ = 2; esquiva_vivida_ = 0;
      const bool larga = fz_t_ > 35.0f;
      const uint32_t cod = kEsquivaFz[fz_dir_ + (larga ? 4 : 0)];
      dbz3::anim::Reproducir(x1, cod, false, 0, 0xFAA);
      fz_clip_ = 0;
      REXLOG_INFO("dbz3 iwdisparo: esquiva(orig) inicio dir {} larga {} t {:.0f} vel {:.4f} codigo {:X} az {:.3f} elev {:.3f}",
                  fz_dir_, fz_larga_, fz_t_, fz_vel_, 0x38E + fz_dir_ + (larga ? 4 : 0), az_, elev_);
    }
  }
  void MoverFreezer(uint8_t* base, uint32_t mgr, const Datos& d) {
    const V3 ojo = Ojo(), f = Dir(g_yaw.load(), g_polar.load());
    const V3 a{std::cos(elev_) * std::sin(az_), std::sin(elev_), -std::cos(elev_) * std::cos(az_)};
    const float apunta = f.x * a.x + f.y * a.y + f.z * a.z;
    fz_interp_ = false;   // dia1E4: lo enciende EsquivaFz cuando corre el original (cvar)
    if (EsquivaOriginal7()) EsquivaFz(base, mgr, d, apunta, f);
    else {
    if (fase_ == kJuego && esquiva_t_ <= 0) {
      if (apunta > 0.98f && !tirada_) {
        tirada_ = true;
        const int r = int(Azar() % 100);
        const bool sale = r < kB50[d.dif];
        REXLOG_INFO("dbz3 iwdisparo: esquiva tirada B50 {} ({}), {}; az {:.3f} elev {:.3f}",
                    kB50[d.dif], r, sale ? "sale" : "no sale", az_, elev_);
        if (sale) {
          esquiva_ = int(Azar() & 3);
          esquiva_t_ = 40 + int(Azar() % 80);
          esquiva_dur_ = esquiva_t_;
          esquiva_vivida_ = 0;
          REXLOG_INFO("dbz3 iwdisparo: esquiva inicio dir {} cuadros {} az {:.3f} elev {:.3f}",
                      esquiva_, esquiva_dur_, az_, elev_);
          if (g_x1.load()) dbz3::anim::Reproducir(g_x1.load(),0xFB9,false,0,0xFAA);
        }
      } else if (apunta <= 0.98f) {
        tirada_ = false;
      }
    }
    if (esquiva_t_ > 0) {
      --esquiva_t_;
      ++esquiva_vivida_;
      const float v = kB48[d.dif];
      if (esquiva_ == 0) az_ += v;
      if (esquiva_ == 1) az_ -= v;
      if (esquiva_ == 2) elev_ += v;
      if (esquiva_ == 3) elev_ -= v;
      const bool fuera = std::abs(az_) > 1.0472f || elev_ < -0.349f || elev_ > 1.0472f;
      if (fuera) esquiva_t_ = 0;   // fuera: a la pose 1
      az_ = std::clamp(az_, -1.0472f, 1.0472f);
      elev_ = std::clamp(elev_, -0.349f, 1.0472f);
      if (esquiva_t_ == 0)
        REXLOG_INFO("dbz3 iwdisparo: esquiva fin dir {} vivida {} de {} fuera {} az {:.3f} elev {:.3f}",
                    esquiva_, esquiva_vivida_, esquiva_dur_, fuera, az_, elev_);
    }
    }
    // dia1E4: con la cvar Freezer solo se mueve en los campos PAL (50/s, 5 de cada 6 cuadros) y a 60 Hz se quedaba
    // quieto 1 cuadro de cada 6. Solo para dibujar, se interpola entre los dos ultimos campos: fraccion = resto del
    // acumulador / 6, que da un avance constante por cuadro (5/6 de campo). La logica (az_/elev_) no cambia.
    float az = az_, el = elev_;
    if (fz_interp_) {
      const float k = float(fz_campo_) / 6.0f;
      az = fz_az_ant_ + (az_ - fz_az_ant_) * k;
      el = fz_el_ant_ + (elev_ - fz_el_ant_) * k;
    }
    if (fase_ == kJuego && std::getenv("DBZ3_TEST_IWMINI"))   // prueba: registro de posicion por cuadro
      REXLOG_INFO("dbz3 iwdisparo: fz dibujo t={} interp={} campo={} az={:.7f} elev={:.7f} az_logica={:.7f} elev_logica={:.7f}",
                  t_, int(fz_interp_), fz_campo_, az, el, az_, elev_);
    g_px = ojo.x + kRadioFz * std::cos(el) * std::sin(az);
    // DS126: la camara del 0x7D7 (clip 950) mira hacia -Z; Freezer va DELANTE de ella en ese eje. Con el
    // signo +Z se quedaba a la espalda y el piloto (y el retículo) apuntaban al suelo para "alcanzarlo".
    g_pz = ojo.z - kRadioFz * std::cos(el) * std::cos(az);
    g_py = std::max(0.0f, ojo.y - 6.0f + kRadioFz * std::sin(el));
  }

  // ---- MSN016: Ten contra Cell (MgShoot_* EU 0x3099E0-0x30CEF0 y la IA de Cell 0x31C400-0x31DF30: tablas de estado
  // 0x4996B0 entrar / 0x4996E0 al acabar la animacion / 0x499710 cada cuadro). Ten (oculto) es la camara: ojo en
  // (x, 400, z) (UiAnim_SetPos de 0x5D9350 = (0, 400, 200) en el controlador +0x80), x -111..74 y z 60..280 a 1,5 por
  // cuadro (MoveAimByPad). Cuadrado mantenido: carga +0,1 cada 2 s (tope 1); circulo: Kikoho (32 cuadros de carga, el
  // disparo gasta 0,1 y otros 32), la primera vez con <= 0,1 una espera de 220 cuadros. Le da a Cell si su posicion
  // proyectada cae en el rombo (300,147)-(403,250)-(300,353)-(197,250) de la pantalla de 600x500 (EU 0x480A10,
  // Char_IsProjectedInQuad). Peligro = altura de Cell / 400: >= 0,5 DANGER, >= 1 o energia <= 0 pierde. Tiempo
  // {40..160} s; al acabar p = altura / 400: <= 0,0001 Z, <= 0,1 S, <= 0,4 A, <= 0,7 B, si no C; {3600..6000} x mult.
  // EU 0x30CCF0 s_MSN016_UPDATE
  // EU 0x30CC40 Minigame_SetupScene
  void Cuadro15(uint8_t* base, uint32_t mgr, const Datos& d) {
    ++t_;
    dbz3::iwmini::SerieJuego(fase_ == kJuego);
    const uint32_t m = g_mando.load();
    const uint16_t bot = uint16_t(m & 0xFFFF), dirs = uint16_t(m >> 16);
    const bool nuevo = (bot & kDisparo) && !(antes15_ & kDisparo);
    antes15_ = bot;
    const int pal = g_palanca.load();
    const int sx = int8_t(pal & 0xFF), sy = int8_t((pal >> 8) & 0xFF);
    // EU 0x30b410 MgShoot_St_PreIntro + 0x30b1c0 MgShoot_St_Intro (clips 1300 y 1303 del #AMC 915/2) y, sin clips,
    // el Ready de 100 cuadros.
    if (fase_ == kListo) {
      if (t_ == 1 && g_x1.load()) dbz3::anim::Bucle(g_x1.load(), 0x960);   // en reposo (0x640, estado 0)
      if (HayCine(d)) {
        const int t0 = d.cams.count(0) ? d.cams.at(0).dur : 0;
        if (t_ == 1 && t0) Cine(mgr, 0, 0, 0);
        if (t_ == t0 + 1) Cine(mgr, 3, 0xFB5, 0x960);        // PINE: 0x690 con 1300
        if (t_ == t0 + 361) Cine(mgr, 2, 0, 0xFB8, true);    // 0x695 con 1303, sin Ten
        if (PasoIntro(base, mgr, d)) return;
      } else if (t_ < 100) {
        Camara15();
        return;
      }
      g_cine = 0;
      fase_ = kJuego;
      t_ = 0;
      REXLOG_INFO("dbz3 iwdisparo: juego 0x{:X} empieza", d.clave);
      if (const char* r = std::getenv("DBZ3_TEST_MANOS15_RAFAGA")) if (FILE* f = std::fopen("dbz3_shot.req", "wb")) { std::fputs(r, f); std::fclose(f); }   // eIW2: fotos por cuadro del juego (prueba)
      CellEstado(1);
      return;
    }
    if (fase_ == kJuego) {
      restante_ -= 1.0f / 60.0f;
      if (sub15_ != 4) CellCuadro(d);   // en la espera de energia baja Cell se para (Store5d94c0Pair(0, 0))
      // fin por tiempo (MgShoot_CheckTimeUp) o derrota (energia <= 0, Cell a la altura de Ten)
      const float p = cy_ / kAltura15;
      if (restante_ <= 0) {
        restante_ = 0;
        p15_ = p;
        Final15(p >= 0 && p < 1 ? "TIEMPO: Cell contenido" : "TIEMPO: Cell arriba");
      } else if (energia_ <= 0 || p >= 1.0f) {
        p15_ = -1;
        Final15(energia_ <= 0 ? "sin energia" : "Cell llega a Ten");
      } else {
        bool fuego = nuevo;
        bool carga = (bot & 0x8000) != 0;   // cuadrado (X del mando)
        float gx = 0, gz = 0;
        if (std::abs(sx) > 32 || std::abs(sy) > 32) {
          gx = float(sx) / 127.0f;
          gz = float(sy) / 127.0f;
        } else {
          const uint16_t dd = uint16_t(dirs | (dirs >> 8));
          gx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
          gz = float((dd & kAbajo) != 0) - float((dd & kArriba) != 0);
        }
        if (g_auto) Piloto15(gx, gz, fuego, carga);
        if (sub15_ == 0) {   // mira
          mx_ = std::clamp(mx_ + gx * 1.5f, -111.0f, 74.0f);   // derecha (0x20) X + 1,5; arriba (0x10) Z - 1,5
          mz_ = std::clamp(mz_ + gz * 1.5f, 60.0f, 280.0f);
          if (carga && energia_ < 1.0f) {
            sub15_ = 1;
          } else if (fuego && (baja_usada_ || energia_ > 0.1f)) {
            sub15_ = 2;
            t15_ = 0;
          } else if (!baja_usada_ && energia_ <= 0.1f) {
            sub15_ = 4;   // LowEnergyWait: 220 cuadros
            t15_ = 0;
            baja_usada_ = true;
          }
        } else if (sub15_ == 1) {   // Charge: +0,1 / (2 x 60) por cuadro; Effect 6 (935/19) en las manos
          energia_ += 0.1f / 120.0f;
          if (!aura_ || !aura_->vivo) aura_ = Lanzar(19, Cam({0, -0.6f, 4.0f}), Mas(Cam({0, 0, 1}), Por(Cam({0, 0, 0}), -1.0f)));
          if (energia_ > 1.0f || !carga) energia_ = std::min(energia_, 1.0f), sub15_ = 0;
        } else if (sub15_ == 2) {   // Fire: 32 cuadros de carga, el disparo, 32 de luces
          const V3 manos = Cam({0, -0.6f, 4.0f}), fv = Mas(Cam({0, 0, 1}), Por(Cam({0, 0, 0}), -1.0f));
          if (t15_ == 0) Lanzar(3, manos, fv);   // Effect 2 (935/3): la carga
          if (++t15_ == 32) {
            Lanzar(4, manos, Por(fv, -1.0f));   // Effect 3 (935/4) con la direccion negada
            Lanzar(6, manos, fv);               // Effect 5 (935/6): el Kikoho hacia la mira
            energia_ -= 0.1f;
            kikoho_ = 32;
            const bool dentro = CellEnRombo();
            REXLOG_INFO("dbz3 iwdisparo: Kikoho ({}): Cell a {:.0f} de altura, energia {:.2f}", dentro ? "le da" : "falla",
                        cy_, energia_);
            if (dentro) {
              Lanzar(5, V3{cx_, cy_ + 6.0f, cz_}, V3{0, 1, 0});   // Effect 4 (935/5) en Cell (Char_EnterPoseState4Alt)
              CellGolpe();
              chispas_.push_back({V3{cx_, cy_ + 6.0f, cz_}, 0});   // el golpe (Effect 4 de IW): la chispa t21
            }
          } else if (t15_ >= 64) {
            sub15_ = 0;
          }
        } else if (sub15_ == 4) {
          if (++t15_ >= 220) sub15_ = 0;
        }
      }
      if (kikoho_ > 0) --kikoho_;
      for (auto& c : chispas_) c.second += 1;
      chispas_.erase(std::remove_if(chispas_.begin(), chispas_.end(), [](const auto& c) { return c.second > 20; }),
                     chispas_.end());
      Camara15();
      return;
    }
    if (fase_ == kDerribo) {   // EU 0x30a3d0 MgShoot_St_GameOver: clip 1301/1302 y resultado
      if (kikoho_ > 0) --kikoho_;
      Camara15();
      PasoFinal(base, mgr, d);
      if (t_ >= fin_dur_) {
        fase_ = kFinal;
        Acabar(base, mgr, d);
      }
    }
  }
  void Final15(const char* por) {
    fase_ = kDerribo;
    t_ = 0;
    segundos_ = p15_ >= 0 && p15_ < 1 ? float(kTiempo15[dif15_]) : 0;   // para el CLEAR y el registro
    // DS126: los clips lo dicen al reves de DS123: contenido (p en [0,1), Cell a ras de suelo) -> 1302 (ojo y=3.7);
    // Cell arriba o sin energia -> 1301 (ojo y=152). Ver Empezar.
    // EU 0x30a3d0 MgShoot_St_GameOver
    if (datos_) ElegirFin(*datos_, p15_ >= 0 && p15_ < 1 ? datos_->fin_bien : datos_->fin_mal);
    REXLOG_INFO("dbz3 iwdisparo: fin del 15 ({}): altura de Cell {:.1f}, p {:.4f}", por, cy_, p15_);
    if (g_x1.load()) dbz3::anim::Corredor(g_x1.load(), false);
  }
  // EU 0x3245D0 Camera_OrbitInputUpdate
  void Camara15() {   // ojo de Ten y su mirada (la del principio, (0, 400, 200) -> (0, 15, 0), trasladada)
    g_ex = mx_;
    g_ey = kAltura15;
    g_ez = mz_;
    g_tx = mx_;
    g_ty = 15.0f;
    g_tz = mz_ - 200.0f;
    g_px = cx_;
    g_py = cy_;
    g_pz = cz_;
    // prueba (DBZ3_DISPARO_VER_CELL=1): la camara de lado, cerca de Cell, para ver sus animaciones de IW
    if (static const bool ver = std::getenv("DBZ3_DISPARO_VER_CELL") != nullptr; ver) {
      g_ex = cx_ + 45.0f;
      g_ey = cy_ + 12.0f;
      g_ez = cz_ + 10.0f;
      g_tx = cx_;
      g_ty = cy_ + 8.0f;
      g_tz = cz_;
    }
    // Ten (oculto) en el origen de IW (+0x800 = (0, 0.1, 0)), fijo: no sigue a Cell (la camara si apunta con mx/mz).
    g_jx = 0.0f;
    g_jz = 0.0f;
  }
  // Rombo del Kikoho (pantalla de 600x500 de IW, centro (300, 250), media diagonal 103 px)
  bool CellEnRombo() const {
    const V3 e{mx_, kAltura15, mz_}, f = Unir(V3{0, 15.0f - kAltura15, -200.0f});
    const V3 r{-f.z, 0, f.x};   // f x arriba (sin normalizar: f.y no entra)
    const V3 rn = Por(r, 1.0f / std::max(Largo(r), 1e-4f));
    const V3 u{rn.y * f.z - rn.z * f.y, rn.z * f.x - rn.x * f.z, rn.x * f.y - rn.y * f.x};
    const V3 v = Mas(V3{cx_, cy_, cz_}, Por(e, -1.0f));
    const float z = v.x * f.x + v.y * f.y + v.z * f.z;
    if (z <= 1) return false;
    const float k = 250.0f / std::tan(0.3926991f) / z;   // px de IW por unidad (FOV 45, media altura 250)
    const float px = (v.x * rn.x + v.y * rn.y + v.z * rn.z) * k, py = (v.x * u.x + v.y * u.y + v.z * u.z) * k;
    return std::abs(px) + std::abs(py) <= 103.0f;
  }
  static V3 Unir(const V3& a) { return Por(a, 1.0f / std::max(Largo(a), 1e-4f)); }

  // ---- IA de Cell (estados 0..6 de las tablas de arriba). Cada fase dura su animacion de IW (#BSK de 914: 0x654 10,
  // 0x656 21, 0x657 60 / 0x658 35 / 0x659 2, 0x66A 52, 0x66C 40 cuadros; auditoria7/I).
  void CellEstado(int e) {
    cc0_ = cst_ == e ? cc0_ + 1 : 0;
    cst_ = e;
    ce_t_ = 0;
    // la animacion de IW de cada estado (auditoria7/I, Char_EnterPoseState0..6Alt; la 1.a entrada y las siguientes)
    if (const uint32_t x1 = g_x1.load()) {
      const uint32_t an[7][3] = {{0x960, 0x960, 0x960}, {0x961, 0x962, 0x962}, {0x963, 0x963, 0x963},
                                 {0x964, 0x965, 0x966}, {0x967, 0x968, 0x968}, {0x969, 0x96A, 0x96A},
                                 {0x96B, 0x96B, 0x96B}};
      if (e >= 0 && e < 7) dbz3::anim::Bucle(x1, an[e][std::min(cc0_, 2)]);
    }
    if (e == 3) cvy_ = kSubida15[dif15_];   // Char_EnterPoseState3Alt: vy = campo +0x10 (3,0..4,0)
    if (e == 4) cvy_ = cvy_ > 0 ? -0.08f : cvy_ - 0.08f;   // Char_EnterPoseState4Alt (golpe)
    if (e == 1 && cc0_ == 0) {   // DashTrail: 5 marcas al azar (Char_InitTrailMarkerRandom)
      namespace ce = dbz3::iwmips::cell;   // N2: con dbz3_iw_logica_original, las del original (LFSR propio)
      ce::Marca om[5];
      if (!cell_semilla_) cell_semilla_ = semilla_ | 1u;
      uint32_t s = cell_semilla_;
      const bool orig = ce::g_marcas && ce::g_marcas(om, s, uint64_t(t15_));
      if (orig) {
        cell_semilla_ = s;
        for (size_t k = 0; k < marcas_.size(); ++k) marcas_[k] = {om[k].tipo, om[k].x, om[k].z, om[k].paso, om[k].dura};
      } else
      for (auto& mk : marcas_) {
        mk.tipo = int(Azar() % 10);
        mk.x = float(Azar() % 230) - 140.0f;
        mk.z = float(Azar() % 270) - 90.0f;
        mk.paso = float(Azar() & 0x7F) / 16.0f;
        mk.dura = float(Azar() & 0x7F) + 32.0f;
      }
      marca_ = 0;
      wx_ = marcas_[0].x;
      wz_ = marcas_[0].z;
      rumbo_ = std::atan2(wx_ - cx_, wz_ - cz_);
    }
  }
  void CellGolpe() {   // Char_HandleQuadLanding: estado 0xC -> 4; guarda la altura del golpe (recuperacion)
    golpe_y_ = cy_;
    CellEstado(4);
  }
  void CellCuadro(const Datos& d) {
    (void)d;
    ++ce_t_;
    switch (cst_) {
      case 1: {   // UpdateSteer: la primera animacion a 1,5 por cuadro; despues las marcas a 3 (campo +0xC)
        if (cc0_ < 1) {
          cx_ += std::sin(rumbo_) * 1.5f;
          cz_ += std::cos(rumbo_) * 1.5f;
          if (ce_t_ >= 10) CellEstado(1);
        } else if (Perseguir()) {
          CellEstado(2);
        }
        break;
      }
      case 2:   // SteerInput (< 12 cuadros a 1,5) y al acabar la animacion: a subir
        if (ce_t_ < 12) cx_ += std::sin(rumbo_) * 1.5f, cz_ += std::cos(rumbo_) * 1.5f;
        if (ce_t_ >= 21) CellEstado(3);
        break;
      case 3:   // sube: y += vy desde la 2.a animacion (0x31CE60); 0x657 -> 0x658 -> 0x659 (DispatchEarly)
        if (cc0_ != 0) cy_ += cvy_;
        if (ce_t_ >= (cc0_ == 0 ? 60 : cc0_ == 1 ? 35 : 2) && cc0_ < 3) CellEstado(3);
        break;
      case 4:   // cae (Char_PoseTickRise): y += vy, vy - 0,06; al suelo (0,1): 5
        if (cy_ + cvy_ > 0.1f) {
          cy_ += cvy_;
          cvy_ -= 0.06f;
        } else {
          cy_ = 0.1f;
          CellEstado(5);
        }
        break;
      case 5:   // TimedDispatch: tras la animacion, y despues altura del golpe / 400 x 60 cuadros: 6
        if (cc0_ == 0 && ce_t_ >= 52) CellEstado(5);
        else if (cc0_ > 0 && float(ce_t_) > golpe_y_ / kAltura15 * 60.0f) CellEstado(6);
        break;
      case 6:   // DashTrail al acabar: marcas nuevas; si la 1.a es de tipo 0, a subir; si no, a correr
        if (ce_t_ >= 40) {
          CellEstado(1);
          if (marcas_[0].tipo == 0) CellEstado(3);
        }
        break;
      default:
        break;
    }
    cx_ = std::clamp(cx_, -250.0f, 250.0f);
    cz_ = std::clamp(cz_, -250.0f, 250.0f);
  }
  // Char_PursueTrailMarker: hacia el punto de trabajo a 3 por cuadro; tipo 1/9 = la marca, 2-8 = se acerca a ella
  // `paso` por cuadro, 0 = fin. Se pasa a la siguiente a menos de 30 (campo +0xC x 10) o al acabar su tiempo.
  bool Perseguir() {
    while (marca_ < 5) {
      const Marca& mk = marcas_[size_t(marca_)];
      if (mk.tipo == 0) return true;
      if (mk.tipo == 1 || mk.tipo == 9) {
        wx_ = mk.x, wz_ = mk.z;
      } else {
        const float dx = mk.x - wx_, dz = mk.z - wz_, l = std::hypot(dx, dz);
        if (l > mk.paso) wx_ += dx / l * mk.paso, wz_ += dz / l * mk.paso;
        else wx_ = mk.x, wz_ = mk.z;
      }
      const float dmx = std::hypot(mk.x - cx_, mk.z - cz_);
      if (dmx < 30.0f || ++marca_t_ > int(mk.dura)) {
        ++marca_;
        marca_t_ = 0;
        continue;
      }
      break;
    }
    if (marca_ >= 5) return true;
    const float dx = wx_ - cx_, dz = wz_ - cz_, l = std::hypot(dx, dz);
    if (l > 1e-3f) {
      const float paso = std::min(3.0f, l);
      cx_ += dx / l * paso;
      cz_ += dz / l * paso;
      rumbo_ = std::atan2(dx, dz);
    }
    return false;
  }
  // Prueba: la mira sigue a Cell y dispara cuando esta dentro del rombo; carga con poca energia.
  void Piloto15(float& gx, float& gz, bool& fuego, bool& carga) {
    // donde estara Cell cuando salga el Kikoho (32 cuadros), en el centro de la mirada: z del ojo = z + 0,52 (400 - y)
    const float y = std::min(cy_ + (cst_ == 3 && sub15_ == 0 ? 32.0f * cvy_ : 0.0f), kAltura15);
    const float objx = cx_, objz = cz_ + 200.0f / 385.0f * (kAltura15 - y);
    gx = std::clamp((objx - mx_) / 1.5f, -1.0f, 1.0f);
    gz = std::clamp((objz - mz_) / 1.5f, -1.0f, 1.0f);
    fuego = sub15_ == 0 && cst_ == 3 && CellEnRombo() && ++auto_t_ % 2 == 0;   // al subir
    carga = energia_ < 0.95f && (cst_ == 1 || cst_ == 4 || cst_ == 5 || cst_ == 6);   // mientras no sube
  }

  // ---- 3: ladrillos con el Kaio (`# SHOOT #`, motor MgFly: EU 0x366480-0x369370; datos DATA_CMN 890 = #MSS 890/4,
  // ladrillo ITEMB03_BRICK 890/1; leidos por R12 y por mi). Bloque del #MSS por dificultad: N ladrillos (60/50/40/30/30/
  // 15), recorrido de 115 por tramo, umbrales de aciertos 7/6/5/4/3 (Z..C) y sus puntos, camara (0, 30, 100) -> (0, 35,
  // 0) a 45 grados, y 60 trayectorias (velocidad, inicio x, y, profundidad, fin x, y, profundidad) en la pantalla de PS2
  // (lienzo600x450, medido enPINE). Mira 2D con la palanca/cruceta; si el ladrillo cae dentro de su radio proyectado
  // (ctx+0x514: focal*S/z, con el factor de direccion del tramo) lo fija y lo imanta12cuadros; circulo = disparo (7 en
  // total): fijado = roto. Sin tiempo: acaba con N tramos o los 7 disparos.
  // EU 0x366f60 StartFunc
  void Cuadro3(uint8_t* base, uint32_t mgr, const Datos& d) {
    ++t_;
    // Oraculo por campoPAL: solo el arnes puede pedir1,2. En la partida
    // normal del port un cuadro60Hz vale1, como en IWNTSC.
    const float dt3=std::getenv("DBZ3_TEST_IWMINI") && std::getenv("DBZ3_TEST_LADRILLOS_PAL")?1.2f:1.0f;
    dbz3::iwmini::SerieJuego(fase_ == kJuego);   // R17: el 3 tambien escribe la serie jugador/ojo de comparar_modo
    const uint32_t m = g_mando.load();
    const uint16_t bot = uint16_t(m & 0xFFFF), dirs = uint16_t(m >> 16);
    bool fuego = (bot & kDisparo) && !(antes15_ & kDisparo);
    antes15_ = bot;
    const int pal = g_palanca.load();
    const int sx = int8_t(pal & 0xFF), sy = int8_t((pal >> 8) & 0xFF);
    // EU 0x367e30 MgFly_StateIntroSetup: entrada del 3 directamente con la camara fija del #MSS (DS126: se quito el
    // clip 0 del #AMC 890/5, que es la casa de Kaio y no acaba en la galeria); sin clip, espera de 100 cuadros.
    if (fase_ == kListo) {
      if (HayCine(d)) {
        const int t0 = d.cams.count(0) ? d.cams.at(0).dur : 0;
        if (t_ == 1 && t0) Cine(mgr, 0, 0, 0);
        if (t_ == t0 + 1) Cine(mgr, 1, 0xFA0, 0);   // PINE: linea 1 de la escena 0: actor 1, 0x1E0 con 750
        if (PasoIntro(base, mgr, d)) return;
      } else if (t_ < 100) {
        return;
      }
      g_cine = 0;
      fase_ = kJuego;
      t_ = 0;
      REXLOG_INFO("dbz3 iwdisparo: juego 0x{:X} empieza", d.clave);
      if(const char* aviso=std::getenv("DBZ3_TEST_LADRILLOS_AVISO");std::getenv("DBZ3_TEST_IWMINI") && aviso) {
        quedan_=std::atoi(aviso)==0?(d.n_tramos*2)/3:d.n_tramos/3;
        Progreso3(d);
      } else if (!(OriginalActiva3() && PasoOriginal3(d, 0, 0, 0, 0, true))) Tramo(d);
      return;
    }
    if (fase_ == kDerribo) {   // EU 0x3675f0 MgFly_FinishAndApplyResult
      if (PasoFinal3(base,mgr,d)) fase_ = kFinal, Acabar(base, mgr, d);
      return;
    }
    if (fase_ != kJuego) return;
    if (orig3_ != 0 && PasoOriginal3(d, bot, dirs, sx, sy)) return;   // T3: logica original del ELF (mgfly)
    // mira: palanca (x*x + y*y > 0xFFF) o cruceta, a la velocidad del #MSS (+0x10 = 10 px de PS2 por cuadro)
    float gx = 0, gy = 0;
    if (sx * sx + sy * sy > 0xFFF) {
      const float l = std::hypot(float(sx), float(sy));
      gx = float(sx) / l, gy = float(sy) / l;
    } else {
      const uint16_t dd = uint16_t(dirs | (dirs >> 8));
      gx = float((dd & kDerecha) != 0) - float((dd & kIzquierda) != 0);
      gy = float((dd & kAbajo) != 0) - float((dd & kArriba) != 0);
    }
    if (g_auto) Piloto3(gx, gy, fuego);
    // el ladrillo avanza por su tramo
    if(mensaje3_.Activo()) {
      mensaje3_.Paso(d.mensajes3,dt3);
      if(!mensaje3_.Activo()) {
        cargar_postmensaje3_=true;tramo_espera_=1; // el task del mensaje termina DESPUES del padre
        REXLOG_INFO("dbz3 iwdisparo: aviso Kaio terminado; siguiente nodo en60cuadros");
      }
    } else if (tramo_espera_ > 0) {
      tramo_espera_-=dt3;
      if (tramo_espera_ <= 0) {
        tramo_espera_=0;
        if(cargar_postmensaje3_) {cargar_postmensaje3_=false;Tramo(d);tramo_preparado3_=true;tramo_espera_=60;}
        else if(tramo_preparado3_) tramo_preparado3_=false;
        else Progreso3(d);
      }
    } else if (ladrillo_vivo_) {
      progreso_ += lad_vel_*dt3;
      const bool escapado=progreso_>d.lad_duracion; // original compara >, no >=
      progreso_=std::min(progreso_,d.lad_duracion);
      const float u = std::min(progreso_ / d.lad_duracion, 1.0f);
      lad_z_ = lad_a_[2] + (lad_b_[2] - lad_a_[2]) * u;
      // EU366700/367C10: interpolar posiciones3D y reproyectar, no pixels.
      const float ax=(lad_a_[0]-300.0f)*lad_a_[2],bx=(lad_b_[0]-300.0f)*lad_b_[2];
      const float ay=(lad_a_[1]-225.0f)*lad_a_[2],by=(lad_b_[1]-225.0f)*lad_b_[2];
      if(lad_z_>0) lad_x_=300.0f+(ax+(bx-ax)*u)/lad_z_,lad_y_=225.0f+(ay+(by-ay)*u)/lad_z_;
      if (escapado) {   // espera de progreso: las puertas de aviso/final se resuelven aparte
        ladrillo_vivo_ = fijado_ = false;
        bloqueo_tiro3_=true;
        tramo_espera_ = quedan_<=0 || tiros_<=0 ? 60 : 1; // EU367C10: progreso inmediato salvo fin
      }
    }
    // StartFunc EU366F60 resuelve el disparo con la fijacion del cuadro
    // anterior y retorna sin mover/adquirir la mira. El task del blanco ya
    // avanzo antes; sus particulas siguen teniendo su propio reloj.
    const bool dispara=fuego && tiros_>0 && !bloqueo_tiro3_;
    if (dispara) {
      --tiros_;
      {   // Effect 7 (890/6/22) en la mira, a la profundidad del ladrillo
        const float zz = lad_z_, xx = lad_x_, yy = lad_y_;
        lad_x_ = mira_x_, lad_y_ = mira_y_, lad_z_ = std::max(zz, 60.0f);
        Lanzar(22, Lad3D(), V3{0, 0, 1});
        lad_x_ = xx, lad_y_ = yy, lad_z_ = zz;
      }
      if (fijado_ && ladrillo_vivo_) {
        ++aciertos_;
        chispas_.push_back({Lad3D(), 0});
        ladrillo_vivo_ = fijado_ = false;
        bloqueo_tiro3_=true;
        tramo_espera_ = 120;   // 30 + 90 cuadros (LoadNodeStart, SegmentProgress)
        REXLOG_INFO("dbz3 iwdisparo: ladrillo roto ({} de {} disparos, quedan {})", aciertos_, 7 - tiros_, quedan_);
      } else {
        REXLOG_INFO("dbz3 iwdisparo: disparo al aire (quedan {} disparos)", tiros_);
      }
      if(tiros_<=0)bloqueo_tiro3_=true; // si falla el ultimo tiro, el blanco termina su vuelo antes del fin
    }
    if(!dispara) {
      const float px=mira_x_,py=mira_y_;
      mira_x_=std::clamp(mira_x_+gx*d.lad_vel_mira,0.0f,600.0f);
      mira_y_=std::clamp(mira_y_+gy*d.lad_vel_mira,0.0f,450.0f);
      if(!fijado_ && ladrillo_vivo_ && lad_z_<=700.0f) {
        const float ax=mira_x_-px,ay=mira_y_-py,bx=lad_x_-px,by=lad_y_-py;
        const float l2=ax*ax+ay*ay,s=l2>0?std::clamp((ax*bx+ay*by)/l2,0.0f,1.0f):0.0f;
        if(std::hypot(bx-ax*s,by-ay*s)<RadioLadrillo()) fijado_=true,tiro_t_=12.0f; // 41400000
      }
      // También se aplica en el MISMO cuadro de adquisición (EU367418).
      if(fijado_ && ladrillo_vivo_) {
        if(tiro_t_>dt3) {
          const float mezcla=dt3/tiro_t_;
          mira_x_+=(lad_x_-mira_x_)*mezcla;mira_y_+=(lad_y_-mira_y_)*mezcla;tiro_t_-=dt3;
        } else mira_x_=lad_x_,mira_y_=lad_y_;
        mira_x_=std::clamp(mira_x_,0.0f,600.0f);mira_y_=std::clamp(mira_y_,0.0f,450.0f);
      }
    }
    for (auto& c : chispas_) c.second += 1;
    chispas_.erase(std::remove_if(chispas_.begin(), chispas_.end(), [](const auto& c) { return c.second > 20; }),
                   chispas_.end());
    if(std::getenv("DBZ3_TEST_IWMINI")) {
      static FILE* traza=[] {const char* p=std::getenv("DBZ3_TEST_LADRILLOS_TRAZA");return p?std::fopen(p,"w"):nullptr;}();
      if(traza) {
        const double host=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::fprintf(traza,"%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%d,%d,%d,%.6f,%lld,%.6f,%d,%zu\n",t_,progreso_,lad_x_,lad_y_,lad_z_,mira_x_,mira_y_,int(fijado_),tiro_t_,quedan_,tiros_,mensaje3_.Id(),mensaje3_.Tiempo(),(long long)mensaje3_.PosicionAudio(),host,int(mensaje3_.VozReanudada()),iwsonido::Salida().Mezcla().Voces());
        std::fflush(traza);
      }
    }
  }
  // T3 (8/11): 0x7D3 con la logica original (iw_mips_vuelo.inl, cvar dbz3_iw_7d3_original, de serie apagada). Por campo PAL
  // (5 de cada 6 cuadros de 60 Hz) corre del ELF la tarea de estados de MgFly y StartFunc 0x366F60; aqui se pasan el
  // mando y el aviso de Kaio, y se recogen mira, ladrillo, fijado, disparos y aciertos. false = el port sigue (USA, error).
  bool OriginalActiva3() const {
    return dbz3::iwmips::mgfly::g_ladrillos && dbz3::iwmips::mgfly::g_activa_lad && dbz3::iwmips::mgfly::g_activa_lad();
  }
  bool PasoOriginal3(const Datos& d, uint16_t bot, uint16_t dirs, int sx, int sy, bool inicio = false) {
    namespace mg = dbz3::iwmips::mgfly;
    if (!inicio && orig3_ != 1) return false;
    if (d.mss_raw.empty()) return false;
    acum3_ += dbz3::iwmips::freezer::g_ntsc ? 6 : 5;   // un campo PAL de cada 6 cuadros de 60 Hz; NTSC: cada cuadro
    if (!inicio && acum3_ < 6) return true;   // cuadro de 60 Hz sin campo PAL
    if (!inicio) acum3_ -= 6;
    mg::Entrada in;
    in.inicio = inicio;
    in.mss = d.mss_raw.data(); in.mss_n = d.mss_raw.size(); in.dif = d.mss_bdif;
    const uint16_t dd = uint16_t(dirs | (dirs >> 8));
    const uint16_t mant = uint16_t((bot & ~0x00F0) | (dd & 0x00F0));
    in.mantenido = mant; in.nuevo = uint16_t(mant & ~held3_);
    held3_ = mant;
    in.palanca_x = int8_t(sx); in.palanca_y = int8_t(sy);
    in.aviso_fin = aviso_fin3_;
    aviso_fin3_ = false;
    in.t = uint64_t(t_);
    if (!sem3_) sem3_ = iwazar::Global().Estado() | 1u;
    mg::Salida o;
    if (!mg::g_ladrillos(in, o, sem3_)) {
      if (inicio) { orig3_ = 0; return false; }
      REXLOG_WARN("dbz3 iwdisparo: la logica original de los ladrillos fallo; sigue el port");
      orig3_ = 2;
      return false;
    }
    if (inicio) { orig3_ = 1; REXLOG_INFO("dbz3 iwdisparo: ladrillos con la logica original del ELF (dbz3_iw_7d3_original)"); }
    const int antes_aciertos = aciertos_;
    mira_x_ = o.mira[0]; mira_y_ = o.mira[1];
    if (o.activo) lad_x_ = o.blanco[0], lad_y_ = o.blanco[1], lad_z_ = o.prof;
    progreso_ = o.progreso; fijado_ = o.fijado != 0; tiro_t_ = o.iman;
    ladrillo_vivo_ = o.activo != 0;
    tiros_ = o.tiros; aciertos_ = o.aciertos; quedan_ = o.quedan;
    lad_a_ = {o.pos3b[0], o.pos3b[1], o.pos3b[2]};
    for (int k = 0; k < o.n_ev; ++k) {
      const mg::Evento& e = o.ev[k];
      if (e.tipo == 'S') {   // Aura_Dash(7): el disparo, en la mira a la profundidad del ladrillo
        const float xx = lad_x_, yy = lad_y_, zz = lad_z_;
        lad_x_ = mira_x_, lad_y_ = mira_y_, lad_z_ = std::max(zz, 60.0f);
        Lanzar(22, Lad3D(), V3{0, 0, 1});
        lad_x_ = xx, lad_y_ = yy, lad_z_ = zz;
      } else if (e.tipo == 'K') {
        if (d.mensajes3.Valido()) { mensaje3_.Iniciar(d.mensajes3, e.n); REXLOG_INFO("dbz3 iwdisparo: aviso Kaio {} (original)", e.n); }
        else aviso_fin3_ = true;
      }
    }
    if (aciertos_ > antes_aciertos) {
      chispas_.push_back({Lad3D(), 0});
      REXLOG_INFO("dbz3 iwdisparo: ladrillo roto (original; {} aciertos, {} tiros, quedan {})", aciertos_, 7 - tiros_, quedan_);
    }
    if (mensaje3_.Activo()) {
      mensaje3_.Paso(d.mensajes3, 1.0f);
      if (!mensaje3_.Activo()) aviso_fin3_ = true;
    }
    if (o.fin) { REXLOG_INFO("dbz3 iwdisparo: original: fin de los ladrillos (rango {})", o.rango); Fin3(d); }
    return true;
  }
  // EU 0x366700 MgFly_SetupNextNode
  void Tramo(const Datos& d) {   // MgFly_SetupNextNode: una trayectoria al azar de las 60
    if (d.tramos.empty()) return;
    --quedan_; // EU367D40/367990/367910: se descuenta al cargar, no al desaparecer
    // EU36672C..3667A8: modulo del contador RESTANTE, decrece y se
    // recarga al agotarse; el buffer de mascara inicializado a0 no se marca
    // en este recorrido (comprobado con las instrucciones, no inferido).
    if(tramos_disponibles_<=0) tramos_disponibles_=int(d.tramos.size());
    size_t indice=iwazar::Global().Memory(32) % uint32_t(tramos_disponibles_);
    if(std::getenv("DBZ3_TEST_IWMINI")) if(const char* p=std::getenv("DBZ3_TEST_LADRILLOS_TRAMO")) {
      const int i=std::atoi(p);if(i>=0 && size_t(i)<d.tramos.size()) indice=size_t(i);
    }
    const auto& tr = d.tramos[indice];
    --tramos_disponibles_;
    lad_vel_ = tr[0];
    lad_a_ = {tr[1], tr[2], tr[3]};
    lad_b_ = {tr[4], tr[5], tr[6]};
    progreso_ = 0;
    lad_x_ = lad_a_[0], lad_y_ = lad_a_[1], lad_z_ = lad_a_[2];
    ladrillo_vivo_ = true;
    fijado_ = false;
    bloqueo_tiro3_=false;
  }
  void Progreso3(const Datos& d) {
    if(quedan_<=0 || tiros_<=0) return Fin3(d);
    const int aviso=quedan_==(d.n_tramos*2)/3?0:quedan_==d.n_tramos/3?1:-1;
    if(aviso>=0 && d.mensajes3.Valido()) {
      bloqueo_tiro3_=true;
      mensaje3_.Iniciar(d.mensajes3,aviso);
      REXLOG_INFO("dbz3 iwdisparo: aviso Kaio {} restantes {} vozADX {}",aviso,quedan_,d.mensajes3.voces[aviso]);
    } else Tramo(d);
  }
  bool PasoFinal3(uint8_t* base,uint32_t mgr,const Datos& d) {
    const bool bien=segundos_>0;
    if(fin3_inicio_<0) {
      // EU342BD0/342990: el objeto es un arbol; no basta el segmento
      // de su raiz (puede acabar antes que el hijo que pinta CLEAR).
      for(const auto& n:aviso_final3_.nodos) {
        // n.seg es cache perezosa del muestreador (empieza en-1).
        // El nodo de disco conserva el primer segmento de la cadena.
        const int seg=n.disco>=0?d.hit_ama.Nodo(n.disco).anim:-1;
        if(seg>=0 && anim::ama::BanderasFin(d.hit_ama,seg,float(t_))!=0xFF)return false;
      }
      fin3_inicio_=t_;
      REXLOG_INFO("dbz3 iwdisparo: fin SHOOT aviso{} terminado en{}; clip{}",bien?7:8,t_,fin_id_);
      if(!sin_cine_ && fin_id_>=0)Cine(mgr,1,bien?0xFA1:0xFA2,0);
    }
    if(sin_cine_ || fin_id_<0)return true;
    const auto it=d.cams.find(fin_id_);if(it==d.cams.end())return true;
    const int cuadro=t_-fin3_inicio_;
    float ojo[3],obj[3];
    if(ClipPose(d,fin_id_,float(cuadro),base,mgr,ojo,obj))PonerCamara(ojo,obj);
    return cuadro>=it->second.dur-1;
  }
  // EU 0x367990 MgFly_StateSegmentProgress
  // EU 0x3676d0 MgFly_StateEndCutsceneLine1
  // EU 0x367850 MgFly_StateEndCutsceneLine2
  void Fin3(const Datos& d) {
    fase_ = kDerribo;
    t_ = 0;
    fin3_inicio_=-1;bloqueo_tiro3_=true;
    const bool superado=std::any_of(std::begin(d.umbral),std::end(d.umbral),[&](int v){return aciertos_>=v;});
    aviso_final3_=anim::ama::CrearArbol(d.hit_ama,d.hit_ama.Objeto(superado?7:8));
    segundos_ = superado ? 1.0f : 0.0f;
    ElegirFin(d, superado ? d.fin_bien : d.fin_mal);
    REXLOG_INFO("dbz3 iwdisparo: fin de los ladrillos: {} aciertos", aciertos_);
  }
  // pantalla de PS2 (640 x 448, centro 320, 224) a mundo: el punto a la profundidad z por la mira de la camara del #MSS
  V3 Lad3D() const {
    V3 e{0, 30, 100}, f = Unir(V3{0, 5, -100});
    if (datos_) {
      e = {datos_->mss_ojo[0], datos_->mss_ojo[1], datos_->mss_ojo[2]};
      f = Unir(Mas(V3{datos_->mss_obj[0], datos_->mss_obj[1], datos_->mss_obj[2]}, Por(e, -1.0f)));
    }
    const V3 r = Unir(V3{-f.z, 0, f.x});
    const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    const float k = lad_z_ * kEsc3 / (datos_?datos_->lad_focal:512.0f);
    return Mas(e, Mas(Por(f, lad_z_ * kEsc3), Mas(Por(r, (lad_x_ - 300.0f) * k), Por(u, (225.0f - lad_y_) * k))));
  }
  // F1: el ladrillo en el mundo bajo su punto de la pantalla de PS2 (donde va la mira) a su profundidad, con la camara
  // real del combate (inversa de su vista). Lad3D supone la camara del #MSS: si la de B3 no coincide, el ladrillo
  // caia fuera de la vista (no se veia en ninguna captura).
  V3 LadCam(const ps2::Camara& cam, const ImGuiIO& io) const {
    const float esc = io.DisplaySize.y / 450.0f;
    const float sx = io.DisplaySize.x * 0.5f + (lad_x_ - 300.0f) * esc, sy = io.DisplaySize.y * 0.5f + (lad_y_ - 225.0f) * esc;
    const float z = std::max(lad_z_ * kEsc3, 2.0f), k = cam.h.y / std::tan(cam.fov * 0.5f) / z;
    const float* m = cam.m;
    const float vx = (sx - cam.c.x) / k - m[12], vy = -(sy - cam.c.y) / k - m[13], vz = -z - m[14];
    return {m[0] * vx + m[1] * vy + m[2] * vz, m[4] * vx + m[5] * vy + m[6] * vz, m[8] * vx + m[9] * vy + m[10] * vz};
  }
  // EU 0x366480 MgFly_ProjectToScreen
  float RadioLadrillo() const {
    // MgFly_ProjectToScreen (EU 0x366480): +0x514 = (focal * S / z) * (0,5*|n.z| + hipot(n.x, n.y)), con n la normal
    // de A->B en espacio de camara y S = bloque +0x08. PINE: focal512, lienzo600x450. IW pone el radio a0 si la
    // profundidad pasa de 700.
    if (lad_z_ > 700.0f) return 0.0f;
    const float kFocal = datos_ ? datos_->lad_focal : 512.0f;
    constexpr float kCx = 300.0f, kCy = 225.0f;
    const V3 a{lad_a_[2] * (lad_a_[0] - kCx) / kFocal, lad_a_[2] * (lad_a_[1] - kCy) / kFocal, lad_a_[2]};
    const V3 b{lad_b_[2] * (lad_b_[0] - kCx) / kFocal, lad_b_[2] * (lad_b_[1] - kCy) / kFocal, lad_b_[2]};
    const V3 n = Unir(Mas(b, Por(a, -1.0f)));
    return kFocal * lad_s_ / std::max(lad_z_, 1.0f) * (0.5f * std::fabs(n.z) + std::hypot(n.x, n.y));
  }
  void Piloto3(float& gx, float& gy, bool& fuego) {
    gx = std::clamp((lad_x_ - mira_x_) / 10.0f, -1.0f, 1.0f);
    gy = std::clamp((lad_y_ - mira_y_) / 10.0f, -1.0f, 1.0f);
    fuego = fijado_ && tiro_t_ <= 1.0f && ++auto_t_ % 2 == 0;
  }

  // Camara: derecha (cine::Camara: f x arriba), arriba y delante. Las manos de IW (892/1 y 892/3) estan hechas en el
  // espacio de la camara: la mano en z = 0 y el brazo hacia -z (hasta -7.5), la izquierda en -x.
  // Viewer_UpdateMain (0x32C7E0) mueve cada mano con sus angulos +0x90/+0x94 (Viewer_GetParam989C por cuadro) sobre el
  // hueso del brazo; no hay un desplazamiento fijo equivalente en B3. [VERIFICAR] en pantalla el sitio de las manos.
  V3 Cam(const V3& c, float escala = 1.0f) const {
    const float yaw = g_yaw.load(), polar = g_polar.load();
    V3 f = Dir(yaw, polar), r{-std::cos(yaw), 0, std::sin(yaw)}, ojo = Ojo();
    if (juego_ == 15) {   // la camara de Ten (Camara15)
      f = Unir(V3{0, 15.0f - kAltura15, -200.0f});
      r = Unir(V3{-f.z, 0, f.x});
      ojo = {mx_, kAltura15, mz_};
    }
    const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    return Mas(ojo, Por(Mas(Por(r, c.x), Mas(Por(u, c.y), Por(f, c.z))), escala));
  }
  static V3 SitioMano(int k, float retro) { return {k == 0 ? 2.0f : -2.0f, -1.73f - 0.3f, 7.1f - 0.6f + 1.47f * retro}; }   // punta de los dedos (z = 0 del modelo), matriz de EU 0x32C5D0
  V3 Mano(int k, float retro) const {   // punta de la mano k en el mundo (de ahi sale el rayo)
    const V3 o = SitioMano(k, retro);
    return Cam({o.x, o.y + 0.3f, o.z + 0.6f});
  }

  void Rayos(uint8_t* base, uint32_t mgr) {
    const uint32_t x1 = mgr + kX0 + kXPaso, rt = REX_LOAD_U32(x1 + 2996);
    for (Rayo& r : rayos_) {
      if (!r.vivo) continue;
      r.p = Mas(r.p, r.v);
      if (r.nucleo) r.nucleo->origen = r.p, r.nucleo->dir = Unir(r.v);
      if (Largo(Mas(r.p, Por(Ojo(), -1.0f))) > kRayoMax) {   // fuera: fallo
        r.vivo = false;
        continue;
      }
      if (!rt || vida_ <= 0) continue;
      // el hueso mas cercano dentro de 1 + su radio (Viewer_PickNearestBoneWorld + Math_SphereHitDist)
      int mejor = -1;
      float dmin = 1e9f;
      for (uint32_t i = 0; i < 22; ++i) {
        const uint32_t a = x1 + 1616 + 16 * i;
        const V3 c{LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)};
        const float dd = Largo(Mas(r.p, Por(c, -1.0f)));
        if (dd <= 1.0f + kRadioHueso[i] && dd < dmin) dmin = dd, mejor = int(i);
      }
      if (mejor < 0) continue;
      r.vivo = false;
      const int g = kGrupo[mejor];
      if (probar_reaccion_piccolo_ && fase_ == kJuego) {
        const auto golpe = reaccion_piccolo_.Golpe(mejor, float(vida_), float(vida_max_), probar_acuse_piccolo_);
        if (golpe.error == iwpiccolo::Error::Ninguno && golpe.orden.reproducir) {
          if (probar_acuse_piccolo_) {
            const auto orden = dbz3::anim::ReiniciarPoseConAcuse(x1,iwpiccolo::AliasReaccion(golpe.orden.codigo),true);
            const bool vinculada = reaccion_piccolo_.VincularReinicio(golpe.estado.generacion,orden);
            REXLOG_INFO("dbz3 solIWX: Piccolo reinicio pedido gen {} orden {} vinculado {}",
                golpe.estado.generacion,orden,vinculada);
          } else dbz3::anim::ReiniciarPose(x1, iwpiccolo::AliasReaccion(golpe.orden.codigo), true);
          REXLOG_INFO("dbz3 solIWX: Piccolo reaccion entra gen {} rol {} codigo {:X} alias {:X} previo {} repeticion {}",
              golpe.estado.generacion, mejor, golpe.orden.codigo, iwpiccolo::AliasReaccion(golpe.orden.codigo),
              golpe.estado.pose_anterior, golpe.estado.repeticion_cc0);
        }
      }
      vida_ = std::max(0, vida_ - kDano[g]);
      chispas_.push_back({r.p, 0});
      hit_t_ = datos_?datos_->hit_cuadros:0; // AMA5/0 objeto5:17cuadros en el disco del jugador
      iwmini::FadeRival(20);   // EU 0x32B2A0: s_UTL_CHRFADE (20, 20, 0,95; tabla 0x480C54) sobre Piccolo
      REXLOG_INFO("dbz3 iwdisparo: impacto hueso {} (grupo {}, -{}): vida {}/{}", mejor, g, kDano[g], vida_, vida_max_);
    }
    rayos_.erase(std::remove_if(rayos_.begin(), rayos_.end(), [](const Rayo& r) { return !r.vivo; }), rayos_.end());
    for (auto& c : chispas_) c.second += 1;
    chispas_.erase(std::remove_if(chispas_.begin(), chispas_.end(), [](const auto& c) { return c.second > 20; }),
                   chispas_.end());
  }

  // Piccolo en su orbita (Viewer_UpdateCharactersAndCamera 0x32D820 / Viewer_PoseScheduler 0x32D6B0): radio 60 + 10
  // sin(fase), +-45 grados (0.7853982 en Viewer_UpdatePoseRotationInput 0x32DC80), esquivas de lado al azar 0..3
  // (Viewer_PickRandomDirection 0x32D540: Memory() & 3, distinta de la anterior); el planificador del estado 1 guarda
  // 120.0f (0x42F00000) en +0x14 y cambia de direccion cada 120 cuadros. El giro de cada esquiva es fRam005d9954 y
  // decae (dt x Viewer_GetParamBC) / 70 por cuadro; el 2.0 / 60 rad por cuadro del port es una aproximacion.
  // [VERIFICAR] en pantalla el ritmo de las esquivas (120 cuadros) y el angulo recorrido.
  // dia1E4: la esquiva ORIGINAL de Piccolo (EU Viewer_UpdatePoseRotationInput 0x32DC80: planificador, direccion al azar,
  // entradas a los estados 1/2 y el giro) ejecutada del ELF por src/iw_mips.cpp con la cvar dbz3_iw_logica_original.
  // Una llamada por campo PAL (5 de cada 6 cuadros). Piccolo va en la esfera de 60 del ojo y se dibuja con la posicion de
  // antes de girar (Viewer_UpdateCharactersAndCamera 0x32D820). Sin cvar, con el modo de prueba solIWX o si el
  // interprete falla, el port de abajo.
  bool PiccoloMips(uint8_t* base, uint32_t mgr) {
    namespace pc = dbz3::iwmips::piccolo;
    namespace fz = dbz3::iwmips::freezer;
    if (juego_ != 9 || fase_ != kJuego || probar_reaccion_piccolo_ || !pc::g_esquiva || !fz::g_activa || !fz::g_activa())
      return false;
    const uint32_t x1 = mgr + kX0 + kXPaso;
    dbz3::anim::EstadoCuerpo e;
    const bool hay = dbz3::anim::LeerEstadoCuerpo(base, x1, 0, e) && e.hay;
    // +0xC74 como lo mide el port: la esquiva acaba con su clip; el reposo (0x3E8, en bucle) al dar la vuelta.
    int c74 = 0;
    if (pc_estado_ == 2) {
      if (hay && !pc_clip_ && e.clip != pc_clip_rep_) pc_clip_ = e.clip;
      c74 = hay && pc_clip_ && (e.clip != pc_clip_ || e.cuadro + 1.0f >= float(e.cuadros));
    } else {
      if (hay && !pc_clip_rep_) pc_clip_rep_ = e.clip;
      if (hay && e.clip == pc_clip_rep_) {
        if (pc_vueltas_ == ~0u) pc_vueltas_ = e.vueltas;
        c74 = e.vueltas != pc_vueltas_;
      }
    }
    auto bits = [](float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; };
    auto flt = [](uint32_t b) { float v; std::memcpy(&v, &b, 4); return v; };
    if (az_ != pc_az_p_) { pc_glob_[0] = bits(az_ - 1.5707964f); pc_d_ok_ = false; }   // az_ movido por fuera (o 1.a vez)
    pc_glob_[1] = bits(elev_);
    const V3 o = Ojo(), f = Dir(g_yaw.load(), g_polar.load());
    const float azo = flt(pc_glob_[0]), el = flt(pc_glob_[1]), ce = std::cos(el);
    const float pos[3] = {o.x + 60.0f * ce * std::cos(azo), o.y + 60.0f * std::sin(el), o.z + 60.0f * ce * std::sin(azo)};
    // Dibujo a 60 Hz: entre los dos ultimos campos (como Freezer, ver MoverFreezer); sin ellos, la posicion logica.
    auto dibujar = [&] {
      float a = azo, b = el;
      if (pc_d_ok_) {
        const float k = float(pc_campo_) / 6.0f;
        a = pc_d_[0] + (pc_d_[2] - pc_d_[0]) * k;
        b = pc_d_[1] + (pc_d_[3] - pc_d_[1]) * k;
      }
      const float cb = std::cos(b);
      g_px = o.x + 60.0f * cb * std::cos(a); g_py = o.y + 60.0f * std::sin(b); g_pz = o.z + 60.0f * cb * std::sin(a);
      if (std::getenv("DBZ3_TEST_IWMINI"))   // prueba: registro de posicion por cuadro (D:/tmp/dia1E4/tiron.py)
        REXLOG_INFO("dbz3 iwdisparo: pc dibujo t={} interp={} campo={} az={:.7f} elev={:.7f} az_logica={:.7f} elev_logica={:.7f}",
                    t_, int(pc_d_ok_), pc_campo_, a, b, flt(pc_glob_[0]), flt(pc_glob_[1]));
    };
    pc_campo_ += fz::g_ntsc ? 6 : 5;   // dia1E5: con el ELF USA (NTSC) el original corre en cada cuadro
    if (pc_campo_ < 6) { dibujar(); return true; }   // cuadro sin campo PAL: el original no corre
    pc_campo_ -= 6;
    pc::EntradaPc in;
    std::memcpy(in.glob, pc_glob_, sizeof pc_glob_);
    in.estado = pc_estado_; in.c74 = c74; in.dif = pc_dif_; in.vida = float(vida_); in.t = uint64_t(t_);
    for (int i = 0; i < 3; ++i) in.pos[i] = pos[i];
    in.ojo[0] = o.x; in.ojo[1] = o.y; in.ojo[2] = o.z;
    in.obj[0] = o.x + f.x; in.obj[1] = o.y + f.y; in.obj[2] = o.z + f.z;
    if (!pc_semilla_) pc_semilla_ = semilla_ | 1u;
    uint32_t s = pc_semilla_;
    pc::SalidaPc r;
    if (!pc::g_esquiva(in, r, s)) { pc_campo_ += 6; return false; }
    pc_semilla_ = s;
    std::memcpy(pc_glob_, r.glob, sizeof pc_glob_);
    az_ = flt(pc_glob_[0]) + 1.5707964f; pc_az_p_ = az_; elev_ = flt(pc_glob_[1]);
    pc_d_[0] = azo; pc_d_[1] = el; pc_d_[2] = flt(pc_glob_[0]); pc_d_[3] = flt(pc_glob_[1]); pc_d_ok_ = true;
    dibujar();
    if (hay && pc_estado_ == 1 && c74) pc_vueltas_ = e.vueltas;
    if (r.estado == 1 && pc_estado_ != 1) {
      pc_vueltas_ = ~0u;
      dbz3::anim::Bucle(x1, 0xFB0);
      REXLOG_INFO("dbz3 iwdisparo: Piccolo esquiva(mips) fin rumbo {} vivida {} az {:.3f} elev {:.3f}", pc_glob_[6],
                  esquiva_vivida_, az_, elev_);
    } else if (r.estado == 2 && pc_estado_ != 2) {
      const uint32_t k = r.b60 - 0x3F2;
      if (k < 8 && g_x1.load()) dbz3::anim::Reproducir(g_x1.load(), 0xFC0 + k, false, 0, 0xFB0);
      pc_clip_ = 0; esquiva_vivida_ = 0;
      REXLOG_INFO("dbz3 iwdisparo: Piccolo esquiva(mips) inicio rumbo {} modo {} giro {:.4f} contador {:.1f} codigo {:X} az {:.3f} "
                  "elev {:.3f}", pc_glob_[6], pc_glob_[3], flt(pc_glob_[5]), flt(pc_glob_[8]), r.b60, az_, elev_);
    } else if (pc_estado_ == 2) {
      ++esquiva_vivida_;
    }
    pc_estado_ = r.estado;
    return true;
  }
  void MoverPiccolo(uint8_t* base, uint32_t mgr, bool quieto) {
    if (PiccoloMips(base, mgr)) return;   // dia1E4
    iwpiccolo::PasoReaccion reaccion;
    if (probar_reaccion_piccolo_ && fase_ == kJuego) {
      dbz3::anim::EstadoCuerpo clip;
      const uint64_t orden = probar_acuse_piccolo_ ? reaccion_piccolo_.OrdenReinicio() : 0;
      const bool confirmado = probar_acuse_piccolo_ ?
          dbz3::anim::LeerEstadoConAcuse(base,mgr+kX0+kXPaso,0,orden,clip) :
          dbz3::anim::LeerEstadoCuerpo(base,mgr+kX0+kXPaso,0,clip);
      reaccion = reaccion_piccolo_.Paso({clip.codigo, clip.cuadros, clip.vueltas, clip.cuadro,
          probar_acuse_piccolo_ && confirmado ? orden : 0},
          float(vida_), float(vida_max_), elev_);
      if (probar_acuse_piccolo_ && reaccion.observado)
        REXLOG_INFO("dbz3 solIWX: Piccolo acuse observado gen {} orden {} codigo {:X} cuadro {:.6f} duracion {} fin {}",
            reaccion_piccolo_.Ver().generacion,orden,clip.codigo,clip.cuadro,clip.cuadros,reaccion.regreso);
      if (reaccion.regreso) {
        dbz3::anim::Bucle(mgr + kX0 + kXPaso, 0xFB0);
        esquiva_t_ = 1; // puente al scheduler previo; NO acredita el scheduler de IW.
        REXLOG_INFO("dbz3 solIWX: Piccolo reaccion fin gen {} codigo {:X} cuadro {:.6f} duracion {} vuelve 3E8",
            reaccion_piccolo_.Ver().generacion, clip.codigo, clip.cuadro, clip.cuadros);
      }
      az_ += reaccion.delta_azimut;
      elev_ += reaccion.delta_inclinacion;
      if (reaccion.activa && reaccion.observado)
        REXLOG_INFO("dbz3 solIWX: Piccolo reaccion paso gen {} codigo {:X} cuadro {:.6f} duracion {} giro {:.6f}/{:.6f}",
            reaccion_piccolo_.Ver().generacion, clip.codigo, clip.cuadro, clip.cuadros,
            reaccion.delta_azimut, reaccion.delta_inclinacion);
    }
    picc_fase_ += 0.1f;
    if (!quieto && fase_ == kJuego && !reaccion.activa && !reaccion.regreso) {
      if (--esquiva_t_ <= 0) {
        int nueva;
        do nueva = int(Azar() & 3);
        while (nueva == esquiva_);
        esquiva_ = nueva;
        esquiva_t_ = 120;   // Viewer_PoseScheduler: +0x14 = 0x42F00000 (120.0f)
        // EU32D2C0: codigo3F2+direccion; la variante larga solo si el
        // contador de la NUEVA esquiva supera35, no por su cuadro actual.
        // EU3279F0: bajo40% scheduler1 fija120; scheduler0 deja contador0.
        const bool larga=vida_<0.4f*vida_max_;
        const uint32_t codigo=0xFC0+uint32_t(esquiva_)+(larga?4u:0u);
        if(g_x1.load())dbz3::anim::Reproducir(g_x1.load(),codigo,false,0,0xFB0);
        if (probar_reaccion_piccolo_)
          reaccion_piccolo_.Esquiva(0x3F2 + uint32_t(esquiva_) + (larga ? 4u : 0u));
        if(std::getenv("DBZ3_TEST_IWMINI"))
          REXLOG_INFO("dbz3 solIWZ: Piccolo esquiva codigo {:X} direccion {} contador {}",0x3F2+esquiva_+(larga?4:0),esquiva_,larga?120:0);
      }
      const float v = (vida_ < 0.4f * vida_max_ ? 2.25f : 2.0f) / kOrbita * 0.5f;
      if (esquiva_ == 0) az_ += v;
      if (esquiva_ == 1) az_ -= v;
      if (esquiva_ == 2) elev_ += v * 0.5f;
      if (esquiva_ == 3) elev_ -= v * 0.5f;
      az_ = std::clamp(az_, -0.7853982f, 0.7853982f);
      elev_ = std::clamp(elev_, -0.05f, kElevMax);
    }
    const float rad = probar_reaccion_piccolo_ && fase_ == kJuego ? reaccion_piccolo_.Radio()
        : juego_ == 9 ? kRadioPicc9 : kOrbita + 10.0f * std::sin(picc_fase_);
    const V3 o = Ojo();
    g_px = o.x + rad * std::cos(elev_) * std::sin(az_);
    // EU32D820 suma el ojo del visor a la esfera de radio60+expansion.
    // El radio23 previo compensaba z=37; no mezclarlo con el radio del core.
    const float centro_z = probar_reaccion_piccolo_ && fase_ == kJuego ? o.z : 0.0f;
    g_pz = juego_ == 9 ? centro_z - rad * std::cos(elev_) * std::cos(az_)
                       : o.z + rad * std::cos(elev_) * std::cos(az_);
    g_py = juego_ == 9 ? o.y + rad * std::sin(elev_)
                       : std::max(0.0f, o.y - 6.0f + rad * std::sin(elev_));
  }

  // Prueba (DBZ3_TEST_IWMINI=0x7D8,auto): gira hacia Piccolo y dispara.
  void Piloto(uint8_t* base, uint32_t mgr, float& gx, float& gy, bool& fuego) {
    const V3 o = Ojo();
    V3 p;
    if (hecho_ && hecho_->juego == 8) {
      // DS129: se apunta al HUESO del pecho del rival (el mismo que usa el 9), que es donde se DIBUJA Freezer. La
      // posicion del modulo (g_px/g_py/g_pz) es la logica y B3 la pinta con la pose ~6-7 u mas arriba: apuntando a
      // ella el disco salia "libre" al agua (prueba del coordinador: 0 aciertos). Si el modelo aun no tiene tabla,
      // se usa la del modulo.
      // 4/11: el rival se COLOCA en T7(module) (Poner de este archivo), asi que
      // el objetivo del piloto es esa misma posicion; el punto de cuerpo del
      // actor oculto puede estar sin actualizar (medido y 0) y hacia fallar el
      // tiro. El piloto es solo prueba (g_auto).
      p = {g_px.load(), g_py.load(), g_pz.load()};
      p = T7(p);
    } else {
      const uint32_t rol = probar_reaccion_piccolo_ ? uint32_t(rol_piccolo_prueba_) : 1u;
      if (!HuesoMundo(base,mgr+kX0+kXPaso,rol,p)) p={g_px.load(),g_py.load(),g_pz.load()};
    }
    const V3 d = Mas(p, Por(o, -1.0f));
    const float yaw = std::atan2(d.x, d.z), polar = std::acos(std::clamp(d.y / std::max(Largo(d), 1e-3f), -1.0f, 1.0f));
    float dy = yaw - g_yaw.load();
    while (dy > 3.14159265f) dy -= 6.2831853f;
    while (dy < -3.14159265f) dy += 6.2831853f;
    gx = std::clamp(-dy / 0.0165f, -1.0f, 1.0f);
    gy = std::clamp(-(polar - g_polar.load()) / 0.0165f, -1.0f, 1.0f);
    fuego = std::abs(dy) < 0.08f && ++auto_t_ % 2 == 0;
    // La focal deja acabar cada reaccion; solo limita las pulsaciones del piloto.
    if (probar_reaccion_piccolo_ && reaccion_piccolo_.Ver().pose_estado == 3) fuego = false;
    if (hecho_ && hecho_->juego == 8)
      fuego = std::abs(dy) < 0.03f && std::abs(polar - g_polar.load()) < 0.03f && auto_t_ % 2 == 0;
  }

  // EU 0x3675f0 MgFly_FinishAndApplyResult / EU 0x367790 MgFly_StateApplyResultAndExit: escribe el bloque de resultado
  // (tipo 2, rango = 4 - indice de umbral, puntos de la tabla del #MSS, premio) y Battle_ApplyResultRewards. El port lo
  // hace con iwmini::Resultado; en el 3 el fallo es +0x3C del bloque (100) y no hay TIME.
  // DS152: el TIME y el rango de 0x7D7/0x7D8/0x7DE salen del tiempo JUGADO, tambien cuando se acaba por tiempo o por
  // quedarse sin tiros. Antes el 0x7D8 ponia 0'00"00 (segundos_ = 0 en el tiempo agotado) y el 0x7D7/0x7D8 un fallo
  // "sin banda" (100). El original: Viewer_StoreResultToConfig 0x324C50 = Menu_ClassifyValueByThreshold(+0x10/60, tabla
  // 0x5D97DC = {999,75,60,45,30,0}); Viewer_CommitShootResult 0x3496B0 = Effect_ClassifyRange (misma tabla 0x5D9AEC,
  // leida del ELF 0x483700); MgShoot_ApplyResult 0x30A200 = MgShoot_GetRankFromTime(p). El premio es SIEMPRE el de la
  // tabla del rango (no depende de superar): 100 solo si el valor no cae en ninguna banda (0x5D986C/0x5D9B34/0x5D9390).
  void Acabar(uint8_t* base, uint32_t mgr, const Datos& d) {
    iwmini::Resultado r;
    r.clave = d.clave;
    r.fila = d.fila;
    // Tiempo jugado real. Limites: 9 = Mesa/Viewer (0x480AB0, 180..90), 8 = 0x483660 (180 para las 6 dificultades),
    // 15 = MgShoot 0x30D150 (40..160). restante_ es lo que queda en el reloj del minijuego.
    const float limite = d.juego == 15 ? float(kTiempo15[d.dif]) : d.juego == 8 ? 180.0f : float(kLimite[d.dif]);
    const float jugado = std::clamp(limite - std::max(restante_, 0.0f), 0.0f, limite);
    int idx = -1;   // Menu_ClassifyValueByThreshold / Effect_ClassifyRange
    if (d.juego != 15 && jugado > 0 && jugado < kUmbral[0])
      for (int i = 4; i >= 0; --i)
        if (jugado <= kUmbral[i]) {
          idx = i;
          break;
        }
    if (d.juego == 3) {   // MgFly_StateSegmentProgress: el primer umbral <= aciertos (r), rango 4 - r; si no, fallo
      int rr = 5;
      for (int i = 0; i < 5 && rr == 5; ++i)
        if (d.umbral[i] <= aciertos_) rr = i;
      r.clave = d.clave;
      r.fila = d.fila;
      r.superado = rr < 5;
      r.rango = rr < 5 ? 4 - rr : 0;
      r.puntos = rr < 5 ? d.puntos[rr] : d.fallo;
      r.score = r.puntos;
      r.zeni = r.puntos;   // fracaso tambien sale del #MSS+3C, no de una cifra fija
      r.objetos = aciertos_;
      r.dificultad = d.dif;
      r.con_tiempo = false;   // el resultado de ladrillos no lleva TIME (captura de DS19: BRICKS/EARNED/TOTAL)
      r.monedas = aciertos_;
      res_ = r;   // DS129: la tabla de resultados de IW que pinta Dibujar en kFinal
      t_fin_ = ImGui::GetTime();
      {
        std::lock_guard<std::mutex> lk(g_mu);
        g_res = r;
      }
      REXLOG_INFO("dbz3 iwdisparo: fin: {} ladrillos, rango {}, puntos {}", aciertos_, "CBASZ"[r.rango], r.puntos);
      EmpezarRecompensa();
      return;
    }
    if (d.juego == 15) {   // MgShoot_GetRankFromTime con p = altura / 400; p = 0 exacto no tiene rango
      const float q = p15_;
      idx = q <= 0 || q >= 1 ? -1 : q <= 0.0001f ? 4 : q <= 0.1f ? 3 : q <= 0.4f ? 2 : q <= 0.7f ? 1 : 0;
    }
    // Superado = se gano el minijuego (Piccolo derribado / 7 discos / Cell contenido). El rango/premio NO lo decide:
    // son del tiempo (8/9) o de la altura (15), como en los Store/ApplyResult citados.
    if (d.juego == 15)
      r.superado = p15_ >= 0 && p15_ < 1;
    else if (d.juego == 8)
      r.superado = aciertos_ >= 7;
    else
      r.superado = vida_ <= 0;
    r.rango = std::max(idx, 0);
    if (idx >= 0) {
      const int raw = int(float(d.juego == 15 ? kBase15[idx] : d.juego == 8 ? kBaseDisco[idx] : kBase[idx]) * kMult[d.dif]);
      r.puntos = (raw % 100 >= 50 ? raw / 100 + 1 : raw / 100) * 100;
    } else {
      r.puntos = 100;
    }
    r.centesimas = int(jugado * 100.0f);
    r.score = r.puntos;
    r.zeni = idx >= 0 ? r.puntos : 100;   // premio de la tabla del rango; sin banda, 100 (0x5D986C)
    // DS129: la pantalla de estos tres no lleva fila de objetos (la unica etiqueta leida, 334/1 t17 fila 2, es BRICKS
    // DESTROYED, solo del 0x7D3): -1 = no se pinta (antes salia "BRICKS DESTROYED <disparos>"). El nº de aciertos va
    // en `monedas`, que es lo que lee la prueba.
    r.objetos = -1;
    r.dificultad = d.dif;
    // DS152: la pantalla del 0x7DE (Stop Cell!) solo lleva RANGO + ZENI (referencia del original 28:06-28:15); la de
    // 0x7D7 y 0x7D8, RANGO + TIEMPO + ZENI (15:00-15:09 y 15:58-16:07). El 15 no pinta TIME.
    r.con_tiempo = d.juego != 15;
    r.monedas = d.juego == 8 ? aciertos_ : disparos_;
    res_ = r;   // DS129: la tabla de resultados de IW que pinta Dibujar en kFinal
    t_fin_ = ImGui::GetTime();
    {
      std::lock_guard<std::mutex> lk(g_mu);
      g_res = r;
    }
    REXLOG_INFO("dbz3 iwdisparo: fin: {} en {:.2f} s (limite {:.0f}), rango {}, premio {}",
                r.superado ? "DERRIBO" : "FALLO", jugado, limite, "CBASZ"[r.rango], r.zeni);
    EmpezarRecompensa();
  }

  void EmpezarRecompensa() {
    recompensa_ = {};
    recompensa_obra_=0;
    recompensa_antes_ = uint16_t(g_mando.load());
    recompensa_hitos_ = 0;
    REXLOG_INFO("dbz3 iw recompensa: inicio clave {:#x} superado {} earned {} total {}; consumidor EU285F30",
                res_.clave, res_.superado, res_.zeni,
                res_.saldo >= 0 ? res_.saldo : P::LeerProgreso("iw_zeni", 0) + res_.zeni);
  }

  void CuadroRecompensa(uint8_t* base, uint32_t mgr) {
    const uint16_t botones = uint16_t(g_mando.load());
    // Registro de botones B3 (+12): A es 0x4000, no el bit XInput 0x1000.
    const bool confirmar = (botones & ~recompensa_antes_ & 0x4000) != 0;
    recompensa_antes_ = botones;
    const float antes=recompensa_.cuadro;
    // El piloto confirma SOLO una vez terminado; nunca acelera el conteo.
    recompensa_.Avanzar(1.0f, confirmar || (g_auto && recompensa_.cuadro >= 420));
    if(confirmar&&antes>0&&antes<420) recompensa_obra_=420;
    ++recompensa_obra_; // BackgroundDisplay sigue vivo aunque el conteo se retenga en420
    if(recompensa_obra_==450&&std::getenv("DBZ3_SOLIW9_TALLY"))
      REXLOG_INFO("dbz3 iw recompensa: AMA450 con conteo {:.3f} retenido; relojes independientes",recompensa_.cuadro);
    if (std::getenv("DBZ3_SOLIW9_TALLY"))
      REXLOG_INFO("dbz3 iw recompensa: muestra reloj {:.3f} earned_cifras {} total_cifras {} cerrado {}",
                  recompensa_.cuadro, recompensa_.Cifras(res_.zeni, false),
                  recompensa_.Cifras(res_.saldo >= 0 ? res_.saldo : P::LeerProgreso("iw_zeni", 0) + res_.zeni, true),
                  recompensa_.cerrado);
    for (int i = 0; i < 3; ++i) {
      const int hito = i == 0 ? 120 : i == 1 ? 320 : 420;
      if (recompensa_.cuadro >= hito && !(recompensa_hitos_ & (1 << i))) {
        recompensa_hitos_ |= 1 << i;
        REXLOG_INFO("dbz3 iw recompensa: hito {} reloj {:.3f} earned_cifras {} total_cifras {} cerrado {}",
                    hito, recompensa_.cuadro, recompensa_.Cifras(res_.zeni, false),
                    recompensa_.Cifras(res_.saldo >= 0 ? res_.saldo : P::LeerProgreso("iw_zeni", 0) + res_.zeni, true),
                    recompensa_.cerrado);
      }
    }
    if (recompensa_.cerrado && !g_fin_pedido.exchange(true)) {
      REXLOG_INFO("dbz3 iw recompensa: confirmado reloj {:.3f}; entrega al conector", recompensa_.cuadro);
      REX_STORE_U8(mgr + kFin, uint8_t(res_.superado ? 1 : 2));
    }
  }

  // ---- dibujo ----
  ImVec2 Px(const S::Area& a, float x, float y) const { return a.At(x, y); }
  void Trozo(ImDrawList* dl, const ps2::Tex& t, float x0, float y0, float x1, float y1, ImVec2 p, float k) const {
    if (!t) return;
    dl->AddImage(t.id(), p, ImVec2(p.x + (x1 - x0) * k, p.y + (y1 - y0) * k), ImVec2(x0 / t.w, y0 / t.h),
                 ImVec2(x1 / t.w, y1 / t.h));
  }
  const ps2::Tex& Hud(int j) const {
    static const ps2::Tex vacia;
    return j < int(hud_.size()) ? hud_[size_t(j)] : vacia;
  }
  void Cifras(ImDrawList* dl, int n, ImVec2 centro, float k) const {   // DATA_USA 9/1 t04 (0-4 arriba, 5-9 abajo)
    static constexpr float kX[10][2] = {{4, 29},  {39, 55},  {68, 92},  {101, 124}, {131, 157},
                                        {5, 29},  {37, 60},  {69, 93},  {100, 124}, {132, 156}};
    char s[8];
    std::snprintf(s, sizeof s, "%d", std::max(n, 0));
    float ancho = 0;
    for (const char* p = s; *p; ++p) ancho += kX[*p - '0'][1] - kX[*p - '0'][0];
    float x = centro.x - ancho * k * 0.5f;
    for (const char* p = s; *p; ++p) {
      const int c = *p - '0';
      const float y0 = c < 5 ? 1 : 33;
      Trozo(dl, Hud(4), kX[c][0], y0, kX[c][1], y0 + 29, ImVec2(x, centro.y - 14.5f * k), k);
      x += (kX[c][1] - kX[c][0]) * k;
    }
  }

  void Dibujar(ImGuiIO& io, uint8_t* base, uint32_t mgr, const Datos& d) {
    ps2::Camara cam = ps2::CamaraCombate(base, io.DisplaySize);
    const ps2::Camara cam_mundo = cam;
    if (juego_ == 8 && !(g_cine.load() & 4) && !g_camclip.load())   // 31/10: el modulo pinta en sus coordenadas; la vista es la del mundo (T7)
      for (int i = 0; i < 3; ++i) {
        cam.m[12 + i] += cam.m[8 + i] * 359.137f;
        cam.m[0 + i] = -cam.m[0 + i], cam.m[8 + i] = -cam.m[8 + i];
      }
    if (cam.ok) {   // rayos, chispas y manos (modelos de IW) sobre el combate
      ImDrawList* bg = ImGui::GetBackgroundDrawList();
      escena_.Empezar();
      for (int k = 0; k < (juego_ == 8 || (g_cine.load() & 4) ? 0 : 2); ++k) {
        const ps2::Modelo& m = k == 0 ? d.mano_i : d.mano_d;
        if (!m.malla) continue;
        ps2::Malla w = m.malla;
        // 15: CameraCtrl_Create (EU 0x3105A0): a 5,5 por la mirada y (0, -1,1, -3,7) -> (0, -1,1, 1,8). Su rotacion
        // (+0x50: -1, pi, 0) no se aplica porque el modelo ya mira a +z en el espacio de la camara. [VERIFICAR] el giro.
        if (juego_ == 15 && ManosToon15() && !ramp15_.empty()) {   // eIW2: color por vertice = 255 - rampa[l], l = 0,5 + 0,5 n.l en vista
          static const V3 L{Env15("DBZ3_TEST_MANOS15_LX", -0.4512f), Env15("DBZ3_TEST_MANOS15_LY", 0.5514f), Env15("DBZ3_TEST_MANOS15_LZ", 0.7018f)};
          static const float oscuro = Env15("DBZ3_TEST_MANOS15_OSC", 0.75f);
          const float cxr = std::cos(-1.0f), sxr = std::sin(-1.0f), baja = (sub15_ == 1 || sub15_ == 2 || kikoho_ > 0) ? oscuro : 0.0f;
          for (auto& t : w.tris)
            for (int c = 0; c < 3; ++c) {
              if (!t.con_color || t.col[c] != IM_COL32(255, 210, 170, 255)) continue;   // solo la piel
              const float y1 = t.n[c].y * cxr - t.n[c].z * sxr, z1 = t.n[c].y * sxr + t.n[c].z * cxr;
              const float l = 0.5f + 0.5f * (-t.n[c].x * L.x + y1 * L.y - z1 * L.z) - baja;
              t.col[c] = ramp15_[size_t(std::clamp(int(l * 64.0f), 0, 63))];
            }
        }
        const V3 o = juego_ == 15 ? V3{0, -1.1f, 1.8f} : SitioMano(k, retroceso_[k]);
        // x5 alrededor del ojo: la misma imagen, pero lejos del corte de Escena::Poner (no pinta lo pegado a la camara)
        for (auto& t : w.tris)
          for (V3& p : t.p) {
            if (juego_ == 15) {   // EU 0x310650: M = T(+0x40: 5,5 por la mirada) x camara x Ry(pi) x Rx(-1) x T(0; -1,1; -3,7)
              static const int conv = [] { const char* e = std::getenv("DBZ3_TEST_MANOS15"); return e ? std::atoi(e) : ManosToon15() ? 4 : 1; }();
              const float vy = p.y - 1.1f, vz = p.z - 3.7f, cx = std::cos(-1.0f), sx = std::sin(-1.0f);
              const float y1 = vy * cx - vz * sx, z1 = vy * sx + vz * cx;
              if (conv == 4) {   // eIW2 (apagado por defecto): pose medida contra el original (IoU 0,52 -> 0,94): esc en x/y, dx, dy
                static const float aj[3] = {Env15("DBZ3_TEST_MANOS15_ESC", 1.06f), Env15("DBZ3_TEST_MANOS15_DX", 0.0f),
                                            Env15("DBZ3_TEST_MANOS15_DY", 0.0f)};
                const float xv = (-p.x + aj[1]) * 5.0f * aj[0], yv = (y1 + aj[2]) * 5.0f * aj[0], zv = -(5.5f + z1) * 5.0f;
                const float* m = cam.m;
                const float ax = xv - m[12], ay = yv - m[13], az = zv - m[14];
                p = {m[0] * ax + m[1] * ay + m[2] * az, m[4] * ax + m[5] * ay + m[6] * az, m[8] * ax + m[9] * ay + m[10] * az};
              } else
              p = conv == 1 ? Cam({-p.x, y1, 5.5f + z1}, 5.0f) : conv == 2 ? Cam({-p.x, y1, 5.5f - z1}, 5.0f) : Cam({p.x, -y1, 5.5f + z1}, 5.0f);
            } else {
              if (juego_ == 9) {   // EU 0x32C5D0 + 0x32CC50: T(ojo - 4,5 por la mirada) x camara x Ry(pi) x Rx(-0,2) x T(-+2; -4; 11 + retroceso)
                const float vx = p.x + (k == 0 ? -2.0f : 2.0f), vy = p.y - 4.0f, vz = p.z + 11.0f + 1.5f * retroceso_[k], cx = std::cos(-0.2f), sx = std::sin(-0.2f);
                p = Cam({-vx, vy * cx - vz * sx, -4.5f + vy * sx + vz * cx}, 5.0f);
              } else {
                p = Cam({p.x + o.x, p.y + o.y, p.z + o.z}, 5.0f);
              }
            }
          }
        if (juego_ == 15)   // [VERIFICAR]: el bit alto del vtype de ITEMB00 (translucido en ps2::Malla) no es de piel
          for (auto& t : w.tris) t.trans = false;
        escena_.Poner(cam, w, k == 0 ? tx_i_ : tx_d_, {0, 0, 0}, 0, 1.0f);
      }
      // Kikoho (15): las mallas del paquete del minijuego (935/1, conos MODEL_L0x_EFT de 25 x 120 hacia -y, atlas 935/0)
      // desde las manos de Ten por su mirada hasta el suelo, 32 cuadros
      if (juego_ == 15 && kikoho_ > 0 && d.efecto.malla) {
        const V3 e{mx_, kAltura15, mz_}, f = Unir(V3{0, 15.0f - kAltura15, -200.0f});
        const float largo = (kAltura15 - 15.0f) / std::max(-f.y, 0.1f);
        OrientadoY(cam, d.efecto, Mas(e, Por(f, 30.0f)), f, largo / 120.0f * (0.6f + 0.4f * float(kikoho_) / 32.0f) * (std::getenv("DBZ3_TEST_KIKOHO_ESC_M") ? float(std::atof(std::getenv("DBZ3_TEST_KIKOHO_ESC_M"))) : 1.0f));
      }
      // 3: el ladrillo de IW (890/1 ITEMB03_BRICK) en su punto del tramo
      if (juego_ == 3 && ladrillo_vivo_ && d.ladrillo.malla) {
        V3 lp = LadCam(cam, io);
        lp.y -= 4.8f;   // el modelo tiene la base en y = 0 (caja 0..9,6): su centro en el punto de la mira
        escena_.Poner(cam, d.ladrillo.malla, tx_lad_, lp, float(t_) * 0.05f, 1.0f);
        static int64_t log_t = 0;
        if (const int64_t ms = int64_t(dbz3::RelojT(ImGui::GetTime()) * 1000); ms - log_t > 1000) {   // F1: sonda (1 por segundo)
          log_t = ms;
          ImVec2 sp;
          float kk = 0;
          const bool vis = cam.P(lp, sp, kk);
          REXLOG_INFO("dbz3 iwdisparo: ladrillo pintado en ({:.0f},{:.0f},{:.0f}) -> pantalla ({:.0f},{:.0f}) k {:.2f} vis {} "
                      "(ps2 {:.0f},{:.0f} z {:.0f}), {} tex, prof {}", lp.x, lp.y, lp.z, sp.x, sp.y, kk, vis, lad_x_, lad_y_,
                      lad_z_, tx_lad_.size(), ps2::EstadoProfundidad());
        }
      }
      // rayos: el modelo del rayo de IW orientado a su marcha (la cabeza, z = 0, delante; la cola hacia -z)
      for (const Rayo& r : rayos_) Orientado(cam, d.efecto, r.p, r.v, 0.6f);   // [VERIFICAR] escala 0.6 en pantalla
      // Kienzan: el modelo de IW, plano y girando; cargando, encima de la mano (x5 alrededor del ojo, como las manos)
      if (disco_ >= 1 && d.efecto.malla) {
        giro_disco_ += 0.5f;
        if (disco_ == 3)
          escena_.Poner(cam, d.efecto.malla, tx_ef_, disco_p_, giro_disco_, 2.2f);
        else
          escena_.Poner(cam, d.efecto.malla, tx_ef_, Cam({1.4f, 1.6f, 7.0f}, 5.0f), giro_disco_,
                        5.0f * (0.6f + 1.2f * float(std::min(carga_, 40)) / 40.0f));
      }
      escena_.Pintar(bg);
      for (const auto& e : efx_) ame::Pintar(*e, cam, tx_atlas_, tx_suma_, bg, 160.0f * io.DisplaySize.y / 720.0f);
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      for (const auto& c : chispas_) {   // chispa (9/1 t21)
        ImVec2 s;
        float k = 0;
        if (!cam.P(c.first, s, k)) continue;
        const float rr = std::max(6.0f, (2.0f + 0.15f * float(c.second)) * k);
        const ImU32 col = IM_COL32(255, 255, 255, int(255 * (1.0f - float(c.second) / 21.0f)));
        if (Hud(21)) fg->AddImage(Hud(21).id(), ImVec2(s.x - rr, s.y - rr), ImVec2(s.x + rr, s.y + rr), ImVec2(0, 0),
                                  ImVec2(1, 1), col);
      }
    }
    if (fase_ != kFinal && !(g_cine.load() & 4)) HudPantalla(io, base, mgr, cam);   // PCSX2: sin HUD en entradas y finales
    // final: el clip de cierre (1001/1002 o 1301/1302) se ve entero y luego fundido a negro (tapa el final de combate
    // de B3); fin_dur_ = 140 cuando no hay clip.
    if (fase_ == kDerribo || fase_ == kFinal) {
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      if(juego_==3 && fase_==kDerribo && fin3_inicio_<0)
        mensaje::DibujarObra(io,d.hit_ama,tx_avisos3_,segundos_>0?7:8,float(t_));
      // Con clip de cierre: el clip se ve entero en kDerribo (fundido a negro al final) y la tabla va en kFinal. Sin
      // clip (el 8): la tabla sale ya en kDerribo, que si no serian 140 cuadros de negro.
      if (fin_id_ >= 0 && fase_ == kDerribo) {
        return;
      }
      // El combate sigue en estado 4 hasta confirmar el panel. El reloj es el
      // de recompensa de IW, sin x4 y sin saltar las cifras desde el inicio.
      if (res_.clave) {
        const S::Area a = S::GameArea(io);
        const ImVec2 q0 = a.At(0, 0), q1 = a.At(1200, 900);
        iwmini::DibujarResultadoIW(fg, q0.x, q0.y, q1.x - q0.x, q1.y - q0.y, res_, recompensa_obra_ / 60.0f, false);
      }
      // Sin fundido propio: la tabla se queda hasta que el combate acaba (g_on = false, lo cierra el callback de
      // VigilarCombate) y entonces el mapa/menu ensena su pantalla de resultados.
    }
  }

  // Modelo m en `pos` con su -y hacia `dir` (los conos de los efectos de IW), escala `esc`.
  void OrientadoY(const ps2::Camara& cam, const ps2::Modelo& m, const V3& pos, const V3& dir, float esc) {
    const V3 f = Unir(dir), r = Unir(V3{f.z, 0, -f.x});
    const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    ps2::Malla w = m.malla;
    for (auto& t : w.tris)
      for (V3& p : t.p) p = Mas(pos, Por(Mas(Por(r, p.x), Mas(Por(f, -p.y), Por(u, p.z))), esc));
    escena_.Poner(cam, w, tx_ef_, {0, 0, 0}, 0, 1.0f);
  }
  // Modelo m en `pos` con su +z hacia `dir` (y su +y hacia arriba), escala `esc`.
  void Orientado(const ps2::Camara& cam, const ps2::Modelo& m, const V3& pos, const V3& dir, float esc) {
    if (!m.malla) return;
    const V3 f = Por(dir, 1.0f / std::max(Largo(dir), 1e-4f));
    V3 r{f.z, 0, -f.x};   // f x arriba
    r = Por(r, 1.0f / std::max(Largo(r), 1e-4f));
    const V3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    ps2::Malla w = m.malla;
    for (auto& t : w.tris)
      for (V3& p : t.p) p = Mas(pos, Por(Mas(Por(r, p.x), Mas(Por(u, p.y), Por(f, p.z))), esc));
    escena_.Poner(cam, w, tx_ef_, {0, 0, 0}, 0, 1.0f);
  }

  void HudPantalla(ImGuiIO& io, uint8_t* base, uint32_t mgr, const ps2::Camara& cam) {
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const S::Area a = S::GameArea(io);
    const float k = 1.6f * a.s;
    // solIWZ DATA_USA9/0: objeto2 centro(470,34),rect+-152/28,
    // escala0.8; objetos27..33 estrellas(373..560,31),rect+-20.
    // EU34B260: municion x=575-37*i,y=417. Lienzo600x450.
    auto aciertos_municion=[&] {
      Trozo(fg,Hud(19),0,0,304,56,Px(a,696.8f,23.2f),k);
      static constexpr float xs[7]={373,404,435,467,498,529,560};
      for(int i=0;i<std::min(aciertos_,7);++i)
        Trozo(fg,Hud(20),0,0,40,40,Px(a,(xs[i]-16)*2,30),k);
      for(int i=0;i<tiros_;++i)
        Trozo(fg,Hud(24),0,0,40,40,Px(a,(575-37*float(i)-16)*2,(417-16)*2),k);
    };
    // reticula (objeto 9, centro): anillo; la cruz si un hueso de Piccolo cae dentro (10 px + su radio en pantalla)
    bool dentro = false;
    if (cam.ok && juego_ != 15 && juego_ != 3) {
      const uint32_t x1 = mgr + kX0 + kXPaso;
      const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
      for (uint32_t i = 0; i < 22 && !dentro; ++i) {
        const uint32_t ad = x1 + 1616 + 16 * i;
        ImVec2 s;
        float kk = 0;
        if (!cam.P({LoadF(base, ad), LoadF(base, ad + 4), LoadF(base, ad + 8)}, s, kk)) continue;
        dentro = std::hypot(s.x - c.x, s.y - c.y) <=
                 (juego_ == 8 ? (kDiscoRadio + kRadioHueso[i]) * kk : 10.0f * a.s + kRadioHueso[i] * kk);
      }
      const float x0 = dentro ? 60 : 7, x1r = dentro ? 107 : 48;
      const float w = (x1r - x0) * k * 0.8f, h = 56 * k * 0.8f;
      Trozo(fg, Hud(1), x0, 0, x1r, 56, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), k * 0.8f);
    }
    // TIME: el aro azul (t05) arriba en el centro con los segundos (referencias 083 y la del Kikoho: x 600 de 1200)
    if (juego_ == 9 && hit_t_ > 0 && tx_hit_) {
      const auto& obra=datos_->hit_ama;
      const float t=float(datos_->hit_cuadros-hit_t_);
      auto arbol=anim::ama::CrearArbol(obra,obra.Objeto(5),t);
      anim::ama::MarcarArbol(arbol,t);
      std::vector<anim::ama::Entrada> entradas;
      anim::ama::ActualizarNodo(arbol,arbol.raiz,{},entradas);
      auto at=[&](float x,float y){return Px(a,x*2,y*2);};
      for(const auto& e:entradas) {
        if(e.SinTextura() || e.textura!=7)continue;
        anim::ama::Quad q;anim::ama::DibujarEntrada(e,q);
        auto color=[&](int i){return std::clamp(int(e.tinte[i]*255+e.rgba[i]+.5f),0,255);};
        fg->AddImageQuad(tx_hit_.id(),at(q.xy[0][0],q.xy[0][1]),at(q.xy[1][0],q.xy[1][1]),
            at(q.xy[2][0],q.xy[2][1]),at(q.xy[3][0],q.xy[3][1]),
            ImVec2(q.uv[0][0]/tx_hit_.w,q.uv[0][1]/tx_hit_.h),ImVec2(q.uv[1][0]/tx_hit_.w,q.uv[1][1]/tx_hit_.h),
            ImVec2(q.uv[2][0]/tx_hit_.w,q.uv[2][1]/tx_hit_.h),ImVec2(q.uv[3][0]/tx_hit_.w,q.uv[3][1]/tx_hit_.h),
            IM_COL32(color(0),color(1),color(2),color(3)));
      }
    }
    const ImVec2 aro = Px(a, 564, 36);
    Trozo(fg, Hud(5), 0, 0, 72, 72, aro, k);
    Cifras(fg, int(std::ceil(restante_)), ImVec2(aro.x + 36 * k, aro.y + 36 * k), k * 0.62f);
    if (juego_ == 3) {   // ladrillos: mira 2D (9/1 t01: anillo; fijado, la cruz), disparos y ladrillos que quedan
      mensaje3_.Dibujar(io,datos_->mensajes3);
      const float esc = io.DisplaySize.y / 450.0f;
      const ImVec2 c(io.DisplaySize.x * 0.5f + (mira_x_ - 300.0f) * esc, io.DisplaySize.y * 0.5f + (mira_y_ - 225.0f) * esc);
      const float x0 = fijado_ ? 60 : 7, x1r = fijado_ ? 107 : 48;
      const float w = (x1r - x0) * k * 0.8f, h = 56 * k * 0.8f;
      Trozo(fg, Hud(1), x0, 0, x1r, 56, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), k * 0.8f);
      aciertos_municion();
      const ImVec2 aro = Px(a, 564, 36);   // ladrillos que quedan (MgFly_AddToCounter) en el aro azul, arriba al centro
      Trozo(fg, Hud(5), 0, 0, 72, 72, aro, k);
      Cifras(fg, quedan_, ImVec2(aro.x + 36 * k, aro.y + 36 * k), k * 0.62f);
      return;
    }
    if (juego_ == 15) {   // MSN016: energia de Ten (cara t22 en su aro t12, barra), peligro (arco t09 + escala t07)
      const ImVec2 b0 = Px(a, 150, 812);
      Trozo(fg, Hud(6), 0, 0, 240, 40, b0, k);
      const float e = std::clamp(energia_, 0.0f, 1.0f);
      if (Hud(3) && e > 0)
        fg->AddImage(Hud(3).id(), ImVec2(b0.x + 24 * k, b0.y + 14 * k), ImVec2(b0.x + (24 + 192 * e) * k, b0.y + 26 * k),
                     ImVec2(0, 0), ImVec2(e, 1));
      Trozo(fg, Hud(22), 0, 0, 64, 64, Px(a, 28, 752), k);
      Trozo(fg, Hud(12), 0, 0, 88, 80, Px(a, 6, 730), k);
      // medidor como el de IW (referencia del usuario 1_ten_kikoho_danger, area 1200x900): el arco t09 a la derecha,
      // 11 segmentos de calor dentro de su canal negro (IW los pinta por codigo hasta el nivel: UiFade_SetRatio), los
      // numeros t07 fila a fila dentro del panel azul, DANGER (t11) fijo arriba, SAFE (t10) fijo abajo y el triangulo
      // t08 a la izquierda del canal, a la altura del nivel (auditoria10/C)
      constexpr float kS = 1.75f;                           // unidades del area por px de t09
      const ImVec2 o9 = Px(a, 890, 300);
      const float ks = kS * a.s;                            // px de pantalla por px de t09
      Trozo(fg, Hud(9), 0, 0, 176, 312, o9, ks);
      auto canal = [](float y) {                            // centro del canal negro de t09 (px de t09): arco ")"
        const float u = (y - 156.0f) / 130.0f;
        return 84.0f - 45.0f * u * u;
      };
      const float q = std::clamp(cy_ / kAltura15, 0.0f, 1.0f);   // nivel: la altura de Cell (como antes)
      constexpr int kSeg = 11;
      constexpr float y_top = 38.0f, y_bot = 278.0f, paso = (y_bot - y_top) / kSeg;
      static constexpr ImU32 kCol[kSeg] = {IM_COL32(245, 245, 245, 255), IM_COL32(245, 245, 245, 255),
                                           IM_COL32(250, 230, 40, 255),  IM_COL32(250, 210, 30, 255),
                                           IM_COL32(250, 185, 20, 255),  IM_COL32(250, 150, 20, 255),
                                           IM_COL32(245, 120, 20, 255),  IM_COL32(240, 90, 20, 255),
                                           IM_COL32(230, 50, 20, 255),   IM_COL32(220, 30, 20, 255),
                                           IM_COL32(205, 20, 20, 255)};
      for (int i = 0; i < kSeg; ++i) {                      // 0 abajo (blanco) .. 10 arriba (rojo)
        const float yb = y_bot - paso * float(i) - 1.5f, yt = yb - paso + 3.0f;
        const float xb = canal(yb), xt = canal(yt), hw = 8.0f;
        const bool lleno = float(i) < q * float(kSeg) + 0.5f;
        const ImU32 c = lleno ? kCol[i] : (kCol[i] & 0x00FFFFFFu) | 0x50000000u;
        auto P = [&](float x, float y) { return ImVec2(o9.x + x * ks, o9.y + y * ks); };
        fg->AddQuadFilled(P(xt - hw, yt), P(xt + hw, yt), P(xb + hw, yb), P(xb - hw, yb), c);
      }
      for (int i = 0; i < 7; ++i)                           // 350 .. 50, una fila de t07 (16 px) cada 66,7 del area
        Trozo(fg, Hud(7), 0, 16.0f * float(i), 40, 16.0f * float(i + 1), Px(a, 1072, 376 + 66.7f * float(i)), 1.5f * a.s);
      Trozo(fg, Hud(11), 0, 0, 80, 24, Px(a, 990, 300), 1.8f * a.s);   // DANGER arriba
      Trozo(fg, Hud(10), 0, 0, 72, 24, Px(a, 990, 826), 1.8f * a.s);   // SAFE abajo
      const float yq = y_bot - (y_bot - y_top) * q;         // triangulo del nivel, a la izquierda del canal
      Trozo(fg, Hud(8), 0, 0, 24, 24, ImVec2(o9.x + (canal(yq) - 8.0f - 24.0f) * ks, o9.y + yq * ks - 12.0f * a.s * 1.4f),
            1.4f * a.s);
      // rombo del Kikoho (objeto 9/1 t17, el aro amarillo de trazos) en el centro
      const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
      const float lado = 103.0f / 250.0f * io.DisplaySize.y * 0.5f * 2.0f;
      if (Hud(17) && sub15_ == 2) fg->AddImage(Hud(17).id(),   // foto del original: no esta al apuntar
                                 ImVec2(c.x - lado * 0.5f, c.y - lado * 0.5f), ImVec2(c.x + lado * 0.5f, c.y + lado * 0.5f),
                                ImVec2(55.0f / 168.0f, 0), ImVec2(105.0f / 168.0f, 1));
      return;
    }
    if (juego_ == 8) {   // MSN009: aciertos (estrellas t20 sobre la barra t19, 7 para ganar) y discos que quedan
      aciertos_municion();
      return;
    }
    if (juego_ == 9) {   // fotos del original: vida de Piccolo arriba a la derecha, espejada; la barra se vacia hacia el retrato
      const ImVec2 m0 = Px(a, 668, 18), m1(m0.x + 240 * k, m0.y + 40 * k);
      fg->AddImage(Hud(6).id(), m0, m1, ImVec2(1, 0), ImVec2(0, 1));
      const float ev = std::clamp(float(vida_) / float(std::max(vida_max_, 1)), 0.0f, 1.0f);
      if (Hud(3) && ev > 0)
        fg->AddImage(Hud(3).id(), ImVec2(m1.x - (24 + 192 * ev) * k, m0.y + 14 * k), ImVec2(m1.x - 24 * k, m0.y + 26 * k), ImVec2(ev, 0), ImVec2(0, 1), IM_COL32(90, 235, 70, 255));
      const ImVec2 q0 = Px(a, 1056, 40);
      Trozo(fg, Hud(23), 0, 0, 64, 64, ImVec2(q0.x + 20 * k, q0.y + 8 * k), k);
      fg->AddImage(Hud(12).id(), q0, ImVec2(q0.x + 88 * k, q0.y + 80 * k), ImVec2(1, 0), ImVec2(0, 1));
      return;
    }
    // vida de Piccolo: cara (t23) en su aro (t12) y la barra (marco t06, relleno t03) abajo a la izquierda
    const ImVec2 b0 = Px(a, 150, 812);
    Trozo(fg, Hud(6), 0, 0, 240, 40, b0, k);
    const float e = std::clamp(float(vida_) / float(std::max(vida_max_, 1)), 0.0f, 1.0f);
    if (Hud(3) && e > 0)
      fg->AddImage(Hud(3).id(), ImVec2(b0.x + 24 * k, b0.y + 14 * k), ImVec2(b0.x + (24 + 192 * e) * k, b0.y + 26 * k),
                   ImVec2(0, 0), ImVec2(e, 1));
    Trozo(fg, Hud(23), 0, 0, 64, 64, Px(a, 28, 752), k);
    Trozo(fg, Hud(12), 0, 0, 88, 80, Px(a, 6, 730), k);
  }

  const Datos* hecho_ = nullptr;
  uint32_t partida_ = 0;
  ps2::Tex tx_hit_;
  int hit_t_ = 0;
  std::vector<ps2::Tex> tx_i_, tx_d_, hud_, tx_ef_, tx_lad_, tx_atlas_, tx_suma_;
  std::vector<std::shared_ptr<ame::Instancia>> efx_;
  std::shared_ptr<ame::Instancia> aura_;
  float giro_disco_ = 0;
  const Datos* datos_ = nullptr;
  // Lanza el #AME del hijo `hijo` del paquete en `pos` hacia `dir` (Effect_DispatchById: el id elige el hijo)
  std::shared_ptr<ame::Instancia> Lanzar(int hijo, const V3& pos, const V3& dir) {
    if (!datos_ || hijo < 0 || hijo >= int(datos_->efectos.size()) || !datos_->efectos[size_t(hijo)]) return nullptr;
    auto in = std::make_shared<ame::Instancia>();
    in->ef = &datos_->efectos[size_t(hijo)];
    in->mezcla_gs = true;   // suma real del GS y afectores: el brillo rojo de la mano sale naranja sobre el cielo, como el original
    in->origen = pos;
    in->dir = Unir(dir);
    efx_.push_back(in);
    return in;
  }
  void Efectos() {   // un cuadro de los efectos
    for (auto& e : efx_) ame::Cuadro(*e, semilla_);
    efx_.erase(std::remove_if(efx_.begin(), efx_.end(), [](const auto& e) { return !e->vivo; }), efx_.end());
  }
  ps2::Tex clear_;
  std::vector<ps2::Tex> tx_avisos3_;
  iwmini::Resultado res_;   // DS129: resultado de Acabar, para la tabla de DibujarResultadoIW en kFinal
  double t_fin_ = 0.0;      // DS129: ImGui::GetTime() al acabar (reloj de la tabla de resultados)
  iwmini::RelojRecompensaIW recompensa_;
  float recompensa_obra_=0;
  uint16_t recompensa_antes_ = 0;
  int recompensa_hitos_ = 0;
  int juego_ = 9;
  ps2::Escena escena_;
  Fase fase_ = kListo;
  std::shared_ptr<ame::Instancia> brillo_[2];
  int t_ = 0, vida_ = 0, vida_max_ = 1, disparos_ = 0, mano_ = 0, esquiva_ = -1, esquiva_t_ = 0, auto_t_ = 0;
  int esquiva_dur_ = 0, esquiva_vivida_ = 0;
  int fz_estado_ = 1, fz_dir_ = -1; bool fz_larga_ = false, fz_preparada_ = false; float fz_vel_ = 0, fz_t_ = 0; uint32_t fz_vueltas_ = ~0u, fz_clip_ = 0, fz_clip_rep_ = 0; int fz_traza_ = 0;
  // dia1E3 (esquiva original en el interprete): LFSR propio de Memory(), campo PAL, anterior y azimut en ejes del original.
  uint32_t fz_semilla_ = 0, fz_dir_prev_ = 0; int fz_campo_ = 0; float fz_az_o_ = 0, fz_az_p_ = 1e9f;   // diagnostico: cuadros pedidos y vividos
  // dia1E4 (esquiva original de Piccolo en el interprete): globales 0x5D9940.., estado de pose, LFSR, campo PAL y fin de clip.
  uint32_t pc_glob_[10] = {}, pc_semilla_ = 0, pc_clip_ = 0, pc_clip_rep_ = 0, pc_vueltas_ = ~0u;
  int pc_estado_ = 1, pc_campo_ = 0, pc_dif_ = 2; float pc_az_p_ = 1e9f;
  bool fz_interp_ = false, fz_ant_ok_ = false; float fz_az_ant_ = 0, fz_el_ant_ = 0;   // dia1E4: dibujo de Freezer a 60 Hz
  bool pc_d_ok_ = false; float pc_d_[4] = {};   // dia1E4: dibujo de Piccolo a 60 Hz (campo anterior y ultimo, ejes del original)
  int rol_piccolo_prueba_ = 1; // Solo objetivo del piloto opt-in; no fuerza el impacto.
  float acc_ = 0, restante_ = 0, segundos_ = 0, az_ = 0, elev_ = 0, picc_fase_ = 0;
  iwpiccolo::ReaccionEnMotor reaccion_piccolo_;
  bool probar_reaccion_piccolo_ = false;
  bool probar_acuse_piccolo_ = false; // solo testmini9 + reaccion + DBZ3_TEST_IWX_PICCOLO_ACUSE
  float espera_[2] = {}, retroceso_[2] = {};
  int tiros_ = 0, aciertos_ = 0, fallos_seguidos_ = 0, disco_ = 0, carga_ = 0, pausa_ = 0, bloqueo_ = -1;
  struct Marca {
    int tipo = 0;
    float x = 0, z = 0, paso = 0, dura = 0;
  };
  std::array<Marca, 5> marcas_{};
  std::vector<ImU32> ramp15_;   // eIW2: colores de la rampa de piel de las manos (255 - rampa), 64 columnas
  uint32_t cell_semilla_ = 0;   // N2: LFSR de Memory() propio de las marcas de Cell (cvar)
  int marca_ = 0, marca_t_ = 0, cst_ = 0, cc0_ = 0, ce_t_ = 0, sub15_ = 0, t15_ = 0, kikoho_ = 0, dif15_ = 2;
  float mx_ = 0, mz_ = 200, cx_ = 0, cy_ = 0, cz_ = 0, cvy_ = 0, golpe_y_ = 0, rumbo_ = 0, wx_ = 0, wz_ = 0;
  float energia_ = 1, p15_ = -1;
  bool baja_usada_ = false;
  uint16_t antes15_ = 0;
  int orig3_ = -1;   // T3: -1 sin decidir, 0 port, 1 logica original, 2 la original fallo a mitad (port)
  int acum3_ = 0;
  uint16_t held3_ = 0;
  uint32_t sem3_ = 0;
  bool aviso_fin3_ = false;
  int quedan_ = 0;
  float tramo_espera_ = 0;
  float mira_x_ = 300, mira_y_ = 225, progreso_ = 0, lad_vel_ = 1, lad_x_ = 0, lad_y_ = 0, lad_z_ = 0, tiro_t_ = 0;
  int tramos_disponibles_ = 0;
  mensaje::Reproductor mensaje3_;
  bool bloqueo_tiro3_=false,tramo_preparado3_=false,cargar_postmensaje3_=false;
  int fin3_inicio_=-1;
  anim::ama::Arbol aviso_final3_;
  float lad_s_ = 18.0f;   // tamano del ladrillo que se proyecta (#MSS +0x08) para el radio de fijado
  // DS123: entrada/cierre por clips del #AMC (ver PasoIntro/PasoFinal)
  bool sin_cine_ = false;
  int fin_id_ = -1, fin_dur_ = 140;
  std::array<float, 3> lad_a_{}, lad_b_{};
  bool ladrillo_vivo_ = false, fijado_ = false;
  bool tirada_ = false, fuego_antes_ = false;
  float combustible_ = 0;
  V3 disco_p_, disco_v_;
  uint32_t semilla_ = 1;

  std::vector<Rayo> rayos_;
  std::vector<std::pair<V3, int>> chispas_;
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

// Carga (modelos de las manos y HUD de la copia del jugador) y el combate. 0x7D8: MSN010 (Piccolo); 0x7D7: MSN009.
// EU 0x327590 s_MSN010_UPDATE / 0x34B3F0 s_MSN009_UPDATE / 0x30CCF0 s_MSN016_UPDATE (entradas de los tres): abren el
// trabajo, arrancan el clip 0 de la UI (`UiAnim_Start(0,1,0,1,0)`), ponen +0x14cd = 1 y +0x359d = 1 en el trabajo (los
// dos flags de control de los luchadores), Timer_SetSlot(2.0, trabajo, 0) y GameState_SetFlag2(0). Ese slot 0 lo
// descuenta Timer_TickSlot (EU 0x2EF990: `[trabajo+0x30+slot*4] -= 1` por cuadro) y Viewer_StateReady (0x325840) da el
// mando al agotarlo: son 2 CUADROS, no 2 s. Aqui la carga y el estado de "listo" (kListo) los monta Empezar/Cuadro*; el
// +0x14cd/+0x359d son los flags de ocultar al rival / jugador de B3 (Veil): `coincide`.
// EU 0x30CC40 Minigame_SetupScene (MSN016): carga camara y modelos, engancha camara->jugador (`Get_uRam0049cda4`:
// +0x80 / +0x40), pone al jugador la pose 10 y al rival la 0 desde auRam005d92f0 / auRam005d9320. En el port eso es
// Cuadro15 (Ten = camara, Cell = luchador 1 con CellEstado(0/1)): los transform fijos de IW no se copian (el escenario de
// B3 manda); `coincide` en el reparto de papeles.
// EU 0x369150 ShootingMinigamesFunc: solo guarda el puntero del trabajo y apaga el flag de estado; `motor` (Empezar).
// EU 0x3691A0 "DestroyTheBricks"Minigame: carga por RAMOpenDATA_CMNFolder los #AMB del 890 (ladrillo ITEMB03, #MSS por
// dificultad) y elige el ID con MinigameIDPicker; es el juego 3 (0x7D3). DS123: ademas de los datos del #MSS/#ITM que
// ya leia, el port saca de aqui la camara del #MSS (DS126: el #AMC 890/5 es la casa de Kaio y no se reproduce).
// EU 0x369370 Minigame_ClearByRank: la nota de la load-wait (0xB/0xC/0xD) elige el bloque 3/1/2 del #MSS.
// EU 0x369150 ShootingMinigamesFunc: guarda el puntero del trabajo y apaga el flag de estado; en el port lo hace Empezar.
// EU 0x369150 ShootingMinigamesFunc
bool Empezar(int clave, int fila, int dificultad, int personaje) {
  auto d = std::make_shared<Datos>();
  d->clave = clave;
  d->fila = fila;
  d->dif = std::clamp(dificultad, 0, 5);
  d->juego = clave == 0x7D7 ? 8 : clave == 0x7DE ? 15 : clave == 0x7D3 ? 3 : 9;
  P::AfsPs2 cmn, usa;
  if (!cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS") || !usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) {
    REXLOG_WARN("dbz3 iwdisparo: sin la copia de IW (DATA_CMN/DATA_USA)");
    return false;
  }
  // EU 0x3691A0 DestroyTheBricksMinigame
  if (d->juego == 3) {   // DATA_CMN 890: 1 ITEMB03_BRICK (+0 #AMT), 4 #MSS (bloque por dificultad en +0x2C)
    {
      uint8_t* base=Base();
      const bool jp=base && REX_LOAD_U8(dbz3::GA(0x824BA110)+0x2C0C9)==0;
      if(!iwazar::Global().Iniciar(P::LeerElf(P::BuscarIW()))) {
        REXLOG_WARN("dbz3 iwdisparo: SHOOT necesita el ELF de IW para inicializar Memory(32)");
        return false;
      }
      d->mensajes3=mensaje::Cargar(cmn,usa,jp);
    }
    const std::vector<uint8_t> amb = cmn.Entrada(890);
    d->ladrillo = ps2::CargarModelo(amb, 1, 0, 0);
    const std::vector<uint8_t> mss = P::HijoAmb(amb, 4);
    auto i32 = [&](size_t o) { int32_t v = 0; if (o + 4 <= mss.size()) std::memcpy(&v, mss.data() + o, 4); return v; };
    auto f32 = [&](size_t o) { float v = 0; if (o + 4 <= mss.size()) std::memcpy(&v, mss.data() + o, 4); return v; };
    if (mss.size() < 0x44 || std::memcmp(mss.data(), "#MSS", 4) != 0) {
      REXLOG_WARN("dbz3 iwdisparo: sin el #MSS de 890");
      return false;
    }
    // EU 0x369370 Minigame_ClearByRank (0xB/0xC/0xD -> bloque 3/1/2)
    int bdif = d->dif;
    size_t b = size_t(i32(0x2C + 4 * size_t(bdif)));
    while (bdif > 0 && b + 0x64 > mss.size()) {   // el #MSS EU solo trae 5 bloques (la copia USA tiene 6)
      --bdif;
      b = size_t(i32(0x2C + 4 * size_t(bdif)));
    }
    if (bdif != d->dif)
      REXLOG_INFO("dbz3 iwdisparo: la copia de IW no tiene bloque para la dificultad {}; se usa la {}",
                  d->dif, bdif);
    d->mss_raw = mss;
    d->mss_bdif = bdif;
    d->n_tramos = i32(b);
    d->lad_s = f32(b + 0x08);   // MgFly_SetupNextNode: +0x510 = bloque +0x08 (tamano proyectado del ladrillo)
    d->lad_duracion=f32(b+0x0C);
    d->lad_vel_mira=f32(0x10);
    if(!(d->lad_duracion>0) || !std::isfinite(d->lad_duracion) || !(d->lad_vel_mira>0) || !std::isfinite(d->lad_vel_mira)) return false;
    for (int k = 0; k < 5; ++k) d->umbral[k] = i32(b + 0x14 + 4 * size_t(k)), d->puntos[k] = i32(b + 0x28 + 4 * size_t(k));
    d->fallo = i32(b + 0x3C);
    // EU 0x367e30 MgFly_StateIntroSetup: la camara del 3 sale del bloque del #MSS (+0x40 ojo, +0x4c objetivo,
    // +0x58 fov y +0x5c giro, en grados; el giro se limita a 5..175 y el port no lo aplica porque el #MSS lo trae 0).
    for (int k = 0; k < 3; ++k) d->mss_ojo[k] = f32(b + 0x40 + 4 * size_t(k));
    for (int k = 0; k < 3; ++k) d->mss_obj[k] = f32(b + 0x4c + 4 * size_t(k));
    const float fov_deg = std::clamp(f32(b + 0x58), 5.0f, 175.0f);
    d->mss_fov = fov_deg * 2.0f * 3.14159265f / 360.0f;
    d->mss_giro = f32(b + 0x5c) * 2.0f * 3.14159265f / 360.0f;
    d->lad_focal = 512.0f;   // PINE49CEAC, lienzo600x450; no es la focal deducida de mss_fov
    REXLOG_INFO("dbz3 iwdisparo: #MSS camara ({:.2f},{:.2f},{:.2f}) -> ({:.2f},{:.2f},{:.2f}), fov {:.1f} grados, giro "
                "{:.1f}; focal {:.2f}", d->mss_ojo[0], d->mss_ojo[1], d->mss_ojo[2], d->mss_obj[0], d->mss_obj[1],
                d->mss_obj[2], fov_deg, d->mss_giro * 360.0f / 6.2831853f, d->lad_focal);
    // DS126: el #AMC 890/5 (270 c.) NO es la entrada de esta galeria. Leido del fichero, su ojo va de
    // (194.5, 7.1, -55.2) a (96.0, 7.1, -29.0) con el objetivo (101.0, 146.2, -193.5) -> (105.4, 33.2, -197.7):
    // es la escena de la casa de Kaio, a cientos de unidades del campo 897 y nunca acaba en la camara del
    // #MSS (0,30,100)->(0,35,0). Reproducirla sobre el campo vacio es la pantalla negra/suelo del principio.
    // EU 0x367e30 MgFly_StateIntroSetup copia directamente la camara del #MSS: esa YA es la entrada.
    d->intro_dur = 0;
    const int pool = std::clamp(i32(b + 0x60), 0, 200);
    for (int k = 0; k < pool; ++k) {
      std::array<float, 7> t{};
      for (int j = 0; j < 7; ++j) t[size_t(j)] = f32(b + 0x64 + 0x1C * size_t(k) + 4 * size_t(j));
      d->tramos.push_back(t);
    }
    REXLOG_INFO("dbz3 iwdisparo: #MSS: {} ladrillos, umbrales {} {} {} {} {}, {} trayectorias", d->n_tramos, d->umbral[0],
                d->umbral[1], d->umbral[2], d->umbral[3], d->umbral[4], d->tramos.size());
    {   // F1: medidas del ladrillo y de las trayectorias (para la escala/camara)
      float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
      for (const auto& t : d->ladrillo.malla.tris)
        for (const V3& p : t.p) {
          const float v[3] = {p.x, p.y, p.z};
          for (int q = 0; q < 3; ++q) lo[q] = std::min(lo[q], v[q]), hi[q] = std::max(hi[q], v[q]);
        }
      REXLOG_INFO("dbz3 iwdisparo: ladrillo {} tri, caja ({:.1f},{:.1f},{:.1f})-({:.1f},{:.1f},{:.1f}), {} texturas",
                  d->ladrillo.malla.tris.size(), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], d->ladrillo.tex.size());
      for (size_t k = 0; k < std::min<size_t>(d->tramos.size(), 4); ++k) {
        const auto& t = d->tramos[k];
        REXLOG_INFO("dbz3 iwdisparo: tramo {}: v {:.2f} ({:.0f},{:.0f},{:.0f}) -> ({:.0f},{:.0f},{:.0f})", k, t[0], t[1], t[2],
                    t[3], t[4], t[5], t[6]);
      }
    }
  }
  // DS123: camaras de entrada/cierre de los otros tres. Todas salen del #AMC general DATA_CMN 915 hijo 2 (Camera_*),
  // con el id que pone cada estado en UiAnim_Start (los valores de los INT_00499xxx leidos del ELF EU):
  //  - 0x7D8 (MSN010): Viewer_StateIntro 0x325c30 -> 1000; Viewer_StateCountdown 0x325ab0 -> 1003; final 0x324e30 ->
  //    1001 (derribo) / 1002 (tiempo agotado).
  //  - 0x7DE (MSN016): MgShoot_St_PreIntro 0x30b410 -> 1300; MgShoot_St_Intro 0x30b1c0 -> 1303; MgShoot_St_GameOver
  //    0x30a3d0 -> 1302 (Cell contenido, camara a ras de suelo) / 1301 (Cell arriba o sin energia; DS126).
  //  - 0x7D7 (MSN009): el descriptor EU 0x49A600 declara TRES camaras (950/951/952). La entrada es la 950
  //    (retroceso de520 cuadros). EU3498B0: a los140, si aciertos>=7
  //    INT49A68C=951 y pose6; si no INT49A690=952 y pose7.
  {
    const std::vector<int> ids = d->juego == 3  ? std::vector<int>{750, 751, 752}
                                 : d->juego == 8  ? std::vector<int>{kIntro7, kMira7Clip, kCierre7}
                                 : d->juego == 9 ? std::vector<int>{1000, 1001, 1002, 1003}
                                                 : std::vector<int>{1300, 1301, 1302, 1303};
    LeerClipsDisparo(P::HijoAmb(cmn.Entrada(915), 2), ids, d->cams);
    auto anade = [&](std::vector<int>& v, std::initializer_list<int> l) {
      for (int id : l)
        if (d->cams.count(id)) v.push_back(id);
    };
    // 31/10: PINE: la entrada empieza por el clip 0 del libro 1 de cada modulo (sin actores ni HUD): DATA_CMN 873 (0x7D7,
    // 440), 874 (0x7D8, 440), 878 (0x7DE, 400) y 890/5 (0x7D3, 270). Rectifica DS126, que lo miraba sin la camara activa.
    LeerClipsDisparo(d->juego == 3 ? P::HijoAmb(cmn.Entrada(890), 5) : cmn.Entrada(d->juego == 8 ? 873 : d->juego == 9 ? 874 : 878), {0}, d->cams);
    anade(d->intro, {0});
    if (d->juego == 3) {
      anade(d->intro, {750});
      anade(d->fin_bien, {751});
      anade(d->fin_mal, {752});
    } else if (d->juego == 8) {
      anade(d->intro, {kIntro7});   // DS126: 950 (no 952); su ultimo cuadro es la camara de juego
      anade(d->fin_bien,{kMira7Clip});
      anade(d->fin_mal,{kCierre7});
    } else if (d->juego == 9) {
      // DS126: 1000 (440 c., y=143) es la entrada y encaja con la camara de juego (0,150,37). El 1003
      // esta a y=76.8 (a ras de suelo), NO en el escenario del 9 (y=143-150): no se reproduce hasta
      // aclarar su papel (antes metia un salto de 70 u al suelo en la cuenta atras). [H]
      anade(d->intro, {1000, 1003});   // 31/10: PINE: 1003 si se reproduce (140, Piccolo 0x43D)
      anade(d->fin_bien, {1001});
      anade(d->fin_mal, {1002});
    } else {
      anade(d->intro, {1300, 1303});
      // 31/10: rectifica DS126 midiendo en PCSX2: contenido = 1301 con Ten 0x691 en pie (la camara mira a Ten a y 152);
      // Cell arriba = 1302 con Ten 0x692 derribado en el suelo.
      anade(d->fin_bien, {1301});
      anade(d->fin_mal, {1302});
    }
    d->intro_dur = 0;
    for (int id : d->intro) d->intro_dur += d->cams[id].dur;
    std::string s;
    for (int id : d->intro) s += fmt::format(" {} ({}f)", id, d->cams[id].dur);
    REXLOG_INFO("dbz3 iwdisparo: camaras de IW (915/2): entrada{} = {} cuadros; fin bien {} / mal {}", s, d->intro_dur,
                d->fin_bien.empty() ? -1 : d->fin_bien[0], d->fin_mal.empty() ? -1 : d->fin_mal[0]);
  }
  if (d->juego == 15) {   // manos de Ten: DATA_CMN 895 (0x37F, Minigame_LoadShootingData): 1 ITEMB00, 0 #AMT
    const std::vector<uint8_t> manos = cmn.Entrada(895);
    // con el color de material de cada parte: la textura 895/0 es blanca con lineas y la piel va en el material
    // (auditoria10/C: las manos salian blancas y planas)
    ps2::LeerAmo(P::HijoAmb(manos, 1), 0, d->mano_i.malla, nullptr, true, true, ps2::RampasPiel(P::HijoAmb(manos, 0)));
    const std::vector<uint8_t> t = P::HijoAmb(manos, 0);
    for (int j = 0, n = P::CuantasAmt(t); j < n; ++j) d->mano_i.tex.push_back(P::TexturaAmt(t, j));
  }
  if (d->juego == 9) {   // el MSN009 no carga manos (sus carpetas: 873 #AMC y 935)
    const std::vector<uint8_t> manos = cmn.Entrada(892);   // #AMB: 0 #AMT, 1 ITEMB04, 2 #AMT, 3 ITEMB05
    for (int k = 0; k < 2; ++k) {   // sin Centrar: estan en el espacio de la camara
      ps2::Modelo& m = k == 0 ? d->mano_i : d->mano_d;
      ps2::LeerAmo(P::HijoAmb(manos, uint32_t(1 + 2 * k)), 0, m.malla);
      const std::vector<uint8_t> t = P::HijoAmb(manos, uint32_t(2 * k));
      for (int j = 0, n = P::CuantasAmt(t); j < n; ++j) m.tex.push_back(P::TexturaAmt(t, j));
    }
  }
  {   // proyectil (ver Datos::efecto): sin Centrar, la cabeza del rayo / el centro del disco en el origen
    const int e = d->juego == 15 ? 935 : d->juego == 8 ? 851 : 842;   // 15: el paquete del minijuego (Kikoho)
    const std::vector<uint8_t> amb = cmn.Entrada(size_t(e));
    if (std::getenv("DBZ3_TEST_KIKOHO_MATERIAL"))
      ps2::LeerAmo(P::HijoAmb(amb, e == 935 ? 1u : e == 851 ? 6u : 7u), 0, d->efecto.malla, nullptr, true, true);
    else
      ps2::LeerAmo(P::HijoAmb(amb, e == 935 ? 1u : e == 851 ? 6u : 7u), 0, d->efecto.malla);
    const std::vector<uint8_t> t = P::HijoAmb(amb, e == 935 ? 0u : e == 851 ? 2u : 8u);
    for (int j = 0, n = P::CuantasAmt(t); j < n; ++j) d->efecto.tex.push_back(P::TexturaAmt(t, j));
  }
  // Manos opacas: en las texturas de personaje de IW el alfa de la CLUT no es transparencia (elige la fila del toon) y
  // la PS2 las pinta opacas (P4: `TexturaAmt` ya pasa el alfa 0x80 -> 255, asi que proyectil y ladrillo van tal cual).
  for (auto& t : d->ladrillo.malla.tris) t.trans = false;   // [VERIFICAR]: el ladrillo macizo (bit alto del vtype)
  for (ps2::Modelo* m : {&d->mano_i, &d->mano_d})
    for (P::Imagen& im : m->tex)
      for (size_t i = 3; i < im.rgba.size(); i += 4) im.rgba[i] = 255;
  {   // efectos de IW (#AME) del paquete del minijuego: 935, o su copia 890/6 en el 3
    const std::vector<uint8_t> pq = d->juego == 3 ? P::HijoAmb(cmn.Entrada(890), 6) : cmn.Entrada(935);
    const std::vector<uint8_t> at = P::HijoAmb(pq, 0);
    for (int j = 0, n = P::CuantasAmt(at); j < n; ++j) d->atlas.push_back(P::TexturaAmt(at, j));
    d->efectos.resize(25);
    int cuantos = 0;
    for (uint32_t h = 3; h < 25; ++h) {
      d->efectos[h] = ame::Leer(P::HijoAmb(pq, h));
      cuantos += d->efectos[h] ? 1 : 0;
    }
    REXLOG_INFO("dbz3 iwdisparo: efectos de IW: {} #AME, atlas de {} texturas", cuantos, d->atlas.size());
  }
  const std::vector<uint8_t> hud = P::HijoAmb(usa.Entrada(9), 1);
  for (int j = 0, n = P::CuantasAmt(hud); j < n; ++j) d->hud.push_back(P::TexturaAmt(hud, j));
  d->hit = P::TexturaAmt(P::HijoAmb(usa.Entrada(5), 1), 7);
  if(d->hit_ama.Cargar(P::HijoAmb(usa.Entrada(5),0)) && d->hit_ama.Objeto(5)>=0)
    d->hit_cuadros=int(anim::ama::MaxCuadros(anim::ama::CrearArbol(d->hit_ama,d->hit_ama.Objeto(5))));
  if(d->juego==3) {
    if(!d->hit_ama.Valida() || d->hit_ama.Objeto(7)<0 || d->hit_ama.Objeto(8)<0)return false;
    const auto atlas=P::HijoAmb(usa.Entrada(5),1);
    for(int i=0,n=P::CuantasAmt(atlas);i<n;++i)d->avisos3_atlas.push_back(P::TexturaAmt(atlas,i));
  }
  d->clear = P::TexturaAmt(P::HijoAmb(usa.Entrada(334), 3), 69);
  REXLOG_INFO("dbz3 iwdisparo: 0x{:X} ({}, dificultad {}): manos {}+{} triangulos, proyectil {} ({} texturas), HUD {} "
              "texturas", clave, d->juego == 15 ? "Kikoho contra Cell" : d->juego == 8 ? "Kienzan contra Freezer" : "rayos contra Piccolo", d->dif,
              d->mano_i.malla.tris.size(), d->mano_d.malla.tris.size(), d->efecto.malla.tris.size(), d->efecto.tex.size(),
              d->hud.size());
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_d = d;
    ++g_partida;
    g_res = iwmini::Resultado{};
    g_res.clave = clave;
    g_res.fila = fila;
  }
  g_fin_pedido = false;
  g_mando = 0;
  g_palanca = 0;
  dbz3::launcher::DeferUi(&CreatePanel);
  // Jugador oculto (primera persona) y el blanco. MSN010: las manos de IW (892) tienen la piel lila y las unas oscuras de
  // Freezer (y W3 dice que Freezer dispara a Piccolo): el jugador es Freezer. MSN009: Krilin contra Freezer.
  // RE PCSX2/PINE EU + iw_sles55347.c: 0x7D7/0x7D8 Stage_BeginLoad ID 7 (tabla 0x44ABC0 -> DATA_CMN 64/65), pero el
  // mundo donde se juegan es el CAMPO GRANDE de IW: Campo IW 883 (Namek en vuelo). DS126: el 0x7D8 se juega a
  // y ~ 150 (su entrada 1000 y su primera persona coinciden); el 0x7D7, segun sus TRES clips (950/951/952), a
  // y ~ 72-82 y z ~ 0-30, no en el (0,142.6,355.6) que se habia tomado de otra medida. El jugador va a sus pies
  // (kAltJug7/kZ7) y la camara de juego es el encuadre del ultimo cuadro del 950 (DS129: anclada a la cintura que se
  // dibuja, kSubeJ7/kDetrasJ7; sin la compensacion de FOV del clip, que es la de la cinematica).
  // 0x7DE usa ID 2/TOWN -> escenario 3. 0x7D3 usa Campo IW 897 si esta instalado.
  const bool disco = d->juego == 8, ten = d->juego == 15, lad = d->juego == 3;
  const int j1 = personaje >= 0 ? personaje : lad ? kGoku : ten ? kTen : disco ? kKrilin : kFreezer;
  const int j2 = lad ? kRaditz : ten ? kCell : disco ? kFreezer : kPiccolo;
  // 31/10: PINE 0x4DB280 = 7 en 0x7D7 y 0x7D8: NAMEC (tabla 0x34A430, DATA_CMN 64), no el campo 883.
  const int campo = lad ? dbz3::escenarios::CampoIW(897)
                        : (d->juego == 8 || d->juego == 9) ? std::max(P::EscenarioPorMapa(P::MapaIW(7)), -1) : -1;
  const int esc = lad ? (campo >= 0 ? campo : kLlanuras)
                      : ten ? std::max(P::EscenarioPorMapa(P::MapaIW(2)), 0) : campo >= 0 ? campo : kStageNamek;   // 31/10: PINE ID 2 es la pradera, no la ciudad
  // 31/10 iwguia: formas segun el original. La captura PCSX2 ee/i9_1 (0x7D8) muestra a Freezer en TERCERA forma (craneo alargado, sin cuernos,
  // piel rosa anillada): forma 2 de B3 (0 = 1.ª, 1 = 2.ª, 2 = 3.ª; la 5 es la nave). Antes ponia 1 (2.ª) por error. 0x7D7: el rival es Freezer
  // en 2.ª forma (video Aq0WlA3UZiU 14:16, leido por Gemini; su AMO, 242, no es el del 0x7D8, 232): forma 1. Cell de 0x7DE es SEMIPERFECTO
  // (forma 1; video 27:16: alas, cresta, cara humana; su AMO, 186, no es el del 0x7DD, 184, que es la primera). El 0x7DD sigue en forma 0.
  if (!dbz3::hub::RequestBattleEx(j1, j2, esc, true, -1, 0, 0, personaje < 0 && j1 == kFreezer ? 2 : 0, (ten || disco) ? 1 : 0)) {
    REXLOG_WARN("dbz3 iwdisparo: el conector no acepto el combate");
    return false;
  }
  REXLOG_INFO("dbz3 iw seleccion: disparo {:#x}: jugador {}, rival {}, actor {}", clave, j1, j2,
              lad ? "oculto como en IW (sin animacion propia)" : "original");
  REXLOG_INFO("dbz3 iwmini: salida puesta (minijuego 0x{:X}, escenario {})", clave, esc);
  g_on = true;
  dbz3::combate::SetMuneco(1);         // el blanco quieto (mando virtual 2): lo mueve el minijuego
  dbz3::combate::OcultarHud(true);     // sin marcador de B3: el HUD es el de IW
  P::Reglas reglas;
  reglas.capsulas = false;
  // Trajes de B3 (rec+18). Ten sin camiseta, como IW (traje 1; ver kTrajeRival de pasarela.cpp). Krilin de 0x7D7 con su armadura de Namek.
  if (ten && personaje < 0) reglas.traje_jugador = 1;
  if (disco && personaje < 0) reglas.traje_jugador = kTrajeArmaduraKrilin;
  P::VigilarCombate("iwdisparo", reglas, [](const P::Fin&) {
    dbz3::combate::SetMuneco(-1);
    dbz3::combate::OcultarHud(false);
    if (g_x1.load()) dbz3::anim::Corredor(g_x1.load(), false);
    g_cine = 0;
    g_on = false;
    g_cam15 = false;
    g_camclip = false;
    dbz3::suelo_iw::SueloFijo(0,false);
    dbz3::suelo_iw::SueloActores(nullptr, nullptr, false);
    iwmini::Resultado r;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      r = g_res;   // sin Acabar (salio desde la pausa): no superado
      g_d.reset();
    }
    iwmini::Terminar(r);
  });
  return true;
}

const bool g_reg = (iwmini::RegistrarModulo(0x7D8, &Empezar), iwmini::RegistrarModulo(0x7D7, &Empezar),
                    iwmini::RegistrarModulo(0x7DE, &Empezar), iwmini::RegistrarModulo(0x7D3, &Empezar),
                    g_auto = std::getenv("DBZ3_TEST_IWMINI") && std::strstr(std::getenv("DBZ3_TEST_IWMINI"), "auto"),
                    true);

}  // namespace
}  // namespace dbz3::iwdisparo

namespace dbz3::iwmini {
// Hilo del juego, al empezar el dibujo (desde iwmini::Fotograma, una linea en iw_minijuegos.cpp): primera persona.
void FotogramaDisparo(uint8_t* base, uint32_t estado) {
  using namespace dbz3::iwdisparo;
  if (!g_on.load() || (estado != 3 && estado != 4)) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr) return;
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  // el jugador: oculto (cuerpo A y B, como el relevo de tagteam.cpp), quieto en el centro y con el mando virtual del
  // muneco (no se mueve ni ataca); el mando 0 lo lee el minijuego
  g_x1 = x1;
  const bool c15 = g_cam15.load();
  const int juego = g_juego.load();
  // 7D7 (8): Krillin se VE (tercera persona) en el sitio que dicen sus clips (pies ~71,3, z ~3,6); 7D8 (9):
  // Freezer oculto en el origen.
  const bool mundo7 = juego == 8 && !(g_cine.load() & 4) && !g_camclip.load();
  // solPUL: B3 apoya tambien a Freezer en el mar (cintura medida -0.70..0.60,
  // frente a la posicion IW ~150). Conservar ambas alturas por separado.
  // Original n20024: raiz267.27/cintura266.91. Antes raiz158.98/cintura~0;
  // despues cintura158.60..158.74 durante393. Se conserva el espacio IW.
  constexpr bool kAlturasMsn009 = true;
  const bool probar_alturas = std::getenv("DBZ3_TEST_IWMINI") && std::getenv("DBZ3_TEST_PUL_ALTURAS");
  const V3 suelo_a = T7(V3{0, kAltJug7, kZ7}), suelo_b = T7(V3{g_px.load(), g_py.load(), g_pz.load()});
  const float sa[3] = {suelo_a.x, suelo_a.y, suelo_a.z}, sb[3] = {suelo_b.x, suelo_b.y, suelo_b.z};
  dbz3::suelo_iw::SueloActores(sa, sb, (kAlturasMsn009 || probar_alturas) && mundo7 && !g_suelo.load());
  Poner(base, x0, c15 ? V3{g_jx.load(), 0, g_jz.load()}
                      : juego == 8 ? T7(V3{0, kAltJug7, kZ7})
                                   : V3{0, 0, 0});   // 3: oculto en el origen (la camara es la del original)
  const bool lad = juego == 3;   // 3: el rival no se ve; el jugador tampoco (camara fija de galeria, como IW)
  const bool ver_jug = juego == 8;
  REX_STORE_U8(x0 + kOcultoA, ver_jug ? 0 : 1);
  REX_STORE_U8(x0 + kOcultoB, ver_jug ? 0 : 1);
  if (juego == 8) {   // DS126: el cuerpo visible mira hacia donde apunta (la camara del 950 mira a -Z)
    const int yaw = int(std::lround(g_yaw.load() * 65536.0f / 6.2831853f)) & 0xFFFF;
    REX_STORE_U16(x0 + kMiradaA, uint16_t(yaw - 15759));
    REX_STORE_U16(x0 + kMiradaB, uint16_t(yaw - 15968));
  }
  if (lad) REX_STORE_U8(x1 + kOcultoA, 1), REX_STORE_U8(x1 + kOcultoB, 1);
  const int cine = g_cine.load();
  // solIWZ: B3 vuelve a apoyar al blanco en SU suelo tras Poner. Medido
  // 7D8: posicion158 pero hueso0=0; el piloto apuntaba al agua. Como el
  // vueloIW, la consulta del suelo mantiene la altura que lleva el modulo.
  // El jugador de7D8 esta oculto; las cinematicas llevan su raiz absoluta.
  if(juego==9)dbz3::suelo_iw::SueloFijo(g_py.load(),!(cine&4) && !g_suelo.load());
  iwmini::PasoFade(base, mgr);
  if (REX_LOAD_U16(x0 + kPadIdx) != 1) REX_STORE_U16(x0 + kPadIdx, 1);
  StoreF(base, x0 + kVida, LoadF(base, x0 + kVidaMax));
  StoreF(base, x1 + kVida, LoadF(base, x1 + kVidaMax));   // la vida de Piccolo es la del minijuego
  if (!g_suelo.load()) Poner(base, x1, juego == 8 ? T7(V3{g_px.load(), g_py.load(), g_pz.load()}) : V3{g_px.load(), g_py.load(), g_pz.load()});
  if (juego == 8 && !(cine & 4)) {
    // 31/10: PINE: matriz de Krilin +Z; la de Freezer le mira a el.
    static constexpr float kId7[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const V3 fz = T7(V3{g_px.load(), g_py.load(), g_pz.load()});
    const float vx = -fz.x, vz = 355.57f - fz.z, vl = std::max(std::hypot(vx, vz), 1e-3f), sn = vx / vl, cs = vz / vl;
    const float kMira[12] = {cs, 0, -sn, 0, 0, 1, 0, 0, sn, 0, cs, 0};
    for (uint32_t i = 0; i < 12; ++i) StoreF(base, x0 + 4 * i, kId7[i]), StoreF(base, x1 + 4 * i, kMira[i]);
  }
  if (cine & 4) {
    static constexpr float kId[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    for (const uint32_t x : {x0, x1}) {
      // PINE 0x438: cintura de Freezer a y 139,5; la pista copiada da 85,1 (el #AMM de 918 sube otros 54,4 por encima de
      // la cintura, en un hueso que no se copia).
      // 31/10 medido en 0x438: original cintura = pista (136,04 a f1 .. 138,73 a f340, +0x40 = 0); port pista - 50,83 (+-0,04 en 4 cuadros).
      Poner(base, x, {0, x == x0 && juego == 9 ? 50.83f : 0.0f, 0});
      for (uint32_t i = 0; i < 12; ++i) StoreF(base, x + 4 * i, kId[i]);
    }
    REX_STORE_U8(x0 + kOcultoA, (cine & 1) ? 0 : 1);
    REX_STORE_U8(x0 + kOcultoB, (cine & 1) ? 0 : 1);
    REX_STORE_U8(x1 + kOcultoA, (cine & 2) ? 0 : 1);
    REX_STORE_U8(x1 + kOcultoB, (cine & 2) ? 0 : 1);
  }
  const uint32_t r = kPads;   // registro del mando 0
  g_mando = uint32_t(REX_LOAD_U16(r + 12)) | uint32_t(REX_LOAD_U16(r + 108)) << 16;
  g_palanca = int(REX_LOAD_U8(r + 26)) | int(REX_LOAD_U8(r + 27)) << 8;
  static uint32_t visto = 0;
  if (g_mando.load() != visto) {
    visto = g_mando.load();
    REXLOG_INFO("dbz3 iwdisparo: mando +12 0x{:04X} +108 0x{:04X}", visto & 0xFFFF, visto >> 16);
  }
  const V3 f = Dir(g_yaw.load(), g_polar.load());
  // DS129: la camara de juego del 8 es el encuadre del ultimo cuadro de su clip de entrada (950) pero anclada a la
  // CINTURA que se dibuja (X+1616 + 0), no a la posicion logica X+48: B3 pinta al muñeco con su pose (vuelo/reposo)
  // ~7 u por encima y la camara absoluta del clip quedaba a la altura de sus pies (prueba del coordinador).
  // Desfase del clip sin compensar FOV (kSubeJ7/kDetrasJ7): el del juego original (tools/dorados/iw_0x7D7.json).
  if (juego == 8) {
    // 31/10: PINE: ojo y mira fijos (en coordenadas del modulo; T7 los pasa al mundo)
    g_m8x = 0.0f, g_m8y = 150.0f, g_m8z = kZ7 + kDetrasJ7 - 1.0f;
    g_o8x = 0.0f, g_o8y = 150.0f, g_o8z = kZ7 + kDetrasJ7;
  }
  // DS126: el 8 usa la camara de juego del ultimo cuadro de su clip de entrada (950); el 9 la primera persona
  // de IW; el 3 la fija del #MSS (R17); el resto la de siempre.
  const V3 yo = juego == 8 ? V3{g_o8x.load(), g_o8y.load(), g_o8z.load()}
                  : juego == 9 ? V3{0, kAltIW, kOjoZ9}
                  : juego == 3 ? V3{kOjo3[0], kOjo3[1], kOjo3[2]}
                               : V3{0, kOjo, 0};
  // DS123: g_camclip manda mientras dura un clip del #AMC (entrada o cierre); si no, las camaras de siempre.
  const bool cc = g_camclip.load();
  const float ojo[3] = {cc || c15 ? g_ex.load() : yo.x, cc || c15 ? g_ey.load() : yo.y, cc || c15 ? g_ez.load() : yo.z};
  const float obj[3] = {cc ? g_tx.load()
                           : c15 ? g_tx.load()
                                 : juego == 3 ? kMira3[0]
                                              : juego == 8 ? g_m8x.load()
                                                           : f.x * (juego == 9 ? 38.0f : 30.0f),
                        cc ? g_ty.load()
                           : c15 ? g_ty.load()
                                 : juego == 3 ? kMira3[1]
                                              : juego == 8 ? g_m8y.load()
                                                           : juego == 9 ? kAltIW + f.y * 38.0f : kOjo + f.y * 30.0f,
                        cc ? g_tz.load()
                           : c15 ? g_tz.load()
                                 : juego == 3 ? kMira3[2]
                                              : juego == 8 ? g_m8z.load()
                                                           : juego == 9 ? kOjoZ9 + f.z * 38.0f
                                                                        : f.z * 30.0f};
  float ojo_m[3], obj_m[3];
  std::copy(ojo, ojo + 3, ojo_m), std::copy(obj, obj + 3, obj_m);
  if (mundo7) {
    const V3 a = T7(V3{ojo[0], ojo[1], ojo[2]}), b = T7(V3{obj[0], obj[1], obj[2]});
    ojo_m[0] = a.x, ojo_m[1] = a.y, ojo_m[2] = a.z, obj_m[0] = b.x, obj_m[1] = b.y, obj_m[2] = b.z;
  }
  {
    const float fc = g_fov_clip.load();
    const float fov_juego=(juego==8 || juego==9)?0.785398163f:LoadF(base,mgr+0x1C4);
    iwmini::CamaraMinijuego(true, ojo_m, obj_m, cc && fc > 0.0f ? fc : fov_juego);
    if (dbz3::gp2cam::Activo()) {
      if (cc) dbz3::gp2cam::AplicadaDisparo(cine);
      else dbz3::gp2cam::Evaluada("disparo", juego == 3 ? 890 : -1, juego == 3 ? 4 : -1,
          -1, -1, -1.0f, -1, -1, cine, ojo_m, obj_m, fov_juego);
    }
  }
  dbz3::cine::Camara(base, ojo_m, obj_m);
  const int64_t ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
  static int64_t sonda_camara = 0;
  if (std::getenv("DBZ3_TEST_IWMINI") && ahora - sonda_camara >= 500) {
    sonda_camara = ahora;
    REXLOG_INFO("dbz3 iwmini: jugador ({:.2f}, {:.2f}, {:.2f}) ojo ({:.2f}, {:.2f}, {:.2f}) objetivo ({:.2f}, {:.2f}, {:.2f}) "
                "fov {:.4f} #AMC 0",
                LoadF(base, x0 + 64), LoadF(base, x0 + 68), LoadF(base, x0 + 72), ojo[0], ojo[1], ojo[2], obj[0], obj[1],
                obj[2], LoadF(base, mgr + 0x1C4));
    {
      V3 h0{}, h1{}, h9{};
      HuesoMundo(base, x0, 0, h0);
      HuesoMundo(base, x0, 9, h9);
      HuesoMundo(base, x1, 0, h1);
      {
        float rz[3] = {};
        const bool hz = dbz3::anim::RaizActual(base, x0, rz);
        REXLOG_INFO("dbz3 iwdisparo: raiz A {} ({:.2f},{:.2f},{:.2f}) cuadro {:.1f} X48 {:.2f} X64 {:.2f}", hz, rz[0], rz[1], rz[2], LoadF(base, x0 + 0x650 + 0x608), LoadF(base, x0 + 52), LoadF(base, x0 + 68));
      }
      REXLOG_INFO("dbz3 iwdisparo: huesos cine {} A0 ({:.1f},{:.1f},{:.1f}) A9 ({:.1f},{:.1f},{:.1f}) B0 ({:.1f},{:.1f},{:.1f})", g_cine.load(), h0.x, h0.y, h0.z, h9.x, h9.y, h9.z, h1.x, h1.y, h1.z);
    }
  }
  static int64_t traza = 0;
  if (estado == 4 && ahora - traza > 2000) {
    traza = ahora;
    REXLOG_INFO("dbz3 iwdisparo: mando 0x{:04X} +108 0x{:04X} palanca {} {}; camara {:.2f} {:.2f}; Piccolo ({:.0f}, "
                "{:.0f}, {:.0f})", REX_LOAD_U16(r + 12), REX_LOAD_U16(r + 108), int8_t(REX_LOAD_U8(r + 26)),
                int8_t(REX_LOAD_U8(r + 27)), g_yaw.load(), g_polar.load(), g_px.load(), g_py.load(), g_pz.load());
  }
}
}  // namespace dbz3::iwmini
