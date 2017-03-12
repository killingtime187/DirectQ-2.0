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
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"
#include "particles.h"


QBUFFERFACTORY BufferFactory;


// all dynamic instance data shares the same buffer now, and the buffer is sized big enough for everything
#define MAX_INSTANCE_BUFFER		0x800000

ID3D11Buffer *QINSTANCE::VertexBuffer = NULL;
int QINSTANCE::BufferMax = 0;
int QINSTANCE::MapOffset = 0;


void D3DMisc_BeginDynamicVertexes (void)
{
	if (!QINSTANCE::VertexBuffer)
	{
		D3D11_BUFFER_DESC desc;

		BufferFactory.CreateVertexBuffer (1, MAX_INSTANCE_BUFFER, &QINSTANCE::VertexBuffer, "QINSTANCE::VertexBuffer");
		QINSTANCE::VertexBuffer->GetDesc (&desc);
		QINSTANCE::BufferMax = desc.ByteWidth;
	}
}


void D3DMisc_ShutdownDynamicVertexes (void)
{
	SAFE_RELEASE (QINSTANCE::VertexBuffer);
}


CD3DInitShutdownHandler d3d_DynamicVertexHandler ("dynamic vertexes", D3DMisc_BeginDynamicVertexes, D3DMisc_ShutdownDynamicVertexes);


QEDICTLIST::QEDICTLIST (void)
{
	this->Edicts = NULL;
	this->NumEdicts = 0;
}

QEDICTLIST::~QEDICTLIST (void)
{
	this->Edicts = NULL;
	this->NumEdicts = 0;
}

void QEDICTLIST::BeginFrame (void)
{
	this->Edicts = (entity_t **) TempHunk->FastAlloc (sizeof (entity_t *) * MAX_EDICTS);
	this->NumEdicts = 0;
}

void QEDICTLIST::AddEntity (entity_t *ent)
{
	if (!r_drawentities.value) return;
	if (!this->Edicts) return;
	if (this->NumEdicts >= MAX_EDICTS) return;

	this->Edicts[this->NumEdicts] = ent;
	this->NumEdicts++;
}


#define SHADOW_SKEW_X	-0.7f	// skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y	0.0f	// skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE	0.0f	// 0 = completely flat
#define SHADOW_HEIGHT	0.1f	// how far above the floor to render the shadow

QMATRIX r_shadowmatrix (
	1.0f,			0.0f,			0.0f,			0.0f,
	0.0f,			1.0f,			0.0f,			0.0f,
	SHADOW_SKEW_X,	SHADOW_SKEW_Y,	SHADOW_VSCALE,	0.0f,
	0.0f,			0.0f,			SHADOW_HEIGHT,	1.0f
);


float *D3DMisc_GetColorFromRGBA (byte *rgba)
{
	static float color[4];

	color[0] = (float) rgba[0] / 255.0f;
	color[1] = (float) rgba[1] / 255.0f;
	color[2] = (float) rgba[2] / 255.0f;
	color[3] = (float) rgba[3] / 255.0f;

	return color;
}


void D3DMisc_PositionFromBBox (float *position, float *mins, float *maxs)
{
	position[0] = mins[0] + (maxs[0] - mins[0]) * 0.5f;
	position[1] = mins[1] + (maxs[1] - mins[1]) * 0.5f;
	position[2] = mins[2] + (maxs[2] - mins[2]) * 0.5f;
}


bool D3DMisc_OverridePS (void)
{
	// test r_wireframe first because it must also override the rasterizer state
	// (which would cause wireframe lines to be coloured by drawflat if the other way around)
	if (r_wireframe.value)
	{
		d3d11_State->PSSetShader (d3d_WireFramePixelShader);
		return true;
	}
	else if (r_showdepth.value)
	{
		d3d11_State->PSSetShader (d3d_ShowDepthPixelShader);
		return true;
	}
	else if (r_drawflat.value)
	{
		d3d11_State->PSSetShader (d3d_DrawFlatPixelShader);
		return true;
	}
	else return false;
}


void D3DMisc_DrawIndexedCommon (UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
	d3d11_State->SynchronizeState ();
	d3d11_Context->DrawIndexed (IndexCount, StartIndexLocation, BaseVertexLocation);
	d3d_RenderDef.numdrawprim++;
}


void D3DMisc_DrawCommon (UINT VertexCount, UINT StartVertexLocation)
{
	d3d11_State->SynchronizeState ();
	d3d11_Context->Draw (VertexCount, StartVertexLocation);
	d3d_RenderDef.numdrawprim++;
}


void D3DMisc_DrawInstancedCommon (UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
	d3d11_State->SynchronizeState ();
	d3d11_Context->DrawInstanced (VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
	d3d_RenderDef.numdrawprim++;
}


void D3DMisc_DrawIndexedInstancedCommon (UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
	d3d11_State->SynchronizeState ();
	d3d11_Context->DrawIndexedInstanced (IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
	d3d_RenderDef.numdrawprim++;
}


cvar_t r_showbboxes ("r_showbboxes", "0");
cvar_t r_boxtest ("r_boxtest", "0");

struct r_bbvertex_t
{
	float xyz[3];
};


struct bboxinstance_t
{
	float Position[3];
	float Scale[3];
	unsigned colour;
};


struct d3d_bboxstate_t
{
	bboxinstance_t *BBoxInstances;
	int NumBBoxes;
};

d3d_bboxstate_t  d3d_BBoxState;


ID3D11Buffer *d3d_BBoxVertexes = NULL;
ID3D11Buffer *d3d_BBoxIndexes = NULL;
ID3D11InputLayout *d3d_BBoxLayout = NULL;
ID3D11VertexShader *d3d_BBoxVertexShader = NULL;
ID3D11PixelShader *d3d_BBoxPixelShader = NULL;
ID3D11RasterizerState *d3d_RSBBoxSolid = NULL;
ID3D11RasterizerState *d3d_RSBBoxFrame = NULL;


void D3DBBoxes_Create (void)
{
	r_bbvertex_t bboxverts[8];
	unsigned short bboxindexes[36] = {0, 2, 6, 0, 6, 4, 1, 3, 7, 1, 7, 5, 0, 1, 3, 0, 3, 2, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6};

	// and fill it in properly
	for (int i = 0; i < 8; i++)
	{
		bboxverts[i].xyz[0] = (i & 1) ? -1.0f : 1.0f;
		bboxverts[i].xyz[1] = (i & 2) ? -1.0f : 1.0f;
		bboxverts[i].xyz[2] = (i & 4) ? -1.0f : 1.0f;
	}

	BufferFactory.CreateVertexBuffer (sizeof (r_bbvertex_t), 8, &d3d_BBoxVertexes, "d3d_BBoxVertexes", bboxverts);
	BufferFactory.CreateIndexBuffer (sizeof (unsigned short), 36, &d3d_BBoxIndexes, "d3d_BBoxIndexes", bboxindexes);

	D3D11_INPUT_ELEMENT_DESC bboxlo[] =
	{
		MAKELAYOUTELEMENT ("POSITION",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0),
		MAKELAYOUTELEMENT ("BOXPOSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 1),
		MAKELAYOUTELEMENT ("BOXSCALE",    0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 1),
		MAKELAYOUTELEMENT ("COLOUR",      0, DXGI_FORMAT_R8G8B8A8_UNORM,  1, 1)
	};

	QSHADERFACTORY ShaderFactory (IDR_DRAWFX);

	ShaderFactory.CreateVertexShader (&d3d_BBoxVertexShader, "BBoxVS");
	ShaderFactory.CreateInputLayout (&d3d_BBoxLayout, "d3d_BBoxLayout", LAYOUTPARAMS (bboxlo));
	ShaderFactory.CreatePixelShader (&d3d_BBoxPixelShader, "BBoxPS");

	D3DState_CreateRasterizer (&d3d_RSBBoxSolid, "d3d_RSBBoxSolid", D3D11_FILL_SOLID, D3D11_CULL_NONE, 0, 0, 0);
	D3DState_CreateRasterizer (&d3d_RSBBoxFrame, "d3d_RSBBoxFrame", D3D11_FILL_WIREFRAME, D3D11_CULL_NONE, 0, 0, 0);
}


void D3DBBoxes_Release (void)
{
	SAFE_RELEASE (d3d_RSBBoxSolid);
	SAFE_RELEASE (d3d_RSBBoxFrame);
	SAFE_RELEASE (d3d_BBoxVertexes);
	SAFE_RELEASE (d3d_BBoxIndexes);
}


CD3DInitShutdownHandler d3d_BBoxHandler ("bbox", D3DBBoxes_Create, D3DBBoxes_Release);


bool D3DBBoxes_IsViewInside (entity_t *ent)
{
	if (r_refdef.vieworigin[0] < ent->cullinfo.mins[0]) return false;
	if (r_refdef.vieworigin[1] < ent->cullinfo.mins[1]) return false;
	if (r_refdef.vieworigin[2] < ent->cullinfo.mins[2]) return false;

	if (r_refdef.vieworigin[0] > ent->cullinfo.maxs[0]) return false;
	if (r_refdef.vieworigin[1] > ent->cullinfo.maxs[1]) return false;
	if (r_refdef.vieworigin[2] > ent->cullinfo.maxs[2]) return false;

	// inside
	return true;
}


void D3DBBoxes_Setup (void)
{
	// it's assumed that there will always be something to draw
	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	d3d11_State->IASetInputLayout (d3d_BBoxLayout);

	d3d11_State->IASetIndexBuffer (d3d_BBoxIndexes, DXGI_FORMAT_R16_UINT, 0);
	d3d11_State->IASetVertexBuffer (0, d3d_BBoxVertexes, sizeof (r_bbvertex_t), 0);

	d3d11_State->VSSetShader (d3d_BBoxVertexShader);
	d3d11_State->PSSetShader (d3d_BBoxPixelShader);

	d3d11_State->OMSetBlendState (d3d_AlphaBlendEnable);

	d3d_BBoxState.BBoxInstances = NULL;
	d3d_BBoxState.NumBBoxes = 0;
}


void D3DBBoxes_Shutdown (void)
{
	d3d11_State->OMSetDepthStencilState (d3d_DepthTestAndWrite);
	d3d11_State->OMSetBlendState (NULL);
	d3d11_State->RSSetState (d3d_RS3DView);
}


void D3DBBoxes_DrawBatch (void)
{
	if (d3d_BBoxState.BBoxInstances)
	{
		d3d11_Context->Unmap (QINSTANCE::VertexBuffer, 0);
		d3d_BBoxState.BBoxInstances = NULL;
	}

	if (d3d_BBoxState.NumBBoxes)
	{
		d3d11_State->SuspendCallback ();
		d3d11_State->IASetVertexBuffer (1, QINSTANCE::VertexBuffer, sizeof (bboxinstance_t), QINSTANCE::MapOffset);

		D3DMisc_DrawIndexedInstancedCommon (36, d3d_BBoxState.NumBBoxes);

		QINSTANCE::MapOffset += CACHE_ALIGN (d3d_BBoxState.NumBBoxes * sizeof (bboxinstance_t));
		d3d_BBoxState.NumBBoxes = 0;
		d3d11_State->ResumeCallback ();
	}
}


bool D3DBBoxes_GetBufferSpace (int numboxes)
{
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	D3D11_MAP MapType = D3D11_MAP_WRITE_NO_OVERWRITE;

	if (QINSTANCE::MapOffset + CACHE_ALIGN ((d3d_BBoxState.NumBBoxes + numboxes) * sizeof (bboxinstance_t)) >= QINSTANCE::BufferMax)
	{
		D3DBBoxes_DrawBatch ();
		MapType = D3D11_MAP_WRITE_DISCARD;
		QINSTANCE::MapOffset = 0;
	}

	if (!d3d_BBoxState.BBoxInstances)
	{
		if (FAILED (d3d11_Context->Map (QINSTANCE::VertexBuffer, 0, MapType, 0, &MappedResource)))
			return false;
		else d3d_BBoxState.BBoxInstances = (bboxinstance_t *) (&((byte *) MappedResource.pData)[QINSTANCE::MapOffset]);
	}

	return true;
}


void D3DBBoxes_ShowForType (QEDICTLIST *List)
{
	if (D3DBBoxes_GetBufferSpace (List->NumEdicts))
	{
		for (int i = 0; i < List->NumEdicts; i++)
		{
			entity_t *ent = List->Edicts[i];

			if (!ent->model) continue;
			if (ent->visframe != d3d_RenderDef.framecount) continue;
			if (D3DBBoxes_IsViewInside (ent)) continue;

			if (ent->model)
				D3DMisc_PositionFromBBox (d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Position, ent->cullinfo.mins, ent->cullinfo.maxs);
			else Vector3Copy (d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Position, ent->origin);

			Vector3Copy (d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale, ent->bboxscale);

			if (r_showbboxes.integer > 1)
				d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].colour = 0x60606060;
			else d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].colour = 0xffa0a0a0;

			d3d_BBoxState.NumBBoxes++;
		}
	}
}


#include "pr_class.h"

void D3DBBoxes_Show (void)
{
	if (r_showbboxes.value && r_drawentities.value)
	{
		D3DBBoxes_Setup ();

		d3d11_State->RSSetState ((r_showbboxes.integer > 1) ? d3d_RSBBoxSolid : d3d_RSBBoxFrame);
		d3d11_State->OMSetDepthStencilState (d3d_DepthTestNoWrite);

		if (r_showbboxes.value == 3 && sv.active)
		{
			// show the server-side boxes
			edict_t *ent = NextEdict (SVProgs->Edicts);

			if (D3DBBoxes_GetBufferSpace (SVProgs->NumEdicts - 1))
			{
				for (int i = 1; i < SVProgs->NumEdicts; i++, ent = NextEdict (ent))
				{
					if (ent->free) continue;
					if (ent == sv_player) continue;

					D3DMisc_PositionFromBBox (d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Position, ent->v.absmin, ent->v.absmax);

					if (ent->v.mins[0] == ent->v.maxs[0] && ent->v.mins[1] == ent->v.maxs[1] && ent->v.mins[2] == ent->v.maxs[2])
					{
						// point entity
						d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[0] = 8.0f;
						d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[1] = 8.0f;
						d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[2] = 8.0f;
						d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].colour = 0x60600060;
					}
					else
					{
						// box entity
						d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[0] = ((ent->v.maxs[0] - ent->v.mins[0]) * 0.5f) + 1.0f;
						d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[1] = ((ent->v.maxs[1] - ent->v.mins[1]) * 0.5f) + 1.0f;
						d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[2] = ((ent->v.maxs[2] - ent->v.mins[2]) * 0.5f) + 1.0f;

						if (ent->v.movetype == MOVETYPE_STEP)
							d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].colour = 0x60006060;
						else d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].colour = 0x60606060;
					}

					d3d_BBoxState.NumBBoxes++;
				}
			}
		}
		else
		{
			// show the client-side boxes
			D3DBBoxes_ShowForType (&d3d_AliasEdicts);
			D3DBBoxes_ShowForType (&d3d_BrushEdicts);
			D3DBBoxes_ShowForType (&d3d_MergeEdicts);
			D3DBBoxes_ShowForType (&d3d_IQMEdicts);
		}

		D3DBBoxes_DrawBatch ();
		D3DBBoxes_Shutdown ();
	}

	if (r_boxtest.integer > 0)
	{
		// just draws randomly positioned and scaled boxes as a stress test
		D3DBBoxes_Setup ();

		d3d11_State->RSSetState (d3d_RSBBoxSolid);
		d3d11_State->OMSetDepthStencilState (d3d_DisableDepthTest);

		// keep results consistent from frame to frame
		Q_randseed (666);

		for (int i = 0; i < r_boxtest.integer; i++)
		{
			if (D3DBBoxes_GetBufferSpace (1024))
			{
				for (int j = 0; j < 1024; j++)
				{
					d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Position[0] = (Q_fastrand () & 8191) - 4096;
					d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Position[1] = (Q_fastrand () & 8191) - 4096;
					d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Position[2] = (Q_fastrand () & 8191) - 4096;

					d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[0] = (Q_fastrand () & 7) + 8;
					d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[1] = (Q_fastrand () & 7) + 8;
					d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].Scale[2] = (Q_fastrand () & 7) + 8;

					d3d_BBoxState.BBoxInstances[d3d_BBoxState.NumBBoxes].colour = 0x60606060;

					d3d_BBoxState.NumBBoxes++;
				}
			}
		}

		D3DBBoxes_DrawBatch ();
		D3DBBoxes_Shutdown ();

		// re-seed the generator
		Q_randseed ((int) (Sys_DoubleTime () * 1000000.0));
	}
}


QCOLOR::QCOLOR (void)
{
	this->color = 0;
}


QCOLOR::QCOLOR (unsigned char _r, unsigned char _g, unsigned char _b)
{
	this->r = _r;
	this->g = _g;
	this->b = _b;
	this->a = 255;
}


QCOLOR::QCOLOR (unsigned char _r, unsigned char _g, unsigned char _b, unsigned char _a)
{
	this->r = _r;
	this->g = _g;
	this->b = _b;
	this->a = _a;
}


QCOLOR::QCOLOR (unsigned char *_rgba)
{
	this->r = _rgba[0];
	this->g = _rgba[1];
	this->b = _rgba[2];
	this->a = _rgba[3];
}


QCOLOR::QCOLOR (unsigned int _color)
{
	this->color = _color;
}


QCOLOR::QCOLOR (unsigned int *_palette, unsigned char _palindex)
{
	this->color = _palette[_palindex];
}


D3D11_BOX *D3DMisc_Box (int left, int right, int top, int bottom, int front, int back)
{
	static D3D11_BOX TheBox;

	TheBox.left = left;
	TheBox.right = right;
	TheBox.top = top;
	TheBox.bottom = bottom;
	TheBox.front = front;
	TheBox.back = back;

	return &TheBox;
}


// this is only enabled when I'm testing for resource leaks
//#define SETOBJECTNAMES

void D3DMisc_SetObjectName (ID3D11DeviceChild *pObject, char *name)
{
#ifdef SETOBJECTNAMES
#ifndef NDEBUG
	if (pObject)
	{
		// because the same object name can be used multiple times we must tag a number along with it
		static int namecount = 0;
		char fullname[256];

		// Only works if device is created with the D3D10 or D3D11 debug layer, or when attached to PIX for Windows
		Q_snprintf (fullname, 256, "%s_%i", name, ++namecount);
		pObject->SetPrivateData (WKPDID_D3DDebugObjectName, strlen (fullname), fullname);
	}
#endif
#endif
}


QVIEWPORT::QVIEWPORT (float x, float y, float w, float h, float zn, float zf)
{
	this->TopLeftX = x;
	this->TopLeftY = y;

	this->Width = w;
	this->Height = h;

	this->MinDepth = zn;
	this->MaxDepth = zf;
}


// the buffer factory ensures that the correct combination of usage/etc flags is always set
void QBUFFERFACTORY::CreateGenericBuffer (D3D11_USAGE usage, UINT bindflags, UINT access, UINT size, void *initdata, ID3D11Buffer **buf, char *name)
{
	// round cbuffer sizes to register size
	if (bindflags & D3D11_BIND_CONSTANT_BUFFER)
		size = (size + 15) & ~15;
	else size = CACHE_ALIGN (size);

    D3D11_BUFFER_DESC desc;

    desc.Usage = usage;
    desc.BindFlags = bindflags;
    desc.CPUAccessFlags = access;
    desc.MiscFlags = 0;
	desc.ByteWidth = size;
	desc.StructureByteStride = 0;

	if (initdata)
	{
		D3D11_SUBRESOURCE_DATA srd = {initdata, 0, 0};
		hr = d3d11_Device->CreateBuffer (&desc, &srd, buf);
	}
	else hr = d3d11_Device->CreateBuffer (&desc, NULL, buf);

	D3DMisc_SetObjectName (buf[0], name);
}


void QBUFFERFACTORY::CreateConstantBuffer (UINT size, ID3D11Buffer **buf, char *name, void *initdata)
{
	this->CreateGenericBuffer (
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_CONSTANT_BUFFER,
		0,
		size,
		initdata,
		buf,
		name);
}


void QBUFFERFACTORY::CreateVertexBuffer (UINT vertexsize, UINT numverts, ID3D11Buffer **buf, char *name, void *initdata)
{
	this->CreateGenericBuffer (
		initdata ? /*D3D11_USAGE_IMMUTABLE*/ D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC,
		D3D11_BIND_VERTEX_BUFFER,
		initdata ? 0 : D3D11_CPU_ACCESS_WRITE,
		vertexsize * numverts,
		initdata,
		buf,
		name);
}


void QBUFFERFACTORY::CreateIndexBuffer (UINT indexsize, UINT numindexes, ID3D11Buffer **buf, char *name, void *initdata)
{
	this->CreateGenericBuffer (
		initdata ? /*D3D11_USAGE_IMMUTABLE*/ D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC,
		D3D11_BIND_INDEX_BUFFER,
		initdata ? 0 : D3D11_CPU_ACCESS_WRITE,
		indexsize * numindexes,
		initdata,
		buf,
		name);
}


void QBUFFERFACTORY::CreateInstanceBuffer (UINT vertexsize, UINT numverts, ID3D11Buffer **buf, char *name)
{
	this->CreateGenericBuffer (
		D3D11_USAGE_DYNAMIC,
		D3D11_BIND_VERTEX_BUFFER,
		D3D11_CPU_ACCESS_WRITE,
		vertexsize * numverts,
		NULL,
		buf,
		name);
}


/*
============================================================================================================

		SETUP

============================================================================================================
*/

void D3DSky_LoadSkyBox (char *basename, bool feedback);


/*
==================
R_InitTextures
==================
*/
byte r_notexture_raw[sizeof (texture_t) + 32 * 32];

void D3DMisc_SetDetailNoiseSize (cvar_t *var);
void D3DMisc_SetDetailNoiseFrequency (cvar_t *var);

cvar_t r_detailnoisesize ("r_detailnoisesize", 128.0f, 0, D3DMisc_SetDetailNoiseSize);
cvar_t r_detailnoisefrequency ("r_detailnoisefrequency", 64.0f, 0, D3DMisc_SetDetailNoiseFrequency);

ID3D11Texture2D *d3d_NoiseTexture = NULL;
ID3D11ShaderResourceView *d3d_NoiseSRV = NULL;

byte *make2DNoiseTexture (int size, int freq);
byte *make3DNoiseTexture (int size, int freq);


void D3DMisc_UpdateDetailNoiseTexture (int size, int freq)
{
	int scaled_size, scaled_freq;

	// frequency must be a power of two; let's just enforce it for size too
	for (scaled_size = 1; scaled_size < size; scaled_size <<= 1);
	for (scaled_freq = 1; scaled_freq < freq; scaled_freq <<= 1);

	// scaled_freq cannot exceed half scaled_size
	if (scaled_size < 16) scaled_size = 16; else if (scaled_size > 1024) scaled_size = 1024;
	if (scaled_freq < 1) scaled_freq = 1; else if (scaled_freq > 1024) scaled_freq = 1024;

	SAFE_RELEASE (d3d_NoiseSRV);
	SAFE_RELEASE (d3d_NoiseTexture);

	// D3D11_BIND_RENDER_TARGET is needed for mip generation (STUPID!) which means it must also have D3D11_USAGE_DEFAULT
	CD3D11_TEXTURE2D_DESC desc (
		DXGI_FORMAT_R8G8B8A8_UNORM,
		scaled_size,
		scaled_size,
		1,
		0,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
		D3D11_USAGE_DEFAULT,
		0,
		1,
		0,
		D3D11_RESOURCE_MISC_GENERATE_MIPS
	);

	int hunkmark = TempHunk->GetLowMark ();
	byte *detailNoise = make2DNoiseTexture (scaled_size, scaled_freq);

	d3d11_Device->CreateTexture2D (&desc, NULL, &d3d_NoiseTexture);
	d3d11_Device->CreateShaderResourceView (d3d_NoiseTexture, NULL, &d3d_NoiseSRV);
	d3d11_Context->UpdateSubresource (d3d_NoiseTexture, 0, NULL, detailNoise, scaled_size * 4, 0);
	d3d11_Context->GenerateMips (d3d_NoiseSRV);

	TempHunk->FreeToLowMark (hunkmark);
}


void D3DMisc_Init (void)
{
	// create a simple checkerboard texture for the default
	memset (r_notexture_raw, 0, sizeof (r_notexture_raw));
	r_notexture_mip = (texture_t *) r_notexture_raw;

	r_notexture_mip->size[0] = r_notexture_mip->size[1] = 32;
	byte *dest = (byte *) (r_notexture_mip + 1);

	for (int y = 0; y < 32; y++)
	{
		for (int x = 0; x < 32; x++)
		{
			if ((y < 16) ^ (x < 16))
				*dest++ = 95;
			else *dest++ = 60;
		}
	}

	// set up correct animations
	r_notexture_mip->animframes[0] = r_notexture_mip;
	r_notexture_mip->animframes[1] = r_notexture_mip;

	// load the notexture properly
	r_notexture_mip->teximage = QTEXTURE::Load ("notexture", r_notexture_mip->size[0], r_notexture_mip->size[1], (byte *) (r_notexture_mip + 1), IMAGE_MIPMAP);
	r_notexture_mip->lumaimage = NULL;

	// build the initial detail noise texture
	D3DMisc_UpdateDetailNoiseTexture (r_detailnoisesize.integer, r_detailnoisefrequency.integer);
}


void D3DMisc_Shutdown (void)
{
	SAFE_RELEASE (d3d_NoiseSRV);
	SAFE_RELEASE (d3d_NoiseTexture);

	// ensure...
	r_notexture_mip = (texture_t *) r_notexture_raw;

	// these are released properly during texture shutdown so we just need to NULL them here
	r_notexture_mip->teximage = NULL;
	r_notexture_mip->lumaimage = NULL;
}


void D3DMisc_SetDetailNoiseSize (cvar_t *var) {D3DMisc_UpdateDetailNoiseTexture (r_detailnoisesize.integer, r_detailnoisefrequency.integer);}
void D3DMisc_SetDetailNoiseFrequency (cvar_t *var) {D3DMisc_UpdateDetailNoiseTexture (r_detailnoisesize.integer, r_detailnoisefrequency.integer);}


CD3DInitShutdownHandler d3d_MiscHandler ("misc", D3DMisc_Init, D3DMisc_Shutdown);


/*
===============
R_Init
===============
*/
cvar_t r_lerporient ("r_lerporient", "1", CVAR_ARCHIVE);
cvar_t r_lerpframe ("r_lerpframe", "1", CVAR_ARCHIVE);

// allow the old QER names as aliases
cvar_alias_t r_interpolate_model_animation ("r_interpolate_model_animation", &r_lerpframe);
cvar_alias_t r_interpolate_model_transform ("r_interpolate_model_transform", &r_lerporient);


void R_Init (void)
{
	ParticleSystem.InitParticles ();
}


/*
============================================================================================================

		NEW MAP

============================================================================================================
*/

/*
===============
R_NewMap
===============
*/
void S_InitAmbients (void);
void D3DSky_ParseWorldSpawn (void);


bool R_RecursiveLeafContents (mnode_t *node)
{
	if (node->contents == CONTENTS_SOLID) return true;
	if (node->visframe != d3d_RenderDef.visframecount) return true;

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		// update contents colour
		if (((mleaf_t *) node)->contents == d3d_RenderDef.viewleaf->contents)
			((mleaf_t *) node)->contentscolor = d3d_RenderDef.viewleaf->contentscolor;
		else
		{
			// don't cross contents boundaries
			return false;
		}

		// leaf visframes are never marked?
		((mleaf_t *) node)->visframe = d3d_RenderDef.visframecount;
		return true;
	}

	// go down both sides
	if (!R_RecursiveLeafContents (node->children[0])) return false;

	return R_RecursiveLeafContents (node->children[1]);
}


void D3DSurf_LeafVisibility (byte *vis);

void R_SetLeafContents (void)
{
	d3d_RenderDef.visframecount = -1;

	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++)
	{
		mleaf_t *leaf = &cl.worldmodel->brushhdr->leafs[i];

		// explicit NULLs
		if (leaf->contents == CONTENTS_EMPTY)
			leaf->contentscolor = NULL;
		else if (leaf->contents == CONTENTS_SOLID)
			leaf->contentscolor = NULL;
		else if (leaf->contents == CONTENTS_SKY)
			leaf->contentscolor = NULL;

		// no contents
		if (!leaf->contentscolor) continue;

		// go to a new visframe, reverse order so that we don't get mixed up with the main render
		d3d_RenderDef.visframecount--;
		d3d_RenderDef.viewleaf = leaf;

		// get pvs for this leaf
		byte *vis = Mod_LeafPVS (leaf, cl.worldmodel);

		// eval visibility
		D3DSurf_LeafVisibility (vis);

		// update leaf contents
		R_RecursiveLeafContents (cl.worldmodel->brushhdr->nodes);
	}

	d3d_RenderDef.visframecount = 0;
}


void LOC_LoadLocations (void);

extern byte *fatpvs;

void Con_RemoveConsole (void);
void Menu_RemoveMenu (void);
void Fog_ParseWorldspawn (void);
void IN_ClearMouseState (void);
void D3DAlias_InitForMap (void);
void D3DSprite_InitBuffers (void);
void D3DIQM_Init (void);
void Mod_InitForMap (model_t *mod);
void D3DBrush_Init (void);
void D3DLight_BuildAllLightmaps (void);
void Host_ResetTimers (void);
void D3DSurf_BuildWorldCache (void);
void ClearAllStates (void);
void V_NewMap (void);

void *memcpy_amd (void *dest, const void *src, size_t n);

void R_NewMap (void)
{
	// set up the pvs arrays (these will already have been done by the server if it's active
	if (!sv.active) Mod_InitForMap (cl.worldmodel);

	// init frame counters
	d3d_RenderDef.framecount = 1;
	d3d_RenderDef.visframecount = 0;

	// clear out efrags (one short???)
	for (int i = 0; i < cl.worldmodel->brushhdr->numleafs; i++)
		cl.worldmodel->brushhdr->leafs[i].efrags = NULL;

	// world entity baseline
	memset (&d3d_RenderDef.worldentity, 0, sizeof (entity_t));
	d3d_RenderDef.worldentity.model = cl.worldmodel;
	d3d_RenderDef.worldentity.alphaval = 255;

	// fix up the worldmodel so it's consistent and we can reuse code
	cl.worldmodel->brushhdr->bspmodel = false;
	cl.worldmodel->brushhdr->firstmodelsurface = 0;
	cl.worldmodel->brushhdr->nummodelsurfaces = cl.worldmodel->brushhdr->numsurfaces;

	// no viewpoint
	d3d_RenderDef.viewleaf = NULL;
	d3d_RenderDef.oldviewleaf = NULL;
	d3d_RenderDef.lastgoodcontents = NULL;

	// setup stuff
	ParticleSystem.ClearParticles ();

	// build lightmaps for this map (must be done before VBOs as lightmap texcoords will go into the VBO cache)
	D3DLight_BuildAllLightmaps ();

	QTEXTURE::Flush ();
	R_SetLeafContents ();
	D3DSky_ParseWorldSpawn ();
	Fog_ParseWorldspawn ();

	// setup vertex buffers for all of our model types
	D3DAlias_InitForMap ();
	D3DSprite_InitBuffers ();
	D3DIQM_Init ();

	// as sounds are now cleared between maps these sounds also need to be
	// reloaded otherwise the known_sfx will go out of sequence for them
	CL_InitTEnts ();
	S_InitAmbients ();
	LOC_LoadLocations ();
	D3DBrush_Init ();
	D3DSurf_BuildWorldCache ();

	// see do we need to switch off the menus or console
	if (key_dest != key_game && (cls.demoplayback || cls.demorecording || cls.timedemo))
	{
		Con_RemoveConsole ();
		Menu_RemoveMenu ();

		// switch to game
		key_dest = key_game;
	}

	// activate the mouse and flush the directinput buffers
	// (pretend we're fullscreen because we definitely want to hide the mouse here)
	ClearAllStates ();
	IN_SetMouseState (true);

	// reset these again here as they can be changed during load processing
	d3d_RenderDef.framecount = 1;
	d3d_RenderDef.visframecount = 0;

	// flush all the input buffers and go back to a default state
	IN_ClearMouseState ();

	// go to the next registration sequence
	d3d_RenderDef.RegistrationSequence++;

	// reinit the timers to keep fx consistent
	Host_ResetTimers ();

	// sync up all of our states
	D3DVid_FlushStates ();

	// reset view params
	V_NewMap ();

	// we're running a map now
	vid.RecalcRefdef = true;
	cls.maprunning = true;

	// release any remaining temp memory used for loading
	// this is a good place to decommit temp memory used for loading stuff and for running the previous map
	TempHunk->Free ();
}


void R_RenderView (void);

void R_TimeRefresh_f (void)
{
	if (cls.demoplayback) return;
	if (cls.timerefresh) return;

	cls.timerefresh = true;

	int numframes = 0;
	float startangle = r_refdef.viewangles[1];
	double start = Sys_DoubleTime ();
	float anglestep = (Cmd_Argc () == 1) ? 360 : atof (Cmd_Argv (1));

	if (anglestep < 2) anglestep = 2;

	for (int i = 0; i < anglestep; i++)
	{
		int hunkmark = TempHunk->GetLowMark ();

		// we can't draw to the front buffer in D3D so instead we just do a normal scene + swap
		if (D3DVid_BeginRendering ())
		{
			CL_PrepEntitiesForRendering ();

			vid.nopresent = false;
			r_refdef.viewangles[1] = (float) i / anglestep * 360.0;

			R_RenderView ();
			D3DVid_EndRendering ();

			numframes++;
		}

		TempHunk->FreeToLowMark (hunkmark);
	}

	cls.timerefresh = false;

	double time = Sys_DoubleTime () - start;

	Con_Printf ("%i frames in %f seconds (%f fps)\n", numframes, time, anglestep / time);

	r_refdef.viewangles[1] = startangle;
}


void R_ShowPipeline_f (void)
{
	D3D11_QUERY_DESC qdesc = {D3D11_QUERY_PIPELINE_STATISTICS, 0};
	ID3D11Query *d3d_PipeLineQuery = NULL;

	if (SUCCEEDED (d3d11_Device->CreateQuery (&qdesc, &d3d_PipeLineQuery)))
	{
		extern bool scr_drawmapshot;

		// just suppress 2D output so that we get correct stats for the 3D view (faked by pretending that we're doing a mapshot)
		scr_drawmapshot = true;
		vid.nopresent = true;
		d3d11_Context->Begin (d3d_PipeLineQuery);
		SCR_UpdateScreen ();
		d3d11_Context->End (d3d_PipeLineQuery);
		vid.nopresent = false;
		scr_drawmapshot = false;

		for (;;)
		{
			D3D11_QUERY_DATA_PIPELINE_STATISTICS stats;

			memset (&stats, 0, sizeof (D3D11_QUERY_DATA_PIPELINE_STATISTICS));
			hr = d3d11_Context->GetData (d3d_PipeLineQuery, &stats, sizeof (D3D11_QUERY_DATA_PIPELINE_STATISTICS), 0);

			if (hr == S_OK)
			{
				int framepixels = vid.ref3dsize.width * vid.ref3dsize.height;
				float overdraw = 100.0f * ((float) ((int) stats.PSInvocations - framepixels) / (float) framepixels);

				Con_Printf ("Vertexes read by IA:           %i\n", (int) stats.IAVertices);
				Con_Printf ("Primitives read by IA:         %i\n", (int) stats.IAPrimitives);
				Con_Printf ("Vertex shader invocations:     %i\n", (int) stats.VSInvocations);
				Con_Printf ("Primitives sent to Rasterizer: %i\n", (int) stats.CInvocations);
				Con_Printf ("Primitives rendered:           %i\n", (int) stats.CPrimitives);
				Con_Printf ("Pixel shader invocations:      %i\n", (int) stats.PSInvocations);
				Con_Printf ("Overdraw:                      %0.2f%%\n", overdraw);
				break;
			}
			else if (FAILED (hr))
			{
				Con_Printf ("R_ShowPipeline_f : ID3D11DeviceContext::GetData failed\n");
				break;
			}
		}

		SAFE_RELEASE (d3d_PipeLineQuery);
	}
	else Con_Printf ("R_ShowPipeline_f : could not create D3D11_QUERY_PIPELINE_STATISTICS\n");
}


cmd_t r_timerefresh ("timerefresh", R_TimeRefresh_f);
cmd_t r_showpipelinestats ("r_showpipelinestats", R_ShowPipeline_f);

