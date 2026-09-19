/*
 * Keystrokes, for the parts of the game that only listen to the keyboard.
 *
 * SiN's menus, and its console, are driven by key events rather than by console
 * commands, so the controllers cannot reach them the way they reach the game.
 * Posting a key message to the engine's own window does: it arrives in the
 * engine's message loop exactly as the keyboard's would, scan code and all,
 * which is what Quake II's input reads.
 */
#ifndef VR_KEYS_H
#define VR_KEYS_H

typedef enum
{
	VR_KEY_UP,
	VR_KEY_DOWN,
	VR_KEY_LEFT,
	VR_KEY_RIGHT,
	VR_KEY_ENTER,
	VR_KEY_ESCAPE,
	VR_KEY_SPACE,
	VR_KEY_COUNT
} vr_key_t;

/* Press and release, as one tap. Silently does nothing if the window is gone. */
void vr_key_tap(vr_key_t key);

#endif /* VR_KEYS_H */
