#include <windows.h>
#include <math.h>

#include "refapi.h"
#include "ref_vr.h"
#include "vr_gesture.h"
#include "vr_holster.h"
#include "vr_input.h"
#include "vr_poses.h"
#include "vr_weapon.h"
#include "vrlog.h"

#define YAW 1
#define LEFT SINVR_HAND_LEFT
#define RIGHT SINVR_HAND_RIGHT

#define DEG_TO_RAD 0.017453292519943295

/* How long a gun hand's grip at a shoulder waits for the other hand to join it
   before it is not the fists, in milliseconds. */
#define BOTH_HANDS_MS 350

/* How long a grip is held in a holster before it is a menu rather than a tap. */
#define MENU_HOLD_MS 300

typedef enum
{
	ZONE_NONE,
	ZONE_SHOULDER_LEFT,
	ZONE_SHOULDER_RIGHT,
	ZONE_HIP_LEFT,
	ZONE_HIP_RIGHT
} zone_t;

static cvar_t *vr_gestures;
static cvar_t *vr_holster_invert; /* 1: letting go in the holster picks, leaving it cancels */

static struct
{
	int valid;
	float origin[3];
	float yaw;
} head;

static zone_t last_zone[2];

/* A grip held in a holster: until it is let go (a tap) or held MENU_HOLD_MS
   (a menu). The shoulders' is the gun hand's grip, the hip's the other's. */
typedef struct
{
	int active;
	int menu;
	zone_t zone;
	DWORD at;
} holding_t;

static holding_t shoulder;
static holding_t hip;

static int gun_taken;      /* the gun hand's grip went to a gesture: so does its release */
static int other_taken;
static int other_at_shoulder;
static DWORD other_pressed_at;
static unsigned done;

void vr_gesture_startup(void)
{
	vr_gestures = vr_cvar("vr_gestures", "1", CVAR_ARCHIVE);
	vr_holster_invert = vr_cvar("vr_holster_invert", "0", CVAR_ARCHIVE);
}

void vr_gesture_head(const float origin[3], const float angles[3])
{
	head.origin[0] = origin[0];
	head.origin[1] = origin[1];
	head.origin[2] = origin[2];
	head.yaw = angles[YAW];
	head.valid = 1;
}

static int enabled(void)
{
	return head.valid && (!vr_gestures || vr_gestures->value != 0.0f);
}

static int gun_hand(void)
{
	return vr_weapon_left_handed() ? LEFT : RIGHT;
}

static int other_hand(void)
{
	return vr_weapon_left_handed() ? RIGHT : LEFT;
}

/*
 * Where a hand is on the body, from the head, in metres: forward the way the
 * head faces, left, and up. Over a shoulder is anywhere from a little below
 * the shoulders to above the head, level with the face or behind it, to that
 * side; a hip is from half a metre to well over a metre below the eyes, close
 * in at that side. Both are generous - a grip has to be pressed there as well.
 */
static zone_t zone_of(int hand)
{
	float at[3], units = vr_worldscale_units();
	float dx, dy, forward, left, up;
	double s, c;

	if (!head.valid || units <= 0.0f || !vr_weapon_hand_at(hand, at))
	{
		return ZONE_NONE;
	}

	s = sin(head.yaw * DEG_TO_RAD);
	c = cos(head.yaw * DEG_TO_RAD);
	dx = at[0] - head.origin[0];
	dy = at[1] - head.origin[1];
	forward = (float)(dx * c + dy * s) / units;
	left = (float)(-dx * s + dy * c) / units;
	up = (at[2] - head.origin[2]) / units;

	if (up > -0.40f && up < 0.30f && forward < 0.05f && forward > -0.50f && fabsf(left) > 0.02f &&
		fabsf(left) < 0.50f)
	{
		return left > 0.0f ? ZONE_SHOULDER_LEFT : ZONE_SHOULDER_RIGHT;
	}

	if (up < -0.50f && up > -1.20f && forward > -0.35f && forward < 0.25f && fabsf(left) > 0.05f &&
		fabsf(left) < 0.50f)
	{
		return left > 0.0f ? ZONE_HIP_LEFT : ZONE_HIP_RIGHT;
	}

	return ZONE_NONE;
}

static int is_shoulder(zone_t zone)
{
	return zone == ZONE_SHOULDER_LEFT || zone == ZONE_SHOULDER_RIGHT;
}

/* The gun hand's own side's hip. */
static int is_gun_hip(zone_t zone)
{
	return zone == (gun_hand() == LEFT ? ZONE_HIP_LEFT : ZONE_HIP_RIGHT);
}

/* And the other hand's. */
static int is_other_hip(zone_t zone)
{
	return zone == (gun_hand() == LEFT ? ZONE_HIP_RIGHT : ZONE_HIP_LEFT);
}

static void act(const char *command, const char *what)
{
	vr_command(command);
	done++;
	vrlog("VR: gesture - %s", what);
}

/* Over the gun hand's own shoulder is the next weapon, over the other one the
   previous: mirrored for a left-handed player, as everything else is. */
static void change_weapon(zone_t zone)
{
	int own = zone == (gun_hand() == LEFT ? ZONE_SHOULDER_LEFT : ZONE_SHOULDER_RIGHT);

	vr_input_haptic(gun_hand(), 0.04f, 0.8f);
	act(own ? "weapnext" : "weapprev", own ? "next weapon, over the gun hand's shoulder" :
		"previous weapon, over the other shoulder");
}

static void fists(void)
{
	shoulder.active = 0;
	vr_input_haptic(LEFT, 0.06f, 1.0f);
	vr_input_haptic(RIGHT, 0.06f, 1.0f);
	act("use Fists", "both hands at the shoulders - the fists");
}

/* Whether letting go of the grip in the holster takes the one picked (and taking
   the hand out of the holster cancels), rather than the other way round. */
static int release_takes(void)
{
	return vr_holster_invert && vr_holster_invert->value != 0.0f;
}

/* Closes the open menu, taking the one picked or not, and says so in the hand. */
static void close_menu(int hand, int take)
{
	vr_holster_close(take);
	vr_input_haptic(hand, take ? 0.06f : 0.03f, take ? 1.0f : 0.4f);
}

static void hold(holding_t *h, zone_t zone)
{
	h->active = 1;
	h->menu = 0;
	h->zone = zone;
	h->at = GetTickCount();
}

int vr_gesture_gun_grip(int down)
{
	zone_t zone;

	if (!down)
	{
		if (shoulder.active)
		{
			/* let go in the holster: a tap changes weapon, a menu closes - as it
			   was, or taking the one picked with vr_holster_invert */
			if (shoulder.menu)
			{
				close_menu(gun_hand(), release_takes());
			}
			else
			{
				change_weapon(shoulder.zone);
			}
			shoulder.active = 0;
		}
		if (gun_taken)
		{
			gun_taken = 0;
			return 1;
		}
		return 0;
	}

	if (!enabled())
	{
		return 0;
	}

	zone = zone_of(gun_hand());
	if (is_shoulder(zone))
	{
		gun_taken = 1;
		if (other_at_shoulder && GetTickCount() - other_pressed_at < BOTH_HANDS_MS)
		{
			fists();
		}
		else
		{
			hold(&shoulder, zone);
		}
		return 1;
	}

	if (is_gun_hip(zone))
	{
		gun_taken = 1;
		vr_input_haptic(gun_hand(), 0.04f, 0.8f);
		act("use Magnum", "the magnum, from the hip");
		return 1;
	}

	return 0;
}

int vr_gesture_other_grip(int down)
{
	if (!down)
	{
		other_at_shoulder = 0;
		if (hip.active)
		{
			/* let go at the hip: a tap uses the item, a menu closes as the
			   shoulder's does */
			if (hip.menu)
			{
				close_menu(other_hand(), release_takes());
			}
			else
			{
				vr_input_haptic(other_hand(), 0.04f, 0.8f);
				act("invuse", "the inventory item, from the other hip");
			}
			hip.active = 0;
		}
		if (other_taken)
		{
			other_taken = 0;
			return 1;
		}
		return 0;
	}

	if (enabled() && is_other_hip(zone_of(other_hand())))
	{
		other_taken = 1;
		hold(&hip, ZONE_NONE);
		return 1;
	}

	if (!enabled() || !is_shoulder(zone_of(other_hand())) || !is_shoulder(zone_of(gun_hand())))
	{
		return 0;
	}

	/* Both hands up: this grip is the fists' half, not running. */
	other_taken = 1;
	other_at_shoulder = 1;
	other_pressed_at = GetTickCount();
	if (shoulder.active && !shoulder.menu && GetTickCount() - shoulder.at < BOTH_HANDS_MS)
	{
		fists();
	}
	return 1;
}

int vr_gesture_trigger(int gun, int down)
{
	if (!(gun ? shoulder.menu : hip.menu))
	{
		return 0;
	}

	if (down)
	{
		vr_holster_next();
		vr_input_haptic(gun ? gun_hand() : other_hand(), 0.02f, 0.5f);
	}
	return 1;
}

/* A grip held long enough in its holster opens that holster's menu; a hand
   that leaves the holster with the menu open takes what it picked - or, with
   vr_holster_invert, cancels it. */
static void run_holster(holding_t *h, int hand, int kind, int in_holster)
{
	if (!h->active)
	{
		return;
	}

	if (!h->menu)
	{
		if (GetTickCount() - h->at < MENU_HOLD_MS)
		{
			return;
		}
		if (vr_holster_open(kind))
		{
			h->menu = 1;
			vr_input_haptic(hand, 0.05f, 0.7f);
			return;
		}
		/* nothing to show: a shoulder is the weapon change it always was */
		if (kind == VR_HOLSTER_WEAPONS)
		{
			change_weapon(h->zone);
			h->active = 0;
		}
		return;
	}

	if (!in_holster)
	{
		close_menu(hand, !release_takes());
		h->active = 0;
		h->menu = 0;
	}
}

void vr_gesture_frame(void)
{
	int hand;

	run_holster(&shoulder, gun_hand(), VR_HOLSTER_WEAPONS, is_shoulder(zone_of(gun_hand())));
	run_holster(&hip, other_hand(), VR_HOLSTER_ITEMS, is_other_hip(zone_of(other_hand())));

	if (!enabled())
	{
		last_zone[LEFT] = last_zone[RIGHT] = ZONE_NONE;
		return;
	}

	/* A light tap as a hand reaches somewhere a grip will do something. */
	for (hand = 0; hand < 2; ++hand)
	{
		zone_t zone = zone_of(hand);
		int counts = is_shoulder(zone) || (hand == gun_hand() && is_gun_hip(zone)) ||
			(hand == other_hand() && is_other_hip(zone));

		if (!counts)
		{
			zone = ZONE_NONE;
		}
		if (zone != ZONE_NONE && zone != last_zone[hand])
		{
			vr_input_haptic(hand, 0.02f, 0.3f);
		}
		last_zone[hand] = zone;
	}
}
