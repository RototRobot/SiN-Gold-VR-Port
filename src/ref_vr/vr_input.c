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
#include "vr_gesture.h"
#include "vr_joy.h"
#include "vr_keys.h"
#include "vr_poses.h"
#include "vr_turn.h"
#include "vr_vignette.h"
#include "vr_view.h"
#include "vr_weapon.h"
#include "vr_xr.h"
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
	ACT_RECENTRE,
	ACT_ADJUST,
	ACT_MOVE,
	ACT_TURN,
	ACT_HAND_LEFT,
	ACT_HAND_RIGHT,
	ACT_GRIP_LEFT,
	ACT_GRIP_RIGHT,
	ACT_HAPTIC_LEFT,
	ACT_HAPTIC_RIGHT,
	ACT_COUNT
};

typedef enum
{
	KIND_BUTTON,
	KIND_AXES,
	KIND_POSE,
	KIND_HAPTIC
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
	{ "menu",        "Menu",            KIND_BUTTON, "key:escape" },
	{ "recentre",    "Recentre",        KIND_BUTTON, "" },
	{ "adjust",      "Adjust weapon",   KIND_BUTTON, "" },
	{ "move",        "Move",            KIND_AXES,   NULL },
	{ "turn",        "Turn",            KIND_AXES,   NULL },
	{ "hand_left",   "Left hand",       KIND_POSE,   NULL },
	{ "hand_right",  "Right hand",      KIND_POSE,   NULL },
	{ "grip_left",   "Left grip",       KIND_POSE,   NULL },
	{ "grip_right",  "Right grip",      KIND_POSE,   NULL },
	{ "haptic_left", "Left buzz",       KIND_HAPTIC, NULL },
	{ "haptic_right", "Right buzz",     KIND_HAPTIC, NULL }
};

typedef struct
{
	int action;
	const char *path;
} binding_t;

/*
 * The Reverb G2, through the HP extension: everything has a home here. SteamVR
 * keeps both menu buttons for its own dashboard on these controllers - a press
 * takes the input focus away and never arrives - so the menu is on the right
 * stick's click as well.
 *
 * Every table is written for a right-handed player. Left-handed, each hand's
 * part is given to the other one as the bindings are suggested (for_hand), the
 * poses excepted: a hand is still the hand it is.
 */
static const binding_t bindings_hp[] = {
	{ ACT_FIRE,        "/user/hand/right/input/trigger/value" },
	{ ACT_USE,         "/user/hand/left/input/trigger/value" },
	{ ACT_JUMP,        "/user/hand/right/input/a/click" },
	{ ACT_CROUCH,      "/user/hand/left/input/x/click" },
	{ ACT_WEAPON_NEXT, "/user/hand/right/input/b/click" },
	{ ACT_WEAPON_PREV, "/user/hand/left/input/y/click" },
	{ ACT_RUN,         "/user/hand/left/input/squeeze/value" },
	{ ACT_MENU,        "/user/hand/left/input/menu/click" },
	{ ACT_MENU,        "/user/hand/right/input/menu/click" },
	{ ACT_MENU,        "/user/hand/right/input/thumbstick/click" },
	{ ACT_RECENTRE,    "/user/hand/left/input/thumbstick/click" },
	{ ACT_ADJUST,      "/user/hand/right/input/squeeze/value" },
	{ ACT_MOVE,        "/user/hand/left/input/thumbstick" },
	{ ACT_TURN,        "/user/hand/right/input/thumbstick" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" },
	{ ACT_GRIP_LEFT,   "/user/hand/left/input/grip/pose" },
	{ ACT_GRIP_RIGHT,  "/user/hand/right/input/grip/pose" },
	{ ACT_HAPTIC_LEFT, "/user/hand/left/output/haptic" },
	{ ACT_HAPTIC_RIGHT, "/user/hand/right/output/haptic" }
};

/* Windows Mixed Reality controllers without that extension: no lettered
   buttons, so the clicks move onto the sticks and pads. */
static const binding_t bindings_wmr[] = {
	{ ACT_FIRE,        "/user/hand/right/input/trigger/value" },
	{ ACT_USE,         "/user/hand/left/input/trigger/value" },
	{ ACT_JUMP,        "/user/hand/right/input/thumbstick/click" },
	{ ACT_CROUCH,      "/user/hand/left/input/trackpad/click" },
	{ ACT_WEAPON_NEXT, "/user/hand/right/input/trackpad/click" },
	{ ACT_WEAPON_PREV, "/user/hand/right/input/squeeze/click" },
	{ ACT_RECENTRE,    "/user/hand/left/input/thumbstick/click" },
	{ ACT_RUN,         "/user/hand/left/input/squeeze/click" },
	{ ACT_MENU,        "/user/hand/left/input/menu/click" },
	{ ACT_MENU,        "/user/hand/right/input/menu/click" },
	{ ACT_MOVE,        "/user/hand/left/input/thumbstick" },
	{ ACT_TURN,        "/user/hand/right/input/thumbstick" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" },
	{ ACT_GRIP_LEFT,   "/user/hand/left/input/grip/pose" },
	{ ACT_GRIP_RIGHT,  "/user/hand/right/input/grip/pose" },
	{ ACT_HAPTIC_LEFT, "/user/hand/left/output/haptic" },
	{ ACT_HAPTIC_RIGHT, "/user/hand/right/output/haptic" }
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
	{ ACT_MENU,        "/user/hand/right/input/thumbstick/click" },
	{ ACT_RECENTRE,    "/user/hand/left/input/thumbstick/click" },
	{ ACT_ADJUST,      "/user/hand/right/input/squeeze/value" },
	{ ACT_MOVE,        "/user/hand/left/input/thumbstick" },
	{ ACT_TURN,        "/user/hand/right/input/thumbstick" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" },
	{ ACT_GRIP_LEFT,   "/user/hand/left/input/grip/pose" },
	{ ACT_GRIP_RIGHT,  "/user/hand/right/input/grip/pose" },
	{ ACT_HAPTIC_LEFT, "/user/hand/left/output/haptic" },
	{ ACT_HAPTIC_RIGHT, "/user/hand/right/output/haptic" }
};

/* The Index has no menu button, so the menu is the right stick's click. */
static const binding_t bindings_index[] = {
	{ ACT_FIRE,        "/user/hand/right/input/trigger/value" },
	{ ACT_USE,         "/user/hand/left/input/trigger/value" },
	{ ACT_JUMP,        "/user/hand/right/input/a/click" },
	{ ACT_CROUCH,      "/user/hand/left/input/a/click" },
	{ ACT_WEAPON_NEXT, "/user/hand/right/input/b/click" },
	{ ACT_WEAPON_PREV, "/user/hand/left/input/b/click" },
	{ ACT_RUN,         "/user/hand/left/input/squeeze/value" },
	{ ACT_MENU,        "/user/hand/right/input/thumbstick/click" },
	{ ACT_RECENTRE,    "/user/hand/left/input/thumbstick/click" },
	{ ACT_ADJUST,      "/user/hand/right/input/squeeze/value" },
	{ ACT_MOVE,        "/user/hand/left/input/thumbstick" },
	{ ACT_TURN,        "/user/hand/right/input/thumbstick" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" },
	{ ACT_GRIP_LEFT,   "/user/hand/left/input/grip/pose" },
	{ ACT_GRIP_RIGHT,  "/user/hand/right/input/grip/pose" },
	{ ACT_HAPTIC_LEFT, "/user/hand/left/output/haptic" },
	{ ACT_HAPTIC_RIGHT, "/user/hand/right/output/haptic" }
};

/* Whatever else a runtime offers: the two buttons every profile has. */
static const binding_t bindings_simple[] = {
	{ ACT_FIRE,        "/user/hand/right/input/select/click" },
	{ ACT_USE,         "/user/hand/left/input/select/click" },
	{ ACT_MENU,        "/user/hand/left/input/menu/click" },
	{ ACT_HAND_LEFT,   "/user/hand/left/input/aim/pose" },
	{ ACT_HAND_RIGHT,  "/user/hand/right/input/aim/pose" },
	{ ACT_GRIP_LEFT,   "/user/hand/left/input/grip/pose" },
	{ ACT_GRIP_RIGHT,  "/user/hand/right/input/grip/pose" },
	{ ACT_HAPTIC_LEFT, "/user/hand/left/output/haptic" },
	{ ACT_HAPTIC_RIGHT, "/user/hand/right/output/haptic" }
};

/*
 * What a left-handed swap has to know about a controller's two halves: the
 * lettered buttons, X and Y on the left and A and B on the right, are not the
 * same buttons under another name on the other hand - asking for a right X
 * gets the whole table refused (XR_ERROR_PATH_UNSUPPORTED), and SteamVR then
 * falls back on a layout of its own, trigger and all. So swapped, X becomes A
 * and Y becomes B, and back. Some controllers have a menu button on the left
 * only; swapped onto the right, that binding is left out.
 */
#define SWAP_LETTERS   0x1
#define MENU_LEFT_ONLY 0x2

typedef struct
{
	const char *profile;
	const binding_t *bindings;
	int count;
	int halves;
} profile_t;

static const profile_t profiles[] = {
	{ "/interaction_profiles/hp/mixed_reality_controller", bindings_hp, ARRAY_COUNT(bindings_hp), SWAP_LETTERS },
	{ "/interaction_profiles/microsoft/motion_controller", bindings_wmr, ARRAY_COUNT(bindings_wmr), 0 },
	{ "/interaction_profiles/oculus/touch_controller", bindings_touch, ARRAY_COUNT(bindings_touch),
		SWAP_LETTERS | MENU_LEFT_ONLY },
	{ "/interaction_profiles/valve/index_controller", bindings_index, ARRAY_COUNT(bindings_index), 0 },
	{ "/interaction_profiles/khr/simple_controller", bindings_simple, ARRAY_COUNT(bindings_simple), 0 }
};

static const char *const key_names[VR_KEY_COUNT] = { "up", "down", "left", "right", "enter", "escape", "space" };

static XrInstance xr_instance;
static XrActionSet action_set;
static XrSession input_session; /* for the buzzes, which are asked for outside a frame's reading */
static cvar_t *vr_input;
static cvar_t *vr_deadzone;
static cvar_t *vr_fake_axes;
static cvar_t *vr_fake_press;
static cvar_t *vr_fake_key;
static cvar_t *vr_fake_hand;
static cvar_t *vr_fake_hand_left;
static cvar_t *vr_fake_idle_press;
static cvar_t *vr_fake_swing;
static DWORD last_fake_idle;
#define FAKE_HOLDS 2
static int fake_held[FAKE_HOLDS] = { -1, -1 }; /* a pretend press, and one more held with it */
static int fake_frames[FAKE_HOLDS];
static int fake_menu[FAKE_HOLDS];
static int started;
static int said_active;
static int said_unfocused;
static int said_polled;
static int said_unpolled;
static unsigned frames;
static cvar_t *vr_recentre_double;
static cvar_t *vr_snap_turn;
static cvar_t *vr_snap_angle;
static cvar_t *vr_haptics;
static cvar_t *vr_move_dir;
static cvar_t *vr_weapon_buttons;
static int said_weapon_buttons;
static int snap_armed = 1;
static int said_no_snap;
static int steadying; /* the other hand's grip button has hold of the weapon */
static XrSpace hand_space[2]; /* where each controller points from, and which way */
static XrSpace grip_space[2]; /* where the hand holds it */
static int said_hands;
static unsigned last_recentre_click;
static int was_in_menu;

/* One per direction, for the key repeat while a stick is held over. */
static struct
{
	int held;
	unsigned repeat_at;
} nav[4];

/* ------------------------------------------------------------------------- */
/* Start-up                                                                   */
/* ------------------------------------------------------------------------- */

void vr_input_register(void)
{
	static int registered;
	char name[64];
	int i;

	if (registered)
	{
		return;
	}
	registered = 1;

	vr_input = vr_cvar("vr_input", "1", CVAR_ARCHIVE);
	vr_deadzone = vr_cvar("vr_deadzone", "0.15", CVAR_ARCHIVE);
	vr_fake_axes = vr_cvar("vr_fake_axes", "", 0);
	vr_fake_press = vr_cvar("vr_fake_press", "", 0);
	vr_fake_key = vr_cvar("vr_fake_key", "", 0);
	vr_fake_hand = vr_cvar("vr_fake_hand", "", 0);
	vr_fake_hand_left = vr_cvar("vr_fake_hand_left", "", 0);
	vr_fake_idle_press = vr_cvar("vr_fake_idle_press", "", 0);
	vr_fake_swing = vr_cvar("vr_fake_swing", "", 0);
	vr_recentre_double = vr_cvar("vr_recentre_double", "1", CVAR_ARCHIVE);
	vr_snap_turn = vr_cvar("vr_snap_turn", "0", CVAR_ARCHIVE);
	vr_snap_angle = vr_cvar("vr_snap_angle", "45", CVAR_ARCHIVE);
	vr_haptics = vr_cvar("vr_haptics", "1", CVAR_ARCHIVE);
	vr_move_dir = vr_cvar("vr_move_dir", "1", CVAR_ARCHIVE);
	vr_weapon_buttons = vr_cvar("vr_weapon_buttons", "0", CVAR_ARCHIVE);

	for (i = 0; i < ACT_COUNT; ++i)
	{
		if (actions[i].kind != KIND_BUTTON)
		{
			continue;
		}

		sprintf(name, "vr_bind_%s", actions[i].name);
		actions[i].bind = vr_cvar(name, actions[i].command, CVAR_ARCHIVE);
	}
}

void vr_input_startup(void)
{
	if (started)
	{
		return;
	}
	started = 1;

	vr_input_register();
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

/*
 * The same binding on the other controller, for a left-handed player: what the
 * right hand did, the left does. The poses keep their own hands, and anything
 * that is not on a hand at all is left alone.
 */
/* The same button on the other hand, or NULL if that hand has none. */
static const char *for_hand(const char *path, action_kind_t kind, int halves, char *buffer, size_t size)
{
	static const char left[] = "/user/hand/left/";
	static const char right[] = "/user/hand/right/";
	static const char *const letters[][2] = { { "input/x/", "input/a/" }, { "input/y/", "input/b/" } };
	const char *head, *tail;
	int i;

	if (kind == KIND_POSE || kind == KIND_HAPTIC || !vr_weapon_left_handed())
	{
		return path;
	}

	if (strncmp(path, left, sizeof(left) - 1) == 0)
	{
		head = right;
		tail = path + sizeof(left) - 1;
	}
	else if (strncmp(path, right, sizeof(right) - 1) == 0)
	{
		head = left;
		tail = path + sizeof(right) - 1;
	}
	else
	{
		return path;
	}

	if (strlen(head) + strlen(tail) >= size)
	{
		return path;
	}

	strcpy(buffer, head);
	strcat(buffer, tail);

	if (halves & SWAP_LETTERS)
	{
		/* left X is right A, left Y right B */
		int to_right = head == right;

		for (i = 0; i < ARRAY_COUNT(letters); ++i)
		{
			const char *from = letters[i][to_right ? 0 : 1];
			const char *to = letters[i][to_right ? 1 : 0];

			if (strncmp(tail, from, strlen(from)) == 0)
			{
				strcpy(buffer, head);
				strcat(buffer, to);
				strcat(buffer, tail + strlen(from));
			}
		}
	}

	if ((halves & MENU_LEFT_ONLY) && strcmp(buffer, "/user/hand/right/input/menu/click") == 0)
	{
		return NULL;
	}
	return buffer;
}

static void suggest_profile(XrInstance instance, const profile_t *profile)
{
	XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	XrActionSuggestedBinding list[32];
	uint32_t count = 0;
	XrResult result;
	int i;

	for (i = 0; i < profile->count && count < ARRAY_COUNT(list); ++i)
	{
		char swapped[96];
		const char *string = for_hand(profile->bindings[i].path,
			actions[profile->bindings[i].action].kind, profile->halves, swapped, sizeof(swapped));
		XrPath path = string ? string_to_path(instance, string) : XR_NULL_PATH;

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
	else
	{
		vrlog("VR: bindings suggested for %s", profile->profile);
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
	input_session = session;
	if (vr_weapon_left_handed())
	{
		vrlog("VR: left-handed - each hand's buttons and stick are given to the other");
	}
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
		case KIND_HAPTIC:
			action_info.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
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

	for (i = 0; i < 4; ++i)
	{
		static const int pose_actions[4] = { ACT_HAND_LEFT, ACT_HAND_RIGHT, ACT_GRIP_LEFT, ACT_GRIP_RIGHT };
		XrActionSpaceCreateInfo space_info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		XrSpace *space = i < 2 ? &hand_space[i] : &grip_space[i - 2];
		int index = pose_actions[i];

		if (actions[index].action == XR_NULL_HANDLE)
		{
			continue;
		}

		space_info.action = actions[index].action;
		space_info.poseInActionSpace.orientation.w = 1.0f;
		if (XR_FAILED(xrCreateActionSpace(session, &space_info, space)))
		{
			vr_printf("VR: could not follow the %s %s", (i & 1) == SINVR_HAND_LEFT ? "left" : "right",
				i < 2 ? "hand" : "grip");
			*space = XR_NULL_HANDLE;
		}
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
static void read_sticks(XrSession session, int menu);
static void do_fake_press(void);
static void do_fake_key(void);
static void press_action(int index, int down, int menu);

/*
 * A press runs the command as typed; a release runs the "-" form of a "+"
 * command, which is what the engine's own key binds do. A binding of
 * "key:<name>" taps that key instead, for the parts of the game that only
 * listen to the keyboard - SiN's menu, which has no command to open it.
 */

static int tap_bound_key(const char *command)
{
	int i;

	if (_strnicmp(command, "key:", 4) != 0)
	{
		return 0;
	}

	for (i = 0; i < VR_KEY_COUNT; ++i)
	{
		if (_stricmp(command + 4, key_names[i]) == 0)
		{
			vr_key_tap((vr_key_t)i);
			return 1;
		}
	}

	vr_printf("VR: \"%s\" names no key", command);
	return 1;
}

static void dispatch(action_t *action, int down)
{
	const char *command = action->bind && action->bind->string ? action->bind->string : action->command;
	char text[128];

	if (!command || !command[0])
	{
		return;
	}

	/* A key is tapped on the press; there is nothing to do on the release. */
	if (_strnicmp(command, "key:", 4) == 0)
	{
		if (down)
		{
			vrlog("    input: %s pressed -> %s", action->name, command);
			tap_bound_key(command);
		}
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
 * Desk testing: "vr_fake_press jump" presses that action, through the same path
 * a controller button takes, and lets go a few frames later; "vr_fake_press
 * adjust 200" holds it for 200 frames, "vr_fake_press adjust 0" until
 * "vr_fake_press release". A new press lets go of the one before, unless it
 * ends "also" - "vr_fake_press run 0 also" - when both are held. The names are
 * the ones in the vr_bind_* cvars.
 */
static int is_fake(int action)
{
	int slot;

	for (slot = 0; slot < FAKE_HOLDS; ++slot)
	{
		if (fake_held[slot] == action)
		{
			return 1;
		}
	}
	return 0;
}

void vr_input_haptic(int hand, float seconds, float amplitude)
{
	XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
	XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
	int index = hand == SINVR_HAND_LEFT ? ACT_HAPTIC_LEFT : ACT_HAPTIC_RIGHT;
	static unsigned haptic_failures;
	XrResult result;

	if (input_session == XR_NULL_HANDLE || actions[index].action == XR_NULL_HANDLE ||
		(vr_haptics && vr_haptics->value == 0.0f))
	{
		return;
	}

	info.action = actions[index].action;
	vibration.duration = (XrDuration)(seconds * 1e9);
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	vibration.amplitude = amplitude;
	result = xrApplyHapticFeedback(input_session, &info, (const XrHapticBaseHeader *)&vibration);
	if (XR_FAILED(result) && haptic_failures++ < 5)
	{
		vrlog("VR: the runtime would not buzz the %s controller (%d)", hand == SINVR_HAND_LEFT ? "left" : "right",
			(int)result);
	}
}

static void release_fake(int slot)
{
	int action = fake_held[slot];

	if (action < 0)
	{
		return;
	}
	fake_held[slot] = -1;
	actions[action].down = 0;
	press_action(action, 0, fake_menu[slot]);
}

static void do_fake_press(void)
{
	char name[32], also[8] = "";
	int i, slot, hold = 12;

	/* Held for a few frames: the engine samples its buttons when it builds a
	   movement command, not when ours arrives. */
	for (slot = 0; slot < FAKE_HOLDS; ++slot)
	{
		if (fake_held[slot] >= 0 && fake_frames[slot] > 0 && --fake_frames[slot] == 0)
		{
			release_fake(slot);
		}
	}

	if (!vr_fake_press || !vr_fake_press->string || !vr_fake_press->string[0] ||
		sscanf(vr_fake_press->string, "%31s %d %7s", name, &hold, also) < 1)
	{
		return;
	}
	vr_cvar_set("vr_fake_press", "");

	/* "release" lets go of them all */
	slot = strcmp(also, "also") == 0 ? 1 : 0;
	for (i = FAKE_HOLDS - 1; i >= slot; --i)
	{
		release_fake(i);
	}
	if (strcmp(name, "release") == 0)
	{
		return;
	}

	for (i = 0; i < ACT_COUNT; ++i)
	{
		if (actions[i].kind == KIND_BUTTON && strcmp(actions[i].name, name) == 0)
		{
			actions[i].down = 1;
			fake_menu[slot] = vr_in_menu();
			press_action(i, 1, fake_menu[slot]);
			fake_held[slot] = i;
			fake_frames[slot] = hold > 0 ? hold : -1;
			return;
		}
	}

	vr_printf("VR: there is no button called \"%s\"", name);
}

/*
 * Clicking the left stick recentres. Twice by default, because a single click is
 * easy to catch by accident while moving; "vr_recentre_double 0" makes one do.
 */
static void recentre_clicked(void)
{
	unsigned now = GetTickCount();
	int twice = !vr_recentre_double || vr_recentre_double->value != 0.0f;

	if (twice && now - last_recentre_click > 400)
	{
		last_recentre_click = now;
		return;
	}

	last_recentre_click = 0;
	vr_xr_request_recenter();
}

/* In a menu, a button is a keystroke: the menus read the keyboard. */
static vr_key_t menu_key_for(int action)
{
	switch (action)
	{
	case ACT_FIRE:
	case ACT_USE:
	case ACT_JUMP:
		return VR_KEY_ENTER;
	default:
		return VR_KEY_ESCAPE;
	}
}

/* Desk testing: "vr_fake_key escape" taps that key, as a menu button would. */
static void do_fake_key(void)
{
	int i;

	if (!vr_fake_key || !vr_fake_key->string || !vr_fake_key->string[0])
	{
		return;
	}

	for (i = 0; i < VR_KEY_COUNT; ++i)
	{
		if (_stricmp(vr_fake_key->string, key_names[i]) == 0)
		{
			vr_key_tap((vr_key_t)i);
			break;
		}
	}

	if (i == VR_KEY_COUNT)
	{
		vr_printf("VR: there is no key called \"%s\"", vr_fake_key->string);
	}

	vr_cvar_set("vr_fake_key", "");
}

/*
 * Whatever a button was doing stops - the mode changed, or the focus went - but
 * it stays held as far as this is concerned, so that a button still under the
 * player's thumb is not taken as a fresh press in the new mode. Opening the
 * menu with a button and holding it down otherwise reads as press, open,
 * release, press, close, press, open, once a frame, which is how one click of
 * the stick came out as a flurry of escapes.
 */
static void stop_holding(int index)
{
	if (index == ACT_ADJUST)
	{
		vrlog("    input: adjust is no longer held");
		vr_gesture_gun_grip(0);
		vr_weapon_adjust_button(0);
		return;
	}
	if (index == ACT_RUN && vr_gesture_other_grip(0))
	{
		return;
	}
	if (index == ACT_RUN && steadying)
	{
		steadying = 0;
		vr_weapon_steady(0);
		return;
	}
	dispatch(&actions[index], 0);
}

/* The button really is up - its controller has gone, or the player let go. */
static void let_go(int index)
{
	actions[index].down = 0;
	stop_holding(index);
}

static void press_action(int index, int down, int menu)
{
	/* The top buttons cycle weapons only if vr_weapon_buttons says so: the
	   shoulders do it now, and the buttons are free for something else. */
	if (!menu && (index == ACT_WEAPON_NEXT || index == ACT_WEAPON_PREV) &&
		vr_weapon_buttons && vr_weapon_buttons->value == 0.0f)
	{
		if (down && !said_weapon_buttons)
		{
			said_weapon_buttons = 1;
			vr_printf("VR: the weapon buttons are off - reach over a shoulder to change weapon, or set "
				"vr_weapon_buttons 1");
		}
		return;
	}

	/* Over a shoulder or at the hip, the gun hand's grip is a gesture's. */
	if (index == ACT_ADJUST && !menu && vr_gesture_gun_grip(down))
	{
		vrlog("    input: adjust %s -> a gesture", down ? "pressed" : "let go");
		return;
	}

	/* Held, it lets go of the weapon so the hand can take it another way. */
	if (index == ACT_ADJUST)
	{
		vrlog("    input: adjust %s", down ? (menu ? "pressed, but an interface has the controllers" :
			"held") : "let go");
		vr_weapon_adjust_button(down && !menu);
		return;
	}

	if (index == ACT_RECENTRE)
	{
		if (down)
		{
			vrlog("    input: recentre clicked");
			recentre_clicked();
		}
		return;
	}

	/* The other hand's grip: at the weapon's fore-end it takes hold of the
	   weapon, and everywhere else it does what it is bound to. The release
	   goes wherever the press went. */
	if (index == ACT_RUN && !menu)
	{
		if (vr_gesture_other_grip(down))
		{
			vrlog("    input: %s %s -> a gesture", actions[index].name, down ? "pressed" : "let go");
			return;
		}
		if (down && vr_weapon_steady(1))
		{
			vrlog("    input: %s pressed at the fore-end -> both hands", actions[index].name);
			steadying = 1;
			return;
		}
		if (!down && steadying)
		{
			vrlog("    input: %s let go -> one hand", actions[index].name);
			steadying = 0;
			vr_weapon_steady(0);
			return;
		}
	}

	if (menu)
	{
		/* Nothing is held in an interface: a press is a tap of a key. */
		if (down)
		{
			vrlog("    input: %s pressed -> key %s (in an interface)", actions[index].name,
				key_names[menu_key_for(index)]);
			vr_key_tap(menu_key_for(index));
		}
		return;
	}

	dispatch(&actions[index], down);
}

static void read_buttons(XrSession session, int menu)
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
			/* The controller it is bound to has gone. */
			if (actions[i].down && !is_fake(i))
			{
				let_go(i);
			}
			continue;
		}

		down = state.currentState ? 1 : 0;
		if (down == actions[i].down)
		{
			continue;
		}
		actions[i].down = down;
		press_action(i, down, menu);
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

/* Over the edge in one direction, with a wait before it starts repeating. */
static void nav_direction(int index, int over, vr_key_t key, unsigned now)
{
	if (!over)
	{
		nav[index].held = 0;
		return;
	}

	if (!nav[index].held)
	{
		nav[index].held = 1;
		nav[index].repeat_at = now + 400;
		vr_key_tap(key);
		return;
	}

	if (now >= nav[index].repeat_at)
	{
		nav[index].repeat_at = now + 150;
		vr_key_tap(key);
	}
}

static void navigate_menu(float forward, float side)
{
	unsigned now = GetTickCount();

	nav_direction(0, forward > 0.5f, VR_KEY_UP, now);
	nav_direction(1, forward < -0.5f, VR_KEY_DOWN, now);
	nav_direction(2, side < -0.5f, VR_KEY_LEFT, now);
	nav_direction(3, side > 0.5f, VR_KEY_RIGHT, now);
}

/*
 * Snap turning (vr_snap_turn 1): pushing the turning stick well over turns the
 * player vr_snap_angle degrees at once, and it has to come back near the
 * middle before it turns again - one push, one turn. The joystick then turns
 * nothing. Smooth turning is the engine's own, from the stick as it is.
 * Returns what the joystick should turn by.
 */
static float snap_turn(float turn)
{
	float angle = vr_snap_angle ? vr_snap_angle->value : 45.0f;

	if (!vr_snap_turn || vr_snap_turn->value == 0.0f)
	{
		snap_armed = 1;
		return turn;
	}

	if (!vr_turn_available())
	{
		if (!said_no_snap)
		{
			said_no_snap = 1;
			vr_printf("VR: snap turning cannot reach this sin.exe - the stick turns smoothly");
		}
		return turn;
	}

	if (angle < 1.0f)
	{
		angle = 1.0f;
	}
	else if (angle > 180.0f)
	{
		angle = 180.0f;
	}

	if (fabsf(turn) < 0.3f)
	{
		snap_armed = 1;
	}
	else if (snap_armed && fabsf(turn) > 0.7f)
	{
		/* the stick to the right turns right, which is the yaw going down */
		snap_armed = 0;
		vr_turn_by(turn > 0.0f ? -angle : angle);
		vrlog("    input: snap turn %s by %.0f", turn > 0.0f ? "right" : "left", angle);
	}

	return 0.0f;
}

/*
 * Which way the stick's forward is, against the body's facing: the head's
 * (vr_move_dir 1), the moving hand's controller's (2), or the body's own (0) -
 * which is the way the player faced when they last recentred. A pose that is
 * not tracked this frame keeps the last one it had.
 */
static float move_yaw(void)
{
	static float head_yaw, hand_yaw;
	static int said_mode = -1;
	const sinvr_poses_t *p = vr_poses_current();
	int mode = vr_move_dir ? (int)vr_move_dir->value : 1;
	int hand = vr_weapon_left_handed() ? SINVR_HAND_RIGHT : SINVR_HAND_LEFT;

	if (p->head_valid)
	{
		head_yaw = p->head_angles[1];
	}
	if (p->hand_valid[hand])
	{
		hand_yaw = p->hand_angles[hand][1];
	}

	if (mode != said_mode)
	{
		said_mode = mode;
		vrlog("VR: the stick's forward is the %s, %.0f degrees from the body's", mode == 2 ? "moving hand's" :
			(mode == 1 ? "head's" : "body's own"), mode == 2 ? hand_yaw : (mode == 1 ? head_yaw : 0.0f));
	}

	return mode == 2 ? hand_yaw : (mode == 1 ? head_yaw : 0.0f);
}

static void read_sticks(XrSession session, int menu)
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

	/* Desk testing: "vr_fake_axes <forward> <side> <turn> [<turning stick up>]"
	   with no controllers. */
	if (vr_fake_axes && vr_fake_axes->string && vr_fake_axes->string[0])
	{
		float fake_forward = 0.0f, fake_side = 0.0f, fake_turn = 0.0f, fake_up = 0.0f;

		if (sscanf(vr_fake_axes->string, "%f %f %f %f", &fake_forward, &fake_side, &fake_turn, &fake_up) >= 1)
		{
			forward = fake_forward;
			side = fake_side;
			turn = fake_turn;
			turn_y = fake_up;
		}
	}

	if (menu)
	{
		/* The player is not moving: the sticks are working the menu. */
		vr_vignette_motion(0.0f);
		vr_joy_set(0.0f, 0.0f, 0.0f);
		vr_weapon_adjust_stick(0.0f);
		navigate_menu(forward, side);
		return;
	}

	/* While the weapon is being adjusted, the turning stick sizes it instead. */
	vr_weapon_adjust_stick(turn_y);
	if (vr_weapon_adjusting())
	{
		turn = 0.0f;
	}

	/* Forward is the way the head - or the hand - points: the stick is turned by
	   that yaw (positive to the left) away from the body's. */
	{
		double yaw = move_yaw() * 0.017453292519943295;
		float f = forward, s = side;

		forward = (float)(f * cos(yaw) + s * sin(yaw));
		side = (float)(s * cos(yaw) - f * sin(yaw));
	}

	turn = snap_turn(turn);
	vr_joy_set(forward, side, turn);

	/* The comfort vignette closes in as far as the stick moves the player, or
	   smoothly turns them; a snap turn gives nothing to feel. */
	{
		float moving = sqrtf(forward * forward + side * side);

		vr_vignette_motion(moving > fabsf(turn) ? moving : fabsf(turn));
	}

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
	int menu;

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
		do_fake_key();
		read_sticks(session, vr_in_menu());
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

	menu = vr_in_menu();
	if (menu != was_in_menu)
	{
		/* Whatever was held belongs to the mode that has just ended. */
		release_buttons();
		memset(nav, 0, sizeof(nav));
		was_in_menu = menu;
	}

	read_buttons(session, menu);
	do_fake_press();
	do_fake_key();
	read_sticks(session, menu);
}

void vr_input_idle(XrSession session)
{
	XrActiveActionSet active = { XR_NULL_HANDLE, XR_NULL_PATH };
	XrActionsSyncInfo sync_info = { XR_TYPE_ACTIONS_SYNC_INFO };
	int i;

	if (action_set == XR_NULL_HANDLE || !vr_input || vr_input->value == 0.0f)
	{
		return;
	}

	vr_joy_set(0.0f, 0.0f, 0.0f);
	vr_vignette_motion(0.0f);

	/* Desk testing: "vr_fake_idle_press 1" presses a button once a second for
	   as long as the engine is not drawing - an impatient player at the card. */
	if (vr_fake_idle_press && vr_fake_idle_press->value != 0.0f && GetTickCount() - last_fake_idle >= 1000)
	{
		last_fake_idle = GetTickCount();
		vrlog("    input: a pretend button while the engine is not drawing -> key space");
		vr_key_tap(VR_KEY_SPACE);
	}

	active.actionSet = action_set;
	sync_info.countActiveActionSets = 1;
	sync_info.activeActionSets = &active;
	if (xrSyncActions(session, &sync_info) != XR_SUCCESS)
	{
		return;
	}

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
		if (XR_FAILED(xrGetActionStateBoolean(session, &get_info, &state)))
		{
			continue;
		}

		down = state.isActive && state.currentState;

		/* Held since the game was running: let the game have its release. A
		   pretend press is left alone, as everywhere else - it lets go on its
		   own count, and the idle pump runs between frames, in the middle of
		   whatever a test is doing. */
		if (actions[i].down && !down && !is_fake(i))
		{
			let_go(i);
			continue;
		}

		if (down && !actions[i].down)
		{
			actions[i].down = 1;
			vrlog("    input: %s pressed while the engine is not drawing -> key space", actions[i].name);
			vr_key_tap(VR_KEY_SPACE);
		}
	}
}

/*
 * Anything a held button was doing stops, without the button counting as
 * released: nothing of the old mode is left running, and nothing is pressed
 * afresh until the player's thumb actually comes off. A press from
 * vr_fake_press is left alone: it releases itself, on its own count.
 */
static void release_buttons(void)
{
	int i;

	for (i = 0; i < ACT_COUNT; ++i)
	{
		if (!is_fake(i) && actions[i].kind == KIND_BUTTON && actions[i].down)
		{
			stop_holding(i);
		}
	}
}

/* A space's pose, in the recentred frame. Both bits are needed: a hand with no
   position is no use for aiming. */
static int locate(XrSpace hand, XrSpace space, XrTime time, float units_per_metre, float origin[3],
	float angles[3])
{
	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	float position[3], orientation[4];

	if (hand == XR_NULL_HANDLE || XR_FAILED(xrLocateSpace(hand, space, time, &location)) ||
		!(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
		!(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
	{
		return 0;
	}

	position[0] = location.pose.position.x;
	position[1] = location.pose.position.y;
	position[2] = location.pose.position.z;
	orientation[0] = location.pose.orientation.x;
	orientation[1] = location.pose.orientation.y;
	orientation[2] = location.pose.orientation.z;
	orientation[3] = location.pose.orientation.w;

	vr_view_local(position, orientation, units_per_metre, origin, angles);
	return 1;
}

/*
 * Desk testing: "vr_fake_swing <left|right> <x> <y> <z> <frames>" takes that
 * fake hand out by (x y z) game units over that many rendered frames, speeding
 * up and then slowing down, holds it there as long, and brings it back three
 * times as slowly - a punch, at whatever speed the frame rate makes of it.
 */
static struct
{
	int hand;
	float reach[3];
	int frames;
	int step;
} fake_swing;

static void read_fake_swing(void)
{
	char which[16];
	float reach[3];
	int frames;

	if (!vr_fake_swing || !vr_fake_swing->string || !vr_fake_swing->string[0])
	{
		return;
	}

	if (sscanf(vr_fake_swing->string, "%15s %f %f %f %d", which, &reach[0], &reach[1], &reach[2],
			&frames) == 5 && frames > 0)
	{
		fake_swing.hand = _stricmp(which, "left") == 0 ? SINVR_HAND_LEFT : SINVR_HAND_RIGHT;
		memcpy(fake_swing.reach, reach, sizeof(reach));
		fake_swing.frames = frames;
		fake_swing.step = 0;
	}
	else
	{
		vr_printf("VR: vr_fake_swing wants <left|right> <x> <y> <z> <frames>");
	}
	vr_cvar_set("vr_fake_swing", "");
}

static void fake_swing_offset(int hand, float offset[3])
{
	int n = fake_swing.frames, s = fake_swing.step;
	float f = 0.0f;
	int i;

	if (n > 0 && hand == fake_swing.hand)
	{
		if (s <= n)
		{
			/* eased in and out, as an arm moves: fastest halfway */
			float t = (float)s / n;

			f = t * t * (3.0f - 2.0f * t);
		}
		else if (s <= 2 * n)
		{
			f = 1.0f;
		}
		else if (s <= 5 * n)
		{
			f = 1.0f - (float)(s - 2 * n) / (3 * n);
		}
	}

	for (i = 0; i < 3; ++i)
	{
		offset[i] = fake_swing.reach[i] * f;
	}
}

/* Desk testing, with no controllers: "vr_fake_hand <x> <y> <z> <pitch> <yaw>
   <roll>" puts the right hand there, in the game's own units and senses, held
   where it points from; vr_fake_hand_left does the same for the left. Either
   works on its own, which is what a left-handed test needs. */
static int fake_hand(const cvar_t *cvar, int hand)
{
	float origin[3] = { 0.0f, 0.0f, 0.0f };
	float angles[3] = { 0.0f, 0.0f, 0.0f };
	float offset[3];
	int i;

	if (!cvar || !cvar->string || !cvar->string[0] ||
		sscanf(cvar->string, "%f %f %f %f %f %f", &origin[0], &origin[1], &origin[2],
			&angles[0], &angles[1], &angles[2]) < 3)
	{
		return 0;
	}

	fake_swing_offset(hand, offset);
	for (i = 0; i < 3; ++i)
	{
		origin[i] += offset[i];
	}

	vr_poses_hand(hand, origin, angles);
	vr_weapon_track_hand(hand, origin, origin, angles);
	return 1;
}

void vr_input_hands(XrSession session, XrSpace space, XrTime time, float units_per_metre)
{
	int i;

	(void)session;
	vr_weapon_hands_begin((double)time * 1e-9);

	read_fake_swing();
	if (fake_swing.frames > 0 && ++fake_swing.step > 5 * fake_swing.frames)
	{
		fake_swing.frames = 0;
	}

	if (fake_hand(vr_fake_hand, SINVR_HAND_RIGHT) | fake_hand(vr_fake_hand_left, SINVR_HAND_LEFT))
	{
		return;
	}

	for (i = 0; i < 2; ++i)
	{
		float origin[3], angles[3], grip[3], grip_angles[3];

		if (!locate(hand_space[i], space, time, units_per_metre, origin, angles))
		{
			continue;
		}

		vr_poses_hand(i, origin, angles);

		/* The weapon goes where the hand holds the controller, pointing the way
		   the controller points; without the grip, where it points from. */
		if (!locate(grip_space[i], space, time, units_per_metre, grip, grip_angles))
		{
			memcpy(grip, origin, sizeof(grip));
		}
		vr_weapon_track_hand(i, grip, origin, angles);

		if (!said_hands && i == SINVR_HAND_RIGHT)
		{
			said_hands = 1;
			vr_printf("VR: hands tracked; right hand at (%.1f %.1f %.1f) units, held at (%.1f %.1f %.1f), "
				"pointing (%.1f %.1f %.1f)", origin[0], origin[1], origin[2], grip[0], grip[1], grip[2],
				angles[0], angles[1], angles[2]);
		}
	}
}

void vr_input_release(void)
{
	int slot;

	for (slot = 0; slot < FAKE_HOLDS; ++slot)
	{
		fake_held[slot] = -1;
		fake_frames[slot] = 0;
	}
	release_buttons();
	vr_joy_set(0.0f, 0.0f, 0.0f);
	vr_vignette_motion(0.0f);
}

void vr_input_destroy(void)
{
	input_session = XR_NULL_HANDLE;
	int i;

	vr_input_release();

	for (i = 0; i < 2; ++i)
	{
		if (hand_space[i] != XR_NULL_HANDLE)
		{
			xrDestroySpace(hand_space[i]);
			hand_space[i] = XR_NULL_HANDLE;
		}
		if (grip_space[i] != XR_NULL_HANDLE)
		{
			xrDestroySpace(grip_space[i]);
			grip_space[i] = XR_NULL_HANDLE;
		}
	}
	said_hands = 0;

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
