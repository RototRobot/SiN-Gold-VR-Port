#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "refapi.h"
#include "ref_vr.h"
#include "vr_gl.h"
#include "vr_hook.h"
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
 */
typedef struct
{
	const char *name; /* models/view_<name>.def */
	float grip[3];
	float scale;
	int two_arms;
} weapon_info_t;

static const weapon_info_t weapon_table[] = {
	{ "punch",     { 18.5f, -11.0f, -10.0f }, 0.50f, 1 },
	{ "magnum",    { 53.0f, -36.0f, -31.0f }, 0.35f, 0 },
	{ "shotgun",   {  8.0f, -14.0f, -15.0f }, 0.60f, 0 },
	{ "asrifle",   { 37.0f, -21.0f, -30.0f }, 0.35f, 0 },
	{ "hvgun",     { 30.0f, -25.0f, -38.0f }, 0.35f, 0 },
	{ "rlaunch",   { 28.0f, -24.0f, -36.0f }, 0.35f, 0 },
	{ "srifle",    { 15.0f, -13.0f, -14.0f }, 0.35f, 0 },
	{ "pulse2",    { 25.0f, -18.0f, -30.0f }, 0.35f, 0 },
	{ "quantum",   { 20.0f, -30.0f, -40.0f }, 0.30f, 0 },
	{ "spgun",     { 30.0f, -15.0f, -30.0f }, 0.35f, 0 },
	{ "mines",     { 36.0f, -22.0f, -18.0f }, 0.35f, 0 },
	{ "detonator", { 15.0f,  -5.0f, -12.0f }, 0.40f, 0 }
};

static const weapon_info_t weapon_other = { NULL, { 30.0f, -20.0f, -25.0f }, 0.35f, 0 };

typedef struct
{
	char key[40];                /* "asrifle", from models/view_asrifle.def */
	const weapon_info_t *info;
	cvar_t *grip[2];             /* vr_grip_<key>, and vr_grip_<key>_left for two arms */
} weapon_t;

static weapon_t weapons[48];
static int weapon_count;
static weapon_t *current;        /* the view weapon last seen */

static cvar_t *vr_weapon_hand;
static cvar_t *vr_weapon_client;
static cvar_t *vr_client_bullets;
static cvar_t *vr_weapon_reset;
static cvar_t *vr_aim;

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

void vr_weapon_startup(void)
{
	int i;

	vr_weapon_hand = vr_cvar("vr_weapon_hand", "1", CVAR_ARCHIVE);
	vr_weapon_client = vr_cvar("vr_weapon_client", "1", 0);
	vr_client_bullets = vr_cvar("vr_client_bullets", "0", CVAR_ARCHIVE);
	vr_weapon_reset = vr_cvar("vr_weapon_reset", "0", 0);
	vr_aim = vr_cvar("vr_aim", "1", 0);

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
static int placing;            /* between a publish and the end of that frame */
static int placed_this_frame;  /* the weapon was put in the hand this frame */
static int said_placed;

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

/* The model's pose in the hand's frame, and its size: from vr_grip_<weapon> if
   that holds a setting, or else from the table. */
static void grip_setting(const weapon_t *w, int hand, vr_pose_t *relative, float *scale)
{
	const cvar_t *cvar = w->grip[hand];
	float origin[3], angles[3], s;
	int i;

	if (cvar && cvar->string &&
		sscanf(cvar->string, "%f %f %f %f %f %f %f", &origin[0], &origin[1], &origin[2],
			&angles[0], &angles[1], &angles[2], &s) == 7 &&
		s >= SCALE_MIN && s <= SCALE_MAX)
	{
		vr_pose_set(relative, origin, angles);
		*scale = s;
		return;
	}

	/* The grip point goes where the hand is, the model pointing where it points. */
	*scale = w->info->scale;
	for (i = 0; i < 3; ++i)
	{
		origin[i] = -w->info->scale * w->info->grip[i];
	}
	if (hand == LEFT)
	{
		origin[1] = -origin[1];
	}
	angles[PITCH] = angles[YAW] = angles[ROLL] = 0.0f;
	vr_pose_set(relative, origin, angles);
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
	vr_pose_t relative;
	float angles[3];
	char value[160];

	if (!w->grip[hand] || !hands[hand].valid)
	{
		return;
	}

	log_pose(hand == LEFT ? "left hand" : "right hand", &hands[hand].frame);
	log_pose("weapon", &p->pose);
	vr_pose_relative(&hands[hand].frame, &p->pose, &relative);
	vr_pose_angles(&relative, angles);
	sprintf(value, "%.2f %.2f %.2f %.1f %.1f %.1f %.3f", relative.origin[0], relative.origin[1],
		relative.origin[2], angles[PITCH], angles[YAW], angles[ROLL], p->scale);
	vr_cvar_set(w->grip[hand]->name, value);
	vr_printf("VR: the %s is held that way from now on: %s \"%s\"", w->key, w->grip[hand]->name, value);
}

void vr_weapon_hands_begin(void)
{
	hands[LEFT].valid = 0;
	hands[RIGHT].valid = 0;
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

/* The grip button: held long enough, the weapon stays where it is; let go,
   and it is taken back the way the hands now hold it. */
static void run_adjust(void)
{
	DWORD now = GetTickCount();
	int hand;

	if (!adjust.active)
	{
		if (!adjust.button || now - adjust.pressed_at < ADJUST_HOLD_MS || !current || !placed[RIGHT].valid)
		{
			return;
		}

		adjust.active = 1;
		adjust.last_tick = now;
		for (hand = 0; hand < 2; ++hand)
		{
			adjust.frozen[hand] = placed[hand];
			memcpy(adjust.anchor[hand], hands[hand].frame.origin, sizeof(adjust.anchor[hand]));
		}
		vr_printf("VR: adjusting the %s - hold it the way you want it and let go of the grip; "
			"the right stick sizes it", current->key);
		log_pose("right hand", &hands[RIGHT].frame);
		log_pose("weapon", &placed[RIGHT].pose);
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
		save_grip(current, RIGHT, &adjust.frozen[RIGHT]);
		if (two_arms() && adjust.frozen[LEFT].valid)
		{
			save_grip(current, LEFT, &adjust.frozen[LEFT]);
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
	vr_printf("VR: the %s is back to its first guess", current->key);
}

static int sin_patched(void);
static int wants_server_bullets(void);

void vr_weapon_publish(void)
{
	int hand, flags = 0;
	float angles[3];

	placed[LEFT].valid = placed[RIGHT].valid = 0;
	placing = 0;

	reset_requested();

	if (!vr_weapon_hand || vr_weapon_hand->value == 0.0f || !hands[RIGHT].valid || !current)
	{
		adjust.active = 0;
		return;
	}

	for (hand = 0; hand < 2; ++hand)
	{
		vr_pose_t relative;

		if (!hands[hand].valid || (hand == LEFT && !two_arms()))
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

	placing = 1;

	/* The client's own impacts are left out only when the game will aim from
	   the barrel, and so send the server's instead. */
	if (sin_patched() && wants_server_bullets())
	{
		flags |= SINVR_CLIENT_BULLETS_OFF;
	}

	vr_pose_angles(&placed[RIGHT].pose, angles);
	vr_poses_weapon(placed[RIGHT].pose.origin, angles, placed[RIGHT].scale, flags);
}

void vr_weapon_end_frame(void)
{
	placing = 0;
	placed_this_frame = 0;
	vr_gl_clear_clips();
}

int vr_weapon_in_hand(void)
{
	return placed_this_frame;
}

/* The weapon, moved from the eye to the hand: origin, angles, the orientation
   matrix SiN's bone code reads, and the size it is drawn and its bones placed
   at. There is no interpolating from where the client had it. */
static void put_in_hand(entity_t *e, const placement_t *p, const float vieworg[3], float yaw)
{
	vr_pose_t world;

	vr_pose_to_world(&p->pose, vieworg, yaw, &world);
	memcpy(e->origin, world.origin, sizeof(e->origin));
	memcpy(e->oldorigin, world.origin, sizeof(e->oldorigin));
	memcpy(e->axis, world.axis, sizeof(e->axis));
	vr_axis_to_angles(world.axis, e->angles);
	e->scale = p->scale;
}

/*
 * One arm of a two-armed model: the other is cut away down the model's middle,
 * the plane of its own forward and up. The entity's group flags cannot do it -
 * they only reach the groups a model's .def names, and the fists' arms are not
 * among them - so it is done where ref_gl draws it (vr_gl.c).
 */
static void keep_one_arm(const entity_t *e, int hand)
{
	float plane[4];
	const float *left = e->axis[1];
	float sign = hand == LEFT ? 1.0f : -1.0f;

	plane[0] = sign * left[0];
	plane[1] = sign * left[1];
	plane[2] = sign * left[2];
	plane[3] = -sign * (left[0] * e->origin[0] + left[1] * e->origin[1] + left[2] * e->origin[2]);
	vr_gl_clip_model_at(e->origin, plane);
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
		vrlog("VR: the view weapon is now %s (%s)", (const char *)e->model,
			w->info == &weapon_other ? "no measurements - a general guess" : "measured");
	}
}

static void say_placed(const char *how)
{
	if (!said_placed && current)
	{
		float angles[3];

		said_placed = 1;
		vr_pose_angles(&placed[RIGHT].pose, angles);
		vr_printf("VR: the %s is in the right hand%s, %s: (%.1f %.1f %.1f) from the eyes, "
			"pointing (%.1f %.1f %.1f), size %.2f", current->key, two_arms() ? " and the left" : "", how,
			placed[RIGHT].pose.origin[0], placed[RIGHT].pose.origin[1], placed[RIGHT].pose.origin[2],
			angles[PITCH], angles[YAW], angles[ROLL], placed[RIGHT].scale);
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

/* The absolute address an instruction at rva uses, after opcode_length bytes
   that must be exactly opcode. */
static const void *address_in(const BYTE *base, DWORD size, DWORD rva, const BYTE *opcode, int opcode_length)
{
	const BYTE *at = base + rva;
	DWORD address;

	if (rva + opcode_length + 4 > size || memcmp(at, opcode, opcode_length) != 0)
	{
		return NULL;
	}

	memcpy(&address, at + opcode_length, sizeof(address));
	if (address < (DWORD)(DWORD_PTR)base || address + 12 > (DWORD)(DWORD_PTR)base + size)
	{
		return NULL;
	}

	return (const void *)(DWORD_PTR)address;
}

/* Where CL_AddViewWeapon hands its entity over. */
static void __fastcall add_view_weapon(entity_t *gun)
{
	int active = placing && sin_patched();
	float yaw;

	if (gun)
	{
		see_weapon(gun);
	}

	if (!active || !gun || !placed[RIGHT].valid)
	{
		keep_client_bullets = 1;
		real_add_entity(gun);
		return;
	}

	yaw = *sin_viewyaw;
	vr_gl_clear_clips();
	if (two_arms() && placed[LEFT].valid)
	{
		entity_t left = *gun;

		put_in_hand(&left, &placed[LEFT], sin_vieworg, yaw);
		keep_one_arm(&left, LEFT);
		real_add_entity(&left);
	}

	put_in_hand(gun, &placed[RIGHT], sin_vieworg, yaw);
	if (two_arms())
	{
		keep_one_arm(gun, RIGHT);
	}
	real_add_entity(gun);

	/* The entity goes on to the model's client commands from here. */
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
	HMODULE exe = GetModuleHandleA(NULL);
	const BYTE *base = (const BYTE *)exe;
	const IMAGE_NT_HEADERS *nt;
	DWORD size;

	if (patched || !exe)
	{
		return;
	}

	nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	size = nt->OptionalHeader.SizeOfImage;

	sin_vieworg = (const float *)address_in(base, size, SIN_USES_VIEWORG, addss_xmm1, sizeof(addss_xmm1));
	sin_viewyaw = (const float *)address_in(base, size, SIN_USES_VIEWYAW, addss_xmm1, sizeof(addss_xmm1));
	sin_command_entity = (entity_t *const *)address_in(base, size, SIN_SETS_COMMAND_ENTITY, mov_to_memory,
		sizeof(mov_to_memory));

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

	patched = 1;
	vrlog("VR: the client's view weapon and bullet impacts go through the weapon code");
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
		"to the server", client_placements, bullets_left_out);
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
	if (!fd || !fd->entities || fd->num_entities <= 0)
	{
		return;
	}

	for (i = 0; i < fd->num_entities; ++i)
	{
		if (fd->entities[i].flags & RF_WEAPONMODEL)
		{
			gun = &fd->entities[i];
			see_weapon(gun);
			break;
		}
	}

	/* The client did it, or there is nothing to do. */
	if (!gun || !placing || !placed[RIGHT].valid || (sin_patched() && placed_this_frame))
	{
		return;
	}

	count = fd->num_entities;
	vr_gl_clear_clips();
	if (two_arms() && placed[LEFT].valid && count < MAX_FRAME_ENTITIES)
	{
		/* The engine's list is its own, and full size is not known, so the
		   second arm goes into a copy of it, for this eye only. */
		memcpy(frame_entities, fd->entities, count * sizeof(entity_t));
		gun = &frame_entities[gun - fd->entities];
		frame_entities[count] = *gun;
		put_in_hand(&frame_entities[count], &placed[LEFT], fd->vieworg, fd->viewangles[YAW]);
		keep_one_arm(&frame_entities[count], LEFT);

		engine_entities = fd->entities;
		engine_entity_count = fd->num_entities;
		fd->entities = frame_entities;
		fd->num_entities = count + 1;
	}

	put_in_hand(gun, &placed[RIGHT], fd->vieworg, fd->viewangles[YAW]);
	if (two_arms())
	{
		keep_one_arm(gun, RIGHT);
	}
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
