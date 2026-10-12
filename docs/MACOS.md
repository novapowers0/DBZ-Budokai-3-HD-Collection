# macOS nativo

Port para Mac con Apple Silicon (arm64). Usa Vulkan sobre Metal (MoltenVK, que
el SDK compila e incluye), SDL3 y el mismo núcleo US/EU que Windows y Linux. No
requiere Wine ni CrossOver.

Probado en un MacBook Pro M4 Max con macOS 27 y la ISO US: launcher, partida,
audio, DualSense por Bluetooth, pantalla completa, ventana maximizada y salida
limpia.

## Dependencias

- Xcode o las Command Line Tools (`xcode-select --install`).
- CMake 3.25 o superior y Ninja: `brew install cmake ninja`.

## Build local

El SDK se construye primero y se instala en `rexglue-mac/`, como en Linux:

```bash
git clone --branch v0.10.0 --recursive https://github.com/rexglue/rexglue-sdk.git rexglue-sdk-0.10
cp -R patches/rexglue-sdk/. rexglue-sdk-0.10/
bash tools/patch_rexglue_macos.sh rexglue-sdk-0.10
cmake --preset mac-arm64 -S rexglue-sdk-0.10 \
  -DREXGLUE_ENABLE_FIDELITYFX=OFF -DREXGLUE_ENABLE_TRACY=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/rexglue-mac"
cmake --build rexglue-sdk-0.10/out/build/mac-arm64 --config Release --parallel
cmake --install rexglue-sdk-0.10/out/build/mac-arm64 --config Release
cmake --preset mac-arm64-release
cmake --build out/build/mac-arm64-release --parallel
bash tools/macos/make_app.sh
```

`tools/patch_rexglue_macos.sh` reutiliza `tools/patch_rexglue_linux.sh` (sin el
paso de `clock_cast`, que el `chrono.h` del SDK ya define en macOS) y aplica el
arreglo del swapchain explicado abajo.

El preset `mac-arm64-release` usa el núcleo dual: necesita `generated/` y
`generated_eu/`, como la CI de Linux. Con un solo codegen (por ejemplo el que
genera `rexglue-mac/bin/rexglue codegen dbz3_manifest.toml` a partir de tu
`yae3_xenon.xex`), añade `-DDBZ3_DUAL_REGION=OFF -DDBZ3_GENERATED_DIR=generated`.

El resultado es `out/DBZ Budokai 3.app`.

## El .app

- Es autocontenido: se puede arrastrar a Aplicaciones. Lleva dentro el
  ejecutable, `librexruntime.dylib`, `librexgpu-xenos.dylib`, MoltenVK
  (`Contents/Resources/vulkan`), `gamecontrollerdb.txt` y el icono.
- Dentro de un bundle no se escribe en el `.app`. Ajustes, `user_data`
  (partidas), `mods/`, logs y los datos del juego (la `.iso`, o `default.xex` +
  `us/` o `eu/`) van a `~/Library/Application Support/DBZ Budokai 3`
  (`src/portable_root.h`). En Windows y Linux nada cambia: todo sigue junto al
  ejecutable.
- Si no encuentra el juego, en vez de cerrarse sin avisar muestra un diálogo:
  **Elegir .iso...** (la enlaza en esa carpeta y el juego se reabre),
  **Abrir carpeta** o **Salir**.
- La firma es ad-hoc (sin notarizar): al descargarlo de Internet, la primera vez
  hay que abrirlo con clic derecho > Abrir.

## Diferencias funcionales

- Los diálogos de carpeta/archivo usan los nativos de macOS (`osascript`).
  "Abrir carpeta" abre el Finder y los enlaces abren el navegador.
- Igual que en Linux, no existen las funciones de la capa D3D12 (DLSS, FSR 3, la
  mejora de texturas HD y el volcado de texturas) ni la compresión LZX del Model
  Swap (binario del XDK de Windows). Los packs de texturas y los mods ya
  construidos sí funcionan.
- El launcher no muestra los iconos: la fuente de iconos (Segoe Fluent Icons) es
  de Windows. El texto usa Arial, Arial Black, Arial Rounded y Comic Sans, que
  vienen con macOS.

## Arreglos propios de macOS

- **Mandos en el hilo de la UI** (`patches/.../sdl_input_driver.cpp`): IOKit y
  GameController atan los mandos al run loop del hilo que inicia
  `SDL_INIT_GAMEPAD`. El hilo propio de la init termina y deja ese run loop
  colgando: al conectarse un DualSense el proceso caía (`EXC_BREAKPOINT` en
  `CFRunLoopAddSource`). En macOS la init va en el hilo de la UI.
- **Swapchain recreado desde cero** (`tools/patch_rexglue_macos.sh`): con
  MoltenVK, el swapchain creado con `oldSwapchain` entregaba a Metal drawables
  de 1x1 (validación de Metal: "renderTargetWidth must be <= minimum attachment
  width (1)"). Tras cualquier cambio de tamaño (pantalla completa, maximizar) se
  perdían todos los frames: pantalla negra, o negra y naranja con imágenes
  viejas del launcher.
- **Log del juego sin ruido**: MoltenVK avisa en cada frame de que Metal no
  puede desactivar el primitive restart. En macOS se apaga
  `vulkan_log_debug_messages`; los avisos de MoltenVK siguen saliendo por stderr.
- **Logs junto a la config** (`patches/.../rex_app.cpp`): el SDK los dejaba
  junto al ejecutable; ahora van junto a su `.toml`, que en el `.app` está en
  Application Support. En Windows y Linux es la misma carpeta que antes.

## CI

`.github/workflows/macos.yml` repite la CI de Linux en `macos-26` (arm64) y
sube `DBZ-Budokai-3-HD-Collection-v<versión>-macos-arm64.zip`. De momento solo
se lanza a mano (`workflow_dispatch`).
