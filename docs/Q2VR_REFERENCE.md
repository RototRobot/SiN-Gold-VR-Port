# Reference: Quake II PCVR (GameOrDie007) and Quake2Quest (Team Beef)

**What it is.** The source is in `..\Quake-II-PCVR-1.0`, reviewed 2026-09-15. It combines three things:
yquake2 7.41, Team Beef's Quake2Quest VR changeset applied verbatim, and a Windows OpenXR +
OpenGL platform layer. It builds for 64-bit Windows with MinGW and is **GPLv2** throughout. The docs worth
reading are `README.md`, `PLAN.md`, `RUNTIME_NOTES.md`, `HANDOFF.md` and `TEAMBEEF-DEFAULTS.md`.

**Why it matters for SiN.** yquake2's `gl1` renderer and its client descend from the same Quake 2 code as
SiN's `ref_gl.dll` and `sin.exe`, so their fixes land on the same functions listed in
`ENGINE_NOTES.md`. The difference is access: they could edit the whole engine, while we can only edit the game DLL
and wrap the renderer.

## Mechanism map

| Problem | How Q2VR solves it (file) | SiN equivalent |
|---|---|---|
| Which eye is being drawn | The renderer reads the sign of `gl_state.camera_separation`, the stock Quake 2 stereo plumbing (`gl1_main.c`) | The same field exists in SiN's renderer; our proxy sees the sign in `BeginFrame(sep)` |
| Per-eye asymmetric projection | `R_MYgluPerspective` substitutes the OpenXR FOV tangents, which reach it through the `gl1_openxr_fov_*` cvars | Intercept `glFrustum`. It is called in R_SetupGL and in both model paths (which cover the view weapon) |
| Culling against a wide asymmetric FOV | `R_SetFrustum` is rebuilt from the HMD FOV; stock culling made geometry vanish at the edges | The renderer is closed, so widen `refdef.fov_x/fov_y` until the frustum encloses the eye frustum, then substitute the true projection |
| Frame structure | The VR layer drives the engine: `Qcommon_Frame` runs once per eye inside one `xrBeginFrame`/`xrEndFrame` pair (`vr_surface.c` `TBXR_FrameSetup`) | SiN already renders two passes per frame with `cl_stereo 1`, so we only wrap BeginFrame and EndFrame |
| Head controls the view | `cl.viewangles` is set to the HMD yaw/pitch/roll plus a snap-turn yaw offset (`cl_input.c` `CL_AdjustAngles`, `VR_GetMove`). The usercmd therefore carries the **head** angles, and the view height comes from the HMD | Rendering: override the refdef per eye in the proxy. Gameplay: requires a hook on the client's `cl.viewangles` / `CL_BaseMove` inside sin.exe |
| Aim and fire | The game DLL reads the controller pose through 3 new `game_import_t` pointers (`getVROrigins`). `SV_SetWeapon_Client6DOF` moves the player origin and `v_angle` onto the controller around `Think_Weapon`, then restores them (`client.c`) | `game_import_t` cannot be extended because the engine is closed. Instead the game DLL fetches the pose from `ref_vr.dll` through GetProcAddress, and we override `Player::GunPosition/GetGunOrientation`, which `Weapon::GetMuzzlePosition` already calls |
| Aim-fix toward the crosshair | `P_ProjectSource`'s aimfix traced from the *view* and bent shots toward it. It is suppressed in VR | SiN's `GetMuzzlePosition` traces from `GunPosition()`, so it should stay consistent once that is the controller. **Verify.** |
| Weapon model in the hand | The client places the view weapon every frame from the controller pose, adjusted per weapon by the six-value `vr_weapon_adjustment_<n>` cvar, with scale 0.56 and pitch −20° (`cl_entities.c` `SetWeapon6DOF`). Recoil rotates the gun, never the view | SiN's closed client draws the view weapon from player state that arrives at 10 Hz. So we place it in the proxy: find the weapon entity in the refdef entity list and overwrite its origin and angles. **This needs the `entity_t` layout.** |
| Locomotion | Stick input becomes forward/side movement, rotated by the off-hand yaw (or the head). Roomscale movement is the per-frame HMD delta converted into forward/side moves, applied only while on the ground (`VrInputDefault.c`, `VR_GetMove`) | Needs usercmd injection in sin.exe, either a `CL_BaseMove` hook or joystick emulation |
| Turning | Snap turn (45°) or smooth turn, both implemented as an accumulated yaw offset. The weapon's one-frame lag on a snap is corrected | Same design |
| Buttons | Console commands (`+attack`, `+movedown`, `use …`), plus `Key_Event` for menus | `ri.Cmd_ExecuteText`-style commands; menu keys as WM_KEYDOWN posted to the game window |
| HUD | Drawn per eye with a horizontal offset computed from that eye's optical centre, `vr_hud_depth 0.5`, `vr_hud_ipd 0.064` (`cl_screen.c`) | Apply the same offset inside our wrapped `Draw*` slots |
| Menus, console, cinematics | The whole frame goes on a flat quad at `vr_screen_depth` 3.5 m (`useScreenLayer`), or the menu gets its own layer | OpenVR overlay or OpenXR quad layer |
| Comfort | No view kick or bob in VR; optional vignette centred on each eye's optical centre | SiN's bob, roll and kick are computed in the game DLL (`Player::CalcViewOffset`) |

## Lessons worth keeping (from RUNTIME_NOTES.md and HANDOFF.md)

- **The engine re-sets VR values further down the pipeline.** Their examples: the weapon-model projection, the viewport,
  the shot direction. SiN's renderer also re-issues `glFrustum` inside its two model paths.
- **Give every eye FBO its own depth attachment**, and clear it yourself. Nothing else will.
- **Turn vsync off in VR** (`gl_swapinterval 0`). Otherwise the monitor's refresh paces the headset, and the judder
  looks exactly like a performance problem.
- **A term that cancels out is invisible to experiment.** If a change "did nothing", first ask whether
  the quantity even reaches the output.
- **Settle axis and sign conventions by looking through the headset**, not on paper.
- **Desk testing.** They ran a second VDXR session to test without wearing the headset. On this machine, SteamVR's null driver is installed
  (currently disabled) and can serve the same purpose.
- **Their bring-up order:** create the instance and read the view configuration before the engine starts; create the session and
  swapchains once a GL context exists.

## Values (Quake II specific; do not copy blindly)

| Setting | Q2VR value | Note for SiN |
|---|---|---|
| `vr_worldscale` | 26.2467 units/m | Quake 2's eye is about 46 units above the floor, so this puts it at about 1.75 m. SiN's eye is at 66 units, so the same rule gives roughly **38 units/m**. Tune it in the headset. |
| Weapon scale | 0.56 | |
| Weapon pitch adjust | −20° | |
| Laser sight | on | |
| HUD depth | 0.5 m | |
| Screen/menu distance | 3.5 m | |
| Snap turn | 45° | |

## Licence

Everything in the reference is GPLv2. SiN's game source is under Night Dive's non-commercial licence,
which is not GPL-compatible. **Never paste their code into gamex86.** Ideas, structure and measured values
are fine to use. Their code could only ever be reused verbatim inside a separate DLL of ours that is itself licensed GPL
(for example `ref_vr.dll`), and the GPL plugin question would need settling first.

## Runtime choice

We chose **OpenXR**, as Q2VR did (2026-09-15). The 32-bit loader, the probe results and the runtime quirks are in
`OPENXR_NOTES.md`. That makes their `vr_surface.c`, an OpenXR + OpenGL implementation for Windows, the closest reference we have
for session, swapchain and frame-loop code. As with everything else here, use it for structure, not for copy-paste.
