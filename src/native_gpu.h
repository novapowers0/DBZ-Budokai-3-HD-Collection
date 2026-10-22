// dbz3 - Motor grafico "Direct3D 12 nativo (experimental)" (native_gpu/, solo Windows + xex EU/PAL).
//
// El launcher se dibuja con el presentador de la GPU de ReX (rexgpu-xenos), y el renderer nativo
// sustituye ese IGraphicsSystem entero: los dos no caben en el mismo proceso. Por eso JUGAR con este
// motor relanza dbz3.exe con DBZ3_NATIVE_GRAPHICS=1 en el entorno; ese proceso hijo se salta el
// launcher, inyecta los servicios nativos (native_gpu/lab/native_services.cpp) y arranca el juego.
// Sin el ajuste elegido no cambia nada: los ganchos del guest pasan directos a la funcion original.
#pragma once

#include <string>

namespace dbz3::native_gpu {
// Valor de dbz3_gpu_backend para este motor.
inline constexpr const char* kBackend = "native_d3d12";

// Este exe lleva el motor nativo (Windows con el codigo EU recompilado).
bool Supported();
// Este proceso es el hijo lanzado con el motor nativo (DBZ3_NATIVE_GRAPHICS=1).
bool ChildActive();
// Faltan los ficheros del renderer junto al exe (native_gpu/): devuelve cual, o vacio si estan.
std::string MissingFile();
// JUGAR con el motor nativo: relanza este exe con el entorno del renderer. true si el hijo arranco
// (el llamador sale entonces del proceso del launcher).
bool LaunchChild();
}  // namespace dbz3::native_gpu
