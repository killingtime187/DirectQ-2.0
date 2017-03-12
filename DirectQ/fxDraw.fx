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
	matrix drawMatrix : packoffset(c0);
};


struct VS_BBOX
{
	float4 Position : POSITION;
	float3 BoxPosition : BOXPOSITION;
	float3 BoxScale : BOXSCALE;
	float4 Colour : COLOUR;
};


struct PS_BBOX
{
	float4 Position : SV_POSITION;
	float4 Color : DRAWCOLOR;
};


static const float4 drawflatcolors[] =
{
	float4 (0.5f, 0.0f, 0.0f, 1.0f),
	float4 (0.0f, 0.5f, 0.0f, 1.0f),
	float4 (0.0f, 0.0f, 0.5f, 1.0f),
	float4 (0.5f, 0.5f, 0.0f, 1.0f),
	float4 (0.5f, 0.0f, 0.5f, 1.0f),
	float4 (0.0f, 0.5f, 0.5f, 1.0f)
};


float4 RDrawFlatPS (float4 Position : SV_POSITION, uint PrimID : SV_PRIMITIVEID) : SV_TARGET0
{
	// if we ever go back to using a GS with particles and moving the cl_particles stuff to it, we'll need to change this
	// as SV_PRIMITIVEID won't be directly readable by the PS anymore...
	return drawflatcolors[PrimID % 6];
}


float4 RWireFramePS (float4 Position : SV_POSITION) : SV_TARGET0
{
	return float4 (1, 1, 1, 1);
}


[maxvertexcount(3)]
void RWireFrameGS (triangle float4 vert[3], inout TriangleStream<float4> triStream)
{
}


float4 RShowDepthPS (float4 Position : SV_POSITION) : SV_TARGET0
{
	float n = 4.0;
	float f = 4096.0;
	float deep = 1.0f - (1.0f / ((1.0f - (f / n)) * Position.z + (f / n)));

	return float4 (deep, deep, deep, 1.0f);
}


float4 BBoxPS (PS_BBOX ps_in) : SV_TARGET0
{
	return ps_in.Color;
}


PS_BBOX BBoxVS (VS_BBOX vs_in)
{
	PS_BBOX vs_out;

	float4x4 boxMatrix = float4x4
	(
		float4 (vs_in.BoxScale.x, 0, 0, vs_in.BoxPosition.x),
		float4 (0, vs_in.BoxScale.y, 0, vs_in.BoxPosition.y),
		float4 (0, 0, vs_in.BoxScale.z, vs_in.BoxPosition.z),
		float4 (0, 0, 0, 1)
	);
	
	vs_out.Position = mul (worldMatrix, mul (boxMatrix, vs_in.Position));
	vs_out.Color = vs_in.Colour;

	return vs_out;
}


SamplerState sampler3 : register(s3);
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);


struct VS_DRAW
{
	float4 Positions : POSITIONS;
	float4 TexCoords : TEXCOORDS;
	float4 DrawColor : DRAWCOLOR;
	uint VertexID : SV_VERTEXID;
};


struct PS_DRAW
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD0;
	float4 Color : DRAWCOLOR;
};


struct VS_BRIGHTPASS
{
	uint VertexID : SV_VERTEXID;
};


struct PS_BRIGHTPASS
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD0;
};


static const float4 InstanceMults[4] =
{
	float4 (1, 0, 1, 0),
	float4 (0, 1, 1, 0),
	float4 (1, 0, 0, 1),
	float4 (0, 1, 0, 1)
};


float2 Draw_GetInstance (float4 InstData, uint VertexID)
{
	return float2 (
		dot (InstData.xy, InstanceMults[VertexID].xy),
		dot (InstData.zw, InstanceMults[VertexID].zw));
}


float4 DrawPS (PS_DRAW ps_in) : SV_TARGET0
{
	return tex0.SampleLevel (sampler3, ps_in.Tex, 0) * ps_in.Color;
}


cbuffer cbPerObject : register(b2)
{
	float3 rdot : packoffset(c0.x);
	float paddingdvr : packoffset(c0.w);
	float3 gdot : packoffset(c1.x);
	float paddingdvg : packoffset(c1.w);
	float3 bdot : packoffset(c2.x);
	float paddingdvb : packoffset(c2.w);
};


float4 DogPS (PS_DRAW ps_in) : SV_TARGET0
{
	float4 color = tex0.SampleLevel (sampler3, ps_in.Tex, 0);
	return float4 (dot (rdot, color.rgb), dot (gdot, color.rgb), dot (bdot, color.rgb), color.a);
}


float4 FadePS (PS_DRAW ps_in) : SV_TARGET0
{
	float4 color = tex0.SampleLevel (sampler3, ps_in.Tex, 0);
	float gs = dot (color, float4 (0.3f, 0.59f, 0.11f, 0.0f));
	return tex1.SampleLevel (sampler3, float2 (gs, 0), 0) * ps_in.Color;
}


float4 ColorPS (PS_DRAW ps_in) : SV_TARGET0
{
	return ps_in.Color;
}


PS_DRAW DrawVS (VS_DRAW vs_in)
{
	PS_DRAW vs_out;

	vs_out.Pos = mul (drawMatrix, float4 (Draw_GetInstance (vs_in.Positions, vs_in.VertexID), 0, 1));
	vs_out.Tex = Draw_GetInstance (vs_in.TexCoords, vs_in.VertexID);
	vs_out.Color = vs_in.DrawColor;

	return vs_out;
}


static const float4 BrightPassPositions[4] =
{
	float4 (-1.0f, 1.0f, 0.0f, 1.0f),
	float4 (1.0f, 1.0f, 0.0f, 1.0f),
	float4 (-1.0f, -1.0f, 0.0f, 1.0f),
	float4 (1.0f, -1.0f, 0.0f, 1.0f)
};


static const float2 BrightPassTexCoords[4] =
{
	float2 (0.0f, 0.0f),
	float2 (1.0f, 0.0f),
	float2 (0.0f, 1.0f),
	float2 (1.0f, 1.0f)
};


cbuffer cbPerObject : register(b1)
{
	float4 rgb_gamma : packoffset(c0.x);
	float4 rgb_contrast : packoffset(c1.x);
	float mipLevel : packoffset(c2.x);
	float3 Junk : packoffset(c2.y);
};


float4 BrightPassPS (PS_DRAW ps_in) : SV_TARGET0
{
	float4 color = tex0.SampleLevel (sampler0, ps_in.Tex, 0);

	// run contrast before gamma as the average colour is also derived from pre-gamma
	color = ((color - ps_in.Color) * rgb_contrast) + ps_in.Color;
	color = pow (color, rgb_gamma);

	return float4 (color.rgb, 1.0f);
}


PS_DRAW BrightPassVS (VS_BRIGHTPASS vs_in)
{
	PS_DRAW vs_out;

	vs_out.Pos = BrightPassPositions[vs_in.VertexID];
	vs_out.Tex = BrightPassTexCoords[vs_in.VertexID];

	// on every driver I've tested sampling level 666 will clamp to the highest, but
	// this behaviour isn't specified in the documentation so let's just do it right instead
	vs_out.Color = tex0.SampleLevel (sampler0, float2 (0, 0), mipLevel);

	return vs_out;
}


