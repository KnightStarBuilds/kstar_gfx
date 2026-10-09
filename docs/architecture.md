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

## Measurements

Statistics distinguish submitted draws, instances, transferred bytes, owned native resources, and frame completion. `steady_state_allocations` is a cumulative count of explicit library resource constructions and upload-staging growth during active frames, including diagnostic readbacks and deliberate stress resources. It is not a global process allocation counter. Demo metadata separately reports allocations in ordinary uncaptured frames. Driver allocations are outside the library's control.

CPU frame timings include recording and any frame-slot wait. They are not GPU timestamp measurements. The draw-dispatch benchmark isolates the C entry point against the same native GL draw call, with rasterization discarded. Both pixel checks and timing evidence are needed: a blank or simplified frame cannot qualify as a performance result.
