// Dragon Budokai Survivors, A16 (P3): musica de batalla de B3 por escenario y sonidos de recogida. Definido en modo_audio.cpp; el coordinador solo
// incluye esta cabecera en modo_interno.h y llama a AudioCuadro en OnDraw y a AudioFin al cerrar el modo.
#pragma once

namespace dbz3::survivors::interno {

// Cada cuadro (hilo de dibujo). `partida` = pantalla de partida en marcha (no resultados ni menus ni saliendo); `escenario` = id del escenario
// (llanura, desierto...); `jefe` = hay un jefe vivo. Con dbz3_survivors_audio_v2 apagado no hace nada.
void AudioCuadro(bool partida, const char* escenario, bool jefe);
// El modo se cierra (vuelta al HUB): corta la musica.
void AudioFin();

}  // namespace dbz3::survivors::interno
