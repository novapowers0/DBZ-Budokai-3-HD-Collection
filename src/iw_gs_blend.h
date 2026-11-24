#pragma once
#include <imgui.h>
namespace dbz3::iwgs {
// Centinelas del pase propio; el ImGui normal no debe recibirlos.
inline void Resta(const ImDrawList*,const ImDrawCmd*) {}
inline void Inversa(const ImDrawList*,const ImDrawCmd*) {}
// RGBAQ/CLUT en unidades GS (128 = uno), ALPHA=0x44.
inline void Alfa128(const ImDrawList*,const ImDrawCmd*) {
  // La lectura volatile impide que /OPT:ICF lo fusione con Resta/Inversa:
  // sus direcciones son el identificador del modo de mezcla.
  static volatile int identidad = 0x44;
  (void)identidad;
}
}
