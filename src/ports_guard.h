// dbz3 - Guardas del frente Ports compartidas por varios ficheros (2026-10-23, CRP).
#pragma once

#include <cstdint>

namespace dbz3::ports {

// Lista de luchadores del combate (cfg+35096 cuenta, cfg+35100 punteros) y accion de cada uno (+3244,
// indice de la tabla de ticks 0x8201F090). Si la lista o la accion no son validas se repara/salta el
// fotograma en vez de llamar a un puntero basura. DBZ3_TEST_SIN_GUARDAS la apaga (pruebas A/B).
bool GuardLuchadores(uint8_t* base);

}  // namespace dbz3::ports
