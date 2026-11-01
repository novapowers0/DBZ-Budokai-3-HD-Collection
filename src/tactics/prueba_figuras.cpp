// Dragon Tactics H1/T2-T3: escena de PRUEBA, solo con DBZ3_TEST_TACTICS_FIGURAS=N (para tools/pruebas.py; el jugador nunca la ve).
// Pinta N figuras de B3 de las 8 candidatas a la vez, en rejilla y en su pose de espera (la guardia del banco de historia de B2),
// todas en UNA Escena (un solo orden de pintor), y registra triangulos, tandas, descartados y el tiempo de CPU por cuadro.
// Solo hay que fijar DBZ3_TEST_TACTICS_SOLAPE=1 para juntar la rejilla y que las figuras se tapen entre si (orden entre tandas).
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

#include "../ps2_modelo.h"

namespace dbz3::pasarela::nave {
bool DibujarFiguraB2En(dbz3::ps2::Escena* comp, ImDrawList* dl, int id_b2, ImVec2 pie, float alto_px, float giro, float t, int traje,
                       int pbank, int panim, const float* cb);   // pasarela_nave.cpp
}

namespace dbz3::tactics {
namespace {

// ids de B2 de la rueda -> Goku, Vegeta, Krilin, Piccolo, Freezer, A-16, A-18, Cell
constexpr int kIdsB2[8] = {0, 3, 4, 6, 15, 16, 18, 20};

class Prueba : public rex::ui::ImGuiDialog {
 public:
  Prueba(rex::ui::ImGuiDrawer* d, int n) : ImGuiDialog(d), n_(n) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const auto t0 = std::chrono::steady_clock::now();
    static const bool solape = std::getenv("DBZ3_TEST_TACTICS_SOLAPE") != nullptr;
    const int cols = std::min(n_, 6), filas = (n_ + cols - 1) / cols;
    const float w = io.DisplaySize.x, h = io.DisplaySize.y;
    const float cw = w / float(cols) * (solape ? 0.45f : 1.0f), ch = h / float(filas) * (solape ? 0.6f : 1.0f);
    const float alto = std::min(ch * 0.85f, cw * 1.7f);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dbz3::ps2::Escena& esc = esc_;   // persistente: sus vectores conservan la memoria de un cuadro al siguiente
    esc.Empezar();
    std::string por_figura;   // triangulos que emite cada figura (con bordes y cortes de rampa)
    size_t antes = 0;
    for (int i = 0; i < n_; ++i) {
      const int c = i % cols, f = i / cols;
      const ImVec2 pie(w * 0.5f + (float(c) - float(cols - 1) * 0.5f) * cw, h * 0.5f + (float(f) + 1.0f - float(filas) * 0.5f) * ch - ch * 0.05f);
      // a distinta altura en cada fila: en el solape, las de abajo (mas cerca de la camara) tapan a las de arriba
      pasarela::nave::DibujarFiguraB2En(&esc, dl, kIdsB2[i % 8], pie, alto * (solape ? 0.8f + 0.1f * float(f) : 1.0f), 0.35f,
                                        float(ImGui::GetTime()), 0, 0, 0, nullptr);
      por_figura += std::to_string(esc.Cuantos() - antes) + " ";
      antes = esc.Cuantos();
    }
    const size_t puestos = esc.Cuantos();
    const auto t1 = std::chrono::steady_clock::now();
    esc.Pintar(dl);
    const auto t2 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t2 - t0).count();
    media_ += (ms - media_) * 0.05;
    poner_ += (std::chrono::duration<double, std::milli>(t1 - t0).count() - poner_) * 0.05;
    if (puestos && ++cuadros_ % 120 == 0) {
      REXLOG_INFO("dbz3 tactics prueba: {} figuras, {} triangulos, {} vertices, tandas {}, descartados {}, pintados {}, CPU {:.2f} ms/cuadro (posar + poner {:.2f}, ordenar + pintar {:.2f}); triangulos por figura: {}",
                  n_, puestos, esc.Vertices(), esc.Tandas(), esc.Descartados(), esc.Pintados(), media_, poner_, media_ - poner_, por_figura);
      if (esc.Tandas() > 1) {   // las ventanas de las tandas, en el orden en que ImGui las dibuja
        std::string orden;
        const ImGuiContext& g = *ImGui::GetCurrentContext();
        for (int k = 0; k < g.Windows.Size; ++k)
          if (std::strncmp(g.Windows[k]->Name, "##dt_tanda", 10) == 0) orden += std::string(g.Windows[k]->Name + 10) + " ";
        REXLOG_INFO("dbz3 tactics prueba: orden de las tandas en ImGui (de atras a delante): {}", orden);
      }
    }
  }

 private:
  dbz3::ps2::Escena esc_;
  int n_;
  double media_ = 0, poner_ = 0;
  int cuadros_ = 0;
};

}  // namespace

void CrearPruebaFiguras(rex::ui::ImGuiDrawer* drawer) {
  const int n = std::clamp(std::atoi(std::getenv("DBZ3_TEST_TACTICS_FIGURAS")), 1, 24);
  new Prueba(drawer, n);   // vive lo que el juego (el dibujante no borra sus dialogos)
}

}  // namespace dbz3::tactics
