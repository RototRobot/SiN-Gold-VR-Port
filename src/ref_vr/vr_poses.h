/*
 * The head and hands, as the game module reads them. The block and the export
 * that hands it over live here; who fills it in is vr_xr.c for the head and
 * vr_input.c for the hands. See src/shared/sinvr_poses.h for the contract.
 */
#ifndef VR_POSES_H
#define VR_POSES_H

#include "../shared/sinvr_poses.h"

/* A frame's worth of poses is written between these two. */
void vr_poses_begin(float units_per_metre);
void vr_poses_head(const float origin[3], const float angles[3]);
void vr_poses_hand(int hand, const float origin[3], const float angles[3]);
void vr_poses_end(void);

/* Nothing is tracked any more: the game should go back to its own aiming. */
void vr_poses_clear(void);

/* The block as last written, for the renderer's own use (the view weapon). */
const sinvr_poses_t *vr_poses_current(void);

#endif /* VR_POSES_H */
