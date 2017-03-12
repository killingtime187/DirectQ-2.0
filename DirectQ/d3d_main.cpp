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
// r_main.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "iqm.h"
#include "cl_fx.h"
#include "particles.h"

void D3DState_DrawOrderChanged (cvar_t *var);
void D3DState_WireFrameChanged (cvar_t *var);

cvar_t gl_fullbrights ("gl_fullbrights", "1", CVAR_ARCHIVE);
cvar_t r_draworder ("r_draworder", "0", 0, D3DState_DrawOrderChanged);

void D3DLight_AnimateLight (float time);
void V_CalcBlend (void);

void D3DSurf_BuildWorld (void);
void D3DSurf_DrawWorld (void);

void D3DWarp_BeginFrame (void);
void D3DFog_BeginFrame (void);

void D3DAlias_DrawViewModel (int passnum);
void D3DAlias_RenderAliasModels (void);
void D3DIQM_DrawIQMs (void);

void D3DBBoxes_Show (void);
void D3DLight_BeginFrame (void);
void D3DHLSL_UpdateMainCBuffer (void);

QMATRIX d3d_WorldMatrix;
QMATRIX d3d_ModelViewProjMatrix;

// render definition for this frame
d3d_renderdef_t d3d_RenderDef;

// view origin
QMATRIX r_viewvectors;

// screen size info
refdef_t	r_refdef;

texture_t	*r_notexture_mip;

cvar_t	r_norefresh ("r_norefresh", "0");
cvar_t	r_drawentities ("r_drawentities", "1");
cvar_t	r_drawviewmodel ("r_drawviewmodel", "1");
cvar_t	r_speeds ("r_speeds", "0");
cvar_t	r_lightmap ("r_lightmap", "0");
cvar_t	r_shadows ("r_shadows", "0", CVAR_ARCHIVE);
cvar_t	r_wateralpha ("r_wateralpha", 1.0f);

cvar_t	gl_cull ("gl_cull", "1");
cvar_t	gl_smoothmodels ("gl_smoothmodels", "1");
cvar_t	gl_affinemodels ("gl_affinemodels", "0");
cvar_t	gl_polyblend ("gl_polyblend", "1", CVAR_ARCHIVE);
cvar_t	gl_nocolors ("gl_nocolors", "0");
cvar_t	gl_doubleeyes ("gl_doubleeys", "1");
cvar_t	gl_clear ("gl_clear", "0");

// match software quake
cvar_t	r_clearcolor ("r_clearcolor", "2");

// renamed this because I had chosen a bad default... (urff)
cvar_t gl_underwaterfog ("r_underwaterfog", 0.0f, CVAR_ARCHIVE);

void R_ForceRecache (cvar_t *var)
{
	// force a rebuild of the PVS if any of the cvars attached to this change
	d3d_RenderDef.oldviewleaf = NULL;
	d3d_RenderDef.rebuildworld = true;
}


cvar_t r_novis ("r_novis", "0", 0, R_ForceRecache);
cvar_t r_lockpvs ("r_lockpvs", "0", 0, R_ForceRecache);
cvar_t r_lockfrustum ("r_lockfrustum", 0.0f, 0, R_ForceRecache);
cvar_t r_locksurfaces ("r_locksurfaces", "0", 0, R_ForceRecache);

cvar_t r_wireframe ("r_wireframe", 0.0f, 0, D3DState_WireFrameChanged);
cvar_t r_drawflat ("r_drawflat", 0.0f);
cvar_t r_showdepth ("r_showdepth", 0.0f);

void D3DAlpha_BeginFrame (void);

void D3D_BeginVisedicts (void)
{
	// begin all of our entity lists for this frame
	d3d_AliasEdicts.BeginFrame ();
	d3d_BrushEdicts.BeginFrame ();
	d3d_MergeEdicts.BeginFrame ();
	d3d_IQMEdicts.BeginFrame ();

	// and anything else
	D3DAlpha_BeginFrame ();

	// mark that we've gone to a new frame of entities (in case we ever separate this from actually presented frames)
	d3d_RenderDef.relinkframe++;
}


float CL_LerpAngle (float oldangle, float newangle, float lerpfrac);

void D3DMain_ComputeEntityTransform (entity_t *ent)
{
	// to do - move the cl_ interpolation to here too???

	// positional interpolation can be conditionally switched off too
	// rotation should be set up here rather than in the transform function so that bounding box building can use it
	if ((ent->model->type != mod_alias) && (ent->model->type != mod_iqm))
	{
		// move to the final position
		Vector3Copy (ent->lerporigin, ent->origin);
		Vector3Copy (ent->lerpangles, ent->angles);
	}
	else
	{
		extern cvar_t r_lerporient;

		if (!(ent->lerpflags & LERP_MOVESTEP) || !r_lerporient.integer)
		{
			// move to the final position
			Vector3Copy (ent->lerporigin, ent->origin);
			Vector3Copy (ent->lerpangles, ent->angles);
		}
		else
		{
			// only interpolate vertical movement if horizontal (step) movement also interpolates
			if (ent->curr.origin[0] != ent->prev.origin[0] || ent->curr.origin[1] != ent->prev.origin[1])
				Vector3Lerp (ent->lerporigin, ent->prev.origin, ent->curr.origin, ent->originlerp.blend);
			else Vector3Copy (ent->lerporigin, ent->origin);

			// send through CL_LerpAngle because it will do the shortest-path stuff for us
			ent->lerpangles[0] = CL_LerpAngle (ent->prev.angles[0], ent->curr.angles[0], ent->angleslerp.blend);
			ent->lerpangles[1] = CL_LerpAngle (ent->prev.angles[1], ent->curr.angles[1], ent->angleslerp.blend);
			ent->lerpangles[2] = CL_LerpAngle (ent->prev.angles[2], ent->curr.angles[2], ent->angleslerp.blend);
		}

		// optional pitch constraint for chase_active (cvar-ized)
		ent->lerpangles[0] *= ((ent == cls.entities[cl.viewentity] && chase_active.value) ? chase_pitch.value : 1.0f);

		// compute the shadevctor from the final angles which may be interpolated
		Q_sincos (
			-((ent->lerpangles[1] + ent->lerpangles[0]) / 180 * D3DX_PI),
			&ent->lightinfo.shadevector[1],
			&ent->lightinfo.shadevector[0]
		);

		ent->lightinfo.shadevector[2] = 1;
		Vector3Normalize (ent->lightinfo.shadevector);
	}
}


cvar_t r_bboxexpand ("r_bboxexpand", 0.0f, CVAR_ARCHIVE);

void D3DAlias_SetupFrame (entity_t *ent, aliashdr_t *hdr);
void D3DAlias_LerpToFrame (entity_t *ent, int pose, float interval);
void D3DLight_SetLightPointFlags (entity_t *ent);

void D3DMain_PrepEntityForRendering (entity_t *ent)
{
	if (!ent->model) return;

	float mins[3];
	float maxs[3];

	if (ent->model->type == mod_alias)
	{
		// use per-frame bboxes for entities
		aliasbbox_t *bboxes = ent->model->aliashdr->bboxes;

		// set up interpolation here to ensure that we get all entities
		// this also keeps interpolation frames valid even if the model has been culled away (bonus!)
		D3DAlias_SetupFrame (ent, ent->model->aliashdr);

		// use per-frame interpolated bboxes
		Vector3Lerp (mins, bboxes[ent->prev.pose].mins, bboxes[ent->curr.pose].mins, ent->poselerp.blend);
		Vector3Lerp (maxs, bboxes[ent->prev.pose].maxs, bboxes[ent->curr.pose].maxs, ent->poselerp.blend);
	}
	else if (ent->model->type == mod_iqm)
	{
		// lerp the frames
		D3DAlias_LerpToFrame (ent, ent->frame, (ent->lerpflags & LERP_FINISH) ? ent->lerpinterval : 0.1f);

		// use the real bboxes, not the physics bboxes
		Vector3Copy (mins, ent->model->iqmheader->mins);
		Vector3Copy (maxs, ent->model->iqmheader->maxs);
	}
	else if (ent->model->type == mod_brush)
	{
		Vector3Copy (mins, ent->model->brushhdr->bmins);
		Vector3Copy (maxs, ent->model->brushhdr->bmaxs);
	}
	else
	{
		Vector3Copy (mins, ent->model->mins);
		Vector3Copy (maxs, ent->model->maxs);
	}

	// set up flags for lightpoint
	D3DLight_SetLightPointFlags (ent);

	// compute the entity transform for all entity types
	D3DMain_ComputeEntityTransform (ent);

	// a rotated bbox only needs to be calculated if the bbox needs to rotate
	if (ent->lerpangles[0] || ent->lerpangles[1] || ent->lerpangles[2])
	{
		float bbox[8][3];

		// compute a full bounding box
		for (int i = 0; i < 8; i++)
		{
			bbox[i][0] = (i & 1) ? mins[0] : maxs[0];
			bbox[i][1] = (i & 2) ? mins[1] : maxs[1];
			bbox[i][2] = (i & 4) ? mins[2] : maxs[2];
		}

		QMATRIX av;

		// invert rotation for the bbox rotation using the interpolated angles
		if (ent->model->type == mod_brush)
			av.Rotate (-ent->lerpangles[1], -ent->lerpangles[0], -ent->lerpangles[2]);
		else av.Rotate (-ent->lerpangles[1], ent->lerpangles[0], -ent->lerpangles[2]);

		// compute the rotated bbox corners
		Mod_ClearBoundingBox (mins, maxs);

		// and rotate the bounding box
		for (int i = 0; i < 8; i++)
		{
			float tmp[3];

			Vector3Copy (tmp, bbox[i]);

			// is this just a matrix mult now???
			bbox[i][0] = Vector3Dot (av.fw, tmp);
			bbox[i][1] = Vector3Dot (av.rt, tmp);
			bbox[i][2] = Vector3Dot (av.up, tmp);

			// and convert them to mins and maxs
			Mod_AccumulateBox (mins, maxs, bbox[i]);
		}
	}

	// the bounding box is expanded by r_bboxexpand.value units in each direction so
	// that it won't z-fight with the model (if it's a tight box)
	// (note that Quake already does this so the default is now 0)
	Vector3Subtract (mins, mins, r_bboxexpand.value);
	Vector3Add (maxs, maxs, r_bboxexpand.value);

	// compute scaling factors
	ent->bboxscale[0] = (maxs[0] - mins[0]) * 0.5f;
	ent->bboxscale[1] = (maxs[1] - mins[1]) * 0.5f;
	ent->bboxscale[2] = (maxs[2] - mins[2]) * 0.5f;

	// translate the bbox to it's final position at the entity origin
	if ((ent->model->type == mod_alias) || (ent->model->type == mod_iqm))
	{
		Vector3Add (ent->cullinfo.mins, ent->lerporigin, mins);
		Vector3Add (ent->cullinfo.maxs, ent->lerporigin, maxs);
	}
	else
	{
		Vector3Add (ent->cullinfo.mins, ent->origin, mins);
		Vector3Add (ent->cullinfo.maxs, ent->origin, maxs);
	}

	// test all planes
	ent->cullinfo.clipflags = 31;
}


void D3DAlias_AddEdict (entity_t *ent);
void D3DSurf_AddEdict (entity_t *ent);

void D3D_AddVisEdict (entity_t *ent)
{
	// check for entities with no models
	if (!ent->model) return;

	// prevent entities from being added twice (as static entities will be added every frame the renderer runs
	// but the client may run less frequently)
	if (ent->relinkframe == d3d_RenderDef.relinkframe) return;

	// only add entities with supported model types
	switch (ent->model->type)
	{
	case mod_alias:
	case mod_brush:
	case mod_iqm:
	case mod_sprite:
		// explicitly bbcull this ent
		ent->nocullbox = false;

		// now mark that it's been added to the visedicts list for this client frame
		ent->relinkframe = d3d_RenderDef.relinkframe;

		// called here rather than elsewhere
		D3DMain_PrepEntityForRendering (ent);

		// add it to the appropriate list
		if (ent->model->type == mod_alias)
			D3DAlias_AddEdict (ent);
		else if (ent->model->type == mod_brush)
			D3DSurf_AddEdict (ent);
		else if (ent->model->type == mod_iqm)
			d3d_IQMEdicts.AddEntity (ent);
		else if (ent->model->type == mod_sprite)
			D3DAlpha_AddToList (ent);
		
		// done
		return;

	default:
		Con_DPrintf ("D3D_AddVisEdict: Unknown entity model type for %s\n", ent->model->name);
	}
}


bool R_CullSphere (float *center, float radius, int clipflags)
{
	int i;
	mplane_t *p;

	for (i = 0, p = vid.frustum; i < 5; i++, p++)
	{
		if (!(clipflags & (1 << i))) continue;
		if (Mod_PlaneDist (p, center) <= -radius) return true;
	}

	return false;
}


/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
#define CULLPOINT(cp1, cp2, cp3) \
	((p->normal[0] * cp1[0]) + (p->normal[1] * cp2[1]) + (p->normal[2] * cp3[2]))

bool R_CullPlaneForNearestPoint (cullinfo_t *ci, mplane_t *p)
{
	switch (p->signbits)
	{
	default:
	case 0: if (CULLPOINT (ci->maxs, ci->maxs, ci->maxs) < p->dist) return true; else break;
	case 1: if (CULLPOINT (ci->mins, ci->maxs, ci->maxs) < p->dist) return true; else break;
	case 2: if (CULLPOINT (ci->maxs, ci->mins, ci->maxs) < p->dist) return true; else break;
	case 3: if (CULLPOINT (ci->mins, ci->mins, ci->maxs) < p->dist) return true; else break;
	case 4: if (CULLPOINT (ci->maxs, ci->maxs, ci->mins) < p->dist) return true; else break;
	case 5: if (CULLPOINT (ci->mins, ci->maxs, ci->mins) < p->dist) return true; else break;
	case 6: if (CULLPOINT (ci->maxs, ci->mins, ci->mins) < p->dist) return true; else break;
	case 7: if (CULLPOINT (ci->mins, ci->mins, ci->mins) < p->dist) return true; else break;
	}

	return false;
}


bool R_CullPlaneForFarthestPoint (cullinfo_t *ci, mplane_t *p)
{
	switch (p->signbits)
	{
	default:
	case 0: if (CULLPOINT (ci->mins, ci->mins, ci->mins) < p->dist) return true; else break;
	case 1: if (CULLPOINT (ci->maxs, ci->mins, ci->mins) < p->dist) return true; else break;
	case 2: if (CULLPOINT (ci->mins, ci->maxs, ci->mins) < p->dist) return true; else break;
	case 3: if (CULLPOINT (ci->maxs, ci->maxs, ci->mins) < p->dist) return true; else break;
	case 4: if (CULLPOINT (ci->mins, ci->mins, ci->maxs) < p->dist) return true; else break;
	case 5: if (CULLPOINT (ci->maxs, ci->mins, ci->maxs) < p->dist) return true; else break;
	case 6: if (CULLPOINT (ci->mins, ci->maxs, ci->maxs) < p->dist) return true; else break;
	case 7: if (CULLPOINT (ci->maxs, ci->maxs, ci->maxs) < p->dist) return true; else break;
	}

	return false;
}


int R_PlaneSide (cullinfo_t *ci, mplane_t *p)
{
	float dist1, dist2;
	int sides = 0;

	switch (p->signbits)
	{
	default:
	case 0: dist1 = CULLPOINT (ci->maxs, ci->maxs, ci->maxs); dist2 = CULLPOINT (ci->mins, ci->mins, ci->mins); break;
	case 1: dist1 = CULLPOINT (ci->mins, ci->maxs, ci->maxs); dist2 = CULLPOINT (ci->maxs, ci->mins, ci->mins); break;
	case 2: dist1 = CULLPOINT (ci->maxs, ci->mins, ci->maxs); dist2 = CULLPOINT (ci->mins, ci->maxs, ci->mins); break;
	case 3: dist1 = CULLPOINT (ci->mins, ci->mins, ci->maxs); dist2 = CULLPOINT (ci->maxs, ci->maxs, ci->mins); break;
	case 4: dist1 = CULLPOINT (ci->maxs, ci->maxs, ci->mins); dist2 = CULLPOINT (ci->mins, ci->mins, ci->maxs); break;
	case 5: dist1 = CULLPOINT (ci->mins, ci->maxs, ci->mins); dist2 = CULLPOINT (ci->maxs, ci->mins, ci->maxs); break;
	case 6: dist1 = CULLPOINT (ci->maxs, ci->mins, ci->mins); dist2 = CULLPOINT (ci->mins, ci->maxs, ci->maxs); break;
	case 7: dist1 = CULLPOINT (ci->mins, ci->mins, ci->mins); dist2 = CULLPOINT (ci->maxs, ci->maxs, ci->maxs); break;
	}

	if (p->dist < dist1) sides |= BOX_INSIDE_PLANE;
	if (dist2 < p->dist) sides |= BOX_OUTSIDE_PLANE;

	return sides;
}


bool R_CullBox (cullinfo_t *ci)
{
	// test the plane that we last culled against first for a potential early-out
	if (ci->cullplane != -1)
		if (R_CullPlaneForNearestPoint (ci, &vid.frustum[ci->cullplane]))
			return true;

	for (int i = 0; i < 5; i++)
	{
		// already clipped against this plane
		if (!(ci->clipflags & (1 << i))) continue;
		if (i == ci->cullplane) continue;

		if (R_CullPlaneForNearestPoint (ci, &vid.frustum[i]))
		{
			// culled against this plane
			ci->cullplane = i;
			return true;
		}
	}

	// inside or intersects (fixme - how to tell which???)
	ci->cullplane = -1;
	return false;
}


//==================================================================================

int SignbitsForPlane (mplane_t *out)
{
	// for fast box on planeside test - identify which corner(s) of the box to text against the plane
	int bits = 0;

	if (out->normal[0] < 0) bits |= (1 << 0);
	if (out->normal[1] < 0) bits |= (1 << 1);
	if (out->normal[2] < 0) bits |= (1 << 2);

	return bits;
}


/*
===============
D3DMain_BeginFrame
===============
*/
void D3DMain_BeginFrame (void)
{
	// initialize r_speeds and flags
	d3d_RenderDef.brush_polys = 0;
	d3d_RenderDef.alias_polys = 0;

	// don't allow cheats in multiplayer
	if (cl.maxclients > 1) r_fullbright.Set (0.0f);

	D3DLight_AnimateLight (cl.time);

	// current viewleaf
	d3d_RenderDef.viewleaf = Mod_PointInLeaf (r_refdef.vieworigin, cl.worldmodel);
}


void D3DMain_SetupProjection (float fovx, float fovy, float zn, float zf, float depthhackscale)
{
	QMATRIX pm;
	int contents = d3d_RenderDef.viewleaf->contents;

	if (r_waterwarp.integer == 4)
	{
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			// variance is a percentage of width, where width = 2 * tan (fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.  what a mess!
			fovx = atan (tan (D3DXToRadian (fovx) / 2) * (0.97 + sin (cl.time * 1.5) * 0.03)) * 2 / (D3DX_PI / 180.0);
			fovy = atan (tan (D3DXToRadian (fovy) / 2) * (1.03 - sin (cl.time * 1.5) * 0.03)) * 2 / (D3DX_PI / 180.0);
		}
	}

	pm.Identity ();
	pm.Projection (fovx, fovy, zn, zf);

	if (r_waterwarp.integer == 2)
	{
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			pm._21 = sin (cl.time * 1.125f) * 0.0666f;
			pm._12 = cos (cl.time * 1.125f) * 0.0666f;

			pm.Scale ((cos (cl.time * 0.75f) + 20.0f) * 0.05f, (sin (cl.time * 0.75f) + 20.0f) * 0.05f, 1);
		}
	}

	// from Doom 3 - m16[14] is -(zf / (zf - zn)) * zn
	pm.m16[14] *= depthhackscale;

	QMATRIX::UpdateMVP (&d3d_ModelViewProjMatrix, &d3d_WorldMatrix, &pm);
}


/*
=============
D3D_PrepareRender
=============
*/
float r_oldvieworigin[3];
float r_oldviewangles[3];

void D3DMain_SetupD3D (void)
{
	vec3_t o, a;

	Vector3Subtract (o, r_refdef.vieworigin, r_oldvieworigin);
	Vector3Subtract (a, r_refdef.viewangles, r_oldviewangles);

	// this prevents greyflash when going to intermission (the client pos can interpolate between the changelevel and the intermission point)
	if (d3d_RenderDef.viewleaf && d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID) d3d_RenderDef.rebuildworld = true;

	// also check for and catch cases where a rebuildworld is flagged elsewhere so that we can update the old values correctly
	// (tighter frustum culling means that we can't use the looser check from the original)
	if (Vector3Length (o) >= 0.5f || Vector3Length (a) >= 0.2f || d3d_RenderDef.rebuildworld)
	{
		// Con_Printf ("rebuild on frame %i\n", d3d_RenderDef.framecount);
		d3d_RenderDef.rebuildworld = true;
		Vector3Copy (r_oldvieworigin, r_refdef.vieworigin);
		Vector3Copy (r_oldviewangles, r_refdef.viewangles);
	}

	// r_wireframe 1 is cheating in multiplayer but forcing it client-side is totally bogus as a cheater could just recompile the engine
	// note that D3D10+ separate the render target from the viewport so that clears are no longer restricted to just the viewport rect
	// the render target clear is handled in D3DRTT_BeginEffects as the active target may change (as is the viewport)
	// an r_draworder change will rebuild the depth test state so that it's inverted
	if (r_draworder.value)
		d3d11_Context->ClearDepthStencilView (d3d11_DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0, 1);
	else d3d11_Context->ClearDepthStencilView (d3d11_DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 1);

	// the world matrix is the inverse of a standard transform with z going up
	d3d_WorldMatrix.Identity ();
	d3d_WorldMatrix.Translate (r_refdef.vieworigin);
	d3d_WorldMatrix.Rotate (r_refdef.viewangles[1], r_refdef.viewangles[0], r_refdef.viewangles[2]);
	d3d_WorldMatrix.PutZGoingUp ();
	d3d_WorldMatrix.Inverse ();

	// create an initial projection matrix for deriving the frustum from; as Quake only culls against the top/bottom/left/right
	// planes we don't need to worry about the far clipping distance yet; we'll just set it to what it was last frame so it has
	// a good chance of being as close as possible anyway.  this will be set for real as soon as we gather surfaces together in
	// the refresh (which we can't do before this as we need to frustum cull there, and we don't know what the frustum is yet!)
	D3DMain_SetupProjection (vid.fov_x, vid.fov_y, 4, vid.farclip);

	// derive these properly
	r_viewvectors.AngleVectors (r_refdef.viewangles);

	// extract the frustum from it
	// retain the old frustum unless we're in the first few frames in which case we want one to be done
	// as a baseline
	if (!r_lockfrustum.integer || d3d_RenderDef.framecount < 5)
		d3d_ModelViewProjMatrix.ExtractFrustum (vid.frustum);

	for (int i = 0; i < 5; i++)
	{
		// frustum planes are never axial
		vid.frustum[i].type = PLANE_ANYZ;
		vid.frustum[i].dist = Vector3Dot (r_refdef.vieworigin, vid.frustum[i].normal); // FIXME: shouldn't this always be zero?
		vid.frustum[i].signbits = SignbitsForPlane (&vid.frustum[i]);
	}

	d3d11_State->RSSetState (d3d_RS3DView);
	d3d11_State->OMSetDepthStencilState (d3d_DepthTestAndWrite);
	d3d11_State->OMSetBlendState (NULL);
}


cvar_t r_truecontentscolour ("r_truecontentscolour", "1", CVAR_ARCHIVE);

float r_underwaterfogcolours[4];
void V_SetContentsColor (int contents);


void D3DMain_UpdateContentsColor (void)
{
	switch (d3d_RenderDef.viewleaf->contents)
	{
	case CONTENTS_EMPTY:
		// the cshift builtin fills the empty colour so we need to handle that (is this valid any more?)
		V_SetContentsColor (d3d_RenderDef.viewleaf->contents);
		d3d_RenderDef.lastgoodcontents = NULL;
		break;

	case CONTENTS_SOLID:
	case CONTENTS_SKY:
		// clear last good as well as contents percent so that we don't inherit contents from previous non-solid
		// leaf if we go into solid from a leaf that had a shift (e.g if noclipping)
		d3d_RenderDef.lastgoodcontents = NULL;
		cl.cshifts[CSHIFT_CONTENTS].percent = 0;
		break;

	default:
		// water, slime or lava
		if (d3d_RenderDef.viewleaf->contentscolor)
		{
			// let the player decide which behaviour they want
			if (r_truecontentscolour.integer)
			{
				// if the viewleaf has a contents colour set we just override with it
				cl.cshifts[CSHIFT_CONTENTS].destcolor[0] = d3d_RenderDef.viewleaf->contentscolor[0];
				cl.cshifts[CSHIFT_CONTENTS].destcolor[1] = d3d_RenderDef.viewleaf->contentscolor[1];
				cl.cshifts[CSHIFT_CONTENTS].destcolor[2] = d3d_RenderDef.viewleaf->contentscolor[2];

				// these now have more colour so reduce the percent to compensate
				if (d3d_RenderDef.viewleaf->contents == CONTENTS_WATER)
					cl.cshifts[CSHIFT_CONTENTS].percent = 128;
				else if (d3d_RenderDef.viewleaf->contents == CONTENTS_LAVA)
					cl.cshifts[CSHIFT_CONTENTS].percent = 150;
				else if (d3d_RenderDef.viewleaf->contents == CONTENTS_SLIME)
					cl.cshifts[CSHIFT_CONTENTS].percent = 150;
				else cl.cshifts[CSHIFT_CONTENTS].percent = 0;
			}
			else V_SetContentsColor (d3d_RenderDef.viewleaf->contents);

			// this was the last good colour used
			d3d_RenderDef.lastgoodcontents = d3d_RenderDef.viewleaf->contentscolor;
			break;
		}
		else if (d3d_RenderDef.lastgoodcontents)
		{
			// the leaf tracing at load time can occasionally miss a leaf so we take it from the last
			// good one we used unless we've had a contents change since.  this seems to only happen
			// with watervised maps that have been through bsp2prt so it seems as though there is
			// something wonky in the BSP tree on these maps.....
			if (d3d_RenderDef.oldviewleaf->contents == d3d_RenderDef.viewleaf->contents)
			{
				// update it and call recursively to get the now updated colour
				// Con_Printf ("D3DMain_UpdateContentsColor : fallback to last good contents!\n");
				d3d_RenderDef.viewleaf->contentscolor = d3d_RenderDef.lastgoodcontents;
				D3DMain_UpdateContentsColor ();
				return;
			}
		}

		// either we've had no last good contents colour or a contents transition so we
		// just fall back to the hard-coded default.  be sure to clear the last good as
		// we may have had a contents transition!!!
		V_SetContentsColor (d3d_RenderDef.viewleaf->contents);
		d3d_RenderDef.lastgoodcontents = NULL;
		break;
	}

	if (gl_underwaterfog.value > 0 && cl.cshifts[CSHIFT_CONTENTS].percent)
	{
		// derive an underwater fog colour from the contents shift
		r_underwaterfogcolours[0] = (float) cl.cshifts[CSHIFT_CONTENTS].destcolor[0] / 255.0f;
		r_underwaterfogcolours[1] = (float) cl.cshifts[CSHIFT_CONTENTS].destcolor[1] / 255.0f;
		r_underwaterfogcolours[2] = (float) cl.cshifts[CSHIFT_CONTENTS].destcolor[2] / 255.0f;
		r_underwaterfogcolours[3] = (float) cl.cshifts[CSHIFT_CONTENTS].percent / 100.0f;

		// reduce the contents shift
		cl.cshifts[CSHIFT_CONTENTS].percent *= 2;
		cl.cshifts[CSHIFT_CONTENTS].percent /= 3;
	}
	else r_underwaterfogcolours[3] = 0;

	if (cl.cshifts[CSHIFT_CONTENTS].percent > 0)
	{
		cl.cshifts[CSHIFT_CONTENTS].initialpercent = cl.cshifts[CSHIFT_CONTENTS].percent;
		cl.cshifts[CSHIFT_CONTENTS].time = cl.time;
	}

	// and now calc the final blend
	V_CalcBlend ();
}


int r_speedstime = -1;

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/

void D3DSurf_DrawBrushModel (entity_t *ent);

void V_AdjustContentCShift (int contents);
void D3DRTT_BeginEffects (void);
void D3DRTT_EndEffects (void);

void D3DMain_CSSetUAV (int slot, ID3D11UnorderedAccessView *uav, UINT initcount)
{
	d3d11_Context->CSSetUnorderedAccessViews (slot, 1, &uav, &initcount);
}


void R_RenderView (void)
{
	if (!D3DVid_IsCreated ()) return;

	double dTime1 = 0, dTime2 = 0;
	int hunkmark = TempHunk->GetLowMark ();

	if (r_norefresh.value)
	{
		d3d11_Context->ClearRenderTargetView (d3d11_RenderTargetView, D3DMisc_GetColorFromRGBA ((byte *) &d3d_QuakePalette.standard11[109]));
		return;
	}

	if (r_speeds.value) dTime1 = Sys_DoubleTime ();

	D3DMain_BeginFrame ();
	D3DWarp_BeginFrame ();
	D3DMain_UpdateContentsColor ();
	V_AdjustContentCShift (d3d_RenderDef.viewleaf->contents);
	V_UpdateCShifts ();

	// setup for render to texture
	D3DRTT_BeginEffects ();

	// set up to render
	D3DMain_SetupD3D ();

	// and anything else
	D3DLight_BeginFrame ();
	D3DFog_BeginFrame ();

	// build the world to get the final far clipping plane we will use
	D3DSurf_BuildWorld ();

	// now update the main constant buffer with our world constants
	D3DHLSL_UpdateMainCBuffer ();

	// draw the gun model first so that we get early-z on other scene objects (this is the solid view model pass)
	D3DAlias_DrawViewModel (0);

	// draw the world model (any brush models that need drawing are also handled here)
	D3DSurf_DrawWorld ();

	// draw our model types
	D3DAlias_RenderAliasModels ();
	D3DIQM_DrawIQMs ();

	// add particles to alpha list always
	ParticleSystem.AddToAlphaList ();

	// draw all items on the alpha list
	D3DAlpha_RenderList ();

	// optionally show bboxes
	D3DBBoxes_Show ();

	// the viewmodel comes last (this is the alpha view model pass)
	D3DAlias_DrawViewModel (1);

	// cascade all of our enumerated RTT effects for this frame
	D3DRTT_EndEffects ();

	if (r_speeds.value)
	{
		dTime2 = Sys_DoubleTime ();
		r_speedstime = (int) ((dTime2 - dTime1) * 1000.0);
	}
	else r_speedstime = -1;

	TempHunk->FreeToLowMark (hunkmark);
}


