/*
 * Implicit OpenXR API layer hygiene for 32-bit processes (docs/OPENXR_NOTES.md).
 */
#ifndef XR_LAYERS_H
#define XR_LAYERS_H

typedef void (*xr_layer_report_t)(const char *library, const char *reason);

/*
 * Call before the first OpenXR call. Switches off, for this process only,
 * every enabled implicit API layer whose library a 32-bit process cannot load,
 * through the layer's own disable_environment variable. Without this the
 * loader fails xrCreateInstance whenever none of the implicit layers loads,
 * which is what VIVE's 64-bit-only layers do to every 32-bit app. Calls
 * report (if given) once per layer switched off, and returns how many.
 */
int xr_disable_unloadable_implicit_layers(xr_layer_report_t report);

/*
 * Call before the first OpenXR call, too. XR_RUNTIME_JSON, when set, takes the
 * loader to that runtime whatever the registry says - and Steam sets it, inside
 * a game that starts its library while SteamVR streams to a Quest, to
 * SteamVR's 64-bit manifest, which a 32-bit process cannot load: the loader
 * then fails xrCreateInstance with XR_ERROR_RUNTIME_UNAVAILABLE. So a manifest
 * whose library is not 32-bit is swapped, for this process, for its 32-bit
 * sibling (steamxr_win64.json -> steamxr_win32.json) when there is one, and
 * otherwise the override is dropped, leaving the registry's 32-bit runtime.
 * Calls report (if given) with what was there and what was done; returns 1 if
 * anything changed.
 */
int xr_fix_runtime_override(xr_layer_report_t report);

#endif /* XR_LAYERS_H */
