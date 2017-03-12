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


// mathlib.h

void Q_sincos (float angradians, float *angsin, float *angcos);
long Q_ftol (float f);

#define min2(a, b) ((a) < (b) ? (a) : (b))
#define min3(a, b, c) (min2 ((a), min2 ((b), (c))))

#define max2(a, b) ((a) > (b) ? (a) : (b))
#define max3(a, b, c) (max2 ((a), max2 ((b), (c))))

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];

extern vec3_t vec3_origin;
extern	int nanmask;

#define	IS_NAN(x) (((*(int *) &x) & nanmask) == nanmask)

// obvious that there needs to be some cleaner refactoring going on here...
void Vector2Copy (float *dst, float *src);
void Vector2Set (float *vec, float x, float y);
void Vector2Clear (float *vec);
void Vector2Add (float *dst, float *vec1, float add);
void Vector2Add (float *dst, float *vec1, float *vec2);
void Vector2Subtract (float *dst, float *vec1, float sub);
void Vector2Subtract (float *dst, float *vec1, float *vec2);
void Vector2Lerp (float *dst, float *l1, float *l2, float b);
float Vector2Dot (float *x, float *y);
void Vector2Scale (float *dst, float *vec, float scale);
void Vector2Scale (float *dst, float *vec, float *scale);
void Vector2Recip (float *dst, float *vec, float scale);
void Vector2Recip (float *dst, float *vec, float *scale);
void Vector2Mad (float *out, float *vec, float scale, float *add);
void Vector2Mad (float *out, float *vec, float *scale, float *add);
float Vector2Length (float *v);
float Vector2Normalize (float *v);
void Vector2Cross (float *cross, float *v1, float *v2);
void Vector2Clamp (float *vec, float clmp);
bool Vector2Compare (float *v1, float *v2);

void Vector3Copy (float *dst, float *src);
void Vector3Set (float *vec, float x, float y, float z);
void Vector3Clear (float *vec);
void Vector3Add (float *dst, float *vec1, float add);
void Vector3Add (float *dst, float *vec1, float *vec2);
void Vector3Subtract (float *dst, float *vec1, float sub);
void Vector3Subtract (float *dst, float *vec1, float *vec2);
void Vector3Lerp (float *dst, float *l1, float *l2, float b);
float Vector3Dot (float *x, float *y);
void Vector3Scale (float *dst, float *vec, float scale);
void Vector3Scale (float *dst, float *vec, float *scale);
void Vector3Recip (float *dst, float *vec, float scale);
void Vector3Recip (float *dst, float *vec, float *scale);
void Vector3Mad (float *out, float *vec, float scale, float *add);
void Vector3Mad (float *out, float *vec, float *scale, float *add);
float Vector3Length (float *v);
float Vector3Normalize (float *v);
void Vector3Cross (float *cross, float *v1, float *v2);
void Vector3Clamp (float *vec, float clmp);
bool Vector3Compare (float *v1, float *v2);

void Vector4Copy (float *dst, float *src);
void Vector4Set (float *vec, float x, float y, float z, float w);
void Vector4Clear (float *vec);
void Vector4Add (float *dst, float *vec1, float add);
void Vector4Add (float *dst, float *vec1, float *vec2);
void Vector4Subtract (float *dst, float *vec1, float sub);
void Vector4Subtract (float *dst, float *vec1, float *vec2);
void Vector4Lerp (float *dst, float *l1, float *l2, float b);
float Vector4Dot (float *x, float *y);
void Vector4Scale (float *dst, float *vec, float scale);
void Vector4Scale (float *dst, float *vec, float *scale);
void Vector4Recip (float *dst, float *vec, float scale);
void Vector4Recip (float *dst, float *vec, float *scale);
void Vector4Mad (float *out, float *vec, float scale, float *add);
void Vector4Mad (float *out, float *vec, float *scale, float *add);
float Vector4Length (float *v);
float Vector4Normalize (float *v);
void Vector4Cross (float *cross, float *v1, float *v2);
void Vector4Clamp (float *vec, float clmp);
bool Vector4Compare (float *v1, float *v2);


#define BOX_INSIDE_PLANE	1
#define BOX_OUTSIDE_PLANE	2
#define BOX_INTERSECT_PLANE	3

float anglemod (float a);

#define CLAMP(minimum, x, maximum) ((x) < (minimum) ? (minimum) : (x) > (maximum) ? (maximum) : (x))
#define Q_rint(x) ((x) > 0 ? (int)((x) + 0.5) : (int)((x) - 0.5))
#define Q_Random(MIN,MAX) ((Q_fastrand () & 32767) * (((MAX) - (MIN)) * (1.0f / 32767.0f)) + (MIN))

int BoxOnPlaneSide (float *emins, float *emaxs, struct mplane_t *plane);

#define BOX_ON_PLANE_SIDE(emins, emaxs, p)		\
	(((p)->type < 3) ?							\
	(											\
		((p)->dist <= (emins)[(p)->type]) ?		\
		1										\
		:										\
		(										\
			((p)->dist >= (emaxs)[(p)->type]) ?	\
			2									\
			:									\
			3									\
		)										\
	)											\
	:											\
	BoxOnPlaneSide ((emins), (emaxs), (p)))


void Q_randseed (int seed);
int Q_fastrand (void);


