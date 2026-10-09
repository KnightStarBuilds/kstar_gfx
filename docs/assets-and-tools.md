# Assets and offline tools

`assets/manifest.json` locks the requested Poly Haven models and environments.
Source payloads live in ignored `assets/source/`, retaining per-model folders and
relative names. All material maps use 2048×2048 PNG, and both environments use
2048×1024 floating-point EXR. Acquisition verifies SHA256 over the actual file;
Poly Haven's MD5 metadata is not used as the integrity check.

The glTF source describes JPEG images. The cooker deliberately resolves their
lossless PNG counterparts, leaving the source unchanged. The separate rifle
accessory alpha map is applied only to the glass material. Both models retain
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
checksums of all outputs. Cooking happens under an exclusive directory lock and
stages results before publication. An interrupted cook can leave `cook.lock`;
remove that directory only after verifying that no cooker is running. Reuse
validates output checksums; source files and existing payload timestamps remain
unchanged. A successful reuse may update the stamp to satisfy incremental builds.

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
build/macos-arm64/bin/Debug/kgfx_asset_cooker --self-test
build/macos-arm64/bin/Release/kgfx_asset_cooker --self-test
```
