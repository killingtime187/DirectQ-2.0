/*
Copyright (C) 1996-1997 Id Software, Inc.
Shader code (C) 2009-2010 MH

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

cbuffer cbPerObject : register(b1)
{
	float3 r_vpn : packoffset(c0.x);
	float distscale : packoffset(c0.w);
	float partscale : packoffset(c1.x);
	float sv_gravity : packoffset(c1.y);
	float cltime : packoffset(c1.z);
	float lightscale : packoffset(c1.w);
	float4 r_voffsets[4] : packoffset(c2);
};


struct VS_PARTICLE
{
	float3 Pos : POSITION;
	float3 Vel : VEL;
	float2 dVel : DVEL;	// swizzled as .xxy
	float Time : TIME;
	float Grav : GRAV;
	float Scale : SCALE;
	uint Ramp : RAMP;
	float RampTime : RAMPTIME;
	float BaseRamp : BASERAMP;
	float4 Color : PARTCOLOR;
	float Die : DIE;
	uint VertexID : SV_VERTEXID;
};


struct PS_PARTICLE
{
	float4 Pos : SV_POSITION;
	float4 Color : DRAWCOLOR;
	float2 Tex : TEXCOORD0;
};


float4 PartPS (PS_PARTICLE ps_in) : SV_TARGET0
{
	float4 color = ps_in.Color;

#if !defined (PARTSHADER_SQUARE)
#if defined (PARTSHADER_CORONA)
	color.a = 1.0f - dot (ps_in.Tex.xy, ps_in.Tex.xy);
	color.a = pow (color.a, 3.0f);
#else
	color.a *= (1.0f - dot (ps_in.Tex.xy, ps_in.Tex.xy)) * 1.5f;
#endif
#endif

#if defined (PARTSHADER_CORONA)
	// coronas don't get fogged owing to the different blend mode they use
	return color;
#elif defined (PARTSHADER_FOG)
	return FogCalc (color, ps_in.Pos.w);
#else
	return color;
#endif
}


static const float2 parttexcoords[4] = {float2 (-1, -1), float2 (1, -1), float2 (-1, 1), float2 (1, 1)};


// particle ramps
// the first ramp is just a dummy and is unused, this lets me test if (p->ramp) and index into this array based on a non-zero value of p->ramp
// ramps are padded out to extra so that the arrays won't overflow if time stalls
static const float4 particleramps[4][12] =
{
	{
		float4 (0.000000, 0.000000, 0.000000, 0.000000), float4 (0.000000, 0.000000, 0.000000, 0.000000), float4 (0.000000, 0.000000, 0.000000, 0.000000),
		float4 (0.000000, 0.000000, 0.000000, 0.000000), float4 (0.000000, 0.000000, 0.000000, 0.000000), float4 (0.000000, 0.000000, 0.000000, 0.000000),
		float4 (0.000000, 0.000000, 0.000000, 0.000000), float4 (0.000000, 0.000000, 0.000000, 0.000000), float4 (0.000000, 0.000000, 0.000000, 0.000000),
		float4 (0.000000, 0.000000, 0.000000, 0.000000), float4 (0.000000, 0.000000, 0.000000, 0.000000), float4 (0.000000, 0.000000, 0.000000, 0.000000)
	},
	{
		float4 (1.000000, 0.949219, 0.105469, 1.000000), float4 (1.000000, 0.949219, 0.105469, 1.000000), float4 (0.871094, 0.667969, 0.152344, 1.000000),
		float4 (0.746094, 0.464844, 0.183594, 1.000000), float4 (0.621094, 0.308594, 0.199219, 1.000000), float4 (0.496094, 0.230469, 0.167969, 1.000000),
		float4 (0.386719, 0.183594, 0.121094, 1.000000), float4 (0.292969, 0.136719, 0.074219, 1.000000), float4 (0.183594, 0.089844, 0.042969, 1.000000),
		float4 (0.183594, 0.089844, 0.042969, 0.000000), float4 (0.183594, 0.089844, 0.042969, 0.000000), float4 (0.183594, 0.089844, 0.042969, 0.000000)
	},
	{
		float4 (1.000000, 0.949219, 0.105469, 1.000000), float4 (1.000000, 0.949219, 0.105469, 1.000000), float4 (0.933594, 0.792969, 0.121094, 1.000000),
		float4 (0.871094, 0.667969, 0.152344, 1.000000), float4 (0.808594, 0.558594, 0.167969, 1.000000), float4 (0.746094, 0.464844, 0.183594, 1.000000),
		float4 (0.683594, 0.386719, 0.183594, 1.000000), float4 (0.558594, 0.261719, 0.199219, 1.000000), float4 (0.449219, 0.214844, 0.136719, 1.000000),
		float4 (0.449219, 0.214844, 0.136719, 0.000000), float4 (0.449219, 0.214844, 0.136719, 0.000000), float4 (0.449219, 0.214844, 0.136719, 0.000000)
	},
	{
		float4 (0.871094, 0.667969, 0.152344, 1.000000), float4 (0.871094, 0.667969, 0.152344, 1.000000), float4 (0.746094, 0.464844, 0.183594, 1.000000),
		float4 (0.355469, 0.355469, 0.355469, 1.000000), float4 (0.292969, 0.292969, 0.292969, 1.000000), float4 (0.246094, 0.246094, 0.246094, 1.000000),
		float4 (0.183594, 0.183594, 0.183594, 1.000000), float4 (0.183594, 0.183594, 0.183594, 0.000000), float4 (0.183594, 0.183594, 0.183594, 0.000000),
		float4 (0.183594, 0.183594, 0.183594, 0.000000), float4 (0.183594, 0.183594, 0.183594, 0.000000), float4 (0.183594, 0.183594, 0.183594, 0.000000)
	}
};


float4 BillboardParticle (VS_PARTICLE vs_in)
{
	// move the particle to it's current position
	// velocity += grav * elapsed; position += velocity * elapsed;
	// http://www.niksula.hut.fi/~hkankaan/Homepages/gravity.html - half the acceleration goes on velocity
	// also equivalent to gl orange book particle system example formula
	float3 Accel = (vs_in.dVel.xxy + float3 (0, 0, sv_gravity * vs_in.Grav)) * vs_in.Time;
	float3 BasePos = vs_in.Pos + (vs_in.Vel + (Accel * 0.5f)) * vs_in.Time;

	// hack a scale up to prevent particles from disappearing
	float HackScale = (1.0f + dot (BasePos - vieworigin, r_vpn) * distscale) * vs_in.Scale * partscale;

	// and expand to quad (something about this just needing to be a matrix multiply is nagging at me)
	return float4 (BasePos + (r_voffsets[vs_in.VertexID].xyz * HackScale), 1.0f);
}


float4 ParticleRampColor (VS_PARTICLE vs_in)
{
	// interpolate between current and next ramp based on the overshoot of current
	float RampTimeHi = vs_in.BaseRamp + (vs_in.Time * vs_in.RampTime) + 0.5f;
	uint RampTimeLo = (uint) floor (RampTimeHi);

	return lerp (particleramps[vs_in.Ramp][RampTimeLo], particleramps[vs_in.Ramp][RampTimeLo + 1], RampTimeHi - RampTimeLo);
}


float4 ParticleStandardColor (VS_PARTICLE vs_in)
{
	// alpha needs to be clamped as high values cause havoc
	float alpha = saturate (0.95f - (cltime - vs_in.Die) * 10.0f);

	return float4 (vs_in.Color.rgb, alpha);
}


// should we have 2 x VS and change them at runtime rather than branching in a single VS???
PS_PARTICLE PartVS (VS_PARTICLE vs_in)
{
	PS_PARTICLE vs_out;

	// move expanded particle to world position
	vs_out.Pos = mul (worldMatrix, BillboardParticle (vs_in));

	// evaluate colour (fixme - should we split this into two vertex shaders???)
	float4 basecolor;

	if (vs_in.Ramp)
		basecolor = ParticleRampColor (vs_in);
	else basecolor = ParticleStandardColor (vs_in);

	vs_out.Color = float4 ((basecolor * lightscale).rgb, basecolor.a);

	// and fill everything else in
	vs_out.Tex = parttexcoords[vs_in.VertexID];

	return vs_out;
}


