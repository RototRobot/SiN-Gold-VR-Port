/*
 * Poses in Quake's terms: +x forward, +y left, +z up, and angles of pitch
 * (positive looking down), yaw (positive turning left) and roll (positive
 * tipping the right side down), in degrees.
 *
 * An orientation is kept as an axis: three rows, the directions of the local
 * forward, left and up - the same layout as SiN's entity_t.axis.
 */
#ifndef VR_MATH_H
#define VR_MATH_H

typedef struct
{
	float origin[3];
	float axis[3][3];
} vr_pose_t;

/* Quake's AngleVectors, as rows forward, left and up. */
void vr_angles_to_axis(const float angles[3], float axis[3][3]);

/* The other way round. Straight up or down, the yaw comes from the up row. */
void vr_axis_to_angles(const float axis[3][3], float angles[3]);

/* A pose from an origin and angles, and back. */
void vr_pose_set(vr_pose_t *pose, const float origin[3], const float angles[3]);
void vr_pose_angles(const vr_pose_t *pose, float angles[3]);

/* local, given in pose's own frame, to the frame pose is given in. */
void vr_pose_point(const vr_pose_t *pose, const float local[3], float out[3]);

/* child, given in parent's frame, into the frame parent is given in. */
void vr_pose_compose(const vr_pose_t *parent, const vr_pose_t *child, vr_pose_t *out);

/* pose, given in the same frame as frame, into frame's own. */
void vr_pose_relative(const vr_pose_t *frame, const vr_pose_t *pose, vr_pose_t *out);

/*
 * The pose reflected in the y = 0 plane of the frame it is given in - the
 * plane of that frame's forward and up. A thing held in one hand, reflected
 * this way in the hand's own frame, is the same thing held in the other hand:
 * the position goes to the other side, the yaw and roll turn the other way,
 * and the thing itself is mirrored with it.
 */
void vr_pose_mirror_y(vr_pose_t *pose);

/* Turned by yaw degrees about +z and moved by origin: the recentred frame,
   which faces the way the body does, into the world. */
void vr_pose_to_world(const vr_pose_t *local, const float origin[3], float yaw, vr_pose_t *out);

#endif /* VR_MATH_H */
