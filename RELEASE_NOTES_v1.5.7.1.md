# v1.5.7.1 — EN

This follow-up to v1.5.7 brings Dragon World and Dragon Mission closer to their PS2 originals in specific scenes and minigames. It also improves outfit editing and protects Hero Mode saves.

## Downloads

| File | Use |
|---|---|
| `DBZ-Budokai-3-HD-Collection-v1.5.7.1.zip` | **Windows game** (required). Includes NVIDIA DLSS. |
| `DBZ3HD-1.5.7.1-Kit-Modding.zip` | Optional Modding Kit. Extract it into the game folder after installing. |
| `...-v1.5.7.1-linux-amd64.tar.gz` | Linux build (Vulkan, beta). Built automatically; it appears a while after publishing. |
| `...-v1.5.7.1-macos-arm64-EXPERIMENTAL.tar.gz` | Apple Silicon Mac (M1 or newer), experimental and untested. Read `README_MAC.txt`. |

## Updating

Back up your settings, saves and mods before updating. On Windows, replace the executable and DLLs together. Keep using your own game copies; game data is not included.

## What's new

### Dragon World (Budokai 2, EXPERIMENTAL)

- **“Battle!” card.** The objective text moves and settles into place as in the PAL original. <!-- b2-card -->
- **Board and Popo.** Pieces start in the PAL positions within their tiles. The board uses the original layers for tile markers and certain effects when their resources are available. When Popo transports a piece, the landing tile also becomes its starting point for the next move. <!-- b2-board -->
- **Shenron's light.** The pillar keeps the original PAL textures' transparency, so you can see more of the background through it. <!-- b2-light -->

### Infinite World (EXPERIMENTAL)

- **Piccolo's pursuit (7DD).** If Piccolo catches you on the fallback route, the results screen now counts it as a defeat. <!-- iw-piccolo -->
- **Ten-round minigame (7D5).** The fallback route uses the original rank cutoffs, checked against the PAL and US versions. <!-- iw-rank -->
- **Stop Cell (7DE).** With the fallback aim and continuous timer, the countdown pauses while your energy is low. The wait ends at the same point as in the original. <!-- iw-cell -->
- **Dragon Mission.** The map uses the original timing for the end of its intro and for the victory pose and tile reload when returning after a battle. <!-- iw-map -->
- **Minigame controls and ranks.** Compatible button rounds recognise simultaneous presses. In 7D4, the stick uses the PS2 input filter and the fallback route uses the PAL hit window for the finishing move. In Frieza's shooting minigame (7D7), the fallback route awards no rank with fewer than seven hits. <!-- iw-input -->
- **More original logic in Infinite World.** Snake Way uses the original task planner, follow camera and falling; collection rounds, the hidden ball and their counters, the ring goal in the Raditz/Namek flights, the flight meter freeze, the button-round UI timing (7DB) and the hit tint now follow the original code. Mission ranks count dodges, teleports and ki charge like the original. <!-- iw-tarde -->

### Hero Mode and character creator (BETA)

- **Undo appearance edits.** The updated Budokai 3 creator keeps up to 32 edits per editing session. Press the left stick to undo when it isn't assigned to Import, or use Ctrl+Z to undo and Ctrl+Y to redo. <!-- cac-undo -->
- **Outfit recipes.** Hair layers and their strands stay together when saving and reapplying an outfit. Switching recipes no longer carries over the previous common hairstyle. <!-- cac-recipes -->
- **Hero saves.** Each Hero save can hold up to 128 unlock entries (up from 64), with extra protection for route progress, rewards and forms when making room. The 100% Collection reward no longer counts itself as a requirement. <!-- hero-save -->
- **Unlock everything.** The launcher's existing action keeps the Hero Collection data and backs up the original `coleccion.toml` as `coleccion.toml.bak` when updating it. <!-- hero-backup -->
- **Unlock everything.** The package includes the `mods_nativos` folder again (the first 1.5.7 download was missing it). <!-- fix74 -->

### Known limits

Dragon World and Infinite World remain experimental. These changes cover specific board events and minigame routes. Hero Mode and the creator remain in beta; the Collection fix removes one circular requirement.

---

# v1.5.7.1 — ES

Esta actualización de la v1.5.7 acerca escenas y minijuegos concretos de Dragon World y Dragon Mission a los originales de PS2. También mejora la edición de atuendos y protege las partidas del Modo Héroe.

## Descargas

| Archivo | Uso |
|---|---|
| `DBZ-Budokai-3-HD-Collection-v1.5.7.1.zip` | **Juego para Windows** (obligatorio). Incluye NVIDIA DLSS. |
| `DBZ3HD-1.5.7.1-Kit-Modding.zip` | Kit de Modding opcional. Descomprímelo en la carpeta del juego después de instalarlo. |
| `...-v1.5.7.1-linux-amd64.tar.gz` | Juego para Linux (Vulkan, beta). Se compila solo y aparece un rato después de publicar. |
| `...-v1.5.7.1-macos-arm64-EXPERIMENTAL.tar.gz` | Mac con Apple Silicon (M1 o posterior), experimental y sin probar. Lee `README_MAC.txt`. |

## Al actualizar

Haz una copia de tus ajustes, partidas y mods antes de actualizar. En Windows, sustituye juntos el ejecutable y las DLL. Sigue usando tus propias copias de los juegos; no se incluyen sus datos.

## Novedades

### Dragon World (Budokai 2, EXPERIMENTAL)

- **Tarjeta de «¡Batalla!».** El texto del objetivo se desplaza y se asienta como en el original PAL. <!-- b2-card -->
- **Tablero y Popo.** Las fichas arrancan en los huecos de casilla de la versión PAL. El tablero usa las capas originales para las señales de casilla y ciertos efectos cuando dispone de sus recursos. Cuando Popo transporta una ficha, la casilla de llegada también pasa a ser el punto de partida del siguiente movimiento. <!-- b2-board -->
- **Luz de Shenron.** La columna conserva la transparencia de las texturas PAL originales y deja ver mejor el fondo. <!-- b2-light -->

### Infinite World (EXPERIMENTAL)

- **Persecución de Piccolo (7DD).** Si Piccolo te alcanza en la ruta de respaldo, el resultado cuenta como derrota. <!-- iw-piccolo -->
- **Minijuego de diez rondas (7D5).** La ruta de respaldo usa los cortes de rango originales, cotejados con las versiones PAL y USA. <!-- iw-rank -->
- **Stop Cell (7DE).** Con la mira de respaldo y el reloj continuo, la cuenta atrás se detiene durante la espera por energía baja. La espera termina en el mismo punto que en el original. <!-- iw-cell -->
- **Dragon Mission.** El mapa recupera los tiempos del original al terminar la introducción y, al volver de un combate ganado, para la pose de victoria y la recarga de las casillas. <!-- iw-map -->
- **Controles y rangos de minijuegos.** Las rondas de botones compatibles reconocen pulsaciones simultáneas. En 7D4 se aplica el filtro de palanca de PS2 y, en la ruta de respaldo, la ventana PAL para acertar el remate. En el minijuego de disparos de Freezer (7D7), la ruta de respaldo no concede rango con menos de siete impactos. <!-- iw-input -->
- **Más lógica original en Infinite World.** El Camino de la Serpiente usa el planificador de tareas, la cámara de seguimiento y la caída originales; las rondas de recogida, la bola oculta y sus contadores, la meta de aros en los vuelos de Raditz y Namek, la congelación del medidor de vuelo, los tiempos de la interfaz de botones (7DB) y el tinte de golpe siguen el código original. El rango de misión cuenta esquivas, teletransportes y carga de ki como el original. <!-- iw-tarde -->

### Modo Héroe y creador de personajes (BETA)

- **Deshacer cambios de aspecto.** El creador renovado de Budokai 3 guarda hasta 32 cambios por sesión de edición. Pulsa la palanca izquierda para deshacer si no está asignada a Importar, o usa Ctrl+Z para deshacer y Ctrl+Y para rehacer. <!-- cac-undo -->
- **Recetas de atuendos.** Las capas de pelo y sus mechones se conservan juntos al guardar y volver a aplicar un atuendo. Cambiar de receta ya no arrastra el peinado común anterior. <!-- cac-recipes -->
- **Guardados de Héroe.** Cada ficha admite hasta 128 registros de desbloqueo (antes 64), con protección adicional para el progreso de ruta, los premios y las formas al hacer sitio. La recompensa del 100 % de la Colección deja de exigirse a sí misma. <!-- hero-save -->
- **Desbloquear todo.** La opción del lanzador conserva los datos de la Colección de Héroe y, al actualizarlos, guarda el `coleccion.toml` original como `coleccion.toml.bak`. <!-- hero-backup -->
- **Desbloquear todo.** El paquete vuelve a incluir la carpeta `mods_nativos` (la primera descarga de la 1.5.7 no la traía). <!-- fix74 -->

### Límites conocidos

Dragon World e Infinite World siguen siendo experimentales. Los cambios cubren eventos concretos del tablero y rutas de minijuegos. Héroe y el creador siguen en beta; la corrección de la Colección elimina un requisito circular.
