# Changelog

All notable changes to **DBZ Budokai 3 HD Collection** (PC port). Older versions
are summarized at the end; the full history (in Spanish) is in
`docs/01_estructura/HISTORICO_RELEASES.md`.

Downloads: [latest release](../../releases/latest).

## v1.4.3.1 "1.4.3 EX" (2026-10-10)
- Performance: the host frame cap no longer sleeps the GPU thread at 60 (fixed "stuck at 30 FPS" on 60 Hz screens), evenly spaced guest vblanks, launcher capped at 60, process priority above normal, shipped shader/pipeline cache, AFS mod lookups cached and readahead with mods, Vulkan FIFO relaxed.
- Compatibility: high-performance GPU on hybrid laptops (D3D12 and Vulkan, Optimus/PowerXpress exports in the exe), AMD RDNA "device lost" fix (MaxAs clamp, from Xenia Canary), Intel Arc on the RTV path, automatic Vulkan fallback when Direct3D 12 is unavailable, better auto preset (AMD APUs, 4 GB cards), FidelityFX DLL delay-loaded, bilingual GPU error messages.
- HD texture packs: replacements re-checked by content (no wrong texture when memory is reused); Linux HD brightness; FR/DE/IT text.

## v1.4.2.3 "1.4.2 EX" (2026-10-07)
- Launcher Mods tab: "Easy textures" (capture while playing, prepare pack-named PNGs with the bundled Python, "My texture pack"), drag-and-drop install of mod zips and edited PNGs, plain-language broken-mod warnings (empty, nested folder with a Fix button, missing files, invalid texture names/sizes) and a warning next to PLAY.
- Modding Kit ships a portable Python (python.org build with numpy, Pillow, scipy, tkinter); the launcher and DBZ3_ModKit.bat use it first.
- Default texture capture folder is next to the game (was a developer path).
- Shin Budokai ports: empty donor yell slots are enabled (SQ flag + volume), specials live in codes 0x240-0x27F, own ball ASTs use low codes with both #AME links, recoil hold, full recolor of ball effects, SB sub-block header fix, donor ki blasts grafted (holding E no longer crashes).

## v1.4.2.2 (2026-10-07)
- New characters now work with the European (EU/PAL) executable too (region-aware guest addresses and hooks; roster_build reads eu/ and data_eng).
- The Kit ships Roboto Bold (Apache 2.0) as its font fallback.

## v1.4.2.1 (2026-10-07)
- New characters not showing: the rebuild crashed with "OSError: cannot open resource" when Arial Rounded (an Office font) was missing; fonts now fall back.
- The New characters tab warns when playing from the ISO (mods are not loaded) or with the EU version.

## v1.4.2 (2026-10-06)
- NVIDIA DLSS (beta, RTX + D3D12) and AMD FSR 3 (beta) with real depth, jitter, motion vectors and reactive mask; live switching; FSR 3 fallback.
- Shadows fixed with temporal upscalers; "More FPS" presets no longer drop below their promised scale.
- HD rim light ("HD shine") on characters is adjustable (Native mods tab, F1).
- Mod Kit: Shin Budokai 1 / Another Road importer (sbport, sb_tecnicas, sb_voces), Super Dragon Ball Heroes models (sdbh_model), camera Studio with Blender round trip, per-form models, per-capsule ki and donor physics in roster_build.

---

## [1.4.1] - 2026-10-05

Performance and polish.

### Fixed

- **Fixes for the 30 FPS lock-up on powerful PCs (issue #8)** (not reproducible on the dev PC; the new telemetry below names the cause on affected machines). The runtime now requests a
  1 ms system timer, uses high-resolution waitable timers for its short sleeps
  (guest vblank, GPU register waits, the game's own `Sleep` calls) and opts the
  process out of Windows power throttling (EcoQoS / timer-resolution
  throttling), so Windows 11 neither stretches 1 ms waits to ~15 ms nor parks
  the game on efficiency cores.
- The low-FPS warning no longer suggests "lower the internal scale to 1x" when
  it is already 1x: it lists only the expensive settings that are active, and
  says when the bottleneck is the CPU.

- **Crash on the character select screen with the new characters pack.** A new
  cell borrows its host's saved costume and costume count (Janemba on Krillin), so
  the game asked for costume 1-3 of a 1-costume character and read past its model
  list (null pointer in `sub_8208DDF0`, called from `0x82134A98`). Model/mouth lists
  are now padded to 8 costumes (repeating the first one) and costume cycling on a
  new cell only walks that character's own costumes.
- **Music silent with a music pack + the characters mod.** The virtual AFS table was
  built from the original `adx_usa.afs` while the data came from the music pack's
  whole-file replacement; it now uses the file actually opened.
- The launcher no longer runs `roster_build.py` on PLAY when the modding kit is not
  installed (the characters pack ships a prebuilt `_roster`).

### Changed

- **DRED (GPU crash diagnostics) is off by default** (it was always on since
  1.3.0, with a cost on every command list and resource). The game turns it on
  automatically for the session after a `D3D12 device removed`.
- The `perf` log line adds `gpu_wait=` (ms per frame blocked on the GPU),
  `syncs=` and `cp_wait=` (ms per frame the GPU thread waited for the game).
- The `entorno` line (component versions, OS, RAM) is also logged when the
  launcher is skipped.
- `dbz3: tiron N ms (...)` lines for guest frames over 50 ms (D3D12 and Vulkan),
  `perf` lines on Vulkan, and `dbz3: audio pico=... rms=...` (guest mix level) with
  `dbz3_perf_logging`.
- Launcher: the New characters tab warns when the game is the EU/PAL version
  (new characters need the US/NA one).

### Modding Kit 1.4.1

- New **Diagnostics** page (`diagnostico.py`, also usable from the console):
  reads a game log and explains, in plain language, old/mixed installs, 30 FPS
  lock-ups (CPU vs GPU), GPU resets, sudden closes, slow disks and new
  characters on the EU version; copies the report for Discord.
- Checks that the new characters mod is built for all five game languages.
- `make_modpacks.py --solo kit`: the characters pack stays the 1.4.0 one on
  Google Drive.

---

## [1.4.0] - 2026-10-04

The biggest update so far: an in-game quick menu, live video settings, a
redesigned launcher that opens instantly and works with a controller, and a
full system for **new characters** in their own select-screen cells.

### Added

- **In-game quick menu** — press **F1** or **Back + Start** on the controller
  during a match. Three pages (Picture, Sound & pad, Display) plus "All
  settings" (F4). Changes apply instantly and are saved automatically. The
  button combo can be changed to **L3 + R3** or keyboard only. While the menu
  is open the game does not receive your inputs.
- **"More FPS with FSR"** (Native / Quality / Balanced / Performance / Ultra
  performance) — the game renders below your resolution and AMD FSR upscales
  it, for real FPS gains. Available in the quick menu and in the launcher's
  scaling tab.
- **New FPS panel (F3)** — game FPS (frames the game actually produces) and
  display FPS, with a frame-time graph and a selectable screen corner. It
  replaces the old FPS window; the launcher's "Show FPS" option drives it.
- **Controller support in the launcher** — navigate with the pad, **LB / RB**
  (or **Ctrl + Tab**) to switch tabs, **START** to play. A hint bar next to the
  buttons shows keyboard keys or Xbox / PlayStation / Switch buttons depending
  on what you used last.
- **New characters in their own cells** of the character-select wheel, without
  replacing anyone: own icon, name banner, portraits, health-bar face, model
  preview framed to their height, techniques and capsules. Your save format does
  not change.
- **Character pack** (`DBZ3HD-1.4.0-Personajes.zip`, optional download):
  **Janemba, Android 19, Zarbon (with his Monster form), Dodoria, Guldo, Jeice
  and Burter**. Zarbon and Dodoria use their **Budokai 1 moves, combos and
  yells**.
- **"New characters" launcher tab** — list of installed characters (tick to
  enable / untick to undo), a slot table (free / taken / conflicts), a
  per-character editor with **live previews** (icon, name banner, P1/P2
  portraits, health-bar face) and "Create character" from your own files. The
  combined mod is rebuilt automatically when you press PLAY.
- **Character importer** ("Import a character from another game"): pick the game,
  search the character, check the name (live preview of the banner) and import.
  Sources: **Budokai 1** and **Budokai 2** (from your own ISOs), **Infinite
  World** (including community IW→B3 moveset ports) and **community Budokai 3
  models** (`.amb`, `.amo` + `.amt`). Shin Budokai 1/2 and Super Dragon Ball
  Heroes are detected but marked "in development".
- **Modding kit** (`DBZ3HD-1.4.0-Kit-Modding.zip`, optional download): the
  Python tools behind the modding tabs, the reference lists the importer uses,
  guides and a one-click requirements installer (`instalar_requisitos.bat`).
- **Own yells and voices** for new characters (a custom audio path, so no XMA
  encoder is needed), **custom capsules** per character (also importable from
  B1/B2/IW), working **Edit Skills** and inventory for new characters, and
  **extra costumes** for existing characters (`traje.toml`).
- **Model gateway** for modders (`awo_tools/b3_gateway.py`): PS2 models, OBJ and
  glTF 2.0 into the HD format, and export to OBJ for editing in Blender.
- **"Save at 100%" native mod** is now functional: it installs a complete
  Xbox 360 save that you provide (`mods_nativos/partida_100`), backing up your
  current save first, with a restore option.

### Changed

- **Launcher redesign** — new fonts and icons, cards, label/control rows, info
  markers instead of hover-only tooltips, a big orange PLAY button. Language
  moved to the right column of the Video tab.
- **Launcher startup from ~31 s to under 1 s** — the game folder is no longer
  scanned in full at startup; folders are listed on demand. (The SDK option
  `vfs_eager_scan = true` restores the old behaviour if ever needed.)
- **Video settings apply live** — internal resolution, upscaler (Basic / AMD
  CAS / AMD FSR), sharpness and FXAA change without restarting the game.
- **F4 over the game** now blocks controller input to the game while it is open
  and applies the changes when you close it.
- The old FSR quality setting is replaced by "More FPS with FSR" (default:
  Native, i.e. the same image as before).
- **Height correction for ported characters** — models ported from other games
  floated or sank; their hip height is now rescaled to the new skeleton.
- New runtime DLLs (`rexruntime.dll` 11,034,624 B, `rexgpu-xenos.dll`
  6,372,864 B, build stamp 1.4.0). Do not mix them with older versions.

### Fixed

- **Crash when using the quick menu** — the controller was read from two
  threads at once without a lock (heap corruption). All device access is now
  locked.
- **Launcher stuck on "working…"** forever when a modding tool printed 4 KB or
  more of output (one-byte buffer overflow in the tool runner).
- **Black screen** with the FSR2 / FSR3 present effects (runtime fix).
- The runtime configuration file is now always saved as valid TOML.
- Zarbon and Dodoria no longer show the donor's transformation effect or speak
  the donor's phrases, and their Dragon Rush keeps its final burst.

### Known issues

- New characters are **experimental**: some Budokai 1 grab techniques play as a
  normal hit and some effects are not perfect.
- New characters and mods need the game data as an **extracted folder** (not ISO
  mode), and new characters need the **USA** executable and data.
- The importer and character creator need the **Modding kit** and Python 3.11+.
- The new launcher and quick-menu texts are in Spanish and English; Italian,
  German and French show them in English for now.
- The Vulkan backend still works but is much slower than D3D12 (default).

### Credits for this version

The quick menu, FPS panel, live video settings, FSR below native resolution, the
FSR2/3 black-screen fix and the TOML fix are adapted from the `burstlimit`
branch of [iExplosiveRage/rexglue-sdk](https://github.com/iExplosiveRage/rexglue-sdk)
(the SDK of **DBZ Burst Limit Recompiled**), restyled for this port. Thanks to
the Budokai modding community for the models and Infinite World ports used in
the character pack.

### Resumen en español

- **Menú rápido en partida** (F1 o Back + Start; también L3 + R3 o solo
  teclado): imagen, sonido y mando, pantalla y «Todos los ajustes». Se aplica al
  momento y se guarda solo.
- **«Más FPS con FSR»**: dibuja por debajo de la resolución y FSR reescala; FPS
  reales en equipos modestos.
- **Panel de FPS nuevo (F3)** con FPS del juego, FPS de pantalla y gráfica.
- **Ajustes de vídeo en vivo**: resolución interna, escalado, nitidez y FXAA sin
  reiniciar.
- **Launcher rediseñado**, abre en **menos de 1 s** (antes ~31 s) y se maneja
  **con el mando** (LB/RB o Ctrl+Tab cambian de pestaña, START = Jugar; ayudas
  con botones de Xbox, PlayStation o Switch).
- **Personajes nuevos** en casillas propias del select, sin sustituir a nadie.
  **Pack de personajes** (descarga opcional): Janemba, Androide 19, Zarbon, Dodoria,
  Guldo, Jeice y Burter; Zarbon y Dodoria con golpes y gritos de Budokai 1.
- **Importador de personajes** desde Budokai 1, Budokai 2, Infinite World y
  modelos de la comunidad, con vista previa del nombre en directo. **Kit de
  modding** (descarga opcional) con las herramientas y un instalador de
  requisitos en un clic.
- Gritos y voces propios, cápsulas propias, Edit Skills para los nuevos y trajes
  extra para personajes existentes.
- **Arreglos**: cierre al usar el menú rápido, launcher colgado en «trabajando…»,
  pantalla negra con FSR2/FSR3, personajes portados que flotaban.
- **Avisos**: los personajes nuevos son experimentales, requieren la versión USA
  y los datos en carpeta (no ISO); los textos nuevos están en español e inglés.
- Mejoras del SDK adaptadas de **DBZ Burst Limit Recompiled** (iExplosiveRage).

---

## [1.3.0] - 2026-09-30

- One-click **Repair installation**, controller button labels (Xbox /
  PlayStation / Switch) in the Controls tab, **DRED** on by default for clearer
  GPU crash reports, `gamecontrollerdb.txt` shipped next to the executable,
  texture-extractor fix (issue #13), launcher polish and caching.

## Older versions

| Version | Date | Summary |
|---|---|---|
| v1.2.9 | 2026-09-26 | Self-explaining diagnostics: sustained-FPS, slow-disk and mixed-install warnings; VRAM guard |
| v1.2.8.2 | 2026-09-24 | Texture enhancement no longer tanks FPS (dynamic-texture throttle) |
| v1.2.8.1 | 2026-09-23 | Texture dump covers uncompressed HUD/UI; RGBA8 packs (issue #11) |
| v1.2.8 | 2026-09-21 | Texture dump fix (chosen folder reaches the plugin) |
| v1.2.7 | 2026-09-21 | PCSX2-style texture packs (D3D12 and Vulkan) |
| v1.2.6 | 2026-09-20 | Polished HD texture enhancement + self-repairing settings file |
| v1.2.5 | 2026-09-19 | Focus QoL (mute/dim), disk diagnostics and read-ahead |
| v1.2.4 / EX | 2026-09-19 | Real volume, update check, FXAA/dither, GPU knobs, portable user data |
| v1.2.3 | 2026-09-18 | Performance counter and quieter AFS log |
| v1.2.2 EX | 2026-09-17 | Launcher finds the executable by itself + ISO-mode fixes |
| v1.2.0 / 1.2.1 | 2026-09-14 | Renewed mod center, polished HD↔HD model swap, FSR/CAS sharpness |
| v1.1.x | 2026-09 | USA+EU dual core, single universal exe (SSSE3), disc (ISO) mode, i18n |
| ≤ v1.0.x | 2026-08 | Initial stabilization |
