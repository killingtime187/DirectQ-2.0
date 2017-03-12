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

#define IQM_FOURCC	0x45544E49
#define IQM_MAGIC "INTERQUAKEMODEL"
#define IQM_VERSION1 1	// this was a bug in the demo header file; 1 is the correct version
#define IQM_VERSION2 2	// this was a bug in the demo header file; 1 is the correct version


struct iqmheader_t
{
    char magic[16];
    unsigned int version;
    unsigned int filesize;
    unsigned int flags;
    unsigned int num_text, ofs_text;
    unsigned int num_meshes, ofs_meshes;
    unsigned int num_vertexarrays, num_vertexes, ofs_vertexarrays;
    unsigned int num_triangles, ofs_triangles, ofs_adjacency;
    unsigned int num_joints, ofs_joints;
    unsigned int num_poses, ofs_poses;
    unsigned int num_anims, ofs_anims;
    unsigned int num_frames, num_framechannels, ofs_frames, ofs_bounds;
    unsigned int num_comment, ofs_comment;
    unsigned int num_extensions, ofs_extensions;
};

struct iqmmesh_t
{
    unsigned int name;
    unsigned int material;
    unsigned int first_vertex, num_vertexes;
    unsigned int first_triangle, num_triangles;
};

enum
{
    IQM_POSITION     = 0,
    IQM_TEXCOORD     = 1,
    IQM_NORMAL       = 2,
    IQM_TANGENT      = 3,
    IQM_BLENDINDEXES = 4,
    IQM_BLENDWEIGHTS = 5,
    IQM_COLOR        = 6,
    IQM_CUSTOM       = 0x10
};

enum
{
    IQM_BYTE   = 0,
    IQM_UBYTE  = 1,
    IQM_SHORT  = 2,
    IQM_USHORT = 3,
    IQM_INT    = 4,
    IQM_UINT   = 5,
    IQM_HALF   = 6,
    IQM_FLOAT  = 7,
    IQM_DOUBLE = 8,
};

struct iqmtriangle_t
{
    unsigned short vertex[3];
};

struct iqmjointv1_t
{
    unsigned int name;
    int parent;
    float translate[3], rotate[3], scale[3];
};

struct iqmjointv2_t
{
    unsigned int name;
    int parent;
    float translate[3], rotate[4], scale[3];
};

struct iqmposev1_t
{
    int parent;
    unsigned int mask;
    float channeloffset[9];
    float channelscale[9];
};

struct iqmposev2_t
{
    int parent;
    unsigned int mask;
    float channeloffset[10];
    float channelscale[10];
};

struct iqmanim_t
{
    unsigned int name;
    unsigned int first_frame, num_frames;
    float framerate;
    unsigned int flags;
};

enum
{
    IQM_LOOP = 1<<0
};

struct iqmvertexarray_t
{
    unsigned int type;
    unsigned int flags;
    unsigned int format;
    unsigned int size;
    unsigned int offset;
};

struct iqmbounds_t
{
    float bbmin[3], bbmax[3];
    float xyradius, radius;
};


struct iqmvertex_t
{
	float position[3];
	float normal[3];
	float texcoord[2];
	byte blendindexes[4];
	byte blendweights[4];
};


// move to model.h or modelgen.h when done...
struct iqmdata_t
{
	int version;

	D3DXMATRIX *frames;

	union
	{
		iqmjointv1_t *jointsv1;
		iqmjointv2_t *jointsv2;
	};

	int num_joints;

	vec3_t mins;
	vec3_t maxs;
	float		sphere[4];

	// interleave vertexes for better performance on the GPU.
	// it's 2012, we have hardware vertex processing now.
	iqmvertex_t *verts;
	iqmtriangle_t *tris;

	int num_frames;
	int num_poses;
	int num_triangles;
	int numvertexes;
	int numindexes;

	iqmmesh_t *mesh;

	int num_mesh;
	int buffernum;

	class QTEXTURE **skins;
	class QTEXTURE **fullbrights;
};


