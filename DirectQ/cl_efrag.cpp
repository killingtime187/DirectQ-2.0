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

// a lot of this crap is probably unnecessary but we'll clean it out gradually instead of doing a grand sweep

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"


void D3D_AddVisEdict (entity_t *ent);


// let's get rid of some more globals...
struct r_efragdef_t
{
	struct efrag_t		**lastlink;
	vec3_t		mins, maxs;
	entity_t	*addent;
};


void R_SplitEntityOnNode (entity_t *ent, mnode_t *node, r_efragdef_t *ed)
{
	if (node->contents == CONTENTS_SOLID) return;

	// add an efrag if the node is a leaf
	if (node->contents < 0)
	{
		mleaf_t *leaf = (mleaf_t *) node;

		// efrags can be just allocated as required without needing to be pulled from a list (cleaner)
		efrag_t *ef = (efrag_t *) MainHunk->Alloc (sizeof (efrag_t));

		ef->entity = ed->addent;

		// add the entity link
		ed->lastlink[0] = ef;
		ed->lastlink = &ef->entnext;
		ef->entnext = NULL;

		// set the leaf links
		ef->leaf = leaf;
		ef->leafnext = leaf->efrags;
		leaf->efrags = ef;

		return;
	}

	// split on this plane
	// this is load-time so it gets to use a box test for slower but tighter culling
	int sides = BoxOnPlaneSide (ed->mins, ed->maxs, node->plane);

	// recurse down the contacted sides
	if (sides & 1) R_SplitEntityOnNode (ent, node->children[0], ed);
	if (sides & 2) R_SplitEntityOnNode (ent, node->children[1], ed);
}


void R_AddEfrags (entity_t *ent)
{
	// entities with no model won't get drawn
	if (!ent->model) return;

	// never add the world
	if (ent == cls.entities[0]) return;

	r_efragdef_t ed;

	// init the efrag definition struct so that we can avoid more ugly globals
	ed.addent = ent;
	ed.lastlink = &ent->efrag;

	Vector3Add (ed.mins, ent->origin, ent->model->mins);
	Vector3Add (ed.maxs, ent->origin, ent->model->maxs);

	R_SplitEntityOnNode (ent, cl.worldmodel->brushhdr->nodes, &ed);
}


cvar_t r_drawflame ("r_drawflame", "1");

void R_StoreEfrags (efrag_t **ppefrag)
{
	efrag_t *pefrag = NULL;
	entity_t *ent = NULL;

	while ((pefrag = *ppefrag) != NULL)
	{
		ent = pefrag->entity;
		ppefrag = &pefrag->leafnext;

		// some progs might try to send static ents with no model through here...
		if (!ent->model) continue;

		// allow skipping flames
		if (ent->model->type == mod_alias && (ent->model->aliashdr->drawflags & AM_FLAME) && !r_drawflame.value) continue;

		// prevent adding twice in this render frame (or if an entity is in more than one leaf)
		if (ent->visframe == d3d_RenderDef.framecount) continue;

		// if the entity wasn't recorded on the previous frame reset it's lerp
		if (ent->visframe != d3d_RenderDef.framecount - 1)
			ent->lerpflags |= LERP_RESETALL;
		else ent->lerpflags |= LERP_RESETMOVE;

		// add it to the visible edicts list
		D3D_AddVisEdict (ent);

		// mark that we've recorded this entity for this frame
		ent->visframe = d3d_RenderDef.framecount;
	}
}


