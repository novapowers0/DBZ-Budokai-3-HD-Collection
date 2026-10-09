# DBZ Budokai 3 HD Collection — Release ejecutable

Copyright (c) 2026 **NovaPowers**. Released under the MIT License.

Este paquete contiene el ejecutable recompilado de *Dragon Ball Z: Budokai 3 HD
Collection* (Xbox 360) con el launcher, el sistema de mods y el pipeline de
modelos. **NO incluye los archivos del juego** (copyright): debes aportar los
de tu copia legal.

## Contenido

- `dbz3.exe` — **el único ejecutable** (núcleo DUAL US/NA + EU/PAL + launcher +
  sistema de mods). Contiene ambas recompilaciones y elige la correcta según el
  `default.xex` que pongas.
- `rexruntime.dll`, `rexgpu-xenos.dll`, `amd_fidelityfx_dx12.dll` — runtime en
  ISA **baseline universal** (SSSE3): funciona en CUALQUIER CPU x64 (Core 2
  2006+), sin variantes. En CPUs modernas es ~5-10% más lento en trabajo de host
  que el runtime AVX2; si lo notas, existe la release de respaldo
  **`v1.1.0-clasico`**.
- `TracyClient.dll` — profiling (requerido por el runtime).
- `amd_fidelityfx_vk.dll`, `SPIRV-Tools-shared.dll` — utilidades del backend Vulkan.
- `mod center hd/` — toolkit de modding (catálogo + scripts + herramientas XDK).
- `RELEASE_README.md` — este archivo.

> **Un solo archivo**: ejecutas `dbz3.exe` y listo. El núcleo es **dual**
> (US/NA + EU/PAL) y al arrancar identifica el `default.xex` por su MD5 para
> usar el código correspondiente. Funciona con el ejecutable US y el EU.

## Cómo instalar y jugar (paso a paso)

El paquete **NO incluye los archivos del juego** (copyright). Aporta los de tu
**copia legal** en una de estas disposiciones (el launcher las detecta todas):

1. **Descomprime** el ZIP en una carpeta, por ejemplo `C:\Juegos\DBZ3\`.
2. Coloca los archivos del juego en una de estas formas:
   - **Opción A — junto a `dbz3.exe`**: `default.xex` + `us/` (y/o `eu/`).
   - **Opción B — dentro de `assets/`**: `assets/default.xex` + `assets/us/`
     (y/o `assets/eu/`).
   - **Opción B2 — el volcado del disco tal cual** (ni renombrar ni mover):
     `DBZ3/yae3_xenon.xex` + `DBZ3/us/` (y/o `eu/`). El launcher busca el
     ejecutable **por tamaño y checksum**, se llame como se llame.
   - **Opción C — jugar directamente del `.iso`** (sin extraer nada): deja el
     `.iso` junto a `dbz3.exe` o usa «Seleccionar ISO...». Se extrae solo el
     ejecutable de Budokai 3 (`DBZ3\yae3_xenon.xex`, unos MB) y se monta el
     resto desde la imagen. Funciona con el **ISO original completo** (con el
     menú de la HD Collection en la raíz del disco). ⚠️ **Los mods necesitan la
     carpeta extraída** (A/B); en modo disco se juega tal cual.
3. **Dentro de `us/`** copia los datos de tu copia legal: `data_cmn.afs`,
   `data_eng/fra/ger/ita/spn/usi.afs`, `data_yah.afs`, `adx_jpn/usa.afs`,
   `lang_jpn/usa.afs`, `opening.sfd`, `Ending00/01.sfd`. La variante PAL va en
   `eu/`.
4. **Copia el ejecutable del juego como `default.xex`** junto a `us/` (es decir,
   junto a `dbz3.exe` en la Opción A, dentro de `assets/` en la B; **nunca**
   dentro de `us/`). Sirve el US/NA (`yae3_xenon.xex`) o el EU/PAL
   (`yae3_xenon_eu.xex`): el launcher elige el núcleo correcto y la región e
   idioma se eligen en el launcher.
5. **Ejecuta `dbz3.exe`** (doble clic).
6. Elige **Región** (USA / EU PAL), **Idioma**, **Vídeo**, **Audio** e **Input**
   y pulsa **Play**.

> Si algo falla, comprueba que `default.xex` y `us/` (o `eu/`) están en la MISMA
> carpeta, o ambos dentro de `assets/`.
> Para extraer los archivos de tu **ISO legal** usa `extract-xiso` (FATX de Xbox
> 360). Tamaños y SHA-256 de cada archivo en `baserom.md`.

## Novedades de esta release — v1.4.3 (2026-10-09)

- **Definitivas**: Zarbon, Dodoria y Androide 19 (pack de personajes 1.4.3) tienen su
  definitiva de Budokai 1 adaptada a Budokai 3 (P+K+G+E en modo hiper); Androide 19 usa
  sus golpes, técnicas y gritos de Budokai 1.
- **Importador** (Kit 1.4.3): Infinite World con sus golpes, técnicas y definitiva
  originales y modo hiper para todos; definitivas de Budokai 1 y Shin Budokai traducidas
  solas; copias USA y europeas de Budokai 1/2, Infinite World y Shin Budokai 1/2.
- **Launcher**: confirmación antes de restablecer, sustituir la partida, borrar un perfil
  o quitar una cápsula; JUGAR ya no congela la ventana; errores explicados en claro.
- DLL del runtime: `rexruntime.dll` 11.109.888 B y `rexgpu-xenos.dll` 6.431.232 B,
  sello 1.4.3: no las mezcles con las de versiones anteriores.

## Release anterior — v1.4.1 (2026-10-05)

- **Rendimiento**: temporización de alta resolución (pausas de 1 ms reales en
  Windows 11) y el juego deja de entrar en el modo ahorro de Windows (núcleos de
  eficiencia), contra los 30 FPS clavados en equipos potentes (issue #8).
- **DRED apagado por defecto**: se arma solo en la sesión siguiente a un
  `device removed`.
- **Registro**: la línea `perf` añade `gpu_wait=`/`syncs=`/`cp_wait=`; los
  frames de más de 50 ms dejan una línea `tiron`; el aviso de FPS bajo solo
  sugiere los ajustes activos y distingue CPU de GPU.
- **Personajes nuevos**: arreglado el cierre en el select al cambiar de traje (Janemba)
  y la música en silencio con un pack de música + el mod de personajes; sin el kit, el
  launcher ya no intenta reconstruir el `_roster` al pulsar JUGAR.
- **Launcher**: aviso de personajes nuevos con el juego EU/PAL.
- **Kit de modding 1.4.1**: página Diagnóstico (`diagnostico.py`).
- DLL del runtime: `rexruntime.dll` 11.039.744 B y `rexgpu-xenos.dll`
  6.385.152 B, sello 1.4.1. El pack de personajes sigue siendo el de la 1.4.0
  (Google Drive).

## Release anterior — v1.4.0 (2026-10-04)

**Menú rápido, vídeo en vivo, launcher nuevo y personajes nuevos.**

- **Menú rápido en partida**: **F1** o **Back + Start** (se puede cambiar a
  L3 + R3 o solo teclado). Imagen, sonido y mando, pantalla y «Todos los
  ajustes» (F4). Se aplica al momento y se guarda solo.
- **«Más FPS con FSR»** (Nativa / Calidad / Equilibrado / Rendimiento / Ultra
  rendimiento): el juego se dibuja por debajo de la resolución y FSR lo
  reescala. En el menú rápido y en la pestaña de escalado.
- **Panel de FPS (F3)** nuevo: FPS del juego, FPS de pantalla y gráfica.
- **Vídeo en vivo**: resolución interna, escalado, nitidez y FXAA sin reiniciar.
- **Launcher rediseñado**, abre en **menos de 1 s** (antes ~31 s) y se maneja
  **con el mando**: LB/RB o Ctrl+Tab cambian de pestaña, START = Jugar, y la
  barra de ayudas muestra teclas o botones (Xbox / PlayStation / Switch).
- **Personajes nuevos** en casillas propias del select (pestaña **Personajes
  nuevos**) e **importador** desde Budokai 1, Budokai 2, Infinite World y
  modelos de la comunidad. Ver «Mods» más abajo.
- **Arreglos**: cierre al usar el menú rápido (lectura del mando sin cerrojo),
  launcher colgado en «trabajando…» con herramientas que escriben mucho,
  pantalla negra con FSR2/FSR3.
- **DLL nuevas** (`rexruntime.dll` 11.034.624 B, `rexgpu-xenos.dll`
  6.372.864 B, sello 1.4.0): no las mezcles con las de versiones anteriores.

Si tu equipo va lento: actualiza, reproduce el problema, cierra el juego y
adjunta el `logs\dbz3_NNN.log`: ya trae sistema, RAM, versiones, configuración,
VRAM y avisos. Para el detalle de rendimiento, activa **Dev → «Registro de
rendimiento»**.

## Historial de versiones

| Versión | Fecha | Resumen |
|---|---|---|
| v1.4.3 | 2026-10-09 | Definitivas de Budokai 1 adaptadas (Zarbon, Dodoria, Androide 19), Infinite World con movesets propios y modo hiper, copias USA/EUR de los juegos de origen, launcher y Kit más fáciles | 1.4.3 |
| v1.4.1 | 2026-10-05 | Rendimiento (temporización precisa, sin modo ahorro de Windows, DRED solo tras un fallo), registro de esperas y tirones, aviso EU de personajes nuevos, Kit con Diagnóstico | 1.4.1 |
| v1.4.0 | 2026-10-04 | Menú rápido en partida (F1 / Back+Start), «Más FPS con FSR», panel de FPS F3, vídeo en vivo, launcher rediseñado con mando y arranque <1 s, personajes nuevos e importador |
| v1.3.0 | 2026-09-30 | Reparar instalación, etiquetas de botón (Xbox/PS/Switch), DRED por defecto, `gamecontrollerdb.txt`, fix del extractor de texturas (#13), pulido y optimización del launcher |
| v1.2.9 | 2026-09-26 | Diagnóstico autoexplicativo: avisos siempre activos (fps, disco, instalación mixta), `vram`/`lim` en `perf`, guardia de VRAM |
| v1.2.8.2 | 2026-09-24 | El upscale de texturas deja de hundir los fps (solo nivel 0 en texturas dinámicas) + `cfg`/`upx_dyn`/`texload` en `perf` |
| v1.2.8.1 | 2026-09-23 | El volcado cubre el HUD/UI sin comprimir; packs RGBA8; tope de 4 versiones por textura (issue #11) |
| v1.2.8 | 2026-09-21 | Fix del volcado de texturas: la carpeta elegida ya llega al plugin (`REXCVAR_QUERY`) |
| v1.2.7 | 2026-09-21 | Packs de texturas estilo PCSX2 (D3D12 y Vulkan) |
| v1.2.6 | 2026-09-20 | Mejora de texturas HD pulida (sin tirones, RGBA8, min-size anti-ringing) + autorreparación del `dbz3_user.toml` |
| v1.2.5 | 2026-09-19 | QoL al perder el foco (mute/dim), diagnóstico de disco (`dbz3_io_logging`) y lectura anticipada |
| v1.2.4 / EX | 2026-09-19 | Volumen real, aviso de nueva versión, FXAA/dither, palancas GPU, datos de usuario portables |
| v1.2.3 | 2026-09-18 | Contador de rendimiento (`dbz3_perf_logging`) y log AFS silenciado |
| v1.2.2 EX | 2026-09-17 | El launcher encuentra el ejecutable solo + fixes del modo ISO + fix del TOML |
| v1.2.1 | 2026-09-14 | Hotfix del launcher (join del pipeline, etiqueta FSR, refresco de la lista de mods) |
| v1.2.0 | 2026-09-14 | Centro de mods renovado + Model Swap HD↔HD pulido + nitidez FSR/CAS |
| v1.1.x | 2026-09 | Núcleo dual US+EU, un solo exe universal (SSSE3), modo disco (ISO), fixes EU, i18n |
| ≤ v1.0.x | 2026-08 | Estabilización inicial (fixes de intro/Demo/Duelo, centro de mods, núcleo dual) |

> Detalle por versión en `docs/01_estructura/HISTORICO_RELEASES.md` (§E) del
> repositorio.

## Mods (WIP)

> 🚧 **Estado: en desarrollo (WIP).** El sistema de mods es experimental y puede
> cambiar. Úsalo con copias de seguridad.

Los mods se gestionan desde las pestañas **Mods**, **Texturas** y **Model Swap**
del launcher. **No modifican** los archivos del juego: aplican un overlay sobre
entradas concretas del AFS, así que cada mod pesa solo ~100 KB.

- **Swap de modelo** B3→B3 nativo: reemplaza el personaje completo (geometría +
  texturas) por otro del catálogo (183 personajes), en cualquier dirección
  (mid-insert virtual).
- **Texturas**: extrae las texturas de un personaje a PNG editables, las editas
  y reconstruyes el mod.
- **Packs de texturas**: sustituye texturas por las tuyas (p. ej. reescaladas con
  IA) **sin tocar los ficheros del juego ni su memoria**.
- **Música** (`og_music`): reemplaza los AFS de audio por región.
- **Personajes nuevos** (v1.4.0, experimental, versión USA): casillas propias en
  el select sin sustituir a nadie. Descargas opcionales:
  - `DBZ3HD-1.4.3-Personajes.zip` (Google Drive: https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) — Janemba, Androide 19, Zarbon, Dodoria,
    Guldo, Jeice y Burter (definitivas WIP: pueden tener fallos). Copia su carpeta
    `mods` junto a `dbz3.exe`.
  - `DBZ3HD-1.4.3-Kit-Modding.zip` — herramientas para crear e importar
    personajes (requiere Python 3.11+; ejecuta `instalar_requisitos.bat` una vez).

## Estado de la release

El modo historia y los modos alternos se han verificado en una pasada completa
**sin errores, crasheos ni fallos conocidos** con la configuración por defecto
(D3D12 + upscaling 2x + 60 FPS). El sistema de mods es la parte experimental:
los swaps y texturas funcionan, pero al ser personalizables, úsalos con copia de
seguridad de tus AFS.

## Bugs conocidos

- **Runtime universal (SSSE3) vs clásico (AVX2)**: el universal es ~5-10% más
  lento en CPUs modernas que el runtime AVX2. Si lo notas, usa la release de
  respaldo `v1.1.0-clasico`.
- **Vulkan experimental**: el backend Vulkan funciona pero el render 3D es
  ~6.5x más lento que D3D12. Usa **D3D12** (por defecto).
- **Personajes nuevos (experimental)**: alguna técnica de agarre de Budokai 1
  sale como un golpe normal y algún efecto puede no ser perfecto. Necesitan la
  versión USA y los datos en carpeta (no modo ISO).
- **Textos nuevos de la v1.4.0** (launcher y menú rápido): en español e inglés;
  en italiano, alemán y francés se ven en inglés por ahora.

## Legal

Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No
afiliado ni avalado por Bandai Namco, Shueisha, Toei Animation ni ningún
titular de los derechos de Dragon Ball. No se distribuye ningún `.xex`, AFS ni
dato del juego. Los archivos del juego son de tu copia legal.

Repositorio: https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection
