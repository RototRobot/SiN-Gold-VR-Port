#include <math.h>
#include <stdio.h>
#include <string.h>

#include "refapi.h"
#include "ref_vr.h"
#include "vr_gl.h"
#include "vr_poses.h"
#include "vr_weapon.h"
#include "vrlog.h"

#define PITCH 0
#define YAW 1
#define ROLL 2

#define RAD_PER_DEG 0.017453292519943295

static cvar_t *vr_weapon_hand;
static cvar_t *vr_weapon_scale;
static cvar_t *vr_weapon_offset;
static int said_moved;
static int in_hand;
static const float hand_nowhere[3] = { 0.0f, 0.0f, 0.0f };

/*
 * The defaults are a first guess, to be settled in the headset. SiN's view
 * models are built to look right from the camera, bigger than life: in the
 * stock view the assault rifle's grip sits about 35 degrees right of and 41
 * below the view axis (measured in an eye dump), some 18 units out. So the
 * model comes back and across towards the hand, and down in size. The size is
 * applied around the model's origin, so the grip only stays in the hand at
 * every size once the offset is right.
 */
void vr_weapon_startup(void)
{
	vr_weapon_hand = vr_cvar("vr_weapon_hand", "1", CVAR_ARCHIVE);
	vr_weapon_scale = vr_cvar("vr_weapon_scale", "0.75", CVAR_ARCHIVE);
	vr_weapon_offset = vr_cvar("vr_weapon_offset", "-18 -10 14", CVAR_ARCHIVE);
}

int vr_weapon_in_hand(void)
{
	return in_hand;
}

/* Quake's own: forward, right and up for pitch-down-positive, yaw-left-positive angles. */
static void angle_vectors(const float angles[3], float forward[3], float right[3], float up[3])
{
	double sp = sin(angles[PITCH] * RAD_PER_DEG), cp = cos(angles[PITCH] * RAD_PER_DEG);
	double sy = sin(angles[YAW] * RAD_PER_DEG), cy = cos(angles[YAW] * RAD_PER_DEG);
	double sr = sin(angles[ROLL] * RAD_PER_DEG), cr = cos(angles[ROLL] * RAD_PER_DEG);

	forward[0] = (float)(cp * cy);
	forward[1] = (float)(cp * sy);
	forward[2] = (float)(-sp);

	right[0] = (float)(-sr * sp * cy + cr * sy);
	right[1] = (float)(-sr * sp * sy - cr * cy);
	right[2] = (float)(-sr * cp);

	up[0] = (float)(cr * sp * cy + sr * sy);
	up[1] = (float)(cr * sp * sy - sr * cy);
	up[2] = (float)(cr * cp);
}

void vr_weapon_to_hand(refdef_t *fd)
{
	const sinvr_poses_t *poses = vr_poses_current();
	const float *local;
	float hand[3], angles[3], forward[3], right[3], up[3], origin[3];
	float offset[3] = { 0.0f, 0.0f, 0.0f };
	float scale;
	double yaw, s, c;
	int i, moved = 0;

	if (!fd || !fd->entities || fd->num_entities <= 0 || !vr_weapon_hand || vr_weapon_hand->value == 0.0f ||
		!poses || !poses->hand_valid[SINVR_HAND_RIGHT])
	{
		in_hand = 0;
		vr_gl_resize_model_at(hand_nowhere, 1.0f);
		return;
	}

	/* The hand is measured from the player's eyes, facing the way the body
	   faces; the engine's own view is exactly that. */
	local = poses->hand_origin[SINVR_HAND_RIGHT];
	yaw = fd->viewangles[YAW] * RAD_PER_DEG;
	s = sin(yaw);
	c = cos(yaw);
	hand[0] = fd->vieworg[0] + (float)(local[0] * c - local[1] * s);
	hand[1] = fd->vieworg[1] + (float)(local[0] * s + local[1] * c);
	hand[2] = fd->vieworg[2] + local[2];

	angles[PITCH] = poses->hand_angles[SINVR_HAND_RIGHT][PITCH];
	angles[YAW] = poses->hand_angles[SINVR_HAND_RIGHT][YAW] + fd->viewangles[YAW];
	angles[ROLL] = poses->hand_angles[SINVR_HAND_RIGHT][ROLL];
	angle_vectors(angles, forward, right, up);

	scale = vr_weapon_scale ? vr_weapon_scale->value : 1.0f;
	if (scale < 0.05f || scale > 4.0f)
	{
		scale = 1.0f;
	}

	/* The view models are built around the camera, not the grip, so the model's
	   origin sits some way from where the hand holds it: vr_weapon_offset moves
	   it, in the hand's own frame, and grows and shrinks with the model. */
	if (vr_weapon_offset && vr_weapon_offset->string)
	{
		sscanf(vr_weapon_offset->string, "%f %f %f", &offset[0], &offset[1], &offset[2]);
	}
	for (i = 0; i < 3; ++i)
	{
		origin[i] = hand[i] + scale * (forward[i] * offset[0] + right[i] * offset[1] + up[i] * offset[2]);
	}

	for (i = 0; i < fd->num_entities; ++i)
	{
		entity_t *e = &fd->entities[i];

		if (!(e->flags & RF_WEAPONMODEL))
		{
			continue;
		}

		memcpy(e->origin, origin, sizeof(e->origin));
		memcpy(e->oldorigin, origin, sizeof(e->oldorigin));
		memcpy(e->angles, angles, sizeof(e->angles));

		/* Kept in step with the angles, rows forward, left, up. ref_gl turns
		   the model with the angles; the matrix is SiN's own bookkeeping. */
		memcpy(e->axis[0], forward, sizeof(e->axis[0]));
		e->axis[1][0] = -right[0];
		e->axis[1][1] = -right[1];
		e->axis[1][2] = -right[2];
		memcpy(e->axis[2], up, sizeof(e->axis[2]));
		moved++;
	}

	/* The size is applied where ref_gl places the model (vr_gl.c): nothing in
	   the entity itself scales it. */
	vr_gl_resize_model_at(origin, moved ? scale : 1.0f);
	in_hand = moved > 0;

	if (moved && !said_moved)
	{
		said_moved = 1;
		vr_printf("VR: the view weapon is in the right hand, at (%.1f %.1f %.1f) pointing (%.1f %.1f %.1f)",
			origin[0], origin[1], origin[2], angles[PITCH], angles[YAW], angles[ROLL]);
	}
}
