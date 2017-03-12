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

// draw.c -- this is the only file outside the refresh that touches the
// vid buffer

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"
#include "resource.h"

#define MAX_CHAR_TEXTURES	64

QTEXTURE *char_texture = NULL;
QTEXTURE char_textures[MAX_CHAR_TEXTURES];

QTEXTURE crosshairtexture;
QTEXTURE mapshottexture;

extern ID3D11Texture2D *d3d_ScreenTexture;
extern ID3D11ShaderResourceView *d3d_ScreenSRV;
extern ID3D11RenderTargetView *d3d_ScreenRTV;
extern int d3d_ScreenNumMips;

void Draw_FreeCrosshairs (void);

// 512 x 256 is adequate for ID1; the extra space is for rogue/hipnotic extras
#define	MAX_SCRAPS			8
#define	SCRAP_BLOCKSIZE		512


struct d3d_drawscrap_t
{
	QTEXTURE tex;
	byte *data;
	int *allocated;
	bool dirty;
};

d3d_drawscrap_t d3d11_scraptextures[MAX_SCRAPS];
bool d3d_ScrapDirty = false;


struct brightpass_t
{
	float rgb_gamma[4];
	float rgb_contrast[4];
	float mipLevel;
	float Junk[3];
};


struct d3d_drawcbuffer_t
{
	QMATRIX drawMatrix;
};


struct drawinst_t
{
	float xl, xh, yl, yh;
	float sl, sh, tl, th;
	unsigned int color;
};


struct d3d_drawstate_t
{
	drawinst_t *DrawQuads;
	int NumQuads;
	unsigned int Color2D;
};


d3d_drawstate_t d3d_DrawState;


ID3D11Buffer *d3d_DrawConstants = NULL;
ID3D11Buffer *d3d_CBConstants = NULL;
ID3D11Buffer *d3d_BrightPassConstants = NULL;

ID3D11InputLayout *d3d_DrawLayout = NULL;
ID3D11VertexShader *d3d_DrawVertexShader = NULL;
ID3D11VertexShader *d3d_BrightPassVertexShader = NULL;
ID3D11PixelShader *d3d_BrightPassPixelShader = NULL;
ID3D11PixelShader *d3d_WireFramePixelShader = NULL;
ID3D11PixelShader *d3d_DrawFlatPixelShader = NULL;
ID3D11PixelShader *d3d_ShowDepthPixelShader = NULL;
ID3D11PixelShader *d3d_DrawPixelShader = NULL;
ID3D11PixelShader *d3d_FadePixelShader = NULL;
ID3D11PixelShader *d3d_CBPixelShader = NULL;
ID3D11PixelShader *d3d_ColorPixelShader = NULL;


void D3DDraw_Init (void)
{
	if (!d3d_DrawLayout)
	{
		D3D11_INPUT_ELEMENT_DESC drawlo[] =
		{
			MAKELAYOUTELEMENT ("POSITIONS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 1),
			MAKELAYOUTELEMENT ("TEXCOORDS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 1),
			MAKELAYOUTELEMENT ("DRAWCOLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 1)
		};

		QSHADERFACTORY ShaderFactory (IDR_DRAWFX);

		ShaderFactory.CreateVertexShader (&d3d_DrawVertexShader, "DrawVS");
		ShaderFactory.CreateInputLayout (&d3d_DrawLayout, "d3d_DrawLayout", LAYOUTPARAMS (drawlo));
		ShaderFactory.CreateVertexShader (&d3d_BrightPassVertexShader, "BrightPassVS");

		ShaderFactory.CreatePixelShader (&d3d_DrawPixelShader, "DrawPS");
		ShaderFactory.CreatePixelShader (&d3d_BrightPassPixelShader, "BrightPassPS");
		ShaderFactory.CreatePixelShader (&d3d_FadePixelShader, "FadePS");
		ShaderFactory.CreatePixelShader (&d3d_CBPixelShader, "DogPS");
		ShaderFactory.CreatePixelShader (&d3d_WireFramePixelShader, "RWireFramePS");
		ShaderFactory.CreatePixelShader (&d3d_DrawFlatPixelShader, "RDrawFlatPS");
		ShaderFactory.CreatePixelShader (&d3d_ShowDepthPixelShader, "RShowDepthPS");
		ShaderFactory.CreatePixelShader (&d3d_ColorPixelShader, "ColorPS");
	}

	BufferFactory.CreateConstantBuffer (sizeof (d3d_drawcbuffer_t), &d3d_DrawConstants, "d3d_DrawConstants");
	BufferFactory.CreateConstantBuffer (sizeof (float) * 12, &d3d_CBConstants, "d3d_CBConstants");
	BufferFactory.CreateConstantBuffer (sizeof (brightpass_t), &d3d_BrightPassConstants, "d3d_BrightPassConstants");

	crosshairtexture.FromResource (IDR_CROSSHAIR, IMAGE_ALPHA | IMAGE_ALPHAMASK);
}


void D3DDraw_Shutdown (void)
{
	for (int i = 0; i < MAX_CHAR_TEXTURES; i++)
		char_textures[i].Release ();

	char_texture = NULL;

	crosshairtexture.Release ();
	mapshottexture.Release ();

	Draw_FreeCrosshairs ();

	for (int i = 0; i < MAX_SCRAPS; i++)
	{
		if (d3d11_scraptextures[i].data) memset (d3d11_scraptextures[i].data, 255, SCRAP_BLOCKSIZE * SCRAP_BLOCKSIZE);
		if (d3d11_scraptextures[i].allocated) memset (d3d11_scraptextures[i].allocated, 0, SCRAP_BLOCKSIZE * sizeof (int));

		d3d11_scraptextures[i].tex.Release ();
		d3d11_scraptextures[i].dirty = false;
	}

	d3d_ScrapDirty = false;

	SAFE_RELEASE (d3d_CBConstants);
	SAFE_RELEASE (d3d_DrawConstants);
	SAFE_RELEASE (d3d_BrightPassConstants);
}


CD3DInitShutdownHandler d3d_DrawHandler ("draw", D3DDraw_Init, D3DDraw_Shutdown);


struct texrect_t
{
	float sl;
	float sh;
	float tl;
	float th;
};

struct glpic_t
{
	QTEXTURE *Texture;
	int flags;
	texrect_t picrect;
	int texwidth;
	int texheight;
};


qpic_t *conback;

cvar_t r_smoothcharacters ("r_smoothcharacters", "0", CVAR_ARCHIVE);


void D3DDraw_DrawBatch (void)
{
	if (d3d_DrawState.DrawQuads)
	{
		d3d11_Context->Unmap (QINSTANCE::VertexBuffer, 0);
		d3d_DrawState.DrawQuads = NULL;
	}

	if (d3d_DrawState.NumQuads)
	{
		d3d11_State->SuspendCallback ();
		d3d11_State->IASetVertexBuffer (0, QINSTANCE::VertexBuffer, sizeof (drawinst_t), QINSTANCE::MapOffset);

		D3DMisc_DrawInstancedCommon (4, d3d_DrawState.NumQuads);

		QINSTANCE::MapOffset += CACHE_ALIGN (d3d_DrawState.NumQuads * sizeof (drawinst_t));
		d3d_DrawState.NumQuads = 0;
		d3d11_State->ResumeCallback ();
	}
}


texrect_t defaulttexrect = {0, 1, 0, 1};

void D3DDraw_SubmitQuad (float x, float y, float w, float h, unsigned int c = 0xffffffff, texrect_t *texrect = &defaulttexrect)
{
	if (d3d_ScrapDirty)
	{
		// ??? needed ???
		D3DDraw_DrawBatch ();

		for (int i = 0; i < MAX_SCRAPS; i++)
		{
			if (d3d11_scraptextures[i].dirty)
			{
				d3d11_scraptextures[i].tex.Release ();
				d3d11_scraptextures[i].tex.Upload (d3d11_scraptextures[i].data, SCRAP_BLOCKSIZE, SCRAP_BLOCKSIZE, IMAGE_ALPHA, d3d_QuakePalette.standard11);
				d3d11_scraptextures[i].dirty = false;
			}
		}

		d3d_ScrapDirty = false;
	}

	D3D11_MAPPED_SUBRESOURCE MappedResource;
	D3D11_MAP MapType = D3D11_MAP_WRITE_NO_OVERWRITE;

	if (QINSTANCE::MapOffset + CACHE_ALIGN ((d3d_DrawState.NumQuads + 1) * sizeof (drawinst_t)) >= QINSTANCE::BufferMax)
	{
		D3DDraw_DrawBatch ();
		MapType = D3D11_MAP_WRITE_DISCARD;
		QINSTANCE::MapOffset = 0;
	}

	if (!d3d_DrawState.DrawQuads)
	{
		if (FAILED (d3d11_Context->Map (QINSTANCE::VertexBuffer, 0, MapType, 0, &MappedResource)))
			return;
		else d3d_DrawState.DrawQuads = (drawinst_t *) (&((byte *) MappedResource.pData)[QINSTANCE::MapOffset]);
	}

	drawinst_t *di = &d3d_DrawState.DrawQuads[d3d_DrawState.NumQuads];

	di->xl = x;
	di->xh = x + w;
	di->yl = y;
	di->yh = y + h;

	di->sl = texrect->sl;
	di->sh = texrect->sh;
	di->tl = texrect->tl;
	di->th = texrect->th;

	di->color = c;

	d3d_DrawState.NumQuads++;
}


void D3DDraw_Begin2D (void);

texrect_t charrects[256];


/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	for (texnum = 0; texnum < MAX_SCRAPS; texnum++)
	{
		if (!d3d11_scraptextures[texnum].data)
			d3d11_scraptextures[texnum].data = (byte *) MainZone->Alloc (SCRAP_BLOCKSIZE * SCRAP_BLOCKSIZE);

		if (!d3d11_scraptextures[texnum].allocated)
			d3d11_scraptextures[texnum].allocated = (int *) MainZone->Alloc (SCRAP_BLOCKSIZE * sizeof (int));

		best = SCRAP_BLOCKSIZE;

		for (i = 0; i < SCRAP_BLOCKSIZE - w; i++)
		{
			best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (d3d11_scraptextures[texnum].allocated[i + j] >= best) break;
				if (d3d11_scraptextures[texnum].allocated[i + j] > best2) best2 = d3d11_scraptextures[texnum].allocated[i + j];
			}

			if (j == w)
			{
				// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > SCRAP_BLOCKSIZE)
			continue;

		for (i = 0; i < w; i++)
			d3d11_scraptextures[texnum].allocated[*x + i] = best + h;

		return texnum;
	}

	return -1;
}


void SCR_RefdefCvarChange (cvar_t *blah);

cvar_t gl_conscale ("gl_conscale", "1", CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t scr_sbarscale ("scr_sbarscale", "1", CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t scr_menuscale ("scr_menuscale", "1", CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t scr_conscale ("scr_conscale", "1", CVAR_ARCHIVE, SCR_RefdefCvarChange);

char *gfxlmps[] =
{
	"bigbox.lmp", "box_bl.lmp", "box_bm.lmp", "box_br.lmp", "box_ml.lmp", "box_mm.lmp", "box_mm2.lmp", "box_mr.lmp", "box_tl.lmp", "box_tm.lmp", "box_tr.lmp",
	"colormap.lmp", "complete.lmp", "conback.lmp", "dim_drct.lmp", "dim_ipx.lmp", "dim_modm.lmp", "dim_mult.lmp", "dim_tcp.lmp", "finale.lmp", "help0.lmp",
	"help1.lmp", "help2.lmp", "help3.lmp", "help4.lmp", "help5.lmp", "inter.lmp", "loading.lmp", "mainmenu.lmp", "menudot1.lmp", "menudot2.lmp", "menudot3.lmp",
	"menudot4.lmp", "menudot5.lmp", "menudot6.lmp", "menuplyr.lmp", "mp_menu.lmp", "netmen1.lmp", "netmen2.lmp", "netmen3.lmp", "netmen4.lmp", "netmen5.lmp",
	"palette.lmp", "pause.lmp", "p_load.lmp", "p_multi.lmp", "p_option.lmp", "p_save.lmp", "qplaque.lmp", "ranking.lmp", "sell.lmp", "sp_menu.lmp", "ttl_cstm.lmp",
	"ttl_main.lmp", "ttl_sgl.lmp", "vidmodes.lmp", NULL
};


cvar_t		gl_nobind ("gl_nobind", "0");

qpic_t		*draw_disc;
qpic_t		*draw_backtile;

struct crosshair_t
{
	texrect_t rect;
	bool replaced;
	QTEXTURE texture;
};

int d3d_NumCrosshairs = 0;
crosshair_t *d3d_Crosshairs = NULL;


//=============================================================================
/* Support Routines */

byte		*menuplyr_pixels = NULL;
byte		*menuplyr_pixels_translated = NULL;

int		pic_texels;
int		pic_count;

// failsafe pic for when a load from the wad fails
// (prevent crashes here during mod dev cycles)
byte *failsafedata = NULL;
qpic_t *draw_failsafe = NULL;


void Draw_SetGLPic (glpic_t *gl, QTEXTURE *tex, float sl, float tl, float sh, float th)
{
	gl->Texture = tex;
	gl->picrect.sl = sl;
	gl->picrect.tl = tl;
	gl->picrect.sh = sh;
	gl->picrect.th = th;
}


bool Draw_TryScrap (qpic_t *pic)
{
	int	x, y;
	int	texnum;

	// store out width and height so that overwriting them with a glpic_t won't corrupt them
	int width = pic->width;
	int height = pic->height;

	// pad the allocation to prevent linear filtering from causing the edges of adjacent textures to bleed into each other
	int scrapw = width + 4;
	int scraph = height + 4;

	if (scrapw > SCRAP_BLOCKSIZE) return false;
	if (scraph > SCRAP_BLOCKSIZE) return false;

	// find a padded block
	if ((texnum = Scrap_AllocBlock (scrapw, scraph, &x, &y)) == -1) return false;

	// center in the padded region
	x += 2;
	y += 2;

	byte *texels = d3d11_scraptextures[texnum].data;

	// pad up/down/left/right so that the correct texels will be caught when filtering
	for (int i = 0, k = 0; i < height; i++)
		for (int j = 0; j < width; j++, k++)
			texels[((y - 1) + i) * SCRAP_BLOCKSIZE + x + j] = pic->data[k];

	for (int i = 0, k = 0; i < height; i++)
		for (int j = 0; j < width; j++, k++)
			texels[((y + 1) + i) * SCRAP_BLOCKSIZE + x + j] = pic->data[k];

	for (int i = 0, k = 0; i < height; i++)
		for (int j = 0; j < width; j++, k++)
			texels[(y + i) * SCRAP_BLOCKSIZE + (x - 1) + j] = pic->data[k];

	for (int i = 0, k = 0; i < height; i++)
		for (int j = 0; j < width; j++, k++)
			texels[(y + i) * SCRAP_BLOCKSIZE + (x + 1) + j] = pic->data[k];

	// do the final centered image
	for (int i = 0, k = 0; i < height; i++)
		for (int j = 0; j < width; j++, k++)
			texels[(y + i) * SCRAP_BLOCKSIZE + x + j] = pic->data[k];

	glpic_t *gl = (glpic_t *) pic->data;

	// scrap textures just go at their default sizes
	Draw_SetGLPic (gl, &d3d11_scraptextures[texnum].tex,
		(float) x / (float) SCRAP_BLOCKSIZE,
		(float) y / (float) SCRAP_BLOCKSIZE,
		(float) (x + width) / (float) SCRAP_BLOCKSIZE,
		(float) (y + height) / (float) SCRAP_BLOCKSIZE);

	gl->texwidth = SCRAP_BLOCKSIZE;
	gl->texheight = SCRAP_BLOCKSIZE;

	// and dirty the scrap
	d3d11_scraptextures[texnum].dirty = true;
	d3d_ScrapDirty = true;

	// that's all we need
	return true;
}


void Draw_SaveMenuPlayer (qpic_t *pic)
{
	menuplyr_pixels = (byte *) GameHunk->Alloc (pic->width * pic->height);
	menuplyr_pixels_translated = (byte *) GameHunk->Alloc (pic->width * pic->height);
	Q_MemCpy (menuplyr_pixels, pic->data, pic->width * pic->height);
	Q_MemCpy (menuplyr_pixels_translated, pic->data, pic->width * pic->height);
}


bool Draw_TryExternalPic (char *name, qpic_t *pic, byte *data, int width, int height, char **paths)
{
	QTEXTURE *tex = NULL;
	glpic_t *gl = (glpic_t *) pic->data;

	// remove the path prefix from the texture so that we can load it from alternate paths
	char *loadname = (!_strnicmp (name, "gfx/", 4)) ? &name[4] : name;

	if ((tex = QTEXTURE::Load (loadname, width, height, data, IMAGE_ALPHA | IMAGE_EXTERNONLY, paths)) != NULL)
	{
		Draw_SetGLPic (gl, tex, 0, 0, 1, 1);

		// these need to be set to the original pic size, not the loaded texture size, so that subpics will work
		gl->texwidth = width;
		gl->texheight = height;

		return true;
	}
	else return false;
}


qpic_t *Draw_LoadPic (char *name, bool allowscrap)
{
	qpic_t *pic = NULL;

	// this should never happen
	if (!name) return NULL;

	// look for it in the gfx.wad first
	if ((pic = (qpic_t *) gfxwad.FindLump (name)) == NULL)
	{
		// no, look for it in the direct path given - if that fails then use the failsafe pic
		if ((pic = (qpic_t *) CQuakeFile::LoadFile (name, GameHunk)) == NULL)
			pic = draw_failsafe;

		// never take these from the scrap
		allowscrap = false;
	}

	if (pic->width < 1 || pic->height < 1)
	{
		// this occasionally gets hosed, dunno why yet...
		// seems preferable to crashing
		Con_Printf ("Draw_LoadPic : pic->width < 1 || pic->height < 1 for %s\n(I fucked up - sorry)\n", name);
		pic = draw_failsafe;
	}

	// quake weenie-ism at it's finest
	// we don't bother validating the paths here as this is primarily a startup-time only thing
	char *paths[] = {"gfx/", "gfx/wad/", "textures/gfx/", "textures/gfx/wad/", "textures/", NULL};

	// store out width and height so that overwriting them with a glpic_t won't hose them
	int width = pic->width;
	int height = pic->height;

	// HACK HACK HACK --- we need to keep the bytes for the translatable player picture just for the menu configuration dialog
	// this should NEVER be an external texture because the translation would break
	if (!strcmp (name, "gfx/menuplyr.lmp"))
	{
		// this should only happen once
		Draw_SaveMenuPlayer (pic);
		allowscrap = false;
	}
	else if (Draw_TryExternalPic (name, pic, pic->data, width, height, paths))
		return pic;

	// try to put it in the scrap if possible
	if (allowscrap && Draw_TryScrap (pic))
		return pic;

	// explicitly no extern because we already tried for one above
	QTEXTURE *tex = QTEXTURE::Load (name, width, height, pic->data, IMAGE_ALPHA | IMAGE_NOEXTERN);
	glpic_t *gl = (glpic_t *) pic->data;

	Draw_SetGLPic (gl, tex, 0, 0, 1, 1);
	gl->texwidth = width;
	gl->texheight = height;

	return pic;
}


/*
===============
Draw_Init
===============
*/
void Draw_FreeCrosshairs (void)
{
	if (!d3d_Crosshairs) return;

	for (int i = 0; i < d3d_NumCrosshairs; i++)
	{
		// only textures which were replaced can be released here
		// the others are released separately
		if (d3d_Crosshairs[i].replaced)
		{
			d3d_Crosshairs[i].texture.Release ();

			// prevent multiple release attempts
			d3d_Crosshairs[i].replaced = false;
		}
	}

	// this will force a load the first time we draw
	d3d_NumCrosshairs = 0;
	d3d_Crosshairs = NULL;
}


void Draw_LoadCrosshairs (void)
{
	// free anything that we may have had previously
	Draw_FreeCrosshairs ();

	// load them into the scratch buffer to begin with because we don't know how many we'll have
	d3d_Crosshairs = (crosshair_t *) scratchbuf;

	// full texcoords for each value
	int xhairs[] = {0, 32, 64, 96, 0, 32, 64, 96};
	int xhairt[] = {0, 0, 0, 0, 32, 32, 32, 32};

	// load default crosshairs
	for (int i = 0; i < 10; i++)
	{
		if (i < 2)
		{
			// + sign crosshairs
			d3d_Crosshairs[i].texture = char_textures[0];

			d3d_Crosshairs[i].rect.sl = charrects['+' + (128 * i)].sl;
			d3d_Crosshairs[i].rect.sh = charrects['+' + (128 * i)].sh;
			d3d_Crosshairs[i].rect.tl = charrects['+' + (128 * i)].tl;
			d3d_Crosshairs[i].rect.th = charrects['+' + (128 * i)].th;
		}
		else
		{
			// crosshair images
			d3d_Crosshairs[i].texture = crosshairtexture;

			d3d_Crosshairs[i].rect.sl = (float) xhairs[i - 2] / 128.0f;
			d3d_Crosshairs[i].rect.sh = (float) (xhairs[i - 2] + 32) / 128.0f;
			d3d_Crosshairs[i].rect.tl = (float) xhairt[i - 2] / 64.0f;
			d3d_Crosshairs[i].rect.th = (float) (xhairt[i - 2] + 32) / 64.0f;
		}

		// we need to track if the image has been replaced so that we know to add colour to crosshair 1 and 2 if so
		d3d_Crosshairs[i].replaced = false;
	}

	// nothing here to begin with
	d3d_NumCrosshairs = 0;

	// conform to qrack (fixme - what does dp do?)
	char *paths[] = {"crosshairs/", "gfx/", "textures/crosshairs/", "textures/gfx/", NULL};

	// now attempt to load replacements
	for (int i = 0;; i++)
	{
		QTEXTURE newcrosshair;

		// attempt to load one
		// standard loader; qrack crosshairs begin at 1 and so should we
		// it is assumed that these include alpha channels so don't mask them
		if (!newcrosshair.LoadExternal (va ("crosshair%i", i + 1), paths, IMAGE_ALPHA))
			break;

		d3d_Crosshairs[i].texture = newcrosshair;
		d3d_Crosshairs[i].rect.sl = 0;
		d3d_Crosshairs[i].rect.tl = 0;
		d3d_Crosshairs[i].rect.sh = 1;
		d3d_Crosshairs[i].rect.th = 1;
		d3d_Crosshairs[i].replaced = true;

		// mark a new crosshair
		d3d_NumCrosshairs++;
	}

	// always include the standard images
	if (d3d_NumCrosshairs < 10) d3d_NumCrosshairs = 10;

	// now set them up in memory for real - put them in the main zone so that we can free replacement textures properly
	d3d_Crosshairs = (crosshair_t *) MainZone->Alloc (d3d_NumCrosshairs * sizeof (crosshair_t));
	Q_MemCpy (d3d_Crosshairs, scratchbuf, d3d_NumCrosshairs * sizeof (crosshair_t));
}


void Draw_SetCharrects (int h, int v, int addx, int addy)
{
	int yadd = v / 16;
	int xadd = h / 16;

	for (int y = 0, z = 0; y < v; y += yadd)
	{
		for (int x = 0; x < h; x += xadd, z++)
		{
			charrects[z].sl = (float) x / (float) h;
			charrects[z].sh = (float) (x + addx) / (float) h;
			charrects[z].tl = (float) y / (float) v;
			charrects[z].th = (float) (y + addy) / (float) v;
		}
	}
}


qpic_t *box_ml;
qpic_t *box_mr;
qpic_t *box_tm;
qpic_t *box_bm;
qpic_t *box_mm2;
qpic_t *box_tl;
qpic_t *box_tr;
qpic_t *box_bl;
qpic_t *box_br;
qpic_t *gfx_pause_lmp;
qpic_t *gfx_loading_lmp;

void Menu_InitPics (void);
void HUD_InitPics (void);
void Scr_InitPics (void);

void Draw_Init (void)
{
	// default chaaracter rectangles for the standard charset
	Draw_SetCharrects (128, 128, 8, 8);

	if (!failsafedata)
	{
		// this is a qpic_t that's used in the event of any qpic_t failing to load!!!
		// we alloc enough memory for the glpic_t that draw_failsafe->data is casted to.
		// this crazy-assed shit prevents an "ambiguous call to overloaded function" error.
		// add 1 cos of integer round down.  do it this way in case we ever change the glpic_t struct
		int failsafedatasize = D3DImage_PowerOf2Size ((int) sqrt ((float) (sizeof (glpic_t))) + 1);

		// persist in memory
		failsafedata = (byte *) MainZone->Alloc (sizeof (int) * 2 + (failsafedatasize * failsafedatasize));
		draw_failsafe = (qpic_t *) failsafedata;
		draw_failsafe->height = failsafedatasize;
		draw_failsafe->width = failsafedatasize;
	}

	byte *charset = (byte *) gfxwad.FindLump ("conchars");

	// switch to proper alpha colour
	for (int i = 0; i < (128 * 128); i++)
		if (charset[i] == 0)
			charset[i] = 255;

	char_textures[0].Upload (charset, 128, 128, IMAGE_ALPHA, d3d_QuakePalette.standard11);
	conback = Draw_LoadPic ("gfx/conback.lmp", false);

	// get the other pics we need
	// draw_disc is also used on the sbar so we need to retain it
	draw_disc = Draw_LoadPic ("disc");
	draw_backtile = Draw_LoadPic ("backtile", false);

	box_ml = Draw_LoadPic ("gfx/box_ml.lmp");
	box_mr = Draw_LoadPic ("gfx/box_mr.lmp");
	box_tm = Draw_LoadPic ("gfx/box_tm.lmp");
	box_bm = Draw_LoadPic ("gfx/box_bm.lmp");
	box_mm2 = Draw_LoadPic ("gfx/box_mm2.lmp");
	box_tl = Draw_LoadPic ("gfx/box_tl.lmp");
	box_tr = Draw_LoadPic ("gfx/box_tr.lmp");
	box_bl = Draw_LoadPic ("gfx/box_bl.lmp");
	box_br = Draw_LoadPic ("gfx/box_br.lmp");

	gfx_pause_lmp = Draw_LoadPic ("gfx/pause.lmp");
	gfx_loading_lmp = Draw_LoadPic ("gfx/loading.lmp");

	// and get the rest of the pics
	Menu_InitPics ();
	HUD_InitPics ();
	Scr_InitPics ();
}

CD3DInitShutdownHandler d3d_DrawPicHandler ("draw pic", Draw_Init, NULL);

void D3DDraw_SetState (ID3D11PixelShader *shader, QTEXTURE *texture = NULL, ID3D11SamplerState *sampler = NULL)
{
	if (shader) d3d11_State->PSSetShader (shader);
	if (texture) d3d11_State->PSSetTexture (0, texture);
	if (sampler) d3d11_State->PSSetSampler (3, sampler);
}


cvar_t gl_consolefont ("gl_consolefont", "0", CVAR_ARCHIVE);

float font_scale_x = 1, font_scale_y = 1;

int Draw_PrepareCharacter (int x, int y, int num)
{
	static int oldfont = -1;

	// get the correct charset to use
	if (gl_consolefont.integer != oldfont || !char_texture)
	{
		// release all except 0 which is the default texture
		for (int i = 1; i < MAX_CHAR_TEXTURES; i++)
			char_textures[i].Release ();

		// bound it
		if (gl_consolefont.integer >= MAX_CHAR_TEXTURES) gl_consolefont.Set (0.0f);
		if (gl_consolefont.integer < 0) gl_consolefont.Set (0.0f);

		char *paths[] =
		{
			"textures/charsets/",
			"textures/gfx/",
			"gfx/",
			NULL
		};

		// load them dynamically so that we don't waste vram by having up to 49 big textures in memory at once!
		// we guaranteed that char_textures[0] is always loaded so this will always terminate
		for (;;)
		{
			// no more textures
			if (gl_consolefont.integer == 0) break;

			// attempt to load it
			if (char_textures[gl_consolefont.integer & 63].LoadExternal (va ("charset-%i", gl_consolefont.integer), paths, IMAGE_ALPHA)) break;

			// go to the next one
			gl_consolefont.integer--;
		}

		// set the correct font texture (this can be 0)
		char_texture = &char_textures[gl_consolefont.integer & 63];

		// store back
		gl_consolefont.Set (gl_consolefont.integer);
		oldfont = gl_consolefont.integer;

		// to do - set up character rectangles (and scales for external charsets!)
		if (gl_consolefont.integer > 0)
		{
			D3D11_TEXTURE2D_DESC texdesc;

			char_texture->GetTextureDesc (&texdesc);
			Draw_SetCharrects (texdesc.Width, texdesc.Height, texdesc.Width / 16, texdesc.Height / 16);
			font_scale_x = 128.0f / (float) texdesc.Width;
			font_scale_y = 128.0f / (float) texdesc.Height;
		}
		else
		{
			Draw_SetCharrects (128, 128, 8, 8);
			font_scale_x = font_scale_y = 1;
		}
	}
	else if (!char_textures[gl_consolefont.integer & 63].HasTexture ())
	{
		char_texture = &char_textures[0];
		gl_consolefont.Set (0.0f);
	}

	num &= 255;

	// don't draw spaces (catch both white and orange chars)
	if ((num & 127) == 32) return 0;

	// check for offscreen
	if (y <= -8) return 0;
	if (y >= vid.currsize.height) return 0;
	if (x <= -8) return 0;
	if (x >= vid.currsize.width) return 0;

	// ok to draw
	return num;
}


/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Character (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	num &= 255;

	D3DDraw_SetState (d3d_DrawPixelShader, char_texture, r_smoothcharacters.integer ? d3d_SampleClampLinear : d3d_SampleClampPoint);
	D3DDraw_SubmitQuad (x, y, 8, 8, d3d_DrawState.Color2D, &charrects[num]);
}


void Draw_BackwardsCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	num &= 255;

	texrect_t rect =
	{
		charrects[num].sh,
		charrects[num].sl,
		charrects[num].tl,
		charrects[num].th
	};

	D3DDraw_SetState (d3d_DrawPixelShader, char_texture, r_smoothcharacters.integer ? d3d_SampleClampLinear : d3d_SampleClampPoint);
	D3DDraw_SubmitQuad (x, y, 8, 8, d3d_DrawState.Color2D, &rect);
}


void Draw_RotateCharacter (int x, int y, int num)
{
	if (!(num = Draw_PrepareCharacter (x, y, num))) return;

	num &= 255;

	texrect_t rect =
	{
		// fixme - this is wrong now...
		charrects[num].sh,
		charrects[num].sl,
		charrects[num].th,
		charrects[num].tl
	};

	D3DDraw_SetState (d3d_DrawPixelShader, char_texture, r_smoothcharacters.integer ? d3d_SampleClampLinear : d3d_SampleClampPoint);
	D3DDraw_SubmitQuad (x, y, 8, 8, d3d_DrawState.Color2D, &rect);
}


void Draw_VScrollBar (int x, int y, int height, int pos, int scale) {}


/*
================
Draw_String
================
*/
void Draw_String (int x, int y, char *str, int ofs)
{
	while (str[0])
	{
		Draw_Character (x, y, str[0] + ofs);
		str++;
		x += 8;
	}
}


/*
================
Draw_DebugChar

Draws a single character directly to the upper right corner of the screen.
This is for debugging lockups by drawing different chars in different parts
of the code.
================
*/
void Draw_DebugChar (char num)
{
	Draw_Character (vid.currsize.width - 20, 20, num);
}


/*
=============
Draw_Pic
=============
*/
void Draw_SubPic (int x, int y, qpic_t *pic, int srcx, int srcy, int width, int height)
{
	glpic_t *gl = (glpic_t *) pic->data;

	texrect_t rect =
	{
		gl->picrect.sl + (float) srcx / (float) gl->texwidth,
		gl->picrect.sl + (float) (srcx + width) / (float) gl->texwidth,
		gl->picrect.tl + (float) srcy / (float) gl->texheight,
		gl->picrect.tl + (float) (srcy + height) / (float) gl->texheight
	};

	D3DDraw_SetState (d3d_DrawPixelShader, gl->Texture, d3d_SampleClampLinear);
	D3DDraw_SubmitQuad (x, y, width, height, d3d_DrawState.Color2D, &rect);
}


void Draw_Pic (int x, int y, qpic_t *pic, float alpha, bool clamp)
{
	glpic_t *gl = (glpic_t *) pic->data;
	unsigned int alphacolor;

	if (alpha < 1)
		alphacolor = (BYTE_CLAMPF (alpha) << 24) | (255 << 0) | (255 << 8) | (255 << 16);
	else alphacolor = d3d_DrawState.Color2D;

	D3DDraw_SetState (d3d_DrawPixelShader, gl->Texture, clamp ? d3d_SampleClampLinear : d3d_SampleWrapLinear);
	D3DDraw_SubmitQuad (x, y, pic->width, pic->height, alphacolor, &gl->picrect);
}


void Draw_Pic (int x, int y, int w, int h, QTEXTURE *texpic)
{
	texrect_t rect = {0, 1, 0, 1};

	D3DDraw_SetState (d3d_DrawPixelShader, texpic, d3d_SampleClampLinear);
	D3DDraw_SubmitQuad (x, y, w, h, d3d_DrawState.Color2D, &rect);
}


void Draw_Crosshair (int x, int y)
{
	// deferred loading because the crosshair texture is not yet up in Draw_Init
	if (!d3d_Crosshairs)
	{
		D3DDraw_DrawBatch ();
		Draw_LoadCrosshairs ();
	}

	// failed
	if (!d3d_Crosshairs) return;

	// we don't know about these cvars
	extern cvar_t crosshair;
	extern cvar_t scr_crosshaircolor;
	extern cvar_t scr_crosshairscale;

	// no crosshair
	if (!crosshair.integer) return;

	// get scale
	float crossscale = (32.0f * scr_crosshairscale.value);

	// - 1 because crosshair 0 is no crosshair
	int currcrosshair = crosshair.integer - 1;

	// wrap it
	if (currcrosshair >= d3d_NumCrosshairs) currcrosshair -= d3d_NumCrosshairs;

	// bound colour
	if (scr_crosshaircolor.integer < 0) scr_crosshaircolor.Set (0.0f);
	if (scr_crosshaircolor.integer > 13) scr_crosshaircolor.Set (13.0f);

	// handle backwards ranges
	int cindex = scr_crosshaircolor.integer * 16 + (scr_crosshaircolor.integer < 8 ? 15 : 0);

	// bound
	if (cindex < 0) cindex = 0;
	if (cindex > 255) cindex = 255;

	// classic crosshair
	if (currcrosshair < 2 && !d3d_Crosshairs[currcrosshair].replaced)
	{
		Draw_Character (x - 4, y - 4, '+' + 128 * currcrosshair);
		return;
	}

	// don't draw it if too small
	if (crossscale < 2) return;

	// center it properly
	x -= (crossscale / 2);
	y -= (crossscale / 2);

	D3DDraw_SetState (d3d_DrawPixelShader, &d3d_Crosshairs[currcrosshair].texture, d3d_SampleClampLinear);
	D3DDraw_SubmitQuad (x, y, crossscale, crossscale, d3d_QuakePalette.standard11[cindex], &d3d_Crosshairs[currcrosshair].rect);
}


void Draw_TextBox (int x, int y, int width, int height)
{
	// corners
	Draw_Pic (x, y, box_tl, 1, true);
	Draw_Pic (x + width + 8, y, box_tr, 1, true);
	Draw_Pic (x, y + height + 8, box_bl, 1, true);
	Draw_Pic (x + width + 8, y + height + 8, box_br, 1, true);

	// left and right sides
	for (int i = 8; i < height; i += 8) Draw_Pic (x, y + i, box_ml, 1, true);
	Draw_Pic (x, y + height, box_ml, 1, true);
	for (int i = 8; i < height; i += 8) Draw_Pic (x + width + 8, y + i, box_mr, 1, true);
	Draw_Pic (x + width + 8, y + height, box_mr, 1, true);

	// top and bottom sides
	for (int i = 16; i < width; i += 8) Draw_Pic (x + i - 8, y, box_tm, 1, true);
	Draw_Pic (x + width - 8, y, box_tm, 1, true);
	for (int i = 16; i < width; i += 8) Draw_Pic (x + i - 8, y + height + 8, box_bm, 1, true);
	Draw_Pic (x + width - 8, y + height + 8, box_bm, 1, true);

	glpic_t *gl = (glpic_t *) box_mm2->data;

	texrect_t texrect =
	{
		0,
		(float) width / (float) box_mm2->width,
		0,
		(float) height / (float) box_mm2->height
	};

	// fill it
	D3DDraw_SetState (d3d_DrawPixelShader, gl->Texture, d3d_SampleWrapLinear);
	D3DDraw_SubmitQuad (x + 8, y + 8, width, height, 0xffffffff, &texrect);
}


char cached_name[256] = {0};

void Draw_InvalidateMapshot (void)
{
	// just invalidate the cached name to force a reload in case the shot changes
	cached_name[0] = 0;
}


void Draw_MapshotTexture (QTEXTURE *mstex, int x, int y)
{
	Draw_TextBox (x - 8, y - 8, 128, 128);
	Draw_Pic (x, y, 128, 128, mstex);
}


void Draw_Mapshot (char *path, char *name, int x, int y)
{
	// flush drawing because we're updating textures here
	D3DDraw_DrawBatch ();

	if (!path || !name)
	{
		// no name supplied so display the console image instead
		glpic_t *gl = (glpic_t *) conback->data;

		// the conback is unpadded so use regular texcoords
		Draw_TextBox (x - 8, y - 8, 128, 128);
		Draw_Pic (x, y, 128, 128, gl->Texture);
		return;
	}

	if (_stricmp (name, cached_name))
	{
		// save to cached name
		Q_strncpy (cached_name, name, 255);

		// texture has changed, release the existing one
		mapshottexture.Release ();

		char *paths[] = {path, NULL};

		for (int j = strlen (cached_name) - 1; j; j--)
		{
			if (cached_name[j] == '.')
			{
				cached_name[j] = 0;
				break;
			}
		}

		// attempt to load it
		if (!mapshottexture.LoadExternal (cached_name, paths, 0))
		{
			// if we didn't load it, call recursively to display the console
			Draw_Mapshot (NULL, NULL, x, y);

			// done
			return;
		}
	}

	// ensure valid
	if (!mapshottexture.HasTexture ())
	{
		// if we didn't load it, call recursively to display the console
		Draw_Mapshot (NULL, NULL, x, y);

		// return
		return;
	}

	// draw it
	Draw_MapshotTexture (&mapshottexture, x, y);
}


/*
=============
Draw_PicTranslate

Only used for the player color selection menu
=============
*/
void Draw_PicTranslate (int x, int y, qpic_t *pic, byte *translation, int shirt, int pants)
{
	// flush drawing because we're updating textures here
	D3DDraw_DrawBatch ();

	// force an update on first entry
	static int old_shirt = -1;
	static int old_pants = -1;

	if (shirt == old_shirt && pants == old_pants)
	{
		// prevent updating if it hasn't changed
		Draw_Pic (x, y, pic, 1, true);
		return;
	}

	// recache the change
	old_shirt = shirt;
	old_pants = pants;

	// update for translation
	byte *src = menuplyr_pixels;
	byte *dst = menuplyr_pixels_translated;

	// copy out the new pixels
	for (int v = 0; v < pic->height; v++, dst += pic->width, src += pic->width)
		for (int u = 0; u < pic->width; u++)
			dst[u] = translation[src[u]];

	// replace the texture fully
	glpic_t *gl = (glpic_t *) pic->data;

	gl->Texture->Release ();
	gl->Texture->Upload (menuplyr_pixels_translated, pic->width, pic->height, IMAGE_ALPHA, d3d_QuakePalette.standard11);

	// and draw it normally
	Draw_Pic (x, y, pic, 1, true);
}


/*
================
Draw_ConsoleBackground

================
*/
void Draw_ConsoleBackground (float percent)
{
	float alpha = percent / 75.0f;
	glpic_t *gl = (glpic_t *) conback->data;
	unsigned int consolecolor = (BYTE_CLAMPF (alpha) << 24) | (255 << 0) | (255 << 8) | (255 << 16);

	// the conback image is unpadded so use regular texcoords
	texrect_t rect = {0, 1, 1.0f - ((float) percent / 100.0f), 1};

	D3DDraw_SetState (d3d_DrawPixelShader, gl->Texture, d3d_SampleClampLinear);
	D3DDraw_SubmitQuad (0, 0, vid.currsize.width, ((float) vid.currsize.height * percent) / 100.0f, consolecolor, &rect);
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.  Only drawn when an update is needed and the full screen is
covered to make certain that we get everything.
=============
*/
void Draw_TileClear (float x, float y, float w, float h)
{
	glpic_t *gl = (glpic_t *) draw_backtile->data;
	texrect_t rect = {x / 64.0f, (x + w) / 64.0f, y / 64.0f, (y + h) / 64.0f};

	D3DDraw_SetState (d3d_DrawPixelShader, gl->Texture, d3d_SampleWrapLinear);
	D3DDraw_SubmitQuad (x, y, w, h, 0xffffffff, &rect);
}


void Draw_TileClear (void)
{
	extern cvar_t cl_sbar;
	extern cvar_t scr_centersbar;

	if (cls.state != ca_connected) return;
	if (cl_sbar.integer) return;

	// do it proper for left-aligned HUDs
	if (scr_centersbar.integer)
	{
		int width = (vid.currsize.width - 320) >> 1;
		int offset = width + 320;

		Draw_TileClear (0, vid.currsize.height - sb_lines, width, sb_lines);
		Draw_TileClear (offset, vid.currsize.height - sb_lines, width, sb_lines);
	}
	else Draw_TileClear (320, vid.currsize.height - sb_lines, vid.currsize.width - 320, sb_lines);
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c, int alpha)
{
	unsigned int fillcolor = d3d_QuakePalette.standard11[c & 255];

	((byte *) &fillcolor)[3] = BYTE_CLAMP (alpha);

	D3DDraw_SetState (d3d_ColorPixelShader);
	D3DDraw_SubmitQuad (x, y, w, h, fillcolor);
}


void Draw_Fill (int x, int y, int w, int h, float r, float g, float b, float alpha)
{
	unsigned int fillcolor = (BYTE_CLAMPF (alpha) << 24) | (BYTE_CLAMPF (r) << 0) | (BYTE_CLAMPF (g) << 8) | (BYTE_CLAMPF (b) << 16);

	D3DDraw_SetState (d3d_ColorPixelShader);
	D3DDraw_SubmitQuad (x, y, w, h, fillcolor);
}


//=============================================================================

cvar_t r_menucolor ("r_menucolor", "1");
extern QTEXTURE d3d_PaletteRowTextures[];

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (int alpha)
{
	D3DDraw_SetState (d3d_ColorPixelShader);
	D3DDraw_SubmitQuad (0, 0, vid.currsize.width, vid.currsize.height, (alpha & 255) << 24);
}


void Draw_FadeScreen (void)
{
	D3DDraw_DrawBatch ();

	if (r_menucolor.integer < 16)
	{
		int row = r_menucolor.integer & 15;

		d3d11_State->PSSetTexture (1, &d3d_PaletteRowTextures[row]);

		D3DDraw_SetRect (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height);
		D3DDraw_SetState (d3d_FadePixelShader, D3DRTT_CopyScreen (), d3d_SampleClampLinear);
		D3DDraw_SubmitQuad (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0xffffffff, &defaulttexrect);

		D3DDraw_DrawBatch ();

		return;
	}

	Draw_FadeScreen (200);
}


cvar_t r_cbvision ("r_cbvision", 0.0f);

static const float cb_normal[] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
static const float cb_protanopia[] = {0.567f, 0.433f, 0.0f, 0.0f, 0.558f, 0.442f, 0.0f, 0.0f, 0.0f, 0.242f, 0.758f, 0.0f};
static const float cb_protanomaly[] = {0.817f, 0.183f, 0.0f, 0.0f, 0.333f, 0.667f, 0.0f, 0.0f, 0.0f, 0.125f, 0.875f, 0.0f};
static const float cb_deuteranopia[] = {0.625f, 0.375f, 0.0f, 0.0f, 0.7f, 0.3f, 0.0f, 0.0f, 0.0f, 0.3f, 0.7f, 0.0f};
static const float cb_deuteranomaly[] = {0.8f, 0.2f, 0.0f, 0.0f, 0.258f, 0.742f, 0.0f, 0.0f, 0.0f, 0.142f, 0.858f, 0.0f};
static const float cb_tritanopia[] = {0.95f, 0.05f, 0.0f, 0.0f, 0.0f, 0.433f, 0.567f, 0.0f, 0.0f, 0.475f, 0.525f, 0.0f};
static const float cb_tritanomaly[] = {0.967f, 0.033f, 0.0f, 0.0f, 0.0f, 0.733f, 0.267f, 0.0f, 0.0f, 0.183f, 0.817f, 0.0f};
static const float cb_achromatopsia[] = {0.299f, 0.587f, 0.114f, 0.0f, 0.299f, 0.587f, 0.114f, 0.0f, 0.299f, 0.587f, 0.114f, 0.0f};
static const float cb_achromatomaly[] = {0.618f, 0.32f, 0.062f, 0.0f, 0.163f, 0.775f, 0.062f, 0.0f, 0.163f, 0.32f, 0.516f, 0.0f};

#define Draw_UpdateCBBuffer(data) d3d11_Context->UpdateSubresource (d3d_CBConstants, 0, NULL, (data), 0, 0);

void Draw_CBVision (void)
{
	D3DDraw_DrawBatch ();

	switch (r_cbvision.integer)
	{
	case 1: Draw_UpdateCBBuffer (cb_protanopia); break;
	case 2: Draw_UpdateCBBuffer (cb_protanomaly); break;
	case 3: Draw_UpdateCBBuffer (cb_deuteranopia); break;
	case 4: Draw_UpdateCBBuffer (cb_deuteranomaly); break;
	case 5: Draw_UpdateCBBuffer (cb_tritanopia); break;
	case 6: Draw_UpdateCBBuffer (cb_tritanomaly); break;
	case 7: Draw_UpdateCBBuffer (cb_achromatopsia); break;
	case 8: Draw_UpdateCBBuffer (cb_achromatomaly); break;
	default: Draw_UpdateCBBuffer (cb_normal); break;
	}

	d3d11_State->PSSetConstantBuffer (2, d3d_CBConstants);

	D3DDraw_SetRect (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height);
	D3DDraw_SetState (d3d_CBPixelShader, D3DRTT_CopyScreen (), d3d_SampleClampLinear);
	D3DDraw_SubmitQuad (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0xffffffff, &defaulttexrect);

	D3DDraw_DrawBatch ();
}


//=============================================================================

void D3D_Set2DShade (float shadecolor)
{
	if (shadecolor >= 0.99f)
	{
		// solid
		d3d_DrawState.Color2D = 0xffffffff;
	}
	else
	{
		// 0 to 255 scale
		byte shade = (shadecolor * 255.0f);

		// fade out
		d3d_DrawState.Color2D = (shade << 24) | (shade << 16) | (shade << 8) | shade;
	}
}


void D3DDraw_PolyBlend (void)
{
	if (cls.state != ca_connected) return;

	if (gl_polyblend.value > 0.0f && vid.cshift[3] > 0)
	{
		D3DDraw_SetRect (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height);

		byte polyblendcolor[4] =
		{
			BYTE_CLAMP (vid.cshift[0]),
			BYTE_CLAMP (vid.cshift[1]),
			BYTE_CLAMP (vid.cshift[2]),
			BYTE_CLAMP (vid.cshift[3] * gl_polyblend.value)
		};

		D3DDraw_SetState (d3d_ColorPixelShader);
		D3DDraw_SubmitQuad (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height - vid.sbar_lines, ((unsigned *) polyblendcolor)[0]);

		vid.cshift[3] = -666;
	}
}


void D3DDraw_Begin2D (void)
{
	// the array of viewports method behaved oddly with mode changes so i reverted it to on-demand changes
	QVIEWPORT vp (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 0);

	// ensure that we have the correct rendertarget set as it may not have been done if we didn't do a 3D draw this frame
	if (vid.brightpass && !vid.nobrightpass)
		d3d11_State->OMSetRenderTargets (d3d_ScreenRTV);
	else d3d11_State->OMSetRenderTargets (d3d11_RenderTargetView);

	d3d11_State->RSSetViewport (&vp);
	d3d11_State->OMSetDepthStencilState (d3d_DisableDepthTest);
	d3d11_State->RSSetState (d3d_RS2DView);

	// set up all the crap we'll use
	d3d11_State->IASetInputLayout (d3d_DrawLayout);
	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	d3d11_State->VSSetConstantBuffer (1, d3d_DrawConstants);
	d3d11_State->PSSetConstantBuffer (1, d3d_DrawConstants);
	d3d11_State->SetOnChangeCallback (D3DDraw_DrawBatch);

	// force a recache
	vid.currsize.width = -1;
	vid.currsize.height = -1;

	// clear the cached states
	d3d_DrawState.Color2D = 0xffffffff;
	d3d_DrawState.NumQuads = 0;
	d3d_DrawState.DrawQuads = NULL;

	// and now bring up the rest of our state
	d3d11_State->VSSetShader (d3d_DrawVertexShader);
	d3d11_State->OMSetBlendState (d3d_AlphaBlendEnable);

	// draw the polyblend first
	D3DDraw_PolyBlend ();
}


void D3DDraw_End2D (void)
{
	if (r_cbvision.integer)
		Draw_CBVision ();

	D3DDraw_DrawBatch ();
	d3d11_State->SetOnChangeCallback (NULL);
	d3d_DrawState.Color2D = 0xffffffff;
}


void D3DDraw_SetRect (int x, int y, int w, int h)
{
	if (vid.currofsx != x || vid.currofsy != y || vid.currsize.width != w || vid.currsize.height != h)
	{
		// flush any pending drawing because this is a state change
		D3DDraw_DrawBatch ();

		d3d_drawcbuffer_t d3d_DrawUpdate;

		d3d_DrawUpdate.drawMatrix.Identity ();
		d3d_DrawUpdate.drawMatrix.OrthoOffCenterRH (0, w, h, 0, -1, 1);
		d3d_DrawUpdate.drawMatrix.Translate (x, y, 0);

		d3d11_Context->UpdateSubresource (d3d_DrawConstants, 0, NULL, &d3d_DrawUpdate, 0, 0);

		// and store it out
		vid.currofsx = x;
		vid.currofsy = y;
		vid.currsize.width = w;
		vid.currsize.height = h;
	}
}


void Draw_BrightPass (void)
{
	// conditionally suppress brightpass for mapshots, screenshots, etc
	if (vid.nobrightpass)
	{
		vid.nobrightpass = false;
		return;
	}

	// not running the bright pass
	if (!vid.brightpass) return;

	// set up state again in case it was changed
	brightpass_t data;
	extern cvar_t v_gamma, r_gamma, g_gamma, b_gamma;
	extern cvar_t v_contrast, r_contrast, g_contrast, b_contrast;

	data.rgb_gamma[0] = r_gamma.value * v_gamma.value;
	data.rgb_gamma[1] = g_gamma.value * v_gamma.value;
	data.rgb_gamma[2] = b_gamma.value * v_gamma.value;

	data.rgb_contrast[0] = r_contrast.value * v_contrast.value;
	data.rgb_contrast[1] = g_contrast.value * v_contrast.value;
	data.rgb_contrast[2] = b_contrast.value * v_contrast.value;

	data.mipLevel = d3d_ScreenNumMips - 1;

	d3d11_Context->UpdateSubresource (d3d_BrightPassConstants, 0, NULL, &data, 0, 0);

	QVIEWPORT vp (0, 0, d3d_CurrentMode.Width, d3d_CurrentMode.Height, 0, 0);

	// go back to the main rendertarget
	d3d11_State->OMSetRenderTargets (d3d11_RenderTargetView);
	d3d11_State->SynchronizeState ();

	// get mip levels for the screen texture so that we can extract an average colour
	d3d11_Context->GenerateMips (d3d_ScreenSRV);

	d3d11_State->RSSetViewport (&vp);

	d3d11_State->OMSetBlendState (NULL);
	d3d11_State->OMSetDepthStencilState (d3d_DisableDepthTest);
	d3d11_State->RSSetState (d3d_RS2DView);

	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	d3d11_State->IASetInputLayout (NULL);

	d3d11_State->VSSetShader (d3d_BrightPassVertexShader);
	d3d11_State->PSSetShader (d3d_BrightPassPixelShader);

	d3d11_State->VSSetSampler (0, d3d_SampleClampPoint);
	d3d11_State->PSSetSampler (0, d3d_SampleClampPoint);

	d3d11_State->VSSetShaderResourceView (0, d3d_ScreenSRV);
	d3d11_State->PSSetShaderResourceView (0, d3d_ScreenSRV);

	d3d11_State->VSSetConstantBuffer (1, d3d_BrightPassConstants);
	d3d11_State->PSSetConstantBuffer (1, d3d_BrightPassConstants);

	D3DMisc_DrawCommon (4);
}

