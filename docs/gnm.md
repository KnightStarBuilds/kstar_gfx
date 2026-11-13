# PS4 GNM backend

OpenGNM and PSBC are editable Git submodules at `deps/opengnm` and
`deps/opengnm-psbc`, using the KnightStarBuilds forks. Their fixes live directly
in those repositories. Builds consume content-identified copies of the working
sources, including local edits; compiler output stays outside the submodules.
No patch files are applied and no dependency source is rewritten during a build.

Initialize dependencies after cloning with `git submodule update --init`.
Commit changes inside each fork first, then update its gitlink in kstar_gfx.
Uncommitted source changes are usable locally and are recorded by content hash,
but another checkout cannot reproduce them until the fork commits are shared.

OpenGNM submission wrappers stay disabled so submission resolves directly to the
platform GNM imports. PSBC targets base PS4 GFX7 through the offline
`tools/build_psbc.py` build. No proprietary SDK or Unreal implementation is
included. Native artifacts identify the actual compiler and fork source content.

## Build

The lighting/effects revision cross-compiles the demo and 30-feature probe to
PS4 ELF in Debug and Release. All 28 native shader packages pass validation,
including the LTC polygon shader's dynamically indexed local-array writes.
PSBC lowers those temporary arrays before ACO instruction selection; shader
features are retained. GNM layout, guardband and multisample host checks pass in
both configurations.

The current ARM64 host cannot execute the SDK's x86-64 `create-fself-macos`, so
SELF/FPKG packaging is blocked after ELF linking. The available installed and
maintained-fork shadPS4 binaries are also x86-64 and cannot run in this host
environment. The new indirect-array readback probe, current scene comparisons,
focused window input and low-resolution shaft regression therefore remain
unverified. Historical emulator results below cover earlier renderer revisions;
they do not validate the new lighting/effects shaders. No installed emulator or
driver was modified. Physical PS4 remains untested for this revision.

A host LLVM installation with `clang`, `clang++`, `ld.lld`, `llvm-ar`, and `llvm-ranlib` is required for the cross target. Native host tools use Apple Clang through `xcrun` on macOS. The external OpenOrbis SDK pin is recorded in `deps/lock.json`; pass the installed SDK directory explicitly or set `OO_PS4_TOOLCHAIN`.

The SDK's `mode_t` declaration is corrected in the maintained source header
`src/gnm/sdk-include/bits/alltypes.h`, with provenance and license recorded in
`deps/sdk-compat.json`. The build copies that header verbatim and verifies the
selected SDK input. It neither rewrites SDK source nor changes the installed SDK.

```sh
./build_gnm.sh --config=Debug --sdk /path/to/PS4Toolchain
./build_gnm.sh --config=Release --sdk /path/to/PS4Toolchain --offline
```

`--llvm-bin` selects another LLVM tool directory. `--resources` reuses an existing host demo resource package; otherwise the helper builds the host Release package first. `--host-tools` reuses an existing native CMake build containing glslang, SPIRV-Tools, and SPIRV-Cross. Shader reflection and PSBC compilation are offline build steps; none run on the console.

For conformance without curated model/HDRI assets:

```sh
./build_gnm.sh --probes-only --sdk /path/to/PS4Toolchain
```

For the standalone core using its checked-in native internal shaders:

```sh
./build_gnm.sh --library-only --sdk /path/to/PS4Toolchain --offline
```

The CMake library-only target needs neither Python nor host shader tools; the Python build helper is optional. Host tooling, curated assets and executable packaging are added only when demos or probes are requested.

Outputs are under `build/ps4-x86_64/standalone`: static archives, PS4 ELF executables in `bin/<config>`, and executable/FSELF packages in `app0/<config>`. `--clean` removes only that integrated PS4 build tree. The SDK, dependency caches, host tools, and cooked assets survive. Per-configuration build provenance records the selected SDK CRT/link-script hashes and LLVM compiler.

A CMake consumer can instead select `KSTAR_GFX_BACKEND=GNM` and use `cmake/ps4-toolchain.cmake`. The target remains `kstar_gfx::gfx`; GNM configuration and implementation details are private to this build. No runtime backend selection occurs.

Release uses supported IPO/ThinLTO for the library, OpenGNM and demo/probe consumers. The LLVM cross-linker accepts native OpenOrbis runtime objects alongside LLVM bitcode. Debug remains an incremental build without IPO.

## Hardware package

Build the full Release demo with the command above before creating the installable
FPKG. The demo build prepares the gallery and industrial models, their 2K textures,
the floor/concrete/metal materials, authored effects, both HDRI environments
and the native GNM shaders. A library-only or probes-only build is insufficient;
`--offline` requires the dependency and asset caches to be populated already.

From the kstar_gfx root:

```sh
python3 ps4/package.py --config Release
```

The packager reads the OpenOrbis SDK location from the configured GNM CMake cache
and writes
`build/ps4-x86_64/standalone/pkg/Release/IV0000-KGFX00001_00-KSTARGFXDEMO0000.pkg`.
The application identifies itself as `KGFX00001`, version `01.01`, and targets
firmware 9.00 with GoldHEN, matching Nocturne's hardware package configuration.
Copy the `.pkg` to a USB drive and install it using the console's Package Installer.
The package contains the native SELF executable, OpenOrbis startup modules,
cooked assets, native shaders and license notices. shadPS4 is not packaged.

Creation verifies cooked assets and native shader hashes before staging, runs
LibOrbisPkg integrity checks, and extracts the result to verify its contents.
The generated SFO retains the application settings and adds only the package
tool's publishing fields. A SHA256 file, input manifest and validation log are
written beside the `.pkg`; failed verification preserves the previous package.

Launching the installed application opens the connected facility walkthrough at
1920×1080 without command-line arguments. Cinematic uses 4× MSAA, 2048-pixel
shadows and the full effects budget. The menu and D-pad select the other three
walkthroughs or any of the six legacy demos. Resources load from `/app0/resources`; the default session
writes no captures and has no timer.

| Controller input | Action |
| --- | --- |
| Left stick / right stick | Walk / look |
| L3 | Sprint |
| Cross | Interact with the nearby station |
| Circle | Toggle inspection camera |
| R3 | Reset walkthrough |
| D-pad left / right | Previous / next scene |
| Left stick, L2 / R2 in inspection | Orbit, zoom out / in |
| Triangle | Switch HDRI environment |
| Square | Cycle material diagnostics |
| L1 / R1 | Decrease / increase exposure |
| Cross in legacy / inspection | Reset |
| Options | Open or close graphics settings |
| D-pad, while settings are open | Navigate rows and edit values |
| Cross, while settings are open | Select the current row |
| Circle, while settings are open | Return to the demo |

Select **Quit App** inside settings to finish GPU work and exit to the console
menu. Cinematic and Quality select 1080p / 4× MSAA / 2048² shadows with different
particle budgets; Performance selects
720p / 4× MSAA / 1024² shadows. Custom offers 720p, 900p or 1080p, 1× or 4×
samples, and 1024² or 2048² shadows. Scene, environment, exposure and diagnostics are
also available. **Apply Changes** replaces internal render targets after GPU
completion; the native VideoOut buffers retain their initial size. Allocation
failure keeps the active settings. Preset names do not promise a measured frame
rate. See [demo settings and controls](demos.md#graphics-settings).

Package creation and emulator validation do not establish successful installation,
rendering, controller input, exit behavior or performance on physical PS4 hardware.
Record those checks separately when testing the installed application. The
current depth, MSAA, menu and floor-material changes require a physical PS4 retest.

### Native raster state

Viewport setup now computes guardband clip limits from the viewport scale and
offset, keeping raster coordinates inside the signed 16-bit pixel range. This
addresses the large ground plane extending behind the camera or far outside
the viewport; the depth test remains enabled and the geometry is unchanged.

Each pass explicitly programs `PA_SC_MODE_CNTL_0.MSAA_ENABLE` together with the
sample count, EQAA state, sample locations and masks. Four-sample attachments
alone did not enable native multisample rasterization after firmware state
initialization. Switching back to a single-sample pass also resets that state.
Host checks cover these register values; shadPS4 derives raster samples from
attachments and cannot establish that the hardware enable bit was correct.
Visual depth and anti-aliasing results still need confirmation on the console.

## Emulator validation

Use an explicitly selected emulator, as in Nocturne's PS4 workflow. The existing
shadPS4 0.18.0 installation remains unchanged. Its local executable is
`/Users/najmm/Workspace/shadps4-macos-sdl-0.18.0/shadps4`.

The optional [KnightStarBuilds/shadPS4](https://github.com/KnightStarBuilds/shadPS4)
test checkout lives at `deps/tools/shadps4`, with separate build output under
`build/validation/shadps4`. Emulator fixes live directly in that fork. Keep its
source revision and working-tree changes with the validation evidence.
The graphics library does not download or build an emulator as a dependency.
Select a fork build with the same `--emulator` option and keep its results separate
from the installed release. The fork contains the fixes required for correct
PBR lighting; the installed 0.18.0 binary has known rendering and stability
failures described in [validation](validation.md).

Walkthrough shaders also require the fork's `S_OR_SAVEEXEC_B64` translation
(`72f34e5e`). It reuses the existing saved-EXEC helper to preserve the old mask,
OR the source into EXEC and update SCC. The unhandled scalar opcode previously
aborted shader recompilation before a walkthrough capture completed. Matching
Debug/Release fork builds pass the full 29-feature native shader probes; this
does not change the installed emulator or establish physical-console results.

The user-facing runner resolves the selected build, prepares a complete app0 and
isolated portable profile, sets an absolute emulator home, and creates home
folders for default and copied users. It keeps the emulator's normal profile
separate and records executable hashes, launch arguments, logs and captures.
Choose a new output directory for each run.

```sh
python3 ps4/validate.py --config Debug \
  --emulator build/validation/shadps4/cmake/Release/shadps4 \
  --output .local-memory/ps4-pbr-debug --scene pbr
```

Use `--scene triangle` for the small presentation check. Select the installed
executable explicitly when comparing against the unmodified release.

Visible runs stay open until manual quit by default, with no process deadline.
Use `--seconds` or `--frames` for bounded validation; those runs retain a default
120-second deadline allowance in addition to any requested duration. An explicit
`--timeout` opts into a process deadline even for an interactive run.

Visible runs exercise VideoOut and controller input. Escape is mapped to the
native Options button in the copied input profile to open settings. Select
**Quit App** with Cross (N in the default keyboard profile) to finish and release
resources. Untimed runs capture the first frame and write its
metadata on guest exit; timed runs capture the final frame. Closing the window terminates the emulator through its
normal host-window path. A window close before a capture does not establish
rendering correctness. `--headless` is reserved for offscreen pixel comparisons;
it does not validate presentation or interactive quitting, and the emulator may
still create a host window.

Precise GPU readbacks use `readbacks_mode=2` and
`readback_linear_images_enabled=true` in the isolated profile. These are ordinary
emulator settings. Captures use `/data/captures` inside that profile. Desktop
CTest never attempts to execute a PS4 ELF on the host. Command-buffer copying
uses the emulator default or the imported profile's setting.

The independent presentation probe checks actual display-buffer pixels and
completion of 600 flips while recycling three buffers:

```sh
python3 scripts/run_gnm_probe.py --present \
  --emulator /Users/najmm/Workspace/shadps4-macos-sdl-0.18.0/shadps4 \
  --elf build/ps4-x86_64/standalone/bin/Debug/kgfx_gnm_probe \
  --sdk /path/to/PS4Toolchain \
  --shaders build/ps4-x86_64/standalone/probe-shaders/Debug \
  --output .local-memory/ps4-present-debug
```

Omit a scope option for the full shader-feature probe. Use `--discard-only` to
isolate fragment discard and retained depth, or `--buffer-overlap-only` to test
uniform and vertex bindings that share one buffer. These options and `--present`
are mutually exclusive. The overlap check binds a 64 KiB uniform range before a
256 KiB vertex span, then checks a second draw after rebinding and the background
clear. `--copy-gpu-buffers` explicitly enables emulator command-buffer copying for
a controlled comparison. Each result records that setting, the process ID, and
executable, shader and driver hashes.

Full-probe results are independent of the presentation check. Emulator crashes and unsupported behavior are
reported as validation failures or limitations. Emulator defects may be fixed
and tested in the fork without changing the installed release or reducing scene
quality. See [validation](validation.md)
for the checks actually completed. Emulator results do not establish physical
PS4 compatibility or performance.

### Building the optional test fork on macOS

The `kstar-gfx` branch uses upstream commit
`0c9ca525b8add53e62b3ccd9c9c2f6ef2ebabbfa` (0.19.1) plus the maintained fork
changes. The x86_64 emulator runs through Rosetta on Apple Silicon; the native
library/tools remain ARM64. From the kstar_gfx root with the test checkout present:

```sh
git -C deps/tools/shadps4 switch kstar-gfx
git -C deps/tools/shadps4 submodule update --init
git -C deps/tools/shadps4 submodule update --init --recursive externals/zydis
kgfx_validation_sdk="$(xcrun --sdk macosx --show-sdk-path)"
env -u LDFLAGS SDKROOT="$kgfx_validation_sdk" cmake \
  -S deps/tools/shadps4 -B build/validation/shadps4/cmake \
  -G "Ninja Multi-Config" -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_SYSROOT="$kgfx_validation_sdk" \
  -DCMAKE_C_COMPILER="$(xcrun --find clang)" \
  -DCMAKE_CXX_COMPILER="$(xcrun --find clang++)" \
  -DCMAKE_OBJC_COMPILER="$(xcrun --find clang)" \
  -DENABLE_SYSTEM_VULKAN=ON -DENABLE_UPDATER=OFF -DENABLE_DISCORD_RPC=OFF \
  -DCMAKE_BUILD_RPATH='@executable_path' \
  -DCMAKE_EXE_LINKER_FLAGS= -DCMAKE_MODULE_LINKER_FLAGS= -DCMAKE_SHARED_LINKER_FLAGS=
env -u LDFLAGS SDKROOT="$kgfx_validation_sdk" cmake \
  --build build/validation/shadps4/cmake --config Release --parallel 4
```

The installed KosmicKrisp driver reports `sparseBinding=0` and
`sparseResidencyBuffer=0`. The fork selects ordinary resident buffers when the
device or selected queue cannot support sparse buffers. This path uses 64 KiB
pages, bounds growth padding, merges overlapping ranges with GPU copies, updates
the buffer-device-address page table and retires old allocations after their
last submitted use. Binding retries retain pending barriers and fast clears when
a range grows during draw setup. The sparse path remains available on capable
devices; it has not been exercised by the macOS validation runs.

The system-Vulkan configuration keeps the existing driver constant while testing
emulator source changes. The unused nested Mesa checkout is not needed for this
configuration. Copy `libvulkan.dylib`, `libvulkan_kosmickrisp.dylib`, and
`kosmickrisp_mesa_icd.json` from the installed 0.18.0 directory into
`build/validation/shadps4/cmake/Release/`. This leaves the installation unchanged;
it does not test the fork's bundled Mesa build. Select
`build/validation/shadps4/cmake/Release/shadps4` with `--emulator` in the same
validation commands above. Record the source commit, executable hash and driver
hashes for each comparison. Commit and push the fork source changes before
expecting another checkout to reproduce them. Build and validation commands do
not commit or push either repository.

## Current API limits

GNM supports one or four samples. Color rectangle copies operate on single-sample 2D surfaces; multisampled targets use the explicit resolve operation before readback. Color textures retain storage for every requested mip, with mip zero used as the attachment. Depth textures support one mip. Depth copies require matching complete surfaces. Copies require distinct, matching-format/sample-count textures and run outside a pass. Cubemap attachments/face copies, partial depth copies and CPU depth uploads report unsupported instead of reinterpreting tiled or compressed memory. CPU uploads to multisampled textures are rejected. VideoOut dimensions remain fixed for the device lifetime; offscreen devices can resize, as exercised by the stress scene.

Vertex attributes support divisor zero (per vertex) or one (per instance). Larger divisors are rejected at pipeline creation rather than silently changing their rate.
