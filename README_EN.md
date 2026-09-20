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
| Players | 1–2 (versus) |
| Platform | Windows |
| Engine | Xbox 360 (ReXGlue SDK) |
| Genre | 3D fighting |
| Version | v1.2.6 |

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
- **Dev**: FPS counter and GPU diagnostics, all OFF by default.

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

## v1.2.4 EX highlights

> Supersedes v1.2.4 (same content plus the items below).

- **Working volume in the launcher**: the "Master volume" slider and the new
  **"Mute all audio"** switch actually do something now. They used to write a
  variable the runtime does not know, so they had no effect and the game could
  not be muted from the UI.
- **New-release notice**: on launch the launcher asks GitHub for the latest
  release and, when a newer one exists, shows **"New version available: vX"**
  with a one-click download (never blocks PLAY). Can be disabled in the Dev tab.
- **New image settings** (Upscaling tab): **FXAA** (none/fxaa/fxaa_extreme) and
  **dither**. FXAA runs before upscaling, so it composes with FSR/CAS: the cheap
  anti-aliasing path when the internal scale or MSAA costs too much.
- **Mouse sensitivity** (Controller tab): real mouse control for the right stick,
  shown when "Use mouse for the right stick" is enabled.
- **GPU diagnostic switches** (Dev tab): **async shader compilation** and
  **game occlusion queries**. They separate a shader-compilation hitch or an
  occlusion wait from a real performance problem, with no rebuild.
- **Truly portable user data**: when the game folder is not writable (Program
  Files install, network share, locked-down OneDrive), settings and saves move to
  `Documents/dbz3` instead of failing silently. The Dev tab shows the folder in
  use.
- **Dead controls removed**: the **Gamma** slider and the **music/SFX/voice**
  sliders are gone (the game mixes all audio into a single stream). "Reset to
  defaults" now also restores VRR, HD textures, FXAA, dither, the GPU switches
  and the mouse sensitivity.

## v1.2.4 highlights

- **Working volume in the launcher**: the "Master volume" slider and the new
  **"Mute all audio"** switch actually do something now. They used to write a
  variable the runtime does not know, so they had no effect and the game could
  not be muted from the UI.
- **New-release notice**: on launch the launcher asks GitHub for the latest
  release and, when a newer one exists, shows **"New version available: vX"**
  with a one-click download (never blocks PLAY). Can be disabled in the Dev tab.
- **Dead controls removed**: the **Gamma** slider and the **music/SFX/voice**
  sliders are gone (the game mixes all audio into a single stream, so they are
  not separable). "Reset to defaults" now restores VRR and HD textures too.
- Base: v1.2.3 (in-game FPS counter + cleaner logs + HD textures WIP).

## v1.2.3 highlights

- **In-game performance counter**: `dbz3_perf_logging` (on by default) logs one line
  every 5 s with the real game FPS and the worst frame of the interval, measured at
  the guest swap (works even without a visible window).
- **Quiet AFS override log**: no longer writes two lines with the full host path on
  every AFS read (thousands per session); the detail is back with `dbz1_diag_logging`.
- **HD textures (WIP, OFF by default)**: emulator-style internal runtime texture
  upscale (x2/x3/x4) that leaves the game's files and memory untouched (it also
  generates the mip chain). It works and is noticeable in the intro, but it stutters
  while new textures are uploaded, so it ships as experimental and disabled by
  default (Video -> "HD textures (WIP)").
- Base: v1.2.2 EX.

## v1.2.2 EX highlights

- **The launcher finds the game executable however you have it**: no need to
  rename anything to `default.xex` or keep it at the root. It looks for it by
  **size + checksum** inside the folder you pick (and in the usual `DBZ3\`,
  `assets\`, `assets\DBZ3\` spots) and prepares it by itself in an internal
  cache (`user_data\xex_cache\`), never writing into your game folder.
- **Fixed booting from a straight disc dump** (the "I press Play and nothing
  happens" case): it used to boot the HD Collection menu at the disc root, which
  does not exist in the Budokai 3 core and died with a cryptic error. Now the
  correct executable is used (`DBZ3\yae3_xenon.xex`) and `DBZ3\` is mounted as
  the game drive.
- **Disc mode (ISO) with a full original ISO, now validated**: the Budokai 3
  executable is taken from inside the disc (not the menu) and the data
  (`DBZ3\us\`, `DBZ3\eu\`) is resolved automatically. A path-normalization bug
  that prevented reading any data from the disc was fixed too.
- **If your folder cannot boot, the ISO next to it is used**: with a straight
  disc dump (a `us\` folder plus the menu as `default.xex`) and the `.iso` next
  to `dbz3.exe`, the launcher switches to the disc by itself and boots.
- **Clear messages when the executable is not the right one**: the HD Collection
  menu is detected and explained; an unknown executable warns but does not block
  Play (it may be a modified dump).
- **Fixed a settings bug**: if your folder or ISO path contains `\`, the
  `dbz3_user.toml` was saved incorrectly and your settings were lost on every
  start (`unknown escape sequence`). It is properly escaped now.
- **Boot diagnostics log**: the executable path, its size and checksum, the
  detected status and the chosen data folder are logged.

## v1.2.1 highlights

- **Fixed a crash when closing the launcher after a Model Swap or Texture
  build**: the Python pipeline thread was left un-joined and destroying the
  launcher called `std::terminate()`. It is now joined on close.
- **Clarified FSR sharpness**: the slider label was inverted (0 is sharper, 2 is
  softer).
- **The mod list refreshes itself** when a swap/texture build finishes (the new
  mod shows up without pressing "Refresh"); "Reset values" invalidates it too.
- **Robustness**: the texture folder is read without exceptions.

## v1.2.0 highlights

- **Renewed mod center (QoL + visuals)**: cached list, search (name, author,
  source, type), Enable all / Disable all / Refresh / Open folder buttons, colored
  type badges and alternating rows.
- **Polished HD↔HD Model Swap**: searchable character dropdowns (183 characters,
  showing `[bin N]` and a `[NOT PLAYABLE]` tag), a preview card, a
  source==target warning/block, and the generated mod is named after catalog names.
- **Adjustable sharpness in Upscaling**: FSR RCAS and CAS additional sharpness
  sliders.
- **Disc (ISO) mode**: explicit warning in the Mods and Model Swap tabs (mods do
  not apply when playing from the `.iso`); the swap button is disabled.
- **Scaling/performance notes**: FSR3/DLSS are not viable short-term (the
  renderer exposes no motion vectors/jitter); FSR1/CAS do work. See
  `docs/ANALISIS_ESCALADO_RENDIMIENTO_2026-09-14.md`.

## v1.1.3 highlights

- **Always-visible source selector**: "Extracted folder" / "ISO (.iso)" buttons
  in the launcher to pick the data source at any time.
- **DBZ1 detection**: if you drop the *DBZ Budokai HD Collection* xex (sister
  project), the launcher blocks Play and tells you to "use the dbz1 launcher
  (dbz1.exe)" instead of crashing.
- **Fully audited i18n**: 0 untranslated strings across ES/EN/IT/DE/FR.
- **Clumsy-user ready**: actionable messages and hints when picking the wrong
  folder.
- **Polish**: `-Wall -Wextra` warning-free, dead code removed, stricter release
  packager (rejects runtime residue in the ZIP).

## v1.1.2 highlights

- **EU crash fix (Dragon Universe / START)**: missing dispatch function
  `sub_820F2398` extracted and registered as `dbz3eu_sub_820F2398`.
- **Incomplete regions**: `ResolveRegion()` auto-falls back to whatever of
  `us/` or `eu/` exists; EU-only or US-only data works out of the box.
- **Real Vulkan backend**: `gpu_backend` cvar added to the SDK and wired into
  `LoadGpuPlugin`; previously the launcher choice was ignored (always D3D12).
- **Polish**: warning-free builds, temporary debug traces removed, footer now
  shows "Japanese" correctly.
- **Disc mode**: play straight from the `.iso` without extracting anything
  (v1.1.2).

---

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) — recompilation tools.
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — SDK API
  reference (reference only; not a base or a code copy).
- Budokai modding community — reference tools and models.
- **NovaPowers** — author of the launcher, the mod system and the tools.