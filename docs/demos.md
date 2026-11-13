# Demos

`kstar_gfx_demo` uses the same graphics API for all drawing, transfers and GPU
captures. SDL creates the desktop window/context and handles input. Scene code
has no OpenGL calls. Shaders and cooked assets load relative to the executable,
under `resources/`, or from an explicit `--resources` directory.

Desktop and PS4 hosts call the same scene implementation for all twelve demos.
The selected backend changes device setup and compiled shader conventions;
geometry, materials, draw order, pass structure and quality settings stay in
the shared renderer. The GNM host does not substitute simpler demo scenes.

## Scenes

| `--demo` | Behavior |
|---|---|
| `facility` (default) | Connected loading yard, glass lab, pump hall and boiler room, with walking collision and interactive stations |
| `glass_lab` | Layered tinted glass, refraction, roughness controls, moving lights and particles on both sides of panes |
| `waterworks` | Flooded pump hall, planar water reflection, foam, spray, ripples and an interactive valve |
| `boiler_room` | Fire, smoke, sparks, embers and a sprinkler that suppresses flames and produces steam |
| `lighting_baked` | Industrial lighting room with Static diffuse lightmaps/probes, animated Stationary/Movable fixtures, moving blockers and wet-surface comparisons |
| `lighting_dynamic` | Matching room, camera, materials and exposure; equivalent Static fixtures become Stationary, with no baked scene lighting |
| `triangle` | Indexed-free vertex drawing, interpolated colors and presentation |
| `cubes` | Indexed textured geometry, complete texture mip chain, varying roughness, depth and culling |
| `pbr` | Poly Haven rifle and hat, tiled Rock Tile Floor, procedural metal/dielectric grid, normal maps, GGX direct lighting, prefiltered IBL and two EXR environments |
| `instances` | 1,024 dynamically streamed instances plus ordered translucent sheets |
| `offscreen` | Directional shadow pass, depth copying/sampling, HDR highlights, 4× MSAA resolve and ACES-fitted tone mapping |
| `stress` | Repeated creation, use and pre-submission destruction of textures and vertex buffers; alternating target resize every 60 frames |

`--list-scenes` prints scene identifiers and their supported views and sequences.
The walkthrough maps use industrial props, concrete and metal material maps,
textured billboard particles, VDB-derived hero volumes, typed local lights and
shadows, SSAO, bloom and fog. Billboard sprites remain the renderer for fine
effects and small puffs; volumetric fire/smoke is reserved for hero emitters.
Glass and water use two peeled transparent interfaces with three particle depth
bands; the overflow diagnostic exposes a third interface. This bounds the
transparency cost and does not support arbitrary recursive refraction.

The default 3D settings use a 2048² shadow map and an RGBA16F 4× MSAA render target.
The resolved HDR image is tone-mapped into an RGBA8 target, with explicit linear
to sRGB conversion, before presentation. Missing required graphics capabilities
cause initialization to fail. Curated assets are required for `pbr` and the six
walkthroughs; the other scenes use procedural geometry and small constant environment fixtures.
The fixtures are not substitutes for the gallery's offline-cooked IBL.

The gallery ground uses 2K Rock Tile Floor albedo, OpenGL-convention normal and
packed ambient-occlusion/roughness/metallic maps, with complete mip chains.
One tile spans two metres, repeating nine times across the 18-metre plane.
The normals, albedo and material diagnostic views expose these texture inputs.

The gallery preserves source geometry and cooked material settings. Opaque and
masked materials draw before blended materials; blended primitives sort back to
front by object center. This ordinary object-level transparency technique does
not solve intersecting transparent surfaces or refractive glass.

## Controls

Walkthrough controls:

| Input | Action |
|---|---|
| WASD / mouse | Walk / look; Shift increases walking speed |
| `F` | Use the nearby material panel, light switch, valve or sprinkler |
| `C` | Toggle inspection camera; drag to orbit and wheel to zoom |
| `V` | Cycle shading, transparency, lighting, wetness, probe and timing diagnostics |
| `R` | Reset the scene and camera |
| `P` | Freeze / resume fixture and effect animation while retaining camera control |
| `L` / `-` / `+` | Cycle Stationary light color / reduce / increase intensity |
| `M` | Switch between `lighting_baked` and `lighting_dynamic`, preserving the view, exposure and animation state |
| Page Up / Page Down | Previous / next scene |

Shared settings and legacy inspection controls:

| Input | Action |
|---|---|
| Left mouse drag | Orbit the camera, normalized to logical window dimensions |
| Mouse wheel | Zoom |
| `E` | Toggle Industrial Pipe & Valve / Qwantani Night |
| `[` / `]` | Reduce / increase exposure by one quarter stop |
| `D` | Cycle legacy shading diagnostics; walkthrough inspection also includes depth, layers and overflow |
| `R` | Legacy scenes: reset camera, exposure, diagnostic and environment; walkthroughs: reset scene and camera |
| Escape / F1 | Open or close graphics settings |
| Arrows / Enter / Backspace | Navigate and edit / select / close settings |
| Window close | Exit |

Walkthroughs start at the selected preset's fixed internal resolution. Legacy
interactive windows initially render at drawable resolution, including Retina scaling.
Bounded and headless runs honor the requested render/capture dimensions exactly.
Visible bounded runs scale the completed image to the full window drawable.
Applying a graphics preset or custom settings selects a fixed internal resolution;
the completed image continues to scale to the current window drawable.

The PS4 walkthrough maps left stick to walk, right stick to look, L3 to sprint,
Cross to interact, Circle to inspection mode, R3 to reset, Square to diagnostics and
D-pad left/right to scenes. Inspection mode uses left stick to orbit, L2/R2
to zoom and Cross to reset; Circle returns to walking. Menus consume controller
input before the scene.

For lighting controls, hold L3 and press Square to freeze animation, Triangle to
cycle Stationary light color, Circle to switch the matched lighting scene, or
L1/R1 to lower/raise Stationary intensity. These chords retain their meaning when
queued during a slow frame; ordinary D-pad scene cycling and menus are unchanged.

The legacy PS4 interactive host maps left stick to orbit, L2/R2 to zoom, Triangle to
environment, Square to diagnostics, Cross to reset, L1/R1 to exposure and
Options to open or close graphics settings. In the menu, D-pad navigates/edits,
Cross selects, and Circle returns to the demo. Select **Quit App** to finish GPU
work and exit. It uses the initial user's controller and a deadzone. Visible
runs initialize controller services, including bounded sessions. Headless runs
stay independent of controller input. The isolated emulator profile also maps
Escape to Options while preserving existing keyboard and controller bindings.
With a fresh emulator profile, WASD/IJKL walk/look, X sprints, N interacts,
B toggles inspection and M resets. In legacy scenes or inspection, WASD orbits,
E/O zooms and N resets. C changes the environment, V changes diagnostics and
Q/U changes exposure. Left/right arrows change scenes outside settings.
Enter and Escape toggle settings; arrows navigate, N selects and B returns.
These keys travel through the emulator's ScePad
mapping and exercise the same guest controller path as a gamepad.

## Graphics settings

| Mode | Internal resolution | Anti-aliasing | Shadow map | Effects |
|---|---|---|---|---|
| Cinematic | 1920×1080 | 4× MSAA | Two 2048² cascades, 512-pixel local tiles | Full particles; half-resolution volumes, 48 total ray steps |
| Quality | 1920×1080 | 4× MSAA | Two 2048² cascades, 512-pixel local tiles | Reduced particles; half-resolution volumes, 48 total ray steps |
| Performance | 1280×720 | 4× MSAA | Two 1024² cascades, 256-pixel local tiles | Reduced particles; quarter-resolution volumes, 24 total ray steps |
| Custom | 1280×720, 1600×900 or 1920×1080 | Off (1×) or 4× MSAA | 1024² or 2048² | Retains selected effects budget |

Walkthroughs default to Cinematic; legacy scenes default to Quality.
Both lighting rooms default to exposure 0.05. Their local lights fit 29 of the
32 persistent face slots; allocation always keeps all faces of a light together.
`--preset cinematic|quality|performance` chooses the startup preset. Explicit
dimensions, sample count and shadow size override its corresponding fields.
The menu also stages scene, environment, exposure and diagnostic changes. Select
**Apply Changes** to activate them; closing the menu discards unapplied edits.
Target replacement waits for GPU completion and retains the previous settings
if allocation fails. PS4 VideoOut stays at its initial output size; presets
change internal targets. All presets retain HDR lighting and full materials.
Their names describe settings, not measured frame-rate guarantees.

`--samples 1|4` and `--shadow-size 1024|2048` select startup settings for automated
comparisons. Captures exclude the settings overlay and record the active render
resolution, sample count and shadow size when the captured frame was rendered.

GL vertex clip depth is -1..+1; the native shader compiler specializes PS4
vertex outputs to 0..1. PS4 shader preprocessing also flips render-target
sampling vertically for tone mapping, presentation and projected shadows.
Uploaded 2D images use top-first asset UVs; the GL specialization flips material
and BRDF-LUT texture coordinates to match GL's bottom-first image storage.
GNM material UVs and the environment's NDC ray reconstruction retain their
original conventions. These conversions are resolved while compiling shader
artifacts.

## Deterministic captures

```sh
kstar_gfx_demo --demo facility --preset cinematic --seed 42 \
  --sequence tour --frames 240 --headless --output facility.ppm
kstar_gfx_demo --demo waterworks --view water --sequence interact \
  --frames 132 --headless --output water-valve.ppm
kstar_gfx_demo --demo glass_lab --view glass --diagnostic layers \
  --frames 4 --headless --output glass-layers.ppm
kstar_gfx_demo --demo pbr --frames 120 --capture-frame 120 \
  --width 1920 --height 1080 --headless --output gallery.ppm
kstar_gfx_demo --demo pbr --environment 1 --frames 120 \
  --headless --output gallery-night.ppm
kstar_gfx_demo --demo pbr --view rifle --frames 4 \
  --headless --output rifle-detail.ppm
kstar_gfx_demo --demo pbr --view hat --frames 4 \
  --headless --output hat-detail.ppm
kstar_gfx_demo --demo instances --frames 660 --benchmark \
  --headless --output instances.ppm
kstar_gfx_demo --demo offscreen --diagnostic albedo --frames 4 \
  --width 320 --height 180 --headless --output offscreen-albedo.ppm
kstar_gfx_demo --demo pbr --view hat --diagnostic normals --frames 4 \
  --headless --output hat-normals.ppm
```

Bounded walkthrough captures advance one 1/60-second simulation tick per frame.
`--ticks-per-frame 2` advances two ticks for a bounded walkthrough, exercising
the simulation cost of a 30 FPS frame while keeping probe updates once per render.
One-shot input actions still apply once. The default remains one tick so existing
capture frame numbers and deterministic replays retain their meaning.
`--seed` initializes particle randomness; `--sequence idle|interact|tour` drives
repeatable scene actions. Interactive walkthroughs use a fixed 60 Hz accumulator,
at most eight catch-up ticks per frame and interpolated rendering. Opening
settings pauses simulation. Interactive focus loss also pauses; bounded captures
continue fixed stepping without focus. Legacy animation keeps its fixed per-frame
step.
Capture frame numbers are one-based. `--output` defaults the capture point to
the last bounded frame. A headless run requires a finite `--frames` value.
For a visible session, `--seconds 30` runs for 30 seconds after scene loading
and captures the final frame on timeout or a normal quit. `--seconds` and
`--frames` are mutually exclusive.
`--diagnostic shaded|normals|albedo|material|shadows` selects the same view as
the `D` key, allowing matched backend captures of individual shading inputs.
Walkthroughs also support `depth`, `layers` and `overflow`. Depth displays linear
radial eye distance; layers distinguish zero, one and two interfaces. Overflow
marks a third interface in magenta. The additional `baked`, `bounce`, `wetness`,
`roughness`, `probes` and `timers` diagnostics isolate lighting and material inputs
or display pass measurements. Use `--list-scenes` for valid named views.
Headless desktop rendering still creates a native graphics context; it requires
a functioning desktop graphics session.

PPM captures come from the completed GPU LDR render target. The renderer waits
for the capture completion token before mapping its readback. Rows are written
top to bottom. JSON beside the image records backend, renderer, dimensions,
frame, scene tick, seed, sequence, interaction state, exposure, environment,
quality settings, timing samples and API counters.

`--benchmark` disables vsync and excludes the first 60 frames from timing
statistics. `cpu_frame_ms` includes recording, submission and frame-slot waits;
`cpu_total_frame_ms` also includes simulation and input handling, with their
cost reported as `cpu_simulation_ms`. Native event pumping is outside these
measurements. Use the total with `--ticks-per-frame 2` for the 30 FPS budget.
`cpu_frame_begin_ms` isolates slot waits and retirement/poll work, while
`cpu_submission_ms` covers command recording and submission/presentation after
the frame begins. `gpu_frame_ms` uses asynchronous native GPU timestamp queries
when available, with explicit `unsupported` status and null values otherwise.
Eight retained timers avoid synchronizing for measurements; a full ring skips
a sample. Statistics retain the newest 65,536 samples without frame allocations.
The Apple A18 Pro OpenGL driver reports timestamp support unavailable; its CPU
frame times therefore remain separate from GPU timing evidence. GNM also
reports timestamps unsupported. Counters returned by the graphics API
are cumulative. `steady_frame_allocations` counts library allocations during
ordinary rendering, excluding capture and the deliberately allocating stress
scene. Capture allocations remain visible in the cumulative resource counter.
Frames that capture, their completion waits and file writing are excluded from
timing samples. Asset loading precedes the render loop. Benchmark mode excludes
the first 60 frames for warmup; ordinary timings can include first-use driver
or emulator shader compilation. Native
desktop, emulator and physical PS4 performance are separate evidence.

`pass_timings` records the captured frame's CPU recording cost for seven showcase
groups and the newest available asynchronous GPU interval, including its frame
serial. Depth/AO includes scene preparation and uploads; reflection includes
initial target clearing and transparency depth preparation; transparency includes
the composited particle bands; effects measures light shafts. Post includes the
final resolve, bloom and tone map. Missing GPU samples are null. These last-frame
pass measurements do not represent warmed percentile statistics. The `timers`
diagnostic shows the same measurements in the presentation overlay.

The `lighting_baked` and `lighting_dynamic` command-line scenes default to
exposure 0.05 for their physical fixture intensities. An explicit `--exposure`
overrides this equally in either scene; matched switching preserves exposure.

## GNM emulator sessions

The visible launcher follows the same isolated-profile workflow as Nocturne:

```sh
python3 ps4/validate.py --config Debug \
  --emulator build/validation/shadps4/cmake/Release/shadps4 \
  --output .local-memory/gnm-visible-debug --scene pbr
```

It reads the SDK, ELF and resources from the configured GNM build. Explicit
`--sdk`, `--elf`, `--resources` and `--build-dir` overrides remain available.
The fork build includes the rendering fixes described in [GNM setup](gnm.md).
Select the installed executable explicitly for an unmodified-release comparison.
Choose a new output directory for each launch. `--emulator-profile PATH` copies
settings, users and input bindings into that directory. Source profiles remain
untouched; the copied profile redirects all user homes into its own `user/home`
and creates the default and configured account directories before launch.

Visible sessions use native GNM VideoOut presentation and controller input and
stay open until manual quit by default. There is no automatic process deadline
unless `--seconds`, `--frames` or `--timeout` is supplied.
Escape or the controller's Options button opens settings; **Quit App** requests
a guest quit. Untimed runs
capture the first frame and write its metadata on guest exit; timed runs capture
the final frame. Closing the emulator window stops the host; if that prevents the
capture, the runner records an incomplete observation rather than claiming a
rendering pass. The result separates completed VideoOut flips from host-window
and controller observations: an offscreen image alone cannot prove a visible
window. Logs, commands, hashes, configuration and captures remain in the output.

`ps4/validate.py` delegates to `scripts/run_gnm_demo.py`; both use the same
staging, profile preparation and process ownership. The low-level
script retains its headless default for deterministic renderer checks.

After building native PS4 resources, run the same scene through the validation
emulator documented in [GNM setup](gnm.md).
The runner records the executable hash so results identify the actual emulator
build as well as the graphics library and shader artifacts.

```sh
python3 scripts/run_gnm_demo.py \
  --emulator /path/to/shadps4 --sdk /path/to/PS4Toolchain \
  --elf build/ps4-x86_64/standalone/bin/Debug/kstar_gfx_demo \
  --resources build/ps4-x86_64/standalone/resources/Debug \
  --output .tmp/gnm-demo-triangle --scene triangle --frames 4
```

Each run requires a new output directory. The runner copies the packaged
resources, uses an isolated emulator profile with precise GPU readback mode 2,
and imposes a process deadline. It retains executable/emulator/resource hashes,
the exact command, logs, native shader dumps, completed GPU captures and their
metadata. A headless run without the expected GPU capture is a failure even
when the emulator exits cleanly. A visible window close can instead be recorded
as an incomplete observation, as described above.

Pass `--reference path/to/opengl.ppm` to compare equivalent frame, dimensions,
view, diagnostic and environment. The runner accepts the same `--diagnostic`
modes as the executable, so an `--scene offscreen --diagnostic albedo` run can
use the `offscreen-albedo.ppm` capture above. The comparison checks whole-image,
4×4-tile and 16×16-tile color error; the finer tiles catch localized alpha or
shadow defects that a large background could hide.
An analytic color/interpolation check additionally protects the triangle. A run
without a reference reports cross-backend comparison as untested. Emulator
frame timings never establish native PS4 performance.

The runner selects the SDK's converter for macOS, Linux or Windows. Windows
uses a dedicated process group and terminates its process tree on timeout;
POSIX hosts use an owned process session. Windows and Linux runner execution
require separate validation from the macOS captures.
