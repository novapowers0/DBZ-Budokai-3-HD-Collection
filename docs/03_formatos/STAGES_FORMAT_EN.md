# HD STAGE FORMAT + #SPX SCRIPT + PS2 CONTAINER (RE 2026-09-08)

> Static RE of the stage bins in `data_cmn.afs` (44-69 us / 3735-3847 HD)
> and of the IW→B3 (PS2) moveset ports, to pin down the PS2↔HD mapping.
> Verified on bin 44 (us stage, 1.9 MB decompressed) and the bins of the
> Goku GT port (`modding resources/Infinite World to Budokai 3 Moveset Ports/`).

## 1. STAGE BIN = 5-LEVEL #AMB CONTAINER

Bin 44 (us stage, 1.94 MB decompressed):

```
BE #AMB container (standard header, see ACM_FORMAT_EN.md §1) n_sub=5:
  sub 0  @0x80     1.48 MB  type=0xFFFFFFFF  #ZDD   <- stage geometry/effects
  sub 1  @0x16ACC0 240 B    type=0xFFFFFFFF  #CAD   <- camera table (n=4)
  sub 2  @0x16ADC0 256 B    type=0xFFFFFFFF  #CAS   <- camera table (n=5)
  sub 3  @0x16AEC0 9752 B   type=0x8         #SPX   <- stage SCRIPT (LE)
  sub 4  @0x16D4E0 443 KB   type=0xFFFFFFFF  #AMB   <- NESTED container
```

The **nested #AMB** (sub 4) is a SPARSE container: n_sub=641, almost all
empty entries (off=0 size=0), with real blocks grouped by "element":

```
  #ACE (Collision/Animated Collision Element)  ~25 blocks, 352-4912 B
  #ACM (stage animations)                       2-4 blocks, 2944 B
  #AWO (models) + #AZT (textures)               per element (DXT3/BC2)
  final 197 KB #AZT (sub 640, big texture)
```

> Reading: the 641-entry array is indexed by stage element (e.g. "zones"):
> each element has #ACE collision + #AWO model + #AZT texture + #ACM animation.
> Empty slots = elements without data.

The **#ZDD** (sub 0, 1.48 MB) is another container: its own header
(`#ZDD 01 01 00 00 00 00 00 00 FFFFFFFF 40 16AADC 16AC0C ...`) wrapping
#AZT (at 0x40) + #AWO/#AWG (at ~0xC1000). The stage's big geometry.

## 2. #SPX BLOCK — SCRIPT (LE, NOT BE)

```
+0x00  "#SPX ver 0.01\0"  (ASCII version string, 16 B with padding)
+0x10  u32 LE  0x20   = header size
+0x14  u32 LE  0x44   = bytecode offset (0x44 = 68)  ✅
+0x18  u32 LE  0x09   = count (9 in stage 44; 0x33=51 in the IW port)
+0x1C  u32 LE  0x00
+0x20  ...            = 2nd copy of the header (0x20 0x44 0x09 0x00)
+0x30  ...            = 16 B 0xFF (sentinel)
+0x38  00 00 00 00    = pad
+0x44  ...            = bytecode STREAM (to the end ~0x25D0)
+~0x25D0              = final table (6×u16 LE: 2 2 2 1 3 3) + debug strings:
+                       "INPUT REST = " / "INPUT WAIT = " / "SCRIPT FRAME = "
```

### 2.1 BYTECODE ENCODING (partial, 2026-09-08)

Stream of 1-byte opcodes + typed immediates. Verified encoding:

| Opcode | Format | Meaning |
|---|---|---|
| 0x08 0x10 `u8` | 3 B | load const u8 (0x00/0x01/0xff/0x15/0x3c/0xfe...) |
| 0x08 0x20 `u16` | 4 B | load const u16 (0x00c8=200...) |
| 0x09 0x30 `u32` | 6 B | load const float LE (0x3f800000=1.0, 0x00000000=0.0) |
| 0x01 0x20 `u16` | 4 B | op with u16 immediate (0x00c6=198, 0x00a7=167, 0x0106=262...) |
| 0x01 0x30 `u32` | 6 B | op with u32 immediate (0x2b9=697, 0xe4=228...) |
| 0x02 `u16` | 3 B | op with u16 immediate (0x0273/0x0280/0x0270/0x22d6...) |
| 0x12 0x10 `u8` | 3 B | subroutine / op with u8 (0x0c, 0x04, 0x1c) |
| 0x13 0x10 `u8` | 3 B | same (0x04, 0x00) |
| 0x0b | 1 B | loop/return marker (appears after 0x12/0x13) |
| 0x0a / 0x00 / 0x4b / 0x51... | 1 B | short opcodes (frequent 0x0a=446, 0x0b=382) |

- The stream is a stage command VM (camera/events): the debug strings
  ("INPUT REST/WAIT", "SCRIPT FRAME") reveal a per-frame loop polling input.
- Typical structure per sub-block: `[13 10 xx] [0b] [08 10 xx]... [01/09 30...]
  [02 u16] [12 10 xx] [0b]` → set-up + constant loads + calls.
- The repeated 1.0/0.0 floats in stage 44 (then `01 20 a7 00` = camera to
  position 167?) suggest per-frame camera parameters.
- Encoding check: the stage 44 stream parses cleanly for ~48-49
  instructions before the first ambiguous case (0x08 0x4b / opcodes 0x4b 0x51
  0x52 0x33 0x5e 0x50 = 1 byte). Full disassembly not reached.

- **#SPX is LITTLE-ENDIAN** (unlike the BE container) — it is the PS2 script
  ported without conversion. Byte histogram: 00 (24%), 01, 08, 10, 02,
  0A, 0B → compact VM opcodes.
- The same "#SPX ver 0.01" magic appears in the **IW (PS2) moveset ports**:
  the version string does not change between games.
- `type=0x8` in the #AMB container identifies the #SPX block (both in HD stages
  and the PS2 port: sub type 8 = #SPX).
- HD movesets (#CSK+#ACM bins) have NO #SPX block: the HD move script lives in
  the #CSK (see ACM_FORMAT_EN.md §4) or the gamepad merges it.

> PENDING: full opcode disassembly (the interpreter is missing: there are no
> references to "#SPX" in `generated/` — the guest dispatches by type=8). Low value
> for modding (stage camera/event scripts); the structure and encoding are
> documented.

## 3. PS2 (LE) vs HD (BE) CONTAINER — IW→B3 PORTS

The IW→B3 ports (`modding resources/.../Goku GT/IW/unnamed_359.bin` etc.) are
the original PS2 containers:

```
PS2 #AMB (LITTLE-ENDIAN):
+0x00 "#AMB" ; +0x04 0x20 (hdr size) ; +0x08 0 ; +0x0C n_files ;
+0x10 n_list ; +0x14 0x20 (table) ; +0x18 first_sub_offset
+0x20  table [off u32, size u32, type u32, pad u32] x n
```

**Exact match with HD** (same structure, LE↔BE, table at +0x20).
Example Goku GT IW (359): #AMC (0x50, type=5) + #BCMg (0x12E30, type=FFFFFFFF)
+ #SPX (0x149A0, type=8).
Example Goku GT B3 (241): #AMC (type=5) + #AML stub (type=6) + #BCMg
(type=FFFFFFFF) + #SPX (type=8).

**Container block types (constants):**

| type | Block | Role | HD |
|------|--------|---------|----|
| 0xFFFFFFFF | #BCMg / #CSK | movelist + properties + hit reactions | #CSK |
| 3 | #AMM | raw animations | #ACM |
| 5 | #AMC | camera | small CAM bin |
| 6 | #AML | stub (empty, 5 B) | — |
| 8 | #SPX | combat/stage script (LE) | #SPX (stage) |

> For the IW→HD moveset port: PS2 BCMg+BSK+AMM → HD #CSK+#ACM
> (LE→BE conversion + magic rename + table), and #AMC → CAM bin.

## 4. CONFIRMED STAGE NAMES (Pal AFL, aligned 1:1 with US 44-69)

The stage bins in `data_cmn.afs` (US, same index as Pal in this range)
are 13 maps on even entries and 13 "SE_" (map sounds/effects) on odd ones:

| Entry | AFL name | | Entry | AFL name |
|---:|---|---|---:|---|
| 44 | RED_RIBBON_BASE_MAP.amb | | 58 | TIME_BOLIC_CHAMBER_MAP.amb |
| 45 | SE_RED_RIBBON_BASE_MAP | | 59 | SE_TIME_BOLIC_CHAMBER |
| 46 | WORLD_TOUR_MAP.amb | | 60 | NAMEK_MAP.amb |
| 47 | SE_WORLD_TOUR_MAP | | 61 | SE_NAMEK_MAP |
| 48 | BUU_BODY_MAP.amb | | 62 | PLAINS_MAP.amb |
| 49 | SE_BUU_BODY_MAP | | 63 | SE_PLAINS_MAP |
| 50 | CELL_RING_MAP.amb | | 64 | DESERT_MAP.amb |
| 51 | SE_CELL_RING_MAP | | 65 | SE_DESERT_MAP |
| 52 | GRANDPA_HOUSE_MAP.amb | | 66 | CITY_MAP.amb |
| 53 | SE_GRANDPA_HOUSE_MAP | | 67 | SE_CITY_MAP |
| 54 | KAI_WORLD_MAP.amb | | 68 | DAMAGED_CITY_MAP.amb |
| 55 | SE_KAI_WORLD_MAP | | 69 | SE_DAMAGED_CITY_MAP |
| 56 | ISLAND_MAP.amb | | | |
| 57 | SE_ISLAND_MAP | | | |

> The stage select videos (`*_SELEC.sfd`) are at 3930-3937 Pal →
> on US with the +6 offset (≈3936-3943). Do NOT confuse them with the select
> portraits (US 3884-3960, guest table `0x82372818`).

## 5. NEW MAGICS RECORDED IN THE CORPUS

`#ZDD` (stage container), `#CAD` (camera table n=4), `#CAS` (camera
table n=5), `#ACE` (collision/animated element), `#AML` (stub), `#MTC`
(transform/matrix block, 2160 B, in bins 3796/3808/3812/3816/3831).
These add to #CSK/#BFC/#BCM/#BSK/#AMM/#AMC/#AST/#ASE/#AME/#AWA/#AWB/#AWBK.
To be added to the `awo_tools/corpus_scan.py` scanner.

## 6. REFERENCES

- `docs/03_formatos/ACM_FORMAT_EN.md` — BE container + #CSK/#ACM (HD moveset).
- `modding resources/Infinite World to Budokai 3 Moveset Ports/Goku GT/{IW,B3}/`
  — reference PS2 bins of the port.
- Dumps: `out/analysis/acm/entry_44.bin` (stage) + `stage44/sub0-4.bin`.
