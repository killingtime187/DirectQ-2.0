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


cbuffer cbRTTGlobal : register(b1)
{
	float2 rttViewPortSize : packoffset(c0.x);
	float2 rttVidModeSize : packoffset(c0.z);
};


cbuffer cbRTTPerEffect : register(b2)
{
	float4 effectConstant : packoffset(c0);
	float4 effectColor : packoffset(c1);
};


// most effects should only use one texture and one sampler but we create space for up to 4 in case we ever need it
SamplerState sampler3 : register(s3);
SamplerState sampler4 : register(s4);
SamplerState sampler5 : register(s5);
SamplerState sampler6 : register(s6);

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);


struct VS_RTT
{
	uint VertexID : SV_VERTEXID;
};


struct PS_WATERWARP
{
	float4 Pos : SV_POSITION;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
	float2 Tex2 : TEXCOORD2;
	float InvertWarp : TEXCOORD3;
};


static const float2 MainRTTTexCoords[4] =
{
	float2 (0.0f, 0.0f),
	float2 (1.0f, 0.0f),
	float2 (0.0f, 1.0f),
	float2 (1.0f, 1.0f)
};


float4 RTTGetPosition (float2 coords)
{
	// needed by quake's fucked-up viewport sizes
	return float4
	(
		((coords.x * (2.0f * rttViewPortSize.x) / rttVidModeSize.x) - 1.0f) * 1.0f,
		((coords.y * (2.0f * rttViewPortSize.y) / rttVidModeSize.y) - 1.0f) * -1.0f,
		0.0f,
		1.0f
	);
}


float2 RTTGetTexCoords (float2 coords)
{
	// needed by quake's fucked-up viewport sizes
	return float2
	(
		(coords.x * rttViewPortSize.x) / rttVidModeSize.x,
		(coords.y * rttViewPortSize.y) / rttVidModeSize.y
	);
}


float4 WaterWarpPSModern (PS_WATERWARP ps_in) : SV_TARGET0
{
	float4 control = tex1.SampleLevel (sampler3, ps_in.Tex2, 0) * effectConstant.w;
	float4 distort1 = tex1.SampleLevel (sampler0, ps_in.Tex1 + effectConstant.x, 0);	// move to vs?
	float4 distort2 = tex1.SampleLevel (sampler0, ps_in.Tex1 - effectConstant.x, 0);	// move to vs?

	float2 warpcoords = float2 (
		ps_in.Tex0.x + (distort1.b * 2.0f - 1.0f) * control.r,
		ps_in.Tex0.y + (distort2.a * 2.0f - 1.0f) * control.g);

	return lerp (tex0.SampleLevel (sampler0, warpcoords * ps_in.InvertWarp, 0), effectColor, effectColor.a);
}


PS_WATERWARP WaterWarpVSModern (VS_RTT vs_in)
{
	PS_WATERWARP vs_out;

	float scale = 256.0f;
	float2 distortsts = float2 (scale, (scale * rttVidModeSize.y) / rttVidModeSize.x);

	vs_out.Pos = RTTGetPosition (MainRTTTexCoords[vs_in.VertexID]);

	vs_out.Tex0 = MainRTTTexCoords[vs_in.VertexID] * RTTGetTexCoords (float2 (scale, scale));
	vs_out.Tex1 = MainRTTTexCoords[vs_in.VertexID] * distortsts * effectConstant.y;
	vs_out.Tex2 = MainRTTTexCoords[vs_in.VertexID];
	vs_out.InvertWarp = 1.0f / scale;

	return vs_out;
}


float4 WaterWarpPSClassic (PS_WATERWARP ps_in) : SV_TARGET0
{
	float4 control = tex1.SampleLevel (sampler3, ps_in.Tex2, 0);
	float4 basecolor = tex0.SampleLevel (sampler3, (ps_in.Tex0 + sin (ps_in.Tex1) * effectConstant.w * control.rg) * ps_in.InvertWarp, 0);

	return lerp (basecolor, effectColor, effectColor.a);
}


PS_WATERWARP WaterWarpVSClassic (VS_RTT vs_in)
{
	PS_WATERWARP vs_out;

	float2 warpcoord = RTTGetTexCoords (MainRTTTexCoords[vs_in.VertexID]);

	vs_out.Pos = RTTGetPosition (MainRTTTexCoords[vs_in.VertexID]);
	vs_out.Tex0 = warpcoord * effectConstant.y;
	vs_out.Tex1 = (warpcoord.yx * effectConstant.y + effectConstant.x);
	vs_out.Tex2 = MainRTTTexCoords[vs_in.VertexID];
	vs_out.InvertWarp = 1.0f / effectConstant.y;

	return vs_out;
}


struct PS_POLYBLEND
{
	float4 Pos : SV_POSITION;
	float2 Tex0 : TEXCOORD0;
};

float4 PolyblendPS (PS_POLYBLEND ps_in) : SV_TARGET0
{
	float4 basecolor = tex0.SampleLevel (sampler3, ps_in.Tex0, 0);
	return lerp (basecolor, effectColor, effectColor.a);
}

PS_POLYBLEND PolyblendVS (VS_RTT vs_in)
{
	PS_POLYBLEND vs_out;

	vs_out.Pos = RTTGetPosition (MainRTTTexCoords[vs_in.VertexID]);
	vs_out.Tex0 = RTTGetTexCoords (MainRTTTexCoords[vs_in.VertexID]);

	return vs_out;
}


