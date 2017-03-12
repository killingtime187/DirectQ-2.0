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
// r_light.c

// odd, but "map a staging texture and use that" instead of allocing directly in system mem is faster, despite the overhead of the map

// this code has now gotten a mite stinky around the offsets bit
#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

// never more than this many lightmaps
#define MAX_LIGHTMAPS		D3D10_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION

cvar_t r_lightscale ("r_lightscale", "1", CVAR_ARCHIVE);

// note - we need to retain the MAX_LIGHTMAPS limit for texturechain building so we put this in a static array instead of a vector
// (this can probably change now that we have a texture array)
// the staging texture could just go to a memory block, then we add a modified flag
class QLIGHTMAP
{
private:
	D3D11_BOX LightBox;
	unsigned int *Texels;
	bool Modified;

	void ConstructDestruct (void)
	{
		this->ResetBox ();
		this->Texels = NULL;
		this->Modified = false;
	}

public:
	QLIGHTMAP (void)
	{
		this->ConstructDestruct ();
	}

	~QLIGHTMAP (void)
	{
		this->ConstructDestruct ();
	}

	void AllocTexels (void)
	{
		if (!this->Texels)
		{
			// alloc an initial bunch of texels
			this->Texels = (unsigned *) MainHunk->Alloc (LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4);
		}
	}

	void ClearTexels (void)
	{
		this->Texels = NULL;
	}

	void ResetBox (void)
	{
		this->LightBox.left = LIGHTMAP_SIZE;
		this->LightBox.right = 0;
		this->LightBox.top = LIGHTMAP_SIZE;
		this->LightBox.bottom = 0;
		this->LightBox.front = 0;
		this->LightBox.back = 1;

		// the lightmap is no longer modified
		this->Modified = false;
	}

	void ExpandBox (D3D11_BOX *box)
	{
		if (box->left < this->LightBox.left) this->LightBox.left = box->left;
		if (box->right > this->LightBox.right) this->LightBox.right = box->right;
		if (box->top < this->LightBox.top) this->LightBox.top = box->top;
		if (box->bottom > this->LightBox.bottom) this->LightBox.bottom = box->bottom;

		// the lightmap is modified now
		this->Modified = true;
	}

	unsigned int *BoxTexels (D3D11_BOX *box)
	{
		if (this->Texels)
			return &this->Texels[box->top * LIGHTMAP_SIZE + box->left];
		else return NULL;
	}

	void Update (int dstslice)
	{
		if (this->Texels && this->Modified)
		{
			// this is about 15% faster because it manages CPU/GPU contention for us
			// note stupid restrictions in ID3D11Texture2D:
			// - a dynamic texture can't be an array (so no mapping at all)
			// - you can only map with discard
			d3d11_Context->UpdateSubresource (
				QLIGHTMAP::Texture,
				dstslice,
				&this->LightBox,
				this->BoxTexels (&this->LightBox),
				LIGHTMAP_SIZE << 2,
				0
			);

			this->ResetBox ();
			d3d_RenderDef.numdlight++;
		}
	}

	// lightmap texture array
	static ID3D11Texture2D *Texture;
	static ID3D11ShaderResourceView *SRV;

	// this is nuts that you can do this...
	static QLIGHTMAP Lightmaps[MAX_LIGHTMAPS];

	// let's get rid of some globals
	static int LightProperty;
	static int NumLightmaps;
	static unsigned short *Allocated;
};


// this is nuts that you can do this...
QLIGHTMAP QLIGHTMAP::Lightmaps[MAX_LIGHTMAPS];

// eeeewww - disgusting language
ID3D11Texture2D *QLIGHTMAP::Texture = NULL;
ID3D11ShaderResourceView *QLIGHTMAP::SRV = NULL;

int QLIGHTMAP::LightProperty = 0;
int QLIGHTMAP::NumLightmaps = 0;
unsigned short *QLIGHTMAP::Allocated = NULL;


void D3DLight_ClearLightmaps (void)
{
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		QLIGHTMAP::Lightmaps[i].ClearTexels ();
		QLIGHTMAP::Lightmaps[i].ResetBox ();
	}

	SAFE_RELEASE (QLIGHTMAP::Texture);
	SAFE_RELEASE (QLIGHTMAP::SRV);

	QLIGHTMAP::Allocated = NULL;
	QLIGHTMAP::NumLightmaps = 0;
	QLIGHTMAP::LightProperty++;
}


void D3DLight_InitLightmaps (void)
{
	D3DLight_ClearLightmaps ();
}


void D3DLight_ShutdownLightmaps (void)
{
	D3DLight_ClearLightmaps ();
}


CD3DInitShutdownHandler d3d_LightHandler ("light", D3DLight_InitLightmaps, D3DLight_ShutdownLightmaps);


void D3DLight_PropertyChange (cvar_t *var)
{
	// go to a new property set
	QLIGHTMAP::LightProperty++;
}

cvar_t r_ambient ("r_ambient", "0", 0, D3DLight_PropertyChange);
cvar_t r_fullbright ("r_fullbright", "0", 0, D3DLight_PropertyChange);
cvar_t r_coloredlight ("r_coloredlight", "1", CVAR_ARCHIVE, D3DLight_PropertyChange);
cvar_t r_overbright ("r_overbright", 1.0f, CVAR_ARCHIVE, D3DLight_PropertyChange);
cvar_t r_hdrlight ("r_hdrlight", 1.0f, CVAR_ARCHIVE, D3DLight_PropertyChange);
cvar_t r_dynamic ("r_dynamic", "1", 0, D3DLight_PropertyChange);

cvar_t r_lerplightstyle ("r_lerplightstyle", "0", CVAR_ARCHIVE);
cvar_alias_t gl_overbright ("gl_overbright", &r_overbright);
cvar_t gl_overbright_models ("gl_overbright_models", 1.0f, CVAR_ARCHIVE);

cvar_t v_dlightcshift ("v_dlightcshift", 1.0f);

struct r_coronadlight_t
{
	dlight_t *dl;
	float radius;
	unsigned colour;
};

// let's get rid of some more globals
struct lightglobals_t
{
	int CoronaState;
	int NumCoronas;
	int dlightframecount;

	int StyleValue[MAX_LIGHTSTYLES];	// 8.8 fraction of base light value
	int ValueTable[256];
	r_coronadlight_t Coronas[MAX_DLIGHTS];
};

lightglobals_t d3d_LightGlobals;

/*
========================================================================================================================

		CORONA RENDERING

========================================================================================================================
*/

cvar_t gl_flashblend ("gl_flashblend", "0", CVAR_ARCHIVE);
cvar_t r_coronas ("r_coronas", "0", CVAR_ARCHIVE);
cvar_t r_coronaradius ("r_coronaradius", "1", CVAR_ARCHIVE);
cvar_t r_coronaintensity ("r_coronaintensity", "1", CVAR_ARCHIVE);

void D3DAlpha_AddToList (dlight_t *dl);

#define CORONA_ONLY				1
#define LIGHTMAP_ONLY			2
#define CORONA_PLUS_LIGHTMAP	3


void D3DLight_BeginCoronas (void)
{
	// fix up cvars
	if (r_coronaradius.integer < 0) r_coronaradius.Set (0.0f);
	if (r_coronaintensity.value < 0) r_coronaintensity.Set (0.0f);

	// nothing yet
	d3d_LightGlobals.NumCoronas = 0;
}


void D3DCorona_Begin (void);
void D3DCorona_DrawSingle (float *origin, unsigned colour, float radius);
void D3DCorona_End (void);

void D3DLight_EndCoronas (void)
{
	if (d3d_LightGlobals.NumCoronas)
	{
		D3DCorona_Begin ();

		for (int i = 0; i < d3d_LightGlobals.NumCoronas; i++)
		{
			r_coronadlight_t *cdl = &d3d_LightGlobals.Coronas[i];
			dlight_t *dl = cdl->dl;

			D3DCorona_DrawSingle (dl->origin, cdl->colour, cdl->radius);
		}

		// draw anything that needs to be drawn
		D3DCorona_End ();
		d3d_LightGlobals.NumCoronas = 0;
	}
}


void D3DLight_AddLightBlend (float r, float g, float b, float a2)
{
	if (v_dlightcshift.value >= 0) a2 *= v_dlightcshift.value;

	float a;
	float blend[4] =
	{
		vid.cshift[0] * 0.00390625f, 
		vid.cshift[1] * 0.00390625f, 
		vid.cshift[2] * 0.00390625f, 
		vid.cshift[3] * 0.00390625f
	};

	blend[3] = a = blend[3] + a2 * (1 - blend[3]);

	a2 = a2 / a;

	vid.cshift[0] = (blend[0] * (1 - a2) + r * a2) * 255.0f;
	vid.cshift[1] = (blend[1] * (1 - a2) + g * a2) * 255.0f;
	vid.cshift[2] = (blend[2] * (1 - a2) + b * a2) * 255.0f;
	vid.cshift[3] = blend[3] * 255.0f;
}


void D3DLight_DrawCorona (dlight_t *dl)
{
	float v[3];
	float colormul = 0.075f;
	float rad = dl->radius * 0.35 * r_coronaradius.value;

	// reduce corona size and boost intensity a little (to make them easier to see) with r_coronas 2...
	if (d3d_LightGlobals.CoronaState == CORONA_PLUS_LIGHTMAP)
	{
		rad *= 0.5f;

		// ?? DP allows different values of it to do this ??
		colormul = 0.1f * r_coronas.value;
	}

	// catch anything that's too small before it's even drawn
	if (rad <= 0) return;
	if (r_coronaintensity.value <= 0) return;

	Vector3Subtract (v, dl->origin, r_refdef.vieworigin);
	float dist = Vector3Length (v);

	// fixme - optimize this out before adding... (done - ish)
	if (dist < rad)
	{
		// view is inside the dlight
		if (d3d_LightGlobals.CoronaState == CORONA_ONLY)
			D3DLight_AddLightBlend ((float) dl->rgb[0] / 512.0f, (float) dl->rgb[1] / 512.0f, (float) dl->rgb[2] / 512.0f, dl->radius * 0.0003);

		return;
	}

	// store it for drawing later
	// we set this array to MAX_DLIGHTS so we don't need to bounds-check it ;)
	d3d_LightGlobals.Coronas[d3d_LightGlobals.NumCoronas].dl = dl;
	d3d_LightGlobals.Coronas[d3d_LightGlobals.NumCoronas].radius = rad;

	byte *colour = (byte *) &d3d_LightGlobals.Coronas[d3d_LightGlobals.NumCoronas].colour;

	for (int i = 0; i < 3; i++)
	{
		int c = dl->rgb[i] * r_coronaintensity.value * colormul;
		colour[i] = BYTE_CLAMP (c);
	}

	colour[3] = 255;

	// go to the next light
	d3d_LightGlobals.NumCoronas++;
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
==================
D3DLight_AnimateLight
==================
*/
void R_SetDefaultLightStyles (void)
{
	for (int i = 0; i < 256; i++)
		d3d_LightGlobals.ValueTable[i] = (int) ((((float) ((signed char) i - 'a') * 5610.0f) / 264.0f) + 0.5f);

	// normal light value - making this consistent with a value of 'm' in D3DLight_AnimateLight
	// will prevent the upload of lightmaps when a surface is first seen
	for (int i = 0; i < MAX_LIGHTSTYLES; i++) d3d_LightGlobals.StyleValue[i] = d3d_LightGlobals.ValueTable['m'];
}


void D3DLight_AnimateLight (float time)
{
	// made this cvar-controllable!
	if (!(r_dynamic.value > 0))
	{
		// set everything to median light
		for (int i = 0; i < MAX_LIGHTSTYLES; i++)
			d3d_LightGlobals.StyleValue[i] = d3d_LightGlobals.ValueTable['m'];
	}
	else if (r_lerplightstyle.value)
	{
		// interpolated light animations
		int			j;
		float		l;
		int			flight;
		int			clight;
		float		lerpfrac;
		float		backlerp;

		// light animations
		// 'm' is normal light, 'a' is no light, 'z' is double bright
		// to do - coarsen this lerp so that we don't overdo the updates
		flight = (int) floor (time * 10.0f);
		clight = (int) ceil (time * 10.0f);
		lerpfrac = (time * 10.0f) - flight;
		backlerp = 1.0f - lerpfrac;

		for (j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cls.lightstyles[j].length)
			{
				d3d_LightGlobals.StyleValue[j] = d3d_LightGlobals.ValueTable['m'];
				continue;
			}
			else if (cls.lightstyles[j].length == 1)
			{
				// single length style so don't bother interpolating
				d3d_LightGlobals.StyleValue[j] = d3d_LightGlobals.ValueTable[cls.lightstyles[j].map[0]];
				continue;
			}

			// interpolate animating light
			l = (float) d3d_LightGlobals.ValueTable[cls.lightstyles[j].map[flight % cls.lightstyles[j].length]] * backlerp;
			l += (float) d3d_LightGlobals.ValueTable[cls.lightstyles[j].map[clight % cls.lightstyles[j].length]] * lerpfrac;

			d3d_LightGlobals.StyleValue[j] = (int) l;
		}
	}
	else
	{
		// old light animation
		int i = (int) (time * 10.0f);

		for (int j = 0; j < MAX_LIGHTSTYLES; j++)
		{
			if (!cls.lightstyles[j].length)
			{
				d3d_LightGlobals.StyleValue[j] = d3d_LightGlobals.ValueTable['m'];
				continue;
			}
			else if (cls.lightstyles[j].length == 1)
			{
				// single length style so don't bother interpolating
				d3d_LightGlobals.StyleValue[j] = d3d_LightGlobals.ValueTable[cls.lightstyles[j].map[0]];
				continue;
			}

			d3d_LightGlobals.StyleValue[j] = d3d_LightGlobals.ValueTable[cls.lightstyles[j].map[i % cls.lightstyles[j].length]];
		}
	}
}


/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int num, mnode_t *node, bool bspmodel)
{
start:;
	if (node->contents < 0) return;

	float dist = Mod_PlaneDist (node->plane, light->transformed);

	if (dist > light->radius - light->minlight)
	{
		node = node->children[0];
		goto start;
	}

	if (dist < -light->radius + light->minlight)
	{
		node = node->children[1];
		goto start;
	}

	msurface_t *surf = node->surfaces;
	float maxdist = light->radius * light->radius;

	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		// no lights on these
		if (surf->flags & SURF_DRAWTURB) continue;
		if (surf->flags & SURF_DRAWSKY) continue;

		// these just repeat calcs in adddynamics - can/should we cache them????
		float impact[3] =
		{
			light->transformed[0] - surf->plane->normal[0] * dist,
			light->transformed[1] - surf->plane->normal[1] * dist,
			light->transformed[2] - surf->plane->normal[2] * dist
		};

		float local[2] =
		{
			Vector3Dot (impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0],
			Vector3Dot (impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1]
		};

		// clamp center of light to corner and check brightness
		int s = local[0] + 0.5; if (s < 0) s = 0; else if (s > surf->extents[0]) s = surf->extents[0];
		int t = local[1] + 0.5; if (t < 0) t = 0; else if (t > surf->extents[1]) t = surf->extents[1];

		s = local[0] - s;
		t = local[1] - t;

		// compare to minimum light
		if ((s * s + t * t + dist * dist) < maxdist)
		{
			// mark the dlight for this surf
			if (surf->dlightframe != d3d_LightGlobals.dlightframecount)
			{
				if (bspmodel)
				{
					// it's a BSP model surf so force a full light update so that reused surfs will be handled properly
					// this sucks but it's no worse than we were before and at least the world will get scaled back updates
					surf->LightProperties = ~QLIGHTMAP::LightProperty;
				}

				// first time hit
				surf->dlightbits[num >> 3] = (1 << (num & 7));
				surf->dlightframe = d3d_LightGlobals.dlightframecount;
			}
			else surf->dlightbits[num >> 3] |= (1 << (num & 7));
		}
	}

	if (node->children[0]->contents >= 0) R_MarkLights (light, num, node->children[0], bspmodel);
	if (node->children[1]->contents >= 0) R_MarkLights (light, num, node->children[1], bspmodel);
}


void D3DLight_ClearDynamics (msurface_t *surf)
{
	memset (surf->dlightbits, 0, sizeof (surf->dlightbits));
	surf->dlightframe = -1;
}


/*
=============
D3DLight_PushDynamics
=============
*/
void D3DLight_NewDynamicFrame (void)
{
	// because we're now able to dynamically light BSP models too (yayy!) we need to use a framecount per-entity to
	// force an update of lighting from a previous time this model may have been used in the current frame
	d3d_LightGlobals.dlightframecount++;
}


void D3DLight_PushDynamics (entity_t *ent, mnode_t *headnode)
{
	if (!ent) return;
	if (!ent->model) return;
	if (!headnode) return;
	if (!(r_dynamic.value > 0)) return;
	if (d3d_LightGlobals.CoronaState == CORONA_ONLY) return;

	dlight_t *dl = cls.dlights;

	for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time || dl->radius <= 0.0f) continue;
		if (dl->visframe != d3d_RenderDef.framecount) continue;

		// transform the light into the correct space for the entity
		if (ent->angles[0] || ent->angles[1] || ent->angles[2])
			ent->invmatrix.TransformPoint (dl->transformed, dl->origin);
		else if (ent->origin[0] || ent->origin[1] || ent->origin[2])
		{
			dl->transformed[0] = dl->origin[0] - ent->origin[0];
			dl->transformed[1] = dl->origin[1] - ent->origin[1];
			dl->transformed[2] = dl->origin[2] - ent->origin[2];
		}
		else Vector3Copy (dl->transformed, dl->origin);

		// and now we can mark the lights
		R_MarkLights (dl, i, headnode, (ent->model->brushhdr->bspmodel && ent->model->numents > 1));
	}
}


void D3DLight_BeginFrame (void)
{
	dlight_t *dl = cls.dlights;

	if (gl_flashblend.value)
	{
		// override everything else, if it's set then we're in coronas-only mode irrespective
		d3d_LightGlobals.CoronaState = CORONA_ONLY;
	}
	else
	{
		// can't use .integer for DP-compatibility
		if (r_coronas.value)
			d3d_LightGlobals.CoronaState = CORONA_PLUS_LIGHTMAP;
		else d3d_LightGlobals.CoronaState = LIGHTMAP_ONLY;
	}

	// now check out and cull the dlights
	for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time || dl->radius <= 0.0f) continue;
		if (R_CullSphere (dl->origin, dl->radius, 31)) continue;

		// light is unculled this frame...
		dl->visframe = d3d_RenderDef.framecount;

		// check to add coronas
		if (dl->flags & DLF_NOCORONA) continue;
		if (d3d_LightGlobals.CoronaState == LIGHTMAP_ONLY) continue;

		// and add a corona for this light
		D3DAlpha_AddToList (dl);
	}

	// the lightmap is always bound to slot 6
	d3d11_State->PSSetShaderResourceView (6, QLIGHTMAP::SRV);
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

void D3DLight_FromSurface (lightinfo_t *info)
{
	// no lightmap
	if (!info->lightsurf->samples) return;

	msurface_t *surf = info->lightsurf;
	float *shadelight = info->shadelight;
	byte *lightmap = surf->samples + ((info->dt >> 4) * surf->smax + (info->ds >> 4)) * 3;

	for (int maps = 0; maps < MAX_SURFACE_STYLES && surf->styles[maps] != 255; maps++)
	{
		// keep this consistent with BSP lighting
		float scale = (float) d3d_LightGlobals.StyleValue[surf->styles[maps]];

		shadelight[0] += (float) lightmap[0] * scale;
		shadelight[1] += (float) lightmap[1] * scale;
		shadelight[2] += (float) lightmap[2] * scale;

		lightmap += surf->smax * surf->tmax * 3;
	}
}


bool D3DLight_RecursiveLightPoint (lightinfo_t *info, mnode_t *node, float *start, float *end)
{
loc0:;
	// didn't hit anything
	if (node->contents < 0) return false;

	// calculate mid point
	float front = Mod_PlaneDist (node->plane, start);
	float back = Mod_PlaneDist (node->plane, end);

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
	{
		node = node->children[front < 0];
		goto loc0;
	}

	float frac = front / (front - back);

	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;

	float mid[] =
	{
		start[0] + frac * (end[0] - start[0]),
		start[1] + frac * (end[1] - start[1]),
		start[2] + frac * (end[2] - start[2])
	};

	// go down front side
	if (D3DLight_RecursiveLightPoint (info, node->children[front < 0], start, mid))
	{
		// hit something
		return true;
	}
	else
	{
		int i;
		msurface_t *surf;

		// check for impact on this node
		for (i = 0, surf = node->surfaces; i < node->numsurfaces; i++, surf++)
		{
			// no lightmaps
			if (surf->flags & SURF_DRAWSKY) continue;
			if (surf->flags & SURF_DRAWTURB) continue;

			int ds = (int) ((float) Vector3Dot (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
			int dt = (int) ((float) Vector3Dot (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

			// out of range
			if (ds < surf->texturemins[0] || dt < surf->texturemins[1]) continue;

			ds -= surf->texturemins[0];
			dt -= surf->texturemins[1];

			// out of range
			if (ds > surf->extents[0] || dt > surf->extents[1]) continue;

			// store out the surf that was hit
			Vector3Copy (info->lightspot, mid);
			info->lightsurf = surf;
			info->lightplane = node->plane;
			info->ds = ds;
			info->dt = dt;

			// and get the lighting
			D3DLight_FromSurface (info);

			// success
			return true;
		}

		// go down back side
		return D3DLight_RecursiveLightPoint (info, node->children[front >= 0], mid, end);
	}
}


void R_MinimumLight (float *c, float factor)
{
	float add = factor - (c[0] + c[1] + c[2]);

	if (add > 0.0f)
	{
		c[0] += add / 3.0f;
		c[1] += add / 3.0f;
		c[2] += add / 3.0f;
	}
}


void D3DLight_ClearInfo (lightinfo_t *info)
{
	// because r_shadows needs lightplane it must be explicitly NULL
	info->lightplane = NULL;
	info->lightsurf = NULL;
}


cvar_t r_gunminimumlight ("r_gunminimumlight", "72", CVAR_ARCHIVE);
cvar_t r_playerminimumlight ("r_playerminimumlight", "24", CVAR_ARCHIVE);
cvar_t r_pickupminimumlight ("r_pickupminimumlight", "72", CVAR_ARCHIVE);


#define LP_STATICENT	1
#define LP_VIEWENT		2
#define LP_PLAYERENT	4
#define LP_ROTATEENT	8
#define LP_FULLBRIGHT	16


void D3DLight_LightPointPoints (float *origin, float *start, float *end)
{
	// extend the end-point just beyond the map bounds so that it's sure to always catch something
	Vector3Copy (start, origin);
	Vector3Set (end, origin[0], origin[1], cl.worldmodel->mins[2] - 10.0f);

	// this can happen if noclipping
	if (end[2] >= start[2]) end[2] = start[2] - 10.0f;
}


void D3DLight_LightPoint (lightinfo_t *info, float *origin)
{
	// keep MDL lighting consistent with the world
	if (r_fullbright.integer || !cl.worldmodel->brushhdr->lightdata)
	{
		// needs braces because this macro has them too...
		Vector3Set (info->shadelight, 32767, 32767, 32767);
	}
	else
	{
		// clear to ambient
		float ambient = (cl.maxclients > 1 || r_ambient.integer < 1) ? 0 : (r_ambient.integer * 128);

		Vector3Set (info->shadelight, ambient, ambient, ambient);

		// add lighting from lightmaps
		if ((info->flags & LP_STATICENT) && info->lightsurf)
			D3DLight_FromSurface (info);
		else if (!(info->flags & LP_STATICENT))
		{
			vec3_t start;
			vec3_t end;

			D3DLight_ClearInfo (info);
			D3DLight_LightPointPoints (origin, start, end);
			D3DLight_RecursiveLightPoint (info, cl.worldmodel->brushhdr->nodes, start, end);
		}

		// add dynamic lights
		if (r_dynamic.value && (d3d_LightGlobals.CoronaState != CORONA_ONLY))
		{
			dlight_t *dl = cls.dlights;
			vec3_t dist;

			for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++, dl++)
			{
				if (dl->die < cl.time) continue;
				if (dl->radius <= 0) continue;

				Vector3Subtract (dist, origin, dl->origin);

				float rad = dl->radius;
				float fdist = Vector3Length (dist);

				if ((rad = rad - fabs (fdist)) < dl->minlight) continue;

				float minlight = rad - dl->minlight;
				float ladd = (minlight - fdist) * r_dynamic.value * 0.5f;

				if (ladd > 0)
				{
					info->shadelight[0] += (ladd * dl->rgb[0]);
					info->shadelight[1] += (ladd * dl->rgb[1]);
					info->shadelight[2] += (ladd * dl->rgb[2]);
				}
			}
		}
	}

	// scale back to standard range
	Vector3Recip (info->shadelight, info->shadelight, 255.0f);

	// nehahra assumes that fullbrights are not available in the engine
	// note that this still needs to go through the full rigmarole to get the lightplane for fucking shadows
	if ((nehahra || !gl_fullbrights.integer) && (info->flags & LP_FULLBRIGHT))
		Vector3Set (info->shadelight, 255, 255, 255);

	if (!r_overbright.integer || nehahra)
	{
		Vector3Scale (info->shadelight, info->shadelight, 2.0f);
		Vector3Clamp (info->shadelight, 255.0f);
		Vector3Scale (info->shadelight, info->shadelight, 0.5f);
	}

	if (!r_coloredlight.integer)
	{
		float white[3] = {0.299f, 0.587f, 0.114f};
		info->shadelight[0] = info->shadelight[1] = info->shadelight[2] = Vector3Dot (info->shadelight, white);
	}

	// set minimum light values
	if (info->flags & LP_VIEWENT) R_MinimumLight (info->shadelight, r_gunminimumlight.value);
	if (info->flags & LP_PLAYERENT) R_MinimumLight (info->shadelight, r_playerminimumlight.value);
	if (info->flags & LP_ROTATEENT) R_MinimumLight (info->shadelight, r_pickupminimumlight.value);

	// take to final range
	Vector3Scale (info->shadelight, info->shadelight, r_lightscale.value / 255.0f);
}


void D3DLight_SetLightPointFlags (entity_t *ent)
{
	ent->lightinfo.flags = 0;

	if (ent->isStatic) ent->lightinfo.flags |= LP_STATICENT;
	if (ent == &cl.viewent) ent->lightinfo.flags |= LP_VIEWENT;
	if (ent->entnum >= 1 && ent->entnum <= cl.maxclients) ent->lightinfo.flags |= LP_PLAYERENT;
	if (ent->model->flags & EF_ROTATE) ent->lightinfo.flags |= LP_ROTATEENT;
	if (ent->model->aliashdr && (ent->model->aliashdr->drawflags & AM_FULLBRIGHT)) ent->lightinfo.flags |= LP_FULLBRIGHT;
}


void D3DLight_PrepStaticEntityLighting (entity_t *ent)
{
	D3DLight_ClearInfo (&ent->lightinfo);

	if (cl.worldmodel->brushhdr->lightdata)
	{
		vec3_t start;
		vec3_t end;

		D3DLight_LightPointPoints (ent->origin, start, end);
		D3DLight_RecursiveLightPoint (&ent->lightinfo, cl.worldmodel->brushhdr->nodes, start, end);
	}

	// mark as static
	ent->isStatic = true;

	// hack - this only ever gets called from parsestatic and needs to be reset to get new statics on the list
	d3d_RenderDef.rebuildworld = true;
}


/*
====================================================================================================================

		LIGHTMAP ALLOCATION AND UPDATING

====================================================================================================================
*/

void D3DLight_UpdateLightmaps (void)
{
	// we need to update any modified lightmaps before we can draw so do it now
	for (int i = 0; i < QLIGHTMAP::NumLightmaps; i++)
		QLIGHTMAP::Lightmaps[i].Update (i);
}


/*
===============
D3DLight_AddDynamics
===============
*/
bool D3DLight_AddDynamics (msurface_t *surf, unsigned *dest, bool forcedirty)
{
	mtexinfo_t *tex = surf->texinfo;
	float dynamic = r_dynamic.value;
	bool updated = false;

	if (!(r_dynamic.value > 0)) return false;
	if (d3d_LightGlobals.CoronaState == CORONA_ONLY) return false;

	dlight_t *dl = cls.dlights;

	for (int lnum = 0; lnum < MAX_DLIGHTS; lnum++, dl++)
	{
		// light is dead or has no radius
		if (dl->die < cl.time) continue;
		if (dl->radius <= 0) continue;

		// if the dlight is not dirty it doesn't need to be updated unless the surf was otherwise updated (in which case it does)
		if (!dl->dirty && !forcedirty) continue;

		// not hit by this light
		if (!(surf->dlightbits[lnum >> 3] & (1 << (lnum & 7)))) continue;

		float rad = dl->radius;
		float dist = Mod_PlaneDist (surf->plane, dl->transformed);

		rad -= fabs (dist);

		if (rad < dl->minlight) continue;

		float minlight = rad - dl->minlight;

		float impact[] =
		{
			dl->transformed[0] - surf->plane->normal[0] * dist,
			dl->transformed[1] - surf->plane->normal[1] * dist,
			dl->transformed[2] - surf->plane->normal[2] * dist
		};

		float local[] =
		{
			(Vector3Dot (impact, tex->vecs[0]) + tex->vecs[0][3]) - surf->texturemins[0],
			(Vector3Dot (impact, tex->vecs[1]) + tex->vecs[1][3]) - surf->texturemins[1]
		};

		// prevent this multiplication from having to happen for each point
		float dlrgb[] =
		{
			(float) dl->rgb[0] * dynamic,
			(float) dl->rgb[1] * dynamic,
			(float) dl->rgb[2] * dynamic
		};

		unsigned *blocklights = dest;
		int sd, td;

		for (int t = 0, ftacc = 0; t < surf->tmax; t++, ftacc += 16)
		{
			if ((td = Q_ftol (local[1] - ftacc)) < 0) td = -td;

			for (int s = 0, fsacc = 0; s < surf->smax; s++, fsacc += 16, blocklights += 3)
			{
				if ((sd = Q_ftol (local[0] - fsacc)) < 0) sd = -sd;

				if (sd > td)
					dist = sd + (td >> 1);
				else dist = td + (sd >> 1);

				int ladd = (minlight - dist);

				if (ladd > 0)
				{
					blocklights[0] += ladd * dlrgb[0];
					blocklights[1] += ladd * dlrgb[1];
					blocklights[2] += ladd * dlrgb[2];

					// the light is updated now
					updated = true;
				}
			}
		}
	}

	D3DLight_ClearDynamics (surf);

	return updated;
}


void D3DLight_ClearToBase (unsigned *blocklights, int len, int baselight)
{
	__asm
	{
		mov eax, dword ptr [baselight]
		mov ecx, dword ptr [len]
		mov edi, dword ptr [blocklights]
		rep stosd
	}
}


void D3DLight_SetScaledLight (unsigned *blocklights, int len, byte *lightmap, int scale, int baselight)
{
	__asm
	{
		mov edi, dword ptr [blocklights]
		mov esi, dword ptr [lightmap]
		mov ebx, dword ptr [scale]
		mov edx, dword ptr [baselight]
		xor ecx, ecx
Loop0:
		cmp ecx, dword ptr [len]
		jge LoopDone

		movzx eax, byte ptr [esi + 0]
		imul eax, ebx
		add eax, edx
		mov dword ptr [edi + 0], eax

		movzx eax, byte ptr [esi + 1]
		imul eax, ebx
		add eax, edx
		mov dword ptr [edi + 4], eax

		movzx eax, byte ptr [esi + 2]
		imul eax, ebx
		add eax, edx
		mov dword ptr [edi + 8], eax

		add edi, 12
		add esi, 3
		add ecx, 3
		jmp Loop0
LoopDone:
	}
}


void D3DLight_AddScaledLight (unsigned *blocklights, int len, byte *lightmap, int scale)
{
	__asm
	{
		mov edi, dword ptr [blocklights]
		mov esi, dword ptr [lightmap]
		mov ebx, dword ptr [scale]
		xor ecx, ecx
Loop0:
		cmp ecx, dword ptr [len]
		jge LoopDone

		movzx eax, byte ptr [esi + 0]
		imul eax, ebx
		add eax, dword ptr [edi + 0]
		mov dword ptr [edi + 0], eax

		movzx eax, byte ptr [esi + 1]
		imul eax, ebx
		add eax, dword ptr [edi + 4]
		mov dword ptr [edi + 4], eax

		movzx eax, byte ptr [esi + 2]
		imul eax, ebx
		add eax, dword ptr [edi + 8]
		mov dword ptr [edi + 8], eax

		add edi, 12
		add esi, 3
		add ecx, 3
		jmp Loop0
LoopDone:
	}
}


void D3DLight_GreyScaleMap (unsigned *blocklights, int len)
{
	for (int i = 0; i < len; i += 3, blocklights += 3)
	{
		// 0.299f, 0.587f, 0.114f
		// convert lighting from RGB to greyscale using a bigger scale to preserve precision
		int t = ((blocklights[0] * 306) + (blocklights[1] * 601) + (blocklights[2] * 117)) >> 10;

		blocklights[0] = blocklights[1] = blocklights[2] = t;
	}
}


void D3DLight_WriteHDRMap (unsigned *dest, int stride, unsigned *blocklights, int smax, int tmax)
{
	for (int i = 0; i < tmax; i++)
	{
		for (int j = 0; j < smax; j++, blocklights += 3)
		{
			// this generates fewer and faster asm instructions
			register int r = blocklights[0];
			register int g = blocklights[1];
			register int b = blocklights[2];
			register int maxl = r ^ ((r ^ g) & -(r < g));

			// less instructions, less branchy
			if ((maxl = maxl ^ ((maxl ^ b) & -(maxl < b))) > 0x7fff)
			{
				maxl /= 254;
				dest[j] = ((0x7f80 / maxl) << 24) | (r / maxl) | ((g / maxl) << 8) | ((b / maxl) << 16);
			}
			else dest[j] = (255 << 24) | (r >> 7) | ((g >> 7) << 8) | ((b >> 7) << 16);
		}

		dest += stride;
	}
}


void D3DLight_WriteLightMap (unsigned *dest, int stride, unsigned *blocklights, int smax, int tmax)
{
	// allows the same shader to be used with all modes
	int alpha = (r_overbright.integer && !nehahra) ? 128 : 255;
	int shift = (r_overbright.integer && !nehahra) ? 8 : 7;

	for (int i = 0; i < tmax; i++)
	{
		for (int j = 0; j < smax; j++, blocklights += 3)
		{
			int r = (blocklights[0] >> shift) - 255; r = (r & (r >> 31)) + 255;
			int g = (blocklights[1] >> shift) - 255; g = (g & (g >> 31)) + 255;
			int b = (blocklights[2] >> shift) - 255; b = (b & (b >> 31)) + 255;

			dest[j] = (alpha << 24) | (r << 0) | (g << 8) | (b << 16);
		}

		dest += stride;
	}
}


void D3DLight_BuildLightmap (msurface_t *surf, QLIGHTMAP *lm)
{
	int size = surf->smax * surf->tmax * 3;
	int hunkmark = TempHunk->GetLowMark ();
	unsigned int *lightblock = (unsigned int *) TempHunk->FastAlloc (size * sizeof (unsigned int));
	bool updated = false;

	// recache properties here because adding dynamic lights may uncache them
	if (surf->LightProperties != QLIGHTMAP::LightProperty)
	{
		surf->LightProperties = QLIGHTMAP::LightProperty;
		updated = true;
	}

	// eval base lighting for this surface
	int baselight = 0;

	if (r_fullbright.integer || !surf->model->brushhdr->lightdata)
		baselight = 32767;
	else if (cl.maxclients > 1 || r_ambient.integer < 1)
		baselight = 0;
	else baselight = r_ambient.integer << 7;

	if (!r_fullbright.integer && surf->model->brushhdr->lightdata)
	{
		byte *lightmap = NULL;

		if ((lightmap = surf->samples) != NULL)
		{
			for (int maps = 0; maps < MAX_SURFACE_STYLES && surf->styles[maps] != 255; maps++)
			{
				int scale = d3d_LightGlobals.StyleValue[surf->styles[maps]];

				// avoid an additional pass over the light data by initializing it on the first map
				if (maps == 0 && scale > 0)
					D3DLight_SetScaledLight (lightblock, size, lightmap, scale, baselight);
				else if (maps == 0)
					D3DLight_ClearToBase (lightblock, size, baselight);
				else if (scale > 0)
					D3DLight_AddScaledLight (lightblock, size, lightmap, scale);

				// go to the next lightmap
				lightmap += size;

				// recache current style
				if (surf->cached_light[maps] != d3d_LightGlobals.StyleValue[surf->styles[maps]])
				{
					surf->cached_light[maps] = d3d_LightGlobals.StyleValue[surf->styles[maps]];
					updated = true;
				}
			}
		}
		else D3DLight_ClearToBase (lightblock, size, baselight);

		if (surf->dlightframe == d3d_LightGlobals.dlightframecount)
		{
			// add all the dynamic lights (don't add if r_fullbright or no lightdata...)
			if (D3DLight_AddDynamics (surf, lightblock, updated))
			{
				// and dirty the properties to force an update next frame in order to clear the light
				surf->LightProperties = ~QLIGHTMAP::LightProperty;
				updated = true;
			}
		}
	}
	else D3DLight_ClearToBase (lightblock, size, baselight);

	// clear dynamic lighting so that the surf won't subsequently keep being updated even if it's been subsequently hit by no lights
	// this is always done so that nothing is left hanging over from a previous frame
	D3DLight_ClearDynamics (surf);

	if (!updated)
	{
		TempHunk->FreeToLowMark (hunkmark);
		return;
	}

	// get a mapping if we need to
	unsigned int *dest = lm->BoxTexels (&surf->LightBox);

	if (!dest)
	{
		// dirty the surface properties so that the mapping will be tried again next time
		surf->LightProperties = ~QLIGHTMAP::LightProperty;
		TempHunk->FreeToLowMark (hunkmark);
		return;
	}

	// standard lighting
	if (!r_coloredlight.integer)
		D3DLight_GreyScaleMap (lightblock, size);

	if (r_overbright.integer && r_hdrlight.integer && !nehahra)
		D3DLight_WriteHDRMap (dest, LIGHTMAP_SIZE, lightblock, surf->smax, surf->tmax);
	else D3DLight_WriteLightMap (dest, LIGHTMAP_SIZE, lightblock, surf->smax, surf->tmax);

	lm->ExpandBox (&surf->LightBox);
	TempHunk->FreeToLowMark (hunkmark);
}


bool D3DLight_AllocBlock (int w, int h, UINT *x, UINT *y)
{
	int		i, j;
	int		best, best2;

	best = LIGHTMAP_SIZE;

	for (i = 0; i < LIGHTMAP_SIZE - w; i++)
	{
		best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (QLIGHTMAP::Allocated[i + j] >= best) break;
			if (QLIGHTMAP::Allocated[i + j] > best2) best2 = QLIGHTMAP::Allocated[i + j];
		}

		if (j == w)
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > LIGHTMAP_SIZE)
		return false;

	for (i = 0; i < w; i++)
		QLIGHTMAP::Allocated[*x + i] = best + h;

	return true;
}


void D3DLight_BuildAllLightmaps (void)
{
	// release all lightmap textures
	D3DLight_ClearLightmaps ();

	// set up the default styles so that (most) lightmaps won't have to be regenerated the first time they're seen
	R_SetDefaultLightStyles ();

	// run an initial light animation to get the rest of the styles on entry
	D3DLight_AnimateLight (0);

	// get the initial hunk mark so that we can do the allocation buffers
	int hunkmark = TempHunk->GetLowMark ();

	// initialize block allocation
	QLIGHTMAP::Allocated = (unsigned short *) TempHunk->Alloc (LIGHTMAP_SIZE * sizeof (unsigned short));

	for (int j = 1; j < MAX_MODELS; j++)
	{
		model_t *mod;

		if (!(mod = cl.model_precache[j])) break;
		if (mod->type != mod_brush) continue;

		if (mod->name[0] == '*')
		{
			mod->subtype = mod_inline;
			continue;
		}

		// identify the model type
		if (mod == cl.worldmodel)
			mod->subtype = mod_world;
		else mod->subtype = mod_bsp;

		// catch null models
		if (!mod->brushhdr) continue;
		if (!mod->brushhdr->numsurfaces) continue;

		brushhdr_t *hdr = mod->brushhdr;
		hunkmark = TempHunk->GetLowMark ();

		for (int i = 0; i < hdr->numsurfaces; i++)
		{
			msurface_t *surf = &hdr->surfaces[i];

			if (surf->flags & SURF_DRAWSKY) continue;
			if (surf->flags & SURF_DRAWTURB) continue;

			// store these out so that we don't have to recalculate them every time
			surf->smax = (surf->extents[0] >> 4) + 1;
			surf->tmax = (surf->extents[1] >> 4) + 1;

			if (!D3DLight_AllocBlock (surf->smax, surf->tmax, &surf->LightBox.left, &surf->LightBox.top))
			{
				// go to a new block
				if ((++QLIGHTMAP::NumLightmaps) >= MAX_LIGHTMAPS)
				{
					Sys_Error ("D3DLight_CreateSurfaceLightmaps : MAX_LIGHTMAPS exceeded");
					return;
				}

				memset (QLIGHTMAP::Allocated, 0, LIGHTMAP_SIZE * sizeof (unsigned short));

				if (!D3DLight_AllocBlock (surf->smax, surf->tmax, &surf->LightBox.left, &surf->LightBox.top))
				{
					Sys_Error ("D3DLight_CreateSurfaceLightmaps : consecutive calls to D3DLight_AllocBlock failed");
					return;
				}
			}

			// fill in lightmap right and bottom (these exist just because I'm lazy and don't want to add a few numbers during updates)
			surf->LightBox.right = surf->LightBox.left + surf->smax;
			surf->LightBox.bottom = surf->LightBox.top + surf->tmax;

			// create our staging texture if we need to
			QLIGHTMAP::Lightmaps[QLIGHTMAP::NumLightmaps].AllocTexels ();

			// ensure no dlight update happens and rebuild the lightmap fully
			D3DLight_ClearDynamics (surf);

			// initially assign these
			surf->LightmapTextureNum = QLIGHTMAP::NumLightmaps;

			// the surf initially has invalid properties set which forces the lightmap to be built here
			surf->LightProperties = ~QLIGHTMAP::LightProperty;

			// also invalidate the light cache to force a recache at the correct values on build
			surf->cached_light[0] = surf->cached_light[1] = surf->cached_light[2] = surf->cached_light[3] = -1;

			// and build the map
			D3DLight_BuildLightmap (surf, &QLIGHTMAP::Lightmaps[surf->LightmapTextureNum]);
		}
	}

	// any future attempts to access this should crash
	QLIGHTMAP::Allocated = NULL;
	QLIGHTMAP::NumLightmaps++;

	// create the texture
	D3D11_TEXTURE2D_DESC *desc = QTEXTURE::MakeTextureDesc (LIGHTMAP_SIZE, LIGHTMAP_SIZE, IMAGE_UPDATE);

	desc->ArraySize = QLIGHTMAP::NumLightmaps;
	desc->BindFlags = D3D11_BIND_SHADER_RESOURCE;

	// and create them all - the first frame will load what lightmaps are needed
	d3d11_Device->CreateTexture2D (desc, NULL, &QLIGHTMAP::Texture);
	d3d11_Device->CreateShaderResourceView (QLIGHTMAP::Texture, NULL, &QLIGHTMAP::SRV);

	// run an initial update of all lightmaps so that it won't need to be done first time
	for (int i = 0; i < QLIGHTMAP::NumLightmaps; i++)
		QLIGHTMAP::Lightmaps[i].Update (i);

	// and done
	TempHunk->FreeToLowMark (hunkmark);
}


void D3DLight_CheckSurfaceForModification (msurface_t *surf)
{
	// no lightmaps
	if (surf->flags & SURF_DRAWSKY) return;
	if (surf->flags & SURF_DRAWTURB) return;

	// disable dynamic lights on this surf in coronas-only mode (don't set cached to false here as
	// we want to clear any previous dlights; D3DLight_PushDynamics will look after not adding more for us)
	if (d3d_LightGlobals.CoronaState == CORONA_ONLY)
		surf->dlightframe = ~d3d_LightGlobals.dlightframecount;

	// check for lighting parameter modification
	if (surf->LightProperties != QLIGHTMAP::LightProperty)
	{
		D3DLight_BuildLightmap (surf, &QLIGHTMAP::Lightmaps[surf->LightmapTextureNum]);
		return;
	}

	// note - r_dynamic sets to median light so we must still check the styles as they may need to change from non-median.
	// we must also clear any previously added dlights; not adding new dlights is handled in the dlight adding functions.
	// dynamic light this frame
	if (surf->dlightframe == d3d_LightGlobals.dlightframecount)
	{
		D3DLight_BuildLightmap (surf, &QLIGHTMAP::Lightmaps[surf->LightmapTextureNum]);
		return;
	}

	// cached lightstyle change
	for (int maps = 0; maps < MAX_SURFACE_STYLES && surf->styles[maps] != 255; maps++)
	{
		if (surf->cached_light[maps] != d3d_LightGlobals.StyleValue[surf->styles[maps]])
		{
			// Con_Printf ("Modified light from %i to %i\n", surf->cached_light[maps], d3d_LightGlobals.StyleValue[surf->styles[maps]]);
			D3DLight_BuildLightmap (surf, &QLIGHTMAP::Lightmaps[surf->LightmapTextureNum]);
			return;
		}
	}
}


