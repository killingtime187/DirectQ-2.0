
/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

/*
============================================================================================================

		MATRIX OPS

	These happen in place on the matrix and update it's current values.  Wherever possible OpenGL-like
	functionality is replicated.

	Why the fuck these ain't in the D3DXMATRIX class I'll never know...

============================================================================================================
*/

QMATRIX::QMATRIX (void)
{
	this->Identity ();
}

QMATRIX::QMATRIX (const QMATRIX *other)
{
	this->Load (other);
}

QMATRIX::QMATRIX (float _11, float _12, float _13, float _14,
				  float _21, float _22, float _23, float _24,
				  float _31, float _32, float _33, float _34,
				  float _41, float _42, float _43, float _44)
{
	this->_11 = _11; this->_12 = _12; this->_13 = _13; this->_14 = _14;
	this->_21 = _21; this->_22 = _22; this->_23 = _23; this->_24 = _24;
	this->_31 = _31; this->_32 = _32; this->_33 = _33; this->_34 = _34;
	this->_41 = _41; this->_42 = _42; this->_43 = _43; this->_44 = _44;
}

QMATRIX::QMATRIX (const float *m)
{
	memcpy (this->m16, m, sizeof (float) * 16);
}


void QMATRIX::Identity (void)
{
	D3DXMatrixIdentity (this);
}

void QMATRIX::PutZGoingUp (void)
{
	// just swap the matrix so that we preserve fp as much as possible
	QMATRIX that (this);

	this->m16[0] = -that.m16[4];
	this->m16[1] = -that.m16[5];
	this->m16[2] = -that.m16[6];

	this->m16[4] = that.m16[8];
	this->m16[5] = that.m16[9];
	this->m16[6] = that.m16[10];

	this->m16[8] = -that.m16[0];
	this->m16[9] = -that.m16[1];
	this->m16[10] = -that.m16[2];
}

void QMATRIX::Translate (float x, float y, float z)
{
	this->m4x4[3][0] += x * this->m4x4[0][0] + y * this->m4x4[1][0] + z * this->m4x4[2][0];
	this->m4x4[3][1] += x * this->m4x4[0][1] + y * this->m4x4[1][1] + z * this->m4x4[2][1];
	this->m4x4[3][2] += x * this->m4x4[0][2] + y * this->m4x4[1][2] + z * this->m4x4[2][2];
	this->m4x4[3][3] += x * this->m4x4[0][3] + y * this->m4x4[1][3] + z * this->m4x4[2][3];
}

void QMATRIX::Translate (const float *xyz)
{
	this->m4x4[3][0] += xyz[0] * this->m4x4[0][0] + xyz[1] * this->m4x4[1][0] + xyz[2] * this->m4x4[2][0];
	this->m4x4[3][1] += xyz[0] * this->m4x4[0][1] + xyz[1] * this->m4x4[1][1] + xyz[2] * this->m4x4[2][1];
	this->m4x4[3][2] += xyz[0] * this->m4x4[0][2] + xyz[1] * this->m4x4[1][2] + xyz[2] * this->m4x4[2][2];
	this->m4x4[3][3] += xyz[0] * this->m4x4[0][3] + xyz[1] * this->m4x4[1][3] + xyz[2] * this->m4x4[2][3];
}

void QMATRIX::Rotate (float x, float y, float z, float angle)
{
	D3DXMATRIX m;
	D3DXVECTOR3 vec (x, y, z);

	D3DXMatrixRotationAxis (&m, &vec, D3DXToRadian (angle));
	this->Mult (&m);
}

void QMATRIX::Scale (float x, float y, float z)
{
	Vector4Scale (this->m4x4[0], this->m4x4[0], x);
	Vector4Scale (this->m4x4[1], this->m4x4[1], y);
	Vector4Scale (this->m4x4[2], this->m4x4[2], z);
}

void QMATRIX::Scale (const float *xyz)
{
	Vector4Scale (this->m4x4[0], this->m4x4[0], xyz[0]);
	Vector4Scale (this->m4x4[1], this->m4x4[1], xyz[1]);
	Vector4Scale (this->m4x4[2], this->m4x4[2], xyz[2]);
}

void QMATRIX::OrthoOffCenterRH (float l, float r, float b, float t, float zn, float zf)
{
	D3DXMATRIX m;

	D3DXMatrixOrthoOffCenterRH (&m, l, r, b, t, zn, zf);
	this->Mult (&m);
}

void QMATRIX::Projection (float fovx, float fovy, float zn, float zf)
{
	float Q = -(zf / (zf - zn));	// flip to RH
	float fovxradians = (fovx * D3DX_PI) / 360.0f;	// equivalent to D3DXToRadian (fovx) / 2 but preserves precision a little better
	float fovyradians = (fovy * D3DX_PI) / 360.0f;	// equivalent to D3DXToRadian (fovy) / 2 but preserves precision a little better

	// and construct our projection matrix
	QMATRIX m (1.0f / tan (fovxradians), 0, 0, 0, 0, 1.0f / tan (fovyradians), 0, 0, 0, 0, Q, -1, 0, 0, (Q * zn), 0);

	this->Mult (&m);
}

void QMATRIX::PerspectiveFovRH (float fovy, float Aspect, float zn, float zf)
{
	D3DXMATRIX m;

	D3DXMatrixPerspectiveFovRH (&m, D3DXToRadian (fovy), Aspect, zn, zf);
	this->Mult (&m);
}

void QMATRIX::Mult (D3DXMATRIX *out, const D3DXMATRIX *in)
{
	out->m[0][0] = in->m[0][0] * this->m[0][0] + in->m[0][1] * this->m[1][0] + in->m[0][2] * this->m[2][0] + in->m[0][3] * this->m[3][0];
	out->m[0][1] = in->m[0][0] * this->m[0][1] + in->m[0][1] * this->m[1][1] + in->m[0][2] * this->m[2][1] + in->m[0][3] * this->m[3][1];
	out->m[0][2] = in->m[0][0] * this->m[0][2] + in->m[0][1] * this->m[1][2] + in->m[0][2] * this->m[2][2] + in->m[0][3] * this->m[3][2];
	out->m[0][3] = in->m[0][0] * this->m[0][3] + in->m[0][1] * this->m[1][3] + in->m[0][2] * this->m[2][3] + in->m[0][3] * this->m[3][3];

	out->m[1][0] = in->m[1][0] * this->m[0][0] + in->m[1][1] * this->m[1][0] + in->m[1][2] * this->m[2][0] + in->m[1][3] * this->m[3][0];
	out->m[1][1] = in->m[1][0] * this->m[0][1] + in->m[1][1] * this->m[1][1] + in->m[1][2] * this->m[2][1] + in->m[1][3] * this->m[3][1];
	out->m[1][2] = in->m[1][0] * this->m[0][2] + in->m[1][1] * this->m[1][2] + in->m[1][2] * this->m[2][2] + in->m[1][3] * this->m[3][2];
	out->m[1][3] = in->m[1][0] * this->m[0][3] + in->m[1][1] * this->m[1][3] + in->m[1][2] * this->m[2][3] + in->m[1][3] * this->m[3][3];

	out->m[2][0] = in->m[2][0] * this->m[0][0] + in->m[2][1] * this->m[1][0] + in->m[2][2] * this->m[2][0] + in->m[2][3] * this->m[3][0];
	out->m[2][1] = in->m[2][0] * this->m[0][1] + in->m[2][1] * this->m[1][1] + in->m[2][2] * this->m[2][1] + in->m[2][3] * this->m[3][1];
	out->m[2][2] = in->m[2][0] * this->m[0][2] + in->m[2][1] * this->m[1][2] + in->m[2][2] * this->m[2][2] + in->m[2][3] * this->m[3][2];
	out->m[2][3] = in->m[2][0] * this->m[0][3] + in->m[2][1] * this->m[1][3] + in->m[2][2] * this->m[2][3] + in->m[2][3] * this->m[3][3];

	out->m[3][0] = in->m[3][0] * this->m[0][0] + in->m[3][1] * this->m[1][0] + in->m[3][2] * this->m[2][0] + in->m[3][3] * this->m[3][0];
	out->m[3][1] = in->m[3][0] * this->m[0][1] + in->m[3][1] * this->m[1][1] + in->m[3][2] * this->m[2][1] + in->m[3][3] * this->m[3][1];
	out->m[3][2] = in->m[3][0] * this->m[0][2] + in->m[3][1] * this->m[1][2] + in->m[3][2] * this->m[2][2] + in->m[3][3] * this->m[3][2];
	out->m[3][3] = in->m[3][0] * this->m[0][3] + in->m[3][1] * this->m[1][3] + in->m[3][2] * this->m[2][3] + in->m[3][3] * this->m[3][3];
}


void QMATRIX::Mult (const D3DXMATRIX *in)
{
	D3DXMatrixMultiply (this, in, this);
}

void QMATRIX::Mult (float _11, float _12, float _13, float _14, float _21, float _22, float _23, float _24, float _31, float _32, float _33, float _34, float _41, float _42, float _43, float _44)
{
	QMATRIX tmp (_11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44);
	this->Mult (&tmp);
}


void QMATRIX::Mult (const float *m)
{
	QMATRIX tmp (m);
	this->Mult (&tmp);
}


void QMATRIX::Load (float _11, float _12, float _13, float _14, float _21, float _22, float _23, float _24, float _31, float _32, float _33, float _34, float _41, float _42, float _43, float _44)
{
	this->_11 = _11; this->_12 = _12; this->_13 = _13; this->_14 = _14;
	this->_21 = _21; this->_22 = _22; this->_23 = _23; this->_24 = _24;
	this->_31 = _31; this->_32 = _32; this->_33 = _33; this->_34 = _34;
	this->_41 = _41; this->_42 = _42; this->_43 = _43; this->_44 = _44;
}

void QMATRIX::Load (const float *m)
{
	memcpy (this->m16, m, sizeof (float) * 16);
}

void QMATRIX::Load (const D3DXMATRIX *in)
{
	Q_MemCpy (this->m, in->m, sizeof (in->m));
}

void QMATRIX::TransformPoint (float *out, const float *in)
{
	D3DXVECTOR3 vin (in);
	D3DXVECTOR3 vout (out);

	D3DXVec3TransformCoord (&vout, &vin, this);
	out[0] = vout.x; out[1] = vout.y; out[2] = vout.z;
}

void QMATRIX::UpdateMVP (QMATRIX *mvp, const QMATRIX *m, const QMATRIX *p)
{
	mvp->Load (p);
	mvp->Mult (m);
}

void QMATRIX::AngleVectors (const float *angles)
{
	// this should be re-expressible as a rotation matrix
	float sr, sp, sy, cr, cp, cy;

	Q_sincos (D3DXToRadian (angles[1]), &sy, &cy);
	Q_sincos (D3DXToRadian (angles[0]), &sp, &cp);
	Q_sincos (D3DXToRadian (angles[2]), &sr, &cr);

	this->Identity ();

	this->fw[0] = cp * cy;
	this->fw[1] = cp * sy;
	this->fw[2] = -sp;

	this->rt[0] = (-1 * sr * sp * cy) + (-1 * cr * -sy);
	this->rt[1] = (-1 * sr * sp * sy) + (-1 * cr * cy);
	this->rt[2] = -1 * sr * cp;

	this->up[0] = (cr * sp * cy) + (-sr * -sy);
	this->up[1] = (cr * sp * sy) + (-sr * cy);
	this->up[2] = cr * cp;
}


void QMATRIX::Rotate (float y, float p, float r)
{
	float sr, sp, sy, cr, cp, cy;

	Q_sincos (D3DXToRadian (y), &sy, &cy);
	Q_sincos (D3DXToRadian (p), &sp, &cp);
	Q_sincos (D3DXToRadian (r), &sr, &cr);

	QMATRIX m (
		(cp * cy),
		(cp * sy),
		-sp,
		0.0f,
		(cr * -sy) + (sr * sp * cy),
		(cr * cy) + (sr * sp * sy),
		(sr * cp),
		0.0f,
		(sr * sy) + (cr * sp * cy),
		(-sr * cy) + (cr * sp * sy),
		(cr * cp),
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		1.0f
	);

	this->Mult (&m);
}


void QMATRIX::Transpose (D3DXMATRIX *dest)
{
	dest->m[0][0] = this->m[0][0]; dest->m[0][1] = this->m[1][0]; dest->m[0][2] = this->m[2][0]; dest->m[0][3] = this->m[3][0];
	dest->m[1][0] = this->m[0][1]; dest->m[1][1] = this->m[1][1]; dest->m[1][2] = this->m[2][1]; dest->m[1][3] = this->m[3][1];
	dest->m[2][0] = this->m[0][2]; dest->m[2][1] = this->m[1][2]; dest->m[2][2] = this->m[2][2]; dest->m[2][3] = this->m[3][2];
	dest->m[3][0] = this->m[0][3]; dest->m[3][1] = this->m[1][3]; dest->m[3][2] = this->m[2][3]; dest->m[3][3] = this->m[3][3];
}

void QMATRIX::Transpose (void)
{
	for (int i = 0; i < 4; i++)
	{
		for (int j = i + 1; j < 4; j++)
		{
			float save = this->m[i][j];
			this->m[i][j] = this->m[j][i];
			this->m[j][i] = save;
		}
	}
}

void QMATRIX::CopyTo (D3DXMATRIX *dest)
{
	Q_MemCpy (dest, this, sizeof (D3DXMATRIX));
}

void QMATRIX::ExtractFrustum (struct mplane_t *f)
{
	// front plane is tested first for faster rejection
	// should we just skip testing of the back plane???
	// front
	f[0].normal[0] = this->_14 + this->_13;
	f[0].normal[1] = this->_24 + this->_23;
	f[0].normal[2] = this->_34 + this->_33;

	// right
	f[1].normal[0] = this->_14 - this->_11;
	f[1].normal[1] = this->_24 - this->_21;
	f[1].normal[2] = this->_34 - this->_31;

	// left
	f[2].normal[0] = this->_14 + this->_11;
	f[2].normal[1] = this->_24 + this->_21;
	f[2].normal[2] = this->_34 + this->_31;

	// bottom
	f[3].normal[0] = this->_14 + this->_12;
	f[3].normal[1] = this->_24 + this->_22;
	f[3].normal[2] = this->_34 + this->_32;

	// top
	f[4].normal[0] = this->_14 - this->_12;
	f[4].normal[1] = this->_24 - this->_22;
	f[4].normal[2] = this->_34 - this->_32;

	Vector3Normalize (f[0].normal);
	Vector3Normalize (f[1].normal);
	Vector3Normalize (f[2].normal);
	Vector3Normalize (f[3].normal);
	Vector3Normalize (f[4].normal);
}

void QMATRIX::Inverse (D3DXMATRIX *other)
{
	D3DXMatrixInverse (other, NULL, this);
}

void QMATRIX::Inverse (void)
{
	D3DXMatrixInverse (this, NULL, this);
}


