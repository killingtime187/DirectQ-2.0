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
	matrix modelMatrix : packoffset(c0);
	float4 surfcolour : packoffset(c4);
};


cbuffer cbSkyParams : register(b2)
{
	matrix skyMatrix : packoffset(c0);
	float3 SkyScale : packoffset(c4.x);
	float SkyAlpha : packoffset(c4.w);
};


cbuffer cbWarpParams : register(b2)
{
	float warptime : packoffset(c0.x);
	float warpfactor : packoffset(c0.y);
	float warpscale : packoffset(c0.z);
	float warptexturescale : packoffset(c0.w);
	float2 ripple : packoffset(c1.x);
	float2 padding : packoffset(c1.z);
};


// force use of specific registers so that ~SetShaderResources will behave itself
Texture2D texDiff : register(t0);
Texture2D texGrad : register(t1);
Texture2D texLuma : register(t1);
Texture2D texNoise : register(t2);
Texture2DArray texLmap : register(t6);

Texture2D skySolidLayer : register(t0);
Texture2D skyAlphaLayer : register(t1);

Texture2D skyBoxRt : register(t0);
Texture2D skyBoxLf : register(t1);
Texture2D skyBoxBk : register(t2);
Texture2D skyBoxFt : register(t3);
Texture2D skyBoxUp : register(t4);
Texture2D skyBoxDn : register(t5);

TextureCube skyBoxCube : register(t7);

struct VS_INPUT
{
	float4 Pos : POSITION;
	float2 Tex0 : TEXCOORD;
	float3 Tex1 : LMCOORD;
};


struct PS_SOLID
{
	float4 Pos : SV_POSITION;
	float2 Tex0 : TEXCOORD0;
	float3 Tex1 : TEXCOORD1;
#if defined (BRUSHSHADER_DETAIL)
	float2 Detail : DETAILCOORD;
#endif
};


struct PS_LIQUID
{
	float4 Pos : SV_POSITION;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
};


struct PS_SKYVERT
{
	float4 Pos : SV_POSITION;
	float3 Tex0 : TEXCOORD0;
};


float4 SurfPS (PS_SOLID ps_in) : SV_TARGET0
{
	float4 lmap = texLmap.SampleLevel (sampler1, ps_in.Tex1, 0);
	float4 diff = texDiff.Sample (sampler0, ps_in.Tex0) * (lmap / lmap.a) * surfcolour;

#if defined (BRUSHSHADER_DETAIL)
	float4 detailNoise = texNoise.Sample (sampler0, ps_in.Detail);
	float noiseNoise = dot (detailNoise, float4 (1.0f, 0.5f, 0.25f, 0.125f)) * 1.066666667f;
	// return noiseNoise * (lmap / lmap.a) * surfcolour * 0.5f;
	diff *= noiseNoise;
#endif

#if defined (BRUSHSHADER_FOG)
	diff = FogCalc (diff, ps_in.Pos.w);
#endif

#if defined (BRUSHSHADER_STDLUMA)
	diff = max (diff, texLuma.Sample (sampler0, ps_in.Tex0));
#elif defined (BRUSHSHADER_ADDLUMA)
	diff += texLuma.Sample (sampler0, ps_in.Tex0);
#endif

	return float4 (diff.rgb, surfcolour.a);
}


#if defined (BRUSHSHADER_DETAIL)
static const float3 baseaxis[18] =
{
	float3 (0.0f, 0.0f, 1.0f), float3 (1.0f, 0.0f, 0.0f), float3 (0.0f, -1.0f, 0.0f), 			// floor
	float3 (0.0f, 0.0f, -1.0f), float3 (1.0f, 0.0f, 0.0f), float3 (0.0f, -1.0f, 0.0f), 		// ceiling
	float3 (1.0f, 0.0f, 0.0f), float3 (0.0f, 1.0f, 0.0f), float3 (0.0f, 0.0f, -1.0f), 			// west wall
	float3 (-1.0f, 0.0f, 0.0f), float3 (0.0f, 1.0f, 0.0f), float3 (0.0f, 0.0f, -1.0f), 		// east wall
	float3 (0.0f, 1.0f, 0.0f), float3 (1.0f, 0.0f, 0.0f), float3 (0.0f, 0.0f, -1.0f), 			// south wall
	float3 (0.0f, -1.0f, 0.0f), float3 (1.0f, 0.0f, 0.0f), float3 (0.0f, 0.0f, -1.0f)			// north wall
};


[maxvertexcount(3)]
void SurfDetailGS (triangle VS_INPUT vs_in[3], inout TriangleStream<PS_SOLID> triStream)
{
	PS_SOLID gs_out;

	// recalculate the texinfo vecs
	int bestaxis = 0;
	int best = 0;

	// duplicate Quake's fucked-up normals (QBSP didn't normalize this)
	float3 normal = cross (vs_in[0].Pos.xyz - vs_in[1].Pos.xyz, vs_in[2].Pos.xyz - vs_in[1].Pos.xyz);

	for (int i = 0; i < 6; i++)
	{
		float d = dot (normal, baseaxis[i * 3]);

		if (d > best)
		{
			best = d;
			bestaxis = i;
		}
	}

	for (int i = 0; i < 3; i++)
	{
		gs_out.Pos = mul (modelMatrix, vs_in[i].Pos);
		gs_out.Tex0 = vs_in[i].Tex0;
		gs_out.Tex1 = vs_in[i].Tex1;

		gs_out.Detail = float2 (
			dot (vs_in[i].Pos.xyz, baseaxis[bestaxis * 3 + 1]),
			dot (vs_in[i].Pos.xyz, baseaxis[bestaxis * 3 + 2])
		) * detailScale;

		triStream.Append (gs_out);
	}
}


VS_INPUT SurfVS (VS_INPUT vs_in)
{
	return vs_in;
}
#else
PS_SOLID SurfVS (VS_INPUT vs_in)
{
	PS_SOLID vs_out;

	vs_out.Pos = mul (modelMatrix, vs_in.Pos);
	vs_out.Tex0 = vs_in.Tex0;
	vs_out.Tex1 = vs_in.Tex1;

	return vs_out;
}
#endif


float4 WarpPS (PS_LIQUID ps_in) : SV_TARGET0
{
	float4 base = texDiff.Sample (sampler0, ps_in.Tex0 + sin (ps_in.Tex1.yx) * warpscale) * surfcolour;

#if defined (WARPSHADER_FOG)
	base = FogCalc (base, ps_in.Pos.w);
#endif

	return base;
}


float4 RippleGetNV (float4 Pos)
{
	return float4
	(
		Pos.x,
		Pos.y,
		Pos.z + ripple.x * sin (Pos.x * 0.05f + ripple.y) * sin (Pos.z * 0.05f + ripple.y),
		Pos.w
	);
}


PS_LIQUID WarpVS (VS_INPUT vs_in)
{
	PS_LIQUID vs_out;

#if defined (WARPSHADER_RIPPLE)
	vs_out.Pos = mul (modelMatrix, RippleGetNV (vs_in.Pos));
#else
	vs_out.Pos = mul (modelMatrix, vs_in.Pos);
#endif

	vs_out.Tex0 = vs_in.Tex0 * warptexturescale;
	vs_out.Tex1 = (vs_in.Tex0 * warpfactor) + warptime;

	return vs_out;
}


float4 SkyPS (PS_SKYVERT ps_in) : SV_TARGET0
{
	float4 diff = float4 (0, 0, 0, 1);

#if defined (SKYSHADER_CUBE)
	float3 skyabs = abs (ps_in.Tex0);

	if (skyabs.x > skyabs.y && skyabs.x > skyabs.z && ps_in.Tex0.x > 0)
		diff = skyBoxRt.SampleLevel (sampler0, ((ps_in.Tex0.yz / skyabs.x) * float2 (-0.5, -0.5)) + 0.5, 0);
	else if (skyabs.x > skyabs.y && skyabs.x > skyabs.z)
		diff = skyBoxLf.SampleLevel (sampler0, ((ps_in.Tex0.yz / skyabs.x) * float2 (0.5, -0.5)) + 0.5, 0);
	else if (skyabs.y > skyabs.x && skyabs.y > skyabs.z && ps_in.Tex0.y > 0)
		diff = skyBoxBk.SampleLevel (sampler0, ((ps_in.Tex0.xz / skyabs.y) * float2 (0.5, -0.5)) + 0.5, 0);
	else if (skyabs.y > skyabs.x && skyabs.y > skyabs.z)
		diff = skyBoxFt.SampleLevel (sampler0, ((ps_in.Tex0.xz / skyabs.y) * float2 (-0.5, -0.5)) + 0.5, 0);
	else if (skyabs.z > skyabs.x && skyabs.z > skyabs.y && ps_in.Tex0.z > 0)
		diff = skyBoxUp.SampleLevel (sampler0, ((ps_in.Tex0.yx / skyabs.z) * float2 (-0.5, 0.5)) + 0.5, 0);
	else diff = skyBoxDn.SampleLevel (sampler0, ((ps_in.Tex0.yx / skyabs.z) * float2 (-0.5, -0.5)) + 0.5, 0);
#elif defined (SKYSHADER_ARRAY)
	diff = skyBoxCube.Sample (sampler0, ps_in.Tex0.xzy);
#else
	float3 Texcoord = normalize (ps_in.Tex0) * SkyScale.z;

	float4 SolidTex = skySolidLayer.SampleLevel (sampler0, Texcoord.xy + SkyScale.x, 0);
	float4 AlphaTex = skyAlphaLayer.SampleLevel (sampler0, Texcoord.xy + SkyScale.y, 0);

	diff = lerp (SolidTex, AlphaTex, AlphaTex.a * SkyAlpha);
#endif

	// sky should be affected by r_lightscale too otherwise it looks really odd as everything else gets brighter
#if defined (SKYSHADER_FOG)
	return lerp (float4 ((diff * surfcolour).rgb, 1.0f), float4 (fogcolor, 1.0f), skyfog);
#else
	return float4 ((diff * surfcolour).rgb, 1.0f);
#endif
}


PS_SKYVERT SkyVS (VS_INPUT vs_in)
{
	PS_SKYVERT vs_out;

	vs_out.Pos = mul (modelMatrix, vs_in.Pos);
	vs_out.Tex0 = mul (skyMatrix, vs_in.Pos).xyz;

	return vs_out;
}


