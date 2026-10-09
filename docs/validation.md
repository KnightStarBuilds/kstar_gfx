# Validation

Desktop validation runs on macOS ARM64 with Apple Clang and an Apple A18 Pro GPU. GNM is cross-compiled with LLVM/OpenOrbis and tested separately through shadPS4. A successful compile is not evidence of rendered backend parity.

| Check | macOS ARM64 OpenGL | PS4 GNM through current shadPS4 fork | Physical PS4 | Linux / Windows |
|---|---|---|---|---|
| C-compatible public API | Debug and Release passed | Debug and Release cross-build passed | Not run | Not run |
| Clear, copy, completed GPU readback | Debug and Release passed | Full shader-feature probes passed with matching Debug/Release emulator and guest configurations | Not run | Not run |
| Blending, depth, scissor, MSAA resolve | Debug and Release passed | Full shader-feature probes passed with matching Debug/Release emulator and guest configurations | Not run | Not run |
| Frame slots and deferred resource retirement | Debug and Release passed | 600-flip copied-command presentation and 124-frame resize/resource-retirement stress passed in Debug and Release | Not run | Not run |
| Demo capture parity | Debug and Release passed, including both HDRIs and model detail views | 14/14 scene comparisons passed with matching Debug/Release emulator and guest configurations, including both PBR HDRIs | Not run | Not run |
| Configuration suites | 22 checks passed in Debug and Release | 27/27 full checks and 2/2 isolated discard checks with matching Debug/Release emulator and guest configurations | Not run | Not run |
| Offline rebuild / cache preservation | Passed, including isolated clean and C consumers | Core Debug/Release passed with Python discovery disabled; missing submodules rejected before clean | Not run | Not run |

`kgfx_conformance` checks GPU pixels for blend/depth behavior, top-left upload/scissor/copy/readback coordinates, padded input rows, implicit uniform-buffer ranges, rejected attachment contracts, 4× MSAA resolve, repeated reuse of three upload slots, deferred resource destruction, and library draw-path C++ allocations. `tests/demo_capture_test.py` renders deterministic captures, checks triangle interpolation or scene visibility, and compares repeated output. Cooker tests check retained mesh topology, tangents, texture mip chains, alpha, floating-point HDR range, generated IBL assets and rough-reflection reconstruction against analytic directional lighting. Acquisition and packaging tests cover offline reuse, complete input preflight and failure rollback.

macOS graphics tests need access to the WindowServer, even with hidden windows. A sandbox failure to create an SDL video context is not a renderer pixel failure. Native tests use the current Apple GPU and OpenGL driver; emulator results are a separate record. Interactive Retina resizing/orbit behavior, minimized-window recovery, extended soak tests, and physical-console performance require their own checks.

Native PBR window checks verify full Retina presentation at a fixed 1280×720
render/capture size, focused Escape in Debug and window close in Release. Both
exit normally with final captures, 4× MSAA, 2048² shadows and zero library
allocations in ordinary frames.

The PS4 demo consumes queued controller reports so a press and release between
slow frames still produces a button edge. Host tests cover short taps, held
buttons, empty polls, analog-state retention and reconnect/disconnect behavior.

Run the suite for each configuration after building it:

```sh
ctest --test-dir build/macos-arm64 -C Debug --output-on-failure
ctest --test-dir build/macos-arm64 -C Release --output-on-failure
build/macos-arm64/bin/Release/kgfx_dispatch_benchmark
```

GNM acceptance requires actual shader draws and GPU output, including textures/constants, cubemap LOD, depth sampling, instancing, alpha, HDR, and MSAA resolve. A GPU DMA clear/copy probe establishes memory and submission bring-up only. shadPS4 success does not establish real PS4 compatibility or performance.

The installed-release record uses the unchanged shadPS4 0.18.0 executable at
`/Users/najmm/Workspace/shadps4-macos-sdl-0.18.0/shadps4`. Results from the separate
KnightStarBuilds fork identify its commit and executable independently. Presentation is
checked separately from offscreen shader rendering: display-buffer readback
verifies output bytes, flip-status checks verify scanout submission/reuse, and
host-window checks verify visibility and quitting.

The full shader probe covers culling/front-face state, vertex and instance
fetch, first-vertex offsets, FP16/FP32 targets, HDR blending, depth sampling/copy,
cubemap mips, transfers, resolve, discard and attachment contracts. A passing
presentation probe does not imply these features pass. The overlap regression
binds a 64 KiB uniform range followed by a 256 KiB vertex span from one buffer,
then verifies changed data after rebinding and the untouched background clear.
It exercises draw-binding migration; it does not cover GPU-written or atomic
buffer aliases.

On the installed 0.18.0 release, the full probe passes 24/26 checks in Debug and
25/26 in Release. `DISCARD_KILL` fails in both configurations; `CLEAR` also fails
in the recorded Debug run. Both processes exit normally. The separate sustained
presentation probe passes display-buffer pixels and all 600 completed flips in
both configurations, without serializing each frame through CPU readback.

Visible triangle rendering and focused Escape through the emulator's ScePad
mapping are verified on 0.18.0: the guest receives Options, captures its final
frame, completes all submitted flips and exits normally. Closing the host window
also exits normally; the runner records it as incomplete if no final guest capture
exists. A timed visible run completed 901 flips. Other runs intermittently abort
with `Out of order flip IRQ`, including before any quit input; these successes do
not establish stable visible emulation. A PBR run with the emulator's default
command-buffer copying setting showed incorrect final material lighting, then
stalled. It does not qualify as a scene or quit pass.

The current test fork uses branch `kstar-gfx` on upstream commit
`0c9ca525b8add53e62b3ccd9c9c2f6ef2ebabbfa` (0.19.1), with maintained source
changes for copied-command lifetimes, submission locking, valid-mask exports
and resident-buffer support on devices without sparse buffers. The upstream
base already includes the depth/stencil clear, comparison-sampler, front-face
encoding and blank-frame scheduler fixes. Shader-cache versioning rejects
binaries generated before the valid-mask change. The installed executable and
Vulkan/KosmicKrisp libraries remain unchanged; no patch files or driver rewrites
are used.

The installed KosmicKrisp driver reports `sparseBinding=0` and
`sparseResidencyBuffer=0`. The newer upstream buffer cache attempts to obtain a
device address from an unbound sparse arena on this device and aborts before
the first probe completes. The fork selects a resident-buffer path using 64 KiB
pages, bounded growth padding and GPU copies when overlapping ranges grow.
Migration updates resident page-table entries, retains retired buffers through
submission completion and retries resource binding if migration changes earlier
bindings. Fast-clear metadata is consumed only after the binding attempt is
accepted; pending image barriers survive retries.

Both emulator configurations build. The Release executable SHA256 is
`8b3e27282ba33dc50fdea11503708c4464dcc76e774181acb83130951abe8724`;
Debug is `4174d928ccc8b9fb7d441886281fe87a5ea966c2f7ef8c2eb242164704bb8b67`.
Each emulator with its matching guest configuration passes 27/27 full
shader-feature checks, 2/2 isolated discard/depth checks and a separate
600-completed-flip presentation probe with command-buffer copying enabled.
The full and discard probes use the default command-buffer copying setting.
These are two matching configuration pairs, not all four emulator/guest
combinations.

The 320×180 scene matrix passes 14/14 OpenGL comparisons: all six scenes in
matching Debug and Release configurations, including both PBR HDRIs. Every
process exits normally. Stress renders 124 frames with two target resizes and
per-frame resource retirement. Industrial and night PBR mean RGB errors are
approximately 0.000710 and 0.000588; stress is 0.000304. The same whole-image and
tile thresholds apply to every run. The 3D scenes retain 4× MSAA and 2048²
shadows; the triangle intentionally uses one sample.

Untimed 1280×720 PBR sessions exit normally through focused Escape in matching
Debug and Release configurations, recording Options quit and 48/48 and 85/85
completed flips respectively. Both record zero library allocations in ordinary
frames. Host-window close also exits normally in both configurations; the runner
records the interrupted final guest metadata as incomplete. Release window
captures from both HDRI environments were visually inspected, and Debug window
captures were retained. Window closure is a quit check, independent of offscreen
pixel validation.

The current runs exercise the resident-buffer fallback on macOS. Physical PS4,
Linux, Windows, sparse-capable GPUs, DMA-only stress and extended soak coverage
remain untested. Historical results from the earlier 0.18-based fork do not
establish those checks for the current upstream base.

Scene comparisons check normalized RGB error over whole images and 4×4 and
16×16 tile grids. The fine-grid regression test rejects localized shadow defects
that whole-image averages can hide. Visual inspection also checks material
detail and rough reflections; backend agreement cannot detect a shared defect.

Standalone integration is exercised by `python3 tests/build_contract_test.py`. It creates a temporary checkout in a path containing spaces, rejects invalid clean arguments before mutation, builds library-only Debug and Release offline, checks incremental reuse and cache preservation, installs the Release library, and compiles/runs separate C consumers through both `find_package` and `add_subdirectory`. It never cleans the working build tree.

A complete copy of the Release executable and packaged resources also launched successfully from a directory containing spaces with `/private/tmp` as the working directory. This checks executable-relative resources without symlinks to the source checkout.

Host GNM layout tests exercise the same OpenGNM submodule source content as the cross build. They check 30 combinations of formats, dimensions and sample counts, including unique sample addresses, padded depth surfaces, HTILE allocation, cube faces/mips and invalid coordinate boundaries. Debug and Release pass. Configure `tests/gnm/host` into `build/macos-arm64/gnm-layout` with Ninja Multi-Config and `KSTAR_GFX_OFFLINE=ON`, then build and run CTest for each configuration. On macOS select the C compiler returned by `xcrun --find clang`.

Native shader checks validate package checksums, vertex/fetch semantics, distinct fragment interpolation slots, descriptor layouts, precision variants and native cache identities. Corrupted packages and stale compiler provenance are rejected. Independent offline compiler rebuilds produce byte-identical shared artifacts with host Debug and Release tools; the internal clear/copy/resolve packages also reproduce from their checked-in source and provenance.

## Desktop measurements

Release gallery runs at 1920×1080, 4× MSAA and 2048² shadows used 660 deterministic frames. The first 60 frames and the captured final frame were excluded from timing, leaving 599 samples per environment. These are CPU frame recording/submission times including frame-slot waits, not GPU timestamps.

| Environment | Mean CPU frame | P95 CPU frame | Library allocations in 659 ordinary frames |
|---|---:|---:|---:|
| Industrial Pipe and Valve | 4.009 ms | 4.949 ms | 0 |
| Qwantani Night | 4.020 ms | 4.938 ms | 0 |

The Release dispatch microbenchmark measured a 6.077 ns median per native GL draw and 7.319 ns through `kgfx_draw`, with the same GPU state and rasterization discarded. Backend selection has no runtime dispatch. The observed C entry-point/counter overhead is 1.242 ns per call; it is not described as zero CPU instructions. LTO was enabled, but this build retained a direct call to `kgfx_draw`. This microbenchmark does not measure GPU rendering throughput or PS4 performance.
