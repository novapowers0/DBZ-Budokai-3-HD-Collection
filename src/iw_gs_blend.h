#pragma once
#include <imgui.h>
namespace dbz3::iwgs {
// Centinelas del pase propio; el ImGui normal no debe recibirlos.
inline void Resta(const ImDrawList*,const ImDrawCmd*) {}
inline void Inversa(const ImDrawList*,const ImDrawCmd*) {}
}
