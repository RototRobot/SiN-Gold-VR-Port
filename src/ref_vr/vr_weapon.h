/*
 * The view weapon, taken off the camera and put in the player's right hand.
 *
 * The engine draws the weapon the player is holding as an ordinary entity in
 * the frame's list, flagged RF_WEAPONMODEL, placed at the eye and turned with
 * the view. In VR the eye is not where the hand is, so before each eye is
 * drawn the entity is moved to the tracked right hand: its origin, its angles
 * and the orientation matrix SiN's models are actually drawn from. The client
 * builds the list afresh every frame, so nothing needs putting back.
 *
 * Aiming is the game module's business (Player::GunPosition): this only makes
 * the gun appear where the shots now come from.
 */
#ifndef VR_WEAPON_H
#define VR_WEAPON_H

#include "refapi.h"

/* Registers vr_weapon_hand, vr_weapon_scale and vr_weapon_offset. */
void vr_weapon_startup(void);

/*
 * Moves the view weapon to the right hand, if it is tracked. fd must still
 * hold the engine's own view - before vr_view_apply - because the hand is
 * measured from the player's eyes and turned with the player's body.
 */
void vr_weapon_to_hand(refdef_t *fd);

/* Whether the last frame's weapon went to the hand - when a crosshair drawn
   where the head looks would only mislead. */
int vr_weapon_in_hand(void);

#endif /* VR_WEAPON_H */
