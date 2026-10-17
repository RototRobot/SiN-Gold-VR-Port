# Third-party notices

SiN Gold VR's own code is under the Apache License 2.0 ([LICENSE](LICENSE)). These parts are not, and
keep their own terms.

## SiN Gold game source - Night Dive Studios

`game/src/code/` (SiN in `game`, Wages of Sin in `game2015`, and `elib`) is the game logic Night Dive
Studios released in 2020, © Night Dive Studios. The `gamex86.dll` files in a release are built from it,
with this mod's changes (marked "SiN Gold VR" in the source; `git diff 5236699 -- game` lists every
one). It is used under Night Dive's licence, [game/license.txt](game/license.txt): non-commercial use,
with SiN Gold only. That licence, not Apache 2.0, applies to those files and to the DLLs built from
them.

## OpenXR loader - The Khronos Group

`openxr_loader.dll` is built, unmodified, from the OpenXR SDK source (release 1.1.63), © The Khronos
Group Inc., under the Apache License 2.0. Its licence text is in the release's `SiN Gold VR licences`
folder and at <https://github.com/KhronosGroup/OpenXR-SDK-Source>.

## SiN and SiN Gold

SiN is © Ritual Entertainment / Activision, and SiN Gold © Night Dive Studios. No game data is included
in this repository or in its releases; the mod needs a legitimate copy of SiN Gold.
