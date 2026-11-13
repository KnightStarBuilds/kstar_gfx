# Assets and offline tools

`assets/manifest.json` locks the requested Poly Haven models, industrial surface materials and environments.
Source payloads live in ignored `assets/source/`, retaining per-model folders and
relative names. All material maps use 2048×2048 PNG, and both environments use
2048×1024 floating-point EXR. Acquisition verifies SHA256 over the actual file;
Poly Haven's MD5 metadata is not used as the integrity check.

The ground uses [Rock Tile Floor](https://polyhaven.com/a/rock_tile_floor) by
Charlotte Baglioni (CC0), with one texture repeat spanning 2 m. Its 2K diffuse,
OpenGL normal and packed AO/roughness/metalness PNGs follow the same immutable
source and SHA256 manifest path as the models. The cooker emits
`textures/rock_tile_floor_{diff,nor_gl,arm}_2k.kgtex`, retaining all 12 mip levels.
Albedo filtering uses linear light before sRGB encoding; normal mips are
renormalized, and ARM channels stay linear. No displacement map is used.

The industrial slice adds [Modular Industrial Pipes](https://polyhaven.com/a/modular_industrial_pipes_01)
and [Barrel 01](https://polyhaven.com/a/Barrel_01) by Jorge Camacho,
[Power Box 01](https://polyhaven.com/a/power_box_01) by Rico Cilliers and Yann Kervran,
and [Painted Concrete](https://polyhaven.com/a/concrete_floor_painted) and
[Rusty Metal 02](https://polyhaven.com/a/rusty_metal_02) by Rob Tuytel. These CC0
payloads retain their original names, author records, upstream file revisions,
SHA256 hashes and acquisition sizes in the manifest. Industrial surfaces use the
same 2K diffuse/normal/ARM path as the existing floor material.

The manifest's `cook` records select model, material and environment outputs.
The pipe kit additionally emits eight reusable `modular_industrial_pipes_01_pipeNN.kgmodel`
components. Component extraction retains the node rotation and scale while
removing the display-layout translation; the complete kit is also retained.
This uses the existing version 2 model format without a runtime import step.

`tools/authored_effects.py` generates original MIT-licensed fire, smoke and spray
flipbooks, sparks, tileable water normals, foam and grime directly into KGTEX.
The three flipbooks use 512² atlases with sixteen 128² cells, top-left row-major
frame order and 16 fps playback metadata in `effects/atlas.json`. Transparent
padding and bounded mips prevent neighboring frame bleed. Other effect textures
are 256². The cooker hashes this generator alongside its other implementation
inputs; effect assets are generated offline and verified with the package.

`effects/surface_masks.kgtex` adds four independently authored material masks in
a 512² atlas: porous wet patches, leaks, footprints and grime. Its linear RGBA
channels contain wetness, roughness variation, relief and coverage. Material UV
scale/offset is independent of the base texture; projected decals use the same
mask cells with their own receiver filters and layer ordering.

`assets/volume-manifest.json` pins the original JangaFX Small Camp Fire, Simple
Fire and Industrial Chimney Smoke archives, with SHA256, byte counts, stable
download pages and [CC0 permission](https://jangafx.com/software/embergen/download/free-vdb-animations).
The archives total approximately 1.27 GiB and stay in ignored
`assets/source/volumes/`. Acquisition resumes partial transfers and publishes
only verified archives. The selected sixteen frames retain their archive paths.
The Simple Fire RAR uses host `bsdtar`; it does not require Blender.

`tools/build_volume_converter.py` builds OpenVDB 12.1.1, oneTBB 2022.2.0 and
Blosc 1.21.6 from hash-pinned archives in `deps/volume-toolchain-lock.json`.
Immutable sources live in `deps/cache/`; generated copies and host binaries live
under the selected `build/<platform>-<arch>/host-volumes/` tree. OpenVDB and its
dependencies are used only by the converter and are absent from runtime/PS4
linkage. Licenses are retained in `deps/volume-licenses/` and packaged notices.

`tools/cook_volumes.py` converts the actual VDB fields to
`effects/volumes/{small_campfire,simple_fire,chimney_smoke}/`. Each volume has a
1088×544 RGBA8 slice atlas containing sixteen 32³ frames, with one replicated
texel around each slice. Density, flame and temperature remain linear data;
the converter rotates source Z-up into the renderer's Y-up convention and uses
one bounding box for the whole sequence. Three endpoint frames crossfade to the
first frame, removing the loop discontinuity. A 128² distant flipbook preserves
mean column density/flame/temperature for the same lighting and extinction
calculation. It blends with spatial slices from 35–50 m when viewed along the
authored projection axis; oblique views retain spatial slices.

The three effects occupy 7,299,072 resident texture bytes. These atlases have one
mip level, with explicit interpolation across spatial slices and animation
frames. The sixteen-frame 32³ conversion is a bounded approximation of the
downloaded simulations. `effects/volumes/cook.stamp` separately records cook
identity and all output hashes. Native resource copying and PS4 package
validation merge this stamp with the model/material cook stamp. Cached normal
builds and clean builds can reuse these outputs offline without rebuilding
OpenVDB, while missing/corrupt archives fail explicitly in offline mode.

```sh
python3 tools/cook_volumes.py --offline
python3 tools/build_volume_converter.py --offline --config Release
python3 tools/test_volumes.py --cooked assets/cooked
```

`assets/scenes.json` supplies scene IDs, titles, allowed views, deterministic
sequence names and required cooked resources. CMake generates
`kgfx_scene_catalog.hpp` for both demo hosts. Python launchers and tests read the
same catalog, and PS4 packaging verifies its identity and required assets.
Library-only builds do not require the catalog or Python.

The glTF source describes JPEG images. The cooker deliberately resolves their
lossless PNG counterparts, leaving the source unchanged. The separate rifle
accessory alpha map is applied only to the glass material. All models retain
all source triangles, materials and double-sided settings. Missing tangents are
generated with MikkTSpace; vertices are reindexed only after tangent generation.

The cooker emits versioned little-endian records defined by
`tools/include/kgfx_asset_format.h`. Vertex positions include the glTF node world
transform. Normal transforms use the inverse transpose; mirrored transforms
reverse triangle winding. Texture bytes use the glTF UV convention, with the PNG
first row at v=0. Albedo is sRGB RGBA8; normals and packed AO/roughness/metalness
are linear RGBA8. Material textures have full mip chains. Color mipmaps filter in
linear light with alpha-weighted color, and normal mipmaps are renormalized.

Environment processing keeps EXR radiance unclipped in RGBA32F. The 2K source is
sampled into a 512² cubemap, whose mip zero also serves as the sky. Lower levels
use GGX importance sampling with source-mip selection by solid angle, 1024 samples
per texel. Asset format version 2 uses six specular levels ending at 16²; the
complete roughness range maps onto those levels. A 1² or 2² tail undersamples
directional lighting and produces bands on rough metals. The bounded chain
preserves smooth reflections without additional runtime samples. The source
equirectangular mip chain uses solid-angle row weights.
Diffuse convolution uses a 32² cubemap with 32768 radiance/solid-angle
importance samples shared across all normals; this concentrates work on compact
HDR light sources and avoids visible per-normal sampling noise. It stores
irradiance divided by pi. The split-sum BRDF lookup is 256²,
using 1024 samples and correlated Smith visibility. Cube order is +X, -X, +Y, -Y,
+Z, -Z, with OpenGL face coordinates. Runtime demos load these records directly;
they contain no glTF/image/EXR decoders or environment baking.

`assets/cooked/` survives build cleaning. Its stamp records source/tool hashes and
checksums of all outputs. Cooking happens under an exclusive process-scoped
file lock and stages results before publication. The OS releases the lock when
a converter exits or is killed, so the next cook can resume automatically. Reuse
validates output checksums; source files and existing payload timestamps remain
unchanged. A successful reuse may update the stamp to satisfy incremental builds.

`tools/cook_lighting.py` independently fingerprints and verifies the lighting
bake, so changing the room does not recook all model textures and HDRIs.
`kgfx_lighting_cooker` uses pinned xatlas and portable CPU ray tracing, without
Blender. The current room unwraps 113 static cube receivers into a 309×312 base
atlas. Twelve directional fields occupy a 1236×936 RGBA32F texture; static curved
and imported receivers retain six-lobe per-object fallback data. Imported mesh
triangles participate in visibility tracing. Static sources alone supply three
diffuse bounces; Rect sources sample their actual emitting surface.

`lighting/industrial.kglight` stores versioned probe/surface data,
`industrial.kglmap` stores object UV mappings and scene identity, and
`industrial_lightmap.kgtex` stores directional fields. `lighting/cook.stamp`
tracks all three hashes separately. Resource and PS4 packages merge model,
lighting and volume stamps. Runtime rejects stale geometry, material membership,
Static light edits and changed imported mesh bytes. Existing model files remain
version 2. Dynamic-only scenes never load the baked companion.

The LTC fit is pinned to selfshadow/ltc_code commit
`31e5e96b54f98f33098f8503003119ba2231a1c6`, with its license retained in
`deps/ltc-LICENSE.txt`. `tools/lighting_lookup.py` packs the BRDF and both LTC
tables into one 256×384 floating-point texture to conserve runtime bindings.

`deps/toolchain-lock.json` separately pins offline compilers, asset libraries and
PSBC build prerequisites. Other source archives and extracted caches are never
build directories. OpenGNM and PSBC are editable Git submodules at
`deps/opengnm` and `deps/opengnm-psbc`. Build snapshots include tracked and
non-ignored untracked files, with commit and content hashes for invalidation.
Generated code and compiler output stay outside both child working trees.
Initialize the forks with `git submodule update --init --recursive`. Publish fork
changes in their child repositories before updating the parent gitlinks. Archive
source/cache markers remain checked against locked archive hashes.
`KSTAR_GFX_OFFLINE=ON` rejects missing/corrupt inputs instead of downloading.

The shader pipeline compiles Vulkan GLSL to validated SPIR-V 1.0. UBO bindings
are 0–3 and sampled-image descriptor bindings are 8+the logical texture slot.
SPIRV-Cross generates GLSL 410 without the unsupported 420pack extension.
The OpenGL implementation binds uniform blocks and samplers by `kgfx_uN` and
`kgfx_tN` names. Reflection emits logical binding constants, descriptor bindings,
layout hashes and optional C/C++ consumer-struct layout assertions.

Shared local `#include` files are expanded by `tools/shader_source.py` before
both target compilations. Cycles and parent-directory traversal are rejected.
CMake tracks shared GLSL files, and shader packages record the expanded-source
hash plus the individual source dependencies. Native incremental compilation
and PS4 packaging reject stale include content. Reflected host-layout checks
cover uniform arrays as well as scalar and matrix members.

Separate `.gnm.spv` artifacts apply the common OpenGL clip-space depth convention
to native GNM at compile time. GNM `.sb` binaries require the PSBC fork host
compiler and explicitly target base PS4 using `-4`. Cross builds import Release
host tools through `KSTAR_GFX_HOST_TOOLS`; they do not try to execute PS4 tools on
the build host. `KSTAR_GFX_PSBC_EXECUTABLE` selects the fork native compiler.

`tools/build_psbc.py` acquires its locked auxiliary inputs and builds the current
PSBC submodule in a disposable source copy. Its `--offline` mode requires the
verified auxiliary cache and initialized submodules. The maintained fork code
supplies the descriptor layout, full native pointer pairs,
GFX7 register budgets, dynamic vertex-fetch ABI and standalone varying linkage.
Fragment inputs receive dense hardware locations before interpolation masks are
gathered; the native validator checks every declared interpolation slot and
rejects a CRC-valid package whose inputs all alias slot zero. Front-face registers
use the native signed-float ABI (`SPI_BARYC_CNTL.FRONT_FACE_ALL_BITS=0`), matching
Mesa's direct ±1 lowering without additional conversion instructions. See
[AMD CIK register definitions](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/programmer-references/CIK_3D_registers_v2.pdf).
Native shader cache identities hash emitted ISA and register/interface metadata,
so precision variants and compiler repairs cannot reuse a stale SPIR-V-only ID.
It also repairs the trailing native input-usage table, whose former offset
wrapped for shaders larger than 1 KiB. Build provenance records actual fork
commits, working-tree content hashes and compiler hashes; packaging rejects stale compiler provenance before compilation
or incremental reuse, and records every output hash. Editing either fork
invalidates affected build copies; changed generator inputs regenerate Mesa
tables. Conflicting sibling dependency directories are rejected.

Fragment packages retain the native FP16 shader as their prefix and append a
native FP32 variant, followed by four little-endian 32-bit footer fields:
`KGFX` magic, version 1, FP32 offset, FP32 size. The backend chooses FP32 for
RGBA32F/R32F pipelines once during creation; other targets keep packed FP16
exports. Vertex packages remain native shader binaries. The validator checks
container extents, native checksums, pointer pairs, fetch calls and PARAM exports.
`src/gnm/shaders/resolve.provenance.json` records the pinned toolchain, compiler
fork identities and hashes of the embedded resolve source, SPIR-V and native packages.

The build and package drivers use this interface:

```sh
build/macos-arm64/bin/Release/kgfx_asset_cooker texture normal \
  assets/source/textures/rock_tile_floor/rock_tile_floor_nor_gl_2k.png \
  .tmp/rock_tile_floor_nor_gl_2k.kgtex
python3 tools/build_psbc.py --build-dir build/macos-arm64/psbc-src --offline
python3 tools/compile_gnm_shaders.py --host-tools build/macos-arm64 \
  --psbc build/macos-arm64/psbc-src/opengnm-psbc --source shaders \
  --output build/ps4-x86_64/resources/shaders --config Release
python3 tools/test_gnm_shader_packages.py build/ps4-x86_64/resources/shaders
python3 tools/embed_gnm_shaders.py --glslang build/macos-arm64/bin/Release/glslang \
  --spirv-val build/macos-arm64/bin/Release/spirv-val \
  --psbc build/macos-arm64/psbc-src/opengnm-psbc --check
```

The standalone native metadata table conventions are cross-checked against
[fpPS4's shader parser](https://github.com/red-prig/fpPS4/blob/trunk/chip/ps4_shader.pas).
The integration uses independently written packaging and validation code.

Demo packaging verifies every cooked asset and shader artifact before replacing
any previous payload, including reflected layouts and generated headers. It
refreshes changed assets and shaders even when the executable does not relink. `resources/notices` includes asset attribution, complete
source manifests, dependency lock records and available dependency license texts.
The executable resolves all resources relative to its own location. No network
access or working-directory assumptions are needed after packaging.

Verification commands:

```sh
python3 tools/test_acquisition.py
python3 tools/test_assets.py assets/cooked
python3 tools/test_scene_catalog.py
build/macos-arm64/bin/Debug/kgfx_asset_cooker --self-test
build/macos-arm64/bin/Release/kgfx_asset_cooker --self-test
```
