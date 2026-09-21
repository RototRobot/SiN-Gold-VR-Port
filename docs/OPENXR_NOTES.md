# OpenXR notes (32-bit, development PC)

These are measured facts about the OpenXR path SiN will use. The machine is a GTX 1080 Ti with an HP Reverb G2
running through SteamVR's WMR "Oasis" driver. Add to this file as we learn more, and say how each fact was
established.

## Decision

**OpenXR, not OpenVR** (2026-09-15). SiN is a 32-bit game, so everything below concerns the 32-bit path.

## Building the 32-bit loader

The source is Khronos' `..\OpenXR-SDK-Source-release-1.1.63`. It needs nothing beyond what is already installed:

- CMake 3.31 ships inside VS Build Tools (it just isn't on PATH).
- jinja2 is bundled in the SDK's `external/python`.

```
set CMAKE="%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
%CMAKE% -S OpenXR-SDK-Source-release-1.1.63 -B OpenXR-SDK-Source-release-1.1.63\build\win32 ^
        -G "Visual Studio 17 2022" -A Win32 -DDYNAMIC_LOADER=ON -DBUILD_API_LAYERS=OFF ^
        -DBUILD_TESTS=OFF -DBUILD_CONFORMANCE_TESTS=OFF -DBUILD_SDK_TESTS=OFF
%CMAKE% --build OpenXR-SDK-Source-release-1.1.63\build\win32 --config Release --target openxr_loader
```

The build takes about 36 s and produces:

- `build\win32\src\loader\Release\openxr_loader.dll` and its `.lib`. The DLL is x86 with a static CRT and imports only
  KERNEL32 and ADVAPI32, so it can ship beside `sin.exe` with no runtime installer.
- The generated headers, in `build\win32\include\openxr\`.

## Probe results (`tools\xrprobe`, 2026-09-15)

| | |
|---|---|
| Runtime | SteamVR/OpenXR 2.17.9, loaded through the 32-bit manifest `steamxr_win32.json` (→ `bin\vrclient.dll`) |
| API version | **1.1 is rejected** (`XR_ERROR_API_VERSION_UNSUPPORTED`) and **1.0 is accepted**, so request 1.0. Quake II PCVR saw the same with a different loader, so this is the runtime, not the loader. |
| System | "Windows Mixed Reality (holographic)", vendor 0x28de, orientation and position tracking |
| Recommended per eye | 2444 × 2392 at 1 sample. This follows SteamVR's resolution setting. |
| Maximum | 8192 × 8192 swapchain images, 16 composition layers |
| Blend modes | opaque only |
| OpenGL | `XR_KHR_opengl_enable` v12, which requires GL **4.3–4.6**. SiN's `ref_gl` gets a 4.6 compatibility context on this GPU, so it qualifies. |
| Useful extensions | `XR_EXT_hp_mixed_reality_controller`, `XR_KHR_composition_layer_depth`, `XR_KHR_visibility_mask`, `XR_EXT_local_floor`, `XR_KHR_locate_spaces` |
| Side effect | Creating the instance started SteamVR's `vrserver` within about 3 s. |

## First session on SiN's own GL context (2026-09-15)

This was `ref_vr` with `vr_enable 1`, in an unattended run with the headset on the desk.

- **Layers and instance.** The three VIVE layers were switched off automatically, and `xrCreateInstance` succeeded with API 1.0.
- **Start-up time.** `xrCreateInstance` took about 12 s, because it started SteamVR (`vrserver`, `vrmonitor`,
  `vrcompositor`). SteamVR stays running after the game quits.
- **Session.** `xrCreateSession` on ref_gl's legacy `wglCreateContext` context (GL 4.6 compatibility) worked first
  time, and the session went IDLE → READY → SYNCHRONIZED → VISIBLE → FOCUSED.
- **Swapchain.** The virtual-screen swapchain was 640×480 in `GL_SRGB8_ALPHA8` (0x8C43), with 3 images. The first frame was
  submitted 0.3 s after the session was created, and 315 frames were shown before a clean shutdown.
- **Focus with nobody wearing the headset.** Focus dropped to VISIBLE/SYNCHRONIZED and then returned. Expect this whenever the
  headset idles or the SteamVR dashboard opens.

## Stereo through a projection layer (2026-09-16)

Same session, now with per-eye swapchains and an `XrCompositionLayerProjection`. From an unattended run with
the headset on the desk (`tools\desk-test.ps1 -Vr`, which sets `vr_force_render 1`):

- **Eye swapchains** were created at the recommended 2444x2392 in `GL_SRGB8_ALPHA8` (0x8C43), one per eye,
  each given our own `DEPTH24_STENCIL8` renderbuffer in an FBO of ours. The runtime hands out the colour
  texture only; depth is the app's problem unless `XR_KHR_composition_layer_depth` is used.
- **406 projection frames** were submitted in one run at about 87 frames/s for both eyes on a GTX 1080 Ti.
  `xrWaitFrame` paces the loop, so the GL swap interval must stay at 0.
- **`xrLocateViews` numbers** were sane: per-eye asymmetric tangents, mirrored between the eyes
  (left `L-1.300 R0.886 D-1.050 U1.056`, right `L-0.886 R1.300 D-1.050 U1.056`), and eye positions 66 mm
  apart, which is this headset's IPD.
- **`XR_VIEW_STATE_ORIENTATION_VALID_BIT`** is the gate on using those poses. When it is clear, the frame is
  submitted with no layers rather than with a guessed pose.
- **An idle headset stops the session at `SYNCHRONIZED`** and every `xrWaitFrame` returns
  `shouldRender = false`. The session only reaches VISIBLE/FOCUSED once the headset is picked up. Honouring
  `shouldRender` therefore means an unattended desk run renders *nothing*, and the log said nothing about
  why - which cost a full test round. Two lessons, both now in the code: say it in the log when the runtime
  asks for no frames, and keep a way to render anyway (`vr_force_render 1`). Submitting layers while
  `shouldRender` is false is legal; the compositor may ignore them.
- **Menus and loading screens** are not stereo frames, so they still go on the quad layer. Switching between
  a projection layer and a quad layer from frame to frame is fine: the same session, one layer each frame.

## Trap: implicit layers registered for 32-bit that ship only 64-bit DLLs

**The problem.** The VIVE driver registers three implicit API layers (hand tracking, facial tracking and SRWorks) under
`HKLM\SOFTWARE\WOW6432Node\Khronos\OpenXR\1\ApiLayers\Implicit`, and all three point at 64-bit DLLs. A 32-bit
process cannot load them (Windows error 193). The loader skips each one, but when **no** implicit layer loads it
fails `xrCreateInstance` with `XR_ERROR_FILE_ACCESS_ERROR` (−32). As a result, **every** 32-bit OpenXR app on this machine fails,
not only ours.

**The fix** works in-process and changes nothing on the system:

1. Before the first OpenXR call, read the implicit-layer manifests in the 32-bit registry view.
2. For every layer whose library is missing or is not x86, set that layer's `disable_environment` variable. On this machine those are
   `DISABLE_XR_APILAYER_VIVE_HAND_TRACKING_1`, `DISABLE_XR_APILAYER_VIVE_FACIAL_TRACKING_1` and
   `DISABLE_XR_APILAYER_VIVE_SRWORKS_1`.

`tools\xrprobe` already does this, and so does `ref_vr.dll` (`src/ref_vr/xr_layers.c` is shared by both),
because every player with VIVE software installed would hit the same failure.

## Diagnostics

- Set `XR_LOADER_DEBUG=all` (or `warn`) to get the loader's log on stderr. This is how the layer problem was found.
- `xrprobe` is built along with everything else, to `build\win32\stage\Release\tools\xrprobe.exe`. It needs a
  headset that is available, and it may start SteamVR.

## Build traps met on the way

- **Long paths.** MSBuild projects under the Claude scratch directory (`%TEMP%\claude\...`) exceed the 260-character path limit
  in their tlog files. Build in short paths outside `%TEMP%`.
- **`IUnknown`.** With `WIN32_LEAN_AND_MEAN` defined, include `<unknwn.h>` before `openxr_platform.h`, because its Win32 interop
  structs use `IUnknown`.

## The eye resolution (2026-09-21)

- **The runtime's recommended size is the "native" resolution in the sense that matters.**
  `xrEnumerateViewConfigurationViews` gives each eye a `recommendedImageRectWidth/Height`: the size that
  puts one rendered pixel on each display pixel at the middle of the lens, where the distortion
  magnifies most. It is core OpenXR 1.0, so every runtime fills it - WMR's, Oculus's and the rest, not
  only SteamVR's - and a runtime with a resolution setting of its own folds that setting in. There is no
  portable query for the panel's own pixel count, and rendering at it would be softer in the middle.
- **On SteamVR it moves.** The same Reverb G2 on the same machine was recommended 2444x2392, 2496x2444
  and 2524x2468 in different sessions: SteamVR's own resolution setting is in it, and that is on its
  automatic choice here. `vr_resolution` multiplies whatever SteamVR says, on each side; SteamVR's
  per-application resolution setting, if it is set, is already in the number.
- **The maximum it reports is not the maximum it will make.** SteamVR reports 8192x8192. On a fresh start
  it made 1.4 of the recommended size (3494x3422) without complaint, and live changes up and down -
  1 to 1.4, 1.2, 1.1 and back to 1 - all went through. But once a *smaller* size has been made in the
  session, a bigger one is refused with `XR_ERROR_RUNTIME_FAILURE`: 1 to 0.5 to 1.4 failed every time
  it was tried, and after that even 1 could not be made again. 3.0 was refused outright. It looks like
  this 32-bit process running out of address space in one piece, though that is not confirmed.
  `vid_restart` always made the size asked for.
- So a refusal is not the end of stereo any more: the eye buffers go back to the last size that was
  made (or the recommended one), the console says so and names `vid_restart`, and the frame carries on.
- Creating the new swapchains before destroying the old ones might dodge the fragmentation, at the cost
  of both sets for a frame. Untried.
