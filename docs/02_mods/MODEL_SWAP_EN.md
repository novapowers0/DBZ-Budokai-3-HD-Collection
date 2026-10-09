# MODEL SWAP — Full research

> Updated: 2026-08-14. What we know, what fails, and what the community documentation says
> about how model swaps were done in the original B3.
>
> Note: since this research, native B3 → B3 swaps work (`swap_b3.py`, the Model swap card in
> the Mod Kit and the launcher's Model Swap tab). This page keeps the history of how we got there.

---

## SUMMARY

The **model swap** (putting one character's model in another's slot) is the goal.
Status at the time: **the override mechanism works** (the bin is served whole),
but **the guest crashed when processing another character's bin**. The research
was still open.

---

## 1. WHAT WE KNOW WORKS

| Technique | Result |
|---|---|
| Replace Krillin's bin with **the same bin** (afstest) | ✅ Loads fine |
| **Texture** mod (#AZT only) | ✅ Works |
| **Per-entry override** (mechanism) | ✅ The bin is served whole |
| Replace the bin with **another character's** (Goten→Krillin) | 🔴 Crashes |
| Inject **another character's body** into the slots (Goten body→Krillin) | 🔴 Crashes |

> Key conclusion: the guest does NOT simply accept another character's bin.
> The problem is NOT the mechanism, it is the CONTENT/structure of the bin.

---

## 2. WHAT THE COMMUNITY DOCUMENTATION SAYS

### 2.1 LGBT Method (Lean's Ginyu Bodyswap Technique) — `modding resources discord\tutorials\LGBT_Method.zip`

The community method for bodyswaps in B3 PS2:

1. **They NEVER replace the character's whole bin**.
2. Find which **axes** are needed: for legs `WAIST STMC RLEGROT RLEG1 RLEG2 RFOOT1 RFOOT2 LLEGROT LLEG1 LLEG2 LFOOT1 LFOOT2`; for the body `WAIST CHEST STMC RCHN RARMROT...`.
3. Find the **model parts** of those axes in the donor.
4. Copy the parts to the receiver, adjusting offsets and pointers.
5. Adjust texture/shader.

**Lesson**: the swap is selective by axes, not a whole-bin swap.

### 2.2 Tutorial "Adding an AMG by hand" (JaromSc) — `modding resources update 2\Tutorial #1 Añadir AMG manualmente`

1. Copy the donor's AMG (mesh part) → paste it at the end of the base model.
2. Edit the **file length** and **part count** in the header.
3. Copy the **bone names** (from "Body" to the last one).
4. Find the target **bone offset** (e.g. NH=0x1E) and replace its pointer with the new AMG location.
5. Assign texture and shader.

**Lesson**: adding a part means adjusting length, part count, bone pointers, texture/shader.

### 2.3 The community has NO PS2→HD converter

- All community tooling (OBJ to AMG, Model Rig Toolset, AMO Decompiler)
  works with the **PS2 (#AMO0 LE)** format.
- The HD format (#AWO BE) is edited with 010 Editor + the `B3_AMB_PS3.bt` template.
- The PS2→HD jump is a **re-layout** (endianness + magics + offset table),
  not a different format. Documented in AWO_FORMAT.md.

---

## 3. WHAT WE VERIFIED BY RE

### 3.1 HD bin structure (with the B3_AMB_PS3.bt template)

```
AMB: #AMB + entry table (loc+size)
  entry0: #AWO (model)
  entry1: #AZT (textures)

AWO: +0x10 numberOfBones, +0x14 ptrConnections, +0x18 numberOfAWGs,
     +0x1C pointerAWGoffsets, +0x24 ptrBoneNames,
     +0x30 AWOunk[bones](32B) → bone zones, + AWGptr table + BoneNames

AWG (per mesh group): +0x10 numberOfBones, +0x14 rigging_data_ptr,
     +0x1C ptrBones, +0x24 unk_Count(80B blocks), +0x28 ptrVertexBlock,
     +0x2C VertexBlockSize, +0x30 ptrFaceData, +0x34 FaceDataSize,
     +0x38 unk_ptr_28, +0x3C sizeOfunk_ptr_28
```

### 3.2 Krillin vs Goten (both HD, same game)

| | Krillin (entry 327) | Goten (entry 298) |
|---|---|---|
| Bones | 51 | 56 |
| AWGs | 18 | 21 |
| AWG0 (body) | vb=2190, face=233 | vb=2035, face=225 |
| Fingers | 10 (L01-L10 L/R) | 12 (L01-L38 L/R) |
| Faces | 7 (L01-L23) | 8 (L01-L41 S00) |
| Labels | KLL_*/XKLL_* | GTN_*/XGTN_* |

**Conclusion**: almost identical structure (same pattern), they differ in counts and labels.
A humanoid from the same game should be compatible... but it crashed.

### 3.3 The HD vertex (stride 44)

```
+00 nan (flag)  +04 u  +08 v
+12 z_local     +16 x_local   +20 y_local
+24 weight      +28 BONE(u32)  +32 nz  +36 -ny  +40 nx
```

### 3.4 The 3 override fixes (critical)

See [COMO_HACER_MODS_EN.md](COMO_HACER_MODS_EN.md). Summary:
1. The hook must support folders (ported from B1).
2. Compression `/N:2048` (not /N:32).
3. Padding to the exact slot size.

---

## 4. CRASH HYPOTHESES (at the time)

Once the override was confirmed to work and serve the whole bin, the crash when
loading another character's bin could be due to:

| Hypothesis | Explanation | How to check |
|---|---|---|
| **A. The guest validates labels/counts** | Krillin's moveset/animations refer to KLL_* bones by index; Goten's bin uses GTN_* | Compare how the guest indexes bones |
| **B. The guest uses the slot's mesh group** | Slot 327 expects a certain mesh-ref block structure | Instrument the guest parser |
| **C. The crash is in another field** | Some internal AWG offset does not match | Instrument the guest |

### 4.1 Confirmed: the crash is NOT LZX truncation (2026-08-14)

The last `goten_body` test (Goten's body on Krillin, LZX `/N:2048`,
padded to 106496) logged:
```
AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=106496
UNHANDLED EXCEPTION: Code=0xC0000005 Addr=0x7ff7bdfe87ee
```
→ The bin was served **whole** (got=106496, the complete LZX) and it still crashed.
→ The crash comes from the **bin content** (the model geometry/structure),
  not the override mechanism or the compression.

### 4.2 Finding: rigData differs between characters (2026-08-14)

Comparing the `rigData` (pose matrices) of Krillin's vs Goten's AWG0:

| Bone | Krillin | Goten |
|---|---|---|
| bone 2 | scale=(0, 0, 0) | scale=(-0.7071, -0.7071, 0) |
| bone 5 | pos=(-0.33, 0, 0.52) | pos=(2.30, 0, 0) |

→ **Each character has its own rigData** (bone position/rotation/scale).
→ The guest in slot 327 expects Krillin's skeleton. Goten's bin brings
  another rig → mismatch → crash.
→ **A model swap is NOT copying geometry**: the donor geometry must be transformed into the
  receiver's skeleton SPACE (`donor_local → world → receiver_local`).
→ This invalidated the earlier assumption of "identical world matrices" (that only held
  for the SAME character PS2 vs HD).

---

## 5. NEXT STEPS (at the time: real RE of the guest)

The community documentation does NOT cover whole-bin swaps between B3
characters (they never did it). Moving forward needed **reverse engineering of the guest
parser**, which lives in `generated/dbz3_recomp.*.cpp`:

1. **Find the function that parses bin 327** in the recompiled guest code
   (the crash address `0x7ff7...` points there).
2. **Instrument**: log which offsets the guest reads from the bin (as with
   `AFS327 READ`, but at model-parsing level).
3. **Compare** the parsing flow of the original bin vs Goten's: see
   exactly which field causes the crash.

### Tools available for this RE
- `awo_tools/awg_to_obj_b3.py` — recommended exporter to check B3 HD bins
- `awo_tools/awg0_export.py` — AWG0 exporter with A/C autodetection
- `awo_tools/analyze_bin_hd.py` — historical PS3 parser, obsolete
- `generated/dbz3_recomp.*.cpp` — recompiled guest code (the real parser)
- `rexglue-sdk-0.10/` — instrumentable runtime (C++), where the hook lives
- Tracy profiling (`win-amd64-tracy` build)
- `mod center hd/` — HD tools we created

---

## 6. REFERENCES

- `AWO_FORMAT.md` (root) — full AFS/AFL/LZX/#AMB/#AWO format
- `modding resources discord\tutorials\LGBT_Method.zip` — bodyswap method
- `modding resources update 2\Tutorial #1 Añadir AMG manualmente` — adding an AMG
- `modding resources discord\research\B3_AMB_PS3.bt` — 010 template of the format
- `modding resources discord\research\00000002-00000002-b3.AMO.json` — aerithdevs intermediate format
- `mod center\OBJ to AMG v0.92` — PS2 OBJ→AMG pipeline
- `mod center\Model Rig Toolset V0.6` — PS2 rig
