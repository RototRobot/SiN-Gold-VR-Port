# SiN Gold VR: start here

This is the handoff for anyone joining the project, whether a person or an AI. It covers what exists,
why it is shaped the way it is, how to build and test it, what has already been learned the hard way,
and what comes next. The detailed evidence lives in the other files in `docs/`; this one is the map.

*Last updated 2026-09-16 (fourth pass).*

## 1. Where the project is

| Milestone | State |
|---|---|
| 1. Pass-through renderer, game DLL built from source, 32-bit OpenXR loader | ✅ Done, verified in unattended runs |
| 2. Virtual "theatre" screen in the headset | ✅ Done. The owner confirmed it in the headset on 2026-09-15: it loads, doesn't crash, and the colours are right. It is low resolution, as expected. |
| 3. Stereo rendering with head tracking | ✅ Built and verified at the desk on 2026-09-16: 406 projection-layer frames in one run, correct per-eye poses and fields of view, ~87 frames/s at 2444×2392 per eye. Not yet seen in the headset by a person. How it works: [§9](#9-how-stereo-works) |
| 4. HUD, crosshair and console in VR | ✅ Built 2026-09-16. The 2D goes into a panel `vr_hud_fov` degrees wide, `vr_hud_depth` metres away, centred on each eye's own axis - which is also what stops the crosshair from doubling. [§9](#9-how-stereo-works) |
| 5. Motion controller input | ✅ Built 2026-09-16: buttons run console commands, the sticks arrive as a joystick the engine polls, and in menus both turn into keystrokes. Measured at the desk. [§11](#11-controller-input) |
| 6. Tracked hands | **Next.** The poses are already located; getting them to the game DLL is the work. [§12](#12-next-tracked-hands) |
| 7–9 | Roomscale comfort, Wages of Sin, and packaging (see the roadmap in the [README](../README.md)) |

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
| `vr_hud_fov` | 60 | Width of the HUD panel, in degrees (archived) |
| `vr_hud_depth` | 6 | How far away the HUD panel appears, in metres; 0 is infinity (archived) |
| `vr_recenter` | 0 | Set to 1 to measure the head afresh from where the player is now; clears itself |
| `vr_input` | 1 | Take input from the controllers (archived) |
| `vr_deadzone` | 0.15 | Stick deadzone, applied to both axes together (archived) |
| `vr_bind_*` | see §11 | The console command each button runs, or `key:<name>` for a keystroke (archived) |
| `vr_recentre_double` | 1 | Two clicks of the left stick to recentre; 0 for one (archived) |
| `vr_dump_eye` | 0 | Set to 1 to write the next frame's eye images beside `sin.exe` as `sinvr_eye0/1.tga` |
| `vr_fake_axes` | | Desk testing: "`<forward> <side> <turn>`" pretends the sticks are held there |
| `vr_fake_press` | | Desk testing: the name of a button to press for a few frames, e.g. `jump` |
| `vr_fake_key` | | Desk testing: a key to tap - `up`, `down`, `left`, `right`, `enter`, `escape` |
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

- A good run takes about 15 s. The script posts a space keypress to the game window until the log shows
  the world being drawn, because a level starts on SiN's "PRESS ANY KEY TO CONTINUE" card and the engine
  runs no commands while it is up (§8). Without that, runs hang until some stray keypress arrives - which
  looks exactly like a load stall.
- `-Commands "<console commands>"` runs once the level is up, semicolon-separated as in the console.
  `vr_dump_eye 1` there writes both eye images for inspection; `paused 0` undoes the pause described below.
- `-Foreground` keeps taking the window back, which is needed for anything that looks at the rendered
  image: **SiN pauses itself whenever its window is not the active one** and paints the view over with an
  opaque blend and a PAUSE sign, so a background run renders no world at all.
- Beware that an **unknown console command is forwarded to the server and ends up as chat** ("Blade: foo"),
  so a typo in `-Commands` fails silently. `cl_paused` and `sv_paused` are not SiN cvars; `paused` is.
- `-KeepRunning` leaves the `quit` out of the cfg and lets the watchdog stop the game. Needed whenever
  the mod itself queues console commands - a controller button, say - because a `wait` holds up
  everything behind it in the command buffer, and the cfg is full of them (§8).
- **Input can be tested without controllers**: `vr_fake_axes "0.9 0 0"` holds the sticks somewhere, and
  `vr_fake_press jump` presses a button for a few frames, both through the real path. `viewpos` prints the
  player's position and yaw, which is how the axes were checked.
- For input, load a **deathmatch map** (`-Map sindm6 -ExtraArgs "+set deathmatch 1"`): the single-player
  levels open on a cinematic that holds the player still, and a player who is still falling from a spawn
  point has almost no control in the air.
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

**Eye images.** `vr_dump_eye 1` writes `sinvr_eye0.tga` and `sinvr_eye1.tga` (2444x2392, 17 MB each)
beside `sin.exe`, straight out of the eye framebuffers. This is the only way to see at the desk what the
headset would show: the window mirror only carries the left eye, and the engine's own `screenshot` reads
the window's back buffer, which in stereo holds nothing but that mirror.

Measuring them beats looking at them. The HUD panel's position is worth checking numerically, for
instance - the bounding box of the non-black pixels should match the panel computed from the eye
tangents, and the two eyes' panels should differ by the asymmetry of their frustums plus a few pixels of
convergence:

```
python -c "from PIL import Image; im = Image.open('sinvr_eye0.tga').convert('L'); print(im.point(lambda v: 255 if v > 6 else 0).getbbox())"
```

**`xrprobe`** checks the OpenXR path without the game: loader, runtime, headset, GL requirements.

## 6. Code map

| File | Role |
|---|---|
| `src/ref_vr/ref_vr.c` | `GetRefAPI`, loading ref_gl, the 36 recording thunks, C wrappers for Init/Shutdown/BeginFrame/RenderFrame/EndFrame, the frame tracer and cvars |
| `src/ref_vr/refapi.h` | The renderer interface reconstructed from the binaries, with compile-time checks of sizes and offsets |
| `src/ref_vr/vr_xr.c` | OpenXR: instance, session on ref_gl's GL context, event loop, the theatre-screen quad layer, and the per-eye swapchains and projection layer |
| `src/ref_vr/vr_gl.c` | Redirects ref_gl's `glFrustum`, `glOrtho`, `glViewport` and `glScissor` while an eye is bound, by hooking its `GetProcAddress` import. Holds the HUD panel geometry |
| `src/ref_vr/vr_view.c` | Turns an OpenXR eye pose into the engine's `refdef_t` (axes, scale, angles, field of view) and back |
| `src/ref_vr/vr_input.c` | OpenXR actions and bindings, and the mapping from them to console commands and stick axes |
| `src/ref_vr/vr_joy.c` | The joystick that only exists in this process, which is how the sticks reach the engine |
| `src/ref_vr/vr_keys.c` | Keystrokes posted to the engine's window, for the menus and the console |
| `src/ref_vr/vr_hook.c` | Patching another module's imports, shared by the GL and joystick hooks |
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

**Testing**

- **A `wait` in the command buffer holds up everything behind it,** including commands the mod queues
  itself. A controller button appends `+attack` the way a key bind does, so with a `wait`-heavy cfg
  running it will not be seen until the cfg drains - or never, if the cfg ends in `quit`. It cost an
  afternoon of "the button does nothing": `desk-test.ps1 -KeepRunning` is the answer.
- **Archived cvars keep whatever a test left in them**, in `sinvr\players\blade\config.cfg`. A test that
  rebinds `vr_bind_jump` to an `echo` and then quits cleanly leaves it that way for the next run.
- **A level starts on a "PRESS ANY KEY TO CONTINUE" card** - Blade against a circuit-board background -
  drawn by `sin.exe` after the last precache line (`sky`). While it is up the engine renders nothing new
  and **executes no console commands**, so a `+exec` script stops dead there and the main thread spins at
  100% of one core. It looks exactly like a load stall, and it cost hours: I measured it against the stock
  game (7.4 s, 6.8 s, 42.4 s versus 8.0 s, 11.5 s, 9.0 s through the mod), concluded SiN was slow to load,
  and wrote that down - when in truth I was timing how long until some stray keypress reached the window.
  What settled it: the owner saw the card on screen and asked whether to press a key, and a full-screen
  capture taken while the game was "stalled" showed it. `desk-test.ps1` now posts a HOME keypress to the
  window every second (unbound in the stock config, and `PostMessage` needs no window focus), and a whole
  VR run takes 13 s.
  - The lesson: when an unattended run stops making progress, **look at the screen** before theorising.

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

   Poses are measured from a **recentred head pose**, not from the tracking space's origin, which is
   wherever the headset happened to be when the session started - usually lying on a desk, pointing
   somewhere arbitrary. Without this the view is displaced by a metre or so and turned away from the
   direction the body moves in. The recentre happens by itself when the session starts, when the headset is
   put on (`FOCUSED`) and when the runtime moves the tracking space, and on demand with `vr_recenter 1`.
5. **Submit.** `EndFrame` ends the pass-1 eye and submits one `XrCompositionLayerProjection` carrying the
   same poses and fields of view the eyes were rendered with. The left eye is blitted to the window first,
   cropped to its shape, so the monitor and screenshots show something. Frames with no world in them -
   menus, loading screens, the console over nothing - are not stereo frames and keep going to the theatre
   screen, which is why a run reports both counts.
6. **The 2D goes into a panel.** ref_gl draws the HUD, crosshair, console and menus in window
   coordinates with an ortho projection. Stretching that over the whole eye put the HUD at the edge of
   vision and, because the eye frustums are asymmetric, put the crosshair at a *different angle* in each
   eye - seen as two crosshairs. So `glOrtho` is hooked as well: it marks the 2D phase, and while that
   phase is on, viewports and scissors are mapped into a panel `vr_hud_fov` degrees wide, in the window's
   shape, centred on **that eye's own axis** and shifted by the parallax of `vr_hud_depth` metres, which is
   what makes both eyes agree. ref_gl sets the viewport before the projection that says what it is for, so
   the last rect it asked for is applied again whenever the phase changes.
   - `DrawFadeScreen` - SiN's full-screen blend, `void (float r, float g, float b, float a)` - is wrapped
     so that it keeps the whole eye: damage flashes and the console's dimming should cover the view, not
     the panel.
7. **Comfort.** View bob and roll move the camera without the head moving, so `bob_up`, `bob_pitch`,
   `bob_roll`, `run_pitch`, `run_roll` and `sv_rollangle` are zeroed once when stereo comes on.

What the desk runs proved: 406 stereo frames submitted, ~87 frames/s with both eyes at 2444×2392 on a
1080 Ti, eye tangents mirrored between the eyes (left `L-1.300 R0.886`, right `L-0.886 R1.300`), the two eye
positions 66 mm apart, and that separation surviving the conversion into game units (2.6 units at 39.37
units per metre). For the panel, the eye dumps put the 2D exactly where the arithmetic says: eye 0's panel
at x 814-2105 of 2444 and eye 1's at 339-1630, each centred on its own axis, each shifted about 6 px
towards the other eye for the 6 m convergence, with the world still drawn across the whole buffer behind
it. The owner confirmed the stereo world in the headset on 2026-09-16, with the view decoupled from the
arms; the doubled crosshair and edge-flung HUD they reported are what the panel addresses.

Still to do, in the order it matters:

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
- The level-start card (§8) is worth handling in VR: it is a non-world frame, so it lands on the theatre
  screen while the world behind it is not being drawn.
- `refdef_t` beyond `rdflags` and `entity_t` are unmapped. They are needed to put the weapon in the hand (see
  [Q2VR_REFERENCE](Q2VR_REFERENCE.md), "weapon model in the hand").

**Input**

- Movement and aiming input need hooks in `sin.exe`'s client (`CL_BaseMove`, `cl.viewangles`), or joystick emulation.
- The head pose is already in the renderer. Getting it to the game DLL - for aiming, and for the body to
  follow the head - needs a channel between the two DLLs (a shared memory block, or cvars for a first cut).

**Content and release**

- Wages of Sin: its game DLL builds, but how to deploy it next to the `2015` data is still undecided.
- The licence for the mod's own code (`src/`, `tools/`, `cmake/`) has not been chosen.

## 11. Controller input

Built 2026-09-16, and measured at the desk. The client that reads input is closed, so nothing here
touches it: the controllers arrive the way a player's own binds and joystick would.

1. **OpenXR actions** (`vr_input.c`). One action set, `gameplay`, created with the session and attached to
   it before the first frame - a session takes its action sets once only, so the pose actions for the
   hands are created now too, ahead of being used. Bindings are suggested for the Reverb G2
   (`/interaction_profiles/hp/mixed_reality_controller`, which needs `XR_EXT_hp_mixed_reality_controller`
   enabled at instance creation for the lettered buttons), plain WMR controllers, Oculus Touch, Valve
   Index, and the Khronos simple controller as a floor. A runtime refuses profiles it does not know, which
   is fine; it keeps the ones it does.
2. **Buttons run console commands.** `refimport_t` slot 5 is Quake II's `Cbuf_ExecuteText(exec_when, text)`
   - confirmed from the disassembly, a three-way switch on the first argument, and from where sin.exe
   fills the table (§ENGINE_NOTES). A press queues the command in `vr_bind_<name>`; a release queues the
   `-` form of a `+` command. That is exactly what the engine's own key binds do, so `+attack`, `+use`,
   `weapnext` and the rest work with no client hook at all.
3. **The sticks are a joystick** (`vr_joy.c`). SiN still carries Quake II's joystick support and imports
   `joyGetNumDevs`, `joyGetDevCapsA` and `joyGetPosEx` from `winmm`, so answering those three in this
   process presents a stick that does not exist: no driver, nothing installed. The engine then does the
   rest of the work itself - acceleration, `cl_forwardspeed`, turning. `vr_input_startup()` runs from the
   renderer's `Init`, which is just before the engine looks for a joystick, and sets `in_joystick 1`,
   `joy_advanced 1` and the axis mapping (x side, y forward, r turn) with sensitivity 1 and no threshold,
   so the values arrive as given. The deadzone is applied in `vr_input.c` instead, where both axes of a
   stick can be judged together.
   - Signs, measured rather than assumed: forward on the stick walks the way the player faces; `side`
     positive strafes right; `turn` positive turns right, which means *negating* it for the engine, whose
     yaw grows to the left.
4. **Focus.** `xrSyncActions` returns `XR_SESSION_NOT_FOCUSED` while the runtime is showing something of
   its own, and then everything held is released and the sticks centre. The same happens if a controller's
   action goes inactive while a button is held, or the session goes away.
5. **In menus the controllers become a keyboard** (`vr_keys.c`). SiN's menus, its loading screens and its
   console are driven by key events, not by console commands, so a key message is posted to the engine's
   own window - it arrives in the message loop exactly as the keyboard's would, scan code, extended bit
   and all. The sticks become the arrow keys (with a 400 ms wait and then a 150 ms repeat), the trigger
   Enter, anything else Escape, and the joystick is centred so the player does not walk while choosing a
   menu item. A binding of `key:<name>` does the same from any button, which is how the menu button
   works: **SiN has no `togglemenu` command** - the escape key is what opens and closes its menu, and
   `menu_main` only opens it.
6. **Which of the two the controllers are** comes from the engine: `cls.key_dest` at sin.exe+0x38AAA4,
   found from the `toggleconsole` handler. It is 0 while the game has the keyboard and non-zero for an
   interface - 5 for the menu on this build. The reading is guarded (inside the image, value 0-7) and
   dropped for good if it ever looks wrong, falling back to "no world was drawn last frame", which covers
   the main menu and the loading screens but not a menu opened during play. This was the only reliable
   signal: SiN draws its menu *over a world that is still being rendered*, through its own draw calls
   rather than `DrawPic`, so nothing in the renderer's own traffic gives it away.
7. **Clicking the left stick twice recentres** (`vr_recentre_double 0` for one click), which is the same
   thing `vr_recenter 1` does from the console.

What the desk runs showed, on `sindm6` with `deathmatch 1`:

- the engine prints `joystick detected` instead of `joystick not found -- no valid joysticks (a5)`;
- `vr_fake_axes "0.9 0 0"` walked the player from (432 -751) to (318 -638) at yaw 135, which is exactly
  along the facing direction;
- `vr_fake_axes "0 0 0.6"` turned the yaw from 135 to 80 and moved the player nowhere;
- `vr_fake_axes "0 0.9 0"` strafed to the right of the facing direction;
- a button bound to `echo` printed in the console, and with the default binding the log shows
  `input: jump pressed -> "+moveup"` and a release 125 ms later;
- the menu button's `key:escape` took `key_dest` from 0 to 5 and back again, the stick moved the
  selection round SiN's wheel menu (checked in the eye dumps: MAIN MENU to CREDITS, then to TRAINING),
  and a stick held hard over while the menu was up moved the player **not at all**;
- one click of the left stick did nothing and a second 328 ms later logged
  `VR: measuring from where the headset is now`.

## 12. Next: tracked hands

The owner's verdict after trying the HUD panel (2026-09-16): the HUD and crosshair read correctly now,
but the crosshair follows the headset rather than where the gun points - which is the next thing, since
aiming has to come from the hand.

The hand poses are already located in the same recentred space as the eyes (`ACT_HAND_LEFT`,
`ACT_HAND_RIGHT` exist and are bound to `/input/aim/pose`); what is missing is a way to get them into the
game, and a weapon that follows them.

1. **A channel between the two DLLs.** Both are ours, so a named shared-memory block written by `ref_vr`
   and read by `gamex86.dll` is the least invasive way: head and hand poses, in game units, plus the
   recentring, written once a frame.
2. **Aiming from the hand.** Game-side, `Player::GunPosition` and `GetGunOrientation` (in
   `game/src/code/game/player.cpp`) decide where shots come from and go. Overriding them with the right
   hand's pose gives real VR aiming, and the crosshair then belongs on the weapon's own axis rather than
   the head's - which is also when the HUD crosshair should stop being drawn in the panel.
3. **The weapon in the hand.** The view model is drawn by the engine from `refdef_t`; the size the owner
   noticed ("the gun arm looked too big") is best fixed by attaching the weapon to the hand at a chosen
   scale rather than by changing projections. `refdef_t` past `rdflags` and `entity_t` still need mapping.
4. **Then comfort:** snap turning, crouching, and the body following the head.

Hand tracking (`XR_EXT_hand_tracking`) comes after that: the same channel, a different source.
