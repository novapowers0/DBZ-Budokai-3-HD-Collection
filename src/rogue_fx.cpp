// Dibujos originales de Dragon Rogue; todos quedan en los margenes de sus pantallas.
#include "rogue_fx.h"

#include <algorithm>
#include <cmath>

namespace dbz3::rogue::fx {
void Pintar(ImDrawList* dl, const dbz3::ingame::style::Area& a, Tipo tipo) {
  if (!dl || a.s <= 0) return;
  const float t = float(ImGui::GetTime());
  const int variante = static_cast<int>(tipo);
  const ImU32 colores[] = {IM_COL32(73, 205, 242, 190), IM_COL32(255, 209, 74, 210),
                            IM_COL32(255, 239, 142, 220), IM_COL32(242, 88, 62, 205),
                            IM_COL32(255, 188, 74, 200)};
  const ImU32 color = colores[variante];
  const auto imagen = dbz3::ingame::style::GuestImage(ImGui::GetIO());
  // Bordes exteriores: nunca se dibuja sobre el retrato, cartas o textos centrales.
  for (int lado = 0; lado < 2; ++lado) {
    // Dejar sitio al radio y al desplazamiento animado, incluso en evolucion.
    const float x = lado ? imagen.p1.x - 24.0f * a.s : imagen.p0.x + 24.0f * a.s;
    for (int i = 0; i < 6; ++i) {
      const float fase = std::fmod(t * (tipo == Tipo::Jefe ? 62.0f : 35.0f) + i * 89.0f + lado * 37.0f, 530.0f);
      const float y = 150.0f + fase;
      const float r = (3.0f + float((i + variante) % 3) * 1.5f) * a.s;
      const ImVec2 p(x + std::sin(t * 1.4f + i) * 3.0f * a.s, a.At(0, y).y);
      if (tipo == Tipo::Jefe) {
        dl->AddTriangleFilled(ImVec2(p.x, p.y-r*2), ImVec2(p.x-r, p.y+r), ImVec2(p.x+r, p.y+r), color);
      } else if (tipo == Tipo::Camino) {
        dl->AddCircle(p, r, color, 8, std::max(1.0f, a.s));
      } else {
        dl->AddLine(ImVec2(p.x-r, p.y), ImVec2(p.x+r, p.y), color, std::max(1.0f, a.s));
        dl->AddLine(ImVec2(p.x, p.y-r*1.5f), ImVec2(p.x, p.y+r*1.5f), color, std::max(1.0f, a.s));
        if (tipo == Tipo::Evolucion) dl->AddCircle(p, r * 1.8f, color, 12, std::max(1.0f, a.s));
      }
    }
  }
}
}
