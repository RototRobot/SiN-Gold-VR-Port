/*
 * Roomscale: walking in the room walks the player, and crouching in it
 * crouches. See vr_room.c.
 */
#ifndef VR_ROOM_H
#define VR_ROOM_H

/* The settings, whether or not the headset is used (config.cfg keeps them). */
void vr_room_startup(void);

/* Reaches into sin.exe's movement commands and prediction; and lets go. */
void vr_room_install(void);
void vr_room_uninstall(void);

/*
 * Once a frame, before any pose is measured from the recentred head: the head
 * as the runtime located it, in the terms of vr_eye_t. This is where the view
 * is lowered for the crouch button, held short of the ceiling, and where a
 * head that has leant too far from the body takes the room with it.
 */
void vr_room_frame(const float position[3], const float orientation[4]);

#endif /* VR_ROOM_H */
