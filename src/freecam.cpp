// dbz3 - Camara libre de pruebas (DS162/DS163, 2026-10-27).
//
// Por que la version de DS162 no cambiaba la vista (medido en el codigo recompilado):
//   * sub_820F4300 (generated/dbz3_recomp.6.cpp) NO dibuja con mgr+0x60. Copia 64 B de la camara activa
//     (*(mgr+0x264), puntero que deja sub_820F47E0) a una struct de pila y construye la vista desde ELLA
//     (sub_82082970 -> sub_82082A08 -> sub_82082908); solo al final copia el resultado a mgr+0x60 con
//     sub_8208A168. Escribir mgr+0x190/+0x1A0/+0x60 en el gancho de sub_820F47E0 se pierde/queda sin uso
//     para el dibujo: eso es lo que midio K2 con DBZ3_TEST_CAMARA_LUCHA (la imagen no cambiaba).
//   * La struct de camara (sub_82082468 la rellena) es: +0x00 ojo (x,y,z,1), +0x10 objetivo (x,y,z,1),
//     +0x20 quaternion de orientacion, +0x30 balanceo, +0x34 fov (rad), +0x38 flags.
//   * Si flags&1, sub_82082868 recalcula el quaternion a partir de ojo->objetivo (sub_82092DA8 +
//     sub_82090BB8) y limpia el bit. Por eso B3 puede dibujar una camara "libre": basta escribir ojo,
//     objetivo y flags|=1 en la struct ACTIVA antes de sub_820F4300; el juego deriva la orientacion.
//     DS163 hace eso (y ademas escribe el quaternion ya calculado, para no depender del flag).
//
// Ganchos: 47E0 (cinematica.cpp, tras la camara del juego) y 4300 (iw_mini_vuelo.cpp). En 4300 se llama
// ANTES del original (AplicarAntesDelDibujo: la ultima escritura que ve el dibujo) y DESPUES (TrasLaVista:
// solo registra la vista que queda en mgr+0x60 y reafirma la struct).
// Con la opcion apagada no se toca nada.
//
// Controles: WASD mueve, Q/E baja y sube, raton con el boton derecho o palanca derecha para mirar, palanca
// izquierda para moverse, Shift/gatillos mas rapido, rueda o LB/RB para el FOV/zoom y R para volver a la
// camara del juego. Mientras esta activa el juego lee un mando neutro (InputSystem::AddUIInputBlocker).
// "Pausar el juego" (DS164) llama a la pausa del propio juego (sub_820FF8A8; reanuda con sub_820FF9B8) y
// ademas congela el reloj de pelea (mgr+35144); "Ocultar HUD" usa combate::OcultarHud.
//
// Pruebas: DBZ3_TEST_FREECAM="x,y,z,yaw,pitch,fov[;...]" (desplazamiento respecto al ojo del juego, giro en
// grados respecto a su orientacion y fov vertical en grados). dbz3_freecam.req elige la pose. Con
// DBZ3_FREECAM_LOG=1 se escribe una linea por muestra (cada 10 cuadros) con la struct leida/escrita y la
// vista; con =2 se anaden las matrices 4x4 de mgr+0x60 y de la matriz del render (0x8238A750, solo US).

#include "freecam.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include "generated/dbz3_init.h"
#include "combate.h"
#include "eu_map.h"
#include "ingame/game_style.h"
#include "launcher/i18n.h"
#include "launcher/launcher_state.h"

namespace dbz3 {
bool OverlayOpen();   // main.cpp: menu F1 / launcher encima del juego
}

// Opciones de F1. Son de SESION: el callback las devuelve al origen "linea de comandos" para que SaveConfig no las
// escriba en dbz3_user.toml, y IniciarSesion las apaga en cada arranque.
REXCVAR_DEFINE_BOOL(dbz3_freecam, false, "DBZ3/Dev", "Camara libre de pruebas (no se guarda)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_freecam_pausa, false, "DBZ3/Dev", "Camara libre: congelar el reloj del combate")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_freecam_hud, false, "DBZ3/Dev", "Camara libre: ocultar el HUD")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace dbz3::freecam {
namespace {

constexpr uint32_t kMgrPtr = 0x823D6CF4;
constexpr uint32_t kEstado = 35128;   // mgr+35128: 3..8 = escena/combate (4 = pelea)
constexpr uint32_t kReloj = 35144;    // mgr+35144: cuadros de pelea (f32); lo paran las cinematicas de A3b
constexpr uint32_t kPausa = 35253;    // mgr+35253: 1 = juego en pausa (lo pone sub_820FF8A8 y lo quita sub_820FF9B8)
constexpr uint32_t kFovGlobal = 0x1C4;  // mgr+0x1C4: fov vertical (radianes; el juego usa 0,785)
constexpr uint32_t kPtrCamA = 0x264;  // *(mgr+0x264): camara activa que copia sub_820F4300
constexpr uint32_t kPtrCamB = 0x260;  // *(mgr+0x260): camara seleccionada por el juego (respaldo)
// Struct de camara (sub_82082468 / sub_82082970 / sub_82082908).
constexpr uint32_t kCamOjo = 0x00, kCamObj = 0x10, kCamQuat = 0x20, kCamGiro = 0x30, kCamFov = 0x34,
                   kCamFlags = 0x38;
constexpr float kFovJuego = 0.7853981634f;
constexpr float kPi = 3.1415926536f;
constexpr uint32_t kMatrizRender = 0x8238A750;   // solo US: vista que aplica sub_82082908/sub_82087500

struct Pose {
  float x = 0, y = 0, z = 0;     // desplazamiento respecto al ojo del juego
  float yaw = 0, pitch = 0;      // grados respecto a la orientacion del juego
  float fov = 45.0f;             // vertical, grados
};

std::atomic<bool> g_activa{false};
std::atomic<bool> g_pausa{false};        // opcion "Pausar el juego" aplicada ahora
std::atomic<bool> g_hud{false};          // opcion "Ocultar HUD" aplicada ahora
std::atomic<bool> g_hud_antes{true};     // el HUD estaba visible al activar (se devuelve asi al salir)
std::atomic<bool> g_fov_pisado{false};
std::atomic<bool> g_bloqueo_mando{false};
std::atomic<uint32_t> g_estado_prev{~0u};

std::mutex g_mu;                          // protege ojo/objetivo/yaw/pitch/fov y la base de prueba
float g_ojo[3] = {};                      // ojo libre (lo mueven los controles)
float g_obj[3] = {};                      // objetivo (ojo + direccion * 25)
float g_base_ojo[3] = {};                 // ojo del juego al activar/entrar en combate
float g_base_yaw = 0, g_base_pitch = 0;   // orientacion del juego al activar/entrar
float g_yaw = 0, g_pitch = 0;             // orientacion libre (radianes)
float g_fov = 45.0f;                      // fov pedido por el usuario (grados)
float g_fov_juego = 45.0f;                // fov del juego al activar (grados), para devolverlo al salir
uint32_t g_reloj = 0;                     // valor de mgr+35144 congelado
std::atomic<bool> g_pausa_guest{false};   // la pausa del propio juego (sub_820FF8A8) esta puesta
std::atomic<bool> g_soltar_pausa{false};  // hay que reanudar en el siguiente cuadro del juego
bool g_tiene_base = false;
std::atomic<bool> g_test{false};
int g_pose = 0;
std::vector<Pose> g_poses;

std::atomic<bool> g_marcando{false};

// Registro (DS163).
std::atomic<int> g_log{0};          // 0 = nada; 1 = muestras; 2 = muestras + matrices 4x4
std::atomic<uint32_t> g_muestras{0};
std::atomic<uint32_t> g_cuadros{0};

float Grados(float rad) { return rad * 180.0f / kPi; }
float Radianes(float grados) { return grados * kPi / 180.0f; }
uint8_t* BaseMem() {
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->virtual_membase() : nullptr;
}
float LoadF(uint32_t a) {
  uint8_t* base = BaseMem();
  if (!base) return 0.0f;
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
void StoreF(uint32_t a, float f) {
  uint8_t* base = BaseMem();
  if (!base) return;
  uint32_t u;
  std::memcpy(&u, &f, 4);
  REX_STORE_U32(a, u);
}
uint32_t LoadU(uint32_t a) {
  uint8_t* base = BaseMem();
  return base ? REX_LOAD_U32(a) : 0u;
}
void StoreU(uint32_t a, uint32_t v) {
  uint8_t* base = BaseMem();
  if (base) REX_STORE_U32(a, v);
}
uint8_t LoadU8(uint32_t a) {
  uint8_t* base = BaseMem();
  return base ? REX_LOAD_U8(a) : 0u;
}
void StoreU8(uint32_t a, uint8_t v) {
  uint8_t* base = BaseMem();
  if (base) REX_STORE_U8(a, v);
}
void StoreV(uint32_t a, const float v[3], float w) {
  for (int i = 0; i < 3; ++i) StoreF(a + 4u * uint32_t(i), v[i]);
  StoreF(a + 12, w);
}
bool Finito(float f) { return std::isfinite(f); }
bool EnCombate(uint32_t estado) { return estado >= 3 && estado <= 8; }

// --- struct de camara del juego --------------------------------------------------------------------------
struct Camara {
  uint32_t ptr = 0;
  uint32_t flags = 0;
  float ojo[4] = {}, obj[4] = {}, quat[4] = {};
  float giro = 0.0f, fov = 0.0f;
  bool ok = false;
};

bool LeerCamara(uint32_t ptr, Camara& c) {
  c = Camara{};
  c.ptr = ptr;
  if (!ptr) return false;
  for (int i = 0; i < 4; ++i) {
    c.ojo[i] = LoadF(ptr + kCamOjo + 4u * uint32_t(i));
    c.obj[i] = LoadF(ptr + kCamObj + 4u * uint32_t(i));
    c.quat[i] = LoadF(ptr + kCamQuat + 4u * uint32_t(i));
  }
  c.giro = LoadF(ptr + kCamGiro);
  c.fov = LoadF(ptr + kCamFov);
  c.flags = LoadU(ptr + kCamFlags);
  const float dx = c.obj[0] - c.ojo[0], dy = c.obj[1] - c.ojo[1], dz = c.obj[2] - c.ojo[2];
  const float largo = std::sqrt(dx * dx + dy * dy + dz * dz);
  c.ok = Finito(c.ojo[0]) && Finito(c.ojo[1]) && Finito(c.ojo[2]) && Finito(c.obj[0]) && Finito(c.obj[1]) &&
         Finito(c.obj[2]) && largo > 1e-3f && largo < 1e7f;
  return c.ok;
}

// Escribe ojo/objetivo/fov en la struct ACTIVA y fuerza flags|=1 para que sub_82082868 derive la
// orientacion de ojo->objetivo (es la misma cuenta que hace el juego, sub_82090BB8).
bool EscribirCamara(uint32_t ptr, const float ojo[3], const float obj[3], float fov_rad) {
  if (!ptr) return false;
  StoreV(ptr + kCamOjo, ojo, 1.0f);
  StoreV(ptr + kCamObj, obj, 1.0f);
  StoreF(ptr + kCamGiro, 0.0f);
  StoreF(ptr + kCamFov, fov_rad);
  StoreU(ptr + kCamFlags, LoadU(ptr + kCamFlags) | 1u);
  return true;
}

// Pausa REAL del juego (DS164). "Pausar el juego" dejo de ser solo el reloj: sub_820FF8A8 es el
// congelado del menu de pausa (pone mgr+35253 = 1, para los relojes de los luchadores con sub_82112D30 /
// sub_82111620 y pausa el audio con sub_821AF810); sub_820FF9B8(1) es su reanudacion (los dos los llama
// sub_820FFFC8, el controlador del menu de pausa). Se llaman en el hilo del juego (Aplicar), nunca en la
// interfaz. En EU no hay pareja medida de esas dos direcciones: alli se deja solo mgr+35253 y el reloj [H].
void PausarJuego(uint8_t* base, uint32_t mgr) {
  if (!base || !mgr || g_pausa_guest.exchange(true)) return;
  if (!dbz3::g_guest_eu) {
    // GuestToHostFunction toma el contexto del hilo del guest (ThreadState) y baja r1: llamar a una
    // funcion recompilada con un PPCContext{} local empezaria con una pila invalida (r1 = 0).
    rex::ppc::GuestToHostFunction<void>(sub_820FF8A8);
  }
  StoreU8(mgr + kPausa, 1);
  REXLOG_INFO("dbz3 camara libre: juego en pausa (mgr+35253)");
}

void ReanudarJuego(uint8_t* base) {
  if (!g_pausa_guest.exchange(false)) return;
  uint8_t* mem = BaseMem();
  const uint32_t mgr = mem ? REX_LOAD_U32(dbz3::GA(kMgrPtr)) : 0;
  if (base && mgr) {
    if (!dbz3::g_guest_eu) {
      rex::ppc::GuestToHostFunction<void>(sub_820FF9B8, 1);   // r3 != 0: reanuda animaciones y audio
    }
    StoreU8(mgr + kPausa, 0);
  }
  REXLOG_INFO("dbz3 camara libre: juego reanudado");
}

// Puntero de la camara que consume sub_820F4300 (copia 64 B de *(mgr+0x264)).
uint32_t PunteroCamara(uint32_t mgr) {
  uint32_t ptr = LoadU(mgr + kPtrCamA);
  if (ptr >= 0x82000000u && ptr < 0x83000000u) return ptr;
  ptr = LoadU(mgr + kPtrCamB);
  if (ptr >= 0x82000000u && ptr < 0x83000000u) return ptr;
  return mgr + 0x190;
}

void ActualizarObjetivo() {
  const float cp = std::cos(g_pitch), sp = std::sin(g_pitch);
  const float dir[3] = {std::sin(g_yaw) * cp, sp, std::cos(g_yaw) * cp};
  for (int i = 0; i < 3; ++i) g_obj[i] = g_ojo[i] + dir[i] * 25.0f;
}

void AplicarPose() {
  if (!g_tiene_base || g_poses.empty()) return;
  const Pose& p = g_poses[size_t(g_pose) % g_poses.size()];
  g_ojo[0] = g_base_ojo[0] + p.x;
  g_ojo[1] = g_base_ojo[1] + p.y;
  g_ojo[2] = g_base_ojo[2] + p.z;
  g_yaw = g_base_yaw + Radianes(p.yaw);
  g_pitch = std::clamp(g_base_pitch + Radianes(p.pitch), -1.48f, 1.48f);
  g_fov = (p.fov >= 15.0f && p.fov <= 120.0f) ? p.fov : 45.0f;
  ActualizarObjetivo();
}

// Lee la camara activa (no mgr+0x190: en combate la copia sub_820F4300 de *(mgr+0x264)).
bool LeerCamaraJuego(uint32_t mgr, float ojo[3], float obj[3], float* fov_grados) {
  if (!mgr) return false;
  Camara c;
  if (!LeerCamara(PunteroCamara(mgr), c)) {
    c = Camara{};
    for (int i = 0; i < 3; ++i) {
      c.ojo[i] = LoadF(mgr + 0x190 + 4u * uint32_t(i));
      c.obj[i] = LoadF(mgr + 0x1A0 + 4u * uint32_t(i));
    }
    c.fov = LoadF(mgr + kFovGlobal);
    const float dx = c.obj[0] - c.ojo[0], dy = c.obj[1] - c.ojo[1], dz = c.obj[2] - c.ojo[2];
    c.ok = std::sqrt(dx * dx + dy * dy + dz * dz) > 1e-3f;
  }
  if (!c.ok) return false;
  for (int i = 0; i < 3; ++i) ojo[i] = c.ojo[i], obj[i] = c.obj[i];
  if (fov_grados) *fov_grados = (c.fov > 0.15f && c.fov < 1.6f) ? Grados(c.fov) : 45.0f;
  return true;
}

void CapturarBase(uint32_t mgr) {
  float ojo[3], obj[3], fov = 45.0f;
  if (!LeerCamaraJuego(mgr, ojo, obj, &fov)) return;
  float dx = obj[0] - ojo[0], dy = obj[1] - ojo[1], dz = obj[2] - ojo[2];
  const float largo = std::sqrt(dx * dx + dy * dy + dz * dz);
  dx /= largo, dy /= largo, dz /= largo;
  std::lock_guard<std::mutex> lk(g_mu);
  for (int i = 0; i < 3; ++i) g_base_ojo[i] = ojo[i], g_ojo[i] = ojo[i];
  g_base_yaw = std::atan2(dx, dz);
  g_base_pitch = std::asin(std::clamp(dy, -1.0f, 1.0f));
  g_yaw = g_base_yaw;
  g_pitch = g_base_pitch;
  g_fov_juego = fov;
  g_fov = g_fov_juego;
  g_tiene_base = true;
  if (g_test) AplicarPose(); else ActualizarObjetivo();
}

void Activar(uint32_t mgr) {
  if (g_activa.load()) return;
  float ojo[3], obj[3], fov = 45.0f;
  if (!LeerCamaraJuego(mgr, ojo, obj, &fov)) return;
  CapturarBase(mgr);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_test) AplicarPose(); else ActualizarObjetivo();
  }
  if (!g_test) {
    if (auto* rt = rex::Runtime::instance())
      if (auto* in = static_cast<rex::input::InputSystem*>(rt->input_system())) {
        in->AddUIInputBlocker();
        g_bloqueo_mando = true;
      }
  }
  // Si se activa antes de que el marcador se haya dibujado (HudNativoVisible aun falso por el giro de
  // camara), en combate el estado normal es "visible": asi al salir no se queda oculto (DS162 [VERIFICAR]).
  g_hud_antes = combate::HudNativoVisible() || EnCombate(LoadU(mgr + kEstado));
  g_hud = false;
  g_pausa = false;
  g_fov_pisado = true;
  g_activa = true;
  const uint32_t ptr = PunteroCamara(mgr);
  REXLOG_INFO("dbz3 camara libre: activa (ojo {:.2f} {:.2f} {:.2f}, objetivo {:.2f} {:.2f} {:.2f}, camara "
              "{:08X}){}",
              ojo[0], ojo[1], ojo[2], obj[0], obj[1], obj[2], ptr, g_test ? " [prueba]" : "");
}

void Desactivar() {
  if (!g_activa.exchange(false)) return;
  if (g_bloqueo_mando.exchange(false)) {
    if (auto* rt = rex::Runtime::instance())
      if (auto* in = static_cast<rex::input::InputSystem*>(rt->input_system())) in->RemoveUIInputBlocker();
  }
  if (g_hud.exchange(false)) combate::OcultarHud(!g_hud_antes.load());
  g_pausa = false;
  // La pausa del guest se quita en el hilo del juego (Aplicar), no aqui: esto corre en la interfaz.
  if (g_pausa_guest.load()) g_soltar_pausa = true;
  uint8_t* base = BaseMem();
  const uint32_t mgr = base ? REX_LOAD_U32(dbz3::GA(kMgrPtr)) : 0;
  if (mgr && g_fov_pisado.load()) {
    {
      std::lock_guard<std::mutex> lk(g_mu);
      const uint32_t ptr = PunteroCamara(mgr);
      if (ptr) StoreF(ptr + kCamFov, Radianes(g_fov_juego));
      StoreF(mgr + kFovGlobal, Radianes(g_fov_juego));
    }
  }
  g_fov_pisado = false;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_tiene_base = false;
  }
  REXLOG_INFO("dbz3 camara libre: apagada (camara del juego)");
}

void LeerPeticionTest() {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists("dbz3_freecam.req", ec)) return;
  std::ifstream f("dbz3_freecam.req");
  std::string s;
  if (f) std::getline(f, s);
  f.close();
  fs::remove("dbz3_freecam.req", ec);
  const size_t p = s.find_first_not_of(" \t\r\n");
  if (p == std::string::npos) return;
  s.erase(0, p);
  const size_t q = s.find_last_not_of(" \t\r\n");
  if (q != std::string::npos) s.erase(q + 1);
  if (s == "off") {
    g_test = false;
    Desactivar();
    return;
  }
  char* fin = nullptr;
  const long n = std::strtol(s.c_str(), &fin, 10);
  if (fin == s.c_str() || n < 0 || n >= long(g_poses.size())) return;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_pose = int(n);
    AplicarPose();
  }
  REXLOG_INFO("dbz3 camara libre: prueba pose {} ({})", n + 1, s.c_str());
}

void CrearPanel();

class Panel : public rex::ui::ImGuiDialog {
 public:
  explicit Panel(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (g_test) return;   // la camara de prueba la lleva AplicarCamara (hilo del juego)
    uint8_t* base = BaseMem();
    const uint32_t mgr = base ? REX_LOAD_U32(dbz3::GA(kMgrPtr)) : 0;
    const bool quiere = rex::cvar::GetFlagByName("dbz3_freecam") == "true";
    if (quiere && !g_activa.load()) {
      if (mgr) Activar(mgr);
    } else if (!quiere && g_activa.load()) {
      Desactivar();
      return;
    }
    if (!g_activa.load()) return;
    ActualizarEntrada(io, mgr);
    DibujarAviso(io);
  }

 private:
  void ActualizarEntrada(ImGuiIO& io, uint32_t mgr) {
    if (!mgr) return;
    if (dbz3::OverlayOpen() || io.WantTextInput) return;   // F1 / launcher mandan; no mover la camara
    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    bool salir = false;
    float mover_x = 0, mover_z = 0, mover_y = 0, girar = 0, cabecear = 0, fov = 0;
    bool rapido = false;

    bool teclado_mueve = false, raton_mira = false;
    {
      if (ImGui::IsKeyDown(ImGuiKey_W)) mover_z += 1.0f, teclado_mueve = true;
      if (ImGui::IsKeyDown(ImGuiKey_S)) mover_z -= 1.0f, teclado_mueve = true;
      if (ImGui::IsKeyDown(ImGuiKey_D)) mover_x += 1.0f, teclado_mueve = true;
      if (ImGui::IsKeyDown(ImGuiKey_A)) mover_x -= 1.0f, teclado_mueve = true;
      if (ImGui::IsKeyDown(ImGuiKey_E)) mover_y += 1.0f, teclado_mueve = true;
      if (ImGui::IsKeyDown(ImGuiKey_Q)) mover_y -= 1.0f, teclado_mueve = true;
      rapido = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
      if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        girar += io.MouseDelta.x * 0.0035f;
        cabecear -= io.MouseDelta.y * 0.0030f;
        raton_mira = true;
      }
      if (io.MouseWheel != 0.0f) fov -= io.MouseWheel * 2.5f;
      if (ImGui::IsKeyPressed(ImGuiKey_R, false)) salir = true;
    }

    if (auto* rt = rex::Runtime::instance()) {
      if (auto* in = static_cast<rex::input::InputSystem*>(rt->input_system())) {
        rex::input::X_INPUT_STATE st = {};
        if (in->GetStateForUI(0, &st) == 0) {
          const int16_t lx = int16_t(st.gamepad.thumb_lx), ly = int16_t(st.gamepad.thumb_ly);
          const int16_t rx = int16_t(st.gamepad.thumb_rx), ry = int16_t(st.gamepad.thumb_ry);
          const float dz = 0.15f * 32767.0f;
          // DS164: con mnk_mode el teclado del jugador tambien se convierte en palanca. Si ImGui ya vio la
          // tecla/raton, no se suma otra vez (la camara iba al doble de velocidad con WASD y con el raton).
          if (!teclado_mueve) {
            if (std::abs(int(lx)) > dz) mover_x += float(lx) / 32767.0f;
            if (std::abs(int(ly)) > dz) mover_z += float(ly) / 32767.0f;
            const uint16_t b = uint16_t(st.gamepad.buttons);
            if (b & rex::input::X_INPUT_GAMEPAD_DPAD_UP) mover_y += 1.0f;
            if (b & rex::input::X_INPUT_GAMEPAD_DPAD_DOWN) mover_y -= 1.0f;
          }
          if (!raton_mira) {
            if (std::abs(int(rx)) > dz) girar += float(rx) / 32767.0f * 2.4f * dt;
            if (std::abs(int(ry)) > dz) cabecear += float(ry) / 32767.0f * 1.8f * dt;
          }
          const uint16_t b = uint16_t(st.gamepad.buttons);
          if (b & rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER) fov += 30.0f * dt;
          if (b & rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER) fov -= 30.0f * dt;
          if (b & rex::input::X_INPUT_GAMEPAD_BACK) salir = true;
          if (st.gamepad.left_trigger > 64 || st.gamepad.right_trigger > 64) rapido = true;
        }
      }
    }

    if (salir) {
      rex::cvar::SetFlagByName("dbz3_freecam", "false");
      Desactivar();
      return;
    }

    mover_x = std::clamp(mover_x, -1.0f, 1.0f);
    mover_z = std::clamp(mover_z, -1.0f, 1.0f);
    mover_y = std::clamp(mover_y, -1.0f, 1.0f);
    const float vel = rapido ? 70.0f : 25.0f;
    std::lock_guard<std::mutex> lk(g_mu);
    const float fx = std::sin(g_yaw), fz = std::cos(g_yaw);
    const float rx = std::cos(g_yaw), rz = -std::sin(g_yaw);
    g_ojo[0] += (fx * mover_z + rx * mover_x) * vel * dt;
    g_ojo[1] += mover_y * vel * dt;
    g_ojo[2] += (fz * mover_z + rz * mover_x) * vel * dt;
    g_yaw += girar;
    g_pitch = std::clamp(g_pitch + cabecear, -1.48f, 1.48f);
    if (fov != 0.0f) g_fov = std::clamp(g_fov + fov, 15.0f, 110.0f);
    ActualizarObjetivo();
  }

  void DibujarAviso(ImGuiIO& io) {
    using namespace dbz3::ingame::style;
    const Imagen g = GuestImage(io);
    const float s = std::max(1.0f, g.Size().y / 900.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const char* texto = dbz3::i18n::T("CAMARA LIBRE (R: salir)", "FREE CAMERA (R: exit)");
    const ImVec2 pos(g.p0.x + 24.0f * s, g.p0.y + 20.0f * s);
    const ImVec2 tam = ImGui::CalcTextSize(texto);
    dl->AddRectFilled(ImVec2(pos.x - 10 * s, pos.y - 6 * s), ImVec2(pos.x + tam.x + 10 * s, pos.y + tam.y + 6 * s),
                      IM_COL32(0, 0, 0, 150), 6 * s);
    dl->AddText(ImVec2(pos.x, pos.y), IM_COL32(255, 220, 90, 235), texto);
  }
};

void CrearPanel() {
  static Panel* panel = nullptr;   // vive lo que el juego
  auto* rt = rex::Runtime::instance();
  if (!panel && rt && rt->imgui_drawer()) panel = new Panel(rt->imgui_drawer());
}

void AsegurarPanel() {
  // ConfigureQuickMenu corre antes de OnCreateDialogs (aun no hay DeferUi): el panel se pide en el primer cuadro
  // de camara, cuando el juego ya esta lanzado y la cola de la interfaz existe.
  static std::atomic<bool> pedido{false};
  auto* rt = rex::Runtime::instance();
  if (!rt || !rt->imgui_drawer()) return;
  if (pedido.exchange(true)) return;
  dbz3::launcher::DeferUi(&CrearPanel);
}

void ParsearPoses() {
  static bool hecho = false;
  if (hecho) return;
  hecho = true;
  const char* e = std::getenv("DBZ3_TEST_FREECAM");
  if (!e || !*e) return;
  std::string texto(e);
  size_t ini = 0;
  while (ini <= texto.size()) {
    size_t fin = texto.find(';', ini);
    const std::string trozo = texto.substr(ini, fin == std::string::npos ? std::string::npos : fin - ini);
    Pose p;
    if (std::sscanf(trozo.c_str(), "%f,%f,%f,%f,%f,%f", &p.x, &p.y, &p.z, &p.yaw, &p.pitch, &p.fov) == 6)
      g_poses.push_back(p);
    if (fin == std::string::npos) break;
    ini = fin + 1;
  }
  g_test = !g_poses.empty();
  if (g_test) REXLOG_INFO("dbz3 camara libre: prueba con {} pose(s)", g_poses.size());
  else REXLOG_WARN("dbz3 camara libre: DBZ3_TEST_FREECAM sin poses validas");
}

void HacerNoGuardable(std::string_view nombre, std::string_view valor) {
  // SaveConfig escribe toda cvar modificada. Se vuelve a aplicar como "linea de comandos" (Source::kCommandLine),
  // que SaveConfig no persiste, y asi F1 puede cambiarla en vivo sin que acabe en dbz3_user.toml.
  if (g_marcando.exchange(true)) return;
  const std::string v(valor);
  rex::cvar::ResetToDefault(nombre);
  rex::cvar::SetFlagFromCommandLine(nombre, v);
  g_marcando.store(false);
}

// --- registro por cuadro (DS163) -------------------------------------------------------------------------
void LogMatriz(const char* etiqueta, uint32_t a) {
  if (g_log.load() < 2 || !a) return;
  char b[420];
  int n = std::snprintf(b, sizeof b, " %s [", etiqueta);
  for (int i = 0; i < 16 && n < int(sizeof b) - 24; ++i) {
    n += std::snprintf(b + n, sizeof b - size_t(n), "%s%.2f", i ? " " : "", LoadF(a + 4u * uint32_t(i)));
  }
  std::snprintf(b + n, sizeof b - size_t(n), "]");
  REXLOG_INFO("dbz3 freecam:{}", b);
}

void LogMuestra(const char* fase, uint32_t mgr, const Camara& antes, const float ojo[3], const float obj[3],
                float fov_grados, uint32_t ptr) {
  if (g_log.load() < 1) return;
  if ((g_muestras.fetch_add(1) + 1) % 10 != 0) return;
  REXLOG_INFO(
      "dbz3 freecam[{}] cuadro {} est {} act {} | ptr264={:08X} ptr260={:08X} cam={:08X} flags={:08X} | "
      "leido ojo ({:.2f} {:.2f} {:.2f}) obj ({:.2f} {:.2f} {:.2f}) quat ({:.3f} {:.3f} {:.3f} {:.3f}) fov "
      "{:.3f} | escrito ojo ({:.2f} {:.2f} {:.2f}) obj ({:.2f} {:.2f} {:.2f}) fov {:.3f}",
      fase, g_cuadros.load(), LoadU(mgr + kEstado), int(g_activa.load()),
      LoadU(mgr + kPtrCamA), LoadU(mgr + kPtrCamB), ptr, antes.flags, antes.ojo[0], antes.ojo[1],
      antes.ojo[2], antes.obj[0], antes.obj[1], antes.obj[2], antes.quat[0], antes.quat[1], antes.quat[2],
      antes.quat[3], antes.fov, ojo[0], ojo[1], ojo[2], obj[0], obj[1], obj[2], fov_grados);
  LogMatriz("vista", mgr + 0x60);
  if (!dbz3::g_guest_eu) LogMatriz("render", kMatrizRender);
}

// Estado comun (activacion, pose, pausa, HUD) y escritura de la camara activa. momento: 0 = tras la camara
// del juego (sub_820F47E0), 1 = antes de sub_820F4300, 2 = despues de sub_820F4300.
void Aplicar(uint8_t* base, int momento) {
  if (!base) return;
  ParsearPoses();
  AsegurarPanel();
  const uint32_t mgr = REX_LOAD_U32(dbz3::GA(kMgrPtr));
  if (!mgr) return;
  // Desactivar corre en la interfaz: la reanudacion del guest se hace aqui, en el hilo del juego.
  if (g_soltar_pausa.exchange(false)) ReanudarJuego(base);
  if (momento == 1) g_cuadros.fetch_add(1);
  const uint32_t estado = REX_LOAD_U32(mgr + kEstado);
  const uint32_t anterior = g_estado_prev.exchange(estado);

  if (!g_activa.load()) {
    if (!g_test || !EnCombate(estado)) return;
    Activar(mgr);
    if (!g_activa.load()) return;
  } else if (EnCombate(estado) && !EnCombate(anterior)) {
    CapturarBase(mgr);   // nueva pelea/escena: la camara libre parte otra vez de la del juego
    if (g_pausa.load()) g_reloj = REX_LOAD_U32(mgr + kReloj);
    // En combate el marcador esta visible aunque HudNativoVisible() aun no lo vea (giro de camara).
    if (!g_hud.load()) g_hud_antes = combate::HudNativoVisible() || EnCombate(estado);
    REXLOG_INFO("dbz3 camara libre: nueva escena, camara reiniciada");
  }
  if (!g_activa.load()) return;

  if (g_test) LeerPeticionTest();

  // DS164: "Pausar el juego" usa la pausa del propio juego (ver PausarJuego), no solo el reloj de pelea.
  const bool pausa = rex::cvar::GetFlagByName("dbz3_freecam_pausa") == "true";
  if (pausa != g_pausa.load()) {
    if (pausa) {
      g_reloj = REX_LOAD_U32(mgr + kReloj);
      PausarJuego(base, mgr);
    } else {
      ReanudarJuego(base);
    }
    g_pausa = pausa;
  }
  if (g_pausa.load()) REX_STORE_U32(mgr + kReloj, g_reloj);

  const bool hud = rex::cvar::GetFlagByName("dbz3_freecam_hud") == "true";
  if (hud != g_hud.load()) {
    if (hud) {
      g_hud_antes = combate::HudNativoVisible() || EnCombate(estado);
      g_hud = true;
    } else {
      g_hud = false;
      combate::OcultarHud(!g_hud_antes.load());
    }
  }
  if (g_hud.load()) combate::OcultarHud(true);

  float ojo[3], obj[3], fov = 45.0f;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    for (int i = 0; i < 3; ++i) ojo[i] = g_ojo[i], obj[i] = g_obj[i];
    fov = g_fov;
  }
  const float fov_rad = Radianes(std::clamp(fov, 15.0f, 110.0f));

  // La camara que dibuja sub_820F4300: se escribe la struct ACTIVA (ojo/objetivo/fov + flags|=1) antes del
  // original, que es el ultimo punto que ve el dibujo. Tambien se mantiene mgr+0x190/+0x1A0 (receta A3b).
  const uint32_t ptr = PunteroCamara(mgr);
  if (momento != 2) {
    Camara antes;
    LeerCamara(ptr, antes);
    EscribirCamara(ptr, ojo, obj, fov_rad);
    if (ptr != mgr + 0x190) {
      StoreV(mgr + 0x190, ojo, 1.0f);
      StoreV(mgr + 0x1A0, obj, 1.0f);
    }
    g_fov_pisado = true;
    LogMuestra(momento == 1 ? "4300pre" : "47e0", mgr, antes, ojo, obj, fov, ptr);
  }

  if (momento == 2) {
    // La vista ya la ha construido el juego (sub_82082908) a partir de la struct que escribimos: NO se toca
    // mgr+0x60 (es justo la matriz que acaba de aplicar; reescribirla con otra convencion la romperia). Solo
    // se reafirma el flag por si el juego lo limpio en la struct original y se registra lo que quedo.
    Camara despues;
    LeerCamara(ptr, despues);
    StoreU(ptr + kCamFlags, LoadU(ptr + kCamFlags) | 1u);
    StoreF(mgr + kFovGlobal, fov_rad);
    if (g_log.load() >= 2 && g_cuadros.load() % 10 == 0) {
      REXLOG_INFO("dbz3 freecam[4300post] cam={:08X} flags={:08X} quat ({:.3f} {:.3f} {:.3f} {:.3f}) vista "
                  "ojo ({:.2f} {:.2f} {:.2f}) obj ({:.2f} {:.2f} {:.2f})",
                  ptr, despues.flags, despues.quat[0], despues.quat[1], despues.quat[2], despues.quat[3],
                  ojo[0], ojo[1], ojo[2], obj[0], obj[1], obj[2]);
      LogMatriz("vista", mgr + 0x60);
      if (!dbz3::g_guest_eu) LogMatriz("render", kMatrizRender);
    }
  }
}

}  // namespace

void IniciarSesion() {
  static bool hecho = false;
  if (hecho) return;
  hecho = true;
  ParsearPoses();
  if (const char* e = std::getenv("DBZ3_FREECAM_LOG")) g_log = std::max(0, std::atoi(e));
  rex::cvar::ResetToDefault("dbz3_freecam");
  rex::cvar::ResetToDefault("dbz3_freecam_pausa");
  rex::cvar::ResetToDefault("dbz3_freecam_hud");
  static bool callbacks = false;
  if (!callbacks) {
    callbacks = true;
    for (const char* n : {"dbz3_freecam", "dbz3_freecam_pausa", "dbz3_freecam_hud"})
      rex::cvar::RegisterChangeCallback(n, [](std::string_view name, std::string_view value) {
        HacerNoGuardable(name, value);
      });
  }
}

bool Activa() { return g_activa.load(); }

// Tras la camara del juego (gancho de sub_820F47E0): estado + escritura de la struct activa y de mgr+0x190.
void AplicarCamara(uint8_t* base) { Aplicar(base, 0); }

// Antes de sub_820F4300: ultima escritura que ve el dibujo (la struct que copia el propio sub_820F4300).
void AplicarAntesDelDibujo(uint8_t* base) { Aplicar(base, 1); }

// Despues de sub_820F4300: reafirma la struct y deja mgr+0x60 con la camara pedida (overlays del proyecto).
void TrasLaVista(uint8_t* base) { Aplicar(base, 2); }

}  // namespace dbz3::freecam
