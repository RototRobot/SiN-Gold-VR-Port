# Working rules for SiN Gold VR

Read [docs/HANDOFF.md](docs/HANDOFF.md) before changing anything. It covers the architecture, the build and
test workflow, and the traps already hit.

1. **Build 32-bit with MSVC only** (`cmake --preset win32`). The game DLL must match `sin.exe`'s ABI.
2. **Never modify or replace files that ship with SiN Gold.** The mod runs through `+set vid_ref vr` and
   `+set game sinvr`.
3. **`game/` is Night Dive Studios' code, under their licence** (`game/license.txt`). Keep changes there minimal and
   commit them separately, so that `git diff 5236699 -- game` shows exactly what we changed.
4. **No GPL code** from Quake2Quest or Quake II PCVR goes into this repository. Their ideas and measured values are fine;
   their code is not.
5. **Git is local.** The owner uploads to GitHub by hand, so never add a remote and never push. Keep commits small,
   and make sure each one builds.
6. **Verify at the desk first**: `tools\desk-test.ps1` runs the game unattended and collects `sinvr.log` and
   screenshots. Then give the owner a launcher and a short list of what to look for. Headset time is the
   scarce resource. Two things to know before you read a failed run as a bug:
   - a headset nobody is wearing gets `shouldRender = false`, so VR runs need `vr_force_render 1` (`-Vr`
     passes it);
   - a level starts on a "PRESS ANY KEY TO CONTINUE" card, and the engine runs no console commands - and
     calls no renderer function - while it is up, so an unattended `+exec` run hangs there.
     `desk-test.ps1` posts a keypress until the world appears; `-NoCardKey` leaves it to the mod's own idle
     pump. When a run stops making progress, capture the screen before theorising about it;
   - a `wait` holds up everything behind it in the command buffer, so a `wait`-heavy test cfg swallows
     console commands the mod itself queues - use `desk-test.ps1 -KeepRunning` for those;
   - `exec` puts a whole cfg into an 8 KB command buffer, and a `wait`-heavy one overflows it, whereupon
     nothing in the file runs at all - not even the `map` on its first line - or, if the file fits but
     what was still queued behind it then doesn't, SiN quits with a fatal "SZ_GetSpace: overflow" box.
     Chain smaller cfgs instead, 3-4 KB each;
   - **a shot cannot be fired in an unattended run.** Neither a console `+attack` nor a key bound to it
     reaches the weapon code, though every other command and key does, so anything about shooting is for
     the owner's headset test;
   - SiN pauses and paints over the view when its window is not active (`-Foreground`), and archived
     cvars keep whatever a test left in them.
7. **Record what you learn**, and how you learned it, in `docs/ENGINE_NOTES.md` or `docs/OPENXR_NOTES.md`.
8. **Don't mix `Remove-Item` with Program Files paths.** On the development machine the command harness refuses any
   command containing both `Remove-Item` and a `C:\Program Files` path. Move test artifacts out of the game folder instead.
