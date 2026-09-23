# Sesion 2026-09-23 — Volcado de texturas: formatos del HUD + packs RGBA8 (v1.2.8.1)

> Seguimiento del **issue #11** ("Error al dumpear texturas"). El fix del volcado
> entro en la **v1.2.8**; al probarlo, el reporter (`mellisxboxkp`) confirmo que
> ya volcaba y reporto **dos cosas mas**:
>
> 1. "no se guardan todas las texturas que se ven en pantalla, solo algunas, el
>    volcado parece omitir la mayoria de texturas del HUD exceptuando algunos
>    fonts";
> 2. "algunas texturas aparecen como un cuadrado negro, cosa que en PCSX2 no
>    pasa".
>
> Esta sesion arregla (1), explica (2) y amplia los packs a las texturas RGBA8.
> Publicado en **v1.2.8.1**.

---

## 1. Por que faltaban texturas (causa)

`Dbz3DdsFourCc()` (`src/graphics/dbz3_texture_pack.cpp`) solo reconocia
**DXT1 / DXT2_3 / DXT4_5**; para cualquier otro formato devolvia `0` y el volcado
hacia `return` **en silencio**. Como el HUD/UI/menus del juego usan sobre todo
formatos **sin comprimir** (`k_8_8_8_8`, `k_1_5_5_5`, `k_5_6_5`, `k_8`...), el
volcado se quedaba con los DXT y unos pocos fonts: exactamente lo que describia
el reporter.

Ademas el volcado **no avisaba** de nada, asi que parecia que el juego "no
tenia" esas texturas.

## 2. Layouts de bits (verificados, no adivinados)

Un DDS sin comprimir necesita **mascaras de bits**, y como el volcado es el
bitmap **CRUDO** (byte a byte, sin decodificar), las mascaras tienen que ser las
del **guest**. Hay una trampa: en Xenos varios formatos tienen el **rojo en los
bits bajos** (`k_5_6_5` = R en 0-4), al contrario de lo que sugiere el nombre.

Se verifico contra la fuente original (Xenia, `pixel_formats.xesli`, funciones
`XePack*UNorm` y las conversiones `XeR5G6B5ToB5G6R5`, `XeR4G4B4A4ToB4G4R4A4`,
`XeR5G5B6ToB5G6R5WithRBGASwizzle`):

| Formato | fmt | DDS | bits | R | G | B | A |
|---|---|---|---|---|---|---|---|
| `k_8_8_8_8` | 6 | RGBA8 | 32 | `0x000000FF` | `0x0000FF00` | `0x00FF0000` | `0xFF000000` |
| `k_2_10_10_10` | 7 | RGBA1010102 | 32 | `0x000003FF` | `0x000FFC00` | `0x3FF00000` | `0xC0000000` |
| `k_1_5_5_5` | 3 | RGB5A1 | 16 | `0x001F` | `0x03E0` | `0x7C00` | `0x8000` |
| `k_5_6_5` | 4 | RGB565 | 16 | `0x001F` | `0x07E0` | `0xF800` | — |
| `k_6_5_5` | 5 | RGB655 | 16 | `0x001F` | `0x03E0` | `0xFC00` | — |
| `k_4_4_4_4` | 15 | RGBA4 | 16 | `0x000F` | `0x00F0` | `0x0F00` | `0xF000` |
| `k_8` / `k_8_A` | 2 / 8 | L8 | 8 | `0xFF` | — | — | — |
| `k_8_8` | 10 | L8A8 | 16 | `0x00FF` | — | — | `0xFF00` |

Comprimidos (sin cambio): DXT1/DXT2_3/DXT4_5 (y sus variantes `_AS_16_16_16_16`)
con su FourCC.

⚠️ **Ojo**: el `texture_dump.cc` de Xenia (antiguo) escribe para `k_8_8_8_8` las
mascaras `R=0x00FF0000` (BGRA). **Es incorrecto** para esta base de codigo:
`XePackR8G8B8A8UNorm` empaqueta `R | G<<8 | B<<16 | A<<24` (byte 0 = R), que es
tambien lo que asume el `texture_load_32bpb` (passthrough) y la mejora HD de
RGBA8 nativas ya validada. **No copiar las mascaras de ahi.**

## 3. Cambios

| Fichero | Cambio |
|---|---|
| `src/graphics/dbz3_texture_pack.h` | `Dbz3DumpFormat` (sufijo + formato DDS) + `Dbz3DumpFormatFor()` + `Dbz3PackReplaceableFormat()` |
| `src/graphics/dbz3_texture_pack.cpp` | tabla de formatos volcables; `Dbz3PackReplaceableFormat` (DXT + `k_8_8_8_8`) |
| `src/graphics/d3d12/texture_cache.cpp` | `Dbz3WriteDds` escribe FourCC **o** mascaras (y arregla el header); `DumpTextureToDds` usa la tabla y el sufijo; `LinearizeGuestTexture` acepta los formatos nuevos; **tope de versiones por identidad**; aviso de formato no soportado; el pack acepta RGBA8 |
| `src/graphics/vulkan/texture_cache.cpp` | mismos gates que D3D12 (`Dbz3DumpFormatFor` / `Dbz3PackReplaceableFormat`) |

### 3.1 Bug del header DDS

`dwCaps` se escribia en **+104**, que **no** es `dwCaps` sino **`dwABitMask`**
dentro de `DDS_PIXELFORMAT` (que ocupa 76..107): se perdia el `dwCaps` **y** se
pisaba la mascara de alpha. Inofensivo en DXT (los visores ignoran `dwCaps`),
pero **critico** para los formatos sin comprimir. Ahora va en **+108** y las
mascaras quedan limpias. De paso se distingue `DDPF_RGB` / `DDPF_LUMINANCE` (para
`L8`/`L8A8`) y se marca `DDPF_ALPHAPIXELS` si hay alpha.

## 4. El flood del video (y el tope por identidad)

Al aceptar los formatos sin comprimir, la **textura de video de la intro**
(`k_8`, 480x360 / 960x720, sin mips) entra en el volcado. Como su **contenido
cambia en cada fotograma**, el dedup por hash no la paraba: **4096 ficheros /
1,4 GB en 5 minutos** (se agoto el limite de 4096 y casi todo eran fotogramas).

Fix: tope de **versiones por IDENTIDAD** (direccion guest + formato + tamano),
`kDbz3DumpMaxVersionsPerTexture = 4`. El dedup por hash se mantiene (asi una
direccion de pool reutilizada con otro contenido **si** se volca), pero el churn
de video se corta:

| | antes | con el tope |
|---|---|---|
| ficheros (5 min) | 4096 (limite) | 194 |
| tamano | 1,4 GB | 51 MB |
| fotogramas de video | 4080+ | 36 (9 identidades x4) |

Aviso en el log una sola vez:
`dbz3: volcado: textura en 0x... cambia de contenido en cada uso (video/render target): solo se volcaron 4 versiones`.

## 5. Los "cuadrados negros" (NO es un bug del volcado)

Comprobado sobre el volcado real: las texturas que se ven negras son **DXT3 cuyo
canal alpha esta TODO a cero** (`00 00 00 00 00 00 00 00 | <color valido>` en cada
bloque). El color es correcto; lo que falta es el alpha. El juego **dibuja esas
texturas ignorando su alpha** (por eso se ven bien en el juego), pero un visor
las muestra **transparentes** → cuadrado negro. En PCSX2 no pasa porque alli el
volcado es de los assets de PS2, que son otros.

No se toca el dato (el volcado es fiel). Para verlas/utilizarlas,
`texture_dump_import.py` gana **`--opaque-alpha`**: si el alpha del DDS esta todo
a cero, escribe el PNG opaco y lo marca (`alpha_all_zero`) en `manifest.json`.

## 6. Packs: formatos reemplazables

El reemplazo sube siempre **RGBA8**, asi que solo valen los formatos cuyo
**recurso host es RGBA8**:

- **DXT1/DXT3/DXT5** (se descomprimen a RGBA8) — ya funcionaba;
- **`k_8_8_8_8` (RGBA8 nativa, swizzle identidad)** — **nuevo**: antes el gate
  del pack exigia DXT, asi que las texturas del HUD que ahora se vuelcan no se
  habrian podido reemplazar. Es el caso que mas importa.

Los formatos de 8/16 bits (`k_8`, `k_8_8`, `k_5_6_5`, `k_1_5_5_5`, `k_4_4_4_4`)
se **vuelcan como referencia** pero su pack se **ignora** (su recurso host no es
RGBA8 y su swizzle es propio del formato). Habilitarlos requiere un recurso RGBA8
para el pack + swizzle identidad en el camino caliente del fetch → **siguiente
paso**, no en un parche. La regla vive en `Dbz3PackReplaceableFormat()` (comun a
los dos backends, para que Windows y Linux hagan lo mismo).

## 7. Verificacion (medida)

Arnes `%TEMP%\opencode\dump_test.ps1` (boot directo con `dbz3_skip_launcher`,
volcado a `%TEMP%\opencode\dump_test`, `dbz3_texture_dump_max=600`):

- **Volcado (240 s, intro)**: **194 DDS / 51 MB** — 96 DXT3, **26 RGBA8**, 72 L8.
  Antes (solo DXT): 51 DDS. Log: `formato k_24_8 (fmt=22) no soportado` +
  el aviso del tope de video.
- **Pillow lee los tres**: DXT3 128x512 RGBA, **RGBA8 128x1024 RGBA con colores
  reales** (p.ej. `(39,133,213)`), L8 960x720 modo L.
- **Pack end-to-end** (`mods/_packtest`, magenta DXT3 + verde RGBA8):
  ```
  dbz3: pack '_packtest' reemplaza 128x512 (fmt 19) -> 128x512 (x1)
  dbz3: pack '_packtest' subido 128x512 (10 niveles, 523776 B)
  dbz3: pack '_packtest' reemplaza 128x1024 (fmt 6) -> 128x1024 (x1)
  dbz3: pack '_packtest' subido 128x1024 (11 niveles, 1048064 B)
  ```
  ⇒ el pack de **RGBA8 (fmt 6)** se aplica (nuevo) y el de DXT3 sigue igual, sin
  errores ni `Unsupported texture formats`.
- Sin regresion: el DDS de DXT sigue byte a byte como antes (mismas mascaras
  FourCC, mismo hash).

## 8. Pendiente

1. **Packs para formatos de 8/16 bits** (el caso del HUD si resulta ser
   `k_5_6_5`/`k_1_5_5_5`): recurso RGBA8 para el pack + swizzle identidad en
   `GetHostFormatSwizzle` (camino caliente: medir antes).
2. Formatos aun no volcables: `k_DXN` (normales, BC5), `k_DXT5A` (alpha, BC4),
   `k_DXT3A`, `k_24_8` (profundidad), `k_16_16_16_16`.
3. Confirmar con el reporter de #11 que ya ve el HUD en el volcado.

## 9. Ficheros tocados

- `rexglue-sdk-0.10/src/graphics/dbz3_texture_pack.{h,cpp}`
- `rexglue-sdk-0.10/src/graphics/d3d12/texture_cache.cpp`
- `rexglue-sdk-0.10/src/graphics/vulkan/texture_cache.cpp`
- `awo_tools/texture_dump_import.py` (`--opaque-alpha`)
- `docs/02_mods/PACKS_DE_TEXTURAS.md`
- DLLs canonicas: `rexgpu-xenos.dll` **6342656 B**, `rexruntime.dll` 10910720 B.
