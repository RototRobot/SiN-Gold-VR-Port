/*
 * SiN Gold VR: what the game copies (SiN's and Wages of Sin's) call in the shared
 * game code, vr_game.cpp. Game headers come first: this uses their types.
 */
#ifndef VR_GAME_H
#define VR_GAME_H

/* How hard the blow Fists::Strike is landing is, against the button's: a
   swung punch lands harder the faster the fist was going. 1 otherwise. */
extern float vr_strike_scale;

/* The view weapon's idle animation is to be held still (the renderer says so):
   Weapon::ForceIdle takes the idle pose once and keeps it, and comes back
   every VR_IDLE_BEAT seconds in place of the animation's end. */
#define VR_IDLE_BEAT 0.5f
qboolean VR_HoldWeaponStill(Sentient *owner);

/* Which way to steer a guided missile, as world angles: where the hand
   holding the weapon points (vr_missile_steer 1, the default) or where the
   head looks (2), telling the renderer so that it does not turn the picture
   twice. fallback when that is not tracked, or the game runs without the VR
   renderer, or vr_missile_steer is 0. */
class Player;
Vector VR_MissileSteering(Player *player, Vector fallback);

#endif /* VR_GAME_H */
