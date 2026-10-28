// dbz3 - Animaciones de Infinite World en el motor de B3 (frente R10). Ver anim_iw.h.
//
// RE (B3 360, codigo generado): sub_82114FB8(X) -> sub_820FA378(A = X+0x650) -> sub_820F95F0: el codigo A+0x520
// (X+2928) se busca en los 3 #CSK de A+0x2C0 (+0x10 n codigos, +0x14 lista ya reubicada; lista[codigo] -> bloque de
// 48 B [anim s16][almacen s16] + 11 u32); A+0x604 = (almacen+1)<<16 | anim; el #ACM del almacen =
// [[A+0x538] + 0x46C + 4*almacen], reubicado en memoria (magia "!ACM", punteros absolutos; +0x10 n, +0x14 tabla de
// 16 B [flags, variante, cuadros, bloque], bloque = 46 x [pista angular, posicional]). Cada cuadro (0x820FA558)
// A+0x608 (cuadro, float) avanza; al pasar de A+0x60C (cuadros) vuelve a 0, A+0x614 = 1 y A+0x618 cuenta las vueltas.
// IW tiene la misma cadena con +0x10 menos (AnimationsStatic/Animations, decomp EU 0x207CE0/0x207E90).
#include "anim_iw.h"

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "generated/dbz3_init.h"
#include "iw_minijuegos.h"
#include "pasarela.h"
#include "pasarela_iw.h"
#include "reloj_prueba.h"
#include "roster_ext.h"

namespace dbz3::anim {
namespace {

namespace P = dbz3::pasarela;

constexpr uint32_t kAnimObj = 0x650, kBsk = 0x2C0, kModelo = 0x538, kAlmacenes = 0x46C, kVueltas = 0x618;
// DS100: segundo cuerpo del luchador (cuerpo B). sub_82109D90 recorre X+1616 y X+1616+0x680; cada cuerpo tiene la
// misma estructura (modelo en +0x538, #ACM en modelo+0x46C, #CSK en +0x2C0 y codigo de animacion en +0x520).
constexpr uint32_t kAnimObj2 = kAnimObj + 0x680, kCodigoCuerpo = 0x520;
constexpr uint32_t kCodigo = 2928, kAccion = 3244;
constexpr uint32_t kAcm = 0x0041434D;            // "#ACM" ("!ACM" ya reubicado): sin el primer byte
constexpr size_t kIwFid = 204;                    // DATA_CMN de IW: #AMB [AMM global, AMM, AMC, BSK comun]
constexpr size_t kIwPiccolo = 925;
constexpr size_t kIwVuelo = 915;                  // DATA_CMN de IW: modelo del visor de vuelo, poses 0x320-0x330                // DATA_CMN de IW: #AMB [BSK, AMM propio (XPIC), AMM, AMM]
constexpr uint32_t kPrimero = 0x19, kN = 6;       // codigos 0x19..0x1E
constexpr uint32_t kMgrPtr = 0x823D6CF4, kX0 = 2096, kXPaso = 8240, kEstado = 35128;   // gestor, jugador 1, estado (4 = pelea)

struct Anim {
  uint32_t codigo = 0, flags = 0, var = 0, cuadros = 0;
  std::vector<std::string> sufijos;               // huesos de su #AMM sin el prefijo (GOK_WAIST -> WAIST)
  uint32_t bloque[11] = {};                       // los 11 u32 del sub-bloque del #BSK de IW (sin lineas AP)
  std::vector<std::array<uint32_t, 2>> pistas;    // pistas ya copiadas al juego (direccion; 0 = sin pista)
  // 06: eventos por cuadro de la animacion (las "lineas AP" del bloque: bloque[9] = n lineas,
  // bloque[10] = offset en el #BSK). Cada linea: [u16 gestor][u16 n][u32 offset de registros];
  // cada registro (16 B): [u16 cuadro][u16 interno][u32 tipo][u32 valor]. Solo valen las lineas
  // de gestor 7 = Anim_ProcessEventTrack (iw_sles55347.c:253899-253998; tipo -> consumidor en
  // Animations2 214601-214760). El id de gestor sale de la tabla de B2, mismo motor: b2.c
  // FUN_001fd0d0 (switch de los 8 gestores) con los cuerpos b2.c:157008-157350, uno a uno con
  // los de IW (0 cabecera, 1 hitboxes, 2 flag 0x40, 3 valor 1B8, 4 valor 1B4, 5 flags set/clear,
  // 6 manos, 7 eventos). Los tipos: 0 efecto/ki (PlayerVsCpuGeneralScene3), 1 y 2 sonido de
  // golpe (canal SE 1 y canal del personaje), 3 voz (GenericCharacterVoiceGruntingMidBattle),
  // 4 efecto Super/Ult, 5 sin consumidor, 6 parametro de escena, 7 audio de ki, 8 boca, 9 valor.
  struct Evento {
    float cuadro;
    uint32_t tipo, valor;
  };
  std::vector<Evento> eventos;
};

// leido una vez (hilo del juego)
bool g_leida = false, g_ok = false;
constexpr uint32_t kCorredor = 0xE2, kNA = kN + 1;   // + el corredor (ultima)
Anim g_an[kNA];
// R16: entrada, comienzo y final de los minijuegos de campo de IW (MgPly_Enter_State0A 0x11D; s_SPACE y estados
// 0x2F/0x30 por variante). 0x11D sale del #BSK comun (204 hijo 3 -> almacen global); los demas, del #BSK del
// jugador de los minijuegos (DATA_CMN 922: Goku; 914: Gohan nino, variante 0xE), almacen propio (hijo 1).
struct Extra {
  uint32_t codigo;
  size_t entrada;     // DATA_CMN de IW
  uint32_t como = 0;  // codigo con el que va al juego (0: el mismo); 0x800+ no pisa ningun codigo de B3
  bool girar = false;  // la raiz girada como el scatto (animaciones que van hacia +X)
};
constexpr Extra kExtras[] = {{0x11D, 204}, {0x51, 922},  {0x53, 922},  {0x118, 922}, {0x11A, 922},
                             {0x17C, 922}, {0x17E, 922}, {0x49C, 922}, {0x49E, 922}, {0x62C, 914},
                             {0x62E, 914}, {0x6F4, 922}, {0x6F6, 922},
                             {0x52, 922},  {0x119, 922}, {0x17D, 922}, {0x49D, 922}, {0x62D, 914}, {0x6F5, 922},   // superado
                             // DS169: pose del estado 0x2F de los minijuegos de campo (EU 0x29B120: +0xB60 = 0xF0). En el
                             // #BSK de Goku de IW (DATA_CMN 535) el 0xF0 va al almacen global (204/0) anim 0: 61 cuadros,
                             // de pie. El 922 (acciones del minijuego) no trae el 0xF0. Alias 0x8F0 (no pisa codigos de B3).
                             {0xF0, 535, 0x8F0},
                             {0x00, 204, 0x800},      // el 0x00 comun (una guardia; 0 es la guardia de B3)
                             {0x02, 204, kReposo}, {0x07, 204, 0x807}, {0x08, 204, 0x808},   // reposo (0x02 = anim 1) y carreras
                             {0x05, 204, 0x805}, {0x06, 204, 0x806},                        // cansado de IW
                             {0x15, 204, 0x815}, {0x16, 204, 0x816},                        // candidatas (auditoria7)
                             {0x11, 204, kParar},   // DS89: anim 16 (frenada del estado 4) en el #BSK comun
                             // DS89: salto de la maquina de estados de tierra (State0C-0x12). En 204 los mismos cuadros del
                             // almacen global van con los codigos 0x103/0x104/0x105/0x106/0x107/0x108/0x109 (ver anim_iw.h).
                             {0x2A, 204, kRebote}, {0x2B, 204, kRebote2}, {0x3D, 204, kDashAereoIni}, {0x3E, 204, kDashAereo},
                             // DS91: pared (estados 0x1F/0x20; ver anim_iw.h). Son los codigos reales del #BSK comun.
                             {0x3F, 204, kParedKick}, {0x40, 204, kParedRebote}, {0x41, 204, kParedReboteAire},
                             {0x103, 204, kSaltoPrep}, {0x104, 204, kSaltoSube}, {0x105, 204, kAterrizar},
                             {0x106, 204, kSaltoVuelo}, {0x107, 204, kSaltoCae}, {0x108, 204, kSaltoAire},
                             {0x109, 204, kDespegueVuelo},
                             {0x29, 204, kSubir},   // salto del estado 0x13 (segundo salto)
                             {0x320, 915}, {0x321, 915}, {0x322, 915}, {0x32D, 915}, {0x323, 915}, {0x324, 915}, {0x328, 915}, {0x329, 915},
                             {0x32A, 915}, {0x32C, 915}, {0x32E, 915},   // vuelo: poses del visor de IW (915)
                             // DS116: animaciones de actor de la intro del vuelo (DS113 las dejo en [H]). Cutscene_GotoLine
                             // (EU 0x3424F0) pide 880/885/870/881/882 = 0x370/0x375/0x366/0x371/0x372. Estan en el #BSK
                             // (hijo 0) de DATA_CMN 931 (Vegeta: 0x370 = anim 57) y 924 (Krilin: 0x375 = anim 11,
                             // 0x366 = anim 5, 0x371 = anim 15, 0x372 = anim 19), con el almacen propio (hijo 1, alm = 3).
                             // Cuadros: 160 (0x370), 620 (0x375), 200 (0x366) y 240 (0x371/0x372), igual que sus clips.
                             {0x370, 931}, {0x375, 924}, {0x366, 924}, {0x371, 924}, {0x372, 924},
                             // DS122: animaciones de actor de la intro de v1 (0x7E0, carrera con Goten). Cutscene_GotoLine
                             // (EU 0x3424F0) pide 1880/1885/1886 = 0x758/0x75D/0x75E en las lineas 0x4831A0/0x4831C0
                             // (actores 2/1/2 de los clips 1400/1401-1404/1407). Estan en el #BSK (hijo 0) de DATA_CMN
                             // 923 (Goten: 0x758 = anim 0 y 0x75E = anim 1), 919 (Gohan adulto: 0x75D = anim 1) y
                             // 927 (Trunks nino: 0x75D = anim 1), con el almacen propio (hijo 1, alm = 3). Cuadros:
                             // 180 (0x758), 300 (0x75D) y 180 (0x75E) = las duraciones de sus clips (#AMC 891/25).
                             {0x758, 923}, {0x75E, 923}, {0x75D, 919},
                             // el 0x75D de Trunks nino (927) es otro #BSK: con alias para no pisar el de Gohan (919)
                             {0x75D, 927, 0x9E0},
                             // Cell en el minijuego de Ten Shin Han (auditoria7/I: #BSK de 914, Char_EnterPoseState0-6Alt)
                             {0x640, 914, 0x960}, {0x654, 914, 0x961}, {0x655, 914, 0x962}, {0x656, 914, 0x963},
                             {0x657, 914, 0x964}, {0x658, 914, 0x965}, {0x659, 914, 0x966}, {0x668, 914, 0x967},
                             {0x669, 914, 0x968}, {0x66A, 914, 0x969}, {0x66B, 914, 0x96A}, {0x66C, 914, 0x96B},   // reposo y andar del jugador de 914 (Gohan nino)
                             // DS37 (0x7DD): el minijuego 14 lo juega Cell (SONNET1 §4: PINE 0x5C0DC0+0x10 = 33 en el
                             // indice 14; DATA_CMN 914 = #BSK de Cell con sus codigos de entrada/final 0x62C-0x62E, que
                             // ya van arriba). Faltaba la locomocion PROPIA de 914: reposo 0x00, andar 0x15, correr
                             // 0x16 y salto 0x22/0x29 (mismos codigos del #BSK comun, almacen propio de 914). Van con
                             // alias >0x800 para no pisar los del #BSK comun (0x800/0x815/0x816/0x822/0x829).
                             {0x00, 914, kCellQuieto}, {0x15, 914, kCellAndar}, {0x16, 914, kCellCorrer},
                             {0x22, 914, kCellSalto}, {0x29, 914, kCellDespegue},
                             // DS89: el salto completo de 914 (mismos codigos que pide Minigame_State0C-0x12).
                             {0x1F, 914, kCellPrep}, {0x20, 914, kCellSube}, {0x21, 914, kCellAire},
                             {0x23, 914, kCellAterrizar}, {0x24, 914, kCellDespegueVuelo}, {0x18, 914, kCellParar}};
// R14: poses de los minijuegos de botones (QTE) de IW. Cada personaje trae las suyas en su #BSK de DATA_CMN (almacen
// propio, hijo 1; 915 trae las de espera 0x57A-0x57D en su #BSK del hijo 3 y su #AMM del hijo 0). Tablas pose -> codigo
// de NB014 (Goku 922 / Gohan joven 920), NB013 (Vegeta 931 / Trunks 928) y NB006 (Gohan nino 921 / Piccolo 925):
// Char_SetPoseState / _P2 del ELF (jump tables 0x499820 / 0x499950, 0x499F30 / 0x49A5B0, 0x49AE00 / 0x49B370 USA;
// scripts en E:\DBZ3HD_trabajo\R14\poses). El alias es 0xB00 + desplazamiento del personaje + (codigo - inicio del rango).
struct QteAnim {
  uint16_t cmn, codigo;
};
constexpr QteAnim kQte[] = {
    // Goku (NB014 jugador): reposo/intro, golpes, esquivas, heridas, victoria y derrota
    {922, 0x5C8}, {922, 0x578}, {922, 0x582}, {922, 0x598}, {922, 0x597}, {922, 0x585}, {922, 0x59B}, {922, 0x59A},
    {922, 0x588}, {922, 0x59E}, {922, 0x59D}, {922, 0x58B}, {922, 0x5A1}, {922, 0x5A0}, {922, 0x5C9}, {922, 0x5BE},
    {922, 0x5C0}, {922, 0x5CA},
    // Gohan joven (NB014 rival)
    {920, 0x579}, {920, 0x583}, {920, 0x584}, {920, 0x586}, {920, 0x587}, {920, 0x589}, {920, 0x58A}, {920, 0x58C},
    {920, 0x58D}, {920, 0x596}, {920, 0x599}, {920, 0x59C}, {920, 0x59F}, {920, 0x5BF}, {920, 0x5C1}, {920, 0x5CD},
    // las de espera de los cuatro (915)
    {915, 0x57A}, {915, 0x57B}, {915, 0x57C}, {915, 0x57D},
    // Vegeta (NB013 jugador)
    {931, 0x564}, {931, 0x565}, {931, 0x566}, {931, 0x578}, {931, 0x582}, {931, 0x598}, {931, 0x597}, {931, 0x585},
    {931, 0x59B}, {931, 0x59A}, {931, 0x588}, {931, 0x59E}, {931, 0x59D}, {931, 0x58B}, {931, 0x5A1}, {931, 0x5A0},
    {931, 0x5BE}, {931, 0x5C0},
    // Trunks (NB013 rival)
    {928, 0x569}, {928, 0x579}, {928, 0x583}, {928, 0x584}, {928, 0x586}, {928, 0x587}, {928, 0x589}, {928, 0x58A},
    {928, 0x58C}, {928, 0x58D}, {928, 0x596}, {928, 0x599}, {928, 0x59C}, {928, 0x59F}, {928, 0x5BF}, {928, 0x5C1},
    // 05_iw_minijuegos_pendientes: poses del choque de NB013 (Mg_StartClashSequence pone 0x11 jugador / 0x21 rival;
    // tablas 0x49A5B0 / 0x499F30 del ELF USA: 0x11 -> 0x543, 0x21 -> 0x552)
    {931, 0x543}, {928, 0x552},
    // Gohan nino (NB006 jugador)
    {921, 0x2AD}, {921, 0x258}, {921, 0x262}, {921, 0x278}, {921, 0x277}, {921, 0x265}, {921, 0x27B}, {921, 0x27A},
    {921, 0x268}, {921, 0x27E}, {921, 0x27D}, {921, 0x26B}, {921, 0x281}, {921, 0x280}, {921, 0x285}, {921, 0x286},
    {921, 0x287}, {921, 0x289}, {921, 0x29F}, {921, 0x2A9}, {921, 0x2AA},
    // Piccolo (NB006 rival)
    {925, 0x2A8}, {925, 0x259}, {925, 0x276}, {925, 0x264}, {925, 0x263}, {925, 0x279}, {925, 0x267}, {925, 0x266},
    {925, 0x27C}, {925, 0x26A}, {925, 0x269}, {925, 0x27F}, {925, 0x26D}, {925, 0x26C}, {925, 0x28A}, {925, 0x288},
    {925, 0x29E}, {925, 0x2A0}, {925, 0x2A9}, {925, 0x2AA},
    // Familia B (MgPly / MgPlyB, ELF 0x308C00-0x311DF0): NB012 Vegeta (estados 0-8: 0x500 0x501 0x502 0x4B0 0x4BA 0x4B1
    // 0x4B2 0x4B3 0x4C4), NB007 Goku (922: 0x30C-0x30E; las de pose compartidas, 915: 0x2BC-0x2BF 0x2C6 0x2D0) y MSN019 Videl (930)
    {931, 0x4B0}, {931, 0x4BA}, {931, 0x4B1}, {931, 0x4B2}, {931, 0x4B3}, {931, 0x4C4}, {931, 0x500}, {931, 0x501},
    {931, 0x502},
    {922, 0x30C}, {922, 0x30D}, {922, 0x30E},
    {915, 0x2BC}, {915, 0x2BD}, {915, 0x2BE}, {915, 0x2BF}, {915, 0x2C6}, {915, 0x2D0},
    {930, 0x76C}, {930, 0x76D}, {930, 0x76E}, {930, 0x76F}, {930, 0x770}, {930, 0x776}, {930, 0x777}, {930, 0x778},
    {930, 0x780}, {930, 0x7BC}, {930, 0x7BD}, {930, 0x7BE}};
constexpr uint32_t kQteBase = 0xB00;
uint32_t AliasQte(uint32_t cmn, uint32_t codigo) {
  // 141 codigos (0x541-0x5CD) por personaje de NB013/NB014, 88 (0x258-0x2AF) por los de NB006, 4 de 915 (0x57A-0x57D)
  struct Rango {
    uint32_t cmn, desde, ancho, desplaza;
  };
  static constexpr Rango r[] = {{922, 0x541, 141, 0},   {920, 0x541, 141, 141}, {931, 0x541, 141, 282},
                                {928, 0x541, 141, 423}, {921, 0x258, 88, 564},  {925, 0x258, 88, 652},
                                {915, 0x57A, 4, 740},
                                // familia B
                                {931, 0x4B0, 21, 744}, {931, 0x500, 3, 765}, {922, 0x30C, 3, 768}, {915, 0x2BC, 4, 771},
                                {915, 0x2C6, 1, 775},  {915, 0x2D0, 1, 776}, {930, 0x76C, 5, 777}, {930, 0x776, 3, 782},
                                {930, 0x780, 1, 785},  {930, 0x7BC, 3, 786}};
  for (const Rango& x : r)
    if (x.cmn == cmn && codigo >= x.desde && codigo - x.desde < x.ancho) return kQteBase + x.desplaza + (codigo - x.desde);
  return 0;
}
std::vector<Anim> g_ex;   // las que se pudieron leer
uint32_t Total() { return kNA + uint32_t(g_ex.size()); }
const Anim& AnimN(uint32_t c) { return c < kNA ? g_an[c] : g_ex[c - kNA]; }
std::atomic<int> g_pose{0};   // Reproducir: 0 nada, 1 en curso, 2 acabada (o no se pudo)
std::atomic<int> g_vista{kApagado};   // fase del scatto que se ve
// estado (hilo del juego)
// DS100: B3 lleva DOS cuerpos por luchador (A = X+0x650, B = X+0xCD0; sub_82109D90 recorre los dos y el que se
// dibuja depende de la forma: con la forma 2 el cuerpo B es el visible). Hasta ahora las animaciones de IW se
// instalaban solo en el cuerpo A, asi que el cuerpo B seguia con su guardia/accion 0 de B3 (pose de combate
// quieta aunque X+2928 tuviera el codigo de IW). La tabla y el #CSK se cachean por #ACM porque cada cuerpo puede
// usar un almacen distinto. g_cache[0] es el cuerpo A (el de CrearTransicion/PrepararTransicion).
struct CacheCuerpo {
  uint32_t acm = 0, tabla = 0, base_idx = 0;
};
CacheCuerpo g_cache[2];
// DS103: el #CSK es POR CUERPO (no por #ACM). Dos cuerpos pueden compartir el mismo #ACM y tener cada uno su
// propia lista de 3 #CSK con codigos distintos. Antes se cacheaba por #ACM, asi que el segundo cuerpo reutilizaba
// el #CSK del primero o, si no habia hueco libre, se quedaba sin el (codigo 0 = guardia de B3: la captura).
// Ahora: se funde el #CSK del cuerpo con el nuestro en el HUECO 0 (primero que mira sub_820F95F0), copiando sus
// codigos originales; asi our alias 0x8xx ganan aunque el #CSK de B3 tenga una entrada propia ahi y el cuerpo no
// dependa de que quede un hueco libre. `prev` = puntero que habia en el hueco, para devolverlo en Quitar.
struct CacheCsk {
  uint32_t A = 0, acm = 0, csk = 0, prev = 0, hueco = 0;
};
CacheCsk g_csk[2];
uint32_t g_acm_principal = 0;   // #ACM del cuerpo A (donde vive la transicion 0x7FE generada)
uint32_t g_csk_principal = 0;   // #CSK del cuerpo A (el de la transicion 0x7FE)

uint32_t Le32(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4);
  return v;
}
uint16_t Le16(const std::vector<uint8_t>& b, size_t o) {
  uint16_t v = 0;
  if (o + 2 <= b.size()) std::memcpy(&v, b.data() + o, 2);
  return v;
}
std::string Sufijo(const char* s, size_t n) {
  std::string t(s, strnlen(s, n));
  const size_t p = t.find('_');
  return p == std::string::npos ? t : t.substr(p + 1);
}
// Valores del #ACM cargado: reubicados (direccion) o relativos al #ACM.
uint32_t Abs(uint32_t v, uint32_t acm) { return v && v < 0x01000000u ? acm + v : v; }

// Las animaciones del scatto de IW corren hacia el +X del modelo (la columna de la cadera se inclina a rumbo 86) y el
// luchador de B3 mira a +Z (la guardia: eje Z de la cadera a -47, X a +43): sin girarlas, Goku corre de lado. Se gira
// la raiz (GOK_WAIST) -90 grados alrededor del eje vertical, en proporcion a lo inclinada que esta (0 en la postura
// de guardia del principio del arranque, 1 en el bucle), para no dar un salto al empezar ni al acabar. Euler u16 de la
// pista: q = qz * qy * qx (awo_tools/altura.py).
void GirarRaiz(uint8_t* base, uint32_t ang, uint32_t pos) {
  constexpr double kPi = 3.14159265358979323846, kU = kPi / 32768.0;
  double suma = 0;
  const uint32_t nk = ang ? REX_LOAD_U32(ang + 8) : 0;
  for (uint32_t k = 0; k < nk; ++k) {
    const uint32_t e = ang + 12 + 8 * k;
    const double ax = REX_LOAD_U16(e + 2) * kU, ay = REX_LOAD_U16(e + 4) * kU, az = REX_LOAD_U16(e + 6) * kU;
    // matriz R = Rz Ry Rx
    const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az),
                 sz = std::sin(az);
    double R[3][3] = {{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
                      {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
                      {-sy, cy * sx, cy * cx}};
    const double inclinado = std::acos(std::fmin(1.0, std::fmax(-1.0, R[1][1])));   // columna (+Y) frente a la vertical
    const double w = std::fmin(1.0, std::fmax(0.0, (inclinado - 8 * kPi / 180) / (33 * kPi / 180)));
    suma += w;
    const double t = -kPi / 2 * w, ct = std::cos(t), st = std::sin(t);
    double N[3][3];   // N = Ry(t) R
    for (int c = 0; c < 3; ++c) {
      N[0][c] = ct * R[0][c] + st * R[2][c];
      N[1][c] = R[1][c];
      N[2][c] = -st * R[0][c] + ct * R[2][c];
    }
    const double ny = std::asin(std::fmin(1.0, std::fmax(-1.0, -N[2][0]))), nx = std::atan2(N[2][1], N[2][2]),
                 nz = std::atan2(N[1][0], N[0][0]);
    auto a16 = [&](double a) { return uint16_t(int64_t(std::llround(a / kU)) & 0xFFFF); };
    REX_STORE_U16(e + 2, a16(nx));
    REX_STORE_U16(e + 4, a16(ny));
    REX_STORE_U16(e + 6, a16(nz));
  }
  const uint32_t np = pos ? REX_LOAD_U32(pos + 8) : 0;
  const double t = nk ? -kPi / 2 * suma / nk : 0, ct = std::cos(t), st = std::sin(t);
  for (uint32_t k = 0; k < np; ++k) {   // la posicion de la cadera, con el giro medio de la animacion
    const uint32_t e = pos + 12 + 16 * k, ux = REX_LOAD_U32(e + 4), uz = REX_LOAD_U32(e + 12);
    float x, z;
    std::memcpy(&x, &ux, 4);
    std::memcpy(&z, &uz, 4);
    const float x2 = float(ct * x + st * z), z2 = float(-st * x + ct * z);
    uint32_t vx, vz;
    std::memcpy(&vx, &x2, 4);
    std::memcpy(&vz, &z2, 4);
    REX_STORE_U32(e + 4, vx);
    REX_STORE_U32(e + 12, vz);
  }
}

// R14: gira la raiz (GOK_WAIST) `t` radianes alrededor del eje vertical en TODOS los cuadros (el lado "P2" de las poses
// de los minijuegos de botones: IW los pone mirandose con 180 grados de diferencia y B3 hace lo mismo en el motor; las
// poses del jugador (P2) vienen escritas a 180 grados de las del rival: medido, 57A -> 57B = 179,8 grados sobre Y).
// Igual que GirarRaiz pero sin ponderar por la inclinacion. Euler u16 de la pista: q = qz * qy * qx.
void GirarYaw(uint8_t* base, uint32_t ang, uint32_t pos, double t) {
  constexpr double kPi = 3.14159265358979323846, kU = kPi / 32768.0;
  const double ct = std::cos(t), st = std::sin(t);
  const uint32_t nk = ang ? REX_LOAD_U32(ang + 8) : 0;
  for (uint32_t k = 0; k < nk; ++k) {
    const uint32_t e = ang + 12 + 8 * k;
    const double ax = REX_LOAD_U16(e + 2) * kU, ay = REX_LOAD_U16(e + 4) * kU, az = REX_LOAD_U16(e + 6) * kU;
    const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az),
                 sz = std::sin(az);
    double R[3][3] = {{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
                      {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
                      {-sy, cy * sx, cy * cx}};
    double N[3][3];   // N = Ry(t) R
    for (int c = 0; c < 3; ++c) {
      N[0][c] = ct * R[0][c] + st * R[2][c];
      N[1][c] = R[1][c];
      N[2][c] = -st * R[0][c] + ct * R[2][c];
    }
    const double ny = std::asin(std::fmin(1.0, std::fmax(-1.0, -N[2][0]))), nx = std::atan2(N[2][1], N[2][2]),
                 nz = std::atan2(N[1][0], N[0][0]);
    auto a16 = [&](double a) { return uint16_t(int64_t(std::llround(a / kU)) & 0xFFFF); };
    REX_STORE_U16(e + 2, a16(nx));
    REX_STORE_U16(e + 4, a16(ny));
    REX_STORE_U16(e + 6, a16(nz));
  }
  const uint32_t np = pos ? REX_LOAD_U32(pos + 8) : 0;
  for (uint32_t k = 0; k < np; ++k) {
    const uint32_t e = pos + 12 + 16 * k, ux = REX_LOAD_U32(e + 4), uz = REX_LOAD_U32(e + 12);
    float x, z;
    std::memcpy(&x, &ux, 4);
    std::memcpy(&z, &uz, 4);
    const float x2 = float(ct * x + st * z), z2 = float(-st * x + ct * z);
    uint32_t vx, vz;
    std::memcpy(&vx, &x2, 4);
    std::memcpy(&vz, &z2, 4);
    REX_STORE_U32(e + 4, vx);
    REX_STORE_U32(e + 12, vz);
  }
}

// iw_fix: la entrada y el final de cada minijuego de campo (0x17C-0x17E de Snake Way: z = 3522 y -3289; 0x49C-0x49E: x =
// -847...) vienen escritos con la posicion ABSOLUTA del escenario de IW en la cadera: en B3 el luchador acababa a miles de
// unidades y desaparecia (el final de Snake Way se veia sin Goku). Se resta a la pista de posicion de la cadera su primer
// cuadro en x y z (y se conserva la altura y el movimiento relativo).
void SinOrigenXZ(uint8_t* base, const Anim& an) {
  for (size_t w = 0; w < an.sufijos.size(); ++w) {
    const uint32_t pos = an.sufijos[w] == "WAIST" ? an.pistas[w][1] : 0;
    const uint32_t nk = pos ? REX_LOAD_U32(pos + 8) : 0;
    if (nk < 1) continue;
    const uint32_t u0x = REX_LOAD_U32(pos + 12 + 4), u0z = REX_LOAD_U32(pos + 12 + 12);
    float x0, z0;
    std::memcpy(&x0, &u0x, 4);
    std::memcpy(&z0, &u0z, 4);
    if (std::fabs(x0) < 200.0f && std::fabs(z0) < 200.0f) continue;   // las demas ya vienen relativas (0x51: z = -24)
    for (uint32_t k = 0; k < nk; ++k) {
      const uint32_t e = pos + 12 + 16 * k, ux = REX_LOAD_U32(e + 4), uz = REX_LOAD_U32(e + 12);
      float x, z;
      std::memcpy(&x, &ux, 4);
      std::memcpy(&z, &uz, 4);
      x -= x0, z -= z0;
      uint32_t vx, vz;
      std::memcpy(&vx, &x, 4);
      std::memcpy(&vz, &z, 4);
      REX_STORE_U32(e + 4, vx);
      REX_STORE_U32(e + 12, vz);
    }
  }
}

// Bucle cerrado (auditoria7/E): las carreras y reposos de IW no acaban donde empiezan (0x16: 1.5 u en z y 0.7 en y
// por vuelta; 0xE2: 1.65 y 4.35) y al volver al cuadro 0 la cadera salta. Se resta a la pista de posicion de la
// cadera la rampa lineal de su deriva, de modo que el ultimo cuadro coincide con el primero.
void CerrarRaiz(uint8_t* base, const Anim& an) {
  for (size_t w = 0; w < an.sufijos.size(); ++w) {
    const uint32_t pos = an.sufijos[w] == "WAIST" ? an.pistas[w][1] : 0;
    const uint32_t nk = pos ? REX_LOAD_U32(pos + 8) : 0;
    if (nk < 2) continue;
    auto clave = [&](uint32_t k, int c) {
      const uint32_t u = REX_LOAD_U32(pos + 12 + 16 * k + 4 + 4 * uint32_t(c));
      float f;
      std::memcpy(&f, &u, 4);
      return f;
    };
    const float f0 = float(REX_LOAD_U32(pos + 12)), fn = float(REX_LOAD_U32(pos + 12 + 16 * (nk - 1)));
    if (fn <= f0) continue;
    float d[3];
    for (int c = 0; c < 3; ++c) d[c] = clave(nk - 1, c) - clave(0, c);
    for (uint32_t k = 0; k < nk; ++k) {
      const float t = (float(REX_LOAD_U32(pos + 12 + 16 * k)) - f0) / (fn - f0);
      for (int c = 0; c < 3; ++c) {
        const float v = clave(k, c) - d[c] * t;
        uint32_t u;
        std::memcpy(&u, &v, 4);
        REX_STORE_U32(pos + 12 + 16 * k + 4 + 4 * uint32_t(c), u);
      }
    }
  }
}

// Copia al juego en big-endian (como awo_tools/ps2hd.py conv_amm: cabecera 3 x u32; claves angulares 4 x u16, el resto
// u32) la animacion del codigo `codigo` de un #BSK de IW (almacen 0 = `global`, 3 = `propio`; #AMM de PS2).
bool g_callada = false;   // Leer: sin una linea de registro por animacion (las 113 de los botones)
bool Copiar(uint8_t* base, Anim& an, const std::vector<uint8_t>& bsk, uint32_t codigo, const std::vector<uint8_t>& global,
            const std::vector<uint8_t>& propio, bool girar) {
  if (bsk.size() < 0x20 || std::memcmp(bsk.data(), "#BSK", 4) != 0 || codigo >= Le32(bsk, 0x10)) return false;
  const uint32_t blk = Le32(bsk, Le32(bsk, 0x14) + 4 * codigo), k = Le16(bsk, blk), alm = Le16(bsk, blk + 2);
  const std::vector<uint8_t>& a = alm == 0 ? global : propio;
  if (!blk || (alm != 0 && alm != 3) || a.size() < 0x20 || std::memcmp(a.data(), "#AMM", 4) != 0) return false;
  const uint32_t n_anim = Le32(a, 0x10), tbl = Le32(a, 0x14), nb = Le32(a, 0x18), nombres = Le32(a, 0x1C);
  if (k >= n_anim || nb == 0 || nb > 128 || nombres + 32 * nb > a.size()) return false;
  an.codigo = codigo;
  for (uint32_t w = 0; w < 11; ++w) an.bloque[w] = Le32(bsk, blk + 4 + 4 * w);
  // 06: eventos por cuadro (lineas AP), antes de anularlas para el #CSK de B3. Formato y
  // gestores: ver `struct Anim`. Solo el gestor 7 (Anim_ProcessEventTrack).
  {
    const uint32_t n_lin = an.bloque[9], off = an.bloque[10];
    if (n_lin <= 64 && off && off + 8 * n_lin <= bsk.size())
      for (uint32_t j = 0; j < n_lin; ++j) {
        const uint32_t eo = off + 8 * j;
        if (Le16(bsk, eo) != 7) continue;
        const uint32_t n = Le16(bsk, eo + 2), regs = Le32(bsk, eo + 4);
        if (n == 0 || n > 64 || !regs || regs + 16 * n > bsk.size()) continue;
        for (uint32_t q = 0; q < n; ++q) {
          const uint32_t r = regs + 16 * q;
          an.eventos.push_back({float(Le16(bsk, r)), Le32(bsk, r + 4), Le32(bsk, r + 8)});
        }
      }
    std::sort(an.eventos.begin(), an.eventos.end(),
              [](const Anim::Evento& x, const Anim::Evento& y) { return x.cuadro < y.cuadro; });
  }
  an.bloque[9] = an.bloque[10] = 0;   // sin lineas AP (sonidos/efectos de IW)
  // DS105: los 11 u32 del sub-bloque de IW NO son el bloque de #CSK de B3. Al resolver el codigo, 0x820F95F0
  // copia sus campos 5/6/7 a A+0x624/A+0x62C/A+0x638 y 0x820FAF40 los usa como CLIPS SECUNDARIOS del modelo:
  //   A+0x62C >= 0 -> clip2 = A+0x62C | 0x80000 (bit 19) y lo aplica a sub_820B0EE8(modelo, 0, clip2) con el
  //                   reloj A+0x630, SUSTITUYENDO el clip base A+0x604 en el canal 0 del modelo;
  //   A+0x638 >= 0 -> clip3 = ((almacen+1)<<16) | A+0x638 con el reloj A+0x63C.
  // En IW esos campos son var/flags (p. ej. la carrera 0x16 trae campo6 = 0): B3 pedia el clip 0x80000 (almacen 7,
  // fila 0) y dibujaba ESA pose, no la de 0x188/0x182; como A+0x624 = -1 impide avanzar el reloj A+0x630 (0x820FA614),
  // la fila se quedaba en el cuadro 0 = la guardia estatica de la captura. Un bloque de B3 sin capas lleva -1 en los
  // dos: se anulan (el evaluador de IW, 0x82122088, solo usa los campos 9/10, que ya van a 0).
  const uint32_t extra_iw5 = an.bloque[5], extra_iw6 = an.bloque[6], extra_iw7 = an.bloque[7];
  an.bloque[6] = 0xFFFFFFFFu;   // A+0x62C: sin clip secundario
  an.bloque[7] = 0xFFFFFFFFu;   // A+0x638: sin tercer clip
  const size_t e = tbl + 16 * k;
  an.flags = Le32(a, e) & ~0x10u;
  an.var = Le32(a, e + 4);
  an.cuadros = Le32(a, e + 8);
  const uint32_t off = Le32(a, e + 12), per = (Le32(a, e) & 0x10) ? 3 : 2;
  if (off + 4 * per * nb > a.size()) return false;
  // Las #AMM de IW SI tienen la 3a pista (flag 0x10 del registro: 165 de las 472.502 animaciones del DATA_CMN), pero
  // NINGUNA de las alcanzables desde los #BSK de los minijuegos la lleva: barridas las tablas de codigos de 204, 912,
  // 915, 920, 921, 922, 925, 928 y 931 (ambos almacenes: propio, pool 3, y el global de 204/0), 0 animaciones con 0x10.
  // Por eso solo se copian 2 pistas (giro y posicion, `i < 2`); si alguna vez aparece una con escala se avisa aqui en
  // vez de quedar en silencio.
  if (per == 3) REXLOG_WARN("dbz3 anim: codigo 0x{:X} trae la pista de escala (flags 0x{:X}): no se copia", codigo,
                            Le32(a, e));
  for (uint32_t j = 0; j < nb; ++j) {
    an.sufijos.push_back(Sufijo(reinterpret_cast<const char*>(a.data() + nombres + 32 * j), 32));
    std::array<uint32_t, 2> pj{0, 0};
    for (uint32_t i = 0; i < 2; ++i) {   // 2 pistas (giro 4 x s16 / posicion 4 x s32); ver arriba la escala
      const uint32_t p = Le32(a, off + 4 * (per * j + i)), nk = Le32(a, p + 8);
      const uint32_t tam = 12 + nk * (i == 0 ? 8 : 16);
      if (!p || nk > 4096 || p + tam > a.size()) continue;
      const uint32_t g = dbz3::roster::GuestAlloc(tam);
      if (!g) return false;
      for (uint32_t q = 0; q < 12; q += 4) REX_STORE_U32(g + q, Le32(a, p + q));
      if (i == 0)
        for (uint32_t q = 12; q < tam; q += 2) REX_STORE_U16(g + q, Le16(a, p + q));
      else
        for (uint32_t q = 12; q < tam; q += 4) REX_STORE_U32(g + q, Le32(a, p + q));
      pj[i] = g;
    }
    if (girar && an.sufijos.back() == "WAIST") GirarRaiz(base, pj[0], pj[1]);
    an.pistas.push_back(pj);
  }
  if (!g_callada)
    REXLOG_INFO("dbz3 anim: codigo 0x{:X} de IW = anim {} del almacen {} ({} cuadros, {} huesos), flags 0x{:X} var {}, "
              "bloque {:X} {:X} {:X} {:X} {:X} {:X} {:X} {:X} {:X} (capas B3 de IW {:X} {:X} {:X} -> anuladas)",
              codigo, k, alm, an.cuadros, nb, an.flags, an.var, an.bloque[0], an.bloque[1], an.bloque[2],
              an.bloque[3], an.bloque[4], an.bloque[5], an.bloque[6], an.bloque[7], an.bloque[8], extra_iw5, extra_iw6,
              extra_iw7);
  return true;
}

// Mezcla entre animaciones de IW (auditoria7/N: B3 no funde por esta via; IW si, CharModel_SetAnimBlend 1/5..1/20):
// al pasar de una animacion de IW a otra se pone una de transicion de kMezcla cuadros con dos claves por hueso, la pose
// actual de la de antes y la del primer cuadro de la nueva, y despues la nueva. Las pistas de la transicion son fijas
// (las enlaza Instalar una vez) y se reescriben en cada cambio. Huesos: todos los de las animaciones leidas.
// R7 (re_anim_bordes.md): IW no funde siempre en 8 cuadros. CharModel_SetAnimBlend (iw_sles55347.c 282349) pone una tasa
// por cuadro (peso += tasa, FuncToDefine 282238) y la anim nueva AVANZA mientras sube el peso, contra la pose que tenia la
// de antes. Tasas leidas (Viewer_StateFlyUpdate0, 431615-431800): al reposo 0x800 1/10; 0x329/0x32A 1/15; 0x328 1/20;
// 0x324 1/18; desde 0x330 siempre 1/15; el resto de llamadas usa 1/15 (o 1/5). La transicion lleva una clave por cuadro.
constexpr uint32_t kTransicion = 0x7FE, kMaxMezcla = 20;
// 06: tasas de IW (peso += 1/n por cuadro; 1 = corte seco, sin rampa). Del visor de vuelo
// (iw_sles55347.c): ->0x800 1/10 (Viewer_StateFlyUpdate0 431700-431705); ->0x329/0x32A 1/15
// (431708, 431713); ->0x328 1/20 (431717-431725); ->0x324 1/18 (431735-431741); ->0x32D 1/10
// (431744-431750); ->0x32C 1/5 (Viewer_StatePoseBank5/6 430600, 430671) o 1/15 desde 0x330;
// ->0x323 1/5 (Viewer_StatePoseDive 430795-430800); ->0x321 1/10 (Viewer_StatePoseTurn
// 430742-430749); desde 0x330 SIEMPRE 1/15 (los `if (iVar1 == 0x330) f = 15` de todos).
uint32_t CuadrosMezcla(uint32_t de, uint32_t a) {
  if (de == 0x330) return 15;
  switch (a) {
    case 0x800: return 10;
    case 0x329:
    case 0x32A: return 15;
    case 0x328: return 20;
    case 0x324: return 18;
    case 0x32D: return 10;
    // studies 4 (frente 04): el frenazo del visor. Viewer_StateFlyUpdate3/4/6 (431016-431020,
    // 431335, 431457) y 431892-431897: ->0x32E 1/5 (1/15 desde 0x330, ya cubierto arriba).
    case 0x32E: return 5;
    case 0x32C: return 5;
    case 0x323: return 5;
    case 0x321: return 10;
    default: return 15;
  }
}
// 06: transiciones que IW corta (CharModel_SetAnimBlend(1.0f, ...): el peso llega a 1 en el
// mismo cuadro) o que no funde (el codigo de la animacion se escribe directo):
//  * Poses de los minijuegos (alias >= kQteBase): IW las cambia escribiendo el codigo en el
//    modelo (+0xb60 = modelo+0x520, leido por Animations 214945) y llamando a su tick, sin
//    mezcla. Los 41 CharModel_SetAnimBlend del decomp estan TODOS fuera de los modulos de
//    minijuegos (ninguno entre 0x400000 y 0x480000) y los gestores de pose escriben +0xb60
//    directo: Char_EnterPoseState0x11/0x12/0x14/0x15 (424575, 425639, 425511, 425450), la
//    familia NB014 0x35b200-0x35c500 (estados 0x0..0xf -> 0x569..0x58d, 0x541..0x547...), y
//    el choque Mg_SetClashStepPoses/Mg_SetClashResultPoses (443219, 443063) via
//    Char_EnterPoseStateByIndex / Char_EnterPoseByTable_49ad90.
//  * Visor de vuelo: ->0x32F corta salvo desde 0x330 (Viewer_StateEnterFinalPose 430509-430513), ->0x32B corta salvo
//    desde 0x330 (FlyingMinigameCharacterHeightPosition 431978-431982) y el guion corta a cualquier codigo
//    (Cutscene_GotoLine 432595). ->0x322 y ->0x32C aparecen de las dos clases, pero el corte NO depende del codigo
//    destino sino del estado del visor (leido 2026-10-25 en CharModel_SetAnimBlend 0x2666C0: peso inicial 1.0 y tasa 0
//    = corte; peso 0 y tasa 1/n = fundido):
//      - Viewer_StatePoseTurn (430719) ->0x322 con 1/5 desde el estado 5/6, y CORTE desde el estado 2 (el 0x321,
//        430734-430738); Viewer_StateFlyUpdate3 (431370) ->0x322 funde.
//      - Viewer_StatePoseBank5/6 (430600/430671) ->0x32C funden 1/5 (1/15 desde 0x330); Viewer_StateFlyUpdate5/6
//        (431067-431070, 431228-431231) CORTAN a 0x32C cuando acaba una pose de banco salvo desde 0x330 (1/15).
//    El corte es, pues, el camino "acabo la animacion" (Update5/6 + FlyingMinigame...), no una propiedad de 0x322/0x32C;
//    el port no reproduce ese autómata de estados (cambia codigos a mano) y por eso los deja fundidos: es lo que hacen
//    las llamadas de pose directas (PoseTurn/PoseBank/StateFlyUpdate3), que son las que corresponden a un cambio de codigo.
bool CorteMezcla(uint32_t de, uint32_t a) {
  if (std::getenv("DBZ3_MEZCLA_FORZADA")) return false;   // prueba: fundir aunque IW corte
  if (de == 0x330) return false;                          // de 0x330 siempre hay rampa (1/15)
  if (de >= kQteBase || a >= kQteBase) return true;       // poses de los minijuegos: corte
  return a == 0x32F || a == 0x32B;
}
// 06: sonidos de los eventos. Los valores de IW son ids de su banco de efectos del IOP
// (AMSOUND, muestras en la SPU2: Hits_TransformationSounds -> FUN_0013f570 -> rpc 0xF), que no
// esta en el AFS (los ADX del juego son voces/musica): no hay fichero que decodificar, asi que la
// equivalencia no se puede sacar del decomp. Se suenan los de B3 que ya usa el port con
// sub_820922A0, medidos en un combate (banco 2 id 2 = impacto; banco 4 ids 19/20/25 = quejido de
// golpe) y se mantiene el reparto por tipo de evento de IW (1/2/7 golpe o ki, 3 voz).
struct SeB3 {
  int banco = 0, id = 0;
  bool son = false;
};
SeB3 SeB3DeEvento(uint32_t tipo) {
  switch (tipo) {
    case 1:
    case 2:
    case 7: return {2, 2, true};   // sonido de golpe / audio de ki
    case 3: return {4, 19, true};  // voz del personaje (el valor de IW no se traduce)
    default: return {};            // efecto/escena/boca/valor: no portado (ver DBZ3_IW_EVENTOS_LOG)
  }
}
// 06: "ha sonado un evento hace poco" por luchador: el respaldo de los minijuegos (retardos
// fijos, SeGolpe de iw_mini_botones.cpp) lo consulta para no doblar el sonido cuando la
// animacion ya traia el suyo. Tick escribe (hilo del juego); el minijuego lee (interfaz).
constexpr int kEventoSonando = 30;   // cuadros
std::atomic<uint32_t> g_ev_x[2];
std::atomic<int> g_ev_ticks[2];
void MarcarEventoSonoro(uint32_t x) {
  for (int i = 0; i < 2; ++i)
    if (g_ev_x[i].load() == x || g_ev_ticks[i].load() <= 0) {
      g_ev_x[i].store(x);
      g_ev_ticks[i].store(kEventoSonando);
      return;
    }
  g_ev_x[0].store(x);
  g_ev_ticks[0].store(kEventoSonando);
}
void DispararEvento(PPCContext& ctx, uint8_t* base, uint32_t x, uint32_t codigo, const Anim::Evento& ev) {
  static const bool log = std::getenv("DBZ3_IW_EVENTOS_LOG") != nullptr;
  const SeB3 se = SeB3DeEvento(ev.tipo);
  if (se.son && !std::getenv("DBZ3_IW_EVENTOS_OFF")) {
    const PPCContext guardado = ctx;
    ctx.r3.u64 = uint64_t(uint32_t(se.banco));
    ctx.r4.u64 = 0;
    ctx.r5.u64 = uint64_t(uint32_t(se.id));
    ctx.r6.u64 = 0;
    sub_820922A0(ctx, base);   // el gestor de efectos de B3 (iw_minijuegos.cpp SeBotones)
    ctx = guardado;
    MarcarEventoSonoro(x);
  }
  if (log)
    REXLOG_INFO("dbz3 anim: evento de IW: luchador 0x{:08X} 0x{:X} cuadro {} tipo {} valor {} ({})", x, codigo, ev.cuadro,
                ev.tipo, ev.valor, se.son ? "suena" : "sin equivalente (no portado)");
}
uint32_t g_mezcla = 15;   // la de la transicion en curso (Tick mira cuando acaba)
// Una pista en el cuadro f (angular: claves [cuadro u16, x, y, z u16]; posicional: [cuadro u32, x, y, z f32]). Sin claves
// deja `out` como esta (el que llama pone antes la pose de la otra animacion: nunca ceros).
void Muestra(uint8_t* base, uint32_t trk, bool angular, float f, float out[3]) {
  const uint32_t nk = trk ? REX_LOAD_U32(trk + 8) : 0, paso = angular ? 8 : 16;
  if (!nk) return;
  auto cuadro = [&](uint32_t k) {
    const uint32_t e = trk + 12 + paso * k;
    return float(angular ? REX_LOAD_U16(e) : REX_LOAD_U32(e));
  };
  auto valor = [&](uint32_t k, int c) {
    const uint32_t e = trk + 12 + paso * k;
    if (angular) return float(REX_LOAD_U16(e + 2 + 2 * uint32_t(c)));
    const uint32_t u = REX_LOAD_U32(e + 4 + 4 * uint32_t(c));
    float v;
    std::memcpy(&v, &u, 4);
    return v;
  };
  uint32_t a = 0;
  while (a + 1 < nk && cuadro(a + 1) <= f) ++a;
  const uint32_t b = a + 1 < nk ? a + 1 : a;
  const float fa = cuadro(a), fb = cuadro(b), u = fb > fa ? std::clamp((f - fa) / (fb - fa), 0.0f, 1.0f) : 0.0f;
  for (int c = 0; c < 3; ++c) {
    float d = valor(b, c) - valor(a, c);
    if (angular) d = float(int16_t(uint16_t(int32_t(d))));   // por el camino corto
    out[c] = valor(a, c) + u * d;
  }
}
const Anim* AnimDe(uint32_t codigo) {
  for (uint32_t c = 0; c < Total(); ++c)
    if (AnimN(c).codigo == codigo) return &AnimN(c);
  return nullptr;
}
void CrearTransicion(uint8_t* base) {
  Anim t;
  t.codigo = kTransicion;
  t.flags = 0x9;
  t.cuadros = kMaxMezcla + 1;
  if (const Anim* q = AnimDe(kQuieto)) std::copy(q->bloque, q->bloque + 11, t.bloque);
  for (uint32_t c = 0; c < Total(); ++c) {
    const Anim& a = AnimN(c);
    for (size_t w = 0; w < a.sufijos.size(); ++w) {
      if (std::find(t.sufijos.begin(), t.sufijos.end(), a.sufijos[w]) != t.sufijos.end()) continue;
      std::array<uint32_t, 2> pj{0, 0};
      for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t src = a.pistas[w][i], tam = 12 + (kMaxMezcla + 1) * (i == 0 ? 8 : 16);
        if (!src) continue;
        const uint32_t g = dbz3::roster::GuestAlloc(tam);
        if (!g) return;
        for (uint32_t q = 0; q < 8; q += 4) REX_STORE_U32(g + q, REX_LOAD_U32(src + q));   // cabecera de la pista
        REX_STORE_U32(g + 8, kMaxMezcla + 1);
        for (uint32_t q = 12; q < tam; ++q) REX_STORE_U8(g + q, REX_LOAD_U8(src + 12 + (q - 12) % (i == 0 ? 8 : 16)));
        pj[i] = g;
      }
      t.sufijos.push_back(a.sufijos[w]);
      t.pistas.push_back(pj);
    }
  }
  g_ex.push_back(std::move(t));
}
// 06: mezcla de giros por cuaternion (slerp) para la pista de transicion, en vez de interpolar
// los tres angulos Euler por separado. Medido con los datos reales (datos/analizar_mezcla.py):
// la interpolacion por componentes se desvia hasta 180 grados de la mezcla directa de las dos
// orientaciones en pares que el port usa (p. ej. NB014 0x59D -> 0x582, hueso GOK_LLEG2, con el
// giro pasando por 179.99 grados; 915 0x32D -> 0x32F, GOK_WAIST; 921 0x258 -> 0x262,
// GHS_RFOOT2). El slerp no depende de la representacion Euler de cada pose. Las claves siguen
// siendo Euler u16 (R = Rz Ry Rx, la convencion del port) y se escriben con continuidad entre
// claves vecinas (sin saltos de +/-65536, que la interpolacion del motor no sabe deshacer).
struct Mat3 {
  double m[3][3];
};
Mat3 MatrizDeEuler(const double v[3]) {
  constexpr double kU = 3.14159265358979323846 / 32768.0;
  const double ax = v[0] * kU, ay = v[1] * kU, az = v[2] * kU;
  const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az),
               sz = std::sin(az);
  return {{{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
           {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
           {-sy, cy * sx, cy * cx}}};
}
void CuaternionDeMatriz(const Mat3& R, double q[4]) {
  const double t = R.m[0][0] + R.m[1][1] + R.m[2][2];
  if (t > 0) {
    const double s = std::sqrt(t + 1.0) * 2;
    q[0] = 0.25 * s;
    q[1] = (R.m[2][1] - R.m[1][2]) / s;
    q[2] = (R.m[0][2] - R.m[2][0]) / s;
    q[3] = (R.m[1][0] - R.m[0][1]) / s;
  } else if (R.m[0][0] > R.m[1][1] && R.m[0][0] > R.m[2][2]) {
    const double s = std::sqrt(1.0 + R.m[0][0] - R.m[1][1] - R.m[2][2]) * 2;
    q[0] = (R.m[2][1] - R.m[1][2]) / s;
    q[1] = 0.25 * s;
    q[2] = (R.m[0][1] + R.m[1][0]) / s;
    q[3] = (R.m[0][2] + R.m[2][0]) / s;
  } else if (R.m[1][1] > R.m[2][2]) {
    const double s = std::sqrt(1.0 + R.m[1][1] - R.m[0][0] - R.m[2][2]) * 2;
    q[0] = (R.m[0][2] - R.m[2][0]) / s;
    q[1] = (R.m[0][1] + R.m[1][0]) / s;
    q[2] = 0.25 * s;
    q[3] = (R.m[1][2] + R.m[2][1]) / s;
  } else {
    const double s = std::sqrt(1.0 + R.m[2][2] - R.m[0][0] - R.m[1][1]) * 2;
    q[0] = (R.m[1][0] - R.m[0][1]) / s;
    q[1] = (R.m[0][2] + R.m[2][0]) / s;
    q[2] = (R.m[1][2] + R.m[2][1]) / s;
    q[3] = 0.25 * s;
  }
}
void Slerp(const double q0[4], const double q1[4], double t, double out[4]) {
  double b[4];
  std::copy(q1, q1 + 4, b);
  double d = q0[0] * b[0] + q0[1] * b[1] + q0[2] * b[2] + q0[3] * b[3];
  if (d < 0) {
    for (double& x : b) x = -x;
    d = -d;
  }
  if (d > 0.9995) {
    std::copy(q0, q0 + 4, out);
    return;
  }
  const double th0 = std::acos(std::clamp(d, -1.0, 1.0)), th = th0 * t;
  const double s0 = std::sin(th0 - th) / std::sin(th0), s1 = std::sin(th) / std::sin(th0);
  for (int i = 0; i < 4; ++i) out[i] = s0 * q0[i] + s1 * b[i];
}
void MatrizDeCuaternion(const double q[4], Mat3& R) {
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  R.m[0][0] = 1 - 2 * (y * y + z * z);
  R.m[0][1] = 2 * (x * y - w * z);
  R.m[0][2] = 2 * (x * z + w * y);
  R.m[1][0] = 2 * (x * y + w * z);
  R.m[1][1] = 1 - 2 * (x * x + z * z);
  R.m[1][2] = 2 * (y * z - w * x);
  R.m[2][0] = 2 * (x * z - w * y);
  R.m[2][1] = 2 * (y * z + w * x);
  R.m[2][2] = 1 - 2 * (x * x + y * y);
}
void EulerDeMatriz(const Mat3& R, double out[3]) {
  constexpr double kU = 3.14159265358979323846 / 32768.0;
  out[1] = std::asin(std::clamp(-R.m[2][0], -1.0, 1.0)) / kU;
  out[0] = std::atan2(R.m[2][1], R.m[2][2]) / kU;
  out[2] = std::atan2(R.m[1][0], R.m[0][0]) / kU;
}

// Rellena la transicion: de `de` en su cuadro `f` a `a` en su cuadro 0. false si alguna no es de IW.
bool PrepararTransicion(uint8_t* base, uint32_t de, float f, uint32_t a) {
  const Anim *A = AnimDe(de), *B = AnimDe(a), *T = AnimDe(kTransicion);
  if (!A || !B || !T) return false;
  const uint32_t n = CuadrosMezcla(de, a);
  g_mezcla = n;
  for (size_t w = 0; w < T->sufijos.size(); ++w) {
    auto pista = [&](const Anim* x, uint32_t i) -> uint32_t {
      for (size_t k = 0; k < x->sufijos.size(); ++k)
        if (x->sufijos[k] == T->sufijos[w]) return x->pistas[k][i];
      return 0;
    };
    for (uint32_t i = 0; i < 2; ++i) {
      const uint32_t dst = T->pistas[w][i];
      if (!dst) continue;
      const bool ang = i == 0;
      const uint32_t pa = pista(A, i), pb = pista(B, i);
      if (!pa && !pb) continue;   // ninguna la mueve: se queda como estaba
      // pose de partida: A en su cuadro f (sin pista o sin claves en A: la de B, nunca ceros)
      float va[3] = {}, vb0[3] = {};
      Muestra(base, pb ? pb : pa, ang, pb ? 0.0f : f, vb0);
      std::copy(vb0, vb0 + 3, va);
      Muestra(base, pa, ang, f, va);
      const uint32_t paso = ang ? 8 : 16;
      REX_STORE_U32(dst + 8, n + 1);
      // 06: la pista angular va por slerp (mezcla de las dos orientaciones), no por Euler por
      // componentes; `qa` = giro de la pose de partida y `prev` = ultima clave escrita (para
      // dar continuidad a la representacion Euler entre claves vecinas).
      double qa[4] = {}, prev[3] = {};
      if (ang) {
        const double ve[3] = {double(va[0]), double(va[1]), double(va[2])};
        const Mat3 ra = MatrizDeEuler(ve);
        CuaternionDeMatriz(ra, qa);
        std::copy(va, va + 3, prev);
      }
      for (uint32_t k = 0; k <= n; ++k) {   // clave por cuadro: B avanza (su cuadro k) y el peso sube k/n
        float vb[3];
        std::copy(va, va + 3, vb);
        if (pb) Muestra(base, pb, ang, float(k), vb);
        const float w = float(k) / float(n);
        const uint32_t e = dst + 12 + paso * k;
        if (ang && k > 0) {
          double qb[4] = {}, qm[4] = {}, v[3] = {};
          const double vb2[3] = {double(vb[0]), double(vb[1]), double(vb[2])};
          const Mat3 rb = MatrizDeEuler(vb2);
          CuaternionDeMatriz(rb, qb);
          Slerp(qa, qb, double(w), qm);
          Mat3 rm;
          MatrizDeCuaternion(qm, rm);
          EulerDeMatriz(rm, v);
          // El giro R = Rz Ry Rx tiene DOS representaciones: (x, y, z) y (x+180, 180-y, z+180)
          // (en unidades u16, 180 = 32768). Se elige la mas cercana a la clave anterior y se
          // pasa a la unidad mas cercana: el motor interpola las claves en linea recta y un
          // cambio de rama del Euler (p. ej. cerca de y = 180) daria un salto de 180 grados en
          // un cuadro; medido en probar_mezcla.py (los dos candidatos y su continuidad).
          const double cand[2][3] = {{v[0], v[1], v[2]}, {v[0] + 32768.0, 32768.0 - v[1], v[2] + 32768.0}};
          double mejor[3] = {}, coste_mejor = 1e30;
          for (int b = 0; b < 2; ++b) {
            double x[3] = {}, coste = 0;
            for (int c = 0; c < 3; ++c) {
              double u = cand[b][c];
              while (u - prev[c] > 32768.0) u -= 65536.0;
              while (prev[c] - u > 32768.0) u += 65536.0;
              x[c] = u;
              coste = std::max(coste, std::fabs(u - prev[c]));
            }
            if (coste < coste_mejor) {
              coste_mejor = coste;
              std::copy(x, x + 3, mejor);
            }
          }
          REX_STORE_U16(e, uint16_t(k));
          for (int c = 0; c < 3; ++c) {
            prev[c] = mejor[c];
            REX_STORE_U16(e + 2 + 2 * uint32_t(c), uint16_t(int64_t(std::llround(mejor[c])) & 0xFFFF));
          }
        } else if (ang) {
          REX_STORE_U16(e, uint16_t(k));
          for (int c = 0; c < 3; ++c)
            REX_STORE_U16(e + 2 + 2 * uint32_t(c), uint16_t(int32_t(std::lround(va[c])) & 0xFFFF));
        } else {
          float v[3];
          for (int c = 0; c < 3; ++c) v[c] = va[c] + w * (vb[c] - va[c]);
          REX_STORE_U32(e, k);
          for (int c = 0; c < 3; ++c) {
            uint32_t u;
            std::memcpy(&u, &v[c], 4);
            REX_STORE_U32(e + 4 + 4 * uint32_t(c), u);
          }
        }
      }
    }
  }
  // R6 (NB014): la transicion llevaba el bloque y las banderas del reposo comun (0x800); con las poses de los minijuegos de
  // botones (raiz absoluta, en el aire a y ~40) el juego la leia como la del reposo y el luchador caia al suelo los 8
  // cuadros de la mezcla (Gohan parpadeaba abajo, rafaga 2026-10-17). Ahora toma los de la animacion a la que va.
  const CacheCuerpo* cc = &g_cache[0];
  for (const CacheCuerpo& q : g_cache)
    if (q.acm && q.acm == g_acm_principal) {
      cc = &q;
      break;
    }
  if (g_csk_principal && cc->acm) {
    const uint32_t bt = REX_LOAD_U32(g_csk_principal + 0x20 + 4 * kTransicion);
    if (bt)
      for (uint32_t w = 0; w < 11; ++w) REX_STORE_U32(bt + 4 + 4 * w, B->bloque[w]);
    for (uint32_t c = 0; c < Total(); ++c)
      if (AnimN(c).codigo == kTransicion) {
        const uint32_t e = Abs(cc->tabla, cc->acm) + 16 * (cc->base_idx + c);
        REX_STORE_U32(e, B->flags);
        REX_STORE_U32(e + 4, B->var);
        REX_STORE_U32(e + 8, n + 1);   // cuadros de esta mezcla
      }
  }
  return true;
}

// El scatto: #BSK comun de IW (204 hijo 3) -> almacen global (204 hijo 0), con la raiz girada. El corredor: IW no tiene
// 0xE2 para humanos (solo el #BSK de Bubbles, 912, esqueleto BBR); el unico ciclo de carrera humano de sus datos es
// el codigo 4 de DATA_CMN 925 (Piccolo, XPIC: anim 4, 32 cuadros, piernas 77-80 grados), con la cadera ya en la
// convencion de B3 (eje Z a -34/-57 grados, como la guardia): sin giro.
bool Leer(uint8_t* base) {
  if (g_leida) return g_ok;
  g_leida = true;
  const std::filesystem::path iw = P::BuscarIW();
  P::AfsPs2 cmn;
  if (iw.empty() || !cmn.Abrir(iw, "DATA_CMN.AFS")) {
    REXLOG_WARN("dbz3 anim: sin la copia de Infinite World en ps2_games");
    return false;
  }
  const std::vector<uint8_t> pack = cmn.Entrada(kIwFid), pic = cmn.Entrada(kIwPiccolo);
  const std::vector<uint8_t> global = P::HijoAmb(pack, 0), comun = P::HijoAmb(pack, 3);
  for (uint32_t c = 0; c < kN; ++c)
    if (!Copiar(base, g_an[c], comun, kPrimero + c, global, {}, true)) {
      REXLOG_WARN("dbz3 anim: DATA_CMN {} de IW no es el almacen comun esperado", kIwFid);
      return false;
    }
  if (!Copiar(base, g_an[kN], P::HijoAmb(pic, 0), 4, global, P::HijoAmb(pic, 1), false)) {
    REXLOG_WARN("dbz3 anim: DATA_CMN {} de IW no es el moveset de Piccolo esperado", kIwPiccolo);
    return false;
  }
  g_an[kN].codigo = kCorredor;
  CerrarRaiz(base, g_an[kN]);
  for (const Extra& e : kExtras) {   // R16: las que falten no impiden las demas
    Anim an;
    const std::vector<uint8_t> amb = e.entrada == kIwFid ? pack : cmn.Entrada(e.entrada);
    // 915 (el modelo del visor de vuelo) lleva el #BSK en el hijo 3 y su #AMM en el 0; 922/914, en el 0 y el 1
    const bool ok = e.entrada == kIwFid    ? Copiar(base, an, comun, e.codigo, global, {}, e.girar)
                    // DS169: 535 (BSK de Goku) trae el #BSK en el hijo 0, pero sus animaciones del 0xF0 viven en el
                    // almacen global (204/0), no en un #AMM propio (535/1 esta vacio).
                    : e.entrada == 535 ? Copiar(base, an, P::HijoAmb(amb, 0), e.codigo, global, {}, false)
                    // en el visor de vuelo el "almacen 0" es su propio #AMM (hijo 0), no el global de 204
                    // y sus poses van de lado como el scatto (+X): con la raiz girada
                    : e.entrada == kIwVuelo ? Copiar(base, an, P::HijoAmb(amb, 3), e.codigo, P::HijoAmb(amb, 0), P::HijoAmb(amb, 0), true)
                                           : Copiar(base, an, P::HijoAmb(amb, 0), e.codigo, global, P::HijoAmb(amb, 1), false);
    // DS89: el salto de Cell (914 0x1F-0x24 y la frenada 0x18) tambien lleva la cadera en coordenadas de IW; se le quita
    // el origen XZ como a las entradas/finales y la mueve la fisica del port.
    const bool salto_914 = e.entrada == 914 && ((e.codigo >= 0x1F && e.codigo <= 0x24) || e.codigo == 0x18);
    // SOL4: EU 0x2997F0 pone el actor en el origen en las secuencias finales.
    // Estas pistas y sus AMC son un par en coordenadas de campo: NO quitar solo
    // el origen de la animacion dejando la camara absoluta (Snake Way/Namek).
    const bool final_campo = e.codigo == 0x52 || e.codigo == 0x53 || e.codigo == 0x119 || e.codigo == 0x11A ||
                             e.codigo == 0x17D || e.codigo == 0x17E || e.codigo == 0x49D || e.codigo == 0x49E ||
                             e.codigo == 0x62D || e.codigo == 0x62E || e.codigo == 0x6F5 || e.codigo == 0x6F6;
    if (ok && !final_campo && e.entrada != kIwFid && e.entrada != kIwVuelo &&
        (salto_914 || (e.codigo >= 0x51 && e.codigo <= 0x6F6 && e.codigo != 0x62C &&
        e.codigo != 0x62D && e.codigo != 0x62E && (e.codigo < 0x640 || e.codigo > 0x66C))))
      SinOrigenXZ(base, an);   // entradas y finales de los minijuegos de campo (922): sin la posicion absoluta de IW
    // DS122: las animaciones de actor de la intro de v1 (0x758/0x75D/0x75E, DATA_CMN 923/919/927) llevan la cadera
    // en coordenadas del escenario de IW (primer cuadro: y = 258, z = -4422) y una trayectoria de 3900-8100 u que es
    // la misma que sigue el objetivo del clip de camara (#AMC 891/25, pista 1: 1400 z -4415->3729, 1401 -4415->-518).
    // La intro coloca al actor con Poner en ese objetivo cada cuadro (AvanzarIntro), asi que esa trayectoria ya la
    // pone la camara: se anula la pista de posicion de la cadera (queda en su bind) para no sumarla dos veces. En
    // estas animaciones la cadera es el UNICO hueso con pista de posicion, asi que la pose (giros) no se toca.
    const bool intro_v1 = e.codigo == 0x758 || e.codigo == 0x75D || e.codigo == 0x75E;
    if (ok && intro_v1)
      for (size_t w = 0; w < an.sufijos.size(); ++w)
        if (an.sufijos[w] == "WAIST") an.pistas[w][1] = 0;
    if (ok && e.como) an.codigo = e.como;
    // las de Cell (0x960-0x96B y DS89 0xC00-0xC0A, del #BSK de 914, un esqueleto GOK_* de nino): solo los giros, salvo
    // la cadera; sus posiciones son las longitudes de los huesos de aquel y estiraban a Cell (alas y cola en pinchos,
    // captura 164915). En la locomocion/salto propio de 914 la cadera conserva su avance y su altura.
    if (ok && ((an.codigo >= 0x960 && an.codigo <= 0x96F) ||
               (an.codigo >= kCellQuieto && an.codigo <= kCellParar)))
      for (size_t w = 0; w < an.sufijos.size(); ++w)
        if (an.sufijos[w] != "WAIST") an.pistas[w][1] = 0;
    if (ok && (an.codigo == kCorrer || an.codigo == kQuieto || an.codigo == kReposo || an.codigo == kAndar || an.codigo == 0x805 ||
               an.codigo == 0x806 || an.codigo == 0x807 || an.codigo == kCellQuieto || an.codigo == kCellCorrer))
      CerrarRaiz(base, an);   // en bucle, o quieto en su sitio (cansado: la cadera de IW deriva ~20 u)
    if (ok) g_ex.push_back(std::move(an));
    else REXLOG_WARN("dbz3 anim: codigo 0x{:X} de IW (DATA_CMN {}) no se pudo leer", e.codigo, e.entrada);
  }
  // R14: poses de los minijuegos de botones (kQte). Cada personaje se lee una vez (BSK y #AMM propio).
  {
    uint32_t actual = 0;
    std::vector<uint8_t> bsk, amm;
    int leidas = 0;
    g_callada = true;
    for (const QteAnim& q : kQte) {
      if (q.cmn != actual) {
        actual = q.cmn;
        const std::vector<uint8_t> amb = cmn.Entrada(q.cmn);
        bsk = P::HijoAmb(amb, q.cmn == kIwVuelo ? 3 : 0);
        amm = P::HijoAmb(amb, q.cmn == kIwVuelo ? 0 : 1);
      }
      Anim an;
      // 915: su almacen "0" es su propio #AMM (hijo 0); el resto, el propio (hijo 1, almacen 3)
      const bool ok = Copiar(base, an, bsk, q.codigo, q.cmn == kIwVuelo ? amm : global, amm, false);
      if (!ok) {
        REXLOG_WARN("dbz3 anim: pose de botones 0x{:X} (DATA_CMN {}) no se pudo leer", q.codigo, q.cmn);
        continue;
      }
      an.codigo = AliasQte(q.cmn, q.codigo);
      // el lado que IW escribio a 180 grados del otro (en NB013 y NB014 el jugador: 922 y 931; en NB006 Piccolo: 925; las
      // 57A / 57C de 915 son del jugador): la cadera de su idle esta girada 180 grados sobre Y respecto a la guardia de B3
      const uint32_t c = q.codigo;
      const bool familiaB = (c >= 0x2BC && c <= 0x2D0) || (c >= 0x30C && c <= 0x30E) || (c >= 0x4B0 && c <= 0x4C4) ||
                            (c >= 0x500 && c <= 0x502) || (c >= 0x76C && c <= 0x7BE);   // ya miran a la camara
      if (!familiaB && (q.cmn == 922 || q.cmn == 931 || q.cmn == 925 || (q.cmn == 915 && (q.codigo == 0x57A || q.codigo == 0x57C))))
        for (size_t w = 0; w < an.sufijos.size(); ++w)
          if (an.sufijos[w] == "WAIST") GirarYaw(base, an.pistas[w][0], an.pistas[w][1], 3.14159265358979323846);
      if (familiaB) SinOrigenXZ(base, an);   // familia B: sin la posicion absoluta del escenario de IW
      // los reposos (en bucle) no deben derivar: la cadera volveria a saltar al empezar otra vuelta
      if (q.codigo == 0x4B0 || q.codigo == 0x2BC || q.codigo == 0x76C || q.codigo == 0x76E || q.codigo == 0x770 ||
          // (0x578 / 0x579, el READY de NB013 / NB014, no son reposos: suben al aire de y 6-10 a ~44 y se ponen una vez; con
          // la rampa quitada Gohan se quedaba en el suelo, fuera del plano de la camara de IW. R6, 2026-10-17)
          q.codigo == 0x7BC || q.codigo == 0x57A || q.codigo == 0x57B || q.codigo == 0x258 ||
          q.codigo == 0x259 || q.codigo == 0x5C8 || q.codigo == 0x5CD)
        CerrarRaiz(base, an);
      g_ex.push_back(std::move(an));
      ++leidas;
    }
    g_callada = false;
    REXLOG_INFO("dbz3 anim: {} poses de botones de IW leidas", leidas);
  }
  CrearTransicion(base);
  // s10 (G01_iw_cam): dejar en el registro de que copia se leyeron los clips y en que region. Es la comprobacion de
  // que USA/EU no se mezclan: la misma `iw` sirve DATA_CMN y DATA_ENG (europea, SLES_553.47) o DATA_USA (americana,
  // SLUS_218.42); si hubiera dos copias en ps2_games, BuscarIW elige una y todo sale de esa misma carpeta/ISO.
  {
    std::error_code ec;
    const size_t elf = P::LeerElf(iw).size();   // SLUS_218.42 = 3784752 B; SLES_553.47 = 3786800 B
    const char* region = elf == 3786800 ? "EU (SLES_553.47)" : elf == 3784752 ? "USA (SLUS_218.42)" : "region no reconocida";
    REXLOG_INFO("dbz3 anim: fuente IW {} [{}] {}: {} codigos propios leidos de la misma copia", iw.string(),
                std::filesystem::is_directory(iw, ec) ? "carpeta" : "ISO", region, g_ex.size());
  }
  g_ok = true;
  return true;
}

// Bloque de huesos de una animacion para el #ACM global de B3, casando los huesos por nombre sin prefijo.
uint32_t Bloque(uint8_t* base, uint32_t acm, const Anim& an, bool relativo) {
  const uint32_t nb = REX_LOAD_U32(acm + 0x18), nombres = Abs(REX_LOAD_U32(acm + 0x1C), acm);
  if (nb == 0 || nb > 128) return 0;
  const uint32_t g = dbz3::roster::GuestAlloc(8 * nb);
  if (!g) return 0;
  for (uint32_t j = 0; j < nb; ++j) {
    char n[32];
    for (uint32_t k = 0; k < 32; ++k) n[k] = char(REX_LOAD_U8(nombres + 32 * j + k));
    const std::string s = Sufijo(n, 32);
    size_t w = 0;
    while (w < an.sufijos.size() && an.sufijos[w] != s) ++w;
    if (w == an.sufijos.size()) continue;
    for (uint32_t i = 0; i < 2; ++i)
      if (const uint32_t p = an.pistas[w][i]) REX_STORE_U32(g + 4 * (2 * j + i), relativo ? p - acm : p);
  }
  return g;
}

// Cache del #ACM de un cuerpo (hasta dos cuerpos con almacenes distintos). nullptr si no hay hueco.
CacheCuerpo* CacheDe(uint32_t acm) {
  for (CacheCuerpo& c : g_cache)
    if (c.acm == acm) return &c;
  for (CacheCuerpo& c : g_cache)
    if (!c.acm) return &c;
  return nullptr;
}

// Las animaciones de IW detras de las del #ACM del cuerpo A (tabla nueva = la vieja + Total; no se toca ninguna
// entrada) y un #CSK propio (codigos 0x19..0x1E, 0xE2 y los alias 0x8xx/0xAxx/0xCxx) en un hueco libre de los 3
// del cuerpo; `hueco` = el que se ocupo (0 si ya estaba). false si no se puede.
// DS100: se llama para el cuerpo A (X+0x650) y el B (X+0xCD0). Si los dos usan el mismo #ACM, la cache se
// reutiliza; si no, se anade la tabla a cada uno (es lo que hace que la carrera se vea en la forma transformada).
// DS103: el #CSK se cachea POR CUERPO y va FUNDIDO con el suyo en el hueco 0. sub_820F95F0 recorre los tres
// huecos y usa el PRIMER #CSK cuyo `n` cubra el codigo y cuya entrada no sea 0: con los alias 0x8xx dentro del
// rango del #CSK de B3 (n ~2048) y algun codigo alto ocupado, el de B3 podia ganar y dibujar SU animacion; y en
// el cuerpo B no siempre queda un hueco NULL en +0x2C0. Copiando sus entradas + las nuestras en el hueco 0 se
// resuelve todo sin depender del hueco libre.
CacheCsk* CacheCskDe(uint32_t A) {
  for (CacheCsk& c : g_csk)
    if (c.A == A) return &c;
  for (CacheCsk& c : g_csk)
    if (!c.A) return &c;
  return nullptr;
}

bool InstalarCuerpo(uint8_t* base, uint32_t A, uint32_t& csk, uint32_t& hueco, uint32_t& csk_prev) {
  csk = 0;
  hueco = 0;
  csk_prev = 0;
  // DS100: el cuerpo B puede no existir o no tener modelo cargado; no se dereferencia un puntero sin rango.
  // DS103: si no se puede instalar se dice POR QUE (una vez por motivo, max 8 lineas): sin esto, un cuerpo que
  // fallaba antes de tocar el #CSK no dejaba rastro y la causa (modelo/#ACM del cuerpo) era invisible en el log.
  static int avisos_fallo = 0;
  auto fallo = [&](const char* que, uint32_t p, uint32_t q) {
    if (avisos_fallo++ < 8)
      REXLOG_WARN("dbz3 anim: cuerpo 0x{:08X} sin instalar: {} (0x{:08X}, 0x{:08X})", A, que, p, q);
    return false;
  };
  auto puntero_ok = [](uint32_t p) { return p >= 0x10000000u && p < 0xF0000000u; };
  const uint32_t modelo = REX_LOAD_U32(A + kModelo);
  if (!puntero_ok(modelo) || !puntero_ok(modelo + kAlmacenes)) return fallo("modelo", modelo, 0);
  const uint32_t acm = REX_LOAD_U32(modelo + kAlmacenes);
  if (!puntero_ok(acm) || (REX_LOAD_U32(acm) & 0x00FFFFFFu) != kAcm)
    return fallo("acm/magia", acm, puntero_ok(acm) ? REX_LOAD_U32(acm) : 0);
  CacheCuerpo* c = CacheDe(acm);
  if (!c) return fallo("cache #ACM llena", acm, 0);
  if (c->acm != acm || REX_LOAD_U32(acm + 0x14) != c->tabla) {
    const uint32_t n = REX_LOAD_U32(acm + 0x10), crudo = REX_LOAD_U32(acm + 0x14), tbl = Abs(crudo, acm);
    if (n == 0 || n > 4096) return fallo("tabla #ACM", n, crudo);
    const bool rel_tabla = crudo < 0x01000000u;
    const uint32_t viejo = Abs(REX_LOAD_U32(tbl + 12), acm);   // bloque de la animacion 0: punteros absolutos o no
    bool rel = true;
    for (uint32_t k = 0; k < 8; ++k)
      if (const uint32_t p = REX_LOAD_U32(viejo + 4 * k)) {
        rel = p < 0x01000000u;
        break;
      }
    const uint32_t nt = dbz3::roster::GuestAlloc(16 * (n + Total()));
    if (!nt) return fallo("sin memoria (#ACM)", n, Total());
    for (uint32_t k = 0; k < 4 * n; ++k) REX_STORE_U32(nt + 4 * k, REX_LOAD_U32(tbl + 4 * k));
    for (uint32_t q = 0; q < Total(); ++q) {
      const uint32_t b = Bloque(base, acm, AnimN(q), rel), e = nt + 16 * (n + q);
      if (!b) return fallo("sin bloque de animacion", AnimN(q).codigo, 0);
      REX_STORE_U32(e, AnimN(q).flags);
      REX_STORE_U32(e + 4, AnimN(q).var);
      REX_STORE_U32(e + 8, AnimN(q).cuadros);
      REX_STORE_U32(e + 12, rel ? b - acm : b);
    }
    c->tabla = rel_tabla ? nt - acm : nt;
    REX_STORE_U32(acm + 0x14, c->tabla);
    REX_STORE_U32(acm + 0x10, n + Total());
    c->acm = acm;
    c->base_idx = n;
    REXLOG_INFO("dbz3 anim: animaciones de IW en el #ACM (0x{:08X}): {}..{} (punteros {})", acm, n,
                n + Total() - 1, rel ? "relativos" : "absolutos");
  }
  CacheCsk* cc = CacheCskDe(A);
  if (!cc) return fallo("cache #CSK llena", A, 0);
  // Ya instalado en alguno de los tres huecos de ESTE cuerpo y con la tabla del #ACM en su sitio: nada que hacer.
  if (cc->csk && cc->acm == acm) {
    for (uint32_t i = 0; i < 3; ++i)
      if (REX_LOAD_U32(A + kBsk + 4 * i) == cc->csk) {
        csk = cc->csk;
        hueco = cc->hueco;
        csk_prev = cc->prev;
        return true;
      }
  }
  // Hueco 0 (el primero que mira sub_820F95F0). Su contenido actual es el que hay que conservar. Si fuera un
  // #CSK nuestro de otra pasada se toma SU original (no se funde lo fundido).
  uint32_t O = REX_LOAD_U32(A + kBsk);
  for (const CacheCsk& q : g_csk)
    if (q.csk && q.csk == O) O = q.prev;
  uint32_t nO = 0, lstO = 0;
  if (puntero_ok(O)) {
    nO = REX_LOAD_U32(O + 0x10);
    if (nO > 4096) nO = 4096;
    lstO = Abs(REX_LOAD_U32(O + 0x14), O);
    if (!puntero_ok(lstO)) nO = 0;
  }
  // DS103: cuantos de nuestros alias (0x8xx/0xAxx/0xCxx) YA tenian entrada en el #CSK de B3 del hueco 0. Si son
  // varios, el primer #CSK que mira sub_820F95F0 podia ganar y dibujar SU animacion (la guardia) aunque el codigo
  // llegara a A+0x520. Se ve en el log; el #CSK fundido lo anula.
  uint32_t colisiones = 0, colision_codigo = 0;
  for (uint32_t q = 0; q < Total(); ++q) {
    const uint32_t cod = AnimN(q).codigo;
    if (cod < nO && REX_LOAD_U32(lstO + 4 * cod)) {
      if (!colisiones) colision_codigo = cod;
      ++colisiones;
    }
  }
  uint32_t n = std::max(nO, kCorredor + 1);
  for (const Anim& a : g_ex) n = std::max(n, a.codigo + 1);
  const uint32_t lst = 0x20, blks = lst + 4 * n;
  const uint32_t g = dbz3::roster::GuestAlloc(blks + 48 * Total());
  if (!g) return fallo("sin memoria (#CSK)", blks, Total());
  const uint32_t cab[8] = {0x2143534Bu /* "!CSK" */, 0x20, 0, 4, n, g + lst, 0, 0};
  for (uint32_t k = 0; k < 8; ++k) REX_STORE_U32(g + 4 * k, cab[k]);
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t p = i < nO ? REX_LOAD_U32(lstO + 4 * i) : 0;   // los codigos de B3, intactos
    REX_STORE_U32(g + lst + 4 * i, p);
  }
  for (uint32_t q = 0; q < Total(); ++q) {
    const uint32_t b = g + blks + 48 * q;
    REX_STORE_U32(g + lst + 4 * AnimN(q).codigo, b);
    REX_STORE_U16(b, uint16_t(c->base_idx + q));
    REX_STORE_U16(b + 2, 0);
    for (uint32_t w = 0; w < 11; ++w) REX_STORE_U32(b + 4 + 4 * w, AnimN(q).bloque[w]);
  }
  cc->A = A;
  cc->acm = acm;
  cc->csk = g;
  cc->prev = O;
  cc->hueco = A + kBsk;
  REX_STORE_U32(A + kBsk, g);
  csk = cc->csk;
  hueco = cc->hueco;
  csk_prev = cc->prev;
  REXLOG_INFO("dbz3 anim: #CSK del cuerpo 0x{:08X} (0x{:08X}): {} codigos de B3 + IW en el hueco 0 (antes 0x{:08X}); "
              "alias que ya existian en B3: {} (el primero 0x{:X})",
              A, acm, nO, O, colisiones, colision_codigo);
  return true;
}

// Cuerpo A (obligatorio) y cuerpo B (opcional: si no tiene modelo/#ACM valido se deja como esta).
bool Instalar(uint8_t* base, uint32_t x, uint32_t& hueco, uint32_t& hueco2, uint32_t& csk, uint32_t& csk2,
              uint32_t& csk_prev, uint32_t& csk2_prev) {
  csk2 = 0;
  hueco2 = 0;
  csk_prev = 0;
  csk2_prev = 0;
  if (!InstalarCuerpo(base, x + kAnimObj, csk, hueco, csk_prev)) return false;
  g_csk_principal = csk;
  const uint32_t mA = REX_LOAD_U32(x + kAnimObj + kModelo);
  g_acm_principal = (mA >= 0x10000000u && mA < 0xF0000000u) ? REX_LOAD_U32(mA + kAlmacenes) : 0;
  InstalarCuerpo(base, x + kAnimObj2, csk2, hueco2, csk2_prev);
  return true;
}

// Como el propio juego (cinematica.cpp Arrancar): fin de la accion, codigo en X+2928 y sub_82114FB8(X).
void Arrancar(PPCContext& ctx, uint8_t* base, uint32_t x, uint32_t codigo, bool guardia) {
  PPCContext c = ctx;
  if (guardia) {
    const uint16_t antes = REX_LOAD_U16(x + kAccion);
    REX_STORE_U16(x + kAccion, 0);
    REX_STORE_U16(x + kAccion + 2, antes);
    REX_STORE_U16(x + kAccion + 4, 0);
    c.r3.u64 = x;
    c.r4.u64 = 0;
    sub_82103978(c, base);
    c = ctx;
  }
  REX_STORE_U32(x + kCodigo, codigo);
  c.r3.u64 = x;
  sub_82114FB8(c, base);
  // DS103: 0x820FA4B8 no avanza el reloj si A+0x66C != 0 (pose congelada por sub_820F9970(-1)). Al arrancar una
  // animacion nuestra se quita el congelado para que el cuadro corra.
  REX_STORE_U8(x + kAnimObj + 0x66C, 0);
}

// DS100: el cuerpo B (X+0xCD0) no pasa por sub_82114FB8. El propio juego lo arranca con sub_820FA378(A2) cuando su
// codigo (A2+0x520) cambia (p. ej. sub_82108A20), asi que se hace lo mismo con el codigo de IW. Si el cuerpo B no
// se pudo preparar (sin modelo/#ACM o sin #CSK) no se toca.
void ArrancarCuerpoB(PPCContext& ctx, uint8_t* base, uint32_t x, uint32_t codigo) {
  const uint32_t A = x + kAnimObj2;
  if (!REX_LOAD_U32(A + kModelo)) return;
  REX_STORE_U32(A + kCodigoCuerpo, codigo);
  PPCContext c = ctx;
  c.r3.u64 = A;
  sub_820FA378(c, base);
  REX_STORE_U8(A + 0x66C, 0);   // DS103: sin congelado (ver Arrancar)
}

bool EnPelea(uint8_t* base) {
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  return mgr && REX_LOAD_U32(mgr + kEstado) == 4;
}
// iw_fix: el final de B3 (estados 5-8: su RING OUT, la pose del ganador, el menu) corre ahora bajo el final del minijuego
// (iw_minijuegos.cpp FinB3, iw_mini_botones.cpp): la animacion final y las poses de resultado siguen puestas en ellos.
bool EnPeleaOFin(uint8_t* base) {
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr), st = mgr ? REX_LOAD_U32(mgr + kEstado) : 0;
  return st >= 4 && st <= 8;
}

// Un luchador con algo de IW puesto. modo: scatto (fase 1-3), corredor (1) o cansado (1 = 0x05, 2 = 0x06 en bucle).
enum Modo : int { kNada = 0, kModoScatto, kModoCorredor, kModoCansado, kModoPose };
struct Pedido {
  uint32_t x;
  int modo, fase;
  bool aire;
  uint32_t codigo = 0;   // kModoPose
  bool sujetar = false;
  uint32_t tope = 0;
  uint32_t luego = 0;    // kModoPose: al acabar, este codigo en bucle (0 = a la guardia o sujeta)
};
struct Luchador {
  uint32_t x = 0, puesto = 0, hueco = 0, cuadro = 0;   // cuadro: el de la animacion propia (bits del float A+0x608)
  uint32_t hueco2 = 0, csk = 0, csk2 = 0;   // DS100: cuerpo B (hueco de su #CSK) y los #CSK puestos en cada cuerpo
  uint32_t csk_prev = 0, csk2_prev = 0;     // DS103: #CSK original de B3 en el hueco 0 de cada cuerpo (se repone)
  uint32_t puesto2 = ~0u;                   // DS100: codigo que lleva puesto el cuerpo B (para no reiniciarlo cada Tick)
  // DS103: reloj propio de cada cuerpo (el cuadro que llega al skinning). B3 lo copia de A+0x608 al estado 0 del
  // modelo en 0x820FAF40 (modelo+0x424); si el cuerpo esta congelado (A+0x66C != 0), el codigo se lo pisa su accion o
  // el reloj no anda, el gancho tras el avance repone codigo y cuadro con este reloj. Ver AjustarReloj.
  float reloj[2] = {0.0f, 0.0f};
  uint32_t cuadro_visto[2] = {~0u, ~0u};
  int64_t t_reloj[2] = {0, 0};   // DS103: ultimo cambio de cuadro (red de seguridad del reloj en Tick)
  uint32_t destino = 0;   // en transicion (kTransicion) hacia este codigo
  int t_mezcla = 0;
  int modo = kNada, fase = 0, ultima = -1;
  bool aire = false;
  uint32_t codigo = 0;
  bool sujetar = false;
  uint32_t tope = 0;
  uint32_t luego = 0;
  float cuadro_ev = -1.0f;   // 06: ultimo cuadro mirado de los eventos (lineas AP) de la animacion en curso
};
std::mutex g_mu;
std::vector<Pedido> g_pedidos;
std::atomic<bool> g_hay{false};
Luchador g_l[2];

void Pedir(const Pedido& p) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_pedidos.push_back(p);
  g_hay = true;
}

void Quitar(PPCContext& ctx, uint8_t* base, Luchador& l) {
  if (l.x && l.puesto && EnPelea(base) && (REX_LOAD_U32(l.x + kCodigo) & 0xFFF) == l.puesto)
    Arrancar(ctx, base, l.x, 0, true);
  // DS100: el cuerpo B no lo toca sub_82114FB8; si se deja con el ultimo codigo de IW, al acabar el minijuego se
  // queda congelado en esa pose. Se devuelve a su animacion 0 de B3 (reposo) y luego se sueltan los #CSK.
  if (l.csk2 && l.x && EnPelea(base)) {
    REX_STORE_U32(l.x + kAnimObj2 + kCodigoCuerpo, 0);
    PPCContext c = ctx;
    c.r3.u64 = l.x + kAnimObj2;
    sub_820FA378(c, base);
  }
  // DS103: se devuelve el #CSK original de B3 que habia en el hueco 0 (no 0: el juego necesita SUS codigos).
  if (l.hueco && l.csk && REX_LOAD_U32(l.hueco) == l.csk) REX_STORE_U32(l.hueco, l.csk_prev);
  if (l.hueco2 && l.csk2 && REX_LOAD_U32(l.hueco2) == l.csk2) REX_STORE_U32(l.hueco2, l.csk2_prev);
  if (l.modo == kModoScatto) g_vista = kApagado;
  l = Luchador{};
}

// Prueba: DBZ3_TEST_SCATTO=1 (el jugador hace el scatto mientras anda y frena al soltar), DBZ3_TEST_CORREDOR=1 (el
// segundo luchador corre en bucle), DBZ3_TEST_CANSADO=1 (el jugador, parado, hace la animacion de cansado).
void Prueba(uint8_t* base) {
  static const auto env = [](const char* n) {
    const char* v = std::getenv(n);
    return v && v[0] == '1';
  };
  static const bool scatto = env("DBZ3_TEST_SCATTO"), corredor = env("DBZ3_TEST_CORREDOR"),
                    cansado = env("DBZ3_TEST_CANSADO");
  static bool corre = false;
  if (!(scatto || corredor || cansado) || !dbz3::iwmini::Activo() || !EnPelea(base)) {
    corre = false;
    return;
  }
  const uint32_t x = REX_LOAD_U32(kMgrPtr) + kX0, accion = REX_LOAD_U16(x + kAccion);
  const bool anda = accion == 2 || accion == 3;
  const Luchador* j = nullptr;
  for (const Luchador& l : g_l)
    if (l.x == x) j = &l;
  const int modo = j ? j->modo : kNada;
  if (scatto && anda && modo == kNada) Scatto(x, kInicio);
  if (scatto && !anda && modo == kModoScatto && j->fase != kFin) Scatto(x, kFin);
  if (cansado && !scatto && !anda && modo == kNada) Cansado(x, true);
  if (cansado && !scatto && anda && modo == kModoCansado) Cansado(x, false);
  if (corredor && !corre) {
    corre = true;
    Corredor(x + kXPaso, true);
  }
}

}  // namespace

void Scatto(uint32_t x, Fase f, bool aire) { Pedir({x, kModoScatto, int(f), aire}); }
void Corredor(uint32_t x, bool on) { Pedir({x, kModoCorredor, on ? 1 : 0, false}); }
void Bucle(uint32_t x, uint32_t codigo) { Pedir({x, kModoCorredor, codigo ? 1 : 0, false, codigo}); }
void Cansado(uint32_t x, bool on) { Pedir({x, kModoCansado, on ? 1 : 0, false}); }
void Reproducir(uint32_t x, uint32_t codigo, bool sujetar, uint32_t tope, uint32_t luego) {
  g_pose = codigo ? 1 : 0;
  Pedir({x, kModoPose, codigo ? 1 : 0, false, codigo, sujetar, tope, luego});
}
bool RaizActual(uint8_t* base, uint32_t x, float out[3]) {
  for (const Luchador& l : g_l) {
    if (l.modo == kNada || l.x != x || !l.puesto || l.puesto == kTransicion) continue;
    const Anim* a = AnimDe(l.puesto);
    if (!a) return false;
    for (size_t w = 0; w < a->sufijos.size(); ++w) {
      if (a->sufijos[w] != "WAIST" || !a->pistas[w][1]) continue;
      const uint32_t cu = REX_LOAD_U32(x + kAnimObj + 0x608);
      float f;
      std::memcpy(&f, &cu, 4);
      Muestra(base, a->pistas[w][1], false, f, out);
      return true;
    }
  }
  return false;
}
float AlturaRaiz(uint8_t* base, uint32_t x) {
  float v[3];
  return RaizActual(base, x, v) ? v[1] : -1.0f;
}
uint32_t CodigoQte(uint32_t cmn, uint32_t codigo) {
  for (const QteAnim& q : kQte)
    if (q.cmn == cmn && q.codigo == codigo) return AliasQte(cmn, codigo);
  return 0;
}
// 06: true si un evento de sonido de cualquier luchador sono hace menos de kEventoSonando
// cuadros (lo consulta iw_mini_botones.cpp para no doblar el sonido con sus retardos fijos).
bool EventosSonandoReciente() { return g_ev_ticks[0].load() > 0 || g_ev_ticks[1].load() > 0; }
int EstadoPose() { return g_pose.load(); }
Fase FaseScatto() { return Fase(g_vista.load()); }

// Hilo del juego, con contexto (gancho de abajo): aplica los pedidos y hace avanzar las fases.
void Tick(PPCContext& ctx, uint8_t* base) {
  Prueba(base);
  for (int i = 0; i < 2; ++i) {   // 06: la marca de "evento sonoro reciente" se gasta por tick
    const int t = g_ev_ticks[i].load();
    if (t > 0) g_ev_ticks[i].store(t - 1);
  }
  if (g_hay.exchange(false)) {
    std::vector<Pedido> ps;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      ps.swap(g_pedidos);
    }
    for (const Pedido& p : ps) {
      Luchador* l = nullptr;
      for (Luchador& c : g_l)
        if (c.modo != kNada && c.x == p.x) l = &c;
      if (!l && p.fase == 0) continue;   // apagar algo que no esta puesto
      for (Luchador& c : g_l)
        if (!l && c.modo == kNada) l = &c;
      if (!l) continue;
      if (p.fase == 0) {
        if (l->modo == p.modo) l->fase = 0;
        continue;
      }
      if (l->modo == p.modo && (p.modo != kModoScatto || p.fase == l->ultima) &&
          ((p.modo != kModoPose && p.modo != kModoCorredor) || p.codigo == l->codigo))
        continue;   // repetir no hace nada
      l->x = p.x;
      l->modo = p.modo;
      l->fase = p.fase;
      l->ultima = p.fase;
      l->aire = p.aire;
      l->codigo = p.codigo;
      l->sujetar = p.sujetar;
      l->tope = p.tope;
      l->luego = p.luego;
    }
  }
  for (Luchador& l : g_l) {
    if (l.modo == kNada) continue;
    // seguro: fuera de un minijuego de IW o de la pelea no queda nada puesto (si P4 no lo apago al acabar)
    if (!dbz3::iwmini::Activo() || !EnPeleaOFin(base)) l.fase = 0;
    if (l.fase == 0) {
      Quitar(ctx, base, l);
      continue;
    }
    uint32_t hueco = 0, hueco2 = 0, csk = 0, csk2 = 0, csk_prev = 0, csk2_prev = 0;
    if (!Leer(base) || !Instalar(base, l.x, hueco, hueco2, csk, csk2, csk_prev, csk2_prev) ||
        ((l.modo == kModoPose || (l.modo == kModoCorredor && l.codigo && l.codigo != kCorredor)) &&
         std::none_of(g_ex.begin(), g_ex.end(), [&](const Anim& a) { return a.codigo == l.codigo; }))) {
      if (l.modo == kModoPose) g_pose = 2;   // sin esa animacion: el minijuego sigue sin ella
      l.fase = 0;
      continue;
    }
    if (hueco && hueco != l.hueco) {
      if (l.hueco && REX_LOAD_U32(l.hueco) == l.csk) REX_STORE_U32(l.hueco, l.csk_prev);
      l.hueco = hueco;
    }
    l.csk = csk;
    l.csk_prev = csk_prev;
    if (csk2 != l.csk2) {
      if (l.hueco2 && REX_LOAD_U32(l.hueco2) == l.csk2) REX_STORE_U32(l.hueco2, l.csk2_prev);
      l.hueco2 = hueco2;
      l.csk2 = csk2;
      l.csk2_prev = csk2_prev;
      l.puesto2 = ~0u;   // #CSK/cuerpo nuevo: hay que arrancarle la animacion
    } else if (hueco2 && hueco2 != l.hueco2) {
      l.hueco2 = hueco2;
      l.csk2_prev = csk2_prev;
    }
    const uint32_t codigo = l.modo == kModoScatto     ? kPrimero + uint32_t(l.fase - 1) + (l.aire ? 3 : 0)
                            : l.modo == kModoCorredor ? (l.codigo ? l.codigo : kCorredor)
                            : l.modo == kModoPose     ? l.codigo
                                                      : (l.fase == 1 ? 0x805u : 0x806u);
    // DS100: el cuerpo B va sin mezcla (su #ACM no tiene el 0x7FE generado). Se arranca una vez por codigo; en la
    // transicion se pone ya el destino y al acabar la mezcla no se reinicia.
    auto armaB = [&](uint32_t cod) {
      if (!l.csk2 || l.puesto2 == cod) return;
      ArrancarCuerpoB(ctx, base, l.x, cod);
      l.puesto2 = cod;
      l.reloj[1] = 0.0f;   // DS103: animacion nueva en el cuerpo B, desde su cuadro 0
      l.cuadro_visto[1] = ~0u;
    };
    const uint32_t ahora = REX_LOAD_U32(l.x + kCodigo) & 0xFFF;
    if (static const bool traza = std::getenv("DBZ3_ANIM_TRAZA") != nullptr; traza && ahora != codigo && ahora != kTransicion) {
      static int n = 0;   // PRUEBA: quien pisa la animacion de IW (codigo de B3 que se ve en vez del nuestro)
      if (n++ < 200)
        REXLOG_INFO("dbz3 anim: traza: luchador 0x{:08X} ve 0x{:X} (puesto 0x{:X}, quiere 0x{:X}, modo {})", l.x, ahora,
                    l.puesto, codigo, l.modo);
    }
    if (ahora == kTransicion && l.destino == codigo) {   // mezcla en curso hacia la que toca
      // por su propio cuadro (A+0x608): Tick va una vez por mando (4 por cuadro, sub_8208FD58) y contar Ticks la
      // dejaba en 2 cuadros (registro: 7FE dura 33 ms; auditoria10/D); el tope de Ticks por si el cuadro no avanza
      const uint32_t cm = REX_LOAD_U32(l.x + kAnimObj + 0x608);
      float fm;
      std::memcpy(&fm, &cm, 4);
      if (fm + 0.5f < float(g_mezcla) && ++l.t_mezcla < 4 * int(g_mezcla) + 8) continue;
      l.destino = 0;
      Arrancar(ctx, base, l.x, codigo, false);
      armaB(codigo);
      l.cuadro = 0;
      if (const Anim* b = AnimDe(codigo); b && b->cuadros > g_mezcla) {   // B ya avanzo g_mezcla cuadros en la mezcla
        const float sigue = float(g_mezcla);
        uint32_t u;
        std::memcpy(&u, &sigue, 4);
        REX_STORE_U32(l.x + kAnimObj + 0x608, u);
      }
      l.puesto = codigo;
      l.reloj[0] = float(g_mezcla);   // DS103: el reloj propio sigue donde acabo la mezcla
      l.cuadro_ev = float(g_mezcla);   // 06: B ya avanzo g_mezcla cuadros en la mezcla: sin repetir sus eventos
      continue;
    }
    // de una animacion de IW (la que se ve) a otra: primero la transicion (no en el scatto, que va por cuadros)
    if (ahora != codigo && ahora == (l.puesto & 0xFFF) && l.modo != kModoScatto && !std::getenv("DBZ3_SIN_MEZCLA")) {
      if (CorteMezcla(ahora, codigo)) {   // 06: IW cambia esta animacion sin rampa (ver CorteMezcla)
        Arrancar(ctx, base, l.x, codigo, false);
        armaB(codigo);
        l.cuadro = 0;
        l.reloj[0] = 0.0f;
        l.puesto = codigo;
        l.cuadro_ev = -1.0f;
        continue;
      }
      const uint32_t cu = REX_LOAD_U32(l.x + kAnimObj + 0x608);
      float f;
      std::memcpy(&f, &cu, 4);
      if (PrepararTransicion(base, l.puesto, f, codigo)) {
        Arrancar(ctx, base, l.x, kTransicion, false);
        // el cuerpo B no tiene la transicion generada (vive en el #ACM del A): va directo a la animacion destino.
        armaB(codigo);
        l.destino = codigo;
        l.t_mezcla = 0;
        l.puesto = kTransicion;
        continue;
      }
    }
    if (ahora != codigo) {
      const bool nueva = l.puesto != codigo;   // DS103: animacion nueva (no un re-arme del juego)
      Arrancar(ctx, base, l.x, codigo, false);
      armaB(codigo);
      // si el juego la piso (la IA de P4 reentra en andar cada pocos cuadros), sigue por el cuadro en que iba
      if (!nueva) REX_STORE_U32(l.x + kAnimObj + 0x608, l.cuadro);
      else l.cuadro = 0;
      if (nueva) {
        l.reloj[0] = 0.0f;
        l.cuadro_ev = -1.0f;   // 06: animacion nueva: los eventos, desde su cuadro 0
      }
      l.puesto = codigo;
      if (l.modo == kModoScatto) g_vista = l.fase;
      static int avisos = 0;
      if (avisos++ < 40)
        REXLOG_INFO("dbz3 anim: luchador 0x{:08X} modo {} fase {} (codigo 0x{:X})", l.x, l.modo, l.fase, codigo);
      continue;
    }
    // acabada: ha dado la vuelta (A+0x618) o esta en su ultimo cuadro (modo que la sujeta, A+1638 == 2)
    const uint32_t cu = REX_LOAD_U32(l.x + kAnimObj + 0x608);
    l.cuadro = cu;
    float cuadro;
    std::memcpy(&cuadro, &cu, 4);
    // 06: eventos por cuadro de las lineas AP del #BSK (tipo 1/2/7 sonido, 3 voz): se disparan
    // al cruzar su cuadro, como IW en Char_HandleAnimEvent/Animations2 (recorrido cuadro viejo ->
    // cuadro nuevo; si la animacion da la vuelta, desde el principio). El respaldo de los
    // minijuegos (retardos fijos) se calla mientras suene uno reciente (EventosSonandoReciente).
    if (const Anim* ae = AnimDe(codigo); ae && !ae->eventos.empty()) {
      if (l.cuadro_ev > cuadro + 0.5f) l.cuadro_ev = -1.0f;   // volvio al cuadro 0
      for (const Anim::Evento& ev : ae->eventos)
        if (ev.cuadro > l.cuadro_ev && ev.cuadro <= cuadro + 0.5f) DispararEvento(ctx, base, l.x, codigo, ev);
    }
    l.cuadro_ev = cuadro;
    const float total = float(int32_t(REX_LOAD_U32(l.x + kAnimObj + 0x60C)));
    if (l.modo == kModoPose && l.tope > 0 && cuadro >= float(l.tope)) {   // quieta en su cuadro tope
      const float u = float(l.tope);
      uint32_t b;
      std::memcpy(&b, &u, 4);
      REX_STORE_U32(l.x + kAnimObj + 0x608, b);
      continue;
    }
    if (REX_LOAD_U32(l.x + kAnimObj + kVueltas) == 0 && cuadro + 1.0f < total) continue;
    if (l.modo == kModoPose) {   // R16: acabada; sujeta se queda en su penultimo cuadro, si no a la guardia
      g_pose = 2;
      if (l.luego) {   // R14: de la pose al reposo de IW en bucle (con la mezcla de 8 cuadros)
        l.modo = kModoCorredor;
        l.codigo = l.luego;
        l.luego = 0;
        l.sujetar = false;
        l.tope = 0;
        continue;
      }
      if (!l.sujetar) {
        Quitar(ctx, base, l);
      } else {
        const float u = std::max(0.0f, total - 2.0f);
        uint32_t b;
        std::memcpy(&b, &u, 4);
        REX_STORE_U32(l.x + kAnimObj + 0x608, b);
      }
      continue;
    }
    if (l.modo == kModoScatto && l.fase == kInicio) l.fase = kBucle;
    else if (l.modo == kModoScatto && l.fase == kFin) Quitar(ctx, base, l);
    else if (l.modo == kModoCansado && l.fase == 1) l.fase = 2;
  }
  // DS103: red de seguridad del reloj. AjustarReloj va colgado del avance del motor (US); en EU (sin ese gancho) o
  // si el motor no llama al avance para este cuerpo, aqui se lleva el reloj de los dos cuerpos: si el cuadro no ha
  // cambiado en ~1 cuadro de juego (15 ms), se suma uno con nuestro reloj. Si el motor avanza, se adopta el suyo
  // y no se toca (asi no se avanza dos veces).
  if (!EnPeleaOFin(base)) return;
  const int64_t ahora_ms = dbz3::RelojMs();
  for (Luchador& l : g_l) {
    if (l.modo == kNada || !l.x) continue;
    if (l.modo == kModoPose && (l.sujetar || l.tope)) continue;   // pose sujeta: la manda Tick
    for (int c = 0; c < 2; ++c) {
      const uint32_t A = l.x + (c ? kAnimObj2 : kAnimObj);
      if (c && l.puesto2 == ~0u) continue;   // cuerpo B aun sin codigo asignado
      const uint32_t des = c ? l.puesto2 : (l.puesto & 0xFFF);
      if (!des || des == kTransicion) continue;
      bool repuesto = false;
      if ((REX_LOAD_U32(A + kCodigoCuerpo) & 0xFFF) != des) {   // el motor puso otra (0 = guardia): devolver la nuestra
        REX_STORE_U32(A + kCodigoCuerpo, des);
        PPCContext c = ctx;
        c.r3.u64 = A;
        sub_820FA378(c, base);
        REX_STORE_U8(A + 0x66C, 0);
        repuesto = true;   // el arranque deja el cuadro a 0: no se adopta, sigue NUESTRO reloj
      }
      const uint32_t total = REX_LOAD_U32(A + 0x60C);
      if (int32_t(total) <= 0) continue;
      const uint32_t u = REX_LOAD_U32(A + 0x608);
      if (!repuesto && u != l.cuadro_visto[c]) {   // el motor (o AjustarReloj) lo movio: se adopta
        l.cuadro_visto[c] = u;
        std::memcpy(&l.reloj[c], &u, 4);
        l.t_reloj[c] = ahora_ms;
        continue;
      }
      if (!repuesto && ahora_ms - l.t_reloj[c] < 15) continue;
      l.reloj[c] += 1.0f;
      while (l.reloj[c] >= float(total)) l.reloj[c] -= float(total);
      if (!(l.reloj[c] >= 0.0f)) l.reloj[c] = 0.0f;
      uint32_t w;
      std::memcpy(&w, &l.reloj[c], 4);
      REX_STORE_U32(A + 0x608, w);
      // DS105: A+0x63C es el reloj del TERCER clip (A+0x638), no "el cuadro que se dibuja"; no se toca.
      REX_STORE_U8(A + 0x66C, 0);
      l.cuadro_visto[c] = w;
      l.t_reloj[c] = ahora_ms;
    }
  }
}

// DS103: gancho DESPUES del avance del reloj del motor (sub_820FA4B8(A, mascara), el unico sitio que suma a
// A+0x608; si A+0x66C != 0 no avanza). Con el cuerpo B
// instalado y el codigo correcto el motor avanza solo, pero en la prueba real el codigo estaba en A y la pose no
// cambiaba: si su accion vuelve a poner otro codigo (el 0 de guardia) o congela la pose, aqui se repone el
// codigo de IW y se lleva el cuadro con NUESTRO reloj (uno por cuadro de juego), de modo que el skinning tenga
// siempre un cuadro nuevo aunque el motor hubiera dejado la animacion a 0. `antes` = A+0x608 antes del avance.
void AjustarReloj(PPCContext& ctx, uint8_t* base, uint32_t A, float antes) {
  if (!EnPeleaOFin(base)) return;
  for (Luchador& l : g_l) {
    if (l.modo == kNada || !l.x) continue;
    const int cuerpo = A == l.x + kAnimObj2 ? 1 : (A == l.x + kAnimObj ? 0 : -1);
    if (cuerpo < 0) continue;
    if (l.modo == kModoPose && (l.sujetar || l.tope)) return;   // pose sujeta: el cuadro lo manda Tick
    if (cuerpo && l.puesto2 == ~0u) return;                     // cuerpo B aun sin codigo asignado
    const uint32_t des = cuerpo ? l.puesto2 : (l.puesto & 0xFFF);
    if (!des || des == kTransicion) return;
    const uint32_t total = REX_LOAD_U32(A + 0x60C);
    if (int32_t(total) <= 0) return;   // sin clip resuelto
    if ((REX_LOAD_U32(A + kCodigoCuerpo) & 0xFFF) != des) {   // su accion puso otra (normalmente 0)
      REX_STORE_U32(A + kCodigoCuerpo, des);
      PPCContext c = ctx;
      c.r3.u64 = A;
      sub_820FA378(c, base);   // resuelve el #CSK a nuestro clip (no pasa por la accion del luchador)
      REX_STORE_U8(A + 0x66C, 0);
      antes = -1.0f;           // el arranque deja el cuadro a 0: no cuenta como avance
    }
    float ahora;
    const uint32_t ua = REX_LOAD_U32(A + 0x608);
    std::memcpy(&ahora, &ua, 4);
    float& reloj = l.reloj[cuerpo];
    // Avance real del motor (hacia delante): se adopta. Si no avanzo (congelado, codigo pisado o vuelta a 0), se
    // lleva con nuestro reloj: +1 por llamada (una por cuerpo y cuadro de juego).
    if (ahora > antes + 0.5f) reloj = ahora;
    else {
      reloj += 1.0f;
      while (reloj >= float(total)) reloj -= float(total);
    }
    if (!(reloj >= 0.0f) || reloj >= float(total)) reloj = 0.0f;
    uint32_t ur;
    std::memcpy(&ur, &reloj, 4);
    REX_STORE_U32(A + 0x608, ur);   // el cuadro que el modelo copia a su estado 0 (0x820FAF40 -> modelo+0x424)
    // DS105: NO se escribe A+0x63C: es el reloj del TERCER clip (A+0x638), no el cuadro que se dibuja. El que llega
    // al skinning es A+0x608 -> modelo+0x424, que el modelo evalua en 0x8208DC60.
    REX_STORE_U8(A + 0x66C, 0);
    l.cuadro_visto[cuerpo] = ur;
    return;
  }
}

// DS103: lectura del clip y del reloj que el motor tiene DE VERDAD en cada cuerpo (para la traza de 0,5 s del
// minijuego). Ver EstadoCuerpo en anim_iw.h.
bool LeerEstadoCuerpo(uint8_t* base, uint32_t x, int cuerpo, EstadoCuerpo& e) {
  e = EstadoCuerpo{};
  if (!x) return false;
  const uint32_t A = x + (cuerpo ? kAnimObj2 : kAnimObj);
  e.objeto = A;
  e.modelo = REX_LOAD_U32(A + kModelo);
  e.hay = e.modelo >= 0x10000000u && e.modelo < 0xF0000000u;
  if (e.hay) {
    const uint32_t acm = REX_LOAD_U32(e.modelo + kAlmacenes);
    if (acm >= 0x10000000u && acm < 0xF0000000u) e.acm = acm;
  }
  for (uint32_t i = 0; i < 3; ++i) e.csk[i] = REX_LOAD_U32(A + kBsk + 4 * i);
  e.codigo = REX_LOAD_U32(A + kCodigoCuerpo) & 0xFFF;
  e.clip = REX_LOAD_U32(A + 0x604);
  e.cuadros = REX_LOAD_U32(A + 0x60C);
  e.vueltas = REX_LOAD_U32(A + kVueltas);
  e.bloque = REX_LOAD_U32(A + 0x524);
  e.congelado = REX_LOAD_U8(A + 0x66C);
  const uint32_t cu = REX_LOAD_U32(A + 0x608), di = REX_LOAD_U32(A + 0x63C);
  std::memcpy(&e.cuadro, &cu, 4);
  std::memcpy(&e.reloj, &di, 4);
  e.oculto1 = REX_LOAD_U8(x + (cuerpo ? 4924u : 3260u));
  e.oculto2 = REX_LOAD_U8(x + (cuerpo ? 4925u : 3261u));
  return e.hay;
}

// =====================================================================================================================
// DS67 - Guion y efectos de IW (ver anim_iw.h). Direcciones del decomp EU 0x1C11A0-0x1C1570, 0x1ED3C0-0x1ED7F0 y
// 0x25FA90. La VM de guion que los llama vive en otros ficheros; aqui queda la logica exacta y compilable.
namespace efecto {
namespace {

constexpr uint32_t kPasoSlot = 0x2140;       // tamano de una ranura de efecto
constexpr uint32_t kDeltaGlobal = 0x49C7DC;  // fRam0049c7dc: tiempo de un cuadro
constexpr uint32_t kRelojGlobal = 0x49CA70;  // iRam0049ca70: bloque de reloj (pausa + escala), si existe

float LeeF32(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void PonF32(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}
bool Pausado(uint8_t* base) {
  const uint32_t reloj = REX_LOAD_U32(kRelojGlobal);
  return reloj && REX_LOAD_U32(reloj + 0x8B7C) != 0;
}
// fVar5 del original: fRam0049c7dc o, si hay bloque de reloj, su +0x8b80 (escala de tiempo).
float EscalaCuadro(uint8_t* base) {
  const uint32_t reloj = REX_LOAD_U32(kRelojGlobal);
  return reloj ? LeeF32(base, reloj + 0x8B80) : LeeF32(base, kDeltaGlobal);
}
// La ranura del original es `(char)`: con signo.
int Slot(uint32_t slot) { return int(int8_t(uint8_t(slot))); }

}  // namespace

// EU 0x1ED730 Effect_SetValueBRelative
void ValorBRelativo(uint8_t* base, int slot, float valor) {
  const uint32_t off = uint32_t(slot) * kPasoSlot;
  PonF32(base, off + 0x4DE1E4, valor - LeeF32(base, off + 0x4DE1F0));
}

// EU 0x1ED7F0 Effect_SetEntryBFromTable
void EntradaBDeTabla(uint8_t* base, int slot, uint32_t id) {
  const uint32_t off = uint32_t(slot) * kPasoSlot;
  const uint32_t entrada = id | 0x10000u;
  REX_STORE_U32(off + 0x4DE1F4, entrada);
  const uint32_t banco_idx = (entrada >> 16) - 1u;   // ((id|0x10000)>>16)-1
  const uint32_t banco = REX_LOAD_U32(off + 0x4DC184 + 4u * banco_idx);
  const uint32_t tabla = banco ? REX_LOAD_U32(banco + 0x14) : 0;
  if (!tabla) return;
  const uint32_t fila = tabla + (id & 0xFFFFu) * 0x10u;
  PonF32(base, off + 0x4DE1EC, float(int32_t(REX_LOAD_U32(fila + 8))));
}

// EU 0x1ED3C0 Effect_AdvanceAnimFrame (contador; el mapeo de huesos FUN_0013bdf0 queda como [H], lo hace el motor de
// efectos del escenario). Devuelve true al completar una vuelta.
bool AvanzarCuadroB(uint8_t* base, int slot, bool bucle) {
  const uint32_t off = uint32_t(slot) * kPasoSlot;
  const float fin = LeeF32(base, off + 0x4DE1EC) - 1.0f;
  if (Pausado(base)) return false;
  float v;
  if ((REX_LOAD_U32(off + 0x4DE1CC) & 0x10u) == 0)
    v = LeeF32(base, off + 0x4DE1F0) * EscalaCuadro(base) + LeeF32(base, off + 0x4DE1E4) + 0.0f;
  else
    v = LeeF32(base, off + 0x4DE1E4) + LeeF32(base, off + 0x4DE1F0);
  if (v < 0.0f) v = 0.0f;
  bool listo = false;
  if (fin < v) {
    v = bucle ? v - fin : fin;
    listo = true;
  }
  PonF32(base, off + 0x4DE1E4, v);
  return listo;
}

// EU 0x1C11A0 ScriptCmd_PlayAnimTimed
int ComandoAnimTemporizado(GuionAnim& g, uint8_t* base, int32_t cuadros, uint32_t entrada, int slot) {
  g.slot = Slot(uint32_t(slot));
  if (g.slot < 0) return 3;
  g.temporizador = -EscalaCuadro(base);
  g.cuadros = float(cuadros);
  EntradaBDeTabla(base, g.slot, entrada);
  ValorBRelativo(base, g.slot, 0.0f);
  if (Pausado(base)) return 3;
  if (g.cuadros == -1.0f) {
    AvanzarCuadroB(base, g.slot, true);
  } else {
    g.temporizador += EscalaCuadro(base);
    if (g.cuadros <= g.temporizador) return 1;   // el original pone a 0 la espera del ctx (+0x10)
    AvanzarCuadroB(base, g.slot, true);
  }
  return 3;
}

// EU 0x1C1100 ScriptWait_AnimFramesTimed
int EsperaAnimTemporizada(GuionAnim& g, uint8_t* base) {
  if (Pausado(base)) return 3;
  if (g.cuadros == -1.0f) {
    AvanzarCuadroB(base, g.slot, true);
    return 3;
  }
  g.temporizador += EscalaCuadro(base);
  if (g.cuadros <= g.temporizador) return 1;
  AvanzarCuadroB(base, g.slot, true);
  return 3;
}

// EU 0x1C1320 ScriptCmd_PlayAnimLoops
int ComandoAnimVueltas(GuionAnim& g, uint8_t* base, int32_t vueltas, uint32_t entrada, int slot) {
  g.slot = Slot(uint32_t(slot));
  if (g.slot < 0) return 3;
  g.vueltas = int(int8_t(uint8_t(vueltas)));
  EntradaBDeTabla(base, g.slot, entrada);
  ValorBRelativo(base, g.slot, 0.0f);
  return EsperaAnimVueltas(g, base);
}

// EU 0x1C12B0 ScriptWait_AnimLoops
int EsperaAnimVueltas(GuionAnim& g, uint8_t* base) {
  if (!AvanzarCuadroB(base, g.slot, true)) return 3;
  if (g.vueltas < 1) return 3;
  --g.vueltas;
  return g.vueltas != 0 ? 3 : 1;
}

// EU 0x1C1570 ScriptWait_ModelAnimCount
int EsperaModeloVueltas(int& vueltas, bool (*avanzar)(void* user, int slot), void* user, int slot) {
  if (!avanzar || !avanzar(user, slot)) return 3;
  if (vueltas < 1) return 3;
  --vueltas;
  return vueltas != 0 ? 3 : 1;
}

// EU 0x25FA90 MouthAnimations
void CopiarBoca(uint8_t* base, uint32_t hablante, uint32_t personaje, uint32_t invertir) {
  if (!hablante || !personaje) return;
  uint32_t mod_a, mod_b, idx_a, idx_b;
  if (!invertir) {   // hablante -> personaje
    mod_a = REX_LOAD_U32(hablante + 0x1C);
    mod_b = REX_LOAD_U32(personaje + 0x4C);
    idx_a = hablante;
    idx_b = personaje + 0x30;
  } else {           // personaje -> hablante
    mod_a = REX_LOAD_U32(personaje + 0x4C);
    mod_b = REX_LOAD_U32(hablante + 0x1C);
    idx_a = personaje + 0x30;
    idx_b = hablante;
  }
  const uint32_t huesos_a = mod_a ? REX_LOAD_U32(mod_a + 0x45C) : 0;
  const uint32_t huesos_b = mod_b ? REX_LOAD_U32(mod_b + 0x45C) : 0;
  if (!huesos_a || !huesos_b) return;
  for (uint32_t i = 0; i < 9; ++i) {
    const int16_t ia = int16_t(REX_LOAD_U16(idx_a + 2 * i));
    const int16_t ib = int16_t(REX_LOAD_U16(idx_b + 2 * i));
    if (ia < 0 || ib < 0) continue;
    const uint32_t src = huesos_a + uint32_t(ia) * 0x80;
    const uint32_t dst = huesos_b + uint32_t(ib) * 0x80;
    for (uint32_t k = 0; k < 0x80; k += 4) REX_STORE_U32(dst + k, REX_LOAD_U32(src + k));
  }
}

}  // namespace efecto

// =====================================================================================================================
// DS79 - Las 8 `falta` de MAPAS_CODIGO/INVENTARIO_ANIMACIONES.md (decomp EU 0x1E3460, 0x2F7E00, 0x262A80, 0x33A8C0,
// 0x339DD0, 0x2A8850, 0x2603B0 y 0x2659B0). Son primitivas de la logica del original: en el port todavia no tienen
// llamante (no carga personajes de escenario IW, ni el visor de IW, ni la tela PS2), asi que no cambian nada visible
// por si solas. Las operaciones que en el original hace el motor (paso de material/modelo, pases de sombra, sonido de
// menu, tabla de anclajes, VU0 de la tela) entran como callbacks o se sustituyen, igual que en DS67. Cada valor y
// rama sale del decomp; lo que queda pendiente de conexion va como [VERIFICAR] en el informe DS79.
namespace puente {
namespace {

float LeerF32(uint8_t* base, uint32_t a) {
  (void)base;
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

}  // namespace

using Accion = void (*)(uint8_t* base, uint32_t p);

// EU 0x1E3460 Stage_UpdateCharacterPose: la instancia de escenario guarda en +0x10 la estructura de animacion; si
// su +0xC no es 0 avanza el material, y si su +8 (modelo) no es 0 avanza el modelo y aplica la pose de huesos
// (**modelo). Callbacks = Anim_StepMaterialAnim / Anim_StepModelAnim / LLEGROT_RARMROT_LRARMROT_Poses_ del motor.
void ActualizarPosePersonajeEscenario(uint8_t* base, uint32_t inst, Accion pasoMaterial, Accion pasoModelo,
                                      Accion poses) {
  if (!inst) return;
  const uint32_t i = REX_LOAD_U32(inst + 0x10);
  if (!i) return;
  if (REX_LOAD_U32(i + 0xC) != 0 && pasoMaterial) pasoMaterial(base, i);
  const uint32_t modelo = REX_LOAD_U32(i + 8);
  if (modelo) {
    if (pasoModelo) pasoModelo(base, i);
    if (poses) poses(base, REX_LOAD_U32(modelo));
  }
}

using Fade = void (*)(uint8_t* base, int a, int b);
using Pop = void (*)(uint8_t* base, int a, int b, uint32_t c, int d);

// EU 0x2F7E00 Audio_MenuFadeAndPop: con fRam005c54e4 > 0 hace el fundido (MenuShowUp_FadeSound(2,0)) y el sonido
// del menu (MenuPopUpSound(2,0,param,0)). El port usa el sonido de B3; los callbacks son ese motor.
void SonidoMenuFundido(uint8_t* base, uint32_t param, Fade fade, Pop pop) {
  if (0.0f < LeerF32(base, 0x5C54E4)) {
    if (fade) fade(base, 2, 0);
    if (pop) pop(base, 2, 0, param, 0);
  }
}

using Shading = void (*)(uint8_t* base, int a, int b);
using Pase = void (*)(uint8_t* base, uint32_t modelo, uint32_t param);

// EU 0x262A80 Shadow_RenderModel: GeneralShadingRender_(0,1); Shadow_RenderToTexture_(modelo);
// Shadow_RenderPassWithCallback_(modelo,param); GeneralShadingRender_(0,0). Si falta cualquiera de los dos punteros
// no hace nada (el original comprueba los dos al entrar).
void SombraModelo(uint8_t* base, uint32_t modelo, uint32_t param, Shading shading, Accion aTextura, Pase pase) {
  if (!modelo || !param) return;
  if (shading) shading(base, 0, 1);
  if (aTextura) aTextura(base, modelo);
  if (pase) pase(base, modelo, param);
  if (shading) shading(base, 0, 0);
}

using Anclaje = uint32_t (*)(uint8_t* base, uint32_t id);

// EU 0x339DD0 Viewer_ResolveAttachPointer: iRam0049ce28 == 1: ids 8/7/0 -> iRam0049ce38 + 0x40/0x20/0 y el 8 pone
// el bit 4 en *flags. == 0: salta a la tabla 0x49A380[id] si id < 0x12 (aqui, callback `tabla`; la tabla no esta
// volcada en el decomp). Cualquier otro valor del modo devuelve 0.
uint32_t ResolverAnclajeVisor(uint8_t* base, uint32_t id, uint32_t* flags, Anclaje tabla) {
  const uint32_t modo = REX_LOAD_U32(0x49CE28);
  if (modo == 1) {
    if (id == 8) {
      if (flags) *flags |= 4;
      return REX_LOAD_U32(0x49CE38) + 0x40;
    }
    if (id == 7) return REX_LOAD_U32(0x49CE38) + 0x20;
    if (id == 0) return REX_LOAD_U32(0x49CE38);
    return 0;
  }
  if (modo == 0 && id < 0x12 && tabla) return tabla(base, id);
  return 0;
}

// EU 0x33A8C0 Viewer_ClearEffectHoldAt: resuelve el anclaje; si el slot +0x18 no es 0 pone su temporizador a
// infinito (Effect_SetTimerInfinite escribe -1 en +0x48) y lo limpia.
void LimpiarAnclajeVisor(uint8_t* base, uint32_t id, Anclaje tabla) {
  uint32_t flags = 0;
  const uint32_t slot = ResolverAnclajeVisor(base, id, &flags, tabla);
  if (slot && REX_LOAD_U32(slot + 0x18) != 0) {
    REX_STORE_U32(slot + 0x48, 0xFFFFFFFFu);
    REX_STORE_U32(slot + 0x18, 0);
  }
}

// EU 0x2A8850 Char_ConsumePendingHit: con el work del minijuego (uRam0049cbc0 + indice*0x260, indice en
// param+0x1380) libre (+0x180 == 0), lee y pone a 0 el contador de golpes pendientes (+0x1A8) solo para el tipo de
// estado uRam0049cba4 +4 = 1, 0x10, 0xE, 10, 2 o 0. Otro tipo devuelve 0 sin tocar el contador.
int ConsumirGolpePendiente(uint8_t* base, uint32_t param) {
  (void)base;
  const uint32_t estado = REX_LOAD_U32(0x49CBA4);
  const uint32_t objetos = REX_LOAD_U32(0x49CBC0);
  if (!estado || !objetos || !param) return 0;
  const uint32_t work = objetos + REX_LOAD_U32(param + 0x1380) * 0x260;
  if (REX_LOAD_U32(work + 0x180) != 0) return 0;
  const int32_t tipo = int32_t(REX_LOAD_U32(estado + 4));
  if (tipo != 1 && tipo != 0x10 && tipo != 0xE && tipo != 10 && tipo != 2 && tipo != 0) return 0;
  const int32_t n = int32_t(REX_LOAD_U32(work + 0x1A8));
  if (n <= 0) return 0;
  REX_STORE_U32(work + 0x1A8, 0);
  return n;
}

}  // namespace puente

}  // namespace dbz3::anim

// =====================================================================================================================
// DS37 - Sprites 2D de Infinite World (#AMA). Ver anim_iw.h. Traduccion de iw_sles55347.c:
//   formato y arbol: ANIMACIONES.md §2.1-§2.4;  muestreadores: §2.3;  entrada de dibujo: §2.5;  HUD: §2.8.
// Cada funcion lleva encima su direccion y nombre en el decompilado europeo (asi el inventario la cuenta).
namespace dbz3::anim::ama {
namespace {

constexpr size_t kClave = 0x20;   // tamano de una clave de cualquier canal (8 floats / 2 vec4)

float Bits(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
// Offsets del #AMA: son relativos al principio del fichero en todos los #AMA medidos (pasarela_iw.cpp lee igual).
struct Lector {
  const uint8_t* d = nullptr;
  size_t n = 0;
  bool U16(size_t o, uint16_t& v) const {
    if (o + 2 > n) return false;
    v = uint16_t(d[o] | (d[o + 1] << 8));
    return true;
  }
  bool U32(size_t o, uint32_t& v) const {
    uint16_t a, b;
    if (!U16(o, a) || !U16(o + 2, b)) return false;
    v = uint32_t(a) | (uint32_t(b) << 16);
    return true;
  }
  bool F32(size_t o, float& v) const {
    uint32_t u;
    if (!U32(o, u)) return false;
    v = Bits(u);
    return true;
  }
};

}  // namespace

uint16_t Canal::U16(size_t k, size_t off) const {
  const size_t o = k * kClave + off;
  return o + 2 <= claves.size() ? uint16_t(claves[o] | (claves[o + 1] << 8)) : 0;
}
uint32_t Canal::U32(size_t k, size_t off) const {
  return uint32_t(U16(k, off)) | (uint32_t(U16(k, off + 2)) << 16);
}
uint8_t Canal::U8(size_t k, size_t off) const {
  const size_t o = k * kClave + off;
  return o < claves.size() ? claves[o] : 0;
}
float Canal::F32(size_t k, size_t off) const { return Bits(U32(k, off)); }

// Un canal: n/duracion en su cabecera, tabla de tiempos de 4 B por clave y claves de 0x20 B (IW:151630-151781).
static void LeerCanal(const Lector& l, size_t nOff, size_t durOff, size_t tOff, size_t kOff, bool bucle, Canal& c) {
  uint32_t nt = 0, nk = 0;
  uint16_t nc = 0, dur = 0;
  c.bucle = bucle;
  if (!l.U16(nOff, nc) || !l.U16(durOff, dur) || !l.U32(tOff, nt) || !l.U32(kOff, nk)) return;
  c.dur = dur;
  // DS57 (IW:151650/151714/151892...): el original da la pista por vacia cuando el puntero a
  // la tabla de tiempos es nulo (lo comprueba antes de leer nada), no solo cuando n = 0. Aqui
  // se deja n = 0 en ese caso: si no, los muestreadores indexarian `tiempos` vacio.
  if (nc == 0 || nc > 4096 || !nt || !nk) return;
  if (size_t(nt) + 4 * size_t(nc) > l.n || size_t(nk) + kClave * size_t(nc) > l.n) return;
  c.n = nc;
  c.tiempos.resize(nc);
  for (uint32_t i = 0; i < nc; ++i) {
    uint16_t q = 0, ti = 0;
    if (!l.U16(size_t(nt) + 4 * i, q) || !l.U16(size_t(nt) + 4 * i + 2, ti)) return;
    c.tiempos[i] = {q, ti};
  }
  c.claves.assign(l.d + nk, l.d + nk + kClave * size_t(nc));
}

// Pista (IW:150910-151781): cabecera de 0x20 B con dos canales. `off` apunta a la cabecera.
static void LeerPista(const Lector& l, size_t off, Pista& p) {
  uint32_t flags = 0;
  if (!l.U32(off, flags)) return;
  LeerCanal(l, off + 4, off + 6, off + 0x10, off + 0x14, (flags & 1) != 0, p.c1);   // bit0 = bucle canal 1
  LeerCanal(l, off + 8, off + 0xA, off + 0x18, off + 0x1C, (flags & 2) != 0, p.c2);   // bit1 = bucle canal 2
}

// Pista de efectos (IW:149835-150061): cabecera de 0x30 B, tres canales de 0x20 B (escala, posicion, color).
static void LeerFx(const Lector& l, size_t off, Fx& fx) {
  uint32_t flags = 0;
  if (!l.U32(off, flags)) return;
  fx.flags = flags;
  LeerCanal(l, off + 4, off + 6, off + 0x18, off + 0x1C, (flags & 1) != 0, fx.escala);
  LeerCanal(l, off + 8, off + 0xA, off + 0x20, off + 0x24, (flags & 2) != 0, fx.pos);
  LeerCanal(l, off + 0xC, off + 0xE, off + 0x28, off + 0x2C, (flags & 4) != 0, fx.color);
}

static int LeerSegmento(Obra& o, const Lector& l, uint32_t off);

// Rellena un nodo y su descendencia (SprAnim_CreateNodeTree, IW:153937-154004). `vistos` evita ciclos/duplicados.
static int LeerNodo(Obra& o, std::map<uint32_t, int>& vistos, const Lector& l, uint32_t off, int padre, int prof) {
  if (prof > 64 || size_t(off) + 0x14 > l.n) return -1;
  auto it = vistos.find(off);
  if (it != vistos.end()) return it->second;
  const int idx = int(o.Nodos().size());
  o.Nodos().push_back({});
  vistos[off] = idx;
  o.Nodos()[size_t(idx)].padre = padre;
  uint32_t v = 0;
  // El vector puede crecer al bajar por el arbol: se vuelve a pedir el nodo por indice (nada de referencias vivas).
  if (l.U32(off + 4, v) && v) o.Nodos()[size_t(idx)].hijo = LeerNodo(o, vistos, l, v, idx, prof + 1);
  if (l.U32(off + 8, v) && v) o.Nodos()[size_t(idx)].hermano = LeerNodo(o, vistos, l, v, padre, prof + 1);
  if (l.U32(off + 0x10, v) && v) o.Nodos()[size_t(idx)].anim = LeerSegmento(o, l, v);
  return idx;
}

// Segmento y su cadena (IW:152302-152313); el rect y la duracion estan en +0x20/+0x1C.
static int LeerSegmento(Obra& o, const Lector& l, uint32_t off) {
  if (size_t(off) + 0x30 > l.n) return -1;
  auto it = o.MapaSegs().find(off);
  if (it != o.MapaSegs().end()) return it->second;
  const int idx = int(o.Segs().size());
  o.Segs().push_back({});
  o.MapaSegs()[off] = idx;
  Segmento& s = o.Segs().back();
  uint32_t v = 0;
  l.U32(off, s.flags);
  l.U32(off + 0xC, s.efectos_crudo);
  l.U32(off + 0x1C, s.duracion);
  for (int i = 0; i < 4; ++i) l.F32(off + 0x20 + 4 * size_t(i), s.rect[i]);
  // +0x04/+0x08 son PUNTEROS a las cabeceras de las pistas (medido en el #AMA 334/2 de DATA_USA: el segmento 0x6B40
  // apunta a 0x6B70/0x6C50; leerlas en linea da basura). La de efectos (+0x0C) y la de eventos (+0x10) tambien.
  if (l.U32(off + 4, v) && v) LeerPista(l, v, s.a);
  if (l.U32(off + 8, v) && v) LeerPista(l, v, s.b);
  if (s.efectos_crudo) {
    LeerFx(l, s.efectos_crudo, s.fx);
    s.tiene_fx = s.fx.escala.n || s.fx.pos.n || s.fx.color.n;
  }
  // Pista de eventos (IW:152066-152198): n +4, duracion +6, tiempos +8, claves +0xC; registro de 16 B.
  uint16_t n = 0, dur = 0;
  uint32_t t = 0, k = 0;
  uint32_t ev = 0;
  if (l.U32(off + 0x10, ev) && ev && l.U16(ev + 4, n) && l.U16(ev + 6, dur) && l.U32(ev + 8, t) && l.U32(ev + 0xC, k) &&
      n > 0 && n <= 256 && size_t(t) + 4 * size_t(n) <= l.n && size_t(k) + 16 * size_t(n) <= l.n) {
    s.eventos.resize(n);
    s.eventos_tiempos.resize(n);
    s.eventos_dur = dur;
    s.eventos_bucle = (l.U32(ev, v) && (v & 1) != 0);
    for (uint16_t i = 0; i < n; ++i) {
      uint16_t q = 0, ti = 0;
      l.U16(size_t(t) + 4 * size_t(i), q);
      l.U16(size_t(t) + 4 * size_t(i) + 2, ti);
      s.eventos_tiempos[i] = {q, ti};
      uint32_t id = 0;
      l.U32(size_t(k) + 16 * size_t(i), id);
      s.eventos[i].id = int32_t(id);
      for (int j = 0; j < 3; ++j) l.U32(size_t(k) + 16 * size_t(i) + 4 + 4 * size_t(j), s.eventos[i].arg[j]);
    }
  } else {
    s.eventos.clear();
    s.eventos_tiempos.clear();
    s.eventos_dur = 0;
    s.eventos_bucle = false;
  }
  uint32_t next = 0;
  if (l.U32(off + 0x18, next) && next) o.Segs()[size_t(idx)].siguiente = LeerSegmento(o, l, next);
  return idx;
}

bool Obra::Cargar(const uint8_t* datos, size_t tam) {
  *this = Obra{};
  if (!datos || tam < 0x20 || std::memcmp(datos, "#AMA", 4) != 0) return false;
  Lector l{datos, tam};
  uint32_t nobj = 0, tabla = 0;
  if (!l.U32(0x10, nobj) || !l.U32(0x14, tabla) || nobj == 0 || nobj > 4096) return false;
  if (size_t(tabla) + 4 * size_t(nobj) > tam) return false;
  std::map<uint32_t, int> vistos;
  Objetos().reserve(nobj);
  for (uint32_t i = 0; i < nobj; ++i) {
    uint32_t off = 0;
    l.U32(size_t(tabla) + 4 * size_t(i), off);
    Objetos().push_back(off ? LeerNodo(*this, vistos, l, off, -1, 0) : -1);
  }
  if (Objetos().empty() || Objetos()[0] < 0) return false;
  PonerRaiz(Objetos()[0]);
  PonerValida(!Nodos().empty());
  return valida_;
}

int Obra::Objeto(uint32_t i) const { return i < objetos_.size() ? objetos_[i] : -1; }

namespace {

// Busqueda sobre una tabla de tiempos (la usan las pistas y la de eventos, que tiene otra cabecera).
int BuscarEn(const std::vector<Tiempo>& tv, int n, float t, int desde) {
  if (n <= 0) return 0;
  if (t < 1.0f) return 0;
  const int64_t fi = int64_t(t);   // (int)param_1 en el original: trunca hacia cero
  int i = 0;
  if (desde < 0) {
    i = n >> 1;
    int p = n;
    for (;;) {
      const int a = p + 1;
      p = a >> 1;
      if (p < 2) break;
      int paso = a >> 2;
      if (fi < int64_t(int16_t(tv[size_t(i)].cuadro))) paso = -paso;
      i += paso;
    }
    if (fi < int64_t(int16_t(tv[size_t(i)].cuadro))) i -= 1;
    return i < 0 ? 0 : i;
  }
  int j = std::min(desde, n - 1);
  for (;;) {
    if (fi < int64_t(int16_t(tv[size_t(j)].cuadro))) {
      --j;
    } else if (j + 1 < n && fi < int64_t(int16_t(tv[size_t(j) + 1].cuadro))) {
      return j;
    } else {
      ++j;
    }
    if (j < 1) return 0;
    if (j >= n - 1) return n - 1;
  }
}

}  // namespace

// EU 0x1B3450 SprAnim_FindKeyIndex.
int BuscarClave(const Canal& c, float t, int desde) { return BuscarEn(c.tiempos, int(c.n), t, desde); }

// EU 0x1B5720 SprAnim_ClampOrWrapTime (canal 2 del original; aqui vale para cualquier canal).
float TiempoCanal(const Canal& c, float t) {
  if (c.n == 0) return 0.0f;
  if (!c.bucle) return t < float(c.dur) ? t : float(c.dur);
  const int d = int(c.dur);
  if (d <= 0) return 0.0f;
  return float(int(t) % d) + (t - float(int(t)));
}

// EU 0x1B65D0 SprAnim_SampleScaleTrack2D: escala 2D (sx, sy, giro). [3..7] solo se escriben con clave de escalon o
// de extremo (el original tampoco los toca al interpolar).
void MuestrearEscala(const Canal& c, float t, int& desde, float out[8]) {
  if (c.n == 0) {
    out[0] = 1.0f;
    out[1] = 1.0f;
    out[2] = 0.0f;
    return;
  }
  const float f = TiempoCanal(c, t);
  int k = desde;
  if (k < 0) k = f == float(c.dur) ? int(c.n) - 1 : BuscarClave(c, f, -1);
  k = std::clamp(k, 0, int(c.n) - 1);
  desde = k;
  auto copia = [&](int q) {
    for (int i = 0; i < 8; ++i) out[i] = c.F32(size_t(q), 4 * size_t(i));
  };
  const uint16_t tipo = c.tiempos[size_t(k)].tipo;
  if (tipo == kSinDato || tipo == kSpline) return;
  if (tipo == kEscalon) {
    copia(k);
    return;
  }
  const float f0 = float(c.tiempos[size_t(k)].cuadro);
  if (f - f0 < 1e-5f) {
    copia(k);
    return;
  }
  if (uint32_t(k) != c.n - 1) {
    const float f1 = float(c.tiempos[size_t(k) + 1].cuadro);
    if (1e-5f <= f1 - f) {
      const float u = (f - f0) / (f1 - f0), w = 1.0f - u;
      for (int i = 0; i < 3; ++i)
        out[i] = w * c.F32(size_t(k), 4 * size_t(i)) + u * c.F32(size_t(k) + 1, 4 * size_t(i));
      return;
    }
    copia(k + 1);
    return;
  }
  if (!c.bucle) {
    copia(k);
    return;
  }
  if (1e-5f <= float(c.dur) - f) {
    const float u = (f - f0) / (float(c.dur) - f0), w = 1.0f - u;
    for (int i = 0; i < 3; ++i) out[i] = w * c.F32(size_t(k), 4 * size_t(i)) + u * c.F32(0, 4 * size_t(i));
    return;
  }
  copia(0);
}

// EU 0x1B5440 SprAnim_SampleFrameTrack: textura (+0), tamano (+4) y UV (+0x10); la textura siempre es la de la clave
// actual, solo se interpola la UV.
void MuestrearCuadro(const Canal& c, float t, int& desde, MuestraCuadro& out) {
  if (c.n == 0) {
    out = MuestraCuadro{};
    return;
  }
  const float f = TiempoCanal(c, t);
  int k = desde;
  if (k < 0) k = f == float(c.dur) ? int(c.n) - 1 : BuscarClave(c, f, -1);
  k = std::clamp(k, 0, int(c.n) - 1);
  desde = k;
  auto copia = [&](int q) {
    out.crudo = c.U32(size_t(q), 0);
    out.tam = c.U32(size_t(q), 4);
    for (int i = 0; i < 4; ++i) out.uv[i] = c.F32(size_t(q), 0x10 + 4 * size_t(i));
  };
  const uint16_t tipo = c.tiempos[size_t(k)].tipo;
  if (tipo == kSinDato || tipo == kSpline) return;   // como el original: no escribe nada
  if (tipo == kEscalon) {
    copia(k);
    return;
  }
  const float f0 = float(c.tiempos[size_t(k)].cuadro);
  if (f - f0 < 1e-5f) {
    copia(k);
    return;
  }
  out.crudo = c.U32(size_t(k), 0);   // la textura es siempre la de la clave actual
  out.tam = c.U32(size_t(k), 4);
  auto interpola = [&](int q, float u) {
    const float cur = c.F32(size_t(k), 8);   // curva de la clave actual (+8)
    const float s = u * cur + (1.0f - cur) * u * u;
    for (int i = 0; i < 4; ++i)
      out.uv[i] = c.F32(size_t(k), 0x10 + 4 * size_t(i)) +
                  (c.F32(size_t(q), 0x10 + 4 * size_t(i)) - c.F32(size_t(k), 0x10 + 4 * size_t(i))) * s;
  };
  if (uint32_t(k) != c.n - 1) {
    const float f1 = float(c.tiempos[size_t(k) + 1].cuadro);
    if (1e-5f <= f1 - f) {
      interpola(k + 1, (f - f0) / (f1 - f0));
      return;
    }
    copia(k + 1);
    return;
  }
  if (!c.bucle) {
    copia(k);
    return;
  }
  if (1e-5f <= float(c.dur) - f) {
    interpola(0, (f - f0) / (float(c.dur) - f0));
    return;
  }
  copia(0);
}

// EU 0x1B5F40 SprAnim_SampleVecSplineTrack: posicion vec4. Tipo 1 = lineal con curva; tipo 2 = Catmull-Rom con los
// cuatro vecinos (en bucle dan la vuelta, sin bucle se fijan en los extremos). Sin pista deja `out` intacto.
void MuestrearPosicion(const Canal& c, float t, int& desde, float out[4]) {
  if (c.n == 0) return;
  const float f = TiempoCanal(c, t);
  int k = desde;
  if (k < 0) k = f == float(c.dur) ? int(c.n) - 1 : BuscarClave(c, f, -1);
  k = std::clamp(k, 0, int(c.n) - 1);
  desde = k;
  const int n = int(c.n);
  auto vec = [&](int q, float v[4]) {
    for (int i = 0; i < 4; ++i) v[i] = c.F32(size_t(q), 4 * size_t(i));
  };
  auto copia = [&](int q) { vec(q, out); };
  const uint16_t tipo = c.tiempos[size_t(k)].tipo;
  if (tipo == kSinDato) return;
  if (tipo == kEscalon) {
    copia(k);
    return;
  }
  const float f0 = float(c.tiempos[size_t(k)].cuadro);
  if (f - f0 < 1e-5f) {
    copia(k);
    return;
  }
  if (tipo == kLineal) {
    auto interpola = [&](int q, float u) {
      const float cur = c.F32(size_t(k), 0x10);
      const float s = u * cur + (1.0f - cur) * u * u;
      for (int i = 0; i < 4; ++i)
        out[i] = c.F32(size_t(k), 4 * size_t(i)) +
                 (c.F32(size_t(q), 4 * size_t(i)) - c.F32(size_t(k), 4 * size_t(i))) * s;
    };
    if (uint32_t(k) != c.n - 1) {
      const float f1 = float(c.tiempos[size_t(k) + 1].cuadro);
      if (1e-5f <= f1 - f) {
        interpola(k + 1, (f - f0) / (f1 - f0));
        return;
      }
      copia(k + 1);
      return;
    }
    if (!c.bucle) {
      copia(k);
      return;
    }
    if (1e-5f <= float(c.dur) - f) {
      interpola(0, (f - f0) / (float(c.dur) - f0));
      return;
    }
    copia(0);
    return;
  }
  // tipo 2: Catmull-Rom. Pesos exactos del decomp (IW:151280-151299 y 151339-151359):
  //   s = u*curva + (1-curva)*u^2;  p = -0.5s^3 + s^2 - 0.5s;  n = 0.5s^3 - 0.5s^2
  //   a = 1.5s^3 - 2.5s^2 + 1;      b = -1.5s^3 + 2s^2 + 0.5s
  auto spline = [&](int ka, int kb, int kprev, int knext, float u) {
    const float cur = c.F32(size_t(ka), 0x10);
    const float s = u * cur + (1.0f - cur) * u * u;
    const float s2 = s * s, s3 = s2 * s;
    const float wp = (s3 + s) * -0.5f + s2;
    const float wn = (s3 - s2) * 0.5f;
    const float wa = s3 * 1.5f - s2 * 2.5f + 1.0f;
    const float wb = s * 0.5f - s3 * 1.5f + s2 * 2.0f;
    float va[4], vb[4], vp[4], vn[4];
    vec(ka, va), vec(kb, vb), vec(kprev, vp), vec(knext, vn);
    for (int i = 0; i < 3; ++i) out[i] = vn[i] * wn + vb[i] * wb + vp[i] * wp + va[i] * wa;
    out[3] = 1.0f;
  };
  if (uint32_t(k) != c.n - 1) {
    const float f1 = float(c.tiempos[size_t(k) + 1].cuadro);
    if (1e-5f <= f1 - f) {
      int prev, next;
      if (!c.bucle) {
        prev = k != 0 ? k - 1 : k;
        next = k == n - 2 ? k + 1 : k + 2;
      } else {
        prev = k == 0 ? n - 1 : k - 1;
        next = k == n - 2 ? 0 : k + 2;
      }
      spline(k, k + 1, prev, next, (f - f0) / (f1 - f0));
      return;
    }
    copia(k + 1);
    return;
  }
  if (!c.bucle) {
    copia(k);
    return;
  }
  if (1e-5f <= float(c.dur) - f) {
    // ultima clave -> primera: vecinos como el original (anterior k-1; siguiente la clave 1).
    const int prev = k != 0 ? k - 1 : k;
    spline(k, 0, prev, n > 1 ? 1 : 0, (f - f0) / (float(c.dur) - f0));
    return;
  }
  copia(0);
}

// EU 0x1B3710 SprAnim_InterpolateKey: mezcla tinte (curva +0x18) y bytes (curva +0x1C); el modo es el de la clave A.
static void InterpolarColor(float u, const Canal& c, int ka, int kb, MuestraColor& out) {
  const float ct = c.F32(size_t(ka), 0x18), cb = c.F32(size_t(ka), 0x1C);
  const float st = u * ct + (1.0f - ct) * u * u;
  for (int i = 0; i < 4; ++i)
    out.tinte[i] = c.F32(size_t(ka), 4 * size_t(i)) +
                   (c.F32(size_t(kb), 4 * size_t(i)) - c.F32(size_t(ka), 4 * size_t(i))) * st;
  const float sb = u * cb + (1.0f - cb) * u * u;
  const float w = 1.0f - sb;
  for (int i = 0; i < 4; ++i) {
    const float v = w * float(c.U8(size_t(ka), 0x10 + size_t(i))) + sb * float(c.U8(size_t(kb), 0x10 + size_t(i)));
    out.rgba[i] = uint8_t(v >= 255.0f ? 255 : int(v));
  }
  out.modo = c.U32(size_t(ka), 0x14);
}

// EU 0x1B4FC0 SprAnim_SampleColorTrackB: tinte + bytes RGBA + modo. Sin pista: (1,1,1,1), bytes 0, modo 1.
void MuestrearColor(const Canal& c, float t, int& desde, MuestraColor& out) {
  if (c.n == 0) {
    out = MuestraColor{};
    return;
  }
  const float f = TiempoCanal(c, t);
  int k = desde;
  if (k < 0) k = f == float(c.dur) ? int(c.n) - 1 : BuscarClave(c, f, -1);
  k = std::clamp(k, 0, int(c.n) - 1);
  desde = k;
  auto copia = [&](int q) {
    for (int i = 0; i < 4; ++i) out.tinte[i] = c.F32(size_t(q), 4 * size_t(i));
    for (int i = 0; i < 4; ++i) out.rgba[i] = c.U8(size_t(q), 0x10 + size_t(i));
    out.modo = c.U32(size_t(q), 0x14);
  };
  const uint16_t tipo = c.tiempos[size_t(k)].tipo;
  if (tipo == kSinDato || tipo == kSpline) return;
  if (tipo == kEscalon) {
    copia(k);
    return;
  }
  const float f0 = float(c.tiempos[size_t(k)].cuadro);
  if (f - f0 < 1e-5f) {
    copia(k);
    return;
  }
  if (uint32_t(k) != c.n - 1) {
    const float f1 = float(c.tiempos[size_t(k) + 1].cuadro);
    if (1e-5f <= f1 - f) {
      InterpolarColor((f - f0) / (f1 - f0), c, k, k + 1, out);
      return;
    }
    copia(k + 1);
    return;
  }
  if (!c.bucle) {
    copia(k);
    return;
  }
  if (1e-5f <= float(c.dur) - f) {
    InterpolarColor((f - f0) / (float(c.dur) - f0), c, k, 0, out);
    return;
  }
  copia(0);
}

// EU 0x1B7520 SprAnim_DrawAtTime: recorre la cadena de segmentos (sin envolver: los bucles son por canal) y muestrea.
// DS57 (IW:152303-152313): el original avanza mientras haya segmento siguiente y su duracion sea <= t (un tramo de
// duracion 0 tambien se salta); el port se paraba en los de duracion 0. El tope por numero de segmentos evita un
// bucle infinito si un #AMA trae una cadena ciclica de tramos de 0.
static int SegmentoEnTiempo(const Obra& o, int seg, float& t) {
  for (size_t n = 0; seg >= 0 && n < o.NumSegmentos(); ++n) {
    const Segmento& s = o.Seg(seg);
    if (float(s.duracion) > t || s.siguiente < 0) break;
    t -= float(s.duracion);
    seg = s.siguiente;
  }
  return seg;
}
// EU 0x1B25E0 Sprite_BuildDrawEntry: compone la entrada de dibujo (IW:149196-149385). El rect sale del segmento por
// las escalas del nodo y del contexto, la posicion es la heredada mas el desplazamiento local escalado, el tinte se
// multiplica, los bytes se suman con tope 255 y el giro es el del nodo mas el heredado.
bool ConstruirEntrada(const Segmento& s, const Contexto& ctx, const Muestras& m, Entrada& e) {
  if (!s.TieneRect()) return false;
  e = Entrada{};
  e.flags = 1u | (s.flags & 6u);
  e.modo = m.color.modo;
  e.cuadro_tam = m.cuadro.tam;
  e.textura = uint16_t(m.cuadro.crudo & 0xFFFF);
  for (int i = 0; i < 2; ++i) {
    e.tinte[i] = ctx.tinte[i] * m.color.tinte[i];
    e.rect[i] = s.rect[i] * m.escala[i] * ctx.escala_tam[i];
    e.rect[2 + i] = s.rect[2 + i] * m.escala[i] * ctx.escala_tam[i];
  }
  // DS57 (IW:149794-149811, Sprite_BuildDrawEntry): el desplazamiento local se GIRA con el giro
  // heredado (contexto +0x40) antes de escalarlo por la escala de posicion y sumarlo a la
  // posicion heredada. Faltaba el giro: los hijos de un nodo con giro heredado salian en la
  // direccion sin girar (posicion visible de los efectos).
  float dx = m.pos[0], dy = m.pos[1];
  if (ctx.giro != 0.0f) {
    const double a = double(ctx.giro) * 3.14159265358979323846 / 180.0;
    const float co = float(std::cos(a)), si = float(std::sin(a));
    const float x = dx * co - dy * si;
    dy = dx * si + dy * co;
    dx = x;
  }
  e.pos[0] = ctx.pos[0] + dx * ctx.escala_pos[0];
  e.pos[1] = ctx.pos[1] + dy * ctx.escala_pos[1];
  e.pos[2] = ctx.pos[2] + m.pos[2];
  e.pos[3] = ctx.pos[3] + m.pos[3];
  for (int i = 2; i < 4; ++i) e.tinte[i] = ctx.tinte[i] * m.color.tinte[i];
  for (int i = 0; i < 4; ++i) e.uv[i] = m.cuadro.uv[i];
  for (int i = 0; i < 4; ++i) {
    const int v = int(ctx.rgba_add[i]) + int(m.color.rgba[i]);
    e.rgba[i] = uint8_t(v > 255 ? 255 : v);
  }
  e.giro = m.escala[2] + ctx.giro;
  return true;
}

bool DibujarEnTiempo(const Obra& o, int seg, float t, const Contexto& ctx, Muestras& m, Entrada& e) {
  if (seg < 0) return false;
  float ft = t;
  const int sg = SegmentoEnTiempo(o, seg, ft);
  if (sg < 0) return false;
  const Segmento& sd = o.Seg(sg);
  // Se muestrea en el tiempo dentro del segmento; los caches son por llamada (-1) como en DrawAtTime.
  int c = -1;
  MuestrearEscala(sd.a.c1, ft, c, m.escala);
  c = -1;
  MuestrearCuadro(sd.b.c1, ft, c, m.cuadro);
  c = -1;
  for (int i = 0; i < 4; ++i) m.pos[i] = 0.0f;
  MuestrearPosicion(sd.a.c2, ft, c, m.pos);
  c = -1;
  MuestrearColor(sd.b.c2, ft, c, m.color);
  return ConstruirEntrada(sd, ctx, m, e);
}

// Copia recursiva del arbol de disco a memoria (la usa CrearArbol).
static int CrearNodo(Arbol& a, int disco, int padre, int prof) {
  if (disco < 0 || disco >= int(a.obra->NumNodos()) || prof > 64) return -1;
  const int idx = int(a.nodos.size());
  a.nodos.push_back({});
  a.nodos[size_t(idx)].disco = disco;
  a.nodos[size_t(idx)].padre = padre;
  a.nodos[size_t(idx)].flags = 1;
  const NodoDisco d = a.obra->Nodo(disco);   // copia: el vector de nodos puede crecer al bajar
  if (d.hermano >= 0) a.nodos[size_t(idx)].hermano = CrearNodo(a, d.hermano, padre, prof + 1);
  if (d.hijo >= 0) a.nodos[size_t(idx)].hijo = CrearNodo(a, d.hijo, idx, prof + 1);
  return idx;
}

// EU 0x1B9510 SprAnim_CreateNodeTree: copia el arbol del nodo de disco (con hermanos e hijos) a memoria.
Arbol CrearArbol(const Obra& obra, int nodoDisco, float tiempo) {
  Arbol a;
  a.obra = &obra;
  if (!obra.Valida() || nodoDisco < 0 || nodoDisco >= int(obra.NumNodos())) return a;
  a.raiz = CrearNodo(a, nodoDisco, -1, 0);
  if (a.raiz >= 0) a.nodos[size_t(a.raiz)].tiempo = tiempo;
  return a;
}

// EU 0x1B82E0 SprNode_MarkTreeActive: fija el tiempo y el bit 1 en el nodo, sus hermanos y sus hijos.
void MarcarArbol(Arbol& a, float tiempo) {
  for (Nodo& n : a.nodos) {
    if (!(n.flags & 1)) continue;
    n.tiempo = tiempo;
    n.flags |= 2;
  }
}

// EU 0x1B23E0 SprNode_GetMaxFrameCount: mayor duracion de los segmentos del arbol.
// EU 0x1B2410 SprNode_CalcMaxFrameRecursive: el original recorre la copia en memoria; aqui basta con la tabla de
// disco, porque la copia conserva el mismo `disco`.
uint32_t MaxCuadros(const Arbol& a) {
  uint32_t mx = 0;
  if (!a.obra) return 0;
  for (const Nodo& n : a.nodos) {
    if (n.disco < 0) continue;
    const int seg = a.obra->Nodo(n.disco).anim;
    if (seg >= 0) mx = std::max(mx, a.obra->Seg(seg).duracion);
  }
  return mx;
}

// EU 0x1B2390 SprNode_HasNoAnimData: el nodo no tiene datos de animacion (sin animacion en disco).
bool SinAnimacion(const Arbol& a) {
  if (!a.obra || a.raiz < 0) return true;
  const Nodo& r = a.nodos[size_t(a.raiz)];
  return r.disco < 0 || a.obra->Nodo(r.disco).anim < 0;
}

// Las tres muestras de la pista de efectos son las mismas tablas de clave de 0x20 B que las de animacion (DS37 §3:
// comparten cuerpo); estas entradas existen aparte porque el original las tiene aparte y asi se cita cada direccion.
// EU 0x1B46A0 SprAnim_SampleScaleTrack: escala de la pista de efectos. NO es la escala 2D del sprite
// (0x1B65D0): la clave es de 8 floats pero solo usa [0..4] = escala X, Y, tamano X, tamano Y y giro, y la
// interpolacion (tipo 1) lleva tres curvas propias en +0x14, +0x18 y +0x1C (decomp 001b46a0, lineas 346-371 y
// 383-415). Con clave de escalon/extremo copia los 8 floats; con tipo 2/3 no escribe. Sin pista: (1,1,1,1,0).
static void MuestrearEscalaFx(const Canal& c, float t, int& desde, float out[8]) {
  if (c.n == 0) {
    out[0] = out[1] = out[2] = out[3] = 1.0f;
    out[4] = 0.0f;
    return;
  }
  const float f = TiempoCanal(c, t);
  int k = desde;
  if (k < 0) k = f == float(c.dur) ? int(c.n) - 1 : BuscarClave(c, f, -1);
  k = std::clamp(k, 0, int(c.n) - 1);
  desde = k;
  auto copia = [&](int q) {
    for (int i = 0; i < 8; ++i) out[i] = c.F32(size_t(q), 4 * size_t(i));
  };
  const uint16_t tipo = c.tiempos[size_t(k)].tipo;
  if (tipo == kEscalon) {
    copia(k);
    return;
  }
  if (tipo != kLineal) return;
  const float f0 = float(c.tiempos[size_t(k)].cuadro);
  if (f - f0 < 1e-5f) {
    copia(k);
    return;
  }
  auto interpola = [&](int q, float u) {
    const float s01 = u * c.F32(size_t(k), 0x14) + (1.0f - c.F32(size_t(k), 0x14)) * u * u;
    out[0] = c.F32(size_t(k), 0x00) + (c.F32(size_t(q), 0x00) - c.F32(size_t(k), 0x00)) * s01;
    out[1] = c.F32(size_t(k), 0x04) + (c.F32(size_t(q), 0x04) - c.F32(size_t(k), 0x04)) * s01;
    const float s23 = u * c.F32(size_t(k), 0x18) + (1.0f - c.F32(size_t(k), 0x18)) * u * u;
    out[2] = c.F32(size_t(k), 0x08) + (c.F32(size_t(q), 0x08) - c.F32(size_t(k), 0x08)) * s23;
    out[3] = c.F32(size_t(k), 0x0C) + (c.F32(size_t(q), 0x0C) - c.F32(size_t(k), 0x0C)) * s23;
    const float s4 = u * c.F32(size_t(k), 0x1C) + (1.0f - c.F32(size_t(k), 0x1C)) * u * u;
    out[4] = c.F32(size_t(k), 0x10) + (c.F32(size_t(q), 0x10) - c.F32(size_t(k), 0x10)) * s4;
  };
  if (uint32_t(k) != c.n - 1) {
    const float f1 = float(c.tiempos[size_t(k) + 1].cuadro);
    if (1e-5f <= f1 - f) {
      interpola(k + 1, (f - f0) / (f1 - f0));
      return;
    }
    copia(k + 1);
    return;
  }
  if (!c.bucle) {
    copia(k);
    return;
  }
  if (1e-5f <= float(c.dur) - f) {
    interpola(0, (f - f0) / (float(c.dur) - f0));
    return;
  }
  copia(0);
}
// EU 0x1B57F0 SprAnim_SamplePosTrack: posicion de la pista de efectos.
static void MuestrearPosicionFx(const Canal& c, float t, int& desde, float out[4]) {
  MuestrearPosicion(c, t, desde, out);
}
// EU 0x1B4B80 SprAnim_SampleColorTrack: tinte y bytes de la pista de efectos.
static void MuestrearColorFx(const Canal& c, float t, int& desde, MuestraColor& out) {
  MuestrearColor(c, t, desde, out);
}

// EU 0x1B3D50 Fx_ApplyKeyframeTracks: aplica la pista de efectos al contexto heredado (posicion girada y escalada,
// tinte multiplicado y bytes sumados, escala y giro).
void AplicarFx(const Segmento& s, float t, const Muestras& m, Contexto& ctx) {
  if (!s.tiene_fx) return;
  const Fx& f = s.fx;
  auto gira = [&](float x, float y, float out[2]) {
    const double a = double(ctx.giro) * 3.14159265358979323846 / 180.0;
    const float co = float(std::cos(a)), si = float(std::sin(a));
    out[0] = x * co - y * si;
    out[1] = x * si + y * co;
  };
  // Posicion: el bit 0x10 usa la posicion muestreada del nodo; si no, su canal (IW:149891-149930).
  if (!(f.flags & 0x10)) {
    if (f.pos.n) {
      int c = -1;
      float p[4] = {0, 0, 0, 0};
      MuestrearPosicionFx(f.pos, t, c, p);
      float g[2];
      gira(p[0], p[1], g);
      ctx.pos[0] += ctx.escala_pos[0] * g[0];
      ctx.pos[1] += ctx.escala_pos[1] * g[1];
      ctx.pos[2] += p[2];
    }
  } else {
    float g[2];
    gira(m.pos[0], m.pos[1], g);
    ctx.pos[0] += ctx.escala_pos[0] * g[0];
    ctx.pos[1] += ctx.escala_pos[1] * g[1];
    ctx.pos[2] += m.pos[2];
  }
  // Color: el bit 0x20 usa el color del nodo; si no, su canal (IW:149933-150021).
  if (!(f.flags & 0x20)) {
    if (f.color.n) {
      int c = -1;
      MuestraColor q;
      MuestrearColorFx(f.color, t, c, q);
      for (int i = 0; i < 4; ++i) ctx.tinte[i] *= q.tinte[i];
      for (int i = 0; i < 4; ++i) {
        const int v = int(ctx.rgba_add[i]) + int(q.rgba[i]);
        ctx.rgba_add[i] = uint8_t(v > 255 ? 255 : v);
      }
    }
  } else {
    for (int i = 0; i < 4; ++i) ctx.tinte[i] *= m.color.tinte[i];
    for (int i = 0; i < 4; ++i) {
      const int v = int(ctx.rgba_add[i]) + int(m.color.rgba[i]);
      ctx.rgba_add[i] = uint8_t(v > 255 ? 255 : v);
    }
  }
  // Escala: el bit 0x08 usa la escala del nodo (solo el tamano y el giro); si no, su canal, que escala las cuatro y
  // suma el giro en [4] (IW:150024-150058).
  if (!(f.flags & 8)) {
    if (f.escala.n) {
      int c = -1;
      float e[8] = {1, 1, 0, 0, 0, 0, 0, 0};
      MuestrearEscalaFx(f.escala, t, c, e);
      ctx.escala_pos[0] *= e[0];
      ctx.escala_pos[1] *= e[1];
      ctx.escala_tam[0] *= e[2];
      ctx.escala_tam[1] *= e[3];
      ctx.giro += e[4];
    }
  } else {
    ctx.escala_tam[0] *= m.escala[0];
    ctx.escala_tam[1] *= m.escala[1];
    ctx.giro += m.escala[2];
  }
}

// EU 0x1B7810 SprAnim_GetTrackEndFlags: 1 si el canal termino (sin claves, o sin bucle y t >= duracion).
static bool CanalFin(const Canal& c, float t) { return c.n == 0 || (!c.bucle && float(c.dur) <= t); }

uint32_t BanderasFin(const Obra& o, int seg, float t) {
  if (seg < 0) return 0;
  // DS57 (IW:152403-152408): el original recibe el PRIMER segmento del nodo y el tiempo del
  // nodo, y avanza el encadenado dentro de esta funcion (restando la duracion de cada tramo).
  // Antes se le pasaba el segmento ya avanzado y el tiempo entero: con cadenas (>1 tramo) las
  // banderas de fin se calculaban con un tiempo de mas y el efecto no terminaba cuando tocaba.
  for (size_t n = 0; n < o.NumSegmentos(); ++n) {
    const Segmento& s = o.Seg(seg);
    if (s.siguiente < 0 || float(s.duracion) > t) break;
    t -= float(s.duracion);
    seg = s.siguiente;
  }
  const Segmento& s = o.Seg(seg);
  uint32_t r = 0;
  if (CanalFin(s.a.c1, t)) r |= 0x04;   // escala 2D
  if (CanalFin(s.a.c2, t)) r |= 0x01;   // posicion
  if (CanalFin(s.b.c2, t)) r |= 0x02;   // color
  if (CanalFin(s.b.c1, t)) r |= 0x08;   // cuadro/textura
  // DS57 (IW:152467-152505): 0x10 = canal de POSICION de la pista de efectos, 0x20 = canal de
  // COLOR y 0x40 = canal de ESCALA. El port usaba color y escala para 0x10/0x20 (dos bits
  // cruzados: posicion y color nunca se avisaban bien).
  if (CanalFin(s.fx.pos, t)) r |= 0x10;     // efectos: canal de posicion
  if (CanalFin(s.fx.color, t)) r |= 0x20;   // efectos: canal de color
  if (CanalFin(s.fx.escala, t)) r |= 0x40;  // efectos: canal de escala
  if (s.eventos.empty() || (!s.eventos_bucle && float(s.eventos_dur) <= t)) r |= 0x80;
  return r;
}

// EU 0x1B69B0 SprAnim_UpdateAndDrawNode: hermano primero, luego este nodo y al final el hijo (orden del decomp).
void ActualizarNodo(Arbol& a, int ni, Contexto ctx, std::vector<Entrada>& salida, CbEvento eventos, void* user,
                    CbEfecto efecto) {
  if (!a.obra || ni < 0 || ni >= int(a.nodos.size())) return;
  Nodo& nd = a.nodos[size_t(ni)];
  ActualizarNodo(a, nd.hermano, ctx, salida, eventos, user, efecto);
  const Obra& o = *a.obra;
  int seg = nd.disco >= 0 ? o.Nodo(nd.disco).anim : -1;
  float t = nd.tiempo;
  seg = SegmentoEnTiempo(o, seg, t);
  nd.seg = seg;
  if (seg >= 0) {
    const Segmento& s = o.Seg(seg);
    Muestras m;
    MuestrearEscala(s.a.c1, t, nd.c_escala, m.escala);
    MuestrearCuadro(s.b.c1, t, nd.c_cuadro, m.cuadro);
    for (int i = 0; i < 4; ++i) m.pos[i] = 0.0f;   // desplazamiento local: el heredado lo suma Entrada.pos
    MuestrearPosicion(s.a.c2, t, nd.c_pos, m.pos);
    MuestrearColor(s.b.c2, t, nd.c_color, m.color);
    // Pista de eventos (IW:152066-152198): indice de clave con la cache del nodo (el original
    // se la pasa siempre a FindKeyIndex, no solo la primera vez) y despacho como el original:
    //  * si la clave NO cambio y es de tipo 1, el evento se vuelve a disparar cada cuadro
    //    (IW:152094-152140: es un evento "vivo" mientras dura la clave);
    //  * si cambio, se disparan las claves NUEVAS de la mas reciente a la mas antigua
    //    (IW:152142-152198) y de cualquier tipo (el tipo solo se mira en la clave actual).
    if (!s.eventos.empty()) {
      const int ne = int(s.eventos.size());
      const float te = s.eventos_bucle ? (s.eventos_dur ? float(int(t) % int(s.eventos_dur)) +
                                                                (t - float(int(t)))
                                                         : 0.0f)
                                       : std::min(t, float(s.eventos_dur));
      const int k = BuscarEn(s.eventos_tiempos, ne, te, nd.c_evento);
      if (k >= 0 && k < ne) {
        if (k == nd.c_evento) {
          if (s.eventos_tiempos[size_t(k)].tipo == uint16_t(kLineal) && s.eventos[size_t(k)].id >= 0 &&
              eventos)
            eventos(user, s.eventos[size_t(k)]);
        } else if (eventos) {
          const int paso = (nd.c_evento < 0 || k > nd.c_evento) ? -1 : 1;
          for (int q = k; q != nd.c_evento; q += paso)
            if (q >= 0 && q < ne && s.eventos[size_t(q)].id >= 0) eventos(user, s.eventos[size_t(q)]);
        }
      }
      nd.c_evento = k;
    }
    if (!s.TieneRect()) {
      // DS57 (IW:152229-152240): sin rect el original solo aplica los FX al contexto y baja a
      // los hijos; NO llama a la callback del nodo ni a la de la tabla del segmento.
      Contexto hijo = ctx;
      AplicarFx(s, t, m, hijo);
      ActualizarNodo(a, nd.hijo, hijo, salida, eventos, user, efecto);
      return;
    }
    Entrada e{};
    if (ConstruirEntrada(s, ctx, m, e)) salida.push_back(e);
    Contexto hijo = ctx;
    AplicarFx(s, t, m, hijo);
    if (efecto && s.efectos_crudo) efecto(user, s, t, hijo);
    ActualizarNodo(a, nd.hijo, hijo, salida, eventos, user, efecto);
    return;
  }
  ActualizarNodo(a, nd.hijo, ctx, salida, eventos, user, efecto);
}

// EU 0x1B7BB0 BackgroundDisplay: avanza el tiempo de la raiz y la dibuja; deja las banderas de fin en la raiz (+8).
uint32_t Fondo(Arbol& a, float factor, float velocidad, Contexto ctx, std::vector<Entrada>& salida, CbEvento eventos,
               void* user, CbEfecto efecto) {
  if (!a.obra || a.raiz < 0 || a.raiz >= int(a.nodos.size())) return 0;
  Nodo& r = a.nodos[size_t(a.raiz)];
  if (!(r.flags & 1)) return 0;
  float t = velocidad * factor + r.tiempo;
  if (t < 0.0f) t = 0.0f;
  MarcarArbol(a, t);
  ActualizarNodo(a, a.raiz, ctx, salida, eventos, user, efecto);
  // DS57: el original (IW:152635-152646) calcula las banderas del PRIMER segmento del nodo con
  // el tiempo del nodo; BanderasFin avanza el encadenado como el original.
  const int seg0 = r.disco >= 0 ? a.obra->Nodo(r.disco).anim : -1;
  r.fin = BanderasFin(*a.obra, seg0, r.tiempo);
  return r.fin;
}

// EU 0x1B2E00 Sprite_DrawEntry: la parte de geometria (el original ademas fija el estado del GS: modo de mezcla
// 0x40001124 para el 0 y 0x40000924 con los registros 0x44/0x48/0x42 para los modos 1/2/3, IW:149227-149247; el alfa
// del byte +0x1F decide el OR de 0x8000). El port pinta con su atlas y estos cuatro vertices.
void DibujarEntrada(const Entrada& e, Quad& q) {
  const double a = double(e.giro) * 3.14159265358979323846 / 180.0;
  const float c = float(std::cos(a)), s = float(std::sin(a));
  const float rx[4] = {e.rect[0], e.rect[2], e.rect[2], e.rect[0]};
  const float ry[4] = {e.rect[1], e.rect[1], e.rect[3], e.rect[3]};
  const float uu[4] = {e.uv[0], e.uv[2], e.uv[2], e.uv[0]};
  const float vv[4] = {e.uv[1], e.uv[1], e.uv[3], e.uv[3]};
  for (int i = 0; i < 4; ++i) {
    q.xy[i][0] = rx[i] * c - ry[i] * s + e.pos[0];
    q.xy[i][1] = rx[i] * s + ry[i] * c + e.pos[1];
    q.uv[i][0] = uu[i];
    q.uv[i][1] = vv[i];
  }
}

// EU 0x1B2030 MiscellaneousGameProperties_Display_: primer registro libre (flags bit0 = 0).
int Pool::Crear(const Arbol& arbol, int id, int slot1, int slot2, uint32_t mascara, int tipo) {
  for (int i = 0; i < kMax; ++i) {
    if (s[i].flags & 1) continue;
    s[i] = SpriteEstado{};
    s[i].flags = 7;   // activo + avanza tiempo + render de textura (IW:148446)
    s[i].arbol = arbol;
    s[i].max = float(MaxCuadros(arbol));
    s[i].mascara = mascara;
    s[i].visuales = uint8_t(tipo);
    // DS57 (IW:148449-148456): el original guarda id/slot1/slot2 en +0x10/+0x14/+0x18 (anim = -1) y la tabla de
    // visuales en +0x13/+0x15. Los indices se guardan aqui; resolverlos a un arbol es del cargador [H].
    s[i].id = id;
    s[i].slot1 = slot1;
    s[i].slot2 = slot2;
    s[i].anim = -1;
    return i;
  }
  return -1;
}

// EU 0x1B1bb0 Sprite_SetFrame.
void Pool::PonerCuadro(SpriteEstado& e, float cuadro) {
  e.cuadro = std::min(cuadro, e.max);
  MarcarArbol(e.arbol, e.cuadro);
}

// EU 0x1B1e20 Appear_FadeFlag_: cambia de animacion (arbol), cuadro a 0, maximo nuevo y un dibujo en el cuadro 0.
void Pool::PonerAnim(SpriteEstado& e, const Arbol& nuevo, int slot, const Contexto& ctx,
                     std::vector<Entrada>& salida, CbEvento eventos, void* user, float velocidad) {
  // DS57 (IW:148324-148325): con la entrada -1 en la tabla de visuales no hace nada (ni toca cuadro/maximo).
  if (slot == -1) return;
  e.arbol = nuevo;
  e.anim = slot;
  e.cuadro = 0.0f;
  e.max = float(MaxCuadros(nuevo));
  // DS57 (IW:148365): el original llama BackgroundDisplay(1.0), o sea factor 1 sobre la velocidad del juego; antes
  // se pintaba en 0 (un cuadro de menos en el primer dibujo del sprite).
  Fondo(e.arbol, 1.0f, velocidad, ctx, salida, eventos, user);
}

// EU 0x1B1960 Sprite_Update: llama a la callback del sprite (+0x58, aqui no modelada) y libera su arbol.
void Pool::Actualizar(SpriteEstado& e) {
  e.arbol = Arbol{};
}

// EU 0x1B1860 Sprite_UpdateAll.
void Pool::ActualizarTodos(const Contexto& ctx) {
  (void)ctx;
  for (SpriteEstado& e : s)
    if ((e.flags & 1) && !(e.flags & 0x10) && !(e.flags & 0x80)) Actualizar(e);
}

// EU 0x1B1a00 GeneralGraphicEffects_Textures (IW:148082-148152).
int Pool::Pases(int modo, uint32_t mascara, float velocidad, const Contexto& ctx, std::vector<Entrada>& salida,
                CbEvento eventos, void* user) {
  int n = 0;
  for (SpriteEstado& e : s) {
    bool match;
    // DS57 (IW:148101-148143): el original dibuja
    //  * modo 1: los registros con mascara (+0x44) == 0 (el grupo "sin capa", que la escena
    //    pinta entre la capa 1 y la 2);
    //  * modo 0 con mascara 0: los que NO llevan el bit 0x40;
    //  * modo 0 con mascara != 0: los que comparten algun bit con ella.
    // El port lo tenia invertido en los dos primeros casos (mascara 0 <-> bit 0x40): las capas
    // del HUD/mapa salian en el pase contrario.
    if (modo == 1) {
      match = e.mascara == 0;
    } else if (mascara == 0) {
      match = (e.flags & 0x40) == 0;
    } else {
      match = (mascara & e.mascara) != 0;
    }
    if (!match || !(e.flags & 1)) continue;
    ++n;
    if (!(e.flags & 2)) {
      Fondo(e.arbol, 0.0f, velocidad, ctx, salida, eventos, user);
    } else {
      Fondo(e.arbol, 1.0f, velocidad, ctx, salida, eventos, user);
      e.cuadro += velocidad;   // fRam0049ca1c = 1.0 por cuadro
      if (e.max <= e.cuadro) {
        if (e.flags & 8)
          Actualizar(e);
        else if (e.fundido == 0)
          e.fundido = 1;   // Appear_FadeFlag_(rec, 1, 0): arranca el fundido de salida
      }
    }
  }
  return n;
}

// EU 0x2FFD30 Sprite_DisplayHudSetByMode (IW:388951-388999).
size_t HudPorModo(int modo, const HudObjeto** lista) {
  static constexpr HudObjeto k0[] = {{0, -1, -1, 2, false}, {1, 2, 3, 1, true}, {4, -1, -1, 1, true},
                                     {5, -1, -1, 1, true}, {6, -1, -1, 1, true}, {7, -1, -1, 1, true}, {-1, -1, -1, 0, false}};
  static constexpr HudObjeto k1[] = {{0x3C, -1, -1, 2, false}, {0x3D, 0x3E, 0x3F, 1, true}, {0x40, -1, -1, 1, true},
                                     {0x41, -1, -1, 1, true}, {0x42, -1, -1, 1, true}, {0x43, -1, -1, 1, true},
                                     {-1, -1, -1, 0, false}};
  static constexpr HudObjeto k3[] = {{0x78, -1, -1, 2, false}, {0x71, 0x72, 0x73, 1, true}, {0x74, -1, -1, 1, true},
                                     {0x75, -1, -1, 1, true}, {0x76, -1, -1, 1, true}, {0x79, -1, -1, 1, true},
                                     {-1, -1, -1, 0, false}};
  static constexpr HudObjeto k5[] = {{0x70, -1, -1, 2, false}, {0x4C, 0x4D, 0x4E, 1, true}, {-1, -1, -1, 0, false}};
  // El modo 4 cae sin break en el 0/2 (IW:388978-388981): 0x77 y despues el set comun.
  static constexpr HudObjeto k4[] = {{0x77, -1, -1, 1, true}, {0, -1, -1, 2, false}, {1, 2, 3, 1, true},
                                     {4, -1, -1, 1, true}, {5, -1, -1, 1, true}, {6, -1, -1, 1, true},
                                     {7, -1, -1, 1, true}, {-1, -1, -1, 0, false}};
  const HudObjeto* l = k0;
  if (modo == 1) l = k1;
  else if (modo == 3) l = k3;
  else if (modo == 4) l = k4;
  else if (modo == 5) l = k5;
  if (lista) *lista = l;
  size_t n = 0;
  while (l[n].id >= 0) ++n;
  return n;
}

}  // namespace dbz3::anim::ama


// Solo US (como iw_minijuegos.cpp). sub_8208FAB0: lo llama la lectura del mando de cada usuario (sub_8208FD58) en
// cada cuadro; de aqui sale el contexto para arrancar animaciones. Sin nada pedido no hace nada.
REX_HOOK_RAW(sub_8208FAB0) {
  __imp__sub_8208FAB0(ctx, base);
  dbz3::anim::Tick(ctx, base);
}

// DS103: reloj del motor. sub_820FA4B8(anim_obj, mascara) es el UNICO que suma a A+0x608 (y copia ese valor al
// reloj del tercer clip A+0x63C); si A+0x66C != 0 no avanza (0x820FA73C). Se guarda el cuadro de antes y, al volver,
// se repone el codigo de IW y se lleva el reloj (ver AjustarReloj). Es el mismo patron que el gancho de Tick.
REX_HOOK_RAW(sub_820FA4B8) {
  const uint32_t A = ctx.r3.u32;
  float antes = 0.0f;
  if (A >= 0x10000000u && A < 0xF0000000u) {
    const uint32_t u = REX_LOAD_U32(A + 0x608);
    std::memcpy(&antes, &u, 4);
  }
  __imp__sub_820FA4B8(ctx, base);
  if (A >= 0x10000000u && A < 0xF0000000u) dbz3::anim::AjustarReloj(ctx, base, A, antes);
}
