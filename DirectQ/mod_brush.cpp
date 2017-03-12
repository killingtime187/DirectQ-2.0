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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"


void D3DSky_InitTextures (miptex_t *mt, char **paths);
float RadiusFromBounds (vec3_t mins, vec3_t maxs);
model_t *Mod_FindName (char *name);

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/


/*
=================
ModBrush_LoadEdges

Yayy C++
=================
*/
template <typename edgetype_t>
void ModBrush_LoadEdges (model_t *mod, byte *mod_base, lump_t *l)
{
	edgetype_t *in = (edgetype_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (edgetype_t))
		Host_Error ("ModBrush_LoadBrushModel: LUMP_EDGES funny lump size in %s", mod->name);

	int count = l->filelen / sizeof (edgetype_t);
	medge_t *out = (medge_t *) MainHunk->Alloc ((count + 1) * sizeof (medge_t));

	mod->brushhdr->edges = out;
	mod->brushhdr->numedges = count;

	for (int i = 0; i < count; i++, in++, out++)
	{
		out->v[0] = in->v[0];
		out->v[1] = in->v[1];
	}
}


/*
=================
ModBrush_LoadTextures
=================
*/
char *Mod_ParseWadName (char *data)
{
	char key[1025];
	static char *value = (char *) scratchbuf;

	if (!(data = COM_Parse (data))) return NULL;
	if (com_token[0] != '{') return NULL;

	for (;;)
	{
		if (!(data = COM_Parse (data))) return NULL;
		if (com_token[0] == '}') return NULL;

		strcpy (key, com_token);
		_strlwr (key);

		if (!(data = COM_Parse (data))) return NULL; // error

		if (!strcmp (key, "wad"))
		{
			strcpy (value, com_token);
			return value;
		}
	}
}


// http://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
// but we don't care about the memory issues mentioned there as we're allocating these on the hunk
// note however that this means that this may not be safe to call from elsewhere!!!
char *trimwhitespace (char *str)
{
	char *end;

	// Trim leading space
	while (isspace ((unsigned char) *str)) str++;

	if (*str == 0)  // All spaces?
		return str;

	// Trim trailing space
	end = str + strlen (str) - 1;
	while (end > str && isspace ((unsigned char) *end)) end--;

	// Write new null terminator
	*(end + 1) = 0;

	return str;
}


bool Mod_LinkTextureAnimations (texture_t **anims, int max, texture_t **altanims, int altmax)
{
	const int ANIM_CYCLE = 2;

	for (int j = 0; j < max; j++)
	{
		texture_t *tx2 = anims[j];

		if (!tx2)
		{
			Con_DPrintf ("Missing frame %i - ", j);
			return false;
		}

		tx2->anim_total = max * ANIM_CYCLE;
		tx2->anim_min = j * ANIM_CYCLE;
		tx2->anim_max = (j + 1) * ANIM_CYCLE;
		tx2->anim_next = anims[(j + 1) % max];

		if (altmax) tx2->animframes[1] = altanims[0];
	}

	return true;
}


void V_ScaleCShift (int *shift, int flags);


void ModBrush_SetTextureContents (texture_t *tx, char *name)
{
	if (name[0] == '*')
	{
		byte rgba[4];

		tx->teximage->RGBA32FromLowestMip (rgba);

		// to do - NTSC scale this from the original cshift scale...
		tx->contentscolor[0] = rgba[0];
		tx->contentscolor[1] = rgba[1];
		tx->contentscolor[2] = rgba[2];

		// and done
		if (!_strnicmp (&name[1], "lava", 4))
			V_ScaleCShift (tx->contentscolor, SURF_DRAWLAVA);
		else if (!_strnicmp (&name[1], "tele", 4))
			V_ScaleCShift (tx->contentscolor, SURF_DRAWTELE);
		else if (!_strnicmp (&name[1], "slime", 5))
			V_ScaleCShift (tx->contentscolor, SURF_DRAWSLIME);
		else V_ScaleCShift (tx->contentscolor, SURF_DRAWWATER);
	}
	else
	{
		tx->contentscolor[0] = 255;
		tx->contentscolor[1] = 255;
		tx->contentscolor[2] = 255;
	}
}


void ModBrush_GetTextureLoadPaths (model_t *mod, byte *mod_base, lump_t *l, lump_t *e, char **paths)
{
	char *texpath = (char *) TempHunk->Alloc (256);
	char *texpathex = (char *) TempHunk->Alloc (256);
	char *modelpath = (char *) TempHunk->Alloc (256);

	strcpy (texpath, "textures/");
	strcpy (texpathex, "textures/exmy/");
	sprintf (modelpath, "textures/%s", &mod->name[5]);

	for (int i = strlen (modelpath) - 1; i; i--)
	{
		if (modelpath[i] == '.')
		{
			modelpath[i] = '/';
			modelpath[i + 1] = 0;
			break;
		}
	}

	// extract the wad names from the entities lump so that they can be added to the search paths
	int Wad = 0;
	char *wadnames = Mod_ParseWadName ((char *) (mod_base + e->fileofs));

	// some maps don't have a "wad" key
	if (wadnames)
	{
		// now parse individual wads out of the names (need to steal some QBSP code for this...)
		int NoOfWads = 1;

		for (int i = 0; ; i++)
		{
			if (!wadnames[i]) break;
			if (wadnames[i] == ';') NoOfWads++;
		}

		// save space for the default paths (this is a little loose but we'll never have this many WADS anyway
		if (NoOfWads > 250) NoOfWads -= 3;

		char *Ptr = wadnames;

		for (int i = 0; i < NoOfWads; i++)
		{
			char *Ptr2 = Ptr;

			// Extract current WAD file name
			while (*Ptr2 != ';' && *Ptr2 != '\0') Ptr2++;

			paths[Wad++] = Ptr;

			if (*Ptr2 != '\0') Ptr = Ptr2 + 1;
		}

		for (int i = 0, j = 0; i < Wad; i++)
		{
			// find first delimiter or end of string
			for (j = 0; ; j++)
			{
				if (paths[i][j] == ';') paths[i][j] = 0;
				if (paths[i][j] == 0) break;
			}

			// count back to remove extension and prepended paths
			for (; j; j--)
			{
				if (paths[i][j] == '.') paths[i][j] = 0;

				if (paths[i][j] == '/' || paths[i][j] == '\\')
				{
					// we have the name of the wad now
					paths[i] = &paths[i][j + 1];
					break;
				}
			}

			// trim leading and trailing spaces
			trimwhitespace (paths[i]);

			// set up for real
			// the final path needs to go on the hunk as scratchbuf may be unsafe to use
			// note that no world content actually goes on the hunk anymore so this can be just thrown out when done
			Ptr = paths[i];
			paths[i] = (char *) TempHunk->Alloc (strlen (Ptr) + 12);	// one or two extra bytes!  oh noes!  wasting memory!
			sprintf (paths[i], "textures/%s/", Ptr);
		}
	}

	// append the rest of the paths
	paths[Wad++] = modelpath;
	paths[Wad++] = texpathex;
	paths[Wad++] = texpath;
	paths[Wad++] = NULL;

	// check if these directories exist and if they don't then don't bother searching for externals in them
	// because we support 7 different image types a one-time search of all paths upfront should be faster than 7 * numpaths * numtextures
	COM_ValidatePaths (paths);
}


void ModBrush_LoadSkyTexture (texture_t *tx, miptex_t *mt, char **paths)
{
	D3DSky_InitTextures (mt, paths);

	// make this explicit
	tx->teximage = NULL;
	tx->lumaimage = NULL;
}


void ModBrush_LoadLiquidTexture (texture_t *tx, miptex_t *mt, char **paths)
{
	tx->teximage = QTEXTURE::Load (mt->name, mt->width, mt->height, mt->texels, IMAGE_MIPMAP | IMAGE_BSP | IMAGE_LIQUID, paths);
	tx->lumaimage = NULL;
}


void ModBrush_LoadSolidTexture (texture_t *tx, miptex_t *mt, char **paths)
{
	int texflags = IMAGE_MIPMAP | IMAGE_BSP | ((mt->name[0] == '{') ? IMAGE_FENCE : 0);
	int lumaflags = texflags | IMAGE_LUMA;

	tx->teximage = QTEXTURE::Load (mt->name, mt->width, mt->height, mt->texels, texflags, paths);
	tx->lumaimage = QTEXTURE::Load (mt->name, mt->width, mt->height, mt->texels, lumaflags, paths);
}


void ModBrush_SequenceTextureAnimations (brushhdr_t *hdr)
{
	// sequence the animations
	texture_t *anims[2][10];
	int animmax[2];

	// this was evil - it crashed to the console if there was an error.  Now it just removes the animation and devprints a message
	for (int i = 0; i < hdr->numtextures; i++)
	{
		texture_t *tx = hdr->textures[i];

		if (!tx || tx->name[0] != '+') continue;
		if (tx->anim_next) continue;

		// find the number of frames in the animation
		memset (anims, 0, sizeof (anims));

		animmax[0] = tx->name[1];
		animmax[1] = 0;

		if (animmax[0] >= 'a' && animmax[0] <= 'z') animmax[0] -= 'a' - 'A';

		if (animmax[0] >= '0' && animmax[0] <= '9')
		{
			animmax[0] -= '0';
			animmax[1] = 0;
			anims[0][animmax[0]] = tx;
			animmax[0]++;
		}
		else if (animmax[0] >= 'A' && animmax[0] <= 'J')
		{
			animmax[1] = animmax[0] - 'A';
			animmax[0] = 0;
			anims[1][animmax[1]] = tx;
			animmax[1]++;
		}
		else
		{
			Con_DPrintf ("Invalid animation name - ");
			goto bad_anim_cleanup;
		}

		for (int j = i + 1; j < hdr->numtextures; j++)
		{
			texture_t *tx2 = hdr->textures[j];

			if (!tx2 || tx2->name[0] != '+') continue;
			if (strcmp (tx2->name + 2, tx->name + 2)) continue;

			int num = tx2->name[1];

			if (num >= 'a' && num <= 'z') num -= 'a' - 'A';

			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[0][num] = tx2;

				if (num + 1 > animmax[0]) animmax[0] = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				anims[1][num] = tx2;

				if (num + 1 > animmax[1]) animmax[1] = num + 1;
			}
			else
			{
				Con_DPrintf ("Invalid animation name - ");
				goto bad_anim_cleanup;
			}
		}

		if (!Mod_LinkTextureAnimations (anims[0], animmax[0], anims[1], animmax[1])) goto bad_anim_cleanup;
		if (!Mod_LinkTextureAnimations (anims[1], animmax[1], anims[0], animmax[0])) goto bad_anim_cleanup;

		// if we got this far the animating texture is good
		continue;

bad_anim_cleanup:;
		// the animation is unclean so clean it
		Con_DPrintf ("Bad animating texture %s", tx->name);

		// switch to non-animating - remove all animation data
		tx->name[0] = '$';	// this just needs to replace the "+" or "-" on the name with something, anything
		tx->animframes[1] = NULL;
		tx->anim_max = 0;
		tx->anim_min = 0;
		tx->anim_next = NULL;
		tx->anim_total = 0;
	}

	// always provide an alternate anim so that we can skip a branch at runtime
	for (int i = 0; i < hdr->numtextures; i++)
	{
		texture_t *tx = hdr->textures[i];

		if (!tx) continue;

		// fill in missing animations
		if (!tx->animframes[0]) tx->animframes[0] = tx;
		if (!tx->animframes[1]) tx->animframes[1] = tx;
	}
}


void ModBrush_LoadTextures (model_t *mod, byte *mod_base, lump_t *l, lump_t *e)
{
	double beginloadtime = Sys_DoubleTime ();
	int hunkmark = TempHunk->GetLowMark ();
	char **paths = (char **) TempHunk->Alloc (256 * sizeof (char *));

	if (!l->filelen)
	{
		mod->brushhdr->textures = NULL;
		return;
	}

	dmiptexlump_t *m = (dmiptexlump_t *) (mod_base + l->fileofs);

	mod->brushhdr->numtextures = m->nummiptex;
	mod->brushhdr->textures = (texture_t **) MainHunk->Alloc (m->nummiptex * sizeof (texture_t *));

	ModBrush_GetTextureLoadPaths (mod, mod_base, l, e, paths);

	for (int i = 0; i < m->nummiptex; i++)
	{
		if (m->dataofs[i] == -1)
		{
			// set correct notexture here
			mod->brushhdr->textures[i] = r_notexture_mip;
			continue;
		}

		miptex_t *mt = (miptex_t *) ((byte *) m + m->dataofs[i]);
		texture_t *tx = (texture_t *) MainHunk->Alloc (sizeof (texture_t));

		// store out
		mod->brushhdr->textures[i] = tx;

		// fix 16 char texture names
		tx->name[16] = 0;
		Q_MemCpy (tx->name, mt->name, sizeof (mt->name));

		tx->size[0] = mt->width;
		tx->size[1] = mt->height;

		// default frame 0 animation sequence is the texture itself
		tx->animframes[0] = tx;

		if (!_strnicmp (mt->name, "sky", 3))
			ModBrush_LoadSkyTexture (tx, mt, paths);
		else if (mt->name[0] == '*')
			ModBrush_LoadLiquidTexture (tx, mt, paths);
		else ModBrush_LoadSolidTexture (tx, mt, paths);

		ModBrush_SetTextureContents (tx, mt->name);
	}

	ModBrush_SequenceTextureAnimations (mod->brushhdr);
	TempHunk->FreeToLowMark (hunkmark);

	Con_DPrintf ("Texture load for %s in %f seconds\n", mod->name, Sys_DoubleTime () - beginloadtime);
}


/*
=================
ModBrush_LoadLighting
=================
*/
bool ModBrush_LoadLITFile (model_t *mod, lump_t *l)
{
	char litname[1024];
	CQuakeFile litfile;
	int litheader[2];

	// take a copy to work on
	Q_strncpy (litname, mod->name, 127);

	// fixme - we use this in a number of places so we should refactor it out
	for (int i = strlen (litname) - 1; i; i--)
	{
		if (litname[i] == '.')
		{
			strcpy (&litname[i + 1], "lit");
			break;
		}
	}

	if (litfile.Open (litname))
	{
		if (!litfile.ValidateLength ((l->filelen * 3) + 8))
		{
			litfile.Close ();
			return false;
		}

		// read and validate the header
		litfile.Read (litheader, sizeof (int) * 2);

		if (litheader[0] != 0x54494C51 || litheader[1] != 1)
		{
			// invalid format
			litfile.Close ();
			return false;
		}

		// the LIT file is good now
		litfile.Read (mod->brushhdr->lightdata, l->filelen * 3);
		litfile.Close ();

		return true;
	}
	else return false;
}


void ModBrush_LoadLighting (model_t *mod, byte *mod_base, lump_t *l)
{
	if (!l->filelen)
	{
		mod->brushhdr->lightdata = NULL;
		return;
	}

	// expand size to 3 component
	mod->brushhdr->lightdata = (byte *) MainHunk->Alloc (l->filelen * 3);

	// check for a lit file
	if (ModBrush_LoadLITFile (mod, l)) return;

	// set source and dest pointers
	byte *src = mod_base + l->fileofs;
	byte *dst = mod->brushhdr->lightdata;

	// fill in the rest
	for (int i = 0; i < l->filelen; i++, dst += 3)
	{
		dst[0] = src[i];
		dst[1] = src[i];
		dst[2] = src[i];
	}
}


/*
=================
ModBrush_LoadEntities
=================
*/
void ModBrush_LoadEntities (model_t *mod, byte *mod_base, lump_t *l)
{
	if (!l->filelen)
	{
		mod->brushhdr->entities = NULL;
		return;
	}

	// resolve missing NULL term in entities lump; VPA will automatically 0 the memory so no need to do so ourselves.
	mod->brushhdr->entities = (char *) MainHunk->Alloc (l->filelen + 1);
	Q_MemCpy (mod->brushhdr->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
ModBrush_LoadSubmodels
=================
*/
void ModBrush_LoadSubmodels (model_t *mod, byte *mod_base, lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = (dmodel_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (dmodel_t))
		Host_Error ("ModBrush_LoadBrushModel: LUMP_SUBMODELS funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dmodel_t);
	out = (dmodel_t *) MainHunk->Alloc (count * sizeof (*out));

	mod->brushhdr->submodels = out;
	mod->brushhdr->numsubmodels = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		for (j = 0; j < 3; j++)
		{
			// spread the mins / maxs by a pixel
			out->mins[j] = in->mins[j] - 1;
			out->maxs[j] = in->maxs[j] + 1;
			out->origin[j] = in->origin[j];
		}

		for (j = 0; j < MAX_MAP_HULLS; j++)
			out->headnode[j] = in->headnode[j];

		out->visleafs = in->visleafs;
		out->firstface = in->firstface;
		out->numfaces = in->numfaces;
	}
}


/*
=================
ModBrush_LoadTexinfo
=================
*/
void ModBrush_LoadTexinfo (model_t *mod, byte *mod_base, lump_t *l)
{
	texinfo_t *in = (texinfo_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (texinfo_t))
		Host_Error ("ModBrush_LoadBrushModel: LUMP_TEXINFO funny lump size in %s", mod->name);

	int count = l->filelen / sizeof (texinfo_t);
	mtexinfo_t *out = (mtexinfo_t *) MainHunk->Alloc (count * sizeof (mtexinfo_t));

	mod->brushhdr->texinfo = out;
	mod->brushhdr->numtexinfo = count;

	for (int i = 0; i < count; i++, in++, out++)
	{
		for (int k = 0; k < 2; k++)
			for (int j = 0; j < 4; j++)
				out->vecs[k][j] = in->vecs[k][j];

		int miptex = in->miptex;
		out->flags = in->flags;

		if (!mod->brushhdr->textures)
		{
			out->texture = r_notexture_mip;	// checkerboard texture
			out->flags = 0;
		}
		else if (miptex >= mod->brushhdr->numtextures || miptex < 0)
		{
			Con_DPrintf ("miptex >= mod->brushhdr->numtextures\n");
			out->texture = r_notexture_mip; // texture not found
			out->flags = 0;
		}
		else
		{
			out->texture = mod->brushhdr->textures[miptex];

			if (!out->texture)
			{
				Con_DPrintf ("!out->texture\n");
				out->texture = r_notexture_mip; // texture not found
				out->flags = 0;
			}
		}
	}
}


/*
=================
Mod_CalcSurfaceBounds

=================
*/
void Mod_CalcSurfaceBounds (model_t *mod, msurface_t *surf)
{
	// let's calc bounds and extents here too...!
	float mins[2] = {99999999.0f, 99999999.0f};
	float maxs[2] = {-99999999.0f, -99999999.0f};

	Mod_ClearBoundingBox (surf->cullinfo.mins, surf->cullinfo.maxs);

	for (int i = 0; i < surf->numvertexes; i++)
	{
		int lindex = mod->brushhdr->dsurfedges[surf->firstedge + i];
		float *vec;

		if (lindex > 0)
			vec = mod->brushhdr->dvertexes[mod->brushhdr->edges[lindex].v[0]].point;
		else vec = mod->brushhdr->dvertexes[mod->brushhdr->edges[-lindex].v[1]].point;

		// bounds
		Mod_AccumulateBox (surf->cullinfo.mins, surf->cullinfo.maxs, vec);

		// extents
		for (int j = 0; j < 2; j++)
		{
			float st = Vector3Dot (vec, surf->texinfo->vecs[j]) + surf->texinfo->vecs[j][3];

			if (st < mins[j]) mins[j] = st;
			if (st > maxs[j]) maxs[j] = st;
		}
	}

	// midpoint
	for (int i = 0; i < 3; i++)
	{
		// expand the bbox by 1 unit in each direction to ensure that marginal surfs don't get culled
		// (needed for D3DSurf_RecursiveWorldNode avoidance)
		surf->cullinfo.mins[i] -= 1.0f;
		surf->cullinfo.maxs[i] += 1.0f;

		// get final mindpoint
		surf->midpoint[i] = surf->cullinfo.mins[i] + (surf->cullinfo.maxs[i] - surf->cullinfo.mins[i]) * 0.5f;
	}

	// no extents
	if ((surf->flags & SURF_DRAWTURB) || (surf->flags & SURF_DRAWSKY)) return;

	// now do extents
	for (int i = 0; i < 2; i++)
	{
		int bmins = floor (mins[i] / 16);
		int bmaxs = ceil (maxs[i] / 16);

		surf->texturemins[i] = bmins * 16;
		surf->extents[i] = (bmaxs - bmins) * 16;
	}
}


/*
=================
ModBrush_LoadSurfaces

Yayy C++
=================
*/
void D3DSurf_AccumulateSurface (msurface_t *surf);

template <typename facetype_t>
void ModBrush_LoadSurfaces (model_t *mod, byte *mod_base, lump_t *l)
{
	facetype_t *face = (facetype_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (facetype_t))
	{
		Host_Error ("ModBrush_LoadSurfaces: LUMP_FACES funny lump size in %s", mod->name);
		return;
	}

	int count = l->filelen / sizeof (facetype_t);
	msurface_t *surf = (msurface_t *) MainHunk->Alloc (count * sizeof (msurface_t));

	mod->brushhdr->surfaces = surf;
	mod->brushhdr->numsurfaces = count;
	mod->brushhdr->numsurfvertexes = 0;

	int indexcount = 0;

	for (int surfnum = 0; surfnum < count; surfnum++, face++, surf++)
	{
		surf->model = mod;

		// verts/etc
		surf->firstedge = face->firstedge;

		if (mod->brushhdr->bspversion == BSPVERSIONRMQ)
		{
			surf->numvertexes = face->numedges;
			surf->plane = mod->brushhdr->planes + face->planenum;
			surf->texinfo = mod->brushhdr->texinfo + face->texinfo;
		}
		else
		{
			surf->numvertexes = (unsigned short) face->numedges;
			surf->plane = mod->brushhdr->planes + (unsigned short) face->planenum;
			surf->texinfo = mod->brushhdr->texinfo + (unsigned short) face->texinfo;
		}

		surf->numindexes = (surf->numvertexes - 2) * 3;
		mod->brushhdr->numsurfvertexes += surf->numvertexes;
		indexcount += surf->numindexes;
		surf->flags = 0;

		if (face->side) surf->flags |= SURF_PLANEBACK;

		// lighting info
		for (int i = 0; i < MAX_SURFACE_STYLES; i++)
			surf->styles[i] = face->styles[i];

		// expand offsets for pre-expanded light
		if (face->lightofs < 0)
			surf->samples = NULL;
		else surf->samples = mod->brushhdr->lightdata + (face->lightofs * 3);

		texture_t *tex = surf->texinfo->texture;

		// set the drawing flags flag
		if (!strncmp (tex->name, "sky", 3))
			surf->flags |= SURF_DRAWSKY;
		else if (tex->name[0] == '{')
			surf->flags |= SURF_DRAWFENCE;
		else if (tex->name[0] == '*')
		{
			// set contents flags
			if (!strncmp (tex->name, "*lava", 5))
				surf->flags |= SURF_DRAWLAVA;
			else if (!strncmp (tex->name, "*tele", 5))
				surf->flags |= SURF_DRAWTELE;
			else if (!strncmp (tex->name, "*slime", 6))
				surf->flags |= SURF_DRAWSLIME;
			else surf->flags |= SURF_DRAWWATER;

			// generic turb marker
			surf->flags |= SURF_DRAWTURB;
		}
		else surf->flags |= SURF_DRAWSOLID;

		D3DSurf_AccumulateSurface (surf);
		Mod_CalcSurfaceBounds (mod, surf);
	}
}


void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;

	if (node->contents < 0)
		return;

	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}


/*
=================
ModBrush_LoadVisLeafsNodes

handles visibility, leafs and nodes

Yayy C++
=================
*/
template <typename nodetype_t, typename leaftype_t>
void ModBrush_LoadVisLeafsNodes (model_t *mod, byte *mod_base, lump_t *v, lump_t *l, lump_t *n)
{
	leaftype_t 	*lin;
	mleaf_t 	*lout;
	nodetype_t	*nin;
	mnode_t		*nout;
	int			i, j, leafcount, p;
	int			nodecount;

	// initial in pointers for leafs and nodes
	lin = (leaftype_t *) (mod_base + l->fileofs);
	nin = (nodetype_t *) (mod_base + n->fileofs);

	if (l->filelen % sizeof (leaftype_t)) Host_Error ("ModBrush_LoadBrushModel: LUMP_LEAFS funny lump size in %s", mod->name);
	if (n->filelen % sizeof (nodetype_t)) Host_Error ("ModBrush_LoadBrushModel: LUMP_NODES funny lump size in %s", mod->name);

	leafcount = l->filelen / sizeof (leaftype_t);
	nodecount = n->filelen / sizeof (nodetype_t);

	mod->brushhdr->numleafs = leafcount;
	mod->brushhdr->numnodes = nodecount;

	// this will crash in-game, so better to take down as gracefully as possible before that
	// note that this is a map format limitation owing to the use of signed shorts for leaf/node numbers
	// (but not necessarily; numleafs + numnodes must not exceed 65536 actually)
	if (mod->brushhdr->numleafs > 65536 && mod->brushhdr->bspversion != BSPVERSIONRMQ)
	{
		Host_Error ("ModBrush_LoadLeafs: mod->brushhdr->numleafs > 65536 without BSPVERSIONRMQ");
		return;
	}

	if (mod->brushhdr->numnodes > 65536 && mod->brushhdr->bspversion != BSPVERSIONRMQ)
	{
		Host_Error ("ModBrush_LoadNodes: mod->brushhdr->numleafs > 65536 without BSPVERSIONRMQ");
		return;
	}

	if (mod->brushhdr->numnodes + mod->brushhdr->numleafs > 65536 && mod->brushhdr->bspversion != BSPVERSIONRMQ)
	{
		Host_Error ("ModBrush_LoadNodes: mod->brushhdr->numnodes + mod->brushhdr->numleafs > 65536 without BSPVERSIONRMQ");
		return;
	}

	// set up vis data buffer and load it in
	byte *visdata = NULL;

	if (v->filelen)
	{
		visdata = (byte *) MainHunk->Alloc (v->filelen);
		Q_MemCpy (visdata, mod_base + v->fileofs, v->filelen);
	}

	// nodes and leafs need to be in consecutive memory - see comment in R_LeafVisibility
	lout = (mleaf_t *) MainHunk->Alloc ((leafcount * sizeof (mleaf_t)) + (nodecount * sizeof (mnode_t)));
	mod->brushhdr->leafs = lout;

	for (i = 0; i < leafcount; i++, lin++, lout++)
	{
		// correct number for SV_ processing
		lout->num = i - 1;

		for (j = 0; j < 3; j++)
		{
			lout->cullinfo.mins[j] = lin->mins[j];
			lout->cullinfo.maxs[j] = lin->maxs[j];
		}

		p = lin->contents;
		lout->contents = p;

		if (mod->brushhdr->bspversion == BSPVERSIONRMQ)
		{
			lout->firstmarksurface = mod->brushhdr->marksurfaces + lin->firstmarksurface;
			lout->nummarksurfaces = lin->nummarksurfaces;
		}
		else
		{
			// cast to unsigned short to conform to BSP file spec
			lout->firstmarksurface = mod->brushhdr->marksurfaces + (unsigned short) lin->firstmarksurface;
			lout->nummarksurfaces = (unsigned short) lin->nummarksurfaces;
		}

		// no visibility yet
		lout->visframe = -1;

		p = lin->visofs;

		if (p == -1 || !v->filelen)
			lout->compressed_vis = NULL;
		else lout->compressed_vis = visdata + p;

		for (j = 0; j < 4; j++)
			lout->ambient_sound_level[j] = lin->ambient_level[j];

		// null the contents
		lout->contentscolor = NULL;

		for (j = 0; j < lout->nummarksurfaces; j++)
		{
			if (lout->contents == CONTENTS_WATER || lout->contents == CONTENTS_LAVA || lout->contents == CONTENTS_SLIME)
			{
				// set contents colour for this leaf
				if ((lout->firstmarksurface[j]->flags & SURF_DRAWWATER) && lout->contents == CONTENTS_WATER)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
				else if ((lout->firstmarksurface[j]->flags & SURF_DRAWLAVA) && lout->contents == CONTENTS_LAVA)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
				else if ((lout->firstmarksurface[j]->flags & SURF_DRAWSLIME) && lout->contents == CONTENTS_SLIME)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
			}

			// duplicate surf flags into the leaf
			lout->flags |= lout->firstmarksurface[j]->flags;
		}

		if (lout->contents == CONTENTS_WATER || lout->contents == CONTENTS_LAVA || lout->contents == CONTENTS_SLIME)
		{
			for (j = 0; j < lout->nummarksurfaces; j++)
			{
				// set contents colour for this leaf
				if ((lout->firstmarksurface[j]->flags & SURF_DRAWWATER) && lout->contents == CONTENTS_WATER)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
				else if ((lout->firstmarksurface[j]->flags & SURF_DRAWLAVA) && lout->contents == CONTENTS_LAVA)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;
				else if ((lout->firstmarksurface[j]->flags & SURF_DRAWSLIME) && lout->contents == CONTENTS_SLIME)
					lout->contentscolor = lout->firstmarksurface[j]->texinfo->texture->contentscolor;

				// duplicate surf flags into the leaf
				lout->flags |= lout->firstmarksurface[j]->flags;
			}
		}
		else
		{
			for (j = 0; j < lout->nummarksurfaces; j++)
			{
				// duplicate surf flags into the leaf
				lout->flags |= lout->firstmarksurface[j]->flags;
			}
		}

		// static entities
		lout->efrags = NULL;
	}

	// set up nodes in contiguous memory - see comment in R_LeafVisibility
	mod->brushhdr->nodes = nout = (mnode_t *) lout;

	// load the nodes
	for (i = 0; i < nodecount; i++, nin++, nout++)
	{
		nout->num = i;

		for (j = 0; j < 3; j++)
		{
			nout->cullinfo.mins[j] = nin->mins[j];
			nout->cullinfo.maxs[j] = nin->maxs[j];
		}

		p = nin->planenum;
		nout->plane = mod->brushhdr->planes + p;

		nout->visframe = -1;

		// the old sexy code i had for setting parent and children at the same time was really fucking with world interaction.
		// things work now.  DO NOT TRY TO SEX THIS UP.
		if (mod->brushhdr->bspversion == BSPVERSIONRMQ)
		{
			nout->surfaces = mod->brushhdr->surfaces + nin->firstface;
			nout->numsurfaces = nin->numfaces;

			// set children and parents here too
			// note - the memory has already been allocated and this field won't be overwritten during node loading
			// so even if a node hasn't yet been loaded it's safe to do this; leafs of course have all been loaded.
			// what the fuck was i smoking when i wrote this...?
			for (j = 0; j < 2; j++)
			{
				if ((p = nin->children[j]) >= 0)
					nout->children[j] = mod->brushhdr->nodes + p;
				else nout->children[j] = (mnode_t *) (mod->brushhdr->leafs + (-1 - p));
			}
		}
		else
		{
			nout->surfaces = mod->brushhdr->surfaces + (unsigned short) nin->firstface;
			nout->numsurfaces = (unsigned short) nin->numfaces;

			for (j = 0; j < 2; j++)
			{
				p = (unsigned short) nin->children[j];

				if (p < nodecount)
					nout->children[j] = mod->brushhdr->nodes + p;
				else
				{
					// note this uses 65535 intentionally, -1 is leaf 0
					p = 65535 - p;

					if (p < mod->brushhdr->numleafs)
						nout->children[j] = (mnode_t *) (mod->brushhdr->leafs + p);
					else
					{
						// map it to the solid leaf
						nout->children[j] = (mnode_t *) (mod->brushhdr->leafs);
						Con_DPrintf ("ModBrush_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, mod->brushhdr->numleafs);
					}
				}
			}
		}
	}

	// first node has no parent
	Mod_SetParent (mod->brushhdr->nodes, NULL);
}


/*
=================
ModBrush_LoadClipnodes

Yayy C++
=================
*/
void ModBrush_SetClippingHull (model_t *mod, hull_t *hull, float *minmaxs)
{
	hull->clipnodes = mod->brushhdr->clipnodes;
	hull->firstclipnode = 0;
	hull->lastclipnode = mod->brushhdr->numclipnodes - 1;
	hull->planes = mod->brushhdr->planes;

	Vector3Copy (hull->clip_mins, &minmaxs[0]);
	Vector3Copy (hull->clip_maxs, &minmaxs[3]);
}


template <typename clipnode_t>
void ModBrush_LoadClipnodes (model_t *mod, byte *mod_base, lump_t *l)
{
	clipnode_t *in = (clipnode_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (clipnode_t))
		Host_Error ("ModBrush_LoadBrushModel: LUMP_CLIPNODES funny lump size in %s", mod->name);

	int count = l->filelen / sizeof (clipnode_t);
	mclipnode_t *out = (mclipnode_t *) MainHunk->Alloc (count * sizeof (mclipnode_t));

	mod->brushhdr->clipnodes = out;
	mod->brushhdr->numclipnodes = count;

	float hull1minmaxs[6] = {-16, -16, -24, 16, 16, 32};
	float hull2minmaxs[6] = {-32, -32, -24, 32, 32, 64};

	ModBrush_SetClippingHull (mod, &mod->brushhdr->hulls[1], hull1minmaxs);
	ModBrush_SetClippingHull (mod, &mod->brushhdr->hulls[2], hull2minmaxs);
	/*
	mod->brushhdr->hulls[1].clipnodes = out;
	mod->brushhdr->hulls[1].firstclipnode = 0;
	mod->brushhdr->hulls[1].lastclipnode = count - 1;
	mod->brushhdr->hulls[1].planes = mod->brushhdr->planes;

	mod->brushhdr->hulls[1].clip_mins[0] = -16;
	mod->brushhdr->hulls[1].clip_mins[1] = -16;
	mod->brushhdr->hulls[1].clip_mins[2] = -24;

	mod->brushhdr->hulls[1].clip_maxs[0] = 16;
	mod->brushhdr->hulls[1].clip_maxs[1] = 16;
	mod->brushhdr->hulls[1].clip_maxs[2] = 32;

	mod->brushhdr->hulls[2].clipnodes = out;
	mod->brushhdr->hulls[2].firstclipnode = 0;
	mod->brushhdr->hulls[2].lastclipnode = count - 1;
	mod->brushhdr->hulls[2].planes = mod->brushhdr->planes;

	mod->brushhdr->hulls[2].clip_mins[0] = -32;
	mod->brushhdr->hulls[2].clip_mins[1] = -32;
	mod->brushhdr->hulls[2].clip_mins[2] = -24;

	mod->brushhdr->hulls[2].clip_maxs[0] = 32;
	mod->brushhdr->hulls[2].clip_maxs[1] = 32;
	mod->brushhdr->hulls[2].clip_maxs[2] = 64;
	*/

	for (int i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->planenum;

		if (mod->brushhdr->bspversion == BSPVERSIONRMQ)
		{
			out->children[0] = in->children[0];
			out->children[1] = in->children[1];
		}
		else
		{
			out->children[0] = (unsigned short) in->children[0];
			out->children[1] = (unsigned short) in->children[1];

			// support > 32k clipnodes
			if (out->children[0] >= count) out->children[0] -= 65536;
			if (out->children[1] >= count) out->children[1] -= 65536;
		}
	}
}


/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0 (model_t *mod)
{
	mnode_t *in = mod->brushhdr->nodes;
	int count = mod->brushhdr->numnodes;
	mclipnode_t *out = (mclipnode_t *) MainHunk->Alloc (count * sizeof (mclipnode_t));
	hull_t *hull = &mod->brushhdr->hulls[0];

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->brushhdr->planes;

	for (int i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->plane - mod->brushhdr->planes;

		for (int j = 0; j < 2; j++)
		{
			mnode_t *child = in->children[j];

			if (child->contents < 0)
				out->children[j] = child->contents;
			else out->children[j] = child - mod->brushhdr->nodes;
		}
	}
}

/*
=================
ModBrush_LoadMarksurfaces

Yayy C++
=================
*/
template <typename marksurf_t>
void ModBrush_LoadMarksurfaces (model_t *mod, byte *mod_base, lump_t *l)
{
	marksurf_t *in = (marksurf_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (marksurf_t))
		Host_Error ("ModBrush_LoadBrushModel: LUMP_MARKSURFACES funny lump size in %s", mod->name);

	int count = l->filelen / sizeof (marksurf_t);
	msurface_t **out = (msurface_t **) MainHunk->Alloc (count * sizeof (msurface_t *));

	mod->brushhdr->marksurfaces = out;
	mod->brushhdr->nummarksurfaces = count;

	for (int i = 0; i < count; i++)
	{
		if (in[i] >= mod->brushhdr->numsurfaces)
			Host_Error ("Mod_ParseMarksurfaces: bad surface number");

		out[i] = mod->brushhdr->surfaces + in[i];
	}
}


/*
=================
ModBrush_LoadPlanes
=================
*/
void ModBrush_LoadPlanes (model_t *mod, byte *mod_base, lump_t *l)
{
	int			i, j;
	dplane_t 	*in;
	mplane_t	*out;
	int			count;
	int			bits;

	in = (dplane_t *) (mod_base + l->fileofs);

	if (l->filelen % sizeof (dplane_t))
		Host_Error ("ModBrush_LoadBrushModel: LUMP_PLANES funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dplane_t);

	// was count * 2; i believe this was to make extra space for a possible expansion of planes * 2 in
	// order to precache both orientations of each plane and not have to use the SURF_PLANEBACK stuff.
	out = (mplane_t *) MainHunk->Alloc (count * sizeof (mplane_t));

	mod->brushhdr->planes = out;
	mod->brushhdr->numplanes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		bits = 0;

		for (j = 0; j < 3; j++)
		{
			out->normal[j] = in->normal[j];

			if (out->normal[j] < 0)
				bits |= 1 << j;
		}

		out->dist = in->dist;
		out->type = in->type;
		out->signbits = bits;
	}
}


void Mod_RecalcNodeBBox (mnode_t *node)
{
	if (!node->plane || node->contents < 0)
	{
		// node is a leaf
		mleaf_t *leaf = (mleaf_t *) node;

		// build a tight bbox around the leaf
		msurface_t **mark = leaf->firstmarksurface;
		int c = leaf->nummarksurfaces;

		if (c)
		{
			Mod_ClearBoundingBox (leaf->cullinfo.mins, leaf->cullinfo.maxs);

			do
			{
				Mod_AccumulateBox (leaf->cullinfo.mins, leaf->cullinfo.maxs, (*mark)->cullinfo.mins, (*mark)->cullinfo.maxs);
				mark++;
			} while (--c);
		}

		return;
	}

	// calculate children first
	Mod_RecalcNodeBBox (node->children[0]);
	Mod_RecalcNodeBBox (node->children[1]);

	// make combined bounding box from children
	mnode_t *nc0 = node->children[0];
	mnode_t *nc1 = node->children[1];

	for (int i = 0; i < 3; i++)
	{
		node->cullinfo.mins[i] = min2 (nc0->cullinfo.mins[i], nc1->cullinfo.mins[i]);
		node->cullinfo.maxs[i] = max2 (nc0->cullinfo.maxs[i], nc1->cullinfo.maxs[i]);
	}
}


void *Mod_CopyLump (dheader_t *header, int lump)
{
	void *data = MainHunk->Alloc (header->lumps[lump].filelen);
	byte *mod_base = (byte *) header;

	Q_MemCpy (data, mod_base + header->lumps[lump].fileofs, header->lumps[lump].filelen);

	return data;
}


void Mod_CalcBModelBBox (model_t *mod, brushhdr_t *hdr)
{
	// qbsp is goddam SLOPPY with bboxes so let's do them right
	msurface_t *surf = hdr->surfaces + hdr->firstmodelsurface;

	Mod_ClearBoundingBox (hdr->bmins, hdr->bmaxs);

	for (int i = 0; i < hdr->nummodelsurfaces; i++, surf++)
		Mod_AccumulateBox (hdr->bmins, hdr->bmaxs, surf->cullinfo.mins, surf->cullinfo.maxs);
}


/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	mod->type = mod_brush;

	dheader_t *header = (dheader_t *) buffer;

	int i = header->version;

	// drop to console
	if (i != PR_BSPVERSION && i != Q1_BSPVERSION && i != BSPVERSIONRMQ)
	{
		Host_Error ("Mod_LoadBrushModel: %s has wrong version number\n(%i should be %i or %i or %i)",
			mod->name, i, PR_BSPVERSION, Q1_BSPVERSION, BSPVERSIONRMQ);
		return;
	}

	byte *mod_base = (byte *) header;

	// alloc space for a brush header
	mod->brushhdr = (brushhdr_t *) MainHunk->Alloc (sizeof (brushhdr_t));

	// store the version for correct hull checking
	mod->brushhdr->bspversion = header->version;
	mod->brushhdr->dvertexes = (dvertex_t *) Mod_CopyLump (header, LUMP_VERTEXES);
	mod->brushhdr->dsurfedges = (int *) Mod_CopyLump (header, LUMP_SURFEDGES);

	// load into heap (these are the only lumps we need to leave hanging around)
	ModBrush_LoadTextures (mod, mod_base, &header->lumps[LUMP_TEXTURES], &header->lumps[LUMP_ENTITIES]);
	ModBrush_LoadLighting (mod, mod_base, &header->lumps[LUMP_LIGHTING]);
	ModBrush_LoadPlanes (mod, mod_base, &header->lumps[LUMP_PLANES]);
	ModBrush_LoadTexinfo (mod, mod_base, &header->lumps[LUMP_TEXINFO]);

	// Yayy C++
	if (header->version == BSPVERSIONRMQ)
	{
		ModBrush_LoadEdges<dedge29a_t> (mod, mod_base, &header->lumps[LUMP_EDGES]);
		ModBrush_LoadSurfaces<dface29a_t> (mod, mod_base, &header->lumps[LUMP_FACES]);
		ModBrush_LoadMarksurfaces<int> (mod, mod_base, &header->lumps[LUMP_MARKSURFACES]);
		ModBrush_LoadVisLeafsNodes<dnode29a_t, dleaf29a_t> (mod, mod_base, &header->lumps[LUMP_VISIBILITY], &header->lumps[LUMP_LEAFS], &header->lumps[LUMP_NODES]);
		ModBrush_LoadClipnodes<dclipnode29a_t> (mod, mod_base, &header->lumps[LUMP_CLIPNODES]);
	}
	else
	{
		ModBrush_LoadEdges<dedge_t> (mod, mod_base, &header->lumps[LUMP_EDGES]);
		ModBrush_LoadSurfaces<dface_t> (mod, mod_base, &header->lumps[LUMP_FACES]);
		ModBrush_LoadMarksurfaces<unsigned short> (mod, mod_base, &header->lumps[LUMP_MARKSURFACES]);
		ModBrush_LoadVisLeafsNodes<dnode_t, dleaf_t>  (mod, mod_base, &header->lumps[LUMP_VISIBILITY], &header->lumps[LUMP_LEAFS], &header->lumps[LUMP_NODES]);
		ModBrush_LoadClipnodes<dclipnode_t> (mod, mod_base, &header->lumps[LUMP_CLIPNODES]);
	}

	ModBrush_LoadEntities (mod, mod_base, &header->lumps[LUMP_ENTITIES]);
	ModBrush_LoadSubmodels (mod, mod_base, &header->lumps[LUMP_MODELS]);

	// set up the model for the same usage as submodels and correct it's bbox
	// (note - correcting the bbox can result in QC crashes in PF_setmodel)
	mod->brushhdr->firstmodelsurface = 0;
	mod->brushhdr->nummodelsurfaces = mod->brushhdr->numsurfaces;

	Mod_MakeHull0 (mod);

	// regular and alternate animation
	mod->numframes = 2;

	// set up the submodels (FIXME: this is confusing)
	// (this should never happen as each model will be it's own first submodel)
	if (!mod->brushhdr->numsubmodels) return;

	// first pass fills in for the world (which is it's own first submodel), then grabs a submodel slot off the list.
	// subsequent passes fill in the submodel slot grabbed at the end of the previous pass.
	// the last pass doesn't need to grab a submodel slot as everything is already filled in
	// fucking hell, he wasn't joking!
	brushhdr_t *smheaders = (brushhdr_t *) MainHunk->Alloc (sizeof (brushhdr_t) * mod->brushhdr->numsubmodels);

	for (i = 0; i < mod->brushhdr->numsubmodels; i++)
	{
		// retrieve the submodel (submodel 0 will be the world)
		dmodel_t *bm = &mod->brushhdr->submodels[i];

		// fill in submodel specific stuff
		mod->brushhdr->hulls[0].firstclipnode = bm->headnode[0];

		for (int j = 1; j < MAX_MAP_HULLS; j++)
		{
			// clipnodes
			mod->brushhdr->hulls[j].firstclipnode = bm->headnode[j];
			mod->brushhdr->hulls[j].lastclipnode = mod->brushhdr->numclipnodes - 1;
		}

		// first surf in the inline model and number of surfs in it
		mod->brushhdr->firstmodelsurface = bm->firstface;
		mod->brushhdr->nummodelsurfaces = bm->numfaces;

		// leafs
		mod->brushhdr->numleafs = bm->visleafs;

		// bounding box
		// (note - correcting this can result in QC crashes in PF_setmodel)
		Vector3Copy (mod->maxs, bm->maxs);
		Vector3Copy (mod->mins, bm->mins);

		// correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
		Vector3Copy (mod->clipmins, mod->mins);
		Vector3Copy (mod->clipmaxs, mod->maxs);

		// radius
		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);

		// grab a submodel slot for the next pass through the loop
		if (i < mod->brushhdr->numsubmodels - 1)
		{
			// duplicate the basic information
			char name[10];

			// build the name
			Q_snprintf (name, 10, "*%i", i + 1);

			// get a slot from the models allocation
			model_t *inlinemod = Mod_FindName (name);

			// duplicate the data
			Q_MemCpy (inlinemod, mod, sizeof (model_t));

			// allocate a new header for the model
			inlinemod->brushhdr = smheaders;
			smheaders++;

			// copy the header data from the original model
			Q_MemCpy (inlinemod->brushhdr, mod->brushhdr, sizeof (brushhdr_t));

			// write in the name
			Q_strncpy (inlinemod->name, name, 63);

			// point mod to the inline model we just got for filling in at the next iteration
			mod = inlinemod;
		}
	}
}


