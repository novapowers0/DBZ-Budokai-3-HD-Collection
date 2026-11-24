/**
 ******************************************************************************
 * dbz3 (#59) - sonda de latencia de entrada a presentacion.
 ******************************************************************************
 * La define `InputSystem::GetState` (src/input/input_system.cpp) cada vez que
 * el guest lee una pulsacion NUEVA, y la consume el presentador al presentar
 * (src/ui/d3d12/d3d12_presenter.cpp) si `dbz3_latencia_sonda` esta encendida.
 * Solo mide el host (lectura de mando -> present); el panel y el mando real se
 * miden aparte con camara. No cambia la entrada ni el juego.
 *
 * Los dos puntos estan en rexruntime.dll, asi que comparten el simbolo.
 * No se usa desde rexgpu-xenos.
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace rex::dbz3 {

// Marca (ns de steady_clock) y mascara de la ultima pulsacion nueva leida por
// el guest. 0 = sin pulsacion pendiente.
inline std::atomic<uint64_t> g_input_edge_ns{0};
inline std::atomic<uint32_t> g_input_edge_buttons{0};

}  // namespace rex::dbz3
