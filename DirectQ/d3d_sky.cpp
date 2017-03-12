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
// gl_warp.c -- sky and water polygons

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"


QTEXTURE solidskytexture;
QTEXTURE alphaskytexture;
QTEXTURE skyboxarray;
QTEXTURE skyboxtextures[6];

ID3D11Buffer *d3d_SkyConstants = NULL;

ID3D11VertexShader *d3d_SkyVertexShader = NULL;
ID3D11PixelShader *d3d_SkyPixelShader[8];

struct skyconstants_t
{
	QMATRIX SkyMatrix;
	float SkyScale[3];
	float SkyAlpha;
};

int r_skyframe = -1;

#define SKYSHADER_CUBE		1
#define SKYSHADER_ARRAY		2
#define SKYSHADER_FOG		4

shaderdefine_t SkyDefines[] =
{
	ENCODE_DEFINE (SKYSHADER_CUBE, "1"),
	ENCODE_DEFINE (SKYSHADER_ARRAY, "1"),
	ENCODE_DEFINE (SKYSHADER_FOG, "1")
};


void D3DSky_Init (void)
{
	QSHADERFACTORY ShaderFactory (IDR_BRUSHFX);
	D3D10_SHADER_MACRO *Defines = NULL;

	ShaderFactory.CreateVertexShader (&d3d_SkyVertexShader, "SkyVS");

	for (int i = 0; i < ARRAYLENGTH (d3d_SkyPixelShader); i++)
	{
		// both cannot be set
		if ((i & SKYSHADER_CUBE) && (i & SKYSHADER_ARRAY)) continue;

		Defines = ShaderFactory.EncodeDefines (SkyDefines, ARRAYLENGTH (SkyDefines), i);
		ShaderFactory.CreatePixelShader (&d3d_SkyPixelShader[i], "SkyPS", Defines);
	}

	BufferFactory.CreateConstantBuffer (sizeof (skyconstants_t), &d3d_SkyConstants, "d3d_SkyConstants");

	r_skyframe = -1;
}


void D3DSky_Shutdown (void)
{
	SAFE_RELEASE (d3d_SkyConstants);

	for (int i = 0; i < 6; i++)
		skyboxtextures[i].Release ();

	skyboxarray.Release ();
	solidskytexture.Release ();
	alphaskytexture.Release ();
}


CD3DInitShutdownHandler d3d_SkyHandler ("sky", D3DSky_Init, D3DSky_Shutdown);


/*
==============================================================================================================================

		SKY WARP RENDERING

==============================================================================================================================
*/

#define SKYBOX_NONE		0
#define SKYBOX_CUBE		1
#define SKYBOX_6TEX		2

// the menu needs to know the name of the loaded skybox
char CachedSkyBoxName[MAX_PATH] = {0};
int SkyboxValid = SKYBOX_NONE;

float spherescale_y = 1.0f;

cvar_t r_skybackscroll ("r_skybackscroll", 8, CVAR_ARCHIVE);
cvar_t r_skyfrontscroll ("r_skyfrontscroll", 16, CVAR_ARCHIVE);
cvar_t r_skyalpha ("r_skyalpha", 1, CVAR_ARCHIVE);
cvar_t r_skyscale ("r_skyscale", 3, CVAR_ARCHIVE);

// intended to be used by mods
cvar_t r_skyrotate_x ("r_skyrotate_x", 0.0f);
cvar_t r_skyrotate_y ("r_skyrotate_y", 0.0f);
cvar_t r_skyrotate_z ("r_skyrotate_z", 0.0f);
cvar_t r_skyrotate_speed ("r_skyrotate_speed", 0.0f);

cvar_t r_skyspherescale ("r_skyspherescale", 1, CVAR_ARCHIVE);
cvar_t r_skyfog ("r_skyfog", 0.5f, CVAR_ARCHIVE);


void D3DSky_DrawSkySurfaces (brushhdr_t *hdr, entity_t *ent)
{
	d3d11_State->VSSetShader (d3d_SkyVertexShader);

	if (!D3DMisc_OverridePS ())
	{
		int SkyShader = (RealFogDensity > 0) ? SKYSHADER_FOG : 0;

		if (r_lightmap.value)
		{
			d3d11_State->PSSetShader (d3d_SkyPixelShader[SkyShader]);

			d3d11_State->PSSetTexture (0, &QTEXTURE::WhiteTexture);
			d3d11_State->PSSetTexture (1, &QTEXTURE::WhiteTexture);

			d3d11_State->PSSetSampler (0, d3d_DefaultSamplerWrap);
		}
		else if (SkyboxValid == SKYBOX_6TEX)
		{
			d3d11_State->PSSetShader (d3d_SkyPixelShader[SkyShader | SKYSHADER_CUBE]);

			d3d11_State->PSSetTexture (0, &skyboxtextures[0]);
			d3d11_State->PSSetTexture (1, &skyboxtextures[1]);
			d3d11_State->PSSetTexture (2, &skyboxtextures[2]);
			d3d11_State->PSSetTexture (3, &skyboxtextures[3]);
			d3d11_State->PSSetTexture (4, &skyboxtextures[4]);
			d3d11_State->PSSetTexture (5, &skyboxtextures[5]);

			d3d11_State->PSSetSampler (0, d3d_DefaultSamplerClamp);
		}
		else if (SkyboxValid == SKYBOX_CUBE)
		{
			d3d11_State->PSSetShader (d3d_SkyPixelShader[SkyShader | SKYSHADER_ARRAY]);
			d3d11_State->PSSetTexture (7, &skyboxarray);
			d3d11_State->PSSetSampler (0, d3d_DefaultSamplerClamp);
		}
		else
		{
			d3d11_State->PSSetShader (d3d_SkyPixelShader[SkyShader]);

			d3d11_State->PSSetTexture (0, &solidskytexture);
			d3d11_State->PSSetTexture (1, &alphaskytexture);

			d3d11_State->PSSetSampler (0, d3d_DefaultSamplerWrap);
		}
	}

	if (r_skyframe != d3d_RenderDef.presentcount)
	{
		skyconstants_t d3d_SkyUpdate;

		d3d_SkyUpdate.SkyMatrix.Identity ();

		if (SkyboxValid)
		{
			// orient for consistency with fitz
			if (SkyboxValid == SKYBOX_CUBE) d3d_SkyUpdate.SkyMatrix.Rotate (0, 0, 1, 90);

			if ((r_skyrotate_x.value || r_skyrotate_y.value || r_skyrotate_z.value) && r_skyrotate_speed.value)
			{
				// rotate the skybox
				float rot[3] = {r_skyrotate_x.value, r_skyrotate_y.value, r_skyrotate_z.value};

				Vector3Normalize (rot);
				d3d_SkyUpdate.SkyMatrix.Rotate (rot[0], rot[1], rot[2], cl.time * r_skyrotate_speed.value);
			}
		}
		else
		{
			// flatten the sphere
			d3d_SkyUpdate.SkyMatrix.Scale (1.0f, 1.0f, 3.0f);
		}

		// move relative to view position only
		d3d_SkyUpdate.SkyMatrix.Translate (-r_refdef.vieworigin[0], -r_refdef.vieworigin[1], -r_refdef.vieworigin[2]);

		// scale[2] goes to the same scale as GLQuake used (378 / 128)
		d3d_SkyUpdate.SkyScale[0] = r_skybackscroll.value * cl.time;
		d3d_SkyUpdate.SkyScale[1] = r_skyfrontscroll.value * cl.time;
		d3d_SkyUpdate.SkyScale[2] = r_skyscale.value * 0.984375f;

		// don't overflow our max texture repeat
		d3d_SkyUpdate.SkyScale[0] = (d3d_SkyUpdate.SkyScale[0] - ((int) d3d_SkyUpdate.SkyScale[0] & ~127)) * 0.0078125f;
		d3d_SkyUpdate.SkyScale[1] = (d3d_SkyUpdate.SkyScale[1] - ((int) d3d_SkyUpdate.SkyScale[1] & ~127)) * 0.0078125f;

		// initialize sky for the frame
		if (r_skyalpha.value < 0.0f) r_skyalpha.Set (0.0f);
		if (r_skyalpha.value > 1.0f) r_skyalpha.Set (1.0f);

		d3d_SkyUpdate.SkyAlpha = r_skyalpha.value;

		d3d11_Context->UpdateSubresource (d3d_SkyConstants, 0, NULL, &d3d_SkyUpdate, 0, 0);

		// update constants ensuring it's only done when frame changes
		r_skyframe = d3d_RenderDef.presentcount;
	}

	d3d11_State->VSSetConstantBuffer (2, d3d_SkyConstants);
	d3d11_State->PSSetConstantBuffer (2, d3d_SkyConstants);

	for (int i = 0; i < hdr->numtextures; i++)
	{
		texture_t *tex = NULL;
		msurface_t *surf = NULL;

		if (!(tex = hdr->textures[i])) continue;
		if (!(surf = tex->texturechain)) continue;
		if (!(surf->flags & SURF_DRAWSKY)) continue;

		D3DSurf_DrawTextureChain (tex);
	}
}


/*
==============================================================================================================================

		SKY INITIALIZATION

==============================================================================================================================
*/

/*
=============
D3DSky_InitTextures

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void D3DSky_InitTextures (miptex_t *mt, char **paths)
{
	// sanity check
	if ((mt->width % 4) || (mt->width < 4) || (mt->height % 2) || (mt->height < 2))
	{
		Host_Error ("D3DSky_InitTextures: invalid sky dimensions (%i x %i)\n", mt->width, mt->height);
		return;
	}

	// because you never know when a mapper might use a non-standard size...
	int hunkmark = TempHunk->GetLowMark ();
	unsigned *trans = (unsigned *) TempHunk->FastAlloc (mt->width * mt->height * sizeof (unsigned) / 2);

	// copy out
	int transwidth = mt->width / 2;
	int transheight = mt->height;

	// we don't really need to do this (we could just reuse the old textures) but that's kinda broke right now
	// (the attempt to load an external will NULL them anyway........)
	solidskytexture.Release ();
	alphaskytexture.Release ();

	// make an average value for the back to avoid a fringe on the top level
	for (int i = 0; i < transheight; i++)
	{
		for (int j = 0; j < transwidth; j++)
		{
			// solid sky can go up as 8 bit
			int p = mt->texels[i * mt->width + j + transwidth];
			((byte *) trans)[(i * transwidth) + j] = p;
		}
	}

	// upload it - solid sky can go up as 8 bit
	if (!solidskytexture.LoadExternal (va ("%s_solid", mt->name), paths, 0))
		solidskytexture.Upload ((byte *) trans, transwidth, transheight, 0, d3d_QuakePalette.standard11);

	// bottom layer
	for (int i = 0; i < transheight; i++)
	{
		for (int j = 0; j < transwidth; j++)
		{
			int p = mt->texels[i * mt->width + j];
			trans[(i * transwidth) + j] = (p == 0) ? 0 : d3d_QuakePalette.standard11[p];
		}
	}

	// upload it - alpha sky needs to go up as 32 bit owing to averaging
	if (!alphaskytexture.LoadExternal (va ("%s_alpha", mt->name), paths, 0))
		alphaskytexture.Upload ((byte *) trans, transwidth, transheight, IMAGE_32BIT | IMAGE_ALPHA, NULL);

	TempHunk->FreeToLowMark (hunkmark);
}


void D3DSky_UnloadSkybox (void)
{
	for (int i = 0; i < 6; i++)
		skyboxtextures[i].Release ();

	skyboxarray.Release ();
	SkyboxValid = SKYBOX_NONE;
	CachedSkyBoxName[0] = 0;
}


void D3DSky_SetRealTextureDesc (D3D11_TEXTURE2D_DESC *desc, int arraysize)
{
	desc->Usage = D3D11_USAGE_DEFAULT;//D3D11_USAGE_IMMUTABLE;
	desc->CPUAccessFlags = 0;
	desc->BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc->ArraySize = arraysize;
}


void D3DSky_SetSRDFromMR (D3D11_SUBRESOURCE_DATA *srd, D3D11_MAPPED_SUBRESOURCE *mr)
{
	srd->pSysMem = mr->pData;
	srd->SysMemPitch = mr->RowPitch;
	srd->SysMemSlicePitch = mr->DepthPitch;
}


void D3DSky_Load6SiderToCube (QTEXTURE *sbtemps, bool feedback)
{
	D3D11_TEXTURE2D_DESC desc[6];
	D3D11_SUBRESOURCE_DATA srd[6];
	D3D11_MAPPED_SUBRESOURCE mr[6];

	for (int i = 0; i < 6; i++)
	{
		sbtemps[i].GetTextureDesc (&desc[i]);

		// see if we're good for creating a 6-item array
		if (desc[i].Width != desc[i].Height) goto errout;
		if (desc[i].Width != desc[0].Width) goto errout;
		if (desc[i].Height != desc[0].Height) goto errout;
		if (desc[i].Format != desc[0].Format) goto errout;

		// and now map it
		if (!sbtemps[i].GetMapping (&mr[i], 0)) goto errout;
	}

	// source and target for translating from standard load order to QF load order, and everything is valid now so we can work on it
	D3DSky_SetSRDFromMR (&srd[0], &mr[3]);
	D3DSky_SetSRDFromMR (&srd[1], &mr[2]);
	D3DSky_SetSRDFromMR (&srd[4], &mr[0]);
	D3DSky_SetSRDFromMR (&srd[5], &mr[1]);
	D3DSky_SetSRDFromMR (&srd[2], &mr[4]);
	D3DSky_SetSRDFromMR (&srd[3], &mr[5]);

	// create the real desc we'll use
	D3DSky_SetRealTextureDesc (&desc[0], 6);

	desc[0].MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	// and create the array texture
	skyboxarray.CreateTextureAndSRV (&desc[0], srd);

	// finally shut down all of the temp textures (the mapping was guaranteed to have succeeded if we got this far)
	for (int i = 0; i < 6; i++)
	{
		sbtemps[i].Unmap (0);
		sbtemps[i].Release ();
	}

	SkyboxValid = SKYBOX_CUBE;

	if (feedback) Con_Printf ("as cubemap\n");

	return;

errout:;
	for (int i = 0; i < 6; i++)
	{
		// just create 6 regular textures (we may have skipped a desc if we got an early out so just do it again)
		sbtemps[i].GetTextureDesc (&desc[i]);

		// we may have skipped a mapping if we early-outed from the first pass
		if (!mr[i].pData) sbtemps[i].GetMapping (&mr[i], 0);

		if (mr[i].pData)
		{
			D3DSky_SetRealTextureDesc (&desc[i], 1);
			D3DSky_SetSRDFromMR (&srd[i], &mr[i]);

			skyboxtextures[i].CreateTextureAndSRV (&desc[i], &srd[i]);
			sbtemps[i].Unmap (0);
		}

		sbtemps[i].Release ();
	}

	SkyboxValid = SKYBOX_6TEX;

	if (feedback) Con_Printf ("as 6 textures\n");
}


void D3DSky_LoadSkyBox (char *basename, bool feedback)
{
	// force an unload of the current skybox
	D3DSky_UnloadSkybox ();

	int numloaded = 0;
	int sbflags = IMAGE_32BIT | IMAGE_READWRITE;
	char *sbpaths[] = {"gfx/env/", "env/gfx/", "env/", NULL};
	char *skysuffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
	unsigned skynosky[4] = {255 << 24, 255 << 24, 255 << 24, 255 << 24};
	QTEXTURE sbtemps[6];

	for (int sb = 0; sb < 6; sb++)
	{
		// attempt to load it (sometimes an underscore is expected)
		if (!sbtemps[sb].LoadExternal (va ("%s%s", basename, skysuffixes[sb]), sbpaths, sbflags))
		{
			if (!sbtemps[sb].LoadExternal (va ("%s_%s", basename, skysuffixes[sb]), sbpaths, sbflags))
			{
				sbtemps[sb].Upload ((byte *) skynosky, 2, 2, sbflags, NULL);
				continue;
			}
		}

		// loaded OK
		numloaded++;
	}

	if (numloaded)
	{
		// as FQ is the behaviour modders expect let's allow partial skyboxes (much as it galls me)
		if (feedback) Con_Printf ("Loaded %i skybox components ", numloaded);

		// the skybox is valid now, no need to search any more
		D3DSky_Load6SiderToCube (sbtemps, feedback);
		strcpy (CachedSkyBoxName, basename);
	}
	else if (feedback)
	{
		// always unload in case we replaced any missing component with the nosky texture
		D3DSky_UnloadSkybox ();
		Con_Printf ("Failed to load skybox\n");
	}
}


void D3DSky_Loadsky_f (void)
{
	if (Cmd_Argc () != 2)
	{
		Con_Printf ("loadsky <skybox> : loads a skybox\n");
		return;
	}

	// send through the common loader
	D3DSky_LoadSkyBox (Cmd_Argv (1), true);
}


void D3DSky_ParseWorldSpawn (void)
{
	// get a pointer to the entities lump
	char *data = cl.worldmodel->brushhdr->entities;
	char key[40];
	extern char lastworldmodel[];

	// can never happen, otherwise we wouldn't have gotten this far
	if (!data) return;

	// if we're on the same map as before we keep the old settings, otherwise we wipe them
	lastworldmodel[63] = 0;

	if (!strcmp (lastworldmodel, cl.worldmodel->name))
		return;
	else D3DSky_UnloadSkybox ();

	// parse the opening brace
	data = COM_Parse (data);

	// likewise can never happen
	if (!data) return;
	if (com_token[0] != '{') return;

	for (;;)
	{
		// parse the key
		data = COM_Parse (data);

		// there is no key (end of worldspawn)
		if (!data) break;

		if (com_token[0] == '}') break;

		// allow keys with a leading _
		if (com_token[0] == '_')
			Q_strncpy (key, &com_token[1], 39);
		else Q_strncpy (key, com_token, 39);

		// remove trailing spaces
		while (key[strlen (key) - 1] == ' ') key[strlen (key) - 1] = 0;

		// parse the value
		data = COM_Parse (data);

		// likewise should never happen (has already been successfully parsed server-side and any errors that
		// were going to happen would have happened then; we only check to guard against pointer badness)
		if (!data) return;

		// check the key for a sky - notice the lack of standardisation in full swing again here!
		if (!_stricmp (key, "sky") || !_stricmp (key, "skyname") || !_stricmp (key, "q1sky") || !_stricmp (key, "skybox"))
		{
			// attempt to load it (silently fail)
			// direct from com_token - is this safe?  should be...
			D3DSky_LoadSkyBox (com_token, false);
			continue;
		}
	}
}


cmd_t Loadsky1_Cmd ("loadsky", D3DSky_Loadsky_f);
cmd_t Loadsky2_Cmd ("skybox", D3DSky_Loadsky_f);
cmd_t Loadsky3_Cmd ("sky", D3DSky_Loadsky_f);

