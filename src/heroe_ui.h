// dbz3 - Modo Heroe: pantallas al estilo Ultimate Tenkaichi (studies 8, equipo A).
//   H3 Create / Edit: vista previa del heroe a la izquierda (tira de giros renderizada del modelo REAL del cuerpo, que
//      saca `mod center hd/cac/vista_heroe.py` del data_cmn.afs del jugador) y la lista a la derecha: Traje, Parte 1-4
//      (panel HSV: tono, saturacion, brillo), Logo, Scouter; en Edit, Pelo y sus 2 colores. Botones Confirmar / Atras /
//      Reset y una linea de ayuda.
//   H4 Select Master: lista de maestros con retrato y el panel MASTER SKILL (% de entrenamiento, modificadores, regla
//      del 60 % y las capsulas que desbloquea).
// Las llama rpg.cpp (vistas kHeroeCrear / kHeroeMaestro) con su candado puesto; el guardado lo hace rpg.cpp (Guardar)
// cuando la pantalla devuelve kConfirmar.
#pragma once

#include <cstdint>

#include "ingame/game_style.h"

namespace dbz3::heroe_ui {

enum class Accion { kNada, kVolver, kConfirmar };

void AbrirCrear(bool edit);   // edit = pantalla Edit (pelo); guarda una copia para Atras
Accion Crear(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t p);
bool EnEdit();

// Mapa del mundo (studies 8 fase 2, H5): sagas como puntos, el heroe en 3D vuela entre ellos, radar de Esferas.
// kConfirmar = empezar la saga siguiente.
void AbrirMapa();
Accion MapaUI(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t p);

// Movesets (studies 8 fase 2): rejilla con los donantes validos (cara + nombre), el elegido marcado y su ficha.
void AbrirMovesets();
Accion MovesetsUI(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t p);

// Fichas de heroe (K): lista de las 8 fichas de cac.toml, duplicar (X) y archivar (Y) sin salir del modo.
void AbrirSlots();
Accion SlotsUI(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t p);

void AbrirMaestros();
Accion Maestros(ImDrawList* dl, const dbz3::ingame::style::Area& a, float t, uint16_t p);

// Vista previa (tira de giros) del cuerpo `id` con el traje `traje`: si falta, se genera en segundo plano.
void PedirVista(int id, int traje);
void DibujarAvatarUT(ImDrawList* dl, const dbz3::ingame::style::Area& a);   // G06a: selector con el cuerpo propio

}  // namespace dbz3::heroe_ui
