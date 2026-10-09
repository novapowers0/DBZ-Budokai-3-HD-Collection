# Texture mod — how it works

> How the B3 HD → B3 HD texture mod works, from the Mod Kit "Textures" card or the
> launcher's Textures tab (script `mod center hd/texture_b3.py`).
>
> For the easy way (capture textures while playing, edit the PNG, done) see the launcher's
> Mods tab, "Easy textures".

---

## 1. What it does

It lets you edit a **character's textures** (the DXT3 `.dds` embedded in the
#AMB bin / #AZT block) and builds an **active mod** that replaces them in game,
without touching the geometry (the bin keeps its size).

Overall flow:
1. **Extract** a character's textures as **editable PNGs**.
2. **Edit** the PNGs (in the folder you choose).
3. **Rebuild** the mod: re-encodes the PNGs to DXT3 and builds the mod's AFS entry.

---

## 2. The fields

### 2.1 Character (texture source)
List from the `mod center hd/catalog_b3.cat` catalog (183 characters). Each
entry shows `Name [bin N]` to tell variants of the same character apart
(e.g. Dr. Gero `[bin 91]` vs `Dr. Gero (alternate costume) [bin 92]`).

> ⚠️ Some bins have NO textures of their own (partial models/variants that
> reuse another bin's, e.g. Dr. Gero's bin 92). Extract says so clearly
> instead of failing.

### 2.2 Destination slot (where the textures are applied)
- **"Same character (no swap)"** (default): the textures go back on the
  character they were extracted from.
- **Another character**: the source bin (with its edited textures) is placed in
  the destination slot → works with **model swaps**: extract A's textures, edit
  them, and apply them on B's bin (which can come from a swap).

### 2.3 Mod name
Sets the mod folder (`mods/<name>/`). If empty, `tex_<bin>` is used.
Characters Windows does not allow (`<>:"|?*`) are cleaned up.

### 2.4 Texture folder (PNG)
- **Filled in automatically** on extract with the default path
  (`mods/<mod>/textures`).
- **Editable**: you can type any other folder where you are editing.
- **"Browse..."** opens the Windows folder picker.

> "Rebuild" is only enabled when the chosen folder contains
> `textures_meta.json` (written by extract). It does not depend on the mod name.

---

## 3. Buttons

| Button | What it does |
|-------|----------|
| **Extract textures to PNG** | Extracts the bin, finds the #AZT block and writes each texture as an editable PNG in the chosen folder. Writes `textures_meta.json` (with each texture's original DDS header). |
| **Open textures folder** | Opens the active folder in Windows Explorer (to view/edit the PNGs). |
| **Rebuild mod with edited textures** | Re-encodes the PNGs to DXT3, rebuilds the DDS (header + bitmap), replaces it in the #AZT, compresses LZX `/N:2048`, pads to the slot and builds the active mod. |

---

## 4. #AZT format (summary)

- The #AMB bin → #AWO (model) + #AZT (textures).
- #AZT header: `tex_am` at +0x10, `index_loc` at +0x14 (offset table).
- Each texture: `idx@+0`, `type@+4`, `w@+16`, `h@+18`, `data_off@+0x14`
  (offset, relative to the AZT, of the 128B DDS header + DXT3 bitmap).
- DXT3 bitmap = `w*h` bytes (BC2, 16B per 4x4 block, mipmaps=0).
- Pillow decodes DXT3 DDS → RGBA; our own DXT3 encoder (numpy) re-encodes
  to the exact size.

---

## 5. `texture_b3.py` script (command line)

```
# Extract (default folder: mods/<mod>/textures)
python "mod center hd/texture_b3.py" extract --bin <n> [--mod <name>] [--afs <afs>] [--out <mods>] [--dir <folder>]

# Rebuild
python "mod center hd/texture_b3.py" build --mod <name> [--afs <afs>] [--out <mods>] [--dir <folder>] [--slot <destination>]
```

Key arguments:
- `--bin N` (extract): the character's AFS entry.
- `--dir <folder>`: PNG folder (extract: output; build: input). Handy for
  editing outside the mod tree.
- `--slot N` (build): destination slot in the AFS (default: the same bin as
  extract). Lets you apply one character's textures on another (swap).
- `--afs <path>`: data_cmn.afs to work on (default `us/data_cmn.afs`).

Build writes the mod's `manifest.txt` (name/description/type/source/target)
and the entry with **mid-insert** (the bin can grow in its slot without
breaking the AFS table).

---

## 6. Robustness notes / errors

- **Fixed work folder**: the script uses `out/build/win-amd64-release/.tex_work` (it does
  not depend on the environment `TEMP`, which the launcher can inherit broken) and
  gives xbcompress/xbdecompress an environment with fixed `TEMP`/`TMP`.
- **Diagnostics**: the launcher writes the exact command to
  `pipeline_cmd.log`; the script dumps any traceback to `texture_b3_error.log`
  and prints it in the output.
- **Bins without #AZT**: clear message instead of a traceback (e.g. Dr. Gero bin 92).
