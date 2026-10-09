# HD ROSTER MAP — character → bins (data_cmn.afs)

> Date: 2026-09-07. Result of Phase 3.3 (map roster + select).
> Merges: `mod center hd/catalog_b3.cat` (models), a real probe of the
> AFS (`out/analysis/probe_result.txt`), the Pal `data_cmn.afl` (names) and
> `modding resources/DBZ_B3_Character_Bin_List.txt` (community groupings).
> Verified empirically by decompressing entries with xbdecompress.

## 1. HOW TO READ THESE TABLES

- **Model** = bin `#AMB1 #AWO1 #AWG15-26 #AZT1` (stride 44, B3 layout).
- **CAM** = camera bin (small, ~20-110 KB, only `#AMB`).
- **LIPS** = mouth animation bin (~5 KB, `#AWO#AWG#ACM`).
- **ANM** = animation/moveset bin (big, `#ACM#AMB`, 1.2-2.4 MB decompressed).
- **SCOUT** = scouter accessory (~5-6 KB).
- **AURA** = character aura bin (entries 0-43).
- Model bins are **self-contained** (format A/B/C autodetected by the
  guest); native HD→HD swaps already rely on that.

## 2. NUMBERING (validated empirically)

The 360 HD uses the `data_cmn` numbering of the **PS2 Greatest Hits**. The
`data_cmn.afl` name list (**Pal** edition, `modding resources/`) matches the
HD **exactly** up to entry 286 and from ~293 on with an offset of
**+6** (the GH added 6 extra "angelic" Goku models, 281-287). Local drift
breaks the offset around Recoome/Raditz/Saibaman and Trunks; in those zones the
**HD catalog (`catalog_b3.cat`) is the authority** for models.

Rule of thumb for auxiliary names: `name = AFL[entry - 6]` for
entries ≥ 287, **except** in the drift zones (358-375, 381-395), where you
use the probe type + position within the group.

**🔴 Portrait offset (IMG region, entry ≥ 3839)**: `US = PAL + 45`.
PAL 3839-3917 (`*_IMG_0/_1.amt`) → US 3884-3962. This offset names
EVERY select slot (§7) — verified with the ones confirmed in game
(Krillin US 3930, Nappa US 3934).

## 3. AURAS (entries 0-43, in character order)

| # | Name | Character |
|---:|---|---|
| 0 | 16G_AURA | Android 16 |
| 1 | 17G_AURA | Android 17 |
| 2 | 18G_AURA | Android 18 |
| 3 | 1-STAR-DRAGON_AURA | Syn Shenron |
| 4 | 20G_AURA | Dr. Gero |
| 5 | BARDOCK_AURA | Bardock |
| 6 | BROLY_AURA | Broly |
| 7 | BUU_LONG_AURA | Majin Buu (fat) |
| 8 | BUU_MED_AURA | Super Buu |
| 9 | BUU_SMALL_AURA | Kid Buu |
| 10 | CELL_AURA | Cell |
| 11 | CELL_Jr._AURA | Cell Jr. |
| 12 | LOADING_AURA | loading aura |
| 13 | COOLER_AURA | Cooler |
| 14 | DABURA_AURA | Dabura |
| 15 | FREEZA_AURA | Freeza |
| 16 | GOGETA_0_AURA | Gogeta SSJ |
| 17 | GOGETA_1_AURA | Gogeta SSJ4 |
| 18 | GOHAN_LONG_AURA | Adult Gohan |
| 19 | GOHAN_MED_AURA | Cell saga Gohan |
| 20 | GOHAN_SMALL_AURA | Kid Gohan |
| 21 | GOKU_SMALL_AURA | Kid Goku |
| 22 | GINYU_AURA | Ginyu |
| 23 | GOKU_AURA | Goku |
| 24 | GOKU_GINYU_AURA | Goku (Ginyu body) |
| 25 | GREAT_SAIYAMAN_AURA | Great Saiyaman |
| 26 | GOTEN_AURA | Goten |
| 27 | GOTENKS_AURA | Gotenks |
| 28 | KULILIN_AURA | Krillin |
| 29 | KAIO-SHIN_AURA | Kaio-Shin |
| 30 | NAPPA_AURA | Nappa |
| 31 | PICCOLO_AURA | Piccolo |
| 32 | RADITZ_AURA | Raditz |
| 33 | RECOOME_AURA | Recoome |
| 34 | SAIBAIMAN_AURA | Saibaman |
| 35 | SATAN_AURA | Mr. Satan |
| 36 | TRUNKS_SMALL_AURA | Kid Trunks |
| 37 | TRUNKS_LONG_AURA | Future Trunks |
| 38 | TENSHINHAN_AURA | Tien |
| 39 | UUB_AURA | Uub |
| 40 | VIDEL_AURA | Videl |
| 41 | VEGETA_AURA | Vegeta |
| 42 | VEGETTO_AURA | Vegito |
| 43 | YAMCHA_AURA | Yamcha |

> Auras share "LOADING_AURA" as a pattern; an aura mod replaces the
> whole bin (same mechanism as the model, per-entry override).

## 4. CHARACTERS — MODEL + AUXILIARIES (final HD bins)

> Model numbers = AFS entry (verified by decompression). The
> CAM/LIPS/ANM/SCOUT columns = AFS entry of the auxiliary (probe + Pal names).
> `—` = the character has no bin of its own (shares the group pool).

| Character | Models | CAM | LIPS | ANM | Scout |
|---|---:|---:|---:|---:|---:|
| Android 16 | 70, 71 | 72 | 73 | 74 | — |
| Android 17 | 75, 76 | 77 | 78 | 79 | — |
| Android 18 | 80, 81, 82 | 83 | 84 | 85 | — |
| Syn Shenron | 86, 87 | 88 | 89 | 90 | — |
| Dr. Gero | 91, 92 | 93 | 94 | 95 | — |
| Babidi | 96 | — | 97 | — | — |
| Bardock | 99, 100 | 101 | 102 | 103 | 98 |
| Buu Gohan | 104, 105 | — | 106 | pool 137-140 | — |
| Buu Gotenks | 107, 108 | — | 109 | pool 137-140 | — |
| Bulma | 110 | 111 | 112 | 113 | — |
| Buu Ghost | 114 | — | 115 | pool 137-140 | — |
| Buu Piccolo | 116, 117 | — | 118 | pool 137-140 | — |
| Broly | 119-122 | 123 | 124, 125 | 126, 127 | — |
| Majin Buu (fat) | 128, 129 | 130 | 131 | 132 | — |
| Super Buu | 133, 134 | 135 | 136 | pool 137-140 | — |
| Kid Buu | 141, 142 | 143 | 144 | 145 | — |
| Cell | 146-151 | 152 | 153-155 | 156-159 | — |
| Cell Jr. | 160, 161 | 162 | 163 | 164 (165 pool) | — |
| Cooler | 166-171 | 172 | 173 | 174, 175 | — |
| Dabura | 176, 177 | 178 | 179 | 180 | — |
| Freeza | 181-193 | 194 | 195-198 | 199-213 | — |
| Veku (fat Gogeta) | 214, 215 | 222 | 216, 221 | 223, 224 | — |
| Gogeta | 217-220 | 222 | 221 | 223, 224 | — |
| Adult Gohan | 225-230 | 231 | 232 | 233, 234 | — |
| Cell saga Gohan | 235-240 | 241 | 242, 243 | 244, 245 | — |
| Kid Gohan | 246-248 | 249 | 250 | 251 | — |
| Kid Goku | 252, 253 | 254 | 255 | 256 | — |
| Ginyu | 258, 259 | 260 | 261 | 262 | 257 |
| Goku (all forms) | 264-287 | 288 | 289 | 290-292 | 263 |
| Great Saiyaman | 293, 294 | 295 | 296 | 297 | — |
| Goten | 298-301 | 302 | 303 | 304 | — |
| Gotenks | 305-310 | 317 | 311, 316 | 318-324 | — |
| Ghost Gotenks | 312, 313 | 317 | 316 | 318-324 | — |
| Fat Gotenks | 314, 315 | 317 | 316 | 318-324 | — |
| Kibito | 325 | — | 326 | — | — |
| **Krillin** | **327, 328, 329** | **330** | **331** | **332, 333** | — |
| Kibito Kai | 334, 335 | 336 | 337 | 338 | — |
| Kaio-Shin | 339, 340 | 341 | 342 | 343 | — |
| Nappa | 345, 346 | 347 | 348 | 349 | 344 |
| Piccolo | 350-353 | 354 | 355, 356 | 358 | 357 |
| Raditz | 360, 361 | 362 | 363 | 364 | — |
| Recoome | 366, 367 | 368 | 369 | 370 | — |
| Saibaman | 371, 372 | 373 | 374 | 375 | — |
| Mr. Satan | 376 (hair), 377, 378 | 379 | 380 | 381, 382 | — |
| Kid Trunks | 383-386 | 387 | 388 | 389 | — |
| Future Trunks | 390-395 | 396 | 397 | 398, 399 | — |
| Tien | 400, 401 | 402 | 403 | 404 | — |
| Uub | 405, 406 | 407 | 408 | 409 | — |
| Videl | 410, 411 | 412 | 413 | 414 | — |
| Vegeta | 416-429 | 430 | 431, 432 | 433-435, 437 | — |
| Vegito | 438-441 | 443 | 442 | 444 | — |
| Yamcha | 445-447 | 448 | 449 | 450 | — |

> ⚠️ Entries without a catalog line but with a real model (probe): `237` = GOHAN_MED_2_00
> (Cell saga Gohan SSJ2/mystic), `366`/`367` = Recoome, `371`/`372` = Saibaman,
> `376` = Mr. Satan's hair. The catalog skips 237 and shifts some Recoome/Saibaman
> labels (359, 363 are scouters/accessories, not models).

## 5. EFFECTS (451-505) AND LOOSE TEXTURE RANGE

- **451-503**: 52 standalone `#AZT` blocks (128 KB-1 MB decompressed) — textures of
  effects/skills/HUD. `481` (~1 MB) is in this group.
- **504-555**: per-character effect bins `#AMB#AZT` (the community list
  names them: 504 Fighting effects, 505 Android 16 ... 555 Yamcha).

## 6. SELECT PORTRAITS (`#AZT1` candidates 3884-3960)

17 candidates extracted (all `288x352`, different hashes):
`3892, 3898, 3900, 3902, 3912, 3918, 3922, 3924, 3925, 3930, 3934, 3938,
3940, 3942, 3952, 3958, 3960` (`out/analysis/azt_exports/`).

**Time-based** association from the trace, medium confidence — §7 below resolves
them all by name:

| Portrait | Next model | Character |
|---:|---:|---|
| 3930 | 327 | Krillin |
| 3952 | 400 | Tien |
| 3960 | 445 | Yamcha |
| 3958 | 416 | Vegeta |
| 3938 | 350 | Piccolo |
| 3940 | 360 | Raditz |
| 3934 | 345 | Nappa |
| 3922 | 258 | Ginyu |
| 3942 | 366 | Recoome |
| 3912 | 181 | Freeza form 1 |
| 3892 | 91 | Dr. Gero |
| 3898 | 128 | Majin Buu |
| 3900 | 133 | Super Buu |
| 3902 | 141 | Kid Buu |
| 3924-3925 | 264/270 | Goku |
| 3918 | 246 | Kid Gohan |

> **✅ CONFIRMED 2026-09-07**: the `portrait_swap_test` mod (serving entry
> **3934** in slot **3930** via per-entry override) showed
> **Nappa's portrait on Krillin**. BOTH pairs are confirmed:
> **3930 = Krillin portrait** and **3934 = Nappa portrait**, and the whole
> mechanism is validated: the per-entry `data_cmn.afs` override serves portraits
> (AFS override + virtual mid-insert, without touching `data_eng.afs`).

## 7. 🔴 ROSTER IN THE GUEST (FINDING 2026-09-07)

The select roster does **NOT live in `data_eng.afs`** (composite): it lives in the
**decrypted guest image** (`dbz3_us_image.bin`, dumped with
`out/analysis/guest_image/dump_image`). Two tables found:

1. **Portrait / select slot table** at `0x82372818` (image offset
   `0x372818`): 78 u32 = **39 slots × 2 entries** (pair per slot, e.g.
   slot 10 = `[3930, 3931]` = Krillin, slot 19 = `[3934, 3935]` = Nappa).
   Select slot order below.
2. **Per-character bin table** at `0x823268C0` (offset `0x3268C0`):
   runs of AFS indices separated by `0xFFFFFFFF` (models → CAM → LIPS/ANM);
   the run `[323..329]` holds Krillin's group (327/328/329).

The portrait table's consumer is `sub_8217F3F0` (recomp
`.15.cpp:13877`): `lis -32201; addi r9,r9,10264` → `0x82372818`, index
`slot*8 + flag*4`. This opens the door to **adding native slots** (§9): the
table is guest data, not host code. (New select cells are now implemented: see
the "New characters" tools.)

**Slot mechanism (initial RE)**: `sub_8217F478` (`.14.cpp:14437`) reads the
**slot id** as u16 at `r3+64`; `0xFFFF` = empty slot (returns without
acting); otherwise it calls `sub_8217F3F0` to resolve the portrait. In
`sub_82180AA0` (`.15.cpp:13916`) a character base is indexed with
**`mulli r10,r10,184`** (184-byte struct per character) and offsets
`+14/+18/+114` (u16) are read. Slots are enumerated with `cmpw r29,r11` (counter vs
byte at `r30+12`).

### Real select order (39 slots — ✅ RESOLVED BY NAME 2026-09-08)

> **Method**: cross the guest portrait table with the Pal AFL. In the
> IMG region, **US = PAL + 45** (PAL 3839-3917 → US 3884-3962). Verified:
> PAL 3885 KULILIN_IMG_0 → US 3930 (confirmed in game), PAL 3889 NAPPA_IMG_0
> → US 3934 (confirmed), and PAL 3859 CELL_IMG_0 → US 3904, etc. The AFL
> `*_IMG_0/_1.amt` portraits name each slot unambiguously. Full source:
> `out/analysis/roster_slots.txt`.

| Slot | US portraits | Character | Slot | US portraits | Character |
|---:|---|---:|---:|---|---|
| 0 | 3924 3925 | Goku | 20 | 3922 3923 | Ginyu |
| 1 | 3920 3921 | Goku (kid) | 21 | 3942 3943 | Recoome |
| 2 | 3918 3919 | Gohan (kid) | 22 | 3912 3913 | Freeza |
| 3 | 3916 3917 | Gohan (Cell saga) | 23 | 3884 3885 | Android 16 |
| 4 | 3914 3915 | Gohan (adult) | 24 | 3886 3887 | Android 17 |
| 5 | 3926 3927 | Great Saiyaman | 25 | 3888 3889 | Android 18 |
| 6 | 3928 3929 | Goten | 26 | 3892 3893 | Dr. Gero |
| 7 | 3958 3959 | Vegeta | 27 | 3904 3905 | Cell |
| 8 | 3950 3951 | Trunks (future) | 28 | 3898 3899 | Majin Buu (fat) |
| 9 | 3948 3949 | Trunks (kid) | 29 | 3900 3901 | Super Buu |
| 10 | 3930 3931 | **Krillin** | 30 | 3902 3903 | Kid Buu |
| 11 | 3938 3939 | Piccolo | 31 | 3910 3911 | Dabura |
| 12 | 3952 3953 | Tien | 32 | 3908 3909 | Cooler |
| 13 | 3960 3961 | Yamcha | 33 | 3894 3895 | Bardock |
| 14 | 3946 3947 | Mr. Satan | 34 | 3896 3897 | Broly |
| 15 | 3956 3957 | Videl | 35 | 3890 3891 | Syn Shenron |
| 16 | 3932 3933 | Kaio-Shin | 36 | 3944 3945 | Saibaman |
| 17 | 3954 3955 | Uub | 37 | 3906 3907 | Cell Jr. |
| 18 | 3940 3941 | Raditz | 38 | 3936 3937 | Random |
| 19 | 3934 3935 | **Nappa** | | | |

## 8. WHAT THIS ENABLES (F3.3)

- **Adding a character by duplication**: duplicate model + CAM/LIPS/ANM + aura +
  portrait, and **extend the guest portrait table** (`0x82372818`) + the
  bin table (`0x823268C0`) — the roster lives in the guest image, and
  `sub_8217F3F0` indexes it (§7).
- **Replacing in an existing slot**: model bin swap (already validated) or
  portrait swap (confirmed §6).
- **Moveset by duplication**: the character's ANM is the big `#ACM` bin
  in the ANM column (validated: Freeza uses 199-213, Goku 290-292, etc.).
- **Do not touch** the whole `data_eng.afs` to change a portrait: the per-entry
  `data_cmn.afs` override is enough for models and single portraits.

## 9. NEXT STEPS FOR NATIVE SLOTS (at the time)

### 9.1 ✅ 184 B / SLOT RECORD — STRUCTURE DECODED (static RE 2026-09-08)

> The record VALUES are runtime (the static image has them zeroed),
> but the STRUCTURE is mapped by reading the accesses in `generated/`.

**184 B record** (base `0x8238B780`, `lis 0x8239 / addi -18560`):
- Indexed by the **index table `0x8238B670`** (`lis 0x8239 / addi -18832`),
  u32 array with **≥4 entries** (offsets +0, +4, +8, +12; `sub_8217ADE8`
  uses 4 indices).
- Access: `index = table[i]` → `ptr = base + index * 184`.
- Fields read (u16, relative to `ptr`):

| Offset | Type | Who | Observed use |
|---|---|---|---|
| +12 | u16 | ADE8 | flag/id (selection by slot flags) |
| +14 | u16 | ADE8, 80AA0, F3F0 | flag set (bit0/bit1 → read) |
| +18 | u16 | ADE8, 80AA0, F3F0 | OR-merged with +114 |
| +68 | u16 | 80AA0 | extra |
| +114 | u16 | ADE8, 80AA0, F3F0 | flag set, OR with +18 |

**Slot object** (`r3` in sub_8217F3F0/F520/80AA0, NOT the 184 B record):

| Offset | Type | Content |
|---|---|---|
| +28 | u32 | callback/vtable (F520 writes `0x8237F478`) |
| +40 | u32 | ptr (ADE8 passes it to sub_8217E008) |
| +48 | u32 | data ptr (`[r48]` = state object: `[+0]u8`, `[+2]u8`, `[+3]u8` flags, `[+12]u32`, `[+120]u32` in 80AA0) |
| +60 | u8 | 0 (reset) |
| +62 | u16 | flags: **bit0 = portrait selector** (F3F0), bits 1-2 cleared in F520 |
| +64 | u16 | **portrait index s16**; `0xFFFF` = **EMPTY slot** (F478 returns) |
| +68 | f32 | default (const -2548) |

**Portrait table `0x82372818`** (`lis 0x8237 / addi 0x2818`, consumer
F3F0): indexed `((s16@+64) * 2 + bit0@+62) << 2` → u32 (the 39×2 portraits).

**Per-character table** `~0x8239F1xx` (base `lis 0x8246 / addi -3720`, written
by A8B8, read by A618):
- `+4 + idx*2`: u16 (char id of slot `+4`)
- `+8 + idx*2`: u16 (id or 38)
- `+12 + idx`: u8 (`[slot+5]`)
- `+18 + idx`: u8 (`[slot+12]`)
- `+20 + idx`: u8 (`[slot+13]`)

**Character global `0x823BA110`** (`lis 0x823C / addi -24304`): bytes +1/+2,
+20/+21/+22, +28, +192/+193/+194 (used by A920/A618/80AA0/ADE8).

### 9.2 Next step at the time (Milestone 2 → 3)

1. **RE of the character base (184 B/slot)**: in `sub_82180AA0` it is indexed
   with `mulli 184` and offsets `+12/+14/+18/+68/+114` (u16) are read — structure
   mapped (9.1); content still to confirm with a runtime trace.
2. See whether the guest reads the table with a fixed limit or a counter — choose between:
   - a **host hook** that patches the table in memory at boot (add a slot),
   - **re-codegen** with an extended table (more invasive).
3. Try adding ONE native slot: portrait + model + CAM/LIPS/ANM duplicated
   in `data_cmn.afs` (the virtual mid-insert supports it) + a table entry.

## 10. SOURCES AND TOOLS

| Resource | Path |
|---|---|
| HD model catalog | `mod center hd/catalog_b3.cat` |
| Raw map (3990) | `mod center hd/data_cmn_map.txt` |
| Pal AFL names | `modding resources/Data_CMN file name list (Budokai 3 Pal)/data_cmn.afl` (parsed in `out/analysis/data_cmn_pal_afl.txt`) |
| Community list | `modding resources/DBZ_B3_Character_Bin_List.txt` |
| Classification probe | `out/analysis/probe_range.py`, results in `out/analysis/probe_result.txt` |
| Full annotated table | `out/analysis/data_cmn_annotated.txt` |
| Extractors | `awo_tools/afs_probe.py`, `awo_tools/extract_azt_afs.py`, `awo_tools/afs_list.py` |
