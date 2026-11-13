# Architecture and API contracts

`KSTAR_GFX_BACKEND` selects a single implementation at CMake configuration time. Both native implementations export the same C symbols. Backend types, headers, command encoders, and shader packages stay private. A consumer cannot switch backend on an existing device. The desktop GL procedure loader is the platform API loader, not an RHI dispatch table. GNM calls OpenGNM directly.

The public header compiles as C11. The implementation requires C++23 without compiler extensions. No Unreal source is included. The core has no dependency on Nocturne, SDL, an asset importer, a scene representation, or a runtime shader compiler. Demos and offline tools are separate targets. Dependencies are pinned by archive SHA256 and provenance; generated dependency build copies are separate from the source cache.

## Ownership and recording

One thread owns each device and its current native context. Resource pointers belong to that device. A successful create transfers ownership to the caller; destroy relinquishes ownership immediately. Calling any API with a pointer after destroy is invalid. Native destruction is delayed until commands that reference the resource complete. Device destruction requires the host's context to remain current.

The ordinary frame sequence is:

1. `kgfx_frame_begin` waits only when the reused frame slot is still in flight.
2. Upload dynamic data, including all per-draw constant-buffer ranges.
3. Begin a pass, bind a pipeline and resources, issue draws, end the pass.
4. Transition textures between passes; copy, resolve, or begin a readback as needed.
5. `kgfx_submit` closes the frame and returns a monotonically increasing completion token.
6. Poll `kgfx_is_complete` or wait for a token when the CPU needs its results.

GL executes native calls as they are recorded; GNM writes native command packets. There is no common command interpreter. Applications must not modify a constant-buffer range between draws that reference that range. Dynamic buffers use frame-local storage; populate each frame's active ranges after `frame_begin`. Explicit transfers and diagnostic readbacks may allocate or synchronize. Normal draw recording has no library heap allocation.

Passes cannot overlap. A null color target with a non-null depth target is a depth-only pass. Both targets null select the presentation framebuffer. Attachment dimensions and sample counts must agree. Passes attach mip zero; cubemap attachments are unsupported because the API has no face selector. Zero pass width or height selects that attachment dimension, or the device dimension for presentation. Sampling an attachment while writing it is forbidden; change passes first. Multisampled targets must resolve to matching single-sampled formats and dimensions before ordinary sampling or readback.

`kgfx_readback_begin` returns a separately owned asynchronous readback. Submit its frame, wait for completion, then map it. Mapped data stays owned by the readback until it is destroyed. Readback rows and CPU image uploads are top-to-bottom. Viewports, scissors, and copy rectangles use top-left pixel coordinates. All GL 2D textures use native lower-left sampling, including uploaded images; upload flips CPU rows through reusable staging memory. GNM uses native top-left sampling. Shared shaders adapt material UV and BRDF-LUT coordinates at compile time, and framebuffer passes account for native rendering coordinates. Cubemap face images retain the asset cooker’s native face convention on both backends.

Matrices are column-major. Demo geometry uses a right-handed camera and counterclockwise front faces. Shared projections produce OpenGL clip depth in [-w,+w]; the offline GNM vertex artifact converts depth to [0,w]. Depth clears to 1 with LESS/LEQUAL comparisons. Display encoding happens once, after linear-light HDR shading and tone mapping. Albedo textures use sRGB decoding; normals and ARM maps remain linear.

## Pipelines and bindings

Pipelines own linked GL programs/VAOs or native GNM shader packages, fetch shaders, and immutable render state. Create/prewarm them before entering steady rendering. GL program compilation happens at pipeline creation because the driver requires it. SPIR-V compilation, validation, reflection, GNM conversion, and GLSL cross-compilation run offline.

The fixed ABI exposes four uniform-buffer slots, sixteen sampled texture/sampler slots, two vertex streams, and an index buffer. Shader blocks are named `kgfx_u0` through `kgfx_u3`; samplers are `kgfx_t0` through `kgfx_t15`. Source SPIR-V uses uniform bindings 0–3 and sampled bindings 8–23. Generated metadata validates the host's uniform layouts. Binding remapping is done when building shader artifacts or creating a pipeline, never per draw.

Release enables supported compiler IPO/LTO. Callers should enable it too. Architecture-specific SIMD or assembly belongs behind narrow platform guards and requires measured evidence; none is introduced simply to make code appear faster. No x86 instruction flags are applied to ARM64 targets. Floating-point asset cooking disables contraction to keep deterministic preprocessing stable.

## Showcase renderer

The industrial walkthrough is a shared demo layer, separate from the C graphics
core. `ShowcaseWorld` owns authored geometry, static collision, camera controls,
interactions and bounded CPU particle pools. `ShowcaseRenderer` consumes one
immutable interpolated frame through the same native graphics API on GL and GNM.
No scene, asset, SDL, compute or runtime backend dependency enters the core.

The forward pass sequence is opaque radial depth and half-resolution bilateral
SSAO, directional and local shadows, reduced-resolution depth-aware light shafts,
planar pool reflection, opaque HDR, layered transparency, bloom and tone
mapping. The auxiliary R32F depth stores linear radial eye distance; separate
hardware depth attachments perform raster depth tests. This avoids a dependency
on MSAA depth resolve. Each refractive pass samples a completed immutable HDR
snapshot while drawing into the original color/depth attachments with matching
MSAA sample counts and preserved contents.

Two depth-peeled glass/water interfaces divide particles into three depth bands.
Particles retain depth order across fire and smoke, so foreground smoke can
attenuate flames. A third peel supports an overflow diagnostic; this renderer
does not implement arbitrary recursive transparent interfaces. Refraction uses
screen-space transmission with boundary rejection, IBL glass reflection, and one
clipped planar pool reflection. These are bounded raster approximations.

All pipelines, target sets, draw arrays and particle upload storage are prepared
before ordinary rendering. Settings allocate candidate targets before replacing
active ones. Scene switching stages a complete replacement and waits for the
previous frame before releasing the old scene; failure keeps the active scene.
The build-time scene catalog supplies identifiers, supported views/sequences and
required assets to the application, launchers, tests and package validation.

Walkthrough simulation advances at 60 Hz with up to eight catch-up ticks per
interactive frame. Pause/focus loss clears pending elapsed time. Bounded captures
advance one tick per frame by default; an explicit two-tick budget measures
30 FPS simulation cost. Named sequences reproduce interactions.
The walkthrough simulation source disables floating-point contraction so ARM
and x86 fractional emission boundaries agree; this policy does not alter the
graphics core or recovered Nocturne simulation. Legacy demo animation retains
its original frame stepping.

Overlay geometry is rebuilt when its text or menu changes, then uploaded into
the current dynamic-buffer frame slot on every visible frame. CPU geometry
caching does not imply persistence across a dynamic buffer's native backing
slots. Presentation tests inspect menu pixels across repeated slot reuse.

## Lighting and surface data

Light shape (Directional, Point, Spot, Rect, Sky) is independent of mobility.
Static sources supply baked diffuse direct and indirect light; runtime specular
remains evaluated from their authored sources. Stationary sources cache static
visibility but update color/intensity and one-bounce diffuse transport. Movable
sources also update position and orientation. Photometric units, temperature,
tint, independent Spot cones, source dimensions and shadow/volume controls are
shared scene data. Legacy intensity retains the existing showcase response.
Rect diffuse and specular use the pinned LTC fit; their five-face hemisphere
shadows approximate visibility from the emitter center. The offline bake samples
sixteen points across the emitting rectangle.

Stable light IDs retain whole local-light allocations in a 32-face atlas: six
faces per Point, one per Spot and five per Rect. Separate static and moving
depth layers allow Stationary emission edits to reuse visibility. Cinematic
tiles are 512 pixels; Performance tiles are 256. Two texel-stabilized directional
cascades blend over their overlap and cache static depth independently of moving
occluders. Sky direct illumination uses traced probe visibility and AO, while
its diffuse bounce uses the same transport path as other dynamic sources.

The matched lighting scenes use 160 probes, bounded by a 192-probe capacity.
Two persistent CPU workers update sixteen probes with 96 deterministic rays each
per rendered frame, independently of the 60 Hz simulation. Oldest-probe
reservations guarantee refresh within twelve updates;
remaining jobs prioritize changed lights and blockers. A static BVH and imported
triangle BVHs cache visibility; moving blockers are tested separately. Directional
distance moments and normal-biased tetrahedral interpolation reduce probe leakage
using four neighboring probes per pixel. This is
one diffuse runtime bounce, not recursive real-time path tracing.

Static cube receivers use xatlas UVs and twelve directional lightmap fields
(six direct, six indirect). Other static geometry uses six-lobe per-object bake
fallbacks. Baked probes carry indirect illumination for movable receivers.
Only Static lights enter the three-bounce CPU bake. `lighting_dynamic` consumes
no baked contribution and reclassifies the two equivalent Static fixtures as
Stationary. Versioned companion payloads validate geometry/material/light
identity and imported mesh bytes without changing `.kgmodel` records.

Opaque and refractive shaders have separate texture bindings, each within the
sixteen-slot ABI. Before lighting, opaque materials evaluate independent UV
wetness/roughness masks and up to eight projected box decals per draw from a
128-decal scene pool. Layer/ID sorting and receiver filters keep composition
stable. Immutable transform and material bindings are cached; camera, reflection
and shadow draws are culled. Depth/shadow passes batch compatible static objects
by mesh and winding into instanced draws and combine each model's primitives.
All instance data uploads precede drawing, avoiding native driver stalls from
updating an in-use buffer. Local and directional shadows use four filtered taps.
Light shafts render at quarter resolution in Cinematic and eighth resolution in
Performance, with depth-aware reconstruction; SSAO remains half resolution.

## Effects and water

Fine effects use textured instanced billboards: smoke/steam puffs, rain, droplets,
dust, sparks and embers retain sprites. Fire/smoke/spray flipbooks interpolate
adjacent frames, with lifetime envelopes, seeded variation, velocity alignment,
local lighting and visibility culling. Hero emitters use shared VDB-derived slice
atlases. Up to four volumes integrate density together on one ray lattice, with
self attenuation, lighting, temporal interpolation and smooth distant flipbook
blending. Cinematic uses half-resolution output and 48 steps per ray;
Performance uses quarter resolution and 24. Glass/water interfaces partition
that same lattice, and foreground smoke attenuates fine particles.

The pool simulation uses 64×64 cells and matching 65×65 vertices. Droplet and
stream impacts inject waves, persistent foam and bounded splash crowns. Shading,
depth and transparent passes use the same interpolated geometry. Coherent
ballistic stream meshes supply close jets, while particles supply spray and
breakup. Planar reflection, absorption, Fresnel, shoreline foam and rejected
invalid screen refraction complete the water response. This demo simulation is
independent of recovered Nocturne physics and cloth.

## Measurements

Statistics distinguish submitted draws, instances, transferred bytes, owned native resources, and frame completion. `steady_state_allocations` is a cumulative count of explicit library resource constructions and upload-staging growth during active frames, including diagnostic readbacks and deliberate stress resources. It is not a global process allocation counter. Demo metadata separately reports allocations in ordinary uncaptured frames. Driver allocations are outside the library's control.

CPU frame timings include recording and any frame-slot wait. They are not GPU timestamp measurements. The draw-dispatch benchmark isolates the C entry point against the same native GL draw call, with rasterization discarded. Both pixel checks and timing evidence are needed: a blank or simplified frame cannot qualify as a performance result.

The additive `kgfx_gpu_timer_*` API measures GPU intervals independently of CPU
submission. Create a bounded ring of timers before rendering, begin and end each
interval within one frame, then poll previous intervals with `read`. Different
timers may overlap, allowing a whole-frame timer to contain pass timers. A read
returns `KGFX_NOT_READY` without waiting or changing its output; a successful read
returns the frame serial and elapsed nanoseconds and caches that sample until
the next begin. Beginning an unread pending timer also returns `KGFX_NOT_READY`;
skip instrumentation when the ring is full. Keep intervals shorter than one
second for implementations with 30-bit counters. Resource retirement protects
pending timer queries, and ordinary begin/end/read operations allocate nothing.

OpenGL uses paired asynchronous `GL_TIMESTAMP` queries and checks availability
of both before fetching results, with native counter-wrap handling. Missing
query entry points or a zero-width timestamp counter make creation return
`KGFX_UNSUPPORTED`. GNM currently reports `KGFX_UNSUPPORTED` because its native
timestamp conversion and readback have not been validated. Unsupported GPU
timings must remain explicit in reports; CPU measurements are never substitutes.
Existing ABI structures and `KGFX_ABI_VERSION` remain unchanged.
