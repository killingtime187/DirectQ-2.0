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
#include "iqm.h"
#include "particles.h"


// to do - how is this calculated anyway?  looks like normals for a sphere to me...
float r_avertexnormals[NUMVERTEXNORMALS][3] =
{
	{-0.525731f, 0.000000f, 0.850651f}, {-0.442863f, 0.238856f, 0.864188f}, {-0.295242f, 0.000000f, 0.955423f}, {-0.309017f, 0.500000f, 0.809017f},
	{-0.162460f, 0.262866f, 0.951056f}, {0.000000f, 0.000000f, 1.000000f}, {0.000000f, 0.850651f, 0.525731f}, {-0.147621f, 0.716567f, 0.681718f},
	{0.147621f, 0.716567f, 0.681718f}, {0.000000f, 0.525731f, 0.850651f}, {0.309017f, 0.500000f, 0.809017f}, {0.525731f, 0.000000f, 0.850651f},
	{0.295242f, 0.000000f, 0.955423f}, {0.442863f, 0.238856f, 0.864188f}, {0.162460f, 0.262866f, 0.951056f}, {-0.681718f, 0.147621f, 0.716567f},
	{-0.809017f, 0.309017f, 0.500000f}, {-0.587785f, 0.425325f, 0.688191f}, {-0.850651f, 0.525731f, 0.000000f}, {-0.864188f, 0.442863f, 0.238856f},
	{-0.716567f, 0.681718f, 0.147621f}, {-0.688191f, 0.587785f, 0.425325f}, {-0.500000f, 0.809017f, 0.309017f}, {-0.238856f, 0.864188f, 0.442863f},
	{-0.425325f, 0.688191f, 0.587785f}, {-0.716567f, 0.681718f, -0.147621f}, {-0.500000f, 0.809017f, -0.309017f}, {-0.525731f, 0.850651f, 0.000000f},
	{0.000000f, 0.850651f, -0.525731f}, {-0.238856f, 0.864188f, -0.442863f}, {0.000000f, 0.955423f, -0.295242f}, {-0.262866f, 0.951056f, -0.162460f},
	{0.000000f, 1.000000f, 0.000000f}, {0.000000f, 0.955423f, 0.295242f}, {-0.262866f, 0.951056f, 0.162460f}, {0.238856f, 0.864188f, 0.442863f},
	{0.262866f, 0.951056f, 0.162460f}, {0.500000f, 0.809017f, 0.309017f}, {0.238856f, 0.864188f, -0.442863f}, {0.262866f, 0.951056f, -0.162460f},
	{0.500000f, 0.809017f, -0.309017f}, {0.850651f, 0.525731f, 0.000000f}, {0.716567f, 0.681718f, 0.147621f}, {0.716567f, 0.681718f, -0.147621f},
	{0.525731f, 0.850651f, 0.000000f}, {0.425325f, 0.688191f, 0.587785f}, {0.864188f, 0.442863f, 0.238856f}, {0.688191f, 0.587785f, 0.425325f},
	{0.809017f, 0.309017f, 0.500000f}, {0.681718f, 0.147621f, 0.716567f}, {0.587785f, 0.425325f, 0.688191f}, {0.955423f, 0.295242f, 0.000000f},
	{1.000000f, 0.000000f, 0.000000f}, {0.951056f, 0.162460f, 0.262866f}, {0.850651f, -0.525731f, 0.000000f}, {0.955423f, -0.295242f, 0.000000f},
	{0.864188f, -0.442863f, 0.238856f}, {0.951056f, -0.162460f, 0.262866f}, {0.809017f, -0.309017f, 0.500000f}, {0.681718f, -0.147621f, 0.716567f},
	{0.850651f, 0.000000f, 0.525731f}, {0.864188f, 0.442863f, -0.238856f}, {0.809017f, 0.309017f, -0.500000f}, {0.951056f, 0.162460f, -0.262866f},
	{0.525731f, 0.000000f, -0.850651f}, {0.681718f, 0.147621f, -0.716567f}, {0.681718f, -0.147621f, -0.716567f}, {0.850651f, 0.000000f, -0.525731f},
	{0.809017f, -0.309017f, -0.500000f}, {0.864188f, -0.442863f, -0.238856f}, {0.951056f, -0.162460f, -0.262866f}, {0.147621f, 0.716567f, -0.681718f},
	{0.309017f, 0.500000f, -0.809017f}, {0.425325f, 0.688191f, -0.587785f}, {0.442863f, 0.238856f, -0.864188f}, {0.587785f, 0.425325f, -0.688191f},
	{0.688191f, 0.587785f, -0.425325f}, {-0.147621f, 0.716567f, -0.681718f}, {-0.309017f, 0.500000f, -0.809017f}, {0.000000f, 0.525731f, -0.850651f},
	{-0.525731f, 0.000000f, -0.850651f}, {-0.442863f, 0.238856f, -0.864188f}, {-0.295242f, 0.000000f, -0.955423f}, {-0.162460f, 0.262866f, -0.951056f},
	{0.000000f, 0.000000f, -1.000000f}, {0.295242f, 0.000000f, -0.955423f}, {0.162460f, 0.262866f, -0.951056f}, {-0.442863f, -0.238856f, -0.864188f},
	{-0.309017f, -0.500000f, -0.809017f}, {-0.162460f, -0.262866f, -0.951056f}, {0.000000f, -0.850651f, -0.525731f}, {-0.147621f, -0.716567f, -0.681718f},
	{0.147621f, -0.716567f, -0.681718f}, {0.000000f, -0.525731f, -0.850651f}, {0.309017f, -0.500000f, -0.809017f}, {0.442863f, -0.238856f, -0.864188f},
	{0.162460f, -0.262866f, -0.951056f}, {0.238856f, -0.864188f, -0.442863f}, {0.500000f, -0.809017f, -0.309017f}, {0.425325f, -0.688191f, -0.587785f},
	{0.716567f, -0.681718f, -0.147621f}, {0.688191f, -0.587785f, -0.425325f}, {0.587785f, -0.425325f, -0.688191f}, {0.000000f, -0.955423f, -0.295242f},
	{0.000000f, -1.000000f, 0.000000f}, {0.262866f, -0.951056f, -0.162460f}, {0.000000f, -0.850651f, 0.525731f}, {0.000000f, -0.955423f, 0.295242f},
	{0.238856f, -0.864188f, 0.442863f}, {0.262866f, -0.951056f, 0.162460f}, {0.500000f, -0.809017f, 0.309017f}, {0.716567f, -0.681718f, 0.147621f},
	{0.525731f, -0.850651f, 0.000000f}, {-0.238856f, -0.864188f, -0.442863f}, {-0.500000f, -0.809017f, -0.309017f}, {-0.262866f, -0.951056f, -0.162460f},
	{-0.850651f, -0.525731f, 0.000000f}, {-0.716567f, -0.681718f, -0.147621f}, {-0.716567f, -0.681718f, 0.147621f}, {-0.525731f, -0.850651f, 0.000000f},
	{-0.500000f, -0.809017f, 0.309017f}, {-0.238856f, -0.864188f, 0.442863f}, {-0.262866f, -0.951056f, 0.162460f}, {-0.864188f, -0.442863f, 0.238856f},
	{-0.809017f, -0.309017f, 0.500000f}, {-0.688191f, -0.587785f, 0.425325f}, {-0.681718f, -0.147621f, 0.716567f}, {-0.442863f, -0.238856f, 0.864188f},
	{-0.587785f, -0.425325f, 0.688191f}, {-0.309017f, -0.500000f, 0.809017f}, {-0.147621f, -0.716567f, 0.681718f}, {-0.425325f, -0.688191f, 0.587785f},
	{-0.162460f, -0.262866f, 0.951056f}, {0.442863f, -0.238856f, 0.864188f}, {0.162460f, -0.262866f, 0.951056f}, {0.309017f, -0.500000f, 0.809017f},
	{0.147621f, -0.716567f, 0.681718f}, {0.000000f, -0.525731f, 0.850651f}, {0.425325f, -0.688191f, 0.587785f}, {0.587785f, -0.425325f, 0.688191f},
	{0.688191f, -0.587785f, 0.425325f}, {-0.955423f, 0.295242f, 0.000000f}, {-0.951056f, 0.162460f, 0.262866f}, {-1.000000f, 0.000000f, 0.000000f},
	{-0.850651f, 0.000000f, 0.525731f}, {-0.955423f, -0.295242f, 0.000000f}, {-0.951056f, -0.162460f, 0.262866f}, {-0.864188f, 0.442863f, -0.238856f},
	{-0.951056f, 0.162460f, -0.262866f}, {-0.809017f, 0.309017f, -0.500000f}, {-0.864188f, -0.442863f, -0.238856f}, {-0.951056f, -0.162460f, -0.262866f},
	{-0.809017f, -0.309017f, -0.500000f}, {-0.681718f, 0.147621f, -0.716567f}, {-0.681718f, -0.147621f, -0.716567f}, {-0.850651f, 0.000000f, -0.525731f},
	{-0.688191f, 0.587785f, -0.425325f}, {-0.587785f, 0.425325f, -0.688191f}, {-0.425325f, 0.688191f, -0.587785f}, {-0.425325f, -0.688191f, -0.587785f},
	{-0.587785f, -0.425325f, -0.688191f}, {-0.688191f, -0.587785f, -0.425325f}
};


bool Mod_FindIQMModel (model_t *mod);
void D3DAlias_MakeAliasMesh (char *name, aliashdr_t *hdr, aliasload_t *load);


/*
==============================================================================

ALIAS MODELS

==============================================================================
*/


void Mod_LoadAliasBBoxes (model_t *mod, aliashdr_t *hdr, aliasload_t *load)
{
	// compute bounding boxes per-frame as well as for the whole model
	// per-frame bounding boxes are used in the renderer for frustum culling and occlusion queries
	// whole model bounding boxes are used on the server
	aliasbbox_t *framebboxes = (aliasbbox_t *) MainHunk->Alloc (hdr->nummeshframes * sizeof (aliasbbox_t));
	hdr->bboxes = framebboxes;

	Mod_ClearBoundingBox (mod->mins, mod->maxs);
	Vector3Clear (hdr->midpoint);

	for (int i = 0; i < hdr->nummeshframes; i++, framebboxes++)
	{
		Mod_ClearBoundingBox (framebboxes->mins, framebboxes->maxs);

		for (int v = 0; v < hdr->vertsperframe; v++)
		{
			// we can't put this through our box accumulator as these are byte vectors
			for (int n = 0; n < 3; n++)
			{
				if (load->vertexes[i][v].v[n] < framebboxes->mins[n]) framebboxes->mins[n] = load->vertexes[i][v].v[n];
				if (load->vertexes[i][v].v[n] > framebboxes->maxs[n]) framebboxes->maxs[n] = load->vertexes[i][v].v[n];
				hdr->midpoint[n] += load->vertexes[i][v].v[n];
			}
		}

		for (int n = 0; n < 3; n++)
		{
			// rescale and accumulate
			framebboxes->mins[n] = framebboxes->mins[n] * hdr->scale[n] + hdr->scale_origin[n];
			framebboxes->maxs[n] = framebboxes->maxs[n] * hdr->scale[n] + hdr->scale_origin[n];

			Mod_AccumulateBox (mod->mins, mod->maxs, framebboxes->mins, framebboxes->maxs);
		}
	}

	// derive the final midpoint - this is really only needed for fixing up gl_doubleeyes
	Vector3Recip (hdr->midpoint, hdr->midpoint, hdr->nummeshframes);
	Vector3Recip (hdr->midpoint, hdr->midpoint, hdr->vertsperframe);
	Vector3Scale (hdr->midpoint, hdr->midpoint, hdr->scale);
	Vector3Add (hdr->midpoint, hdr->midpoint, hdr->scale_origin);

	// absolute clamp so that we don't go into the wrong clipping hull
	for (int i = 0; i < 3; i++)
	{
		if (mod->mins[i] > -16) mod->mins[i] = -16;
		if (mod->maxs[i] < 16) mod->maxs[i] = 16;
	}
}


struct vertexnormals_t
{
	int numnormals;
	float normal[3];
};


void Mod_LoadFrameVerts (aliashdr_t *hdr, trivertx_t *verts, aliasload_t *load)
{
	// to do - remap stverts for floating point; do them for baseframe only or for each frame???
	drawvertx_t *vertexes = (drawvertx_t *) TempHunk->FastAlloc (hdr->vertsperframe * sizeof (drawvertx_t));
	int fhunkmark = TempHunk->GetLowMark ();
	vertexnormals_t *vnorms = (vertexnormals_t *) TempHunk->FastAlloc (sizeof (vertexnormals_t) * hdr->vertsperframe);

	// copy out positions and set up for normals calculation
	for (int i = 0; i < hdr->vertsperframe; i++)
	{
		// provide a default w coord for vertex buffer input
		Q_MemCpy (vertexes[i].v, verts[i].v, 3);
		vertexes[i].v[3] = 1;

		// assume that the vertex will be lerped by default
		vertexes[i].lerpvert = true;

		// no normals initially
		Vector3Clear (vnorms[i].normal);
		vnorms[i].numnormals = 0;
	}

	// recalc the normals based on modelgen.c code
	for (int i = 0; i < hdr->numtris; i++)
	{
		float triverts[3][3];
		float vtemp1[3], vtemp2[3], normal[3];
		int *vertindexes = load->triangles[i].vertindex;

		// undo the vertex rotation from modelgen.c here too
		for (int j = 0; j < 3; j++)
		{
			Vector3Set (triverts[j],
				(float) verts[vertindexes[j]].v[1] * hdr->scale[1] + hdr->scale_origin[1],
				-((float) verts[vertindexes[j]].v[0] * hdr->scale[0] + hdr->scale_origin[0]),
				(float) verts[vertindexes[j]].v[2] * hdr->scale[2] + hdr->scale_origin[2]);
		}

		// calc the per-triangle normal
		Vector3Subtract (vtemp1, triverts[0], triverts[1]);
		Vector3Subtract (vtemp2, triverts[2], triverts[1]);
		Vector3Cross (normal, vtemp1, vtemp2);
		Vector3Normalize (normal);

		// rotate the normal so the model faces down the positive x axis
		float newnormal[3] = {-normal[1], normal[0], normal[2]};

		// and accumulate it into the calculated normals array
		for (int j = 0; j < 3; j++)
		{
			Vector3Add (vnorms[vertindexes[j]].normal, vnorms[vertindexes[j]].normal, newnormal);
			vnorms[vertindexes[j]].numnormals++;
		}
	}

	// copy out normals
	for (int i = 0; i < hdr->vertsperframe; i++)
	{
		// numnormals was checked for > 0 in modelgen.c so we shouldn't need to do it again 
		// here but we do anyway just in case a rogue modder has used a bad modelling tool
		if (vnorms[i].numnormals > 0)
		{
			Vector3Scale (vertexes[i].normal, vnorms[i].normal, (float) vnorms[i].numnormals);
			Vector3Normalize (vertexes[i].normal);
		}
		else Vector3Set (vertexes[i].normal, 0.0f, 0.0f, 1.0f);
	}

	// and done
	TempHunk->FreeToLowMark (fhunkmark);

	load->vertexes[hdr->nummeshframes] = vertexes;
	hdr->nummeshframes++;
}


/*
=================
Mod_LoadAliasFrame
=================
*/
daliasframetype_t *Mod_LoadAliasFrame (aliashdr_t *hdr, daliasframe_t *pdaliasframe, maliasframedesc_t *frame, aliasload_t *load)
{
	Q_strncpy (frame->name, pdaliasframe->name, 16);
	frame->firstpose = hdr->nummeshframes;
	frame->numposes = 1;

	for (int i = 0; i < 3; i++)
	{
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}

	trivertx_t *verts = (trivertx_t *) (pdaliasframe + 1);

	// load the frame vertexes
	Mod_LoadFrameVerts (hdr, verts, load);

	return (daliasframetype_t *) (verts + hdr->vertsperframe);
}


/*
=================
Mod_LoadAliasGroup
=================
*/
daliasframetype_t *Mod_LoadAliasGroup (aliashdr_t *hdr, daliasgroup_t *pingroup, maliasframedesc_t *frame, aliasload_t *load)
{
	frame->firstpose = hdr->nummeshframes;
	frame->numposes = pingroup->numframes;

	for (int i = 0; i < 3; i++)
	{
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}

	// let's do frame intervals properly
	daliasinterval_t *pin_intervals = (daliasinterval_t *) (pingroup + 1);
	frame->intervals = (float *) MainHunk->Alloc ((pingroup->numframes + 1) * sizeof (float));

	frame->intervals[pingroup->numframes] = pin_intervals[pingroup->numframes - 1].interval + pin_intervals[0].interval;

	void *ptemp = (void *) (pin_intervals + pingroup->numframes);

	for (int i = 0; i < pingroup->numframes; i++)
	{
		frame->intervals[i] = pin_intervals[i].interval;
		Mod_LoadFrameVerts (hdr, (trivertx_t *) ((daliasframe_t *) ptemp + 1), load);
		ptemp = (trivertx_t *) ((daliasframe_t *) ptemp + 1) + hdr->vertsperframe;
	}

	return (daliasframetype_t *) ptemp;
}


//=========================================================

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

struct floodfill_t
{
	short		x, y;
};


// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

void Mod_FloodFillSkin (byte *skin, int skinwidth, int skinheight)
{
	byte		fillcolor = skin[0]; // assume this is the pixel to fill
	int			hunkmark = TempHunk->GetLowMark ();
	floodfill_t	*fifo = (floodfill_t *) TempHunk->FastAlloc (FLOODFILL_FIFO_SIZE * sizeof (floodfill_t));
	int			inpt = 0, outpt = 0;
	int			filledcolor = -1;

	// this will always be true
	// opaque black doesn't exist in the gamma scaled palette (this is also true of GLQuake)
	// so just use the darkest colour in the palette instead
	if (filledcolor == -1) filledcolor = d3d_QuakePalette.darkindex;

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		// hmm - this happens more often than one would like...
		// Con_DPrintf ("not filling skin from %d to %d\n", fillcolor, filledcolor);
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP (-1, -1, 0);
		if (x < skinwidth - 1)	FLOODFILL_STEP (1, 1, 0);
		if (y > 0)				FLOODFILL_STEP (-skinwidth, 0, -1);
		if (y < skinheight - 1)	FLOODFILL_STEP (skinwidth, 0, 1);

		skin[x + skinwidth * y] = fdc;
	}

	TempHunk->FreeToLowMark (hunkmark);
}


QTEXTURE *Mod_LoadPlayerColormap (char *name, int width, int height, byte *texels)
{
	int s = width * height;
	int hunkmark = TempHunk->GetLowMark ();
	unsigned int *rgba = (unsigned int *) TempHunk->FastAlloc (s * 4);
	byte *pixels = (byte *) rgba;

	for (int j = 0; j < s; j++, pixels += 4)
	{
		bool base = false;
		bool shirt = false;
		bool pants = false;

		if (texels[j] < 16)
			base = true;
		else if (texels[j] < 32)
			shirt = true;
		else if (texels[j] < 96)
			base = true;
		else if (texels[j] < 112)
			pants = true;
		else base = true;

		// these are greyscale colours so we can just take the red channel (the others are equal anyway)
		pixels[0] = shirt ? 255 : 0;
		pixels[1] = shirt ? ((byte *) &d3d_QuakePalette.standard11[texels[j] - 16])[0] : 0;
		pixels[2] = pants ? ((byte *) &d3d_QuakePalette.standard11[texels[j] - 96])[0] : 0;
		pixels[3] = pants ? 255 : 0;
	}

	QTEXTURE *cm = QTEXTURE::Load (va ("%s_colormap", name),
		width, height,
		(byte *) rgba, IMAGE_ALPHA | IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_32BIT);

	TempHunk->FreeToLowMark (hunkmark);

	return cm;
}


/*
===============
Mod_LoadAllSkins
===============
*/
void Mod_AllocSkins (aliasskin_t *skin, int numskins)
{
	if (numskins > 0)
	{
		skin->teximage = (QTEXTURE **) MainHunk->Alloc (sizeof (QTEXTURE *) * numskins);
		skin->lumaimage = (QTEXTURE **) MainHunk->Alloc (sizeof (QTEXTURE *) * numskins);
		skin->cmapimage = (QTEXTURE **) MainHunk->Alloc (sizeof (QTEXTURE *) * numskins);
	}
	else Sys_Error ("Mod_AllocSkins : skin group with < 1 skin(s)");
}


void Mod_LoadAliasSkin (model_t *mod, aliasskin_t *skin, int num, char *name, int width, int height, byte *texels)
{
	int texflags = IMAGE_MIPMAP | IMAGE_ALIAS;
	int lumaflags = IMAGE_MIPMAP | IMAGE_ALIAS | IMAGE_LUMA;

	// Mod_FloodFillSkin (texels, width, height);

	// standard flood-fill causes a mess with luma masks so we just replace all occurrances of the top-left with full alpha
	// then run alphaedgefix on the expanded 32-bit data instead
	byte filltex = texels[0];

	for (int i = 0; i < (width * height); i++)
		if (texels[i] == filltex)
			texels[i] = 255;

	// default paths are good for these
	skin->teximage[num] = QTEXTURE::Load (name, width, height, texels, texflags);
	skin->lumaimage[num] = QTEXTURE::Load (name, width, height, texels, lumaflags);

	if (!_stricmp (mod->name, "progs/player.mdl"))
	{
		skin->cmapimage[num] = Mod_LoadPlayerColormap (name, width, height, texels);
		mod->flags |= EF_PLAYER;
	}
	else skin->cmapimage[num] = NULL;
}


void *Mod_LoadAllSkins (model_t *mod, aliashdr_t *hdr, daliasskintype_t *pskintype)
{
	int j;
	char name[64];

	if (hdr->numskins < 1) Host_Error ("Mod_LoadAllSkins: Invalid # of skins: %d\n", hdr->numskins);

	hdr->skins = (aliasskin_t *) MainHunk->Alloc (hdr->numskins * sizeof (aliasskin_t));

	// don't remove the extension here as Q1 has s_light.mdl and s_light.spr, so we need to differentiate them
	// dropped skin padding because there are too many special cases
	for (int i = 0; i < hdr->numskins; i++)
	{
		if (pskintype->type == ALIAS_SKIN_SINGLE)
		{
			Q_snprintf (name, 63, "%s_%i", mod->name, i);
			Mod_AllocSkins (&hdr->skins[i], 1);
			Mod_LoadAliasSkin (mod, &hdr->skins[i], 0, name, hdr->skinwidth, hdr->skinheight, (byte *) (pskintype + 1));

			hdr->skins[i].numskins = 1;
			hdr->skins[i].type = ALIAS_SKIN_SINGLE;
			hdr->skins[i].intervals = NULL;

			pskintype = (daliasskintype_t *) ((byte *) (pskintype + 1) + (hdr->skinwidth * hdr->skinheight));
		}
		else
		{
			// animating skin group.  yuck.
			pskintype++;

			daliasskingroup_t *pinskingroup = (daliasskingroup_t *) pskintype;
			int groupskins = pinskingroup->numskins;
			daliasskininterval_t *pinskinintervals = (daliasskininterval_t *) (pinskingroup + 1);

			hdr->skins[i].intervals = (float *) MainHunk->Alloc (groupskins * sizeof (float));
			pskintype = (daliasskintype_t *) (pinskinintervals + groupskins);

			Mod_AllocSkins (&hdr->skins[i], groupskins);

			for (j = 0; j < groupskins; j++)
			{
				Q_snprintf (name, 63, "%s_%i_%i", mod->name, i, j);
				Mod_LoadAliasSkin (mod, &hdr->skins[i], j, name, hdr->skinwidth, hdr->skinheight, (byte *) (pskintype));

				// load correct interval and advance
				hdr->skins[i].intervals[j] = pinskinintervals[j].interval;
				pskintype = (daliasskintype_t *) ((byte *) (pskintype) + (hdr->skinwidth * hdr->skinheight));
			}

			hdr->skins[i].numskins = groupskins;
			hdr->skins[i].type = ALIAS_SKIN_GROUP;
		}
	}

	return (void *) pskintype;
}

//=========================================================================

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	mdl_t *pinmodel = (mdl_t *) buffer;
	aliasload_t load;

	// look for an IQM to replace it
	if (Mod_FindIQMModel (mod))
	{
		// copy across the flags from the model it replaced (note DP extension clearing here too)
		mod->flags = pinmodel->flags & 255;
		return;
	}

	if (pinmodel->version != ALIAS_VERSION)
		Host_Error ("%s has wrong version number (%i should be %i)", mod->name, pinmodel->version, ALIAS_VERSION);

	// alloc the header in the cache
	aliashdr_t *hdr = (aliashdr_t *) MainHunk->Alloc (sizeof (aliashdr_t));

	// clear the loading struct
	memset (&load, 0, sizeof (load));

	// fill in basic stuff
	mod->flags = pinmodel->flags;
	mod->type = mod_alias;

	// darkplaces extends model flags so here we must clear the extra ones so that we can safely add our own
	// (this is only relevant if an MDL was made with extended DP flags)
	mod->flags &= 255;

	// even if we alloced from the cache we still fill it in
	// endian-adjust and copy the data, starting with the alias model header
	hdr->boundingradius = pinmodel->boundingradius;
	hdr->numskins = pinmodel->numskins;
	hdr->skinwidth = pinmodel->skinwidth;
	hdr->skinheight = pinmodel->skinheight;
	hdr->vertsperframe = pinmodel->numverts;
	hdr->numtris = pinmodel->numtris;
	hdr->numframes = pinmodel->numframes;

	// validate the setup
	// Sys_Error seems a little harsh here...
	if (hdr->numframes < 1) Host_Error ("Mod_LoadAliasModel: Model %s has invalid # of frames: %d\n", mod->name, hdr->numframes);
	if (hdr->numtris <= 0) Host_Error ("model %s has no triangles", mod->name);
	if (hdr->vertsperframe <= 0) Host_Error ("model %s has no vertices", mod->name);

	hdr->size = pinmodel->size * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) pinmodel->synctype;
	mod->numframes = hdr->numframes;

	for (int i = 0; i < 3; i++)
	{
		hdr->scale[i] = pinmodel->scale[i];
		hdr->scale_origin[i] = pinmodel->scale_origin[i];
	}

	// load the skins
	daliasskintype_t *pskintype = (daliasskintype_t *) &pinmodel[1];
	pskintype = (daliasskintype_t *) Mod_LoadAllSkins (mod, hdr, pskintype);

	// load base s and t vertices
	load.stverts = (stvert_t *) pskintype;

	// load triangle lists
	load.triangles = (dtriangle_t *) &load.stverts[hdr->vertsperframe];

	// load the frames
	daliasframetype_t *pframetype = (daliasframetype_t *) &load.triangles[hdr->numtris];
	hdr->frames = (maliasframedesc_t *) MainHunk->Alloc (hdr->numframes * sizeof (maliasframedesc_t));

	hdr->nummeshframes = 0;

	// because we don't know how many frames we need in advance we take a copy to the scratch buffer initially
	// the size of the scratch buffer is compatible with the max number of frames allowed by protocol 666
	load.vertexes = (drawvertx_t **) scratchbuf;

	for (int i = 0; i < hdr->numframes; i++)
	{
		aliasframetype_t frametype = (aliasframetype_t) pframetype->type;

		if (frametype == ALIAS_SINGLE)
		{
			daliasframe_t *frame = (daliasframe_t *) (pframetype + 1);
			pframetype = Mod_LoadAliasFrame (hdr, frame, &hdr->frames[i], &load);
		}
		else
		{
			daliasgroup_t *group = (daliasgroup_t *) (pframetype + 1);
			pframetype = Mod_LoadAliasGroup (hdr, group, &hdr->frames[i], &load);
		}
	}

	// copy framepointers from the scratch buffer to the final cached copy
	drawvertx_t **hdrvertexes = (drawvertx_t **) TempHunk->FastAlloc (hdr->nummeshframes * sizeof (drawvertx_t *));

	for (int i = 0; i < hdr->nummeshframes; i++)
		hdrvertexes[i] = load.vertexes[i];

	load.vertexes = hdrvertexes;

	Mod_LoadAliasBBoxes (mod, hdr, &load);

	// build the draw lists and vertex/index buffers
	D3DAlias_MakeAliasMesh (mod->name, hdr, &load);

	// set the final header
	mod->aliashdr = hdr;

	// muzzleflash colour flags and other hard-coded crapness
	if (!strncmp (&mod->name[6], "wizard", 6)) mod->flags |= EF_WIZARDFLASH;
	if (!strncmp (&mod->name[6], "shalrath", 6)) mod->flags |= EF_SHALRATHFLASH;
	if (!strncmp (&mod->name[6], "shambler", 6)) mod->flags |= EF_SHAMBLERFLASH;

	// calculate drawflags
	hdr->drawflags = 0;
	char *Name = strrchr (mod->name, '/');

	if (Name)
	{
		Name++;

		if (strstr (mod->name, "flame")) hdr->drawflags |= AM_FLAME;
		if (strstr (mod->name, "torch")) hdr->drawflags |= AM_FLAME;
		if (strstr (mod->name, "candle")) hdr->drawflags |= AM_FLAME;
		if (strstr (mod->name, "trch")) hdr->drawflags |= AM_FLAME;
		if (strstr (mod->name, "fire")) hdr->drawflags |= AM_FLAME;

		// this list was more or less lifted straight from Bengt Jardrup's engine.  Personally I think that hard-coding
		// behaviours like this into the engine is evil, but there are mods that depend on it so oh well.
		// At least I guess it's *standardized* evil...
		if (!strcmp (mod->name, "progs/flame.mdl") || !strcmp (mod->name, "progs/flame2.mdl"))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if (!strcmp (mod->name, "progs/eyes.mdl"))
			hdr->drawflags |= AM_EYES;
		else if (!strcmp (mod->name, "progs/bolt.mdl"))
			hdr->drawflags |= AM_NOSHADOW;
		else if (!strncmp (Name, "flame", 5) || !strncmp (Name, "torch", 5) || !strcmp (mod->name, "progs/missile.mdl") ||
			!strcmp (Name, "newfire.mdl") || !strcmp (Name, "longtrch.mdl") || !strcmp (Name, "bm_reap.mdl"))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if (!strncmp (Name, "lantern", 7) ||
			 !strcmp (Name, "brazshrt.mdl") ||  // For Chapters ...
			 !strcmp (Name, "braztall.mdl"))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if (!strncmp (Name, "bolt", 4) ||	    // Bolts ...
			 !strcmp (Name, "s_light.mdl"))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if (!strncmp (Name, "candle", 6))
			hdr->drawflags |= (AM_NOSHADOW | AM_FULLBRIGHT);
		else if ((!strcmp (Name, "necro.mdl") ||
			 !strcmp (Name, "wizard.mdl") ||
			 !strcmp (Name, "wraith.mdl")) && nehahra)	    // Nehahra
			hdr->drawflags |= AM_NOSHADOW;
		else if (!strcmp (Name, "beam.mdl") ||	    // Rogue
			 !strcmp (Name, "dragon.mdl") ||    // Rogue
			 !strcmp (Name, "eel2.mdl") ||	    // Rogue
			 !strcmp (Name, "fish.mdl") ||
			 !strcmp (Name, "flak.mdl") ||	    // Marcher
			 (Name[0] != 'v' && !strcmp (&Name[1], "_spike.mdl")) ||
			 !strcmp (Name, "imp.mdl") ||
			 !strcmp (Name, "laser.mdl") ||
			 !strcmp (Name, "lasrspik.mdl") ||  // Hipnotic
			 !strcmp (Name, "lspike.mdl") ||    // Rogue
			 !strncmp (Name, "plasma", 6) ||    // Rogue
			 !strcmp (Name, "spike.mdl") ||
			 !strncmp (Name, "tree", 4) ||
			 !strcmp (Name, "wr_spike.mdl"))    // Nehahra
			hdr->drawflags |= AM_NOSHADOW;
	}
}


