# Texturas HD en runtime (capa exterior) — APARCADO

> Estado: **DESACTIVADO / APARCADO el 2026-09-18**, a petición del usuario, para
> centrar el esfuerzo en rendimiento. El código está implementado y probado en
> su parte estructural; basta activar el cvar para retomarlo.
>
> Cvar: **`dbz3_texture_upscale`** (`1` = off por defecto, `2`-`4` = factor;
> requiere reinicio). Valor actual en el build de desarrollo: `1`.

---

## 1. Objetivo

Duplicar/triplicar/cuadruplicar la resolución de las TEXTURAS (no la resolución
de render, que ya la da `draw_resolution_scale`), de forma **independiente a la
resolución de ventana**, con el **mínimo coste de rendimiento posible**.

## 2. Las dos vías posibles (y por qué una no vale)

| Vía | Descripción | Resultado |
|---|---|---|
| **A. Override del bin** | Reescalar las texturas dentro del `#AZT` del propio bin y servirlo por el override de AFS (mid-insert virtual). | ❌ **FALLA**: el juego tiene su presupuesto de memoria para el modelo/texturas; al crecer el `#AZT` el guest se corrompe. |
| **B. Capa exterior en runtime** | No tocar el juego: crear la textura host a N× y rellenarla con una pasada de upscale en GPU al cargarla. | ✅ Implementada (D3D12). Es la vía correcta. |

### 2.1 Evidencia de por qué la Vía A no sirve (2026-09-18)

Herramienta usada: `mod center hd/texture_upscale_b3.py` (upscale IA + rebuild
del `#AZT`/`#AMB`; quedó como generador de referencia).

| Test | Qué es | Resultado |
|---|---|---|
| `texai4x_327` | Krillin (entrada 327) con texturas IA x4 (AZT 391.680 B → 6.519.328 B) | **Crash** al entrar en Practicar (antes del select). |
| `_texai2x327` | El mismo con x2 (AZT → 1.559.040 B) | **Modelo deformado** (torso cizallado, piernas abiertas): desborde de memoria del guest que pisa geometría/skinning. |
| `_pad327` | **Control**: bin ORIGINAL byte a byte, pero con el fichero del override rellenado a 200 KB (mismo crecimiento de tabla AFS) | **Perfecto**. ⇒ El *mid-insert virtual* no es el problema; el problema es el **tamaño de las texturas** dentro del presupuesto del guest. |

⇒ Cualquier pack que crezca está jugando dentro de la memoria del propio juego:
**el override es la herramienta equivocada para escalar texturas**.

## 3. Qué hay implementado (Vía B, D3D12)

Archivos (también copiados a `github/patches/rexglue-sdk/`):

- `include/rex/graphics/d3d12/texture_cache.h`
  - `GetTextureUpscaleFactor(key)` / `IsTextureUpscaled(key)`.
  - `GetDXGIResourceFormat/GetDXGIUnormFormat(TextureKey)` devuelven el formato
    **descomprimido** cuando hay upscale (coherencia recurso↔SRV↔copy format).
  - Miembros `upscale_root_signature_`, `upscale_pipeline_` y declaración de
    `InitializeTextureUpscale()` / `UpscaleTextureData()`.
- `src/graphics/d3d12/texture_cache.cpp`
  - Cvar `dbz3_texture_upscale` (1..4, `kRequiresRestart`).
  - **Elegibilidad** (conservadora): 2D, `mip_max_level == 0`, sin array, no
    `scaled_resolve`, sin vista signed separada, con variante descomprimida y
    dimensiones ≤ `D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION / factor`.
  - `CreateTexture`: recurso a **N×**, formato RGBA8, `ALLOW_UNORDERED_ACCESS`.
  - `GetLoadShaderIndex`: usa el load shader de **descompresión**.
  - Carga: se llena el scratch buffer a 1× como siempre y, en vez de
    `CopyTextureRegion`, se llama a `UpscaleTextureData()`: compute **bicúbico
    Catmull-Rom** (16 taps) desde el buffer (SRV `ByteAddressBuffer`) al UAV de
    la textura N×. Una sola vez por carga de textura (no por frame).
  - `Initialize()` llama a `InitializeTextureUpscale()` solo si el cvar > 1; si
    el pipeline falla, el factor se anula solo (no rompe nada).
- `src/graphics/shaders/texture_upscale_cs.hlsl` + bytecode
  `bytecode/d3d12_5_1/texture_upscale_cs.h` (compilado con `fxc /T cs_5_1`).
  ⚠️ `dxcompiler.dll` NO se despliega junto al exe, por eso se compila offline y
  se embebe (no usar compilación en runtime con DXC).

### 3.1 Cómo retomar (pasos)

1. `dbz3_texture_upscale = 2` en `dbz3_user.toml` (o exponerlo en el launcher).
2. Arrancar y comprobar en el log:
   `D3D12TextureCache: DBZ3 texture upscale pipeline ready (x2)`.
3. Comparar 1 / 2 / 3 / 4 usando el log de rendimiento (ver
   `ANALISIS_RENDIMIENTO_LOGS_2026-09-18.md`): FPS y `max_frame_ms`.
4. Pendiente / siguientes pasos:
   - Exclusión fina de texturas problemáticas (UI/atlas/render targets) si
     aparecen artefactos al samplearlas con offsets en texels.
   - **Vulkan**: no implementado (mismo diseño, otro backend).
   - Mips: hoy solo mip 0; añadir cadena de mips reduciría el shimmering.
   - Evaluar la variante **assets IA**: el punto de enganche
     (`UpscaleTextureData`) permite sustituir la pasada de GPU por la carga de
     un DDS generado con IA (mismo beneficio que un pack de emulador, sin tocar
     el juego). Es la vía con más calidad; la de GPU es la de "cero disco".

### 3.2 Riesgos / límites conocidos

- **VRAM**: el destino es RGBA8 (un UAV no puede ser BC), así que N× implica
  hasta ~16× la VRAM original en x4. Aceptable por personaje, vigilando el LRU
  (`texture_cache_memory_limit_soft/hard`).
- Solo se escalan **texturas DXT/BC con mip único** (personajes/stages); UI y
  fuentes **no se tocan** (bien: evita romper pixel-exact).
- La calidad es **nitidez de magnificación, no detalle nuevo** (no es IA).

---

## 4. Referencias

- Herramienta offline del intento A: `mod center hd/texture_upscale_b3.py`
  (`--list`, `--scale`, `--skip-ai`, `--bin-out`).
- Mods de evidencia (desactivados): `_pad327`, `_texr1x327`, `_texai2x327`,
  `texai4x_327` en `out/build/win-amd64-release/mods/`.
- Medición del volumen real de texturas: 8.056 texturas DXT3 / 182,3 MB de
  payload en `data_cmn.afs`; personajes (bins 70-505) 3.558 tex / 95,9 MB
  (⇒ x4 = 1,53 GB de bitmap). Detalle en el historial de sesión.

## 5. Estado de verificacion (2026-09-18) - AUN NO HA ESCALADO NADA

Tras el contador `upx=` (nº de texturas escaladas, en la linea `dbz3: perf`) y el
diagnostico `upscale skip [...]` / `upscale ACCEPT`:

- En **todas** las sesiones grabadas (incluidas 6 con el pipeline x2/x3
  inicializado): **0 lineas `upscale ACCEPT` y 0 apariciones de `upx=`** -> el
  upscale **nunca ha llegado a aplicarse a una sola textura**.
- Lo unico consultado hasta ahora eran texturas de **video/frontbuffer
  1280x720** (`no_uncompressed` / `scaled_resolve`); en el titulo no se cargan
  texturas DXT de personaje. Hacen falta pantallas de combate/select.
- Prueba visual del usuario: **no aplica** — su sesion (`dbz3_090`, 23:11) corrio
  con el pipeline **NO inicializado** (x1), asi que no habia nada que comparar.
- **Trampa**: al salir, el launcher reescribe `dbz3_user.toml` y **borra los cvars
  que solo existen en el SDK** (`dbz3_texture_upscale`, `native_2x_msaa`,
  `dbz3_perf_logging`). Para probarlo hay que (a) exponerlo en el launcher (que lo
  persista y permita alternarlo) o (b) reaplicar el toml justo antes de lanzar.

**Consecuencia**: el feature esta implementado pero **sin validar end-to-end**.
Para que tenga valor visible hay dos caminos: (1) terminar la validacion en
combate (y afinar elegibilidad si las texturas de personaje tampoco entran), o
(2) la variante **assets IA** (mismo punto de enganche, detalle real, no solo
nitidez de magnificacion).

## 6. FUNCIONA (2026-09-18, noche) - activado desde el launcher

**Ojo**: el feature SI funciona. Todas las conclusiones de la seccion 5 eran falsas: el
build del juego **sobrescribio `rexruntime.dll` con la version stale** de
`rexglue/bin` (AGENTS seccion 7) -> el runtime perdio `dbz3_perf_logging` (las
lineas `perf fps` desaparecieron y parecia un cuelgue) y recupero el log AFS
incondicional. El juego nunca se colgo. SIEMPRE verificar tras compilar el juego:
`Select-String rexruntime.dll -Pattern dbz3_perf_logging` debe dar PRESENTE
(tamano 10.870.272 B en baseline).

Que se implemento:
- **Control en el launcher**, primer tab (Video, junto a la escala interna):
  `Texturas HD` con Off / x2 / x3 / x4 (cvar `dbz3_hd_textures`, persiste en
  `dbz3_user.toml`). Se reenvia al cvar del SDK `dbz3_texture_upscale` al arrancar
  (requiere reinicio).
- **Cadena de mips generada**: el recurso host se crea a Nx con la misma cadena
  que el guest; el nivel 0 se escala con bicubico (Catmull-Rom) desde el buffer
  1x y los niveles siguientes se generan promediando bloques 2^level del nivel 0
  en el mismo shader (`texture_upscale_cs`, parametro `level`). No se leen los
  mips empaquetados del guest (layout complicado): se regeneran.
- **Elegibilidad**: 2D, sin array, no scaled-resolve, no signed-separate, con
  variante descompromprimida y dimensiones <= 16384/factor. Se permite cualquier
  numero de mips.
- **Diagnostico**: lineas `dbz3: upscale ACCEPT fmt=.. dim=.. WxH factor=..`
  (combinaciones unicas) y `dbz3: upscale skip [motivo] ...`; y en la linea de
  rendimiento, `upx=<n>` = texturas escaladas acumuladas.

Medido (RTX 4070 SUPER, opening, x3): 6 texturas escaladas (128x512, 128x128,
256x256, 1024x512, 1024x256, 256x128), **60,0 FPS estables** con un unico hitch de
carga al principio (`max_frame_ms` 724 ms en la primera ventana; el coste es la
generacion de mips de las texturas grandes). Pendiente de medir en combate.

Nota de rendimiento: el hitch inicial se puede reducir generando solo los
primeros niveles de mip (o limitando el bloque del promedio).

## 7. Veredicto del usuario y estado final (2026-09-19)

Prueba en partida con x3: **se nota cierta mejora en la intro**, pero provoca
**tirones continuos** ("afecto a todo el ecosistema"); el usuario pide dejarlo
como **WIP y OFF por defecto**. Hecho:
- Cvar del launcher `dbz3_hd_textures` con **default 1 (Off)** y etiqueta
  "Texturas HD (WIP)" + aviso ambar; el valor se persiste y se reenvia al cvar
  del SDK `dbz3_texture_upscale` al arrancar (requiere reinicio).
- El feature se queda implementado y documentado (secciones 3 y 6).

Causa probable de los tirones (siguientes pasos para retomarlo):
1. **Coste de la generacion de mips**: los niveles > 0 se generan promediando
   bloques 2^level del nivel 0 en el shader; para texturas grandes de nivel bajo
   eso es un bucle muy largo (el opening mostro un hitch de ~724 ms). Mitigar:
   generar solo los primeros niveles (o limitar el tamano de bloque) y dejar los
   niveles altos al guest, o generar la cadena en un paso aparte.
2. **VRAM**: el destino es RGBA8 Nx (x3 = 9x texeles, ~16x el bitmap del guest en
   x4) -> presion sobre el LRU de la cache -> mas recargas/evicciones y tirones
   al entrar en zonas nuevas. Vigilar `texture_cache_memory_limit_soft/hard`.
3. Posible solucion de fondo: assets IA precalculados en el mismo enganche
   (menos coste por carga, mas calidad), o escalar solo atlas concretos.
