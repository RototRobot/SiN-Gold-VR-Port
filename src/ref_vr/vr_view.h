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

#endif /* VR_VIEW_H */
