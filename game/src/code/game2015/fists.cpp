//-----------------------------------------------------------------------------
//
//  $Logfile:: /Quake 2 Engine/Sin/code/game/fists.cpp                           $
// $Revision:: 34                                                             $
//   $Author:: Markd                                                          $
//     $Date:: 11/17/98 1:31a                                                 $
//
// Copyright (C) 2020 by Night Dive Studios, Inc.
// All rights reserved.
//
// See the license.txt file for conditions and terms of use for this code.
//
// DESCRIPTION:
// Normal Hands
// 

#include "g_local.h"
#include "fists.h"
#include "misc.h"
#include "specialfx.h"
#include "surface.h"
#include "game_vr/vr_link.h"

CLASS_DECLARATION(Weapon, Fists, nullptr);

ResponseDef Fists::Responses[] =
{
   { &EV_Weapon_Shoot, (Response)&Fists::Shoot },
   { nullptr, nullptr }
};

Fists::Fists() : Weapon()
{
   SetModels(nullptr, "view_punch.def");
   SetAmmo(nullptr, 0, 0);
   SetRank(10, 10);
   strike_reach  = 64;
   strike_damage = 20;
   SetMaxRange(strike_reach);
   SetType(WEAPON_MELEE);
   kick = 40;
   meansofdeath = MOD_FISTS;
}

void Fists::Shoot(Event * ev)
{
   Vector	start;
   Vector	dir;

   assert(owner);
   if(!owner)
   {
      return;
   }

   NextAttack(1);

   GetMuzzlePosition(&start, &dir);
   VRFeelHit(Strike(start, start + dir * strike_reach), VR_HAND_WEAPON);
}

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
static float vr_strike_scale = 1.0f;

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

// A blow from start to end: whatever is first in the way takes it. This is
// what Shoot always did once it knew where from; a swung fist uses it too.
trace_t Fists::Strike(Vector start, Vector end)
{
   trace_t	trace;
   float    damage;
   Vector   org;
   Vector   dir;
   int      surfflags;
   int      surftype;

   damage = G_Random(strike_damage)+strike_damage;
   damage *= vr_strike_scale;   // SiN Gold VR: a swung punch, by its speed

   trace = G_FullTrace(start, vec_zero, vec_zero, end, 64, owner, MASK_PROJECTILE, "Fists::Shoot");

   if(!trace.surface)
   {
      surfflags = 0;
      surftype = 0;
   }
   else
   {
      surfflags = trace.surface->flags;
      surftype = SURFACETYPE_FROM_FLAGS(surfflags);
      surfaceManager.DamageSurface(&trace, damage, owner);
   }
   dir = Vector(trace.endpos) - start;
   dir.normalize();

   org = Vector(trace.endpos) - dir;

   if((trace.fraction < 1.0f))
   {
      if(trace.ent->entity && trace.ent->entity->takedamage)
      {
         if(trace.ent->entity->flags & FL_BLOOD)
         {
            if((meansofdeath == MOD_MUTANTHANDS) || (trace.ent->entity->health < -500))
            {
               owner->RandomGlobalSound("impact_goryimpact");
            }
            else
            {
               owner->RandomGlobalSound("impact_bodyimpact");
            }
            SpawnBlood(org, trace.plane.normal, damage);
         }
         else
         {
            gi.WriteByte(svc_temp_entity);
            gi.WriteByte(TE_STRIKE);
            gi.WritePosition(org.vec3());
            gi.WriteDir(trace.plane.normal);
            gi.WriteByte(120);
            gi.WriteByte(surftype);
            gi.multicast(org.vec3(), MULTICAST_PVS);
         }
         if(trace.intersect.valid)
         {
            // take the ground out so that the kick works
            trace.ent->entity->groundentity = nullptr;

            // We hit a valid group so send in location based damage
            trace.ent->entity->Damage(
               this,
               owner,
               damage,
               trace.endpos,
               dir,
               trace.plane.normal,
               kick,
               0,
               meansofdeath,
               trace.intersect.parentgroup,
               -1,
               1);
            //trace.intersect.damage_multiplier );
         }
         else
         {
            // We didn't hit any groups, so send in generic damage
            trace.ent->entity->Damage(
               this,
               owner,
               damage,
               trace.endpos,
               dir,
               trace.plane.normal,
               kick,
               0,
               meansofdeath,
               -1,
               -1,
               1);
         }
      }
      else
      {
         gi.WriteByte(svc_temp_entity);
         gi.WriteByte(TE_STRIKE);
         gi.WritePosition(org.vec3());
         gi.WriteDir(trace.plane.normal);
         gi.WriteByte(120);
         gi.WriteByte(surftype);
         gi.multicast(org.vec3(), MULTICAST_PVS);
      }
   }

   return trace;
}

//###
void Fists::SecondaryUse(Event *ev)
{
   // switch to the dual magnums
   owner->useWeapon("ConcussionGun");
}
//###

// EOF

