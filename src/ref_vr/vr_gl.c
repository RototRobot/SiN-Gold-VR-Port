#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <string.h>

#include "ref_vr.h"
#include "vr_gl.h"
#include "vr_hook.h"

vr_gl_t vrgl;

typedef void(APIENTRY *gl_frustum_t)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
	GLdouble znear, GLdouble zfar);
typedef void(APIENTRY *gl_rect_t)(GLint x, GLint y, GLsizei width, GLsizei height);

static gl_frustum_t real_glFrustum;
static gl_frustum_t real_glOrtho;
static gl_rect_t real_glViewport;
static gl_rect_t real_glScissor;

#define RAD_PER_DEG 0.017453292519943295

static struct
{
	int active;
	int window_width, window_height;
	int target_width, target_height;
	double scale_x, scale_y;
	double tan_left, tan_right, tan_down, tan_up;
	double offset_x;            /* this eye, right of the head, in metres */

	/* The HUD panel, in target pixels: where the 2D goes. */
	double panel_x, panel_y, panel_width, panel_height;

	int in_2d;                  /* between glOrtho and the next glFrustum */
	int full_screen_2d;         /* a 2D call that should cover the view */

	/* What ref_gl last asked for, in window coordinates, so that a change of
	   phase can apply it again under the other mapping. */
	GLint viewport[4], scissor[4];
	int has_viewport, has_scissor;
} eye;

static struct
{
	float fov, depth;
} hud = { 60.0f, 6.0f };

void vr_gl_set_hud(float fov, float depth)
{
	hud.fov = fov > 20.0f ? (fov < 170.0f ? fov : 170.0f) : 20.0f;
	hud.depth = depth > 0.0f ? depth : 0.0f;
}

/* Where a tangent from the view axis falls in the eye target. */
static double pixel_x(double tangent)
{
	return (tangent - eye.tan_left) / (eye.tan_right - eye.tan_left) * eye.target_width;
}

static double pixel_y(double tangent)
{
	return (tangent - eye.tan_down) / (eye.tan_up - eye.tan_down) * eye.target_height;
}

/*
 * The panel is hud.fov degrees across, in the shape of the window so that the 2D
 * keeps its proportions, and centred on this eye's axis. Putting it at a finite
 * depth means shifting it away from the eye by the angle the eyes differ by at
 * that distance, which is what makes both eyes agree on where the crosshair is.
 */
static void compute_panel(void)
{
	double half_width = tan(hud.fov * 0.5 * RAD_PER_DEG);
	double half_height = half_width * eye.window_height / eye.window_width;
	double centre_x = hud.depth > 0.0f ? -eye.offset_x / hud.depth : 0.0;
	double x0 = pixel_x(centre_x - half_width);
	double x1 = pixel_x(centre_x + half_width);
	double y0 = pixel_y(-half_height);
	double y1 = pixel_y(half_height);

	eye.panel_x = x0;
	eye.panel_y = y0;
	eye.panel_width = x1 - x0;
	eye.panel_height = y1 - y0;
}

int vr_gl_load(void)
{
	vrgl.GenFramebuffers = (void *)wglGetProcAddress("glGenFramebuffers");
	vrgl.DeleteFramebuffers = (void *)wglGetProcAddress("glDeleteFramebuffers");
	vrgl.BindFramebuffer = (void *)wglGetProcAddress("glBindFramebuffer");
	vrgl.FramebufferTexture2D = (void *)wglGetProcAddress("glFramebufferTexture2D");
	vrgl.FramebufferRenderbuffer = (void *)wglGetProcAddress("glFramebufferRenderbuffer");
	vrgl.CheckFramebufferStatus = (void *)wglGetProcAddress("glCheckFramebufferStatus");
	vrgl.BlitFramebuffer = (void *)wglGetProcAddress("glBlitFramebuffer");
	vrgl.GenRenderbuffers = (void *)wglGetProcAddress("glGenRenderbuffers");
	vrgl.DeleteRenderbuffers = (void *)wglGetProcAddress("glDeleteRenderbuffers");
	vrgl.BindRenderbuffer = (void *)wglGetProcAddress("glBindRenderbuffer");
	vrgl.RenderbufferStorage = (void *)wglGetProcAddress("glRenderbufferStorage");

	return vrgl.GenFramebuffers && vrgl.DeleteFramebuffers && vrgl.BindFramebuffer &&
		vrgl.FramebufferTexture2D && vrgl.FramebufferRenderbuffer && vrgl.CheckFramebufferStatus &&
		vrgl.BlitFramebuffer && vrgl.GenRenderbuffers && vrgl.DeleteRenderbuffers &&
		vrgl.BindRenderbuffer && vrgl.RenderbufferStorage;
}

/* ------------------------------------------------------------------------- */
/* ref_gl's GL calls                                                          */
/* ------------------------------------------------------------------------- */

/*
 * A rect in window coordinates, in the eye target: over the whole of it for the
 * world and for 2D that covers the view, inside the HUD panel otherwise.
 */
static void map_rect(GLint *x, GLint *y, GLsizei *width, GLsizei *height)
{
	double origin_x = 0.0, origin_y = 0.0, scale_x = eye.scale_x, scale_y = eye.scale_y;
	GLint x0, y0, x1, y1;

	if (eye.in_2d && !eye.full_screen_2d)
	{
		origin_x = eye.panel_x;
		origin_y = eye.panel_y;
		scale_x = eye.panel_width / eye.window_width;
		scale_y = eye.panel_height / eye.window_height;
	}

	x0 = (GLint)floor(origin_x + *x * scale_x + 0.5);
	y0 = (GLint)floor(origin_y + *y * scale_y + 0.5);
	x1 = (GLint)floor(origin_x + (*x + *width) * scale_x + 0.5);
	y1 = (GLint)floor(origin_y + (*y + *height) * scale_y + 0.5);

	*x = x0;
	*y = y0;
	*width = x1 - x0;
	*height = y1 - y0;
}

static void APIENTRY hook_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (eye.active)
	{
		eye.viewport[0] = x;
		eye.viewport[1] = y;
		eye.viewport[2] = width;
		eye.viewport[3] = height;
		eye.has_viewport = 1;
		map_rect(&x, &y, &width, &height);
	}

	real_glViewport(x, y, width, height);
}

static void APIENTRY hook_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (eye.active)
	{
		eye.scissor[0] = x;
		eye.scissor[1] = y;
		eye.scissor[2] = width;
		eye.scissor[3] = height;
		eye.has_scissor = 1;
		map_rect(&x, &y, &width, &height);
	}

	real_glScissor(x, y, width, height);
}

/* ref_gl sets the viewport before the projection that says what it is for, so
   the rect it asked for is applied again whenever the phase changes. */
static void reapply_rects(void)
{
	if (eye.has_viewport)
	{
		hook_glViewport(eye.viewport[0], eye.viewport[1], eye.viewport[2], eye.viewport[3]);
	}
	if (eye.has_scissor)
	{
		hook_glScissor(eye.scissor[0], eye.scissor[1], eye.scissor[2], eye.scissor[3]);
	}
}

void vr_gl_full_screen_2d(int on)
{
	if (eye.full_screen_2d == on)
	{
		return;
	}

	eye.full_screen_2d = on;

	/* The rect ref_gl asked for means something different now. */
	if (eye.active && eye.in_2d)
	{
		reapply_rects();
	}
}

/* ref_gl's 2D projection. It is left alone; what changes is where it lands. */
static void APIENTRY hook_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
	GLdouble znear, GLdouble zfar)
{
	if (eye.active && !eye.in_2d)
	{
		eye.in_2d = 1;
		reapply_rects();
	}

	real_glOrtho(left, right, bottom, top, znear, zfar);
}

/* ref_gl sets a projection in three places, the view weapon included; all of
   them get the eye's frustum, at the depth range ref_gl asked for. */
static void APIENTRY hook_glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
	GLdouble znear, GLdouble zfar)
{
	if (eye.active)
	{
		if (eye.in_2d)
		{
			eye.in_2d = 0;
			reapply_rects();
		}

		left = znear * eye.tan_left;
		right = znear * eye.tan_right;
		bottom = znear * eye.tan_down;
		top = znear * eye.tan_up;
	}

	real_glFrustum(left, right, bottom, top, znear, zfar);
}

static FARPROC WINAPI hook_GetProcAddress(HMODULE module, LPCSTR name)
{
	FARPROC proc = GetProcAddress(module, name);

	/* Lookups by ordinal pass straight through. */
	if (!proc || !HIWORD((DWORD_PTR)name))
	{
		return proc;
	}

	if (strcmp(name, "glFrustum") == 0)
	{
		real_glFrustum = (gl_frustum_t)proc;
		return (FARPROC)hook_glFrustum;
	}
	if (strcmp(name, "glOrtho") == 0)
	{
		real_glOrtho = (gl_frustum_t)proc;
		return (FARPROC)hook_glOrtho;
	}
	if (strcmp(name, "glViewport") == 0)
	{
		real_glViewport = (gl_rect_t)proc;
		return (FARPROC)hook_glViewport;
	}
	if (strcmp(name, "glScissor") == 0)
	{
		real_glScissor = (gl_rect_t)proc;
		return (FARPROC)hook_glScissor;
	}

	return proc;
}

int vr_gl_hook_refgl(HMODULE refgl)
{
	if (!vr_hook_import(refgl, "KERNEL32.dll", "GetProcAddress", (void *)hook_GetProcAddress, NULL))
	{
		vr_printf("could not hook ref_gl.dll's GL lookups - stereo will not work");
		return 0;
	}

	return 1;
}

void vr_gl_eye_begin(int target_width, int target_height, int window_width, int window_height,
	float tan_left, float tan_right, float tan_down, float tan_up, float offset_x)
{
	eye.window_width = window_width;
	eye.window_height = window_height;
	eye.target_width = target_width;
	eye.target_height = target_height;
	eye.scale_x = (double)target_width / window_width;
	eye.scale_y = (double)target_height / window_height;
	eye.tan_left = tan_left;
	eye.tan_right = tan_right;
	eye.tan_down = tan_down;
	eye.tan_up = tan_up;
	eye.offset_x = offset_x;
	compute_panel();

	/* Each eye starts in the world, and knows nothing of the last one's rects. */
	eye.in_2d = 0;
	eye.full_screen_2d = 0;
	eye.has_viewport = 0;
	eye.has_scissor = 0;
	eye.active = real_glFrustum && real_glOrtho && real_glViewport && real_glScissor;
}

void vr_gl_eye_end(void)
{
	eye.active = 0;
	eye.in_2d = 0;
	eye.full_screen_2d = 0;
}
