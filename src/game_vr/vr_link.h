/*
 * The game module's side of the link to the VR renderer.
 *
 * ref_vr.dll is in the same process and exports the player's head, hand and
 * weapon poses (src/shared/sinvr_poses.h). This asks for them, and answers in
 * plain floats so that nothing of the game's own headers is needed here - the
 * game code turns them into its own Vectors.
 *
 * Everything reports "not tracked" when the game runs without the VR renderer,
 * when the headset is idle, or when a controller is not being tracked, so the
 * game falls back to what it has always done.
 */
#ifndef VR_LINK_H
#define VR_LINK_H

#define VR_HAND_LEFT 0
#define VR_HAND_RIGHT 1

/* The client has left its own bullet impacts to the server. */
#define VR_CLIENT_BULLETS_OFF 0x1

/* The weapon is drawn mirrored, in a left-handed player's left hand: a bone's
   distance to the model's left changes sign with it. */
#define VR_WEAPON_MIRRORED 0x2

/* The view weapon's idle animation should be held still. */
#define VR_WEAPON_STILL 0x4

/*
 * A hand, in the frame the renderer recentred: origin in game units from the
 * player's eyes, +x the way the player's body faces, +y to their left, +z up,
 * and angles in the game's own senses. Returns 0 if that hand is not tracked,
 * leaving origin and angles untouched.
 */
int VR_HandPose(int hand, float origin[3], float angles[3]);

/* The head, in the same frame. The renderer draws the view from it already;
   this is for the game to know where the player is really looking. */
int VR_HeadPose(float origin[3], float angles[3]);

/*
 * The view weapon as the renderer draws it in the player's hand, in the same
 * frame: the model's origin and angles, and the size it is drawn at - which is
 * also the size of its bones, so the barrel bone scaled by it is the barrel
 * the player sees. flags holds VR_CLIENT_BULLETS_OFF, VR_WEAPON_MIRRORED and
 * VR_WEAPON_STILL. Returns 0 when the weapon is not in the hand.
 */
int VR_WeaponPose(float origin[3], float angles[3], float *scale, int *flags);

/*
 * The last punch a hand threw by swinging: its number, which grows by one with
 * each and is 0 before the first, where the hand was at the swing's fastest,
 * in the same frame as VR_HandPose, the way it was going (a unit vector) and
 * how fast, in metres a second. Returns 0 while the renderer is not tracking.
 */
int VR_Punch(int hand, unsigned *number, float origin[3], float dir[3], float *speed);

/*
 * 1 the first time the renderer's poses arrive, -1 the first time they stop,
 * 0 whenever nothing has changed. For telling the player the link is up.
 */
int VR_TrackingChanged(void);

/*
 * Something the player should feel: a shot (VR_FEEDBACK_SHOT) or a punch
 * landing (VR_FEEDBACK_HIT), in a hand - VR_HAND_LEFT, VR_HAND_RIGHT or
 * VR_HAND_WEAPON for the one holding the weapon - at a strength from 0 to 1.
 * Does nothing without the VR renderer.
 */
#define VR_FEEDBACK_SHOT 1
#define VR_FEEDBACK_HIT 2
#define VR_FEEDBACK_CHARGE 3
#define VR_FEEDBACK_HEAD_STEERS 4
#define VR_HAND_WEAPON (-1)
void VR_Feedback(int kind, int hand, float strength);

/*
 * Roomscale. The client marks each movement command with the step the player
 * took in their room, in the command's roll angle. VR_TakeMoveKey takes that
 * mark out of the command's angles, before anything reads them, and returns it
 * - 0 for none, and always 0 without the VR renderer. VR_Pmove then moves the
 * player with pmove (gi.Pmove) and the step after it, exactly as the client
 * predicted; without the VR renderer it is pmove alone.
 */
typedef void (*vr_pmove_fn)(void *pm);
int VR_TakeMoveKey(short angles[3]);
void VR_Pmove(void *pm, int key, vr_pmove_fn pmove);

/* The renderer is drawing the view from the headset. */
int VR_Tracking(void);

/* What the player carries, for the holster menus: a sinvr_holsters_t
   (src/shared/sinvr_poses.h), filled in by the game code. Does nothing without
   the VR renderer. */
void VR_SendHolsters(const void *holsters);

#endif /* VR_LINK_H */
