# Texturas HD en runtime (capa exterior)

> Estado: **FUNCIONAL, opt-in (default OFF)**, tope **x3**. Cvar
> **`dbz3_texture_upscale`** (`1` = off, `2`/`3` = factor; requiere reinicio).
> En el launcher: "Mejora de texturas (experimental)" con dos niveles
> (**Nitidas** = x2 / **Muy nitidas** = x3); el ajuste avanzado de VRAM vive en
> el tab Dev. Ver §11 para el rediseno de UX y §12 para la guardia de video.

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

## 8. ARREGLO DE LOS TIRONES — limite de muestras del bloque de mips (2026-09-19)

**Diagnostico (causa raiz confirmada).** `texture_upscale_cs.hlsl` generaba cada
nivel de mip promediando el bloque COMPLETO `2^level x 2^level` del nivel 0 en
cada uno de los 16 taps Catmull-Rom: `16 * 4^level` lecturas EN SERIE por texel
de salida. En los mips altos el dispatch queda con muy pocos hilos (p.ej. nivel 9
de una textura 1024x512 -> ~18 hilos, cada uno con 16 x 512x512 = 4,2M de
iteraciones encadenadas tras un acumulador dependiente) -> la latencia de memoria
se paga entera y el frame sube a cientos de ms. Es la causa de los "tirones al
cargar texturas nuevas" que se venian reportando (y de que el tester con una
RTX 5090 viera caidas a 30 fps: el coste no es GPU-bound, una GPU mas rapida no
ayuda).

**Arreglo.** `XeLoadLevelTexel` muestrea una rejilla de como maximo
`kXeMaxBlockSamples = 8` por eje (paso uniforme sobre el bloque). Para niveles
bajos (bloque <= 8) es EXACTO; para los altos es una aproximacion (los mips altos
son minificacion borrosa, imperceptible). Reduce el trabajo por hilo de millones
de iteraciones a <= 64. Basta recompilar el shader y recompilar `rexgpu-xenos`:

```
fxc /nologo /T cs_5_1 /E main /Vn texture_upscale_cs /O3 ^
    /Fh bytecode\d3d12_5_1\texture_upscale_cs.h texture_upscale_cs.hlsl
```

**Medicion (RTX 4070 SUPER, `hd_tex=4x` + `3x` + MSAA + cap 60, config identica
al tester):**

| | ventanas | fps min | <58 fps | frames >100 ms | peor frame |
|---|---|---|---|---|---|
| Antes (`dbz3_144`) | 32 | 41.7 | **10** | **14** | 905 ms |
| Despues (`dbz3_146`) | 73 | 52.6 | **1** | **1** | 634 ms\* |

\* el unico hitch restante NO era del upscale: `io SLOW 205127us` en
`adx_usa.afs` (lectura de disco de 205 ms, problema de E/S ajeno al feature).

Con el arreglo la sesion escalo **1614 texturas** (vs 274 antes) sin hitches de
upscale. **Pendiente: validacion visual del usuario** (los mips altos son
aproximados).

**Nota FSR vs escala interna (importante para el launcher).** Con
`present_effect=fsr`, el presentador solo usa FSR EASU si el frontbuffer del
guest es MENOR que la salida (`src/ui/presenter.cpp:1054-1131`); si la escala
interna hace el frontbuffer >= la salida, cae a **CAS**
(supersampling/downscale) y los ajustes de FSR quedan inertes. Es decir, "escala
interna 3x" mejora el render (supersampling) pero no cambia la resolucion
mostrada; para que FSR haga upscale de verdad hay que dejar la escala interna por
debajo de la salida.

## 9. ALCANCE REAL — SOLO SE ESCALABAN LAS DXT (y por que no se notaba)

**Sintoma**: el usuario (SSGPrinceVegeta) activo Texturas HD a 4x y "no noto
mejora de texturas", aunque los tirones se fueron con §8. Los logs lo explican.

**Diagnostico con el catalogo instrumentado** (`dbz3_147`, hd_tex=4x):

```
upscale ACCEPT fmt=19 ...      <- fmt=19 = k_DXT2_3 (DXT3)  -> si se escalaba
upscale skip [no_uncompressed] fmt=6 ...   <- fmt=6 = k_8_8_8_8 (RGBA8 nativo)
upscale skip [no_uncompressed] fmt=6 ... 1024x1024 / 1024x512 / 256x2048 ...
```

`GetTextureUpscaleFactor` exigia `host_format.dxgi_format_uncompressed`, que
**solo esta relleno en las DXT** (para descomprimir). Las texturas **RGBA8
nativas (`fmt=6`)** -las MAS grandes del juego: caras, ropa, escenarios- tienen
ese campo a `DXGI_FORMAT_UNKNOWN` y se descartaban TODAS. Resultado: se escalaban
~650 texturas (DXT3, casi siempre pequenas de UI/efectos) y el efecto apenas se
notaba en personajes/escenarios.

## 10. EXTENSION A RGBA8 NATIVAS (2026-09-19, v1.2.6-WIP)

**Objetivo**: escalar tambien las RGBA8 (`fmt=6`), que son las que de verdad dan
el salto visual.

**Requisito**: el shader de upscale escribe un UAV RGBA8 y lee la fuente como
`uint32` (`R|G<<8|B<<16|A<<24`). Por tanto solo vale un formato host RGBA8
explicito (`R8G8B8A8_UNORM`) con un load shader que produzca RGBA8
(`bytes_per_host_block == 4`).

**Cambios** (`texture_cache.{cpp,h}`):
1. `GetTextureUpscaleFactor`: si no hay `dxgi_format_uncompressed`, usa
   `dxgi_format_unsigned` **solo si es `R8G8B8A8_UNORM`** y el load estandar
   produce RGBA8. Se rechaza el resto (`not_rgba8` / `load_not_rgba8`) para no
   corromper texturas de otros formatos (`k_8`, `k_8_8`, `k_4_4_4_4`, `k_24_8`).
2. `GetLoadShaderIndex` e `GetDXGIResourceFormat`/`GetDXGIUnormFormat(TextureKey)`:
   nuevo helper `GetTextureUpscaleRgba8Format` que devuelve el RGBA8 correcto
   (descomprimido para DXT, `dxgi_format_unsigned` para RGBA8 nativas). **Bug
   corregido**: antes devolvian `dxgi_format_uncompressed`, que en `k_8_8_8_8`
   es `UNKNOWN` -> el recurso se creaba con formato invalido y el juego escupia
   miles de `Unsupported texture formats used in the frame: k_8_8_8_8 resource`.
3. **Exclusion del frontbuffer** (`swap_texture_key_`): `RequestSwapTexture`
   registra la key de la textura de presentacion ANTES de crearla, y
   `GetTextureUpscaleFactor` la rechaza (`swap_texture`). **Bug corregido**: sin
   esto, la textura de swap de 1280x720 se creaba a Nx (2560x1440) y el
   presentador fallaba al crear el guest output ("Failed to create a command
   allocator" / 3840x2160) - el crash del primer intento.
4. **Limite de area** (cvar `dbz3_upscale_max_texels`, default 1 M texeles =
   1024x1024): evita escalar texturas enormes (2048x1024+ = 32 MB+ a x4 por
   textura), que dispararian la VRAM. `0` = sin limite. Expuesto en el launcher
   como "Limite de tamano de textura HD (Mpx)".

**Resultado (RTX 4070 SUPER, `hd_tex=2x`/`4x` + `3x` + MSAA + cap 60):**

| | aceptadas | errores | fps min | frames >100 ms | peor frame |
|---|---|---|---|---|---|
| Solo DXT (`dbz3_147`) | 12 tipos | 0 | 50.9 | 14 | 905 ms |
| DXT fix mips (`146`) | 12 tipos | 0 | 52.6 | 1 | 634 ms |
| **+RGBA8 (`152`/`153`)** | **41 tipos** | **0** | **57.6** | **0** | **55 ms** |

Con `dbz3_154` (intro+menu+demo, 52 ventanas): **1515 texturas** escaladas,
fps min 57.4, **0 frames >100 ms**, 0 errores, 0 lecturas lentas de disco. La
VRAM se mantiene (~3 GB usados de 12 GB). El gate `swap_texture` actua (1
exclusion registrada, frontbuffer intacto).

  **Pendiente**: validacion visual del usuario (nitidez de personajes/escenarios
  con `hd_tex=4x`). Los mips altos siguen siendo aproximados (§8).

## 11. REDISENO DE UX + TOPE x3 (2026-09-19, feedback del usuario)

Feedback del usuario: el feature "se puede abusar mucho sin entenderlo", el
"HD texture size limit (Mpx)" es incomprensible para el usuario promedio y en
general los controles no son user-friendly. Cambios:

- **Tope duro x3** (antes x4): la cvar `dbz3_texture_upscale` pasa a rango 1-3 y
  el launcher solo ofrece Off / x2 / x3. x4 multiplicaba VRAM y GPU casi sin
  ganancia visible sobre x3.
- **Sin "Mpx" en la vista normal**: el limite de area se movio al **tab Dev**
  como ajuste avanzado, en lenguaje no tecnico ("Bajo/Medio/Alto: cuanto
  gastar"), con la explicacion por *que hace*, no por megapixeles.
- **Nombres orientados al resultado**: la opcion de texturas pasa de
  "Texturas HD (WIP) x2/x3/x4" a **"Mejora de texturas (experimental)"** con
  **"Nitidas (uso ligero)"** y **"Muy nitidas (exigente)"**; el aviso dice
  claro que sube el consumo de GPU y que hay que reiniciar.
- **Default de area mas conservador**: `dbz3_upscale_max_texels` pasa de 1 M a
  **0.5 M texeles** (1024x512 / 512x1024), lo que deja fuera las texturas
  grandes de escenario y recorta el coste por defecto.
- **Presets de calidad reformulados** (ver §13): ya no se llaman
  Baja/Media/Alta/Ultra, sino **Rendimiento / Equilibrado / Calidad**, y
  **ninguno sube la escala interna** (esa era la otra fuente de consumo
  desmedido). Los nombres viejos se aceptan como alias al cargar.

## 12. GUARDIA DE VIDEO — causa del consumo brutal (2026-09-19)

**Sintoma**: con `hd_tex` activado el consumo de GPU se disparaba (80 % / 133 W
/ 3.1 GB en la RTX 4070 SUPER del dev, captura del overlay MSI Afterburner) y
aparecian fallos graficos, incluso con factor x2.

**Diagnostico**: el contador `upx` (texturas escaladas) llegaba a **32182** en
~35 s durante la intro, frente a ~700 en combate. La intro reproduce video (SFD)
renderizado como una textura que se **reescribe ~60 veces por segundo**; cada
reescritura marca la textura como *outdated*, dispara una carga completa y con
ella **regenera toda la cadena de mips**. Es decir: se estaba re-escalando el
video frame a frame, sin ninguna ganancia (es contenido que cambia cada frame).

**Fix** (`UpscaleBudgetAllows`): ventana deslizante; si en 0.5 s se conceden mas
de 24 upscales, se dejan de conceder durante 3 s (probable video). La decision
se **cachea por key** (`upscale_granted_keys_`) porque debe ser estable: el
recurso se crea Nx al inicio y sus recargas deben seguir tratandolo como Nx (si
no, el recurso Nx queda sin rellenar -> `device removed: 0x887A0001`). El
usuario no tiene que entender nada: el feature se frena solo cuando no aporta.

**Medicion** (`hd_tex=2x`, limite 0.5 M, escala 1x, mismo tramo de intro):

| | upx | GPU | potencia | VRAM |
|---|---|---|---|---|
| Sin guardia (`dbz3_160`) | **32182** | 80 % | 133 W | 3.1 GB |
| **Con guardia (`dbz3_162`)** | **237** | 39 % | 34 W | 1.95 GB |

Con `hd_tex=3x` y combate real (`dbz3_163`): **689 texturas**, 0 errores, 0
`device removed`, 60 FPS, GPU < 25 % / 35 W / 1.9 GB. La guardia se activa 1 vez
por sesion (al pasar la intro) y no afecta al combate.

## 13. PRESETS DE CALIDAD REFORMULADOS (2026-09-19)

Los presets (tab Video) pasan de `Auto/Baja/Media/Alta/Ultra/Manual` a:

| Preset | Escala | MSAA | Aniso | Efecto |
|---|---|---|---|---|
| **Automatico** (recomendado) | detecta GPU (1x) | segun tier | segun tier | segun tier |
| **Rendimiento** | 1x | no | no | bilinear |
| **Equilibrado** | 1x | no | 4x | fsr |
| **Calidad** | 1x | si | 16x | fsr |
| **Personalizado** | lo que ponga el usuario | | | |

Claves del cambio:
- **Ningun preset sube la escala interna** (1x siempre). "Ultra" pedia 2x de
  supersampling: justo el caso que multiplica la GPU por poco beneficio, asi que
  se elimino (quien lo quiera, sube la escala a mano). El tope del tier "high"
  ya era 1x.
- Los nombres **describen el resultado** que busca el usuario, no un nivel
  abstracto.
- El combo muestra siempre **que valores resuelve** ("Activo: Calidad -> 1x,
  MSAA ON, aniso 16, fsr") para que "Automatico" no sea una caja negra.
- **Migracion**: `low`->`performance`, `medium`->`balanced`, `high`/`ultra`->
  `quality` (alias en `QualityConfigForPreset`; siguen en `.allowed()` para que
  un `dbz3_user.toml` viejo no invalide el fichero).

## 14. FIX DEL HUD EMBORRONADO (2026-09-19b, feedback del usuario)

**Sintoma**: sobre la barra de vida del combate, los "cuadritos" que
representan los segmentos de vida salian **emborronados/sucios** (captura del
usuario, RTX 4070 S, 80 % / 133 W antes de la iteracion). No era el video de la
intro: es **contenido de HUD**.

**Causa**: los segmentos de la barra de vida son **quads diminutos con textura
pequena** (glifos de UI: 1x1..16x16), y el kernel **Catmull-Rom bicubico** puro
hace *ringing* (sobre/sub-disparo) en bordes de contraste fuerte → halo sucio
que se percibe como desenfoque. Escalar una textura de 8x8 no aporta nada de
todas formas.

**Fix (dos capas)**:
1. **Minimo de tamano** (`dbz3_upscale_min_size`, default **16**): no se
   escalan texturas cuyo ancho o alto sea menor que 16 texeles. Quita de raiz
   las micro-texturas de HUD/iconos y deja intacto el catalogo util (las
   aceptadas van de 16x32 a 512x1024). `1` = sin minimo (avanzado).
2. **Clamp anti-ringing** en `texture_upscale_cs.hlsl`: el resultado del
   bicubico se acota al `[min, max]` de las 16 muestras del kernel. Mantiene la
   nitidez sin el sobre-disparo en contornos (glifos, letras, bordes duros).

**Medicion** (`hd_tex=3x` + 0.5 M, preset Calidad, `dbz3_170`): **0 errores**,
fps min 54.7, upx 508, GPU **39 % / 33 W / 1.67 GB**. El bytecode del shader se
regenero con `fxc /T cs_5_1 /E main /Vn texture_upscale_cs /O3 /Fh ...`.

**Pendiente**: validacion visual del usuario del HUD (necesita ventana
on-screen; `long_run.ps1` la mueve fuera de pantalla y `PrintWindow` no captura
el 3D).

## 15. UX — "que no se pueda abusar" (2026-09-19b)

Principios aplicados al feature (peticion explicita del usuario):
- **Defaults seguros**: off por defecto; area maxima 0.5 M; minimo 16;
  presets que no suben la escala interna.
- **Sin jerga en la vista normal**: nada de "Mpx", "factor", "texeles". El
  control principal es un combo con **resultado** ("Nitidas" / "Muy nitidas") y
  los ajustes tecnicos viven en el tab **Dev**, explicados por lo que hacen.
- **Tope duro x3** en la cvar, no solo en la UI (aunque se edite el toml, no
  hay x4).
- **Coste visible**: al activar la mejora aparece un aviso naranja en la propia
  pestana ("mas detalle a cambio de mas consumo de GPU y VRAM").
- **Presets con nombre de intencion** (Rendimiento/Equilibrado/Calidad) y
  resumen "Activo: …" siempre visible para que "Automatico" no sea caja negra.
- **Salvaguardas internas invisibles**: la guardia de video (§12) y el minimo
  de tamano (§14) no tienen control: frenan el mal uso por si solo.

## 16. RENDIMIENTO — EL COSTE ES EL SUPERSAMPLING, NO LAS TEXTURAS HD (2026-09-19c)

**Confusion a resolver**: el usuario reporto "lo note muy fluido pero me consumia
demasiado" (80 % / 132 W) con SU config. **Esa captura era de la version vieja
(x4 HD)**; con la version actual, ese consumo viene de la **escala interna 3x**,
no de las texturas HD (que estan en OFF en su toml).

**Medido (RTX 4070 SUPER, combate, `dbz3_175..178`)**:

| Config | GPU | Potencia | VRAM |
|---|---|---|---|
| **1x + FSR** (nativo) | **22-23 %** | **29-30 W** | 1.35 GB |
| 3x interno (supersampling) | 51 % | 50 W | 2.9 GB |
| 3x + HD x4 (version vieja) | **80 %** | **132 W** | 3.1 GB |

**Conclusion**: `draw_resolution_scale` hace que el **guest renderice de verdad
a Nx** (supersampling real, `command_processor.cpp`), no es un upscale barato.
Cada paso de escala cuesta ~el doble. A 1x el port consume **30 W / 23 %**, que
es lo esperable de un port nativo a 720p. **No hay bug ni desperdicio**: a 3x el
FSR/CAS queda inerte (frontbuffer >= salida) y no se añade ninguna pasada extra
(verificado en `presenter.cpp:1065`).

**Accion (peticion del usuario: "mantener 3x pero avisar fuerte")**:
- Aviso naranja visible cuando `scale > 1` ("la GPU trabajara mucho mas...").
- **Boton "Volver a nativo (1x)"** de un clic (pone escala 1x y persiste).
- Etiquetas del combo de escala con el coste ("consume mas GPU" / "mucho mas").
- Tooltip del preset aclara que **ninguno sube la escala**.
- Tooltip del MSAA: coste moderado, se puede quitar si subes la escala.
- `1x` es el default y el recomendado (marcado en la propia opcion).

**La config del tester (SSGPrinceVegeta) se conserva** en el toml local
(`manual` + 3x, su intencion original) pero el launcher la presenta con el aviso
y la via de un clic para bajarla.

**FIX de valores fuera de rango**: el toml del tester traia
`dbz3_texture_upscale = 4` (valor viejo, ya no existe) y
`dbz3_hd_textures = 3` con `dbz3_skip_launcher = true`. Un valor fuera de rango
**invalida el fichero ENTERO** en el parser (`could not determine value type`) y
el usuario pierde todos los ajustes. Se limpio el toml; conviene recordar que
los rangos nuevos son HD textures 1-3, area 0..64M, minimo 1..4096.
