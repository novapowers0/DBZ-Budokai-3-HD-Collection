# Carga de Dragon Tactics y Dragon Budokai Survivors

Los dos modos nuevos tardaban mucho en abrir. Desde el 12/11 cargan casi de golpe porque el
trabajo pesado se hace antes y en paralelo:

- el combate de fondo se pide al empezar la carga, no al final, y se prepara a la vez que las
  figuras;
- el catálogo de figuras se busca por índice, no por nombre en cada figura y cada cuadro;
- la altura de cada figura se guarda en caché;
- las figuras no se dibujan con el antialiasing de relleno (solo afectaba al borde);
- en Tactics, las figuras de 2 a 4 estrellas ya no se generan durante la ronda;
- el sonido tiene un tope de voces y los golpes repetidos se juntan en vez de perder voces.

## Cuánto se nota

| Modo | Antes | Ahora |
|---|---|---|
| Dragon Tactics, carga hasta el tablero | 20,6 s | **4,8 s** con la caché hecha |
| Dragon Tactics, si tiene que rehacer los clips | — | 9,8-10,6 s |
| Dragon Budokai Survivors, segunda apertura seguida | — | **0,9 s** |

La **primera vez** que un modo tiene que generar sus modelos y clips de Budokai 3 puede tardar
bastante más (minutos si parte de cero). A partir de ahí se reutiliza lo generado y la carga es
la de la tabla.

También mejoró el ritmo: Tactics pasó de 49 a **57 FPS** en la ronda de 6 contra 6 (la meta de
60 aún no está; el coste que queda está en el dibujo compartido, fuera de Tactics), y Survivors
aguanta **60 FPS con 400 enemigos**.

## Dónde están

- **Dragon Tactics (beta)**: fila propia en el HUB.
- **Dragon Budokai Survivors (beta)**: se enciende en **F1 > MODOS > «Dragon Budokai Survivors»** (apagado de
  serie); al encenderlo aparece su fila en el HUB.

Ninguno de los dos toca tu partida guardada.
