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

#include "quakedef.h"
#include "d3d_model.h"
#include "cl_fx.h"
#include "particles.h"

// spawn extra dynamic lights
cvar_t r_extradlight ("r_extradlight", "1", CVAR_ARCHIVE);

// the rate at which dynamic client-side effects are updated
cvar_t cl_effectrate ("cl_effectrate", 36.0f, CVAR_ARCHIVE);

// flickering (muzzleflash, brightlight, dimlight) effect rates
cvar_t cl_flickerrate ("cl_flickerrate", 10, CVAR_ARCHIVE);

// prevent wild flickering when running fast; this also keeps random effects synced with time which means they won't flicker when paused
#define FLICKERTABLE_SIZE	4096

int flickertable[FLICKERTABLE_SIZE];


void CL_InitFX (void)
{
	for (int i = 0; i < FLICKERTABLE_SIZE; i++)
		flickertable[i] = Q_fastrand ();
}


dlight_t *CL_FindDlight (int key)
{
	dlight_t *dl = NULL;
	float lowdie = 999999999.0f;
	int lownum = 0;

	// first look for an exact key match
	if (key)
	{
		dl = cls.dlights;

		for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof (*dl));
				dl->key = key;
				return dl;
			}
		}
	}

	// then look for anything else
	dl = cls.dlights;

	for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < lowdie)
		{
			// mark the one that will die soonest
			lowdie = dl->die;
			lownum = i;
		}

		if (dl->die < cl.time)
		{
			// first one that's died can go
			memset (dl, 0, sizeof (*dl));
			dl->key = key;
			return dl;
		}
	}

	// replace the one that's going to die soonest
	dl = &cls.dlights[lownum];
	memset (dl, 0, sizeof (*dl));
	dl->key = key;
	return dl;
}


void CL_ColourDlight (dlight_t *dl, int r, int g, int b)
{
	dl->rgb[0] = r;
	dl->rgb[1] = g;
	dl->rgb[2] = b;
}


dlight_t *CL_AllocDlight (int key)
{
	// find a dlight to use
	dlight_t *dl = CL_FindDlight (key);

	// set default colour for any we don't colour ourselves
	CL_ColourDlight (dl, DL_COLOR_WHITE);

	// initial dirty state is dirty
	dl->dirty = true;
	dl->dirtytime = cl.time;
	dl->spawntime = cl.time;

	// other defaults
	dl->minlight = 0;
	dl->decay = 0;
	dl->die = cl.time + 0.1f;
	dl->flags = 0;

	// done
	return dl;
}


void CL_DecayLights (void)
{
	dlight_t *dl = cls.dlights;
	double dirtytime = (cl_effectrate.value > 0) ? (1.0 / cl_effectrate.value) : 0;

	for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		// ensure that one frame flags get an update
		if (dl->flags & DLF_KILL) dl->die = cl.time + 1;
		if (dl->die < cl.time || (dl->radius <= 0)) continue;

		if (cl.time >= (dl->dirtytime + dirtytime))
		{
			// fps-independent dynamic light decay
			dl->radius = dl->initialradius - (cl.time - dl->spawntime) * dl->decay;
			dl->dirty = true;
			dl->dirtytime = cl.time;

			// DLF_KILL is handled here to ensure that the light lasts for at least one frame
			if (dl->radius <= 0 || (dl->flags & DLF_KILL))
			{
				dl->flags &= ~DLF_KILL;
				dl->die = -1;
				dl->spawntime = -1;
			}
		}
		else dl->dirty = false;
	}
}


void CL_MuzzleFlash (entity_t *ent, int entnum)
{
	QMATRIX av;
	dlight_t *dl;

	dl = CL_AllocDlight (entnum);
	dl->flags |= DLF_NOCORONA;

	if (entnum == cl.viewentity)
	{
		// switch the flash colour for the current weapon
		if (cl.stats[STAT_ACTIVEWEAPON] == IT_SUPER_LIGHTNING)
			CL_ColourDlight (dl, DL_COLOR_BLUE);
		else if (cl.stats[STAT_ACTIVEWEAPON] == IT_LIGHTNING)
			CL_ColourDlight (dl, DL_COLOR_BLUE);
		else CL_ColourDlight (dl, DL_COLOR_ORANGE);
	}
	else
	{
		// some entities have different attacks resulting in a different flash colour
		if (ent->model->flags & EF_WIZARDFLASH)
			CL_ColourDlight (dl, DL_COLOR_GREEN);
		else if (ent->model->flags & EF_SHALRATHFLASH)
			CL_ColourDlight (dl, DL_COLOR_PURPLE);
		else if (ent->model->flags & EF_SHAMBLERFLASH)
			CL_ColourDlight (dl, DL_COLOR_BLUE);
		else if (ent->model->flags & EF_ORANGEFLASH)
			CL_ColourDlight (dl, DL_COLOR_ORANGE);
		else if (ent->model->flags & EF_REDFLASH)
			CL_ColourDlight (dl, DL_COLOR_RED);
		else if (ent->model->flags & EF_YELLOWFLASH)
			CL_ColourDlight (dl, DL_COLOR_YELLOW);
		else CL_ColourDlight (dl, DL_COLOR_ORANGE);
	}

	Vector3Copy (dl->origin, ent->origin);
	dl->origin[2] += 16;
	av.AngleVectors (ent->angles);

	Vector3Mad (dl->origin, av.fw, 18.0f, dl->origin);

	dl->radius = 200 + (flickertable[(entnum + (int) (cl.time * cl_flickerrate.value)) & (FLICKERTABLE_SIZE - 1)] & 31);
	dl->initialradius = dl->radius;
	dl->minlight = 32;

	// the server clears muzzleflashes after each frame, but as the client is now running faster, it won't get the message for several
	// frames - potentially over 10.  therefore we should also clear the flash on the client too.  this also fixes demos ;)
	ent->effects &= ~EF_MUZZLEFLASH;
}


void CL_BrightLight (entity_t *ent, int entnum)
{
	// uncoloured
	dlight_t *dl = CL_AllocDlight (entnum);

	Vector3Copy (dl->origin, ent->origin);
	dl->origin[2] += 16;
	dl->radius = 400 + (flickertable[(entnum + (int) (cl.time * cl_flickerrate.value)) & (FLICKERTABLE_SIZE - 1)] & 31);
	dl->initialradius = dl->radius;
	dl->flags = DLF_NOCORONA | DLF_KILL;
}


void CL_DimLight (entity_t *ent, int entnum)
{
	dlight_t *dl = CL_AllocDlight (entnum);

	Vector3Copy (dl->origin, ent->origin);
	dl->radius = 200 + (flickertable[(entnum + (int) (cl.time * cl_flickerrate.value)) & (FLICKERTABLE_SIZE - 1)] & 31);
	dl->initialradius = dl->radius;
	dl->flags = DLF_KILL;

	// powerup dynamic lights
	if (entnum == cl.viewentity)
	{
		// if it's a powerup coming from the viewent then we remove the corona
		dl->flags |= DLF_NOCORONA;

		// and set the appropriate colour depending on the current powerup(s)
		if ((cl.items & IT_QUAD) && (cl.items & IT_INVULNERABILITY))
			CL_ColourDlight (dl, DL_COLOR_PURPLE);
		else if (cl.items & IT_QUAD)
			CL_ColourDlight (dl, DL_COLOR_BLUE);
		else if (cl.items & IT_INVULNERABILITY)
			CL_ColourDlight (dl, DL_COLOR_RED);
		else CL_ColourDlight (dl, DL_COLOR_WHITE);
	}
}


void CL_TrailLight (entity_t *ent, int entnum, int r, int g, int b)
{
	dlight_t *dl = CL_AllocDlight (entnum);

	Vector3Copy (dl->origin, ent->origin);
	dl->radius = 200;
	dl->initialradius = dl->radius;
	dl->flags |= DLF_KILL;

	CL_ColourDlight (dl, r, g, b);
}


void CL_WizardTrail (entity_t *ent, int entnum)
{
	if (r_extradlight.value)
		CL_TrailLight (ent, entnum, DL_COLOR_GREEN);

	ParticleSystem.RocketTrail (ent->oldorg, ent->origin, RT_WIZARD);
}


void CL_KnightTrail (entity_t *ent, int entnum)
{
	if (r_extradlight.value)
		CL_TrailLight (ent, entnum, DL_COLOR_ORANGE);

	ParticleSystem.RocketTrail (ent->oldorg, ent->origin, RT_KNIGHT);
}


void CL_RocketTrail (entity_t *ent, int entnum)
{
	CL_TrailLight (ent, entnum, DL_COLOR_ORANGE);
	ParticleSystem.RocketTrail (ent->oldorg, ent->origin, RT_ROCKET);
}


void CL_VoreTrail (entity_t *ent, int entnum)
{
	if (r_extradlight.value)
		CL_TrailLight (ent, entnum, DL_COLOR_PURPLE);

	ParticleSystem.RocketTrail (ent->oldorg, ent->origin, RT_VORE);
}


// old cl_tent stuff; appropriate here

#include <vector>

struct beam_t
{
	int		entity;
	struct model_t	*model;
	float	endtime;
	vec3_t	start, end;
	int		type;
};

std::vector<beam_t *> cl_beams;

sfx_t			*cl_sfx_wizhit;
sfx_t			*cl_sfx_knighthit;
sfx_t			*cl_sfx_tink1;
sfx_t			*cl_sfx_ric1;
sfx_t			*cl_sfx_ric2;
sfx_t			*cl_sfx_ric3;
sfx_t			*cl_sfx_r_exp3;

void D3D_AddVisEdict (entity_t *ent);

float anglestable[1024];

/*
=================
CL_InitTEnts
=================
*/
model_t	*cl_bolt1_mod = NULL;
model_t	*cl_bolt2_mod = NULL;
model_t	*cl_bolt3_mod = NULL;
model_t *cl_beam_mod = NULL;

void CL_InitTEnts (void)
{
	// set up the angles table
	for (int i = 0; i < 1024; i++)
		anglestable[i] = Q_fastrand () % 360;

	// we need to load these too as models are being cleared between maps
	cl_bolt1_mod = Mod_ForName ("progs/bolt.mdl", true);
	cl_bolt2_mod = Mod_ForName ("progs/bolt2.mdl", true);
	cl_bolt3_mod = Mod_ForName ("progs/bolt3.mdl", true);

	// don't crash as this isn't in ID1
	cl_beam_mod = Mod_ForName ("progs/beam.mdl", false);

	// sounds
	cl_sfx_wizhit = S_PrecacheSound ("wizard/hit.wav");
	cl_sfx_knighthit = S_PrecacheSound ("hknight/hit.wav");
	cl_sfx_tink1 = S_PrecacheSound ("weapons/tink1.wav");
	cl_sfx_ric1 = S_PrecacheSound ("weapons/ric1.wav");
	cl_sfx_ric2 = S_PrecacheSound ("weapons/ric2.wav");
	cl_sfx_ric3 = S_PrecacheSound ("weapons/ric3.wav");
	cl_sfx_r_exp3 = S_PrecacheSound ("weapons/r_exp3.wav");

	// ensure on each map load
	cl_beams.clear ();
}


/*
=================
CL_ParseBeam
=================
*/
void CL_SetupBeam (beam_t *b, model_t *m, int ent, int type, float *start, float *end)
{
	b->entity = ent;
	b->model = m;
	b->type = type;
	b->endtime = cl.time + 0.2f;
	Vector3Copy (b->start, start);
	Vector3Copy (b->end, end);
}


void CL_ParseBeam (model_t *m, int ent, int type, float *start, float *end)
{
	// if the model didn't load just ignore it
	if (!m) return;

	beam_t *b = NULL;

	// override any beam with the same entity
	for (int i = 0; i < cl_beams.size (); i++)
	{
		b = cl_beams[i];

		if (b->entity == ent)
		{
			CL_SetupBeam (b, m, ent, type, start, end);
			return;
		}
	}

	// find a free beam
	for (int i = 0; i < cl_beams.size (); i++)
	{
		b = cl_beams[i];

		if (!b->model || b->endtime < cl.time)
		{
			CL_SetupBeam (b, m, ent, type, start, end);
			return;
		}
	}

	// alloc a new beam
	b = (beam_t *) MainHunk->Alloc (sizeof (beam_t));
	cl_beams.push_back (b);
	b = cl_beams[cl_beams.size () - 1];

	// set it's properties
	CL_SetupBeam (b, m, ent, type, start, end);
}


/*
=================
CL_ParseTEnt
=================
*/
void CL_SetTEntDLight (float *pos, float radius, float die, float decay, int r, int g, int b, bool readcolors)
{
	dlight_t *dl = CL_AllocDlight (0);

	Vector3Copy (dl->origin, pos);

	dl->radius = radius;
	dl->initialradius = dl->radius;
	dl->die = cl.time + die;
	dl->decay = decay;

	if (readcolors)
	{
		dl->rgb[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags) * 255;
		dl->rgb[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags) * 255;
		dl->rgb[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags) * 255;
	}
	else CL_ColourDlight (dl, r, g, b);
}


void CL_ReadTEntCoord (float *coord)
{
	coord[0] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
	coord[1] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
	coord[2] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
}


void CL_ParseBeamTEnt (model_t *m, int type)
{
	vec3_t start, end;
	int ent = MSG_ReadShort ();

	CL_ReadTEntCoord (start);
	CL_ReadTEntCoord (end);
	CL_ParseBeam (m, ent, type, start, end);
}


void CL_ParseRainSnowTEnt (int type)
{
	vec3_t pos, pos2, dir;

	CL_ReadTEntCoord (pos);
	CL_ReadTEntCoord (pos2);
	CL_ReadTEntCoord (dir);

	int count = MSG_ReadShort (); // number of particles
	int colorStart = MSG_ReadByte (); // color

	ParticleSystem.Rain (pos, pos2, dir, count, colorStart, type);
}


void CL_ParseSpikeTEnt (int count)
{
	vec3_t pos;

	CL_ReadTEntCoord (pos);
	ParticleSystem.WallHitParticles (pos, vec3_origin, 0, count);

	if (Q_fastrand () % 5)
		S_StartSound (-1, 0, cl_sfx_tink1, pos, 1, 1);
	else
	{
		int rnd = Q_fastrand () & 3;

		if (rnd == 1)
			S_StartSound (-1, 0, cl_sfx_ric1, pos, 1, 1);
		else if (rnd == 2)
			S_StartSound (-1, 0, cl_sfx_ric2, pos, 1, 1);
		else S_StartSound (-1, 0, cl_sfx_ric3, pos, 1, 1);
	}
}


void CL_ParseKnightWizardSpike (int partcolor, int partcount, sfx_t *sfx, int r, int g, int b)
{
	vec3_t pos;

	CL_ReadTEntCoord (pos);

	if (r_extradlight.value)
		CL_SetTEntDLight (pos, 250, 0.5, 300, r, g, b);

	ParticleSystem.WallHitParticles (pos, vec3_origin, partcolor, partcount);
	S_StartSound (-1, 0, sfx, pos, 1, 1);
}


void CL_ParseTEnt (void)
{
	vec3_t pos;
	int type = MSG_ReadByte ();

	switch (type)
	{
	case TE_WIZSPIKE: CL_ParseKnightWizardSpike (20, 30, cl_sfx_wizhit, DL_COLOR_GREEN); break;
	case TE_KNIGHTSPIKE: CL_ParseKnightWizardSpike (226, 20, cl_sfx_knighthit, DL_COLOR_ORANGE); break;

	case TE_SPIKE: CL_ParseSpikeTEnt (10); break;
	case TE_SUPERSPIKE: CL_ParseSpikeTEnt (20); break;

	case TE_GUNSHOT:			// bullet hitting wall
		CL_ReadTEntCoord (pos);
		ParticleSystem.WallHitParticles (pos, vec3_origin, 0, 20);
		break;

	case TE_EXPLOSION:			// rocket explosion
		CL_ReadTEntCoord (pos);
		ParticleSystem.Explosion (pos);
		CL_SetTEntDLight (pos, 350, 0.5, 300, DL_COLOR_ORANGE);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_TAREXPLOSION:			// tarbaby explosion
		CL_ReadTEntCoord (pos);
		ParticleSystem.BlobExplosion (pos);

		if (r_extradlight.value)
			CL_SetTEntDLight (pos, 350, 0.5, 300, DL_COLOR_PURPLE);

		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_LIGHTNING1: CL_ParseBeamTEnt (cl_bolt1_mod, TE_LIGHTNING1); break;
	case TE_LIGHTNING2: CL_ParseBeamTEnt (cl_bolt2_mod, TE_LIGHTNING2); break;
	case TE_LIGHTNING3: CL_ParseBeamTEnt (cl_bolt3_mod, TE_LIGHTNING3); break;
	case TE_BEAM:		CL_ParseBeamTEnt (cl_beam_mod,	TE_BEAM); break;

	case TE_LAVASPLASH:
		CL_ReadTEntCoord (pos);
		ParticleSystem.LavaSplash (pos);
		break;

	case TE_TELEPORT:
		CL_ReadTEntCoord (pos);
		ParticleSystem.TeleportSplash (pos);
		break;

	case TE_EXPLOSION2:				// color mapped explosion
		{
			CL_ReadTEntCoord (pos);

			int colorStart = MSG_ReadByte ();
			int colorLength = MSG_ReadByte ();

			ParticleSystem.Explosion (pos, colorStart, colorLength);
			CL_SetTEntDLight (pos, 350, 0.5, 300, DL_COLOR_WHITE);

			S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		}
		break;

	case TE_SMOKE:
		// falls through to explosion 3
		CL_ReadTEntCoord (pos);
		MSG_ReadByte();	// swallow

	case TE_EXPLOSION3:
		CL_ReadTEntCoord (pos);
		CL_SetTEntDLight (pos, 350, 0.5, 300, DL_COLOR_WHITE, true);
		S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
		break;

	case TE_LIGHTNING4:
		{
			// need to do it this way for correct parsing order
			char *modelname = MSG_ReadString ();
			CL_ParseBeamTEnt (Mod_ForName (modelname, true), TE_LIGHTNING4);
		}
		break;

	case TE_NEW1: break;
	case TE_NEW2: break;

	case TE_PARTICLESNOW: CL_ParseRainSnowTEnt (1); break;
	case TE_PARTICLERAIN: CL_ParseRainSnowTEnt (0); break;

	default:
		// no need to crash the engine but we will crash the map, as it means we have
		// a malformed packet
		Host_Error ("CL_ParseTEnt: bad type %i\n", type);
		break;
	}
}


/*
=================
CL_UpdateTEnts
=================
*/
void CL_UpdateTEnts (void)
{
	vec3_t	    dist, org;
	entity_t    *ent;
	float	    yaw, pitch;
	float	    forward;

	// hack - cl.time goes to 0 before some maps are fully flushed which can cause invalid
	// beam entities to be added to the list, so need to test for that (this can cause
	// crashes on maps where you give yourself the lightning gun and then issue a changelevel)
	// hmmm - i think this one was more like i being used in both the inner and outer loops.
	if (cl.time < 0.1f) return;

	// no server
	//if (!sv.active) return;

	// no beams while a server is paused (if running locally)
	if (sv.active && sv.paused) return;

	// no beams allocated
	if (!cl_beams.size ()) return;

	// update lightning
	for (int beamnum = 0; beamnum < cl_beams.size (); beamnum++)
	{
		beam_t *b = cl_beams[beamnum];

		if (!b->model || b->endtime < cl.time) continue;

		// if coming from the player, update the start position
		if (b->entity == cl.viewentity)
			Vector3Copy (b->start, cls.entities[cl.viewentity]->origin);

		// calculate pitch and yaw
		Vector3Subtract (dist, b->end, b->start);

		if (dist[1] == 0 && dist[0] == 0)
		{
			// linear so pythagoras can have his coffee break
			yaw = 0;

			if (dist[2] > 0)
				pitch = 90;
			else pitch = 270;
		}
		else
		{
			forward = sqrt (dist[0] * dist[0] + dist[1] * dist[1]);
			pitch = (int) (atan2 (dist[2], forward) * 180 / D3DX_PI);
			yaw = (int) (atan2 (dist[1], dist[0]) * 180 / D3DX_PI);

			if (yaw < 0) yaw += 360;
			if (pitch < 0) pitch += 360;
		}

		// add new entities for the lightning
		Vector3Copy (org, b->start);
		float d = Vector3Normalize (dist);
		int dlstep = 0;
		dlight_t *dl;

		while (d > 0)
		{
			// because we release TempHunk at the end of each frame we can safely do this
			ent = (entity_t *) TempHunk->Alloc (sizeof (entity_t));

			ent->model = b->model;
			ent->alphaval = 0;
			ent->angles[0] = pitch;
			ent->angles[1] = yaw;
			ent->angles[2] = anglestable[(int) ((cl.time * cl_effectrate.value) + d) & 1023];

			// i is no longer on the outer loop
			for (int i = 0; i < 3; i++)
			{
				ent->origin[i] = org[i];
				org[i] += dist[i] * 30;
			}

			if (r_extradlight.value && ((++dlstep) > 8) && cl.time >= cl.nexteffecttime)
			{
				switch (b->type)
				{
				case TE_LIGHTNING1:
				case TE_LIGHTNING2:
				case TE_LIGHTNING3:
				case TE_LIGHTNING4:
					dl = CL_AllocDlight (0);

					Vector3Copy (dl->origin, org);
					dl->radius = 250 + (flickertable[(int) (d + (cl.time * cl_flickerrate.value)) & (FLICKERTABLE_SIZE - 1)] & 31);
					dl->initialradius = dl->radius;
					dl->flags |= (DLF_NOCORONA | DLF_KILL);

					CL_ColourDlight (dl, DL_COLOR_BLUE);
					break;

				default: break;
				}

				dlstep = 0;
			}

			// add a visedict for it
			D3D_AddVisEdict (ent);

			d -= 30;
		}
	}
}


