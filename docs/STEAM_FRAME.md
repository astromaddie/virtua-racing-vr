# Steam Frame Linux ARM64

This branch provides a native OpenXR game path with independently projected
stereo geometry, tracked head poses and Frame controller actions. The original
menu/HUD artwork uses a separate compositor panel. The flat Enhanced build and
standalone ROM-free colour probe remain explicit entry points. See
[VR architecture](VR_ARCHITECTURE.md) for camera scale, controls and evidence.

## Build

The project Dockerfile supplies Ubuntu 22.04 ARM64 compiler, SDL2, OpenGL, X11,
zlib, CMake, and Python development tools. Build the image and mount the source
tree into it:

```sh
docker buildx build --platform linux/arm64 -t virtua-racing-vr-build .
docker run --rm --platform linux/arm64 \
  -v "$PWD:/work" -w /work virtua-racing-vr-build tools/build_frame.sh
docker run --rm --platform linux/arm64 \
  -v "$PWD:/work" -w /work virtua-racing-vr-build make -j2 build/vrdetrace test analogtest vrtest
```

`tools/build_frame.sh` refuses other host OS/architectures. It verifies the
OpenXR-SDK 1.1.63 source archive against its pinned SHA-256, builds its static
loader with tests and API layers disabled, and builds the ARM64 game and probe
with at most two compile jobs. `SDL2_CONFIG` can override the Makefile's
`sdl2-config` command; for example, `make SDL2_CONFIG='pkg-config sdl2'`.

## Launch and game data

Create a ROM-free package with `python3 tools/package_frame.py`. Its source
snapshot and allowlisted files are hashed in `SHA256SUMS`, and build metadata
records the source revision, dirty paths, and builder-side dynamic dependency
closure. The root `LICENSE`, PicoDrive and YM2413 notices, OpenXR-SDK license,
and adapted G-Diffuser probe license are included. The package contains no ROM
or save data.

The VR entry point is `./launch-vr.sh [RAW_US_ROM_PATH]`; the explicit flat entry
point is `./launch-flat.sh [RAW_US_ROM_PATH]`. Launch through Steam so the
OpenXR action bindings and native controller runtime are available. The ROM is
read in place and is never copied or extracted. The accepted raw US image is
3,145,728 bytes with MD5 `72b1ad0f949f68da7d0a6339ecd51a3f` and SHA-1
`18dfdeb50780c2623e60a6587d7ed701a1cf81f1`. The launcher runs the Python
standard-library preflight before starting the game. With no argument, it uses
`Virtua Racing Deluxe (USA).32x` in
`${XDG_DATA_HOME:-$HOME/.local/share}/VirtuaRacingVR`. It changes the working
directory there, so `vrde.ini` and saves persist outside a resettable runtime.

The independent probe entry point is `./launch-openxr-probe.sh [SECONDS]`.
It needs a SteamVR OpenXR runtime and an Xlib/GLX OpenGL 4.3 context. Its default
run is eight seconds; accepted durations are 1 to 120 seconds. It uses
`xrWaitFrame` for pacing, locates both views at `predictedDisplayTime`, and
ends standby frames with zero composition layers. It never forces a render.

## Runtime boundary

The package targets SteamLinuxRuntime_4-arm64 and dynamically uses platform
SDL2, OpenGL/GLX, X11, zlib, and system C/C++ libraries; OpenXR's loader is
static. Package creation rejects unresolved builder dependencies and records
the complete `ldd` closure. Python 3 is required by the ROM preflight launcher.
The recorded closure is builder evidence; final target-runtime launch is a
separate check. The probe's successful build is likewise distinct from
runtime eye-colour confirmation.

## Finite diagnostics

For a one-shot Steam diagnostic, put exactly `45 0 1801` in
`$XDG_DATA_HOME/VirtuaRacingVR/vr-diagnostic.once` (using the default XDG path
above when unset). The fields are seconds, synthetic head yaw in degrees and
the emulated capture frame. The launcher consumes the file, runs the checked
menu/start inputs and captures `vr-eye0.bmp`, `vr-eye1.bmp`, `vr-hud.bmp` in the
persistent data directory, then exits. Use yaw 90 or 180 for the other views.
The next normal launch has no diagnostic request. Diagnostics do not alter
system audio/mute settings.

For host geometry/cadence checks inside the Linux ARM64 builder, mount the
user-owned ROM read-only and run from an empty evidence directory:

```bash
read -ra script < /work/tools/race_inputs.txt
Xvfb :88 -screen 0 1024x768x24 > /tmp/vr-xvfb.log 2>&1 &
export DISPLAY=:88
# Wait for the X server before launching.
sleep 1
VRDE_VR_DIAG_HZ=90 /work/build/vrde-vr --vr-diagnostic "${script[@]}" /rom/game.32x
```

The diagnostic is finite (1,801 frames by default). `VRDE_VR_DIAG_FRAMES`
changes that count; `VRDE_VR_DIAG_HZ` selects 60–144 synthetic display Hz.
The final frame must be an active race for the identity/geometry gate to pass.
These headless captures do not measure target-runtime display performance.

The VR performance target is 2160 × 2160 per eye at 120 Hz. Set this title’s
SteamVR refresh preference to 120 Hz before launch; the default swapchain size
is 2160. Confirm an actual 8.333 ms period in the runtime log.
