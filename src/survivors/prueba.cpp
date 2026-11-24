// Dragon Budokai Survivors: PRUEBA TECNICA (no es un modo de juego). Cientos de Saibaman como IMPOSTORES (cuadros 2D horneados una vez desde el
// modelo 3D de B3 con HornearFiguraB3, survivors/horno.inl) y Goku con el modelo 3D completo (DibujarFiguraB3En), sobre un suelo liso con
// rejilla, con camara cenital inclinada que sigue al jugador. La horda la mueve sim.cpp (60 Hz fijos, rejilla espacial).
// Se activa con el ajuste dbz3_survivors_prueba (apagado de serie) o con DBZ3_TEST_SURVIVORS (tools/pruebas.py):
//   =auto  el jugador anda en circulo y N recorre 100, 300, 600, 1000 y 2000, 10 s cada uno; al acabar registra "survivors prueba: fin"
//   =N     N enemigos (sin valor: 300); palanca / cruceta mueven, LB / RB quitan / ponen 100
//   DBZ3_TEST_SURVIVORS_3D=K: ademas K Saibaman en 3D completo (para comparar el coste con los impostores)
// Cada tramo de 10 s registra una linea de medidas (medias sin el primer segundo).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include "../launcher/launcher_state.h"
#include "../pasarela.h"
#include "../ps2_modelo.h"
#include "sim.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

REXCVAR_DEFINE_BOOL(dbz3_survivors_prueba, false, "DBZ3/Mods",
                    "Dragon Budokai Survivors: prueba tecnica al arrancar (horda de impostores + jugador 3D, medidas en el registro)");

namespace dbz3::pasarela::nave {   // pasarela_nave.cpp
int EstadoFiguraB3(int b3, int forma, int fid);
bool DibujarFiguraB3En(dbz3::ps2::Escena* esc, const dbz3::ps2::Camara& cam, int b3, int forma, dbz3::ps2::V3 suelo, float alto, float giro, int clip, float t, int fid, float dura = 0.0f,
                       float ciclo = -1.0f, int clip_prev = -1, float t_prev = 0.0f, float mezcla = 0.0f);
int HornearFiguraB3(int b3, int forma, int fid, int clip, int cuadros, int dirs, int px, float picado, pasarela::Imagen* atlas, float* ancla);   // survivors/horno.inl
}  // namespace dbz3::pasarela::nave

namespace dbz3::survivors {
namespace {

using Reloj = std::chrono::steady_clock;
double Ms(Reloj::time_point a, Reloj::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

// modelos de tactics_manifest.toml (campo modelo = fid): Goku forma 0 y Saibaman
constexpr int kB3Goku = 0, kFidGoku = 264, kB3Saiba = 42, kFidSaiba = 371;
constexpr int kClipEspera = 0, kClipAndar = 1;
constexpr int kCuadros = 8, kDirs = 8, kPx = 96;   // atlas 768 x 768 (2,3 MB)
constexpr float kPicado = 0.95993f;                // 55 grados sobre el horizonte (camara y horno)
constexpr float kAlto = 1.8f;                      // alto de una figura en unidades de la simulacion
constexpr int kTramos[] = {100, 300, 600, 1000, 2000};
constexpr double kTramoSeg = 10.0, kDescarte = 1.0;
constexpr uint16_t kArriba = 0x0001, kAbajo = 0x0002, kIzq = 0x0004, kDer = 0x0008, kLB = 0x0100, kRB = 0x0200;

// unica lectura de las variables de entorno de la prueba: "" -> DBZ3_TEST_SURVIVORS, "_3D" -> DBZ3_TEST_SURVIVORS_3D
const char* EnvPrueba(const char* sufijo) { return std::getenv((std::string("DBZ3_TEST_SURVIVORS") + sufijo).c_str()); }

std::atomic<uint16_t> g_botones{0};
std::atomic<int> g_lx{0}, g_ly{0};
std::atomic<bool> g_pad_pendiente{false};

rex::input::InputSystem* Input() {
  auto* rt = rex::Runtime::instance();
  return rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
}

class Prueba : public rex::ui::ImGuiDialog {
 public:
  explicit Prueba(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {
    const char* v = EnvPrueba("");
    auto_ = v && std::strcmp(v, "auto") == 0;
    n_ = v && !auto_ && std::atoi(v) > 0 ? std::atoi(v) : 300;
    if (const char* k = EnvPrueba("_3D")) fig3d_ = std::clamp(std::atoi(k), 0, 64);
    REXLOG_INFO("dbz3 survivors prueba: abierta ({}, fig3d={})", auto_ ? "auto" : "manual", fig3d_);
  }

 protected:
  // mando 1 (hilo de la interfaz, como Pad() de tactics/entrada.cpp): botones, palanca izquierda y cruceta
  void LeerPad() {
    if (!g_pad_pendiente.exchange(true)) {
      dbz3::launcher::DeferUi([] {
        uint16_t b = 0;
        int lx = 0, ly = 0;
        if (auto* inp = Input()) {
          rex::input::X_INPUT_STATE s = {};
          if (inp->GetStateForUI(0, &s) == 0) {
            b = static_cast<uint16_t>(s.gamepad.buttons);
            lx = int16_t(s.gamepad.thumb_lx), ly = int16_t(s.gamepad.thumb_ly);
          }
        }
        g_botones = b, g_lx = lx, g_ly = ly;
        g_pad_pendiente = false;
      });
    }
  }

  bool Preparar() {
    if (atlas_.w == 0) {
      const auto h0 = Reloj::now();
      const int e = pasarela::nave::HornearFiguraB3(kB3Saiba, 0, kFidSaiba, kClipAndar, kCuadros, kDirs, kPx, kPicado, &atlas_, ancla_);
      if (e == 2) {
        horno_ms_ = Ms(h0, Reloj::now());
        tex_ = ps2::Crear(atlas_);
      } else if (e == 0 && !avisado_) {
        avisado_ = true;
        REXLOG_ERROR("dbz3 survivors prueba: no se puede hornear el Saibaman (fid {}): falta B3 HD o sus clips", kFidSaiba);
      }
    }
    const int g = pasarela::nave::EstadoFiguraB3(kB3Goku, 0, kFidGoku);
    if (g == 0 && !avisado_) {
      avisado_ = true;
      REXLOG_ERROR("dbz3 survivors prueba: no esta el modelo de Goku (fid {})", kFidGoku);
    }
    return atlas_.w > 0 && tex_ && g == 2;
  }

  void EmpezarTramo() {
    if (auto_) {
      REXLOG_INFO("dbz3 survivors prueba: tramo N={}", kTramos[tramo_]);
      sim_.PonerN(kTramos[tramo_]);
    }
    t_tramo_ = 0, cuadros_ = 0, dt_ = peor_ = sim_ms_ = ene_ms_ = jug_ms_ = cpu_ms_ = fig_ms_ = 0, sim_pasos_ = 0, pintados_ = 0, lentos_ = 0;
  }

  void CerrarTramo() {
    const double c = std::max(1, cuadros_);
    double ws = 0, priv = 0;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pm = {};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof pm))
      ws = double(pm.WorkingSetSize) / 1048576.0, priv = double(pm.PrivateUsage) / 1048576.0;
#endif
    REXLOG_INFO("dbz3 survivors prueba: N={} fps={:.1f} peor_ms={:.1f} sim_ms={:.3f} enemigos_ms={:.3f} jugador_ms={:.2f} cpu_ms={:.2f} pintados={} "
                "mem_ws_mb={:.0f} mem_priv_mb={:.0f} atlas={}x{} atlas_mb={:.1f} horno_ms={:.0f} fig3d={} fig3d_ms={:.2f} cuadros_50ms={}",
                sim_.n, dt_ > 0 ? double(cuadros_) / dt_ : 0.0, peor_ * 1000.0, sim_pasos_ ? sim_ms_ / double(sim_pasos_) : 0.0, ene_ms_ / c,
                jug_ms_ / c, cpu_ms_ / c, int(double(pintados_) / c + 0.5), ws, priv, atlas_.w, atlas_.h, double(atlas_.rgba.size()) / 1048576.0,
                horno_ms_, std::min(fig3d_, sim_.n), fig_ms_ / c, lentos_);
  }

  void OnDraw(ImGuiIO& io) override {
    const auto t0 = Reloj::now();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(58, 82, 60, 255));
    if (!listo_) {
      dl->AddText(ImVec2(24, 24), IM_COL32_WHITE, "Dragon Budokai Survivors (prueba): preparando modelos e impostores...");
      if (!Preparar()) return;
      listo_ = true;
      sim_.Iniciar(2024, auto_ ? kTramos[0] : n_);
      EmpezarTramo();
    }
    const float dtr = std::min(io.DeltaTime, 0.25f);
    t_ += dtr;

    // entrada
    float dx = 0, dy = 0;
    if (auto_) {   // circulo de radio ~10 (velocidad 5, 0,5 rad/s)
      const float a = float(sim_.pasos) * Sim::kDt * 0.5f;
      dx = std::cos(a), dy = std::sin(a);
    } else {
      LeerPad();
      const uint16_t b = g_botones.load(), nuevo = uint16_t(b & ~ultimo_);
      ultimo_ = b;
      const float lx = float(g_lx.load()) / 32767.0f, ly = float(g_ly.load()) / 32767.0f;
      if (std::abs(lx) > 0.25f || std::abs(ly) > 0.25f) dx = lx, dy = -ly;   // arriba en la palanca = hacia el fondo (-z)
      if (b & kIzq) dx = -1;
      if (b & kDer) dx = 1;
      if (b & kArriba) dy = -1;
      if (b & kAbajo) dy = 1;
      if (nuevo & (kLB | kRB)) {
        n_ = std::clamp(n_ + ((nuevo & kRB) ? 100 : -100), 100, Sim::kMax);
        sim_.PonerN(n_);
        REXLOG_INFO("dbz3 survivors prueba: N={} (mando)", n_);
      }
    }
    if (dx != 0 || dy != 0) giro_ = std::atan2(dx, dy);   // giro de DibujarFiguraB3En: 0 = de cara a +z

    // simulacion: pasos fijos de 1/60 s (como mucho 4 por cuadro)
    const auto ts = Reloj::now();
    acum_ += dtr;
    int pasos = 0;
    while (acum_ >= Sim::kDt && pasos < 4) sim_.Paso(dx, dy), acum_ -= Sim::kDt, ++pasos;
    if (pasos == 4) acum_ = 0;
    const double sim_ms = Ms(ts, Reloj::now());

    // camara: ortografica para el suelo y los impostores (S px por unidad, pies del jugador en (cx, cy))
    const float S = H / 22.0f, cx = W * 0.5f, cy = H * 0.55f, sp = std::sin(kPicado), cp = std::cos(kPicado);
    const float jx = sim_.jx, jy = sim_.jy;
    const ImU32 linea = IM_COL32(74, 102, 76, 255);
    for (float x = std::floor((jx - cx / S) / 2.0f) * 2.0f; x <= jx + cx / S + 2.0f; x += 2.0f) {
      const float sx = cx + S * (x - jx);
      dl->AddLine(ImVec2(sx, 0), ImVec2(sx, H), linea, 2.0f);
    }
    for (float z = std::floor((jy - cy / (S * sp)) / 2.0f) * 2.0f; z <= jy + (H - cy) / (S * sp) + 2.0f; z += 2.0f) {
      const float sy = cy + S * sp * (z - jy);
      dl->AddLine(ImVec2(0, sy), ImVec2(W, sy), linea, 2.0f);
    }

    // enemigos: rectangulo de cada uno, descartados los de fuera, ordenados por y de pantalla (pies) sin memoria nueva
    const auto te = Reloj::now();
    const int k3d = std::min(fig3d_, sim_.n);
    const float fpx = S * kAlto / ancla_[2], celda = float(kPx) * fpx, ox = ancla_[0] * fpx, oy = ancla_[1] * fpx;
    int m = 0;
    for (int i = k3d; i < sim_.n; ++i) {
      const float sx = cx + S * (sim_.x[i] - jx), sy = cy + S * sp * (sim_.y[i] - jy);
      if (sx + celda - ox < 0 || sx - ox > W || sy - oy > H || sy - oy + celda < 0) continue;
      claves_[m++] = (uint64_t(Bits(sy)) << 32) | uint32_t(i);
    }
    std::sort(claves_, claves_ + m);
    const int corte = int(std::lower_bound(claves_, claves_ + m, uint64_t(Bits(cy)) << 32) - claves_);   // detras del jugador | delante
    const auto emitir = [&](int a, int b) {
      if (b <= a) return;
      dl->PushTextureID(tex_.id());
      dl->PrimReserve((b - a) * 6, (b - a) * 4);
      for (int q = a; q < b; ++q) {
        const int i = int(claves_[q] & 0xFFFFFFFFu);
        const float sx = cx + S * (sim_.x[i] - jx), sy = cy + S * sp * (sim_.y[i] - jy);
        const float g = std::atan2(jx - sim_.x[i], jy - sim_.y[i]);   // de cara al jugador
        const int d = ((int(std::floor(g / (6.2831853f / kDirs) + 0.5f)) % kDirs) + kDirs) % kDirs;
        const float fase = t_ / std::max(ancla_[3], 0.1f) + sim_.fase[i];
        const int c = int((fase - std::floor(fase)) * kCuadros) % kCuadros;
        dl->PrimRectUV(ImVec2(sx - ox, sy - oy), ImVec2(sx - ox + celda, sy - oy + celda), ImVec2(float(c) / kCuadros, float(d) / kDirs),
                       ImVec2(float(c + 1) / kCuadros, float(d + 1) / kDirs), IM_COL32_WHITE);
      }
      dl->PopTextureID();
    };
    emitir(0, corte);
    double ene_ms = Ms(te, Reloj::now());

    // jugador (y los K Saibaman de comparacion) en 3D: camara de perspectiva de fov pequeno y lejos, con la misma inclinacion
    ps2::Camara cam;
    cam.fov = 0.1f, cam.c = ImVec2(cx, cy), cam.h = ImVec2(W * 0.5f, H * 0.5f), cam.ok = true, cam.za = cam.zb = 0;
    const float D = cam.Foco() / S, ex = jx, ey = sp * D, ez = jy + cp * D;   // ojo = pies del jugador + atras * D
    cam.m[0] = 1, cam.m[4] = 0, cam.m[8] = 0, cam.m[12] = -ex;
    cam.m[1] = 0, cam.m[5] = cp, cam.m[9] = -sp, cam.m[13] = -(cp * ey - sp * ez);
    cam.m[2] = 0, cam.m[6] = sp, cam.m[10] = cp, cam.m[14] = -(sp * ey + cp * ez);
    cam.m[15] = 1;
    const auto tj = Reloj::now();
    esc_.Empezar();
    const bool anda = dx != 0 || dy != 0;
    pasarela::nave::DibujarFiguraB3En(&esc_, cam, kB3Goku, 0, {jx, 0, jy}, kAlto, giro_, anda ? kClipAndar : kClipEspera, t_, kFidGoku);
    const size_t tris_j = esc_.Cuantos();
    const auto tf = Reloj::now();
    for (int i = 0; i < k3d; ++i)
      pasarela::nave::DibujarFiguraB3En(&esc_, cam, kB3Saiba, 0, {sim_.x[i], 0, sim_.y[i]}, kAlto, std::atan2(jx - sim_.x[i], jy - sim_.y[i]), kClipAndar,
                                        t_ + sim_.fase[i] * ancla_[3], kFidSaiba);
    const auto tp = Reloj::now();
    esc_.Pintar(dl);
    const auto tq = Reloj::now();
    const double reparto = esc_.Cuantos() ? double(tris_j) / double(esc_.Cuantos()) : 1.0;   // el pintado se reparte por triangulos
    const double jug_ms = Ms(tj, tf) + Ms(tp, tq) * reparto, fig_ms = Ms(tf, tp) + Ms(tp, tq) * (1.0 - reparto);

    const auto te2 = Reloj::now();
    emitir(corte, m);
    ene_ms += Ms(te2, Reloj::now());

    char txt[160];
    std::snprintf(txt, sizeof txt, "Dragon Budokai Survivors (prueba)  N=%d  fps=%.0f  sim %.2f ms  enemigos %.2f ms  jugador %.2f ms  pintados %d%s", sim_.n,
                  io.Framerate, sim_ms, ene_ms, jug_ms, m, auto_ ? "  [auto]" : "  [LB/RB: N]");
    ImGui::GetForegroundDrawList()->AddText(ImVec2(24, 24), IM_COL32_WHITE, txt);

    // medidas del tramo (sin el primer segundo)
    t_tramo_ += dtr;
    if (t_tramo_ > kDescarte) {
      ++cuadros_, dt_ += io.DeltaTime, peor_ = std::max(peor_, double(io.DeltaTime)), lentos_ += io.DeltaTime > 0.05f;   // A17: cuadros de >50 ms
      sim_ms_ += sim_ms, sim_pasos_ += pasos, ene_ms_ += ene_ms, jug_ms_ += jug_ms, fig_ms_ += fig_ms, pintados_ += m;
      cpu_ms_ += Ms(t0, Reloj::now());
    }
    if (!fin_ && t_tramo_ >= kTramoSeg) {
      CerrarTramo();
      if (auto_ && ++tramo_ >= int(std::size(kTramos))) {
        fin_ = true;
        REXLOG_INFO("dbz3 survivors prueba: fin");
      } else {
        EmpezarTramo();
      }
    }
  }

 private:
  static uint32_t Bits(float sy) {   // y de pantalla -> clave que ordena como el float (positivo)
    const float v = std::clamp(sy + 100000.0f, 0.0f, 1e9f);
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return u;
  }

  Sim sim_;
  ps2::Escena esc_;
  pasarela::Imagen atlas_;
  ps2::Tex tex_;
  float ancla_[4] = {0, 0, 1, 1};
  uint64_t claves_[Sim::kMax] = {};
  bool auto_ = false, listo_ = false, fin_ = false, avisado_ = false;
  int n_ = 300, fig3d_ = 0, tramo_ = 0;
  uint16_t ultimo_ = 0;
  float t_ = 0, giro_ = 0, acum_ = 0;
  double horno_ms_ = 0, t_tramo_ = 0;
  int cuadros_ = 0;
  double dt_ = 0, peor_ = 0, sim_ms_ = 0, ene_ms_ = 0, jug_ms_ = 0, cpu_ms_ = 0, fig_ms_ = 0;
  int64_t sim_pasos_ = 0, pintados_ = 0, lentos_ = 0;
};

}  // namespace

bool PruebaPedida() { return EnvPrueba("") != nullptr || rex::cvar::GetFlagByName("dbz3_survivors_prueba") == "true"; }

void CrearPrueba(rex::ui::ImGuiDrawer* drawer) {
  new Prueba(drawer);   // vive lo que el juego (el dibujante no borra sus dialogos)
}

}  // namespace dbz3::survivors
