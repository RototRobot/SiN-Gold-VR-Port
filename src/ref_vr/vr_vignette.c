#include <windows.h>
#include <GL/gl.h>
#include <math.h>

#include "ref_vr.h"
#include "vr_vignette.h"

/*
 * The comfort vignette: while the stick moves or smoothly turns the player,
 * the edge of the view fades to black, so that the motion the eyes see and the
 * body does not feel is kept to the middle of the view. Walking in the room
 * and snap turns move nobody the body does not know about, so they leave it
 * alone.
 *
 * It is a ring drawn over each eye after the world (and the hands), under the
 * HUD: clear inside, black outside, centred on the eye's own straight ahead -
 * the eyes' frustums are not symmetrical, so the middle of the picture is not
 * the middle of the view. How far in it comes is vr_vignette, 0 for never.
 */

#define DEG_TO_RAD 0.017453292519943295
#define SEGMENTS 48
#define FADE_IN_PER_SECOND 6.0f   /* about a sixth of a second to close */
#define FADE_OUT_PER_SECOND 2.5f  /* and nearer half a second to open again */
#define OPEN_ANGLE 75.0f          /* where the clear part ends, barely closed */
#define CLOSED_ANGLE 25.0f        /* and fully closed */
#define FEATHER 22.0f             /* degrees from clear to black */

static cvar_t *vr_vignette;
static float wanted;     /* how much the stick moves the player, 0-1 */
static float shown;      /* how far closed the vignette is now, 0-1 */
static DWORD last_tick;
static unsigned drawn;

void vr_vignette_startup(void)
{
	vr_vignette = vr_cvar("vr_vignette", "0.6", CVAR_ARCHIVE);
}

void vr_vignette_motion(float amount)
{
	wanted = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
}

/* Once a frame: closes towards how much the player is moved, and opens again. */
static void fade(void)
{
	DWORD now = GetTickCount();
	float seconds = last_tick ? (now - last_tick) * 0.001f : 0.0f;

	if (seconds > 0.1f)
	{
		seconds = 0.1f;
	}
	last_tick = now;

	if (wanted > shown)
	{
		shown += FADE_IN_PER_SECOND * seconds;
		shown = shown > wanted ? wanted : shown;
	}
	else
	{
		shown -= FADE_OUT_PER_SECOND * seconds;
		shown = shown < wanted ? wanted : shown;
	}
}

/* A direction tan_x, tan_y from the eye's straight ahead, in the eye's clip
   coordinates. */
static void vertex_at(const vr_eye_t *eye, double tan_x, double tan_y)
{
	double x = (2.0 * tan_x - (eye->tan_right + eye->tan_left)) / (eye->tan_right - eye->tan_left);
	double y = (2.0 * tan_y - (eye->tan_up + eye->tan_down)) / (eye->tan_up - eye->tan_down);

	glVertex2d(x, y);
}

/* The band between two angles from straight ahead, from alpha a to alpha b. */
static void band(const vr_eye_t *eye, float inner, float outer, float a, float b)
{
	double t_in = tan(inner * DEG_TO_RAD), t_out = tan(outer * DEG_TO_RAD);
	int i;

	glBegin(GL_TRIANGLE_STRIP);
	for (i = 0; i <= SEGMENTS; ++i)
	{
		double theta = 2.0 * 3.14159265358979 * i / SEGMENTS, c = cos(theta), s = sin(theta);

		glColor4f(0.0f, 0.0f, 0.0f, a);
		vertex_at(eye, t_in * c, t_in * s);
		glColor4f(0.0f, 0.0f, 0.0f, b);
		vertex_at(eye, t_out * c, t_out * s);
	}
	glEnd();
}

void vr_vignette_draw(const vr_eye_t *eye, int first_eye)
{
	float amount = vr_vignette ? vr_vignette->value : 0.0f, inner, outer;

	if (first_eye)
	{
		fade();
	}
	if (!eye || amount <= 0.0f || shown <= 0.01f)
	{
		return;
	}

	amount = amount > 1.0f ? 1.0f : amount;
	inner = OPEN_ANGLE - (OPEN_ANGLE - CLOSED_ANGLE) * amount * shown;
	outer = inner + FEATHER;

	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_FOG);
	glDisable(GL_LIGHTING);
	glDisable(GL_CLIP_PLANE0);
	glDisable(GL_CLIP_PLANE1);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glShadeModel(GL_SMOOTH);

	band(eye, inner, outer > 88.0f ? 88.0f : outer, 0.0f, 1.0f);
	if (outer < 88.0f)
	{
		band(eye, outer, 88.0f, 1.0f, 1.0f);
	}

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glPopAttrib();

	drawn++;
}

unsigned vr_vignette_frames(void)
{
	return drawn;
}
