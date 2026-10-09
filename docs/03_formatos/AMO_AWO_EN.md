# Model format — PS2 (#AMO0) vs HD (#AWO)

> Updated: 2026-08-14. Accessible summary of the format. Full detail in `AWO_FORMAT.md` (root).

---

## 1. SUMMARY

The Xbox 360 `#AWO` IS the same PS2 `#AMO0`/`#AMG` model, repackaged:
- **Little-endian** (PS2) → **Big-endian** (360)
- Renamed magics: `#AMO0`→`#AWO`, `#AMG`→`#AWG`, `#AMT`→`#AZT`
- Different layout: sequential blocks → offset table

**NO re-rigging**: same bones, same pose matrices (51/51 identical
on Krillin).

---

## 2. AMB CONTAINER

```
#AMB
  +0x0C entry_count
  +0x20 table: (loc u32, size u32) × entry_count
  entry0: #AWO  (model)
  entry1: #AZT  (textures)
```

---

## 3. AWO HEADER

| Offset | Field |
|---|---|
| +0x10 | numberOfBones (51 on Krillin) |
| +0x14 | ptrtoConnections (hierarchy) |
| +0x18 | numberOfAWGs (18 on Krillin) |
| +0x1C | pointerAWGoffsets (AWG offset table) |
| +0x24 | ptrBoneNames |
| +0x30 | AWOunk[bones] (32B each = bone zones) |

Then: `AWGptr[numberOfAWGs]` table + `BoneNames[numberOfBones]` (32B each).

---

## 4. AWG HEADER (one mesh group)

| Offset | Field |
|---|---|
| +0x10 | numberOfBones |
| +0x14 | rigging_data_ptr |
| +0x1C | ptrBones |
| +0x24 | unk_Count (80B blocks = axis zones) |
| +0x28 | ptrVertexBlock (vb2, vertex buffer) |
| +0x2C | VertexBlockSize (size in bytes) |
| +0x30 | ptrFaceData (sec34) |
| +0x34 | FaceDataSize |
| +0x38 | unk_ptr_28 (IB) |
| +0x3C | sizeOfunk_ptr_28 (IB count) |

**IMPORTANT**: the vertex/index "counters" are NOT direct fields —
they are SIZES in bytes (+0x2C, +0x34) and uint32 counts (+0x3C). The number of
vertices = size / stride (44).

---

## 5. HD VERTEX (stride 44)

```
+00 nan (flag)    +04 u     +08 v
+12 z_local       +16 x_local  +20 y_local
+24 weight        +28 BONE(u32)  +32 nz  +36 -ny  +40 nx
```

- `+28` is the **bone index** (u32). Critical: write it right (0 = BODY).
- Positions are **local to the bone** (the guest skins with the bone matrix).

---

## 6. PS2 FORMAT (to read source models)

### 6.1 PS2 vertex (48 bytes, type B5)
```
+00 pos XYZ (3×f32 LE)   +0C null   +10 normal XYZ
+1C null   +20 UV (2×f32)   +28 null×8
```
Other types: B4=32B facial, 90=16B shadows, 199=32B without UV.

### 6.2 PS2 submesh (no explicit index buffer)
```
0x20 header: FaceType at +0x10 (1=triangle strip, 0=triplet), VertCount at +0x14
then VertCount vertices of the part's type
```
- FaceType 1 = triangle strip (alternating zig-zag winding)
- FaceType 0 = triplets (every 3 vertices = 1 triangle)

### 6.3 PS2 mesh part
```
0xA0 header: MeshType[8] (first byte = vertex type), +0x90 = mesh_size flag
   (mesh_size = (flag-0x60000000)*16, skips the part)
```
The vertex stride comes from MeshType[1].

---

## 7. READING TOOLS

| Tool | Reads |
|---|---|
| `awo_tools/analyze_bin_hd.py` | Historical PS3 parser; obsolete |
| `awo_tools/awg_to_obj_b3.py` | Recommended exporter for B3 HD bins |
| `awo_tools/awg0_export.py` | Recommended AWG0 exporter for A/C formats |
| `awo_tools/parse_ps2_mesh.py` | PS2 mesh (#AMO0) → verts+IB |
| `modding resources discord\research\B3_AMB_PS3.bt` | 010 Editor template (reference) |
| `modding resources discord\research\00000002...b3.AMO.json` | aerithdevs intermediate format |
