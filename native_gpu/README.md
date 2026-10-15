# Budokai 3 native graphics work

Start with [INTEGRATION.md](INTEGRATION.md) for the current opt-in EU host,
asset-free tests, review order and known gaps. The default release backend is
unchanged. This directory also contains historical component experiments.

This is a working Direct3D 12 renderer, not a completed replacement for the
installed game's graphics system. The release game still uses rexgpu-xenos.
The standalone native viewer and renderer DLL never load that plugin, parse PM4 commands, or
translate Xbox shader microcode. They upload host vertex/index buffers and RGBA
textures, run custom HLSL, and submit native draw calls on the PC GPU.

Current experimental integration (2026-10-15) recognizes 23 source material
families. It covers dynamic indexed geometry, character skinning and rigid
pieces, outlines, mask/shadow passes, screen/world sprites, YUV video, image
copies, FXAA, 13-tap Gaussian blur and 9-tap depth of field. Its own GPU color
and D24S8 targets support ordered copies, clears, complete depth/stencil
restores and point resampling of every stencil bit without optional stencil
export hardware. Offscreen postprocess targets are 640x360, 320x180, 160x90 and 80x45.
Solid sprites preserve vertex alpha and fog independently of texture fallbacks.
The dynamic frontend accepts big-endian 16/32-bit indexed lists, strips and
fans, UP triangle fans and line strips, and CPU RGBA4444 assets. Rendered
allocation aliases retain separate native storage versions when their shape
or color/depth type changes. Completed GPU snapshots survive frame boundaries.
Image copies support wrap, clamp and mirror addressing with point/linear filters.

`test_streaming_video.py --post-graph-trace <private-cpu-trace-folder>` checks
the integrated effect chain against independently declared typed operations,
with original resolved GPU color/depth RAM unmapped. It seeds small-target
color from native video instead of the intervening 3D draws. This verifies
frontend ordering, resources and parameters; separate original-output pixel
oracles verify the individual postprocess shaders. Neither test establishes
complete scene fidelity or coverage of every game mode.

With `DBZ3_NATIVE_GRAPHICS=1`, the live EU laboratory installs its own graphics
system, window swapchain, high-level buffer completion, display interrupts and
60 Hz vblank pacing. Module enumeration verifies that the reference GPU plugin
is absent. ReX remains responsible for the CPU runtime, audio, input and OS
services. Original CPU calls may still construct legacy command storage; the
native backend never interprets or executes those GPU packets. CPU meshes,
palettes and assets are copied before original draw calls can retire storage.
Full fence/readback semantics, per-draw sampling/mips, further material/target
variants and all-game pixel fidelity remain unfinished. Unsupported calls are
counted explicitly. The default/reference comparison mode and older component
milestones below are separate historical evidence.

The isolated EU lab now hooks actual CPU draw calls. Loading sprites update an
offscreen native renderer while the game runs. An indexed world component
contains 71 draws / 6,210 triangles from the game's own vertex/index buffers,
textures and matrices. Its C++ bridge passes a 15-frame hardware replay without
loading rexruntime/rexgpu/Xenia; live integration is recorded separately in
`native-world-status.json`. These are partial components, not a complete frame
or a replacement for the release executable. The optional version-3 component
adds 24 skinned character draws to those 71 world draws. Its deformation is
checked independently against captured GPU vertex outputs, then exercised
through the same C++ bridge. See `lab/README.md` and the packaged evidence for
the distinction between replay and live-game tests.

## Implemented and exercised on hardware

- AFS/LZX/AWO asset conversion using the existing project's model parser.
- Host mesh topology, depth, textures, toon subtraction, rim light, and an
  inverted-hull silhouette pass.
- Direct3D 12 device, resource uploads, descriptors, native PSOs, swapchain,
  synchronization, and readback.
- A C ABI for world-space vertex updates and explicit row-major camera matrices.
- Interactive model viewer, offscreen rendering, and input validation.
- Goku (AFS entry 264), Vegeta (424), and Krillin (327) from the player's EU copy.

The automated test renders each character from four views, verifies material
pixels, updates geometry and a camera through the DLL, rejects malformed data,
and enumerates loaded modules to check for rexgpu/Xenia. Dynamic frames in this
test are synthetic updates to decoded assets, **not captured gameplay**.

## Build

This directory can build independently of ReXGlue. With an installed Windows
C++ toolchain and SDK:

```powershell
cmake -S native_gpu -B work/native-build
cmake --build work/native-build --config Release
```

The build used here is portable Zig 0.15.2, SHA256
`3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c`.

```powershell
native_gpu/build.ps1 -Zig <path-to-zig.exe> -OutputDirectory <output-folder>
```

For model conversion, keep this directory inside the upstream game repository
so it can import `mod center hd/model_render.py`, `awo_tools/b3_gateway.py`,
`skin_slots.py`, and `awg_vertex_buffer.py`. Install NumPy and Pillow into the
Python environment used for conversion and tests. Point `--decompressor` to the
Xbox compression tool from the existing local installation, with its DLLs.

```powershell
python native_gpu/export_scene.py --afs <data_cmn.afs> --entry 264 `
    --decompressor <xbdecompress.exe> --output <assets/goku.dbzgpu>
python native_gpu/test_native.py --directory <output-folder> --scratch <scratch-folder>
```

The scene is player-owned data. Do not publish it, the XEX, the AFS files, or
generated guest source with the code. Copy this `native_gpu/` folder into the
upstream source tree if consuming the source archive alone.

## Scene format, version 1

All integers are little-endian unsigned 32-bit and all floats are IEEE float32.
The header is 48 bytes: magic `DBZNGPU1` (8), vertex count, index count, draw
count, texture count (16), and min/max xyz bounds (24). Vertex stride is 32:
position xyz, normal xyz, UV xy. Indices are u32 triangle lists.

Each draw is 32 bytes: first index, index count, base texture, ramp texture,
blend flag (5 u32), diffuse RGB (3 floats). Texture index `0xffffffff` means
absent. Each texture has width, height, byte count, then RGBA8 pixels.
Translucent materials are deliberately rejected until their semantics are
implemented and checked. Base alpha currently selects the toon ramp row;
it must not be used indiscriminately as opacity.

## Scene format, version 2

`DBZNGPU2` retains the common header and adds width/height (u32), making it
56 bytes. Vertices are 52 bytes: position xyz, normal xyz, UV xy, RGBA, fog.
After the u32 triangle indices and legacy 32-byte draws, each draw has a
148-byte state: 16 row-major matrix floats, 20 material floats, and pipeline
flags (u32). Textures retain the version-1 RGBA8 encoding.

Material floats are control (UV offset xy, fog enabled, mode), four fog
parameters, tint RGBA, RGB mix color/factor, and fog color. Mode 1 is textured
CPU vertex color with optional mix/fog; mode 2 is the checked shadow material.
Depth, culling, RGB write masks and four blend equations have explicit PSOs.
`test_frame.py` checks alpha blending, depth rejection and live updates on GPU.
`test_live_indexed.py` runs captured CPU draws through the production C++ bridge
in a reserved guest arena, exercising the E0000000 physical alias and camera
updates. It compares that conversion against the independent Python exporter;
this is not a pixel-fidelity comparison against the original game's frame.

## Scene format, version 3 and character deformation

`DBZNGPU3` retains version 2's header, geometry, indices, draws and states.
Before the textures, every draw adds a u32 float4-row count followed by those
rows. A mode-4 skinned draw requires exactly 388 rows: four row-vector model
matrix rows, then 128 joints of three float4 rows. Other draws have zero rows.
Unused joint rows are sanitized to zero; all uploaded values must be finite.
`dbz_native_update_palette` replaces an allocation in place and rejects count
changes, nonfinite values and non-skinned draws. Bone IDs/weights in both
initial and updated geometry are checked before GPU submission.

For a joint with rows A/B/C, translation is A.xyz, the first quaternion's xyz
is (A.w,B.w,C.w), and the two animation quaternion xyz parts are B.xyz/C.xyz.
Every quaternion reconstructs positive w as `sqrt(abs(1-dot(xyz,xyz)))` in
float32. The animation quaternions are linearly blended along the shortest
arc with the vertex weight, then normalized for rotation. The shader applies
the animation rotation, the first rotation, translation, model and projection
in that order. Zero joint rows therefore represent identity, not a zero matrix.
Normals use the same rotations and model's linear part. The GPU reads the
palette from a structured buffer; the CPU does not predeform native vertices.

Mode 4 stores bone ID/weight in vertex color RG (BA unused). Its material
control is UV offset, fog enabled, 4; fog is the game's four fog parameters;
tint stores light xyz/rim strength; mix stores eye xyz/toon-ramp row; fog color
is the game's fog RGBA. Legacy draw diffuse RGB stores the checked shader's
three immutable material constants. Mode 5 takes world positions/normals and
the same material, providing a separate post-VS reference path for tests.

`export_character_frame.py` checks world position, normal, projection, UV,
rim factor and fog against independent GPU output. `test_character.py` compares
three native GPU-skinned poses to that oracle, exercises palette updates and
invalid inputs, and checks that no reference runtime is loaded. It shares the
native pixel shader between both paths, so this establishes deformation and
varyings, **not pixel-shader fidelity**. A few silhouette pixels can differ
because float32 rounding moves a triangle edge across a sample.

## Integration contract

`native_gpu.h` is a C ABI, **not** a drop-in implementation of `rex_gpu_create`.
Creating a renderer establishes the topology and texture set. Updates supply
world-space vertices with the same topology; changing a costume or topology
requires a new scene/renderer. The caller supplies the camera matrix and eye
position. Calls for a single renderer must be serialized. Frames wait for GPU
completion before upload buffers can be reused. CPU asset conversion caches
compare current source bytes before reusing native resources; changed content
retains a separate immutable native version for pending draws. Multiple GPU/CPU
frames in flight remain future work once gameplay correctness is established.

The current `native_stream.cpp` bridge dynamically submits CPU geometry,
textures, palettes, materials and ordered target operations without fixed
geometry manifests. Material identity checks recognize independently studied
source families; an unknown family is rejected. Earlier `native_indexed.cpp`
component tests retain their fixed manifests. Upstream notes about CPU skinning
must not be generalized to every draw family.

## Version 4 targets and raster state

`DBZNGPU4` adds a 28-byte frame clear after the common 56-byte header. Each
148-byte draw state adds 60 bytes: stencil (12), alpha test (8), viewport (24)
and scissor (16). Palettes/textures retain version 3's layout. `DBZIDX04` adds
the same raster after the 84-byte entry and 28-byte material-family extension.
Previous scene/manifest versions remain supported.

Modes 11/12 render skinned/rigid masks using native palettes and the original
pixel constant (C255.x in RGB, alpha 1). Target flag
bit 20 selects a separate RGBA8/D24S8 mask; bit 21 samples that mask in a later
main-target draw. Each mask group clears its own resources. The main depth and
stencil survive target switches. No reference framebuffer or resolved guest
texture supplies the live mask. Bit 22 accepts clip position/w for independent
oracle geometry, preserving perspective interpolation; the live bridge does
not use GPU post-transform geometry.

`dbz_native_update_raster` changes stencil reference, alpha comparison/reference,
viewport and scissor. Immutable stencil operations/masks stay in the PSO.
`dbz_native_set_clear` supplies explicit native frame clear values. CPU register
decoding lives in the bridge (`live_raster.py` / `lab/live_raster.h`), not the
renderer. The checked stage starts at depth 1 / stencil 1, independently seen
in the reference GPU before its first indexed world draw. Other scene clear
policies must be obtained from their own high-level intent.

The V4 exporter still omits separate postprocess targets and UP restore/copy
passes. Its mask/projection support does not establish final-frame fidelity.
`test_raster_targets.py` exercises resource transitions, depth preservation,
alpha discard, stencil, viewport/scissor and hidden producers on the hardware.
`test_live_raster.py` checks C++/Python ABI conversion against captured states
and signed-offset/front-back edge cases. `test_live_indexed.py` replays real
source geometry in a process with no reference GPU/runtime loaded.

Reference exporters may set `texture_versions: true` to save textures at each
selected event. This is required for image comparisons of reused GPU targets;
resource identity alone does not identify its contents across draws.

`map_guest_graphics.py` scans a locally regenerated EU codegen for conservative
packet-emitter and presentation candidates. The report is research evidence,
not a validated set of hook signatures. Known candidates from this copy include
the frame presentation function `sub_82237B30`. Live tracing confirmed
`sub_8223A670` as DrawPrimitiveUP (device, primitive, vertex count, CPU pointer,
stride) and `sub_8223A6B8` as DrawIndexed (device, primitive, base vertex, start
index, index count). The latter emits predicated `C0032201` packets. Earlier
allocator candidates such as `sub_82232528` were not draw signatures.

## Remaining work before a native game renderer exists

1. Compare complete scene render targets against the reference, including
   lighting, transparency, mipmaps, per-draw samplers and all material variants.
2. Complete target history/resource lifetimes across frames and other clear
   or target layouts. Keep unsupported paths explicitly counted.
3. Replace guest graphics submission and supply interrupts, vsync, fences,
   presentation and window handling without the reference command processor.
4. Exercise menus, story, transformations, every stage and costume, versus and
   long sessions before switching the player-facing executable to this renderer.

## Current fidelity limits

The viewer uses an offline pose and a demonstration light/camera. It is not a
pixel-exact capture of gameplay. Some armor/tail geometry in the Vegeta export
also appears in the upstream software-rendered reference; its posing must be
resolved through the actual gameplay geometry. The prototype covers only
checked geometry, sprite/video and effect families. Complete character/material
coverage, upstream 3D fidelity and reference-free synchronization are incomplete. The
reported per-model timings are not a prediction of full-game FPS.
