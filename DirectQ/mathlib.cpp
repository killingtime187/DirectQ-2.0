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
// mathlib.c -- math primitives

#include "versions.h"

#include <math.h>
#include "quakedef.h"
#include "d3d_model.h"

void Sys_Error (char *error, ...);

vec3_t vec3_origin = {0, 0, 0};
int nanmask = 255 << 23;


/*-----------------------------------------------------------------*/


float anglemod (float a)
{
	a = (360.0 / 65536) * ((int) (a * (65536 / 360.0)) & 65535);
	return a;
}


void Vector2Mad (float *out, float *vec, float scale, float *add)
{
	out[0] = vec[0] * scale + add[0];
	out[1] = vec[1] * scale + add[1];
}


void Vector2Mad (float *out, float *vec, float *scale, float *add)
{
	out[0] = vec[0] * scale[0] + add[0];
	out[1] = vec[1] * scale[1] + add[1];
}


void Vector3Mad (float *out, float *vec, float scale, float *add)
{
	out[0] = vec[0] * scale + add[0];
	out[1] = vec[1] * scale + add[1];
	out[2] = vec[2] * scale + add[2];
}


void Vector3Mad (float *out, float *vec, float *scale, float *add)
{
	out[0] = vec[0] * scale[0] + add[0];
	out[1] = vec[1] * scale[1] + add[1];
	out[2] = vec[2] * scale[2] + add[2];
}


void Vector4Mad (float *out, float *vec, float scale, float *add)
{
	out[0] = vec[0] * scale + add[0];
	out[1] = vec[1] * scale + add[1];
	out[2] = vec[2] * scale + add[2];
	out[3] = vec[3] * scale + add[3];
}


void Vector4Mad (float *out, float *vec, float *scale, float *add)
{
	out[0] = vec[0] * scale[0] + add[0];
	out[1] = vec[1] * scale[1] + add[1];
	out[2] = vec[2] * scale[2] + add[2];
	out[3] = vec[3] * scale[3] + add[3];
}


void Vector2Scale (float *dst, float *vec, float scale)
{
	dst[0] = vec[0] * scale;
	dst[1] = vec[1] * scale;
}


void Vector2Scale (float *dst, float *vec, float *scale)
{
	dst[0] = vec[0] * scale[0];
	dst[1] = vec[1] * scale[1];
}


void Vector3Scale (float *dst, float *vec, float scale)
{
	dst[0] = vec[0] * scale;
	dst[1] = vec[1] * scale;
	dst[2] = vec[2] * scale;
}


void Vector3Scale (float *dst, float *vec, float *scale)
{
	dst[0] = vec[0] * scale[0];
	dst[1] = vec[1] * scale[1];
	dst[2] = vec[2] * scale[2];
}


void Vector4Scale (float *dst, float *vec, float scale)
{
	dst[0] = vec[0] * scale;
	dst[1] = vec[1] * scale;
	dst[2] = vec[2] * scale;
	dst[3] = vec[3] * scale;
}


void Vector4Scale (float *dst, float *vec, float *scale)
{
	dst[0] = vec[0] * scale[0];
	dst[1] = vec[1] * scale[1];
	dst[2] = vec[2] * scale[2];
	dst[3] = vec[3] * scale[3];
}


void Vector2Recip (float *dst, float *vec, float scale)
{
	dst[0] = vec[0] / scale;
	dst[1] = vec[1] / scale;
}


void Vector2Recip (float *dst, float *vec, float *scale)
{
	dst[0] = vec[0] / scale[0];
	dst[1] = vec[1] / scale[1];
}


void Vector3Recip (float *dst, float *vec, float scale)
{
	dst[0] = vec[0] / scale;
	dst[1] = vec[1] / scale;
	dst[2] = vec[2] / scale;
}


void Vector3Recip (float *dst, float *vec, float *scale)
{
	dst[0] = vec[0] / scale[0];
	dst[1] = vec[1] / scale[1];
	dst[2] = vec[2] / scale[2];
}


void Vector4Recip (float *dst, float *vec, float scale)
{
	dst[0] = vec[0] / scale;
	dst[1] = vec[1] / scale;
	dst[2] = vec[2] / scale;
	dst[3] = vec[3] / scale;
}


void Vector4Recip (float *dst, float *vec, float *scale)
{
	dst[0] = vec[0] / scale[0];
	dst[1] = vec[1] / scale[1];
	dst[2] = vec[2] / scale[2];
	dst[3] = vec[3] / scale[3];
}


void Vector2Copy (float *dst, float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
}


void Vector3Copy (float *dst, float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}


void Vector4Copy (float *dst, float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = src[3];
}


void Vector2Add (float *dst, float *vec1, float add)
{
	dst[0] = vec1[0] + add;
	dst[1] = vec1[1] + add;
}


void Vector2Add (float *dst, float *vec1, float *vec2)
{
	dst[0] = vec1[0] + vec2[0];
	dst[1] = vec1[1] + vec2[1];
}


void Vector2Subtract (float *dst, float *vec1, float sub)
{
	dst[0] = vec1[0] - sub;
	dst[1] = vec1[1] - sub;
}


void Vector2Subtract (float *dst, float *vec1, float *vec2)
{
	dst[0] = vec1[0] - vec2[0];
	dst[1] = vec1[1] - vec2[1];
}


void Vector3Add (float *dst, float *vec1, float add)
{
	dst[0] = vec1[0] + add;
	dst[1] = vec1[1] + add;
	dst[2] = vec1[2] + add;
}


void Vector3Add (float *dst, float *vec1, float *vec2)
{
	dst[0] = vec1[0] + vec2[0];
	dst[1] = vec1[1] + vec2[1];
	dst[2] = vec1[2] + vec2[2];
}


void Vector3Subtract (float *dst, float *vec1, float sub)
{
	dst[0] = vec1[0] - sub;
	dst[1] = vec1[1] - sub;
	dst[2] = vec1[2] - sub;
}


void Vector3Subtract (float *dst, float *vec1, float *vec2)
{
	dst[0] = vec1[0] - vec2[0];
	dst[1] = vec1[1] - vec2[1];
	dst[2] = vec1[2] - vec2[2];
}


void Vector4Add (float *dst, float *vec1, float add)
{
	dst[0] = vec1[0] + add;
	dst[1] = vec1[1] + add;
	dst[2] = vec1[2] + add;
	dst[3] = vec1[3] + add;
}


void Vector4Add (float *dst, float *vec1, float *vec2)
{
	dst[0] = vec1[0] + vec2[0];
	dst[1] = vec1[1] + vec2[1];
	dst[2] = vec1[2] + vec2[2];
	dst[3] = vec1[3] + vec2[3];
}


void Vector4Subtract (float *dst, float *vec1, float sub)
{
	dst[0] = vec1[0] - sub;
	dst[1] = vec1[1] - sub;
	dst[2] = vec1[2] - sub;
	dst[3] = vec1[3] - sub;
}


void Vector4Subtract (float *dst, float *vec1, float *vec2)
{
	dst[0] = vec1[0] - vec2[0];
	dst[1] = vec1[1] - vec2[1];
	dst[2] = vec1[2] - vec2[2];
	dst[3] = vec1[3] - vec2[3];
}


float Vector2Dot (float *x, float *y)
{
	return (x[0] * y[0]) + (x[1] * y[1]);
}


float Vector3Dot (float *x, float *y)
{
	return (x[0] * y[0]) + (x[1] * y[1]) + (x[2] * y[2]);
}


float Vector4Dot (float *x, float *y)
{
	return (x[0] * y[0]) + (x[1] * y[1]) + (x[2] * y[2]) + (x[3] * y[3]);
}


void Vector2Lerp (float *dst, float *l1, float *l2, float b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b;
	dst[1] = l1[1] + (l2[1] - l1[1]) * b;
}


void Vector3Lerp (float *dst, float *l1, float *l2, float b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b;
	dst[1] = l1[1] + (l2[1] - l1[1]) * b;
	dst[2] = l1[2] + (l2[2] - l1[2]) * b;
}


void Vector4Lerp (float *dst, float *l1, float *l2, float b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b;
	dst[1] = l1[1] + (l2[1] - l1[1]) * b;
	dst[2] = l1[2] + (l2[2] - l1[2]) * b;
	dst[3] = l1[3] + (l2[3] - l1[3]) * b;
}


void Vector2Set (float *vec, float x, float y)
{
	vec[0] = x;
	vec[1] = y;
}


void Vector2Clear (float *vec)
{
	vec[0] = vec[1] = 0.0f;
}


void Vector3Set (float *vec, float x, float y, float z)
{
	vec[0] = x;
	vec[1] = y;
	vec[2] = z;
}


void Vector3Clear (float *vec)
{
	vec[0] = vec[1] = vec[2] = 0.0f;
}


void Vector4Set (float *vec, float x, float y, float z, float w)
{
	vec[0] = x;
	vec[1] = y;
	vec[2] = z;
	vec[3] = w;
}


void Vector4Clear (float *vec)
{
	vec[0] = vec[1] = vec[2] = vec[3] = 0.0f;
}


void Vector2Clamp (float *vec, float clmp)
{
	if (vec[0] > clmp) vec[0] = clmp;
	if (vec[1] > clmp) vec[1] = clmp;
}


void Vector3Clamp (float *vec, float clmp)
{
	if (vec[0] > clmp) vec[0] = clmp;
	if (vec[1] > clmp) vec[1] = clmp;
	if (vec[2] > clmp) vec[2] = clmp;
}


void Vector4Clamp (float *vec, float clmp)
{
	if (vec[0] > clmp) vec[0] = clmp;
	if (vec[1] > clmp) vec[1] = clmp;
	if (vec[2] > clmp) vec[2] = clmp;
	if (vec[3] > clmp) vec[3] = clmp;
}


void Vector2Cross (float *cross, float *v1, float *v2)
{
	Sys_Error ("Just what do you think you're doing, Dave?");
}


void Vector3Cross (float *cross, float *v1, float *v2)
{
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}


void Vector4Cross (float *cross, float *v1, float *v2)
{
	Sys_Error ("Just what do you think you're doing, Dave?");
}


float sqrt (float x);


float Vector2Length (float *v)
{
	return sqrt (Vector2Dot (v, v));
}


float Vector3Length (float *v)
{
	return sqrt (Vector3Dot (v, v));
}


float Vector4Length (float *v)
{
	return sqrt (Vector4Dot (v, v));
}


float Vector2Normalize (float *v)
{
	float length = Vector2Dot (v, v);

	if ((length = sqrt (length)) > 0)
	{
		float ilength = 1 / length;

		v[0] *= ilength;
		v[1] *= ilength;
	}

	return length;
}


float Vector3Normalize (float *v)
{
	float length = Vector3Dot (v, v);

	if ((length = sqrt (length)) > 0)
	{
		float ilength = 1 / length;

		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}

	return length;
}


float Vector4Normalize (float *v)
{
	float length = Vector4Dot (v, v);

	if ((length = sqrt (length)) > 0)
	{
		float ilength = 1 / length;

		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
		v[3] *= ilength;
	}

	return length;
}


bool Vector2Compare (float *v1, float *v2)
{
	if (v1[0] != v2[0]) return false;
	if (v1[1] != v2[1]) return false;

	return true;
}


bool Vector3Compare (float *v1, float *v2)
{
	if (v1[0] != v2[0]) return false;
	if (v1[1] != v2[1]) return false;
	if (v1[2] != v2[2]) return false;

	return true;
}


bool Vector4Compare (float *v1, float *v2)
{
	if (v1[0] != v2[0]) return false;
	if (v1[1] != v2[1]) return false;
	if (v1[2] != v2[2]) return false;
	if (v1[3] != v2[3]) return false;

	return true;
}


void Q_sincos (float angradians, float *angsin, float *angcos)
{
	// bleagh - the inline asm version of this fucks up in release builds
	*angsin = sin (angradians);
	*angcos = cos (angradians);
}


__declspec (naked) long Q_ftol (float f)
{
	static int tmp;

	__asm fld dword ptr [esp + 4]
	__asm fistp tmp
	__asm mov eax, tmp
	__asm ret
}


int R_PlaneSide (cullinfo_t *ci, mplane_t *p);

int BoxOnPlaneSide (float *emins, float *emaxs, mplane_t *p)
{
	cullinfo_t cull;

	Vector3Copy (cull.mins, emins);
	Vector3Copy (cull.maxs, emaxs);

	cull.cullplane = -1;

	return R_PlaneSide (&cull, p);
}


int random_seed = 0;

// return 32 bit random number
// modified standard fastrand that takes the previous seed plus a new one to generate a real 32-bit number
// over 2.5 times as fast as CRT rand in a simulation of 1.2 million calls per frame
int Q_fastrand (void)
{
	int r1 = (random_seed >> 16) & 0x7fff;

	random_seed = (214013 * random_seed) + 2531011;

	return (r1 | (((random_seed >> 16) & 0x7fff) << 16));
}


void Q_randseed (int seed)
{
	// standard CRT rand is used to seed the fastrand which then takes over
	srand (seed);
	random_seed = rand ();
}

