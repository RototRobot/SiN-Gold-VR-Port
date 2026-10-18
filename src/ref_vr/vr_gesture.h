/*
 * Reaching for things on the body.
 *
 * The grip buttons do more where the hand is. The gun hand's grip over a
 * shoulder: a tap changes weapon - over the gun hand's own shoulder the next,
 * over the other the previous - and held, it opens the weapon menu
 * (vr_holster.h). At the gun hand's own hip it draws the magnum. The other
 * hand's grip at its own hip: a tap uses the inventory item the HUD shows, and
 * held, it opens the inventory menu. Both hands at the shoulders with both
 * grips pressed together bring up the fists. While a menu is open, the trigger
 * of the hand holding it steps along it, taking that hand out of the holster
 * takes the one picked, and letting go of the grip there closes it with nothing
 * changed. Anywhere else each grip does what it did (vr_input.c, vr_weapon.c).
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

/* After the hands are tracked: buzzes for a hand reaching a place, menus
   opening once a grip has been held, and closing as the hand leaves. */
void vr_gesture_frame(void);

/* The grip buttons, pressed and let go. Returns 1 if this press - or the
   release of a press it took - was a gesture's, and so not the button's. */
int vr_gesture_gun_grip(int down);
int vr_gesture_other_grip(int down);

/* A trigger, pressed or let go: the gun hand's if gun is set. Returns 1 if it
   belongs to an open menu, and so is not the fire or use button. */
int vr_gesture_trigger(int gun, int down);

#endif /* VR_GESTURE_H */
