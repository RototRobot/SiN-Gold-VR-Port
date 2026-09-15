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

`tools\xrprobe` already does this. **`ref_vr.dll` must do it too**, because every player with VIVE software installed will hit
the same failure.

## Diagnostics

- Set `XR_LOADER_DEBUG=all` (or `warn`) to get the loader's log on stderr. This is how the layer problem was found.
- `xrprobe` is built along with everything else, to `build\win32\stage\Release\tools\xrprobe.exe`. It needs a
  headset that is available, and it may start SteamVR.

## Build traps met on the way

- **Long paths.** MSBuild projects under the Claude scratch directory (`%TEMP%\claude\...`) exceed the 260-character path limit
  in their tlog files. Build in short paths outside `%TEMP%`.
- **`IUnknown`.** With `WIN32_LEAN_AND_MEAN` defined, include `<unknwn.h>` before `openxr_platform.h`, because its Win32 interop
  structs use `IUnknown`.
