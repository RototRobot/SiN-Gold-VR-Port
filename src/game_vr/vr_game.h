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

#endif /* VR_GAME_H */
