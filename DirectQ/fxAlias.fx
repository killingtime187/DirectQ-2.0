/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
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
	float4 shadelight : packoffset(c4);
	float3 shadevector : packoffset(c5.x);
	float blend : packoffset(c5.w);
};


cbuffer cbJointRows : register(b2)
{
	float4 jr1[256] : packoffset(c0);
	float4 jr2[256] : packoffset(c256);
	float4 jr3[256] : packoffset(c768);
};


Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
Texture3D texNoise : register(t5);


struct VS_MESH
{
	float4 CurrPos : CURRPOS;
	float3 CurrNorm : CURRNORMAL;
	float4 LastPos : LASTPOS;
	float3 LastNorm : LASTNORMAL;
	float2 Tex : TEXCOORD;
};

struct VS_INSTANCED
{
	float4 CurrPos : CURRPOS;
	float3 CurrNorm : CURRNORMAL;
	float4 LastPos : LASTPOS;
	float3 LastNorm : LASTNORMAL;
	float2 Tex : TEXCOORD;
	matrix modelMatrix : TRANSFORM;
	float4 shadelight : SHADELIGHT;
	float3 shadevector : SHADEVECTOR;
	float blend : BLEND;
};

struct VS_VIEWMODEL
{
	float4 CurrPos : CURRPOS;
	float3 CurrNorm : CURRNORMAL;
	float4 LastPos : LASTPOS;
	float3 LastNorm : LASTNORMAL;
	float2 Tex : TEXCOORD;
	float LerpType : LERPTYPE;
};

struct VS_IQM
{
	float4 Position : POSITION;
	float3 Normal : NORMAL;
	float2 Texcoord : TEXCOORD;
	uint4 BIndexes : BINDEX;
	float4 BWeight : BWEIGHT;
};

struct PS_MESH
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD0;
	float3 normal : SHADENORMAL;
	float4 shadelight : SHADELIGHT;
	float3 shadevector : SHADEVECTOR;
};


float4 MeshPS (PS_MESH ps_in) : SV_TARGET0
{
#if defined (ALIASSHADER_SHADOW)
	float4 diff = float4 (0, 0, 0, r_shadows);

#if defined (ALIASSHADER_FOG)
	diff = FogCalc (diff, ps_in.Pos.w);
#endif

	return diff;
#else
	float shadedot = dot (normalize (ps_in.normal), ps_in.shadevector);
	float4 lmap = ps_in.shadelight * max (shadedot + 1.0f, (shadedot * 0.2954545f) + 1.0f);

#if defined (ALIASSHADER_PLAYER)
	// and this, ladies and gentlemen, is one reason why Quake sucks but Quake II doesn't...
	float4 colormap = tex2.Sample (sampler0, ps_in.Tex);

	float4 diff = (tex0.Sample (sampler0, ps_in.Tex) * (1.0f - (colormap.r + colormap.a))) +
				  (tex3.SampleLevel (sampler0, float2 (colormap.g, 0), 0) * colormap.r) +
				  (tex4.SampleLevel (sampler0, float2 (colormap.b, 0), 0) * colormap.a);
#else
	float4 diff = tex0.Sample (sampler0, ps_in.Tex);
#endif

	diff *= (lmap * 2.0f);

#if defined (ALIASSHADER_FOG)
	diff = FogCalc (diff, ps_in.Pos.w);
#endif

#if defined (ALIASSHADER_STDLUMA)
	diff = max (diff, tex1.Sample (sampler0, ps_in.Tex));
#elif defined (ALIASSHADER_ADDLUMA)
	diff += tex1.Sample (sampler0, ps_in.Tex);
#endif

	return float4 (diff.rgb, ps_in.shadelight.a);
#endif
}


PS_MESH MeshVS (VS_MESH vs_in)
{
	PS_MESH vs_out;

	vs_out.Pos = mul (modelMatrix, lerp (vs_in.LastPos, vs_in.CurrPos, blend));
	vs_out.Tex = vs_in.Tex;
	vs_out.normal = lerp (vs_in.LastNorm, vs_in.CurrNorm, blend);
	vs_out.shadelight = shadelight;
	vs_out.shadevector = shadevector;

	return vs_out;
}


PS_MESH InstancedVS (VS_INSTANCED vs_in)
{
	PS_MESH vs_out;

	vs_out.Pos = mul (vs_in.modelMatrix, lerp (vs_in.LastPos, vs_in.CurrPos, vs_in.blend));
	vs_out.Tex = vs_in.Tex;
	vs_out.normal = lerp (vs_in.LastNorm, vs_in.CurrNorm, vs_in.blend);
	vs_out.shadelight = vs_in.shadelight;
	vs_out.shadevector = vs_in.shadevector;

	return vs_out;
}


PS_MESH ViewModelVS (VS_VIEWMODEL vs_in)
{
	PS_MESH vs_out;

	// generates 2 instructions...
	// ge r1.x, l(0.000000), v5.x
	// movc r1.x, r1.x, cb1[5].w, l(1.000000)
	float stepblend = step (vs_in.LerpType, 0);

	if (stepblend == 0)
		stepblend = 1;
	else stepblend = blend;

	vs_out.Pos = mul (modelMatrix, lerp (vs_in.LastPos, vs_in.CurrPos, stepblend)) * float4 (1.0f, 1.0f, 0.5f, 1.0f);
	vs_out.Tex = vs_in.Tex;
	vs_out.normal = lerp (vs_in.LastNorm, vs_in.CurrNorm, stepblend);
	vs_out.shadelight = shadelight;
	vs_out.shadevector = shadevector;

	return vs_out;
}


PS_MESH IQMVS (VS_IQM vs_in)
{
	PS_MESH vs_out;

	float3x4 Joint = float3x4 (jr1[vs_in.BIndexes.x], jr2[vs_in.BIndexes.x], jr3[vs_in.BIndexes.x]) * vs_in.BWeight.x;

	Joint += float3x4 (jr1[vs_in.BIndexes.y], jr2[vs_in.BIndexes.y], jr3[vs_in.BIndexes.y]) * vs_in.BWeight.y;
	Joint += float3x4 (jr1[vs_in.BIndexes.z], jr2[vs_in.BIndexes.z], jr3[vs_in.BIndexes.z]) * vs_in.BWeight.z;
	Joint += float3x4 (jr1[vs_in.BIndexes.w], jr2[vs_in.BIndexes.w], jr3[vs_in.BIndexes.w]) * vs_in.BWeight.w;

	vs_out.Pos = mul (modelMatrix, float4 (mul (Joint, vs_in.Position), vs_in.Position.w));
	vs_out.Tex = vs_in.Texcoord;
	vs_out.normal = vs_in.Normal;
	vs_out.shadelight = shadelight;
	vs_out.shadevector = shadevector;

	return vs_out;
}


