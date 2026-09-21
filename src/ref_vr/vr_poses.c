#include <windows.h>
#include <string.h>

#include "vr_poses.h"

/*
 * One block, written in place. The game reads it on the same thread, between
 * engine frames, so a half-written frame cannot be seen; the frame counter is
 * there so the game can tell a stale block from a live one.
 */
static sinvr_poses_t poses = { SINVR_POSES_VERSION };

void vr_poses_begin(float units_per_metre)
{
	poses.units_per_metre = units_per_metre;
	poses.head_valid = 0;
	poses.hand_valid[SINVR_HAND_LEFT] = 0;
	poses.hand_valid[SINVR_HAND_RIGHT] = 0;
	poses.weapon_valid = 0;
	poses.flags = 0;
}

void vr_poses_head(const float origin[3], const float angles[3])
{
	memcpy(poses.head_origin, origin, sizeof(poses.head_origin));
	memcpy(poses.head_angles, angles, sizeof(poses.head_angles));
	poses.head_valid = 1;
}

void vr_poses_hand(int hand, const float origin[3], const float angles[3])
{
	if (hand != SINVR_HAND_LEFT && hand != SINVR_HAND_RIGHT)
	{
		return;
	}

	memcpy(poses.hand_origin[hand], origin, sizeof(poses.hand_origin[hand]));
	memcpy(poses.hand_angles[hand], angles, sizeof(poses.hand_angles[hand]));
	poses.hand_valid[hand] = 1;
}

void vr_poses_weapon(const float origin[3], const float angles[3], float scale, int flags)
{
	memcpy(poses.weapon_origin, origin, sizeof(poses.weapon_origin));
	memcpy(poses.weapon_angles, angles, sizeof(poses.weapon_angles));
	poses.weapon_scale = scale;
	poses.flags = flags;
	poses.weapon_valid = 1;
}

void vr_poses_punch(int hand, const float origin[3], const float dir[3], float speed)
{
	if (hand != SINVR_HAND_LEFT && hand != SINVR_HAND_RIGHT)
	{
		return;
	}

	memcpy(poses.punch_origin[hand], origin, sizeof(poses.punch_origin[hand]));
	memcpy(poses.punch_dir[hand], dir, sizeof(poses.punch_dir[hand]));
	poses.punch_speed[hand] = speed;
	if (++poses.punch_count[hand] == 0)
	{
		poses.punch_count[hand] = 1; /* 0 is "none yet" */
	}
}

void vr_poses_end(void)
{
	poses.time_ms = GetTickCount();
	poses.frame++;
}

void vr_poses_clear(void)
{
	unsigned frame = poses.frame;

	memset(&poses, 0, sizeof(poses));
	poses.version = SINVR_POSES_VERSION;
	poses.frame = frame;
}

const sinvr_poses_t *vr_poses_current(void)
{
	return &poses;
}

/* ref_vr.dll's side of the contract. */
__declspec(dllexport) const sinvr_poses_t *SinVrPoses(int version)
{
	return version == SINVR_POSES_VERSION ? &poses : NULL;
}
