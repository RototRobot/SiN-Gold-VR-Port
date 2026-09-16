#include <math.h>
#include <string.h>

#include "vr_view.h"

#define PITCH 0
#define YAW 1
#define ROLL 2

#define DEG_PER_RAD 57.295779513082321
#define RAD_PER_DEG 0.017453292519943295

/* v rotated by the unit quaternion q (x, y, z, w). */
static void quat_rotate(const float q[4], const float v[3], float out[3])
{
	/* t = 2 cross(q.xyz, v);  out = v + w t + cross(q.xyz, t) */
	float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);

	out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
	out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
	out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

/* OpenXR (+x right, +y up, -z forward) to Quake (+x forward, +y left, +z up). */
static void xr_to_quake(const float v[3], float out[3])
{
	out[0] = -v[2];
	out[1] = -v[0];
	out[2] = v[1];
}

static double largest(double a, double b)
{
	return a > b ? a : b;
}

void vr_view_apply(refdef_t *fd, const vr_eye_t *eye, float units_per_metre, vr_view_saved_t *saved)
{
	static const float xr_forward[3] = { 0.0f, 0.0f, -1.0f };
	static const float xr_right[3] = { 1.0f, 0.0f, 0.0f };
	static const float xr_up[3] = { 0.0f, 1.0f, 0.0f };
	float v[3], forward[3], right[3], up[3], offset[3];
	float body_yaw = fd->viewangles[YAW];
	double s = sin(body_yaw * RAD_PER_DEG);
	double c = cos(body_yaw * RAD_PER_DEG);
	double sin_pitch;

	memcpy(saved->vieworg, fd->vieworg, sizeof(saved->vieworg));
	memcpy(saved->viewangles, fd->viewangles, sizeof(saved->viewangles));
	saved->fov_x = fd->fov_x;
	saved->fov_y = fd->fov_y;

	quat_rotate(eye->orientation, xr_forward, v);
	xr_to_quake(v, forward);
	quat_rotate(eye->orientation, xr_right, v);
	xr_to_quake(v, right);
	quat_rotate(eye->orientation, xr_up, v);
	xr_to_quake(v, up);
	xr_to_quake(eye->position, offset);

	/* The tracking space turns with the body: its forward is the engine's yaw. */
	fd->vieworg[0] += (float)((offset[0] * c - offset[1] * s) * units_per_metre);
	fd->vieworg[1] += (float)((offset[0] * s + offset[1] * c) * units_per_metre);
	fd->vieworg[2] += offset[2] * units_per_metre;

	/* Quake angles: pitch positive looking down, yaw positive turning left,
	   roll positive tipping the right side down. */
	sin_pitch = forward[2] > 1.0f ? 1.0 : (forward[2] < -1.0f ? -1.0 : forward[2]);
	fd->viewangles[PITCH] = (float)(-asin(sin_pitch) * DEG_PER_RAD);
	fd->viewangles[YAW] = body_yaw + (float)(atan2(forward[1], forward[0]) * DEG_PER_RAD);
	fd->viewangles[ROLL] = (float)(atan2(-right[2], up[2]) * DEG_PER_RAD);

	fd->fov_x = (float)(2.0 * atan(largest(-eye->tan_left, eye->tan_right)) * DEG_PER_RAD);
	fd->fov_y = (float)(2.0 * atan(largest(eye->tan_up, -eye->tan_down)) * DEG_PER_RAD);
}

void vr_view_restore(refdef_t *fd, const vr_view_saved_t *saved)
{
	memcpy(fd->vieworg, saved->vieworg, sizeof(fd->vieworg));
	memcpy(fd->viewangles, saved->viewangles, sizeof(fd->viewangles));
	fd->fov_x = saved->fov_x;
	fd->fov_y = saved->fov_y;
}
