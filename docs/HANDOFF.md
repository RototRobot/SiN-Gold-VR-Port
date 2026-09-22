# SiN Gold VR: start here

This is the handoff for anyone joining the project, whether a person or an AI. It covers what exists,
why it is shaped the way it is, how to build and test it, what has already been learned the hard way,
and what comes next. The detailed evidence lives in the other files in `docs/`; this one is the map.

*Last updated 2026-09-20 (seventh pass).*

## 1. Where the project is

| Milestone | State |
|---|---|
| 1. Pass-through renderer, game DLL built from source, 32-bit OpenXR loader | ✅ Done, verified in unattended runs |
| 2. Virtual "theatre" screen in the headset | ✅ Done. The owner confirmed it in the headset on 2026-09-15: it loads, doesn't crash, and the colours are right. It is low resolution, as expected. |
| 3. Stereo rendering with head tracking | ✅ Built and verified at the desk on 2026-09-16: 406 projection-layer frames in one run, correct per-eye poses and fields of view, ~87 frames/s at 2444×2392 per eye. Not yet seen in the headset by a person. How it works: [§9](#9-how-stereo-works) |
| 4. HUD, crosshair and console in VR | ✅ Built 2026-09-16. The 2D goes into a panel `vr_hud_fov` degrees wide, `vr_hud_depth` metres away, centred on each eye's own axis - which is also what stops the crosshair from doubling. [§9](#9-how-stereo-works) |
| 5. Motion controller input | ✅ Built 2026-09-16: buttons run console commands, the sticks arrive as a joystick the engine polls, and in menus both turn into keystrokes. Measured at the desk. [§11](#11-controller-input) |
| 6. Tracked hands | ✅ Built 2026-09-19, left-handed mode 2026-09-20. The weapon is drawn on the controller, at a per-weapon grip the player can set in the headset, and fires down its own barrel; sin.exe itself places it, so its muzzle flash, tracers and impacts come from there too. The fists are one arm per hand. `vr_left_handed 1` mirrors the lot into the other hand. Firing is the one part the desk cannot exercise. [§12](#12-tracked-hands) |
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
| `vr_aim` | 1 | Game module: shots come from the weapon in the hand |
| `vr_dump_eye` | 0 | Set to 1 to write the next frame's eye images beside `sin.exe` as `sinvr_eye0/1.tga`; later dumps are numbered |
| `vr_fake_axes` | | Desk testing: "`<forward> <side> <turn> [<turning stick up>]`" pretends the sticks are held there |
| `vr_fake_press` | | Desk testing: a button to press, e.g. `jump`; "`adjust 200`" holds it for 200 frames, "`adjust 0`" until `release` |
| `vr_fake_key` | | Desk testing: a key to tap - `up`, `down`, `left`, `right`, `enter`, `escape`, `space` |
| `vr_fake_hand`, `vr_fake_hand_left` | | Desk testing: "`<x> <y> <z> <pitch> <yaw> <roll>`" puts that hand there |
| `vr_fake_idle_press` | 0 | Desk testing: presses a button once a second while the engine is not drawing |
| `vr_weapon_hand` | 1 | Draw the view weapon in the right hand (archived) |
| `vr_grip_<weapon>` | | Where that weapon sits in the hand: offset, angles and size, written by the grip button (archived) |
| `vr_weapon_reset` | 0 | Set to 1 to forget the setting for the weapon in hand; clears itself |
| `vr_weapon_client` | 1 | 0 stops sin.exe's own calls being redirected, so the weapon is moved in RenderFrame instead |
| `vr_client_bullets` | 0 | 1 puts back the client's own bullet impacts, which start at the eye (archived) |
| `vr_crosshair` | 1 | SiN's crosshair follows the gun; 0 leaves it out, 2 leaves it in the middle of the view (archived) |
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
- **Input can be tested without controllers**, all through the real path:
  `vr_fake_axes "<forward> <side> <turn> [<turning stick up>]"` holds the sticks somewhere;
  `vr_fake_press jump` presses a button for a few frames, `vr_fake_press "adjust 200"` for 200 rendered
  frames, and `vr_fake_press "adjust 0"` until `vr_fake_press release` - and a `wait` is not a frame, so
  the 0.8 s the grip has to be held is nearer 400 `wait`s than 120; `vr_fake_hand "<x> <y> <z>
  <pitch> <yaw> <roll>"` and `vr_fake_hand_left` put the hands somewhere in the game's own units and
  senses. `viewpos` prints the player's position and yaw, which is how the axes were checked.
  `vr_fake_swing "<left|right> <x> <y> <z> <frames>"` swings a fake hand out by that much over that
  many rendered frames, speeding up and slowing down like an arm, and back three times as slowly - a
  punch at a speed the frame rate decides. SiN's eye is 66 units above the feet, so a fist aimed at the
  floor has to get most of the way there.
- **A cvar a test sets is still set in the next test.** SiN writes `config.cfg` on the way out, and
  every `vr_` cvar worth keeping is archived, so a run that ends with `vr_weapon_hand 0` leaves the next
  run with no weapon in the hand and no sign of why. Put the cvars a test depends on at the top of its
  own step list, and put back anything it changes.
- **A left-handed cvar is worse than most**, because `vr_left_handed` is read once at start-up: a run
  that archives it leaves every later run in the other hand, with the fake right hand tracking nothing
  and no weapon placed - which looks like the weapon code failing. Pass `+set vr_left_handed 0` (or 1)
  on the command line in every test that cares, and remember that SiN writes the player's `config.cfg`
  without being asked, not only at exit.
- **The HUD is drawn in the same colours a marker is.** Finding a drawn line by looking for pure red or
  pure green pixels also finds the health and armour bars, which sit in the same part of the picture; a
  centroid over the lot points somewhere between the two and looks like a bug in the drawing. Group the
  pixels into clusters and take the one shaped like what was drawn.
- **`vr_weapon_hand 0` does not take the weapon out of the picture**; it hands it back to SiN, which
  draws it in front of the camera. To photograph a view with no weapon in it, leave the weapon in the
  hand and move the hand behind the eyes (`vr_fake_hand "-80 0 -30 0 0 0"`).
- **The eye images are not the same size from one session to the next.** The runtime picks the size, and
  it moved by half a percent between two runs an hour apart, so anything comparing pixels between runs
  has to scale one to the other first. Within a run the two eyes match.
- **A cfg must fit in the command buffer.** `exec` puts the whole file into an 8 KB buffer, and a
  `wait`-heavy test cfg reaches that easily - whereupon *nothing* in it runs, not even the `map` on the
  first line, and the game sits on the menu looking like a hang. Split long ones into files that `exec`
  each other at the end; each is only read when its turn comes.
- **Rendered frames and `wait`s do not run at the same rate.** On this machine the command buffer gets
  through about 180 `wait`s a second while the headset path renders about 88 frames a second, and the
  ratio moves. Anything timed against a held button (the weapon adjustment, say) wants
  `vr_fake_press "<action> 0"` and an explicit `vr_fake_press release` rather than a count of `wait`s.
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
beside `sin.exe`, straight out of the eye framebuffers; later dumps in the same run are numbered
(`sinvr_eye0_1.tga`), so one run can compare several. This is the only way to see at the desk what the
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
| `src/ref_vr/vr_gl.c` | Redirects ref_gl's `glFrustum`, `glOrtho`, `glViewport`, `glScissor`, `glTranslatef`, `glRotatef` and `glPopMatrix` while an eye is bound, by hooking its `GetProcAddress` import. Holds the HUD panel geometry, the clip plane that halves a model, the mirror that turns a right-handed weapon round, and the world drawing this mod does of its own |
| `src/ref_vr/vr_view.c` | Turns an OpenXR eye pose into the engine's `refdef_t` (axes, scale, angles, field of view) and back |
| `src/ref_vr/vr_input.c` | OpenXR actions and bindings, and the mapping from them to console commands and stick axes |
| `src/ref_vr/vr_joy.c` | The joystick that only exists in this process, which is how the sticks reach the engine |
| `src/ref_vr/vr_keys.c` | Keystrokes posted to the engine's window, for the menus and the console |
| `src/ref_vr/vr_poses.c` | The head and hand poses the game module reads, and the export that hands them over |
| `src/ref_vr/vr_weapon.c` | The weapon in the hand: per-weapon grips and sizes, the adjustment in the headset, the two calls redirected inside sin.exe, the fists split between the hands, and which hand holds it |
| `src/ref_vr/vr_math.c` | Poses in Quake's terms: angles to axes and back, composing them, taking one relative to another, and reflecting one for the other hand |
| `src/shared/sinvr_poses.h` | The contract between the two DLLs: the block, its frame of reference |
| `src/game_vr/vr_link.cpp` | The game module's side of that link. Ours, built into the game DLL |
| `src/ref_vr/vr_hook.c` | Patching another module in this process: its imports (the GL and joystick hooks), and single direct calls inside sin.exe |
| `src/ref_vr/xr_layers.c` | Implicit-layer hygiene for 32-bit processes (shared with xrprobe) |
| `src/ref_vr/vrlog.c` | `sinvr.log` |
| `game/` | Night Dive's source (SiN in `src/code/game`, WoS in `src/code/game2015`, shared `elib`) plus `game/CMakeLists.txt`. `git diff 5236699 -- game` shows everything we changed there: VR aiming in `player.cpp`/`player.h`, the muzzle and shot direction in `weapon.cpp`, server-side bullet impacts in `bullet.cpp`, and a line in `g_main.cpp`. |
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
- **The weapon cannot be fired in an unattended run.** Neither a console `+attack` nor a key bound to it
  reaches the weapon code - the ammo count does not move and the client's own bullet trace never runs -
  while every other console command and posted key works. Real input fires; this is a limit of the
  harness. Anything about shooting has to be checked in the headset.
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
- The level-start card (§8) is worth handling in VR: it is a non-world frame, so it lands on the theatre
  screen while the world behind it is not being drawn.

**Input**

- The body does not follow the head yet. The head pose already reaches the game module
  (`VR_HeadPose`, §12), so walking in the room can become the player walking - the game moves the
  player by the head's change of position each frame, and recentres around it - without a new channel.
- A real crouch - the head going down past a threshold pressing crouch - is the rest of roadmap
  milestone 7, with the body following the head. Until then a turn pivots on the recentre point, not
  on the head (`vr_view_apply` turns the head's offset with the body's yaw), so a snap taken half a
  metre from it swings the view sideways too.

**Content and release**

- Wages of Sin: its game DLL builds, but how to deploy it next to the `2015` data is still undecided.
- The licence for the mod's own code (`src/`, `tools/`, `cmake/`) has not been chosen.

## 11. Controller input

Built 2026-09-16, and measured at the desk. The client that reads input is closed, so nothing here
touches it: the controllers arrive the way a player's own binds and joystick would.

1. **OpenXR actions** (`vr_input.c`). One action set, `gameplay`, created with the session and attached to
   it before the first frame - a session takes its action sets once only, so the pose actions for the
   hands are created now too, ahead of being used. The tables are written right-handed; left-handed,
   each path is rewritten to the other hand as it is suggested (`for_hand`), the poses excepted. Because
   a session takes its bindings once, `vr_left_handed` is read at start-up and a change asks for a
   `vid_restart` - the weapon waits for it too, so the hand holding the gun and the hand with the
   trigger are never different ones. Bindings are suggested for the Reverb G2
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
   - **Snap turning** (`vr_snap_turn 1`, `vr_snap_angle`, 2026-09-22). Both of SiN's ways of turning - the
     joystick in `IN_JoyMove` and the keys in `CL_AdjustAngles` - add a rate times the frame's length to
     `cl.viewangles[YAW]`, so a one-frame shove on the stick would snap by a different amount every time.
     `vr_turn.c` changes the angle itself instead: its address is read out of the joystick's own
     `addss`/`movss` pair and believed only if both name it (ENGINE_NOTES). A push past 0.7 turns once,
     the stick has to come back inside 0.3 before it turns again, and the joystick is told to turn
     nothing; smooth turning is the engine's own, untouched. Measured at the desk with `vr_fake_axes`:
     right, held, back, left gave -45, -45, 0; a 30 degree snap gave -29 by `viewpos`, which truncates.
     `vr_fake_snap <degrees>` turns without controllers - even flatscreen, which is how the headset
     being off one afternoon did not stop the angle being checked (45, 45, 90 and 30 exactly, and
     walking afterwards went the new way: the game's movement follows, not just the view).
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
8. **A button held across a change of mode must not read as a fresh press.** `release_buttons` used to
   clear `down` as well as stopping what the button was doing, so a button still under the player's
   thumb was seen as pressed again the moment the mode changed - and opening the menu *is* a change of
   mode. One click of the stick therefore came out as press, open, press, close, press, open, once a
   frame, which is what the owner saw. It now stops what the button was doing and leaves it held, and
   nothing is pressed again until the thumb comes off.
9. **The menu is the right stick's click.** Both menu buttons are bound to Escape as well, but on the
   Reverb G2 they never arrive: SteamVR keeps them for its own dashboard. The owner's log shows it
   plainly - two presses, no action, and each one followed by
   `the runtime has the input focus - the controllers are idle until it gives it back` half a second
   apart. The Windows button is the runtime's too. Whatever a runtime does pass on still works, so the
   bindings stay.
10. **The idle pump.** While SiN's "PRESS ANY KEY TO CONTINUE" card is up the engine stops calling the
   renderer altogether - but it keeps pumping window messages, which is how the keyboard reaches the card.
   Everything above runs inside renderer calls, so on the card the controllers went deaf and the headset
   was left on SteamVR's loading grid. A `WM_TIMER` on the engine's window (`idle_timer` in `ref_vr.c`)
   now notices when nothing has been drawn for 100 ms after a theatre frame, and `vr_xr_idle` keeps the
   OpenXR frame loop going - re-showing the last released image of the theatre swapchain, so nothing is
   drawn - while `vr_input_idle` turns any button into a space bar. Buttons held when the engine went
   quiet are released first, so no `+command` is left held. It never starts OpenXR, never runs inside a
   renderer call, and steps aside after a stereo frame, which the runtime reprojects better.
   - Verified with `desk-test.ps1 -NoCardKey`, which leaves the card to the mod, and
     `+set vr_fake_idle_press 1`, a pretend button pressed once a second while the engine is quiet: the
     card went at 10.97 s and the world was in stereo at 11.0 s, with 50 idle frames in between.
11. **Input needs the headset awake.** OpenXR only gives an application its actions while the session is
    `FOCUSED`, and SteamVR drops that when the headset's proximity sensor says nobody is wearing it. That
    is the runtime's rule, not something to work around: for desk testing, stop the headset sleeping.

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

## 12. Tracked hands

The owner's verdict after the input work (2026-09-16): playable, but the crosshair follows the headset
rather than the gun, and the gun arm looks too big. Aiming from the hand is the answer to the first, and
holding the weapon in the hand to the second.

Built so far:

1. **The poses.** `ACT_HAND_LEFT`/`ACT_HAND_RIGHT` are bound to `/input/aim/pose`, and
   `xrCreateActionSpace` gives each a space that `xrLocateSpace` reads at the same display time the eyes
   were located for, so the hands and the view agree about when they are. `vr_view_local()` turns each
   pose into the recentred frame the game wants.
2. **The channel.** Both DLLs are in sin.exe, so nothing as heavy as shared memory is needed:
   `ref_vr.dll` exports `SinVrPoses`, the game module looks it up with `GetModuleHandleA`/
   `GetProcAddress` (`src/game_vr/vr_link.cpp`), and reads the block described in
   `src/shared/sinvr_poses.h`. A frame counter in the block is how the game knows the renderer is still
   writing: a tenth of a second of the same frame counts as stopped, and the game goes back to its own
   aiming. Without the VR renderer the export is simply absent.
3. **The frame of reference** is the recentred one: origin at the player's eyes
   (`Sentient::EyePosition`), +x the way the body faces, in game units. The game finishes the job with
   its own yaw, which is the one thing the renderer does not know:
   `world = EyePosition() + rotate_yaw(hand_origin, worldangles[YAW])`.
4. **Aiming.** `Player::GunPosition` and `Player::GetGunOrientation` override Sentient's, and every
   weapon goes through them: `Weapon::GetMuzzlePosition` asks the owner for both. They answer with the
   weapon as the renderer draws it (below), so a shot leaves the barrel the player can see. Gated on
   `vr_aim`, on a weapon in the hand, on normal gravity, and on this being the first client - the player
   at this machine - so a flatscreen run, an untracked controller or another player behaves exactly as
   SiN always did.

What the desk could show: with `vr_fake_hand "16 -6 -10 0 40 0"` the game module prints
`SiN Gold VR: tracking the player's head and hands`, which is the whole channel working - renderer,
export, game module, validity and staleness checks.

What it could not: **a shot.** `+attack` from a console script never reaches the weapon code in an
unattended run - probes in `Sentient::FireWeapon`, `Weapon::Fire`, `BulletWeapon::FireBullets`,
`Weapon::GetMuzzlePosition` and `Sentient::GunPosition` all stayed silent while `G_RunFrame`'s probe
fired every frame, so the client is not turning a console `+attack` into a usercmd button here. Real
controller and mouse input does fire, so this is a limit of the harness, not of the game: the aiming
maths is what the owner's next headset test exercises.

5. **The weapon in the hand** (`vr_weapon.c`). The view weapon is an ordinary entity in the frame's
   list, flagged `RF_WEAPONMODEL`, placed at the eye and turned with the view. In VR it goes on the
   controller instead.
   - **The controller is a frame**: its position is the *grip* pose, where the hand actually holds it,
     and its orientation is the *aim* pose, the ray the controller points along. Both are located at the
     same display time as the eyes.
   - **Each weapon has a grip and a size.** The grip is the point of the model the hand holds, in the
     model's own units; the model is placed so that point lands at the controller's grip, pointing where
     the controller points. The table of first guesses in `vr_weapon.c` was measured off the models
     themselves (ENGINE_NOTES has the file formats): the guns are about three times life size, which is
     also what the game's own `Weapon::MuzzleOffset` assumes when it scales their bones by 0.3.
   - **The player settles it in the headset.** Holding the right grip button for 0.8 s freezes the weapon
     where it is; the hand then moves to hold it as wanted, the right stick sizes it, and letting go
     stores the transform the hand now has relative to it in `vr_grip_<weapon>` (archived, so it keeps).
     `vr_weapon_reset 1` clears the weapon in hand back to its guess.
   - **Size** is the entity's own `scale` at +0xAC: ref_gl multiplies SiN model vertices by it, and the
     client's bone code scales bone positions by it too, so the barrel bone stays on the barrel. (An
     earlier note here said +0xAC was not a scale. It is: eye dumps of 1.0 against 0.35 settle it.)
   - **The crosshair follows the gun.** It is SiN's own `ch<n>_on`/`ch<n>_off` pic, drawn through
     `DrawPic` in the middle of the view - where the head looks, which is no use once the gun aims
     itself. So the pic is offset instead: the weapon's direction is taken into the eye's frame, and
     `vr_gl_panel_offset` turns that into the window coordinates that land there once the 2D has been
     mapped into the HUD panel. Both eyes get the same angle, so it hangs in that direction the way a
     red dot does rather than sitting on the panel. Measured in eye dumps: a gun 20 degrees right and
     one 15 degrees down put the pic within a pixel or two of where the geometry says, and nowhere
     else. `vr_crosshair 0` leaves it out, 2 leaves it where SiN put it.

6. **The client places it, so its effects follow** (`vr_weapon.c`, "Through sin.exe"). Moving the entity
   in `RenderFrame` moves the model and nothing else: SiN's client builds the view weapon's entity, hands
   it to the scene, and *then* runs the model's own client-side commands on it - muzzle flash attached to
   the barrel bone, tracers and shell cases spawned from it, a dynamic light, and its own bullet impacts.
   All of that is computed from where the client thinks the weapon is. So two calls inside `sin.exe` are
   redirected into this DLL (`vr_hook_call`, put back at Shutdown):
   - **`CL_AddViewWeapon`'s call to `V_AddEntity`** (+0x747F). The replacement moves the entity to the
     hand first, and for a two-armed model adds a second copy for the left hand. Everything downstream -
     the scene, the attachments, the model's commands - then sees the weapon where the player sees it.
   - **The `shootbullets` client command's call** (+0x1A707), which traces bullets from the player's
     *eye* along the weapon's angles and draws the sparks, holes and sounds where they land. It is
     skipped for the view weapon, and the game module sends the server's own impacts instead
     (`BulletWeapon::FireBullets`'s `server_effects`) - traced from the barrel, and drawn by the same
     client code by way of `TE_GUNSHOT`. `vr_client_bullets 1` keeps the client's instead.

   Each call is checked before it is changed - the instruction must be `E8 rel32` reaching the expected
   function - and the addresses the replacement needs (`cl.refdef.vieworg`, `cl.refdef.viewangles`, and
   the entity whose commands are running) are read out of the instructions that use them, so ASLR is
   handled and another build of `sin.exe` is simply left alone. When that happens the weapon is moved in
   `RenderFrame` instead: the model goes to the hand, its effects stay at the eye.

7. **Two arms** (the fists). `view_punch.def` is one model with both arms in it, so it is drawn twice,
   once per hand, and each copy has the other arm cut away by a clip plane down the model's middle
   (`vr_gl_clip_model_at`). Per-entity group flags cannot do it: `entity_t.groups` is indexed by the
   groups a model's `.def` *names*, and the fists' arms are not among them. ref_gl draws each model
   between a `glPushMatrix`, a `glTranslatef` to its origin and the matching `glPopMatrix`, so the plane
   goes on at that translate - in world coordinates, while the modelview still holds only the view, which
   is how GL takes clip planes - and comes off at the pop.

8. **Left-handed** (`vr_left_handed 1`). Everything above happens in the left hand, and a weapon built
   for the right one is drawn mirrored.
   - **The arrangement is reflected, not moved.** A weapon's place is held as a pose relative to the
     hand's own frame; reflecting that pose in the frame's y = 0 plane - the plane of the hand's forward
     and up - and the model with it gives the mirror image of the right-handed hold
     (`vr_pose_mirror_y`). So the measurement is stored right-handed whichever hand settled it, and a
     weapon tuned in one mode is tuned in the other. The fists, which have an arm each side already,
     are not mirrored: each arm simply goes to its own hand as before.
   - **The model is mirrored where ref_gl draws it** (`vr_gl_mirror_model_at`). The mirror belongs in
     the model's own frame, which the matrix reaches after `R_RotateForEntity`'s three rotations, so the
     rotations after the marked translate are counted and a `glScalef(1, -1, 1)` goes on after the
     third, with `glFrontFace(GL_CW)` so the reflection does not turn the model inside out; the pop puts
     the winding back. A negative entity scale would have been a point inversion, not a mirror.
   - **The barrel moves with it.** The game measures the muzzle off the model's own bones, so the
     renderer sends `SINVR_WEAPON_MIRRORED` and `Weapon::MuzzleOffset` changes the sign of the bone's
     y - its distance to the model's left - for the mirrored model.
   - **The buttons swap with it** (§11), which is why the setting is read once, at start-up.

9. **The controllers are drawn while a weapon is being placed** (`vr_show_hands`, default 1; 2 draws
   them always). A weapon frozen in mid-air has nothing to line up against, so each tracked hand gets a
   cross at the point it holds, its arms along the hand's own forward, left and up, and a line 30 cm out
   the way the controller points.
   - **It is drawn after ref_gl has finished with the eye**, into the same framebuffer, before the
     client draws the HUD. `vr_gl_world_begin` sets the eye's own frustum and rebuilds the view matrix
     from the refdef exactly as `R_SetupGL` does - the disassembly confirms SiN's is Quake II's, to the
     rotation - takes the whole eye as the viewport, and turns off texturing, the scissor and the depth
     test, so the cross is visible through the weapon it is being lined up with. Everything is put back
     with `glPushAttrib`/`glPopAttrib` and the matrix stacks.
   - **The hands are in the recentred frame**, so turning them into the world needs the engine's own
     view, not the eye's; `vr_weapon_to_hand` keeps it each frame, before the view moves to the eye.
   - Measured at the desk on 2026-09-20, with the magnum's grip set to nothing at all so that the
     weapon's origin and direction are the hand's own: the cross landed within 5 px of where the eye's
     logged frustum says it should, in a 2524x2468 eye; its aim ray pointed at the crosshair - projected
     by code measured earlier - to within 3 px over a 680 px extension, and within 1 px with the hand
     turned up 10 and left 20 degrees; moving the hand 6 units to the right moved it 375 px against a
     predicted 377. Nothing is drawn in play, and it appears the moment the weapon freezes.

10. **The idle animation can be held still** (`vr_freeze_idle`, and always while a weapon is being
   placed). `Weapon::ForceIdle` plays the model's `idle` animation, which is where the breathing and
   the swaying arm come from; a weapon on a controller has the hand's own movement instead, and
   breathing under a weapon being lined up with a hand is only in the way.
   - **The renderer decides, the game acts.** `vr_weapon.c` sets `SINVR_WEAPON_STILL` when the grip
     button has the weapon frozen or `vr_freeze_idle` is on, and `Weapon::ForceIdle` takes the idle
     pose once and then `StopAnimating`s, which holds that frame.
   - **The idle animation is also a heartbeat.** Its end posts `EV_Weapon_Idle`, which is what brings
     the weapon back to `Weapon::Idle` to check whether it should reload and to pick its next idle. So
     the frozen path posts that event itself every half second; `Weapon::Idle` sends it away again
     while the weapon is firing or reloading, where the animation's end would never have arrived.
   - **Only the idle is held.** Firing, reloading, raising and putting away animate as before, which
     matters because the client's muzzle flash, shell cases and `shootbullets` hang off the *fire*
     animation's first frame (docs/ENGINE_NOTES.md).
   - Measured at the desk on 2026-09-20, two dumps a second apart with the hand held still: with the
     animation running, 14,719 pixels of the weapon changed; with it held, 1,971 - and those are the
     one-pixel outlines a sub-pixel shift of the head leaves behind, against 459 and 129 in a patch of
     world with no weapon in it. The game says which state it is in, either way, once per change.

11. **Punching by swinging** (`vr_punch`, `vr_punch_speed`), 2026-09-21. The fists are one per hand, so a
   swing of either throws a punch from that fist.
   - **The renderer sees swings** (`watch_swings` in `vr_weapon.c`). Each hand's speed is measured
     between this frame's grip position and the last, over the runtime's own display times, so the
     frame rate does not enter into it. A swing starts when the speed passes `vr_punch_speed` *the way
     the controller points* - at least a fifth of the motion along the aim, which is along the forearm
     for a jab, across for a hook, up for an uppercut, and never for pulling the fist back. The punch is
     thrown at the swing's fastest, where a real punch lands, when the speed falls to 85% of its peak or
     the fist turns round; then the hand must slow to half the threshold before it can throw another.
   - **Tracking jumps are not punches.** A controller that loses tracking and finds it again, a recentre,
     or the desk's pretend hands taking over from real ones all arrive somewhere else in one frame: the
     first desk run saw 103 and 172 m/s "punches" that way. Anything over 20 m/s is taken as a jump and
     measured from afresh, and a recentre forgets both hands.
   - **The renderer sends every swing, the game decides.** The block (`SINVR_POSES_VERSION` 3) keeps the
     last punch of each hand with a count; `Player::VRPunches`, from `Player::EndFrame`, lands each new
     one if the current weapon is a `Fists` (which takes in `MutantHands`). The first look only notes the
     counts, and a count of 0 is none, so nothing is replayed after a level change or a `vid_restart`.
   - **Where it lands** (`Fists::VRPunch`). The first version traced a thin line along the swing, and the
     owner found punches "really hard to land". The log of that session showed why: all 48 swings were
     seen, but a real punch curves, and at its fastest half of them were going up or down more steeply
     than forward - `(0.46 -0.24 0.85)`, `(0.44 -0.44 -0.78)` - so the line went over or under whoever
     was being punched. Now, in turn:
     1. **the fist**: a box 12 units (30 cm) across swept along the swing from 16 units behind the fist to
        16 past it; anything that can be hurt takes the blow, along a line from the eye through the
        point the box met it (so a hook to the head is a head hit), or through its middle;
     2. **aim assist** (`vr_punch_assist`, degrees, default 35): the living `Sentient` whose box comes
        within the fists' own `strike_reach` of the eye - 64 units, the button's reach - and passes
        nearest the head's look or the fist's heading, with nothing solid in between;
     3. **the swing**: whatever wall it went into, or nothing.
     The cone is measured to the nearest point of the creature's *box*, not its middle: at punching
     range a creature's middle is 30-odd degrees below the eye, and the first version of the assist
     missed an uppercut at a grunt the head was plainly looking at. If the eye cannot see the start of
     the swing, the fist is through a wall, and the blow lands where the arm went in. The damage,
     sparks, blood and sounds are `Fists::Strike`, which is `Shoot`'s body moved out so all of them can
     use it - the button behaves exactly as before. The full trace's radius is SiN's own for the fists,
     64: once a line is inside a creature's box it finds the model generously (bullets use 5).
   - `vr_punch 2` prints each punch, what it hit and how. At the desk: a fist driven into the floor
     landed ("World", 13.6 m/s); the slower pull back, a fist yanked backwards, a 60-unit jump down the
     fist's own line and a jab with the magnum out threw nothing. Against grunts spawned with
     `spawn grunt_none origin "x y z"` (a grunt with no gun; cheat commands work on a map loaded without
     `deathmatch`): a jab at the air with a grunt 200 units off hit nothing; a jab that reached one 48
     units off landed by the fist; an uppercut that went nowhere near him landed by aim assist.
   - Not done: a buzz in the controller when a punch lands (OpenXR haptics want an output action and
     the game to say "hit" back), punches that hit harder the faster they go, and swinging a gun.

What the desk could show, with `vr_fake_hand` and `vr_fake_hand_left`: the weapon in the hand at the
computed place and size, turning the right way in pitch and yaw, the fists as one arm per hand, and the
grip button freezing and re-attaching the weapon (frozen with the hand at yaw 0, released with it at yaw
30, stored as -30 and the size 1.41x) - through both paths, the client's and the renderer's fallback.

Left-handed, at the desk on 2026-09-20: the same weapon and hand position mirrored gave the pose
(1.5 -6.6 2.8) against the right-handed (1.5 6.6 2.8), to the last digit; the eye dumps show the gun in
the left hand with its own details on the other side, and reflecting the right-handed picture puts the
same side of the gun in view as the left-handed one (`lefty_side_by_side`); the fists still come out one
arm per hand. `vid_restart` is enough to change the setting over: the renderer shuts down, starts
again, latches it, suggests the swapped bindings and the weapon comes back in the other hand - the
whole OpenXR session is rebuilt and the run carries on. Comparing the two runs pixel for pixel is not
worth much: each eye's frustum reaches
further to its own side, so the same gun in the same place is cut off by a different edge in each eye,
and the runtime's eye size moves between sessions.

What it still could not show: **a shot.** A console `+attack`, and a key bound to it, both leave the
weapon code untouched in an unattended run (§8), so the firing chain - the muzzle from the barrel bone,
the server's impacts - is what the owner's headset test exercises.

Still to do, and what is known about each:

- **Two-handed holds**: a rifle steadied by the off hand near its fore-end, which is also where SiN's
  models already draw a hand. Nothing in the engine is in the way - the weapon's pose is this mod's to
  compute - so it is a matter of a second point per weapon, a way to say the off hand has taken hold
  (its grip button, or simply its being near that point), and orientation from the line between the
  hands with the roll still coming from the gun hand. It wants the same in-headset tuning the grip has.
- **Manual reloading**: SiN already has clips, `reload`/`silreload` animations and
  `Weapon::ForceReload`, which `Player::DoUse` calls - the Use button doubles as a reload key. So a
  gesture only has to reach it: watching the off hand under the weapon in `vr_weapon.c` and running
  the bound command is the whole of it, with a small console command of our own in the game DLL if it
  should not also open doors.
- **The idle freeze is not always consistent** (owner, in the headset, 2026-09-21: "works, but didn't
  seem 100% consistent"; to revisit). Leads, none of them confirmed:
  - it only takes hold at the next `Weapon::ForceIdle`, so there is up to one idle cycle of breathing
    after the grip freezes the weapon or `vr_freeze_idle` goes on. Poking the weapon with
    `EV_Weapon_Idle` from the player's frame when the flag changes would make it immediate;
  - `ForceIdle` is usually reached at the *end* of an idle animation, and its "already idling?" check
    then keeps whatever frame the weapon is on - the last one of that idle, not the first - so the held
    pose can differ between `idle1` and `idle2`, and from the first frame the muzzle is measured at;
  - `Weapon::SecondaryUse` starts `primaryidle`/`secondaryidle` itself for the dual-mode weapons, which
    then play through once before the freeze sees them.
  Nothing else in the game overrides `Idle` or `ForceIdle`.
- **Comfort and roomscale:** snap turning, crouching, and the body following the head.
- **Hand tracking** (`XR_EXT_hand_tracking`) is the same channel with a different source.
