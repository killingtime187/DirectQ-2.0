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
// wad.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

#define	CMP_NONE		0
#define	CMP_LZSS		1

#define	TYP_NONE		0
#define	TYP_LABEL		1

#define	TYP_LUMPY		64				// 64 + grab command number
#define	TYP_PALETTE		64
#define	TYP_QTEX		65
#define	TYP_QPIC		66
#define	TYP_SOUND		67
#define	TYP_MIPTEX		68


int W_LumpCompare (lumpinfo_t *a, lumpinfo_t *b)
{
	return _stricmp (a->name, b->name);
}


QWAD2::QWAD2 (void)
{
	this->ClearWAD ();
}

bool QWAD2::Load (char *filename)
{
	if (!(this->base = (byte *) CQuakeFile::LoadFile (filename, GameHunk))) return false;

	this->header = (wadinfo_t *) this->base;

	// per comment in wad.h both should be allowed
	if (strncmp (this->header->identification, "WAD2", 4))
		if (strncmp (this->header->identification, "2DAW", 4))
			Sys_Error ("Wad file %s doesn't have WAD2 id\n", filename);

	int infotableofs = this->header->infotableofs;

	this->numlumps = this->header->numlumps;
	this->lumps = (lumpinfo_t *) (this->base + infotableofs);

	qsort (this->lumps, this->numlumps, sizeof (lumpinfo_t), (sortfunc_t) W_LumpCompare);

	return true;
}

QWAD2::~QWAD2 (void)
{
	this->ClearWAD ();
}

void *QWAD2::FindLump (char *name)
{
	if (!_strnicmp (name, "gfx/", 4)) return NULL;

	int imin = 0;
	int imax = this->numlumps - 1;

	for (;;)
	{
		if (imax < imin) break;

		int imid = (imax + imin) >> 1;
		int comp = _stricmp (this->lumps[imid].name, name);

		if (comp < 0)
			imin = imid + 1;
		else if (comp > 0)
			imax = imid - 1;
		else return (void *) (this->base + this->lumps[imid].filepos);
	}

	// not found
	return NULL;
}

void QWAD2::ClearWAD (void)
{
	// any further attempts to access it should crash
	this->base = NULL;
	this->header = NULL;
	this->lumps = NULL;
	this->numlumps = 0;
}


// there's only one WAD used by the game (but this is expandable if we ever do HL WADs
// (in which case we'll extend the class to support them)
QWAD2 gfxwad;


// palettes need to be reloaded on every game change so do it here
// this should really move to vidnt.cpp
void D3DImage_MakeQuakePalettes (byte *palette);

bool QWAD2::LoadPalette (void)
{
	// these need to be statics so that they can be freed OK
	static byte *palette = NULL;

	if (palette) MainZone->Free (palette);

	if ((palette = (byte *) CQuakeFile::LoadFile ("gfx/palette.lmp")) != NULL)
	{
		D3DImage_MakeQuakePalettes (palette);
		return true;
	}

	// failed to load either
	return false;
}

