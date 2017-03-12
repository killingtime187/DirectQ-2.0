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

#define P_NEVERKILL		1
#define P_KILLFRAME		2

struct partvert_t
{
	// driver-usable fields
	float org[3];
	float vel[3];
	float dvel[2];	// swizzled as .xxy
	float etime;
	float grav;
	float scale;
	unsigned rampnum;
	float ramptime;
	float baseramp;

	union
	{
		unsigned color;
		byte rgba[4];
	};

	float die;
};


struct particle_t
{
	// having this struct inherit from partvert_t and then copying it over in the renderer was a huge code-smell
	// in PVS studio so I did it this way instead
	partvert_t v;

	// drivers never touch the following fields
	int killflag;
	particle_t *next;
};


// this is needed outside of r_part now...
struct emitter_t
{
	particle_t *particles;
	int numparticles;
	vec3_t spawnorg;
	double spawntime;
	emitter_t *next;
};


#define NUMVERTEXNORMALS		162
#define MAX_EXPLOSION_ORIGINS	4096


class QPARTICLESYSTEM
{
public:
	QPARTICLESYSTEM (void);

	void InitParticles (void);
	void ClearParticles (void);
	void AddToAlphaList (void);

	void PointFile (void);
	void BenchMark (int num);
	void ParseEffect (void);
	void RunEffect (vec3_t org, vec3_t dir, int color, int count);
	void Explosion (vec3_t org);
	void Explosion (vec3_t org, int colorStart, int colorLength);
	void BlobExplosion (vec3_t org);
	void WallHitParticles (vec3_t org, vec3_t dir, int color, int count);
	void Rain (vec3_t mins, vec3_t maxs, vec3_t dir, int count, int colorbase, int type);
	void LavaSplash (vec3_t org);
	void TeleportSplash (vec3_t org);
	void RocketTrail (vec3_t start, vec3_t end, int trailtype);
	void EntityParticles (entity_t *ent);

private:
	emitter_t *NewEmitter (vec3_t spawnorg);
	particle_t *NewParticle (emitter_t *pe);

	void SetExplosionVelocity (float *vel);
	void SetExplosionOrigin (float *orgout, float *orgin, int num);
	void RainParticle (emitter_t *pe, vec3_t mins, vec3_t maxs, int color, vec3_t vel, float time, float z, int type);

	void StandardParticle (particle_t *p, float *org, float *dir, int color);
	void Blood (vec3_t org, vec3_t dir, int color, int count);
	void SetRamp (partvert_t *v, int rampnum, float baseramp, float ramptime);

	particle_t *FreeParticles;
	emitter_t *ActiveEmitters;
	emitter_t *FreeEmitters;

	particle_t DefaultParticle;

	// if the counts in R_TeleportSplash are ever changed this will need to be changed too...!
	float TeleSplashNormals[8][8][14][3];

	// so that rand calls won't give different results each frame for these
	float AVelocities[NUMVERTEXNORMALS][2];
	float EntPartScales[NUMVERTEXNORMALS];

	float ExplosionOrigins[MAX_EXPLOSION_ORIGINS][3];
};


extern QPARTICLESYSTEM ParticleSystem;

