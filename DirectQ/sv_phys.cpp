/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
// sv_phys.c

#include "quakedef.h"
#include "pr_class.h"

/*

pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement and push 
normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

cvar_t	sv_friction ("sv_friction", "4", CVAR_SERVER);
cvar_t	sv_stopspeed ("sv_stopspeed", "100", CVAR_SERVER);
cvar_t	sv_gravity ("sv_gravity", "800", CVAR_SERVER);
cvar_t	sv_maxvelocity ("sv_maxvelocity", "2000", CVAR_SERVER);
cvar_t	sv_oldvelocity ("sv_oldvelocity", "1", CVAR_SERVER);
cvar_t	sv_nostep ("sv_nostep", "0", CVAR_SERVER);

static	vec3_t	vec_origin = {0.0, 0.0, 0.0};

#define	MOVE_EPSILON		0.01
#define MIN_STEP_NORMAL		0.7
#define	STEPSIZE			18

void SV_Physics_Toss (edict_t *ent, double frametime);

/*
================
SV_CheckAllEnts
================
*/
void SV_CheckAllEnts (void)
{
	int			e;
	edict_t		*check;

	// see if any solid entities are inside the final position
	check = NextEdict (SVProgs->Edicts);

	for (e = 1; e < SVProgs->NumEdicts; e++, check = NextEdict (check))
	{
		if (check->free) continue;

		if (check->v.movetype == MOVETYPE_PUSH ||
			check->v.movetype == MOVETYPE_NONE ||
			check->v.movetype == MOVETYPE_FOLLOW || // Nehahra
			check->v.movetype == MOVETYPE_NOCLIP)
			continue;

		if (SV_TestEntityPosition (check))
			Con_Printf ("entity in invalid position\n");
	}
}


/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity (edict_t *ent)
{
	int		i;

	// bound velocity
	for (i = 0; i < 3; i++)
	{
		if (IS_NAN (ent->v.velocity[i]))
		{
			Con_DPrintf ("Got a NaN velocity on %s\n", SVProgs->GetString (ent->v.classname));
			ent->v.velocity[i] = 0;
		}

		if (IS_NAN (ent->v.origin[i]))
		{
			Con_DPrintf ("Got a NaN origin on %s\n", SVProgs->GetString (ent->v.classname));
			ent->v.origin[i] = 0;
		}

		if (sv_oldvelocity.value)
		{
			// original velocity bounding (retains original gameplay)
			if (ent->v.velocity[i] > sv_maxvelocity.value) ent->v.velocity[i] = sv_maxvelocity.value;
			if (ent->v.velocity[i] < -sv_maxvelocity.value) ent->v.velocity[i] = -sv_maxvelocity.value;
		}
	}

	// already done
	if (sv_oldvelocity.value) return;

	// correct velocity bounding
	// note - this may be "correct" but it's a gameplay change!!!
	float vel = Vector3Length (ent->v.velocity);

	if (vel > sv_maxvelocity.value)
		Vector3Scale (ent->v.velocity, ent->v.velocity, sv_maxvelocity.value / vel);
}


/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
bool SV_RunThink (edict_t *ent, double frametime)
{
	float thinktime = ent->v.nextthink;

	if (thinktime <= 0 || thinktime > (sv.time + frametime)) return true;

	// don't let things stay in the past.  it is possible to start that way by a trigger with a local time.
	if (thinktime < sv.time) thinktime = sv.time;

	// cache the frame from before the think function was run
	float oldframe = ent->v.frame;

	ent->v.nextthink = 0;
	SVProgs->GlobalStruct->time = thinktime;

	// now run the think function
	SVProgs->RunInteraction (ent, SVProgs->Edicts, ent->v.think);

	// default is not to do it
	ent->sendinterval = false;

	// check if we need to send the interval
	if (!ent->free && ent->v.nextthink && (ent->v.movetype == MOVETYPE_STEP || ent->v.frame != oldframe))
	{
		int i = Q_rint ((ent->v.nextthink - thinktime) * 255);

		if (i >= 0 && i < 256 && i != 25 && i != 26) ent->sendinterval = true;
	}

	return !ent->free;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
void SV_Impact (edict_t *e1, edict_t *e2)
{
	int		old_self, old_other;

	old_self = SVProgs->GlobalStruct->self;
	old_other = SVProgs->GlobalStruct->other;

	SVProgs->GlobalStruct->time = sv.time;

	if (e1->v.touch && e1->v.solid != SOLID_NOT) SVProgs->RunInteraction (e1, e2, e1->v.touch);
	if (e2->v.touch && e2->v.solid != SOLID_NOT) SVProgs->RunInteraction (e2, e1, e2->v.touch);

	SVProgs->GlobalStruct->self = old_self;
	SVProgs->GlobalStruct->other = old_other;
}


/*
==================
ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
#define	STOP_EPSILON	0.1

void ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float backoff = Vector3Dot (in, normal) * overbounce;

	for (int i = 0; i < 3; i++)
	{
		float change = normal[i] * backoff;

		out[i] = in[i] - change;

		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}
}


/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
============
*/
#define	MAX_CLIP_PLANES	5

int SV_FlyMove (edict_t *ent, double time, trace_t *steptrace)
{
	int			bumpcount, numbumps;
	vec3_t		dir;
	float		d;
	int			numplanes;
	vec3_t		planes[MAX_CLIP_PLANES];
	vec3_t		primal_velocity, original_velocity, new_velocity;
	int			i, j;
	trace_t		trace;
	vec3_t		end;
	double		time_left;
	int			blocked;

	numbumps = 4;

	blocked = 0;
	Vector3Copy (original_velocity, ent->v.velocity);
	Vector3Copy (primal_velocity, ent->v.velocity);
	numplanes = 0;

	time_left = time;

	for (bumpcount = 0; bumpcount < numbumps; bumpcount++)
	{
		if (!ent->v.velocity[0] && !ent->v.velocity[1] && !ent->v.velocity[2]) break;

		Vector3Mad (end, ent->v.velocity, time_left, ent->v.origin);
		trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);

		if (trace.allsolid)
		{
			// entity is trapped in another solid
			Vector3Copy (ent->v.velocity, vec3_origin);
			return 3;
		}

		if (trace.fraction > 0)
		{
			// actually covered some distance
			Vector3Copy (ent->v.origin, trace.endpos);
			Vector3Copy (original_velocity, ent->v.velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			break;		// moved the entire distance

		if (!trace.ent)
		{
			Sys_Error ("SV_FlyMove: !trace.ent");
			return 0;
		}

		if (trace.plane.normal[2] > MIN_STEP_NORMAL)
		{
			blocked |= 1;		// floor

			if (trace.ent->v.solid == SOLID_BSP)
			{
				ent->v.flags =	(int) ent->v.flags | FL_ONGROUND;
				ent->v.groundentity = EdictToProg (trace.ent);
			}
		}

		if (!trace.plane.normal[2])
		{
			blocked |= 2;		// step

			if (steptrace)
				*steptrace = trace;	// save for player extrafriction
		}

		// run the impact function
		SV_Impact (ent, trace.ent);

		if (ent->free)
			break;		// removed by the impact function

		time_left -= time_left * trace.fraction;

		// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{
			// this shouldn't really happen
			Vector3Copy (ent->v.velocity, vec3_origin);
			return 3;
		}

		Vector3Copy (planes[numplanes], trace.plane.normal);
		numplanes++;

		// modify original_velocity so it parallels all of the clip planes
		for (i = 0; i < numplanes; i++)
		{
			// overbounce 1.01 here (from q2) is key to resolving the clipping bug (but breaks on RMQ ladders)
			ClipVelocity (original_velocity, planes[i], new_velocity, 1);

			for (j = 0; j < numplanes; j++)
			{
				if (j != i)
				{
					if (Vector3Dot (new_velocity, planes[j]) < 0)
						break;	// not ok
				}
			}

			if (j == numplanes)
				break;
		}

		if (i != numplanes)
		{
			// go along this plane
			Vector3Copy (ent->v.velocity, new_velocity);
		}
		else
		{
			// go along the crease
			if (numplanes != 2)
			{
				Vector3Copy (ent->v.velocity, vec3_origin);
				return 7;
			}

			Vector3Cross (dir, planes[0], planes[1]);
			d = Vector3Dot (dir, ent->v.velocity);
			Vector3Scale (ent->v.velocity, dir, d);
		}

		// if original velocity is against the original velocity, stop dead
		// to avoid tiny occilations in sloping corners
		if (Vector3Dot (ent->v.velocity, primal_velocity) <= 0)
		{
			Vector3Copy (ent->v.velocity, vec3_origin);
			return blocked;
		}
	}

	return blocked;
}


/*
============
SV_AddGravity

============
*/
void SV_AddGravity (edict_t *ent, double frametime)
{
	float	ent_gravity;

	eval_t	*val = GETEDICTFIELDVALUEFAST (ent, ed_gravity);

	if (val && val->_float)
		ent_gravity = val->_float;
	else ent_gravity = 1.0;

	ent->v.velocity[2] -= (ent_gravity * sv_gravity.value * frametime);
}


/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
trace_t SV_PushEntity (edict_t *ent, vec3_t push)
{
	trace_t	trace;
	vec3_t	end;

	Vector3Add (end, ent->v.origin, push);

	if (ent->v.movetype == MOVETYPE_FLYMISSILE)
		trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent);
	else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
	{
		// only clip against bmodels
		trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent);
	}
	else trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);

	Vector3Copy (ent->v.origin, trace.endpos);
	SV_LinkEdict (ent, true);

	if (trace.ent)
		SV_Impact (ent, trace.ent);

	return trace;
}


edict_t **moved_edict = NULL;
vec3_t *moved_from = NULL;


/*
============
SV_PushMove

============
*/
void SV_PushMove (edict_t *pusher, float movetime)
{
	int			i, e;
	edict_t		*check, *block;
	vec3_t		mins, maxs, move;
	vec3_t		entorig, pushorig;
	int			num_moved;

	/*
	veeeerrryyy iffy.  do we *really* want to to run for *all* SOLID_NOT entities???
	if (pusher->v.solid == SOLID_NOT)
	{
		if (pusher->v.avelocity[0] || pusher->v.avelocity[1] || pusher->v.avelocity[2])
		{
			for (i = 0; i < 3; i++)
			{
				pusher->v.angles[0] = pusher->v.angles[0] + movetime * pusher->v.avelocity[0];
				pusher->v.angles[1] = pusher->v.angles[1] + movetime * pusher->v.avelocity[1];
				pusher->v.angles[2] = pusher->v.angles[2] + movetime * pusher->v.avelocity[2];
			}
		}

		for (i = 0; i < 3; i++)
		{
			move[i] = pusher->v.velocity[i] * movetime;
			mins[i] = pusher->v.absmin[i] + move[i];
			maxs[i] = pusher->v.absmax[i] + move[i];
		}

		Vector3Add (pusher->v.origin, pusher->v.origin, move);
		pusher->v.ltime += movetime;
		SV_LinkEdict (pusher, false);
		return;
	}
	*/

	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	for (i = 0; i < 3; i++)
	{
		move[i] = pusher->v.velocity[i] * movetime;
		mins[i] = pusher->v.absmin[i] + move[i];
		maxs[i] = pusher->v.absmax[i] + move[i];
	}

	Vector3Copy (pushorig, pusher->v.origin);

	// move the pusher to it's final position
	Vector3Add (pusher->v.origin, pusher->v.origin, move);
	pusher->v.ltime += movetime;
	SV_LinkEdict (pusher, false);

	// see if any solid entities are inside the final position
	num_moved = 0;
	check = NextEdict (SVProgs->Edicts);

	for (e = 1; e < SVProgs->NumEdicts; e++, check = NextEdict (check))
	{
		if (check->free) continue;

		if (check->v.movetype == MOVETYPE_PUSH ||
			check->v.movetype == MOVETYPE_NONE ||
			check->v.movetype == MOVETYPE_FOLLOW || // Nehahra
			check->v.movetype == MOVETYPE_NOCLIP)
			continue;

		// if the entity is standing on the pusher, it will definately be moved
		if (!(((int) check->v.flags & FL_ONGROUND) && ProgToEdict (check->v.groundentity) == pusher))
		{
			if (check->v.absmin[0] >= maxs[0] || check->v.absmin[1] >= maxs[1] || check->v.absmin[2] >= maxs[2] ||
				check->v.absmax[0] <= mins[0] || check->v.absmax[1] <= mins[1] || check->v.absmax[2] <= mins[2])
				continue;

			// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check)) continue;
		}

		// remove the onground flag for non-players
		if (check->v.movetype != MOVETYPE_WALK)
			check->v.flags = (int) check->v.flags & ~FL_ONGROUND;

		Vector3Copy (entorig, check->v.origin);
		Vector3Copy (moved_from[num_moved], check->v.origin);
		moved_edict[num_moved++] = check;

		// only check for types that can block
		if (pusher->v.solid == SOLID_BSP || pusher->v.solid == SOLID_BBOX || pusher->v.solid == SOLID_SLIDEBOX)
		{
			// store out the previous solid value because we're going to change it next
			float oldpushervsolid = pusher->v.solid;

			// try moving the contacted entity
			pusher->v.solid = SOLID_NOT;

			SV_PushEntity (check, move);

			// restore from the stored solid
			pusher->v.solid = oldpushervsolid;

			// if it is still inside the pusher, block
			block = SV_TestEntityPosition (check);
		}
		else
		{
			// not blocked
			block = NULL;
		}

		if (block)
		{
			// fail the move
			if (check->v.mins[0] == check->v.maxs[0]) continue;

			if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
			{
				// corpse
				check->v.mins[0] = check->v.mins[1] = 0;
				Vector3Copy (check->v.maxs, check->v.mins);
				continue;
			}

			Vector3Copy (check->v.origin, entorig);
			SV_LinkEdict (check, true);

			Vector3Copy (pusher->v.origin, pushorig);
			SV_LinkEdict (pusher, false);
			pusher->v.ltime -= movetime;

			// if the pusher has a "blocked" function, call it
			// otherwise, just stay in place until the obstacle is gone
			if (pusher->v.blocked) SVProgs->RunInteraction (pusher, check, pusher->v.blocked);

			// move back any entities we already moved
			for (i = 0; i < num_moved; i++)
			{
				Vector3Copy (moved_edict[i]->v.origin, moved_from[i]);
				SV_LinkEdict (moved_edict[i], false);
			}

			return;
		}
	}
}


/*
============
SV_PushRotate

============
*/
void SV_PushRotate (edict_t *pusher, float movetime)
{
	int			i, e;
	edict_t		*check, *block;
	vec3_t		move, a, amove;
	vec3_t		entorig, pushorig;
	int			num_moved;
	vec3_t		org, org2;
	QMATRIX		av;

	if (!pusher->v.avelocity[0] && !pusher->v.avelocity[1] && !pusher->v.avelocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	for (i = 0; i < 3; i++)
		amove[i] = pusher->v.avelocity[i] * movetime;

	Vector3Subtract (a, vec3_origin, amove);
	av.AngleVectors (a);

	Vector3Copy (pushorig, pusher->v.angles);

	// move the pusher to it's final position
	Vector3Add (pusher->v.angles, pusher->v.angles, amove);
	pusher->v.ltime += movetime;
	SV_LinkEdict (pusher, false);

	// see if any solid entities are inside the final position
	num_moved = 0;
	check = NextEdict (SVProgs->Edicts);

	for (e = 1; e < SVProgs->NumEdicts; e++, check = NextEdict (check))
	{
		if (check->free)
			continue;

		if (check->v.movetype == MOVETYPE_PUSH ||
			check->v.movetype == MOVETYPE_NONE ||
			check->v.movetype == MOVETYPE_FOLLOW ||
			check->v.movetype == MOVETYPE_NOCLIP)
			continue;

		// if the entity is standing on the pusher, it will definately be moved
		if (!(((int) check->v.flags & FL_ONGROUND) && ProgToEdict (check->v.groundentity) == pusher))
		{
			if (check->v.absmin[0] >= pusher->v.absmax[0] ||
				check->v.absmin[1] >= pusher->v.absmax[1] ||
				check->v.absmin[2] >= pusher->v.absmax[2] ||
				check->v.absmax[0] <= pusher->v.absmin[0] ||
				check->v.absmax[1] <= pusher->v.absmin[1] ||
				check->v.absmax[2] <= pusher->v.absmin[2])
				continue;

			// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check))
				continue;
		}

		// remove the onground flag for non-players
		if (check->v.movetype != MOVETYPE_WALK)
			check->v.flags = (int) check->v.flags & ~FL_ONGROUND;

		Vector3Copy (entorig, check->v.origin);
		Vector3Copy (moved_from[num_moved], check->v.origin);
		moved_edict[num_moved++] = check;

		// calculate destination position
		Vector3Subtract (org, check->v.origin, pusher->v.origin);

		org2[0] = Vector3Dot (org, av.fw);
		org2[1] = -Vector3Dot (org, av.rt);
		org2[2] = Vector3Dot (org, av.up);

		Vector3Subtract (move, org2, org);

		// try moving the contacted entity
		float oldsolid = pusher->v.solid;

		pusher->v.solid = SOLID_NOT;
		SV_PushEntity (check, move);
		pusher->v.solid = oldsolid;

		// if it is still inside the pusher, block
		block = SV_TestEntityPosition (check);

		if (block)
		{
			// fail the move
			if (check->v.mins[0] == check->v.maxs[0])
				continue;

			if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
			{
				// corpse
				check->v.mins[0] = check->v.mins[1] = 0;
				Vector3Copy (check->v.maxs, check->v.mins);
				continue;
			}

			Vector3Copy (check->v.origin, entorig);
			SV_LinkEdict (check, true);

			Vector3Copy (pusher->v.angles, pushorig);
			SV_LinkEdict (pusher, false);
			pusher->v.ltime -= movetime;

			// if the pusher has a "blocked" function, call it
			// otherwise, just stay in place until the obstacle is gone
			if (pusher->v.blocked) SVProgs->RunInteraction (pusher, check, pusher->v.blocked);

			// move back any entities we already moved
			for (i = 0; i < num_moved; i++)
			{
				Vector3Copy (moved_edict[i]->v.origin, moved_from[i]);
				Vector3Subtract (moved_edict[i]->v.angles, moved_edict[i]->v.angles, amove);
				SV_LinkEdict (moved_edict[i], false);
			}

			return;
		}
		else Vector3Add (check->v.angles, check->v.angles, amove);
	}
}


/*
================
SV_Physics_Pusher

================
*/
void SV_Physics_Pusher (edict_t *ent, double frametime)
{
	float	thinktime;
	float	oldltime;
	float	movetime;

	oldltime = ent->v.ltime;
	thinktime = ent->v.nextthink;

	if (thinktime < ent->v.ltime + frametime)
	{
		movetime = thinktime - ent->v.ltime;

		if (movetime < 0)
			movetime = 0;
	}
	else movetime = frametime;

	// advances ent->v.ltime if not blocked
#if 1
	// this gives bad results with the end.bsp spiked ball
	if (movetime)
	{
		// hmmm - what solid does the end spiky ball use...?
		if (ent->v.solid == SOLID_BSP && (ent->v.avelocity[0] || ent->v.avelocity[1] || ent->v.avelocity[2]) && ent != SVProgs->Edicts)
			SV_PushRotate (ent, frametime);
		else SV_PushMove (ent, movetime);
	}
#else
	if (movetime) SV_PushMove (ent, movetime);
#endif

	if (thinktime > oldltime && thinktime <= ent->v.ltime)
	{
		ent->v.nextthink = 0;
		SVProgs->GlobalStruct->time = sv.time;
		SVProgs->RunInteraction (ent, SVProgs->Edicts, ent->v.think);

		if (ent->free) return;
	}
}


/*
===============================================================================

CLIENT MOVEMENT

===============================================================================
*/

/*
=============
SV_CheckStuck

This is a big hack to try and fix the rare case of getting stuck in the world
clipping hull.
=============
*/
void SV_CheckStuck (edict_t *ent)
{
	int		i, j;
	int		z;
	vec3_t	org;

	if (!SV_TestEntityPosition (ent))
	{
		Vector3Copy (ent->v.oldorigin, ent->v.origin);
		return;
	}

	Vector3Copy (org, ent->v.origin);
	Vector3Copy (ent->v.origin, ent->v.oldorigin);

	if (!SV_TestEntityPosition (ent))
	{
		Con_DPrintf ("Unstuck.\n");
		SV_LinkEdict (ent, true);
		return;
	}

	for (z = 0; z < 18; z++)
	{
		for (i = -1; i <= 1; i++)
		{
			for (j = -1; j <= 1; j++)
			{
				ent->v.origin[0] = org[0] + i;
				ent->v.origin[1] = org[1] + j;
				ent->v.origin[2] = org[2] + z;

				if (!SV_TestEntityPosition (ent))
				{
					Con_DPrintf ("Unstuck.\n");
					SV_LinkEdict (ent, true);
					return;
				}
			}
		}
	}

	Vector3Copy (ent->v.origin, org);
	Con_DPrintf ("player is stuck.\n");
}


/*
=============
SV_CheckWater
=============
*/
bool SV_CheckWater (edict_t *ent)
{
	vec3_t	point;
	int		cont;

	point[0] = ent->v.origin[0];
	point[1] = ent->v.origin[1];
	point[2] = ent->v.origin[2] + ent->v.mins[2] + 1;

	ent->v.waterlevel = 0;
	ent->v.watertype = CONTENTS_EMPTY;
	cont = SV_PointContents (point);

	if (cont <= CONTENTS_WATER)
	{
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		point[2] = ent->v.origin[2] + (ent->v.mins[2] + ent->v.maxs[2]) * 0.5;
		cont = SV_PointContents (point);

		if (cont <= CONTENTS_WATER)
		{
			ent->v.waterlevel = 2;
			point[2] = ent->v.origin[2] + ent->v.view_ofs[2];
			cont = SV_PointContents (point);

			if (cont <= CONTENTS_WATER)
				ent->v.waterlevel = 3;
		}
	}

	return ent->v.waterlevel > 1;
}

/*
============
SV_WallFriction

============
*/
void SV_WallFriction (edict_t *ent, trace_t *trace)
{
	QMATRIX		av;
	float		d, i;
	vec3_t		into, side;

	av.AngleVectors (ent->v.v_angle);
	d = Vector3Dot (trace->plane.normal, av.fw);

	d += 0.5;

	if (d >= 0)
		return;

	// cut the tangential velocity
	i = Vector3Dot (trace->plane.normal, ent->v.velocity);
	Vector3Scale (into, trace->plane.normal, i);
	Vector3Subtract (side, ent->v.velocity, into);

	ent->v.velocity[0] = side[0] * (1 + d);
	ent->v.velocity[1] = side[1] * (1 + d);
}

/*
=====================
SV_TryUnstick

Player has come to a dead stop, possibly due to the problem with limited
float precision at some angle joins in the BSP hull.

Try fixing by pushing one pixel in each direction.

This is a hack, but in the interest of good gameplay...
======================
*/
int SV_TryUnstick (edict_t *ent, double frametime, vec3_t oldvel)
{
	int		i;
	vec3_t	oldorg;
	vec3_t	dir;
	int		clip;
	trace_t	steptrace;

	Vector3Copy (oldorg, ent->v.origin);
	Vector3Copy (dir, vec3_origin);

	for (i = 0; i < 8; i++)
	{
		// try pushing a little in an axial direction
		switch (i)
		{
		case 0:	dir[0] = 2; dir[1] = 0; break;
		case 1:	dir[0] = 0; dir[1] = 2; break;
		case 2:	dir[0] = -2; dir[1] = 0; break;
		case 3:	dir[0] = 0; dir[1] = -2; break;
		case 4:	dir[0] = 2; dir[1] = 2; break;
		case 5:	dir[0] = -2; dir[1] = 2; break;
		case 6:	dir[0] = 2; dir[1] = -2; break;
		case 7:	dir[0] = -2; dir[1] = -2; break;
		}

		SV_PushEntity (ent, dir);

		// retry the original move
		ent->v.velocity[0] = oldvel[0];
		ent->v.velocity[1] = oldvel[1];
		ent->v.velocity[2] = 0;

		clip = SV_FlyMove (ent, frametime, &steptrace);

		if (fabs (oldorg[1] - ent->v.origin[1]) > 4 || fabs (oldorg[0] - ent->v.origin[0]) > 4)
		{
			//Con_DPrintf ("unstuck!\n");
			return clip;
		}

		// go back to the original pos and try again
		Vector3Copy (ent->v.origin, oldorg);
	}

	Vector3Copy (ent->v.velocity, vec3_origin);
	return 7;		// still not moving
}


/*
=====================
SV_WalkMove

Only used by players
======================
*/
void SV_WalkMove (edict_t *ent, double frametime)
{
	vec3_t		upmove, downmove;
	vec3_t		oldorg, oldvel;
	vec3_t		nosteporg, nostepvel;
	int			clip, oldclip;
	int			oldonground;
	trace_t		steptrace, uptrace, downtrace;

	// do a regular slide move unless it looks like you ran into a step
	oldonground = (int) ent->v.flags & FL_ONGROUND;
	ent->v.flags = (int) ent->v.flags & ~FL_ONGROUND;

	Vector3Copy (oldorg, ent->v.origin);
	Vector3Copy (oldvel, ent->v.velocity);

	clip = SV_FlyMove (ent, frametime, &steptrace);

	if (!(clip & 2)) return;		// move didn't block on a step
	if (!oldonground && ent->v.waterlevel == 0) return;		// don't stair up while jumping
	if (ent->v.movetype != MOVETYPE_WALK) return;		// gibbed by a trigger
	if (sv_nostep.value) return;
	if ((int) sv_player->v.flags & FL_WATERJUMP) return;

	Vector3Copy (nosteporg, ent->v.origin);
	Vector3Copy (nostepvel, ent->v.velocity);

	// try moving up and forward to go up a step
	Vector3Copy (ent->v.origin, oldorg);	// back to start pos

	Vector3Copy (upmove, vec3_origin);
	Vector3Copy (downmove, vec3_origin);

	upmove[2] = STEPSIZE;
	downmove[2] = -STEPSIZE;

	// move up
	uptrace = SV_PushEntity (ent, upmove);	// FIXME: don't link?

	// move forward
	Vector3Set (ent->v.velocity, oldvel[0], oldvel[1], 0);
	oldclip = clip;
	clip = SV_FlyMove (ent, frametime, &steptrace);

	// check for stuckness, possibly due to the limited precision of floats in the clipping hulls
	if (clip)
	{
#define STUCK_EPSILON	0.03125
		if (fabs (oldorg[1] - ent->v.origin[1]) < STUCK_EPSILON && fabs (oldorg[0] - ent->v.origin[0]) < STUCK_EPSILON)
		{
			// stepping up didn't make any progress
			clip = SV_TryUnstick (ent, frametime, oldvel);
		}
	}

	// extra friction based on view angle
	if (clip & 2) SV_WallFriction (ent, &steptrace);

	// move down
	downtrace = SV_PushEntity (ent, downmove);	// FIXME: don't link?

	if (downtrace.plane.normal[2] > MIN_STEP_NORMAL)
	{
		if (ent->v.solid == SOLID_BSP)
		{
			ent->v.flags =	(int) ent->v.flags | FL_ONGROUND;
			ent->v.groundentity = EdictToProg (downtrace.ent);
		}
	}
	else
	{
		// if the push down didn't end up on good ground, use the move without
		// the step up.  This happens near wall / slope combinations, and can
		// cause the player to hop up higher on a slope too steep to climb
		// allow the move if we originally clipped on a step but didn't clip on anything subsequent
		// this works around a bug in SV_Move where clipping on steps is occasionally misreported
		if (!(oldclip == 3 && !clip))
		{
			Vector3Copy (ent->v.origin, nosteporg);
			Vector3Copy (ent->v.velocity, nostepvel);
		}
	}
}


/*
================
SV_Physics_Client

Player character actions
================
*/
cvar_t sv_nocliptouchestriggers ("sv_nocliptouchestriggers", 0.0f, CVAR_SERVER);

void SV_Physics_Client (edict_t	*ent, int num, double frametime)
{
	if (!svs.clients[num-1].active)
		return;		// unconnected slot

	// call standard client pre-think
	SVProgs->GlobalStruct->time = sv.time;
	SVProgs->RunInteraction (ent, NULL, SVProgs->GlobalStruct->PlayerPreThink);

	// do a move
	SV_CheckVelocity (ent);

	// decide which move function to call
	switch ((int) ent->v.movetype)
	{
	case MOVETYPE_NONE:
		if (!SV_RunThink (ent, frametime))
			return;

		break;

	case MOVETYPE_WALK:
		if (SV_RunThink (ent, frametime))
		{
			if (!SV_CheckWater (ent) && !((int) ent->v.flags & FL_WATERJUMP))
				SV_AddGravity (ent, frametime);

			SV_CheckStuck (ent);
			SV_WalkMove (ent, frametime);
		}

		break;

	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
		SV_Physics_Toss (ent, frametime);
		break;

	case MOVETYPE_FLY:
		if (!SV_RunThink (ent, frametime))
			return;

		SV_FlyMove (ent, frametime, NULL);
		break;

	case MOVETYPE_NOCLIP:
		if (!SV_RunThink (ent, frametime))
			return;

		Vector3Mad (ent->v.origin, ent->v.velocity, frametime, ent->v.origin);
		break;

	default:
		Sys_Error ("SV_Physics_client: bad movetype %i", (int) ent->v.movetype);
	}

	// call standard player post-think
	if (ent->v.movetype == MOVETYPE_NOCLIP && !sv_nocliptouchestriggers.integer)
		SV_LinkEdict (ent, false);
	else SV_LinkEdict (ent, true);

	SVProgs->GlobalStruct->time = sv.time;
	SVProgs->RunInteraction (ent, NULL, SVProgs->GlobalStruct->PlayerPostThink);
}

//============================================================================

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
void SV_Physics_None (edict_t *ent, double frametime)
{
	// regular thinking
	SV_RunThink (ent, frametime);
}


// Nehahra
/*
=============
SV_Physics_Follow

Entities that are "stuck" to another entity
=============
*/
void SV_Physics_Follow (edict_t *ent, double frametime)
{
	QMATRIX av;
	vec3_t  angles, v;
	edict_t *e;

	// regular thinking
	if (!SV_RunThink (ent, frametime))
		return;

	e = ProgToEdict (ent->v.aiment);

	if (e->v.angles[0] == ent->v.punchangle[0] && e->v.angles[1] == ent->v.punchangle[1] && e->v.angles[2] == ent->v.punchangle[2])
	{
		// quick case for no rotation
		Vector3Add (ent->v.origin, e->v.origin, ent->v.view_ofs);
	}
	else
	{
		Vector3Set (angles, -ent->v.punchangle[0], ent->v.punchangle[1], ent->v.punchangle[2]);
		av.AngleVectors (angles);

		v[0] = ent->v.view_ofs[0] * av.fw[0] + ent->v.view_ofs[1] * av.rt[0] + ent->v.view_ofs[2] * av.up[0];
		v[1] = ent->v.view_ofs[0] * av.fw[1] + ent->v.view_ofs[1] * av.rt[1] + ent->v.view_ofs[2] * av.up[1];
		v[2] = ent->v.view_ofs[0] * av.fw[2] + ent->v.view_ofs[1] * av.rt[2] + ent->v.view_ofs[2] * av.up[2];

		Vector3Set (angles, -e->v.angles[0], e->v.angles[1], e->v.angles[2]);
		av.AngleVectors (angles);

		ent->v.origin[0] = Vector3Dot (v, av.fw) + e->v.origin[0];
		ent->v.origin[1] = Vector3Dot (v, av.rt) + e->v.origin[1];
		ent->v.origin[2] = Vector3Dot (v, av.up) + e->v.origin[2];
	}

	Vector3Add (ent->v.angles, e->v.angles, ent->v.v_angle);
	SV_LinkEdict (ent, true);
}


/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
void SV_Physics_Noclip (edict_t *ent, double frametime)
{
	// regular thinking
	if (!SV_RunThink (ent, frametime))
		return;

	Vector3Mad (ent->v.angles, ent->v.avelocity, frametime, ent->v.angles);
	Vector3Mad (ent->v.origin, ent->v.velocity, frametime, ent->v.origin);

	SV_LinkEdict (ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
void SV_CheckWaterTransition (edict_t *ent)
{
	int		cont;

	cont = SV_PointContents (ent->v.origin);

	if (!ent->v.watertype)
	{
		// just spawned here
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		return;
	}

	if (cont <= CONTENTS_WATER)
	{
		if (ent->v.watertype == CONTENTS_EMPTY)
		{
			// just crossed into water
			SV_StartSound (ent, 0, "misc/h2ohit1.wav", 255, 1);
		}

		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
	}
	else
	{
		if (ent->v.watertype != CONTENTS_EMPTY)
		{
			// just crossed into water
			SV_StartSound (ent, 0, "misc/h2ohit1.wav", 255, 1);
		}

		ent->v.watertype = CONTENTS_EMPTY;
		ent->v.waterlevel = cont;
	}
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
void SV_Physics_Toss (edict_t *ent, double frametime)
{
	trace_t	trace;
	vec3_t	move;
	float	backoff;

	// regular thinking
	if (!SV_RunThink (ent, frametime)) return;
	if (((int) ent->v.flags & FL_ONGROUND)) return;

	SV_CheckVelocity (ent);

	// add gravity
	if (ent->v.movetype != MOVETYPE_FLY && ent->v.movetype != MOVETYPE_FLYMISSILE)
		SV_AddGravity (ent, frametime);

	// move angles
	Vector3Mad (ent->v.angles, ent->v.avelocity, frametime, ent->v.angles);

	// move origin
	Vector3Scale (move, ent->v.velocity, frametime);
	trace = SV_PushEntity (ent, move);

	if (trace.fraction == 1) return;
	if (ent->free) return;

	if (ent->v.movetype == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else backoff = 1;

	ClipVelocity (ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

	// stop if on ground
	if (trace.plane.normal[2] > MIN_STEP_NORMAL)
	{
		if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE)
		{
			ent->v.flags = (int) ent->v.flags | FL_ONGROUND;
			ent->v.groundentity = EdictToProg (trace.ent);

			Vector3Copy (ent->v.velocity, vec3_origin);
			Vector3Copy (ent->v.avelocity, vec3_origin);
		}
	}

	// check for in water
	SV_CheckWaterTransition (ent);
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
=============
*/
void SV_Physics_Step (edict_t *ent, double frametime)
{
	// freefall if not onground
	if (!((int) ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM)))
	{
		bool hitsound = false;

		if (ent->v.velocity[2] < sv_gravity.value * -0.1)
			hitsound = true;
		else hitsound = false;

		SV_AddGravity (ent, frametime);
		SV_CheckVelocity (ent);
		SV_FlyMove (ent, frametime, NULL);
		SV_LinkEdict (ent, true);

		if ((int) ent->v.flags & FL_ONGROUND)	// just hit ground
		{
			if (hitsound)
				SV_StartSound (ent, 0, "demon/dland2.wav", 255, 1);
		}
	}

	// regular thinking
	SV_RunThink (ent, frametime);
	SV_CheckWaterTransition (ent);
}

//============================================================================


/*
================
SV_Physics

================
*/
void SV_Physics (double frametime)
{
	int hunkmark = TempHunk->GetLowMark ();

	// keep these off the stack - 1 MB (OUCH!)
	// the code doesn't check these for NULL at any point so we can keep them as fast allocs
	moved_edict = (edict_t **) TempHunk->FastAlloc (MAX_EDICTS * sizeof (edict_t *));
	moved_from = (vec3_t *) TempHunk->FastAlloc (MAX_EDICTS * sizeof (vec3_t));

	// let the progs know that a new frame has started
	SVProgs->GlobalStruct->time = sv.time;
	SVProgs->RunInteraction (SVProgs->Edicts, SVProgs->Edicts, SVProgs->GlobalStruct->StartFrame);

	//SV_CheckAllEnts ();

	// treat each object in turn
	edict_t *ent = SVProgs->Edicts;

	for (int i = 0; i < SVProgs->NumEdicts; i++, ent = NextEdict (ent))
	{
		if (ent->free)
			continue;

		if (SVProgs->GlobalStruct->force_retouch)
			SV_LinkEdict (ent, true);	// force retouch even for stationary

		if (i > 0 && i <= svs.maxclients)
			SV_Physics_Client (ent, i, frametime);
		else if (ent->v.movetype == MOVETYPE_PUSH)
			SV_Physics_Pusher (ent, frametime);
		else if (ent->v.movetype == MOVETYPE_NONE)
			SV_Physics_None (ent, frametime);
		else if (ent->v.movetype == MOVETYPE_FOLLOW) // Nehahra
			SV_Physics_Follow (ent, frametime);
		else if (ent->v.movetype == MOVETYPE_NOCLIP)
			SV_Physics_Noclip (ent, frametime);
		else if (ent->v.movetype == MOVETYPE_STEP)
			SV_Physics_Step (ent, frametime);
		else if (ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_BOUNCE ||
				 ent->v.movetype == MOVETYPE_FLY || ent->v.movetype == MOVETYPE_FLYMISSILE)
			SV_Physics_Toss (ent, frametime);
		else Sys_Error ("SV_Physics: bad movetype %i", (int) ent->v.movetype);
	}

	if (SVProgs->GlobalStruct->force_retouch)
		SVProgs->GlobalStruct->force_retouch--;

	// further attempts to access these are errors
	moved_edict = NULL;
	moved_from = NULL;
	TempHunk->FreeToLowMark (hunkmark);

	// accumulate the time as a double
	// (fixme - accumulating time this way is crap)
	sv.time += frametime;
}


trace_t SV_Trace_Toss (edict_t *ent, edict_t *ignore)
{
	edict_t	tempent, *tent;
	trace_t	trace;
	vec3_t	move;
	vec3_t	end;

	// needs to simulate different FPS because it comes from progs
	// (note - this is probably incorrect - should it be 0.1???)
	double frametime = 0.05;

	Q_MemCpy (&tempent, ent, sizeof (edict_t));
	tent = &tempent;

	for (;;)
	{
		SV_CheckVelocity (tent);
		SV_AddGravity (tent, frametime);
		Vector3Mad (tent->v.angles, tent->v.avelocity, frametime, tent->v.angles);
		Vector3Scale (move, tent->v.velocity, frametime);
		Vector3Add (end, tent->v.origin, move);
		trace = SV_Move (tent->v.origin, tent->v.mins, tent->v.maxs, end, MOVE_NORMAL, tent);
		Vector3Copy (tent->v.origin, trace.endpos);

		if (trace.ent)
			if (trace.ent != ignore)
				break;
	}

	return trace;
}
