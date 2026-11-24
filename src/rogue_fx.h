#pragma once

#include <imgui.h>
#include "ingame/game_style.h"

namespace dbz3::rogue::fx {
enum class Tipo { Camino, Premio, Evolucion, Jefe, Resultado };
void Pintar(ImDrawList* dl, const dbz3::ingame::style::Area& area, Tipo tipo);
}
