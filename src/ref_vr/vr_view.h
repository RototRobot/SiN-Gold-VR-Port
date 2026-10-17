/*
 * From an OpenXR eye to the engine's view.
 */
#ifndef VR_VIEW_H
#define VR_VIEW_H

#include "refapi.h"

/* One eye, in OpenXR terms. */
typedef struct
{
	float position[3];    /* metres in the tracking space: +x right, +y up, -z forward */
	float orientation[4]; /* unit quaternion x, y, z, w */
	float tan_left, tan_right, tan_down, tan_up; /* frustum edges; left and down are negative */
} vr_eye_t;

/* The refdef fields vr_view_apply changes, so they can be put back. */
typedef struct
{
	float vieworg[3];
	float viewangles[3];
	float fov_x, fov_y;
} vr_view_saved_t;

/*
 * Head poses are measured from the pose given here: the player's own head,
 * standing where the game puts their eyes and facing where the body faces.
 * Without it the tracking space's origin - wherever the headset happened to be
 * when the session started, often on a desk - would offset and turn the view.
 * Takes a head pose in the same terms as vr_eye_t.
 */
void vr_view_recenter(const float position[3], const float orientation[4]);

/*
 * Puts the eye on top of the engine's view. The engine keeps the position and
 * turns the body with the mouse (its yaw). The headset's pose, measured from
 * the recentred one, is added on top: its offset, turned with the body, at
 * units_per_metre; its yaw added to the body's; and its pitch and roll in place
 * of the engine's.
 *
 * fov_x and fov_y are widened to enclose the eye's asymmetric frustum, because
 * ref_gl culls against a symmetric one built from them. The projection itself
 * comes from vr_gl.
 */
void vr_view_apply(refdef_t *fd, const vr_eye_t *eye, float units_per_metre, vr_view_saved_t *saved);
void vr_view_restore(refdef_t *fd, const vr_view_saved_t *saved);

/*
 * A pose in the recentred frame, as the game module wants it: origin in game
 * units from the recentred head, +x the way the body faces, and angles in
 * Quake's senses. The engine's own yaw is left out, because the game adds it.
 */
void vr_view_local(const float position[3], const float orientation[4], float units_per_metre,
	float origin[3], float angles[3]);

/*
 * Roomscale (vr_room.c). The recentred head moves by "by" - game units, in the
 * recentred frame - so that everything measured from it afterwards is that
 * much less: the body has stepped after the head, and the head is measured
 * from where the body is now.
 */
void vr_view_shift(const float by[3], float units_per_metre);

/* The whole player - head, hands and weapon - lowered by this many units,
   for the crouch button: vr_view_local and vr_view_apply both take it off. */
void vr_view_set_lowered(float units);
float vr_view_lowered(void);

/* The highest an eye may be, in units above the recentred head: what is above
   the player. FLT_MAX for nothing. Only the view is held down by it. */
void vr_view_set_ceiling(float units);

/* The body's yaw the last stereo frame was turned with. 0 if there has not
   been one lately. */
int vr_view_body_yaw(float *yaw);

/* For the log: the last eye's view origin, and the engine's under it. */
void vr_view_last(float engine[3], float eye[3]);

/*
 * The view is a camera the head steers - a guided missile with
 * vr_missile_steer 2: the camera turns to wherever the head points, and
 * adding the head's turn to the camera's would turn the picture twice. For
 * as long as this keeps being said (each frame), the view's yaw is the
 * body's as it was before, carried by any turning since, plus the head's:
 * where the head points, which is where the camera is going.
 */
void vr_view_head_steers(void);

#endif /* VR_VIEW_H */
