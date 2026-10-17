#include <windows.h>
#include <float.h>
#include <math.h>
#include <string.h>

#include "vr_turn.h"
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

/* The head pose everything is measured from, in Quake axes and degrees. */
static float g_origin[3];
static float g_origin_yaw;

/* Game units the whole player is lowered by (the crouch button), and the
   highest the eye may go, above the recentred head (the ceiling). */
static float g_lowered;
static float g_ceiling = FLT_MAX;

/* The body's yaw the last stereo frame turned the head with, and when. */
static float g_body_yaw;
static DWORD g_body_tick;

#define BODY_STALE_MS 250

/* The head steering a camera: since when, and the body's yaw it was held at
   with the client's own yaw then. */
static DWORD g_steer_tick;
static int g_steering;
static float g_steer_body_yaw;
static float g_steer_client_yaw;

#define STEER_STALE_MS 300

static float steered_body_yaw(float camera_yaw);

/* Where the last eye was drawn from, and where the engine had the view. */
static float g_last_eye[3];
static float g_last_engine[3];

/* The direction the quaternion q looks, and the yaw of it, in Quake terms. */
static void quake_forward(const float q[4], float forward[3])
{
	static const float xr_forward[3] = { 0.0f, 0.0f, -1.0f };
	float v[3];

	quat_rotate(q, xr_forward, v);
	xr_to_quake(v, forward);
}

void vr_view_head_steers(void)
{
	g_steer_tick = GetTickCount();
}

static float steered_body_yaw(float camera_yaw)
{
	int steering = g_steer_tick && GetTickCount() - g_steer_tick < STEER_STALE_MS;

	if (steering && !g_steering)
	{
		g_steer_body_yaw = g_body_yaw;
		g_steer_client_yaw = vr_turn_client_yaw();
	}
	g_steering = steering;

	return steering ? g_steer_body_yaw + vr_turn_client_yaw() - g_steer_client_yaw : camera_yaw;
}

void vr_view_recenter(const float position[3], const float orientation[4])
{
	float forward[3];

	xr_to_quake(position, g_origin);
	quake_forward(orientation, forward);
	g_origin_yaw = (float)(atan2(forward[1], forward[0]) * DEG_PER_RAD);
}

void vr_view_local(const float position[3], const float orientation[4], float units_per_metre,
	float origin[3], float angles[3])
{
	static const float xr_right[3] = { 1.0f, 0.0f, 0.0f };
	static const float xr_up[3] = { 0.0f, 1.0f, 0.0f };
	float v[3], forward[3], right[3], up[3], offset[3];
	double turn = -g_origin_yaw * RAD_PER_DEG;
	double s = sin(turn);
	double c = cos(turn);
	double sin_pitch;

	quake_forward(orientation, forward);
	quat_rotate(orientation, xr_right, v);
	xr_to_quake(v, right);
	quat_rotate(orientation, xr_up, v);
	xr_to_quake(v, up);

	xr_to_quake(position, offset);
	offset[0] -= g_origin[0];
	offset[1] -= g_origin[1];
	offset[2] -= g_origin[2];

	origin[0] = (float)((offset[0] * c - offset[1] * s) * units_per_metre);
	origin[1] = (float)((offset[0] * s + offset[1] * c) * units_per_metre);
	origin[2] = offset[2] * units_per_metre - g_lowered;

	sin_pitch = forward[2] > 1.0f ? 1.0 : (forward[2] < -1.0f ? -1.0 : forward[2]);
	angles[PITCH] = (float)(-asin(sin_pitch) * DEG_PER_RAD);
	angles[YAW] = (float)(atan2(forward[1], forward[0]) * DEG_PER_RAD) - g_origin_yaw;
	angles[ROLL] = (float)(atan2(-right[2], up[2]) * DEG_PER_RAD);
}

void vr_view_apply(refdef_t *fd, const vr_eye_t *eye, float units_per_metre, vr_view_saved_t *saved)
{
	static const float xr_forward[3] = { 0.0f, 0.0f, -1.0f };
	static const float xr_right[3] = { 1.0f, 0.0f, 0.0f };
	static const float xr_up[3] = { 0.0f, 1.0f, 0.0f };
	float v[3], forward[3], right[3], up[3], offset[3];
	float body_yaw = steered_body_yaw(fd->viewangles[YAW]);
	/* The recentred head faces the way the body does, so the tracking space
	   turns by the difference between the two. */
	double turn = (body_yaw - g_origin_yaw) * RAD_PER_DEG;
	double s = sin(turn);
	double c = cos(turn);
	double sin_pitch;
	float height;

	if (!g_steering)
	{
		g_body_yaw = body_yaw;
	}
	g_body_tick = GetTickCount();

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
	offset[0] -= g_origin[0];
	offset[1] -= g_origin[1];
	offset[2] -= g_origin[2];

	height = offset[2] * units_per_metre - g_lowered;
	fd->vieworg[0] += (float)((offset[0] * c - offset[1] * s) * units_per_metre);
	fd->vieworg[1] += (float)((offset[0] * s + offset[1] * c) * units_per_metre);
	fd->vieworg[2] += height < g_ceiling ? height : g_ceiling;

	/* Quake angles: pitch positive looking down, yaw positive turning left,
	   roll positive tipping the right side down. */
	sin_pitch = forward[2] > 1.0f ? 1.0 : (forward[2] < -1.0f ? -1.0 : forward[2]);
	fd->viewangles[PITCH] = (float)(-asin(sin_pitch) * DEG_PER_RAD);
	fd->viewangles[YAW] = body_yaw - g_origin_yaw + (float)(atan2(forward[1], forward[0]) * DEG_PER_RAD);
	fd->viewangles[ROLL] = (float)(atan2(-right[2], up[2]) * DEG_PER_RAD);

	memcpy(g_last_eye, fd->vieworg, sizeof(g_last_eye));
	memcpy(g_last_engine, saved->vieworg, sizeof(g_last_engine));

	fd->fov_x = (float)(2.0 * atan(largest(-eye->tan_left, eye->tan_right)) * DEG_PER_RAD);
	fd->fov_y = (float)(2.0 * atan(largest(eye->tan_up, -eye->tan_down)) * DEG_PER_RAD);
}

void vr_view_shift(const float by[3], float units_per_metre)
{
	double yaw = g_origin_yaw * RAD_PER_DEG;
	double s = sin(yaw);
	double c = cos(yaw);

	if (units_per_metre <= 0.0f)
	{
		return;
	}

	g_origin[0] += (float)((by[0] * c - by[1] * s) / units_per_metre);
	g_origin[1] += (float)((by[0] * s + by[1] * c) / units_per_metre);
	g_origin[2] += by[2] / units_per_metre;
}

void vr_view_set_lowered(float units)
{
	g_lowered = units;
}

float vr_view_lowered(void)
{
	return g_lowered;
}

void vr_view_set_ceiling(float units)
{
	g_ceiling = units;
}

void vr_view_last(float engine[3], float eye[3])
{
	memcpy(engine, g_last_engine, sizeof(g_last_engine));
	memcpy(eye, g_last_eye, sizeof(g_last_eye));
}

int vr_view_body_yaw(float *yaw)
{
	if (!g_body_tick || GetTickCount() - g_body_tick > BODY_STALE_MS)
	{
		return 0;
	}
	*yaw = g_body_yaw;
	return 1;
}

void vr_view_restore(refdef_t *fd, const vr_view_saved_t *saved)
{
	memcpy(fd->vieworg, saved->vieworg, sizeof(fd->vieworg));
	memcpy(fd->viewangles, saved->viewangles, sizeof(fd->viewangles));
	fd->fov_x = saved->fov_x;
	fd->fov_y = saved->fov_y;
}
