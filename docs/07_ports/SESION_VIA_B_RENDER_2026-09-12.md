# SESIÓN VÍA B — RENDER (2026-09-12)

## Objetivo
Hacer renderizar la **Vía B** (port completo PS2→B3 HD con la topología PS2:
ventanas de 44 B + IB) para poder portar modelos ausentes en HD.

## Resumen del resultado
- ✅ **2 bugs reales de `grow()` corregidos** (crash + fetch incompleto).
- ✅ Confirmado: la geometría del port es **exacta** y **llega al GPU**.
- ❌ **El modelo explota al renderizar** → **Vía B NO está validada**.
- ⚠️ **Corrección**: la "validación en juego" anterior (2026-09-12) era el
  **swap nativo `cell_native`**, NO el port.

## Contexto: la confusión previa
En ninguna sesión logueada apareció un `AFS OVERRIDE HIT` de `cell_native` /
`cell_viab`. El **logging AFS no captura `data_cmn`** en esas sesiones (ruta/caché
distinta), así que la ausencia de log **no prueba nada**; pero el render sí se ve.
La conclusión anterior ("`cell_viab` = Cell perfecto") se basó en una observación
del **swap nativo en el slot de Krillin (327)**, no del port en la 147.

## Hechos verificados (offline + captura GPU)
1. **Geometría exacta**: las 5148 ventanas del port reconstruyen el modelo PS2
   (bbox idéntica, nearest-neighbor dist **0.0000**).
2. **Llega al GPU**: `win0/win100/win500/win1000/win2000/win3000` halladas
   **contiguas** en `dbz3_vf.bin` (el GPU copió la región verbatim).
3. **El guest trocea el IB**: el cuerpo se dibuja con `prim=4` (lista) en ~11
   draws (`dma=1BD0xxxx`), cada una un rango de índices. **NO** dibuja todo el IB
   de una vez.
4. **El fetch del vértice** (fc=95) usa el **buffer global** de ventanas
   (`addr=1BD02000`) para todas las draws del cuerpo.

## Bugs de `grow()` corregidos (`awo_tools/awg_vertex_buffer.py`)
1. **Doble ajuste de la tabla AWG** (crash): la tabla AWG (offsets **relativos**
   a `awo`) vive dentro de `[awo, awg0)`; el bucle de la cabecera `#AWO` la
   trataba como punteros **absolutos** y la desplazaba **dos veces** → 16 entradas
   apuntaban a basura → *parser* `#AMB` despachaba null
   (`UNREGISTERED indirect call target=0`, `caller_lr=0x8208018C`).
   **Fix**: excluir `[tbl, tbl+n_awg*4)` de ese bucle.
2. **`AWG0+0x2C` sin actualizar** (fetch incompleto): ese campo es el **TAMAÑO del
   buffer de vértices en bytes** (plantilla: `2948*44 = 129712`). El guest fija el
   fetch con ese valor → sólo leía 2948 ventanas; los índices >2947 del IB leían
   basura → *render despedazado*. **Fix**: `set32(awg0+0x2C, new_n*44)`.
   Verificado: tras el fix hay un `VFDUMP fc=95 size=56628 dwords = 226512 B`
   (= `5148*44`).

## Diagnóstico del bloqueo restante
Aunque la geometría es correcta y completa, el port **explota**. Descartado el
**rigging** (transferir el skin HD con `--hd-skin` **empeora**). El bloqueo es:

> El guest **trocea el IB por los descriptores/rangos de parte A/B de la
> PLANTILLA**. El port cambia la topología (pool + IB) pero **deja intactos** los
> descriptores 0x60 y los mesh-refs. Al dibujar, los rangos `B` cortan el IB del
> port en posiciones que ya no corresponden a las partes reales → conectividad
> mal → "explosión".

### Evidencia adicional (IB del guest ≠ IB del fichero)
La draw del cuerpo (`prim=4`, `dma=1BD0…`) dibuja una IB que **no coincide** con
la del fichero del port en ese offset:
- fichero port: `IB[127] = 49, 48, 49, 50, …`
- guest (draw `dma=1BD020FE`): `2429 2430 2429 2428 …`
Además, `dma=1BD020FE` cae **dentro** de la región del vertex buffer
(`VFDUMP fc=95 addr=1BD02000`), lo que sugiere que el guest **reubica/reordena**
la IB y/o hay una inconsistencia de buffers (IB vs VB) al crecer. Esto apunta a
que el bloqueo es más profundo que "solo recalcular los rangos A/B": **hay que
entender cómo el guest construye/ubica la IB** (probablemente la rearma a partir
de los descriptores → mapa de memorias).

## Decisión (2026-09-12)
**APARCAR** la Vía B: el render es un **RE profundo** (reconstrucción del
troceado + ubicación de buffers por el guest) con payoff incierto, y el proyecto
ya tiene vías **validadas** (swap nativo HD→HD + Vía A inyección). Se deja el
trabajo acotado y documentado aquí para retomarlo solo si aparece un modelo
ausente en HD que Vía A no cubra. `grow()` queda arreglado (bugfix real).

Ver §3.4.8 de `AGENTS.md` (descriptores 0x60 y partición del pool) y
`awo_tools/phase_c_descriptors.py`, `phase_c_meshgroup.py`.

## Próximos pasos (Vía B)
1. **RE del troceado**: entender cómo el guest deriva las draws (`B` ranges) de
   los descriptores/mesh-refs y si dependen también de `A`.
2. **Reconstruir los rangos A/B + mesh-refs** para la topología nueva (pool + IB
   del port), de forma consistente.
3. Re-validar con Cell (que ya "cabe"/crece) **antes** de la prueba definitiva.
4. Solo entonces: prueba definitiva (modelo ausente en HD).

## Herramientas / ficheros tocados
- `awo_tools/awg_vertex_buffer.py` — fixes de `grow()`; modo `info` lee el tamaño
  real vía `g(0x2C)`.
- `mod center hd/ports/port_b3_windows.py` — `--hd-skin` (skin HD por vecino más
  cercano; descartado/empeora), `--fit`/grow.
- Mods de test: `_grow327` (port en slot Krillin 327), `cell_viab`/`cell_viab_grow`
  (147). `cell_native` (327) = swap nativo (referencia que SÍ renderiza).
- Capturas: `out/build/win-amd64-release/dbz3_draws.log` + `dbz3_vf.bin`
  (marker `dbz3_drawlog.on`; la DLL `rexgpu-xenos` sigue instrumentada).
