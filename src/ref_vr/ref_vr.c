/*
 * ref_vr.dll - the renderer SiN Gold VR plugs into the engine.
 *
 * sin.exe loads "ref_<vid_ref>.dll", so "+set vid_ref vr" loads this module in
 * place of ref_gl.dll. It loads the stock ref_gl.dll itself and gives the
 * engine ref_gl's own interface, with every call routed through here:
 *
 *   - Init, Shutdown, BeginFrame, RenderFrame and EndFrame are wrapped in C;
 *   - every other export goes through a thunk that records the call and jumps on.
 *
 * Nothing is changed on the way through yet. The recording shows the order the
 * engine drives the renderer in, which the stereo work depends on. With
 * vr_enable set, the finished frame is also shown on a virtual screen in the
 * headset (vr_xr.c).
 */
#include <windows.h>
#include <GL/gl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "refapi.h"
#include "ref_vr.h"
#include "vr_xr.h"
#include "vrlog.h"

#ifndef SINVR_VERSION
#define SINVR_VERSION "dev"
#endif

typedef refexport_t (*get_ref_api_t)(refimport_t imports);
typedef int (*re_init_t)(void *hinstance, void *wndproc);
typedef void (*re_void_t)(void);
typedef void (*re_begin_frame_t)(float camera_separation);
typedef void (*re_render_frame_t)(refdef_t *fd);
typedef void (*con_printf_t)(int print_level, const char *fmt, ...);
typedef cvar_t *(*cvar_get_t)(const char *name, const char *value, int flags);

static HMODULE g_self;
static HMODULE g_refgl;
static refimport_t g_ri;
static refexport_t g_ours;

/* ref_gl's exports as a flat array, so the thunks can jump through it. */
static void *g_real_slot[REF_EXPORT_SLOTS];
static unsigned g_slot_calls[REF_EXPORT_SLOTS];

static cvar_t *vr_enable;
static cvar_t *vr_trace;
static cvar_t *vr_screen_distance;
static cvar_t *vr_screen_width;

/* ------------------------------------------------------------------------- */
/* Output                                                                     */
/* ------------------------------------------------------------------------- */

void vr_printf(const char *fmt, ...)
{
	char text[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	vrlog("%s", text);

	if (g_ri.slot[RI_CON_PRINTF])
	{
		((con_printf_t)g_ri.slot[RI_CON_PRINTF])(PRINT_ALL, "[sinvr] %s\n", text);
	}
}

static const char *slot_name(int slot)
{
	static const char *const names[REF_EXPORT_SLOTS] = {
		"Init", "Shutdown", "BeginRegistration", "RegisterModel", "RegisterSkin", "RegisterPic",
		"SetSky", "EndRegistration", "RenderFrame", "DrawGetPicSize", "DrawPic", "DrawStretchPic",
		"DrawChar", "DrawTileClear", "DrawFill", "DrawFadeScreen", NULL, NULL,
		NULL, NULL, NULL, "BeginFrame", "EndFrame", "AppActivate",
		NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL
	};
	static char unnamed[REF_EXPORT_SLOTS][16];

	if (names[slot])
	{
		return names[slot];
	}

	if (!unnamed[slot][0])
	{
		snprintf(unnamed[slot], sizeof(unnamed[slot]), "slot+0x%02X", 4 + 4 * slot);
	}

	return unnamed[slot];
}

/* Appends to a fixed buffer, never past its end. */
static void append(char *buffer, size_t size, size_t *used, const char *fmt, ...)
{
	va_list args;
	int n;

	if (*used + 1 >= size)
	{
		return;
	}

	va_start(args, fmt);
	n = vsnprintf(buffer + *used, size - *used, fmt, args);
	va_end(args);

	if (n > 0)
	{
		*used += ((size_t)n < size - *used) ? (size_t)n : size - *used - 1;
	}
}

/* ------------------------------------------------------------------------- */
/* Call recording                                                             */
/* ------------------------------------------------------------------------- */

/* One entry per run of identical calls within an engine frame. */
#define TRACE_MAX 160

typedef struct
{
	unsigned char slot;
	unsigned short count;
} trace_entry_t;

typedef struct
{
	int x, y, width, height;
	float fov_x, fov_y;
	float vieworg[3];
	float viewangles[3];
	int rdflags;
} view_note_t;

static trace_entry_t g_trace[TRACE_MAX];
static int g_trace_len;
static int g_trace_overflow;

static float g_separations[4];
static int g_separation_count;
static view_note_t g_views[2];
static int g_view_count;
static int g_frame_has_world;

static unsigned g_frame_number;
static unsigned g_frames_logged;
static unsigned g_world_frames_logged;
static unsigned g_signatures[64];
static int g_signature_count;
static DWORD g_stats_time;
static unsigned g_stats_frames;

static void __cdecl record_call(int slot)
{
	trace_entry_t *last = g_trace_len ? &g_trace[g_trace_len - 1] : NULL;

	g_slot_calls[slot]++;

	if (last && last->slot == slot)
	{
		if (last->count < 65535)
		{
			last->count++;
		}
	}
	else if (g_trace_len < TRACE_MAX)
	{
		g_trace[g_trace_len].slot = (unsigned char)slot;
		g_trace[g_trace_len].count = 1;
		g_trace_len++;
	}
	else
	{
		g_trace_overflow = 1;
	}
}

/* True the first time a frame of this shape is seen. */
static int new_signature(unsigned signature)
{
	int i;

	for (i = 0; i < g_signature_count; ++i)
	{
		if (g_signatures[i] == signature)
		{
			return 0;
		}
	}

	if (g_signature_count < (int)(sizeof(g_signatures) / sizeof(g_signatures[0])))
	{
		g_signatures[g_signature_count++] = signature;
	}

	return 1;
}

/*
 * Logs the frame just finished when its sequence of calls has a shape not seen
 * before, for the first few frames that draw the world, and now and then
 * after that. Keeps the log to a few hundred lines however long the session.
 */
static void end_frame_trace(void)
{
	char line[2048];
	size_t used = 0;
	unsigned signature = 2166136261u;
	DWORD now = GetTickCount();
	int i, log_it;

	for (i = 0; i < g_trace_len; ++i)
	{
		signature = (signature ^ g_trace[i].slot) * 16777619u;
	}

	log_it = new_signature(signature);
	if (g_frame_has_world && g_world_frames_logged < 3)
	{
		log_it = 1;
	}
	if (g_frame_number % 5000 == 0)
	{
		log_it = 1;
	}
	if (g_frames_logged >= 400 || !vr_trace || vr_trace->value == 0.0f)
	{
		log_it = 0;
	}

	if (log_it)
	{
		append(line, sizeof(line), &used, "frame %u:", g_frame_number);
		for (i = 0; i < g_trace_len; ++i)
		{
			if (g_trace[i].count > 1)
			{
				append(line, sizeof(line), &used, " %s x%u", slot_name(g_trace[i].slot), g_trace[i].count);
			}
			else
			{
				append(line, sizeof(line), &used, " %s", slot_name(g_trace[i].slot));
			}
		}
		if (g_trace_overflow)
		{
			append(line, sizeof(line), &used, " ...");
		}
		vrlog("%s", line);

		for (i = 0; i < g_separation_count; ++i)
		{
			vrlog("    BeginFrame camera_separation %.4f", g_separations[i]);
		}

		for (i = 0; i < g_view_count; ++i)
		{
			const view_note_t *v = &g_views[i];

			vrlog("    RenderFrame view %d,%d %dx%d fov %.1f/%.1f org (%.1f %.1f %.1f) ang (%.1f %.1f %.1f) rdflags 0x%x",
				v->x, v->y, v->width, v->height, v->fov_x, v->fov_y,
				v->vieworg[0], v->vieworg[1], v->vieworg[2],
				v->viewangles[0], v->viewangles[1], v->viewangles[2], v->rdflags);
		}

		g_frames_logged++;
		if (g_frame_has_world)
		{
			g_world_frames_logged++;
		}
	}

	if (!g_stats_time)
	{
		g_stats_time = now;
		g_stats_frames = g_frame_number;
	}
	else if (now - g_stats_time >= 10000)
	{
		vrlog("stats: %.1f frames/s over the last %.0f s",
			(g_frame_number - g_stats_frames) * 1000.0 / (now - g_stats_time), (now - g_stats_time) / 1000.0);
		g_stats_time = now;
		g_stats_frames = g_frame_number;
	}

	g_trace_len = 0;
	g_trace_overflow = 0;
	g_separation_count = 0;
	g_view_count = 0;
	g_frame_has_world = 0;
	g_frame_number++;
}

/*
 * One thunk per export: record the call, then jump straight into ref_gl with
 * the engine's stack untouched, so the signature never needs to be known.
 * ref_gl's exports are all plain cdecl, but eax/ecx/edx are kept anyway.
 */
#define SLOT_THUNK(i)                                          \
	static __declspec(naked) void slot_thunk_##i(void)        \
	{                                                          \
		__asm push eax                                         \
		__asm push ecx                                         \
		__asm push edx                                         \
		__asm push i                                           \
		__asm call record_call                                 \
		__asm add esp, 4                                       \
		__asm pop edx                                          \
		__asm pop ecx                                          \
		__asm pop eax                                          \
		__asm jmp dword ptr [g_real_slot + i * 4]              \
	}

SLOT_THUNK(0)  SLOT_THUNK(1)  SLOT_THUNK(2)  SLOT_THUNK(3)  SLOT_THUNK(4)  SLOT_THUNK(5)
SLOT_THUNK(6)  SLOT_THUNK(7)  SLOT_THUNK(8)  SLOT_THUNK(9)  SLOT_THUNK(10) SLOT_THUNK(11)
SLOT_THUNK(12) SLOT_THUNK(13) SLOT_THUNK(14) SLOT_THUNK(15) SLOT_THUNK(16) SLOT_THUNK(17)
SLOT_THUNK(18) SLOT_THUNK(19) SLOT_THUNK(20) SLOT_THUNK(21) SLOT_THUNK(22) SLOT_THUNK(23)
SLOT_THUNK(24) SLOT_THUNK(25) SLOT_THUNK(26) SLOT_THUNK(27) SLOT_THUNK(28) SLOT_THUNK(29)
SLOT_THUNK(30) SLOT_THUNK(31) SLOT_THUNK(32) SLOT_THUNK(33) SLOT_THUNK(34) SLOT_THUNK(35)

static void (*const g_thunks[REF_EXPORT_SLOTS])(void) = {
	slot_thunk_0,  slot_thunk_1,  slot_thunk_2,  slot_thunk_3,  slot_thunk_4,  slot_thunk_5,
	slot_thunk_6,  slot_thunk_7,  slot_thunk_8,  slot_thunk_9,  slot_thunk_10, slot_thunk_11,
	slot_thunk_12, slot_thunk_13, slot_thunk_14, slot_thunk_15, slot_thunk_16, slot_thunk_17,
	slot_thunk_18, slot_thunk_19, slot_thunk_20, slot_thunk_21, slot_thunk_22, slot_thunk_23,
	slot_thunk_24, slot_thunk_25, slot_thunk_26, slot_thunk_27, slot_thunk_28, slot_thunk_29,
	slot_thunk_30, slot_thunk_31, slot_thunk_32, slot_thunk_33, slot_thunk_34, slot_thunk_35
};

/* ------------------------------------------------------------------------- */
/* Wrapped exports                                                            */
/* ------------------------------------------------------------------------- */

static void register_cvars(void)
{
	cvar_get_t cvar_get = (cvar_get_t)g_ri.slot[RI_CVAR_GET];

	vr_enable = cvar_get("vr_enable", "0", 0);
	vr_trace = cvar_get("vr_trace", "1", 0);
	vr_screen_distance = cvar_get("vr_screen_distance", "2.5", CVAR_ARCHIVE);
	vr_screen_width = cvar_get("vr_screen_width", "2.4", CVAR_ARCHIVE);
}

static int vr_Init(void *hinstance, void *wndproc)
{
	int result;

	record_call(RE_INIT);
	register_cvars();

	result = ((re_init_t)g_real_slot[RE_INIT])(hinstance, wndproc);

	if (result == -1)
	{
		vr_printf("ref_gl.dll failed to initialise");
	}
	else
	{
		vr_printf("SiN Gold VR renderer %s over ref_gl.dll: %s | %s | GL %s", SINVR_VERSION,
			(const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER),
			(const char *)glGetString(GL_VERSION));
	}

	return result;
}

static void vr_Shutdown(void)
{
	int i;

	record_call(RE_SHUTDOWN);

	/* Before ref_gl destroys the GL context the OpenXR session belongs to. */
	vr_xr_shutdown();

	((re_void_t)g_real_slot[RE_SHUTDOWN])();

	vrlog("Shutdown after %u frames. Calls per export over this load:", g_frame_number);
	for (i = 0; i < REF_EXPORT_SLOTS; ++i)
	{
		if (g_slot_calls[i])
		{
			vrlog("    %-18s %u", slot_name(i), g_slot_calls[i]);
		}
	}

	/* The engine frees this DLL straight after Shutdown, and would have freed
	   ref_gl at the same point, so ref_gl goes now too. */
	if (g_refgl)
	{
		FreeLibrary(g_refgl);
		g_refgl = NULL;
	}

	vrlog_close();
}

static void vr_BeginFrame(float camera_separation)
{
	record_call(RE_BEGIN_FRAME);

	if (g_separation_count < 4)
	{
		g_separations[g_separation_count++] = camera_separation;
	}

	((re_begin_frame_t)g_real_slot[RE_BEGIN_FRAME])(camera_separation);
}

static void vr_RenderFrame(refdef_t *fd)
{
	record_call(RE_RENDER_FRAME);

	if (fd)
	{
		if (g_view_count < 2)
		{
			view_note_t *v = &g_views[g_view_count++];

			v->x = fd->x;
			v->y = fd->y;
			v->width = fd->width;
			v->height = fd->height;
			v->fov_x = fd->fov_x;
			v->fov_y = fd->fov_y;
			memcpy(v->vieworg, fd->vieworg, sizeof(v->vieworg));
			memcpy(v->viewangles, fd->viewangles, sizeof(v->viewangles));
			v->rdflags = fd->rdflags;
		}

		if (!(fd->rdflags & RDF_NOWORLDMODEL))
		{
			g_frame_has_world = 1;
		}
	}

	((re_render_frame_t)g_real_slot[RE_RENDER_FRAME])(fd);
}

static void vr_EndFrame(void)
{
	record_call(RE_END_FRAME);

	/* The finished frame is still in the back buffer here, before the swap. */
	if (vr_enable && vr_enable->value != 0.0f)
	{
		vr_xr_frame(vr_screen_distance->value, vr_screen_width->value);
	}
	else
	{
		vr_xr_shutdown(); /* nothing to do unless VR was switched off while running */
	}

	((re_void_t)g_real_slot[RE_END_FRAME])();

	end_frame_trace();
}

/* ------------------------------------------------------------------------- */
/* Entry points                                                               */
/* ------------------------------------------------------------------------- */

static void log_tables(const refexport_t *real)
{
	HMODULE exe = GetModuleHandleA(NULL);
	int i;

	vrlog("ref_gl.dll at %p, api_version %d. Exports:", (void *)g_refgl, real->api_version);
	for (i = 0; i < REF_EXPORT_SLOTS; ++i)
	{
		vrlog("    +0x%02X %-18s ref_gl+0x%05X", 4 + 4 * i, slot_name(i),
			(unsigned)((const char *)real->slot[i] - (const char *)g_refgl));
	}

	vrlog("sin.exe at %p. Imports it gave the renderer:", (void *)exe);
	for (i = 0; i < REF_IMPORT_SLOTS; ++i)
	{
		const char *p = (const char *)g_ri.slot[i];
		const char *base = (const char *)exe;

		if (p >= base && p < base + 0x600000)
		{
			vrlog("    [%2d] sin.exe+0x%06X", i, (unsigned)(p - base));
		}
		else
		{
			vrlog("    [%2d] %p", i, (const void *)p);
		}
	}
}

/*
 * The engine's entry point into a renderer. The tables go by value both ways,
 * which MSVC's C ABI does the same way ref_gl.dll does: the result through a
 * hidden pointer, the argument copied onto the stack.
 */
refexport_t GetRefAPI(refimport_t imports)
{
	get_ref_api_t real_get_ref_api;
	refexport_t real;
	int i;

	g_ri = imports;
	vrlog_open(g_self);
	vrlog("SiN Gold VR renderer %s loading", SINVR_VERSION);

	/* api_version 0 makes the engine refuse this renderer cleanly. */
	memset(&g_ours, 0, sizeof(g_ours));

	g_refgl = LoadLibraryA("ref_gl.dll");
	if (!g_refgl)
	{
		vr_printf("could not load ref_gl.dll (error %lu)", GetLastError());
		return g_ours;
	}

	real_get_ref_api = (get_ref_api_t)GetProcAddress(g_refgl, "GetRefAPI");
	if (!real_get_ref_api)
	{
		vr_printf("ref_gl.dll has no GetRefAPI");
		FreeLibrary(g_refgl);
		g_refgl = NULL;
		return g_ours;
	}

	real = real_get_ref_api(imports);
	if (real.api_version != REF_API_VERSION)
	{
		/* Passed on regardless: the engine makes the same check and says so. */
		vr_printf("ref_gl.dll reports api_version %d, expected %d", real.api_version, REF_API_VERSION);
	}

	log_tables(&real);

	g_ours.api_version = real.api_version;
	for (i = 0; i < REF_EXPORT_SLOTS; ++i)
	{
		g_real_slot[i] = real.slot[i];
		g_ours.slot[i] = (void *)g_thunks[i];
	}

	g_ours.slot[RE_INIT] = (void *)vr_Init;
	g_ours.slot[RE_SHUTDOWN] = (void *)vr_Shutdown;
	g_ours.slot[RE_BEGIN_FRAME] = (void *)vr_BeginFrame;
	g_ours.slot[RE_RENDER_FRAME] = (void *)vr_RenderFrame;
	g_ours.slot[RE_END_FRAME] = (void *)vr_EndFrame;

	return g_ours;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	(void)reserved;

	if (reason == DLL_PROCESS_ATTACH)
	{
		g_self = instance;
		DisableThreadLibraryCalls(instance);
	}

	return TRUE;
}
