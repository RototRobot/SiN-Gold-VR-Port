#include <windows.h>
#include <mmsystem.h>
#include <string.h>

#include "ref_vr.h"
#include "vr_hook.h"
#include "vr_joy.h"

/* The engine's raw axis range. Quake II reads value/32768 - 1, so this centre
   is exactly the middle. */
#define JOY_CENTRE 32768
#define JOY_SPAN 32767

static struct
{
	int installed;
	float forward, side, turn;
	unsigned polls;
} joy;

static float clamp_axis(float value)
{
	if (value > 1.0f)
	{
		return 1.0f;
	}
	if (value < -1.0f)
	{
		return -1.0f;
	}

	return value;
}

static DWORD raw_axis(float value)
{
	return (DWORD)(JOY_CENTRE + (int)(clamp_axis(value) * JOY_SPAN));
}

/* Enough devices for the engine to bother asking about the first one. */
static UINT WINAPI hook_joyGetNumDevs(void)
{
	return 16;
}

static MMRESULT WINAPI hook_joyGetDevCapsA(UINT_PTR id, LPJOYCAPSA caps, UINT size)
{
	if (id != JOYSTICKID1 || !caps || size < sizeof(JOYCAPSA))
	{
		return JOYERR_PARMS;
	}

	memset(caps, 0, size);
	caps->wMid = 0x1209;  /* generic, and not claiming to be anyone's product */
	caps->wPid = 0x5331;
	strcpy(caps->szPname, "SiN Gold VR controllers");
	caps->wXmax = caps->wYmax = caps->wZmax = caps->wRmax = caps->wUmax = caps->wVmax = 65535;
	caps->wPeriodMin = 10;
	caps->wPeriodMax = 1000;
	caps->wCaps = JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASV;
	caps->wMaxAxes = 6;
	caps->wNumAxes = 6;

	/* No buttons: the engine would turn them into key events, and the
	   controllers' buttons reach the game as console commands instead. */
	caps->wNumButtons = 0;
	caps->wMaxButtons = 0;

	return JOYERR_NOERROR;
}

static MMRESULT WINAPI hook_joyGetPosEx(UINT id, LPJOYINFOEX info)
{
	if (id != JOYSTICKID1 || !info || info->dwSize < sizeof(JOYINFOEX))
	{
		return JOYERR_PARMS;
	}

	joy.polls++;

	/* x is the side axis and y the forward one; r carries turning, because the
	   engine's advanced mapping puts AxisTurn there (vr_input.c sets it up).
	   Turning is negated: the engine adds this to the yaw, which grows to the
	   left, while a stick pushed right should turn right. */
	info->dwXpos = raw_axis(joy.side);
	info->dwYpos = raw_axis(joy.forward);
	info->dwZpos = JOY_CENTRE;
	info->dwRpos = raw_axis(-joy.turn);
	info->dwUpos = JOY_CENTRE;
	info->dwVpos = JOY_CENTRE;
	info->dwButtons = 0;
	info->dwButtonNumber = 0;
	info->dwPOV = JOY_POVCENTERED;

	return JOYERR_NOERROR;
}

int vr_joy_install(void)
{
	HMODULE exe = GetModuleHandleA(NULL);
	int patched = 0;

	if (joy.installed)
	{
		return 1;
	}

	patched += vr_hook_import(exe, "WINMM.dll", "joyGetNumDevs", (void *)hook_joyGetNumDevs, NULL);
	patched += vr_hook_import(exe, "WINMM.dll", "joyGetDevCapsA", (void *)hook_joyGetDevCapsA, NULL);
	patched += vr_hook_import(exe, "WINMM.dll", "joyGetPosEx", (void *)hook_joyGetPosEx, NULL);

	if (patched != 3)
	{
		vr_printf("VR: sin.exe's joystick calls could not be answered (%d of 3) - "
			"the sticks will not move the player", patched);
		return 0;
	}

	joy.installed = 1;
	return 1;
}

void vr_joy_set(float forward, float side, float turn)
{
	joy.forward = forward;
	joy.side = side;
	joy.turn = turn;
}

unsigned vr_joy_polls(void)
{
	return joy.polls;
}
