// dbz3 - Cinematicas en el motor de B3 (frente R3). Ver cinematica.h.
//
// Piezas del juego que usa (medidas por otros frentes):
//  - camara: cine::Camara (mgr+0x60 vista, +0x190 ojo, +0x1A0 objetivo; se pisa al empezar el dibujo, cine.cpp);
//  - luchadores: X = mgr + 2096 + 8240 * lado, posicion X+48, mando X+5028 (16 bits, -1 = COM; cine.cpp deja al COM en
//    el mando 0 mientras dura su intro y sin pulsar no hace nada);
//  - mando: registro 184 B en 0x8238B780 + 184 * usuario: +12 pulsados, +14 recien pulsados, +16 soltados, +24
//    palancas, +72 + 4j cuadros pulsado del boton j (bit 0x100 << j), +108..+114 direcciones (combate.cpp);
//  - pose: Y+B = 0xA000 en +12, la carga de ki del propio personaje (tagteam.cpp, relevo).
#include "cinematica.h"

#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "generated/dbz3_init.h"
#include "cine.h"
#include "combate.h"
#include "eu_map.h"
#include "freecam.h"
#include "gigantes.h"
#include "ingame/game_style.h"
#include "launcher/i18n.h"
#include "launcher/ui_kit.h"
#include "launcher/launcher_state.h"

namespace dbz3::net {
void InyectarPad(uint32_t usuario, int32_t botones);   // net/netplay_hooks.cpp
}

// Parejas EU medidas con el metodo de tools/eu_map.py (E:/DBZ3HD_trabajo/P3/eu_par.py, sin reescribir el mapa):
// sub_82114FB8 -> 821150B8 (26 instr., 0 diferencias sin explicar, 67 llamadas bien) y sub_82103978 -> 82103A78
// (46 instr., 0 sin explicar, 63 llamadas bien).
#if defined(DBZ3_DUAL_REGION)
REX_EXTERN(dbz3eu_sub_821150B8);
REX_EXTERN(dbz3eu_sub_82103A78);
REX_EXTERN(dbz3eu_sub_820FF920);
#endif
#define DBZ3_ARRANCA_ANIM DBZ3_CALL2(sub_82114FB8, dbz3eu_sub_821150B8)
#define DBZ3_FIN_ACCION DBZ3_CALL2(sub_82103978, dbz3eu_sub_82103A78)
// DS178: pareja EU de sub_820FF820 medida con el metodo de tools/eu_map.py (mismo cuerpo normalizado, 79 lineas, y las
// mismas llamadas: 82133D50->82133E58, 820FDDE0->820FDEE0, 82165190->82165150; el resto de diferencias son datos).
#define DBZ3_SALIDA_RESULTADO DBZ3_CALL2(sub_820FF820, dbz3eu_sub_820FF920)

namespace dbz3::cinematica {
namespace {

namespace S = dbz3::ingame::style;
constexpr dbz3::GA kMgrPtr = 0x823D6CF4;
constexpr uint32_t kX0 = 2096, kXPaso = 8240, kPos = 48, kMandoIdx = 5028;
float g_dir[7] = {};            // A3b: camara deseada (ojo, objetivo, giro) del cuadro; la aplica AplicarCamaraDirecta
std::atomic<float> g_ancla_d[3] = {};   // DS145: desplazamiento del ancla de camara del cuadro (0 si no hay)
std::atomic<bool> g_ancla_ok{false};
std::atomic<bool> g_dir_ok{false};
// DS178: salto de la victoria/resultado de un combate "solo escena" (armado al acabar la escena; lo consume el gancho
// de la camara en el primer cuadro del estado 5).
std::atomic<bool> g_resultado_rapido{false};
constexpr uint32_t kPasado = 35144;
constexpr uint16_t kAccionPose = 15;   // accion de las poses (entrada 0x280/0x281, victoria 0x2A0; sonda)
constexpr uint32_t kCodigo = 2928;   // codigo de la animacion en curso (sub_82103C48 lo pone y sub_82114FB8 la arranca)   // cuadros de pelea pasados (float; el reloj del combate, pasarela.cpp)
constexpr float kFundido = 0.6f;   // s de paso de un plano a otro (y a la camara del combate al acabar)

std::mutex g_mu;
std::shared_ptr<const Escena> g_pedida, g_esc;   // pedida (la siguiente pelea) / en curso
float g_son_ojo[3] = {}, g_son_obj[3] = {};      // S1 SONDA: camara de prueba en un combate nativo (sin escena)
std::atomic<bool> g_son_ok{false};
std::atomic<bool> g_en_curso{false}, g_saltar{false}, g_conectada{false};
std::atomic<bool> g_siguiente{false};   // A: el paso siguiente sin esperar (START salta la escena)
std::atomic<int> g_paso{-1};
bool g_oculto2 = false;   // DS31: hemos puesto X+3261/X+4925 al lado 2 (se devuelven en Terminar)
std::atomic<float> g_abre{0};   // 0..1 de las bandas (panel)
uint32_t g_frame = 0;
int64_t g_t0 = 0, g_t0_paso = 0;   // ms (reloj de pared: las voces van en tiempo real aunque el juego baje de 60)
int64_t AhoraMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int16_t g_mando[2] = {-1, -1};   // X+5028 de cada lado al empezar (-1 = COM)
bool g_cambiado[2] = {};
int g_usuario[2] = {-1, -1};      // mando (usuario de XInput) virtual de cada lado mientras dura
uint32_t g_codigo0[2] = {};       // X+2928 de cada lado al empezar (la guardia a la que se vuelve)
bool g_puesto[2] = {};            // Tick puso una animacion a ese lado
uint32_t g_pasado = 0;  // mgr+35144 al empezar: el reloj se queda parado mientras dura la escena
struct V3 {
  float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
V3 Mezcla(V3 a, V3 b, float t) { return a + (b - a) * t; }
V3 Unit(V3 a) {
  const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
  return l > 1e-6f ? a * (1.0f / l) : V3{0, 0, -1};
}
float Suave(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
// Valor de una pista de camara {cuadro, v...} en el cuadro f (lineal, como el juego; 1 clave por cuadro).
template <size_t N>
bool MuestraCam(const std::vector<std::array<float, N>>& k, float f, float* out) {
  if (k.empty()) return false;
  size_t i = 0;
  while (i + 1 < k.size() && k[i + 1][0] <= f) ++i;
  const size_t j = std::min(i + 1, k.size() - 1);
  const float d = k[j][0] - k[i][0], t = d > 0 ? std::clamp((f - k[i][0]) / d, 0.0f, 1.0f) : 0.0f;
  for (size_t c = 1; c < N; ++c) out[c - 1] = k[i][c] + (k[j][c] - k[i][c]) * t;
  return true;
}
void PonV(uint8_t* base, uint32_t x, V3 p) {   // la posicion va tres veces (X+48/+64/+80; tagteam.cpp StepSlide)
  for (uint32_t o : {48u, 64u, 80u}) {
    const float f[3] = {p.x, p.y, p.z};
    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t u;
      std::memcpy(&u, &f[c], 4);
      REX_STORE_U32(x + o + 4 * c, u);
    }
  }
}
V3 LeeV(uint8_t* base, uint32_t a) {
  float f[3];
  for (uint32_t c = 0; c < 3; ++c) {
    const uint32_t u = REX_LOAD_U32(a + 4 * c);
    std::memcpy(&f[c], &u, 4);
  }
  return {f[0], f[1], f[2]};
}
// DS185: punto de mundo del hueso del anclaje `i` (0 cintura, 3 cabeza) del luchador `x`. Sale del esqueleto de la
// logica ([x+2952]+1032: 64 B por hueso; indice del anclaje en x+2956 + 2*i, ELF US 0x82333AC0: 0 WAIST, 1 STMC,
// 2 NECK, 3 HEAD; DS154) pasado a mundo con la matriz del luchador (x+0, filas 0..2) y su posicion x+48, como
// iw_minijuegos.cpp HuesoMundo / DS141. Es la POSE que se dibuja (la guardia baja la cadera ~3 u), no la tabla de
// puntos X+1616: esa solo se refresca en los estados 3/4/5/8 del combate (sub_820F9548) y en la llegada conserva la
// escena anterior (y~241 medido en DS178: con ella la camara subia a 247 y el paisaje quedaba vacio). false si aun
// no hay modelo/tabla o el punto es nulo/no finito.
bool PuntoCuerpo(uint8_t* base, uint32_t x, int i, V3& p) {
  const uint32_t mi = REX_LOAD_U32(x + 2952), anc = REX_LOAD_U32(x + 2956);
  if (!mi || !anc) return false;
  const uint32_t tb = REX_LOAD_U32(mi + 1032);
  if (tb < 0x40000000u || tb >= 0xA0000000u) return false;
  const int hb = int16_t(REX_LOAD_U16(anc + 2u * uint32_t(i)));
  if (hb < 0 || hb > 255) return false;
  const uint32_t h = tb + 64u * uint32_t(hb);
  if (h < 0x40000000u || h >= 0xA0000000u) return false;
  const V3 r0 = LeeV(base, x), r1 = LeeV(base, x + 16), r2 = LeeV(base, x + 32);
  const V3 v = LeeV(base, h + 48), t = LeeV(base, x + 48);
  p = {r0.x * v.x + r0.y * v.y + r0.z * v.z + t.x, r1.x * v.x + r1.y * v.y + r1.z * v.z + t.y,
       r2.x * v.x + r2.y * v.y + r2.z * v.z + t.z};
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z) > 1e-3f;
}
// DS186: la misma regla de DS185, para las tomas cuya camara no va anclada a una trayectoria del mundo: objetivo de B1
// -> MISMA fraccion de la recta cintura->cabeza del cuerpo DIBUJADO (esqueleto animado de la escena). `cadera_b1` es
// la cintura de B1 en el mundo de la escena y `cabeza_b1` la cabeza de B1 sobre su cintura. El ojo se desplaza lo mismo
// (el angulo vertical de la toma no cambia). Si los huesos no son los del luchador, no toca nada (false).
bool AjustarObjetivoCuerpo(uint8_t* base, uint32_t mgr, int lado, float cadera_b1, float cabeza_b1, V3& ojo, V3& obj,
                           const char* que) {
  if (lado < 0 || lado > 1 || cabeza_b1 < 0.5f) return false;
  const uint32_t x = mgr + kX0 + kXPaso * uint32_t(lado);
  V3 cintura{}, cabeza{};
  if (!PuntoCuerpo(base, x, 0, cintura) || !PuntoCuerpo(base, x, 3, cabeza)) return false;
  const V3 pies = LeeV(base, x + kPos);
  // mismas guardas que DS185: el punto tiene que ser del luchador (no la tabla vieja X+1616 ni otro cuerpo).
  if (std::fabs(cabeza.y - cintura.y) < 2.0f || std::fabs(cabeza.y - cintura.y) > 12.0f ||
      std::fabs(cintura.x - pies.x) > 20.0f || std::fabs(cintura.z - pies.z) > 20.0f ||
      std::fabs(cabeza.x - pies.x) > 20.0f || std::fabs(cabeza.z - pies.z) > 20.0f ||
      cintura.y < pies.y + 3.0f || cintura.y > pies.y + 15.0f)
    return false;
  const float f = (obj.y - cadera_b1) / cabeza_b1;
  const float y = cintura.y + f * (cabeza.y - cintura.y);
  const float dy = y - obj.y;
  ojo.y += dy;
  obj.y += dy;
  static const int sonda = [] {
    const char* v = std::getenv("DBZ3_TEST_PROPS_LLEGADA_LOG");
    return v ? std::max(1, std::atoi(v)) : 0;
  }();
  if (sonda && g_frame % 60 == 0)
    REXLOG_INFO("dbz3 cinematica: cuerpo {} cuadro {}: cintura dibujada ({:.1f} {:.1f} {:.1f}) cabeza ({:.1f} {:.1f} "
                "{:.1f}) cintura B1 {:.2f} f {:.3f}; objetivo y {:.2f} -> {:.2f}", que, g_frame, cintura.x, cintura.y,
                cintura.z, cabeza.x, cabeza.y, cabeza.z, cadera_b1, f, obj.y - dy, y);
  return true;
}

// DS16: pose de brazos de B1 sobre el esqueleto de B3. Las direcciones (unidad, en el espacio del modelo) del hueso del
// brazo y del antebrazo de cada lado, medidas en la toma 1 de B1 (lado 1, clip 0xC0) a 10 Hz. El cuerpo de B3 sigue su
// clip; solo se giran las cadenas de brazo para que la silueta sea la de B1 (brazos abiertos -> manos arriba -> guardia).
constexpr float kBrazoB1[31][12] = {
    {0.479f, -0.877f, 0.034f, 0.344f, -0.852f, 0.395f, -0.247f, -0.876f, -0.414f, -0.502f, -0.854f, 0.137f},
    {0.476f, -0.879f, 0.032f, 0.329f, -0.857f, 0.396f, -0.257f, -0.876f, -0.408f, -0.500f, -0.857f, 0.124f},
    {0.479f, -0.878f, 0.023f, 0.311f, -0.863f, 0.399f, -0.274f, -0.874f, -0.401f, -0.497f, -0.862f, 0.099f},
    {0.493f, -0.870f, 0.016f, 0.304f, -0.864f, 0.402f, -0.295f, -0.870f, -0.396f, -0.497f, -0.865f, 0.067f},
    {0.537f, -0.844f, 0.013f, 0.330f, -0.847f, 0.416f, -0.316f, -0.860f, -0.400f, -0.507f, -0.861f, 0.031f},
    {0.558f, -0.829f, 0.020f, 0.354f, -0.834f, 0.422f, -0.319f, -0.850f, -0.419f, -0.520f, -0.854f, 0.015f},
    {0.638f, -0.760f, 0.122f, 0.491f, -0.751f, 0.441f, -0.368f, -0.715f, -0.594f, -0.612f, -0.767f, -0.191f},
    {0.752f, -0.641f, 0.152f, 0.584f, -0.627f, 0.516f, -0.472f, -0.561f, -0.680f, -0.684f, -0.622f, -0.381f},
    {0.882f, -0.447f, 0.151f, 0.714f, -0.441f, 0.544f, -0.588f, -0.350f, -0.729f, -0.857f, -0.421f, -0.298f},
    {0.960f, -0.173f, 0.222f, 0.901f, -0.141f, 0.410f, -0.786f, -0.084f, -0.612f, -0.956f, -0.071f, -0.283f},
    {0.986f, -0.016f, 0.167f, 0.862f, 0.177f, 0.476f, -0.806f, 0.067f, -0.588f, -0.934f, 0.213f, -0.286f},
    {0.884f, 0.354f, 0.306f, 0.712f, 0.602f, 0.361f, -0.777f, 0.287f, -0.561f, -0.711f, 0.642f, -0.287f},
    {0.731f, 0.592f, 0.339f, 0.285f, 0.940f, 0.186f, -0.733f, 0.439f, -0.519f, -0.349f, 0.918f, -0.191f},
    {0.270f, 0.917f, 0.292f, -0.072f, 0.984f, -0.163f, -0.638f, 0.752f, -0.168f, 0.163f, 0.974f, -0.155f},
    {0.127f, 0.956f, 0.264f, -0.128f, 0.973f, -0.191f, -0.563f, 0.826f, -0.015f, 0.282f, 0.949f, -0.140f},
    {-0.154f, 0.927f, 0.341f, -0.313f, 0.934f, -0.174f, -0.328f, 0.879f, 0.346f, 0.428f, 0.902f, -0.056f},
    {-0.381f, 0.706f, 0.598f, -0.549f, 0.835f, -0.032f, -0.168f, 0.705f, 0.690f, 0.521f, 0.842f, 0.142f},
    {-0.487f, 0.442f, 0.754f, -0.715f, 0.673f, 0.188f, -0.199f, 0.416f, 0.887f, 0.571f, 0.735f, 0.364f},
    {-0.517f, 0.149f, 0.843f, -0.877f, 0.443f, 0.185f, -0.302f, 0.096f, 0.949f, 0.515f, 0.553f, 0.655f},
    {-0.500f, -0.034f, 0.865f, -0.956f, 0.292f, 0.038f, -0.443f, -0.069f, 0.894f, 0.481f, 0.445f, 0.756f},
    {-0.499f, -0.306f, 0.811f, -0.969f, 0.097f, -0.226f, -0.657f, -0.259f, 0.708f, 0.350f, 0.285f, 0.892f},
    {-0.478f, -0.519f, 0.709f, -0.953f, -0.008f, -0.304f, -0.844f, -0.336f, 0.418f, 0.145f, 0.150f, 0.978f},
    {-0.381f, -0.700f, 0.604f, -0.956f, -0.107f, -0.272f, -0.949f, -0.313f, 0.023f, -0.193f, -0.022f, 0.981f},
    {-0.239f, -0.796f, 0.557f, -0.974f, -0.153f, -0.169f, -0.930f, -0.253f, -0.267f, -0.380f, -0.126f, 0.917f},
    {-0.160f, -0.821f, 0.547f, -0.980f, -0.166f, -0.106f, -0.894f, -0.217f, -0.391f, -0.429f, -0.156f, 0.890f},
    {0.099f, -0.834f, 0.543f, -0.965f, -0.228f, 0.129f, -0.672f, -0.056f, -0.738f, -0.601f, -0.228f, 0.766f},
    {0.262f, -0.777f, 0.573f, -0.896f, -0.319f, 0.309f, -0.422f, 0.094f, -0.902f, -0.740f, -0.265f, 0.618f},
    {0.336f, -0.715f, 0.614f, -0.803f, -0.395f, 0.446f, -0.222f, 0.236f, -0.946f, -0.796f, -0.299f, 0.527f},
    {0.368f, -0.687f, 0.627f, -0.789f, -0.412f, 0.456f, -0.188f, 0.266f, -0.945f, -0.801f, -0.310f, 0.512f},
    {0.368f, -0.687f, 0.626f, -0.789f, -0.415f, 0.453f, -0.188f, 0.268f, -0.945f, -0.801f, -0.313f, 0.511f},
    {0.369f, -0.688f, 0.625f, -0.789f, -0.416f, 0.452f, -0.188f, 0.269f, -0.945f, -0.800f, -0.315f, 0.510f},
};
float LeeBoneF(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void PonBoneF(uint8_t* base, uint32_t a, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}
void RotDe(const double a[3], const double b[3], double R[9]) {   // giro minimo que lleva a en b (filas)
  const double v[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  const double c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const double s = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (s < 1e-8) {
    if (c > 0) {
      for (int i = 0; i < 9; ++i) R[i] = (i % 4 == 0) ? 1.0 : 0.0;
    } else {   // 180 grados: eje perpendicular a a
      double e[3] = {0, 0, 0};
      if (std::fabs(a[0]) < 0.9) e[0] = 1; else e[1] = 1;
      const double k[3] = {a[1] * e[2] - a[2] * e[1], a[2] * e[0] - a[0] * e[2], a[0] * e[1] - a[1] * e[0]};
      const double l = std::sqrt(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]);
      const double x = k[0] / l, y = k[1] / l, z = k[2] / l;
      R[0] = 2 * x * x - 1; R[1] = 2 * x * y; R[2] = 2 * x * z;
      R[3] = 2 * x * y; R[4] = 2 * y * y - 1; R[5] = 2 * y * z;
      R[6] = 2 * x * z; R[7] = 2 * y * z; R[8] = 2 * z * z - 1;
    }
    return;
  }
  const double x = v[0] / s, y = v[1] / s, z = v[2] / s, C = 1.0 - c;
  R[0] = c + x * x * C; R[1] = x * y * C - z * s; R[2] = x * z * C + y * s;
  R[3] = y * x * C + z * s; R[4] = c + y * y * C; R[5] = y * z * C - x * s;
  R[6] = z * x * C - y * s; R[7] = z * y * C + x * s; R[8] = c + z * z * C;
}
void GiraHueso(uint8_t* base, uint32_t tb, int i, const double R[9], const double piv[3]) {
  const uint32_t a = tb + 64 * uint32_t(i);
  double m[16];
  for (int k = 0; k < 16; ++k) m[k] = LeeBoneF(base, a + 4 * uint32_t(k));
  double R0[9] = {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
  double t[3] = {m[12], m[13], m[14]};
  double Rn[9], tn[3];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) Rn[3 * r + c] = R[3 * r] * R0[c] + R[3 * r + 1] * R0[3 + c] + R[3 * r + 2] * R0[6 + c];
  for (int r = 0; r < 3; ++r)
    tn[r] = piv[r] + R[3 * r] * (t[0] - piv[0]) + R[3 * r + 1] * (t[1] - piv[1]) + R[3 * r + 2] * (t[2] - piv[2]);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) PonBoneF(base, a + 4 * uint32_t(4 * r + c), float(Rn[3 * r + c]));
    PonBoneF(base, a + 4 * uint32_t(4 * r + 3), 0.0f);   // la 4.a columna de una afin: 0
  }
  PonBoneF(base, a + 48, float(tn[0]));
  PonBoneF(base, a + 52, float(tn[1]));
  PonBoneF(base, a + 56, float(tn[2]));
}
void AplicaBrazosB1(uint8_t* base, uint32_t x, float t) {
  const uint32_t mi = REX_LOAD_U32(x + 2952);
  const uint32_t tb = mi ? REX_LOAD_U32(mi + 1032) : 0;
  if (!tb) return;
  float k[12];
  const float f = std::clamp(t * 10.0f, 0.0f, 30.0f);
  const int i = std::min(int(f), 29);
  const float u = f - float(i);
  for (int c = 0; c < 12; ++c) k[c] = kBrazoB1[i][c] + (kBrazoB1[i + 1][c] - kBrazoB1[i][c]) * u;
  // indices del #AMM de Mr. Satan (56 huesos, ficha 14): 11..16 cadena derecha (RARMROT..NRA) y 32..37 izquierda
  // (LARMROT..NLA); el codo es 13/34 y la muneca 14/35. Solo se llama para el lado 1 de la toma 1 de Satán.
  struct Lado { int raiz, codo, muneca, n, cadena[8]; };
  static const Lado lados[2] = {{32, 34, 35, 6, {32, 33, 34, 35, 36, 37}}, {11, 13, 14, 6, {11, 12, 13, 14, 15, 16}}};
  for (int L = 0; L < 2; ++L) {
    const Lado& q = lados[L];
    const uint32_t pr = tb + 64 * uint32_t(q.raiz), pc = tb + 64 * uint32_t(q.codo), pm = tb + 64 * uint32_t(q.muneca);
    double piv[3] = {LeeBoneF(base, pr + 48), LeeBoneF(base, pr + 52), LeeBoneF(base, pr + 56)};
    double elb[3] = {LeeBoneF(base, pc + 48), LeeBoneF(base, pc + 52), LeeBoneF(base, pc + 56)};
    double d1[3] = {k[L * 6], k[L * 6 + 1], k[L * 6 + 2]};
    double d2[3] = {k[L * 6 + 3], k[L * 6 + 4], k[L * 6 + 5]};
    double a[3], b[3], R[9];
    for (int c = 0; c < 3; ++c) a[c] = elb[c] - piv[c];
    const double la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (la < 1e-6) continue;
    for (int c = 0; c < 3; ++c) a[c] /= la;
    RotDe(a, d1, R);
    for (int j = 0; j < q.n; ++j) GiraHueso(base, tb, q.cadena[j], R, piv);
    double elb2[3] = {LeeBoneF(base, pc + 48), LeeBoneF(base, pc + 52), LeeBoneF(base, pc + 56)};
    double mun[3] = {LeeBoneF(base, pm + 48), LeeBoneF(base, pm + 52), LeeBoneF(base, pm + 56)};
    for (int c = 0; c < 3; ++c) b[c] = mun[c] - elb2[c];
    const double lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (lb < 1e-6) continue;
    for (int c = 0; c < 3; ++c) b[c] /= lb;
    RotDe(b, d2, R);
    for (int j = 2; j < q.n; ++j) GiraHueso(base, tb, q.cadena[j], R, elb2);
  }
}

// Marcador del combate (barras, caras, reloj): lo esconde el propio juego (combate.h, frente P4).
void OcultarHud(bool ocultar) { dbz3::combate::OcultarHud(ocultar); }

// Plano p con los luchadores en p1 y p2 (unidades de B3; un luchador mide ~12).
// s1, s2: escala de dibujo de cada lado (gigantes.cpp, 1 = normal). El gigante se agranda DESDE LOS PIES (fila 3 de
// su matriz intacta, escenarios.md R2): con el ojo y el objetivo a s veces su distancia de los pies y el mismo fov, la
// imagen es la de un luchador normal (triangulos semejantes): sale entero sin estimar nada (studies 8 fase 2).
void Encuadre(int p, V3 p1, V3 p2, V3* ojo, V3* obj, int pose = 0, float s1 = 1.0f, float s2 = 1.0f) {
  const V3 c = Mezcla(p1, p2, 0.5f), eje = Unit(V3{p2.x - p1.x, 0, p2.z - p1.z});
  const V3 lado{-eje.z, 0, eje.x};   // perpendicular horizontal
  if (p == kGeneral) {
    const float s = std::max(s1, s2);   // general: los dos; el mas alto manda
    *ojo = c + lado * (48.0f * s) + V3{0, 14.0f * s, 0};
    *obj = c + V3{0, 8.0f * s, 0};
    return;
  }
  const V3 yo = p == kLado1 ? p1 : p2, frente = p == kLado1 ? eje : eje * -1.0f;   // de cara a su rival
  // casi de lado: el otro fuera. El luchador entero por encima de la banda del subtitulo: X+48 es el suelo y mide
  // ~14 (medido en las capturas 20261016-025250); la zona libre (entre las bandas) tiene su centro 48 px por encima
  // del de la pantalla, asi que se apunta un poco por debajo del pecho. Con pose, mas lejos (la victoria se agacha y
  // senala, la entrada sube los brazos); la victoria ademas gira al personaje: la camara va al otro costado.
  const float l = (p == kLado1 ? 1.0f : -1.0f) * (pose == 2 ? -1.0f : 1.0f);
  // con pose a 46 (a 38 la victoria de Satan salia sin pies en el plano: auditoria7/G, captura c2_paso)
  const float s = p == kLado1 ? s1 : s2;
  *ojo = yo + (Unit(frente * 0.35f + lado * l) * (pose ? 46.0f : 28.0f) + V3{0, pose ? 9.0f : 8.0f, 0}) * s;
  *obj = yo + V3{0, (pose ? 7.0f : 6.0f) * s, 0};   // con pose, sitio para los altos (Ten Shin Han) y el brazo arriba
}

void Terminar(uint8_t* base, const char* por) {
  g_dir_ok = false;
  if (!g_en_curso.exchange(false)) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  for (uint32_t k = 0; k < 2; ++k) {
    const uint32_t x = mgr + kX0 + kXPaso * k;
    if (mgr && g_cambiado[k] && int16_t(REX_LOAD_U16(x + kMandoIdx)) == g_usuario[k])
      REX_STORE_U16(x + kMandoIdx, uint16_t(g_mando[k]));
    if (g_usuario[k] >= 0) dbz3::net::InyectarPad(uint32_t(g_usuario[k]), -1);
    g_cambiado[k] = false;
    g_usuario[k] = -1;
  }
  if (mgr && g_oculto2) {   // DS31: se devuelve el cuerpo del lado 2 que oculto el paso
    const uint32_t x2 = mgr + kX0 + kXPaso;
    REX_STORE_U8(x2 + 3261, 0);
    REX_STORE_U8(x2 + 4925, 0);
    g_oculto2 = false;
  }
  OcultarHud(false);
  g_paso = -1;
  g_abre = 0;
  std::function<void()> fin;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_esc) fin = g_esc->al_terminar_escena;
    g_esc.reset();
  }
  if (fin) fin();   // S1: fuera del cerrojo (devuelve la mezcla de B3)
  REXLOG_INFO("dbz3 cinematica: {} en el cuadro {}", por, g_frame);
}

// ---- Panel (hilo de la interfaz): bandas, retrato, subtitulo y "A: saltar" ----
struct Tex {
  std::unique_ptr<rex::ui::ImmediateTexture> t;
  float w = 0, h = 0;
};
class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!g_en_curso.load()) {
      a_antes_ = start_antes_ = true;
      return;
    }
    if (auto* rt = rex::Runtime::instance())
      if (auto* in = static_cast<rex::input::InputSystem*>(rt->input_system())) {
        rex::input::X_INPUT_STATE st = {};
        const bool ok = in->GetStateForUI(0, &st) == 0;
        const bool a = ok && (st.gamepad.buttons & 0x1000), start = ok && (st.gamepad.buttons & 0x0010);
        if (a && !a_antes_) g_siguiente = true;
        if (start && !start_antes_) g_saltar = true;
        a_antes_ = a;
        start_antes_ = start;
      }
    std::shared_ptr<const Escena> e;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      e = g_esc;
    }
    const int paso = g_paso.load();
    if (e.get() != esc_ || paso != paso_) {   // textura del subtitulo del paso nuevo
      esc_ = e.get();
      paso_ = paso;
      texto_ = Tex{};
      auto* dr = dbz3::launcher::PreviewDrawer();
      if (e && paso >= 0 && paso < int(e->pasos.size()) && e->pasos[size_t(paso)].texto && dr) {
        const pasarela::Imagen& im = e->pasos[size_t(paso)].texto;
        texto_.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                     im.rgba.data());
        texto_.w = float(im.w);
        texto_.h = float(im.h);
        // letra clara (avisos de B1, para su franja oscura) o oscura (charlas, para el bocadillo claro)
        uint64_t luz = 0, n = 0;
        for (size_t i = 0; i + 3 < im.rgba.size(); i += 4)
          if (im.rgba[i + 3] > 64) luz += im.rgba[i] + im.rgba[i + 1] + im.rgba[i + 2], ++n;
        claro_ = !n || luz / (3 * n) > 100;   // por defecto franja oscura (subtitulos de IW: blanco con borde)
      }
    }
    const S::Area a = S::GameArea(io);
    const float w = io.DisplaySize.x, h = io.DisplaySize.y, abre = Suave(g_abre.load());
    const float arriba = (a.p0.y + 90.0f * a.s) * abre, abajo = (h - a.At(0, 690).y) * abre;   // el marcador ya no sale
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const bool estilo_b1 = e && paso >= 0 && paso < int(e->pasos.size()) && e->pasos[size_t(paso)].estilo_b1;   // A3b: como B1: imagen entera, sin bandas, subtitulo blanco con borde negro abajo
    if (!estilo_b1) {
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, arriba), IM_COL32(0, 0, 0, 255));
      dl->AddRectFilled(ImVec2(0, h - abajo), ImVec2(w, h), IM_COL32(0, 0, 0, 255));
    }
    const S::Imagen g = S::GuestImage(io);   // S1-F06: la ayuda en la banda de arriba de la imagen del juego
    // DS13: los pasos que en B1 no llevan rotulo (las tomas del READY) lo apagan; el resto sigue igual
    const bool ayuda = !(e && paso >= 0 && paso < int(e->pasos.size()) && e->pasos[size_t(paso)].sin_ayuda);
    if (ayuda)
      S::HelpLine(dl, ImVec2(g.p0.x + g.Size().x * 0.80f, g.p0.y + 45.0f * a.s * abre), S::mode::kSmall * a.s,
                  {{"A", dbz3::i18n::T("Siguiente", "Next")}, {"START", dbz3::i18n::T("Saltar", "Skip")}});
    if (abre < 0.99f || !e || paso < 0 || paso >= int(e->pasos.size())) return;
    const Paso& p = e->pasos[size_t(paso)];
    if (estilo_b1) {
      if (!p.linea.empty()) {
        ImFont* fuente = dbz3::launcher::ui::GetFonts().menu ? dbz3::launcher::ui::GetFonts().menu : ImGui::GetFont();   // la fuente de los menus del juego (la de sus textos de historia)
        const S::Imagen gi = S::GuestImage(io);
        float tam = gi.Size().y * 0.052f;   // B1: letra de ~5 % del alto de la imagen, centrada, a ~86 % del alto
        {
          const float ancho = fuente->CalcTextSizeA(tam, 1e9f, 0.0f, p.linea.c_str()).x, maximo = gi.Size().x * 0.90f;
          if (ancho > maximo) tam *= maximo / ancho;   // las lineas largas se encogen para caber
        }
        const ImVec2 sz = fuente->CalcTextSizeA(tam, 1e9f, 0.0f, p.linea.c_str());
        const ImVec2 pos(gi.p0.x + (gi.Size().x - sz.x) * 0.5f, gi.p0.y + gi.Size().y * 0.86f - sz.y * 0.5f);
        const float o = std::max(1.0f, tam * 0.06f);
        for (const ImVec2 d : {ImVec2(-o, 0), ImVec2(o, 0), ImVec2(0, -o), ImVec2(0, o), ImVec2(-o, -o), ImVec2(o, o), ImVec2(-o, o), ImVec2(o, -o)})
          dl->AddText(fuente, tam, ImVec2(pos.x + d.x, pos.y + d.y), IM_COL32(0, 0, 0, 255), p.linea.c_str());
        dl->AddText(fuente, tam, pos, IM_COL32(255, 255, 255, 255), p.linea.c_str());
      }
      return;
    }
    // subtitulo en la banda de abajo: retrato a la izquierda (cara del marcador) y la imagen o el texto
    const float y0 = h - abajo + S::mode::kPad * a.s, alto = abajo - 2 * S::mode::kPad * a.s;
    float x = a.At(S::mode::kX0 - 220, 0).x;
    ImTextureID cara_tex = p.cara >= 0 ? pasarela::CaraDe(p.cara) : ImTextureID{};
    if (!cara_tex && p.cara_alt >= 0) cara_tex = pasarela::CaraDe(p.cara_alt);
    if (p.cara >= 0 || p.cara_alt >= 0)
      if (ImTextureID c = cara_tex) {
        dl->AddImage(c, ImVec2(x, y0), ImVec2(x + alto, y0 + alto));
        x += alto + S::mode::kGap * a.s;
      }
    if (texto_.t) {   // fondo como el del juego de origen: bocadillo claro para letra oscura, franja oscura para clara
      const float k = std::min(alto / texto_.h, (a.At(S::mode::kX1 + 220, 0).x - x) / texto_.w);
      const ImVec2 q0(x, y0 + (alto - texto_.h * k) * 0.5f), q1(x + texto_.w * k, q0.y + texto_.h * k);
      dl->AddRectFilled(ImVec2(q0.x - 8 * a.s, q0.y - 6 * a.s), ImVec2(q1.x + 8 * a.s, q1.y + 6 * a.s),
                        claro_ ? IM_COL32(8, 30, 48, 230) : IM_COL32(236, 232, 220, 235), 10 * a.s);
      dl->AddImage(reinterpret_cast<ImTextureID>(texto_.t.get()), q0, q1);
    } else if (!p.linea.empty()) {
      dl->AddText(nullptr, S::mode::kRow * a.s, ImVec2(x, y0 + alto * 0.3f), IM_COL32(255, 255, 255, 255), p.linea.c_str());
    }
  }

 private:
  bool a_antes_ = true, start_antes_ = true;
  const Escena* esc_ = nullptr;
  int paso_ = -2;
  Tex texto_;
  bool claro_ = false;
};
void CrearPanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

void EmpezarPaso(int k, const Escena& e) {
  g_paso = k;
  g_t0_paso = AhoraMs();
  const Paso& p = e.pasos[size_t(k)];
  if (!p.voces.empty() && !e.iso_voces.empty()) pasarela::OirVozB1(e.iso_voces, p.voces, e.volumen);
  if (p.al_empezar) p.al_empezar();
  REXLOG_INFO("dbz3 cinematica: paso {}/{} (plano {}, {:.1f} s, botones {:04X}/{:04X}, voces {})", k + 1, e.pasos.size(),
              p.plano, p.segundos, p.botones[0], p.botones[1], p.voces.size());
}

}  // namespace

void Empezar(Escena e) {
  static const bool sin = std::getenv("DBZ3_SIN_CINE") != nullptr;   // pruebas que pelean desde el primer cuadro
  if (sin) return;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_pedida = e.pasos.empty() ? nullptr : std::make_shared<const Escena>(std::move(e));
  }
  static bool panel = false;
  if (!panel) {
    panel = true;
    dbz3::launcher::DeferUi(&CrearPanel);
  }
}
bool Activa() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_pedida || g_en_curso.load();
}
void Cancelar() {
  std::lock_guard<std::mutex> lk(g_mu);
  g_pedida.reset();
  g_saltar = g_en_curso.load();
  g_siguiente = false;
}

bool Conectada() { return g_conectada.load(); }

// Hilo del juego, con contexto (net/netplay_hooks.cpp, al leer el mando virtual): la animacion de cada lado como la
// arranca el propio juego (sub_82103C48: accion anterior en X+3246, accion nueva en X+3244, sub_82103978(X, 0),
// codigo en X+2928 y sub_82114FB8(X)). Las poses van en la accion 15 (la de la entrada y la victoria, medido con la
// sonda); cuando acaba y el juego vuelve a la 0, se repite mientras dura la linea. Al pasar a un paso sin pose, la
// guardia (accion 0, codigo del principio). US y EU (parejas arriba).
void Arrancar(PPCContext& ctx, uint8_t* base, uint32_t x, uint16_t accion, uint32_t codigo) {
  const uint16_t antes = REX_LOAD_U16(x + 3244);
  REX_STORE_U16(x + 3244, accion);
  REX_STORE_U16(x + 3246, antes);
  REX_STORE_U16(x + 3248, 0);
  PPCContext c = ctx;
  c.r3.u64 = x;
  c.r4.u64 = 0;
  DBZ3_FIN_ACCION(c, base);
  REX_STORE_U32(x + kCodigo, codigo);
  c = ctx;
  c.r3.u64 = x;
  DBZ3_ARRANCA_ANIM(c, base);
}
void Tick(PPCContext& ctx, uint8_t* base) {
  static uint32_t hecho = ~0u;
  // pose de cada lado puesta en este paso (escena, paso, cuadro): al acabarla el juego vuelve a la accion 0 y se volvia
  // a poner sin fin (la de victoria cada 3,98 s: Satan repetia el gesto ~3 veces; auditoria10/D)
  static const Escena* armada_e = nullptr;
  static int armada_k[2] = {-1, -1};
  static uint32_t armada_c[2] = {};
  if (!g_en_curso.load() || hecho == g_frame) return;
  hecho = g_frame;
  std::shared_ptr<const Escena> e;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    e = g_esc;
  }
  const int k = g_paso.load();
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!e || !mgr || k < 0 || k >= int(e->pasos.size())) return;
  for (uint32_t s = 0; s < 2; ++s) {
    const uint32_t x = mgr + kX0 + kXPaso * s;
    const int pose = e->pasos[size_t(k)].anim[s];
    // sin pose: no se fuerza la guardia (al acabar la suya el juego vuelve solo a la accion 0; forzarla a mitad dejaba
    // al luchador corriendo de vuelta, captura 20261016-035508 c4)
    if (pose < 0) continue;
    if (e.get() != armada_e) armada_e = e.get(), armada_k[0] = armada_k[1] = -1;
    // una vez por paso (los 30 primeros cuadros puede reintentar si el juego aun no la tomo)
    if (armada_k[s] == k && g_frame - armada_c[s] > 30) continue;
    const uint16_t accion = pose >= 0 ? kAccionPose : 0;
    const uint32_t quiero = pose >= 0 ? uint32_t(pose) : g_codigo0[s];
    g_puesto[s] = pose >= 0;
    if (REX_LOAD_U16(x + 3244) == accion && (REX_LOAD_U32(x + kCodigo) & 0xFFF) == quiero) continue;
    if (armada_k[s] != k) armada_k[s] = k, armada_c[s] = g_frame;
    Arrancar(ctx, base, x, accion, quiero);
    REXLOG_INFO("dbz3 cinematica: lado {} accion {} animacion {:03X} (paso {}, cuadro {})", s + 1, accion, quiero, k + 1,
                g_frame);
  }
}

// S1 SONDA: barrido del campo de la expresion de la cara del luchador 1 (Satan) con la camara en su cabeza, en
// cualquier combate (con o sin escena: manda sobre la camara de la escena). El fotograma del dibujo escribe el campo y
// el gancho de la camara (sub_820F47E0) pone el primer plano.
//   DBZ3_TEST_CARA=<campo>,<ms>,<v1>,<v2>...  campo del registro X del luchador 1 y valores en ciclo
//   DBZ3_TEST_CARA_CAMARA=0   no se toca la camara (se ve la del juego); ESPERA=<cuadros> espera al empezar la pelea
//   DBZ3_TEST_CARA_DIST=<u>   distancia de la camara a la cabeza (def. 4,5; _LADO=<u> de lado, def. 1,2)
//   DBZ3_TEST_CARA_DURACION=<ms>  apaga la camara a los N ms (0 = hasta el final)
//   DBZ3_TEST_CARA_HUESO=codo_der|mano_der|codo_izq|mano_izq (alias antebrazo_*, *_derecho/derecha) cambia la cabeza
//     por una articulacion del brazo (DS154, piezas de guante/manga); _VISTA=frente|espalda y
//     _DIST_BRAZO/_LADO_BRAZO (def. 7,0 / 2,0: los que dieron el encuadre bueno). La prueba puede cambiar de
//     articulacion/vista sin relanzar con `dbz3_cam.req`
//     (cwd, una linea "<hueso> [frente|espalda] [dist] [lado]" o "off"; se relee cada 6 cuadros).
//     DS160: el ojo se pone del lado de FUERA del brazo (opuesto al torso, eje de los hombros + hombro->mano);
//     antes, en el brazo derecho, caia al otro lado y el cuerpo tapaba el antebrazo y la muneca.
// La cabeza sale del punto de golpe de la cabeza (X+1616+48), que el juego calcula cada cuadro desde el esqueleto
// animado y esta en coordenadas del mundo (la posicion real que se dibuja); si no vale, la matriz del hueso
// ([X+2952]+1032, traslacion +48/+52/+56) pasada a mundo, y si tampoco, X+48 + 13,2. El luchador 1 mira al 2: la
// camara va entre los dos. Las articulaciones del brazo (DS154) salen de los 22 puntos de cuerpo X+1616 (los que el
// juego saca de los huesos con la tabla de anclajes X+2956; sub_820F9548): 8 LARM2, 9 RARM2, 10 LHAND, 11 RHAND.
namespace {
// DS154: nombres y orden de la tabla de anclajes del juego (0x82333AC0, las cadenas s16 que usa para los 22 puntos
// de cuerpo): 0 WAIST, 1 STMC, 2 NECK, 3 HEAD, 4 LLEG2, 5 RLEG2, 6 LFOOT1, 7 RFOOT1, 8 LARM2, 9 RARM2, 10 LHAND,
// 11 RHAND, ... El codo es LARM2/RARM2 (la matriz del antebrazo arranca en el codo); la mano, L00_LHAND/L00_RHAND.
struct BrazoSlot { const char* nombre; int slot; int lado; };   // lado: +1 derecho (R), -1 izquierdo (L)
constexpr BrazoSlot kBrazoSlots[] = {
    {"codo_der", 9, +1},   {"antebrazo_der", 9, +1},   {"codo_derecho", 9, +1},   {"antebrazo_derecho", 9, +1},
    {"mano_der", 11, +1},  {"mano_derecha", 11, +1},
    {"codo_izq", 8, -1},   {"antebrazo_izq", 8, -1},   {"codo_izquierdo", 8, -1}, {"antebrazo_izquierdo", 8, -1},
    {"mano_izq", 10, -1},  {"mano_izquierda", 10, -1},
};
struct CamaraModo {
  bool apagada = false;
  int slot = -1;          // -1 = cabeza (DS141); >=0 = articulacion del brazo (indice de la tabla de anclajes)
  int lado = 0;           // +1 derecho / -1 izquierdo (solo brazo)
  bool espalda = false;   // vista por detras (solo brazo)
  float dist = 0.0f;      // 0 = defecto
  float lado_u = 0.0f;    // 0 = defecto
  std::string hueso = "cabeza";
};
std::string Minus(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
CamaraModo ParseModo(const std::string& texto, const CamaraModo& base) {
  CamaraModo m = base;
  std::istringstream is(texto);
  std::string nombre, vista, ds, ls;
  if (!(is >> nombre)) return m;
  nombre = Minus(nombre);
  if (nombre == "off" || nombre == "nada" || nombre == "0") { m.apagada = true; return m; }
  if (nombre == "cabeza" || nombre == "head" || nombre == "cara") {
    m.slot = -1, m.lado = 0, m.espalda = false, m.hueso = "cabeza";
  } else {
    bool encontrado = false;
    for (const BrazoSlot& b : kBrazoSlots)
      if (nombre == b.nombre) { m.slot = b.slot, m.lado = b.lado, m.hueso = b.nombre, encontrado = true; break; }
    if (!encontrado) return m;   // nombre desconocido: se deja el modo anterior
  }
  m.apagada = false;
  if (is >> vista) m.espalda = (Minus(vista) == "espalda");
  if (is >> ds) { const float f = float(std::atof(ds.c_str())); if (f > 0.1f) m.dist = f; }
  if (is >> ls) { const float f = float(std::atof(ls.c_str())); if (f >= 0.0f) m.lado_u = f; }
  return m;
}
// DS155: durante las fotos de brazos el combate se queda quieto (solo prueba). Mismo metodo que las escenas
// (cinematica.cpp/escenarios.cpp): el mando de cada lado pasa a uno virtual sin pulsaciones (el COM pierde la IA
// y el humano deja de leer el suyo) y se para el reloj de pelea (mgr+35144). Al soltar se devuelven los mandos y
// el reloj. Ademas se espera a que los dos esten en la accion 0 (guardia/reposo) para disparar la foto.
struct QuietoBrazo {
  bool activo = false, listo = false;
  int16_t mando[2] = {-1, -1};
  int usuario[2] = {-1, -1};
  bool cambiado[2] = {};
  uint32_t pasado = 0, idle = 0;
  void Empezar(uint8_t* base, uint32_t mgr) {
    (void)base;
    if (activo || !mgr) return;
    for (uint32_t k = 0; k < 2; ++k) mando[k] = int16_t(REX_LOAD_U16(mgr + kX0 + kXPaso * k + kMandoIdx));
    for (uint32_t k = 0; k < 2; ++k) {
      const uint32_t x = mgr + kX0 + kXPaso * k;
      const int16_t otro = mando[1 - k];
      cambiado[k] = mando[k] < 0 || mando[k] > 3;
      const int u = cambiado[k] ? (otro >= 0 && otro <= 3 ? otro : 0) : mando[k];
      usuario[k] = u;
      dbz3::net::InyectarPad(uint32_t(u), 0);
      if (cambiado[k]) REX_STORE_U16(x + kMandoIdx, uint16_t(u));
    }
    pasado = REX_LOAD_U32(mgr + kPasado);
    activo = true;
    listo = false;
    idle = 0;
  }
  void Tick(uint8_t* base, uint32_t mgr) {
    (void)base;
    if (!activo || !mgr) return;
    REX_STORE_U32(mgr + kPasado, pasado);   // reloj de pelea parado
    bool ok = true;
    for (uint32_t k = 0; k < 2; ++k)
      if (REX_LOAD_U16(mgr + kX0 + kXPaso * k + 3244) != 0) ok = false;
    idle = ok ? idle + 1 : 0;
    if (!listo && idle >= 20) listo = true;
  }
  void Soltar(uint8_t* base, uint32_t mgr) {
    (void)base;
    if (!activo) return;
    if (mgr) {
      for (uint32_t k = 0; k < 2; ++k) {
        const uint32_t x = mgr + kX0 + kXPaso * k;
        if (cambiado[k] && int16_t(REX_LOAD_U16(x + kMandoIdx)) == usuario[k])
          REX_STORE_U16(x + kMandoIdx, uint16_t(mando[k]));
      }
      REX_STORE_U32(mgr + kPasado, pasado);
    }
    for (uint32_t k = 0; k < 2; ++k) {
      if (usuario[k] >= 0) dbz3::net::InyectarPad(uint32_t(usuario[k]), -1);
      cambiado[k] = false;
      usuario[k] = -1;
    }
    activo = false;
    listo = false;
    idle = 0;
  }
};
QuietoBrazo g_quieto;
}  // namespace
void SondaCaraFotograma(uint8_t* base, uint32_t estado) {
  static const char* v = std::getenv("DBZ3_TEST_CARA");   // se lee una vez (esto corre cada cuadro)
  if (!v) return;   // la sonda manda sobre la camara de la escena (es una prueba: primer plano de la cara)
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (estado != 4) {   // solo en combate: el contador empieza cuando empieza la pelea (no en el menu ni en la intro)
    g_son_ok = false;
    g_quieto.Soltar(base, mgr);   // DS155: si se acaba la pelea, devuelve mandos y reloj
    return;
  }
  if (!mgr) return;
  // DS154: modo de la sonda. Por defecto, la cabeza (DS141); con DBZ3_TEST_CARA_HUESO, una articulacion del brazo
  // (codo/antebrazo o mano, derecha o izquierda) para ver las piezas de guante/manga de cerca. `dbz3_cam.req` (cwd)
  // manda sobre el entorno y se relee cada 6 cuadros para que la prueba cambie de articulacion/vista/lado sin
  // relanzar el juego; "off" devuelve la camara al juego.
  static const CamaraModo base_modo = [] {
    CamaraModo m;
    if (const char* h = std::getenv("DBZ3_TEST_CARA_HUESO")) m = ParseModo(h, m);
    if (const char* vw = std::getenv("DBZ3_TEST_CARA_VISTA"))
      if (m.slot >= 0) m.espalda = (Minus(vw) == "espalda");
    if (const char* d = std::getenv("DBZ3_TEST_CARA_DIST_BRAZO")) m.dist = float(std::atof(d));
    if (const char* l = std::getenv("DBZ3_TEST_CARA_LADO_BRAZO")) m.lado_u = float(std::atof(l));
    return m;
  }();
  static CamaraModo pedido;
  static bool hay_pedido = false;
  static std::string pedido_txt;
  static uint32_t nreq = 0;
  if ((nreq++ % 6) == 0) {
    std::ifstream f("dbz3_cam.req");
    std::string t;
    if (f && std::getline(f, t)) {
      if (t != pedido_txt) pedido_txt = t, pedido = ParseModo(t, base_modo);
      hay_pedido = true;
    } else {
      hay_pedido = false;
      pedido_txt.clear();
    }
  }
  const CamaraModo modo = hay_pedido ? pedido : base_modo;
  static const uint32_t espera = [] {   // la intro del combate (lineas de velocidad) dura ~2-3 s: se espera
    const char* e = std::getenv("DBZ3_TEST_CARA_ESPERA");
    return uint32_t(e ? std::max(0, std::atoi(e)) : 240);
  }();
  // DS141: se cuentan TODOS los cuadros de combate (antes solo los que llegaban al final de la funcion: con
  // DBZ3_TEST_CARA=0 campo/vals vacios y el contador se quedaba clavado). `_DURACION` apaga la camara a los N
  // MILISEGUNDOS de encenderla (en cuadros dependia de la tasa de dibujo: el juego dibuja a ~120 y 330 cuadros se
  // quedaban en 2,7 s), para que la prueba pueda volver a la camara del juego (cuerpo entero, golpes...).
  static uint32_t peleados = 0;
  ++peleados;
  if (peleados <= espera) {
    g_son_ok = false;
    g_quieto.Soltar(base, mgr);   // DS155
    return;
  }
  static const int64_t duracion_ms = [] {
    const char* e = std::getenv("DBZ3_TEST_CARA_DURACION");
    return int64_t(e ? std::max(0, std::atoi(e)) : 0);
  }();
  static int64_t t_son = 0;
  if (peleados == espera + 1) t_son = AhoraMs();
  if (duracion_ms > 0 && AhoraMs() - t_son > duracion_ms) {
    g_son_ok = false;
    g_quieto.Soltar(base, mgr);   // DS155
    return;
  }
  if (modo.apagada) {   // dbz3_cam.req = off: camara del juego (la prueba ya tiene las fotos)
    g_son_ok = false;
    g_quieto.Soltar(base, mgr);   // DS155: devuelve mandos y reloj
    return;
  }
  const V3 p1 = LeeV(base, mgr + kX0 + kPos), p2 = LeeV(base, mgr + kX0 + kXPaso + kPos);
  // DS141: la cabeza del luchador 1 por el punto de golpe de la cabeza (X+1616+48), que el juego calcula cada cuadro
  // desde el esqueleto animado y esta en coordenadas del mundo: es la posicion real que se dibuja. DS133 usaba la
  // matriz del hueso pero leia +40/+44/+48 (m10/m11/m12: rotacion y la traslacion x) en vez de +48/+52/+56, y la
  // camara salia bajo el suelo (hierba). La matriz queda de contraste/fallback.
  auto fl = [&](uint32_t a) { const uint32_t u = REX_LOAD_U32(a); float f; std::memcpy(&f, &u, 4); return f; };
  auto vale = [&](const V3& b) {   // la cabeza tiene que caer cerca del luchador (no en el origen ni bajo el suelo)
    const float dx = b.x - p1.x, dz = b.z - p1.z;
    return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.z) && dx * dx + dz * dz < 30.0f * 30.0f &&
           b.y > p1.y - 1.0f && b.y < p1.y + 40.0f;
  };
  // DS141: el punto de golpe de la cabeza (X+1616+48) es la posicion real que se DIBUJA; la matriz del hueso
  // ([X+2952]+1032) esta en el espacio del MODELO y pasa a mundo con la matriz del luchador X+0 (3x4) y X+48.
  const V3 golpe{fl(mgr + kX0 + 1616 + 48), fl(mgr + kX0 + 1616 + 52), fl(mgr + kX0 + 1616 + 56)};
  const V3 cuello{fl(mgr + kX0 + 1616 + 32), fl(mgr + kX0 + 1616 + 36), fl(mgr + kX0 + 1616 + 40)};
  V3 cabeza{p1.x, p1.y + 13.2f, p1.z};
  const char* fuente = "raiz+13,2";
  bool con_golpe = vale(golpe) && (std::fabs(golpe.x) + std::fabs(golpe.y) + std::fabs(golpe.z) > 1e-3f);
  if (con_golpe) cabeza = golpe, fuente = "punto de golpe";
  // La tabla de huesos [X+2952]+1032 esta en el espacio del MODELO: la matriz del hueso (traslacion m12/m13/m14)
  // pasa a mundo con la matriz del luchador X+0 (3x4) y la posicion X+48. El anclaje 3 (cabeza en la tabla s16) en
  // este heroe resuelve al hueso 18, que NO es la cabeza: se busca el hueso cuya posicion de mundo cae junto al
  // punto de golpe de la cabeza (el mas cercano) para dejar en el log la posicion REAL del hueso de la cabeza y su
  // indice (contraste); la camara apunta al punto de golpe.
  int bone = -1, bone_cabeza = -1;
  V3 hueso{}, hueso_cabeza{};
  float d_cabeza = 1e9f;
  const uint32_t mi = REX_LOAD_U32(mgr + kX0 + 2952), anc = REX_LOAD_U32(mgr + kX0 + 2956);
  const uint32_t tb = mi ? REX_LOAD_U32(mi + 1032) : 0;
  const V3 r0{fl(mgr + kX0), fl(mgr + kX0 + 4), fl(mgr + kX0 + 8)};
  const V3 r1{fl(mgr + kX0 + 16), fl(mgr + kX0 + 20), fl(mgr + kX0 + 24)};
  const V3 r2{fl(mgr + kX0 + 32), fl(mgr + kX0 + 36), fl(mgr + kX0 + 40)};
  auto mundo = [&](uint32_t h) {
    const V3 loc{fl(h + 48), fl(h + 52), fl(h + 56)};
    return V3{r0.x * loc.x + r0.y * loc.y + r0.z * loc.z + p1.x,
              r1.x * loc.x + r1.y * loc.y + r1.z * loc.z + p1.y,
              r2.x * loc.x + r2.y * loc.y + r2.z * loc.z + p1.z};
  };
  if (tb) {
    if (anc) {
      bone = int16_t(REX_LOAD_U16(anc + 6));
      const uint32_t h = tb + 64 * uint32_t(bone);
      if (bone >= 0 && bone < 256 && h > 0x40000000u && h < 0xA0000000u) {
        hueso = mundo(h);
        if (!con_golpe && vale(hueso)) cabeza = hueso, fuente = "hueso(anclaje)";
      }
    }
    if (con_golpe)
      for (uint32_t i = 0; i < 96; ++i) {
        const uint32_t h = tb + 64 * i;
        if (h < 0x40000000u || h >= 0xA0000000u) continue;
        const V3 b = mundo(h);
        if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) continue;
        const float dx = b.x - golpe.x, dy = b.y - golpe.y, dz = b.z - golpe.z, d = dx * dx + dy * dy + dz * dz;
        if (d < d_cabeza) d_cabeza = d, bone_cabeza = int(i), hueso_cabeza = b;
      }
  }
  static const float dist_cara = [] { const char* e = std::getenv("DBZ3_TEST_CARA_DIST"); return e ? float(std::atof(e)) : 4.5f; }();
  static const float lado_cara = [] { const char* e = std::getenv("DBZ3_TEST_CARA_LADO"); return e ? float(std::atof(e)) : 1.2f; }();
  static const float dist_brazo = [] { const char* e = std::getenv("DBZ3_TEST_CARA_DIST_BRAZO"); return e ? float(std::atof(e)) : 7.0f; }();
  static const float lado_brazo = [] { const char* e = std::getenv("DBZ3_TEST_CARA_LADO_BRAZO"); return e ? float(std::atof(e)) : 2.0f; }();
  // DS141: la camara va delante de la CARA, no solo en la recta al rival: el eje del cuello a la cabeza es el
  // "arriba" real del craneo (se inclina cuando el heroe se agacha o recibe), la cara es perpendicular a el y al
  // lado del cuerpo. Con el heroe erguido da la recta al rival de siempre; agachado, la camara baja para mirarle la
  // cara en vez de verle la coronilla o meterse en el gi.
  const V3 qf = Unit(V3{p2.x - p1.x, 0, p2.z - p1.z});
  V3 arriba = Unit(cabeza - cuello);
  if (std::fabs(arriba.x) + std::fabs(arriba.y) + std::fabs(arriba.z) < 0.5f) arriba = V3{0, 1, 0};
  V3 lado_c = Unit(V3{arriba.y * qf.z - arriba.z * qf.y, arriba.z * qf.x - arriba.x * qf.z,
                      arriba.x * qf.y - arriba.y * qf.x});
  if (std::fabs(lado_c.x) + std::fabs(lado_c.y) + std::fabs(lado_c.z) < 0.5f) lado_c = V3{1, 0, 0};
  V3 frente = Unit(V3{lado_c.y * arriba.z - lado_c.z * arriba.y, lado_c.z * arriba.x - lado_c.x * arriba.z,
                      lado_c.x * arriba.y - lado_c.y * arriba.x});
  if (frente.x * qf.x + frente.z * qf.z < 0) frente = frente * -1.0f;   // la cara mira al rival
  if (!(std::isfinite(frente.x) && std::isfinite(frente.y) && std::isfinite(frente.z))) frente = qf;
  // La vista de B3 degenera si el ojo mira casi en vertical (Cruz con (0,1,0) -> 0): se limita la inclinacion a
  // +-0,55 y, si la cara queda casi vertical, se usa la recta al rival.
  frente.y = std::clamp(frente.y, -0.55f, 0.55f);
  if (std::sqrt(frente.x * frente.x + frente.z * frente.z) < 0.30f) frente = qf;
  frente = Unit(frente);
  const V3 perp = Unit(V3{-frente.z, 0, frente.x});
  V3 ojo{}, objetivo = cabeza;
  float dist = dist_cara, lado = lado_cara;
  static const bool sin_camara = [] {   // DBZ3_TEST_CARA_CAMARA=0: no se toca la camara (se ve la del juego)
    const char* e = std::getenv("DBZ3_TEST_CARA_CAMARA");
    return e && std::atoi(e) == 0;
  }();
  static const bool quieto_req = [] {   // DS155: solo prueba (lo pone tools/pruebas.py)
    const char* e = std::getenv("DBZ3_TEST_CARA_QUIETO");
    return e && std::atoi(e) != 0;
  }();
  g_son_ok = !sin_camara;
  const float fov = fl(mgr + 0x1C4);
  static uint32_t nlog = 0;   // DS141/DS154: log de ojo/objetivo y del hueso (al cambiar de modo y cada 30 cuadros)
  static std::string clave_log;
  const std::string clave = modo.hueso + (modo.slot >= 0 ? (modo.espalda ? " espalda" : " frente") : "");
  if (clave != clave_log) clave_log = clave, nlog = 0;   // la primera linea del modo sale al momento
  if (modo.slot >= 0) {
    // DS154: articulacion del brazo. El objetivo es el punto de mundo X+1616+16*slot (los 22 puntos de cuerpo del
    // juego, sub_820F9548: 8 LARM2, 9 RARM2, 10 LHAND, 11 RHAND). Si no vale, la matriz del hueso de la tabla de
    // anclajes X+2956 (la misma tabla que lee la cabeza) pasada a mundo.
    const uint32_t punto = mgr + kX0 + 1616 + 16 * uint32_t(modo.slot);
    V3 brazo = LeeV(base, punto);
    const char* fuente_brazo = "punto de cuerpo";
    const int bone_brazo = anc ? int16_t(REX_LOAD_U16(anc + 2 * uint32_t(modo.slot))) : -1;
    V3 hueso_brazo{};
    bool con_hueso = false;
    if (tb && bone_brazo >= 0 && bone_brazo < 256) {
      const uint32_t h = tb + 64 * uint32_t(bone_brazo);
      if (h > 0x40000000u && h < 0xA0000000u) hueso_brazo = mundo(h), con_hueso = true;
    }
    if (!vale(brazo) || std::fabs(brazo.x) + std::fabs(brazo.y) + std::fabs(brazo.z) < 1e-3f) {
      if (con_hueso && vale(hueso_brazo)) brazo = hueso_brazo, fuente_brazo = "hueso(anclaje)";
      else brazo = cabeza, fuente_brazo = "cabeza(reserva)";
    }
    // DS155: hombro = punto de cuerpo del ARM1 (anclaje 17 derecho / 16 izquierdo) y mano = LHAND/RHAND
    // (10/11). Se apunta al punto medio del brazo ENTERO y se mira DE LADO (perpendicular al brazo): antes el
    // ojo caia sobre la recta al rival, casi a lo largo del antebrazo, y solo salia la mano (ni codo ni manga).
    auto punto_brazo = [&](uint32_t slot, V3* out) -> bool {
      const V3 p = LeeV(base, mgr + kX0 + 1616 + 16 * slot);
      if (vale(p) && std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z) > 1e-3f) { *out = p; return true; }
      const int b = anc ? int16_t(REX_LOAD_U16(anc + 2 * slot)) : -1;
      if (tb && b >= 0 && b < 256) {
        const uint32_t h = tb + 64 * uint32_t(b);
        if (h > 0x40000000u && h < 0xA0000000u) {
          const V3 w = mundo(h);
          if (vale(w)) { *out = w; return true; }
        }
      }
      return false;
    };
    const uint32_t sh_slot = (modo.lado > 0) ? 17u : 16u;
    const uint32_t mano_slot = (modo.lado > 0) ? 11u : 10u;
    V3 hombro{}, mano{};
    const bool con_hombro = punto_brazo(sh_slot, &hombro);
    const bool con_mano = punto_brazo(mano_slot, &mano);
    if (!con_hombro) hombro = brazo;
    if (!con_mano) mano = brazo;
    const V3 medio = (hombro + mano) * 0.5f;
    const float largo = std::sqrt((mano.x - hombro.x) * (mano.x - hombro.x) +
                                  (mano.y - hombro.y) * (mano.y - hombro.y) +
                                  (mano.z - hombro.z) * (mano.z - hombro.z));
    objetivo = medio + (brazo - medio) * 0.15f;
    dist = std::max(modo.dist > 0.0f ? modo.dist : dist_brazo, std::max(2.0f, largo * 1.35f));
    lado = modo.lado_u > 0.0f ? modo.lado_u : lado_brazo;
    // DS155: quieto para la foto (solo prueba): mandos sin pulsaciones (el COM pierde la IA), reloj de pelea
    // parado y se espera a que los dos esten en la accion 0 (guardia/reposo) antes de disparar.
    static bool quieto_log = false;
    if (quieto_req) {
      if (!g_quieto.activo) g_quieto.Empezar(base, mgr);
      g_quieto.Tick(base, mgr);
      if (g_quieto.listo && !quieto_log) {
        quieto_log = true;
        REXLOG_INFO("dbz3 cinematica: sonda de brazo: quieto (pose de reposo) cuadro {}", peleados);
      }
    } else {
      g_quieto.Soltar(base, mgr);
      quieto_log = false;
    }
    // Vista de frente o de espalda, pero de LADO al brazo: el ojo se aparta hacia el lado del brazo y solo un
    // poco hacia delante/detras, para que el antebrazo y el codo se vean enteros (no escorzados).
    // DS160: el ojo va SIEMPRE del lado de FUERA del brazo (el opuesto al torso). Con la camara en el lado
    // contrario el cuerpo tapaba el antebrazo y la muneca (fila del brazo derecho del 27/10, hoja
    // D:/tmp/heroe_brazos383b.png). El eje del cuerpo sale de los DOS hombros (anclajes 17 derecho y 16
    // izquierdo, datos del juego: no depende de la convencion de signos de cross()) y el lado de fuera de la
    // direccion hombro->mano: si la mano cruza al otro lado del torso manda la mano (es lo que se quiere ver);
    // si no (brazo colgando o en guardia, mano cerca del centro), manda el hombro del propio brazo.
    V3 f_h = Unit(V3{frente.x, 0.0f, frente.z});
    if (std::sqrt(f_h.x * f_h.x + f_h.z * f_h.z) < 0.5f) f_h = qf;
    V3 sh_der{}, sh_izq{};
    const bool con_eje = punto_brazo(17u, &sh_der) && punto_brazo(16u, &sh_izq);
    // eje = derecha del luchador (hombro derecho - hombro izquierdo); sin hombros, cross(frente, arriba).
    V3 eje = con_eje ? Unit(V3{sh_der.x - sh_izq.x, 0.0f, sh_der.z - sh_izq.z})
                     : Unit(V3{-f_h.z, 0.0f, f_h.x});
    if (std::sqrt(eje.x * eje.x + eje.z * eje.z) < 0.5f) eje = Unit(V3{-f_h.z, 0.0f, f_h.x});
    // frente del torso (frente/espalda de las fotos): perpendicular al eje de los hombros y hacia el rival.
    V3 f_t = Unit(V3{eje.z, 0.0f, -eje.x});
    if (f_t.x * qf.x + f_t.z * qf.z < 0.0f) f_t = f_t * -1.0f;
    if (std::sqrt(f_t.x * f_t.x + f_t.z * f_t.z) < 0.5f) f_t = f_h;
    const V3 centro = con_eje ? (sh_der + sh_izq) * 0.5f : p1;
    const float s_hombro = (hombro.x - centro.x) * eje.x + (hombro.z - centro.z) * eje.z;
    const float s_mano = (mano.x - centro.x) * eje.x + (mano.z - centro.z) * eje.z;
    const float fuera = (s_hombro * s_mano < 0.0f && std::fabs(s_mano) > 0.40f)
                            ? (s_mano >= 0.0f ? 1.0f : -1.0f)
                            : (s_hombro >= 0.0f ? 1.0f : -1.0f);
    const float lado_w = std::max(0.60f, 0.9f * lado);
    const float tilt = modo.espalda ? -0.45f : 0.45f;
    const float lateral = fuera * lado_w;   // lado de FUERA del brazo (+ = derecha del luchador)
    const V3 vista = Unit(V3{f_t.x * tilt + eje.x * lateral, 0.0f, f_t.z * tilt + eje.z * lateral});
    ojo = medio + vista * dist + V3{0, 0.08f * dist, 0};
    ojo.y = std::max(ojo.y, p1.y + 0.4f);
    // DS160: si el lado de fuera del brazo cae hacia el rival, el ojo no se mete dentro de el (el plano cercano
    // enseñaria el interior del modelo): se aparta en horizontal lo justo para quedar a 2 u.
    {
      const float ddx = ojo.x - p2.x, ddz = ojo.z - p2.z, d2 = std::sqrt(ddx * ddx + ddz * ddz);
      if (d2 < 2.0f) {
        if (d2 > 1e-4f) ojo.x = p2.x + ddx / d2 * 2.0f, ojo.z = p2.z + ddz / d2 * 2.0f;
        else ojo.x = p2.x + 2.0f, ojo.z = p2.z;
      }
    }
    if ((nlog++ % 30) == 0)
      REXLOG_INFO("dbz3 cinematica: sonda de brazo cuadro {}: hueso {} (slot {} indice {}) hueso ({:.2f} {:.2f} {:.2f}) d {:.2f} punto ({:.2f} {:.2f} {:.2f}) fuente {} hombro ({:.2f} {:.2f} {:.2f}) medio ({:.2f} {:.2f} {:.2f}) largo {:.2f} vista {} lado {} fuera {} fov {:.3f} dist {:.2f} ojo ({:.2f} {:.2f} {:.2f}) objetivo ({:.2f} {:.2f} {:.2f})",
                  peleados, modo.hueso, modo.slot, bone_brazo, hueso_brazo.x, hueso_brazo.y, hueso_brazo.z,
                  con_hueso ? std::sqrt((hueso_brazo.x - brazo.x) * (hueso_brazo.x - brazo.x) +
                                        (hueso_brazo.y - brazo.y) * (hueso_brazo.y - brazo.y) +
                                        (hueso_brazo.z - brazo.z) * (hueso_brazo.z - brazo.z))
                            : -1.0f,
                  brazo.x, brazo.y, brazo.z, fuente_brazo, hombro.x, hombro.y, hombro.z, medio.x, medio.y, medio.z,
                  largo, modo.espalda ? "espalda" : "frente",
                  modo.lado > 0 ? "derecho" : "izquierdo", fuera > 0.0f ? "derecho" : "izquierdo", fov, dist,
                  ojo.x, ojo.y, ojo.z, objetivo.x, objetivo.y, objetivo.z);
  } else {
    g_quieto.Soltar(base, mgr);   // DS155: fuera del modo brazo no se queda nada quieto
    ojo = cabeza + frente * dist + perp * lado + V3{0, 0.3f, 0};
    ojo.y = std::max(ojo.y, p1.y + 0.5f);
    if ((nlog++ % 30) == 0)
      REXLOG_INFO("dbz3 cinematica: sonda de cara cuadro {}: cabeza ({:.2f} {:.2f} {:.2f}) fuente {} hueso-cabeza {} ({:.2f} {:.2f} {:.2f}) d {:.2f} golpe ({:.2f} {:.2f} {:.2f}) cuello ({:.2f} {:.2f} {:.2f}) anclaje {} ({:.2f} {:.2f} {:.2f}) fov {:.3f} dist {:.2f} ojo ({:.2f} {:.2f} {:.2f}) objetivo ({:.2f} {:.2f} {:.2f})",
                  peleados, cabeza.x, cabeza.y, cabeza.z, fuente, bone_cabeza, hueso_cabeza.x, hueso_cabeza.y,
                  hueso_cabeza.z, std::sqrt(d_cabeza), golpe.x, golpe.y, golpe.z, cuello.x, cuello.y, cuello.z,
                  bone, hueso.x, hueso.y, hueso.z, fov, dist, ojo.x, ojo.y, ojo.z, objetivo.x, objetivo.y,
                  objetivo.z);
  }
  g_son_ojo[0] = ojo.x, g_son_ojo[1] = ojo.y, g_son_ojo[2] = ojo.z;
  g_son_obj[0] = objetivo.x, g_son_obj[1] = objetivo.y, g_son_obj[2] = objetivo.z;
  // el campo de la expresion, en ciclo por cuadros
  static uint32_t campo = 0, ms = 3000;
  static std::vector<int> vals;
  static bool leido = false;
  if (!leido) {
    leido = true;
    campo = uint32_t(std::strtoul(v, nullptr, 0));
    if (const char* c = std::strchr(v, ',')) {
      ms = uint32_t(std::max(200, std::atoi(c + 1)));
      const char* d = std::strchr(c + 1, ',');
      while (d && *d) {
        char* fin = nullptr;
        const long x = std::strtol(d + 1, &fin, 0);
        if (fin == d + 1) break;
        vals.push_back(int(x));
        d = fin;
      }
    }
  }
  if (!campo || vals.empty()) return;
  const uint32_t idx = uint32_t(double(peleados - espera) * 16.6667 / double(ms)) % uint32_t(vals.size());
  REX_STORE_U8(mgr + kX0 + campo, uint8_t(vals[idx] & 0xFF));
  static uint32_t ultimo = ~0u;
  if (idx != ultimo) {
    ultimo = idx;
    REXLOG_INFO("dbz3 cinematica: sonda de cara (combate) cuadro {}: X+{} = {} (ojo {:.2f} {:.2f} {:.2f} objetivo {:.2f} {:.2f} {:.2f})",
                peleados, campo, vals[idx], ojo.x, ojo.y, ojo.z, objetivo.x, objetivo.y, objetivo.z);
  }
}

void Fotograma(uint8_t* base, uint32_t estado) {
  g_conectada = true;
  SondaCaraFotograma(base, estado);   // S1: sonda de la cara en combates nativos (sin escena)
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  // SONDA (DBZ3_CINE_TRAZA=1): estado del combate, accion (X+3244) y codigo de animacion (X+2928) de cada luchador al
  // cambiar: de aqui salen los codigos de la entrada, la carga de ki y la victoria que reproducen las escenas
  static const bool traza = std::getenv("DBZ3_CINE_TRAZA") != nullptr;
  if (traza && mgr)
    for (uint32_t s = 0; s < 2; ++s) {
      static uint32_t visto[2] = {~0u, ~0u};
      const uint32_t x = mgr + kX0 + kXPaso * s, ac = REX_LOAD_U16(x + 3244), an = REX_LOAD_U32(x + kCodigo) & 0xFFF;
      const uint32_t clave = estado << 28 | ac << 12 | an;
      if (clave == visto[s]) continue;
      visto[s] = clave;
      REXLOG_INFO("dbz3 cinematica: sonda lado {} (id {}): estado {} accion {} codigo {:03X}", s + 1,
                  REX_LOAD_U32(x + 7420), estado, ac, an);
    }
  const bool saltar = g_saltar.exchange(false);
  if (!mgr || estado != 4 || saltar) {
    Terminar(base, saltar ? "saltada" : "cortada");
    return;
  }
  std::shared_ptr<const Escena> e;
  if (!g_en_curso.load()) {
    {
      std::lock_guard<std::mutex> lk(g_mu);
      e = std::move(g_pedida);
      g_esc = e;
    }
    if (!e) return;
    // mandos virtuales (XInput, net/netplay_hooks.cpp): el humano sigue en el suyo, que deja de leerse; el COM pasa
    // al del otro lado (su IA para mientras dura). ponytail: darle uno libre (el 4, como el relevo del Tag Team)
    // saca la pausa "2P PAUSE" (prueba 2026-10-16); compartido, el que no sale en el plano repite la pose.
    for (uint32_t k = 0; k < 2; ++k) g_mando[k] = int16_t(REX_LOAD_U16(mgr + kX0 + kXPaso * k + kMandoIdx));
    for (uint32_t k = 0; k < 2; ++k) {
      const uint32_t x = mgr + kX0 + kXPaso * k;
      const int16_t otro = g_mando[1 - k];
      g_cambiado[k] = g_mando[k] < 0 || g_mando[k] > 3;
      const int u = g_cambiado[k] ? (otro >= 0 && otro <= 3 ? otro : 0) : g_mando[k];
      g_usuario[k] = u;
      dbz3::net::InyectarPad(uint32_t(u), 0);
      if (g_cambiado[k]) REX_STORE_U16(x + kMandoIdx, uint16_t(u));
    }
    REXLOG_INFO("dbz3 cinematica: mandos {} / {} -> virtuales {} / {}", g_mando[0], g_mando[1], g_usuario[0], g_usuario[1]);
    OcultarHud(true);
    for (uint32_t s = 0; s < 2; ++s) {
      g_codigo0[s] = REX_LOAD_U32(mgr + kX0 + kXPaso * s + kCodigo) & 0xFFF;
      g_puesto[s] = false;
    }
    g_pasado = REX_LOAD_U32(mgr + kPasado);
    g_frame = 0;
    g_t0 = AhoraMs();
    g_en_curso = true;
    if (e->al_empezar_escena) e->al_empezar_escena();   // S1: al empezar la escena (callar la mezcla de B3, pasarela.cpp)
    EmpezarPaso(0, *e);
  } else {
    std::lock_guard<std::mutex> lk(g_mu);
    e = g_esc;
  }
  if (!e) return Terminar(base, "sin escena");
  REX_STORE_U32(mgr + kPasado, g_pasado);   // reloj parado (el tiempo del combate empieza al acabar la escena)
  int k = g_paso.load();
  // A pasa al paso siguiente (como mucho uno cada 0,4 s: la voz nueva corta la anterior, PlaySound)
  const bool adelante = AhoraMs() - g_t0_paso > 400 && g_siguiente.exchange(false);   // antes: se aplaza, no se pierde
  // Duracion del paso: si la escena de motor trae los cuadros reales (tabla de clips de su banco), manda el
  // frame a 60 Hz; si no, los segundos del respaldo.
  const Paso& paso_antes = e->pasos[size_t(k)];
  const float seg_paso = paso_antes.cuadros > 0 ? float(paso_antes.cuadros) / 60.0f : paso_antes.segundos;
  if (adelante || float(AhoraMs() - g_t0_paso) >= seg_paso * 1000.0f) {
    if (++k >= int(e->pasos.size())) return Terminar(base, "terminada");
    EmpezarPaso(k, *e);
  }
  const Paso& p = e->pasos[size_t(k)];   // el paso que se ve AHORA (pudo avanzar arriba)
  // DS31: en el final, Cell solo esta en el primer plano (clip de 650 cuadros del paquete de B1); en los demas
  // encuadres B1 no lo dibuja. Se oculta el cuerpo del lado 2 con los flags del propio juego (tagteam.cpp: 3261
  // cuerpo A, 4925 cuerpo B; es lo que usa el perdedor en la pose de victoria).
  if (p.ocultar_lado2) {
    const uint32_t x2 = mgr + kX0 + kXPaso;
    REX_STORE_U8(x2 + 3261, 1);
    REX_STORE_U8(x2 + 4925, 1);
    g_oculto2 = true;
  } else if (g_oculto2) {
    const uint32_t x2 = mgr + kX0 + kXPaso;
    REX_STORE_U8(x2 + 3261, 0);
    REX_STORE_U8(x2 + 4925, 0);
    g_oculto2 = false;
  }
  {   // botones del paso en el mando virtual de cada lado; si lo comparten, los del lado del plano
    const uint16_t b0 = p.botones[0], b1 = p.botones[1];
    if (g_usuario[0] >= 0 && g_usuario[0] == g_usuario[1])
      dbz3::net::InyectarPad(uint32_t(g_usuario[0]), p.plano == kLado1 ? b0 : p.plano == kLado2 ? b1 : (b0 | b1));
    else
      for (uint32_t s = 0; s < 2; ++s)
        if (g_usuario[s] >= 0) dbz3::net::InyectarPad(uint32_t(g_usuario[s]), p.botones[s]);
  }
  if (g_frame % 20 == 0 && (e->pasos[size_t(k)].botones[0] | e->pasos[size_t(k)].botones[1]))   // traza: accion (X+3244)
    REXLOG_INFO("dbz3 cinematica: cuadro {} paso {}: acciones {} / {}", g_frame, k + 1,
                REX_LOAD_U16(mgr + kX0 + 3244), REX_LOAD_U16(mgr + kX0 + kXPaso + 3244));
  // camara: plano del paso, fundido desde el anterior; al final, hacia la del combate
  V3 p1 = LeeV(base, mgr + kX0 + kPos), p2 = LeeV(base, mgr + kX0 + kXPaso + kPos);
  float distancia = e->distancia;
  if (p.distancia_final > 0.0f && distancia > 0.0f) {   // DS16: B1 los lleva al centro del ring antes del READY
    const float avance = std::clamp(float(AhoraMs() - g_t0_paso) / (1000.0f * std::max(0.01f, p.segundos)), 0.0f, 1.0f);
    distancia += (p.distancia_final - distancia) * Suave(avance);
  }
  if (distancia > 0) {   // separados como pide la escena (B1: lados en x = -15 y +15)
    const V3 c = Mezcla(p1, p2, 0.5f), eje = Unit(V3{p2.x - p1.x, 0, p2.z - p1.z});
    p1 = V3{c.x, p1.y, c.z} - eje * (distancia * 0.5f);
    p2 = V3{c.x, p2.y, c.z} + eje * (distancia * 0.5f);
    PonV(base, mgr + kX0, p1);
    PonV(base, mgr + kX0 + kXPaso, p2);
  }
  V3 ojo, obj, ojo0, obj0;
  auto con_pose = [&](int i) {
    const Paso& q = e->pasos[size_t(i)];
    const int a = q.plano == kLado1 ? q.anim[0] : q.plano == kLado2 ? q.anim[1] : -1;
    return a < 0 ? 0 : a == kPoseVictoria ? 2 : 1;
  };
  const float s1 = dbz3::gigantes::EscalaDe(base, 0), s2 = dbz3::gigantes::EscalaDe(base, 1);
  Encuadre(e->pasos[size_t(k)].plano, p1, p2, &ojo, &obj, con_pose(k), s1, s2);
  const float t = float(AhoraMs() - g_t0_paso) / 1000.0f, f = Suave(t / kFundido);
  if (k > 0 && f < 1.0f) {
    Encuadre(e->pasos[size_t(k - 1)].plano, p1, p2, &ojo0, &obj0, con_pose(k - 1), s1, s2);
    ojo = Mezcla(ojo0, ojo, f);
    obj = Mezcla(obj0, obj, f);
  }
  float oj[3], ob[3];
  const bool ultimo = k + 1 == int(e->pasos.size());
  // Camara del clip de la linea (escenas de MOTOR de IW): ojo/objetivo/fov/roll por cuadro, 60 Hz. Tiene
  // prioridad sobre el plano del respaldo (Encuadre); el fundido al final sigue mezclando con la del combate.
  float roll = 0.0f;
  {
    const float tf = t * 60.0f;
    float co[3], cb[3];
    if (MuestraCam(p.cam_ojo, tf, co) && MuestraCam(p.cam_obj, tf, cb)) {
      ojo = V3{co[0], co[1], co[2]};
      obj = V3{cb[0], cb[1], cb[2]};
      if (p.rel) {   // marco de los luchadores (p1/p2 ya estan en su sitio de la escena)
        const V3 c = Mezcla(p1, p2, 0.5f), eje = Unit(V3{p2.x - p1.x, 0, p2.z - p1.z});
        const V3 lado{-eje.z, 0, eje.x};
        auto mundo = [&](V3 l) { return c + eje * l.x + V3{0, l.y, 0} + lado * l.z; };
        ojo = mundo(ojo);
        obj = mundo(obj);
        // DS186: el objetivo de B1 de estas tomas (kObjReady de las rondas, la tabla medida de la previa) es un punto
        // del cuerpo del actor; se lleva a la misma fraccion del cuerpo DIBUJADO, como DS185 en la llegada.
        if (p.objetivo_cuerpo_b3 && p.cuerpo_cintura_b1 > 0.1f) {
          const int la = p.plano == kLado1 ? 0 : p.plano == kLado2 ? 1 : -1;
          if (la >= 0) {
            const V3 pies = LeeV(base, mgr + kX0 + kXPaso * uint32_t(la) + kPos);
            AjustarObjetivoCuerpo(base, mgr, la, pies.y + p.cuerpo_cintura_b1, p.cuerpo_cabeza_b1, ojo, obj, "escena");
          }
        }
      }
      float fov = 0.6f;
      if (MuestraCam(p.cam_fov, tf, &fov)) {
        fov = std::clamp(fov, 0.2f, 1.3f);
        if (p.fov_acerca && !p.fov_acerca_sin_mover) {   // DS168: la toma del coche conserva el ojo de B1 (ver cinematica.h)
          // DS178: el truco (acercar el ojo al objetivo) solo vale si el objetivo de B1 cae sobre el actor. En el clip 6
          // de la llegada (G 3320..3480) el objetivo esta a 347 u del ojo y a 320 del actor: acercarlo un 52 % movia el
          // ojo 182 u, se pasaba 146 u del luchador (que quedaba DETRAS de la camara) y salia el paisaje con una bota en
          // el borde (fotos c133-c147). Si el objetivo esta lejos del actor se deja el ojo de B1, como la toma del coche.
          float a[3];
          const bool con_ancla = p.cam_ancla_lado >= 0 && MuestraCam(p.cam_ancla, tf, a);
          const float daim = std::sqrt(std::pow(obj.x - ojo.x, 2) + std::pow(obj.y - ojo.y, 2) + std::pow(obj.z - ojo.z, 2));
          const float dact = con_ancla ? std::sqrt(std::pow(a[0] - ojo.x, 2) + std::pow(a[1] - ojo.y, 2) + std::pow(a[2] - ojo.z, 2)) : 0.0f;
          const float k = std::tan(fov * 0.5f) / std::tan(0.785f * 0.5f);
          if (!(con_ancla && k < 0.999f && daim > 1.8f * dact + 4.0f))
            ojo = obj + (ojo - obj) * k;
        }
        uint32_t u;
        std::memcpy(&u, &fov, 4);
        REX_STORE_U32(mgr + 0x1C4, u);   // fov vertical (cine.cpp)
      }
      MuestraCam(p.cam_roll, tf, &roll);
    }
  }
  // DS143: camara de B1 anclada a un luchador (escenas de la ronda 1): desplazamiento por cuadro entre su posicion en
  // B1 y la que tiene en el escenario de B3. Con esto las tomas de B1 encuadran al luchador de B3 sin moverlo del ring.
  if (p.cam_ancla_lado >= 0) {
    const float fa = float(AhoraMs() - g_t0_paso) / 1000.0f * 60.0f;
    float a[3];
    if (MuestraCam(p.cam_ancla, fa, a)) {
      const V3 b3 = p.cam_ancla_lado == 0 ? p1 : p2;
      if (p.cam_ancla_giro == 0.0f) {   // DS145: el desplazamiento tambien vale para los props del mundo de B1
        g_ancla_d[0] = b3.x - a[0], g_ancla_d[1] = b3.y - a[1], g_ancla_d[2] = b3.z - a[2];
        g_ancla_ok = true;
      } else {
        g_ancla_ok = false;
      }
      if (p.cam_ancla_giro != 0.0f) {   // DS145: la toma de B1 se lleva al lado del cuerpo que tiene en B3
        const float cg = std::cos(p.cam_ancla_giro), sg = std::sin(p.cam_ancla_giro);
        auto gira = [&](const V3& v) {
          const V3 r{v.x - a[0], v.y - a[1], v.z - a[2]};
          return V3{b3.x + r.x * cg + r.z * sg, b3.y + r.y, b3.z - r.x * sg + r.z * cg};
        };
        ojo = gira(ojo);
        obj = gira(obj);
        // DS186: la previa (Cell de cerca) usa esta camara girada sobre el ancla; su objetivo tambien es un punto del
        // cuerpo de B1 (cintura de `a` + `cuerpo_suelo_b3`) y el B3 lo dibuja con su pose. Misma regla que DS185.
        if (p.objetivo_cuerpo_b3 && p.cuerpo_suelo_b3 > 0.5f)
          AjustarObjetivoCuerpo(base, mgr, p.cam_ancla_lado, b3.y + p.cuerpo_suelo_b3, p.cuerpo_cabeza_b1, ojo, obj,
                                "ancla");
      } else {
        V3 d{b3.x - a[0], b3.y - a[1], b3.z - a[2]};
        // DS185: el objetivo (sobre todo su altura) va a la MISMA fraccion de la recta cintura->cabeza del cuerpo que
        // se DIBUJA, leida del esqueleto animado (pose de la escena), no del `y` absoluto de B1: la guardia de B3 baja
        // la cadera ~3 u y con el `y` de B1 el luchador se hundia al final del clip 5 y en el 7 (solo el pelo por el
        // borde inferior). El horizontal sigue siendo el ancla del luchador. La cintura de B1 es la de ESTE cuadro
        // (a.y + `cuerpo_suelo_b3`), no la del principio del tramo: en el clip 8 sube 4,5 u dentro del mismo tramo.
        V3 cintura{}, cabeza{};
        const uint32_t xc = mgr + kX0 + kXPaso * uint32_t(p.cam_ancla_lado);
        static const int sonda_cuerpo = [] { const char* v = std::getenv("DBZ3_TEST_PROPS_LLEGADA_LOG"); return v ? std::max(1, std::atoi(v)) : 0; }();
        if (p.objetivo_cuerpo_b3 && p.cuerpo_cabeza_b1 > 0.5f && p.cuerpo_suelo_b3 > 0.5f &&
            PuntoCuerpo(base, xc, 0, cintura) && PuntoCuerpo(base, xc, 3, cabeza) &&
            std::fabs(cabeza.y - cintura.y) > 2.0f && std::fabs(cabeza.y - cintura.y) < 12.0f &&
            std::fabs(cintura.x - b3.x) < 20.0f && std::fabs(cintura.z - b3.z) < 20.0f &&
            std::fabs(cabeza.x - b3.x) < 20.0f && std::fabs(cabeza.z - b3.z) < 20.0f &&
            cintura.y > b3.y + 3.0f && cintura.y < b3.y + 15.0f) {
          const float cadera = a[1] + p.cuerpo_suelo_b3;   // cintura de B1 (mundo) en este cuadro
          const float f = (obj.y - cadera) / p.cuerpo_cabeza_b1;
          d.y = (cintura.y + f * (cabeza.y - cintura.y)) - obj.y;
          if (sonda_cuerpo && g_frame % 60 == 0) {
            REXLOG_INFO("dbz3 cinematica: llegada cuerpo cuadro {}: cintura dibujada ({:.1f} {:.1f} {:.1f}) cabeza "
                        "({:.1f} {:.1f} {:.1f}) cintura B1 {:.2f} f {:.3f}; objetivo y de B1 {:.2f} -> {:.2f}",
                        g_frame, cintura.x, cintura.y, cintura.z, cabeza.x, cabeza.y, cabeza.z, cadera, f, obj.y,
                        cintura.y + f * (cabeza.y - cintura.y));
          }
        } else if (sonda_cuerpo && p.objetivo_cuerpo_b3) {
          static int ultimo_fallo = -100000;
          if (g_frame - ultimo_fallo >= 120) {
            ultimo_fallo = g_frame;
            REXLOG_INFO("dbz3 cinematica: llegada cuerpo cuadro {}: sin huesos validos (cintura {:.1f} {:.1f} {:.1f} "
                        "cabeza {:.1f} {:.1f} {:.1f}); se deja la camara de B1", g_frame, cintura.x, cintura.y, cintura.z,
                        cabeza.x, cabeza.y, cabeza.z);
          }
        }
        ojo = ojo + d;
        obj = obj + d;
      }
    }
  } else {
    g_ancla_ok = false;
  }
  const float queda = p.segundos - t;
  if (ultimo && queda < kFundido && cine::LeerCamara(base, oj, ob)) {
    const float v = Suave(1.0f - queda / kFundido);
    ojo = Mezcla(ojo, V3{oj[0], oj[1], oj[2]}, v);
    obj = Mezcla(obj, V3{ob[0], ob[1], ob[2]}, v);
  }
  float eo[3] = {ojo.x, ojo.y, ojo.z}, eb[3] = {obj.x, obj.y, obj.z};
  if (p.fov_acerca) {   // A3b: B3 dibuja 0,8 x lo puesto + 0,2 x su camara de guion (objetivo = mgr+0x160, medido); se pone TAMBIEN esa camara (ojo mgr+0x150, objetivo +0x160) igual a la deseada: la mezcla da la deseada, sin lazo
    static const bool lazo = std::getenv("DBZ3_TEST_LAZO") != nullptr;   // el lazo cerrado filtrado (ganancia 0,3) en vez de la camara directa
    if (!lazo) {   // camara DIRECTA: se guarda y la pone el gancho de la funcion de camara del juego (sub_820F47E0) justo despues de que ella la calcule
      for (int i = 0; i < 3; ++i) g_dir[i] = eo[i], g_dir[3 + i] = eb[i];
      g_dir[6] = roll;
      g_dir_ok = true;
    } else {
      g_dir_ok = false;   // DS30: con el lazo no hay camara exacta que ofrecer a los props (CamaraEscena)
      static float co[3], ct[3], d[6], rf[6];
      static bool hay = false, hayr = false;
      static const float ganancia = [] { const char* v = std::getenv("DBZ3_TEST_GANANCIA"); return v ? float(std::atof(v)) : 0.3f; }();
      float ro[3], rt[3];
      if (hay && cine::LeerCamara(base, ro, rt)) {
        const float r[6] = {ro[0], ro[1], ro[2], rt[0], rt[1], rt[2]};
        for (int i = 0; i < 6; ++i) rf[i] = hayr ? 0.5f * rf[i] + 0.5f * r[i] : r[i];
        hayr = true;
        float lejos = 0;
        for (int i = 0; i < 6; ++i) lejos = std::max(lejos, std::fabs(rf[i] - d[i]));
        for (int i = 0; i < 3; ++i) {
          co[i] = lejos > 150.0f ? 0.0f : co[i] + ganancia * (d[i] - rf[i]);
          ct[i] = lejos > 150.0f ? 0.0f : ct[i] + ganancia * (d[3 + i] - rf[3 + i]);
        }
      }
      for (int i = 0; i < 3; ++i) d[i] = eo[i], d[3 + i] = eb[i];
      hay = true;
      for (int i = 0; i < 3; ++i) eo[i] += co[i], eb[i] += ct[i];
    }
  }
  if (static const int sonda = [] { const char* v = std::getenv("DBZ3_TEST_CAMLOG"); return v ? std::max(1, std::atoi(v)) : 0; }(); sonda && g_frame % sonda == 0) {   // A3b: p1/p2, camara escrita (w) y camara que dibuja B3 (leida)
    float ro[3] = {0, 0, 0}, rt[3] = {0, 0, 0};
    cine::LeerCamara(base, ro, rt);
    if (g_frame % (sonda * 300) == 0) {   // volcado de mgr+0x100..0x200 en floats: a ver donde guarda B3 su camara propia (N) antes de mezclarla
      std::string v;
      const uint32_t mg = REX_LOAD_U32(kMgrPtr);
      auto fl = [&](uint32_t a) { const uint32_t u = REX_LOAD_U32(a); float f; std::memcpy(&f, &u, 4); return f; };
      for (uint32_t o = 0x100; o < 0x200; o += 16) {
        char b[160];
        std::snprintf(b, sizeof b, " [%03X] %.2f %.2f %.2f %.2f;", o, fl(mg + o), fl(mg + o + 4), fl(mg + o + 8), fl(mg + o + 12));
        v += b;
      }
      REXLOG_INFO("dbz3 cinematica: volcado mgr cuadro {}:{}", g_frame, v);
    }
    REXLOG_INFO("dbz3 cinematica: sonda cuadro {} paso {} t {:.3f} escrita ojo ({:.1f} {:.1f} {:.1f}) obj ({:.1f} {:.1f} {:.1f}) leida ojo ({:.1f} {:.1f} {:.1f}) obj ({:.1f} {:.1f} {:.1f})",
                g_frame, k + 1, t, eo[0], eo[1], eo[2], eb[0], eb[1], eb[2], ro[0], ro[1], ro[2], rt[0], rt[1], rt[2]);
  }
  cine::Camara(base, eo, eb, roll);
  // DS16: la pose de brazos de B1 sobre el cuerpo del clip de B3 (solo el luchador 1, solo en el paso marcado)
  if (p.brazos_b1 && !std::getenv("DBZ3_DS16_SIN_BRAZOS")) AplicaBrazosB1(base, mgr + kX0, t);
  g_abre = std::min({1.0f, float(AhoraMs() - g_t0) / 500.0f, ultimo ? queda / 0.5f : 1.0f});
  ++g_frame;
}

// Tras leer el mando: nada (los botones del paso van por XInput, InyectarPad); se queda para el gancho de combate.cpp.
void Mando(uint8_t* /*base*/, uint32_t /*usuario*/) {}


// A3b: lo llama el gancho de sub_820F47E0 (funcion de camara del juego: ojo +0x190, objetivo +0x1A0, vista +0x60; medido con un punto de datos,
// herramientas/watch_camara.py: la cadena 82092848 -> 820F47E0 -> 820F43A8 escribe ojo y objetivo y 820F4300 la vista) tras calcularla ella: la camara
// de la escena se pone ENTERA, sin mezcla ni lazo. Escribe ojo, objetivo y vista (cine::Camara).
void AplicarCamaraDirecta(uint8_t* base) {
  if (!g_dir_ok.load() || !g_en_curso.load()) return;
  const float eo[3] = {g_dir[0], g_dir[1], g_dir[2]}, eb[3] = {g_dir[3], g_dir[4], g_dir[5]};
  cine::Camara(base, eo, eb, g_dir[6]);
}
// DS30: la camara exacta del paso en curso, para los props que se pintan fuera del juego (reportera/tarjeta del final):
// mgr+0x60 alterna entre la camara nativa del guest y la de la escena (mezcla 0,8/0,2) y el prop temblaba en la interfaz.
bool AnclaDelta(float d[3]) {
  if (!g_ancla_ok.load()) return false;
  for (int i = 0; i < 3; ++i) d[i] = g_ancla_d[i].load();
  return true;
}
bool CamaraEscena(float ojo[3], float objetivo[3], float* giro) {
  if (!g_dir_ok.load() || !g_en_curso.load()) return false;
  for (int i = 0; i < 3; ++i) {
    if (ojo) ojo[i] = g_dir[i];
    if (objetivo) objetivo[i] = g_dir[3 + i];
  }
  if (giro) *giro = g_dir[6];
  return true;
}
// S1: la camara de la sonda de la cara (combate nativo, sin escena en curso). DS141: mientras la sonda esta activa
// se oculta el cuerpo del luchador 2 (los flags del perdedor, como DS31): en el cuerpo a cuerpo el rival se metia
// entre la camara y la cabeza del 1 (se veia su pecho). Se devuelve al apagarse la sonda.
void AplicarCamaraSonda(uint8_t* base) {
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  static bool oculto2 = false;
  if (!g_son_ok.load()) {
    if (oculto2 && mgr) {
      const uint32_t x2 = mgr + kX0 + kXPaso;
      REX_STORE_U8(x2 + 3261, 0);
      REX_STORE_U8(x2 + 4925, 0);
      oculto2 = false;
    }
    return;
  }
  if (mgr && !oculto2) {
    const uint32_t x2 = mgr + kX0 + kXPaso;
    REX_STORE_U8(x2 + 3261, 1);
    REX_STORE_U8(x2 + 4925, 1);
    oculto2 = true;
  }
  cine::Camara(base, g_son_ojo, g_son_obj, 0.0f);
}

// DS178: saltar la victoria/resultado de un combate "solo escena" (DS170 midio 3,0 s de estado 5 + 0,2 s de 6-7; el
// estado 8, la pantalla de resultado, sigue su curso). Lo arma pasarela al acabar la escena (LanzarSolo) y lo consume
// el gancho de la camara del juego (sub_820F47E0, mas abajo) con su contexto: la funcion del juego usa la pila, asi que
// la llamada va SIEMPRE dentro de un gancho (nunca desde un hilo propio). Se espera 150 ms desde que se ve el estado 5
// para que el juego haya fijado el ganador y la puntuacion. Una vez por combate; DBZ3_SIN_RESULTADO_RAPIDO lo apaga.
void PedirResultadoRapido() { g_resultado_rapido = true; }
void QuitarResultadoRapido() { g_resultado_rapido = false; }
void SaltarResultadoEscena(PPCContext& ctx, uint8_t* base) {
  if (!g_resultado_rapido.load()) return;
  if (std::getenv("DBZ3_SIN_RESULTADO_RAPIDO")) {
    g_resultado_rapido = false;
    return;
  }
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr) return;
  const uint32_t st = REX_LOAD_U32(mgr + 35128);
  static int64_t t5 = 0;
  if (st < 5) {
    t5 = 0;
    return;
  }
  if (st != 5) {   // ya no hace falta (el juego paso de 5 por su cuenta)
    g_resultado_rapido = false;
    t5 = 0;
    return;
  }
  if (!t5) t5 = AhoraMs();
  if (AhoraMs() - t5 < 150) return;
  g_resultado_rapido = false;
  t5 = 0;
  PPCContext c = ctx;
  DBZ3_SALIDA_RESULTADO(c, base);   // US sub_820FF820 / EU sub_820FF920: estado 8 + relleno del resultado
  REXLOG_INFO("dbz3 pasarela: resultado del combate de escena saltado (estado 5 -> 8; sub_820FF820)");
}
}  // namespace dbz3::cinematica

DBZ3_HOOK_EU(sub_820F47E0) {   // A3b: despues de la camara del juego
  orig(ctx, base);
  dbz3::cinematica::AplicarCamaraDirecta(base);
  dbz3::cinematica::AplicarCamaraSonda(base);   // S1: primer plano de la cara en un combate nativo (sonda)
  dbz3::freecam::AplicarCamara(base);   // DS162: camara libre (no hace nada si esta apagada)
  dbz3::cinematica::SaltarResultadoEscena(ctx, base);   // DS178: victoria/resultado del combate de escena (si esta armado)
}
