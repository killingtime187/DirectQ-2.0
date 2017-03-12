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
// r_main.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

cvar_t r_waterwarp ("r_waterwarp", 1, CVAR_ARCHIVE);
cvar_t r_waterwarpspeed ("r_waterwarpspeed", 1.0f, CVAR_ARCHIVE);
cvar_t r_waterwarpscale ("r_waterwarpscale", 16.0f, CVAR_ARCHIVE);
cvar_t r_waterwarpfactor ("r_waterwarpfactor", 12.0f, CVAR_ARCHIVE);


#define RTTTYPE_WATERWARP_CLASSIC		1
#define RTTTYPE_WATERWARP_MODERN		2

QTEXTURE warpgradient;

struct rttglobalcbuf_t
{
	float rttViewPortSize[2];
	float rttVidModeSize[2];
};


struct rttpereffectcbuf_t
{
	float effectConstant[4];
	float effectColor[4];
};


struct rttvert_t
{
	float Position[2];
};


ID3D11Buffer *d3d_RTTGlobalConstants;
ID3D11Buffer *d3d_RTTPerEffectConstants;

ID3D11Texture2D *d3d_ScreenTexture;
ID3D11ShaderResourceView *d3d_ScreenSRV;
ID3D11RenderTargetView *d3d_ScreenRTV;
int d3d_ScreenNumMips;

class rttview_t
{
public:
	void Init (char *texname, char *srvname, char *rtvname);
	void Shutdown (void);
	void ReInit (char *texname, char *srvname, char *rtvname);

	QTEXTURE *CopyScreen (void);

	QTEXTURE Texture;
	ID3D11RenderTargetView *RTV;
};


void rttview_t::Init (char *texname, char *srvname, char *rtvname)
{
	// get the description of the default render target for creating a new one from it
	D3D11_TEXTURE2D_DESC desc;
	ID3D11Texture2D *pRenderTargetTexture = NULL;

	if (SUCCEEDED (d3d11_SwapChain->GetBuffer (0, __uuidof (ID3D11Texture2D), (LPVOID *) &pRenderTargetTexture)))
	{
		pRenderTargetTexture->GetDesc (&desc);
		pRenderTargetTexture->Release ();

		// ensure this
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

		this->Texture.CreateTextureAndSRV (&desc, NULL);
		this->Texture.CreateRenderTarget (&this->RTV);
		this->Texture.SetObjectNames (texname, srvname);

		D3DMisc_SetObjectName (this->RTV, rtvname);

		Con_DPrintf ("Specified Render Target View at %ix%i\n", desc.Width, desc.Height);
	}
}


void rttview_t::Shutdown (void)
{
	this->Texture.Release ();
	SAFE_RELEASE (this->RTV);
}


void rttview_t::ReInit (char *texname, char *srvname, char *rtvname)
{
	// only shut down and reinit if we've already created stuff otherwise we're going to leak the previous copy
	if (this->RTV)
	{
		this->Shutdown ();
		this->Init (texname, srvname, rtvname);
	}
}


QTEXTURE *rttview_t::CopyScreen (void)
{
	if (vid.brightpass && !vid.nobrightpass)
	{
		// this code path is no longer used (aside from the fadescreen which we really should port too)
		this->Texture.CopyFrom (d3d_ScreenTexture);
		return &this->Texture;
	}
	else
	{
		ID3D11Texture2D *pRenderTargetTexture = NULL;

		if (SUCCEEDED (d3d11_SwapChain->GetBuffer (0, __uuidof (ID3D11Texture2D), (LPVOID *) &pRenderTargetTexture)))
		{
			this->Texture.CopyFrom (pRenderTargetTexture);
			pRenderTargetTexture->Release ();

			return &this->Texture;
		}
		else return NULL;
	}
}


rttview_t d3d_RTTViews[2];


class QRTTEFFECT
{
public:
	void Init (char *vsentry, char *psentry, char *loname, int type);
	void Shutdown (void);
	void Check (void);
	void Cascade (int FlipSequence);

	static QRTTEFFECT *Effects;

	static void SetGlobalState (void);

protected:
	ID3D11VertexShader *VertexShader;
	ID3D11PixelShader *PixelShader;

	int Type;
	float Constants[4];
	float Color[4];

	QRTTEFFECT *next;
};


void QRTTEFFECT::Init (char *vsentry, char *psentry, char *loname, int type)
{
	QSHADERFACTORY ShaderFactory (IDR_RTTFX);

	ShaderFactory.CreateVertexShader (&this->VertexShader, vsentry);
	ShaderFactory.CreatePixelShader (&this->PixelShader, psentry);

	this->Type = type;
}


void QRTTEFFECT::Shutdown (void)
{
	SAFE_RELEASE (this->VertexShader);
	SAFE_RELEASE (this->PixelShader);
}


bool D3DRTT_CheckWaterWarp (int targetNum)
{
	// ensure that this fires only if we're actually running a map
	if (!cls.maprunning) return false;

	// this isn't the waterwarp you're looking for
	if (r_waterwarp.integer != targetNum) return false;

	// these content types don't have a warp
	// we can't check cl.inwater because it's true if partially submerged and it may have some latency from the server
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_EMPTY) return false;
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID) return false;
	if (d3d_RenderDef.viewleaf->contents == CONTENTS_SKY) return false;

	// we have a valid warp now
	return true;
}


void QRTTEFFECT::Check (void)
{
	switch (this->Type)
	{
	case RTTTYPE_WATERWARP_MODERN: if (D3DRTT_CheckWaterWarp (1)) break; else return;
	case RTTTYPE_WATERWARP_CLASSIC: if (D3DRTT_CheckWaterWarp (3)) break; else return;
	default: return; // undefined type
	}

	// ok, we've got a good type now so set everything else we need up
	if (gl_polyblend.value > 0.0f && vid.cshift[3] > 0)
	{
		Vector4Scale (this->Color, vid.cshift, 0.00390625f);
		this->Color[3] *= gl_polyblend.value;
	}
	else Vector4Clear (this->Color);

	// disable color shift (because we're going to merge it into the RTT effect) so that we don't also get a polyblend
	vid.cshift[3] = -666;

	// and link it in
	this->next = QRTTEFFECT::Effects;
	QRTTEFFECT::Effects = this;
}


void QRTTEFFECT::Cascade (int FlipSequence)
{
	// set up the correct render target for this pass (the final pass just goes to the default back buffer)
	// these are the final things done in the 3D view so they're safe to disable the depth buffer
	if (this->next)
		d3d11_State->OMSetRenderTargets (d3d_RTTViews[FlipSequence & 1].RTV);
	else if (vid.brightpass && !vid.nobrightpass)
		d3d11_State->OMSetRenderTargets (d3d_ScreenRTV);
	else d3d11_State->OMSetRenderTargets (d3d11_RenderTargetView);

	switch (this->Type)
	{
	case RTTTYPE_WATERWARP_MODERN:
		this->Constants[0] = cl.time * r_waterwarpspeed.value * 2.0f;
		this->Constants[0] = (this->Constants[0] - ((int) this->Constants[0] & ~127)) * 0.0078125f;
		this->Constants[1] = r_waterwarpfactor.value * 0.0002f;
		this->Constants[3] = r_waterwarpscale.value * 0.125f;

		d3d11_State->PSSetTexture (1, &warpgradient);
		d3d11_State->PSSetSampler (3, d3d_SampleClampLinear);
		d3d11_State->PSSetSampler (0, d3d_SampleWrapLinear);
		break;

	case RTTTYPE_WATERWARP_CLASSIC:
		this->Constants[0] = cl.time * r_waterwarpspeed.value;
		this->Constants[1] = r_waterwarpscale.value;
		this->Constants[3] = r_waterwarpfactor.value > 1 ? (1.0f / r_waterwarpfactor.value) : 1;

		d3d11_State->PSSetTexture (1, &warpgradient);
		d3d11_State->PSSetSampler (3, d3d_SampleClampLinear);
		d3d11_State->PSSetSampler (0, d3d_SampleWrapLinear);
		break;

	default:
		// undefined type (still set the render targets above so that the last one will be correct)
		return;
	}

	// because we're going to use the prev render target as texture input we need to sync state in order to
	// unbind it from usage as a render target before we can continue.  small overhead, not significant.
	d3d11_State->SynchronizeState ();
	d3d11_State->PSSetTexture (0, &d3d_RTTViews[!(FlipSequence & 1)].Texture);

	// update the per-effect cbuffer
	rttpereffectcbuf_t cbdata;

	Q_MemCpy (cbdata.effectConstant, this->Constants, sizeof (float) * 4);
	Q_MemCpy (cbdata.effectColor, this->Color, sizeof (float) * 4);

	d3d11_Context->UpdateSubresource (d3d_RTTPerEffectConstants, 0, NULL, &cbdata, 0, 0);

	// set the shaders
	d3d11_State->VSSetShader (this->VertexShader);
	d3d11_State->PSSetShader (this->PixelShader);

	// draw
	D3DMisc_DrawCommon (4);

	// go to the next effect (and next flip sequence)
	if (this->next) this->next->Cascade (FlipSequence + 1);
}


void QRTTEFFECT::SetGlobalState (void)
{
	QVIEWPORT vp (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 0);

	// the array of viewports method behaved oddly with mode changes so i reverted it to on-demand changes
	d3d11_State->RSSetViewport (&vp);

	d3d11_State->OMSetBlendState (NULL);
	d3d11_State->OMSetDepthStencilState (d3d_DisableDepthTest);
	d3d11_State->RSSetState (d3d_RS2DView);

	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	d3d11_State->IASetInputLayout (NULL);

	d3d11_State->VSSetConstantBuffer (1, d3d_RTTGlobalConstants);
	d3d11_State->PSSetConstantBuffer (1, d3d_RTTGlobalConstants);

	d3d11_State->VSSetConstantBuffer (2, d3d_RTTPerEffectConstants);
	d3d11_State->PSSetConstantBuffer (2, d3d_RTTPerEffectConstants);

	// split to global and per-effect
	rttglobalcbuf_t cbdata = {
		{vid.ref3dsize.width, vid.ref3dsize.height},
		{d3d_CurrentMode.Width, d3d_CurrentMode.Height}
	};

	d3d11_Context->UpdateSubresource (d3d_RTTGlobalConstants, 0, NULL, &cbdata, 0, 0);
}


QRTTEFFECT d3d_WaterWarpEffectClassic;
QRTTEFFECT d3d_WaterWarpEffectModern;

QRTTEFFECT *QRTTEFFECT::Effects = NULL;


void D3DRTT_ClearRenderTarget (ID3D11RenderTargetView *rtv)
{
	// we only need to clear if we're rendering 3D
	byte *clearcolor = NULL;

	extern cvar_t r_lockpvs;
	extern cvar_t r_lockfrustum;
	extern cvar_t r_locksurfaces;
	extern cvar_t gl_clear;
	extern cvar_t r_clearcolor;

	// select correct color clearing mode (depth was already cleared in begin frame)
	if (!d3d_RenderDef.viewleaf || d3d_RenderDef.viewleaf->contents == CONTENTS_SOLID || !d3d_RenderDef.viewleaf->compressed_vis)
		clearcolor = (byte *) &d3d_QuakePalette.standard11[2];
	else if (r_wireframe.integer)
		clearcolor = (byte *) &d3d_QuakePalette.standard11[2];
	else if (r_lockfrustum.integer || r_lockpvs.integer || r_locksurfaces.integer)
		clearcolor = (byte *) &d3d_QuakePalette.standard11[109];
	else if (gl_clear.value)
		clearcolor = (byte *) &d3d_QuakePalette.standard11[r_clearcolor.integer & 255];

	if (clearcolor) d3d11_Context->ClearRenderTargetView (rtv, D3DMisc_GetColorFromRGBA (clearcolor));
}


void D3DRTT_BeginEffects (void)
{
	QRTTEFFECT::Effects = NULL;
	QVIEWPORT vp (0, 0, vid.ref3dsize.width, vid.ref3dsize.height, 0, 1);

	// enumerate effects and link them to the list - effects are enumerated in reverse order to that
	// they will be applied in so that the list will unwind correctly with the last one setting the
	// default backbuffer as the final render target
	d3d_WaterWarpEffectClassic.Check ();
	d3d_WaterWarpEffectModern.Check ();

	// the clear needs to be handled here as the RTV may change
	if (QRTTEFFECT::Effects)
	{
		d3d11_State->OMSetRenderTargets (d3d_RTTViews[0].RTV, d3d11_DepthStencilView);
		D3DRTT_ClearRenderTarget (d3d_RTTViews[0].RTV);
	}
	else if (vid.brightpass && !vid.nobrightpass)
	{
		d3d11_State->OMSetRenderTargets (d3d_ScreenRTV, d3d11_DepthStencilView);
		D3DRTT_ClearRenderTarget (d3d_ScreenRTV);
	}
	else
	{
		d3d11_State->OMSetRenderTargets (d3d11_RenderTargetView, d3d11_DepthStencilView);
		D3DRTT_ClearRenderTarget (d3d11_RenderTargetView);
	}

	d3d11_State->RSSetViewport (&vp);
}


void D3DRTT_EndEffects (void)
{
	if (!QRTTEFFECT::Effects) return;

	QRTTEFFECT::SetGlobalState ();
	QRTTEFFECT::Effects->Cascade (1);	// begins at FlipSequence 1
	QRTTEFFECT::Effects = NULL;
}


void D3DRTT_InitScreenTexture (void)
{
	// get the description of the default render target for creating a new one from it
	D3D11_TEXTURE2D_DESC desc;
	ID3D11Texture2D *pRenderTargetTexture = NULL;

	if (SUCCEEDED (d3d11_SwapChain->GetBuffer (0, __uuidof (ID3D11Texture2D), (LPVOID *) &pRenderTargetTexture)))
	{
		pRenderTargetTexture->GetDesc (&desc);
		pRenderTargetTexture->Release ();

		// ensure this
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.MipLevels = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		d3d11_Device->CreateTexture2D (&desc, NULL, &d3d_ScreenTexture);
		d3d11_Device->CreateShaderResourceView (d3d_ScreenTexture, NULL, &d3d_ScreenSRV);
		d3d11_Device->CreateRenderTargetView (d3d_ScreenTexture, NULL, &d3d_ScreenRTV);
		d3d11_Context->GenerateMips (d3d_ScreenSRV);
		d3d11_Context->Flush ();

		d3d_ScreenTexture->GetDesc (&desc);
		d3d_ScreenNumMips = desc.MipLevels;

		D3DMisc_SetObjectName (d3d_ScreenTexture, "d3d_ScreenTexture");
		D3DMisc_SetObjectName (d3d_ScreenSRV, "d3d_ScreenSRV");
		D3DMisc_SetObjectName (d3d_ScreenRTV, "d3d_ScreenRTV");

		Con_DPrintf ("Specified Render Target View at %ix%i\n", desc.Width, desc.Height);
	}
}


void D3DRTT_ShutdownScreenTexture (void)
{
	SAFE_RELEASE (d3d_ScreenRTV);
	SAFE_RELEASE (d3d_ScreenSRV);
	SAFE_RELEASE (d3d_ScreenTexture);
}


void D3DRTT_Shutdown (void)
{
	D3DRTT_ShutdownScreenTexture ();

	// shutdown effects
	d3d_WaterWarpEffectClassic.Shutdown ();
	d3d_WaterWarpEffectModern.Shutdown ();

	// shutdown views
	d3d_RTTViews[0].Shutdown ();
	d3d_RTTViews[1].Shutdown ();

	// shutdown common objects
	SAFE_RELEASE (d3d_RTTGlobalConstants);
	SAFE_RELEASE (d3d_RTTPerEffectConstants);

	warpgradient.Release ();
}


void D3DRTT_Init (void)
{
	D3DRTT_InitScreenTexture ();

	// create effects
	d3d_WaterWarpEffectClassic.Init ("WaterWarpVSClassic", "WaterWarpPSClassic", "WaterWarpLayoutClassic", RTTTYPE_WATERWARP_CLASSIC);
	d3d_WaterWarpEffectModern.Init ("WaterWarpVSModern", "WaterWarpPSModern", "WaterWarpLayoutModern", RTTTYPE_WATERWARP_MODERN);

	// create views
	d3d_RTTViews[0].Init ("rtt0tex", "rtt0srv", "rtt0rtv");
	d3d_RTTViews[1].Init ("rtt1tex", "rtt1srv", "rtt1rtv");

	// create common objects
	BufferFactory.CreateConstantBuffer (sizeof (rttglobalcbuf_t), &d3d_RTTGlobalConstants, "d3d_RTTGlobalConstants");
	BufferFactory.CreateConstantBuffer (sizeof (rttpereffectcbuf_t), &d3d_RTTPerEffectConstants, "d3d_RTTPerEffectConstants");

	// construct and load the waterwarp gradient texture
#define WARPGRADIENT_SIZE	256
	int hunkmark = TempHunk->GetLowMark ();
	byte *data = (byte *) TempHunk->FastAlloc (WARPGRADIENT_SIZE * WARPGRADIENT_SIZE * 4);
	byte *rgba = data;
	byte *gradtable = (byte *) TempHunk->FastAlloc (WARPGRADIENT_SIZE);

	// setup the gradients table
	memset (gradtable, 255, WARPGRADIENT_SIZE);

	for (int i = 0, j = WARPGRADIENT_SIZE - 1, g = 1; g < 256; i++, j--, g <<= 1)
		gradtable[i] = gradtable[j] = g - 1;

	// keep the randomization consistent
	srand (666);

	for (int y = 0; y < WARPGRADIENT_SIZE; y++)
	{
		for (int x = 0; x < WARPGRADIENT_SIZE; x++, rgba += 4)
		{
			// control attenuation factors
			rgba[0] = gradtable[x];
			rgba[1] = gradtable[y];

			// a simple random noise is the baseline
			rgba[2] = rand () & 255;
			rgba[3] = rand () & 255;
		}
	}

	// blur it once, raise it to a coupla powers, then blur it again to get the final image
	D3DImage_Blur (data, WARPGRADIENT_SIZE, WARPGRADIENT_SIZE, 2, CM_B | CM_A);
	D3DImage_Power (data, WARPGRADIENT_SIZE, WARPGRADIENT_SIZE, 2, CM_B | CM_A);
	D3DImage_Blur (data, WARPGRADIENT_SIZE, WARPGRADIENT_SIZE, 2, CM_B | CM_A);

	// SCR_WriteDataToTGA ("grad.tga", data, WARPGRADIENT_SIZE, WARPGRADIENT_SIZE, 32);
	warpgradient.Upload (data, WARPGRADIENT_SIZE, WARPGRADIENT_SIZE, IMAGE_MIPMAP | IMAGE_32BIT, NULL);
	TempHunk->FreeToLowMark (hunkmark);
}


void D3DRTT_Resize (void)
{
	D3DRTT_ShutdownScreenTexture ();
	D3DRTT_InitScreenTexture ();

	// shutdown and recreate views
	d3d_RTTViews[0].ReInit ("rtt0tex", "rtt0srv", "rtt0rtv");
	d3d_RTTViews[1].ReInit ("rtt1tex", "rtt1srv", "rtt1rtv");
}


CD3DInitShutdownHandler d3d_RenderTargetHandler ("rtt", D3DRTT_Init, D3DRTT_Shutdown);


QTEXTURE *D3DRTT_CopyScreen (void)
{
	// just copy the screen to one of our views for when we can't be bothered setting up a full RTT pass
	return d3d_RTTViews[0].CopyScreen ();
}

