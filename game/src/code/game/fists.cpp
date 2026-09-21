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

CLASS_DECLARATION(Weapon, Fists, NULL);

ResponseDef Fists::Responses[] =
{
   { &EV_Weapon_Shoot, (Response)&Fists::Shoot },
   { NULL, NULL }
};

Fists::Fists() : Weapon()
{
   SetModels(NULL, "view_punch.def");
   SetAmmo(NULL, 0, 0);
   SetRank(10, 10);
   strike_reach = 64;
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
   Strike(start, start + dir * strike_reach);
}

//
// SiN Gold VR: a punch thrown by swinging a controller. The fist is where the
// player's hand is, not a reach away from the eye, so the blow runs along the
// swing, from a little behind the fist to a little past it - about 40 cm and
// 20 cm. The player's real arm goes through walls the game's would not, so if
// the way from the eye to the start of that is blocked, the blow lands where
// the arm went into the wall.
//
#define VR_PUNCH_BEHIND 16.0f
#define VR_PUNCH_AHEAD  8.0f

trace_t Fists::VRPunch(Vector fist, Vector dir)
{
   trace_t  reach;
   Vector   eye;
   Vector   start;
   Vector   end;
   Vector   in;

   memset(&reach, 0, sizeof(reach));
   reach.fraction = 1.0f;
   if(!owner || ((weaponstate != WEAPON_READY) && (weaponstate != WEAPON_FIRING)))
   {
      return reach;
   }

   eye = owner->EyePosition();
   start = fist - dir * VR_PUNCH_BEHIND;
   end = fist + dir * VR_PUNCH_AHEAD;

   reach = G_Trace(eye, vec_zero, vec_zero, start, owner, MASK_SOLID, "Fists::VRPunch");
   if(reach.fraction < 1.0f)
   {
      in = Vector(reach.endpos) - eye;
      in.normalize();
      start = eye;
      end = Vector(reach.endpos) + in * 4;
   }

   // the swing's own whoosh: the fire animation plays it for the button
   owner->sound(G_Random(1) < 0.5f ? "weapons/punch/whsh1.wav" : "weapons/punch/whsh2.wav", 1.0f, CHAN_WEAPON);

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
            trace.ent->entity->groundentity = NULL;

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

// EOF

