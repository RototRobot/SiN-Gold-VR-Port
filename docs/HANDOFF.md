# SiN Gold VR: start here

This is the handoff for anyone joining the project, whether a person or an AI. It covers what exists,
why it is shaped the way it is, how to build and test it, what has already been learned the hard way,
and what comes next. The detailed evidence lives in the other files in `docs/`; this one is the map.

*Last updated 2026-09-15.*

## 1. Where the project is

| Milestone | State |
|---|---|
| 1. Pass-through renderer, game DLL built from source, 32-bit OpenXR loader | ✅ Done, verified in unattended runs |
| 2. Virtual "theatre" screen in the headset | ✅ Done. The owner confirmed it in the headset on 2026-09-15: it loads, doesn't crash, and the colours are right. It is low resolution, as expected. |
| 3. Stereo rendering with head tracking | **Next.** The plan is in [§9](#9-next-stereo-rendering-with-head-tracking) |
| 4–9 | HUD and menus, controllers, locomotion, weapons in the hand, Wages of Sin, and packaging (see the roadmap in the [README](../README.md)) |

## 2. How it fits together

```
sin.exe   closed: Night Dive's 2020 rebuild of SiN's Quake II-based engine, 32-bit
 |
 |-- vid_ref "vr" --> ref_vr.dll        ours (src/ref_vr)
 |                     |-- loads the stock ref_gl.dll (closed) and passes every call through
 |                     '-- OpenXR: openxr_loader.dll (delay-loaded) --> SteamVR, 32-bit runtime
 |
 '-- game "sinvr" --> sinvr\gamex86.dll ours: Night Dive's source in game/, plus our changes
```

The decisions behind that shape:

| Decision | Why |
|---|---|
| **The engine loads our renderer by name** (`+set vid_ref vr`) instead of us hooking `sin.exe` | `sin.exe` loads `ref_<vid_ref>.dll`, so nothing that ships is replaced and nothing is patched. If `ref_vr.dll` is missing, the engine falls back to `ref_gl.dll` on its own. |
| **The game DLL is built from source into a mod folder** (`+set game sinvr`) | Night Dive released this code, so gameplay changes such as aiming and comfort go here. Configs and saves stay in `sinvr\players\blade\`, apart from the player's normal ones. |
| **OpenXR rather than OpenVR** | It is the current API, it matches the Quake II PCVR reference, and SteamVR registers a 32-bit OpenXR runtime. |
| **32-bit and MSVC only** | `sin.exe` is 32-bit, and the game DLL must match its ABI. |
| **`ref_vr` forwards all 36 renderer exports through thunks** | The pass-through is exact by construction. The thunks also record every call, which shows how the engine drives the renderer without our needing every signature. |
| **`openxr_loader.dll` is delay-loaded** | If the loader is missing, VR is lost but the game still runs. |
| **Unloadable implicit OpenXR layers are switched off in-process** | Otherwise VIVE's 64-bit-only layers break every 32-bit OpenXR app ([OPENXR_NOTES](OPENXR_NOTES.md)). |

## 3. Where everything is

These paths are on the development machine. The repository is `SiN-Gold-VR-Port\` inside the project folder
`C:\Users\<you>\Desktop\Sin Gold VR project files\`.

| Path | What it is |
|---|---|
| `SiN-Gold-VR-Port\` | This repository |
| `OpenXR-SDK-Source-release-1.1.63\` | Khronos OpenXR source, beside the repo. The build uses it if present. |
| `sin-ex-game-master\` | Night Dive's original game source download, kept for comparison |
| `Quake-II-PCVR-1.0\` | GameOrDie007's GPL port of Team Beef's Quake2Quest. **Reference only.** See [Q2VR_REFERENCE](Q2VR_REFERENCE.md). |
| `FEAR2VR-master\` | A third-party VR mod for F.E.A.R. 2 (D3D9 with DXVK). Its `AGENTS.md` and `FRAME_LOOP.md` are useful reading. |
| `openvr-master\`, `dxvk*\` | Not used. We chose OpenXR, and SiN renders with OpenGL rather than D3D. |
| `C:\Program Files (x86)\Steam\steamapps\common\SiN 1\` | The game (Steam app 1313). Wages of Sin runs with `+set game 2015`. |

The development machine is a GTX 1080 Ti on NVIDIA driver 582.66, with a 4K monitor. The headset is an HP Reverb G2 through
SteamVR 2.17.9 and the WMR "Oasis" driver. The toolchain is VS 2022 Build Tools 17.14 (MSVC 14.44, Windows SDK 10.0.26100),
the CMake 3.31 bundled with VS, Python 3.12 and git 2.55.

## 4. Build, install, run

```
cmake --preset win32              # once; creates build\win32
cmake --build --preset release    # ~30 s from clean, seconds when incremental
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1      # add -GameDir <dir> or -Uninstall as needed
```

CMake is not on PATH on the development machine. It is at
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.

The build writes to `build\win32\stage\Release\`, laid out like the game folder. It holds `ref_vr.dll` and `openxr_loader.dll`,
plus `sinvr\gamex86.dll`, `wos\gamex86.dll` and `tools\xrprobe.exe`, all with PDBs.

`deploy.ps1` copies the files, writes two launchers beside `sin.exe`, and records everything it wrote in
`sinvr\installed-files.txt`. `-Uninstall` removes exactly what is listed there.

| Launcher | Command line |
|---|---|
| `SiN VR.bat` | `+set game sinvr +set vid_ref vr +set logfile 2 +set vr_enable 1` |
| `SiN VR (flatscreen).bat` | the same, with `vr_enable 0`: a pure pass-through |

The mod adds these cvars:

| Cvar | Default | Meaning |
|---|---|---|
| `vr_enable` | 0 | Show the game in the headset |
| `vr_screen_distance` | 2.5 | Theatre screen distance, in metres (archived) |
| `vr_screen_width` | 2.4 | Theatre screen width, in metres (archived) |
| `vr_trace` | 1 | Log the renderer calls of each new kind of frame |

There are two logs:

- **`sinvr.log`** (beside `sin.exe`) comes from the renderer. It holds the export and import tables, per-frame call traces,
  and the VR start-up, session states and frame counts.
- **`sinvr\qconsole.log`** is the engine console. Lines from the renderer carry the prefix `[sinvr]`.

## 5. Testing at the desk

Headset time is scarce: the owner does those tests. So prove everything you can at the desk first, then hand
over a launcher with a short list of what to look for.

**Unattended runs.** Put a cfg in `sinvr\`, launch with `+exec` on it, and let it run:

```
map bank
wait        (x400: one wait is one engine frame)
screenshot
wait        (x400)
screenshot
wait        (x20)
quit
```

Launch it with
`sin.exe +set game sinvr +set vid_ref vr +set vr_enable 0|1 +set logfile 2 +exec <cfg>` and a watchdog. Here is what to expect:

- The flatscreen run takes about 40 s. Loading `bank` stalls a single frame for around 22 s.
- A VR run adds about 12 s while SteamVR starts, and SteamVR stays running afterwards.
- Screenshots land in `sinvr\players\blade\scrnshot\sinNNN.tga`. These are 24-bit, bottom-up TGA files; convert them to PNG to view.

**A healthy `sinvr.log`** has these pieces:

- the banner `SiN Gold VR renderer 0.1.0 over ref_gl.dll: ...`
- the export and import tables
- `frame N: BeginFrame ... EndFrame` lines, one per new frame shape
- `stats:` lines
- `Shutdown after N frames` with the count for each export

With VR on, it also shows these, in order:

1. the switched-off layers
2. `VR: SteamVR/OpenXR 2.17.9, headset: ...`
3. `session created`
4. IDLE → READY → SYNCHRONIZED → VISIBLE → FOCUSED
5. `virtual screen 640x480, format 0x8C43`
6. `N frames shown`

**`xrprobe`** checks the OpenXR path without the game: loader, runtime, headset, GL requirements.

## 6. Code map

| File | Role |
|---|---|
| `src/ref_vr/ref_vr.c` | `GetRefAPI`, loading ref_gl, the 36 recording thunks, C wrappers for Init/Shutdown/BeginFrame/RenderFrame/EndFrame, the frame tracer and cvars |
| `src/ref_vr/refapi.h` | The renderer interface reconstructed from the binaries, with compile-time checks of sizes and offsets |
| `src/ref_vr/vr_xr.c` | OpenXR: instance, session on ref_gl's GL context, event loop, and the theatre-screen swapchain and quad layer |
| `src/ref_vr/xr_layers.c` | Implicit-layer hygiene for 32-bit processes (shared with xrprobe) |
| `src/ref_vr/vrlog.c` | `sinvr.log` |
| `game/` | Night Dive's source (SiN in `src/code/game`, WoS in `src/code/game2015`, shared `elib`) plus `game/CMakeLists.txt`. `git diff 07ae69c -- game` shows everything we changed there; today that is one line in `g_main.cpp`. |
| `cmake/openxr.cmake` | Builds the loader from the sibling SDK, or fetches `release-1.1.63` |
| `tools/deploy.ps1`, `tools/xrprobe/` | Install/uninstall, and the OpenXR probe |
| `docs/ENGINE_NOTES.md` | How the engine works: the slot map, RVAs, `refdef_t`, the stereo path and runtime observations |
| `docs/OPENXR_NOTES.md` | The 32-bit OpenXR path: building the loader, probe results, the layer trap, the first session |
| `docs/Q2VR_REFERENCE.md` | What carries over from the Quake II VR ports, and what doesn't |

## 7. Rules of the road

The short version for AI tools is [AGENTS.md](../AGENTS.md).

- **Build 32-bit with MSVC only.**
- **Never change files that ship with the game.** Everything goes through `vid_ref vr` and `game sinvr`.
- **Keep changes to `game/` small**, and commit them separately from other work. The code is under Night Dive's licence (`game/license.txt`).
- **No GPL code from Quake2Quest or Quake II PCVR.** Ideas and measured values are fine; code is not.
- **Git stays local.** The owner uploads to GitHub by hand, so never add a remote or push. Keep commits small and building.
- **Record new findings** in `ENGINE_NOTES.md` or `OPENXR_NOTES.md`, together with how you established them.

## 8. Traps already hit (don't rediscover them)

**The engine and the renderer**

- **Only the video menu is affected by `vid_ref vr`.** Everywhere else `"vr"` behaves like `"gl"`. *Applying* video settings rewrites `vid_ref` to
  `gl`/`soft` for that session; the launchers set it back on the next start.
- **`gi.dprintf` in the game DLL prints only with `developer 1`.** Use `gi.printf` for anything that must show.
- **`DrawFadeScreen` (slot `+0x40`) runs after every world view.** It is probably SiN's full-screen blend, not a menu fade. Several slot names are
  still guesses ([ENGINE_NOTES](ENGINE_NOTES.md)).

**OpenXR**

- **SteamVR's 32-bit OpenXR runtime rejects API 1.1.** Request 1.0.
- **VIVE's implicit OpenXR layers are 64-bit only** but registered for 32-bit, which makes `xrCreateInstance` fail with −32.
  `xr_layers.c` handles this.

**Building**

- **MSBuild under `%TEMP%\claude\...` exceeds the 260-character path limit.** Build in short paths.
- **With `WIN32_LEAN_AND_MEAN`, include `<unknwn.h>` before `openxr_platform.h`.**
- **In Windows PowerShell 5.1, `$PSScriptRoot` is empty inside `param()` default values.** Compute paths in the script body.

**The development machine**

- **The command harness refuses any command containing both `Remove-Item` and a `C:\Program Files` path.** Move test artifacts
  out of the game folder instead of deleting them, or delete in a separate command.
- **Vsync must be off in VR.** `vr_xr.c` turns it off, because the desktop refresh rate would otherwise fight `xrWaitFrame`.

## 9. Next: stereo rendering with head tracking

This is the plan. None of it is built yet.

1. **Use the engine's own two-pass loop.** After ref_gl's Init returns, set `cl_stereo 1` and `cl_stereo_separation 0`.
   Keeping `cl_stereo` at 0 *during* Init stops ref_gl from asking for a quad-buffer pixel format that it would then
   reset. With the separation at 0 its sign cannot identify the eye, so count the BeginFrame calls within each frame instead:
   the first is the left eye, the second the right.
2. **Per-eye render targets.** Create one swapchain per eye at the recommended size (2444×2392 here), each with our own `DEPTH24_STENCIL8`
   buffer. Bind the eye target *after* ref_gl's BeginFrame, because its `glDrawBuffer(GL_BACK)` must hit the default framebuffer, and clear it on bind.
3. **Intercept GL calls.** Before Init, hook ref_gl's import of `GetProcAddress` so that its GL loader resolves `glFrustum`,
   `glViewport` and `glScissor` to our versions.
   - Viewport and scissor: scale from window coordinates to eye-buffer coordinates.
   - Frustum: replace it with the eye's asymmetric tangents. This must apply at every call site, including the view weapon's.
4. **Per-eye view.** At the first BeginFrame of a frame, call `xrWaitFrame`, `xrBeginFrame` and `xrLocateViews`.
   In RenderFrame, put the eye pose on top of the engine's view:
   - **Axes:** OpenXR to Quake is `x = −z`, `y = −x`, `z = y`.
   - **Scale:** about 39.4 units per metre.
   - **Angles:** yaw is the engine's yaw plus the head's yaw; pitch and roll come from the head alone.
   - **FOV:** widen `fov_x` and `fov_y` enough to enclose the eye frustum, so ref_gl's culling doesn't clip the edges.
   - **Restore** the refdef fields after the call.
5. **Submit.** End the frame with a projection layer that carries the *same* poses and FOVs used for rendering, and mirror the left eye to the
   desktop window, cropped. Frames that draw no world (menus, loading screens) keep using the theatre screen.
6. **Comfort, for now.** Turn off view bob and roll through the game's cvars.

Risks to watch:

- Screenshots and any screen-copy effects still assume window coordinates.
- The HUD will appear at infinite depth.
- The view weapon follows the mouse, not the head. Hands come in milestone 7.

## 10. Open questions and backlog

**Resolution and rendering**

- The theatre screen uses the window's resolution (640×480 by default). A higher `gl_mode` would sharpen it.
- Loading `bank` stalls for about 22 s. Compare this with a stock launch.
- `refdef_t` beyond `rdflags` and `entity_t` are unmapped. They are needed to put the weapon in the hand (see
  [Q2VR_REFERENCE](Q2VR_REFERENCE.md), "weapon model in the hand").

**Input**

- Movement and aiming input need hooks in `sin.exe`'s client (`CL_BaseMove`, `cl.viewangles`), or joystick emulation.

**Content and release**

- Wages of Sin: its game DLL builds, but how to deploy it next to the `2015` data is still undecided.
- The licence for the mod's own code (`src/`, `tools/`, `cmake/`) has not been chosen.
