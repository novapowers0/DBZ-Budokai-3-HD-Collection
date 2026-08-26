# DBZ Budokai 3 HD Collection

English · [Español](README.md)

A native PC port of *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360) built
on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The game's
original PowerPC code is statically recompiled and linked into a standalone
executable with its own launcher and mod system. It is a real port, not an
emulator.

[![Release](https://img.shields.io/github/v/release/novapowers0/DBZ-Budokai-3-HD-Collection?sort=semver&style=flat-square&color=orange&label=Release)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows-0078D6?style=flat-square)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![License](https://img.shields.io/github/license/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square)](LICENSE)
[![Stars](https://img.shields.io/github/stars/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square&color=yellow)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)
[![Built with](https://img.shields.io/badge/built%20with-ReXGlue-8A2BE2?style=flat-square)](https://github.com/rexglue/rexglue-sdk)

| | |
|---|---|
| **Players** | 1–2 (versus) |
| **Platform** | Windows |
| **Engine** | Xbox 360 (ReXGlue SDK) |
| **Genre** | 3D fighting |
| **Version** | v1.0.8 |

Copyright (c) 2026 **NovaPowers**. Released under the MIT License (see `LICENSE`).

---

## ⚖️ Legal notice

The game and its data **are not distributed**. To play, you must provide the
files from your **own legal copy**: the executable (`default.xex`) and the
`data_*.afs` files of the region you use. This project follows the convention
of the static-recompilation community (such as
`mstan/DragonBallZBuusFuryRecomp`): the code and launcher are distributed,
**the game content is not**.

- `baserom.md` lists the exact identity of each file (sizes and SHA-256
  checksums) and how to extract them from your ISO.
- The recompiled code (`generated/`) is produced **locally** from your `.xex`
  and is **never uploaded** to the repository.

Unofficial project, non-commercial, for research and preservation. Not
affiliated with or endorsed by Bandai Namco, Shueisha, Toei Animation or any
rights holder of Dragon Ball.

---

## 🎮 How to play (step by step)

1. **Download** the ZIP from the **Releases** tab and extract it to any folder
   (e.g. `C:\Games\DBZ3`).
2. **Provide the game files** next to `dbz3.exe`. Both layouts below work (the
   launcher detects them automatically):

   ```
   C:\Games\DBZ3\                C:\Games\DBZ3\
   ├── dbz3.exe                   ├── dbz3.exe
   ├── dbz3_avx2\                 ├── dbz3_avx2\
   ├── dbz3_legacy\               ├── dbz3_legacy\
   ├── default.xex                └── assets\
   └── us\ (and/or eu\)               ├── default.xex
                                     └── us\ (and/or eu\)
   ```

3. **Run `dbz3.exe`**. The launcher opens; if something is missing, it tells
   you and you can locate your game folder with the "Select game data folder..."
   button.
4. In the launcher choose **Region** (USA / EU PAL), **Language**, **Video** and
   **Audio**, then press **Play**.

> **About the two `dbz3.exe` files**: the one at the root is a tiny launcher
> that detects your CPU and starts the right variant (`dbz3_avx2\` if it
> supports AVX2, `dbz3_legacy\` otherwise). You never have to pick anything:
> always run the root `dbz3.exe`. Both inner `dbz3.exe` files contain the USA
> and EU recompilations and select the correct one based on the `default.xex`
> you provide.

### Which game files you need

Only the executable and your region's data, **not the whole ISO**:

- **USA**: into `us\` → `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`,
  `data_ger.afs`, `data_ita.afs`, `data_spn.afs`, `data_usi.afs`,
  `data_yah.afs`, `adx_jpn.afs`, `adx_usa.afs`, `lang_jpn.afs`,
  `lang_usa.afs`, `opening.sfd`, `Ending00.sfd`, `Ending01.sfd`.
- **EU/PAL**: the same files into `eu\`.

Everything can live next to `dbz3.exe` or inside `assets\` (with `default.xex`).
You can verify the files against `baserom.md`.

To extract them from your **legal ISO**, use a tool such as `extract-xiso`
(reads the FATX filesystem of the Xbox 360).

---

## 📁 Repository layout

```
DBZ-Budokai-3-HD-Collection/
├── default.xex               # NOT included. Game executable (USA or EU)
├── us/                       # NOT included. USA region data
├── eu/                       # NOT included. EU/PAL region data
├── src/                      # Recompiler + launcher + mod system
│   ├── main.cpp              #   entry point, window, crash handler
│   ├── bootstrap.cpp         #   ISA launcher (avx2/legacy) for the package
│   ├── mods.cpp              #   mod system (AFS overlay)
│   ├── launcher/             #   launcher UI + model pipeline
│   └── ingame/               #   in-game menu
├── generated/                # NOT included. Code derived from your .xex
├── mod center hd/            # Python modding tools (ours)
├── awo_tools/                # AWO/AWG format reverse-engineering tools
├── patches/                  # ReXGlue SDK patches (see its README)
├── mods/                     # User mods (empty)
├── tools/                    # xbcompress/xbdecompress + utilities
├── docs/                     # Full documentation
├── CMakeLists.txt            # Build
├── baserom.md                # Required game files + how to extract them
└── LICENSE                   # MIT (NovaPowers)
```

---

## 🌍 USA / EU regions

The USA (`yae3_xenon.xex`) and EU (`yae3_xenon_eu.xex`) executables are
**different builds** (they are not identical), and the package includes the
recompilation of each one inside the same dual core. The launcher identifies
which one you placed (by checksum) and uses the matching code; if it does not
match, it warns you and blocks Play so you do not hit a cryptic crash.

The **data** region (the `us\` or `eu\` folder) and the **language** are chosen
in the launcher and do not depend on the executable. Saves are shared between
regions.

---

## 🛠️ Mods

Mods live in `mods\<name>\` (the folder ships empty) and replace AFS entries
through an overlay, without touching the original AFS files:

```
mods/<mod>/us/data_cmn.afs/<entry>/geom.bin   # override of one AFS entry
mods/<mod>/manifest.txt                       # metadata (name, author...)
mods/<mod>/.disabled                          # if present, the mod is OFF
```

They are managed visually from the launcher (**Mods**, **Textures** and
**Model Swap** tabs) or with the tools in `mod center hd/`. Guides in
`docs/02_mods/`.

### Model swaps in any direction (virtual mid-insert)

A B3→B3 swap is a per-entry override (~100 KB) that is served on the target
slot even when the binary is **larger** than the original slot: the runtime
presents the game a consistent AFS table (the entry grows in place and the
following ones shift) and translates the reads. That is how, for example,
placing Goten into Krillin's slot works.

This requires the **ReXGlue SDK patch** included in `patches/` (see
`patches/README.md`).

### Launcher features

- **Video**: internal resolution, region, language, VRR, frame cap (0 = no
  cap), quality presets per GPU.
- **Upscaling**: FSR / CAS.
- **Audio**: master / music / effects / voice volumes.
- **Input**: keyboard and gamepad (XInput), key remapping, deadzone, rumble.
- **Mods**: enable/disable mods and edit their manifest.
- **Textures**: extract a character's textures to PNG, edit them and rebuild
  the mod.
- **Model Swap**: native B3→B3 swap (183-character catalog).
- **Dev**: FPS counter and GPU diagnostics — **all OFF by default**.

---

## 🏗️ Building from source

Requirements: a C++23 compiler, CMake ≥ 3.25 and the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (`REXSDK_DIR` or a
`rexglue/` folder next to the project).

> **Apply the runtime patches first** (`patches/`) onto your copy of the SDK,
> as explained in `patches/README.md`, and rebuild the runtime. Without them,
> swaps with binaries larger than the slot do not work.

```
git clone --recurse-submodules https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection.git
cd DBZ-Budokai-3-HD-Collection

# 1) place your legal .xex as default.xex
# 2) regenerate the code derived from the xex
cmake --build out/build/win-amd64-release --target dbz3_codegen
# 3) build
cmake -S . -B out/build/win-amd64-release
cmake --build out/build/win-amd64-release
# 4) run
out\build\win-amd64-release\dbz3.exe
```

The recompiled code (`generated/`) is derived from your `.xex` and **never
uploaded** (see `generated/README.md` and `.gitignore`). The release package
layout (bootstrap + avx2/legacy variants) is assembled by
`tools/make_release.ps1`.

---

## 📄 Status

| Technique | Status |
|---|---|
| Native B3→B3 swap (~100 KB override) | ✅ Works in any direction (bins > or < slot) |
| B3 HD texture mod | ✅ Works (per-entry override, ~118 KB) |
| 2+ simultaneous model/texture mods | ✅ Works (virtual mid-insert) |
| Music mod (og_music) | ✅ Works |
| USA/EU dual core (single binary) | ✅ Works (validated in-game) |
| PS2→HD port | ⚠️ Researched; requires a full rebuild |
| IW→B3 character ports | 🔴 Dropped (Janemba failed, archived) |

---

## 👥 Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) — recompilation tools.
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — SDK API
  reference (reference only; not a base or a code copy).
- Budokai modding community — reference tools and models.
- **NovaPowers** — author of the launcher, the mod system and the tools.