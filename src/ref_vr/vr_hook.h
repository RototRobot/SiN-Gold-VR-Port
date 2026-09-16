/*
 * Patching another module's imports, in this process only. Used on ref_gl's GL
 * lookups (vr_gl.c) and on sin.exe's joystick calls (vr_joy.c).
 */
#ifndef VR_HOOK_H
#define VR_HOOK_H

#include <windows.h>

/*
 * Points one of a module's imported functions at replacement, and hands back
 * what was there if previous is not NULL. Returns 0 if the module does not
 * import that function by name, which also means nothing was changed.
 */
int vr_hook_import(HMODULE module, const char *dll, const char *function, void *replacement, void **previous);

#endif /* VR_HOOK_H */
