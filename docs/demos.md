# Demos

`kstar_gfx_demo` uses the same graphics API for all drawing, transfers and GPU
captures. SDL creates the desktop window/context and handles input. Scene code
has no OpenGL calls. Shaders and cooked assets load relative to the executable,
under `resources/`, or from an explicit `--resources` directory.

Desktop and PS4 hosts call the same scene implementation for all six demos.
The selected backend changes device setup and compiled shader conventions;
geometry, materials, draw order, pass structure and quality settings stay in
the shared renderer. The GNM host does not substitute simpler demo scenes.

## Scenes

| `--demo` | Behavior |
|---|---|
| `triangle` | Indexed-free vertex drawing, interpolated colors and presentation |
| `cubes` | Indexed textured geometry, complete texture mip chain, varying roughness, depth and culling |
| `pbr` | Poly Haven rifle and hat, procedural metal/dielectric grid, normal maps, GGX direct lighting, prefiltered IBL and two EXR environments |
| `instances` | 1,024 dynamically streamed instances plus ordered translucent sheets |
| `offscreen` | Directional shadow pass, depth copying/sampling, HDR highlights, 4× MSAA resolve and ACES-fitted tone mapping |
| `stress` | Repeated creation, use and pre-submission destruction of textures and vertex buffers; alternating target resize every 60 frames |

All 3D scenes use a 2048² shadow map and an RGBA16F 4× MSAA render target.
The resolved HDR image is tone-mapped into an RGBA8 target, with explicit linear
to sRGB conversion, before presentation. Missing required graphics capabilities
cause initialization to fail. Curated assets are required only for `pbr`; the
other scenes use procedural geometry and small constant environment fixtures.
The fixtures are not substitutes for the gallery's offline-cooked IBL.

The gallery preserves source geometry and cooked material settings. Opaque and
masked materials draw before blended materials; blended primitives sort back to
front by object center. This ordinary object-level transparency technique does
not solve intersecting transparent surfaces or refractive glass.

## Controls

| Input | Action |
|---|---|
| Left mouse drag | Orbit the camera, normalized to logical window dimensions |
| Mouse wheel | Zoom |
| `E` | Toggle Industrial Pipe & Valve / Qwantani Night |
| `[` / `]` | Reduce / increase exposure by one quarter stop |
| `D` | Cycle shaded, normals, albedo, material channels, shadow visibility |
| `R` | Reset camera, exposure, diagnostic and environment |
| Escape | Exit |

Interactive windows render at drawable resolution, including Retina scaling.
Bounded and headless runs honor the requested render/capture dimensions exactly.
Visible bounded runs scale the completed image to the full window drawable.

The PS4 interactive host maps left stick to orbit, L2/R2 to zoom, Triangle to
environment, Square to diagnostics, Cross to reset, L1/R1 to exposure and
Options to exit. It uses the initial user's controller and a deadzone. Visible
runs initialize controller services, including bounded sessions. Headless runs
stay independent of controller input. The isolated emulator profile also maps
Escape to Options while preserving existing keyboard and controller bindings.
With a fresh emulator profile, WASD drives orbit, E/O zooms, C changes the
environment, V changes diagnostics, N resets, and Q/U changes exposure.
Enter and Escape both quit. These keys travel through the emulator's ScePad
mapping and exercise the same guest controller path as a gamepad.

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

Scene animation uses a fixed 1/60-second step and no nondeterministic randomness.
Capture frame numbers are one-based. `--output` defaults the capture point to
the last bounded frame. A headless run requires a finite `--frames` value.
For a visible session, `--seconds 30` runs for 30 seconds after scene loading
and captures the final frame on timeout or a normal quit. `--seconds` and
`--frames` are mutually exclusive; time limits do not change the fixed scene step.
`--diagnostic shaded|normals|albedo|material|shadows` selects the same view as
the `D` key, allowing matched backend captures of individual shading inputs.
Headless desktop rendering still creates a native graphics context; it requires
a functioning desktop graphics session.

PPM captures come from the completed GPU LDR render target. The renderer waits
for the capture completion token before mapping its readback. Rows are written
top to bottom. JSON beside the image records backend, renderer, dimensions,
frame, exposure, environment, quality settings, timing samples and API counters.

`--benchmark` disables vsync and excludes the first 60 frames from timing
statistics. Timings cover CPU frame recording/submission and frame-slot waits;
they are not GPU timestamp measurements. Counters returned by the graphics API
are cumulative. `steady_frame_allocations` counts library allocations during
ordinary rendering, excluding capture and the deliberately allocating stress
scene. Capture allocations remain visible in the cumulative resource counter.
Frames that capture, their completion waits and file writing are excluded from
timing samples. Asset loading precedes the render loop. Benchmark mode excludes
the first 60 frames for warmup; ordinary timings can include first-use driver
or emulator shader compilation. Native
desktop, emulator and physical PS4 performance are separate evidence.

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
Escape or the controller's Options button requests a guest quit. Untimed runs
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
