# SiN Gold VR: start here

This is the handoff for anyone joining the project, whether a person or an AI. It covers what exists,
why it is shaped the way it is, how to build and test it, what has already been learned the hard way,
and what comes next. The detailed evidence lives in the other files in `docs/`; this one is the map.

*Last updated 2026-10-05 (roomscale).*

## 1. Where the project is

| Milestone | State |
|---|---|
| 1. Pass-through renderer, game DLL built from source, 32-bit OpenXR loader | ✅ Done, verified in unattended runs |
| 2. Virtual "theatre" screen in the headset | ✅ Done. The owner confirmed it in the headset on 2026-09-15: it loads, doesn't crash, and the colours are right. It is low resolution, as expected. |
| 3. Stereo rendering with head tracking | ✅ Built and verified at the desk on 2026-09-16: 406 projection-layer frames in one run, correct per-eye poses and fields of view, ~87 frames/s at 2444×2392 per eye. Not yet seen in the headset by a person. How it works: [§9](#9-how-stereo-works) |
| 4. HUD, crosshair and console in VR | ✅ Built 2026-09-16. The 2D goes into a panel `vr_hud_fov` degrees wide, `vr_hud_depth` metres away, centred on each eye's own axis - which is also what stops the crosshair from doubling. [§9](#9-how-stereo-works) |
| 5. Motion controller input | ✅ Built 2026-09-16: buttons run console commands, the sticks arrive as a joystick the engine polls, and in menus both turn into keystrokes. Measured at the desk. [§11](#11-controller-input) |
| 6. Tracked hands | ✅ Built 2026-09-19, left-handed mode 2026-09-20. The weapon is drawn on the controller, at a per-weapon grip the player can set in the headset, and fires down its own barrel; sin.exe itself places it, so its muzzle flash, tracers and impacts come from there too. The fists are one arm per hand. `vr_left_handed 1` mirrors the lot into the other hand. Firing is the one part the desk cannot exercise. [§12](#12-tracked-hands) |
| 7. Roomscale comfort | ✅ Snap turning (§11). The body following the head, and crouching in the room, built and verified at the desk 2026-10-05: each movement command carries the head's step, sin.exe's Pmove is followed by it on the client and the server alike, and the view moves with the body. [§13](#13-roomscale) |
| 8. Wages of Sin | ✅ 2026-10-11: every VR change ported to `game2015`, built into `sinvr2015\gamex86.dll`, run with `+set game sinvr2015` - the engine adds the expansion's `2015` data under it. Desk: `2015dm1` in stereo, the weapon in the hand, a pretend metre walked with the server running every step. [§14](#14-wages-of-sin) |
| 9 | Packaging (see the roadmap in the [README](../README.md)) |

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
  frames, and `vr_fake_press "adjust 0"` until `vr_fake_press release`; a press ending `also`
  (`vr_fake_press "run 0 also"`) is held together with the one already held, and `release` lets go of
  both - and a `wait` is not a frame, so
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
  each other at the end; each is only read when its turn comes. The other way to overflow it: a file
  that fits, `exec`ed while plenty is still queued behind it (the rest of `desk-test.ps1`'s own cfg),
  so that putting the rest back after it does not. That one is fatal - SiN shows "SZ_GetSpace:
  overflow without allowoverflow set" in a message box and quits, straight after `execing ...` in the
  log. Keep each chained file to 3-4 KB.
- **Rendered frames and `wait`s do not run at the same rate.** On this machine the command buffer gets
  through about 180 `wait`s a second while the headset path renders about 88 frames a second, and the
  ratio moves. Anything timed against a held button (the weapon adjustment, say) wants
  `vr_fake_press "<action> 0"` and an explicit `vr_fake_press release` rather than a count of `wait`s.
- For input, load a **deathmatch map** (`-Map sindm6 -ExtraArgs "+set deathmatch 1"`): the single-player
  levels open on a cinematic that holds the player still, and a player who is still falling from a spawn
  point has almost no control in the air.
- A VR run adds about 12 s while SteamVR starts, and SteamVR stays running afterwards.
- **A save made on a deathmatch map does not load**: `sindm6` saves, and `load` then stops with
  `ERROR: CM_InlineModel: bad number`, while the same on `bank` or `training` loads. The owner's test
  save `vrtest` (every weapon, `wuss` plus a `give` of each ammunition) is therefore on `training`,
  SiN's own training course, in `sinvr\players\blade\save\vrtest`.
- **Saves live in `sinvr\players\blade\save\`**, and `save0` there is SiN's own, rewritten when a
  level starts - every desk run writes it. Move a test's saves out afterwards, not delete them (§7).
- **Back up the owner's `config.cfg` before a test and compare it after** (sorted: SiN writes it in
  reverse order each time). Until 2026-09-27 a flatscreen run (`vr_enable 0`) dropped every controller
  and weapon setting from it, because SiN writes back only the cvars something has registered, and
  those were registered only with the headset on; they are registered either way now
  (`vr_input_register`).
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
| `src/ref_vr/vr_room.c` | Roomscale: the head's step in each movement command, the step after Pmove for client and server (`SinVrPmove`), crouching in the room, the crouch button's drop, the ceiling and the lean (§13) |
| `src/ref_vr/vr_weapon.c` | The weapon in the hand: per-weapon grips and sizes, the adjustment in the headset, the two calls redirected inside sin.exe, the fists split between the hands, and which hand holds it |
| `src/ref_vr/vr_math.c` | Poses in Quake's terms: angles to axes and back, composing them, taking one relative to another, and reflecting one for the other hand |
| `src/shared/sinvr_poses.h` | The contract between the two DLLs: the block, its frame of reference |
| `src/game_vr/vr_link.cpp` | The game module's side of that link. Ours, built into the game DLL |
| `src/game_vr/vr_game.cpp` | The game module's VR code itself - aim from the hand, swing punches, the idle hold - compiled into both SiN's and Wages of Sin's DLL against each game's own headers (§14) |
| `src/ref_vr/vr_hook.c` | Patching another module in this process: its imports (the GL and joystick hooks), and single direct calls inside sin.exe |
| `src/ref_vr/xr_layers.c` | Implicit-layer hygiene for 32-bit processes (shared with xrprobe) |
| `src/ref_vr/vrlog.c` | `sinvr.log` |
| `game/` | Night Dive's source (SiN in `src/code/game`, WoS in `src/code/game2015`, shared `elib`) plus `game/CMakeLists.txt`. `git diff 5236699 -- game` shows everything we changed there: VR aiming, swing punches, and roomscale's step and held eye height in `player.cpp`/`player.h`, the muzzle and shot direction in `weapon.cpp`, server-side bullet impacts in `bullet.cpp`, and a line in `g_main.cpp`. |
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

- ~~**The body does not follow the head.**~~ It does now (§13): the head's step is in every movement
  command, so the player's collision position follows it.
- **Screen-copy effects** (screenshots, and anything that reads the window) still assume window coordinates.

## 10. Open questions and backlog

**Resolution and rendering**

- The theatre screen uses the window's resolution (640×480 by default). A higher `gl_mode` does **not**
  sharpen it (desk, 2026-10-11, `gl_mode 7`, 1600×1200): SiN draws its menus and HUD at their 640×480
  size in pixels, so they come out as a small picture in the middle of a big black one, and the HUD
  shrinks the same way in the stereo panel. Sharper menus would mean drawing the 2D into a bigger
  buffer of our own with the viewport scaled up - text and edges crisper, though the art itself is
  640×480. The stereo path does not care: it renders at the runtime's recommended eye size regardless
  of `gl_mode`.
- The level-start card (§8) is worth handling in VR: it is a non-world frame, so it lands on the theatre
  screen while the world behind it is not being drawn.

**Input**

- ~~The body following the head, and a real crouch~~: done, §13. It did need a channel of its own in
  the end - the step has to be the same each time the client re-predicts a command, so it travels in
  the command.

**Content and release**

- ~~Wages of Sin's deployment~~: `sinvr2015` (§14).
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
   - **Left-handed, the lettered buttons change letter as well as hand** (fixed 2026-09-27). The G2 and
     Touch controllers have X and Y on the left and A and B on the right; moving the left's X to the
     right hand asked for a right X, which does not exist, and SteamVR refused the whole G2 table with
     `XR_ERROR_PATH_UNSUPPORTED` (-22) and fell back on a layout of its own - which is why, in the
     owner's left-handed session, the right trigger fired. Now X becomes A and Y becomes B (and back) on
     those profiles, and on Touch, whose menu button is on the left only, that one binding is left out
     rather than sinking the rest. The log says `bindings suggested for <profile>` for each table taken;
     a `would not take` line for a controller you are using is the thing to look for.
   - **Body gestures** (`vr_gesture.c`, `vr_gestures`, 2026-09-28). The owner's design: the gun hand's grip
     over its own shoulder is `weapnext`, over the other `weapprev`, at its own hip `use Magnum`; both
     hands up at the shoulders with both grips `use Fists`. The places are boxes in metres from the head,
     turned with the head's yaw (over a shoulder: 0.40 below the eyes to 0.30 above, level with the face
     or behind it to half a metre back, to that side; a hip: 0.50 to 1.20 below the eyes, 0.35 back to
     0.25 forward, 5-50 cm out). `press_action` asks `vr_gesture_gun_grip`/`vr_gesture_other_grip` first,
     and a press they take takes its release as well, so the adjust hold and running never see it. A gun
     grip at a shoulder with the other hand also at one waits 350 ms for the other grip before it is a
     weapon change (`vr_gesture_frame` times it out); the other grip, pressed with both hands up, is the
     fists' half and never `+speed`. Haptics came with it: two `XR_ACTION_TYPE_VIBRATION_OUTPUT` actions
     bound to each profile's `/user/hand/*/output/haptic`, not swapped for the left-handed, a light tap
     as a hand reaches a place and a firmer one as a gesture fires (`vr_input_haptic`). Desk, both
     handednesses, with the hands put in each place: next, previous, magnum, the fists with either grip
     first, and away from the places the adjust hold and `+speed` as before; every profile's bindings
     still taken. The commands themselves queue behind a test cfg's `wait`s, as every mod-queued
     command does (§5), so the weapon changes are for the headset. The Y and B buttons keep the weapon
     cycling for now, as a fallback, until the owner gives them something else.
   - **The top buttons off, and something to feel** (2026-09-28). `vr_weapon_buttons` (default 0) lets the
     weapon-cycling buttons through only when set; otherwise they are ignored outside menus, with a
     hint the first time. Shots and punches are felt through a second export, `SinVrFeedback(kind, hand,
     strength)` beside `SinVrPoses` (sinvr_poses.h), which the game calls through `VR_Feedback`: a
     shot from `Weapon::Fire` once it has taken the ammunition - every weapon's fire animation has one
     `shoot` on its first frame, so that is one call a shot, automatics included - for anything but
     the fists (`inheritsFrom("Fists")`, which takes in the mutant hands); a hit from
     `Fists::VRFeelHit`, after the button's punch in `Fists::Shoot` and the swung one in
     `Player::VRPunches`, 1 for something that takes damage and 0.4 for a wall. The renderer buzzes
     the hand (the weapon's, for `SINVR_HAND_WEAPON`), longer and harder for the shotgun, rocket
     launcher, sniper rifle, quantum destabilizer and speargun, and the other hand as well while it
     steadies the weapon. `vr_haptics 0` silences everything. Desk: the button off, then on; two
     swung punches into a grunt, both felt; the shots are for the headset.
   - **Pulses long enough to feel, and the destabilizer's charge** (2026-09-28). The owner felt only
     every other magnum shot, though the session's log had all 173 shots sent (as many as the 166
     flashes, near enough): the G2's controllers let a 40 ms pulse through only some of the time. No
     pulse is shorter than 100 ms now, and `vr_input_haptic` logs the first few the runtime refuses.
     The quantum destabilizer charges while the trigger is held - `eatammo` every tenth of a second
     from its fire animation, `ReleaseFire` setting the power to the seconds held over five, and
     `destruct_time 5` blowing it up in the player's hands at five - so `Weapon::Fire` no longer sends
     its shot; `QuantumDestabilizer::EatAmmo` sends `VR_FEEDBACK_CHARGE` with the seconds held over five
     each tick, a 120 ms pulse from faint to full so they run together, and `ReleaseFire` the shot at
     0.3 + 0.7 of the power. Under god mode `EatAmmo` does not post itself again, so the buzz is one
     tick there. For the headset: nothing about firing reaches a desk run.
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
12. **Two-handed holds** (`vr_two_hand`, `vr_fore_<weapon>`), 2026-09-27. All in `vr_weapon.c`; nothing
   in the engine or the game module changes, because the game already fires down whatever pose it is
   given.
   - **The fore-end** is a point on the model, kept in the model's own units as the right hand holds it
     (mirrored in use, like the grip), so moving or resizing the grip later carries it along. The first
     guess is the table's `fore` distance - 0.30 m, 0.35 for the sniper rifle and the quantum
     destabilizer, 0 for the magnum, mines, detonator and fists - straight ahead of the grip point along
     the model's x. That is deliberate: the modelled left hands are all fused into one mesh with the
     gun, and finding them by connectivity (`scratchpad/findhand.py`, research only) gave points too
     scattered to trust, whereas a point on the barrel line makes the gun point exactly where the two
     palms do - the palm-to-palm line on a real rifle is near enough parallel to the bore.
   - **Taking hold**: the other hand's grip button is the `run` action, which is already on the off
     hand in both handednesses (the tables are swapped by `for_hand`). `press_action` asks
     `vr_weapon_steady(1)` first; if the hand is within 25 cm of the fore-end the press is the weapon's
     and no `+speed` is sent, and its release goes the same way. Anywhere else it runs. While adjusting,
     the same press moves the fore-end to the hand instead. Changing weapon, or starting to adjust,
     drops the hold.
   - **The pivot** (`pivot_weapon`): the gun hand's frame is turned about its own origin so that the
     direction to the fore-end, as the hand sees it, lies along the direction to the other hand; the
     roll is the one that keeps the hand's up as near its real up as the line allows (its left instead
     when the line is within about 18 degrees of straight up or down). The weapon is then recomposed
     from the turned hand with the grip as before, so everything downstream - the model, the crosshair,
     the muzzle the game fires from - follows without knowing.
   - Desk, 2026-09-27, `sindm6`, assault rifle, `vr_fake_hand "20 -6 -8 0 0 0"` and the other hand at
     (31 0 -2): taking hold 22 cm from the fore-end turned the rifle to (-25.6 28.6 0.0), exactly the
     direction between the hands; letting go returned it; a press with the hand 30-odd cm away sent
     `+speed`/`-speed` as before; adjusting and pressing the other grip wrote `vr_fore_asrifle
     "59.9 -15.3 -18.6"` - the hand's offset divided by the model's scale, to the digit - and a hold taken
     there turned nothing, as it should. Left-handed, every number mirrored: (-25.6 -28.6 0.0) and the
     same stored fore-end. `vr_fake_press "run 0 also"` holds a second button with the first, which is
     how the adjust-and-set step was driven.

13. **The other hand, drawn** (`vr_other_hand`), 2026-09-27. With anything but the fists out, the hand
   without the weapon had no model at all. It is now a copy of the weapon's entity with the fists' model
   (`models/view_punch.def`, loaded by the renderer itself at the end of every registration, just
   before ref_gl frees what nobody asked for - `vr_EndRegistration`), held on the first frame of its
   idle, cut down to that hand's arm as the fists are, and placed from `vr_grip_punch_left` (or
   `vr_grip_punch`, left-handed) - so it looks exactly like that fist when punching. Three things
   learned on the way:
   - **a model's animations are numbered alphabetically by name**, not in the `.def`'s order: idle1 is 5
     on the fists (fire1-5 come first). The first try used 1 and drew the fist in fire2's pose, off at
     the bottom of the view;
   - **the copy has to have an id of its own.** `V_AddEntity` gives any entity whose id is `0x1E61` a
     fresh one, and effects like the muzzle flash attach to the gun by its id, so the copy carries
     `0x1E61`;
   - **it carries the weapon's flags**, so the renderer's search for the view weapon found it as well and
     flipped between the two every frame (the log said "the view weapon is now" dozens of times a
     second). It is told apart by where it was put (`is_other_hand`).
   Desk, 2026-09-27: the fist in the left hand with the rifle, pulse rifle, speargun, chaingun and
   shotgun, the same as the fists' own left arm; left-handed, a right fist with the gun mirrored in the
   left hand.
14. **The arm built into the weapons, left out** (`vr_weapon_arm 0`), 2026-09-27. SiN's long weapons
   carry a left arm - sleeve and glove - reaching in to the fore-end, but in the same mesh and group as
   the gun, so neither the entity's group flags nor a clip plane can separate it. What can: SiN's model
   drawer (ref_gl+0x18420) sends each strip as `glBegin`, then `glTexCoord2f`, `glColor4f` and
   `glVertex3fv` per vertex, then `glEnd`, and the vertex it passes is a pointer into its own array
   of the frame's vertices (named by the `lea eax, [edx + array]` at ref_gl+0x18A58, 16 bytes each), in
   the model's own numbering. So `vr_gl.c` holds each strip of a marked model back until `glEnd` and
   drops it if every vertex is in the arm's ranges; everything else is sent on exactly as it came. The
   ranges are per weapon in `weapon_table`: found by splitting each model into connected pieces and
   taking the ones painted in the sleeve's brown and the glove's blue (`scratchpad/armparts.py`,
   `showarm.py`), then checked by eye - colour alone also picks out blue and brown parts of the guns.
   The quantum destabilizer has no modelled hand; the magnum's and the mines' is the gun hand and stays.
   Desk: the rifle with `vr_weapon_arm 1` shows the sleeve and glove, with 0 it does not; the same for
   the others in turn.
15. **The fore-end where the model's hand was, and the other hand locked to it**, 2026-09-27. The first
   guess at each fore-end is now the middle of the modelled left hand's glove on the idle's first frame
   (`hand` in `weapon_table`, from `scratchpad/handspot.py`): 30-38 cm ahead of the grip on the shotgun
   and assault rifle, 21-27 on the pulse rifle and speargun, 14 on the sniper rifle, up and to the side
   on the rocket launcher. The chaingun's modelled hand is off to the side, clear of the gun, so it keeps
   the barrel-line guess, as does the quantum destabilizer, which has no hand. While the other hand
   holds the weapon, its fist is drawn at the fore-end, turned as the real hand was when it took hold,
   and moves with the weapon (`steady.hold`) - the owner saw it pull away from the gun before. Desk: the
   real hand moved 12 units off after taking hold; the fist stayed on the rifle as the rifle turned.
16. **The side the guns never had** (`vr_weapon_fill`), 2026-09-27. Measured over the models with the
   strip winding honoured (`scratchpad/facing.py`): the fists have as much surface facing left as right,
   every weapon two to five times more facing left - the side the original camera saw. So each strip of
   a gun, after it is drawn, has its left-facing triangles (within 60 degrees of left, and on the left
   of the barrel's line) drawn again reflected across that line and wound the other way (`fill_strip`
   in `vr_gl.c`), with their own texture coordinates and shading. The line is each model's barrel
   marker's y (`middle` in `weapon_table`), which is the model's own coordinates times the drawn size -
   ref_gl's vertex array already carries the `.def`'s origin and scale and the entity's scale. The
   first version took everything facing even slightly left and laid copies of the top faces over the
   real top of the magnum; the 60 degree limit took that away. Desk: from the right the rifle, shotgun
   and magnum go from slivers to solid, textured guns; from the usual side the picture is unchanged
   except a few pixels along the magnum's top edge, less than the idle animation moves between two
   dumps. Which brings up:
   - (A note written here first read two eye dumps 12,219 pixels apart as the idle freeze leaking. It
     was mostly the console text and SiN's weapon wheel fading - see 17: compare dumps below the
     console, and away from the HUD, or both will read as the weapon moving.)
17. **The idle held still in the picture** (`vr_weapon_idle`, default 0), 2026-09-27, after the owner
   saw the rebuilt side sway the opposite way to the gun - a reflection of a swaying model sways
   mirrored. The game's own hold (`vr_freeze_idle`) could not be relied on, so the renderer holds it:
   `hold_idle` swaps the weapon's idle animation for the first frame of that mode's main idle
   (`idle_table`: idle1-3 to idle1, silidle1-3 to silidle1, the chaingun's primaryidle1-2 to
   primaryidle1 and its secondaryidle to itself, and so on), on the copy handed to the scene only -
   `add_view_weapon` puts the entity's own animation back before the client runs the model's
   commands on it, so frame-timed sounds and effects go on as before. Desk: the magnum held for 600
   waits and dumped twice, 60 waits apart, with `vr_freeze_idle 0` - no pixel of the gun changed.
   Two more things from the same report:
   - **Mirroring only where it fits** (`vr_fill_<weapon>`, and `fill`/`keep` in `weapon_table`). Each
     gun drawn from the left, from the right as rebuilt, and from above with the copy in red
     (`scratchpad/sides.py`): the chaingun has a box on its left side only, so it is not filled; the
     magnum's own gun hand and forearm were being copied into a second forearm, and the pulse rifle's
     sleeve and glove along its top likewise, so those vertices are kept out of the copy (`keep`,
     checked per triangle in `fill_strip` against the vertex numbers the strip carries).
   - **The gun's middle** is the barrel marker's y, except on the chaingun and rocket launcher, whose
     marker sits on one of several barrels - 5 and 4 units off the body's middle - and which use the
     body's middle instead (-24.1 and -23.5; ENGINE_NOTES). For a while the fore-end was moved onto
     it as well, after the owner found the grip "a little too far to the right"; on the next test
     they found it too far the other way and put the first reading down to an off hand not yet tuned,
     so the fore-end is the glove's middle again.
18. **After the next headset test** (2026-09-27):
   - **The rebuilt side only while idling or firing.** Switching weapons, the reflection animated too
     and seemed to join up with the gun: the ready and putaway animations turn and roll the model, and
     a reflection across a fixed plane turns the other way. `idle_table` now carries a mask per model
     of the animations the weapon only tips up and down in - its idles and fire animations - and
     `hide_modelled_arm` rebuilds the side only in those (the rifle's ready1 is 6, not in 0x0F1E).
   - **Side handles.** The rocket launcher's modelled hand holds a handle out on its left side, which
     the glove's middle already was; the chaingun's floating hand held nothing useful, but the gun
     has a loop handle round its left side and underneath (vertices 310-357), and its fore-end is now
     the outside of that loop, (36.9 -9.0 -36.6). Desk: taking hold at the computed points on the
     rifle and the chaingun both said 0 cm.
   - The sleeve and glove on the pulse rifle's top were checked again, drawn on their own: an arm
     lying along the top, not a handle.

19. **The muzzle flash and shell cases at the gun** (2026-09-27). The owner saw them "in their flat
   position" rather than at the gun: they had never been checked, since the desk cannot fire. The
   model's client commands place them off its bones in `CL_CMDS_ProcessCommands` (+0x191E0), and for an
   entity with RF_WEAPONMODEL the `attach` (+0x194CE), `bonespawn` (+0x1B03B) and one more command
   (+0x1AA04) multiply the bone's offset by 0.3 (at +0x146DF0) - SiN's view models are drawn about three
   times life size - and mirror it when the `hand` cvar (pointer at +0x3FEC04) is 1. In the hand the
   weapon is drawn at its true size, so the flash sat at 0.3 of the way from the model's origin, behind
   the hand, to the barrel. After handing the weapon to the scene, `add_view_weapon` now divides the
   entity's scale by that 0.3 for the commands that follow, and for a mirrored weapon sets `hand`'s
   value to 1 until `vr_weapon_commands_done` at the start of RenderFrame, before ref_gl, which reads
   the same cvar to flip a view weapon, draws anything. Both addresses are read out of the attach
   code and the 0.3 checked. Desk: installs and runs every frame, left-handed too, with the weapon drawn
   as before and `hand` still 0 in config.cfg; the flash itself is for the headset. Two more tries at
   firing at the desk - holding the controller's fire for 200 frames, once straight after a weapon
   change and once 1800 waits later with the weapon wheel long gone - left the ammunition at 270.
   - **The owner's screenshot** (magnum, left-handed) showed the flash at the right depth but beside the
     gun. Not the axes: `00418E40` lays the offset out along the entity's own axis rows, so the flash
     follows the gun. It is `view_magnum.def`'s `attach ... origin 10 0 -10`: the effect option parser
     (+0x1793C) adds `origin` along those axes in flat view-model units, unscaled - tuned for the flat
     camera. The shotgun (`10 0 0`), rocket launcher (`20 0 0`) and chaingun (`30 0 20`) have one too.
     The view weapon's commands all pass through one call in `CL_AddViewWeapon` (+0x7580), which now
     goes through `process_view_commands`: while the weapon is in the hand, an `attach` command has its
     `origin` option left out, so the flash sits on the barrel bone. The call passes the word count and
     words on the stack (the caller pops them) with register arguments in ecx and edx, which the thunk
     keeps. Desk: installed, commands pass through it on weapon changes, clean shutdown; the firing is
     for the headset.
   - **The next screenshot** (left-handed again, all 13 flashes with their offset dropped, per the log)
     had the flash on the right of the gun - at the barrel of the gun *unmirrored*. Forcing `hand` to 1
     only mirrored the flash's first frame: an attached effect is placed again every frame by the
     other half of `00418E40`, which finds its parent in the scene list by id and lays the bone out
     along that entity's axis rows, with no 0.3 and no `hand`. So the `hand` trick is gone; instead a
     mirrored weapon's axis has its left row turned round (`mirror_axis`), in the scene's copy and the
     commands' alike. ref_gl draws the weapon from its angles, computed before, so the picture is
     unchanged; its only reader of the rows for a model is the cull box (+0x19685), which then fits the
     mirrored model. Desk: the mirrored magnum drawn in four poses, two half out of view, with nothing
     culled.

20. **The rebuilt side's join** (2026-09-28). The owner marked a thin gap along the magnum's top where
   the rebuilt side meets the real one. The mirror was across the barrel marker's y, -37.1, but the
   magnum's upward-facing surfaces - modelled all the way across - run from -42.3 to -30.5, middle
   -36.4, which is also the middle of its body; 0.7 units out, the reflection lands 1.4 units (about
   9 mm at its size) too far out. Every gun measured the same way (upward faces within 37 degrees of
   up, 5th to 95th percentile of their y) against its body's middle: where the two agree within half a
   unit the top faces' middle is used - magnum -36.4, shotgun -14.85, assault rifle -22.25, sniper
   rifle -15.9, pulse rifle -23.35, speargun -24.25 - and the body's elsewhere (quantum destabilizer
   -35.1, rocket launcher -23.5; the chaingun is not rebuilt). No gun is rolled or yawed in its model
   by more than 3 degrees, so the plane stays square. The join itself is for the headset; a desk pose
   close enough to see it put the gun mostly out of the picture.

21. **The fists' arms cut short** (`vr_upper_arm`, default 0), 2026-09-28. The owner wanted the fists' arm,
   and the other hand's, to stop below the elbow as the magnum's hand does. The fists' model has the
   whole arm - wrist to elbow, then back up (drawn with `scratchpad`'s side view: wrist at (13.5, -13),
   elbow at (0.5, -26.5), x forward and z up, both arms alike). `keep_one_arm`, which every fist goes
   through, now adds a second clip plane (`vr_gl` takes two per model, GL_CLIP_PLANE0 and 1) through
   the forearm 80% of the way to the elbow. Square across the forearm it left the upper arm's far end,
   which folds back over the forearm, on the kept side - a sliver in the first desk picture - so it
   leans 16 degrees back from square; checked against every vertex of the arm first, then in the
   eye dumps from both sides.

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

- **The rebuilt side drifts during the fire animation** (owner, 2026-09-28: "moves a bit when the
  recoil animation plays" - logged, not fixed). The reflection is across a fixed plane in the model's
  frame, and the fire animation moves the gun in ways a mirror across that plane does not follow
  (anything but a pitch about the model's y axis). The way to fix it: pick three vertices spread over
  each gun's body, note their positions in ref_gl's vertex array whenever the idle is held (`hold_idle`
  puts the gun on the first frame of its main idle, so that is the rest pose, at the size it is drawn),
  and in any other animation work out the rigid motion from those three to their current positions
  (orthonormal frames from each triangle) and carry the plane with it - reflecting across a general
  plane, p - 2(n.p - d)n, instead of `y = middle`. The arrays are already there at the first `glBegin`
  of the model.
- **Two-handed holds, what is left**: the fore-end distances are first guesses waiting on the headset,
  and the other hand is a fist - there is no open hand in SiN's models to hold a fore-end with.
- ~~**Mutant hands**~~ (2026-10-11): in the table now as two arms, like the fists - `view_mutanthands.def`
  has them either side of y = 0 with nothing near it (165 vertices each), each a stub of forearm
  (x 5) and a clawed hand (x 28-43), so the fists' elbow cut is not wanted and `keep_one_arm` makes it
  only on the fists' model. Held at the root of the claws, (31, -21, -15), at 0.35. Swing punches
  already reached them: `MutantHands` derives from `Fists` (reach 75, damage 100). Desk: both hands on
  their own pretend controllers, each turning with its own.
- **Punches by speed** (`vr_punch_full`, 2026-10-11): a swung punch's damage is `Strike`'s times its
  speed over `vr_punch_full` (4 m/s), held between half and twice; the trigger's is unchanged. The
  desk cannot throw one (§8) - for the headset.
- ~~**Manual reloading**~~: dropped by the owner on 2026-09-28 - only the magnum (15) and the assault
  rifle (45) have clips at all, so it would do little. If it comes back: SiN already has
  `reload`/`silreload` animations and `Weapon::ForceReload`, which `Player::DoUse` calls.
- **The game module's idle freeze is not always consistent** (owner, in the headset, 2026-09-21: "works,
  but didn't seem 100% consistent"). The picture no longer depends on it - the renderer holds the idle
  still (§12 item 17) - so this matters only if `vr_freeze_idle` is wanted for its own sake. Leads,
  none of them confirmed:
  - it only takes hold at the next `Weapon::ForceIdle`, so there is up to one idle cycle of breathing
    after the grip freezes the weapon or `vr_freeze_idle` goes on. Poking the weapon with
    `EV_Weapon_Idle` from the player's frame when the flag changes would make it immediate;
  - `ForceIdle` is usually reached at the *end* of an idle animation, and its "already idling?" check
    then keeps whatever frame the weapon is on - the last one of that idle, not the first - so the held
    pose can differ between `idle1` and `idle2`, and from the first frame the muzzle is measured at;
  - `Weapon::SecondaryUse` starts `primaryidle`/`secondaryidle` itself for the dual-mode weapons, which
    then play through once before the freeze sees them.
  Nothing else in the game overrides `Idle` or `ForceIdle`.
- ~~**Comfort and roomscale**~~: done - snap turning (§11), the body following the head and crouching
  (§13).
- **Hand tracking** (`XR_EXT_hand_tracking`) is the same channel with a different source.

## 13. Roomscale

Built 2026-10-05 (`vr_room.c`, and `Player::ClientThink`, `ClientMove` and `CalcViewOffset` in
`player.cpp`). Until then the view followed the head but the body - the box the game collides, the
point a turn pivots on - stayed where the stick left it, so a player could walk their head through a
wall, and a snap turn half a metre from the recentre point swung the view sideways.

1. **Why the step travels in the command.** The client predicts movement: every frame it runs the
   engine's `Pmove` again over each command the server has not acknowledged, from the server's last
   state, and the server runs it once more for real. Anything that moves the body has to give the
   same answer each of those times, or the client's prediction and the server disagree and the view
   is pulled about. So the step is attached to the command it was taken in:
   - `CL_FinishMove` is hooked (sin.exe +0xF11B, one call per command, the command in `ecx`). After
     it, the step from the body to the head - the **neck**, 8 cm behind and 11 cm below the eyes
     along the head's own axes, so that nodding or looking at the floor is not a step - is turned
     into the world with the body's yaw, kept in a table of 255, and its number written into the
     command's **roll** angle, which SiN's client always sends as 0 (`cl.viewangles[ROLL]` is never
     written). At most 16 units a command; under an eighth of a unit is not worth a number.
   - The client's `Pmove` call in `CL_PredictMovement` (+0x15F2D) is hooked, and the game module
     calls `SinVrPmove(pm, number, gi.Pmove)` instead of `gi.Pmove` (`VR_Pmove`). Both take the
     number out of the command, run the real `Pmove`, then **sweep the box along the step**: sliding
     along what it meets, a stair's height up and back down when it is stopped on the ground, kept on
     the ground going down, and put on the engine's eighth-of-a-unit grid somewhere the box fits.
     `ClientThink` takes the number out of `current_ucmd` before anything reads the angles, and only
     for the first client - another player's numbers would mean nothing here.
   - Steps are only asked for while both sides have run a move in the last half second - the client's
     prediction (so not with `cl_predict 0` or `PMF_NO_PREDICTION`: vehicles, death, cameras) and the
     game module (so not on someone else's server) - and only on foot (`PM_NORMAL`, `PM_ZOOM`).
2. **The view follows the body.** The first time the client takes a command's step, the recentred
   head moves with it (`vr_view_shift`), so the view stays exactly where the head is and the body
   ends up under it. That happens in the frame the command was built: `CL_Frame` sends the command
   (+0x12DC1) and predicts (+0x12DCB) before it draws. A step the world refuses stays between head and
   body; past `vr_lean` the recentred head is moved instead - the room slides - so a head pushed into
   a wall stops 30 cm out from the body's middle, 4 units clear of the wall the 32-unit box is
   touching.
3. **Crouching.** The neck `vr_crouch_depth` below where it was recentred (8 cm back up to stand) sets
   the command's `upmove` to the crouch key's -200 - but not in water, where it would swim down, or on
   a ladder. The game now holds `ps.viewoffset` at the standing 66 while the headset is tracked
   (`CalcViewOffset`; no landing dip, bob or kick either), so the head's own height is the eye's. Two
   things are then the renderer's:
   - **the crouch button** - the engine ducked without the player crouching in the room - lowers the
     whole recentred frame, head and hands alike, as far as SiN's own crouch (36 units, over a tenth
     of a second), but never below its crouching eye (30), so a player already low drops less. The
     hands going down with it fixes something that was wrong before: crouched by the button, the view
     dropped 36 units but the game still shot from 64 above the feet, well above the gun on screen;
   - **the ceiling** - after each prediction a head-sized box is swept up from the body's middle, and
     the eye is kept 5 units under whatever it meets. Only the view is held down, not the hands.

What the desk showed, on `sindm6` with `vr_fake_step` (a pretend step of the head, at once or walked
over some seconds) and `cl_showmiss 1`:

- the box is (-16 -16 0)-(16 16 72), the eye 66 above the origin, as `q_shared.h` says;
- walking 25 m backward, left and forward over six seconds each: the body walked until a wall
  stopped it, with the eye held 11.8 units (`vr_lean`) behind it; slid along that wall 15 m; climbed a
  16-unit step and stopped at the next wall, where the eye dump shows the wall close up and nothing
  through it. 1098 steps asked for, the server ran 1098, 731 refused by walls, the room slid 553
  times - and **not one prediction miss**;
- a pretend 0.5 m crouch: crouching at 0.36 m, the engine ducked, the eye going 66 → 49 → 46 above the
  feet while the engine's own stayed at 66; back to 66 standing;
- `+movedown`: ducked, the eye at 30.1 - lowered 36 - and back to 66.1 on release;
- a snap turn of 90 and back: the eye stayed 2.7 units from the body's middle (the neck behind the
  eyes) and turned about it, the body stayed put - the turn pivots on the player now;
- a pretend 1 m step at once is mostly taken up by the lean - the room slides 0.7 m of it - which is
  what a sudden metre should do; walking is what the body follows.

**Where the menu screen hangs** (2026-10-11). The theatre quad was posed at the LOCAL space's origin
- wherever the headset was when the session began - so a session started with the headset low (on the
desk) put every menu that far down: the owner found menus "way too low". It is placed in front of
the head now (a VIEW space located in LOCAL), level and at head height, each time the screen comes up
after the world and after a recentre. A desk run put the head 2.2 m above the LOCAL origin - which is
how far down the old screen would have been.

**Which way the stick walks** (`vr_move_dir`, 2026-10-11). Until then the stick's forward was the
body's - the way the player faced at the last recentre - because nothing turned it: the owner noticed
once walking about the room made them face elsewhere. `read_sticks` now turns the stick by the head's
yaw (1, the default) or the moving hand's aim yaw (2) from the published poses, keeping the last one
when a pose is not tracked. Desk: with `vr_fake_hand`/`vr_fake_hand_left` at yaw 90 and mode 2, the
stick's forward walked the body 98 units to its left and none forward.

What it cannot show: how it feels. The owner's headset test is for walking into things, leaning over
edges, crouching under something low (the ceiling clamp has only been reasoned about - nothing in
the desk walks went under anything), and whether 0.35 m is the right depth.

## 14. Wages of Sin

2026-10-11. Steam's own Wages of Sin launch runs `sin.exe +set game 2015`, the expansion's folder,
which holds its data (`pak0.sin`, 270 MB) and its own `gamex86.dll`. Neither can be touched, and a mod
folder of our own would normally lose the data - but **sin.exe adds the `2015` folder under any game
folder whose name contains "2015"** (and is not `2015` itself), and sets `game2015 1` as it does for
the expansion (`FS_SetGamedir`, sin.exe +0x54DC0: a `strstr` against "2015", then the `2015`
directory added and `game2015` set; ENGINE_NOTES). So the expansion's VR game module goes in
**`sinvr2015`**, and `+set game sinvr2015` runs Wages of Sin with it - its maps, its weapons, our
code. `deploy.ps1` installs it and writes `SiN VR - Wages of Sin.bat` and a flatscreen one;
`desk-test.ps1 -Game sinvr2015` tests it.

**The code.** `game2015` is Night Dive's separate copy of the game source for the expansion - the
same files, much changed (3,500 lines of `player.cpp` differ). Every VR change in `game` was carried
across by applying `git diff 5236699 -- game/src/code/game` to it with `patch -F3`: 27 of 28 pieces
went in, and `ForceIdle`'s idle hold, whose signature reads `ForceIdle()` there, was put in by hand.
**Every fuzzy placement has to be read**: one of them - `ClientThink` taking the step's number out
of the command - landed inside the comment above the function, and the first desk run showed it:
the client took 65 steps, the server none, and `cl_showmiss` printed a prediction miss each frame.
In its place, the server ran all 65 and the misses stopped.

**One copy of our game code** (same day). Every whole function of ours - the aim from the hand
(`Player::VRWeaponPose`, `GunPosition`, `GetGunOrientation`), swing punches (`Player::VRPunches`,
`Fists::VRPunch`, `VRSwing`, `VRFeelHit` and their helpers) and the idle hold
(`VR_HoldWeaponStill`) - moved out of both game copies into **`src/game_vr/vr_game.cpp`**, which
`game/CMakeLists.txt` compiles into each game DLL against that game's own headers (the game's source
folder is on the target's include path). The script that moved them checked that the two copies of
each block were identical first. What is left in each copy is the ~170 lines that have to sit inside
Night Dive's own functions (a call, a check, a few lines of `Strike`, `ForceIdle`, `MuzzleOffset`),
the same in both - so a new feature goes in `vr_game.cpp` once, and only a new hook needs adding to
both copies. Desk, after: both games walked a pretend metre as 39.4 units with the server running
every step.

**The menus.** The first run in the headset showed SiN's main menu, whose New Game starts SiN: the
expansion's menus are not in its pak but loose, in `2015\locale\en\menus` (with its dialogue and
pictures), and sin.exe adds `<base>\base\locale\<lang>` and `<base>\<gamedir>\locale\<lang>` by the
game folder's own name (+0x54F60) - so `sinvr2015\locale`, which did not exist. `deploy.ps1` makes it
a **junction** to `2015\locale`: nothing copied, nothing of the game's changed, and uninstalling
removes the link with `rmdir` - never a recursive delete, which would follow it into `2015`. With it
the main menu reads "Wages of Sin" in its corner and New Game reaches "Select single player
adventure", Wages of Sin or Sin (desk, by Enter through the menus).

**Steam's Play button.** Steam's launch entries for app 1313 (from `appcacheppinfo.vdf`) are
`sin.exe` with nothing, "Play Wages of SiN" with `+set game 2015`, and "Play CTF" with `+set game ctf`;
the player's Launch Options come after them, and the last `+set game` wins. So
`+set game sinvr2015 +set vid_ref vr +set vr_enable 1` makes every entry the mod's Wages of Sin,
whose New Game offers both adventures. Desk, with Steam's Wages of Sin command line in front: VR,
and New Game reached "Select single player adventure". `game2015` reads 0 at that menu when
`+set game 2015` came first - the game-change handler resets it, even after `+set game2015 1` - and 1
once a level starts, which the expansion's game module forces in `InitGame`; the menus come from
`locale` either way.

What the desk showed, on `2015dm1` (`club-day` starts with the player held for its intro, which
rightly leaves roomscale off): `SiN Gold VR game module` in the console, tracking, the weapon in the
hand, a pretend metre walked as 39.4 units, client and server agreeing on every step.

Not yet looked at: the expansion's own new weapons are not in the weapon table, so they are held from
the general guess until the grip button settles them; and its config is separate from SiN's - the
owner's was copied across once, by hand.
