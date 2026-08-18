# Formato del modelo — PS2 (#AMO0) vs HD (#AWO)

> Actualizado: 2026-08-14. Resumen accesible del formato. Detalle completo en `AWO_FORMAT.md` (raíz).

---

## 1. RESUMEN

El `#AWO` de Xbox 360 ES el mismo modelo `#AMO0`/`#AMG` de PS2, re-empaquetado:
- **Little-endian** (PS2) → **Big-endian** (360)
- Magics renombrados: `#AMO0`→`#AWO`, `#AMG`→`#AWG`, `#AMT`→`#AZT`
- Layout distinto: bloques secuenciales → tabla de offsets

**NO hay re-rigging**: mismos huesos, mismas matrices de pose (51/51 idénticas
en Krillin).

---

## 2. CONTENEDOR AMB

```
#AMB
  +0x0C entry_count
  +0x20 tabla: (loc u32, size u32) × entry_count
  entry0: #AWO  (modelo)
  entry1: #AZT  (texturas)
```

---

## 3. HEADER AWO

| Offset | Campo |
|---|---|
| +0x10 | numberOfBones (51 en Krillin) |
| +0x14 | ptrtoConnections (jerarquía) |
| +0x18 | numberOfAWGs (18 en Krillin) |
| +0x1C | pointerAWGoffsets (tabla de offsets de AWGs) |
| +0x24 | ptrBoneNames |
| +0x30 | AWOunk[bones] (32B c/u = zonas de hueso) |

Luego: tabla `AWGptr[numberOfAWGs]` + `BoneNames[numberOfBones]` (32B c/u).

---

## 4. HEADER AWG (un mesh group)

| Offset | Campo |
|---|---|
| +0x10 | numberOfBones |
| +0x14 | rigging_data_ptr |
| +0x1C | ptrBones |
| +0x24 | unk_Count (bloques de 80B = zonas de ejes) |
| +0x28 | ptrVertexBlock (vb2, buffer de vértices) |
| +0x2C | VertexBlockSize (tamaño en bytes) |
| +0x30 | ptrFaceData (sec34) |
| +0x34 | FaceDataSize |
| +0x38 | unk_ptr_28 (IB) |
| +0x3C | sizeOfunk_ptr_28 (conteo IB) |

**IMPORTANTE**: los "contadores" de vértices/índices NO son campos directos —
son TAMAÑOS en bytes (+0x2C, +0x34) y conteos de uint32 (+0x3C). El nº de
vértices = tamaño / stride (44).

---

## 5. VÉRTICE HD (stride 44)

```
+00 nan (flag)    +04 u     +08 v
+12 z_local       +16 x_local  +20 y_local
+24 peso          +28 BONE(u32)  +32 nz  +36 -ny  +40 nx
```

- El `+28` es el **bone index** (u32). Crítico: escribirlo bien (0 = BODY).
- Posiciones **locales al hueso** (el guest skinnea con la matriz del hueso).

---

## 6. FORMATO PS2 (para leer modelos fuente)

### 6.1 Vértice PS2 (48 bytes, tipo B5)
```
+00 pos XYZ (3×f32 LE)   +0C null   +10 normal XYZ
+1C null   +20 UV (2×f32)   +28 null×8
```
Otros tipos: B4=32B faciales, 90=16B sombras, 199=32B sin UV.

### 6.2 Submesh PS2 (sin index buffer explícito)
```
header 0x20: FaceType en +0x10 (1=triangle strip, 0=triplete), VertCount en +0x14
luego VertCount vértices del tipo del part
```
- FaceType 1 = triangle strip (winding alternado zig-zag)
- FaceType 0 = tripletes (cada 3 vértices = 1 triángulo)

### 6.3 Mesh part PS2
```
header 0xA0: MeshType[8] (primer byte = tipo vértice), +0x90 = flag mesh_size
   (mesh_size = (flag-0x60000000)*16, saltar el part)
```
El stride del vértice lo da MeshType[1].

---

## 7. HERRAMIENTAS DE LECTURA

| Herramienta | Lee |
|---|---|
| `awo_tools/analyze_bin_hd.py` | Bin HD (#AWO) con template oficial |
| `awo_tools/parse_ps2_mesh.py` | Malla PS2 (#AMO0) → verts+IB |
| `modding resources discord\research\B3_AMB_PS3.bt` | Template 010 Editor (referencia) |
| `modding resources discord\research\00000002...b3.AMO.json` | Formato intermedio aerithdevs |
