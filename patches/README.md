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

## Como aplicar (ReXGlue 0.10.0)

Copiar los 16 archivos sobre el SDK (rutas relativas a la raiz del SDK):

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
patches/rexglue-sdk/src/ui/d3d12/d3d12_presenter.cpp -> rexglue-sdk/src/ui/d3d12/d3d12_presenter.cpp
patches/rexglue-sdk/src/system/dbz1_audio_jp_flag.cpp  ->  rexglue-sdk/src/system/dbz1_audio_jp_flag.cpp
patches/rexglue-sdk/src/system/dbz1_diag_flags.cpp     ->  rexglue-sdk/src/system/dbz1_diag_flags.cpp
patches/rexglue-sdk/src/system/dbz1_region_flag.cpp    ->  rexglue-sdk/src/system/dbz1_region_flag.cpp
patches/rexglue-sdk/src/system/function_dispatcher.cpp ->  rexglue-sdk/src/system/function_dispatcher.cpp
patches/rexglue-sdk/src/filesystem/CMakeLists.txt      ->  rexglue-sdk/src/filesystem/CMakeLists.txt
patches/rexglue-sdk/src/system/CMakeLists.txt          ->  rexglue-sdk/src/system/CMakeLists.txt
```

Los 2 CMakeLists anaden los archivos nuevos a los targets (`afs.cpp` a
`rexfilesystem`, los 3 `dbz1_*_flag.cpp` a `REXSYSTEM_SOURCES`). Sin ellos el
link de rexruntime falla con `undefined symbol: FLAGS_dbz1_*_storage_(void)`.

Luego recompilar el runtime y copiar la DLL al build del juego:

```powershell
cmake --build rexglue-sdk/out/build-win-vulkan --target rexruntime
Copy-Item rexglue-sdk/out/win-amd64/rexruntime.dll out/build/win-amd64-release/
```

⚠️ En 0.10 el compilador del juego debe ser `C:/Program Files/LLVM/bin/clang++.exe`
(target MSVC). El toolchain retcomm (MinGW/libstdc++) NO compila el header
`rex/chrono/chrono.h` (falta `std::chrono::clock_time_conversion`).

Los scripts `mod center hd/swap_b3.py` y `mod center hd/texture_b3.py` generan
los overrides con el padding correcto (al to_read del slot, o al to_read
virtual si el bin es mayor) y el runtime los sirve con el mid-insert virtual.
