# Dragon Rogue (beta)

Dragon Rogue es un modo de partidas cortas con cartas: avanzas piso a piso, eliges una mejora
tras cada combate y llegas lo más lejos que puedas. Está en pruebas y cambia a menudo.

## Cómo se enciende

Todavía no tiene interruptor en F1. Con el juego cerrado, abre `dbz3_user.toml` (junto al
`dbz3.exe`) y añade:

```
dbz3_rogue = true
```

Al abrir el juego aparece una fila **Dragon Rogue** en el HUB. Si lo apagas, la fila deja de
salir (hace falta reiniciar para quitarla si ya estaba puesta).

## Cómo se juega

1. Elige un camino. La pantalla enseña su resumen, tu récord, el premio y si está abierto o
   cerrado.
2. Cada piso es un combate. Al ganarlo eliges **1 de 3 cartas**: A elige, X cambia las tres
   (una vez por partida, hay otra comprable), Y salta la elección y te cura un 10 % de la vida
   máxima (20 % con la ventaja de Kami), B guarda y sale.
3. Las cartas cambian el daño que haces o recibes, el ki, la vida o las cápsulas. También hay
   sinergias por juntar varias del mismo tipo (Ki, Cuerpo, Apoyo).
4. Cada cierto número de pisos sale un **jefe**, con aviso, retrato y sus reglas. Al ganarlo
   recuperas toda la vida y la siguiente elección trae cartas raras o legendarias.
5. Al terminar (o al perder) sale el resultado con pisos, jefes, récord, sinergias y Zenie
   ganados. Los Zenie van a tu monedero.

## Cartas de evolución y fusión

Además de las cartas normales, tras vencer a un jefe pueden salir **cartas de evolución**:

- **Primera, segunda y tercera transformación**: empiezas cada combate ya transformado, con las
  cápsulas de esa forma puestas en cadena (por ejemplo, Super Saiyan 2 lleva las cápsulas 1, 2 y 3).
- **Media fusión** y **Fusión**: la media fusión prepara la otra; con la **Fusión** pasas a luchar
  como tu personaje fusionado del juego (Goku → Vegito, Goten → Gotenks...). Solo con el traje de
  serie.

Solo salen las que tu luchador tiene de verdad y solo puedes llevar **una evolución activa**: si
coges otra más alta, sustituye a la anterior. La fusión entra ya en su forma fusionada.

## Caminos

Hay ocho: **Torre**, **Supervivencia**, **Kumite**, **Kumite II**, **Torre de la semana**,
**El Abismo**, **Simulación** y **Torneo**. Cada uno tiene sus combates, su jefe cada N pisos, su
forma de llevar la vida y su premio. El **Kumite II** es el reto de los tres jefes con sus reglas
(armadura, gigante y aguantar), en orden. Los cerrados se abren con un buen récord o comprándolos
en el Entrenamiento de Kami.

Desde la misma pantalla se entra en **Retos clásicos**, que devuelve a los modos de siempre
(Torre, Supervivencia, Kumite, Torneo...).

## Reto diario

Una vez al día hay un reto **siempre en El Abismo**, con el **mismo luchador y la misma semilla
para todo el mundo** y sin ventajas de Kami. El primer intento puntúa; los siguientes son
práctica. Los puntos salen de los pisos (×100), los jefes (×500), la vida que te queda (÷10) y el
tiempo en segundos (se resta). Se guardan la racha y los últimos 30 días, y hay un código para
compartir (`ROGUE-DDMM-ABC-PUNTOS`). La pestaña **Dragon Rogue** del Ranking enseña tu mejor
puesto y el código del día.

## Entrenamiento de Kami

Con los Zenie que ganas puedes comprar en Kami:

- caminos cerrados (2000 Zenie cada uno);
- cartas que aparecen en la baraja (cada una tiene su precio);
- ventajas: un segundo cambio de cartas (1500), empezar con +500 de vida (3 niveles, 1200 cada
  uno) y que saltar la carta cure el 20 % en vez del 10 % (1000).

Las ventajas no valen en el reto diario. Lo comprado se guarda en `user_data/dbz3/rogue.toml`.

## Guardado y límites

- La partida se guarda en `user_data/dbz3/rogue.toml` (con copia de seguridad
  `*.pre_rogue.toml`). No toca tu partida de Budokai 3.
- Puedes cerrar el juego y seguir donde lo dejaste; B en la elección guarda y sale.
- Es una **beta**: falta la dificultad III (y la II de los caminos que no tienen modo clásico
  equivalente) y algunas ventajas del diseño aún no existen. El retrato de Kami y algunos fondos
  son provisionales (sale la cara de Piccolo y el fondo del HUB), y algunos textos nuevos aún no
  están traducidos al portugués ni al japonés.
