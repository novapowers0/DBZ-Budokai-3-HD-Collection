# DBZ Budokai 3 HD Collection — Guía del paquete v1.5.7

Port nativo de *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360) hecho con ReXGlue SDK. El paquete de Windows contiene `dbz3.exe` con núcleo dual USA/EU, el launcher, el sistema de mods y sus bibliotecas, incluidas `nvngx_dlss.dll` y `NVIDIA_DLSS_LICENSE.txt` para NVIDIA DLSS en equipos compatibles. **No incluye los archivos del juego:** necesitas tu propia copia legal.

**[Descargar v1.5.7 — última versión](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7)** · **[Vídeo de lanzamiento / Launch video](https://youtu.be/47XrgFQ8zaQ)**

## Qué descargar

Abre **Assets** en la página de la versión. Los archivos **Source code** de GitHub no son el juego ni el Kit.

| Plataforma o complemento | Archivo |
|---|---|
| **Windows x64** | `DBZ-Budokai-3-HD-Collection-v1.5.7.zip` (disponible). |
| **Kit de Modding para Windows** | `DBZ3HD-1.5.7-Kit-Modding.zip` (disponible y opcional). Instálalo encima del juego. |
| **Linux amd64 (Vulkan, BETA)** | `DBZ-Budokai-3-HD-Collection-v1.5.7-linux-amd64.tar.gz`, aún sin adjuntar. Si aparece en Assets, extráelo, coloca `default.xex` y `us/` o `eu/` junto a `dbz3` y ejecuta `./dbz3`. Consulta la [guía de Linux](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/docs/LINUX.md). |
| **Mac con Apple Silicon (EXPERIMENTAL, SIN PROBAR)** | M1 o posterior. `DBZ-Budokai-3-HD-Collection-v1.5.7-macos-arm64-EXPERIMENTAL.tar.gz`, aún sin adjuntar. Si aparece en Assets, sigue el `README_MAC.txt` incluido. |

## Instalar y jugar en Windows

1. Descomprime todo el ZIP de Windows en una carpeta escribible, como `C:\Juegos\DBZ3\`.
2. Abre `dbz3.exe` y selecciona los datos de tu copia legal. Elige una sola de estas disposiciones:

   | Fuente | Dónde colocar los datos |
   |---|---|
   | ISO completa de Xbox 360 | Selecciónala en el launcher. Puede conservar el menú de la HD Collection en la raíz; el launcher busca el ejecutable de Budokai 3 dentro. |
   | Archivos extraídos | `default.xex` junto a la carpeta `us/` o `eu/`: ambos al lado de `dbz3.exe` o ambos dentro de `assets/`. No pongas el `.xex` dentro de `us/` o `eu/`. |
   | Carpeta `DBZ3/` del disco | Colócala junto a `dbz3.exe` sin cambiar su estructura. El launcher detecta el ejecutable por tamaño y suma de verificación; no hace falta renombrarlo. |

3. Comprueba que el ejecutable y los datos son de la misma región. Puedes cotejar tamaños y sumas en la [lista de archivos requeridos](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/baserom.md). Pulsa **JUGAR**.
4. Para los modos y los importadores que requieran otro disco, selecciona la ISO correspondiente de tu propia copia de PS2 en **Juego → Elegir ISO**.

## Actualizar una instalación existente

1. Cierra el juego y guarda una copia de `dbz3_user.toml`, tus partidas y datos de usuario (`user_data/`, o `Documents/dbz3` si el launcher indica esa ubicación) y `mods/`.
2. Extrae el ZIP entero encima de tu instalación, conservando los datos de tu copia legal. Sustituye juntos el ejecutable, las DLL y el resto de archivos del paquete. Si utilizas el Kit, extrae después su ZIP de la misma versión.
3. En el primer arranque tras actualizar, los ajustes del historial que aún coinciden con el valor de serie anterior adoptan el nuevo. Los demás se conservan. El launcher mueve los archivos obsoletos de la lista a una subcarpeta por versión dentro de `_respaldo_actualizacion/`, sin borrarlos. Ese respaldo automático no sustituye tu copia manual.

## Modos en v1.5.7

- **Dragon HUB:** pasa de la última fila del menú principal. Retos clásicos conserva su entrada y el HUB admite el ratón.
- **Dragon Budokai Survivors (BETA):** hordas, ataques automáticos, cartas, ocho luchadores, seis escenarios y mejoras permanentes.
- **Dragon Rogue (BETA):** ocho caminos, cartas de Ki, Cuerpo y Apoyo, guardado y reanudación, entrenamiento de Kami y reto diario de El Abismo.
- **Modo Héroe y creador (BETA):** Habitación, Técnicas, Evolución y Colección renovadas. Guarda atuendos, compártelos por código y asígnalos a formas compatibles.
- **Dragon Tactics (EXPERIMENTAL):** tienda, carrusel de luchadores e historial por rival al reanudar.
- **Dragon World (EXPERIMENTAL)** y **Nave de Babidi (BETA):** mejoras de tablero y diálogo. La transición de **Llanura de Budokai 2** se activa al lanzar al rival contra la pared correspondiente: solo en combate local, con núcleo HD USA y el escenario convertido desde tu copia de Budokai 2.
- **Infinite World (EXPERIMENTAL):** cambios en el mapa, los minijuegos y la ruta traducida de arranque y dibujo en cursos compatibles.
- **Leyenda de Mr. Satán (BETA):** voces y música de la escena larga en la primera ronda si están disponibles en tu copia de Budokai 1.
- **Tag Team (BETA)** y **Online (BETA):** protección del relevo en las formas añadidas más altas, mensajes de error más claros y un informe de conexión.

## Mods y Kit de Modding

- Gestiona tus mods desde la pestaña **Mods** o coloca sus carpetas en `mods/` junto a `dbz3.exe`. Para los mods compatibles en modo ISO, el launcher prepara datos locales sin modificar la imagen original.
- Extrae el Kit en la carpeta del juego ya instalado. `DBZ3_ModKit.bat`, `mod center hd/`, `awo_tools/` y `python/` deben quedar junto a `dbz3.exe`. Abre `DBZ3_ModKit.bat` y elige **Comprobar instalación**. Usa la carpeta de datos extraídos si la herramienta necesita leer directamente los archivos del juego. El Kit incluye Python portátil y sus bibliotecas; si usas tu propio Python, necesitas 3.11 o posterior con Tkinter y debes ejecutar `instalar_requisitos.bat` para instalar las bibliotecas. Consulta [la guía rápida](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/docs/02_mods/KIT_GUIA_RAPIDA.md).
- Las recetas de personajes de Budokai 1 y 2 usan movimientos y cámaras de Budokai 3 HD. Esas recetas no necesitan un disco de Budokai 3 para PS2. Reconstruye los mods afectados de Shin Budokai con el Kit actualizado para aplicar la corrección de Dragon Rush.
- [Pack opcional de personajes (en desarrollo)](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing): `DBZ3HD-1.4.3-Personajes.zip`, pensado para el núcleo HD USA. Con el juego cerrado, combina su carpeta `mods/` con la de tu instalación y abre **Personajes** en el launcher.

## Preguntas frecuentes

- **¿No arranca?** Comprueba que el launcher apunta a Budokai 3, no al menú de la colección ni al ejecutable de Budokai 1. Si falta una DLL o el launcher avisa de versiones mezcladas, vuelve a extraer el ZIP completo.
- **¿No puedes entrar a una partida Online?** Prueba con la misma versión y consulta el informe de conexión. No se han confirmado partidas largas entre dos PC ni la compatibilidad Windows/Linux.
- **¿Dónde aviso de un fallo?** Abre un [issue](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/issues) con tu versión, sistema, región, pasos para reproducir y el último archivo de `logs/`.

## English summary

Download the Windows ZIP from **Assets** on the v1.5.7 release page. Extract it, open `dbz3.exe`, select the complete Xbox 360 ISO or the Budokai 3 executable with matching `us/` or `eu/` data from your own legal copy, then press **PLAY**. Use **Game → Choose ISO** for modes and imports that need one of your own PS2 disc images. The Linux amd64 build (Vulkan, BETA) and Apple Silicon Mac build (EXPERIMENTAL, UNTESTED) were not attached at the time of writing; check Assets.

When updating, close the game, back up settings, saves and mods, and replace the EXE and DLLs together with the full new ZIP. Only listed settings still on their old defaults change. Listed obsolete files move to `_respaldo_actualizacion/` without being deleted. To use the optional Windows Modding Kit, extract its ZIP into the installed game folder and open `DBZ3_ModKit.bat` using the bundled portable Python.

Find **Dragon HUB** beyond the last main-menu row. **Dragon Budokai Survivors (BETA)** and **Dragon Rogue (BETA)** debut there. **Hero Mode and character creator (BETA)**, **Dragon Tactics (EXPERIMENTAL)**, **Dragon World (EXPERIMENTAL)**, **Babidi’s Ship (BETA)**, **Infinite World (EXPERIMENTAL)**, **Mr. Satan’s Legend (BETA)**, **Tag Team (BETA)** and **Online (BETA)** receive updates. Budokai 2’s Plains area transition requires a local battle on the US HD core and a stage converted from your own Budokai 2 copy. Long two-PC matches and Windows/Linux compatibility are unconfirmed.

## Créditos y apoyo

- **🇮🇹⚠ 𝐒𝐏𝐈𝐃𝐄𝐑 ⚠🇮🇹** — decompilación de *Infinite World* que sirvió de base para el port de su campaña / *decompilation used to port the Infinite World campaign*.
- **@ArielPVB**, la comunidad de modding de Budokai, ReXGlue SDK y DBZ Burst Limit Recompiled. Consulta los créditos completos en el [README del repositorio](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection).
- [Ko-fi](https://ko-fi.com/novapowers) (opcional / optional).

Copyright (c) 2026 **NovaPowers** · [Licencia MIT](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/LICENSE). Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No está afiliado ni avalado por los titulares de Dragon Ball.
