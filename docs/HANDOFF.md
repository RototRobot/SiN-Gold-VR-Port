# SiN Gold VR: start here

This is the handoff for anyone joining the project, whether a person or an AI. It covers what exists,
why it is shaped the way it is, how to build and test it, what has already been learned the hard way,
and what comes next. The detailed evidence lives in the other files in `docs/`; this one is the map.

*Last updated 2026-09-16.*

## 1. Where the project is

| Milestone | State |
|---|---|
| 1. Pass-through renderer, game DLL built from source, 32-bit OpenXR loader | ✅ Done, verified in unattended runs |
| 2. Virtual "theatre" screen in the headset | ✅ Done. The owner confirmed it in the headset on 2026-09-15: it loads, doesn't crash, and the colours are right. It is low resolution, as expected. |
| 3. Stereo rendering with head tracking | ✅ Built and verified at the desk on 2026-09-16: 406 projection-layer frames in one run, correct per-eye poses and fields of view, ~87 frames/s at 2444×2392 per eye. Not yet seen in the headset by a person. How it works: [§9](#9-how-stereo-works) |
| 4. HUD and menus in VR | **Next.** The 2D overlay currently lands inside each eye image, stretched over the whole field of view |
| 5–9 | Controllers, locomotion, weapons in the hand, Wages of Sin, and packaging (see the roadmap in the [README](../README.md)) |

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
| `vr_stereo` | 1 | Render the world once per eye (archived). With 0 everything stays on the theatre screen |
| `vr_worldscale` | 39.37 | Game units per metre (archived) |
| `vr_screen_distance` | 2.5 | Theatre screen distance, in metres (archived) |
| `vr_screen_width` | 2.4 | Theatre screen width, in metres (archived) |
| `vr_force_render` | 0 | Draw for the headset even while the runtime says it is idle. Needed for unattended VR runs |
| `vr_trace` | 1 | Log the renderer calls of each new kind of frame |

There are two logs:

- **`sinvr.log`** (beside `sin.exe`) comes from the renderer. It holds the export and import tables, per-frame call traces,
  and the VR start-up, session states and frame counts.
- **`sinvr\qconsole.log`** is the engine console. Lines from the renderer carry the prefix `[sinvr]`.

## 5. Testing at the desk

Headset time is scarce: the owner does those tests. So prove everything you can at the desk first, then hand
over a launcher with a short list of what to look for.

**Unattended runs.** `tools\desk-test.ps1` does the whole thing: it writes a cfg that loads a map, waits,
takes two screenshots and quits, runs the game with a watchdog, then collects the logs and screenshots and
converts the screenshots to PNG.

```
powershell -ExecutionPolicy Bypass -File tools\desk-test.ps1 -OutDir %TEMP%\sinvr-flat
powershell -ExecutionPolicy Bypass -File tools\desk-test.ps1 -Vr -OutDir %TEMP%\sinvr-vr
```

`-Map`, `-WaitFrames`, `-TimeoutSeconds` and `-ExtraArgs` (`"+set r_fullbright 1"` makes a dark map legible)
are parameters. What to expect:

- A good run takes 20-40 s. **Loading a map stalls for anywhere between 8 s and over four minutes**, one core
  at 100%, before the first world frame. The stock game does it too (§8), so expect to repeat runs.
- A VR run adds about 12 s while SteamVR starts, and SteamVR stays running afterwards.
- `-Vr` passes `vr_force_render 1`. Without it the runtime reports `shouldRender = false` for a headset lying
  on the desk and nothing at all is drawn for the headset.
- Screenshots land in `sinvr\players\blade\scrnshot\sinNNN.tga`, 24-bit bottom-up TGA, which the script
  converts to PNG. They come from the window, so a stereo run's screenshots show the left eye, cropped to the
  window's 4:3 shape out of the square-ish eye image.

**A healthy `sinvr.log`** has these pieces:

- the banner `SiN Gold VR renderer 0.1.0 over ref_gl.dll: ...`
- the export and import tables
- `frame N: BeginFrame ... EndFrame` lines, one per new frame shape
- `stats:` lines
- `Shutdown after N frames` with the count for each export

With VR on, it also shows these, in order:

1. the switched-off layers
2. `VR: SteamVR/OpenXR 2.17.10, headset: ...`
3. `session created`
4. `session IDLE` → `READY` → `SYNCHRONIZED`, and, once the headset is worn, → `VISIBLE` → `FOCUSED`
5. `theatre screen 640x480, format 0x8C43, 3 images`
6. `N frames shown on the theatre screen`
7. `VR: stereo on; view bob and roll off`
8. at the first world frame, `VR: stereo eye buffers 2444x2392` and two `eye 0:` / `eye 1:` lines giving the
   tangents, the headset-space position in metres and the resulting Quake view
9. `VR: 1 frames rendered in stereo`, then every 5400 frames
10. `VR: shutting down after N stereo and M theatre frames` — menus and loading screens are the theatre ones

A stereo world frame's trace line has **two** `BeginFrame ... RenderFrame` groups and one `EndFrame`, with
`camera_separation` −0.0000 then 0.0000 (both zero: the eye offsets come from the headset, not the engine).

**`xrprobe`** checks the OpenXR path without the game: loader, runtime, headset, GL requirements.

## 6. Code map

| File | Role |
|---|---|
| `src/ref_vr/ref_vr.c` | `GetRefAPI`, loading ref_gl, the 36 recording thunks, C wrappers for Init/Shutdown/BeginFrame/RenderFrame/EndFrame, the frame tracer and cvars |
| `src/ref_vr/refapi.h` | The renderer interface reconstructed from the binaries, with compile-time checks of sizes and offsets |
| `src/ref_vr/vr_xr.c` | OpenXR: instance, session on ref_gl's GL context, event loop, the theatre-screen quad layer, and the per-eye swapchains and projection layer |
| `src/ref_vr/vr_gl.c` | Redirects ref_gl's `glFrustum`, `glViewport` and `glScissor` while an eye is bound, by hooking its `GetProcAddress` import |
| `src/ref_vr/vr_view.c` | Turns an OpenXR eye pose into the engine's `refdef_t` (axes, scale, angles, field of view) and back |
| `src/ref_vr/xr_layers.c` | Implicit-layer hygiene for 32-bit processes (shared with xrprobe) |
| `src/ref_vr/vrlog.c` | `sinvr.log` |
| `game/` | Night Dive's source (SiN in `src/code/game`, WoS in `src/code/game2015`, shared `elib`) plus `game/CMakeLists.txt`. `git diff 07ae69c -- game` shows everything we changed there; today that is one line in `g_main.cpp`. |
| `cmake/openxr.cmake` | Builds the loader from the sibling SDK, or fetches `release-1.1.63` |
| `tools/deploy.ps1`, `tools/desk-test.ps1`, `tools/xrprobe/` | Install/uninstall, an unattended run, and the OpenXR probe |
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
- **A headset nobody is wearing gets `XrFrameState.shouldRender = false`,** and the session stops at
  `SYNCHRONIZED` instead of reaching `VISIBLE`. Skipping the render then means an unattended run proves
  nothing, which cost a whole test round: `vr_force_render 1` draws and submits anyway.
- **VIVE's implicit OpenXR layers are 64-bit only** but registered for 32-bit, which makes `xrCreateInstance` fail with −32.
  `xr_layers.c` handles this.

**Building**

- **MSBuild under `%TEMP%\claude\...` exceeds the 260-character path limit.** Build in short paths.
- **With `WIN32_LEAN_AND_MEAN`, include `<unknwn.h>` before `openxr_platform.h`.**
- **In Windows PowerShell 5.1, `$PSScriptRoot` is empty inside `param()` default values.** Compute paths in the script body.

**The engine's own slowness**

- **Loading a map stalls before the first world frame,** one core at 100%, for 8 s to over four minutes
  from the same command line. Measured on the stock game (`vid_ref gl`, no mod) as well: three passes of
  `map bank` took 7.4 s, 6.8 s and 42.4 s, against 8.0 s, 11.5 s and 9.0 s through the mod. **It is SiN's
  own behaviour, not ours.** The main thread is burning CPU throughout (sampled with
  `Get-Process sin | % Threads`), so it is a computation, not a deadlock, and the renderer is not involved:
  the stall sits between the last loading-plaque frame and the first world frame.

**The development machine**

- **The command harness refuses any command containing both `Remove-Item` and a `C:\Program Files` path.** Move test artifacts
  out of the game folder instead of deleting them, or delete in a separate command.
- **Vsync must be off in VR.** `vr_xr.c` turns it off, because the desktop refresh rate would otherwise fight `xrWaitFrame`.

## 9. How stereo works

Built and verified at the desk on 2026-09-16. The pieces, in the order a frame goes through them:

1. **The engine's own two-pass loop drives it.** SiN kept Quake II's `cl_stereo` support: with `cl_stereo`
   set, `SCR_UpdateScreen` calls `BeginFrame(-sep/2)`, draws, calls `BeginFrame(+sep/2)`, draws again, and
   ends with a single `EndFrame`. `ref_vr` sets `cl_stereo 1` and `cl_stereo_separation 0` once the headset
   is running (`update_stereo()`), so the engine renders the world twice and adds no offset of its own; the
   eye offsets come from the headset. `cl_stereo` stays 0 *during* ref_gl's `Init`, or ref_gl asks for a
   quad-buffer pixel format, fails to get one, and switches `cl_stereo` off itself.
   With the separation at 0 its sign cannot name the eye, so the passes are counted instead: pass 0 is the
   left eye, pass 1 the right.
2. **Per-eye swapchains.** One `XrSwapchain` per eye at the runtime's recommended size (2444×2392 on this
   headset), each wrapped in our own FBO with a `DEPTH24_STENCIL8` renderbuffer. The eye target is bound
   *after* ref_gl's `BeginFrame`, whose `glDrawBuffer(GL_BACK)` needs the default framebuffer, and is
   cleared on bind because nothing else clears it.
3. **ref_gl's GL calls are redirected** while an eye is bound (`vr_gl.c`). ref_gl resolves GL entry points
   through `GetProcAddress`, so its import of `GetProcAddress` is patched before `Init` and three functions
   come back as ours: `glViewport` and `glScissor` scale window coordinates to the eye buffer, and
   `glFrustum` is replaced with the eye's own asymmetric frustum. Hooking `glFrustum` covers every call
   site at once, including the view weapon's second projection.
4. **The view comes from the headset** (`vr_view.c`). Pass 0 calls `xrWaitFrame`, `xrBeginFrame` and
   `xrLocateViews`; `RenderFrame` then puts the eye pose on top of the engine's `refdef_t` and restores it
   afterwards, because the engine may render the same refdef again:
   - axes OpenXR → Quake are `x = -z`, `y = -x`, `z = y`, scaled by `vr_worldscale` units per metre;
   - the head offset is rotated by the player's yaw before it is added to `vieworg`;
   - yaw is the engine's yaw plus the head's, while pitch and roll come from the head alone;
   - `fov_x` and `fov_y` are widened to enclose the eye frustum so ref_gl's culling keeps the edges.
5. **Submit.** `EndFrame` ends the pass-1 eye and submits one `XrCompositionLayerProjection` carrying the
   same poses and fields of view the eyes were rendered with. The left eye is blitted to the window first,
   cropped to its shape, so the monitor and screenshots show something. Frames with no world in them -
   menus, loading screens, the console over nothing - are not stereo frames and keep going to the theatre
   screen, which is why a run reports both counts.
6. **Comfort.** View bob and roll move the camera without the head moving, so `bob_up`, `bob_pitch`,
   `bob_roll`, `run_pitch`, `run_roll` and `sv_rollangle` are zeroed once when stereo comes on.

What one desk run proved: 406 stereo frames submitted, ~87 frames/s with both eyes at 2444×2392 on a
1080 Ti, eye tangents mirrored between the eyes (left `L-1.300 R0.886`, right `L-0.886 R1.300`), the two eye
positions 66 mm apart, and that separation surviving the conversion into game units (2.6 units at 39.37
units per metre).

Still to do, in the order it matters:

- **The HUD and the console** are drawn into each eye's image, so the glFrustum/viewport scaling stretches
  them across the whole field of view. They want their own head-locked quad layer, or a 2D pass into a
  smaller part of the eye image.
- **The body does not follow the head.** Pitch and yaw are the head's, but movement, turning and the
  player's collision position still come from the keyboard and mouse.
- **The view weapon** follows the engine's angles, not the head, and will move to the hand in milestone 7.
- **Screen-copy effects** (screenshots, and anything that reads the window) still assume window coordinates.

## 10. Open questions and backlog

**Resolution and rendering**

- The theatre screen uses the window's resolution (640×480 by default). A higher `gl_mode` would sharpen it.
  The stereo path does not care: it renders at the runtime's recommended eye size regardless of `gl_mode`.
- The eye buffers are the recommended 2444×2392. A `vr_resolution` scale would trade sharpness for frames
  on weaker cards, and is easy to add: the swapchains are created in one place.
- Loading a map stalls before the first frame, 8 s to over four minutes, in the stock game as well (§8).
  Worth chasing eventually, since it is a bad first impression: the game source is available, so the place
  to look is what the server does between the last precache (`sky`) and the first server frame.
- `refdef_t` beyond `rdflags` and `entity_t` are unmapped. They are needed to put the weapon in the hand (see
  [Q2VR_REFERENCE](Q2VR_REFERENCE.md), "weapon model in the hand").

**Input**

- Movement and aiming input need hooks in `sin.exe`'s client (`CL_BaseMove`, `cl.viewangles`), or joystick emulation.
- The head pose is already in the renderer. Getting it to the game DLL - for aiming, and for the body to
  follow the head - needs a channel between the two DLLs (a shared memory block, or cvars for a first cut).

**Content and release**

- Wages of Sin: its game DLL builds, but how to deploy it next to the `2015` data is still undecided.
- The licence for the mod's own code (`src/`, `tools/`, `cmake/`) has not been chosen.
