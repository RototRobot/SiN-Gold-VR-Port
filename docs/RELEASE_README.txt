SiN Gold VR {VERSION}
====================

A PC VR mod for SiN Gold (Night Dive Studios' re-release of Ritual's 1998 shooter), for SiN and its
expansion, Wages of Sin: stereo 3D, head and hand tracking, the weapon in your hand, punching with
your fists, walking and crouching in your room, and a comfort vignette.

This mod was made with heavy AI (Claude) assistance.

It changes nothing that ships with the game: it adds its own files beside it, and the game without
the mod is just as it was.


WHAT YOU NEED

- SiN Gold on Steam (or GOG), installed.
- A PC VR headset that works with SteamVR. Tested with an HP Reverb G2 and a Meta Quest 3 over
  Steam Link.
- SteamVR, running before you start the game.


INSTALLING

1. Find your SiN Gold folder. On Steam: right-click SiN Gold in your library, Manage, Browse local
   files. It is the folder with sin.exe in it.
2. Unzip everything in this zip into that folder, keeping the folders as they are. You should end
   up with ref_vr.dll and openxr_loader.dll beside sin.exe, a "sinvr" and a "sinvr2015" folder, and
   the "SiN VR" launchers.


PLAYING

Start SteamVR and put the headset on, then double-click one of the launchers:

  SiN VR - Wages of Sin.bat                RECOMMENDED: Wages of Sin in VR, and its New Game
                                           also offers the original SiN
  SiN VR.bat                               the original SiN on its own, as it shipped
  SiN VR (flatscreen).bat                  the same mod with VR off
  SiN VR - Wages of Sin (flatscreen).bat   the same, for Wages of Sin

Use "SiN VR - Wages of Sin.bat" for both campaigns: its New Game asks which adventure to play, and
one launcher keeps one set of settings and grips.

Or from Steam's own Play button: right-click SiN Gold, Properties, Launch Options, and enter

  +set game sinvr2015 +set vid_ref vr +set vr_enable 1

Run "SiN VR - Wages of Sin.bat" once first, so that Wages of Sin's menus are found. Empty the box
again to play without VR.

Stand up straight and click the left stick twice to recentre when you start: crouching is measured
from there.

MOVE AND RESIZE YOUR GUNS - every grip starts from a best guess, so set the ones you use:
  1. With the weapon out, squeeze and HOLD the grip of the hand holding it (about a second).
     The gun stops following your hand.
  2. Move your hand to where you want to hold it.
  3. Push that hand's stick up or down to make the gun bigger or smaller.
  4. Let go of the grip. It is remembered for each weapon.


CONTROLS (right-handed; vr_left_handed 1 swaps the hands, vr_swap_sticks 1 swaps the sticks
and A/X on their own)

  Left stick            move (forward is where you look; vr_move_dir changes it)
  Right stick           turn - smoothly, or in steps with vr_snap_turn 1
  Right trigger         fire          Left trigger      use
  A                     jump          X                 crouch
  B                     switch the weapon's mode (right mouse button)
  Y                     use the selected inventory item (flashlight, goggles, powerups)
  Grip at the other hip tap: use the inventory item; hold: the inventory menu, to select one
  Grip over a shoulder, held                        the weapon menu
  In a menu: that hand's trigger steps along, take the hand out of the holster to pick,
  let go of the grip in the holster to cancel
  Right stick click     menu          Left stick, twice recentre
  Left grip             run, or hold a long gun's front with your other hand
  Right grip, held      let go of the weapon to set how it sits in your hand
  Grip over a shoulder  next / previous weapon      Grip at your hip   the magnum
  Both grips at your shoulders                      the fists
  Swing a fist          punch (harder the faster you swing)

Every setting is a console command or variable - vr_vignette, vr_snap_turn, vr_left_handed and the
rest - saved in sinvr\players\blade\config.cfg (sinvr2015\... for Wages of Sin). The full list, and
how everything works, is in the README on the project's page.


UNINSTALLING

Delete ref_vr.dll, openxr_loader.dll, the four "SiN VR" launchers and the sinvr and sinvr2015
folders from the SiN Gold folder. (sinvr2015\locale is only a link to the game's own 2015\locale:
deleting the link leaves that folder alone.)


IF SOMETHING GOES WRONG

- Nothing in the headset: is SteamVR running and showing the headset? Start it before the game.
- sinvr.log, beside sin.exe, says what the VR side did; sinvr\qconsole.log is the game's console.
  Both are worth attaching to a bug report.
- Leave the game's video options alone while playing through the mod: they were made for a
  monitor, and have not been tested in VR.


SiN is by Ritual Entertainment. SiN Gold is by Night Dive Studios. This is a fan mod, not affiliated
with either. The mod's own code is under the Apache License 2.0; the game logic (gamex86.dll) is built
from Night Dive's released source and is under their licence, for use with SiN Gold only. See the
"SiN Gold VR licences" folder.
