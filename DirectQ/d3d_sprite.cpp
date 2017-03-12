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

extern cvar_t r_lightscale;

struct spriteinstance_t
{
	float entorigin[3];
	float uvec[3];
	float rvec[3];
	float color;
	float alpha;
};

struct d3d_spritestate_t
{
	spriteinstance_t *SpriteQuads;
	int NumSprites;
	int LastFrame;
};


d3d_spritestate_t d3d_SpriteState;

struct spritevert_t
{
	float position[2];
};


ID3D11InputLayout *d3d_SpriteLayout = NULL;
ID3D11Buffer *d3d_SpriteVertexes = NULL;
ID3D11VertexShader *d3d_SpriteVertexShader = NULL;
ID3D11PixelShader *d3d_SpritePixelShader[2] = {NULL, NULL};


void D3DSprite_BuildFrameVerts (spritevert_t *verts, mspriteframe_t *frame)
{
	// build in strip order so that we can draw it sensibly
	verts[0].position[0] = frame->down;
	verts[0].position[1] = frame->left;

	verts[1].position[0] = frame->up;
	verts[1].position[1] = frame->left;

	verts[2].position[0] = frame->down;
	verts[2].position[1] = frame->right;

	verts[3].position[0] = frame->up;
	verts[3].position[1] = frame->right;
}


void D3DSprite_InitBuffers (void)
{
	model_t *mod = NULL;

	// it's assumed that we won't have more than 4mb worth of sprites...
	spritevert_t *verts = (spritevert_t *) scratchbuf;
	int numverts = 0;

	// clear down what we already got
	SAFE_RELEASE (d3d_SpriteVertexes);

	for (int i = 0; i < MAX_MOD_KNOWN; i++)
	{
		// nothing allocated yet
		if (!mod_known) continue;
		if (!(mod = mod_known[i])) continue;
		if (mod->type != mod_sprite) continue;

		msprite_t *hdr = mod->spritehdr;

		for (int j = 0; j < hdr->numframes; j++)
		{
			// auto-animation - yuck
			if (hdr->frames[j].type == SPR_SINGLE)
			{
				D3DSprite_BuildFrameVerts (&verts[numverts], hdr->frames[j].frameptr);
				hdr->frames[j].frameptr->firstvertex = numverts;
				numverts += 4;
			}
			else
			{
				mspritegroup_t *pspritegroup = (mspritegroup_t *) hdr->frames[j].frameptr;

				for (int k = 0; k < pspritegroup->numframes; k++)
				{
					D3DSprite_BuildFrameVerts (&verts[numverts], pspritegroup->frames[k]);
					pspritegroup->frames[k]->firstvertex = numverts;
					numverts += 4;
				}
			}
		}
	}

	if (numverts)
	{
		BufferFactory.CreateVertexBuffer (sizeof (spritevert_t), numverts, &d3d_SpriteVertexes, "d3d_SpriteVertexes", scratchbuf);
		Con_DPrintf ("Loaded %i sprite vertexes\n", numverts);
	}
}


void D3DSprite_Init (void)
{
	D3D11_INPUT_ELEMENT_DESC spritelo[] =
	{
		MAKELAYOUTELEMENT ("POSITIONS", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0),
		MAKELAYOUTELEMENT ("ENTORIGIN", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 1),
		MAKELAYOUTELEMENT ("UVECTOR",   0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 1),
		MAKELAYOUTELEMENT ("RVECTOR",   0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 1),
		MAKELAYOUTELEMENT ("DRAWCOLOR", 0, DXGI_FORMAT_R32_FLOAT,		1, 1),
		MAKELAYOUTELEMENT ("DRAWALPHA", 0, DXGI_FORMAT_R32_FLOAT,		1, 1)
	};

	QSHADERFACTORY ShaderFactory (IDR_SPRITEFX);
	D3D10_SHADER_MACRO FogDefines[] = {{"SPRITE_FOG", "1"}, {NULL, NULL}};

	ShaderFactory.CreateVertexShader (&d3d_SpriteVertexShader, "SpriteVS");
	ShaderFactory.CreateInputLayout (&d3d_SpriteLayout, "d3d_SpriteLayout", LAYOUTPARAMS (spritelo));

	ShaderFactory.CreatePixelShader (&d3d_SpritePixelShader[0], "SpritePS");
	ShaderFactory.CreatePixelShader (&d3d_SpritePixelShader[1], "SpritePS", FogDefines);

	d3d_SpriteState.LastFrame = -1;
	d3d_SpriteState.NumSprites = 0;
	d3d_SpriteState.SpriteQuads = NULL;

	D3DSprite_InitBuffers ();
}


void D3DSprite_Shutdown (void)
{
	SAFE_RELEASE (d3d_SpriteVertexes);
}


CD3DInitShutdownHandler d3d_SpriteHandler ("sprite", D3DSprite_Init, D3DSprite_Shutdown);


/*
=============================================================

  SPRITE MODELS

=============================================================
*/

/*
=================
Mod_LoadSpriteFrame
=================
*/
void *Mod_LoadSpriteFrame (model_t *mod, msprite_t *thespr, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[64];

	pinframe = (dspriteframe_t *) pin;

	width = pinframe->width;
	height = pinframe->height;

	size = width * height * (thespr->version == SPR32_VERSION ? 4 : 1);

	pspriteframe = (mspriteframe_t *) MainHunk->Alloc (sizeof (mspriteframe_t));

	memset (pspriteframe, 0, sizeof (mspriteframe_t));

	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = pinframe->origin[0];
	origin[1] = pinframe->origin[1];

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	Q_snprintf (name, 64, "%s_%i", mod->name, framenum);

	// default paths are good for these (d3d11 is rgba not bgra so we don't need to switch the colours for spr32)
	pspriteframe->texture = QTEXTURE::Load
	(
		name,
		width,
		height,
		(byte *) (pinframe + 1),
		IMAGE_MIPMAP | IMAGE_ALPHA | (thespr->version == SPR32_VERSION ? (IMAGE_SPRITE | IMAGE_32BIT) : IMAGE_SPRITE)
	);

	// accumulate total frames including group frames
	pspriteframe->framenum = thespr->totalframes;
	thespr->totalframes++;

	return (void *) ((byte *) pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void *Mod_LoadSpriteGroup (model_t *mod, msprite_t *thespr, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *) pin;
	numframes = pingroup->numframes;

	pspritegroup = (mspritegroup_t *) MainHunk->Alloc (sizeof (mspritegroup_t) + (numframes - 1) * sizeof (pspritegroup->frames[0]));

	pspritegroup->numframes = numframes;
	*ppframe = (mspriteframe_t *) pspritegroup;
	pin_intervals = (dspriteinterval_t *) (pingroup + 1);
	pspritegroup->intervals = (float *) MainHunk->Alloc (numframes * sizeof (float));

	for (i = 0; i < numframes; i++)
	{
		pspritegroup->intervals[i] = pin_intervals->interval;

		if (pspritegroup->intervals[i] <= 0.0)
			Host_Error ("Mod_LoadSpriteGroup: interval<=0");

		pin_intervals++;
	}

	ptemp = (void *) pin_intervals;

	for (i = 0; i < numframes; i++)
		ptemp = Mod_LoadSpriteFrame (mod, thespr, ptemp, &pspritegroup->frames[i], framenum * 100 + i);

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*hdr;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;

	pin = (dsprite_t *) buffer;

	version = pin->version;

	if (version != SPRITE_VERSION && version != SPR32_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i or %i)", mod->name, version, SPRITE_VERSION, SPR32_VERSION);

	numframes = pin->numframes;

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (hdr->frames);

	hdr = (msprite_t *) MainHunk->Alloc (size);

	mod->spritehdr = hdr;

	hdr->type = pin->type;
	hdr->version = version;
	hdr->maxwidth = pin->width;
	hdr->maxheight = pin->height;
	hdr->beamlength = pin->beamlength;
	mod->synctype = (synctype_t) pin->synctype;
	hdr->numframes = numframes;
	hdr->totalframes = 0;

	mod->mins[0] = mod->mins[1] = -hdr->maxwidth / 2;
	mod->maxs[0] = mod->maxs[1] = hdr->maxwidth / 2;
	mod->mins[2] = -hdr->maxheight / 2;
	mod->maxs[2] = hdr->maxheight / 2;

	// load the frames
	if (numframes < 1)
		Host_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *) (pin + 1);

	for (i = 0; i < numframes; i++)
	{
		spriteframetype_t	frametype;

		frametype = (spriteframetype_t) pframetype->type;
		hdr->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
			pframetype = (dspriteframetype_t *) Mod_LoadSpriteFrame (mod, hdr, pframetype + 1, &hdr->frames[i].frameptr, i);
		else pframetype = (dspriteframetype_t *) Mod_LoadSpriteGroup (mod, hdr, pframetype + 1, &hdr->frames[i].frameptr, i);
	}

	mod->type = mod_sprite;
}


//=============================================================================

mspriteframe_t *D3DSprite_GetFrame (entity_t *ent)
{
	msprite_t *hdr = ent->model->spritehdr;
	int frame = ((unsigned) ent->frame) % hdr->numframes;

	if (hdr->frames[frame].type == SPR_SINGLE)
		return hdr->frames[frame].frameptr;
	else
	{
		mspritegroup_t *pspritegroup = (mspritegroup_t *) hdr->frames[frame].frameptr;

		frame = Mod_AnimateGroup (ent, pspritegroup->intervals, pspritegroup->numframes);
		return pspritegroup->frames[frame];
	}
}


void D3DSprite_Begin (void)
{
	d3d11_State->IASetInputLayout (d3d_SpriteLayout);
	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	d3d11_State->IASetVertexBuffer (0, d3d_SpriteVertexes, sizeof (spritevert_t), 0);
	d3d11_State->VSSetShader (d3d_SpriteVertexShader);

	if (!D3DMisc_OverridePS ())
	{
		if (RealFogDensity > 0.0f)
			d3d11_State->PSSetShader (d3d_SpritePixelShader[1]);
		d3d11_State->PSSetShader (d3d_SpritePixelShader[0]);
	}

	d3d11_State->PSSetSampler (0, d3d_DefaultSamplerClamp);

	d3d_SpriteState.LastFrame = -1;
	d3d_SpriteState.NumSprites = 0;
	d3d_SpriteState.SpriteQuads = NULL;
}


void D3DSprite_End (void)
{
	if (d3d_SpriteState.SpriteQuads)
	{
		d3d11_Context->Unmap (QINSTANCE::VertexBuffer, 0);
		d3d_SpriteState.SpriteQuads = NULL;
	}

	if (d3d_SpriteState.NumSprites)
	{
		d3d11_State->SuspendCallback ();
		d3d11_State->IASetVertexBuffer (1, QINSTANCE::VertexBuffer, sizeof (spriteinstance_t), QINSTANCE::MapOffset);

		D3DMisc_DrawInstancedCommon (4, d3d_SpriteState.NumSprites, d3d_SpriteState.LastFrame);

		QINSTANCE::MapOffset += CACHE_ALIGN (d3d_SpriteState.NumSprites * sizeof (spriteinstance_t));
		d3d_SpriteState.NumSprites = 0;
		d3d11_State->ResumeCallback ();
	}
}


void D3DSprite_Draw (entity_t *ent)
{
	float		*uvec, *rvec;
	QMATRIX		av;
	vec3_t		fixed_origin;
	vec3_t		temp;
	float		sr, cr;

	// don't even bother culling, because it's just a single polygon without a surface cache
	mspriteframe_t *frame = D3DSprite_GetFrame (ent);
	msprite_t *hdr = ent->model->spritehdr;

	Vector3Copy (fixed_origin, ent->origin);

	switch (hdr->type)
	{
	case SPR_ORIENTED:
		// bullet marks on walls
		av.AngleVectors (ent->angles);

		Vector3Scale (temp, av.fw, -2);
		Vector3Add (fixed_origin, fixed_origin, temp);

		uvec = av.up;
		rvec = av.rt;
		break;

	case SPR_VP_PARALLEL_UPRIGHT:
		Vector3Set (av.up, 0, 0, 1);

		uvec = av.up;
		rvec = r_viewvectors.rt;
		break;

	case SPR_FACING_UPRIGHT:
		Vector3Set (av.fw, ent->origin[0] - r_refdef.vieworigin[0], ent->origin[1] - r_refdef.vieworigin[1], 0);
		Vector3Normalize (av.fw);

		Vector3Set (av.rt, av.fw[1], -av.fw[0], 0);
		Vector3Set (av.up, 0, 0, 1);

		uvec = av.up;
		rvec = av.rt;
		break;

	case SPR_VP_PARALLEL:
		// normal sprite
		uvec = r_viewvectors.up;
		rvec = r_viewvectors.rt;
		break;

	case SPR_VP_PARALLEL_ORIENTED:
		Q_sincos ((ent->angles[2] * D3DX_PI) / 180.0, &sr, &cr);

		Vector3Set (av.rt, r_viewvectors.rt[0] * cr + r_viewvectors.up[0] * sr, r_viewvectors.rt[1] * cr + r_viewvectors.up[1] * sr, r_viewvectors.rt[2] * cr + r_viewvectors.up[2] * sr);
		Vector3Set (av.up, r_viewvectors.rt[0] * -sr + r_viewvectors.up[0] * cr, r_viewvectors.rt[1] * -sr + r_viewvectors.up[1] * cr, r_viewvectors.rt[2] * -sr + r_viewvectors.up[2] * cr);

		uvec = av.up;
		rvec = av.rt;
		break;

	default:
		// unknown type - just assume it's normal and Con_DPrintf it
		uvec = r_viewvectors.up;
		rvec = r_viewvectors.rt;
		Con_DPrintf ("D3DSprite_Draw - Unknown Sprite Type %i\n", hdr->type);
		break;
	}

	if (frame->firstvertex != d3d_SpriteState.LastFrame)
	{
		// if the frame changed we need to begin a new instance batch (flushing the old)
		// the texture is normally expected to change here too
		D3DSprite_End ();

		if (r_lightmap.integer)
			d3d11_State->PSSetTexture (0, &QTEXTURE::WhiteTexture);
		else d3d11_State->PSSetTexture (0, frame->texture);

		d3d_SpriteState.LastFrame = frame->firstvertex;
	}

	D3D11_MAPPED_SUBRESOURCE MappedResource;
	D3D11_MAP MapType = D3D11_MAP_WRITE_NO_OVERWRITE;

	if (QINSTANCE::MapOffset + CACHE_ALIGN ((d3d_SpriteState.NumSprites + 1) * sizeof (spriteinstance_t)) >= QINSTANCE::BufferMax)
	{
		D3DSprite_End ();
		MapType = D3D11_MAP_WRITE_DISCARD;
		QINSTANCE::MapOffset = 0;
	}

	if (!d3d_SpriteState.SpriteQuads)
	{
		if (FAILED (d3d11_Context->Map (QINSTANCE::VertexBuffer, 0, MapType, 0, &MappedResource)))
			return;
		else d3d_SpriteState.SpriteQuads = (spriteinstance_t *) (&((byte *) MappedResource.pData)[QINSTANCE::MapOffset]);
	}

	// and write in the sprite instance
	Vector3Copy (d3d_SpriteState.SpriteQuads[d3d_SpriteState.NumSprites].entorigin, fixed_origin);
	Vector3Copy (d3d_SpriteState.SpriteQuads[d3d_SpriteState.NumSprites].uvec, uvec);
	Vector3Copy (d3d_SpriteState.SpriteQuads[d3d_SpriteState.NumSprites].rvec, rvec);

	d3d_SpriteState.SpriteQuads[d3d_SpriteState.NumSprites].color = r_lightscale.value;

	if (ent->alphaval < 1 || ent->alphaval > 254)
		d3d_SpriteState.SpriteQuads[d3d_SpriteState.NumSprites].alpha = 1.0f;
	else d3d_SpriteState.SpriteQuads[d3d_SpriteState.NumSprites].alpha = (float) ent->alphaval / 255.0f;

	d3d_SpriteState.NumSprites++;
}


