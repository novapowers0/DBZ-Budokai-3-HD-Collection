// Habitacion del Heroe: eleccion persistente, compartida por la ficha y su UI.
#pragma once

namespace dbz3::heroe {
// Valores estables del guardado heroe_casa (version 1); ausente o invalido = Capsule.
enum class CasaHabitacion { kCapsule = 0, kKame = 1 };
CasaHabitacion CasaActual();
void CambiarCasa();   // La UI devuelve Hab::kCambio: rpg.cpp guarda como los demas campos.
}  // namespace dbz3::heroe
