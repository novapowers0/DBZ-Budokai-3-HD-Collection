// dbz3 - Minijuegos de VUELO de Infinite World ("# FLIGHT #", frente R12, 2026-10-16): 7 (0x7D6) "Go to Guru's
// House! Don't get caught by Vegeta!" y 17 (0x7E0) "Race with Goten? Well, do your best!". Registrados en
// iw_minijuegos (RegistrarModulo). RE: docs/14_frentes/RONDA4_PULIDO.md "R12" y E:/DBZ3HD_trabajo/R12/.
//
// Del original (ELF USA, decomp EU 0x339620-0x345000; el modulo vive en *(gp-0x6FD0) = G):
//  - Carga (USA 0x33E040 / EU 0x33E540): variante v = 0 (Vegeta) o 1 (Goten) en G+0x4F0, personaje en G+0x4F4,
//    dificultad 0..5 en G+0x4F8. Personajes (0x482860): v0 Krilin 10 o Gohan nino 2 contra Vegeta 7 (traje 3);
//    v1 Gohan 4 o Trunks nino 9 contra Goten 6. Campo de vuelo (0x482940): v0 DATA_CMN 883 (MFNMCA, Namek: nubes,
//    cielo, 112 losetas de 4000 y la casa del Gran Patriarca, 883/7 `A1_SAITYOU`), v1 881 (MFEARA, Tierra). Datos:
//    DATA_CMN 891 (#AMB: aro RING_A01 3, moneda ITEMA11 7, objetos ITEMA02/03/08/10, flecha ITEMA04, #MSF 23 + v).
//  - #MSF: cabecera (+0x14 hover 16, +0x18/0x1C/0x20 empuje de los estados 3/5/6 = 10/34/56, +0x24 28, +0x28 10,
//    +0x2C desaceleracion del estado 4 = 0.95, +0x34/0x38/0x3C freno 0.98, +0x44.. giro por estado en 1/65536 de
//    vuelta {192, 128, 96, 96, 96} (+0x48 = el 128 del estado 2/4), +0x58/0x5C
//    duracion de los objetos 2 y 3 = 180, +0x60 + 4 (tipo & 0xFF) radio de recogida) y 6 bloques por dificultad
//    (+0xA8): salida {estado, giro, x, z}, meta {flags, tipo, x, z}, L1 (puntos de control 0x100), L2 (monedas y
//    objetos), +0x14 medidor inicial, +0x18/+0x1C/+0x20 subida del medidor (base, estado 5, estado 6), +0x24 cuadros
//    de congelado, +0x28 reaparicion, +0x2C puntos de moneda {1000, 500, 100}, +0x38 rangos en segundos (el 5.o es el
//    limite), +0x4C puntos por rango, +0x60 puntos al fallar.
//  - Vuelo (Viewer_StateFlyUpdate0/3/4/5/6, EU 0x340D70, 0x3403A0, 0x3407D0, 0x33FEF0...): altura fija 400; la palanca
//    vertical da la velocidad (estado 0: hover x v|v|; 3/5/6: max(v x freno, empuje x v^2)), la horizontal gira (giro -=
//    h x tasa), R1 (0x800) = estados 5/6 (el 6 solo con el objeto 2 activo; el objeto 3 lo impide). Con la palanca < 0.25
//    se pasa al estado 4 (desaceleracion 0.95, planeo 0x323 o frenazo 0x32E, zoom -10). Palanca: zona muerta 64,
//    (v -+ 64) / 64 (Viewer_ReadControlAxes 0x33E8B0). El mundo da la vuelta (Node_WrapPositionInGrid): 16000 / 24000.
//  - Objetos (Viewer_UpdateObjectCollection 0x33B7D0): en el plano, con barrido; v0 muestra los puntos de control de
//    uno en uno, v1 todos; con todos cogidos sale la meta. Monedas suman sus puntos, 2 = velocidad (estado 6), 3 = sin
//    R1, 8/0x108 = congela el medidor 300 cuadros.
//  - Rival = MEDIDOR (FlyingMinigameSpritesScripts 0x33C660): G+0x51C += base + extra del estado 5/6 por cuadro; a 1
//    Vegeta te atrapa / Goten llega antes. v0 ademas pierde al agotar el limite. En IW el rival solo sale en las
//    escenas: el vuelo usa el sprite del medidor (DATA_USA 7/1 obj 0x07/0x08, cursor t3 y cabeza t4/t5). DS68 lo pinta
//    con HudVuelo; el luchador de B3 del rival se deja oculto (no hay cuerpo 3D en el original).
//  - Resultado (Viewer_StateCheckResult/Success/Fail 0x33C190..): meta con t <= limite: rango = 4 - (primer umbral
//    de +0x38 que supera t), puntos = monedas + puntos del rango; si no, rango -1 y +0x60 puntos.
//  - DS113: la intro es la escena 0 del guion de actores del visor (Cutscene_GotoLine EU 0x3424F0; tablas de escena
//    0x483160/0x483180/0x4831e0/0x483200 y lineas 0x483120/0x483140/0x4831a0/0x4831c0): libro 1 clip 0 (300 cuadros,
//    sin actores) + 900 (160, actor 2 Vegeta, anim 880) + 901 (620, actor 1 Krilin, anim 885); v1: 0, 1400, 1401/1404
//    y 1407. Su audio (EU 0x33CE40): musica ADX_CMN 65 (v0) / 63 (v1) por el canal 0 y las pistas del #AMB 891
//    (hijos 29/30/31 en v0, 32/33/31 en v1) por el canal 3 en el arranque, la linea 2 y la meta (0x33B2C0).
//  - DS118 (0x7E0): la visibilidad de los objetos es el byte +0x10 del #MSF (1 = todos, 891/24; 0 = uno a uno,
//    891/23); el READY? va DESPUES de la intro (90 cuadros, +0x52c) sobre la camara del vuelo; el rival tiene sus
//    frases (tasas del medidor 0,1/0,7/0,9/1,0 = ADX 1112..1115) y el jugador las suyas al cruzar aros (tabla
//    0x483230) y al primer segundo de vuelo; la animacion de cada actor de la intro es su `anim` del guion
//    (v1: 1880/1885/1886). La ventana con el retrato del rival y las escenas 2/3 siguen [H].
#include "reloj_prueba.h"
#include "iw_minijuegos.h"
#include "iw_mini_extra.h"
#include "iw_mini_vuelo.h"

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rex/hook.h>

#include "generated/dbz3_init.h"
#include "anim_iw.h"
#include "iw_camaras_gp2.h"
#include "iw_ame.h"
#include "iw_amc.h"
#include "iw_cutscene.h"
#define IWV_SOLO_DECL
#include "iw_mips_vuelo.inl"   // T4B: vuelo con la logica original (solo declaraciones)
#undef IWV_SOLO_DECL
#include <chrono>
#include "iw_gs.h"
#include "cine.h"
#include "combate.h"
#include "escenarios.h"
#include "ingame/game_style.h"
#include "eu_map.h"
#include "freecam.h"
#include "launcher/launcher_state.h"
#include "pasarela.h"
#include "ps2_modelo.h"
#include "suelo_iw.h"

namespace dbz3::hub {
bool RequestBattleEx(int id1, int id2, int stage, bool p2_com, int time_s, int hp1, int hp2, int form1, int form2);
}
// suelo_iw.cpp (D34): la malla que se DIBUJA del campo (no esta en suelo_iw.h; misma declaracion que iw_minijuegos.cpp).
namespace dbz3::suelo_iw {
float AlturaMalla(float x, float z, float sin_suelo);
}
namespace dbz3 {
bool GameDimmed();
bool OverlayOpen();
}  // namespace dbz3
namespace dbz3::iwmini {
// X de la palanca derecha (iw_minijuegos.cpp; netplay_hooks.cpp la guarda junto a la Y): el visor la usa en
// Pad_GetStickAxes (EU 0x3394F0), la de mayor magnitud de las dos.
int PalancaDerechaX();
}  // namespace dbz3::iwmini

namespace dbz3::iwmini {
// Hilo del juego, cada cuadro: lo llama Fotograma de iw_minijuegos.cpp (clima.cpp sub_820C2058, empieza el dibujo
// tras la logica: ahi la camara y los luchadores puestos son los que se pintan). Sin un vuelo en marcha no hace nada.
void PasoVuelo(uint8_t* base);
// Hilo del juego: Mando de iw_minijuegos.cpp (antes y despues de leer el mando `usuario`).
void MandoVuelo(uint8_t* base, uint32_t usuario, bool leido);
namespace {

namespace P = dbz3::pasarela;
namespace ame = dbz3::iwame::ame;
using ps2::Modelo;
using ps2::Tex;
using ps2::V3;

constexpr dbz3::GA kMgrPtr = 0x823D6CF4, kPads = 0x8238B780;
// DS117: niebla del campo de vuelo. G = 0x823A3D80; G+76 = banderas del escenario y el bit 0x20 enciende la
// niebla (clima.cpp: sub_820C2058 solo la aplica con ese bit). El anfitrion del alta (R&R BASE, registro 16) lo
// trae apagado, asi que el #MCF del 883 no se veia. La entrada de niebla (0x823232A0, 20 B) la rellena
// escenarios.cpp con color/inicio/fin; aqui se ponen los dos floats +4/+8 del #MCF (+0x20/+0x24: 0/255 en 883).
constexpr uint32_t kFogFlags = 0x823A3D80 + 76, kRecs = 0x82323760, kNieblas = 0x823232A0;
constexpr uint32_t kRec = 92, kAnfitrion = 16;   // escenarios.cpp: registro del anfitrion de las altas
constexpr uint32_t kFogFlag = 0x20;              // G+76 & 0x20: niebla activa
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kFin = 35569, kEstado = 35128, kVida = 6080, kVidaMax = 6084;
constexpr uint32_t kCopias[3] = {48, 64, 80};
constexpr uint32_t kMiradaA = 6062, kMiradaB = 6074;   // P4: X+6062 = angulo - 15759, X+6074 = angulo - 15968
constexpr uint16_t kR1 = 0x0800;                       // registro del mando +12 (disposicion de PS2)
constexpr uint16_t kArriba = 0x0010, kDerecha = 0x0020, kAbajo = 0x0040, kIzquierda = 0x0080;   // +108
constexpr float kAltura = 400.0f;      // altura de vuelo de IW (la salida y los objetos: 0x43C80000)
// DS87/DS90 - camara del vuelo, medida en el decomp EU, en los #AMO del AFS y en los fotogramas 802/815 s:
//  * FOV de IW: MinigameCamera_ (EU 0x2982E0) llama a Camera_InitFieldDefault (0x297140), que pone la camara del
//    minijuego en 40 grados (+0x120 = 0x42200000) y los pasa a radianes en +0xB4: 40 * 2pi / 360 = 0,6981317.
//    Es FOV VERTICAL (el mismo campo +0xB4 que B3 en mgr+0x1C4, cine.cpp). En 4:3 son 51,77 grados horizontales.
//  * DS90 interpreto la captura c003 (zoom 22-28) como que el 0,698 si llegaba al campo, pero DS92 encontro en
//    iw_mini_botones.cpp (s8, medido con DBZ3_S8_FOV) que escribir mgr+0x1C4 al empezar el dibujo NO cambia la
//    proyeccion del juego: el 3D ya se dibuja con el 0,785 de B3 y ese campo solo lo leen los overlays. El tamano
//    pequeno de c003 era el zoom del impulso (D=80,2) MAS el arrastre de la camara de B3 (abajo). Se deja de escribir
//    el FOV y se compensa la distancia (k_fov) para reproducir el encuadre del 40 de IW sobre la proyeccion de B3.
//  * escala del modelo: el esqueleto del Krilin de IW (#AMO 548, DATA_CMN) y el del Krilin de B3 (modelo 327,
//    roster id 10) tienen los mismos huesos y las mismas alturas (pies -5,762, cadera 1,83, cuello 4,43, cabeza
//    5,045): relacion 1,000, el modelo B3 ya se dibuja a la escala de IW. No se toca distancia/zoom por escala.
//  * pivote: FlyingMinigameCharacterHeightPosition (EU 0x341770) guarda la posicion (x, 400, z) en +0x5F0 y la
//    camara orbita ese punto. DS117: el objetivo vertical es 6,0 (entre el 5,7755 de DS87 y el ancla (0,7,0) de
//    0x482ff0: ELF EU = 00 00 00 00 00 00 e0 40 00 00 00 00 00 00 80 3f). Con la distancia corregida por el FOV y
//    por la pose (52,2 x 0,665 x 0,84 = 29,1) los pies caen al 74,9 % y la cabeza al 41,7 % del alto en 16:9; el
//    fotograma 815s mide 74,7 % / 41,3 %. DS90 llego a 8,0 midiendo solo los pies con la distancia sin corregir
//    (45,9); la diferencia de silueta es la pose retarget de B3 (8,0 u frente a 9,6 u del IW), no la escala.
constexpr uint32_t kFovMgr = 0x1C4;               // mgr+0x1C4: FOV vertical en radianes (cine.cpp)
// DS117: el 40 de Camera_InitFieldDefault (EU 0x297140, +0x120) es el campo HORIZONTAL de la pantalla 4:3 de IW.
// El vertical equivalente es 2*atan(tan(20)/ (4/3)) = 30,8 grados: en los fotogramas 779s/802s/815s el borde de la
// malla del campo (8000 u, 2,9 grados bajo el horizonte con la camara a 11,25) cae al 26 % del alto (fila 126 de
// 225), que es justo lo que da 30,8; B3 pinta con 45 vertical y lo ponia al 34 % (fila 250 de 720 en el port). DS92
// comparo 40 con 45 como si los dos fueran verticales y dejaba la distancia en 0,8787 x 52,2 = 45,9: el personaje
// salia un 25-30 % mas pequeno que en IW. El factor correcto es tan(30,8/2)/tan(45/2) = 0,665 (52,2 -> 34,7).
constexpr float kAspectoIW = 4.0f / 3.0f;
constexpr float kFovVueloIW = 0.6981317f;         // 40 grados HORIZONTALES de IW (pantalla 4:3), EU 0x297140
// DS117: objetivo 6,0 (entre el 5,7755 de DS87 y el ancla (0,7,0) de 0x482ff0) y factor 0,84 de la pose: la
// silueta vertical del Krilin de B3 en el vuelo mide 8,0 u y la del IW 9,6 u (fotograma 815s, D 52,2, 40 horizontal);
// con 52,2 x 0,665 x 0,84 = 29,1 y el objetivo 6,0 los pies caen al 74,9 % y la cabeza al 41,7 %, y el original mide
// 74,7 % / 41,3 %. Sin el 0,84 (solo FOV) Krilin quedaba un 15 % pequeno (era la pose retarget de B3, mas compacta).
// solIW9: proyeccion activaG+690 (EU12A820/12AC30/12C9E0):
// u=300+512*X/Z;v=225+512*Y/Z. No es el40 de297140.
constexpr float kRaizVueloIW = 7.0f; // vector482FF0, sin ajuste de silueta
constexpr float kFovVueloReal = 0.82809721896f; // 2*atan(225/512)
// DS113: altura de la cadera del modelo de IW (= B3; 1,83 u, medido en AGENTS §3.2 / DS87). El objetivo de los clips
// de cutscene (Cutscene_GotoLine 0x3424F0 -> FUN_00127280) encuadra el centro del cuerpo, no los pies.
constexpr float kCinturaVuelo = 1.83f;
constexpr float kEscalaModeloVuelo = 1.0f;        // altura Krilin B3 / altura Krilin IW (esqueletos identicos)
constexpr float kDistVueloIW = 52.2f;             // CameraFocusWhileFlying EU 0x339AA0 (+0x60C = 0x4250CCCD)
// DS117: la flecha es ITEMA04_ARROW (891/1 + #AMT 0): 5 u de ancho x 7,5 de largo y todo el color en los vertices
// (rojo/naranja, alfa 0,5), no la parte de ITEMA08 que usaba DS92. World_CharacterLoader (EU 0x33B580) la dibuja en
// espacio de CAMARA con _LAB_00483250 = (0, -9, -45) (ELF EU 0x483250, leido: 0x483250 = 0,-9,-45,1).
// DS124: (0,-9,-45) es un punto FIJO del espacio de camara (45 delante del eje, 9 por debajo): su angulo bajo el eje
// es atan(9/45) = 11,31 grados, que con el vertical de B3 (45) cae al (0,2/tan22,5) = 48,3 % de media altura, o sea al
// 74,2 % del alto. Medido en los fotogramas del original (contenido 400x225): centro de la flecha al 72,2 % (802s) y
// 73,1 % (815s), practicamente quieto aunque el luchador se mueva -> es un elemento de pantalla, no un objeto de mundo.
// DS128 lo confirma y lo afina: su ala de arriba empieza donde acaban las BOTAS (815s: botas 69,3 %, flecha
// 69,8..76,4 %), asi que el punto util es la proyeccion de los pies y no el centro del encuadre.
// Escala: el modelo mide 5 x 7,5 u y en el original ocupa ~9 % del ancho (36 px de 400 en 815s) y ~6,7 % del alto
// (15 px de 225). Como el tamano de pantalla no depende de la distancia, la escala crece con la distancia de la flecha
// (kFlechaPantalla x z). Con 0,030 el bbox proyectado sale ~10 % del ancho (5 u de ancho y 7,5 de largo segun el giro)
// y ~13 % del alto en el port (mas alta que la del original porque el modelo no se inclina con el cabeceo: [H] de
// DS124), con el ala de arriba a la altura de los pies: 5 x 0,030 = 0,15 rad ->
// 0,15 / (2 tan22,5 x 16/9) = 10,2 % del ancho (mas contando el largo girado).
// DS128: el punto (0,-9,-45) de 0x483250 se toma SOBRE LOS PIES que se pintan (misma direccion: los pies estan 6 u
// bajo el objetivo de la camara a 28,9 -> atan(6/28,9) = 11,7 grados, el 11,31 del original) y su centro queda
// kFlechaBajoPies por debajo de su proyeccion, de modo que el ala de arriba le TOCA las botas. Medido en el original
// (contenido 400x225, filas 67..291): botas de 815s al 69,3 % del alto y flecha 69,8..76,4 % (centro 73,1 %); en 802s,
// botas 67,1 % y flecha 69,8..74,7 %. Es decir, el ala de arriba empieza donde acaban las botas. En la prueba del
// 27/10 04:24 la flecha salia separada de el (b_vuelo: centro al 73,5 % con los pies al 83,3 %; a_salida: al 73,5 %
// con los pies al 56,3 %) porque el centro era el de la PANTALLA y el luchador no estaba donde la camara decia.
// 0,005 rad = 0,6 % del alto (1/(2 tan 22,5) = 1,207 %/rad): el centro queda a la altura de los pies y el ala de abajo
// (~6,5 % mas) no llega a la barra SAFE (su borde de arriba esta al 82,9 % del alto).
constexpr float kFlechaBajoPies = 0.005f;        // radianes bajo la proyeccion de los pies
constexpr float kFlechaPantalla = 0.030f;        // escala por unidad de profundidad (10-15 % del ancho en 16:9)
constexpr float kFlechaDetras = 2.0f;            // u de mas, detras del luchador: que el cuerpo tape la punta
constexpr float kFlechaDistMin = 8.0f, kFlechaDistMax = 200.0f;   // cordura si la vista no cuadra
// DS124/DS128/DS132: la camara DIBUJADA no es la pedida porque B3 mezcla 0,8 x la puesta con 0,2 x la suya (A3b):
// se quedaba en D 23,9 / cabeceo 10,37 en vez de 28,9 / 11,25 y NO convergia (no era una mezcla lenta: el punto fijo de
// la mezcla es otro). DS128 escribio la supuesta "otra fuente" en mgr+0x150/+0x160: en el codigo recompilado ese campo
// es un PUNTERO (sub_820F47E0 lo compara con mgr+0x260, cmplw) y no cambio nada. DS132 la ANULA invirtiendola:
// W = (pedido - 0,2 N) / 0,8, con N reconstruida de la lectura del cuadro anterior (ver InvertirMezcla). El READY?
// conserva la comprobacion como red de seguridad y con un tope corto (antes 90 cuadros: "al tope ... sin asentar").
constexpr float kTolCabeceo = 2.5f;              // grados
constexpr float kTolRumbo = 8.0f;                // grados
constexpr int kEsperaCamara = 4;                 // cuadros de READY? de mas como maximo (solo si no converge)
// DS92 añadió una realimentación de cámara sobre una cámara que el motor vuelve a mezclar cada cuadro. La lectura es
// de la cámara anterior (cine::LeerCamara) y se compara con un objetivo que ya avanzó; acumular ese error produce
// oscilación cuando la ruta/rumbo cambian. La cámara de vuelo se escribe desde un único estado (CameraFocusWhileFlying,
// EU 0x339AA0); no se integra una segunda fuente de corrección.
constexpr float kPeriodo[2] = {16000.0f, 24000.0f};   // 4 x 4 / 6 x 6 losetas de 4000 (#MCF +0x14)
constexpr float kMedio = 2952.0f;      // ancho del campo de R8 (+-1476): el mundo de IW se junta a el
// B3 no deja a los luchadores a mas de ~150 (medido con DBZ3_R12_SONDA: con 553 el juego los juntaba 200 cada uno y el
// jugador se pintaba detras de la camara): el rival va como mucho a kRivalB3 del jugador.
constexpr float kRivalB3 = 120.0f;
constexpr uint32_t kOcultoA = 3261, kOcultoB = 4925;   // sin cuerpo A ni B (tagteam.cpp Veil; P4 lo usa en el pivote)

// Tipos de objeto del #MSF
constexpr int kMeta = 0, kControl = 0x100;
bool EsMoneda(int t) { return t == 0x201 || t == 0x101 || t == 1; }

struct Obj {
  int tipo = 0, flags = 0;
  float x = 0, z = 0, r = 1;
  bool activo = false;
  float reaparece = 0;
};

// Clip de camara de un #AMC (source 891/25, el primer hijo "AMC" de DATA_CMN 891; ver
// RAMFlyingMinigamesRelated 0x306A20). Cabecera del #AMC: +0x10 nº clips, +0x14 tabla de 16 B por clip;
// clip: +8 duracion en cuadros, +12 offset. Cada pista: +8 nº claves y claves de 16 B (x,y,z,bandera) desde +16.
// El cutscene de vuelo (0x3424F0) usa el indice de la linea como clip y la duracion del clip (+8) como fin de linea.
// dia 1A: lector y muestreo unicos de iw_amc.h (FUN_00127280); `dur` y `fov` (1.a clave de la pista 3) como antes.
struct ClipVuelo {
  int dur = 0;
  float fov = 0.0f;
  dbz3::iwamc::Clip c;
};

uint32_t U32(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
  return v;
}
float F32(const std::vector<uint8_t>& b, size_t o) {
  const uint32_t u = U32(b, o);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// DS101: malla del campo de vuelo de IW (DATA_CMN 883/881, hijo 5) leida del AFS del jugador, con TODAS sus partes
// y AMGs y la transformacion de huesos del #AMO (la misma que suelo_iw::LeerSuelo). Hace falta porque la consulta de
// suelo del port (suelo_iw::kCampos) solo cubre 884/897/900/903/906/909: para 881/883 AlturaMalla()/Altura() no ven
// nada y el vuelo se quedaba en el max(400, ...) sin consultar la malla. Datos de los dos campos (medidos 26/10):
//  - 883: 811 partes, 93439 vertices -> 43625 triangulos; x/z -8000..8000, y -16.4..243.1 (la ruta de los aros entra
//    entera: -7500..6500 / -6500..7500).
//  - 881: 780 partes, 155952 vertices; x/z -12000..12000, y -52.6..357.9.
// En los dos la altura del decomp (400) queda POR ENCIMA de la malla en todo el recorrido, asi que el max no sube; la
// consulta se deja igualmente para que la regla sea real (y para 881 si algun dia sube de 370).
struct TerrenoVuelo {
  struct Tri {
    float a[3], b[3], c[3];
  };
  std::vector<Tri> tris;
  std::vector<uint32_t> ini, lista;
  float x0 = 0, z0 = 0, x1 = 0, z1 = 0, y0 = 0, y1 = 0, celda = 100.0f;
  int nx = 0, nz = 0;
  bool vale = false;

  void Rejilla() {
    if (tris.empty()) return;
    x0 = x1 = tris[0].a[0];
    z0 = z1 = tris[0].a[2];
    y0 = y1 = tris[0].a[1];
    for (const Tri& t : tris) {
      const float* v[3] = {t.a, t.b, t.c};
      for (const float* p : v) {
        x0 = std::min(x0, p[0]), x1 = std::max(x1, p[0]);
        z0 = std::min(z0, p[2]), z1 = std::max(z1, p[2]);
        y0 = std::min(y0, p[1]), y1 = std::max(y1, p[1]);
      }
    }
    const float lado = std::max(x1 - x0, z1 - z0);
    celda = std::max(100.0f, lado / 240.0f);   // 883: 160x160 celdas; 881: 240x240
    nx = std::max(1, int(std::ceil((x1 - x0) / celda)));
    nz = std::max(1, int(std::ceil((z1 - z0) / celda)));
    ini.assign(size_t(nx) * size_t(nz) + 1, 0);
    auto celdas = [&](const Tri& t, auto&& f) {
      const float xa = std::min({t.a[0], t.b[0], t.c[0]}), xb = std::max({t.a[0], t.b[0], t.c[0]});
      const float za = std::min({t.a[2], t.b[2], t.c[2]}), zb = std::max({t.a[2], t.b[2], t.c[2]});
      const int i0 = std::clamp(int((xa - x0) / celda), 0, nx - 1), i1 = std::clamp(int((xb - x0) / celda), 0, nx - 1);
      const int j0 = std::clamp(int((za - z0) / celda), 0, nz - 1), j1 = std::clamp(int((zb - z0) / celda), 0, nz - 1);
      for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) f(size_t(j) * size_t(nx) + size_t(i));
    };
    for (const Tri& t : tris) celdas(t, [&](size_t c) { ++ini[c + 1]; });
    for (size_t c = 1; c < ini.size(); ++c) ini[c] += ini[c - 1];
    lista.resize(ini.back());
    std::vector<uint32_t> llenas(ini.begin(), ini.end() - 1);
    for (uint32_t k = 0; k < tris.size(); ++k) celdas(tris[k], [&](size_t c) { lista[llenas[c]++] = k; });
  }

  // Superficie horizontal mas alta de la malla en (x, z); `sin_suelo` si cae fuera o no hay cara en planta.
  float Altura(float x, float z, float sin_suelo) const {
    if (!vale || nx <= 0) return sin_suelo;
    const int i = int((x - x0) / celda), j = int((z - z0) / celda);
    if (i < 0 || j < 0 || i >= nx || j >= nz) return sin_suelo;
    bool hay = false;
    float y = sin_suelo;
    const size_t c = size_t(j) * size_t(nx) + size_t(i);
    for (uint32_t k = ini[c]; k < ini[c + 1]; ++k) {
      const Tri& t = tris[lista[k]];
      auto lado = [&](const float* p, const float* q) { return (q[0] - p[0]) * (z - p[2]) - (q[2] - p[2]) * (x - p[0]); };
      const float s1 = lado(t.a, t.b), s2 = lado(t.b, t.c), s3 = lado(t.c, t.a);
      if (!((s1 >= -1e-3f && s2 >= -1e-3f && s3 >= -1e-3f) || (s1 <= 1e-3f && s2 <= 1e-3f && s3 <= 1e-3f))) continue;
      const float ux = t.b[0] - t.a[0], uy = t.b[1] - t.a[1], uz = t.b[2] - t.a[2];
      const float vx = t.c[0] - t.a[0], vy = t.c[1] - t.a[1], vz = t.c[2] - t.a[2];
      const float pnx = uy * vz - uz * vy, pny = uz * vx - ux * vz, pnz = ux * vy - uy * vx;
      if (std::fabs(pny) < 1e-6f) continue;
      const float h = t.a[1] - (pnx * (x - t.a[0]) + pnz * (z - t.a[2])) / pny;
      if (!hay || h > y) y = h, hay = true;
    }
    return hay ? y : sin_suelo;
  }
};

// Triangulos de todas las partes del #AMO PS2 (posiciones, transformadas al mundo por sus huesos). Mismo recorrido
// que suelo_iw::LeerSuelo con solo_suelo = false, pero sobre todos los AMGs y sin nombres.
void LeerTerrenoVuelo(const std::vector<uint8_t>& b, TerrenoVuelo& t) {
  if (b.size() < 0x40 || std::memcmp(b.data(), "#AMO", 4) != 0) return;
  const uint32_t tabla = U32(b, 0x14), namg = U32(b, 0x18), amgtbl = U32(b, 0x1C);
  if (!namg || namg > 64 || !amgtbl) return;
  auto f32 = [&](size_t o) { return F32(b, o); };
  for (uint32_t m = 0; m < namg; ++m) {
    const uint32_t a = U32(b, amgtbl + 4 * size_t(m));
    if (a + 0x20 > b.size() || std::memcmp(&b[a], "#AMG", 4) != 0) continue;
    const uint32_t nb = std::min<uint32_t>(U32(b, a + 0x10), 4096), ejes = U32(b, a + 0x14);
    std::vector<std::array<float, 12>> mundo(nb);
    for (uint32_t i = 0; i < nb; ++i) {
      const size_t e = a + ejes + 80 * size_t(i);
      const float x = f32(e), y = f32(e + 4), z = f32(e + 8), w = f32(e + 12);
      const float s[3] = {f32(e + 32), f32(e + 36), f32(e + 40)};
      const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                          2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                          2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
      std::array<float, 12> l{};
      for (int f = 0; f < 3; ++f) {
        for (int c = 0; c < 3; ++c) l[size_t(f * 4 + c)] = r[f * 3 + c] * s[c];
        l[size_t(f * 4 + 3)] = f32(e + 16 + 4 * size_t(f));
      }
      const uint32_t padre = U32(b, tabla + 32 * size_t(i) + 0x10);
      const uint32_t ip = padre >= tabla ? (padre - tabla) / 32 : ~0u;
      if (padre && ip < i) {
        const auto& mm = mundo[ip];
        std::array<float, 12> o{};
        for (int f = 0; f < 3; ++f)
          for (int c = 0; c < 4; ++c)
            o[size_t(f * 4 + c)] = mm[size_t(f * 4)] * l[size_t(c)] + mm[size_t(f * 4 + 1)] * l[size_t(4 + c)] +
                                   mm[size_t(f * 4 + 2)] * l[size_t(8 + c)] + (c == 3 ? mm[size_t(f * 4 + 3)] : 0.0f);
        mundo[i] = o;
      } else {
        mundo[i] = l;
      }
    }
    for (uint32_t i = 0; i < nb; ++i) {
      const uint32_t brazo = U32(b, a + ejes + 80 * size_t(i) + 0x34);
      const uint32_t grupo = brazo ? U32(b, a + brazo + 4) : 0;
      if (!grupo) continue;
      const auto& mm = mundo[i];
      const size_t g = a + grupo;
      const uint32_t n = std::min<uint32_t>(U32(b, g), 256), tt = U32(b, g + 4);
      for (uint32_t k = 0; k < n; ++k) {
        const size_t po = a + grupo + U32(b, g + tt + 4 * size_t(k));
        const uint32_t vtype = U32(b, po), tam = U32(b, po + 0x90);
        if ((vtype & 0xFFFF) == 0xFFFF || tam < 0x60000000 || tam >= 0x70000000) continue;
        size_t pos = po + 0xA0;
        const size_t fin = std::min(b.size(), pos + size_t(tam - 0x60000000) * 16);
        while (pos + 0x20 <= fin) {
          const uint32_t vif = U32(b, pos + 0xC), ft = U32(b, pos + 0x10), cnt = U32(b, pos + 0x14),
                         num = (vif >> 16) & 0xFF;
          if (cnt == 0 || cnt > 0xFFFF || ((vif >> 24) & 0x7F) != 0x6C || num < 1) break;
          const uint32_t paso = (num - 1) * 16 / cnt;
          const size_t vp = pos + 0x20;
          if (paso < 16 || vp + size_t(cnt) * paso > b.size()) break;
          std::vector<std::array<float, 3>> vs(cnt);
          for (uint32_t v = 0; v < cnt; ++v) {
            const size_t o = vp + size_t(v) * paso;
            const float p[3] = {f32(o), f32(o + 4), f32(o + 8)};
            for (int f = 0; f < 3; ++f)
              vs[v][size_t(f)] = mm[size_t(f * 4)] * p[0] + mm[size_t(f * 4 + 1)] * p[1] +
                                 mm[size_t(f * 4 + 2)] * p[2] + mm[size_t(f * 4 + 3)];
          }
          auto tri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
            TerrenoVuelo::Tri x{};
            for (int cam = 0; cam < 3; ++cam) x.a[cam] = vs[i0][size_t(cam)];
            for (int cam = 0; cam < 3; ++cam) x.b[cam] = vs[i1][size_t(cam)];
            for (int cam = 0; cam < 3; ++cam) x.c[cam] = vs[i2][size_t(cam)];
            t.tris.push_back(x);
          };
          if (ft == 1) {
            for (uint32_t v = 0; v + 2 < cnt; ++v) tri(v, v + 1, v + 2);
          } else {
            for (uint32_t v = 0; v + 2 < cnt; v += 3) tri(v, v + 1, v + 2);
          }
          pos = vp + size_t(cnt) * paso;
        }
      }
    }
  }
  t.vale = !t.tris.empty();
  if (t.vale) t.Rejilla();
}

struct Juego {
  int clave = 0, fila = -1, v = 0, dif = 2, pj = 1;
  int jugador = 2, rival = 7, escenario = 7;
  // #MSF
  float hover = 16, empuje[3] = {10, 34, 56}, vmin = 28, vfreno = 10, freno[3] = {0.98f, 0.98f, 0.98f};
  int giro0 = 192, giro[3] = {96, 96, 96};
  // Viewer_StateFlyUpdate4 (decomp EU 0x3407D0): estado 4 (desaceleracion). +0x560 *= #MSF +0x2C (0,95); el giro es
  // el de +0x48 (128, el del estado 2); con la palanca abajo y >= +0x24 (28) frena a +0x28 (10), anim 0x32E y +0x614
  // = +0x61C (-10); si no, al acabar la transicion vuelve al estado 0 (Viewer_StatePoseDive, caso 4).
  float freno_picado = 0.95f;
  int giro_picado = 128, anim_picado = 0x800, picado_t = 0;
  bool pose_aro = false;
  int estado_aro = 0;
  uint32_t codigo_vuelo = 0;
  bool retorno_reposo = false;
  float dur2 = 180, dur3 = 180;
  float radio[16] = {};
  float medidor0 = 0, sube[3] = {0, 0, 0}, congela = 300, reaparece = 120;
  int puntos_moneda[3] = {1000, 500, 100}, rango_s[5] = {60, 70, 80, 90, 110}, puntos_rango[5] = {}, puntos_fallo = 100;
  int salida_estado = 0, salida_giro = 0;
  float sx = 0, sz = 0;
  Obj meta;
  std::vector<Obj> objs;   // L1 (puntos de control) y luego L2
  int n_control = 0;
  float periodo = 16000, escala = 0.18f;
  bool campo = false, auto_ = false, vuelo_real = false;   // vuelo_real: el campo de vuelo 883/881 de R8
  // DS118: #MSF +0x10 (byte) = modo de visibilidad de los objetos. G+0x504 se copia de ahi en la carga
  // (`*(uint*)(G+0x504) = *(byte*)(iVar8 + 0x10)`): 1 = todos visibles desde el principio (891/24, 0x7E0) y
  // 0 = solo el primer punto de control (891/23, 0x7D6). Antes se deducia de la variante; ahora es el dato.
  bool mostrar_todos = false;
  // DS117: #MCF del campo (+0x20/+0x24), que son los +4/+8 de la entrada de niebla de B3 (883: 0/255).
  float fog_a = 0.0f, fog_b = 255.0f;
  // DS182: niebla del #MCF que el original aplica al AGUA (color +0x1C, rango en distancia +0x28/+0x2C; 881 =
  // 0x87FFFF y 2400..3700). El port no la tenia: se pinta como velo por filas sobre lo dibujado (Panel::VeloHorizonte),
  // con la distancia real del plano del agua a y=0 desde la camara del cuadro. Es la "bruma turquesa del horizonte"
  // que midio DS173 y que DS176 dejo como pendiente del pase de dibujo.
  uint32_t fog_rgb = 0x87FFFFu;
  float fog_ini = 2400.0f, fog_fin = 3700.0f;
  // DS182: efectos #AME del visor. Viewer_SetupEffectBundle (EU 0x33AA80) carga el hijo #AMB 28 de DATA_CMN 891
  // (21 hijos: 0 #AMT atlas, 1 #AMO mallas, 2 #AMT pequeno, 3..20 los 18 #AME) en ce34[0..17]. Los emisores van
  // por Viewer_UpdateEffectSlots (0x33A000) y Aura_Dash_... (0x33A950); los dispara Viewer_StatePoseBank5/6/Turn/
  // Dive (0x33F040/0x33ECC0/0x33F2C0/0x33F5E0, auras 0/1/2), Viewer_UpdateObjectCollection (0x33B7D0, aros) y la
  // carga (0x33D900: 4,6,8,0xB,0x11 fijos).
  std::vector<ame::Efecto> efx;
  std::vector<P::Imagen> fx_atlas;   // #AMT hijo 0 del paquete (47 texturas)
  std::vector<ps2::Malla> fx_modelos;
  struct FxVivo {
    int id = -1;      // indice del #AME (0..17)
    int slot = -1;    // hueco de efecto del original (para matarlo como Viewer_KillEffectAt)
    bool sigue = false;   // el origen se actualiza al cuerpo del luchador cada cuadro
    std::shared_ptr<ame::Instancia> in;
  };
  std::vector<FxVivo> fx;
  uint32_t fx_sem = 0x2F6B3D1u;
  bool fx_ok = false, fx_iniciado = false;
  int fx_estado = -1;   // estado de vuelo del cuadro anterior (auras de 5/6)
  // DS118: READY? de IW. Al acabar la intro, Viewer_StateCutsceneInput (EU 0x33CB80) deja el vuelo parado y
  // +0x52c = 0x42B40000 (90 cuadros) esperando al sprite; luego arrancan los scripts del vuelo (0x33C660).
  int listo_t = 0;
  anim::ama::Obra avisos;
  std::vector<P::Imagen> aviso_atlas;
  // EU342F90/3462B0: CMN936 (no es el HUD7 ni un retrato del cuerpo3D).
  anim::ama::Obra dialogo_ama;
  std::vector<P::Imagen> dialogo_atlas;
  P::Imagen dialogo_textos[4];
  float dialogo_pos[2] = {195,312};
  V3 dialogo_marco{};
  int dialogo_voz[4] = {600,600,600,600};
  std::deque<int> dialogos;
  int dialogo = -1, dialogo_t = 0, dialogo_salida = -1;
  int aviso_id = -1, aviso_t = 0;
  bool derrota_espera = false, derrota_vista = false;
  bool voz_inicio = false;   // frase del jugador al pasar de 1 s de vuelo (EU 0x33C660, ver PasoVuelo)
  uint8_t eventos = 0;       // dialogos del rival ya disparados (EU 0x33C660 / 0x33AFA0; bit = indice de evento)
  int voz_aro = 0;           // contador para el `Memory() % 3` de la frase de los aros (EU 0x33AFA0)
  TerrenoVuelo terreno;   // DS101: malla del 883/881 (hijo 5 del AFS de IW) para la altura de vuelo
  // #AMC de 891: clave = (libro << 16) | clip. Libro 0 = hijo 25 (todos los clips, 1408); libro 1 = hijo 26 (su
  // unico clip 0, 300 cuadros = plano de presentacion del campo). DS113: las escenas de Cutscene_GotoLine (0x3424F0)
  // guardan el libro en el descriptor de linea (0x483120: [u16 clip][u8 libro][u8 n][ptr actores]).
  std::map<int, ClipVuelo> cams;   // (libro<<16)|clip: intro v0 0/900/901, meta 210/211, victoria 902.., derrota 903...
  // DS113: guion de actores de la escena 0 del visor (tablas EU 0x483160/0x483180/0x4831e0/0x483200; lineas en
  // 0x483120/0x483140/0x4831a0/0x4831c0). Cada linea = clip de un libro + el actor que anima (0 jugador de juego,
  // 1 Krilin del guion, 2 Vegeta) y el codigo de animacion de IW (880/885/...).
  struct LineaGuion {
    int libro = 0, clip = 0, actor = -1;
    uint32_t anim = 0;   // codigo de animacion del actor (IW); 0 = linea sin actores
  };
  std::vector<LineaGuion> guion;   // escena 0 (intro)
  dbz3::iwcut::Escena cs;           // dia 1A: Cutscene_GotoLine/Update (iw_cutscene.h) sobre `guion`
  int guion_linea = 0, guion_f = 0, intro_total = 0, clip_meta = 210;
  bool intro = false, intro_done = false, poses = false;
  int intro_t = 0;
  float intro_ojo[3] = {0, 0, 0};
  // DS113: audio de la intro. EU 0x33CE40: CharacterDialoguesStart_(0, Audio_GetFilteredTableEntry(0x6b, 0x49c458[v])
  // | 0x20000, 0) = musica (ADX_CMN 65 v0 / 63 v1) y Audio_PlayDialogueOnChannel(3, #AMB 891, 0x49c460[v*3+k]) =
  // hijos ADX 29/30/31 (v0) y 32/33/31 (v1). Se copian una vez a un AFS cache para el reproductor de pasarela.
  bool voces_ok = false;
  std::filesystem::path dir_voces;
  int voz_child[3] = {0, 0, 0};   // indice del hijo de 891 (29..33) del arranque, la linea 2 y la meta
  bool mus_intro = false;
  bool cierre = false;
  int cierre_t = 0, cierre_dur = 0;
  // 31/10: EU285F30: la tabla encima de la pose de la escena 2/3, sujeta (PCSX2: 15.000 Zenies sobre Krilin), no en negro
  RelojRecompensaIW recompensa;
  bool en_panel = false;
  uint16_t panel_antes = 0xFFFF;
  int escena_cierre = 1;   // Cutscene_GotoLine: 1 meta, 2 victoria, 3 derrota
  bool resultado_visto = false;
  // modelos de IW
  Modelo m_aro, m_moneda, m_casa, m_item[4];   // item: 2, 3, 8, 10
  Modelo m_flecha;                             // DS117: ITEMA04_ARROW (891/1 + #AMT 0), colores por vertice
  std::vector<float> uv_casa_dv;
  P::Imagen clear;   // DATA_USA 334/3 t69 "CLEAR" (el mismo que P4)
  // estado (hilo del juego)
  float x = 0, z = 0, vel = 0, medidor = 0, congelado = 0, t568 = 0, t56c = 0, t_ms = 0;
  int yaw = 0, estado = 0, restantes = 0, monedas = 0, puntos = 0, cuadros = 0;
  // T4B: logica original del vuelo (iw_mips_vuelo.inl). orig: 0 sin probar, 1 en marcha, 2 fallo/apagada
  std::vector<uint8_t> msf_raw;
  int orig = 0;
  bool congelar_pend = false;
  uint32_t anim_prev = 0;
  dbz3::iwmips::vuelo7::Salida orig_o;
  int cam_yaw = -1;   // R7: rumbo de la camara, sigue a yaw a 1/16 por cuadro (Viewer_UpdateOrbitCamera L427262)
  // aplicar_estudios (entrega 16 P2): +0x604 cabeceo (palanca derecha Y) y +0x608 balanceo (palanca X) del visor de vuelo
  int cabeceo = -0x800, balanceo = 0;
  float cam_x = 0, cam_y = 0;   // ejes de Pad_GetStickAxes de este cuadro (0 fuera del mando)
  float zoom = 0, zoom_meta = 0;   // R7: +0x610 / +0x614 del visor de vuelo (distancia extra y su meta)
  int est_cam = 0;
  // DS124: ultima camara PEDIDA (ojo y objetivo) y cuadros que lleva el READY? alargado esperando a que la camara
  // DIBUJADA (mgr+0x60) coincida con ella (VistaAsentada).
  float cam_ped[6] = {};
  bool cam_ped_ok = false;
  int espera_cam = 0;
  // DS132: ultima camara ESCRITA por nosotros (ojo/objetivo) para invertir la mezcla 0,8/0,2 de B3, y cuadros de vuelo
  // con camara propia (para los logs de los primeros cuadros).
  float cam_esc[6] = {};
  bool cam_esc_ok = false;
  int cuadros_cam = 0;
  // `p_dibujo` es la posicion
  // real del jugador que se pinta, para colgar la flecha de mundo bajo sus pies.
  V3 p_dibujo;
  bool p_dibujo_ok = false;
  int virada = 0, media = 0;   // cuadros que quedan de la virada (0x321, 22) y de la media vuelta (0x32D, 44)
  // DS84: estela/aura del turbo. En el original el objeto tipo 0x0A de la L2 no se dibuja: al tocarlo lanza el
  // efecto Aura_Dash (Viewer_UpdateObjectCollection 0x33B7D0, rama uVar2==10) y ObjectsCollectionTeleport.
  float aura = 0;   // cuadros que quedan del aura (m_item[3] = ITEMA10_BLV_BODY alrededor del jugador)
  size_t siguiente = 0;   // v0: punto de control visible
  bool abierta = false, colocado = false, fin = false, superado = false;
  std::deque<V3> rastro;  // Vegeta: por donde ha pasado el jugador (coordenadas de IW sin dar la vuelta)
  float ux = 0, uz = 0;   // jugador sin dar la vuelta (para el rastro)
  V3 rival_p;
  int rival_yaw = 0;
};

std::mutex g_mu;
std::shared_ptr<Juego> g_j;
// Mando del jugador como lo leyo el juego (hilo del juego: MandoVuelo lo guarda, PasoVuelo lo usa)
struct Pad {
  bool hay = false;
  uint16_t b12 = 0, b14 = 0, d108 = 0, d110 = 0;
  uint8_t p26 = 0, p27 = 0;
  uint32_t b16 = 0;            // +16/+18: otro campo de botones (medido con DBZ3_R12_SONDA)
  uint32_t cuenta[16] = {};    // +0x28 + 4 bit: cuadros pulsado (> 0) o desde que se solto (< 0); P4 pone -1 en +92
};
Pad g_pad, g_dejado;
std::atomic<bool> g_corre{false};
std::atomic<int> g_pj_mapa{-1};   // personaje de IW que lleva el jugador en el mapa (PersonajeDelMapa)
// DS117: banderas de escenario (G+76) que se tocan para encender la niebla del #MCF mientras dura el vuelo.
uint32_t g_fog_flags_orig = 0;
bool g_fog_flags_guardado = false;
// DS132: camara del vuelo publicada para el gancho de dibujo (sub_820F4300, ver el final del archivo). Ese gancho
// escribe SOLO la matriz de vista (mgr+0x60), que es la que consume el render justo despues; NO toca +0x190/+0x1A0,
// que son la ENTRADA de la mezcla 0,8/0,2 de B3 (se leen al cuadro siguiente y alimentarian la mezcla).
std::mutex g_cv_mu;
float g_cv_ojo[3] = {}, g_cv_obj[3] = {}, g_cv_roll = 0.0f;
bool g_cv_on = false;
std::atomic<float> g_cv_fov_clip{0};   // 31/10: guion: FOV del clip; 0 = el del vuelo
void PublicarCamaraVuelo(const float ojo[3], const float obj[3], float roll) {
  std::lock_guard<std::mutex> lk(g_cv_mu);
  for (int i = 0; i < 3; ++i) g_cv_ojo[i] = ojo[i], g_cv_obj[i] = obj[i];
  g_cv_roll = roll;
  g_cv_on = true;
}
std::atomic<bool> g_render_on{false};   // DS137: hay cuadro del render publicado (vuelo en marcha)
void ApagarCamaraVuelo() {
  std::lock_guard<std::mutex> lk(g_cv_mu);
  g_cv_on = false;
  g_cv_fov_clip = 0.0f;
  g_render_on = false;
}
// DS177: camara del minijuego de campo (iw_minijuegos.cpp: entrada, carrera y final). Se aplica como la del vuelo
// (reescribiendo SOLO mgr+0x60 en el gancho de sub_820F4300, que es la vista que consume el dibujo de ese cuadro) y
// se publica la foto que ha quedado: vista + fov con el que el render proyecto. La escena 3D de la interfaz usa ESA
// foto, no mgr+0x60/+0x1C4. Medido: sub_82082A08 lee el fov de la struct activa *(mgr+0x264)+0x34 (escribir
// mgr+0x1C4 no cambia el 3D, DS92); mgr+0x60 es la SALIDA de sub_8208A168 y la entrada de sub_82087500 (el dibujo).
std::atomic<bool> g_mc_on{false};
std::mutex g_mc_mu;
float g_mc_ojo[3] = {}, g_mc_obj[3] = {}, g_mc_fov = 0.785f, g_mc_roll = 0;
bool g_mc_estaba = false;            // el cuadro anterior se aplico la camara del minijuego
uint32_t g_mc_cam = 0;               // struct activa en la que se escribio
float g_mc_fov_guardado = 0.785f;    // su fov antes de pisarlo (se devuelve al salir)
std::atomic<bool> g_rv_on{false};   // DS177: foto del cuadro que el render dibujo (vista + fov)
std::mutex g_rv_mu;
float g_rv_m[16] = {};
float g_rv_fov = 0.785f;
// DS137: el cuadro que B3 pinta de verdad. La vista que se dibuja NO es mgr+0x60 (el gancho lo pisa con la pedida):
// sub_820F4300 (generated/dbz3_recomp.6.cpp) termina con sub_8208A168(mgr+0x60), que copia la matriz del RENDER
// (global 0x8238A750) y la aplica al dibujo; +0x190/+0x1A0 guardan el ojo/objetivo de esa misma camara. Medido en las
// capturas del 27/10 (b_vuelo, TIME 01'22"52): con ESA camara (ojo 101,7 411,6 1626,3 / obj 123,2 406 1651,9 y el
// luchador en X+48) los pies del Krilin dibujado caen en (732, 460) y la cabeza en (735, 317); con la pedida (D 28,9)
// salian 78 px mas abajo. Por eso el gancho publica aqui el cuadro del render (vista, ojo/objetivo, balanceo y los
// puntos de cuerpo del luchador) ANTES de pisar mgr+0x60, y la interfaz proyecta con el: mismo cuadro que el dibujo,
// sin leer estado vivo (mover 2 u/cuadro cambia 100 px entre dos lecturas de OnDraw).
struct RenderCuadro {
  bool ok = false;
  float fov = 0.7853981634f;
  float ojo[3] = {}, obj[3] = {}, roll = 0.0f;   // camara del cuadro que se pinta
  float matriz[16] = {};                          // su vista (mgr+0x60 antes del pisa, copia de la del render)
  float pies[3] = {};                             // X+48 del luchador en ese cuadro
  float bajo[3] = {};                             // punto mas bajo de los 22 de cuerpo (las botas dibujadas)
  bool hay_bajo = false;
  int i_bajo = -1;
  float cabeza[3] = {};                           // punto 3 del cuerpo (cabeza dibujada)
  bool hay_cabeza = false;
  uint32_t cuadro = 0;
};
std::mutex g_render_mu;
RenderCuadro g_render;
RenderCuadro CuadroRender() {
  if (!g_render_on.load()) return {};
  std::lock_guard<std::mutex> lk(g_render_mu);
  return g_render;
}
void PublicarCuadroRender(const RenderCuadro& r) {
  {
    std::lock_guard<std::mutex> lk(g_render_mu);
    g_render = r;
  }
  g_render_on = true;
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
// DS92: FOV que usa DE VERDAD la proyeccion del juego en la pelea. Medido en iw_mini_botones.cpp (s8, 2706-2669):
// escribir mgr+0x1C4 al empezar el dibujo NO cambia nada (DBZ3_S8_FOV=0.35 daba la misma imagen) porque la matriz de
// proyeccion ya esta hecha con el 0,785 de B3; mgr+0x1C4 solo lo leen nuestros overlays (ps2::CamaraCombate), asi que
// escribir ahi el 40 de IW descuadraba aros/flecha (proyectados con 40) respecto a los luchadores (dibujados con 45).
// Se lee el FOV real y se compensa la distancia con tan(40/2)/tan(fov_juego/2), el mismo recurso de s8: el personaje
// sale al tamano de IW aunque la proyeccion siga siendo la de B3.
float FovJuegoVuelo(uint8_t* base) {
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!base || !mgr) return 0.7853981634f;
  const uint32_t cam=REX_LOAD_U32(mgr+0x264);
  if(cam>=0x82000000u&&cam<0x83000000u) {
    const float actual=LoadF(base,cam+0x34);
    if(actual>.1f&&actual<3) return actual;
  }
  const float f = LoadF(base, mgr + kFovMgr);
  return (f > 0.3f && f < 1.2f) ? f : 0.7853981634f;
}
float FovVerticalIW() { return 2.0f * std::atan(std::tan(kFovVueloIW * 0.5f) / kAspectoIW); }
float CompensacionFovVuelo(float fov_juego) {
  // DS117: se compara el 40 HORIZONTAL de IW (vertical 30,8 en 4:3) con el vertical real de B3. Con 45 da 0,665.
  const float k = std::tan(FovVerticalIW() * 0.5f) / std::tan(fov_juego * 0.5f);
  return std::clamp(k, 0.4f, 1.5f);
}
const char* NombreFov(float fov) {
  if(std::fabs(fov-kFovVueloReal)<.001f) return "IW-focal512";
  return std::fabs(fov - kFovVueloIW) < 0.02f ? "IW40" : std::fabs(fov - 0.7853981634f) < 0.02f ? "B3-45" : "otro";
}
// DS92: la intro y el cierre del #AMC tambien se venian dibujando con el 40 de IW: como el juego pinta con el FOV de
// B3, se acerca el ojo al objetivo en la misma proporcion para conservar el encuadre del clip.
void AcercarPorFov(uint8_t* base, float ojo[3], const float obj[3]) {
  const float k = CompensacionFovVuelo(FovJuegoVuelo(base));
  for (int i = 0; i < 3; ++i) ojo[i] = obj[i] + (ojo[i] - obj[i]) * k;
}
float DistanciaCamara(const float a[3], const float b[3]) {
  return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}
// DS119: tamano en pantalla de los objetos de mundo del vuelo (aros, monedas, objetos y casa de la meta).
// IW pinta con 40 grados HORIZONTALES de 4:3 = 30,8 verticales (Camera_InitFieldDefault EU 0x297140) y su camara
// esta a 52,2 del objetivo (CameraFocusWhileFlying EU 0x339AA0). El port no puede cambiar la proyeccion: B3 pinta
// con 45 verticales y DS117 acerca la camara a 52,2 x 0,665 x 0,84 = 29,1 para que el luchador conserve el tamano.
// Ese acercamiento vale para lo que esta al lado del jugador, pero un objeto LEJANO (un aro a 1000-2000 u) no gana
// nada y con el FOV de B3 sale 0,665 (tan 15,4 / tan 22,5) de su tamano de IW: era el "los aros lejanos casi no se
// ven" (prueba 27/10 02:06, c_control). Para que un objeto a `d` del ojo se vea como en IW con su camara `dif` mas
// atras hay que multiplicarlo por (d tan(45/2)) / ((d + dif) tan(30,8/2)): tiende a 1,50 de lejos y a ~0,84 al
// lado del jugador (donde el acercamiento de DS117 ya lo compensa). El tamano aparente sigue creciendo al acercarse
// (1/(d+dif)), asi que no "respira": solo se acerca mas despacio, como el 30,8 de IW.
float EscalaObjetoVuelo(float d, float dif_cam) {
  const float t_b3 = std::tan(0.7853981634f * 0.5f);
  const float t_iw = std::tan(FovVerticalIW() * 0.5f);
  if (!(d > 1.0f)) return 1.0f;
  return std::clamp((d * t_b3) / ((d + std::max(dif_cam, 0.0f)) * t_iw), 0.75f, 1.55f);
}
// DS119: en el original la flecha (EU 0x33B580) va en espacio de camara en (0, -9, -45) sobre _LAB_00483250. Ya no
// se reproduce en mundo (DS119 la colgaba de los pies a 6,0 u hacia la camara): ver las constantes kFlecha* de arriba,
// que la ponen en el espacio de la camara DIBUJADA como el original. Aqui queda el dato del original, para el informe:
// con la camara de IW (52,2 detras del punto (0,7,0), cabeceo -0x800 = 11,25) ese punto son 8,81 u hacia la camara y
// 0,43 u por debajo de los pies; medido en pantalla, la flecha esta siempre al ~73,5 % del alto, no pegada a la camara.
// Base de la camara DIBUJADA: la vista mgr+0x60 que consume Escena (ps2::CamaraCombate). La matriz es la de cine::Camara
// (columnas derecha/arriba/atras y traslacion en la ultima columna), asi que el ojo sale de -d*m12 -u*m13 -a*m14.
// DS124: la flecha se coloca con ESTA base para que su proyeccion y la del resto de la escena usen la misma camara.
struct BaseVista {
  float ojo[3] = {}, der[3] = {}, arr[3] = {}, del[3] = {};
  bool ok = false;
};
BaseVista BaseDeVista(const ps2::Camara& cam) {
  BaseVista b;
  if (!cam.ok) return b;
  const float atr[3] = {cam.m[2], cam.m[6], cam.m[10]};
  const float l = std::sqrt(atr[0] * atr[0] + atr[1] * atr[1] + atr[2] * atr[2]);
  if (!(l > 0.5f)) return b;
  b.der[0] = cam.m[0], b.der[1] = cam.m[4], b.der[2] = cam.m[8];
  b.arr[0] = cam.m[1], b.arr[1] = cam.m[5], b.arr[2] = cam.m[9];
  b.del[0] = -atr[0], b.del[1] = -atr[1], b.del[2] = -atr[2];
  for (int i = 0; i < 3; ++i) b.ojo[i] = -(b.der[i] * cam.m[12] + b.arr[i] * cam.m[13] + atr[i] * cam.m[14]);
  b.ok = true;
  return b;
}
// DS132: B3 NO dibuja la camara que se le pone: mezcla 0,8 x lo puesto (+0x190 ojo / +0x1A0 objetivo) con 0,2 x la
// suya (medido en cinematica.cpp A3b). DS128 intento igualar la mezcla escribiendo su "otra fuente" en +0x150/+0x160 y
// no cambio nada: en el codigo recompilado ese campo NO es un ojo/objetivo, es un PUNTERO. sub_820F47E0
// (generated/dbz3_recomp.18.cpp) hace `lwz r10,608(r31)` y `cmplw` contra `r30+176` = mgr+0x150: si coinciden llama a
// sub_820F4488. Escribir floats ahi solo falsea esa comparacion. La unica forma de anular la mezcla sin enganchar la
// funcion de camara (el gancho de sub_820F47E0 ya es de cinematica.cpp) es INVERTIRLA: se escribe
//     W = (pedido - 0,2 * N) / 0,8
// donde N es la camara propia de B3, que no se puede calcular (va retrasada con respecto al jugador: en el vuelo el
// objetivo reconstruido esta 74 u detras) pero SI se mide: N = (leido - 0,8 * W_anterior) / 0,2, con `leido` la lectura
// de +0x190/+0x1A0 al empezar el cuadro (lo que dejo el cuadro anterior). Asi 0,8*W + 0,2*N = pedido.
constexpr float kMezclaB3 = 0.8f;        // peso de la camara puesta en lo que B3 dibuja (A3b, medido)
constexpr float kSaltoMaxCam = 3000.0f;  // cordura: si N o W se van mas de esto, se usa la pedida tal cual

// Invierte la mezcla de B3. `leido` = lo que hay en +0x190/+0x1A0 al empezar el cuadro (ojo, objetivo);
// `escrito_ant` = lo que escribimos el cuadro anterior; `ped` = camara del vuelo de este cuadro (ojo, objetivo).
// Deja en `n_est` la camara propia de B3 reconstruida (para el log) y en `salida` lo que hay que escribir.
bool InvertirMezcla(const float leido[6], const float escrito_ant[6], bool hay_ant, const float ped[6], float salida[6],
                    float n_est[6]) {
  bool reconstruida = hay_ant;
  for (int i = 0; i < 6 && reconstruida; ++i) {
    const float n = (leido[i] - kMezclaB3 * escrito_ant[i]) / (1.0f - kMezclaB3);
    n_est[i] = n;
    if (!std::isfinite(n) || std::fabs(n - leido[i]) > kSaltoMaxCam) reconstruida = false;
  }
  if (!reconstruida) {
    for (int i = 0; i < 6; ++i) n_est[i] = leido[i];
    for (int i = 0; i < 6; ++i) salida[i] = ped[i];
    return false;
  }
  for (int i = 0; i < 6; ++i) {
    float w = (ped[i] - (1.0f - kMezclaB3) * n_est[i]) / kMezclaB3;
    if (!std::isfinite(w) || std::fabs(w - ped[i]) > kSaltoMaxCam) w = ped[i];
    salida[i] = w;
  }
  return true;
}
// DS132: diagnostico del campo +0x150/+0x160. Lo que demuestra el log: el valor crudo es un puntero (> 0x80000000) y
// cambia entre dos objetos; leido como floats no tiene sentido como coordenada. Opcional (DBZ3_R12_PRUEBA_150) hace la
// prueba de escritura de DS128 y ensena, al cuadro siguiente, que B3 lo reescribe.
void LogCampoGuion(uint8_t* base, uint32_t mgr, int cuadro, bool prueba) {
  const uint32_t u150 = REX_LOAD_U32(mgr + 0x150), u160 = REX_LOAD_U32(mgr + 0x160);
  const float f150[3] = {LoadF(base, mgr + 0x150), LoadF(base, mgr + 0x154), LoadF(base, mgr + 0x158)};
  const float f160[3] = {LoadF(base, mgr + 0x160), LoadF(base, mgr + 0x164), LoadF(base, mgr + 0x168)};
  REXLOG_INFO("dbz3 r12 cam: +0x150 crudo 0x{:08X} / +0x160 crudo 0x{:08X} (cuadro {}); como floats "
              "({:.1f},{:.1f},{:.1f}) y ({:.1f},{:.1f},{:.1f}) | {}",
              u150, u160, cuadro, f150[0], f150[1], f150[2], f160[0], f160[1], f160[2],
              (u150 > 0x80000000u && u160 > 0x80000000u)
                  ? "puntero/estructura (sub_820F47E0 lo compara con mgr+0x260, cmplw): NO es la fuente de la mezcla"
                  : "valor bajo: revisar (no parece puntero)");
  if (prueba) {
    for (int i = 0; i < 3; ++i) StoreF(base, mgr + 0x150 + 4 * uint32_t(i), 1234.5f + float(i));
    REXLOG_INFO("dbz3 r12 cam: PRUEBA +0x150: escrito (1234.5, 1235.5, 1236.5) en el cuadro {}; el cuadro siguiente se "
                "lee el valor crudo de arriba para ver si B3 lo reescribe",
                cuadro);
  }
}
// Cabeceo en grados de una direccion de vista (positivo = mira hacia abajo).
float CabeceoDeDireccion(const float d[3]) {
  return std::atan2(-d[1], std::hypot(d[0], d[2])) * 57.2957795f;
}
// Rumbo (grados) de una direccion en el plano x/z.
float RumboDeDireccion(const float d[3]) { return std::atan2(d[0], d[2]) * 57.2957795f; }
// DS124: READY? alargado hasta que la camara DIBUJADA coincide con la pedida (cabeceo y rumbo). Devuelve true si ya
// coincide; deja en `cab_v`/`cab_p` los cabeceos dibujado y pedido (grados) para el log. Si no hay camara pedida, no
// espera.
bool VistaAsentada(uint8_t* base, uint32_t mgr, const Juego& j, float& cab_v, float& cab_p) {
  cab_v = cab_p = 0.0f;
  if (!j.cam_ped_ok || !base || !mgr) return true;
  float m[16];
  for (int i = 0; i < 16; ++i) m[i] = LoadF(base, mgr + 0x60 + 4 * uint32_t(i));
  const float del[3] = {-m[2], -m[6], -m[10]};
  const float l = std::sqrt(del[0] * del[0] + del[1] * del[1] + del[2] * del[2]);
  if (!(l > 0.5f)) return true;
  cab_v = CabeceoDeDireccion(del);
  const float ped[3] = {j.cam_ped[3] - j.cam_ped[0], j.cam_ped[4] - j.cam_ped[1], j.cam_ped[5] - j.cam_ped[2]};
  cab_p = CabeceoDeDireccion(ped);
  if (std::fabs(cab_v - cab_p) > kTolCabeceo) return false;
  const float dh = std::fabs(std::remainder(RumboDeDireccion(del) - RumboDeDireccion(ped), 360.0f));
  return dh < kTolRumbo;
}
// Cabeceo en grados como el decomp: negativo = el ojo esta por encima y mira hacia abajo (CameraFocusWhileFlying -0x800
// = -11,25 grados). Se calcula de la camara real (ojo - objetivo), no del valor pedido.
float CabeceoCamara(const float ojo[3], const float obj[3]) {
  const float d = DistanciaCamara(ojo, obj);
  if (d < 1e-3f) return 0.0f;
  return -std::asin(std::clamp((ojo[1] - obj[1]) / d, -1.0f, 1.0f)) * 57.2957795f;
}
// DS92: una linea por segundo con la camara PEDIDA y la DIBUJADA (la que B3 tenia en +0x190/+0x1A0 antes de escribir
// la de este cuadro), el FOV real y la correccion del lazo. El coordinador la busca con "r12 cam:".
void LogCamaraVuelo(const Juego& j, const float ped_ojo[3], const float ped_obj[3], bool leido, const float leo[3],
                    const float lob[3], float fov_juego, float k_fov, float dist_iw) {
  const float d_ped = DistanciaCamara(ped_ojo, ped_obj);
  const char* fov_txt = NombreFov(fov_juego);
  if (leido) {
    const float d_dib = DistanciaCamara(leo, lob);
    REXLOG_INFO("dbz3 r12 cam: t {:.1f}s estado {} zoom {:.1f} | pedido D {:.1f} (IW {:.1f} x k {:.4f}) cabeceo "
                "{:.2f} obj ({:.1f}, {:.1f}, {:.1f}) | dibujado D {:.1f} cabeceo {:.2f} ojo ({:.1f}, {:.1f}, {:.1f}) "
                "obj ({:.1f}, {:.1f}, {:.1f}) | corr ({:.1f}, {:.1f}, {:.1f}) | FOV juego {:.5f} rad ({}), IW40 "
                "{:.5f} | jugador ({:.1f}, {:.1f}, {:.1f})",
                j.t_ms / 1000.0f, j.estado, j.zoom, d_ped, dist_iw, k_fov, CabeceoCamara(ped_ojo, ped_obj), ped_obj[0],
                ped_obj[1], ped_obj[2], d_dib, CabeceoCamara(leo, lob), leo[0], leo[1], leo[2], lob[0], lob[1], lob[2],
                0.0f, 0.0f, 0.0f, fov_juego, fov_txt, kFovVueloIW,
                j.p_dibujo.x, j.p_dibujo.y, j.p_dibujo.z);
  } else {
    REXLOG_INFO("dbz3 r12 cam: t {:.1f}s estado {} zoom {:.1f} | pedido D {:.1f} (IW {:.1f} x k {:.4f}) cabeceo "
                "{:.2f} obj ({:.1f}, {:.1f}, {:.1f}) | dibujado SIN LECTURA | FOV juego {:.5f} rad ({}), IW40 {:.5f} "
                "| jugador ({:.1f}, {:.1f}, {:.1f})",
                j.t_ms / 1000.0f, j.estado, j.zoom, d_ped, dist_iw, k_fov, CabeceoCamara(ped_ojo, ped_obj), ped_obj[0],
                ped_obj[1], ped_obj[2], fov_juego, fov_txt, kFovVueloIW, j.p_dibujo.x, j.p_dibujo.y, j.p_dibujo.z);
  }
}
void Poner(uint8_t* base, uint32_t x, const V3& p) {
  for (uint32_t o : kCopias) {
    StoreF(base, x + o, p.x);
    StoreF(base, x + o + 4, p.y);
    StoreF(base, x + o + 8, p.z);
  }
}
void Mirar(uint8_t* base, uint32_t x, int yaw) {
  REX_STORE_U16(x + kMiradaA, uint16_t(yaw - 15759));
  REX_STORE_U16(x + kMiradaB, uint16_t(yaw - 15968));
}
void ColocarActorGuion(uint8_t* base,uint32_t x) {
  for (uint32_t i=0;i<16;++i) StoreF(base,x+4*i,i%5==0 ? 1.0f : 0.0f);
  Poner(base,x,{0,0,0});
}
float Ang(int yaw) { return float(yaw & 0xFFFF) / 65536.0f * 6.2831853f; }
// DS147: el rumbo del MODELO no lo dan X+6062/+6074. Esos dos campos son la "mirada" que usa la IA/los pases de
// animacion: con el cuerpo del vuelo (poses 0x320-0x32E) no giran al luchador. El que se dibuja sale de su MATRIZ
// DE MUNDO, que B3 rehace cada cuadro y que hasta ahora solo se escribia en la fila 3 (la posicion, Poner; filas 0-2
// = giro, X+0..X+44, X+48 = fila 3: docs/14_frentes/escenarios.md R2 y el gancho de gigantes.cpp:160, que escala esas
// mismas filas al dibujar). Por eso el cuerpo conservaba la orientacion de la SALIDA del combate: en 0x7D6 el rumbo
// del vuelo era ~0 (miraba a +Z, la misma que la salida) y se veia de espaldas; en 0x7E0 el rumbo pasa de 200 grados
// y Gohan salia de perfil (capturas del coordinador 27/10 08:09, b_vuelo/c_control). Se escribe la fila del giro con
// el rumbo de la marcha: el frente del modelo de B3 es +Z (las puntas de los pies, XGHL_LFOOT2 a z>0 en 225 y
// XKLL_LFOOT2 en 327), asi que +Z local -> (sin a, 0, cos a), la misma direccion que usa Volar para avanzar.
void Orientar(uint8_t* base, uint32_t x, int yaw) {
  // 27/10 (coordinador): con a = rumbo, Krilin (rumbo ~0) salia de perfil mirando a la derecha: el frente local del
  // cuerpo en la matriz de mundo de B3 va de lado, no +Z (DS145 vio lo mismo en Satan). Medido en pantalla 27/10:
  // con rumbo - 90 grados salian de frente a la camara; con rumbo + 90 grados, de espaldas como el original.
  const float a = Ang(yaw) + 1.5707963f, c = std::cos(a), s = std::sin(a);
  // matriz de mundo 4x4 por filas, con vectores fila (misma convencion que ps2::Camara::P y que la fila 3 en X+48).
  StoreF(base, x + 0, c);
  StoreF(base, x + 4, 0.0f);
  StoreF(base, x + 8, -s);
  StoreF(base, x + 12, 0.0f);
  StoreF(base, x + 16, 0.0f);
  StoreF(base, x + 20, 1.0f);
  StoreF(base, x + 24, 0.0f);
  StoreF(base, x + 28, 0.0f);
  StoreF(base, x + 32, s);
  StoreF(base, x + 36, 0.0f);
  StoreF(base, x + 40, c);
  StoreF(base, x + 44, 0.0f);
}
using dbz3::iwvuelo::Envolver;   // iw_mini_vuelo.h (mundo que da la vuelta)

// EU 0x1DD4F0 Anim_StepUvAnim: extrae la deriva constante (descriptor 5) de los canales del #MTA.
std::vector<float> LeerDerivasMta(const std::vector<uint8_t>& b) {
  std::vector<float> dv(16, 0.0f);
  if (b.size() < 0x0C || std::memcmp(b.data(), "#MTA", 4) != 0) return dv;
  const size_t n = std::min<size_t>(U32(b, 4), dv.size()), tabla = U32(b, 8);
  for (size_t c = 0; c < n; ++c) {
    const size_t o = tabla + 0x80 * c;
    if (o + 0x64 > b.size()) break;
    const int32_t nk = int32_t(U32(b, o + 0x50));
    const size_t ptr = U32(b, o + 0x58);
    dv[c] = F32(b, o + 0x5C);
    if (nk > 0 && ptr + 8 <= b.size()) dv[c] = F32(b, ptr + 4);
  }
  return dv;
}

// #AMC de camara de 891 (formato del vuelo: pista +8 = nº claves, claves de 16 B x,y,z,bandera desde +16).
// DS113: `libro` = indice del #AMC dentro del bundle (0 = hijo 25, 1 = hijo 26); la clave del mapa es libro<<16|clip.
void LeerClipsVuelo(const std::vector<uint8_t>& amc, int libro, const std::vector<int>& ids, std::map<int, ClipVuelo>& out) {
  for (int id : ids) {
    const int clave = (libro << 16) | (id & 0xFFFF);
    if (out.count(clave)) continue;
    ClipVuelo v;
    if (!dbz3::iwamc::Leer(amc, id, v.c)) continue;
    v.dur = v.c.nf;
    if (!v.c.fov.empty()) v.fov = v.c.fov[0][1];
    out[clave] = std::move(v);
  }
}

bool CamaraClip(const Juego& j, int libro, int clip, float f, float ojo[3], float obj[3]) {
  const auto it = j.cams.find((libro << 16) | (clip & 0xFFFF));
  if (it == j.cams.end()) return false;
  const float u = std::clamp(f, 0.0f, float(it->second.dur));
  const dbz3::iwamc::Pose pose = dbz3::iwamc::Muestrea(it->second.c, u);
  std::copy(pose.ojo, pose.ojo + 3, ojo);
  std::copy(pose.obj, pose.obj + 3, obj);
  const bool ok = true;
  if (ok && dbz3::gp2cam::Activo())
    dbz3::gp2cam::Evaluada("vuelo", 891, 25 + libro, libro, clip, f, it->second.dur,
        j.intro ? 0 : j.cierre ? j.escena_cierre : -1, j.intro ? j.guion_linea : -1,
        ojo, obj, it->second.fov, 0.0f, (libro << 16) | clip);
  return ok;
}

// ---- DS113: audio de la intro (musica + voces del #AMB 891) ----
// EU 0x33CE40 FlyingMinigameFuncs: la musica del visor sale por el canal 0 con
// CharacterDialoguesStart_(0, Audio_GetFilteredTableEntry(0x6b, (&LAB_0049c458)[v]) | 0x20000, 0). La tabla de
// audio del visor es la fila 0x12 de EU 0x4803B0 = ADX_CMN {63,64,65,66,67,68,69,70}; los indices 2 (v0) y 0 (v1)
// son las pistas 65 y 63 (el 3 = 66 que usaba el port es el de otro minijuego). Volumen: el del lanzador, como el
// resto de musica de pasarela.
void MusicaVuelo(const Juego& j) {
  const int pista = j.v == 0 ? 65 : 63;
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.8f;
  REXLOG_INFO("dbz3 r12: musica de la intro: ADX_CMN {} (EU 0x4803B0 fila 0x12 indice {}, EU 0x33CE40); volumen {:.2f}",
              pista, j.v == 0 ? 2 : 0, vol);
  P::OirMusicaPs2(P::BuscarIW(), "ADX_CMN.AFS", pista, vol);
}

// EU 0x342090 Audio_PlayDialogueOnChannel + EU 0x1BA4F0 READ_FileIndex_: el indice de la tabla 0x49C460 es el HIJO
// del #AMB de datos del vuelo (891) y READ_FileIndex_ devuelve su offset dentro del bundle. Los hijos 29..33 son ADX
// CRI de 24 kHz (five voice/cue tracks). El reproductor ADX del port (pasarela::OirAdxPs2) lee entradas de un AFS, asi
// que los cinco hijos se copian una vez a un AFS normal en user_data (misma idea que la cache de SFD de IW).
bool PrepararVocesVuelo(const std::vector<uint8_t>& amb, Juego& j) {
  std::vector<std::vector<uint8_t>> adx;
  for (uint32_t c = 29; c <= 33; ++c) {
    std::vector<uint8_t> d = P::HijoAmb(amb, c);
    if (d.size() < 0x20 || d[0] != 0x80) return false;   // firma ADX
    adx.push_back(std::move(d));
  }
  std::error_code ec;
  j.dir_voces = dbz3::settings::UserDataRoot() / "cache_ps2" / "iw";
  const std::filesystem::path afs = j.dir_voces / "voces_vuelo.afs";
  size_t total = 8 + 8 * adx.size();
  for (const auto& a : adx) total += (a.size() + 0x7FF) & ~size_t(0x7FF);
  if (std::filesystem::exists(afs, ec) && std::filesystem::file_size(afs, ec) == total) return true;
  std::filesystem::create_directories(j.dir_voces, ec);
  if (ec) return false;
  std::vector<uint8_t> out(total, 0);
  std::memcpy(out.data(), "AFS\0", 4);
  auto u32 = [&](size_t o, uint32_t v) { std::memcpy(&out[o], &v, 4); };
  u32(4, uint32_t(adx.size()));
  size_t o = 8 + 8 * adx.size();
  for (size_t k = 0; k < adx.size(); ++k) {
    u32(8 + 8 * k, uint32_t(o));
    u32(8 + 8 * k + 4, uint32_t(adx[k].size()));
    std::memcpy(&out[o], adx[k].data(), adx[k].size());
    o += (adx[k].size() + 0x7FF) & ~size_t(0x7FF);
  }
  const std::string tmp = afs.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
  }
  std::filesystem::rename(tmp, afs, ec);
  if (ec) {   // en Windows rename no pisa: se aparta el viejo
    std::filesystem::remove(afs, ec);
    std::filesystem::rename(tmp, afs, ec);
  }
  if (ec) return false;
  REXLOG_INFO("dbz3 r12: voces de la intro: #AMB 891 hijos 29..33 -> {} ({:.0f} KB)", afs.string(), total / 1024.0);
  return true;
}

// 0 = arranque de la intro (linea 0), 1 = linea 2 (Krilin/Dende), 2 = meta (Viewer_BeginFinalPhase 0x33B2C0).
void VozVuelo(const Juego& j, int cual) {
  if (!j.voces_ok || cual < 0 || cual > 2) return;
  const int indice = j.voz_child[cual] - 29;
  if (indice < 0 || indice > 4) return;
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  REXLOG_INFO("dbz3 r12: voz de la intro {}: #AMB 891 hijo {} (AFS {} entrada {}), EU 0x342090", cual,
              j.voz_child[cual], "voces_vuelo.afs", indice);
  P::OirAdxPs2(j.dir_voces, "voces_vuelo.afs", indice, vol);
}

// EU 0x33CB80 Viewer_StateCutsceneInput (fin de la intro) / EU 0x33B2C0 Viewer_BeginFinalPhase (meta):
// CharacterDialoguesStart_(2, Table_Lookup480450(byte)|0x30000, 0). EU 0x480450 = 1248 + indice, asi que el byte
// 0x49C468 (=0) da 1248 (frase del START; 1,2 s) y 0x49C46A (=5) da 1253 (frase de la meta si se llego dentro del
// limite; 1,6 s; con el tiempo agotado el original usa 1248). El AFS de voces es el que el juego tiene elegido en
// Opciones (adx_jpn con 0, adx_usa si no; el mismo criterio de video_sfd.cpp).
void VozCorta(const Juego& j, int entrada) {
  (void)j;
  rex::Runtime* rt = rex::Runtime::instance();
  uint8_t* base = rt ? rt->virtual_membase() : nullptr;
  const int byte = base ? int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9)) : 1;   // 0 = japones
  const char* afs = byte == 0 ? "ADX_JPN.AFS" : "ADX_USA.AFS";
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  REXLOG_INFO("dbz3 r12: voz corta {} entrada {} (idioma de voces {}), EU 0x33CB80/0x33B2C0", afs, entrada,
              byte == 0 ? "japones" : "ingles");
  P::OirAdxPs2(P::BuscarIW(), afs, entrada, vol);
}

// DS118: entrada suelta del AFS de voces (banco 3) que el juego tiene elegido en Opciones; el mismo criterio que
// VozCorta (byte G+0x2C0C9: 0 = ADX_JPN, si no ADX_USA; los dos con 1499 entradas).
void VozBanco3(const Juego& j, int entrada, const char* motivo) {
  (void)j;
  if (entrada < 0) return;
  rex::Runtime* rt = rex::Runtime::instance();
  uint8_t* base = rt ? rt->virtual_membase() : nullptr;
  const int byte = base ? int(REX_LOAD_U8(dbz3::GA(0x824BA110) + 0x2C0C9)) : 1;
  const char* afs = byte == 0 ? "ADX_JPN.AFS" : "ADX_USA.AFS";
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume());
  REXLOG_INFO("dbz3 r12: voz {} entrada {} de {} (EU 0x31B3C0 Audio_GetFilteredTableEntry)", motivo, entrada, afs);
  P::OirAdxPs2(P::BuscarIW(), afs, entrada, vol);
}

void CuadroDialogo(Juego& j) {
  if (j.dialogo<0&&!j.dialogos.empty()) {
    j.dialogo=j.dialogos.front();j.dialogos.pop_front();j.dialogo_t=0;j.dialogo_salida=-1;
    VozBanco3(j,1111+j.dialogo,"cola del retrato rival (EU342F90)");
    REXLOG_INFO("dbz3 solIW7: retrato rival evento {} entrada cuadro {}",j.dialogo,j.cuadros);
  }
  if (j.dialogo<0) return;
  ++j.dialogo_t;
  const int nodo=j.dialogo_ama.Objeto(15);
  const int entrada=nodo>=0 ? int(anim::ama::MaxCuadros(anim::ama::CrearArbol(j.dialogo_ama,nodo))) : 26;
  // El reloj de600 empieza al terminar la entrada; la voz comienza al encolar.
  if (j.dialogo_salida<0&&j.dialogo_t>=entrada&&
      (j.dialogo_t>=j.dialogo_voz[j.dialogo-1]||j.dialogo_t>=entrada+600)) {
    j.dialogo_salida=j.dialogo_t;
    REXLOG_INFO("dbz3 solIW7: retrato rival evento {} salida cuadro {}",j.dialogo,j.dialogo_t);
  }
  const int salida=j.dialogo_ama.Objeto(16);
  const int dur_salida=salida>=0 ? int(anim::ama::MaxCuadros(anim::ama::CrearArbol(j.dialogo_ama,salida))) : 27;
  if (j.dialogo_salida>=0&&j.dialogo_t-j.dialogo_salida>=dur_salida) j.dialogo=-1;
}

// DS118: voz del jugador de la tabla del ELF. EU 0x31B3C0 Audio_GetFilteredTableEntry(clave, i) devuelve
// PTR_DAT_4803B0[MinigameAudioTriggerFilter(clave)][i] (EU 0x298380; para los personajes 2..10 la fila es char-1).
// Bases de fila medidas en el ELF EU (0x4803B0): char 2 (Gohan nino) 0x47FC00 = 1013, char 4 (Gohan) 0x47FCD0 =
// 973, char 9 (Trunks nino) 0x47FE30 = 1276 y char 10 (Krilin) 0x47FE70 = 1134. Son los 4 personajes del vuelo
// (FlyingMinigameCharacterIDs, ELF EU 0x483010); las filas tienen 12+ entradas y los indices del vuelo (2..17) caen dentro.
int VozDePersonaje(int char_iw, int indice) {
  const int base = char_iw == 2 ? 1013 : char_iw == 4 ? 973 : char_iw == 9 ? 1276 : char_iw == 10 ? 1134 : -1;
  return base < 0 || indice < 0 ? -1 : base + indice;
}

// DS118: frase del jugador (aro cruzado / arranque del vuelo). El original la pide por la fila de su personaje
// (Viewer_OnObjectHitEvent EU 0x33AFA0 y Viewer_StateCutsceneInput/0x33C660): el indice sale de las tablas de
// abajo, no de una lista propia.
void VozJugador(const Juego& j, int indice, const char* motivo) {
  const int entrada = VozDePersonaje(j.jugador, indice);
  if (entrada < 0) return;
  VozBanco3(j, entrada, motivo);
}


void AplicarDerivaUvCasa(ps2::Malla& m, const std::vector<float>& canales, float cuadros) {
  for (ps2::Malla::Tri& tr : m.tris) {
    const size_t canal = size_t(10 + tr.parte);
    if (canal >= canales.size()) continue;
    const float delta = canales[canal] * cuadros;
    float menor = tr.uv[0].y + delta, mayor = menor;
    for (int c = 1; c < 3; ++c) menor = std::min(menor, tr.uv[c].y + delta), mayor = std::max(mayor, tr.uv[c].y + delta);
    const float ajuste = menor > 1.0f ? -1.0f : mayor < 0.0f ? 1.0f : 0.0f;
    for (int c = 0; c < 3; ++c) tr.uv[c].y += delta + ajuste;
  }
}

// EU 0x33E540 LoadWaitFlyingMinigame
// EU 0x346E80 MgFly_RunLoadTable
// EU 0x346F40 MgFly_CallLoadFunc
bool Cargar(Juego& j, std::string& error) {
  P::AfsPs2 cmn;
  if (!cmn.Abrir(P::BuscarIW(), "DATA_CMN.AFS")) return error = "sin DATA_CMN.AFS de Infinite World", false;
  // DS101: malla del campo de vuelo (883 v0 / 881 v1, hijo 5). Se lee del AFS de IW, que es la fuente de la ruta.
  const std::vector<uint8_t> campo = cmn.Entrada(j.v == 0 ? 883 : 881);
  LeerTerrenoVuelo(P::HijoAmb(campo, 5), j.terreno);
  // DS117: niebla del #MCF (hijo 0): color +0x1C, floats +0x20/+0x24 (los +4/+8 de la entrada de B3), inicio/fin
  // +0x28/+0x2C. El color y el rango los pone campos_iw.py/escenarios.cpp; aqui se guardan los dos floats.
  const std::vector<uint8_t> mcf = P::HijoAmb(campo, 0);
  if (mcf.size() >= 0x30 && std::memcmp(mcf.data(), "#MCF", 4) == 0) {
    j.fog_a = F32(mcf, 0x20);
    j.fog_b = F32(mcf, 0x24);
    // DS182/DS184: color de la niebla (+0x1C) y su rango de DISTANCIA (+0x28 inicio, +0x2C fin: 2400..3700). El color
    // es FOGCOL 0x00BBGGRR (R = byte bajo; 0x00FFFF87 = 135/255/255 turquesa; leerlo como ARGB daba 255/255/135),
    // el mismo que usa mod center hd/campos_iw.py. Es lo que en el original tine el agua del horizonte
    // (fotograma 1946 s: agua plana 115/212/221); el port lo pinta con VeloHorizonte.
    j.fog_rgb = U32(mcf, 0x1C);
    j.fog_ini = F32(mcf, 0x28);
    j.fog_fin = F32(mcf, 0x2C);
  }
  REXLOG_INFO("dbz3 r12: niebla #MCF campo {}: color {:08X}, +0x20/+0x24 {:.0f}/{:.0f}, rango {:.0f}..{:.0f}", j.v == 0 ? 883 : 881,
              mcf.size() >= 0x30 ? U32(mcf, 0x1C) : 0, j.fog_a, j.fog_b,
              mcf.size() >= 0x30 ? F32(mcf, 0x28) : 0.0f, mcf.size() >= 0x30 ? F32(mcf, 0x2C) : 0.0f);
  const std::vector<uint8_t> amb = cmn.Entrada(891);
  const std::vector<uint8_t> m = P::HijoAmb(amb, uint32_t(23 + j.v));
  j.msf_raw = m;
  if (m.size() < 0x200 || std::memcmp(m.data(), "#MSF", 4) != 0) return error = "sin #MSF en DATA_CMN 891", false;
  iwvuelo::Msf f;
  if (!iwvuelo::LeerMsf(m, j.dif, f, error)) return false;
  j.hover = f.hover;
  std::copy(f.empuje, f.empuje + 3, j.empuje);
  std::copy(f.freno, f.freno + 3, j.freno);
  std::copy(f.giro, f.giro + 3, j.giro);
  j.vmin = f.vmin;
  j.vfreno = f.vfreno;
  j.giro0 = f.giro0;
  j.dur2 = f.dur2;
  j.dur3 = f.dur3;
  std::copy(f.radio, f.radio + 16, j.radio);
  j.medidor0 = f.medidor0;
  std::copy(f.sube, f.sube + 3, j.sube);
  j.congela = f.congela;
  j.reaparece = f.reaparece;
  std::copy(f.puntos_moneda, f.puntos_moneda + 3, j.puntos_moneda);
  std::copy(f.rango_s, f.rango_s + 5, j.rango_s);
  std::copy(f.puntos_rango, f.puntos_rango + 5, j.puntos_rango);
  j.puntos_fallo = f.puntos_fallo;
  j.salida_estado = f.salida_estado;
  j.salida_giro = f.salida_giro;
  j.sx = f.sx;
  j.sz = f.sz;
  j.freno_picado = F32(m, 0x2C);   // Viewer_StateFlyUpdate4: +0x560 *= +0x2C
  j.giro_picado = int(U32(m, 0x48));   // estado 2/4: +0x55C -= +0x48 (128)
  auto obj = [](const iwvuelo::Objeto& o) {
    Obj x;
    x.tipo = o.tipo;
    x.flags = o.flags;
    x.x = o.x;
    x.z = o.z;
    x.r = o.r;
    return x;
  };
  j.meta = obj(f.meta);
  j.objs.clear();
  for (const iwvuelo::Objeto& o : f.lista) j.objs.push_back(obj(o));
  j.n_control = f.n_control;
  // EU 0x33E540 LoadWaitFlyingMinigame
  // DS118: visibilidad de los objetos = byte +0x10 del #MSF (el mismo que copia la carga a G+0x504). 891/23 (v0) = 0
  // (uno a uno) y 891/24 (v1) = 1 (todos). Ver Viewer_OnObjectHitEvent 0x33AFA0 / FlyingMinigameFuncs_ 0x33CE40.
  j.mostrar_todos = m.size() > 0x10 && m[0x10] != 0;
  // modelos de IW (891 y la meta del campo de vuelo: la casa del Gran Patriarca 883/7 o la de 881/7)
  j.m_aro = ps2::CargarModelo(amb, 3, 2, 0);
  // DS117: RING_A01 (891/3 + #AMT 2) trae dos partes: 0 = toro (tex 0/2) y 1 = membrana interior (tex 1,
  // translucida). En el original el aro en reposo es HUECO (fotograma 779s: solo el toro dorado); la membrana es
  // el brillo de cruzarlo (Viewer_OnObjectHitEvent 0x33afa0 -> Aura_Dash...). Con la membrana el port pintaba un
  // hexagono verde opaco y el aro "casi no se veia". Se quita del aro del vuelo.
  j.m_aro.malla.tris.erase(
      std::remove_if(j.m_aro.malla.tris.begin(), j.m_aro.malla.tris.end(),
                     [](const ps2::Malla::Tri& tr) { return tr.parte == 1; }),
      j.m_aro.malla.tris.end());
  j.m_moneda = ps2::CargarModelo(amb, 7, 6, 0);
  j.m_item[0] = ps2::CargarModelo(amb, 11, 10, 0);
  j.m_item[1] = ps2::CargarModelo(amb, 14, 13, 0);
  j.m_item[2] = ps2::CargarModelo(amb, 17, 16, 0);
  j.m_item[3] = ps2::CargarModelo(amb, 20, 19, 0);
  // DS117: la flecha del original es ITEMA04_ARROW (primer #AMO de 891 = hijo 1, su #AMT = hijo 0; ver
  // LoadWaitFlyingMinigame EU 0x33E540 lineas 429888-429894: RAMFlyingMinigamesRelated_ busca el primer "AMO" y
  // "AMT"). Su color va en los vertices (vtype 0x838008b6, rojo/naranja con alfa 0,5): CargarSuelto con colores.
  j.m_flecha = ps2::CargarSuelto(P::HijoAmb(amb, 1), P::HijoAmb(amb, 0), false, true, false);
  j.m_casa = ps2::CargarModelo(cmn.Entrada(j.v == 0 ? 883 : 881), 7, 8, 0);
  if (j.v == 1) j.uv_casa_dv = LeerDerivasMta(P::HijoAmb(cmn.Entrada(881), 10));
  // DS182: paquete de efectos del visor = hijo 28 de 891 (el #AMB anidado que Viewer_SetupEffectBundle EU 0x33AA80
  // carga con la etiqueta "AMB"; ver ELF EU 0x49A4B0). Sus 21 hijos: 0 #AMT (atlas de 47 texturas), 1 #AMO, 2 #AMT,
  // 3..20 los 18 #AME que van a ce34[0..17] (el id de Aura_Dash_... es el indice). El atlas es el del hijo 0: los
  // indices de textura de los #AME llegan hasta 44. Los #AME se leen con el reproductor reducido iw_ame.h (el mismo
  // que el minijuego de disparo, frente R13).
  {
    const std::vector<uint8_t> paq = P::HijoAmb(amb, 28);
    if (paq.size() > 0x20 && std::memcmp(paq.data(), "#AMB", 4) == 0) {
      const std::vector<uint8_t> at = P::HijoAmb(paq, 0);
      for (int k = 0, n = P::CuantasAmt(at); k < n; ++k) j.fx_atlas.push_back(P::TexturaAmt(at, k));
      const auto amo=P::HijoAmb(paq,1);
      uint32_t nm=0;
      if(amo.size()>=0x20) std::memcpy(&nm,amo.data()+0x18,4);
      j.fx_modelos.resize(std::min<uint32_t>(nm,64));
      for(size_t i=0;i<j.fx_modelos.size();++i) ps2::LeerAmo(amo,int(i),j.fx_modelos[i],nullptr,true);
      j.efx.resize(18);
      int n_efx = 0;
      for (uint32_t h = 3; h <= 20; ++h) {
        j.efx[h - 3] = ame::Leer(P::HijoAmb(paq, h),true);
        n_efx += j.efx[h - 3] ? 1 : 0;
      }
      j.fx_ok = n_efx > 0;
      int afectores[6]={}, anidados=0;
      std::function<void(const ame::Efecto&)> contar=[&](const ame::Efecto& e) {
        auto nodo=[&](const ame::Nodo& n) { for(const auto& a:n.afectores)
          if(a.tipo>=0&&a.tipo<6) ++afectores[a.tipo]; };
        for(const auto& n:e.raices) nodo(n);
        for(const auto& g:e.grupos) {
          for(const auto& n:g.parts) nodo(n);
          for(const auto& h:g.hijos) { ++anidados;contar(*h); }
        }
      };
      for(const auto& e:j.efx) contar(e);
      REXLOG_INFO("dbz3 iw vuelo9: afectores tipos0..5 {}/{}/{}/{}/{}/{}; emisores anidados {}",
                  afectores[0],afectores[1],afectores[2],afectores[3],afectores[4],afectores[5],anidados);
      REXLOG_INFO("dbz3 r12: efectos del visor (891/28): {} #AME, atlas de {} texturas, #MCF color {:08X} rango "
                  "{:.0f}..{:.0f}; aros id 0xE/0xA, auras de impulso 0/1/2, fijos 4/6/8/B/11",
                  n_efx, j.fx_atlas.size(), j.fog_rgb, j.fog_ini, j.fog_fin);
    } else {
      REXLOG_WARN("dbz3 r12: 891/28 no es el #AMB de efectos ({} B)", paq.size());
    }
  }
  if (P::AfsPs2 usa; usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) {
    j.clear = P::TexturaAmt(P::HijoAmb(usa.Entrada(334), 3), 69);
    const auto paquete=usa.Entrada(5), atlas=P::HijoAmb(paquete,1);
    j.avisos.Cargar(P::HijoAmb(paquete,0));
    for (int i=0,n=P::CuantasAmt(atlas);i<n;++i) j.aviso_atlas.push_back(P::TexturaAmt(atlas,i));
    if (j.v==1) {
      const auto paquete_dialogo=cmn.Entrada(936), obra=P::HijoAmb(paquete_dialogo,0);
      j.dialogo_ama.Cargar(obra);
      if (const int n=j.dialogo_ama.Objeto(0);n>=0) {
        const int seg=j.dialogo_ama.Nodo(n).anim;
        if (seg>=0) {
          int clave=-1;float posicion[4]={};
          anim::ama::MuestrearPosicion(j.dialogo_ama.Seg(seg).a.c2,0,clave,posicion);
          j.dialogo_marco={posicion[0],posicion[1],0};
        }
      }
      const auto retratos=P::HijoAmb(paquete_dialogo,1), textos=P::HijoAmb(paquete,11);
      for (int i=0,n=P::CuantasAmt(retratos);i<n;++i) j.dialogo_atlas.push_back(P::TexturaAmt(retratos,i));
      for (int i=0;i<4;++i) j.dialogo_textos[i]=P::TexturaAmt(textos,i+1);
      // MgFly_State_ShowAndLoadNext toma el centro de la secuencia34.
      const size_t tabla=U32(obra,0x1C);
      if (tabla+35*4<=obra.size()) {
        const size_t seg=U32(obra,tabla+34*4), pista=U32(obra,seg+4), claves=U32(obra,pista+0x1C);
        if (claves+16<=obra.size()) { j.dialogo_pos[0]=F32(obra,claves);j.dialogo_pos[1]=F32(obra,claves+4); }
      }
      uint8_t* base=Base();
      const bool jp=base&&REX_LOAD_U8(dbz3::GA(0x824BA110)+0x2C0C9)==0;
      P::AfsPs2 voces;
      if (voces.Abrir(P::BuscarIW(),jp ? "ADX_JPN.AFS" : "ADX_USA.AFS"))
        for (int i=0;i<4;++i) {
          const auto adx=voces.Entrada(1112+i);
          if (adx.size()<16) continue;
          auto be=[&](size_t o) { return (uint32_t(adx[o])<<24)|(uint32_t(adx[o+1])<<16)|(uint32_t(adx[o+2])<<8)|adx[o+3]; };
          const uint32_t hz=be(8), muestras=be(12);
          if (hz) j.dialogo_voz[i]=int((uint64_t(muestras)*60+hz-1)/hz);
        }
      REXLOG_INFO("dbz3 solIW7: ventana rival CMN936, AMA15/16, texto ({:.0f},{:.0f}), voces {}/{}/{}/{} cuadros",
          j.dialogo_pos[0],j.dialogo_pos[1],j.dialogo_voz[0],j.dialogo_voz[1],j.dialogo_voz[2],j.dialogo_voz[3]);
    }
  }
  // EU 0x33D900: el cutscene de vuelo toma sus clips del primer hijo "AMC" de 891 (RAMFlyingMinigamesRelated,
  // 0x306A20) y del segundo (libro 1). DS113: la escena 0 de Cutscene_GotoLine (EU 0x3424F0) empieza en el libro 1
  // (hijo 26, su unico clip 0 = 300 cuadros de presentacion del campo) y sigue con el libro 0 (hijo 25).
  LeerClipsVuelo(P::HijoAmb(amb, 25), 0,
                 {210, 211, 540, 541, 900, 901, 902, 903, 905, 906, 1400, 1401, 1402, 1403, 1404, 1405, 1406, 1407},
                 j.cams);
  LeerClipsVuelo(P::HijoAmb(amb, 26), 1, {0}, j.cams);
  // Guion de actores de la escena 0 (intro), leido de las tablas del ELF:
  //  - Escena 0 de v0/pj0 en EU 0x483160 -> lineas 0x483120; v0/pj1 en 0x483180 -> 0x483140; v1/pj0 en 0x4831e0 ->
  //    0x4831a0; v1/pj1 en 0x483200 -> 0x4831c0.
  //  - Descriptor de linea (8 B): [u16 clip][u8 libro][u8 n_actores][u32 ptr actores]; actor (4 B): [u8 indice][u8
  //    efecto][u16 anim]. Actores: 0 = modelo del jugador (G+0x548), 1 = Krilin del guion (G+0x7a0, #BSK 924), 2 =
  //    Vegeta (G+0x7a4, #BSK 931). Personajes en 0x483010 y ficheros de animacion en 0x483030.
  //  - Linea 0 = libro 1 clip 0 sin actores; despues 900 (actor 2, anim 880 = 0x370) y 901 (actor 1, anim 885 =
  //    0x375). v1: 0, 1400 (actor 2, 1880), 1401/1404 (actor 1, 1885) y 1407 (actor 2, 1886).
  //  - Escena 1 (meta): 210 (v0pj0), 211 (v0pj1), 540 (v1pj0) o 541 (v1pj1); escenas 2/3 (victoria/derrota) 902/903,
  //    905/906, 1402/1403 y 1405/1406 (el port no las reproduce: el combate de B3 cierra el minijuego).
  j.guion.clear();
  j.clip_meta = 210;
  if (j.v == 0) {
    j.guion = {{1, 0, -1, 0}, {0, 900, 2, 880}, {0, 901, 1, 885}};
    j.clip_meta = j.pj == 0 ? 210 : 211;
  } else if (j.pj == 0) {
    j.guion = {{1, 0, -1, 0}, {0, 1400, 2, 1880}, {0, 1401, 1, 1885}, {0, 1407, 2, 1886}};
    j.clip_meta = 540;
  } else {
    j.guion = {{1, 0, -1, 0}, {0, 1400, 2, 1880}, {0, 1404, 1, 1885}, {0, 1407, 2, 1886}};
    j.clip_meta = 541;
  }
  j.intro_total = 0;
  for (size_t k = 0; k < j.guion.size();) {
    const auto it = j.cams.find((j.guion[k].libro << 16) | j.guion[k].clip);
    if (it == j.cams.end()) {
      REXLOG_WARN("dbz3 r12: falta el clip de intro libro {} clip {}; se quita la linea", j.guion[k].libro,
                  j.guion[k].clip);
      j.guion.erase(j.guion.begin() + std::ptrdiff_t(k));
      continue;
    }
    j.intro_total += it->second.dur;
    ++k;
  }
  j.cs = {};
  for (const Juego::LineaGuion& l : j.guion) {
    dbz3::iwcut::Linea li;
    li.libro = l.libro, li.clip = l.clip;
    li.cuadros = j.cams.at((l.libro << 16) | l.clip).dur;
    if (l.actor >= 0) li.actores.push_back({l.actor, -1, l.anim});
    j.cs.lineas.push_back(std::move(li));
  }
  {
    std::string s;
    for (const Juego::LineaGuion& l : j.guion) {
      const auto it = j.cams.find((l.libro << 16) | l.clip);
      s += fmt::format(" [libro {} clip {} {}f actor {} anim {}]", l.libro, l.clip,
                       it == j.cams.end() ? 0 : it->second.dur, l.actor, l.anim);
    }
    REXLOG_INFO("dbz3 r12: guion de la intro (EU 0x3424F0, escena 0): {} lineas, {} cuadros ({} s):{}", j.guion.size(),
                j.intro_total, j.intro_total / 60.0, s);
  }
  // DS113: voz de la intro (hijos 29..33 del #AMB 891) a un AFS cache y pistas de cada momento.
  j.voces_ok = PrepararVocesVuelo(amb, j);
  j.voz_child[0] = j.v == 0 ? 29 : 32;
  j.voz_child[1] = j.v == 0 ? 30 : 33;
  j.voz_child[2] = 31;
  REXLOG_INFO("dbz3 r12: #AMC 891/25: intro {} {} cuadros, meta {}, victoria {}, derrota {}; {} clips; voces {}",
              j.cams.count(900) ? j.cams[900].dur : 0, j.cams.count(901) ? j.cams[901].dur : 0, j.clip_meta,
              j.cams.count(902) ? j.cams[902].dur : 0, j.cams.count(903) ? j.cams[903].dur : 0, j.cams.size(),
              j.voces_ok ? "OK" : "sin AFS cache");
  // DS117: la flecha es el primer #AMO de 891 (ITEMA04_ARROW, 891/1 + #AMT 0) con color por vertice, no la parte de
  // ITEMA08 de DS92 (esa era una placa de tres galones y salia como una piramide).
  REXLOG_INFO("dbz3 r12: flecha ITEMA04 (891/1+#AMT 0): {} triangulos, {} texturas, color por vertice; objetos {} {} {} {}",
              j.m_flecha.malla.tris.size(), j.m_flecha.tex.size(), j.m_item[0].malla.tris.size(),
              j.m_item[1].malla.tris.size(), j.m_item[2].malla.tris.size(), j.m_item[3].malla.tris.size());
  // DS147: el contador del HUD es controles + meta. #MSF 891/24 medido: dif 0 y 1 = 19 controles => 20 aros (el
  // video); dif 2..5 = 29 controles => 30 aros. La prueba del 27/10 08:09 es dificultad 2, por eso "NN/30" es correcto.
  REXLOG_INFO("dbz3 r12: #MSF {} dificultad {}: salida ({:.0f}, {:.0f}) giro {}, meta ({:.0f}, {:.0f}), {} puntos de "
              "control y {} objetos ({} aros con la meta), limite {} s, medidor {:.4f}/{:.4f}/{:.4f}; objetos todos "
              "visibles {} (#MSF +0x10); modelos aro {} moneda {} casa {}",
              23 + j.v, j.dif, j.sx, j.sz, j.salida_giro, j.meta.x, j.meta.z, j.n_control,
              j.objs.size() - size_t(j.n_control), j.n_control + 1, j.rango_s[4], j.sube[0], j.sube[1], j.sube[2],
              j.mostrar_todos ? "si" : "no (uno a uno)",
              j.m_aro.malla.tris.size(), j.m_moneda.malla.tris.size(), j.m_casa.malla.tris.size());
  return true;
}

// DS182: efectos #AME del visor de vuelo (Viewer_SetupEffectBundle EU 0x33AA80; #AME del paquete 891/28). Los
// parametros (duracion, retraso, radio, velocidad, tope, ritmo, vida, tamano, color, textura, mezcla) salen del
// propio #AME; aqui solo se decide DONDE y CUANDO se lanzan, copiando las llamadas del decompilado:
//  - 0x33D900 (carga del vuelo): Aura_Dash_(4,0,0) (6,0,0) (8,0,0) (0xb,0,0) (0x11,0,0) -> efectos FIJOS del vuelo
//    (las lineas de velocidad son el 8: rayos tex34 de 95..100 u, blancos, emisor esferico; el 6 y el 0xB son las
//    estelas/niebla del cuerpo y el 0x11 las chispas del borde).
//  - 0x33F040 Viewer_StatePoseBank5 (estado 5): mata el hueco 1, lanza 0 y 2. 0x33ECC0 Viewer_StatePoseBank6
//    (estado 6): mata el 0, lanza 1 y 2. 0x33F2C0 PoseTurn / 0x33F5E0 PoseDive (estados 3/4): matan 0, 1 y 2.
//  - 0x33B7D0 Viewer_UpdateObjectCollection/0x33AFA0: aro (tipo 0x100) = 0xE en el punto del choque + 0xA en el
//    cuerpo; tipo 2 = 5 y 0xF; tipo 3 = 7 y 0x10; tipo 8/0x108 = 0xD y 0xC; monedas tipo 1/0x101/0x201 = 9;
//    tipo 0xA = 3.
// El origen: el cuerpo del luchador (el original pasa la posicion en vf0; Aura_Dash la guarda en el hueco y
// Viewer_UpdateEffectSlots la mueve/mata). El slot conserva el indice del original para poder matarlo igual.
namespace Fx {
// EU339E20..339EB8: el id no siempre coincide con la ranura. s_WAIST
// dibuja estas cinco instancias en CADA objeto, no encima del luchador.
int TipoObjeto(int id) {
  switch (id) { case 4:return 0;case 6:return 2;case 8:return 3;case 0xB:return 8;case 0x11:return 1;default:return -1; }
}
V3 Cuerpo(const Juego& j) { return V3{j.x, kAltura, j.z}; } // G+5D0, no un ancla403 ajustada
V3 Rumbo(int yaw) {
  const float a = Ang(yaw);
  return V3{std::sin(a), 0.0f, std::cos(a)};
}
std::shared_ptr<ame::Instancia> Lanzar(Juego& j, int id, int slot, const V3& pos, const V3& dir, bool sigue,
                                       float esc = 1.0f) {
  if (!j.fx_ok || id < 0 || id >= int(j.efx.size()) || !j.efx[size_t(id)]) return nullptr;
  auto in = std::make_shared<ame::Instancia>();
  in->ef = &j.efx[size_t(id)];
  // Los efectos que siguen al cuerpo se SIMULAN en el marco local y se
  // transforman solo al dibujar (RAM original025: contexto y emisores en0).
  in->origen = sigue ? V3{} : pos;
  in->dir = sigue ? V3{0,0,1} : dir;
  in->esc = esc;
  in->mezcla_gs = true;
  in->modelos = !j.fx_modelos.empty();
  Juego::FxVivo v;
  v.id = id;
  v.slot = slot;
  v.sigue = sigue;
  v.in = in;
  j.fx.push_back(std::move(v));
  return in;
}
// Viewer_KillEffectAt (EU 0x33A910): marca muerto el efecto del hueco.
void Matar(Juego& j, int slot) {
  for (Juego::FxVivo& f : j.fx)
    if (f.slot == slot) f.in->vivo = false, f.slot = -1000;
}
// Viewer_UpdateEffectSlots (EU 0x33A000) + EffectDrawRegulator: avanza un cuadro los efectos vivos.
void Cuadro(Juego& j) {
  if (j.fx.empty()) return;
  // Simular en el marco original; su traslacion y giro se aplican al pintar.
  for (Juego::FxVivo& f : j.fx) {
    if (!f.in||!f.in->vivo) continue; // Matar no debe reactivar un emisor infinito al actualizarlo
    ame::Cuadro(*f.in, j.fx_sem);
  }
  j.fx.erase(std::remove_if(j.fx.begin(), j.fx.end(), [](const Juego::FxVivo& f) { return !f.in || !f.in->vivo; }),
             j.fx.end());
}
void Limpiar(Juego& j) {
  j.fx.clear();
  j.fx_iniciado = false;
  j.fx_estado = -1;
}
// EU33CFA0: plantillas de los cinco efectos de objetos; el dibujo las repite en cada nodo activo.
void Iniciar(Juego& j) {
  if (!j.fx_ok) return;
  const V3 p{}, d{0,0,1};
  for (int id : {4, 6, 8, 0xB, 0x11}) Lanzar(j, id, id, p, d, false);
  j.fx_iniciado = true;
  j.fx_estado = j.estado;
  REXLOG_INFO("dbz3 solIW7: efectos fijos 4/6/8/B/11 en objetos 0/2/3/8/1; no en el cuerpo");
}
// Transiciones de los estados de impulso (0x33F040/0x33ECC0/0x33F2C0/0x33F5E0).
void Estado(Juego& j, int estado) {
  if (!j.fx_ok || estado == j.fx_estado) return;
  const V3 p = Cuerpo(j), d = Rumbo(j.yaw);
  if (estado == 5) {   // EU33F040:0/3→5 solo0;6→5 mata1 y lanza0+2
    Lanzar(j, 0, 0, p, d, true);
    if(j.fx_estado==6) {Matar(j,1);Lanzar(j,2,2,p,d,true);} // EU33F040: solo6→5 lleva2
  } else if (estado == 6) {   // Viewer_StatePoseBank6: KillEffectAt(0) + Aura 1 y 2
    Lanzar(j, 1, 1, p, d, true);
    if(j.fx_estado==5) Matar(j,0);
    Lanzar(j,2,2,p,d,true); // EU33ECC0: tanto0/3→6 como5→6
  } else if (estado == 3 || estado == 4) {   // Viewer_StatePoseTurn / PoseDive: KillEffectAt(0/1/2)
    Matar(j, 0);
    Matar(j, 1);
    Matar(j, 2);
  }
  j.fx_estado = estado;
}
}  // namespace Fx

// Pone la partida en la salida (READY y primer cuadro)
void Reiniciar(Juego& j,bool reiniciar_efectos=true) {
  j.x = j.ux = j.sx;
  j.z = j.uz = j.sz;
  j.yaw = j.salida_giro;
  if (const char* e = std::getenv("DBZ3_R12_SALIDA")) {   // pruebas: "x,z,giro" (p. ej. delante de un objeto)
    float x = 0, z = 0;
    int g = 0;
    if (std::sscanf(e, "%f,%f,%d", &x, &z, &g) == 3) j.x = j.ux = x, j.z = j.uz = z, j.yaw = g & 0xFFFF;
  }
  j.estado = j.salida_estado;
  j.vel = j.t568 = j.t56c = j.congelado = j.t_ms = 0;
  j.medidor = j.medidor0;
  j.monedas = j.puntos = j.cuadros = 0;
  j.cabeceo = -0x800, j.balanceo = 0;   // CameraFocusWhileFlying(52.2, -0x800): +0x604 = pitch, +0x608 = 0
  // DS124: el estado de la camara de orbita tambien se reinicia. Si un minijuego anterior (o el reinicio del visor,
  // Viewer_RestartMinigame 0x33E1B0) dejo zoom, rumbo de camara o virada, el primer cuadro del vuelo salia con la
  // camara corrida (distance 52,2 + zoom, rumbo retrasado) y el luchador fuera de su sitio.
  j.zoom = j.zoom_meta = 0;
  j.cam_yaw = -1;
  j.est_cam = j.estado;
  j.cam_x = j.cam_y = 0;
  j.virada = j.media = 0;
  j.picado_t = 0;
  j.pose_aro = false;
  j.codigo_vuelo = 0;
  j.orig = 0; j.congelar_pend = false; j.anim_prev = 0;   // T4B: la logica original vuelve a montarse
  j.espera_cam = 0;
  j.cam_ped_ok = false;
  j.cam_esc_ok = false;
  j.cuadros_cam = 0;
  ApagarCamaraVuelo();   // DS132: sin camara publicada hasta que el vuelo vuelva a pedir la suya
  j.restantes = j.n_control;
  j.siguiente = 0;
  j.abierta = j.fin = j.superado = false;
  j.aura = 0;
  if(reiniciar_efectos) {
    Fx::Limpiar(j);
    Fx::Iniciar(j); // EU33CE40: nacen antes de la intro, no al terminar READY
  }
  j.listo_t = 0;
  j.aviso_id=-1;j.aviso_t=0;j.derrota_espera=j.derrota_vista=false;
  j.voz_inicio = false;
  j.eventos = 0;
  j.voz_aro = 0;
  j.meta.activo = false;
  for (size_t k = 0; k < j.objs.size(); ++k) {
    Obj& o = j.objs[k];
    // DS118: G+0x504 (#MSF +0x10): 1 = todos visibles (891/24, 0x7E0) y 0 = solo el primer punto de control
    // (891/23, 0x7D6). Los objetos de la lista L2 (monedas, velocidad...) siempre estan activos.
    o.activo = k >= size_t(j.n_control) || j.mostrar_todos || k == 0;
    o.reaparece = 0;
  }
  j.rastro.clear();
  j.p_dibujo_ok = false;
}

  // En IW el rival NO tiene cuerpo durante el vuelo. FlyingMinigameSpritesScripts (0x33C660) actualiza el medidor
  // (Vegeta_GotenMeterSpriteMovementSpeed_MaxReach...) y al llegar a 0,8 inicia el aviso de acoso; la función del
  // sprite muestrea la animación y posiciona los sprites del HUD, no un luchador del mundo. Los modelos B3 que el port
  // colocaba detrás del jugador eran una adición no fiel: no dibujar el rival en ninguna de las dos variantes. El
  // progreso/derrota sigue gobernado por j.medidor en PasoVuelo.
// EU 0x33C660 FlyingMinigameSpritesScripts/MissionObjectiveScripts
void Rival(Juego& j) {
  auto en = [&](const V3& a, const V3& b, float u) { return V3{a.x + (b.x - a.x) * u, 0, a.z + (b.z - a.z) * u}; };
  if (j.v == 0) {
    float falta = (1.0f - std::clamp(j.medidor, 0.0f, 1.0f)) * kRivalB3 / j.escala;
    V3 p{j.ux, 0, j.uz}, d{0, 0, 0};
    for (auto it = j.rastro.rbegin(); it != j.rastro.rend(); ++it) {
      const float l = std::hypot(it->x - p.x, it->z - p.z);
      if (l >= falta && l > 0) {
        d = {p.x - it->x, 0, p.z - it->z};
        p = en(p, *it, falta / l);
        falta = 0;
        break;
      }
      falta -= l;
      d = {p.x - it->x, 0, p.z - it->z};
      p = *it;
    }
    if (falta > 0) p = {p.x - std::sin(Ang(j.yaw)) * falta, 0, p.z - std::cos(Ang(j.yaw)) * falta};
    j.rival_p = p;
    if (d.x != 0 || d.z != 0) j.rival_yaw = int(std::lround(std::atan2(d.x, d.z) / 6.2831853f * 65536.0f));
    else j.rival_yaw = j.yaw;
    return;
  }
  std::vector<V3> ruta{{j.sx, 0, j.sz}};
  for (int k = 0; k < j.n_control; ++k) {
    const V3& a = ruta.back();
    ruta.push_back({a.x + Envolver(j.objs[size_t(k)].x - a.x, j.periodo), 0, a.z + Envolver(j.objs[size_t(k)].z - a.z, j.periodo)});
  }
  {
    const V3& a = ruta.back();
    ruta.push_back({a.x + Envolver(j.meta.x - a.x, j.periodo), 0, a.z + Envolver(j.meta.z - a.z, j.periodo)});
  }
  float total = 0;
  for (size_t k = 1; k < ruta.size(); ++k) total += std::hypot(ruta[k].x - ruta[k - 1].x, ruta[k].z - ruta[k - 1].z);
  float falta = std::clamp(j.medidor, 0.0f, 1.0f) * total;
  for (size_t k = 1; k < ruta.size(); ++k) {
    const float l = std::hypot(ruta[k].x - ruta[k - 1].x, ruta[k].z - ruta[k - 1].z);
    if (falta <= l || k + 1 == ruta.size()) {
      j.rival_p = en(ruta[k - 1], ruta[k], l > 0 ? std::min(falta / l, 1.0f) : 0);
      j.rival_yaw = int(std::lround(std::atan2(ruta[k].x - ruta[k - 1].x, ruta[k].z - ruta[k - 1].z) / 6.2831853f * 65536.0f));
      break;
    }
    falta -= l;
  }
}

// IW -> B3: el mundo de IW se junta al campo de R8 (+-1476) alrededor del centro; la altura, 400 x escala sobre el
// suelo del campo si lo hay
V3 AB3(const Juego& j, float x, float z) {
  const float rx = Envolver(x, j.periodo) * j.escala, rz = Envolver(z, j.periodo) * j.escala;
  // En los campos de vuelo 883/881 la logica de B3 pega al luchador a su suelo (sin #WCF: 0, el mar; medido con
  // DBZ3_R12_SONDA) y la camara del juego lo sigue alli: por eso, mientras dura el vuelo, la consulta de suelo del juego
  // responde 400 (suelo_iw::SueloFijo) y el luchador vuela "en el suelo" a la altura de IW.
  // DS90: el suelo que cuenta es la MALLA QUE SE DIBUJA (AlturaMalla), no el #WCF del anfitrion: el 883 no trae #WCF
  // propio y Altura() devuelve el plano 0 de Llanuras, asi que el max(400, suelo+30) nunca subia y las mesetas del
  // campo (hasta ~900-1000 en el 883; la ruta pasa por (100,3500) con h~940 y por (0,-6500) con cumbres ~3900) se
  // quedaban muy por debajo del personaje. Con la malla, sobre una meseta vuela 30 por encima de ella (como en el
  // original) y en el llano se mantiene la altura 400 del decomp (0x43C80000).
  float y = kAltura * j.escala;
  if (j.campo) {
    // DS101: en los campos reales de vuelo (883/881) la consulta del port (suelo_iw) no los conoce; se usa la malla
    // leida del AFS. En los campos de respaldo (900/903 con escala != 1) se mantiene la consulta de siempre.
    float suelo = (j.vuelo_real && j.terreno.vale) ? j.terreno.Altura(rx, rz, -1e30f) : -1e30f;
    if (suelo <= -1e29f) suelo = dbz3::suelo_iw::AlturaMalla(rx, rz, -1e30f);
    if (suelo <= -1e29f) suelo = dbz3::suelo_iw::Altura(rx, rz, 0.0f);
    y = std::max(y, suelo + 30.0f);
  }
  return {rx, y, rz};
}

// EU 0x33C190 Viewer_StateResultSuccess
// EU 0x33C420 Viewer_StateResultFail
bool FinAviso(const Juego& j) {
  const int nodo=j.avisos.Objeto(uint32_t(j.aviso_id));
  if (nodo<0) return j.aviso_t>= (j.aviso_id==0 ? 90 : 121);
  // EU342C20/1B7AF0 consulta la RAIZ, no MaxCuadros del arbol:
  // READY92 acaba aunque su hijo START siga hasta134.
  return anim::ama::BanderasFin(j.avisos,j.avisos.Nodo(nodo).anim,float(j.aviso_t))==0xFF;
}
void Acabar(uint8_t* base, uint32_t mgr, Juego& j, bool meta, const char* por) {
  const bool superado = meta && j.t_ms <= float(j.rango_s[4]) * 1000.0f;
  if (!meta&&!j.derrota_vista&&!j.resultado_visto&&j.vuelo_real) {
    if (!j.derrota_espera) {
      j.derrota_espera=true;j.aviso_id=8;j.aviso_t=0;
      if (j.estado==3||j.estado==5||j.estado==6) {
        j.estado=4;j.anim_picado=0x32E;j.vel=j.vfreno;j.zoom_meta=-10;
        j.pose_aro=false;dbz3::anim::Bucle(mgr+kX0,0x32E);
      }
      REXLOG_INFO("dbz3 solIW6: vuelo derrota frenazo32E estado {} espera AMA8",j.estado);
    }
    return;
  }
  // EU 0x33C340 Viewer_StateCheckResult / 0x33C550 LoadWaitWinLoseScreenFlyingMinigame:
  // la escena 2/3 se reproduce ANTES de escribir el resultado. DATA_CMN 924
  // trae los codigos 0x371/0x372 (240 cuadros) ya cargados por anim_iw.
  // Cada pareja conserva su propio #BSK y sus clips (ELF EU 0x483220).
  if (!j.resultado_visto && j.vuelo_real) {
    j.resultado_visto = true;
    const int clip = (j.v == 0 ? (j.pj == 0 ? 902 : 905) : (j.pj == 0 ? 1402 : 1405)) + (superado ? 0 : 1);
    if (j.cams.count(clip)) {
      j.escena_cierre = superado ? 2 : 3;
      j.cierre = true;
      j.cierre_t = 0;
      j.superado = superado;
      if (meta) { j.aviso_id=superado ? 7 : 8;j.aviso_t=0; }
      Fx::Limpiar(j);
      if (!meta) VozCorta(j, 1250);
      REXLOG_INFO("dbz3 r12: resultado de IW: escena {} clip {} anim 0x{:X}, {} cuadros antes de salir",
                  j.escena_cierre, clip, j.v == 0 ? (superado ? 0x371 : 0x372) : (superado ? 0x759 : 0x75A),
                  j.cams.at(clip).dur);
      return;
    }
    REXLOG_WARN("dbz3 r12: falta el clip {} de resultado de IW; se entrega el resultado sin escena", clip);
  }
  j.fin = true;
  // EU 0x33C340 Viewer_StateCheckResult
  j.superado = superado;   // Viewer_StateCheckResult
  REX_STORE_U8(mgr + kFin, uint8_t(j.superado ? 1 : 2));          // el combate acaba como un ring out
  // EU 0x33C660 FlyingMinigameSpritesScripts_MissionObjectiveScripts
  // DS118: al perder, la rama `bVar2` corta la musica del canal 2 y lanza
  // CharacterDialoguesStart_(2, Table_Lookup480450(SemaParam.count)|0x30000) = 1250 (ADX_USA/JPN). La victoria ya
  // la lanza Viewer_BeginFinalPhase (1253/1250) en PasoCierre.
  if (!meta && !j.resultado_visto) VozCorta(j, 1250);
  REXLOG_INFO("dbz3 r12: fin ({}): tiempo {:.2f} s, medidor {:.3f}, monedas {} ({} puntos), superado {}", por,
              j.t_ms / 1000.0f, j.medidor, j.monedas, j.puntos, j.superado);
}

// Pose del jugador en cada estado del vuelo (auditoria7/J, visor de vuelo de IW: DATA_CMN 915; Viewer_StateFlyUpdate0
// 0x340D70, 3 0x3403A0, 5 0x33FEF0, Viewer_StatePoseBank5 0x33F040): reposo 0x320 (800), subir 0x328, bajar 0x324, de
// lado 0x329/0x32A, crucero 0x322, planeo 0x323 (palanca < 0.25), frenazo 0x32E, impulso con R1 0x32C; el estado 4
// (desaceleracion, Viewer_StateFlyUpdate4 0x3407D0) usa 0x323 o 0x32E segun Viewer_StatePoseDive 0x33F5E0. La mezcla
// de IW es CharModel_SetAnimBlend 1/5 (1/15 si venia de 0x330); el port cambia de animacion sin mezcla. [VERIFICAR] en
// pantalla el arranque/parada (0x323/0x32E) y la vuelta al reposo.
// EU 0x341770 FlyingMinigameCharacterHeightPosition
uint32_t PoseVuelo(const Juego& j, float h, float v) {
  if (j.retorno_reposo) return 0x320;
  if (j.pose_aro) return 0x330;
  if (j.estado == 7) return 0x32D;
  if (j.estado == 2) return 0x321;
  if (j.estado == 4) return uint32_t(j.anim_picado);
  if (j.estado == 5 || j.estado == 6) return 0x32C;
  if (j.estado == 3) return v >= 0.25f ? 0x322 : 0x323;
  if (v > 0) return 0x328;
  if (v < 0) return 0x324;
  if (h != 0) return h > 0 ? 0x329 : 0x32A;
  return 0x320;
}
void AplicarPoseVuelo(uint32_t x,Juego& j,uint32_t codigo) {
  if (codigo==j.codigo_vuelo) return;
  j.codigo_vuelo=codigo;
  // Las transiciones no son ciclos. El camino sujeto conserva el avance
  // nativo a60Hz, sin el reloj de respaldo de las poses en bucle.
  if (codigo==0x321||codigo==0x32D||codigo==0x323||codigo==0x32E||codigo==0x330||codigo==0x32F)
    dbz3::anim::Reproducir(x,codigo,true);
  else dbz3::anim::Bucle(x,codigo);
}

// Un cuadro de vuelo (hilo del juego): Viewer_StateFlyUpdate0/3/5/6 con los numeros del #MSF.
// EU 0x340AA0 Viewer_StateFlyUpdate2 y 0x33FA10 Viewer_StateFlyUpdate6: mismo esquema que los demas estados: el rumbo
// (+0x55c, 1/65536 de vuelta) gira con el eje horizontal x la tasa del #MSF (+0x48 en el 2, +0x54/0x38 en el 6),
// +0x558 = (+0x558 + lado*4096) >> 1 (balanceo), la velocidad +0x560 = max(+0x560 x freno, empuje x v^2 x delta) y la
// posicion +0x5c0/+0x5c8 += direccion x velocidad, con Node_WrapPositionInGrid. EU 0x340CB0 Viewer_StateFlyUpdate7: la
// media vuelta es `+0x55c ^= 0x8000` (180 grados) al acabar la pose 0x32D, y luego el picado. Todo eso es Volar()/j.media.
// EU 0x32C250 Viewer_SetFlags y 0x33E870 Viewer_BeginMinigameLoadAlt: un setter de flag del visor y el arranque de la
// carga (LoadWaitFlyingMinigame(0, tipo == 2)); aqui lo hace Empezar (variante v = 0/1) y el registro del modulo.
// EU 0x33E1B0 Viewer_RestartMinigame y 0x33C550 LoadWaitWin/LoseScreenFlyingMinigame: reinicio (libera 0x19 sprites,
// 0x15 efectos y llama a FlyingMinigameFuncs_) y la pantalla de fin (Viewer_ClearAllEffectHolds, MgFly_RunLoadTable y
// Cutscene_GotoLine(...,3,0) -> Viewer_StateResultFail). El port no reinicia dentro del combate: al acabar ensena su
// resultado y vuelve al mapa (Terminar); `motor`.
// EU 0x3414D0 ObjectsCollectionTeleport: suma un desplazamiento a +0x5c0/+0x5d0/+0x5f0 y rehace la matriz y el
// envoltorio del mundo (Node_WrapPositionInGrid); es la recolocacion de los objetos/altura al empezar y en cada vuelta:
// aqui lo hace Envolver() sobre j.x/j.z y kAltura sobre la y.
// EU 0x345CE0 LoadWaitFlyingMinigameSpecialScripts: copia la tabla 0x483460 (6 pares) y anade el sprite del medidor
// (`+0x4f0 + 7 | 0x8000`), carga LoadWaitFlyingMinigamesContent y monta el medidor con +0x51C. Aqui el medidor es el
// propio HUD (dibujado con las texturas de 891/0) y el rival derivado de el: `motor`.
// EU 0x340D70 Viewer_StateFlyUpdate0
// EU 0x3403A0 Viewer_StateFlyUpdate3
// EU 0x3407D0 Viewer_StateFlyUpdate4
// EU 0x33FEF0 Viewer_StateFlyUpdate5
// EU 0x33FA10 Viewer_StateFlyUpdate6
// EU 0x340CB0 Viewer_StateFlyUpdate7
void Volar(uint8_t* base, uint32_t x, Juego& j, float h, float v, bool r1) {
  j.retorno_reposo=false;
  auto acabada = [&](uint32_t codigo) {
    dbz3::anim::EstadoCuerpo e;
    const bool fin=dbz3::anim::LeerEstadoCuerpo(base,x,0,e) && e.hay &&
        e.codigo==codigo && e.cuadros>0 && (e.vueltas || e.cuadro>=float(e.cuadros-1));
    if (fin&&std::getenv("DBZ3_TEST_IWMINI"))
      REXLOG_INFO("dbz3 solIW6: vuelo fin pose {:X} cuadro {:.1f}/{} estado {}",codigo,e.cuadro,e.cuadros,j.estado);
    return fin;
  };
  if (j.pose_aro && acabada(0x330)) j.pose_aro=false;
  if (j.estado==7) {
    // EU340CB0: no corre la fisica de crucero durante la media vuelta.
    if (!acabada(0x32D)) return;
    j.yaw^=0x8000;
    j.estado=0;
    j.retorno_reposo=true;
    return;   // EU340CB0 -> PoseDive: instala800 ANTES de aceptar otra media vuelta
  }
  if (j.estado==2 && acabada(0x321)) j.estado=3;
  const int estado_antes=j.estado;
  const float rumbo_antes=Ang(j.yaw);
  if (v < 0 && j.estado == 0) v *= 0.8f;
  if (j.estado == 4) {
    // Viewer_StateFlyUpdate4 0x3407D0: giro del estado 2 (+0x48), +0x560 *= +0x2C (0,95); con la palanca abajo y
    // velocidad >= +0x24 pasa a 0x32E, +0x560 = +0x28 y +0x614 = +0x61C (-10). Al acabar la transicion, PoseDive
    // (caso 4) vuelve al estado 0 con la animacion 800.
    j.yaw = (j.yaw - int(h * float(j.giro_picado))) & 0xFFFF;
    j.vel *= j.freno_picado;
    j.balanceo >>= 1;   // Viewer_StateFlyUpdate4: +0x558 = +0x558 >> 1
    if (v < 0.0f && j.vel >= j.vmin) {
      j.vel = j.vfreno;
      j.anim_picado = 0x32E;
      j.zoom_meta = -10.0f;
    } else if (acabada(uint32_t(j.anim_picado))) {
      j.estado = 0;
      j.anim_picado = 0x320;
      j.retorno_reposo=true;
    }
  } else {
    // cambios de estado: R1 = 5 (6 con el objeto 2), sin R1 vuelve a 3; el objeto 3 impide R1
    const bool impulso = r1 && j.t56c <= 0 && v >= -0.75f;
    if (j.estado==0&&r1&&v< -0.75f&&h==0) {
      j.estado=7;
      j.pose_aro=false;
      return;
    }
    if (impulso) j.estado = j.t568 > 0 ? 6 : 5;
    else if (j.estado == 5 || j.estado == 6) j.estado = 3;
    else if (j.estado == 0 && v > 0.5f) j.estado = 2;
    const int tasa = estado_antes == 0 ? j.giro0 : j.estado==2 ? j.giro_picado : j.giro[j.estado == 3 ? 0 : j.estado == 5 ? 1 : 2];
    j.yaw = (j.yaw - int(h * float(tasa))) & 0xFFFF;
    if (estado_antes == 0) {
      // EU340D70: primero desplaza con el hover del estado0 y DESPUES
      // instala el impulso. Cambiar antes a5 adelantaba34 en vez de16.
      j.vel = j.hover * v * std::fabs(v);
    } else {
      const int k = j.estado == 3 || j.estado==2 ? 0 : j.estado == 5 ? 1 : 2;
      const float empuje = v > 0 ? j.empuje[k] * v * v : 0.0f;
      j.vel = std::max(j.vel * j.freno[k], empuje);
      bool frena = false;
      if (k == 0 && v < 0 && j.vel >= j.vmin) {   // frenazo: +0x560 = +0x28 (Viewer_StateFlyUpdate3)
        j.vel = j.vfreno;
        frena = true;
      }
      if (j.estado == 3 && v < 0.25f) {   // Viewer_StatePoseDive (casos 3/5/6): planeo 0x323 o frenazo 0x32E
        j.estado = 4;
        j.anim_picado = frena ? 0x32E : 0x323;
        if (frena) j.zoom_meta = -10.0f;
      }
    }
  }
  if (j.pose_aro && j.estado!=j.estado_aro) j.pose_aro=false;
  if (estado_antes!=j.estado && std::getenv("DBZ3_TEST_IWMINI"))
    REXLOG_INFO("dbz3 solIW6: vuelo estado {} -> {}",estado_antes,j.estado);
  const float a = estado_antes==0 ? rumbo_antes : Ang(j.yaw), dx = std::sin(a) * j.vel, dz = std::cos(a) * j.vel;
  j.ux += dx;
  j.uz += dz;
  j.x = Envolver(j.x + dx, j.periodo);
  j.z = Envolver(j.z + dz, j.periodo);
}

// Objetos (Viewer_UpdateObjectCollection): barrido en el plano desde el sitio anterior
void Recoger(uint8_t* base, uint32_t mgr, Juego& j, float x0, float z0, const dbz3::iwmips::vuelo7::Salida* orig = nullptr) {
  auto toca = [&](const Obj& o) {
    const float ax = Envolver(x0 - o.x, j.periodo), az = Envolver(z0 - o.z, j.periodo);
    const float bx = Envolver(j.x - o.x, j.periodo), bz = Envolver(j.z - o.z, j.periodo);
    const float sx = bx - ax, sz = bz - az, l2 = sx * sx + sz * sz;
    const float u = l2 > 0 ? std::clamp(-(ax * sx + az * sz) / l2, 0.0f, 1.0f) : 0.0f;
    const float cx = ax + sx * u, cz = az + sz * u;
    return cx * cx + cz * cz <= o.r * o.r;
  };
  for (size_t k = 0; k < j.objs.size(); ++k) {
    Obj& o = j.objs[k];
    bool cogido;
    if (orig) {   // T4B: Viewer_UpdateObjectCollection del ELF; el objeto k es el k+1 de su tabla (el 0 es la meta)
      const bool visible = k + 1 < size_t(orig->n_obj) && !(orig->flags[k + 1] & 0x8000);
      cogido = o.activo && !visible;
      o.activo = visible;
      if (!cogido) continue;
    } else {
      if (!o.activo) {
        if (o.reaparece > 0 && (o.reaparece -= 1.0f) <= 0) o.activo = true;
        continue;
      }
      if (!toca(o)) continue;
      o.activo = false;
      if (o.flags & 0x4000) o.reaparece = j.reaparece;
    }
    if (o.tipo == kControl) {
      --j.restantes;
      if (!orig && !j.mostrar_todos && k + 1 < size_t(j.n_control)) j.objs[k + 1].activo = true, j.siguiente = k + 1;
      REXLOG_INFO("dbz3 r12: punto de control {} ({} quedan)", k, j.restantes);
      // EU 0x33AFA0 Viewer_OnObjectHitEvent
      // v0: al PRIMER aro (quedan n-1) y cuando queda 1 se dispara el dialogo del rival
      // (Timelapse_...Visuals(1)/(2) -> Cutscene_QueueEvent2C/2D, Audio_GetFilteredTableEntry(0x6C, 0x14/0x15) =
      // ADX 957/958) EN LUGAR de la frase del jugador. Si no, la frase sale de DAT_00483230 (ELF EU 0x483230).
      bool dialogo = false;
      if (j.v == 0) {
        if (j.restantes == j.n_control - 1 && !(j.eventos & 0x10)) {
          j.eventos |= 0x10;
          dialogo = true;
          VozBanco3(j, 957, "dialogo del rival, primer aro (EU 0x33AFA0)");
        } else if (j.restantes == 1 && !(j.eventos & 0x20)) {
          j.eventos |= 0x20;
          dialogo = true;
          VozBanco3(j, 958, "dialogo del rival, ultimo aro (EU 0x33AFA0)");
        }
      }
      if (!dialogo) {
        // DAT_00483230 (ELF EU 0x483230, 24 bytes) = indice de voz dentro de la fila del personaje, por
        // [pj][tarde][variante]. Se lee el VALOR de la tabla (no el indice): v0 pj1 usa 12..14 / 15..17.
        static constexpr uint8_t kVozAro[24] = {3, 4, 5, 6, 7, 8, 12, 13, 14, 15, 16, 17,
                                                3, 4, 5, 6, 7, 8, 3,  4,  5,  6,  7,  8};
        const bool tarde = j.t_ms >= float(j.rango_s[4]) * 700.0f && (j.n_control >> 1) < j.restantes;
        const int pos = (j.voz_aro++ % 3) + (tarde ? 3 : 0) + j.pj * 6 + j.v * 12;
        VozJugador(j, kVozAro[pos], tarde ? "aro cruzado (tarde)" : "aro cruzado");
      }
      if (!orig && j.restantes <= 0 && !j.abierta) {
        j.abierta = j.meta.activo = true;
        REXLOG_INFO("dbz3 r12: meta abierta en ({:.0f}, {:.0f})", j.meta.x, j.meta.z);
      }
      // DS182: Viewer_UpdateObjectCollection EU 0x33B7D0 rama uVar2==0x100: Aura_Dash_(0xE, punto_del_choque,
      // 0x8110) y despues Viewer_OnObjectHitEvent 0x33AFA0 -> Aura_Dash_(0xA, 0, 1). El 0xE es la explosion del aro
      // (hijo 17: puffs verdes 4 u + rayos 5..8 u) y el 0xA la rafaga que queda en el cuerpo (hijo 13).
      {
        const V3 p = Fx::Cuerpo(j), d = Fx::Rumbo(j.yaw);
        Fx::Lanzar(j, 0xE, 0xE, p, d, false);
        Fx::Lanzar(j, 0xA, 0xA, p, d, true);
        j.pose_aro=true;
        j.estado_aro=j.estado;
        AplicarPoseVuelo(mgr+kX0,j,0x330);
        if (std::getenv("DBZ3_TEST_IWMINI")) REXLOG_INFO("dbz3 solIW6: aro pose330 efectosE/A");
      }
    } else if (EsMoneda(o.tipo)) {
      ++j.monedas;
      j.puntos += j.puntos_moneda[(o.tipo >> 4) % 3];
      // DS182: tipo 1/0x101/0x201 -> Aura_Dash_(9, 0, 1) (hijo 12: chispas radiales).
      Fx::Lanzar(j, 9, 9, Fx::Cuerpo(j), Fx::Rumbo(j.yaw), true);
    } else if (o.tipo == 0xA) {
      // EU 0x33B7D0 Viewer_UpdateObjectCollection, rama uVar2==10: aura dash. El objeto es un disparador, no un
      // cartel del recorrido: [VERIFICAR] la duracion exacta del efecto (aqui 60 cuadros, 1 s a 60 Hz).
      j.aura = 60.0f;
      Fx::Lanzar(j, 3, 3, Fx::Cuerpo(j), Fx::Rumbo(j.yaw), true);   // DS182: Aura_Dash_(3, punto, 0)
    } else if (o.tipo == 2) {
      j.t568 = j.dur2;
      j.t56c = 0;
      Fx::Lanzar(j, 5, 5, Fx::Cuerpo(j), Fx::Rumbo(j.yaw), true);    // DS182: Aura_Dash_(5, 0, 1)
      Fx::Lanzar(j, 0xF, 0xF, Fx::Cuerpo(j), Fx::Rumbo(j.yaw), true);
    } else if (o.tipo == 3) {
      j.t56c = j.dur3;
      j.t568 = 0;
      Fx::Lanzar(j, 7, 7, Fx::Cuerpo(j), Fx::Rumbo(j.yaw), true);    // DS182: Aura_Dash_(7, 0, 1)
      Fx::Lanzar(j, 0x10, 0x10, Fx::Cuerpo(j), Fx::Rumbo(j.yaw), true);
    } else if (o.tipo == 8 || o.tipo == 0x108) {
      j.congelado = j.congela; j.congelar_pend = orig != nullptr;   // T4B: con el original, ctx+0x520 = +0x24 el cuadro siguiente. [VERIFICAR]: en IW +0x520 lanza Vegeta_GotenHarassmentEffectDraw(8) antes de congelar
      Fx::Lanzar(j, 0xD, 0xD, Fx::Cuerpo(j), Fx::Rumbo(j.yaw), true);   // DS182: Aura_Dash_(0xD, punto, 0x8210)
      Fx::Lanzar(j, 0xC, 0xC, Fx::Cuerpo(j), Fx::Rumbo(j.yaw), true);   //        + Aura_Dash_(0xC, 0, 1)
    }
    REXLOG_INFO("dbz3 r12: objeto {} (tipo 0x{:X}) cogido", k, o.tipo);
  }
  if (orig) {   // T4B: la meta es el objeto 0 de la tabla; Viewer_BeginFinalPhase avisa de que se cruzo
    j.meta.activo = !(orig->flags[0] & 0x8000);
    if (j.meta.activo && !j.abierta) { j.abierta = true; REXLOG_INFO("dbz3 r12: meta abierta (original) en ({:.0f}, {:.0f})", j.meta.x, j.meta.z); }
  }
  if (orig ? orig->meta : (j.meta.activo && toca(j.meta))) {
    // 0x33C190 Viewer_StateResultSuccess: antes de CLEAR va la parte 1 del guion (linea 210, aterrizaje + pose).
    if (!j.cierre) {
      j.cierre = true;
      j.cierre_t = 0;
      j.aviso_id=2;j.aviso_t=0;
    }
  }
}

// Piloto de prueba ("auto"): gira hacia el objetivo y vuela con R1 mientras el medidor lo permite
void Piloto(const Juego& j, float& h, float& v, bool& r1) {
  const Obj* o = j.meta.activo ? &j.meta : nullptr;
  float mejor = 1e30f;
  for (int k = 0; k < j.n_control; ++k) {
    const Obj& c = j.objs[size_t(k)];
    if (!c.activo) continue;
    const float d = std::hypot(Envolver(c.x - j.x, j.periodo), Envolver(c.z - j.z, j.periodo));
    if (!j.meta.activo && d < mejor) mejor = d, o = &c;
  }
  if (!o) {
    h = 0, v = 1, r1 = false;
    return;
  }
  const float dx = Envolver(o->x - j.x, j.periodo), dz = Envolver(o->z - j.z, j.periodo);
  float dif = std::atan2(dx, dz) - Ang(j.yaw);
  while (dif > 3.14159265f) dif -= 6.2831853f;
  while (dif < -3.14159265f) dif += 6.2831853f;
  h = std::clamp(-dif * 4.0f, -1.0f, 1.0f);   // giro -= h: a la izquierda (dif > 0) con h < 0
  // radio de giro = vel / (tasa en radianes): si el objetivo queda dentro, frena para no dar vueltas a su alrededor
  const float d = std::hypot(dx, dz), radio = std::max(j.vel, 1.0f) / (96.0f / 65536.0f * 6.2831853f);
  v = std::fabs(dif) > 0.3f && d < radio * 2.0f ? 0.3f : std::fabs(dif) > 1.2f ? 0.6f : 1.0f;
  r1 = j.medidor < 0.6f && std::fabs(dif) < 0.15f && d > 1500.0f;
  if (j.estado == 0 || j.vel < 8.0f) v = 1.0f;   // T4B: lento (tras un choque con el original) acelera a fondo: si no, gira en redondo
}

}  // namespace

// DS132: lo llama el gancho de sub_820F4300 (rellena la matriz de vista del juego, generated/dbz3_recomp.6.cpp; su
// llamador sub_820C1418 la usa para dibujar en el mismo cuadro). Se ANULA la mezcla 0,8/0,2 de B3 igual que hace
// cinematica.cpp A3b con el gancho de sub_820F47E0, pero sin tocar +0x190/+0x1A0: se reescribe SOLO la vista (mgr+0x60)
// con la camara pedida del vuelo. Asi lo que se dibuja (y lo que proyecta Escena para la flecha) es exactamente lo
// pedido, en todos los cuadros, sin la camara nativa que alternaba (DS30). Fuera del vuelo no hace nada.
void AplicarCamaraVueloTrasB3(uint8_t* base) {
  // DS164: la camara libre manda sobre la del vuelo. Escribio la struct de camara antes de sub_820F4300, asi
  // que la vista que acaba de construir el juego (mgr+0x60) YA es la libre; pisarla ahora con la del vuelo
  // (lo que hace el resto de esta funcion) devolveria la camara del minijuego y seria como no tenerla.
  if (dbz3::freecam::Activa()) return;
  if (!g_corre.load()) return;   // fuera del vuelo, la vista del juego no se toca (evita quedarse con la ultima)
  {
    std::lock_guard<std::mutex> lk(g_cv_mu);
    if (!g_cv_on) return;
  }
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr || !base) return;
  // DS137: ANTES de pisar mgr+0x60, guardar el cuadro que B3 acaba de construir para el dibujo (mgr+0x60 = copia de
  // la matriz del render que hizo sub_8208A168; +0x190/+0x1A0 = su ojo/objetivo) y el luchador de este mismo cuadro
  // (X+48) con sus 22 puntos de cuerpo (X+1616, la pose que se pinta). La interfaz usara esta foto: leer en OnDraw el
  // estado vivo mezclaba cuadros distintos (~2 u por cuadro = ~100 px) y ademas proyectaba con la camara pedida.
  {
    RenderCuadro r;
    const uint32_t cam_activa=REX_LOAD_U32(mgr+0x264);
    if(cam_activa>=0x82000000u&&cam_activa<0x83000000u) r.fov=LoadF(base,cam_activa+0x34);
    static bool proyeccion_medida=false;
    if(!proyeccion_medida&&std::getenv("DBZ3_TEST_IWMINI")) {
      proyeccion_medida=true;
      const float y=1/std::tan(r.fov*.5f);
      REXLOG_INFO("dbz3 iw vuelo9: camara real fovy {:.9f} pxx {:.6f} pyy {:.6f}",r.fov,y*.75f,y);
    }
    const uint32_t xg = mgr + kX0;
    for (int c = 0; c < 3; ++c) {
      r.ojo[c] = LoadF(base, mgr + 0x190 + 4 * uint32_t(c));
      r.obj[c] = LoadF(base, mgr + 0x1A0 + 4 * uint32_t(c));
      r.pies[c] = LoadF(base, xg + 48 + 4 * uint32_t(c));
    }
    for (int i = 0; i < 16; ++i) r.matriz[i] = LoadF(base, mgr + 0x60 + 4 * uint32_t(i));
    const float fv[3] = {r.obj[0] - r.ojo[0], r.obj[1] - r.ojo[1], r.obj[2] - r.ojo[2]};
    const float lv = std::sqrt(fv[0] * fv[0] + fv[1] * fv[1] + fv[2] * fv[2]);
    if (lv > 1.0f && std::isfinite(lv)) {
      const float nf[3] = {fv[0] / lv, fv[1] / lv, fv[2] / lv};
      float d0[3] = {-nf[2], 0.0f, nf[0]};   // f x (0,1,0)
      const float ld = std::sqrt(d0[0] * d0[0] + d0[2] * d0[2]);
      if (ld > 1e-4f) {
        d0[0] /= ld, d0[2] /= ld;
        const float u0[3] = {d0[1] * nf[2] - d0[2] * nf[1], d0[2] * nf[0] - d0[0] * nf[2],
                             d0[0] * nf[1] - d0[1] * nf[0]};
        // balanceo: el arriba de la matriz frente al arriba sin giro (u = u0 cos + d0 sin)
        const float um[3] = {r.matriz[1], r.matriz[5], r.matriz[9]};
        const float cu = um[0] * u0[0] + um[1] * u0[1] + um[2] * u0[2];
        const float su = um[0] * d0[0] + um[1] * d0[1] + um[2] * d0[2];
        r.roll = (cu * cu + su * su > 0.5f) ? std::atan2(su, cu) : 0.0f;
      }
      r.ok = true;
    }
    const uint32_t rt = REX_LOAD_U32(xg + 2996);
    if (rt >= 0x40000000u && rt < 0xA0000000u) {   // tabla de los 22 puntos de cuerpo (gigantes.cpp)
      for (uint32_t i = 0; i < 22; ++i) {
        const uint32_t a = xg + 1616 + 16 * i;
        const float px = LoadF(base, a), py = LoadF(base, a + 4), pz = LoadF(base, a + 8);
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) continue;
        if (std::fabs(px) + std::fabs(py) + std::fabs(pz) < 1e-3f) continue;
        if (i == 3) {   // DS129: 3 = cabeza (los puntos que el juego saca de los huesos cada cuadro)
          r.cabeza[0] = px, r.cabeza[1] = py, r.cabeza[2] = pz;
          r.hay_cabeza = true;
        }
        const float radio = LoadF(base, rt + 24 + 4 * i);
        if (!(radio > 0.0f) || radio > 20.0f) continue;
        const float suelo = py - radio;   // suelo de la esfera: las botas que se dibujan
        if (!r.hay_bajo || suelo < r.bajo[1]) {
          r.bajo[0] = px, r.bajo[1] = suelo, r.bajo[2] = pz;
          r.hay_bajo = true;
          r.i_bajo = int(i);
        }
      }
    }
    static uint32_t n_cuadro = 0;
    r.cuadro = ++n_cuadro;
    PublicarCuadroRender(r);
  }
  // La entrada real ya se aplico antes del original. No pisar solamente
  // mgr+60 despues: esa salida no modifica la proyeccion ni el escenario.
}

// DS177: camara del minijuego de campo, publicada por iw_minijuegos.cpp cada cuadro (entrada, carrera y final).
static int g_mc_llamadas = 0;
static float g_mc_dif = 0;
void CamaraMinijuego(bool activa, const float ojo[3], const float obj[3], float fov, float roll) {
  std::lock_guard<std::mutex> lk(g_mc_mu);
  if (!activa || !ojo || !obj) {
    g_mc_on = false;
    return;
  }
  if (g_mc_on) for (int i = 0; i < 3; ++i) g_mc_dif += std::fabs(g_mc_ojo[i] - ojo[i]);   // prueba: escrituras que compiten
  ++g_mc_llamadas;
  for (int i = 0; i < 3; ++i) g_mc_ojo[i] = ojo[i], g_mc_obj[i] = obj[i];
  g_mc_fov = fov;
  // GP2: seis vistas efectivas por650/1050 coinciden con RAM PS2;
  // error de base<3e-7 y traslacion<3.5e-5. No perder el roll del AMC.
  constexpr bool kRollOriginalVerificado = true;
  g_mc_roll = (kRollOriginalVerificado ||
      (std::getenv("DBZ3_TEST_IWMINI") && std::getenv("DBZ3_TEST_GP2_ROLL"))) ? roll : 0.0f;
  g_mc_on = true;
}

// DS177: vista (4x4) y fov vertical (rad) con los que el juego ha DIBUJADO el ultimo cuadro. Es la foto que debe usar
// cualquier escena 3D de la interfaz para caer exactamente sobre lo que pinta B3 (aureola, platanos, monedas, polvo).
bool CamaraDelRenderIw(float m[16], float* fov) {
  if (!g_rv_on.load()) return false;
  std::lock_guard<std::mutex> lk(g_rv_mu);
  for (int i = 0; i < 16; ++i) m[i] = g_rv_m[i];
  if (fov) *fov = g_rv_fov;
  return true;
}

// DS177: se llama tras sub_820F4300 (y tras la camara del vuelo): publica la vista que el dibujo va a usar y el fov
// real del render. El fov NO es mgr+0x1C4: sub_82082A08 lo lee de la struct activa *(mgr+0x264)+0x34 (con respaldo
// *(mgr+0x260) y mgr+0x1C4). Con esto la interfaz y el juego proyectan igual aunque la struct y el global difieran.
void PublicarCamaraRender(uint8_t* base) {
  const uint32_t mgr = base ? REX_LOAD_U32(kMgrPtr) : 0;
  if (!mgr) {
    g_rv_on = false;
    return;
  }
  float fov = 0.0f;
  uint32_t cam = REX_LOAD_U32(mgr + 0x264);
  if (!(cam >= 0x82000000u && cam < 0x83000000u)) cam = REX_LOAD_U32(mgr + 0x260);
  if (cam >= 0x82000000u && cam < 0x83000000u) fov = LoadF(base, cam + 0x34);
  if (!(fov > 0.1f && fov < 3.0f)) fov = LoadF(base, mgr + 0x1C4);
  if (!(fov > 0.1f && fov < 3.0f)) fov = 0.785f;
  float m[16];
  for (int i = 0; i < 16; ++i) m[i] = LoadF(base, mgr + 0x60 + 4u * uint32_t(i));
  if (dbz3::gp2cam::Activo()) dbz3::gp2cam::Dibujo(m, fov);
  {
    std::lock_guard<std::mutex> lk(g_rv_mu);
    for (int i = 0; i < 16; ++i) g_rv_m[i] = m[i];
    g_rv_fov = fov;
  }
  // Prueba (DBZ3_TEST_IWCAMTRAZA=fichero): la vista con que se dibuja, cuadro a cuadro: ojo y frente, lo que publico CamaraMinijuego y cuantas veces.
  if (static const char* traza = std::getenv("DBZ3_TEST_IWCAMTRAZA"); traza) {
    static FILE* f = std::fopen(traza, "w");
    if (f) {
      const float e[3] = {-(m[12] * m[0] + m[13] * m[1] + m[14] * m[2]), -(m[12] * m[4] + m[13] * m[5] + m[14] * m[6]), -(m[12] * m[8] + m[13] * m[9] + m[14] * m[10])};
      const uint32_t x0 = mgr + 2096;
      static const auto t0 = std::chrono::steady_clock::now();
      const auto ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count() / 1000.0;
      std::lock_guard<std::mutex> lk(g_mc_mu);
      std::fprintf(f, "%.1f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.3f,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%X,%.1f\n", ms, e[0], e[1], e[2], m[2], m[6], m[10], fov, int(g_mc_on), g_mc_llamadas,
                   g_mc_dif, g_mc_ojo[0], g_mc_ojo[1], g_mc_ojo[2], g_mc_obj[0], g_mc_obj[1], g_mc_obj[2], REX_LOAD_U32(x0 + 0x650 + 0x520) & 0xFFF, LoadF(base, x0 + 0x650 + 0x608));
      std::fflush(f);
      g_mc_llamadas = 0, g_mc_dif = 0;
    }
  }
  // solGP2: evidencia de un MISMO cuadro del hilo guest, tambien en intro
  // (el reloj de pelea permanece parado ahi). No usar milisegundos host para
  // emparejarla con PS2: se guardan codigo/clip/cuadro de ambos actores.
  if (static const char* gp2 = std::getenv("DBZ3_TEST_GP2_CUADROS");
      gp2 && std::getenv("DBZ3_TEST_IWMINI")) {
    static FILE* f2 = std::fopen(gp2, "w");
    static unsigned long long secuencia = 0;
    if (f2) {
      if (secuencia == 0) {
        std::fprintf(f2, "secuencia,reloj,codigo0,clip0,cuadro0,duracion0,codigo1,clip1,cuadro1,duracion1,fov");
        for (int i=0;i<16;++i) std::fprintf(f2, ",m%d",i);
        std::fprintf(f2, "\n");
      }
      const uint32_t a0=mgr+2096+0x650,a1=a0+8240;
      std::fprintf(f2,"%llu,%.6f,%u,%u,%.6f,%u,%u,%u,%.6f,%u,%.9f",
          ++secuencia,LoadF(base,mgr+35144),
          REX_LOAD_U32(a0+0x520)&0xFFF,REX_LOAD_U32(a0+0x604),LoadF(base,a0+0x608),REX_LOAD_U32(a0+0x60C),
          REX_LOAD_U32(a1+0x520)&0xFFF,REX_LOAD_U32(a1+0x604),LoadF(base,a1+0x608),REX_LOAD_U32(a1+0x60C),fov);
      for(float v:m) std::fprintf(f2,",%.9g",double(v));
      std::fprintf(f2,"\n");std::fflush(f2);
    }
  }
  g_rv_on = true;
}

// DS177: aplica la camara del minijuego a la struct de camara ACTIVA antes de sub_820F4300, igual que la camara libre
// (DS163): es la fuente del dibujo. sub_820F4300 copia 64 B de *(mgr+0x264), y sub_82082970, si +0x38 tiene el bit 0,
// recalcula el cuaternion desde ojo->objetivo (sub_82082868) antes de construir y aplicar la vista. Escribir
// mgr+0x60 despues del original NO cambia el 3D (mgr+0x60 es una salida de sub_8208A168). No se toca +0x190/+0x1A0.
void AplicarCamaraMinijuegoAntes(uint8_t* base) {
  if (!base) return;
  // Prueba A/B: DBZ3_IWMINI_CAM_JUEGO=0 deja el render con la camara de B3 (solo se arregla la proyeccion del panel).
  static const bool forzar = std::getenv("DBZ3_IWMINI_CAM_JUEGO") == nullptr;
  if (!forzar) return;
  // La camara libre tiene prioridad. El vuelo usa el mismo punto de entrada
  // real, pero toma ojo/objetivo/balanceo de su canalCV independiente.
  if (dbz3::freecam::Activa()) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr) return;
  float ojo[3], obj[3], fov = 0.0f,roll=0;
  bool on = false;
  if(g_corre.load()) {
    std::lock_guard<std::mutex> lk(g_cv_mu);
    if(g_cv_on) {
      on=true;fov=g_cv_fov_clip.load()>0.1f ? g_cv_fov_clip.load() : kFovVueloReal;roll=g_cv_roll;
      for(int i=0;i<3;++i) {ojo[i]=g_cv_ojo[i];obj[i]=g_cv_obj[i];}
    }
  } else {
    std::lock_guard<std::mutex> lk(g_mc_mu);
    if (g_mc_on) {
      on = true;
      for (int i = 0; i < 3; ++i) ojo[i] = g_mc_ojo[i], obj[i] = g_mc_obj[i];
      fov = g_mc_fov;roll = g_mc_roll;
    }
  }
  if (on) {
    // Cordura: no pisar la struct con ojo/objetivo degenerados (sub_82082868 dividiria por ~0 al derivar el giro).
    const float dx = obj[0] - ojo[0], dy = obj[1] - ojo[1], dz = obj[2] - ojo[2];
    if (!(dx * dx + dy * dy + dz * dz > 1e-6f) || !std::isfinite(ojo[0]) || !std::isfinite(ojo[1]) ||
        !std::isfinite(ojo[2]) || !std::isfinite(obj[0]) || !std::isfinite(obj[1]) || !std::isfinite(obj[2]))
      on = false;
  }
  if (!on) {
    // Al salir del minijuego se devuelve el fov de la struct (podria no reescribirlo el juego y quedaria el de IW).
    if (g_mc_estaba && g_mc_cam >= 0x82000000u && g_mc_cam < 0x83000000u && g_mc_fov_guardado > 0.1f)
      StoreF(base, g_mc_cam + 0x34, g_mc_fov_guardado);
    g_mc_estaba = false;
    g_mc_cam = 0;
    return;
  }
  uint32_t cam = REX_LOAD_U32(mgr + 0x264);   // la que copia sub_820F4300
  if (!(cam >= 0x82000000u && cam < 0x83000000u)) cam = REX_LOAD_U32(mgr + 0x260);
  if (!(cam >= 0x82000000u && cam < 0x83000000u)) cam = mgr + 0x190;
  if (!g_mc_estaba) {
    g_mc_cam = cam;
    g_mc_fov_guardado = LoadF(base, cam + 0x34);
    if (!(g_mc_fov_guardado > 0.1f && g_mc_fov_guardado < 1.6f)) g_mc_fov_guardado = 0.785f;
    g_mc_estaba = true;
  }
  for (uint32_t i = 0; i < 3; ++i) {
    StoreF(base, cam + 4u * i, ojo[i]);
    StoreF(base, cam + 0x10 + 4u * i, obj[i]);
  }
  StoreF(base, cam + 0x0C, 1.0f);   // w
  StoreF(base, cam + 0x1C, 1.0f);
  StoreF(base, cam + 0x30, roll);
  const float f = (fov > 0.1f && fov < 1.6f) ? fov : LoadF(base, mgr + 0x1C4);
  if (f > 0.1f && f < 1.6f) StoreF(base, cam + 0x34, f);
  REX_STORE_U32(cam + 0x38, REX_LOAD_U32(cam + 0x38) | 1u);   // sub_82082970: deriva la orientacion de ojo->objetivo
}

// DS137: camara con la que B3 DIBUJA el cuadro del vuelo (la que publica el gancho de arriba). Es la matriz que
// sub_820F4300 construye y sub_8208A168 copia a mgr+0x60 justo antes de que la use el dibujo (sub_82087500); el gancho
// la copia ANTES de pisarla con la pedida. Del resto (fov mgr+0x1C4, rectangulo de la imagen del juego, planos) se
// encarga CamaraCombate. Si la matriz no es una vista valida se rehace desde +0x190/+0x1A0; sin cuadro publicado
// (antes del vuelo) devuelve false y se sigue con CamaraCombate, como siempre.
bool CamaraDelRender(uint8_t* base, float ancho, float alto, ps2::Camara& cam) {
  // DS164: con la camara libre activa la vista que se dibuja es mgr+0x60 (la escribe sub_820F4300 con la
  // struct libre); el cuadro de vuelo publicado es de antes y descuadraria flechas/aros. Se devuelve false
  // para que el que llama use ps2::CamaraCombate, que lee esa vista.
  if (dbz3::freecam::Activa()) return false;
  const RenderCuadro r = CuadroRender();
  if (!r.ok) return false;
  cam = ps2::CamaraCombate(base, ImVec2(ancho, alto));
  if(r.fov>0.1f&&r.fov<3) cam.fov=r.fov;
  bool finito = true;
  for (int i = 0; i < 16; ++i) finito = finito && std::isfinite(r.matriz[i]);
  const float d[3] = {r.matriz[0], r.matriz[4], r.matriz[8]};
  const float u[3] = {r.matriz[1], r.matriz[5], r.matriz[9]};
  const float a[3] = {r.matriz[2], r.matriz[6], r.matriz[10]};
  const float ld = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  const float lu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
  const float du = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
  if (finito && std::fabs(ld - 1.0f) < 0.05f && std::fabs(lu - 1.0f) < 0.05f && std::fabs(du) < 0.2f) {
    for (int i = 0; i < 16; ++i) cam.m[i] = r.matriz[i];
    return cam.ok;
  }
  // Respaldo: rehacer la base con el ojo/objetivo publicados (+0x190/+0x1A0) y el balanceo de la matriz.
  float f[3] = {r.obj[0] - r.ojo[0], r.obj[1] - r.ojo[1], r.obj[2] - r.ojo[2]};
  const float lf = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  if (!(lf > 1e-4f) || !std::isfinite(lf)) return false;
  for (float& v : f) v /= lf;
  float d0[3] = {-f[2], 0.0f, f[0]};   // f x (0,1,0)
  const float ld0 = std::sqrt(d0[0] * d0[0] + d0[2] * d0[2]);
  if (!(ld0 > 1e-4f)) return false;
  for (float& v : d0) v /= ld0;
  const float u0[3] = {d0[1] * f[2] - d0[2] * f[1], d0[2] * f[0] - d0[0] * f[2], d0[0] * f[1] - d0[1] * f[0]};
  const float cg = std::cos(r.roll), sg = std::sin(r.roll);
  const float dd[3] = {d0[0] * cg - u0[0] * sg, d0[1] * cg - u0[1] * sg, d0[2] * cg - u0[2] * sg};
  const float uu[3] = {u0[0] * cg + d0[0] * sg, u0[1] * cg + d0[1] * sg, u0[2] * cg + d0[2] * sg};
  const float aa[3] = {-f[0], -f[1], -f[2]};
  const float m[16] = {dd[0],
                       uu[0],
                       aa[0],
                       0,
                       dd[1],
                       uu[1],
                       aa[1],
                       0,
                       dd[2],
                       uu[2],
                       aa[2],
                       0,
                       -(dd[0] * r.ojo[0] + dd[1] * r.ojo[1] + dd[2] * r.ojo[2]),
                       -(uu[0] * r.ojo[0] + uu[1] * r.ojo[1] + uu[2] * r.ojo[2]),
                       -(aa[0] * r.ojo[0] + aa[1] * r.ojo[1] + aa[2] * r.ojo[2]),
                       1};
  for (int i = 0; i < 16; ++i) cam.m[i] = m[i];
  return cam.ok;
}

// Hilo del juego (Mando de iw_minijuegos.cpp, gancho de combate.cpp sub_8208FD58): el vuelo se queda el mando y el
// luchador de B3 no lo ve (ni anda ni ataca: solo START y SELECT), como P4 con la palanca derecha y la cruceta. Antes de
// cada lectura se le devuelve al juego el registro como lo dejo (mantiene las direcciones de un cuadro a otro).
// EU 0x33E8B0 Viewer_ReadControlAxes
void MandoVuelo(uint8_t* base, uint32_t usuario, bool leido) {
  if (usuario != 0) return;
  const uint32_t r = kPads;
  if (!leido) {
    if (g_dejado.hay) {
      REX_STORE_U16(r + 12, g_dejado.b12);
      REX_STORE_U16(r + 14, g_dejado.b14);
      REX_STORE_U16(r + 108, g_dejado.d108);
      REX_STORE_U16(r + 110, g_dejado.d110);
      REX_STORE_U8(r + 26, g_dejado.p26);
      REX_STORE_U8(r + 27, g_dejado.p27);
      REX_STORE_U32(r + 16, g_dejado.b16);
      for (uint32_t k = 0; k < 16; ++k) REX_STORE_U32(r + 0x28 + 4 * k, g_dejado.cuenta[k]);
      g_dejado.hay = false;
    }
    return;
  }
  if (!g_corre.load()) return;
  g_pad = {true,          REX_LOAD_U16(r + 12), REX_LOAD_U16(r + 14), REX_LOAD_U16(r + 108), REX_LOAD_U16(r + 110),
           REX_LOAD_U8(r + 26), REX_LOAD_U8(r + 27)};
  g_pad.b16 = REX_LOAD_U32(r + 16);
  if(std::getenv("DBZ3_SOLIW9_VUELO_PASOS")) {
    static bool mando_medido=false;
    const uint16_t dirs=uint16_t(g_pad.d108|(g_pad.d108>>8));
    if(!mando_medido&&(g_pad.b12&kR1)&&((dirs&kArriba)||int8_t(g_pad.p27)<-64)) {
      mando_medido=true;
      REXLOG_INFO("dbz3 iw vuelo9: mando arriba+R1 confirmado antes de avanzar, b12 {:04x} d108 {:04x} p27 {}",
                  g_pad.b12,g_pad.d108,int(int8_t(g_pad.p27)));
    }
  }
  for (uint32_t k = 0; k < 16; ++k) g_pad.cuenta[k] = REX_LOAD_U32(r + 0x28 + 4 * k);
  g_dejado = g_pad;
  if (static const bool sonda = std::getenv("DBZ3_R12_SONDA") != nullptr; sonda && (g_pad.b12 & 0xFFF0)) {
    std::string w;
    for (uint32_t o = 0; o < 184; o += 4) w += fmt::format(" {:X}:{:08X}", o, REX_LOAD_U32(r + o));
    REXLOG_INFO("dbz3 r12: sonda mando{}", w);
  }
  constexpr uint16_t kDeja = 0x0009;   // SELECT 0x01 y START 0x08 (la pausa); el resto son golpes, ki, guardia...
  REX_STORE_U16(r + 12, uint16_t(g_pad.b12 & kDeja));
  REX_STORE_U16(r + 14, uint16_t(g_pad.b14 & kDeja));
  REX_STORE_U16(r + 108, 0);
  REX_STORE_U16(r + 110, 0);
  REX_STORE_U8(r + 26, 0);
  REX_STORE_U8(r + 27, 0);
  REX_STORE_U32(r + 16, g_pad.b16 & (uint32_t(kDeja) << 16 | kDeja));
  for (uint32_t k = 0; k < 16; ++k)   // los botones quitados, "sueltos" como hace P4 con el circulo (+92 = -1)
    if (!(kDeja >> k & 1) && int32_t(g_pad.cuenta[k]) > 0) REX_STORE_U32(r + 0x28 + 4 * k, uint32_t(-1));
}

// Intro del vuelo. DS113: escena 0 de Cutscene_GotoLine (EU 0x3424F0), con las lineas del guion de actores
// (0x483120/0x483140/0x4831a0/0x4831c0) y los clips del #AMC de 891 (libro 1 = hijo 26, libro 0 = hijo 25).
//  - Linea 0: libro 1 clip 0, 300 cuadros: plano de presentacion del campo, sin actores.
//  - Linea 1: libro 0 clip 900, 160 cuadros: actor 2 (Vegeta, G+0x7a4) con animacion 880 (0x370).
//  - Linea 2: libro 0 clip 901, 620 cuadros: actor 1 (Krilin, G+0x7a0) con animacion 885 (0x375).
//  - v1 (0x7E0): 0, 1400 (180, actor 2 = Goten, anim 1880), 1401/1404 (300, actor 1 = Gohan/Trunks, 1885) y 1407
//    (180, actor 2, 1886). DS118: la animacion ya no se elige por variante: es el `anim` del guion (L->anim), el
//    mismo dato que pasa Cutscene_GotoLine a CharModel_SetAnimBlend.
// Cutscene_Update (EU 0x342170) avanza +0xC con el cuadro y, al llegar a la duracion del clip (+0x10), pasa a la
// linea siguiente. Los actores se colocan en el objetivo de su clip: el modelo de IW trae la posicion en la propia
// animacion y la camara del clip encuadra ese punto (DS92: el objetivo es el centro del cuerpo; los pies van
// kCinturaVuelo por debajo). Las animaciones exactas 880/885 (0x370/0x375) las carga anim_iw.cpp desde DS116
// (DATA_CMN 931 y 924, almacen propio); antes se usaba la pose de vuelo mas parecida (0x320 / 0x322).
bool AvanzarIntro(uint8_t* base, uint32_t mgr, Juego& j) {
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  // fin de la escena 0: el jugador (x0) vuelve a verse y el rival (x1) queda oculto, como en el vuelo.
  // DS119: EU 0x33CB80 Viewer_StateCutsceneInput, rama de fin de escena: Cutscene_GotoLine(-1,-1) (la cutscene
  // deja de colocar y animar actores), G+0x54C = 1 (mascara de dibujo: SOLO el modelo del jugador G+0x548; los
  // actores del guion Krilin/Vegeta de G+0x7a0/+0x7a4 no se pintan), Char_AdvanceAnimAndMouth(G+0x548) y
  // +0x52c = 90 cuadros (READY?) con la camara del vuelo. Aqui x0 ES el actor del guion que encarnaba al jugador
  // y x1 el rival: se oculta x1, se para su animacion de la intro y se devuelve true para que este MISMO cuadro
  // ya pinte con la camara/posicion del vuelo (antes se veia un cuadro de la ultima toma con la mascara vieja).
  auto fin_intro = [&]() {
    j.intro_done = true;
    REX_STORE_U8(x0 + kOcultoA, 0);
    REX_STORE_U8(x0 + kOcultoB, 0);
    REX_STORE_U8(x1 + kOcultoA, 1);
    REX_STORE_U8(x1 + kOcultoB, 1);
    Reiniciar(j,false); // salida nueva, pero los efectos cargados NO vuelven a nacer
    // EU 0x33CB80: al saltar al vuelo, CharacterDialoguesStart_(2, 1248|0x30000, 0) en el canal de voz y
    // +0x52c = 0x42B40000 (90 cuadros) antes de pasar a los scripts del vuelo (Viewer_StateEndingWait
    // 0x33CA90 -> FlyingMinigameSpritesScripts 0x33C660). Ese es el READY? de IW: el vuelo no avanza.
    VozCorta(j, 1248);
    j.listo_t = 90;
    j.aviso_id=0;j.aviso_t=0;
    REXLOG_INFO("dbz3 r12: intro terminada ({} cuadros); READY? {} cuadros y empieza el vuelo", j.intro_t, j.listo_t);
  };
  // Cutscene_Update EU 0x342170: avanza el cuadro +0xC; cuando llega a la duracion del clip (+0x10) pasa a la linea
  // siguiente (Cutscene_GotoLine con la linea +1) y, agotada la ultima, la escena termina. La linea nueva se pinta en
  // este mismo cuadro (GotoLine deja la camara en su cuadro 0).
  const dbz3::iwcut::Paso paso = j.cs.Tick();
  if (paso.fin) {
    j.guion_linea = int(j.guion.size());
    fin_intro();
    return true;
  }
  j.guion_linea = paso.linea;
  j.guion_f = int(paso.t);
  const Juego::LineaGuion* L = &j.guion[size_t(paso.linea)];
  const int dur = paso.l->cuadros;
  float ojo[3], obj[3];
  const auto cl_it = j.cams.find((L->libro << 16) | L->clip);
  const float cl_fov = cl_it == j.cams.end() ? 0.0f : cl_it->second.fov;
  const bool hay = CamaraClip(j, L->libro, L->clip, float(j.guion_f), ojo, obj);
  if (!hay) {   // clip sin pistas (raro): se salta la linea
    j.cs.lineas[size_t(paso.linea)].cuadros = 0;
    return false;
  }
  // Musica de la intro (EU 0x33CE40): ADX_CMN 65 (v0) / 63 (v1) por el canal 0. Se lanza en el cuadro 10 de la
  // linea 0 para que quede despues de la peticion 66 que hace el mapa durante su fundido de salida (pasarela_misiones:
  // MusicaIW(66) mientras su panel se apaga, ~0,12 s) y este no la vuelva a pedir al no cambiar su pista.
  if (j.guion_linea == 0 && j.guion_f == 10 && !j.mus_intro) {
    j.mus_intro = true;
    MusicaVuelo(j);
  }
  // 1) al entrar en una linea: el audio que arranca en ese momento (EU 0x33CE40 y 0x33CB80).
  if (j.guion_f == 0) {
    if (j.guion_linea == 0) {
      VozVuelo(j, 0);   // Audio_PlayDialogueOnChannel(3, 891, 0x49C460[v*3]): hijo 29 (v0) / 32 (v1)
    } else if (j.guion_linea == (j.v == 0 ? 2 : 1)) {
      // Viewer_StateCutsceneInput 0x33CB80: con SemaParam == (v==0) suena 0x49C461 (hijo 30/33) cuando la linea es
      // 2 en v0 y 1 en v1. Es el tramo de dialogo de Krilin/Dende.
      VozVuelo(j, 1);
    }
  }
  ++j.guion_f;
  ++j.intro_t;
  j.intro_ojo[0] = ojo[0], j.intro_ojo[1] = ojo[1], j.intro_ojo[2] = ojo[2];
  // DS92: la proyeccion del juego es la de B3 (45 grados); se acerca el ojo para conservar el 40 del clip de IW.
  // 31/10: como disparo: cine::Camara solo no manda en el dibujo y los actores del guion quedaban fuera de plano.
  if (cl_fov > 0.1f) {
    g_cv_fov_clip = cl_fov;
    PublicarCamaraVuelo(ojo, obj, 0.0f);
  } else {
    AcercarPorFov(base, ojo, obj);
  }
  dbz3::cine::Camara(base, ojo, obj, 0.0f);
  // DS128/DS132: la intro y el cierre no se tocan (se ven bien con lo que hay). La mezcla 0,8/0,2 de B3 se anula solo
  // en la camara del vuelo (InvertirMezcla): al empezar el vuelo, el 2.o cuadro ya es la pedida.
  if (std::getenv("DBZ3_TEST_IWMINI") && j.guion_f % 60 == 2) {
    float r0[3] = {}, r1[3] = {};
    dbz3::anim::RaizActual(base, x0, r0);
    dbz3::anim::RaizActual(base, x1, r1);
    {
      dbz3::anim::EstadoCuerpo ea{}, eb{};
      dbz3::anim::LeerEstadoCuerpo(base, x0, 0, ea);
      dbz3::anim::LeerEstadoCuerpo(base, x1, 0, eb);
      REXLOG_INFO("dbz3 r12: cuerpos A hay {} cod {:X} f {:.0f} ocu {}/{} id {} pos ({:.0f},{:.0f},{:.0f}) | B hay {} cod {:X} f {:.0f} ocu {}/{} id {} pos ({:.0f},{:.0f},{:.0f})",
                  ea.hay, ea.codigo, ea.cuadro, ea.oculto1, ea.oculto2, REX_LOAD_U32(x0 + 7420), LoadF(base, x0 + 48), LoadF(base, x0 + 52), LoadF(base, x0 + 56),
                  eb.hay, eb.codigo, eb.cuadro, eb.oculto1, eb.oculto2, REX_LOAD_U32(x1 + 7420), LoadF(base, x1 + 48), LoadF(base, x1 + 52), LoadF(base, x1 + 56));
    }
    REXLOG_INFO("dbz3 r12: guion linea {} f {} ojo ({:.1f},{:.1f},{:.1f}) obj ({:.1f},{:.1f},{:.1f}) raiz A ({:.1f},{:.1f},{:.1f}) B ({:.1f},{:.1f},{:.1f}) fov {:.3f}", j.guion_linea, j.guion_f,
                ojo[0], ojo[1], ojo[2], obj[0], obj[1], obj[2], r0[0], r0[1], r0[2], r1[0], r1[1], r1[2], cl_fov);
  }
  // El actor del plano: 2 = Vegeta (x1), 1 = Krilin del guion (x0). El resto de la escena queda oculto (el original
  // dibuja solo los actores del mask de la linea; World_CharacterLoader 0x33B580).
  const bool ver_p1 = L->actor == 1, ver_p2 = L->actor == 2;
  ColocarActorGuion(base,x0);
  ColocarActorGuion(base,x1);
  REX_STORE_U8(x0 + kOcultoA, uint8_t(ver_p1 ? 0 : 1));
  REX_STORE_U8(x0 + kOcultoB, uint8_t(ver_p1 ? 0 : 1));
  REX_STORE_U8(x1 + kOcultoA, uint8_t(ver_p2 ? 0 : 1));
  REX_STORE_U8(x1 + kOcultoB, uint8_t(ver_p2 ? 0 : 1));
  if (j.guion_f == 1) {   // pose al entrar en la linea (la animacion de IW de esa linea)
    // EU 0x3424F0: CharModel_SetAnimBlend(1.0, 0, actor, anim, 0) + Char_PushAnimIds. El codigo de animacion ES el
    // `anim` del guion (0x4831xx): v0 880 (0x370, Vegeta, DATA_CMN 931) y 885 (0x375, Krilin, 924); v1 1880 (0x758,
    // Goten, 923), 1885 (0x75D, Gohan 919 / Trunks nino 927) y 1886 (0x75E, Goten, 923), con el almacen propio
    // (hijo 1, alm 3) de cada #BSK. 0x370/0x375 estan en kExtras desde DS116; los tres de v1 hay que anadirlos alli.
    // DS122: el 0x75D de Trunks nino (927, 52 huesos) va con alias 0x9E0 para no pisar el de Gohan (919)
    const uint32_t porte = (j.v == 0 && j.pj == 1 && L->actor == 1 && L->anim == 0x375) ? 0x9E1u
                           : (j.v == 1 && j.pj == 1 && L->anim == 0x75D) ? 0x9E0u
                           : L->anim ? L->anim : uint32_t(L->actor == 2 ? 0x370 : 0x375);
    // 31/10: como el cierre: la pista absoluta (AMM y AMC en par); en Bucle el actor quedaba fuera de plano
    if (ver_p2) dbz3::anim::Reproducir(x1, porte, true, uint32_t(std::max(dur - 1, 1)));
    if (ver_p1) dbz3::anim::Reproducir(x0, porte, true, uint32_t(std::max(dur - 1, 1)));
    REXLOG_INFO("dbz3 r12: intro linea {} (libro {} clip {}, {} cuadros): actor {} anim {} de IW -> 0x{:X}",
                j.guion_linea, L->libro, L->clip, dur, L->actor, L->anim, porte);
  }
  // 31/10: el actor del guion cruza el mundo a 45 u por cuadro con el clip; la pista va sin esa recta (anim_iw) y el
  // actor se pone en el objetivo del clip cada cuadro (PINE/log: cadera = objetivo +-6): un cuadro de desfase lo sacaba de plano.
  if (ver_p1 || ver_p2) Poner(base, x0, {obj[0], obj[1], obj[2]}), Poner(base, x1, {obj[0], obj[1], obj[2]});   // B3 acota la distancia entre los dos
  StoreF(base, x0 + kVida, LoadF(base, x0 + kVidaMax));
  StoreF(base, x1 + kVida, LoadF(base, x1 + kVidaMax));
  return false;
}

// Parte 1 del guion (escena 1: clip 210/211 en v0, 540/541 en v1; 200 cuadros): el aterrizaje con pose de victoria
// antes de la escena 2/3. Solo despues Viewer_StateResultSuccess/Fail escribe el resultado.
void PasoCierre(uint8_t* base, uint32_t mgr, Juego& j) {
  const bool meta = j.escena_cierre == 1;
  const int clip = meta ? j.clip_meta
      : (j.v == 0 ? (j.pj == 0 ? 902 : 905) : (j.pj == 0 ? 1402 : 1405)) + (j.escena_cierre == 2 ? 0 : 1);
  const auto cl = j.cams.find(clip);
  const int dur = cl == j.cams.end() ? 0 : cl->second.dur;
  float ojo[3], obj[3];
  const bool hay = CamaraClip(j, 0, clip, float(j.cierre_t), ojo, obj);
  ++j.cierre_t;
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  if (j.cierre_t == 1) {
    // EU 0x3424F0: actor 1, no el visor 915/0x32F. Codigos propios por
    // variante y alias por personaje para no mezclar 924/921 ni 919/927.
    const uint32_t codigo = j.v == 0
        ? (j.pj == 0 ? (meta ? 0x366u : j.escena_cierre == 2 ? 0x371u : 0x372u)
                     : (meta ? 0x9E2u : j.escena_cierre == 2 ? 0x9E3u : 0x9E4u))
        : (j.pj == 0 ? (meta ? 0x74Eu : j.escena_cierre == 2 ? 0x759u : 0x75Au)
                     : (meta ? 0x9E5u : j.escena_cierre == 2 ? 0x9E6u : 0x9E7u));
    const int banco = j.v == 0 ? (j.pj == 0 ? 924 : 921) : (j.pj == 0 ? 919 : 927);
    dbz3::anim::Reproducir(x0, codigo, true, uint32_t(std::max(dur - 1, 1)));
    REXLOG_INFO("dbz3 r12: actor final de IW (escena {}, #BSK {}, anim 0x{:X}, clip {}, {} cuadros)",
                j.escena_cierre, banco, codigo, clip, dur);
    // EU 0x33B2C0 Viewer_BeginFinalPhase: Audio_PlayDialogueOnChannel(3, 891, 0x49C462[v*3]) = hijo 31. Es el audio
    // de la meta (no de la intro; la misma pista para v0 y v1: 0x49C462 = 0x1F y 0x49C465 = 0x1F). Ademas, por el
    // canal de voz: 1253 si se llego dentro del limite, 1250 si no (bytes 0x49C46A = 5 y SemaParam.count = 2 en la
    // tabla EU 0x480450 = 1248 + indice).
    if (meta) {
      VozVuelo(j, 2);
      VozCorta(j, j.t_ms <= float(j.rango_s[4]) * 1000.0f ? 1253 : 1250);
    }
  }
  if (hay) {
    if (cl != j.cams.end() && cl->second.fov > 0.1f) {
      g_cv_fov_clip = cl->second.fov;
      PublicarCamaraVuelo(ojo, obj, 0.0f);
    } else {
      AcercarPorFov(base, ojo, obj);
    }
    dbz3::cine::Camara(base, ojo, obj, 0.0f);
    ColocarActorGuion(base,x0);
    ColocarActorGuion(base,x1);
    if (std::getenv("DBZ3_TEST_IWMINI") && j.cierre_t % 60 == 1)
      REXLOG_INFO("dbz3 r12: cierre t {} ojo ({:.1f},{:.1f},{:.1f}) obj ({:.1f},{:.1f},{:.1f}) cintura ({:.1f},{:.1f},{:.1f})", j.cierre_t, ojo[0], ojo[1], ojo[2],
                  obj[0], obj[1], obj[2], LoadF(base, x0 + 1616), LoadF(base, x0 + 1620), LoadF(base, x0 + 1624));
    REX_STORE_U8(x0 + kOcultoA, 0);
    REX_STORE_U8(x0 + kOcultoB, 0);
    REX_STORE_U8(x1 + kOcultoA, 1);
    REX_STORE_U8(x1 + kOcultoB, 1);
    StoreF(base, x0 + kVida, LoadF(base, x0 + kVidaMax));
  }
  if ((!hay || j.cierre_t >= dur) && FinAviso(j)) {
    if (!meta && hay && !j.recompensa.cerrado) {
      j.en_panel = true;
      j.cierre_t = dur;
      const uint16_t b = REX_LOAD_U16(kPads + 12);
      const bool prueba = j.auto_ || std::getenv("DBZ3_TEST_IWMINI");
      j.recompensa.Avanzar(1.0f, (b & ~j.panel_antes & 0x4000) != 0 || (prueba && j.recompensa.cuadro >= 420));
      j.panel_antes = b;
      if (!j.recompensa.cerrado) return;
    }
    j.en_panel = false;
    j.cierre = false;
    Acabar(base, mgr, j, meta || j.superado, meta ? "META" : "ESCENA DE RESULTADO");
  }
}

// EU 0x33EA50 Viewer_UpdateEventTimers
// T4B: un cuadro del vuelo con la logica original del ELF (iw_mips_vuelo.inl). false = sigue el port.
bool VueloOriginal(uint8_t* base, uint32_t x0, Juego& j, int px, int py, bool r1) {
  namespace v7 = dbz3::iwmips::vuelo7;
  if (j.orig == 2 || !v7::g_vuelo || !v7::g_activa_vuelo || !v7::g_activa_vuelo()) return false;
  v7::Entrada in;
  in.inicio = j.orig == 0;
  in.msf = j.msf_raw.data(); in.msf_n = j.msf_raw.size();
  in.v = j.v; in.dif = j.dif; in.pj = j.pj;
  in.botones = r1 ? 0x800 : 0;
  in.lx = int8_t(std::clamp(px, -128, 127));
  in.ly = int8_t(py >= 128 ? -128 : std::clamp(-py, -128, 127));
  in.rx = int8_t(std::clamp(dbz3::iwmini::PalancaDerechaX() / 256, -128, 127));
  dbz3::anim::EstadoCuerpo e;
  if (dbz3::anim::LeerEstadoCuerpo(base, x0, 0, e) && e.hay) {
    in.anim_cod = e.codigo; in.anim_cuadro = e.cuadro; in.anim_cuadros = int(e.cuadros);
  }
  in.congelar = j.congelar_pend;
  j.congelar_pend = false;
  in.t = uint64_t(j.cuadros);
  v7::Salida o;
  if (!v7::g_vuelo(in, o)) {
    REXLOG_WARN("dbz3 r12: la logica original del vuelo no esta disponible; sigue el port");
    j.orig = 2;
    return false;
  }
  if (j.orig == 0) REXLOG_INFO("dbz3 r12: vuelo con la logica original del ELF (dbz3_iw_vuelo_original)");
  j.orig = 1;
  const float ox = j.x, oz = j.z;
  j.x = o.x; j.z = o.z;
  j.ux += Envolver(o.x - ox, j.periodo); j.uz += Envolver(o.z - oz, j.periodo);
  j.yaw = int(o.yaw);
  j.vel = o.vel;
  j.estado = o.estado >= 10 ? 0 : o.estado;
  j.t568 = o.t568; j.t56c = o.t56c; j.congelado = o.congelado;
  j.orig_o = o;
  if (o.anim == 0x32E && j.anim_prev != 0x32E) j.zoom_meta = -10.0f;
  j.anim_prev = o.anim;
  j.pose_aro = o.anim == 0x330;
  AplicarPoseVuelo(x0, j, o.anim);
  return true;
}

void PasoVuelo(uint8_t* base) {
  if (!g_corre.load()) return;
  std::shared_ptr<Juego> jp;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    jp = g_j;
  }
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!jp || !mgr) return;
  if (jp->colocado && jp->intro_done && jp->listo_t<=0 && std::getenv("DBZ3_SOLIW9_VUELO_PASOS")) {
    int objetivo=0;
    std::ifstream f("iw9_vuelo_cuadro.req");f>>objetivo;
    if(int(std::lround(jp->t_ms*.06f))>=objetivo) return; // solo escena de medida; camara publicada conservada
  }
  // DS132: por defecto el gancho de dibujo no pisa la vista; solo la camara del vuelo la publica (mas abajo).
  ApagarCamaraVuelo();
  // DS92: el juego dibuja con el FOV que trae B3 (0,785); no se escribe mgr+0x1C4. Se compensa la distancia.
  const float fov_juego = FovJuegoVuelo(base);
  const float k_fov = 1.0f; // proyeccion activa real; sin compensacion de distancia
  Juego& j = *jp;
  const uint32_t estado = REX_LOAD_U32(mgr + kEstado);
  if (estado != 3 && estado != 4) return;
  const uint32_t x0 = mgr + kX0, x1 = x0 + kXPaso;
  std::lock_guard<std::mutex> lk(g_mu);
  if (j.colocado && (j.intro && !j.intro_done || j.listo_t>0) &&
      !dbz3::GameDimmed() && !dbz3::OverlayOpen()) Fx::Cuadro(j);
  // DS117: niebla del #MCF. El alta del campo (registro anfitrion 16, R&R BASE) no trae el bit 0x20 de G+76 y la
  // niebla no se pintaba; se enciende mientras dura el vuelo y se reponen los +4/+8 del #MCF (883: 0/255), que
  // escenarios.cpp no escribe (DS88). Al acabar se devuelve el valor de G+76 (el alta siguiente pone el suyo).
  if (j.vuelo_real) {
    if (!g_fog_flags_guardado) {
      g_fog_flags_orig = REX_LOAD_U32(kFogFlags);
      g_fog_flags_guardado = true;
    }
    REX_STORE_U32(kFogFlags, g_fog_flags_orig | kFogFlag);
    // +4/+8 NO se reponen: con los crudos del #MCF (0/255) la niebla de B3 sale turquesa en vez de amarilla
    // (prueba 27/10 02:04; mismo fallo que DS88 quito en campos_iw.py).
  }
  if (!j.colocado) {
    Reiniciar(j);
    j.colocado = true;
    // DS83: la intro corre con este mismo Juego venga del mapa (mapa -> ficha -> minijuego) o del salto de prueba
    // DBZ3_TEST_IWMINI; los dos llaman a iwmini::Empezar -> modulo 0x7D6 Empezar (RegistrarModulo) y este PasoVuelo.
    // El arnes de pruebas empieza a capturar cuando ve "r12: empieza el vuelo" (despues de los 1080 cuadros de la
    // escena 0 en v0), asi que
    // sus fotos no la ensenan; no se salta en ninguno de los dos caminos.
    // DS113: la intro es la escena 0 del guion (Cutscene_GotoLine 0x3424F0) con los clips de los libros 0/1 de
    // 891. Antes de la v1.3 este bloque saltaba la linea 0 del libro 1 (300 cuadros) y usaba solo 900/901.
    j.intro = j.vuelo_real && !j.guion.empty();
    j.intro_done = !j.intro;
    j.intro_t = 0;
    j.guion_linea = j.guion_f = 0;
    j.cs.Reinicia();
    j.poses = false;
    j.mus_intro = false;
    if (j.intro) {
      REXLOG_INFO("dbz3 r12: intro del vuelo: {} lineas, {} cuadros, primer clip libro {} clip {}",
                  j.guion.size(), j.intro_total, j.guion.front().libro, j.guion.front().clip);
      if (!AvanzarIntro(base, mgr, j)) return;
      // DS119: la escena 0 acaba en este cuadro: el original ya ha hecho Cutscene_GotoLine(-1,-1) y la mascara
      // G+0x54C = 1. Se sigue abajo para pintar YA con la camara/posicion del vuelo (READY?), sin un cuadro de
      // la ultima toma de la intro con los actores del guion.
    }
  }
  if (!j.intro_done) {
    if (!AvanzarIntro(base, mgr, j)) return;
  }
  if (j.aviso_id>=0) ++j.aviso_t;
  CuadroDialogo(j);
  if (!j.poses) {
    j.poses = true;
    // vuelo: el scatto en el aire de IW (0x1D, R10). [VERIFICAR] el 0x320 del #BSK de 915 (auditoria7/F, visor)
    // resulto ser una pose de pie (anim 73 del almacen comun, 36 cuadros; captura 20261016-133040): sin la de vuelo
    // vuelo de crucero de IW: 0x32C del visor de vuelo (DATA_CMN 915, su #AMM propio; cuerpo horizontal, capturas
    // 20261016-145048). DBZ3_IW_VUELO=<hex> otra del visor; =0 el scatto aereo de antes
    static const uint32_t pose = [] {
      const char* v = std::getenv("DBZ3_IW_VUELO");
      return v ? uint32_t(std::strtoul(v, nullptr, 16)) : 0x322u;
    }();
    if (pose) {
      dbz3::anim::Bucle(x0, 0x320);   // el jugador, en reposo hasta que se mueva (PoseVuelo)
      dbz3::anim::Bucle(x1, pose);
    } else {
      dbz3::anim::Scatto(x0, dbz3::anim::kBucle, true);
      dbz3::anim::Scatto(x1, dbz3::anim::kBucle, true);
    }
    REXLOG_INFO("dbz3 r12: poses del vuelo puestas en ({:.0f}, {:.0f}) giro {} (READY? {} cuadros)", j.x, j.z, j.yaw,
                j.listo_t);
    // DS92: respuesta en el log a "que camara/FOV usa el port": el juego pinta con el FOV de B3 (mgr+0x1C4 ya
    // consumido antes de este gancho, medido en s8); el 40 de IW se reproduce acercando la camara (distancia x k).
    REXLOG_INFO("dbz3 r12 cam: FOV del juego {:.5f} rad = {:.2f} grados [{}]; IW 40 = {:.5f} rad; mgr+0x1C4 NO se "
                "escribe (solo lo leen los overlays); compensacion de distancia x {:.4f}",
                fov_juego, fov_juego * 57.2957795f, NombreFov(fov_juego), kFovVueloIW, k_fov);
  }
  // EU 0x33CB80 Viewer_StateCutsceneInput
  // DS118: READY? de IW. Al acabar la intro el original deja el vuelo parado 90 cuadros (+0x52c = 0x42B40000) con
  // la camara del vuelo y el jugador en la salida (pose de reposo 0x320), y solo despues arrancan el reloj, el
  // medidor y los scripts (Viewer_StateEndingWait 0x33CA90 -> FlyingMinigameSpritesScripts 0x33C660).
  // DS124/DS128/DS132: el READY? espera (tope kEsperaCamara) a que la camara que se DIBUJA (mgr+0x60, la que usan el
  // 3D y los overlays) coincida en cabeceo y rumbo con la pedida. DS124 lo alargo a 90 cuadros y no asentaba (el juego
  // no converge: se queda en el punto fijo de su mezcla 0,8/0,2; prueba 27/10 04:24: "al tope (90 cuadros) ... cabeceo
  // dibujado 10,37 / pedido 11,25"). DS128 escribio mgr+0x150/+0x160 creyendo que era la otra fuente y tampoco cambio
  // nada (es un puntero; prueba 27/10 05:04: "al tope (4 cuadros)"). DS132 invierte la mezcla (InvertirMezcla), asi
  // que la vista es la pedida desde el 2.o cuadro; la comprobacion sigue como red de seguridad y solo informa.
  if (j.listo_t > 0) {
    // Los90 regulan voz, no la duracion del nodo ni una espera de camara.
    if (FinAviso(j)) {
      j.listo_t=0;
      dbz3::anim::Bucle(x0, 0x320);
      REXLOG_INFO("dbz3 solIW6: READY fin raiz AMA0 cuadro {}",j.aviso_t);
      REXLOG_INFO("dbz3 r12: READY? terminado; empieza el vuelo en ({:.0f}, {:.0f}) giro {} y arranca el reloj", j.x,
                  j.z, j.yaw);
    }
  }
  if (j.derrota_espera) {
    Volar(base,x0,j,0,0,false);
    AplicarPoseVuelo(x0,j,PoseVuelo(j,0,0));
    if (FinAviso(j)) {
      j.derrota_espera=false;j.derrota_vista=true;
      REXLOG_INFO("dbz3 solIW6: FAILED fin raiz AMA8 cuadro {}",j.aviso_t);
      Acabar(base,mgr,j,false,"DERROTA");
    }
  }
  if (j.cierre) {
    PasoCierre(base, mgr, j);
    return;
  }
  int objetivo9=0;
  const bool pasos9=std::getenv("DBZ3_SOLIW9_VUELO_PASOS")!=nullptr;
  if(pasos9) {std::ifstream f("iw9_vuelo_cuadro.req");f>>objetivo9;}
  const bool avanzar9=!pasos9||int(std::lround(j.t_ms*.06f))<objetivo9;
  if (!j.fin && !j.derrota_espera && j.listo_t <= 0 && avanzar9 && !dbz3::GameDimmed() && !dbz3::OverlayOpen()) {
    // Reintento solo si no habia datos en la carga; su edad normal viene de la intro.
    if (!j.fx_iniciado) Fx::Iniciar(j);
    // mando del jugador (registro 0): palanca +26/+27 (s8, arriba negativo) o direcciones +108 (teclado)
    // (lo guarda MandoVuelo al leer el juego el mando, antes de quitarselo al luchador de B3)
    const uint16_t d108 = g_pad.d108, dd = uint16_t(d108 | (d108 >> 8));
    int px = int(int8_t(g_pad.p26)), py = -int(int8_t(g_pad.p27));
    if (dd & (kArriba | kAbajo | kDerecha | kIzquierda)) {
      px = (dd & kDerecha) ? 127 : (dd & kIzquierda) ? -128 : 0;
      // PS2: el extremo negativo del s8 es-128. Arriba invierte ese eje;
      // 127 daba63/64 y reducia al cuadrado el empuje original.
      py = (dd & kArriba) ? 128 : (dd & kAbajo) ? -127 : 0;
    }
    float h = iwvuelo::EjeIW(px), v = iwvuelo::EjeIW(py);
    bool r1 = (g_pad.b12 & kR1) != 0;
    if (j.auto_) Piloto(j, h, v, r1);
    // Pad_GetStickAxes (EU 0x3394F0): X = la de mayor magnitud de las dos palancas (+0xD8/+0xDA); Y = la
    // derecha (+0xD9). Las dos por el mismo decodificado que la izquierda (EjeIW), tambien la Y: sin el,
    // cam_y valia x128 (el mando son -32768..32767) y +0x604 (cabeceo) saturado al tope con cualquier
    // toque (antes: j.cam_y = PalancaDerechaY() / 256). Signo cerrado con el decomp: *param_2 = (byte con signo)/64
    // (arriba positivo; el flag de inversion bRam005c1534&1 lo niega) y Viewer_UpdateOrbitCamera 0x339620 hace
    // +0x604 += Y * -96, igual que el port (EjeIW da arriba positivo y se multiplica por -96).
    // X = la de mayor magnitud de las dos palancas, Y = la derecha (traduccion en src/iw_mini_extra.cpp; mismo
    // decodificado /64 con zona muerta 64 que EjeIW).
    // EU 0x3394F0 Pad_GetStickAxes
    extra::PadGetStickAxes(px, std::clamp(dbz3::iwmini::PalancaDerechaX() / 256, -127, 127),
                           std::clamp(dbz3::iwmini::PalancaDerechaY() / 256, -127, 127), j.cam_x, j.cam_y);
    // EU 0x339620 (Viewer_UpdateOrbitCamera): el balanceo sale de X, la horizontal de las palancas, que es la MISMA
    // que gira (+0x608 += (-2048*X - +0x608) >> 3). El piloto de prueba no mueve la palanca fisica (dirige con h):
    // se le da esa misma X para que la camara se ladee con el giro como en el video del original.
    if (j.auto_ && j.cam_x == 0.0f && j.cam_y == 0.0f) j.cam_x = h;
    const float ax = j.x, az = j.z;
    if (j.auto_) {   // piloto de prueba: h, v -> bytes de palanca (inversa de EjeIW: zona muerta de 64)
      auto inv = [](float a) { return a == 0.0f ? 0 : std::clamp(int(std::lround(a * 64.0f)) + (a > 0 ? 64 : -64), -127, 127); };
      px = inv(h); py = inv(v);
    }
    const bool orig = VueloOriginal(base, x0, j, px, py, r1);   // T4B: posicion, giro, estado, medidor y objetos del ELF
    if (!orig) {
      Volar(base, x0, j, h, v, r1);
      if (static const bool poses = !std::getenv("DBZ3_IW_VUELO"); poses) AplicarPoseVuelo(x0, j, PoseVuelo(j, h, v));
    }
    j.rastro.push_back({j.ux, 0, j.uz});
    if (j.rastro.size() > 2400) j.rastro.pop_front();
    if (!orig) {
      j.t568 = std::max(0.0f, j.t568 - 1.0f);
      j.t56c = std::max(0.0f, j.t56c - 1.0f);
    }
    j.aura = std::max(0.0f, j.aura - 1.0f);
    // medidor del rival (FlyingMinigameSpritesScripts EU 0x33C660): congelado o base + extra del estado 5/6
    if (!orig && j.congelado > 0) {
      j.congelado = std::max(0.0f, j.congelado - 1.0f);
    } else {
      const float medidor_antes = j.medidor;
      if (orig) j.medidor = j.orig_o.medidor;   // T4B: FlyingMinigameSpritesScripts del ELF (el congelado tambien)
      else j.medidor = std::max(0.0f, j.medidor + j.sube[0] +
                                      (j.estado == 6 ? j.sube[2] : j.estado == 5 ? j.sube[1] : 0.0f));
      // EU 0x33C660 FlyingMinigameSpritesScripts_MissionObjectiveScripts
      // DS118: dialogos del rival en v1 (param 3..6 de Timelapse_Vegeta_GotenMeter_ObjectsCountVisuals, uno por
      // partida). Se mira el valor ANTERIOR de +0x51c, como el original, y el orden del else-if (solo el umbral mas
      // alto de ese cuadro): >=1,0 -> evento 1 (ADX 1112), >=0,9 -> 4 (1115), >=0,7 -> 2 (1113) y >=0,1 -> 3 (1114)
      // de la fila 4 de la tabla0x4803B0. La cola conserva retrato, texto y voz juntos.
      if (j.v == 1) {
        if (medidor_antes >= 1.0f && !(j.eventos & 1)) j.eventos |= 1, j.dialogos.push_back(1);
        else if (medidor_antes >= 0.9f && !(j.eventos & 8)) j.eventos |= 8, j.dialogos.push_back(4);
        else if (medidor_antes >= 0.7f && !(j.eventos & 4)) j.eventos |= 4, j.dialogos.push_back(2);
        else if (medidor_antes >= 0.1f && !(j.eventos & 2)) j.eventos |= 2, j.dialogos.push_back(3);
      }
    }
    // EU 0x33C660 FlyingMinigameSpritesScripts_MissionObjectiveScripts
    // DS118: frase del jugador al pasar de 1 s de vuelo (`fVar5 < 1000 && 1000 <= fVar6` con
    // CharacterDialoguesStart_(1, Audio_GetFilteredTableEntry(char, DAT_00483230[pj + variant*2]), ...). El indice
    // de la tabla 0x49C45C es {2,0x0B,2,2}: pj 1 usa 0x0B y el resto 2 (el mismo en las dos variantes).
    const float t_antes = j.t_ms;
    if (orig) j.t_ms = j.orig_o.t_ms; else j.t_ms += 1000.0f / 60.0f;
    if (!j.voz_inicio && t_antes < 1000.0f && j.t_ms >= 1000.0f) {
      j.voz_inicio = true;
      VozJugador(j, j.pj == 1 ? 0x0B : 2, "arranque del vuelo (EU 0x33C660)");
    }
    ++j.cuadros;
    Recoger(base, mgr, j, ax, az, orig ? &j.orig_o : nullptr);
    // DS182: auras de los estados de impulso (0x33F040/0x33ECC0/0x33F2C0/0x33F5E0) y un cuadro de los #AME.
    Fx::Estado(j, j.estado);
    Fx::Cuadro(j);
    if (std::getenv("DBZ3_SOLIW9_VUELO_PASOS")) {
      const int edad=int(std::lround(j.t_ms*.06f));
      if(edad<=120&&edad%30==0)
        REXLOG_INFO("dbz3 iw vuelo9: edad {} tiempo_ms {:.4f} estado {} posicion ({:.6f},{:.6f}) fx_edad {:.3f}",
                    edad,j.t_ms,j.estado,j.x,j.z,j.fx.empty() ? -1 : j.fx.front().in->edad);
    }
    if (!j.fin && !j.cierre && j.medidor >= 1.0f)
      Acabar(base, mgr, j, false, j.v == 0 ? "Vegeta te atrapa" : "Goten llega antes");
    if (!j.fin && !j.cierre && j.v == 0 && j.t_ms >= float(j.rango_s[4]) * 1000.0f)
      Acabar(base, mgr, j, false, "TIEMPO");
    static int traza = 0;
    static const bool traza_log = std::getenv("DBZ3_TEST_IWMINI") || std::getenv("DBZ3_R12_SONDA");
    if (traza_log && ++traza % 120 == 0) {
      // DS101: superficie de la malla del campo bajo el jugador (dato para comparar la altura con el original).
      const float hs = (j.vuelo_real && j.terreno.vale) ? j.terreno.Altura(j.x, j.z, -1e30f) : -1e30f;
      REXLOG_INFO("dbz3 r12: ({:.0f}, {:.0f}) giro {} estado {} vel {:.1f} medidor {:.3f} t {:.1f} s quedan {} zoom {:.1f} "
                  "cabeceo {} balanceo {} suelo {:.1f}",
                  j.x, j.z, j.yaw, j.estado, j.vel, j.medidor, j.t_ms / 1000.0f, j.restantes, j.zoom, j.cabeceo, j.balanceo,
                  hs > -1e29f ? hs : 0.0f);
    }
  }
  Rival(j);
  // los dos luchadores de B3 donde dice el vuelo, mirando hacia su marcha, sin KO; camara de CameraFocusWhileFlying
  V3 p = AB3(j, j.x, j.z), q = AB3(j, j.rival_p.x, j.rival_p.z);
  {   // el campo de R8 tiene mesetas mas altas que 400 x escala: se sube antes de llegar (60 u por delante) y sin saltos
    const float a0 = Ang(j.yaw);
    const V3 delante = AB3(j, j.x + std::sin(a0) * 60.0f / j.escala, j.z + std::cos(a0) * 60.0f / j.escala);
    static float y = 0;
    const float meta_y = std::max(p.y, delante.y);
    y = (estado == 3 || std::fabs(y - meta_y) > 200.0f) ? meta_y : y + (meta_y - y) * 0.12f;
    p.y = y;
    q.y = std::max(q.y, y);
  }
  // rival lejos (Goten por el recorrido): oculto y aparcado detras del jugador
  // La función de IW que representa al rival opera sobre iconos, no sobre el modelo del luchador.
  q = {p.x - std::sin(Ang(j.yaw)) * 60.0f, p.y, p.z - std::cos(Ang(j.yaw)) * 60.0f};
  REX_STORE_U8(x1 + kOcultoA, 1);
  REX_STORE_U8(x1 + kOcultoB, 1);
  static int sonda = 0;
  if (std::getenv("DBZ3_R12_SONDA") && ++sonda % 30 == 0 && sonda < 1200) {
    float cam[3] = {}, mira[3] = {};
    dbz3::cine::LeerCamara(base, cam, mira);
    REXLOG_INFO("dbz3 r12: sonda estado {}: luchador ({:.1f}, {:.1f}, {:.1f}) puesto ({:.1f}, {:.1f}, {:.1f}) suelo {:.1f} "
                "camara del juego ({:.1f}, {:.1f}, {:.1f}) -> ({:.1f}, {:.1f}, {:.1f})",
                estado, LoadF(base, x0 + 64), LoadF(base, x0 + 68), LoadF(base, x0 + 72), p.x, p.y, p.z,
                dbz3::suelo_iw::Altura(p.x, p.z, -999.0f), cam[0], cam[1], cam[2], mira[0], mira[1], mira[2]);
  }
  Poner(base, x0, p);
  Poner(base, x1, q);
  Mirar(base, x0, j.yaw);
  Mirar(base, x1, j.rival_yaw);
  // DS147: el cuerpo (matriz de mundo) mira hacia la marcha. Es lo que pone a Gohan de espaldas en 0x7E0; Mirar no
  // gira el modelo del vuelo (ver el comentario de Orientar).
  Orientar(base, x0, j.yaw);
  Orientar(base, x1, j.rival_yaw);
  if (j.cuadros == 1)
    REXLOG_INFO("dbz3 r12: cuerpo del jugador orientado con su matriz de mundo a rumbo {} ({:.1f} grados desde +Z) y "
                "el rival a {}; la camara va detras con el mismo rumbo",
                j.yaw, float(j.yaw & 0xFFFF) / 65536.0f * 360.0f, j.rival_yaw);
  StoreF(base, x0 + kVida, LoadF(base, x0 + kVidaMax));
  StoreF(base, x1 + kVida, LoadF(base, x1 + kVidaMax));
  j.p_dibujo = p;   // DS92: la flecha de mundo se cuelga de los pies que se pintan este cuadro
  j.p_dibujo_ok = true;
  const float pie[3] = {p.x, p.y, p.z};
  float ojo[3], obj[3];
  // R7: la camara de IW no gira de golpe con el rumbo: +0x600 += (rumbo - camara) >> 4 por cuadro (EU L427262-298).
  // Viewer_UpdateOrbitCamera 0x339620 (dt = 1 cuadro): cabeceo += -96 * Y, limitado a [-0x1400, +0xC00]; balanceo
  // hacia -2048 * X a 1/8 en los estados 2/3/5/6 y decae 7/8 en el resto; el estado 4 ademas lo parte por 2 (>>1 en
  // Viewer_StateFlyUpdate4). La X viene de Pad_GetStickAxes (la de mayor magnitud de las dos palancas).
  // EU 0x339620 Viewer_UpdateOrbitCamera
  j.cabeceo = std::clamp(int(int16_t(uint16_t(j.cabeceo + int(int16_t(int(j.cam_y * -96.0f)))))), -0x1400, 0xC00);
  if (j.estado == 2 || j.estado == 3 || j.estado == 5 || j.estado == 6)
    j.balanceo += (int(int16_t(int(j.cam_x * -2048.0f))) - j.balanceo) >> 3;
  else
    j.balanceo = j.balanceo * 7 >> 3;
  j.cam_x = j.cam_y = 0;
  j.cam_yaw = j.cam_yaw < 0 ? j.yaw : (j.cam_yaw + (int(int16_t(uint16_t((j.yaw - j.cam_yaw) & 0xFFFF))) >> 4)) & 0xFFFF;
  // R7 quinta tanda, zoom de IW (Viewer_UpdateOrbitCamera EU L427252-427326; valores de L427394-427401): distancia =
  // 52,2 + z, z va hacia la meta m a +5 por cuadro (subiendo) y -1 (bajando); con z o m negativos, +0,5 / -1. Al entrar
  // en el impulso (estados 5/6, Viewer_StatePoseBank5/6) m = +28 (80,2); en 2/3/5/6 al llegar a 28 m = 0; fuera de 5/6
  // m > 0 pasa a 0. El estado 4 (frenazo) pone m = -10 (42,2) desde Viewer_StateFlyUpdate4 (+0x614 = +0x61C) y lo
  // mantiene mientras dura; Volar lo fija al frenar.
  {
    const bool imp = j.estado == 5 || j.estado == 6, era = j.est_cam == 5 || j.est_cam == 6;
    if (imp && !era) j.zoom_meta = 28.0f;
    if ((j.estado == 3 || imp) ? 28.0f - 0.1f <= j.zoom : j.zoom <= -10.0f + 0.1f) j.zoom_meta = 0;
    if (!imp && j.zoom_meta > 0) j.zoom_meta = 0;
    const bool neg = j.zoom < 0 || j.zoom_meta < 0;
    const float sube = neg ? 0.5f : 5.0f, baja = 1.0f;
    j.zoom = j.zoom < j.zoom_meta ? std::min(j.zoom + sube, j.zoom_meta) : std::max(j.zoom - baja, j.zoom_meta);
    j.est_cam = j.estado;
  }
  // solIW9:52,2 y ancla7 originales, con proyeccion512/600x450 real.
  // La antigua aproximacion40H/30,8V y los factores0,665/0,84 no se aplican.
  const float dist_iw=kDistVueloIW*kEscalaModeloVuelo;
  iwvuelo::CamaraVuelo(pie,j.cam_yaw,ojo,obj,kRaizVueloIW,dist_iw,j.cabeceo);
  // EU339620: T(-D),roll,pitch,T(-zoom),yaw,T(ancla),T(pos).
  // Zoom va por el eje horizontal, no dentro de la orbita inclinada.
  const float zsx=std::sin(Ang(j.cam_yaw))*j.zoom,zsz=std::cos(Ang(j.cam_yaw))*j.zoom;
  ojo[0]-=zsx;ojo[2]-=zsz;obj[0]-=zsx;obj[2]-=zsz;
  // FlyingZaxis(-roll): Viewer_UpdateOrbitCamera 0x339620 llama FlyingZaxis(-*(int*)(G+0x608)) (L427329) y
  // CameraFocusWhileFlying 0x339AA0 igual (L427407); el port pasa -balanceo, mismo signo que el motor.
  const float roll = -float(j.balanceo) / 65536.0f * 6.2831853f;
  // Fuente única: cámara de órbita del vuelo (Viewer_UpdateOrbitCamera EU 0x339620 + CameraFocusWhileFlying
  // EU 0x339AA0). No realimentar una lectura del cuadro previo: con giro/altura variables introduce error atrasado.
  float leo[3] = {}, lob[3] = {};
  const bool leido = dbz3::cine::LeerCamara(base, leo, lob);
  float ojo_ped[3], obj_ped[3];
  for (int i = 0; i < 3; ++i) ojo_ped[i] = ojo[i], obj_ped[i] = obj[i];
  // DS132: B3 dibuja 0,8 x lo puesto + 0,2 x su camara (A3b). Se invierte la mezcla con la camara propia de B3
  // reconstruida de la lectura del cuadro anterior (N = (leido - 0,8 W_ant) / 0,2) y se escribe W = (pedido - 0,2 N)/0,8.
  // Con eso lo DIBUJADO es la pedida (o la de un cuadro antes si B3 aplica lo escrito al cuadro siguiente). DS128
  // escribia +0x150/+0x160 creyendo que era la otra fuente: ese campo es un puntero (ver LogCampoGuion), por eso no
  // tenia ningun efecto. Aqui no se toca.
  const float leido6[6] = {leo[0], leo[1], leo[2], lob[0], lob[1], lob[2]};
  const float ped6[6] = {ojo_ped[0], ojo_ped[1], ojo_ped[2], obj_ped[0], obj_ped[1], obj_ped[2]};
  float esc6[6], n_est[6];
  const bool mezcla_inv=false;
  std::copy(ped6,ped6+6,esc6);std::copy(leido6,leido6+6,n_est);
  const float esc_ojo[3] = {esc6[0], esc6[1], esc6[2]}, esc_obj[3] = {esc6[3], esc6[4], esc6[5]};
  dbz3::cine::Camara(base, esc_ojo, esc_obj, roll);
  // DS132: la pedida de este cuadro va tambien al gancho de dibujo (sub_820F4300), que la aplica DESPUES de la camara
  // del juego: la vista que se dibuja (y la que usa Escena para la flecha) es la pedida, no la mezcla 0,8/0,2.
  PublicarCamaraVuelo(ojo_ped, obj_ped, roll);
  for (int i = 0; i < 6; ++i) j.cam_esc[i] = esc6[i];
  j.cam_esc_ok = leido;
  ++j.cuadros_cam;
  // DS124: se guarda la camara PEDIDA para que VistaAsentada compare con la DIBUJADA (mgr+0x60) en el READY?.
  for (int i = 0; i < 3; ++i) j.cam_ped[i] = ojo_ped[i], j.cam_ped[3 + i] = obj_ped[i];
  j.cam_ped_ok = true;
  static int n_log_cam = 0;   // DS92: una linea por segundo de dibujo (60 cuadros), aunque el juego este en pausa
  static const bool cam_log = std::getenv("DBZ3_TEST_IWMINI") || std::getenv("DBZ3_R12_SONDA");
  if (cam_log && ++n_log_cam % 60 == 0) {
    LogCamaraVuelo(j, ojo_ped, obj_ped, leido, leo, lob, fov_juego, k_fov, dist_iw);
    // DS132: lo que se escribe, lo que dejo B3 del cuadro anterior y la camara propia reconstruida (la que se anula).
    REXLOG_INFO("dbz3 r12 cam: mezcla t {:.1f}s {} | pedido ojo ({:.1f}, {:.1f}, {:.1f}) obj ({:.1f}, {:.1f}, {:.1f}) "
                "| escrito ojo ({:.1f}, {:.1f}, {:.1f}) obj ({:.1f}, {:.1f}, {:.1f}) | B3 leyo antes ojo ({:.1f}, "
                "{:.1f}, {:.1f}) obj ({:.1f}, {:.1f}, {:.1f}) | N reconstruida ojo ({:.1f}, {:.1f}, {:.1f}) obj "
                "({:.1f}, {:.1f}, {:.1f})",
                j.t_ms / 1000.0f, mezcla_inv ? "invertida 0,8/0,2" : "sin lectura previa: pedido tal cual",
                ojo_ped[0], ojo_ped[1], ojo_ped[2], obj_ped[0], obj_ped[1], obj_ped[2], esc6[0], esc6[1], esc6[2],
                esc6[3], esc6[4], esc6[5], leo[0], leo[1], leo[2], lob[0], lob[1], lob[2], n_est[0], n_est[1], n_est[2],
                n_est[3], n_est[4], n_est[5]);
  }
  // DS132: primera linea del vuelo (cuadro 1) y luego 1/s: prueba de que +0x150/+0x160 es un puntero y no la fuente.
  static const bool prueba150 = std::getenv("DBZ3_R12_PRUEBA_150") != nullptr;
  if (cam_log && (j.cuadros_cam == 1 || j.cuadros_cam % 60 == 0))
    LogCampoGuion(base, mgr, j.cuadros_cam, prueba150 && j.cuadros_cam == 1);
}

namespace {

// DS182/DS184: bruma del horizonte. En el original es la niebla del #MCF (color +0x1C, rango de DISTANCIA +0x28/+0x2C
// = 2400..3700 en el 881) aplicada al AGUA del campo; el port no la tenia porque el campo de IW se dibuja con
// B3 (ps2::Escena solo pinta aros/objetos) y la niebla de B3 no llega a esas mallas (DS176 §4).
// DS184: el color es FOGCOL, dword 0x00BBGGRR (R = byte BAJO: el GS pone R en los bits 0-7 de FOGCOL; misma conversion
// que mod center hd/campos_iw.py poner_niebla: r = c & 0xFF, g = (c>>8) & 0xFF, b = (c>>16) & 0xFF). Leerlo como ARGB
// daba 0x00FFFF87 -> 255/255/135 (AMARILLO): era la franja de la prueba del 27/10. Con FOGCOL es 135/255/255 (turquesa).
// Se compone como niebla lineal sobre el plano del agua y=0: cada punto de pantalla lanza el rayo de la camara DIBUJADA
// (BaseDeVista, la misma matriz que Escena), se corta con y=0 y alfa = (d-ini)/(fin-ini). Las lineas de igual distancia
// son rectas PARALELAS al horizonte del plano, que no es horizontal si la camara balancea (el vuelo llega a ~11 grados,
// medido en el log r12 del 27/10: balanceo 2041); por eso se pintan como tiras perpendiculares a la proyeccion del eje
// Y, no como filas. Por encima del horizonte no hay agua: se hace un remate que arranca con el MISMO alfa del horizonte
// (continuo, sin escalon) y se apaga en kAltoBrumaCielo. Su alcance no lo da el #MCF: medido en el fotograma 1946 s,
// el cielo pasa de 110/206/220 (casi el color de niebla) a azul en ~80 px de 719 = 11 % del alto.
constexpr float kAltoBrumaCielo = 0.11f;   // fraccion del alto, medida en el fotograma 1946 s
void VeloHorizonte(const Juego& j, const ps2::Camara& cam, const ImVec2& sz) {
  if (!cam.ok || !(j.fog_fin > j.fog_ini + 1.0f) || sz.y < 16.0f) return;
  const BaseVista bv = BaseDeVista(cam);
  if (!bv.ok || bv.ojo[1] < 1.0f) return;
  const float fov = cam.fov > 0.2f ? cam.fov : 0.7853981634f;
  const float foc = sz.y * 0.5f / std::tan(fov * 0.5f);
  const float cx = sz.x * 0.5f, cy = sz.y * 0.5f;
  // FOGCOL 0x00BBGGRR (mod center hd/campos_iw.py; GS FOGCOL R en los bits 0-7 del registro)
  const ImU32 rgb = IM_COL32(int(j.fog_rgb & 0xFF), int((j.fog_rgb >> 8) & 0xFF), int((j.fog_rgb >> 16) & 0xFF), 255);
  // Normal en pantalla de las lineas de igual elevacion (perpendicular al horizonte): (-der.y, arr.y)/L. Sin balanceo
  // es (0,1) y coincide con la formula de filas de DS182.
  const float L = std::sqrt(bv.der[1] * bv.der[1] + bv.arr[1] * bv.arr[1]);
  if (!(L > 1e-3f)) return;
  const float nx = -bv.der[1] / L, ny = bv.arr[1] / L;
  const float ex = -ny, ey = nx;                             // a lo largo del horizonte
  const float u_h = foc * bv.del[1] / L;                     // u del horizonte (u aumenta hacia abajo)
  const float alto_cielo = sz.y * kAltoBrumaCielo;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const float medio = (sz.x + sz.y) * 0.75f;                 // media longitud de tira: cubre la pantalla con balanceo
  const float u_min = -(std::fabs(nx) * sz.x + std::fabs(ny) * sz.y) * 0.5f;
  const float u_max = -u_min;
  const float paso = 2.0f;
  // DS184: una linea por sesion con los datos del velo (para poder comprobarlo en el log sin instrumentar nada).
  static bool una_vez = false;
  if (!una_vez) {
    una_vez = true;
    REXLOG_INFO("dbz3 r12: bruma #MCF rgb={},{},{} rango {:.0f}..{:.0f} ojo_y {:.1f} horizonte {:.0f}px de {} "
                "(balanceo de la vista {:.1f} grados)",
                int(j.fog_rgb & 0xFF), int((j.fog_rgb >> 8) & 0xFF), int((j.fog_rgb >> 16) & 0xFF), j.fog_ini,
                j.fog_fin, bv.ojo[1], u_h + cy, sz.y, std::atan2(bv.der[1], bv.arr[1]) * 57.2957795f);
  }
  for (float u = u_min; u < u_max; u += paso) {
    const float uc = u + paso * 0.5f;
    float a = 0.0f;
    if (uc > u_h) {
      // rayo del punto a `uc` del centro, sobre la perpendicular al horizonte
      const float vx = uc * nx / foc, vy = -uc * ny / foc;
      float d[3] = {bv.del[0] + bv.der[0] * vx + bv.arr[0] * vy, bv.del[1] + bv.der[1] * vx + bv.arr[1] * vy,
                    bv.del[2] + bv.der[2] * vx + bv.arr[2] * vy};
      const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (l > 1e-4f) {
        for (float& v : d) v /= l;
        if (d[1] < -1e-4f) {
          const float dist = bv.ojo[1] / (-d[1]);   // distancia al plano del agua y=0 a lo largo del rayo
          a = std::clamp((dist - j.fog_ini) / (j.fog_fin - j.fog_ini), 0.0f, 1.0f);
        }
      }
    } else {
      // remate del cielo: 1 en el horizonte -> 0 a kAltoBrumaCielo por encima (smoothstep: sin borde recto)
      const float t = 1.0f - std::clamp((u_h - uc) / alto_cielo, 0.0f, 1.0f);
      a = t * t * (3.0f - 2.0f * t);
    }
    if (a <= 0.004f) continue;
    const int alfa = int(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    const ImU32 col = (rgb & 0x00FFFFFFu) | (uint32_t(alfa) << 24);
    const float x0 = cx + nx * u, y0 = cy + ny * u;
    const float x1 = cx + nx * (u + paso), y1 = cy + ny * (u + paso);
    dl->AddQuadFilled(ImVec2(x0 - ex * medio, y0 - ey * medio), ImVec2(x0 + ex * medio, y0 + ey * medio),
                      ImVec2(x1 + ex * medio, y1 + ey * medio), ImVec2(x1 - ex * medio, y1 - ey * medio), col);
  }
}

const ps2::Escena::Toon kToon{};   // toon de IW en los objetos (R8 / ps2_modelo)

class Panel : public iwgs::Drawer {
 public:
  Panel() = default;

 protected:
  bool Visible() const override { return g_corre.load(); }
  void OnDraw(ImGuiIO& io) override {
    if (!g_corre.load()) return;
    std::shared_ptr<Juego> jp;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      jp = g_j;
    }
    uint8_t* base = Base();
    if (!jp || !base) return;
    const int st = P::EstadoCombate();
    const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
    if (st < 3 || st > 8 || !mgr) return;   // iw_fix: hasta el 8: B3 pintaba su menu azul "Luchar otra vez?" ~1,7 s tras el fundido
    if (jp.get() != hecho_) {
      hecho_ = jp.get();
      ComenzarAtlas();
      aviso_tx_.clear();
      for (const auto& im:jp->aviso_atlas) aviso_tx_.push_back(ps2::Crear(im));
      dialogo_tx_.clear();
      for (const auto& im:jp->dialogo_atlas) dialogo_tx_.push_back(ps2::Crear(im));
      for (int i=0;i<4;++i) dialogo_textos_[i]=ps2::Crear(jp->dialogo_textos[i]);
      tx_aro_ = ps2::Texturas(jp->m_aro);
      tx_moneda_ = ps2::Texturas(jp->m_moneda);
      tx_casa_ = ps2::Texturas(jp->m_casa);
      for (int k = 0; k < 4; ++k) tx_item_[k] = ps2::Texturas(jp->m_item[k]);
      // ITEMA08_BLV_CHARACTER (891/16): t0 cara de Goten en su aro, t1 la flecha roja, t2 cara de Vegeta: el objeto 8
      // (congela el medidor) lleva la cara del rival; la malla apunta a la t0, en v0 va la de Vegeta
      if (jp->v == 0 && tx_item_[2].size() > 2) std::swap(tx_item_[2][0], tx_item_[2][2]);
      clear_ = ps2::Crear(jp->clear);
      if (P::AfsPs2 usa; usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) {
        const std::vector<uint8_t> t51 = P::HijoAmb(usa.Entrada(5), 1);
        ready_ = ps2::Crear(P::TexturaAmt(t51, 8));   // DATA_USA 5/1 t08 "READY?"
        start_ = ps2::Crear(P::TexturaAmt(t51, 0));   // t00 "START!"
        goal_ = ps2::Crear(P::TexturaAmt(t51, 3));    // DS84: t03 = "GOAL!" (el rotulo real, no texto a mano)
      }
      // DS117: la flecha es el modelo ITEMA04 (891/1 + #AMT 0) con el color en los vertices; se dibuja en espacio
      // de camara a (0,-9,-45), como World_CharacterLoader EU 0x33B580 (_LAB_00483250 = 0x483250).
      tx_flecha_ = ps2::Texturas(jp->m_flecha);
      // DS182: atlas del paquete de efectos (891/28 hijo 0) + su version de SUMA (alfa = brillo), como el
      // minijuego de disparo (P4/iw_mini_disparo): la PS2 mezcla los efectos con Cs*As+Cd y no con alfa normal.
      tx_atlas_.clear();
      tx_suma_.clear();
      for (const P::Imagen& im : jp->fx_atlas) {
        tx_atlas_.push_back(ps2::Crear(im));
        RegistrarTextura(tx_atlas_.back().id(),im);
        P::Imagen su = im;
        for (size_t i = 0; i + 3 < su.rgba.size(); i += 4)
          su.rgba[i + 3] = uint8_t(std::max({su.rgba[i], su.rgba[i + 1], su.rgba[i + 2]}) * su.rgba[i + 3] / 255);
        tx_suma_.push_back(ps2::Crear(su));
      }
      REXLOG_INFO("dbz3 r12: flecha ITEMA04 lista: {} triangulos, {} texturas ({}x{})", jp->m_flecha.malla.tris.size(),
                  tx_flecha_.size(), tx_flecha_.empty() ? 0 : tx_flecha_[0].w, tx_flecha_.empty() ? 0 : tx_flecha_[0].h);
      fin_t_ = -1;
    }
    const Juego& j = *jp;
    const bool en_intro = j.intro && !j.intro_done;
    const bool en_cierre = j.cierre;
    const int cierre_t = j.cierre_t;
    const float t = float(dbz3::RelojT(ImGui::GetTime()));
    // DS137: la camara del cuadro que B3 pinta (publicada por el gancho de sub_820F4300: ojo/objetivo del render +
    // balanceo). Antes se usaba mgr+0x60, que el gancho pisa con la camara PEDIDA: 78-94 px de diferencia vertical en
    // los pies del luchador. Sin cuadro publicado (antes del vuelo) se queda CamaraCombate, como siempre.
    ps2::Camara cam;
    if (!CamaraDelRender(base, io.DisplaySize.x, io.DisplaySize.y, cam)) cam = ps2::CamaraCombate(base, io.DisplaySize);
    float cam_ojo[3] = {}, cam_mira[3] = {};
    const bool cam_ok = dbz3::cine::LeerCamara(base, cam_ojo, cam_mira);
    escena_.Empezar();
    float segundos = 0, medidor = 0;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      const V3 yo = AB3(j, j.x, j.z);
      // DS119: distancia entre la camara del port y la que tendria IW (52,2 del punto de mira). Sirve para
      // EscalaObjetoVuelo: un objeto lejano no gana nada del acercamiento de DS117 y con el FOV de B3 saldria 0,665
      // de su tamano de IW (los aros "casi no se ven"); el factor lo devuelve a su tamano de pantalla del original.
      float dif_cam = 0.0f;
      if (cam_ok) {
        const V3 pie_dib = j.p_dibujo_ok ? j.p_dibujo : yo;
        const float mira_p[3] = {pie_dib.x, pie_dib.y + kRaizVueloIW, pie_dib.z};
        dif_cam = std::clamp(kDistVueloIW * kEscalaModeloVuelo - DistanciaCamara(cam_ojo, mira_p), 0.0f, 40.0f);
      }
      // En la intro y el cierre la camara es la del clip del #AMC y ya lleva su propia compensacion (AcercarPorFov):
      // el factor de pantalla de los objetos es solo para la camara del vuelo (CameraFocusWhileFlying).
      const bool camara_vuelo = !en_intro && !en_cierre;
      // cada objeto en su copia mas cercana al jugador (el mundo da la vuelta)
      auto sitio = [&](float x, float z) { return AB3(j, j.x + Envolver(x - j.x, j.periodo), j.z + Envolver(z - j.z, j.periodo)); };
      // niebla de IW; en la intro el centro es la camara del guion (los aros/la casa se ven desde alli)
      auto cerca = [&](const V3& s) {
        return en_intro ? std::hypot(s.x - j.intro_ojo[0], s.z - j.intro_ojo[2]) < 6000.0f
                        : std::hypot(s.x - yo.x, s.z - yo.z) < 3000.0f * j.escala;
      };
      const V3* flecha_a = nullptr;
      V3 objetivo;
      for (size_t k = 0; k < j.objs.size(); ++k) {
        const Obj& o = j.objs[k];
        if (!o.activo) continue;
        // DS84: el tipo 0x0A es el disparador del aura dash (no tiene cartel en el recorrido). Antes se pintaba
        // con ITEMA10_BLV_BODY y salian las "capsulas grandes con punto rojo" que no estan en el original.
        if (o.tipo == 0xA) continue;
        const V3 s = sitio(o.x, o.z);
        // flecha (G+0x544): v0 el primer punto de control visible de la lista, v1 el mas cercano
        // EU 0x33AD10 GetObject_WhileFlying: elige el objeto del tipo pedido (+0x50c: 0x100 con puntos de control, si no
        // 0) y escribe el rumbo hacia el en +0x544 = angulo x 32768/pi. En v0 se queda con el PRIMERO de la lista y en
        // v1 con el mas cercano (distancia en el plano con ObjectsCountEffect): es este mismo criterio.
        // DS68 cierra el [H] del UV: +0x540 = clamp(13 - distancia x 0.006, 1, 10) NO es el tamano, es la VELOCIDAD
        // del canal de UV (+0x53c += +0x540 x dt en 0x33C660, linea 429277). World_CharacterLoader (0x33B580) solo
        // evalúa UvAnim_EvalAllChannels(+0x53c, +0x538) si +0x538 != 0, y +0x538 SOLO se escribe con 0 en la carga
        // de recursos (0x33D900, linea 429893): la rama nunca se ejecuta, asi que en el original no hay avance de UV
        // visible y la flecha va a tamano fijo. No hay efecto que portar.
        if (o.tipo == kControl &&
            (!flecha_a || (j.v == 1 && std::hypot(s.x - yo.x, s.z - yo.z) < std::hypot(objetivo.x - yo.x, objetivo.z - yo.z))))
          objetivo = s, flecha_a = &objetivo;
        if (!cerca(s)) continue;
        const Modelo* mo = o.tipo == kControl ? &j.m_aro : EsMoneda(o.tipo) ? &j.m_moneda
                           : o.tipo == 2                 ? &j.m_item[0]
                           : o.tipo == 3                 ? &j.m_item[1]
                           : (o.tipo & 0xFF) == 8        ? &j.m_item[2]
                                                         : &j.m_item[3];
        const auto& tx = mo == &j.m_aro ? tx_aro_ : mo == &j.m_moneda ? tx_moneda_ : tx_item_[mo - j.m_item];
        // la malla va con la y minima en 0: el aro (radio 24) centrado a la altura del cuerpo (+8)
        const float y = mo == &j.m_aro ? s.y + 8.0f - 24.0f : s.y + 4.0f;
        // los objetos _BLV_ (ITEMA02/03/08/10) son carteles: de cara a la camara ([VERIFICAR]: frente de la malla en +z)
        const float giro = mo == &j.m_aro || mo == &j.m_moneda || !cam_ok ? mo->giro.En(t)
                                                                           : std::atan2(cam_ojo[0] - s.x, cam_ojo[2] - s.z);
        // DS119: tamano de pantalla de IW (ver EscalaObjetoVuelo). El aro mide r 17,2..24 u en el #AMO (DS117);
        // el dato de mundo se respeta y el factor solo compensa el FOV de B3, no agranda el modelo por capricho.
        const float ancla[3] = {s.x, y, s.z};
        const float d_obj = cam_ok ? DistanciaCamara(cam_ojo, ancla) : 1.0f;
        if (mo->malla)
          escena_.Poner(cam, mo->malla, tx, {s.x, y, s.z}, giro, 1.0f, kToon);
      }
      if (j.meta.activo) {
        V3 s = sitio(j.meta.x, j.meta.z);
        objetivo = s;
        flecha_a = &objetivo;
        // DS118: el suelo de la meta, de la MALLA del propio campo (DS101: 881/883 no estan en suelo_iw::kCampos y
        // Altura() devolvia 0 siempre). Si no hay malla, la consulta de siempre.
        float suelo_meta = j.campo ? dbz3::suelo_iw::Altura(s.x, s.z, 0.0f) : 0.0f;
        if (j.vuelo_real && j.terreno.vale) {
          const float h = j.terreno.Altura(s.x, s.z, -1e30f);
          if (h > -1e29f) suelo_meta = h;
        }
        s.y = suelo_meta;
        // la casa del Gran Patriarca (883/7) / el objeto GOAL del 881/7 (el #AMO con los huesos BBKAI_RABBITS: el
        // modelo 3D de la meta de 0x7E0, con su #AMT 881/8 y la animacion de UV del #MTA 881/10). R8 los deja fuera
        // del campo para el minijuego; aqui van en su sitio, por sus huesos. [VERIFICAR] altura/giro en pantalla.
        if (j.m_casa.malla && cerca(s)) {
          ps2::Malla casa = j.m_casa.malla;
          AplicarDerivaUvCasa(casa, j.uv_casa_dv, t * 60.0f);
          const float ancla[3] = {s.x, s.y, s.z};
          const float d_obj = cam_ok ? DistanciaCamara(cam_ojo, ancla) : 1.0f;
          escena_.Poner(cam, casa, tx_casa_, s, 0, 1.0f, kToon);
        }
      }
      // DS119: el objeto 10 del #MSF (tipo 0xA) NO es un aura que se cuelga del jugador. Viewer_UpdateObjectCollection
      // (EU 0x33B7D0, rama uVar2 == 10) llama a ObjectsCollectionTeleport(10, ...) y a Aura_Dash(3, hit, 0): es un
      // disparador (y ademas mueve al jugador). ITEMA10_BLV_BODY (891/20 + #AMT 891/19) es su cartel/portal, no un
      // aura: DS84 lo pintaba encima del luchador y en la prueba del 27/10 (02:07:54, a_salida) llenaba la pantalla
      // con un primer plano marron de 42 u (el modelo se veia como "la camara dentro de un personaje"). No se pinta.
      // DS117/DS119/DS124/DS128: flecha ITEMA04. World_CharacterLoader EU 0x33B580 la dibuja en ESPACIO DE CAMARA, en
      // el punto fijo _LAB_00483250 = (0, -9, -45) (ELF EU 0x483250: 0,-9,-45,1), girada con el rumbo al siguiente
      // objeto (CharacterAxisPosition2). Ese punto son 11,31 grados bajo el eje de la vista (74,2 % del alto con el FOV
      // de B3) y el modelo se ve a tamano fijo: ~9 % del ancho y ~6,7 % del alto en el original (802s/815s).
      // DS128: el punto NO se toma del centro del encuadre, sino de la PROYECCION DE LOS PIES que se pintan: medido en
      // el original, el ala de arriba de la flecha empieza donde acaban las botas (815s: botas 69,3 % del alto y flecha
      // 69,8..76,4 %, centro 73,1 %) y en horizontal cae bajo el luchador. Con el centro en la pantalla salia separada
      // de el (b_vuelo: 73,5 % con los pies al 83,3 %; a_salida: 73,5 % con los pies al 56,3 %, "abajo a la izquierda")
      // porque el luchador no estaba donde la camara pedia. Ahora: se pasan los pies a la base de la camara DIBUJADA
      // (BaseDeVista: la misma matriz que proyecta Escena), su direccion se lleva al plano de la flecha
      // (profundidad de los pies + kFlechaDetras) y el centro baja kFlechaBajoPies (0,6 % del alto): la flecha queda
      // pegada a las botas, sin taparse con la barra SAFE. La escala crece con la distancia (kFlechaPantalla) para
      // conservar el tamano de pantalla cuando la camara se aleja con el impulso (zoom +28). El giro sigue siendo el
      // rumbo al objeto (punta del modelo en -z: giro = rumbo + pi).
      // DS137: los pies de la flecha son los del LUCHADOR QUE SE PINTA, del MISMO cuadro que la camara: se toman del
      // cuadro que publica el gancho (X+1616, los 22 puntos de cuerpo que el juego saca de los huesos cada cuadro; el
      // mas bajo es el suelo de las botas). X+48 es la posicion logica: queda ~2 u por encima/Detras de las botas
      // dibujadas. Se leen UNA vez por cuadro: antes la flecha y el log las leian por separado dentro de OnDraw y con
      // el vuelo a ~20 u/cuadro (1,2 u/ms) las dos lecturas ya diferian 3-4 u (~100 px).
      const RenderCuadro rc = CuadroRender();
      auto sana = [&](const float p[3]) {
        return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) &&
               std::fabs(p[0] - yo.x) < 300.0f * std::max(1.0f, j.escala) &&
               std::fabs(p[2] - yo.z) < 300.0f * std::max(1.0f, j.escala);
      };
      V3 pies{};
      const bool bajo_sano = rc.ok && rc.hay_bajo && sana(rc.bajo) && sana(rc.pies) &&
                             std::hypot(rc.bajo[0] - rc.pies[0], rc.bajo[2] - rc.pies[2]) < 6.0f * std::max(1.0f, j.escala) &&
                             rc.bajo[1] - rc.pies[1] > -4.0f * std::max(1.0f, j.escala) &&
                             rc.bajo[1] - rc.pies[1] < 8.0f * std::max(1.0f, j.escala);
      if (bajo_sano) pies = {rc.bajo[0], rc.bajo[1], rc.bajo[2]};
      else if (rc.ok && sana(rc.pies)) pies = {rc.pies[0], rc.pies[1], rc.pies[2]};
      else {
        const uint32_t xg = mgr + kX0;
        const float g[3] = {LoadF(base, xg + 48), LoadF(base, xg + 52), LoadF(base, xg + 56)};
        if (sana(g)) pies = {g[0], g[1], g[2]};
        else pies = j.p_dibujo_ok ? j.p_dibujo : yo;
      }
      const bool cab_ok = rc.ok && rc.hay_cabeza && sana(rc.cabeza);
      const V3 cab = cab_ok ? V3{rc.cabeza[0], rc.cabeza[1], rc.cabeza[2]} : V3{};
      V3 flecha_mundo{};
      bool flecha_puesta = false;
      if (!en_intro && !en_cierre && flecha_a && jp->m_flecha.malla && !tx_flecha_.empty()) {
        const BaseVista bv = BaseDeVista(cam);
        if (bv.ok) {
          const float px = pies.x - bv.ojo[0], py = pies.y - bv.ojo[1], pz = pies.z - bv.ojo[2];
          const float zf=45.0f; // vector483250, en espacio de camara
          // 27/10 (coordinador): como el original, la flecha va en el punto FIJO de camara (0,-9,-45) de EU 0x483250
          // (centrada, 11,31 grados bajo el eje). Atarla a los pies fallaba: el cuadro del gancho no llega
          // ("esfera no vale") y X+48/yo quedan ~100 px a la izquierda/abajo de donde B3 pinta a Krilin. Con la camara ya
          // convergida (DS132) el luchador queda centrado y la flecha bajo sus botas, como en 802s/815s.
          const float xf = 0.0f;
          const float yf = -9.0f / 45.0f + kFlechaBajoPies;   // se resta kFlechaBajoPies abajo
          (void)px; (void)py; (void)pz;
          const float z = zf;
          const V3 fpos{bv.ojo[0] + bv.der[0] * (xf * z) + bv.arr[0] * ((yf - kFlechaBajoPies) * z) + bv.del[0] * z,
                        bv.ojo[1] + bv.der[1] * (xf * z) + bv.arr[1] * ((yf - kFlechaBajoPies) * z) + bv.del[1] * z,
                        bv.ojo[2] + bv.der[2] * (xf * z) + bv.arr[2] * ((yf - kFlechaBajoPies) * z) + bv.del[2] * z};
          const int yaw_obj = int(std::lround(
              std::atan2(flecha_a->x - pies.x, flecha_a->z - pies.z) / 6.2831853f * 65536.0f));
          // el modelo apunta a -z (punta en z=-3,75): giro = rumbo + pi para que la punta mire al objeto.
          escena_.Poner(cam, jp->m_flecha.malla, tx_flecha_, fpos, Ang(yaw_obj) + 3.14159265f, 1.0f,
                        kToon);
          flecha_mundo = fpos;
          flecha_puesta = true;
        }
      }
      segundos = j.v == 0 ? std::max(0.0f, float(j.rango_s[4]) - j.t_ms / 1000.0f) : j.t_ms / 1000.0f;
      medidor = std::clamp(j.medidor, 0.0f, 1.0f);
      // DS92/DS137: la camara que se DIBUJA de verdad (la del cuadro publicado por el gancho: ojo/objetivo/matriz del
      // render). Antes se leia mgr+0x60 (la pedida, reescrita por el gancho) y no era la del dibujo: los pies salian
      // 78-94 px mas abajo que Krilin en la captura.
      static int n_log_panel = 0;
      static const bool panel_log = std::getenv("DBZ3_TEST_IWMINI") || std::getenv("DBZ3_R12_SONDA");
      if (panel_log && cam.ok && ++n_log_panel % 60 == 0) {
        const BaseVista bvd = BaseDeVista(cam);
        const float d_ped = j.cam_ped_ok ? DistanciaCamara(j.cam_ped, j.cam_ped + 3) : 0.0f;
        REXLOG_INFO("dbz3 r12 cam: dibujo t {:.1f}s estado {} zoom {:.1f} | vista RENDER ojo ({:.1f}, {:.1f}, {:.1f}) "
                    "cabeceo {:.2f} rumbo {:.2f} D(pedido) {:.1f} | FOV dibujo {:.5f} rad ({}) | jugador ({:.1f}, "
                    "{:.1f}, {:.1f})",
                    j.t_ms / 1000.0f, j.estado, j.zoom, bvd.ojo[0], bvd.ojo[1], bvd.ojo[2],
                    CabeceoDeDireccion(bvd.del), RumboDeDireccion(bvd.del), d_ped, cam.fov, NombreFov(cam.fov), pies.x,
                    pies.y, pies.z);
        // DS132/DS137 (b): medida en PIXELES de los pies y la cabeza que se pintan, del centro final de la flecha y
        // de su separacion, todo con la MISMA camara del dibujo y el MISMO cuadro. Es lo que el coordinador mide en la
        // captura: si los pies y la flecha coinciden aqui, la flecha esta bajo las botas.
        const float pa[3] = {pies.x, pies.y, pies.z}, fa[3] = {flecha_mundo.x, flecha_mundo.y, flecha_mundo.z};
        const float ca[3] = {cab.x, cab.y, cab.z}, la[3] = {rc.pies[0], rc.pies[1], rc.pies[2]};
        float pxs = 0, pys = 0, kp = 0, pxf = 0, pyf = 0, kf = 0, pxc = 0, pyc = 0, kc = 0, pxl = 0, pyl = 0, kl = 0;
        const bool ok_pies = dbz3::iwvuelo::Proyectar(base, io.DisplaySize.x, io.DisplaySize.y, pa, pxs, pys, kp);
        const bool ok_flecha = flecha_puesta &&
                                  dbz3::iwvuelo::Proyectar(base, io.DisplaySize.x, io.DisplaySize.y, fa, pxf, pyf, kf);
        const bool ok_cab = cab_ok && dbz3::iwvuelo::Proyectar(base, io.DisplaySize.x, io.DisplaySize.y, ca, pxc, pyc, kc);
        const bool ok_log = rc.ok && dbz3::iwvuelo::Proyectar(base, io.DisplaySize.x, io.DisplaySize.y, la, pxl, pyl, kl);
        REXLOG_INFO("dbz3 r12 cam: pixeles t {:.1f}s pantalla {:.0f}x{:.0f} | pies {} ({:.0f}, {:.0f}) [{:.1f} %, "
                    "{:.1f} %] esfera {}{} | X+48 {} ({:.0f}, {:.0f}) [{:.1f} %, {:.1f} %] | cabeza {} ({:.0f}, {:.0f}) "
                    "[{:.1f} %, {:.1f} %] | flecha {} ({:.0f}, {:.0f}) [{:.1f} %, {:.1f} %] | separacion {} ({:.0f}, "
                    "{:.0f}) px",
                    j.t_ms / 1000.0f, io.DisplaySize.x, io.DisplaySize.y, ok_pies ? "si" : "no", pxs, pys,
                    pxs * 100.0f / io.DisplaySize.x, pys * 100.0f / io.DisplaySize.y,
                    rc.hay_bajo ? std::to_string(rc.i_bajo) : std::string("-"), bajo_sano ? "" : " (no vale, uso X+48)",
                    ok_log ? "si" : "no", pxl, pyl, pxl * 100.0f / io.DisplaySize.x, pyl * 100.0f / io.DisplaySize.y,
                    ok_cab ? "si" : "no", pxc, pyc, pxc * 100.0f / io.DisplaySize.x, pyc * 100.0f / io.DisplaySize.y,
                    ok_flecha ? "si" : "no", pxf, pyf, pxf * 100.0f / io.DisplaySize.x, pyf * 100.0f / io.DisplaySize.y,
                    ok_pies && ok_flecha ? "flecha-pies" : "-", pxf - pxs, pyf - pys);
      }
    }
    escena_.Tapar(cam, base, mgr + kX0);
    escena_.Tapar(cam, base, mgr + kX0 + kXPaso);
    escena_.Pintar(ImGui::GetBackgroundDrawList());
    // DS182/DS184: niebla del #MCF sobre el agua (bruma turquesa del horizonte) y despues los #AME del visor (las
    // lineas de velocidad y las auras van por delante del agua; la niebla es de distancia, los efectos estan al lado).
    // DS184: color FOGCOL (turquesa; leerlo como ARGB la ponia amarilla), remate de cielo continuo con el horizonte y
    // tiras paralelas al horizonte real (la camara balancea hasta ~11 grados).
    // Apagada otra vez por el coordinador (27/10 22:06): sale una banda turquesa OPACA de borde recto que tapa la costa;
    // la del original es suave. DBZ3_IWVUELO_BRUMA=1 la enciende para arreglarla.
    if (static const bool bruma = std::getenv("DBZ3_IWVUELO_BRUMA") != nullptr; bruma) VeloHorizonte(j, cam, io.DisplaySize);
    // solIW7: los efectos fijos pertenecen a los objetos del curso. La suma
    // usa ahora el backend aditivo; no se imita con alfa=brillo.
    {
      static const char* env_fx=std::getenv("DBZ3_IWVUELO_FX");
      static const bool fx=!env_fx||env_fx[0]!='0';
      std::lock_guard<std::mutex> lk(g_mu);
      ImDrawList* bg = ImGui::GetBackgroundDrawList();
      auto pintar=[&](const ame::Instancia& in,const V3& desplazamiento,bool sigue=false) {
        auto mundo=in;
        const float a=Ang(j.yaw),c=std::cos(a),s=std::sin(a);
        auto rotar=[&](V3 v) {return V3{c*v.x+s*v.z,v.y,-s*v.x+c*v.z};};
        if(sigue) mundo.giro_dibujo=a;
        for (auto& lista:mundo.ps) for (auto& p:lista) {
          V3 w=p.p;
          if(sigue) {w=iwame::Mas(rotar(w),Fx::Cuerpo(j));p.v=rotar(p.v);}
          w=iwame::Mas(w,desplazamiento);
          p.p=AB3(j,w.x,w.z);p.p.y+=(w.y-kAltura)*j.escala;
          p.medio*=j.escala;p.v=iwame::Por(p.v,j.escala);
        }
        ame::Pintar(mundo,cam,tx_atlas_,tx_suma_,bg,160.0f*io.DisplaySize.y/720.0f,true);
        const size_t tris=ame::PintarModelos(mundo,cam,j.fx_modelos,tx_atlas_,bg);
        static bool medido=false;
        if(tris&&!medido&&std::getenv("DBZ3_TEST_IWMINI")) {
          medido=true;
          REXLOG_INFO("dbz3 solIW8: vuelo AME modelo: {} triangulos dibujados, {} variantes AMG",tris,j.fx_modelos.size());
        }
      };
      for (const Juego::FxVivo& f : j.fx) {
        if (!f.in||(!fx&&f.id!=0xE&&f.id!=0xA)) continue;
        const int tipo=Fx::TipoObjeto(f.id);
        if (tipo<0) pintar(*f.in,{},f.sigue);
        else for (const Obj& o:j.objs)
          if (o.activo&&(o.tipo&0xFF)==tipo) pintar(*f.in,{o.x,kAltura,o.z});
        if (tipo==0&&j.meta.activo) pintar(*f.in,{j.meta.x,kAltura,j.meta.z});
      }
    }
    // DS68: HUD del vuelo con el sprite del rival (DATA_USA 7/1 obj 0x07/0x08, cabeza t4 Vegeta / t5 Goten); antes
    // se pintaba el HUD generico con la cara del jugador, que no es el del original. DS81: ademas el contador de aros
    // (7/0 t13 + cifras 6/0 t12) y el TIME abajo a la derecha, como el #AMA 7/1. DS83: el total es +0x166f + 1
    // (Minigame_UpdateScoreDigits 0x30ECD0: aros de control del #MSF + la meta = 19 + 1 = 20), no +0x166f.
    // En la intro, en el READY? y en el aterrizaje no hay HUD (el original ensena la escena limpia).
    const bool en_listo = j.listo_t > 0;
    if (!en_intro && !en_listo && !en_cierre)
      HudVuelo(segundos, medidor, j.v, j.n_control - j.restantes, j.n_control + 1, 0);
    if (!en_intro&&!en_cierre&&j.dialogo>=0) DibujarDialogo(io,j);
    // READY? durante la espera tras la intro y START! al empezar el vuelo (como IW; texturas DATA_USA 5/1 t08/t00).
    // DS118: el READY? va DESPUES de la escena (EU 0x33CB80 -> Viewer_StateEndingWait, +0x52c = 90 cuadros) sobre la
    // camara del vuelo, no en el ultimo segundo de la intro; el START! dura los primeros ~2 s (fotogramas del video
    // 32:19-32:21). Antes el READY? se pintaba sobre el ultimo plano del guion.
    DibujarAviso(io,j);
    if (j.en_panel) {
      Resultado r;
      r.clave = j.clave;
      r.fila = j.fila;
      r.superado = j.superado;
      r.centesimas = int(j.t_ms / 10.0f);
      int i = 0;
      while (i < 4 && int(j.t_ms / 1000.0f) >= j.rango_s[i]) ++i;
      r.rango = r.superado ? 4 - i : -2;   // fase 1 (7/11): fallo = sin rango (Viewer_StateResultFail EU 0x33C420: -1)
      r.puntos = r.superado ? j.puntos + j.puntos_rango[i] : j.puntos_fallo;
      r.monedas = j.monedas;
      const auto a = dbz3::ingame::style::GameArea(io);
      const ImVec2 q0 = a.At(0, 0), q1 = a.At(1200, 900);
      DibujarResultadoIW(ImGui::GetForegroundDrawList(), q0.x, q0.y, q1.x - q0.x, q1.y - q0.y, r, j.recompensa.cuadro / 60.0f, false);
    }
    // Al acabar: fundido a negro sobre el final de combate de B3 ("RING OUT!") con CLEAR si llego a la meta, como P4
    bool fin = false, meta = false;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      fin = j.fin;
      meta = j.superado;
    }
    if (!fin) return;
    // El anuncio nativo ya termino ANTES de entregar resultado. Solo se
    // oculta el transito del combate anfitrion, sin fundido0,6s/CLEAR propio.
    if (st>=5) ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0,0),io.DisplaySize,IM_COL32(0,0,0,255));
  }

 private:
  void DibujarAviso(ImGuiIO& io,const Juego& j) {
    DibujarObra(io,j.avisos,aviso_tx_,j.aviso_id,float(j.aviso_t),600,450);
  }
  void DibujarDialogo(ImGuiIO& io,const Juego& j) {
    int t=j.dialogo_t;
    if (std::getenv("DBZ3_TEST_IWMINI"))
      if (const char* fijo=std::getenv("DBZ3_SOLIW7_DIALOGO_CUADRO")) t=std::max(0,std::atoi(fijo));
    // La raiz sin rect aporta el offset del contenedor (936: y175).
    // El muestreador de FX no propaga esa pista A por si solo.
    DibujarObra(io,j.dialogo_ama,dialogo_tx_,0,float(t),600,450,j.dialogo_marco);
    DibujarObra(io,j.dialogo_ama,dialogo_tx_,j.dialogo_salida<0 ? 15 : 16,
        float(j.dialogo_salida<0 ? t : std::max(0,t-j.dialogo_salida)),600,450);
    const auto& tx=dialogo_textos_[j.dialogo-1];
    const int nodo=j.dialogo_ama.Objeto(15);
    const int entrada=nodo>=0 ? int(anim::ama::MaxCuadros(anim::ama::CrearArbol(j.dialogo_ama,nodo))) : 26;
    if (tx&&t>=entrada&&j.dialogo_salida<0) {
      const auto a=dbz3::ingame::style::GameArea(io);
      const float x=j.dialogo_pos[0],y=j.dialogo_pos[1];
      ImGui::GetForegroundDrawList()->AddImage(tx.id(),a.At((x-tx.w*.5f)*2,(y-tx.h*.5f)*2),
          a.At((x+tx.w*.5f)*2,(y+tx.h*.5f)*2));
    }
  }
  void DibujarObra(ImGuiIO& io,const anim::ama::Obra& obra,const std::vector<Tex>& atlas,
      int objeto,float tiempo,float ancho,float alto,V3 desplazamiento={}) {
    const int nodo=obra.Objeto(uint32_t(objeto));
    if (nodo<0) return;
    auto arbol=anim::ama::CrearArbol(obra,nodo,tiempo);
    anim::ama::MarcarArbol(arbol,tiempo); // CrearArbol solo fija el reloj de la raiz
    std::vector<anim::ama::Entrada> entradas;
    anim::ama::Contexto contexto;
    contexto.pos[0]=desplazamiento.x;contexto.pos[1]=desplazamiento.y;
    anim::ama::ActualizarNodo(arbol,arbol.raiz,contexto,entradas);
    const auto area=dbz3::ingame::style::GameArea(io);
    auto at=[&](float x,float y) { return area.At(x*1200/ancho,y*900/alto); };
    ImDrawList* dl=ImGui::GetForegroundDrawList();
    for (const auto& e:entradas) {
      if (e.SinTextura()||e.textura>=atlas.size()||!atlas[e.textura]) continue;
      const auto& tx=atlas[e.textura];
      anim::ama::Quad q;anim::ama::DibujarEntrada(e,q);
      auto color=[&](int i) { return std::clamp(int(e.tinte[i]*255+e.rgba[i]+.5f),0,255); };
      const ImU32 col=IM_COL32(color(0),color(1),color(2),color(3));
      dl->AddImageQuad(tx.id(),at(q.xy[0][0],q.xy[0][1]),at(q.xy[1][0],q.xy[1][1]),at(q.xy[2][0],q.xy[2][1]),at(q.xy[3][0],q.xy[3][1]),
          ImVec2(q.uv[0][0]/tx.w,q.uv[0][1]/tx.h),ImVec2(q.uv[1][0]/tx.w,q.uv[1][1]/tx.h),
          ImVec2(q.uv[2][0]/tx.w,q.uv[2][1]/tx.h),ImVec2(q.uv[3][0]/tx.w,q.uv[3][1]/tx.h),col);
    }
  }
  std::vector<Tex> aviso_tx_;
  std::vector<Tex> dialogo_tx_;
  Tex dialogo_textos_[4];
  const Juego* hecho_ = nullptr;
  std::vector<Tex> tx_aro_, tx_moneda_, tx_casa_, tx_item_[4];
  std::vector<Tex> tx_flecha_;   // DS117: ITEMA04_ARROW (891/1 + #AMT 0)
  std::vector<Tex> tx_atlas_, tx_suma_;   // DS182: atlas del paquete de efectos (891/28 hijo 0)
  ps2::Escena escena_;
  Tex clear_, ready_, start_, goal_;
  float fin_t_ = -1;
};

void CreatePanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer() && rt->graphics_system() && rt->graphics_system()->presenter()) {
    panel = new Panel();
    rt->graphics_system()->presenter()->AddUIDrawerFromUIThread(panel,63);
  }
}

// EU 0x33E830 Viewer_BeginMinigameLoad
// EU 0x33E870 Viewer_BeginMinigameLoadAlt
bool Empezar(int clave, int fila, int dificultad, int personaje) {
  // I03 (studies 4): el vuelo ya se ha validado con el xex y la ISO europeos (0x7D6 y 0x7E0 PASS en EU), asi que
  // esta disponible en las dos regiones. Si la carga falla, Empezar devuelve false y el mapa sigue.
  const bool eu = dbz3::g_guest_eu;
  REXLOG_INFO("dbz3 iwmini vuelo region={} clave 0x{:X}: disponible", eu ? "eu" : "us", clave);
  auto j = std::make_shared<Juego>();
  j->clave = clave;
  j->fila = fila;
  j->v = clave == 0x7E0 ? 1 : 0;
  j->dif = std::clamp(dificultad, 0, 5);
  // Personaje (G+0x4F4, Viewer_BeginMinigameLoad/Alt): 1 si el descriptor de la mision (USA 0x5C05C0) +0x10 == 2 (v0) o
  // == 9 (v1), si no 0. Ese +0x10 lo pone Dragon Mission (USA 0x2FA558) = tabla 0x47D450 [DM+0x374] = el personaje que
  // el jugador lleva en el mapa (0 Goku, 2 Gohan nino, 9 Trunks nino, 10 Krilin...). Solo Gohan nino (v0) o Trunks nino
  // (v1) cambian: con cualquier otro van Krilin (v0) y Gohan (v1). El mapa (pasarela_misiones.cpp) lo pasa con
  // PersonajeDelMapa; DBZ3_IWVUELO_PJ=0/1 (pruebas) lo fuerza.
  const char* pj = std::getenv("DBZ3_IWVUELO_PJ");
  j->pj = pj ? std::clamp(std::atoi(pj), 0, 1) :
      (personaje >= 0 ? personaje : g_pj_mapa.load()) == (j->v == 0 ? 2 : 9) ? 1 : 0;
  constexpr int kJugador[2][2] = {{10, 2}, {4, 9}}, kRival[2] = {7, 6};
  j->jugador = kJugador[j->v][j->pj];
  j->rival = kRival[j->v];
  const char* t = std::getenv("DBZ3_TEST_IWMINI");
  j->auto_ = t && std::strstr(t, "auto");
  j->periodo = kPeriodo[j->v];
  j->escala = kMedio / j->periodo;
  std::string error;
  if (!Cargar(*j, error)) {
    REXLOG_WARN("dbz3 r12: {}", error);
    return false;
  }
  // campo: el de vuelo de IW (883 / 881) si R8 lo instala (coordenadas de IW, escala 1; cajas de +-8000 / +-12000 con
  // paredes y sin vuelta: la vuelta del mundo la hace Volar/AB3, como IW); si no, el campo de IW
  // de R8 mas parecido (900 Namek, 903 llanura, juntando el mundo) y si tampoco, el escenario de B3 (Namek 7, Llanuras 5)
  int campo = dbz3::escenarios::CampoIW(j->v == 0 ? 883 : 881);
  j->vuelo_real = campo >= 0;
  if (j->vuelo_real) j->escala = 1.0f;
  else campo = dbz3::escenarios::CampoIW(j->v == 0 ? 900 : 903);
  j->campo = campo >= 0;
  j->escenario = campo >= 0 ? campo : (j->v == 0 ? 7 : 5);
  REXLOG_INFO("dbz3 r12: clave 0x{:X} (variante {}, dificultad {}): {} contra {}, escenario {} ({}), escala {:.3f}",
              clave, j->v, j->dif, j->jugador, j->rival, j->escenario, j->campo ? "campo de IW de R8" : "B3",
              j->escala);
  // DS101: dato de altura del recorrido frente a la malla del campo (misma tabla de aros que se juega).
  if (j->vuelo_real && j->terreno.vale) {
    float hmin = 1e30f, hmax = -1e30f;
    int con = 0;
    auto mira = [&](float x, float z) {
      const float h = j->terreno.Altura(x, z, -1e30f);
      if (h > -1e29f) hmin = std::min(hmin, h), hmax = std::max(hmax, h), ++con;
    };
    mira(j->sx, j->sz);
    for (int k = 0; k < j->n_control; ++k) mira(j->objs[size_t(k)].x, j->objs[size_t(k)].z);
    mira(j->meta.x, j->meta.z);
    REXLOG_INFO("dbz3 r12: malla del campo {} (IW hijo 5): {} triangulos, x {:.0f}..{:.0f}, z {:.0f}..{:.0f}, y "
                "{:.1f}..{:.1f}; salida+{} aros+meta: {} con malla, superficie {:.1f}..{:.1f}; vuelo y={:.0f} "
                "(holgura {:.1f})",
                j->v == 0 ? 883 : 881, j->terreno.tris.size(), j->terreno.x0, j->terreno.x1, j->terreno.z0,
                j->terreno.z1, j->terreno.y0, j->terreno.y1, j->n_control, con, con ? hmin : 0.0f,
                con ? hmax : 0.0f, kAltura, con ? kAltura - hmax : 0.0f);
  }
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_j = j;
  }
  dbz3::launcher::DeferUi(&CreatePanel);
  if (!dbz3::hub::RequestBattleEx(j->jugador, j->rival, j->escenario, true, -1, 0, 0, 0, 0)) {
    REXLOG_WARN("dbz3 r12: el conector no acepto el combate");
    return false;
  }
  dbz3::combate::SetMuneco(1);       // el rival sin IA ni ataques: lo mueve el medidor
  dbz3::combate::OcultarHud(true);   // sin marcador de B3: HUD de IW
  if (j->vuelo_real) dbz3::suelo_iw::SueloFijo(kAltura, true);   // el luchador a 400 (ver AB3)
  g_corre = true;
  P::Reglas r;
  r.capsulas = false;
  P::VigilarCombate("r12", r, [j](const P::Fin&) {
    g_corre = false;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      Fx::Limpiar(*j);   // DS182: el original libera los 0x15 huecos de efecto al salir (Viewer_RestartMinigame)
    }
    ApagarCamaraVuelo();   // DS132: que el gancho de vista deje de pisar la camara al acabar el vuelo
    // DS117: devolver las banderas de escenario que se encendieron para la niebla del #MCF.
    if (g_fog_flags_guardado) {
      if (uint8_t* base = Base()) REX_STORE_U32(kFogFlags, g_fog_flags_orig);
      g_fog_flags_guardado = false;
    }
    // DS92: ya no se escribe mgr+0x1C4, asi que no hay FOV que devolver (el de B3 queda intacto todo el vuelo).
    dbz3::combate::SetMuneco(-1);
    dbz3::combate::OcultarHud(false);
    dbz3::suelo_iw::SueloFijo(0.0f, false);
    if (uint8_t* base = Base())
      if (const uint32_t mgr = REX_LOAD_U32(kMgrPtr)) {
        dbz3::anim::Scatto(mgr + kX0, dbz3::anim::kApagado);
        dbz3::anim::Scatto(mgr + kX0 + kXPaso, dbz3::anim::kApagado);
        dbz3::anim::Bucle(mgr + kX0, 0);
        dbz3::anim::Bucle(mgr + kX0 + kXPaso, 0);
      }
    Resultado res;
    // EU 0x3675F0 MgFly_FinishAndApplyResult: bloque de resultado tipo 2, segundos = +0x4dc, rango = 4 - +0x4e0 (indice
    // del primer umbral de +0x38 que supera el tiempo) y puntos = tabla de premios[+0x4e0]; EU 0x33C420
    // Viewer_StateResultFail deja rango -1 (aqui 0 = no superado) y los puntos de fallo (+0x60). `coincide`.
    {
      std::lock_guard<std::mutex> lk(g_mu);
      res.clave = j->clave;
      res.fila = j->fila;
      res.superado = j->fin && j->superado;
      res.centesimas = int(j->t_ms / 10.0f);
      int i = 0;
      while (i < 4 && int(j->t_ms / 1000.0f) >= j->rango_s[i]) ++i;   // Viewer_StateResultSuccess
      res.rango = res.superado ? 4 - i : -2;   // fallo = sin rango (EU 0x33C420 pone -1; aqui -2: el -1 es "por puntos")
      res.puntos = res.superado ? j->puntos + j->puntos_rango[i] : j->puntos_fallo;
      res.monedas = j->monedas;
    }
    REXLOG_INFO("dbz3 r12: resultado superado {} rango {} puntos {} tiempo {} cs", res.superado, res.rango >= 0 ? "CBASZ"[res.rango] : '-',
                res.puntos, res.centesimas);
    Terminar(res);
  });
  return true;
}

const bool g_registro = (RegistrarModulo(0x7D6, &Empezar), RegistrarModulo(0x7E0, &Empezar), true);

}  // namespace
}  // namespace dbz3::iwmini

// El resto de profundidad 1 del visor (poses, sprites y cutscenes de la PS2) esta en
// MAPAS_CODIGO/SUSTITUIDAS_IW_MINIJUEGOS.txt, con el archivo:funcion del port que lo cubre.
// ---- iw_mini_vuelo.h: piezas del motor de vuelo para otros minijuegos (R13) ----
namespace dbz3::iwvuelo {
namespace {
uint32_t U32(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
  return v;
}
float F32(const std::vector<uint8_t>& b, size_t o) {
  const uint32_t u = U32(b, o);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
}  // namespace

bool LeerMsf(const std::vector<uint8_t>& m, int dificultad, Msf& j, std::string& error) {
  if (m.size() < 0x200 || std::memcmp(m.data(), "#MSF", 4) != 0) return error = "no es un #MSF", false;
  j.hover = F32(m, 0x14);
  for (int k = 0; k < 3; ++k) {
    j.empuje[k] = F32(m, 0x18 + 4 * size_t(k));
    j.freno[k] = F32(m, 0x34 + 4 * size_t(k));
    j.giro[k] = int(U32(m, 0x4C + 4 * size_t(k)));
  }
  j.vmin = F32(m, 0x24);
  j.vfreno = F32(m, 0x28);
  j.giro0 = int(U32(m, 0x44));
  j.dur2 = F32(m, 0x58);
  j.dur3 = F32(m, 0x5C);
  for (int k = 0; k < 16; ++k) j.radio[k] = F32(m, 0x60 + 4 * size_t(k));
  const std::vector<uint32_t> bl = Bloques(m, 0xA8);
  const size_t b = bl[size_t(std::clamp(dificultad, 0, 5))];
  if (!b || b + 0x64 > m.size()) return error = "bloque de dificultad fuera del #MSF", false;
  const size_t o_sal = U32(m, b), o_meta = U32(m, b + 4), o_l1 = U32(m, b + 8), o_l2 = U32(m, b + 12);
  j.medidor0 = F32(m, b + 0x14);
  for (int k = 0; k < 3; ++k) j.sube[k] = F32(m, b + 0x18 + 4 * size_t(k));
  j.congela = F32(m, b + 0x24);
  j.reaparece = F32(m, b + 0x28);
  for (int k = 0; k < 3; ++k) j.puntos_moneda[k] = int(U32(m, b + 0x2C + 4 * size_t(k)));
  for (int k = 0; k < 5; ++k) {
    j.rango_s[k] = int(U32(m, b + 0x38 + 4 * size_t(k)));
    j.puntos_rango[k] = int(U32(m, b + 0x4C + 4 * size_t(k)));
  }
  j.puntos_fallo = int(U32(m, b + 0x60));
  j.salida_estado = int(U32(m, o_sal));
  j.salida_giro = int(U32(m, o_sal + 4));
  j.sx = F32(m, o_sal + 8);
  j.sz = F32(m, o_sal + 12);
  auto obj = [&](size_t o) {
    Objeto x;
    x.flags = int(U32(m, o) & 0xFFFF);
    x.tipo = int(U32(m, o) >> 16);
    x.x = F32(m, o + 4);
    x.z = F32(m, o + 8);
    x.r = j.radio[x.tipo & 0xF];
    return x;
  };
  j.meta = obj(o_meta);
  j.lista.clear();
  for (size_t o : {o_l1, o_l2}) {
    const uint32_t n = U32(m, o);
    if (n > 256 || o + 4 + 12 * size_t(n) > m.size()) return error = "lista de objetos rota en el #MSF", false;
    for (uint32_t k = 0; k < n; ++k) j.lista.push_back(obj(o + 4 + 12 * size_t(k)));
    if (o == o_l1) j.n_control = int(n);
  }
  return true;
}

std::vector<uint32_t> Bloques(const std::vector<uint8_t>& g, uint32_t en, int n) {
  std::vector<uint32_t> v;
  for (int k = 0; k < n; ++k) {
    const uint32_t o = U32(g, en + 4 * size_t(k));
    v.push_back(o < g.size() ? o : 0);
  }
  return v;
}

float EjeIW(int v) { return v > 64 ? float(v - 64) / 64.0f : v < -64 ? float(v + 64) / 64.0f : 0.0f; }

float Envolver(float d, float periodo) {
  d = std::fmod(d + periodo * 0.5f, periodo);
  if (d < 0) d += periodo;
  return d - periodo * 0.5f;
}

// EU 0x339AA0 CameraFocusWhileFlying
void CamaraVuelo(const float p[3], int yaw, float ojo[3], float obj[3], float alto, float dist, int cabeceo) {
  const float a = float(yaw & 0xFFFF) / 65536.0f * 6.2831853f, c = -float(cabeceo) / 65536.0f * 6.2831853f;
  ojo[0] = p[0] - std::sin(a) * dist * std::cos(c);
  ojo[1] = p[1] + alto + dist * std::sin(c);
  ojo[2] = p[2] - std::cos(a) * dist * std::cos(c);
  obj[0] = p[0];
  obj[1] = p[1] + alto;
  obj[2] = p[2];
}

bool DelanteDeCamara(uint8_t* base, float adelante, float abajo, float out[3]) {
  float ojo[3], mira[3];
  if (!dbz3::cine::LeerCamara(base, ojo, mira)) return false;
  float fx = mira[0] - ojo[0], fy = mira[1] - ojo[1], fz = mira[2] - ojo[2];
  const float fl = std::max(std::sqrt(fx * fx + fy * fy + fz * fz), 1e-3f);
  fx /= fl, fy /= fl, fz /= fl;
  const float rl = std::max(std::hypot(fz, fx), 1e-3f), rx = -fz / rl, rz = fx / rl;   // derecha = f x arriba
  const float ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;                     // arriba = derecha x f
  out[0] = ojo[0] + fx * adelante - ux * abajo;
  out[1] = ojo[1] + fy * adelante - uy * abajo;
  out[2] = ojo[2] + fz * adelante - uz * abajo;
  return true;
}

bool Proyectar(uint8_t* base, float ancho, float alto, const float p[3], float& x, float& y, float& k) {
  // DS137: con la camara del cuadro que B3 pinta (la publica el gancho de sub_820F4300), no con mgr+0x60 (la pedida,
  // que el gancho reescribe y que da los pies ~80-95 px por debajo de donde se dibuja al luchador).
  ps2::Camara cam;
  if (!dbz3::iwmini::CamaraDelRender(base, ancho, alto, cam)) cam = ps2::CamaraCombate(base, ImVec2(ancho, alto));
  ImVec2 o;
  if (!cam.ok || !cam.P(ps2::V3{p[0], p[1], p[2]}, o, k)) return false;
  x = o.x;
  y = o.y;
  return true;
}

}  // namespace dbz3::iwvuelo

namespace dbz3::iwvuelo {
void PersonajeDelMapa(int id_iw) { dbz3::iwmini::g_pj_mapa = id_iw; }
}  // namespace dbz3::iwvuelo

// DS132: gancho de dibujo del juego (US). sub_820F4300 rellena la vista que usa sub_820C1418 en el mismo cuadro; al
// volver se reescribe SOLO esa vista con la camara del vuelo, anulando de hecho la mezcla 0,8/0,2 de B3 sin tocar su
// entrada (+0x190/+0x1A0). Es la misma receta medida en cinematica.cpp A3b (gancho tras la camara del juego), pero en
// el ultimo punto antes del dibujo y sin definir otra vez sub_820F47E0 (que ya engancha cinematica.cpp).
// La pareja EU se verifico contra el cuerpo PPC completo y sus llamadores.
// Pareja verificada:28 instrucciones PPC, mismos offsets264/250 y llamada82082A08;
// EUgenerated_eu/dbz3_eu_recomp.42.cpp:6319..6389.
DBZ3_HOOK(sub_820F4300,dbz3eu_sub_820F43F0) {
  dbz3::escenarios::AplicarCamaraB2AntesDelDibujo(base); // fuente activa B2, sin modificar las cámaras IW
  // DS163: la camara libre escribe la struct que copia sub_820F4300 ANTES del original (es la ultima escritura
  // que ve el dibujo; escribir despues solo cambia mgr+0x60, que ya no es la camara del render).
  dbz3::freecam::AplicarAntesDelDibujo(base);
  // DS177: la camara del minijuego de campo se escribe en la struct activa (la fuente del dibujo; DS163), justo antes
  // del original, que es el ultimo punto que ve el render.
  dbz3::iwmini::AplicarCamaraMinijuegoAntes(base);
  orig(ctx, base);
  dbz3::iwmini::AplicarCamaraVueloTrasB3(base);
  // DS177: foto del cuadro que el juego acaba de dibujar (vista + fov real) para que la escena 3D de la interfaz use
  // EXACTAMENTE la misma camara: aureola, platanos, monedas y polvo caen sobre lo que pinta B3.
  dbz3::iwmini::PublicarCamaraRender(base);
  dbz3::freecam::TrasLaVista(base);   // DS163: reafirma la struct y deja mgr+0x60 para los overlays
}
