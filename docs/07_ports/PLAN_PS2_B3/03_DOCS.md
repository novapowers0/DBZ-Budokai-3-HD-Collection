# 03 — Documentación interna consolidada: Port PS2 → B3 HD

> Informe de análisis documental. Extrae de la documentación interna TODOS los
> hechos, formatos, intentos y lecciones sobre el port de modelos PS2→HD del
> DBZ Budokai 3 HD Collection. No genera bins ni código; solo consolida la
> evidencia dispersa y cita fuente/sección.
>
> **Fecha del informe**: 2026-09-10.
> **Alcance**: Vía A (inyección), Vía B (port completo), formato de vértices,
> arms/skinning, descriptores, cara/huesos 48-63, tests y constraints.

---

## 1. ESTADO DE LAS VÍAS A y B — MOTIVO EXACTO DEL BLOQUEO

### 1.1 Tabla de estado (verificado en juego)

| Vía | Estado | Mejor resultado | Fuente |
|---|---|---|---|
| Swap nativo B3→B3 | ✅ FUNCIONA | `sw_goten_nativo`, `sw_vegeta424` | AGENTS §3.4.1; HISTORICO §3.4.1 |
| **Vía A — Inyección** (plantilla + posiciones PS2) | ✅ FUNCIONA (reconocible) | `cell_npm4` (umbral binario 0.8); refinado `cell_npm_fix`; `cell_best` | AGENTS §3.4.1/§10; SESION_INYECCION §5 |
| **Vía B — Port completo** (topología PS2) | ⛔ BLOQUEADA (deforme) | — | AGENTS §3.4.3; SESION_FASE_B §4 |
| Swap de cabeza HD→HD | ◑ PARCIAL / pausado | `goku_armadura` v3 | HISTORICO §13.9/§13.10; AGENTS §3.4.1 |

### 1.2 Qué es cada vía

- **Vía A — INYECCIÓN** (`mod center hd/ports/port_ps2_b3_inject.py`): se toma la
  plantilla HD COMPLETA e intacta (pool, IB, descriptores, arms, ejes) y SOLO se
  reescriben las posiciones `+12/+16/+20` (y normales `+32/+36/+40`) de los slots
  `sec34` con la geometría PS2 convertida a **bone-local**.
  (`SESION_INYECCION_2026-08-26.md` §1.2; AGENTS §3.4.3.)
- **Vía B — PORT COMPLETO** (pipeline `port_ps2_b3_extract → geometry → draw →
  pack → verify`): reconstruye el pool `sec34`, el IB y los descriptores A/B con
  la topología PS2. (`HOJA_DE_RUTA_PORT_PS2_B3.md` §1/§2; `ESTRUCTURA_DIBUJO_HD.md` §6.)

### 1.3 EL MOTIVO EXACTO DEL BLOQUEO (síntesis de Fase B, 2026-09-10)

El bloqueo NO es que el IB no se use, ni que la geometría esté mal. Es que
**el pool de vértices se consume POSICIONALMENTE por una estructura que NO es
el descriptor A**. Cadena de evidencia:

1. **El IB SÍ gobierna la conectividad**: test **T7** (`_t7_ibrev`, pool intacto,
   IB entero invertido) → **deformidad masiva** ("cabeza completamente deforme").
   (`SESION_FASE_B_ARMS_2026-09-10.md` §3.14.)
2. **El orden del pool es sagrado**: tests **T3** (reverse + IB remapeado) y
   **T4** (reverse intra-bloque A + IB remapeado, A intacto) → **deformes**
   ("las mismas exactas deformidades"). **T5** (reverse del pool SIN tocar el
   IB) → **mucho peor** (cara extendida por el cuerpo).
   (`SESION_FASE_B_ARMS_2026-09-10.md` §3.6/§3.9/§3.10.)
3. **Prueba dura de que el IB no basta**: siguiendo el IB índice a índice, los
   registros de vértice son **IDÉNTICOS** en T2, T3 y T4
   (`IB-follow t2/t3/t4: same=5125 diff=0`). Un relabeling consistente del pool
   + remapeo del IB es una **identidad geométrica** → cualquier consumidor que
   resuelva SOLO por el IB vería la misma geometría. Si T3/T4 deforman
   ⇒ **existe un consumo POR POSICIÓN**. (§3.9.)
4. **Ese consumo posicional NO es el descriptor A**: test **T6** (`_t6_adesc`,
   pool e IB intactos, solo se ROTAN los rangos A entre descriptores) → **NORMAL**
   ("se ve bien y está igual que siempre"). ⇒ **el rango A NO determina la
   geometría dibujada**. (§3.12.)
5. **H3 ("cada bloque A = unidad") es INSUFICIENTE**: cada bloque A contiene
   **2–85 runs de hueso** (los bloques A particionan el pool de forma contigua,
   real, pero no son "un hueso cada uno"). Por eso "orden libre dentro del bloque"
   es FALSO. (§3.9/§3.8.)
6. **Sospechoso principal (histórico)**: la **estructura de skinning (arms)/zonas
   atada al orden del pool**. El `DBZ3_DRAW` log probó que el guest dibuja los
   strips correctos (`B_start×2`, `B_count+2` con +2 de padding degenerado y el
   amorfo del port venía del **skinning (arms) atado al orden**.
   (`SESION_INYECCION_2026-08-26.md` §1.1; `SESION_FASE_B §3.14`.)

**Conclusión operativa**: la Vía B exige reconstruir TODO el sistema de dibujo
coherente con el pool nuevo (mesh-ref + matriz de zonas + bboxes + descriptores
+ **arms/skinning**), no solo el IB y A/B. `HOJA_DE_RUTA_PORT_PS2_B3.md` §5.1;
`ESTRUCTURA_DIBUJO_HD.md` §7 (conclusión del port); AGENTS §3.4.2.3.

### 1.4 Correcciones de creencias previas

- **"El orden del pool NO importa"** (`SESION_PORT_RE §4.1`) era **FALSO**: venía
  de un test CONTAMINADO (`cell_npm8_test` activo servía la inyección, no el
  reverse). Re-testado en solitario (`§7.1`) → "una serie de deformidades
  impresionantes". Coexiste con: el transform usa el bone del vértice (`+28`),
  PERO la estructura referencia el pool por su orden original.
  (`SESION_PORT_RE §4.1` vs `§7.1`; HISTORICO §3.4.2.3.)
- **"La estructura está atada a mesh-ref/zonas"** → matizado por Fase B: no es
  el descriptor A (T6), probablemente **arms/skinning** (Fase B §4).
- **El "A mal" del port** (`A = [primer_vértice, nº]` asumiendo contigüidad) fue
  corregido a `A = [min(B), max(B)+1)`. El fix `cell_port_Afix_test` dio "SIN
  CAMBIO VISUAL" (pero contaminado; pendiente re-validar en solitario).
  (`SESION_PORT_RE §4.2`; `HOJA_DE_RUTA_PORT §5.2`.)

---

## 2. FORMATO DE VÉRTICE DE LOS AWGs (variantes A/B/C y "AWG de cara")

> ⚠️ **DISCREPANCIA DOCUMENTADA**: `docs/03_formatos/BIN_LAYOUT.md` §3 y
> `AMO_AWO.md` §4 están **MAL** (invierten `sec34`/`vb2` y dicen `sec34=+0x30`,
> `IB=+0x38`). Los offsets correctos son **`vb2=+0x2C, IB=+0x30, sec34=+0x34,
> end=+0x38`** (verificado empíricamente). Fuente: `SESION_FASE_B §3.1`;
> `AWO_FORMAT.md` §4.6/§5 también arrastra el layout viejo.

### 2.1 Mapa del header AWG (canónico, verificado)

```
+0x10 n_bones   +0x14 axes (== rigging_data_ptr)
+0x2C vb2       +0x30 IB        +0x34 sec34      +0x38 end
```
(`SESION_FASE_B §3.1`; `RE_AWO_HD_CONVERSOR.md` §1.3; `CONSOLIDADO.md` §13.5.1.)

### 2.2 Formato A — sec34 "estándar" (Cell F2, Krillin; stride 44, align +2)

```
+0  0xFFFFFFFF (nan marker)
+4  u            +8  v
+12 z_local      +16 x_local      +20 y_local
+24 peso (0.1-1.0)                 +28 BONE (u32, 0-35 en plantilla)
+32 nrm.z        +36 nrm.y negado  +40 nrm.x
```
Verificado: 36 bones únicos 0-35, normales |mag|≈1, peso 0.1-1.0, FFFF en +0.
**El bone va en +28, NO en +0x10** (las herramientas viejas escribían en +4/+16
→ masa deforme). (`AGENTS §3.2`; `CONSOLIDADO §13.5.16`; HISTORICO §65.1e.)

- **ALIGN +2 CRÍTICO**: el `sec34` real empieza en `sec34_rel + 2`; el marker
  del primer vértice está en `sec34_rel+2`; los 2 bytes previos son padding.
  (`RE_AWO_HD_CONVERSOR.md` §1.4/§5.)
- El `+0x2C`/`+0x34` del descriptor es el **TAMAÑO en bytes** del buffer (no
  offset): nº vértices = tamaño/44. (`AGENTS §3.2`.)

### 2.3 Formato C — la mayoría de bins (Goku 264, Vegeta 424, Babidi, Goten; stride 44 SIN align)

```
+0  x | +4  y | +8  z (local del hueso, [-1,1])
+12 0xFFFFFFFF
+16 u | +20 v | +24 n.x | +28 n.y | +32 n.z | +36 weight | +40 BONE
```
**En formato C el bone NO está en +28, va en +40.** El marker/flag NO es
necesariamente FFFFFFFF y **no hay align +2**. El pipeline DEBE autodetectar
formato A vs C. (`AGENTS §3.2`; `SESION_FASE_B §3.5`; `DICTAMEN_GPT6 §0.1`;
HISTORICO §13.8/`sw_vegeta424` validado en juego.)

### 2.4 vb2 — variantes

- **vb2 "estático" de Krillin**: `[pos.x_abs, y, z, 0,0,0, peso=0,
  0xFFFFFFFF@+28, nx, ny, nz]` — posiciones **ABSOLUTAS**, bone=FFFF = sin skin.
  (`AGENTS §3.2`; `awg_to_obj_b3.py` docstring; HISTORICO §13.5.18.)
- **vb2 de Cell F2 (layout B, 276 slots)**: `[1.0, 0, 0, ?, ?, ?, nan@+20,
  U@+24, V@+28, nrm@+32]` — SIN posiciones claras.
  (`AGENTS §3.2/§3.4.2.5`; `SESION_PORT_RE §4.4`.)
- ⚠️ El `sec2C`/`vb2` tiene `nan` en `+28` (layout DIFERENTE al sec34).
  (`CONSOLIDADO §12.1`.)

### 2.5 AWG de CARA / mano (n_bones = 1) — layout PROPIO

Resuelto en `HISTORICO §13.9` (2026-08-19). **Vértice (44B, marker FFFFFFFF en +0)**:

```
+0  0xFFFFFFFF (marcador)
+4  u | +8  v
+12 x | +16 y | +20 z   (POSICIÓN en espacio LOCAL del hueso cabeza)
+24 weight (=1.0) | +28 pad 0.0
+32 nx | +36 ny | +40 nz  (normal unitaria directa)
```
Verificado: `normal·(pos−centroide) > 0` en el 100%.

**Estructura del AWG de cara (offsets rel header #AWG, h)** — los campos
significan OTRA cosa que en el AWG0:
```
+0x10 n_bones (=1) | +0x2C = TAMAÑO del buffer de vértices (n*44)
+0x30 ib_rel = offset del IB | +0x34 sec_rel = TAMAÑO del IB en bytes
+0x38 end_rel
Descriptor en h+0x180: +0x1C = n_verts, +0x24 = n_tris
Buffer de vértices SIEMPRE en h+0x1F0
```
- En AWG de cara, `sec_rel` (+0x34) **NO es offset: es el tamaño del IB**. El
  buffer de vértices NO está en `sec_rel` sino en **h+0x1F0 fijo**.
- El IB de cara es **lista de triángulos** (cada 3 índices = 1 triángulo,
  quad-strip), no strip con winding alternado.
- **Solape buffer/IB**: el IB empieza en `ib_rel = 0x1F0 + n*44 - 32` (se solapan
  32 B); `end_rel = ib_rel + n*2`. (`HISTORICO §13.10`.)
- **Espacio local del hueso cabeza compartido** entre personajes (Goku y Vegeta
  tienen bounds casi idénticos) → la geometría se copia 1:1 sin transformación.
  (`HISTORICO §13.9`.)
- Herramientas: `awo_tools/awg_cara_export.py` (exporta), `awg0_export.py`
  (autodetecta A/C), `awg_to_obj_b3.py` (bins completos).
  (`AGENTS §10`; `ESTADO.md`; `ESTRUCTURA_DIBUJO_HD.md`.)

---

## 3. ESTRUCTURA DE "ARMS"/SKINNING Y LAS DOS TABLAS DE DESCRIPTORES

### 3.1 Arms — REVISIÓN FINAL (Fase B, 2026-09-10): son un armazón, NO rangos del IB

**Refutado "arms = rangos del IB"** (`CONSOLIDADO §13.5.13` y
`port_ps2_to_b3.py`). Dump de `phase_b_arms_dump.py`:
- cada **arm** es `[bone, ptr, 0, ptr_matriz, 0]`;
- `arm+12` → **datos float (matriz 4×4) que termina en `3F800000`** y crece
  **exactamente +64 B por hueso** ("Mesh End" de 64 B);
- `arm+4` → arrays pequeños.
⇒ Es un **armazón/armature con punteros**. Por eso `port_ps2_to_b3.py`, que los
regeneraba como rangos, **crasheaba** (punteros corruptos), no por un límite del
formato. (`SESION_FASE_B §3.3`; `CONSOLIDADO §13.5.13`; `RE_AWO_HD_CONVERSOR §4`.)

**Interpretación histórica** (la que documenta el bloqueo): los arms son datos de
**skinning por hueso** (estilo rig PS2 con chunks+pesos), NO rangos del IB a
dibujar. En Krillin original los 5140 índices están todos en `[0,3904)`; los
rangos `[3904,4936)` de los shadows estaban **VACÍOS** → el IB se dibuja completo
y los arms definen otra información. (`CONSOLIDADO §13.5.14` punto 5;
`SESION_INYECCION §1.1`; `ESTRUCTURA_DIBUJO_HD §5`.)
- Histórico intermedio (`CONSOLIDADO §13.5.13`): "zona de arms = bloques de 20 B
  `[bone_idx, offA_bytes, 0, offB_bytes, 0]`, solo los de sello 0x204 definen
  límites del IB en bytes; el runtime dibuja `[offset_previo, offset_bone)`".
  **Esto quedó REFUTADO** por Fase B §3.3 (los offsets de dibujo están vacíos).
- Forma `RE_AWO_HD_CONVERSOR §4`: `[bone, fin, 0, ini, 0]` (20 B), arm0 =
  `[0, 8064, 0, 7680, 0]`.

### 3.2 Mesh-ref blocks (0x50) — `X` = índice de descriptor, `Y` = hueso primario

```
+00 id/sub
+08 00 00 01 B5 (tipo B5, cuerpo) | 00 00 01 B4 (B4, cara) | 0x1F5 (F5)
+0C 00 00 29 BD (textura/shader)
+10 00 00 00 44 ×2 (0x44=68: conteo/bytes)
+18..+3F matrices (1.0/0.0)
+40 marker
```
- nº mesh-ref blocks = `groups` del header AWG0 (7 en Babidi, 13 en Krillin Cell
  F2 = 17 B5 + ~4 B4). (`ESTRUCTURA_DIBUJO_HD §2/§4`; `SESION_PORT_RE §2`.)
- Cell F2: `X = índice de descriptor` (varios mesh-ref por descriptor),
  `Y = hueso primario`. (SESION_PORT_RE §2.)

### 3.3 Descriptores de submesh (0x60) — encoding A/B confirmado

```
+00 label (8 chars, p.ej. "XCEL_BODY"/"XKLL_BODY")
+10 0x09 | +14 0x0F (constantes)
+18 "max N m" (string)
+24 0x34 (offset al rango)
+40 0x1158 (pool offset const) | +44 0x2C (stride 44) | +48 0x05
+50 A_start <<8 | +54 A_count <<8 | +58 B_start <<8 | +5C B_count <<8
   flag 0x01 en +0x5C (B_count)
```
**A = rango de VÉRTICES del pool (sec34); B = rango de ÍNDICES del IB.**
Verificado: los índices del IB en rango B caen SIEMPRE dentro de A
(Krillin 13/13, Cell 29/29, Babidi 10/10; correlación directa Cell 22/22).
(`ESTRUCTURA_DIBUJO_HD §3/§7`; `SESION_PORT_RE §3`; `SESION_FASE_B §3.2`.)

- **A particiona el pool de forma CONTIGUA**: cada `A_start` = anterior
  `A_start+A_count`. `A ≈ [min(B), max(B+1))`. El pool NO es contiguo por hueso
  (412 bloques de hueso fragmentados). (`SESION_FASE_B §3.7`.)
- **Descriptor INERTE** ("neutralizado"): A apunta más allá de sec34 (p.ej.
  4440 > 1934) y B=(x,0) → no dibuja. (`ESTRUCTURA_DIBUJO_HD §3`;
  HISTORICO §13.10, neutralizar cara de Vegeta.)
- Los descriptores faciales/manos tienen A en **vb2** (bones-in-A vacío):
  `XCEL_L00_FACE`, `XKLL_M_DTEETH`. (`SESION_FASE_B §3.7`.)
- **⚠️ HAY DOS TABLAS DE DESCRIPTORES** (Fase B §3.15):
  1. **Mesh group @AWG0+~0x2D49**: entradas 0x60 (label + `max N m` + A/B
     `<<8`, +0x50/+0x54/+0x58/+0x5C, flag 0x01) = la que edita T6.
  2. **AWG0+0x1F80**: SEGUNDA tabla (u32 en PLANO) con los MISMOS valores
     (`42,60,74,126…`, +0x1158, +0x2C const). Probablemente la **tabla runtime**
     que el guest consume en draw ⇒ T6 (que solo tocó la 1ª) no tuvo efecto.

### 3.4 Ejes (80 B) y el PARENT como OFFSET (no índice)

```
+00..+2F quat+pos / 3×4 floats (bind pose)
+30 sello: 0x6000020F (body) | 0x9800020C / 0x9000020C (sub-bone) | 0x8000020C | 0x00000204 (shadow)
+34 arm_ptr (rel AWG)
+38 hijo   +3C hermano   +40 PADRE
```
- **El parent (+0x40) es un OFFSET relativo al AWG0, NO un índice de hueso**:
  `parent_idx = (AWG0 + poff − axes_base) // 80`. Corregir esto invalida todas
  las matrices world calculadas antes (conversión `cell_conv` era inválida).
  (`SESION_INYECCION §2.1`; addendum §7 para PS2: `+0x40` offset rel AMG,
  `axes_rel=0x20`.)
- Con el parent corregido, el world del template traza un cuerpo coherente que
  **coincide con el model-space PS2** (pies, rodillas, cadera, pecho, cabeza).
  (`SESION_INYECCION §2.1`.)

### 3.5 Matriz de zonas, bboxes y caja del mesh group (Cell F2)

| Región | Off rel AWG0 | Tamaño | Contenido |
|---|---|---|---|
| mesh-ref blocks | 0x640 | 0x6E0 | parts de dibujo (bloque 0 = 0x40 + 21×0x50) |
| ejes | 0xD20 (axes_base) | 48×0x50 | quat+pos+scale + sello + arm/hijo/hermano/padre |
| matriz de zonas | 0x1C20 (0x28E0) | 48×0x10 | diagonal de índices de hueso + punteros a bboxes |
| bboxes | 0x1FE0 (0x2CA0) | 0x40 c/u | AABB por zona (min/max vec4), model-space |
| descriptores | 0x2209 (0x2EA9) | 0x60 c/u | A/B de dibujo |

(`SESION_PORT_RE §1`; `ESTRUCTURA_DIBUJO_HD §7`.) El mesh group HD es un
**CONTENEDOR** que incluye ejes + arms + mesh-ref + descriptores JUNTOS; los
ejes están DENTRO del mesh group (deben apuntar a arms válidos). Ponerlos fuera
→ crash. (`RE_AWO_HD_CONVERSOR §10`.)

---

## 4. TODOS LOS TESTS, INTENTOS Y SU LECCIÓN

### 4.1 Matriz de permutaciones (DICTAMEN_GPT6 §2.3) vs ejecución real

El dictamen propuso T0–T6. La ejecución real (Fase B + sesiones) fue:

| Test | Cambio | Render en juego | Lección | Fuente |
|---|---|---|---|---|
| **T0** | Original | referencia | — | DICTAMEN §2.3 |
| **T1** | Round-trip sin cambios | (no ejecutado como tal) | errores de serializador | DICTAMEN §2.3 |
| **T2** | swap 2 vértices del MISMO hueso + IB remapeado | **IDÉNTICO** ✅ | el orden DENTRO de un descriptor/hueso es libre | FASE_B §3.5 |
| **T3** | reverse del pool + IB remapeado + A/B recomputados | **DEFORME** ❌ | el reverse limpio SÍ deforma | FASE_B §3.6 |
| **T4** | reverse DENTRO de cada bloque A + IB remapeado (A intacto) | **DEFORME = que T3** ❌❌ | hay consumo POR POSICIÓN; el IB no basta | FASE_B §3.9 |
| **T5** | reverse del pool, IB SIN tocar | **MUCHO PEOR** (cara extendida) ❌❌ | T5≫T4 ⇒ el IB controla geometría; 2ª vía posicional | FASE_B §3.10 |
| **T6** | solo rotar rangos A (pool+IB intactos) | **NORMAL** ✅ | **el rango A NO se usa para dibujar** | FASE_B §3.12 |
| **T7** | pool intacto, IB entero invertido | **MASIVO** (cabeza rota) ✅ | **el IB SÍ gobierna la conectividad** | FASE_B §3.14 |
| **IB-follow** | T2/T3/T4 | `same=5125 diff=0` | registros idénticos ⇒ el guest NO dibuja solo por IB | FASE_B §3.9 |

**Census / consumer scan** (`phase_b_census.py`, `phase_b_consumer_scan.py`):
NO hay consumer oculto u16/u32 crudo. Solo-IB: Krillin 1866/2182, Cell 2356/2937.
El consumo posicional no aparece con encodings `v*44`, `v*44+sec`, `v<<2`,
`v<<8`. 15 entradas del IB fuera de rango (2188–2189, 8 más allá del pool) al
final del IB → posible buffer/pool adicional. (`FASE_B §3.4`; AGENTS §3.4.2.3.)

### 4.2 Test bone0 (el guest usa el bone del vértice)

`cell_bone0_test`: plantilla con TODOS los bones sec34 = 0 (posiciones
intactas) → **colapsa TODO a los pies**; la cara superior y una mano se salvan
(viven en vb2). ⇒ **el guest USA el bone del vértice (+28) para el transform**.
**VÁLIDO** (no contaminado). (`SESION_PORT_RE §4.5/§7`; HISTORICO §3.4.2.2.)

### 4.3 Inyección v1–v7 y NPM (umbral = parámetro crítico)

| Mod | Método | Resultado |
|---|---|---|
| inject2 (1001 slots) | per-bone greedy, bone-local | Cell reconocible (manos, torso, parte cabeza, una pierna) |
| inject4 (2385+276) | world-matching + umbral 2.0 | más Cell pero "decimado/reducido" + polígonos deformes |
| npm (2443+218) | NPM (density fix) | ≈ inject4 |
| npm2 | NPM + normal de triángulo | más brillante, amorfo en boca/cola/manos/brazos |
| npm3 | NPM + normal de vértice | ≈ igual; mano mala mejora "ligeramente" |
| **npm4 (1821+840)** | **NPM + normales + umbral ESTRICTO 0.8** | ✅ **MEJOR**: torso, cabeza sup., cintura, piernas, pies, brazos, manos |
| npm6 (2443+218) | NPM + normales + **soft[0.5,2.0]** | ❌ deformidad importante (blends parciales) |
| npm7 (1821+840) | NPM + normales + **soft[0.3,0.8]** | ❌ PEOR que npm4 |

(`SESION_INYECCION §5.1`.)

**Lección maestra**: **BINARIO SÍ, BLEND NO**. La inyección completa de los slots
bien alineados funciona; cualquier peso parcial (blend) produce posiciones a
medias (ni PS2 ni HD) → amorfo. El único parámetro a afinar es el VALOR del
umbral. (`SESION_INYECCION §5.2`.)

**Por hueso (match distances)**: alinean bien core (bones 0,1,15,19),
piernas (5,9,10,11); mal OBI (3,4), bones 13/14, **muy mal manos/brazos
(18,20,21) hasta max 8.9**; cabeza medio (1.0–1.25). El umbral 2.0 inyectaba
extremidades mal emparejadas → estiradas. (`SESION_INYECCION §5.2`.)

**Normales**: formato HD `[nz, -ny, nx]` en +32/+36/+40; 100% unitarios.
Escribir solo posiciones dejaba normales HD → specular roto. Normal geométrica
vs interpolada no cambió la causa principal. (`SESION_INYECCION §5.3`.)

**Bug del extractor PS2 (addendum 2026-09-10)**: las partes "L00" (manos bones
23/30, cara 40, dientes 36/38, cola 43–47) van en **espacio LOCAL del hueso**;
el extractor las trataba como model-space. Fix en `compute_worlds()` +
`parse_parts(..., worlds)`: NPM 0.8 pasa de 1821→**1962 inyectados**; LHAND
alineación 12.41→**0.15**. Mod `cell_npm_fix`. (`SESION_INYECCION §7`.)

### 4.4 Janemba (IW→B3) — FRACASO DOCUMENTADO, archivado

- v4–v10 (14/08): parse→skin→decimar→build → **masa deforme**; causa inicial:
  el parser PS2 no leía el IB real (FaceType). (`AGENTS §3.4.4`.)
- v6 (14/08): entra en combate sin crash con conteos EXACTOS de Krillin
  (sec34=1956, IB=5140) → masa deforme por re-rigging JNB→KLL. (`CONSOLIDADO
  §13.5.14`.)
- v7: cuerpo reconocible; v8 (dedos→18/25, caras→36): **CRASH**. Estado v7
  (corrupto). (`CONSOLIDADO §13.5.16`.)
- **Causa raíz del sistema**: los arms/mesh-ref de Krillin apuntaban a rangos
  equivocados al inyectar geometría de Janemba con IB distinto.
  (`CONSOLIDADO §13.5.11`.)
- **Decisión del usuario**: eliminar/archivar el trabajo de Janemba
  (`awo_tools/historial_fallidos/`). **NO reintentar sin conversor completo
  validado.** (`AGENTS §3.1`; HISTORICO §11.1.)

### 4.5 Pikkon (IW) — DESCARTADO por esqueleto

PKH con `SKIRT`, 58 huesos, distinto a KLL → NO 1:1. v7 (umbral 0.3): cuerpo
"sobrependentemente bien" pero fallan 7 zonas (oreja, cabeza trasera, boca,
hombro der, cinturón, rodilla der, pie izq). Rodilla/pie **viven en vb2**
(posiciones absolutas) → la inyección solo toca sec34 → quedan HD SIEMPRE.
Para arreglar cara/piernas hay que reconstruir el bin completo.
(`AGENTS §3.1`; HISTORICO §65.1f; `MATRIZ_CANDIDATOS` §orden.)

### 4.6 Contaminación de tests (lección operativa crítica)

`cell_npm8_test` (inyección umbral 1.4 en cabeza) quedó ACTIVO durante toda la
sesión de RE del port. `AfsListMods` + `AfsFindModOverride`: los mods activos van
primero en orden ALFABÉTICO y se sirve el **PRIMER mod con entrada para ese slot**.
Con npm8 activo: `cell_reverse_test` y `cell_port_Afix_test` sirvieron npm8 (tests
**INVÁLIDOS**); `cell_bone0_test` (bone0 < npm8 alfabéticamente) fue VÁLIDO.
**Fix**: desactivar npm8. **REGLA**: UN SOLO mod activo por test; verificar con
`Get-ChildItem mods | Where {-not .disabled}`. (`SESION_PORT_RE §7`; AGENTS
§3.4.5.4.)

### 4.7 Otros tests relevantes

- **Reverse** (pool invertido): re-testado en solitario → deformidades. El orden
  del pool importa. (`SESION_PORT_RE §7.1`.)
- **Port conv2** (pool PS2 + IB + A/B): amorfo. `cell_port_Afix_test` (A
  corregido): sin cambio visual. (`SESION_PORT_RE §4.1/§4.2`; HOJA_DE_RUTA §5.)
- **Tests negativos sin efecto** (port completo): clampear bones (35/33),
  `+0x10=9` en todos, `0xFFFF→0` en el IB, flip de winding → **NINGUNO cambió el
  render**. El bin SÍ se servía. (`SESION_INYECCION §1.1`.)
- **Re-layout de buffers**: sec34 **+1 CRASHEA** en combate; vb2 **+1 entra en
  combate** (con lag/mano faltante). ⇒ `sec34_count` es parámetro FIJO, vb2
  tolerante. AWG0 no puede encoger (v5 no arranca) ni crecer en exceso (v4 crash
  combate). `sec34=1956/IB=5140` exactos = clave del v6. (`CONSOLIDADO §13.5.7/
  §13.5.8/§13.5.14`.)
- **Swap de cabeza HD→HD**: `swap_cabeza.py` (reconstrucción de bloque) →
  CRASH 0xC0000005 (el AWG0 referencia los AWG de cara por offsets que se rompen
  al mover el bloque). `swap_cabeza_inplace.py` (in-place, mantiene tamaños) →
  **FUNCIONA en juego** pero con **z-fighting** frente/ojos. v3 neutraliza
  descriptores de cara de Vegeta → desaparecen partes del pelo, sigue sin ser la
  cara de Goku. **PAUSADO por decisión del usuario.** (`HISTORICO §13.9/§13.10`.)

---

## 5. CARA, HUESOS 48-63 Y SWAP DE CABEZA

### 5.1 El límite estructural de la Vía A: 16 AWGs de 1 hueso (48-63)

**Cell F2 (2026-09-10)**: el bin HD tiene **17 AWGs**:
- **AWG0**: 48 huesos, 2661 verts (cuerpo).
- **16 AWGs de 1 hueso = huesos 48-63** (cara/detalles).

El **PS2 solo tiene 48 huesos (0-47)** → los 16 AWGs extra **NO tienen
equivalente PS2** y quedan HD (por eso la cara sale en HD). La inyección actual
solo toca el `sec34` del primer AWG0; el bone global de cada AWG de 1 hueso se
lee en su arm `+0x34` → struct[0]. Para PS2-izar la cara hay que **remapear por
label los huesos PS2 33-40 a los AWGs 48-63** (formats de vértice variables por
AWG: `FFFF@0/@12/@28/@32`). (`AGENTS §10`.)

- Mod `mods/cell_best` = cuerpo corregido + **manos en HD real**.
  ⚠️ El test previo `cell_npm_fix_nohand` revertía a npm4 (NO a la plantilla);
  npm4 ya tenía las manos mangleadas (bone 23 con 228 slots movidos). Incluye
  guardia anti-estirado (revierte triángulos con área inyectada >3× la HD;
  ~68-115 verts). (`AGENTS §10`.)

### 5.2 Por qué la cara/piernas quedan HD (mecanismo general)

- El `sec34` de la plantilla usa **solo bones 0-35** (36 bones); piernas
  (38-50) y rostro van al **vb2** (sin skin, posiciones absolutas). En Krillin HD
  `sec34=1956` solo bones 0-35; vb2=226 con bone=0xFFFFFFFF.
  (`AGENTS §3.2`; HISTORICO §13.5.18; HISTORICO §3.3.)
- La inyección solo reescribe `sec34` → **cara/piernas quedan 100% HD siempre**.
  Inherente a la Vía A. (`HISTORICO §65.1f`; MATRIZ_CANDIDATOS.)

### 5.3 AWG de cara (nb=1) — estructura y swap (ver §2.5)

Mapa confirmado: Goku AWG16-22 = `XGOK_L01/L18/L09/L04/L05/L06/L42_S00_FACE` ↔
Vegeta AWG19-25 = `XVGT_L01/L18/L00_S09/L04/L05/L06/L44_S00_FACE`.
Correspondencia por label numérico. Espacio local del hueso cabeza compartido →
geometría copiable 1:1. (`HISTORICO §13.9`.)

### 5.4 Estado del swap de cabeza

- **in-place FUNCIONA** pero con z-fighting (la cara/cabello de Vegeta sigue en
  el AWG0 superponiéndose). Neutralizar descriptores de cara de Vegeta deja
  huecos. No se logra "la cara de Goku completa". Pausado.
  (`HISTORICO §13.9/§13.10`.) Para una cara completa habría que sustituir
  TAMBIÉN las piezas del AWG0 (face/dientes/HAIR). (`HISTORICO §2098`.)

---

## 6. CONSTRAINTS CRÍTICOS

### 6.1 AFS / override / mid-insert virtual

- Tabla AFS del runtime se lee en **offset 8** (magic "AFS"3B + pad1B + count4B;
  luego `(addr u32, size u32)×8B`), **NO en 0x10** (off-by-one servía el bin N+1
  → crash). (`AGENTS §5`; FASE_B §2.)
- Override por entrada: `mods/<mod>/us/<afs>/<entry_index>[/archivo]`
  (`AfsFindModOverride`; soporta carpeta). (`AGENTS §6`; ESTUDIO §1.1.)
- **MID-INSERT VIRTUAL** (`AfsGetVirtualTable`): si el override excede `to_read`,
  la entrada crece in-place (alineado 0x800) y las posteriores se desplazan por
  el delta acumulado, todo en memoria (`AfsVirtualRange`). **SIN archivos
  gigantes**. Criterio: crece solo si `override > to_read`, NO por exceder el
  slot físico. (`AGENTS §6`.)
- **`--append` DESCARTADO**: el guest usa **búsqueda binaria** sobre la tabla
  (asume offsets crecientes); append desordena → devuelve entradas equivocadas →
  `0xC0000005`. (`AGENTS §6`; HISTORICO §65.2.)
- **🔴 DLL correcta**: el build del juego SOBRESCRIBE `rexruntime.dll` con la
  stale de `rexglue/bin`. Tras `cmake --build` hay que recopiar la DLL canónica.
  Verificar `Select-String rexruntime.dll -Pattern "AfsGetVirtualTable"`.
  (`AGENTS §7`; ESTUDIO §5.6.)
- **La entrada correcta**: Krillin visible = **entrada 327** (105296 B → padded
  106496). (`AGENTS §6`; note: `CONSOLIDADO §13.5.14` histórico decía e326 por
  1-off, luego corregido a 327.)

### 6.2 Compresión y padding

- **LZX `/N:2048`** (NO `/N:32`). `xbcompress /N:2048 <src> <dst>` /
  `xbdecompress`. (`AGENTS §4/§6`; ESTUDIO §1.2.)
  ⚠️ `AWO_FORMAT.md` §2.3 dice `/N:32` → **DESACTUALIZADO** (mantener solo como
  histórico).
- **Padding al `to_read` EXACTO** que lee el guest. Si es más corto → crash.
  (`AGENTS §6`; ESTUDIO §5.5.)
- Verificación en logs: `AFS OVERRIDE HIT (folder)` + `AFS MOD READ: ...
  got=to_read`; si `got < to_read` → falta padding. (`AGENTS §6`.)

### 6.3 Tamaños / conteos de buffers

- `sec34_count`/`vb2_count` son parámetros FIJOS derivados de los offsets del AWG
  header; cambiarlos (aunque +1) rompe el parseo → null deref.
  (`CONSOLIDADO §13.5.8`.)
- Crecimiento excesivo del AWG0/sec34 → crash **0x856AC389**.
  (`AGENTS §3.4.5.5`.)
- Mantener buffers del MISMO tamaño o usar conteos ≤ plantilla.
- v6 Janemba: rellenar a los conteos EXACTOS de Krillin con slots vacíos
  (sec34=1956, IB=5140) es la clave. (`CONSOLIDADO §13.5.14`.)

### 6.4 Contaminación de mods (repetido por criticidad)

`AfsFindModOverride` sirve el PRIMER mod activo por orden alfabético. Un mod
olvidado invalida los tests del mismo slot. **UN SOLO mod activo por test**;
usar `.disabled`. (`AGENTS §3.4.5.4`; FASE_B §6; SESION_PORT_RE §7.)

### 6.5 Mods / activación

- Un mod está activo si NO tiene el marker `.disabled` (`IsModEnabled`).
  El cvar `dbz3_enabled_mods` es CÓDIGO MUERTO. (`AGENTS §6`.)
- `AfsFindModFileOverride`: reemplazo de archivo completo en
  `mods/<mod>/<filename>` o `mods/<mod>/us|eu/<filename>`. (`AGENTS §6`.)

### 6.6 Formato / ingeniería

- El **bin HD es AUTOCONTENIDO**; el guest autodetecta formato A/B/C. No forzar
  formato A de Krillin. (`AGENTS §3.4.2.7`; ESTUDIO §5.7.)
- **`bone@+28` solo para sec34/formato A**; formato C usa `+40`. Seleccionar
  layout por AWG, nunca offset global. (`DICTAMEN §0.1`; AGENTS §3.2.)
- Descriptor A: `A = [min(B), max(B)+1)`, NUNCA asumir contigüidad (las parts
  comparten vértices dedup). (`AGENTS §3.4.5.3`; SESION_PORT_RE §4.2.)
- El IB PS2 es implícito por **FaceType** (1=strip, 0=triplete), no lista de
  índices. (`ESTUDIO §5.4`; `AMO_AWO §6.2`.)
- El bone del vértice HD va en +28 (formato A) — NO copiar el layout B1 (bone@+16).
  (`ESTUDIO §5.3`.)
- `mid-insert` amplía una entrada AFS existente; **NO añade índices AFS nuevos**.
  (`DICTAMEN §0.1`; HOJA_DE_RUTA_2026_09 §3.6.)

---

## 7. INVENTARIO DE HECHOS CLAVE (referencia rápida)

| Hecho | Valor / resultado | Fuente |
|---|---|---|
| Krillin PS2 GH = HD 360 | 51 huesos, 18 AWG, 68 labels idénticos, NO re-rigging | AWO_FORMAT §5; CONSOLIDADO §1.2 |
| nº AWGs/huesos varía | Krillin 18/51, Bulma 2/43, Babidi 1/41, Cell F2 17 (48+16) | RE_AWO §8; AGENTS §10 |
| sec34 stride | 44 B, align +2 (formato A) | AGENTS §3.2; RE_AWO §1.4 |
| Descriptor A/B | A=vértices, B=índices IB; B⊂A siempre | ESTRUCTURA_DIBUJO §3 |
| 2 tablas descriptores | mesh-group @~0x2D49 + AWG0+0x1F80 (runtime) | FASE_B §3.15 |
| Arms | `[bone, ptr, 0, ptr_mat4x4, 0]`, +64 B/hueso, armature | FASE_B §3.3 |
| Parent del eje | offset rel AWG/AMG, no índice | SESION_INYECCION §2.1/§7 |
| El guest usa bone +28 | test bone0 colapsa a pies | SESION_PORT_RE §4.5 |
| El IB se usa | test T7 masivo | FASE_B §3.14 |
| El rango A NO se usa | test T6 normal | FASE_B §3.12 |
| Consumo posicional | T4/T5 deforman con IB consistente | FASE_B §3.9/§3.10 |
| Mejor inyección | cell_npm4 / cell_npm_fix (umbral 0.8 binario) | SESION_INYECCION §5 |
| Cara = 16 AWGs 48-63 | sin equivalente PS2 | AGENTS §10 |
| Swap cabeza | parcial, z-fighting, pausado | HISTORICO §13.10 |
| Janemba | fracaso archivado | HISTORICO §11.1; AGENTS §3.1 |
| Pikkon | descartado (esqueleto no 1:1) | AGENTS §3.1 |
| Reverse | deforma (orden del pool importa) | SESION_PORT_RE §7.1 |
| sec34 +1 | CRASH combate | CONSOLIDADO §13.5.8 |
| vb2 +1 | entra en combate (tolerante) | CONSOLIDADO §13.5.8 |
| Compresión | LZX /N:2048 + pad a to_read | AGENTS §6 |
| Mods | 1 activo a la vez; marker `.disabled` | AGENTS §3.4.5.4 |

---

## 8. VACÍOS / INCÓGNITAS PENDIENTES (para futuras sesiones)

1. **Localizar el consumo posicional del pool**: no es descriptor A (T6);
   sospechoso = arms/skinning + tabla runtime @AWG0+0x1F80. RE del bucle de draw
   en `generated/dbz3_recomp.*.cpp`.
2. **Formato de arms/skinning** sin mapear al 100% (estructura de skinning por
   hueso); `phase_b_arms_dump.py` es el instrumento.
3. **Confirmar qué tabla de descriptores usa el draw** (mesh-group vs 0x1F80).
4. **vb2 layout B** de Cell F2 sin emitir correctamente por el port.
5. **Remapear huesos PS2 33-40 a AWGs 48-63** para PS2-izar la cara (formatos de
   vértice variables por AWG).
6. **Rig 1:1 pendiente**: Babidi PS2 desde AFS GH devuelve #AMB/#AWO BE (no
   #AMO0 LE) → fuente PS2 real sin localizar. TienTien con capa (IW) pasa rig
   base 1:1 (42 labels comunes + 10 de capa). (`SESION_BABIDI`; SESION_TIEN;
   MATRIZ_CANDIDATOS.)

---

### Fuentes leídas

`AGENTS.md` (§3.4, §6, §10); `docs/07_ports/`: `ESTRUCTURA_DIBUJO_HD.md`,
`SESION_INYECCION_2026-08-26.md`, `SESION_FASE_B_ARMS_2026-09-10.md`,
`SESION_PORT_RE_2026-08-26.md`, `HOJA_DE_RUTA_PORT_PS2_B3.md`,
`ESTUDIO_ECOSISTEMA_MODS.md`, `MATRIZ_CANDIDATOS_PS2_HD.md`,
`SESION_BABIDI_VALIDACION_2026-09-08.md`, `SESION_TIEN_RIG_2026-09-08.md`;
`docs/03_formatos/`: `AMO_AWO.md`, `BIN_LAYOUT.md`, `ACM_FORMAT.md`;
`AWO_FORMAT.md`; `docs/DICTAMEN_GPT6_ASTRA.md`;
`docs/HOJA_DE_RUTA_2026_09.md`; `awo_tools/CONSOLIDADO.md`,
`awo_tools/RE_AWO_HD_CONVERSOR.md`, `awo_tools/RE_PROGRESO.md` (greps);
`docs/01_estructura/HISTORICO_AGENTS.md` (§3.3, §3.4, §13.5.x, §13.9, §13.10,
§65.1e/§65.1f).
