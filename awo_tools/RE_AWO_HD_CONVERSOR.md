# RE del formato AWO HD B3 — hacia un conversor universal OBJ→AWG HD

> 2026-08-18. Ingeniería inversa del bin HD de Budokai 3 para construir un
> conversor universal que convierta CUALQUIER modelo 3D + texturas a `.bin`
> HD compatible. Objetivo final: un "transformador a .bin de Budokai HD"
> (equivalente al `OBJ to AMG v0.92` de la comunidad, pero generando AWGs HD).

---

## 1. ESTRUCTURA DEL BIN HD (`#AMB` → `#AWO` + `#AZT`)

El bin HD de un personaje (p.ej. Krillin, 682528 B) es un contenedor `#AMB`:

| Offset | Contenido |
|---|---|
| 0x00 | `#AMB` header (count, tabla) |
| 0x40 | `#AWO` (el modelo 3D, big-endian) |
| 0x47020 | `#AZT` (las texturas, big-endian) |

### 1.1 Header del `#AWO` (relativo a 0x40)

| Campo | Offset | Krillin | Significado |
|---|---|---|---|
| magic | +0x00 | `#AWO` | |
| bones | +0x10 | 0x33 (51) | nº huesos |
| | +0x14 | 0x30 | |
| n_awg | +0x18 | 0x12 (18) | nº de AWGs |
| awg_tbl | +0x1C | 0x690 | tabla de offsets de AWGs (rel AWO) |
| | +0x20 | 0x18 | |
| labels | +0x24 | 0x6D8 | labels de huesos (rel AWO) |
| bones_tbl | +0x34 | 0x42360 | tabla de zonas de hueso |

### 1.2 Los 18 AWGs del cuerpo de Krillin

El bin NO tiene un solo AWG — tiene **18 AWGs separados**:

| AWG | Label | Rol | bones | sec34 | vb2 | ib |
|---|---|---|---|---|---|---|
| AWG0 | XKLL_BODY | cuerpo | 51 | 1956 | 226 | 5140 |
| AWG1-5 | KLL_L0X_LHAND | mano izq | 1 | ~120-163 | 10 | ~408-600 |
| AWG6-10 | KLL_L0X_RHAND | mano der | 1 | ~120-163 | 10 | ~408-600 |
| AWG11-17 | XKLL_*_FACE | cara | 1 | ~148-164 | 14 | 510 |

> El cuerpo es el AWG0 (51 huesos). Las manos y la cara son AWGs separados
> (1 bone cada uno). Un personaje nuevo necesitará su propia estructura de AWGs.

### 1.3 Header del `#AWG` (relativo al magic `#AWG`)

| Campo | Offset | Krillin AWG0 | Significado |
|---|---|---|---|
| magic | +0x00 | `#AWG` | |
| n_bones | +0x10 | 0x33 (51) | |
| axes | +0x14 | 0xA10 | zona de ejes (rel AWG) |
| groups | +0x18 | 0x0D (13) | nº mesh groups |
| | +0x1C | 0x40 | |
| | +0x20 | 0x6A0 | tabla mesh? |
| | +0x24 | 0x0B | |
| | +0x28 | 0x2700 | |
| vb2_rel | +0x2C | 0x17868 | offset vb2 (rel AWG) |
| ib_rel | +0x30 | 0x19F68 | offset IB (rel AWG) |
| sec34_rel | +0x34 | 0x2826 | offset sec34 (rel AWG) |
| end_rel | +0x38 | 0x1C790 | offset final |
| | +0x3C | 0x24 | |
| labels | +0x40 | XKLL_* | labels de huesos (16B c/u, bones pares) |

### 1.4 Zonas del AWG0 (relativo al AWG)

| Zona | Offset | Tamaño | Contenido |
|---|---|---|---|
| labels | 0x40 | ~0x9D0 | labels 16B × 51 |
| ejes | 0xA10 | 51×80=0xFF0 | quat+pos+arm_ptr(+0x34) |
| mesh group | 0x1200 | 13×0x50 | mesh-ref blocks |
| (mesh extra) | 0x1610 | ~0x3F0 | más mesh blocks |
| arms | 0x1A00 | 51×0x14=0x3FC | arms [bone,fin,0,ini,0] |
| tabla huesos | 0x1BF4 | ~0x208 | índices de hueso por part |
| descriptores | 0x1DFC | ~0x830 | 19 × 0x60 descriptores |
| sec34 | **0x2828** | 1956×44 | vértices (align +2) |
| vb2 | 0x17868 | 226×44 | vértices estáticos |
| ib | 0x19F68 | 5140×2 | índices u16 |
| end | 0x1C790 | | |

> **⚠️ ALIGN +2 CRÍTICO**: el sec34 real empieza en `sec34_rel + 2`
> (no en `sec34_rel`). El marker `FFFFFFFF` del primer vértice está en
> `sec34_rel+2`. Los 2 bytes en `sec34_rel` son padding.

### 1.5 Vértice sec34 (stride 44, big-endian, align +2)

```
+0x00  FFFFFFFF (marker)
+0x04  u (float)
+0x08  v (float)
+0x0C  z_local
+0x10  x_local
+0x14  y_local
+0x18  peso
+0x1C  BONE (u32)
+0x20  nz
+0x24  -ny
+0x28  nx
```

## 2. LOS DESCRIPTORES DE SUBMESH (19 × 0x60 en el AWG0)

Localizados entre los arms y el sec34 (0x1DFC..0x2826). Cada descriptor de 0x60:

| Campo | Offset | Significado |
|---|---|---|
| label | +0x00 | 16B (XKLL_BODY, ...) |
| | +0x10 | const 0x09000000 |
| | +0x14 | const 0x0F000000 |
| | +0x18 | debug "max N m" |
| | +0x20..0x3F | material/transform |
| | +0x40..0x4F | consts |
| rango A start | +0x50 | (offset<<8) |
| rango A size | +0x54 | (tamaño<<8) |
| rango B start | +0x58 | (offset<<8) |
| rango B size | +0x5C | ((tamaño<<8)\|1) |

**⚠️ PROBLEMA CLAVE**: los rangos A del AWG0 de Krillin apuntan a offsets
del sec34 que van hasta 4440, pero la geometría de un personaje reconstruido
con MENOS vértices (p.ej. 734 tras decimar) deja los descriptores **fuera de
rango (OOB)** → el cuerpo se deforma / parpadea.

**La reconstrucción correcta debe regenerar los descriptores con los rangos
reales del sec34/IB del personaje nuevo** (no los de Krillin).

## 3. LOS MESH-REF BLOCKS (13 × 0x50 en el AWG0)

| Campo | Offset | Significado |
|---|---|---|
| escala | +0x00..0x0C | 4× float 1.0 |
| sello | +0x10 | 0x204 sombra / 0x20C mesh |
| ptr huesos | +0x14 | offset a tabla de huesos del part |
| | +0x1C | offset (a veces) |
| | +0x20 | offset |

## 4. LOS ARMS (51 × 0x14 en el AWG0)

```
[bone, fin, 0, ini, 0]   (u32 c/u, 20 bytes)
```
- arm 0: `[0, 8064, 0, 7680, 0]` — el cuerpo, rango IB ini=7680 fin=8064 (bytes).
- arms 1-50: `[bone, 0, 0, 0, 0]` — solo bone, sin rango.

## 5. EL BUG DEL ALIGN +2 (descubierto 2026-08-18)

Los scripts previos (`port_ps2_to_b3.py`, `mezclar_ps2_hd.py`) escribían los
vértices sec34 en `sec34_rel+0`, pero el formato real es `sec34_rel+2`.
Esto desalineaba el marker `FFFFFFFF` → el runtime interpretaba coords basura
→ modelo gigante/deforme. **Corregido a `sec34_rel+2`.**

## 6. LECCIÓN DE LOS EXPERIMENTOS EN JUEGO

| Mod | Qué hizo | Resultado |
|---|---|---|
| krillin_rec_test | rec con desc/arms regenerados (uniforme) + align mal | CRASH |
| krillin_rec_diag | rec sin tocar desc/arms + align mal | gigante, sin crash |
| krillin_rec_align | rec con desc/arms regenerados + align +2 | CRASH |
| krillin_align2 | rec sin tocar desc/arms + align +2 | **manos bien, cuerpo deforme OOB, sin crash** |

**Conclusión**: la estructura del bin se acepta sin crash si NO se tocan los
descriptores/arms. El cuerpo deforme es por los descriptores OOB (apuntan más
allá de los vértices del sec34 reconstruido). Las manos se ven bien porque sus
AWGs (1-10) están intactos de Krillin.

## 7. HACIA EL CONVERSOR UNIVERSAL OBJ→AWG HD

Inspirado en el `OBJ to AMG v0.92` (que construye AMGs PS2 desde OBJ con
templates), el conversor HD debe:

1. **Parsear el modelo fuente** (OBJ, o convertir cualquier formato a OBJ
   vía Blender/FBX).
2. **Construir la geometría HD**: sec34 (stride 44, align +2, layout §1.5)
   + vb2 (estático) + IB (triángulos).
3. **Generar la estructura AWG**: header, ejes (1 por hueso), mesh-ref blocks
   (1 por mesh part), arms (por hueso), descriptores (con rangos REALES del
   sec34/IB del personaje, no OOB).
4. **Construir el `#AWO`** con N AWGs (1 por mesh group).
5. **Convertir la textura** a `#AZT` HD.
6. **Empaquetar `#AMB`** + comprimir LZX `/N:2048` + instalar como override
   por entrada (mid-insert virtual permite bins de cualquier tamaño).

### Las 3 piezas que faltan / están en progreso

1. **✅ Formato del AWG mapeado** (este documento): header, zonas, vértice,
   mesh blocks, arms, descriptores, align +2.
2. **❓ Cómo generar descriptores/arms/mesh-blocks coherentes** para una
   geometría nueva (evitar OOB y crash). La comunidad PS2 (OBJ to AMG,
   Budokai Toolset) genera AMGs PS2 con templates; hay que replicar el
   patrón en HD.
3. **❓ Conversión #AMT → #AZT** (texturas). Documentada parcialmente.

### Herramientas de la comunidad que pueden guiar el conversor

- `mod center/OBJ to AMG v0.92/source code.zip` — construye AMG PS2 desde OBJ
  (Python, templates binarias: amg_header, model_part_header, triangle).
- `modding resources update 2/lean bone tutorial/Budokai Toolset/` — AMG_C,
  AMO_S, AMB_C + templates `b3_amg_*.bin` + presets.
- `mod center/B3-IW AMO Converter + Shadows/` — conversor AMO.
- `github.com/SamuelDBZMAAM/Budokai-Modding-Tool` — AMG Creator + AMB Combiner.

---

## 8. 🔴 HALLAZGO CLAVE DE LA SESIÓN 2026-08-18: BINS HD AUTOCONTENIDOS

**El swap nativo HD→HD funciona** (el usuario logró poner Bulma y Babidi en el
slot de Krillin). Esto demuestra que **el bin HD es AUTOCONTENIDO**: cada
personaje lleva su esqueleto, geometría, texturas y estructura de dibujo
completa. El runtime lo acepta en cualquier slot.

**Análisis comparativo de 3 bins HD** (Krillin 327, Bulma 110, Babidi 96):

| Personaje | bones | AWGs | estructura |
|---|---|---|---|
| Krillin (327) | 51 | 18 | AWG0 cuerpo + 17 AWGs de manos/cara |
| Bulma (110) | 43 | 2 | AWG0 cuerpo + 1 AWG separado |
| Babidi (96) | 41 | 1 | un solo AWG0 |

→ **El nº de AWGs y huesos varía por personaje.** No hay estructura fija. Cada
bin HD es independiente.

**PS2 (#AMO0) y HD (#AWO) comparten estructura** (verificado con Janemba y
Krillin): los **ejes de 80B son idénticos** (mismos sellos: eje 0 =
0x6000020F, sub-bones = 0x9800020C/0x9800020E/0x9000020C). El esqueleto es el
mismo. El layout del vértice B3 es correcto (verificado: 1956/1956 markers,
bones, pesos; normales |mag|≈1).

**Diagnóstico del fracaso de Janemba/Krillin**: ambos se intentaron inyectando
geometría PS2 en la **plantilla de Krillin** (conteos fijos, descriptores/arms
de Krillin). Esto SIEMPRE falla con polígonos deformes porque la estructura de
dibujo HD (mesh parts + descriptores + arms) no coincide con la geometría
inyectada.

**✅ LA VÍA CORRECTA**: construir el bin HD de Janemba como un bin
**AUTOCONTENIDO** (como Bulma/Babidi), re-layouteando su #AMO0 PS2 al #AWO HD,
con su propia estructura de AWGs, ejes, descriptores y arms. NO inyectar en la
plantilla de Krillin.

### El conversor universal (OBJ/PS2 → bin HD autocontenido)

Pipeline (análogo al `amg_c.py` de SamuelDBZMAAM, que construye AMGs PS2 desde
cero con templates):
1. **Parsear el modelo fuente** (PS2 #AMO0, OBJ, o cualquier formato→OBJ).
2. **Construir los AWGs HD**: header + labels + ejes (reusar los del PS2,
   mismos sellos) + mesh parts + descriptores + arms + buffers
   (sec34/vb2/IB).
3. **Convertir la geometría** PS2 (48B, rig→bone) → HD (sec34 44B skinned +
   vb2 44B estático + IB).
4. **Convertir texturas** #AMT→#AZT.
5. **Empaquetar #AMB** + comprimir LZX /N:2048 + override por entrada
   (mid-insert virtual permite bins de cualquier tamaño).

### Qué falta / está en progreso
1. **✅** Formato HD mapeado (header, AWG, vértice, ejes, descriptores, arms).
2. **✅** Confirmado que PS2 y HD comparten estructura (re-layout, no formato nuevo).
3. **❓** El **rig PS2** (mapeo bone→vértice) para asignar el bone correcto a
   cada vértice PS2 en el sec34 HD. Ver Model-Rig Extractor + parse_ps2_mesh.
4. **❓** Generar descriptores/arms/mesh-parts HD coherentes para una geometría
   nueva (el patrón de `amg_c.py` en HD).
5. **❓** Retargeting de pose si el esqueleto del personaje difiere del anfitrión.

## 9. 🔴 ESTRUCTURA DE DIBUJO HD (mesh group) — ANÁLISIS (2026-08-18)

El mesh group (mg_off en el AWG header +0x20) contiene TODO el sistema de
dibujo HD: ejes + arms + mesh parts + descriptores. Analizado en Babidi (el
caso más simple, 1 AWG, 7 mesh groups).

**Ejes (rel AWG, en +0x14)**: cada eje de 80B tiene:
- +0x30 = sello (0x6000020F body / 0x9800020C / 0x9000020C / 0x8000020C).
- +0x34 = **arm** (ptr a la tabla de arms).
- +0x38 = **p38** (ptr al mesh part del cuerpo / 0 si es un bone sin parte).

**Arms** (tabla en los offsets arm de los ejes): enlazan cada bone con su
rango. Babidi eje 0 arm=0x14B0, eje 1 arm=0x14C4... (cada +0x14).

**Mesh parts**: los headers B5/B4 en el mesh group (patrón `00 00 01 B5
00 00 29 BD`), cada uno con textura/shader y offsets a su geometría.

**Descriptores**: los 0x60-byte blocks con labels + rangos A (sec34) + B (IB),
que conectan cada mesh part con los vértices/índices que dibuja. Babidi: 15
descriptores (8 de cuerpo con rangos reales + 7 de manos/cara con `A=4440`).

**La estructura es un sistema interconectado** (ejes → arms → mesh parts →
descriptores). Reconstruirla para una geometría nueva requiere generar TODOS
los campos coherentes. Este es el paso 3 del conversor (en progreso).

**Herramienta**: `awo_tools/analyze_meshgroup.py` — descompone el mesh group
de un AWG HD (ejes/arms/mesh parts).

## 10. 🔴 CRASH DEL BIN AUTOCONTENIDO — ESTRUCTURA REAL DEL MESH GROUP

**El bin autocontenido de Janemba CRASHÓ al cargar** (2026-08-18). El mod
`janemba_autocontenido` se desactivó (el juego volvió a funcionar). Causa:
**el mesh group HD es un CONTENEDOR que incluye ejes + arms + mesh-ref blocks
+ descriptores TODOS juntos** (los ejes están DENTRO del mesh group, no fuera).
Mi bin los ponía fuera y usaba un descriptor mínimo → el runtime rechazaba la
estructura.

**Mapa del mesh group de Babidi** (rel AWG, el más simple, 1 AWG):

| Zona | Offset (rel AWG) | Tamaño | Contenido |
|---|---|---|---|
| mesh-ref blocks | 0x560 | ~0x280 | estructura de dibujo (mesh parts con texturas/shader) |
| ejes | 0x7E0 | 41×80=0xCD0 | esqueleto (quat+pos+sello+arm_ptr) |
| arms | 0x14B0 | ~0x4C9 | rangos del IB por bone |
| descriptores | 0x1979 | 15×0x60 | label + stride 44 + rangos A(sec34)/B(IB) |
| (padding) | 0x2499 | | hasta sec34 |
| sec34 | 0x2610 | | vértices |

**Implicación para el conversor**: reconstruir el bin autocontenido requiere
construir el mesh group COMPLETO (no un descriptor mínimo): mesh-ref blocks +
ejes (dentro) + arms + descriptores. Los ejes deben apuntar (arm_ptr) a arms
válidos, y los descriptores a rangos del sec34/IB.

**PENDIENTE (paso 3 refinado)**: replicar la estructura del mesh group de
Babidi como plantilla, con los ejes JNB (48 bones) y los descriptores/arms de
Janemba. El nº de bones difiere (48 vs 41) → hay que re-layoutear ejes/arms.
