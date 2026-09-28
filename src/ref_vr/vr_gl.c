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
typedef void(APIENTRY *gl_enum_t)(GLenum mode);
typedef void(APIENTRY *gl_vertex_t)(const GLfloat *v);
typedef void(APIENTRY *gl_texcoord_t)(GLfloat s, GLfloat t);
typedef void(APIENTRY *gl_color4_t)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
static gl_translate_t real_glTranslatef;
static gl_rotate_t real_glRotatef;
static gl_void_t real_glPopMatrix;
static gl_enum_t real_glBegin;
static gl_void_t real_glEnd;
static gl_vertex_t real_glVertex3fv;
static gl_texcoord_t real_glTexCoord2f;
static gl_color4_t real_glColor4f;

#define MAX_MODELS 4

static struct
{
	int count;
	float origin[MAX_MODELS][3];
	GLdouble plane[MAX_MODELS][2][4];
	int has_plane[MAX_MODELS];      /* how many planes */
	int mirror[MAX_MODELS];
	const int (*hide[MAX_MODELS])[2];
	int hide_count[MAX_MODELS];
	int fill[MAX_MODELS];
	float middle[MAX_MODELS];
	const int (*keep[MAX_MODELS])[2];
	int keep_count[MAX_MODELS];
	int on;        /* the clip plane is on */
	int counting;  /* a mirrored model's rotations are being counted */
	int rotates;
	int mirrored;  /* the mirror is in the matrix and the winding swapped */
	int hiding;    /* the model being drawn has parts left out: its entry, plus one */
} models;

/*
 * Leaving parts of a model out. SiN's model drawer (ref_gl+0x18420) sends each
 * strip or fan of triangles as glBegin, then per vertex glTexCoord2f, glColor4f
 * and glVertex3fv, then glEnd - and the vertex it passes is a pointer into its
 * own array of the frame's vertices, 16 bytes each, in the model's numbering:
 * the lea at ref_gl+0x18A58 names the array. So while a model with parts to
 * hide is drawn, each strip is held back until its glEnd, and sent on only if
 * one of its vertices is not among them.
 */
#define LERP_ARRAY_RVA 0x18A58 /* lea eax, [edx + the array] */
#define LERP_STRIDE 16
#define LERP_VERTICES 4096
#define STRIP_MAX 512

static const BYTE *lerp_array;

static struct
{
	GLenum mode;
	int open;       /* between a held-back glBegin and its glEnd */
	int passing;    /* too long to hold: the rest goes straight through */
	int count;
	int hidden;     /* how many of them are among the parts left out */
	int has_st, has_colour;
	GLfloat st[2], colour[4];
	struct
	{
		GLfloat st[2], colour[4], xyz[3];
		int has_st, has_colour;
		int index;      /* the model's vertex number, or -1 */
	} v[STRIP_MAX];
} strip;

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
	models.hide[models.count] = NULL;
	models.hide_count[models.count] = 0;
	models.fill[models.count] = 0;
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

	if (models.has_plane[at] >= 2)
	{
		return;
	}
	for (i = 0; i < 4; ++i)
	{
		models.plane[at][models.has_plane[at]][i] = plane[i];
	}
	models.has_plane[at]++;
}

void vr_gl_mirror_model_at(const float origin[3])
{
	int at = model_at(origin);

	if (at >= 0)
	{
		models.mirror[at] = 1;
	}
}

void vr_gl_hide_model_parts_at(const float origin[3], const int (*ranges)[2], int count)
{
	int at;

	if (!lerp_array || !ranges || count <= 0)
	{
		return;
	}

	at = model_at(origin);
	if (at >= 0)
	{
		models.hide[at] = ranges;
		models.hide_count[at] = count;
	}
}

void vr_gl_fill_model_at(const float origin[3], float middle, const int (*keep)[2], int keep_count)
{
	int at;

	if (!lerp_array)
	{
		return;
	}

	at = model_at(origin);
	if (at >= 0)
	{
		models.fill[at] = 1;
		models.middle[at] = middle;
		models.keep[at] = keep;
		models.keep_count[at] = keep ? keep_count : 0;
	}
}

int vr_gl_can_hide_parts(void)
{
	return lerp_array && real_glBegin && real_glEnd && real_glVertex3fv && real_glTexCoord2f && real_glColor4f;
}

void vr_gl_clear_models(void)
{
	models.count = 0;
}

/* The model's own number for the vertex ref_gl points at, or -1. */
static int vertex_index(const GLfloat *v)
{
	DWORD_PTR offset;

	if ((const BYTE *)v < lerp_array)
	{
		return -1;
	}
	offset = (const BYTE *)v - lerp_array;
	if (offset % LERP_STRIDE != 0 || offset / LERP_STRIDE >= LERP_VERTICES)
	{
		return -1;
	}
	return (int)(offset / LERP_STRIDE);
}

static int in_ranges(int index, const int (*ranges)[2], int count)
{
	int i;

	for (i = 0; ranges && index >= 0 && i < count; ++i)
	{
		if (index >= ranges[i][0] && index <= ranges[i][1])
		{
			return 1;
		}
	}
	return 0;
}

/* The strip held back so far, sent on as ref_gl sent it. */
static void send_strip(void)
{
	int i;

	real_glBegin(strip.mode);
	for (i = 0; i < strip.count; ++i)
	{
		if (strip.v[i].has_st)
		{
			real_glTexCoord2f(strip.v[i].st[0], strip.v[i].st[1]);
		}
		if (strip.v[i].has_colour)
		{
			real_glColor4f(strip.v[i].colour[0], strip.v[i].colour[1], strip.v[i].colour[2],
				strip.v[i].colour[3]);
		}
		real_glVertex3fv(strip.v[i].xyz);
	}
}

/*
 * The strip just sent, its left-facing triangles again, reflected across the
 * model's middle. The strip's triangles are taken the way GL takes them - a
 * fan about its first vertex, a strip turning every other one round - and a
 * reflection turns a triangle inside out, so each is sent in the reverse
 * order to face the other way. Which way is left: SiN's models are wound
 * clockwise, so a triangle facing +y has a normal, by the right-hand rule,
 * pointing -y; measured over the models, the fists come out even and every
 * weapon several times over on that side (docs/ENGINE_NOTES.md).
 */
static void fill_strip(float middle)
{
	int k, sent = 0;

	for (k = 2; k < strip.count; ++k)
	{
		int a, b, c, j;
		const GLfloat *p, *q, *r;
		float n_x, n_y, n_z, centre_y;

		if (strip.mode == GL_TRIANGLE_FAN)
		{
			a = 0, b = k - 1, c = k;
		}
		else if (strip.mode == GL_TRIANGLE_STRIP)
		{
			a = (k & 1) ? k - 1 : k - 2;
			b = (k & 1) ? k - 2 : k - 1;
			c = k;
		}
		else
		{
			return;
		}

		if (in_ranges(strip.v[a].index, models.keep[models.hiding - 1], models.keep_count[models.hiding - 1]) ||
			in_ranges(strip.v[b].index, models.keep[models.hiding - 1], models.keep_count[models.hiding - 1]) ||
			in_ranges(strip.v[c].index, models.keep[models.hiding - 1], models.keep_count[models.hiding - 1]))
		{
			continue;
		}

		p = strip.v[a].xyz;
		q = strip.v[b].xyz;
		r = strip.v[c].xyz;
		n_x = (q[1] - p[1]) * (r[2] - p[2]) - (q[2] - p[2]) * (r[1] - p[1]);
		n_y = (q[2] - p[2]) * (r[0] - p[0]) - (q[0] - p[0]) * (r[2] - p[2]);
		n_z = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
		centre_y = (p[1] + q[1] + r[1]) / 3.0f;

		/* Facing left - within 60 degrees of it: the top and the underside
		   were built across the middle already, and a copy of them only lies
		   on top of the real thing - and on the left of the middle. */
		if (n_y >= 0.0f || n_y * n_y < 0.25f * (n_x * n_x + n_y * n_y + n_z * n_z) || centre_y <= middle)
		{
			continue;
		}

		if (!sent)
		{
			real_glBegin(GL_TRIANGLES);
			sent = 1;
		}
		{
			const int order[3] = { a, c, b };

			for (j = 0; j < 3; ++j)
			{
				const int i = order[j];
				GLfloat xyz[3];

				if (strip.v[i].has_st)
				{
					real_glTexCoord2f(strip.v[i].st[0], strip.v[i].st[1]);
				}
				if (strip.v[i].has_colour)
				{
					real_glColor4f(strip.v[i].colour[0], strip.v[i].colour[1], strip.v[i].colour[2],
						strip.v[i].colour[3]);
				}
				xyz[0] = strip.v[i].xyz[0];
				xyz[1] = 2.0f * middle - strip.v[i].xyz[1];
				xyz[2] = strip.v[i].xyz[2];
				real_glVertex3fv(xyz);
			}
		}
	}

	if (sent)
	{
		real_glEnd();
	}
}

static void APIENTRY hook_glBegin(GLenum mode)
{
	if (!models.hiding)
	{
		real_glBegin(mode);
		return;
	}

	strip.mode = mode;
	strip.open = 1;
	strip.passing = 0;
	strip.count = 0;
	strip.hidden = 0;
	strip.has_st = 0;
	strip.has_colour = 0;
}

static void APIENTRY hook_glTexCoord2f(GLfloat s, GLfloat t)
{
	if (!strip.open || strip.passing)
	{
		real_glTexCoord2f(s, t);
		return;
	}

	strip.st[0] = s;
	strip.st[1] = t;
	strip.has_st = 1;
}

static void APIENTRY hook_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	if (!strip.open || strip.passing)
	{
		real_glColor4f(r, g, b, a);
		return;
	}

	strip.colour[0] = r;
	strip.colour[1] = g;
	strip.colour[2] = b;
	strip.colour[3] = a;
	strip.has_colour = 1;
}

static void APIENTRY hook_glVertex3fv(const GLfloat *v)
{
	int i;

	if (!strip.open || strip.passing)
	{
		real_glVertex3fv(v);
		return;
	}

	if (strip.count >= STRIP_MAX)
	{
		/* Longer than any model's: whatever it is, it is drawn. */
		send_strip();
		strip.passing = 1;
		real_glVertex3fv(v);
		return;
	}

	i = strip.count++;
	memcpy(strip.v[i].st, strip.st, sizeof(strip.st));
	memcpy(strip.v[i].colour, strip.colour, sizeof(strip.colour));
	memcpy(strip.v[i].xyz, v, sizeof(strip.v[i].xyz));
	strip.v[i].has_st = strip.has_st;
	strip.v[i].has_colour = strip.has_colour;
	strip.has_st = 0;
	strip.has_colour = 0;
	strip.v[i].index = vertex_index(v);
	strip.hidden += in_ranges(strip.v[i].index, models.hide[models.hiding - 1], models.hide_count[models.hiding - 1]);
}

static void APIENTRY hook_glEnd(void)
{
	if (!strip.open)
	{
		real_glEnd();
		return;
	}

	strip.open = 0;
	if (strip.passing)
	{
		real_glEnd();
		return;
	}

	/* Every vertex among the parts left out: the whole strip is. The colour
	   and texture coordinate it would have left set go on regardless. */
	if (strip.count > 0 && strip.hidden == strip.count)
	{
		if (strip.count && strip.v[strip.count - 1].has_colour)
		{
			const GLfloat *c = strip.v[strip.count - 1].colour;

			real_glColor4f(c[0], c[1], c[2], c[3]);
		}
		return;
	}

	send_strip();
	real_glEnd();

	if (models.fill[models.hiding - 1])
	{
		fill_strip(models.middle[models.hiding - 1]);
	}
}

static void APIENTRY hook_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	int i;

	if (eye.active && !models.on && !models.counting && !models.mirrored && !models.hiding)
	{
		for (i = 0; i < models.count; ++i)
		{
			if (fabsf(x - models.origin[i][0]) < 0.01f && fabsf(y - models.origin[i][1]) < 0.01f &&
				fabsf(z - models.origin[i][2]) < 0.01f)
			{
				if (models.has_plane[i])
				{
					glClipPlane(GL_CLIP_PLANE0, models.plane[i][0]);
					glEnable(GL_CLIP_PLANE0);
					if (models.has_plane[i] > 1)
					{
						glClipPlane(GL_CLIP_PLANE1, models.plane[i][1]);
						glEnable(GL_CLIP_PLANE1);
					}
					models.on = models.has_plane[i];
				}
				if (models.mirror[i])
				{
					models.counting = 1;
					models.rotates = 0;
				}
				if ((models.hide[i] || models.fill[i]) && vr_gl_can_hide_parts())
				{
					models.hiding = i + 1;
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
		if (models.on > 1)
		{
			glDisable(GL_CLIP_PLANE1);
		}
		models.on = 0;
	}
	if (models.mirrored)
	{
		glFrontFace(GL_CCW);
		models.mirrored = 0;
	}
	models.counting = 0;
	models.hiding = 0;
	if (strip.open)
	{
		/* not expected: a model ends inside a strip */
		strip.open = 0;
		if (!strip.passing)
		{
			send_strip();
		}
		real_glEnd();
	}
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

int vr_gl_world_begin(const float vieworg[3], const float viewangles[3])
{
	const double near_z = 4.0, far_z = 4096.0;

	if (!eye.active || !real_glFrustum || !real_glViewport || !real_glRotatef || !real_glTranslatef)
	{
		return 0;
	}

	glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CLIP_PLANE0);
	glDisable(GL_CLIP_PLANE1);
	glLineWidth(eye.target_width > 1600 ? 3.0f : 2.0f); /* about a tenth of a degree */

	/* The real entry points throughout: the hooked ones are for ref_gl's own
	   calls and would read this as a phase of its frame. */
	real_glViewport(0, 0, eye.target_width, eye.target_height);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	real_glFrustum(near_z * eye.tan_left, near_z * eye.tan_right, near_z * eye.tan_down,
		near_z * eye.tan_up, near_z, far_z);

	/* Quake's own view matrix: z upwards, then the view angles, then the
	   origin (ref_gl's R_SetupGL, and the same order). */
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	real_glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
	real_glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
	real_glRotatef(-viewangles[2], 1.0f, 0.0f, 0.0f);
	real_glRotatef(-viewangles[0], 0.0f, 1.0f, 0.0f);
	real_glRotatef(-viewangles[1], 0.0f, 0.0f, 1.0f);
	real_glTranslatef(-vieworg[0], -vieworg[1], -vieworg[2]);
	return 1;
}

void vr_gl_world_end(void)
{
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glPopAttrib();

	/* ref_gl's own viewport and scissor, under whichever phase it is in. */
	reapply_rects();
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
	if (strcmp(name, "glBegin") == 0)
	{
		real_glBegin = (gl_enum_t)proc;
		return (FARPROC)hook_glBegin;
	}
	if (strcmp(name, "glEnd") == 0)
	{
		real_glEnd = (gl_void_t)proc;
		return (FARPROC)hook_glEnd;
	}
	if (strcmp(name, "glVertex3fv") == 0)
	{
		real_glVertex3fv = (gl_vertex_t)proc;
		return (FARPROC)hook_glVertex3fv;
	}
	if (strcmp(name, "glTexCoord2f") == 0)
	{
		real_glTexCoord2f = (gl_texcoord_t)proc;
		return (FARPROC)hook_glTexCoord2f;
	}
	if (strcmp(name, "glColor4f") == 0)
	{
		real_glColor4f = (gl_color4_t)proc;
		return (FARPROC)hook_glColor4f;
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
	static const BYTE lea_eax_edx[] = { 0x8D, 0x82 };

	if (!vr_hook_import(refgl, "KERNEL32.dll", "GetProcAddress", (void *)hook_GetProcAddress, NULL))
	{
		vr_printf("could not hook ref_gl.dll's GL lookups - stereo will not work");
		return 0;
	}

	lerp_array = (const BYTE *)vr_hook_data_address(refgl, LERP_ARRAY_RVA, lea_eax_edx, sizeof(lea_eax_edx));
	if (!lerp_array)
	{
		vr_printf("this ref_gl.dll is not the build the weapon code knows - the arm modelled on a "
			"weapon will stay on it");
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
