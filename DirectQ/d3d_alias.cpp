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

// gl_mesh.c: triangle model functions


#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

// needed for optimize faces/vertices
#include <d3dx9.h>
#pragma comment (lib, "d3dx9.lib")

void D3DLight_LightPoint (lightinfo_t *info, float *origin);
void D3DMain_ComputeEntityTransform (entity_t *ent);

extern cvar_t r_lightscale;
extern cvar_t r_additivefullbrights;


cvar_t r_aliasdelerpdelta ("r_aliasdelerpdelta", 10.0f, CVAR_ARCHIVE | CVAR_MAP);

#define MESH_SCALE_VERT(v, ind) (((v)[ind] * hdr->scale[ind]) + hdr->scale_origin[ind])

void DelerpMuzzleFlashes (aliashdr_t *hdr, aliasload_t *load)
{
	// shrak crashes as it has viewmodels with only one frame - who woulda ever thought!!!
	if (hdr->numframes < 2) return;

	// get pointers to the verts
	drawvertx_t *vertsf0 = load->vertexes[0];
	drawvertx_t *vertsf1 = load->vertexes[1];

	// now go through them and compare.  we expect that (a) the animation is sensible and there's no major
	// difference between the 2 frames to be expected, and (b) any verts that do exhibit a major difference
	// can be assumed to belong to the muzzleflash (this is going to break sometime)
	for (int j = 0; j < hdr->vertsperframe; j++, vertsf0++, vertsf1++)
	{
		// if it's above a certain treshold, assume a muzzleflash and mark for no lerp
		// 10 is the approx lowest range of visible front to back in a view model, so that seems reasonable to work with
		if (MESH_SCALE_VERT (vertsf1->v, 0) - MESH_SCALE_VERT (vertsf0->v, 0) > r_aliasdelerpdelta.value)
			vertsf0->lerpvert = false;
		else vertsf0->lerpvert = true;
	}
}


#define ALIASSHADER_FOG			1
#define ALIASSHADER_STDLUMA		2
#define ALIASSHADER_ADDLUMA		4
#define ALIASSHADER_PLAYER		8
#define ALIASSHADER_SHADOW		(1 << 31)

shaderdefine_t AliasDefines[] =
{
	ENCODE_DEFINE (ALIASSHADER_FOG, "1"),
	ENCODE_DEFINE (ALIASSHADER_STDLUMA, "1"),
	ENCODE_DEFINE (ALIASSHADER_ADDLUMA, "1"),
	ENCODE_DEFINE (ALIASSHADER_PLAYER, "1"),
	ENCODE_DEFINE (ALIASSHADER_SHADOW, "1")
};


ID3D11InputLayout *d3d_MeshLayout = NULL;
ID3D11InputLayout *d3d_InstancedLayout = NULL;
ID3D11InputLayout *d3d_ViewModelLayout = NULL;

ID3D11VertexShader *d3d_MeshVertexShader = NULL;
ID3D11VertexShader *d3d_InstancedVertexShader = NULL;
ID3D11VertexShader *d3d_ViewModelVertexShader = NULL;

ID3D11PixelShader *d3d_AliasPixelShaders[16];
ID3D11PixelShader *d3d_ShadowPixelShaders[2];

ID3D11Buffer *d3d_MeshConstants = NULL;


struct aliasbuffer_t
{
	ID3D11Buffer *Indexes;
	ID3D11Buffer *Positions;
	ID3D11Buffer *Texcoords;
	ID3D11Buffer *Blends;
	int RegistrationSequence;
	char Name[64];
	int NumMesh;
	int NumIndexes;
	static int NumBuffers;
};


int aliasbuffer_t::NumBuffers = 0;

aliasbuffer_t d3d_AliasBuffers[MAX_MODELS];

struct aliasposition_t
{
	float position[3];
	float normal[3];
};

struct aliastexcoord_t
{
	float st[2];
};

struct aliasblend_t
{
	float blendindex;
};

// this can be reused as the instanced vertex definition...
struct aliasinstance_t
{
	D3DXMATRIX ModelMatrix;
	float shadelight[4];
	float shadevector[3];
	float blend;
};


QTEXTURE d3d_PaletteRowTextures[16];


void D3DAlias_FreeBufferSet (aliasbuffer_t *buf)
{
	// release the buffer set
	SAFE_RELEASE (buf->Positions);
	SAFE_RELEASE (buf->Texcoords);
	SAFE_RELEASE (buf->Blends);
	SAFE_RELEASE (buf->Indexes);

	// and invalidate the cached data
	buf->Name[0] = 0;
	buf->NumMesh = 0;
	buf->NumIndexes = 0;
}


void D3DAlias_InitForMap (void)
{
	// clear down any buffers not marked as live for this map
	for (int i = 0; i < MAX_MODELS; i++)
	{
		if (d3d_AliasBuffers[i].RegistrationSequence == d3d_RenderDef.RegistrationSequence) continue;

		D3DAlias_FreeBufferSet (&d3d_AliasBuffers[i]);
	}
}


void D3DAlias_Shutdown (void)
{
	for (int i = 0; i < 16; i++)
		d3d_PaletteRowTextures[i].Release ();

	for (int i = 0; i < MAX_MODELS; i++)
	{
		D3DAlias_FreeBufferSet (&d3d_AliasBuffers[i]);
		d3d_AliasBuffers[i].RegistrationSequence = -1;
	}

	aliasbuffer_t::NumBuffers = 0;

	SAFE_RELEASE (d3d_MeshConstants);
}


void D3DAlias_InitGlobal (void)
{
	// put the buffer structs into a known-good state
	for (int i = 0; i < MAX_MODELS; i++)
	{
		d3d_AliasBuffers[i].Positions = NULL;
		d3d_AliasBuffers[i].Texcoords = NULL;
		d3d_AliasBuffers[i].Blends = NULL;
		d3d_AliasBuffers[i].Indexes = NULL;
		d3d_AliasBuffers[i].RegistrationSequence = -1;
	}

	// and there are no buffers yet
	aliasbuffer_t::NumBuffers = 0;

	// reconvert backward ranges to forward so that we can do correct lookups on them
	for (int i = 0; i < 16; i++)
	{
		byte palrow[16];

		for (int j = 0, k = 15; j < 16; j++, k--)
			palrow[j] = i * 16 + ((i > 7 && i < 14) ? k : j);

		d3d_PaletteRowTextures[i].Upload (palrow, 16, 1, 0, d3d_QuakePalette.standard11);
	}

#define D3D_MESH_LAYOUT_COMMON \
		MAKELAYOUTELEMENT ("CURRPOS",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0), \
		MAKELAYOUTELEMENT ("CURRNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0), \
		MAKELAYOUTELEMENT ("LASTPOS",    0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0), \
		MAKELAYOUTELEMENT ("LASTNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0), \
		MAKELAYOUTELEMENT ("TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT, 2, 0)

	D3D11_INPUT_ELEMENT_DESC meshlo[] =
	{
		D3D_MESH_LAYOUT_COMMON
	};

	D3D11_INPUT_ELEMENT_DESC viewlo[] =
	{
		D3D_MESH_LAYOUT_COMMON,
		MAKELAYOUTELEMENT ("LERPTYPE", 0, DXGI_FORMAT_R32_FLOAT, 3, 0)
	};

	D3D11_INPUT_ELEMENT_DESC instlo[] =
	{
		D3D_MESH_LAYOUT_COMMON,
		MAKELAYOUTELEMENT ("TRANSFORM",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 3, 1),
		MAKELAYOUTELEMENT ("TRANSFORM",   1, DXGI_FORMAT_R32G32B32A32_FLOAT, 3, 1),
		MAKELAYOUTELEMENT ("TRANSFORM",   2, DXGI_FORMAT_R32G32B32A32_FLOAT, 3, 1),
		MAKELAYOUTELEMENT ("TRANSFORM",   3, DXGI_FORMAT_R32G32B32A32_FLOAT, 3, 1),
		MAKELAYOUTELEMENT ("SHADELIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 3, 1),
		MAKELAYOUTELEMENT ("SHADEVECTOR", 0, DXGI_FORMAT_R32G32B32_FLOAT,    3, 1),
		MAKELAYOUTELEMENT ("BLEND",       0, DXGI_FORMAT_R32_FLOAT,          3, 1)
	};

	QSHADERFACTORY ShaderFactory (IDR_ALIASFX);
	D3D10_SHADER_MACRO *Defines = NULL;

	ShaderFactory.CreateVertexShader (&d3d_MeshVertexShader, "MeshVS");
	ShaderFactory.CreateInputLayout (&d3d_MeshLayout, "d3d_MeshLayout", LAYOUTPARAMS (meshlo));

	ShaderFactory.CreateVertexShader (&d3d_InstancedVertexShader, "InstancedVS");
	ShaderFactory.CreateInputLayout (&d3d_InstancedLayout, "d3d_InstancedLayout", LAYOUTPARAMS (instlo));

	ShaderFactory.CreateVertexShader (&d3d_ViewModelVertexShader, "ViewModelVS");
	ShaderFactory.CreateInputLayout (&d3d_ViewModelLayout, "d3d_ViewModelLayout", LAYOUTPARAMS (viewlo));

	for (int i = 0; i < ARRAYLENGTH (d3d_AliasPixelShaders); i++)
	{
		// both cannot be set
		if ((i & ALIASSHADER_STDLUMA) && (i & ALIASSHADER_ADDLUMA)) continue;

		Defines = ShaderFactory.EncodeDefines (AliasDefines, ARRAYLENGTH (AliasDefines), i);
		ShaderFactory.CreatePixelShader (&d3d_AliasPixelShaders[i], "MeshPS", Defines);
	}

	Defines = ShaderFactory.EncodeDefines (AliasDefines, ARRAYLENGTH (AliasDefines), ALIASSHADER_SHADOW);
	ShaderFactory.CreatePixelShader (&d3d_ShadowPixelShaders[0], "MeshPS", Defines);

	Defines = ShaderFactory.EncodeDefines (AliasDefines, ARRAYLENGTH (AliasDefines), ALIASSHADER_SHADOW | ALIASSHADER_FOG);
	ShaderFactory.CreatePixelShader (&d3d_ShadowPixelShaders[1], "MeshPS", Defines);

	BufferFactory.CreateConstantBuffer (sizeof (aliasinstance_t), &d3d_MeshConstants, "d3d_MeshConstants");
}


CD3DInitShutdownHandler d3d_AliasHandlerForMap ("alias for map", D3DAlias_InitForMap);
CD3DInitShutdownHandler d3d_AliasHandlerGlobal ("alias global", D3DAlias_InitGlobal, D3DAlias_Shutdown);


aliasbuffer_t *D3DAlias_GetMeshBuffer (aliashdr_t *hdr)
{
	// first look for a free slot
	for (int i = 0; i < aliasbuffer_t::NumBuffers; i++)
	{
		// buffers which were not released are not valid
		if (d3d_AliasBuffers[i].Blends) continue;
		if (d3d_AliasBuffers[i].Indexes) continue;
		if (d3d_AliasBuffers[i].Positions) continue;
		if (d3d_AliasBuffers[i].Texcoords) continue;

		// this is a valid slot
		hdr->buffer = i;

		return &d3d_AliasBuffers[hdr->buffer];
	}

	// the buffer cache was sized at MAX_MODELS so this should never happen
	// we should make it an std::vector really
	if (aliasbuffer_t::NumBuffers >= MAX_MODELS)
	{
		Sys_Error ("D3DAlias_GetMeshBuffer : aliasbuffer_t::NumBuffers >= MAX_MODELS");
		return NULL;
	}

	// now alloc a new buffer set
	hdr->buffer = aliasbuffer_t::NumBuffers;
	aliasbuffer_t::NumBuffers++;

	return &d3d_AliasBuffers[hdr->buffer];
}


void D3DAlias_MakeMeshBuffers (aliashdr_t *hdr, aliasload_t *load, aliasbuffer_t *buf)
{
	// release any buffers previously used by this buffer set
	SAFE_RELEASE (buf->Positions);
	SAFE_RELEASE (buf->Texcoords);
	SAFE_RELEASE (buf->Blends);
	SAFE_RELEASE (buf->Indexes);

	// this buffer is live now
	buf->RegistrationSequence = d3d_RenderDef.RegistrationSequence;

	aliasposition_t *positions = (aliasposition_t *) TempHunk->FastAlloc (hdr->nummesh * hdr->nummeshframes * sizeof (aliasposition_t));
	aliastexcoord_t *texcoords = (aliastexcoord_t *) TempHunk->FastAlloc (hdr->nummesh * sizeof (aliastexcoord_t));
	aliasblend_t *blends = (aliasblend_t *) TempHunk->FastAlloc (hdr->nummesh * sizeof (aliasblend_t));

	// copy out frames
	for (int m = 0; m < hdr->nummeshframes; m++)
	{
		aliasmesh_t *src = load->mesh;
		aliasposition_t *dst = &positions[m * hdr->nummesh];
		drawvertx_t *dv = load->vertexes[m];

		for (int mm = 0; mm < hdr->nummesh; mm++)
		{
			// pre-scale the verts so that we save a few extra calcs at runtime
			dst[mm].position[0] = MESH_SCALE_VERT (dv[src[mm].vertindex].v, 0);
			dst[mm].position[1] = MESH_SCALE_VERT (dv[src[mm].vertindex].v, 1);
			dst[mm].position[2] = MESH_SCALE_VERT (dv[src[mm].vertindex].v, 2);

			dst[mm].normal[0] = dv[src[mm].vertindex].normal[0];
			dst[mm].normal[1] = dv[src[mm].vertindex].normal[1];
			dst[mm].normal[2] = dv[src[mm].vertindex].normal[2];
		}
	}

	// copy out texcoords
	for (int m = 0; m < hdr->nummesh; m++)
	{
		// convert back to floating point and store out
		texcoords[m].st[0] = (float) load->mesh[m].st[0] / (float) hdr->skinwidth;
		texcoords[m].st[1] = (float) load->mesh[m].st[1] / (float) hdr->skinheight;

		// store lerping factor for view models
		if (load->vertexes[0][load->mesh[m].vertindex].lerpvert)
			blends[m].blendindex = -1;
		else blends[m].blendindex = 1;
	}

	// create buffers
	BufferFactory.CreateVertexBuffer (sizeof (aliasposition_t), hdr->nummesh * hdr->nummeshframes, &buf->Positions, "d3d_MeshPositions", positions);
	BufferFactory.CreateVertexBuffer (sizeof (aliastexcoord_t), hdr->nummesh, &buf->Texcoords, "d3d_MeshTexCoords", texcoords);
	BufferFactory.CreateVertexBuffer (sizeof (aliasblend_t), hdr->nummesh, &buf->Blends, "d3d_MeshBlends", blends);
	BufferFactory.CreateIndexBuffer (sizeof (unsigned short), hdr->numindexes, &buf->Indexes, "d3d_MeshIndexes", load->indexes);
}


void D3DAlias_CompressMesh (aliashdr_t *hdr, aliasload_t *load)
{
	// vertex cache optimization
	// set up the initial params
	hdr->nummesh = 0;
	hdr->numindexes = 0;

	for (int i = 0, v = 0; i < hdr->numtris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			// unique vertex identifiers
			int vertindex = load->triangles[i].vertindex[j];
			bool facesfront = load->triangles[i].facesfront || !load->stverts[vertindex].onseam;

			// see does this vert already exist
			for (v = 0; v < hdr->nummesh; v++)
			{
				// it could use the same xyz but have different s and t
				// MDL only allows this if the front-facing is different so that's what we check
				if (load->mesh[v].vertindex == vertindex && load->mesh[v].facesfront == facesfront)
				{
					// exists; emit an index for it
					load->indexes[hdr->numindexes] = v;
					break;
				}
			}

			if (v == hdr->nummesh)
			{
				// doesn't exist; emit a new vert and index
				load->indexes[hdr->numindexes] = hdr->nummesh;

				load->mesh[hdr->nummesh].vertindex = vertindex;
				load->mesh[hdr->nummesh].st[0] = load->stverts[vertindex].s + (facesfront ? 0 : (hdr->skinwidth / 2));
				load->mesh[hdr->nummesh].st[1] = load->stverts[vertindex].t;
				load->mesh[hdr->nummesh].facesfront = facesfront;

				hdr->nummesh++;
			}

			hdr->numindexes++;
		}
	}
}


void D3DAlias_OptimizeMesh (aliashdr_t *hdr, aliasload_t *load)
{
	// and now optimize it
	DWORD *remaptable = (DWORD *) TempHunk->FastAlloc (hdr->nummesh * sizeof (DWORD));
	DWORD *optresult = (DWORD *) TempHunk->FastAlloc ((hdr->nummesh > hdr->numtris ? hdr->nummesh : hdr->numtris) * sizeof (DWORD));

	aliasmesh_t *newmesh = (aliasmesh_t *) TempHunk->FastAlloc (hdr->nummesh * sizeof (aliasmesh_t));
	unsigned short *newindexes = (unsigned short *) TempHunk->FastAlloc (hdr->numindexes * sizeof (unsigned short));

	D3DXOptimizeFaces (load->indexes, hdr->numtris, hdr->nummesh, FALSE, optresult);

	for (int i = 0; i < hdr->numtris; i++)
	{
		int src = optresult[i] * 3;
		int dst = i * 3;

		newindexes[dst + 0] = load->indexes[src + 0];
		newindexes[dst + 1] = load->indexes[src + 1];
		newindexes[dst + 2] = load->indexes[src + 2];
	}

	D3DXOptimizeVertices (newindexes, hdr->numtris, hdr->nummesh, FALSE, optresult);

	for (int i = 0; i < hdr->nummesh; i++)
	{
		Q_MemCpy (&newmesh[i], &load->mesh[optresult[i]], sizeof (aliasmesh_t));
		remaptable[optresult[i]] = i;
	}

	// do the index remap
	for (int i = 0; i < hdr->numindexes; i++)
		load->indexes[i] = remaptable[newindexes[i]];

	// point the original mesh to the optimized version
	load->mesh = newmesh;
}


void D3DAlias_MakeAliasMesh (char *name, aliashdr_t *hdr, aliasload_t *load)
{
	// see is it currently in use
	for (int i = 0; i < aliasbuffer_t::NumBuffers; i++)
	{
		// buffers which were released are not valid
		if (!d3d_AliasBuffers[i].Blends) continue;
		if (!d3d_AliasBuffers[i].Indexes) continue;
		if (!d3d_AliasBuffers[i].Positions) continue;
		if (!d3d_AliasBuffers[i].Texcoords) continue;

		// this doesn't need a checksum as models are unique to a gamedir (...unlike textures...) and buffer sets are cleared on game change
		if (!strcmp (d3d_AliasBuffers[i].Name, name))
		{
			// re-mark as live so that it won't be released
			d3d_AliasBuffers[i].RegistrationSequence = d3d_RenderDef.RegistrationSequence;

			// restore the info we need to draw it
			hdr->nummesh = d3d_AliasBuffers[i].NumMesh;
			hdr->numindexes = d3d_AliasBuffers[i].NumIndexes;

			// this MDL is now using this buffer
			hdr->buffer = i;
			return;
		}
	}

	// all memory in here is going on the temp hunk
	int hunkmark = TempHunk->GetLowMark ();

	load->mesh = (aliasmesh_t *) TempHunk->FastAlloc (hdr->numtris * 3 * sizeof (aliasmesh_t));
	load->indexes = (unsigned short *) TempHunk->FastAlloc (3 * sizeof (unsigned short) * hdr->numtris);

	// compress and optimize the mesh
	D3DAlias_CompressMesh (hdr, load);
	D3DAlias_OptimizeMesh (hdr, load);

	// delerp muzzle-flash verts
	DelerpMuzzleFlashes (hdr, load);

	// create the buffer set from the generated mesh data
	D3DAlias_MakeMeshBuffers (hdr, load, D3DAlias_GetMeshBuffer (hdr));

	// store out the info needed for drawing so that cache is valid
	strcpy (d3d_AliasBuffers[hdr->buffer].Name, name);
	d3d_AliasBuffers[hdr->buffer].NumMesh = hdr->nummesh;
	d3d_AliasBuffers[hdr->buffer].NumIndexes = hdr->numindexes;

	// and free our hunk memory
	TempHunk->FreeToLowMark (hunkmark);
}


// fix terminal spellcheck failure in the original cvar
cvar_alias_t gl_doubleeyes_spellcheck_failure ("gl_doubleeyes", &gl_doubleeyes);
extern cvar_t r_lerpframe;
extern cvar_t r_lerporient;


void D3DAlias_TransformStandard (entity_t *ent)
{
	ent->matrix.Identity ();
	ent->matrix.Translate (ent->lerporigin);
	ent->matrix.Rotate (ent->lerpangles[1], -ent->lerpangles[0], ent->lerpangles[2]);
}


// this was c&p from fitz so it needs to be extern for license compatibility crap
extern QMATRIX r_shadowmatrix;

void D3DAlias_TransformShadowed (entity_t *ent)
{
	ent->matrix.Identity ();
	ent->matrix.Translate (ent->lerporigin);

	ent->matrix.Translate (0, 0, -(ent->lerporigin[2] - ent->lightinfo.lightspot[2]));
	ent->matrix.Mult (&r_shadowmatrix);
	ent->matrix.Translate (0, 0, (ent->lerporigin[2] - ent->lightinfo.lightspot[2]));

	ent->matrix.Rotate (ent->lerpangles[1], 0, 0);
}


void D3DAlias_UpdateInstance (entity_t *ent, aliasinstance_t *inst)
{
	// suitable for both writing to a mapped buffer or to system memory
	QMATRIX m (&d3d_ModelViewProjMatrix);

	m.Mult (&inst->ModelMatrix, &ent->matrix);

	inst->shadelight[0] = ent->lightinfo.shadelight[0];
	inst->shadelight[1] = ent->lightinfo.shadelight[1];
	inst->shadelight[2] = ent->lightinfo.shadelight[2];

	if (ent->alphaval > 0 && ent->alphaval < 255)
		inst->shadelight[3] = (float) ent->alphaval / 255.0f;
	else inst->shadelight[3] = 1.0f;

	inst->shadevector[0] = ent->lightinfo.shadevector[0];
	inst->shadevector[1] = ent->lightinfo.shadevector[1];
	inst->shadevector[2] = ent->lightinfo.shadevector[2];

	inst->blend = ent->poselerp.blend;
}


void D3DAlias_UpdateConstants (entity_t *ent)
{
	aliasinstance_t ThisInstance;

	D3DAlias_UpdateInstance (ent, &ThisInstance);
	d3d11_Context->UpdateSubresource (d3d_MeshConstants, 0, NULL, &ThisInstance, 0, 0);
	d3d11_State->VSSetConstantBuffer (1, d3d_MeshConstants);
}


void D3DAlias_SetShadersAndTextures (entity_t *ent, QTEXTURE *teximage, QTEXTURE *lumaimage, int flags = 0)
{
	if (D3DMisc_OverridePS ()) return;

	if (flags & AM_DRAWSHADOW)
	{
		// shadows are simpler
		if (RealFogDensity > 0)
			d3d11_State->PSSetShader (d3d_ShadowPixelShaders[1]);
		else d3d11_State->PSSetShader (d3d_ShadowPixelShaders[0]);
	}
	else
	{
		int ShaderFlags = (RealFogDensity > 0) ? ALIASSHADER_FOG : 0;

		if (r_lightmap.value)
		{
			teximage = &QTEXTURE::GreyTexture;
			lumaimage = NULL;
		}
		else if ((ent->model->flags & EF_PLAYER) && ent->cmapimage && !gl_nocolors.value)
		{
			// convert player skin colours back to 0..15 range each
			// the new palette texture will automatically handle backward ranges
			int shirt = (ent->playerskin & 0xf0) >> 4;
			int pants = ent->playerskin & 15;

			// the colormap must always go in tmu2
			d3d11_State->PSSetTexture (2, ent->cmapimage);

			// each row of the palette is a separate texture to prevent bilerp bleeding
			d3d11_State->PSSetTexture (3, &d3d_PaletteRowTextures[shirt]);	// and the palette into tmu3
			d3d11_State->PSSetTexture (4, &d3d_PaletteRowTextures[pants]);	// and the palette into tmu4

			ShaderFlags |= ALIASSHADER_PLAYER;
		}

		d3d11_State->PSSetTexture (0, teximage);
		d3d11_State->PSSetSampler (0, d3d_DefaultSamplerClamp);

		if (lumaimage && gl_fullbrights.integer)
		{
			d3d11_State->PSSetTexture (1, lumaimage);

			// select luma pixel shader
			if (r_additivefullbrights.value)
				ShaderFlags |= ALIASSHADER_ADDLUMA;
			else ShaderFlags |= ALIASSHADER_STDLUMA;
		}

		d3d11_State->PSSetShader (d3d_AliasPixelShaders[ShaderFlags]);
	}
}


void D3DAlias_Transform (entity_t *ent, aliashdr_t *hdr, int flags = 0)
{
	// build the transforms for the entity
	if (flags & AM_DRAWSHADOW)
		D3DAlias_TransformShadowed (ent);
	else D3DAlias_TransformStandard (ent);

	if (hdr->drawflags & AM_EYES)
	{
		// the scaling needs to be included at this time
		float sc = gl_doubleeyes.value + 1.0f;
		float trans[3] = {hdr->midpoint[0] * gl_doubleeyes.value, hdr->midpoint[1] * gl_doubleeyes.value, hdr->midpoint[2] * gl_doubleeyes.value};

		// fix this up properly
		QMATRIX eyematrix (sc, 0, 0, 0, 0, sc, 0, 0, 0, 0, sc, 0, -trans[0], -trans[1], -trans[2], 1);

		ent->matrix.Mult (&eyematrix);
	}
}


void D3DAlias_SetVertexState (entity_t *ent, aliashdr_t *hdr, int flags)
{
	aliasbuffer_t *buf = &d3d_AliasBuffers[hdr->buffer];

	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	d3d11_State->IASetIndexBuffer (buf->Indexes, DXGI_FORMAT_R16_UINT, 0);

	d3d11_State->IASetVertexBuffer (0, buf->Positions, sizeof (aliasposition_t), ent->curr.pose * hdr->nummesh * sizeof (aliasposition_t));
	d3d11_State->IASetVertexBuffer (1, buf->Positions, sizeof (aliasposition_t), ent->prev.pose * hdr->nummesh * sizeof (aliasposition_t));
	d3d11_State->IASetVertexBuffer (2, buf->Texcoords, sizeof (aliastexcoord_t), 0);

	if (flags & AM_VIEWMODEL)
	{
		d3d11_State->IASetVertexBuffer (3, buf->Blends, sizeof (aliasblend_t), 0);
		d3d11_State->IASetInputLayout (d3d_ViewModelLayout);
		d3d11_State->VSSetShader (d3d_ViewModelVertexShader);
	}
	else if (flags & AM_INSTANCED)
	{
		d3d11_State->IASetVertexBuffer (3, QINSTANCE::VertexBuffer, sizeof (aliasinstance_t), QINSTANCE::MapOffset);
		d3d11_State->IASetInputLayout (d3d_InstancedLayout);
		d3d11_State->VSSetShader (d3d_InstancedVertexShader);
	}
	else
	{
		d3d11_State->IASetInputLayout (d3d_MeshLayout);
		d3d11_State->VSSetShader (d3d_MeshVertexShader);
	}
}


bool D3DAlias_BreakBatch (entity_t *ent, entity_t *prev)
{
	// no previous batch
	if (!prev) return false;

	// if any of these change we must begin a new batch as they will change the per-instance data
	if (ent->model != prev->model) return true;
	if (ent->curr.pose != prev->curr.pose) return true;
	if (ent->prev.pose != prev->prev.pose) return true;
	if (ent->teximage != prev->teximage) return true;
	if (ent->lumaimage != prev->lumaimage) return true;
	if (ent->cmapimage != prev->cmapimage) return true;

	// no change
	return false;
}


struct instancebatch_t
{
	entity_t *ent;
	aliashdr_t *hdr;
	QTEXTURE *TexImage;
	QTEXTURE *LumaImage;
	int NumIndexes;
	int FirstInstance;
	int NumInstances;
};


void D3DAlias_WriteInstancedBatch (instancebatch_t *Batch, entity_t *ent, aliashdr_t *hdr)
{
	Batch->ent = ent;
	Batch->hdr = hdr;
	Batch->TexImage = ent->teximage;
	Batch->LumaImage = ent->lumaimage;
	Batch->NumIndexes = hdr->numindexes;
}


void D3DAlias_DrawUninstanced (entity_t *ent, aliashdr_t *hdr, int flags)
{
	D3DAlias_Transform (ent, hdr, flags);
	D3DAlias_SetVertexState (ent, hdr, flags);
	D3DAlias_SetShadersAndTextures (ent, ent->teximage, ent->lumaimage, flags);
	D3DAlias_UpdateConstants (ent);

	D3DMisc_DrawIndexedCommon (hdr->numindexes);
	d3d_RenderDef.alias_polys += hdr->numtris;
}


void D3DAlias_DrawAliasBatch (entity_t **ents, int numents, int flags = 0)
{
	if (flags & AM_DRAWSHADOW)
	{
		d3d11_State->OMSetBlendState (d3d_AlphaBlendEnable);
		d3d11_State->OMSetDepthStencilState (d3d_ShadowStencil, 0x00000001);
	}

	// we map space for up to numents entities but the actual number written may be less
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	D3D11_MAP MapType = D3D11_MAP_WRITE_NO_OVERWRITE;
	int MapSize = CACHE_ALIGN (numents * sizeof (aliasinstance_t));

	if (QINSTANCE::MapOffset + MapSize >= QINSTANCE::BufferMax)
	{
		MapType = D3D11_MAP_WRITE_DISCARD;
		QINSTANCE::MapOffset = 0;
	}

	if (FAILED (d3d11_Context->Map (QINSTANCE::VertexBuffer, 0, MapType, 0, &MappedResource)))
	{
		Con_Printf ("D3DAlias_DrawAliasBatch : Failed to Map instance buffer\n");
		return;
	}

	entity_t *lastent = NULL;
	aliashdr_t *lasthdr = NULL;

	int hunkmark = TempHunk->GetLowMark ();

	// each ent may in theory need a new batch so we must alloc space for them all
	instancebatch_t *InstanceBatches = (instancebatch_t *) TempHunk->FastAlloc (numents * sizeof (instancebatch_t));
	aliasinstance_t *AliasInstances = (aliasinstance_t *) (((byte *) MappedResource.pData) + QINSTANCE::MapOffset);

	int NumInstanceBatches = 0;
	int NumAliasInstances = 0;

	// init the first instance
	InstanceBatches[0].FirstInstance = 0;
	InstanceBatches[0].NumInstances = 0;

	for (int i = 0; i < numents; i++)
	{
		entity_t *ent = ents[i];

		// check for skipping
		if (flags & AM_DRAWSHADOW)
		{
			// don't draw shadows here
			if (ent->alphaval > 0 && ent->alphaval < 255) continue;
			if (!ent->lightinfo.lightplane) continue;
			if (ent->model->aliashdr->drawflags & AM_NOSHADOW) continue;
		}
		else
		{
			// prydon gets this
			if (!ent->teximage) continue;
		}

		// check if state changes are needed and go to a new batch if so
		if (D3DAlias_BreakBatch (ent, lastent))
		{
			// update the previous instance batch with the previous values
			D3DAlias_WriteInstancedBatch (&InstanceBatches[NumInstanceBatches++], lastent, lasthdr);

			// init the new batch
			InstanceBatches[NumInstanceBatches].FirstInstance = NumAliasInstances;
			InstanceBatches[NumInstanceBatches].NumInstances = 0;
		}

		// retrieve the mdl header
		aliashdr_t *hdr = ent->model->aliashdr;

		// add an instance for this entity
		D3DAlias_Transform (ent, hdr, flags);
		D3DAlias_UpdateInstance (ent, &AliasInstances[NumAliasInstances++]);

		// add to the current instance batch
		InstanceBatches[NumInstanceBatches].NumInstances++;

		// accumulate crap
		d3d_RenderDef.alias_polys += hdr->numtris;
		lastent = ent;
		lasthdr = hdr;
	}

	// terminate the final batch
	D3DAlias_WriteInstancedBatch (&InstanceBatches[NumInstanceBatches++], lastent, lasthdr);

	// and unmap
	d3d11_Context->Unmap (QINSTANCE::VertexBuffer, 0);

	// now iterate through them and draw them all
	for (int i = 0; i < NumInstanceBatches; i++)
	{
		instancebatch_t *Batch = &InstanceBatches[i];

		// it's possible that we accumulated nothing
		if (!Batch->NumInstances) continue;

		D3DAlias_SetVertexState (Batch->ent, Batch->hdr, flags | AM_INSTANCED);
		D3DAlias_SetShadersAndTextures (Batch->ent, Batch->TexImage, Batch->LumaImage, flags | AM_INSTANCED);
		D3DMisc_DrawIndexedInstancedCommon (Batch->NumIndexes, Batch->NumInstances, 0, 0, Batch->FirstInstance);
	}

	// advance the mapping offset by the number of instances actually written
	QINSTANCE::MapOffset += CACHE_ALIGN (NumAliasInstances * sizeof (aliasinstance_t));

	// and done
	TempHunk->FreeToLowMark (hunkmark);

	if (flags & AM_DRAWSHADOW)
	{
		// back to normal (we never draw shadows on translucent models so it's OK to put them back this way)
		d3d11_State->OMSetBlendState (NULL);
		d3d11_State->OMSetDepthStencilState (d3d_DepthTestAndWrite);
	}
}


void D3DAlias_BeginLerp (lerpinfo_t *info, float baseblend)
{
	info->starttime = cl.time;
	info->blend = baseblend;
}


void D3DAlias_BoundLerp (lerpinfo_t *info)
{
	if (cl.paused) info->blend = 0;
	if (info->blend > 1) info->blend = 1;
	if (info->blend < 0) info->blend = 0;
}


// "new" is a keyword... :P
void D3DAlias_OrientLerp (entity_t *ent, lerpinfo_t *info, float *prev, float *curr, float *neu, int lerpflag, float delta)
{
	if (ent->lerpflags & lerpflag)
	{
		D3DAlias_BeginLerp (info, 1);

		Vector3Copy (prev, neu);
		Vector3Copy (curr, neu);

		ent->lerpflags &= ~lerpflag;
	}
	else
	{
		float vec[3];

		Vector3Subtract (vec, neu, curr);

		if (Vector3Dot (vec, vec) >= delta)
		{
			D3DAlias_BeginLerp (info, 0);

			Vector3Copy (prev, curr);
			Vector3Copy (curr, neu);
		}
		else info->blend = (cl.time - info->starttime) / 0.1;
	}

	// don't let blends pass 1
	D3DAlias_BoundLerp (info);
}


// split out for use by IQMs too - should this move to a different module? (misc?  model?  main?)
// pose needs to be separated from ent owing to alias framegroups
void D3DAlias_LerpToFrame (entity_t *ent, int pose, float interval)
{
	if (!r_lerpframe.value) ent->lerpflags |= LERP_RESETFRAME;
	if (!r_lerporient.value) ent->lerpflags |= LERP_RESETMOVE;
	if (!(ent->lerpflags & LERP_MOVESTEP)) ent->lerpflags |= LERP_RESETMOVE;

	// buncha hacks to fix up buggy Quake models
	if (pose == 0 && ent == &cl.viewent) ent->lerpflags |= LERP_RESETFRAME;
	if (ent->curr.pose == ent->prev.pose && ent->curr.pose == 0 && ent != &cl.viewent) ent->lerpflags |= LERP_RESETFRAME;

	if (ent->lerpflags & LERP_RESETFRAME)
	{
		D3DAlias_BeginLerp (&ent->poselerp, 1);

		ent->prev.pose = pose;
		ent->curr.pose = pose;

		ent->lerpflags &= ~LERP_RESETFRAME;
	}
	else if (pose != ent->curr.pose)
	{
		D3DAlias_BeginLerp (&ent->poselerp, 0);

		ent->prev.pose = ent->curr.pose;
		ent->curr.pose = pose;
	}
	else ent->poselerp.blend = (cl.time - ent->poselerp.starttime) / interval;

	// don't let blends pass 1
	D3DAlias_BoundLerp (&ent->poselerp);

	// and now do the same for origin and angles
	D3DAlias_OrientLerp (ent, &ent->originlerp, ent->prev.origin, ent->curr.origin, ent->origin, LERP_RESETORIGIN, 0.125f);
	D3DAlias_OrientLerp (ent, &ent->angleslerp, ent->prev.angles, ent->curr.angles, ent->angles, LERP_RESETANGLES, 1.4f);
}


void D3DAlias_SetupFrame (entity_t *ent, aliashdr_t *hdr)
{
	float frame_interval = 0.1f;
	int framenum = ((unsigned) ent->frame) % hdr->numframes;
	maliasframedesc_t *frame = &hdr->frames[framenum];
	int posenum = frame->firstpose;

	if (frame->numposes > 1)
	{
		int addpose = Mod_AnimateGroup (ent, frame->intervals, frame->numposes);

		if (addpose > 0)
			frame_interval = frame->intervals[addpose] - frame->intervals[addpose - 1];
		else frame_interval = frame->intervals[0];

		// go to the new pose
		posenum += addpose;
	}
	else if (ent->lerpflags & LERP_FINISH)
		frame_interval = ent->lerpinterval;
	else frame_interval = 0.1f;

	D3DAlias_LerpToFrame (ent, posenum, frame_interval);
}


void D3DAlias_SetupAliasModel (entity_t *ent)
{
	// take pointers for easier access
	aliashdr_t *hdr = ent->model->aliashdr;

	// assume that the model has been culled
	ent->visframe = -1;

	// the gun or the chase model are never culled away
	if (ent == cls.entities[cl.viewentity] && chase_active.value)
		;	// no bbox culling on certain entities
	else if (ent->nocullbox)
		;	// no bbox culling on certain entities
	else if (R_CullBox (&ent->cullinfo))
	{
		ent->lerpflags |= LERP_RESETALL;
		return;
	}

	// the model has not been culled now
	ent->visframe = d3d_RenderDef.framecount;
	ent->lightinfo.lightplane = NULL;

	// get lighting information
	D3DLight_LightPoint (&ent->lightinfo, ent->lerporigin);

	// get texturing info from the proper skin
	int skinnum = ((unsigned) ent->skinnum) % hdr->numskins;
	aliasskin_t *skin = &hdr->skins[skinnum];
	int anim = (skin->type == ALIAS_SKIN_GROUP) ? Mod_AnimateGroup (ent, skin->intervals, skin->numskins) : 0;

	// and now save them out to the state for this entity
	ent->teximage = skin->teximage[anim];
	ent->lumaimage = skin->lumaimage[anim];

	// nehahra uses player.mdl for non-players :(
	if (ent->entnum >= 1 && ent->entnum <= cl.maxclients && (ent->model->flags & EF_PLAYER))
		ent->cmapimage = skin->cmapimage[anim];
	else ent->cmapimage = NULL;

	// build the sort order
	ent->sortpose[0] = ent->curr.pose;
	ent->sortpose[1] = ent->prev.pose;
}


QEDICTLIST d3d_AliasEdicts;


int D3DAlias_ModelSortFunc (entity_t **e1, entity_t **e2)
{
	// sort so that the same pose is likely to be used more often
	// (should this use current poses instead of the cached poses???
	if (e1[0]->model == e2[0]->model)
		return (e1[0]->sortorder - e2[0]->sortorder);
	return (int) (e1[0]->model - e2[0]->model);
}


void D3DAlias_RenderAliasModels (void)
{
	// if (NumOccluded) Con_Printf ("occluded %i\n", NumOccluded);
	if (!d3d_AliasEdicts.NumEdicts) return;

	// sort the alias edicts by model and poses
	// (to do - chain these in a list instead to save memory, remove limits and run faster...)
	qsort (d3d_AliasEdicts.Edicts, d3d_AliasEdicts.NumEdicts, sizeof (entity_t *), (sortfunc_t) D3DAlias_ModelSortFunc);

	// draw in two passes to prevent excessive shader switching
	D3DAlias_DrawAliasBatch (d3d_AliasEdicts.Edicts, d3d_AliasEdicts.NumEdicts);

	if (r_shadows.value > 0)
		D3DAlias_DrawAliasBatch (d3d_AliasEdicts.Edicts, d3d_AliasEdicts.NumEdicts, AM_DRAWSHADOW);
}


void D3DAlias_AddEdict (entity_t *ent)
{
	D3DAlias_SetupAliasModel (ent);

	if (ent->visframe != d3d_RenderDef.framecount) return;

	if (ent->alphaval > 0 && ent->alphaval < 255)
		D3DAlpha_AddToList (ent);
	else d3d_AliasEdicts.AddEntity (ent);
}


float SCR_CalcFovX (float fov_y, float width, float height);
float SCR_CalcFovY (float fov_x, float width, float height);
void SCR_SetFOV (float *fovx, float *fovy, float fovvar, int width, int height, bool guncalc);
void D3DIQM_DrawIQM (entity_t *ent);
void D3DLight_SetLightPointFlags (entity_t *ent);

// fixme - shouldn't this be the first thing drawn in the frame so that a lot of geometry can get early-z???
void D3DAlias_DrawViewModel (int passnum)
{
	// conditions for switching off view model
	if (chase_active.value) return;
	if (cl.stats[STAT_HEALTH] <= 0) return;
	if (!cl.viewent.model) return;
	if (!r_drawentities.value) return;
	if (r_drawviewmodel.value <= 0) return;

	// select view ent
	entity_t *ent = &cl.viewent;
	aliashdr_t *hdr = ent->model->aliashdr;

	// the viewmodel should always be an alias model
	if (ent->model->type != mod_alias) return;

	// never check for bbox culling on the viewmodel
	ent->nocullbox = true;

	if ((cl.items & IT_INVISIBILITY) || r_drawviewmodel.value < 1.0f)
	{
		// if it's the solid pass then don't draw it yet
		if (!passnum) return;

		// initial alpha
		ent->alphaval = (int) (r_drawviewmodel.value * 255.0f);

		// adjust for invisibility and take to final range; if the ent is fully transparent then don't bother drawing it
		if (cl.items & IT_INVISIBILITY) ent->alphaval >>= 1;
		if ((ent->alphaval = BYTE_CLAMP (ent->alphaval)) < 1) return;

		// enable blending
		d3d11_State->OMSetBlendState (d3d_AlphaBlendEnable);
	}
	else if (passnum)
		return;

	// recalculate the FOV here because the gun model might need different handling
	extern cvar_t scr_fov;

	float fov_x = 0;
	float fov_y = 0;

	// never go above 90 for the gun model FOV
	SCR_SetFOV (&fov_x, &fov_y, (scr_fov.value > 90.0f) ? 90.0f : scr_fov.value, vid.ref3dsize.width, vid.ref3dsize.height, true);

	// always done because it needs a scaling factor in m16[14] for depth hacking; this
	// allows a depth-range multiply of 0.5 which can be optimized better by the GPU.
	// we don't need to extract a new frustum as the gun is never frustum-culled
	D3DMain_SetupProjection (fov_x, fov_y, 4, 4096, 0.25f);

	// sets the flags for lighting the entity because it wasn't added to the visedicts list
	// (so it wouldn't have got them during the standard pass)
	D3DLight_SetLightPointFlags (ent);

	if (ent->model->type == mod_iqm)
	{
		D3DAlias_LerpToFrame (ent, ent->frame, 0.1f);
		D3DMain_ComputeEntityTransform (ent);
		D3DIQM_DrawIQM (ent);
	}
	else
	{
		D3DAlias_SetupFrame (ent, hdr);
		D3DMain_ComputeEntityTransform (ent);
		D3DAlias_SetupAliasModel (ent);
		D3DAlias_DrawUninstanced (ent, hdr, AM_VIEWMODEL);
	}

	// restore alpha
	ent->alphaval = 0;

	d3d11_State->OMSetBlendState (NULL);

	// the gun model may now be the first item drawn so we must restore the projection
	// (the frustum has already been extracted and is still valid)
	D3DMain_SetupProjection (vid.fov_x, vid.fov_y, 4, vid.farclip);
}

