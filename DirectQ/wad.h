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

// wad.h

struct qpic_t
{
	int			width, height;
	byte		data[4];			// variably sized
};


struct wadinfo_t
{
	char		identification[4];		// should be WAD2 or 2DAW
	int			numlumps;
	int			infotableofs;
};

struct lumpinfo_t
{
	int			filepos;
	int			disksize;
	int			size;					// uncompressed
	char		type;
	char		compression;
	char		pad1, pad2;
	char		name[16];				// must be null terminated
};


class QWAD2
{
public:
	QWAD2 (void);
	bool Load (char *filename);
	~QWAD2 (void);
	void *FindLump (char *name);

	static bool LoadPalette (void);

private:
	void ClearWAD (void);

	byte *base;
	wadinfo_t *header;
	int numlumps;
	lumpinfo_t *lumps;
};


extern QWAD2 gfxwad;
