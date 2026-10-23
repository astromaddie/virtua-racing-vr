# VR architecture

Target: native Linux ARM64, OpenGL 4.3/GLX and OpenXR 1.0 through
SteamLinuxRuntime_4-arm64. `make build/vrde-vr` enables `VRDE_VR`; the ordinary
`make build/vrde` target retains the flat renderer. OpenXR-SDK 1.1.63 supplies
the statically linked loader. The runtime API request is 1.0 because the Frame
runtime rejects a 1.1 instance request; no 1.1 core call is required.

The base is [PokaMilkTea/VirtuaRacingDeluxeEnhanced at
9f059e10](https://github.com/PokaMilkTea/VirtuaRacingDeluxeEnhanced/tree/9f059e10e4c8bff928a581777f19e80c3d117b92).
PicoDrive still executes the game logic and audio. The 68000 sends the original
scene to SH2 memory, and Enhanced's HLE reconstructs its models. This is an
emulation/native-renderer hybrid, not a recompilation of the entire game.
The [sega-vr-disasm reference](https://github.com/matiaszanolli/sega-vr-disasm/tree/60819eff3cdf2abe0037081c9e6a484bf06bb9dc)
is a separate reverse-engineering reference.

## Scene and camera

`hle_vr_prepare()` interpolates a single snapshot at the display-frame boundary.
It collects exact camera-space model vertices in `draw_model()`, before screen
projection, near clipping, backface rejection or SH2 screen/depth buckets.
The immutable triangle mesh is uploaded once and used for both GPU eye draws.
Game state, interpolation time, animation and audio never advance per eye.
The original integer projection path remains available for flat pixel parity.

The VR candidate set includes the existing bounded track-grid walk in every
head direction, including rear cells. Object culling against the original
screen is bypassed during collection. Opaque triangles use a real depth buffer,
so turning away from the native camera does not retain its painter ordering.
Dithered shadows/effects retain cutout coverage, with depth testing. The
original data cannot supply dynamic objects which the game has omitted from
its scene; the existing far-view prop reconstruction covers the known course
props, not arbitrary missing meshes. The bounded grid can still end at the far
view distance. No substitute geometry or cockpit assets are generated.

The default is the original chase camera, with native camera pitch and roll
removed before applying tracked head orientation. The shader converts the
game's right/up/forward coordinates into OpenXR right/up/back coordinates.
Both eyes use their own runtime pose and asymmetric FOV from the same predicted
display time. Eye separation comes only from those poses in normal play.
Physical lean is clamped to 0.35 m around the recentered head centre; this is a
seated camera lean, not collision-body locomotion.

The provisional scale is **16 original world units per metre**. The sampled
Formula car body spans 67.265625 units longitudinally, giving a 4.20 m body as
an explicit scale reference. This is a geometric calibration assumption, not
a claim that the source model depicts a measured real vehicle. The single
uniform scale applies to world geometry and physical eye/head translation.
`VRDE_VR_WORLD_SCALE` accepts 4–64 units/m; larger values make the world smaller.
`VRDE_VR_HORIZON_LOCK=0` preserves the original camera pitch and roll.

## Draw inventory

| Source element | VR treatment |
|---|---|
| Track, cars, wheels, roadside models | Unprojected original triangles; per-eye GPU projection and depth |
| Original screen/object/track culling | Wider bounded candidates; GPU frustum/near-plane clipping per eye |
| Dithered shadows and effects | Original colour and cutout coverage with depth testing |
| Plane B sky | Direction-only cylindrical sampling of the original repeating plane; no positional disparity; wrap period follows VDP plane width |
| Copied 32X HUD | Original item mask supplies explicit alpha, retaining black artwork |
| HLE needle and course map | Separate HUD raster and coverage masks, including palette-index-zero coverage |
| Title, demo, menu, results and blinds transitions | Original full image on a yaw-anchored compositor panel; result HUD types 7/8/10 select flat presentation |
| Race HUD | Separate alpha compositor quad, yaw-following with roll removed |
| Projection background | Always present when submitting a visible frame, including flat screens |

HUD/menu artwork is reused without new UI assets. The panel defaults to 2 m,
1.9 m wide with the console's intended 4:3 display aspect. Its transparent
pixels are zero RGB as well as zero alpha, matching OpenXR premultiplied-alpha
composition. `VRDE_VR_HUD_DISTANCE` accepts 1–5 m. Sky sampling and polygon
palette output are already gamma encoded: framebuffer sRGB conversion is
disabled when copying these into the required sRGB swapchains.

Native VR eyes use fixed **4× MSAA**, with same-size sRGB8-alpha8 colour and
24-bit depth renderbuffers. The OpenXR images remain single-sample. Each eye
resolves colour into its acquired image before diagnostic readback and release;
the GPU eye timer includes that resolve. Both attachments must actually have
four samples and form a complete framebuffer, or initialization stops with a
diagnostic. There is no VR AA selector or automatic sample-count reduction.
The existing game MSAA setting continues to control only the flat HLE renderer.

Geometry keeps framebuffer sRGB conversion disabled for the encoded palette.
The matching-format resolve enables sRGB conversion only for the blit and then
restores the prior state. Where available, the resolve invalidates its temporary
colour and depth attachments immediately afterward to avoid unnecessary sample
storage on tiled GPUs. HUD/menu panels retain their separate single-sample path.

## Road depth and car contact

The original renderer uses painter sorting for coplanar road detail. VR draws
opaque geometry first, then original road paint, baked marks and persistent
ribbons in one depth-tested overlay pass with `glPolygonOffset(-1,-2)` and depth
writes disabled. This retains road/car/prop occlusion while preventing overlays
from fighting the asphalt or one another. Depth writes and polygon-offset state
are restored after the pass. Both passes use the same immutable vertex buffer.

A lazy per-course cache identifies track triangles with positive-area overlap
on an asphalt triangle's plane. Paint can straddle an asphalt edge; adjacent
terrain sharing only an edge, raised kerbs and vertical faces do not qualify.
Object/car meshes are never classified as road detail. Material colours and
painter-sort header bits alone are insufficient. The classification is cached
once per track model, shares the bounded road cache, and fails closed on overflow.

Original rotating tyre meshes sit roughly four world units above the rendered
road for all three car kinds. VR lowers the complete car assembly, including
children and car-relative effects, by four world units along world Y. This is a
constant visual pivot correction: physical poses, camera placement, wheel spin,
suspension and airborne displacement remain intact. The independent projection
check uses car provenance and this explicit transform rather than relaxing its
error tolerance. Flat rendering receives neither correction.

`build/vr_surface_rom_test ROM [0|1|2]` exercises all car kinds and pitched/rolled
poses through the real record renderer, and checks overlay coverage on all five
course grids. `build/vr_surface_gl_test --vr-diagnostic ... ROM` uses the real
production eye callback at 24 nearby poses, comparing its bias to disabled and
stronger bias while checking state restoration and ordinary depth occlusion.
It requires a GL implementation supporting the exact configured sample count.

## Persistent tyre marks

VR adds race-local rear tyre ribbons for the player and all 15 opponents. The
raw DREQ snapshot is the only update point; interpolation and either eye only
read the resulting world geometry. The 68000 opponent writer at `8836DE` maps
physics records `9100+i*100` to scene bodies `218+i*3c` in fixed order, so LOD
changes cannot exchange car identities. The cockpit helper `883130` calls the
same world-pose writer `882996`; tyre placement never uses the camera-relative
cockpit model matrix.

Eligibility follows the original sustained slip-sound controller (`8885C4`,
`888610`): absolute car-record `+94` starts above 32 and stops at 16 or below.
Those routines issue sound commands BD/C8. A midpoint-heading lateral-motion
check also catches genuine sliding/spins: 18% entry, 10% exit, with two ticks
of confirmation and at least 120 world units/s. Sound-qualified cornering does
not require that lateral angle. Both paths require real travel and reject
snapshot gaps, teleports and implausible pose jumps. A local deterministic hash
selects three quarters of sliding episodes, independently of the emulator's
RNG, and varies their dark grey intensity.

Rear wheel meshes supply tyre width and centres. The three car kinds have
rear tyre widths of 5.859375, 6.5625 and 5.609375 world units, confirmed from
their ROM asset packs. Body X/Z words are final world units; Y and wheel
placements divide by 16; object mesh vertices divide by 64. A lazy bounded
cache decodes only the current course grid's asphalt polygons (alternating
palette indices 12/13/14, verified present in all five course grids). Each ribbon
is clipped to those triangles and follows
their actual height and slope. The contact tolerance accommodates the original
ride height; airborne tyres, grass, water, kerbs, vertical faces, props and cars
are excluded. Marks use the existing palette and lie exactly on the receiving
triangle; the road-overlay pass supplies their depth bias. New segments appear only once
the interpolated presentation has reached their source tick. Both eyes see the
same immutable triangles.

The fixed ring holds 8,192 triangles, replacing the oldest only when full; the
road cache holds at most 16,384 triangles and fails closed when full. Combined
persistent storage, including cached road-overlay flags, is under 1.5 MiB. Drawing is limited to the existing reached
track cells and an 8,192-unit camera radius. Marks survive laps, pauses, focus
loss and view changes; a discontinuity only breaks connecting trails. Leaving
race command 2, results HUD types 7/8/10, a changed course grid or `hle_reset()`
(including the existing state-load route) clears them. There is no setting,
physics/RNG write or shader change.
The ordinary flat build excludes the entire feature.

`make vrskidtest` covers sound-threshold hysteresis, straight/reverse motion,
ordinary tangent-aligned corners, geometric sliding, all car slots, episode
determinism, repeated ticks, gaps/teleports/reset, slope/edge/contact rejection
and ring bounds. The optional `build/vr_skids_rom_test ROM` runs the real
`tools/race_inputs.txt` through production snapshot hooks, checking player/AI
emission, persistence, cockpit views, immutable preparation and results/reset.
Its `pause` argument tests Start pause/resume; `corner` uses the chronological
`tools/skid_corner_inputs.txt` script, releasing the first turn at frame 2026
and adding a right turn at 2100–2126. Host stereo diagnostics retain
the `[vr-diag] skids` counters alongside the existing seven-field state hashes.

## Frame protocol and controls

The target is **2160 × 2160 per eye at 120 Hz**. The application defaults to
2160-square swapchains, bounded by the runtime maximum. `VRDE_VR_EYE_SIZE`
accepts 1440–2160 for matched comparisons. Configure 120 Hz for this title in
SteamVR before launch; the application records the actual predicted period
and follows it. Neither refresh rate nor swapchain size changes during a
session. This target requires measured full-game timing, not only a held scene.

`xrWaitFrame` supplies time and pacing. A 59.92 Hz accumulator calls the existing
`run_frame()` and audio path once per due simulation frame, capped at six steps
following a long delay. `shouldRender=false`, invalid tracking and loss of focus
freeze ordinary simulation/audio and discard catch-up time while polling keeps
running. Valid unfocused views can still render the frozen scene. Both eyes
share the prepared scene; swapchain acquire/wait/draw/release surrounds each
eye, followed by the HUD/menu swapchain and `xrEndFrame`.

Session STOPPING, EXITING, LOSS_PENDING and process termination signals use the
same cleanup path. Signals only set a flag. Swapchain waits have bounded retry
windows, and already-waited images are released on errors. Exit requests have a
finite stop-event watchdog. Normal frames do not call `glReadPixels`, `glFinish`
or wait for GPU queries.

Bindings prefer `/interaction_profiles/valve/frame_controller_valve` when its
extension is advertised, with Index and Touch fallbacks. Inputs feed the
existing pad/analog adapter; there is no keyboard injection.

| Frame control | Existing game input |
|---|---|
| Left stick / D-pad | Steering and menu navigation |
| Right trigger / left trigger | Analog throttle / brake |
| A / B / X | Genesis B / C / A, matching Enhanced's existing positional map |
| Y / left bumper / right bumper | Genesis Z / X / Y camera views |
| Right Menu | Start/pause |
| Left View short release | Mode/back |
| Left View hold 1 second | Recenter at the next stereo frame boundary |

The View state machine is vendored unchanged from
[`astromaddie/sfvr` at 9932388f](https://github.com/astromaddie/sfvr/tree/9932388f788ccbd71154ad3a766ac690b5bea669),
under `licenses/sfvr-MIT.txt`. There are no mode changes on stick flicks.
Custom VR settings UI and new cockpit interaction are outside this milestone;
existing source settings and documented environment keys remain available.

## Diagnostics and evidence

Host `--vr-diagnostic` runs real game inputs at an explicitly synthetic display
rate and renders both production eye callbacks every display tick into a
256-square FBO, then records 768-square yaw 0/90/180 eye pairs and a HUD image.
It is a geometry/state test, not a performance measurement. Host diagnostic
IPD is explicitly synthetic 64 mm. The identity check independently rebuilds
camera xyz from the existing flat exact projection and searches for those
corners in the unprojected mesh; near-clipped intersections are excluded.

On the Frame, `VRDE_VR_FORCE_RENDER=1` is accepted only alongside a finite
`VRDE_VR_AUTO_EXIT`. It synthesizes the diagnostic head pose and uses valid
runtime IPD when available, with a labelled synthetic 64 mm fallback. In
standby it can render diagnostic textures but still submits zero compositor
layers. `VRDE_VR_DIAG_YAW`, `VRDE_VR_DUMP_FRAME` and
`VRDE_VR_FREEZE_FRAME` select pose, one-shot capture and a fixed emulated frame
for matched timing. The dump readback is confined to that explicit diagnostic
frame. Normal launch arms none of these settings. The launcher's
`vr-diagnostic.once` route consumes the request before starting.

Every ten seconds `[vr-perf]` reports windowed display/emulation rate, submitted
and diagnostic-rendered frame counts, CPU wait/prepare/eye/acquire/release times,
and available asynchronous per-eye GPU elapsed-query results when
`VRDE_VR_TIMING_GPU=1`. Queries are off in normal play; pending queries are
skipped, never waited on. Exclude startup/warm-up and the explicit capture
window when comparing fixed-scene runs. Submitted frames and offscreen forced
renders are separate counters; standby rendering is not compositor acceptance.

`VRDE_VR_TIMING=1` records up to 32,768 per-frame CPU timing rows in memory,
then writes `vr-timing.csv` in the working directory on exit. It separates
emulation, audio queueing, frame uploads, mesh preparation, vertex uploads, HUD,
eye acquisition/draw/release, query polling and frame submission. GPU results
are attributed to the frame that issued them. Startup logs pair the local
performance clock with real time for external correlation. The recorder performs
no per-frame file writes or readbacks. Its `start_s` intervals also reveal work
between frames that falls outside the individual stage spans.

For a finite timing replay, `VRDE_VR_REPLAY=1` alongside `VRDE_VR_AUTO_EXIT`
uses the supplied chronological `-p` inputs with ordinary settings, audio and
rendering. Unlike parity diagnostics, it does not hash/log every emulation tick
or substitute diagnostic graphics settings. Use `VRDE_VR_FORCE_RENDER=1` only
when an explicitly labelled standby workload is needed. Compare matching
simulation ranges and tick/non-tick frames separately; a 120 Hz average can
conceal emulation ticks that exceed the display period.

The wide sky panorama decodes each eight-pixel tile span once, then copies
repeated horizontal plane periods. It still reads the current VRAM, palette and
scroll at every emulated frame start; there is no stale-frame cache. `make
skytest` compares every output pixel and panorama metadata with the original
per-pixel formula across plane sizes, scroll boundaries, wraps, flips and palettes.

On heterogeneous Linux CPUs, the XR main thread uses the highest-capacity CPUs
within its original allowed mask while running the synchronous emulator and HLE
mesh preparation. Capacity and allowed-mask discovery happen once at XR startup.
Each scope restores the original mask before SDL audio, GL or OpenXR calls, so
existing workers and lazily created driver/runtime threads keep their own normal
placement. Missing topology, homogeneous CPUs or a failed narrowing retain normal
scheduling; a failed restoration stops the frame before external work. This changes
neither global scheduling nor priorities/clocks. `VRDE_VR_CPU_AFFINITY=0` disables
the policy for matched controls; `make vrcputest` checks selection, permitted-mask
limits, ties, opt-out, fallback and restoration. Flat and host geometry diagnostics
do not initialize this XR-only policy.

VR's SH2 HUD observer filters irrelevant accesses before the interpreter's
indirect callback. Only master HUD-list reads and framebuffer writes reach the
unchanged HUD consumer. Memory accesses, instruction timing and emulated state
still execute in full. Flat rendering and trace tools retain complete observer
delivery. `VRDE_VR_HUD_FILTER=0` restores full delivery for a same-binary
comparison; `make vrhudtest` checks the actual dispatch against HUD state across
buffer changes, cached aliases, list entries and transparent writes.

Verified host results for the checked-in Big Forest input script:

- The flat trace reproduces all seven fields of all 8,000 retained pre-stereo
  frame hashes exactly (screen, audio, 68K RAM, VDP, SH2 SDRAM, framebuffers,
  32X registers).
- At 72, 90 and 120 synthetic display Hz, all 1,801 simulation frames match all
  seven fields of a sample-rate-matched 48 kHz reference, including audio. Both
  eye callbacks run per display tick: 2,164 / 2,705 / 3,607 ticks respectively.
- Flat identity geometry: 1,629 corners at 72/90 Hz and 1,625 at 120 Hz, zero
  missing corners; maximum component error 0.000122 world units. The different
  final sub-tick fractions account for the small corner-count difference.
- The sampled mesh contains 21,642 vertices with no capacity overflow. Captures
  show front, side building/track and rear start-line scenery, plus the separate
  original HUD. Runtime controller actuation and optical comfort require direct
  interaction; these host captures establish geometry and production routing.

The retained 44.1 kHz trace was unsuitable for comparing the game's 48 kHz audio.
The matched reference uses `git show 25a0988:src/vrdetrace.c`, with only
`core_init(rom_path, 44100)` changed to `48000`, compiled against the unchanged
flat core/HLE objects. It runs `-n 8000` with `tools/race_inputs.txt`; the newly
built ordinary 44.1 kHz trace independently matches the retained original.
The SHA-256 identity of reference source and results is recorded in the compact
host report under `build/vr-evidence/verification.json`.

The initial production flat GPU path also reproduces its retained baseline:
0/1,801 core-composite mismatches and 0/539 HLE-composite mismatches. Its known
pre-existing Plane B seam at frame 1265 and raster comparison differences are
separate from the stereo geometry check.

The Steam Frame runtime check used SteamVR/OpenXR 2.18.2, API 1.0, two
2160-square three-image sRGB eye swapchains, a 640 × 448 HUD swapchain and a
title-specific 120 Hz preference. The final VR executable's SHA-256 is
`c42b2c865e9cc391c062fb6c3cefa83b95f68db621f64737ec414caa649b685c`.
A finite 75-second diagnostic advanced the real game continuously, without
`VRDE_VR_FREEZE_FRAME`, and rendered 8,708 stereo frames. After startup and
the explicit readback window, its final three ten-second windows measured:

| Measurement | Range |
|---|---|
| Render-loop cadence | 119.95–120.02 Hz |
| Original simulation cadence | 59.87–59.96 Hz |
| CPU scene preparation | 3.70–3.85 ms |
| OpenXR frame wait | 2.54–2.65 ms |
| GPU eye draw, left / right | 0.757–0.785 / 0.793–0.808 ms |

This measures forced texture rendering with the runtime in standby: only one
frame received compositor submission permission. It does not establish
sustained visible presentation, optical quality or interaction acceptance.
These timings describe the original single-sample eye renderer; they are not
measurements of the fixed 4× MSAA path.
An ordinary Steam launch with diagnostics absent also created the expected
swapchains, entered VISIBLE, froze simulation during standby, and shut down
through the normal window-close/session-stop path. All three controller
profiles were accepted by the runtime; physical input remains a separate gate.

Packaging includes a matching source archive and static-loader license. Build
input hashes cover native source, headers, Makefile and the SDK loader;
packaging rejects changed compilation inputs or binaries until rebuilt.
