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
// gl_warp.c -- sky and water polygons

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

ID3D11VertexShader *d3d_WarpVertexShader[4];
ID3D11PixelShader *d3d_WarpPixelShader[4];
ID3D11Buffer *d3d_WarpConstants = NULL;


struct warpconstants_t
{
	float warptime;
	float warpfactor;
	float warpscale;
	float warptexturescale;
	float ripple[2];
	float padding[2];
};

int r_warpframe = -1;

#define WARPSHADER_MODERN		1
#define WARPSHADER_RIPPLE		2
#define WARPSHADER_FOG			2

shaderdefine_t VSWarpDefines[] =
{
	ENCODE_DEFINE (WARPSHADER_MODERN, "1"),
	ENCODE_DEFINE (WARPSHADER_RIPPLE, "1")
};


shaderdefine_t PSWarpDefines[] =
{
	ENCODE_DEFINE (WARPSHADER_MODERN, "1"),
	ENCODE_DEFINE (WARPSHADER_FOG, "1")
};


void D3DWarp_Init (void)
{
	QSHADERFACTORY ShaderFactory (IDR_BRUSHFX);
	D3D10_SHADER_MACRO *Defines = NULL;

	for (int i = 0; i < ARRAYLENGTH (d3d_WarpVertexShader); i++)
	{
		Defines = ShaderFactory.EncodeDefines (VSWarpDefines, ARRAYLENGTH (VSWarpDefines), i);
		ShaderFactory.CreateVertexShader (&d3d_WarpVertexShader[i], "WarpVS", Defines);
	}

	for (int i = 0; i < ARRAYLENGTH (d3d_WarpPixelShader); i++)
	{
		Defines = ShaderFactory.EncodeDefines (PSWarpDefines, ARRAYLENGTH (PSWarpDefines), i);
		ShaderFactory.CreatePixelShader (&d3d_WarpPixelShader[i], "WarpPS", Defines);
	}

	BufferFactory.CreateConstantBuffer (sizeof (warpconstants_t), &d3d_WarpConstants, "d3d_WarpConstants");

	r_warpframe = -1;
}


void D3DWarp_Shutdown (void)
{
	SAFE_RELEASE (d3d_WarpConstants);
}


CD3DInitShutdownHandler d3d_WarpHandler ("warp", D3DWarp_Init, D3DWarp_Shutdown);


byte d3d_WaterAlpha = 255;
byte d3d_LavaAlpha = 255;
byte d3d_SlimeAlpha = 255;
byte d3d_TeleAlpha = 255;

cvar_t r_lavaalpha ("r_lavaalpha", 1);
cvar_t r_telealpha ("r_telealpha", 1);
cvar_t r_slimealpha ("r_slimealpha", 1);
cvar_t r_warpspeed ("r_warpspeed", 4, CVAR_ARCHIVE);
cvar_t r_warpscale ("r_warpscale", 8, CVAR_ARCHIVE);
cvar_t r_warpfactor ("r_warpfactor", 2, CVAR_ARCHIVE);
cvar_t r_warptexturescale ("r_warptexturescale", 1, CVAR_ARCHIVE);
cvar_t r_warpstyle ("r_warpstyle", 0.0f, CVAR_ARCHIVE);

// for nehahra and anyone who wants them...
cvar_t r_waterripple ("r_waterripple", 0.0f, CVAR_ARCHIVE);
cvar_t r_nowaterripple ("r_nowaterripple", 1.0f, CVAR_ARCHIVE);

// lock water/slime/tele/lava alpha sliders
cvar_t r_lockalpha ("r_lockalpha", "1");

extern QTEXTURE warpgradient;

void D3DBrush_BatchAlphaSurface (msurface_t *surf, entity_t *ent, int alpha);
void D3DSurf_EmitSurfToAlpha (msurface_t *surf, entity_t *ent);

void D3DWarp_BeginFrame (void)
{
	// store alpha values
	// this needs to be checked first because the actual values we'll use depend on whether or not the sliders are locked
	// multiply by 256 to prevent float rounding errors
	d3d_WaterAlpha = (r_lockalpha.value) ? BYTE_CLAMP (r_wateralpha.value * 256) : BYTE_CLAMP (r_wateralpha.value * 256);
	d3d_LavaAlpha = (r_lockalpha.value) ? BYTE_CLAMP (r_wateralpha.value * 256) : BYTE_CLAMP (r_lavaalpha.value * 256);
	d3d_SlimeAlpha = (r_lockalpha.value) ? BYTE_CLAMP (r_wateralpha.value * 256) : BYTE_CLAMP (r_slimealpha.value * 256);
	d3d_TeleAlpha = (r_lockalpha.value) ? BYTE_CLAMP (r_wateralpha.value * 256) : BYTE_CLAMP (r_telealpha.value * 256);
}


void D3DWarp_DrawAlphaSurface (msurface_t *surf, entity_t *ent)
{
	byte thisalpha = 255;

	// automatic alpha always
	if (surf->flags & SURF_DRAWWATER) thisalpha = d3d_WaterAlpha;
	if (surf->flags & SURF_DRAWLAVA) thisalpha = d3d_LavaAlpha;
	if (surf->flags & SURF_DRAWTELE) thisalpha = d3d_TeleAlpha;
	if (surf->flags & SURF_DRAWSLIME) thisalpha = d3d_SlimeAlpha;

	// entity override
	if (ent && (ent->alphaval > 0 && ent->alphaval < 255)) thisalpha = ent->alphaval;

	D3DBrush_BatchAlphaSurface (surf, ent, thisalpha);
}


bool D3DWarp_CheckAlphaSurface (msurface_t *surf, entity_t *ent)
{
	if ((surf->flags & SURF_DRAWLAVA) && d3d_LavaAlpha < 255) {D3DSurf_EmitSurfToAlpha (surf, ent); return true;}
	if ((surf->flags & SURF_DRAWTELE) && d3d_TeleAlpha < 255) {D3DSurf_EmitSurfToAlpha (surf, ent); return true;}
	if ((surf->flags & SURF_DRAWSLIME) && d3d_SlimeAlpha < 255) {D3DSurf_EmitSurfToAlpha (surf, ent); return true;}
	if ((surf->flags & SURF_DRAWWATER) && d3d_WaterAlpha < 255) {D3DSurf_EmitSurfToAlpha (surf, ent); return true;}

	return false;
}


void D3DWarp_DrawWaterSurfaces (brushhdr_t *hdr, entity_t *ent)
{
	// explicitly called when we are drawing a warp chain so set up here
	if (r_warpframe != d3d_RenderDef.presentcount)
	{
		warpconstants_t d3d_WarpUpdate;

		// all of these below are premultiplied by various factors to save on PS instructions
		// some day I'll #define them properly so that we don't have scary magic numbers all over the place
		d3d_WarpUpdate.warptime = (cl.time * r_warpspeed.value) / 4.0f;
		d3d_WarpUpdate.warpfactor = (r_warpfactor.value * D3DX_PI) / 128.0;
		d3d_WarpUpdate.warpscale = (r_warpscale.value > 0) ? (1.0f / r_warpscale.value) : 1.0f;
		d3d_WarpUpdate.warptexturescale = (r_warptexturescale.value > 0) ? (1.0f / (64.0f * r_warptexturescale.value)) : (1.0f / 64.0f);

		// determine if we're rippling
		// this madness is fucking nehahra compatibility
		d3d_WarpUpdate.ripple[0] = (r_waterripple.value && !r_nowaterripple.value) ? r_waterripple.value : 0;
		d3d_WarpUpdate.ripple[1] = (r_waterripple.value && !r_nowaterripple.value) ? (cl.time * 3.0f) : 0;

		d3d11_Context->UpdateSubresource (d3d_WarpConstants, 0, NULL, &d3d_WarpUpdate, 0, 0);

		r_warpframe = d3d_RenderDef.presentcount;
	}

	d3d11_State->VSSetConstantBuffer (2, d3d_WarpConstants);
	d3d11_State->PSSetConstantBuffer (2, d3d_WarpConstants);

	int VSShaderFlag = 0;

	if (r_waterripple.value && !r_nowaterripple.value) VSShaderFlag |= WARPSHADER_RIPPLE;
	if (r_warpstyle.value) VSShaderFlag |= WARPSHADER_MODERN;

	d3d11_State->VSSetShader (d3d_WarpVertexShader[VSShaderFlag]);

	if (!D3DMisc_OverridePS ())
	{
		int PSShaderFlag = 0;

		if (RealFogDensity > 0) PSShaderFlag |= WARPSHADER_FOG;

		if (r_warpstyle.value)
		{
			d3d11_State->PSSetTexture (1, &warpgradient);
			PSShaderFlag |= WARPSHADER_MODERN;
		}

		d3d11_State->PSSetShader (d3d_WarpPixelShader[PSShaderFlag]);
	}

	d3d11_State->PSSetSampler (0, d3d_DefaultSamplerWrap);

	for (int i = 0; i < hdr->numtextures; i++)
	{
		texture_t *tex = NULL;
		msurface_t *surf = NULL;

		if (!(tex = hdr->textures[i])) continue;
		if (!(surf = tex->texturechain)) continue;
		if (!(surf->flags & SURF_DRAWTURB)) continue;

		if (r_lightmap.integer)
			d3d11_State->PSSetTexture (0, &QTEXTURE::WhiteTexture);
		else d3d11_State->PSSetTexture (0, tex->teximage);

		D3DSurf_DrawTextureChain (tex);
	}
}


