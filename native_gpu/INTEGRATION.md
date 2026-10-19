# Experimental native Direct3D 12 backend

This contribution provides an opt-in native D3D12 renderer for the EU/PAL guest.
It is **not a complete replacement** for the release backend. The Linux/Vulkan
backend and SDK binaries are unchanged.

## Launcher integration

On Windows builds that include the EU codegen (dual-region or EU-only),
`DBZ3_NATIVE_GPU` (CMake option, default ON) adds a third choice to the
launcher's graphics backend combo: **Native Direct3D 12 (experimental, PAL)**
(`dbz3_gpu_backend = "native_d3d12"`). The launcher itself keeps drawing with
ReX's D3D12 presenter. Pressing PLAY with this backend relaunches `dbz3.exe`
with `DBZ3_NATIVE_GRAPHICS=1` and the renderer paths in its environment. The new
process skips the launcher, injects the native `IGraphicsSystem` instead of
`rexgpu-xenos`, and boots the game. With a US/NA xex it falls back to normal
D3D12.

- `src/native_gpu.{h,cpp}`: availability, child detection, relaunch.
- `src/native_gpu_hooks.cpp`: the EU driver hooks (`dbz3eu_sub_*` in the dual
  core). Outside native mode each hook calls the original directly.
- The build stages `dbz3-native-renderer.dll`, `materials.hlsl` and
  `materials.dbzregistry` in `native_gpu/` next to the exe.
- ImGui overlays (F1/F4 menus, custom panels) are not drawn in native mode.

The isolated laboratory below remains available for research and replay.

The renderer submits its own D3D12 draw calls and custom HLSL. In native mode the
EU host injects its own `IGraphicsSystem`, creates its own swapchain in the SDK
window, handles high-level buffer/display completion, and supplies vblank
interrupts and 60 Hz pacing. ReX still provides CPU execution, audio, input and
OS services. Original CPU driver functions can construct legacy command storage;
the native path retires it without interpreting PM4 or translating source shader
programs. Program hashes identify studied material families only.

## Review map

| Area | Files | Responsibility |
|---|---|---|
| GPU implementation | `native_renderer.cpp`, `native_gpu.h`, `materials.hlsl` | D3D12 resources, PSOs, custom shaders, stream ABI, swapchain |
| CPU draw bridge | `lab/graphics_hooks.cpp`, `lab/native_stream.cpp`, `lab/source_*.h` | Copy CPU geometry/assets/palettes before the driver retires storage; validate material/state/layout |
| Guest services | `lab/native_services.cpp` | Injected graphics interface, buffer labels, display counters, vblank/IRQ and pacing |
| Host/bootstrap | `lab/main.cpp`, `lab/eu_dispatch.cpp`, `lab/prepare_codegen.py` | Isolated EU host, instrumentation and two SDK 0.10 function-pointer repairs |
| Recognition | `materials.registry.json`, `build_registry.py` | 23 families / 92 endian-variant numeric identities; no original program bytes |
| Optional diagnostics | `lab/native_input_audit.h`, `lab/native_memory_watch.h`, `lab/trace.cpp` | State edges and CPU write notifications, private local captures |
| Regressions | `test_synthetic.py`, `lab/replay_api.cpp`, `lab/test_*.cpp` | Asset-free GPU/CPU contracts, optional local replay |

Read `README.md` for the scene ABI and historical component tests, and
`lab/README.md` for EU CPU entry points and offsets. Those offsets are specific
to the locally validated EU image. Do not apply them to the US image.

## Supported paths and limits

Implemented paths include dynamic world/character geometry and palettes, rigid
pieces, outlines, masks/shadows, sprites, YUV video, FXAA, Gaussian blur, depth of
field, color/depth copies and depth/stencil restoration. Indexed lists/strips/fans
accept 16/32-bit indices; UP fans/line strips and RGBA4444 assets are supported.
Image copies support independent U/V wrap/clamp/mirror and point/linear filters.
Native snapshots persist across completed frames. Native resources are versioned
when a resolve changes shape or color/depth type.

Known blockers before enabling this in a release:

1. Particle/distortion materials and an additional postprocess variant are
   unimplemented. Unknown materials are counted and skipped.
2. Reuse of an old rendered physical address for a new CPU asset is detected but
   still rejected as a rendered alias. The optional CPU write audit demonstrates
   reuse; it deliberately does not invalidate aliases or supply a rendering fix.
   A notification alone does not prove that every texel has been replaced.
3. Complete per-draw sampling/mips, remaining target/clear variants, MSAA, CPU
   readback and general fence semantics are unfinished. Frames currently wait for
   native GPU completion; multiple frames in flight remain future work.
4. Full-game final pixels, every battle/mode, US hooks and production launcher
   integration need independent validation. Accepted-call counts are not a
   completion percentage or a fidelity measure.

## Build the renderer without game data or ReX

Windows x64, CMake 3.25+, a Windows C++ toolchain/SDK, and Python 3 are needed.
The renderer uses C++17; the isolated replay bridge uses C++23. The CMake build
also generates `materials.dbzregistry` from the public numeric JSON:

```powershell
cmake -S native_gpu -B build/native -DPython3_EXECUTABLE=<python.exe>
cmake --build build/native --config Release
python -m pip install -r native_gpu/requirements.txt
python native_gpu/test_synthetic.py --directory build/native/Release `
  --bridge build/native/Release/indexed-replay.dll --scratch build/native-tests
```

For a single-config generator such as Ninja, use `build/native` as the binary
directory in the test command. These tests use synthetic inputs and the local
D3D12 device. They verify sampling, ordering, GPU history, complete stencil
resampling and selected shader contracts; they do not prove full-game fidelity.

The portable renderer-only alternative is Zig 0.15.2:

```powershell
native_gpu/build.ps1 -Zig <zig.exe> -Python <python.exe> -OutputDirectory <native-output>
```

## Build and run the isolated EU host

Use Clang with the MSVC ABI, C++23, CMake/Ninja and the project's patched ReXGlue
SDK 0.10. Apply the existing `patches/rexglue-sdk/` overlay. SDK headers/import
libraries and runtime DLLs must match the patched runtime, not an unpatched SDK
release. Generated code must use the same SDK version as the runtime.

1. Regenerate `generated_eu/` from your own EU executable with
   `dbz3_manifest_eu.toml` (set its local input path as needed). The accompanying
   config change places `indirect_calls` at TOML root scope. Run the repository's
   normal EU codegen fixups, then the opt-in lab instrumentation:

   ```powershell
   python native_gpu/lab/prepare_codegen.py --generated generated_eu
   ```

2. Build the separate laboratory:

   ```powershell
   cmake -S native_gpu/lab -B build/native-lab -G Ninja `
     -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=<clang++.exe> `
     -DCMAKE_PREFIX_PATH=<patched-sdk> -DDBZ3_NATIVE_CONTRACT_TESTS=ON
   cmake --build build/native-lab
   ```

   `DBZ3_GENERATED_EU_DIR` optionally selects another local generated directory.
   Stage the canonical matching `rexruntime.dll` and its dependencies beside the
   host/test executables. No `rexgpu*.dll` is required in native mode. This PR
   contains no SDK DLLs, SDK source checkout or import-library binaries.

3. Run the CPU contracts, then launch with your extracted EU data directory
   containing `yae3_xenon.xex` and `eu/`:

   ```powershell
   ctest --test-dir build/native-lab --output-on-failure
   native_gpu/Run-Native-Lab.ps1 -LabDirectory build/native-lab `
     -NativeDirectory build/native/Release -GameDataRoot <extracted-DBZ3>
   ```

The host uses `lab-config.toml`, `lab-user/` and `lab-cache/` next to its own EXE.
It mounts player data read-only. `native-stream-status.json` reports accepted
and rejected draws/resolves and internal failures; `native-services-status.json`
reports presentation/service state separately. Verify the running process has
loaded D3D12 and has **not** loaded `rexgpu`/Xenia graphics modules. The optional
`test_native_services.py` additionally checks captured service contracts and
live cadence; see its CLI help.

`-AuditInputState` and `-AuditMemory` enable diagnostic audits. CPU memory
notifications can include release/protect events and do not cover all host
physical-provider writes. They are not a general CPU/GPU coherence solution.
Reference comparison/RenderDoc mode is available separately, disabled by
default; `DBZ3_REFERENCE_CAPTURE` requires the RenderDoc application header.

## Evidence and reproducibility

The public synthetic suite is self-contained. Tests named `*_oracle.py`, live
capture exporters, and captured-input replays require private captures made from
your own game. No XEX/AFS, generated guest source, textures, meshes, original
shader programs, shader caches, frame captures or RenderDoc captures accompany
this contribution. The checked numeric registry builds without those inputs.

`ci/windows-build.yml` is an optional public Windows build workflow template.
Maintainers can copy it to `.github/workflows/native-d3d12.yml` to activate it.
It builds the independent renderer/replay bridge without game data or the SDK;
GPU image tests remain local. This contribution does not change active workflows.

Historical local evidence includes individual postprocess comparisons against
reference pixels, an integrated graph replay with original GPU framebuffer RAM
unmapped, and real EU boots with no reference GPU module loaded. Those checks
have distinct scopes. Private inputs are not bundled, so maintainers should
reproduce them locally before changing the release default.

Recommended next integration steps: review the typed GPU ABI/renderer first,
run the public tests, audit the EU hooks and service completions, reproduce a
local game boot, then address lifetime/material gaps before wiring a production
backend selector. Preserve the existing backend for current release users.

The contribution follows the repository's MIT license; retained ReXGlue/Xenia
attributions are in `THIRD_PARTY_NOTICES.md`.
