# Sesión Fase B — arms, census y test T2 (2026-09-10)

> Objetivo: cerrar (o confirmar) el bloqueo de la Vía B del port PS2→B3 HD
> ("el orden del pool importa" / "consumer oculto por índice"). Herramientas
> nuevas, census de dominios y test de permutación T2 en juego.

---

## 1. CONTEXTO Y PREGUNTA

La Vía B (port completo, topología PS2) estaba bloqueada por dos creencias
heredadas de `SESION_PORT_RE_2026-08-26 §7.1`:
1. "El guest está ATADO al orden del pool" (test reverse que deformó).
2. Los "arms" definen rangos del IB a dibujar (`CONSOLIDADO §13.5.13`).

Y una duda: ¿existe un **consumer oculto** que referencia el pool de vértices
por índice (fuera del IB y de los descriptores A/B)?

## 2. INSTRUMENTOS (en `awo_tools/`)

| Herramienta | Función |
|---|---|
| `phase_b_census.py` | Parsea #AMB→#AWO→AWG0; header canónico; descriptores A/B; arms; census. |
| `phase_b_consumer_scan.py` | Cuenta referencias a índices de vértice FUERA del IB (todo el AWG0). |
| `phase_b_make_t2.py` | Permuta 2 vértices + remapea el IB + valida offline. |
| `afs_extract_hd.py` | Extrae una entrada del AFS HD (tabla en offset 8). |

Bins de trabajo (extraídos de `us/data_cmn.afs`, LZX→`xbdecompress`):
Krillin 327 (682528), Cell F2 147 (715872), Babidi 96 (383904).

## 3. HALLAZGOS

### 3.1 Mapa AWG canónico (verificado empíricamente)
`+0x14` = axes (== rigging_data_ptr). Offsets correctos:
`vb2=+0x2C, IB=+0x30, sec34=+0x34, end=+0x38` (align +2 del sec34).
⚠️ `docs/03_formatos/BIN_LAYOUT.md` y `AMO_AWO.md` están **MAL** (dicen
sec34=+0x30, IB=+0x38); el pipeline y `CONSOLIDADO` son correctos.

### 3.2 Descriptor A/B — confirmado
Los índices del IB en rango B caen SIEMPRE dentro del rango A:
Krillin 13/13, Cell 29/29, Babidi 10/10 OK.

### 3.3 Arms — REFUTADO "arms = rangos del IB"
Dump de los destinos de cada arm:
- cada arm es `[bone, ptr, 0, ptr_matriz, 0]`;
- `arm+12` → **datos float (matriz 4×4, termina en `3F800000`)** y crece
  **exactamente +64 B** por hueso (bloque "Mesh End" de 64 B);
- `arm+4` → arrays pequeños.
→ Son un **armazón/armature con punteros**, no rangos de dibujo. Por eso
`port_ps2_to_b3.py`, que los regeneraba como rangos, **crasheaba** (punteros
corruptos), no por un límite del formato.

### 3.4 Census: NO hay consumer oculto
Nº de índices de vértice referenciados **solo por el IB** (sin ninguna
referencia u16/u32 en el resto del AWG0):

| Bin | n (sec34+vb2) | solo-IB |
|---|---:|---:|
| Krillin | 2182 | **1866** |
| Cell F2 | 2937 | **2356** |

Los que tienen referencias externas son valores pequeños (0,1,2,5,11,13…)
que coinciden con índices de hueso y constantes; no forman listas.

### 3.5 Test T2 en juego — IDÉNTICO ✅
`_t2_swap`: 2 vértices del MISMO hueso/descriptor intercambiados + IB
remapeado (validado offline: geometría idéntica por construcción, ambos índices
sin referencia externa). **Resultado en juego (usuario): renderiza IDÉNTICO a
Krillin normal.** ⇒ el orden DENTRO de un descriptor/hueso es libre.

Además se confirmó el **formato C de Babidi**: marker != FFFFFFFF, sin align
+2, bone NO en +28 (va en +40) → el pipeline debe autodetectar formato A/C.

### 3.6 Test T3 en juego — DEFORME ❌ (pero revela el mecanismo)
`_t3_reverse`: reverse del pool sec34 + **IB remapeado** + **A/B recomputados**
(13/13, validado offline). **Resultado en juego (usuario): "se comprende la
silueta y algunas cosas permanecen en su sitio, pero deforme".**

⇒ El "reverse limpio" (con invariantes correctas) **SÍ deforma**. La lección
previa "no hay binding / el reverse era un artefacto" es **INCOMPLETA**: el
orden importa **cuando los vértices cruzan descriptores**.

### 3.7 Análisis de descriptores — la clave
Volcado de A/B vs índices de B (Krillin/Cell):

| Hecho | Evidencia |
|---|---|
| Los rangos A **particionan el pool de forma CONTIGUA** | Krillin: A=42→102→211→353→377→887→1080→…; cada `A_start` = anterior `A_start+A_count` |
| A ≈ `[min(B), max(B+1))` | Krillin 11/13, Cell 20/29 exactos |
| El pool **NO** es contiguo por hueso | 412 bloques de hueso (fragmentado) |
| Los descriptores faciales/manos tienen A en **vb2** (bones-in-A vacío) | XCEL_L00_FACE, XKLL_M_DTEETH |

### 3.8 Hipótesis H3 (la que reconcilia T2 vs T3)
**El guest trata el rango `A=[A_start, A_start+A_count)` de cada descriptor como
un BLOQUE CONTIGUO de pool.** El orden DENTRO del bloque es libre (T2), pero
mezclar vértices entre bloques rompe la partición → deforme (T3).

Mecanismo probable: por descriptor, el guest volca/skinnea el bloque A y dibuja
su rango B con **índices locales al bloque** (o una paleta de huesos derivada del
bloque). Al recomputar A como `[minB,maxB+1)` tras el reverse, el bloque de un
descriptor pasa a contener vértices de otros → deforme.

### 3.9 Test T4 en juego — MISMA DEFORMIDAD que T3 ❌❌ (hallazgo duro)
`_t4_inpart`: invertir los vértices **DENTRO** de cada bloque A (12 bloques,
1512 verts) + IB remapeado, **A intacto**. **Resultado (usuario): "las mismas
exactas deformidades que antes"** (idénticas a T3).

**Prueba matemática (offline)**: siguiendo el IB índice a índice, los registros
de vértice son **idénticos** en T2, T3 y T4:
```
IB-follow  t2: same=5125 diff=0
IB-follow  t3: same=5125 diff=0
IB-follow  t4: same=5125 diff=0
```
Es decir, **cualquier consumidor que resuelva por el IB ve exactamente la misma
geometría**. Un relabeling consistente del pool + rmepaeo del IB es una
IDENTIDAD geométrica (por eso T2 es idéntico). **Si T3/T4 deforman, el guest NO
resuelve la geometría (solo) por el IB: existe un consumo POR POSICIÓN.**

**Descartado**: (a) el mod SÍ se cargó (logs: `dbz3_010`=_t2_swap,
`dbz3_011`=_t3_reverse, `dbz3_012`=_t4_inpart, todos `AFS OVERRIDE HIT` en la
entrada 327 en el momento del combate); (b) T3 y T4 son bins distintos (68.989
diffs entre sí); (c) no es bug de remapeo del IB (validado por IB-follow).
(d) **H3 insuficiente**: cada bloque A contiene **2–85 runs de hueso** (no es
"un bloque = un hueso"), así que "orden dentro del bloque libre" es FALSO.

### 3.10 Test T5 en juego — MUCHO PEOR (IB sí importa) ✅
`_t5_noremap`: invertir el pool sec34 **sin tocar el IB**. **Resultado (usuario):
"mucho más deformidad; una de las texturas (¿la cara?) se extendió por gran
parte del cuerpo".**

⇒ **T5 ≫ peor que T4** ⇒ el **IB SÍ controla la geometría** (remapearlo en T4
mejoró las cosas). Pero T4 **sigue** deformando ⇒ hay una **segunda vía
POSICIONAL** además del IB. La textura de cara extendida = triángulos del cuerpo
tomando vértices de la zona de la cara (el IB apunta a índices cuyo registro ya
no es el correcto; en T5 el sec34 contiene la cara, descriptor 2 huesos
28-35/A=[211,353)).

### 3.11 Análisis A vs B (offline)
`phase_b_ab_compare.py`: triangulación de A (como lista y como strip) vs el rango
B del IB (como strip). Solape parcial 45–78% (no idénticos), IB poco secuencial
(26–89% de pares ±1). No concluyente por sí solo.

### 3.12 Test T6 en juego — NORMAL ✅ (A NO se usa para dibujar)
`_t6_adesc`: pool e IB **intactos**, solo se **rotan los rangos A** entre los 12
descriptores sec34. **Resultado (usuario): "se ve bien y está igual que siempre".**

⇒ **El rango A NO determina la geometría dibujada.** No es la vía posicional.

### 3.13 Estado del puzzle (2026-09-10)
| Test | Cambio | Render |
|---|---|---|
| T2 | swap 2 records (mismo bloque) + IB remap | idéntico |
| T3 | reverse pool + IB remap + A/B recomputados | deforme |
| T4 | reverse intra-bloque A + IB remap | deforme |
| T5 | reverse pool, IB SIN tocar | MUCHO peor (cara extendida) |
| T6 | solo rotar rangos A (pool+IB intactos) | **normal** |
| IB-follow | T2/T3/T4 | same=5125 diff=0 |

**Lectura**: el pool intacto ⇒ normal (T6). El pool cambiado ⇒ deforme (T4/T5),
aunque el IB sea consistente (T3/T4) ⇒ **el pool se consume POSICIONALMENTE**
(y esa vía NO es el descriptor A). El IB podría ser auxiliar; **T5≫T4 pudo ser
solo una permutación peor**, no prueba de que el IB importe.

**Test decisivo T7** (`_t7_ibrev`): pool intacto, **IB invertido entero**.
- Normal → el IB **no** se usa para dibujar ⇒ **dibujo POSICIONAL** ⇒ el orden del
  pool es sagrado ⇒ **Vía B (reordenar) incompatible; Vía A = única vía**.
- Deforme → el IB sí se usa ⇒ revisar el remapeo (T4 debió ser idéntico).

### 3.14 Test T7 en juego — DEFORMIDAD MASIVA ✅ (IB SÍ se usa)
`_t7_ibrev`: pool intacto, **IB entero invertido**. **Resultado (usuario): "mucho
más masiva, cabeza completamente deforme, solo una mano se ve bien, silueta de
piernas/torso presente pero MAL".** Imagen: malla con triángulos conectando
vértices equivocados.

⇒ **El IB SÍ gobierna la conectividad.** Pero entonces T4 (IB remapeado
consistentemente + pool reordenado) debería ser idéntico… y no lo fue.
⇒ **El pool se consume POSICIONALMENTE por una estructura que NO es el descriptor
A** (T6). Histórico (`SESION_INYECCION_2026-08-26.md`): el **draw log**
(`DBZ3_DRAW`) probó que el guest dibuja los strips correctos (B_start/B_count) y el
amorfo del port venía de la **estructura de SKINNING (arms) atada al orden del
pool**. Eso encaja con TODO: T2 (swap intra-hueso) OK; T4 (reordena cruzando
huesos) deforme; T6 (A) normal; T7 (IB) deforme.

### 3.15 🔴 HALLAZGO: hay DOS tablas de descriptores
- **Mesh group @AWG0+~0x2D49**: entradas 0x60 con label + `max N m` + A/B
  codificados `<<8` (+0x50/+0x54/+0x58/+0x5C, flag 0x01). Es la que editamos en T6.
- **AWG0+0x1F80**: una SEGUNDA tabla (u32 en PLANO) con los MISMOS valores:
  `42,60,74,126`… (+0x1158, +0x2C const). Probablemente la **tabla runtime** que el
  guest consume en draw ⇒ **T6 (que solo tocó la 1ª) no tuvo efecto**.
⇒ Pendiente: confirmar qué tabla usa el draw y si referencia el pool por posición.

## 4. CONCLUSIONES (revisadas 5ª vez)

1. **El IB se usa** (T7 masivo) **y el pool debe estar en su orden original**
   (T4/T5) ⇒ hay un consumo **por posición**.
2. **No es el descriptor A** (T6) — probablemente es la **estructura de skinning
   (arms)/zonas** atada al orden (histórico: draw log + arms). Hay **dos tablas**
   de descriptores (mesh-group + AWG0+0x1F80).
3. **Vía B (topología PS2) = RE multi-sesión**: reconstruir skinning/arms + tablas
   coherentes con el pool nuevo. **Vía A (NPM injection, conserva el orden) = el
   port PRÁCTICO validado** (`cell_npm4`, umbral 0.8).

## 5. IMPLICACIONES / SIGUIENTE

- **Opción práctica (recomendada)**: reactivar/refinar `cell_npm4` (Vía A).
- **Opción research**: mapear el formato de **arms/skinning** (arm = `[bone, ptr,
  0, ptr_mat4x4, 0]`; +0x1F80 matriz/datos) y la tabla runtime de descriptores;
  entonces se puede emitir un pool nuevo coherente (Vía B real).
- Instrumento nuevo: `awo_tools/phase_b_arms_dump.py` (dump arms + regiones 0x1E00/0x1F80).

## 6. REPRODUCIR

```
python awo_tools/afs_extract_hd.py 327 147 96        # -> e*.comp
xbdecompress e327.comp e327.bin
python awo_tools/phase_b_census.py e327.bin
python awo_tools/phase_b_consumer_scan.py e327.bin
python awo_tools/phase_b_desc_detail.py e327.bin      # A/B + huesos por bloque
python awo_tools/phase_b_deep_scan.py e327.bin        # refs escaladas
python awo_tools/phase_b_ab_compare.py e327.bin       # A vs B (triangulacion)
python awo_tools/phase_b_make_t2.py e327.bin e327_t2.bin
python awo_tools/phase_b_make_t3.py e327.bin e327_t3.bin
python awo_tools/phase_b_make_t4.py e327.bin e327_t4.bin
python awo_tools/phase_b_make_t5.py e327.bin e327_t5.bin
python awo_tools/phase_b_make_t6.py e327.bin e327_t6.bin
python awo_tools/phase_b_make_t7.py e327.bin e327_t7.bin
```
Empaquetar: `xbcompress /N:2048 e327_tX.bin geom.raw` → pad a 106496 →
`out/build/win-amd64-release/mods/_tX/us/data_cmn.afs/327/geom.bin`.

**Mods de test (UNO activo a la vez)**:
`_t2_swap` (idéntico), `_t3_reverse`/`_t4_inpart` (deforme), `_t5_noremap` (mucho
peor), `_t6_adesc` (normal → A no se usa), **`_t7_ibrev` (ACTIVO, decisivo IB)**.
⚠️ `AfsFindModOverride` sirve el PRIMER mod activo por orden alfabético:
desactivar el resto (`.disabled`).
