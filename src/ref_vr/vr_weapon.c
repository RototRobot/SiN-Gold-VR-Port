#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "refapi.h"
#include "ref_vr.h"
#include "vr_gl.h"
#include "vr_hook.h"
#include "vr_input.h"
#include "vr_math.h"
#include "vr_poses.h"
#include "vr_weapon.h"
#include "vrlog.h"

#define PITCH 0
#define YAW 1
#define ROLL 2

#define LEFT SINVR_HAND_LEFT
#define RIGHT SINVR_HAND_RIGHT

#define ADJUST_HOLD_MS 800 /* how long the grip is held before the weapon lets go */
#define SCALE_MIN 0.05f
#define SCALE_MAX 3.0f
#define STEADY_REACH 0.25f /* metres: how near the fore-end the other hand takes hold */
#define STEADY_SHORTEST 0.05f /* metres: hands nearer than this give no direction */

/* ------------------------------------------------------------------------- */
/* The weapons                                                                */
/* ------------------------------------------------------------------------- */

/*
 * First guesses, measured off the models (docs/ENGINE_NOTES.md): the point the
 * right hand holds, in the model's own units, and the size that makes the hand
 * on it about the size of a real one. Most two-handed guns only model the left
 * hand, on the fore-end, so their grip is where the missing right hand would
 * be, under the back of the receiver. The fists have both arms, one each side
 * of the model's middle, and the left fist is the right one's mirrored.
 *
 * fore is how far ahead of the grip, along the barrel, the other hand steadies
 * the weapon, in metres - the palm-to-palm distance on a real rifle - and 0
 * for the weapons held in one hand. A line along the barrel from the hand is
 * what makes the weapon point where the two hands do; the player can move it
 * in the headset.
 *
 * arm is the left arm and hand SiN built into the model - sleeve and glove,
 * reaching in from the side to the fore-end - as runs of the model's vertices,
 * first and last. They are all one mesh with the weapon, so they are left out
 * as ref_gl draws them (vr_gl.c); the player's own other hand is drawn instead.
 * Found by splitting each model into its connected pieces and picking the ones
 * in the sleeve's and glove's colours, each checked by eye (docs/ENGINE_NOTES.md).
 * The magnum's and the mines' modelled hand is the gun hand, and stays.
 *
 * hand, where there is one, is where that modelled hand held the weapon: the
 * middle of its glove on the first frame of the idle the muzzle is measured
 * from, in the model's units. It is the first guess at the fore-end, in place
 * of fore - the other hand goes where the model had it, which on the rocket
 * launcher is the handle out on its left side. The chaingun's modelled hand
 * floats clear of the gun, so its hand is the outside of the loop handle round
 * its left side instead (vertices 310-357, those more than 12 units left of
 * the body's middle).
 *
 * middle is the model's own middle across: halfway between the two edges of
 * its upward-facing surfaces, which were modelled all the way across - where
 * that agrees with the middle of its body within half a unit. The barrel
 * markers are off by up to 0.75 units, and a mirror that far out opens a gap
 * of twice that down the join, which the owner saw on the magnum. The quantum
 * destabilizer, rocket launcher and chaingun, whose tops carry parts on one
 * side only, use the middle of the body. The missing right side is rebuilt
 * across it, as the left side reflected
 * (vr_gl_fill_model_at). fill says whether the weapon is near enough
 * symmetrical for that - the chaingun, with a box on its left side only, is
 * not - and vr_fill_<weapon> can say otherwise; keep is a hand on the weapon
 * that is not copied: the magnum's own gun hand, and the pulse rifle's sleeve
 * and glove lying along its top. A middle of 0 leaves a model as it is: the
 * fists are whole, and the mines and detonator are mostly hand.
 */
typedef struct
{
	const char *name; /* models/view_<name>.def */
	float grip[3];
	float scale;
	int two_arms;
	float fore;
	int arm_count;
	int arm[2][2];
	int has_hand;
	float hand[3];
	float middle;
	int fill;
	int keep_count;
	int keep[1][2];
} weapon_info_t;

static const weapon_info_t weapon_table[] = {
	{ "punch",     { 18.5f, -11.0f, -10.0f }, 0.50f, 1, 0.0f },
	{ "magnum",    { 53.0f, -36.0f, -31.0f }, 0.35f, 0, 0.0f, 0, { { 0 } }, 0, { 0 }, -36.4f, 1, 1, { { 8, 116 } } },
	{ "shotgun",   {  8.0f, -14.0f, -15.0f }, 0.60f, 0, 0.30f, 1, { { 30, 95 } }, 1, { 27.6f, -13.3f, -16.4f }, -14.85f, 1 },
	{ "asrifle",   { 37.0f, -21.0f, -30.0f }, 0.35f, 0, 0.30f, 1, { { 276, 308 } }, 1, { 79.3f, -18.5f, -39.4f }, -22.25f, 1 },
	{ "hvgun",     { 30.0f, -25.0f, -38.0f }, 0.35f, 0, 0.30f, 2, { { 200, 309 }, { 358, 405 } }, 1, { 36.9f, -9.0f, -36.6f }, -24.1f, 0 },
	{ "rlaunch",   { 28.0f, -24.0f, -36.0f }, 0.35f, 0, 0.30f, 1, { { 146, 218 } }, 1, { 49.8f, -6.3f, -33.3f }, -23.5f, 1 },
	{ "srifle",    { 15.0f, -13.0f, -14.0f }, 0.35f, 0, 0.35f, 1, { { 249, 306 } }, 1, { 30.5f, -12.7f, -13.3f }, -15.9f, 1 },
	{ "pulse2",    { 25.0f, -18.0f, -30.0f }, 0.35f, 0, 0.30f, 1, { { 358, 393 } }, 1, { 49.0f, -19.0f, -26.0f }, -23.35f, 1, 1, { { 44, 147 } } },
	{ "quantum",   { 20.0f, -30.0f, -40.0f }, 0.30f, 0, 0.35f, 0, { { 0 } }, 0, { 0 }, -35.1f, 1 },
	{ "spgun",     { 30.0f, -15.0f, -30.0f }, 0.35f, 0, 0.30f, 1, { { 217, 259 } }, 1, { 60.1f, -21.3f, -35.8f }, -24.25f, 1 },
	{ "mines",     { 36.0f, -22.0f, -18.0f }, 0.35f, 0, 0.0f },
	{ "detonator", { 15.0f,  -5.0f, -12.0f }, 0.40f, 0, 0.0f }
};

static const weapon_info_t weapon_other = { NULL, { 30.0f, -20.0f, -25.0f }, 0.35f, 0, 0.30f };

/*
 * The other hand is drawn with the fists' model, the arm for that hand only,
 * held still on the first frame of its idle - a model's animations are
 * numbered in alphabetical order of their names, not the order the .def gives
 * them, so view_punch.def's fire1-fire5 are 0-4 and idle1 is 5 (the fists came
 * up as 9, ready1, and the magnum's ready, putaway and reload as 4, 3 and 5,
 * which only that order explains) - and placed the way
 * vr_grip_punch or vr_grip_punch_left has it, so a fist settled for punching
 * looks the same holding a rifle.
 */
/*
 * The idle animations, held still. A weapon in a tracked hand already moves
 * with the hand, and one that breathes and sways besides drifts against it -
 * and the side rebuilt as a reflection (vr_gl_fill_model_at) sways the other
 * way. The game's own hold (vr_freeze_idle, in the game module) waits for the
 * next idle to start and lets some through, so the picture is held here
 * instead: whatever idle the weapon is playing, it is drawn on the first frame
 * of that mode's main idle - the one the muzzle is measured from - and the
 * silenced and second-mode idles on their own first frame. Animations are
 * numbered alphabetically by name (docs/ENGINE_NOTES.md); the pairs are
 * from, to.
 */
/*
 * steady is the animations - one bit each - in which the weapon only tips up
 * and down about its own middle, which is what a reflection across that
 * middle can follow: the idles and the firing. Raising and lowering it,
 * reloading and changing mode turn and roll it as well, and a reflected side
 * goes the opposite way and pulls apart from the gun - the owner saw it as
 * the two halves joining up on every switch - so the side is not rebuilt
 * during those.
 */
typedef struct
{
	const char *name; /* models/view_<name>.def */
	int count;
	int map[6][2];
	unsigned steady;
} idle_info_t;

static const idle_info_t idle_table[] = {
	{ "punch",       3, { { 5, 5 }, { 6, 5 }, { 7, 5 } }, 0x00FF },
	{ "magnum",      4, { { 1, 1 }, { 2, 1 }, { 7, 7 }, { 8, 7 } }, 0x01C7 },
	{ "shotgun",     2, { { 1, 1 }, { 2, 1 } }, 0x0007 },
	{ "asrifle",     6, { { 2, 2 }, { 3, 2 }, { 4, 2 }, { 9, 9 }, { 10, 9 }, { 11, 9 } }, 0x0F1E },
	{ "hvgun",       3, { { 2, 2 }, { 3, 2 }, { 9, 9 } }, 0x030E },
	{ "rlaunch",     2, { { 1, 1 }, { 3, 3 } }, 0x004F },
	{ "srifle",      1, { { 1, 1 } }, 0x0003 },
	{ "pulse2",      2, { { 2, 2 }, { 8, 8 } }, 0x0186 },
	{ "quantum",     1, { { 1, 1 } }, 0x0013 },
	{ "spgun",       4, { { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 } }, 0x001F },
	{ "mines",       2, { { 1, 1 }, { 2, 1 } }, 0x0007 },
	{ "detonator",   2, { { 1, 1 }, { 2, 1 } }, 0x0007 },
	{ "mutanthands", 1, { { 4, 4 } }, 0x001F }
};

static const idle_info_t *idle_info(const char *key)
{
	int i;

	for (i = 0; key && i < (int)(sizeof(idle_table) / sizeof(idle_table[0])); ++i)
	{
		if (strcmp(idle_table[i].name, key) == 0)
		{
			return &idle_table[i];
		}
	}
	return NULL;
}

#define FIST_MODEL "models/view_punch.def"
#define FIST_IDLE_ANIM 5
#define NEW_ENTITY_ID 0x1E61 /* V_AddEntity gives an entity with this id a fresh one of its own */

static void *fist_model;
static int said_other_hand;
static float fist_origin[3];   /* where the other hand's fist was put this frame */
static int fist_placed;

typedef struct
{
	char key[40];                /* "asrifle", from models/view_asrifle.def */
	const weapon_info_t *info;
	cvar_t *grip[2];             /* vr_grip_<key>, and vr_grip_<key>_left for two arms */
	cvar_t *fore;                /* vr_fore_<key>: the fore-end, in the model's own units */
	cvar_t *fill;                /* vr_fill_<key>: whether its missing side is rebuilt */
} weapon_t;

static weapon_t weapons[48];
static int weapon_count;
static weapon_t *current;        /* the view weapon last seen */

static cvar_t *vr_weapon_hand;
static cvar_t *vr_left_handed;
static cvar_t *vr_weapon_client;
static cvar_t *vr_client_bullets;
static cvar_t *vr_weapon_reset;
static cvar_t *vr_show_hands;
static cvar_t *vr_freeze_idle;
static cvar_t *vr_punch;
static cvar_t *vr_punch_speed;
static cvar_t *vr_two_hand;
static cvar_t *vr_other_hand;
static cvar_t *vr_weapon_arm;
static cvar_t *vr_weapon_fill;
static cvar_t *vr_weapon_idle;
static cvar_t *vr_upper_arm;
static cvar_t *vr_aim;

/* Read once, at start-up: the buttons are attached to the session with the
   hands already swapped, and a session takes them only once. */
static int lefty;
static int said_lefty_change;

int vr_weapon_left_handed(void)
{
	return lefty;
}

/* The hand the weapon is in, and the other one. */
static int gun_hand(void)
{
	return lefty ? LEFT : RIGHT;
}

static int other_hand(void)
{
	return lefty ? RIGHT : LEFT;
}

static const char *hand_name(int hand)
{
	return hand == LEFT ? "left" : "right";
}

/* Says once that the setting has been changed under a running session. */
static void check_mode(void)
{
	int wanted = vr_left_handed && vr_left_handed->value != 0.0f;

	if (wanted != lefty && !said_lefty_change)
	{
		said_lefty_change = 1;
		vr_printf("VR: vr_left_handed is now %d - the weapon and the buttons swap over together, "
			"at the next vid_restart", wanted);
	}
}

static weapon_t *find_weapon(const char *key)
{
	char name[64];
	int i;

	for (i = 0; i < weapon_count; ++i)
	{
		if (strcmp(weapons[i].key, key) == 0)
		{
			return &weapons[i];
		}
	}

	if (weapon_count >= (int)(sizeof(weapons) / sizeof(weapons[0])))
	{
		return NULL;
	}

	weapons[weapon_count].info = &weapon_other;
	for (i = 0; i < (int)(sizeof(weapon_table) / sizeof(weapon_table[0])); ++i)
	{
		if (strcmp(weapon_table[i].name, key) == 0)
		{
			weapons[weapon_count].info = &weapon_table[i];
		}
	}

	strncpy(weapons[weapon_count].key, key, sizeof(weapons[weapon_count].key) - 1);
	sprintf(name, "vr_grip_%.40s", key);
	weapons[weapon_count].grip[RIGHT] = vr_cvar(name, "", CVAR_ARCHIVE);
	if (weapons[weapon_count].info->two_arms)
	{
		sprintf(name, "vr_grip_%.40s_left", key);
		weapons[weapon_count].grip[LEFT] = vr_cvar(name, "", CVAR_ARCHIVE);
	}
	else if (weapons[weapon_count].info->fore > 0.0f)
	{
		sprintf(name, "vr_fore_%.40s", key);
		weapons[weapon_count].fore = vr_cvar(name, "", CVAR_ARCHIVE);
	}
	if (weapons[weapon_count].info->middle != 0.0f)
	{
		sprintf(name, "vr_fill_%.40s", key);
		weapons[weapon_count].fill = vr_cvar(name, weapons[weapon_count].info->fill ? "1" : "0", CVAR_ARCHIVE);
	}

	return &weapons[weapon_count++];
}

/* "models/view_asrifle.def" -> "asrifle". ref_gl's model_t begins with its
   name, which is checked before it is believed. */
static int weapon_key(const entity_t *e, char *key, size_t size)
{
	const char *name = (const char *)e->model;
	const char *start, *end;
	size_t i;

	if (!name)
	{
		return 0;
	}

	for (i = 0; i < 64 && name[i]; ++i)
	{
		if (name[i] < 32 || name[i] > 126)
		{
			return 0;
		}
	}
	if (i == 0 || i == 64 || _strnicmp(name, "models/", 7) != 0)
	{
		return 0;
	}

	start = name + 7;
	if (_strnicmp(start, "view_", 5) == 0)
	{
		start += 5;
	}
	end = strrchr(start, '.');
	if (!end)
	{
		end = start + strlen(start);
	}
	if (end <= start || (size_t)(end - start) >= size)
	{
		return 0;
	}

	memcpy(key, start, end - start);
	key[end - start] = 0;
	_strlwr(key);
	return 1;
}

void vr_weapon_register_models(void *(*register_model)(const char *name))
{
	fist_model = register_model ? register_model(FIST_MODEL) : NULL;
	if (!fist_model)
	{
		vrlog("VR: %s would not load - the other hand will not be drawn", FIST_MODEL);
	}
}

void vr_weapon_startup(void)
{
	int i;

	vr_weapon_hand = vr_cvar("vr_weapon_hand", "1", CVAR_ARCHIVE);
	vr_left_handed = vr_cvar("vr_left_handed", "0", CVAR_ARCHIVE);
	vr_weapon_client = vr_cvar("vr_weapon_client", "1", 0);
	vr_client_bullets = vr_cvar("vr_client_bullets", "0", CVAR_ARCHIVE);
	vr_weapon_reset = vr_cvar("vr_weapon_reset", "0", 0);
	vr_show_hands = vr_cvar("vr_show_hands", "1", CVAR_ARCHIVE);
	vr_freeze_idle = vr_cvar("vr_freeze_idle", "0", CVAR_ARCHIVE);
	vr_punch = vr_cvar("vr_punch", "1", CVAR_ARCHIVE);
	vr_punch_speed = vr_cvar("vr_punch_speed", "2", CVAR_ARCHIVE);
	vr_two_hand = vr_cvar("vr_two_hand", "1", CVAR_ARCHIVE);
	vr_other_hand = vr_cvar("vr_other_hand", "1", CVAR_ARCHIVE);
	vr_weapon_arm = vr_cvar("vr_weapon_arm", "0", CVAR_ARCHIVE);
	vr_weapon_fill = vr_cvar("vr_weapon_fill", "1", CVAR_ARCHIVE);
	vr_weapon_idle = vr_cvar("vr_weapon_idle", "0", CVAR_ARCHIVE);
	vr_upper_arm = vr_cvar("vr_upper_arm", "0", CVAR_ARCHIVE);
	vr_aim = vr_cvar("vr_aim", "1", 0);
	lefty = vr_left_handed->value != 0.0f;
	if (lefty)
	{
		vrlog("VR: left-handed: the weapon goes in the left hand, mirrored, and the buttons swap over");
	}

	/* Every known weapon's cvars now, so that a setting in config.cfg is kept
	   as an archived one even in a session that never uses that weapon. */
	for (i = 0; i < (int)(sizeof(weapon_table) / sizeof(weapon_table[0])); ++i)
	{
		find_weapon(weapon_table[i].name);
	}
}

/* ------------------------------------------------------------------------- */
/* Placing it                                                                 */
/* ------------------------------------------------------------------------- */

typedef struct
{
	int valid;
	vr_pose_t pose; /* the model's origin and axis, in the recentred frame */
	float scale;
} placement_t;

static struct
{
	int valid;
	vr_pose_t frame; /* grip position, aim orientation */
} hands[2];

static placement_t placed[2];
static float base_origin[3];   /* the engine's own view, before it moved to an eye */
static float base_yaw;
static int base_valid;
static vr_pose_t world_aim;    /* the weapon in the world, as drawn */
static int world_aim_valid;
static int placing;            /* between a publish and the end of that frame */
static int said_hands_drawn;
static int placed_this_frame;  /* the weapon was put in the hand this frame */
static int said_placed;
static float fore_at[3];       /* the fore-end, as placed this frame, in the recentred frame */
static int fore_valid;

static struct
{
	int active;                /* the other hand has hold of the weapon */
	int said;                  /* where it pointed, logged once a hold */
	unsigned count;
	vr_pose_t hold;            /* the other hand on the weapon: turned as it took hold, at the fore-end */
} steady;

static struct
{
	int button;
	DWORD pressed_at;
	int active;
	placement_t frozen[2];
	float anchor[2][3];
	float stick;
	DWORD last_tick;
} adjust;

/*
 * Which setting holds a weapon's grip: its own for each arm of a two-armed
 * model, and for everything else the one right-handed setting, which is
 * mirrored when the weapon is in the left hand.
 */
static int grip_slot(const weapon_t *w, int hand)
{
	return w->info->two_arms ? hand : RIGHT;
}

static int grip_mirrored(const weapon_t *w, int hand)
{
	return !w->info->two_arms && hand == LEFT;
}

/* The model's pose in the hand's frame, and its size: from vr_grip_<weapon> if
   that holds a setting, or else from the table. */
static void grip_setting(const weapon_t *w, int hand, vr_pose_t *relative, float *scale)
{
	const cvar_t *cvar = w->grip[grip_slot(w, hand)];
	float origin[3], angles[3], s;
	int i;

	if (cvar && cvar->string &&
		sscanf(cvar->string, "%f %f %f %f %f %f %f", &origin[0], &origin[1], &origin[2],
			&angles[0], &angles[1], &angles[2], &s) == 7 &&
		s >= SCALE_MIN && s <= SCALE_MAX)
	{
		vr_pose_set(relative, origin, angles);
		*scale = s;
	}
	else
	{
		/* The grip point goes where the hand is, the model pointing where it
		   points. */
		*scale = w->info->scale;
		for (i = 0; i < 3; ++i)
		{
			origin[i] = -w->info->scale * w->info->grip[i];
		}
		if (w->info->two_arms && hand == LEFT)
		{
			origin[1] = -origin[1];
		}
		angles[PITCH] = angles[YAW] = angles[ROLL] = 0.0f;
		vr_pose_set(relative, origin, angles);
	}

	/* Held in the hand it was not built for, the whole arrangement is the
	   right-handed one reflected in the hand's own middle - and so is the
	   model, which vr_gl mirrors as it is drawn. */
	if (grip_mirrored(w, hand))
	{
		vr_pose_mirror_y(relative);
	}
}

/* A pose's origin and angles, for the log. */
static void log_pose(const char *what, const vr_pose_t *pose)
{
	float angles[3];

	vr_pose_angles(pose, angles);
	vrlog("    %s at (%.1f %.1f %.1f) pointing (%.1f %.1f %.1f)", what, pose->origin[0], pose->origin[1],
		pose->origin[2], angles[PITCH], angles[YAW], angles[ROLL]);
}

static void save_grip(const weapon_t *w, int hand, const placement_t *p)
{
	const cvar_t *cvar = w->grip[grip_slot(w, hand)];
	vr_pose_t relative;
	float angles[3];
	char value[160];

	if (!cvar || !hands[hand].valid)
	{
		return;
	}

	log_pose(hand == LEFT ? "left hand" : "right hand", &hands[hand].frame);
	log_pose("weapon", &p->pose);
	vr_pose_relative(&hands[hand].frame, &p->pose, &relative);
	if (grip_mirrored(w, hand))
	{
		/* Kept as the right hand would hold it, whichever hand settled it. */
		vr_pose_mirror_y(&relative);
	}
	vr_pose_angles(&relative, angles);
	sprintf(value, "%.2f %.2f %.2f %.1f %.1f %.1f %.3f", relative.origin[0], relative.origin[1],
		relative.origin[2], angles[PITCH], angles[YAW], angles[ROLL], p->scale);
	vr_cvar_set(cvar->name, value);
	vr_printf("VR: the %s is held that way from now on: %s \"%s\"", w->key, cvar->name, value);
}

static double hands_time; /* what this frame's hands are for, in seconds */

void vr_weapon_hands_begin(double seconds)
{
	hands[LEFT].valid = 0;
	hands[RIGHT].valid = 0;
	hands_time = seconds;
}

void vr_weapon_track_hand(int hand, const float grip[3], const float aim_origin[3], const float aim_angles[3])
{
	(void)aim_origin;

	if (hand != LEFT && hand != RIGHT)
	{
		return;
	}

	vr_pose_set(&hands[hand].frame, grip, aim_angles);
	hands[hand].valid = 1;
}

int vr_weapon_hand_at(int hand, float out[3])
{
	if ((hand != LEFT && hand != RIGHT) || !hands[hand].valid)
	{
		return 0;
	}
	memcpy(out, hands[hand].frame.origin, sizeof(float) * 3);
	return 1;
}

void vr_weapon_adjust_button(int down)
{
	adjust.button = down;
	if (down)
	{
		adjust.pressed_at = GetTickCount();
	}
}

void vr_weapon_adjust_stick(float y)
{
	adjust.stick = y;
}

int vr_weapon_adjusting(void)
{
	return adjust.active;
}

static int two_arms(void)
{
	return current && current->info->two_arms;
}

/* A weapon with one arm in it, held in the hand it was not built for: ref_gl
   draws it mirrored down its own middle. */
static int mirror_weapon(void)
{
	return lefty && current && !current->info->two_arms;
}

/* The grip button: held long enough, the weapon stays where it is; let go,
   and it is taken back the way the hands now hold it. */
static void run_adjust(void)
{
	DWORD now = GetTickCount();
	int hand;

	if (!adjust.active)
	{
		if (!adjust.button || now - adjust.pressed_at < ADJUST_HOLD_MS || !current ||
			!placed[gun_hand()].valid)
		{
			return;
		}

		adjust.active = 1;
		adjust.last_tick = now;
		steady.active = 0;
		for (hand = 0; hand < 2; ++hand)
		{
			adjust.frozen[hand] = placed[hand];
			memcpy(adjust.anchor[hand], hands[hand].frame.origin, sizeof(adjust.anchor[hand]));
		}
		vr_printf("VR: adjusting the %s - hold it the way you want it and let go of the grip; "
			"that hand's stick sizes it", current->key);
		log_pose(hand_name(gun_hand()), &hands[gun_hand()].frame);
		log_pose("weapon", &placed[gun_hand()].pose);
		return;
	}

	if (adjust.button && current)
	{
		float seconds = (now - adjust.last_tick) / 1000.0f;

		adjust.last_tick = now;
		if (fabsf(adjust.stick) > 0.2f)
		{
			/* Up to twice or half the size a second, about where the hand was. */
			float factor = (float)exp(adjust.stick * seconds * 0.693147);

			for (hand = 0; hand < 2; ++hand)
			{
				placement_t *p = &adjust.frozen[hand];
				float scale = p->scale * factor;
				int i;

				if (!p->valid || scale < SCALE_MIN || scale > SCALE_MAX)
				{
					continue;
				}
				for (i = 0; i < 3; ++i)
				{
					p->pose.origin[i] = adjust.anchor[hand][i] +
						(p->pose.origin[i] - adjust.anchor[hand][i]) * (scale / p->scale);
				}
				p->scale = scale;
			}
		}
		return;
	}

	/* Let go: the hands hold it the way they do now. */
	adjust.active = 0;
	if (current)
	{
		save_grip(current, gun_hand(), &adjust.frozen[gun_hand()]);
		if (two_arms() && adjust.frozen[other_hand()].valid)
		{
			save_grip(current, other_hand(), &adjust.frozen[other_hand()]);
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Two hands                                                                  */
/* ------------------------------------------------------------------------- */

/*
 * The fore-end, in the model's own units as the right hand holds it: from
 * vr_fore_<weapon> if that holds a setting, or else where the model's own left
 * hand was, or else the table's distance ahead of the grip along the model's
 * forward - which, with the grip as it comes, is straight ahead of the hand.
 * Returns 0 for a weapon held in one hand.
 */
static int fore_in_model(const weapon_t *w, float out[3])
{
	vr_pose_t relative, hand, origin;
	float scale, units = vr_worldscale_units();
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };

	if (!w || w->info->two_arms || w->info->fore <= 0.0f || !w->fore)
	{
		return 0;
	}

	if (w->fore->string && sscanf(w->fore->string, "%f %f %f", &out[0], &out[1], &out[2]) == 3)
	{
		return 1;
	}

	if (w->info->has_hand)
	{
		memcpy(out, w->info->hand, sizeof(w->info->hand));
		return 1;
	}

	/* where the hand is on the model: the model's pose in the hand, undone */
	grip_setting(w, RIGHT, &relative, &scale);
	vr_pose_set(&origin, zero, zero);
	vr_pose_relative(&relative, &origin, &hand);
	out[0] = (hand.origin[0] + w->info->fore * units) / scale;
	out[1] = hand.origin[1] / scale;
	out[2] = hand.origin[2] / scale;
	return 1;
}

/* The fore-end of the weapon placed at p, in p's own frame, at its size. A
   mirrored model has it on the other side of its middle. */
static int fore_local(const weapon_t *w, const placement_t *p, float local[3])
{
	int i;

	if (!p->valid || !fore_in_model(w, local))
	{
		return 0;
	}

	if (grip_mirrored(w, gun_hand()))
	{
		local[1] = -local[1];
	}
	for (i = 0; i < 3; ++i)
	{
		local[i] *= p->scale;
	}
	return 1;
}

/* The same, in the frame p is given in. */
static int fore_point(const weapon_t *w, const placement_t *p, float out[3])
{
	float local[3];

	if (!fore_local(w, p, local))
	{
		return 0;
	}
	vr_pose_point(&p->pose, local, out);
	return 1;
}

static float distance(const float a[3], const float b[3])
{
	float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };

	return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

static float dot(const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int normalise(float v[3])
{
	float length = sqrtf(dot(v, v));

	if (length < 1e-6f)
	{
		return 0;
	}
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
	return 1;
}

/*
 * Rows forward, left and up, forward along f, and rolled so that row `which`
 * (1 left, 2 up) is as near to ref as it can be. Returns 0 if ref lies along f.
 */
static int frame_along(const float f[3], const float ref[3], int which, float out[3][3])
{
	float along = dot(ref, f);
	int i;

	memcpy(out[0], f, sizeof(out[0]));
	for (i = 0; i < 3; ++i)
	{
		out[which][i] = ref[i] - along * f[i];
	}
	if (!normalise(out[which]))
	{
		return 0;
	}

	if (which == 2)
	{
		/* left = up x forward */
		out[1][0] = out[2][1] * f[2] - out[2][2] * f[1];
		out[1][1] = out[2][2] * f[0] - out[2][0] * f[2];
		out[1][2] = out[2][0] * f[1] - out[2][1] * f[0];
	}
	else
	{
		/* up = forward x left */
		out[2][0] = f[1] * out[1][2] - f[2] * out[1][1];
		out[2][1] = f[2] * out[1][0] - f[0] * out[1][2];
		out[2][2] = f[0] * out[1][1] - f[1] * out[1][0];
	}
	return 1;
}

/*
 * Both hands on it: the gun hand stays where it is, and the weapon turns about
 * it until the fore-end lies on the line to the other hand. Only the line is
 * the other hand's; the roll about it is the gun hand's, as near as the line
 * allows - its up stays as near its real up as it can, or, with the weapon
 * pointing straight up or down, its left. Returns 0 if the hands are too
 * close together to say which way that is.
 */
static int pivot_weapon(placement_t *p, const vr_pose_t *hand, const float fore[3], const float other[3])
{
	float shortest = STEADY_SHORTEST * vr_worldscale_units();
	float to_fore[3], to_other[3], local[3];
	float in_hand[3][3], in_world[3][3];
	static const float hand_ref[2][3] = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
	vr_pose_t turned, relative;
	int i, j, which;

	if (distance(fore, hand->origin) < shortest || distance(other, hand->origin) < shortest)
	{
		return 0;
	}

	for (i = 0; i < 3; ++i)
	{
		to_fore[i] = fore[i] - hand->origin[i];
		to_other[i] = other[i] - hand->origin[i];
	}
	normalise(to_other);

	/* the way to the fore-end, as the hand sees it */
	for (i = 0; i < 3; ++i)
	{
		local[i] = dot(to_fore, hand->axis[i]);
	}
	normalise(local);

	/* roll by the hand's up, unless the line is nearly along it */
	which = fabsf(dot(to_other, hand->axis[2])) < 0.95f && fabsf(local[2]) < 0.95f ? 2 : 1;
	if (!frame_along(local, hand_ref[which - 1], which, in_hand) ||
		!frame_along(to_other, hand->axis[which], which, in_world))
	{
		return 0;
	}

	/* the turn that takes one frame to the other, applied to the hand */
	turned = *hand;
	for (j = 0; j < 3; ++j)
	{
		for (i = 0; i < 3; ++i)
		{
			turned.axis[j][i] = in_world[0][i] * in_hand[0][j] + in_world[1][i] * in_hand[1][j] +
				in_world[2][i] * in_hand[2][j];
		}
	}

	vr_pose_relative(hand, &p->pose, &relative);
	vr_pose_compose(&turned, &relative, &p->pose);
	return 1;
}

/* Where the fore-end is while the weapon is being adjusted: where the other
   hand is now, on the model as it is frozen. */
static void set_fore(void)
{
	const placement_t *p = &adjust.frozen[gun_hand()];
	vr_pose_t at, local;
	float model[3];
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };
	char value[80];

	vr_pose_set(&at, hands[other_hand()].frame.origin, zero);
	vr_pose_relative(&p->pose, &at, &local);
	model[0] = local.origin[0] / p->scale;
	model[1] = local.origin[1] / p->scale;
	model[2] = local.origin[2] / p->scale;
	if (grip_mirrored(current, gun_hand()))
	{
		/* kept as the right-handed model has it */
		model[1] = -model[1];
	}

	sprintf(value, "%.1f %.1f %.1f", model[0], model[1], model[2]);
	vr_cvar_set(current->fore->name, value);
	vr_printf("VR: the %s's fore-end is there from now on: %s \"%s\"", current->key, current->fore->name,
		value);
}

int vr_weapon_steady(int down)
{
	float at[3];
	float reach = STEADY_REACH * vr_worldscale_units();

	if (!down)
	{
		if (steady.active)
		{
			steady.active = 0;
			vrlog("VR: the other hand has let go of the %s", current ? current->key : "weapon");
		}
		return 0;
	}

	if ((vr_two_hand && vr_two_hand->value == 0.0f) || !current || !hands[other_hand()].valid ||
		!fore_in_model(current, at))
	{
		return 0;
	}

	if (adjust.active)
	{
		set_fore();
		return 1;
	}

	if (!fore_point(current, &placed[gun_hand()], at) || distance(at, hands[other_hand()].frame.origin) > reach)
	{
		return 0;
	}

	steady.active = 1;
	steady.said = 0;
	steady.count++;

	/* From here the other hand is drawn on the weapon, not where it is: at the
	   fore-end, turned as it was when it took hold. */
	vr_pose_relative(&placed[gun_hand()].pose, &hands[other_hand()].frame, &steady.hold);
	fore_local(current, &placed[gun_hand()], steady.hold.origin);
	vrlog("VR: the %s is held in both hands (%.0f cm from the fore-end)", current->key,
		distance(at, hands[other_hand()].frame.origin) * 100.0f / vr_worldscale_units());
	return 1;
}

static unsigned shots_felt;
static unsigned hits_felt;

void vr_weapon_feedback(int kind, int hand, float strength)
{
	static const char *const heavy[] = { "shotgun", "rlaunch", "srifle", "quantum", "spgun" };
	int h = (hand == LEFT || hand == RIGHT) ? hand : gun_hand();
	int i, big = 0;

	strength = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);

	if (kind == SINVR_FEEDBACK_SHOT)
	{
		for (i = 0; current && i < (int)(sizeof(heavy) / sizeof(heavy[0])); ++i)
		{
			big |= strcmp(current->key, heavy[i]) == 0;
		}
		/* The G2's controllers let a 40 ms pulse through only some of the time
		   (the owner felt every other magnum shot, with all of them sent), so
		   nothing here is shorter than a tenth of a second. */
		vr_input_haptic(h, big ? 0.14f : 0.10f, (big ? 0.6f : 0.4f) + 0.4f * strength);
		if (steady.active)
		{
			vr_input_haptic(h == LEFT ? RIGHT : LEFT, big ? 0.12f : 0.10f, (big ? 0.4f : 0.25f) + 0.3f * strength);
		}
		if (!shots_felt++)
		{
			vrlog("VR: the game sends its shots - the %s hand feels them", hand_name(h));
		}
	}
	else if (kind == SINVR_FEEDBACK_CHARGE)
	{
		/* A tick every tenth of a second, each a little longer, so the buzz
		   runs on unbroken while the trigger is held, from faint to full. */
		vr_input_haptic(h, 0.12f, 0.08f + 0.92f * strength);
		if (steady.active)
		{
			vr_input_haptic(h == LEFT ? RIGHT : LEFT, 0.12f, 0.05f + 0.6f * strength);
		}
	}
	else if (kind == SINVR_FEEDBACK_HIT)
	{
		vr_input_haptic(h, 0.10f, 0.4f + 0.6f * strength);
		if (!hits_felt++)
		{
			vrlog("VR: the game sends its punches' hits - the %s hand felt one", hand_name(h));
		}
	}
}

static void reset_requested(void)
{
	if (!vr_weapon_reset || vr_weapon_reset->value == 0.0f)
	{
		return;
	}

	vr_cvar_set("vr_weapon_reset", "0");
	if (!current)
	{
		vr_printf("VR: vr_weapon_reset: no weapon has been seen yet");
		return;
	}

	/* Whatever was being adjusted is dropped, and the grip button has to be
	   pressed afresh to start again. */
	adjust.active = 0;
	adjust.button = 0;

	if (current->grip[RIGHT])
	{
		vr_cvar_set(current->grip[RIGHT]->name, "");
	}
	if (current->grip[LEFT])
	{
		vr_cvar_set(current->grip[LEFT]->name, "");
	}
	if (current->fore)
	{
		vr_cvar_set(current->fore->name, "");
	}
	vr_printf("VR: the %s is back to its first guess", current->key);
}

/* ------------------------------------------------------------------------- */
/* Punches                                                                    */
/* ------------------------------------------------------------------------- */

/*
 * A punch is a swing: the hand goes past vr_punch_speed the way its knuckles
 * lead - roughly where the controller points, which is along the forearm for
 * a jab, across the body for a hook and upwards for an uppercut - and the
 * punch is thrown at the swing's fastest, where a real one lands, with the
 * hand's position and direction there. Pulling the fist back goes against the
 * way it points, so that is never a punch, however fast; and after a punch the
 * hand has to slow to half the speed before it can throw another, so one swing
 * is one punch. Speed is taken between this frame's pose and the last,
 * both for the display times the runtime gave, so it does not depend on how
 * fast the engine happens to be running. Every swing is sent; the game only
 * counts the ones thrown with the fists out.
 */
static struct
{
	int known;              /* a position from the last frame to measure against */
	float origin[3];        /* the grip, recentred, game units */
	double time;
	int armed;              /* slow enough, since the last punch, to throw another */
	int swinging;           /* past the speed, waiting for the fastest moment */
	float peak;             /* m/s */
	float peak_origin[3];
	float peak_dir[3];
} swing[2];

#define PUNCH_ALONG 0.2f /* the least of the swing that goes the way the fist points */
#define PUNCH_MAX 20.0f  /* m/s: faster than any fist; that is the tracking jumping */

static unsigned punches_thrown;

void vr_weapon_recentred(void)
{
	swing[LEFT].known = 0;
	swing[RIGHT].known = 0;
}

static void watch_swings(void)
{
	float units = vr_worldscale_units();
	float threshold = vr_punch_speed ? vr_punch_speed->value : 2.0f;
	int enabled = (!vr_punch || vr_punch->value != 0.0f) && !adjust.active;
	int hand, i;

	if (threshold < 0.5f)
	{
		threshold = 0.5f;
	}

	for (hand = 0; hand < 2; ++hand)
	{
		float velocity[3], speed, along;
		double dt;

		if (!hands[hand].valid)
		{
			swing[hand].known = 0;
			continue;
		}

		dt = hands_time - swing[hand].time;
		if (!swing[hand].known || dt <= 0.0 || dt > 0.1 || units <= 0.0f)
		{
			/* the first sight of it, or a gap: nothing to measure against */
			memcpy(swing[hand].origin, hands[hand].frame.origin, sizeof(swing[hand].origin));
			swing[hand].time = hands_time;
			swing[hand].known = 1;
			swing[hand].swinging = 0;
			continue;
		}

		speed = 0.0f;
		for (i = 0; i < 3; ++i)
		{
			velocity[i] = (float)((hands[hand].frame.origin[i] - swing[hand].origin[i]) / dt / units);
			speed += velocity[i] * velocity[i];
		}
		speed = sqrtf(speed);
		along = 0.0f;
		if (speed > 0.0f)
		{
			for (i = 0; i < 3; ++i)
			{
				along += velocity[i] * hands[hand].frame.axis[0][i];
			}
			along /= speed;
		}
		memcpy(swing[hand].origin, hands[hand].frame.origin, sizeof(swing[hand].origin));
		swing[hand].time = hands_time;

		/* A controller that loses its tracking and finds it again, or a hand
		   that changes hands - the desk's pretend ones taking over - arrives
		   somewhere else in one frame. Measure from there next time. */
		if (speed > PUNCH_MAX)
		{
			swing[hand].swinging = 0;
			continue;
		}

		if (!enabled)
		{
			swing[hand].swinging = 0;
			continue;
		}

		if (!swing[hand].armed)
		{
			swing[hand].armed = speed < threshold * 0.5f;
			continue;
		}

		if (speed >= threshold && along >= PUNCH_ALONG && (!swing[hand].swinging || speed > swing[hand].peak))
		{
			swing[hand].swinging = 1;
			swing[hand].peak = speed;
			memcpy(swing[hand].peak_origin, hands[hand].frame.origin, sizeof(swing[hand].peak_origin));
			for (i = 0; i < 3; ++i)
			{
				swing[hand].peak_dir[i] = velocity[i] / speed;
			}
			continue;
		}

		/* past its fastest, or coming back: that was the punch */
		if (swing[hand].swinging && (speed < swing[hand].peak * 0.85f || along < 0.0f))
		{
			vr_poses_punch(hand, swing[hand].peak_origin, swing[hand].peak_dir, swing[hand].peak);
			vrlog("VR: a punch with the %s hand, %.1f m/s, going (%.2f %.2f %.2f)", hand_name(hand),
				swing[hand].peak, swing[hand].peak_dir[0], swing[hand].peak_dir[1], swing[hand].peak_dir[2]);
			punches_thrown++;
			swing[hand].swinging = 0;
			swing[hand].armed = 0;
		}
	}
}

static int sin_patched(void);
static int wants_server_bullets(void);

void vr_weapon_publish(void)
{
	int hand, gun = gun_hand(), flags = 0;
	float angles[3];

	placed[LEFT].valid = placed[RIGHT].valid = 0;
	placing = 0;
	fore_valid = 0;

	check_mode();
	reset_requested();
	watch_swings();

	if (!vr_weapon_hand || vr_weapon_hand->value == 0.0f || !hands[gun].valid || !current)
	{
		adjust.active = 0;
		return;
	}

	for (hand = 0; hand < 2; ++hand)
	{
		vr_pose_t relative;

		if (!hands[hand].valid || (hand != gun && !two_arms()))
		{
			continue;
		}

		grip_setting(current, hand, &relative, &placed[hand].scale);
		vr_pose_compose(&hands[hand].frame, &relative, &placed[hand].pose);
		placed[hand].valid = 1;
	}

	run_adjust();
	if (adjust.active)
	{
		placed[LEFT] = adjust.frozen[LEFT];
		placed[RIGHT] = adjust.frozen[RIGHT];
	}
	else if (steady.active && hands[other_hand()].valid && fore_point(current, &placed[gun], fore_at))
	{
		if (pivot_weapon(&placed[gun], &hands[gun].frame, fore_at, hands[other_hand()].frame.origin) &&
			!steady.said)
		{
			steady.said = 1;
			log_pose("both hands: weapon", &placed[gun].pose);
		}
	}
	fore_valid = fore_point(current, &placed[gun], fore_at);

	placing = 1;

	/* The client's own impacts are left out only when the game will aim from
	   the barrel, and so send the server's instead. */
	if (sin_patched() && wants_server_bullets())
	{
		flags |= SINVR_CLIENT_BULLETS_OFF;
	}

	/* The barrel of a mirrored model is on the other side of its middle, and
	   the game measures from the model. */
	if (mirror_weapon())
	{
		flags |= SINVR_WEAPON_MIRRORED;
	}

	/* Breathing under a weapon that is being lined up with a hand is only in
	   the way; in play it is the player's own choice. */
	if (adjust.active || (vr_freeze_idle && vr_freeze_idle->value != 0.0f))
	{
		flags |= SINVR_WEAPON_STILL;
	}

	vr_pose_angles(&placed[gun].pose, angles);
	vr_poses_weapon(placed[gun].pose.origin, angles, placed[gun].scale, flags);
}

void vr_weapon_end_frame(void)
{
	placing = 0;
	placed_this_frame = 0;
	world_aim_valid = 0;
	base_valid = 0;
	fore_valid = 0;
	fist_placed = 0;
	vr_gl_clear_models();
}

/* ------------------------------------------------------------------------- */
/* Where the hands are                                                        */
/* ------------------------------------------------------------------------- */

/* A cross at the hand, its arms along the way the hand is held, and a line out
   the way it points. Bright for the hand the weapon is in, dim for the other. */
static void draw_hand_marker(const vr_pose_t *pose, float units, int bright)
{
	const float cross = 0.05f * units; /* 5 cm arms, 30 cm of aim */
	const float ray = 0.30f * units;
	float shade = bright ? 1.0f : 0.45f;
	int axis;

	glBegin(GL_LINES);
	for (axis = 0; axis < 3; ++axis)
	{
		/* forward red, left green, up blue */
		glColor3f(axis == 0 ? shade : 0.0f, axis == 1 ? shade : 0.0f, axis == 2 ? shade : 0.0f);
		glVertex3f(pose->origin[0] - pose->axis[axis][0] * cross,
			pose->origin[1] - pose->axis[axis][1] * cross,
			pose->origin[2] - pose->axis[axis][2] * cross);
		glVertex3f(pose->origin[0] + pose->axis[axis][0] * cross,
			pose->origin[1] + pose->axis[axis][1] * cross,
			pose->origin[2] + pose->axis[axis][2] * cross);
	}

	glColor3f(shade, shade, shade * 0.4f);
	glVertex3fv(pose->origin);
	glVertex3f(pose->origin[0] + pose->axis[0][0] * ray, pose->origin[1] + pose->axis[0][1] * ray,
		pose->origin[2] + pose->axis[0][2] * ray);
	glEnd();
}

/* The fore-end: a small cross, cyan, brighter while the other hand has hold. */
static void draw_fore_marker(const float at[3], float units)
{
	const float cross = 0.03f * units;
	float shade = steady.active ? 1.0f : 0.6f;
	int axis;

	glBegin(GL_LINES);
	glColor3f(0.0f, shade, shade);
	for (axis = 0; axis < 3; ++axis)
	{
		glVertex3f(at[0] - (axis == 0) * cross, at[1] - (axis == 1) * cross, at[2] - (axis == 2) * cross);
		glVertex3f(at[0] + (axis == 0) * cross, at[1] + (axis == 1) * cross, at[2] + (axis == 2) * cross);
	}
	glEnd();
}

void vr_weapon_draw_hands(const refdef_t *fd)
{
	int mode = vr_show_hands ? (int)vr_show_hands->value : 1;
	float units = vr_worldscale_units();
	int hand, drawn = 0;

	if (!fd || !base_valid || mode <= 0 || (mode < 2 && !adjust.active))
	{
		return;
	}

	if (!hands[LEFT].valid && !hands[RIGHT].valid)
	{
		return;
	}

	if (!vr_gl_world_begin(fd->vieworg, fd->viewangles))
	{
		return;
	}

	for (hand = 0; hand < 2; ++hand)
	{
		vr_pose_t world;

		if (!hands[hand].valid)
		{
			continue;
		}

		vr_pose_to_world(&hands[hand].frame, base_origin, base_yaw, &world);
		draw_hand_marker(&world, units, hand == gun_hand());
		drawn++;
	}

	if (fore_valid)
	{
		vr_pose_t at, world;
		static const float zero[3] = { 0.0f, 0.0f, 0.0f };

		vr_pose_set(&at, fore_at, zero);
		vr_pose_to_world(&at, base_origin, base_yaw, &world);
		draw_fore_marker(world.origin, units);
	}

	vr_gl_world_end();

	if (!said_hands_drawn && drawn)
	{
		said_hands_drawn = 1;
		vrlog("VR: the controllers are drawn in the view (vr_show_hands %d)", mode);
	}
}

int vr_weapon_in_hand(void)
{
	return placed_this_frame;
}

/* The weapon, moved from the eye to the hand: origin, angles, the orientation
   matrix SiN's bone code reads, and the size it is drawn and its bones placed
   at. There is no interpolating from where the client had it. The hand holding
   it goes into world_aim as well, for the crosshair to follow. */
static void put_in_hand(entity_t *e, const placement_t *p, const float vieworg[3], float yaw)
{
	vr_pose_t world;

	vr_pose_to_world(&p->pose, vieworg, yaw, &world);
	memcpy(e->origin, world.origin, sizeof(e->origin));
	memcpy(e->oldorigin, world.origin, sizeof(e->oldorigin));
	memcpy(e->axis, world.axis, sizeof(e->axis));
	vr_axis_to_angles(world.axis, e->angles);
	e->scale = p->scale;

	if (p == &placed[gun_hand()])
	{
		world_aim = world;
		world_aim_valid = 1;
	}
}

int vr_weapon_world_aim(float origin[3], float forward[3])
{
	if (!world_aim_valid || !placed_this_frame)
	{
		return 0;
	}

	memcpy(origin, world_aim.origin, sizeof(float) * 3);
	memcpy(forward, world_aim.axis[0], sizeof(float) * 3);
	return 1;
}

/*
 * One arm of a two-armed model: the other is cut away down the model's middle,
 * the plane of its own forward and up. The entity's group flags cannot do it -
 * they only reach the groups a model's .def names, and the fists' arms are not
 * among them - so it is done where ref_gl draws it (vr_gl.c).
 *
 * The arm is cut short as well, unless vr_upper_arm says otherwise: the fists'
 * model has the whole arm, down the forearm from the wrist to the elbow and
 * back up again, built for a camera it hung down from, and in the hand the
 * upper arm sticks out where no arm is. Measured off view_punch.def's idle,
 * in its own units (x forward, z up; both arms alike), the wrist is at
 * (13.5, -13) and the elbow at (0.5, -26.5); the cut goes through the forearm
 * 80% of the way from the wrist to the elbow - about as much arm as the
 * magnum's own hand has. Square across the forearm, it left the far end of
 * the upper arm, which folds back up over the forearm, on the kept side, so
 * the plane leans 16 degrees back from square (its normal 30 degrees up from
 * forward, where the forearm's is 46): the whole upper arm is then behind it.
 */
#define FOREARM_CUT_X 3.1f
#define FOREARM_CUT_Z (-23.8f)
#define FOREARM_TOWARD_WRIST_X 0.866f
#define FOREARM_TOWARD_WRIST_Z 0.5f

static void keep_one_arm(const entity_t *e, int hand)
{
	float plane[4], at[3], n[3];
	const float *left = e->axis[1];
	float sign = hand == LEFT ? 1.0f : -1.0f;
	int i;

	plane[0] = sign * left[0];
	plane[1] = sign * left[1];
	plane[2] = sign * left[2];
	plane[3] = -sign * (left[0] * e->origin[0] + left[1] * e->origin[1] + left[2] * e->origin[2]);
	vr_gl_clip_model_at(e->origin, plane);

	if (vr_upper_arm && vr_upper_arm->value != 0.0f)
	{
		return;
	}
	for (i = 0; i < 3; ++i)
	{
		at[i] = e->origin[i] + e->scale * (FOREARM_CUT_X * e->axis[0][i] + FOREARM_CUT_Z * e->axis[2][i]);
		n[i] = FOREARM_TOWARD_WRIST_X * e->axis[0][i] + FOREARM_TOWARD_WRIST_Z * e->axis[2][i];
	}
	plane[0] = n[0];
	plane[1] = n[1];
	plane[2] = n[2];
	plane[3] = -(n[0] * at[0] + n[1] * at[1] + n[2] * at[2]);
	vr_gl_clip_model_at(e->origin, plane);
}

/* The arm built into the weapon's model, left out unless vr_weapon_arm says
   otherwise; and the side of it that was never made, filled in unless
   vr_weapon_fill says otherwise. */
static void hide_modelled_arm(const entity_t *gun)
{
	const idle_info_t *anims = idle_info(current ? current->key : NULL);

	if (current && current->info->arm_count > 0 && (!vr_weapon_arm || vr_weapon_arm->value == 0.0f))
	{
		vr_gl_hide_model_parts_at(gun->origin, current->info->arm, current->info->arm_count);
	}
	if (current && current->fill && current->fill->value != 0.0f &&
		(!vr_weapon_fill || vr_weapon_fill->value != 0.0f) &&
		anims && gun->anim >= 0 && gun->anim < 32 && (anims->steady >> gun->anim & 1))
	{
		vr_gl_fill_model_at(gun->origin, current->info->middle * gun->scale, current->info->keep,
			current->info->keep_count);
	}
}

/*
 * The hand that is not holding the weapon, as the fists' arm for it: out is
 * filled from the weapon's entity, with the fists' model, and placed. Returns 0
 * when there is nothing to draw - no fists model, the fists themselves out
 * (they have both arms), or that hand not tracked.
 */
static int dress_other_hand(entity_t *out, const entity_t *gun, const float vieworg[3], float yaw)
{
	int hand = other_hand();
	weapon_t *fists;
	placement_t p;
	vr_pose_t relative;

	if (!fist_model || (vr_other_hand && vr_other_hand->value == 0.0f) || !current || two_arms() ||
		!hands[hand].valid)
	{
		return 0;
	}

	fists = find_weapon("punch");
	if (!fists)
	{
		return 0;
	}
	grip_setting(fists, hand, &relative, &p.scale);
	if (steady.active && placed[gun_hand()].valid)
	{
		/* holding the weapon: the hand goes with it */
		vr_pose_t on_weapon;

		vr_pose_compose(&placed[gun_hand()].pose, &steady.hold, &on_weapon);
		vr_pose_compose(&on_weapon, &relative, &p.pose);
	}
	else
	{
		vr_pose_compose(&hands[hand].frame, &relative, &p.pose);
	}
	p.valid = 1;

	*out = *gun;
	out->model = fist_model;
	out->anim = FIST_IDLE_ANIM;
	out->frame = 0;
	out->oldframe = 0;
	out->backlerp = 0.0f;
	out->skinnum = 0;
	out->skin = NULL;
	out->id = NEW_ENTITY_ID;
	memset(out->unknown_70, 0, sizeof(out->unknown_70));
	memset(out->groups, 0, sizeof(out->groups));
	put_in_hand(out, &p, vieworg, yaw);
	keep_one_arm(out, hand);
	memcpy(fist_origin, out->origin, sizeof(fist_origin));
	fist_placed = 1;
	if (!said_other_hand)
	{
		said_other_hand = 1;
		vrlog("VR: the other hand is drawn as %s at (%.1f %.1f %.1f), size %.2f, flags 0x%x",
			(const char *)fist_model, out->origin[0], out->origin[1], out->origin[2], out->scale, out->flags);
	}
	return 1;
}

/* The other hand's fist carries the weapon's flags, so it is told apart by
   where it was put. */
static int is_other_hand(const entity_t *e)
{
	return fist_placed && e->model == fist_model && fabsf(e->origin[0] - fist_origin[0]) < 0.01f &&
		fabsf(e->origin[1] - fist_origin[1]) < 0.01f && fabsf(e->origin[2] - fist_origin[2]) < 0.01f;
}

/* An idle animation on e, as the current weapon plays it, held on its first
   frame - unless vr_weapon_idle 1 lets it play. */
static void hold_idle(entity_t *e)
{
	const idle_info_t *anims = idle_info(current ? current->key : NULL);
	int j;

	if (!anims || (vr_weapon_idle && vr_weapon_idle->value != 0.0f))
	{
		return;
	}

	for (j = 0; j < anims->count; ++j)
	{
		if (e->anim == anims->map[j][0])
		{
			e->anim = anims->map[j][1];
			e->frame = 0;
			e->oldframe = 0;
			e->backlerp = 0.0f;
			return;
		}
	}
}

/* Notes which weapon this is, and says so when it changes. */
static void see_weapon(const entity_t *e)
{
	char key[40];
	weapon_t *w;

	if (!weapon_key(e, key, sizeof(key)))
	{
		return;
	}

	if (current && strcmp(current->key, key) == 0)
	{
		return;
	}

	w = find_weapon(key);
	if (w && w != current)
	{
		current = w;
		steady.active = 0;
		vrlog("VR: the view weapon is now %s (%s), animation %d frame %d", (const char *)e->model,
			w->info == &weapon_other ? "no measurements - a general guess" : "measured", e->anim, e->frame);
	}
}

static void say_placed(const char *how)
{
	if (!said_placed && current)
	{
		float angles[3];

		said_placed = 1;
		vr_pose_angles(&placed[gun_hand()].pose, angles);
		vr_printf("VR: the %s is in the %s hand%s%s, %s: (%.1f %.1f %.1f) from the eyes, "
			"pointing (%.1f %.1f %.1f), size %.2f", current->key, hand_name(gun_hand()),
			two_arms() ? " and the other" : "", mirror_weapon() ? ", mirrored" : "", how,
			placed[gun_hand()].pose.origin[0], placed[gun_hand()].pose.origin[1],
			placed[gun_hand()].pose.origin[2], angles[PITCH], angles[YAW], angles[ROLL],
			placed[gun_hand()].scale);
	}
}

/* ------------------------------------------------------------------------- */
/* Through sin.exe                                                            */
/* ------------------------------------------------------------------------- */

/*
 * Measured on the Steam build of sin.exe (docs/ENGINE_NOTES.md), as offsets from
 * its base. Each call is checked before it is changed, and each address is read
 * out of an instruction that uses it, so another build is left alone.
 *
 *   CL_AddViewWeapon builds the view weapon's entity, hands it to V_AddEntity
 *   at +0x747F, then runs the model's client commands on the same entity.
 *   The "shootbullets" command calls CL_ShootBullets at +0x1A707: it traces
 *   from the player's eye along the entity's angles and draws the impacts.
 */
#define SIN_ADD_VIEW_WEAPON_CALL 0x747F
#define SIN_ADD_ENTITY 0x25520
#define SIN_SHOOT_BULLETS_CALL 0x1A707
#define SIN_SHOOT_BULLETS 0x208F0
#define SIN_USES_VIEWORG 0x718C          /* addss xmm1, [cl.refdef.vieworg] */
#define SIN_USES_VIEWYAW 0x726E          /* addss xmm1, [cl.refdef.viewangles + 4] */
#define SIN_SETS_COMMAND_ENTITY 0x7572   /* mov [the entity running commands], eax */
#define SIN_READS_BONE_SCALE 0x194DD     /* movss xmm0, [0.3], in the attach command */

/*
 * The model's client commands - the muzzle flash (attach), tracers and shell
 * cases (bonespawn) - put what they make at the weapon's bones, and for a view
 * weapon (RF_WEAPONMODEL) CL_CMDS_ProcessCommands (+0x191E0) first shrinks the
 * bone's offset from the model's origin to 0.3 of itself, because SiN draws its
 * view models about three times life size. In the hand the weapon is drawn at
 * its true size, so the commands are given the weapon at its size over 0.3,
 * which puts the bones back where the drawn model has them.
 *
 * Both they and the client's upkeep of an attached effect afterwards - which
 * finds the weapon in the scene by its id and places the flash off its bone
 * every frame, with no 0.3 and no regard for the "hand" cvar (the first half
 * of +0x18E40) - lay the bone's offset out along the entity's axis rows, not
 * its angles. ref_gl draws the weapon from its angles. So a weapon drawn
 * mirrored has its left row turned round in both the scene's copy and the
 * commands' (mirror_axis): the effects follow the mirror, the picture is
 * unchanged, and the only other reader in ref_gl, the bounding box it culls
 * by (+0x19685), gets the mirrored model's box, which is the right one.
 */
static const float *sin_bone_scale;
static unsigned effects_placed;

static void mirror_axis(entity_t *e)
{
	e->axis[1][0] = -e->axis[1][0];
	e->axis[1][1] = -e->axis[1][1];
	e->axis[1][2] = -e->axis[1][2];
}

/*
 * And some weapons' muzzle flash is moved off the barrel on purpose:
 * view_magnum.def's is "attach barrel sprites/gunblast.spr ... origin 10 0 -10",
 * and the shotgun, rocket launcher and chaingun have one along the barrel. The
 * option parser (+0x1793C) adds that offset along the weapon's axes, in the
 * flat view model's units and unscaled - it was tuned to look right from the
 * flat camera, and in the hand it puts the magnum's flash beside the gun. The
 * view weapon's commands all go through one call in CL_AddViewWeapon
 * (+0x7580), so there, while the weapon is in the hand, an attach command has
 * its origin option left out and the flash sits on the barrel itself. The
 * call passes the count of words and the words on the stack, and the caller
 * pops them.
 */
#define SIN_VIEW_COMMANDS_CALL 0x7580
#define SIN_PROCESS_COMMANDS 0x191E0
#define MAX_COMMAND_WORDS 64

static void *real_process_commands;
static char *command_words[MAX_COMMAND_WORDS];
static unsigned view_commands;
static unsigned flash_offsets_dropped;

typedef void(__fastcall *add_entity_t)(entity_t *entity);

static add_entity_t real_add_entity;
static void *real_shoot_bullets;
static const float *sin_vieworg;
static const float *sin_viewyaw;
static entity_t *const *sin_command_entity;
static int patched;
static int keep_client_bullets = 1; /* read by the thunk: 0 leaves the view weapon's out */
static unsigned bullets_left_out;
static unsigned client_placements;

/* The words of one of the view weapon's commands, as they are to be run. */
static char **__cdecl filter_view_command(int *argc, char **argv)
{
	int i, n = 0, attach = 0;

	view_commands++;
	if (!placed_this_frame || !argv || *argc < 1 || *argc > MAX_COMMAND_WORDS)
	{
		return argv;
	}

	for (i = 0; i < *argc; ++i)
	{
		if (!argv[i])
		{
			return argv;
		}
		if (_stricmp(argv[i], "attach") == 0)
		{
			attach = 1;
		}
		else if (attach && _stricmp(argv[i], "origin") == 0 && i + 3 < *argc)
		{
			i += 3;
			continue;
		}
		command_words[n++] = argv[i];
	}

	if (n == *argc)
	{
		return argv;
	}
	*argc = n;
	flash_offsets_dropped++;
	return command_words;
}

static __declspec(naked) void process_view_commands(void)
{
	__asm {
		push ecx
		push edx
		mov eax, [esp + 16]  /* the words */
		lea ecx, [esp + 12]  /* the count, which may come down */
		push eax
		push ecx
		call filter_view_command
		add esp, 8
		pop edx
		pop ecx
		mov [esp + 8], eax   /* the caller pushed them and pops them itself */
		jmp real_process_commands
	}
}

static int sin_patched(void)
{
	return patched && vr_weapon_client && vr_weapon_client->value != 0.0f;
}

/*
 * The client traces its own bullets from the player's eye, along the weapon's
 * angles, and draws the sparks, holes and sounds where they land - which is not
 * where a weapon in the hand is firing from. So they are left to the server,
 * which traces from the barrel and sends the same effects back (TE_GUNSHOT
 * reaches the same code in the client). "vr_client_bullets 1" keeps the
 * client's, for anyone who would rather have them than none.
 */
static int wants_server_bullets(void)
{
	return (!vr_aim || vr_aim->value != 0.0f) && (!vr_client_bullets || vr_client_bullets->value == 0.0f);
}

/* Where CL_AddViewWeapon hands its entity over. */
static void __fastcall add_view_weapon(entity_t *gun)
{
	int active = placing && sin_patched();
	int hand = gun_hand();
	float yaw;

	if (gun)
	{
		see_weapon(gun);
	}

	if (!active || !gun || !placed[hand].valid)
	{
		keep_client_bullets = 1;
		real_add_entity(gun);
		return;
	}

	yaw = *sin_viewyaw;
	vr_gl_clear_models();
	if (two_arms() && placed[other_hand()].valid)
	{
		entity_t other = *gun;

		put_in_hand(&other, &placed[other_hand()], sin_vieworg, yaw);
		keep_one_arm(&other, other_hand());
		hold_idle(&other);
		real_add_entity(&other);
	}

	put_in_hand(gun, &placed[hand], sin_vieworg, yaw);
	if (two_arms())
	{
		keep_one_arm(gun, hand);
	}
	else if (mirror_weapon())
	{
		vr_gl_mirror_model_at(gun->origin);
		mirror_axis(gun);
	}
	hide_modelled_arm(gun);

	/* The scene gets the weapon held still; the client's commands, which go on
	   to run on this entity, still see the frame the game is really on. */
	{
		int anim = gun->anim, frame = gun->frame, oldframe = gun->oldframe;
		float backlerp = gun->backlerp;

		hold_idle(gun);
		real_add_entity(gun);
		gun->anim = anim;
		gun->frame = frame;
		gun->oldframe = oldframe;
		gun->backlerp = backlerp;
	}

	{
		entity_t fist;

		if (dress_other_hand(&fist, gun, sin_vieworg, yaw))
		{
			real_add_entity(&fist);
		}
	}

	/* The entity goes on to the model's client commands from here: at the size
	   that puts their bones where the drawn ones are - and mirrored, if it is,
	   by its axis. */
	if (sin_bone_scale)
	{
		gun->scale /= *sin_bone_scale;
		effects_placed++;
	}
	keep_client_bullets = !wants_server_bullets();
	placed_this_frame = 1;
	client_placements++;
	say_placed("placed by the client, with its effects");
}

/*
 * CL_ShootBullets takes the entity in ecx, the count in edx, the range in xmm3
 * and the spread on the stack, which the caller pops. So this is only a
 * decision: go on to it untouched, or return at once.
 */
static __declspec(naked) void shoot_bullets(void)
{
	__asm {
		cmp keep_client_bullets, 0
		jne pass_on
		mov eax, sin_command_entity
		mov eax, [eax]
		test eax, eax
		je pass_on
		test byte ptr [eax + 48h], 4 /* RF_WEAPONMODEL: the view weapon's command */
		je pass_on
		inc bullets_left_out
		ret
	pass_on:
		jmp real_shoot_bullets
	}
}

void vr_weapon_install(void)
{
	static const BYTE addss_xmm1[] = { 0xF3, 0x0F, 0x58, 0x0D };
	static const BYTE mov_to_memory[] = { 0xA3 };
	static const BYTE movss_xmm0[] = { 0xF3, 0x0F, 0x10, 0x05 };
	HMODULE exe = GetModuleHandleA(NULL);

	if (patched || !exe)
	{
		return;
	}

	sin_vieworg = (const float *)vr_hook_data_address(exe, SIN_USES_VIEWORG, addss_xmm1, sizeof(addss_xmm1));
	sin_viewyaw = (const float *)vr_hook_data_address(exe, SIN_USES_VIEWYAW, addss_xmm1, sizeof(addss_xmm1));
	sin_command_entity = (entity_t *const *)vr_hook_data_address(exe, SIN_SETS_COMMAND_ENTITY, mov_to_memory,
		sizeof(mov_to_memory));
	sin_bone_scale = (const float *)vr_hook_data_address(exe, SIN_READS_BONE_SCALE, movss_xmm0,
		sizeof(movss_xmm0));
	if (sin_bone_scale && fabsf(*sin_bone_scale - 0.3f) > 0.001f)
	{
		sin_bone_scale = NULL;
	}
	if (!sin_bone_scale)
	{
		vr_printf("VR: this sin.exe is not the build the weapon code knows - the muzzle flash and shell "
			"cases will not follow a weapon in the hand exactly");
	}

	if (!sin_vieworg || !sin_viewyaw || !sin_command_entity || sin_viewyaw != sin_vieworg + 4)
	{
		vr_printf("VR: this sin.exe is not the build the weapon code knows - the weapon will be moved "
			"to the hand, but its muzzle flash and tracers will stay at the eye");
		return;
	}

	if (!vr_hook_call(exe, SIN_ADD_VIEW_WEAPON_CALL, SIN_ADD_ENTITY, (void *)add_view_weapon,
			(void **)&real_add_entity))
	{
		vr_printf("VR: could not reach the client's view weapon - its effects will stay at the eye");
		return;
	}

	if (!vr_hook_call(exe, SIN_SHOOT_BULLETS_CALL, SIN_SHOOT_BULLETS, (void *)shoot_bullets, &real_shoot_bullets))
	{
		vr_unhook_calls();
		vr_printf("VR: could not reach the client's bullet impacts - its effects will stay at the eye");
		return;
	}

	if (!vr_hook_call(exe, SIN_VIEW_COMMANDS_CALL, SIN_PROCESS_COMMANDS, (void *)process_view_commands,
			&real_process_commands))
	{
		vr_printf("VR: could not reach the view weapon's commands - a muzzle flash SiN moves off the barrel "
			"will stay moved");
	}

	patched = 1;
	vrlog("VR: the client's view weapon and bullet impacts go through the weapon code%s",
		sin_bone_scale ? ", and its flash and shell cases are placed at its true size" : "");
}

void vr_weapon_uninstall(void)
{
	if (!patched)
	{
		return;
	}

	vr_unhook_calls();
	patched = 0;
	vrlog("VR: the view weapon was placed by the client %u times; %u of its bullet impacts were left "
		"to the server; %u punches were thrown; the other hand took hold %u times; its effects were placed "
		"at its true size %u times; of its %u commands, %u flashes were put back on the barrel; %u shots and "
		"%u punches' hits were felt", client_placements, bullets_left_out, punches_thrown, steady.count,
		effects_placed, view_commands, flash_offsets_dropped, shots_felt, hits_felt);
}

/* ------------------------------------------------------------------------- */
/* In RenderFrame, when sin.exe could not be reached                          */
/* ------------------------------------------------------------------------- */

#define MAX_FRAME_ENTITIES 256

static entity_t frame_entities[MAX_FRAME_ENTITIES + 1];
static entity_t *engine_entities;
static int engine_entity_count;

void vr_weapon_to_hand(refdef_t *fd)
{
	int i, count;
	entity_t *gun = NULL;

	engine_entities = NULL;
	if (!fd)
	{
		return;
	}

	/* The engine's own view, which is what the hands were measured from: kept
	   whether or not there is anything to move, for drawing them later. */
	memcpy(base_origin, fd->vieworg, sizeof(base_origin));
	base_yaw = fd->viewangles[YAW];
	base_valid = 1;

	if (!fd->entities || fd->num_entities <= 0)
	{
		return;
	}

	for (i = 0; i < fd->num_entities; ++i)
	{
		if ((fd->entities[i].flags & RF_WEAPONMODEL) && !is_other_hand(&fd->entities[i]))
		{
			gun = &fd->entities[i];
			see_weapon(gun);
			break;
		}
	}

	/* The client did it, or there is nothing to do. */
	if (!gun || !placing || !placed[gun_hand()].valid || (sin_patched() && placed_this_frame))
	{
		return;
	}

	count = fd->num_entities;
	vr_gl_clear_models();
	if (count < MAX_FRAME_ENTITIES)
	{
		/* The engine's list is its own, and full size is not known, so the
		   other hand goes into a copy of it, for this eye only: the fists'
		   second arm, or the fist of the hand without the weapon. */
		entity_t other;
		int extra = 0;

		if (two_arms() && placed[other_hand()].valid)
		{
			other = *gun;
			put_in_hand(&other, &placed[other_hand()], fd->vieworg, fd->viewangles[YAW]);
			keep_one_arm(&other, other_hand());
			hold_idle(&other);
			extra = 1;
		}
		else
		{
			entity_t placed_gun = *gun;

			put_in_hand(&placed_gun, &placed[gun_hand()], fd->vieworg, fd->viewangles[YAW]);
			extra = dress_other_hand(&other, &placed_gun, fd->vieworg, fd->viewangles[YAW]);
			other.id = 0x1F000001; /* nothing is attached to it: an id nothing else has */
		}

		if (extra)
		{
			memcpy(frame_entities, fd->entities, count * sizeof(entity_t));
			gun = &frame_entities[gun - fd->entities];
			frame_entities[count] = other;

			engine_entities = fd->entities;
			engine_entity_count = fd->num_entities;
			fd->entities = frame_entities;
			fd->num_entities = count + 1;
		}
	}

	put_in_hand(gun, &placed[gun_hand()], fd->vieworg, fd->viewangles[YAW]);
	if (two_arms())
	{
		keep_one_arm(gun, gun_hand());
	}
	else if (mirror_weapon())
	{
		vr_gl_mirror_model_at(gun->origin);
	}
	hide_modelled_arm(gun);
	hold_idle(gun);
	placed_this_frame = 1;
	say_placed("moved in the renderer");
}

void vr_weapon_after_eye(refdef_t *fd)
{
	if (fd && engine_entities)
	{
		fd->entities = engine_entities;
		fd->num_entities = engine_entity_count;
	}
	engine_entities = NULL;
}
