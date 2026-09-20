#include <math.h>
#include <string.h>

#include "vr_math.h"

#define PITCH 0
#define YAW 1
#define ROLL 2

#define DEG_PER_RAD 57.295779513082321
#define RAD_PER_DEG 0.017453292519943295

void vr_angles_to_axis(const float angles[3], float axis[3][3])
{
	double sp = sin(angles[PITCH] * RAD_PER_DEG), cp = cos(angles[PITCH] * RAD_PER_DEG);
	double sy = sin(angles[YAW] * RAD_PER_DEG), cy = cos(angles[YAW] * RAD_PER_DEG);
	double sr = sin(angles[ROLL] * RAD_PER_DEG), cr = cos(angles[ROLL] * RAD_PER_DEG);

	/* forward */
	axis[0][0] = (float)(cp * cy);
	axis[0][1] = (float)(cp * sy);
	axis[0][2] = (float)(-sp);

	/* left, which is Quake's right turned round */
	axis[1][0] = (float)(sr * sp * cy - cr * sy);
	axis[1][1] = (float)(sr * sp * sy + cr * cy);
	axis[1][2] = (float)(sr * cp);

	/* up */
	axis[2][0] = (float)(cr * sp * cy + sr * sy);
	axis[2][1] = (float)(cr * sp * sy - sr * cy);
	axis[2][2] = (float)(cr * cp);
}

void vr_axis_to_angles(const float axis[3][3], float angles[3])
{
	double sin_pitch = axis[0][2];
	double horizontal;

	if (sin_pitch > 1.0)
	{
		sin_pitch = 1.0;
	}
	if (sin_pitch < -1.0)
	{
		sin_pitch = -1.0;
	}

	angles[PITCH] = (float)(-asin(sin_pitch) * DEG_PER_RAD);
	horizontal = sqrt((double)axis[0][0] * axis[0][0] + (double)axis[0][1] * axis[0][1]);

	if (horizontal > 1e-4)
	{
		angles[YAW] = (float)(atan2(axis[0][1], axis[0][0]) * DEG_PER_RAD);
		angles[ROLL] = (float)(atan2(axis[1][2], axis[2][2]) * DEG_PER_RAD);
	}
	else
	{
		/* Looking straight up or down: roll and yaw are the same turn, so it
		   all goes into the yaw, which the up row still gives. */
		angles[YAW] = (float)(atan2(-axis[2][1] * sin_pitch, -axis[2][0] * sin_pitch) * DEG_PER_RAD);
		angles[ROLL] = 0.0f;
	}
}

void vr_pose_set(vr_pose_t *pose, const float origin[3], const float angles[3])
{
	memcpy(pose->origin, origin, sizeof(pose->origin));
	vr_angles_to_axis(angles, pose->axis);
}

void vr_pose_angles(const vr_pose_t *pose, float angles[3])
{
	vr_axis_to_angles(pose->axis, angles);
}

void vr_pose_point(const vr_pose_t *pose, const float local[3], float out[3])
{
	int i;

	for (i = 0; i < 3; ++i)
	{
		out[i] = pose->origin[i] + local[0] * pose->axis[0][i] + local[1] * pose->axis[1][i] +
			local[2] * pose->axis[2][i];
	}
}

void vr_pose_compose(const vr_pose_t *parent, const vr_pose_t *child, vr_pose_t *out)
{
	vr_pose_t result;
	int row, i;

	vr_pose_point(parent, child->origin, result.origin);
	for (row = 0; row < 3; ++row)
	{
		for (i = 0; i < 3; ++i)
		{
			result.axis[row][i] = child->axis[row][0] * parent->axis[0][i] +
				child->axis[row][1] * parent->axis[1][i] + child->axis[row][2] * parent->axis[2][i];
		}
	}

	*out = result;
}

void vr_pose_relative(const vr_pose_t *frame, const vr_pose_t *pose, vr_pose_t *out)
{
	vr_pose_t result;
	float d[3];
	int row, i;

	for (i = 0; i < 3; ++i)
	{
		d[i] = pose->origin[i] - frame->origin[i];
	}

	for (i = 0; i < 3; ++i)
	{
		result.origin[i] = d[0] * frame->axis[i][0] + d[1] * frame->axis[i][1] + d[2] * frame->axis[i][2];
	}

	for (row = 0; row < 3; ++row)
	{
		for (i = 0; i < 3; ++i)
		{
			result.axis[row][i] = pose->axis[row][0] * frame->axis[i][0] +
				pose->axis[row][1] * frame->axis[i][1] + pose->axis[row][2] * frame->axis[i][2];
		}
	}

	*out = result;
}

void vr_pose_mirror_y(vr_pose_t *pose)
{
	/* The reflection m = (1, -1, 1) on both sides of the orientation, which
	   negates every entry with exactly one index of y: a rotation again, being
	   two reflections. */
	pose->origin[1] = -pose->origin[1];
	pose->axis[0][1] = -pose->axis[0][1];
	pose->axis[1][0] = -pose->axis[1][0];
	pose->axis[1][2] = -pose->axis[1][2];
	pose->axis[2][1] = -pose->axis[2][1];
}

void vr_pose_to_world(const vr_pose_t *local, const float origin[3], float yaw, vr_pose_t *out)
{
	static const float no_turn[3] = { 0.0f, 0.0f, 0.0f };
	vr_pose_t body;
	float angles[3];

	angles[PITCH] = 0.0f;
	angles[YAW] = yaw;
	angles[ROLL] = 0.0f;
	vr_pose_set(&body, origin ? origin : no_turn, angles);
	vr_pose_compose(&body, local, out);
}
