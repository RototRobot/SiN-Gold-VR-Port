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

/*
 * Models drawn other than as ref_gl would: part of them cut away by a plane -
 * the fists, drawn once in each hand, each copy showing only its own arm - or
 * mirrored down their middle, for a weapon held in the hand it was not built
 * for. ref_gl draws a model between a glPushMatrix, a glTranslatef to its
 * origin, and the glPopMatrix after it, with nothing else pushed or popped in
 * between, so a model is known by that translate and left again at the pop.
 *
 * The clip plane goes on at the translate, while the modelview still holds
 * only the view, which is the frame GL takes clip planes in. The mirror has to
 * wait for the model's own frame: R_RotateForEntity (docs/ENGINE_NOTES.md) is
 * that translate and then exactly three rotations - yaw, pitch, roll - so it
 * goes on after the third.
 */
typedef void(APIENTRY *gl_translate_t)(GLfloat x, GLfloat y, GLfloat z);
typedef void(APIENTRY *gl_rotate_t)(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
typedef void(APIENTRY *gl_void_t)(void);
static gl_translate_t real_glTranslatef;
static gl_rotate_t real_glRotatef;
static gl_void_t real_glPopMatrix;

#define MAX_MODELS 4

static struct
{
	int count;
	float origin[MAX_MODELS][3];
	GLdouble plane[MAX_MODELS][4];
	int has_plane[MAX_MODELS];
	int mirror[MAX_MODELS];
	int on;        /* the clip plane is on */
	int counting;  /* a mirrored model's rotations are being counted */
	int rotates;
	int mirrored;  /* the mirror is in the matrix and the winding swapped */
} models;

/* The entry for the model at origin, adding one if it is new. -1 if there is
   no room left this frame. */
static int model_at(const float origin[3])
{
	int i;

	for (i = 0; i < models.count; ++i)
	{
		if (fabsf(origin[0] - models.origin[i][0]) < 0.01f &&
			fabsf(origin[1] - models.origin[i][1]) < 0.01f &&
			fabsf(origin[2] - models.origin[i][2]) < 0.01f)
		{
			return i;
		}
	}

	if (models.count >= MAX_MODELS)
	{
		return -1;
	}

	memcpy(models.origin[models.count], origin, sizeof(models.origin[0]));
	models.has_plane[models.count] = 0;
	models.mirror[models.count] = 0;
	return models.count++;
}

void vr_gl_clip_model_at(const float origin[3], const float plane[4])
{
	int at = model_at(origin);
	int i;

	if (at < 0)
	{
		return;
	}

	for (i = 0; i < 4; ++i)
	{
		models.plane[at][i] = plane[i];
	}
	models.has_plane[at] = 1;
}

void vr_gl_mirror_model_at(const float origin[3])
{
	int at = model_at(origin);

	if (at >= 0)
	{
		models.mirror[at] = 1;
	}
}

void vr_gl_clear_models(void)
{
	models.count = 0;
}

static void APIENTRY hook_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	int i;

	if (eye.active && !models.on && !models.counting && !models.mirrored)
	{
		for (i = 0; i < models.count; ++i)
		{
			if (fabsf(x - models.origin[i][0]) < 0.01f && fabsf(y - models.origin[i][1]) < 0.01f &&
				fabsf(z - models.origin[i][2]) < 0.01f)
			{
				if (models.has_plane[i])
				{
					glClipPlane(GL_CLIP_PLANE0, models.plane[i]);
					glEnable(GL_CLIP_PLANE0);
					models.on = 1;
				}
				if (models.mirror[i])
				{
					models.counting = 1;
					models.rotates = 0;
				}
				break;
			}
		}
	}

	real_glTranslatef(x, y, z);
}

static void APIENTRY hook_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	real_glRotatef(angle, x, y, z);

	/* The third rotation: the matrix now holds the model's own frame. */
	if (models.counting && ++models.rotates >= 3)
	{
		models.counting = 0;
		models.mirrored = 1;
		glScalef(1.0f, -1.0f, 1.0f);
		glFrontFace(GL_CW);
	}
}

/* The mirror itself goes with the matrix; the winding has to be put back. */
static void end_model(void)
{
	if (models.on)
	{
		glDisable(GL_CLIP_PLANE0);
		models.on = 0;
	}
	if (models.mirrored)
	{
		glFrontFace(GL_CCW);
		models.mirrored = 0;
	}
	models.counting = 0;
}

static void APIENTRY hook_glPopMatrix(void)
{
	real_glPopMatrix();
	end_model();
}

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

int vr_gl_panel_offset(float tan_x, float tan_y, float *dx, float *dy)
{
	double half_width = tan(hud.fov * 0.5 * RAD_PER_DEG);
	double half_height = half_width * eye.window_height / eye.window_width;
	double centre_x = hud.depth > 0.0f ? -eye.offset_x / hud.depth : 0.0;

	if (!eye.active || eye.window_width <= 0 || eye.window_height <= 0 || half_width <= 0.0)
	{
		return 0;
	}

	/* The panel holds the whole window, tangent for tangent, and its own middle
	   is centre_x - the shift that puts it at hud.depth. Window y grows down. */
	*dx = (float)((tan_x - centre_x) / (2.0 * half_width) * eye.window_width);
	*dy = (float)(-tan_y / (2.0 * half_height) * eye.window_height);
	return 1;
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
	if (strcmp(name, "glTranslatef") == 0)
	{
		real_glTranslatef = (gl_translate_t)proc;
		return (FARPROC)hook_glTranslatef;
	}
	if (strcmp(name, "glRotatef") == 0)
	{
		real_glRotatef = (gl_rotate_t)proc;
		return (FARPROC)hook_glRotatef;
	}
	if (strcmp(name, "glPopMatrix") == 0)
	{
		real_glPopMatrix = (gl_void_t)proc;
		return (FARPROC)hook_glPopMatrix;
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
	end_model();
}
