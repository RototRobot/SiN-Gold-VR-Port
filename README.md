# SiN Gold VR

A PC VR mod for **SiN Gold**, Ritual Entertainment's 1998 shooter as re-released by Night Dive Studios.
The goal is a native-feeling VR game: stereo rendering in the headset, 6DoF head tracking, roomscale
movement and tracked motion controllers.

> **Status: early development.** The mod runs the game through its own renderer and game module,
> renders the world **in stereo with head tracking** through an OpenXR projection layer, keeps the HUD
> in a readable panel, **plays with the VR controllers**, and **puts the weapon in your hand**: it is
> drawn where the controller is, it fires down its own barrel, and its muzzle flash, tracers and
> bullet holes come from there too. Comfort and roomscale come next (see the roadmap).

**New to the project?** Start with [docs/HANDOFF.md](docs/HANDOFF.md).

## How it works

SiN runs on a modified Quake II engine. The engine (`sin.exe`) and its OpenGL renderer (`ref_gl.dll`)
are closed source. The game logic was released as source by Night Dive Studios in 2020, and is in
[`game/`](game). The mod works within that split, and replaces nothing that ships with the game:

- **`ref_vr.dll`** is a renderer the engine loads when started with `+set vid_ref vr`. It loads the
  stock `ref_gl.dll`, passes the engine's calls through to it, and adds the VR side over OpenXR. The
  world is rendered twice per frame through the engine's own stereo loop, once into each eye's
  headset buffer, with the view taken from the headset. The HUD, crosshair and console go into a
  panel in front of you rather than across the whole field of view.
- **`sinvr\gamex86.dll`** is the game logic, built from Night Dive's source. It is where VR gameplay
  changes (aiming, comfort) will go. The engine loads it for `+set game sinvr`.

The client that reads input is closed, so the controllers reach the game the way a player's own key
binds and joystick would: buttons run console commands, and the thumbsticks arrive as a joystick the
engine polls for itself.

Background: [docs/ENGINE_NOTES.md](docs/ENGINE_NOTES.md) (how the engine fits together),
[docs/OPENXR_NOTES.md](docs/OPENXR_NOTES.md) (the 32-bit OpenXR path) and
[docs/Q2VR_REFERENCE.md](docs/Q2VR_REFERENCE.md) (lessons taken from Quake II VR ports).

## Requirements

- **SiN Gold** from Steam or GOG: the 2020 Night Dive release.
- **Windows 10 or 11** and a PC VR headset with an OpenXR runtime that supports **32-bit**
  applications, since SiN is a 32-bit game. SteamVR does.

## Building

You need:

- **Visual Studio 2022**, or its Build Tools, with *Desktop development with C++*. That includes CMake.
- **Python 3**. The OpenXR loader generates some of its source while it builds.
- **The OpenXR SDK source.** By default the build uses `..\OpenXR-SDK-Source-release-1.1.63` beside this repository.
  If that folder is missing, it fetches the pinned release from GitHub instead. Pass `-DSINVR_OPENXR_SDK=<path>` to use
  another copy.

Then run:

```
cmake --preset win32
cmake --build --preset release
```

If `cmake` is not on your PATH, the copy bundled with VS 2022 is at
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.

The output goes to `build\win32\stage\Release\`, laid out like the game folder.

## Installing and running

```
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1
```

By default this installs into the Steam folder; pass `-GameDir <folder>` for another location. It copies:

- `ref_vr.dll` and `openxr_loader.dll` beside `sin.exe`
- `gamex86.dll` into `sinvr\`

It also adds two launchers beside `sin.exe`:

| Launcher | What it does |
|---|---|
| `SiN VR.bat` | VR on: the world is drawn in stereo in the headset, menus on a virtual screen. Start SteamVR first. |
| `SiN VR (flatscreen).bat` | The same mod with VR off. Use it to check that the pass-through changes nothing. |

`tools\deploy.ps1 -Uninstall` removes exactly what was installed. Launching SiN from Steam as normal is
unaffected either way.

## Controls

| Control (Reverb G2) | Does |
|---|---|
| Left stick | Move and strafe |
| Right stick, left/right | Turn |
| Right trigger | Fire |
| Left trigger | Use |
| Right **A** | Jump |
| Left **X** | Crouch |
| Right **B** | Next weapon |
| Left **Y** | Previous weapon |
| Left grip | Run |
| Right stick, click | Opens and closes the game menu (Escape) |
| Right stick, up/down, while adjusting a weapon | Makes it bigger or smaller |
| Right grip, held | Lets go of the weapon so you can take it another way - see below |
| Any button, on "PRESS ANY KEY TO CONTINUE" | Continues |
| Left stick, click twice | Recentre: the way you are standing and facing becomes forward |

On the Reverb G2 the two **menu** buttons never reach the game: SteamVR keeps them for its own
dashboard, and a press takes the input focus away instead. So the menu is on the right stick's click.
They are still bound, for controllers whose runtime does pass them on. The Windows button beside them
is the runtime's own and is never offered to an application at all.

**Left-handed.** `vr_left_handed 1` puts the weapon in the left hand and gives each hand the other's
buttons and stick: fire is the left trigger, use the right, the menu is the left stick's click, and so
on down the table. The weapon is drawn mirrored down its own middle, so the side of it that was
modelled is the side you see and the barrel it fires from moves with it. What a weapon remembers about
how you hold it is kept as the right hand would hold it and mirrored as it is used, so a weapon you
have settled in one mode is settled in the other too. The setting is read when the renderer starts:
set it and run `vid_restart`, or set it before launching.

**Putting a weapon where you want it.** Every weapon starts from a measured guess at where your hand
holds it and how big it should be. To change one, hold it and **squeeze the grip of the hand it is in**
(the right, or the left if you are left-handed): after a moment the weapon stops following your hand
and hangs in the air. **Your controllers appear while you do it**: a cross where each hand is holding,
its arms along the way the hand is turned, and a line out of the front of the controller showing where
it points. Line the cross up with the part of the gun you want in your hand and the line up with the
barrel. The weapon also stops breathing while you do this - its idle animation is held still, so the
only thing moving is your hand. Move your hand to hold it the way you want - and push that hand's
stick up or down to make it bigger or smaller - then let go of the grip.
It is remembered for that weapon, and kept between sessions. `vr_weapon_reset 1` in the console puts
the weapon you are holding back to its first guess.

In a menu, on the loading screen or with the console up, the controllers turn into a keyboard instead:
the sticks are the arrow keys, the trigger is Enter, and anything else is Escape. The mod knows which
it is from the engine, so the player does not move while a menu is open.

Other headsets get the same layout where their controllers have the buttons for it; Touch and Index
controllers are covered, and anything else falls back to trigger, menu and the sticks.

Each one is a cvar, so it can be rebound to any console command without rebuilding: `vr_bind_fire`,
`vr_bind_use`, `vr_bind_jump`, `vr_bind_crouch`, `vr_bind_weapon_next`, `vr_bind_weapon_prev`,
`vr_bind_run`, `vr_bind_menu`. A `+command` is released as its `-command`, exactly as a key bind would
be. A binding of `key:escape`, `key:enter`, `key:up`, `key:down`, `key:left` or `key:right` taps that key
instead of running a command, which is how the menu button works - SiN's menu has no console command,
only the escape key. Setting a binding to nothing unbinds it.

These console variables control the mod:

| Cvar | Default | Meaning |
|---|---|---|
| `vr_enable` | 0 | Show the game in the headset. The launchers set it. |
| `vr_stereo` | 1 | Render the world once per eye. With 0, everything stays on the virtual screen. |
| `vr_resolution` | 1 | How big each eye is rendered, on each side, against the size the headset's runtime recommends. 1.2 is sharper and costs 44% more pixels; 0.8 is softer and faster. Takes effect straight away. |
| `vr_worldscale` | 39.37 | Game units per metre, for head movement. Lower values make the world feel bigger. |
| `vr_screen_distance` | 2.5 | Virtual screen distance, in metres. |
| `vr_screen_width` | 2.4 | Virtual screen width, in metres. |
| `vr_hud_fov` | 60 | How wide the HUD, crosshair and console panel is, in degrees. Smaller brings it closer to the middle. |
| `vr_hud_depth` | 6 | How far away that panel appears, in metres. 0 puts it at infinity. |
| `vr_recenter` | 0 | Set it to 1 to measure the head afresh from where you are standing and facing now. Clears itself. |
| `vr_input` | 1 | Take input from the VR controllers. |
| `vr_deadzone` | 0.15 | How far a stick must move before the game sees it. |
| `vr_recentre_double` | 1 | Require two clicks of the left stick to recentre. 0 makes one do. |
| `vr_aim` | 1 | Shots come from the weapon in the hand rather than from the player's angles. |
| `vr_weapon_hand` | 1 | Draw the weapon in the hand instead of in front of the camera. |
| `vr_left_handed` | 0 | 1 puts the weapon in the left hand, mirrored, and swaps the two controllers' buttons and sticks. Read when the renderer starts, so `vid_restart` after changing it. |
| `vr_show_hands` | 1 | Draw the controllers - a cross and the line they point along - while a weapon is being placed. 2 draws them all the time, 0 never. |
| `vr_freeze_idle` | 0 | 1 holds the weapon's idle animation still in play as well - no breathing, no swaying arm. It is always held while a weapon is being placed. |
| `vr_grip_<weapon>` | empty | Where that weapon sits in the hand: offset, angles and size. The grip button writes it; empty means the measured guess. |
| `vr_weapon_reset` | 0 | Set it to 1 to forget the setting for the weapon in hand. Clears itself. |
| `vr_crosshair` | 1 | SiN's crosshair follows the gun while it is in the hand. 0 leaves it out; 2 leaves it in the middle of the view, where SiN draws it. |
| `vr_client_bullets` | 0 | 1 puts back the client's own bullet impacts, which are drawn from the eye rather than from the barrel. |
| `vr_force_render` | 0 | Draw for the headset even while the runtime says it is idle. For testing with the headset off. |
| `vr_dump_eye` | 0 | Set it to 1 to write the next frame's two eye images beside `sin.exe`, for checking a build at the desk. |
| `vr_trace` | 1 | Record the renderer calls of each new kind of frame in `sinvr.log`. |

While stereo is on, the mod sets `cl_stereo 1`, `cl_stereo_separation 0`, and switches off view bob
and roll (`bob_up`, `bob_pitch`, `bob_roll`, `run_pitch`, `run_roll`, `sv_rollangle`), which move the
camera without the head moving.

The renderer writes `sinvr.log` beside `sin.exe`, and the game console log goes to `sinvr\qconsole.log`.

**Known issues**

- Do not apply changes in the game's video options while running through the mod. Applying them
  resets `vid_ref` to `gl`, which swaps the VR renderer out.
- Every weapon's place in the hand starts from a guess measured off the model. Settle the ones you use
  with the grip button, as above.
- The crosshair follows the gun rather than the head: it is SiN's own crosshair, drawn in the direction
  the weapon points, and both eyes see it at the same angle, so it sits out there like a red dot rather
  than on a pane of glass. Point the gun away from where you are looking and it leaves the view, which
  is what it should do.
- Most of SiN's two-handed weapons only ever modelled the left hand, on the fore-end, so that is the
  hand you see on them; the fists are the one weapon with two arms, and they get one each.
- The weapon's idle animation is what makes it breathe and sway. A weapon on a controller already
  moves with the hand, so `vr_freeze_idle 1` holds that animation on its first frame. Only the idle is
  held: firing, reloading and putting the weapon away animate as they always did, and the muzzle stays
  where the model has it.
- A mirrored weapon - left-handed mode - is lit as its mirror image was: SiN shades a model from its
  own list of normals, which the mirror does not reach, so the light on the gun falls where it would
  have fallen in the right hand. Nothing else about it is out of place, and the fists, having an arm
  each side already, are not mirrored at all.
- The weapon is put into the frame by sin.exe itself, through two calls this mod redirects, which is
  what carries its muzzle flash, tracers and shell cases along with it. Both are checked against the
  Steam build before anything is changed, so another build of `sin.exe` is left alone - the weapon then
  still goes to the hand, but its effects stay at the eye.
- The body does not move with the headset yet: walking and turning are the sticks' job.
- **The controllers only work while the headset is awake.** OpenXR gives an application controller input
  only while it has the input focus, and SteamVR takes the focus away when the headset goes to sleep -
  put it down on a desk and the game stops hearing the controllers. For testing with the headset off your
  head, stop it sleeping: turn off SteamVR's option to pause VR when the headset is idle, or cover the
  proximity sensor between the lenses.
- **Raising `vr_resolution` in the middle of a session can be refused** after it has been lowered in
  the same session - SteamVR will not make the bigger buffers, even at 1. The picture stays at the last
  size that worked, the console says so, and `vid_restart` makes them afresh at the size asked for.
  Setting it once and restarting never runs into this.
- Head poses are measured from where you were when you put the headset on. `vr_recenter 1` in the
  console does it again if you end up off-centre.

## Roadmap

1. ✅ Pass-through renderer, game DLL built from source, and a 32-bit OpenXR loader
2. ✅ Virtual screen in the headset, from an OpenXR session on SiN's own GL context
3. ✅ Stereo rendering and head-tracked views, driven by the engine's own two-pass stereo loop
4. ✅ HUD, crosshair and console in a panel at a comfortable distance
5. ✅ Motion controller input: sticks to move and turn, buttons bound to commands
6. ✅ Tracked hands: the weapon is drawn in the hand, fires down its own barrel, and carries its
   effects with it; each weapon's grip can be set in the headset, and left-handed players get the
   mirror image of all of it
7. Roomscale comfort: snap turning, crouching, and the body following the head
8. Wages of Sin
9. Release packaging

## Repository layout

| Path | Contents |
|---|---|
| `src/ref_vr/` | The VR renderer DLL |
| `game/` | Night Dive's SiN Gold game source (SiN and Wages of Sin) plus the CMake build |
| `tools/` | `deploy.ps1`, `desk-test.ps1` (an unattended run), and `xrprobe` (an OpenXR diagnostic) |
| `cmake/` | Build helpers (the OpenXR loader) |
| `docs/` | Research notes |

## Credits and licences

- **SiN** is by Ritual Entertainment. The **game source** in `game/` is © 2020 Night Dive Studios, used under
  their licence ([game/license.txt](game/license.txt)): non-commercial use with SiN Gold only.
- **OpenXR** is by the Khronos Group. The loader is built from their SDK (Apache-2.0).
- **Design reference:** Team Beef's Quake2Quest and GameOrDie007's Quake II PCVR port. They were studied
  for design only, and no code from them is included.
- The mod's own code (`src/`, `tools/`, `cmake/`) is under a licence still to be decided.
