# Kit de Modding: alcance y límites (14/11)

El Kit es una ventana para hacer mods de DBZ Budokai 3 HD sin escribir comandos: cambiar trajes,
añadir escenarios, técnicas, fusiones y personajes, con deshacer en todo. Se abre con el botón
**Kit de Modding** de la pestaña Personajes del launcher (ajuste `dbz3_launcher_kit`, encendido de
serie), con `DBZ3_ModKit.bat` o con `python "mod center hd/modkit_gui.py"`.

La guía paso a paso está en [KIT_GUIA_RAPIDA](../02_mods/KIT_GUIA_RAPIDA.md). Esta página solo
resume **qué está probado, qué está en pruebas y qué todavía no se puede**.

## Ya probado en el juego

- **Cambiar traje**: un modelo distinto por forma (Base, Super Saiyan...), con vista previa y
  validación antes de guardar. Visto en el juego en base y en Super Saiyan.
- **Nuevo escenario** de Budokai 1 o Budokai 2: sale en el selector y se juega en combate. Los
  marcados «aún no funciona» no se pueden elegir.
- **Fusiones**: duración corta, original, larga, sin límite o los segundos que quieras. Los cuatro
  casos se midieron en el juego (original ~15 s; 10 s exactos; 50 %; sin límite).
- **Nueva técnica**: el movimiento que pones se ve en el juego al usarla, sin cierres.
- **Gogeta como personaje suelto**: sale en la rueda y pelea como uno más.
- **Mis mods**: activar, apagar, deshacer y avisar de choques; «Probar en el juego» dice si cargó
  y **no toca tus partidas**. Todo lo nuevo se aparta a `modkit_backups`; nunca se borra nada.

## En pruebas (funciona en las comprobaciones, falta verlo en el juego)

- Que un **traje nuevo** se vea en la rueda de selección.
- La **segunda zona** de un escenario nuevo.
- El **asistente de personaje** y el **pack de texturas** (su lógica no cambió, no se repitieron).
- Que el **golpe de un movimiento copiado haga daño** (los guiones no llegaron a tiempo).
- **Vegetto y Gotenks** como personajes sueltos: solo se comprobó hasta el arranque del combate.

## Lo que todavía no se puede

- **Gogeta suelto** necesita una casilla libre en la rueda; con muchos
  personajes instalados puede no haberla (el Kit lo avisa y no lo crea).
- **Vegetto y Gotenks sueltos** siguen en pruebas: llegar al inicio del combate no acredita un combate completo.
- En un movimiento copiado, **los golpes y el daño son los del movimiento sustituido**, no los del
  original. No hay cinemáticas con cámara propia, ni banco común, ni crear movimientos pose a pose,
  ni importar de Blender. Las pistas de cámara y los efectos son solo informativas, y solo hay
  movimientos de Budokai 3.
- **Escenarios**: solo de Budokai 1 y Budokai 2, y de **tu propia copia**. Algunos aún cierran el
  juego.
- **El arte de otros juegos lo pone el jugador.** El Kit no reparte modelos, imágenes ni escenarios
  de nadie.
- En modo avanzado, **«Formas y ki» no se edita en el sitio**, y «Mis mods» aún no enseña foto de la
  prueba.

## Si algo va mal

Cada asistente tiene **Deshacer** y todo se guarda con copia en `modkit_backups`. «Algo no va bien»
en la portada del Kit explica qué hacer. El Kit no cambia tus partidas guardadas.

## Alcance de cada operación

| Función y entrada | Fuente y comportamiento observado | Reserva, deshacer y límite |
|---|---|---|
| Traje por forma, en el Kit | Modelo elegido por el jugador; se vio en base y Super Saiyan. | Valida antes de guardar; deshacer desde el asistente. La aparición en la rueda sigue pendiente. |
| Escenario nuevo, en el Kit | Datos de tu copia de Budokai 1 o 2; hay un escenario visto en combate. | Los señalados como no funcionales no se pueden elegir; deshacer desde el asistente. La segunda zona queda pendiente. |
| Fusión, en el Kit | Personajes y datos de B3; se midieron duración original, 10 s, 50 % e ilimitada. | Guardar y deshacer desde el asistente; Gogeta suelto necesita casilla libre. Vegetto y Gotenks solo se comprobaron hasta el arranque. |
| Técnica, en el Kit | Movimiento de B3; se ha visto la animación copiada en juego. | Se puede deshacer. Conserva golpes y daño del movimiento sustituido; el daño del caso Goku/Vegeta aún no se ha medido en combate. |
| Personaje y texturas, en el Kit | Recursos aportados por el jugador. | Validar y deshacer desde el asistente; sus comprobaciones recientes no sustituyen una nueva prueba visual. |

Las pruebas recientes corrigieron una colisión al deshacer dos operaciones rápidas, pero no acreditan todas las combinaciones ni la restauración ante todos los errores. Consulta también [compatibilidad de personajes de XV2](CAC_XV2_COMPATIBILIDAD.md).
