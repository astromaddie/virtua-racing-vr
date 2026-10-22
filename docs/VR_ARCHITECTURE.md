# VR architecture: current source state

Target: native Linux ARM64 on Steam Frame, using SteamVR's OpenXR runtime. The
first graphics gate is the ROM-free `openxr-colour-probe` executable; it creates
an Xlib/GLX OpenGL 4.3 context, selects an `GL_SRGB8_ALPHA8` stereo swapchain,
and clears the two eyes to distinct dim colours. Its `xrWaitFrame` loop uses the
runtime's predicted display time, submits zero layers when `shouldRender` is
false, and continues polling through standby. This probe does not call game code.

The flat application remains the Enhanced build with its existing display,
input, settings, and menu behaviour. The game uses a hybrid render/emulation
path: PicoDrive runs the 68000, Z80, and SH2 emulation, while Enhanced's HLE
reconstructs the 32X race scene from SH2 scene data and draws it with OpenGL.
The SH2 raster path remains available for pixel comparisons. Source anchors are
[`src/hle.c`](../src/hle.c), [`src/hle.h`](../src/hle.h), and
[`src/sh2raster.c`](../src/sh2raster.c).

Enhanced computes a view transform and transforms scene vertices before
projection, but the renderer's polygon records contain screen-space vertex
coordinates and depth. The game-facing render list therefore no longer carries
the original 3D vertices and per-eye camera state that a stereo pass needs.
The rasterizer also preserves the SH2 path's facing/culling rules, and HUD
pixels are composed as screen-space layers. These are renderer integration
constraints for a later phase; this foundation adds no game stereo pass.

| Gate | Current evidence |
|---|---|
| Source/base | `PokaMilkTea/VirtuaRacingDeluxeEnhanced` at `9f059e10e4c8bff928a581777f19e80c3d117b92`; source and PicoDrive notices retained |
| Linux ARM64 flat build | Native `vrde` built from the existing plain-C PicoDrive source list; target execution and race/HLE traces recorded by the build validation |
| OpenXR colour probe | Standalone probe builds against pinned OpenXR-SDK 1.1.63 and statically links its loader; runtime eye output requires the Steam Frame/OpenXR graphics gate |
| Stereo game renderer | Not implemented; requires a new per-eye scene representation before projection, then an explicit HUD/layer treatment and frame-state parity checks |

The disassembly reference is [`matiaszanolli/sega-vr-disasm` at
`60819eff3cdf2abe0037081c9e6a484bf06bb9dc`](https://github.com/matiaszanolli/sega-vr-disasm/tree/60819eff3cdf2abe0037081c9e6a484bf06bb9dc).
It builds the game ROM and serves as a reference, while this port's executable,
HLE scene reconstruction, OpenGL renderer, emulator core, tests, and build system
come from [`PokaMilkTea/VirtuaRacingDeluxeEnhanced`](https://github.com/PokaMilkTea/VirtuaRacingDeluxeEnhanced/tree/9f059e10e4c8bff928a581777f19e80c3d117b92).
