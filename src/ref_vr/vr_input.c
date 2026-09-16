/*
 * OpenXR actions, turned into the two things SiN's closed client will accept:
 * console commands and a joystick. See vr_input.h.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <openxr/openxr.h>

#include "refapi.h"
#include "ref_vr.h"
#include "vr_input.h"
#include "vr_joy.h"
#include "vrlog.h"

#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* The Reverb G2's controllers report their A/B/X/Y buttons only through this. */
#define HP_CONTROLLER_EXTENSION "XR_EXT_hp_mixed_reality_controller"

enum
{
	ACT_FIRE,
	ACT_USE,
	ACT_JUMP,
	ACT_CROUCH,
	ACT_WEAPON_NEXT,
	ACT_WEAPON_PREV,
	ACT_RUN,
	ACT_MENU,
	ACT_MOVE,
	ACT_TURN,
	ACT_HAND_LEFT,
	ACT_HAND_RIGHT,
	ACT_COUNT
};

typedef enum
{
	KIND_BUTTON,
	KIND_AXES,
	KIND_POSE
} action_kind_t;

typedef struct
{
	const char *name;    /* the OpenXR action name, and the cvar's suffix */
	const char *title;   /* what a binding interface would show a player */
	action_kind_t kind;
	const char *command; /* buttons: the command bound to it by default */
	XrAction action;
	cvar_t *bind;        /* buttons: vr_bind_<name>, so it can be rebound */
	int down;            /* buttons: whether it was held last frame */
} action_t;

static action_t actions[ACT_COUNT] = {
	{ "fire",        "Fire",            KIND_BUTTON, "+attack" },
	{ "use",         "Use",             KIND_BUTTON, "+use" },
	{ "jump",        "Jump",            KIND_BUTTON, "+moveup" },
	{ "crouch",      "Crouch",          KIND_BUTTON, "+movedown" },
	{ "weapon_next", "Next weapon",     KIND_BUTTON, "weapnext" },
	{ "weapon_prev", "Previous weapon", KIND_BUTTON, "weapprev" },
	{ "run",         "Run",             KIND_BUTTON, "+speed" },
	{ "menu",        "Menu",            KIND_BUTTON, "togglemenu" },
	{ "move",        "Move",            KIND_AXES,   NULL },
	{ "turn",        "Turn",            KIND_AXES,   NULL },
	{ "hand_left",   "Left hand",       KIND_POSE,   NULL },
	{ "hand_right",  "Right hand",      KIND_POSE,   NULL }
};

typedef struct
{
	int action;
	const char *path;
} binding_t;

/* The Reverb G2, through the HP extension: everything has a home here. */
static const binding_t bindings_hp[] = {
	{ ACT_FIRE,        "/user/hand/right/input/trigger/value" },
	{ ACT_USE,         "/user/hand/left/input/trigger/value" },
	{ ACT_JUMP,        "/user/hand/right/input/a/click" },
	{ ACT_CROUCH,      "/user/hand/left/input/x/click" },
	{ ACT_WEAPON_NEXT, "/user/hand/right/input/b/click" },
	{ ACT_WEAPON_PREV, "/user/hand/left/input/y/click" },
	{ ACT_RUN,         "/user/hand/left/input/squeeze/value" },
	{ ACT_MENU,        "/user/hand/left/input/menu/click" },
	{ ACT_MOVE,        "/user/hand/left/input/thumbstick" },
	{ ACT_TURN,        "/user/hand/right/input/thumbstick" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" }
};

/* Windows Mixed Reality controllers without that extension: no lettered
   buttons, so the clicks move onto the sticks and pads. */
static const binding_t bindings_wmr[] = {
	{ ACT_FIRE,        "/user/hand/right/input/trigger/value" },
	{ ACT_USE,         "/user/hand/left/input/trigger/value" },
	{ ACT_JUMP,        "/user/hand/right/input/thumbstick/click" },
	{ ACT_CROUCH,      "/user/hand/left/input/thumbstick/click" },
	{ ACT_WEAPON_NEXT, "/user/hand/right/input/trackpad/click" },
	{ ACT_WEAPON_PREV, "/user/hand/left/input/trackpad/click" },
	{ ACT_RUN,         "/user/hand/left/input/squeeze/click" },
	{ ACT_MENU,        "/user/hand/left/input/menu/click" },
	{ ACT_MOVE,        "/user/hand/left/input/thumbstick" },
	{ ACT_TURN,        "/user/hand/right/input/thumbstick" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" }
};

static const binding_t bindings_touch[] = {
	{ ACT_FIRE,        "/user/hand/right/input/trigger/value" },
	{ ACT_USE,         "/user/hand/left/input/trigger/value" },
	{ ACT_JUMP,        "/user/hand/right/input/a/click" },
	{ ACT_CROUCH,      "/user/hand/left/input/x/click" },
	{ ACT_WEAPON_NEXT, "/user/hand/right/input/b/click" },
	{ ACT_WEAPON_PREV, "/user/hand/left/input/y/click" },
	{ ACT_RUN,         "/user/hand/left/input/squeeze/value" },
	{ ACT_MENU,        "/user/hand/left/input/menu/click" },
	{ ACT_MOVE,        "/user/hand/left/input/thumbstick" },
	{ ACT_TURN,        "/user/hand/right/input/thumbstick" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" }
};

static const binding_t bindings_index[] = {
	{ ACT_FIRE,        "/user/hand/right/input/trigger/value" },
	{ ACT_USE,         "/user/hand/left/input/trigger/value" },
	{ ACT_JUMP,        "/user/hand/right/input/a/click" },
	{ ACT_CROUCH,      "/user/hand/left/input/a/click" },
	{ ACT_WEAPON_NEXT, "/user/hand/right/input/b/click" },
	{ ACT_WEAPON_PREV, "/user/hand/left/input/b/click" },
	{ ACT_RUN,         "/user/hand/left/input/squeeze/value" },
	{ ACT_MOVE,        "/user/hand/left/input/thumbstick" },
	{ ACT_TURN,        "/user/hand/right/input/thumbstick" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" }
};

/* Whatever else a runtime offers: the two buttons every profile has. */
static const binding_t bindings_simple[] = {
	{ ACT_FIRE,        "/user/hand/right/input/select/click" },
	{ ACT_USE,         "/user/hand/left/input/select/click" },
	{ ACT_MENU,        "/user/hand/left/input/menu/click" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" }
};

typedef struct
{
	const char *profile;
	const binding_t *bindings;
	int count;
} profile_t;

static const profile_t profiles[] = {
	{ "/interaction_profiles/hp/mixed_reality_controller", bindings_hp, ARRAY_COUNT(bindings_hp) },
	{ "/interaction_profiles/microsoft/motion_controller", bindings_wmr, ARRAY_COUNT(bindings_wmr) },
	{ "/interaction_profiles/oculus/touch_controller", bindings_touch, ARRAY_COUNT(bindings_touch) },
	{ "/interaction_profiles/valve/index_controller", bindings_index, ARRAY_COUNT(bindings_index) },
	{ "/interaction_profiles/khr/simple_controller", bindings_simple, ARRAY_COUNT(bindings_simple) }
};

static XrInstance xr_instance;
static XrActionSet action_set;
static cvar_t *vr_input;
static cvar_t *vr_deadzone;
static cvar_t *vr_fake_axes;
static cvar_t *vr_fake_press;
static int fake_held = -1;
static int fake_frames;
static int started;
static int said_active;
static int said_unfocused;
static int said_polled;
static int said_unpolled;
static unsigned frames;

/* ------------------------------------------------------------------------- */
/* Start-up                                                                   */
/* ------------------------------------------------------------------------- */

void vr_input_startup(void)
{
	char name[64];
	int i;

	if (started)
	{
		return;
	}
	started = 1;

	vr_input = vr_cvar("vr_input", "1", CVAR_ARCHIVE);
	vr_deadzone = vr_cvar("vr_deadzone", "0.15", CVAR_ARCHIVE);
	vr_fake_axes = vr_cvar("vr_fake_axes", "", 0);
	vr_fake_press = vr_cvar("vr_fake_press", "", 0);

	for (i = 0; i < ACT_COUNT; ++i)
	{
		if (actions[i].kind != KIND_BUTTON)
		{
			continue;
		}

		sprintf(name, "vr_bind_%s", actions[i].name);
		actions[i].bind = vr_cvar(name, actions[i].command, CVAR_ARCHIVE);
	}

	if (vr_input->value == 0.0f)
	{
		return;
	}

	if (!vr_joy_install())
	{
		return;
	}

	/*
	 * The engine's own joystick handling does the rest, so it is set up to read
	 * the axes vr_joy.c fills: x sideways, y forward, r turning. Sensitivity 1
	 * and no threshold keep the values as they are given; the deadzone is
	 * applied here instead, where both axes of a stick can be judged together.
	 */
	vr_cvar_set("in_joystick", "1");
	vr_cvar_set("joy_advanced", "1");
	vr_cvar_set("joy_advaxisx", "3"); /* side */
	vr_cvar_set("joy_advaxisy", "1"); /* forward */
	vr_cvar_set("joy_advaxisz", "0");
	vr_cvar_set("joy_advaxisr", "4"); /* turn */
	vr_cvar_set("joy_advaxisu", "0");
	vr_cvar_set("joy_advaxisv", "0");
	vr_cvar_set("joy_forwardsensitivity", "1");
	vr_cvar_set("joy_sidesensitivity", "1");
	vr_cvar_set("joy_yawsensitivity", "1");
	vr_cvar_set("joy_pitchsensitivity", "1");
	vr_cvar_set("joy_forwardthreshold", "0");
	vr_cvar_set("joy_sidethreshold", "0");
	vr_cvar_set("joy_yawthreshold", "0");
	vr_cvar_set("joy_pitchthreshold", "0");

	/* The engine builds its axis mapping from those cvars on this command, and
	   at its own start-up, which is still ahead of us. */
	vr_command("joy_advancedupdate");
}

const char *vr_input_extension(void)
{
	return HP_CONTROLLER_EXTENSION;
}

static XrPath string_to_path(XrInstance instance, const char *string)
{
	XrPath path = XR_NULL_PATH;

	if (XR_FAILED(xrStringToPath(instance, string, &path)))
	{
		return XR_NULL_PATH;
	}

	return path;
}

static void suggest_profile(XrInstance instance, const profile_t *profile)
{
	XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	XrActionSuggestedBinding list[ARRAY_COUNT(bindings_hp)];
	uint32_t count = 0;
	XrResult result;
	int i;

	for (i = 0; i < profile->count && count < ARRAY_COUNT(list); ++i)
	{
		XrPath path = string_to_path(instance, profile->bindings[i].path);

		if (path == XR_NULL_PATH)
		{
			continue;
		}

		list[count].action = actions[profile->bindings[i].action].action;
		list[count].binding = path;
		count++;
	}

	suggested.interactionProfile = string_to_path(instance, profile->profile);
	suggested.countSuggestedBindings = count;
	suggested.suggestedBindings = list;
	if (suggested.interactionProfile == XR_NULL_PATH || count == 0)
	{
		return;
	}

	/* A runtime refuses profiles it does not know, which is not our problem:
	   the ones it does know are what matter. */
	result = xrSuggestInteractionProfileBindings(instance, &suggested);
	if (XR_FAILED(result))
	{
		vrlog("VR: the runtime would not take bindings for %s (%d)", profile->profile, (int)result);
	}
}

int vr_input_create(XrInstance instance, XrSession session)
{
	XrActionSetCreateInfo set_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
	XrSessionActionSetsAttachInfo attach_info = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	int i;

	if (!vr_input || vr_input->value == 0.0f)
	{
		return 0;
	}

	xr_instance = instance;
	strcpy(set_info.actionSetName, "gameplay");
	strcpy(set_info.localizedActionSetName, "Gameplay");
	set_info.priority = 0;
	if (XR_FAILED(xrCreateActionSet(instance, &set_info, &action_set)))
	{
		vr_printf("VR: could not create the action set - the controllers will do nothing");
		action_set = XR_NULL_HANDLE;
		return 0;
	}

	for (i = 0; i < ACT_COUNT; ++i)
	{
		XrActionCreateInfo action_info = { XR_TYPE_ACTION_CREATE_INFO };

		strcpy(action_info.actionName, actions[i].name);
		strcpy(action_info.localizedActionName, actions[i].title);
		switch (actions[i].kind)
		{
		case KIND_AXES:
			action_info.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
			break;
		case KIND_POSE:
			action_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
			break;
		default:
			action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			break;
		}

		if (XR_FAILED(xrCreateAction(action_set, &action_info, &actions[i].action)))
		{
			vr_printf("VR: could not create the \"%s\" action", actions[i].name);
			actions[i].action = XR_NULL_HANDLE;
		}
	}

	for (i = 0; i < ARRAY_COUNT(profiles); ++i)
	{
		suggest_profile(instance, &profiles[i]);
	}

	attach_info.countActionSets = 1;
	attach_info.actionSets = &action_set;
	if (XR_FAILED(xrAttachSessionActionSets(session, &attach_info)))
	{
		vr_printf("VR: the session would not take the action set - the controllers will do nothing");
		vr_input_destroy();
		return 0;
	}

	return 1;
}

/* ------------------------------------------------------------------------- */
/* Each frame                                                                 */
/* ------------------------------------------------------------------------- */

static void release_buttons(void);
static void read_sticks(XrSession session);
static void do_fake_press(void);

/* A press runs the command as typed; a release runs the "-" form of a "+"
   command, which is what the engine's own key binds do. */
static void dispatch(action_t *action, int down)
{
	const char *command = action->bind && action->bind->string ? action->bind->string : action->command;
	char text[128];

	if (!command || !command[0])
	{
		return;
	}

	if (down)
	{
		vrlog("    input: %s pressed -> \"%s\"", action->name, command);
		vr_command(command);
		return;
	}

	if (command[0] == '+')
	{
		sprintf(text, "-%.120s", command + 1);
		vrlog("    input: %s released -> \"%s\"", action->name, text);
		vr_command(text);
	}
}

/*
 * Desk testing: "vr_fake_press jump" presses that action for one frame, through
 * the same path a controller button takes. The names are the ones in the
 * vr_bind_* cvars.
 */
static void do_fake_press(void)
{
	int i;

	/* Held for a few frames: the engine samples its buttons when it builds a
	   movement command, not when ours arrives. */
	if (fake_held >= 0)
	{
		if (--fake_frames > 0)
		{
			return;
		}

		actions[fake_held].down = 0;
		dispatch(&actions[fake_held], 0);
		fake_held = -1;
	}

	if (!vr_fake_press || !vr_fake_press->string || !vr_fake_press->string[0])
	{
		return;
	}

	for (i = 0; i < ACT_COUNT; ++i)
	{
		if (actions[i].kind == KIND_BUTTON && strcmp(actions[i].name, vr_fake_press->string) == 0)
		{
			actions[i].down = 1;
			dispatch(&actions[i], 1);
			fake_held = i;
			fake_frames = 12;
			break;
		}
	}

	if (fake_held < 0)
	{
		vr_printf("VR: there is no button called \"%s\"", vr_fake_press->string);
	}

	vr_cvar_set("vr_fake_press", "");
}

static void read_buttons(XrSession session)
{
	int i;

	for (i = 0; i < ACT_COUNT; ++i)
	{
		XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
		XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		int down;

		if (actions[i].kind != KIND_BUTTON || actions[i].action == XR_NULL_HANDLE)
		{
			continue;
		}

		get_info.action = actions[i].action;
		if (XR_FAILED(xrGetActionStateBoolean(session, &get_info, &state)) || !state.isActive)
		{
			/* The controller it is bound to has gone. Anything held on it has
			   to be let go of, or it stays held for good. */
			if (actions[i].down && i != fake_held)
			{
				actions[i].down = 0;
				dispatch(&actions[i], 0);
			}
			continue;
		}

		down = state.currentState ? 1 : 0;
		if (down != actions[i].down)
		{
			actions[i].down = down;
			dispatch(&actions[i], down);
		}
	}
}

/* Away from the middle, a stick starts at nothing and reaches full travel at
   the edge, so that the deadzone does not turn into a step. */
static void apply_deadzone(float *x, float *y)
{
	float zone = vr_deadzone ? vr_deadzone->value : 0.0f;
	double length = sqrt((double)(*x) * (*x) + (double)(*y) * (*y));
	double scale;

	if (zone < 0.0f)
	{
		zone = 0.0f;
	}
	if (zone > 0.9f)
	{
		zone = 0.9f;
	}

	if (length <= zone)
	{
		*x = *y = 0.0f;
		return;
	}

	scale = (length - zone) / (1.0 - zone) / length;
	*x = (float)(*x * scale);
	*y = (float)(*y * scale);
}

static int axes_of(XrSession session, int index, float *x, float *y)
{
	XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };

	*x = *y = 0.0f;
	if (actions[index].action == XR_NULL_HANDLE)
	{
		return 0;
	}

	get_info.action = actions[index].action;
	if (XR_FAILED(xrGetActionStateVector2f(session, &get_info, &state)) || !state.isActive)
	{
		return 0;
	}

	*x = state.currentState.x;
	*y = state.currentState.y;
	return 1;
}

static void read_sticks(XrSession session)
{
	float move_x, move_y, turn_x, turn_y;
	float forward, side, turn;

	axes_of(session, ACT_MOVE, &move_x, &move_y);
	axes_of(session, ACT_TURN, &turn_x, &turn_y);
	apply_deadzone(&move_x, &move_y);
	apply_deadzone(&turn_x, &turn_y);

	forward = move_y;
	side = move_x;
	turn = turn_x;

	/* Desk testing: "vr_fake_axes <forward> <side> <turn>" with no controllers. */
	if (vr_fake_axes && vr_fake_axes->string && vr_fake_axes->string[0])
	{
		float fake_forward = 0.0f, fake_side = 0.0f, fake_turn = 0.0f;

		if (sscanf(vr_fake_axes->string, "%f %f %f", &fake_forward, &fake_side, &fake_turn) >= 1)
		{
			forward = fake_forward;
			side = fake_side;
			turn = fake_turn;
		}
	}

	vr_joy_set(forward, side, turn);

	/* Whether the engine is actually reading the stick answers most "why does
	   this do nothing" questions, so it is said either way, once. */
	frames++;
	if (vr_joy_polls() > 0)
	{
		if (!said_polled)
		{
			said_polled = 1;
			vr_printf("VR: the engine is reading the sticks through its joystick support");
		}
	}
	else if (frames > 600 && !said_unpolled)
	{
		said_unpolled = 1;
		vr_printf("VR: the engine is not reading the joystick - the sticks will not move the player");
	}
}

/* Which controllers the runtime thinks are in the player's hands. Said once,
   because it answers "why is this button doing nothing". */
static void say_profile(XrInstance instance, XrSession session)
{
	XrInteractionProfileState state = { XR_TYPE_INTERACTION_PROFILE_STATE };
	XrPath hand = string_to_path(instance, "/user/hand/right");
	char name[XR_MAX_PATH_LENGTH];
	uint32_t length = 0;

	if (hand == XR_NULL_PATH || XR_FAILED(xrGetCurrentInteractionProfile(session, hand, &state)) ||
		state.interactionProfile == XR_NULL_PATH ||
		XR_FAILED(xrPathToString(instance, state.interactionProfile, sizeof(name), &length, name)))
	{
		vr_printf("VR: controllers active");
		return;
	}

	vr_printf("VR: controllers active: %s", name);
}

void vr_input_frame(XrSession session)
{
	XrActiveActionSet active = { XR_NULL_HANDLE, XR_NULL_PATH };
	XrActionsSyncInfo sync_info = { XR_TYPE_ACTIONS_SYNC_INFO };
	XrResult result;

	if (action_set == XR_NULL_HANDLE || !vr_input || vr_input->value == 0.0f)
	{
		return;
	}

	active.actionSet = action_set;
	sync_info.countActiveActionSets = 1;
	sync_info.activeActionSets = &active;
	result = xrSyncActions(session, &sync_info);

	/* Not focused means the runtime is showing something of its own, and the
	   game should not be receiving input at all. */
	if (result == XR_SESSION_NOT_FOCUSED)
	{
		if (!said_unfocused)
		{
			vr_printf("VR: the runtime has the input focus - the controllers are idle until it gives it back");
			said_unfocused = 1;
		}
		release_buttons();
		do_fake_press();
		read_sticks(session);
		return;
	}

	if (XR_FAILED(result))
	{
		return;
	}

	said_unfocused = 0;
	if (!said_active)
	{
		said_active = 1;
		say_profile(xr_instance, session);
	}

	read_buttons(session);
	do_fake_press();
	read_sticks(session);
}

/* Anything held down is let go of, as if the player had released it. A press
   from vr_fake_press is left alone: it releases itself, on its own count. */
static void release_buttons(void)
{
	int i;

	for (i = 0; i < ACT_COUNT; ++i)
	{
		if (i != fake_held && actions[i].kind == KIND_BUTTON && actions[i].down)
		{
			actions[i].down = 0;
			dispatch(&actions[i], 0);
		}
	}
}

void vr_input_release(void)
{
	fake_held = -1;
	fake_frames = 0;
	release_buttons();
	vr_joy_set(0.0f, 0.0f, 0.0f);
}

void vr_input_destroy(void)
{
	int i;

	vr_input_release();

	for (i = 0; i < ACT_COUNT; ++i)
	{
		actions[i].action = XR_NULL_HANDLE;
	}

	if (action_set != XR_NULL_HANDLE)
	{
		xrDestroyActionSet(action_set);
		action_set = XR_NULL_HANDLE;
	}

	said_active = 0;
	said_unfocused = 0;
}
