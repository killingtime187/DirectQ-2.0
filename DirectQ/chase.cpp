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
// chase.c -- chase camera code

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

bool SV_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace);

cvar_t	chase_back ("chase_back", "100", CVAR_ARCHIVE);
cvar_t	chase_up ("chase_up", "16", CVAR_ARCHIVE);
cvar_t	chase_right ("chase_right", "0", CVAR_ARCHIVE);
cvar_t	chase_active ("chase_active", "0", CVAR_ARCHIVE);
cvar_t	chase_scale ("chase_scale", "1", CVAR_ARCHIVE);
cvar_t	chase_pitch ("chase_pitch", "0.3", CVAR_ARCHIVE);


void Chase_Init (void)
{
}


void Chase_Reset (void)
{
	// for respawning and teleporting
	//	start position 12 units behind head
}

void TraceLine (vec3_t start, vec3_t end, vec3_t impact)
{
	trace_t	trace;

	memset (&trace, 0, sizeof (trace));
	SV_RecursiveHullCheck (cl.worldmodel->brushhdr->hulls, 0, 0, 1, start, end, &trace);

	Vector3Copy (impact, trace.endpos);
}


#define CHASE_DEST_OFFSET 4.0f
bool chase_nodraw;
int chase_alpha;

bool Chase_CheckBrushEdict (entity_t *e, float *checkpoint, int viewcontents)
{
	// don't check against self
	if (e == cls.entities[cl.viewentity]) return true;

	// let's not clip against these types
	if (e->model->type != mod_brush) return true;

	// don't check instanced models
	if (e->model->name[0] != '*') return true;

	// check against bbox
	if (checkpoint[0] < e->cullinfo.mins[0]) return true;
	if (checkpoint[1] < e->cullinfo.mins[1]) return true;
	if (checkpoint[2] < e->cullinfo.mins[2]) return true;
	if (checkpoint[0] > e->cullinfo.maxs[0]) return true;
	if (checkpoint[1] > e->cullinfo.maxs[1]) return true;
	if (checkpoint[2] > e->cullinfo.maxs[2]) return true;

	// blocked
	return false;
}


void Chase_CheckBrushEdictList (QEDICTLIST *list, float *checkpoint, int viewcontents)
{
	for (int i = 0; i < list->NumEdicts; i++)
	{
		if (!list->Edicts[i]->model) continue;
		if (!Chase_CheckBrushEdict (list->Edicts[i], checkpoint, viewcontents)) return;
	}
}


bool Chase_Check (float *checkpoint, int viewcontents)
{
	// check against world model - going into different contents
	if ((Mod_PointInLeaf (checkpoint, cl.worldmodel))->contents != viewcontents) return false;

	// check visedicts - this happens *after* CL_UpdateClient so the list will be valid (but will not include static entities)
	// (which is OK as we can't clip against them on the server anyway)
	Chase_CheckBrushEdictList (&d3d_MergeEdicts, checkpoint, viewcontents);
	Chase_CheckBrushEdictList (&d3d_BrushEdicts, checkpoint, viewcontents);

	// it's good now
	return true;
}


void Chase_Adjust (vec3_t chase_dest)
{
	// calculate distance between chasecam and original org to establish number of tests we need.
	// an int is good enough here.:)  add a cvar multiplier to this...
	int num_tests = sqrt ((r_refdef.vieworigin[0] - chase_dest[0]) * (r_refdef.vieworigin[0] - chase_dest[0]) +
		(r_refdef.vieworigin[1] - chase_dest[1]) * (r_refdef.vieworigin[1] - chase_dest[1]) +
		(r_refdef.vieworigin[2] - chase_dest[2]) * (r_refdef.vieworigin[2] - chase_dest[2])) * chase_scale.value;

	// take the contents of the view leaf
	int viewcontents = (Mod_PointInLeaf (r_refdef.vieworigin, cl.worldmodel))->contents;
	int best;

	// move along path from r_refdef.vieworigin to chase_dest
	for (best = 0; best < num_tests; best++)
	{
		vec3_t chase_newdest;

		chase_newdest[0] = r_refdef.vieworigin[0] + (chase_dest[0] - r_refdef.vieworigin[0]) * best / num_tests;
		chase_newdest[1] = r_refdef.vieworigin[1] + (chase_dest[1] - r_refdef.vieworigin[1]) * best / num_tests;
		chase_newdest[2] = r_refdef.vieworigin[2] + (chase_dest[2] - r_refdef.vieworigin[2]) * best / num_tests;

		// check for a leaf hit with different contents
		if (!Chase_Check (chase_newdest, viewcontents))
		{
			// go back to the previous best as this one is bad
			if (best > 1)
				best--;
			else best = num_tests;

			break;
		}
	}

	// certain surfaces can be viewed at an oblique enough angle that they are partially clipped
	// by znear, so now we fix that too...
	int chase_vert[] = {0, 0, 1, 1, 2, 2};
	int dest_offset[] = {CHASE_DEST_OFFSET, -CHASE_DEST_OFFSET};

	// move along path from chase_dest to r_refdef.vieworigin
	// this one will early-out the vast majority of cases
	for (; best >= 0; best--)
	{
		// number of matches
		int nummatches = 0;

		// adjust
		chase_dest[0] = r_refdef.vieworigin[0] + (chase_dest[0] - r_refdef.vieworigin[0]) * best / num_tests;
		chase_dest[1] = r_refdef.vieworigin[1] + (chase_dest[1] - r_refdef.vieworigin[1]) * best / num_tests;
		chase_dest[2] = r_refdef.vieworigin[2] + (chase_dest[2] - r_refdef.vieworigin[2]) * best / num_tests;

		// run 6 tests: -x/+x/-y/+y/-z/+z
		for (int test = 0; test < 6; test++)
		{
			// adjust, test and put back.
			chase_dest[chase_vert[test]] -= dest_offset[test & 1];

			if (Chase_Check (chase_dest, viewcontents)) nummatches++;

			chase_dest[chase_vert[test]] += dest_offset[test & 1];
		}

		// test result, if all match we're done in here
		if (nummatches == 6) break;
	}

	float chase_length = (r_refdef.vieworigin[0] - chase_dest[0]) * (r_refdef.vieworigin[0] - chase_dest[0]);
	chase_length += (r_refdef.vieworigin[1] - chase_dest[1]) * (r_refdef.vieworigin[1] - chase_dest[1]);
	chase_length += (r_refdef.vieworigin[2] - chase_dest[2]) * (r_refdef.vieworigin[2] - chase_dest[2]);

	if (chase_length < 150)
	{
		chase_nodraw = true;
		chase_alpha = 255;
	}
	else
	{
		chase_nodraw = false;
		chase_alpha = (chase_length - 150);

		if (chase_alpha > 255) chase_alpha = 255;
	}
}


void Chase_Update (void)
{
	float	dist;
	QMATRIX	av;
	vec3_t	dest, stop;
	vec3_t	chase_dest;

	// if can't see player, reset
	av.AngleVectors (cl.viewangles);

	// calc exact destination
	chase_dest[0] = r_refdef.vieworigin[0] - av.fw[0] * chase_back.value - av.rt[0] * chase_right.value;
	chase_dest[1] = r_refdef.vieworigin[1] - av.fw[1] * chase_back.value - av.rt[1] * chase_right.value;
	chase_dest[2] = r_refdef.vieworigin[2] + chase_up.value;

	// don't allow really small or negative scaling values
	if (chase_scale.value < 0.01)
	{
		// revert to default Q1 chasecam behaviour
		// store out alpha and nodraw so that they're valid
		chase_alpha = 255;
		chase_nodraw = false;
	}
	else
	{
		// adjust the chasecam to prevent it going into solid
		Chase_Adjust (chase_dest);
	}

	// store alpha to entity
	cls.entities[cl.viewentity]->alphaval = chase_alpha;

	// find the spot the player is looking at
	Vector3Mad (dest, av.fw, 4096, r_refdef.vieworigin);
	TraceLine (r_refdef.vieworigin, dest, stop);

	// calculate pitch to look at the same spot from camera
	Vector3Subtract (stop, stop, r_refdef.vieworigin);
	dist = Vector3Dot (stop, av.fw);

	if (dist < 1) dist = 1;

	r_refdef.viewangles[0] = -atan (stop[2] / dist) / D3DX_PI * 180;

	// move towards destination
	Vector3Copy (r_refdef.vieworigin, chase_dest);
}


