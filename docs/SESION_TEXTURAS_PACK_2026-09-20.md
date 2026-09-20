# Texturas tipo PCSX2 — Fase 1: volcado dev (2026-09-20)

> Objetivo: permitir "packs de texturas" al estilo de PCSX2. Esta sesión
> implementa **la Fase 1 (volcado/censo de texturas en modo dev)**, que además
> valida la identidad por hash antes de construir el cargador del pack.
> El cargador (reemplazo) es la Fase 2 y aún no está hecho.

**Publicación**: el 2026-09-20 21:54 se **reemplazó el asset Windows de la
release v1.2.6** (`DBZ-Budokai-3-HD-Collection-v1.2.6.zip`, 22.090.591 B, digest
`10df4bce…`) para incluirlo, junto con el helper `mod center hd/
texture_dump_import.py`. Sigue **desactivado por defecto** (tab Dev). El tarball
Linux de v1.2.6 no cambia. `verify_release.ps1 -Version v1.2.6` = OK.

---

## 1. Idea y por qué NO se toca el bin

- El override del bin (`#AZT` más grande) **ya se descartó**: el guest tiene su
  propio presupuesto de memoria y al crecer el `#AZT` corrompe geometría/skinning
  (`docs/07_ports/TEXTURAS_HD_RUNTIME_UPSCALE.md:24-36`). El pack **no debe tocar
  ficheros**.
- El punto de intercepción es el mismo que usa la mejora de texturas HD:
  `D3D12TextureCache::LoadTextureDataFromResidentMemoryImpl`
  (`rexglue-sdk-0.10/src/graphics/d3d12/texture_cache.cpp`).
- La **única** implementación de decodificación (DXT→RGBA) son los *load
  shaders* GPU (bytecode, sin fuente en el repo), y no hay readback cómodo. Por
  eso el volcado **no decodifica**: escribe el bitmap original comprimido tal
  cual, que es exactamente el mismo dato que el `#AZT` del juego, y la
  conversión a PNG la hace Python (Pillow) offline.

## 2. Volcado en runtime (SDK)

Fichero: `rexglue-sdk-0.10/src/graphics/d3d12/texture_cache.cpp`.

- Cvars nuevas (categoría `GPU`):
  - `dbz3_texture_dump` (string, ruta de carpeta; vacío = desactivado).
  - `dbz3_texture_dump_max` (int, nº máximo de texturas únicas; 0 = sin límite).
- `D3D12TextureCache::DumpTextureToDds()`:
  - Solo actúa con `dbz3_texture_dump` no vacío.
  - **No se ejecuta** si la mejora de texturas HD está activa
    (`dbz3_texture_upscale > 1`): no deben solaparse.
  - Formatos: `k_DXT1`, `k_DXT2_3`, `k_DXT4_5` (y sus variantes `AS_16_16_16_16`).
    Otros formatos se omiten en silencio (se añadirán después).
  - Solo 2D (`k2DOrStacked`), una sola rebanada, sin `scaled_resolve`.
  - **Excluye el frontbuffer** (la textura que produce el presentador).
  - Lee los bytes guest con `shared_memory().memory().TranslatePhysical()`
    (sin readback de GPU). Si la textura es *tiled*, la lineariza con
    `texture_conversion::Untile`; aplica el *endian swap*
    (`texture_conversion::CopySwapBlock`) en ambos casos.
  - Deduplica por **XXH3 del bitmap lineal** (`rex::XXHasher`/`XXH3_64bits`).
  - Escribe `<carpeta>/<hash>_<W>x<H>_<FOURCC>.dds` (DDS estándar, cabecera de
    128 B con FourCC legacy) y añade una línea a `<carpeta>/index.jsonl`.
- La llamada está dentro del bucle de carga, justo tras calcular
  `guest_address`, solo para el nivel base (`is_base && level_first == 0`).

## 3. UI del launcher (tab Dev) y la carpeta

- **🔴 Descubrimiento clave**: el plugin GPU (`rexgpu-xenos.dll`) tiene su
  **propio registro de cvars**, separado del ejecutable; `SetFlagByName` desde el
  exe **no** llega al plugin. El plugin sí lee `dbz3_user.toml` al arrancar, así
  que **el TOML es el puente**. Por eso el launcher define una cvar con el
  **mismo nombre** que la del plugin (`dbz3_texture_dump`, en
  `src/launcher/settings.cpp`): se persiste al TOML y el plugin la lee.
- `src/launcher/settings.{h,cpp}`: `TextureDumpEnabled()`, `TextureDumpDir()`,
  `SetTextureDumpDir()`, `SetTextureDumpEnabled()` y `DefaultTextureDumpDir()`.
- **La carpeta NO vive en el disco de instalación**: puede ocupar cientos de MB,
  así que el usuario la elige (`dbz3_texture_dump`) y se recuerda. Por defecto se
  sugiere `D:\Proyectos IA\DBZ B3 DDS`.
- `src/launcher/launcher_state.cpp` (tab **Dev**): casilla
  "Volcado de texturas para mods (dev)" + campo de carpeta + botón
  "Elegir carpeta...". Requiere reiniciar (el cvar es `kRequiresRestart`).
- i18n: entrada nueva en `src/launcher/i18n.cpp`.

## 4. Importar el volcado y organizarlo (offline)

Herramienta: `awo_tools/texture_dump_import.py`.

```powershell
python awo_tools\texture_dump_import.py <dump_dir> <out_dir> --afs us\data_cmn.afs
# opciones: --catalog "mod center hd\catalog_b3.cat" --bins 70-95 --limit N
#           --max-bins N  --no-match
```

- Convierte cada DDS a PNG (Pillow).
- Si se le pasa `--afs`, **identifica el personaje y el índice de textura**
  escaneando los bins del catálogo y casando el bitmap `#AZT` byte a byte
  (md5). Coloca el PNG en `<out>/<personaje>/<texNN>_<WxH>.png`; los que no
  casan van a `<out>/_unknown/`.
- Sin `--afs` (o con `--no-match`) agrupa por `<WxH>`.
- Escribe `manifest.json` con el mapeo.

Esto es lo que da las **carpetas por personaje/material**; el nombre de material
fino (qué malla usa cada textura) se puede derivar más adelante cruzando el
índice de textura con el mesh-part del `#AWO`.

## 5. Cambio colateral en el SDK: `frame_cap` duplicado

Al reconstruir el SDK saltó un símbolo duplicado `FLAGS_frame_cap_storage_`: el
cvar `frame_cap` estaba definido **en el presenter D3D12 y en el de Vulkan**, y
en Windows se compilan ambos backends. Se movió la **definición** a
`src/ui/presenter.cpp` (fichero común a todos los backends) y los dos presenters
pasan a **declararlo** (`REXCVAR_DECLARE(int32_t, frame_cap)`). Se expone
`SharedMemory::memory()` como `public` para leer la memoria guest desde la cache
de texturas.

## 6. Límites conocidos (Fase 1)

- Solo DXT1/3/5; el resto de formatos (RGBA8 nativas, DXN, empaquetados…) aún no.
- Solo nivel 0 (sin cadena de mips).
- Texturas *render-target* (dinámicas) pueden volcarse con contenido no
  finalizado; el dedupe por hash lo hace ruidoso pero inocuo.
- El casado con el `#AZT` depende de que los bytes guest coincidan con el
  fichero (linearizado + endian); si un personaje no casa, saldrá en `_unknown`
  (es la señal de que hay tiling/endian distinto que revisar).
- Dev-only y OFF por defecto.

## 7. Siguientes fases

- **Fase 2 (cargador del pack)**: en el mismo seam, si el hash de la textura
  guest coincide con una entrada del pack, subir el DDS del pack en lugar de los
  datos guest. Empezar por mismo tamaño/formato (trivial) y luego HD (recurso Nx).
- **Fase 3 (launcher + conflicto)**: activar/desactivar pack y **exclusión mutua
  con aviso fuerte** si hay a la vez un pack de texturas HD y la mejora de
  texturas HD en runtime (ya se evita el solape del volcado; falta el del pack).

## 7.bis VALIDACIÓN (2026-09-20)

Con `dbz3_texture_dump = "D:\\...\\run4"` en el TOML y `skip_launcher`:

- **138 DDS escritos** (todos DXT3, `fmt=19`), **3,4 MB**, en segundos tras
  cargar la intro/título. `index.jsonl` con 137 líneas (hash, dims, formato,
  `tiled`, mips, dirección guest). Cabecera DDS válida.
- El importador convirtió 137 DDS → PNG sin error.
- Con `--afs us\data_cmn.afs --bins 70-110 --max-bins 25` (19 bins
  escaneados): **7 texturas identificadas** contra el `#AZT` y colocadas en su
  carpeta de personaje. Confirma la cadena completa: volcado (DDS) → casado por
  hash del bitmap → PNG organizado por personaje.
- **C: intacto**: todo el volcado va a la carpeta elegida (D:).

## 8. Ficheros tocados

- SDK: `src/graphics/d3d12/texture_cache.cpp`,
  `include/rex/graphics/d3d12/texture_cache.h`,
  `include/rex/graphics/shared_memory.h`,
  `src/ui/presenter.cpp`, `src/ui/d3d12/d3d12_presenter.cpp`,
  `src/ui/vulkan/vulkan_presenter.cpp`.
- Launcher: `src/launcher/settings.{h,cpp}`, `src/launcher/launcher_state.cpp`,
  `src/launcher/i18n.cpp`.
- Herramienta: `awo_tools/texture_dump_import.py`.
- DLLs regeneradas: `rexruntime.dll` 10.910.720 B, `rexgpu-xenos.dll`
  6.246.400 B (copian junto al exe tras compilar).
