# SESIÓN GPU / DRAW — Instrumentación del consumo del pool (2026-09-11)

> Objetivo: entender a nivel de **draw/GPU** por qué reordenar el pool dentro de un
> bloque deforma (T9) aunque el IB se remapee (identidad geométrica), y así
> desbloquear la **Vía B** (port PS2→HD con topología PS2).
> Estado: **instrumentación lista + primera captura analizada**. Falta identificar
> el layout exacto del vertex buffer y el `vfetch`.

---

## 1. INSTRUMENTACIÓN (SDK 0.10, rexgpu-xenos)

Editado `rexglue-sdk-0.10/src/graphics/command_processor.cpp` (bloque del draw
`VGT_DRAW_INITIATOR`, ~línea 1345):
- `#include <cstdio>` y `<string>`.
- Helper `rexglue_dbz3_path(name)` → ruta junto al **exe** (robusta ante CWD).
- Log **a fichero** `dbz3_draws.log` (junto a dbz3.exe), activado por:
  - env var `DBZ3_LOG_DRAWS=1`, **o**
  - marker file `dbz3_drawlog.on` (junto a dbz3.exe).
- Por **draw único** (máx 1500) escribe:
  - `DRAW prim=.. idx=.. src=.. isz=.. dma=.. nw=..`
  - `IB:` los **primeros 16 índices** del index buffer (leídos de memoria guest).
  - `VF[i] addr=.. size=.. endian=..` de cada **vertex fetch constant** con
    `type=kVertex && address && size` (base del vertex buffer), + primeros **16
    bytes** del buffer.

**Compilar** (baseline):
```
cmake --build "rexglue-sdk-0.10\out\build-win-vulkan-baseline" --target rexgpu-xenos
```
Salida canónica: `rexglue-sdk-0.10\out\win-amd64-baseline\rexgpu-xenos.dll`.
Copiada a `out\build\win-amd64-release\rexgpu-xenos.dll` y `...-dual\`.

⚠️ **PENDIENTE**: la DLL baseline canónica quedó **instrumentada** (6168064 B). Al
terminar la RE hay que **revertir el edit** de `command_processor.cpp` y
recompilar para restaurar la baseline limpia (para releases). El edit está
documentado aquí; no hace falta git (el SDK no está versionado).

---

## 2. CAPTURA

- Marker `dbz3_drawlog.on` puesto → juego lanzado → combate **Cell (slot 327 vía
  `cell_native`) vs Goku Traje 2** → cerrado.
- Log: `out\build\win-amd64-release\dbz3_draws.log` (233 KB, 4404 líneas,
  **318 draws**). Copia en `%TEMP%\opencode\phaseb\dbz3_draws.log`.

---

## 3. HALLAZGOS (con evidencia del log)

1. **TODOS los draws son indexados**: `src=0` (=`SourceSelect::kDMA`),
   `isz=0` (=int16), en los 318/318. **No hay auto-draws** (`kAutoIndex`).
   ⇒ El IB gobierna todo; no hay draw posicional sin índice.
2. **Los índices del IB son índices globales de un espacio unificado**: max
   observado **8490** (938 de 4960 muestreados > 2936, el pool del AWG0 de Cell).
   ⇒ Se indexa sobre **varios AWGs** (probablemente todo el AWO / un buffer
   unificado), no solo el AWG0.
3. **315 IB buffers distintos** (direcciones `dma`) para 318 draws.
4. **El vertex buffer NO es el `sec34` crudo**: p.ej. `VF[95] addr=1BD1A000
   size=32428 endian=2` (32428 words ≈ 129712 B ≈ tamaño del pool de Cell) pero
   sus primeros bytes son `40 33 80 32 3F 63 9E 49 3E AF 7B 46 3F 80 00 00`
   = **vec4 (x, y, z, 1.0)**. El `sec34` empieza con `FFFFFFFF` (marker) y u/v
   pequeños ⇒ **el guest construye un buffer de vértices derivado** (posiciones),
   o el shader hace `vfetch` a offsets (no distinguible con 16 B).
5. Los VF “grandes” (`000FE3FC size=4850437`, `00000280 size=5502592`,
   `002CE1DC size=4195584`) son de 4–5 M words ⇒ **no son buffers de modelo**
   (datos/constantes); los útiles son los de size 1500–70000.
6. Buffers candidatos de modelo (size en words): `1BD1A000 32428`,
   `1D6B8000 32428`, `1D110000 69432`, `1D16E000 100012`, `1BA6F000 27005`,
   `1BA73000 22869`, etc. (dos personajes: Cell y Goku).

### Interpretación
El guest **transforma/repacka** la geometría a un buffer GPU (vec4 de posiciones)
y dibuja con el IB global. Si el prepass escribe `derived[i] = f(pool[i])` sería
consistente con cualquier reordenamiento; **T9 deformó**, así que el prepass NO es
un simple por-índice (o el `vfetch` lee el pool con un offset/stride atado al
orden). **Esto es lo que falta fijar** (ver §4).

---

## 4. SIGUIENTES PASOS (masticados)

> Orden sugerido; los pasos 1–2 son offline (código SDK), 3–4 requieren una
> captura del usuario.

1. **Log del `vfetch` del shader** (decisivo). Instrumentar
   `rexglue-sdk-0.10/src/graphics/pipeline/shader/translator.cpp` (o
   `dxbc_translator_fetch.cpp`): al traducir, volcar por shader cada instrucción
   `kVertexFetch` con `{fetch_constant_index, offset, stride, format}` (posición,
   bone index, weight...). Sabremos:
   - si la posición sale de un buffer **16 B/vec4** (repack) o del `sec34` (44 B);
   - el `stride` y los **offsets** exactos;
   - de dónde se lee el **índice de hueso** (¿`sec34+28` o stream aparte?).
   Buscar el acceso a `register_file_.GetVertexFetch(...)` en
   `interpreter.cpp:927` como referencia.
2. **Log del `VGT_INDX_OFFSET`** (base vertex) en el draw: añadir al log del
   `command_processor.cpp` `regs.Get<reg::VGT_INDX_OFFSET>()` si existe ⇒ descarta
   un offset posicional de base.
3. **Identificar la dirección guest del pool**: (a) volcar la dirección de carga
   del `#AMB` (log en `afs.cpp` al servir el override), o (b) escanear la memoria
   guest buscando la firma del `sec34` (secuencia `FFFFFFFF` + u/v conocidos de
   `e147.bin`). Comparar con las `VF addr` ⇒ saber si el body se dibuja desde el
   pool o desde un derivado.
4. **Volcar 256+ B de los buffers candidatos** (no solo 16) y **comparar offline**
   con `e147.bin`/`e327.bin` para deducir stride/layout y el mapeo posición→slot.
   (Aumentar el `for (int bi=0; bi<16; ++bi)` del log.)
5. **Captura diferencial**: misma escena **normal** vs **T9** y comparar los
   buffers derivados/IB ⇒ ver exactamente qué cambia en el draw.

### Archivos/herramientas
- Instrumentación: `rexglue-sdk-0.10/src/graphics/command_processor.cpp` (~1345).
- Log: `out\build\win-amd64-release\dbz3_draws.log` (copia en temp).
- Bins de referencia: `%TEMP%\opencode\phaseb\e147.bin`, `e327.bin`.
- Herramientas Fase C: `awo_tools/phase_c_descriptors.py`, `phase_c_make_t9.py`,
  `awo_tools/awg_invariants.py`.

### Reproducir la captura
```powershell
# crear marker junto a dbz3.exe
New-Item -ItemType File -Force "out\build\win-amd64-release\dbz3_drawlog.on"
# lanzar el juego, entrar a un combate, salir
# leer el log
Get-Content "out\build\win-amd64-release\dbz3_draws.log"
```

---

## 5. ESTADO GLOBAL (para /compact)

- **HD→HD (swap nativo)**: ✅ DEFINITIVO y validado (Cell F2 → Krillin 327,
  `mod center hd/swap_b3.py`, mod `cell_native` ACTIVO). Documentado en
  `docs/07_ports/SESION_SWAP_NATIVO_2026-09-10.md`.
- **Vía A (inyección PS2→HD)**: funciona con limitaciones; sólo útil para
  personajes SIN modelo HD.
- **Vía B (topología PS2)**: BLOQUEADA por el consumidor posicional.
  - Fase C localizó el mapa (rangos de parte / A de descriptores 0x60 + arms;
    `AWG0+0x1F80` = matrices bind-pose): `SESION_FASE_C_CONSUMER_2026-09-10.md`.
  - T8 (mover partes enteras) = IDÉNTICO; T9 (reordenar runs dentro de bloque) =
    DEFORME. No hay tabla posición→hueso ⇒ dependencia GPU.
  - **Esta sesión**: instrumentado el draw; todos indexados; hay un buffer de
    vértices derivado; falta fijar layout/vfetch (§4).
- **Mods**: sólo `cell_native` activo. Logs de test retirados.
- **AVISO**: `rexgpu-xenos.dll` baseline está **instrumentada** (restaurar al
  cerrar la RE). El log/marker están desactivados sin `dbz3_drawlog.on`.

---

## 6. FASE 2 — LAYOUT DEL VERTEX FETCH Y DEL BUFFER (2026-09-11, 2ª captura)

> Se instrumentó además el **vfetch** (`translator.cpp` → `dbz3_vfetch.log`) y el
> **volcado binario completo** de los vertex buffers (`dbz3_vf.bin`). Captura:
> Cell (slot 327 vía `cell_native`) vs Goku traje 2.

### 6.1 VFETCH — layout de streams (verificado)
- **`fc=95`** = buffer de vértices del modelo/personaje, **stride=11 dwords = 44 B**
  (¡el mismo tamaño que el registro del `sec34`!). Offsets/formatos:
  ```
  +0   fmt 57 = 32_32_32_FLOAT   position.xyz
  +12  fmt 36 = 32_FLOAT         w (1.0)
  +16  fmt 6  = 8_8_8_8 (used=1) BONE (valor u32 = índice de hueso)
  +20  fmt 57 = 32_32_32_FLOAT   normal.xyz
  +32  fmt 6  = 8_8_8_8          (campo 4 B; suele FFFFFFFF)
  +36  fmt 37 = 32_32_FLOAT      uv.xy
  ```
- **`fc=94`** = **paleta de matrices de hueso**, **stride=12 dwords = 48 B**
  (3× vec4 = 3 filas de una 4×4), NO es un buffer de vértices.
- VFs “grandes” raros (`size` 4–5 M) = datos/constantes; ignorar.

### 6.2 EL GPU BUFFER ES COPIA VERBATIM DEL POOL, EN ORDEN
Volcado del buffer del body de Cell (`fc=95`, `size=32428` words = **2948 registros**
de 44 B) frente al `sec34` de `e147.bin`:
- **`buffer[i+10] == sec34[i]`** para **i=0..2660**: huesos **2661/2661** y
  posiciones **max diff = 0.00000** (0 registros con dif > 0.01).
- ⇒ El guest **reempaqueta el pool respetando el orden** (posición/normal
  copiadas exactas, hueso a 1 byte, `w=1.0`, marker en +32). **NO hay skinning
  en el buffer** (pose bind copiada literal).
- Marcadores `FFFFFFFF` en `+32` de cada registro (64/64 muestreados).

### 6.3 EL GUEST USA EL IB DEL FICHERO (verificado)
- El IB dibujado (leído de memoria guest) es un **substring CONTIGUO** del IB del
  fichero del personaje correcto: para Goku (e270) **coincidencia total**
  (444/444, 459/459, 162/162…). Antes comparé contra `e147` y no cuadraba por ser
  **otro personaje** (42 huesos, buffer `25916` = e270 `n_pool=2358`).
- ⇒ El guest **no regenera** el IB: dibuja sub-rangos del IB del fichero.

### 6.4 🔴 EL BUG: EL `sec` DEL TOOL ESTÁ ~9-10 REGISTROS TARDE
- El IB del fichero referencia **0..2947** (max=2947 → **2948 vértices**).
- `buffer[10+i]==sec34[i]` ⇒ en el espacio de índices del **IB**, el `sec34`
  empieza en **+10**, no en 0.
- Los ~10 registros anteriores a `sec` **existen en el fichero** (región
  15440..15866 en `e147`, junto a un tag descriptor `max 0 m…`) y el guest los
  incluye como `buffer[0..9]`. El IB **sí** referencia 0..9 (`e147`: 0→2, 1→5…).
- El tool (`phase_*` y `port_ps2_b3_*`) computa `sec=awg0+g(0x34)` y
  `n_pool=n_sec+n_vb2=2937` — **11 corto** y **desplazado ~10**. Cualquier remapeo
  del IB hecho en ese espacio de índices toca los registros equivocados.
- ⇒ **Hipótesis fuerte**: las deformaciones de T4/T9 (“consumidor posicional”)
  son **un bug de base de índices del tool**, NO una dependencia posicional del
  GPU. Coherente con: el GPU buffer es order-preserving y el IB es el del fichero
  ⇒ **un relabeling consistente DEBE ser identidad**.

### 6.5 POR QUÉ T2 (swap 2 verts) SALIÓ IDÉNTICO
`phase_b_make_t2.py` elige i,j de los **valores del IB** y toca los registros en
`sec+i*44` (base del tool). El desajuste de ~10 puede no manifestarse si i,j caen
en el mismo “bucket” o si el par apenas se usa. No invalida la hipótesis; falta
una confirmación in-game.

### 6.6 EXPERIMENTO DECISIVO PENDIENTE (T10)
1. Definir el **pool real en el espacio del IB**: empezar en el primer registro
   del run de markers que termina en `sec+2` (en `e147` = offset 15472; 9-10
   registros extra) y `n_pool = (vb2 - pool_start)//44 + n_vb2` (≈2948).
2. Aplicar una permutación (p.ej. **invertir runs dentro de un bloque A**) en ESE
   espacio y remapear el IB con la MISMA base de índices.
3. **Si es IDÉNTICO** ⇒ confirmado bug de tool ⇒ **Vía B desbloqueada** (el
   reconstruidor solo debe emitir un pool coherente con el IB).
   **Si DEFORMA** ⇒ sí hay consumidor posicional real (volver a GPU).
- Alternativa más simple para la 1ª confirmación: **intercambiar 2 registros
  enteros + remapear IB en la base correcta** (debe ser identidad trivial).

### 6.7 OTROS DATOS
- Buffers por personaje identificados en la captura: Cell=`32428` (2948 regs, 34
  huesos), Goku alt=`25916` (2356, 42), otro=`22869` (2079, 28); `1536` (fc=94) =
  paleta de matrices.
- Instrumentación añadida: `translator.cpp` (`rexglue_dbz3_vfetch_path` /
  `rexglue_dbz3_vfetch_enabled`, usa `rex::filesystem::GetExecutableFolder()`);
  `command_processor.cpp` volcado binario `dbz3_vf.bin` + `indxoff` en la línea DRAW.

---

## 7. ✅ EXPERIMENTO T10 — VÍA B DESBLOQUEADA (GEOMETRÍA) (2026-09-11)

### 7.1 Qué se hizo
- Tool nuevo `awo_tools/phase_c_make_t10.py`: **REVERSE completo del `sec34`**
  (permutación máxima) + remapeo del IB **en la base de índices del guest**
  (`IB'[k] = perm[IB[k]-10] + 10`; el histórico usaba base 0 = bug).
- Mod `_t10` (solo activo) con el bin comprimido LZX/2048 + padded a 118784.
  Validación offline del tool: 5418 entradas de IB remapeadas, **OK** (cada
  vértice resuelve al mismo registro).

### 7.2 Resultado EN JUEGO
- **La geometría del modelo es EXACTAMENTE IGUAL** al Cell normal (confirmado
  por el usuario). ⇒ **Un relabeling consistente ES identidad** ⇒ el
  “consumidor posicional” de T4/T9 era el **bug de base de índices del tool**.
  **Vía B desbloqueada para geometría** (posición/normal/hueso).
- **PERO las texturas se deforman** (solo UV; el modelo está bien).

### 7.3 Causa de lo de las texturas: SKEW de UV (+1 registro)
Verificado offline sobre el buffer normal (`dbz3_vf_fase2.bin`, buffer del body
`1B77F000`):
```
buf[i].uv == file_sec34[pos_index + 1].uv     1190/1190   (pos_index = i-10)
buf[i].uv == file_sec34[pos_index + 0].uv       63/1190
```
⇒ El guest **lee el UV del registro SIGUIENTE** (`pos_index+1`), no del propio.
El fichero almacena el UV **una fila por delante** de la posición (o el guest
aplica un skew de 1). Por eso mover registros enteros (T10) rompe la asociación
posición↔UV aunque la geometría sea idéntica.

**Implicación para el reconstruidor Vía B**: no se pueden tratar los registros de
44 B como unidades cerradas; el UV de la “fila lógica” k vive en el registro k+1.
Opciones:
1. Al permutar, **arrastrar el UV con el skew** (el UV de la fila k va al registro
   `destino(k)+1`, o permutar el campo UV por separado con el offset ±1).
2. O emitir la topología PS2 respetando ese desplazamiento de la tabla de UV.
3. (Vía A, inyección, NO toca el orden → inmune a esto.)

### 7.4 Conclusión
- **Geometría**: relabeling = identidad ⇒ el bloqueo histórico de Vía B era un
  **bug de índice del tool**, no una dependencia GPU. Se puede reordenar el pool
  (contando con la base +10).
- **Texturas**: pendiente resolver el **skew de UV (+1)** en el reconstruidor.
  Antes de atacar esto, merece la pena confirmar el skew con un T11 mínimo (mover
  una fila y su UV+1 juntos → debe ser identidad total, geometría + textura).

### 7.5 Ficheros
- Tool: `awo_tools/phase_c_make_t10.py`.
- Mod de test: `out/build/win-amd64-release/mods/_t10` (⚠️ ahora `.disabled`;
  `cell_native` restaurado como único activo).
- Datos: `%TEMP%\opencode\phaseb\e327_t10.bin`, `dbz3_vf_fase2.bin`,
  `dbz3_draws_fase2.log`.

---

## 8. ✅✅ VÍA B LISTA — MODELO DE "VENTANAS" + T11 (2026-09-11)

### 8.1 El GPU buffer es una copia VERBATIM de una región contigua
Comprobado byte a byte (`%TEMP%\...\basecmp.py`):
```
GPU buffer (fc=95, size 2948 ventanas) == file[15440 : 15440+129712]
                                          match 32307/32307 dwords
15440 = ib_abs - 2948*44 = vb0      (ib_abs = awg0 + g(0x30))
```
⇒ El guest **no reempaqueta**: copia (o enlaza) la región `[vb0, ib)` del AWO al
buffer del GPU. La rejilla `sec+2` del tool está **desalineada +428 B** respecto
a la rejilla de ventanas; de ahí el "skew UV" de §6-7.

### 8.2 Cada VENTANA de 44 B es autocontenida para el shader
```
+0   position.xyz  (3f)   fmt 57 = 32_32_32_FLOAT
+12  w             (f)    fmt 36 = 32_FLOAT
+16  bone          (u32)  fmt 6  = 8_8_8_8 (1 byte)
+20  normal.xyz    (3f)   fmt 57
+32  0xFFFFFFFF    (4B)   fmt 6  (no usado)
+36  uv.xy         (2f)   fmt 37 = 32_32_FLOAT
```
El IB (`g(0x30)`, int16 BE) referencia **directamente** índices de ventana
(0..N-1). El guest **usa el IB del fichero** (§6.3).

### 8.3 T11 = permutar VENTANAS + remapear IB ⇒ IDENTIDAD TOTAL
- `awo_tools/phase_c_make_t11.py`: reverse de las 2948 ventanas +
  `IB'=perm[IB]`. Validación offline OK.
- **En juego: PERFECTO, geometría Y texturas** (confirmado por el usuario). 
- ⇒ **Vía B desbloqueada por completo.** Cualquier relabeling consistente en la
  rejilla de ventanas es identidad (posición, normal, hueso, UV, textura).

### 8.4 Herramienta canónica
`awo_tools/awg_vertex_buffer.py` — modelo real del vertex buffer:
```
python awg_vertex_buffer.py info <bin>
python awg_vertex_buffer.py permute <in> <out> [--reverse | --swap I J]
python awg_vertex_buffer.py roundtrip <in> <out>
# API: AwgVertexBuffer.load(path).vertices / .indices / .emit(out, vertices, indices)
```
- `permute --reverse` == T11 byte a byte (validado en juego).
- `roundtrip` reproduce el bin original exacto.

### 8.5 RECETA VÍA B (port PS2 → B3 HD) — ya sin bloqueos de RE
1. Elegir **plantilla HD con el MISMO esqueleto** (nº/orden de huesos) y misma
   textura; se conservan ejes, arms, mesh-ref y descriptores.
2. Escribir las **N ventanas** (pos/normal/hueso/uv) de la geometría nueva
   (convertida a bone-local como en Vía A).
3. Escribir el **IB nuevo** (índices de ventana) = topología PS2.
4. Conservar descriptores/arms (no afectan al dibujo — T6), o reajustarlos.
- **Límite v1**: N fijo = capacidad de la plantilla. Menos vértices ⇒ rellenar
  ventanas. Más vértices ⇒ crecer la región (mid-insert interno) = pendiente v2.

### 8.6 Pipeline antiguo (SUPERADO)
`mod center hd/ports/port_ps2_b3_{geometry,draw,pack}.py` trabajaban con el
modelo **descriptores A/B + buffers sec34/vb2 separados**, que NO es como dibuja
el GPU. Deben reconstruirse sobre `awg_vertex_buffer.py` (ventanas + IB). Se
mantienen como referencia histórica.

---

## 9. ✅ SEMÁNTICA FIJA + VÍA B v2 IMPLEMENTADA (2026-09-12)

### 9.1 Orden de campos: NATURAL (x,y,z), NO (z,x,y)
El shader (vfetch) confirma `pos@0, w@12, bone@16, nrm@20, marker@32, uv@36`.
Comparando el HD `e147` con su equivalente PS2 (`cell_extract2.json`, mismo
modelo):

| | c0 | c1 | c2 |
|---|---|---|---|
| PS2 bone-local (inv(world)·model) | [-3.31, 6.21] | [-2.97, 5.03] | [-3.37, 2.33] |
| HD ventana pos | [-3.31, 6.21] | [-2.97, 10.44] | [-3.37, 2.33] |
| PS2 normal local | (0.48,0.48,0.55)… | | |
| HD ventana nrm | (0.47,0.50,0.53)… | | |

⇒ `ventana.pos = inv(world[bone])·model` y
`ventana.nrm = inv(world[bone]).R·model_nrm`, **ambas en orden natural
(x,y,z)**. Los `world` HD y PS2 son **idénticos** (max diff 5e-7).
El formato "C" era la rejilla `sec+2` desalineada +428 B; **el modelo de
ventanas es universal** (Goku 264 también da bone entero en +16).
⚠️ **La inyección Vía A permutaba** `(lc[2],lc[0],lc[1])` en `(+12,+16,+20)` →
por eso solo era "reconocible". El orden correcto es `lc` directo.

### 9.2 Herramienta canónica (ampliada)
`awo_tools/awg_vertex_buffer.py`:
- `info` / `permute` / `roundtrip` / `grow` / `selftest`.
- `bind_worlds()` (ejes 80B: quat+pos, parent +0x40), `bone_labels()` (AWG0+0x40,
  stride 32), `window_from_model(pos,nrm,bone,w,uv,invw)`.
- `grow(new_n, new_nib)`: amplía ventanas **e IB**, desplaza el tail y reajusta
  AMB (0x24/0x30/0x34), tabla AWG (rel. a awo) y cabecera AWO **[awo,awg0)
  (punteros absolutos, tabla por-hueso en +0x34/stride 0x20)**. vb0 constante.
  Tail idéntico +delta verificado. **EXPERIMENTAL** (quedan 2 punteros dudosos
  del modelo viejo sec34 en `[awg0,vb0)`; validar en juego).
- `selftest <bin>`: forward(ventana→modelo)∘inverse = original (≤1e-7).

### 9.3 Conversor Vía B
`mod center hd/ports/port_b3_windows.py <ps2_extract.json> <plantilla.bin> <out>`
`[--fit | --no-grow]`: mapea huesos PS2→HD por label, emite ventanas + IB
(lista de triángulos, prim=4) sobre la plantilla. `--fit` = cluster-decimate
para caber sin grow. Validado: reconstruir model-space desde el port == PS2
original (dist 0.0000).

### 9.4 Test en juego
- `cell_extract2.json` (PS2 Cell) → plantilla `e147.bin` (Cell F2 HD):
  - **fit**: 1907 verts / 2033 tris → mod **`cell_viab`** (entrada **147**), sin
    growth. **← test principal.**
  - **grow**: 5148 verts / 8724 IB → mod `cell_viab_grow` (deshabilitado).
- Compresión LZX `/N:2048`, pad a ceil(comp/0x1000)*0x1000, round-trip
  verificado. `cell_native` (327) sigue activo (no colisiona: otra entrada).

### 9.5 ❌ RESULTADO EN JUEGO (2026-09-12) — Vía B NO renderiza
**Corrección importante**: lo que parecía "Cell perfecto" era el **swap nativo
`cell_native`** (bin HD nativo en el slot 327), **no** el port. En ninguna sesión
logueada hubo un `AFS OVERRIDE HIT` de `cell_native`/`cell_viab` (el logging AFS
no captura `data_cmn` por una ruta/caché distinta, así que la ausencia de log no
prueba nada; pero el render sí se ve).

El port (`cell_viab`/`_grow327`, PS2 Cell → e147) **explota en pantalla**:
- Geometría **exacta**: las 5148 ventanas reconstruyen el modelo PS2 (dist 0.0000).
- **Llega al GPU**: ventanas `win0..win3000` halladas contiguas en `dbz3_vf.bin`.
- Tras el fix de `AWG0+0x2C`, el fetch cubre las 5148 ventanas (56628 dwords), y
  el guest dibuja el cuerpo con `prim=4` (lista) en ~11 draws troceadas.
- Aun así el modelo sale **despedazado** (ver §10).

**Causa**: el guest **trocea el IB por los descriptores/rangos de parte A/B de la
PLANTILLA**; como el port cambia la topología, esos rangos no casan → conectividad
mal. Transferir el **skin HD** (`--hd-skin`) **empeora** (no es el rigging el
problema principal). ⇒ Falta **reconstruir los rangos A/B y los mesh-refs**.
Detalle completo: `SESION_VIA_B_RENDER_2026-09-12.md`.

> El `grow` **sí** queda arreglado mecánicamente (2 bugs, ver §10), pero Vía B
> sigue **sin validar**. La prueba *definitiva* (port de un modelo ausente en HD)
> queda bloqueada hasta resolver el render.
