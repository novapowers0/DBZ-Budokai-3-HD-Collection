// dbz3 - Camara libre de pruebas (DS162/DS163). Ver freecam.cpp.
//
// Se enciende desde F1 (dbz3_freecam, que NO se guarda: vuelve apagada en cada arranque) o desde la variable de
// pruebas DBZ3_TEST_FREECAM. La camara se escribe en la struct que copia sub_820F4300 (*(mgr+0x264): ojo,
// objetivo, fov y flags|=1 para que el juego derive la orientacion) justo antes del dibujo; tambien se mantienen
// mgr+0x190/+0x1A0 (receta A3b) y mgr+0x60 (overlays del proyecto). DS164: "Pausar el juego" usa la pausa del
// propio juego (sub_820FF8A8 / sub_820FF9B8) y en IW la libre manda sobre la camara del vuelo. Fuera de la camara
// libre no se toca nada.
#pragma once

#include <cstdint>

namespace dbz3::freecam {

// Una vez por arranque: deja las opciones en su valor por defecto (apagadas), las marca como no guardables y crea
// el panel que lee el teclado/raton/mando. Lo llama ConfigureQuickMenu (F1).
void IniciarSesion();

// Tras la camara del juego (gancho de sub_820F47E0): estado (activacion, pose, pausa, HUD) y escritura de la
// camara activa. Si esta apagada no hace nada.
void AplicarCamara(uint8_t* base);

// Antes de sub_820F4300 (gancho de iw_mini_vuelo.cpp): ultima escritura que ve el dibujo (la struct que copia
// el propio sub_820F4300). Es la que manda en combate.
void AplicarAntesDelDibujo(uint8_t* base);

// Despues de sub_820F4300: reafirma la struct y deja mgr+0x60 con la camara pedida (overlays).
void TrasLaVista(uint8_t* base);

bool Activa();

}  // namespace dbz3::freecam
