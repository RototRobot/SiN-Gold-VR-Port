# SiN Gold VR

A PC VR mod for **SiN Gold**, Ritual Entertainment's 1998 shooter as re-released by Night Dive Studios.
The goal is a native-feeling VR game: stereo rendering in the headset, 6DoF head tracking, roomscale
movement and tracked motion controllers.

> **Status: early development.** The mod runs the game through its own renderer and game module,
> renders the world **in stereo with 3DoF head tracking** through an OpenXR projection layer, and
> shows menus and loading screens on a virtual screen. Roomscale movement and motion controllers
> come next (see the roadmap).

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

These console variables control the mod:

| Cvar | Default | Meaning |
|---|---|---|
| `vr_enable` | 0 | Show the game in the headset. The launchers set it. |
| `vr_stereo` | 1 | Render the world once per eye. With 0, everything stays on the virtual screen. |
| `vr_worldscale` | 39.37 | Game units per metre, for head movement. Lower values make the world feel bigger. |
| `vr_screen_distance` | 2.5 | Virtual screen distance, in metres. |
| `vr_screen_width` | 2.4 | Virtual screen width, in metres. |
| `vr_hud_fov` | 60 | How wide the HUD, crosshair and console panel is, in degrees. Smaller brings it closer to the middle. |
| `vr_hud_depth` | 6 | How far away that panel appears, in metres. 0 puts it at infinity. |
| `vr_recenter` | 0 | Set it to 1 to measure the head afresh from where you are standing and facing now. Clears itself. |
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
- View pitch and yaw come from the headset, but the player still moves and turns with the keyboard
  and mouse, and the body does not move with the headset yet.
- Head poses are measured from where you were when you put the headset on. `vr_recenter 1` in the
  console does it again if you end up off-centre.

## Roadmap

1. ✅ Pass-through renderer, game DLL built from source, and a 32-bit OpenXR loader
2. ✅ Virtual screen in the headset, from an OpenXR session on SiN's own GL context
3. ✅ Stereo rendering and head-tracked views, driven by the engine's own two-pass stereo loop
4. ✅ HUD, crosshair and console in a panel at a comfortable distance
5. 🚧 Motion controllers: aiming through the game DLL, and buttons
6. Locomotion, turning and roomscale
7. Weapons held in the hand
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
