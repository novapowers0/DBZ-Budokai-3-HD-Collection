# HD #AMB / #CSK / #ACM FORMAT — MOVESET AND CONTAINER (RE 2026-09-08)

> Static RE of the 360 HD moveset bin (Krillin bin 333, checked against the
> corpus: all 120 `cluster=moveset` bins in `data_cmn.afs` share the structure).
> The HD #ACM is the big-endian equivalent of the PS2 AMM; the #CSK of the PS2 BSK
> (renamed, like #AMO0→#AWO). The RE started from PS2 knowledge
> (`IW_moveset_editing_notes`, NIM PDFs) + dumps of real HD bins.

## 1. GENERIC BIG-ENDIAN CONTAINER (#AMB, #CSK, #ACM)

Every HD container shares the SAME header layout:

```
+0x00  magic (4 B)                "#AMB" | "#CSK" | "#ACM" | ...
+0x04  0x00000020                 (header/table: fixed header size)
+0x08  0x00000000                 (reserved)
+0x0C  0x00000002                 (n types / flag; 4 in #CSK)
+0x10  u32  n_sub                 NUMBER OF SUB-ENTRIES in the table
+0x14  [off, size, 0, 0]          descriptor of the table itself (off=0x20, size=n_sub*0x10)
+0x20  table: n_sub × [u32 off, u32 size, u32 type, u32 pad]
```

- Sub-block offsets are **relative to the start of the container**.
- `type`: in the moveset, `0xFFFFFFFF` = #CSK block; `3` = #ACM block.
- No internal compression; the blocks are back to back.
- Example bin 333 (Krillin): `+0x10=4` → 4 sub-blocks:
  `[0x60, 0x2577C, 0xFFFFFFFF]` = #CSK · `[0x257E0, 0x140280, 3]` = #ACM1 ·
  `[0x165A60, 0x42560, 3]` = #ACM2 · `[0x1A7FC0, 0x2D60, 3]` = #ACM3.

## 2. MOVESET / ANM BIN (full structure)

```
data_cmn.afs entry (e.g. 333 Krillin, 1.75 MB decompressed) =
  #AMB container {
    #CSK   (153 KB)  = renamed PS2 BSK  -> animation properties + hit reactions
    #ACM1  (1.25 MB) = animation pool (PS2 AMM) -> pose/basic/attacks
    #ACM2  (271 KB)  = second animation pool
    #ACM3  (11 KB)   = third pool (small: face/auxiliary)
  }
```

- **120 bins** `cluster=moveset` with signature `{#AMB:1, #CSK:1, #ACM:2..3}`.
  Characters with 2 #ACM: Buu Gohan/Gotenks/Ghost/Piccolo (pool 137-140),
  Cell Jr. (165), etc.
- The **LIPS** bin (mouth animation, ~5 KB) is `#AMB → #AWO#AWG#ACM` (1 #ACM).
- The **CAM** bin is just `#AMB` (camera).
- **Stages** (44-69) and **effects** (504-555) are #AMB with sub-blocks
  `#AWO/#AWG/#AZT/#ACM/#SPX/#ACC` (see corpus).

## 3. #ACM BLOCK — ANIMATION POOL (PS2 AMM)

```
+0x00  "#ACM" ; +0x04 0x20 ; +0x08 0 ; +0x0C 2 ; +0x10 n_anim ; +0x14 0x20 ; +0x18 ? ; +0x1C ?
+0x20  animation table: n_anim × 0x10
       each entry: [0x09, variant, ?, offset]  (0x19 if there is scale)
       -> offset relative to the start of the #ACM; stride usually constant
+0x20 + n_anim*0x10
       animation blocks: each = per-bone table (0x190 B = 50 bones × 8 B)
       per bone: [ptr_angular_data u32, ptr_positional_data u32]
```

- Angular data (PS2 AMM, same BE structure): per frame
  `frame_no u16 + roll u16 + pitch u16 + yaw u16` (0x0000=0°, 0xFFFF=360°),
  rotation applied yaw→pitch→roll.
- Positional data: per frame `frame_no u32 + 3×f32` (roll/pitch/yaw).
- Krillin #ACM1: `n_anim=99` (0x63), entries with offsets `0x650, 0x7E0, 0x970,
  0xB00, 0xC90, ...` (stride 0x190 = 50-bone table).

## 4. #CSK BLOCK — ANIMATION PROPERTIES + HIT REACTIONS (PS2 BSK)

> 🔴 **Format CORRECTED 2026-09-08** (after the `krillin_dmg_test` crash):
> the earlier reading (0x10 B entries and "HR data [damage<<16|code]") was
> WRONG: the "HR list" is the **AP addresses** block (types 0-7) and the
> "HR data" are **AP blocks** (frames). The real DAMAGE lives in the HR blocks
> at the end (section +0x1C), indexed by the **HR code** of the type 1 APs.

```
+0x00  "#CSK" ; +0x04 0x20 ; +0x08 0 ; +0x0C 4 ; +0x10 n_attack_codes (0x7C7=1991)
+0x14  offset of the animation address list (0x20) ; +0x18 n_hr_blocks (0xF1=241)
+0x1C  offset of the HR section (e.g. 0x1DEFC)
+0x20  address list (4 B/entry): POSITION = attack code (0x20 + code*4)
+0x20 + n*4  animation blocks
```

### 4.1 CORRECT chain to edit a move (verified on Krillin bin 333)

1. **Attack code → animation block address**: `list[code]` (u32 at
   `0x20 + code*4`). Attack code = position in the list (same as the BCM).
   Krillin uses codes 0, 2, 0x38-0x3C, 0xEA-0xFF, 0x200+... (206 with a hitbox).
2. **Animation block** (e.g. attack 0x21b → @0x28CC): one or more
   sub-blocks `[anim u16][amm u16] + params + 0xFFFFFFFF×3 (sentinel) +
   [0000][n_ap u32][ap_addrs_off u32]`. AMM 3 = third #ACM of the bin.
3. **AP addresses block** (@ap_addrs_off, n_ap entries × 8 B):
   `[AP_type u16][n_lines u16][data_off u32]`. AP types 0-7 (0=head tracking,
   1=**HIT properties**, 2=airborne, 3=turnaround, 4=speed, 5=limb, 6=hands,
   7=misc). Several sub-blocks per attack (multi-hit, ground/air).
4. **AP type 1 (Hit properties)** — 16 B line:
   `[frame u16][ID u16][act u8][pad u8][HR_code u16][props u16][body u8][radius u8][pos x i8][pos y i8][pos z i8][pad u8]`
   - Window-closing lines have `HR=0xFFFF`.
   - `HR_code` (u16 in bytes 6-7) = HR block index.
   - `props`: 0x10=normal, 0x01=already used, 0x04=armor, 0x20=charged, 0x40=unblockable...
   - `body`: 0=WAIST, 1=STMC, 2=NECK, 3=HEAD, 16=LARM1, 17=RARM1, 18=CHEST...
5. **HR block** (8 lines × 16 B, at `hr_off + HR_code*128`), one line per
   hit situation:
   `[damage u16][grunt u8][visual u8][stun_type u16][stun_code u16][pushback f32][specific f32]`
   - Lines: 1=normal, 2=counter, 3=juggle, 4=back, 5=grounded, 6=blocking,
     7=??, 8=stunned.
   - `stun_type`: 00=normal, 01=juggle, 02=knockaway, 03=scripted (SPX), 04/05=block.
   - `specific`: juggle height (type 01) / blockstun length (04/05) /
     knockaway direction (02: 2×s16).

Real example (Krillin attack 0x21b, >P): HR blocks 28/29/30/31 with damage
59/78/68/98 (each hit of the combo), type 01 (juggle), juggle height 1.4/1.2/1.0.
Blocking (line 6) always dmg=0. HR 0x5C = knockaway: dmg 120, pushback 70.

> ⚠️ **Do NOT use the earlier "5-level chain"** (it was the AP chain, which ends
> in FRAMES, not damage). Editing frames → crash in combat (call to NULL in
> `sub_820800A8`).

### 4.2 Tools

- `awo_tools/csk_chain.py` — walks the CORRECT chain: `--scan` lists the
  attack codes with a hitbox, `--code <hex>` shows the full chain (AP and HR
  codes), `--hr <hex>` shows the HR block (8 lines with damage/stun/pushback).
- `awo_tools/csk_edit.py` — edits DAMAGE: `--attack <hex> --damage N [--line]`
  or `--hr <hex> --damage N`; `--install` packs the override (LZX /N:2048).
  Verified: attack 0x21b → damage 100 in HR blocks 28/29/... with the structure
  intact (the chain still parses after the patch).

## 5. 🔴 PS2 → HD MAPPING (IDENTITY CONFIRMED 2026-09-08)

**The PS2 moveset bin (GH, LE) and the HD one (BE) are the SAME file.**
Verified with Krillin e333: identical sizes (BSK 153468 = #CSK 153468;
AMM1 1311344 ≈ #ACM1 1311360; AMM2 271712 = #ACM2 271712) and equal internal
offsets. PS2 GH numbering = HD numbering (3990 entries in both
`data_cmn.afs`). The LE/BE diff IS the parser (A4):

| PS2 (LE) | HD 360 (BE) | Content |
|---|---|---|
| AMB | #AMB | generic container (same header) |
| BSK | #CSK | animation properties + hit reactions (damage/stun/pushback) |
| AMM | #ACM | raw animations (roll/pitch/yaw per bone) |
| BCM/SPX/AMC | (to RE) | movelist/scripts/camera — still to locate (in #CSK?) |

> The HD #ACM1 differs from the PS2 AMM1 by 16 B (1311360 vs 1311344): a different
> header field (version or pad). Check before a byte-exact port.
> Extracting the PS2 BSK/AMM of any GH character gives the LE reference to
> decode the HD #CSK/#ACM of the same character (same indices).

## 6. WHAT THIS ENABLES

1. **Moveset swap by blob** (already in `swap_matrix.py --type moveset`): moving the
   whole #AMB bin between slots — works without crashing, BUT combos/attacks
   stay mapped to the destination's attack codes (if moveset and BCM do not
   share numbering, attacks do not match: validated 2026-09-08,
   Krillin→Tien without a crash but with "corrupt" combos). A correct swap
   would need mapping the attack codes (BCM) or editing the destination #CSK.
2. **Editing a move** (S3, CORRECTED 2026-09-08): `awo_tools/csk_edit.py`
   patches an attack code's **damage** following the correct chain
   (attack → AP type 1 → HR code → HR block → `damage u16` of lines 1-8).
   Verified: attack 0x21b (>P combo) → HR blocks 28/29/... with damage 100 and
   the structure intact (the chain still parses).
   ```powershell
   python awo_tools/csk_edit.py --entry 333 --attack 21b --damage 100 --mod krillin_dmg_test --install
   python awo_tools/csk_edit.py --entry 333 --hr 5c --damage 80   # HR block directly
   ```
   Preview first: `python awo_tools/csk_chain.py --entry 333 --scan`
   (lists the attack codes with a hitbox) and `--code <hex>` (chain) / `--hr <hex>`.
   ⚠️ The earlier csk_edit (which patched AP FRAMES) crashed combat
   (NULL in sub_820800A8) — fixed, do not reuse the old bin.
   ⚠️ If the recompressed bin GROWS the entry (bigger than `to_read`), the runtime
   rebuilds the AFS physically (fix 2026-09-09, `AfsRebuildPath`); without that
   fix the crash appeared in the select (read with an old offset → garbage magic
   #ACP with no handler).
3. **Adding animations**: create new #ACM blocks in the container (n_sub and
   table), as long as the bin fits in `to_read` or uses the virtual mid-insert.
4. **Corpus**: bins with #CSK are now classified as real movesets
   (120 in data_cmn); #CSK is the signature that tells a moveset from a stage.

## 7. TOOLS

- `awo_tools/acm_parse.py` — extracts the bin, decompresses LZX, lists the
  #AMB container and dumps sub-blocks (#CSK/#ACM).
- `awo_tools/acm_analyze.py` — analyses an #ACM block (animation table,
  bone blocks).
- `awo_tools/csk_chain.py` — #CSK RE (CORRECT format): `--scan` attack
  codes with a hitbox, `--code <hex>` full chain (AP + HR codes), `--hr <hex>`
  HR block with damage/stun/pushback per situation.
- `awo_tools/csk_edit.py` — EDITS damage (attack → HR block → damage u16) and
  packs the override (`--install`). See §6.
- `awo_tools/csk_analyze.py` — ⚠️ OUTDATED (AP/Frames chain, NOT damage).
  Use `csk_chain.py`.
- `awo_tools/corpus_scan.py` — classifies bins by magics (#CSK/#ACM/#SPX/...).

## 8. PENDING

- ✅ CORRECT edit chain decoded (attack → AP1 → HR code → HR block →
  damage) + `csk_edit.py` tool (§4.1/§4.2).
- ✅ PS2 BSK/AMM = HD #CSK/#ACM identity confirmed by size diff (§5).
- Fine semantics of stun_code (stun/juggle animations) and of the `specific` field of
  each type (knockaway = direction 2×s16): cross-check with BSK_breakdown.pdf.
- Locate the **BCM** (movelist) to map combos → attack codes (needed for correct
  moveset swaps between characters).
- Check the 2 extra #ACM blocks and the #CSK of the pools (137-140).
- Document #SPX (scripts) and #ACC (audio?) once their role in stages is confirmed.
