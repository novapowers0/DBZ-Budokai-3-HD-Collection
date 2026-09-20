# Linux nativo

El soporte Linux se incorpora en la rama de la v1.2.6 y responde al issue
[#10](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/issues/10).
Usa Vulkan, SDL3 y el mismo núcleo dual US/EU que Windows. No requiere Wine.

## Dependencias

En Ubuntu 22.04 o derivados:

```bash
sudo apt install clang-18 libc++-18-dev libc++abi-18-dev cmake ninja-build pkg-config libvulkan-dev \
  libsdl3-dev libx11-xcb-dev libwayland-dev wayland-protocols \
  libasound2-dev libpulse-dev libpipewire-0.3-dev unzip zenity
```

`kdialog` también sirve como selector de archivos alternativo a `zenity`.

## Build local

El SDK se construye primero y se instala en `rexglue-linux/`. Después se
configura el juego contra esa instalación:

```bash
cmake --preset linux-amd64 -S rexglue-sdk-0.10 \
  -DREXGLUE_ENABLE_FIDELITYFX=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/rexglue-linux"
cmake --build rexglue-sdk-0.10/out/build/linux-amd64 --config Release --parallel
cmake --install rexglue-sdk-0.10/out/build/linux-amd64 --config Release
cmake --preset linux-amd64-release -DCMAKE_PREFIX_PATH="$PWD/rexglue-linux"
cmake --build out/build/linux-amd64-release --config Release --parallel
```

El preset aplica `-march=x86-64 -mssse3`, Vulkan y `DBZ3_DUAL_REGION=ON`.
Antes de compilar el SDK ejecuta `bash tools/patch_rexglue_linux.sh`; corrige
la ausencia de `std::chrono::clock_time_conversion` en libstdc++ de Ubuntu 22.04.
La CI usa libc++ 18 porque Ubuntu 22.04 incluye libstdc++ 12, que no expone
`std::expected` completo para este SDK C++23.

## Codegen privado

El código generado deriva del `default.xex` y no se publica. La CI lo obtiene
del repositorio privado `novapowers0/DBZ-Budokai-3-HD-Collection-generated`,
usando el secret `DBZ3_GENERATED_TOKEN`. El repositorio contiene únicamente:

- `us/`: `sources.cmake`, init/register y fuentes generadas USA.
- `eu/`: `sources.cmake`, init/register y fuentes generadas EU.

Para una build local, coloca esas dos carpetas como `generated/` y
`generated_eu/`. No se deben añadir `.xex`, `.afs`, ISOs ni datos de usuario.

## Diferencias funcionales

- Los diálogos de carpeta/archivo usan `zenity` y luego `kdialog` como fallback.
- El pipeline de Model Swap ejecuta Python mediante `posix_spawn` y captura su
  salida sin abrir una shell interactiva.
- La instalación de mods ZIP usa el comando `unzip` del sistema.
- La compresión LZX de Model Swap sigue dependiendo del binario XDK de Windows;
  el juego base y los mods ya construidos sí funcionan en Linux. Portar LZX a
  `libmspack` queda separado de la primera build jugable.

## Paquete de datos

El tarball no incluye el juego. Coloca junto a `dbz3` el `default.xex` legal y
`us/` o `eu/`, como en Windows. La carpeta `mods/` se crea junto al ejecutable.
