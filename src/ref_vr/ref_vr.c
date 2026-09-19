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
 * With vr_enable set the game goes to the headset (vr_xr.c). Frames that draw
 * the world are rendered in stereo through the engine's own two-pass loop
 * (cl_stereo): each BeginFrame pass gets an eye, RenderFrame gets that eye's
 * view (vr_view.c), and ref_gl's projection and viewports follow the eye
 * (vr_gl.c). Everything else is shown on the theatre screen.
 */
#include <windows.h>
#include <GL/gl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "refapi.h"
#include "ref_vr.h"
#include "vr_gl.h"
#include "vr_input.h"
#include "vr_view.h"
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
typedef void (*re_draw_fade_screen_t)(float red, float green, float blue, float alpha);
typedef void (*re_draw_pic_t)(int x, int y, const char *name);
typedef void (*re_draw_stretch_pic_t)(int x, int y, int width, int height, const char *name);
typedef void (*con_printf_t)(int print_level, const char *fmt, ...);
typedef cvar_t *(*cvar_get_t)(const char *name, const char *value, int flags);
typedef cvar_t *(*cvar_set_t)(const char *name, const char *value);
typedef void (*cmd_execute_text_t)(int exec_when, const char *text);

#define EXEC_APPEND 2 /* the command buffer runs it in turn, like a bind */

static HMODULE g_self;
static HMODULE g_refgl;
static refimport_t g_ri;
static refexport_t g_ours;

/* ref_gl's exports as a flat array, so the thunks can jump through it. */
static void *g_real_slot[REF_EXPORT_SLOTS];
static unsigned g_slot_calls[REF_EXPORT_SLOTS];

static cvar_t *vr_enable;
static cvar_t *vr_stereo;
static cvar_t *vr_worldscale;
static cvar_t *vr_hud_fov;
static cvar_t *vr_hud_depth;
static cvar_t *vr_trace;
static cvar_t *vr_force_render;
static cvar_t *vr_recenter;
static cvar_t *vr_dump_eye;
static cvar_t *vr_screen_distance;
static cvar_t *vr_screen_width;
static cvar_t *cl_stereo;

/* The frame in progress. */
static int g_pass;                 /* BeginFrame calls since the last EndFrame */
static int g_stereo_frame;         /* this frame renders the world once per eye */
static int g_last_frame_had_world;
static int g_last_frame_had_menu;
static const vr_eye_t *g_eye;      /* the eye being rendered, or NULL */
static int g_eye_index;
static int g_eye_renders_logged;
static int g_comfort_applied;

/* The idle pump (vr_xr_idle): a timer on the engine's window that looks after
   the headset whenever the engine has gone quiet. */
#define IDLE_TIMER_ID 0x53494E56 /* "SINV" */
#define IDLE_AFTER_MS 100
static HWND g_idle_window;
static DWORD g_last_draw_tick;
static int g_in_renderer;

static void start_idle_timer(void);

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
/* Cvars                                                                      */
/* ------------------------------------------------------------------------- */

static void register_cvars(void)
{
	cvar_get_t cvar_get = (cvar_get_t)g_ri.slot[RI_CVAR_GET];

	vr_enable = cvar_get("vr_enable", "0", 0);
	vr_stereo = cvar_get("vr_stereo", "1", CVAR_ARCHIVE);
	vr_worldscale = cvar_get("vr_worldscale", "39.37", CVAR_ARCHIVE);
	vr_hud_fov = cvar_get("vr_hud_fov", "60", CVAR_ARCHIVE);
	vr_hud_depth = cvar_get("vr_hud_depth", "6", CVAR_ARCHIVE);
	vr_trace = cvar_get("vr_trace", "1", 0);
	vr_force_render = cvar_get("vr_force_render", "0", 0);
	vr_recenter = cvar_get("vr_recenter", "0", 0);
	vr_dump_eye = cvar_get("vr_dump_eye", "0", 0);
	vr_screen_distance = cvar_get("vr_screen_distance", "2.5", CVAR_ARCHIVE);
	vr_screen_width = cvar_get("vr_screen_width", "2.4", CVAR_ARCHIVE);
	cl_stereo = cvar_get("cl_stereo", "0", 0);
}

cvar_t *vr_cvar(const char *name, const char *value, int flags)
{
	cvar_get_t cvar_get = (cvar_get_t)g_ri.slot[RI_CVAR_GET];

	return cvar_get ? cvar_get(name, value, flags) : NULL;
}

void vr_cvar_set(const char *name, const char *value)
{
	((cvar_set_t)g_ri.slot[RI_CVAR_SET])(name, value);
}

void vr_command(const char *text)
{
	cmd_execute_text_t execute = (cmd_execute_text_t)g_ri.slot[RI_CMD_EXECUTE_TEXT];
	char line[192];

	if (!execute || !text || !text[0])
	{
		return;
	}

	sprintf(line, "%.180s\n", text);
	execute(EXEC_APPEND, line);
}

/* The world goes to the headset in stereo when that is asked for and a session is running. */
/*
 * Testing at the desk: the runtime says a headset lying still needs no picture,
 * and this renders for it regardless, so an unattended run exercises the whole
 * path and leaves an eye image in the window.
 */
static int force_render(void)
{
	return vr_force_render && vr_force_render->value != 0.0f;
}

static int stereo_wanted(void)
{
	return vr_enable && vr_enable->value != 0.0f && vr_stereo && vr_stereo->value != 0.0f &&
		vr_xr_stereo_possible();
}

/*
 * The engine reads cl_stereo as each frame starts, so it is changed here, at the
 * end of one, for the next. The separation goes to zero so the engine adds no
 * eye offset of its own: the eyes come from the headset.
 */
static void update_stereo(void)
{
	static const char *const view_motion[] = {
		"bob_up", "bob_pitch", "bob_roll", "run_pitch", "run_roll", "sv_rollangle"
	};
	int want = stereo_wanted();
	int have = cl_stereo && cl_stereo->value != 0.0f;
	size_t i;

	if (!cl_stereo || want == have)
	{
		return;
	}

	vr_cvar_set("cl_stereo", want ? "1" : "0");

	if (!want)
	{
		vr_printf("VR: stereo off");
		return;
	}

	vr_cvar_set("cl_stereo_separation", "0");

	/* View bob and roll move the camera without the head moving. */
	if (!g_comfort_applied)
	{
		for (i = 0; i < sizeof(view_motion) / sizeof(view_motion[0]); ++i)
		{
			vr_cvar_set(view_motion[i], "0");
		}
		g_comfort_applied = 1;
	}

	vr_printf("VR: stereo on; view bob and roll off");
}

/*
 * Whether the keyboard belongs to the game or to some interface of the engine's
 * - a menu, the console, the chat line. sin.exe keeps Quake II's cls.key_dest
 * for this, and it is the only reliable way to tell: SiN draws its menus over a
 * world that is still being rendered, through its own draw calls, so there is
 * nothing in the renderer's own traffic that says so.
 *
 * The address came from the toggleconsole handler (docs/ENGINE_NOTES.md) and
 * belongs to this build of sin.exe, so it is checked before it is believed: in
 * the module, and holding one of the values SiN writes. Anything else and the
 * reading is dropped for good, leaving the guess below.
 */
#define SIN_KEY_DEST_RVA 0x38AAA4
#define SIN_KEY_DEST_MAX 7

static const volatile int *g_key_dest;
static int g_key_dest_checked;
static int g_key_dest_last = -1;

static void find_key_dest(void)
{
	const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
	const IMAGE_NT_HEADERS *nt;

	g_key_dest_checked = 1;
	if (!base)
	{
		return;
	}

	nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE ||
		SIN_KEY_DEST_RVA + sizeof(int) > nt->OptionalHeader.SizeOfImage)
	{
		vr_printf("VR: this sin.exe is not the build the menu detection knows - "
			"the controllers will guess when a menu is up");
		return;
	}

	g_key_dest = (const volatile int *)(base + SIN_KEY_DEST_RVA);
	if (*g_key_dest < 0 || *g_key_dest > SIN_KEY_DEST_MAX)
	{
		g_key_dest = NULL;
		vr_printf("VR: sin.exe's key focus is not where it was expected - "
			"the controllers will guess when a menu is up");
	}
}

float vr_worldscale_units(void)
{
	return vr_worldscale && vr_worldscale->value > 1.0f ? vr_worldscale->value : 39.37f;
}

int vr_in_menu(void)
{
	int where;

	if (!g_key_dest_checked)
	{
		find_key_dest();
	}

	if (g_key_dest)
	{
		where = *g_key_dest;
		if (where < 0 || where > SIN_KEY_DEST_MAX)
		{
			g_key_dest = NULL;
		}
		else
		{
			if (where != g_key_dest_last)
			{
				g_key_dest_last = where;
				vrlog("    input: the keyboard now belongs to %s (key_dest %d)",
					where == 0 ? "the game" : "an interface", where);
			}
			return where != 0;
		}
	}

	/* Without it: a menu over no world at all, which covers the main menu and
	   the loading screens but not a menu opened during play. */
	return g_last_frame_had_menu || !g_last_frame_had_world;
}

static void window_size(int *width, int *height)
{
	RECT client;

	*width = *height = 0;
	if (GetClientRect(WindowFromDC(wglGetCurrentDC()), &client))
	{
		*width = client.right;
		*height = client.bottom;
	}
}

/* ------------------------------------------------------------------------- */
/* Wrapped exports                                                            */
/* ------------------------------------------------------------------------- */

static int vr_Init(void *hinstance, void *wndproc)
{
	int result;

	record_call(RE_INIT);
	register_cvars();

	/* ref_gl asks for a quad-buffer pixel format when cl_stereo is set, and
	   switches cl_stereo off when it cannot have one. Keep it off until Init
	   is done; update_stereo() turns it on once the headset is running. */
	if (vr_enable->value != 0.0f && cl_stereo->value != 0.0f)
	{
		vr_cvar_set("cl_stereo", "0");
	}

	/* Before the engine's own input start-up, which comes after this. */
	if (vr_enable->value != 0.0f)
	{
		vr_input_startup();
	}

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

		if (vr_enable->value != 0.0f)
		{
			g_last_draw_tick = GetTickCount();
			start_idle_timer();
		}
	}

	return result;
}

/*
 * WM_TIMER is delivered from the engine's own message loop, on the engine's
 * thread, with ref_gl's context current - between frames, never inside one.
 * SiN keeps pumping messages while its "press any key" card is up, which is
 * exactly when it stops drawing, so this is where the headset is kept alive.
 */
static int force_render(void);

static VOID CALLBACK idle_timer(HWND window, UINT message, UINT_PTR id, DWORD now)
{
	(void)window;
	(void)message;
	(void)id;
	(void)now;

	if (g_in_renderer || !vr_enable || vr_enable->value == 0.0f ||
		GetTickCount() - g_last_draw_tick < IDLE_AFTER_MS)
	{
		return;
	}

	vr_xr_idle(force_render());
}

static void start_idle_timer(void)
{
	HDC dc = wglGetCurrentDC();

	g_idle_window = dc ? WindowFromDC(dc) : NULL;
	if (g_idle_window && !SetTimer(g_idle_window, IDLE_TIMER_ID, 11, idle_timer))
	{
		g_idle_window = NULL;
	}
}

static void stop_idle_timer(void)
{
	/* Before this DLL can be unloaded: a timer left behind would call into it. */
	if (g_idle_window)
	{
		KillTimer(g_idle_window, IDLE_TIMER_ID);
		g_idle_window = NULL;
	}
}

static void vr_Shutdown(void)
{
	int i;

	record_call(RE_SHUTDOWN);
	stop_idle_timer();

	/* Before ref_gl destroys the GL context the OpenXR session belongs to. */
	g_eye = NULL;
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
	int pass = g_pass++;
	int width, height;

	record_call(RE_BEGIN_FRAME);
	g_in_renderer = 1;
	g_last_draw_tick = GetTickCount();

	if (g_separation_count < 4)
	{
		g_separations[g_separation_count++] = camera_separation;
	}

	/* ref_gl's BeginFrame points glDrawBuffer at the window's back buffer, which
	   only works with the window's framebuffer bound: the previous eye is
	   finished first, and the next one bound after. */
	if (g_eye)
	{
		vr_xr_end_eye();
		g_eye = NULL;
	}

	((re_begin_frame_t)g_real_slot[RE_BEGIN_FRAME])(camera_separation);

	/* Decided on the first pass. cl_stereo was read as the frame started, so
	   a second pass is coming exactly when it is set now. */
	if (pass == 0)
	{
		g_stereo_frame = stereo_wanted() && g_last_frame_had_world && cl_stereo->value != 0.0f;
	}

	if (pass == 0 && vr_enable && vr_enable->value != 0.0f)
	{
		vr_xr_input();
	}

	/* "vr_recenter 1" and "vr_dump_eye 1" in the console, answered and cleared here. */
	if (pass == 0 && vr_recenter && vr_recenter->value != 0.0f)
	{
		vr_xr_request_recenter();
		vr_cvar_set("vr_recenter", "0");
	}
	if (pass == 0 && vr_dump_eye && vr_dump_eye->value != 0.0f)
	{
		vr_xr_request_eye_dump();
		vr_cvar_set("vr_dump_eye", "0");
	}

	if (g_stereo_frame && pass < 2)
	{
		vr_gl_set_hud(vr_hud_fov->value, vr_hud_depth->value);
		window_size(&width, &height);
		if (width > 0 && height > 0)
		{
			g_eye = vr_xr_begin_eye(pass, width, height, force_render());
			g_eye_index = pass;
		}
	}
}

static void vr_RenderFrame(refdef_t *fd)
{
	vr_view_saved_t saved;

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

	if (!g_eye || !fd || (fd->rdflags & RDF_NOWORLDMODEL))
	{
		((re_render_frame_t)g_real_slot[RE_RENDER_FRAME])(fd);
		return;
	}

	vr_view_apply(fd, g_eye, vr_worldscale_units(), &saved);

	if (g_eye_renders_logged < 4)
	{
		vrlog("    eye %d: tangents L%.3f R%.3f D%.3f U%.3f, at (%.3f %.3f %.3f) m -> org (%.1f %.1f %.1f) ang (%.1f %.1f %.1f) fov %.1f/%.1f",
			g_eye_index, g_eye->tan_left, g_eye->tan_right, g_eye->tan_down, g_eye->tan_up,
			g_eye->position[0], g_eye->position[1], g_eye->position[2],
			fd->vieworg[0], fd->vieworg[1], fd->vieworg[2],
			fd->viewangles[0], fd->viewangles[1], fd->viewangles[2], fd->fov_x, fd->fov_y);
		g_eye_renders_logged++;
	}

	((re_render_frame_t)g_real_slot[RE_RENDER_FRAME])(fd);

	/* The engine may render from the same refdef again, paused or not. */
	vr_view_restore(fd, &saved);
}

/*
 * Every 2D pic the engine draws goes through here by name, which is how the
 * renderer can tell a menu from the game: SiN draws its menus from pics under
 * "menu", and nothing else does.
 */
static const char *g_pics[64];
static int g_pic_count;
static int g_frame_had_menu_pic;

static void note_pic(const char *name)
{
	int i;

	if (!name || !name[0])
	{
		return;
	}

	if (_strnicmp(name, "menu", 4) == 0 || _strnicmp(name, "pics/menu", 9) == 0)
	{
		g_frame_had_menu_pic = 1;
	}

	if (!vr_trace || vr_trace->value == 0.0f)
	{
		return;
	}

	/* Each name once: the pointers come from the engine's own string pool. */
	for (i = 0; i < g_pic_count; ++i)
	{
		if (g_pics[i] == name || strcmp(g_pics[i], name) == 0)
		{
			return;
		}
	}

	if (g_pic_count < (int)(sizeof(g_pics) / sizeof(g_pics[0])))
	{
		g_pics[g_pic_count++] = name;
		vrlog("    pic: %s", name);
	}
}

static void vr_DrawPic(int x, int y, const char *name)
{
	record_call(RE_DRAW_PIC);
	note_pic(name);
	((re_draw_pic_t)g_real_slot[RE_DRAW_PIC])(x, y, name);
}

static void vr_DrawStretchPic(int x, int y, int width, int height, const char *name)
{
	record_call(RE_DRAW_STRETCH_PIC);
	note_pic(name);
	((re_draw_stretch_pic_t)g_real_slot[RE_DRAW_STRETCH_PIC])(x, y, width, height, name);
}

/*
 * SiN's full-screen blend - damage, pickups, water - takes a colour and an alpha,
 * and it is drawn with the 2D projection, so it would otherwise be confined to
 * the HUD panel along with the HUD.
 */
static void vr_DrawFadeScreen(float red, float green, float blue, float alpha)
{
	record_call(RE_DRAW_FADE_SCREEN);

	vr_gl_full_screen_2d(1);
	((re_draw_fade_screen_t)g_real_slot[RE_DRAW_FADE_SCREEN])(red, green, blue, alpha);
	vr_gl_full_screen_2d(0);
}

static void vr_EndFrame(void)
{
	record_call(RE_END_FRAME);

	if (vr_enable && vr_enable->value != 0.0f)
	{
		/* A stereo frame ends here; any other frame goes on the theatre screen.
		   Either way the window's back buffer holds its picture before the swap. */
		if (!vr_xr_end_stereo_frame())
		{
			vr_xr_frame(vr_screen_distance->value, vr_screen_width->value, force_render());
		}
	}
	else
	{
		vr_xr_shutdown(); /* nothing to do unless VR was switched off while running */
	}
	g_eye = NULL;

	((re_void_t)g_real_slot[RE_END_FRAME])();

	g_last_frame_had_world = g_frame_has_world;
	g_last_frame_had_menu = g_frame_had_menu_pic;
	g_frame_had_menu_pic = 0;
	g_pass = 0;
	g_stereo_frame = 0;
	update_stereo();
	end_frame_trace();
	g_last_draw_tick = GetTickCount();
	g_in_renderer = 0;
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

	/* Before ref_gl's Init resolves its GL functions. */
	vr_gl_hook_refgl(g_refgl);

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
	g_ours.slot[RE_DRAW_FADE_SCREEN] = (void *)vr_DrawFadeScreen;
	g_ours.slot[RE_DRAW_PIC] = (void *)vr_DrawPic;
	g_ours.slot[RE_DRAW_STRETCH_PIC] = (void *)vr_DrawStretchPic;

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
