# SESIÓN: SEMÁNTICA REAL DEL DRAW (Vía B PS2→B3 HD) — 2026-09-11

> Continúa `SESION_GPU_DRAW_2026-09-11.md` y `SESION_VIA_B_RENDER_2026-09-12.md`.
> Aquí queda **probado** cómo dibuja el guest el AWG0 y por qué el port explota.
> Leer ANTES de tocar el port.

## 0. 🔴 RETOMO RÁPIDO (leer esto primero tras un compact)

**DÓNDE ESTAMOS:** el port PS2→B3 HD tiene **geometría + draw CORRECTOS** (1 solo
draw `prim=6` strip; el guest usa VB+IB verbatim). El **único bloqueo restante es
el SKINNING/RIG** (skin PS2 vs animación HD). Nada de esto es entrega.
- Mod de referencia ACTIVO: `_strip3` (slot **327**/Krillin, `prim=6` 1 draw).
- DLLs canónicas LIMPIAS (sin instrumentación) en `out/win-amd64-baseline`,
  build del juego y `rexglue/bin`.
- Backups instrumentados: `%TEMP%\opencode\draw_evidence\command_processor.cpp.*.bak`.

**REPRODUCIR EL PORT** (PS2 Cell F2 → plantilla HD e147 → slot 327):
```powershell
# 1) extraer el modelo PS2 (#AMB o #AMO0) -> json
python "mod center hd\ports\port_ps2_b3_extract.py" <ps2_cell.amb> cell_extract.json
# 2) ventanas 44B + IB-lista (mapeo de huesos por label). Genera y crece (grow) si hace falta.
python "mod center hd\ports\port_b3_windows.py" cell_extract.json <tpl_e147.bin> port.amb
# 3) IB -> STRIP (winding preservado) + anular arms (+0x3C/+0x44) + desc[0]=B[0,n_ib)
python "mod center hd\ports\port_b3_strip.py" port.amb port_strip.bin
# 4) LZX /N:2048 + pad a multiplos de 0x1000:
#    & "mod center\Xbox 360 Compression - Decompression tool .../xbcompress.exe" /N:2048 port_strip.bin port_strip.lzx
# 5) instalar (UN SOLO mod activo):
#    mods\<mod>\us\data_cmn.afs\327\geom.bin   <- port_strip.lzx (+pad)
# verificación offline (sin abrir el juego):
python "awo_tools\render_bin_windows.py" port_strip.bin out.png "$PWD\awo_tools" 0 --strip
```
Ficheros de referencia (en `%TEMP%\opencode\phaseb\`, regenerables): `e147.bin`
(plantilla HD AWG0 de Cell F2, entrada 147 de `us/data_cmn.afs`),
`cell_extract2.json` (extract PS2 de Cell F2), `port_cell_grow_fix2.amb` (port sin
strip), `port_cell_strip3.bin` (port del mod `_strip3`).

**SIGUIENTE PASO (barato, 1 test):** aislar manos/cara → emitir SOLO el cuerpo
(huesos 0-33) y **omitir los triángulos con vértices de hueso 34-47** (que en el
HD viven en AWG1-16). Si el cuerpo renderiza bien ⇒ el flap es de manos/cara
(bones 34-47 metidos en AWG0). Si sigue mal ⇒ el problema es del rig/cuerpo.
Alternativas en §6.

**REGLA DE ORO:** los tests se hacen con **UN SOLO mod activo** (marker
`.disabled`); el runtime sirve el primero por orden alfabético.

## 1. MÉTODO (evidencia dura, sin especulación)

Se cruzaron:
- La captura `dbz3_draws.log` (instrumentación de `command_processor.cpp`, log de
  draws con `dma`, `idx`, `prim`, e IB de cada draw).
- El bin del port (`port_cell_grow_fix2.amb`, AWG0 5148 ventanas / 8724 índices).
- Los descriptores 0x60 de la plantilla `e147.bin` (`awo_tools/phase_c_descriptors.py`).
- Tool canónico `awo_tools/awg_vertex_buffer.py` (modelo de ventanas).

## 2. HECHOS PROBADOS (lo importante de esta sesión)

1. **El guest usa el IB del port VERBATIM.** Los **33/33** draws del cuerpo
   coinciden índice a índice con `AwgVertexBuffer.load(port).indices()` en
   `(dma - 0x1BD00000)//2`. (Esto **refuta** la nota previa "la IB del guest ≠
   la del fichero": era un **error de offset** de la comparación.)
2. **Los draws los gobiernan los descriptores 0x60.** El `(dma-0x1BD00000)//2`
   de cada draw == el `B_start` del descriptor correspondiente (match exacto
   para los 28 de cuerpo + manos/cara).
3. **El vertex buffer es un fetch GLOBAL**, no por-parte: `VF[95]`
   `addr=0x1BD04000 size=56628` dwords = **226512 B = 5148×44** (todo el pool de
   ventanas). El IB vive aparte, justo antes (`0x1BD00000..0x1BD031xx`). **No
   solapan.** ⇒ **los rangos `A` no afectan al fetch**; solo importan `B` + prim.
4. **El prim es POR DESCRIPTOR**: cuerpo = `prim=6` (Xenos raw = `kTriangleStrip`),
   manos/cara = `prim=4` (= `kTriangleList`). Correlaciona 1:1 con:
   - descriptor 0x60 campo **`+0x48`**: `0x500` (5) cuerpo / `0x400` (4) manos-cara.
     Enum D3D del SDK: `kTriangleList=4`, `kTriangleStrip=5`.
   - part-descriptor de los arms campo **`+0x30`**: 5 (bone0/cuerpo) / 4 (manos/cara).
5. **El port emitía el IB como LISTA** (`port_b3_windows.py` línea 14/197:
   `ib = [i for t in tris for i in t]`). El guest lo dibuja como **strip** ⇒
   triangulación incorrecta ⇒ "explosión". **Ésta es la causa raíz del render.**
6. **El IB-lista del port es 100% válido** (0 triángulos degenerados): el
   problema no es la malla sino la *interpretación* (list vs strip).
7. **La geometría del port es correcta**: render offline (ventanas + IB-lista +
   `world[bone]`) = Cell F2 perfecto (ver `%TEMP%\opencode\phaseb\view_port.png`).
8. **🔴 HAY DOS FUENTES DE DRAW (probado 2026-09-11 con captura de todos los
   draws)**: el port se dibuja con **6 draws distintos**:
   - `off=0 prim=6` (el cuerpo, el strip de `desc[0]`).
   - `off=4243/5251/5485/5611/6109 prim=4` (las **manos/cara**).
   Los offsets 4243+ **NO** vienen de los descriptores 0x60 sino de los
   **part-descriptores de los arms**, que guardan:
   ```
   +0x38 vert_start  +0x3C vert_count   (rango de VÉRTICES)
   +0x40 idx_start   +0x44 idx_count    (rango de ÍNDICES)   <- el que dibuja
   +0x48 label[]
   ```
   (bone0 body `idx[0,108)`; bone23 LHAND `idx[4243,4333)`; bone30 RHAND
   `idx[4747,4837)`; bone36/38 dientes `idx[5251,5293)/[5485,5527)`;
   bone40 FACE `idx[5611,5711)`; bone47 `idx[6109,6173)`).
   ⇒ Anular solo `+0x3C` **NO** basta: hay que anular **también `+0x44`**.
   ⇒ **Ésta era la 2ª fuente** que producía el "flap" (manos/cara dibujando el
   IB del port en los offsets de la plantilla).
   Tool: `mod center hd/ports/port_b3_strip.py` (ahora anula `+0x3C` y `+0x44`).
9. **Tras eliminar la 2ª fuente, queda 1 solo draw real** (`prim=6 idx=8726
   off=0`); el VB que recibe el guest es **copia verbatim** de las ventanas del
   fichero (muestra 11/11) y el IB coincide ⇒ **VB+IB+draw correctos**. Aun así el
   modelo sale deforme ⇒ **el problema restante es el SKINNING**:
   - Labels PS2 y HD **idénticos y mismo orden** (bmap=identidad) ⇒ índices de
     hueso correctos.
   - Pero el 66% de los vértices del port caen en un hueso distinto al del
     vértice HD más cercano (el skin PS2 difiere del rig HD en zonas limítrofes:
     mangas, torso). La animación HD aplicada al mesh con skin PS2 lo deforma mal
     (en bind-pose no se nota: `inv(bind)·model` cancela el hueso).
   - ⇒ **El port necesita el SKIN HD** (transferir hueso+peso por vecindad), no el
     skin PS2. Probar `--hd-skin` **ahora que el IB está bien** (la nota previa
     "empeora" se midió con el port-lista que explotaba → ya no aplica).

## 3. INTENTOS Y RESULTADOS (NO REPETIR)

| Test | Qué hace | Resultado |
|---|---|---|
| `_grow327` | port tal cual (IB lista + descriptores de plantilla) | explota |
| `_desc_one` | 1 descriptor `B=[0,n_ib)`, `+0x48=4` (list), resto a `B_c=0`, arms `+0x3C=0` | **sigue explotando** ⇒ `+0x48` **no** basta para cambiar el prim del draw (o el guest no lo lee de ahí) |
| `_strip2` | IB reescrito como **una tira** (winding preservado) + 1 descriptor `B=[0,n_ib)` | **cambia** la deformidad: se reconocen cabeza/torso/brazo, pero **aún explota** con un flap gigante |
| `_fit_strip` | port **decimado** (`--fit`) + strip | **inválido**: `cluster_fit` deforma la malla (no sirve para aislar) |
| **`_grow_tpl`** | **plantilla NATIVA Cell F2 pasada por `grow()`** (5148/8724), sin tocar nada más | **RENDERIZA PERFECTO** ⇒ **`grow()` está BIEN**. El flap del port **NO** es de `grow()` ⇒ **hay una 2ª fuente de draw** (o es de los datos del port) |
| `_strip3` | strip + `desc[0]` + anular arms (`+0x3C` **y `+0x44`**) | **queda 1 SOLO draw real** (`prim=6 idx=8726`); VB+IB del guest correctos; **pero sigue deforme** ⇒ el problema restante es el **SKIN** |
| `_hdskin_strip` | `_strip3` + **skin HD transferido por vecindad** (2587/5148 reasignados) | **MUCHO PEOR** ⇒ la transferencia HD por vecindad **NO sirve** (coincide con la nota del AGENTS) |

**Conclusión de `_grow_tpl` (2026-09-11)**: descartado `grow()`. El port explota
por una **segunda vía de dibujo** que el port no controla, o por los propios
datos del port (IB/descriptor). El siguiente paso es **capturar los draws del
port** para ver cuántos hay y de dónde salen.

**Bug de herramienta corregido**: `awo_tools/awg_vertex_buffer.py` `_parse`
calculaba `n`/`vb0` con el **máximo índice del IB**; en un fichero crecido con
`grow` el IB puede no referenciar el tramo nuevo → `vb0` mal. Ahora usa
`g(0x2C)//44` (tamaño real del buffer de ventanas, el que usa el guest).

Nota: el strip de `_strip2` fue validado offline (render idéntico al del IB-lista,
`orient_mal=0`, `faltan=0`). Es decir, el IB del port **ya es un strip correcto**;
algo **adicional** sigue dibujándose mal.

## 4. HERRAMIENTAS (persistidas en el repo)

- `awo_tools/render_bin_windows.py <bin> <png> <awo_tools> [yaw] [--strip]`:
  render correcto del modelo de ventanas (lista o strip). Feedback offline.
- `mod center hd/ports/port_b3_strip.py <in> <out>`: stripificador **preservando
  orden y winding** (recorre los triángulos EN ORDEN y une por aristas; conectores
  degenerados con paridad ajustada) + reescribe el descriptor a `B=[0,n_ib)`.
- `awo_tools/strip_order_winding.py <bin> <awo_tools> <out.txt>`: valida el
  strip (`orient_mal=0`).
- `awo_tools/phase_d_cmp_guest_ib.py <log> <port.bin> <awo_tools>`: comprueba si
  el guest usa el IB del fichero (match por draw).
- `awo_tools/phase_d_descriptor_corr.py <bin>`: correlaciona campos del
  descriptor con el prim real del draw (localiza `+0x48`).
- Capturas crudas: `C:\Users\javie\AppData\Local\Temp\opencode\draw_evidence\`
  (regenerables re-instrumentando).

## 5. SIGUIENTE PASO PROPUESTO (para retomar)

**`grow()` está DESCARTADO** (`_grow_tpl` renderiza perfecto). El IB del port es
un strip correcto (validado offline). Pero el port sigue con un flap ⇒ **hay una
2ª fuente de draw**, o es de los datos del port. Plan:

1. **Re-instrumentar** `command_processor.cpp` (backup con la instrumentación en
   `%TEMP%\opencode\draw_evidence\command_processor.cpp.dbz3bak`) y capturar los
   draws de `_strip2`: **cuántos** draws hace el AWG0, con qué `dma`/`idx`/`prim`.
   Si hay más de uno, los mesh-refs/arms/AWG1-16 siguen produciendo draws.
   ⚠️ Quitar el **dedup** por `(idx,dma)` para no ocultar draws repetidos.
2. **Candidatos a fuente extra**: los **mesh-refs** (0x50) del mesh-group, los
   **part-descriptores de los arms** (`+0x30` prim, `+0x38/+0x3C` rango), y los
   **AWG1-16** (manos/cara HD que el port NO toca y siguen dibujándose).
3. Probar a **anular por completo** arms + mesh-refs + AWG1-16 y dejar SOLO
   desc[0] como strip, para aislar.

⚠️ **NO volver a** interpretar `+0x48` como interruptor único del prim (probado
que no cambia el render). ⚠️ **NO** repetir el análisis de "consumidor
posicional" (refutado: era bug de base de índices del tool). ⚠️ **NO** culpar a
`grow()` (probado OK con `_grow_tpl`). ⚠️ El **fit** (`cluster_fit`) deforma la
malla → no sirve para validar render.

## 6. ESTADO REAL Y CONCLUSIÓN (2026-09-11)

**Lo que YA está resuelto del port (no volver a tocarlo):**
- La **geometría** es correcta (render offline perfecto).
- El **draw**: 1 solo `prim=6` strip; el guest usa VB+IB verbatim.
- La **2ª fuente de draw** (arms `+0x40/+0x44`) está localizada y anulada.
- `grow()` OK. El loader `awg_vertex_buffer` corregido.

**Bloqueo REAL restante = SKINNING/RIG.** El mesh del port lleva el **skin PS2**;
la animación HD aplicada encima lo deforma (en bind no se ve porque
`inv(bind)·model` cancela el hueso). El % de huesos que difieren del HD más
cercano es alto (~66%), y la **transferencia HD por vecindad empeora**. Esto es
el problema conocido de fondo: **el HD es un RE-TRABAJO del rig**, no 1:1.

**Próximos experimentos sugeridos (baratos, uno por test):**
1. **Restringir el mesh a huesos 0-33** (cuerpo): quitar/omitir las caras de los
   vértices con hueso 34-47 (manos/cara, que en el HD viven en AWG1-16). Si el
   cuerpo renderiza bien ⇒ el flap es de las manos/cara (bones 34-47 en AWG0).
2. **Mantener la topología PS2 pero el skin del HD** con un método mejor que
   "vértice más cercano" (p. ej. transfer por hueso del vértice HD más cercano
   con suavizado por región / propagación).
3. **Port por regiones**: emitir las manos/cara en sus propios AWGs (1 hueso)
   como hace el HD, en vez de meterlas en AWG0.

⚠️ **NO volver a** interpretar `+0x48` como interruptor único del prim (probado
que no cambia el render). ⚠️ **NO** repetir el análisis de "consumidor
posicional" (refutado: era bug de base de índices del tool). ⚠️ **NO** culpar a
`grow()` (probado OK con `_grow_tpl`). ⚠️ El **fit** (`cluster_fit`) deforma la
malla → no sirve para validar render. ⚠️ **NO** `--hd-skin` por vértice más
cercano (empeora, probado).

## 7. REFERENCIAS
- `awo_tools/awg_vertex_buffer.py` (modelo de ventanas), `phase_c_descriptors.py`,
  `phase_c_meshgroup.py`.
- `mod center hd/ports/port_b3_windows.py` (emisión; **falta** integrar la pasada
  de `strip`), `mod center hd/ports/port_b3_strip.py` (strip + anula arms).
- `docs/07_ports/SESION_GPU_DRAW_2026-09-11.md`,
  `SESION_VIA_B_RENDER_2026-09-12.md`.
- Captura cruda: `C:\Users\javie\AppData\Local\Temp\opencode\draw_evidence\`.

## 8. AISLAMIENTO POR HUESO (2026-09-13) — TESTS EN JUEGO PENDIENTES

**Hallazgo (histograma de huesos, refs del IB)**:
- **Plantilla `e147.bin` (AWG0)**: usa **SOLO huesos 0-33**. Los huesos 34-47 NO
  se referencian NUNCA en AWG0 (su geometría vive en los AWG auxiliares). Labels:
  34-41 = boca/dientes/cara (`XCEL_M_*`, `XCEL_L00_FACE`), 42-47 = **cola**
  (`CEL_T_TAIL1-6`).
- **Port `port_cell_grow_fix2.amb` (AWG0)**: mete **1638 refs a huesos 34-47**
  (908 vértices) DENTRO de AWG0, incluida la **cola (43-47)** y la cara. Eso es
  **estructuralmente distinto** de la plantilla ⇒ candidato directo al flap (los
  ejes de esos huesos en AWG0 no los usa la plantilla; la cola puede tener ejes
  "muertos").

Detalle (refs por hueso, port): `32:1466` (vs 68 en plantilla) y `34-47` suman
1638; la plantilla reparte esas zonas de otra forma (`0:2058`, `23:837`).

**Tests preparados** (`port_b3_strip.py ... --max-bone=N`, filtra triángulos que
toquen un vértice de hueso > N, DESPUÉS de recorrer el IB-lista y ANTES de
stripificar):
- `mods/_body33` (**ACTIVO**): solo cuerpo (huesos ≤33). 2908→2320 triángulos.
  Si el cuerpo renderiza limpio ⇒ el flap viene de boca/cara/cola en AWG0.
- `mods/_nottail` (`.disabled`): cuerpo+boca/cara (≤41), sin cola. 2908→2700.
  Sirve para afinar si `_body33` sale bien.

Offline verificado (`render_bin_windows.py --strip`, `strip_order_winding.py`):
`orient_mal=0`, sin NaN, bbox sano en ambos. ⚠️ El render offline SIEMPRE sale
bien (usa `world[bone]·pos` = model ⇒ cancela el hueso): **el flap solo se ve
in-game**.

**Cómo se hizo**: `python "mod center hd\ports\port_b3_strip.py" <port.amb>
<out.bin> --max-bone=33` → `xbcompress /N:2048` → pad a 0x1000 → instalar en
`mods/<mod>/us/data_cmn.afs/327/geom.bin` (un solo mod activo). Roundtrip
verificado (`xbdecompress` = byte-idéntico).

**Interpretación esperada**:
- `_body33` OK ⇒ reencaminar boca/cara/cola a sus AWG (1-hueso) o excluirlas.
- `_body33` aún deforme ⇒ el problema es del **cuerpo/rig** (0-33), no de 34-47.

## 9. REFERENCIAS NUEVAS
- `mod center hd/ports/port_b3_strip.py` (**NUEVO** `--max-bone=N`).
- Mods de test: `mods/_body33` (activo), `mods/_nottail` (disabled); UNO a la vez.
- Intermedios: `%TEMP%\opencode\port3\` (`port_body33.bin`, `port_nottail.bin`,
  `check_bins.py`, `bonestats.py`, `labels.py`).

## 10. 🔴 EXPERIMENTO DECISIVO GPU (2026-09-13) — RESULTADO

Se instrumentó `command_processor.cpp` (temporal, YA REVERTIDO) para volcar por
draw la **paleta `fc=94` completa + VB `fc=95` + IB** a `dbz3_capture.bin`
(formato: header 32 B + `nslots`×(20 B + datos) + IB). Se capturaron DOS
partidas en el character select (slot 327):

- **Captura A** = port `_strip3` (explota).
- **Captura B** = control `_grow_tpl` (plantilla NATIVA crecida; renderiza
  PERFECTO).

### Hallazgos (duros)

1. **El VB del guest es COPIA VERBATIM de las ventanas del bin** (capturado y
   comparado byte a byte). Geometría/IB confirmados.
2. **La paleta `fc=94` es IDÉNTICA entre el port (que explota) y el nativo (que
   renderiza perfecto)** — mismos valores byte a byte (misma escena/pose). ⇒
   **LA PALETA NO ES EL PROBLEMA.** El GPU recibe exactamente el mismo skinning.
3. ⇒ **El fallo está en los datos por-vértice `(pos, bone)` de las ventanas del
   port** (el skin PS2 aplicado al rig HD), NO en el draw, el IB ni la paleta.

### Datos de la paleta (para futuras sesiones)

- `fc=94`: **128 matrices × 48 B** (6144 B) por draw; `endian=2`. El bone se lee
  como 1 byte con `k8in32` ⇒ afecta al byte **+19** de la ventana (no +16).
- ⚠️ El layout de la paleta **NO es** el naive "3×vec4 = 3x4 con traslación en la
  col 3": ninguna de las interpretaciones probadas
  (`%TEMP%\opencode\port3\pal_layout.py`) da matrices rígidas ni reconstruye el
  nativo. **Hay un remapeo/offset de slot que NO está decodificado** (la paleta
  NO se indexa por el bone crudo: p.ej. el record 0 es cero y el nativo usa el
  bone 0). Pendiente: decodificar el mapeo `bone -> slot de paleta` (probable
  base por draw/AWG).
- La captura del port se guardó en
  `%TEMP%\opencode\port3\capture_port\` y la del nativo en `...\capture_native\`.

### Conclusión

La causa raíz de Vía B es el **skinning `(pos, bone)` de las ventanas**, que
proviene del **skin PS2**. La paleta/draw son correctos. El siguiente paso real es
arreglar la asignación de hueso por vértice (parseo del skin PS2; ver
`INVESTIGACION_PS2_HD_2026-09-13.md` §5-6: los formatos PS2 usan **tablas de
peso con 2 influencias** — el extractor puede estar leyendo mal). Alternativa:
clonar el skin del HD (topología distinta → sólo por región).

⚠️ **Instrumentación REVERTIDA** (`command_processor.cpp` de vuelta a 1616
líneas, 0 marcas) y DLL limpia (6165504 B) reinstalada en build-release, dual y
`rexglue/bin`. Capturas y scripts en `%TEMP%\opencode\port3\`.

## 11. MODS — ESTADO AL CIERRE
- Activo: **`cell_best2`** (Vía A: cuerpo PS2 + manos/cara HD; entrega usable).
- `.disabled`: `_strip3`, `_body33`, `_nottail`, `_grow_tpl`, `_bone0port` y demás.

## 12. ITERACIÓN 2026-09-13b — AUDITORÍA DEL SKIN PS2 + TEST BONE0 (RETOMO)

### Dónde quedamos (resumen para continuar)
- **Causa raíz de Vía B confirmada**: draw + IB + **paleta** son correctos (la
  paleta del port es IDÉNTICA a la del nativo `_grow_tpl` que renderiza perfecto
  — ver §10). El fallo está en los **datos `(pos, bone, w)` por vértice** de las
  ventanas del port (el skin PS2 aplicado al rig HD).

### Auditoría del skin (pipeline `port_ps2_b3_extract.py` → `port_b3_windows.py`)
- `extract_skin()` lee las tablas de peso del rig PS2: para cada hueso hay un
  puntero en `amg + 32 + bi*80 + 52`; chunks de 32 B en `rig+16+i*32`:
  `weight(+0), ch_len(+4), ch_loc(+8), sb_len(+12), sb_loc(+16)`. Los vértices
  referidos (`amg+ch_loc+k*32+12`) se asignan a `[bi, weight]` (**first-wins**).
- **Cobertura: 3922/5148 verts.** El resto (partes de manos/cara: bones
  23/30/36/38/40/47) **NO tienen skin** y caen al **hueso de la PARTE**
  (`p["bone"]`) con `w=1.0`.
- Pesos 0.2–1.0 (mode 1.0). La web confirma que Budokai PS2 usa **2 influencias
  (hueso + padre, w y 1−w)**; el extractor **colapsa a 1 influencia**.
- `port_b3_windows.py`: `hb = bmap[ps2_bone]` (label→label; para Cell F2 PS2 y HD
  tienen los MISMOS labels y orden ⇒ `bmap` = identidad). `w` = peso PS2.
- **Referencia de la comunidad para el parseo fino**: `budokai ps2 2025.ms`
  (killercracker/SleepyZay, `modding resources update 3\3DMax\` o
  `...update 2\lean bone tutorial\budokai_updated.ms`): `weightData` =
  `weight, weightVertCount, weightVertOffset, weightVertCount2, weightVertOffset2`.

### Test de aislamiento `_bone0port` → **CRASHEA**
- `_bone0port` = TODO el port rígido al hueso 0 (posiciones recomputadas a
  `inv(world[0])·model`; hueso 0 en todos). **El juego crashea** en el character
  select (no se llegó a ver nada).
- ⚠️ **Casualidad reveladora**: el **record 0 de la paleta capturada es TODO
  CEROS** (§10). Si todos los vértices usan bone 0 y su matriz de paleta es cero,
  todo colapsa al origen ⇒ geometría degenerada ⇒ crash probable.
- ⇒ **Pista fuerte: el slot de paleta NO se indexa por el bone crudo** (el nativo
  usa el bone 0 y renderiza bien; el record 0 es cero). Hay un **remapeo
  `bone → slot` sin decodificar**.

### Hipótesis vivas (orden de probabilidad)
1. **Remapero `bone→slot` de la paleta** sin decodificar (y el layout de 48 B de
   la matriz, que tampoco es el naive "3×vec4 = 3x4 con traslación en col 3").
2. Asignación hueso/peso del skin PS2 (2 influencias colapsadas a 1; 1226 verts
   sin skin).
3. Cálculo bone-local (`inv(world)`) — menos probable (el render offline del
   nativo es correcto, lo que valida `bind_worlds()` contra el fichero del juego).

### Próximos pasos (siguiente iteración)
0. **NO volver a probar `_bone0port`** (crashea).
1. **Decodificar el layout + mapeo `bone→slot` de la paleta `fc=94`** usando las
   capturas guardadas (`%TEMP%\opencode\port3\capture_native\`). Método: en pose
   neutra, buscar en la paleta matrices ≈ identidad (slot del hueso raíz) y, para
   cada bone, localizar el slot cuya matriz sea una transformación rígida válida;
   validar reconstruyendo el NATIVO (debe salir Cell F2).
2. Con `slot()` y el layout: `skinned = palette[slot(bone)]·[pos,1]` offline para
   el port y el nativo; localizar exactamente los vértices/slots que explotan.
3. Si el slot/layout es correcto y aun así explota → arreglar el **skin PS2**
   (2 influencias) con la referencia de la comunidad.

### Comandos de reproducción (resumen)
```powershell
# capturar (requiere re-instrumentar command_processor.cpp — backup en
# %TEMP%\opencode\draw_evidence\command_processor.cpp.instrumented.bak —
# y recompilar rexgpu-xenos; NO olvidar revertir + reinstalar DLL limpia)
# analizar:
python "%TEMP%\opencode\port3\compare_pal.py"     # paleta port vs nativo
python "%TEMP%\opencode\port3\pal_layout.py"       # probar layouts de paleta
# regenerar el port:
python "mod center hd\ports\port_b3_windows.py" <extract.json> <tpl.bin> out.amb
python "mod center hd\ports\port_b3_strip.py" out.amb out_strip.bin [--max-bone=N]
```

### Ficheros clave de esta iteración
- Scripts: `%TEMP%\opencode\port3\` (`deep_analyze.py`, `compare_pal.py`,
  `pal_layout.py`, `bonemap.py`, `check_bins.py`, `bonestats.py`, `labels.py`).
- Capturas: `%TEMP%\opencode\port3\capture_port\` y `capture_native\`.
- Mods: `_bone0port` (crash), `_body33`, `_nottail`, `_strip3`, `_grow_tpl`.
- Estado del juego: DLL LIMPIA (6165504 B), `cell_best2` activo, 0 marcadores.

## 13. ITERACIÓN 2026-09-13c — BUG DEL IB RESUELTO (el "explosion" NO era el skin)

### Cómo se encontró (analizando las capturas guardadas)
- Se comparó el draw del cuerpo del PORT vs el NATIVO desde
  `%TEMP%\opencode\port3\capture_port\` y `...\capture_native\` (`compare_pal.py`,
  `deep_analyze.py`, `repro.py`, `ib_stats.py`, `cmp_ib.py`).
- **El IB del guest coincide con el `IB` del bin hasta el índice 6301** y a partir
  de ahí hay **BASURA** (`0xAAAA`, `0xD69A`, `0xFFFF`, índices hasta 63891).
  6301·2 = **12602 B** = el IB de la **plantilla original**.
- **Causa**: `awg+0x34` = **TAMAÑO del IB en bytes**. En la plantilla vale `2*n_ib`
  en TODOS los AWGs (AWG1 816=2·408, AWG2 912=2·456, AWG3 1056=2·528…). `grow()`
  actualizaba `0x2C` (VB), `0x30` (ib_rel) y `0x38` (end) **pero NO `0x34`** → el
  guest dimensionaba su copia del IB con el valor viejo (12602) → solo 6301 índices
  válidos → el resto producía vertex fetch fuera de rango → **explosión**.
- **FIX** en `awo_tools/awg_vertex_buffer.py::grow()`:
  `set32(b, self.awg0 + 0x34, new_nib * 2)`.

### Resultado en juego (tras el fix)
- **Ya NO explota**: sale un **Cell conectado** (deforme, pero reconocible). El
  pipeline (ventanas + IB + draw `prim=6` + paleta + `grow`) está **VALIDADO**.
- **Test `_strip4r1`** (TODO rígido al hueso 1, `pos=inv(world[1])·model`):
  **Cell Forma 2 en T‑pose PERFECTO con texturas, silueta y escala correctas.**

### Bloqueo restante = SKIN/ANIMACIÓN (reabierto)
- `_strip4` (skin PS2 real) → deforme. `_strip4hds` (skin HD por vecino más
  cercano) → deforme. `_strip4w1` (huesos PS2, `w=1.0`) → **mejor**: pierna y
  cintura bien, torso/brazos mal.
- Comprobado: `world` PS2 == `world` HD (48/48) y `labels` PS2 == HD (48/48).
  Los `parts`/skin son plausibles (bone 32=head con 819 verts es normal: el nativo
  tiene la cabeza en un AWG auxiliar aparte).
- ⚠️ **Todas las conclusiones de skin anteriores (sesión §12, `_body33`, `--hd-skin`)
  se hicieron con el IB roto → INVALIDAS.** Reabrir el análisis del skin.
- **Herramientas**: `awo_tools/awg_vertex_buffer.py` (fix aplicado).
- **Mods de trabajo (uno activo)**: `_strip4` (skin PS2), **`_strip4r1` (rígido
  hueso 1 = PERFECTO)**, `_strip4w1`, `_strip4hds`, `_strip4b33`, `_strip4nt`.
- **Scripts**: `%TEMP%\opencode\port3\{ib_stats,cmp_ib,maxidx,repro,pal_analysis,
  dump_pal,checkbone}.py`.

## 14. ITERACIÓN 2026-09-13d — SKIN POR SUPERFICIE (FALLIDO) + LÍMITES

### Qué se intentó
- **Transfer de skin por superficie** (`port_b3_windows.py --surface-skin[=w]`):
  punto más cercano sobre los TRIÁNGULOS del template HD en model-space
  (baricéntricas; hueso del vértice dominante). scipy cKDTree. Reasigna
  3867/5148, dist media 0.60, max 7.4 (manos/cara/cola no están en el AWG0 HD).
- **Resultado en juego `_strip4surf` = MÁS DEFORME** que el skin PS2. ⇒ **la
  asignación de huesos NO es la causa** (el skin PS2, que es el autorado, es el
  mejor hasta ahora). `--hd-skin` (grid con radio ±1.5 u) también fallaba por
  bug de radio; el surface lo mejora pero empeora el render ⇒ descartado.

### Orden de calidad observado (todos con el IB ya arreglado)
1. `_strip4r1` (TODO rígido a hueso 1) → **PERFECTO** (valida geometría/pipeline).
2. `_strip4w1` (huesos PS2 reales, `w=1.0`) → **mejor**: pierna+cintura OK, torso/
   brazos mal. ⇒ el peso `w` del PS2 estorba (blending con 2ª influencia que no
   cuadra).
3. `_strip4` (huesos + `w` PS2) → deforme.
4. `_strip4hds` (huesos HD por vecino) / `_strip4surf` (por superficie) → peor.

### Hechos duros nuevos
- `world` PS2 == HD (48/48) y `labels` 48/48. `parts`/skin plausibles.
- **La paleta `fc=94` NO es `world`** (bind): buscando las 12 floats de cada
  `world[b]` en la paleta (row-major / transpuesta / 4x3) → **0 coincidencias**.
  ⇒ el select **no está en bind**, o el layout de 48 B no es una 3x4 directa.
- Reproducir el draw con la paleta capturada da valores enormes (basura) **también
  para el NATIVO** ⇒ la captura de `fc=94` no es un array de matrices 3x4 legible
  por esta vía; la decodificación de la paleta queda **bloqueada**.
- **Deducción**: el slot 0 de la paleta capturada es CERO pero el nativo usa el
  hueso 0 con 772 verts y renderiza bien ⇒ el shader **no indexa `palette[bone]`
  crudo** (hay remapeo/slot y/o 2ª influencia). No reproducido todavía.

### Conclusión / límite
- El **pipeline Vía B está resuelto** (IB `awg+0x34`). El **skinning** del guest
  (remapeo `bone→slot`, layout real de la paleta y 2ª influencia) **no está
  decodificado** y es el bloqueo restante. Los intentos de transferir el skin
  (vecino/superficie) **empeoran**, lo que sugiere que el problema no es "qué
  hueso" sino **cómo el shader compone la transformación** (paleta/blend).
- **Próximo recomendado**: RE del shader de skinning (traducir el VS del Xenos)
  o captura de la paleta en una pose conocida para resolver `bone→slot` y el
  blend de 2ª influencia. Alternativa de entrega: Vía A (`cell_best2`).
- Mods de trabajo nuevos: `_strip4surf`, `_strip4surfw1` (uno activo a la vez).
- Herramienta nueva: `port_b3_windows.py --surface-skin[=w]` +
  `transfer_skin_surface()` (`_closest_on_tri`).

## 15. ITERACIÓN 2026-09-13e — VS DE SKINNING DECODIFICADO + BUG DE PALETA NaN

### Cómo se obtuvo
- Se activó la cvar **`dump_shaders`** (REXCVAR ya existente en el SDK;
  `flags.cpp`, `translator.cpp:339`, `shader.cpp:122 DumpUcode`) apuntando a
  `%TEMP%\opencode\shaderdump`. Con `LoadUserSettings`/`rex::cvar::LoadConfig`
  cualquier cvar del `dbz3_user.toml` se aplica. Una pasada al select volcó
  **91 VS + 54 FS** (`shader_<HASH>.ucode.vert`, el desensamblado Xenos).
- **VS de skinning** = los que fetchean **Stride=11 (VB) y Stride=12 (paleta)**:
  6 ficheros (`2DD4268D7EE12280`, `9B2CDBD500DB8645`, `B4611D5E3A350359`,
  `D0D07B299C625324`, `DA9A3E04256563FF`, `F3AC1AA2FE3EA253`).
- Decodificación (subagente) — **LAYOUT DE LA PALETA (48 B/hueso)**:
  `[T.xyz][qA.x][qB.xyz][qA.y][qC.xyz][qA.z]` (3 vec4 entrelazados). `qA` es la
  rotación principal (3 componentes; `qA.w = sqrt(1-|qA|²)` reconstruida por el
  VS). `qB/qC` son rotaciones auxiliares para el blend intra-hueso.
  - Skinning: `model = R(qA)·pos + T` (con `weight=1.0`; rígido por hueso).
  - `pos` (offset 0) = **bone-local**; `bone` (offset 4 dw = byte 16) =
    **índice DIRECTO a la paleta** (`vf1 + bone*48`, sin remapeo); `weight`
    (offset 3) = blend **intra-hueso** (no 2º hueso); `normal` (offset 5)
    bone-local. Después `c0..c3` = mundo·vista·proy.
  - **No hay campo de escala** por hueso.
- Validado: con la paleta decodificada, el VB del NATIVO da un humanoide
  coherente (`dec_native.png`, bbox X[-10,9] Y[-6,17] Z[-8,12]).

### 🔴 BUG REAL encontrado (offline)
- La paleta capturada tiene entradas **NaN en los huesos 42-49** (y 85,86):
  el juego **no las define** (el AWG0 de la plantilla solo anima ciertos huesos).
- El **nativo NO las usa**; el **port SÍ usa 43-47 (la cola)** → `R·pos+T` = **NaN**
  → vértices/geometría volando → deformación/explosión.
- Remapeando esos huesos a uno válido ⇒ bbox finito; pero el render sigue
  **fragmentado** ⇒ **hay al menos otra causa** además del NaN.
- **Conclusión operativa**: el skin del port **no puede usar huesos cuya paleta la
  plantilla no define** (42-49). Hay que remapearlos (a la cadena de la cola HD o
  a un hueso válido) o clonar la cola HD.

### Ficheros / config de esta iteración
- `dbz3_user.toml`: añadida `dump_shaders = "…/opencode/shaderdump"` (QUITAR al
  terminar el RE).
- `%TEMP%\opencode\shaderdump\`: 91 `.ucode.vert` + `.d3d12.bin.vert` (DXBC) + `.frag`.
- Scripts nuevos: `%TEMP%\opencode\port3\decode_pal_repro.py` (decodifica la
  paleta y renderiza nativo/port), `find_world_pal.py`.
- **Próximo**: (1) remapear/clonar los huesos 42-49 del port; (2) investigar la
  fragmentación restante con la paleta ya decodificada (comparar `R(qA)·pos+T`
  del port vs nativo vértice a vértice).

## 16. ITERACIÓN 2026-09-13f — 🔴 CAUSA RAÍZ: NUESTRO `world` ES INCORRECTO

### La prueba
- Con la paleta decodificada (`decode_pal_repro.py`), el VB del **NATIVO** da un
  humanoide coherente; el del **PORT** da NaN (huesos 43-47 con paleta NaN) y, tras
  remapearlos, queda **fragmentado**.
- Pero al reconstruir el modelo del NATIVO con **NUESTRO `world`**
  (`world_ours[b]·pos`) el render sale **DESTROZADO** (espinas gigantes), con la
  triangulación correcta (lista, como la lee `port_b3_strip.py`). NO es artefacto.
- Comparando `T` de la paleta (origen del hueso según el JUEGO) con la traslación
  de **nuestro** `world`:
  - hueso 1 (CEL_WAIST): paleta `(0.92, 7.23, -0.11)` vs nuestro `world` `(0,0,0)`
    → **dif 7.28**
  - hueso 2 (CEL_LLEGROT): paleta `(0.19, 7.80, -0.72)` vs nuestro `(1.32,0.65,0)`
  - diferencias de 2.8–21.6 unidades en muchos huesos.

### Conclusión
- **`awo_tools/awg_vertex_buffer.py::bind_worlds()` NO da el bind real.** Los ejes
  del AWG (stride 80) tienen **traslaciones ~0** (quat + escala, pero pos=0 en casi
  todos los huesos) → la acumulación por padre produce traslaciones sin sentido.
- **El "render de bind" offline es SIEMPRE limpio** porque `world·inv(world)·model
  = model` (auto-consistente) → **NO valida `world`**. Por eso pasó desapercibido
  todo este tiempo.
- **El test rígido (`_strip4r1`) parecía perfecto** porque aplicar UN solo hueso a
  todo el modelo = una transformación global → el modelo sigue coherente aunque el
  frame sea erróneo. NO validaba el skin.
- 🔴 **Consecuencia**: `window_from_model` calcula `pos = inv(world_ours)·model` en
  un frame EQUIVOCADO → el shader del guest hace `R_anim·pos + T_anim` y cada
  pieza se va a su sitio mal → **deformación**. Esto afecta a **Vía B y también a
  Vía A** (el "reconocible pero deforme" de la inyección era esto).
- El skin PS2 (asignación de huesos) NO era el problema; el problema es el FRAME
  bone-local (`world`).

### Dónde está el bind real (pendiente)
- Los `T` de la paleta SÍ son los orígenes reales de los huesos (pose animada).
  En un frame de animación identidad, `(R,T)` = el BIND de ese hueso.
- Los ejes del AWG NO contienen las traslaciones. Buscar el bind en:
  (1) una convención alternativa de los ejes (pos en otro offset / sin acumular),
  (2) la tabla/zonas de `AWG0+0x1F80`/`+0x2000` (datos empaquetados no-4x4 claros),
  (3) derivarlo de la ANIMACIÓN en frame 0,
  (4) capturar la paleta con la animación forzada a identidad.
- **Script**: `%TEMP%\opencode\port3\decode_pal_repro.py`. Ficheros de prueba:
  `nat_world_LIST.png` (destrozado) vs `dec_native.png` (coherente).
- **Nota**: la paleta capturada tiene NaN en huesos 42-49; el port usa 43-47
  (cola) → hay que remapearlos/clonarlos igualmente.

### Convenciones de `world` probadas (2026-09-13f) — TODAS fallan
Render del NATIVO reconstruyendo el modelo con distintas convenciones (todas
DESTROZADAS, no es artefacto de triangulación — se usó la regla LISTA):
- `bind_worlds()` (quat acumulado + pos) → destrozado (`nat_world_LIST.png`).
- Solo rotación acumulada → destrozado (`nat_rot_only.png`).
- Quat propio sin acumular (world-space) → destrozado (`nat_own_quat.png`).
- Trasposición / inversa del quat propio → igual.
⇒ **los ejes del AWG NO bastan** para el bind (ni rotación ni traslación).
- La ÚNICA fuente fiable de los frames del juego es la **paleta capturada**
  (`R_anim, T_anim`), pero es la pose del select (NO bind).
- Vía pragmática candidata (SIGUIENTE): **transferir el `pos` bone-local del
  NATIVO** (no recalcular con `inv(world)`): el port usaría la skin del HD
  "pegada" (pos del vértice HD más cercano). Requiere una correspondencia en
  model-space fiable (el model-space actual de `parse_parts` también usa el
  `world` roto → hay que resolver la correspondencia aparte, p.ej. por UV/parte).
- **Pendiente de verdad**: encontrar el BIND real (frame de animación identidad,
  o RE del código que construye la paleta del guest).

### §17. INVESTIGACIÓN DEL BIND (sesión 2026-09-13g) — resultados duros
Se revisaron en profundidad las estructuras y todas las hipótesis alternativas.
Conclusiones (todas verificadas con scripts):

1. **El layout de la ventana es correcto** (NO es el bug): `pos@+0, w@+12,
   bone@+16 (valor en +19), nrm@+20, FFFFFFFF@+32, uv@+36`. Primeras ventanas de
   `e147`: `bone=21`, `pos=(2.80,0.89,0.34)`, `w=1.0`. Coincide con §3.4.9.
2. **Los huesos del AWG0 de la plantilla son 0-33** (34 únicos, 2948 verts),
   labels correctos: `0=XCEL_BODY 1=CEL_WAIST 2=CEL_LLEGROT 3=CEL_LLEG1 …`
   (coincide con `bone_labels()`). El `bone` del vértice indexa el ESQUELETO.
3. **La jerarquía de `bind_worlds()` es CORRECTA**: `+0x40` de la estructura de
   80 B del eje es un offset rel. `awg0` → `awg0+poff` = dirección del eje del
   PADRE (p.ej. bone2 `poff=0xd70` → `0x1a30` = eje de bone1). Confirmado.
   (Ojo: `0xd70` visto como offset de fichero cae en strings de labels — es
   coincidencia, no un pointer a label.)
4. **Ninguna convención de los ejes reconstruye el bind** (búsqueda exhaustiva:
   acumular sí/no × cuat. conjugado/sí × pos rotada/sí × orden padre·hijo/hijo·padre
   × quat xyzw/wxyz). Métrica = longitud de aristas de FRONTERA entre huesos
   (los "pinchos"): todas ≥3.0 para un personaje de ~13 u (un bind correcto daría
   ~0.1). Render del NATIVO con `world` → destrozado (`nat_world_view.png`);
   con `rot` solo, con quat propio, etc. → destrozado (`nat_own_quat*.png`).
5. **NO hay tabla de matrices bind** en el fichero (scan de 4×4/3×4 con rotación
   ortonormal → 0 candidatos). Los bloques 0x130 por hueso (AWO+0x30 →
   0x4f860+i*0x130) son tablas de índices, no transforms.
6. **La paleta (`fc=94`, 6144 B = 128 entradas × 48 B) es la ÚNICA fuente fiable**
   y es **IDÉNTICA port vs nativo**. Decodificada como skin matrix
   `[T.xyz][qA.x][qB.xyz][qA.y][qC.xyz][qA.z]`, `model = R(qA)·pos + T`. `P[0]` =
   identidad. `P·pos` = modelo coherente (`dec_native.png`).
   ⇒ \(P = M_{anim}·M_{bind}^{-1}\).
7. **La paleta NO está indexada por el índice de hueso del esqueleto en el orden
   de los ejes**: `P·world_axes` (hipótesis "ejes = pose select") NO da un
   esqueleto coherente (`skeleton_anim.png`, bbox ±14). Es decir,
   `palette[b] ≠ transform del hueso b`. **Falta decodificar el mapeo `bone→slot`
   de la paleta** (ya anotado en §15).
8. `M_bind = palette⁻¹·world` probado → frontera 10.2 (peor). Descartado.
9. **`pos` no es world/model-space**: el render de `pos` crudo también es una masa
   (`nat_rawpos.png`), compacta pero spiky ⇒ sí es bone-local, y necesita el bind
   correcto.

**Conclusión**: el bloqueo es doble y está localizado:
(a) **decodificar el mapeo `bone→slot` de la paleta** (por qué `P[b]` no es la
transform del hueso `b` aunque el shader indexe `vf1+bone*48`); y
(b) **obtener el BIND real** (`M_bind`). Con (a) resuelto, la paleta da
`M_anim·M_bind⁻¹` y podríamos **usar la propia paleta (capturada) como bind
bakeado**: generar `pos_port = P_ref[b]⁻¹·model_ps2` (frame de la paleta), que a
tiempo de referencia reproduce exactamente el modelo PS2 y anima por
`P_t·P_ref⁻¹`. Es la vía más corta a un port que RENDERICE.

**Siguiente experimento (barato, decisivo)**: volcar en el runtime, POR DRAW, la
paleta y el `bone` crudo de cada vértice, y **correlacionar** `bone → slot` usando
el hecho de que el nativo renderiza: para cada slot de la paleta, buscar qué
hueso (por sus vértices y `pos` conocidos) lo usa. Alternativa: instrumentar la
función PPC que construye la paleta (rellena el buffer `fc=94`) — es el RE que
cierra el tema de una vez.

**Herramientas de esta sesión** (`%TEMP%\opencode\port3\`): `nat_world_view.png`,
`nat_rawpos.png`, `skeleton_anim.png`, `bind_hyp_PinvAxes.png`, `nat_own_quat*.png`.
**Mod de entrega vigente**: `cell_best2` (Vía A).

### §18. 🔴 HALLAZGO GRANDE (2026-09-13g, 2ª parte): el `world` de los ejes SÍ es
### un esqueleto T-POSE perfecto — el problema es el mapeo de hueso del vértice
Renderizando SOLO los orígenes de `bind_worlds()` (hueso→padre como líneas):

- **`skel_worldT.png` = un esqueleto T-POSE PERFECTO** (cabeza arriba, brazos
  horizontales, columna, piernas abajo). ⇒ **la jerarquía y las traslaciones de
  `bind_worlds()` son CORRECTAS y `world` ES el bind (T-pose).** Antes lo dimos por
  roto: era un artefacto de que la malla de encima emborronaba la lectura.
- `skel_paletteT.png` (orígenes de la paleta) = figura de pie NO T-pose ⇒
  **la paleta = `M_anim` (pose select), NO el bind.**

**La contradicción**: si `world`=bind y `pos` es bone-local, `world·pos` debería dar
el modelo T-pose. NO lo da (`nat_world_STRIPTRUE.png`, triangulación STRIP correcta
`prim=6`, sigue spiky). Tampoco con `T+pos`, `R^T·pos+T`, etc.
⇒ **`pos` NO es `M_bind⁻¹·model` con el bind de `world`.** El eslabón que falta es
el **mapeo `índice de hueso del vértice → hueso del esqueleto** (σ), o una
orientación de bind de skin distinta de la del esqueleto de display.

**Datos a favor de σ ≠ identidad**: la métrica "distancia del vértice transformado
al origen del hueso" no elige un `w` único por cada `v` (empates por simetría
izq/der), consistente con un remapeo no trivial. El `bone` del vértice SÍ indexa
la paleta directamente (lo probó `decode_pal_repro.py`: `P[B]·pos` = coherente).

**Cómo atacarlo (barato y decisivo)**: como la paleta = `M_anim` y `world` = `M_bind`,
la skin matrix es `S[v] = paleta[v]·world[?]⁻¹`; resolviendo qué hueso del esqueleto
hace consistente `S` (o directamente: cuál `world[w]` hace `world[w]·pos_v` suave)
se obtiene σ. Implementar un solve por hueso con métrica de aristas de frontera
(no la distancia al origen, que empata).
**Alternativa definitiva**: instrumentar el runtime para volcar, por hueso, el
**buffer de inverse-bind** que usa el guest (y/o la matriz `M_bind` real).

### §19. 🔴🔴 INSTRUMENTACIÓN HECHA (2026-09-13h): **la paleta = `M_anim`**
Se instrumentó `rexgpu-xenos` (bloque en `ExecutePacketType3Draw`, caso `kDMA`,
gated por `dbz3_drawlog.on`) para volcar el IB, todos los vertex-fetch y una
**ventana de 32 KB de memoria guest** alrededor de cada buffer. Captura en el
Select (muchos personajes y trajes): `out\build\win-amd64-release\dbz3_draws.log`
+ `dbz3_bufs.bin`. **Instrumentación REVERTIDA y DLL limpia reinstalada**
(6165504 B, `command_processor.cpp` 1616 líneas, 0 refs `dbz3`).

Hallazgos (bytes **big-endian**):
1. El buffer de skin (fetch de 6144 B = 128×48) decodifica como paleta conocida:
   `P[1]=(0.922,7.225,-0.112)` (¡el waist de Cell F2!). **La COLUMNA T de la
   paleta es un esqueleto DE PIE** (cabeza y≈13, pelvis y≈7.2). ⇒ **la paleta NO
   es la skin matrix, es `M_anim`** (las world matrices animadas del hueso). El VS
   hace `model = M_anim·pos`; por tanto **`pos = M_bind⁻¹·model`** (bone-local).
2. **`M_bind ≠ ` nuestro `world` (ejes)**: los `M_anim` (paleta) y el bind comparten
   longitudes de hueso (rígido); comparando distancias padre-hijo, el bind real
   necesita p.ej. bone1 (waist) a **7.28** del root, pero nuestro `world[1]` =
   `(0,0,0)` (sin offset). En la cadena de la pierna la razón es ~1.32 (select vs
   bind, pose distinta), pero en la columna no cuadra ⇒ **el bind de los ejes está
   mal (falta el offset del root/waist y/o la acumulación difiere)**.
3. En la memoria volcada **no aparece un array denso de 48 matrices T-pose**
   (bind) con stride 48/64 y valores acotados — el guest probablemente **calcula
   `M_bind` al vuelo desde los ejes** y sólo persiste `M_anim` (paleta).

**Conclusión**: `M_bind` hay que obtenerlo del guest (los ejes con la convención
correcta) o capturando la paleta en el **frame del BIND (T-pose)**.
**Vía corta a un port que RENDERICE**: capturar `M_anim` en el estado T-pose
(bind) y bakearlo como `M_bind` ⇒ `pos_port = M_bind⁻¹·model_ps2` (a tiempo real
anima con `M_anim·M_bind⁻¹`). Es la línea a seguir.

### §19.b MATIZ IMPORTANTE (2026-09-13h): la paleta es la SKIN MATRIX, no `M_anim`
Al examinar dos paletas contiguas (`0x1BA72000` y `0x1BA76000`, ambas 6144 B =
128×48) se ve que sus columnas T difieren por frame: `A[1]=(0.922,7.225,-0.112)`
vs `B[1]=(0.148,6.524,-0.114)` ⇒ son DOS frames del MISMO personaje. Y la columna
T **no es un esqueleto consistente** (p.ej. cabeza y≈0.05 pero pecho y≈12.4;
revisión: `palette[8]=RLEGROT` a y≈10.5 — imposible como cadera). ⇒ la paleta es
la **skin matrix** `S = M_anim·M_bind⁻¹` (con `pos` bone-local), NO `M_anim`.
`A⁻¹·B` = pose relativa entre frames (valores pequeños, no esqueleto). Confirmado:
**el bloqueo es exactamente `M_bind`**, no decodificable de estas capturas (el
guest lo calcula al vuelo; no hay array de bind denso en memoria).
**Opciones**: (1) RE de la función PPC que construye la skin (`S`); (2) capturar
`S` en la pose BIND (T-pose) y usarla como `M_bind`; (3) aceptar Vía A como
entrega. La instrumentación quedó **REVERTIDA** y DLL limpia (6165504 B).

---

## §20. 🔒 CIERRE (2026-09-13i) — Vía B APARCADA; HD↔HD = entrega validada

Decisión del usuario: **opción 3** para Vía B (= aparcada "de momento", no
imposible), y **cerrar/pulir el swap nativo B3 HD ↔ B3 HD** (que sí está
validado y es la entrega real). Vía A queda como aproximación documentada.

### 20.1 Hallazgos de layout (IMPORTANTES, aplican a TODAS las herramientas)
Verificado sobre la plantilla `e147` (Cell F2) leyendo los 17 AWGs:
- **TODOS los AWGs (0-16) usan el MISMO layout de VERTEX VENTANA**:
  `pos.xyz@+0 | w@+12 | bone@+16 | nrm.xyz@+20 | 0xFFFFFFFF@+32 | uv.xy@+36`
  (stride 44). El marcador `FFFFFFFF` está en `+32` en **el 100% de los slots
  de los 17 AWGs**. La región es `[ib − g(0x2C), ib)` con `g(0x2C)/44` = nº exacto
  de slots (e147 AWG0: 2948; AWG1: 196; …).
- 🔴 **BUG histórico de `port_ps2_b3_inject.py`**: usaba `sec_real = AWG0 +
  g(0x34) + 2` con layout sec34 (`FFFF@+0`, `bone@+28`, `pos@+12`). Para e147,
  esa región **NO está alineada a 44** (`(ib−start)/44 = 2938.27`) y queda
  **desfasada 428 B (=10 slots)** respecto a la ventana real; el conteo
  `(g(0x2C)−g(0x34)−2)/44 = 2661` es incorrecto (real 2948). ⇒ la inyección
  escribía en slots desplazados.
- 🔴 **BUG histórico de `port_ps2_b3_inject_aux.py`**: usaba una tabla de **6
  "familias" de layout** (offsets distintos por AWG). Es **incorrecta**: los 16
  AWGs aux (manos/cara) usan el MISMO layout de ventana. El hueso host (23/30/32)
  sí era correcto.
- Correcciones hechas: `%TEMP%\opencode\phaseb\make_winbody.py` (cuerpo con layout
  de ventana) y `make_winaux.py` (16 AWGs aux con layout de ventana uniforme).
  Mods generados: `cell_winbody`, `cell_win2` (ambos probados en juego).

### 20.2 Resultado Vía A (aproximada) — PLATEAU
- La inyección mueve poco (mediana ~0, máx ≈0.8 u sobre un personaje de ~13).
- `cell_best2`, `cell_winbody` y `cell_win2` se ven **prácticamente igual** en
  juego: el fix de layout mejora torso/cintura, pero los fallos visibles
  (brazos, hombro, cabeza, cola) están en el **cuerpo (AWG0)** y son
  **intrínsecos a la inyección** (mueve vértices HD hacia la superficie PS2; no
  re-topologiza). El aux (manos/cara) cambió bytes pero es visualmente
  despreciable (PS2 ≈ HD).
- ⇒ Vía A NO supera al HD nativo. Para un modelo que exista en HD → **swap
  nativo**; para uno que NO exista en HD → Vía A aproximada (techo = plantilla).

### 20.3 Vía B (bind real) — aparcada
- Bloqueo único y localizado: **`M_bind`** (matriz de bind real del skin). La
  paleta = transform aplicada a `pos`; `pos` es bone-local; `world` de los ejes
  ≠ `M_bind` (longitudes de hueso mal). No hay inverse-bind explícita en el
  fichero; el guest la calcula al vuelo.
- Descartado el tooling de la comunidad (`budokai_111616.ms`, `pose_matrix.py`,
  GitHub `SamuelDBZMAAM/Budokai-Modding-Tool`): trabajan a nivel de "model part",
  no reconstruyen el rig completo.
- Vías para retomar: (1) RE de la función PPC que construye la skin
  (`sub_82087F58`); (2) capturar la paleta en el frame de BIND/T-pose.
- Evidencia/artefactos: `%TEMP%\opencode\port3\` (`nat_*`, `skel_worldT.png` =
  T-pose correcto del `world`, `skel_paletteT.png` = pose select),
  `%TEMP%\opencode\shaderdump\` (VS de skinning), capturas en
  `out\build\win-amd64-release\dbz3_bufs.bin`/`dbz3_draws.log`.

### 20.4 Entrega validada: swap nativo B3 HD ↔ B3 HD
- `mod center hd/swap_b3.py` (`--origen`/`--dest`/`--mod`/`--list`/`--verify`),
  catálogo `catalog_b3.cat` (183 entradas). Mid-insert virtual en el runtime
  cubre bins > `to_read`. Validado en juego (`cell_native`, Vegeta 424, Goten).
- **Implementado en el launcher** (pestaña "Cambio de modelo"): selector origen/
  destino, aviso `[NO JUGABLE]`, auto-activación del mod, log de salida, ruta
  AFS automática o manual. Pulido de cierre: eliminado log temporal
  `pipeline_cmd.log`; guardia origen==destino (`mod_pipeline.cpp`).
