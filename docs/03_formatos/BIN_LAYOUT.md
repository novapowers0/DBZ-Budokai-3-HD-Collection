# Layout del bin HD — campo a campo

> Actualizado: 2026-08-14. Detalle del bin Krillin visible (entrada 327, `rt_327.bin`).

---

## 1. VISTA GENERAL DEL AMB (682528 bytes)

```
0x00000  #AMB header (0x40)
0x00040  #AWO (modelo) — 290784 bytes
0x47020  #AZT (texturas) — 391680 bytes
```

---

## 2. ESTRUCTURA DEL AWO (Krillin)

| Campo | Valor | Nota |
|---|---|---|
| numberOfBones | 51 | XKLL_BODY, KLL_WAIST, KLL_STMC... |
| numberOfAWGs | 18 | AWG0=cuerpo, AWG1-10=dedos, AWG11-17=caras |
| pointerAWGoffsets | 0x690 | tabla de 18 punteros |
| ptrBoneNames | 0x6D8 | labels de 32B c/u |

### AWGs del bin (offset → label → conteos)

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

## 3. NOTA SOBRE vb vs sec34

- **vb2** (`ptrVertexBlock`, +0x28) = el buffer PRINCIPAL de vértices (2190 en
  Krillin), stride 44, con **bone index en +28**. Es el buffer que skinnea el cuerpo.
- **sec34** (`ptrFaceData`, +0x30) = buffer secundario (233), usado para
  caras/partes estáticas.
- El AGENTS llamaba "sec34" al buffer grande por error de nomenclatura.
  En la template oficial, el grande es `VertexBlock`.

---

## 4. IB (index buffer)

- En `unk_ptr_28` (+0x38 del AWG), `sizeOfunk_ptr_28` (+0x3C) = nº de u32.
- Para Krillin AWG0: 5140 índices (con 0xFFFF como restart).
- El guest dibuja el IB; los arms/mesh-ref definen la estructura.

---

## 5. VÉRTICE (stride 44) — VERIFICADO

```
+00 nan (0xFFC00000 o similar)   +04 u  +08 v
+12 z_local  +16 x_local  +20 y_local
+24 peso  +28 bone_index (u32)  +32 nz  +36 -ny  +40 nx
```

> Verificado empíricamente (el bone en +28 da valores 0-50 coherentes; en otras
> posiciones daba absurdos).

---

## 6. COMPRESIÓN Y SLOT

- Bins del AFS: LZX `/N:2048` (magic `0F F5 12 EE`).
- Entrada 327: slot = 105296 bytes → padded a 106496 (el guest lee 106496).
- El bin del mod debe comprimirse `/N:2048` y paddearse a 106496.
