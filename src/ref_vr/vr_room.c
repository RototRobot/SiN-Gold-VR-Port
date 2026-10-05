#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ref_vr.h"
#include "vr_hook.h"
#include "vr_room.h"
#include "vr_view.h"
#include "vrlog.h"
#include "../shared/sinvr_poses.h"

/*
 * Roomscale: walking in the room walks the player, and crouching crouches.
 *
 * The view has always followed the head (vr_view.c), but the body - the box the
 * game collides, the point a snap turn pivots on - stayed wherever the stick
 * left it. So each movement command the client builds now also carries the
 * step the head has taken away from the body, and the engine's Pmove is
 * followed by that step, swept through the world as any move is: stopped by
 * walls, up and down stairs, kept on the ground.
 *
 * The client predicts movement by running Pmove again over every command the
 * server has not answered yet, and the server runs it once more for real
 * (through the game module and SinVrPmove), so a command's step has to be the
 * same every time it is run. It is looked up by a number the command carries
 * in its roll angle, which SiN's client always sends as 0, and which the game
 * module takes out again before anything reads it (player.cpp).
 *
 * The first time the client takes a step, the point the head is measured from
 * moves with it (vr_view_shift), so the view stays exactly where the head is
 * and the body ends up under it. A step the world refuses - a wall - is left
 * between the two, up to vr_lean; past that the room slides instead, so a
 * head pushed into a wall stops short of it.
 *
 * Crouching: with the head far enough below where it was recentred, the
 * command asks to crouch, and the game holds the view at standing height
 * (player.cpp, CalcViewOffset) so that the head's own height is the eye's.
 * The crouch button still lowers the view as SiN always did, and the eye is
 * kept out of whatever is above the player.
 */

/*
 * Measured on the Steam build of sin.exe, as offsets from its base.
 *
 * CL_SendCmd (+0xF070) builds the command on its stack and finishes it with
 * CL_FinishMove (+0xEC50), which takes it in ecx and writes the angles from
 * cl.viewangles - ANGLE2SHORT of each, roll included - then the impulse and
 * the light level. It is called once per command, at +0xF11B.
 *
 * CL_PredictMovement (+0x15DE0) copies each command the server has not
 * acknowledged into a pmove_t on its stack and calls Pmove (+0x84EE0) on it,
 * at +0x15F2D, the one direct call there is; the server's own goes through
 * the game module's gi.Pmove. CL_Frame calls CL_SendCmd (+0x12DC1) and then
 * CL_PredictMovement (+0x12DCB) before it draws, so a command is predicted
 * for the first time in the frame it is built.
 */
#define SIN_FINISH_MOVE_CALL 0xF11B
#define SIN_FINISH_MOVE 0xEC50
#define SIN_PREDICT_PMOVE_CALL 0x15F2D
#define SIN_PMOVE 0x84EE0

/* SiN's q_shared.h, as sin.exe's code has it laid out (the asserts below). */
typedef struct
{
	BYTE msec;
	BYTE buttons;
	short angles[3];
	short forwardmove, sidemove, upmove;
	BYTE impulse;
	BYTE lightlevel;
	BYTE debris;
} sin_usercmd_t;

typedef struct
{
	float normal[3];
	float dist;
	BYTE type;
	BYTE signbits;
	BYTE pad[2];
} sin_plane_t;

typedef struct
{
	int allsolid;
	int startsolid;
	float fraction;
	float endpos[3];
	sin_plane_t plane;
	void *surface;
	int contents;
	void *ent;
	float dir[3];
	int intersect_valid, intersect_group, intersect_parent, intersect_triangle;
	float intersect_position[3], intersect_normal[3], intersect_damage;
} sin_trace_t;

typedef sin_trace_t (*sin_trace_fn)(float *start, float *mins, float *maxs, float *end);

typedef struct
{
	int pm_type;
	short origin[3];      /* eighths of a unit */
	short velocity[3];
	short pm_flags;
	BYTE pm_time;
	short gravity;        /* a 13-bit field in its own short */
	unsigned gravity_axis;
	short delta_angles[3];
} sin_pmove_state_t;

typedef struct
{
	sin_pmove_state_t s;
	sin_usercmd_t cmd;
	int snapinitial;
	int numtouch;
	void *touchents[32];
	float viewangles[3];
	float viewheight;
	float mins[3], maxs[3];
	void *groundentity;
	void *groundsurface;
	sin_plane_t groundplane;
	int groundcontents;
	int watertype;
	int waterlevel;
	int onladder;
	sin_trace_fn trace;
	int (*pointcontents)(float *point);
} sin_pmove_t;

/* CL_PredictMovement clears 0x11C bytes for it, keeps the command at +0x24
   and the trace at +0x114; Pmove reads the gravity axis at +0x18 and fills in
   the view angles at +0xC0; CL_PMTrace copies 0x70 bytes of trace out. */
C_ASSERT(sizeof(sin_usercmd_t) == 18);
C_ASSERT(sizeof(sin_pmove_state_t) == 0x24);
C_ASSERT(FIELD_OFFSET(sin_pmove_t, cmd) == 0x24);
C_ASSERT(FIELD_OFFSET(sin_pmove_t, viewangles) == 0xC0);
C_ASSERT(FIELD_OFFSET(sin_pmove_t, mins) == 0xD0);
C_ASSERT(FIELD_OFFSET(sin_pmove_t, trace) == 0x114);
C_ASSERT(sizeof(sin_pmove_t) == 0x11C);
C_ASSERT(sizeof(sin_trace_t) == 0x70);

typedef void (__cdecl *pmove_fn)(sin_pmove_t *pm);

#define PM_NORMAL 0
#define PM_ZOOM 1

#define PMF_DUCKED 0x1
#define PMF_ON_GROUND 0x4
#define PMF_TIME_TELEPORT 0x20

#define STAND_EYE_HEIGHT 66.0f
#define CROUCH_EYE_HEIGHT 30.0f
#define STEP_HEIGHT 18.0f        /* g_phys.h's STEPSIZE */
#define CROUCH_UPMOVE (-200)     /* cl_upspeed's default, which is what the crouch key sends */

#define DEG_TO_RAD 0.017453292519943295

#define KEY_COUNT 256            /* the numbers a command carries: 1-255, 0 for none */
#define LONGEST_STEP 16.0f       /* in one command, game units */
#define SHORTEST_STEP 0.125f     /* an eighth, which is as fine as the engine keeps a position */
#define CEILING_GAP 5.0f         /* between the eye and what is above it: the near plane is at 4 */
#define LOWER_PER_SECOND 360.0f  /* the crouch button's drop, over a tenth of a second */
#define CROUCH_RELEASE 0.08f     /* metres back up before a crouch in the room ends */
#define NECK_FORWARD 0.08f       /* metres from the neck to the eyes, along the head's forward */
#define NECK_UP 0.11f            /* and along its up */
#define STALE_MS 500

typedef struct
{
	float move[2];  /* in the world, game units */
	float yaw;      /* the body's, when the step was turned into the world */
	int followed;   /* the client has taken it, and the view has followed */
} step_t;

static step_t steps[KEY_COUNT];
static int next_key = 1;

static cvar_t *vr_roomscale;
static cvar_t *vr_lean;
static cvar_t *vr_crouch;
static cvar_t *vr_crouch_depth;
static cvar_t *vr_fake_step;

static int installed;
static void *real_finish_move;
static pmove_fn real_pmove;

static struct
{
	int valid;
	float position[3];
	float orientation[4];
} head;

/* What the client's newest prediction left the body as. */
static struct
{
	int moving;       /* an ordinary move, on foot */
	int ducked;
	int in_water;
	int on_ladder;
	float headroom;   /* from the origin up to whatever is above, or FLT_MAX */
	float origin[3];
} body;

static DWORD client_ran;    /* when the client last predicted a move */
static DWORD server_ran;    /* when the game module last ran one: it takes steps */
static DWORD last_frame;
static int crouching;
static float lowered;

static unsigned steps_asked, steps_taken, steps_run_by_server, steps_refused, room_slid, crouches;
static float walked;
static int said_box;

void vr_room_startup(void)
{
	vr_roomscale = vr_cvar("vr_roomscale", "1", CVAR_ARCHIVE);
	vr_lean = vr_cvar("vr_lean", "0.3", CVAR_ARCHIVE);
	vr_crouch = vr_cvar("vr_crouch", "1", CVAR_ARCHIVE);
	vr_crouch_depth = vr_cvar("vr_crouch_depth", "0.35", CVAR_ARCHIVE);
	vr_fake_step = vr_cvar("vr_fake_step", "", 0);
}

static int recent(DWORD when)
{
	return when && GetTickCount() - when < STALE_MS;
}

/* Steps are taken only while both sides of the game are running them. */
static int room_on(void)
{
	return vr_roomscale && vr_roomscale->value != 0.0f && head.valid && recent(client_ran) &&
		recent(server_ran) && !vr_in_menu();
}

/* ------------------------------------------------------------------------- */
/* Sweeping the body through the world                                         */
/* ------------------------------------------------------------------------- */

static sin_trace_t trace(sin_pmove_t *pm, const float start[3], const float end[3])
{
	float from[3], to[3];

	memcpy(from, start, sizeof(from));
	memcpy(to, end, sizeof(to));
	return pm->trace(from, pm->mins, pm->maxs, to);
}

static float flat_distance(const float a[3], const float b[3])
{
	return sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
}

/* From start along move, sliding along whatever it meets; a corner stops it. */
static void slide(sin_pmove_t *pm, const float start[3], const float move[3], float end[3])
{
	float at[3], left[3], to[3], met[4][3];
	int bump, i, j, count = 0;
	sin_trace_t tr;

	memcpy(at, start, sizeof(at));
	memcpy(left, move, sizeof(left));

	for (bump = 0; bump < 4; ++bump)
	{
		float into;

		if (left[0] * left[0] + left[1] * left[1] + left[2] * left[2] < 0.0001f)
		{
			break;
		}

		for (i = 0; i < 3; ++i)
		{
			to[i] = at[i] + left[i];
		}
		tr = trace(pm, at, to);
		if (tr.allsolid)
		{
			break;
		}
		memcpy(at, tr.endpos, sizeof(at));
		if (tr.fraction >= 1.0f)
		{
			break;
		}

		/* What is left of it, along the surface: a little off it, so the next
		   sweep does not start against it. */
		into = 0.0f;
		for (i = 0; i < 3; ++i)
		{
			left[i] *= 1.0f - tr.fraction;
			into += left[i] * tr.plane.normal[i];
		}
		for (i = 0; i < 3; ++i)
		{
			left[i] -= tr.plane.normal[i] * into * 1.01f;
		}

		for (j = 0; j < count; ++j)
		{
			if (left[0] * met[j][0] + left[1] * met[j][1] + left[2] * met[j][2] < 0.0f)
			{
				left[0] = left[1] = left[2] = 0.0f;
			}
		}
		memcpy(met[count++], tr.plane.normal, sizeof(met[0]));
	}

	memcpy(end, at, sizeof(at));
}

/* A step as a foot would take it: on the ground it climbs a stair it meets
   and comes down the other side, and stays down on the way down. */
static void walk(sin_pmove_t *pm, const float start[3], const float move[3], float end[3])
{
	int on_ground = (pm->s.pm_flags & PMF_ON_GROUND) != 0;
	float flat[3], up[3], over[3], down[3];
	float wanted = sqrtf(move[0] * move[0] + move[1] * move[1]);
	sin_trace_t tr;

	slide(pm, start, move, flat);
	memcpy(end, flat, sizeof(flat));

	if (on_ground && flat_distance(start, flat) < wanted - 0.01f)
	{
		memcpy(up, start, sizeof(up));
		up[2] += STEP_HEIGHT;
		tr = trace(pm, start, up);
		if (!tr.allsolid)
		{
			memcpy(up, tr.endpos, sizeof(up));
			slide(pm, up, move, over);
			memcpy(down, over, sizeof(down));
			down[2] -= up[2] - start[2];
			tr = trace(pm, over, down);
			if (!tr.allsolid && tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f &&
				flat_distance(start, tr.endpos) > flat_distance(start, flat) + 0.01f)
			{
				memcpy(end, tr.endpos, sizeof(tr.endpos));
			}
		}
	}

	if (on_ground)
	{
		memcpy(down, end, sizeof(down));
		down[2] -= STEP_HEIGHT;
		tr = trace(pm, end, down);
		if (!tr.allsolid && tr.fraction > 0.0f && tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f)
		{
			memcpy(end, tr.endpos, sizeof(tr.endpos));
		}
	}
}

/* Onto the eighths the engine keeps a position in, somewhere the box fits:
   the nearest corner of the grid first, then the others. */
static int onto_grid(sin_pmove_t *pm, const float at[3])
{
	int nearer[3], farther[3], i, corner;

	for (i = 0; i < 3; ++i)
	{
		float eighths = at[i] * 8.0f;
		int below = (int)floorf(eighths);

		nearer[i] = eighths - below < 0.5f ? below : below + 1;
		farther[i] = nearer[i] == below ? below + 1 : below;
		if (below < -32767 || below + 1 > 32767)
		{
			return 0;
		}
	}

	for (corner = 0; corner < 8; ++corner)
	{
		float try_at[3];
		sin_trace_t tr;

		for (i = 0; i < 3; ++i)
		{
			try_at[i] = ((corner >> i) & 1 ? farther[i] : nearer[i]) * 0.125f;
		}
		tr = trace(pm, try_at, try_at);
		if (!tr.allsolid && !tr.startsolid)
		{
			for (i = 0; i < 3; ++i)
			{
				pm->s.origin[i] = (short)((corner >> i) & 1 ? farther[i] : nearer[i]);
			}
			return 1;
		}
	}

	return 0;
}

static void take_step(sin_pmove_t *pm, const float move[2], float taken[2])
{
	float start[3], end[3], step[3];
	short was[3];
	int i;

	for (i = 0; i < 3; ++i)
	{
		was[i] = pm->s.origin[i];
		start[i] = was[i] * 0.125f;
	}
	step[0] = move[0];
	step[1] = move[1];
	step[2] = 0.0f;

	walk(pm, start, step, end);
	if (!onto_grid(pm, end))
	{
		memcpy(pm->s.origin, was, sizeof(was));
	}

	taken[0] = (pm->s.origin[0] - was[0]) * 0.125f;
	taken[1] = (pm->s.origin[1] - was[1]) * 0.125f;
}

/* From the middle of the body straight up, as wide as a head: how much room
   there is above the origin, as far as a standing eye and a little more. */
static float headroom(sin_pmove_t *pm)
{
	static float mins[3] = { -4.0f, -4.0f, 0.0f };
	static float maxs[3] = { 4.0f, 4.0f, 0.0f };
	float from[3], to[3];
	sin_trace_t tr;
	int i;

	for (i = 0; i < 3; ++i)
	{
		from[i] = to[i] = pm->s.origin[i] * 0.125f;
	}
	from[2] += 0.5f * (pm->mins[2] + pm->maxs[2]);
	to[2] += STAND_EYE_HEIGHT + 16.0f;
	if (to[2] <= from[2])
	{
		return FLT_MAX;
	}

	tr = pm->trace(from, mins, maxs, to);
	if (tr.allsolid || tr.startsolid || tr.fraction >= 1.0f)
	{
		return FLT_MAX;
	}
	return tr.endpos[2] - pm->s.origin[2] * 0.125f;
}

/* The client has moved the body: the head is measured from it now. */
static void follow(const float taken[2], float yaw)
{
	double s = sin(-yaw * DEG_TO_RAD), c = cos(-yaw * DEG_TO_RAD);
	float by[3];

	if (taken[0] == 0.0f && taken[1] == 0.0f)
	{
		steps_refused++;
		return;
	}

	by[0] = (float)(taken[0] * c - taken[1] * s);
	by[1] = (float)(taken[0] * s + taken[1] * c);
	by[2] = 0.0f;
	vr_view_shift(by, vr_worldscale_units());

	if (!steps_taken++)
	{
		vrlog("VR: roomscale - the body took its first step after the head, %.2f units", sqrtf(taken[0] *
			taken[0] + taken[1] * taken[1]));
	}
	walked += sqrtf(taken[0] * taken[0] + taken[1] * taken[1]);
}

/* Pmove, then the command's step, for the client and the server alike. */
static void room_pmove(sin_pmove_t *pm, int key, pmove_fn pmove, int client)
{
	int moving;

	pmove(pm);

	moving = (pm->s.pm_type == PM_NORMAL || pm->s.pm_type == PM_ZOOM) && pm->trace;
	if (moving && key > 0 && key < KEY_COUNT && !(pm->s.pm_flags & PMF_TIME_TELEPORT))
	{
		float taken[2];

		take_step(pm, steps[key].move, taken);
		if (client && !steps[key].followed)
		{
			steps[key].followed = 1;
			follow(taken, steps[key].yaw);
		}
		if (!client)
		{
			steps_run_by_server++;
		}
	}

	if (!client)
	{
		return;
	}

	client_ran = GetTickCount();
	body.moving = moving;
	body.ducked = (pm->s.pm_flags & PMF_DUCKED) != 0;
	body.in_water = pm->waterlevel >= 2;
	body.on_ladder = pm->onladder != 0;
	body.headroom = moving ? headroom(pm) : FLT_MAX;
	body.origin[0] = pm->s.origin[0] * 0.125f;
	body.origin[1] = pm->s.origin[1] * 0.125f;
	body.origin[2] = pm->s.origin[2] * 0.125f;

	if (moving && !said_box)
	{
		said_box = 1;
		vrlog("VR: the player's box is (%.0f %.0f %.0f)-(%.0f %.0f %.0f), the eye %.0f above its origin",
			pm->mins[0], pm->mins[1], pm->mins[2], pm->maxs[0], pm->maxs[1], pm->maxs[2], pm->viewheight);
	}
}

/* The client's prediction: the command's number is in the copy it moves. */
static void __cdecl client_pmove(sin_pmove_t *pm)
{
	int key = pm->cmd.angles[2];

	pm->cmd.angles[2] = 0;
	room_pmove(pm, key > 0 && key < KEY_COUNT ? key : 0, real_pmove, 1);
}

/* The server's, from the game module, which has taken the number out already. */
__declspec(dllexport) void SinVrPmove(void *pm, int key, void (*pmove)(void *pm))
{
	server_ran = GetTickCount();
	room_pmove((sin_pmove_t *)pm, key, (pmove_fn)pmove, 0);
}

/* ------------------------------------------------------------------------- */
/* Each command                                                                */
/* ------------------------------------------------------------------------- */

/*
 * The point the head turns about, a little behind and below the eyes, from
 * the eyes and the head's angles in the recentred frame. Nodding, or looking
 * down at the floor, moves the eyes several centimetres but not this, so it is
 * what the body steps under and what a crouch is measured by. Its height is
 * given from where it is with the head level, so that it is 0 standing as the
 * player was recentred.
 */
static void neck_of(const float eyes[3], const float angles[3], float units, float neck[3])
{
	double sp = sin(angles[0] * DEG_TO_RAD), cp = cos(angles[0] * DEG_TO_RAD);
	double sy = sin(angles[1] * DEG_TO_RAD), cy = cos(angles[1] * DEG_TO_RAD);
	double sr = sin(angles[2] * DEG_TO_RAD), cr = cos(angles[2] * DEG_TO_RAD);
	double forward[3], up[3];
	int i;

	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
	up[0] = cr * sp * cy + sr * sy;
	up[1] = cr * sp * sy - sr * cy;
	up[2] = cr * cp;

	for (i = 0; i < 3; ++i)
	{
		neck[i] = (float)(eyes[i] - (forward[i] * NECK_FORWARD + up[i] * NECK_UP) * units);
	}
	neck[2] += NECK_UP * units;
}

static void crouch_in_room(float height_metres)
{
	float depth = vr_crouch_depth ? vr_crouch_depth->value : 0.35f;

	if (!vr_crouch || vr_crouch->value == 0.0f || vr_in_menu())
	{
		crouching = 0;
		return;
	}

	if (depth < 0.1f)
	{
		depth = 0.1f;
	}
	if (!crouching && height_metres < -depth)
	{
		crouching = 1;
		if (!crouches++)
		{
			vrlog("VR: crouching in the room, %.2f m below the recentred head", -height_metres);
		}
	}
	else if (crouching && height_metres > -depth + CROUCH_RELEASE)
	{
		crouching = 0;
	}
}

static void __cdecl command_built(sin_usercmd_t *cmd)
{
	float units = vr_worldscale_units(), eyes[3], angles[3], at[3], yaw, length;
	double s, c;
	int key;

	if (!cmd || !head.valid || units <= 0.0f)
	{
		return;
	}

	vr_view_local(head.position, head.orientation, units, eyes, angles);
	neck_of(eyes, angles, units, at);

	/* The neck's own height, not the lowered one. In the water the crouch
	   would swim down, and on a ladder climb down, so it is left out there. */
	crouch_in_room((at[2] + lowered) / units);
	if (crouching && !body.in_water && !body.on_ladder && cmd->upmove <= 0 && cmd->upmove > CROUCH_UPMOVE)
	{
		cmd->upmove = CROUCH_UPMOVE;
	}

	if (!room_on() || !vr_view_body_yaw(&yaw))
	{
		return;
	}

	length = sqrtf(at[0] * at[0] + at[1] * at[1]);
	if (length < SHORTEST_STEP)
	{
		return;
	}
	if (length > LONGEST_STEP)
	{
		at[0] *= LONGEST_STEP / length;
		at[1] *= LONGEST_STEP / length;
	}

	key = next_key;
	next_key = next_key % (KEY_COUNT - 1) + 1;

	s = sin(yaw * DEG_TO_RAD);
	c = cos(yaw * DEG_TO_RAD);
	steps[key].move[0] = (float)(at[0] * c - at[1] * s);
	steps[key].move[1] = (float)(at[0] * s + at[1] * c);
	steps[key].yaw = yaw;
	steps[key].followed = 0;
	cmd->angles[2] = (short)key;
	steps_asked++;
}

/* CL_FinishMove takes the command in ecx and leaves the stack alone; the
   caller may count on more of its registers than C would keep. */
static __declspec(naked) void finish_move(void)
{
	__asm {
		push ecx
		call real_finish_move
		pop ecx
		push eax
		push ecx
		push edx
		sub esp, 128
		movdqu [esp], xmm0
		movdqu [esp + 16], xmm1
		movdqu [esp + 32], xmm2
		movdqu [esp + 48], xmm3
		movdqu [esp + 64], xmm4
		movdqu [esp + 80], xmm5
		movdqu [esp + 96], xmm6
		movdqu [esp + 112], xmm7
		push ecx
		call command_built
		add esp, 4
		movdqu xmm0, [esp]
		movdqu xmm1, [esp + 16]
		movdqu xmm2, [esp + 32]
		movdqu xmm3, [esp + 48]
		movdqu xmm4, [esp + 64]
		movdqu xmm5, [esp + 80]
		movdqu xmm6, [esp + 96]
		movdqu xmm7, [esp + 112]
		add esp, 128
		pop edx
		pop ecx
		pop eax
		ret
	}
}

/* ------------------------------------------------------------------------- */
/* Each frame                                                                  */
/* ------------------------------------------------------------------------- */

/*
 * Desk testing: "vr_fake_step 1 0 0" moves the head a metre forward at once,
 * the way a step in the room would - x forward, y left, z up, in metres from
 * where it is - and leaves it there, so "vr_fake_step 0 0 -0.5" is a crouch
 * until the next recentre. A fourth number spreads it over that many seconds,
 * which is walking: "vr_fake_step 3 0 0 3" is three metres at a metre a second.
 * "vr_fake_step 0" moves nothing, and says where everything is.
 */
static struct
{
	float left[3];      /* metres still to go */
	float per_second[3];
} fake_walk;

static void fake_step(float seconds)
{
	float metres[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, by[3], engine[3], eye[3], units = vr_worldscale_units();
	int i, walking = 0;

	for (i = 0; i < 3; ++i)
	{
		float part = fake_walk.per_second[i] * seconds;

		if (fabsf(part) > fabsf(fake_walk.left[i]))
		{
			part = fake_walk.left[i];
		}
		fake_walk.left[i] -= part;
		by[i] = -part * units;
		walking |= part != 0.0f;
	}
	if (walking)
	{
		vr_view_shift(by, units);
	}

	if (!vr_fake_step || !vr_fake_step->string || !vr_fake_step->string[0])
	{
		return;
	}
	if (sscanf(vr_fake_step->string, "%f %f %f %f", &metres[0], &metres[1], &metres[2], &metres[3]) >= 1)
	{
		vr_view_last(engine, eye);
		vrlog("    room: a pretend step of (%.2f %.2f %.2f) m over %.1f s; the body is at (%.1f %.1f %.1f)%s, "
			"the eye at (%.1f %.1f) from it and %.1f above (the engine's %.1f), lowered %.1f, headroom %.0f",
			metres[0], metres[1], metres[2], metres[3], body.origin[0], body.origin[1], body.origin[2],
			body.ducked ? " ducked" : "", eye[0] - body.origin[0], eye[1] - body.origin[1],
			eye[2] - body.origin[2], engine[2] - body.origin[2], lowered,
			body.headroom < FLT_MAX ? body.headroom : -1.0f);
		for (i = 0; i < 3; ++i)
		{
			if (metres[3] > 0.0f)
			{
				fake_walk.left[i] = metres[i];
				fake_walk.per_second[i] = metres[i] / metres[3];
			}
			else
			{
				by[i] = -metres[i] * units;
			}
		}
		if (metres[3] <= 0.0f)
		{
			vr_view_shift(by, units);
		}
	}
	vr_cvar_set("vr_fake_step", "");
}

void vr_room_frame(const float position[3], const float orientation[4])
{
	DWORD now = GetTickCount();
	float units = vr_worldscale_units(), at[3], angles[3], height, target, change, seconds;

	seconds = last_frame ? (now - last_frame) * 0.001f : 0.0f;
	if (seconds > 0.1f)
	{
		seconds = 0.1f;
	}
	last_frame = now;

	memcpy(head.position, position, sizeof(head.position));
	memcpy(head.orientation, orientation, sizeof(head.orientation));
	head.valid = 1;

	fake_step(seconds);

	vr_view_local(position, orientation, units, at, angles);
	height = at[2] + lowered;

	/* Crouched by the button, or kept crouched by a low ceiling, rather than
	   by the player: down as SiN's own crouch goes, but no lower than its
	   crouching eye, so a player already low in the room drops less. */
	target = 0.0f;
	if (body.moving && body.ducked && !crouching && recent(client_ran))
	{
		target = STAND_EYE_HEIGHT + height - CROUCH_EYE_HEIGHT;
		target = target < 0.0f ? 0.0f : (target > STAND_EYE_HEIGHT - CROUCH_EYE_HEIGHT ?
			STAND_EYE_HEIGHT - CROUCH_EYE_HEIGHT : target);
	}
	change = LOWER_PER_SECOND * seconds;
	lowered = target > lowered ? (lowered + change < target ? lowered + change : target) :
		(lowered - change > target ? lowered - change : target);
	vr_view_set_lowered(lowered);

	/* Leaning: the eyes as far as vr_lean from the body, and then the room
	   comes too - while there is a body on its feet to lean from. */
	if (room_on() && body.moving)
	{
		float lean = (vr_lean ? vr_lean->value : 0.3f) * units;
		float length = sqrtf(at[0] * at[0] + at[1] * at[1]);

		if (lean < 0.0f)
		{
			lean = 0.0f;
		}
		if (length > lean && length > 0.0f)
		{
			float by[3];

			by[0] = at[0] * (1.0f - lean / length);
			by[1] = at[1] * (1.0f - lean / length);
			by[2] = 0.0f;
			vr_view_shift(by, units);
			room_slid++;
		}
	}

	/* The eye stops short of what is above the player. */
	if (body.moving && recent(client_ran) && body.headroom < FLT_MAX)
	{
		vr_view_set_ceiling(body.headroom - CEILING_GAP - STAND_EYE_HEIGHT);
	}
	else
	{
		vr_view_set_ceiling(FLT_MAX);
	}
}

/* ------------------------------------------------------------------------- */
/* Installing                                                                  */
/* ------------------------------------------------------------------------- */

void vr_room_install(void)
{
	HMODULE exe = GetModuleHandleA(NULL);

	if (installed || !exe)
	{
		return;
	}

	if (!vr_hook_call(exe, SIN_PREDICT_PMOVE_CALL, SIN_PMOVE, (void *)client_pmove, (void **)&real_pmove))
	{
		vr_printf("VR: this sin.exe is not the build the roomscale code knows - walking in the room will not "
			"move the player, and crouching in it will not crouch");
		return;
	}
	installed = 1;

	if (!vr_hook_call(exe, SIN_FINISH_MOVE_CALL, SIN_FINISH_MOVE, (void *)finish_move, &real_finish_move))
	{
		vr_printf("VR: could not reach the client's movement commands - walking in the room will not move "
			"the player, and crouching in it will not crouch");
		return;
	}

	vrlog("VR: roomscale reaches the client's movement commands and their prediction");
}

void vr_room_uninstall(void)
{
	if (!installed)
	{
		return;
	}

	vr_unhook_calls();
	installed = 0;
	vrlog("VR: roomscale asked for %u steps; the client took %u (%.0f units in all), the world refused %u, "
		"the server ran %u; the room slid after the head %u times, and the player crouched in it %u times",
		steps_asked, steps_taken, walked, steps_refused, steps_run_by_server, room_slid, crouches);
}
