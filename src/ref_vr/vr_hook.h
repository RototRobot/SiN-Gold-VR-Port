/*
 * Patching another module, in this process only: its imports - ref_gl's GL
 * lookups (vr_gl.c) and sin.exe's joystick calls (vr_joy.c) - and, where there
 * is no import to catch, single direct calls inside sin.exe (vr_weapon.c).
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

/*
 * Points one direct call inside a module (E8 rel32, at site_rva) at replacement
 * instead of the function at target_rva, and hands back the address of that
 * function in original. Nothing is changed unless the instruction is exactly
 * that call, so a different build of the module is left alone: returns 0.
 * The module's base comes from the running process, so ASLR does not matter.
 */
int vr_hook_call(HMODULE module, DWORD site_rva, DWORD target_rva, void *replacement, void **original);

/* Puts back every call vr_hook_call changed. Before this DLL is unloaded. */
void vr_unhook_calls(void);

#endif /* VR_HOOK_H */
