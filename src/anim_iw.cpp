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
#include <mutex>
#include <string>
#include <vector>

#include "generated/dbz3_init.h"
#include "iw_minijuegos.h"
#include "pasarela.h"
#include "pasarela_iw.h"
#include "roster_ext.h"

namespace dbz3::anim {
namespace {

namespace P = dbz3::pasarela;

constexpr uint32_t kAnimObj = 0x650, kBsk = 0x2C0, kModelo = 0x538, kAlmacenes = 0x46C, kVueltas = 0x618;
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
                             {0x00, 204, 0x800},      // el 0x00 comun (una guardia; 0 es la guardia de B3)
                             {0x02, 204, 0x802}, {0x07, 204, 0x807}, {0x08, 204, 0x808},   // reposo y carreras
                             {0x05, 204, 0x805}, {0x06, 204, 0x806},                        // cansado de IW
                             {0x15, 204, 0x815}, {0x16, 204, 0x816},                        // candidatas (auditoria7)
                             {0x22, 204, 0x822}, {0x29, 204, kSubir}, {0x103, 204, 0xA03}, {0x104, 204, kCaer}, {0x105, 204, 0xA05},   // salto (914 es Cell: CEL_*)
                             {0x320, 915}, {0x321, 915}, {0x322, 915}, {0x32D, 915}, {0x323, 915}, {0x324, 915}, {0x328, 915}, {0x329, 915},
                             {0x32A, 915}, {0x32C, 915}, {0x32E, 915},   // vuelo: poses del visor de IW (915)
                             // Cell en el minijuego de Ten Shin Han (auditoria7/I: #BSK de 914, Char_EnterPoseState0-6Alt)
                             {0x640, 914, 0x960}, {0x654, 914, 0x961}, {0x655, 914, 0x962}, {0x656, 914, 0x963},
                             {0x657, 914, 0x964}, {0x658, 914, 0x965}, {0x659, 914, 0x966}, {0x668, 914, 0x967},
                             {0x669, 914, 0x968}, {0x66A, 914, 0x969}, {0x66B, 914, 0x96A}, {0x66C, 914, 0x96B}};   // reposo y andar del jugador de 914 (Gohan nino)
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
uint32_t g_acm = 0, g_tabla = 0, g_base_idx = 0, g_csk = 0, g_csk_idx = 0;

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
  const size_t e = tbl + 16 * k;
  an.flags = Le32(a, e) & ~0x10u;
  an.var = Le32(a, e + 4);
  an.cuadros = Le32(a, e + 8);
  const uint32_t off = Le32(a, e + 12), per = (Le32(a, e) & 0x10) ? 3 : 2;
  if (off + 4 * per * nb > a.size()) return false;
  for (uint32_t j = 0; j < nb; ++j) {
    an.sufijos.push_back(Sufijo(reinterpret_cast<const char*>(a.data() + nombres + 32 * j), 32));
    std::array<uint32_t, 2> pj{0, 0};
    for (uint32_t i = 0; i < 2; ++i) {   // ponytail: sin la pista de escala (per 3); estas no la llevan
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
              "bloque {:X} {:X} {:X} {:X} {:X} {:X} {:X} {:X} {:X}", codigo, k, alm, an.cuadros, nb, an.flags, an.var,
              an.bloque[0], an.bloque[1], an.bloque[2], an.bloque[3], an.bloque[4], an.bloque[5], an.bloque[6],
              an.bloque[7], an.bloque[8]);
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
//  * Visor de vuelo: ->0x32F corta salvo desde 0x330 (Viewer_StateEnterFinalPose 430509-430513),
//    ->0x32B corta salvo desde 0x330 (FlyingMinigameCharacterHeightPosition 431978-431982) y el
//    guion corta a cualquier codigo (Cutscene_GotoLine 432595). ->0x322 y ->0x32C tienen
//    llamadas de las dos clases (430734-430738 y 431990-431994 cortan; 430719, 431370 y
//    Viewer_StatePoseBank5/6 430600/430671 funden) [H]: no se cortan.
bool CorteMezcla(uint32_t de, uint32_t a) {
  if (std::getenv("DBZ3_MEZCLA_FORZADA")) return false;   // prueba: fundir aunque IW corte
  if (de == 0x330) return false;                          // de 0x330 siempre hay rampa (1/15)
  if (de >= kQteBase || a >= kQteBase) return true;       // poses de los minijuegos: corte
  return a == 0x32F || a == 0x32B;
}
// 06: sonidos de los eventos. Los valores de IW son ids de su banco de efectos del IOP
// (AMSOUND, muestras en la SPU2: Hits_TransformationSounds -> FUN_0013f570 -> rpc 0xF), que no
// esta en el AFS (los ADX del juego son voces/musica): no hay fichero que decodificar. Se
// suenan los equivalentes de B3 que ya usa el port con sub_820922A0 (medidos en un combate:
// banco 2 id 2 = impacto; banco 4 ids 19/20/25 = quejido de golpe). [H] en la equivalencia.
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
  if (g_csk && g_acm) {
    const uint32_t bt = REX_LOAD_U32(g_csk + 0x20 + 4 * kTransicion);
    if (bt)
      for (uint32_t w = 0; w < 11; ++w) REX_STORE_U32(bt + 4 + 4 * w, B->bloque[w]);
    for (uint32_t c = 0; c < Total(); ++c)
      if (AnimN(c).codigo == kTransicion) {
        const uint32_t e = Abs(g_tabla, g_acm) + 16 * (g_base_idx + c);
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
                    // en el visor de vuelo el "almacen 0" es su propio #AMM (hijo 0), no el global de 204
                    // y sus poses van de lado como el scatto (+X): con la raiz girada
                    : e.entrada == kIwVuelo ? Copiar(base, an, P::HijoAmb(amb, 3), e.codigo, P::HijoAmb(amb, 0), P::HijoAmb(amb, 0), true)
                                           : Copiar(base, an, P::HijoAmb(amb, 0), e.codigo, global, P::HijoAmb(amb, 1), false);
    if (ok && e.entrada != kIwFid && e.entrada != kIwVuelo && e.codigo >= 0x51 && e.codigo <= 0x6F6 && e.codigo != 0x62C &&
        e.codigo != 0x62D && e.codigo != 0x62E && (e.codigo < 0x640 || e.codigo > 0x66C))
      SinOrigenXZ(base, an);   // entradas y finales de los minijuegos de campo (922): sin la posicion absoluta de IW
    if (ok && e.como) an.codigo = e.como;
    // las de Cell (0x960-0x96B, del #BSK de 914, un esqueleto GOK_* de nino): solo los giros, salvo la cadera; sus
    // posiciones son las longitudes de los huesos de aquel y estiraban a Cell (alas y cola en pinchos, captura 164915)
    if (ok && an.codigo >= 0x960 && an.codigo <= 0x96F)
      for (size_t w = 0; w < an.sufijos.size(); ++w)
        if (an.sufijos[w] != "WAIST") an.pistas[w][1] = 0;
    if (ok && (an.codigo == kCorrer || an.codigo == kQuieto || an.codigo == kAndar || an.codigo == 0x805 ||
               an.codigo == 0x806 || an.codigo == 0x807))
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

// Las animaciones de IW detras de las del almacen global de B3 (tabla nueva = la vieja + 7; no se toca ninguna
// entrada) y un #CSK propio (codigos 0x19..0x1E y 0xE2) en un hueco libre de los 3 del luchador; `hueco` = el que se
// ocupo (0 si ya estaba). false si no se puede.
bool Instalar(uint8_t* base, uint32_t x, uint32_t& hueco) {
  const uint32_t A = x + kAnimObj, modelo = REX_LOAD_U32(A + kModelo);
  const uint32_t acm = modelo ? REX_LOAD_U32(modelo + kAlmacenes) : 0;
  if (!acm || (REX_LOAD_U32(acm) & 0x00FFFFFFu) != kAcm) return false;
  if (acm != g_acm || REX_LOAD_U32(acm + 0x14) != g_tabla) {
    const uint32_t n = REX_LOAD_U32(acm + 0x10), crudo = REX_LOAD_U32(acm + 0x14), tbl = Abs(crudo, acm);
    if (n == 0 || n > 4096) return false;
    const bool rel_tabla = crudo < 0x01000000u;
    const uint32_t viejo = Abs(REX_LOAD_U32(tbl + 12), acm);   // bloque de la animacion 0: punteros absolutos o no
    bool rel = true;
    for (uint32_t k = 0; k < 8; ++k)
      if (const uint32_t p = REX_LOAD_U32(viejo + 4 * k)) {
        rel = p < 0x01000000u;
        break;
      }
    const uint32_t nt = dbz3::roster::GuestAlloc(16 * (n + Total()));
    if (!nt) return false;
    for (uint32_t k = 0; k < 4 * n; ++k) REX_STORE_U32(nt + 4 * k, REX_LOAD_U32(tbl + 4 * k));
    for (uint32_t c = 0; c < Total(); ++c) {
      const uint32_t b = Bloque(base, acm, AnimN(c), rel), e = nt + 16 * (n + c);
      if (!b) return false;
      REX_STORE_U32(e, AnimN(c).flags);
      REX_STORE_U32(e + 4, AnimN(c).var);
      REX_STORE_U32(e + 8, AnimN(c).cuadros);
      REX_STORE_U32(e + 12, rel ? b - acm : b);
    }
    g_tabla = rel_tabla ? nt - acm : nt;
    REX_STORE_U32(acm + 0x14, g_tabla);
    REX_STORE_U32(acm + 0x10, n + Total());
    g_acm = acm;
    g_base_idx = n;
    REXLOG_INFO("dbz3 anim: animaciones de IW en el almacen global de B3 (0x{:08X}): {}..{} (punteros {})", acm, n,
                n + Total() - 1, rel ? "relativos" : "absolutos");
  }
  if (!g_csk || g_csk_idx != g_base_idx) {
    uint32_t n = kCorredor + 1;
    for (const Anim& a : g_ex) n = std::max(n, a.codigo + 1);
    const uint32_t lst = 0x20, blks = lst + 4 * n;
    const uint32_t g = dbz3::roster::GuestAlloc(blks + 48 * Total());
    if (!g) return false;
    const uint32_t cab[8] = {0x2143534Bu /* "!CSK" */, 0x20, 0, 4, n, g + lst, 0, 0};
    for (uint32_t k = 0; k < 8; ++k) REX_STORE_U32(g + 4 * k, cab[k]);
    for (uint32_t c = 0; c < Total(); ++c) {
      const uint32_t b = g + blks + 48 * c;
      REX_STORE_U32(g + lst + 4 * AnimN(c).codigo, b);
      REX_STORE_U16(b, uint16_t(g_base_idx + c));
      REX_STORE_U16(b + 2, 0);
      for (uint32_t w = 0; w < 11; ++w) REX_STORE_U32(b + 4 + 4 * w, AnimN(c).bloque[w]);
    }
    g_csk = g;
    g_csk_idx = g_base_idx;
  }
  hueco = 0;
  for (uint32_t i = 0; i < 3; ++i)
    if (REX_LOAD_U32(A + kBsk + 4 * i) == g_csk) return true;
  for (uint32_t i = 0; i < 3; ++i) {
    const uint32_t s = A + kBsk + 4 * i;
    if (REX_LOAD_U32(s) == 0) {
      REX_STORE_U32(s, g_csk);
      hueco = s;
      if (i > 0) REXLOG_WARN("dbz3 anim: #CSK propio en el hueco {} (0xE2 puede salir del de B3)", i);
      return true;
    }
  }
  REXLOG_WARN("dbz3 anim: el luchador 0x{:08X} no tiene hueco libre de #CSK", x);
  return false;
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
  if (l.hueco && REX_LOAD_U32(l.hueco) == g_csk) REX_STORE_U32(l.hueco, 0);
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
    uint32_t hueco = 0;
    if (!Leer(base) || !Instalar(base, l.x, hueco) ||
        ((l.modo == kModoPose || (l.modo == kModoCorredor && l.codigo && l.codigo != kCorredor)) &&
         std::none_of(g_ex.begin(), g_ex.end(), [&](const Anim& a) { return a.codigo == l.codigo; }))) {
      if (l.modo == kModoPose) g_pose = 2;   // sin esa animacion: el minijuego sigue sin ella
      l.fase = 0;
      continue;
    }
    if (hueco) l.hueco = hueco;
    const uint32_t codigo = l.modo == kModoScatto     ? kPrimero + uint32_t(l.fase - 1) + (l.aire ? 3 : 0)
                            : l.modo == kModoCorredor ? (l.codigo ? l.codigo : kCorredor)
                            : l.modo == kModoPose     ? l.codigo
                                                      : (l.fase == 1 ? 0x805u : 0x806u);
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
      l.cuadro = 0;
      if (const Anim* b = AnimDe(codigo); b && b->cuadros > g_mezcla) {   // B ya avanzo g_mezcla cuadros en la mezcla
        const float sigue = float(g_mezcla);
        uint32_t u;
        std::memcpy(&u, &sigue, 4);
        REX_STORE_U32(l.x + kAnimObj + 0x608, u);
      }
      l.puesto = codigo;
      l.cuadro_ev = float(g_mezcla);   // 06: B ya avanzo g_mezcla cuadros en la mezcla: sin repetir sus eventos
      continue;
    }
    // de una animacion de IW (la que se ve) a otra: primero la transicion (no en el scatto, que va por cuadros)
    if (ahora != codigo && ahora == (l.puesto & 0xFFF) && l.modo != kModoScatto && !std::getenv("DBZ3_SIN_MEZCLA")) {
      if (CorteMezcla(ahora, codigo)) {   // 06: IW cambia esta animacion sin rampa (ver CorteMezcla)
        Arrancar(ctx, base, l.x, codigo, false);
        l.cuadro = 0;
        l.puesto = codigo;
        l.cuadro_ev = -1.0f;
        continue;
      }
      const uint32_t cu = REX_LOAD_U32(l.x + kAnimObj + 0x608);
      float f;
      std::memcpy(&f, &cu, 4);
      if (PrepararTransicion(base, l.puesto, f, codigo)) {
        Arrancar(ctx, base, l.x, kTransicion, false);
        l.destino = codigo;
        l.t_mezcla = 0;
        l.puesto = kTransicion;
        continue;
      }
    }
    if (ahora != codigo) {
      Arrancar(ctx, base, l.x, codigo, false);
      // si el juego la piso (la IA de P4 reentra en andar cada pocos cuadros), sigue por el cuadro en que iba
      if (l.puesto == codigo) REX_STORE_U32(l.x + kAnimObj + 0x608, l.cuadro);
      else l.cuadro = 0;
      if (l.puesto != codigo) l.cuadro_ev = -1.0f;   // 06: animacion nueva: los eventos, desde su cuadro 0
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
}

}  // namespace dbz3::anim

// Solo US (como iw_minijuegos.cpp). sub_8208FAB0: lo llama la lectura del mando de cada usuario (sub_8208FD58) en
// cada cuadro; de aqui sale el contexto para arrancar animaciones. Sin nada pedido no hace nada.
REX_HOOK_RAW(sub_8208FAB0) {
  __imp__sub_8208FAB0(ctx, base);
  dbz3::anim::Tick(ctx, base);
}
