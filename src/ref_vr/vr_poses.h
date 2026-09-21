/*
 * The head, hands and weapon, as the game module reads them. The block and the
 * export that hands it over live here; who fills it in is vr_xr.c for the head,
 * vr_input.c for the hands and vr_weapon.c for the weapon. See
 * src/shared/sinvr_poses.h for the contract.
 */
#ifndef VR_POSES_H
#define VR_POSES_H

#include "../shared/sinvr_poses.h"

/* A frame's worth of poses is written between these two. */
void vr_poses_begin(float units_per_metre);
void vr_poses_head(const float origin[3], const float angles[3]);
void vr_poses_hand(int hand, const float origin[3], const float angles[3]);
void vr_poses_weapon(const float origin[3], const float angles[3], float scale, int flags);

/* A punch thrown with a hand. Kept, and counted, until the next one from the
   same hand: it is an event, and the game reads the block ten times a second. */
void vr_poses_punch(int hand, const float origin[3], const float dir[3], float speed);
void vr_poses_end(void);

/* Nothing is tracked any more: the game should go back to its own aiming. */
void vr_poses_clear(void);

/* The block as last written, for the renderer's own use. */
const sinvr_poses_t *vr_poses_current(void);

#endif /* VR_POSES_H */
