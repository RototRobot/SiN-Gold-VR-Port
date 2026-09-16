#include <windows.h>
#include <GL/gl.h>

#include "vr_keys.h"

/*
 * Quake II reads the scan code out of the message's lParam rather than the
 * virtual key, and the extended bit is what separates the arrow keys from the
 * keypad, so both have to be right.
 */
typedef struct
{
	WORD virtual_key;
	BYTE scan;
	int extended;
} key_t;

static const key_t keys[VR_KEY_COUNT] = {
	{ VK_UP, 0x48, 1 },
	{ VK_DOWN, 0x50, 1 },
	{ VK_LEFT, 0x4B, 1 },
	{ VK_RIGHT, 0x4D, 1 },
	{ VK_RETURN, 0x1C, 0 },
	{ VK_ESCAPE, 0x01, 0 }
};

/* The window ref_gl is drawing into, which is the one the engine listens to. */
static HWND engine_window(void)
{
	HDC dc = wglGetCurrentDC();

	return dc ? WindowFromDC(dc) : NULL;
}

void vr_key_tap(vr_key_t key)
{
	HWND window = engine_window();
	LPARAM down, up;

	if (!window || key < 0 || key >= VR_KEY_COUNT)
	{
		return;
	}

	down = (LPARAM)(1 | (keys[key].scan << 16) | (keys[key].extended ? (1 << 24) : 0));
	up = down | (1 << 30) | (1 << 31);

	PostMessageA(window, WM_KEYDOWN, keys[key].virtual_key, down);
	PostMessageA(window, WM_KEYUP, keys[key].virtual_key, up);
}
