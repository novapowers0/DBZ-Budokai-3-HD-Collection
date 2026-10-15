# Isolated EU CPU/native GPU integration lab

This host boots the locally recompiled EU image. With
`DBZ3_NATIVE_GRAPHICS=1` and `DBZ3_NATIVE_STREAM=1`, it uses an injected native
graphics system and presents its own D3D12 targets in the SDK-created window.
The graphics plugin is not loaded. ReX CPU/audio/input/OS services remain.
Without that flag, the older reference/offscreen comparison mode is retained.

The functional release executable and its matching DLLs remain untouched.
The lab has its own config, user data and cache. Player data is mounted read-only.
It is not the full dual-region launcher or a release-ready GPU replacement.

## Build and run

Use a Windows C++23 toolchain, CMake/Ninja, and the patched ReXGlue SDK 0.10
headers/import library matching the installed release runtime. Apply the
repository's SDK overlay. A baseline SDK import library is not compatible with
all the canonical release's exports. Stage the release's matching DLL set next
to the lab executable; do not replace it with unpatched baseline SDK binaries.

Generate EU code with `dbz3_manifest_eu.toml`, then run:

```powershell
python native_gpu/lab/prepare_codegen.py --generated generated_eu
cmake -S native_gpu/lab -B <lab-build> -G Ninja -DCMAKE_PREFIX_PATH=<patched-sdk>
cmake --build <lab-build>
```

The root-level `indirect_calls` configuration is correctly placed, but SDK 0.10
still emits invalid integer switches for two thin EU function-pointer thunks.
`eu_dispatch.cpp` supplies their actual pointer-dispatch behavior. Its separate
test covers non-null/null event dispatch and signed state-table selection.
Both repairs are required to boot this independently generated host reliably.

Set these process environment variables to enable native components:

- `DBZ3_NATIVE_RENDERER`: absolute path to `dbz3-native-renderer.dll`.
- `DBZ3_NATIVE_SHADERS`: absolute path to `materials.hlsl`.
- `DBZ3_NATIVE_MATERIALS`: absolute path to `materials.dbzregistry`.
- `DBZ3_NATIVE_STREAM=1`: current dynamic CPU frontend.
- `DBZ3_NATIVE_GRAPHICS=1`: own swapchain, buffer/display services and pacing.

The following variables select historical fixed component tests, and should
not be combined with the current dynamic mode:

- `DBZ3_NATIVE_UI_SCENE`: `loading-ui.dbzgpu`, with adjacent `.bridge` manifest.
- `DBZ3_NATIVE_WORLD_SCENE`: `world-component.dbzgpu`, with adjacent `.indexed`.

Run `dbz3-gpu-lab.exe --game_data_root=<player's extracted DBZ3 directory>`.
The opening/title demo reaches the checked world component after about 130
seconds at 60fps. `native-loading-status.json` / `native-world-status.json`
record matched draws, native frame counts and failures; PPM captures are made
at native frames 1, 15, 30 and 60. Only draws matched in the current CPU frame
are made visible. Unseen/cull-removed draws submit no work and cannot leave
stale geometry in the next frame. The scene remains a partial component.

## Validated entry points and memory

`sub_8223A670`: r3 device, r4 primitive, r5 vertex count, r6 CPU pointer, r7
byte stride. `sub_8223A6B8`: r3 device, r4 primitive, r5 signed base vertex, r6
start index, r7 index count. In native graphics mode, indexed/UP inputs are
copied before the original CPU emitter runs, preserving ownership before old
storage is retired. Before/after CPU field audits check that the consumed state
agrees. Reference comparison mode observes after the original call. Resolves
remain after their original CPU call computes destination address and pitch.
`sub_82237B30` is the presentation boundary.

CPU device offsets: TF0 at 0x480, vertex fetch 95 at 0x778, VS floats at 1920,
PS floats at 6016, PS object at 13068, VS object at 13072, index object at
12812. These are EU-specific and guarded by manifests/layout checks.
The host physical arena is guest base + 4GiB. E0000000+ guest physical aliases
need an additional 0x1000 displacement. This applies to index pointers before
reading physical bytes; vertex/texture fetch descriptors already contain GPU
physical addresses. Ignoring the alias produced invalid index captures.

The world bridge admits only the independently checked FDF960/1BFD material
pair, 44-byte vertices, big-endian u16 indices, triangle lists/strips and the
recorded texture/pipeline identities. It checks original program hashes to
identify that pair; it does not interpret or compile microcode. No microcode
or player assets are included in the source archive.

## Capture/replay

Create `lab-ui.req` to capture three frames when a supported screen-space draw
appears; `lab-indexed.req` waits for indexed geometry; `lab-capture.req` captures
the next three frames. These opt-in diagnostics write CPU buffers, device
snapshots, texture bytes and program identities to `lab-trace/`. Captures can be
large; the hot GPU polling loop is intentionally excluded from entry tracing.
`export_live_ui.py` / `export_live_indexed.py` decode those into native scenes.

`replay_api.cpp` is a test-only wrapper for `native_indexed.cpp`. Build the two
as a standalone DLL without any ReXGlue libraries, then run
`test_live_indexed.py`. Its guest arena replays the real captures and changes
camera values for 15 frames. Module enumeration checks that rexruntime,
rexgpu and Xenia were not loaded. The independent Python conversion and C++
bridge agree to mean pixel error below 0.001 in the current component; this only
checks conversion consistency, not final-pixel fidelity against the game.

Captures are separated into `lab-trace/<process-id>/`, preventing old frame
numbers from colliding with a new run. Indexed captures include the additional
slot-94 character transform palette. To reload a newly exported scene/manifest,
create `native-world-reload.req`; the host replaces the native renderer between
frames. Each request is attempted once, including on failure.

`DBZIDX02` manifests identify texture content after removing pitch/tile padding
and converting endianness, with swizzle included in the identity. Uninitialized
padding can differ across launches while every decoded texel remains identical.
The hardware replay mutates 126 such bytes and verifies draw matching persists.

## Remaining graphics work

The optional `--characters` exporter admits the independently checked F3AC/5F27
skinned family. A `DBZIDX03` manifest retains each 84-byte DBZIDX02 entry, then
adds a packed 28-byte extension before its source-vertex IDs: second texture
identity (u64), byte count, width, height, format, kind (five u32). Kind 1 is
skinned and requires both textures to match; kind 0 has a zero extension.
The bridge reads vf94's 6144-byte palette, sanitizes unused joints, and updates
the native structured buffer on each matched draw. Status includes skinned
frames, changed poses, matched skinned draws and maximum per-frame coverage.
These counters do not imply that an entire character or game frame is covered.

The dynamic `native_stream.cpp` path now supersedes the fixed component
manifests for current work. It includes 23 material families, rigid/skinned
characters and outlines, stencil masks, dynamic topology/assets, sprites/video
and the checked native postprocess chain. `test_post_graph.py` declares the
expected small-target graph independently; original GPU framebuffer allocations
are unmapped during frontend replay. The old component manifests remain useful
for isolated regressions.

Own color/depth/stencil snapshots survive completed frames, and same-address
resolve shape/type changes select separate reusable native resources. Real
32-bit indices, triangle fans, line strips, RGBA4444 assets, world-space YUV
and differing HUD depth ranges are supported. Copy sampling includes mirror.

`native_services.cpp` supplies the injected graphics system, guarded high-level
buffer serial/cursor completion, counter endian writes, guest vblank interrupts
and one-frame-per-tick pacing. CPU emitter bytes are an offline diagnostic
oracle only. Production code does not execute GPU packets or compile source
shader programs. Per-process module inventories prove absence of the reference
GPU plugin, separately from pixel and service-contract tests.

Complete upstream 3D pixel fidelity, mips/per-draw samplers, remaining
clear/target/material variants, full fence/readback semantics and all-game
coverage still need work. Actual battles have exposed additional distortion
materials. A sampled run with no rejected calls does not prove full coverage.
