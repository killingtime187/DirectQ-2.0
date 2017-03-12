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
#include "resource.h"
#include "particles.h"

#define PARTSHADER_SQUARE		1
#define PARTSHADER_FOG			2
#define PARTSHADER_CORONA		4

shaderdefine_t PartDefines[] =
{
	ENCODE_DEFINE (PARTSHADER_SQUARE, "1"),
	ENCODE_DEFINE (PARTSHADER_FOG, "1"),
	ENCODE_DEFINE (PARTSHADER_CORONA, "1")
};


int r_particleframe = -1;

ID3D11InputLayout *d3d_PartInputLayout;
ID3D11VertexShader *d3d_PartVertexShader;
ID3D11Buffer *d3d_PartConstants = NULL;
ID3D11PixelShader *d3d_PartPixelShaders[8];
ID3D11BlendState *d3d_CoronaBlendEnable = NULL;

// if this changes be sure to check out R_BenchmarkParticles in cl_particles.cpp!!!!
#define MAX_DRAW_PARTICLES	8193

extern cvar_t sv_gravity;
extern cvar_t r_lightscale;

float r_particlescale = 0;
float r_particlegravity = 0;
float r_particledist = 0;

cvar_t r_particlesize ("r_particlesize", "1", CVAR_ARCHIVE);
cvar_t r_drawparticles ("r_drawparticles", "1", CVAR_ARCHIVE);
cvar_alias_t r_particles ("r_particles", &r_drawparticles);
cvar_t r_particlestyle ("r_particlestyle", "0", CVAR_ARCHIVE);
cvar_t r_particledistscale ("r_particledistscale", "0.004", CVAR_ARCHIVE);
cvar_t r_gpuparticles ("r_gpuparticles", "1", CVAR_ARCHIVE);


struct partcbuf_t
{
	float r_vpn[3];
	float distscale;
	float scale;
	float sv_gravity;
	float cltime;
	float lightscale;
	float r_voffsets[4][4];
};


struct d3d_partstate_t
{
	partvert_t *Particles;
	int NumParticles;
};

d3d_partstate_t d3d_PartState;

void D3DPart_Init (void)
{
	D3DState_CreateBlend (&d3d_CoronaBlendEnable, "d3d_CoronaBlendEnable", TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE);

	static D3D11_INPUT_ELEMENT_DESC partlo[] =
	{
		MAKELAYOUTELEMENT ("POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 1),
		MAKELAYOUTELEMENT ("VEL",       0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 1),
		MAKELAYOUTELEMENT ("DVEL",      0, DXGI_FORMAT_R32G32_FLOAT,    0, 1),
		MAKELAYOUTELEMENT ("TIME",      0, DXGI_FORMAT_R32_FLOAT,       0, 1),
		MAKELAYOUTELEMENT ("GRAV",      0, DXGI_FORMAT_R32_FLOAT,       0, 1),
		MAKELAYOUTELEMENT ("SCALE",     0, DXGI_FORMAT_R32_FLOAT,       0, 1),
		MAKELAYOUTELEMENT ("RAMP",      0, DXGI_FORMAT_R32_UINT,        0, 1),
		MAKELAYOUTELEMENT ("RAMPTIME",  0, DXGI_FORMAT_R32_FLOAT,       0, 1),
		MAKELAYOUTELEMENT ("BASERAMP",  0, DXGI_FORMAT_R32_FLOAT,       0, 1),
		MAKELAYOUTELEMENT ("PARTCOLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 1),
		MAKELAYOUTELEMENT ("DIE",       0, DXGI_FORMAT_R32_FLOAT,       0, 1)
	};

	QSHADERFACTORY ShaderFactory (IDR_PARTICLESFX);
	D3D10_SHADER_MACRO *Defines = NULL;

	ShaderFactory.CreateVertexShader (&d3d_PartVertexShader, "PartVS");
	ShaderFactory.CreateInputLayout (&d3d_PartInputLayout, "d3d_PartInputLayout", LAYOUTPARAMS (partlo));

	for (int i = 0; i < ARRAYLENGTH (d3d_PartPixelShaders); i++)
	{
		// both cannot be set
		if ((i & PARTSHADER_SQUARE) && (i & PARTSHADER_CORONA)) continue;
		if ((i & PARTSHADER_FOG) && (i & PARTSHADER_CORONA)) continue;

		Defines = ShaderFactory.EncodeDefines (PartDefines, ARRAYLENGTH (PartDefines), i);
		ShaderFactory.CreatePixelShader (&d3d_PartPixelShaders[i], "PartPS", Defines);
	}

	BufferFactory.CreateConstantBuffer (sizeof (partcbuf_t), &d3d_PartConstants, "d3d_PartConstants");
}


void D3DPart_Shutdown (void)
{
	SAFE_RELEASE (d3d_PartConstants);
	SAFE_RELEASE (d3d_CoronaBlendEnable);
}


CD3DInitShutdownHandler d3d_PartHandler ("part", D3DPart_Init, D3DPart_Shutdown);

void D3DPart_CommonState (void)
{
	// Con_Printf ("partvert is %i\n", sizeof (partvert_t));

	// distance scaling is a function of the current display mode and a user-selectable value
	// if the resolution is smaller distant particles will map to sub-pixel sizes so we try to prevent that
	if (r_particledistscale.value < 0) r_particledistscale.Set (0.0f);
	if (r_particledistscale.value > 0.02f) r_particledistscale.Set (0.02f);

	r_particlescale = r_particlesize.value * 0.5f;
	r_particlegravity = sv_gravity.value * 0.05f;

	if (d3d_CurrentMode.Height < d3d_CurrentMode.Width)
		r_particledist = (r_particledistscale.value * 480.0f) / (float) d3d_CurrentMode.Height;
	else r_particledist = (r_particledistscale.value * 640.0f) / (float) d3d_CurrentMode.Width;

	if (r_particleframe != d3d_RenderDef.presentcount)
	{
		partcbuf_t d3d_PartUpdate;

		Vector3Copy (d3d_PartUpdate.r_vpn, r_viewvectors.fw);

		d3d_PartUpdate.distscale = r_particledist;
		d3d_PartUpdate.scale = r_particlescale;
		d3d_PartUpdate.sv_gravity = r_particlegravity;
		d3d_PartUpdate.cltime = cl.time;
		d3d_PartUpdate.lightscale = r_lightscale.value;

		// precompute the offset vup/vright so that we don't need to do it for each vertex
		// these must be the same as the coords as specified in fxParticles.fx
		// (something about this just needing to be a matrix multiply is nagging at me)
		float parttexcoords[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

		for (int i = 0; i < 4; i++)
		{
			d3d_PartUpdate.r_voffsets[i][0] = r_viewvectors.up[0] * parttexcoords[i][0] + r_viewvectors.rt[0] * parttexcoords[i][1];
			d3d_PartUpdate.r_voffsets[i][1] = r_viewvectors.up[1] * parttexcoords[i][0] + r_viewvectors.rt[1] * parttexcoords[i][1];
			d3d_PartUpdate.r_voffsets[i][2] = r_viewvectors.up[2] * parttexcoords[i][0] + r_viewvectors.rt[2] * parttexcoords[i][1];
			d3d_PartUpdate.r_voffsets[i][3] = 0;
		}

		d3d11_Context->UpdateSubresource (d3d_PartConstants, 0, NULL, &d3d_PartUpdate, 0, 0);

		r_particleframe = d3d_RenderDef.presentcount;
	}

	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	d3d11_State->IASetInputLayout (d3d_PartInputLayout);
	d3d11_State->VSSetShader (d3d_PartVertexShader);
	d3d11_State->VSSetConstantBuffer (1, d3d_PartConstants);

	d3d_PartState.NumParticles = 0;
}


void D3DPart_Begin (void)
{
	D3DPart_CommonState ();

	if (!D3DMisc_OverridePS ())
	{
		int ShaderFlag = 0;

		if (r_particlestyle.value) ShaderFlag |= PARTSHADER_SQUARE;
		if (RealFogDensity > 0.0f) ShaderFlag |= PARTSHADER_FOG;

		d3d11_State->PSSetShader (d3d_PartPixelShaders[ShaderFlag]);
	}

	d3d_PartState.NumParticles = 0;
	d3d_PartState.Particles = NULL;
}


void D3DPart_End (void)
{
	if (d3d_PartState.Particles)
	{
		d3d11_Context->Unmap (QINSTANCE::VertexBuffer, 0);
		d3d_PartState.Particles = NULL;
	}

	if (d3d_PartState.NumParticles)
	{
		d3d11_State->SuspendCallback ();
		d3d11_State->IASetVertexBuffer (0, QINSTANCE::VertexBuffer, sizeof (partvert_t), QINSTANCE::MapOffset);

		D3DMisc_DrawInstancedCommon (4, d3d_PartState.NumParticles);

		QINSTANCE::MapOffset += CACHE_ALIGN (d3d_PartState.NumParticles * sizeof (partvert_t));
		d3d_PartState.NumParticles = 0;
		d3d11_State->ResumeCallback ();
	}
}


bool D3DPart_GetBufferSpace (int numparts)
{
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	D3D11_MAP MapType = D3D11_MAP_WRITE_NO_OVERWRITE;

	if (QINSTANCE::MapOffset + CACHE_ALIGN ((d3d_PartState.NumParticles + numparts) * sizeof (partvert_t)) >= QINSTANCE::BufferMax)
	{
		D3DPart_End ();
		MapType = D3D11_MAP_WRITE_DISCARD;
		QINSTANCE::MapOffset = 0;
	}

	if (!d3d_PartState.Particles)
	{
		if (FAILED (d3d11_Context->Map (QINSTANCE::VertexBuffer, 0, MapType, 0, &MappedResource)))
			return false;
		else d3d_PartState.Particles = (partvert_t *) (&((byte *) MappedResource.pData)[QINSTANCE::MapOffset]);
	}

	return true;
}


void D3DPart_DrawEmitter (emitter_t *pe)
{
	if (D3DPart_GetBufferSpace (pe->numparticles))
	{
		for (particle_t *p = pe->particles; p; p = p->next)
		{
			// prevent sneaky compiler shenanigans that could result in reading from mapped buffer memory
			Q_MemCpy (d3d_PartState.Particles, &p->v, sizeof (partvert_t));
			d3d_PartState.Particles++;
		}

		d3d_PartState.NumParticles += pe->numparticles;
	}
}


void D3DCorona_Begin (void)
{
	D3DPart_CommonState ();

	if (!D3DMisc_OverridePS ())
		d3d11_State->PSSetShader (d3d_PartPixelShaders[PARTSHADER_CORONA]);

	d3d11_State->OMSetBlendState (d3d_CoronaBlendEnable);
}


void D3DCorona_DrawSingle (float *origin, unsigned colour, float radius)
{
	if (D3DPart_GetBufferSpace (1))
	{
		partvert_t *pv = d3d_PartState.Particles;

		// in explosions coronas can be co-planar with sprites which causes them to z-fight, so
		// adjust the corona position by a small offset to correct this.
		pv->org[0] = origin[0] - 0.25f;
		pv->org[1] = origin[1] - 0.25f;
		pv->org[2] = origin[2] - 0.25f;

		pv->vel[0] = 0;
		pv->vel[1] = 0;
		pv->vel[2] = 0;

		pv->dvel[0] = 0;
		pv->dvel[1] = 0;

		pv->etime = 0;
		pv->grav = 0;
		pv->scale = radius;

		pv->rampnum = 0;
		pv->color = colour;
		pv->die = cl.time + 666;

		d3d_PartState.Particles++;
		d3d_PartState.NumParticles++;
	}
}


void D3DCorona_End (void)
{
	D3DPart_End ();
	d3d11_State->OMSetBlendState (d3d_AlphaBlendEnable);
}

