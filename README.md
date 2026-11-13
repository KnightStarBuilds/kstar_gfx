# kstar_gfx

A standalone C graphics API with a C++23 implementation and one backend selected at build time. The library contains no runtime backend switch, virtual RHI, or backend function table. OpenGL 4.1 and PS4 GNM have separate native implementations. SDL is used only by the desktop demo and test hosts.

```sh
./build.sh --config=Debug
./run.sh --config=Debug -- --demo=facility
./run.sh --config=Debug -- --demo=pbr --environment=1
ctest --test-dir build/macos-arm64 -C Debug --output-on-failure
```

The first demo build acquires pinned dependencies, Poly Haven assets and CC0 JangaFX simulations. Later builds can use `--offline`. `--clean` removes only the selected platform build directory; verified downloads and cooked assets survive. On macOS the script selects Apple Clang through `xcrun`. Linux and Windows can use the equivalent CMake commands with Ninja Multi-Config; see the validation record for tested platforms.

The default `facility` walkthrough connects an industrial loading yard, glass laboratory, flooded pump hall, and boiler room. `glass_lab`, `waterworks`, and `boiler_room` offer focused maps with material controls, valves, and sprinklers. Walk with WASD/mouse, interact with F, change camera with C, and switch scenes with Page Up/Down or the settings menu. PS4 walking uses both sticks, Cross, Circle, and D-pad left/right; inspection uses left-stick orbit, L2/R2 zoom, and Cross to reset. `--list-scenes` lists all maps and capture views.

The showcase adds refractive glass, simulated water and impact foam, projected wet decals, textured billboard particles and VDB-derived hero fire/smoke. `lighting_baked` and `lighting_dynamic` share one industrial room with typed physical lights, LTC rectangles, cached local shadows and dynamic one-bounce probes. The baked version adds Static three-bounce lightmaps. Press M for a matched scene switch, P to freeze animation, L to change Stationary color, and +/- for intensity. Cinematic targets 1080p/30 with 4× MSAA; this is a performance goal, not a hardware guarantee. Both native backends use the same scene and render passes.

The original `triangle`, `cubes`, `pbr`, `instances`, `offscreen`, and `stress` demos remain available. PBR retains the rifle and fisherman's hat, 2K Rock Tile Floor, both HDR environments, GGX materials, directional shadows, HDR, MSAA, and ACES tone mapping. Captures read the completed GPU render target.

Options on PS4 or Escape/F1 on desktop opens graphics settings. Cinematic and Quality use 1080p / 4× MSAA / 2048² shadows; Performance uses 720p / 4× MSAA / 1024² shadows with smaller effect budgets. Custom settings expose render resolution, anti-aliasing and shadow size, alongside scene, environment, exposure and diagnostics. Apply changes explicitly; choose Quit App from the menu to exit.

```sh
./run.sh --config=Release -- --demo=pbr --headless --benchmark \
  --width=1920 --height=1080 --frames=300 --capture-frame=300 \
  --output=build/pbr.ppm
```

The demo loads packaged resources relative to its executable. `--resources` selects another package. No runtime downloads occur. See [demo controls and capture checks](docs/demos.md).

To embed the repository as a submodule or other source dependency:

```cmake
set(KSTAR_GFX_BACKEND OPENGL CACHE STRING "")
set(KSTAR_GFX_BUILD_DEMOS OFF CACHE BOOL "")
set(KSTAR_GFX_BUILD_TESTS OFF CACHE BOOL "")
add_subdirectory(deps/kstar_gfx)
target_link_libraries(my_renderer PRIVATE kstar_gfx::gfx)
```

Include `<kstar_gfx/gfx.h>` from C or C++. The host creates an OpenGL 4.1 core context and passes its procedure loader and presentation callback using `<kstar_gfx/opengl.h>`. The library-only OpenGL build has no dependency downloads:

```sh
./build.sh --library-only --offline
```

For Release consumers, enable interprocedural optimization on the consuming target as well as the library to allow inlining across the C API. Backend selection is static even without LTO. Driver calls, command recording, resource validation, and synchronization still have their native costs; there is no claim that rendering itself costs zero CPU time. The `kgfx_dispatch_benchmark` target compares native GL draw submission with the C entry point using identical GPU state.

See [API contracts](docs/architecture.md), [asset and shader tooling](docs/assets-and-tools.md), and [validation](docs/validation.md). [PS4 build and emulator procedures](docs/gnm.md) include the version 01.01 hardware package and accept the existing shadPS4 installation or a separate build of the KnightStarBuilds fork. Native viewport guardbands and the MSAA raster-enable state have been corrected; the current changes await physical PS4 retesting. Linux and Windows execution remain unvalidated. This repository has its own Git history; Nocturne's renderer is not yet migrated.

The library is MIT licensed. External dependency licenses and CC0 asset attribution are pinned in the dependency manifests and included with packaged demo resources.
