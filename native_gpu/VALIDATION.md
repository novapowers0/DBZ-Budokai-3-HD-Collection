# Local validation of this contribution

Validated on Windows x64 on 2026-10-15, against upstream base
`e58e74e856f87adaccdee23a6fbaae2b95392eda`. These are local observations,
not guarantees of complete gameplay correctness or cross-vendor compatibility.

## Clean source builds

- Independent renderer EXE, DLL and CPU replay bridge: CMake/Ninja, Clang with
  MSVC ABI, matching Windows SDK/CRT; successful.
- Numeric material registry: public JSON rebuilt to 92 records, byte-for-byte
  equal to the locally exercised lookup table. It contains no program bytes.
- Isolated EU host, including the latest upstream application overlay:
  successful with the patched ReXGlue 0.10 headers/import library and canonical
  runtime DLLs. Private regenerated EU source was used locally and is ignored.
- All public Python files compile. The repository publication check reports
  zero errors. New `sub_...` warnings identify documented EU hook addresses.
- The optional Windows build workflow template requires no game/SDK; no hosted
  run is claimed. The standalone MSVC `cl` configuration was
  not locally validated (the portable installation lacks its language DLL).

## Asset-free tests

Run `test_synthetic.py` as documented in `INTEGRATION.md`:

| Test | Observation |
|---|---|
| Image samplers | 18 independent U/V/filter cases; maximum RGB error 1/255 |
| Frame operations | Ordered color/depth snapshots, clear/restore and history; exact expected pixels and negative validation checks |
| DOF depth filter | Own D24 target vs independent UNORM24/bilinear CPU oracle: max error 1/255; deliberately incorrect point path differs by 17/255 |
| Depth/stencil resampling | All 256 stencil values and varying native depth, exact independent point-sampled expectations; no original depth upload |
| Blend/shadow sampler | Seven blend/edge cases passed |
| Scene snapshots | Native source geometry and GPU-only history; exact immutable-input comparison |
| CPU RGBA4444 | 32 tiled/endian/packed-tail cases, all 65,536 values, truncated input/output rejection |

CTest passed both CPU contracts: physical-memory notifications (including
one-shot/rearm and the physical-provider bypass limitation), and the two EU
function-pointer dispatch repairs. Neither test boots the game or loads the
reference GPU backend.

## Live EU smoke with the exact proposed source build

The separately built host and renderer reached **8,400 native frames**, including
6,929 frames containing indexed geometry. This sampled run reported 1,097,616
accepted indexed draws, 726,831 accepted UP draws and 125,310 accepted resolves,
zero rejections in that interval, and an empty native internal-error field.
Those numbers measure the sampled interval only. Longer earlier runs exposed
the missing materials/allocation reuse described in `INTEGRATION.md`.

Process module enumeration verified `d3d12.dll` was loaded and no `rexgpu` or
Xenia graphics module was loaded. ReX CPU/audio/input services remain active.
The live service check passed 64 buffer serial/cursor label pairs and six
counter writes against independently decoded CPU-emitter diagnostics. Over a
six-second observation, display pacing advanced 300 frames/300 vblanks at
about 59.62 Hz. This is display-service cadence, not a gameplay benchmark.
The smoke process was stopped after collecting evidence.

Raw CPU/game captures, original shaders, RenderDoc captures, GPU caches and
generated sources remain private local inputs and are not included. Existing
reference-oracle scripts are provided so a maintainer can reproduce additional
pixel checks using their own data. The public synthetic suite does not depend
on any private input.
