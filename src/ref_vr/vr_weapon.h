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
 *     once in each hand, with the other arm cut away down the middle;
 *   - a long weapon has a fore-end, where the other hand steadies it. That
 *     hand's grip button, pressed there, takes hold: the gun hand stays where
 *     it is and the weapon turns about it to lie along the line to the other
 *     hand, rolled as the gun hand is. Pressed anywhere else, the button does
 *     what it is bound to, as before.
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
 *
 * Left-handed ("vr_left_handed 1"), all of that happens in the left hand, and
 * a weapon built for the right one is drawn mirrored down its own middle, so
 * that the side of it the player sees is the side that was modelled. The
 * measurements are kept right-handed and mirrored as they are used, so a
 * weapon settled in one mode is settled in the other as well. The fists,
 * having an arm each side already, are not mirrored.
 */
#ifndef VR_WEAPON_H
#define VR_WEAPON_H

#include "refapi.h"

/* Registers the cvars: vr_weapon_hand, vr_left_handed, vr_show_hands,
   vr_freeze_idle, vr_punch, vr_punch_speed, vr_two_hand, vr_other_hand,
   vr_weapon_arm, vr_grip_<weapon>, vr_fore_<weapon>, vr_weapon_reset. */
void vr_weapon_startup(void);

/*
 * At the end of each registration - a level loading, or vid_restart - before
 * ref_gl frees what was not asked for: the fists' model, which the hand without
 * the weapon is drawn with. register_model is ref_gl's own RegisterModel.
 */
void vr_weapon_register_models(void *(*register_model)(const char *name));

/*
 * Whether the player is left-handed, as vr_left_handed said when the renderer
 * started. The buttons swap over with the weapon (vr_input.c), and a session
 * takes its bindings once, so the answer does not change until the renderer
 * does.
 */
int vr_weapon_left_handed(void);

/* After ref_gl's Init, and before Shutdown lets this DLL go: the two calls in
   sin.exe that are redirected here. */
void vr_weapon_install(void);
void vr_weapon_uninstall(void);

/*
 * The controllers, in the recentred frame, once a frame: begin, with the time
 * the poses are for, in seconds, then each hand that is tracked - where it is
 * held (grip), and where and which way it points (aim) - then publish, which
 * places the weapon, watches for punches and hands it all to the game.
 */
void vr_weapon_hands_begin(double seconds);

/* The frame the hands are measured in has just moved: whatever they seem to
   do across that is not the player's doing. */
void vr_weapon_recentred(void);
void vr_weapon_track_hand(int hand, const float grip[3], const float aim_origin[3], const float aim_angles[3]);

/* Where a hand holds its controller this frame, in the recentred frame.
   Returns 0 if it is not tracked. */
int vr_weapon_hand_at(int hand, float out[3]);
void vr_weapon_publish(void);

/* The frame is over: nothing is placed until the next publish. */
void vr_weapon_end_frame(void);

/*
 * Where the controllers are, drawn into the eye after ref_gl has finished with
 * it: a cross in each hand and a line the way it points, which is what makes
 * lining a frozen weapon up with the hand possible. fd must hold the view of
 * the eye just drawn. vr_show_hands says when: 1 while a weapon is being
 * placed, 2 always, 0 never.
 */
void vr_weapon_draw_hands(const refdef_t *fd);

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

/* The grip button of the hand the weapon is in, and that hand's stick up and
   down, which sizes the weapon while it is being adjusted. */
void vr_weapon_adjust_button(int down);
void vr_weapon_adjust_stick(float y);
int vr_weapon_adjusting(void);

/*
 * The grip button of the other hand. Pressed with that hand at the weapon's
 * fore-end, it takes hold of the weapon with both hands until it is let go;
 * pressed while the weapon is being adjusted, it puts the fore-end where the
 * hand is. Returns whether the press was taken for either - if not, it is the
 * button's ordinary press. A release always returns 0.
 */
int vr_weapon_steady(int down);

#endif /* VR_WEAPON_H */
