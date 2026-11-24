// R16 COMP. Indicadores propios dibujados con primitivas del HUD.
#include "comp_hud.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "combate.h"
#include "comp_estado.h"
#include "generated/dbz3_init.h"
#include "guest_region.h"
#include "ingame/game_style.h"
#include "net/netplay.h"

namespace dbz3::comp::hud {
namespace {
struct Lado {
  std::atomic<float> ki{0}, fatiga{0};
  std::atomic<int64_t> cuando{0};
};
std::array<Lado, 2> lados;
std::atomic<bool> listo{false};
int64_t Ahora() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool Activado() {
  return !dbz3::g_guest_eu && REXCVAR_QUERY(bool, dbz3_modo_competitivo);
}
float F(uint8_t* base, uint32_t a) {
  const uint32_t u = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &u, sizeof(f));
  return std::isfinite(f) ? f : 0.0f;
}
void TrazaGeometria() {
  static bool hecha = false;
  if (hecha) return;
  hecha = true;
  if (!std::getenv("DBZ3_TEST_COMP_HUD_GEOMETRIA")) return;
  // Solo pruebas: vertices de las mismas funciones que consume el renderer,
  // para compararlos con la emision GS del interprete original (sin arte).
  auto traza = [](const char* clase, int lado, float valor, int dibujo, const auto& v) {
    for (int i = 0; i < int(v.size()); ++i)
      REXLOG_INFO("COMP HUD GEO {} {} {} {} {} {} {} {} {}", clase, lado, valor, dibujo, i,
                  v[i].x, v[i].y, v[i].u, v[i].v);
  };
  for (int lado = 0; lado < 2; ++lado) {
    for (int ki : {0, 500, 1000, 2500, 6000}) {
      int dibujo = 0;
      for (int bloque = 0; bloque < 6; ++bloque) {
        traza("ki", lado, float(ki), dibujo++, geometria::Segmento(lado, bloque, 1, false));
        const float f = std::clamp(float(ki) / 1000.0f - bloque, 0.0f, 1.0f);
        if (f > 0) traza("ki", lado, float(ki), dibujo++, geometria::Segmento(lado, bloque, f, true));
      }
    }
    for (float f : {0.0f, 300.0f, 333.3f, 500.0f, 625.0f, 666.6f, 1000.0f})
      traza("fatiga", lado, f, 0, geometria::Fatiga(lado, f, 79, 81));
  }
}
class Hud final : public rex::ui::ImGuiDialog {
 public:
  explicit Hud(rex::ui::ImGuiDrawer* dr) : ImGuiDialog(dr) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!Activado()) return;
    TrazaGeometria();
    listo.store(true, std::memory_order_release);
    if (!combate::HudNativoVisible()) return;
    const auto imagen = ingame::style::GuestImage(io);
    const ImVec2 tam = imagen.Size();
    if (tam.x <= 0 || tam.y <= 0) return;
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(imagen.p0, imagen.p1, true);
    // IW mide 600 x 450 en su espacio 2D. El rectangulo presentado, no la ventana,
    // mantiene la misma posicion con las bandas de una ventana 4:3.
    auto punto = [&](float x, float y) {
      return ImVec2(imagen.p0.x + x * tam.x / 600.0f,
                     imagen.p0.y + y * tam.y / 450.0f);
    };
    auto quad = [&](int lado, int bloque, float f, bool relleno, ImU32 color) {
      const auto v = geometria::Segmento(lado, bloque, f, relleno);
      const ImVec2 p[] = {punto(v[0].x, v[0].y), punto(v[1].x, v[1].y),
                          punto(v[2].x, v[2].y), punto(v[3].x, v[3].y)};
      dl->AddConvexPolyFilled(p, 4, relleno ? color : IM_COL32(22, 24, 31, 230));
      if (!relleno) dl->AddPolyline(p, 4, IM_COL32(154, 157, 165, 255), ImDrawFlags_Closed, 1.0f);
    };
    for (int lado = 0; lado < 2; ++lado) {
      const int64_t cuando = lados[lado].cuando.load(std::memory_order_acquire);
      if (!cuando || Ahora() - cuando > 100) continue;
      const float ki = lados[lado].ki.load(std::memory_order_relaxed);
      for (int bloque = 0; bloque < 6; ++bloque) {
        quad(lado, bloque, 1, false, IM_COL32(255, 255, 255, 204));
        const float f = std::clamp(ki - float(bloque), 0.0f, 1.0f);
        if (f <= 0) continue;
        // IW: lleno amarillo; parcial rojo en el primer bloque, naranja en los demas.
        const int verde = f == 1 ? 255 : bloque == 0 ? 0 : 153;
        quad(lado, bloque, f, true, IM_COL32(255, verde, 0, 255));
      }
      // 0x251060: abanico de fatiga con tres tramos de 333.3 (no una barra inventada).
      // Puntos originales 0x458C70: (5,0),(0,8),(18,14),(41,14),(60,6),(56,0).
      // Se situa bajo el retrato de B3; la vida, el retrato y el reloj siguen nativos.
      const float f = std::clamp(lados[lado].fatiga.load(std::memory_order_relaxed), 0.0f, 1000.0f);
      if (f <= 0) continue;
      const auto v = geometria::Fatiga(lado, f, 79, 81);
      // La arista exterior del abanico forma la curva, recortada segun fatiga.
      ImVec2 curva[4];
      for (int j = 0; j < 4; ++j) curva[j] = punto(v[j + 1].x, v[j + 1].y);
      const float grosor = std::max(1.0f, 2.0f * tam.y / 450.0f);
      dl->AddPolyline(curva, 4, IM_COL32(32, 35, 43, 255), 0, grosor + 2.0f);
      dl->AddPolyline(curva, 4, IM_COL32(202, 208, 216, 255), 0, grosor);
    }
    dl->PopClipRect();
  }

};
}  // namespace

void Crear() {
  static Hud* p = nullptr;
  auto* rt = rex::Runtime::instance();
  if (!p && rt && rt->imgui_drawer()) p = new Hud(rt->imgui_drawer());
}

bool SustituirKi(PPCContext& ctx, uint8_t* base) {
  const uint32_t lado = ctx.r3.u32;
  if (lado > 1 || !Activado() || !listo.load(std::memory_order_acquire)) return false;
  const auto& sesion = net::Session::Get();
  if (sesion.active() && !sesion.CombatRulesAgreed()) return false;
  const uint32_t mgr = REX_LOAD_U32(0x823D6CF4), hud = REX_LOAD_U32(0x82324064);
  if (!mgr || !hud || REX_LOAD_U32(mgr + 35128) != 4 || !combate::HudNativoVisible()) return false;
  // sub_820DB728: lado de pantalla -> lado del luchador. DB6F8 / DB6E0: estado visual.
  const uint32_t actor = REX_LOAD_U8(hud + 3872 + lado);
  if (actor > 1) return false;
  const uint32_t visual = hud + actor * 480;
  if (REX_LOAD_U32(visual + 1936 + 232) || (REX_LOAD_U32(visual + 2208) & 0x400)) {
    lados[lado].cuando.store(0, std::memory_order_release);
    return false;  // forma alternativa/indicador oculto: misma puerta que el dibujo nativo
  }
  lados[lado].ki.store(comp::KiBloques(int(actor)), std::memory_order_relaxed);
  lados[lado].fatiga.store(F(base, mgr + 2096 + actor * 8240 + 6120), std::memory_order_relaxed);
  lados[lado].cuando.store(Ahora(), std::memory_order_release);
  return true;
}
}  // namespace dbz3::comp::hud

// Solo sustituye el DIBUJO del ki, no su actualizacion ni sus nodos/recursos.
// US: generated/dbz3_recomp.15.cpp:5587. EU conserva su funcion original.
REX_HOOK_RAW(sub_820E9588) {
  if (!dbz3::comp::hud::SustituirKi(ctx, base)) __imp__sub_820E9588(ctx, base);
}
