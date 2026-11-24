// Dragon Tactics H1/T6: entrada "Dragon Tactics" del Dragon HUB y su arena (suelo plano, camara fija elevada, 3 contra 3 en espera).
// Se registra sola (inicializador estatico de entrada.cpp con hub::RegisterEntry); solo hace falta este .h para abrirla desde otro sitio.
#pragma once

namespace dbz3::tactics {
void AbrirArena();      // hilo del juego, con el menu principal activo (lo que llama el HUB al elegir la entrada)
bool ArenaAbierta();    // la arena esta en pantalla (el "busy" del HUB)
// TACTICS-01: la UNICA puerta de las variables de prueba de Dragon Tactics (las fija solo tools/pruebas.py).
// EnvPrueba("AUTO") = valor de DBZ3_TEST_TACTICS_AUTO o nullptr. Nadie mas lee DBZ3_TEST_TACTICS_* con getenv.
const char* EnvPrueba(const char* nombre);
}  // namespace dbz3::tactics
