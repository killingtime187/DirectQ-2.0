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
// r_misc.c

#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

#include <d3dcompiler.h>
#include <vector>

#pragma comment (lib, "d3dcompiler.lib")

ID3D11Buffer *d3d_MainConstants = NULL;

#define CBUFFER_INJECT \
	"// common stuff for all shaders\n" \
	"cbuffer cbPerFrame : register(b0)\n" \
	"{\n" \
	"	matrix worldMatrix : packoffset(c0);\n" \
	"	float r_shadows : packoffset(c4.x);\n" \
	"	float detailScale : packoffset(c4.y);\n" \
	"	float fogdensity : packoffset(c4.z);\n" \
	"	float skyfog : packoffset(c4.w);\n" \
	"	float3 fogcolor : packoffset(c5.x);\n" \
	"	float clienttime : packoffset(c5.w);\n" \
	"	float3 vieworigin : packoffset(c6.x);\n" \
	"	float paddingb4 : packoffset(c6.w);\n" \
	"};\n\n" \
	"\n" \
	"SamplerState sampler0 : register(s0);\n" \
	"SamplerState sampler1 : register(s1);\n" \
	"SamplerState sampler2 : register(s2);\n\n\n\n" \
	"\n" \
	"float4 FogCalc (float4 color, float fogw)\n" \
	"{\n" \
	"	float fogfactor = 1.0f / pow (2.71828183f, pow (fogdensity * fogw, 2.0f));\n" \
	"	return lerp (float4 (fogcolor, 1.0f), color, clamp (fogfactor, 0.0, 1.0));\n" \
	"}\n\n\n\n"

struct d3d_maincbuffer_t
{
	QMATRIX WorldMatrix;
	float r_shadows;
	float detailScale;
	float fogdensity;
	float skyfog;
	float fogcolor[3];
	float clienttime;
	float vieworigin[3];
	float paddingb4;
};


// so that i never have to worry about releasing a resource (to do - extend this to all resource types - textures, buffers, etc)
enum resourcetype_t
{
	RT_VERTEXSHADER,
	RT_PIXELSHADER,
	RT_COMPUTESHADER,
	RT_GEOMETRYSHADER,
	RT_INPUTLAYOUT,
	RT_BUFFER
};


struct QRESOURCE
{
	QRESOURCE (void *res, resourcetype_t type)
	{
		switch (type)
		{
		case RT_VERTEXSHADER:   this->vs = (ID3D11VertexShader **) res; break;
		case RT_PIXELSHADER:    this->ps = (ID3D11PixelShader **) res; break;
		case RT_COMPUTESHADER:  this->cs = (ID3D11ComputeShader **) res; break;
		case RT_GEOMETRYSHADER: this->gs = (ID3D11GeometryShader **) res; break;
		case RT_INPUTLAYOUT:    this->lo = (ID3D11InputLayout **) res; break;
		default: Sys_Error ("QRESOURCE::QRESOURCE : SHIT SHIT SHIT SHIT SHIT - this should never happen");
		}

		this->Type = type;
	}

	~QRESOURCE (void)
	{
		switch (this->Type)
		{
		case RT_VERTEXSHADER:   SAFE_RELEASE (this->vs[0]); break;
		case RT_PIXELSHADER:    SAFE_RELEASE (this->ps[0]); break;
		case RT_COMPUTESHADER:  SAFE_RELEASE (this->cs[0]); break;
		case RT_GEOMETRYSHADER: SAFE_RELEASE (this->gs[0]); break;
		case RT_INPUTLAYOUT:    SAFE_RELEASE (this->lo[0]); break;
		default: Sys_Error ("QRESOURCE::~QRESOURCE : SHIT SHIT SHIT SHIT SHIT - this should never happen");
		}
	}

	union
	{
		ID3D11VertexShader **vs;
		ID3D11PixelShader **ps;
		ID3D11ComputeShader **cs;
		ID3D11GeometryShader **gs;
		ID3D11InputLayout **lo;
		ID3D11Buffer **buf;
	};

	resourcetype_t Type;
};


std::vector<QRESOURCE *> d3d_HLSLResources;


void D3DHLSL_Init (void)
{
	// create our constant buffer
	BufferFactory.CreateConstantBuffer (sizeof (d3d_maincbuffer_t), &d3d_MainConstants, "d3d_MainConstants");

	// DO NOT clear the vector here because other handlers which create resources could fire before this
}


void D3DHLSL_Shutdown (void)
{
	for (int i = 0; i < d3d_HLSLResources.size (); i++)
	{
		if (d3d_HLSLResources[i])
		{
			delete d3d_HLSLResources[i];
			d3d_HLSLResources[i] = NULL;
		}
	}

	d3d_HLSLResources.clear ();
	SAFE_RELEASE (d3d_MainConstants);
}


void D3DHLSL_UpdateMainCBuffer (void)
{
	// update constant buffer
	d3d_maincbuffer_t d3d_MainUpdate;
	extern cvar_t sv_gravity;

	// set the final projection matrix that we'll actually use (this is good for most Quake scenes)
	D3DMain_SetupProjection (vid.fov_x, vid.fov_y, 4, vid.farclip);

	d3d_MainUpdate.WorldMatrix.Load (&d3d_ModelViewProjMatrix);
	d3d_MainUpdate.r_shadows = r_shadows.value;

	d3d_MainUpdate.skyfog = SkyFogDensity;

	if (r_detailtextures.value > 0)
		d3d_MainUpdate.detailScale = 0.15f / r_detailtextures.value;
	else d3d_MainUpdate.detailScale = 0;

	d3d_MainUpdate.fogdensity = RealFogDensity;
	d3d_MainUpdate.clienttime = cl.time * temp1.value;

	Vector3Copy (d3d_MainUpdate.fogcolor, RealFogColor);
	Vector3Copy (d3d_MainUpdate.vieworigin, r_refdef.vieworigin);

	d3d11_Context->UpdateSubresource (d3d_MainConstants, 0, NULL, &d3d_MainUpdate, 0, 0);

	// make it available to all shader stages
	d3d11_State->VSSetConstantBuffer (0, d3d_MainConstants);
	d3d11_State->PSSetConstantBuffer (0, d3d_MainConstants);
	d3d11_Context->GSSetConstantBuffers (0, 1, &d3d_MainConstants);
}


CD3DInitShutdownHandler d3d11_HLSLHandler ("hlsl", D3DHLSL_Init, D3DHLSL_Shutdown);


QSHADERFACTORY::QSHADERFACTORY (int resourceid)
{
	this->ShaderText = NULL;
	this->ShaderBlob = NULL;

	char *data = NULL;
	int len = Sys.LoadResourceData (resourceid, (void **) &data);

	if (data && len)
	{
		TempHunk->UnlockScratch ();
		this->ShaderText = (char *) TempHunk->ScratchAlloc (len + strlen (CBUFFER_INJECT) + 4);

		memset (this->ShaderText, 0, len + strlen (CBUFFER_INJECT) + 2);
		strcpy (this->ShaderText, CBUFFER_INJECT);
		Q_MemCpy (&this->ShaderText[strlen (CBUFFER_INJECT) - 1], data, len);
	}
	else Sys_Error ("D3DHLSL_GetTextFromResource : failed to get resource %i\n", resourceid);
}


QSHADERFACTORY::~QSHADERFACTORY (void)
{
	SAFE_RELEASE (this->ShaderBlob);
}


D3D10_SHADER_MACRO *QSHADERFACTORY::EncodeDefines (const shaderdefine_t *shaderdefines, int numshaderdefines, int flags)
{
	// encodes the flags into an array of D3D10_SHADER_MACRO from a master list of defines
	static D3D10_SHADER_MACRO Defines[33];
	int NumDefines = 0;

	// clear all defines from the list
	memset (Defines, 0, sizeof (Defines));

	// check the flags to see what defines we need to set
	// there can never be more than 32 as we use an int for flags, and add one extra for NULL-termination
	for (int i = 0; i < numshaderdefines; i++)
	{
		if (flags & shaderdefines[i].Flag)
		{
			Q_MemCpy (&Defines[NumDefines], &shaderdefines[i].Define, sizeof (D3D10_SHADER_MACRO));
			NumDefines++;
		}
	}

	return Defines;
}


HRESULT QSHADERFACTORY::CompileGeneric (const char *entrypoint, const char *profile, const D3D10_SHADER_MACRO *defines)
{
	SAFE_RELEASE (this->ShaderBlob);

	assert (entrypoint);
	assert (profile);
	assert (this->ShaderText);

	DWORD dwShaderFlags = D3D10_SHADER_SKIP_VALIDATION | D3D10_SHADER_OPTIMIZATION_LEVEL3;
	ID3D10Blob *pBlobError = NULL;

	hr = D3DCompile (
		this->ShaderText,
		strlen (this->ShaderText) + 1,
		NULL,
		defines,
		NULL,
		entrypoint,
		profile,
		dwShaderFlags,
		0,
		&this->ShaderBlob,
		&pBlobError
	);

	if (FAILED (hr))
	{
		if (pBlobError != NULL)
		{
			Sys_Error ("Error compiling %s : %s", entrypoint, (char *) pBlobError->GetBufferPointer ());
			pBlobError->Release ();
		}
	}

	return hr;
}


void QSHADERFACTORY::CreateVertexShader (ID3D11VertexShader **vs, char *entrypoint, const D3D10_SHADER_MACRO *defines)
{
	assert (vs);
	assert (this->ShaderText);
	assert (entrypoint);

	if (SUCCEEDED (hr = this->CompileGeneric (entrypoint, "vs_4_0", defines)))
	{
		if (SUCCEEDED (hr = d3d11_Device->CreateVertexShader (this->ShaderBlob->GetBufferPointer (), this->ShaderBlob->GetBufferSize (), NULL, vs)))
		{
			d3d_HLSLResources.push_back (new QRESOURCE (vs, RT_VERTEXSHADER));
			D3DMisc_SetObjectName (vs[0], entrypoint);
		}
		else Sys_Error ("QSHADERFACTORY::CreateVertexShader : ID3D11Device::CreateVertexShader failed");
	}
	else Sys_Error ("QSHADERFACTORY::CreateVertexShader : QSHADERFACTORY::CompileGeneric failed");
}


void QSHADERFACTORY::CreatePixelShader (ID3D11PixelShader **ps, char *entrypoint, const D3D10_SHADER_MACRO *defines)
{
	assert (ps);
	assert (this->ShaderText);
	assert (entrypoint);

	if (SUCCEEDED (hr = this->CompileGeneric (entrypoint, "ps_4_0", defines)))
	{
		if (SUCCEEDED (hr = d3d11_Device->CreatePixelShader (this->ShaderBlob->GetBufferPointer (), this->ShaderBlob->GetBufferSize (), NULL, ps)))
		{
			d3d_HLSLResources.push_back (new QRESOURCE (ps, RT_PIXELSHADER));
			D3DMisc_SetObjectName (ps[0], entrypoint);
		}
		else Sys_Error ("QSHADERFACTORY::CreatePixelShader : ID3D11Device::CreatePixelShader failed");
	}
	else Sys_Error ("QSHADERFACTORY::CreatePixelShader : QSHADERFACTORY::CompileGeneric failed");
}


void QSHADERFACTORY::CreateComputeShader (ID3D11ComputeShader **cs, char *entrypoint, const D3D10_SHADER_MACRO *defines)
{
	assert (cs);
	assert (this->ShaderText);
	assert (entrypoint);

	if (SUCCEEDED (hr = this->CompileGeneric (entrypoint, "cs_5_0", defines)))
	{
		if (SUCCEEDED (hr = d3d11_Device->CreateComputeShader (this->ShaderBlob->GetBufferPointer (), this->ShaderBlob->GetBufferSize (), NULL, cs)))
		{
			d3d_HLSLResources.push_back (new QRESOURCE (cs, RT_COMPUTESHADER));
			D3DMisc_SetObjectName (cs[0], entrypoint);
		}
		else Sys_Error ("QSHADERFACTORY::CreateComputeShader : ID3D11Device::CreateComputeShader failed");
	}
	else Sys_Error ("QSHADERFACTORY::CreateComputeShader : QSHADERFACTORY::CompileGeneric failed");
}


void QSHADERFACTORY::CreateGeometryShader (ID3D11GeometryShader **gs, char *entrypoint, const D3D10_SHADER_MACRO *defines)
{
	assert (gs);
	assert (this->ShaderText);
	assert (entrypoint);

	if (SUCCEEDED (hr = this->CompileGeneric (entrypoint, "gs_4_0", defines)))
	{
		if (SUCCEEDED (hr = d3d11_Device->CreateGeometryShader (this->ShaderBlob->GetBufferPointer (), this->ShaderBlob->GetBufferSize (), NULL, gs)))
		{
			d3d_HLSLResources.push_back (new QRESOURCE (gs, RT_GEOMETRYSHADER));
			D3DMisc_SetObjectName (gs[0], entrypoint);
		}
		else Sys_Error ("QSHADERFACTORY::CreateGeometryShader : ID3D11Device::CreateGeometryShader failed");
	}
	else Sys_Error ("QSHADERFACTORY::CreateGeometryShader : QSHADERFACTORY::CompileGeneric failed");
}


void QSHADERFACTORY::CreateInputLayout (ID3D11InputLayout **lo, char *loname, D3D11_INPUT_ELEMENT_DESC *lodesc, int loitems)
{
	assert (loname);
	assert (lo);
	assert (lodesc);
	assert (loitems);
	assert (this->ShaderBlob);

	if (SUCCEEDED (d3d11_Device->CreateInputLayout (lodesc, loitems, this->ShaderBlob->GetBufferPointer (), this->ShaderBlob->GetBufferSize (), lo)))
	{
		d3d_HLSLResources.push_back (new QRESOURCE (lo, RT_INPUTLAYOUT));
		D3DMisc_SetObjectName (lo[0], loname);
	}
	else Sys_Error ("QSHADERFACTORY::CreateInputLayout : ID3D11Device::CreateInputLayout failed");
}

