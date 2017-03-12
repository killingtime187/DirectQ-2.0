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

// contains all particle code which is actually spawned from the client, i.e. is not really a part of the renderer

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "particles.h"
#include "cl_fx.h"

extern cvar_t r_particlesize;
extern cvar_t r_drawparticles;
extern cvar_t sv_gravity;
extern cvar_t r_particlestyle;

extern float r_avertexnormals[162][3];

QPARTICLESYSTEM ParticleSystem;

QPARTICLESYSTEM::QPARTICLESYSTEM (void)
{
	// ensure clean state on construction
	this->ClearParticles ();
}


void QPARTICLESYSTEM::StandardParticle (particle_t *p, float *org, float *dir, int color)
{
	p->v.die = cl.time + (0.1 * (Q_fastrand () % 5));
	p->v.color = d3d_QuakePalette.standard11[((color & ~7) + (Q_fastrand () & 7)) & 255];
	p->v.grav = -1;

	for (int j = 0; j < 3; j++)
	{
		p->v.org[j] = org[j] + ((Q_fastrand () & 15) - 8);
		p->v.vel[j] = dir[j] * 15;
	}
}


void QPARTICLESYSTEM::SetExplosionVelocity (float *vel)
{
	vel[0] = (Q_fastrand () % 512) - 256;
	vel[1] = (Q_fastrand () % 512) - 256;
	vel[2] = (Q_fastrand () % 512) - 256;
}


void QPARTICLESYSTEM::SetExplosionOrigin (float *orgout, float *orgin, int num)
{
	orgout[0] = orgin[0] + this->ExplosionOrigins[num & (MAX_EXPLOSION_ORIGINS - 1)][0];
	orgout[1] = orgin[1] + this->ExplosionOrigins[num & (MAX_EXPLOSION_ORIGINS - 1)][1];
	orgout[2] = orgin[2] + this->ExplosionOrigins[num & (MAX_EXPLOSION_ORIGINS - 1)][2];
}


void QPARTICLESYSTEM::SetRamp (partvert_t *v, int rampnum, float baseramp, float ramptime)
{
	// the point at which a colour ramp dies, padded with one at the start so that !p->v.ramp is valid
	float rampdie[4] = {0, 9, 9, 7};

	v->color = 0;
	v->rampnum = rampnum;
	v->baseramp = baseramp;
	v->ramptime = ramptime;
	v->die = cl.time + (rampdie[v->rampnum] - 0.5f - v->baseramp) / v->ramptime;
}


void QPARTICLESYSTEM::InitParticles (void)
{
	// change r_telesplash_normals too
	for (int i = -16; i < 16; i += 4)
	{
		for (int j = -16; j < 16; j += 4)
		{
			for (int k = -24; k < 32; k += 4)
			{
				float *norm = this->TeleSplashNormals[(i + 16) >> 2][(j + 16) >> 2][(k + 24) >> 2];

				norm[0] = j * 8;
				norm[1] = i * 8;
				norm[2] = k * 8;

				Vector3Normalize (norm);
			}
		}
	}

	for (int i = 0; i < MAX_EXPLOSION_ORIGINS; i++)
	{
		// define the base origin
		this->ExplosionOrigins[i][0] = (Q_fastrand () % 32) - 16;
		this->ExplosionOrigins[i][1] = (Q_fastrand () % 32) - 16;
		this->ExplosionOrigins[i][2] = (Q_fastrand () % 32) - 16;

		// convert origin to unit length then expand out again to form a rough spherical area
		// otherwise explosions will appear to form a box shape
		Vector3Normalize (this->ExplosionOrigins[i]);

		// and bring it back up to our desired scale
		this->ExplosionOrigins[i][0] *= 16.0f;
		this->ExplosionOrigins[i][1] *= 16.0f;
		this->ExplosionOrigins[i][2] *= 16.0f;
	}

	for (int i = 0; i < NUMVERTEXNORMALS; i++)
	{
		this->AVelocities[i][0] = (float) (Q_fastrand () & 255) * 0.01;
		this->AVelocities[i][1] = (float) (Q_fastrand () & 255) * 0.01;
		this->EntPartScales[i] = 0.75f + (float) (Q_fastrand () % 5001) * 0.0001f;
	}

	// default velocity change and gravity
	this->DefaultParticle.v.dvel[0] = this->DefaultParticle.v.dvel[1] = 0;
	this->DefaultParticle.v.grav = 0;

	// colour and ramps
	this->DefaultParticle.v.color = 0;
	this->DefaultParticle.v.rampnum = 0;
	this->DefaultParticle.v.baseramp = 0;
	this->DefaultParticle.v.ramptime = 0;
	this->DefaultParticle.v.die = -1;
}


void QPARTICLESYSTEM::ClearParticles (void)
{
	// these need to be wiped immediately on going to a new server
	// ...it's faster to just alloc one at a time as needed than it is to pre-alloc or alloc in batches...
	this->ActiveEmitters = NULL;
	this->FreeEmitters = NULL;
	this->FreeParticles = NULL;
}


emitter_t *QPARTICLESYSTEM::NewEmitter (vec3_t spawnorg)
{
	if (!this->FreeEmitters)
	{
		this->FreeEmitters = (emitter_t *) MainHunk->FastAlloc (sizeof (emitter_t));
		this->FreeEmitters->next = NULL;
	}

	// just take from the free list
	emitter_t *pe = this->FreeEmitters;
	this->FreeEmitters = pe->next;

	// no particles yet
	pe->particles = NULL;

	// copy across origin
	Vector3Copy (pe->spawnorg, spawnorg);
	pe->spawntime = cl.time;

	// link it in
	pe->next = this->ActiveEmitters;
	this->ActiveEmitters = pe;

	// done
	return pe;
}


particle_t *QPARTICLESYSTEM::NewParticle (emitter_t *pe)
{
	if (!this->FreeParticles)
	{
		this->FreeParticles = (particle_t *) MainHunk->FastAlloc (sizeof (particle_t));
		this->FreeParticles->next = NULL;
	}

	// just take from the free list
	particle_t *p = this->FreeParticles;
	this->FreeParticles = p->next;

	// set default drawing parms (may be overwritten as desired)
	Q_MemCpy (p, &this->DefaultParticle, sizeof (particle_t));
	p->v.scale = 0.75f + (float) (Q_fastrand () % 5001) * 0.0001f;

	// link it in
	p->next = pe->particles;
	pe->particles = p;

	// done
	return p;
}


void QPARTICLESYSTEM::EntityParticles (entity_t *ent)
{
	particle_t	*p;
	float		sp, sy, cp, cy;
	float		forward[3];

	emitter_t *pe = this->NewEmitter (ent->origin);

	for (int i = 0; i < NUMVERTEXNORMALS; i++)
	{
		if (!(p = this->NewParticle (pe))) return;

		Q_sincos (cl.time * this->AVelocities[i][0], &sy, &cy);
		Q_sincos (cl.time * this->AVelocities[i][0], &sp, &cp);

		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;

		// these particles should be automatically killed after 1 frame
		p->killflag = P_KILLFRAME;
		p->v.grav = -1;
		p->v.scale = this->EntPartScales[i];

		this->SetRamp (&p->v, 1, 0, 10);

		p->v.dvel[0] = p->v.dvel[1] = 4;

		p->v.org[0] = ent->origin[0] + r_avertexnormals[i][0] * 64 + forward[0] * 16;
		p->v.org[1] = ent->origin[1] + r_avertexnormals[i][1] * 64 + forward[1] * 16;
		p->v.org[2] = ent->origin[2] + r_avertexnormals[i][2] * 64 + forward[2] * 16;
	}
}


void QPARTICLESYSTEM::PointFile (void)
{
	char name[MAX_PATH];

	Q_snprintf (name, 128, "%s/maps/%s.pts", com_gamedir, sv.name);

	// we don't expect that these will ever be in PAKs
	std::ifstream f (name);

	if (!f.is_open ())
	{
		Con_Printf ("couldn't open %s\n", name);
		return;
	}

	Con_Printf ("Reading %s...\n", name);
	f >> std::skipws;

	int c, r;
	emitter_t *pe = NULL;

	for (c = 0, r = 0; ; c++)
	{
		vec3_t org;
		particle_t *p;

		f >> org[0] >> org[1] >> org[2];

		if (f.fail ()) break;

		// create a new emitter every 2048 particles so as to not overflow the vertex buffer
		if (!(c & 2047)) pe = this->NewEmitter (vec3_origin);

		if (!(p = this->NewParticle (pe)))
		{
			// woah!  this was a return!  NOT clever!
			Con_Printf ("Pointfile overflow - ");
			break;
		}

		// make these easier to see
		p->v.scale = 3.0f;
		p->v.die = cl.time + 999999;
		p->killflag = P_NEVERKILL;
		p->v.color = d3d_QuakePalette.standard11[((-c) & 15)];
		Vector3Copy (p->v.vel, vec3_origin);
		Vector3Copy (p->v.org, org);
	}

	f.close ();
	Con_Printf ("%i points read\n", c);
}


void QPARTICLESYSTEM::ParseEffect (void)
{
	vec3_t org, dir;
	int i, count, color;

	for (i = 0; i < 3; i++) org[i] = MSG_ReadCoord (cl.Protocol, cl.PrototcolFlags);
	for (i = 0; i < 3; i++) dir[i] = MSG_ReadChar () * (1.0 / 16);

	count = MSG_ReadByte ();
	color = MSG_ReadByte ();

	this->RunEffect (org, dir, color, count);
}


void QPARTICLESYSTEM::Explosion (vec3_t org)
{
	particle_t *p;
	emitter_t *pe = this->NewEmitter (org);
	int base = Q_fastrand ();

	for (int i = 0; i < 1024; i++)
	{
		if (!(p = this->NewParticle (pe))) return;

		p->v.grav = -1;

		if (i & 1)
		{
			this->SetRamp (&p->v, 1, Q_fastrand () & 3, 10);
			p->v.dvel[0] = p->v.dvel[1] = 4;
		}
		else
		{
			this->SetRamp (&p->v, 2, Q_fastrand () & 3, 15);
			p->v.dvel[0] = p->v.dvel[1] = -1;
		}

		this->SetExplosionVelocity (p->v.vel);
		this->SetExplosionOrigin (p->v.org, org, base + i);
	}
}


void QPARTICLESYSTEM::Explosion (vec3_t org, int colorStart, int colorLength)
{
	int			i;
	particle_t	*p;
	int			colorMod = 0;
	int			base = Q_fastrand ();

	emitter_t *pe = this->NewEmitter (org);

	for (i = 0; i < 512; i++)
	{
		if (!(p = this->NewParticle (pe))) return;

		p->v.die = cl.time + 0.3f;
		p->v.color = d3d_QuakePalette.standard11[(colorStart + (colorMod % colorLength)) & 255];
		colorMod++;

		p->v.grav = -1;
		p->v.dvel[0] = p->v.dvel[1] = 4;

		this->SetExplosionVelocity (p->v.vel);
		this->SetExplosionOrigin (p->v.org, org, base + i);
	}
}


void QPARTICLESYSTEM::BlobExplosion (vec3_t org)
{
	int			i;
	particle_t	*p;
	int			base = Q_fastrand ();

	emitter_t *pe = this->NewEmitter (org);

	for (i = 0; i < 1024; i++)
	{
		if (!(p = this->NewParticle (pe))) return;

		p->v.die = cl.time + (1 + (Q_fastrand () & 8) * 0.05);
		p->v.grav = -1;

		if (i & 1)
		{
			p->v.dvel[0] = p->v.dvel[1] = 4;
			p->v.color = d3d_QuakePalette.standard11[66 + (Q_fastrand () % 6)];
		}
		else
		{
			p->v.dvel[0] = -4;
			p->v.dvel[1] = 0;
			p->v.color = d3d_QuakePalette.standard11[150 + (Q_fastrand () % 6)];
		}

		this->SetExplosionVelocity (p->v.vel);
		this->SetExplosionOrigin (p->v.org, org, base + i);
	}
}


void QPARTICLESYSTEM::Blood (vec3_t org, vec3_t dir, int color, int count)
{
	int			i;
	particle_t	*p;

	emitter_t *pe = this->NewEmitter (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = this->NewParticle (pe))) return;

		this->StandardParticle (p, org, dir, color);
	}
}


void QPARTICLESYSTEM::RunEffect (vec3_t org, vec3_t dir, int color, int count)
{
	int			i;
	particle_t	*p;

	// hack the effect type out of the parameters
	if (hipnotic && count <= 4)
	{
		// particle field
		//return;
	}
	else if (count == 255)
	{
		// always an explosion
		this->Explosion (org);

		// this is our exploding barrel effect and it needs a light to go with it so we copied it from TE_EXPLOSION
		CL_SetTEntDLight (org, 350, 0.5, 300, DL_COLOR_ORANGE);

		return;
	}
	else if (color == 73)
	{
		// blood splashes
		this->Blood (org, dir, color, count);
		return;
	}
	else if (color == 225)
	{
		// blood splashes
		this->Blood (org, dir, color, count);
		return;
	}

	// standard/undefined effect
	emitter_t *pe = this->NewEmitter (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = this->NewParticle (pe))) return;

		this->StandardParticle (p, org, dir, color);
	}
}


void QPARTICLESYSTEM::WallHitParticles (vec3_t org, vec3_t dir, int color, int count)
{
	particle_t *p;
	int i;

	emitter_t *pe = this->NewEmitter (org);

	for (i = 0; i < count; i++)
	{
		if (!(p = this->NewParticle (pe))) break;

		this->StandardParticle (p, org, dir, color);
	}
}


void QPARTICLESYSTEM::RainParticle (emitter_t *pe, vec3_t mins, vec3_t maxs, int color, vec3_t vel, float time, float z, int type)
{
	// type 1 is snow, 0 is rain
	particle_t *p = this->NewParticle (pe);

	if (!p) return;

	p->v.org[0] = Q_Random (mins[0], maxs[0]);
	p->v.org[1] = Q_Random (mins[1], maxs[1]);
	p->v.org[2] = z;

	p->v.vel[0] = vel[0];
	p->v.vel[1] = vel[1];
	p->v.vel[2] = vel[2];

	p->v.color = d3d_QuakePalette.standard11[color & 255];
	p->v.grav = -1;

	p->v.die = cl.time + time;
}


void QPARTICLESYSTEM::Rain (vec3_t mins, vec3_t maxs, vec3_t dir, int count, int colorbase, int type)
{
	emitter_t *pe = this->NewEmitter (mins);

	// increase the number of particles
	count *= 2;

	vec3_t		vel;
	float		t, z;

	if (maxs[0] <= mins[0]) {t = mins[0]; mins[0] = maxs[0]; maxs[0] = t;}
	if (maxs[1] <= mins[1]) {t = mins[1]; mins[1] = maxs[1]; maxs[1] = t;}
	if (maxs[2] <= mins[2]) {t = mins[2]; mins[2] = maxs[2]; maxs[2] = t;}

	if (dir[2] < 0) // falling
	{
		t = (maxs[2] - mins[2]) / -dir[2];
		z = maxs[2];
	}
	else // rising??
	{
		t = (maxs[2] - mins[2]) / dir[2];
		z = mins[2];
	}

	if (t < 0 || t > 2) // sanity check
		t = 2;

	// type 1 is snow, 0 is rain
	switch (type)
	{
	case 0:
		while (count--)
		{
			vel[0] = dir[0] + Q_Random (-16, 16);
			vel[1] = dir[1] + Q_Random (-16, 16);
			vel[2] = dir[2] + Q_Random (-32, 32);

			this->RainParticle (pe, mins, maxs, colorbase + (Q_fastrand () & 3), vel, t, z, type);
		}

		break;

	case 1:
		while (count--)
		{
			vel[0] = dir[0] + Q_Random (-16, 16);
			vel[1] = dir[1] + Q_Random (-16, 16);
			vel[2] = dir[2] + Q_Random (-32, 32);

			this->RainParticle (pe, mins, maxs, colorbase + (Q_fastrand () & 3), vel, t, z, type);
		}

		break;

	default:
		Con_DPrintf ("QPARTICLESYSTEM::Rain : unknown type %i (0 = rain, 1 = snow)\n", type);
	}
}


void QPARTICLESYSTEM::LavaSplash (vec3_t org)
{
	int			i, j, k;
	particle_t	*p;
	float		vel;
	vec3_t		dir;

	emitter_t *pe = this->NewEmitter (org);

	for (i = -16; i < 16; i++)
	{
		for (j = -16; j < 16; j++)
		{
			for (k = 0; k < 1; k++)
			{
				if (!(p = this->NewParticle (pe))) return;

				p->v.die = cl.time + (2 + (Q_fastrand () & 31) * 0.02);
				p->v.color = d3d_QuakePalette.standard11[224 + (Q_fastrand () & 7)];
				p->v.grav = -1;

				dir[0] = j * 8 + (Q_fastrand () & 7);
				dir[1] = i * 8 + (Q_fastrand () & 7);
				dir[2] = 256;

				p->v.org[0] = org[0] + dir[0];
				p->v.org[1] = org[1] + dir[1];
				p->v.org[2] = org[2] + (Q_fastrand () & 63);

				Vector3Normalize (dir);
				vel = 50 + (Q_fastrand () & 63);
				Vector3Scale (p->v.vel, dir, vel);
			}
		}
	}
}


void QPARTICLESYSTEM::TeleportSplash (vec3_t org)
{
	particle_t	*p;
	float		vel;

	emitter_t *pe = this->NewEmitter (org);
	float *norm = this->TeleSplashNormals[0][0][0];

	// change r_telesplash_normals too
	for (int i = -16; i < 16; i += 4)
	{
		for (int j = -16; j < 16; j += 4)
		{
			for (int k = -24; k < 32; k += 4, norm += 3)
			{
				if (!(p = this->NewParticle (pe))) return;

				p->v.die = cl.time + (0.2 + (Q_fastrand () & 7) * 0.02);
				p->v.color = d3d_QuakePalette.standard11[7 + (Q_fastrand () & 7)];
				p->v.grav = -1;

				p->v.org[0] = org[0] + i + (Q_fastrand () & 3);
				p->v.org[1] = org[1] + j + (Q_fastrand () & 3);
				p->v.org[2] = org[2] + k + (Q_fastrand () & 3);

				vel = 50 + (Q_fastrand () & 63);
				Vector3Scale (p->v.vel, norm, vel);
			}
		}
	}
}


void QPARTICLESYSTEM::RocketTrail (vec3_t start, vec3_t end, int trailtype)
{
	vec3_t vec;
	int dec;
	static int tracercount;

	if (trailtype < 128)
		dec = 3;
	else
	{
		dec = 1;
		trailtype -= 128;
	}

	if (trailtype == RT_ZOMGIB) dec += 3;

	Vector3Subtract (vec, end, start);
	float len = Vector3Length (vec);

	float porg[3];
	float plerp;
	int nump = (len / dec) + 0.5f;

	if (nump < 1) return;

	// a particle trail adds too few new particles to the emiiter, even over a 1/36 second interval
	// (maxes out at approx. 10) so we can't make this a single draw call....
	emitter_t *pe = this->NewEmitter (start);
	particle_t *p = NULL;

	for (int i = 0; i < nump; i++)
	{
		if (!(p = this->NewParticle (pe))) return;

		// at 36 fps particles have a tendency to clump in discrete packets so interpolate them along the trail length instead
		if (nump == 1)
		{
			porg[0] = start[0];
			porg[1] = start[1];
			porg[2] = start[2];
		}
		else if (nump == 2)
		{
			porg[0] = i ? end[0] : start[0];
			porg[1] = i ? end[1] : start[1];
			porg[2] = i ? end[2] : start[2];
		}
		else
		{
			plerp = (float) i / (float) (nump - 1);

			porg[0] = start[0] + plerp * (end[0] - start[0]);
			porg[1] = start[1] + plerp * (end[1] - start[1]);
			porg[2] = start[2] + plerp * (end[2] - start[2]);
		}

		Vector3Copy (p->v.vel, vec3_origin);
		p->v.die = cl.time + 2;

		switch (trailtype)
		{
		case RT_ROCKET:
		case RT_GRENADE:
			// rocket/grenade trail
			p->v.grav = 1;

			// grenade trail decays faster
			if (trailtype == RT_GRENADE)
				this->SetRamp (&p->v, 3, 2 + (Q_fastrand () & 3), 5);
			else this->SetRamp (&p->v, 3, Q_fastrand () & 3, 5);

			p->v.org[0] = porg[0] + ((Q_fastrand () % 6) - 3);
			p->v.org[1] = porg[1] + ((Q_fastrand () % 6) - 3);
			p->v.org[2] = porg[2] + ((Q_fastrand () % 6) - 3);

			break;

		case RT_GIB:
		case RT_ZOMGIB:
			// blood/slight blood
			p->v.grav = -1;
			p->v.color = d3d_QuakePalette.standard11[67 + (Q_fastrand () & 3)];

			p->v.org[0] = porg[0] + ((Q_fastrand () % 6) - 3);
			p->v.org[1] = porg[1] + ((Q_fastrand () % 6) - 3);
			p->v.org[2] = porg[2] + ((Q_fastrand () % 6) - 3);

			break;

		case RT_WIZARD:
		case RT_KNIGHT:
			// tracer - wizard/hellknight
			p->v.die = cl.time + 0.5f;

			tracercount++;

			Vector3Copy (p->v.org, porg);

			// split trail left/right
			if (tracercount & 1)
			{
				p->v.vel[0] = 29.3825109f;
				p->v.vel[1] = -1.78082475f;
			}
			else
			{
				p->v.vel[0] = -29.3825109f;
				p->v.vel[1] = 1.78082475f;
			}

			p->v.color = d3d_QuakePalette.standard11[((trailtype == RT_WIZARD) ? 52 : 230) + ((tracercount & 4) << 1)];

			break;

		case RT_VORE:
			p->v.color = d3d_QuakePalette.standard11[9 * 16 + 8 + (Q_fastrand () & 3)];
			p->v.die = cl.time + 0.3f;

			p->v.org[0] = porg[0] + ((Q_fastrand () & 15) - 8);
			p->v.org[1] = porg[1] + ((Q_fastrand () & 15) - 8);
			p->v.org[2] = porg[2] + ((Q_fastrand () & 15) - 8);

			break;
		}
	}
}


void QPARTICLESYSTEM::BenchMark (int num)
{
	for (int n = 0; n < num; n++)
	{
		particle_t *p = NULL;
		emitter_t *pe = NULL;

		float org[3] =
		{
			r_refdef.vieworigin[0] + ((Q_fastrand () & 1023) - 512),
			r_refdef.vieworigin[1] + ((Q_fastrand () & 1023) - 512),
			r_refdef.vieworigin[2] + ((Q_fastrand () & 1023) - 512)
		};

		// create a new emitter every 1024 particles so as to not overflow the vertex buffer
		// if this changes be sure to check out MAX_DRAW_PARTICLES in d3d_part.cpp!!!!
		if ((pe = this->NewEmitter (org)) == NULL) return;

		for (int i = 0; i < 1024; i++)
		{
			if (!(p = this->NewParticle (pe))) return;

			p->v.org[0] = pe->spawnorg[0] + ((Q_fastrand () & 1023) - 512);
			p->v.org[1] = pe->spawnorg[1] + ((Q_fastrand () & 1023) - 512);
			p->v.org[2] = pe->spawnorg[2] + ((Q_fastrand () & 1023) - 512);

			p->v.grav = -0.25f;

			if (i & 1)
			{
				this->SetRamp (&p->v, 1, Q_fastrand () & 1, 2);
				p->v.dvel[0] = p->v.dvel[1] = 4;
			}
			else
			{
				this->SetRamp (&p->v, 3, Q_fastrand () & 1, 1);
				p->v.dvel[0] = p->v.dvel[1] = -1;
			}

			p->v.vel[0] = (Q_fastrand () & 127) - 64;
			p->v.vel[1] = (Q_fastrand () & 127) - 64;
			p->v.vel[2] = (Q_fastrand () & 127) - 64;
		}
	}
}


void QPARTICLESYSTEM::AddToAlphaList (void)
{
	if (this->ActiveEmitters)
	{
		// removes expired particles from the active particles list
		emitter_t *pe;
		emitter_t *pekill;

		// remove from the head of the list
		for (;;)
		{
			if ((pekill = this->ActiveEmitters) != NULL && !pekill->particles)
			{
				// return to the free list
				this->ActiveEmitters = pekill->next;
				pekill->next = this->FreeEmitters;
				this->FreeEmitters = pekill;
				continue;
			}

			break;
		}

		for (pe = this->ActiveEmitters; pe; pe = pe->next)
		{
			// remove from a mid-point in the list
			for (;;)
			{
				if ((pekill = pe->next) != NULL && !pekill->particles)
				{
					pe->next = pekill->next;
					pekill->next = this->FreeEmitters;
					this->FreeEmitters = pekill;
					continue;
				}

				break;
			}

			// prepare this emitter for rendering
			if (!pe->particles) continue;

			// removes expired particles from the active particles list
			//particle_t *p;
			particle_t *pkill;
			float etime = cl.time - pe->spawntime;

			// this is the count of particles that will be drawn this frame
			pe->numparticles = 0;

			// remove from the head of the list
			for (;;)
			{
				if ((pkill = pe->particles) != NULL && (pkill->v.die < cl.time))
				{
					pe->particles = pkill->next;
					pkill->next = this->FreeParticles;
					this->FreeParticles = pkill;

					continue;
				}

				break;
			}

			for (particle_t *p = pe->particles; p; p = p->next)
			{
				// remove from a mid-point in the list
				for (;;)
				{
					if ((pkill = p->next) != NULL && (pkill->v.die < cl.time))
					{
						p->next = pkill->next;
						pkill->next = this->FreeParticles;
						this->FreeParticles = pkill;

						continue;
					}

					break;
				}

				// update it's time since the emitter was spawned
				p->v.etime = etime;

				// count the active particles for this type
				pe->numparticles++;

				// killflags will take effect on the next frame
				if (p->killflag == P_KILLFRAME) p->v.die = -1;
				if (p->killflag == P_NEVERKILL) p->v.die = cl.time + 666;
			}

			if (!pe->particles) continue;
			if (!pe->numparticles) continue;

			// these are deferred to here so that particles get removed from the lists even if we're not drawing any
			if (!r_drawparticles.integer) continue;
			if (!(r_particlesize.value > 0)) continue;

			// add to the draw list (only if there's something to draw)
			D3DAlpha_AddToList (pe);
		}
	}
}


void R_ReadPointFile_f (void)
{
	ParticleSystem.PointFile ();
}


void R_BenchMarkParticles (void)
{
	int num = 1;

	// let them specify an amount but don't get *too* silly.
	if (Cmd_Argc () > 1) num = atoi (Cmd_Argv (1));
	if (num < 1) num = 1;
	if (num > 1024) num = 1024;

	ParticleSystem.BenchMark (num);
}


cmd_t R_ReadPointFile_f_Cmd ("pointfile", R_ReadPointFile_f);
cmd_t r_benchmarkparticles ("r_benchmarkparticles", R_BenchMarkParticles);

