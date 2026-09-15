# SiN Gold VR

A PC VR mod for **SiN Gold**, Ritual Entertainment's 1998 shooter as re-released by Night Dive Studios.
The goal is a native-feeling VR game: stereo rendering in the headset, 6DoF head tracking, roomscale
movement and tracked motion controllers.

> **Status: early development.** The mod runs the game through its own renderer and game module,
> records how the engine drives the renderer, and can show the game on a virtual screen in the
> headset. Stereo rendering, head tracking and controllers come next (see the roadmap).

## How it works

SiN runs on a modified Quake II engine. The engine (`sin.exe`) and its OpenGL renderer (`ref_gl.dll`)
are closed source. The game logic was released as source by Night Dive Studios in 2020, and is in
[`game/`](game). The mod works within that split, and replaces nothing that ships with the game:

- **`ref_vr.dll`** is a renderer the engine loads when started with `+set vid_ref vr`. It loads the
  stock `ref_gl.dll`, passes the engine's calls through to it, and adds the VR side over OpenXR.
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
| `SiN VR.bat` | VR on: the game appears on a virtual screen in the headset. Start SteamVR first. |
| `SiN VR (flatscreen).bat` | The same mod with VR off. Use it to check that the pass-through changes nothing. |

`tools\deploy.ps1 -Uninstall` removes exactly what was installed. Launching SiN from Steam as normal is
unaffected either way.

These console variables control the mod:

| Cvar | Default | Meaning |
|---|---|---|
| `vr_enable` | 0 | Show the game in the headset. The launchers set it. |
| `vr_screen_distance` | 2.5 | Virtual screen distance, in metres. |
| `vr_screen_width` | 2.4 | Virtual screen width, in metres. |
| `vr_trace` | 1 | Record the renderer calls of each new kind of frame in `sinvr.log`. |

The renderer writes `sinvr.log` beside `sin.exe`, and the game console log goes to `sinvr\qconsole.log`.

**Known issue:** do not apply changes in the game's video options while running through the mod.
Applying them resets `vid_ref` to `gl`, which swaps the VR renderer out.

## Roadmap

1. ✅ Pass-through renderer, game DLL built from source, and a 32-bit OpenXR loader
2. 🚧 Virtual screen in the headset, from an OpenXR session on SiN's own GL context
3. Stereo rendering and head tracking, driven by the engine's own two-pass stereo loop
4. HUD and menus in VR
5. Motion controllers: aiming through the game DLL, and buttons
6. Locomotion, turning and roomscale
7. Weapons held in the hand
8. Wages of Sin
9. Release packaging

## Repository layout

| Path | Contents |
|---|---|
| `src/ref_vr/` | The VR renderer DLL |
| `game/` | Night Dive's SiN Gold game source (SiN and Wages of Sin) plus the CMake build |
| `tools/` | `deploy.ps1`, and `xrprobe` (an OpenXR diagnostic) |
| `cmake/` | Build helpers (the OpenXR loader) |
| `docs/` | Research notes |

## Credits and licences

- **SiN** is by Ritual Entertainment. The **game source** in `game/` is © 2020 Night Dive Studios, used under
  their licence ([game/license.txt](game/license.txt)): non-commercial use with SiN Gold only.
- **OpenXR** is by the Khronos Group. The loader is built from their SDK (Apache-2.0).
- **Design reference:** Team Beef's Quake2Quest and GameOrDie007's Quake II PCVR port. They were studied
  for design only, and no code from them is included.
- The mod's own code (`src/`, `tools/`, `cmake/`) is under a licence still to be decided.
