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
| Version | v1.4.2 EX (1.4.2.3) |

Copyright (c) 2026 **NovaPowers**. Released under the MIT License (see `LICENSE`).

**[⬇ Download the latest version](../../releases/latest)** ·
[What's new (CHANGELOG)](CHANGELOG.md) · [How to install](#how-to-install-step-by-step) ·
[Mods and new characters](#mods-and-new-characters)

---

## What's new in 1.4.2 EX

- **Easy PNG textures:** capture textures while playing, edit the PNG and it applies automatically.
- **Drag-and-drop mods:** drop a `.zip` on the launcher to install it.
- **Broken-mod warning:** the launcher explains what is wrong with each mod and warns next to PLAY.
- **Modding Kit with Python included:** nothing to install.
- **Native mods translated** to the launcher language.
- **Shin Budokai characters:** voices, Burning Attack and ki blasts fixed.
- 1.4.2 brought **NVIDIA DLSS and AMD FSR 3 (beta)**, adjustable HD shine and the Shin Budokai importer.

## What's new in v1.4.1

- **Fixes for the 30 FPS lock-up on powerful PCs** (issue #8): precise timing, and Windows no
  longer puts the game in power-saving mode (efficiency cores).
- GPU crash diagnostics (DRED) no longer cost performance: they only turn on
  for the session after a GPU crash.
- The log shows where the time goes (`gpu_wait`, `cp_wait`) and records hitches
  longer than 50 ms.
- The launcher warns when your game is the European version and you have new
  characters (they only work with the US/NA version).
- Fixed the crash when changing costume on a new character (Janemba) and the
  music going silent with the characters mod plus a music pack.
- Modding Kit 1.4.1: a **Diagnostics** page that reads the game log and
  explains what is wrong and what to do.

The character pack is still `DBZ3HD-1.4.0-Personajes.zip` (it works as-is).

## What's new in v1.4.0

- **In-game quick menu**: press **F1** or **Back + Start** on the controller and
  change picture, sound or display settings without leaving the fight. Changes
  apply instantly and are saved automatically.
- **More FPS with FSR**: the game can render below your resolution and let FSR
  upscale it, for smoother play on modest PCs.
- **New FPS counter (F3)**: game FPS and display FPS, with a frame-time graph.
- **Redesigned launcher**: new look, opens in **under a second** (it used to
  take ~30 s) and can be driven entirely **with a controller**.
- **New characters** in their own select-screen cells, replacing nobody:
  Janemba, Android 19, Zarbon, Dodoria, Guldo, Jeice and Burter (optional pack).
- **Character importer** from Budokai 1, Budokai 2, Infinite World and community
  models, all from the launcher.

Full list in the [CHANGELOG](CHANGELOG.md).

---

## Screenshots

<!--
TODO(screenshots v1.4.0): save the screenshots as .jpg in docs/screenshots/ (.png is
in .gitignore and publish_check flags it as FAIL), then remove this comment.

![Launcher v1.4.0](docs/screenshots/launcher_inicio.jpg)
![New characters tab with the importer](docs/screenshots/launcher_personajes_nuevos.jpg)
![In-game quick menu (F1 / Back + Start)](docs/screenshots/menu_rapido.jpg)
![FPS counter (F3)](docs/screenshots/fps_f3.jpg)
![Character select with the new characters](docs/screenshots/select_personajes_nuevos.jpg)
![A fight with a new character](docs/screenshots/combate_personaje_nuevo.jpg)
-->

*Screenshots coming soon.*

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

## How to install (step by step)

### What to download

From the [**Releases**](../../releases/latest) page:

| File | What it is |
|---|---|
| `DBZ-Budokai-3-HD-Collection-v1.4.2.3.zip` | **The game for Windows** (required). |
| `DBZ-Budokai-3-HD-Collection-v1.4.2.3-linux-amd64.tar.gz` | The game for Linux (Vulkan). |
| [`DBZ3HD-1.4.0-Personajes.zip`](https://drive.google.com/file/d/1zpwuuU7ITKZlC43nsTbp6s2wceBcwO85/view?usp=sharing) | Optional: the 7 new characters (**download it from [Google Drive](https://drive.google.com/file/d/1zpwuuU7ITKZlC43nsTbp6s2wceBcwO85/view?usp=sharing)**). |
| `DBZ3HD-1.4.2.3-Kit-Modding.zip` | Optional: tools to create and import characters. |

### Windows

1. **Extract** `DBZ-Budokai-3-HD-Collection-v1.4.2.3.zip` into a folder, for
   example `C:\Games\DBZ3\`.
2. **Add the data from your copy of the game**, in one of two ways:
   - **Easiest — the ISO**: drop the game's `.iso` next to `dbz3.exe`. Nothing
     to extract. (Mods and new characters need the next option.)
   - **The extracted folder** (needed for mods): put the `DBZ3\` folder exactly
     as it comes off the disc next to `dbz3.exe`, or `default.xex` + the `us\`
     (or `eu\`) folder.
3. **Open `dbz3.exe`**. The launcher checks what you have and tells you what is
   missing, if anything. You can also point it at your data with the folder or
   ISO buttons.
4. Pick region, language, video and sound, then press **PLAY** (or **START** on
   the controller).

> If something goes wrong, the launcher shows you where the log is (`logs\`).
> Attach it when you open an issue.

### Linux

The package includes `dbz3` and `librexgpu-xenos.so`, but not the game. Extract
the tarball, place your legally obtained `default.xex` and `us/` or `eu/` next
to `dbz3`, then run `./dbz3`. See [`docs/LINUX.md`](docs/LINUX.md) for
dependencies and local builds.

### Valid file layouts

```
C:\Games\DBZ3\                 C:\Games\DBZ3\                 C:\Games\DBZ3\
├── dbz3.exe                   ├── dbz3.exe                   ├── dbz3.exe
├── default.xex                └── assets\                    └── DBZ3\            ← straight off the disc
└── us\ (and/or eu\)               ├── default.xex                ├── yae3_xenon.xex
                                   └── us\ (and/or eu\)           └── us\ (and/or eu\)
```

No need to rename the executable: the launcher finds it by **size and
checksum**. If you drop the HD Collection menu instead of the Budokai 3
executable, it tells you and blocks PLAY. The full original ISO (with the menu
at the root) works too: the launcher takes the Budokai 3 executable from inside.

### Which game files you need (extracted folder)

Only the executable and your region's data, not the whole ISO:

- **USA**: into `us\` → `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`,
  `data_ger.afs`, `data_ita.afs`, `data_spn.afs`, `data_usi.afs`,
  `data_yah.afs`, `adx_jpn.afs`, `adx_usa.afs`, `lang_jpn.afs`,
  `lang_usa.afs`, `opening.sfd`, `Ending00.sfd`, `Ending01.sfd`.
- **EU/PAL**: the same files into `eu\`.

You can verify them against `baserom.md`. To extract them from your legal ISO,
use a tool such as `extract-xiso` (reads the Xbox 360 FATX filesystem).

---

## Quick controls

| Key / button | What it does |
|---|---|
| **F1** or **Back + Start** | In-game quick menu (picture, sound & pad, display). Can be switched to L3 + R3 or keyboard only. |
| **F3** | FPS counter (game and display). |
| **F4** | All settings (the launcher, over the game). |
| **LB / RB** or **Ctrl + Tab** | Switch launcher tabs. |
| **START** | PLAY from the launcher. |

The launcher's hint bar shows keys or buttons (Xbox, PlayStation or Switch)
depending on what you used last.

---

## Mods and new characters

> Mods need the game data as a **folder** (ISO mode plays the game as it is).
> New characters also need the **USA version**.

### Character pack (`DBZ3HD-1.4.0-Personajes.zip`)

Seven **new** characters in their own cells of the character-select wheel
(they replace nobody): **Janemba, Android 19, Zarbon (with his transformation),
Dodoria, Guldo, Jeice and Burter**. Zarbon and Dodoria bring the moves, combos
and yells of their Budokai 1 version.

1. Close the game.
2. Download the ZIP from [Google Drive](https://drive.google.com/file/d/1zpwuuU7ITKZlC43nsTbp6s2wceBcwO85/view?usp=sharing) and copy its `mods`
   folder next to `dbz3.exe` (if Windows asks, merge the folders: nothing of
   yours is deleted).
3. Open `dbz3.exe`, check the **New characters** tab and press **PLAY**.

To remove one, untick it under **New characters → Installed**. Your saves are
never touched.

### Modding kit (`DBZ3HD-1.4.2.3-Kit-Modding.zip`)

The tools behind the launcher's modding tabs: create and **import characters**
(Budokai 1, Budokai 2, Infinite World and community models), custom capsules,
voices and yells, textures and model swap.

1. Install [Python 3.11 or newer](https://www.python.org/downloads/) and tick
   *Add python.exe to PATH*.
2. Copy the whole ZIP content into the game folder (`mod center hd` and
   `awo_tools` must end up next to `dbz3.exe`).
3. Double-click `instalar_requisitos.bat` (only once).
4. Open `dbz3.exe` → **New characters → Import a character from another game**:
   pick the game, search the character, check the name (you see live how it will
   look on the select screen) and press **Import**.

To import from other games, use **your own copies** in the `ps2_games` folder
(read its `LEEME.txt`). Super Dragon Ball Heroes and Shin Budokai 1/2 are
detected but still in development.

### Other mods

Mods live in `mods\<name>\` and replace AFS entries through an overlay, without
touching the original AFS files:

```
mods/<mod>/us/data_cmn.afs/<entry>/geom.bin   # override of one AFS entry
mods/<mod>/manifest.txt                       # metadata (name, author...)
mods/<mod>/.disabled                          # if present, the mod is OFF
```

They are managed from the launcher (**Mods**, **Textures**, **Model Swap** and
**New characters** tabs) or with the tools in `mod center hd/`. Guides in
[`docs/02_mods/`](docs/02_mods/) and the capsule format in
[`docs/03_formatos/CAPSULAS_B3.md`](docs/03_formatos/CAPSULAS_B3.md).

**Model swaps in any direction (virtual mid-insert).** A B3→B3 swap is a
per-entry override (~100 KB) served even when the binary is **larger** than the
original slot: the runtime presents the game a consistent AFS table and
translates the reads. That is how, for example, putting Goten into Krillin's
slot works.

**New select-screen cells.** Since v1.4.0 the runtime can also **append**
entries after the last one of an AFS, and the executable extends the select
wheel with new cells. The builder `mod center hd/roster_build.py` turns
`personaje.toml` folders into the generated mod `mods/_roster` (icons, name
banners, portraits, health-bar face, capsules and yells), and the launcher
rebuilds it automatically when you press PLAY.

---

## Features

**One universal executable**

- A single `dbz3.exe` (baseline SSSE3 runtime) that runs on any x64 CPU (Core 2
  2006 onwards), with no variants.
- **USA/EU dual core** with both recompilations inside and **executable
  auto-detection** by size and checksum.
- **Disc (ISO) mode**: play straight from the `.iso` without extracting anything.

**Video and performance**

- **FSR 1 / CAS**, **FXAA** and dither; internal resolution, MSAA and
  anisotropic filtering. **Applied live**, no restart.
- **More FPS with FSR** (Quality / Balanced / Performance / Ultra performance):
  renders below your resolution and FSR upscales it.
- **FPS panel (F3)** with game FPS, display FPS and a frame-time graph.
- **VRR** and a real **frame cap**; **per-GPU quality presets**.
- **Texture enhancement (experimental)** at runtime and **texture packs**.

**Audio and controls**

- Real **master volume** and **Mute**, applied on the fly.
- **Gamepad (XInput / SDL)** or keyboard, with remappable keys, deadzone, rumble
  and Xbox, PlayStation and Switch button names.
- When you leave the window: **mute** and/or **dim** (optional).

**Launcher**

- New design with cards and icons, a big **PLAY** button and **startup in under
  a second**.
- **Controller navigation** with a device-aware hint bar.
- **5 languages** (ES/EN/IT/DE/FR), clear messages when the executable is the
  wrong one, one-click **Repair installation** and an update check.
- **Portable, self-repairing** settings.

**Mods**

- **Per-entry AFS override** with **virtual mid-insert** and **appended
  entries** (new characters).
- **New characters** with icon, name banner, portraits and health-bar face
  rendered from the model itself, with a live preview in the launcher.
- **Importer** for Budokai 1, Budokai 2, Infinite World and community models
  (`.amb`, `.amo` + `.amt`).
- Per-character **capsules**, own **yells and voices** and **extra costumes**
  for existing characters (`traje.toml`).
- **B3↔B3 model swap** (183-character catalog) and **textures** (PNG).

**Diagnostics (optional)**

- Dev tab: I/O and performance logging and GPU diagnostic knobs. All **off by
  default**.

---

## Status

| Technique | Status |
|---|---|
| Native B3→B3 swap (~100 KB override) | Works in any direction (bins > or < slot) |
| B3 HD texture mod | Works (per-entry override) |
| 2+ simultaneous model/texture mods | Works (virtual mid-insert) |
| Music mod (og_music) | Works |
| Play from the ISO (disc mode) | Works (base game; mods need the folder) |
| USA/EU dual core (single binary) | Works (validated in-game) |
| New characters in their own cells | Works, experimental (USA version) |
| B1 / B2 / IW → B3 character ports | Works, experimental (importer) |
| Shin Budokai 1/2 and Super Dragon Ball Heroes | In development |

---

## Repository layout

```
DBZ-Budokai-3-HD-Collection/
├── default.xex               # NOT included. Game executable (USA or EU)
├── us/  eu/                  # NOT included. Data of each region
├── src/                      # Recompiler + launcher + mod system
│   ├── main.cpp              #   entry point, window, crash handler
│   ├── mods.cpp              #   mod system (AFS overlay)
│   ├── roster_ext.cpp        #   new characters (template, capsules, yells)
│   ├── select_ext.cpp        #   extended select wheel
│   ├── launcher/             #   launcher UI (ui_kit) + mod pipeline
│   └── ingame/               #   in-game menu and quick menu (quick_settings)
├── generated/                # NOT included. Code derived from your .xex
├── mod center hd/            # Python modding tools (roster_build, importar...)
├── awo_tools/                # Format converters and RE (AWO/AWG, PS2→HD, B1)
├── patches/                  # ReXGlue SDK overlay (see its README)
├── mods/                     # User mods (empty)
├── tools/                    # Release, modpack and utility scripts
├── docs/                     # Full documentation
├── CHANGELOG.md              # What's new per version
├── baserom.md                # Required game files + how to extract them
└── LICENSE                   # MIT (NovaPowers)
```

---

## USA / EU regions

The USA (`yae3_xenon.xex`) and EU (`yae3_xenon_eu.xex`) executables are
different builds and the dual core includes the recompilation of each one. The
launcher identifies which one you placed by checksum and uses the matching
code; if they don't match, it warns you and blocks PLAY.

The data region (the `us\` or `eu\` folder) and the language are chosen in the
launcher. Saves are shared between regions. The v1.4.0 new characters only
apply with the **USA** executable and data.

---

## Building from source

You need a C++23 compiler (LLVM clang on Windows), CMake ≥ 3.25 and the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) **v0.10.0** with our
overlay applied.

> Copy `patches/rexglue-sdk/.` over a clean checkout of the SDK's `v0.10.0` tag
> (see `patches/README.md`) and build the runtime. Without the overlay, large
> swaps, new characters and the quick menu do not work.

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
uploaded (see `generated/README.md` and `.gitignore`). The release package is
assembled by `tools/make_release.ps1` and the modpacks by
`tools/make_modpacks.py`.

---

## Credits

- **@ArielPVB**: Guldo, Jeice, Burter, Zarbon (base and monster form), Dodoria
  and Android 19 models in the character pack.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — static recompilation
  tools and runtime (derived from [Xenia](https://xenia.jp)).
- [DBZ Burst Limit Recompiled](https://github.com/iExplosiveRage/DBZ-Burst-Limit-Recompiled)
  by **iExplosiveRage** — inspiration and source of the SDK improvements adapted
  in v1.4.0 (controller quick menu, FPS panel, live video settings, FSR below
  native resolution), taken from the `burstlimit` branch of
  [iExplosiveRage/rexglue-sdk](https://github.com/iExplosiveRage/rexglue-sdk).
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — SDK API reference
  (reference only; not a base or a code copy).
- The Budokai modding community — tools, reference lists and the models and
  Infinite World ports the character pack is based on.
- [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) —
  controller mappings (`gamecontrollerdb.txt`).
- **NovaPowers** — author of the launcher, the mod system and the tools.
