# kstar_gfx progress

Native macOS Debug and Release suites pass 47/47 checks. The six Cinematic
showcases meet the 33.33 ms total-frame p95 target in bounded 1080p captures with
two simulation ticks per rendered frame. Current GNM Debug/Release ELF builds
and all 28 native shader packages pass compilation and structural validation.
See [validation.md](validation.md) for evidence and measurement limits.

## Remaining implementation

- [ ] Extend xatlas directional lightmaps to static curved and imported mesh
  receivers. These currently use six-lobe per-object bake fallbacks. Preserve
  the existing model format, companion-payload versioning and stale-bake checks.
- [ ] Review and improve probe visibility around thin walls and moving blockers.
  Directional distance moments approximate visibility; check for light leaking
  across occluding surfaces in the matched rooms and close views.
- [ ] Reduce the dynamic-bounce transient on matched scene switches. Fresh
  probes currently converge over up to twelve rendered frames. Preserve the
  sixteen-probe/96-ray frame budget and avoid transferring incompatible baked
  contributions into the dynamic scene.

## PS4 packaging and GNM validation

- [ ] Restore a usable ELF-to-SELF and FPKG packaging workflow on a supported
  host. The current OpenOrbis macOS SELF converter fails with OS error 86 on
  this environment; the package tool is also x86-64. Validate the selected host
  tools and produce Debug/Release packages with current shaders, assets and
  notices. Any replacement host tools need pinned source and provenance.
- [ ] Run the current 30-feature GNM readback probe with the maintained,
  source-built shadPS4 fork in Debug and Release, including indirect local-array
  reads/writes, resource retirement and repeated HDR/MSAA resolve/load behavior.
  The local fork's x86-64 executable currently fails to launch with OS error 86,
  including outside the sandbox. This is an execution-environment blocker;
  the fork source and local builds already exist.
- [ ] Capture and compare current GNM lighting, glass, volume, water and wetness
  views against native OpenGL at 1× and 4× MSAA. Existing emulator results cover
  earlier renderer revisions.
- [ ] Recheck the known low-resolution GNM light-shaft artifact against native
  OpenGL. The dedicated shaft pass has changed, but resolution of the artifact
  requires rendered evidence with the current shaders.
- [ ] Validate the current packages on physical PS4: depth and MSAA state,
  lighting/shadows, transparency and effects, controller input, resource
  lifetimes and sustained performance. Host checks and emulator output do not
  establish hardware acceptance.

The shadPS4 fork tracks its execution and emulation work in its own
`PROGRESS.md`. PSBC's standalone compiler-regression coverage is tracked in
[its progress file](../deps/opengnm-psbc/PROGRESS.md).

## Native presentation and platform coverage

- [ ] Measure warmed interactive Cinematic 1080p performance including native
  event handling and presentation. The existing p95 results exclude those
  costs. Check Retina resizing/input, focus loss, minimization/recovery and
  sustained scene cycling.
- [ ] Record full matched walkthroughs and close orbits in both lighting scenes
  at 1× and 4× MSAA. Current evidence consists of final-frame captures. Review
  animated lights/blockers, probe leakage, volume overlap and camera-inside
  effects, smoke attenuation of embers, water impacts/foam, wet boundaries,
  reflections and glass composition throughout each route.
- [ ] Collect separate GPU pass timings on a backend/driver with timestamp
  support. The current Apple OpenGL driver and GNM implementation report
  unsupported timing; CPU wall times do not establish GPU pass costs.
- [ ] Build and run native Debug/Release checks on Linux and Windows, including
  shader/pixel tests, resource lifetimes, deterministic replay, offline cooking
  and clean-cache preservation. Report each platform independently.
