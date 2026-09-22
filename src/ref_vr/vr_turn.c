#include <windows.h>
#include <stdio.h>

#include "ref_vr.h"
#include "vr_hook.h"
#include "vr_turn.h"
#include "vrlog.h"

/*
 * Measured on the Steam build of sin.exe, as offsets from its base. The
 * joystick's turning in IN_JoyMove adds its step to the yaw and stores it
 * straight back:
 *
 *     +0x9B8C8  addss xmm0, [cl.viewangles + 4]
 *     +0x9B8D0  movss [cl.viewangles + 4], xmm0
 *
 * and the keys' turning in CL_AdjustAngles (+0xE54C, +0xE58F) stores to
 * the same address. Both joystick instructions have to name it before it is
 * believed.
 */
#define SIN_JOYSTICK_ADDS_YAW 0x9B8C8
#define SIN_JOYSTICK_STORES_YAW 0x9B8D0

static float *sin_yaw;
static cvar_t *vr_fake_snap;

void vr_turn_install(void)
{
	static const BYTE addss_xmm0[] = { 0xF3, 0x0F, 0x58, 0x05 };
	static const BYTE movss_from_xmm0[] = { 0xF3, 0x0F, 0x11, 0x05 };
	HMODULE exe = GetModuleHandleA(NULL);
	const void *adds, *stores;

	sin_yaw = NULL;
	vr_fake_snap = vr_cvar("vr_fake_snap", "", 0);
	if (!exe)
	{
		return;
	}

	adds = vr_hook_data_address(exe, SIN_JOYSTICK_ADDS_YAW, addss_xmm0, sizeof(addss_xmm0));
	stores = vr_hook_data_address(exe, SIN_JOYSTICK_STORES_YAW, movss_from_xmm0, sizeof(movss_from_xmm0));
	if (!adds || adds != stores)
	{
		vr_printf("VR: this sin.exe is not the build the turning code knows - snap turning is not "
			"available, and the stick turns smoothly");
		return;
	}

	sin_yaw = (float *)adds;
	vrlog("VR: snap turning reaches the client's view angle");
}

int vr_turn_available(void)
{
	return sin_yaw != NULL;
}

void vr_turn_frame(void)
{
	float degrees;

	if (!vr_fake_snap || !vr_fake_snap->string || !vr_fake_snap->string[0])
	{
		return;
	}

	if (sscanf(vr_fake_snap->string, "%f", &degrees) == 1)
	{
		vrlog("    input: a pretend snap turn of %.0f", degrees);
		vr_turn_by(degrees);
	}
	vr_cvar_set("vr_fake_snap", "");
}

void vr_turn_by(float degrees)
{
	/* No wrapping: the client's own turning never wraps it either, and the
	   angle goes to the game as a short, which does. */
	if (sin_yaw)
	{
		*sin_yaw += degrees;
	}
}
