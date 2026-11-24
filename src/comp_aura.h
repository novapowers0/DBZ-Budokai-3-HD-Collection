// Filtro corporal y envoltura originales del modo competitivo.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dbz3::comp::aura {
struct Muestra {
  float intensidad = 0;
  std::array<uint8_t, 3> azul{20, 50, 200};
  std::array<uint8_t, 3> blanco{180, 200, 255};
};
void Actualizar(uint8_t* base);
void Crear();
}  // namespace dbz3::comp::aura
