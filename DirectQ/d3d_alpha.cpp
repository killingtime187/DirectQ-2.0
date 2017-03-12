/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "particles.h"

#define MAX_ALPHA_ITEMS		65536

// list of alpha items
struct d3d_alphalist_t
{
	int Type;
	float Dist;

	// added for brush surfaces so that they don't need to allocate a modelsurf (yuck)
	entity_t *SurfEntity;

	union
	{
		dlight_t *DLight;
		entity_t *Entity;
		emitter_t *Particle;
		msurface_t *surf;
		void *data;
	};
};

// if these are modified then d3d_AlphaBeginFuncs and 
// d3d_AlphaEndFuncs (below) must also be modified!!!
#define D3D_ALPHATYPE_PARTICLE		0
#define D3D_ALPHATYPE_WATERWARP		1
#define D3D_ALPHATYPE_SURFACE		2
#define D3D_ALPHATYPE_FENCE			3
#define D3D_ALPHATYPE_CORONA		4
#define D3D_ALPHATYPE_ALIAS			5
#define D3D_ALPHATYPE_BRUSH			6
#define D3D_ALPHATYPE_SPRITE		7
#define D3D_ALPHATYPE_IQM			8
#define D3D_ALPHATYPE_NULL			9

d3d_alphalist_t **d3d_AlphaList = NULL;
int d3d_NumAlphaList = 0;

void D3DAlpha_BeginFrame (void)
{
	// we're no longer checking for NULL so this can be fast-alloc'ed
	d3d_AlphaList = (d3d_alphalist_t **) TempHunk->FastAlloc (MAX_ALPHA_ITEMS * sizeof (d3d_alphalist_t *));
	d3d_NumAlphaList = 0;
}

float D3DAlpha_GetDist (float *origin)
{
	// no need to sqrt these as all we're concerned about is relative distances
	// (if x < y then sqrt (x) is also < sqrt (y))
	return
	(
		(origin[0] - r_refdef.vieworigin[0]) * (origin[0] - r_refdef.vieworigin[0]) +
		(origin[1] - r_refdef.vieworigin[1]) * (origin[1] - r_refdef.vieworigin[1]) +
		(origin[2] - r_refdef.vieworigin[2]) * (origin[2] - r_refdef.vieworigin[2])
	);
}

void D3DAlpha_AddToList (int type, void *data, float dist)
{
	if (d3d_NumAlphaList == MAX_ALPHA_ITEMS) return;

	// on temp hunk so it's always going to be allocated
	d3d_AlphaList[d3d_NumAlphaList] = (d3d_alphalist_t *) TempHunk->FastAlloc (sizeof (d3d_alphalist_t));

	d3d_AlphaList[d3d_NumAlphaList]->Type = type;
	d3d_AlphaList[d3d_NumAlphaList]->data = data;
	d3d_AlphaList[d3d_NumAlphaList]->Dist = dist;

	d3d_NumAlphaList++;
}

void D3DAlpha_AddToList (entity_t *ent)
{
	if (ent->model->type == mod_alias)
		D3DAlpha_AddToList (D3D_ALPHATYPE_ALIAS, ent, D3DAlpha_GetDist (ent->origin));
	else if (ent->model->type == mod_iqm)
		D3DAlpha_AddToList (D3D_ALPHATYPE_IQM, ent, D3DAlpha_GetDist (ent->origin));
	else if (ent->model->type == mod_brush)
		D3DAlpha_AddToList (D3D_ALPHATYPE_BRUSH, ent, D3DAlpha_GetDist (ent->origin));
	else if (ent->model->type == mod_sprite)
		D3DAlpha_AddToList (D3D_ALPHATYPE_SPRITE, ent, D3DAlpha_GetDist (ent->origin));
	else;
}

void D3DAlpha_AddToList (msurface_t *surf, entity_t *ent, float *midpoint)
{
	// we only support turb surfaces for now
	if (surf->flags & SURF_DRAWTURB)
		D3DAlpha_AddToList (D3D_ALPHATYPE_WATERWARP, surf, D3DAlpha_GetDist (midpoint));
	else if (surf->flags & SURF_DRAWFENCE)
		D3DAlpha_AddToList (D3D_ALPHATYPE_FENCE, surf, D3DAlpha_GetDist (midpoint));
	else D3DAlpha_AddToList (D3D_ALPHATYPE_SURFACE, surf, D3DAlpha_GetDist (midpoint));

	// eeewwww
	d3d_AlphaList[d3d_NumAlphaList - 1]->SurfEntity = ent;
}

void D3DAlpha_AddToList (emitter_t *particle)
{
	D3DAlpha_AddToList (D3D_ALPHATYPE_PARTICLE, particle, D3DAlpha_GetDist (particle->spawnorg));
}

void D3DAlpha_AddToList (dlight_t *dl)
{
	D3DAlpha_AddToList (D3D_ALPHATYPE_CORONA, dl, D3DAlpha_GetDist (dl->origin));
}

int D3DAlpha_SortFunc (const void *a, const void *b)
{
	d3d_alphalist_t *al1 = *(d3d_alphalist_t **) a;
	d3d_alphalist_t *al2 = *(d3d_alphalist_t **) b;

	// back to front ordering
	// this is more correct as it will order surfs properly if less than 1 unit separated
	if (al2->Dist > al1->Dist)
		return 1;
	else if (al2->Dist < al1->Dist)
		return -1;
	else return 0;
}


void D3DAlias_SetupAliasModel (entity_t *e);
void D3DAlias_DrawAliasBatch (entity_t **ents, int numents, int flags = 0);
void D3DSprite_Draw (entity_t *ent);

void D3DBrush_BeginSurfaces (void);
void D3DBrush_EndAlphaSurfaces (void);
void D3DBrush_BatchAlphaSurface (msurface_t *surf, entity_t *ent, int alpha);
void D3DWarp_DrawAlphaSurface (msurface_t *surf, entity_t *ent);

void D3DPart_Begin (void);
void D3DPart_DrawEmitter (emitter_t *pe);
void D3DPart_End (void);

void D3DSprite_Begin (void);
void D3DSprite_End (void);

void D3DLight_BeginCoronas (void);
void D3DLight_EndCoronas (void);
void D3DLight_DrawCorona (dlight_t *dl);

void D3DIQM_DrawIQM (entity_t *ent);

void D3DAlpha_NullFunc (void) {}

void D3DAlpha_Cull (void) {}
void D3DAlpha_NoCull (void) {}


xcommand_t d3d_AlphaEndFuncs[] =
{
	D3DPart_End,				// D3D_ALPHATYPE_PARTICLE
	D3DBrush_EndAlphaSurfaces,	// D3D_ALPHATYPE_WATERWARP
	D3DBrush_EndAlphaSurfaces,	// D3D_ALPHATYPE_SURFACE
	D3DBrush_EndAlphaSurfaces,	// D3D_ALPHATYPE_FENCE
	D3DLight_EndCoronas,		// D3D_ALPHATYPE_CORONA
	D3DAlpha_NullFunc,			// D3D_ALPHATYPE_ALIAS
	D3DAlpha_NullFunc,			// D3D_ALPHATYPE_BRUSH
	D3DSprite_End,				// D3D_ALPHATYPE_SPRITE
	D3DAlpha_NullFunc,			// D3D_ALPHATYPE_IQM
	D3DAlpha_NullFunc			// D3D_ALPHATYPE_NULL
};

xcommand_t d3d_AlphaBeginFuncs[] =
{
	D3DPart_Begin,					// D3D_ALPHATYPE_PARTICLE
	D3DBrush_BeginSurfaces,			// D3D_ALPHATYPE_WATERWARP
	D3DBrush_BeginSurfaces,			// D3D_ALPHATYPE_SURFACE
	D3DBrush_BeginSurfaces,			// D3D_ALPHATYPE_FENCE
	D3DLight_BeginCoronas,			// D3D_ALPHATYPE_CORONA
	D3DAlpha_NullFunc,				// D3D_ALPHATYPE_ALIAS
	D3DAlpha_NullFunc,				// D3D_ALPHATYPE_BRUSH
	D3DSprite_Begin,				// D3D_ALPHATYPE_SPRITE
	D3DAlpha_NullFunc,				// D3D_ALPHATYPE_IQM
	D3DAlpha_NullFunc				// D3D_ALPHATYPE_NULL
};


void D3DAlpha_RenderList (void)
{
	// nothing to add
	if (!d3d_AlphaList) return;
	if (!d3d_NumAlphaList) return;

	// sort the alpha list
	if (d3d_NumAlphaList > 1)
		qsort (d3d_AlphaList, d3d_NumAlphaList, sizeof (d3d_alphalist_t *), D3DAlpha_SortFunc);

	d3d11_State->OMSetBlendState (d3d_AlphaBlendEnable);
	d3d11_State->OMSetDepthStencilState (d3d_DepthTestNoWrite);

	// so that previous always has a value (for simpler logic below)
	d3d_alphalist_t nullitem;
	d3d_alphalist_t *previous = &nullitem;

	// don't call a state change func for this item
	previous->Type = D3D_ALPHATYPE_NULL;

	// now add all the items in it to the alpha buffer
	for (int i = 0; i < d3d_NumAlphaList; i++)
	{
		// check for state change
#pragma warning(suppress: 6385)
		if (d3d_AlphaList[i]->Type != previous->Type)
		{
#pragma warning(suppress: 6385)
			d3d_AlphaEndFuncs[previous->Type] ();
#pragma warning(suppress: 6385)
			d3d_AlphaBeginFuncs[d3d_AlphaList[i]->Type] ();
			d3d11_State->SynchronizeState ();
		}

		switch (d3d_AlphaList[i]->Type)
		{
		case D3D_ALPHATYPE_ALIAS:
			D3DAlias_DrawAliasBatch (&d3d_AlphaList[i]->Entity, 1);
			break;

		case D3D_ALPHATYPE_IQM:
			D3DIQM_DrawIQM (d3d_AlphaList[i]->Entity);
			break;

		case D3D_ALPHATYPE_SPRITE:
			D3DSprite_Draw (d3d_AlphaList[i]->Entity);
			break;

		case D3D_ALPHATYPE_PARTICLE:
			D3DPart_DrawEmitter (d3d_AlphaList[i]->Particle);
			break;

		case D3D_ALPHATYPE_WATERWARP:
			D3DWarp_DrawAlphaSurface (d3d_AlphaList[i]->surf, d3d_AlphaList[i]->SurfEntity);
			break;

		case D3D_ALPHATYPE_SURFACE:
		case D3D_ALPHATYPE_FENCE:
			D3DBrush_BatchAlphaSurface (d3d_AlphaList[i]->surf, d3d_AlphaList[i]->SurfEntity, d3d_AlphaList[i]->SurfEntity->alphaval);
			break;

		case D3D_ALPHATYPE_CORONA:
			D3DLight_DrawCorona (d3d_AlphaList[i]->DLight);
			break;

		case D3D_ALPHATYPE_BRUSH:
			// not implemented - bmodels are added and sorted per-surface
		default:
			// nothing to add
			break;
		}

		// this now becomes the entry to check the next one against
		previous = d3d_AlphaList[i];
	}

	// take down the final state used (in case it was a HLSL state)
	// (we ensured that previous is always a valid entry)
	d3d_AlphaEndFuncs[previous->Type] ();

	d3d11_State->OMSetBlendState (NULL);
	d3d11_State->OMSetDepthStencilState (d3d_DepthTestAndWrite);

	// reset alpha list
	// Con_Printf ("%i items in alpha list\n", d3d_NumAlphaList);
	d3d_NumAlphaList = 0;
}

