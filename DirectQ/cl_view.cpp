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
// view.c -- player eye positioning
// renamed to cl_view to keep it grouped with the rest of the client code

#include "quakedef.h"


/*

The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.

*/


cvar_t	scr_ofsx ("scr_ofsx", "0");
cvar_t	scr_ofsy ("scr_ofsy", "0");
cvar_t	scr_ofsz ("scr_ofsz", "0");

cvar_t	cl_rollspeed ("cl_rollspeed", "200");
cvar_t	cl_rollangle ("cl_rollangle", "2.0");

cvar_t	cl_bob ("cl_bob", "0.02");
cvar_t	cl_bobcycle ("cl_bobcycle", "0.6");
cvar_t	cl_bobup ("cl_bobup", "0.5");

cvar_t	v_kicktime ("v_kicktime", "0.5", CVAR_ARCHIVE);
cvar_t	v_kickroll ("v_kickroll", "0.6", CVAR_ARCHIVE);
cvar_t	v_kickpitch ("v_kickpitch", "0.6", CVAR_ARCHIVE);
cvar_t  v_gunkick ("v_gunkick", "1", CVAR_ARCHIVE);

cvar_t	v_iyaw_cycle ("v_iyaw_cycle", "2");
cvar_t	v_iroll_cycle ("v_iroll_cycle", "0.5");
cvar_t	v_ipitch_cycle ("v_ipitch_cycle", "1");
cvar_t	v_iyaw_level ("v_iyaw_level", "0.3");
cvar_t	v_iroll_level ("v_iroll_level", "0.1");
cvar_t	v_ipitch_level ("v_ipitch_level", "0.3");

cvar_t	v_idlescale ("v_idlescale", "0");

cvar_t	crosshair ("crosshair", "3", CVAR_ARCHIVE);
cvar_t	cl_crossx ("cl_crossx", "0", CVAR_ARCHIVE);
cvar_t	cl_crossy ("cl_crossy", "0", CVAR_ARCHIVE);
cvar_t	scr_crosshairscale ("scr_crosshairscale", 1, CVAR_ARCHIVE);
cvar_t	scr_crosshaircolor ("scr_crosshaircolor", "0", CVAR_ARCHIVE);
cvar_alias_t crosshaircolor ("crosshaircolor", &scr_crosshaircolor);			
cvar_alias_t crosshairsize ("crosshairsize", &scr_crosshairscale);

// compatibility with a typo in fitz
cvar_alias_t scr_crosshaircale ("scr_crosshaircale", &scr_crosshairscale);

cvar_t	gl_cshiftpercent ("gl_cshiftpercent", "100");

cvar_t	v_damagedroprate ("v_damagedroprate", 150.0f);
cvar_t	v_bonusdroprate ("v_bonusdroprate", 100.0f);

cvar_t	v_gunangle ("v_gunangle", 2, CVAR_ARCHIVE);
cvar_alias_t r_gunangle ("r_gunangle", &v_gunangle);

double	v_dmg_time;
float	v_dmg_roll, v_dmg_pitch;

extern	int			in_forward, in_forward2, in_back;


cvar_t	v_centermove ("v_centermove", 0.15f);
cvar_t	v_centerspeed ("v_centerspeed", 250.0f);


void V_StartPitchDrift (void)
{
	if (cl.laststop >= cl.time)
	{
		// something else is keeping it from drifting
		return;
	}

	if (cl.nodrift || !cl.pitchvel)
	{
		cl.pitchvel = v_centerspeed.value;
		cl.nodrift = false;
		cl.driftmove = 0;
	}
}


void V_StopPitchDrift (void)
{
	// set above client time as CL_LerpPoint can change time before V_StartPitchDrift is called
	cl.laststop = cl.time + 1;
	cl.nodrift = true;
	cl.pitchvel = 0;
}


void V_DriftPitch (void)
{
	extern cvar_t freelook;

	// explicitly kill pitch drifting if we're freelooking
	if (noclip_anglehack || !cl.onground || cls.demoplayback || (in_mlook.state & 1) || freelook.integer)
	{
		V_StopPitchDrift ();
		cl.driftmove = 0;
	}
	else
	{
		// don't count small mouse motion
		if (cl.nodrift)
		{
			if (fabs (cl.cmd.forwardmove) < cl_forwardspeed.value)
				cl.driftmove = 0;
			else cl.driftmove += (cl.time - cl.drifttime);

			if (cl.driftmove > v_centermove.value)
				V_StartPitchDrift ();
		}
		else
		{
			float delta = cl.idealpitch - cl.viewangles[0];

			if (!delta)
				cl.pitchvel = 0;
			else
			{
				float move = (cl.time - cl.drifttime) * cl.pitchvel;

				cl.pitchvel += (cl.time - cl.drifttime) * v_centerspeed.value;

				if (delta > 0)
				{
					if (move > delta)
					{
						cl.pitchvel = 0;
						move = delta;
					}

					cl.viewangles[0] += move;
				}
				else if (delta < 0)
				{
					if (move > -delta)
					{
						cl.pitchvel = 0;
						move = -delta;
					}

					cl.viewangles[0] -= move;
				}
			}
		}
	}

	cl.drifttime = cl.time;
}


/*
===============
V_CalcRoll

Used by view and sv_user
no time dependencies
===============
*/
float V_CalcRoll (vec3_t angles, vec3_t velocity)
{
	float	sign;
	float	side;
	float	value;
	QMATRIX mrot;

	mrot.AngleVectors (angles);
	side = Vector3Dot (velocity, mrot.rt);
	sign = side < 0 ? -1 : 1;
	side = fabs (side);

	value = cl_rollangle.value;

	if (cl_rollspeed.value)
	{
		if (side < cl_rollspeed.value)
			side = side * value / cl_rollspeed.value;
		else side = value;
	}
	else side = value;

	return side * sign;
}


/*
===============
V_CalcBob

===============
*/
float V_CalcBob (void)
{
	// prevent division by 0 weirdness
	if (!cl_bob.value) return 0;
	if (!cl_bobup.value) return 0;
	if (!cl_bobcycle.value) return 0;

	// bound bob up
	if (cl_bobup.value >= 0.99f) cl_bobup.Set (0.99f);

	// WARNING - don't try anything sexy with time in here or you'll
	// screw things up and make the engine appear to run jerky
	float cycle = cl.time - (int) (cl.time / cl_bobcycle.value) * cl_bobcycle.value;
	cycle /= cl_bobcycle.value;

	if (cycle < cl_bobup.value)
		cycle = D3DX_PI * cycle / cl_bobup.value;
	else cycle = D3DX_PI + D3DX_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value);

	// bob is proportional to velocity in the xy plane
	// (don't count Z, or jumping messes it up)
	float bob = sqrt (cl.velocity[0] * cl.velocity[0] + cl.velocity[1] * cl.velocity[1]) * cl_bob.value;
	bob = bob * 0.3 + bob * 0.7 * sin (cycle);

	if (bob > 4)
		bob = 4;
	else if (bob < -7)
		bob = -7;

	return bob;
}


//=============================================================================


/*
==============================================================================

						PALETTE FLASHES

==============================================================================
*/


// default cshifts are too dark in GL so lighten them a little
#define	cshift_empty	130, 80, 50, 0
#define	cshift_water	130, 80, 50, 128
#define	cshift_slime	0, 25, 5, 150
#define	cshift_lava		255, 80, 0, 150

int all_cshifts[4][4] =
{
	{cshift_empty},
	{cshift_water},
	{cshift_slime},
	{cshift_lava}
};


#include "d3d_model.h"

void V_ScaleCShift (int *shift, int flags)
{
	int *thisshift = all_cshifts[0];

	if (flags & SURF_DRAWLAVA)
		thisshift = all_cshifts[3];
	else if (flags & SURF_DRAWSLIME)
		thisshift = all_cshifts[2];
	else if (flags & SURF_DRAWWATER)
		thisshift = all_cshifts[1];
	else thisshift = all_cshifts[0];

	int incominggs = (shift[0] * 30) + (shift[1] * 59) + (shift[2] * 11);
	int desiredgs = (thisshift[0] * 30) + (thisshift[1] * 59) + (thisshift[2] * 11);

	// because it's possible for a wacky modder to specify an all-black shift
	if (incominggs > 0 && desiredgs > 0)
	{
		shift[0] = (shift[0] * desiredgs) / incominggs;
		shift[1] = (shift[1] * desiredgs) / incominggs;
		shift[2] = (shift[2] * desiredgs) / incominggs;
	}
	else shift[0] = shift[1] = shift[2] = 0;
}


cvar_t cl_damagered ("cl_damagered", "3", CVAR_ARCHIVE);

/*
===============
V_ParseDamage
===============
*/
void V_ParseDamage (void)
{
	int		armor, blood;
	vec3_t	from;
	int		i;
	entity_t	*ent;
	float	side;
	float	count;

	armor = MSG_ReadByte ();
	blood = MSG_ReadByte ();

	for (i = 0; i < 3; i++)
		from[i] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);

	if (armor + blood > 0)
	{
		float armorpct = (float) armor / (float) (armor + blood);
		float bloodpct = (float) blood / (float) (armor + blood);

		// blend between the 2 classic extremes depending on how much damage was done to each component
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200 * armorpct + 255 * bloodpct;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 100 * armorpct;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100 * armorpct;

		if ((count = blood * 0.5 + armor * 0.5) < 10) count = 10;

		cl.faceanimtime = cl.time + 0.2f;
		cl.cshifts[CSHIFT_DAMAGE].percent += cl_damagered.value * count;

		if (cl.cshifts[CSHIFT_DAMAGE].percent < 0) cl.cshifts[CSHIFT_DAMAGE].percent = 0;
		if (cl.cshifts[CSHIFT_DAMAGE].percent > 150) cl.cshifts[CSHIFT_DAMAGE].percent = 150;

		cl.cshifts[CSHIFT_DAMAGE].initialpercent = cl.cshifts[CSHIFT_DAMAGE].percent;
		cl.cshifts[CSHIFT_DAMAGE].time = cl.time;

		// calculate view angle kicks
		ent = cls.entities[cl.viewentity];

		Vector3Subtract (from, from, ent->origin);
		Vector3Normalize (from);

		QMATRIX mrot;

		mrot.AngleVectors (ent->angles);

		side = Vector3Dot (from, mrot.rt);
		v_dmg_roll = count * side * v_kickroll.value;

		side = Vector3Dot (from, mrot.fw);

		v_dmg_pitch = count * side * v_kickpitch.value;
		v_dmg_time = cl.time + v_kicktime.value;
	}
}


void V_SetCShift (int cshift, int r, int g, int b, int pct)
{
	cl.cshifts[cshift].destcolor[0] = r;
	cl.cshifts[cshift].destcolor[1] = g;
	cl.cshifts[cshift].destcolor[2] = b;
	cl.cshifts[cshift].initialpercent = pct;
	cl.cshifts[cshift].percent = pct;
	cl.cshifts[cshift].time = cl.time;
}


/*
==================
V_cshift_f
==================
*/
void V_cshift_f (void)
{
	V_SetCShift (CSHIFT_VCSHIFT, atoi (Cmd_Argv (1)), atoi (Cmd_Argv (2)), atoi (Cmd_Argv (3)), atoi (Cmd_Argv (4)));
}


/*
==================
V_BonusFlash_f

When you run over an item, the server sends this command
==================
*/
void V_BonusFlash_f (void)
{
	V_SetCShift (CSHIFT_BONUS, 215, 186, 69, 50);
}


/*
=============
V_SetContentsColor

Underwater, lava, etc each has a color shift
=============
*/
void V_SetContentsColor (int contents)
{
	switch (contents)
	{
	case CONTENTS_EMPTY:
	case CONTENTS_SOLID:
		V_SetCShift (CSHIFT_CONTENTS, cshift_empty);
		break;
	case CONTENTS_LAVA:
		V_SetCShift (CSHIFT_CONTENTS, cshift_lava);
		break;
	case CONTENTS_SLIME:
		V_SetCShift (CSHIFT_CONTENTS, cshift_slime);
		break;
	default:
		V_SetCShift (CSHIFT_CONTENTS, cshift_water);
	}
}


void V_CalcIndividualBlend (float *rgba, float r, float g, float b, float a)
{
	// no blend
	if (a < 1) return;

	// calc alpha amount
	float a2 = a / 255.0;

	// evaluate blends
	rgba[3] = rgba[3] + a2 * (1 - rgba[3]);
	a2 = a2 / rgba[3];

	// blend it in
	rgba[0] = rgba[0] * (1 - a2) + r * a2;
	rgba[1] = rgba[1] * (1 - a2) + g * a2;
	rgba[2] = rgba[2] * (1 - a2) + b * a2;
}


/*
=============
V_CalcPowerupCShift
=============
*/
cvar_t v_quadcshift ("v_quadcshift", 1.0f);
cvar_t v_suitcshift ("v_suitcshift", 1.0f);
cvar_t v_ringcshift ("v_ringcshift", 1.0f);
cvar_t v_pentcshift ("v_pentcshift", 1.0f);

void V_CalcPowerupCShift (void)
{
	// default is no shift for no powerups
	float rgba[4] = {0, 0, 0, 0};

	// now let's see what we've got
	if ((cl.items & IT_QUAD) && v_quadcshift.value >= 0) V_CalcIndividualBlend (rgba, 0, 0, 255, (int) (30.0f * v_quadcshift.value));
	if ((cl.items & IT_SUIT) && v_suitcshift.value >= 0) V_CalcIndividualBlend (rgba, 0, 255, 0, (int) (20.0f * v_suitcshift.value));
	if ((cl.items & IT_INVISIBILITY) && v_ringcshift.value >= 0) V_CalcIndividualBlend (rgba, 100, 100, 100, (int) (100.0f * v_ringcshift.value));
	if ((cl.items & IT_INVULNERABILITY) && v_pentcshift.value >= 0) V_CalcIndividualBlend (rgba, 255, 255, 0, (int) (30.0f * v_pentcshift.value));

	// clamp blend 0-255 and store out
	V_SetCShift (CSHIFT_POWERUP, BYTE_CLAMP (rgba[0]), BYTE_CLAMP (rgba[1]), BYTE_CLAMP (rgba[2]), BYTE_CLAMPF (rgba[3]));
}


/*
=============
V_CalcBlend
=============
*/
cvar_t v_contentblend ("v_contentblend", 1.0f);
cvar_t v_damagecshift ("v_damagecshift", 1.0f);
cvar_t v_bonusflash ("v_bonusflash", 1.0f);
cvar_t v_powerupcshift ("v_powerupcshift", 1.0f);

cvar_t v_emptycshift ("v_emptycshift", 1.0f);
cvar_t v_solidcshift ("v_solidcshift", 1.0f);
cvar_t v_lavacshift ("v_lavacshift", 1.0f);
cvar_t v_watercshift ("v_watercshift", 1.0f);
cvar_t v_slimecshift ("v_slimecshift", 1.0f);

void V_AdjustContentCShift (int contents)
{
	if (contents == CONTENTS_SOLID && v_solidcshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_solidcshift.value;
	else if (contents == CONTENTS_EMPTY && v_emptycshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_emptycshift.value;
	else if (contents == CONTENTS_LAVA && v_lavacshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_lavacshift.value;
	else if (contents == CONTENTS_WATER && v_watercshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_watercshift.value;
	else if (contents == CONTENTS_SLIME && v_slimecshift.value >= 0)
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_slimecshift.value;
}


void V_CalcBlend (void)
{
	vid.cshift[0] = vid.cshift[1] = vid.cshift[2] = vid.cshift[3] = 0;

	for (int j = 0; j < NUM_CSHIFTS; j++)
	{
		if (j == CSHIFT_CONTENTS && v_contentblend.value >= 0) cl.cshifts[j].percent *= v_contentblend.value;
		if (j == CSHIFT_DAMAGE && v_damagecshift.value >= 0) cl.cshifts[j].percent *= v_damagecshift.value;
		if (j == CSHIFT_BONUS && v_bonusflash.value >= 0) cl.cshifts[j].percent *= v_bonusflash.value;
		if (j == CSHIFT_POWERUP && v_powerupcshift.value >= 0) cl.cshifts[j].percent *= v_powerupcshift.value;

		// no shift
		if (cl.cshifts[j].percent < 1) continue;

		V_CalcIndividualBlend (vid.cshift,
			cl.cshifts[j].destcolor[0],
			cl.cshifts[j].destcolor[1],
			cl.cshifts[j].destcolor[2],
			cl.cshifts[j].percent);
	}

	// take alpha shift to final scale
	vid.cshift[3] *= 255.0f;
}


void V_DropCShift (cshift_t *cs, float droprate)
{
	// dropping based on delta times is evil.
	if (cs->time < 0)
		cs->percent = 0;
	else if ((cs->percent = cs->initialpercent - (cl.time - cs->time) * droprate) <= 0)
	{
		cs->percent = 0;
		cs->time = -1;
	}
}


/*
=============
V_UpdatePalette

a lot of this can go away with a non-sw renderer
=============
*/
void V_UpdateCShifts (void)
{
	if (cl.intermission)
	{
		// certain cshifts are removed on intermission
		// (we keep contents as the intermission camera may be underwater)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;
		cl.cshifts[CSHIFT_BONUS].percent = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
		cl.cshifts[CSHIFT_VCSHIFT].percent = 0;
	}
	else
	{
		// drop the damage and bonus values
		V_DropCShift (&cl.cshifts[CSHIFT_DAMAGE], v_damagedroprate.value);
		V_DropCShift (&cl.cshifts[CSHIFT_BONUS], v_bonusdroprate.value);

		// add powerups
		V_CalcPowerupCShift ();
	}

	V_CalcBlend ();
}


/*
==============================================================================

						VIEW RENDERING

==============================================================================
*/

float angledelta (float a)
{
	a = anglemod (a);

	if (a > 180)
		a -= 360;

	return a;
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle (float *angles, float idlescale)
{
	// WARNING - don't try anything sexy with time in here or you'll
	// screw things up and make the engine appear to run jerky
	if (idlescale)
	{
		angles[2] += idlescale * sin (cl.time * v_iroll_cycle.value) * v_iroll_level.value;
		angles[0] += idlescale * sin (cl.time * v_ipitch_cycle.value) * v_ipitch_level.value;
		angles[1] += idlescale * sin (cl.time * v_iyaw_cycle.value) * v_iyaw_level.value;
	}
}


/*
==================
CalcGunAngle
==================
*/
void CalcGunAngle (void)
{
	float	yaw, pitch, move;
	static float oldyaw = 0;
	static float oldpitch = 0;

	yaw = r_refdef.viewangles[1];
	pitch = -r_refdef.viewangles[0];

	yaw = angledelta (yaw - r_refdef.viewangles[1]) * 0.4;

	if (yaw > 10) yaw = 10;
	if (yaw < -10) yaw = -10;

	pitch = angledelta (-pitch - r_refdef.viewangles[0]) * 0.4;

	if (pitch > 10) pitch = 10;
	if (pitch < -10) pitch = -10;

	move = (cl.time - cl.angletime) * 20;
	cl.angletime = cl.time;

	if (yaw > oldyaw)
	{
		if (oldyaw + move < yaw)
			yaw = oldyaw + move;
	}
	else
	{
		if (oldyaw - move > yaw)
			yaw = oldyaw - move;
	}

	if (pitch > oldpitch)
	{
		if (oldpitch + move < pitch)
			pitch = oldpitch + move;
	}
	else
	{
		if (oldpitch - move > pitch)
			pitch = oldpitch - move;
	}

	oldyaw = yaw;
	oldpitch = pitch;

	cl.viewent.angles[1] = r_refdef.viewangles[1] + yaw;
	cl.viewent.angles[0] = -(r_refdef.viewangles[0] + pitch);

	// take down the idlescale to prevent the gun from going nuts (also handle negative values here)
	if (v_idlescale.value > 0)
		V_AddIdle (cl.viewent.angles, -sqrt (v_idlescale.value));
	else if (v_idlescale.value < 0)
		V_AddIdle (cl.viewent.angles, sqrt (-v_idlescale.value));
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets (void)
{
	entity_t *ent;

	// fixme - are these two off???
	float mins[3] = {-14, -14, -22};
	float maxs[3] = {14, 14, 30};

	ent = cls.entities[cl.viewentity];

	// absolutely bound refresh relative to entity clipping hull
	// so the view can never be inside a solid wall
	for (int i = 0; i < 3; i++)
	{
		if (r_refdef.vieworigin[i] < ent->origin[i] + mins[i])
			r_refdef.vieworigin[i] = ent->origin[i] + mins[i];
		else if (r_refdef.vieworigin[i] > ent->origin[i] + maxs[i])
			r_refdef.vieworigin[i] = ent->origin[i] + maxs[i];
	}
}


/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll (void)
{
	float side;

	side = V_CalcRoll (cls.entities[cl.viewentity]->angles, cl.velocity);
	r_refdef.viewangles[2] += side;

	if (v_dmg_time >= cl.time)
	{
		float dmg_time = v_dmg_time - cl.time;

		if (v_kicktime.value)
		{
			r_refdef.viewangles[2] += dmg_time / v_kicktime.value * v_dmg_roll;
			r_refdef.viewangles[0] += dmg_time / v_kicktime.value * v_dmg_pitch;
		}
	}

	if (cl.stats[STAT_HEALTH] <= 0) r_refdef.viewangles[2] = 80;
}


/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef (void)
{
	entity_t	*ent, *view;

	// ent is the player model (visible when out of body)
	ent = cls.entities[cl.viewentity];

	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	Vector3Copy (r_refdef.vieworigin, ent->origin);
	Vector3Copy (r_refdef.viewangles, ent->angles);
	view->model = NULL;
}


/*
==================
V_CalcRefdef

==================
*/
cvar_t cl_stepsmooth ("cl_stepsmooth", "1", CVAR_ARCHIVE);
cvar_t cl_stepsmooth_mult ("cl_stepsmooth_mult", "80", CVAR_ARCHIVE);
cvar_t cl_stepsmooth_delta ("cl_stepsmooth_delta", "12", CVAR_ARCHIVE);

void V_CalcRefdef (void)
{
	V_DriftPitch ();

	// ent is the player model (visible when out of body)
	entity_t *ent = cls.entities[cl.viewentity];

	// view is the weapon model (only visible from inside body)
	entity_t *view = &cl.viewent;

	// transform the view offset by the model's matrix to get the offset from
	// model origin for the view.  the model should face the view dir.
	ent->angles[0] = -cl.viewangles[0];
	ent->angles[1] = cl.viewangles[1];
	ent->angles[2] = 0;

	float bob = V_CalcBob ();

	// refresh position
	Vector3Copy (r_refdef.vieworigin, ent->origin);
	r_refdef.vieworigin[2] += cl.viewheight + bob;

	// never let it sit exactly on a node line, because a water plane can
	// dissapear when viewed with the eye exactly on it.
	// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
	r_refdef.vieworigin[0] += 1.0 / 32;
	r_refdef.vieworigin[1] += 1.0 / 32;
	r_refdef.vieworigin[2] += 1.0 / 32;

	Vector3Copy (r_refdef.viewangles, cl.viewangles);
	V_CalcViewRoll ();
	V_AddIdle (r_refdef.viewangles, v_idlescale.value);

	// offsets - because entity pitches are actually backward
	float angles[] = {-ent->angles[0], ent->angles[1], ent->angles[2]};
	QMATRIX mrot;

	mrot.AngleVectors (angles);

	r_refdef.vieworigin[0] += scr_ofsx.value * mrot.fw[0] + scr_ofsy.value * mrot.rt[0] + scr_ofsz.value * mrot.up[0];
	r_refdef.vieworigin[1] += scr_ofsx.value * mrot.fw[1] + scr_ofsy.value * mrot.rt[1] + scr_ofsz.value * mrot.up[1];
	r_refdef.vieworigin[2] += scr_ofsx.value * mrot.fw[2] + scr_ofsy.value * mrot.rt[2] + scr_ofsz.value * mrot.up[2];

	V_BoundOffsets ();

	// set up gun position
	Vector3Copy (view->angles, cl.viewangles);

	CalcGunAngle ();

	Vector3Copy (view->origin, ent->origin);
	view->origin[2] += cl.viewheight;

	view->origin[0] += mrot.fw[0] * bob * 0.4;
	view->origin[1] += mrot.fw[1] * bob * 0.4;
	view->origin[2] += mrot.fw[2] * bob * 0.4;

	view->origin[2] += bob;

	// note - default equates to glquakes "viewsize 100" position.
	// fudging was only needed in software...
	// set to 0 to replicate darkplaces/fitzquake style
	view->origin[2] += v_gunangle.value * 0.75f;

	view->model = cl.model_precache[cl.stats[STAT_WEAPON]];
	view->frame = cl.stats[STAT_WEAPONFRAME];

	// set up the refresh position
	vec3_t kickangle;

	Vector3Scale (kickangle, cl.punchangle, v_gunkick.value);

	if (v_gunkick.value) Vector3Add (r_refdef.viewangles, r_refdef.viewangles, kickangle);

	// smooth out stair step ups
	if (cl.onground && (ent->origin[2] > cl.oldstepz) && cl_stepsmooth.value)
	{
		// * 2 to replicate bjp quake's setting and remove jerkiness on certain plats
		if (cl.steptime < cl.time) cl.oldstepz += (cl.time - cl.steptime) * cl_stepsmooth_mult.value * 2.0f;
		if (cl.oldstepz > ent->origin[2]) cl.oldstepz = ent->origin[2];
		if (ent->origin[2] - cl.oldstepz > cl_stepsmooth_delta.value) cl.oldstepz = ent->origin[2] - cl_stepsmooth_delta.value;

		r_refdef.vieworigin[2] += cl.oldstepz - ent->origin[2];
		view->origin[2] += cl.oldstepz - ent->origin[2];
	}
	else cl.oldstepz = ent->origin[2];

	cl.steptime = cl.time;

	if (chase_active.value) Chase_Update ();
}


/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
void V_RenderView (void)
{
	if (cls.maprunning && cl.worldmodel && cls.signon == SIGNON_CONNECTED)
	{
		// don't allow cheats in multiplayer
		if (cl.maxclients > 1)
		{
			scr_ofsx.Set (0.0f);
			scr_ofsy.Set (0.0f);
			scr_ofsz.Set (0.0f);
		}

		if (cl.intermission)
		{
			// intermission / finale rendering
			V_CalcIntermissionRefdef ();
		}
		else
		{
			if (!cl.paused)
				V_CalcRefdef ();
		}
	}
}


//============================================================================


void V_NewMap (void)
{
	for (int j = 0; j < NUM_CSHIFTS; j++)
	{
		cl.cshifts[j].initialpercent = 0;
		cl.cshifts[j].time = -1;
	}

	v_dmg_time = -1;
	cl.steptime = 0;
	cl.oldstepz = 9999999;
}


/*
=============
V_Init
=============
*/
cmd_t V_cshift_f_Cmd ("v_cshift", V_cshift_f);
cmd_t V_BonusFlash_f_Cmd ("bf", V_BonusFlash_f);

void V_Init (void)
{
}


