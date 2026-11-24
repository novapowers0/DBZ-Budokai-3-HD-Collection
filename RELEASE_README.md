# DBZ Budokai 3 HD Collection — Release ejecutable v1.5.7

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
- `nvngx_dlss.dll` y `NVIDIA_DLSS_LICENSE.txt` — NVIDIA DLSS y su licencia.
- `mod center hd/` — toolkit de modding (catálogo + scripts + herramientas XDK).
- `RELEASE_README.md` — este archivo.

> **Un solo archivo**: ejecutas `dbz3.exe` y listo. El núcleo es **dual**
> (US/NA + EU/PAL) y al arrancar identifica el `default.xex` por su MD5 para
> usar el código correspondiente. Funciona con el ejecutable US y el EU.

## Cómo instalar y jugar (paso a paso)

**Si actualizas desde 1.5.6:** guarda una copia de tus ajustes, partidas y mods,
y reemplaza juntos el ejecutable y las DLL. El launcher migra los ajustes que
figuran en su historial y aún coinciden con el valor de serie anterior; conserva
los valores distintos. Mueve los archivos obsoletos de la lista a
`_respaldo_actualizacion/1.5.6/` sin borrarlos.

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
     resto desde la imagen. Usa la **ISO original completa** (con el menú de
     la HD Collection en la raíz del disco). Para usar mods desde la ISO, el
     launcher prepara una copia local de los datos necesarios; no modifica la ISO.
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

## Novedades de esta release — v1.5.7 (23/11/2026)

Incluye los cambios preparados como 1.5.6.1 y 1.5.6.2, que no se publicaron por separado.

- **Dragon Budokai Survivors — nuevo modo (BETA):** hordas con ataques automáticos, cartas y evoluciones, ocho luchadores y seis escenarios. Jefes con repertorio propio en dos fases, Kienzan normal de Krilin con modelo y texturas originales cuando están disponibles, mejoras permanentes con zeni y récords locales. «Más fácil» reduce vida y daño enemigos.
- **Dragon Rogue — nuevo modo (BETA):** ocho caminos, cartas de Ki, Cuerpo y Apoyo, transformaciones, fusiones, guardado y reanudación, entrenamiento de Kami y reto diario de El Abismo. Busca los dos modos nuevos en Dragon HUB, más allá de la última fila del menú principal. Retos clásicos mantiene su acceso.
- **Llanura de Budokai 2:** lanza al rival contra la pared que activa la escena para cambiar de zona en pleno combate local. Requiere núcleo HD USA y el escenario convertido desde tu copia de Budokai 2. **Dragon World e Infinite World (EXPERIMENTAL):** mejoras de tablero, lógica de minijuegos y mapa; en los cursos de IW compatibles se usa la ruta traducida. La Nave de Babidi y la Leyenda de Mr. Satán (BETA) reciben mejoras de diálogo y audio.
- **Modo Héroe y creador (BETA):** Habitación, mapa y ficha renovados, técnicas con maestros y recompensas en Colección. Cuatro pestañas, peinados de Budokai 3, restablecimiento de colores, vista de formas con el mando y atuendos guardables, compartibles por código y asignables por forma compatible.
- **Combate y Dragon HUB:** rueda continua con origen, azar y preescucha cuando hay pista; admite hasta 238 escenarios añadidos. Mejoras de formas, clones y retos. **Dragon Tactics (EXPERIMENTAL):** tienda, carrusel e historial por rival renovados. **Tag Team y Online (BETA):** protección de relevos e informes de conexión más claros; sin certificar partidas largas entre dos PC ni compatibilidad Windows/Linux.
- **Launcher, Kit, idiomas y rendimiento:** migración y respaldos; mejores importadores y generación de contenido desde tus copias; correcciones de Deshacer y Dragon Rush de Shin Budokai al reconstruir mods afectados. 75 textos en siete idiomas; con japonés y datos europeos, interfaz propia en japonés y juego original en inglés. Menos carga de CPU, búsqueda de mods más rápida y recuperación del audio.

### English summary

- **Dragon Budokai Survivors — new mode (BETA):** automatic attacks against hordes, cards and evolutions, eight fighters and six stages. Two phases for bosses with their own movesets, Krillin’s regular Destructo Disc with its original model and textures when available, permanent Zenie upgrades and local records. “Easier” lowers enemy health and damage.
- **Dragon Rogue — new mode (BETA):** eight paths, Ki/Body/Support cards, transformations, fusions, save and resume, Kami’s Training and a daily Abyss challenge. Find both new modes in Dragon HUB beyond the last main-menu row. Classic Challenges keeps its own entry.
- **Budokai 2’s Plains:** knock your opponent into the wall that triggers the scene to change areas mid-fight in a local battle. Requires the US HD core and the stage converted from your Budokai 2 copy. **Dragon World and Infinite World (EXPERIMENTAL):** board, minigame logic and map improvements, with a translated path in compatible IW courses. Babidi’s Ship and Mr. Satan’s Legend (BETA) get dialogue and audio updates.
- **Hero Mode and creator (BETA):** updated Room, map and profile, master unlocks for equipped moves, and Collection rewards. Four tabs, Budokai 3 hairstyles, colour reset, controller previews and outfits you can save, share by code and assign to compatible forms.
- **Combat and Dragon HUB:** a continuous wheel shows source games, supports up to 238 added stages, and offers random picks and music previews where available. Form, clone and challenge fixes. **Dragon Tactics (EXPERIMENTAL):** updated shop, carousel and rival history. **Tag Team and Online (BETA):** switch safeguards and clearer connection reports; long two-PC matches and Windows/Linux compatibility are not certified.
- **Launcher, Kit, languages and performance:** back up settings, saves and mods when updating; replace the EXE and DLLs together. Updated import tools and same-second Undo; rebuild affected Shin Budokai mods for the Dragon Rush fix. 75 texts across seven languages; choosing Japanese with European data keeps the port UI in Japanese and runs the original game in English. Lower CPU load, faster mod lookup and audio recovery.

Notas completas y límites / Full notes and known limits: [v1.5.7](RELEASE_NOTES_v1.5.7.md). En el ZIP, consulta las notas de la versión en [GitHub](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7) cuando se publique.

## Novedades de la v1.5.0 (2026-10-17)

> Vídeo de la versión: tráiler, novedades y tutorial de instalación (ES/EN) en el canal de YouTube de NovaPowers.

- **Dragon HUB:** placas de rango (de KYU 5 a MAESTRO), fondos y anfitriones nuevos, personajes que vuelan a cámara, icono de guardado y marcas **BETA / EXPERIMENTAL** en todo lo que está en construcción.
- **Tag Team (beta):** relevos con teletransporte, también entre luchadores del ordenador (COM vs COM).
- **Mods desde la ISO:** jugando directamente con la `.iso` los mods ya funcionan (la primera vez se prepara en unos segundos).
- **Tus discos de PS2:** usa «Elegir ISO» en Launcher → Juego: Budokai 1 para la Leyenda de Mr. Satán y la importación de personajes, Budokai 2 e Infinite World para sus modos experimentales.
- **Launcher renovado** (ayuda al pasar el ratón, Nova's Mods en tarjetas, mods ACTIVO/APAGADO) y **menú F1 renovado**.
- **Modos nuevos:** Misión 100, Modo Héroe (beta, cambia de estilo entre combates con LB/RB), Leyenda de Mr. Satán (beta, modelos de Budokai 3).
- **Campañas de Infinite World y de Budokai 2 + Nave de Babidi (EXPERIMENTAL):** se pueden probar; les queda trabajo.
- **Cápsula única de transformaciones** y versión EU.
- **Todo traducido** a español, inglés, francés, alemán e italiano (también las descripciones de Nova's Mods).
- Créditos: **🇮🇹⚠ 𝐒𝐏𝐈𝐃𝐄𝐑 ⚠🇮🇹** por la decompilación de Infinite World.

## Novedades de la v1.4.5 (2026-10-14)

- **Pack de texturas HD sin tirones**: las texturas HD se preparan en segundo plano (como
  Burst Limit) y entran poco a poco; el juego ya no se para al llegar a un menú o a un combate,
  y encender o apagar el pack (F1) es inmediato. La memoria se ajusta a la RAM libre de tu PC
  y el juego recuerda las texturas que usas para tenerlas listas la próxima vez.
- **Pack HD de SSGPrinceVegeta en Nova's Mods**: se descarga e instala con un clic desde el
  launcher (2,9 GB). Arreglado: la 1.4.4.5 no cargaba ningún pack de texturas.
- **Launcher que cuida tus mods**: instalar un zip ya no puede romper el mod que tenías; los mods
  metidos en una carpeta de más y los zips con varios mods (como el pack de personajes) se
  colocan solos; nombres con tildes, ruso o japonés; si la partida se cierra de golpe, ofrece
  desactivar los mods nuevos; avisa cuando dos mods cambian lo mismo.
- **Buscadores** en perfiles, Nova's Mods (también por autor) y personajes nuevos.
- **Mandos**: si no hay mando de Xbox conectado, el juego usa SDL solo (DualSense, Switch...).
- **Traducciones**: launcher completo en italiano, alemán y francés.
- **Kit de modding**: rehacer un port con su receta, copia de seguridad antes de cambiar un
  personaje y avisos más claros.
- DLL del runtime: `rexruntime.dll` 11.145.216 B y `rexgpu-xenos.dll` 6.537.216 B,
  sello 1.4.5: no las mezcles con las de versiones anteriores.

## Release anterior — v1.4.3 (2026-10-09)

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

| Versión | Fecha | Resumen | Pack de personajes |
|---|---|---|---|
| v1.5.7 | 2026-11-23 | Debut de Survivors y Rogue (BETA), transición en Llanura B2 local USA, mejoras por modo | 1.4.3 |
| v1.5.0 | 2026-10-17 | Dragon HUB, Tag Team con teletransporte, mods desde la ISO, pasarela PS2 (B1/B2/IW), launcher y F1 renovados, Misión 100, Modo Héroe y Leyenda de Mr. Satán (beta), campañas IW y B2 + Nave de Babidi (experimental), todo traducido | 1.4.3 |
| v1.4.5.2 | 2026-10-14 | Tag Team: ring out con compañero vivo = relevo; Online: instalar los mods que faltan desde el panel, UPnP/STUN; ports oficiales recreables; campañas con textos automáticos | 1.4.3 |
| v1.4.5.1 | 2026-10-14 | El pack HD instalado desde el launcher se ve al pulsar JUGAR, sin reiniciar | 1.4.3 |
| v1.4.5 | 2026-10-14 | Pack HD sin tirones (carga en segundo plano, precarga aprendida) y en Nova's Mods, launcher que coloca y protege los mods, buscadores, traducciones IT/DE/FR | 1.4.3 |
| v1.4.4 | 2026-10-12 | Tag Team y Online (beta) nativos, Nova's Mods, historia de Freezer, selector de transformación (beta), Budokai 2 en el importador | 1.4.3 |
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
  - `DBZ3HD-1.5.7-Kit-Modding.zip`, si está adjunto — herramientas para crear
    e importar personajes (requiere Python 3.11+; ejecuta `instalar_requisitos.bat` una vez).

## Estado de la release

El candidato aún no se ha publicado; falta verificar los paquetes finales del
juego y del Kit. Las pruebas disponibles cubren casos concretos, no todos los
recorridos o combinaciones. En Dragon World, el resumen de menos de siete Bolas
muestra el recuento, pero difiere del original. El sistema de mods sigue en
desarrollo; guarda una copia de tus AFS antes de usar las herramientas.

## Bugs conocidos

- **Runtime universal (SSSE3) vs clásico (AVX2)**: el universal es ~5-10% más
  lento en CPUs modernas que el runtime AVX2. Si lo notas, usa la release de
  respaldo `v1.1.0-clasico`.
- **Vulkan experimental**: el backend Vulkan funciona pero el render 3D es
  ~6.5x más lento que D3D12. Usa **D3D12** (por defecto).
- **Personajes nuevos (experimental)**: alguna técnica de agarre de Budokai 1
  sale como un golpe normal y algún efecto puede no ser perfecto. Necesitan la
  versión USA; desde la ISO, el launcher prepara los datos necesarios para mods.

## Créditos y apoyo

- **🇮🇹⚠ 𝐒𝐏𝐈𝐃𝐄𝐑 ⚠🇮🇹** — decompilación de *Dragon Ball Z: Infinite World* que hemos usado para portar la
  campaña de Infinite World / *decompilation of DBZ: Infinite World used to port the Infinite World campaign*.
- **@ArielPVB**, la comunidad de modding de Budokai, ReXGlue SDK y DBZ Burst Limit Recompiled (lista completa en el
  README del repositorio).
- Ko-fi (opcional / optional): https://ko-fi.com/novapowers

## Legal

Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No
afiliado ni avalado por Bandai Namco, Shueisha, Toei Animation ni ningún
titular de los derechos de Dragon Ball. No se distribuye ningún `.xex`, AFS ni
dato del juego. Los archivos del juego son de tu copia legal.

Repositorio: https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection
