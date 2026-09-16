/*
 * The VR controllers, as far as the game is concerned.
 *
 * SiN's client is closed, so input arrives from the side, two ways:
 *   - buttons become console commands, exactly as a key bind would
 *     ("+attack" when pressed, "-attack" when released);
 *   - the thumbsticks become a joystick the engine polls for itself (vr_joy.c),
 *     which gets movement and turning through its own input path.
 *
 * Each button's command is a cvar, so it can be rebound from the console:
 * vr_bind_fire, vr_bind_use, and so on. Setting one to nothing unbinds it.
 */
#ifndef VR_INPUT_H
#define VR_INPUT_H

#include <openxr/openxr.h>

/*
 * Registers the cvars, and presents the joystick to the engine. Call from the
 * renderer's Init: the engine looks for a joystick just after that, and once
 * only.
 */
void vr_input_startup(void);

/* The extension that gives the Reverb G2's controllers their A/B/X/Y buttons,
   to enable at instance creation when the runtime has it. */
const char *vr_input_extension(void);

/*
 * Creates the actions and attaches them to the session. Once per session,
 * before the first frame, since a session takes its action sets only once.
 */
int vr_input_create(XrInstance instance, XrSession session);

/* Once per frame: reads the controllers and passes their state to the game. */
void vr_input_frame(XrSession session);

/* Lets go of anything held, and centres the sticks. */
void vr_input_release(void);

/* Forgets the actions. The session they belong to is going away. */
void vr_input_destroy(void);

#endif /* VR_INPUT_H */
