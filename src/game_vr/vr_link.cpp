#include <windows.h>
#include <string.h>
#include <stdio.h>

#include "../shared/sinvr_poses.h"
#include "vr_link.h"

/*
 * The renderer is loaded and unloaded by the engine, and a level can outlive
 * either, so the export is looked up again whenever it has not been found yet.
 * Once found it stays: the engine keeps the renderer for the life of the video
 * mode, and a lost renderer shows up as poses that stop advancing.
 */
static const sinvr_poses_t *poses;
static unsigned last_frame;
static unsigned last_frame_time;

static const sinvr_poses_t *find_poses(void)
{
	HMODULE renderer;
	sinvr_poses_fn get_poses;

	if (poses)
	{
		return poses;
	}

	renderer = GetModuleHandleA("ref_vr.dll");
	if (!renderer)
	{
		return NULL;
	}

	get_poses = (sinvr_poses_fn)GetProcAddress(renderer, "SinVrPoses");
	if (!get_poses)
	{
		return NULL;
	}

	poses = get_poses(SINVR_POSES_VERSION);
	return poses;
}

/*
 * Poses are only worth using while they are being written. The renderer stops
 * writing when VR is switched off or the headset goes idle, and the block would
 * otherwise keep reporting the last thing it saw.
 */
static int tracking(void)
{
	const sinvr_poses_t *p = find_poses();
	unsigned now;

	if (!p)
	{
		return 0;
	}

	now = GetTickCount();
	if (p->frame != last_frame)
	{
		last_frame = p->frame;
		last_frame_time = now;
		return 1;
	}

	/* A tenth of a second of the same frame means the renderer has stopped. */
	return now - last_frame_time < 100;
}

int VR_HandPose(int hand, float origin[3], float angles[3])
{
	const sinvr_poses_t *p;

	if ((hand != VR_HAND_LEFT && hand != VR_HAND_RIGHT) || !tracking())
	{
		return 0;
	}

	p = poses;
	if (!p->hand_valid[hand])
	{
		return 0;
	}

	memcpy(origin, p->hand_origin[hand], sizeof(float) * 3);
	memcpy(angles, p->hand_angles[hand], sizeof(float) * 3);
	return 1;
}

int VR_HeadPose(float origin[3], float angles[3])
{
	const sinvr_poses_t *p;

	if (!tracking())
	{
		return 0;
	}

	p = poses;
	if (!p->head_valid)
	{
		return 0;
	}

	memcpy(origin, p->head_origin, sizeof(float) * 3);
	memcpy(angles, p->head_angles, sizeof(float) * 3);
	return 1;
}

/*
 * Says once, and again after a break, that the renderer is sending poses. The
 * game prints it so that a player - or a log - can see the link is up without
 * having to fire a shot.
 */
int VR_TrackingChanged(void)
{
   static int told = 0;
   int live = tracking() && (poses->hand_valid[VR_HAND_RIGHT] || poses->head_valid);

   if(live == told)
   {
      return 0;
   }

   told = live;
   return live ? 1 : -1;
}
