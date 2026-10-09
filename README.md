# kstar_gfx

A standalone C graphics API with a C++23 implementation and one backend selected at build time. The library contains no runtime backend switch, virtual RHI, or backend function table. OpenGL 4.1 and PS4 GNM have separate native implementations. SDL is used only by the desktop demo and test hosts.

```sh
./build.sh --config=Debug
./run.sh --config=Debug -- --demo=pbr
./run.sh --config=Debug -- --demo=pbr --environment=1
ctest --test-dir build/macos-arm64 -C Debug --output-on-failure
```

The first demo build acquires pinned dependencies and the selected Poly Haven assets. Later builds can use `--offline`. `--clean` removes only the selected platform build directory; verified downloads and cooked assets survive. On macOS the script selects Apple Clang through `xcrun`. Linux and Windows can use the equivalent CMake commands with Ninja Multi-Config; see the validation record for tested platforms.

Demo modes are `triangle`, `cubes`, `pbr`, `instances`, `offscreen`, and `stress`. PBR uses the 2K rifle and fisherman's hat models with Industrial Pipe and Valve or Qwantani Night lighting. The raster path includes tangent-space normal maps, GGX materials, image-based lighting, 2048² directional shadows, RGBA16F HDR, 4× MSAA, and ACES-fitted tone mapping. Captures read the completed GPU render target.

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

See [API contracts](docs/architecture.md), [asset and shader tooling](docs/assets-and-tools.md), and [validation](docs/validation.md). [PS4 build and emulator procedures](docs/gnm.md) accept the existing shadPS4 installation or a separate build of the KnightStarBuilds fork. Physical PS4, Linux and Windows execution remain unvalidated. This repository has its own Git history; Nocturne's renderer is not yet migrated.

The library is MIT licensed. External dependency licenses and CC0 asset attribution are pinned in the dependency manifests and included with packaged demo resources.
