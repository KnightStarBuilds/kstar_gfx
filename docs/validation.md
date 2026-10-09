# Validation

Desktop validation runs on macOS ARM64 with Apple Clang and an Apple A18 Pro GPU. GNM is cross-compiled with LLVM/OpenOrbis and tested separately through shadPS4. A successful compile is not evidence of rendered backend parity.

| Check | macOS ARM64 OpenGL | PS4 GNM through shadPS4 fork `dd98d9e6` | Physical PS4 | Linux / Windows |
|---|---|---|---|---|
| C-compatible public API | Debug and Release passed | Debug and Release cross-build passed | Not run | Not run |
| Clear, copy, completed GPU readback | Debug and Release passed | Full shader-feature probes passed in both emulator and guest configurations | Not run | Not run |
| Blending, depth, scissor, MSAA resolve | Debug and Release passed | Full shader-feature probes passed in both emulator and guest configurations | Not run | Not run |
| Frame slots and deferred resource retirement | Debug and Release passed | Visible flip completion and 124-frame resize/resource-retirement stress passed | Not run | Not run |
| Six demo captures | Debug and Release passed, including both HDRIs and model detail views | 14/14 comparisons passed across both guest configurations, including both PBR HDRIs | Not run | Not run |
| Configuration suites | 22 checks passed in Debug and Release | 26/26 full checks and 2/2 isolated discard checks across Debug/Release emulator and guest combinations | Not run | Not run |
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
presentation probe does not imply these features pass.

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

The separate test fork uses branch `kstar-gfx-validation-0.18`, based on release
`e3ce810f`, with these source commits at `dd98d9e6`:

| Commit | Behavior corrected |
|---|---|
| `d1adf55c` | Retain copied command storage until queued consumers finish and synchronize the submission gate. |
| `54d42cdb` | Keep detile scratch ownership through the caller's final upload recording. |
| `57dd26ac` | Preserve valid-mask exports when EXEC is empty so killed fragments can discard before depth writes. |
| `61392dc1` | Initialize depth/stencil attachment clear flags; upstream backport. |
| `16d00fb0` | Enable sampler comparison only for comparison fetches; upstream backport. |
| `6cd76213` | Honor both integer and signed-float front-face encodings; upstream backport with regression tests. |
| `dd98d9e6` | Use the presentation scheduler for blank frames instead of concurrently submitting the guest's command buffer; upstream backport. |

The rebuilt unmodified 0.18 baseline reproduces the Metal buffer-to-image crash:
the copy reaches an invalid or nil source buffer. Without an open VideoOut port,
the presentation thread incorrectly uses the guest's draw scheduler, allowing
it to submit and replace a command buffer while guest texture operations are
still being recorded. The scheduler fix separates these owners; scratch
ownership also lasts through the caller's final upload recording. The installed
executable and Vulkan/KosmicKrisp libraries remain unchanged; the fixes are in
the separate emulator source checkout and have not been pushed.

The final fork builds in Debug and Release. Both CPU front-face regression tests
pass in each configuration. Its Release executable SHA256 is
`799e1e815a4a5c884c230389ea5d3f17d5eddfd572e7750a0b749da4d27a65a9`;
Debug is `2cdb0e2cadb7e0cd57d04fdeafa9053cf49d59d9adb866b3437dd30d90de487c`.

PBR's native shader requests a signed-float front-face value. The old emulator
supplies integer bits instead, corrupting normal orientation and final lighting.
The corrected fork matches the OpenGL industrial PBR capture at 640×360 with
4× MSAA and 2048² shadows: normalized mean RGB error is 0.000355, and the worst
16×16-grid tile mean is 0.010729. The normal diagnostic's mean error is 0.000322.
Visual inspection confirms model texture detail, shadows and rough highlights.
These comparisons use the default command-buffer copying setting.

Visible PBR checks on the final Release emulator also pass. A focused 150 ms
Escape tap with the Debug guest at 1280×720 records the Options quit, captures
the last frame, completes 100/100 flips and exits normally. The Release guest's
night environment renders correctly and exits normally through window close;
its interrupted final capture is recorded as incomplete. A five-second
1920×1080 Release session completes 183/183 flips and exits normally. The
completed 1280×720 and 1920×1080 captures match OpenGL with mean RGB errors
0.000185 and 0.000128 respectively, using 4× MSAA, 2048² shadows and zero library
allocations in ordinary frames.

The 320×180 scene matrix passes 14/14 comparisons on the Release emulator,
covering all six scenes in both guest configurations and both PBR HDRIs. Stress
runs render 124 frames, including two target resizes and per-frame resource
retirement. All processes exit normally. Industrial and night PBR mean RGB
errors are approximately 0.000710 and 0.000588; stress is 0.000304. The same
whole-image and tile thresholds apply to every run. The 3D scenes retain
4× MSAA and 2048² shadows; the triangle intentionally uses one sample.

With the scheduler fix, all Debug/Release emulator and Debug/Release guest
combinations pass 26/26 shader-feature checks and 2/2 separate fresh-device
discard/depth checks. All eight runs use fresh profiles, default command-buffer
copying and normal process exit. The earlier intermittent `DEPTH_COPY` zero-depth
failure is absent in these runs.

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
