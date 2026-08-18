# SUBMESH DATA B3 — LAYOUT MAPEADO (2026-08-17)

> La pieza que faltaba para la reconstrucción completa PS2→HD (inspirada en
> la SESION11 del proyecto B1). El B3 SÍ tiene la zona de submesh data y ahora
> está mapeada.

---

## 1. UBICACIÓN

En el AWG0 del bin Krillin (b327_hd.bin), entre la zona de labels/ejes y el
sec34:

```
0x2D49 .. 0x3459  = 19 descriptores (stride 0x60 = 96 bytes)
0x35A6            = inicio del sec34 (44B/slot)
```

Los descriptores del cuerpo (0-11) usan `XKLL_BODY` como label + debug `max N m`.
Los descriptores de cara (13-18) usan `KLL_L00_LHAND`, `KLL_L00_RHAND`,
`XKLL_M_DTEETH`, `XKLL_M_UTEETH`, `XKLL_L00_FACE` (layout distinto: material).

---

## 2. LAYOUT DEL DESCRIPTOR DE CUERPO B3 (stride 0x60, 96 bytes)

```
+00..+0F  label 16B (XKLL_BODY, ...)
+10       u32 constante 0x09000000 (¿submesh count?)
+14       u32 constante 0x0F000000 (¿flags?)
+18..+1F  debug string "max N m" (del desarrollador)
+20..+3F  floats de transformación/material (quats, pos, escala)
+40       u32 constante 0x00115800 (offset buffer de geometría)
+44       u32 constante 0x00002C00 (tamaño buffer)
+48       u32 0x00000500 (¿count?)
+4C       u32 0
+50       u32 INICIO rango A (contiguo entre descriptores)
+54       u32 TAMAÑO rango A
+58       u32 INICIO rango B
+5C       u32 TAMAÑO rango B
```

**DIFERENCIA vs B1**: en el B1 los rangos estaban en `+60/+64/+68/+6C` del
descriptor (después de +0x5F de floats). En el B3 el descriptor es más corto
(0x60 vs 0x80+) y los rangos están en `+50/+54/+58/+5C`.

---

## 3. CONTIGÜIDAD DEL RANGO A (verificado, 12/12)

| # | label | +50 (inicio A) | +54 (tamaño A) | fin | contiguo |
|---|-------|----------------|----------------|-----|----------|
| 0 | XKLL_BODY | 0x2A00 | 0x3C00 | 0x6600 | OK |
| 1 | XKLL_BODY | 0x6600 | 0x6D00 | 0xD300 | OK |
| 2 | XKLL_BODY | 0xD300 | 0x8E00 | 0x16100 | OK |
| 3 | XKLL_BODY | 0x16100 | 0x1800 | 0x17900 | OK |
| 4 | XKLL_BODY | 0x17900 | 0x1FE00 | 0x37700 | OK |
| 5 | XKLL_BODY | 0x37700 | 0xC200 | 0x43900 | OK |
| 6 | XKLL_BODY | 0x43900 | 0x1C00 | 0x45500 | OK |
| 7 | XKLL_BODY | 0x45500 | 0x800 | 0x45D00 | OK |
| 8 | XKLL_BODY | 0x45D00 | 0x1400 | 0x47100 | OK |
| 9 | XKLL_BODY | 0x47100 | 0xC600 | 0x53700 | OK |
| 10 | XKLL_BODY | 0x53700 | 0xC300 | 0x5FA00 | OK |
| 11 | XKLL_BODY | 0x5FA00 | 0x1800 | 0x61200 | OK |

Los rangos A son **contiguos** (fin de uno = inicio del siguiente). Cubren la
geometría del buffer (sec34 + IB). Este es el patrón que el B1 describió.

---

## 4. IMPLICACIÓN PARA LA RECONSTRUCCIÓN

Para portar un personaje PS2→B3 HD:

1. Parsear el #AMO0 PS2 (mesh parts, verts, rig → coords locales + bones).
2. Generar sec34 (44B, layout REAL con BONE@+28) + IB desde los triángulos PS2.
3. **Regenerar los descriptores de submesh** (uno por mesh part):
   - label del part PS2
   - +50/+54 = inicio/tamaño rango A del buffer de geometría (contiguos)
   - +58/+5C = inicio/tamaño rango B
4. Regenerar arms (rangos del IB por bone).
5. Mantener ejes, mesh part headers, y la estructura del AWG0 del bin
   plantilla (mismo esqueleto).

**Riesgo (documentado en B1)**: copiar la zona de submesh de una plantilla
sobre geometría nueva → hang (offsets que no coinciden). Hay que GENERAR los
descriptores con los rangos de los buffers nuevos.

---

## 5. PENDIENTE

- Mapear los descriptores de cara (13-18, layout de material) y los de los
  AWGs de manos/caras (1-17).
- Verificar qué cubre exactamente el rango A (sec34? IB? ambos?) para poder
  generar los offsets correctos.
- Adaptar `amo0_to_awo.py` del B1 al layout B3 (vértice 44B con BONE@+28,
  descriptor submesh B3, vb2 distinto).

---

## 6. 🔴 MAPA ESTRUCTURAL COMPLETO DEL AWG0 B3 (2026-08-17, verificado)

### 6.1 Layout del AWG0 (bin b327_hd.bin, Krillin)

```
AWG0 @0xD80 (abs), magic '#AWG' en +0:
  +0x04: 0x40 (header size)
  +0x0C: 0x4 (flag B3)
  +0x10: 0x33 = 51 bones
  +0x14: 0xA10 = axes loc (rel AWG0) -> ejes @0x1790
  +0x18: 0xD = 13 mesh groups
  +0x1C: 0x40 = name offset
  +0x20: 0x6A0 = mesh group zone (rel)
  +0x24: 0xB = mesh parts count?
  +0x28: 0x2700 = sec34 rel? (se usa +0x34 en el codigo actual)
  +0x2C: 0x17868 = vb2 rel -> vb2 @0x185E8
  +0x30: 0x19F68 = IB rel -> IB @0x1ACE8
  +0x34: 0x2826 = sec34 rel -> sec34 @0x35A6
  +0x38: 0x1C790 = end rel -> end @0x1D510
  +0x40: 'XKLL_BODY' (label raíz 16B)

Ejes: 51 ejes de 0x50 (80B) @0x1790..0x2780
  +0x00..+0x0C: quaternion local [x,y,z,w]
  +0x10..+0x1C: posicion local [px,py,pz]
  +0x30: sello (0x9000020C mesh / 0x204 shadow / 0x6000020F raiz)
  +0x34: arm_ptr (rel AWG0) -> arm block
  +0x38: child_ptr | +0x3C: sibling_ptr | +0x40: parent_ptr

Arms: 51 bloques de 0x14 (20B) @0x1A00..0x1DF0
  [bone, fin, 0, ini, 0] donde bone=indice, ini/fin = BYTE offsets del IB
  Bones con mesh (ini/fin != 0): 0, 18, 25, 32, 35, 36
    bone 0:  [0, 8064, 0, 7680, 0] -> IB idx [3840..4032]
    bone 18: [18, 9328, 0, 7744, 0] -> IB idx [3872..4664]
    bone 25: [25, 9440, 0, 7808, 0] -> IB idx [3904..4720]
    bone 32: [32, 9552, 0, 7872, 0] -> IB idx [3936..4776]
    bone 35: [35, 9760, 0, 7936, 0] -> IB idx [3968..4880]
    bone 36: [36, 9872, 0, 8000, 0] -> IB idx [4000..4936]
  ⚠️ Los rangos de los arms [3840-4936] son la zona de SHADOWS del IB.
  El IB real se dibuja COMPLETO (5140 indices); los arms NO definen que
  dibujar (son refs de skinning/otra info). Item 27 AGENTS.

Mesh group @0x1420 (13 grupos de 0x40): headers con type2=0x29BD (sello B3)
  - El header real del mesh part tiene +38=0x1B5 (type1) +3C=0x29BD (type2)
  - Patron por bone con malla: 5,5,1,0x1B5,0x29BD (shadow/extra blocks)
  - Los demas headers: matrices identidad 4x4 (material)

Zona submesh data @0x2CD9..0x34D9 (19 descriptores de 0x60): ver §2-3
  - Descriptores de cuerpo (0-11): label XKLL_BODY, rango A contiguo +50/+54
  - Descriptores de cara (13-18): KLL_L00_LHAND/RHAND, XKLL_M_DTEETH/UTEETH,
    XKLL_L00_FACE (layout de material distinto)

sec34 @0x35A6: 1956 slots de 44B (layout REAL, ver AGENTS §3.2)
vb2 @0x185E8: 226 slots (cabeza/caras, layout propio, bone=0xFFFFFFFF)
IB @0x1ACE8: 5140 indices u16 (referencia sec34 0-1955 + vb2 1956-2181)
```

### 6.2 Hallazgo clave de la sesión

El **vb2 cubre el 15.4% del IB** (789 de 5140 índices = cabeza/caras) con
layout PROPIO (posiciones 0..2, no world). La inyección PS2 solo toca el
sec34 → cabeza/piernas/rodilla/pie NO se pueden arreglar por inyección.
Para eso hay que reconstruir el bin completo incluyendo vb2.

---

## 7. REFERENCIAS

- B1: `DBZ Budokai HD\docs\re\SESION11_PORT_PS2_METODOLOGIA.md` §3.
- B1: `DBZ Budokai HD\mod center hd\conversores\amo0_to_awo.py`.
- B3: `awo_tools\SESION_2026-08-17.md` §6.
- B3: `docs\VIABILIDAD_MODELOS_EXTERNOS.md`.