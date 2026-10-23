// dbz3 - Editor de colores del traje en la rueda de personajes (E1, studies 5): LB abre un panel con las regiones de
// color del personaje (Piel, Pelo, Ropa 1...), tono / saturacion / brillo por region, vista previa con retraso y
// "Guardar como traje extra" (recolorear.py crear -> mods/color_<id>_<hora>/traje.toml). Se dibuja con las piezas del
// juego (game_style). Lo llama TagTeamPanel::OnDraw.
#pragma once

#include <cstdint>

#include <imgui.h>

namespace dbz3::ingame {

// El panel esta abierto (la rueda no debe recibir el mando: el panel pone su propio bloqueo de entrada).
bool ColorPanelActive();
// Cada fotograma con la rueda viva (select_live) o con el panel abierto. `pressed` = botones recien pulsados del
// mando 1 (mascara XInput); `select_live` = la rueda esta en pantalla; `side`/`id` = el lado con el cursor y su personaje.
void ColorPanelTick(ImGuiIO& io, uint16_t pressed, bool select_live, int id);

}  // namespace dbz3::ingame
