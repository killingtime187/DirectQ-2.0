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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "iqm.h"

float Mod_PlaneDist (mplane_t *plane, float *point)
{
	if (plane->type < 3)
		return point[plane->type] - plane->dist;
	else return Vector3Dot (point, plane->normal) - plane->dist;
}


int Mod_AnimateGroup (entity_t *ent, float *intervals, int numintervals)
{
	if (!ent) return 0;
	if (!intervals) return 0;
	if (!numintervals) return 0;

	float time = cl.time + ent->syncbase;
	float fullinterval = intervals[numintervals - 1];
	float targettime = time - ((int) (time / fullinterval)) * fullinterval;

	for (int i = 0; i < numintervals - 1; i++)
		if (intervals[i] > targettime)
			return i;

	// default is the last animation interval (consistency with software Quake)
	return numintervals - 1;
}


void Mod_ClearBoundingBox (float *mins, float *maxs)
{
	mins[0] = mins[1] = mins[2] = 999999999.0f;
	maxs[0] = maxs[1] = maxs[2] = -999999999.0f;
}


void Mod_ClearBoundingBox (cullinfo_t *cull)
{
	cull->mins[0] = cull->mins[1] = cull->mins[2] = 999999999.0f;
	cull->maxs[0] = cull->maxs[1] = cull->maxs[2] = -999999999.0f;
}


void Mod_AccumulateBox (cullinfo_t *bbcull, cullinfo_t *pcull)
{
	for (int i = 0; i < 3; i++)
	{
		if (pcull->mins[i] < bbcull->mins[i]) bbcull->mins[i] = pcull->mins[i];
		if (pcull->maxs[i] > bbcull->maxs[i]) bbcull->maxs[i] = pcull->maxs[i];
	}
}


void Mod_AccumulateBox (float *bbmins, float *bbmaxs, float *point)
{
	for (int i = 0; i < 3; i++)
	{
		if (point[i] < bbmins[i]) bbmins[i] = point[i];
		if (point[i] > bbmaxs[i]) bbmaxs[i] = point[i];
	}
}


void Mod_AccumulateBox (float *bbmins, float *bbmaxs, float *pmins, float *pmaxs)
{
	for (int i = 0; i < 3; i++)
	{
		if (pmins[i] < bbmins[i]) bbmins[i] = pmins[i];
		if (pmaxs[i] > bbmaxs[i]) bbmaxs[i] = pmaxs[i];
	}
}


void Mod_LoadIQMModel (model_t *mod, void *buffer, char *path);
bool Mod_FindIQMModel (model_t *mod);
void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, bool crash);

// imports from sv_main
extern byte *fatpvs;
extern int fatbytes;
extern cvar_t sv_pvsfat;

// imported from pr_cmds
extern byte *checkpvs;

// local
byte	*mod_novis;

model_t	**mod_known = NULL;
int		mod_numknown = 0;


/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	vec3_t extent;
	float radius = 0;

	extent[0] = fabs (maxs[0] - mins[0]) * 0.5f;
	extent[1] = fabs (maxs[1] - mins[1]) * 0.5f;
	extent[2] = fabs (maxs[2] - mins[2]) * 0.5f;

	if (extent[0] > radius) radius = extent[0];
	if (extent[1] > radius) radius = extent[1];
	if (extent[2] > radius) radius = extent[2];

	return radius;
}


void Mod_SphereFromBounds (float *mins, float *maxs, float *sphere)
{
	D3DXVECTOR3 points[2];
	D3DXVECTOR3 center;
	FLOAT radius;

	for (int i = 0; i < 3; i++)
	{
		points[0][i] = mins[i];
		points[1][i] = maxs[i];
	}

	D3DXComputeBoundingSphere (points, 2, sizeof (D3DXVECTOR3), &center, &radius);

	sphere[0] = center.x;
	sphere[1] = center.y;
	sphere[2] = center.z;
	sphere[3] = radius;
}


/*
===============
Mod_Init

===============
*/
void Mod_Init (void)
{
	mod_known = (model_t **) MainZone->Alloc (MAX_MOD_KNOWN * sizeof (model_t *));
}


/*
===============
Mod_InitForMap

===============
*/
void Mod_InitForMap (model_t *mod)
{
	// only alloc as much as we actually need
	mod_novis = (byte *) MainHunk->Alloc ((mod->brushhdr->numleafs + 7) / 8);
	memset (mod_novis, 0xff, (mod->brushhdr->numleafs + 7) / 8);
	checkpvs = (byte *) MainHunk->Alloc ((mod->brushhdr->numleafs + 7) / 8);

	fatbytes = (mod->brushhdr->numleafs + 31) >> 3;
	fatpvs = (byte *) MainHunk->Alloc (fatbytes);
}


/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	if (!model)
	{
		Host_Error ("Mod_PointInLeaf: NULL model");
		return NULL;
	}
	else if (!model->brushhdr->nodes)
	{
		Host_Error ("Mod_PointInLeaf: bad model");
		return NULL;
	}

	mnode_t *node = model->brushhdr->nodes;

	for (;;)
	{
		if (node->contents < 0)
			return (mleaf_t *) node;

		if (Mod_PlaneDist (node->plane, p) > 0)
			node = node->children[0];
		else node = node->children[1];
	}

	return NULL;	// never reached
}


int Mod_CompressVis (byte *vis, byte *dest, model_t *model)
{
	int		j;
	int		rep;
	byte	*dest_p;
	int row = (model->brushhdr->numleafs + 7) >> 3;

	dest_p = dest;

	for (j = 0; j < row; j++)
	{
		*dest_p++ = vis[j];

		if (vis[j])
			continue;

		rep = 1;

		for (j++; j < row; j++)
		{
			if (vis[j] || rep == 255)
				break;
			else rep++;
		}

		*dest_p++ = rep;
		j--;
	}

	return dest_p - dest;
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	int		c;

	int row = (model->brushhdr->numleafs + 7) >> 3;
	byte *out = scratchbuf;

	if (!in)
	{
		// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}

		return scratchbuf;
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;

		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - scratchbuf < row);

	return scratchbuf;
}


byte *Mod_LeafPVS (mleaf_t *leaf, model_t *model)
{
	if (leaf == model->brushhdr->leafs)
		return mod_novis;

	return Mod_DecompressVis (leaf->compressed_vis, model);
}


// fixme - is this now the same as the server-side version?
void Mod_AddToFatPVS (vec3_t org, mnode_t *node)
{
	// optimized recursion without a goto! whee!
	for (;;)
	{
		// if this is a leaf, accumulate the pvs bits
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				byte *pvs = Mod_LeafPVS ((mleaf_t *) node, cl.worldmodel);

				for (int i = 0; i < fatbytes; i++) fatpvs[i] |= pvs[i];
			}

			return;
		}

		float d = Mod_PlaneDist (node->plane, org);

		// add extra fatness here as the server-side fatness is not sufficient
		if (d > 120 + sv_pvsfat.value)
			node = node->children[0];
		else if (d < - (120 + sv_pvsfat.value))
			node = node->children[1];
		else
		{
			// go down both
			Mod_AddToFatPVS (org, node->children[0]);
			node = node->children[1];
		}
	}
}


byte *Mod_FatPVS (vec3_t org)
{
	// it's expected that cl.worldmodel will be the same as sv.worldmodel. ;)
	fatbytes = (cl.worldmodel->brushhdr->numleafs + 31) >> 3;
	memset (fatpvs, 0, fatbytes);

	Mod_AddToFatPVS (org, cl.worldmodel->brushhdr->nodes);

	return fatpvs;
}


/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	// NULL the structs
	for (int i = 0; i < MAX_MOD_KNOWN; i++)
		mod_known[i] = NULL;

	// note - this was a nasty memory leak which I'm sure was inherited from the original code.
	// the models array never went down, so if over MAX_MOD_KNOWN unique models get loaded it's crash time.
	// very unlikely to happen, but it was there all the same...
	mod_numknown = 0;
}

/*
==================
Mod_FindName

==================
*/
model_t *Mod_FindName (char *name)
{
	int i;

	if (!name[0])
	{
		Host_Error ("Mod_ForName: NULL name");
		return NULL;
	}

	// search the currently loaded models
	for (i = 0; i < mod_numknown; i++)
		if (!strcmp (mod_known[i]->name, name))
			break;

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Host_Error ("mod_numknown == MAX_MOD_KNOWN");

		// allocate a model
		mod_known[i] = (model_t *) MainHunk->Alloc (sizeof (model_t));

		Q_strncpy (mod_known[i]->name, name, 63);
		mod_known[i]->needload = true;
		mod_numknown++;
	}

	// return the model we got
	return mod_known[i];
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (char *name) {Mod_FindName (name);}


/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t *Mod_LoadModel (model_t *mod, bool crash)
{
	// already loaded
	if (!mod->needload)
	{
		mod->RegistrationSequence = d3d_RenderDef.RegistrationSequence;
		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);
		Mod_SphereFromBounds (mod->mins, mod->maxs, mod->sphere);
		return mod;
	}

	// we're going to be loading and using a lot of temp data with each model, so take a temp hunk pointer for it
	int hunkmark = TempHunk->GetLowMark ();
	unsigned *buf =  NULL;

	// load the file
	if (!(buf = (unsigned *) CQuakeFile::LoadFile (mod->name, TempHunk)))
	{
		// return NULL is NOT an else
		if (crash) Host_Error ("Mod_LoadModel: %s not found", mod->name);
		return NULL;
	}

	// call the apropriate loader
	mod->needload = false;

	// set all header pointers initially NULL
	mod->aliashdr = NULL;
	mod->brushhdr = NULL;
	mod->spritehdr = NULL;
	mod->iqmheader = NULL;

	switch (((unsigned *) buf)[0])
	{
	case IQM_FOURCC:
		Mod_LoadIQMModel (mod, buf, mod->name);
		break;

	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf);
		break;

	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;

	case Q1_BSPVERSION:
	case PR_BSPVERSION:
	case BSPVERSIONRMQ:
		// bsp files don't have a header ident... sigh...
		// the version seems good for use though
		Mod_LoadBrushModel (mod, buf);
		break;

	default:
		// we can't host_error here as this will dirty the model cache
		Sys_Error (
			"Unknown model type for %s ('%c' '%c' '%c' '%c')\n",
			mod->name,
			((char *) buf)[0],
			((char *) buf)[1],
			((char *) buf)[2],
			((char *) buf)[3]
		);

		break;
	}

	// eval the radius to get a bounding sphere
	mod->radius = RadiusFromBounds (mod->mins, mod->maxs);
	Mod_SphereFromBounds (mod->mins, mod->maxs, mod->sphere);

	// and throw away the temp data
	TempHunk->FreeToLowMark (hunkmark);

	mod->RegistrationSequence = d3d_RenderDef.RegistrationSequence;
	return mod;
}


/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, bool crash) {return Mod_LoadModel (Mod_FindName (name), crash);}


