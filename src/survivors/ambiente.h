// Dragon Budokai Survivors: capa de ambiente de cada escenario (tanda 4: lluvia, hojas, arena, niebla, chispas y nieve). Hojas de 8 cuadros de
// 256x256 en fila (res/survivors/tanda4/03_ambiente/ambiente_<nombre>.png, celda y ancla fijas, en bucle) repetidas en mosaico por toda la
// pantalla, con un cuadro de salida distinto por casilla y el emisor desplazandose (deriva propia + la camara a medias). Va encima del
// mundo y debajo del marcador. Hilo de dibujo.
#pragma once

#include <imgui.h>

namespace dbz3::survivors {

// camara = posicion del jugador en px de pantalla (mundo * px por unidad); t = s; escala = px de pantalla por px de 1080 (con el zoom).
// false si la capa no existe o falta su hoja.
bool DibujarAmbiente(ImDrawList* dl, const char* nombre, float alfa, float W, float H, ImVec2 camara, float t, float escala);

}  // namespace dbz3::survivors
