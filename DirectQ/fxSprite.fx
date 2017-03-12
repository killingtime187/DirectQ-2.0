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


struct VS_SPRITE
{
	float2 Positions : POSITIONS;
	float3 entorigin : ENTORIGIN;
	float3 uvec : UVECTOR;
	float3 rvec : RVECTOR;
	float color : DRAWCOLOR;
	float alpha : DRAWALPHA;
	uint VertexID : SV_VERTEXID;
};


struct PS_SPRITE
{
	float4 Pos : SV_POSITION;
	float4 Color : DRAWCOLOR;
	float2 Tex : TEXCOORD0;
};


Texture2D tex0 : register(t0);


float4 SpritePS (PS_SPRITE ps_in) : SV_TARGET0
{
	float4 color = tex0.Sample (sampler0, ps_in.Tex);

#if defined (SPRITE_FOG)
	return color * ps_in.Color;
#else
	return FogCalc (color * ps_in.Color, ps_in.Pos.w);
#endif
}


static const float2 spritetexcoords[4] = {float2 (0, 1), float2 (0, 0), float2 (1, 1), float2 (1, 0)};

PS_SPRITE SpriteVS (VS_SPRITE vs_in)
{
	PS_SPRITE vs_out;

	float4 BasePos = float4 (vs_in.rvec * vs_in.Positions.y + (vs_in.uvec * vs_in.Positions.x + vs_in. entorigin), 1.0);

	vs_out.Pos = mul (worldMatrix, BasePos);
	vs_out.Color = float4 (vs_in.color, vs_in.color, vs_in.color, vs_in.alpha);
	vs_out.Tex = spritetexcoords[vs_in.VertexID];

	return vs_out;
}


