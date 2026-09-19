/*
 * The weapon in the player's hands.
 *
 * SiN draws the weapon the player holds as a "view model": an ordinary model,
 * flagged RF_WEAPONMODEL, placed at the eye, turned with the view and built to
 * look right from there - lower right, pointing ahead, and about three times
 * life size (the game's own muzzle code scales its bones by 0.3). In VR it goes
 * to the right controller instead:
 *
 *   - a controller is a frame: where the hand holds it (the grip pose) and the
 *     way it points (the aim pose);
 *   - each weapon has a grip, the point of the model the hand holds, and a
 *     size. A table of first guesses measured off the models gives them, and
 *     the player can replace them in the headset: holding the right grip
 *     button lets go of the weapon, and letting go of the button takes it
 *     back the way the hand is then holding it;
 *   - the fists are one model with both arms in it, so it is drawn twice,
 *     once in each hand, with the other arm cut away down the middle.
 *
 * The placement is worked out once a frame, in the recentred frame of
 * sinvr_poses.h, and handed to the game module, which fires from the barrel it
 * describes. It reaches the picture through sin.exe itself where it can: the
 * client builds the view weapon's entity and straight away runs the model's
 * client-side commands on it - muzzle flash, tracers, shells and bullet
 * impacts of its own - so the one call that hands the entity to the scene is
 * redirected here and the entity moved before anything uses it. The client's
 * own bullet impacts, which start at the eye, are left out and the server's
 * used instead. If sin.exe is not the build this was measured on, nothing in
 * it is touched and the entity is moved in RenderFrame, which places the model
 * but not its effects.
 */
#ifndef VR_WEAPON_H
#define VR_WEAPON_H

#include "refapi.h"

/* Registers the cvars: vr_weapon_hand, vr_grip_<weapon>, vr_weapon_reset. */
void vr_weapon_startup(void);

/* After ref_gl's Init, and before Shutdown lets this DLL go: the two calls in
   sin.exe that are redirected here. */
void vr_weapon_install(void);
void vr_weapon_uninstall(void);

/*
 * The controllers, in the recentred frame, once a frame: begin, then each hand
 * that is tracked - where it is held (grip), and where and which way it points
 * (aim) - then publish, which places the weapon and hands it to the game.
 */
void vr_weapon_hands_begin(void);
void vr_weapon_track_hand(int hand, const float grip[3], const float aim_origin[3], const float aim_angles[3]);
void vr_weapon_publish(void);

/* The frame is over: nothing is placed until the next publish. */
void vr_weapon_end_frame(void);

/*
 * Only when sin.exe could not be redirected: moves the view weapon in fd to
 * the hands, adding the second arm if there is one. fd must still hold the
 * engine's own view, before vr_view_apply. vr_weapon_after_eye puts fd's
 * entity list back afterwards.
 */
void vr_weapon_to_hand(refdef_t *fd);
void vr_weapon_after_eye(refdef_t *fd);

/* Whether this frame's weapon is in the hand - when a crosshair drawn where the
   head looks would only mislead. */
int vr_weapon_in_hand(void);

/* Where the weapon is and which way it points, in the world, as it was drawn
   this frame: what the crosshair follows. Returns 0 if it is not in the hand. */
int vr_weapon_world_aim(float origin[3], float forward[3]);

/* The right grip button, and the right stick's up and down, which sizes the
   weapon while it is being adjusted. */
void vr_weapon_adjust_button(int down);
void vr_weapon_adjust_stick(float y);
int vr_weapon_adjusting(void);

#endif /* VR_WEAPON_H */
