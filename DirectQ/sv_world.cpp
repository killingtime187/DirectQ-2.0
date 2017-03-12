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
// world.c -- world query functions

#include "quakedef.h"
#include "d3d_model.h"
#include "pr_class.h"

/*

entities never clip against themselves, or their owner

line of sight checks trace->crosscontent, but bullets don't

*/


void SV_RotatePoint (QMATRIX *av, float *point)
{
	vec3_t temp;

	Vector3Copy (temp, point);

	point[0] = Vector3Dot (temp, av->fw);
	point[1] = -Vector3Dot (temp, av->rt);
	point[2] = Vector3Dot (temp, av->up);
}


// ClearLink is used for new headnodes
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}
void InsertLinkAfter (link_t *l, link_t *after)
{
	l->next = after->next;
	l->prev = after;
	l->prev->next = l;
	l->next->prev = l;
}


struct moveclip_t
{
	vec3_t		boxmins, boxmaxs;// enclose the test object along entire move
	float		*mins, *maxs;	// size of the moving object
	vec3_t		mins2, maxs2;	// size when clipping against mosnters
	float		*start, *end;
	trace_t		trace;
	int			type;
	edict_t		*passedict;
};


int SV_HullPointContents (hull_t *hull, int num, vec3_t p);

/*
===============================================================================

HULL BOXES

===============================================================================
*/


static	hull_t		box_hull;
static	mclipnode_t	box_clipnodes[6];
static	mplane_t	box_planes[6];

/*
===================
SV_InitBoxHull

Set up the planes and clipnodes so that the six floats of a bounding box
can just be stored out and get a proper hull_t structure.
===================
*/
void SV_InitBoxHull (void)
{
	box_hull.clipnodes = box_clipnodes;
	box_hull.planes = box_planes;
	box_hull.firstclipnode = 0;
	box_hull.lastclipnode = 5;

	for (int i = 0; i < 6; i++)
	{
		box_clipnodes[i].planenum = i;

		int side = i & 1;

		box_clipnodes[i].children[side] = CONTENTS_EMPTY;

		if (i != 5)
			box_clipnodes[i].children[side ^ 1] = i + 1;
		else box_clipnodes[i].children[side ^ 1] = CONTENTS_SOLID;

		box_planes[i].type = i >> 1;
		box_planes[i].normal[i >> 1] = 1;
	}
}


/*
===================
SV_HullForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
hull_t *SV_HullForBox (vec3_t mins, vec3_t maxs)
{
	box_planes[0].dist = maxs[0];
	box_planes[1].dist = mins[0];
	box_planes[2].dist = maxs[1];
	box_planes[3].dist = mins[1];
	box_planes[4].dist = maxs[2];
	box_planes[5].dist = mins[2];

	return &box_hull;
}



/*
================
SV_HullForEntity

Returns a hull that can be used for testing or clipping an object of mins/maxs
size.
Offset is filled in to contain the adjustment that must be added to the
testing object's origin to get a point to use with the returned hull.
================
*/
hull_t *SV_HullForEntity (edict_t *ent, vec3_t mins, vec3_t maxs, vec3_t offset)
{
	model_t		*model;
	vec3_t		size;
	vec3_t		hullmins, hullmaxs;
	hull_t		*hull;

	// decide which clipping hull to use, based on the size
	if (ent->v.solid == SOLID_BSP)
	{
		// explicit hulls in the BSP model
		if (ent->v.movetype != MOVETYPE_PUSH)
			Sys_Error ("SOLID_BSP without MOVETYPE_PUSH");

		model = sv.models[(int) ent->v.modelindex];

		if (!model || model->type != mod_brush)
		{
			Sys_Error ("MOVETYPE_PUSH with a non bsp model");
			return NULL;
		}

		Vector3Subtract (size, maxs, mins);

		if (size[0] < 3)
			hull = &model->brushhdr->hulls[0];
		else if (size[0] <= 32)
			hull = &model->brushhdr->hulls[1];
		else hull = &model->brushhdr->hulls[2];

		// calculate an offset value to center the origin
		Vector3Subtract (offset, hull->clip_mins, mins);
		Vector3Add (offset, offset, ent->v.origin);
	}
	else
	{
		// create a temp hull from bounding box sizes
		Vector3Subtract (hullmins, ent->v.mins, maxs);
		Vector3Subtract (hullmaxs, ent->v.maxs, mins);
		hull = SV_HullForBox (hullmins, hullmaxs);

		Vector3Copy (offset, ent->v.origin);
	}

	return hull;
}

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/

struct areanode_t
{
	int		axis;		// -1 = leaf node
	float	dist;
	struct areanode_t	*children[2];
	link_t	trigger_edicts;
	link_t	solid_edicts;
};


static	areanode_t	*sv_areanodes = NULL;

/*
===============
SV_CreateAreaNode

===============
*/
areanode_t *SV_CreateAreaNode (int depth, vec3_t mins, vec3_t maxs)
{
	areanode_t	*anode;
	vec3_t		size;
	vec3_t		mins1, maxs1, mins2, maxs2;

	anode = (areanode_t *) MainHunk->Alloc (sizeof (areanode_t));

	ClearLink (&anode->trigger_edicts);
	ClearLink (&anode->solid_edicts);

	Vector3Subtract (size, maxs, mins);

	if ((size[0] < 500 && size[1] < 500) ||
		(size[0] < 500 && size[2] < 500) ||
		(size[1] < 500 && size[2] < 500))
	{
		anode->axis = -1;
		anode->children[0] = anode->children[1] = NULL;
		return anode;
	}

	if (size[0] > size[1])
		anode->axis = 0;
	else anode->axis = 1;

	anode->dist = 0.5 * (maxs[anode->axis] + mins[anode->axis]);
	Vector3Copy (mins1, mins);
	Vector3Copy (mins2, mins);
	Vector3Copy (maxs1, maxs);
	Vector3Copy (maxs2, maxs);

	maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

	anode->children[0] = SV_CreateAreaNode (depth + 1, mins2, maxs2);
	anode->children[1] = SV_CreateAreaNode (depth + 1, mins1, maxs1);

	return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld (void)
{
	SV_InitBoxHull ();
	sv_areanodes = SV_CreateAreaNode (0, sv.worldmodel->mins, sv.worldmodel->maxs);
}


/*
===============
SV_UnlinkEdict

===============
*/
void SV_UnlinkEdict (edict_t *ent)
{
	if (!ent->area.prev)
		return;		// not linked in anywhere

	RemoveLink (&ent->area);
	ent->area.prev = ent->area.next = NULL;
}


/*
====================
SV_TouchLinks

====================
*/
void SV_TouchLinks (edict_t *ent, areanode_t *node)
{
	int hunkmark = TempHunk->GetLowMark ();
	edict_t **touchlist = (edict_t **) TempHunk->FastAlloc (MAX_EDICTS * sizeof (edict_t *));
	int numtouched = 0;

	// Build a list of touched edicts since linked list may change during touch
	for (link_t *l = node->trigger_edicts.next; l != &node->trigger_edicts; l = l->next)
	{
		if (!l)
		{
			// this shouldn't happen as the list no longer changes but it seems a good sanity test all the same
			Con_DPrintf ("SV_TouchLinks : null link\n");
			break;
		}

		edict_t	*touch = EDICT_FROM_AREA (l);

		if (touch == ent) continue;
		if (touch->free) continue;
		if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER) continue;

		if (ent->v.absmin[0] > touch->v.absmax[0] || ent->v.absmin[1] > touch->v.absmax[1] || ent->v.absmin[2] > touch->v.absmax[2] ||
			ent->v.absmax[0] < touch->v.absmin[0] || ent->v.absmax[1] < touch->v.absmin[1] || ent->v.absmax[2] < touch->v.absmin[2])
			continue;

		if (numtouched == MAX_EDICTS)
		{
			Con_DPrintf ("SV_TouchLinks : too many touched trigger_edicts (max = %d)\n", MAX_EDICTS);
			break;
		}
		else touchlist[numtouched++] = touch;
	}

	// touch linked edicts
	for (int i = 0; i < numtouched; i++)
	{
		if (touchlist[i])
		{
			int old_self = SVProgs->GlobalStruct->self;
			int old_other = SVProgs->GlobalStruct->other;

			SVProgs->GlobalStruct->time = sv.time;
			SVProgs->RunInteraction (touchlist[i], ent, touchlist[i]->v.touch);

			SVProgs->GlobalStruct->self = old_self;
			SVProgs->GlobalStruct->other = old_other;
		}
	}

	// done with the temp list now
	TempHunk->FreeToLowMark (hunkmark);

	// recurse down both sides
	if (node->axis != -1)
	{
		if (ent->v.absmin[node->axis] > node->dist) SV_TouchLinks (ent, node->children[0]);
		if (ent->v.absmin[node->axis] < node->dist) SV_TouchLinks (ent, node->children[1]);
	}
}


void SV_RotateBBoxToBBox (edict_t *ent, float *bbmin, float *bbmax, float *rmins, float *rmaxs)
{
	vec3_t bbox[8];
	QMATRIX av;

	// compute a full bounding box
	for (int i = 0; i < 8; i++)
	{
		bbox[i][0] = (i & 1) ? bbmin[0] : bbmax[0];
		bbox[i][1] = (i & 2) ? bbmin[1] : bbmax[1];
		bbox[i][2] = (i & 4) ? bbmin[2] : bbmax[2];
	}

	// derive forward/right/up vectors from the angles
	av.AngleVectors (ent->v.angles);

	// compute the rotated bbox corners
	Mod_ClearBoundingBox (rmins, rmaxs);

	// and rotate the bounding box
	for (int i = 0; i < 8; i++)
	{
		// and convert them to mins and maxs
		SV_RotatePoint (&av, bbox[i]);
		Mod_AccumulateBox (rmins, rmaxs, bbox[i]);
	}
}


void SV_RotateBBoxToAbsMinMax (edict_t *ent)
{
	float mins[3];
	float maxs[3];

	// compute initial bbox rotation
	SV_RotateBBoxToBBox (ent, ent->v.mins, ent->v.maxs, mins, maxs);

	// translate the bbox to it's final position at the entity origin
	Vector3Add (ent->v.absmin, ent->v.origin, mins);
	Vector3Add (ent->v.absmax, ent->v.origin, maxs);

	// Con_Printf ("%f %f %f\n", ent->v.angles[0], ent->v.angles[1], ent->v.angles[2]);
}


void SV_FindTouchedLeafs (edict_t *ent, mnode_t *node)
{
	if (node->contents == CONTENTS_SOLID) return;

	// add an efrag if the node is a leaf
	if (node->contents < 0)
	{
		if (ent->num_leafs < MAX_ENT_LEAFS)
		{
			ent->leafnums[ent->num_leafs] = ((mleaf_t *) node) - sv.worldmodel->brushhdr->leafs - 1;
			ent->num_leafs++;
		}

		return;
	}

	// split on this plane
	// use slower but tighter culling on the server-side
	int sides = BoxOnPlaneSide (ent->v.absmin, ent->v.absmax, node->plane);

	// recurse down the contacted sides
	if (sides & 1) SV_FindTouchedLeafs (ent, node->children[0]);
	if (sides & 2) SV_FindTouchedLeafs (ent, node->children[1]);
}


/*
===============
SV_LinkEdict

===============
*/
void SV_LinkEdict (edict_t *ent, bool touch_triggers)
{
	// clear all touched leafs so that if the edict doesn't get linked it won't get added to the client PVS
	ent->num_leafs = 0;

	if (ent->area.prev) SV_UnlinkEdict (ent);	// unlink from old position
	if (ent == SVProgs->Edicts) return;		// don't add the world
	if (ent->free) return;

	if (ent->v.solid == SOLID_BSP && (ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) && ent != SVProgs->Edicts)
		SV_RotateBBoxToAbsMinMax (ent);
	else
	{
		// set the abs box
		Vector3Add (ent->v.absmin, ent->v.origin, ent->v.mins);
		Vector3Add (ent->v.absmax, ent->v.origin, ent->v.maxs);
	}

	// to make items easier to pick up and allow them to be grabbed off
	// of shelves, the abs sizes are expanded
	if ((int) ent->v.flags & FL_ITEM)
	{
		ent->v.absmin[0] -= 15;
		ent->v.absmin[1] -= 15;
		ent->v.absmax[0] += 15;
		ent->v.absmax[1] += 15;
	}
	else
	{
		// because movement is clipped an epsilon away from an actual edge,
		// we must fully check even when bounding boxes don't quite touch
		ent->v.absmin[0] -= 1;
		ent->v.absmin[1] -= 1;
		ent->v.absmin[2] -= 1;
		ent->v.absmax[0] += 1;
		ent->v.absmax[1] += 1;
		ent->v.absmax[2] += 1;
	}

	// link to PVS leafs (this may be inherited across multiple frames...)
	if (ent->v.modelindex)
		SV_FindTouchedLeafs (ent, sv.worldmodel->brushhdr->nodes);

	if (ent->v.solid == SOLID_NOT)
		return;

	// find the first node that the ent's box crosses
	areanode_t *node = sv_areanodes;

	for (;;)
	{
		if (node->axis == -1)
			break;

		if (ent->v.absmin[node->axis] > node->dist)
			node = node->children[0];
		else if (ent->v.absmax[node->axis] < node->dist)
			node = node->children[1];
		else break;		// crosses the node
	}

	// link it in
	if (ent->v.solid == SOLID_TRIGGER)
		InsertLinkBefore (&ent->area, &node->trigger_edicts);
	else InsertLinkBefore (&ent->area, &node->solid_edicts);

	// if touch_triggers, touch all entities at this node and descend for more
	if (touch_triggers) SV_TouchLinks (ent, sv_areanodes);
}



/*
===============================================================================

POINT TESTING IN HULLS

===============================================================================
*/

/*
==================
SV_HullPointContents

==================
*/
int SV_HullPointContents (hull_t *hull, int num, vec3_t p)
{
	while (num >= 0)
	{
		if (num < hull->firstclipnode || num > hull->lastclipnode)
			Host_Error ("SV_HullPointContents: bad node number");

		mclipnode_t *node = hull->clipnodes + num;
		mplane_t *plane = hull->planes + node->planenum;

		if (Mod_PlaneDist (plane, p) < 0)
			num = node->children[1];
		else num = node->children[0];
	}

	return num;
}


/*
==================
SV_PointContents

==================
*/
int SV_PointContents (vec3_t p)
{
	int cont = SV_HullPointContents (&sv.worldmodel->brushhdr->hulls[0], 0, p);

	if (cont <= CONTENTS_CURRENT_0 && cont >= CONTENTS_CURRENT_DOWN)
		cont = CONTENTS_WATER;

	return cont;
}

int SV_TruePointContents (vec3_t p)
{
	return SV_HullPointContents (&sv.worldmodel->brushhdr->hulls[0], 0, p);
}

//===========================================================================

/*
============
SV_TestEntityPosition

This could be a lot more efficient...
============
*/
edict_t	*SV_TestEntityPosition (edict_t *ent)
{
	trace_t	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, MOVE_NORMAL, ent);

	if (trace.startsolid)
		return SVProgs->Edicts;

	return NULL;
}


/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/

/*
==================
SV_RecursiveHullCheck

==================
*/
bool SV_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	// check for empty
	if (num < 0)
	{
		if (num != CONTENTS_SOLID)
		{
			trace->allsolid = false;

			if (num == CONTENTS_EMPTY)
				trace->inopen = true;
			else trace->inwater = true;
		}
		else trace->startsolid = true;

		return true;		// empty
	}

	if (num < hull->firstclipnode || num > hull->lastclipnode)
		Sys_Error ("SV_RecursiveHullCheck: bad node number");

	// find the point distances
	mclipnode_t *node = hull->clipnodes + num;
	mplane_t *plane = hull->planes + node->planenum;

	float t1 = Mod_PlaneDist (plane, p1);
	float t2 = Mod_PlaneDist (plane, p2);

	if (t1 >= 0 && t2 >= 0) return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
	if (t1 < 0 && t2 < 0) return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);

	// put the crosspoint DIST_EPSILON pixels on the near side
	float frac = (t1 < 0) ? ((t1 + DIST_EPSILON) / (t1 - t2)) : ((t1 - DIST_EPSILON) / (t1 - t2));

	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;

	float midf = p1f + (p2f - p1f) * frac;

	float mid[] =
	{
		p1[0] + frac * (p2[0] - p1[0]),
		p1[1] + frac * (p2[1] - p1[1]),
		p1[2] + frac * (p2[2] - p1[2])
	};

	// move up to the node
	if (!SV_RecursiveHullCheck (hull, node->children[(t1 < 0)], p1f, midf, p1, mid, trace))
		return false;

	// go past the node
	if (SV_HullPointContents (hull, node->children[(t1 < 0) ^ 1], mid) != CONTENTS_SOLID)
		return SV_RecursiveHullCheck (hull, node->children[(t1 < 0) ^ 1], midf, p2f, mid, p2, trace);

	if (trace->allsolid)
		return false;		// never got out of the solid area

	// the other side of the node is solid, this is the impact point
	if (!(t1 < 0))
	{
		Vector3Copy (trace->plane.normal, plane->normal);
		trace->plane.dist = plane->dist;
	}
	else
	{
		Vector3Subtract (trace->plane.normal, vec3_origin, plane->normal);
		trace->plane.dist = -plane->dist;
	}

	int fixupcount = 0;

	while (SV_HullPointContents (hull, hull->firstclipnode, mid) == CONTENTS_SOLID)
	{
		// shouldn't really happen, but does occasionally
		frac -= 0.1f;

		if (frac < 0.0f || fixupcount > 400)
		{
			trace->fraction = midf;
			Vector3Copy (trace->endpos, mid);
			Con_DPrintf ("backup past 0\n");
			return false;
		}

		midf = p1f + (p2f - p1f) * frac;

		mid[0] = p1[0] + frac * (p2[0] - p1[0]);
		mid[1] = p1[1] + frac * (p2[1] - p1[1]);
		mid[2] = p1[2] + frac * (p2[2] - p1[2]);

		fixupcount++;
	}

	trace->fraction = midf;
	Vector3Copy (trace->endpos, mid);

	return false;
}


/*
==================
SV_ClipMoveToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points
==================
*/
trace_t SV_ClipMoveToEntity (edict_t *ent, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	trace_t		trace;
	vec3_t		offset;
	vec3_t		start_l, end_l;
	hull_t		*hull;

	// fill in a default trace
	memset (&trace, 0, sizeof (trace_t));
	trace.fraction = 1;
	trace.allsolid = true;
	Vector3Copy (trace.endpos, end);

	// get the clipping hull
	hull = SV_HullForEntity (ent, mins, maxs, offset);

	Vector3Subtract (start_l, start, offset);
	Vector3Subtract (end_l, end, offset);

	// rotate start and end into the models frame of reference
	if (ent->v.solid == SOLID_BSP && (ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) && ent != SVProgs->Edicts)
	{
		QMATRIX av;

		av.AngleVectors (ent->v.angles);
		SV_RotatePoint (&av, start_l);
		SV_RotatePoint (&av, end_l);
	}

	// trace a line through the apropriate clipping hull
	SV_RecursiveHullCheck (hull, hull->firstclipnode, 0, 1, start_l, end_l, &trace);

	// rotate endpos back to world frame of reference
	if (ent->v.solid == SOLID_BSP && (ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) && ent != SVProgs->Edicts)
	{
		if (trace.fraction != 1)
		{
			vec3_t   a;
			QMATRIX av;

			Vector3Subtract (a, vec3_origin, ent->v.angles);
			av.AngleVectors (a);
			SV_RotatePoint (&av, trace.endpos);
			SV_RotatePoint (&av, trace.plane.normal);
		}

		// fix trace up by the offset
		Vector3Add (trace.endpos, trace.endpos, offset);
	}
	else
	{
		// Cases where not Solid BSP or no avelocity
		// Otherwise backpacks from dead monsters and such can fall through the floor
		if (trace.fraction != 1)
			Vector3Add (trace.endpos, trace.endpos, offset);
	}

	// did we clip the move?
	if (trace.fraction < 1 || trace.startsolid)
		trace.ent = ent;

	return trace;
}

//===========================================================================

/*
====================
SV_ClipToLinks

Mins and maxs enclose the entire area swept by the move
====================
*/
void SV_ClipToLinks (areanode_t *node, moveclip_t *clip)
{
	link_t		*l, *next;
	edict_t		*touch;
	trace_t		trace;

loc0:;
	// touch linked edicts
	for (l = node->solid_edicts.next; l != &node->solid_edicts; l = next)
	{
		next = l->next;
		touch = EDICT_FROM_AREA (l);

		if (touch->v.solid == SOLID_NOT) continue;
		if (touch == clip->passedict) continue;
		if (touch->v.solid == SOLID_TRIGGER) Sys_Error ("Trigger in clipping list");
		if (clip->type == MOVE_NOMONSTERS && touch->v.solid != SOLID_BSP) continue;

		if (clip->boxmins[0] > touch->v.absmax[0] || clip->boxmins[1] > touch->v.absmax[1] || clip->boxmins[2] > touch->v.absmax[2] ||
			clip->boxmaxs[0] < touch->v.absmin[0] || clip->boxmaxs[1] < touch->v.absmin[1] || clip->boxmaxs[2] < touch->v.absmin[2])
			continue;

		if (clip->passedict && clip->passedict->v.size[0] && !touch->v.size[0]) continue;	// points never interact
		if (clip->trace.allsolid) return; // might intersect, so do an exact clip

		if (clip->passedict)
		{
			if (ProgToEdict (touch->v.owner) == clip->passedict) continue;	// don't clip against own missiles
			if (ProgToEdict (clip->passedict->v.owner) == touch) continue;	// don't clip against owner
		}

		if ((int) touch->v.flags & FL_MONSTER)
			trace = SV_ClipMoveToEntity (touch, clip->start, clip->mins2, clip->maxs2, clip->end);
		else trace = SV_ClipMoveToEntity (touch, clip->start, clip->mins, clip->maxs, clip->end);

		if (trace.allsolid || trace.startsolid || trace.fraction < clip->trace.fraction)
		{
			trace.ent = touch;

			if (clip->trace.startsolid)
			{
				clip->trace = trace;
				clip->trace.startsolid = true;
			}
			else clip->trace = trace;
		}
		else if (trace.startsolid)
			clip->trace.startsolid = true;
	}

	// recurse down both sides
	if (node->axis == -1) return;

	if (clip->boxmaxs[node->axis] > node->dist)
	{
		if (clip->boxmins[node->axis] < node->dist)
			SV_ClipToLinks(node->children[1], clip);

		node = node->children[0];
		goto loc0;
	}
	else if (clip->boxmins[node->axis] < node->dist)
	{
		node = node->children[1];
		goto loc0;
	}
}


/*
==================
SV_MoveBounds
==================
*/
void SV_MoveBounds (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, vec3_t boxmins, vec3_t boxmaxs)
{
	int		i;

	for (i = 0; i < 3; i++)
	{
		if (end[i] > start[i])
		{
			boxmins[i] = start[i] + mins[i] - 1;
			boxmaxs[i] = end[i] + maxs[i] + 1;
		}
		else
		{
			boxmins[i] = end[i] + mins[i] - 1;
			boxmaxs[i] = start[i] + maxs[i] + 1;
		}
	}
}


/*
==================
SV_Move
==================
*/
trace_t SV_Move (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int type, edict_t *passedict)
{
	moveclip_t	clip;

	memset (&clip, 0, sizeof (moveclip_t));

	// clip to world
	clip.trace = SV_ClipMoveToEntity (SVProgs->Edicts, start, mins, maxs, end);

	clip.start = start;
	clip.end = end;
	clip.mins = mins;
	clip.maxs = maxs;
	clip.type = type;
	clip.passedict = passedict;

	if (type == MOVE_MISSILE)
	{
		for (int i = 0; i < 3; i++)
		{
			clip.mins2[i] = -15;
			clip.maxs2[i] = 15;
		}
	}
	else
	{
		Vector3Copy (clip.mins2, mins);
		Vector3Copy (clip.maxs2, maxs);
	}

	// create the bounding box of the entire move
	SV_MoveBounds (start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs);

	// clip to entities
	SV_ClipToLinks (sv_areanodes, &clip);

	return clip.trace;
}

