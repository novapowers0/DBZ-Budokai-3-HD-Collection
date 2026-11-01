# How to make mods for DBZ Budokai 3 HD Collection

> Updated: 2026-10-06 (§5 porting a Shin Budokai character, §6 cameras with the Studio).
> CORRECT pipeline, validated (per-entry override + virtual mid-insert).

---

## 0. SHORTCUT FOR PLAYERS: THE MOD KIT WIZARDS (phase 2)

If you do not want to touch bins or a console, the Mod Kit home page has four "guided" cards. Each one asks step by
step, shows a summary with "Change" on every line (the preset and the base character are shown there) and has a
single **Build and try** button:

- **Add a character**: source game -> character -> name, preset and base character -> imports and builds.
- **Change look and aura**: game character -> aura and ki colour -> (optional) your PNGs; the wizard can export the
  PNGs for you. It creates a colours mod and a textures mod.
- **Give someone a costume**: game character -> model (an already imported character or your `.amb`/`.bin`) -> name.
- **Install a mod (.zip)**: pick the zip, tick which mods you want and it installs. If you already had one with that
  name the old one is kept in `modkit_backups`. Program files in the zip (`.dll`, `.py`, `.exe`...) are NOT installed
  unless you tick that you trust the author. Zips with unsafe paths are refused.

Everything can be **Undone** (wizard button): new mods are moved to `modkit_backups` and the previous build
(`_roster`) is put back; nothing is ever deleted. They are installed in `mods/`; the game uses them when you press PLAY.
Headless test: `python modkit_gui.py --selftest-asistentes <copy of us> <new folder>`.

**Build and try, for real (phase 3).** When the build ends, the wizard starts the game and tells you in one sentence
whether the mod loaded ("It works..." or the cause of the failure, no jargon). It presses nothing in the game: it stays
on the start menu, so **your saved games are not touched**. If the game is already open it asks you to close it. On the
developer PC the test runs on the fixed copy `D:\DBZ3HD\smoke_c` through `tools/pruebas.py` (what it adds goes back to
`D:\_PARA_BORRAR_CLAUDE\kit`); on a player's PC it starts their own `dbz3.exe` with no test variables.

**Expert mode.** The old "Tools" tab (every tool with its options; Textures, Create and Hex repeat what the wizards
do) is now called **Expert**, in Advanced mode. No common task needs it. More in [TOOLS.md](../04_herramientas/TOOLS.md),
[texture packs](PACKS_DE_TEXTURAS_EN.md) and [model swap](MODEL_SWAP_EN.md).

## 1. THE TWO KINDS OF MOD

### 1.1 Whole-file override (replaces an entire AFS)
```
mods/<mod>/us/data_cmn.afs        ← complete AFS file (293MB)
mods/<mod>/eu/data_cmn.afs
```
- Used by `og_music`.
- Served by the `AfsFindModFileOverride` hook.
- **Downside**: the whole AFS must be rebuilt (build_afs.py). It is NO longer
  needed for model/texture swaps.

### 1.2 Per-entry override (replaces ONE bin inside the AFS) — RECOMMENDED
```
mods/<mod>/us/data_cmn.afs/327/geom.bin     ← FOLDER form
mods/<mod>/us/data_cmn.afs/327              ← DIRECT FILE form
```
- Served by the `AfsFindModOverride` hook.
- **No AFS rebuild** — only the bin of one entry (~100KB).
- **Any bin size**: if it exceeds the slot, the runtime applies the
  **virtual mid-insert** (see §2.4). 2+ mods at once (different entries).

---

## 2. STEPS FOR A MODEL MOD

### Step 0: know the right entry
Entry = index in the AFS table (offset 8 of the file, `entry_count` at +4).
- Visible Krillin = **entry 327** (NOT 326). Verified by instrumentation.
- The runtime logs reads: `AFS327 READ` in `logs/dbz3_*.log`.

### Step 1: extract the original bin
```powershell
# 1. Find the entry in the AFS (table at offset 8)
# 2. Extract the LZX-compressed bytes
# 3. Decompress with xbdecompress
xbdecompress.exe <entry.lzx> <entry.bin>
```

### Step 2: edit the bin
- To check the B3 HD structure use `awo_tools/awg_to_obj_b3.py`,
  `awo_tools/awg0_export.py` or `awo_tools/awg_cara_export.py`.
- For native swaps use `swap_b3.py` or the launcher's Model Swap tab.

### Step 3: compress with /N:2048 (IMPORTANT)
```powershell
xbcompress.exe /N:2048 <plain_bin> <compressed_bin.lzx>
```
> ⚠️ Do NOT use `/N:32` or `/N:64` — they produce bins bigger than the slot → crash.

### Step 4: pad to to_read
The guest allocates `to_read = ceil(table_size/0x1000)*0x1000`. The compressed bin is
padded with zeros up to that size:
- **If it fits** in the slot (bin ≤ to_read): pad to the slot's to_read.
- **If it is bigger** (e.g. Goten 107006 > Krillin 106496): pad to
  `virtual_to_read = ceil(bin/0x1000)*0x1000` (110592). The runtime allows it
  with the virtual mid-insert: the entry grows in place in the virtual table and the
  following ones shift by +delta. The guest allocates the right buffer and gets the
  whole bin, untruncated.

`swap_b3.py` does this padding automatically.

### Step 5: install
```powershell
# Create the mod folder structure
New-Item -ItemType Directory -Path "mods/<mod>/us/data_cmn.afs/327" -Force
Copy-Item padded.bin "mods/<mod>/us/data_cmn.afs/327/geom.bin"

# Enable it (remove .disabled if present)
Remove-Item "mods/<mod>/.disabled"
```

### Step 6: check the logs
```
logs/dbz3_001.log:
  AFS OVERRIDE HIT (folder): ...\mods\<mod>\us\data_cmn.afs\327\geom.bin
  AFS MOD READ: bin 327 mod_off=0x0 to_read=106496 got=106496 mod_size=...
```
> `got=to_read` = the guest got the whole bin. If `got < to_read` → padding is missing.

---

## 2.4 🔴 VIRTUAL MID-INSERT (2026-08-18)

The guest reads each `data_cmn.afs` entry with a buffer of
`to_read = ceil(size/0x1000)*0x1000` taken from the AFS table. A mod bin
bigger than that to_read used to be truncated when served by override → crash.

The runtime fix (patch in `patches/`, files `afs.cpp`/`afs.h`/
`host_path_file.cpp`) shows the guest a **CONSISTENT virtual AFS table**:

- `AfsGetVirtualTable()`: if an override exceeds the slot's `to_read`, the
  entry **grows in place** (slot aligned to 0x800) and **every following entry
  shifts** by the accumulated delta — exactly like a rebuild with a mid-insert.
  The virtual addresses are consistent → the guest finds them correctly (unlike the
  "naive" attempt that inflated sizes but kept addresses: the guest recomputes offsets
  by adding up sizes → crash).
- `AfsTranslateOffset()`: for data reads, translates virtual → physical
  (subtracts the entry's delta) and serves the override (whole bin) or reads the
  physical file at the translated offset.

**Growth rule**: it only grows if the override > `to_read` (what the guest already
allocates), NOT if it exceeds the physical slot. A mod that fits (e.g. Gero's
textures, 114688 = to_read) shifts nothing.

**Result**: native B3→B3 swaps of ~100KB, in **any direction** (the bin can be bigger
or smaller than the slot), and 2+ model/texture mods active at once.

---

## 3. SLOT SIZES OF KEY ENTRIES

| Entry | Character | Compressed slot | to_read |
|---|---|---|---|
| 327 | Krillin (visible) | 105296 | 106496 |
| 328 | Krillin Buu Saga | 104404 | 106496 |
| 329 | Krillin Namek | 101268 | 102400 |
| 298 | Goten | 107006 | 110592 |
| 270 | Goku | 128062 | 130048 |

> With the virtual mid-insert the bin NO longer has to fit in the target entry's slot:
> if it exceeds it, the virtual table grows the entry and shifts the following ones
> automatically.

---

## 4. ENABLING / DISABLING MODS

- **Enabled**: folder `mods/<mod>/` WITHOUT a `.disabled` file.
- **Disabled**: with `.disabled`.
- **Order**: mods are sorted alphabetically; the first match wins.
- Only the `.disabled` marker counts; `dbz3_enabled_mods` is obsolete and does not
  control current mods. The per-entry override does not depend on the profile shown in
  the launcher.

---

## 5. PORTING A SHIN BUDOKAI CHARACTER (start to finish)

Sources: the PSP ISOs in `ps2_games/` (Shin Budokai = `sb1`, Another Road = `sb2`) and, for
better HD models, the Super Dragon Ball Heroes World Mission folder in `modding resources/`
(`sdbh`). Formats and differences: `docs/03_formatos/SB_VS_B3_MOVESET.md`.

Budokai 2 models: when the roster is built, the Kit automatically converts their shading ramps to the B3 style (`awo_tools/rampas.py`), so skin no longer comes out white or with black patches. Nothing to do.

### 5.1 The quick way (no console)
Launcher → **New characters** → "Import a character" → *Shin Budokai: Another Road* → the
character → name → **Import** → PLAY. In the Mod Kit it is the "Import a character" card.

What the importer does (`mod center hd/importar.py importar sb2 GHF --mod … --nombre …`):
1. **Models:** each `BC<XXX>B0n.amb` → HD bin (`awo_tools/psp_amo.py`); with `sdbh`, each
   `bc<xxx>bNN` → HD bin with mouth, 7 faces and ramps (`awo_tools/sdbh_model.py`, template = the
   donor's model). They become **forms** of one costume (`modelos_por_traje`).
2. **Moveset and camera:** `awo_tools/sbport.py` (AP 20 → 16 B, HR 160 → 128 B, damage × 0.85,
   remapped codes, global store per table, hyper / throw / Dragon Rush / ultimate from the donor).
   Writes `moveset/anm_forma1.bin` and `moveset/camara.bin`.
3. **Techniques:** `awo_tools/sb_tecnicas.py` (hybrid BSP, official names) →
   `moveset/tecnicas.bin`.
4. **Forms:** `formas = n`, `transformacion = "donante"` (the donor's P+K+G, drops SB's down+E),
   `fisica = "donante"` and one transformation capsule per form named after the donor's
   (its ki bars). See `docs/03_formatos/FORMAS_Y_KI.md`.
5. **Voices:** Shin Budokai voices and yells when available, otherwise the donor's.

If `sbport.py` fails or is missing, the character is still imported with **the donor's moves**
(and as many forms as the donor has) and the log says so.

### 5.2 Fine-tuning afterwards (Mod Kit → Characters)
- **Forms, physics and look:** forms, base ki per form, model per form, hair and belt physics,
  donor transformation.
- **Capsules:** official names, order, **"Set ki"** for each transformation (bars you must
  have; they are not spent).
- **Cameras (Studio):** SB ultimates have no camera of their own: make one with the Studio (§6).
- **Images:** icon, banner and portraits are generated from the model; "Refresh preview".

### 5.3 By hand (console)
```powershell
python awo_tools/sbport.py --lista --juego sb2                       # 3-letter codes
python "mod center hd/importar.py" lista sb2                          # what the launcher sees
python "mod center hd/importar.py" importar sb2 GHF --mod imp_sb2_future_gohan --nombre "Future Gohan" --mods <mods folder>
python "mod center hd/roster_build.py" construir --mods <mods folder>    # or press PLAY
```
To test without touching your mods, use a separate folder with `--mods` (`construir` rewrites the
`_roster` of the folder you pass).

### 5.4 What to check in game
Normal hits and damage; P+K+G with 3/4/5/6 bars; back to normal with under 1 bar; the pause
sheet ("With over N Ki gauges"); specials and ultimate; the belt at rest and while walking; the
toon shadow and the HD shine at 0 % and 100 %.

---

## 6. TECHNIQUE CAMERAS (Studio)

User guide: `docs/02_mods/STUDIO_CAMARAS.md`. Format: `docs/03_formatos/CAMARA_ACC.md`.

- **Game character:** the Studio writes `mods/studio_<character>/us/data_cmn.afs/<CAM fid>/geom.bin`
  (LZX `/N:2048`, padded to a **reserved** size so it can reload without restarting) and a
  `studio.json` with the edited clips.
- **New character:** rewrites its `moveset/camara.bin` (mounted when you press PLAY).
- Backups in `mods/<mod>/respaldo/<date>/` (never inside `us/`: the runtime serves the first
  file in the entry folder).
- Test loop: save → Pause → "Reselect characters" → use the technique. The first time,
  restart the game.
- Console: `python "mod center hd/studio/studio_core.py" info 0` (Goku's clips and scripts),
  `exportar-glb`, `importar-glb`, `selftest`.


## Adding your own music (Mod Kit)
In the Mod Kit, "Add music": pick your own mp3, wav, ogg, flac or adx file, give it a name and the Kit converts it (with ffmpeg if it is not .adx) and saves it as a new `musica_<name>` mod in `mods/` (with "Undo"). To hear it, press L1/R1 (or `[` `]`) on the fight selector, or use F1 > Fight music and pick "Mi música"; `dbz3_musica_combate = fija:propia:<n>` also works. It only adds: the game's original music is never touched. Use only songs you have the right to use; the project does not distribute game audio.

## PS2 mode: mods for PS2 Budokai 3

In the Mod Kit, top right, **Game > B3 PS2**. It creates mods for your PS2 copy (Greatest Hits USA, SLUS-20998: an .iso or the folder extracted from the disc; a .chd must be converted first) and builds a new ISO. Your copy is never changed and nothing is distributed.

1. **Texture**: pick character and form, 'Extract textures', edit the PNGs without changing their size and 'Create mod with my changes'. PS2 uses 16 or 256-colour palettes: your image's colours are reduced on save; what you do not touch stays the same.
2. **Character**: a character with another's look. It keeps name, icon and moves. It does not add characters to the wheel.
3. **Music**: replaces a track (you can listen first) and repeats when it ends.
4. Enable your mods and press **Build ISO** (a few seconds, 2.4 GB). Open it in PCSX2. Tested on PCSX2 2.9; real consoles were not tested.

Not available: new characters, capsules, aura colours, stories, Studio and HD packs. They need the port's engine; the screen explains it. From the command line: `python "mod center hd/ps2_b3.py"`.

## Optional art and lessons from the 31/10 round
**Check this mod (Mod Kit, My mods):** reports missing files, typing errors in `personaje.toml` or `traje.toml`, unusable `.wav` files and mods inside an extra folder. It does not open the game, so it does not test how the mod looks.

**Optional art (Mod Kit):** pick the folder with your PNGs; it says which piece each one is, warns about shape, size and transparency, fixes double extensions (`x.png.png`) and suggests the name for a near miss. It installs to `mods/pack_hub_mio/ui_ingame/hub/`: a normal mod you can turn off and undo. Any mod can carry `ui_ingame/hub/<piece>.png`.

**Costumes from DAE:** `awo_tools/dae_a_b3.py` converts a DAE model with Xenoverse/Legends-style bones to a costume (`--mod FOLDER` writes `traje.toml` and `modelo.lzx`). The mouth is reweighted to `M_JAW`; always check it in game, especially if the model wears a mask.

**Wheel and locked costumes:** an extra costume goes after the native ones. If native costumes are locked in that save, the selector skips them and reaches the added one. Test with a save that does not have everything unlocked.

**Checking a character:**
- A fight photo does not prove floating: it may be mid-jump. Measure the feet or use a side view at rest.
- A flat arm is not always the shading's fault: first check whether the texture itself is flat.
- A metric against your own render proves nothing: compare with a capture of the game or the original.
- Without its own moveset, a character moves like its donor. Renaming the donor's techniques does not make them its own.

**Voices:** `voces = "t3:<bank>|ninguna"` takes voices from your Tenkaichi 3 and stays silent without the ISO. Those clips have no situation table: assigning them by length can play a line at the wrong moment, so check each one. Silent beats a wrong line.
