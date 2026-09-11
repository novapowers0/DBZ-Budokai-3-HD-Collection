# SESIÓN FASE C — El consumidor posicional del pool (2026-09-10)

> Objetivo: localizar la vía que consume el pool de vértices **por posición**
> (bloqueo de la Vía B: "reordenar el pool deforma aunque el IB sea consistente").
> Resultado: **cartografiada**. El pool está **100% particionado en rangos por
> parte** (sin solapes), declarados en los **descriptores 0x60** (campo A) y en
> los **descriptores de parte de los arms**. Instrumentos en `awo_tools/phase_c_*`.

---

## 0. TL;DR

- **A del descriptor 0x60 = rango de vértices `(start,count)` del pool**, no del
  IB. Los 29 descriptores "max N m" + los 7 descriptores de parte (arms) **teselan
  el pool entero** `[0, n_pool)` con **0 solapes** y **0 huecos reales** (los
  huecos de una tabla los cubre la otra).
- **B del descriptor = rango del IB**; los índices del IB son **globales**
  (`min == A_start`, `max == A_start+A_count-1`: cubren todo A).
- Los **arms** (`[bone, p1, 0, p2, 0]`): `p1` → **descriptor de parte** (nombre +
  rango de vértices en +0x38/+0x3C); `p2` → **matriz bind-pose**.
- La "2ª tabla @AWG0+0x1F80" **NO es una tabla de descriptores**: es la **tabla de
  matrices bind-pose** (a la que apuntan los `p2`). Corrige la hipótesis previa.
- ⇒ Para la Vía B hay que reconstruir **pool + A + IB(B) + descriptores de parte**;
  ahora se sabe exactamente dónde están.

---

## 1. MÉTODO (herramientas nuevas)

| Herramienta | Función |
|---|---|
| `awo_tools/phase_c_arms_targets.py` | Vuelca los destinos de los arms (`p1`/`p2`) y busca `(start,count)`. |
| `awo_tools/phase_c_meshgroup.py` | Mapa del mesh-group + scan de partes + `--region OFF LEN`. |
| `awo_tools/phase_c_descriptors.py` | Mapa de **todas** las tablas de descriptores 0x60 (todos los AWGs) y de los arms→parte. |

Bin de trabajo: `e147.bin` (Cell F2, 715872 B, AWG0=0xCC0, n_pool=2937, n_ib=6302).

---

## 2. FORMATO DEL DESCRIPTOR 0x60 (verificado)

```
+0x00  char  label[]        p.ej. "XCEL_BODY", "CEL_L00_LHAND", "XCEL_L00_FACE"
+0x18  char  "max N m"      tag (census lo usa para localizarlo)
+0x44  u32   type == 0x2C00 (los válidos; 0x8000003F/0x80000000 = centinelas)
+0x50  u32   A_start << 8   ─┐ rango de VÉRTICES (start,count) del pool
+0x54  u32   A_count << 8   ─┘
+0x58  u32   B_start << 8   ─┐ rango de ÍNDICES del IB
+0x5C  u32   B_count << 8   ─┘  (bit 0 = flag)
```
> ⚠️ Corrección a `AGENTS §3.3`: **A NO es `[min(B),max(B)+1)`**; A es el rango de
> vértices del pool `[A_start, A_start+A_count)`.

### 2.1 Cobertura (Cell F2, AWG0)
- 36 descriptores "max N m"; 29 con A válido → 2441/2937 vértices.
- Los **6 huecos** de esa tabla son exactamente las partes de los arms:
  `[0,56)`, `[2176,2292)`, `[2399,2515)`, `[2622,2655)`, `[2680,2822)`, `[2904,2937)`.
- **Unión (descriptores ∪ arms) = `[0,2937)` sin solapes.** El pool es una
  concatenación de partes.

---

## 3. LOS ARMS (verificado)

Sólo **7 huesos** del AWG0 tienen arm con datos: **0, 23, 30, 36, 38, 40, 47**.

```
arm = [bone, p1, 0, p2, 0]
p1 -> descriptor de parte:
     +0x00 id        +0x38 start      +0x3C count      +0x44 data_off
     +0x10 vec4      +0x28 0x1158     +0x2C 0x2C(=44)  +0x60 "max N m"
     +0x48 char[] label
p2 -> matriz bind-pose (en la tabla de matrices @AWG0+0x1F80)
```

| hueso | parte (label) | rango de vértices |
|---:|---|---|
| 0 | XCEL_BODY | [0,56) |
| 23 | CEL_L00_LHAND | [2176,2292) |
| 30 | CEL_L00_RHAND | [2399,2515) |
| 36 | XCEL_M_DTEETH | [2622,2655) |
| 38 | XCEL_M_UTEETH | [2680,2713) |
| 40 | XCEL_L00_FACE | [2713,2822) |
| 47 | CEL_T_TAIL6 | [2904,2948) |

⇒ **La "estructura de skinning/arms atada al orden del pool"** (histórico) es, en
realidad, **la partición del pool en rangos de parte**: éste es el consumidor
posicional. Los arms sólo aportan el puntero a la parte y su matriz.

---

## 4. LA "SEGUNDA TABLA @AWG0+0x1F80" (corrección)

`AWG0+0x1F80` (abs 0x2C40 en Cell) es una **tabla de matrices bind-pose**:
- cabecera corta (0,0,0,0, 0x2C, 0x2D, 0x2E, 0x2F…) y luego **matrices 4×4
  consecutivas** (`… 3F800000`) de 64 B.
- Los `p2` de los arms apuntan aquí (p.ej. bone0 p2=0x1FE0 → matriz en 0x2CA0;
  bone23 p2=0x2020 → 0x2CE0; +0x40 B por hueso… en realidad +64 B por matriz).
⇒ **NO es la "tabla runtime de descriptores"**: era una hipótesis errónea de la
Fase B. La tabla de descriptores que sí importa es la de 0x60 (del mesh-group).

---

## 5. IMPLICACIÓN PARA LA VÍA B (port completo PS2→HD)

El bloqueo deja de ser una incógnita. **Regla empírica (T8)**: el pool es una
**secuencia de runs de hueso contiguos**; el orden global de las partes/runs es
libre, pero **cada hueso debe formar un run contiguo** (y su orden interno puede
permutarse dentro del run: T2). T4/T3 deformaban por **mezclar runs** dentro de un
bloque.

### 5.1 Invariante que debe cumplir el reconstruidor
`awo_tools/awg_invariants.py` comprueba en un bin real:
1. las partes (A de descriptores 0x60 + rangos de los arms) teselan el pool;
2. **los vértices se agrupan por hueso en runs contiguos** (el chequeo de
   homogeneidad por parte demuestra que las partes NO son la unidad: Krillin
   14/18 y Cell 28/35 partes mezclan huesos ⇒ la unidad son los **runs**);
3. los índices del IB de cada B caen dentro de su A.

### 5.2 Reconstrucción correcta
1. Parsear el modelo PS2 → superficies por hueso.
2. **Ordenar el pool por runs de hueso** (todos los vértices de un hueso
   contiguos; orden de huesos libre).
3. Asignar a cada descriptor/parte un rango A **que respete los límites de run**
   (puede abarcar varios runs completos, pero no cortar uno).
4. **Reconstruir el IB** (strip, no lista) y reescribir B.
5. Cerrar campos auxiliares del descriptor (type `0x2C00`, tag `max N m`, vec4
   `+0x10`, const `0x1158`) y las matrices bind-pose/arms del rig PS2.
6. LZX `/N:2048` + override por entrada (1 mod activo).

### 5.3 BUGS LOCALIZADOS en el pipeline Vía B actual (`mod center hd/ports/`)
- `port_ps2_b3_geometry.py::build_buffers`: asigna índices globales por orden de
  part, pero **con vértices compartidos entre parts los índices no quedan
  contiguos**; además **NO garantiza runs de hueso contiguos** (éste es el fallo
  del "amorfo"). `A=[g_first, n_unique]` es incorrecto si hay compartidos.
- `port_ps2_b3_geometry.py`: construye el IB como **lista de triángulos**
  (`ib.append` por vértice), pero el cuerpo HD usa **triangle STRIP**
  (§3.2) → revisar el `prim_type` del template.
- `port_ps2_b3_pack.py`: conserva arms/mesh-ref de la plantilla (requiere
  esqueleto 1:1) y no actualiza los **descriptores de parte de los arms**.
- `port_ps2_b3_draw.py`: fusiona grupos y recomputa `A=[min,max]` (rompe la
  contigüidad). Correcto: `A` = rango contiguo del run/parte.

### 5.4 Incógnita abierta
Con A teselando el pool y el IB global, un reordenamiento **consistente** debería
ser identidad geométrica; T8 lo confirma para partes enteras. El mecanismo exacto
del consumidor (¿asigna matrices por run?) es inferido, no observado; no bloquea
porque la regla de runs ya se cumple por construcción.

---

## 6. TEST T8 — ✅ RESULTADO: IDÉNTICO (parte entera permutable)

**Herramienta**: `awo_tools/phase_c_make_t8.py <in.bin> <out.bin>` — motor
reutilizable para la Vía B: detecta las partes (rangos A de descriptores 0x60 +
rangos de los descriptores de parte de los arms) e **intercambia dos partes de
igual tamaño** (bloques completos del pool), actualizando sus rangos y remapeando
el IB. Es una **identidad geométrica** (validado offline: 5125 triángulos → mismos
registros).

**Mod probado**: `mods/_t8_partswap` (entrada 327 = Krillin; intercambia las dos
manos, 187 v cada una). El runtime ordena los mods alfabéticamente (`afs.cpp`
`std::sort`) ⇒ `_t8` gana el override.

**✅ Resultado (usuario): IDÉNTICO a Krillin normal.**

### 6.1 CONCLUSIÓN — la regla de la Vía B

- **Mover/pemutar PARTES enteras es SEGURO** (identidad).
- **Revertir DENTRO de un bloque (T4) deforma**: mezcla los *runs* de hueso.
- **T2** (swap 2 vértices del MISMO hueso) era idéntico: dentro de un run de hueso
  el orden es libre.
- **T7** (IB invertido) deforma: el IB gobierna la conectividad.

⇒ **Modelo correcto**: el pool es una secuencia de **runs de hueso contiguos**; el
orden GLOBAL de las partes/runs es libre, pero **cada run de hueso debe permanecer
contiguo y en su orden interno**. Ése es el "consumidor posicional".
⇒ **La Vía B es ahora un problema de INGENIERÍA** (emitir pool/A/B/IB coherentes
desde el PS2), no un misterio. T4 fallaba por mezclar runs; T6 (A de metadatos)
no afectaba porque A no es el índice de dibujo sino la descripción del run.

### 6.2 Test T9 — ✅ RESULTADO: DEFORME (localizado)

`awo_tools/phase_c_make_t9.py` reordena el orden de los **runs mono-hueso dentro
de un bloque A** (manteniendo cada run intacto) + remapea el IB (identidad
geométrica validada). Mod `mods/_t9_runs` (entrada 327 = Krillin; bloque
`[377,887)`, 75 runs).

**Resultado (usuario): DEFORMIDAD en cara/cabeza y un brazo, no severa.**

### 6.3 CONCLUSIÓN REVISADA (el run NO es suficiente)

- **T8** (mover partes enteras) = **identidad**.
- **T9** (reordenar runs dentro de un bloque) = **deforme**.
- **T2** (swap 2 vértices del MISMO hueso) = identidad.
- **T4** (reverse dentro de bloque) = deforme.
- **T7** (IB invertido) = deforme; **T6** (A metadatos) = normal.

⇒ El `+28` del vértice **se usa** (test bone0: bones→0 colapsa), pero el
**orden intra-bloque también importa**. Se buscó una **tabla posición→hueso**
(`phase_c_find_bonemap.py`, u8/u16/u32) en el AWG y **NO existe**. ⇒ La
dependencia **no es un dato en el bin**: está en el **fetch/draw (GPU)** o en la
interpretación del IB/strip a nivel de pipeline. **No es observable offline.**
⇒ La Vía B NO se puede reconstruir por ingeniería a ciegas: hace falta **RE del
draw a nivel GPU** (instrumentar el vertex fetch / el bucle de draw).

**Estado**: `mods/_t9_runs` retirado (vuelve `cell_native`). La entrega sólida
sigue siendo el **swap nativo HD→HD**; la Vía B queda como investigación abierta
con el siguiente paso definido (instrumentación GPU).

---

## 7. REPRODUCIR

```
python awo_tools/phase_c_descriptors.py  <bin>          # todas las tablas 0x60
python awo_tools/phase_c_arms_targets.py <bin> 16       # arms p1/p2
python awo_tools/phase_c_meshgroup.py    <bin>          # mesh-group + partes
python awo_tools/phase_c_meshgroup.py    <bin> --region 0x1F80 0x100   # matrices
```

---

## 8. ESTADO Y SIGUIENTE

- **Vía A** (inyección): sin cambios (entrega para PS2-only).
- **Vía B**: **desbloqueada a nivel de mapa** (se sabe qué reconstruir). Siguiente:
  (a) cerrar los campos auxiliares del descriptor; (b) T8 para el mecanismo;
  (c) escribir el reconstruidor pool/A/IB desde el modelo PS2.
- **Swap nativo HD→HD**: sigue siendo la vía de entrega (perfecta) para
  personajes que ya existen en HD.

### Referencias
- `AGENTS.md §3.4` (Vía A/B, tests T2–T7).
- `docs/07_ports/SESION_FASE_B_ARMS_2026-09-10.md` (tests T2–T7, arms).
- `docs/07_ports/PLAN_PS2_B3/PLAN.md` (plan) y `04_FORMATO_RE.md`.
- `docs/07_ports/SESION_SWAP_NATIVO_2026-09-10.md` (swap nativo).
