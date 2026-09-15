# Working rules for SiN Gold VR

Read [docs/HANDOFF.md](docs/HANDOFF.md) before changing anything. It covers the architecture, the build and
test workflow, and the traps already hit.

1. **Build 32-bit with MSVC only** (`cmake --preset win32`). The game DLL must match `sin.exe`'s ABI.
2. **Never modify or replace files that ship with SiN Gold.** The mod runs through `+set vid_ref vr` and
   `+set game sinvr`.
3. **`game/` is Night Dive Studios' code, under their licence** (`game/license.txt`). Keep changes there minimal and
   commit them separately, so that `git diff 07ae69c -- game` shows exactly what we changed.
4. **No GPL code** from Quake2Quest or Quake II PCVR goes into this repository. Their ideas and measured values are fine;
   their code is not.
5. **Git is local.** The owner uploads to GitHub by hand, so never add a remote and never push. Keep commits small,
   and make sure each one builds.
6. **Verify at the desk first**, with unattended runs, `sinvr.log` and screenshots. Then give the owner a launcher and a
   short list of what to look for. Headset time is the scarce resource.
7. **Record what you learn**, and how you learned it, in `docs/ENGINE_NOTES.md` or `docs/OPENXR_NOTES.md`.
8. **Don't mix `Remove-Item` with Program Files paths.** On the development machine the command harness refuses any
   command containing both `Remove-Item` and a `C:\Program Files` path. Move test artifacts out of the game folder instead.
