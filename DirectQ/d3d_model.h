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

#ifndef __MODEL__
#define __MODEL__

#include "modelgen.h"
#include "spritegn.h"

#define AM_NOSHADOW			1
#define AM_FULLBRIGHT		2
#define AM_EYES				4
#define AM_DRAWSHADOW		8
#define AM_VIEWMODEL		16
#define AM_IQM				32
#define AM_FLAME			64
#define AM_INSTANCED		128

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

// entity effects

#define	EF_BRIGHTFIELD			1
#define	EF_MUZZLEFLASH 			2
#define	EF_BRIGHTLIGHT 			4
#define	EF_DIMLIGHT 			8
#define	EF_REDLIGHT 			16
#define	EF_BLUELIGHT 			32

#define EF_FULLBRIGHT			16384

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2

struct mvertex_t
{
	vec3_t position;
};

struct mclipnode_t
{
	int			planenum;
	int			children[2];	// negative numbers are contents
};

struct texture_t
{
	// 1 extra for 16 char texnames
	char		name[17];

	float		size[2];

	class QTEXTURE *teximage;
	class QTEXTURE *lumaimage;

	int			anim_total;				// total tenths in sequence (0 = no)
	int			anim_min, anim_max;		// time for this frame anim_min <= time < anim_max
	struct texture_t *anim_next;		// in the animation sequence

	// for faster retrieval animframes[0] is always the texture, animframes[1] is alternate anims
	struct texture_t *animframes[2];

	int contentscolor[3];

	// chains for rendering
	struct msurface_t *texturechain;
	struct msurface_t **texturechain_tail;

	// for indexed drawing
	int firstindex;
	int numindexes;
};


// basic types
#define	SURF_PLANEBACK		2
#define	SURF_DRAWSKY		4
#define SURF_DRAWSOLID		8
#define SURF_DRAWTURB		16

// contents types
#define SURF_DRAWLAVA		256
#define SURF_DRAWTELE		512
#define SURF_DRAWWATER		1024
#define SURF_DRAWSLIME		2048
#define SURF_DRAWFENCE		4096

struct medge_t
{
	unsigned int	v[2];
};

struct mtexinfo_t
{
	float		vecs[2][4];
	texture_t	*texture;
	int			flags;
};


struct msurface_t
{
	// if this layout is changed then D3DSurf_TransferIndexes must also be changed
	// data needed to draw the surface
	int firstvertex;
	int numvertexes;
	int numindexes;

	// allocated on hunk during world building
	void *indexes;

	mtexinfo_t	*texinfo;

	int			LightmapTextureNum;

	// other stuff
	int			visframe;		// should be drawn when node is crossed

	struct model_t *model;
	mplane_t	*plane;
	int			flags;

	int			firstedge;

	msurface_t *texturechain;

	short		texturemins[2];
	short		extents[2];

	// changed to ints for the new larger lightmap sizes
	// note - even shorts are too small for the new max surface extents
	int			smax, tmax;			// lightmap extents (width and height) (relative to LIGHTMAP_SIZE, not 0 to 1)

	// rectangle specifying the surface lightmap
	D3D11_BOX	LightBox;

	// lighting info
	int			dlightframe;
	byte		dlightbits[(MAX_DLIGHTS + 7) >> 3];

	// lighting parameters may change in which case the lightmap is rebuilt for this surface
	int			LightProperties;

	// extents of the surf in world space
	cullinfo_t	cullinfo;

	byte		styles[MAX_SURFACE_STYLES];
	int			cached_light[MAX_SURFACE_STYLES];	// values currently used in lightmap

	byte		*samples;		// [numstyles*surfsize]

	// for alpha sorting
	float		midpoint[3];
};


// yayy C++
struct nodeleafcommon_t
{
	int			contents;		// 0, to differentiate from leafs
	int			visframe;		// node needs to be traversed if current
	cullinfo_t	cullinfo;
	struct mnode_t	*parent;
	int			num;
	int			flags;
};


struct mnode_t : nodeleafcommon_t
{
	// node specific
	float		dot;
	int			side;
	mplane_t	*plane;
	struct mnode_t	*children[2];
	msurface_t	*surfaces;
	int	numsurfaces;
};



struct mleaf_t : nodeleafcommon_t
{
	// leaf specific
	byte		*compressed_vis;
	msurface_t	**firstmarksurface;
	int			nummarksurfaces;
	struct efrag_t	*efrags;	// client-side for static entities
	// int			sv_visframe;
	// int			key;			// BSP sequence number for leaf's contents
	float		ambient_sound_level[NUM_AMBIENTS];

	// contents colour for cshifts
	int *contentscolor;
};

// !!! if this is changed, it must be changed in asm_i386.h too !!!
struct hull_t
{
	mclipnode_t	*clipnodes;
	mplane_t	*planes;
	int			firstclipnode;
	int			lastclipnode;
	vec3_t		clip_mins;
	vec3_t		clip_maxs;
	float		sphere[4];
};

float Mod_PlaneDist (mplane_t *plane, float *point);

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/


// FIXME: shorten these?
struct mspriteframe_t
{
	int		framenum;	// index of total frames including framegroup numbers
	int		width;
	int		height;
	float	up, down, left, right;
	int		firstvertex;
	class QTEXTURE *texture;
};

struct mspritegroup_t
{
	int				numframes;
	float			*intervals;
	mspriteframe_t	*frames[1];
};

struct mspriteframedesc_t
{
	spriteframetype_t	type;
	mspriteframe_t		*frameptr;
};

struct msprite_t
{
	int					type;
	int					version;
	int					maxwidth;
	int					maxheight;
	int					numframes;
	int					totalframes;	// including group frames
	float				beamlength;		// remove?
	void				*cachespot;		// remove?
	mspriteframedesc_t	frames[1];
};


/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

struct maliasframedesc_t
{
	int					firstpose;
	int					numposes;
	float				*intervals;
	trivertx_t			bboxmin;
	trivertx_t			bboxmax;
	int					frame;
	char				name[16];
};

struct aliasmesh_t
{
	unsigned short st[2];
	unsigned short vertindex;
	bool facesfront;
};

struct aliasskin_t
{
	int numskins;
	aliasskintype_t type;
	float *intervals;
	class QTEXTURE **cmapimage;	// player skins only
	class QTEXTURE **teximage;
	class QTEXTURE **lumaimage;
};


struct aliasbbox_t
{
	float mins[3];
	float maxs[3];
	float sphere[4];
};


// temp struct for loading stuff
struct aliasload_t
{
	// loaded from disk
	stvert_t *stverts;
	dtriangle_t *triangles;
	struct drawvertx_t	**vertexes;

	// generated by the mesh compression/optimization code
	aliasmesh_t *mesh;
	unsigned short *indexes;
};


struct aliashdr_t
{
	vec3_t		scale;
	vec3_t		scale_origin;

	// for gl_doubleeyes
	float		midpoint[3];

	float		boundingradius;
	synctype_t	synctype;
	unsigned int drawflags;
	float		size;

	int			nummeshframes;
	int			numtris;

	int			vertsperframe;
	int			numframes;

	int			buffer;

	int			nummesh;
	int			numindexes;

	maliasframedesc_t	*frames;
	aliasbbox_t			*bboxes;

	int			skinwidth;
	int			skinheight;
	int			numskins;
	aliasskin_t *skins;
};


//===================================================================

// Whole model
typedef enum {mod_brush, mod_sprite, mod_alias, mod_iqm, mod_null} modtype_t;
typedef enum {mod_world, mod_inline, mod_bsp} modsubtype_t;

#define	EF_ROCKET		1			// leave a trail
#define	EF_GRENADE		2			// leave a trail
#define	EF_GIB			4			// leave a trail
#define	EF_ROTATE		8			// rotate (bonus items)
#define	EF_WIZARDTRAIL	16			// green split trail
#define	EF_ZOMGIB		32			// small blood trail
#define	EF_KNIGHTTRAIL	64			// orange split trail + rotate
#define	EF_VORETRAIL	128			// purple trail

// mh - special flags
#define EF_PLAYER			(1 << 21)

// bad guy muzzle flashes
#define EF_WIZARDFLASH		(1 << 22)
#define EF_SHALRATHFLASH	(1 << 23)
#define EF_SHAMBLERFLASH	(1 << 24)

// generic flashes (the first 3 of these line up with the id bad guys)
#define EF_GREENFLASH		(1 << 22)
#define EF_PURPLEFLASH		(1 << 23)
#define EF_BLUEFLASH		(1 << 24)
#define EF_ORANGEFLASH		(1 << 25)
#define EF_REDFLASH			(1 << 26)
#define EF_YELLOWFLASH		(1 << 27)


struct brushhdr_t
{
	int			firstmodelsurface;
	int			nummodelsurfaces;

	int			numsubmodels;
	dmodel_t	*submodels;

	int			numplanes;
	mplane_t	*planes;

	int			numleafs;		// number of visible leafs, not counting 0
	mleaf_t		*leafs;

	int			numnodes;
	mnode_t		*nodes;

	int			numtexinfo;
	mtexinfo_t	*texinfo;

	int			numsurfaces;
	msurface_t	*surfaces;

	int			numclipnodes;
	mclipnode_t	*clipnodes;

	int			nummarksurfaces;
	msurface_t	**marksurfaces;

	hull_t		hulls[MAX_MAP_HULLS];

	int			numtextures;
	texture_t	**textures;

	byte		*lightdata;
	char		*entities;

	int			numedges;
	medge_t		*edges;

	// loaded directly from disk with no intermediate processing
	dvertex_t	*dvertexes;
	int			*dsurfedges;

	int			numsurfvertexes;

	// 29 (Q1) or 30 (HL)
	int			bspversion;

	// true if it's a BSP bmodel (but not the world)
	bool		bspmodel;

	// bounding box used for rendering with
	float		bmins[3];
	float		bmaxs[3];
	float		sphere[4];
};


struct model_t
{
	char	name[MAX_QPATH];
	bool	needload;

	// number of entities using this model in this frame
	int		numents;

	modtype_t	type;
	modsubtype_t	subtype;
	int			numframes;
	synctype_t	synctype;

	int			flags;

	// volume occupied by the model graphics
	// alias models normally check this per frame rather than for the entire model;
	// this is retained for compatibility with anything server-side that sill uses it
	vec3_t		mins, maxs;
	float		sphere[4];
	float		radius;

	// solid volume for clipping
	bool	clipbox;
	vec3_t	clipmins, clipmaxs;
	float		clipsphere[4];

	// brush/alias/sprite headers
	// this gets a LOT of polluting data OUT of the model_t struct
	// could turn these into a union... bit it makes accessing them a bit more awkward...
	// besides, it's only an extra 8 bytes per model... chickenfeed, really...
	// it also allows us to be more robust by setting the others to NULL in the loader
	brushhdr_t	*brushhdr;
	aliashdr_t	*aliashdr;
	msprite_t	*spritehdr;
	struct iqmdata_t *iqmheader;

	// will be == d3d_RenderDef.RegistrationSequence is this model was touched on this map load
	int RegistrationSequence;
};

//============================================================================

void	Mod_Init (void);
void	Mod_ClearAll (void);
model_t *Mod_ForName (char *name, bool crash);
void	Mod_TouchModel (char *name);

// this can be greater than MAX_MODELS if an alias model is in the cache
#define	MAX_MOD_KNOWN	8192

extern model_t	**mod_known;
extern int mod_numknown;

mleaf_t *Mod_PointInLeaf (float *p, model_t *model);
void Mod_SphereFromBounds (float *mins, float *maxs, float *sphere);
byte	*Mod_LeafPVS (mleaf_t *leaf, model_t *model);
byte *Mod_FatPVS (vec3_t org);

// handles frame and skin group auto-animations
int Mod_AnimateGroup (entity_t *ent, float *intervals, int numintervals);

#endif	// __MODEL__
