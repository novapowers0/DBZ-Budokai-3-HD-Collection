// dbz3 - GIGANTES (ficha 19): el rival como jefe a escala gigante contra un luchador normal. Ver gigantes.cpp.
// Solo US; fuera en online.
#pragma once

#include <cstdint>

namespace dbz3::gigantes {

// Regla de reto: el lado (0 jugador 1, 1 jugador 2) a esa escala (0.5..3; 0 = quitar la regla). La opcion
// dbz3_jefe_gigante hace lo mismo con el rival COM de cualquier combate. Quitarla al acabar el reto.
void Activar(int lado, float escala);
bool ReglaActiva();   // hay una regla de reto puesta (para limpiar la que quede de un reto abandonado)
// Escala con la que se dibuja ahora el lado (1 = normal): para encuadrarlo (intro de cine.cpp).
float EscalaDe(uint8_t* base, int lado);

// Cada fotograma de escenario (gancho de clima.cpp); frames_pelea = fotogramas en estado 4.
void Fotograma(uint8_t* base, uint32_t frames_pelea);

}  // namespace dbz3::gigantes
