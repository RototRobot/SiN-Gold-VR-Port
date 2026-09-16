# SiN Gold engine notes (first familiarization pass, 2026-09-15)

These facts come from the Steam install (app 1313). They were gathered by static analysis
(`dumpbin /disasm` plus string cross-references) and one short launch with `+set logfile 2`.
Addresses are **RVAs** unless marked VA. `sin.exe` (preferred base 0x400000) and
`ref_gl.dll` (0x10000000) both have ASLR enabled, so at runtime an address is the module
base plus the RVA.

## What is open and what is closed

| Component | File | Source? | Notes |
|---|---|---|---|
| Engine: client, server, input, sound, net, Pmove | `sin.exe` | **no** | Night Dive 2020 rebuild ("SiN-Ex", PDB `C:\Devstuff\sin-ex\...\SiN-Ex.pdb`), VS2015 toolset, x86. Reports `version "1.13 x86 Feb 6 2020 Win32 RELEASE"`, protocol 7 |
| OpenGL renderer | `ref_gl.dll` | **no** | Quake 2 `ref_gl` descendant; its only export is `GetRefAPI` |
| Software renderer | `ref_soft.dll` | no | not relevant |
| SiN game logic | `base/gamex86.dll` | **yes** | `sin-ex-game-master/src/code/game` |
| Wages of Sin game logic | `2015/gamex86.dll` | **yes** | `.../src/code/game2015` (+33.6k lines vs `game`) |
| CTF game logic | `ctf/gamex86.dll` | no | only `sdk/ctf.cpp` is in the install |

Steam launch options: **SiN** runs `sin.exe`; **Wages of SiN** runs `sin.exe +set game 2015`;
**CTF** runs `sin.exe +set game ctf`.

The game-source license covers non-commercial use with SiN Gold only (Night Dive Studios, 2020).

## Module layout (Quake 2 lineage)

```
sin.exe ── GetGameAPI(game_import_t) ──► gamex86.dll   server-side game logic, GAME_API_VERSION 14
   │
   └── LoadLibrary("ref_" + vid_ref + ".dll"), GetRefAPI(refimport_t) ──► ref_gl.dll
                                                                             │
             LoadLibrary(gl_driver = "opengl32") + GetProcAddress × 365 ◄────┘  (QGL)
```

- `VID_CheckChanges` (sin.exe 0x9F4C0) formats `ref_%s.dll` with `vid_ref->string` and calls
  `VID_LoadRefresh` (0x9F250). As a result, **`+set vid_ref vr` loads `ref_vr.dll`**, so a proxy renderer
  never has to replace a shipped file. `vid_ref` is archived, so the value ends up in `config.cfg`.
- **Other places that read `vid_ref`'s value.**
  - VID_Init (0x9F6B0) only uses `"gl"` as the default.
  - The HUD layout parser (`ifsoft`, 0x2AB80) and the brightness menu (0x47A90) test for `"soft"` alone.
  - The video menu (0x47090, 0x47620) compares it with `"gl"` and `"soft"`, and **applying video settings
    rewrites it** to one of those.

  So `"vr"` behaves exactly like `"gl"` everywhere except the video menu.
- The game DLL runs inside the local server. It receives `usercmd_t` and produces `player_state_t`, with
  10 Hz server frames (`FRAMETIME 0.1`). The client predicts player movement with the engine's
  own `Pmove`, the same code the game reaches through `gi.Pmove` (`cl_predict 1`).
- The game renders through a GL 4.6 compatibility context (GTX 1080 Ti, NVIDIA 582.66), so FBOs and every
  modern entry point are available once a context exists.

## Renderer interface

`GetRefAPI` (ref_gl 0x13E60) copies a 37-dword `refimport_t` into `ri` (0x95BA0). It then fills a
37-dword (0x94-byte) `refexport_t` with `api_version = 65`. sin.exe copies that into `re`
(**0x1D3060**), checks `api_version == 65`, and calls `re.Init(hInstance, MainWndProc@0x9EBA0)`.

Slot map. Names are inferred; ✔ marks strong evidence.

| Off | ref_gl RVA | Name | Evidence |
|---|---|---|---|
| +0x04 | 0x12DA0 | Init ✔ | called once by VID_LoadRefresh with (hInstance, WndProc) |
| +0x08 | 0x13590 | Shutdown ✔ | called on Init failure / reflib unload |
| +0x0C | 0x0E5E0 | BeginRegistration | `maps/%s.bsp`, `flushmap` |
| +0x10 | 0x0E760 | RegisterModel | Mod_ForName; 21 engine call sites |
| +0x14 | 0x08880 | RegisterSkin | GL_FindImage |
| +0x18 | 0x04AA0 | RegisterPic | `pics/%s` |
| +0x1C | 0x1EFE0 | SetSky | `env/%s%s.tga` |
| +0x20 | 0x0E840 | EndRegistration | |
| +0x24 | 0x124C0 | **RenderFrame(refdef_t\*)** ✔ | calls R_RenderView (`R_RenderView: NULL worldmodel`), R_SetupGL, then R_SetGL2D |
| +0x28 | 0x04B40 | DrawGetPicSize | `pics/%s`, used by SCR_UpdateScreen |
| +0x2C | 0x04D40 | DrawPic | textured quad, `Can't find pic` |
| +0x30 | 0x04B80 | DrawStretchPic | |
| +0x34 | 0x04810 | DrawChar | 17 call sites |
| +0x38 | 0x04F10 | DrawTileClear | |
| +0x3C | 0x05120 | DrawFill | untextured `glColor3f` quad |
| +0x40 | 0x05240 | DrawFadeScreen | `glBlendFunc` + `glColor4f` |
| +0x44 | 0x05730 | ? | clear + `wglSwapBuffers`; 1 caller (0x58920) |
| +0x48 | 0x05840 | no-op stub | the same stub also sits at +0x88 and +0x8C |
| +0x4C | 0x05850 | ? pixel blit | `glRasterPos2f`, `glPixelZoom`, `glDrawPixels`; called from SCR_UpdateScreen |
| +0x50 | 0x053F0 | ? DrawStretchRaw | `glTexImage2D` + quad; no direct engine call site |
| +0x54 | 0x139E0 | ? clear | `glClearColor`, `glClear` |
| +0x58 | 0x13600 | **BeginFrame(float camera_separation)** ✔ | `*** R_BeginFrame ***`, GLimp_BeginFrame, sets up 2D ortho |
| +0x5C | 0x1FD80 | **EndFrame** ✔ | `GLimp_EndFrame() - SwapBuffers() failed!` |
| +0x60 | 0x1FDC0 | AppActivate | only caller is MainWndProc |
| +0x64…+0x84 | various | SiN-specific extras | 3D consoles, fonts, models; many callers |
| +0x90 | 0x0E930 | ? | 3 callers |

### refdef_t

The prefix matches Quake 2 exactly. The evidence is ref_gl's own `r_newrefdef` at 0x95C40, as used in
R_SetupGL and R_RenderFrame:

| Field | Offset |
|---|---|
| x, y, width, height | +0x00, +0x04, +0x08, +0x0C |
| fov_x, fov_y | +0x10, +0x14 |
| vieworg[3] | +0x18 |
| viewangles[3] | +0x24 |
| blend[4] | +0x30 |
| time | +0x40 |
| rdflags | +0x44 |

The rest of the struct (entity, dlight and particle arrays, plus SiN extras) is **still unknown**.

`cvar_t` also follows the Quake 2 layout: `modified` is at +0x10 and `value` at +0x14.

## The built-in stereo path (the most important finding)

Quake 2's quad-buffer stereo support survived in both binaries. The pieces:

- **sin.exe `SCR_UpdateScreen` (0x17270).** It clamps `cl_stereo_separation` to [0,1]. When `cl_stereo`
  is non-zero it renders **two passes**: `re.BeginFrame(-sep/2)` then the view and 2D, then
  `re.BeginFrame(+sep/2)` then the view and 2D again. After both passes it calls `re.EndFrame()` once.
- **ref_gl `R_BeginFrame` (0x13600)** stores `camera_separation` in `gl_state` at 0x95820. It then calls
  **`GLimp_BeginFrame` (0x1FCD0)**. When `gl_state.stereo_enabled` (0x95824) is set, that function picks
  `glDrawBuffer(GL_BACK_LEFT 0x402)` for a negative separation, `GL_BACK_RIGHT 0x403` for a positive one,
  and `GL_BACK 0x405` otherwise.
- **`GLimp_InitGL` (0x1F960)** requests `PFD_STEREO` when `cl_stereo` is set. In Quake 2 this
  function resets `cl_stereo` to 0 through `ri.Cvar_SetValue` when quad-buffer stereo is unavailable, which
  is the case on GeForce cards. The strings and the `cl_stereo` reference are present, but the reset has
  **not yet been observed at runtime** - `ref_vr` keeps `cl_stereo` at 0 across `Init` and never gives it
  the chance.
- **Projection is set only by `glFrustum`**, called in `R_SetupGL` (0x114C0, which also calls
  `glViewport`), `R_DrawAliasModel` (0xC2D0), `SINMDL_DrawModel` (0x19910) and the gl_log wrapper.
- **Other `EndFrame` callers** (sin.exe 0x404D0, 0x46540, 0x485D0, which look like loading screens and menus) render a
  single pass without a pair of BeginFrame calls.

Useful sin.exe RVAs:
- Cvar functions: `Cvar_Get` 0x80570, `Cvar_Set` 0x80920, `Cvar_SetValue` 0x80A20.
- `cvar_t*` globals: `cl_stereo` 0x38AA10, `cl_stereo_separation` 0x38AA48, `cl_predict` 0x3FEC10,
  `cl_gun` 0x3FEC08, `vid_ref` 0x2D2380, `in_joystick` 0x1D8A44.
- Registration: CL_InitLocal-style cvar registration at 0x11D40.
- RenderFrame callers: 0x16C90 (probably `timerefresh`, which also has BeginFrame and EndFrame),
  0x27150 (probably V_RenderView), and 0x48A60 and 0x48E50.

## Observed at runtime (ref_vr call trace, 2026-09-15)

These come from `sinvr.log` in an unattended run: start the game, `map bank`, take two screenshots, quit.

- **A world frame in normal play (`cl_stereo 0`) is a single pass:**
  `BeginFrame(0.0)`, `slot+0x64` ×2–25, `RenderFrame`, `DrawFadeScreen`, then the HUD (`DrawChar`, `slot+0x68`),
  then `EndFrame`.
  - `slot+0x40` ("DrawFadeScreen") runs after *every* world view. So it is more likely SiN's full-screen blend
    than Quake 2's menu fade.
  - `slot+0x64` is also called in between `RegisterModel` calls while a level loads, so it looks like a per-model query.
- **Loading and menu frames** use `slot+0x6C` and `slot+0x70` together with `DrawGetPicSize`.
- **The world view** was a 640×480 viewport at `fov_x` 90.0 / `fov_y` 73.7, with `rdflags` 0.
- **Call counts over the run:** `DrawChar` 14.8k, `slot+0x68` 13.5k, `slot+0x64` 1.1k, `RenderFrame` 409.
- **Where the mod folder's files go.** With `+set game sinvr`, the engine writes its config, saves and screenshots under
  `sinvr\players\blade\`. The normal `base` config is never touched.
- **The level-start card, and the "load stall" that wasn't.** After the last precache line (`sky`),
  `sin.exe` shows a full-screen card - Blade against a circuit board, with "PRESS ANY KEY TO CONTINUE" in
  red - and waits for a key. While it is up:
  - the renderer sees one frame (`DrawGetPicSize`, a pic, `EndFrame`) and then nothing;
  - **the console command buffer does not run**, so a `+exec` script stops there and never reaches its
    `quit`;
  - the main thread spins at 100% of one core (sampled with `Get-Process sin | % Threads`).

  Unattended runs therefore appeared to stall for 8 s to over four minutes, depending on when a stray
  keypress happened to reach the window; a stock launch behaved the same way (7.4 s, 6.8 s, 42.4 s for
  `map bank`, against 8.0 s, 11.5 s and 9.0 s through the mod), which made it easy to write off as SiN
  being slow to load. It is not: a full-screen capture taken during one of those "stalls" showed the card.
  `tools\desk-test.ps1` posts a HOME keypress (unbound in the stock config) to the game window every
  second, and a full VR run then takes 13 s.
  - The string in the data files is `"PRESS ANY KEY TO CONTINUE"`; `base\pak0.sin` also holds
    `"PRESS ANY KEY TO RETURN"`, which belongs to the in-level computer terminals, not to this card.
- **Missing renderer.** If `vid_ref` is `vr` but `ref_vr.dll` is missing, the engine prints `LoadLibrary("ref_vr.dll") failed` and
  falls back to `ref_gl.dll` by itself.

## The stereo path confirmed at runtime (2026-09-16)

`ref_vr` now sets `cl_stereo 1` and `cl_stereo_separation 0` while the headset is running, and the traces
show the engine behaving exactly as the disassembly suggested:

- A world frame becomes **two `BeginFrame` ... `RenderFrame` groups followed by one `EndFrame`**. Both
  passes get `camera_separation` 0 - the first as -0.0 and the second as +0.0, which is the sign
  `GLimp_BeginFrame` would have used to pick `GL_BACK_LEFT`/`GL_BACK_RIGHT`. Counting the passes within a
  frame is therefore the reliable way to name the eye.
- **Both passes redraw the 2D overlay** (`DrawFadeScreen`, `DrawChar` ×78, the HUD slots), so the HUD is
  per-eye for free - though at whatever viewport the eye buffer has.
- The engine reads `cl_stereo` once as the frame starts, so changing it at `EndFrame` takes effect on the
  next frame, and a frame's pass count is known from the value read before it.
- **Frames with no world in them stay single-pass:** menus, the loading plaque and the console over nothing.
  Over a 637-frame run, 406 frames were stereo and 231 were not.
- `RenderFrame` is called twice per stereo frame with the *same* `refdef_t`, which is why the eye view is
  applied to it and then restored (814 `RenderFrame` calls for 637 frames).
- Both eyes at 2444×2392 on a GTX 1080 Ti ran at about 87 frames/s in `bank`.

## Renderer and client behaviour found while building the HUD panel (2026-09-16)

- **`DrawFadeScreen` (slot `+0x40`, ref_gl+0x05240) is `void (float r, float g, float b, float a)`.** Both
  call sites in sin.exe push 16 bytes and clean up with `add esp,10h`, so it is cdecl with four floats, and
  the function reads them at `[ebp+8]`..`[ebp+14h]`. With an alpha of 1 or more it skips enabling blending
  and paints opaque; otherwise it blends `GL_SRC_ALPHA`/`GL_ONE_MINUS_SRC_ALPHA`. This settles the old
  guess: it is SiN's screen blend, not a menu fade.
- **`glOrtho` is resolved by name** through ref_gl's QGL layer, like `glFrustum`, `glViewport` and
  `glScissor`, so the 2D phase can be intercepted the same way. ref_gl sets the viewport *before* the
  projection call that says what the viewport is for, in both phases.
- **SiN pauses itself when its window is not the active one,** and while paused it paints the whole view
  over (an opaque `DrawFadeScreen`) and draws a PAUSE sign. An unattended run in the background therefore
  renders no world at all. The cvar is `paused`; `cl_paused` and `sv_paused` do not exist.
- **An unknown console command is forwarded to the server and comes back as chat** - `cl_paused 0` shows up
  in the console as `Blade: cl_paused 0`. Silent failure for a typo in a test script.
- **The level-start card leaves the console down** over the view often enough to matter when reading a
  rendered frame; escape then opens the menu rather than closing anything, because by that point the
  key destination is the game again.

## Game DLL: what matters for VR

- **Aiming.** `Weapon::GetMuzzlePosition` (weapon.cpp:324) calls `owner->GunPosition()` and
  `owner->GetGunOrientation()`. `Sentient` implements both (sentient.cpp:157/169) from the
  origin plus `gunoffset` and the entity orientation. `Actor` overrides them and `Player` does not. That makes
  them the natural hook for controller aiming.
- **View.** `Player::CalcViewOffset` (player.cpp:3181) adds weapon kick, damage and fall pitch, run pitch and
  roll, and bob. `Player::CalcGunOffset` (:3318) adds gun bob and sway.
  `Player::SetCameraValues` (:4016) forces the view for cinematic cameras (a comfort problem in VR).
- **Units.** `STAND_HEIGHT 72`, `STAND_EYE_HEIGHT 66`, `CROUCH_EYE_HEIGHT 30`, which works out to roughly 1 unit
  per inch (about 39.4 units per metre).
- **`usercmd_t`.** Fields are `msec`, `buttons` (ATTACK 1, USE 2, JUMP 4 in WoS, ANY 128), `angles[3]` (short),
  `forwardmove`/`sidemove`/`upmove`, `impulse`, `lightlevel` and `debris`.

## Building the game DLLs

The projects target v140 and Windows SDK 8.1, and neither is installed. Override both:

```
MSBuild src\code\vc2015\gamex86\gamex86.vcxproj /p:Configuration=Release /p:Platform=Win32 ^
        /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.26100.0
```

`gamex86` and `gamex86_wos` both build cleanly, in about 20 s each with 1 warning each (VS2022 Build
Tools 17.14). Note that both projects name their output `gamex86.dll`, so each needs its own OutDir.

## Environment

- **Display and VR.** GTX 1080 Ti, 4K monitor, HP Reverb G2 through SteamVR (WMR "Oasis" driver).
  SteamVR ships the 32-bit `bin\vrclient.dll`.
- **OpenVR.** The 32-bit runtime is in `openvr-master/bin/win32/openvr_api.dll`, with its import library in `lib/win32/`.
- **Current game config.** `gl_mode 0` (640×480 windowed), `gl_swapinterval 1`, `cl_maxfps 200`,
  `cl_stereo 0`, `cl_stereo_separation 0.4`.
- **DXVK is not applicable.** SiN renders with OpenGL, and OpenVR accepts GL textures directly.

## VR architecture (1-3 built; see HANDOFF §9 for what was built)

`Q2VR_REFERENCE.md` covers how Team Beef's Quake II port solved each of these pieces, and how each
maps onto SiN's closed engine. Steps 1-3 are built and running on OpenXR rather than OpenVR; 4 and 5 are
still ahead.

1. **Proxy renderer `ref_vr.dll`**, selected with `+set vid_ref vr`. It loads the stock `ref_gl.dll`,
   wraps `refimport_t` (for example to veto the `cl_stereo 0` reset) and wraps the export slots.
2. **Drive the engine's own stereo loop** with `cl_stereo 1`:
   - `BeginFrame(sep)`: bind the left or right eye FBO according to the sign of `sep`.
   - `RenderFrame`: override `vieworg`, `viewangles` and `fov` for each eye from the HMD pose.
   - `Draw*`: route 2D drawing to a HUD layer.
   - `EndFrame`: submit both eyes to OpenVR and blit a mirror to the window. Keep the swap interval at 0 so that
     `WaitGetPoses` paces the frame loop.
3. **Per-eye asymmetric projection** by intercepting `glFrustum`. The proxy can hook ref_gl's
   `GetProcAddress` import while QGL resolves names, or `gl_driver` can point at a thin forwarding GL DLL.
4. **Game DLL changes** for controller aiming (Player overrides of `GunPosition`/`GetGunOrientation`),
   comfort (no bob, roll or kick), roomscale offsets, and weapons attached to the hands.
5. **Input** has to be injected because the client is closed. Buttons can be sent as commands (`+attack`, …), for example
   through `ri.Cmd_ExecuteText`. Analog movement and turning can come from joystick emulation (sin.exe imports
   `joyGetPosEx`) or, later, from a hook on the client's command builder.

## Open questions and next checks

- ~~Does a pass-through `ref_vr.dll` run identically, and does the video menu leave `vid_ref vr` alone?~~
  Yes, except that *applying* video settings rewrites `vid_ref`.
- ~~Does a source-built `gamex86.dll` run from a mod folder (`+set game sinvr`)?~~ Yes. WoS still needs its
  own plan because its game dir is `2015`.
- ~~Observe `cl_stereo 1` at runtime.~~ Done: see "The stereo path confirmed at runtime" above.
- Map the rest of the `refdef_t` and `entity_t` layouts (needed for hand and weapon models).
- Is the level-start card the engine's or the game DLL's? It is drawn by `sin.exe`, but what dismisses it,
  and whether a cvar can skip it, is still unknown. Relevant to VR: while it is up, no world is drawn.
- How to handle cinematic cameras (`SetCameraValues`) and `.ogv` movies in VR.

## How these facts were obtained (reproducible)

1. Dump the disassembly: `dumpbin /disasm /out:x.asm <module>`. dumpbin lives under
   `VS2022 BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x86\`.
2. Map string file offsets to VAs using the section table, then search the disassembly for `push <va>h`.
3. Name QGL pointers by binding each pushed `"glXxx"` string to the call that follows it. The compiler
   schedules the *next* push before the pointer stores, so pairing a store with the nearest push is off by one.
