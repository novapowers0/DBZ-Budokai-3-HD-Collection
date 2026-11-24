<p align="center"><img src="docs/logo.png" alt="Dragon Ball Z Budokai 3 HD Recomp" width="480"></p>

# DBZ Budokai 3 HD Collection

[English](README.md) · Español

Un port nativo de *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360), hecho con el [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). Recompila de forma estática el código PowerPC original y añade un launcher, un sistema de mods y nuevas formas de jugar. Necesitas tu propia copia legal del juego original; sus archivos no se incluyen.

**[Descargar v1.5.7 — última versión](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7)** · **[Vídeo de lanzamiento](https://youtu.be/47XrgFQ8zaQ)** · [Historial de cambios](CHANGELOG.md)

## A qué puedes jugar

| Modo | Dónde entrar y qué trae la v1.5.7 |
|---|---|
| **Dragon HUB** | Pasa de la última fila del menú principal del juego. El HUB admite el ratón y Retos clásicos conserva su propio acceso. |
| **Dragon Budokai Survivors (BETA)** | Esquiva hordas mientras tus técnicas atacan solas. Recoge gemas, combina cartas para evolucionar técnicas y gasta zeni en mejoras permanentes. Ocho luchadores, seis escenarios y récords locales. |
| **Dragon Rogue (BETA)** | Recorre ocho caminos, combina cartas de Ki, Cuerpo y Apoyo y encuentra transformaciones y fusiones. Guarda y reanuda partidas, prueba el entrenamiento de Kami o juega al reto diario de El Abismo. |
| **Modo Héroe y creador de personajes (BETA)** | Explora la Habitación, Técnicas, Evolución y Colección renovadas. Guarda atuendos, compártelos por código y asígnalos a formas compatibles. |
| **Dragon Tactics (EXPERIMENTAL)** | Forma equipos con la tienda y el carrusel de luchadores renovados. El historial de cada rival sigue disponible al reanudar. |
| **Dragon World (EXPERIMENTAL)** y **Nave de Babidi (BETA)** | Mejoras de progreso y combate en el tablero y de sincronización del diálogo de Babidi. En combate local, golpea al rival contra la pared que activa el cambio de zona de Llanura. Requiere el núcleo HD USA y el escenario convertido desde tu copia de Budokai 2. |
| **Infinite World (EXPERIMENTAL)** | Mejoras del mapa de misiones y de los minijuegos: más lógica del original y ruta traducida de arranque y dibujo en los cursos compatibles. |
| **Leyenda de Mr. Satán (BETA)** | La primera ronda usa las voces y la música de la escena larga cuando están disponibles en tu copia de Budokai 1. |
| **Tag Team (BETA)** y **Online (BETA)** | Vuelve a cambiar de luchador al bajar de las formas añadidas más altas. Online explica mejor los fallos y guarda un informe de conexión. |

La rueda admite hasta 238 escenarios **añadidos**, con azar entre los visibles y preescucha cuando hay pista. Consulta las [notas completas y los límites conocidos](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7).

## Plataformas y descargas

Abre **Assets** en la [página de la v1.5.7](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7). Los archivos automáticos **Source code** de GitHub sirven para compilar desde el código fuente, no para jugar.

| Plataforma | Paquete |
|---|---|
| **Windows x64** | `DBZ-Budokai-3-HD-Collection-v1.5.7.zip` — disponible; núcleo dual USA/EU con Direct3D 12 y NVIDIA DLSS en equipos compatibles. |
| **Linux amd64 (Vulkan, BETA)** | La compilación automática genera `DBZ-Budokai-3-HD-Collection-v1.5.7-linux-amd64.tar.gz`. Aún no está adjunto; consulta Assets. Cuando esté, descomprímelo, coloca `default.xex` y los datos `us/` o `eu/` que le correspondan junto a `dbz3` y ejecuta `./dbz3`. Consulta la [guía de Linux](docs/LINUX.md). |
| **Mac con Apple Silicon (EXPERIMENTAL, SIN PROBAR)** | M1 o posterior. La compilación automática genera `DBZ-Budokai-3-HD-Collection-v1.5.7-macos-arm64-EXPERIMENTAL.tar.gz`. Aún no está adjunto; consulta Assets y sigue el `README_MAC.txt` incluido si aparece. |

## Instalar en Windows

1. Descomprime el ZIP **entero** de Windows en una carpeta donde puedas escribir, por ejemplo `C:\Juegos\DBZ3\`.
2. Abre `dbz3.exe` y selecciona la ISO completa de Xbox 360 de **tu propia copia legal**, o una carpeta con el ejecutable de Budokai 3 (`default.xex`) junto a sus datos `us/` o `eu/` de la misma región. El launcher también reconoce la carpeta `DBZ3/` tal cual sale del disco, sin renombrar el ejecutable. Consulta la [lista de archivos y sus sumas de verificación](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/baserom.md).
3. Pulsa **JUGAR**. Para los modos e importaciones que requieran datos de PS2, ve a **Juego → Elegir ISO** en el launcher y selecciona la imagen del disco correspondiente de tu propia copia legal.

La [guía de instalación](RELEASE_README.md) muestra las disposiciones de carpetas. Dentro del juego, **F1** abre los ajustes rápidos y **F3** muestra el contador de FPS.

## Actualizar

Cierra el juego. Guarda una copia de `dbz3_user.toml`, de tus partidas y datos de usuario (`user_data/`, o `Documents/dbz3` si el launcher usa esa carpeta), y de `mods/`. Descomprime el paquete entero sobre tu instalación para actualizar juntos el ejecutable, las DLL y los demás archivos del paquete. Conserva los datos de tu copia del juego y no mezcles DLL de versiones distintas.

En el primer arranque, los ajustes incluidos en el historial de migración solo cambian si aún coinciden con el valor de serie anterior. Se conservan los demás valores. El launcher mueve los archivos obsoletos de la lista a una subcarpeta por versión dentro de `_respaldo_actualizacion/`, sin borrarlos. Conserva también tu copia manual.

## Mods y Kit de Modding

- **Instalar y gestionar mods:** usa la pestaña **Mods** del launcher o coloca cada carpeta de mod en `mods/`, junto a `dbz3.exe`. Para los mods compatibles con el modo ISO, el launcher puede preparar los datos locales necesarios sin modificar tu ISO.
- **Kit opcional para Windows:** descomprime `DBZ3HD-1.5.7-Kit-Modding.zip` en la carpeta del juego instalado, de forma que `DBZ3_ModKit.bat`, `mod center hd/`, `awo_tools/` y `python/` queden junto a `dbz3.exe`. Abre `DBZ3_ModKit.bat` y empieza por **Comprobar instalación**. Para las herramientas que requieren acceso directo a los archivos del juego, usa los datos extraídos en una carpeta. El Kit trae Python portátil y sus bibliotecas; si usas tu propio Python, usa la versión 3.11 o posterior con Tkinter y ejecuta `instalar_requisitos.bat` para instalar las bibliotecas. Consulta la [guía rápida del Kit](docs/02_mods/KIT_GUIA_RAPIDA.md).
- **Importar desde tus propios juegos:** las recetas de personajes de Budokai 1 y 2 usan los movimientos y cámaras base de Budokai 3 HD; para esas recetas ya no necesitas una copia aparte de Budokai 3 para PS2. Reconstruye los mods afectados de Shin Budokai con el Kit actualizado para corregir las piezas que desaparecían durante Dragon Rush.
- **Pack opcional de personajes (en desarrollo):** [`DBZ3HD-1.4.3-Personajes.zip`](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) se ofrece por separado y está pensado para el núcleo HD USA. Con el juego cerrado, combina su carpeta `mods/` con la de tu instalación y revisa **Personajes** en el launcher. Respalda antes cualquier mod que vayas a sustituir.

## Preguntas frecuentes

- **¿Por qué no arranca al pulsar JUGAR?** Comprueba que has seleccionado Budokai 3, no el ejecutable del menú de la colección ni el de Budokai 1, y que los datos coinciden con la región USA o EU. Si acabas de actualizar y faltan archivos o son de versiones distintas, vuelve a descomprimir el ZIP entero de Windows.
- **¿Dónde están Survivors y Rogue?** En Dragon HUB, más allá de la última fila del menú principal. Busca allí los accesos, no en la lista de mods del launcher.
- **¿Puedo jugar online entre sistemas?** No están confirmadas las partidas largas entre dos PC ni la compatibilidad Windows/Linux. Usa la misma versión y consulta el informe de conexión si no puedes entrar.
- **¿Cómo aviso de un problema?** Abre un [issue](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/issues) con la versión, el sistema, la región del juego, los pasos para reproducirlo y el último registro de `logs/`.

## Documentación y compilación

Consulta el [índice de documentación](docs/README.md), la [guía de compilación](docs/05_build/COMO_COMPILAR.md) y los [parches para ReXGlue SDK](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/patches/README.md). Se usa ReXGlue v0.10.0 con los parches del proyecto. El código recompilado se genera localmente desde tu propio `.xex`.

## Créditos y apoyo

- **🇮🇹⚠ 𝐒𝐏𝐈𝐃𝐄𝐑 ⚠🇮🇹** — decompilación de *Infinite World*, base del port del mapa, misiones, minijuegos y animaciones.
- **@ArielPVB** — modelos de Guldo, Jeice, Burter, Zarbon (ambas formas), Dodoria y Androide 19 en el pack opcional de personajes.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — herramientas de recompilación estática y runtime, derivado de [Xenia](https://xenia.jp).
- [DBZ Burst Limit Recompiled](https://github.com/iExplosiveRage/DBZ-Burst-Limit-Recompiled) de **iExplosiveRage** — inspiración y mejoras del SDK para el menú rápido, el panel de FPS, los ajustes de vídeo en vivo y FSR, adaptadas de la rama `burstlimit` de [iExplosiveRage/rexglue-sdk](https://github.com/iExplosiveRage/rexglue-sdk).
- Comunidad de modding de Budokai — herramientas, listas de referencia, modelos y ports de Infinite World del pack de personajes. **[SSGPrinceVegeta](https://x.com/SSGPrinceVegeta)** — pack de texturas HD.
- [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) — asignaciones de mandos.
- **NovaPowers** — launcher, sistema de mods y herramientas.

[Apoya el proyecto en Ko-fi](https://ko-fi.com/novapowers). Apoyar es opcional; el port es gratuito.

Copyright (c) 2026 **NovaPowers** · [Licencia MIT](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/LICENSE). Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No está afiliado ni avalado por los titulares de Dragon Ball.
