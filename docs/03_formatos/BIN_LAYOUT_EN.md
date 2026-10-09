# HD bin layout — field by field

> Updated: 2026-08-14. Detail of the visible Krillin bin (entry 327, `rt_327.bin`).

---

## 1. AMB OVERVIEW (682528 bytes)

```
0x00000  #AMB header (0x40)
0x00040  #AWO (model) — 290784 bytes
0x47020  #AZT (textures) — 391680 bytes
```

---

## 2. AWO STRUCTURE (Krillin)

| Field | Value | Note |
|---|---|---|
| numberOfBones | 51 | XKLL_BODY, KLL_WAIST, KLL_STMC... |
| numberOfAWGs | 18 | AWG0=body, AWG1-10=fingers, AWG11-17=faces |
| pointerAWGoffsets | 0x690 | table of 18 pointers |
| ptrBoneNames | 0x6D8 | 32B labels each |

### AWGs in the bin (offset → label → counts)

| AWG | Offset | Label | Bones | vb | face |
|---|---|---|---|---|---|
| 0 | 0xD40 | XKLL_BODY | 51 | 2190 | 233 |
| 1 | 0x1D560 | KLL_L01_LHAND | 1 | 141 | 18 |
| 2 | 0x1F2A0 | KLL_L02_LHAND | 1 | 164 | 20 |
| 3 | 0x21440 | KLL_L04_LHAND | 1 | 169 | 24 |
| 4 | 0x23740 | KLL_L05_LHAND | 1 | 139 | 18 |
| 5 | 0x25440 | KLL_L10_LHAND | 1 | 191 | 27 |
| 6 | 0x27BA0 | KLL_L01_RHAND | 1 | 141 | 18 |
| 7 | 0x298E0 | KLL_L02_RHAND | 1 | 164 | 20 |
| 8 | 0x2BA80 | KLL_L04_RHAND | 1 | 169 | 24 |
| 9 | 0x2DD80 | KLL_L05_RHAND | 1 | 139 | 18 |
| 10 | 0x2FA80 | KLL_L10_RHAND | 1 | 191 | 27 |
| 11 | 0x321E0 | XKLL_L01_FACE | 1 | 173 | 23 |
| 12 | 0x34620 | XKLL_L18_FACE | 1 | 172 | 23 |
| 13 | 0x36A40 | XKLL_L09_FACE | 1 | 181 | 23 |
| 14 | 0x38FE0 | XKLL_L04_FACE | 1 | 172 | 23 |
| 15 | 0x3B400 | XKLL_L05_FACE | 1 | 173 | 23 |
| 16 | 0x3D840 | XKLL_L06_FACE | 1 | 175 | 23 |
| 17 | 0x3FCE0 | XKLL_L23_FACE | 1 | 188 | 23 |

---

## 3. NOTE ON vb vs sec34

- **vb2** (`ptrVertexBlock`, +0x28) = the MAIN vertex buffer (2190 on
  Krillin), stride 44, with the **bone index at +28**. It is the buffer that skins the body.
- **sec34** (`ptrFaceData`, +0x30) = secondary buffer (233), used for
  faces/static parts.
- Older notes called the big buffer "sec34" by mistake.
  In the official template, the big one is `VertexBlock`.

---

## 4. IB (index buffer)

- At `unk_ptr_28` (+0x38 of the AWG), `sizeOfunk_ptr_28` (+0x3C) = number of u32.
- For Krillin AWG0: 5140 indices (with 0xFFFF as restart).
- The guest draws the IB; the arms/mesh-ref define the structure.

---

## 5. VERTEX (stride 44) — VERIFIED

```
+00 nan (0xFFC00000 or similar)   +04 u  +08 v
+12 z_local  +16 x_local  +20 y_local
+24 weight  +28 bone_index (u32)  +32 nz  +36 -ny  +40 nx
```

> Verified empirically (the bone at +28 gives consistent 0-50 values; other
> positions gave nonsense).

---

## 6. COMPRESSION AND SLOT

- AFS bins: LZX `/N:2048` (magic `0F F5 12 EE`).
- Entry 327: slot = 105296 bytes → padded to 106496 (the guest reads 106496).
- A mod bin must be compressed `/N:2048` and padded to 106496.
