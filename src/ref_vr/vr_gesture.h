/*
 * Reaching for things on the body.
 *
 * The grip buttons do more where the hand is: the gun hand's grip over a
 * shoulder changes weapon - over the gun hand's own shoulder the next, over
 * the other the previous - and at the gun hand's own hip draws the magnum;
 * both hands at the shoulders with both grips pressed together bring up the
 * fists. Anywhere else each grip does what it did (vr_input.c, vr_weapon.c).
 *
 * The places are measured from the head, turned with it, so they follow the
 * player's height and which way they face; a controller buzzes as it reaches
 * one. vr_gestures 0 turns it all off.
 */
#ifndef VR_GESTURE_H
#define VR_GESTURE_H

/* Registers vr_gestures. */
void vr_gesture_startup(void);

/* The head, in the recentred frame the hands are in, once a frame. */
void vr_gesture_head(const float origin[3], const float angles[3]);

/* After the hands are tracked: buzzes for a hand reaching a place, and a
   shoulder's weapon change that has waited long enough for the other hand. */
void vr_gesture_frame(void);

/* The grip buttons, pressed and let go. Returns 1 if this press - or the
   release of a press it took - was a gesture's, and so not the button's. */
int vr_gesture_gun_grip(int down);
int vr_gesture_other_grip(int down);

#endif /* VR_GESTURE_H */
