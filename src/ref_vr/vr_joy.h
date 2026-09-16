/*
 * A joystick that only exists in this process, so that the controllers' sticks
 * can move the player.
 *
 * SiN's client is closed, but it still carries Quake II's joystick support: it
 * polls winmm for a stick every frame and turns the axes into movement and
 * turning. sin.exe imports joyGetNumDevs, joyGetDevCapsA and joyGetPosEx, so
 * answering those three in-process is enough to present one, with no driver and
 * nothing installed on the machine.
 */
#ifndef VR_JOY_H
#define VR_JOY_H

/*
 * Answers sin.exe's joystick calls from here on, and sets the engine's joystick
 * cvars so that the axes mean what vr_joy_set says they mean. Call before the
 * engine's input start-up, which happens after the renderer's Init. Returns 0
 * if sin.exe does not import those calls, in which case nothing changed.
 */
int vr_joy_install(void);

/*
 * The stick, from -1 to 1: forward is away from the player, side is to the
 * right, turn is to the right. Centres by itself if nothing sets it.
 */
void vr_joy_set(float forward, float side, float turn);

/* How many times the engine has polled the stick. 0 means it never found it. */
unsigned vr_joy_polls(void);

#endif /* VR_JOY_H */
