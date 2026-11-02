<p align="center"><img src="docs/logo.png" alt="Dragon Ball Z Budokai 3 HD Recomp" width="480"></p>

<p align="center"><a href="https://ko-fi.com/novapowers"><img src="https://img.shields.io/badge/Ko--fi-Support%20the%20project-FF5E5B?logo=ko-fi&logoColor=white" alt="Support the project on Ko-fi"></a></p>

# DBZ Budokai 3 HD Collection

**Windows | Linux** · English · [Español](README_ES.md)

A native PC port of *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360), built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The original PowerPC code is statically recompiled into a standalone executable with its own launcher and mod system. Native Linux builds use Vulkan and SDL3.

[![Release](https://img.shields.io/github/v/release/novapowers0/DBZ-Budokai-3-HD-Collection?sort=semver&style=flat-square&color=orange&label=Release)](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)

| | |
|---|---|
| Players | 1–2 (versus) |
| Platform | Windows / Linux |
| Engine | Xbox 360 (ReXGlue SDK) |
| Genre | 3D fighting |
| Version | v1.5.6 |

Copyright (c) 2026 **NovaPowers**. MIT License (see `LICENSE`).

**[Download the latest release](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest)** · [CHANGELOG](CHANGELOG.md) · [Installation](#how-to-install-step-by-step) · [Mods](#mods-and-new-characters)

🎬 [1.5.6 video](https://youtu.be/J2P42s0yNVI) · [1.5.0 trailer and installation tutorial (ES/EN)](https://youtu.be/zcW-9RK046Q) · [Short](https://youtube.com/shorts/uqRetUHTBaA)

## What's new in 1.5.6

- **Dragon Tactics (experimental):** an auto-battler tournament against seven bots, with 34 B3 fighters, 11 synergies, shop, capsules, saved matches and upgrades to four stars. Mouse/controller support; optional preparation timer, off by default. Some fighters can still get trapped behind allies.
- **Characters:** L1/R1 select pages for more than 24 added fighters, selectable extra costumes even with locked native costumes, and repaired missing technique labels when rebuilding the roster.
- **Shin Budokai:** fixed SB1 skin references that left the body rigid with floating head/hands. Characters with a B3 HD equivalent use that native body with SB moves. Krillin, Vegeta and Janemba SB1 were checked in battle; other models were checked offline. Rebuild requires the PSP source ISO; the previous mod is backed up.
- **Infinite World / Dragon Mission (experimental):** targeted intro/result, timing, HUD, ultimate and ki corrections. Freezer and Krillin retain their own height in 7D7. Full camera, movement and effect parity remains unfinished.
- **Budokai 2 and Babidi's Ship (experimental):** menu/figure corrections and removal of the extra pre-battle VS card. The original floor-specific HUD and complete original result comparisons remain unfinished.
- **Mr. Satan legend (beta):** targeted improvements to the car, smoke, fades, READY shots and round-summary layout.
- **Hero mode (beta):** an Export button saves your hero as a separate character (PC only) with an art-free recipe; your name in dialogue and selection, saga-map events, Dragon Balls and Shenron's wish; more glove corrections and optional XV2 parts from your copy. Story translation is partial.
- **Modding Kit:** assistants and previews, a B3 PS2 mode that builds a separate ISO from your copy, Optional art for your own HUB images, and a DAE costume converter. No third-party art is included.
- **Interface:** more translated news and recipe text. Linux Repair installation checks `.so` libraries instead of Windows DLLs (#62); this branch was reviewed in source, not tested on Linux.
- **Online (beta):** a simpler screen: choose 1 vs 1 or Tag Team, and each player fights with their own Custom list. Builds from before 1.5.6 cannot join. Consent is asked before using the rendezvous service. Measured on one PC: lockstep with up to 200 ms latency and 20 % lost inputs kept every checksum equal; matchmaking paired two copies in 7 s. Two real PCs and Windows-Linux were not tested; rollback stays experimental and off by default.
- **Fusion and Edit Skills:** Potara (Gotan) and mod capsules now appear in Edit Skills; a failed Yamhan fusion no longer closes the game. The real fusion button sequence was not seen.
- **Gogeta SS4 / Vegito:** their ultimates play to the end (measured damage after the fix). Baby's ultimate measured 1081 damage against a still opponent.
- **Interface:** the Select Characters box no longer covers the title, 4:3 bands are cleaner and What's New scrolls.

Full details and the measured limits are in the [CHANGELOG](CHANGELOG.md).

## Legal notice

The game is **not included**. Supply the executable (`default.xex`) and regional `data_*.afs` files from your own legal copy, or use your original ISO. `baserom.md` lists file sizes, checksums and extraction information. Recompiled `generated/` code is produced locally from your executable and is not uploaded.

Unofficial, non-commercial research and preservation project. Not affiliated with or endorsed by Bandai Namco, Shueisha, Toei Animation or any Dragon Ball rights holder.

## How to install (step by step)

### Windows

1. Download and extract **`DBZ-Budokai-3-HD-Collection-v1.5.6.zip`** from [Releases](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/latest), for example into `C:\Games\DBZ3\`.
2. Add your data:
   - **ISO:** place the original `.iso` beside `dbz3.exe`, or select it in the launcher. Mods are prepared on first use.
   - **Extracted files:** place the disc's `DBZ3\` folder beside `dbz3.exe`, or supply `default.xex` plus `us\` and/or `eu\`.
3. Open **`dbz3.exe`**. The launcher explains missing files and follows your Windows language on first start. The **First steps** button reopens the guide.
4. Choose region, language, video and sound, then press **PLAY** or controller **START**.

Attach the log from `logs\` when reporting a problem.

### Linux

Use the Linux tarball when available in Releases. Extract it, place your legally obtained executable and `us/` or `eu/` beside `dbz3`, then run `./dbz3`. See [docs/LINUX.md](docs/LINUX.md) for dependencies and builds. This closing check was run on Windows; it does not certify Linux behavior.

### Extracted file layouts

```text
C:\Games\DBZ3\
├── dbz3.exe
├── default.xex
└── us\ (and/or eu\)
```

The disc's `DBZ3\yae3_xenon.xex` layout and an `assets\` layout are also recognized. No renaming is needed: the launcher identifies the executable by size and checksum. The HD Collection menu executable is not the Budokai 3 executable; the full original ISO works because the launcher locates the game inside it.

For an extracted region, keep `data_cmn.afs`, `data_eng.afs`, `data_fra.afs`, `data_ger.afs`, `data_ita.afs`, `data_spn.afs`, `data_usi.afs`, `data_yah.afs`, `adx_jpn.afs`, `adx_usa.afs`, `lang_jpn.afs`, `lang_usa.afs`, `opening.sfd`, `Ending00.sfd` and `Ending01.sfd`. Check them against `baserom.md`.

## Quick controls

| Key / button | Action |
|---|---|
| **F1** / **Back + Start** | In-game quick settings; shortcut can be changed. |
| **F3** | Game/display FPS and frame-time graph. |
| **F4** | Launcher settings over the game. |
| **LB / RB** / **Ctrl + Tab** | Launcher tabs. |
| **A / B** | Select / back. |
| **START** | PLAY from the launcher. |

The hint bar follows the last input device; Xbox, PlayStation and Switch button names are available.

## Features

- **One dual-region executable:** USA/EU executable detection, baseline SSSE3 runtime, ISO or extracted-folder data.
- **Video:** internal resolution, MSAA, anisotropic filtering, FSR/CAS, FXAA, dither, texture enhancement/packs and live adjustments. Experimental options remain labelled in the launcher.
- **Audio/input:** live master volume/mute, XInput/SDL gamepads, remappable keyboard controls, sensitivity, deadzone and rumble. Optional mute/dim when unfocused.
- **Launcher:** controller navigation, seven interface languages (ES/EN/IT/DE/FR/PT/JA), portable settings, repair report and update check.
- **Dragon HUB:** beta/experimental modes including Tag Team, Hero, Tactics and B1/B2/IW campaign gateways. Source games are required for their imported content; parity is not complete.
- **Online (beta):** room-code play, optional compatible search after consent, compatibility checks and per-player local music. Added-character page selection reached battle on two local copies. Search pairing was measured only with a local fake service, two homes. Rollback is experimental and off by default; two-home acceptance and transformations under rollback remain unverified.
- **Diagnostics:** optional logs and GPU controls in Advanced; no diagnostics are required to play.

### Optional HD videos (Windows)

An opening/ending pack is downloaded separately through **Nova's Mods** (about 287 MB). It is not in the game archive. Regional audio and original credits remain; original videos are used when disabled or unavailable. Resume, SHA-256 checks and moving removal to backup were tested. A real story's final credits/return, F1 pause and modest-PC performance remain unverified. Linux uses original videos.

### Hero and optional XV2 parts

Create and name a fighter, follow saga maps, earn rewards/Dragon Balls and make a wish through the native scene. B3 parts are the default. Optional XV2 parts read your own installed copy; fitting remains approximate in some clothes/hips. Templates have French/German/Italian translations checked in generated data, but saga titles/intros/endings still contain English. Portuguese/Japanese story dialogue falls back to English. Hero-created XV2 parts have not been validated through the experimental standalone spring-physics route.

## Mods and new characters

Mods work in ISO mode after initial preparation. Added character cells use the **USA** executable and data. Enable/disable mods in **Mods** or **New characters**; the launcher rebuilds `mods/_roster` on PLAY.

**Nova's Mods** offers separate downloads. A mod being prepared or tested does not mean it has already been published. Game assets, extracted voices and generated character mods are not in the source repository or main game archive.

The older optional [seven-character pack](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) contains Janemba, Android 19, Zarbon, Dodoria, Guldo, Jeice and Burter in separate cells. It is a WIP pack, not a claim that its older download contains every later local adjustment. Burter/Jeice/Guldo use donor moves rather than their own complete source movesets.

### Modding Kit

Use the separate **Kit-Modding** archive available in Releases. Place `mod center hd` and `awo_tools` beside `dbz3.exe`; the kit includes Python and its libraries. Launch `DBZ3_ModKit.bat` or use the launcher's import tools. Source-game imports read your own copies; see the kit's `ps2_games/LEEME.txt`.

- Characters, capsules, voices/yells, textures, models, stories and HUB challenges; step-by-step assistants, Build and Test and an Expert area.
- Image/texture/model previews, audio playback and waveforms; capsule/aura previews are illustrative.
- **B3 PS2 mode:** texture/model-swap/looped-music mods become a separate ISO. Selection/battle checks used US `SLUS-20998` in PCSX2; other regions and real consoles are untested. The preview explicitly uses the HD model.
- **Optional art:** review your own PNG names, shape, transparency and damaged images, then install recognized HUB pieces as a switchable mod with backups/Undo. No artwork is supplied by this tool.
- **DAE:** costume conversion through the existing conversion path; a local Piccolo costume was selected and used in battle. Masked mouth animation remains unverified.

### Mod layout

```text
mods/<name>/us/data_cmn.afs/<entry>/geom.bin
mods/<name>/manifest.txt
mods/<name>/.disabled                        # OFF when present
```

Per-entry AFS overlays leave original files intact. Virtual mid-insert supports larger replacements; appended entries provide new character cells. `personaje.toml` recipes generate icons, portraits, labels and capsule/yell resources in `_roster`. See [mod guides](docs/02_mods/), [capsules](docs/03_formatos/CAPSULAS_B3.md) and [optional plugins](docs/02_mods/PLUGINS.md).

## Repository layout

| Path | Content |
|---|---|
| `src/` | Launcher, runtime hooks, modes and mod support. |
| `generated/`, `generated_eu/` | Locally regenerated code; only instructions are distributed. |
| `mod center hd/`, `awo_tools/` | Modding tools and format converters. |
| `patches/` | ReXGlue SDK overlay. |
| `tools/`, `tests/` | Build/release utilities and tests. |
| `docs/` | [Documentation index](docs/README.md). |
| `mods/` | User mods, empty in the release. |
| `baserom.md` | Required source-game files. |

## Building from source

Requires C++23 (LLVM clang on Windows), CMake 3.25+ and **ReXGlue SDK v0.10.0** with this repository's overlay. Apply `patches/rexglue-sdk/.` to the SDK and follow [patches/README.md](patches/README.md) to build/install it.

**Generate the US code before the first CMake configure.** A fresh checkout has no generated selector includes; CMake's selector generator needs the US output already present. With the SDK's `rexglue` executable on PATH and your legal source executable in the location specified by `dbz3_manifest.toml`:

```sh
rexglue codegen dbz3_manifest.toml
cmake -S . -B out/build/win-amd64-release -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe" -DCMAKE_PREFIX_PATH="C:/path/to/rexglue"
cmake --build out/build/win-amd64-release
```

Use your compiler/SDK paths on Linux. The example is the default US build; a dual-region build also requires the EU generated code and its documented fixes. Configuring after generation adds `src/net/rb_store.h` to the US generated header only when absent; generated guest sources stay local. This source-order review is not a clean build test. See [build instructions](docs/05_build/COMO_COMPILAR.md) for project-specific requirements.

## Known limits

New modes/imports retain BETA/EXPERIMENTAL labels. No full 18-minigame IW visual parity, full ship-floor HUD parity, complete native Nova fusion flow, long two-home online acceptance or complete Linux regression check is claimed. English remains in parts of Hero story content. See the [CHANGELOG](CHANGELOG.md) for feature-specific evidence and limits.

## Support and credits

[Support the project on Ko-fi](https://ko-fi.com/novapowers). The game and content remain free; support is optional.

- **NovaPowers** — launcher, mod system and tools.
- **🇮🇹⚠ 𝐒𝐏𝐈𝐃𝐄𝐑 ⚠🇮🇹** — Infinite World decompilation used for campaign work.
- **@ArielPVB** — Guldo, Jeice, Burter, Zarbon, Dodoria and Android 19 models in the separate character pack.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — recompilation tools/runtime, derived from [Xenia](https://xenia.jp).
- **iExplosiveRage**, [DBZ Burst Limit Recompiled](https://github.com/iExplosiveRage/DBZ-Burst-Limit-Recompiled) and [SDK branch](https://github.com/iExplosiveRage/rexglue-sdk) — improvements adapted in 1.4.0.
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — SDK API reference.
- The Budokai modding community — tools, reference lists and work underlying optional ports.
- [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) — controller mappings.
