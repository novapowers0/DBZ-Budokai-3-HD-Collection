// Dragon Tactics H1/T6: entrada "Dragon Tactics" del Dragon HUB y su arena (suelo plano, camara fija elevada, 3 contra 3 en espera).
// Se registra sola (inicializador estatico de entrada.cpp con hub::RegisterEntry); solo hace falta este .h para abrirla desde otro sitio.
#pragma once

namespace dbz3::tactics {
void AbrirArena();      // hilo del juego, con el menu principal activo (lo que llama el HUB al elegir la entrada)
bool ArenaAbierta();    // la arena esta en pantalla (el "busy" del HUB)
}  // namespace dbz3::tactics
