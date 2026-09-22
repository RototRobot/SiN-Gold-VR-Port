/*
 * Snap turning. SiN turns the player by changing the view angle its client
 * keeps - cl.viewangles, which every movement command carries to the game -
 * and both of its ways of turning, the joystick and the keys, change it at a
 * rate times the frame's length. That is smooth turning and nothing else, so a
 * snap goes to the angle itself: so many degrees, in one go.
 */
#ifndef VR_TURN_H
#define VR_TURN_H

/* Finds the angle in sin.exe. After ref_gl's Init, with the other patches;
   if sin.exe is not the build this was measured on, nothing is touched and
   turning stays smooth. */
void vr_turn_install(void);

/* Whether a snap can be made. */
int vr_turn_available(void);

/* Turns the player by that many degrees: positive to the left, which is the
   way the engine's yaw grows. */
void vr_turn_by(float degrees);

/* Once a frame, headset or not. Desk testing: "vr_fake_snap <degrees>" turns
   the player that much once, the way a snap does, with no controllers. */
void vr_turn_frame(void);

#endif /* VR_TURN_H */
