/*
 * The renderer interface between SiN Gold's sin.exe and its renderer DLLs.
 *
 * It is Quake 2's refexport_t / refimport_t design with SiN's additions,
 * reconstructed from the shipped binaries (see docs/ENGINE_NOTES.md). Only what
 * ref_vr uses is typed; everything else passes through as opaque pointers.
 */
#ifndef REFAPI_H
#define REFAPI_H

#include <stddef.h>

#define REF_API_VERSION 65

/*
 * Both tables are 37 dwords. GetRefAPI takes refimport_t by value and returns
 * refexport_t by value, exactly as ref_gl.dll's does.
 */
#define REF_IMPORT_SLOTS 37
#define REF_EXPORT_SLOTS 36 /* after api_version */

typedef struct
{
	void *slot[REF_IMPORT_SLOTS];
} refimport_t;

typedef struct
{
	int api_version;
	void *slot[REF_EXPORT_SLOTS];
} refexport_t;

/* refexport_t slots, as indexes into slot[]. Byte offset in the table = 4 + 4 * index. */
enum
{
	RE_INIT = 0,               /* +0x04 int  (*)(void *hinstance, void *wndproc); -1 on failure */
	RE_SHUTDOWN = 1,           /* +0x08 void (*)(void) */
	RE_BEGIN_REGISTRATION = 2, /* +0x0C */
	RE_REGISTER_MODEL = 3,     /* +0x10 */
	RE_REGISTER_SKIN = 4,      /* +0x14 */
	RE_REGISTER_PIC = 5,       /* +0x18 */
	RE_SET_SKY = 6,            /* +0x1C */
	RE_END_REGISTRATION = 7,   /* +0x20 */
	RE_RENDER_FRAME = 8,       /* +0x24 void (*)(refdef_t *fd) */
	RE_DRAW_GET_PIC_SIZE = 9,  /* +0x28 */
	RE_DRAW_PIC = 10,          /* +0x2C */
	RE_DRAW_STRETCH_PIC = 11,  /* +0x30 */
	RE_DRAW_CHAR = 12,         /* +0x34 */
	RE_DRAW_TILE_CLEAR = 13,   /* +0x38 */
	RE_DRAW_FILL = 14,         /* +0x3C */
	RE_DRAW_FADE_SCREEN = 15,  /* +0x40 void (*)(float r, float g, float b, float a) */
	RE_BEGIN_FRAME = 21,       /* +0x58 void (*)(float camera_separation) */
	RE_END_FRAME = 22,         /* +0x5C void (*)(void) */
	RE_APP_ACTIVATE = 23       /* +0x60 void (*)(int active) */
};

/* refimport_t slots ref_vr calls. The rest are passed through untouched. */
enum
{
	RI_CMD_EXECUTE_TEXT = 5, /* void  (*)(int exec_when, const char *text); 0 now, 1 next, 2 queued */
	RI_CON_PRINTF = 6,     /* void    (*)(int print_level, const char *fmt, ...) */
	RI_CVAR_GET = 12,      /* cvar_t *(*)(const char *name, const char *value, int flags) */
	RI_CVAR_SET = 13,      /* cvar_t *(*)(const char *name, const char *value) */
	RI_CVAR_SET_VALUE = 14 /* void    (*)(const char *name, float value) */
};

#define PRINT_ALL 0
#define PRINT_DEVELOPER 1

#define CVAR_ARCHIVE 1

typedef struct cvar_s
{
	char *name;
	char *string;
	char *latched_string;
	int flags;
	int modified;
	float value;
	struct cvar_s *next;
} cvar_t;

/*
 * The leading part of refdef_t, identical to Quake 2's; checked against
 * ref_gl's own r_newrefdef. The rest of the struct is not mapped yet, so it
 * must only ever be used through a pointer the engine supplies.
 */
#define RDF_UNDERWATER 1
#define RDF_NOWORLDMODEL 2

typedef struct
{
	int x, y, width, height;
	float fov_x, fov_y;
	float vieworg[3];
	float viewangles[3];
	float blend[4];
	float time;
	int rdflags;
} refdef_t;

/* Compile-time checks against the layouts measured in the binaries. */
typedef char refimport_size_check[sizeof(refimport_t) == 0x94 ? 1 : -1];
typedef char refexport_size_check[sizeof(refexport_t) == 0x94 ? 1 : -1];
typedef char refdef_vieworg_check[offsetof(refdef_t, vieworg) == 0x18 ? 1 : -1];
typedef char refdef_rdflags_check[offsetof(refdef_t, rdflags) == 0x44 ? 1 : -1];
typedef char cvar_value_check[offsetof(cvar_t, value) == 0x14 ? 1 : -1];

#endif /* REFAPI_H */
