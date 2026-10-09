# Texture packs (PCSX2 style)

> A **texture pack** replaces the game's textures with your own versions
> (usually AI-upscaled) **without touching files or the guest memory**:
> the runtime intercepts each texture as it loads and, if the pack has a version
> of it, uploads that one instead of the game's.
>
> Status: **Phase 1 (dump) + Phase 2 (loader) implemented and validated.**
> The loader supports DDS (DXT1/3/5 and uncompressed 32bpp) and PNG, with a
> x1..x4 factor and mip generation.
>
> **Easy way (1.4.2 EX):** in the launcher's Mods tab, "Easy textures": capture while
> playing, press "Prepare PNGs", edit, save in "My texture pack". The steps below are
> the manual way.
>
> **v1.2.8.1 — the dump also covers HUD/UI formats**: besides the compressed ones
> (DXT1/DXT3/DXT5) it dumps the UNCOMPRESSED ones the game really uses
> (RGBA8, RGB565, RGB5A1, RGB655, RGBA4, L8, L8A8, RGBA1010102).
> **Replaceable by a pack**: DXT1/3/5 and **RGBA8** (`k_8_8_8_8`); the 8/16-bit ones are
> dumped as reference but their pack is **not** applied yet.

---

## 1. How it works (summary)

1. The game has textures in memory. The runtime computes an **XXH3-64 hash** of each
   texture's original bitmap (the same data stored in the `#AZT`).
2. A pack is a folder in `mods/` with files named after that hash.
3. When a texture loads, if its hash is in the pack, the pack image is used
   (decoded to RGBA8) at the file's resolution (x1..x4) and mips are generated.
   Otherwise the game texture is used.

No file override and no repacking: it is a host layer, like the experimental
"HD texture enhancement" (which it does **not** combine with: the pack wins).

## 2. Pack format

```
mods/
  MyPack/
    1F1ED618559910C0_256x1024_DXT3.dds     <- texture upscaled x2
    0129771C81A045B0_128x128_RGBA.dds      <- x2 uncompressed
    04037CE1AFA993E9_1024x512_DXT3.png     <- PNG works too
    pack.json                              <- optional (metadata)
```

- Name: **`<hash:16 hex>_<Width>x<Height>_<suffix>.dds`** (or `.png`).
  - `hash` = the one from the dump (identifies the ORIGINAL texture).
  - `Width`/`Height` = size of **this** image (the pack's).
  - `suffix` = free (`DXT3`, `RGBA8`, `PNG`...); informative only.
- The **factor** is derived: `factor = pack_width / original_width`, and it must be
  a whole number, equal in X and Y, between **1 and 4**.
- Accepted formats: **DDS** (DXT1/BC1, DXT3/BC2, DXT5/BC3, or uncompressed 32bpp)
  and **PNG**. Anything else is ignored with a warning in the log.
  Since the pack is always uploaded as **RGBA8**, exporting PNG is usually best (free
  factor and content) or 32bpp DDS.
- `pack.json` (optional):
  ```json
  { "name": "My pack", "author": "your name", "version": "1.0",
    "description": "Faces and stages at 4x" }
  ```

## 3. Making a pack (step by step)

### 3.1 Dump the game's textures

1. Launcher → Mods tab → turn on **"Capture textures while playing"** (or, in the
   Development tab, the dev texture dump with a folder of your choice).
2. Restart and play. The DDS files + `index.jsonl` (hash, size, format, DDS suffix,
   mips) of each unique texture are written. The file name carries the format suffix:
   `DXT1`/`DXT3`/`DXT5` (compressed) or `RGBA8`, `RGB565`, `RGB5A1`, `RGB655`, `RGBA4`,
   `L8`, `L8A8`, `RGBA1010102`.

Dump notes:

- **Unsupported formats**: skipped, with **one warning per format** in the log
  (`dbz3: volcado: formato k_24_8 (fmt=22) no soportado, texturas omitidas`).
- **Video/render targets**: a texture whose content changes on every use (the intro
  video) is dumped at most **4 times** and then no longer (warning in the log).
- **Total limit**: `dbz3_texture_dump_max` (default 4096, `0` = no limit) in the Dev tab.

### 3.2 Convert and organise

```powershell
python awo_tools\texture_dump_import.py "<dump folder>" "D:\pack_png" --afs us\data_cmn.afs
```

Converts the DDS files to PNG and places them in `<character>\texNN_WxH.png`; the ones
that match no `#AZT` go to `_unknown\`. With `--pack-names` the PNGs keep the pack name,
ready to edit and drop into a pack.

> **Black squares**: some game textures have an alpha channel that is **all
> zero** (the game ignores that alpha, but a viewer shows them transparent, i.e. black).
> It is not a dump bug. To see/use them, add **`--opaque-alpha`** and the PNG is written
> opaque. In a pack it makes no difference: the game already ignores that alpha.

### 3.3 Upscale

Run the PNGs through your favourite upscaler (waifu2x, Real-ESRGAN, Topaz...). Keep
**the same file name**; only the content and size change.

### 3.4 Pack it

Create `mods\MyPack\` and save the upscaled images there **renamed to the pack
format**: `<hash>_<newWidth>x<newHeight>_<suffix>.dds` (or `.png`).
The hash and factor come from `index.jsonl` / the original name:

```
original:  1F1ED618559910C0_128x512_DXT3.dds
x2:        mods\MyPack\1F1ED618559910C0_256x1024_DXT3.dds
```

### 3.5 Validate

```powershell
python "mod center hd\texture_pack.py" validar "mods\MyPack" --dump "<dump folder>"
```

Checks names, that the file size matches the name and, if you pass the dump, that the
hash exists and the factor is valid (1..4).

### 3.6 Enable

Launcher → **Mods** tab: the folder shows up as a mod (it can be enabled/disabled).
The launcher detects it as a pack (it has `.dds`/`.png` files with the right name) and
passes it to the runtime. Restart.

## 4. Rules and limits

- Only **single-slice 2D** textures (no cubemaps/3D/arrays). The **frontbuffer** (the
  presented image) is never replaced.
- **Replaceable formats**: DXT1/DXT3/DXT5 and **RGBA8** (`k_8_8_8_8`). The 8/16-bit
  formats (`k_8`, `k_8_8`, `k_5_6_5`, `k_1_5_5_5`, `k_4_4_4_4`...) **are** dumped
  (reference/editing) but their pack is **ignored** for now.
- The pack **wins** over the runtime "HD texture enhancement"; better not to enable both.
- **Conflicts**: if two packs define the same hash, the first by folder alphabetical order
  wins; the runtime warns in the log.
- Packs are **not applied in disc (ISO) mode**: they need the extracted folder (like every
  other mod).
- Maximum factor is **x4**. A file whose size is not a whole multiple of the original is
  ignored (warning in the log).
- VRAM: the replacement is uploaded as **RGBA8** (4 bytes/texel). A x4 pack of a
  1024x1024 texture is ~64 MB with mips; a full x4 pack can need several GB. Start with x2
  and with specific textures.

## 5. Diagnostics

In `logs\dbz3_NNN.log` (the log messages are in Spanish):

```
dbz3: pack de texturas 'MyPack' -> 12 texturas          (pack loaded, 12 textures)
dbz3: pack 'MyPack' reemplaza 128x512 (fmt 19) -> 256x1024 (x2)   (replaced)
dbz3: pack 'MyPack' subido 256x1024 (10 niveles, 1441280 B)      (uploaded)
```

- `packs cvar = ''` → the launcher found no pack (check the folder).
- `no es multiplo de ...` → the file size does not match the original.
- `factor invalido` → the factor is not whole or is above x4.
- `no se pudo decodificar` → unsupported format or corrupt file.

The Mod Kit's **Diagnostics** page explains the log in plain English.

## 6. Files involved

- Runtime (SDK): `rexglue-sdk-0.10/src/graphics/dbz3_texture_pack.{h,cpp}` and
  `d3d12/texture_cache.cpp` + `vulkan/texture_cache.cpp`.
- Launcher: `src/launcher/settings.cpp` (`RefreshTexturePacks`, cvar
  `dbz3_texture_packs`), `src/launcher/launcher_state.cpp` (Mods tab).
- Tools: `awo_tools/texture_dump_import.py` (DDS→PNG and organising),
  `mod center hd/texture_pack.py` (validate/list).
- Runtime cvar: `dbz3_texture_packs` (folders separated by `;`).
