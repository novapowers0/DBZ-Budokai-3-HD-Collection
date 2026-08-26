# ESTRUCTURA DE DIBUJO HD — Mapa verificado (Babidi bin 96, formato C)

> Fecha: 2026-08-26. Investigación de la estructura de dibujo del bin HD usando
> la plantilla MÁS SIMPLE del juego (Babidi, bin 96: 1 AWG, 41 huesos, formato C)
> para cerrar el gap del conversor PS2→B3 HD (ESTUDIO_ECOSISTEMA_MODS §4.2).

---

## 1. RESUMEN EJECUTIVO

**La estructura de dibujo HD está totalmente mapeada y es REGENERABLE.** No es
un bloqueo fundamental (como parecía), sino un problema de regenerar el layout
de forma CONSISTENTE. Las piezas:

| Pieza | Contenido | Regeneración |
|---|---|---|
| **Mesh-ref blocks** (mesh parts) | Tipo de vértice (B5/B4) + textura/shader por part | De los mesh parts PS2 (tipo + tex/shader) |
| **Axes** (41×80B) | quat+pos (bind pose) + sello + arm_ptr | **Copiar de la plantilla HD** (esqueleto 1:1) o del PS2 (mismos sellos) |
| **Arms** | Datos por hueso (offsets+conteos → matrices/pesos) | **Copiar de plantilla 1:1** o generar del rig PS2 |
| **Descriptores** (0x60) | label + `max N m` + rango A (vértices) + rango B (IB) | **Calcular** de los buffers construidos |
| **sec34 / vb2 / IB** | Buffers de geometría | Ya resuelto (`ps2_to_hd_geometry.py`) |

**Implicación**: para un personaje con esqueleto **1:1** con un bin HD existente
(Babidi PS2→HD), el conversor = usar el bin HD como plantilla estructural
(copiar axes/arms/estructura) y regenerar solo geometría + descriptores +
mesh-ref blocks. **Es exactamente el enfoque de `build_from_template.py`** (el
que generó `janemba_from_cell`) — por eso el **re-test de `janemba_from_cell`
es el experimento crítico barato**.

---

## 2. LAYOUT DEL MESH GROUP (AWG0 de Babidi)

```
AWG0 @0xAC0:  n_bones=41  axes=0x7E0  groups=7  mg_off=0x560  mg_size=0x1F20
             sec34=0x2610(1934)  ib=0x17294(4872 u16)  end=0x198A4

Mesh group (mg=0x1020, size 0x1F20):
  0x0000  header (0x80B): floats 3D8AD927×3, 1.0×3, 0s,
          0xFFFFFFFF×2, 0x190/0x190 (counts), 0x44/0x44 ...
  0x0080  mesh-ref blocks (mesh parts) ×7, cada 0x50:
            id/sub, 00 00 01 B5 (tipo B5) o 00 00 01 B4 (facial), 00 00 29 BD (tex)
            0x44 ×2 (vert count / byte len), matrices 1.0/0.0, 00 00 00 05 marker
          → partes: 0x80, 0xD0, 0x120, 0x170, 0x1C0, 0x210(B4), 0x260
  0x02B0..  ejes (41 × 80B): eje0 @0x12A0 (sello 0x6000020F, arm 0x14B0 rel AWG)
  0x1F70    arms (datos por hueso): (offset, count) → matrices/quat+pos
  0x2439    descriptores (0x60 c/u): label + 'max N m' + rangos A/B
```

---

## 3. DESCRIPTOR DE SUBMESH (0x60 bytes) — SEMÁNTICA CONFIRMADA

Layout del descriptor (primer XBAB_BODY @0x2439):

```
+00  label (bone name, null-terminated)      XBAB_BODY
+0x18 "max N m" (string)                     max 12 m
+0x50 rango A start (u32) >> 8   → 0x77   = 119
+0x54 rango A count (u32) >> 8   → 0x09   = 9
+0x58 rango B start (u32) >> 8   → 0xED   = 237
+0x5C rango B count (u32) >> 8   → 0x0E   = 14
```

**A = rango de VÉRTICES en el pool (sec34)**: [start, start+count).
**B = rango de ÍNDICES del IB** (triangle strip): [start, start+count).

**Verificado por correlación** (descriptor 1 XBAB_BODY): el IB en el rango B
[237,251) es `125,125,126,119,121,120,121,122,123,124,124,126,126,121` → los
índices de vértice 119-126 caen EXACTAMENTE en el rango A [119,128). ✓

**Babidi tiene 14 descriptores**: 9×XBAB_BODY, BAB_L00_RHAND, 2×XBAB_M_DTEETH,
XBAB_M_UTEETH, BAB_L00_LHAND.

**Descriptor INERTE** (patrón de "neutralizado", §13.10): A apunta más allá de
sec34 (p.ej. 4440 > 1934) y B=(x,0) → no dibuja. El campo A del header del mg
vale 4440/44 — el mismo patrón.

---

## 4. MESH-REF BLOCK (mesh part, 0x50)

```
+00  id (u32) / sub (u32)
+08  00 00 01 B5   ← tipo de vértice (B5 = cuerpo skinned 48B PS2)
+0C  00 00 29 BD   ← textura/shader
+10  00 00 00 44 ×2 (0x44 = 68: conteo/bytes)
+18..+3F  matrices (1.0/0.0)
+40  00 00 00 05   ← marker
```

Las partes faciales usan `00 00 01 B4` (y `00 00 01 B4` ×2). El nº de mesh-ref
blocks = `groups` del header AWG0 (7 en Babidi).

---

## 5. EJES (80B por hueso) y ARMS

```
Eje (80B):
  +00..+2F  quat+pos / matrices (bind pose; 3×4 floats)
  +0x30  sello: 0x6000020F (body), 0x9000020C (sub-bone), 0x8000020C, 0x00000204 (shadow)
  +0x34  arm_ptr (rel AWG0)
  +0x38  otro ptr (p38)
```
Ejes verificados: 41 (igual al nº de huesos PS2 → mismo esqueleto, §12.2).

```
Arm (en arm_ptr del eje, p.ej. @0x1F70):
  (offset1, offset2) + conteos (1, 2, 3...)  → apuntan a bloques de datos por
  hueso: matrices 4×float + quat+pos (p.ej. @0x22B0, @0x23F0).
```
Los arms son datos de **skinning por hueso** (estilo rig PS2 con chunks+pesos),
NO rangos del IB a dibujar (eso lo hacen los descriptores). Por eso en Krillin
los "rangos de los shadows" estaban vacíos (§13.8): los arms no definen draw.

---

## 6. REGENERACIÓN PARA EL CONVERSOR (port PS2→B3 HD)

Dado un modelo PS2 (Babidi PS2 GH, 1 AMG, 41 huesos, 3403 verts/2219 tris) y la
plantilla HD (bin 96, 1 AWG, 41 huesos):

1. **sec34/vb2/IB**: generar con `port_ps2_b3_geometry` (ya resuelto).
2. **Descriptores**: tras construir el IB, agrupar los índices por mesh part
   (material/hueso del PS2) → por grupo: `A = (min_vert, count)`,
   `B = (min_idx, count)` → emitir descriptor 0x60 (label del hueso,
   `max N m`, A/B). **Cálculo directo.**
3. **Mesh-ref blocks**: por mesh part PS2 → bloque 0x50 con tipo (B5/B4 según
   vtype) y textura/shader del part PS2. **Cálculo directo.**
4. **Axes + arms**: copiar de la plantilla HD (esqueleto 1:1 = mismo orden de
   huesos). Si el esqueleto difiere, usar `retarget_hd.py` o generar del rig PS2.
5. **Empaquetar** #AMB + LZX + override (`port_ps2_b3_pack`).

**Pendiente de verificación en juego**: si `janemba_from_cell` (plantilla Cell
48 huesos + geometría Janemba, ya construido y DLL correcta) renderiza, la
plantilla con mismo nº de huesos acepta geometría ajena → la vía de la tabla
queda VALIDADA y el conversor se reduce a la mecánica anterior.

---

## 7. VINCULACIÓN CON EL HISTORIAL

- Los cuelgues de `build_awo_v2-v5` y la "masa deforme" de Janemba NO eran la
  estructura de dibujo en sí: eran (a) IB falso (no FaceType), (b) bone en +28
  (layout real), (c) conteos/layout inconsistentes (sec34/vb2/IB fuera de rango
  vs descriptores), (d) la inyección en plantilla de OTRO personaje (Krillin)
  con geometría re-topologizada. Ver ESTUDIO_ECOSISTEMA_MODS §5.
- `SUBMESH_DATA_B3.md` documentaba ya el descriptor (0x60, rangos A/B); esta
  sesión confirma A=vértices / B=IB por correlación directa y añade el layout
  del mesh-ref block, los ejes y los arms.