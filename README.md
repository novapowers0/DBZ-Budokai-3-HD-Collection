<p align="center"><img src="docs/logo.png" alt="Dragon Ball Z Budokai 3 HD Recomp" width="480"></p>

# DBZ Budokai 3 HD Collection

English · [Español](README_ES.md)

A native port of *Dragon Ball Z: Budokai 3 HD Collection* (Xbox 360), built with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). It statically recompiles the original PowerPC code and adds a launcher, a mod system and new ways to play. You'll need your own legal copy of the original game; its files are not included.

**[Download v1.5.7 — latest release](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7)** · **[Launch video](https://youtu.be/47XrgFQ8zaQ)** · [Changelog](CHANGELOG.md)

## What you can play

| Mode | Where to start and what's in v1.5.7 |
|---|---|
| **Dragon HUB** | Go past the last row of the game's main menu. The HUB supports mouse navigation, and Classic Challenges still has its own entry. |
| **Dragon Budokai Survivors (BETA)** | Dodge hordes as your techniques fire automatically. Collect gems, combine cards to evolve techniques and spend Zenie on permanent upgrades. Eight fighters, six stages and local records. |
| **Dragon Rogue (BETA)** | Climb through eight paths, build synergies with Ki, Body and Support cards, and find transformations and fusions. Save and resume runs, try Kami's Training or take on the daily Abyss challenge. |
| **Hero Mode and character creator (BETA)** | Explore the updated Room, Techniques, Evolution and Collection. Save outfits, share them by code and assign them to compatible forms. |
| **Dragon Tactics (EXPERIMENTAL)** | Build teams with the updated shop and fighter carousel. Match history for each rival is available when you resume. |
| **Dragon World (EXPERIMENTAL)** and **Babidi’s Ship (BETA)** | Budokai 2 board progression and battle improvements, plus updated timing for Babidi's dialogue. In local combat, Budokai 2's Plains can change areas mid-fight when you knock your opponent into the wall that triggers the scene. This requires the US HD core and the stage converted from your own Budokai 2 copy. |
| **Infinite World (EXPERIMENTAL)** | Mission map and minigame updates, including more original logic and a translated startup and drawing path in compatible courses. |
| **Mr. Satan’s Legend (BETA)** | Round one uses the long scene's voices and music when available from your own Budokai 1 copy. |
| **Tag Team (BETA)** and **Online (BETA)** | Switch fighters again after dropping out of the highest added forms. Online shows clearer error messages and saves a connection report. |

The stage wheel supports up to 238 **added** stages, with random selection among visible stages and music previews where a track is available. See the [release notes and known limits](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7) for the full list.

## Platforms and downloads

Use **Assets** on the [v1.5.7 release page](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/releases/tag/v1.5.7). GitHub's automatic **Source code** archives are for building from source, not for playing.

| Platform | Package |
|---|---|
| **Windows x64** | `DBZ-Budokai-3-HD-Collection-v1.5.7.zip` — available now; dual US/EU core with Direct3D 12 and NVIDIA DLSS on compatible hardware. |
| **Linux amd64 (Vulkan, BETA)** | The automatic build produces `DBZ-Budokai-3-HD-Collection-v1.5.7-linux-amd64.tar.gz`. Not attached at the time of writing; check Assets. Once available, extract it, place your `default.xex` and matching `us/` or `eu/` beside `dbz3` and run `./dbz3`. See the [Linux guide](docs/LINUX.md). |
| **Mac with Apple Silicon (EXPERIMENTAL, UNTESTED)** | M1 or newer. The automatic build produces `DBZ-Budokai-3-HD-Collection-v1.5.7-macos-arm64-EXPERIMENTAL.tar.gz`. Not attached at the time of writing; check Assets and follow the included `README_MAC.txt` if it appears. |

## Install on Windows

1. Extract the **whole** Windows ZIP into a writable folder, such as `C:\Games\DBZ3\`.
2. Open `dbz3.exe` and select the complete Xbox 360 ISO from **your own legal copy**, or select a folder containing its Budokai 3 executable (`default.xex`) alongside matching `us/` or `eu/` data. The launcher can also recognise an intact `DBZ3/` folder from the disc without renaming the executable. See the [required file list and checksums](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/baserom.md).
3. Press **PLAY**. For modes and imports that need PS2 data, use **Game → Choose ISO** in the launcher and select the relevant disc image from your own legal copy.

See the [installation guide](RELEASE_README.md) for folder layouts. In game, **F1** opens quick settings and **F3** shows the FPS counter.

## Update

Close the game. Back up `dbz3_user.toml`, your saves and user data (`user_data/`, or `Documents/dbz3` if the launcher uses that folder), and `mods/`. Extract the full new package over your installation so the executable, DLLs and other bundled files are updated together. Keep your own game data and avoid mixing DLLs from different releases.

On the first launch after updating, settings listed in the migration history change only if they still match the old defaults. Your other values are kept. The launcher moves listed obsolete files to a versioned folder under `_respaldo_actualizacion/` instead of deleting them. Keep your manual backup as well.

## Mods and Modding Kit

- **Install and manage mods:** use the launcher's **Mods** tab, or put each mod folder under `mods/` beside `dbz3.exe`. The launcher can prepare the local data needed for compatible ISO-mode mods without modifying your ISO.
- **Optional Windows Kit:** extract `DBZ3HD-1.5.7-Kit-Modding.zip` into the installed game folder so `DBZ3_ModKit.bat`, `mod center hd/`, `awo_tools/` and `python/` sit beside `dbz3.exe`. Open `DBZ3_ModKit.bat` and start with the installation check. For tools that need direct access to game files, use the extracted data folder. The Kit includes portable Python and its libraries; if you use your own Python, use 3.11 or newer with Tkinter and run `instalar_requisitos.bat` to install the libraries. See the [Modding Kit quick guide](docs/02_mods/KIT_GUIA_RAPIDA_EN.md).
- **Import from your own games:** Budokai 1 and 2 character recipes now use base moves and cameras from Budokai 3 HD; these recipes no longer require a separate PS2 copy of Budokai 3. Rebuild affected Shin Budokai mods with the updated Kit for the disappearing-parts fix during Dragon Rush.
- **Optional character pack (WIP):** [`DBZ3HD-1.4.3-Personajes.zip`](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) is separate from the release and intended for the US HD core. With the game closed, merge its `mods/` into your game folder and check **New characters** in the launcher. Back up any mod you plan to replace.

## FAQ

- **Why won't PLAY start?** Check that you selected Budokai 3, not the HD Collection menu or Budokai 1 executable, and that the data matches the US or EU version. If you've just updated, re-extract the full Windows ZIP if files are missing or mismatched.
- **Where are Survivors and Rogue?** Inside Dragon HUB, beyond the last main-menu row. Look there for the mode entries, not in the launcher's mod list.
- **Can I play Online across systems?** Long matches between two PCs and Windows/Linux compatibility have not been confirmed. Use the same release and check the connection report if you can't join.
- **How do I report a problem?** Open an [issue](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/issues) with your version, operating system, game region, steps to reproduce and the newest file in `logs/`.

## Documentation and source builds

See the [documentation index](docs/README.md), the [build guide](docs/05_build/COMO_COMPILAR.md) and the [ReXGlue SDK patches](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/patches/README.md). The project uses ReXGlue v0.10.0 with the project patches. You generate the recompiled source locally from your own `.xex`.

## Credits and support

- **🇮🇹⚠ 𝐒𝐏𝐈𝐃𝐄𝐑 ⚠🇮🇹** — *Infinite World* decompilation used to port the map, missions, minigames and animations.
- **@ArielPVB** — models for Guldo, Jeice, Burter, Zarbon (both forms), Dodoria and Android 19 in the optional character pack.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — static recompilation tools and runtime, derived from [Xenia](https://xenia.jp).
- [DBZ Burst Limit Recompiled](https://github.com/iExplosiveRage/DBZ-Burst-Limit-Recompiled) by **iExplosiveRage** — inspiration and SDK improvements for the quick menu, FPS panel, live video settings and FSR, adapted from the `burstlimit` branch of [iExplosiveRage/rexglue-sdk](https://github.com/iExplosiveRage/rexglue-sdk).
- [WistfulHopes/DBZ1](https://github.com/WistfulHopes/DBZ1) — SDK API reference.
- The Budokai modding community — tools, reference lists, models and Infinite World ports used by the character pack. **[SSGPrinceVegeta](https://x.com/SSGPrinceVegeta)** — HD texture pack.
- [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) — controller mappings.
- **NovaPowers** — launcher, mod system and tools.

[Support the project on Ko-fi](https://ko-fi.com/novapowers). Support is optional; the port is free.

Copyright (c) 2026 **NovaPowers** · [MIT License](https://github.com/novapowers0/DBZ-Budokai-3-HD-Collection/blob/master/LICENSE). Unofficial, non-commercial project for research and preservation. Not affiliated with or endorsed by any Dragon Ball rights holder.
