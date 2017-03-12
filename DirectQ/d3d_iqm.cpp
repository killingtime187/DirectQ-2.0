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
#include "iqm.h"
#include "resource.h"

QEDICTLIST d3d_IQMEdicts;

// beware of row-major/column-major differences.  i use column-major thoughout this code with the exception of
// D3DXMatrixTransformation which generates a row-major matrix and needs to be transposed (i've wrapped it to
// do this and get rid of some unused params too

float RadiusFromBounds (vec3_t mins, vec3_t maxs);

void QMatrixTransformation (D3DXMATRIX *m, D3DXVECTOR3 *s, D3DXQUATERNION *r, D3DXVECTOR3 *t)
{
	D3DXMatrixTransformation (m, NULL, NULL, s, NULL, r, t);
	D3DXMatrixTranspose (m, m);
}


void IQM_LoadVertexes (iqmheader_t *iqm, iqmdata_t *iqmdata)
{
	float *vposition = NULL, *vtexcoord = NULL, *vnormal = NULL;
	unsigned char *vblendindexes = NULL, *vblendweights = NULL;
	byte *buf = (byte *) iqm;
	iqmvertexarray_t *va = (iqmvertexarray_t *) (buf + iqm->ofs_vertexarrays);

	for (int i = 0; i < iqm->num_vertexarrays; i++)
	{
		switch (va[i].type)
		{
		case IQM_POSITION:
			if (va[i].format == IQM_FLOAT && va[i].size == 3)
				vposition = (float *) (buf + va[i].offset);
			break;

		case IQM_TEXCOORD:
			if (va[i].format == IQM_FLOAT && va[i].size == 2)
				vtexcoord = (float *) (buf + va[i].offset);
			break;

		case IQM_NORMAL:
			if (va[i].format == IQM_FLOAT && va[i].size == 3)
				vnormal = (float *) (buf + va[i].offset);
			break;

		case IQM_BLENDINDEXES:
			if (va[i].format == IQM_UBYTE && va[i].size == 4)
				vblendindexes = (unsigned char *) (buf + va[i].offset);
			break;

		case IQM_BLENDWEIGHTS:
			if (va[i].format == IQM_UBYTE && va[i].size == 4)
				vblendweights = (unsigned char *) (buf + va[i].offset);
			break;
		}
	}

	if (!vposition || !vtexcoord || !vnormal || !vblendindexes || !vblendweights)
	{
		Sys_Error ("IQM_LoadVertexes : incomplete model or limits exceeded");
		return;
	}

	// load vertex data
	iqmdata->verts = (iqmvertex_t *) MainHunk->Alloc (iqmdata->numvertexes * sizeof (iqmvertex_t));

	for (int i = 0; i < iqmdata->numvertexes; i++, vposition += 3, vnormal += 3, vtexcoord += 2, vblendindexes += 4, vblendweights += 4)
	{
		for (int j = 0; j < 3; j++)
		{
			iqmdata->verts[i].position[j] = vposition[j];
			iqmdata->verts[i].normal[j] = vnormal[j];
		}

		iqmdata->verts[i].texcoord[0] = vtexcoord[0];
		iqmdata->verts[i].texcoord[1] = vtexcoord[1];

		for (int j = 0; j < 4; j++)
		{
			iqmdata->verts[i].blendindexes[j] = vblendindexes[j];
			iqmdata->verts[i].blendweights[j] = vblendweights[j];
		}
	}
}


void IQM_LoadV1Joints (iqmheader_t *iqm, iqmdata_t *iqmdata, D3DXMATRIX *baseframe, D3DXMATRIX *inversebaseframe)
{
	byte *buf = (byte *) iqm;

	// load the joints
	iqmdata->jointsv1 = (iqmjointv1_t *) MainHunk->Alloc (iqm->num_joints * sizeof (iqmjointv1_t));
	Q_MemCpy (iqmdata->jointsv1, (buf + iqm->ofs_joints), iqm->num_joints * sizeof (iqmjointv1_t));

	for (int i = 0; i < (int) iqm->num_joints; i++)
	{
		iqmjointv1_t *j = &iqmdata->jointsv1[i];

		// first need to make a vec4 quat from our rotation vec
		D3DXVECTOR3 rot (j->rotate[0], j->rotate[1], j->rotate[2]);
		D3DXQUATERNION q_rot (j->rotate[0], j->rotate[1], j->rotate[2], -sqrt (max (1.0 - pow (D3DXVec3Length (&rot), 2), 0.0)));
		D3DXVECTOR3 vscale (j->scale);
		D3DXVECTOR3 vtrans (j->translate);

		QMatrixTransformation (&baseframe[i], &vscale, &q_rot, &vtrans);
		D3DXMatrixInverse (&inversebaseframe[i], NULL, &baseframe[i]);

		if (j->parent >= 0)
		{
			baseframe[i] = baseframe[j->parent] * baseframe[i];
			inversebaseframe[i] = inversebaseframe[i] * inversebaseframe[j->parent];
		}
	}
}


void IQM_LoadV2Joints (iqmheader_t *iqm, iqmdata_t *iqmdata, D3DXMATRIX *baseframe, D3DXMATRIX *inversebaseframe)
{
	byte *buf = (byte *) iqm;

	// load the joints
	iqmdata->jointsv2 = (iqmjointv2_t *) MainHunk->Alloc (iqm->num_joints * sizeof (iqmjointv2_t));
	Q_MemCpy (iqmdata->jointsv2, (buf + iqm->ofs_joints), iqm->num_joints * sizeof (iqmjointv2_t));

	for (int i = 0; i < (int) iqm->num_joints; i++)
	{
		iqmjointv2_t *j = &iqmdata->jointsv2[i];
		D3DXVECTOR3 vscale (j->scale);
		D3DXQUATERNION qrot (j->rotate);
		D3DXVECTOR3 vtrans (j->translate);

		QMatrixTransformation (&baseframe[i], &vscale, &qrot, &vtrans);
		D3DXMatrixInverse (&inversebaseframe[i], NULL, &baseframe[i]);

		if (j->parent >= 0)
		{
			baseframe[i] = baseframe[j->parent] * baseframe[i];
			inversebaseframe[i] = inversebaseframe[i] * inversebaseframe[j->parent];
		}
	}
}


void IQM_LoadV1Poses (iqmheader_t *iqm, iqmdata_t *iqmdata, D3DXMATRIX *baseframe, D3DXMATRIX *inversebaseframe)
{
	byte *buf = (byte *) iqm;
	iqmposev1_t *posesv1 = (iqmposev1_t *) (buf + iqm->ofs_poses);
	unsigned short *framedata = (unsigned short *) (buf + iqm->ofs_frames);
	float posevecs[9];
	D3DXMATRIX m;

	for (int i = 0; i < iqm->num_frames; i++)
	{
		for (int j = 0; j < iqm->num_poses; j++)
		{
			iqmposev1_t p = posesv1[j];

			for (int trs = 0; trs < 9; trs++)
			{
				posevecs[trs] = p.channeloffset[trs];
				if (p.mask & (1 << trs)) posevecs[trs] += *framedata++ * p.channelscale[trs];
			}

			D3DXVECTOR3 vscale (&posevecs[6]);
			D3DXVECTOR3 vrot (&posevecs[3]);
			D3DXQUATERNION qrot (posevecs[3], posevecs[4], posevecs[5], -sqrt (max (1.0 - pow (D3DXVec3Length (&vrot), 2), 0.0)));
			D3DXVECTOR3 vtrans (posevecs);

			QMatrixTransformation (&m, &vscale, &qrot, &vtrans);

			if (p.parent >= 0)
				iqmdata->frames[i * iqm->num_poses + j] = (baseframe[p.parent] * m) * inversebaseframe[j];
			else iqmdata->frames[i * iqm->num_poses + j] = m * inversebaseframe[j];
		}
	}
}


void IQM_LoadV2Poses (iqmheader_t *iqm, iqmdata_t *iqmdata, D3DXMATRIX *baseframe, D3DXMATRIX *inversebaseframe)
{
	byte *buf = (byte *) iqm;
	iqmposev2_t *posesv2 = (iqmposev2_t *) (buf + iqm->ofs_poses);
	unsigned short *framedata = (unsigned short *) (buf + iqm->ofs_frames);
	float posevecs[10];
	D3DXMATRIX m;

	for (int i = 0; i < iqm->num_frames; i++)
	{
		for (int j = 0; j < iqm->num_poses; j++)
		{
			iqmposev2_t p = posesv2[j];

			for (int trs = 0; trs < 10; trs++)
			{
				posevecs[trs] = p.channeloffset[trs];
				if (p.mask & (1 << trs)) posevecs[trs] += *framedata++ * p.channelscale[trs];
			}

			D3DXVECTOR3 vscale (&posevecs[7]);
			D3DXQUATERNION qrot (&posevecs[3]);
			D3DXVECTOR3 vtrans (posevecs);

			QMatrixTransformation (&m, &vscale, &qrot, &vtrans);

			if (p.parent >= 0)
				iqmdata->frames[i * iqm->num_poses + j] = (baseframe[p.parent] * m) * inversebaseframe[j];
			else iqmdata->frames[i * iqm->num_poses + j] = m * inversebaseframe[j];
		}
	}
}


void IQM_LoadJoints (iqmheader_t *iqm, iqmdata_t *iqmdata)
{
	int hunkmark = TempHunk->GetLowMark ();

	// these don't need to be a part of mod
	D3DXMATRIX *baseframe = (D3DXMATRIX *) TempHunk->FastAlloc (iqm->num_joints * sizeof (D3DXMATRIX));
	D3DXMATRIX *inversebaseframe = (D3DXMATRIX *) TempHunk->FastAlloc (iqm->num_joints * sizeof (D3DXMATRIX));

	if (iqm->version == IQM_VERSION1)
		IQM_LoadV1Joints (iqm, iqmdata, baseframe, inversebaseframe);
	else IQM_LoadV2Joints (iqm, iqmdata, baseframe, inversebaseframe);

	if (!iqm->num_poses || !iqm->num_frames)
		iqmdata->frames = NULL;
	else
	{
		iqmdata->frames = (D3DXMATRIX *) MainHunk->Alloc (iqm->num_frames * iqm->num_poses * sizeof (D3DXMATRIX));

		if (iqm->version == IQM_VERSION1)
			IQM_LoadV1Poses (iqm, iqmdata, baseframe, inversebaseframe);
		else IQM_LoadV2Poses (iqm, iqmdata, baseframe, inversebaseframe);
	}

	TempHunk->FreeToLowMark (hunkmark);
}


void IQM_LoadBounds (iqmheader_t *iqm, iqmdata_t *iqmdata, model_t *mod)
{
	byte *buf = (byte *) iqm;

	// load bounding box data
	if (iqm->ofs_bounds)
	{
		float xyradius = 0, radius = 0;
		iqmbounds_t *bounds = (iqmbounds_t *) (buf + iqm->ofs_bounds);

		Mod_ClearBoundingBox (iqmdata->mins, iqmdata->maxs);

		// we're only using one frame so we only use one bounding box for the entire model
		// we'll still check 'em all just to be precise though
		for (int i = 0; i < (int) iqm->num_frames; i++)
		{
			Mod_AccumulateBox (iqmdata->mins, iqmdata->maxs, bounds[i].bbmin, bounds[i].bbmax);

			if (bounds[i].xyradius > xyradius) xyradius = bounds[i].xyradius;
			if (bounds[i].radius > radius) radius = bounds[i].radius;
		}
	}
	else
	{
		// no bounds so just take it from the vertexes
		Mod_ClearBoundingBox (iqmdata->mins, iqmdata->maxs);

		for (int i = 0; i < iqmdata->numvertexes; i++)
			Mod_AccumulateBox (iqmdata->mins, iqmdata->maxs, iqmdata->verts[i].position);
	}

	for (int i = 0; i < 3; i++)
	{
		// this bbox is used server-side for collisions so it should be clamped
		if (iqmdata->mins[i] > -16) mod->mins[i] = -16; else mod->mins[i] = iqmdata->mins[i];
		if (iqmdata->maxs[i] < 16) mod->maxs[i] = 16; else mod->maxs[i] = iqmdata->maxs[i];
	}
}


void IQM_LoadTriangles (iqmheader_t *iqm, iqmdata_t *iqmdata)
{
	byte *buf = (byte *) iqm;
	int *inelements;

	// load triangle data - here we compress down to unsigned short because OpenGL weenies are unaware of how hardware actually works
	inelements = (int *) (buf + iqm->ofs_triangles);
	iqmdata->tris = (iqmtriangle_t *) MainHunk->Alloc (iqm->num_triangles * sizeof (iqmtriangle_t));

	for (int i = 0; i < (int) iqm->num_triangles; i++)
	{
		iqmdata->tris[i].vertex[0] = inelements[0];
		iqmdata->tris[i].vertex[1] = inelements[1];
		iqmdata->tris[i].vertex[2] = inelements[2];
		inelements += 3;
	}
}


void IQM_LoadMesh (iqmheader_t *iqm, iqmdata_t *iqmdata, char *path)
{
	if (!path) return;
	if (!iqm) return;
	if (!iqmdata) return;

	byte *buf = (byte *) iqm;
	char *str = (char *) &buf[iqm->ofs_text];
	iqmmesh_t *mesh = (iqmmesh_t *) (buf + iqm->ofs_meshes);

	// lead meshes
	iqmdata->num_mesh = iqm->num_meshes;
	iqmdata->numindexes = 0;
	iqmdata->mesh = (iqmmesh_t *) MainHunk->Alloc (iqmdata->num_mesh * sizeof (iqmmesh_t));
	iqmdata->skins = (QTEXTURE **) MainHunk->Alloc (iqmdata->num_mesh * sizeof (QTEXTURE *));
	iqmdata->fullbrights = (QTEXTURE **) MainHunk->Alloc (iqmdata->num_mesh * sizeof (QTEXTURE *));

	for (int i = 0; i < iqmdata->num_mesh; i++)
	{
		char texturepath[1024];
		int hunkmark = TempHunk->GetLowMark ();

		// no more endian weenieness here
		Q_MemCpy (&iqmdata->mesh[i], &mesh[i], sizeof (iqmmesh_t));
		iqmdata->numindexes += mesh[i].num_triangles * 3;

		// build the base texture path from the model directory and the material defined in the IQM
		strcpy (texturepath, path);

		for (int j = strlen (texturepath); j; j--)
		{
			if (texturepath[j] == '/' || texturepath[j] == '\\')
			{
				strcpy (&texturepath[j + 1], &str[iqmdata->mesh[i].material]);
				break;
			}
		}

		COM_StripExtension (texturepath, texturepath);

		iqmdata->skins[i] = QTEXTURE::Load (texturepath, 0, 0, NULL, IMAGE_32BIT | IMAGE_MIPMAP | IMAGE_IQM);
		iqmdata->fullbrights[i] = QTEXTURE::Load (texturepath, 0, 0, NULL, IMAGE_32BIT | IMAGE_MIPMAP | IMAGE_LUMA | IMAGE_IQM);

		Con_DPrintf ("texture %i %s\n", i, texturepath);

		TempHunk->FreeToLowMark (hunkmark);
	}
}


void Mod_LoadIQMModel (model_t *mod, void *buffer, char *path)
{
	iqmheader_t *hdr = (iqmheader_t *) buffer;
	int hunkmark = TempHunk->GetLowMark ();

	if (strcmp (hdr->magic, IQM_MAGIC)) goto IQM_LoadError;
	if (hdr->version != IQM_VERSION1 && hdr->version != IQM_VERSION2) goto IQM_LoadError;

	// just needs to be something that's not mod_iqm so that we can test for success
	mod->type = mod_alias;

	iqmdata_t *iqmdata = (iqmdata_t *) MainHunk->Alloc (sizeof (iqmdata_t));

	// set these up-front in case anything in the loaders need them
	iqmdata->num_frames = hdr->num_anims;
	iqmdata->num_joints = hdr->num_joints;
	iqmdata->num_poses = hdr->num_frames;
	iqmdata->numvertexes = hdr->num_vertexes;
	iqmdata->num_triangles = hdr->num_triangles;

	IQM_LoadVertexes (hdr, iqmdata);
	IQM_LoadJoints (hdr, iqmdata);
	IQM_LoadBounds (hdr, iqmdata, mod);
	IQM_LoadTriangles (hdr, iqmdata);
	IQM_LoadMesh (hdr, iqmdata, path);

	// store the version so that we know how to animate it
	iqmdata->version = hdr->version;

	// loaded OK
	mod->iqmheader = iqmdata;
	mod->flags = hdr->flags;

	// this signifies success
	mod->type = mod_iqm;

	TempHunk->FreeToLowMark (hunkmark);

	if (mod->type == mod_iqm) return;

IQM_LoadError:;
	Sys_Error ("Mod_LoadIQMModel : failed to load %s\n", path);
}


bool Mod_FindIQMModel (model_t *mod)
{
	char iqmname[MAX_PATH];
	iqmheader_t *hdr = NULL;
	int hunkmark = TempHunk->GetLowMark ();

	// attempt q2 naming convention
	COM_StripExtension (mod->name, iqmname);
	strcat (iqmname, "/tris.iqm");

	if ((hdr = (iqmheader_t *) CQuakeFile::LoadFile (iqmname, TempHunk)) != NULL)
	{
		Mod_LoadIQMModel (mod, hdr, iqmname);
		TempHunk->FreeToLowMark (hunkmark);
		return true;
	}

	// fallback on direct replace
	COM_StripExtension (mod->name, iqmname);
	COM_DefaultExtension (iqmname, ".iqm");

	if ((hdr = (iqmheader_t *) CQuakeFile::LoadFile (iqmname, TempHunk)) != NULL)
	{
		Mod_LoadIQMModel (mod, hdr, iqmname);
		TempHunk->FreeToLowMark (hunkmark);
		return true;
	}

	return false;
}


struct iqmbuffer_t
{
	ID3D11Buffer *Vertexes;
	ID3D11Buffer *Indexes;
};

struct iqmjointrow_t
{
	D3DXVECTOR4 data1[256];
	D3DXVECTOR4 data2[256];
	D3DXVECTOR4 data3[256];
};


iqmbuffer_t d3d_IQMBuffers[MAX_MOD_KNOWN];

ID3D11Buffer *d3d_IQMJointRows = NULL;
ID3D11InputLayout *d3d_IQMLayout = NULL;
ID3D11VertexShader *d3d_IQMVertexShader = NULL;


void D3DIQM_Init (void)
{
	if (!d3d_IQMJointRows)
		BufferFactory.CreateConstantBuffer (sizeof (iqmjointrow_t), &d3d_IQMJointRows, "d3d_IQMJointRows");

	// this is used as a condition for everything else
	if (!d3d_IQMLayout)
	{
		D3D11_INPUT_ELEMENT_DESC iqmlo[] =
		{
			MAKELAYOUTELEMENT ("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0),
			MAKELAYOUTELEMENT ("NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0),
			MAKELAYOUTELEMENT ("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0),
			MAKELAYOUTELEMENT ("BINDEX",   0, DXGI_FORMAT_R8G8B8A8_UINT,   0, 0),
			MAKELAYOUTELEMENT ("BWEIGHT",  0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 0)
		};

		QSHADERFACTORY ShaderFactory (IDR_ALIASFX);

		ShaderFactory.CreateVertexShader (&d3d_IQMVertexShader, "IQMVS");
		ShaderFactory.CreateInputLayout (&d3d_IQMLayout, "d3d_IQMLayout", LAYOUTPARAMS (iqmlo));
	}

	model_t *mod = NULL;

	for (int i = 0; i < MAX_MOD_KNOWN; i++)
	{
		SAFE_RELEASE (d3d_IQMBuffers[i].Vertexes);
		SAFE_RELEASE (d3d_IQMBuffers[i].Indexes);

		// nothing allocated yet
		if (!mod_known) continue;
		if (!(mod = mod_known[i])) continue;
		if (mod->type != mod_iqm) continue;

		int hunkmark = TempHunk->GetLowMark ();
		iqmdata_t *hdr = mod->iqmheader;
		int numindexes = 0;

		for (int m = 0; m < hdr->num_mesh; m++)
			numindexes += hdr->mesh[m].num_triangles * 3;

		unsigned short *ndx = (unsigned short *) TempHunk->FastAlloc (numindexes * sizeof (unsigned short));
		unsigned short *basendx = ndx;

		for (int m = 0; m < hdr->num_mesh; m++)
		{
			Q_MemCpy (ndx, &hdr->tris[hdr->mesh[m].first_triangle], 3 * hdr->mesh[m].num_triangles * sizeof (unsigned short));
			ndx += hdr->mesh[m].num_triangles * 3;
		}

		BufferFactory.CreateIndexBuffer (sizeof (unsigned short), numindexes, &d3d_IQMBuffers[i].Indexes, va ("&d3d_IQMBuffers[%i].Indexes", i), basendx);
		BufferFactory.CreateVertexBuffer (sizeof (iqmvertex_t), hdr->numvertexes, &d3d_IQMBuffers[i].Vertexes, va ("&d3d_IQMBuffers[%i].Vertexes", i), hdr->verts);

		TempHunk->FreeToLowMark (hunkmark);

		// and this is the buffer set that this model will use
		hdr->buffernum = i;
	}
}


void D3DIQM_Shutdown (void)
{
	for (int i = 0; i < MAX_MOD_KNOWN; i++)
	{
		SAFE_RELEASE (d3d_IQMBuffers[i].Vertexes);
		SAFE_RELEASE (d3d_IQMBuffers[i].Indexes);
	}

	SAFE_RELEASE (d3d_IQMJointRows);
}


// no on-init handler because it's called just-in-time when a map using iqms is loaded
CD3DInitShutdownHandler d3d_IQMHandler ("iqm", D3DIQM_Init, D3DIQM_Shutdown);

// shared code with alias model renderer
void D3DLight_LightPoint (lightinfo_t *info, float *origin);
void D3DAlias_TransformStandard (entity_t *ent);
void D3DAlias_TransformShadowed (entity_t *ent);
void D3DAlias_UpdateConstants (entity_t *ent);
void D3DAlias_SetShadersAndTextures (entity_t *ent, QTEXTURE *teximage, QTEXTURE *lumaimage, int flags);
extern ID3D11Buffer *d3d_MeshConstants;


// fixme - iqm with no joints?
void D3DIQM_AnimateFrame (iqmdata_t *hdr, int lastframe, int currframe, float lerp)
{
	// temp memory for building the animation (we need to keep this for the entire model as subsequent
	// joints may reference prior parent joints in the list)
	int hunkmark = TempHunk->GetLowMark ();
	D3DXMATRIX *outframe = (D3DXMATRIX *) TempHunk->FastAlloc (hdr->num_joints * sizeof (D3DXMATRIX));
	iqmjointrow_t *rows = (iqmjointrow_t *) TempHunk->FastAlloc (sizeof (iqmjointrow_t));

	D3DXVECTOR4 *outvecs1 = rows->data1;
	D3DXVECTOR4 *outvecs2 = rows->data2;
	D3DXVECTOR4 *outvecs3 = rows->data3;

	// this memory should never be referenced if there are no frames or poses
	D3DXMATRIX *mat1 = (!hdr->num_poses || !hdr->num_frames) ? NULL : &hdr->frames[(lastframe % hdr->num_poses) * hdr->num_joints];
	D3DXMATRIX *mat2 = (!hdr->num_poses || !hdr->num_frames) ? NULL : &hdr->frames[(currframe % hdr->num_poses) * hdr->num_joints];

	// and build the animation
	for (int i = 0; i < hdr->num_joints; i++)
	{
		// the joints were unioned and parent is in the same memory location so this is valid to do
		if (!hdr->num_poses || !hdr->num_frames)
			D3DXMatrixIdentity (&outframe[i]);
		else if (hdr->jointsv2[i].parent >= 0)
			outframe[i] = outframe[hdr->jointsv2[i].parent] * (mat1[i] + ((mat2[i] - mat1[i]) * lerp));
		else outframe[i] = mat1[i] + ((mat2[i] - mat1[i]) * lerp);

		// copy out to 3xvec4 for more space in the constants (otherwise we'd just use a matrix array)
		outvecs1[i] = D3DXVECTOR4 (outframe[i].m[0]);
		outvecs2[i] = D3DXVECTOR4 (outframe[i].m[1]);
		outvecs3[i] = D3DXVECTOR4 (outframe[i].m[2]);
	}

	d3d11_Context->UpdateSubresource (d3d_IQMJointRows, 0, NULL, rows, 0, 0);

	TempHunk->FreeToLowMark (hunkmark);
}


void D3DIQM_DrawFullMesh (iqmdata_t *hdr, entity_t *ent, QTEXTURE **teximages, QTEXTURE **lumaimages, int flags)
{
	for (int i = 0; i < hdr->num_mesh; i++)
	{
		D3DAlias_SetShadersAndTextures (ent, teximages ? teximages[i] : NULL, lumaimages ? lumaimages[i] : NULL, flags);
		D3DMisc_DrawIndexedCommon (hdr->mesh[i].num_triangles * 3, hdr->mesh[i].first_triangle * 3);
		d3d_RenderDef.alias_polys += hdr->mesh[i].num_triangles;
	}
}


void D3DIQM_DrawIQM (entity_t *ent)
{
	model_t *mod = ent->model;
	iqmdata_t *hdr = mod->iqmheader;
	extern QMATRIX r_shadowmatrix;

	d3d11_State->VSSetShader (d3d_IQMVertexShader);
	d3d11_State->PSSetSampler (0, d3d_DefaultSamplerClamp);
	d3d11_State->PSSetSampler (1, d3d_DefaultSamplerClamp);

	d3d11_State->IASetInputLayout (d3d_IQMLayout);
	d3d11_State->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	d3d11_State->IASetIndexBuffer (d3d_IQMBuffers[hdr->buffernum].Indexes, DXGI_FORMAT_R16_UINT, 0);
	d3d11_State->IASetVertexBuffer (0, d3d_IQMBuffers[hdr->buffernum].Vertexes, sizeof (iqmvertex_t), 0);
	d3d11_State->VSSetConstantBuffer (2, d3d_IQMJointRows);

	D3DLight_LightPoint (&ent->lightinfo, ent->lerporigin);
	D3DAlias_TransformStandard (ent);
	D3DAlias_UpdateConstants (ent);

	D3DIQM_AnimateFrame (hdr, ent->prev.pose, ent->curr.pose, ent->poselerp.blend);
	D3DIQM_DrawFullMesh (hdr, ent, hdr->skins, hdr->fullbrights, AM_IQM);

	// no shadows on alpha IQMs
	if (ent->alphaval > 0 && ent->alphaval < 255) return;

	// just draw shadows in-place while we have joint transforms up and loaded
	// yeah it's state changes, but the alternative is to rebuild and reload the joints
	if (r_shadows.value > 0 && ent != &cl.viewent)
	{
		// different transform for shadowing
		D3DAlias_TransformShadowed (ent);
		D3DAlias_UpdateConstants (ent);
		d3d11_State->OMSetBlendState (d3d_AlphaBlendEnable);
		d3d11_State->OMSetDepthStencilState (d3d_ShadowStencil, 0x00000001);

		D3DIQM_DrawFullMesh (hdr, ent, NULL, NULL, AM_DRAWSHADOW);

		d3d11_State->OMSetDepthStencilState (d3d_DepthTestAndWrite);
		d3d11_State->OMSetBlendState (NULL);
	}
}


void D3DIQM_DrawIQMs (void)
{
	for (int i = 0; i < d3d_IQMEdicts.NumEdicts; i++)
	{
		entity_t *ent = d3d_IQMEdicts.Edicts[i];

		if (!ent->model) continue;
		if (ent->model->type != mod_iqm) continue;

		// we must always have at least one valid skin
		if (!ent->model->iqmheader->skins[0]) continue;

		if (R_CullBox (&ent->cullinfo))
		{
			ent->lerpflags |= LERP_RESETALL;
			continue;
		}

		// mark as visible (primarily for bbox drawing)
		ent->visframe = d3d_RenderDef.framecount;

		if (ent->alphaval > 0 && ent->alphaval < 255)
		{
			D3DAlpha_AddToList (ent);
			continue;
		}

		D3DIQM_DrawIQM (ent);
	}
}

