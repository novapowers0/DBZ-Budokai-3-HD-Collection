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

#include <rex/cvar.h>
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
#include <deque>
#include <map>
#include <unordered_map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <cctype>
#include <functional>

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
namespace dbz3::hub { bool RequestEditSkills(); }   // hub.cpp: abre el Edit Skills nativo (pantalla 22)

namespace dbz3::pasarela::b2 {
namespace {

namespace T = dbz3::i18n;
namespace S = dbz3::ingame::style;

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kBack = 0x0020,
                   kLB = 0x0100, kRB = 0x0200, kRT = 0x0400, kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
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

// Tabla de personajes de B2 (DS23): 53 registros de 712 B (0x2C8) en el ELF, VA 0x389540 -> fichero 0x289540; el
// nombre va en +0x80 del registro (ROMaji: GOKOU, KULILIN, FREEZA...). Los 25 primeros son el orden de B1; del 25 en
// adelante estan los de B2 (Dr. Gero, Goten, Bu, fusiones...). La tabla completa se lee de ahi, no se adivina.
constexpr int kB2Personajes = 53;
constexpr int kB2aB3[kB2Personajes] = {
    // 0-24: mismo orden que B1 (kB1aB3); se escribe entero para que la tabla sea la unica fuente.
    0, 2, 3, 7, 10, 8, 11, 12, 13, 18, 19, 20, 21, 38, 19, 27, 28, 29, 30, 32, 33, 14, 42, 43, 5,
    // 25 Dr.GERO -> 32; 26 GOTEN -> 6; 27 TRUNKS S -> 9; 28 GOHAN L -> 4; 29 BUU L -> 34; 30 BUU M -> 35;
    // 31 BUU S -> 36; 32 DABURA -> 37; 33 VIEDL -> 15; 34 M VEGETA -> 7 (Vegeta); 35 KAIO-SHIN -> 16;
    // 36 GOTENKS -> 64 (forma 3); 37 YAMHAN -> 12 (no existe en B3: Tien); 38 VEGITO (GOKOU) -> 72 (forma 2);
    // 39 GOTAN (GOKOU) -> 0 (no existe en B3: Goku); 40 KIBITO-SHIN -> 16 (forma 1); 41 VEGITO (VEGETA) -> 72;
    // 42 GOTAN (SATAN) -> 0; 43 KIBITO -> 16; 44 BABIDI -> 16; 45 GOTENKS L -> 64; 46 YAMHAN S -> 12;
    // 47-52 formas de Bu -> 35 (el traje del absorbido lo pone TrajeAbsorcionB2, ver Combate()).
    32, 6, 9, 4, 34, 35, 36, 37, 15, 7, 16, 64, 12, 72, 0, 16, 72, 0, 16, 16, 64, 12, 35, 35, 35, 35, 35, 35};
int B2aB3(int c) { return c >= 0 && c < kB2Personajes ? kB2aB3[c] : -1; }

// Nombre del registro de B2 (misma tabla). Solo se usa para los personajes que NO existen en B3 (Zarbon, Dodoria,
// Yamhan, Gotan, Babidi, Kibito, Gotenks gordo y las absorciones de Bu): los demas conservan el nombre de B3.
const std::string& NombreB2(int c) {
  static std::array<std::string, kB2Personajes> n;
  static bool leido = false;
  if (!leido) {
    leido = true;
    const auto& e = dbz3::pasarela::nave::ElfB2();
    for (int i = 0; i < kB2Personajes; ++i) {
      const size_t o = 0x2895C0 + size_t(i) * 0x2C8;   // 0x389540 - 0x100000 + 0x80
      if (o + 32 <= e.size()) {
        const char* p = reinterpret_cast<const char*>(&e[o]);
        n[size_t(i)].assign(p, strnlen(p, 32));
      }
    }
  }
  static const std::string vacio;
  return c >= 0 && c < kB2Personajes ? n[size_t(c)] : vacio;
}
bool SoloB2(int c) {   // el personaje no tiene equivalente en B3 (tabla de 44 + fusiones 64/70/72)
  switch (c) {
    case 13: case 14: case 37: case 39: case 42: case 43: case 44: case 45: case 46:
    case 47: case 48: case 49: case 50: case 51: case 52: return true;
  }
  return false;
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
  // Super Bu: las absorciones de B2 (tabla de personajes del ELF 0x2895C0) son c47 BUU (GOTENKS), c48 BUU (GOHAN L), c50
  // BUU (CELL) y c51 BUU (FREEZA). En B3 (tabla de transformaciones, docs/07_ports/GOHAN_FUTURO_EXPLORACION_2026-10-06/
  // 04_transformaciones/evid_tabla_b3.md) el 35 tiene forma 1 = modelo 84 Piccolo, 2 = 78 Gotenks y 3 = 79 Gohan; Cell y
  // Freezer no existen. Medido 2026-10-25: pedir el combate nativo con el 35 en forma 2 (Bu Gotenks) CRASHEA el juego al
  // cargar (0xC0000005, corrida 20261025-040549_ds21_fase8_bu47), igual que la forma 3 en pasarela_iw.cpp.
  // DS23: la via que funciona es la de los trajes (DS21 bloque 6): el 35 entra en forma 0 con el traje del absorbido
  // (TrajeAbsorcionB2). La forma inicial sigue siendo 0; no se depende del guard para elegir el modelo.
  if (id_b3 == 35) return 0;
  return forma_b2 > 0 ? FormaB3(id_b3, forma_b2) : 0;
}

// DS23, absorciones de Bu (c47/48/50/51 -> id 35 + traje del absorbido). Los mods de traje estan en
// mod center hd/campanas/trajes_bu/: Gotenks (modelo 107) y Gohan (104) existen; B3 no tiene Bu con Cell ni con
// Freezer, asi que esos dos devuelven -1 (se queda el traje 0, Bu de siempre) hasta que haya mod.
int TrajeAbsorcionB2(int c) {
  const char* carpeta = c == 47 ? "bu_absorbido_gotenks"
                        : c == 48 ? "bu_absorbido_gohan"
                        : c == 50 ? "bu_absorbido_cell"
                        : c == 51 ? "bu_absorbido_freeza"
                                  : nullptr;
  if (!carpeta) return -1;
  return dbz3::pasarela::TrajeExtra(35, carpeta);
}

// EU 0x273290 FUN_00273290 (las 4 vecinas de una casilla: +8..+0xE del registro de 0x34 B)
// EU 0x273A90 FUN_00273a90 (x, y del centro de la casilla: +0 y +4 del mismo registro)
struct Casilla {
  float x = 0, y = 0;
  int vec[4] = {};
  int tipo = 0, destino = 0, condicion = 0;   // ver T1 arriba
  int escenario = 0;                          // +0x18: escenario de B2 del combate (0-12)
  int musica = -1;                            // +0x1F: pista del combate de la casilla (M4): 17, 1, 4, 6, 20, 21,
                                              // 7, 15, 22 por fase I-IX; indice de la lista de PistaCombateB2
  bool lucha = false;
};
// EU 0x1B2790 FUN_001b2790
// Escenario de B2 (T1+0x18 -> 0x699391 -> FUN_001b2790) -> grupo de DATA_CMN que carga (tabla del ELF 0x372AB0 + 0x9C * i:
// +0 #MDD 8g+7, +4 #MAD, +8 #MAS; 9-12 = 0, 1, 4, 5 en su variante 1) -> carpeta del alta que monta mod center hd/campos_iw.py
// (B12) desde la ISO del jugador. Sin carpeta, el escenario de B3 de la fase (kMapaB3).
// DS127: el grupo 5 (el de la fase VII) NO se instala y se usa la entrada nativa 58 (kMapaB3[6]). Medido sobre la ISO y el
// data_cmn del jugador: el bin nativo 58 contiene las MISMAS 16 texturas que el #AMT del grupo 5 (permutacion exacta por
// media RGBA/alfa: 0->1, 1->2, 2->3, 3->4, 4->5, 5->6, 6->7, 7->8, 8->9, 9->10, 10->11, 11->12, 12->0, 13->13, 14->14,
// 15->15) y su region #AWO mide 413016 B frente a 413760 B del convertido: es la misma arena (Camara del Tiempo). El alta
// convertida del grupo 5 se probo y pinta el suelo/mar de nubes NEGRO (86 % de la mitad inferior frente a 0 % del nativo;
// X1esc §2-4): sus capas 9/10/14 son blancas con alfa 1-4 y el original las mezcla de forma aditiva, que B3 no reproduce con
// el material convertido. Instalarla empeoraria la fidelidad, asi que se mantiene el nativo 58, que ya es esa misma arena.
const char* const kEscenarioB2[13] = {"Llanura B2", "Calle de la ciudad B2", nullptr /* g5: nativo B3 58, misma arena */, "Torneo B2" /* g1, entrada 5001 de B12 */,
                                      "Base Red Ribbon B2", "Montanas nevadas", "Templo de Kami", "Islas B2", "Namek B2",
                                      "Llanura B2", "Calle de la ciudad B2", "Base Red Ribbon B2", "Montanas nevadas"};
struct Objeto {
  int casilla = 0, tipo = 0, valor = 0;
  bool visible = false, enterrado = false, hecho = false;
  std::vector<int> banderas;   // las que pide (T8 +8); vacia = ninguna
};
// DS45: aqui habia una lista de marcas EU 0x... (0x277890/77A90/77AF0/77670 estados de dibujo, 0x278D40/78E20/78D60/784B0
// accesos de casilla y hueco, 0x279030/27A0D0/279FD0/276CF0 personaje y vidas, 0x2769E0/2768F0 copia y guardado de T2) que
// no estaba sobre el codigo que hace cada cosa. Las que el port SI modela quedan marcadas en su sitio (Cargar, Empezar,
// EmpezarPaso, RecalcularSlots, Orden 53/54, Coger); los estados de dibujo 0xB/0xF/0xC no se portaron y no llevan marca.
// Escenas de B2 y quien las sustituye en el port ( EU 0x232E10 FUN_00232E10(escena, dato) fija la siguiente; el administrador de escenas EU 0x231210 / EU 0x231800 / EU 0x231C90 (esperan a la carga y pasan a la tarea de la escena) y EU 0x232030 FUN_00232030 (la
// arranca: 3 combate 0x27EC80, 5 menu 0x2A5FB0, 6 Mundo Dragon 0x238870, 0xE Dueling 0x247E20, 0x11 Training 0x261540, 0x14 Torneo
// 0x242A30, 0x18 nave 0x265F20, 0x1B Edit Skills 0x287240, 0x1C Options 0x25E420). Indices del menu principal -> escena (b2.c:194000):
// 0 Dragon World = 5 (partida nueva) o 6, 1 = 0xE, 2 = 0x14, 3 = 0x11, 4 = 0x1B, 5 = 0x1C, 6 = 0x18 (nave, si esta abierta).
// El port no tiene administrador de escenas: el hub entra al tablero (b2::Entrar -> Dibujar) y a la nave (nave::Entrar -> Dibujar), y la
// vuelta del combate es `Resultado`. 0x232E40 / 0x232E50 habilitan el reinicio por pad (Select+Start -> FUN_0022ED40(3)),
// ya traducido con Back+Start en DibujarTablero. Las escenas de menu de B2 Torneo 0x242A30, Dueling 0x247E20, Options
// 0x25E420 y Training 0x261540 [H] no tienen pantalla propia en el port: el hub entra directo al Dragon World y a la nave,
// y ofrece sus equivalentes de B3 (Torneo de 4, Duelo, Opciones y Entrenamiento). Portarlas exige la maquina de escenas
// completa (0x231210/0x231800/0x231C90/0x232030) y sus recursos; no se inventan aqui.
// Registro de pieza del original (0x69B7A0 + 0x24 n, runtime; FUN_002769E0 copia aqui los 32 registros de T2 y FUN_002768F0 los
// guarda en un buffer = `Cargar`/`Preparar` y el reinicio de fase) y sus accesos, leidos uno a uno y puestos en el campo del port:
//   +0 estado de dibujo (0 quieta, 2 anda, 4/0xB/0xC/0xE/0xF..: ver las ordenes 58-62 y la reaccion tras el combate)
//   +1 vidas (barras - 1; EU 0x279F60 +1 hasta 5 EU 0x279FD0 -1 sin bajar de 1 EU 0x27A0D0 vale 1 si == 0) -> Ficha::barras
//   +2 / +3 movimientos / por turno del jugador, o acciones / tipo de movimiento del rival ( EU 0x278F90 EU 0x278EE0)
//   +4 personaje ( EU 0x279030 FUN_00279030) -> Ficha::c
//   +6 u16: & 0xF estado (0-1 fuera, 2 en juego; EU 0x27A090), & 0xF0 bando (0x10 jugador, 0x20 rival EU 0x278B00), & 0x100 bandera
//   +8 casilla actual y +0xA destino ( EU 0x278E20 FUN_00278E20 EU 0x278D60 FUN_00278D60 EU 0x2784B0 = han cambiado) -> `desde` y
//      `casilla` durante el paso; +0xC hueco y +0xD hueco destino ( EU 0x278D40 FUN_00278D40) -> Ficha::slot / slot_ant (el original tiene 6
//      huecos con desplazamientos (0,0), (+60,-20), (-60,-20), (-60,+20), (+60,+20), (0,+30) en 0x2739A8-0x273A80; solo se escriben 0 y 1
//      (FUN_00278A30), el port usa esos dos)
//   +0x10 ataque, +0x12 defensa ( EU 0x276E10, 0x276DE0), +0x14 vida ( EU 0x276CF0 FUN_00276CF0), +0x16 ki, +0x18 COM, +0x19 forma
//   +0x1A traje, +0x1C anim, +0x1F vida / 1000 ( EU 0x279F60)
// Anims del estado visual (DS53): EU 0x277890 (contacto: 0xB + anim 2), EU 0x277BB0 (perdedor tras el combate: 7/5
// golpeado u 8/4 KO), EU 0x277AF0 (inicio de la busqueda: anim 4) + EU 0x277860 (estado 0xC), EU 0x277A90 (rival enterrado
// encontrado: 0xB + anim 8), EU 0x277A30 (objeto sacado al buscar: 4 + anim 10), EU 0x277B50 (objeto recogido andando:
// 0xE + anim 9), EU 0x277670 (0xF). Todos pasan por `anim_estado`/`anim_b2` y la tabla 0x379010 de DibFicha3D/DibGoku3D
// (fichas CMN 334, Goku CMN 336). 0x232E40 / 0x232E50 (uRam006937a8 = 0 / 1) activan el reinicio por pad (FUN_002323D0: Select+Start ->
// FUN_0022ED40(3)); el port lo hace en DibujarTablero con Back+Start (misma pareja en XInput). 0x278E40 (funde el +0x64 de
// las piezas: 0 normal, 0,5 atenuada, 0,1 por cuadro) ya se dibuja (FundirPiezas/PasoFundido y el color de DibPieza); el
// +0x8A del registro visual (0 / 1 jugador / 2 bandera / 6 estado) sigue [H] sin lector identificado.
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
  // bloque. Se guardan como indices de B2; la forma se traduce con FormaTableroB3 y las absorciones de Bu (c47/48/50/51)
  // con TrajeAbsorcionB2 (DS23: id 35 + traje del absorbido; el consumidor debe aplicar el traje, ver INFORME).
  int forma_b2 = -1;              // orden 52: +0x19 (byte; a1 del guion)
  int traje_b2 = -1;              // orden 52: +0x1A (word; a0 del guion)
  int com_b2 = -1;                // +0x18 (byte; COM inicial del T2 +5, orden 51 lo cambia; va al COM del rival, 0x699365)
  // studies 2 G04 (ordenes 65/66, FUN_002774F0): la pieza va a un punto (x, y) del mapa a 4 px/cuadro sin tocar su
  // casilla logica. 0 = quieta, 1 = andando, 2 = llego (se queda en el punto hasta otra orden de colocacion).
  int punto = 0, punto_orden = 0;
  float punto_x = 0, punto_y = 0, punto_x0 = 0, punto_y0 = 0, punto_t0 = 0, punto_dur = 0;
  int n = 0;                      // indice de la pieza en el #YDW (el que usan los scripts)
  int mov = -1, mov_p = 0;        // tipo de movimiento (T2 +3/+4, o el que pone la orden 50) y su dato
  int modo = 0;                   // Saibaman: fila de sprites del #AMA (T2 +0x07 -> +0x1E; FUN_002791a0 b2.c:223478)
  int desde = 0;
  float corta = 0;                // px que se queda corto del centro de la casilla de destino (contacto: 60, FUN_002738A0); 0 = llega
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
  int acc_turno = 1;  // rival: +3 del registro (acciones por turno; 0x278EE0 las recarga)
  int acciones = 0;   // rival: acciones que le quedan en la fase enemiga (+2 del registro; 0x278EE0 las recarga a 1)
  int estado0 = 0;                // +5 del registro T2: COM inicial (FUN_002791A0 lo copia a +0x18; se cambio con la orden 51)
  float brote_t = -10;            // cuando aparecio (brota del suelo o se materializa)
  // columna de vida (FUN_00274210, b2.c:220302): +0x78 barras que se ven (van hacia `barras` de una en una, al acabar la
  // anim 0x36 ganar / 0x37 perder del #AMA 397) y +0x7C su temporizador (aqui, cuando empezo; < 0 = sin empezar)
  mutable int barras_vis = -1;
  mutable float vida_t0 = -1;
  // EU 0x278E40 FUN_00278e40: +0x64 del registro visual (0 = normal, 0,5 = atenuada). Se mueve 0,1 por cuadro hacia
  // `opac_obj`; FUN_00278E40(0,5, pieza) atenua las demas al elegir/contactar y FUN_00278E40(0, ...) las devuelve (T5).
  float opac = 0.0f, opac_obj = 0.0f;
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
  std::vector<Imagen> mpt_texturas;         // DATA_CMN 791 (#AMT), recursos de las escenas #SPX M/P/T
  std::vector<Sprite> mpt_ama;              // DATA_CMN 790 (#AMA), ventana de las escenas M/P/T
  std::vector<Sprite> shen_ama;            // Shenron: disposicion de su ventana (CMN 849)
  // Marcador y ventanas del tablero (ELF 0x2350C0: fijos CMN 402 = #AMA y DATA_<idioma> 188 = sus texturas, los
  // uRam00698dc8/db0 de las tareas D_TURN, D_RADAR, D_RADARBASE, D_CLEAR, D_DBGET, DBALL y del menu DWU_MMAIN/DWU_YN*):
  // 0-2 barra de madera, 3-5 "MUNDO DRAGON", 6-15 Bolas, 16-24 radar, 38-50 rombo del turno y cifras, 51-74 ventana de
  // mensaje (8 piezas: entrar 0x33+3k, reposo +1, salir +2), 87-89 flecha, punto y brillo del radar, 97-120 menu y Si/No.
  std::vector<Imagen> hud;
  std::vector<Sprite> hud_ama;
  std::vector<Sprite> life_ama;   // CMN 397 (uRam00698dcc): 0x34 celda vacia, 0x35 llena, 0x36 ganar (81), 0x37 perder (41)
  std::vector<Imagen> aliados;
  std::vector<Imagen> ayuda;         // DATA_<idioma> 1145: tiras de la barra de ayuda del original (texto+boton ya pintados)
  std::vector<Sprite> aliados_ama;   // B2 EU: tabla 0x3A4010 = DATA_<idioma> 184 + CMN 399 (NO 402)
  // s9: tarjeta "Battle!" que sale tras el contacto: DATA_<idioma> 190 (12 texturas: pergamino, titulo...) + 189 (los 6 textos
  // de condicion, uno por T1+0x19) y la disposicion en CMN 408 (7 = pergamino, 6 = titulo; CMN 410 = pose del texto)
  std::vector<Imagen> tarjeta, tarjeta_cond;
  std::vector<Sprite> tarjeta_ama;
  struct Adorno {
    int obj = 0, tipo = 0;
    float x = 0, y = 0;
    std::vector<int> conds;   // +0x10 cuantas, +0x14 banderas: n > 0 pide la bandera n puesta, -n pide la n apagada (0x238AF0)
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
// ponen. El original NO las quita por tiempo: se quedan hasta que el jugador pulsa X (la orden 39 que sigue al 38 en los
// 9 #YDW; medido 14 s en pantalla en fase I, LUNA17/INFORME.md bloque 7) y la tarea D_CLEAR traza sus piezas mientras
// tanto. El corte a los 3 s queda SOLO como respaldo del modo automatico de las pruebas (auto_on).
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
// Ventana de zenie del objeto tipo 8 (0x27A980 secuencia 0xFFFC): FUN_00254720(0xD001, 0) la crea y FUN_00254620(valor*100, 8) le da el
// importe y el paso. Tarea 0x254460 (carga) y dibujo 0x253F80: el contador (monedero 0x6937F8, tope 99.999.900) suma `paso` (8) por cuadro
// de 60 Hz mientras queda importe, con el sonido 0xE cada 6 cuadros y el 0xF al acabar; luego se mantiene 30 unidades de dt (0,5 s) y se
// cierra. [H]: el monedero del juego (0x6937F8) no es el del HUB: el contador parte de g_zenie (lo ganado en la sesion); la posicion y el
// aspecto del panel (sprites 6..15 digitos, 16/17 separador del AMA iGpffff8afc) no se han medido: panel de ImGui.
struct VentanaZenie {
  bool abierta = false;
  int total = 0, resto = 0, paso = 8, cuadro = 0, tics = 0, estado = 0;   // estado 5 contando, 3 mantenida, 4 cerrando
  float t_ini = 0, t_cuenta = 0, t_fin = 0;
} g_vz;
void SonidoCanal3(int id);
struct Subida {
  int n = 0, signo = 1, valor = 0;
  bool defensa = false, sonado = false;
  float t0 = 0;
  // Objetos del suelo ( EU 0x27A980 FUN_0027A980, lista de animaciones por tipo en 0x379190 + 0x3791D0): `an0`/`nan` = animaciones del
  // #AMA 397 que se dibujan (-1 = las de ataque/defensa de siempre), `sin_cifra` = sin el "N %" de FUN_0027BAB0, sonidos a los 60
  // (0xD jugador / 0xA rival) y a los 110 cuadros (por tipo).
  int an0 = -1, nan = 4, snd60 = 0, snd110 = 0;
  bool sin_cifra = false, son60 = false;
  int AnBase() const { return an0 >= 0 ? an0 : (defensa ? 120 : 116); }
  int Nan() const { return signo ? (an0 >= 0 ? nan : 4) : 0; }
};
std::vector<Subida> g_subidas;
struct SonidoDif {
  float t;
  int id;
};
std::vector<SonidoDif> g_son_dif;   // sonidos del canal 3 programados (objetos recogidos)
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
// EU 0x27D110 FUN_0027d110 (restaura las tablas 0x69BC20 = buscado, 0x69BC60 = casilla por objeto y 0x69BD38 = Bolas desde
// un guardado intermedio; las salva 0x27D010 dentro de 0x238320). Antes el port no reanudaba una fase a medias: Empezar
// volvia a colocar todo. Ahora se guarda al cerrar cada turno y se restaura al entrar otra vez en la misma fase.
// Copia en RAM de las 32 piezas (FUN_002768F0): el original la usa para reanudar la fase con el mismo estado de vida,
// barras, casillas y acciones. No va a disco: se pierde al reiniciar, igual que el buffer del juego.
std::vector<Ficha> g_snap_piezas;
void BorrarEstadoFase() {
  g_snap_piezas.clear();
  GuardarProgreso("tablero_b2_snap_fase", -1);
  GuardarProgreso("tablero_b2_snap_n", 0);
  GuardarProgreso("tablero_b2_snap_nb", 0);
  GuardarProgreso("tablero_b2_snap_nbus", 0);
}
void GuardarEstadoFase() {
  if (g_d.fase < 0) return;
  // EU 0x2768F0 FUN_002768f0 (guarda las 32 piezas del registro 0x69B7A0 para reanudar la fase)
  g_snap_piezas = g_f;
  GuardarProgreso("tablero_b2_snap_fase", g_d.fase);
  GuardarProgreso("tablero_b2_snap_n", int(g_obj.size()));
  for (size_t i = 0; i < g_obj.size(); ++i) {
    const std::string k = "tablero_b2_snap_o" + std::to_string(i);
    GuardarProgreso(k + "_c", g_obj[i].casilla);
    GuardarProgreso(k + "_v", g_obj[i].visible ? 1 : 0);
    GuardarProgreso(k + "_e", g_obj[i].enterrado ? 1 : 0);
    GuardarProgreso(k + "_h", g_obj[i].hecho ? 1 : 0);
  }
  GuardarProgreso("tablero_b2_snap_nbus", int(g_buscada.size()));
  for (size_t i = 0; i < g_buscada.size(); i += 32) {
    uint32_t w = 0;
    for (size_t b = 0; b < 32 && i + b < g_buscada.size(); ++b) w |= uint32_t(g_buscada[i + b]) << b;
    GuardarProgreso("tablero_b2_snap_b" + std::to_string(i / 32), int(w));
  }
  GuardarProgreso("tablero_b2_snap_bolas", g_bolas);
  GuardarProgreso("tablero_b2_snap_nb", int(g_bolas_fase.size()));
  for (size_t i = 0; i < g_bolas_fase.size(); ++i) {
    GuardarProgreso("tablero_b2_snap_bola" + std::to_string(i) + "_c", g_bolas_fase[i][0]);
    GuardarProgreso("tablero_b2_snap_bola" + std::to_string(i) + "_n", g_bolas_fase[i][1]);
  }
}
bool RestaurarEstadoFase() {
  if (g_d.fase < 0 || LeerProgreso("tablero_b2_snap_fase", -1) != g_d.fase) return false;
  if (LeerProgreso("tablero_b2_snap_n", -1) != int(g_obj.size())) return false;
  if (g_snap_piezas.size() == g_f.size()) {   // EU 0x2768F0/0x27D110: mismas piezas que al cerrar la accion
    g_f = g_snap_piezas;
    for (Ficha& f : g_f) {   // los campos de animacion/posicion en curso no cruzan la reanudacion
      f.punto = 0; f.punto_orden = 0; f.punto_t0 = -10; f.punto_dur = 0; f.corta = 0;
      f.t_mov = -10; f.t_giro = 0; f.t_dur = 0.35f; f.giro_obj_activo = false; f.giro_obj_t0 = -10;
      f.anim_b2 = -1; f.anim_estado = 0; f.anim_t0 = -10; f.brote_t = -10;
      f.opac = f.opac_obj = 0; f.barras_vis = -1; f.vida_t0 = -1;
    }
  }
  for (size_t i = 0; i < g_obj.size(); ++i) {
    const std::string k = "tablero_b2_snap_o" + std::to_string(i);
    g_obj[i].casilla = LeerProgreso(k + "_c", g_obj[i].casilla);
    g_obj[i].visible = LeerProgreso(k + "_v", g_obj[i].visible ? 1 : 0) != 0;
    g_obj[i].enterrado = LeerProgreso(k + "_e", g_obj[i].enterrado ? 1 : 0) != 0;
    g_obj[i].hecho = LeerProgreso(k + "_h", g_obj[i].hecho ? 1 : 0) != 0;
  }
  const int nb = LeerProgreso("tablero_b2_snap_nbus", int(g_buscada.size()));
  if (nb == int(g_buscada.size())) {
    for (size_t i = 0; i < g_buscada.size(); i += 32) {
      const uint32_t w = uint32_t(LeerProgreso("tablero_b2_snap_b" + std::to_string(i / 32), 0));
      for (size_t b = 0; b < 32 && i + b < g_buscada.size(); ++b) g_buscada[i + b] = (w >> b & 1u) != 0;
    }
  }
  g_bolas = LeerProgreso("tablero_b2_snap_bolas", g_bolas);
  g_bolas_fase.clear();
  const int nbola = LeerProgreso("tablero_b2_snap_nb", 0);
  for (int i = 0; i < nbola && i < 16; ++i)
    g_bolas_fase.push_back({LeerProgreso("tablero_b2_snap_bola" + std::to_string(i) + "_c", 0),
                            LeerProgreso("tablero_b2_snap_bola" + std::to_string(i) + "_n", 0)});
  REXLOG_INFO("dbz3 tablero b2: fase {} reanudada: {} objetos, {} Bolas", g_d.fase + 1, g_obj.size(), g_bolas_fase.size());
  return true;
}
bool Condiciones(const std::vector<int>& r, size_t desde) {   // todas las banderas de r[desde..] como se piden
  for (size_t k = desde; k < r.size(); ++k)
    if (r[k] > 0 ? !Bandera(r[k]) : Bandera(-r[k])) return false;
  return true;
}
// EU 0x27CD40 FUN_0027cd40 (objeto a la vista: no enterrado y con sus banderas) / EU 0x27C090 FUN_0027c090 (enterrado que puede salir al buscar)
// EU 0x27CD40 FUN_0027cd40 (objeto a la vista: no enterrado y con sus banderas)
bool EnSuelo(const Objeto& o) { return !o.hecho && !o.enterrado && (o.visible || Condiciones(o.banderas, 0)); }
// EU 0x27C090 FUN_0027c090 (primer enterrado de la casilla cuyas banderas se cumplen)
bool Escondido(const Objeto& o) { return !o.hecho && o.enterrado && Condiciones(o.banderas, 0); }
int BolaEn(int casilla) {
  for (size_t i = 0; i < g_bolas_fase.size(); ++i)
    if (g_bolas_fase[i][0] == casilla) return int(i);
  return -1;
}
float g_mensaje_t = -10;
// D_CLEAR: la ventana del CMN 402 tiene tres juegos de 8 piezas (entrar 0x33+3k, reposo 0x34+3k, salir 0x35+3k). El
// original las anima con FUN_0026EFC0/0x26ED50/0x26EB00 usando la duracion de cada pieza (FUN_00245C00). El cierre
// empieza en el cuadro en que se pulsa A: se pinta la salida y la ventana desaparece al acabar su duracion.
bool g_msg_cerrando = false;
float g_msg_cierre = -10;
int g_pend_j = -1, g_pend_r = -1;   // combate pendiente (fichas de g_f)
int g_com_rival_b2 = -1;   // orden 51: +0x18 del rival (0x699365); -1 = sin orden (ver ComRivalB2)
int g_ult_mov = -1;    // la ultima pieza que anduvo por casillas (uGpffff88d4 en el original)
int g_pend_mov = -1;   // la ficha que se movio al contacto (uGpffff88d4: la camara la sigue, FUN_00235780/0x236420)
float g_pend_t = 0;
bool g_luchando = false;
// Recolocacion andando tras el combate (EU 0x2778F0 / EU 0x2779D0 + EU 0x278400): la pieza que entro en la casilla
// (hueco 1) anda desde el punto de contacto hasta el centro de su hueco; el turno no sigue hasta que llega (0x277470:
// ninguna pieza en el estado 5 de recolocacion; aqui, `punto_orden` = 100).
bool g_vuelta = false;
int g_vuelta_pend = -1;   // pieza que debe empezar la vuelta en el primer cuadro del tablero (g_ahora al dia)
int g_2contacto = -1;   // segundo rival en la casilla del ganador (EU 0x278320): se encadena al acabar la vuelta
// Radar del Dragon recogido (bit 1 de 0x6992DC: D_RADAR solo dibuja con el), marcador de abajo escondido con L1 (bit 8,
// 0x2700E0) y menu del tablero (triangulo/Start, ELF 0x2A35F0: "Salir" y "Reiniciar Mundo Dragon", cada uno con su Si/No
// de 0x2A2C90; 0 cerrado, 1 menu, 2 Si/No).
bool g_radar = false, g_hud_oculto = false;
bool g_shen_no = false;   // el deseo pedido no se puede cumplir en B3 (Shenron dice su frase 537 #3)
// Cuando entro o salio cada parte del marcador (entradas y salidas animadas de DibHud) y cuando se dibujo por ultima vez
float g_hud_t = -10, g_radar_t = -10, g_rombo_t = -10, g_hud_visto = -10;
float g_rombo_fin = -1;   // el rombo del turno viejo empezo su salida (anim 40) en este instante (0x237F70 estado 0: FUN_0026FDD0); -1 = no sale

bool g_radar_visto = false;
float g_bola_t = -10;   // D_DBGET: la Bola recien cogida (g_bola_n, 0..6) entra en la barra
int g_bola_n = -1;
int g_menu = 0, g_menu_sel = 0, g_yn_sel = 1;
float g_menu_t = 0;
int g_marca = -1, g_marca_icono = 0;   // "?!" o "!!" sobre una casilla
float g_marca_t = -10;
// Alfombra de Mr. Popo (alfombra de la casilla tipo 3). Original: al acabar el paso, FUN_00236830 caso 3 llama a
// FUN_00272DA0, que crea la tarea de la tabla 0x378F40 = animaciones 12, 13, 14 y 15 del #AMA 343+5k (uRam00698DC4) y el
// sonido canal 3 id 0x0E; al acabar (caso 4) FUN_0027A370 recoloca la pieza (hueco libre con FUN_00278A30) y el caso 5
// espera 24 cuadros (0x18). El port hacia el salto instantaneo al pisar la casilla.
struct Alfombra { int i = -1, dest = 0; float t_mueve = 0, t_fin = 0; };
Alfombra g_alf;
float DuracionAlfombra() {   // max de las duraciones de 12..15 (FUN_00245C00 = max(+0x20,+0x24))
  float d = 0;
  for (int k = 12; k <= 15 && k < int(g_d.textos.size()); ++k) d = std::max(d, g_d.textos[size_t(k)].dur);
  return d > 0 ? d / 60.0f : 0.4f;   // sin el #AMA (otra ISO), el minimo del paso
}
float g_cam_x = 0, g_cam_y = 0, g_auto_t = 0;
// R2 "Display Status" ( EU 0x237790 FUN_00237790 / EU 0x237730 FUN_00237730 / EU 0x23B080 FUN_0023B080): la camara se libera y las
// flechas la desplazan 5 px por cuadro de 60 Hz (limitada a [0, mapa - pantalla]); R2 otra vez sale. R2 = RT en el port.
// `g_mantenidos` = botones mantenidos (lo rellena `Mantenidos`, que debe llamar el hub cada cuadro; sin el, cada pulsacion mueve 60 px).
bool g_vista = false;
uint16_t g_mantenidos = 0;
bool g_mantenidos_ok = false;
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

// Audio de eventos del tablero. El original los pide con FUN_00147420(0x694060,3,0) (corta el anterior) +
// FUN_00147260(3,0,id,0): el mismo "tipo 3" que buscar y los pasos (b2.c:189927 contacto id 7, 189789 marcador id 3 y
// 223825 cursor id 0), que R7 midio en el banco general DATA_<idioma> 142 (cuerpo) + 143 (IECS, 26 sonidos) y que el port
// ya usa en SonidoCanal3. Antes se pedia al cuerpo 185 con tipo 5 (una conjetura del runbook); se corrige al medido.
void SonidoCanal3(int id);
void SonidoEventoB2(const char* evento, int id) {
  if (g_iso_b2.empty() || id < 0 || id >= 26) return;
  REXLOG_INFO("dbz3 tablero b2: audio evento {}: canal 3 id {}", evento, id);
  SonidoCanal3(id);
}
// El cuerpo del sonido esta en DATA_<idioma> 142; FUN_00147260(canal, 0, id, 0) usa ese banco con el canal solicitado.
void SonidoCanal(int canal, int id) {
  if (g_iso_b2.empty()) return;
  const bool prueba = std::getenv("DBZ3_TEST_MUSICA") != nullptr;
  const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.7f;
  REXLOG_INFO("dbz3 tablero b2: audio canal {}: cuerpo 142 id {}", canal, id);
  OirSonidoPs2(g_iso_b2, g_loc_afs, 142, id, canal, prueba ? 1.0f : vol, 0, prueba);
}
void SonidoCanal3(int id) { SonidoCanal(3, id); }
// EU 0x2463D0 FUN_002463d0 (sonido por el canal 3 con su envoltorio FUN_00147260)
void SonidoBuscarB2(const Ficha& f) {
  // FUN_00277D40: solo el personaje B2 c=0 (Goku) emite el sonido 0x15 por FUN_002463D0/canal 3.
  if (f.c == kGoku) SonidoCanal3(21);
}
// FUN_00275EB0 (b2.c:221250-221299) emite por canal 3: Goku id 5 en los cuadros 5 y 23 (flag +0x46 evita repeticiones),
// el resto id 6 al superar 13. El mismo recurso de paso se conserva para cualquier longitud del recorrido.
void SonidosPasosB2(float anterior, float ahora) {
  if (anterior < 0 || ahora < anterior) return;
  for (const Ficha& f : g_f) {
    if (f.fuera || f.oculta || f.desde == f.casilla) continue;
    const float inicio = f.t_mov + f.t_giro;
    auto cruce = [&](float cuadro, int id) {
      const float cuando = inicio + cuadro / 60.0f;
      if (cuadro / 60.0f < f.t_dur && anterior < cuando && cuando <= ahora) SonidoCanal3(id);
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
std::string NombreDeC(int c) {
  // DS23: los personajes sin modelo en B3 muestran el nombre de la tabla del ELF de B2 (c 13/14/37/39/42-52).
  if (SoloB2(c)) {
    const std::string& n = NombreB2(c);
    if (!n.empty()) return n;
  }
  const int id = B2aB3(c);
  return id >= 0 ? NombreDe(id) : "#" + std::to_string(c);
}
std::string Nombre(const Ficha& f) { return NombreDeC(f.c); }

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
    // EU 0x279030 FUN_00279030 (personaje de la pieza: +4 del registro 0x69B7A0 + 0x24 n)
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
    // EU 0x2791A0 FUN_002791a0: +5 del T2 -> +0x18 del registro (COM inicial); +8 -> +0x19 (forma inicial).
    // El COM de la pieza no es un estado de dibujo: la orden 51 lo cambia despues y el combate usa el +0x18.
    f.estado0 = y[o + 5];   // se conserva solo para el volcado de pruebas
    f.com_b2 = y[o + 5];    // +0x18: COM inicial de la pieza (FUN_002791a0 b2.c:223455)
    if (y[o + 8] > 0) f.forma_b2 = y[o + 8];   // +8 -> +0x19: forma inicial; 0 = la de por defecto de B3
    f.modo = y[o + 7];      // Saibaman: +0x07 del registro T2 (40 B), copiado a +0x1E por FUN_002791a0
                             // (b2.c:223478); elige su fila de sprites del #AMA (FUN_002762b0 b2.c:221416).
                             // La cambia la orden 56 (FUN_00276cd0 b2.c:221735). Antes se leia +0x2F, 7 bytes
                             // dentro del registro siguiente: los colores de fase IV/IX salian desplazados.
    if (f.c == 40) f.forma_b2 = 1000;   // Kibitoshin = Kaio-shin de B3 en su forma 1 (capsula 197; N100): marca para FormaTableroB3Impl
    if (f.casilla <= 0 || f.casilla >= int(n1)) continue;
    f.desde = f.casilla;
    d.piezas.push_back(f);
  }
  // DS23: la tabla kB2aB3 cubre los 53 personajes de B2 (0-52). Los que no tienen modelo en B3 usan el donante que
  // marca la tabla y conservan su nombre del ELF (SoloB2/NombreB2); aqui solo se avisa si el dato del #YDW trae un ID
  // fuera de la tabla (fichero alterado), que es el unico caso sin nombre ni combate.
  for (const Ficha& f : d.piezas) {
    if (f.c == kSaibaman || B2aB3(f.c) >= 0) continue;
    bool repetido = false;
    for (const Ficha& q : d.piezas) {
      if (&q == &f) break;
      if (q.c == f.c) repetido = true;
    }
    if (repetido) continue;
    REXLOG_WARN("dbz3 tablero b2: personaje B2 c={} (fase {}) fuera de la tabla 0-{}: sin nombre, retrato ni combate",
                f.c, fase + 1, kB2Personajes - 1);
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
    Datos::Adorno ad{int(u32(p)), tipo, f32(p + 8), f32(p + 12), {}};
    const uint32_t nc = p + 0x14 <= y.size() ? u32(p + 0x10) : 0;   // EU 0x238AF0 FUN_00238af0: lista de banderas de cada adorno
    for (uint32_t c = 0; c < nc && c < 16 && p + 0x14 + 4 * size_t(c) + 4 <= y.size(); ++c) ad.conds.push_back(int(u32(p + 0x14 + 4 * size_t(c))));
    if (!ad.conds.empty()) REXLOG_INFO("dbz3 tablero b2: adorno {} (tipo {}) con {} banderas, la primera {}", ad.obj, ad.tipo, ad.conds.size(), ad.conds[0]);
    d.colocados.push_back(std::move(ad));
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
  // Barra de ayuda del tablero y de la tarjeta "Battle!": el original pinta tiras ya compuestas (texto + boton) de
  // DATA_<idioma> 1145 (FUN_002a81a0, b2.c:248596: FUN_0012d100(0x10479) = entrada 1145). No se redibuja con fuente.
  amt(loc, 1145, d.ayuda);
  leer_ama(399, d.aliados_ama);
  amt(cmn, 791, d.mpt_texturas);
  leer_ama(790, d.mpt_ama);
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
  // EU 0x2350C0 FUN_002350c0: recursos fijos del tablero (CMN 397/398/402, DL 188, Goku 337/339 + 338/340, CMN 334/335)
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
    // Goku de cuerpo entero (DATA_<idioma> 155): se descodifica SOLO la textura de 64x88 que usa el respaldo 2D, en vez de
    // la entrada #AMT completa (antes se decodificaban todas y se tiraban; una vez por sesion). TexturaAmt no cachea.
    const std::vector<uint8_t> amt155 = loc.Entrada(155);
    for (int j = 0; j < std::min(CuantasAmt(amt155), 64); ++j) {
      Imagen im = TexturaAmt(amt155, j);
      if (im.w == 64 && im.h == 88) { d.goku = std::move(im); break; }
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
  // Cobertura de la fase (sin cambiar nada): los guiones y las tablas de combate que trae su #YDW. Las cifras
  // cuadran con studies 5/ENTREGAS/DS75/INFORME.md y sirven para detectar un #YDW recortado en la copia del jugador.
  REXLOG_INFO("dbz3 tablero b2: fase {} guiones {} (T3 {} disparadores, T4 {} contactos, T5 {} sucesos)", fase + 1,
              d.guiones.size(), d.t3.size(), d.t4.size(), d.t5.size());
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
    // DS23: la misma tabla con los nombres, para cotejar las parejas del original sin mirar indices (T4: pieza A,
    // pieza B, casilla, script; ver T4 arriba). Es el dato del #YDW, no una lista hecha a mano.
    for (size_t i = 1; i < d.t4.size(); ++i) {
      const auto& t = d.t4[i];
      if (t.size() < 4) continue;
      int ca = -1, cb = -1;
      for (const Ficha& p : d.piezas) {
        if (p.n == t[0]) ca = p.c;
        if (p.n == t[1]) cb = p.c;
      }
      REXLOG_INFO("dbz3 b2 dump: t4#{} {} vs {} casilla {} script {}", i, NombreDeC(ca), NombreDeC(cb), t[2], t[3]);
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

// ---- Pasos y giros de las piezas (R7) ---- EU 0x274670 FUN_00274670: 0x800 de 0x10000 por cuadro del juego, sin dt: se ejecuta una vez por cuadro de 60 Hz (el PAL va mas
// lento por su naturaleza; el port sigue el valor por cuadro del original).
// EU 0x274670 FUN_00274670
constexpr float kVelGiro = 0.19635f * 60.0f;
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
// EU 0x278970 FUN_00278970
// La pieza empieza a andar de su casilla a `dest`: gira (si no es del jugador) y anda L / 4 cuadros.
// A2b: dos fichas del mismo bando comparten casilla: la que llega va al hueco 1 (FUN_00278A30, b2.c:223092); con el hueco 1 ya
// ocupado la casilla esta llena y no se puede elegir como destino (FUN_00278970, b2.c:223062; lo usa FUN_002793E0).
// EU 0x2793E0 FUN_002793e0 (casillas llenas: no se ofrecen como destino)
// EU 0x278A30 FUN_00278a30 (hueco libre de la casilla: 1 la primera, -1 llena o del otro bando)
bool CasillaLlena(const Ficha& f, int casilla) {
  for (const Ficha& o : g_f)
    if (&o != &f && !o.fuera && !o.oculta && o.casilla == casilla && o.rival == f.rival && o.slot == 1) return true;
  return false;
}
// EU 0x278D40 FUN_00278d40 (hueco +0xC) y EU 0x278A30 FUN_00278a30 (hueco libre de la casilla): el port lo recalcula en
// orden de tabla (2 huecos, los unicos que escribe el original).
void RecalcularSlots() {   // en orden de la tabla: la primera ficha de la casilla en el hueco 0, la siguiente en el 1
  for (size_t i = 0; i < g_f.size(); ++i) {
    g_f[i].slot = 0;
    if (g_f[i].fuera || g_f[i].oculta) continue;
    for (size_t j = 0; j < i; ++j)
      if (!g_f[j].fuera && !g_f[j].oculta && g_f[j].casilla == g_f[i].casilla && g_f[j].rival == g_f[i].rival) g_f[i].slot = 1;
    g_f[i].slot_ant = g_f[i].slot;
  }
}
// EU 0x273800 FUN_00273800: pasos (cuadros de 60 Hz, 4 px cada uno) que dura el paso hasta una casilla = (int)(distancia entre los
// centros de las dos casillas / 4) + 1; es el contador +0xAE con el que FUN_00277ED0 pone a la pieza en el estado 2 (andar)
int PasosHasta(float dist) { return int(dist / 4.0f) + 1; }
// EU 0x2738A0 FUN_002738a0: igual cuando hay un rival en la casilla de destino (contacto): la pieza se queda a 60 px de el,
// (int)((distancia - 60) / 4) + 1 pasos (los llaman FUN_00235930 / FUN_00235C30 al pulsar X o decidir el rival)
int PasosContacto(float dist) { return int((dist - 60.0f) / 4.0f) + 1; }
void EmpezarPaso(Ficha& f, int dest, float ahora) {
  f.punto = 0;   // un paso por casillas anula el punto de las ordenes 65/66
  f.corta = 0;
  if (&f >= g_f.data() && &f < g_f.data() + g_f.size()) g_ult_mov = int(&f - g_f.data());
  const float g = GiroDeOrden(f, ahora);   // continua desde el angulo fijo de 61/62 si lo habia
  f.giro_obj_activo = false;   // andar vuelve a mandar sobre el angulo fijo de las ordenes 61/62
  const auto &a = g_d.casillas[size_t(f.casilla)], &b = g_d.casillas[size_t(dest)];
  const float dx = b.x - a.x, dy = b.y - a.y;
  f.giro0 = g;
  // EU 0x2732E0 FUN_002732e0
  f.giro1 = dx != 0 || dy != 0 ? std::atan2(dx, dy) : 0.0f;   // 0x2732E0 FUN_002732E0: angulo = 0x4000 - atan2(-dy, dx) / 2pi * 65536
  f.t_giro = f.jugador ? 0.0f : std::fabs(Dif(g, f.giro1)) / kVelGiro;
  if (f.jugador) f.giro0 = f.giro1;
  // EU 0x273630 FUN_00273630
  f.t_dur = std::max(0.1f, float(PasosHasta(std::hypot(dx, dy))) / 60.0f);   // 0x273630 FUN_00273630: 4 px por cuadro, sin dt; 0x273800: pasos de la duracion
  f.anim_b2 = -1;   // andar vuelve al estado de reposo: la pose puntual (0x277B50/0x277A30/0x277A90...) no se arrastra
  f.anim_estado = 0;
  f.t_mov = ahora;
  // EU 0x278E20 FUN_00278e20 (casilla actual: +8 del registro de pieza)
  f.desde = f.casilla;
  // EU 0x278D60 FUN_00278d60 (casilla destino: +0xA del registro de pieza)
  f.casilla = dest;
  // EU 0x2784B0 FUN_002784b0 (las dos casillas han cambiado: +8 != +0xA)
  const int ant = f.slot;   // el hueco sale del orden de la tabla (medido: Goku, p1, queda en el 0 y Gohan, p5, pasa al 1)
  RecalcularSlots();
  f.slot_ant = ant;
}
float TiempoPaso(const Ficha& f) { return f.t_giro + f.t_dur; }

// ---- Reglas ---- EU 0x27A090 FUN_0027A090 (estado del registro +6 & 0xF < 2 = fuera del tablero) y EU 0x27A1C0 FUN_0027A1C0 (en juego = (+6 & 0xF) == 2
// con +2 != 0) = Activa + movs/acciones; EU 0x278B00 FUN_00278B00 ((+6 & 0xF0) == 0x10 = bando del jugador) = Ficha::jugador.
// EU 0x27A090 FUN_0027a090
// EU 0x27A1C0 FUN_0027a1c0
// EU 0x278B00 FUN_00278b00
bool Activa(const Ficha& f) { return !f.fuera && !f.oculta; }
// EU 0x27A0D0 FUN_0027a0d0 (barras agotadas: +1 del registro == 0; 1 = sin vidas)
bool SinBarras(const Ficha& f) { return f.barras <= 0; }
// EU 0x278320 FUN_00278320: ocupante de la casilla; prefiere el hueco 0 (o el 2) y deja el 1 como ultimo recurso.
int FichaEn(int casilla, bool rival) {
  int otro = -1;
  for (size_t i = 0; i < g_f.size(); ++i) {
    if (!Activa(g_f[i]) || g_f[i].casilla != casilla || !(rival ? g_f[i].rival : g_f[i].jugador)) continue;
    if (g_f[i].slot == 0) return int(i);
    if (otro < 0) otro = int(i);
  }
  return otro;
}
// EU 0x278260 FUN_00278260: el contacto solo mira el hueco 0 (el del centro); una segunda pieza en el hueco 1 no inicia
// combate (eso lo encadena FUN_00278320 al acabar el primero, g_2contacto).
int FichaEnHueco0(int casilla, bool rival) {
  for (size_t i = 0; i < g_f.size(); ++i)
    if (Activa(g_f[i]) && g_f[i].casilla == casilla && (rival ? g_f[i].rival : g_f[i].jugador) && g_f[i].slot == 0)
      return int(i);
  return -1;
}
// EU 0x27A210 FUN_0027A210: hay alguna pieza en juego del bando con +2 (movimientos) != 0
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
  // EU 0x2A1230 FUN_002a1230 (orden de la lista de aliados: tabla 0x37A830)
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
  g_vuelta = false;
  g_vuelta_pend = -1;
  g_2contacto = -1;
  // EU 0x2769E0 FUN_002769e0 (copia los 32 registros del T2 al registro de pieza 0x69B7A0)
  g_f = g_d.piezas;
  for (Ficha& f : g_f) f.opac = f.opac_obj = 0.0f;   // +0x64 arranca a 0 (normal)
  // EU 0x27CF00 FUN_0027cf00
  // Inicio de fase: pone a 0 las banderas de objeto buscado (0x69BC20, 0x40 bytes = g_buscada) y a -1 la tabla de casilla por
  // objeto (0x69BC60, 0x40 shorts: aqui es Objeto::casilla de g_obj), y luego coloca los objetos (0x27CD40, Objeto) y las Bolas
  // (0x27BD80, el bloque "Bolas de la fase" de abajo). Las tablas de objeto/buscado/Bolas se guardan al cerrar cada turno
  // y 0x27D110 las restaura si se vuelve a entrar en la misma fase a medias (RestaurarEstadoFase).
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
  // EU 0x27BD80 FUN_0027bd80: coloca las Bolas de la fase (min(fase,6) - las que tienes + 1, como mucho 3)
  // EU 0x27D110 FUN_0027d110: si hay guardado de ESTA fase a medias, sus tablas mandan (objetos buscados, casillas y Bolas).
  const bool reanudada = RestaurarEstadoFase();
  if (reanudada) {
    g_bolas_ini = g_bolas;
    RecalcularSlots();   // las piezas restauradas pueden traer otro hueco/casilla
    if (g_sel < 0 || g_sel >= int(g_f.size()) || !Activa(g_f[size_t(g_sel)]) || g_f[size_t(g_sel)].c != kGoku) {
      for (size_t i = 0; i < g_f.size(); ++i)
        if (Activa(g_f[i]) && g_f[i].jugador && g_f[i].c == kGoku) { g_sel = int(i); break; }
    }
  } else {   // Bolas de la fase (ELF 0x27BD80)
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
  // EU 0x26F340 FUN_0026f340
  // R7: la banda (D_RADARBASE, bit 0x02 de 0x6992DC) no sale sola al empezar: solo si el jugador la dejo puesta con L1
  // (bit 0x08, guardado con la partida; FUN_0026FFF0 al empezar el turno), al coger una Bola (0x26F340) o el Radar
  g_hud_oculto = LeerProgreso("tablero_b2_marcador", 0) == 0;
  // PRUEBA: DBZ3_TEST_B2_RADAR=1 empieza con el Radar del Dragon; DBZ3_TEST_B2_BOLAS=N (7 bits) con esas Bolas en la barra
  if (std::getenv("DBZ3_TEST_B2_RADAR")) g_radar = true;
  if (const char* b = std::getenv("DBZ3_TEST_B2_BOLAS")) g_bolas |= std::atoi(b) & 0x7F;
  g_menu = 0;
  g_mensajes.clear();
  g_msg_cerrando = false;
  g_msg_cierre = -10;
  g_premio_id = 0;
  g_espera_x = false;
  g_subidas.clear();
  g_vz = VentanaZenie{};
  g_alf = Alfombra{};   // la alfombra pendiente no cruza de fase
  // EU 0x238870 FUN_00238870 (modo 3: copia a RAM el estado guardado; aqui, las banderas de las fases anteriores)
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
  // EU 0x23ACB0 FUN_0023acb0
  // EU 0x23AF30 FUN_0023af30
  // DS45: la camara se pone a (0,0) al ENTRAR en el Mundo Dragon (FUN_0023B290, b2.c:191905-191919, llamada por las tres
  // ramas de FUN_00238870), no al cargar cada fase: el reinicio va en Entrar/Resultado. La orden 9 del guion de inicio la
  // lleva luego a su casilla.
  REXLOG_INFO("dbz3 tablero b2: fase {} empieza: {}, {} candidatos a aliado, jefe {}", g_d.fase + 1,
              Nombre(g_f[size_t(g_sel)]), g_cand.size(), g_jefe >= 0 ? Nombre(g_f[size_t(g_jefe)]) : std::string("ninguno"));
  {   // evidencia de los tipos de movimiento (R7/b2/re_ia.md §3: T2 +3/+4, tabla 0x378FE0): el rival no lleva paridad de turnos
    std::string tipos;
    for (const Ficha& f : g_f)
      if (f.rival) tipos += " c" + std::to_string(f.c) + "=" + std::to_string(f.mov) + "/" + std::to_string(f.mov_p);
    REXLOG_INFO("dbz3 tablero b2: rivales de la fase {} (tipo/dato):{}", g_d.fase + 1, tipos);
  }
}

void Guion(int k, int prof);
// Disparadores de contacto: T3 {pieza, pieza, 0, script} y T4 por personaje con banderas (el primero que vale).
int g_suceso = -1;   // fila de T4 del ultimo contacto (iGpffff8d8c del ELF)
// EU 0x278DE0 FUN_00278de0 (personaje de una pieza: +0 del registro de T2)
int PersonajeDe(int n) {   // personaje (+0 del registro de T2) de la pieza n
  for (const Ficha& f : g_d.piezas)
    if (f.n == n) return f.c;
  return -1;
}
// EU 0x27F510 FUN_0027f510 (T4: contactos por personaje/casilla/banderas tras un paso; no mira el bando)
void SucesosDeContacto(const Ficha& a, const Ficha& b) {
  g_suceso = -1;
  for (size_t i = 1; i < g_d.t4.size(); ++i) {   // como el ELF, desde la fila 1
    const auto& r = g_d.t4[i];
    const int ca = r[0] ? PersonajeDe(r[0]) : -1, cb = r[1] ? PersonajeDe(r[1]) : -1;
    bool vale;
    if (ca < 0 && cb < 0) vale = false;   // solo A = 0 / B = 0 significan "cualquiera" (FUN_0027f510)
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
// EU 0x278E40 FUN_00278e40 (funde el +0x64 de todas las piezas: objetivo 0 normal / 0,5 atenuada; `excepto` se queda en 0)
void FundirPiezas(float objetivo, int excepto) {
  for (size_t i = 0; i < g_f.size(); ++i) g_f[i].opac_obj = int(i) == excepto ? 0.0f : objetivo;
}
// Pose puntual de una pieza: estado visual (+0) y animacion (+0x1C) del registro de 0x69B7A0, con su tiempo de inicio.
// La usan 0x277890 (combate), 0x277A90 (golpeado), 0x277AF0 (KO), 0x277B50 (recoger), 0x277860 (buscar) y 0x277A30
// (objeto encontrado al buscar). DibPieza/DibFicha3D/DibGoku3D la pintan con la tabla 0x379010.
void PonerPose(Ficha& f, int anim, int estado) {
  f.anim_b2 = anim;
  f.anim_estado = estado;
  f.anim_t0 = g_ahora;
}
// El original llama a FUN_00278E40 en su estado de cada cuadro: avanza 0,1 por cuadro hacia el objetivo.
void PasoFundido() {
  for (Ficha& f : g_f) {
    if (f.opac < f.opac_obj) f.opac = std::min(f.opac + 0.1f, f.opac_obj);
    else if (f.opac > f.opac_obj) f.opac = std::max(f.opac - 0.1f, f.opac_obj);
  }
}
// EU 0x235410 FUN_00235410
// Tras el combate del contacto: T5 de esa fila con el suceso (ELF 0x235410 -> 0x27F3A0). R7: el codigo es 1 gana el
// jugador, 4 gana y el rival queda fuera (FUN_0027A0D0), 2 gana el rival, 3 gana el rival y la ficha del jugador queda
// fuera; 5 = cualquiera. Comprobado con los datos: fase IV, T4 #27 Goku contra Cell, suceso 4 -> guion 413 -> 306 (orden
// 41, fase superada).
// EU 0x27F3A0 FUN_0027f3a0 (T5: suceso tras el combate de esa fila de T4)
void SucesosDeCombate(bool gana, bool fuera) {
  // EU 0x238250 FUN_00238250: al arrancar el guion de T5 vuelve a poner el +0x64 de TODAS las piezas a 0 (funde la
  // atenuacion del contacto, 0,1 por cuadro). Antes el port no dibujaba ese fundido.
  FundirPiezas(0.0f, -1);
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
  // T4 (FUN_0027F510, arriba): lo mira TODO contacto; el segundo de la casilla del ganador tambien (0x236420 estado 0x14).
  SucesosDeContacto(g_f[size_t(j)], g_f[size_t(r)]);
  // EU 0x235C30 FUN_00235c30: al cerrar el paso contra el rival, FUN_00278E40(0,5, pieza que se movio) atenua las demas.
  FundirPiezas(0.5f, j);
  g_pend_j = j;
  g_pend_r = r;
  g_pend_t = g_ahora;
  // EU 0x277890 FUN_00277890 (contacto: estado 0xB y animacion 2 en las DOS piezas que luchan; +0x3A visual = 3)
  PonerPose(g_f[size_t(j)], 2, 0x0B);
  PonerPose(g_f[size_t(r)], 2, 0x0B);
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
// Rival en la MISMA casilla que la pieza elegida: el original no abre el destino para esa pieza. En FUN_00236EA0
// (b2.c:189715) FUN_00278320(uGpffff88d4) devuelve el ocupante y, si no es 0, se salta FUN_00276F50 (el cursor de
// destino); al confirmar (cuadrado/circulo, casos 0x8000/0x2000 de b2.c:189865/189940) pasa a FUN_00236420 estado 0
// con uGpffff88bc = la pieza elegida y uGpffff88c0 = el ocupante: mismo T4 y mismo combate que un contacto normal.
// El port solo dejaba andar a otra casilla, asi que en la fase 3 (Vegeta y Freezer en la casilla 4 con Goku/Gohan)
// el piloto no podia atacar y se quedaba 128 turnos en "no tiene a donde ir".
// EU 0x236EA0 FUN_00236ea0
bool AtacarOcupante(int i) {
  if (i < 0 || i >= int(g_f.size())) return false;
  Ficha& f = g_f[size_t(i)];
  if (!f.jugador || f.movs <= 0) return false;
  // EU 0x278320 FUN_00278320: ocupante del bando contrario en la casilla actual (hueco 2/0 y, si no, el 1)
  const int r = FichaEn(f.casilla, true);
  if (r < 0) return false;
  --f.movs;                    // EU 0x27A180 FUN_0027a180 (la accion gasta un movimiento)
  g_pend_mov = i;              // uGpffff88d4 = la pieza elegida: la camara del contacto la sigue (FUN_00236420)
  g_dest = -1;
  REXLOG_INFO("dbz3 tablero b2: {} ataca al ocupante de la casilla {}: {}", Nombre(f), f.casilla, Nombre(g_f[size_t(r)]));
  Contacto(i, r);              // EU 0x236420 FUN_00236420 estado 0x14 (T4 y combate)
  return true;
}

std::vector<int> Distancias(int c);
// s9: coger la daga / la armadura. El original no pone ningun cartel (medido en PCSX2, savestate 3, casilla de la espada):
// la pieza muestra "!" y el objeto, y sobre ella sale "+010%" con el kanji 攻 (daga) / 防 (armadura): son las mismas
// animaciones de las ordenes 83/84 (tareas 0x27BAB0 y 0x27BD00); aqui se lanzan igual, con el sonido del 110.
// EU 0x27BD00 FUN_0027bd00 (crea la tarea de la cifra de las ordenes 83/84 y del objeto recogido)
void SubidaDeObjeto(const Ficha& f, int valor, bool defensa, int an0 = -1, int nan = 4, int snd110 = 0, bool sin_cifra = false) {
  Subida s;
  s.n = f.n;
  s.signo = 1;
  s.valor = valor & 0xFF;
  s.defensa = defensa;
  s.t0 = g_ahora;
  s.an0 = an0;
  s.nan = nan;
  s.snd110 = snd110;
  s.sin_cifra = sin_cifra;
  s.snd60 = f.jugador ? 0x0D : 0x0A;   // FUN_00278B00: pieza del jugador
  g_subidas.push_back(s);
  REXLOG_INFO("dbz3 tablero b2: {}: {} +{} % (animacion de las ordenes 83/84)", Nombre(f), defensa ? "armadura" : "daga", valor);
}
// EU 0x27A140 FUN_0027a140
// EU 0x27C3E0 FUN_0027C3E0 + EU 0x27A980 FUN_0027A980: al recoger un objeto (tipo = +4 del registro T8, valor = +5) sale la tarea de
// 0x27A980: la pose 0x3C del #AMA 397 sobre la pieza, a los 60 cuadros el sonido 0xD (pieza del jugador) o 0xA (rival) y, a los 110, el
// del tipo (1: 0xC, 2 y 4: 0xD, 3: 0x12, 5: 0x10, 6: 0xF); al acabar la pose, segun el tipo: 0 Bola de Dragon (mensaje D_DBGET + sonido
// 0x11), 1-4 +-ataque / defensa (FUN_00276E10 / FUN_00276DE0), 5 +movimientos (FUN_0027A140), 6 -1 vida (FUN_00279FD0), 7 capsula
// (FUN_002835F0 + ventana "Get skill!"), 8 zenie (FUN_00254720 / FUN_00254620 = ventana con la cuenta valor x 100), 9 Radar (bit 0 de
// 0x6992DC + FUN_0026FFF0 + sonido 3).
void ProgramarSonidosObjeto(const Ficha& f, int tipo) {
  const auto& la = g_d.life_ama;
  const float pose = la.size() > 0x3C ? la[0x3C].dur / 60.0f : 1.8f;   // anim 0x3C: fin de la pose (+0x44 = 0x3C)
  if (tipo == 0 || tipo >= 7) g_son_dif.push_back({g_ahora + 60.0f / 60.0f, f.jugador ? 0x0D : 0x0A});
  // EU 0x26FE80 FUN_0026fe80 (aviso D_DBGET de la Bola: sonido 0x11 al acabar la pose)
  if (tipo == 0 && f.jugador) g_son_dif.push_back({g_ahora + pose, 0x11});   // 0xFFFD: FUN_0026FE80 + sonido 0x11
  if (tipo == 9 && f.jugador && !g_radar) g_son_dif.push_back({g_ahora + pose, 0x03});   // 0xFFFA: radar por primera vez
}
// EU 0x27A980 FUN_0027a980 (secuencia de recoger objeto: pose 0x3C, sonidos 60/110 y efecto por tipo)
void Coger(Ficha& f, Objeto& o, bool buscando = false) {
  o.hecho = true;
  // EU 0x277B50 FUN_00277b50 (al recoger andando: estado 0xE y animacion 9; tabla 0x379010 -> clip 0x10)
  // EU 0x277A30 FUN_00277a30 (objeto sacado al buscar: estado 4 y animacion 10; tabla 0x379010 -> clip 0xC)
  if (buscando) PonerPose(f, 10, 4);
  else PonerPose(f, 9, 0x0E);
  char t[160];
  ProgramarSonidosObjeto(f, o.tipo);
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
    // EU 0x27A980 FUN_0027a980 (secuencia 0xFFFA del radar)
    // EU 0x26FFF0 FUN_0026fff0
    // 0x27A980 secuencia 0xFFFA (estado 1): si la pieza NO es del jugador (FUN_00278B00 == 0) no pasa nada; si lo es y el bit 0 de
    // 0x6992DC aun no estaba, se pone (g_radar), FUN_0026FFF0 saca la banda del radar en el marcador (animacion 0x10 del AMA
    // 0x698DC8 = g_hud_oculto/g_radar_t) y suena el 3 (ProgramarSonidosObjeto); estado 2 espera 30 unidades de dt y acaba.
    // DS45: no hay mensaje ni calculo de direccion en el original (eso era una invencion del port; 0x27CC30 es la marca de
    // objeto mas cercana de la IA, no el radar). Con el radar ya en la mano (bit 0 puesto) tampoco se reabre la banda.
    case 9: {
      if (f.jugador && !g_radar) {
        g_radar = true;   // el radar del marcador (D_RADAR) se enciende con sus flechas hacia las Bolas
        g_hud_oculto = false;
      }
      REXLOG_INFO("dbz3 tablero b2: Radar del Dragon recogido (radar {})", g_radar ? "encendido" : "ya lo tenia");
      return;
    }
    case 8:   // al monedero del HUB (antes se sumaba a g_zenie y se perdia: auditoria8/C)
      // EU 0x254720 FUN_00254720
      // EU 0x254620 FUN_00254620
      g_vz = VentanaZenie{true, g_zenie, o.valor * 100, 8, 0, 0, 5, g_ahora, g_ahora, 0};
      g_zenie += o.valor * 100;
      dbz3::rpg::Premio(o.valor * 100);
      std::snprintf(t, sizeof(t), T::T("%d zenie.", "%d zenie."), o.valor * 100);
      return Aviso(t, 25);
    case 5:   // 0x27A980 tipo 5 -> FUN_0027A140: +valor a los movimientos y a los de por turno; anims 130-132, sonido 0x10
      f.movs += o.valor;
      f.movs_turno += o.valor;
      SubidaDeObjeto(f, o.valor, false, 130, 3, 0x10, true);
      return Aviso(Nombre(f) + T::T(": cebolla, mueve mas casillas por turno.", ": onion, moves more spaces per turn."), 24);
    case 2:   // tipo 2 -> FUN_00276E10(-valor): ataque - valor; sin animaciones, con la cifra; sonido 0xD
      f.ataque = int16_t(f.ataque - o.valor);   // DS45: +0x10 es i16 en el original (FUN_00276e10, b2.c:221946)
      return SubidaDeObjeto(f, o.valor, false, 0, 0, 0x0D);
    case 4:   // tipo 4 -> FUN_00276DE0(-valor): defensa - valor; idem
      f.defensa = int16_t(f.defensa - o.valor);   // DS45: +0x12 i16 (FUN_00276de0, b2.c:221934)
      return SubidaDeObjeto(f, o.valor, true, 0, 0, 0x0D);
    case 1:   // daga: +valor % al ataque (Reglas::ataque). RE exacta: FUN_0027A980, tipo 1/2 (b2.c:224592-224595) ->
              // FUN_00276E10 (+0x10 i16 = ataque), tipo 3/4 -> FUN_00276DE0 (+0x12 i16 = defensa), sin clamp ni caducidad;
              // FUN_00276E40/76E60 los leen y FUN_00277... los copia al bloque del combate (0x69933E/0x699340, b2.c:
              // 226683-226708). Se suman durante toda la fase y los gasta el combate al empezar.
      f.ataque = int16_t(f.ataque + o.valor);   // DS45: +0x10 es i16 en el original (FUN_00276e10)
      return SubidaDeObjeto(f, o.valor, false, -1, 4, 0x0C);   // DS45: sonido del tipo 1 a los 110 cuadros
    case 3:   // armadura: +valor % a la defensa (mismo FUN_0027A980, tipos 3/4 -> +0x12; el consumidor lo limita a 0,9)
      f.defensa = int16_t(f.defensa + o.valor);   // DS45: +0x12 i16 (FUN_00276de0)
      return SubidaDeObjeto(f, o.valor, true, -1, 4, 0x12);   // DS45: sonido del tipo 3 a los 110 cuadros
    // EU 0x27C830 FUN_0027c830
    case 7: {   // 0x27A980 (0xFFFB) / 0x27C830: FUN_002835F0(grupo = +5 del objeto, personajes de la pieza) elige la capsula y, si la
      // pieza es del jugador, sale la ventana "Get skill!" (FUN_002A44C0, 0xD001) igual que en las ordenes 8/70; al cerrarla se gana
      // (ver g_premio_id). Antes: capsula al azar sin ventana.
      const int id = f.jugador ? dbz3::pasarela::nave::CapsulaAlAzar(o.valor, f.c) : 0;
      REXLOG_INFO("dbz3 tablero b2: objeto capsula (grupo {}, pieza {} = personaje {}): capsula {}", o.valor, f.n, f.c, id);
      if (id > 0) {
        g_premio_id = id;
        g_premio_t = -10;
        return;
      }
      return Aviso(T::T("Cápsula: no hay ninguna que puedas ganar.", "Capsule: none you can win."), 22);
    }
    case 6:   // tipo 6 -> FUN_00279FD0: -1 a las vidas (+1) sin bajar de 1; anims 124-129, sonido 0xF (antes: un aviso "Mr. Popo")
      // EU 0x279FD0 FUN_00279fd0 (-1 barra sin bajar de 1)
      f.barras = std::max(1, f.barras - 1);
      SubidaDeObjeto(f, 0, false, 124, 6, 0x0F, true);
      return Aviso(Nombre(f) + T::T(" pierde una barra de vida.", " loses a life bar."), 24);
  }
  std::snprintf(t, sizeof(t), T::T("Objeto de tipo %d.", "Item of type %d."), o.tipo);
  Aviso(t);
}

// EU 0x273160 FUN_00273160 (rejilla de distancias: byte en iGpffff8d24 con el ancho de la tabla iGpffff88b8 +0x20)
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
// EU 0x273180 FUN_00273180
// EU 0x273F40 FUN_00273f40
// EU 0x276B80 FUN_00276b80
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

// Turno de los rivales. En el original cada pieza tiene un tipo de movimiento (registro +0x1C, tabla 0x378FE0) y su dato
// (+0x1D): 0 quieto, 1 encara a una ficha del jugador vecina, 2 va a la casilla del dato, 3 al azar, 4 persigue a la ficha
// contraria mas cercana, 5 sigue a la pieza del dato, 6 va a la contraria con menos barras, 7/8 van a la marca de objeto
// mas cercana (8 solo del tipo del dato). El tipo viene en T2 +3/+4 y lo cambian los guiones con la orden 50 (pieza, tipo,
// dato; ELF 0x293EE0 -> 0x276D80), que el port ejecuta. Medido en R7/b2/re_ia.md §3: no hay contador de turnos ni paridad
// (cada rival en juego con accion actua en cada fase enemiga; FUN_00278EE0), el paso no mira la ocupacion (tipos 3 y 7) y el
// contacto se comprueba DESPUES de entrar en la casilla (FUN_00235780 -> FUN_00235930 -> FUN_00278260). El port ya hacia
// las tres cosas asi; Saibaman y Dr. Gero tienen tipo 0 en los datos y por eso no se mueven.
// Script k de la fase: de sus ordenes se portea la 50 (tipo de movimiento de una pieza: pieza, tipo, dato; ELF 0x293EE0)
// y la 44 (salta a otro script, 0x294450). Las demas (dialogos, camara, apariciones, combates, objetos) van por OrdenEscena
// y Sucesos*; su lista y lo que llama cada una: E:/DBZ3HD_trabajo/R2/scr_ops.txt.
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
    // EU 0x293CE0 FUN_00293ce0
    // EU 0x293CA0 FUN_00293ca0
    // EU 0x293C60 FUN_00293c60
    // EU 0x293C20 FUN_00293c20
    // EU 0x293BE0 FUN_00293be0
    // EU 0x2775F0 FUN_002775f0 (orden 62, espera)
    // EU 0x277630 FUN_00277630 (orden 61, sin esperar)
    if (o[1] >= 58 && o[1] <= 62) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        if (o[1] <= 60) {
          // EU 0x277700 FUN_00277700 (58) / EU 0x277740 FUN_00277740 (60) / EU 0x277780 FUN_00277780 (59)
          // EU 0x277740 FUN_00277740
          // EU 0x277780 FUN_00277780
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
    // EU 0x293B60 FUN_00293b60
    // EU 0x293AD0 FUN_00293ad0
    // EU 0x277540 FUN_00277540
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
    // EU 0x293A50 FUN_00293a50
    // studies 2 G04: 65/66 {pieza, x, y} (FUN_002774F0): la pieza anda a un punto del mapa a 4 px por cuadro (240 px/s),
    // sin cambiar su casilla logica. 65 no bloquea (tabla 0x37A020[65] = 0); 66 bloquea hasta llegar. Al llegar la
    // funcion original devuelve 2; aqui, si ya esta en el punto, se esperan esos 2 cuadros.
    // EU 0x2939C0 FUN_002939c0
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
    // EU 0x293940 FUN_00293940
    // EU 0x2775D0 FUN_002775d0
    if (o[1] == 68) {   // 0x293940 FUN_00293940 -> FUN_002775D0 {pieza, angulo}: +0x20 del registro visual = el angulo ACTUAL
      // (1/65536 de vuelta); FUN_00274670 lo acerca al objetivo +0x24 (las ordenes 61/62) a 0x800 por cuadro: la pieza salta a
      // ese angulo y gira de vuelta al objetivo que tenia
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        const float objetivo = GiroDeOrden(f, g_ahora);
        f.giro_obj0 = float(o[3] & 0xFFFF) * 6.2831853f / 65536.0f;
        f.giro_obj1 = objetivo;
        f.giro_obj_activo = true;
        f.giro_obj_t0 = g_ahora;
        f.giro_obj_dur = std::fabs(Dif(f.giro_obj0, f.giro_obj1)) / kVelGiro;
      }
      return espera;
    }
    // EU 0x294A40 FUN_00294a40
    // EU 0x292BD0 FUN_00292bd0
    // EU 0x292BA0 FUN_00292ba0
    if (o[1] == 34 || o[1] == 85 || o[1] == 86) {
      // EU 0x27A290 FUN_0027a290
      // EU 0x277400 FUN_00277400
      // EU 0x277270 FUN_00277270
      // EU 0x294A40 FUN_00294A40 -> FUN_0027A290 {pieza, v}: u32 en +0x20 del registro 0x69B7A0 + 0x24 n. EU 0x292BD0 / 0x292BA0
      // -> FUN_00277400 / FUN_00277270 {pieza}: 85 pone a 0 y 86 recalcula (0 nada, 1 su bando tiene una pieza en juego con
      // acciones, 2 si +6 & 0x100) el campo +0x3A del registro visual. Busqueda en b2.c (0x270000-0x27B000): nadie lee el +0x20
      // del registro ni el +0x3A visual con una comparacion (solo los escriben 0x277060, 0x2770F0, 0x277890, 0x277DD0, 0x278030...):
      // son datos sin efecto en pantalla, el port no los guarda. [H] por si los lee la tabla de funciones 0x378FE0.
      REXLOG_INFO("dbz3 tablero b2: script {}: orden {} ({}, {}): campo sin lector, sin efecto", k, o[1], o[2], o[3]);
      return espera;
    }
    // EU 0x293980 FUN_00293980
    // EU 0x2776A0 FUN_002776a0
    if (o[1] == 67) {   // 0x293980 FUN_00293980 -> FUN_002776A0 {pieza, x, y}: estado 0xF (el de las ordenes 58-60) y su
      // posicion visual (+0/+4 del registro de 0x50 B) = el punto (x, y) del mapa, de golpe (sin andar: no toca su casilla)
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        f.punto = 2;
        f.punto_orden = 67;
        f.punto_x = f.punto_x0 = float(o[3]);
        f.punto_y = f.punto_y0 = float(o[4]);
        f.punto_t0 = g_ahora;
        f.punto_dur = 0;
        // EU 0x277670 FUN_00277670 (estado visual 0xF: el de las ordenes 58-60)
        f.anim_estado = 0x0F;
        f.anim_t0 = g_ahora;
        REXLOG_INFO("dbz3 tablero b2: script {}: orden 67: {} en el punto ({}, {})", k, Nombre(f), o[3], o[4]);
      }
      return espera;
    }
    // EU 0x293370 FUN_00293370
    // EU 0x293330 FUN_00293330
    // EU 0x27A100 FUN_0027a100
    if (o[1] == 80 || o[1] == 81) {   // 0x293370 FUN_00293370 -> FUN_0027A140 / EU 0x293330 FUN_00293330 -> FUN_0027A100:
      // 80 {pieza, otra}: los bytes +2 y +3 del registro de la pieza (0x69B7A0 + 0x24 n) = los de la otra; 81 {pieza, n}: se
      // les suma n (char) a los dos. +2 = acciones y +3 = tipo de movimiento (T2; el port solo acepta 0-8)
      Ficha *pa = nullptr, *pb = nullptr;
      for (Ficha& f : g_f) {
        if (f.n == o[2]) pa = &f;
        if (f.n == o[3]) pb = &f;
      }
      // +2 / +3 segun el bando ( EU 0x278F90 FUN_00278F90: al empezar el turno del jugador +2 = +3 = sus movimientos por turno EU 0x278EE0: las acciones del rival). OJO: el tipo de movimiento del rival es el +0x1C del registro, no el +3 (FUN_002791A0))
      auto campo = [](Ficha& f, int c) -> int& { return f.jugador ? (c == 2 ? f.movs : f.movs_turno) : (c == 2 ? f.acciones : f.acc_turno); };
      if (pa && o[1] == 80 && pb) {
        campo(*pa, 2) = campo(*pb, 2);
        campo(*pa, 3) = campo(*pb, 3);
      } else if (pa && o[1] == 81) {
        const int d = int8_t(o[3] & 0xFF);
        campo(*pa, 2) = (campo(*pa, 2) + d) & 0xFF;
        const int m = (campo(*pa, 3) + d) & 0xFF;
        campo(*pa, 3) = m;
      }
      REXLOG_INFO("dbz3 tablero b2: script {}: orden {} ({}, {})", k, o[1], o[2], o[3]);
      return espera;
    }
    // EU 0x293DA0 FUN_00293da0
    // EU 0x293D60 FUN_00293d60
    // EU 0x2936C0 FUN_002936c0
    // EU 0x293680 FUN_00293680
    if (o[1] == 55 || o[1] == 57 || o[1] == 72 || o[1] == 73) {   // colocar pieza / cambiar su ficha (ELF 0x27A370,
      // EU 0x27A3F0 FUN_0027a3f0
      // EU 0x276B30 FUN_00276b30
      // EU 0x276AD0 FUN_00276ad0
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
      // 72 {pieza, v} ( EU 0x2936C0 FUN_002936C0 -> FUN_00276B30): +1 del registro = vidas (FUN_002791A0 lo carga del byte +6 del T2 =
      // `barras`; FUN_00279F60 lo sube hasta 5 y FUN_0027A0D0 mira si es 0) y refresca la columna de vida; 73 {pieza, otra} ( EU 0x293680 ->
      // FUN_00276AD0): las vidas de la otra. (Antes se tomaban por el indice de retrato `idx`: error.)
      if (o[1] == 72) pa->barras = std::clamp(o[3], 0, 5);
      if (o[1] == 73 && pb) pa->barras = pb->barras;
      REXLOG_INFO("dbz3 tablero b2: script {}: orden {} ({}, {}) a {}", k, o[1], o[2], o[3], Nombre(*pa));
      return espera;
    }
    // EU 0x294840 FUN_00294840
    // EU 0x270130 FUN_00270130
    // EU 0x270140 FUN_00270140
    if (o[1] == 38) {
      auto dentro = [](int k) {
        return k >= 0 && k < int(g_d.textos.size()) && g_d.textos[size_t(k)].tex >= 0 && g_d.textos[size_t(k)].py > 0 &&
               g_d.textos[size_t(k)].py < 448;
      };
      if (dentro(o[2]) || dentro(o[3])) {
        if (g_mensajes.empty()) {   // nueva ventana: D_CLEAR arranca su entrada
          g_mensaje_t = -10;
          g_msg_cerrando = false;
          g_msg_cierre = -10;
        }
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
    // EU 0x293740 FUN_00293740 (orden 70: capsula para el personaje de la pieza)
    if (o[1] == 70 || o[1] == 8) {   // capsula de premio con su ventana (ver g_premio_id)
      // 8 {grupo} (FUN_00295C10, b2.c:239395): igual que la 70 pero sin pieza (mascara = todos tus personajes); la usan
      // los guiones de despues de vencer a un Saibaman (fase I: 62/64 -> grupo 35).
      const int pj = o[1] == 8 ? -1 : PersonajeDe(o[3]);
      // EU 0x2835F0 FUN_002835f0 (capsula al azar del #SKA del grupo, usable por el personaje y no al maximo)
      const int id = dbz3::pasarela::nave::CapsulaAlAzar(o[2], pj);
      REXLOG_INFO("dbz3 tablero b2: script {}: orden {} (grupo {}, pieza {} = personaje {}): capsula {}{}", k, o[1], o[2],
                  o[1] == 8 ? 0 : o[3], pj, id, id ? (dbz3::pasarela::nave::CapsulaNueva(id) ? " nueva" : "") : " (ninguna: no hay ventana)");
      if (id > 0) {
        g_premio_id = id;
        g_premio_t = -10;
      }
      return espera;
    }
    // EU 0x292EA0 FUN_00292ea0 (83, ataque) / EU 0x292C00 FUN_00292c00 (84, defensa)
    // EU 0x292C00 FUN_00292c00
    if (o[1] == 83 || o[1] == 84) {   // "攻/防 +N" sobre la pieza (ver Subida)
      Subida s;
      s.n = o[2];
      s.signo = o[3];
      s.valor = o[4] & 0xFF;   // FUN_0027BD00: (u16)(a2 & 0xFF)
      s.defensa = o[1] == 84;
      s.snd110 = s.defensa ? 0x12 : 0x0C;   // 0x292EA0/0x292C00: sonido del canal 3 a los 110 cuadros (0x0C 攻 / 0x12 防)
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
    // EU 0x294A80 FUN_00294a80
    // EU 0x294A00 FUN_00294a00
    // EU 0x2949C0 FUN_002949c0
    // EU 0x293E20 FUN_00293e20
    // EU 0x293DE0 FUN_00293de0
    if (o[1] == 33 || o[1] == 35 || o[1] == 36 || o[1] == 53 || o[1] == 54) {
      // EU 0x27A4B0 FUN_0027a4b0
      // EU 0x27A6E0 FUN_0027a6e0
      // EU 0x276D10 FUN_00276d10
      // EU 0x276E10 FUN_00276e10
      // EU 0x276DE0 FUN_00276de0
      // 33 {pieza} (FUN_0027A4B0): estado de la pieza (+6 & 0xF) = 1, fuera del tablero hasta que la 32 la vuelva a
      //   poner (= 2, FUN_0027A6E0). 35/36 {pieza, n} (FUN_00276E10/00276DE0): +0x10 / +0x12 += n (ataque / defensa, los
      //   mismos que la daga y la armadura). 53/54 {pieza, n} (FUN_00276D10/00276CF0): +0x16 / +0x14 = n (ki y vida del
      //   siguiente combate: FUN_00279050 los copia al bloque del combate 0x69932A/0x69932C, b2.c:223404). El combate de B3
      //   todavia no recibe el ki ni la vida de la pieza (el consumidor, pasarela_misiones.cpp, no es de este fichero): se
      //   guardan y viajan en out[8..13], pero nadie los aplica. Sin medir el campo +0x16 de B2 no se inventa su escala.
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        if (o[1] == 33) {
          f.oculta = true;
          REXLOG_INFO("dbz3 tablero b2: script {}: orden 33: {} sale del tablero", k, Nombre(f));
        } else if (o[1] == 35 || o[1] == 36) {
          // DS45: +0x10/+0x12 son i16 y el original suma con desbordamiento de 16 bits (FUN_00276e10/00276de0)
          if (o[1] == 35) f.ataque = int16_t(f.ataque + int16_t(o[3]));
          else f.defensa = int16_t(f.defensa + int16_t(o[3]));
          REXLOG_INFO("dbz3 tablero b2: script {}: orden {}: {} {} {:+d} (ahora {})", k, o[1], Nombre(f),
                      o[1] == 35 ? "ataque" : "defensa", int16_t(o[3]), o[1] == 35 ? f.ataque : f.defensa);
        } else {
          // EU 0x276CF0 FUN_00276cf0 (vida +0x14; la 53 es EU 0x276D10, ki +0x16)
          (o[1] == 53 ? f.ki_b2 : f.vida_b2) = int16_t(o[3]);
          REXLOG_INFO("dbz3 tablero b2: script {}: orden {}: {} {} = {}", k, o[1], Nombre(f), o[1] == 53 ? "ki" : "vida",
                      int16_t(o[3]));
        }
      }
      return espera;
    }
    // EU 0x294660 FUN_00294660 (41, fase superada) / EU 0x294510 FUN_00294510 (42, final del Mundo Dragon) / 0x2944A0 (43)
    // EU 0x294510 FUN_00294510
    if (o[1] == 41 || o[1] == 42 || o[1] == 43) {   // fin de la fase (ver g_fase_sig)
      if (o[1] == 43) {
        g_estado = kPerdida;
        g_espera_x = false;   // DS131: la orden 39 pendiente no debe sobrevivir al fin de la fase
        BorrarEstadoFase();   // derrota: el reintento empieza la fase de cero
        REXLOG_INFO("dbz3 tablero b2: script {}: orden 43, fase {} perdida", k, g_d.fase + 1);
      } else {
        g_fase_sig = o[1] == 41 ? std::clamp(o[2], 0, kFases - 1) : -1;
        g_estado = kGanada;
        if (o[1] == 41) GuardarProgreso("tablero_b2_fase", g_fase_sig);
        // EU 0x238320 FUN_00238320
        GuardarBanderas(o[1] == 42);   // 0x238320 las guarda; la 42 borra la partida del Mundo Dragon
        BorrarEstadoFase();            // la fase acabo: el guardado intermedio (0x27D110) no pasa a la siguiente
        REXLOG_INFO("dbz3 tablero b2: script {}: orden {}, fase {} completada (siguiente {}, bolas {:#x})", k, o[1],
                    g_d.fase + 1, g_fase_sig + 1, g_bolas);
      }
      return espera;
    }
    // EU 0x2938A0 FUN_002938a0 (orden 69) -> EU 0x2A3DE0 FUN_002a3de0 (crea la tarea 0x2A2550 con el contador)
    // EU 0x2A3DE0 FUN_002a3de0
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
    // EU 0x2933B0 FUN_002933b0
    // EU 0x293630 FUN_00293630
    // 75 = 0x2935E0 (canal 3, una vez), 76 = 0x293590 (canal 4, una vez), 77 = 0x2934F0 (canal 0, en bucle cada a1 cuadros hasta el
    // fin de la escena), 78 = 0x293450 (canal 3, en bucle); 74 = 0x293630 y 79 = 0x2933B0 los otros dos de la tabla 0x379EC0
    // EU 0x2935E0 FUN_002935e0
    // EU 0x293590 FUN_00293590
    // EU 0x2934F0 FUN_002934f0
    // EU 0x293450 FUN_00293450
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
    // EU 0x293700 FUN_00293700 (orden 71: pista de musica en 0x6992DB)
    if (o[1] == 71) {   // musica (uRam006992DB = a0)
      g_musica = o[2];
      REXLOG_INFO("dbz3 tablero b2: script {}: musica {} (ADX_US {})", k, o[2], PistaB2(o[2]));
      return espera;
    }
    // EU 0x293140 FUN_00293140
    if (o[1] == 82) {   // variante del Goku del tablero (ELF 0x293140): CMN 339/340 + bit 0x04 de 0x6992DC
      VarianteGoku();
      REXLOG_INFO("dbz3 tablero b2: script {}: orden 82 (variante de Goku)", k);
      return espera;
    }
    // EU 0x276D30 FUN_00276d30
    // studies 2 G04: orden 52 {pieza, a0, a1} (FUN_00276D30): +0x19 (byte) = a1 (forma) y +0x1A (word) = a0 (traje),
    // los que el combate copia a 0x699322/0x699342. Se conserva la anchura del registro (byte/word); la forma se
    // traduce en Combate/FormaTableroB3 y las absorciones de Bu (c47/48/50/51) llevan su traje de B3
    // (TrajeAbsorcionB2), no el indice crudo de B2.
    // EU 0x293E60 FUN_00293e60
    if (o[1] == 52) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        f.forma_b2 = o[4] & 0xFF;
        f.traje_b2 = o[3] & 0xFFFF;
        REXLOG_INFO("dbz3 tablero b2: B2 orden 52: pieza={} forma={} traje={}", o[2], f.forma_b2, f.traje_b2);
      }
      return espera;
    }
    // EU 0x276D60 FUN_00276d60
    // studies 2 G04: orden 51 {pieza, a} (FUN_00276D60): +0x18 (byte) = a. El combate copia el +0x18 del rival a
    // 0x699365 (COM del rival). Se guarda por ficha y se entrega a pasarela_misiones con b2::ComRivalB2().
    // EU 0x293EA0 FUN_00293ea0
    if (o[1] == 51) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        f.com_b2 = o[3] & 0xFF;
        REXLOG_INFO("dbz3 tablero b2: B2 orden 51: pieza={} com={}", o[2], f.com_b2);
      }
      return espera;
    }
    // EU 0x276D80 FUN_00276d80 (orden 50: +0x1C/+0x1D del registro = tipo y dato de movimiento)
    // EU 0x293EE0 FUN_00293ee0 -> EU 0x276D80 FUN_00276d80: orden 50, tipo de movimiento y su dato
    if (o[1] == 50) {
      for (Ficha& f : g_f) {
        if (f.n != o[2]) continue;
        f.mov = o[3];
        f.mov_p = o[4];
        REXLOG_INFO("dbz3 tablero b2: script {}: {} pasa al movimiento {} ({})", k, Nombre(f), f.mov, f.mov_p);
      }
      return espera;
    }
    // EU 0x276CD0 FUN_00276cd0 (fila de sprites del Saibaman: +0x1E del registro)
    // EU 0x293D20 FUN_00293d20
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
// EU 0x2960C0 FUN_002960c0 (arranque del interprete de guiones: fija la tabla y el hilo de entrada)
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
// camara (9, 11: a una pieza). El banco SI es el del original: FUN_0023B900/FUN_0026ED50 dibujan sobre uRam00698DC4, que es
// el #AMA 343+5k de la fase (las texturas, uRam00698DA4, son DATA_<idioma> 146+k); d.textos es justo ese #AMA y PonerEfecto
// usa su animacion y duracion (max(+0x20,+0x24) = FUN_00245C00). La pieza sembrada sale de su agujero (DibPieza) y las demas
// aparecen con el destello de su brote.
// Camara de los guiones (ELF: ordenes 9-12 = 0x295BD0.. con paso lineal FUN_0023ACB0, 5 px por cuadro y por eje hasta
// llegar; 13-16 = suave FUN_0023AF30, 1/12 de la distancia por cuadro, durante a1 cuadros). 9/10/13/14 van a la casilla
// a0 (la tabla de 0x34 B de la cabecera +0x24 = T1), 11/12/15/16 a la pieza a0. Se queda alli mientras corren los guiones.
int g_cam_pieza = -1, g_cam_casilla = 0;
bool g_cam_script = false, g_cam_lineal = false;
ImVec2 g_cam_desv(0, 0);   // ordenes 10/12/14/16: el punto sale en (412, 200) de la pantalla en vez de en el centro (320, 224)
float g_cam_hasta = -10;
ImVec2 PosFicha(const Ficha& f);
// Efectos de los guiones (b2.c). Cada orden crea una tarea cuyo +0x34 = a0 (animacion del banco uRam00698DC4 = #AMA 343+5k),
// +0x38/+0x3C = a1/a2 y +0x40 = a3; el tiempo +0x30 sube fGpffff866c por cuadro. Familias (todas dibujan con el mismo
// parametro 0xB000/0xAA00 de 0x379EA0):
//   en un punto del mapa (a1, a2) FUN_0023B900:  17 EU 0x295860 una vez | 20 EU 0x295590 en bucle hasta la orden 37 
//     23 EU 0x2952A0 a3 veces | 26 EU 0x294FC0 una vez, bloquea | 29 EU 0x294CE0 a3 veces, bloquea
//   en la casilla a1 (FUN_00273AD0): 18 EU 0x295780 | 21 EU 0x2954A0 | 24 EU 0x2951B0 | 27 EU 0x294EE0 | 30 EU 0x294BE0
//   en la pieza a1 (FUN_0027A790):   19 EU 0x2956A0 | 22 EU 0x2953B0 | 25 EU 0x2950C0 | 28 EU 0x294E00 | 31 EU 0x294AE0
//   en un punto de la pantalla de 640x448 (a1, a2) FUN_0023B640: 45 EU 0x294360 | 46 EU 0x294250 | 47 EU 0x294140 
//     48 EU 0x294040 (bloquea) | 49 EU 0x293F20 (bloquea)
// "a3 veces": la tarea resta 1 a +0x40 en CADA cuadro con el tiempo ya pasado de la duracion (el tiempo no se reinicia), asi
// que dura la animacion + a3 cuadros (no es un retraso: el port lo usaba como retraso). Bloquean las ordenes con 1 en la
// tabla 0x37A020 (26-31, 48, 49): el guion espera a que la tarea ponga uGpffff8db8 = 1 al acabar.
struct Efecto {
  int k = 0;
  float x = 0, y = 0, t0 = 0, dur = 0;
  bool bucle = false, pantalla = false;   // bucle: hasta la orden 37; pantalla: x, y de la pantalla de 640x448 (sin camara)
};
std::vector<Efecto> g_efectos;
float PonerEfecto(int k, float x, float y, float retraso, int extra = 0, bool bucle = false, bool pantalla = false) {
  if (k < 0 || k >= int(g_d.textos.size())) return 0;
  const float cuadros = std::max(1.0f, g_d.textos[size_t(k)].dur);
  const float dur = (bucle ? 1e9f : cuadros + float(extra)) / 60.0f;
  if (g_efectos.size() < 500) g_efectos.push_back({k, x, y, g_ahora + retraso, dur, bucle, pantalla});
  return bucle ? 0.0f : retraso + dur;
}
// EU 0x294970 FUN_00294970 orden 37: pone uGpffff8dd4 = 1 durante un cuadro (+0x34 pasa de 0 a 1 y al siguiente cuadro lo
// baja y se borra): las tareas en bucle (20-22, 46) lo ven y se borran.
void FinBucleEfectos() {
  g_efectos.erase(std::remove_if(g_efectos.begin(), g_efectos.end(), [](const Efecto& e) { return e.bucle; }), g_efectos.end());
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
    // EU 0x2929A0 FUN_002929a0
    case 4: case 5: case 6:   // FUN_002929A0: #SPX de CMN 450 / 612 / 943 + a0 = ventana de mensaje M/P/T (AbrirMensajeSpx)
      // M11 (mod opcional b2.efectos_spx): las ventanas de mensaje de las ordenes 4/5/6 las ensena el port desde
      // R7/b2/INFORME_shenron.md; el mod solo las enciende o apaga. Sin el mod instalado vale el defecto del port
      // (salen); con el instalado manda su casilla (activado: salen; desactivado: no, se espera su tiempo).
      // (Para que el defecto sin mod sea no verlas: cambiar por `if (!dbz3::opcional::Activo("b2.efectos_spx")) return 0.3f;`.)
      if (dbz3::opcional::Desactivado("b2.efectos_spx")) return 0.3f;
      AbrirMensajeSpx(o[1], o[2]);
      return 0.3f;
    // EU 0x295D50 FUN_00295d50
    case 7:   // espera (cuadros de 60 Hz); DS45: el original suma el delta de cuadro hasta a0, sin tope
      return std::max(0.0f, float(o[2]) / 60.0f);
    // EU 0x295BD0 FUN_00295bd0
    // EU 0x295B90 FUN_00295b90
    // EU 0x295B50 FUN_00295b50
    // EU 0x295B10 FUN_00295b10
    // EU 0x295AA0 FUN_00295aa0
    // EU 0x295A30 FUN_00295a30
    // EU 0x2959C0 FUN_002959c0
    // EU 0x295950 FUN_00295950
    // EU 0x273050 FUN_00273050
    // EU 0x273000 FUN_00273000
    // EU 0x276EC0 FUN_00276ec0
    // EU 0x276E80 FUN_00276e80
    // EU 0x273100 FUN_00273100
    // EU 0x2730B0 FUN_002730b0
    // EU 0x276F10 FUN_00276f10
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
      // 0x295BD0/0x295B90/0x295B50/0x295B10 (9-12, lineal FUN_0023ACB0) y 0x295AA0/0x295A30/0x2959C0/0x295950 (13-16, suave
      // FUN_0023AF30): 9, 11, 13, 15 centran el punto (FUN_00273050/00276EC0/00273100/00276FF0: esquina = punto - pantalla/2);
      // 10, 12, 14, 16 (FUN_00273000/00276E80/002730B0/00276F10) lo dejan en el sitio (412, 200) de la pantalla: esquina = punto - (412, 200)
      g_cam_desv = o[1] % 2 == 0 ? ImVec2(320.0f - 412.0f, 224.0f - 200.0f) : ImVec2(0, 0);
      meta.x += g_cam_desv.x;
      meta.y += g_cam_desv.y;
      g_cam_script = true;
      g_cam_lineal = o[1] <= 12;
      // DS45: el original espera a que la camara llegue (0x295BD0 -> FUN_00273050/00273000) o a que pasen a1 cuadros
      // (0x295AA0... suave); sin topes. 5 px por cuadro = 300 px/s.
      if (g_cam_lineal)
        return std::max(std::fabs(meta.x - g_cam_x), std::fabs(meta.y - g_cam_y)) / 300.0f;
      return std::max(0.0f, float(o[3]) / 60.0f);
    }
    // EU 0x295860 FUN_00295860
    // EU 0x295590 FUN_00295590
    // EU 0x2952A0 FUN_002952a0
    // EU 0x294FC0 FUN_00294fc0
    // EU 0x294CE0 FUN_00294ce0
    // EU 0x295780 FUN_00295780
    // EU 0x2954A0 FUN_002954a0
    // EU 0x2951B0 FUN_002951b0
    // EU 0x294EE0 FUN_00294ee0
    // EU 0x294BE0 FUN_00294be0
    // EU 0x2956A0 FUN_002956a0
    // EU 0x2953B0 FUN_002953b0
    // EU 0x2950C0 FUN_002950c0
    // EU 0x294E00 FUN_00294e00
    // EU 0x294AE0 FUN_00294ae0
    // EU 0x294360 FUN_00294360
    // EU 0x294250 FUN_00294250
    // EU 0x294140 FUN_00294140
    // EU 0x294040 FUN_00294040
    // EU 0x293F20 FUN_00293f20
    case 17: case 20: case 23: case 26: case 29: case 21: case 24: case 27: case 30: case 18: case 22: case 25: case 28: case 31:
    case 19: case 45: case 46: case 47: case 48: case 49: {
      const int op = o[1];
      const bool pant = op >= 45;
      const int fam = pant ? 3 : (op - 17) % 3;   // 0 punto, 1 casilla, 2 pieza (17-19, 20-22, ...: de tres en tres)
      const int var = pant ? (op == 45 ? 0 : op == 46 ? 1 : op == 47 ? 2 : op == 48 ? 3 : 4) : (op - 17) / 3;   // 0 una vez, 1 bucle, 2 a3 veces, 3 una vez+bloquea, 4 a3 veces+bloquea
      const bool bucle = var == 1, cuenta = var == 2 || var == 4, bloquea = var == 3 || var == 4;
      const int extra = cuenta ? std::max(1, o[5]) : 0;
      float x = float(o[3]), y = float(o[4]);
      bool hay = true;
      // EU 0x273AD0 FUN_00273ad0: centro (x, y) de la casilla de la tabla iGpffff88b8 +0x24
      if (fam == 1) {   // casilla a1 (FUN_00273AD0)
        hay = o[3] > 0 && o[3] < int(g_d.casillas.size());
        if (hay) x = g_d.casillas[size_t(o[3])].x, y = g_d.casillas[size_t(o[3])].y;
      } else if (fam == 2) {   // pieza a1 (FUN_0027A790)
        hay = false;
        for (Ficha& f : g_f)
          if (f.n == o[3]) {
            hay = true;
            if (op == 19 || op == 28) Aparecer(f, "brota");
            x = PosFicha(f).x, y = PosFicha(f).y;
          }
      }
      const float d = hay ? PonerEfecto(o[2], x, y, 0, extra, bucle, pant) : 0.0f;
      return bloquea ? d : 0.0f;   // DS45: el original espera la senal de fin de la tarea, sin tope de 6 s
    }
    case 37:   // 0x294970 FUN_00294970: fin de los efectos en bucle
      FinBucleEfectos();
      return 0;
    // EU 0x294AB0 FUN_00294ab0
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

// EU 0x27F7E0 FUN_0027f7e0
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


// EU 0x295F20 FUN_00295f20 (bucle del guion: ordenes del mismo paso y espera a las que bloquean)
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
// EU 0x273C30 FUN_00273c30
// EU 0x273D60 FUN_00273d60
// Paso de un rival segun su tipo de movimiento (ELF 0x378FE0): 0 quieto, 2 a la casilla mov_p, 3 al azar, 4 persigue a la
// ficha del jugador mas cercana (los 9 tipos medidos en R7/b2/re_ia.md §3; ya no queda ninguno "por descifrar").
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
// EU 0x27CC30 FUN_0027cc30 (casilla del objeto valido mas cercano, 0 = ninguno)
int Marca(int tipo, int desde) {
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

// EU 0x235E70 FUN_00235e70
// EU 0x237F70 FUN_00237f70
// EU 0x279360 FUN_00279360
// EU 0x276FF0 FUN_00276ff0
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
  // EU 0x235FD0 FUN_00235fd0
  g_rival_paso = -1;   // 0x235FD0: la fase enemiga arranca pasados 24 cuadros
  g_rival_t = g_ahora;
  for (Ficha& f : g_f)
    // EU 0x278EE0 FUN_00278ee0
    if (f.rival && f.c != kSaibaman) f.acciones = f.acc_turno;   // 0x278EE0
  g_dest = -1;
  REXLOG_INFO("dbz3 tablero b2: fase enemiga del turno {}", g_turno);
}
void TurnoJugador() {
  g_fase_rival = false;
  g_cam_rival = -1;
  ++g_turno;
  g_rombo_t = g_turno_t = g_ahora;   // el rombo (D_TURN) vuelve a entrar con el turno nuevo (0x237F70 espera a que acabe)
  REXLOG_INFO("dbz3 tablero b2: turno {}", g_turno);
  // EU 0x27F9A0 FUN_0027f9a0: comprobador de disparadores del INICIO de turno (0x237F70 estado 1, tras sumar 1 al turno): devuelve el guion de
  // la PRIMERA fila de T3 que cumple: A = -1 y (B = 0 o hay una pieza en juego en la casilla B: FUN_00278800), o A != -1 y B = la casilla de
  // ESA pieza A (FUN_00278E20(A), no la de la pieza activa: desensamblado 0x27F9D8 `lw a0,0(s0)` -> `beq a0,-1` -> `jal 0x9e388` ->
  // 0x27F9E8 `lw s3,4(s0)` -> 0x27F9EC `beq v0,s3`); ademas turno de la fila (+8) = 0 o el actual,
  // y todas sus banderas. (Las filas con A = -1 no las mira FUN_0027F7E0, el de despues de cada paso: SucesosDeCasilla.) Antes solo valian las
  // de A = -1, B = 0 y turno exacto, y se lanzaban todas.
  {
    for (const auto& t : g_d.t3) {
      bool ok;
      if (t[0] == -1) {
        ok = t[1] == 0;
        for (const Ficha& f : g_f)
          // FUN_00278800(0, B) != 0: la pieza 0 tiene bando 0, asi que devuelve -1 si en B hay una pieza de otro bando y 1
          // si es del 0; en los dos casos != 0 = hay alguna pieza en juego en B (lo mismo que `Activa && casilla == B`).
          if (!ok && Activa(f) && f.casilla == t[1]) ok = true;
      } else {
        // EU 0x27F9A0 FUN_0027f9a0: A != -1 = "la pieza A esta en la casilla B" (FUN_00278E20 lee +8 del registro de ESA pieza;
        // el turno de la fila (+8) se mira aparte, 0 = cualquier turno).
        ok = false;
        for (const Ficha& f : g_f)
          if (f.n == t[0] && f.casilla == t[1]) { ok = true; break; }
      }
      if (ok && (t[2] == 0 || t[2] == g_turno) && Condiciones(t, 5)) {
        REXLOG_INFO("dbz3 tablero b2: turno {}: disparador de inicio de turno, script {}", g_turno, t[3]);
        Guion(t[3], 0);
        break;
      }
    }
  }
  for (Ficha& f : g_f) {
    // EU 0x278F90 FUN_00278f90
    if (f.jugador) f.movs = f.movs_turno;   // 0x278F90 FUN_00278F90: +2 = +3 al empezar el turno del jugador
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
    if (g_rival_i >= g_f.size()) {
      // EU 0x26FDD0 FUN_0026fdd0 (0x237F70 estado 0: pone +0x40 = 0 en el rombo, que pasa a su salida, anim 0x28 = 40) y 0x237F70 estado 1:
      // espera a que el rombo este oculto (FUN_0026FDC0 == 0) antes de sumar el turno
      if (g_rombo_fin < 0) g_rombo_fin = g_ahora;
      const float d40 = g_d.hud_ama.size() > 40 ? std::max(1.0f, g_d.hud_ama[40].dur) : 10.0f;
      if (g_ahora - g_rombo_fin < d40 / 60.0f) return;
      g_rombo_fin = -1;
      return TurnoJugador();
    }
    Ficha& r = g_f[g_rival_i];
    // EU 0x279E30 FUN_00279e30
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
      // EU 0x235C30 FUN_00235c30
      // s9b: el rival que no se mueve tambien pasa por el final de su accion (FUN_00235c30 -> FUN_00236830, caso 6 ->
      // FUN_0027F7E0): los disparadores de casilla (T3) se miran en la casilla en la que se queda. Asi el Cell de la fase V,
      // puesto en la 7 por el guion 303, arranca su patrulla (guion 333) y deja libre el unico paso hacia la 22.
      if (p <= 0) {
        SucesosDeCasilla(r);
        return;
      }
      g_rival_lucha = FichaEnHueco0(p, false);   // entra en la casilla y, al llegar, combate (0x278260: hueco 0)
      EmpezarPaso(r, p, g_ahora);
      if (g_rival_lucha >= 0) {   // contacto: FUN_00235C30 usa 0x2738A0 (se queda a 60 px del jugador) en vez de 0x273800
        const Casilla &ca = g_d.casillas[size_t(r.desde)], &cb = g_d.casillas[size_t(p)];
        r.t_dur = std::max(0.1f, float(PasosContacto(std::hypot(cb.x - ca.x, cb.y - ca.y))) / 60.0f);
        r.corta = 60.0f;   // se para a 60 px del jugador (PosFicha); el combate la recoloca al resolverse
      }
      g_rival_espera = TiempoPaso(r);
      REXLOG_INFO("dbz3 tablero b2: {} a la casilla {}", Nombre(r), p);
      if (g_rival_lucha < 0) SucesosDeCasilla(r);
      return;
    }
    if (g_ahora - g_rival_t < std::max(kAnda, g_rival_espera)) return;
    // EU 0x27A180 FUN_0027a180
    if (r.acciones > 0) --r.acciones;   // 0x27A180 resta UNA accion (+2 del registro, si > 0); antes: a 0 (solo valia con 1 accion por turno)
    g_rival_paso = 0;
    g_rival_i = 0;   // FUN_00279360(0x20) vuelve a elegir la primera pieza rival en juego con acciones: la misma si le quedan
    if (g_rival_lucha >= 0) {
      const int j = g_rival_lucha;
      g_rival_lucha = -1;
      g_pend_mov = int(&r - g_f.data());   // se movio el rival: la camara del contacto lo sigue a el
      return Contacto(j, int(&r - g_f.data()));
    }
  }
}
// Ordenes del guion que cambian el registro de pieza, cada tarea leida (tabla 0x379EC0; las funciones van a EU 0x27xxxx):
//   32 EU 0x294AB0 -> FUN_0027A6E0 | 33 EU 0x294A80 -> FUN_0027A4B0 | 35 EU 0x294A00 -> FUN_00276E10 (ataque) | 36 EU 0x2949C0 -> FUN_00276DE0
//   (defensa) | 51 EU 0x293EA0 -> FUN_00276D60 (+0x18 COM) | 52 EU 0x293E60 -> FUN_00276D30 (+0x19 forma, +0x1A traje) | 53 EU 0x293E20 ->
//   FUN_00276D10 (+0x16 ki) | 54 EU 0x293DE0 -> FUN_00276CF0 (+0x14 vida) | 55 EU 0x293DA0 -> FUN_0027A370 | 56 EU 0x293D20 -> FUN_00276CD0
//   (+0x1E modo Saibaman) | 57 EU 0x293D60 -> FUN_0027A3F0 | 72 EU 0x2936C0 -> FUN_00276B30 (+1 vidas) | 73 EU 0x293680 -> FUN_00276AD0 (vidas de otra)
// y las que esperan: 66 EU 0x2939C0 (FUN_00277670 + FUN_002774F0 hasta llegar). Todas estan en `Orden` / `OrdenEscena` de abajo. EU 0x2360B0 FUN_002360B0 (resolucion tras un combate del tablero), estado a estado frente al port:
//   0 espera +0x38 y lanza los disparadores T5 (FUN_0027F3A0: 4/1 si el ganador es la pieza elegida, 3/2 si no; 4 y 3 si el
//     perdedor se queda sin vidas: CERRADO, ver Resultado) y el guion (FUN_002960C0): equivale a `SucesosDeCombate` [VERIFICAR: que los tipos 1-4 de T5 se
//     elijan igual: el port decide por ganador/perdedor].
//   1 FUN_00277BB0 (reaccion del perdedor): coincide con g_goku_reac_t / g_ficha_lucha (ver DibPieza).
//   2 espera al perdedor fuera del tablero; si la bandera 0x69B7C5 (+1 de la pieza 1 = vidas de Goku) llega a 0 va a la escena 10
//     (fin de partida, FUN_00232E10(10, 0)): COINCIDE con `kPerdida` ("Goku ha caido"); si no, elige la siguiente pieza activa
//     (FUN_00279360(0x10)) y recoloca al ganador: CERRADO con FUN_002778F0/00278400 -> la pieza del hueco 1 anda al 0
//     (EmpezarVuelta/ActualizarVuelta; FUN_00277470 = g_vuelta) y el turno espera a que llegue.
//   3 si la pieza que se movio cambio de casilla y hay un rival en su casilla (FUN_00278320: hueco 0/2) y es la activa, nuevo
//     contacto (FUN_00277890 a las dos; estado 0x236420): CERRADO con g_2contacto (FichaEn prefiere el hueco 0; el que acaba
//     de luchar queda en el 1 y no se repite). El segundo contacto vuelve a mirar T4 (FUN_00236420 estado 0x14) -> SegundoContacto.
void FinDeAccion() {
  if (g_vuelta || g_vuelta_pend >= 0 || g_2contacto >= 0) return;   // recolocacion andando / segundo contacto (0x277470, 0x2320): el turno espera
  if (g_pend_j >= 0 || g_estado != kJuego || g_fase_rival) return;
  GuardarEstadoFase();   // EU 0x27D010/0x27D110: guardado intermedio de objetos, buscados y Bolas al cerrar la accion
  if (!QuedanMovs()) TurnoRival();
  else if (g_f[size_t(g_sel)].movs <= 0) SiguienteSel();
}
// Alfombra de Mr. Popo (ver g_alf): recoloca la pieza al acabar la animacion de la alfombra y cierra la accion 24 cuadros
// despues (mismo orden que FUN_00236830: caso 4 recoloca, caso 5 espera 0x18 y sigue). Se llama cada cuadro.
// EU 0x27A370 FUN_0027a370 (recoloca la pieza en la casilla de la alfombra; el hueco libre lo elige FUN_00278A30 y
// FUN_00273950 lo pasa a x/y: en el port lo hacen RecalcularSlots y PosFicha con el mismo criterio de primer hueco)
void ActualizarAlfombra() {
  if (g_alf.i < 0) return;
  Ficha& f = g_f[size_t(g_alf.i)];
  if (g_ahora >= g_alf.t_mueve && f.casilla != g_alf.dest) {
    f.desde = f.casilla;
    f.casilla = g_alf.dest;
    f.anim_b2 = -1;   // al bajar de la alfombra vuelve al reposo
    f.anim_estado = 0;
    RecalcularSlots();
    REXLOG_INFO("dbz3 tablero b2: Mr. Popo deja a {} en la casilla {}", Nombre(f), f.casilla);
  }
  if (g_ahora < g_alf.t_fin) return;
  g_alf.i = -1;
  REXLOG_INFO("dbz3 tablero b2: Mr. Popo termina con {} en la casilla {}", Nombre(f), f.casilla);
  // EU 0x236830 FUN_00236830 caso 5 -> 6: tras recolocar solo se miran los disparadores (FUN_0027F7E0); los objetos se
  // recogen en el caso 0, en la casilla de llegada (antes de la alfombra), no en la de destino
  SucesosDeCasilla(f);
  FinDeAccion();
}

int g_cont_j = -1, g_cont_r = -1;   // contacto pendiente: la pieza anda hasta pegarse al rival y entonces sale el guion
float g_cont_t = 0;
// EU 0x235930 FUN_00235930 (tarea del paso del jugador: anda a la casilla, mira contacto con FUN_00278260 y lanza T4/T5)
void Mover(int i, int dest) {
  Ficha& f = g_f[size_t(i)];
  --f.movs;
  g_dest = -1;
  // EU 0x278260 FUN_00278260 (rival del bando contrario en la casilla de destino, en juego y en el hueco 0: el primero de la casilla por el orden de la tabla)
  // EU 0x2365D0 FUN_002365d0 (tarea del contacto del jugador: anda hasta el rival, lo encara y lanza el guion/combate)
  if (const int r = FichaEnHueco0(dest, true); r >= 0) {
    g_pend_mov = i;   // se movio el jugador: la camara del contacto lo sigue a el (FUN_002365D0 b2.c:189388)
    // A2c: en el original la pieza anda hasta pegarse al rival (a ~70 px de el) y SOLO entonces salen el "!!" y el guion de contacto
    const ImVec2 O = PosFicha(f), R = PosFicha(g_f[size_t(r)]);
    const float dx = R.x - O.x, dy = R.y - O.y, d = std::hypot(dx, dy);
    // 0x2738A0: la pieza anda PasosContacto(distancia entre los centros de las casillas) pasos de 4 px hacia el rival, o sea que se
    // queda a unos 60 px de el (antes: 70, a ojo del emulador)
    const Casilla &ca = g_d.casillas[size_t(f.casilla)], &cb = g_d.casillas[size_t(dest)];
    const int pasos = PasosContacto(std::hypot(cb.x - ca.x, cb.y - ca.y));
    if (d > 4.0f * float(pasos) + 1.0f && pasos > 0) {
      const float k = std::min(1.0f, 4.0f * float(pasos) / d), dur = IrAPunto(f, 65, O.x + dx * k, O.y + dy * k, g_ahora);
      g_cont_j = i;
      g_cont_r = r;
      g_cont_t = g_ahora + dur + 0.15f;
      return;
    }
    return Contacto(i, r);
  }
  // studies 8 (E): encuentro con un aliado. Los contactos de T4 van por personaje sin mirar el bando (FUN_0027F510) y los
  // #YDW traen filas entre dos piezas del jugador (fase I: {1, 2, 0, 401, .., -101}: Goku con Piccolo -> guion 401 =
  // giro, "攻 +10%" con las ordenes 35 + 83, bandera 101 para que no se repita). El original tambien lo dispara entre dos
  // piezas del jugador: FUN_0027F7E0 (b2.c:226915), el comprobador de T4 tras el paso (FUN_00236830 caso 6), filtra por
  // pieza/personaje y banderas, pero NO por bando. La comprobacion de contacto de combate (FUN_00278260) si exige bando
  // distinto, y por eso el aliado no inicia combate. La pieza sigue andando como antes.
  if (const int al = FichaEn(dest, false); al >= 0 && al != i) {
    SucesosDeContacto(f, g_f[size_t(al)]);
    g_suceso = -1;   // sin combate: que no lo herede el siguiente
  }
  EmpezarPaso(f, dest, g_ahora);
  g_dest = -1;
  REXLOG_INFO("dbz3 tablero b2: turno {}: {} a la casilla {}", g_turno, Nombre(f), dest);
  const Casilla& c = g_d.casillas[size_t(dest)];
  // EU 0x236830 FUN_00236830 caso 0: FUN_0027C3E0 recoge los objetos de la casilla de llegada ANTES de Dende / la alfombra (caso 3)
  for (Objeto& o : g_obj) {
    if (EnSuelo(o) && o.casilla == f.casilla) Coger(f, o);
  }
  // EU 0x279F60 FUN_00279f60
  if (c.tipo == 2) {   // Dende: vida llena y una barra mas (FAQ: "+1"); FUN_00272DA0 tipo 2, sonido canal 3 id 0x0B
    f.vida = 0;
    f.barras = std::min(f.barras_max, f.barras + 1);
    SonidoCanal3(0x0B);
    Aviso(T::T("Dende te cura.", "Dende heals you."));
  // EU 0x272FB0 FUN_00272fb0: casilla tipo 3 (alfombra) -> +0x14 = su destino
  } else if (c.tipo == 3 && c.destino > 0) {   // alfombra de Mr. Popo
    // EU 0x27A2B0 FUN_0027A2B0 (desde FUN_0027A370, caso 4 de FUN_00236830): si la casilla de destino esta bloqueada
    // (FUN_00278A30 == -1: casilla 0, una pieza del otro bando o el hueco 1 ocupado) se prueban sus vecinas en orden
    // (FUN_00273210) hasta una libre. Antes: sin alfombra si habia un rival en el destino.
    int destino = c.destino;
    auto bloqueada = [&](int cas) {
      if (cas <= 0) return true;
      for (const Ficha& o : g_f)
        if (&o != &f && Activa(o) && o.casilla == cas && (o.rival != f.rival || o.slot == 1)) return true;
      return false;
    };
    for (int n = 0, vistas = 0; bloqueada(destino) && vistas < 8; ++vistas) {   // [H] orden de las vecinas = el de `vec` del T1
      // EU 0x273210 FUN_00273210 (vecinas de la casilla en el orden de la rejilla)
      const int cual = g_d.casillas[size_t(c.destino)].vec[n % 4];
      ++n;
      if (cual > 0) destino = cual;
    }
    if (!bloqueada(destino)) {
    // FUN_00272DA0: animaciones 12-15 en la casilla de la alfombra y sonido canal 3 id 0x0E; FUN_0027A370 recoloca y el
    // caso 5 de FUN_00236830 espera 24 cuadros. La pieza se queda en la alfombra hasta que acaba la animacion.
    const float anda = TiempoPaso(f), d = DuracionAlfombra();
    // EU 0x272DA0 FUN_00272da0 (efectos 12-15 de la alfombra y sonido canal 3 0x0E; FUN_0027A370 recoloca)
    for (int k = 12; k <= 15; ++k) PonerEfecto(k, c.x, c.y, anda);
    SonidoCanal3(0x0E);
    g_alf.i = i;
    g_alf.dest = destino;
    g_alf.t_mueve = g_ahora + anda + d;
    g_alf.t_fin = g_alf.t_mueve + 25.0f / 60.0f;   // caso 5: +0x38 sube 1 por cuadro hasta pasar de 0x18 = 25 cuadros de 60 Hz
    Aviso(T::T("Mr. Popo te lleva en su alfombra.", "Mr. Popo carries you on his carpet."), 16);
    REXLOG_INFO("dbz3 tablero b2: Mr. Popo: {} a la alfombra de la casilla {} ({:.2f} s de animacion) y luego a la {} (+24 cuadros)",
                Nombre(f), dest, d, destino);
    return;   // el FinDeAccion lo hace ActualizarAlfombra al acabar
    }
  }
  for (Objeto& o : g_obj) {
    if (EnSuelo(o) && o.casilla == f.casilla) Coger(f, o);
  }
  SucesosDeCasilla(f);
  FinDeAccion();
}

// EU 0x236AD0 FUN_00236ad0 (busqueda: una por casilla; saca al rival enterrado o el primer objeto escondido)
void Buscar(int i) {
  Ficha& f = g_f[size_t(i)];
  if (g_buscada[size_t(f.casilla)]) return Aviso(T::T("Aquí ya has buscado.", "You already searched here."));
  --f.movs;
  g_dest = -1;   // sale del modo destino: la camara vuelve a la ficha (FUN_00236AD0 b2.c:189566)
  g_marca = f.casilla;
  g_marca_icono = 7;   // "?!"
  g_marca_t = g_ahora;
  // EU 0x277AF0 FUN_00277af0 (primer estado de la busqueda: animacion 4)
  // EU 0x277860 FUN_00277860 (estado visual 0xC de la busqueda)
  PonerPose(f, 4, 0x0C);
  // FUN_00277D40 (b2.c:222525-222541): la busqueda pone la animacion 3; solo para c==0 llama a
  // FUN_002463D0(0x42140000, 3, 0x15), que acaba en FUN_00147260(3, 0, 21, 0). No depende de movimientos restantes.
  SonidoBuscarB2(f);
  REXLOG_INFO("dbz3 tablero b2: turno {}: {} busca en la casilla {}", g_turno, Nombre(f), f.casilla);
  for (size_t k = 0; k < g_f.size(); ++k) {
    Ficha& s = g_f[k];
    if (s.oculta && !s.fuera && s.casilla == f.casilla) {
      s.oculta = false;
      s.rival = true;
      g_pend_mov = i;   // el que se movio (buscar) es el que sigue la camara del contacto
      // EU 0x277A90 FUN_00277a90 (estado 0xB y animacion 8 antes del contacto con el rival enterrado)
      PonerPose(f, 8, 0x0B);
      return Contacto(i, int(k));
    }
  }
  // una busqueda = el primer enterrado que queda (ELF 0x27C090); sin mas, la casilla queda buscada (en gris)
  auto queda = [&](const Objeto& o) { return o.casilla == f.casilla && Escondido(o); };
  if (auto it = std::find_if(g_obj.begin(), g_obj.end(), queda); it != g_obj.end()) {
    Coger(f, *it, true);
  } else {
    Aviso(T::T("No hay nada.", "Nothing here."));
    f.anim_b2 = -1;   // busqueda sin nada: vuelve al reposo (si no, la pose 4/0xC se quedaba congelada)
    f.anim_estado = 0;
  }
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
// jefe (por su casilla) y, si no se puede llegar, al rival mas cercano. DS23: la tabla kB2aB3 cubre los 53 de B2, asi
// que ya no se salta a nadie por "solo de B2" (los que no existen en B3 usan su donante y su nombre del ELF).
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
        // un rival que no se puede combatir (ID fuera de la tabla, o peligroso para esta pieza) tapa su casilla
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
      // Un rival en la casilla de la pieza elegida es un ataque, no un destino (EU 0x236EA0 FUN_00236ea0:
      // FUN_00278320 != 0 -> EU 0x236420 FUN_00236420). Es lo que hace un jugador y lo unico que cierra la
      // fase 3: con Freezer/Vegeta en la casilla 4, el destino solo se buscaba en otras casillas y no habia ninguno.
      // Se mantiene la guardia de las fases con trampa (V: un contacto que enciende 301/302 pierde la fase).
      const int ocup = FichaEn(f.casilla, true);
      if (ocup >= 0 && !Peligroso(f, g_f[size_t(ocup)]) && AtacarOcupante(g_sel)) return;
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
std::vector<Tex> g_tmapa, g_tfichas, g_ticonos, g_tadornos, g_tloc, g_thud, g_tmpt, g_tayuda;
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
// escenas: FUN_002929A0 carga el #SPX de DATA_CMN (450 / 612 / 943 + id) y de su bytecode salen la entrada del texto y las
// caras de cada linea (AbrirMensajeSpx, abajo), asi que valen las M/P/T de las 9 fases sin tabla fija. El visor de sucesos
// de la orden 41 (FUN_00270A50, b2.c:217936-217950) es otra ventana y sigue sin portar; NO usa DATA_<idioma> 289
// (medido: 289 es el #AMT de mensajes de memory card, 53 textos). FUN_00270550/00270460/00270660 (b2.c:217677-217791)
// consumen el valor dinamico de 0x69b710+0x54 (y, en el visor triple, +0x714/+0x718) como argumento de FUN_0026e020
// y lo limpian. En el decomp no aparece una tabla fija de ids: los suministra el guion/VM; no se pueden listar desde una
// constante ni mapear a DATA 289. Nota estatica: localizar IDs requeriria seguir la VM y cada guion, no copiar una tabla.
// DS43: estado de una celda de la tabla 0x691CB0 que el guion #SPX deja montada (patron del #AMA, capa, posicion y
// banderas). Lo llena `spx::Maquina` al llegar al texto (ver `spx::Salida`) y lo pinta `DibEscena` en vez de la maqueta fija.
struct MptCelda {
  int bloque = 0, entrada = 0, patron = 0, flags = 0;
  float ancho = 0, x = 0, y = 0, capa = 0;
};
struct Escena {
  bool abierta = false, teatro = false, comic = false, bustos = false;   // comic: panel de las ventanas P/T (CMN 793 + 791)
  std::vector<Tex> img;
  std::vector<int> textos, fondos;   // indices de img: lineas de texto y fondos (teatro)
  size_t paso = 0;
  float t0 = 0;
  int numero = 0;                 // entrada de DATA_<idioma>
  std::vector<int> hablan;        // charla: ID de B2 de quien dice cada linea (-1 = sin cara)
  std::vector<int> voces;         // entrada de ADX_US.AFS de la voz de cada linea (builtin 0x159 del #SPX; -1 = sin voz)
  int voz_paso = -1;              // linea cuya voz ya se mando
  // DS43: celdas de sprite del guion (0x691CB0) y los recursos que pidio (ruta del .ama / .amt por bloque).
  std::vector<MptCelda> celdas;
  std::array<std::string, 8> recursos_ama, recursos_amt;
  std::array<int, 8> celda_ama{{-1, -1, -1, -1, -1, -1, -1, -1}}, celda_amt{{-1, -1, -1, -1, -1, -1, -1, -1}};
  float voz_fin = 0;              // segundos desde t0 en que acaba la voz (duracion real del ADX); 0 = sin voz
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
  esc.bustos = esc.comic && letra == 'P';   // las P llevan a Goku y al rival en 3D: modelo de B3 + banco de B2 y su #AMC (DS23)
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

// ======================================================================================================
// MAQUINA VIRTUAL #SPX (el interprete de guiones de las ventanas de mensaje M/P/T de B2)
// Traduccion de las 9 funciones de instancia (0x14C3F0-0x14C680), del bucle FUN_0014C740, del lanzador de hilos
// FUN_0014FA50, de los decodificadores de operandos (0x14F310-0x14F890) y de los 99 manejadores de operacion de las
// tablas 0x34D980.. (indice = op>>4, entrada = op&15). Un hilo es un bloque de 0x80 bytes: r0..r7 de proposito general,
// r8 = base del codigo, r9 = pc, r10 = destino (etiqueta de reinicio, op 0x90), r11 = SP, r12/r13 = punteros de
// variables del guion, r14 = zona de datos, r15 = fondo de pila; f0..f15 en +0x40. Los 71 builtins (op 0x80) son las
// funciones del juego (texto, voz, camara, modelos...); aqui solo se ejecutan los que dan los datos de la ventana
// (texto, voz, region) y los demas devuelven 1 sin hacer nada (se anotan en `sin_nativo`, mirar el log).
// Medido en los 187 guiones (89 M, 50 P, 48 T): todos llegan a su texto (builtin 0x157) y a su voz (0x159).
// Auditoria LUNA_B2R0 (DIFIERE estructurales de la VM): 0x14C3F0 reserva una instancia de 0x5C en un pool, 0x14C4F0
// libera sus recursos, 0x14C740 ejecuta hasta ceder, 0x14FA50 usa 16 ranuras fijas y 0x14F310 escribe en la memoria real
// del motor. El port reimplementa la VM con una Maquina por guion, hilos con pila propia y un mapa de memoria; mantiene
// la semantica observable (decodificacion, saltos, esperas y builtins), no el almacenamiento interno de PS2.
// ======================================================================================================
namespace spx {
constexpr uint32_t kBase = 0x24;           // FUN_0014C550: base del codigo = cabecera + *(cabecera+0x14)
constexpr uint32_t kPila = 0x1000;         // tamano de la pila de un hilo (manager+0x40)
// 0x14CD10 (opcode 7) usa la conversion float->entero de PowerPC (fctiwz): trunca hacia cero, satura fuera de rango y
// devuelve 0x80000000 con NaN. Un `int32_t(f)` de C++ no define esos casos (auditoria LUNA_B2R0).
static int32_t Fctiwz(float f) {
  if (std::isnan(f)) return INT32_MIN;
  if (f >= 2147483647.0f) return INT32_MAX;
  if (f <= -2147483648.0f) return INT32_MIN;
  return int32_t(f);
}
constexpr int kCuadrosMotor = 0;   // con la espera real del builtin 0x104 el ritmo ya sale bien (voces 483..488 en M001)
constexpr uint32_t kVars = 0x10000;        // r13 de los hilos (manager+0x28)
struct Hilo {
  int32_t r[16] = {};
  float f[16] = {};
  uint32_t pc = 0;
  int espera = 0;
  bool fin = false;
};
struct Dst {   // destino de un operando: registro entero (0), registro float (1) o memoria (2)
  int tipo = 0;
  uint32_t v = 0;
};
struct Salida {
  int texto = -1;                                  // builtin 0x157: entrada del AFS de idioma
  std::vector<int> caras;                          // [r13+4] por linea ([r13])
  std::vector<std::array<int, 3>> voces;           // builtin 0x159: {entrada, AFS, canal}
  std::vector<int> voz_linea;                      // entrada de ADX_US de cada linea ([r13] en el momento de la llamada)
  std::vector<int> nativos_ignorados;              // builtins que no se ejecutan
  int cuadros = 0;
  // DS43: lo que el guion monta para pintar la ventana (no lo que el port dibujaba a mano).
  std::vector<MptCelda> celdas;                    // tabla 0x691CB0: celdas activas en el momento del texto
  std::array<std::string, 8> ama, amt;             // rutas que pide el guion para cada bloque (0x2257B0 / 0x225590)
  std::array<int, 8> id_ama{{-1, -1, -1, -1, -1, -1, -1, -1}};   // entradas que pide por numero (0x224AC0 / 0x224A30)
  std::array<int, 8> id_amt{{-1, -1, -1, -1, -1, -1, -1, -1}};
  struct SpriteEsp {                               // tabla 0x68ED90 (texto y caras de la ventana)
    int slot = 0, imagen = -1, cuadro0 = 0, cuadro1 = 0, flags = 0;
    float x = 320, y = 224, ancho = 0, alto = 0, recorte[4] = {0, 0, 0, 0};
    bool activo = false;
  };
  std::vector<SpriteEsp> sprites;
  std::vector<std::string> rutas;                  // recursos que pide el guion por ruta (0x228F60/0x228CB0/0x228AA0)
  int voz_espera = 0;                              // cuadros que el guion espero por 0x84 (fin real del ADX)
};

class Maquina {
 public:
  explicit Maquina(const std::vector<uint8_t>& guion) : d_(guion) {}
  Salida sal;
  uint16_t pad = 0;   // botones pulsados en este cuadro (0x3B43AE+0xB8*jugador): lo lee el builtin 0x90
  // DS43: duracion en cuadros de una entrada de ADX_US.AFS (la pone CorrerSpx, que si tiene la ISO). La usa el builtin
  // 0x84 (FUN_00227310, espera al fin de la voz) para esperar lo que dura el ADX de verdad, no un numero fijo.
  std::function<int(int)> duracion_voz;

  // EU 0x14C3F0 FUN_0014c3f0
  // Reservar una instancia: aqui la instancia es la propia Maquina (estado 1 = en uso; el juego tiene un pool de 0x5C bytes).
  bool Reservar() {
    if (d_.size() < 0x28 || std::memcmp(d_.data(), "#SPX", 4) != 0) return false;
    estado_ = 1;
    return true;
  }
  // EU 0x14C4F0 FUN_0014c4f0
  void Parar() {   // FUN_00148650(...,1): mata las tareas de la instancia
    for (auto& h : hilos_) h.fin = true;
  }
  // EU 0x14C550 FUN_0014c550
  void PonerGuion() {   // instancia+4 = cabecera, +8 = base = cabecera + *(cabecera+0x14)
    base_ = U32(0x14);
  }
  // EU 0x14C590 FUN_0014c590
  void PonerTablaExt(int tabla) { tabla_ext_ = tabla; }   // 0x379640 en las ventanas de mensaje (las funciones se despachan en Nativo)
  // EU 0x14C5C0 FUN_0014c5c0
  void PonerGancho48(int g) { g48_ = g; }   // 0x281060 (M) / 0x280f90 (P, T): se llama antes de lanzar la entrada
  // EU 0x14C5F0 FUN_0014c5f0
  void PonerGancho4C(int g) { g4c_ = g; }   // 0x292920 (M) / 0x292960 (P, T): al morir el ultimo hilo
  // EU 0x14C620 FUN_0014c620
  void PonerGancho50(int g) { g50_ = g; }   // 0x280d70: al crear un hilo
  // EU 0x14C650 FUN_0014c650
  void PonerGancho54(int g) { g54_ = g; }   // 0x280d20: al terminar un hilo
  // EU 0x14C680 FUN_0014c680
  void Lanzar(int entrada) {   // hilo en cabecera+8+tabla[entrada]
    Crear(base_ + U32(0x20 + 4 * uint32_t(entrada)), 0);
    estado_ = 2;
  }

  // Un cuadro del juego: cada hilo ejecuta hasta que cede (FUN_0014c740 una vez por tarea).
  void Cuadro() {
    ++sal.cuadros;
    for (auto& b : bloques_)   // FUN_001a7ce0: cada cuadro avanza el fotograma de las celdas activas
      for (auto& e : b.ent)
        if (e.activa) e.cuadro += 1.0f;
    const size_t n = hilos_.size();   // los hilos nacidos en este cuadro empiezan en el siguiente
    for (size_t i = 0; i < n; ++i) {
      Hilo& h = hilos_[i];
      if (h.fin) continue;
      if (h.espera > 0) {
        --h.espera;
        continue;
      }
      Ejecutar(h);
    }
  }
  bool Vivo() const {
    for (const auto& h : hilos_)
      if (!h.fin) return true;
    return false;
  }

 private:
  // EU 0x14C740 FUN_0014c740
  void Ejecutar(Hilo& h) {
    int pasos = 0;
    for (;;) {
      if (h.pc >= d_.size() || ++pasos > 20000) {
        if (pasos > 20000) REXLOG_INFO("dbz3 spx: hilo sin ceder en {:#x}", h.pc);
        h.fin = true;
        return;
      }
      const uint8_t op = d_[h.pc++];
      uint32_t cod = Op(h, op);
      while (cod != 1) {
        const uint32_t c = cod & 0xff, val = cod >> 8;
        switch (c) {
          case 2:   // espera `val` cuadros, pila vacia y vuelta a la etiqueta del hilo
            h.espera = int(val) + kCuadrosMotor;  
            h.r[11] = h.r[15] + int32_t(kPila);
            h.pc = uint32_t(h.r[10]);
            return;
          case 3:
            h.espera = int(val);
            return;
          // EU 0x14CAD0 FUN_0014cad0 (cola de FUN_0014CA20: el hilo termina y su ranura queda libre)
          case 4:   // FUN_001485f0: el hilo termina
            h.fin = true;
            return;
          case 5:   // reinicio: este hilo muere y nace otro en r0 con dato r1
            h.fin = true;
            Crear(uint32_t(h.r[0]), h.r[1]);
            return;
          case 7:   // builtin (op 0x80): grupo = val>>4, indice = val&15
            cod = Nativo(h, int(val));
            break;
          default:
            return;
        }
      }
    }
  }
  // EU 0x14FA50 FUN_0014fa50
  void Crear(uint32_t pc, int32_t /*dato*/) {
    int vivos = 0;
    for (const auto& x : hilos_) vivos += !x.fin;
    if (vivos >= 16 || hilos_.size() > 4000) {   // 16 ranuras de 0x80 bytes en manager+0x34
      REXLOG_INFO("dbz3 spx: sin ranuras para otro hilo");
      return;
    }
    hilos_.emplace_back();
    Hilo& h = hilos_.back();
    const int ranura = vivos;
    h.r[8] = int32_t(base_);
    h.r[12] = int32_t(kVars + 0x10000);
    h.r[13] = int32_t(kVars);
    h.r[14] = int32_t(0x30000 + ranura * 0x400);
    h.r[15] = int32_t(0x40000 + ranura * kPila);
    h.r[10] = int32_t(pc);
    h.r[11] = h.r[15] + int32_t(kPila);
    h.pc = pc;
  }

  uint32_t U32(uint32_t o) const {
    uint32_t v = 0;
    if (o + 4 <= d_.size()) std::memcpy(&v, &d_[o], 4);
    return v;
  }
  int32_t Rd(uint32_t a) const {
    auto it = mem_.find(a);
    return it == mem_.end() ? 0 : it->second;
  }
  void Wr(uint32_t a, int32_t v) {
    mem_[a] = v;
    // las lineas: [r13] = numero de linea, [r13+4] = cara (ID de B2 de la hoja T_CHARA en las M; "motion id" en las P)
    if (a == kVars + 4 && v != -1) {
      const int linea = std::max(0, Rd(kVars));
      if (linea > 63) return;
      if (size_t(linea) >= sal.caras.size()) sal.caras.resize(size_t(linea) + 1, -1);
      sal.caras[size_t(linea)] = v;
    }
  }
  static float AF(int32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
  }
  static int32_t AB(float f) {
    int32_t b;
    std::memcpy(&b, &f, 4);
    return b;
  }
  uint8_t Byte(Hilo& h) { return h.pc < d_.size() ? d_[h.pc++] : uint8_t(0); }
  int32_t Imm(Hilo& h, int n) {
    int64_t v = 0;
    for (int i = 0; i < n; ++i) v |= int64_t(Byte(h)) << (8 * i);
    if (n == 1) return int8_t(v);
    if (n == 2) return int16_t(v);
    return int32_t(v);
  }
  uint32_t Dir(Hilo& h, int modo, int n) {   // modos 4-7 (direccion de memoria)
    if (modo == 4) return uint32_t(h.r[n]);
    if (modo == 5) return uint32_t(h.r[n] + Imm(h, 1));
    if (modo == 6) return uint32_t(h.r[n] + Imm(h, 2));
    const int m = Byte(h) >> 4;
    return uint32_t(h.r[n] + h.r[m]);
  }
  // EU 0x14F340 FUN_0014f340
  int32_t LeeI(Hilo& h) {   // operando entero (valor)
    const uint8_t b = Byte(h);
    const int modo = b >> 4, n = b & 15;
    if (modo == 0) return h.r[n];
    if (modo == 1) return Imm(h, 1);
    if (modo == 2) return Imm(h, 2);
    if (modo == 3) return Imm(h, 4);
    return Rd(Dir(h, modo, n));
  }
  // EU 0x14F6D0 FUN_0014f6d0
  Dst DstI(Hilo& h) {   // operando entero (destino)
    const uint8_t b = Byte(h);
    const int modo = b >> 4, n = b & 15;
    if (modo == 0) return {0, uint32_t(n)};
    if (modo == 1) return {2, uint32_t(Imm(h, 1))};
    if (modo == 2) return {2, uint32_t(Imm(h, 2))};
    if (modo == 3) return {2, uint32_t(Imm(h, 4))};
    return {2, Dir(h, modo, n)};
  }
  // EU 0x14F4F0 FUN_0014f4f0
  float LeeF(Hilo& h) {   // operando flotante (valor). Leido del ensamblado: los modos 1/2/3 cargan el inmediato (i8/i16 con signo, i32) como BITS de float (sw + lwc1), no como numero
    const uint8_t b = Byte(h);
    const int modo = b >> 4, n = b & 15;
    if (modo == 0) return h.f[n];
    if (modo == 1) return AF(Imm(h, 1));
    if (modo == 2) return AF(Imm(h, 2));
    if (modo == 3) return AF(Imm(h, 4));
    return AF(Rd(Dir(h, modo, n)));
  }
  // EU 0x14F890 FUN_0014f890
  Dst DstF(Hilo& h) {   // operando flotante (destino)
    const uint8_t b = Byte(h);
    const int modo = b >> 4, n = b & 15;
    if (modo == 0) return {1, uint32_t(n)};
    if (modo == 1) return {2, uint32_t(Imm(h, 1))};
    if (modo == 2) return {2, uint32_t(Imm(h, 2))};
    if (modo == 3) return {2, uint32_t(Imm(h, 4))};
    return {2, Dir(h, modo, n)};
  }
  // EU 0x14F310 FUN_0014f310
  void Esc(Hilo& h, Dst d, int32_t bits) {   // copia de 4 bytes al destino
    if (d.tipo == 0) h.r[d.v & 15] = bits;
    else if (d.tipo == 1) h.f[d.v & 15] = AF(bits);
    else Wr(d.v, bits);
  }
  int32_t LeeD(Hilo& h, Dst d) {
    if (d.tipo == 0) return h.r[d.v & 15];
    if (d.tipo == 1) return AB(h.f[d.v & 15]);
    return Rd(d.v);
  }
  void Push(Hilo& h, int32_t v) {
    h.r[11] -= 4;
    Wr(uint32_t(h.r[11]), v);
  }
  int32_t Pop(Hilo& h) {
    const int32_t v = Rd(uint32_t(h.r[11]));
    h.r[11] += 4;
    return v;
  }
  bool PilaVacia(const Hilo& h) const { return uint32_t(h.r[11]) >= uint32_t(h.r[15]) + kPila; }
  // EU 0x14DD70/0x14DE30/0x14DEF0/0x14DFB0/0x14E070/0x14E130/0x14E1F0 usan la constante 6.2831855f del ELF
  // (no la de doble precision): con el mismo valor el truncado a entero coincide en los casos frontera.
  static constexpr float kDosPi = 6.2831855f;
  // EU 0x14CEF0 / EU 0x14CF60: 0x10/0x11 no suman al entero: recorren los 4 bytes del destino con acarreo/préstamo.
  void SumaBytes(Hilo& h, const Dst& d, int delta) {
    uint32_t v = uint32_t(LeeD(h, d));
    for (int i = 0; i < 4; ++i) {
      const uint32_t pos = uint32_t(i) * 8u;
      const uint32_t b = ((v >> pos) + uint32_t(delta)) & 0xFFu;
      v = (v & ~(0xFFu << pos)) | (b << pos);
      if (delta > 0 ? b != 0 : b != 0xFFu) break;
    }
    Esc(h, d, int32_t(v));
  }

  uint32_t Op(Hilo& h, uint8_t op) {
    const uint32_t base = uint32_t(h.r[8]);
    switch (op) {
      case 0x00: return 1;   // opcode 0: sin efecto (la liberacion de ranura de FUN_0014CAD0 va con el fin del hilo)
      // EU 0x14CAE0 FUN_0014cae0
      case 0x01: { const int32_t i = LeeI(h); Esc(h, DstI(h), i); return 1; }
      // EU 0x14CB50 FUN_0014cb50
      case 0x02: Esc(h, DstI(h), 0); return 1;
      // EU 0x14CB90 FUN_0014cb90
      case 0x03: { const float f = LeeF(h); Esc(h, DstF(h), AB(f)); return 1; }
      // EU 0x14CC00 FUN_0014cc00
      case 0x04: Esc(h, DstF(h), AB(0.0f)); return 1;
      // EU 0x14CC40 FUN_0014cc40
      case 0x05: Esc(h, DstF(h), AB(1.0f)); return 1;
      // EU 0x14CC90 FUN_0014cc90
      case 0x06: { const int32_t i = LeeI(h); Esc(h, DstF(h), AB(float(i))); return 1; }
      // EU 0x14CD10 FUN_0014cd10 (float -> entero con fctiwz: truncado hacia cero, saturacion y NaN = 0x80000000)
      case 0x07: { const float f = LeeF(h); Esc(h, DstI(h), Fctiwz(f)); return 1; }
      // EU 0x14CD90 FUN_0014cd90
      case 0x08: Push(h, LeeI(h)); return 1;
      // EU 0x14CDE0 FUN_0014cde0
      case 0x09: Push(h, AB(LeeF(h))); return 1;
      // EU 0x14CE30 FUN_0014ce30
      case 0x0A: Esc(h, DstI(h), Pop(h)); return 1;
      // EU 0x14CE90 FUN_0014ce90
      case 0x0B: Esc(h, DstF(h), Pop(h)); return 1;
      // EU 0x14CEF0 FUN_0014cef0 (0x10: +1 byte a byte con acarreo)
      case 0x10: { const Dst d = DstI(h); SumaBytes(h, d, +1); return 1; }
      // EU 0x14CF60 FUN_0014cf60 (0x11: -1 byte a byte con prestamo mientras el byte quede en 0xFF)
      case 0x11: { const Dst d = DstI(h); SumaBytes(h, d, -1); return 1; }
      // EU 0x14CFE0 FUN_0014cfe0
      case 0x12: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, int32_t(uint32_t(LeeD(h, d)) + uint32_t(i))); return 1; }
      // EU 0x14D080 FUN_0014d080
      case 0x13: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, int32_t(uint32_t(LeeD(h, d)) - uint32_t(i))); return 1; }
      // EU 0x14D120 FUN_0014d120
      case 0x14: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, int32_t(uint32_t(LeeD(h, d)) * uint32_t(i))); return 1; }
      // EU 0x14D1C0 FUN_0014d1c0
      case 0x15: { const int32_t i = LeeI(h); const Dst d = DstI(h); const int32_t a = LeeD(h, d); Esc(h, d, i != 0 && !(a == INT32_MIN && i == -1) ? a / i : 0); return 1; }
      // EU 0x14D280 FUN_0014d280
      case 0x16: { const int32_t i = LeeI(h); const Dst d = DstI(h); const int32_t a = LeeD(h, d); Esc(h, d, i != 0 && !(a == INT32_MIN && i == -1) ? a % i : 0); return 1; }
      // EU 0x14D340 FUN_0014d340
      case 0x17: { const Dst d = DstI(h); Esc(h, d, int32_t(0u - uint32_t(LeeD(h, d)))); return 1; }
      // EU 0x14D3B0 FUN_0014d3b0
      case 0x18: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, int32_t(std::pow(float(LeeD(h, d)), float(i)))); return 1; }   // FUN_0011d9d0 -> FUN_0011b128 = powf(base, exponente) (devuelve 1.0 si el exponente es 0)
      // EU 0x14D470 FUN_0014d470
      case 0x20: { const Dst d = DstF(h); Esc(h, d, AB(AF(LeeD(h, d)) + 1.0f)); return 1; }
      // EU 0x14D4F0 FUN_0014d4f0
      case 0x21: { const Dst d = DstF(h); Esc(h, d, AB(AF(LeeD(h, d)) - 1.0f)); return 1; }
      // EU 0x14D570 FUN_0014d570
      case 0x22: { const float f = LeeF(h); const Dst d = DstF(h); Esc(h, d, AB(f + AF(LeeD(h, d)))); return 1; }
      // EU 0x14D620 FUN_0014d620
      case 0x23: { const float f = LeeF(h); const Dst d = DstF(h); Esc(h, d, AB(AF(LeeD(h, d)) - f)); return 1; }
      // EU 0x14D6D0 FUN_0014d6d0
      case 0x24: { const float f = LeeF(h); const Dst d = DstF(h); Esc(h, d, AB(f * AF(LeeD(h, d)))); return 1; }
      // EU 0x14D780 FUN_0014d780
      case 0x25: { const float f = LeeF(h); const Dst d = DstF(h); Esc(h, d, AB(f != 0 ? AF(LeeD(h, d)) / f : 0.0f)); return 1; }
      // EU 0x14D840 FUN_0014d840
      case 0x26: { const float f = LeeF(h); const Dst d = DstF(h); const int32_t b = int32_t(f); Esc(h, d, AB(b != 0 ? float(int32_t(AF(LeeD(h, d))) % b) : 0.0f)); return 1; }
      // EU 0x14D930 FUN_0014d930
      case 0x27: { const Dst d = DstF(h); Esc(h, d, AB(-AF(LeeD(h, d)))); return 1; }
      // EU 0x14D9B0 FUN_0014d9b0
      case 0x28: { const float f = LeeF(h); const Dst d = DstF(h); Esc(h, d, AB(std::pow(AF(LeeD(h, d)), f))); return 1; }   // powf (FUN_0011d9d0)
      // EU 0x14DA60 FUN_0014da60
      case 0x29: { const Dst d = DstF(h); Esc(h, d, AB(std::sqrt(AF(LeeD(h, d))))); return 1; }
      // EU 0x14DAE0 FUN_0014dae0
      case 0x30: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, LeeD(h, d) & i); return 1; }
      // EU 0x14DB90 FUN_0014db90
      case 0x31: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, LeeD(h, d) | i); return 1; }
      // EU 0x14DC40 FUN_0014dc40
      case 0x32: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, LeeD(h, d) ^ i); return 1; }
      // EU 0x14DCF0 FUN_0014dcf0
      case 0x33: { const Dst d = DstI(h); Esc(h, d, ~LeeD(h, d)); return 1; }
      // 0x40-0x46: trigonometria en vueltas de 65536. Identificadas por su cuerpo: FUN_0011d7e0 seno, d540 coseno, d8c8 tangente, d988 arcoseno
      // (serie de FUN_0011a7e0), d970 arcocoseno (pi/2 - asin), d270 arcotangente, d9a0 atan2(y, x) (FUN_0011ab80: devuelve pi para y=0,x<0)
      // EU 0x14DD70 FUN_0014dd70
      case 0x40: { const int32_t i = LeeI(h); Esc(h, DstF(h), AB(std::sin(float(uint16_t(i)) * kDosPi / 65536.f))); return 1; }
      // EU 0x14DE30 FUN_0014de30
      case 0x41: { const int32_t i = LeeI(h); Esc(h, DstF(h), AB(std::cos(float(uint16_t(i)) * kDosPi / 65536.f))); return 1; }
      // EU 0x14DEF0 FUN_0014def0
      case 0x42: { const int32_t i = LeeI(h); Esc(h, DstF(h), AB(std::tan(float(uint16_t(i)) * kDosPi / 65536.f))); return 1; }
      // EU 0x14DFB0 FUN_0014dfb0
      case 0x43: { const float f = LeeF(h); Esc(h, DstI(h), int32_t(std::asin(f) * 65536.f / kDosPi) & 0xFFFF); return 1; }
      // EU 0x14E070 FUN_0014e070
      case 0x44: { const float f = LeeF(h); Esc(h, DstI(h), int32_t(std::acos(f) * 65536.f / kDosPi) & 0xFFFF); return 1; }
      // EU 0x14E130 FUN_0014e130
      case 0x45: { const float f = LeeF(h); Esc(h, DstI(h), int32_t(std::atan(f) * 65536.f / kDosPi) & 0xFFFF); return 1; }
      // EU 0x14E1F0 FUN_0014e1f0
      // La funcion auxiliar de atan2 (FUN_0011ab80) es una utilidad de la biblioteca, no una funcion del modo; va aparte.
      case 0x46: { const float a = LeeF(h); const float b = LeeF(h); h.r[0] = int32_t(std::atan2(a, b) * 65536.f / kDosPi) & 0xFFFF; return 1; }
      // EU 0x14E2A0 FUN_0014e2a0
      case 0x50: { const int32_t a = LeeI(h); const int32_t b = LeeI(h); h.r[0] = -int32_t(b < a); return 1; }
      // EU 0x14E310 FUN_0014e310
      case 0x51: h.r[0] = -int32_t(LeeI(h) != 0); return 1;
      // EU 0x14E360 FUN_0014e360
      case 0x52: { const float a = LeeF(h); const float b = LeeF(h); h.r[0] = -int32_t(b < a); return 1; }
      // EU 0x14E3E0 FUN_0014e3e0
      case 0x53: h.r[0] = -int32_t(LeeF(h) != 0.0f); return 1;
      // EU 0x14E440 FUN_0014e440
      case 0x60: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, int32_t(uint32_t(LeeD(h, d)) << (i & 31))); return 1; }
      // EU 0x14E4F0 FUN_0014e4f0
      case 0x61: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, int32_t(uint32_t(LeeD(h, d)) >> (i & 31))); return 1; }
      // EU 0x14E5A0 FUN_0014e5a0
      case 0x62: { const int32_t i = LeeI(h); const Dst d = DstI(h); Esc(h, d, LeeD(h, d) >> (i & 31)); return 1; }
      // EU 0x14E650 FUN_0014e650
      case 0x63: { const int32_t i = LeeI(h); const Dst d = DstI(h); const uint32_t a = uint32_t(LeeD(h, d)); const int s = (i > 0 ? i : -i) & 31; if (i != 0 && s) Esc(h, d, int32_t(i > 0 ? (a << s | a >> (32 - s)) : (a >> s | a << (32 - s)))); return 1; }
      // EU 0x14E7D0 FUN_0014e7d0
      case 0x64: { const int32_t i = LeeI(h); const Dst d = DstI(h); const uint32_t a = uint32_t(LeeD(h, d)); const int s = (i > 0 ? i : -i) & 31; if (i != 0 && s) Esc(h, d, int32_t(i > 0 ? (a >> s | a << (32 - s)) : (a << s | a >> (32 - s)))); return 1; }
      // EU 0x14E950 FUN_0014e950
      case 0x70: h.pc = base + uint32_t(LeeI(h)); return 1;
      // EU 0x14E9A0 FUN_0014e9a0
      case 0x71: { const int32_t c = LeeI(h); const int32_t a = LeeI(h); if (c != 0) h.pc = base + uint32_t(a); return 1; }
      // EU 0x14EA20 FUN_0014ea20
      case 0x72: { const int32_t c = LeeI(h); const int32_t a = LeeI(h); if (c == 0) h.pc = base + uint32_t(a); return 1; }
      // EU 0x14EAA0 FUN_0014eaa0
      case 0x73: { const int32_t a = LeeI(h); Push(h, int32_t(h.pc)); h.pc = base + uint32_t(a); return 1; }
      // EU 0x14EB10 FUN_0014eb10
      case 0x74: { const int32_t c = LeeI(h); const int32_t a = LeeI(h); if (c != 0) { Push(h, int32_t(h.pc)); h.pc = base + uint32_t(a); } return 1; }
      // EU 0x14EBA0 FUN_0014eba0
      case 0x75: { const int32_t c = LeeI(h); const int32_t a = LeeI(h); if (c == 0) { Push(h, int32_t(h.pc)); h.pc = base + uint32_t(a); } return 1; }
      // EU 0x14EC30 FUN_0014ec30
      case 0x76:   // ret: con la pila vacia devuelve 2 (el hilo vuelve a su etiqueta al cuadro siguiente)
        if (PilaVacia(h)) return 2;
        h.pc = uint32_t(Pop(h));
        return 1;
      // EU 0x14EC90 FUN_0014ec90
      case 0x77: { const int32_t a = LeeI(h); if (PilaVacia(h)) return 2; h.pc = uint32_t(a); h.r[11] += 4; return 1; }
      // EU 0x14ED30 FUN_0014ed30
      case 0x78: { const int32_t c = LeeI(h); if (c != 0) { if (PilaVacia(h)) return 2; h.pc = uint32_t(Pop(h)); } return 1; }
      // EU 0x14EDE0 FUN_0014ede0
      case 0x79: { const int32_t c = LeeI(h); const int32_t a = LeeI(h); if (c != 0) { if (PilaVacia(h)) return 2; h.pc = uint32_t(a); h.r[11] += 4; } return 1; }
      // EU 0x14EEA0 FUN_0014eea0
      case 0x7A: { const int32_t c = LeeI(h); if (c == 0) { if (PilaVacia(h)) return 2; h.pc = uint32_t(Pop(h)); } return 1; }
      // EU 0x14EF50 FUN_0014ef50
      case 0x7B: { const int32_t c = LeeI(h); const int32_t a = LeeI(h); if (c == 0) { if (PilaVacia(h)) return 2; h.pc = uint32_t(a); h.r[11] += 4; } return 1; }
      // EU 0x14F010 FUN_0014f010
      case 0x80: return uint32_t(LeeI(h)) * 0x100u + 7u;   // builtin: el bucle despacha grupo = I>>4, indice = I&15
      // EU 0x14F040 FUN_0014f040
      case 0x90: h.r[10] = int32_t(base + uint32_t(LeeI(h))); return 1;
      // EU 0x14F090 FUN_0014f090
      case 0x91: { const int32_t a = LeeI(h); Crear(base + uint32_t(a), Rd(uint32_t(h.r[11]) + 4)); return 1; }
      // EU 0x14F120 FUN_0014f120
      case 0x98: { const int32_t a = LeeI(h); const int32_t dato = LeeI(h); Crear(base + uint32_t(a), dato); return 1; }
      // EU 0x14F1D0 FUN_0014f1d0
      case 0x92: return 3;
      // EU 0x14F1E0 FUN_0014f1e0
      case 0x93: { const int32_t i = LeeI(h); return 3u | uint32_t(std::max(i - 1, 0)) << 8; }
      // EU 0x14F220 FUN_0014f220
      case 0x94: return 2;
      // EU 0x14F230 FUN_0014f230
      case 0x95: return 4;
      // EU 0x14F240 FUN_0014f240
      case 0x96: { const int32_t a = LeeI(h); h.r[0] = int32_t(base + uint32_t(a)); h.r[1] = Rd(uint32_t(h.r[11]) + 4); return 5; }
      // EU 0x14F300 FUN_0014f300
      case 0x97: return 6;
      // EU 0x14F290 FUN_0014f290
      case 0x99: { const int32_t a = LeeI(h); h.r[0] = int32_t(base + uint32_t(a)); h.r[1] = LeeI(h); return 5; }
      default:
        REXLOG_INFO("dbz3 spx: operacion {:#04x} desconocida en {:#x}", op, h.pc - 1);
        h.fin = true;
        return 4;
    }
  }

  // Builtins (op 0x80): FUN_xxxx de las tablas 0x379210..0x3795a0 (despacho en FUN_0014c740 case 7). Argumentos = pila: a0 es el
  // ultimo empujado. Solo se ejecutan los que dan datos de la ventana; el resto es del motor del juego (modelos, camara, sonido)
  // y devuelve 1.
  uint32_t Nativo(Hilo& h, int cod) {
    const uint32_t sp = uint32_t(h.r[11]);
    auto a = [&](int k) { return Rd(sp + 4u * uint32_t(k)); };
    switch (cod) {
      // EU 0x2249A0 FUN_002249a0
      case 0x157:   // texto: FUN_0012D100(a2<<16 | a1): entrada a1 del AFS a2 (1 = el de idioma); r0 = a3; espera a que cargue
        if (a(2) == 1 && sal.texto < 0) sal.texto = a(1);
        h.r[0] = a(3);
        // El original devuelve 3 y espera a que la entrada del AFS este lista: FUN_00228E30 (sondeo) mira el estado
        // (+4, short) == 3, y entonces registra la imagen en la tabla de sprites 0x68ED90 (FUN_00228D10), la suelta y
        // devuelve 1. Aqui la lectura es inmediata, asi que se espera el mismo cuadro que tarda en quedar lista (1).
        // NO es una espera por X: 0x228E30 no lee el mando.
        // DS43: FUN_00228D10(bank, a0, imagen = a1, datos) -> la ranura a0 de 0x68ED90 queda con esa imagen (texto).
        {
          SpriteEst& s = Sprite(a(0));
          s.activo = true;
          s.imagen = a(1);
          s.cuadro0 = 0;
          s.cuadro1 = 0;
        }
        if (!instantanea_ && sal.cuadros > 120) Instantanea();   // guion sin voz: la ventana ya esta montada
        // DS43: se devuelve 1 (carga hecha) y NO 3. En este VM un 3 duerme un cuadro y continua DESPUES del builtin, y con
        // esa espera el guion perdia las lineas 2+ (medido: M001 daba 2 caras en vez de 6); el sondeo 0x228E30 del original
        // solo existe por la carga asincrona, que aqui es inmediata.
        return 1;
      // EU 0x2248A0 FUN_002248a0
      case 0x159:   // voz: FUN_0012BA00(canal a2, a1<<16 | a0); r0 = manejador (aqui 1)
        sal.voces.push_back({a(0), a(1), a(2)});
        if (const int l = Rd(kVars); l >= 0 && l < 64) {
          if (size_t(l) >= sal.voz_linea.size()) sal.voz_linea.resize(size_t(l) + 1, -1);
          if (sal.voz_linea[size_t(l)] < 0) sal.voz_linea[size_t(l)] = a(0);   // la primera de cada linea
        }
        ultima_voz_ = a(0);
        voz_canal_ = true;
        Instantanea();   // DS43: la ventana ya esta montada (celdas + recursos + sprites)
        h.r[0] = 1;
        // FUN_002248A0 devuelve 3 (arranca el ADX y el hilo espera un cuadro); aqui 1 por el mismo motivo que 0x157: el
        // arranque es inmediato y la espera real es 0x84, que dura lo que el ADX.
        return 1;
      // EU 0x228F60 FUN_00228f60 (0x24) / EU 0x228CB0 FUN_00228cb0 (0x25) / EU 0x228AA0 FUN_00228aa0 (0x26)
      // Carga un recurso por RUTA (base+a1) y lo registra en la tabla de sprites 0x68ED90 (sondeos 0x228E30/0x228B00/
      // 0x228880). En los guiones son la hoja del texto (P_MSG/M001MSG.AMT) y la voz (P_ADX/M001_NN.adx): los mismos
      // ficheros que ya entran por 0x157 y 0x159; aqui se anota la ruta y se sigue (la lectura es inmediata).
      case 0x24:
      case 0x25:
      case 0x26:
        sal.rutas.push_back(Cadena(a(1)));
        h.r[0] = a(2);
        return 1;
      // EU 0x227480 FUN_00227480
      case 0x83: {   // voz por ruta: FUN_0012B8C0(canal a1, base+a0, 0) + FUN_0012BEA0(handle, 1) (arranca el ADX)
        voz_ruta_ = Cadena(a(0));
        voz_canal_ = true;
        h.r[0] = 1;
        return 1;
      }
      // EU 0x227310 FUN_00227310
      case 0x84: {   // espera al fin de la voz del handle a0: FUN_0012BED0(handle) hasta que != 0
        // En este VM un nativo que devuelve 3 duerme N cuadros y sigue (el original vuelve a llamar al sondeo cada
        // cuadro): esperar de una vez la duracion real del ADX es equivalente.
        const int dur = duracion_voz ? std::max(duracion_voz(ultima_voz_), 0) : 0;
        sal.voz_espera += dur;
        voz_restante_ = dur;
        h.r[0] = 1;
        return dur > 0 ? 3u | uint32_t(dur - 1) << 8 : 1u;
      }
      // EU 0x227230 FUN_00227230
      case 0x85:   // corta la voz (FUN_0012BB70): deja el canal libre
        voz_restante_ = 0;
        voz_canal_ = false;
        h.r[0] = 1;
        return 1;
      // EU 0x2245C0 FUN_002245c0
      case 0x8C:   // r0 = FUN_0012BC60(a0): estado del canal de voz (0 libre; 1-4 cargando/listo; 5 acabado)
        // El estado es distinto de 0 desde que arranca la voz (0x159/0x83) hasta que se corta (0x85); al acabar NO pasa a
        // distinto de 0 por si solo (FUN_0015F870 devuelve el estado CRI y el guion avanza con el).
        h.r[0] = voz_canal_ ? 1 : 0;
        return 1;
      // EU 0x225990 FUN_00225990
      case 0x104:   // espera a0 unidades del reloj del guion (suma dt por cuadro: unidades de 1/60 s); sondeo 0x225930: (reloj - inicio + 0.5) < a0
        h.r[0] = 1;
        return 3u | uint32_t(std::max(a(0) - 1, 0)) << 8;
      // ---------------- DS43: celdas de sprite del guion (tabla 0x691CB0) ----------------
      // EU 0x2257B0 FUN_002257b0  (y 0x2257b0 por la tabla 0x10c): carga un recurso (a1 = ruta en el guion) en la celda
      // del bloque a2; el original lo deja en +0x04/+0x0c (el #AMA de los patrones). Devuelve a2 y espera el sondeo.
      case 0x10C:
      case 0x110: {
        BloqueCelda& b = Bloque(a(2));
        b.creado = true;
        b.ama = Cadena(a(1));
        h.r[0] = a(2);
        return 3;
      }
      // EU 0x225590 FUN_00225590 (y 0x10e): igual con el segundo recurso (a1 = ruta del .amt), celda +0x00/+0x08.
      case 0x10E:
      case 0x112: {
        BloqueCelda& b = Bloque(a(2));
        b.creado = true;
        b.amt = Cadena(a(1));
        h.r[0] = a(2);
        return 3;
      }
      // EU 0x224AC0 FUN_00224ac0: carga el #AMA por NUMERO de entrada (los guiones del interfaz usan esto, no la ruta):
      // a0 no se usa, a1 = entrada del AFS, a2 = 0 DATA_CMN / 1 idioma, a3 = bloque de la celda; r0 = a3. Sondeo 0x225930.
      case 0x155: {
        BloqueCelda& b = Bloque(a(3));
        b.creado = true;
        b.id_ama = a(1);
        b.afs_ama = a(2);
        h.r[0] = a(3);
        return 3;
      }
      // EU 0x224A30 FUN_00224a30: igual con el #AMT (a1 = entrada, a3 = bloque), sondeo 0x225410.
      case 0x156: {
        BloqueCelda& b = Bloque(a(3));
        b.creado = true;
        b.id_amt = a(1);
        b.afs_amt = a(2);
        h.r[0] = a(3);
        return 3;
      }
      // EU 0x2253B0 FUN_002253b0: celda del bloque a2: +0x10 = a0 (prioridad), +0x14 = a1 (capa 0x7000/0x8000).
      case 0x114: {
        BloqueCelda& b = Bloque(a(2));
        b.prioridad = float(a(0));
        b.capa = float(a(1));
        return 1;
      }
      // EU 0x2252C0 FUN_002252c0: crea la celda del bloque a2: limpia las 16 entradas (+0x30..+0x330) y guarda las
      // banderas a0 en +0x20; arranca la tarea FUN_00225130 (aqui la celda se pinta cada cuadro desde DibEscena).
      case 0x115: {
        BloqueCelda& b = Bloque(a(2));
        b.creado = true;
        b.flags = a(0);
        for (auto& e : b.ent) e = CeldaEst{};
        return 1;
      }
      // EU 0x2250A0 FUN_002250a0: libera la celda del bloque a0 (FUN_001485f0 de su tarea).
      case 0x116: {
        BloqueCelda& b = Bloque(a(0));
        b.creado = false;
        for (auto& e : b.ent) e = CeldaEst{};
        return 1;
      }
      // EU 0x225000 FUN_00225000: libera las 8 celdas del banco.
      case 0x117:
        for (auto& b : bloques_) {
          b.creado = false;
          for (auto& e : b.ent) e = CeldaEst{};
        }
        return 1;
      // EU 0x224F80 FUN_00224f80: activa la entrada a0+1 del bloque a2 con el patron a1 (registro del #AMA) y cuadro 0.
      case 0x118: {
        CeldaEst& e = Entrada(a(2), a(0) + 1);
        e = CeldaEst{};
        e.activa = true;
        e.patron = a(1);
        e.cuadro = 0;
        return 1;
      }
      // EU 0x224F10 FUN_00224f10: desactiva la entrada a0+1 del bloque a1.
      case 0x119:
        Entrada(a(1), a(0) + 1).activa = false;
        return 1;
      // EU 0x224E90 FUN_00224e90: desplazamiento (x = a1, y = a0) de la entrada a2+1 del bloque a3.
      case 0x11A: {
        CeldaEst& e = Entrada(a(3), a(2) + 1);
        e.x = float(a(1));
        e.y = float(a(0));
        return 1;
      }
      // EU 0x224E20 FUN_00224e20: banderas a0 de la entrada a1+1 del bloque a2 (+0x34).
      case 0x11B:
        Entrada(a(2), a(1) + 1).flags = a(0);
        return 1;
      // EU 0x224DA0 FUN_00224da0: +0x34 |= 4 y +0x38 = a0 (fila/variante del registro, no un ancho: FUN_001a7f40
      // lo usa como param+0x18) de la entrada a2+1 del bloque a1.
      case 0x11C: {
        CeldaEst& e = Entrada(a(1), a(2) + 1);
        e.flags |= 4;
        e.ancho = float(a(0));
        return 1;
      }
      // EU 0x224CE0 FUN_00224ce0: consulta de la entrada a0+1 del bloque a1: r0 = FUN_001a7de0(fotograma, tabla);
      // 0 si la celda esta vacia. Aqui devuelve el fotograma actual (+1 para que 0 = vacia, como el original).
      case 0x11D: {
        const CeldaEst& e = Entrada(a(1), a(0) + 1);
        h.r[0] = e.activa ? int32_t(e.cuadro) + 1 : 0;
        return 1;
      }
      // EU 0x224470 FUN_00224470: libera la celda del bloque a0 (los dos recursos y la tarea).
      case 0x11E: {
        BloqueCelda& b = Bloque(a(0));
        b.creado = false;
        b.ama.clear();
        b.amt.clear();
        for (auto& e : b.ent) e = CeldaEst{};
        return 1;
      }
      // ---------------- DS43: tabla de sprites del texto y las caras (0x68ED90) ----------------
      // EU 0x22C950 FUN_0022c950: activa/desactiva el bucle por cuadro de la instancia (callback 0x22C5F0) con el byte
      // a1; el original lo deja en la tarea (+0x14). Aqui solo se anota: la interpolacion es 0x47.
      case 0x40:   // EU 0x22C5F0 FUN_0022c5f0
        bucle_ = a(0) != 0;
        bucle_byte_ = a(1);
        return 1;
      // EU 0x22C450 FUN_0022c450: prepara la ranura a3 desde la tabla de imagenes del recurso: +0x08 = a2, +0x0c = a1,
      // +0x30/+0x34 = 0, ancho/alto de la imagen (+0x50/+0x54), anclas (+0x40/+0x44) y escalas 1. [H]: la tabla de
      // imagenes (+0x14 del recurso) es la del #AMT; aqui se guardan los argumentos sin resolver la imagen.
      case 0x41: {
        SpriteEst& s = Sprite(a(3));
        s.activo = true;
        s.cuadro0 = 0;
        s.cuadro1 = 0;
        return 1;
      }
      // EU 0x22C3B0 FUN_0022c3b0: posicion (x = a0, y = a1), fotogramas (a2, a3) e imagen de la ranura a6.
      case 0x42: {
        SpriteEst& s = Sprite(a(6));
        s.activo = true;
        s.x = float(a(0));
        s.y = float(a(1));
        s.cuadro0 = a(3);
        s.cuadro1 = a(2);
        return 1;
      }
      // EU 0x22C340 FUN_0022c340: recorte de origen (x0,y0,x1,y1) = a0..a3 de la ranura a4 (+0x90..+0x9c).
      case 0x43: {
        SpriteEst& s = Sprite(a(4));
        if (!s.activo) s.activo = true;
        for (int k = 0; k < 4; ++k) s.recorte[k] = float(a(k));
        return 1;
      }
      // EU 0x22C240 FUN_0022c240: tamano (ancho = a0, alto = a1) de la ranura a2 (+0x50/+0x54).
      case 0x44: {
        SpriteEst& s = Sprite(a(2));
        s.ancho = float(a(0));
        s.alto = float(a(1));
        return 1;
      }
      // EU 0x22C130 FUN_0022c130: anclas/recorte (a0 -> +0x68/+0x78, a1 -> +0x84, a2 -> +0x80) de la ranura a3.
      case 0x45: {
        SpriteEst& s = Sprite(a(3));
        s.activo = true;
        s.recorte[0] = float(a(0));
        return 1;
      }
      // EU 0x22BF80 FUN_0022bf80: banderas de dibujo a0 de la ranura a1 (+0x14) y recalculo de los recortes:
      // bit 0/1 = centrado en x/y, 2/3 = espejo x/y, 4 = modo de color 0x48/0x44, 5 = mezcla 5.
      case 0x46: {
        SpriteEst& s = Sprite(a(1));
        s.activo = true;
        s.flags = a(0);
        return 1;
      }
      // EU 0x22BDE0 FUN_0022bde0: interpolacion color/posicion de la ranura a0 gobernada por el sondeo 0x22BC00
      // (mientras el bucle de 0x40 este activo). Solo se anota la llamada; DibEscena pinta la ranura con su estado.
      case 0x47:
        interpolando_ = true;
        return 1;
      // [H] 0x22E670 FUN_0022e670: funcion del motor de B2 no decompilada; el guion no la usa en los #YDW del port.
      case 0x04:   // sin efecto: solo `return 1` (r0 no se toca)
        return 1;
      // [H] 0x22E660 FUN_0022e660: idem.
      case 0x05:   // sin efecto: solo `return 1`
        return 1;
      // EU 0x227150 FUN_00227150
      case 0x90: {   // r0 = botones pulsados: modo a1 = 0 mando 1 (0x3B43AC), 1 mando 2 (0x3B4464), 0x100 el del jugador activo (+0x3B43AE), otro = los dos
        const int modo = a(1);
        // El port tiene una sola entrada fisica (`pad`): el mando 2 no existe y devuelve 0; el resto es la misma lectura.
        h.r[0] = modo == 1 ? 0 : int32_t(pad);
        return 1;
      }
      // EU 0x224440 FUN_00224440
      case 0x1A2:   // r0 = (a0 & 0x4000) != 0: ¿esta la X pulsada en la lectura de mando a0? (a0 viene del builtin 0x90)
        h.r[0] = (a(0) & 0x4000) != 0;
        return 1;
      // EU 0x27FC50 FUN_0027fc50
      case 0x173:   // r0 = 0x18 | (region PAL ? 2 : 0): bit 0x10 = el guion carga su texto; B2 EU es PAL
        h.r[0] = 0x18 | 2;
        return 1;
      default:
        if (std::find(sal.nativos_ignorados.begin(), sal.nativos_ignorados.end(), cod) == sal.nativos_ignorados.end())
          sal.nativos_ignorados.push_back(cod);
        h.r[0] = 1;
        return 1;
    }
  }

  // ---- DS43: tabla de celdas de sprite del guion (0x691CB0) y tabla de sprites del texto (0x68ED90) ----
  // 0x691CB0: 8 bloques de 0x330 B por banco (0x1980); cada bloque = cabecera + 16 entradas de 0x30 B. La entrada i se
  // direcciona con arg+1 (las funciones 0x118/0x119/0x11a/0x11b/0x11c/0x11d suman 0x30 sobre arg*0x30). Campos leidos en
  // FUN_00225130 (la tarea que la pinta) y escritos por 0x118-0x11c.
  struct CeldaEst {
    bool activa = false;
    int flags = 0;          // +0x04 (0x11b; 0x11c pone el bit 4 = escribe +0x08)
    int patron = 0;         // +0x20: indice del registro del #AMA que dibuja FUN_001a8d10
    float ancho = 0;        // +0x08: fila/variante del registro (FUN_001a7f40 lo usa en param+0x18); 0x11c lo pone
    float x = 0, y = 0;     // +0x10/+0x14: desplazamiento sobre la posicion del patron
    float cuadro = 0;       // +0x24: fotograma de la animacion del #AMA (avanza FUN_001a7ce0)
  };
  struct BloqueCelda {
    bool creado = false;
    float prioridad = 0;    // +0x10 (0x114 a0)
    float capa = 0;         // +0x14 (0x114 a1): 0x7000/0x8000 en los guiones = orden de dibujo
    int flags = 0;          // +0x20 (0x115 a0): bit 0 = modo 2D, bit 1 = FUN_00132f70(2,..)
    std::string ama, amt;   // rutas que carga 0x110/0x112 (0x2257b0 / 0x225590)
    int id_ama = -1, id_amt = -1;   // numeros de entrada que cargan 0x155/0x156 (0x224ac0 / 0x224a30)
    int afs_ama = 0, afs_amt = 0;   // 0 = DATA_CMN, 1 = AFS del idioma
    std::array<CeldaEst, 16> ent{};
  };
  std::array<BloqueCelda, 8> bloques_{};
  struct SpriteEst {        // 0x68ED90: 48 ranuras de 0xD0 B por banco (0x2700). Solo lo que el guion toca.
    bool activo = false;
    int imagen = -1;        // entrada del AFS de idioma que registro 0x157 (0x228D10)
    int cuadro0 = 0, cuadro1 = 0;
    int flags = 0;
    float x = 0, y = 0, ancho = 0, alto = 0;
    float recorte[4] = {0, 0, 0, 0};
  };
  std::array<SpriteEst, 48> sprites_{};
  int ultima_voz_ = -1;     // entrada de ADX_US de la ultima 0x159 (para la espera 0x84)
  bool instantanea_ = false;
  bool bucle_ = false, interpolando_ = false;
  int bucle_byte_ = 0;
  int voz_restante_ = 0;    // cuadros que quedan de la voz que espera 0x84
  bool voz_canal_ = false;  // canal de voz ocupado desde 0x159/0x83 hasta 0x85 (estado que consulta 0x8C)
  std::string voz_ruta_;    // ruta del .adx que pidio 0x83 (solo para el log)

  SpriteEst& Sprite(int i) {
    if (i < 0) i = 0;
    return sprites_[size_t(i % int(sprites_.size()))];
  }

  BloqueCelda& Bloque(int i) {
    if (i < 0 || i >= int(bloques_.size())) i = 0;
    return bloques_[size_t(i)];
  }
  CeldaEst& Entrada(int bloque, int entrada) {
    BloqueCelda& b = Bloque(bloque);
    if (entrada < 0 || entrada >= int(b.ent.size())) entrada = 0;
    return b.ent[size_t(entrada)];
  }
  // Cadena del guion (ruta de un recurso): FUN_0012D000 recibe `base + a1`. Las rutas de los 187 guiones son
  // "/CDROM/CMN/SYSTEM/SCR/PP/*.ama|.amt" y "/CDROM/US/SYSTEM/SCR/P_ADX/*.adx".
  std::string Cadena(int32_t off) const {
    const uint32_t o = base_ + uint32_t(off);
    std::string r;
    for (uint32_t i = o; i < d_.size() && d_[i] && r.size() < 96; ++i) r.push_back(char(d_[i]));
    return r;
  }
  // DS43: copia lo que el guion ha montado (celdas + recursos + sprites del texto) en `sal`. Se toma al aparecer la
  // primera voz (0x159), que es cuando la ventana ya esta montada y antes de que el guion empiece a desmontarla.
  void Instantanea() {
    if (instantanea_) return;
    instantanea_ = true;
    sal.celdas.clear();
    for (int b = 0; b < int(bloques_.size()); ++b) {
      sal.ama[size_t(b)] = bloques_[size_t(b)].ama;
      sal.amt[size_t(b)] = bloques_[size_t(b)].amt;
      sal.id_ama[size_t(b)] = bloques_[size_t(b)].id_ama;
      sal.id_amt[size_t(b)] = bloques_[size_t(b)].id_amt;
      if (!bloques_[size_t(b)].creado) continue;
      for (int i = 1; i < int(bloques_[size_t(b)].ent.size()); ++i) {
        const CeldaEst& e = bloques_[size_t(b)].ent[size_t(i)];
        if (!e.activa) continue;
        sal.celdas.push_back(MptCelda{b, i, e.patron, e.flags, e.ancho, e.x, e.y, bloques_[size_t(b)].capa});
      }
    }
    sal.sprites.clear();
    for (int i = 0; i < int(sprites_.size()); ++i) {
      const SpriteEst& s = sprites_[size_t(i)];
      if (!s.activo) continue;
      Salida::SpriteEsp x;
      x.slot = i;
      x.imagen = s.imagen;
      x.cuadro0 = s.cuadro0;
      x.cuadro1 = s.cuadro1;
      x.flags = s.flags;
      x.x = s.x;
      x.y = s.y;
      x.ancho = s.ancho;
      x.alto = s.alto;
      for (int k = 0; k < 4; ++k) x.recorte[k] = s.recorte[k];
      x.activo = true;
      sal.sprites.push_back(x);
    }
  }

  const std::vector<uint8_t>& d_;
  std::unordered_map<uint32_t, int32_t> mem_;
  std::deque<Hilo> hilos_;
  uint32_t base_ = kBase;
  int estado_ = 0, tabla_ext_ = 0, g48_ = 0, g4c_ = 0, g50_ = 0, g54_ = 0;
};}   // namespace spx

// Ejecuta el #SPX de una ventana de mensaje (CMN 450/612/943 + a0) como lo hace FUN_002929A0 (estado 1): instancia, guion, tabla
// de extensiones 0x379640, ganchos y arranque de la entrada 0. Devuelve el texto, las caras por linea y las voces.
// DS43: duracion en cuadros de una entrada de ADX_US.AFS (el canal de voz de las M/P/T). Sale del ADX de verdad:
// cabecera CRI, +8 frecuencia (BE u32) y +0xc numero de muestras; cuadros = muestras * 60 / frecuencia.
int FramesVozAdx(int entrada) {
  static std::map<int, int> cache;
  if (entrada < 0) return 0;
  if (const auto it = cache.find(entrada); it != cache.end()) return it->second;
  int cuadros = 0;
  AfsPs2 adx;
  const std::filesystem::path iso = g_iso_b2.empty() ? BuscarJuego("budokai 2") : std::filesystem::path(g_iso_b2);
  if (!iso.empty() && adx.Abrir(iso, "ADX_US.AFS")) {
    const std::vector<uint8_t> d = adx.Entrada(size_t(entrada), 32);   // solo la cabecera: no hace falta el PCM
    if (d.size() >= 32 && d[0] == 0x80) {
      auto be32 = [&](size_t o) {
        return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3];
      };
      const int hz = int(be32(8));
      const uint32_t muestras = be32(12);
      if (hz > 0 && muestras > 0) cuadros = int((int64_t(muestras) * 60 + hz / 2) / hz);   // redondeo a medio cuadro
    }
  }
  cache[entrada] = cuadros;
  REXLOG_INFO("dbz3 spx: voz ADX_US {} = {} cuadros ({:.2f} s)", entrada, cuadros, cuadros / 60.0);
  return cuadros;
}

spx::Salida CorrerSpx(const std::vector<uint8_t>& d, char tipo) {
  spx::Maquina m(d);
  if (!m.Reservar()) return {};
  m.duracion_voz = FramesVozAdx;   // DS43: 0x84 espera lo que dura el ADX (no un numero fijo)
  m.PonerGuion();
  m.PonerTablaExt(0x379640);
  m.PonerGancho48(tipo == 'M' ? 0x281060 : 0x280f90);
  m.PonerGancho4C(tipo == 'M' ? 0x292920 : 0x292960);
  m.PonerGancho50(0x280d70);
  m.PonerGancho54(0x280d20);
  m.Lanzar(0);
  // SOLO EXTRACCION (modo automatico/prueba, no es juego): el "jugador" pulsa X (0x4000) un cuadro de cada 8 para que el guion recorra todas
  // sus lineas y den sus datos (texto, caras, voces, celdas). En el juego la X la lee el port (kA = X de PS2) en DibEscena y avanza la linea
  // al pulsarla (el builtin 0x90 leeria `Maquina::pad`).
  // 0x228E30 NO espera la X: es el sondeo de carga de la imagen de texto (estado +4 == 3) y su registro en 0x68ED90; el
  // builtin 0x157 devuelve 3 (un cuadro de espera) y 0x159 igual al arrancar la voz, como el original.
  for (int c = 0; c < 60000 && m.Vivo(); ++c) {   // holgado: ahora 0x84 espera la duracion real del ADX de cada linea
    m.pad = c % 8 == 4 ? 0x4000 : 0;
    m.Cuadro();
  }
  for (int cod : m.sal.nativos_ignorados) REXLOG_INFO("dbz3 spx: builtin {:#x} sin ejecutar (motor)", cod);
  return m.sal;
}

// Busqueda por patron de bytes de antes de la VM: SOLO si la VM no llega al texto (opcode o builtin sin traducir), con aviso en el log.
void PatronSpx(const std::vector<uint8_t>& d, int orden, std::pair<int, std::vector<int>>& r) {
    {
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
}

// FUN_0026E020 (los 12 tipos 0..0xB) NO es el #SPX ni una ventana del juego: es el sistema de mensajes de la TARJETA DE MEMORIA (guardar,
// cargar, formatear, "no hay tarjeta"). Pruebas: las tareas esperan el estado de la tarjeta (iGpffff8558 = 2 listo / 3 error, codigo en
// iGpffff855c con -5, -2, -1, 0 = resultados de la tarjeta), usan FUN_0013D130/FUN_0013D270 (E/S de ficheros de tarjeta, tabla por ranura
// 0x3B4288 + 0x40*n) y sus mensajes son los 53 textos de DATA_<idioma> 289 (el #AMT de "mensajes de memory card" medido antes); la etapa
// que FUN_002682D0 elige (4, 0x17, 0x1A, 0x25, 0x29, 10+n...) es el numero de mensaje. Los llaman los puntos de guardado del juego (orden 41
// de fin de fase, 0x270460/550/660/800, menus 0x285630/0x2857E0, 0x296DA0/0x2976E0, 0x299F20/0x29EA10, 0x2B1360). Tipos: 0 guardar en
// partida nueva (0x26D450), 1/3 cargar (0x26AF70), 2 (0x26D930), 4/5/8/9/10 mensaje por ficha (0x269770), 6 cerrar (0x26AA60), 7 (0x26A000),
// 0xB (0x26D930, etapa 0x29). El port no usa tarjeta (la partida se guarda en la copia del jugador con GuardarProgreso): sustituido, no se
// reconstruye (ver SUSTITUIDAS_B2_TABLERO.txt).
// Ordenes 4/5/6 (FUN_002929A0, a1 = 'M'/'P'/'T'): corren el #SPX de DATA_CMN 450/612/943 + a0, que es la ventana de
// mensaje de la escena M###/P###/T### (rutas /SCR/P_MSG/M001MSG.AMT, voz P_ADX/M001_00.adx). Del bytecode (VM del 05,
// medido 2026-10-18 en el emulador: M001 = CMN 451 = la charla de Raditz y Nappa) se sacan:
//  - el texto: `push #id; push #1; mov #343, r2; builtin r2` = builtin 0x157 (FUN_002249A0) de (1 << 16 | id) = la
//    entrada id del AFS de idioma (#AMT, una imagen por linea; M001 -> 207, M002 -> 208, M155 -> 281, T001 -> 1160);
//  - quien dice cada linea (solo M; en las P ese `switch` vale 0/1 por linea, el "motion id" de su escena 3D, no una
//    cara: van sin cara): el `switch` del guion `push #linea ... jz; push #cara; pop r0; mov r0, [r13+4]`
//    con la cara = ID de B2 de la hoja T_CHARA (CMN 1196): M001 = {9, 10, 10, 10, 10, 9}, M002 = {10}.
// La capa visual de M usa ahora la animacion #AMA 790 y las texturas #AMT 791 (abajo, DibEscena). Siguen pendientes la
// tabla de voces ADX_US.AFS y los modelos 3D de las escenas P ("motion id").
std::map<int, std::vector<int>> g_voz_spx;   // CMN del #SPX -> voz por linea
void AbrirMensajeSpx(int orden, int a0) {
  static std::map<int, spx::Salida> memo;   // CMN -> lo que deja la VM (texto, caras, celdas, recursos, sprites)
  const int id = (orden == 4 ? 450 : orden == 5 ? 612 : 943) + a0;
  auto it = memo.find(id);
  if (it == memo.end()) {
    spx::Salida sa;
    std::pair<int, std::vector<int>> r{-1, {}};
    AfsPs2 cmn;
    std::vector<uint8_t> d;
    if (cmn.Abrir(BuscarJuego("budokai 2"), "DATA_CMN.AFS")) d = cmn.Entrada(size_t(id));
    if (d.size() > 0x24 && std::memcmp(d.data(), "#SPX", 4) == 0) {
      sa = CorrerSpx(d, "MPT"[orden - 4]);
      r.first = sa.texto;
      if (orden == 4) g_voz_spx[id] = sa.voz_linea;   // la voz por linea (solo M: en las P/T el contador [r13] no es la linea)
      if (orden == 4) r.second = sa.caras;   // solo las M llevan cara (ID de B2); en las P el valor es un "motion id"
      REXLOG_INFO("dbz3 tablero b2: #SPX CMN {}: VM {} cuadros, texto {}, {} lineas con cara, {} voces, {} celdas, {} sprites, "
                  "voz {} cuadros",
                  id, sa.cuadros, sa.texto, sa.caras.size(), sa.voces.size(), sa.celdas.size(), sa.sprites.size(), sa.voz_espera);
      for (const auto& r : sa.rutas) REXLOG_INFO("dbz3 tablero b2: #SPX CMN {} pide recurso '{}'", id, r);
      for (size_t b = 0; b < sa.ama.size(); ++b)
        if (!sa.ama[b].empty() || !sa.amt[b].empty() || sa.id_ama[b] >= 0 || sa.id_amt[b] >= 0)
          REXLOG_INFO("dbz3 tablero b2: #SPX CMN {} celda {}: ama {} '{}' amt {} '{}'", id, b, sa.id_ama[b], sa.ama[b],
                      sa.id_amt[b], sa.amt[b]);
      if (r.first < 0) {
        REXLOG_INFO("dbz3 tablero b2: #SPX CMN {}: la VM no llego al texto (builtins sin ejecutar: {}), busqueda por patron", id,
                    sa.nativos_ignorados.size());
        PatronSpx(d, orden, r);
      }
    }
    if (sa.texto < 0) sa.texto = r.first;   // (por si lo saco la busqueda por patron)
    if (sa.caras.empty()) sa.caras = r.second;
    it = memo.emplace(id, std::move(sa)).first;
  }
  REXLOG_INFO("dbz3 tablero b2: orden {} ({}): #SPX CMN {} -> mensaje loc {}, {} caras", orden, a0, id, it->second.texto,
              it->second.caras.size());
  if (it->second.texto <= 0) return;
  AbrirEscena("MPT"[orden - 4], it->second.texto);
  if (g_esc.abierta && g_esc.numero == it->second.texto) {
    g_esc.hablan = it->second.caras;   // (solo las M traen caras; en las P/T va vacio)
    if (auto v = g_voz_spx.find(id); v != g_voz_spx.end()) g_esc.voces = v->second;
    g_esc.celdas = it->second.celdas;   // DS43: celdas que el guion dejo montadas
    g_esc.recursos_ama = it->second.ama;
    g_esc.recursos_amt = it->second.amt;
    g_esc.celda_ama = it->second.id_ama;
    g_esc.celda_amt = it->second.id_amt;
  }
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
// DS23/24: banco de animaciones de historia y camara del rival no Saibaman de las P. El banco es el #AMM 801..848 cuyo
// segundo hueso tiene el prefijo del cuerpo de B2 (el mismo criterio que nave::BancoHistoria, A1c: Goku 827, Saibaman
// 844); la camara es el #AMC banco - 1. Se extraen ojo/objetivo/FOV del clip del propio #AMC, nunca se estiman. Goku
// usa clip 2 de 826 y Saibaman clip 0 de 843 (A1c); los demas rivales usan clip 0 de su propio #AMC. La pista viene de
// la tabla +0x14 del #AMC y los tracks 0/1/3, aunque falta verificacion visual individual.
std::string PrefijoCuerpoB2(int id_b2, int traje) {
  static std::map<std::pair<int, int>, std::string> cache;
  const auto clave = std::make_pair(id_b2, traje);
  if (auto it = cache.find(clave); it != cache.end()) return it->second;
  std::string r;
  const auto& elf = dbz3::pasarela::nave::ElfB2();
  const size_t rec = 0x2895C0 + size_t(std::max(0, id_b2)) * 0x2C8;
  if (rec + 0x2C8 <= elf.size()) {
    const int nc = int(elf[rec + 0x30]) | int(elf[rec + 0x31]) << 8;
    const size_t o = rec + 0x74 + 4 * size_t(std::max(0, traje) * std::max(nc, 1));
    uint32_t cuerpo = 0;
    if (o + 4 <= elf.size()) std::memcpy(&cuerpo, &elf[o], 4);
    AfsPs2 cmn;
    if (cuerpo && cmn.Abrir(g_iso_b2.empty() ? BuscarJuego("budokai 2") : std::filesystem::path(g_iso_b2), "DATA_CMN.AFS")) {
      const std::vector<uint8_t> amo = HijoAmb(cmn.Entrada(cuerpo), 0);
      size_t ini = 0;
      for (size_t k = 0; k + 7 < amo.size(); ++k)
        if (std::memcmp(&amo[k], "_BODY", 5) == 0 && (amo[k + 5] == 0 || (amo[k + 5] == '_' && amo[k + 6] == '1' && amo[k + 7] == 0))) {
          ini = k;
          while (ini > 0 && ((amo[ini - 1] >= 'A' && amo[ini - 1] <= 'Z') || (amo[ini - 1] >= '0' && amo[ini - 1] <= '9') ||
                             amo[ini - 1] == '_'))
            --ini;
          break;
        }
      if (ini > 0 && ini < amo.size()) {
        const char* p = reinterpret_cast<const char*>(&amo[ini]);
        const std::string nm(p, strnlen(p, std::min<size_t>(32, amo.size() - ini)));
        const size_t u = nm.find('_');   // GOK_BODY -> GOK
        r = u == std::string::npos ? nm : nm.substr(0, u);
      }
    }
  }
  cache[clave] = r;
  return r;
}
int BancoHistoriaB2(const std::string& prefijo) {
  if (prefijo.empty()) return 0;
  static std::map<std::string, int> cache;
  if (auto it = cache.find(prefijo); it != cache.end()) return it->second;
  int hallado = 0;
  AfsPs2 cmn;
  if (cmn.Abrir(g_iso_b2.empty() ? BuscarJuego("budokai 2") : std::filesystem::path(g_iso_b2), "DATA_CMN.AFS"))
    for (uint32_t e = 801; e <= 848 && !hallado; ++e) {
      const std::vector<uint8_t> a = cmn.Entrada(e);
      if (a.size() < 0x80 || a[1] != 'A' || a[2] != 'M' || a[3] != 'M') continue;   // #AMM o !AMM
      uint32_t nb = 0, off = 0;
      std::memcpy(&nb, &a[0x18], 4);
      std::memcpy(&off, &a[0x1C], 4);
      if (nb < 2 || off + 64 > a.size()) continue;
      const std::string n1(reinterpret_cast<const char*>(&a[off + 32]), strnlen(reinterpret_cast<const char*>(&a[off + 32]), 32));
      const size_t u = n1.find('_');
      if (u != std::string::npos && n1.substr(0, u) == prefijo) hallado = int(e);
    }
  cache[prefijo] = hallado;
  REXLOG_INFO("dbz3 tablero b2: banco de historia del rival ({}): {}", prefijo, hallado);
  return hallado;
}
struct CamB2 {
  float ojo[3] = {}, obj[3] = {}, fov = 0.6109f;
  bool ok = false;
};
const CamB2& CamaraDeBancoB2(int banco, int clip) {
  static std::map<std::pair<int, int>, CamB2> cache;
  const auto clave = std::make_pair(banco, clip);
  if (auto it = cache.find(clave); it != cache.end()) return it->second;
  CamB2& c = cache[clave];
  AfsPs2 cmn;
  if (banco > 0 && cmn.Abrir(g_iso_b2.empty() ? BuscarJuego("budokai 2") : std::filesystem::path(g_iso_b2), "DATA_CMN.AFS")) {
    const std::vector<uint8_t> a = cmn.Entrada(uint32_t(banco - 1));
    auto u32 = [&](size_t o) {
      uint32_t v = 0;
      if (o + 4 <= a.size()) std::memcpy(&v, &a[o], 4);
      return v;
    };
    auto f32 = [&](size_t o) {
      float v = 0;
      if (o + 4 <= a.size()) std::memcpy(&v, &a[o], 4);
      return v;
    };
    if (a.size() >= 0x20 && std::memcmp(a.data(), "#AMC", 4) == 0) {
      const uint32_t ncl = u32(0x10), tbl = u32(0x14);
      if (!ncl || !tbl) return c;
      if (clip >= int(ncl)) clip = 0;
      const uint32_t off = u32(tbl + 16 * size_t(clip) + 12);
      auto clave3 = [&](int track, float* dst) {
        const uint32_t p = u32(off + 4 * size_t(track));
        if (!p || p + 28 > a.size()) return false;
        for (int k = 0; k < 3; ++k) dst[k] = f32(p + 16 + 4 * size_t(k));   // clave 0: cuadro en +12, xyz en +16
        return true;
      };
      const bool e1 = clave3(0, c.ojo), t1 = clave3(1, c.obj);
      const uint32_t pf = u32(off + 4 * 3);
      if (pf && pf + 20 <= a.size()) c.fov = f32(pf + 16);
      c.ok = e1 && t1;
      REXLOG_INFO("dbz3 tablero b2: camara del rival: #AMC {} clip {} -> ojo ({:.2f}, {:.2f}, {:.2f}) objetivo ({:.2f}, {:.2f}, {:.2f}) fov {:.4f}",
                  banco - 1, clip, c.ojo[0], c.ojo[1], c.ojo[2], c.obj[0], c.obj[1], c.obj[2], c.fov);
    }
  }
  return c;
}

// DS43: las celdas de sprite del guion (#SPX) dibujan los patrones de su #AMA con las texturas de su #AMT. Estas son las
// rutas que piden los 187 guiones ("/CDROM/CMN/SYSTEM/SCR/PP/*.ama|.amt"): como la ISO no trae tabla de nombres (el AFS
// solo guarda tamaños), se mapea por nombre al bloque donde el juego los empaqueta, comprobado por contenido: PP_FIXBX =
// #AMA 788 (6 patrones) + #AMT 789 (2 texturas), PP_FIXCH = 790 (28) + 791 (8), PP_FIXST = 792 (8) + 793 (4) y las caras
// T_CHARA = #AMT 1196 (55 de 136x136). Cualquier otra ruta se registra en el log y no se pinta.
int EntradaDeRutaMpt(const std::string& ruta) {
  std::string s = ruta;
  for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  if (s.find("PP_FIXBX.AMA") != std::string::npos) return 788;
  if (s.find("PP_FIXBX.AMT") != std::string::npos) return 789;
  if (s.find("PP_FIXCH.AMA") != std::string::npos) return 790;
  if (s.find("PP_FIXCH.AMT") != std::string::npos) return 791;
  if (s.find("PP_FIXST.AMA") != std::string::npos) return 792;
  if (s.find("PP_FIXST.AMT") != std::string::npos) return 793;
  if (s.find("T_CHARA.AMT") != std::string::npos) return 1196;
  return -1;
}
// Parser de #AMA a `Datos::Sprite` (mismo formato que el lambda `leer_ama` de Cargar: copiado para no tocarlo).
void LeerAmaMpt(const std::vector<uint8_t>& am, std::vector<Datos::Sprite>& out) {
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
    if (nb < 256 && q <= am.size() && size_t(nb) <= (am.size() - q) / 0x30)
      for (uint32_t j = 0; j < nb; ++j) {
        const size_t b = q + 0x30 * size_t(j);
        sp.recortes.push_back({float(a32(b)), int32_t(a32(b + 4)), af(b + 0x20), af(b + 0x24), af(b + 0x28) + 1,
                               af(b + 0x2C) + 1});
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
    auto num = [&](size_t q3) {
      const float f = af(q3);
      return f >= 0.5f && f < 100000.0f ? f : float(a32(q3) < 100000 ? a32(q3) : 0);
    };
    sp.dur = std::max(num(o + 0x20), num(o + 0x24));
    out.push_back(sp);
  }
}
struct BancoMpt {
  std::vector<Datos::Sprite> ama;
  std::vector<Tex> tex;
};
// Banco (patrones + texturas) de una celda. Cache por rutas: las texturas viven hasta cerrar el juego.
const BancoMpt* BancoMptDe(int id_ama, const std::string& ruta_ama, int id_amt, const std::string& ruta_amt, int bloque) {
  const int ea = id_ama >= 0 ? id_ama : EntradaDeRutaMpt(ruta_ama);
  const int et = id_amt >= 0 ? id_amt : EntradaDeRutaMpt(ruta_amt);
  static std::map<std::pair<int, int>, BancoMpt> cache;
  const std::pair<int, int> clave{ea, et};
  if (const auto it = cache.find(clave); it != cache.end()) return it->second.ama.empty() ? nullptr : &it->second;
  BancoMpt b;
  auto* dr = dbz3::launcher::PreviewDrawer();
  AfsPs2 cmn;
  const std::filesystem::path iso = g_iso_b2.empty() ? BuscarJuego("budokai 2") : std::filesystem::path(g_iso_b2);
  if (dr && !iso.empty() && cmn.Abrir(iso, "DATA_CMN.AFS")) {
    if (ea < 0 || et < 0)
      REXLOG_INFO("dbz3 spx: celda {}: ruta sin mapear (ama {} '{}' amt {} '{}'), no se pinta", bloque, ea, ruta_ama, et, ruta_amt);
    else {
      LeerAmaMpt(cmn.Entrada(size_t(ea)), b.ama);
      const std::vector<uint8_t> amt = cmn.Entrada(size_t(et));
      for (int j = 0; j < std::min(CuantasAmt(amt), 64); ++j) {
        const Imagen im = TexturaAmt(amt, j);
        Tex x;
        if (im) {
          x.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
          x.w = float(im.w);
          x.h = float(im.h);
        }
        b.tex.push_back(std::move(x));
      }
      REXLOG_INFO("dbz3 spx: celda {}: {} patrones del #AMA {} + {} texturas del #AMT {}", bloque, b.ama.size(), ea, b.tex.size(), et);
    }
  }
  return (cache.emplace(clave, std::move(b)).first->second.ama.empty()) ? nullptr : &cache[clave];
}

void DibMptAma(ImDrawList* dl, int sprite, ImVec2 origen, float escala, float cuadro, ImU32 color);
bool DibCeldasMpt(ImDrawList* dl, const Escena& e, ImVec2 lo, float ls, float desde, int cara);   // DS43 (definido tras Lienzo)
void AyudaEscena(ImDrawList* dl, const S::Area& a, bool comic);
void DibEscena(ImDrawList* dl, const S::Area& a, float t, uint16_t& pressed) {
  if (!g_esc.abierta) return;
  Escena& e = g_esc;
  const ImVec2 W = ImGui::GetIO().DisplaySize;
  // Voz de la linea (builtin 0x159 del #SPX, ADX_US.AFS): una vez por linea, cortando la anterior.
  // DS43: se apunta cuanto dura de verdad (cabecera del ADX) para que la espera de la linea acabe con la voz, como 0x84.
  if (e.paso < e.voces.size() && e.voz_paso != int(e.paso) && e.voces[e.paso] > 0 && !g_iso_b2.empty()) {
    e.voz_paso = int(e.paso);
    const float vol = dbz3::settings::AudioMute() ? 0.0f : float(dbz3::settings::MasterVolume()) * 0.9f;
    dbz3::pasarela::CallarAdxPs2();
    dbz3::pasarela::OirAdxPs2(g_iso_b2, "ADX_US.AFS", e.voces[e.paso], std::getenv("DBZ3_TEST_MUSICA") ? 0.001f : vol);
    e.voz_fin = float(FramesVozAdx(e.voces[e.paso])) / 60.0f;
  }
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
    // DS23: las P llevan a Goku y al rival en 3D. Los cuerpos son los de la tabla de personajes de B2 (CMN 74-317,
    // los #AMB del registro del ELF) dibujados con el modelo de B3 y animados con su banco de historia (#AMM 801-848);
    // cada rival usa su #AMC (banco - 1) (ver el bloque e.bustos).
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
    // El lienzo del comic va centrado y en 4:3; en ventana 16:9 quedaban los laterales mostrando el tablero. El original
    // tiene la madera de 793 #3 a TODA la pantalla (medido en su frame sin texto del contacto de la fase II), asi que se
    // pinta su color de madera SOLO fuera del lienzo (el hueco del recuadro tiene que seguir mostrando el tablero).
    {
      const ImVec2 c0 = P(0, 0), c1 = P(640, 448);
      // Los laterales de la ventana son la misma madera del panel nativo CMN 793 #3 (640x448), no un color plano: se
      // muestrea una zona de tablon opaca del propio recurso (x 32-96, y 56-120; comprobado sin alfa) y se estira al
      // hueco. El centro lo tapa despues el panel completo, en su sitio.
      ImVec2 tam;
      const ImTextureID madera = CmnTex(793, 3, &tam);
      if (madera && tam.x > 0 && tam.y > 0) {
        const ImVec2 u0(32.0f / tam.x, 56.0f / tam.y), u1(96.0f / tam.x, 120.0f / tam.y);
        if (c0.x > 0) dl->AddImage(madera, ImVec2(0, 0), ImVec2(c0.x, W.y), u0, u1);
        if (c1.x < W.x) dl->AddImage(madera, ImVec2(c1.x, 0), W, u0, u1);
        if (c0.y > 0) dl->AddImage(madera, ImVec2(c0.x, 0), ImVec2(c1.x, c0.y), u0, u1);
        if (c1.y < W.y) dl->AddImage(madera, ImVec2(c0.x, c1.y), ImVec2(c1.x, W.y), u0, u1);
      } else {
        const ImU32 color = IM_COL32(185, 160, 95, 255);
        if (c0.x > 0) dl->AddRectFilled(ImVec2(0, 0), ImVec2(c0.x, W.y), color);
        if (c1.x < W.x) dl->AddRectFilled(ImVec2(c1.x, 0), W, color);
        if (c0.y > 0) dl->AddRectFilled(ImVec2(c0.x, 0), ImVec2(c1.x, c0.y), color);
        if (c1.y < W.y) dl->AddRectFilled(ImVec2(c0.x, c1.y), ImVec2(c1.x, W.y), color);
      }
    }
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
      // DS23: la camara del rival no Saibaman sale de su #AMC (banco de historia - 1), no de la de 843. El banco es el
      // #AMM 801..848 con el prefijo de su cuerpo (A1c); Goku usa el clip 2 de 826 y Saibaman el 0 de 843 (medidos).
      static const float kEsc = std::getenv("DBZ3_TEST_CB_S") ? float(std::atof(std::getenv("DBZ3_TEST_CB_S"))) : 1.0f;
      const float hy = 224.0f * ls;
      const float cg[14] = {-5.377f, 7.024f, 21.458f, -0.046f, 3.923f, -0.328f, 0.6109f, P(80, 324).x, P(80, 324).y, hy, kEsc, -1.20f, -0.25f, 0.25f};
      const float cs[14] = {0.0f, 6.731f, 29.331f, 0.0f, 1.643f, -0.381f, 0.6109f, P(420, 214).x, P(420, 214).y, hy, kEsc, -0.12f, 0.0f, -0.05f};
      if (std::getenv("DBZ3_TEST_GOKU_VIEJO") || !dbz3::pasarela::nave::DibujarFiguraB2(dl, 0, P(80, 610), 420.0f * ls, 0.0f, e.paso == 0 ? 0.0f : desde, 0, 827, 4, cg))
        DibGoku3DTarjeta(dl, P(84, 566), 350.0f * ls, desde, 0.62f);   // sin el banco 827 (otra ISO): la tarjeta de antes
      (void)ij; (void)gp;
      int banco_r = ir == kSaibaman ? 844 : (ir >= 0 ? BancoHistoriaB2(PrefijoCuerpoB2(ir, 0)) : 0);
      float cbr[14] = {};
      const float* cam_r = nullptr;
      if (ir >= 0 && ir != kSaibaman && banco_r > 0) {
        const CamB2& cb = CamaraDeBancoB2(banco_r, banco_r == 827 ? 2 : 0);
        if (cb.ok) {
          cbr[0] = cb.ojo[0]; cbr[1] = cb.ojo[1]; cbr[2] = cb.ojo[2];
          cbr[3] = cb.obj[0]; cbr[4] = cb.obj[1]; cbr[5] = cb.obj[2];
          cbr[6] = cb.fov; cbr[7] = P(420, 214).x; cbr[8] = P(420, 214).y; cbr[9] = hy;
          cbr[10] = kEsc; cbr[11] = 0.0f; cbr[12] = 0.0f; cbr[13] = 0.0f;   // sin los ajustes del Saibaman
          cam_r = cbr;
        }
      }
      if (ir >= 0 && !std::getenv("DBZ3_TEST_SIN_FIG_RIVAL")) dbz3::pasarela::nave::DibujarFiguraB2(
          dl, ir, P(424, 378), 313.0f * ls, 0.0f, 34.0f / 60.0f + desde, ir == kSaibaman ? g_f[size_t(g_pend_r)].modo : 0,
          ir == kSaibaman ? 844 : (cam_r ? banco_r : 0), 0, ir == kSaibaman ? cs : cam_r);
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
    // M/P/T: DS43: si el guion dejo celdas montadas (tabla 0x691CB0) se pintan sus patrones con sus texturas, que es lo
    // que hace la tarea FUN_00225130; si no, la maqueta fija de siempre. El texto y la cara siguen con su dibujo medido
    // (la tabla de sprites 0x68ED90 y el gancho 0x281060 del texto quedan [H], ver INFORME DS43).
    const bool ama790 = g_d.mpt_ama.size() > 5 && g_tmpt.size() > 5 && g_tmpt[2] && g_tmpt[3] && g_tmpt[5];
    const int quien = e.paso < e.hablan.size() ? e.hablan[e.paso] : -1;
    const bool celdas = DibCeldasMpt(dl, e, lo, ls, desde, quien);
    if (celdas) {
      // fondo liso: el guion pinta su propia franja/velo (patrones 0/5/14/19/25 del #AMA)
    } else if (ama790) {
      DibMptAma(dl, 5, lo, ls, desde * 60.0f, IM_COL32_WHITE);   // franja sombreada del lienzo (AMT791 #5, 640x196)
    } else {
      dl->AddRectFilled(P(0, 350), P(640, 448), IM_COL32(64, 40, 120, 235));
      dl->AddRectFilled(P(150, 300), P(625, 398), IM_COL32(244, 244, 244, 250), 14 * ls);
      dl->AddRect(P(150, 300), P(625, 398), IM_COL32(200, 200, 210, 255), 14 * ls, 0, 1.5f * ls);
    }
    // R7: la cara es el retrato de B2 de esa pieza (su ficha, 342+5k #2*indice, de la ISO del jugador). Los bustos propios
    // de las charlas no estan localizados (R7/b2/INFORME.md: ni en DATA_CMN ni en DATA_<idioma> hay un banco de bustos)
    // R7: las cabezas de B2 (CMN 1196: 55 de 136x136, una por ID de personaje de B2: 0 Goku, 9 Raditz, 10 Nappa...)
    // DS43: si el guion pinto la cara (celda del banco T_CHARA) ya esta puesta con su textura; no se dobla.
    if (!celdas) {
      if (const ImTextureID cab = CabezaB2(quien)) {
        // El aro del retrato es el emblema nativo del original (CMN 791 #0, 120x112, el mismo que usa el panel comico
        // de arriba); estaba dibujado con dos circulos propios. Solo si falta el recurso se conservan los circulos.
        if (const ImTextureID aro = CmnTex(791, 0)) {
          const ImVec2 c(75, 350);
          dl->AddImage(aro, ImVec2(c.x - 60 * ls, c.y - 56 * ls), ImVec2(c.x + 60 * ls, c.y + 56 * ls));
        } else {
          dl->AddCircleFilled(P(75, 350), 64 * ls, IM_COL32(200, 40, 40, 255));
          dl->AddCircle(P(75, 350), 64 * ls, IM_COL32(255, 220, 120, 255), 0, 3 * ls);
        }
        dl->AddImage(cab, P(18, 290), P(132, 404));
      }
    }
    if (!celdas && ama790) {
      const float cuadro = desde * 60.0f;
      DibMptAma(dl, 0, lo, ls, cuadro, IM_COL32(210, 210, 210, 255));   // velo/modulacion de la nube: RGB 230 -> 190 medido en PCSX2
      DibMptAma(dl, 2, lo, ls, cuadro, IM_COL32_WHITE);   // logo Dragon Ball Z del banco 791, bajo la cara
    }
    const Tex& x = e.img[size_t(e.textos[e.paso])];
    if (x) {
      const float esc = std::min(1.3f, std::min(450.0f / x.w, 80.0f / x.h));
      dl->AddImage(x.id(), P(388 - x.w * esc * 0.5f, 349 - x.h * esc * 0.5f), P(388 + x.w * esc * 0.5f, 349 + x.h * esc * 0.5f),
                   ImVec2(0, 0), ImVec2(1, 1), IM_COL32(30, 30, 40, 255));
    }
  }
  AyudaEscena(dl, a, e.comic);
  // A2b: las ventanas M/P/T del original esperan a X (medido en PCSX2: la P de la fase IV sigue en pantalla 9 s despues del
  // contacto); solo la intro del Mundo Dragon (155) pasa sola y el modo automatico da su tiempo a cada linea.
  // DS43: la espera va sincronizada con el ADX de la linea (0x227310 / 0x227230): el avance automatico no entra hasta que
  // la voz ha terminado (duracion de su cabecera) y, si el jugador pasa de linea antes, se corta la voz como 0x227230.
  const float fin_voz = std::max(0.0f, e.voz_fin);
  const bool sigue = (pressed & kA) || (g_esc_auto && desde > std::max(0.35f, fin_voz)) || (e.numero == 155 && desde > 6.0f);
  if (pressed & kB) e.paso = e.textos.size();
  else if (sigue && desde > 0.25f) {
    if (e.voz_paso >= 0 && desde < fin_voz) dbz3::pasarela::CallarAdxPs2();   // 0x227230: cortar la voz al pasar de linea
    ++e.paso;
    e.t0 = t;
    e.voz_fin = 0;
  }
  pressed = 0;
  if (e.paso >= e.textos.size()) {   // S1-F07.2: sus AddImage de este cuadro aun no se han pintado
    if (!e.voces.empty()) dbz3::pasarela::CallarAdxPs2();
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
std::vector<uint8_t> g_goku_amm;                  // banco CMN 336 para las poses puntuales (tabla 0x379010)
std::map<int, ps2::Anim> g_goku_clips;            // cache de clips de Goku por indice de banco
float g_goku_reac_t = -10;
bool g_goku_reac_ko = false;
float g_goku_alto = 0, g_goku_giro = 0;
bool g_goku3d_cargado = false;
bool g_goku_variante = false;   // orden 82 (ELF 0x6992DC bit 0x04): Goku del tablero con CMN 339/340
// Ficha del tablero en 3D (CMN 335: #AMO de 21 huesos y 3 partes; su textura 0 es el retrato y la 1 el dorso con la Z de
// cada pieza, de 342+5k: 2 * indice y 2 * indice + 1) con sus 26 animaciones (CMN 334). Las texturas de las fichas de la
// fase como ps2::Tex (las mismas que g_tfichas). La espera es el clip 0 del CMN 334 y dura 72 cuadros (medido con LeerAnim:
// "ficha 3D: ... espera 72 cuadros" en la corrida 20261025-040826_ds21_fase8_bu47).
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
  g_goku_amm = amm;   // poses puntuales: 0x277B50 (anim 9), 0x277A30 (anim 10), 0x277A90/AF0 (anim 5/4)
  g_goku_clips.clear();
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
// Camara de las figuras del tablero con los datos del original (b2.c: FUN_0023A050 = Goku, FUN_0023A2E0 = ficha, con FUN_0023AB60):
// la camara por defecto (FUN_0012C0F0, en el origen mirando a -z) con la proyeccion normal (foco 512 px de la pantalla de 640x448
// por tangente: FUN_00131450 [0xF] = DAT_0033D170), el modelo a `dist` unidades por delante (FUN_0013CDF0(0,0,z): Goku uGpffff81a0 =
// -63,0 en el ELF 0x3A3E90; ficha 0xC2480000 = -50,0) y cabeceo de 0x1333 = 27 grados (FUN_0013C6A0: seno con 65536 por vuelta).
// `e` = px del area por px de la pantalla de 640x448. La proyeccion del original es cuadrada tras el 4:3; aqui se dibuja antes del
// estiramiento vertical de Dibujar(), asi que DibFigura3D aplana despues 1/k (ver abajo).
ps2::Camara CamaraFigura(ImVec2 pie, float dist, float e) {
  ps2::Camara cam;
  cam.c = pie;
  cam.h = ImVec2(450, 450);
  cam.fov = 2.0f * std::atan(450.0f / (512.0f * e));   // f = h.y / tan(fov / 2) = 512 e
  const float th = 4915.0f * 6.28318531f / 65536.0f, cth = std::cos(th), sth = std::sin(th);
  cam.m[0] = 1;
  cam.m[5] = cth, cam.m[9] = -sth;
  cam.m[6] = sth, cam.m[10] = cth;
  cam.m[15] = 1;
  cam.m[14] = -dist;
  cam.ok = true;
  return cam;
}
// El estiramiento 4:3 de Dibujar() multiplica y por k alrededor del centro del area; la figura (proyeccion cuadrada) se aplana
// antes 1/k alrededor de su pie para que quede cuadrada al final, como en el original.
void AplanarFigura(ImDrawList* dl, int v0, ImVec2 pie) {
  const float k = (900.0f / 448.0f) / (1200.0f / 640.0f);
  for (int i = v0; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].pos.y = pie.y + (dl->VtxBuffer[i].pos.y - pie.y) / k;
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
// Clip de Goku (CMN 336) para una animacion +0x1C del registro visual, con la misma tabla 0x379010. Goku es el unico
// personaje del tablero que no usa la ficha 3D (CMN 335), asi que sus poses 0x277B50/0x277A30/0x277A90/0x277AF0 se
// cargan de su propio banco. Las animaciones de Goku son absolutas (no relativas), como las de CargarGoku3D.
const ps2::Anim* ClipGoku(int anim) {
  const int i = ClipDeAnim(anim);
  if (i < 0 || g_goku_amm.empty() || !g_goku3d.esq) return nullptr;
  auto it = g_goku_clips.find(i);
  if (it != g_goku_clips.end()) return it->second ? &it->second : nullptr;
  ps2::Anim a = ps2::LeerAnim(g_goku_amm, uint32_t(i), g_goku3d.esq->padre.size());
  a.relativa = false;
  ps2::Anim& e = g_goku_clips[i];
  e = a;
  return e ? &e : nullptr;
}
bool DibFicha3D(ImDrawList* dl, int idx, ImVec2 pie, float e, float t, float giro = 0, bool anda = false,
                bool lucha = false, float busca = -1, int clip = -1, float clip_t = 0, int estado = 0, float alfa = 1.0f) {
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
  const ps2::Camara cam = CamaraFigura(pie, 50.0f, e);
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;   // cel shading y borde de P4 (la carta y la peana traen normales)
  toon.borde = g_ficha_alto * 0.009f;
  esc.Poner(cam, m, g_ps2_fichas, {0, -ymin, 0}, g_ficha_base + giro, 1.0f, toon);
  const int v0 = dl->VtxBuffer.Size;
  esc.Pintar(dl);
  if (alfa < 1.0f)
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
      ImU32& c = dl->VtxBuffer[i].col;
      c = (c & 0x00FFFFFFu) | (ImU32(float(c >> 24) * std::clamp(alfa, 0.0f, 1.0f)) << 24);
    }
  AplanarFigura(dl, v0, pie);
  return true;
}
// Goku 3D de pie en `pie` (pantalla), de `alto_px` de alto, mirando hacia (dx, dy) del mapa si anda.
bool DibGoku3D(ImDrawList* dl, ImVec2 pie, float e, float t, bool anda, float dx, float dy, float alfa = 1.0f,
               int clip = -1, float clip_t = 0, int estado = 0) {
  CargarGoku3D();
  if (!g_goku3d.malla || g_goku_alto <= 0) return false;
  // dx > 0: pose de combate (dx s desde el contacto); dy > 0: alerta de la busqueda (dy s); si no, andar o reposo.
  // clip >= 0: pose puntual del registro (+0x1C) pedida por 0x277B50/0x277A30/0x277A90/0x277AF0 o por las ordenes 58-60.
  const bool lucha = dx > 0 && g_goku_lucha, alerta = !lucha && dy > 0 && g_goku_alerta;
  const ps2::Anim* pose_an = clip >= 0 ? ClipGoku(clip) : nullptr;
  const bool con_pose = pose_an && *pose_an;
  const ps2::Anim& reac = g_goku_reac_ko ? g_goku_ko : g_goku_herido;
  const float tr = g_ahora - g_goku_reac_t;
  const bool reacciona = !con_pose && !lucha && !alerta && !anda && reac && tr >= 0 && tr < float(reac.cuadros) / 60.0f + 0.5f;
  const ps2::Anim& an = con_pose ? *pose_an : lucha ? g_goku_lucha : alerta ? g_goku_alerta : reacciona ? reac : anda ? g_goku_andar : g_goku_espera;
  if (con_pose)
    t = estado == 0x10 ? std::fmod(std::max(0.0f, clip_t), std::max(1.0f, float(pose_an->cuadros)) / 60.0f)
                       : std::min(std::max(0.0f, clip_t), float(pose_an->cuadros - 1) / 60.0f);
  else if (lucha || alerta) t = std::min(lucha ? dx : dy, float(an.cuadros - 1) / 60.0f);   // una vez y se queda en el ultimo
  if (reacciona) t = std::min(tr, float(an.cuadros - 1) / 60.0f);
  static ps2::Malla pose;
  const bool anima = bool(an);
  if (anima) ps2::Posar(*g_goku3d.esq, an, t, pose);
  // Posar deja la y del modelo: el pie mas bajo al suelo en cada cuadro (con la del primero, al pasar de esperar a
  // andar flotaba o se hundia; auditoria7/G)
  float ymin = 1e9f;
  for (const auto& tr : (anima ? pose : g_goku3d.malla).tris)
    for (const auto& v : tr.p) ymin = std::min(ymin, v.y);
  const ps2::Camara cam = CamaraFigura(pie, 63.0f, e);   // datos del original, ver CamaraFigura
  ps2::Escena esc;
  esc.Empezar();
  ps2::Escena::Toon toon;   // cel shading y borde negro de P4 (las partes de Goku traen normales, vtype & 1)
  toon.borde = g_goku_alto * 0.009f;
  esc.Poner(cam, anima ? pose : g_goku3d.malla, g_goku3d_tx, {0, -ymin, 0}, g_goku_giro, 1.0f, toon);
  const int v0 = dl->VtxBuffer.Size;
  esc.Pintar(dl);
  if (alfa < 1.0f)
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
      ImU32& c = dl->VtxBuffer[i].col;
      c = (c & 0x00FFFFFFu) | (ImU32(float(c >> 24) * std::clamp(alfa, 0.0f, 1.0f)) << 24);
    }
  AplanarFigura(dl, v0, pie);
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
  lista(g_d.mpt_texturas, g_tmpt);
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
    lista(g_d.ayuda, g_tayuda);
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
  float k = 1.0f;
  ImVec2 P(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s * k); }
};
Lienzo LienzoDe(const S::Area& a) {
  Lienzo L;
  // Dibujar() estira TODO lo del tablero (mundo, HUD, dialogos) en vertical x(900/448)/(1200/640) alrededor del centro del area
  // (la pantalla de 640x448 sale en 4:3: FB 512x512, FUN_00130410 / FUN_00133340; mismo factor que la nave). Por eso el lienzo es
  // uniforme y con las bandas de 30 unidades que el estiramiento quita; con k != 1 aqui el HUD se estiraba dos veces (LUNA28).
  const float ls = std::min(1200.0f / 640.0f, 900.0f / 448.0f) * a.s;
  L.s = ls;
  L.k = 1.0f;
  L.o = ImVec2(a.p0.x + (1200.0f * a.s - 640.0f * ls) * 0.5f, a.p0.y + (900.0f * a.s - 448.0f * ls) * 0.5f);
  return L;
}
// Tiras de la barra de ayuda del original: DATA_<idioma> 1145, cargadas en Cargar (FUN_002a81a0, b2.c:248596, pide
// FUN_0012d100(0x10479)). Cada tira ya lleva el texto y el boton pintados, asi que no se redibuja nada con fuente.
// El objeto de ayuda (FUN_002a72e0) dibuja las tiras a su tamano en el rectangulo 0x37A870[0] = (40,392)-(607,424) y
// las desplaza 1,25 px por cuadro (1,5 a 60 Hz) cuando no caben: es la marquesina. `pinta=false` solo comprueba datos.
bool DibAyudaTiras(ImDrawList* dl, const Lienzo& L, float t, const int* idx, int n, bool pinta) {
  if (!idx || n <= 0 || g_tayuda.empty()) return false;
  float total = 0;
  for (int k = 0; k < n; ++k) {
    const int j = idx[k];
    if (j < 0 || j >= int(g_tayuda.size()) || !g_tayuda[size_t(j)]) return false;
    total += g_tayuda[size_t(j)].w + 32.0f;
  }
  if (total <= 0 || !pinta) return total > 0;
  const float x0 = L.P(40, 0).x, x1 = L.P(607, 0).x;
  const float y0 = L.P(0, 392).y, y1 = L.P(0, 424).y;
  const float vel = 75.0f * L.s;   // 1,25 px/cuadro a 60 Hz (FUN_002a72e0)
  const float off = std::fmod(std::max(0.0f, t) * vel, total * L.s);
  dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
  float x = x0 - off;
  for (int vuelta = 0; vuelta < 2; ++vuelta) {
    for (int k = 0; k < n; ++k) {
      const Tex& tx = g_tayuda[size_t(idx[k])];
      const float w = tx.w * L.s;
      if (x + w > x0 && x < x1)
        dl->AddImage(tx.id(), ImVec2(x, y0), ImVec2(x + w, y0 + 32.0f * L.s), ImVec2(0, 0), ImVec2(1, 1));
      x += w + 32.0f * L.s;
    }
  }
  dl->PopClipRect();
  return true;
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
// DS43: dibuja UN sprite (sin indice), para poder cambiarle la textura a una copia (la cara del guion).
void DibSpUno(ImDrawList* dl, const Datos::Sprite& sp, const std::vector<Tex>& tx, const Lienzo& L, float c, ImU32 col,
              float dx, float dy, float giro) {
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
void DibSp(ImDrawList* dl, const std::vector<Datos::Sprite>& v, const std::vector<Tex>& tx, int k, const Lienzo& L,
           float c = -1, ImU32 col = IM_COL32_WHITE, float dx = 0, float dy = 0, float giro = 0) {
  if (k < 0 || k >= int(v.size())) return;
  DibSpUno(dl, v[size_t(k)], tx, L, c, col, dx, dy, giro);
}
void DibMptAma(ImDrawList* dl, int sprite, ImVec2 origen, float escala, float cuadro, ImU32 color) {
  Lienzo L;
  L.o = origen;
  L.s = escala;
  DibSp(dl, g_d.mpt_ama, g_tmpt, sprite, L, cuadro, color);
}
// DS43: pinta las celdas que el guion dejo montadas. FUN_00225130 recorre las 16 entradas de cada celda y por cada una
// activa dibuja su patron (FUN_001a8d10(celda+0xc, entrada+0x20, entrada+0x24)) sumandole el desplazamiento
// (entrada+0x10/+0x14); el orden es la capa de la celda (+0x14: 0x7000 las del fondo, 0x8000 la cara). El fotograma
// avanza uno por cuadro (FUN_001a7ce0) y aqui sale del tiempo que lleva abierta la linea.
bool DibCeldasMpt(ImDrawList* dl, const Escena& e, ImVec2 lo, float ls, float desde, int cara) {
  if (e.celdas.empty()) return false;
  std::vector<const MptCelda*> orden;
  orden.reserve(e.celdas.size());
  for (const auto& c : e.celdas) orden.push_back(&c);
  std::stable_sort(orden.begin(), orden.end(), [](const MptCelda* a, const MptCelda* b) {
    if (a->capa != b->capa) return a->capa < b->capa;
    return a->bloque < b->bloque;
  });
  Lienzo L;
  L.o = lo;
  L.s = ls;
  const float cuadro = desde * 60.0f;   // 60 Hz, igual que las cuentas por cuadro del original
  bool algo = false;
  for (const MptCelda* c : orden) {
    if (c->bloque < 0 || c->bloque >= 8) continue;
    const BancoMpt* b = BancoMptDe(e.celda_ama[size_t(c->bloque)], e.recursos_ama[size_t(c->bloque)],
                                  e.celda_amt[size_t(c->bloque)], e.recursos_amt[size_t(c->bloque)], c->bloque);
    if (!b) continue;
    if (c->patron < 0 || c->patron >= int(b->ama.size())) continue;
    // La cara: la celda pinta el registro del #AMA con la textura que trae (T_CHARA 7 = Ten Shin Han de reserva); el guion
    // elige el personaje en [r13+4] y aqui se cambia solo la textura de la copia: posicion, caja y animacion del guion.
    const int amt_cara = e.celda_amt[size_t(c->bloque)] >= 0 ? e.celda_amt[size_t(c->bloque)]
                                                             : EntradaDeRutaMpt(e.recursos_amt[size_t(c->bloque)]);
    Datos::Sprite sp = b->ama[size_t(c->patron)];
    if (cara >= 0 && amt_cara == 1196 && cara < int(b->tex.size()) && b->tex[size_t(cara)] &&
        b->tex[size_t(cara)].w >= 128.0f)   // T_CHARA: 136x136 por cara (el indice 1 es una textura de 8x8)
      sp.tex = cara;
    DibSpUno(dl, sp, b->tex, L, cuadro, IM_COL32_WHITE, c->x, c->y, 0);
    algo = true;
  }
  return algo;
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

// EU 0x239690 FUN_00239690
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
// EU 0x2A1E30 FUN_002a1e30 (lista de aliados: casillas 0x27/0x2B, cursor y marco)
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
    {   // casilla: 0x27 (39) sin marcar / 0x2B (43) marcada (FUN_002A1E30); 38 es la entrada. El motor espera a que
        // FUN_00245BD0 termine la 0x2B, cuya duracion es FUN_00245C00: max(+0x20,+0x24) del registro #AMA.
      const bool marcada = std::find(g_marcadas.begin(), g_marcadas.end(), libres[size_t(i)]) != g_marcadas.end();
      const float ultima = g_d.aliados_ama.size() > 43 ? std::max(0.0f, g_d.aliados_ama[43].dur - 1.0f) : 0.0f;
      if (marcada) dib(43, ultima, 32.0f * fila);
      else dib(38, 0, 32.0f * fila);
    }
    int etiqueta = -1;
    for (int j = 0; j < 14; ++j) if (kAliados01[j] == id) etiqueta = 51 + j;
    if (etiqueta >= 0) dib(45, 0, 32.0f * fila, etiqueta, color);
    else S::MenuText(dl, L.P(345, 207 + 32.0f * fila), 16 * L.s * L.k, Nombre(g_f[size_t(libres[size_t(i)])]).c_str(), true, false);
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
    // el original muestra siempre las tres: Select / Confirm / Cancel (medido en las listas de las fases I-IX; Triangle
    // desmarca la fila marcada y, sin marcas, no hace nada, pero la ayuda sale igual).
    S::HelpLine(dl, L.P(310, 409), 24 * L.s * L.k, {{"PAD", T::T("Seleccionar", "Select")}, {"A", T::T("Confirmar", "Confirm")}, {"B", T::T("Cancelar", "Cancel")}});
  }
  return true;
}

// ---- Tarjeta "Battle!" (s9) ----
// Tras el contacto B2 enseña esta pantalla (savestate 5 del emulador; textos y capas medidos con el CMN 408 / 410 y
// DATA_<idioma> 189-190): degradado azul (22,8,84) -> (63,91,209) de arriba a abajo; a los 60-80 cuadros el titulo (anim 6,
// textura 2) entra de la derecha hasta x = 5, y a los 70-80 el pergamino (anim 7, textura 0) entra de la izquierda hasta
// x = 258, y = 240, con el texto de la condicion (189 + condicion, 352x96) llegando a x = 234; abajo, la barra oscura
// (x 33-597, y 394-427, 30 % negro) con "X: Next / START: Start battle". El personaje del jugador va a la derecha (cabeza a
// y = 19, de pie hasta salirse por abajo). B3M resolvio la pose (guardia, anim 0 del banco de historia 801-848). Queda
// FUN_002ad940 (b2.c:251540-251654) inicializa tareas 3D #0 en x=161,y=346,z=1199.5 y #1 en x=480,y=346,z=1199.5
// (constantes IEEE del ELF). FUN_002b1ef0 estado 5 sube el alfa a +0xa0 por tick hasta fGpffff839c, con tasa
// fGpffff8398. El port usa el ancla #1 (x=480,y=346) para el modelo B3/B2. [VERIFICAR] la rampa alfa/instante:
// los globals son datos de runtime y DibujarFiguraB2 no expone alfa por figura.
void DibFicha(ImDrawList* dl, const Ficha& f, ImVec2 base, float w, float t, ImU32 col);
bool DibGoku3DTarjeta(ImDrawList* dl, ImVec2 pie, float alto_px, float t, float giro);
// EU 0x2AD940 FUN_002ad940 (tareas 3D de la tarjeta "Battle!": #0 en (161,346,1199.5), #1 en (480,346,1199.5))
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
      const ImVec2 pie = L.P(480.0f, 346.0f);   // FUN_002ad940: tarea #1, x=480,y=346,z=1199.5 (b2.c:251540-251654)
    // A2c: el modelo de B2 con la pose de B2 (el cargador de la rueda de la nave: registro 0x389540 + id * 0x2C8 y banco CMN 931)
    static const float giro_fig = std::getenv("DBZ3_TEST_FIG_GIRO") ? float(std::atof(std::getenv("DBZ3_TEST_FIG_GIRO"))) : -0.45f;
    static const float alto_fig = std::getenv("DBZ3_TEST_FIG_ALTO") ? float(std::atof(std::getenv("DBZ3_TEST_FIG_ALTO"))) : 433.0f;
    // B3M: la pose de la tarjeta NO es CMN 931 (sus 7 animaciones dan brazos arriba o el cuerpo plegado); es la guardia
    // (anim 0) del banco de historia del personaje (801..848; Goku 827, medido contra el original). 931 solo con
    // DBZ3_SEL_TARJ_931=1 o DBZ3_TEST_TARJ_BANCO, para comparar.
    static const bool fig_b2 = std::getenv("DBZ3_TEST_SIN_FIG_TARJETA") == nullptr;   // pose de 931 ya absoluta (medida en PCSX2, 23/10)
    // B3M: banco / animacion de la tarjeta (prueba: DBZ3_TEST_TARJ_BANCO="banco anim"; 0 = el banco de la rueda, CMN 931)
    static int tarj_banco = 0, tarj_anim = 0;
    static const bool tarj_leido = [] { if (const char* e = std::getenv("DBZ3_TEST_TARJ_BANCO")) std::sscanf(e, "%d %d", &tarj_banco, &tarj_anim); return true; }();
    (void)tarj_leido;
    const int id_tarjeta = std::getenv("DBZ3_TEST_B2_TARJ_CHAR") ? std::clamp(std::atoi(std::getenv("DBZ3_TEST_B2_TARJ_CHAR")), 0, 40) : j.c;
    if (!(fig_b2 && dbz3::pasarela::nave::DibujarFiguraB2(dl, id_tarjeta, pie, alto_fig * L.s, giro_fig, 0.0f, 0, tarj_banco, tarj_anim)) &&
        (id_tarjeta != kGoku || !DibGoku3DTarjeta(dl, pie, 413.0f * L.s, t, std::getenv("DBZ3_TEST_ER_GIRO") ? float(std::atof(std::getenv("DBZ3_TEST_ER_GIRO"))) : -0.55f)))
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
        dl->AddText(ImGui::GetFont(), 8 * L.s * L.k, ImVec2(c.x - 14 * L.s, c.y + 3 * L.s * L.k), IM_COL32(210, 214, 226, 255), "START");
      }
      const float fs = 21.0f * L.s * L.k;
      const ImVec2 tp = L.P(x + 16, 399);
      dl->AddText(ImGui::GetFont(), fs, ImVec2(tp.x + 1.5f * L.s, tp.y + 1.5f * L.s), IM_COL32(20, 20, 60, 255), txt);
      dl->AddText(ImGui::GetFont(), fs, tp, IM_COL32(255, 255, 255, 255), txt);
    };
    // La barra de ayuda de la tarjeta es la misma tarea que la del tablero (FUN_002ad940 -> FUN_002a81a0; mascara de
    // ayuda): sus frases son tiras de DATA_<idioma> 1145 (25 = "X : Next", 30 = "START : Start battle"). Solo si la
    // copia no trae el banco se conserva el dibujo propio de antes.
    static const int kAyudaTarjeta[2] = {25, 30};
    const float ayuda_t = std::max(0.0f, c) / 60.0f;   // la marquesina empieza con la tarjeta, no con el reloj global
    if (DibAyudaTiras(dl, L, ayuda_t, kAyudaTarjeta, 2, false)) {
      DibAyudaTiras(dl, L, ayuda_t, kAyudaTarjeta, 2, true);
    } else {
      boton(269, false, ":Next");
      boton(413, true, T::T(":Empezar combate", ":Start battle"));
    }
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
constexpr float kTableroAlto = 448.0f;   // alto de la pantalla del original (0x3A43B4). Medido el 2026-10-25 con la sonda
                                         // sobre PCSX2 (PINE, instancia luna): 0x3A43B4 = 0x43E00000 = 448.0f y el flotante
                                         // siguiente (0x3A43B8) = 0x3F4CCCCD = 0.8. Si midiera otro valor, cambiar solo este.
struct Cam {
  float cx = 0, cy = 0, e = 1;
  ImVec2 c;
  bool P(float mx, float my, ImVec2& out, float* esc = nullptr) const {
    out = ImVec2(c.x + (mx - cx) * e, c.y + (my - cy) * e);
    if (esc) *esc = e;
    return true;
  }
};

// Representacion de piezas medida en original_fase1_p1.png: Goku jugador = mini 3D; aliados/rivales = retrato hexagonal
// sobre peana (342+5k); Saibaman = cabeza animada sobre hoyo (#AMA 343+5k). No sustituir por modelos de cuerpo entero.
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
  // DS23: los personajes que no existen en B3 muestran su propio retrato de B2 (CMN 1196); el resto, la cara de B3.
  if (ImTextureID cara = SoloB2(f.c) ? CabezaB2(f.c) : CaraDe(B2aB3(f.c))) {
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
//   Goku: mini 3D del tablero (con sprite 155 como respaldo); aliados/rivales: retrato hexagonal sobre la peana 3D de ficha;
//   Saibaman: cabeza del #AMA sobre el hoyo. Corroborado por original_fase1_p1.png; no usar cuerpos enteros aqui.
float DibPieza(ImDrawList* dl, const Cam& k, const Ficha& f, ImVec2 pos, float t, ImU32 col) {
  ImVec2 p;
  float e = 1;
  if (!k.P(pos.x, pos.y, p, &e)) return -1;
  // EU 0x278E40: el +0x64 es la atenuacion (0 normal, 0,5 a la mitad); se aplica al color de sprites y vertices 3D.
  const float alfa = std::clamp(1.0f - std::clamp(f.opac, 0.0f, 1.0f), 0.0f, 1.0f);
  col = (col & 0x00FFFFFFu) | (ImU32(float(col >> 24) * alfa) << 24);
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
    if (!DibGoku3D(dl, p, e, t, Andando(f, g_ahora), en_lucha ? std::max(0.001f, g_ahora - g_pend_t) : 0,
                   busca >= 0 && busca < 36.0f / 60.0f ? std::max(0.001f, busca) : 0, alfa,
                   f.anim_b2, g_ahora - f.anim_t0, f.anim_estado) && g_tgoku)
      dl->AddImage(g_tgoku.id(), ImVec2(p.x - 29 * e, p.y - 80 * e), ImVec2(p.x + 29 * e, p.y), ImVec2(0, 0), ImVec2(1, 1), col);
    arriba = p.y - 90 * e;   // 13,33 u x 512/63 x cos 27 grados, entre k
  } else if (DibFicha3D(dl, f.idx, p, e, t + float(f.n) * 0.37f, GiroDeOrden(f, g_ahora), Andando(f, g_ahora), en_lucha,
                        busca, f.anim_b2, g_ahora - f.anim_t0, f.anim_estado, alfa)) {
    arriba = p.y - 104 * e;   // (11 u x 512/50 x cos 27 grados, entre k) la ficha 3D del original (CMN 335) con el retrato y el dorso de su pieza
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
    DibFicha(dl, f, ImVec2(p.x, p.y - 6 * e), 49 * e, t, col);   // B2: 0,7x el hexágono; el modelo alto debe medir ~1,4x su altura
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

// EU 0x27A790 FUN_0027a790 (posicion de la pieza en el mapa, ya con el desplazamiento de su hueco)
ImVec2 PosFicha(const Ficha& f) {
  if (f.punto != 0) {   // ordenes 65/66: punto del mapa, sin tocar la casilla logica
    const float k = f.punto_dur > 0 ? std::clamp((g_ahora - f.punto_t0) / f.punto_dur, 0.0f, 1.0f) : 1.0f;
    return ImVec2(f.punto_x0 + (f.punto_x - f.punto_x0) * k, f.punto_y0 + (f.punto_y - f.punto_y0) * k);
  }
  const Casilla &a = g_d.casillas[size_t(f.desde)], &b = g_d.casillas[size_t(f.casilla)];
  // lineal, tras el giro (0x273630: inicio + direccion * 4 * cuadros)
  const float k = std::clamp((g_ahora - f.t_mov - f.t_giro) / std::max(f.t_dur, 0.01f), 0.0f, 1.0f);
  // EU 0x273950 FUN_00273950 (x, y de la casilla + el desplazamiento del hueco: tabla 0x3A2C20 -> 0x2739A8..; solo el 0 y el 1 se usan)
  // hueco 1 de la casilla: (+60, -20) (FUN_00273950 -> 0x2739A8)
  const float ax = a.x + (f.slot_ant ? 60.0f : 0.0f), ay = a.y + (f.slot_ant ? -20.0f : 0.0f);
  float bx = b.x + (f.slot ? 60.0f : 0.0f), by = b.y + (f.slot ? -20.0f : 0.0f);
  if (f.corta > 0) {   // contacto: se queda a `corta` px del centro de destino, sobre la recta de llegada
    const float dx = bx - ax, dy = by - ay, d = std::hypot(dx, dy);
    if (d > f.corta) {
      bx -= dx / d * f.corta;
      by -= dy / d * f.corta;
    }
  }
  return ImVec2(ax + (bx - ax) * k, ay + (by - ay) * k);
}

// studies 2 G04: ordenes 65/66 (FUN_002774F0). Arranca el movimiento al punto (x, y) a 4 px por cuadro (240 px/s) o,
// si ya va a ese mismo punto, devuelve lo que le queda. El hilo de la orden 66 espera a que FUN_002774F0 diga que llego;
// la 65 lo ignora y la pieza sigue andando sola. DS45: si ya esta en el punto, el original termina la tarea en el acto
// (FUN_002774F0 devuelve != 0), no espera 2 cuadros.
// EU 0x2774F0 FUN_002774f0 (anda a un punto del mapa a 4 px por cuadro; ordenes 65/66)
float IrAPunto(Ficha& f, int orden, float x, float y, float ahora) {
  if (f.punto == 1 && f.punto_orden == orden && f.punto_x == x && f.punto_y == y) {
    const float resta = f.punto_t0 + f.punto_dur - ahora;
    return resta > 0.0f ? resta : 0.0f;   // DS45: ya en el punto -> la tarea acaba en el acto
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
  f.anim_b2 = -1;   // andar al punto tambien vuelve al reposo (ordenes 65/66 y la vuelta del combate)
  f.anim_estado = 0;
  // EU 0x2733B0 FUN_002733b0
  f.punto_dur = d / 240.0f;   // 0x2733B0 FUN_002733B0 (de FUN_002774F0): 4 px por llamada = por cuadro de 60 Hz
  if (d > 0.001f) {   // mira hacia el punto y anda (el giro del original es el mismo de los pasos por casilla)
    const float g = g_ant;
    f.giro0 = g;
    f.giro1 = std::atan2(dx, dy);
  // EU 0x277DD0 FUN_00277dd0 (la pieza del jugador se pone mirando al destino sin girar)
  f.t_giro = f.jugador ? 0.0f : std::fabs(Dif(g, f.giro1)) / kVelGiro;
    if (f.jugador) f.giro0 = f.giro1;
    f.t_mov = ahora;
    f.t_dur = f.punto_dur;
  }
  return d > 0.001f ? f.punto_dur : 0.0f;   // DS45: ya en el punto -> sin espera
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

// Recolocacion andando tras el combate. EU 0x2778F0 FUN_002778f0 (hueco destino 0) / EU 0x2779D0 FUN_002779d0 (hueco 1)
// ponen el estado 5 de la pieza con su destino = su casilla; FUN_00277470 espera a que ninguna pieza este en el estado 5.
// EU 0x278400 FUN_00278400 elige la pieza del hueco 1/2 de la casilla del ganador: la que entro a luchar (el que se movio).
// El paso es el mismo de las ordenes 65/66 (4 px por cuadro): desde el punto de contacto (60 px antes) al centro del hueco.
void EmpezarVuelta(int i) {
  if (i < 0 || i >= int(g_f.size()) || !Activa(g_f[size_t(i)])) {
    g_vuelta = true;   // nada que recolocar: ActualizarVuelta cierra la accion (o encadena el segundo contacto)
    return;
  }
  Ficha& f = g_f[size_t(i)];
  const Casilla& c = g_d.casillas[size_t(f.casilla)];
  const ImVec2 p = PosFicha(f);
  const float d = std::hypot(c.x - p.x, c.y - p.y);
  if (d < 0.5f) {
    f.punto = 0;
    f.corta = 0;
    g_vuelta = true;   // ya esta en el centro: ActualizarVuelta cierra la accion (o encadena el segundo contacto)
    return;
  }
  IrAPunto(f, 100, c.x, c.y, g_ahora);   // 100 = marca interna de la vuelta (no es una orden del guion)
  f.corta = 0;
  f.slot = f.slot_ant = 0;   // 0x2778F0: hueco destino = 0
  g_vuelta = true;
  REXLOG_INFO("dbz3 tablero b2: recolocacion andando de {} a la casilla {} ({:.0f} px)", Nombre(f), f.casilla, d);
}

// EU 0x236420 FUN_00236420 (estado 0x14, b2.c:189347-189380; aqui, la parte de T4 + guion de contacto: el resto del estado,
// FUN_0027EEF0/FUN_00279050, son los datos del combate y los pone pasarela_misiones)
// Segundo rival de la casilla del ganador (EU 0x278320 FUN_00278320, estado 3 de EU 0x2360B0 FUN_002360b0). El original
// encadena el contacto con la MISMA maquina de estados que el primero (FUN_00236420) y en su estado 0x14 vuelve a llamar a
// FUN_0027F510 con las dos piezas (uGpffff88bc = la elegida, uGpffff88c0 = el segundo rival): el suceso T4 se recalcula y
// su guion de contacto se lanza (FUN_002960C0) igual que en un contacto normal. Despues, al acabar el combate, FUN_0027F3A0
// (T5) busca la fila con ese suceso y su codigo. El port se saltaba ese T4: g_suceso quedaba en -1, T5 no arrancaba ningun
// guion y el KO de un jefe encadenado no cerraba la fase. DS136 (fase 2): tras caer Ginyu, Gohan lucho contra Freezer como
// segundo rival de la casilla 17, Freezer cayo... y no salio el guion 222 (T5 suceso 22 codigo 4) -> 303 -> orden 41, asi
// que la fase se quedo en "fase enemiga del turno N" para siempre.
void SegundoContacto(int segundo) {
  Contacto(g_sel, segundo);   // con T4 (FUN_0027F510), como FUN_00236420 estado 0x14
}

void ActualizarVuelta() {
  if (!g_vuelta || g_vuelta_pend >= 0) return;
  for (const Ficha& f : g_f)
    if (f.punto == 1 && f.punto_orden == 100) return;
  for (Ficha& f : g_f)
    if (f.punto == 2 && f.punto_orden == 100) f.punto = 0;
  g_vuelta = false;
  if (g_2contacto >= 0) {
    const int segundo = g_2contacto;
    g_2contacto = -1;
    SegundoContacto(segundo);   // 0x2360B0 estado 3 -> FUN_00236420 estado 0x14 (con T4)
    return;
  }
  FinDeAccion();
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
  // EU 0x276F50 FUN_00276f50 (camara al punto medio entre la ficha y la casilla elegida)
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
    obj = ImVec2(obj.x + g_cam_desv.x, obj.y + g_cam_desv.y);
  }
  // Camara libre del port: el original entra en la vista con 0x237790 (R2 "Display Status", FUN_00237790) y sale con
  // 0x237730 (FUN_00237730); dentro, el control es 0x23B080 (FUN_0023B080, 5 px/cuadro). El port reproduce las tres:
  // el estado `g_vista` es la tarea de entrada/salida y el movimiento con flechas es 0x23B080.
  // EU 0x23B080 FUN_0023B080
  if (g_vista) {   // 0x23B080 FUN_0023B080: +-5 px por cuadro de 60 Hz con las flechas
    const float v = 5.0f * 60.0f * std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    float px = 0, py = 0;
    if (g_mantenidos_ok) {
      px = float(!!(g_mantenidos & kRight)) - float(!!(g_mantenidos & kLeft));
      py = float(!!(g_mantenidos & kDown)) - float(!!(g_mantenidos & kUp));
    }
    g_cam_x += px * v;
    g_cam_y += py * v;
    obj = ImVec2(g_cam_x, g_cam_y);
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
      if (ad.tipo != 4 || ad.obj < 0 || ad.obj >= int(g_d.sprites.size()) || !Condiciones(ad.conds, 0)) continue;
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
  // EU 0x238AF0 FUN_00238af0 (adornos T7 con su lista de banderas: n > 0 pide la bandera n puesta, -n apagada; tipo 4 z 100030, 3 z 100010, 2 z 99990)
  // adornos de la fase (345+5k con su recorte del #AMA 344+5k) en sus sitios de la tabla 7; bajo los caminos.
  // Tipos 0/2/3 y 1 (grupo): se dibujan todos; 0/1/2/3 usan la lista A (pose) y la lista B (fotograma/textura).
  for (const auto& ad : g_d.colocados) {
    if (ad.tipo == 4 || !Condiciones(ad.conds, 0)) continue;   // capa de fondo: va antes del mapa; las banderas como en 0x238AF0
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
    // 0x23B900 / 0x23B640: el tiempo se reduce a la duracion del #AMA (modulo), asi que pasada la duracion (a3 cuadros
    // extra, o el bucle) la animacion vuelve a empezar
    // EU 0x23B900 FUN_0023b900
    // EU 0x23B640 FUN_0023b640
    const float durc = std::max(1.0f, g_d.textos[size_t(ef.k)].dur);
    const float cuadro = std::fmod((t - ef.t0) * 60.0f, durc);
    const Lienzo Lp = LienzoDe(a);
    if (t >= ef.t0 && (ef.pantalla ? (p = Lp.P(ef.x, ef.y), e = Lp.s, true) : k.P(ef.x, ef.y, p, &e))) {
      Lienzo L;
      L.o = p;
      L.s = e;
      const Datos::Sprite& sp = g_d.textos[size_t(ef.k)];
      DibSp(dl, g_d.textos, g_tloc, ef.k, L, cuadro, IM_COL32_WHITE, -sp.px, -sp.py);
    }
    ++i;
  }
  // studies 8 (E), ordenes 83/84: "攻/防 +N%" sobre la pieza (ver Subida). El #AMA 397 se dibuja con las texturas de 398
  // (uRam00698DCC / uRam00698DB4), con su ancla en el punto de la pieza en el mapa (FUN_0027A790), como el original.
  for (size_t i = 0; i < g_son_dif.size();) {   // sonidos programados de los objetos recogidos
    if (t >= g_son_dif[i].t) {
      SonidoCanal3(g_son_dif[i].id);
      g_son_dif.erase(g_son_dif.begin() + std::ptrdiff_t(i));
    } else {
      ++i;
    }
  }
  if (g_vz.abierta) {   // EU 0x253F80 FUN_00253f80: contador de la ventana de zenie
    VentanaZenie& v = g_vz;
    if (v.estado == 5) {
      const int debidos = int((t - v.t_cuenta) * 60.0f) - v.cuadro;   // cuadros de 60 Hz pasados desde el ultimo avance
      for (int q = 0; q < debidos && v.estado == 5; ++q) {
        ++v.cuadro;
        v.total += v.paso;
        v.resto -= v.paso;
        if (++v.tics > 5) {   // FUN_00147420 + FUN_00147260(0, 0, 0xE): cada 6 cuadros
          v.tics = 0;
          SonidoCanal3(0xE);
        }
        if (v.resto <= 0) {   // se acaba el importe: se suma lo que sobra, tope 99.999.900, sonido 0xF y se mantiene
          v.total = std::min(v.total + v.resto, 99999900);
          v.resto = 0;
          v.estado = 3;
          v.t_fin = t;
          SonidoCanal3(0xF);
        }
      }
    } else if (t - v.t_fin > 0.5f) {   // 0x27A980 caso 3: 30 unidades de dt (0,5 s) y se cierra
      v.abierta = false;
    }
    if (v.abierta) {
      const Lienzo Lp = LienzoDe(a);
      const ImVec2 p0 = Lp.P(448, 40), p1 = Lp.P(624, 88);
      dl->AddRectFilled(p0, p1, IM_COL32(20, 24, 60, 220), 6 * Lp.s);
      dl->AddRect(p0, p1, IM_COL32(255, 220, 120, 255), 6 * Lp.s, 0, 2 * Lp.s);
      char tx[32];
      std::snprintf(tx, sizeof(tx), "%d", v.total);
      dl->AddText(ImGui::GetFont(), 22 * Lp.s, Lp.P(460, 52), IM_COL32(255, 255, 255, 255), tx);
      dl->AddText(ImGui::GetFont(), 12 * Lp.s, Lp.P(580, 66), IM_COL32(255, 220, 120, 255), "ZENIE");
    }
  }
  for (size_t i = 0; i < g_subidas.size();) {
    Subida& s = g_subidas[i];
    const auto& la = g_d.life_ama;
    const float c = (t - s.t0) * 60.0f;
    float fin = s.signo ? 0.0f : 120.0f;
    for (int j = 0; j < s.Nan(); ++j) {
      const int an = s.AnBase() + j;
      if (an < int(la.size())) fin = std::max(fin, la[size_t(an)].dur - 1);
    }
    const float dur_cifra = !s.sin_cifra && la.size() > 0x4D ? la[0x4D].dur : 0.0f;   // FUN_0027BD00: +0x34 = FUN_00245C00(397, 0x4D)
    // DS45: 0x27A980 lanza el sonido del tipo a los 110 cuadros (>110) en la misma tarea; la tarea no puede borrarse antes o
    // el sonido no sale (daga sin 0xD/armadura sin 0x12 si la animacion dura menos).
    const float fin_snd = s.snd110 ? 111.0f : 0.0f;   // el sonido sale cuando el tiempo PASA de 110 (cuadro 111)
    if (c > std::max(std::max(fin, dur_cifra), fin_snd) || (la.size() <= 123 && !s.snd60 && !s.snd110)) {
      g_subidas.erase(g_subidas.begin() + std::ptrdiff_t(i));
      continue;
    }
    ++i;
    if (s.snd60 && !s.son60 && c >= 60) {   // 0x27A980: a los 60 cuadros 0xD (pieza del jugador) o 0xA (rival)
      SonidoCanal3(s.snd60);
      s.son60 = true;
    }
    if (!s.sonado && c >= 110) {   // b2.c:237756-237759 / 237832-237835
      SonidoCanal3(s.snd110 ? s.snd110 : s.defensa ? 0x12 : 0x0C);
      s.sonado = true;
    }
    const Ficha* f = PorN(s.n);
    ImVec2 p;
    float e = 1;
    if (!f || !k.P(PosFicha(*f).x, PosFicha(*f).y, p, &e)) continue;
    Lienzo L;
    L.o = p;
    L.s = e;
    for (int j = 0; j < s.Nan(); ++j) {
      const int an = s.AnBase() + j;
      if (an < int(la.size())) DibNativa01(dl, la, g_ticonos, an, L, std::min(c, la[size_t(an)].dur - 1));
    }
    if (c < dur_cifra) {   // EU 0x27BAB0 FUN_0027bab0: la pose de 0x4D con cada imagen en su sitio
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
  // EU 0x275440 FUN_00275440
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

// Marcador del original (CMN 402 + DATA_<idioma> 188), en la pantalla de 640x448. El port lo compone con sprites y
// temporizadores propios, mas abajo; por eso estas rutinas (0x26E6F0, 0x26EA10, 0x26F5D0, 0x26F940) no se citan como
// traducidas: son aproximaciones [VERIFICAR] (auditoria R3).
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
      // EU 0x26F5D0 FUN_0026f5d0 (D_RADARBASE: barra 0 entrar / 1 reposo / 2 salir, "MUNDO DRAGON" 3/4/5 y las Bolas)
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
      // EU 0x26F940 FUN_0026f940 (D_RADAR: radar 16/17/18, brillo 89 y por Bola la flecha 87 o el punto 88)
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
    // EU 0x26FDE0 FUN_0026fde0 (arranca la tarea del rombo: +0x3C = 1, +0x40 = 2)
    // EU 0x26EA10 FUN_0026ea10 (estado 1: anim 0x26 = 38 hasta que acaba su duracion)
    // EU 0x26E910 FUN_0026e910 (estado 2: anim 0x27 = 39 en bucle mientras +0x40 != 0; FUN_0026FDD0, que lo pondria a 0 para pasar a
    // la salida 0x28 de FUN_0026E840, no la llama nadie en el ELF europeo: el rombo se queda en 39)
    const float cd = (t - g_rombo_t) * 60.0f / lento;
    const float d38 = g_d.hud_ama.size() > 38 ? std::max(1.0f, g_d.hud_ama[38].dur) : 10.0f;
    // EU 0x26E840 FUN_0026e840 (estado 3: salida, anim 0x28 = 40, cuando 0x237F70 pone +0x40 = 0 al acabar el turno enemigo)
    const float d40 = g_d.hud_ama.size() > 40 ? std::max(1.0f, g_d.hud_ama[40].dur) : 10.0f;
    const bool sale = g_rombo_fin >= 0;
    const float c40 = (t - g_rombo_fin) * 60.0f / lento;
    if (!sale || c40 < d40) DibHudSp(dl, sale ? 40 : cd < d38 ? 38 : 39, L, sale ? c40 : cd < d38 ? cd : -1.0f);
    if (sale && c40 >= d40) return;   // oculto: tampoco hay cifras
    // EU 0x26E6F0 FUN_0026e6f0 (cifras del turno, hasta 99: unidades anim 0x29 + d en el sitio de la 41 o la 42, decenas en el de la 43)
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
  // DS138: 696..764 (antes 702..770); la banda de ayuda del tablero empieza en 769 y se tocaba en 1 u.
  dl->AddRectFilled(a.At(250, 696), a.At(950, 764), IM_COL32(10, 24, 70, 215), 12 * a.s);
  dl->AddRect(a.At(250, 696), a.At(950, 764), IM_COL32(120, 170, 255, 220), 12 * a.s, 0, 2 * a.s);
  float x = 600;
  if (g_msg_icono >= 0) {
    if (g_msg_icono == 2) {
      const int n = g_d.fase % 7;
      DibIcono(dl, 2, a.At(266, 708), a.At(310, 752), ImVec4(40.0f * float(n), 0, 40.0f * float(n + 1), 40));
    } else {
      DibIcono(dl, g_msg_icono, a.At(266, 708), a.At(310, 752));
    }
    x = 620;
  }
  S::MenuText(dl, a.At(x, 730), 30 * a.s, g_msg.c_str(), true, false, 600 * a.s);
}

void AyudaEscena(ImDrawList* dl, const S::Area& a, bool comic) {
  if (comic) return;
  const float h = 34.0f * a.s;
  S::HelpLine(dl, a.At(600, 835), h, {{"A", T::T("Siguiente", "Next")}, {"B", T::T("Saltar", "Skip")}});
}

}  // namespace

// Forma inicial B3 de una pieza del tablero (Cell perfecto / Freezer final por defecto; orden 52 si la hay).
int FormaTableroB3(int id_b3, int forma_b2) { return FormaTableroB3Impl(id_b3, forma_b2); }

// studies 2 G04: COM del rival de la orden 51 (+0x18 del rival -> 0x699365). -1 = ese combate no trae orden 51.
// pasarela_misiones.cpp lo declara en su propio ambito (no se toca pasarela.h) y lo lleva a Reglas::nivel_com.
int ComRivalB2() { return g_com_rival_b2; }

// Cabeza de B2 (CMN 1196) del luchador `id_b3` de B3 (el primer ID de B2 que le corresponde); nada si no hay.
// DS23: se saltan los personajes que solo son de B2 (SoloB2), para que el donante (p.ej. Zarbon -> 38) no le preste
// su cara a Cooler.
ImTextureID CabezaDeB3(int id_b3) {
  for (int c = 0; c < kB2Personajes; ++c)
    if (!SoloB2(c) && B2aB3(c) == id_b3) return CabezaB2(c);
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
  // EU 0x23B290 FUN_0023b290 (desde FUN_00238870): al entrar en el Mundo Dragon la camara vuelve a (0,0)
  g_cam_x = g_cam_y = 0;
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
  // EU 0x23B290 FUN_0023b290: la rama de vuelta del combate de FUN_00238870 vuelve a poner la camara en (0,0)
  g_cam_x = g_cam_y = 0;
  g_2contacto = -1;
  g_luchando = false;
  const int pj = g_pend_j, pr = g_pend_r, mv = g_pend_mov;
  // EU 0x2360B0 FUN_002360b0 estado 2: la pieza que entro a luchar (hueco 1) vuelve al centro andando (0x278400 ->
  // 0x2778F0); no se le quita el recorte de 60 px hasta que EmpezarVuelta lo usa como punto de partida.
  for (Ficha& f : g_f) {
    if (int(&f - g_f.data()) == mv) continue;
    f.corta = 0;
  }
  g_pend_j = g_pend_r = g_pend_mov = -1;
  if (pj >= 0 && pj != mv) g_f[size_t(pj)].punto = 0;   // vuelve a su casilla (andaba hasta el rival)
  if (pj >= 0 && pr >= 0 && ganador >= 0) {
    const Ficha &fj = g_f[size_t(pj)], &fr = g_f[size_t(pr)];
    // EU 0x2360B0 FUN_002360B0 estado 0: el tipo T5 es 4/1 si el GANADOR (uGpffff8198) es la pieza elegida (uGpffff88d4) y 3/2 si no;
    // 4 y 3 con el perdedor sin vidas (FUN_0027A0D0). Antes: por bando (gana el jugador), distinto cuando una pieza del jugador
    // que no es la elegida gana defendiendo, o la elegida pierde.
    // EU 0x2360B0 FUN_002360b0
    const int ganadora = ganador == 0 ? pj : pr;
    SucesosDeCombate(ganadora == g_sel, ganador == 0 ? fr.barras - 1 <= 0 : fj.barras - 1 <= 0);
  }
  if (pj < 0 || pr < 0 || ganador < 0) return FinDeAccion();
  Ficha &j = g_f[size_t(pj)], &r = g_f[size_t(pr)];
  PonerPose(j, -1, 0);   // el ganador vuelve a reposo
  PonerPose(r, -1, 0);
  REXLOG_INFO("dbz3 tablero b2: combate {} contra {}: gana {}, vida {}", Nombre(j), Nombre(r), ganador == 0 ? "el jugador" : "el rival", vida);
  if (ganador == 0) {
    j.vida = vida > 0 && vida < j.vida_max ? vida : 0;
    // FAQ: si estaba aturdido, el golpe quita 2 barras. El ELF (FUN_0027A980/FUN_00276E10, ver comentario de Ficha)
    // no define estado aturdido al perder: se conserva -1 barra y no se inventa aturdimiento.
    r.barras -= 1;   // R7: sin "aturdido" (de la FAQ; el ELF no lo tiene)
    // EU 0x277BB0 FUN_00277bb0 (reaccion del perdedor: sin vidas -> KO; con vidas -> golpeado)
    if (SinBarras(r)) {
      // EU 0x277BB0 FUN_00277bb0 (perdedor sin vidas: +0 = 8 y animacion 4; tabla 0x379010 -> clip 7)
      PonerPose(r, 4, 0x08);
      r.fuera = true;
      Aviso(Nombre(r) + T::T(" ha caído.", " is down."));
    } else {
      // EU 0x277BB0 FUN_00277bb0 (perdedor con vidas: +0 = 7 y animacion 5; tabla 0x379010 -> clip 8)
      PonerPose(r, 5, 0x07);
      Aviso(Nombre(r) + T::T(" pierde una barra.", " loses a bar."), 4);
    }
  } else {   // Regla del tablero: una derrota quita una barra a la ficha propia; si sobrevive, vuelve con vida llena.
    if (j.c == kGoku && j.bando == 4) {   // FUN_00277BB0: la pieza que pierde, golpeada o derribada
      g_goku_reac_t = g_ahora;
      g_goku_reac_ko = j.barras - 1 <= 0;
    }
    j.vida = 0;
    --j.barras;
    if (SinBarras(j)) {
      // EU 0x277BB0 FUN_00277bb0 (perdedor sin vidas: +0 = 8 y animacion 4)
      PonerPose(j, 4, 0x08);
      j.fuera = true;
      // EU 0x27A7B0 FUN_0027a7b0 (la pieza 1, Goku, sin vidas: cRam0069b7c5 == 0)
      bool queda = false;
      for (const Ficha& f : g_f) queda |= Activa(f) && f.jugador && f.c == kGoku;
      if (!queda) {
        g_estado = kPerdida;
        // DS131: la derrota cierra la fase. CorrerHilos solo corre en kJuego, asi que el guion de derrota que abrio
        // SucesosDeCombate (script 302) se quedaba en g_hilos para siempre: HilosOcupados() bloqueaba el reintento
        // automatico y la prueba se quedaba 10 min en GAME OVER. El original no deja la tarea del combate viva.
        g_hilos.clear();
        g_espera_x = false;
        BorrarEstadoFase();   // derrota: el reintento empieza la fase de cero
        REXLOG_INFO("dbz3 tablero b2: fase {} perdida", g_d.fase + 1);
        return Aviso(T::T("Goku ha caído.", "Goku is down."));
      }
      Aviso(Nombre(j) + T::T(" queda fuera del tablero.", " is out of the board."));
    } else {
      // EU 0x277BB0 FUN_00277bb0 (perdedor con vidas: +0 = 7 y animacion 5; tabla 0x379010 -> clip 8)
      PonerPose(j, 5, 0x07);
      Aviso(Nombre(j) + T::T(" pierde una barra.", " loses a bar."), 15);
    }
  }
  if (g_sel >= 0 && !Activa(g_f[size_t(g_sel)])) SiguienteSel();
  // La pieza que entro a luchar ocupa la casilla del otro (el jugador no la cambiaba al acercarse: solo andaba al punto).
  if (mv >= 0 && mv < int(g_f.size()) && Activa(g_f[size_t(mv)])) {
    Ficha& m = g_f[size_t(mv)];
    if (mv == pj && pr >= 0) {
      m.desde = m.casilla;
      m.casilla = g_f[size_t(pr)].casilla;
    }
    g_ult_mov = mv;
    RecalcularSlots();
  } else if (mv >= 0 && mv < int(g_f.size())) {
    g_f[size_t(mv)].corta = 0;
    g_f[size_t(mv)].punto = 0;
  }
  // EU 0x278320 FUN_00278320: segundo rival en la casilla del ganador (hueco 0/2; el 1 no cuenta) si el ganador es la
  // pieza elegida: nuevo contacto al acabar la vuelta (0x2360B0 estado 3). El que acaba de luchar esta en el 1, no cuenta.
  g_2contacto = -1;
  if (ganador == 0 && g_estado == kJuego && g_sel == pj && Activa(g_f[size_t(pj)])) {
    // Se excluye al que acaba de luchar (pr) aunque su hueco sea el 0: el segundo contacto es con OTRO rival de la casilla.
    int segundo = -1;
    for (size_t i = 0; i < g_f.size(); ++i) {
      const Ficha& f = g_f[i];
      if (int(i) == pr || !Activa(f) || !f.rival || f.casilla != g_f[size_t(pj)].casilla) continue;
      if (f.slot == 0) {
        segundo = int(i);
        break;
      }
      if (segundo < 0) segundo = int(i);
    }
    if (segundo >= 0) {
      g_2contacto = segundo;
      REXLOG_INFO("dbz3 tablero b2: segundo rival en la casilla {}: {} (0x278320)", g_f[size_t(pj)].casilla,
                  Nombre(g_f[size_t(segundo)]));
    }
  }
  if (mv >= 0 && mv < int(g_f.size()) && Activa(g_f[size_t(mv)]))
    g_vuelta_pend = mv;   // el paso empieza al volver al tablero, con g_ahora al dia y tras el guion T5
  if (g_vuelta_pend >= 0 || g_2contacto >= 0) return;   // el turno sigue en DibujarTablero (vuelta) o en ActualizarVuelta
  FinDeAccion();
}

// s9b: la ayuda de abajo del tablero del original (medida en PCSX2, savestates 3 y 4 de la fase I): una banda oscura y
// difuminada de ~24 px de alto (y 394-417 del lienzo de 448) a todo el ancho, con las frases en blanco que pasan de derecha a
// izquierda sin parar (~72 px/s = 1,2 px por cuadro; medido: 125 px en 1,7 s) y su boton delante. Cambia con el modo: elegir
// ficha o elegir casilla destino. El marcador de madera ("DRAGON WORLD") la tapa por la izquierda cuando esta puesto.
void AyudaMarquesina(ImDrawList* dl, const S::Area& a, float t) {
  const Lienzo L = LienzoDe(a);
  const bool destino = g_dest > 0;
  // Indices de DATA_<idioma> 1145 (una tira por frase del original; orden de las mascaras de ayuda del tablero).
  static const int kSel[6] = {10, 13, 14, 15, 23, 29};
  static const int kDest[7] = {9, 13, 14, 15, 19, 22, 28};
  const int* nativas = destino ? kDest : kSel;
  const int nnat = destino ? 7 : 6;
  // I01 (studies 4): la banda va del borde izquierdo al derecho de la pantalla del original (640), no de la
  // ventana: en 16:9 el texto asomaba fuera del area 4:3 (a la izquierda del marcador).
  const float xl = (!g_hud_oculto && g_radar) ? L.P(272, 0).x : L.P(0, 0).x;
  const float xr = L.P(640, 0).x;
  if (DibAyudaTiras(dl, L, t, nativas, nnat, false)) {
    // La banda es la del port (medida); las frases ya son las tiras nativas de 1145, no fuente.
    const float y0 = 392.0f, y1 = 424.0f, h = 34.0f * L.s * L.k;
    const float ya = L.P(0, y0).y, yb = L.P(0, y1).y, borde = 3.0f * L.s * L.k;
    dl->PushClipRect(ImVec2(xl, ya - h), ImVec2(xr, yb + h), true);
    dl->AddRectFilledMultiColor(ImVec2(xl, ya - borde), ImVec2(xr, ya + borde), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 96), IM_COL32(0, 0, 0, 96));
    dl->AddRectFilled(ImVec2(xl, ya + borde), ImVec2(xr, yb - borde), IM_COL32(0, 0, 0, 96));
    dl->AddRectFilledMultiColor(ImVec2(xl, yb - borde), ImVec2(xr, yb + borde), IM_COL32(0, 0, 0, 96), IM_COL32(0, 0, 0, 96), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    DibAyudaTiras(dl, L, t, nativas, nnat, true);
    dl->PopClipRect();
    return;
  }
  struct Frase {
    const char* boton;
    std::string texto;
  };
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
  const float y0 = 394.0f, y1 = 418.0f, h = 20.0f * L.s * L.k;
  ImFont* fuente = ImGui::GetFont();
  const float tam = 18.0f * L.s * L.k;
  // banda: 3 franjas (borde de arriba, centro, borde de abajo) para el difuminado
  const float ya = L.P(0, y0).y, yb = L.P(0, y1).y, borde = 3.0f * L.s * L.k;
  // 25/10 (clip del tablero B2, revisado por Opus: el texto de la marquesina salia cortado por la mitad de abajo; la fuente
  // dibuja las letras mas abajo que el centro de su caja): el recorte solo limita a los lados, no por arriba ni por abajo
  dl->PushClipRect(ImVec2(xl, ya - h), ImVec2(xr, yb + h), true);
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
        dl->AddText(fuente, tam, ImVec2(pos.x + 1.2f * L.s, pos.y + 1.2f * L.s * L.k), IM_COL32(0, 0, 0, 200), f.texto.c_str());
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
// EU 0x2A35F0 FUN_002a35f0 (menu del tablero: Salir / Reiniciar Mundo Dragon) y EU 0x2A2C90 (Si/No)
int MenuTablero(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed) {
    const Lienzo L = LienzoDe(a);
    const float c = (t - g_menu_t) * 60.0f / Lento();
    const ImU32 apagado = IM_COL32(110, 110, 130, 255);
    // DWU_MMAIN (CMN 402): el original no oscurece el fondo al abrir este panel. Ver comparativa directa de fase I
    // (captura original antes/despues de Start, `capturas_luna/coord_b2_e3.png` y `luna17_e3_menu.png`).
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
      // El original muestra la opcion activa por su iluminacion y no presenta una flecha separada en el panel DWU_YN
      // (confirmado en PCSX2/PINE, captura luna17_e3_confirm2.png con "No" activo y luna17_e3_confirm_yes.png con "Yes").
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
        GuardarProgreso("tablero_b2_fase", 0);  GuardarBanderas(true);  BorrarEstadoFase();
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
  PasoFundido();   // EU 0x278E40: +0x64 de las piezas, 0,1 por cuadro
  // EU 0x2360B0 estado 2-3: primero acaba el guion T5 (g_hilos), luego la vuelta andando y, al llegar, el segundo contacto.
  const bool libre_t5 = !HilosOcupados() && !EscenaAbierta() && g_mensajes.empty() && g_elegir == 0 && g_premio_id == 0 &&
                        !g_espera_x;
  if (g_estado == kJuego && libre_t5 && g_vuelta_pend >= 0) {
    const int i = g_vuelta_pend;
    g_vuelta_pend = -1;
    EmpezarVuelta(i);
  }
  if (g_estado == kJuego && libre_t5 && !g_vuelta && g_vuelta_pend < 0 && g_2contacto >= 0) {
    const int segundo = g_2contacto;
    g_2contacto = -1;
    SegundoContacto(segundo);   // 0x2360B0 estado 3 -> FUN_00236420 estado 0x14 (con T4)
  }
  ActualizarPuntos();   // ordenes 65/66: llegada (libera la 66 en el cuadro exacto)
  ActualizarVuelta();   // EU 0x277470: recolocacion andando tras el combate; al llegar sigue el turno
  ActualizarAlfombra();   // alfombra de Mr. Popo: recolocacion y espera de 24 cuadros
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
  // EU 0x232E40 FUN_00232e40 (uRam006937A8 = 0: habilita el reinicio por mando mientras se juega)
  // EU 0x232E50 FUN_00232e50 (uRam006937A8 = 1: mismo reinicio durante la fase enemiga/guion)
  // FUN_002323D0: Select+Start (PS2 0x1|0x8; en XInput, Back+Start = 0x20|0x10) -> FUN_0022ED40(3), que desmonta la
  // escena y vuelve al menu. Aqui, salir del tablero.
  if (g_mantenidos_ok && (g_mantenidos & (kBack | kStart)) == (kBack | kStart)) {
    REXLOG_INFO("dbz3 tablero b2: reinicio por mando (Select+Start, 0x232E40/E50)");
    return 1;
  }
  if (const char* v = std::getenv("DBZ3_TEST_B2_TARJETA")) {   // PRUEBA: ensena solo la tarjeta "Battle!" (condicion v)
    static float t0 = -1;
    if (t0 < 0) t0 = t;
    Ficha goku;
    DibTarjeta(dl, a, t, (t - t0) * 60.0f, std::atoi(v), goku);
    return 0;
  }
  // PRUEBA: DBZ3_TEST_B2_ALFOMBRA=1 lleva a Goku a una alfombra de Mr. Popo nada mas estabilizarse el tablero, para medir
  // la animacion 12-15 y los 24 cuadros de espera sin depender de las teclas ni del auto-movimiento.
  static bool test_alf_done = false;
  if (!test_alf_done && std::getenv("DBZ3_TEST_B2_ALFOMBRA") && g_estado == kJuego && !g_fase_rival && !g_luchando &&
      !HilosOcupados() && !EscenaAbierta() && g_mensajes.empty() && g_elegir == 0 && g_premio_id == 0 && g_alf.i < 0 &&
      g_pend_j < 0) {
    for (size_t c = 1; c < g_d.casillas.size(); ++c) {
      if (g_d.casillas[c].tipo != 3 || g_d.casillas[c].destino <= 0) continue;
      if (FichaEn(int(c), false) >= 0 || FichaEn(int(c), true) >= 0) continue;
      int desde = -1;
      for (int v : g_d.casillas[c].vec)
        if (v > 0 && FichaEn(v, false) < 0 && FichaEn(v, true) < 0) { desde = v; break; }
      int i = -1;
      for (size_t k = 0; k < g_f.size(); ++k)
        if (g_f[k].jugador && !g_f[k].fuera && g_f[k].c == kGoku) { i = int(k); break; }
      if (desde < 0 || i < 0) continue;
      g_f[size_t(i)].casilla = desde;
      RecalcularSlots();
      test_alf_done = true;
      REXLOG_INFO("dbz3 tablero b2: prueba: alfombra: Goku de la casilla {} a la {}", desde, c);
      Mover(i, int(c));
      break;
    }
  }
  uint16_t mando = pressed;   // s9b: con el menu abierto el resto de la pantalla no recibe pulsaciones ni avanza
  if (g_menu > 0) pressed = 0;
  if (g_vuelta) pressed = 0;   // recolocacion andando (0x277470): sin mandos hasta que la pieza llega al centro
  if (g_cont_j >= 0 && g_ahora >= g_cont_t) {
    const int cj = g_cont_j, cr = g_cont_r;
    g_cont_j = g_cont_r = -1;
    Contacto(cj, cr);
  }
  if (g_estado == kJuego && g_menu == 0 && !g_vuelta && !std::getenv("DBZ3_TEST_B2_ALIADO")) CorrerHilos();
  // PRUEBA de imagen (solo con DBZ3_TEST_B2_MENU=1): abre el DWU cuando el tablero ya esta libre, sin inyectar una
  // tecla al guest. Permite comparar el panel en el mismo punto con PCSX2 sin que el auto-movimiento empiece un combate.
  static bool test_menu_abierto = false;
  if (!test_menu_abierto && std::getenv("DBZ3_TEST_B2_MENU") && g_estado == kJuego && !g_luchando && !g_fase_rival &&
      !HilosOcupados() && !EscenaAbierta() && g_mensajes.empty() && g_premio_id == 0 && g_elegir == 0) {
    test_menu_abierto = true;
    g_menu = std::getenv("DBZ3_TEST_B2_MENU_CONFIRM") ? 2 : 1;
    g_menu_sel = 0;
    g_yn_sel = 1;
    g_menu_t = t;
    REXLOG_INFO("dbz3 tablero b2: menu abierto (prueba{})", g_menu == 2 ? ", confirmacion" : "");
  }
  // PRUEBA de RE (solo DBZ3_TEST_B2_BUSCAR=1): verifica la ruta de sonido de FUN_00277D40 sin inyectar Cuadrado.
  static bool test_buscar_sonido = false;
  if (!test_buscar_sonido && std::getenv("DBZ3_TEST_B2_BUSCAR") && g_estado == kJuego && !g_menu && !g_luchando &&
      !g_fase_rival && !HilosOcupados() && !EscenaAbierta() && g_mensajes.empty() && g_premio_id == 0 && g_elegir == 0 &&
      g_sel >= 0 && g_sel < int(g_f.size()) && g_f[size_t(g_sel)].c == kGoku) {
    test_buscar_sonido = true;
    SonidoBuscarB2(g_f[size_t(g_sel)]);
    REXLOG_INFO("dbz3 tablero b2: prueba: sonido de buscar de Goku verificado");
  }
  // DS131: en las pantallas de fin de partida (GAME OVER / fase completada) la A del piloto no puede depender de los
  // guiones: si quedo algun hilo del combate, HilosOcupados() la bloqueaba y la prueba se quedaba en GAME OVER.
  const bool fin_partida = g_estado == kPerdida || g_estado == kGanada;
  if (auto_on && g_menu == 0 && !g_vuelta && (fin_partida || (!HilosOcupados() && !EscenaAbierta())) &&
      t - g_auto_t > 1.4f) {
    g_auto_t = t;
    // EU 0x294660 FUN_00294660 prepara la salida de fase completada; en derrota no
    // se debe reutilizar ese avance. La prueba encadenada escoge explícitamente
    // "Reintentar" (la acción A del menú de resultado del port) para reiniciar esta fase.
    if (g_estado == kPerdida) pressed |= kA;
    else if (g_estado == kGanada) pressed |= kA;
    else Auto();
  }
  const bool prepara = g_estado == kPrepara;
  DibTablero(dl, a, t, prepara);
  // PRUEBA de imagen: mantiene la eleccion de aliados visible y no avanza guiones/partida. "lista" deja la ventana tal
  // cual (sin marcar ni confirmar), para parear con los savestates de A2, que estan en ese estado.
  if (const char* modo_aliado = std::getenv("DBZ3_TEST_B2_ALIADO")) {
   if (g_estado == kJuego && !prepara && g_d.aliados_ama.size() >= 71) {
    std::vector<int> lista;
    for (int k : g_cand)
      if (k >= 0 && k < int(g_f.size()) && g_f[size_t(k)].fuera) lista.push_back(k);
    if (!lista.empty()) {
      auto_on = false;
      g_esc_auto = false;
      const bool solo_lista = std::string(modo_aliado) == "lista";
      const size_t marcado = std::min<size_t>(4, lista.size() - 1);   // fila visible de Krillin en fase I
      g_comp = solo_lista ? 0 : int(marcado);   // la lista original arranca con el cursor en la primera fila
      if (solo_lista) {
        g_marcadas.clear();
        g_conf = -1;
        if (g_elegir <= 0) g_elegir = (g_d.fase == 3 || g_d.fase == 4 || g_d.fase == 8) ? 2 : 1;
      } else {
        g_elegir = 1;
        g_marcadas.assign(1, lista[marcado]);
        g_conf = 1;
        g_conf_t = 1.0f;
      }
      DibAliados01(dl, a, t, lista);
      static bool trazado = false;
      if (!trazado) {
        REXLOG_INFO("dbz3 tablero b2: prueba: confirmacion de aliado dibujada {}", Nombre(g_f[size_t(lista[marcado])]));
        trazado = true;
      }
      return 0;
    }
   }
  }
  if (!prepara && !g_mensajes.empty()) {   // ventana de mensaje del script, en la pantalla de 640x448 del original
    if (g_mensaje_t < 0) g_mensaje_t = t;
    const Lienzo L = LienzoDe(a);
    // D_CLEAR (ELF 0x26F280 -> 0x26EFC0 -> 0x26ED50 -> 0x26EB00): la ventana del CMN 402 tiene tres juegos de 8 piezas
    // (entrar 0x33+3k, reposo 0x34+3k, salir 0x35+3k). Cada pieza dura lo que dice su lista A (FUN_00245C00); el juego
    // de cada fase se pinta hasta la duracion MAXIMA de sus 8 piezas, avanzando el fotograma por cuadro de 60 Hz. La 8.a
    // no tiene textura y oscurece la pantalla (velo negro del port). Antes solo se pintaba la pieza de entrada y se
    // borraba la ventana de golpe; ahora entra, reposa y sale como la tarea original.
    const float c = (t - g_mensaje_t) * 60.0f;
    const bool hay = g_d.hud_ama.size() > 74;
    auto dur_pieza = [&](int i) {
      return i >= 0 && i < int(g_d.hud_ama.size()) ? std::max(1.0f, g_d.hud_ama[size_t(i)].dur) : 10.0f;
    };
    auto dur_juego = [&](int base) {
      float d = 1.0f;
      for (int p = 0; p < 8; ++p) d = std::max(d, dur_pieza(base + 3 * p));
      return d;
    };
    const float dur_sal = dur_juego(0x35);
    if (hay) {
      dl->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize,
                        IM_COL32(0, 0, 0, g_msg_cerrando ? 100 : int(100 * std::min(1.0f, c / 20.0f))));
      if (!g_msg_cerrando) {
        const float dur_ent = dur_juego(0x33);
        if (c < dur_ent) {
          // EU 0x26EFC0 FUN_0026efc0 (entrada: piezas 0x33+3k; el fotograma se para en el ultimo)
          for (int p = 0; p < 8; ++p) DibHudSp(dl, 0x33 + 3 * p, L, std::min(c, dur_pieza(0x33 + 3 * p) - 1.0f));
        } else {
          // EU 0x26ED50 FUN_0026ed50 (reposo: piezas 0x34+3k)
          for (int p = 0; p < 8; ++p) DibHudSp(dl, 0x34 + 3 * p, L, c - dur_ent);
        }
      } else {
        // EU 0x26EB00 FUN_0026eb00 (salida: piezas 0x35+3k; al acabar su duracion la ventana desaparece)
        const float cs = (t - g_msg_cierre) * 60.0f;
        for (int p = 0; p < 8; ++p) DibHudSp(dl, 0x35 + 3 * p, L, std::min(cs, dur_pieza(0x35 + 3 * p) - 1.0f));
      }
    }
    if (!g_msg_cerrando)
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
    // EU 0x26F280 FUN_0026f280 (arranque del cierre: la tarea pasa por 0x26EFC0/0x26ED50 y, con +0x40 = 1, por 0x26EB00)
    if (!g_msg_cerrando && ((pressed & kA) || (t - g_mensaje_t > 3.0f && (!sigue39 || auto_on)))) {
      if (pressed & kA)
        for (Hilo& h : g_hilos) {
          const auto& g = g_d.guiones[size_t(h.guion)];
          if (h.i < g.size() && g[h.i][1] == 39) ++h.i;
        }
      g_msg_cerrando = true;
      g_msg_cierre = t;
      pressed &= uint16_t(~kA);
    } else if (g_msg_cerrando && t - g_msg_cierre >= dur_sal / 60.0f) {
      g_mensajes.erase(g_mensajes.begin());
      g_msg_cerrando = false;
      g_msg_cierre = -10;
      g_mensaje_t = g_mensajes.empty() ? -10 : t;   // si hay otra, entra con su propia D_CLEAR
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
    // cartel, X / cruceta da la vuelta a la ficha y A / Y la cierran. Los 30 cuadros NO son un bloqueo de la entrada: es
    // el segundo sonido del cartel (FUN_002A3E90: con 30.0 < contador y +0x3C == 0 reproduce tipo 0 id 1 una sola vez).
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
  // EU 0x2A2550 FUN_002a2550 (tarea de eleccion de aliados de la orden 69)
  if (g_estado == kJuego && g_elegir > 0 && !EscenaAbierta() && g_mensajes.empty()) {
    // orden 69: como el original (tarea 0x2A2550), las fichas libres en fila y "quedan N"; A elige, las elegidas salen
    // al tablero en su casilla del #YDW. El panel y el cursor son los del original: dib() usa el #AMA 399 (aliados_ama) y
    // las texturas DATA_<idioma> 184 (0-2 marco, 35 cursor, 38/43 casilla, 45+51 etiquetas), medido en LUNA17/INFORME.md
    // bloque 4 (marca de aliado, animacion 0x2B) y bloque 3 (cursor del "Is this okay?").
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
    if (g_conf >= 0) {                                   // EU 0x2A1640 FUN_002a1640: "Is this okay?"
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
    // PRUEBA (auto): con 2+ aliados, si el cursor esta en una fila ya marcada baja a la primera sin marcar; si no, la A
    // automatica no marcaba la segunda y las fases IV, V y IX se quedaban en la eleccion (log "quedan 1" y 5 min parado).
    if (auto_on && quedan > 0) {
      auto marcada_en = [&](int idx) { return std::find(g_marcadas.begin(), g_marcadas.end(), idx) != g_marcadas.end(); };
      if (marcada_en(libres[size_t(g_comp)])) {
        for (int k = 1; k < n; ++k) {
          const int j = (g_comp + k) % n;
          if (!marcada_en(libres[size_t(j)])) { g_comp = j; break; }
        }
      }
    }
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
  if (g_estado == kJuego && (HilosOcupados() || EscenaAbierta() || g_alf.i >= 0)) return 0;   // el guion o la alfombra mandan
  if (g_estado == kJuego && g_fase_rival && g_pend_j < 0 && g_menu == 0 && !g_vuelta && !HilosOcupados() &&
      g_vuelta_pend < 0 && g_2contacto < 0) {
    FaseRival();
    if (g_fase_rival && g_pend_j < 0) return 0;
  }
  if (g_estado == kGanada && g_fase_sig >= 0 && !std::getenv("DBZ3_TEST_B2_ROTULO")) {
    // S1-F01.1: orden 41 como el original (FUN_00294660, b2.c): sin rotulo; guardada la fase (orden 41, ya hecho), la Bola
    // se cierra hasta el negro (FUN_0024A120(0xB, 0x2382C0): 100 cuadros) y se pasa a la fase siguiente, que entra con el
    // iris que abre (FUN_0024A080(0xB, 0, 0) al empezar la fase).
    // DS23: la musica se para al cerrar la fase (FUN_00246190(.., -2) pone el destino del fundido a -2; aqui el motor
    // de audio del port no tiene fundido, se corta al entrar en kGanada; [VERIFICAR] en pantalla el fundido de ~1 s).
    // El visor de sucesos de antes del cierre (FUN_00270A50(0x1500) -> FUN_0026E020(4, ...)) NO usa DATA_<idioma> 289:
    // 289 es el #AMT de mensajes de memory card (53 textos medidos: "Current setting will be lost", "No memory card"...).
    // Su texto lo sirve la VM de mensajes con los ids que deja el guion en 0x69b710+0x54; queda [VERIFICAR] (no hay
    // tabla fija que copiar).
    static int fase_musica = -1;
    if (fase_musica != g_d.fase) {
      fase_musica = g_d.fase;
      CallarMusicaPs2();
    }
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
        GuardarProgreso("tablero_b2_fase", 0);  GuardarBanderas(true);  BorrarEstadoFase();
      }
      const int f = gana ? (g_fase_sig >= 0 ? g_fase_sig : 0) : g_d.fase;
      if (!gana) REXLOG_INFO("dbz3 tablero b2: reintento de la fase {} (A en GAME OVER)", f + 1);
      if (Cargar(f)) Preparar();
    }
    return 0;
  }
  if (g_estado == kDeseo) {   // Shenron (dw_shenglong.c): su ventana (CMN 850 en el sitio del #AMA 849) con sus frases
    // (537) y la lista de deseos en las 3 filas del #AMA (284, 320, 356), delante de Shenron en 3D (modelo de B2, CMN 327).
    // DS23: los #SPX 329/331/332 NO son efectos cargables de esta copia. Se leyeron sus cadenas: son scripts de escena
    // que apuntan a "/CDROM/JP/SYSTEM/SCR/SH/SHB01*.adx", ".../B_01.AMB", ".../BSE01.*", y esas rutas no existen en la
    // ISO EU (solo trae los 7 AFS, el .SFD y los .IRX); en b2.c no se localizaron referencias directas a las entradas
    // 329/331/332 (0x149/0x14B/0x14C). La escena real de B2 es 327/328, que ya se carga aqui (sitio hijo 0, Bolas
    // 7/8/9, animacion 3).
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
    // salta al plano final (FUN_0029B020: cuenta < 1410 y boton). DS23: la luz/brillo que el comentario viejo atribuia al
    // emisor 0x100 del #SPX 329 no se puede cargar (329 es un script con rutas /CDROM ausentes y sin referencia en b2.c,
    // ver arriba); el brillo 2D es lo unico que hay en esta copia. La camara son 3 planos medidos, no la timeline.
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
        } else {   // Sin equivalente visual en B3 (lista arriba): el port conserva el acceso visible al control.
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
      GuardarProgreso("tablero_b2_fase", 0);  GuardarBanderas(true);  BorrarEstadoFase();
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
    // EU 0x276E40 FUN_00276e40 (ataque de la pieza: u16 en +0x10 del registro 0x69B7A0 + 0x24 n)
    out[6] = j.ataque;
    // EU 0x276E60 FUN_00276e60 (defensa de la pieza: u16 en +0x12)
    out[7] = j.defensa;
    // studies 2 G04: contrato ampliado a 14 enteros (0-7 conservan su significado). El consumidor debe reservar
    // int[14] (pasarela_misiones.cpp). DS23: la forma sale ya traducida con FormaTableroB3; el traje de las
    // absorciones de Bu (c47/48/50/51) sale con TrajeAbsorcionB2 (indice de traje de B3); el resto sigue siendo el
    // indice crudo de B2 de la orden 52.
    const int tj_abs = TrajeAbsorcionB2(j.c), tr_abs = TrajeAbsorcionB2(r.c);
    out[8] = j.forma_b2;    // orden 52: +0x19 (byte)
    out[9] = tj_abs >= 0 ? tj_abs : j.traje_b2;    // orden 52: +0x1A (word; absorciones -> traje de B3)
    out[10] = r.forma_b2;
    out[11] = tr_abs >= 0 ? tr_abs : r.traje_b2;
    if (tj_abs >= 0 || tr_abs >= 0)
      REXLOG_INFO("dbz3 tablero b2: absorcion de Bu: {} traje B3 {}, {} traje B3 {} (forma 0, id 35)", Nombre(j), tj_abs,
                  Nombre(r), tr_abs);
    if ((j.c == 50 || j.c == 51 || r.c == 50 || r.c == 51) && tj_abs < 0 && tr_abs < 0)
      REXLOG_WARN("dbz3 tablero b2: Bu con Cell/Freezer (c50/c51) sin traje en B3: id 35 forma 0 (Bu de siempre)");
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
    // forma con la que entra cada uno (la del tablero; el consumidor la pide con FormaTableroB3, pasarela_misiones.cpp:662)
    REXLOG_INFO("dbz3 tablero b2: combate: formas de B3 {}={} {}={} (B2 {}/{})", Nombre(j),
                FormaTableroB3Impl(out[0], out[8]), Nombre(r), FormaTableroB3Impl(out[1], out[10]), j.c, r.c);
    return 2;
  }
  if (g_pend_j >= 0) return 0;
  if (g_menu > 0) return 0;   // el menu ya se atendio arriba (MenuTablero)
  // botones de B2 (ELF, control del tablero): cruz mueve, cuadrado busca, circulo (B) pasa, triangulo o Start (Y) abren el
  // menu, L1 esconde o saca el marcador de abajo. RB cambia de ficha (propio).
  AyudaMarquesina(dl, a, t);
  // EU 0x237990 FUN_00237990: el contador +0x30 cuenta 0..0x18 sin mandos (FUN_00276FF0 sigue a la pieza) y en 0x19 saca el cursor; los
  // mandos se leen desde el cuadro 27 (0x1A en adelante): 26 cuadros de 60 Hz (antes 0,6 s a ojo). Botones del original (mascara
  // de PS2: 0x4000 cruz = elegir, 0x0008 Start = menu, 0x0400 L1 = marcador/radar, 0x0800 R1 = "Set up Skills" (FUN_0028EBC0),
  // 0x0200 R2 = "Display Status" (FUN_00237790)). El port usa mascaras propias (kLB/kRB...), no las de PS2; R1 y R2 aun sin
  // portar [H: panel de habilidades 0x2976E0 / 0x296DA0 / 0x28BEF0-0x28E910].
  // EU 0x26FDC0 FUN_0026fdc0 (estado del rombo: 2 = entrada acabada, 0 = oculto)
  // EU 0x237F70 FUN_00237f70 caso 3: espera a que el rombo (FUN_0026FDC0 == 2) acabe su entrada (anim 38) y solo entonces cede a 0x237990
  // (los 26 cuadros de arriba): sin mandos durante duracion(38) + 26 cuadros (antes solo 26)
  if (t - g_turno_t < ((g_d.hud_ama.size() > 38 ? std::max(1.0f, g_d.hud_ama[38].dur) : 10.0f) + 26.0f) / 60.0f) return 0;   // asi no se encadenan turnos con B
  if (pressed & (kY | kStart)) {
    g_menu = 1;
    g_menu_sel = 0;
    g_menu_t = t;
    REXLOG_INFO("dbz3 tablero b2: menu abierto");
    return 0;
  }
  Ficha& f = g_f[size_t(g_sel)];
  // EU 0x2700E0 FUN_002700e0 (L1: si la banda esta puesta (estado 2) la esconde con FUN_0026FF10, si no la saca con FUN_0026FFF0)
  // EU 0x26FF10 FUN_0026ff10 (esconder: el radar (iGpffff8cd4) pasa al estado 4 con la anim 0x12 (18, salida) y la barra (iGpffff8cd8) al 3 con la anim 2)
  // EU 0x26FFF0 FUN_0026fff0 (sacar: el radar al estado 2 con la anim 0x10 (16, entrada, solo si ya tiene el Radar) y la barra al 1 con la anim 0)
  if ((pressed & kLB) && g_radar) {   // FUN_0026FFF0 solo saca la banda si ya tiene el Radar del Dragon; sin el, L1 no hace nada
    SonidoEventoB2("marcador", 3);   // FUN_00236EA0 (b2.c:189786-189789): L1 esconde o saca el marcador
    g_hud_oculto = !g_hud_oculto;
    g_hud_t = g_radar_t = t;   // sale o entra con su animacion
    GuardarProgreso("tablero_b2_marcador", g_hud_oculto ? 0 : 1);   // bit 0x08 de 0x6992DC, va con la partida
  }
  if (g_vista) {   // vista de estado: mientras dura no se elige nada
    // EU 0x237730 FUN_00237730 (salida de la vista: 0x200 de PS2 = RT en el port; devuelve el mando al tablero)
    if (pressed & kRT) {
      g_vista = false;
      SonidoCanal(0, 0x16);   // FUN_00237730 -> FUN_00147260(0,0,0x16,0): salida de Display Status
    }
    return 0;
  }
  // EU 0x237790 FUN_00237790 (entrada en la vista de estado: R2 0x0200 de PS2 = RT; los mandos mueven la camara)
  if (pressed & kRT) {   // R2 "Display Status" (EU 0x237990: 0x0200)
    g_vista = true;
     SonidoCanal(0, 0x15);   // FUN_00237790 -> FUN_00147260(0,0,0x15,0): entrada a Display Status
    return 0;
  }
  // R1 "Set up Skills" ( EU 0x237990: 0x0800 -> FUN_0028EBC0 + FUN_0028E910 + FUN_0028E830, tarea 0x236D60): el panel de B2 reparte 7
  // puntos (7 - (byte +8 & 7) de cada capsula, tabla #SKA 0x6942A8; FUN_0028E4B0) entre hasta 7 capsulas por personaje. En el port
  // los combates son de B3 y las capsulas, las de B3, asi que el panel que tiene efecto es el Edit Skills NATIVO de B3 (el mismo que
  // abre la nave): se reutiliza en vez de duplicar la lista de capsulas de B2 [H: la disposicion del panel de B2 no se copia].
  // (ADICION PROPIA retirada por decision del coordinador: RB era "siguiente pieza"; el codigo queda debajo por si el usuario lo quiere.)
  // if (pressed & kRB) SiguienteSel();
  if ((pressed & kRB) && g_dest <= 0) {
    if (dbz3::hub::RequestEditSkills()) REXLOG_INFO("dbz3 tablero b2: R1 Set up Skills: Edit Skills nativo pedido");
    return 0;
  }
  // A2: el original tiene DOS pasos (FUN_002798A0 y FUN_002793E0, medido en PCSX2): "Select a Play Character" (las flechas pasan a
  // la ficha del jugador mas cercana en esa direccion; X la elige) y "Select the Map Point Where You are Going" (el destino arranca
  // en la primera vecina de la casilla, las flechas cambian a otra vecina, X mueve, Circulo/Triangulo vuelve al paso 1).
  // g_dest > 0 = paso 2.
  const bool paso2 = g_dest > 0;
  // EU 0x236EA0 FUN_00236ea0 (la maquina de los dos pasos; el codigo que la ejecuta empieza aqui)
  // EU 0x2798A0 FUN_002798a0 / EU 0x2793E0 FUN_002793e0: elegir ficha o casilla por la direccion pulsada (mas cercana en
  // el eje; sin ninguna, la mas lejana) y no ofrecer casillas llenas
  if (pressed & (kUp | kDown | kLeft | kRight)) {
    const int h = (pressed & kRight) ? 1 : (pressed & kLeft) ? -1 : 0;
    const int v = (pressed & kDown) ? 1 : (pressed & kUp) ? -1 : 0;
    // A2b: la regla exacta de FUN_002798A0 / FUN_002793E0 (b2.c:223522, 223682): el candidato mas cercano EN EL EJE pulsado
    // (dx2 / dy2, o (dx2+dy2)/2 en diagonal); sin ninguno en esa direccion, el mas lejano en ese eje (vuelta). En el paso 2 las
    // distancias se miden desde el destino ACTUAL (no desde la ficha) y los candidatos son las vecinas menos el destino actual.
    // El filtro de "+0xC == 1" del original no cambia nada: FUN_002791A0 deja +0xC a 0 al cargar y no hay ninguna escritura a
    // 1 en b2.c (R7/b2/re_ia.md §3 y §9; la unica comprobacion, FUN_00276B80, nunca descarta).
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
    // EU 0x236EA0 FUN_00236ea0: con un rival en la casilla de la pieza elegida (FUN_00278320 != 0) no hay destino;
    // confirmar ataca a ese ocupante por FUN_00236420 estado 0 (T4 + combate). Lo mismo que el piloto automatico.
    if (AtacarOcupante(g_sel)) return 0;
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
// la lista sale de [indices_menu] del toml. Reparto del port, no correspondencia afirmada del original: menu del tablero
// (Salir/Reiniciar y su Si/No) = 13, eleccion de aliados = 15, pantalla de los deseos = 17. [VERIFICAR] auditiva por
// pantalla; b2.c confirma las pistas 13/15/17 en FUN_0023f4b0/002422a0/0025db40, no el mapeo de este port.
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

// CM 24/10 ("se solapa una musica con la otra"): el menu principal de B3 sigue sonando por debajo del tablero (medido: la
// mezcla del juego mantiene ~0,3 de pico en todo el tablero) y se mezcla con la ADX de B2. Mientras el tablero esta en
// pantalla se calla la mezcla del juego (audio_gain 0, como video_sfd.cpp); la musica y los efectos del tablero salen por
// winmm y no se ven afectados. Al salir o ir a un combate se devuelve el volumen general.
static bool g_mezcla_callada = false;
static void MezclaB3(bool callada) {
  bool& puesta = g_mezcla_callada;
  if (callada == puesta) return;
  puesta = callada;
  rex::cvar::SetFlagByName("audio_gain", std::to_string(callada ? 0.0 : std::clamp(dbz3::settings::MasterVolume(), 0.0, 1.0)));
  REXLOG_INFO("dbz3 tablero b2: mezcla de B3 {} (audio_gain)", callada ? "callada" : "devuelta");
}

// Botones mantenidos (mascara de la entrada del hub, con RT = 0x0400): el hub la llama cada cuadro antes de Dibujar.
void Mantenidos(uint16_t mantenidos) {
  g_mantenidos = mantenidos;
  g_mantenidos_ok = true;
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
  if (en_video) g_mezcla_callada = false;   // el video calla y devuelve la mezcla por su cuenta: al acabar se vuelve a callar
  else MezclaB3(r == 0);
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
    // EU 0x246190 FUN_00246190 (pone la pista de 0x6992DB; aqui, OirMusicaPs2 del índice de PistaB2)
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
