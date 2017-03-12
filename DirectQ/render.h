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

// refresh.h -- public interface to refresh functions

struct efrag_t
{
	struct mleaf_t		*leaf;
	struct efrag_t		*leafnext;
	struct entity_t		*entity;
	struct efrag_t		*entnext;
};


#define	TOP_RANGE		16			// soldier uniform colors
#define	BOTTOM_RANGE	96

//=============================================================================


#define LERP_MOVESTEP		(1 << 0) // this is a MOVETYPE_STEP entity, enable movement lerp
#define LERP_RESETFRAME		(1 << 1) // disable lerping until next frame
#define LERP_RESETORIGIN	(1 << 2) // disable lerping until next frame
#define LERP_RESETANGLES	(1 << 3) // disable lerping until next frame
#define LERP_FINISH			(1 << 4) // use lerpfinish time from server update instead of assuming interval of 0.1
#define LERP_RESETALL		(LERP_RESETFRAME | LERP_RESETORIGIN | LERP_RESETANGLES)
#define LERP_RESETMOVE		(LERP_RESETORIGIN | LERP_RESETANGLES)

void D3DMisc_PositionFromBBox (float *position, float *mins, float *maxs);

struct lerpstate_t
{
	float msg_origin[3];
	float msg_angles[3];

	float origin[3];
	float angles[3];

	int pose;

	double msgtime;
};


struct lerpinfo_t
{
	double starttime;
	float blend;
};


struct cullinfo_t
{
	float mins[3];
	float maxs[3];
	int cullplane;
	int clipflags;
};


struct lightinfo_t
{
	// for r_lightpoint lighting and shadows
	struct msurface_t *lightsurf;
	float lightspot[3];
	struct mplane_t *lightplane;

	// lightdata offsets
	int ds;
	int dt;

	// final data
	float		shadelight[3];
	float		shadevector[3];

	// special-case flags
	int flags;
};


struct entity_t
{
	cullinfo_t			cullinfo;
	vec3_t				bboxscale;

	bool				forcelink;		// model changed

	entity_state_t			baseline;		// to fill in defaults in updates

	vec3_t					oldorg;			// for particle spawning

	vec3_t					origin;
	vec3_t					angles;
	struct model_t			*model;			// NULL = no model
	struct efrag_t			*efrag;
	int						frame;
	float					syncbase;		// for client-side animations
	int						effects;		// light, particals, etc
	int						skinnum;		// for Alias models
	int						visframe;		// last frame this entity was found in an active leaf
	int						relinkframe;	// static entities only; frame when added to the visedicts list
	vec3_t					modelorg;		// relative to r_origin

	// player skins
	int						playerskin;

	// allocated at runtime client and server side
	int						entnum;

	// sort order for MDLs
	union
	{
		unsigned int sortorder;
		unsigned short sortpose[2];
	};

	lerpstate_t	curr;
	lerpstate_t prev;

	// interpolation
	lerpinfo_t	poselerp;
	lerpinfo_t	originlerp;
	lerpinfo_t	angleslerp;

	// for movetype_step entities we interpolate position to sync with the pose
	float		lerporigin[3];
	float		lerpangles[3];

	// allows an alpha value to be assigned to any entity
	int			alphaval;
	float		lerpinterval;
	int			lerpflags;

	// light for MDLs
	lightinfo_t	lightinfo;

	// false if the entity is to be subjected to bbox culling
	bool		nocullbox;

	// distance from client (for depth sorting)
	float		dist;

	bool isStatic;

	// the matrix used for transforming this entity
	QMATRIX			matrix;
	QMATRIX			invmatrix;

	// flags for drawing brush models to avoid unneeded state changes for different surface types
	int		BrushDrawFlags;

	// textures used to draw alias models
	class QTEXTURE *teximage;
	class QTEXTURE *lumaimage;
	class QTEXTURE *cmapimage;
};


struct refdef_t
{
	vec3_t	vieworigin;
	vec3_t	viewangles;
};


extern	refdef_t	r_refdef;

extern QMATRIX r_viewvectors;

extern	struct texture_t	*r_notexture_mip;


void R_Init (void);
void R_NewMap (void);


