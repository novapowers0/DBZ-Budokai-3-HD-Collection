// dbz3 - Combate (src/combate.cpp): lo que pueden usar otras partes del juego.
#pragma once

namespace dbz3::combate {

// Muñeco del laboratorio (el rival del jugador no decide, no contraataca y se queda quieto o en guardia) sin tocar
// el ajuste guardado dbz3_combate_prueba_muneco: 0 = no hay muñeco, 1 = quieto, 2 = en guardia, 3 = ataca (patada cada
// 12 cuadros con su mando virtual: ponerlo ~13 cuadros da un golpe; R14), -1 = lo que diga el ajuste. Cualquier hilo;
// vale desde la siguiente pelea (o ya, si hay una en marcha).
void SetMuneco(int modo);

// Marcador del combate (barras de vida y ki, caras, "V 0", reloj, avisos): true = no se dibuja, como el juego en la
// presentacion y en las cinematicas de los ataques definitivos. Es la tarea "FIX DRAW" del marcador (sub_820DC1A8;
// sub_820DC230 en el modo 6), que el propio juego salta cuando mgr+35253 = 1 o falta el bit alto de 0x823CD870.
// Cualquier hilo; el que lo pone lo quita (minijuegos de IW, cinematicas).
void OcultarHud(bool ocultar);
// El marcador del juego se esta dibujando (la condicion de FIX DRAW y sin OcultarHud): para que los HUD propios
// (modos, retos) salgan y se vayan con el. Solo lectura; cualquier hilo.
bool HudNativoVisible();

// Efecto de sonido del propio juego (gestor de efectos sub_820922A0: banco e id; p. ej. 2/2 = impacto): lo reproduce el hilo del juego en el
// siguiente cuadro de pelea. Cualquier hilo, pero un solo productor; hasta 4 por cuadro y 16 en cola (el resto se descarta).
void PedirSonido(int banco, int id);
// Voces propias de otros personajes (Dragon Tactics): bit N = que el banco de gritos del personaje B3 N este cargado en una ranura libre del
// gestor de efectos (5..15; los que ya no se piden se sueltan). SlotVoz = ranura lista para PedirSonido(ranura, id) o -1. Cualquier hilo.
void CargarVoces(uint64_t mascara);
int SlotVoz(int personaje);

}  // namespace dbz3::combate
