/*
 * What the renderer tells the game about the player's head and hands.
 *
 * Both DLLs live in sin.exe, so there is no need for shared memory or a socket:
 * ref_vr.dll exports SinVrPoses, the game module asks the loaded modules for it
 * (see src/game_vr/vr_link.cpp), and reads the block it returns. When the game
 * runs without the VR renderer the export is simply not there, and the game
 * carries on as it always did.
 *
 * The frame of reference is the recentred one: the origin is where the game puts
 * the player's eyes (Sentient::EyePosition), +x is the way the player's body
 * faces, +y is to their left and +z is up, in game units. So the game turns a
 * hand into world space with the player's own yaw:
 *
 *     world = EyePosition() + rotate_yaw(hand_origin, worldangles[YAW])
 *     angles = hand_angles + (0, worldangles[YAW], 0)
 *
 * Nothing here is written while the game is reading it: one thread, and the
 * renderer writes the whole block between engine frames.
 */
#ifndef SINVR_POSES_H
#define SINVR_POSES_H

#define SINVR_POSES_VERSION 1
#define SINVR_HAND_LEFT 0
#define SINVR_HAND_RIGHT 1

typedef struct
{
	int version;             /* SINVR_POSES_VERSION */
	unsigned frame;          /* renderer frames so far; stops when VR stops */
	unsigned time_ms;        /* GetTickCount when this was written */
	float units_per_metre;   /* what the poses were scaled by */

	int head_valid;
	float head_origin[3];    /* from the recentred origin, game units */
	float head_angles[3];    /* pitch, yaw, roll, degrees, Quake's senses */

	int hand_valid[2];       /* a hand is only valid while its controller is tracked */
	float hand_origin[2][3];
	float hand_angles[2][3];
} sinvr_poses_t;

/* ref_vr.dll's export. NULL if it speaks a different version. */
typedef const sinvr_poses_t *(*sinvr_poses_fn)(int version);

#endif /* SINVR_POSES_H */
