# DBZ Budokai 3 HD Collection

**Windows | Linux**

English · [Español](README.md)

A native PC port of *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360) built
on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The game's
original PowerPC code is statically recompiled and linked into a standalone
executable with its own launcher and mod system. It is a real port, not an
emulator. Native Linux builds use Vulkan and SDL3.

[![Release](https://img.shields.io/github/v/release/novapowers0/DBZ-Budokai-3-HD-Collection?sort=semver&style=flat-square&color=orange&label=Release)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-0078D6?style=flat-square)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)
[![License](https://img.shields.io/github/license/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square)](LICENSE)
[![Stars](https://img.shields.io/github/stars/novapowers0/DBZ-Budokai-3-HD-Collection?style=flat-square&color=yellow)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection)
[![Built with](https://img.shields.io/badge/built%20with-ReXGlue-8A2BE2?style=flat-square)](https://github.com/rexglue/rexglue-sdk)

| | |
|---|---|
| Players | 1–2 (versus) |
| Platform | Windows / Linux |
| Engine | Xbox 360 (ReXGlue SDK) |
| Genre | 3D fighting |
| Version | v1.2.8.2 |

Copyright (c) 2026 **NovaPowers**. Released under the MIT License (see `LICENSE`).

---

## Legal notice

This project does not include the game. To play, you must provide the files
from **your own legal copy**: the executable (`default.xex`) and the
`data_*.afs` files of the region you use. This follows the usual convention of
the static-recompilation community (e.g. `mstan/DragonBallZBuusFuryRecomp`):
the code and launcher are distributed, the game content is not.

- `baserom.md` lists the exact identity of each file (sizes and SHA-256
  checksums) and how to extract them from your ISO.
- The recompiled code (`generated/`) is produced locally from your `.xex` and
  is never uploaded to the repository.

Unofficial project, non-commercial, for research and preservation. Not
affiliated with or endorsed by Bandai Namco, Shueisha, Toei Animation or any
rights holder of Dragon Ball.

---

## How to play

### Platform downloads

- **Windows:** `DBZ-Budokai-3-HD-Collection-v1.2.8.2.zip`
- **Linux amd64:** `DBZ-Budokai-3-HD-Collection-v1.2.8.2-linux-amd64.tar.gz` (Vulkan)

The Linux package includes `dbz3` and `librexgpu-xenos.so`, but not the game or
its assets. Extract the tarball, place your legally obtained `default.xex` and
`us/` or `eu/` folder next to `dbz3`, then run `./dbz3`. See
[`docs/LINUX.md`](docs/LINUX.md) for dependencies and local builds.

There are two ways to provide the game data: the extracted folder or the ISO
directly. Both are detected automatically, nothing to configure.

**Option A — the extracted folder (if you want mods)**

1. Download the ZIP from **Releases** and extract it anywhere.
2. Put `default.xex` and the `us\` (or `eu\`) folder next to `dbz3.exe`. Both
   layouts below work:

   ```
   C:\Games\DBZ3\                C:\Games\DBZ3\
   ├── dbz3.exe                   ├── dbz3.exe
   ├── default.xex                └── assets\
   └── us\ (and/or eu\)               ├── default.xex
                                     └── us\ (and/or eu\)
   ```

   **A straight disc dump works too** (with the `DBZ3\` folder and its
   executable not renamed): the launcher finds the Budokai 3 executable itself.

   ```
   C:\Rom\Budokai HD Collection\
   ├── dbz3.exe
   └── DBZ3\                      ← exactly as it comes off your ISO
       ├── yae3_xenon.xex
       └── us\ (and/or eu\)
   ```

3. Run `dbz3.exe`. The launcher checks what's there and tells you if something
   is missing. You can locate your game folder with "Select game data folder...".
4. Choose **Region**, **Language**, **Video** and **Audio**, then press **Play**.

**Option B — the ISO directly (play without extracting anything)**

Drop the game's `.iso` next to `dbz3.exe` (or use "Select ISO..." in the
launcher). The launcher detects it, pulls the Budokai 3 executable out of the
disc (`DBZ3\yae3_xenon.xex`, a few MB) and mounts the rest straight from the
image: no need to extract or copy the AFS files. The region is detected on its
own from the disc's executable.

> Works with the full original ISO (the one that has the HD Collection menu at
> the disc root): the launcher takes the Budokai 3 executable from inside the
> disc, not the menu.

> Mods need the extracted folder (option A). In disc mode you play the game as
> it comes on the ISO.

> **A single `dbz3.exe`**: since v1.1.0 there are no variants. One universal
> executable (baseline SSSE3 runtime) that runs on any x64 CPU (Core 2 2006
> onwards), with the USA and EU recompilations inside and auto-detection of the
> executable you provide (by size + checksum, whatever it is called and
> wherever it is).

### Which game files you need (option A)

Only the executable and your region's data, not the whole ISO:

- **USA**: into `us\` → `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`,
  `data_ger.afs`, `data_ita.afs`, `data_spn.afs`, `data_usi.afs`,
  `data_yah.afs`, `adx_jpn.afs`, `adx_usa.afs`, `lang_jpn.afs`,
  `lang_usa.afs`, `opening.sfd`, `Ending00.sfd`, `Ending01.sfd`.
- **EU/PAL**: the same files into `eu\`.

Everything can live next to `dbz3.exe` or inside `assets\` (with `default.xex`).
You can verify the files against `baserom.md`.

To extract them from your legal ISO, use a tool such as `extract-xiso` (reads
the FATX filesystem of the Xbox 360).

---

## Repository layout

```
DBZ-Budokai-3-HD-Collection/
├── default.xex               # NOT included. Game executable (USA or EU)
├── us/                       # NOT included. USA region data
├── eu/                       # NOT included. EU/PAL region data
├── src/                      # Recompiler + launcher + mod system
│   ├── main.cpp              #   entry point, window, crash handler
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

## USA / EU regions

The USA (`yae3_xenon.xex`) and EU (`yae3_xenon_eu.xex`) executables are
different builds, not two copies of the same thing, and the dual core includes
the recompilation of each one. The launcher identifies which one you placed by
checksum and uses the matching code; if they don't match, it warns you and
blocks Play so you don't end up with a cryptic crash.

The file does not have to be named `default.xex` nor sit at the root: the
launcher finds it by **size + checksum** (typical names are `yae3_xenon.xex` /
`yae3_xenon_eu.xex`) and prepares it by itself. If you drop the HD Collection
menu instead of the Budokai 3 executable, it tells you and blocks Play.

The data region (the `us\` or `eu\` folder) and the language are chosen in the
launcher and don't depend on the executable. Saves are shared between regions.

---

## Mods

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
slot even when the binary is larger than the original slot: the runtime
presents the game a consistent AFS table (the entry grows in place and the
following ones shift) and translates the reads. That is how, for example,
placing Goten into Krillin's slot works.

This requires the **ReXGlue SDK patch** included in `patches/` (see
`patches/README.md`).

---

## Building from source

You need a C++23 compiler, CMake ≥ 3.25 and the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (`REXSDK_DIR` or a
`rexglue/` folder next to the project).

> Apply the runtime patches first (`patches/`) onto your copy of the SDK, as
> explained in `patches/README.md`, and rebuild the runtime. Without them,
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

The recompiled code (`generated/`) is derived from your `.xex` and never
uploaded (see `generated/README.md` and `.gitignore`). The release package
layout is assembled by `tools/make_release.ps1`.

---

## Status

| Technique | Status |
|---|---|
| Native B3→B3 swap (~100 KB override) | Works in any direction (bins > or < slot) |
| B3 HD texture mod | Works (per-entry override, ~118 KB) |
| 2+ simultaneous model/texture mods | Works (virtual mid-insert) |
| Music mod (og_music) | Works |
| Play from the ISO (disc mode) | Works (base game; mods need the folder) |
| USA/EU dual core (single binary) | Works (validated in-game) |
| PS2→HD port | In research; requires a full rebuild |
| IW→B3 character ports | Dropped (Janemba failed, archived) |

---

## Features

**One universal executable**

- A single `dbz3.exe` (baseline SSSE3 runtime) that runs on any x64 CPU (Core 2
  2006 onwards), with no variants.
- **USA/EU dual core** with both recompilations inside and **executable
  auto-detection** by size and checksum: no need to rename it or keep it at the
  root.
- **Disc (ISO) mode**: play straight from the `.iso` without extracting
  anything, including the full original ISO with the HD Collection menu.

**Video and performance**

- **FSR 1 / CAS**, **FXAA** and dither; internal resolution, MSAA and
  anisotropic filtering.
- **VRR** and a real **frame cap** (0 = uncapped).
- **Per-GPU quality presets**: the Automatic mode detects your GPU and picks the
  profile; no preset raises the internal scale (1x is recommended).
- **Texture enhancement (experimental)**: upscales the game's textures at
  runtime (Sharp x2 / Very sharp x3) without touching the game files, with a
  **clean HUD**.
- **Cost warning** when you raise the internal scale and a **"Back to native
  (1x)"** button.

**Audio and controls**

- Real **master volume** and **Mute**, applied on the fly.
- **Gamepad (XInput)** or keyboard, with remappable keys, deadzone, rumble and
  mouse sensitivity.
- When you leave the window: **mute** and/or **dim** (optional).

**Launcher**

- Tabbed UI, an **always-visible source selector** (folder or ISO) and a
  **Play** button that warns you if something is missing instead of failing
  silently.
- **5 languages** (ES/EN/IT/DE/FR) and **clear messages** when the executable is
  not the right one (HD Collection menu, DBZ1 executable, unknown dump).
- **Portable, self-repairing settings**: if the folder is not writable,
  `Documents/dbz3` is used; if `dbz3_user.toml` gets damaged, it is repaired
  automatically or a `.bak` copy is kept.
- **Update check** on startup (can be disabled).

**Mods**

- **Per-entry AFS override** (without touching the original AFS files) with
  **virtual mid-insert**: works even when the model is larger than the slot.
- **Native B3↔B3 model swap**: 183-character catalog, search, preview and in any
  direction.
- **Textures**: extract to PNG, edit and rebuild the mod; music and whole files
  can be replaced too.
- Mod center with search, enable/disable all, typed badges and ZIP install.

**Diagnostics (optional)**

- Dev tab: FPS counter, I/O and performance logging and GPU diagnostic knobs.
  All **off by default**.

---

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) — recompilation tools.
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — SDK API
  reference (reference only; not a base or a code copy).
- Budokai modding community — reference tools and models.
- **NovaPowers** — author of the launcher, the mod system and the tools.
