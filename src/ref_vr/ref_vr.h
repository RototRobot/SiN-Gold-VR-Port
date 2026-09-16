/* Declarations shared between the ref_vr sources. */
#ifndef REF_VR_H
#define REF_VR_H

#include "refapi.h"

/* Prints to sinvr.log and to the SiN console, prefixed "[sinvr]". */
void vr_printf(const char *fmt, ...);

/* The engine's cvars and console, through the import table it gave us. */
cvar_t *vr_cvar(const char *name, const char *value, int flags);
void vr_cvar_set(const char *name, const char *value);

/* Queues a console command, as a key bind would: no newline needed. */
void vr_command(const char *text);

/*
 * True while the game is showing something with no world behind it: a menu, the
 * loading screen, a briefing. Those listen to the keyboard rather than to
 * console commands, so the controllers have to speak to them differently.
 */
int vr_in_menu(void);

#endif /* REF_VR_H */
