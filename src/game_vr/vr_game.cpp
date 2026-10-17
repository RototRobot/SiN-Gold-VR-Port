/*
 * SiN Gold VR: the game module's own VR code, shared by SiN (game/src/code/game)
 * and Wages of Sin (game/src/code/game2015). Night Dive released the two as
 * separate copies of the game source, so this file is compiled into each game
 * DLL against that game's own headers (game/CMakeLists.txt), and the copies
 * themselves only call into it - see vr_game.h for what they call.
 *
 * Whole functions of ours live here: the player's aim from the weapon in the
 * hand, punches thrown by swinging a controller and how hard they land, and
 * holding the weapon's idle animation still. The few lines that have to sit
 * inside Night Dive's own functions are in the game copies, marked
 * "SiN Gold VR".
 */
#include "g_local.h"
#include "player.h"
#include "weapon.h"
#include "fists.h"
#include "game_vr/vr_link.h"
#include "game_vr/vr_game.h"

#include <math.h>

/*
 * SiN Gold VR: shots come from the weapon in the player's right hand.
 *
 * The renderer draws the view weapon in the tracked hand and says where: the
 * model's origin and angles in the frame it recentred - from the player's eyes,
 * with +x the way their body faces - and the size it draws it at. The player's
 * own yaw turns that into the world. GunPosition is the model's origin and
 * GetGunOrientation its axes, so the barrel bone Weapon::MuzzleOffset measures
 * from the model's origin lands on the barrel the player sees.
 *
 * Only the first client - the player at this machine - aims this way. With no
 * weapon in the hand, gravity turned on its side, or vr_aim off, this is the
 * ordinary SiN behaviour.
 *
 * A left-handed player holds a mirrored model, and mirrored is set for whoever
 * measures anything off it; still says the weapon's idle animation should be
 * held where it is.
 */
static cvar_t *vr_aim = NULL;
static qboolean vr_said_aiming = false;

qboolean Player::VRWeaponPose(Vector *pos, Vector *angles, float *scale, qboolean *server_effects,
                              qboolean *mirrored, qboolean *still)
{
   float    origin[3];
   float    ang[3];
   float    size;
   float    yaw;
   float    s, c;
   int      flags;

   if(!vr_aim)
   {
      vr_aim = gi.cvar("vr_aim", "1", 0);
   }

   if(!vr_aim->value || gravaxis || entnum != 1 || !VR_WeaponPose(origin, ang, &size, &flags))
   {
      return false;
   }

   if(!vr_said_aiming)
   {
      vr_said_aiming = true;
      gi.printf("SiN Gold VR: shots come from the weapon in the player's hand\n");
   }

   yaw = worldangles[YAW] * (M_PI / 180.0f);
   s = sin(yaw);
   c = cos(yaw);

   if(pos)
   {
      *pos = EyePosition() + Vector(origin[0] * c - origin[1] * s,
                                    origin[0] * s + origin[1] * c,
                                    origin[2]);
   }
   if(angles)
   {
      *angles = Vector(ang[0], ang[1] + worldangles[YAW], ang[2]);
   }
   if(scale)
   {
      *scale = size;
   }
   if(server_effects)
   {
      *server_effects = (flags & VR_CLIENT_BULLETS_OFF) != 0;
   }
   if(mirrored)
   {
      *mirrored = (flags & VR_WEAPON_MIRRORED) != 0;
   }
   if(still)
   {
      *still = (flags & VR_WEAPON_STILL) != 0;
   }

   return true;
}

/*
 * SiN Gold VR: punches thrown by swinging the controllers, while the fists are
 * out. The renderer counts every swing of each hand; a new count is a new
 * punch, landed from where the hand was, the way it was going. Only the player
 * at this machine has controllers, so the counts seen are kept here rather
 * than in the player. The first look only notes where the counts are, so
 * nothing thrown before is replayed. vr_punch 0 leaves the fists to the fire
 * button; 2 says what each punch hit.
 */
static cvar_t    *vr_punch = NULL;
static unsigned   vr_punch_seen[2];
static qboolean   vr_punch_primed[2];

void Player::VRPunches(void)
{
   unsigned    number;
   float       origin[3];
   float       dir[3];
   float       speed;
   float       yaw;
   float       s, c;
   float       head_origin[3];
   float       head_angles[3];
   int         hand;
   Vector      fist;
   Vector      way;
   Vector      look;
   trace_t     trace;
   const char  *how;

   if(!vr_punch)
   {
      vr_punch = gi.cvar("vr_punch", "1", 0);
   }

   if(entnum != 1)
   {
      return;
   }

   for(hand = 0; hand < 2; hand++)
   {
      if(!VR_Punch(hand, &number, origin, dir, &speed))
      {
         continue;
      }

      if(!vr_punch_primed[hand])
      {
         vr_punch_primed[hand] = true;
         vr_punch_seen[hand] = number;
         continue;
      }

      if(!number || (number == vr_punch_seen[hand]))
      {
         continue;
      }
      vr_punch_seen[hand] = number;

      if(!vr_punch->value || !currentWeapon || !currentWeapon->isSubclassOf<Fists>())
      {
         continue;
      }

      yaw = worldangles[YAW] * (M_PI / 180.0f);
      s = sin(yaw);
      c = cos(yaw);
      fist = EyePosition() + Vector(origin[0] * c - origin[1] * s, origin[0] * s + origin[1] * c, origin[2]);
      way = Vector(dir[0] * c - dir[1] * s, dir[0] * s + dir[1] * c, dir[2]);

      // where the head is looking, which is not where the body faces
      if(VR_HeadPose(head_origin, head_angles))
      {
         Vector(head_angles[0], head_angles[1] + worldangles[YAW], head_angles[2]).AngleVectors(&look, NULL, NULL);
      }
      else
      {
         v_angle.AngleVectors(&look, NULL, NULL);
      }

      trace = ((Fists *)currentWeapon)->VRPunch(fist, way, look, speed, &how);
      ((Fists *)currentWeapon)->VRFeelHit(trace, hand);

      if(vr_punch->value >= 2)
      {
         gi.printf("SiN Gold VR: a punch with the %s hand, %.1f m/s - %s, %s\n",
                   hand == VR_HAND_LEFT ? "left" : "right", speed,
                   (trace.fraction < 1.0f && trace.ent && trace.ent->entity) ?
                   trace.ent->entity->getClassname() : "nothing", how);
      }
   }
}

Vector Player::GunPosition(void)
{
   Vector   pos;

   if(!VRWeaponPose(&pos, NULL, NULL, NULL))
   {
      return Sentient::GunPosition();
   }

   return pos;
}

void Player::GetGunOrientation(Vector pos, Vector *forward, Vector *right, Vector *up)
{
   Vector   angles;

   if(!VRWeaponPose(NULL, &angles, NULL, NULL))
   {
      Sentient::GetGunOrientation(pos, forward, right, up);
      return;
   }

   angles.AngleVectors(forward, right, up);
}

// ---------------------------------------------------------------------------

// SiN Gold VR: a punch that lands is felt in the hand that threw it - hard on
// something that can be hurt, lighter on a wall.
void Fists::VRFeelHit(trace_t trace, int hand)
{
   if(!owner || !owner->isClient() || trace.fraction >= 1.0f || !trace.ent || !trace.ent->entity)
   {
      return;
   }

   VR_Feedback(VR_FEEDBACK_HIT, hand,
               (trace.ent->entity != world && trace.ent->entity->takedamage) ? 1.0f : 0.4f);
}

//
// SiN Gold VR: a punch thrown by swinging a controller.
//
// A real punch curves, and the way the fist is going at its fastest is often
// well up or down - in the owner's first session, half the punches went up or
// down more steeply than they went forward - so a line along it misses whoever
// was being punched. So, in turn:
//
//   - what the fist ran into: a box about the size of a fist, swept along the
//     swing from 40 cm behind the fist to 40 cm past it. Anything that can be
//     hurt takes the blow, through the point the fist met it;
//   - who the player was going for: the living creature within the fists' own
//     reach - the one the button has - that is nearest the middle of where the
//     head is looking or where the fist was headed, vr_punch_assist degrees
//     either side, with nothing solid in between;
//   - the swing itself, which lands on whatever wall it went into, or on
//     nothing. The player's real arm goes through walls the game's would not,
//     so if the eye cannot see the start of the swing, the blow lands where the
//     arm went in.
//
// Whichever it is, the blow itself is Strike's, as the button's is.
//
#define VR_PUNCH_BEHIND 16.0f
#define VR_PUNCH_AHEAD  16.0f
#define VR_PUNCH_FIST   6.0f

static cvar_t *vr_punch_assist = NULL;
static cvar_t *vr_punch_full = NULL;

// SiN Gold VR: how hard the blow Strike is landing is, against the button's:
// a swung punch lands harder the faster the fist was going.
float vr_strike_scale = 1.0f;

static Vector vr_middle(Entity *ent)
{
   return (ent->absmin + ent->absmax) * 0.5f;
}

// The end of a blow from the eye through point that lands on target: the line
// from the eye, through point and a little past, must meet the target first.
static qboolean vr_line_to(Entity *owner, Entity *target, Vector eye, Vector point, Vector *end)
{
   Vector   way;
   Vector   e;
   trace_t  trace;

   way = point - eye;
   if(way.length() < 1.0f)
   {
      return false;
   }
   way.normalize();
   e = point + way * 16;
   trace = G_FullTrace(eye, vec_zero, vec_zero, e, 64, owner, MASK_PROJECTILE, "Fists::VRPunch");
   if((trace.fraction < 1.0f) && trace.ent && (trace.ent->entity == target))
   {
      *end = e;
      return true;
   }

   return false;
}

// How near a ray from the eye passes to an entity's box, as the cosine of the
// angle to the box's nearest point at the box's own distance: 1 when the ray
// goes through it anywhere, head to foot. Measured to the middle instead, a
// creature close enough to punch is well below the eye, and only a player
// staring at its belt would be looking at it.
static float vr_cos_to_box(Vector eye, Vector ray, Entity *ent)
{
   Vector   p;
   Vector   q;
   Vector   to;
   int      i;

   p = eye + ray * (vr_middle(ent) - eye).length();
   for(i = 0; i < 3; i++)
   {
      q[i] = p[i] < ent->absmin[i] ? ent->absmin[i] : (p[i] > ent->absmax[i] ? ent->absmax[i] : p[i]);
   }
   to = q - eye;
   if(to.length() < 1.0f)
   {
      return 1.0f;
   }
   to.normalize();
   return to * ray;
}

// The living creature the player was most likely punching, or NULL.
static Entity *vr_punch_target(Entity *owner, Vector eye, Vector look, Vector toward, float reach)
{
   Entity   *ent;
   Entity   *best;
   float    cone;
   float    bestcos;
   float    c;
   Vector   middle;
   Vector   nearest;
   trace_t  trace;
   int      i;

   if(!vr_punch_assist)
   {
      vr_punch_assist = gi.cvar("vr_punch_assist", "35", CVAR_ARCHIVE);
   }
   if(vr_punch_assist->value <= 0)
   {
      return NULL;
   }

   cone = cos(vr_punch_assist->value * M_PI / 180.0f);
   best = NULL;
   bestcos = cone;

   ent = NULL;
   while((ent = findradius(ent, eye, reach + 64)) != NULL)
   {
      if((ent == owner) || !ent->takedamage || (ent->health <= 0) || !ent->isSubclassOf<Sentient>())
      {
         continue;
      }

      // near enough: the closest point of its box, within the fists' reach
      for(i = 0; i < 3; i++)
      {
         nearest[i] = eye[i] < ent->absmin[i] ? ent->absmin[i] : (eye[i] > ent->absmax[i] ? ent->absmax[i] : eye[i]);
      }
      if((nearest - eye).length() > reach)
      {
         continue;
      }

      // in front: where the head looks, or where the fist was going
      middle = vr_middle(ent);
      c = vr_cos_to_box(eye, look, ent);
      if(vr_cos_to_box(eye, toward, ent) > c)
      {
         c = vr_cos_to_box(eye, toward, ent);
      }
      if(c <= bestcos)
      {
         continue;
      }

      // and in sight
      trace = G_Trace(eye, vec_zero, vec_zero, middle, owner, MASK_SOLID, "Fists::VRPunch");
      if(trace.fraction < 1.0f)
      {
         continue;
      }

      best = ent;
      bestcos = c;
   }

   return best;
}

trace_t Fists::VRPunch(Vector fist, Vector dir, Vector look, float speed, const char **how)
{
   trace_t  blow;

   // as hard as the button's at vr_punch_full metres a second, half that at
   // half the speed and twice at twice, and no more or less
   if(!vr_punch_full)
   {
      vr_punch_full = gi.cvar("vr_punch_full", "4", CVAR_ARCHIVE);
   }
   vr_strike_scale = vr_punch_full->value > 0 ? speed / vr_punch_full->value : 1.0f;
   vr_strike_scale = vr_strike_scale < 0.5f ? 0.5f : (vr_strike_scale > 2.0f ? 2.0f : vr_strike_scale);

   blow = VRSwing(fist, dir, look, how);
   vr_strike_scale = 1.0f;
   return blow;
}

trace_t Fists::VRSwing(Vector fist, Vector dir, Vector look, const char **how)
{
   trace_t  reach;
   trace_t  probe;
   Vector   eye;
   Vector   start;
   Vector   end;
   Vector   in;
   Vector   toward;
   Vector   aim;
   Vector   fistmins(-VR_PUNCH_FIST, -VR_PUNCH_FIST, -VR_PUNCH_FIST);
   Vector   fistmaxs(VR_PUNCH_FIST, VR_PUNCH_FIST, VR_PUNCH_FIST);
   Entity   *target;

   memset(&reach, 0, sizeof(reach));
   reach.fraction = 1.0f;
   *how = "not ready";
   if(!owner || ((weaponstate != WEAPON_READY) && (weaponstate != WEAPON_FIRING)))
   {
      return reach;
   }

   eye = owner->EyePosition();
   start = fist - dir * VR_PUNCH_BEHIND;
   end = fist + dir * VR_PUNCH_AHEAD;
   toward = end - eye;
   toward.normalize();

   // the swing's own whoosh: the fire animation plays it for the button
   owner->sound(G_Random(1) < 0.5f ? "weapons/punch/whsh1.wav" : "weapons/punch/whsh2.wav", 1.0f, CHAN_WEAPON);

   reach = G_Trace(eye, vec_zero, vec_zero, start, owner, MASK_SOLID, "Fists::VRPunch");
   if(reach.fraction == 1.0f)
   {
      // what the fist ran into
      probe = G_Trace(start, fistmins, fistmaxs, end, owner, MASK_PROJECTILE, "Fists::VRPunch");
      if((probe.fraction < 1.0f) && probe.ent && probe.ent->entity && (probe.ent->entity != world) &&
         probe.ent->entity->takedamage)
      {
         target = probe.ent->entity;
         if(vr_line_to(owner, target, eye, Vector(probe.endpos), &aim) ||
            vr_line_to(owner, target, eye, vr_middle(target), &aim))
         {
            *how = "by the fist";
            return Strike(eye, aim);
         }
      }
   }

   // who the player was going for
   target = vr_punch_target(owner, eye, look, toward, strike_reach);
   if(target && vr_line_to(owner, target, eye, vr_middle(target), &aim))
   {
      *how = "by aim assist";
      return Strike(eye, aim);
   }

   // the swing itself
   *how = "along the swing";
   if(reach.fraction < 1.0f)
   {
      in = Vector(reach.endpos) - eye;
      in.normalize();
      start = eye;
      end = Vector(reach.endpos) + in * 4;
   }

   return Strike(start, end);
}

// ---------------------------------------------------------------------------

//
// SiN Gold VR: the view weapon is held still - it is being placed in the
// player's hand, or vr_freeze_idle asks for it in play as well. The weapon is
// on a controller there, which gives it all the movement it needs.
//
static qboolean vr_said_still = false;

qboolean VR_HoldWeaponStill(Sentient *owner)
{
   qboolean still = false;

   if(owner && owner->isClient())
   {
      ((Player *)(Entity *)owner)->VRWeaponPose(NULL, NULL, NULL, NULL, NULL, &still);
   }

   // said either way, once each time it changes: it is a question a player
   // asks of the picture, and the log answers it
   if(still != vr_said_still)
   {
      vr_said_still = still;
      gi.printf(still ? "SiN Gold VR: the weapon's idle animation is held still\n" :
                "SiN Gold VR: the weapon's idle animation is running again\n");
   }

   return still;
}

// ---------------------------------------------------------------------------

//
// SiN Gold VR: steering with the weapon hand. A guided missile turns the way
// the player's angles say, and in VR those are the body's - turned by the
// stick, never tipped up or down - while the head turns the view on top of
// them, so steering by the head would turn the picture twice over. The hand
// that holds the weapon points the missile instead, as a remote would.
//
static cvar_t *vr_left_handed_game = NULL;

Vector VR_HandSteering(Player *player, Vector fallback)
{
   float origin[3];
   float angles[3];
   int   hand;

   if(!vr_left_handed_game)
   {
      vr_left_handed_game = gi.cvar("vr_left_handed", "0", 0);
   }
   hand = vr_left_handed_game->value ? VR_HAND_LEFT : VR_HAND_RIGHT;

   if(!player || player->entnum != 1 || player->gravaxis || !VR_HandPose(hand, origin, angles))
   {
      return fallback;
   }

   return Vector(angles[0], angles[1] + player->worldangles[YAW], angles[2]);
}
