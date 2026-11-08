# Resolución interna y destello de Broly (guía para jugar)

*English version below.*

## Resolución interna

Está en la pestaña **Vídeo**, en la lista **Resolución interna (reinicia el juego)**. El cambio se aplica al volver a abrir el juego.

| Opción | Tamaño del 3D | Qué hace |
|---|---|---|
| **Nativo (720p)** | 1280×720 | Lo normal. Viene marcado por defecto. |
| **Ligero 360p (ruta SD del juego)** | 640×360 | Usa el modo SD del propio juego: la mitad de píxeles por lado. |
| **Solo el 3D a 240p (HUD y menús nítidos)** | 426×240 | Baja solo el escenario y los personajes. |
| **Solo el 3D a 144p (al mínimo)** | 256×144 | Igual que 240p, pero mucho más suave. Es la opción más agresiva. |

Por debajo de 360p **solo se baja el 3D**: las barras de vida, el HUD y los menús siguen a la resolución de vídeo y se ven nítidos.

Ten en cuenta que esto **no da más FPS si el límite es el procesador**, que es lo habitual en equipos modestos. Lo que baja es el trabajo de relleno de la tarjeta gráfica y la VRAM (en la prueba, unos 18 MB menos a 240p y 36 MB menos a 144p). Si el límite es la gráfica, ayuda; si es la CPU, usa escala 1x y apaga el MSAA.

## Broly sin destello

En la pestaña **Juego** hay un interruptor llamado **Broly sin destello**. Viene **apagado por defecto**, así que el juego se comporta como el original.

La definitiva de Broly en un escenario que se puede destruir tapa el impacto con un destello blanco de pantalla completa. Ese destello lo crea un efecto del escenario, no la técnica. Al encender el interruptor se omite **solo ese efecto de pantalla**:

- La técnica y la cámara no cambian.
- El escenario se sigue destruyendo y el combate termina sobre el escenario destruido.
- Solo se aplica a Broly nativo (ID 40, códigos `0x4A0..0x4A9`). Otros personajes, o un clon con otro ID, conservan su destello.

La prueba en juego se hizo en la versión US. La versión EU usa el mismo camino de código, pero no se ha ejecutado en una copia EU.

---

# Internal resolution and Broly's flash (player guide)

## Internal resolution

It is in the **Video** tab, in the **Internal resolution (restart the game)** list. The change is applied the next time you open the game.

| Option | 3D size | What it does |
|---|---|---|
| **Native (720p)** | 1280×720 | The normal setting and the default. |
| **Light 360p (game's SD path)** | 640×360 | Uses the game's own SD mode: half the pixels per side. |
| **3D only at 240p (sharp HUD and menus)** | 426×240 | Lowers only the scenery and characters. |
| **3D only at 144p (minimum)** | 256×144 | Same as 240p, but much softer. The most aggressive option. |

Below 360p **only the 3D is lowered**: health bars, the HUD and menus keep the video resolution and stay sharp.

This **will not raise FPS if the CPU is the limit**, which is common on modest PCs. It lowers GPU fill work and VRAM (about 18 MB less at 240p and 36 MB less at 144p in our test). It helps when the graphics card is the limit; if the CPU is the limit, use 1x scale and turn MSAA off.

## Broly without flash

The **Game** tab has a **Broly without flash** switch. It is **off by default**, so the game behaves like the original.

Broly's ultimate on a destructible stage covers the impact with a full-screen white flash. That flash is created by a stage effect, not by the move. Turning the switch on skips **only that screen effect**:

- The move and the camera are unchanged.
- The stage is still destroyed, and the fight continues on the destroyed stage.
- It only applies to native Broly (ID 40, codes `0x4A0..0x4A9`). Other characters, or a clone with another ID, keep their flash.

The in-game test was done on the US version. The EU version uses the same code path, but it has not been run on an EU copy.
