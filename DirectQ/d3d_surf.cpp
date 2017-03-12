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
#include "d3d_quake.h"
#include "resource.h"


void D3DSurf_ResetDetailTextures (cvar_t *var)
{
	// because detail textures use a gs, reset all state if they're enabled/disabled
	D3DVid_FlushStates ();
}


cvar_t r_chainfronttoback ("r_chainfronttoback", 1.0f, CVAR_ARCHIVE);
cvar_t r_mergebmodels ("r_mergebmodels", 1.0f, CVAR_ARCHIVE);
cvar_t r_detailtextures ("r_detailtextures", 0.0f, CVAR_ARCHIVE, D3DSurf_ResetDetailTextures);

extern cvar_t r_lightscale;

QEDICTLIST d3d_BrushEdicts;
QEDICTLIST d3d_MergeEdicts;

#define INDEX_USHORT_CUTOFF		65535
#define MIN_BRUSH_INDEXES		4096

struct surfconstants_t
{
	QMATRIX ModelMatrix;
	float surfcolour[4];
};

struct brushpolyvert_t
{
	float xyz[3];
	float st[2];
	float lm[3];
};

struct d3d_BrushState_t
{
	int LoadVertexes;

	DXGI_FORMAT IndexFormat;
	int IndexSize;

	int CurrentAlpha;
	entity_t *CurrentEnt;
	texture_t *CurrentTexture;

	int FirstIndex;
	int MaxIndexes;
};


d3d_BrushState_t d3d_BrushState;

void Mod_RecalcNodeBBox (mnode_t *node);
void Mod_CalcBModelBBox (model_t *mod, brushhdr_t *hdr);

void D3DSurf_AccumulateSurface (msurface_t *surf)
{
	d3d_BrushState.LoadVertexes += surf->numvertexes;
}


template <typename index_t> void D3DSurf_BuildPolygon (brushhdr_t *hdr, msurface_t *surf, brushpolyvert_t *verts)
{
	for (int v = 0; v < surf->numvertexes; v++, verts++)
	{
		int lindex = hdr->dsurfedges[surf->firstedge + v];

		if (lindex > 0)
		{
			verts->xyz[0] = hdr->dvertexes[hdr->edges[lindex].v[0]].point[0];
			verts->xyz[1] = hdr->dvertexes[hdr->edges[lindex].v[0]].point[1];
			verts->xyz[2] = hdr->dvertexes[hdr->edges[lindex].v[0]].point[2];
		}
		else
		{
			verts->xyz[0] = hdr->dvertexes[hdr->edges[-lindex].v[1]].point[0];
			verts->xyz[1] = hdr->dvertexes[hdr->edges[-lindex].v[1]].point[1];
			verts->xyz[2] = hdr->dvertexes[hdr->edges[-lindex].v[1]].point[2];
		}

		if (surf->flags & SURF_DRAWTURB)
		{
			verts->st[0] = Vector3Dot (verts->xyz, surf->texinfo->vecs[0]);
			verts->st[1] = Vector3Dot (verts->xyz, surf->texinfo->vecs[1]);
		}
		else if (!(surf->flags & SURF_DRAWSKY))
		{
			float st[2] =
			{
				(Vector3Dot (verts->xyz, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]),
				(Vector3Dot (verts->xyz, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3])
			};

			verts->st[0] = st[0] / surf->texinfo->texture->size[0];
			verts->st[1] = st[1] / surf->texinfo->texture->size[1];

			verts->lm[0] = ((st[0] - surf->texturemins[0]) + (surf->LightBox.left * 16) + 8) / (float) (LIGHTMAP_SIZE * 16);
			verts->lm[1] = ((st[1] - surf->texturemins[1]) + (surf->LightBox.top * 16) + 8) / (float) (LIGHTMAP_SIZE * 16);
			verts->lm[2] = surf->LightmapTextureNum;
		}
	}

	index_t *ndx = (index_t *) MainHunk->FastAlloc (surf->numindexes * sizeof (index_t));

	surf->indexes = ndx;

	for (int v = 2; v < surf->numvertexes; v++, ndx += 3)
	{
		ndx[0] = surf->firstvertex;
		ndx[1] = surf->firstvertex + v - 1;
		ndx[2] = surf->firstvertex + v;
	}
}


void D3DSurf_AddToTextureChain (msurface_t *surf, texture_t *tex)
{
	if (r_chainfronttoback.value)
	{
		*tex->texturechain_tail = surf;
		tex->texturechain_tail = &surf->texturechain;
		surf->texturechain = NULL;
	}
	else
	{
		surf->texturechain = tex->texturechain;
		tex->texturechain = surf;
	}

	tex->numindexes += surf->numindexes;
}


void D3DSurf_ClearTextureChain (texture_t *tex)
{
	tex->texturechain = NULL;
	tex->texturechain_tail = &tex->texturechain;
	tex->numindexes = 0;
}


ID3D11Buffer *d3d_SurfIndexes = NULL;
ID3D11Buffer *d3d_SurfConstants = NULL;
ID3D11Buffer *d3d_SurfVertexes = NULL;

ID3D11InputLayout *d3d_SurfLayout = NULL;
ID3D11VertexShader *d3d_SurfVertexShader[2];
ID3D11PixelShader *d3d_SurfPixelShader[16];
ID3D11GeometryShader *d3d_SurfGeometryShader;

void D3DBrush_Shutdown (void)
{
	SAFE_RELEASE (d3d_SurfIndexes);
	SAFE_RELEASE (d3d_SurfVertexes);
}


void D3DBrush_ClearLoadData (brushhdr_t *hdr)
{
	// any further attempts to access these are errors
	hdr->edges = NULL;
	hdr->dsurfedges = NULL;
	hdr->dvertexes = NULL;
}


void D3DBrush_Init (void)
{
	// ensure because this gets called at map load as well as at vid_restart
	D3DBrush_Shutdown ();

	// now build the surfaces for real
	if (!d3d_BrushState.LoadVertexes) return;

	// we need the index format up-front so that we can know how much space to allocate
	if (d3d_BrushState.LoadVertexes < INDEX_USHORT_CUTOFF)
	{
		d3d_BrushState.IndexFormat = DXGI_FORMAT_R16_UINT;
		d3d_BrushState.IndexSize = 2;
	}
	else
	{
		d3d_BrushState.IndexFormat = DXGI_FORMAT_R32_UINT;
		d3d_BrushState.IndexSize = 4;
	}

	// verts are only needed while loading; once loaded they can be discarded
	int hunkmark = TempHunk->GetLowMark ();
	brushpolyvert_t *verts = (brushpolyvert_t *) TempHunk->FastAlloc (d3d_BrushState.LoadVertexes * sizeof (brushpolyvert_t));
	d3d_BrushState.LoadVertexes = 0;

	int numindexes = 0;
	int numvertexes = 0;
	model_t *mod = NULL;

	for (int j = 1; j < MAX_MODELS; j++)
	{
		if (!(mod = cl.model_precache[j])) break;
		if (mod->type != mod_brush) continue;

		// catch null models
		if (!mod->brushhdr) continue;
		if (!mod->brushhdr->numsurfaces) continue;

		brushhdr_t *hdr = mod->brushhdr;

		if (mod->name[0] == '*')
		{
			// the model has already had it's surfs calced so just recalc it's bbox
			Mod_CalcBModelBBox (mod, hdr);
			D3DBrush_ClearLoadData (hdr);
			hdr->bspmodel = false;
			continue;
		}

		// this also sets to true for the world but we'll fix that up later
		hdr->bspmodel = true;

		// init texture chains for the model
		for (int i = 0; i < hdr->numtextures; i++)
		{
			texture_t *tex = NULL;

			if (!(tex = hdr->textures[i])) continue;

			D3DSurf_ClearTextureChain (tex);
		}

		// chain the surfaces in texture order
		for (int i = 0; i < hdr->numsurfaces; i++)
			D3DSurf_AddToTextureChain (&hdr->surfaces[i], hdr->surfaces[i].texinfo->texture);

		// and build them from the chains
		for (int i = 0; i < hdr->numtextures; i++)
		{
			msurface_t *surf = NULL;
			texture_t *tex = NULL;

			if (!(tex = hdr->textures[i])) continue;
			if (!(surf = tex->texturechain)) continue;

			for (; surf; surf = surf->texturechain)
			{
				surf->firstvertex = numvertexes;
				numvertexes += surf->numvertexes;
				numindexes += surf->numindexes;

				if (d3d_BrushState.IndexFormat == DXGI_FORMAT_R16_UINT)
					D3DSurf_BuildPolygon<unsigned short> (hdr, surf, &verts[surf->firstvertex]);
				else D3DSurf_BuildPolygon<unsigned int> (hdr, surf, &verts[surf->firstvertex]);
			}

			// and reset the chain when done
			D3DSurf_ClearTextureChain (tex);
		}

		D3DBrush_ClearLoadData (hdr);
		Mod_RecalcNodeBBox (hdr->nodes);
		Mod_CalcBModelBBox (mod, hdr);
	}

	// set a minimum index count
	numindexes = (numindexes > MIN_BRUSH_INDEXES ? numindexes : MIN_BRUSH_INDEXES);

	BufferFactory.CreateVertexBuffer (sizeof (brushpolyvert_t), numvertexes, &d3d_SurfVertexes, "d3d_SurfVertexes", verts);
	BufferFactory.CreateIndexBuffer (d3d_BrushState.IndexSize, numindexes, &d3d_SurfIndexes, "d3d_SurfIndexes");

	d3d_BrushState.MaxIndexes = numindexes;
	d3d_BrushState.FirstIndex = 0;

	TempHunk->FreeToLowMark (hunkmark);
}


// items that are common to both VS and PS should go to the front of the list
#define BRUSHSHADER_DETAIL		1

// items that are PS only make up the rest of the list
#define BRUSHSHADER_FOG			2
#define BRUSHSHADER_STDLUMA		4
#define BRUSHSHADER_ADDLUMA		8

shaderdefine_t BrushVSDefines[] =
{
	ENCODE_DEFINE (BRUSHSHADER_DETAIL, "1")
};


shaderdefine_t BrushPSDefines[] =
{
	ENCODE_DEFINE (BRUSHSHADER_DETAIL, "1"),
	ENCODE_DEFINE (BRUSHSHADER_FOG, "1"),
	ENCODE_DEFINE (BRUSHSHADER_STDLUMA, "1"),
	ENCODE_DEFINE (BRUSHSHADER_ADDLUMA, "1")
};


void D3DSurf_Init (void)
{
	D3D11_INPUT_ELEMENT_DESC surflo[] =
	{
		MAKELAYOUTELEMENT ("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0),
		MAKELAYOUTELEMENT ("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,	   0, 0),
		MAKELAYOUTELEMENT ("LMCOORD",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0)
	};

	QSHADERFACTORY ShaderFactory (IDR_BRUSHFX);
	D3D10_SHADER_MACRO *Defines = NULL;

	for (int i = 0; i < ARRAYLENGTH (d3d_SurfVertexShader); i++)
	{
		Defines = ShaderFactory.EncodeDefines (BrushVSDefines, ARRAYLENGTH (BrushVSDefines), i);
		ShaderFactory.CreateVertexShader (&d3d_SurfVertexShader[i], "SurfVS", Defines);
	}

	ShaderFactory.CreateInputLayout (&d3d_SurfLayout, "d3d_SurfLayout", LAYOUTPARAMS (surflo));
	ShaderFactory.CreateGeometryShader (&d3d_SurfGeometryShader, "SurfDetailGS", ShaderFactory.EncodeDefines (BrushVSDefines, ARRAYLENGTH (BrushVSDefines), BRUSHSHADER_DETAIL));

	for (int i = 0; i < ARRAYLENGTH (d3d_SurfPixelShader); i++)
	{
		// both cannot be set
		if ((i & BRUSHSHADER_STDLUMA) && (i & BRUSHSHADER_ADDLUMA)) continue;

		Defines = ShaderFactory.EncodeDefines (BrushPSDefines, ARRAYLENGTH (BrushPSDefines), i);
		ShaderFactory.CreatePixelShader (&d3d_SurfPixelShader[i], "SurfPS", Defines);
	}

	BufferFactory.CreateConstantBuffer (sizeof (surfconstants_t), &d3d_SurfConstants, "d3d_SurfConstants");
}


void D3DSurf_Shutdown (void)
{
	SAFE_RELEASE (d3d_SurfConstants);
}


// handles the stuff that needs to be done only on startup/shutdown
CD3DInitShutdownHandler d3d_SurfHandler ("surf", D3DSurf_Init, D3DSurf_Shutdown);

// handles the stuff that needs to be done on map changes
CD3DInitShutdownHandler d3d_BrushHandler ("brush", D3DBrush_Init, D3DBrush_Shutdown);


void D3DBrush_BeginSurfaces (void)
{
	// common surface state
	d3d11_State->VSSetConstantBuffer (1, d3d_SurfConstants);
	d3d11_State->PSSetConstantBuffer (1, d3d_SurfConstants);

	d3d11_State->IASetInputLayout (d3d_SurfLayout);
	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	d3d11_State->IASetVertexBuffer (0, d3d_SurfVertexes, sizeof (brushpolyvert_t), 0);
	d3d11_State->IASetIndexBuffer (d3d_SurfIndexes, d3d_BrushState.IndexFormat, 0);

	// entity may not be NULL any more but 0 is still valid alpha
	d3d_BrushState.CurrentAlpha = -1;
	d3d_BrushState.CurrentEnt = NULL;
	d3d_BrushState.CurrentTexture = NULL;
}


void D3DState_DepthBiasChanged (cvar_t *var);

cvar_t r_additivefullbrights ("r_additivefullbrights", "0", CVAR_ARCHIVE);
cvar_t r_depthbias ("r_depthbias", "0", 0, D3DState_DepthBiasChanged);
cvar_t r_depthbiasclamp ("r_depthbiasclamp", "0", 0, D3DState_DepthBiasChanged);
cvar_t r_slopescaleddepthbias ("r_slopescaleddepthbias", "0", 0, D3DState_DepthBiasChanged);
bool r_usingdepthbias = false;

void D3DLight_UpdateLightmaps (void);
void D3DLight_CheckSurfaceForModification (msurface_t *surf);

void D3DLight_NewDynamicFrame (void);
void D3DLight_PushDynamics (entity_t *ent, mnode_t *headnode);
void D3DWarp_DrawWaterSurfaces (brushhdr_t *hdr, entity_t *ent);
bool D3DWarp_CheckAlphaSurface (msurface_t *surf, entity_t *ent);
void D3DSky_DrawSkySurfaces (brushhdr_t *hdr, entity_t *ent);

void D3DSurf_MarkLeaves (void);

extern cvar_t r_lockpvs;
extern cvar_t r_lockfrustum;
extern cvar_t r_locksurfaces;

msurface_t **r_cachedworldsurfaces = NULL;
int r_numcachedworldsurfaces = 0;

mleaf_t **r_cachedworldleafs = NULL;
int r_numcachedworldleafs = 0;

mnode_t **r_cachedworldnodes = NULL;
int r_numcachedworldnodes = 0;

void D3DSurf_BuildWorldCache (void)
{
	r_cachedworldsurfaces = (msurface_t **) MainHunk->Alloc (cl.worldmodel->brushhdr->numsurfaces * sizeof (msurface_t *));
	r_numcachedworldsurfaces = 0;

	r_cachedworldleafs = (mleaf_t **) MainHunk->Alloc ((cl.worldmodel->brushhdr->numleafs + 1) * sizeof (mleaf_t *));
	r_numcachedworldleafs = 0;

	// not doing anything with this yet...
	//r_cachedworldnodes = (mnode_t **) MainHunk->Alloc (cl.worldmodel->brushhdr->numnodes * sizeof (mnode_t *));
	//r_numcachedworldnodes = 0;

	// always rebuild the world
	d3d_RenderDef.rebuildworld = true;
}


void D3DSurf_EmitSurfToAlpha (msurface_t *surf, entity_t *ent)
{
	if (ent)
	{
		// copy out the midpoint for matrix transforms
		float midpoint[3];

		// transform the surface midpoint by the modelsurf matrix so that it goes into the proper place
		// (we kept the entity matrix in local space so that we can do this correctly)
		ent->matrix.TransformPoint (midpoint, surf->midpoint);

		// now add it
		D3DAlpha_AddToList (surf, ent, midpoint);
	}
	else
	{
		// just add it
		D3DAlpha_AddToList (surf, ent, surf->midpoint);
	}
}


texture_t *D3DSurf_TextureAnimation (texture_t *base)
{
	int relative = (int) (cl.time * 10) % base->anim_total;
	int count = 0;
	texture_t *cached = base;

	// fixme - put these in a table
	while (base->anim_min > relative || base->anim_max <= relative)
	{
		base = base->anim_next;

		// prevent crash here
		if (!base) return cached;
		if (++count > 100) return cached;
	}

	return base;
}


void D3DSurf_DrawTextureChain (texture_t *tex)
{
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	D3D11_MAP MapType = D3D11_MAP_WRITE_NO_OVERWRITE;

	// size to map rounded off to a cache line (at this stage it may be more efficient to just push everything into the index buffer in one go)
	int MapSize = CACHE_ALIGN (tex->numindexes * d3d_BrushState.IndexSize) / d3d_BrushState.IndexSize;

	if (d3d_BrushState.FirstIndex + MapSize >= d3d_BrushState.MaxIndexes)
	{
		MapType = D3D11_MAP_WRITE_DISCARD;
		d3d_BrushState.FirstIndex = 0;
	}

	if (SUCCEEDED (d3d11_Context->Map (d3d_SurfIndexes, 0, MapType, 0, &MappedResource)))
	{
		byte *Indexes = (byte *) MappedResource.pData + (d3d_BrushState.FirstIndex * d3d_BrushState.IndexSize);

		for (msurface_t *surf = tex->texturechain; surf; surf = surf->texturechain)
		{
			Q_MemCpy (Indexes, surf->indexes, surf->numindexes * d3d_BrushState.IndexSize);
			Indexes += surf->numindexes * d3d_BrushState.IndexSize;
			d3d_RenderDef.brush_polys++;
		}

		d3d11_Context->Unmap (d3d_SurfIndexes, 0);
		D3DMisc_DrawIndexedCommon (tex->numindexes, d3d_BrushState.FirstIndex);
		d3d_BrushState.FirstIndex += MapSize;
	}

	D3DSurf_ClearTextureChain (tex);
}


void D3DSurf_DrawSolidSurfaces (brushhdr_t *hdr, entity_t *ent)
{
	int ShaderFlags = 0;

	if (r_detailtextures.value)
	{
		ShaderFlags |= BRUSHSHADER_DETAIL;
		d3d11_Context->GSSetShader (d3d_SurfGeometryShader, NULL, 0);
		d3d11_Context->GSSetConstantBuffers (1, 1, &d3d_SurfConstants);
		d3d11_State->PSSetShaderResourceView (2, d3d_NoiseSRV);
	}

	d3d11_State->VSSetShader (d3d_SurfVertexShader[ShaderFlags]);

	d3d11_State->PSSetSampler (0, d3d_DefaultSamplerWrap);
	d3d11_State->PSSetSampler (1, d3d_SampleClampLinear);

	for (int i = 0; i < hdr->numtextures; i++)
	{
		texture_t *tex = NULL;
		msurface_t *surf = NULL;

		if (!(tex = hdr->textures[i])) continue;
		if (!(surf = tex->texturechain)) continue;
		if (!(surf->flags & SURF_DRAWSOLID)) continue;

		// get the correct texture frame to use for animations (ent is always guaranteed non-NULL now)
		// this also protects against mods setting frame to anything other than 0 or 1
		// Mod_LoadTextures ensured that every texture always has a valid alternate anim (even if it's just
		// pointing back to the texture itself) so this is safe and might help performance by avoiding a branch.
		texture_t *frame = tex->animframes[!!ent->frame];
		texture_t *anim = frame->anim_total ? D3DSurf_TextureAnimation (frame) : frame;

		if (!D3DMisc_OverridePS ())
		{
			// get the shader to use
			int ShaderFlag = (RealFogDensity > 0) ? BRUSHSHADER_FOG : 0;

			if (r_lightmap.integer)
				d3d11_State->PSSetTexture (0, &QTEXTURE::GreyTexture);
			else
			{
				d3d11_State->PSSetTexture (0, anim->teximage);

				if (anim->lumaimage && gl_fullbrights.integer)
				{
					// and fullbright type
					if (r_additivefullbrights.value)
						ShaderFlag |= BRUSHSHADER_ADDLUMA;
					else ShaderFlag |= BRUSHSHADER_STDLUMA;

					d3d11_State->PSSetTexture (1, anim->lumaimage);
				}
			}

			// add other flags
			if (r_detailtextures.value) ShaderFlag |= BRUSHSHADER_DETAIL;

			d3d11_State->PSSetShader (d3d_SurfPixelShader[ShaderFlag]);
		}

		// using tex instead of anim because that's where the index counts are stored
		D3DSurf_DrawTextureChain (tex);
	}

	if (r_detailtextures.value) d3d11_Context->GSSetShader (NULL, NULL, 0);
}


void D3DSurf_DrawTextureChains (entity_t *ent, int alpha)
{
	if (!ent) return;
	if (!ent->BrushDrawFlags) return;

	model_t *mod = ent->model;
	brushhdr_t *hdr = mod->brushhdr;

	// in general ent and alpha will both change together, but sometimes they don't (we'll just suck it up)
	if (ent != d3d_BrushState.CurrentEnt || alpha != d3d_BrushState.CurrentAlpha)
	{
		surfconstants_t d3d_SurfUpdate;

		d3d_SurfUpdate.ModelMatrix.Load (&d3d_ModelViewProjMatrix);
		d3d_SurfUpdate.ModelMatrix.Mult (&ent->matrix);

		d3d_SurfUpdate.surfcolour[0] = d3d_SurfUpdate.surfcolour[1] = d3d_SurfUpdate.surfcolour[2] = r_lightscale.value;
		d3d_SurfUpdate.surfcolour[3] = (float) alpha / 255.0f;

		d3d11_Context->UpdateSubresource (d3d_SurfConstants, 0, NULL, &d3d_SurfUpdate, 0, 0);

		d3d_BrushState.CurrentEnt = ent;
		d3d_BrushState.CurrentAlpha = alpha;
	}

	// update any lightmaps that were modified
	D3DLight_UpdateLightmaps ();

	// draw solid surfaces first because they're most likely to be the baseline for everything
	if (ent->BrushDrawFlags & SURF_DRAWSOLID) D3DSurf_DrawSolidSurfaces (hdr, ent);

	// drawn after because they're more complex shaders and more likely to be occluded (and thus early-z rejected) by the solid stuff
	if (ent->BrushDrawFlags & SURF_DRAWTURB) D3DWarp_DrawWaterSurfaces (hdr, ent);

	// and finally draw sky
	if (ent->BrushDrawFlags & SURF_DRAWSKY) D3DSky_DrawSkySurfaces (hdr, ent);

	// clear flags so that we don't accumulate draw calls we shouldn't
	ent->BrushDrawFlags = 0;
}


void D3DSurf_ChainCommon (entity_t *ent, msurface_t *surf, texture_t *tex)
{
	D3DLight_CheckSurfaceForModification (surf);
	D3DSurf_AddToTextureChain (surf, tex);
	ent->BrushDrawFlags |= surf->flags;
}


void D3DSurf_ChainSurface (msurface_t *surf, entity_t *ent)
{
	// defer animation until draw time
	if ((surf->flags & SURF_DRAWTURB) && D3DWarp_CheckAlphaSurface (surf, ent))
		return;

	D3DSurf_ChainCommon (ent, surf, surf->texinfo->texture);
}


void D3DBrush_EndAlphaSurfaces (void)
{
	if (d3d_BrushState.CurrentEnt)
	{
		// this is a hack otherwise alpha won't update
		// it will recache properly (if needed) after this call
		int alpha = d3d_BrushState.CurrentAlpha;
		entity_t *ent = d3d_BrushState.CurrentEnt;

		// and set them back to invalid otherwise they won't recache
		d3d_BrushState.CurrentAlpha = -1;
		d3d_BrushState.CurrentEnt = NULL;

		// and draw using the saved off versions
		D3DSurf_DrawTextureChains (ent, alpha);
	}
}


void D3DBrush_BatchAlphaSurface (msurface_t *surf, entity_t *ent, int alpha)
{
	texture_t *tex = surf->texinfo->texture;

	// because alpha surfaces are sorted back to front we can't rely on the built-in change trackers in
	// the default draw so we need to check them separately here.  the built-ins are still used for doing
	// the actual change; these checks just handle submitting the current chain
	if (d3d_BrushState.CurrentTexture != tex || d3d_BrushState.CurrentEnt != ent || d3d_BrushState.CurrentAlpha != alpha)
	{
		// draw anything accumulated so far
		D3DBrush_EndAlphaSurfaces ();

		// re-push dynamic lights (only if entity changes otherwise the transformed origins will be OK)
		if (!(surf->flags & SURF_DRAWTURB) && !(surf->flags & SURF_DRAWSKY) && (d3d_BrushState.CurrentEnt != ent))
		{
			brushhdr_t *hdr = ent->model->brushhdr;

			// always go to a new dynamic frame
			D3DLight_NewDynamicFrame ();
			D3DLight_PushDynamics (ent, hdr->nodes + hdr->hulls[0].firstclipnode);
		}

		d3d_BrushState.CurrentTexture = tex;
		d3d_BrushState.CurrentEnt = ent;
		d3d_BrushState.CurrentAlpha = alpha;
	}

	D3DSurf_ChainCommon (ent, surf, tex);
}


void D3DSurf_DrawBModelSurfaces (entity_t *ent, brushhdr_t *hdr)
{
	// calculate dynamic lighting for the inline bmodel
	// this is done after the matrix is calced so that we can have the proper transform for lighting
	D3DLight_PushDynamics (ent, hdr->nodes + hdr->hulls[0].firstclipnode);

	// and now handle it's surfaces
	msurface_t *surf = hdr->surfaces + hdr->firstmodelsurface;

	// don't bother with ordering these for now; we'll sort them by texture later
	for (int s = 0; s < hdr->nummodelsurfaces; s++, surf++)
	{
		// fixme - do this per node?
		float dot = Mod_PlaneDist (surf->plane, ent->modelorg);

		if (((surf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(surf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (surf->flags & SURF_DRAWFENCE)
				D3DSurf_EmitSurfToAlpha (surf, ent);
			else if (ent->alphaval > 0 && ent->alphaval < 255)
				D3DSurf_EmitSurfToAlpha (surf, ent);
			else D3DSurf_ChainSurface (surf, ent);
		}
	}
}


void D3DSurf_DrawBrushModel (entity_t *ent)
{
	model_t *mod = ent->model;

	// catch null models
	if (!mod->brushhdr) return;
	if (!mod->brushhdr->numsurfaces) return;

	if (R_CullBox (&ent->cullinfo))
	{
		// mark as not visible
		ent->visframe = -1;
		return;
	}

	// visible this frame now
	ent->visframe = d3d_RenderDef.framecount;

	// try to avoid having to do rotation + inversion if possible
	if (/*ent->origin[0] || ent->origin[1] || ent->origin[2] ||*/ ent->angles[0] || ent->angles[1] || ent->angles[2])
	{
		// transform in local space for dlight, sky, etc handling
		ent->matrix.Identity ();
		ent->matrix.Translate (ent->lerporigin);
		ent->matrix.Rotate (ent->lerpangles[1], ent->lerpangles[0], ent->lerpangles[2]);

		// this is needed for dlights as well so only calc the inverse matrix once (we should probably only do this for bmodel
		// entities that are actually hit by lights - NO - also needed for modelorg)
		ent->matrix.Inverse (&ent->invmatrix);
		ent->invmatrix.TransformPoint (ent->modelorg, r_refdef.vieworigin);
	}
	else if (ent->origin[0] || ent->origin[1] || ent->origin[2])
	{
		ent->matrix.Identity ();
		ent->matrix.Translate (ent->lerporigin);

		ent->invmatrix.Identity ();
		ent->invmatrix.Translate (-ent->lerporigin[0], -ent->lerporigin[1], -ent->lerporigin[2]);

		ent->invmatrix.TransformPoint (ent->modelorg, r_refdef.vieworigin);
	}
	else
	{
		ent->matrix.Identity ();
		ent->invmatrix.Identity ();
		Vector3Copy (ent->modelorg, r_refdef.vieworigin);
	}

	// go to a new dynamic frame because this may be a shared model
	D3DLight_NewDynamicFrame ();
	D3DSurf_DrawBModelSurfaces (ent, mod->brushhdr);

	if (ent->BrushDrawFlags)
	{
		extern ID3D11RasterizerState *d3d_RSZFighting;

		if (r_usingdepthbias && ent->model->name[0] == '*')
		{
			d3d11_State->RSSetState (d3d_RSZFighting);
			D3DSurf_DrawTextureChains (ent, 255);
			d3d11_State->RSSetState (d3d_RS3DView);
		}
		else D3DSurf_DrawTextureChains (ent, 255);
	}
}


void R_StoreEfrags (struct efrag_t **ppefrag);
bool R_CullPlaneForNearestPoint (cullinfo_t *ci, mplane_t *p);
bool R_CullPlaneForFarthestPoint (cullinfo_t *ci, mplane_t *p);

bool D3DSurf_CullNode (cullinfo_t *ci, int clipflags)
{
	if ((ci->clipflags = clipflags) != 0)
	{
		if (ci->cullplane != -1)
		{
			if (R_CullPlaneForNearestPoint (ci, &vid.frustum[ci->cullplane])) return true;
			if (!R_CullPlaneForFarthestPoint (ci, &vid.frustum[ci->cullplane])) ci->clipflags &= ~(1 << ci->cullplane);
		}

		for (int c = 0; c < 5; c++)
		{
			if (!(ci->clipflags & (1 << c))) continue;
			if (c == ci->cullplane) continue;

			if (R_CullPlaneForNearestPoint (ci, &vid.frustum[c]))
			{
				ci->cullplane = c;
				return true;
			}

			if (!R_CullPlaneForFarthestPoint (ci, &vid.frustum[c])) ci->clipflags &= ~(1 << c);
		}
	}

	ci->cullplane = -1;
	return false;
}


// this now only builds a cache of surfs and leafs for drawing from; the real draw lists are built separately
void D3DSurf_RecursiveWorldNode (mnode_t *node, int clipflags)
{
	if (node->contents == CONTENTS_SOLID) return;
	if (node->visframe != d3d_RenderDef.visframecount) return;
	if (D3DSurf_CullNode (&node->cullinfo, clipflags)) return;

	// if it's a leaf node draw stuff
	if (node->contents < 0)
	{
		// node is a leaf so add stuff for drawing
		mleaf_t *leaf = (mleaf_t *) node;
		msurface_t **mark = leaf->firstmarksurface;
		int c = leaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = d3d_RenderDef.framecount;
				(*mark)->cullinfo.clipflags = clipflags;
				mark++;
			} while (--c);
		}

		if (leaf->efrags)
		{
			// store out any efrags we got so that they can emit entities to the appropriate lists
			R_StoreEfrags (&leaf->efrags);

			// and cache this leaf for reuse (only needed for efrags)
			r_cachedworldleafs[r_numcachedworldleafs] = leaf;
			r_numcachedworldleafs++;
		}

		d3d_RenderDef.numleaf++;
		return;
	}

	// node is just a decision point, so go down the appropriate sides
	node->dot = Mod_PlaneDist (node->plane, d3d_RenderDef.worldentity.modelorg);
	node->side = (node->dot >= 0 ? 0 : 1);

	// recurse down the children, front side first
	D3DSurf_RecursiveWorldNode (node->children[node->side], node->cullinfo.clipflags);

	// draw stuff
	if (node->numsurfaces)
	{
		msurface_t *surf = node->surfaces;
		int sidebit = (node->dot >= 0 ? 0 : SURF_PLANEBACK);
		int nodesurfs = 0;
		float dot;

		// add stuff to the draw lists
		for (int c = node->numsurfaces; c; c--, surf++)
		{
			// the SURF_PLANEBACK test never actually evaluates to true with GLQuake as the surf
			// will have the same plane and facing as the node here.  oh well...
			if (surf->visframe != d3d_RenderDef.framecount) continue;
			if ((surf->flags & SURF_PLANEBACK) != sidebit) continue;

			// only check for culling if both the node and leaf containing this surf intersect the frustum
			if (node->cullinfo.clipflags && surf->cullinfo.clipflags)
				if (R_CullBox (&surf->cullinfo)) continue;

			dot = (surf->plane == node->plane) ? node->dot : Mod_PlaneDist (surf->plane, d3d_RenderDef.worldentity.modelorg);

			if (dot > vid.farclip) vid.farclip = dot;
			if (-dot > vid.farclip) vid.farclip = -dot;

			// cache this surface for reuse
			r_cachedworldsurfaces[r_numcachedworldsurfaces] = surf;
			r_numcachedworldsurfaces++;

			nodesurfs++;
		}

		if (nodesurfs)
		{
			// Con_Printf ("node with %i surfaces\n", node->numsurfaces);
			// Con_Printf ("node volume is %f\n", node->volume);
			d3d_RenderDef.numnode++;
		}
	}

	// recurse down the back side (the compiler should optimize this tail recursion)
	D3DSurf_RecursiveWorldNode (node->children[!node->side], node->cullinfo.clipflags);
}


void D3DSurf_BuildWorld (void)
{
	// ensure
	cl.worldmodel->brushhdr->bspmodel = false;
	d3d_RenderDef.worldentity.model = cl.worldmodel;

	// the world entity needs a matrix set up correctly so that trans surfs will transform correctly
	// the world model hasn't moved at all so it's local space matrix is identity
	// this only needs to be done once (at map load time)
	d3d_RenderDef.worldentity.matrix.Identity ();
	d3d_RenderDef.worldentity.invmatrix.Identity ();
	Vector3Copy (d3d_RenderDef.worldentity.modelorg, r_refdef.vieworigin);

	// mark visible leafs
	D3DSurf_MarkLeaves ();

	// assume that the world is always visible ;)
	// (although it might not be depending on the scene...)
	d3d_RenderDef.worldentity.visframe = d3d_RenderDef.framecount;
	d3d_RenderDef.worldentity.frame = 0;

	d3d_RenderDef.numnode = 0;
	d3d_RenderDef.numleaf = 0;

	if (d3d_RenderDef.rebuildworld && !r_locksurfaces.integer)
	{
		// go to a new cache
		r_numcachedworldsurfaces = 0;
		r_numcachedworldleafs = 0;

		vid.farclip = 4096.0f;	// never go below this

		D3DSurf_RecursiveWorldNode (cl.worldmodel->brushhdr->nodes, 31);
		r_numcachedworldnodes = d3d_RenderDef.numnode;

		// vid.farclip so far represents one side of a right-angled triangle with the longest side being what we actually want
		vid.farclip = sqrt (vid.farclip * vid.farclip + vid.farclip * vid.farclip);

		// mark not to rebuild
		d3d_RenderDef.rebuildworld = false;
	}
	else
	{
		// store efrags from the previous build so that they can emit entities to the appropriate lists
		for (int i = 0; i < r_numcachedworldleafs; i++)
			R_StoreEfrags (&r_cachedworldleafs[i]->efrags);
	}

	// update r_speeds counters
	d3d_RenderDef.numnode = r_numcachedworldnodes;
	d3d_RenderDef.numleaf = r_numcachedworldleafs;
}


void D3DSurf_DrawWorld (void)
{
	D3DBrush_BeginSurfaces ();

	// add any brush models we got first as they are more likely to
	// be occluded by the world than occlude the world
	for (int i = 0; i < d3d_BrushEdicts.NumEdicts; i++)
	{
		entity_t *ent = d3d_BrushEdicts.Edicts[i];
		D3DSurf_DrawBrushModel (ent);
	}

	// go to a new dynamic frame because otherwise we'll inherit it from the prev bmodel
	D3DLight_NewDynamicFrame ();

	// merge selected bmodels into the world
	for (int i = 0; i < d3d_MergeEdicts.NumEdicts; i++)
	{
		entity_t *ent = d3d_MergeEdicts.Edicts[i];
		model_t *mod = ent->model;

		if (R_CullBox (&ent->cullinfo))
		{
			// mark as not visible
			ent->visframe = -1;
			continue;
		}

		// now do the merging
		ent->visframe = d3d_RenderDef.framecount;

		// ensure that there is a valid matrix in the entity for alpha sorting/distance
		// this model hasn't moved at all so it's local space matrix is identity
		ent->matrix.Identity ();
		ent->invmatrix.Identity ();
		Vector3Copy (ent->modelorg, r_refdef.vieworigin);

		// these don't go a new dynamic frame and pretend that they belong to the world
		D3DSurf_DrawBModelSurfaces (&d3d_RenderDef.worldentity, mod->brushhdr);
	}

	// chain up the world now
	D3DLight_PushDynamics (&d3d_RenderDef.worldentity, cl.worldmodel->brushhdr->nodes);

	// build the draw lists from an existing cache
	for (int i = 0; i < r_numcachedworldsurfaces; i++)
	{
		msurface_t *surf = r_cachedworldsurfaces[i];

		D3DSurf_ChainSurface (surf, &d3d_RenderDef.worldentity);
		surf->visframe = d3d_RenderDef.framecount;
	}

	// and now draw the world
	D3DSurf_DrawTextureChains (&d3d_RenderDef.worldentity, 255);
}


bool D3DSurf_ShouldEntityBeMerged (entity_t *ent, model_t *mod)
{
	if (!r_mergebmodels.value) return false;

	// if using depth bias it will need separate state
	if (r_usingdepthbias) return false;

	// if more than 1 entity uses the model we can't merge it or we'd create an infinite texture chain
	if (mod->numents > 1) return false;

	// this is only valid for inline bmodels as instanced models may have textures which are not in the world
	if (mod->name[0] != '*') return false;

	// now we check for transforms
	if (ent->origin[0] || ent->origin[1] || ent->origin[2]) return false;
	if (ent->angles[0] || ent->angles[1] || ent->angles[2]) return false;

	// translucent models drawn later too
	if (ent->alphaval > 0 && ent->alphaval < 255) return false;

	// entities with alternate texture anims can't be merged as they would pick up the frame from the worldmodel
	if (ent->frame) return false;

	// Con_Printf ("merging %s\n", mod->name);

	// the entity is OK for merging into the world now
	return true;
}


void D3DSurf_AddEdict (entity_t *ent)
{
	model_t *mod = ent->model;

	if (!mod) return;
	if (mod->type != mod_brush) return;

	// catch null models
	if (!mod->brushhdr) return;
	if (!mod->brushhdr->numsurfaces) return;

	// alpha surfs need to be added individually for proper sorting so just add it to the list for now
	if (D3DSurf_ShouldEntityBeMerged (ent, mod))
		d3d_MergeEdicts.AddEntity (ent);
	else d3d_BrushEdicts.AddEntity (ent);
}


/*
=============================================================

	VISIBILITY

=============================================================
*/

void D3DSurf_LeafVisibility (byte *vis)
{
	// leaf 0 is the generic solid leaf; the last leaf overlaps with the first node
	mleaf_t *leaf = &cl.worldmodel->brushhdr->leafs[1];

	// mark leafs and surfaces as visible
	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++, leaf++)
	{
		if (!vis || (vis[i >> 3] & (1 << (i & 7))))
		{
			// note - nodes and leafs need to be in consecutive memory for this to work so
			// that the last leaf will resolve to the first node here; this is set up in Mod_LoadVisLeafsNodes
			// first node always needs to be in the pvs for D3DSurf_RecursiveWorldNode
			mnode_t *node = (mnode_t *) leaf;

			do
			{
				// already added
				if (node->visframe == d3d_RenderDef.visframecount) break;

				// add it
				node->visframe = d3d_RenderDef.visframecount;
				node = node->parent;
			} while (node);
		}
	}
}


void D3DSurf_MarkLeaves (void)
{
	// viewleaf hasn't changed or we're drawing with a locked PVS
	if ((d3d_RenderDef.oldviewleaf == d3d_RenderDef.viewleaf) || r_lockpvs.value) return;

	// go to a new visframe
	d3d_RenderDef.visframecount++;

	// rebuild the world lists
	d3d_RenderDef.rebuildworld = true;

	// add in visible leafs - we always add the fat PVS to ensure that client visibility
	// is the same as that which was used by the server; R_CullBox will take care of unwanted leafs
	if (r_novis.integer)
		D3DSurf_LeafVisibility (NULL);
	else if (d3d_RenderDef.viewleaf->flags & SURF_DRAWTURB)
		D3DSurf_LeafVisibility (Mod_FatPVS (r_refdef.vieworigin));
	else D3DSurf_LeafVisibility (Mod_LeafPVS (d3d_RenderDef.viewleaf, cl.worldmodel));

	// no old viewleaf so can't make a transition check
	if (d3d_RenderDef.oldviewleaf)
	{
		// check for a contents transition
		if (d3d_RenderDef.oldviewleaf->contents == d3d_RenderDef.viewleaf->contents)
		{
			// if we're still in the same contents we still have the same contents colour
			d3d_RenderDef.viewleaf->contentscolor = d3d_RenderDef.oldviewleaf->contentscolor;
		}
		else if (!r_novis.integer && ((d3d_RenderDef.viewleaf->flags & SURF_DRAWTURB) || (d3d_RenderDef.oldviewleaf->flags & SURF_DRAWTURB)))
		{
			// we've had a contents transition so merge the old pvs with the new
			D3DSurf_LeafVisibility (Mod_LeafPVS (d3d_RenderDef.oldviewleaf, cl.worldmodel));
		}
	}

	// we've now completed the PVS change
	switch (d3d_RenderDef.viewleaf->contents)
	{
	case CONTENTS_EMPTY:
	case CONTENTS_WATER:
	case CONTENTS_SLIME:
	case CONTENTS_LAVA:
		d3d_RenderDef.oldviewleaf = d3d_RenderDef.viewleaf;
		break;

	default:
		d3d_RenderDef.oldviewleaf = NULL;
		break;
	}
}



