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
- **During the level-start card the engine calls the renderer not at all, but keeps pumping window
  messages** - that is how the keyboard reaches the card. A `WM_TIMER` with a `TIMERPROC` on the engine's
  window therefore still runs, on the engine's thread, between its own work: the mod's idle pump lives
  there (HANDOFF §11). Measured: the card came up about 5 s into `map bank` and the timer ran throughout.
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

## The import table, and how input gets in (2026-09-16)

**`refimport_t` is Quake II's, with two extra entries before the cvar calls.** sin.exe fills it on the
stack at 0x49F2DE onwards, which gives the layout directly. By slot, with sin.exe RVAs:

| Slot | RVA | What it is |
|---|---|---|
| 0 | 0x09E980 | `Sys_Error` |
| 1 | 0x077250 | `Cmd_AddCommand` |
| 2 | 0x077300 | `Cmd_RemoveCommand` |
| 3 | 0x076E00 | `Cmd_Argc` - returns a global int |
| 4 | 0x076E10 | `Cmd_Argv` - bounds-checked array of strings |
| 5 | 0x076360 | **`Cbuf_ExecuteText(exec_when, text)`** - switch on arg 0: 0 now, 1 next, 2 queued |
| 6 | 0x09E8C0 | `Con_Printf` |
| 7-11 | 0x016440, 0x054120, 0x0541B0, 0x054630, 0x0548D0 | file system and friends, not yet named |
| 12 | 0x080570 | `Cvar_Get` |
| 13 | 0x080920 | `Cvar_Set` |
| 14 | 0x080A20 | `Cvar_SetValue` |

Slot 5 is what lets the renderer act as a player: queueing `+attack` there is indistinguishable from a
key bind firing.

**Quake II's joystick support is intact, and nothing else uses it.** sin.exe imports exactly the three
winmm calls `IN_StartupJoystick` needs - `joyGetNumDevs`, `joyGetDevCapsA`, `joyGetPosEx` - and all of
Quake II's joystick cvars are in the binary (`joy_advanced`, `joy_advaxisx`..`joy_advaxisv`,
`joy_forwardsensitivity`, `joy_yawsensitivity`, the thresholds, and the `joy_advancedupdate` command).
Answering those three imports in-process presents a joystick that does not exist, and the engine then
does the movement work itself. The console says `joystick detected` where it used to say
`joystick not found -- no valid joysticks (a5)` (0xa5 is `JOYERR_PARMS`).

- Axis values are raw, 0..65535, and the engine reads `value / 32768 - 1`, so 32768 is the centre.
- With `joy_advanced 1` the mapping comes from the `joy_advaxis*` cvars: 1 forward, 2 look, 3 side,
  4 turn. `AxisTurn` adds to the yaw, which grows to the *left*, so a rightward stick has to be negated.
- Buttons are better left at zero: the engine would turn them into `K_JOY*` key events, and whatever the
  player has bound to those would fire as well.

**Other client behaviour worth knowing:**

- **A `wait` stops the command buffer for that frame,** including anything appended behind it. A cfg full
  of `wait`s therefore swallows commands the renderer queues, until it drains.
- **Single-player levels open on a cinematic** that holds the player still, and the camera in `refdef_t`
  is the cinematic's, not the player's. Deathmatch maps (`sindm6`, `biodm`, `oilrigdm`, `sindm9` with
  `deathmatch 1`) start the player in control, which is what input tests want.
- **There is almost no air control**, as in Quake II, so a test that presses a movement key while the
  player is still falling from a spawn point measures nothing.
- `viewpos` prints the player's position and yaw, which is the cheapest way to measure input from a
  script.

**Menus, and how to tell one is up (2026-09-16):**

- **`cls.key_dest` is at sin.exe+0x38AAA4.** Found from the `toggleconsole` handler at 0x427760, which
  compares it with 1 (`key_console`) and sets it to 0 (`key_game`) beside a `Cvar_SetValue("paused", 0)`.
  Of its 50 references, the writes are 0, 1, 2, 4 and 5 - so SiN extended Quake II's four-value enum.
  Measured at runtime: **0 during play, 5 while the menu is up**, back to 0 when it closes.
- **There is no `togglemenu` command.** The string is not in the binary, and sending it goes to the
  server and comes back as chat. The stock config binds it to escape anyway, which works because the
  engine handles the escape *key* itself. `menu_main`, `menu_loadgame`, `menu_quit` and the other
  `menu_*` commands do exist.
- **The menu is drawn over a world that is still being rendered,** through SiN's own renderer slots
  (`+0x68`, `+0x6C`, `+0x70`, `+0x74`) rather than through `DrawPic`, and the HUD is drawn underneath it.
  So a menu frame looks like a gameplay frame in the renderer's traces, apart from call counts. That is
  why `key_dest` is worth reading.
- **`DrawPic` is `void (int x, int y, char *name)` and `DrawStretchPic` is
  `void (int x, int y, int w, int h, char *name)`** - both Quake II's, confirmed from the disassembly
  (each passes its last argument to `Draw_FindPic` at ref_gl+0x04AA0). Wrapping them gives the name of
  every 2D pic the engine draws, which `vr_trace` logs once each.
- **Arrow keys and Enter drive the menu,** and Escape opens and closes it. Posting `WM_KEYDOWN`/`WM_KEYUP`
  to the engine's window with the right scan code and the extended bit works: the menu's selection moved
  round its wheel exactly as it would from the keyboard.

## The frame's entity list, and the view weapon (2026-09-19)

- **`refdef_t` is 0x78 bytes**: ref_gl's R_RenderView copies exactly 30 dwords of it into its own
  `r_newrefdef` (ref_gl+0x95C40, found from the `rdflags & RDF_NOWORLDMODEL` test at +0x44). The entity
  count and list are at +0x50 and +0x54, as in Quake II; R_DrawEntitiesOnList walks the list in steps
  of 0xC8.
- **`entity_t` is 0xC8 bytes.** Read off the view weapon at runtime and out of the code that fills and
  draws it: `model` +0x00 (ref_gl's `model_t`, which begins with its name, so the view weapon's name can
  be read straight off it), `angles` +0x04, `origin` +0x10, `frame` +0x1C, `oldorigin` +0x20, `oldframe`
  +0x2C, `backlerp` +0x30, `alpha` +0x3C, `flags` +0x48, a 3x3 orientation matrix at +0x4C with rows
  forward, left, up, an id at +0x88 that attachments refer to, `groups[24]` at +0x8C, the animation at
  +0xA8, `scale` at +0xAC, and the gravity axis at +0xC4.
- **+0xAC is the model's scale.** SiN's model drawer (ref_gl+0x19910) copies it into a global that the
  vertex setup (+0x18420) multiplies every vertex and every frame offset by, and the client's
  `GetBoneTransform` takes it as its scale argument - so a bone stays where the drawn model has it.
  (An earlier pass through here concluded it was not a scale. It is; two eye dumps at 1.0 and 0.35 with
  nothing else changed settle it.)
- **`groups[24]` is per-group render flags**, OR-ed into each group's own (`MDL_GROUP_NODRAW` 0x80 is
  honoured: the draw loop skips a group whose flags have bit 7). The index is not into the `.sbm`'s
  groups, though - it is into the groups the model's `.def` *names*, each of which lists the `.sbm`
  groups it covers. A model whose `.def` names none, like the fists, cannot have parts switched off this
  way; a clip plane is the way to cut one in half.
- **The view weapon's flags are 0x47**: `RF_MINLIGHT | RF_VIEWERMODEL | RF_WEAPONMODEL | RF_FRAMELERP`.
  SiN has no depth hack for it - bit 4 is `RF_ENVMAPPED` here - so it is an ordinary world-space model,
  and moving it is just a matter of setting its fields.
- **Models are placed with `glTranslatef` and turned with `glRotatef`** from `angles`, inside a
  `glPushMatrix`/`glPopMatrix` pair with nothing else pushed in between - which is what makes a clip
  plane set at the translate and cleared at the pop apply to exactly one model. `R_RotateForEntity`
  (ref_gl+0xEA00) is translate, `glRotatef(yaw, z)`, `glRotatef(-pitch, y)`, `glRotatef(-roll, x)`;
  the model drawer negates the pitch around it, so the entity's `angles` are in the same sense as the
  player's view angles (pitch positive looks down), and the axis rows at +0x4C are the true orientation.
  The three rotations are unconditional - straight line, no branches, all through the same QGL pointer
  as the translate - so counting them is a reliable way to know when the matrix has reached the model's
  own frame. That is where `vr_gl_mirror_model_at` puts its `glScalef(1, -1, 1)` and its swap of the
  winding, to draw a model mirrored down its own middle for the other hand.
- **A view weapon's `.def` names its animations, and its client effects hang off their frames.**
  `view_magnum.def` has `ready`, `fire`, `idle1`, `idle2`, `reload`, `putaway` and a silenced set of
  each, and lines like `client fire first attach barrel sprites/gunblast.spr` and
  `client fire first shootbullets 1 10 10 10 8192` - so the muzzle flash, the shells and the client's
  own bullets are commands on the *first frame of the fire animation*. Holding the idle animation
  still leaves all of that alone.
- **The barrel the game fires from does not move with the animation.** `!init:` carries
  `server setaimanim idle1 0`, which sets the `aimanim`/`aimframe` that `Weapon::MuzzleOffset` reads in
  place of the current frame: the muzzle is measured from idle1's first frame whatever the weapon is
  doing. Freezing the drawn idle therefore brings the picture closer to where shots come from, not
  further away.
- **A mirrored model is lit unmirrored.** Shading comes from each vertex's normal index through the
  engine's own table, and a mirror in the matrix does not reach it, so a mirrored weapon carries its
  original vertex shades - the light on it appears to come from the other side. Visible if looked for,
  not in play.
- **The crosshair is a 2D pic**, `ch1_off` for the default one, drawn through `DrawPic`.

## Drawing a model's parts, and adding entities of our own (2026-09-27)

- **`entity_t.anim` numbers a model's animations alphabetically by name**, not in the `.def`'s order.
  The fists came up as 9 (`ready1` - after fire1-5, idle1-3 and putaway1) and the magnum's ready,
  putaway and reload as 4, 3 and 5; idle1 on the fists is 5.
- **`V_AddEntity` (+0x25520) gives an entity whose id is `0x1E61` a fresh id** from a counter at
  +0x14A110 (capped at 0x1F000000). An entity with a parent (+0x70 non-zero) is noted in the
  attachment table, by parent and by its own id - which is how a muzzle flash stays on its gun. A copy
  of the view weapon must not keep the weapon's id.
- **SiN's model drawer sends every strip in immediate mode.** In ref_gl+0x18420 each glcmd strip or fan
  is `glBegin(GL_TRIANGLE_STRIP/FAN)`, then for each vertex `glTexCoord2f`, `glColor4f` and
  `glVertex3fv`, then `glEnd`; the pointer given to `glVertex3fv` is `array + 16 * index`, the array
  named by the `lea eax, [edx + 0x100C6D00]` at ref_gl+0x18A58 and the index the model's own vertex
  number from the `.sbm`'s glcmds. A few other passes (a second one for envmapped groups, some debug
  lines) use the same calls.
- **The weapons' modelled left arm is part of group 0**, the same mesh as the gun. In each model it is
  one or two runs of consecutive vertex numbers (connected pieces are stored consecutively):
  shotgun 30-95, assault rifle 276-308, chaingun 200-309 and 358-405 (a ring round the hand),
  rocket launcher 146-218, sniper rifle 249-306, pulse rifle 358-393, speargun 217-259. The quantum
  destabilizer has none; the magnum's 8-116 and the mines' 106-222 are the gun hand.

- **The weapons' view models are open on the right.** With each strip's winding honoured (a strip
  turns every other triangle round, a fan does not), the fists have 430 units of surface facing left
  and 429 right; the magnum 2544 and 617, the assault rifle 2297 and 485, and every other weapon two to
  five times more on the left. SiN's models are wound clockwise: a triangle facing +y has a normal, by
  the right-hand rule on the order drawn, pointing -y. The bottoms and fronts are thin too, but less so.
- **Where each modelled left hand held its gun** (glove middle, idle frame 0, model units): shotgun
  (27.6 -13.3 -16.4), assault rifle (79.3 -18.5 -39.4), rocket launcher (49.8 -6.3 -33.3), sniper rifle
  (30.5 -12.7 -13.3), pulse rifle (49.0 -19.0 -26.0), speargun (60.1 -21.3 -35.8). The chaingun's is at
  (46.6 35.8 -33.7), off to the side of the gun altogether.
- **The barrel markers' y** (the `id 199`/`29`/`4` groups): magnum -37.1, shotgun -15.0, assault rifle
  -23.0, chaingun -29.1, rocket launcher -27.4, sniper rifle -15.9, pulse rifle -23.6, quantum
  destabilizer -35.6, speargun -24.1. Against the middle of each body without its arm (5th to 95th
  percentile of the vertices' y) they agree within a unit, except the chaingun (body -24.1) and the
  rocket launcher (-23.5), whose marker is on one of several barrels.
- **The idle animations' numbers** (alphabetical): fists idle1-3 = 5-7; magnum idle1-2 = 1-2,
  silidle1-2 = 7-8; shotgun idle1-2 = 1-2; assault rifle idle1-3 = 2-4, silidle1-3 = 9-11; chaingun
  primaryidle1-2 = 2-3, secondaryidle = 9; rocket launcher idle1 = 1, primaryidle1 = 3; sniper rifle
  idle1 = 1; pulse rifle primaryidle = 2, secondaryidle = 8; quantum destabilizer idle = 1; speargun
  idle1-4 = 1-4; mines and detonator idle1-2 = 1-2; mutant hands idle1 = 4. Each weapon's `setaimanim`
  names the idle its muzzle is measured from: idle1, primaryidle1 or primaryidle.
- **The pulse rifle carries a second sleeve and glove along its top** (vertices 44-147, not all
  consecutive), besides the left arm at its fore-end.

- **A view weapon's client effects are placed at 0.3 of its bones' offsets.** In
  `CL_CMDS_ProcessCommands`, after the bone lookup (+0x89EB0, whose scale argument is the entity's
  scale), `attach` tests the command entity's RF_WEAPONMODEL (+0x194CE) and, if set, multiplies the
  offset by the float at +0x146DF0 (0.3) and, if `(int)hand->value` is 1, negates the right vector it
  is laid out along (`hand` is the cvar whose pointer is at +0x3FEC04, registered at +0x123B8). The
  same is done in `bonespawn` (+0x1B03B) and at +0x1AA04. The client runs the model's commands only
  when the view weapon's animation or frame differs from the last it ran them for (+0x74D6), with the
  entity it just handed to `V_AddEntity`.

## Saves and config.cfg (2026-09-27)

- **`config.cfg` holds only the cvars the engine has registered as archived.** A line `set vr_foo "1"`
  read from it makes a cvar without the archive flag, so if nothing registers `vr_foo` with
  `CVAR_ARCHIVE` in that session it is simply not written back, and the setting is gone. Found when a
  flatscreen desk run (the VR renderer loaded, `vr_enable 0`) rewrote the owner's config without a
  single controller or weapon setting. The file is also written in the reverse order each time.
- **Single-player saves go to `<game>\players\<player>\save\<name>\`** (`game.ssv`, `server.ssv`,
  and `<map>.sav`/`.sv2`), with `current` and `save0` written by the engine itself as levels start.
- **A save made on a deathmatch map loaded in single player does not load back**: `load` fails with
  `ERROR: CM_InlineModel: bad number` on `sindm6`, where `bank` and `training` load cleanly (and print
  `Loading nodes...`, the AI path nodes, which the deathmatch map has none of). Not chased further:
  `training` does the job.
- **The campaign opens on `intro`**, whose first minutes are the helicopter ride and turret, not
  walking. `bank` leads to `abandon` (its `trigger_changelevel`); `intro`'s own exit is not in its
  entity lump, so it is presumably scripted.

## The client's view angle (2026-09-22)

- **`cl.viewangles[YAW]` is at sin.exe +0x3CBAE0** (0x7CBAE0 at the preferred base). It is what every
  movement command carries to the game, so changing it turns the player - view and movement both.
  Found from the cvars that scale turning: `cl_yawspeed` is kept at +0x4BFC58 and `joy_yawsensitivity`
  at +0x1D8B38, and the code that reads them is `IN_JoyMove`, which ends
  `addss xmm0, [+0x3CBAE0]` / `movss [+0x3CBAE0], xmm0` at +0x9B8C8 / +0x9B8D0, and `CL_AdjustAngles`,
  which stores to the same place at +0xE54C (after its `subss` for turning right) and +0xE58F (after its `addss` for left).
  Pitch should be the float before it, as in Quake II's `vec3_t`, but that has not been checked.
  Nothing wraps the yaw, and the game gets it as a short, which does.

## The client's view weapon and its effects (2026-09-19)

All RVAs are for the Steam build of `sin.exe` (preferred base 0x400000, ASLR on).

- **The player's own weapon entity never goes into the scene as itself.** `CL_AddPacketEntities`
  (+0x5970) spots the entity whose parent is the player and which is flagged `RF_VIEWERMODEL`, copies it
  aside to a global at +0x153640 and skips it.
- **`CL_AddViewWeapon` (+0x7100)** builds the view weapon from that copy each frame:
  `origin = cl.refdef.vieworg + lerp(ps->gunoffset)` and `angles = cl.refdef.viewangles +
  LerpAngle(ps->gunangles)` (the game fills both in `Player::CalcGunOffset`), then `AngleVectors` into
  the entity's axis, then `V_AddEntity` at **+0x747F**. After that it negates the entity's pitch - the
  sense the bone code wants - and runs the model's client-side commands on the same entity, which is
  where the muzzle flash, tracers, shell cases, dynamic light and the client's own bullet impacts come
  from. The entity whose commands are running is in a global at +0x1532F4.
- **`V_AddEntity` (+0x25520)** copies 0xC8 bytes into the scene list at +0x30E920 (up to 256 entities,
  count at +0x31B120) and, for an entity with a parent, notes it in an attachment table at +0x309D20.
- **The `.def` files' client commands** are dispatched by name in `CL_CMDS_ProcessCommands` (+0x191E0):
  `attach`, `bonespawn`, `shootbullets`, `dlight`, `spawn`, `shootbeam`, `cachemodel` and so on.
- **`shootbullets` (+0x208F0, called at +0x1A707)** traces its bullets from **the player's eye** - the
  centity's origin plus the view height - along the *entity's* angles, with a random spread, and calls
  the impact effect (+0x207B0) at each hit: surface sound, sparks, a decal and surface debris. The same
  effect function is what the `TE_GUNSHOT` temp entity runs (`CL_ParseTEnt` +0x219E0, case 0), so a
  server-sent impact looks exactly like a client-predicted one. Player weapons pass `server_effects =
  false` in the game module, which is why their impacts are the client's and start at the eye.
- **The def's `origin` and `scale`** are stored in the model header at +0x90 and +0x94..0x9C and applied
  by the renderer as `(raw + origin) * scale` - not `(raw - origin)`, which is what the numbers first
  suggest.

## SiN's model files (2026-09-19)

Read with a small parser written for this (`scratchpad/sinmodel.py`), against the game's own `pak0.sin`.

- **The paks are `SPAK`**: magic, directory offset, directory length; entries are 120-byte names plus
  offset and size, 128 bytes each.
- **A `.def` is text** (`SDEF`): `path`, `origin`, `scale`, the `.sbm` base model, skins, the named
  animations (`.sam` files), `id <n> group <name> [bone <name> <angles>]` lines, and then `!init:` and
  `!main:` sections of `server`/`client` commands tied to animation frames.
- **`.sbm`**: magic `SBM `, version, vertex count, texture-coordinate count, group count, an offset and
  the file size; then one 24-byte record per group - an id, a vertex count, a command count, and three
  offsets *relative to the record itself* - followed by Quake-style GL command lists (strip or fan
  length, then s, t and a vertex index per vertex).
- **`.sam`**: magic `SAM `, version, a 64-byte name, some overall values, then the vertex count, frame
  count and frame table offset; each frame is 44 bytes (a movement delta, a time, a scale, a translate
  and an offset to its vertices, again relative to the record), and its vertices are one byte per axis
  plus a normal index, as Quake's are.
- What that gives: **where each part of a view model is**, which is how the table of grips and sizes in
  `vr_weapon.c` was measured. Most of SiN's two-handed weapons model only the *left* hand, on the
  fore-end; the right hand that would hold the grip is off the bottom of the screen and was never built.
  The fists are the one view model with two arms, cleanly separated either side of y = 0.

## The game module's aiming and firing path (2026-09-16)

- **Every weapon asks its owner where to shoot from.** `Weapon::GetMuzzlePosition` calls
  `owner->GunPosition()` and `owner->GetGunOrientation(...)`, both virtual on `Sentient`, and adds the
  weapon's own `MuzzleOffset()`. Overriding the two in `Player` therefore moves aiming for every weapon
  at once, hitscan and projectile alike.
- **`Sentient::EyePosition()`** is `worldorigin + eyeposition` (0, 0, 64 by default) and is the right
  reference point for a head-relative pose; `GunPosition` is `worldorigin + gunoffset` (0, 0, 56).
- **`gravaxis`** flips the axes in both functions for gravity-turned areas, which a VR pose does not
  account for, so the VR path steps aside when it is set.
- **A console `+attack` does not fire a shot in an unattended run.** Probes in `Sentient::FireWeapon`,
  `Weapon::Fire`, `BulletWeapon::FireBullets`, `Weapon::GetMuzzlePosition` and `Sentient::GunPosition`
  all stayed silent while a probe in `G_RunFrame` fired every frame - so the client never turned the
  console command into a usercmd button, even with the window in front and `key_dest` 0. Firing has to
  be tested by hand.
  - Worth knowing for the next time: `gi.printf` from the game module does reach the console and
    `qconsole.log` during play, and a probe that writes its own file (`fopen` in the game's working
    directory) is the way to check a code path without depending on the console at all.

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
