# SiN Gold VR

A virtual reality mod for **SiN Gold** (PC, Steam or GOG) and its expansion **Wages of Sin**, adding
stereoscopic rendering, 6DoF head tracking and motion controllers: the weapon in your hand, long guns
held in both hands, punches thrown with your own fists, and walking and crouching in your room.

It runs as a renderer the engine loads (`ref_vr.dll`) over the game's own OpenGL renderer, with game
logic built from Night Dive Studios' released source. Nothing that ships with the game is changed.

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

> [!NOTE]
> **This mod was made with heavy AI (Claude) assistance.** I want to be upfront
> about that.

> [!WARNING]
> **This is version 1.0.** Both campaigns are playable from start to finish in VR, but expect rough
> edges. Bug reports with logs are very welcome; see [Troubleshooting](#troubleshooting).

> [!IMPORTANT]
> **Windows may block this mod from running.**
>
> **Smart App Control** (Windows 11) refuses unsigned code outright. This mod is unsigned, so on a
> machine where SAC is enforced the game will start and the VR renderer will not load. SAC has no
> exclusion list; the only option is to turn it off:
> `Windows Security → App & browser control → Smart App Control → Off`.
>
> Ordinary antivirus may also object: a renderer DLL that patches the running game looks a lot like
> what malware does. An exclusion for the game folder fixes that.

---

## About the game

**SiN** (Ritual Entertainment, 1998) is a Quake II engine shooter: Colonel John Blade, HardCorps, a
city run by SinTEK and the mutagen U4. **SiN Gold** is Night Dive Studios' 2020 re-release, with the
expansion **Wages of Sin** (2015, Inc.) in the same package, and Night Dive released its game logic as
source.

It was never made for VR - one camera, a flat HUD, weapons built to be seen from one angle on a
monitor - but it fits well. Its levels are built at human scale, its guns are big and readable, and its
fists, mutant hands and long guns map naturally onto two tracked hands.

---

## Requirements

- **SiN Gold** for PC: the Steam or the GOG release (the 2020 Night Dive version). Wages of Sin comes
  with it.
- A PC VR headset with two tracked motion controllers
- **SteamVR** installed and running (its OpenXR runtime is what runs the mod: SiN is a 32-bit game, and
  SteamVR's runtime supports 32-bit applications)
- Windows 10 or 11

Developed and tested on an **HP Reverb G2**, and tested on a **Meta Quest 3** over **Steam Link**.
Bindings are suggested for HP Reverb G2, Windows Mixed Reality, Oculus Touch and Valve Index
controllers. Nothing is specific to one headset: field of view, eye separation and render resolution
all come from the runtime.

---

## Features

### Rendering

- **True stereo, both eyes every frame**, through the engine's own two-pass stereo loop and an OpenXR
  projection layer.
- **Per-eye asymmetric projection** from the runtime, and SteamVR's recommended resolution
  (`vr_resolution` scales it).
- **The HUD, crosshair and console in a panel** at a comfortable distance, the same in both eyes.
- **Menus and loading screens on a virtual screen** that comes up in front of you.
- **No view bob, roll or kick**: only your head moves the camera.

### Hands and weapons

- **The weapon in your hand**: drawn where the controller is, firing down its own barrel, with its
  muzzle flash, tracers, shell cases and bullet holes coming from there.
- **The missing sides of the guns rebuilt.** SiN's view models were only modelled on the side the
  original camera saw; the other side is now that side reflected, following the gun through its
  firing animation.
- **Two hands on long guns**: grip the fore-end with your other hand to aim with both. The arms SiN
  built into the models are taken out and your own hand is drawn instead. The helicopter's
  minigun is the exception: it is carried in its own two modelled hands, as in the original game,
  and both your hands steer it all the time, like handlebars.
- **Set each weapon's grip in the headset**: hold the grip button and place the weapon in your hand.
- **Punch with your fists** (and the mutant hands): a swing lands where your fist goes, harder the
  faster you swing, with a little aim assist.
- **Pick weapons from your body**: reach over a shoulder for the next or previous weapon, to your hip
  for the magnum, both hands up for the fists.
- **Holster menus**: hold the grip over a shoulder and every weapon you carry comes up in front of you;
  hold it at your other hip for your inventory. The trigger steps along, and taking your hand out of
  the holster picks it: the weapon is drawn, the item selected for Y to use - the flashlight, the
  goggles, a powerup.
- **Every button on the controllers**: weapon modes on B, the selected item on Y, and
  `vr_swap_sticks` to move with the right stick in either handed mode.
- **Haptics** for shots, punches and the quantum destabilizer's charge.
- **Left-handed mode**: the mirror image of all of it.
- **Wages of Sin's own weapons** too: the dual magnums in both hands, a guided missile steered with
  your hand or your head, and the rest.

### Movement and comfort

- **Roomscale**: walk around your room and the player follows, stopped by walls and climbing stairs;
  turns pivot on you.
- **Physical crouch**: crouch for real to crouch, and your view stays out of the ceiling.
- **Ladders climb straight**: pushing the stick at a ladder climbs straight up it, without sliding
  sideways when the stick is a little off square.
- **Smooth or snap turning**, and movement towards where you look or where your hand points.
- **An optional comfort vignette** that narrows the view while the stick moves you.

---

## Installation

### 1. Copy the files

Download `SiN-Gold-VR-<version>.zip` from the Releases page and unzip everything into the folder that
contains `sin.exe`, keeping the folders. Steam's default:

```
C:\Program Files (x86)\Steam\steamapps\common\SiN 1\
```

(On Steam: right-click SiN Gold → Manage → Browse local files.)

| File | What it is |
|---|---|
| `ref_vr.dll` | The VR renderer, which loads the game's own `ref_gl.dll` |
| `openxr_loader.dll` | The OpenXR loader, 32-bit |
| `sinvr\gamex86.dll` | SiN's game logic, with the VR changes |
| `sinvr2015\gamex86.dll` | Wages of Sin's game logic, with the VR changes |
| `SiN VR*.bat` | The launchers (below) |
| `SiN Gold VR - Read Me.txt`, `SiN Gold VR licences\` | A short read-me, and the licences |

### 2. Start the game

Start SteamVR and put the headset on, then run a launcher from the game folder.

> [!TIP]
> **Use `SiN VR - Wages of Sin.bat` for both campaigns.** Its New Game asks which adventure to play,
> Wages of Sin or the original SiN, and one launcher keeps one set of settings and grips. Use
> `SiN VR.bat` only if you want SiN exactly as it shipped, without the expansion's game code.

| Launcher | What it does |
|---|---|
| `SiN VR - Wages of Sin.bat` | **Recommended.** Wages of Sin in VR; its New Game also offers the original SiN |
| `SiN VR.bat` | The original SiN on its own, as it shipped |
| `SiN VR (flatscreen).bat` | The same mod with VR off |
| `SiN VR - Wages of Sin (flatscreen).bat` | The same, for Wages of Sin |

**Or from Steam's Play button:** right-click SiN Gold → Properties → Launch Options:

```
+set game sinvr2015 +set vid_ref vr +set vr_enable 1
```

Steam adds these after its own arguments, so both "Play" and "Play Wages of SiN" then start the mod's
Wages of Sin, whose New Game asks which adventure to play. Run `SiN VR - Wages of Sin.bat` once first:
it links `sinvr2015\locale` to the game's own `2015\locale`, which is where the expansion's menus and
dialogue are found. Empty the box to play without VR again.

When the game starts, **stand up straight and click the left stick twice** to recentre: forward and
your crouching height are measured from there.

> [!TIP]
> **Every gun can be moved and resized in your hand - and you will want to.** The grips start from a
> best guess. With a weapon out:
>
> 1. **Squeeze and hold the grip of the hand holding it** (about a second). The gun stops following
>    your hand.
> 2. **Move your hand** to where you want to hold it.
> 3. **Push that hand's stick up or down** to make the gun bigger or smaller.
> 4. **Let go of the grip.** The gun is held that way from now on, remembered for each weapon.
>
> Details in [Setting a weapon in your hand](#setting-a-weapon-in-your-hand).

---

## Controls

Defaults for right-handed play, on Touch-style controllers (Reverb G2, Quest, Rift). With
`vr_left_handed 1` each hand takes the other's buttons and stick.

| Control | Action |
|---|---|
| Left stick | Move and strafe; forward is where you look (`vr_move_dir`) |
| Left stick, click twice | Recentre |
| Right stick | Turn: smoothly, or `vr_snap_angle` degrees a push with `vr_snap_turn 1` |
| Right stick, click | The game menu (Escape) |
| Right trigger | Fire |
| Left trigger | Use |
| A | Jump |
| X | Crouch |
| B | Switch the weapon's mode (SiN's right mouse button), for weapons that have two |
| Y | Use the inventory item you have selected: the flashlight, the goggles, a powerup (pick it from the hip menu below) |
| Left grip | Run (walk, with `cl_run 1`); at a long gun's fore-end, hold it in both hands |
| **Right grip, held** | **Move the weapon in your hand; push the right stick up / down to resize it** |
| Right hand over your right / left shoulder, right grip: a tap | Next / previous weapon |
| Right hand over a shoulder, right grip: **held** | **The weapon menu**: right trigger steps along, take your hand away from the shoulder to switch to the one picked, let go of the grip there to cancel |
| Right hand at your right hip, right grip | The magnum |
| Left hand at your left hip, left grip: a tap | Use the inventory item the HUD shows (powerups) |
| Left hand at your left hip, left grip: **held** | **The inventory menu**: left trigger steps along, take your hand away from the hip to select the one picked (Y then uses it), let go of the grip there to cancel |
| Both hands at your shoulders, both grips | The fists |
| Swing a fist | Punch with that hand |
| Walk / crouch in your room | Walk / crouch |

B and Y can be rebound (`vr_bind_top_gun`, `vr_bind_top_other`). With `vr_weapon_buttons 1`, B and Y
cycle weapons instead. In the holster menus, `vr_holster_invert 1` makes letting go of the grip pick and
taking your hand out cancel. `vr_swap_sticks 1` swaps the two sticks, and A and X with them, so you move with
the right stick and turn with the left - in either handed mode. In a menu, on the loading screen or with the
console up, the sticks are the arrow keys, the trigger is Enter and anything else is Escape.

### Setting a weapon in your hand

Every weapon starts from a measured guess at where your hand holds it, so this is worth doing for the
guns you use. Hold the weapon and **squeeze and hold the grip of that hand** for about a second: the
weapon stops following your hand and your controllers appear - a cross where each hand holds and a
line along where it points. **Move your hand** to hold it the way you want, **push the same hand's
stick up or down to resize it**, then **let go of the grip**. It is remembered per weapon. With the gun hand's grip held, squeezing the other hand's grip sets the fore-end there.
`vr_weapon_reset 1` puts the weapon in hand back to its first guess.

### Two hands on a long gun

Put your other hand on the front of the gun - the fore-end, where SiN's own model had its left hand -
and squeeze that hand's grip: the gun lies along the line from your gun hand to the other, and you aim
with both. Let go and it follows the gun hand alone.

---

## Configuration

Every setting is a console variable, changed in the console (`~`) or in the config with the game
closed, and saved in `sinvr\players\blade\config.cfg` (`sinvr2015\players\blade\config.cfg` for Wages of
Sin - the two games keep their own). Each button is a cvar too (`vr_bind_fire`, `vr_bind_jump` and so
on), bindable to any console command.

### The settings most worth knowing

| Setting | Default | What it does |
|---|---|---|
| `vr_left_handed` | `0` | The weapon in the left hand, mirrored, with the hands' buttons swapped. Takes effect at the next start (or `vid_restart`) |
| `vr_snap_turn` / `vr_snap_angle` | `0` / `45` | Turn in steps of that many degrees instead of smoothly |
| `vr_move_dir` | `1` | Forward is where `1` your head, `2` your moving hand, `0` your body faces |
| `vr_vignette` | `0` | Comfort vignette while the stick moves you: `0.6` is a good start, `1` a narrow window |
| `vr_roomscale` | `1` | Walking in your room walks the player |
| `vr_crouch` / `vr_crouch_depth` | `1` / `0.35` | Crouching in your room crouches, this many metres down |
| `vr_resolution` | `1` | Render size against the runtime's recommendation |
| `vr_haptics` | `1` | Controller buzzes |
| `vr_punch_full` | `4` | Swing speed (m/s) for a full-strength punch |
| `vr_missile_steer` | `1` | Wages of Sin's guided missile: `1` your hand steers, `2` your head |
| `vr_weapon_fill` | `1` | Rebuild the guns' missing sides (`vr_fill_<weapon>` per gun) |

### Every setting

| Cvar | Default | Meaning |
|---|---|---|
| `vr_enable` | 0 | Show the game in the headset. The launchers set it. |
| `vr_stereo` | 1 | Render the world once per eye. With 0, everything stays on the virtual screen. |
| `vr_resolution` | 1 | How big each eye is rendered, on each side, against the size the headset's runtime recommends. 1.2 is sharper and costs 44% more pixels; 0.8 is softer and faster. Takes effect straight away. |
| `vr_worldscale` | 39.37 | Game units per metre, for head movement. Lower values make the world feel bigger. |
| `vr_screen_distance` | 2.5 | Virtual screen distance, in metres. The screen comes up in front of you, at your head's height, each time a menu or loading screen does, and when you recentre. |
| `vr_screen_width` | 2.4 | Virtual screen width, in metres. |
| `vr_hud_fov` | 60 | How wide the HUD, crosshair and console panel is, in degrees. Smaller brings it closer to the middle. |
| `vr_hud_depth` | 6 | How far away that panel appears, in metres. 0 puts it at infinity. |
| `vr_recenter` | 0 | Set it to 1 to measure the head afresh from where you are standing and facing now. Clears itself. |
| `vr_input` | 1 | Take input from the VR controllers. |
| `vr_deadzone` | 0.15 | How far a stick must move before the game sees it. |
| `vr_recentre_double` | 1 | Require two clicks of the left stick to recentre. 0 makes one do. |
| `vr_snap_turn` | 0 | 1 turns in steps: each push of the turning stick turns you `vr_snap_angle` degrees at once, and the stick has to come back to the middle before it turns you again. 0 is smooth turning. |
| `vr_snap_angle` | 45 | How far one snap turns you, in degrees. |
| `vr_weapon_buttons` | 0 | 1 lets the top buttons (B and Y) cycle weapons as well as the shoulders. |
| `vr_bind_top_other` | `invuse` | What the other hand's top button (Y, right-handed) does while `vr_weapon_buttons` is 0: any console command. By default it uses the selected inventory item. |
| `vr_bind_top_gun` | `weaponuse` | The gun hand's top button (B): switches the weapon's mode. |
| `vr_haptics` | 1 | The controllers buzz: as a hand reaches a shoulder or hip, as a gesture fires, when a weapon goes off (harder for the heavy ones, and in both hands while steadying a rifle), rising while the quantum destabilizer charges - full strength at five seconds, which is also when it blows up in your hands - and when a punch lands (harder on an enemy than a wall). 0 turns every buzz off. |
| `vr_holster_invert` | 0 | 1 turns the holster menus' choosing round: letting go of the grip in the holster picks, and taking your hand out cancels. |
| `vr_gestures` | 1 | The grips at the shoulders and hips change weapon and use the inventory, as in the controls above. 0 leaves the grips doing only what they do elsewhere. |
| `vr_move_dir` | 1 | Which way the stick's forward is: 1 where your head faces, 2 where the moving hand's controller points, 0 the way you faced when you last recentred. |
| `vr_missile_steer` | 1 | How Wages of Sin's guided missile steers: 1 with the hand that holds the weapon, pointed where it should go; 2 with your head, the missile turning to wherever you look; 0 as the flat game does, by the body's facing (left and right only in VR). |
| `vr_vignette` | 0 | The comfort vignette: while the stick moves or smoothly turns you, the edge of the view fades to black, as far in as this says - 0.6 is a good start, 1 closes it to a narrow window, 0 (the default) is off. Walking in your room and snap turns leave it open. |
| `vr_roomscale` | 1 | Walking in your room walks the player. 0 leaves the body where the stick puts it, and only the view moves with your head. |
| `vr_lean` | 0.3 | How far your head can go past your body when the body is stopped - leaning over a ledge, into a wall - in metres. Past it the room slides instead. |
| `vr_crouch` | 1 | Crouching in your room crouches the player. 0 leaves crouching to the button. |
| `vr_crouch_depth` | 0.35 | How far below where you recentred your head has to go to crouch, in metres. It stands again 8 cm above that. |
| `vr_ladder_assist` | 1 | Pushing the stick at a ladder (within 60 degrees of square on) pushes straight into it, so you climb without sliding sideways. 0 leaves the stick as it is. |
| `vr_aim` | 1 | Shots come from the weapon in the hand rather than from the player's angles. |
| `vr_weapon_hand` | 1 | Draw the weapon in the hand instead of in front of the camera. |
| `vr_left_handed` | 0 | 1 puts the weapon in the left hand, mirrored, and swaps the two controllers' buttons and sticks. Read when the renderer starts, so `vid_restart` after changing it. |
| `vr_swap_sticks` | 0 | 1 swaps the move and turn sticks, and jump and crouch (A and X), whichever hand the weapon is in. Read when the renderer starts, so `vid_restart` after changing it. |
| `vr_show_hands` | 1 | Draw the controllers - a cross and the line they point along - while a weapon is being placed. 2 draws them all the time, 0 never. |
| `vr_punch` | 1 | Swinging a fist punches while the fists are out. 0 leaves punching to the trigger; 2 also says in the console what each punch hit. |
| `vr_punch_speed` | 2 | How fast a fist has to be going to punch, in metres a second. |
| `vr_punch_full` | 4 | How fast a swung punch has to go to hit as hard as the trigger's, in metres a second. Slower swings hit softer, down to half; faster ones harder, up to twice. |
| `vr_punch_assist` | 35 | How far to either side of where you look - or where your fist was headed - a punch still finds someone within reach, in degrees. 0 lands punches only where the fist actually goes. |
| `vr_freeze_idle` | 0 | 1 also stops the game module playing idle animations at all. Not needed for the picture any more - `vr_weapon_idle` holds that still - and the game's own hold only takes effect at the next idle. |
| `vr_grip_<weapon>` | empty | Where that weapon sits in the hand: offset, angles and size. The grip button writes it; empty means the measured guess. |
| `vr_two_hand` | 1 | The other hand's grip, squeezed at a long weapon's fore-end, holds it in both hands. 0 leaves that grip only running. |
| `vr_other_hand` | 1 | Draw the hand without the weapon, as the fists' arm for that hand, placed the way the fists are. 0 leaves it out. |
| `vr_weapon_fill` | 1 | Rebuild the side of each gun that SiN never made - its right side, which the original camera never saw - as its left side reflected. 0 shows the guns as they were built. |
| `vr_fill_<weapon>` | 1, 0 for the chaingun | The same for one weapon, for any that look wrong mirrored. The chaingun is off: it has a box on its left side only. |
| `vr_weapon_idle` | 0 | The weapon's idle animation - breathing and swaying - is held still, since the weapon already moves with your hand. 1 lets it play. Firing, reloading and switching still animate. |
| `vr_upper_arm` | 0 | The fists - in both hands, and the one drawn in the hand without a weapon - stop just short of the elbow. 1 draws the whole arm the model has, elbow and upper arm too. |
| `vr_weapon_arm` | 0 | 1 puts back the left arm SiN built into the long weapons' models; 0 leaves it out, since your own other hand is drawn instead. |
| `vr_fore_<weapon>` | empty | Where that weapon's fore-end is, in the model's own units. Set by squeezing the other grip while adjusting; empty means about 30 cm ahead of the grip along the barrel. |
| `vr_weapon_reset` | 0 | Set it to 1 to forget the setting for the weapon in hand. Clears itself. |
| `vr_crosshair` | 1 | SiN's crosshair follows the gun while it is in the hand. 0 leaves it out; 2 leaves it in the middle of the view, where SiN draws it. |
| `vr_client_bullets` | 0 | 1 puts back the client's own bullet impacts, which are drawn from the eye rather than from the barrel. |

### For testing

| Cvar | Default | Meaning |
|---|---|---|
| `vr_force_render` | 0 | Draw for the headset even while the runtime says it is idle. For testing with the headset off. |
| `vr_dump_eye` | 0 | Set it to 1 to write the next frame's two eye images beside `sin.exe`, for checking a build at the desk. |
| `vr_fake_step` | empty | For testing at the desk: `"1 0 0"` moves the head a metre forward at once (x forward, y left, z up), `"1 0 0 2"` over two seconds, which is walking; `"0"` only says in `sinvr.log` where the body and view are. |
| `vr_trace` | 1 | Record the renderer calls of each new kind of frame in `sinvr.log`. |

---

## Building from source

### Prerequisites

- **Visual Studio 2022** (or Build Tools) with *Desktop development with C++*, which includes CMake.
- **Python 3**: the OpenXR loader generates some of its source while it builds.
- **The OpenXR SDK source.** By default the build uses `..\OpenXR-SDK-Source-release-1.1.63` beside
  this repository, and fetches the pinned release from GitHub if it is missing. Pass
  `-DSINVR_OPENXR_SDK=<path>` to use another copy.

Everything is built **32-bit**: `sin.exe` is a 32-bit process, and the game DLL has to match its ABI.

### Build

```bat
cmake --preset win32
cmake --build --preset release
```

The output goes to `build\win32\stage\Release\`, laid out like the game folder.

### Install or package

```bat
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1     :: into the Steam folder (-GameDir for another; -Uninstall removes it)
powershell -ExecutionPolicy Bypass -File tools\package.ps1    :: the release zip, in dist\
powershell -ExecutionPolicy Bypass -File tools\desk-test.ps1 -Vr -OutDir <folder>   :: an unattended test run
```

### How it works

SiN runs on a modified Quake II engine. The engine (`sin.exe`) and its OpenGL renderer (`ref_gl.dll`)
are closed source; the game logic was released by Night Dive Studios and is in [`game/`](game).

- **`ref_vr.dll`** is a renderer the engine loads for `+set vid_ref vr`. It loads the stock
  `ref_gl.dll`, passes the engine's calls through, and adds the VR side over OpenXR: each eye rendered
  through the engine's own stereo loop, the 2D in a panel, the controllers as console commands and an
  emulated joystick, and a few calls inside `sin.exe` redirected so the weapon, its effects and the
  player's movement follow the headset and hands.
- **`gamex86.dll`**, built from Night Dive's source for SiN (`sinvr`) and Wages of Sin (`sinvr2015`),
  takes aim from the hand, swing punches, roomscale steps and the rest. The mod's own game code is in
  `src/game_vr/`, shared by both; the game copies only call into it.

New to the code? Start with [docs/HANDOFF.md](docs/HANDOFF.md). Background:
[docs/ENGINE_NOTES.md](docs/ENGINE_NOTES.md), [docs/OPENXR_NOTES.md](docs/OPENXR_NOTES.md) and
[docs/Q2VR_REFERENCE.md](docs/Q2VR_REFERENCE.md).

### Layout

| Path | Contents |
|---|---|
| `src/ref_vr` | The VR renderer: OpenXR, stereo, HUD panel, input, weapons and hands, roomscale, vignette |
| `src/game_vr` | The mod's game code, shared by SiN and Wages of Sin, and the link to the renderer |
| `src/shared` | The contract between the renderer and the game DLL |
| `game` | Night Dive's SiN Gold game source (SiN and Wages of Sin) and its CMake build |
| `tools` | Install, package and test scripts, and `xrprobe` (an OpenXR diagnostic) |
| `cmake` | Build helpers (the OpenXR loader) |
| `docs` | The handoff, research notes and the release read-me |

---

## Known limitations

- **Leave the game's video options alone** while playing through the mod. They were made for a
  monitor. The mod keeps itself loaded when they are applied, but they have not been tested in VR.
- **Weapon grips start from a guess.** Settle the ones you use with the grip button.
- **The rebuilt sides of the guns are mirror images.** Details that are not symmetrical - an ejection
  port, a label - appear on both sides. They are only there while a gun idles or fires; raising,
  lowering and reloading show the gun as built. `vr_fill_<weapon> 0` turns one off.
- **Your other hand is a fist** on a long gun: it is the only hand model the game has.
- **A left-handed weapon is lit as its mirror image** was.
- **Crouching is measured from where you recentred.** Recentre standing up straight; seated, crouch with
  the button (and `vr_crouch 0` if leaning forward ever crouches you).
- **The controllers only work while the headset is awake**: SteamVR takes input away from a headset
  that sleeps.
- **The menus are SiN's own 640×480 screens**, on a virtual screen.

---

## Uninstalling

Clear the Steam launch option if you set it, then delete from the game folder:

```
ref_vr.dll   openxr_loader.dll   SiN VR*.bat   SiN Gold VR - Read Me.txt   SiN Gold VR licences\
sinvr\   sinvr2015\   sinvr.log
```

`sinvr2015\locale` is only a link to the game's own `2015\locale`: deleting it leaves the game's folder
alone. The game's own files were never changed.

---

## Troubleshooting

**The game runs on the monitor but nothing appears in the headset.**
Start SteamVR first and check it shows the headset. `sinvr.log` beside `sin.exe` says what the VR side
did, and says plainly when something fails.

**The view is offset or facing the wrong way.**
Click the left stick twice to recentre, standing up straight.

**Controller buttons do nothing, or the wrong thing.**
SteamVR may have kept an older or custom binding: SteamVR → Settings → Controllers → Manage Controller
Bindings. The headset also has to be awake for the controllers to work.

**Wages of Sin's New Game starts SiN.**
Start it with `SiN VR - Wages of Sin.bat` once, which links its menus into place.

**The game drops to the monitor after you enter a name, or shows `GLimp_EndFrame() - SwapBuffers() failed!`.**
This happened in 1.0.0 on the first start of Wages of Sin if you had played it before without the mod. Update to
1.0.1 or later.

**Reporting a bug.**
Send `sinvr.log` from beside `sin.exe` and `qconsole.log` from `sinvr\` (or `sinvr2015\`).

---

## Licence

This project's own code (`src/`, `tools/`, `cmake/`, the build files and the documentation) is
released under the **Apache License 2.0**. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

Not covered by it, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md):

- **The SiN Gold game source** in `game/` (and the `gamex86.dll` built from it) is © 2020 Night Dive
  Studios, used under their licence ([game/license.txt](game/license.txt)): non-commercial use, with
  SiN Gold only.
- **The OpenXR loader** (Apache 2.0, © The Khronos Group). `openxr_loader.dll` is built unmodified from
  the OpenXR SDK.

SiN and SiN Gold are © their owners. This mod is unofficial, not affiliated with or endorsed by Ritual
Entertainment, Night Dive Studios, Activision or their successors, contains no game data, and needs a
legitimate copy of the game.

---

> [!IMPORTANT]
> ## Thanks
>
> This mod exists because other people solved harder problems first and published their work.
>
> - **Night Dive Studios**, for re-releasing SiN Gold and publishing its game source.
> - **Ritual Entertainment**, for making SiN, and **2015, Inc.** for Wages of Sin.
> - **The Khronos Group** and the OpenXR contributors, and **Valve** for SteamVR's OpenXR runtime,
>   which still runs a 32-bit game from 1998.
> - **Team Beef** (Quake2Quest) and **GameOrDie007** (Quake II PCVR), whose Quake II VR ports were
>   studied for their design. No code from them is included.

Any mistakes in this mod are my own, not theirs.
