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

#endif /* REF_VR_H */
