# Parches del ReXGlue SDK

Este proyecto usa el [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) como
dependencia externa (no se incluye aqui). Los archivos de esta carpeta son
**modificaciones del runtime** necesarias para que los model swaps por override
funcionen con bins que exceden el slot del AFS.

> **Version del parche: ReXGlue 0.10.0** (migrado desde 0.9.0 el 2026-08-25).
> En 0.10 el filesystem fue refactorizado: `src/filesystem/afs.cpp` y
> `include/rex/filesystem/afs.h` **no existen** en el SDK 0.10 (se eliminaron) y
> `host_path_file.cpp`/`host_path_entry.cpp` son mucho mas simples (sin logica
> AFS/override). Por eso el parche 0.10 **recrea** `afs.h`/`afs.cpp`, **porta**
> la logica a los nuevos `host_path_file.cpp`/`host_path_entry.cpp`, y ademas
> restaura los 3 cvars dbz1 que 0.10 elimino (necesarios para linkear
> `REXCVAR_DECLARE`) y modifica 2 CMakeLists para incluir los archivos nuevos.

## Que hacen estos cambios

### 1. Mid-insert virtual en la tabla AFS (`afs.cpp`, `afs.h`)

El guest (juego) lee cada entrada del `data_cmn.afs` con un buffer de tamano
`to_read = ceil(size/0x1000)*0x1000` derivado de la tabla AFS. Un bin de mod mas
grande que ese to_read (p.ej. Goten 107006 B en el slot de Krillin, to_read
106496 B) se truncaba al servirse por override -> crash.

Antes de estos cambios, el runtime solo servia un bin de override si cabia en el
to_read del slot. Para bins mayores habia que reconstruir el AFS completo
(~280 MB por mod), lo cual impedia tener 2+ mods de modelo simultaneos.

El **mid-insert virtual** presenta al guest una tabla AFS CONSISTENTE que
replica exactamente un rebuild con mid-insert:

- `AfsGetVirtualTable()`: construye y cachea una tabla virtual donde cada
  entrada con override mayor que su `to_read` **crece in-place** (slot alineado
  a 0x800) y **todas las entradas posteriores se desplazan** por el delta
  acumulado, igual que un AFS reconstruido.
- `AfsTranslateOffset()`: para las lecturas de datos, traduce el offset
  virtual -> fisico (resta el delta de la entrada) y sirve el override (bin
  completo) o lee del archivo fisico en el offset traducido.

Criterio de crecimiento: solo crece si el override excede `to_read` (lo que el
guest ya aloca), NO si excede el slot fisico. Asi los mods que caben (p.ej.
tex_91, 114688 = to_read) no desplazan nada.

Resultado: swaps nativos de modelo B3->B3 que pesan ~100 KB por mod, 2+ mods de
modelo/textura activos simultaneamente, y swaps en cualquier direccion (el bin
puede ser mayor o menor que el slot).

### 2. Override de archivo completo (`host_path_entry.cpp`, `afs.cpp`, `afs.h`)

Ademas de reemplazar entradas individuales de un AFS, los mods pueden reemplazar
**archivos enteros** (p.ej. `opening.sfd`, `adx_usa.afs`, `Ending00.sfd` del mod
de musica OG). Esto permite aplicar mods de archivo completo **sin staging ni
duplicacion de assets**: el runtime los sirve directamente desde
`mods/<mod>/<filename>` (o `mods/<mod>/us/<filename>` / `mods/<mod>/eu/<filename>`).

- `AfsFindModFileOverride()` (`afs.cpp`): busca un reemplazo completo para un
  archivo por nombre, en orden alfabetico de mods.
- `HostPathEntry::Open()` (`host_path_entry.cpp`): si hay un reemplazo completo
  para el archivo que el guest abre, abre el archivo del mod en su lugar.

Esto es lo que permite que el juego use directamente la carpeta de assets
(`game_data_root`) sin un overlay `active_region` que hardlinkee/copie los
archivos. La region (us/eu) se monta aparte (ver la seccion de la app).

### 3. Traduccion de lecturas en el dispositivo de archivos
(`host_path_file.cpp`)

`HostPathFile::ReadSync` ahora:

1. Si la peticion cae en la region cabecera+tabla del `data_cmn.afs`, sirve la
   tabla virtual (completando desde el archivo real si la peticion cruza el
   final de la tabla, porque el guest redondea a 0x8000).
2. Si la peticion cae en datos y el AFS tiene entradas crecidas, llama a
   `AfsTranslateOffset` para servir el override completo o leer del archivo
   fisico en el offset traducido.
3. Si no hay entradas crecidas, se usa el override por entrada clasico (sin
   tabla virtual) como antes.

### 4. Mods root con walk-up (`afs.cpp`, `settings.cpp`, `mod_pipeline.cpp`)

En la release, el ejecutable del juego vive en una subcarpeta
(`dbz3_avx2/` o `dbz3_legacy/` — ver bootstrap de ISA en `src/bootstrap.cpp`)
mientras los mods quedan junto a los datos del juego (`<raiz>/mods`).
`AfsModsRoot()` (runtime) y los `ModsRoot()`/`ModsOutDir()` del launcher
suben hasta 3 niveles desde el ejecutable buscando una carpeta `mods/` y usan
la primera que encuentren (en dev es la del propio exe, sin cambios).

### 5. Input: deadzone y rumble configurables (`input_system.cpp`)

El SDK 0.10 eliminó los cvars `deadzone`/`rumble` que el 0.9 tenía. Este parche
los restaura y los hace REALES (antes los controles del launcher eran placebo):
- `REXCVAR_DEFINE_DOUBLE(deadzone, 0.1, ...)` — aplicado en `InputSystem::GetState`
  sobre el estado fusionado (todos los ejes de los sticks se anulan si su
  magnitud está por debajo de `deadzone * INT16_MAX`). Cubre los 3 drivers
  (XInput, SDL y MnK) en el punto unico de salida al guest.
- `REXCVAR_DEFINE_BOOL(rumble, true, ...)` — en `InputSystem::SetState` acepta
  la vibracion sin llegar a ningun pad cuando esta desactivado.
- El launcher escribe ambos por nombre (`SetFlagByName`), el registro de cvars
  de `rexruntime.dll` es compartido con el exe (los exporta) asi que la
  propagacion llega al runtime.

### 6. Frame cap real del presentador (`d3d12_presenter.cpp`)

El SDK 0.10 **elimino el cvar `frame_cap`** del 0.9 (el pacing del juego lo hace
ahora el vblank del guest via `vsync`). La opcion "Frame cap" del launcher era
por tanto un placebo. Este parche lo restaura de verdad en el backend D3D12:
- `REXCVAR_DEFINE_INT32(frame_cap, 0, "UI/Presenter", ...)` (0 = sin limite).
- En `D3D12Presenter::PaintAndPresentImpl`, antes de pintar/presentar, se espera
  al siguiente slot de `frame_cap` FPS con `std::chrono::steady_clock` +
  `rex::thread::Sleep`. La pintura esta serializada (un unico owner), asi que un
  timestamp file-scope es seguro.
- Solo afecta a la tasa de presentacion host (30 = media carga en GPUs
  integradas); NO toca el vblank del guest ni la velocidad del juego (eso es el
  cvar `vsync`, que el launcher fuerza a 60 Hz).
- El launcher lo propaga con `SetSdkInt("frame_cap", ...)` SOLO en el modo
  juego (el launcher mantiene sus repaints sin limite).
- Tambien anade un log diagnostico del primer `Present` (`dbz3: first present
  OK (...)`) para medir desde el log la duracion de la pantalla negra del
  launcher en maquinas lentas (cuanto tarda el init de device/swapchain).

### 6.b Frame cap y modo seguro del presenter Vulkan (`vulkan_presenter.cpp`)

El presenter Vulkan del SDK 0.10 podia elegir `IMMEDIATE` o `MAILBOX` por
defecto y no compartia el cvar `frame_cap` del presenter D3D12. En Linux esto
es problematico con MangoHud y Steam, que interceptan cada
`vkQueuePresentKHR`: se podia generar un bucle de presents sin limite, causar
tirones severos con MangoHud y hacer que Steam mostrara cientos o miles de FPS
aunque la cadencia logica del guest siguiera siendo 60 Hz.

El parche:

- restaura `frame_cap` en el presenter Vulkan y aplica el mismo pacing host que
  D3D12;
- fuerza FIFO cuando hay un cap configurado;
- cambia `IMMEDIATE`, `MAILBOX` y `FIFO_RELAXED` a opt-in, dejando FIFO como
  comportamiento seguro por defecto;
- no altera el vblank ni la velocidad logica del guest.

### 7. Build de la variante legacy (SDK CMakeLists, opcional)

Para compilar el SDK sin AVX2 (`-march=x86-64-v2`) en un directorio aparte sin
pisar `out/win-amd64`, el CMakeLists raiz del SDK acepta la cache var
`REXGLUE_OUTPUT_DIR` (si se deja vacia usa el default). No es un cambio del
runtime; es una ayuda de build para generar `out/win-amd64-legacy`.

### 8. Timeout del tick de UI del presentador (`presenter.cpp`)

El hilo UI espera el vblank del monitor (via el hilo `DXGIUITickThread`) antes
de pintar cada frame, para no saturar la GPU. Si ese vblank deja de llegar
(monitor perdido/stale, `WaitForVBlank` atascado, cambio de modo de pantalla),
`WaitForUITickFromUIThread` se quedaba esperando para siempre: la ventana se
ponia negra y "No responde", y el cierre por ventana (Alt+F4) no se procesaba.

Parche: la espera usa `condition_variable::wait_for(50 ms)` en vez de
`wait()` indefinido. Si no llega tick a tiempo, la UI pinta igualmente (caida a
~20 FPS como mucho). El hilo UI nunca se bloquea, el launcher siempre aparece y
los mensajes de la ventana siempre se procesan.

### 9. Inicializacion asincrona del driver SDL (`sdl_input_driver.{h,cpp}`)

`SDL_InitSubSystem(SDL_INIT_GAMEPAD)` puede bloquearse indefinidamente cuando
hay software de captura cargado (RTSS/OBS) o la enumeracion de joysticks es
lenta. El driver SDL lo llamaba de forma sincrona desde `OnWindowAvailable`
(va `CallInUIThreadSynchronous`), con lo que el launcher se quedaba en negro +
"No responde" al abrir (reproducido: el arranque se colgaba en `AttachWindow`).

Parche: `OnWindowAvailable` solo asocia la ventana y lanza un `std::thread`
que hace toda la init SDL (events + gamepad + mappings) en segundo plano. La
init SDL es thread-safe; los mandos aparecen via eventos cuando el hilo acaba,
y `EnumerateDevices` no devuelve nada hasta entonces. Los flags de init pasan
a ser `std::atomic<bool>` (los lee el hilo de input). El hilo se deja detached
(jamás se une): si sigue bloqueado en `SDL_InitSubSystem` en el cierre, un
`join()` colgaria el shutdown (el juego hard-exit al cerrar de todos modos).

### 10. Idioma del guest = idioma del launcher (`xam_info.cpp`)

El juego (guest) elige su idioma de texto via `XGetLanguage`. Antes devolvía
inglés fijo (basado en region). Ahora devuelve `user_language`, el cvar que el
launcher ya propagaba desde `dbz3_language` en `ApplyUserSettingsToSdk`
(`REXCVAR_SET(user_language, Language())`). Asi el selector "Idioma del launcher
y del juego" controla TAMBIEN el texto del juego, no solo la UI del launcher.

`XGetLanguage_entry` lee `REXCVAR_GET(user_language)`. Ojo con el scope:
`user_language` se define con `REXCVAR_DEFINE_UINT32` en `xam_user.cpp` ANTES
de abrir los namespaces, asi que su accesor vive a nivel GLOBAL — el
`REXCVAR_DECLARE(uint32_t, user_language)` de este archivo debe ir tambien
fuera de `namespace rex::kernel::xam` (si no, el link falla con
`undefined symbol: rex::kernel::xam::FLAGS_user_language_storage_`).

### 11. Recoleccion de funciones no registradas (`function_dispatcher.cpp`)

`InvalidFunctionTrap` (lo que el runtime ejecuta cuando el guest llama a una
funcion indirecta que no esta en la tabla) ahora, si existe la variable de
entorno `DBZ3_COLLECT_UNREGISTERED`, escribe cada direccion a
`dbz3_unregistered.txt` y continua en vez de abortar con `REX_FATAL`. Sin la
variable, el comportamiento es identico (aborta). Es una ayuda de diagnostico
para la variante EU/PAL (segunda recompilacion): si el guest alcanza en combate
una funcion no registrada, se recolectan las direcciones y se declaran en
`dbz3_config_eu.toml`.

Ademas, en cada indirect call no registrado se loguea (nivel critical) el target,
el `caller_lr` del guest y los registros r3/r4/r11 — sirvio para localizar la
causa raiz del crash de la batalla DEMO EU (v1.0.11): calls virtuales a
vtable/mid-function blocks mal clasificados como jump tables (ver AGENTS 14.16).

### 12. Diagnostico del lanzamiento del juego (`rex_app.cpp`)

`ReXApp::LaunchModule` (el lambda diferido que arranca el guest en el hilo UI)
se envuelve en try/catch que registra `e.what()` y re-lanza. Convierte el
`std::terminate` intermitente del arranque (0xC0000409) en un log con el
mensaje de la excepcion. Se compila en el juego (no en rexruntime.dll) desde
`rexglue/share/rexglue/rex_app.cpp` — el parche va a la fuente del SDK.

### 13. Blindaje del pacing del guest a 60 Hz (`graphics_system.cpp`)

El worker "GPU VSync" de `GraphicsSystem` marca el vblank del guest con un
intervalo derivado del modo de video (60 Hz) cuando el cvar `vsync` esta ON,
pero con el cvar OFF lo colapsaba a ~1 ms (1000 Hz) y la logica del juego
corria ~16x mas rapida ("juego acelerado", reportado por usuarios al
desactivar el V-Sync). El launcher ya forzaba `vsync=true` al arrancar, pero
cualquier ruta que lo apagara en runtime (toml, config, cvar residual) volvia a
acelerar el juego.

El parche **clampa el intervalo** en el propio worker:

```cpp
uint64_t interval_ticks = std::max(
    REXCVAR_GET(vsync) ? vsync_interval_ticks : no_vsync_interval_ticks,
    vsync_interval_ticks);
```

asi el vblank del guest nunca puede ser mas corto que un frame de 60 Hz y
`vsync=false` se convierte en un no-op (el juego siempre corre a su velocidad).
Vive en **rexgpu-xenos.dll** (no en rexruntime.dll): tras aplicar el parche hay
que recompilar `rexgpu-xenos` en ambas variantes (v3 y v2) y copiar las DLLs.

## Como aplicar (ReXGlue 0.10.0)

> **Desde la v1.4.0** basta con copiar la carpeta entera encima de un checkout
> limpio del tag `v0.10.0` (es lo que hace el CI de Linux):
>
> ```
> git clone --branch v0.10.0 https://github.com/rexglue/rexglue-sdk.git rexglue-sdk-0.10
> cp -a patches/rexglue-sdk/. rexglue-sdk-0.10/        # PowerShell: Copy-Item -Recurse -Force
> ```
>
> **La carpeta es el overlay COMPLETO** (desde la v1.4.0): tiene mas de 100
> archivos y se copia entera. La lista de abajo es la base historica de los
> parches 1-13 (v1.0-v1.3) mas los archivos del 12/11 y **no** es la lista
> completa: si copias solo esos, faltan cabeceras, shaders y `.hlsl` y el link o
> la compilacion fallan. Para el overlay actual, copia la carpeta entera (ver el
> bloque de arriba).

Los archivos base (los 19 historicos y los anadidos el 12/11), con rutas
relativas a su raiz:

```
patches/rexglue-sdk/include/rex/filesystem/afs.h      ->  rexglue-sdk/include/rex/filesystem/afs.h
patches/rexglue-sdk/include/rex/input/sdl/sdl_input_driver.h
                                                      ->  rexglue-sdk/include/rex/input/sdl/sdl_input_driver.h
patches/rexglue-sdk/src/filesystem/afs.cpp           ->  rexglue-sdk/src/filesystem/afs.cpp
patches/rexglue-sdk/src/filesystem/devices/host_path_file.cpp
                                                      ->  rexglue-sdk/src/filesystem/devices/host_path_file.cpp
patches/rexglue-sdk/src/filesystem/devices/host_path_entry.cpp
                                                      ->  rexglue-sdk/src/filesystem/devices/host_path_entry.cpp
patches/rexglue-sdk/src/input/input_system.cpp      ->  rexglue-sdk/src/input/input_system.cpp
patches/rexglue-sdk/src/input/sdl/sdl_input_driver.cpp
                                                      ->  rexglue-sdk/src/input/sdl/sdl_input_driver.cpp
patches/rexglue-sdk/src/kernel/xam/xam_info.cpp    ->  rexglue-sdk/src/kernel/xam/xam_info.cpp
patches/rexglue-sdk/src/ui/presenter.cpp            ->  rexglue-sdk/src/ui/presenter.cpp
patches/rexglue-sdk/include/rex/ui/d3d12/d3d12_presenter.h
                                                      ->  rexglue-sdk/include/rex/ui/d3d12/d3d12_presenter.h
patches/rexglue-sdk/src/ui/d3d12/d3d12_presenter.cpp -> rexglue-sdk/src/ui/d3d12/d3d12_presenter.cpp
patches/rexglue-sdk/src/graphics/dbz3_texture_pack.h  ->  rexglue-sdk/src/graphics/dbz3_texture_pack.h
patches/rexglue-sdk/src/graphics/dbz3_texture_pack.cpp
                                                      ->  rexglue-sdk/src/graphics/dbz3_texture_pack.cpp
patches/rexglue-sdk/src/graphics/shaders/dbz3_night.hlsl
                                                      ->  rexglue-sdk/src/graphics/shaders/dbz3_night.hlsl
patches/rexglue-sdk/src/graphics/command_processor.cpp
                                                      ->  rexglue-sdk/src/graphics/command_processor.cpp
patches/rexglue-sdk/src/graphics/d3d12/command_processor.cpp
                                                      ->  rexglue-sdk/src/graphics/d3d12/command_processor.cpp
patches/rexglue-sdk/src/graphics/graphics_system.cpp  ->  rexglue-sdk/src/graphics/graphics_system.cpp
patches/rexglue-sdk/src/system/dbz1_audio_jp_flag.cpp  ->  rexglue-sdk/src/system/dbz1_audio_jp_flag.cpp
patches/rexglue-sdk/src/system/dbz1_diag_flags.cpp     ->  rexglue-sdk/src/system/dbz1_diag_flags.cpp
patches/rexglue-sdk/src/system/dbz1_region_flag.cpp    ->  rexglue-sdk/src/system/dbz1_region_flag.cpp
patches/rexglue-sdk/src/system/function_dispatcher.cpp ->  rexglue-sdk/src/system/function_dispatcher.cpp
patches/rexglue-sdk/src/ui/rex_app.cpp                 ->  rexglue-sdk/src/ui/rex_app.cpp
patches/rexglue-sdk/src/core/logging.cpp               ->  rexglue-sdk/src/core/logging.cpp
patches/rexglue-sdk/src/filesystem/CMakeLists.txt      ->  rexglue-sdk/src/filesystem/CMakeLists.txt
patches/rexglue-sdk/src/system/CMakeLists.txt          ->  rexglue-sdk/src/system/CMakeLists.txt
```

Los 2 CMakeLists anaden los archivos nuevos a los targets (`afs.cpp` a
`rexfilesystem`, los 3 `dbz1_*_flag.cpp` a `REXSYSTEM_SOURCES`). Sin ellos el
link de rexruntime falla con `undefined symbol: FLAGS_dbz1_*_storage_(void)`.

Luego recompilar el runtime y copiar las DLLs al build del juego:

```powershell
cmake --build rexglue-sdk/out/build-win-vulkan --target rexruntime
cmake --build rexglue-sdk/out/build-win-vulkan --target rexgpu-xenos
Copy-Item rexglue-sdk/out/win-amd64/rexruntime.dll out/build/win-amd64-release/
Copy-Item rexglue-sdk/out/win-amd64/rexgpu-xenos.dll out/build/win-amd64-release/
```

> El parche 13 (`graphics_system.cpp`) vive en **rexgpu-xenos.dll** (no en
> rexruntime.dll): recompilar `rexgpu-xenos` en ambas variantes (v3 y v2) y
> copiar las DLLs.

⚠️ En 0.10 el compilador del juego debe ser `C:/Program Files/LLVM/bin/clang++.exe`
(target MSVC). El toolchain retcomm (MinGW/libstdc++) NO compila el header
`rex/chrono/chrono.h` (falta `std::chrono::clock_time_conversion`).

Los scripts `mod center hd/swap_b3.py` y `mod center hd/texture_b3.py` generan
los overrides con el padding correcto (al to_read del slot, o al to_read
virtual si el bin es mayor) y el runtime los sirve con el mid-insert virtual.

## 2026-11-09/12 - Overlay: texturas, borde nativo, cielo de noche y latencia

Estos archivos ya estan en la carpeta `rexglue-sdk/` y forman parte del overlay.
No son opcionales: si falta uno, la compilacion falla o la DLL se queda sin la
funcion. Los que llevan cabecera y `.cpp` van **en pareja** (misma version).

- **`include/rex/ui/d3d12/d3d12_presenter.h`** + **`src/ui/d3d12/d3d12_presenter.cpp`**
  (rexruntime): cabecera y cuerpo del presentador D3D12. La cabecera declara los
  recursos temporales del guest (`Dbz3TemporalInputs`, profundidad, movimiento y
  reactividad), los tiempos de GPU del pintado y la cola de presentacion de
  `dbz3_baja_latencia`. El `.cpp` aplica el frame cap, los tiempos y la latencia.
- **`src/graphics/dbz3_texture_pack.{h,cpp}`** (rexgpu-xenos): indice y carga de
  los packs de texturas estilo PCSX2 (`<hash>_<W>x<H>_<FOURCC>.dds/.png`), mas
  el lector de `dbz3_ui_borde_nativo`: con la opcion encendida, los menus del
  juego conservan sus tiras laterales cuando "Bordes dorados" esta apagado. La
  cvar del exe se lee **una vez al arrancar**; por eso el ajuste pide reiniciar.
  `src/graphics/CMakeLists.txt` compila estos dos archivos.
- **`src/ui/presenter.cpp`** (rexruntime): frame cap (`frame_cap`), modo oculto
  de pruebas, `dbz3_baja_latencia` y su sonda, y el arreglo de las bandas de
  `dbz3_aspect=keep` en pantallas ultraanchas: la proporcion de las bandas sale
  de la imagen del juego (1280x720), no del modo de video.
- **`src/graphics/shaders/dbz3_night.hlsl`** + su bytecode
  **`src/graphics/shaders/bytecode/d3d12_5_1/dbz3_night_ps.h`** (rexgpu-xenos):
  pase de cielo de noche de "Clima vivo" (`dbz3_clima_noche`, lo pone
  `src/clima.cpp` del juego). Corre donde el relight, sobre la escena 3D
  compuesta y antes del HUD. Si se toca el `.hlsl`, regenerar la cabecera con
  `fxc /nologo /T ps_5_1 /E night_ps /O3 /Vn dbz3_night_ps /Fh
  bytecode/d3d12_5_1/dbz3_night_ps.h dbz3_night.hlsl`.
- **`src/graphics/d3d12/command_processor.cpp`** (rexgpu-xenos): contadores de
  la linea `perf`, lineas `tiron`, reporte DRED y el pase de cielo de noche
  (`dbz3_night.hlsl`).
- **`src/graphics/command_processor.cpp`** (rexgpu-xenos): control del hilo del
  command processor (`dbz3_cp_event_wait`), contador `g_dbz3_regmem_wait_us`,
  escala de rim light y ajustes de cielo. Va con su cabecera
  `include/rex/graphics/command_processor.h`.
- **`SLOTS_PJ-10_afs_ScanModDirs.patch`**: parche en formato diff (no es una
  copia completa) sobre `src/filesystem/afs.cpp`. Anade
  `dbz3_afs_saltar_fuentes` (apagado de serie; se enciende con
  `dbz3_afs_saltar_fuentes = true`): al escanear `mods/`, salta las carpetas
  FUENTE de `roster_build` (con `personaje.toml`/`traje.toml` pero sin `us/` ni
  `eu/`), que no sirven ningun AFS. Con muchos mods ahorra decenas de segundos
  de arranque. Se aplica despues de copiar el overlay:
  `git apply SLOTS_PJ-10_afs_ScanModDirs.patch` (o a mano; el parche son dos
  bloques pequenos). Si el `afs.cpp` copiado ya trae la cvar, no lo apliques.

## Cambios 2026-09-18 (rendimiento + texturas HD)

- **`src/graphics/d3d12/texture_cache.cpp` + `include/rex/graphics/d3d12/texture_cache.h`**
  y los shaders **`src/graphics/shaders/texture_upscale_cs.hlsl`** /
  **`bytecode/d3d12_5_1/texture_upscale_cs.h`**: capa exterior de upscale de
  texturas en runtime (recurso host Nx + pasada bicubica). Cvar
  **`dbz3_texture_upscale`** (1 = off). El launcher lo controla con
`dbz3_hd_textures` (Video -> "Texturas HD (WIP)", x2/x3/x4; tambien genera
la cadena de mips promediando bloques del nivel 0). **WIP y OFF por
defecto**: funciona, pero provoca tirones al cargar texturas nuevas. Ver
  `docs/07_ports/TEXTURAS_HD_RUNTIME_UPSCALE.md`.
- **`src/filesystem/afs.cpp`**: el log de overrides
  (`AFS OVERRIDE LOOKUP/HIT/MISS`) pasa a estar condicionado a
  `dbz1_diag_logging`; antes se escribia en CADA lectura AFS (miles de
  lineas por sesion).
- **`src/ui/d3d12/d3d12_presenter.cpp`**: cvar **`dbz3_perf_logging`**
  (default true) con una linea cada 5 s `dbz3: perf fps=... frames=...
  max_frame_ms=... cap=...`.
- DLLs recompiladas (baseline): `rexruntime.dll` 10.870.272 B,
  `rexgpu-xenos.dll` 6.180.864 B (2026-09-18).

### 2026-09-18 (cont.) - contador de rendimiento en el swap del guest

- **`src/graphics/d3d12/command_processor.cpp`** (rexgpu-xenos):
  `Dbz3LogGuestPerformance()` al inicio de `D3D12CommandProcessor::IssueSwap`
  -> cvar `dbz3_perf_logging` (leida por nombre via
  `rex::cvar::GetFlagByName`, definida en el runtime) y una linea cada 5 s
  con `fps`, `frames` y `max_frame_ms` del juego. Es la medicion valida en
  partida y funciona con la ventana fuera de pantalla (el presentador de la
  UI solo pinta el launcher).
- **`src/system/dbz1_diag_flags.cpp`**: definicion compartida de
  `dbz1_diag_logging` (rexruntime), usada para gatear los logs de overrides
  AFS y el trace de reads.
- Arnes de pruebas offscreen: **`tools/hidden_run.ps1`**.
- DLLs canonicas finales (2026-09-19): `rexgpu-xenos.dll` **6.202.368 B**,
  `rexruntime.dll` **10.870.272 B** (ambas con el marker `dbz3_perf_logging`).
  ⚠️ Compilar el juego sobrescribe `rexruntime.dll` con el stale de
  `rexglue/bin` -> recopiar del baseline tras cada build (AGENTS 7).
- **`src/audio/sdl/sdl_audio_driver.cpp`** (rexruntime, 2026-09-19): cvar
  `audio_gain` (double, 0.0-1.0) multiplicada en el callback SDL
  (`gain = GetOutputGain() * clamp(audio_gain, 0, 1)`); `audio_mute` ya existia.
  Es el volumen REAL del launcher: antes los sliders escribian `master_volume`,
  una cvar que **no existe** en el SDK, asi que no hacian nada (y el launcher
  forzaba `audio_mute=false`, por lo que tampoco se podia silenciar).
- **DLLs canonicas (2026-09-19)**: `rexruntime.dll` **10.873.856 B** (con
  `audio_gain` + `dbz3_perf_logging`), `rexgpu-xenos.dll` **6.202.368 B**,
  `amd_fidelityfx_dx12.dll` **5.413.888 B** (el de `rexglue-sdk-0.10/bin/` se
  regenera distinto al compilar: NO copiarlo).

### 2026-09-19 (cont.) - los logs de diagnostico pasan a ser opt-in

Los diagnosticos salian activados de fabrica: quien no tocaba el tab Dev
acababa con lineas de log que no habia pedido, y el log creaba un fichero nuevo
por ejecucion sin borrar los viejos.

- **`src/filesystem/afs.cpp`**: `dbz3_io_logging` pasa de `true` a **`false`**
  por defecto (sigue activable desde el tab Dev del launcher).
- **`src/ui/d3d12/d3d12_presenter.cpp`**: `dbz3_perf_logging` pasa de `true` a
  **`false`** por defecto (nueva casilla "Registro de rendimiento (cada 5 s)"
  en el tab Dev; `Dbz3IsOurWindowForeground` sigue aportando el `fg=`).
- **`src/core/logging.cpp`** (NUEVO parche): `NextSequentialLogPath` poda los
  `dbz3_NNN.log` mas antiguos al arrancar, respetando `log_max_files`
  (default 20). Antes el tope de 20 no aplicaba entre ejecuciones porque cada
  run usa un nombre secuencial nuevo (138 ficheros acumulados en pruebas;
  verificado 138 -> 20).
- **DLLs canonicas (2026-09-19, v1.2.5 definitiva)**: `rexruntime.dll`
  **10.910.208 B**, `rexgpu-xenos.dll` **6.202.368 B**,
  `amd_fidelityfx_dx12.dll` **5.413.888 B**.

### 2026-09-19 (cont.) - Texturas HD: fix de tirones + alcance RGBA8

> Detalle completo: `docs/07_ports/TEXTURAS_HD_RUNTIME_UPSCALE.md` §8-§10.

- **`src/graphics/shaders/texture_upscale_cs.hlsl`** + **`bytecode/d3d12_5_1/
  texture_upscale_cs.h`**: `XeLoadLevelTexel` muestrea una rejilla de como maximo
  `kXeMaxBlockSamples = 8` por eje (antes promediaba el bloque `2^level x
  2^level` completo = `16 * 4^level` lecturas EN SERIE; en los mips altos quedan
  pocos hilos -> frames de cientos de ms). Recompilar con
  `fxc /nologo /T cs_5_1 /E main /Vn texture_upscale_cs /O3 /Fh
  bytecode/d3d12_5_1/texture_upscale_cs.h texture_upscale_cs.hlsl`.
- **`src/graphics/d3d12/texture_cache.cpp` + `include/rex/graphics/d3d12/
  texture_cache.h`**: el upscale pasa a cubrir tambien las **RGBA8 nativas**
  (`fmt=6`), no solo las DXT:
  - `GetTextureUpscaleFactor`: acepta `dxgi_format_unsigned == R8G8B8A8_UNORM`
    cuando el load shader produce RGBA8 (`bytes_per_host_block == 4`); rechaza
    los demas formatos (`not_rgba8`/`load_not_rgba8`) para no corromper texturas.
  - **Exclusion del frontbuffer** (`swap_texture_key_`): `RequestSwapTexture`
    registra la key ANTES de crearla y el upscale la rechaza (`swap_texture`).
    Sin esto el recurso de presentacion se creaba a Nx y el swap fallaba.
  - Nuevo helper `GetTextureUpscaleRgba8Format` para `GetDXGIResourceFormat` /
    `GetDXGIUnormFormat(TextureKey)` (antes devolvian `dxgi_format_uncompressed`,
    que en `k_8_8_8_8` es `UNKNOWN` -> miles de
    `Unsupported texture formats used in the frame`).
  - Nueva cvar **`dbz3_upscale_max_texels`** (default `1 << 19` texeles = 0.5 M,
    area max. 1024x512 / 512x1024; `0` = sin limite) para acotar VRAM. El
    launcher la expone como ajuste **avanzado** en el tab Dev
    ("HD textures: spending", Bajo/Medio/Alto) -> cvar
    `dbz3_hd_texture_max_texels`.
  - **Guardia de video** (`UpscaleBudgetAllows`): la intro/SFD reescribe la
    textura de video ~60 veces/s y cada reescritura regeneraba la cadena de mips
    (`upx` llegaba a **32182**, GPU al 80 % / 133 W). Ventana deslizante: >24
    upscales en 0.5 s -> deja de conceder 3 s. La decision se **cachea por key**
    (`upscale_granted_keys_`) para que sea estable entre la creacion del recurso
    Nx y sus recargas (si no, el recurso Nx queda sin rellenar -> `device
    removed 0x887A0001`). Medido: upx 32182->237, GPU 80->39 %, 133->34 W.
  - **Tope x3** (`dbz3_texture_upscale` rango 1-3, antes 1-4): x4 multiplicaba
    VRAM/GPU casi sin ganancia visible.
  - **Minimo de tamano** (`dbz3_upscale_min_size`, default **16**): no se escalan
    texturas menores de 16 texeles de ancho/alto. Son de HUD/UI (segmentos de
    barra de vida, iconos) y el bicubico las emborronaba. Fix del HUD sucio
    (feedback del usuario).
  - **Clamp anti-ringing** en `texture_upscale_cs.hlsl`: el resultado del kernel
    Catmull-Rom se acota al `[min, max]` de las 16 muestras, sin sobre-disparo
    en contornos (glifos/letras). Bytecode regenerado con `fxc /T cs_5_1 /E main
    /Vn texture_upscale_cs /O3 /Fh ...`.
- **Medicion** (`hd_tex=3x` + limite 0.5 M, preset Calidad, RTX 4070 SUPER,
  combate real): **0 errores**, fps min 54.7, GPU **39 % / 33 W / 1.67 GB**.
- **DLLs canonicas (2026-09-19b, texturas HD: RGBA8 + guardia + min-size)**: `rexgpu-xenos.dll`
  **6.227.456 B** (baseline; SHA256 varia por build), `rexruntime.dll`
  **10.910.208 B**, `amd_fidelityfx_dx12.dll` **5.413.888 B**.

### 2026-09-29 - DRED activo en release + gamecontrollerdb

Dos cosas aprendidas de la recompilacion hermana **reblue** (ReXGlue 0.10, misma
base que este proyecto): el DRED solo estaba armado cuando `d3d12_debug=ON`, y el
`gamecontrollerdb.txt` que el runtime ya sabe leer **no se enviaba**. Los dos
ficheros de abajo ya forman parte del arbol de parches.

- **`src/ui/d3d12/d3d12_provider.cpp`** (rexruntime): DRED sale de dentro del
  `if (d3d12_debug)` y pasa a su propia cvar **`d3d12_dred`** (default **true**).
  `ID3D12DeviceRemovedExtendedDataSettings` se obtiene con
  `D3D12GetDebugInterface`, que **no** requiere la capa debug (esa es pesada y
  sigue OFF por defecto): armar DRED en release es gratis y es lo unico que,
  cuando el device se pierde, nombra la operacion que fallo (auto-breadcrumbs) y
  la asignacion en la VA del page fault.
- **`src/graphics/d3d12/command_processor.cpp`** (rexgpu-xenos):
  `LogDeviceRemovalDiagnostics` enriquece el reporte:
  - cada breadcrumb imprime ademas el nombre (SetName) de la **command queue** y
    la **command list** que iba ejecutando;
  - del page fault se vuelcan los nodos `D3D12_DRED_ALLOCATION_NODE`
    (existentes + liberados recientemente) con su tipo y nombre.
- **`gamecontrollerdb.txt`** (~608 KB, raiz + espejo en `github/`): base de mandos
  de la comunidad (SDL_GameControllerDB, zlib). El runtime ya tiene la cvar
  **`hid_mappings_file`** (default `gamecontrollerdb.txt`) y carga el fichero con
  `SDL_AddGamepadMappingsFromFile`; lo que faltaba era **enviarlo** junto al exe.
  `CMakeLists.txt` lo copia en POST_BUILD, `tools/make_release.ps1` lo mete en el
  zip y `tools/sync_github.ps1` lo versiona. Beneficio: el backend SDL reconoce
  mandos genericos que no van por XInput.
- **DLLs canonicas (2026-09-29)**: `rexruntime.dll` **10.920.448 B**,
  `rexgpu-xenos.dll` **6.360.064 B**, `amd_fidelityfx_dx12.dll` **5.413.888 B**.

## 2026-10-04 - v1.4.0: menu rapido, FSR en vivo, VFS diferido, personajes nuevos

A partir de la v1.4.0 esta carpeta es el **overlay COMPLETO** de nuestro SDK sobre
ReXGlue **v0.10.0** (`git diff v0.10.0` de la rama local `dbz3-burstlimit`): copiar
`patches/rexglue-sdk/.` encima de un checkout limpio de v0.10.0 deja el arbol
identico al que compila las DLL canonicas. Se anaden, ademas de los parches de las
secciones anteriores, `CMakeLists.txt` (raiz del SDK: `REXGLUE_OUTPUT_DIR`),
`src/core/CMakeLists.txt`, `src/ui/CMakeLists.txt`, `include/rex/rex_app.h`
(`ResolveImageInfo` del nucleo dual y `OnConfigureQuickMenu`) y el resto de
ficheros listados abajo.

### Funciones adaptadas de Burst Limit Recompiled (iExplosiveRage)

Cherry-picks de la rama `burstlimit` de
[iExplosiveRage/rexglue-sdk](https://github.com/iExplosiveRage/rexglue-sdk)
(proyecto *DBZ Burst Limit Recompiled*), que desciende del mismo `v0.10.0`
(commits originales 0f57cc6, 284e15b, 5f3abd4, be4bdb0, 9296733, 156a164,
d197cd7, 431266b, 3360458, 0f1ae03 + port minimo de 1fc298c). Licencia del SDK
(BSD-3) intacta en las cabeceras.

- **Menu rapido con mando** (`ui/overlay/quick_menu.{h,cpp}`, `rex_app.{h,cpp}`):
  F1 o el combo `quick_menu_buttons` (Back+Start por defecto; L3+R3 o solo
  teclado). La app lo rellena con `ReXApp::OnConfigureQuickMenu`. Adaptado a
  DBZ3: paleta naranja/azul del launcher, banda de cabecera, textos traducibles,
  elementos de tipo accion (`kAction`), `on_changed` (guardar en
  `dbz3_user.toml`) y `can_open` (solo en partida).
- **Bloqueo de entrada para la UI** (`input/input_system.{h,cpp}`): combo de
  apertura y *input blockers*; con un dialogo abierto el guest no recibe teclas ni
  botones.
- **Panel de FPS F3 restilizado** (`ui/overlay/debug_overlay.{h,cpp}`,
  `overlay_text.{h,cpp}`, `perf/frame_rate.{h,cpp}`): FPS del juego (swaps del
  guest por segundo) y FPS de pantalla, grafica de tiempo de frame, esquina
  configurable (`debug_overlay_position`).
- **Ajustes de video en vivo** (`ui/presenter.cpp`, `ui/d3d12/d3d12_presenter.cpp`,
  `graphics/d3d12/command_processor.{h,cpp}`, `graphics/graphics_system.cpp`):
  `present_effect`, FSR/CAS, nitidez, FXAA (`swap_post_effect`) y
  `draw_resolution_scale` se aplican sin reiniciar; el upscaler FidelityFX no se
  libera mientras un pintado lo usa.
- **FSR por debajo de la resolucion** (`present_fsr_quality_mode`): los modos
  calidad/equilibrado/rendimiento renderizan por debajo de `draw_resolution_scale`
  (FPS reales). En DBZ3 lo expone la cvar `dbz3_fsr_render` («Mas FPS con FSR»).
- **Fix de pantalla negra** con `present_effect` fsr2/fsr3.
- **Guardado TOML valido** (`core/cvar.cpp`, test en `tests/unit/core/cvar_test.cpp`).
- No portado (especifico de su juego): tope de FPS por vblank, FOV/modo foto,
  mips/precarga de packs de texturas.

### Cambios propios v1.4.0

- **Cerrojo de entrada** (`input/input_system.cpp`): `GetCapabilities`, `SetState`,
  `GetKeystroke` y `RefreshDevices` toman el mutex del `InputSystem`. El menu rapido
  lee el mando desde el hilo de la UI mientras el juego lo lee desde los suyos;
  sin el cerrojo habia corrupcion del heap (0xC0000374 en `RefreshDevices`).
- **VFS diferido** (`filesystem/devices/host_path_device.cpp`,
  `host_path_entry.{h,cpp}`, `filesystem/entry.{h,cpp}`): `HostPathDevice::Initialize`
  ya no recorre toda la carpeta del juego (30 s en frio); los directorios se listan
  bajo demanda (`EnsureChildrenListed` al enumerar, busqueda exacta al abrir). La
  cvar `vfs_eager_scan=true` recupera el modo antiguo. Sellos `dbz3 startup:` en el
  log (`system/runtime.cpp`). Launcher: de ~31 s a <1 s.
- **Entradas AFS anadidas** (`filesystem/afs.{h,cpp}`, `host_path_file.cpp`): los
  mods pueden ANADIR entradas detras de la ultima de cualquier AFS
  (`mods/<mod>/us/<afs>/<N>` con `N` >= numero de entradas: `data_cmn`, `data_usi`,
  `lang_*`... para los personajes nuevos); `AfsVirtualSize` presenta al guest el
  tamano virtual del contenedor. Capacidad publicada por la cvar `dbz3_afs_append` (si falta, el
  launcher desactiva `mods/_roster` para que el juego arranque).
- **Gritos RXADPC** (`audio/xma_context.cpp`): `Decode()` acepta paquetes
  `"RXADPC\x01"` (cabecera de 8 B + hasta 7 bloques IMA ADPCM de 260 B / 512
  muestras) y los decodifica sin FFmpeg. Es la pasarela de gritos de combate de
  los personajes nuevos (no hay codificador XMA).
- **Diagnostico opcional** (`graphics/pipeline/shader/translator.cpp`,
  `graphics/pipeline/texture/cache.cpp`, `graphics/command_processor.cpp`):
  registro del layout de vertex fetch (solo con `DBZ3_LOG_DRAWS=1` o el marcador
  `dbz3_drawlog.on`) y contadores de swap del guest para el panel de FPS.
- **Sello de version**: `include/rex/dbz3_build.h` -> `1.4.0`.
- **DLLs canonicas v1.4.0 (2026-10-04)**: `rexruntime.dll` **11.034.624 B**,
  `rexgpu-xenos.dll` **6.372.864 B**, `amd_fidelityfx_dx12.dll` **5.413.888 B**
  (sin cambios). `tools/verify_release.ps1` comprueba hash contra el SDK baseline y
  estos tamanos de referencia.

## 2026-10-05 - v1.4.1: rendimiento y diagnostico

- `src/core/threading_win.cpp` (solo Windows, no esta en el overlay de Linux):
  `timeBeginPeriod(1)` + opt-out de power throttling (EcoQoS e
  IGNORE_TIMER_RESOLUTION) la primera vez que se duerme, y `Sleep`/`AlertableSleep`
  con un waitable timer de alta resolucion por hilo (fallback a `::Sleep`).
  Motivo: issue #8 (30 FPS clavados con i9-14900K + RTX 4090).
- `src/graphics/command_processor.cpp` + `include/rex/graphics/command_processor.h`:
  contador `g_dbz3_regmem_wait_us` (tiempo dormido en WAIT_REG_MEM).
- `src/graphics/d3d12/command_processor.cpp`: esperas a fences cronometradas
  (`gpu_wait=`/`syncs=`/`cp_wait=` en la linea `perf`), lineas `tiron` para los
  frames del guest de mas de 50 ms (max 30/sesion) y aviso de FPS bajo que solo
  sugiere los ajustes activos y distingue CPU de GPU.
- `src/ui/d3d12/d3d12_provider.cpp`: `d3d12_dred` pasa a **false** por defecto;
  el juego lo arma para la sesion siguiente a un `D3D12 device removed`
  (`src/launcher/settings.cpp`, `ArmGpuCrashDiagnostics`).
- `src/graphics/d3d12/pipeline_cache.cpp` (solo D3D12): contador
  `g_dbz3_sync_shader_work` (traducciones/pipelines hechos en el hilo de la GPU
  emulada) que la linea `tiron` muestra como `shaders +N`.
- `include/rex/dbz3_build.h`: sello `1.4.1`.
- `src/filesystem/devices/host_path_file.cpp` + `host_path_entry.cpp`: la tabla virtual de
  un AFS (mods que anaden/agrandan entradas) se calcula sobre el fichero que de verdad se
  abre. Con un pack de musica que trae su propio `adx_usa.afs` (override de fichero
  completo) y el mod de personajes anadiendo voces a ese AFS, se leia el pack con los
  offsets del original y la musica salia en silencio (reproducido con un pack reordenado:
  menu a 0.000 sin el arreglo, musica con el).
- `src/audio/sdl/sdl_audio_driver.cpp`: linea `dbz3: audio pico=... rms=...` cada 5 s con
  `dbz3_perf_logging` (nivel de la mezcla del guest, antes de mute/volumen).
- `src/graphics/vulkan/command_processor.cpp`: linea `perf` (backend=vulkan) y lineas
  `tiron` tambien en Vulkan/Linux.
