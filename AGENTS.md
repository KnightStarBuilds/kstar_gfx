# kstar_gfx

Independent MIT graphics library. Public headers compile as C11; implementation
uses C++23 without extensions. CMake 3.24+, Ninja Multi-Config, incremental Debug
by default. Select exactly one OPENGL or GNM backend at configure time. Backend
source files directly define kgfx_* functions: no virtual interface, runtime
backend switch or RHI function-pointer table.

Keep core independent of SDL, demo assets and Nocturne. Use native OpenGL 4.1 on
desktop and OpenGNM/OpenOrbis on PS4. Preserve native GPU completion and resource
lifetimes. No steady-state library draw allocations or global mutexes. Do not
reduce visual features to conceal compiler/backend defects.

Keep OpenGNM and PSBC as editable Git submodules. Fix their actual source files;
do not apply patches or rewrite dependency code during builds. Build from
content-identified copies so compiler output stays out of the submodules.
Keep other acquired dependencies immutable and assets behind SHA256-verified
manifests. Temporary work goes under .tmp/. Clean only the selected platform
build tree; caches, submodules and assets survive clean builds.

shadPS4 is a validation tool. Preserve the user's installed emulator and drivers.
The KnightStarBuilds/shadPS4 fork may be cloned into deps/tools/shadps4 and built
separately under build/validation/shadps4 for testing. Fix actual source and
commit emulator fixes in that fork; do not apply patch files during builds or
push without authorization. Record installed-binary and fork results separately.
Use isolated portable profiles and the ps4/validate.py workflow. Validate visible
presentation, focused Escape and window close separately from offscreen readback.

Run applicable Debug/Release tests. Report native GL, GNM host checks, shadPS4,
physical PS4, Linux and Windows evidence separately. Do not claim unexecuted
checks or classify image transport as rendering validation.
