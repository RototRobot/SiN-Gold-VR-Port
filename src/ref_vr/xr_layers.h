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

#endif /* XR_LAYERS_H */
