/*
 * What the renderer tells the game about the player's head, hands and weapon.
 *
 * Both DLLs live in sin.exe, so there is no need for shared memory or a socket:
 * ref_vr.dll exports SinVrPoses, the game module asks the loaded modules for it
 * (see src/game_vr/vr_link.cpp), and reads the block it returns. When the game
 * runs without the VR renderer the export is simply not there, and the game
 * carries on as it always did.
 *
 * The frame of reference is the recentred one: the origin is where the game puts
 * the player's eyes (Sentient::EyePosition), +x is the way the player's body
 * faces, +y is to their left and +z is up, in game units. So the game turns a
 * pose into world space with the player's own yaw:
 *
 *     world = EyePosition() + rotate_yaw(origin, worldangles[YAW])
 *     angles = angles + (0, worldangles[YAW], 0)
 *
 * Nothing here is written while the game is reading it: one thread, and the
 * renderer writes the whole block between engine frames.
 */
#ifndef SINVR_POSES_H
#define SINVR_POSES_H

#define SINVR_POSES_VERSION 3
#define SINVR_HAND_LEFT 0
#define SINVR_HAND_RIGHT 1

/* The client leaves bullet impacts to the server: the game should send them
   (BulletWeapon::FireBullets' server_effects), or there will be none. */
#define SINVR_CLIENT_BULLETS_OFF 0x1

/* The weapon is drawn mirrored down its own middle, for a left-handed player:
   everything the game measures off the model is on the other side of it, so a
   bone's y - its distance to the model's left - changes sign. */
#define SINVR_WEAPON_MIRRORED 0x2

/* The view weapon's idle animation should be held still: it is being placed in
   the player's hand, or vr_freeze_idle asks for it. A weapon on a controller
   has the hand's own movement already, and breathing under it makes lining the
   two up harder. */
#define SINVR_WEAPON_STILL 0x4

typedef struct
{
	int version;             /* SINVR_POSES_VERSION */
	unsigned frame;          /* renderer frames so far; stops when VR stops */
	unsigned time_ms;        /* GetTickCount when this was written */
	float units_per_metre;   /* what the poses were scaled by */

	int head_valid;
	float head_origin[3];    /* from the recentred origin, game units */
	float head_angles[3];    /* pitch, yaw, roll, degrees, Quake's senses */

	int hand_valid[2];       /* a hand is only valid while its controller is tracked */
	float hand_origin[2][3]; /* where the controller points from ("aim") */
	float hand_angles[2][3]; /* and the way it points */

	/* Version 2. The view weapon as the renderer draws it in the player's hand:
	   the model's own origin and angles, and the size it is drawn at, which is
	   also the size of its bones - the barrel is where the model has it. */
	int weapon_valid;
	float weapon_origin[3];
	float weapon_angles[3];
	float weapon_scale;
	int flags;               /* SINVR_CLIENT_BULLETS_OFF, _MIRRORED, _STILL */

	/* Version 3. The last punch each hand threw by swinging: counted from 1, so
	   the game can tell a new one from one it has already seen, 0 being none
	   yet. Where the hand was at the swing's fastest, in the same frame as the
	   hands, the way it was moving there, and how fast, in metres a second.
	   The renderer sees every swing; the game decides whether one is a punch. */
	unsigned punch_count[2];
	float punch_origin[2][3];
	float punch_dir[2][3];
	float punch_speed[2];
} sinvr_poses_t;

/* ref_vr.dll's export. NULL if it speaks a different version. */
typedef const sinvr_poses_t *(*sinvr_poses_fn)(int version);

/*
 * The other way: something the player should feel, sent by the game as it
 * happens through ref_vr.dll's SinVrFeedback. hand is SINVR_HAND_LEFT or
 * _RIGHT, or SINVR_HAND_WEAPON for whichever holds the weapon; strength is
 * 0-1. A shot is the weapon going off - the fists' punches are not shots - and
 * a hit is a punch meeting something, 1 for something that can be hurt. A
 * charge is the quantum destabilizer being held, strength rising to 1 over its
 * five seconds; its shot, on release, carries the power it was charged to.
 */
#define SINVR_FEEDBACK_SHOT 1
#define SINVR_FEEDBACK_HIT 2
#define SINVR_FEEDBACK_CHARGE 3 /* a weapon being charged, strength how far: sent every tenth of a second */
#define SINVR_HAND_WEAPON (-1)

typedef void (*sinvr_feedback_fn)(int kind, int hand, float strength);

#endif /* SINVR_POSES_H */
