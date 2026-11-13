# Validation

Desktop validation runs on macOS ARM64 with Apple Clang and an Apple A18 Pro GPU. GNM is cross-compiled with LLVM/OpenOrbis and tested separately through shadPS4. A successful compile is not evidence of rendered backend parity.

## Lighting and effects revision

The current renderer adds the matched `lighting_baked` / `lighting_dynamic`
rooms, typed lights, directional and local shadow caching, LTC rectangles,
Static lightmaps, runtime probes, projected wet decals, VDB hero effects and
impact-driven water. Textured billboard particles remain for fine effects and
small puffs. The original transparency pixel thresholds are unchanged.

The focused renderer checks cover six Point faces and cube seams, Stationary
emission edits, moving blockers, near/blended/far directional cascades, cached
directional submissions, Rect backside/orientation/width/specular response,
Static specular preservation, direct Sky visibility, four-volume extinction and
order independence, camera-inside rays, opaque termination, smoke attenuation
of emissive sprites, wet masks and projected decal receiver filtering. Volume,
glass and wetness cases exercise 1× and 4× MSAA. The water inspection retains its
existing zero-overflow requirement: coherent jets use one front interface with
their diameter supplying absorption thickness.

CPU checks cover the sixteen-probe budget per rendered frame even when two
simulation ticks occur, twelve-update freshness, Static-only baking, Stationary
bounce edits, moving occluders, imported triangle holes, stale geometry/material/
light/mesh payloads, water impacts/foam/crowns, deterministic replay and ordinary
frame allocations. Asset checks verify the 160-probe companion, 113 xatlas cube
receivers, all twelve directional fields and the three real VDB atlases. A killed
converter releases its process lock; the recovery regression acquires it again
without deleting a lock directory.

The full native suites pass 47/47 checks in both Debug and Release. Offline Debug/Release rebuilds,
isolated clean-cache preservation, paths with spaces and installed/source C
consumers pass the build-contract check. Source and cooked asset caches remain
ignored and intact.

Release measurements on Apple A18 Pro / macOS 27.2 use 360 bounded, hidden frames,
the first 60 for warmup, and 299 measured frames excluding capture. Cinematic is
1920×1080 with 4× MSAA; Performance is 1280×720 with 4× MSAA. The total includes
input, two 60 Hz simulation ticks, probe updates, render submission and frame-slot
waits. Native event pumping, presentation and capture readback are excluded.
All six Cinematic total-frame p95 values meet the 33.33 ms target in this run.

| Scene | Recorded baseline render p95 (ms) | Updated render p95 (ms) | Cinematic total p95, two ticks (ms) | Performance total p95, two ticks (ms) |
|---|---:|---:|---:|---:|
| `facility` | 26.89 | 29.05 | 29.68 | 13.09 |
| `glass_lab` | 22.25 | 30.40 | 30.03 | 12.66 |
| `waterworks` | 19.87 | 28.81 | 27.99 | 12.59 |
| `boiler_room` | 18.74 | 29.15 | 29.55 | 11.39 |
| `lighting_baked` | — | 32.38 | 32.92 | 14.79 |
| `lighting_dynamic` | — | 32.47 | 32.60 | 14.65 |

The first two columns measure rendering with one simulation tick per frame.
The baseline is a retained 120-frame run (59 measured samples), with earlier
system/compiler versions; it is a reference rather than a controlled same-build
comparison. The updated measurements include the added lighting, water and
effect work. All twelve current preset/scene runs report zero ordinary-frame
library allocations. This does not count driver-internal allocations. GPU
timestamps are unsupported on this native driver, so no GPU pass duration or
CPU/GPU bottleneck split is inferred from these CPU wall times.
Evidence is under ignored `.tmp/fidelity/`: `before-*`, `after-*`, `after30-*`,
`benchmark-total.log` and the corresponding completed-GPU PPM captures/JSON.

Matched 240-tick walking and close-orbit runs at 960×540 were captured for both
lighting scenes at 1× and 4× MSAA, using identical input and seed. The final
images retain matching geometry, fixture positions and glass composition while
showing the different baked contribution. Water/fire close views at both sample
counts show the stream impact/crown, pool and wet-floor reflections, volumetric
flame/smoke and fine sparks. These are final-frame captures, not recorded video
or evidence of every point along the route. The scratch harness, images and
metadata are `walk.cpp`, `lighting_*-walk-*`, `lighting_*-orbit-*`,
`waterworks-close-*`, `boiler_room-close-*` and `capture-views.log` in that directory.

GNM host layout/guardband/MSAA checks pass in Debug and Release. The current
demo compiles and links into PS4 ELF files in both configurations, and all 28 native shader
packages pass validation through the corrected PSBC compiler. SELF/FPKG creation
is blocked by the SDK's x86-64 converter on this ARM64 host (`OS error -86`). The
installed and maintained-fork emulator binaries also cannot run here. Current
emulator pixels, the 30-feature probe, the low-resolution shaft comparison and
physical PS4 behavior are unverified. Linux and Windows execution are untested.

Directional lightmaps currently cover static cube receivers; curved and imported
receivers use six-lobe per-object bake fallbacks. Probe visibility uses directional
distance moments, so thin-wall leakage remains an approximation requiring visual
review. A matched scene switch retains the camera, animation and controls; newly
initialized dynamic bounce converges over the next twelve rendered frames.
The dedicated shaft pass replaces the earlier implementation discussed
below, but its GNM artifact cannot be declared resolved without a new comparison.
No Nocturne gameplay, recovered physics or cloth source was changed; campaign
behavior was not revalidated by these standalone graphics tests.

## Earlier baseline validation

The following table and emulator narratives describe the earlier showcase
baseline. They are retained as regression evidence, not as validation of the
new lighting/effects shaders.

| Check | macOS ARM64 OpenGL | PS4 GNM through current shadPS4 fork | Physical PS4 | Linux / Windows |
|---|---|---|---|---|
| C-compatible public API | Debug and Release passed | Debug and Release cross-build passed | Not run | Not run |
| Clear, copy, completed GPU readback | Debug and Release passed | Full shader-feature probes passed with matching Debug/Release emulator and guest configurations | Not run | Not run |
| Blending, depth, scissor, MSAA resolve | Debug and Release passed | Full shader-feature probes passed with matching Debug/Release emulator and guest configurations | v01.00 ground-depth and AA defects reported; v01.01 retest pending | Not run |
| Frame slots and deferred resource retirement | Debug and Release passed | 600-flip copied-command presentation and 124-frame resize/resource-retirement stress passed in Debug and Release | Not run | Not run |
| Demo capture parity | Debug and Release passed, including walkthroughs, both HDRIs and model detail views | Walkthroughs: 10/10 Debug and 9/10 Release at 320×180; Release glass 4× passes at 1080p. Gallery baselines also passed. See the light-shaft limitation below. | v01.01 retest pending | Not run |
| Configuration suites | 38 checks passed in Debug and Release | 29/29 full checks with matching Debug/Release emulator and guest configurations; separate baseline discard checks also passed | Not run | Not run |
| Offline rebuild / cache preservation | Passed, including isolated clean and C consumers | Core Debug/Release passed with Python discovery disabled; missing submodules rejected before clean | Not run | Not run |

`kgfx_conformance` checks GPU pixels for blend/depth behavior, top-left upload/scissor/copy/readback coordinates, padded input rows, implicit uniform-buffer ranges, rejected attachment contracts, 4× MSAA resolve, repeated HDR resolve/load cycles with persistent depth, reuse of three upload slots, deferred resource destruction, and library draw-path C++ allocations. `tests/demo_capture_test.py` renders deterministic captures, checks triangle interpolation or scene visibility, and compares repeated output. Cooker tests check retained mesh topology, tangents, texture mip chains, alpha, floating-point HDR range, generated IBL assets and rough-reflection reconstruction against analytic directional lighting. Acquisition and packaging tests cover offline reuse, complete input preflight and failure rollback.

The additive GPU timer API preserves the C11 ABI and passes native core builds
and C compatibility checks in Debug and Release. `kgfx_gpu_timing` passes both
configurations using a deterministic GL procedure fixture: missing support,
pending-query polling without waits, cached results, 30-bit counter wrap,
frame ownership and deferred destruction are covered. Native OpenGL conformance
passes both configurations on Apple A18 Pro, whose driver reports timestamps
unsupported; the nested native timing branch is therefore unavailable on this
machine. GNM core cross-builds pass both configurations and explicitly return
`KGFX_UNSUPPORTED`. These timer changes have no new emulator, physical PS4,
Linux or Windows runtime validation.

The walkthrough checks exercise all four maps. CPU tests cover a complete walking
route through the facility, station reachability, glass collision, ramp height,
particle limits, seeded replay, reset, pause and 30 Hz input feeding the 60 Hz
simulation. A shared-session GPU test cycles all ten scenes, applies and cancels
menu edits, retains the old scene when assets are missing, and checks complete
resource retirement. Completed back-buffer pixels verify that the settings
overlay remains visible across all three upload slots and disappears when closed.
Hidden and unfocused bounded captures must still advance
their exact requested simulation ticks and contain particles.

Renderer probes use controlled geometry and completed GPU readback at both 1×
and 4× MSAA. They check fire behind one and two panes, opaque occlusion, soft
particle intersections, foreground smoke attenuation, tint and frost, planar
reflection orientation, two-layer masks, third-layer overflow and target resize
failure recovery. A grazing-angle case keeps two panes only three centimetres
apart and checks distinct attenuation of particles between and behind them.
Map captures verify interaction-driven pixel changes, repeated
seed/tick determinism, radial-depth grayscale, analytic layer colors and no
overflow in the authored inspection views. Allocation checks exclude frames that
perform capture readback; every tested walkthrough reports zero library
allocations in its other frames. These checks do not establish arbitrary transparency depth
or physical-console performance.

macOS graphics tests need access to the WindowServer, even with hidden windows. A sandbox failure to create an SDL video context is not a renderer pixel failure. Native tests use the current Apple GPU and OpenGL driver; emulator results are a separate record. Interactive Retina resizing/orbit behavior, minimized-window recovery, extended soak tests, and physical-console performance require their own checks.

Native window checks verify Retina presentation and the settings overlay.
Escape opens/closes settings; Quit is an explicit menu action. Pending edits
remain separate from active GPU resources until Apply.

Untimed gallery baseline GL and GNM window sessions pass in Debug and Release. Each applies
Performance (720p/4×/1024 shadows), custom 1× AA, and Quality (1080p/4×/2048
shadows), cancels an unapplied edit, and exits through menu Quit. All four
sessions report zero library allocations in ordinary frames. GNM completes
1,385/1,385 flips in Debug and 1,934/1,934 in Release. Separate host-window close
checks also exit normally in all four configurations; window closure is a
process/input check, independent of offscreen pixel validation. The native
Retina output and PS4 VideoOut dimensions remain fixed while internal targets
change. GPU captures omit the presentation overlay.

The final Release walkthrough also passes a focused native window session at
2560×1440 Retina output with 1280×720 internal rendering: settings open/close,
Page Down scene selection, and host-window closure while settings are open.
A separate Release GNM walkthrough session verifies the visible facility,
Options menu open/close, D-pad selection of the glass lab and return to the
facility, and menu Quit. It completes 168/168 submitted flips and exits normally.
Closing a separate GNM window while its menu is open also exits normally; that
runner correctly reports `closed`, without claiming a completed guest capture.
These GNM sessions use staged application resources, not an installed FPKG.

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

The pre-menu triangle demo verified visible rendering and focused Escape through
the emulator's ScePad mapping on 0.18.0: Options captured its final frame,
completed all submitted flips and exited normally. Closing the host window
also exits normally; the runner records it as incomplete if no final guest capture
exists. A timed visible run completed 901 flips. Other runs intermittently abort
with `Out of order flip IRQ`, including before any quit input; these successes do
not establish stable visible emulation. A PBR run with the emulator's default
command-buffer copying setting showed incorrect final material lighting, then
stalled. It does not qualify as a scene or quit pass.

The earlier gallery baseline used branch `kstar-gfx` on upstream commit
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

Both baseline emulator configurations built. The baseline Release executable SHA256 is
`8b3e27282ba33dc50fdea11503708c4464dcc76e774181acb83130951abe8724`;
Debug is `4174d928ccc8b9fb7d441886281fe87a5ea966c2f7ef8c2eb242164704bb8b67`.
Each emulator with its matching guest configuration passes 28/28 full
shader-feature checks. Separate isolated discard/depth checks and a
600-completed-flip presentation probe also passed with command-buffer copying enabled.
The full and discard probes use the default command-buffer copying setting.
These are two matching configuration pairs, not all four emulator/guest
combinations.

The six-scene baseline matrix passes 14/14 OpenGL comparisons at 320×180: all six scenes in
matching Debug and Release configurations, including both PBR HDRIs. Every
process exits normally. Stress renders 124 frames with two target resizes and
per-frame resource retirement. Industrial and night PBR mean RGB errors are
approximately 0.000710 and 0.000588; stress is 0.000304. The same whole-image and
tile thresholds apply to every run. The 3D scenes retain 4× MSAA and 2048²
shadows; the triangle intentionally uses one sample. These comparisons use the
gray-ground gallery; the tiled-floor matrix is recorded separately below.

The tiled-floor PBR matrix passes 16/16 comparisons across matching Debug and
Release builds: shaded, normals and material diagnostics under each HDRI, plus
each HDRI with 1× AA and 1024² shadows. Every native reference is rendered twice
to check determinism. Captures remain 320×180 and use the existing whole-image,
4×4 and 16×16 tile thresholds. All runs exit normally and report zero library
allocations in ordinary frames. Separate 1080p window captures verify the floor
texture, settings overlay and packaged presentation visually.

## Walkthrough emulator checks

The maintained fork includes `72f34e5e`, which adds `S_OR_SAVEEXEC_B64`
translation through the existing saved-EXEC helper. The walkthrough surface
shader previously stopped in the recompiler on that unsupported opcode. The
recorded Debug executable SHA256 is
`b87aa4ba59d23b3063eb604295312748675964858dac59c61b560adf152e715a`;
Release is `1b26a22535e2567be8a00a35003e8960f946898a488751ea10fff2e4126ebf8b`.
Both matching emulator/guest configurations pass 29/29 native shader probes.
The additional HDR resolve/load regression verifies four RGBA16F 4× phases,
partial sample coverage, retained depth, repeated premultiplied blending and
interleaved single-sample targets against known pixel values. The same compound
case also passes through native OpenGL. These checks do not establish physical
PS4 behavior.

The walkthrough matrix uses 320×180, the performance preset, 1024² shadows,
environment 0 and seed 8675309. Each configuration compares all four maps at
tick 132 with 1× MSAA and the idle sequence; glass and water also use 4× MSAA
with the interaction sequence. Glass/water linear-depth and layer diagnostics
use tick 4 and 1× MSAA. Debug passes 10/10 comparisons; Release passes 9/10.
All twenty captures match simulation ticks, interaction state and live particle
counts across backends, and report zero steady-frame allocations. Both layer
diagnostics match their OpenGL references byte-for-byte in both configurations.

Release glass at 4× after interaction retains localized dark strips. Its mean
normalized RGB error is 0.004648 and p95 is 0.023529, but its worst 16×16-grid
tile is 0.081693 against the unchanged 0.08 limit. The corresponding Debug tile
is 0.075728, so its aggregate pass does not imply that the artifact is absent.
The 1× interaction capture passes with a worst tile of 0.003845. At 1920×1080,
the same Release 4× interaction case passes the unchanged limits: mean error
0.001598, p95 0.003922 and worst fine-grid tile 0.025422. That capture uses the
same production shaders, tick, seed and settings, matches simulation state and
reports zero steady-frame allocations. The low-resolution difference remains
unresolved; its physical-console status is unknown.

Isolated shader experiments do not remove the strips when replacing optical
shading with snapshot copies, discarding all refractors and particles, bypassing
surface and volumetric shadow samples, removing bloom, or bounding HDR input
before ACES. Removing ambient IBL or direct specular also leaves the strips.
Albedo, normals, material/AO and directional-shadow diagnostics are close at the
affected tile. Pixel-center peel reconstruction leaves both final images
byte-identical, and the compound resolve/load probe passes. Disabling only local
light-shaft radiance in a temporary experiment lowers the worst fine-grid tile
to 0.019780, locating the difference in that contribution. A pixel-center shaft
ray with sampled opaque-depth termination does not fix it (worst tile 0.082626)
and was reverted. Production retains the original shafts and all other effects;
comparison thresholds are unchanged. Final captures, metadata and logs are retained under
`.local-memory/showcase-validation/backend-evidence/matrix-320x180` and
`probe-{Debug,Release}` in the same evidence directory. The 1080p pair is under
`backend-evidence/presentation-1920x1080/Release/glass_lab-shaded-4x-interact`
relative to `.local-memory/showcase-validation`. Temporary shader experiments
remain under `.tmp/showcase-*-isolation`; the rejected shaft reconstruction is
under `.tmp/showcase-shafts-final`.

The unchanged installed 0.18.0 executable, SHA256
`eece906602cbc4cc0d991c3a03d1a472030d9c070f7081a813f1bce8559f4d27`,
fails the separate Release glass attempt before a capture: its presentation
thread reports a read from address `0x20`, followed by exit status -5. This
installed-binary failure is distinct from the maintained fork's image
comparison result. Its evidence is
`.local-memory/showcase-validation/backend-evidence/installed-Release-glass`.

The current runs exercise the resident-buffer fallback on macOS. Physical PS4
launch/rendering of v01.00 is reported, with ground-depth and AA defects.
Hardware regression tests and performance measurements, Linux, Windows,
sparse-capable GPUs, DMA-only stress and extended soak coverage remain pending.
Earlier emulator results do not establish those checks.

Scene comparisons check normalized RGB error over whole images and 4×4 and
16×16 tile grids. The fine-grid regression test rejects localized shadow defects
that whole-image averages can hide. Visual inspection also checks material
detail and rough reflections; backend agreement cannot detect a shared defect.

Standalone integration is exercised by `python3 tests/build_contract_test.py`. It creates a temporary checkout in a path containing spaces, rejects invalid clean arguments before mutation, builds library-only Debug and Release offline, checks incremental reuse and cache preservation, installs the Release library, and compiles/runs separate C consumers through both `find_package` and `add_subdirectory`. It never cleans the working build tree.

A complete copy of the Release executable and packaged resources also launched
successfully from a directory containing spaces with `/private/tmp` as the
working directory. The waterworks capture advances four simulation ticks,
contains particles, passes scene visibility checks and reports zero ordinary
library allocations. This checks executable-relative resources without symlinks
to the source checkout or a resource-path override.

Host GNM layout tests exercise the same OpenGNM submodule source content as the cross build. They check 30 combinations of formats, dimensions and sample counts, including unique sample addresses, padded depth surfaces, HTILE allocation, cube faces/mips and invalid coordinate boundaries. Debug and Release pass. Configure `tests/gnm/host` into `build/macos-arm64/gnm-layout` with Ninja Multi-Config and `KSTAR_GFX_OFFLINE=ON`, then build and run CTest for each configuration. On macOS select the C compiler returned by `xcrun --find clang`.

The host suite also checks viewport-dependent guardbands against the signed
16.8 raster-coordinate range and decodes the emitted MSAA register packets
through repeated 1×/4× transitions. All three host checks pass in Debug and
Release. The native GPU probe covers the near-clipped 18 m ground at 1280×720:
eight camera angles, both draw orders and both sample counts pass all 32 cases.
HDR resolve checks observe exact quarter, half and three-quarter pixel coverage.
shadPS4 does not enforce the hardware guardband limits or the MSAA enable bit;
its pixel results complement the register/math checks but cannot establish
the hardware fix. A physical PS4 retest is required.

Native shader checks validate package checksums, vertex/fetch semantics, distinct fragment interpolation slots, descriptor layouts, precision variants and native cache identities. Corrupted packages and stale compiler provenance are rejected. Independent offline compiler rebuilds produce byte-identical shared artifacts with host Debug and Release tools; the internal clear/copy/resolve packages also reproduce from their checked-in source and provenance.

## Hardware package checks

The standalone packager produces Debug and Release FPKGs for `KGFX00001`,
version `01.01`, targeting firmware 9.00 with GoldHEN. The current default is
the `facility` walkthrough. Both configurations pass 28 LibOrbisPkg integrity
checks and verification of 282 payload files: 281 files match their staged
SHA256 hashes, and all 12 original SFO fields remain unchanged alongside the
two expected publishing fields. Extraction produces 283 files. The shell icon
is 512×512 RGB24. The Release package is 794,689,536 bytes, SHA256
`01232946364ee4e1df771766bc1284c6f681a61a6c233e55d3778b09fd1407ef`.
Package manifests, checksums and validation logs accompany each artifact under
`build/ps4-x86_64/standalone/pkg/<config>`. These revamp packages have not yet
been launched in an emulator or on physical PS4.

The previous gallery baseline's extracted Release v01.01 package boots in the
tested shadPS4 fork without guest arguments or a timer. Its visible captures show
both models, material spheres and the tiled floor under both HDRIs, plus normal, albedo and material
diagnostics. Options opens settings and menu Quit exits with code zero after
567 submitted and completed flips. This verifies packaged resource loading and
emulator presentation for that gallery baseline; physical PS4 depth/AA behavior
and the current walkthrough packages still require retesting.

Fourteen isolated input checks pass, including rejection of corrupted native
shaders, corrupted cooked assets and missing HDRIs before staging, preservation
of an existing package on failure, and detection of zero-exit tool error/usage
output. Separate SFO checks reject changed application metadata and incorrect
published package size. Four additional cases reject each missing Rock Tile
Floor map and all three missing together, including otherwise consistent
stamp/index/payload combinations. Linux and Windows packaging remain untested.

## Desktop measurements

The Release walkthrough measurements use Cinematic at 1920×1080, 4× MSAA,
2048² shadows, seed 8675309 and 660 fixed ticks on Apple A18 Pro. Each excludes
60 warmup frames and the final capture, leaving 599 timing samples and 659
ordinary allocation-check frames. No other validation renderer was active.
Facility uses the tour sequence; the focused maps use their glass/water/fire
inspection view. Every completed capture contains simulated particles and passes
scene visibility checks.

| Scene / environment | Mean CPU frame | P95 CPU frame | Ordinary library allocations |
|---|---:|---:|---:|
| Facility tour / industrial | 26.712 ms | 29.890 ms | 0 |
| Glass lab / industrial | 23.859 ms | 25.291 ms | 0 |
| Waterworks / industrial | 21.245 ms | 22.707 ms | 0 |
| Boiler room / industrial | 20.436 ms | 21.861 ms | 0 |
| Facility tour / night | 31.221 ms | 33.720 ms | 0 |

These CPU recording/submission measurements include frame-slot waits. They are
not GPU timestamps or console frame-rate measurements; the night tour's P95
slightly exceeds the 33.33 ms target. Physical PS4's 1080p/30 target remains
unmeasured.

The gray-ground Release gallery baseline at 1920×1080, 4× MSAA and 2048² shadows used 660 deterministic frames. The first 60 frames and the captured final frame were excluded from timing, leaving 599 samples per environment. These are CPU frame recording/submission times including frame-slot waits, not GPU timestamps, and do not measure the new tiled-floor presets.

| Environment | Mean CPU frame | P95 CPU frame | Library allocations in 659 ordinary frames |
|---|---:|---:|---:|
| Industrial Pipe and Valve | 4.009 ms | 4.949 ms | 0 |
| Qwantani Night | 4.020 ms | 4.938 ms | 0 |

The Release dispatch microbenchmark measured a 6.077 ns median per native GL draw and 7.319 ns through `kgfx_draw`, with the same GPU state and rasterization discarded. Backend selection has no runtime dispatch. The observed C entry-point/counter overhead is 1.242 ns per call; it is not described as zero CPU instructions. LTO was enabled, but this build retained a direct call to `kgfx_draw`. This microbenchmark does not measure GPU rendering throughput or PS4 performance.
