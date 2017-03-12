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
#include "winquake.h"


// this is defined as a class so that we can create multiple state filters in the event of ever needing to use them in deferred contexts
STATEFILTER *d3d11_State = NULL;


void D3DState_ClearDynamicState (void)
{
	if (!d3d11_State)
		d3d11_State = new STATEFILTER ();

	d3d11_State->ClearState ();
}


CD3DInitShutdownHandler d3d_DynamicStateHandler ("state filter", D3DState_ClearDynamicState, D3DState_ClearDynamicState);


STATEFILTER::STATEFILTER (void)
{
	this->ClearState ();
}


void STATEFILTER::ResetMultislotState (d3d_multislot_t *slotstate)
{
	slotstate->MinSlot = 0xffffffff;
	slotstate->MaxSlot = 0;
}


void STATEFILTER::AccumulateMultislotState (d3d_multislot_t *slotstate, UINT slot, UINT dirtybit)
{
	if (slot < slotstate->MinSlot) slotstate->MinSlot = slot;
	if (slot > slotstate->MaxSlot) slotstate->MaxSlot = slot;

	this->DirtyBits |= dirtybit;
}


void STATEFILTER::ClearShaderState (shaderstate_t *ShaderState)
{
	for (int i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++) ShaderState->ShaderResourceView[i] = NULL;
	for (int i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++) ShaderState->Sampler[i] = NULL;
	for (int i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++) ShaderState->CBuffer[i] = NULL;

	this->ResetMultislotState (&ShaderState->SRVState);
	this->ResetMultislotState (&ShaderState->SSState);
	this->ResetMultislotState (&ShaderState->CBState);

	// dirty everything
	ShaderState->DirtyBits = 0;
}


void STATEFILTER::ClearState (void)
{
	// just wipes the state
	this->DepthStencilState = NULL;
	this->StencilRef = 0xffffffff;

	this->BlendState = NULL;
	this->BlendFactor[0] = this->BlendFactor[1] = this->BlendFactor[2] = this->BlendFactor[3] = 0;
	this->SampleMask = 0xffffffff;

	this->RasterizerState = NULL;

	this->ClearShaderState (&this->VSState);
	this->ClearShaderState (&this->PSState);

	for (int i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++)
	{
		this->VertexBuffers[i] = NULL;
		this->VBOffsets[i] = 0;
		this->VBStrides[i] = 0;
	}

	this->VertexShader = NULL;
	this->PixelShader = NULL;

	this->IndexBuffer = NULL;
	this->InputLayout = NULL;
	this->Topol = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	memset (&this->Viewport, 0, sizeof (D3D11_VIEWPORT));

	this->RTV = NULL;
	this->DSV = NULL;

	this->Callback = NULL;
	this->CallbackSuspended = false;

	// reset the multislot states
	this->ResetMultislotState (&this->VBState);

	// dirty everything
	this->DirtyBits = 0;
}


void STATEFILTER::IACheckDirty (void)
{
	if (this->DirtyBits & PT_DIRTY_BIT) d3d11_Context->IASetPrimitiveTopology (this->Topol);
	if (this->DirtyBits & IL_DIRTY_BIT) d3d11_Context->IASetInputLayout (this->InputLayout);
	if (this->DirtyBits & IB_DIRTY_BIT) d3d11_Context->IASetIndexBuffer (this->IndexBuffer, this->IBFormat, this->IBOffset);

	// bring all states up to date before drawing, using the batched versions of our state functions
	if (this->DirtyBits & VB_DIRTY_BIT)
	{
		// use the bulk update versions here
		if (this->VBState.MaxSlot >= this->VBState.MinSlot)
		{
			d3d11_Context->IASetVertexBuffers (this->VBState.MinSlot,
				(this->VBState.MaxSlot - this->VBState.MinSlot) + 1,
				&this->VertexBuffers[this->VBState.MinSlot],
				&this->VBStrides[this->VBState.MinSlot],
				&this->VBOffsets[this->VBState.MinSlot]);
		}

		this->ResetMultislotState (&this->VBState);
	}
}


void STATEFILTER::VSCheckDirty (shaderstate_t *ShaderState)
{
	if (ShaderState->DirtyBits & SS_DIRTY_BIT)
	{
		if (ShaderState->SSState.MaxSlot >= ShaderState->SSState.MinSlot)
		{
			d3d11_Context->VSSetSamplers (ShaderState->SSState.MinSlot,
				(ShaderState->SSState.MaxSlot - ShaderState->SSState.MinSlot) + 1,
				&ShaderState->Sampler[ShaderState->SSState.MinSlot]);
		}

		this->ResetMultislotState (&ShaderState->SSState);
	}

	if (ShaderState->DirtyBits & SRV_DIRTY_BIT)
	{
		// use the bulk update versions here
		if (ShaderState->SRVState.MaxSlot >= ShaderState->SRVState.MinSlot)
		{
			d3d11_Context->VSSetShaderResources (ShaderState->SRVState.MinSlot,
				(ShaderState->SRVState.MaxSlot - ShaderState->SRVState.MinSlot) + 1,
				&ShaderState->ShaderResourceView[ShaderState->SRVState.MinSlot]);
		}

		this->ResetMultislotState (&ShaderState->SRVState);
	}

	if (ShaderState->DirtyBits & CB_DIRTY_BIT)
	{
		// use the bulk update versions here
		if (ShaderState->CBState.MaxSlot >= ShaderState->CBState.MinSlot)
		{
			d3d11_Context->VSSetConstantBuffers (ShaderState->CBState.MinSlot,
				(ShaderState->CBState.MaxSlot - ShaderState->CBState.MinSlot) + 1,
				&ShaderState->CBuffer[ShaderState->CBState.MinSlot]);
		}

		this->ResetMultislotState (&ShaderState->CBState);
	}

	if (this->DirtyBits & VS_DIRTY_BIT) d3d11_Context->VSSetShader (this->VertexShader, NULL, 0);

	ShaderState->DirtyBits = 0;
}


void STATEFILTER::RSCheckDirty (void)
{
	if (this->DirtyBits & VP_DIRTY_BIT) d3d11_Context->RSSetViewports (1, &this->Viewport);
	if (this->DirtyBits & RS_DIRTY_BIT) d3d11_Context->RSSetState (this->RasterizerState);
}


void STATEFILTER::PSCheckDirty (shaderstate_t *ShaderState)
{
	if (ShaderState->DirtyBits & SS_DIRTY_BIT)
	{
		if (ShaderState->SSState.MaxSlot >= ShaderState->SSState.MinSlot)
		{
			d3d11_Context->PSSetSamplers (ShaderState->SSState.MinSlot,
				(ShaderState->SSState.MaxSlot - ShaderState->SSState.MinSlot) + 1,
				&ShaderState->Sampler[ShaderState->SSState.MinSlot]);
		}

		this->ResetMultislotState (&ShaderState->SSState);
	}

	if (ShaderState->DirtyBits & SRV_DIRTY_BIT)
	{
		// use the bulk update versions here
		if (ShaderState->SRVState.MaxSlot >= ShaderState->SRVState.MinSlot)
		{
			d3d11_Context->PSSetShaderResources (ShaderState->SRVState.MinSlot,
				(ShaderState->SRVState.MaxSlot - ShaderState->SRVState.MinSlot) + 1,
				&ShaderState->ShaderResourceView[ShaderState->SRVState.MinSlot]);
		}

		this->ResetMultislotState (&ShaderState->SRVState);
	}

	if (ShaderState->DirtyBits & CB_DIRTY_BIT)
	{
		// use the bulk update versions here
		if (ShaderState->CBState.MaxSlot >= ShaderState->CBState.MinSlot)
		{
			d3d11_Context->PSSetConstantBuffers (ShaderState->CBState.MinSlot,
				(ShaderState->CBState.MaxSlot - ShaderState->CBState.MinSlot) + 1,
				&ShaderState->CBuffer[ShaderState->CBState.MinSlot]);
		}

		this->ResetMultislotState (&ShaderState->CBState);
	}

	if (this->DirtyBits & PS_DIRTY_BIT) d3d11_Context->PSSetShader (this->PixelShader, NULL, 0);

	ShaderState->DirtyBits = 0;
}


void STATEFILTER::OMCheckDirty (void)
{
	if (this->DirtyBits & RT_DIRTY_BIT) d3d11_Context->OMSetRenderTargets (1, &this->RTV, this->DSV);
	if (this->DirtyBits & DS_DIRTY_BIT) d3d11_Context->OMSetDepthStencilState (this->DepthStencilState, this->StencilRef);
	if (this->DirtyBits & BS_DIRTY_BIT) d3d11_Context->OMSetBlendState (this->BlendState, this->BlendFactor, this->SampleMask);
}


void STATEFILTER::SynchronizeState (void)
{
	// we can't update states if a context has not yet been created (or has just been destroyed)
	if (!d3d11_Context) return;

	// early out if at all possible
	if (!this->DirtyBits) return;

	// check each stage in turn
	// (does the order matter and - if so - what is the optimal order?)
	// (can we generalize VS and PS?)
	if (this->DirtyBits & IAALL_DIRTY_BIT) this->IACheckDirty ();
	if (this->DirtyBits & VSALL_DIRTY_BIT) this->VSCheckDirty (&this->VSState);
	if (this->DirtyBits & RSALL_DIRTY_BIT) this->RSCheckDirty ();
	if (this->DirtyBits & PSALL_DIRTY_BIT) this->PSCheckDirty (&this->PSState);
	if (this->DirtyBits & OMALL_DIRTY_BIT) this->OMCheckDirty ();

	// the state is clean now
	this->DirtyBits = 0;
}


void STATEFILTER::SuspendCallback (void)
{
	this->CallbackSuspended = true;
}


void STATEFILTER::ResumeCallback (void)
{
	this->CallbackSuspended = false;
}


void STATEFILTER::SetOnChangeCallback (xcommand_t callback)
{
	this->Callback = callback;
}


void STATEFILTER::RSSetViewport (D3D11_VIEWPORT *vp)
{
	if (memcmp (&this->Viewport, vp, sizeof (D3D11_VIEWPORT)))
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		Q_MemCpy (&this->Viewport, vp, sizeof (D3D11_VIEWPORT));

		this->DirtyBits |= (VP_DIRTY_BIT | RSALL_DIRTY_BIT);
	}
}


void STATEFILTER::OMSetRenderTargets (ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv)
{
	if (!rtv) rtv = this->RTV;
	if (!dsv) dsv = this->DSV;

	if (this->RTV != rtv || this->DSV != dsv)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->RTV = rtv;
		this->DSV = dsv;

		this->DirtyBits |= (RT_DIRTY_BIT | OMALL_DIRTY_BIT);
	}
}


void STATEFILTER::RSSetState (ID3D11RasterizerState *rs)
{
	if (this->RasterizerState != rs)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->RasterizerState = rs;
		this->DirtyBits |= (RS_DIRTY_BIT | RSALL_DIRTY_BIT);
	}
}


void STATEFILTER::OMSetBlendState (ID3D11BlendState *bs, float bf[4], UINT samplemask)
{
	if (this->BlendState != bs || this->BlendFactor[0] != bf[0] || this->BlendFactor[1] != bf[1] ||
		this->BlendFactor[2] != bf[2] || this->BlendFactor[3] != bf[3] || this->SampleMask != samplemask)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->BlendState = bs;
		this->BlendFactor[0] = bf[0];
		this->BlendFactor[1] = bf[1];
		this->BlendFactor[2] = bf[2];
		this->BlendFactor[3] = bf[3];
		this->SampleMask = samplemask;
		this->DirtyBits |= (BS_DIRTY_BIT | OMALL_DIRTY_BIT);
	}
}


void STATEFILTER::OMSetBlendState (ID3D11BlendState *bs)
{
	if (this->BlendState != bs || this->BlendFactor[0] != 1 || this->BlendFactor[1] != 1 ||
		this->BlendFactor[2] != 1 || this->BlendFactor[3] != 1 || this->SampleMask != 0xffffffff)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->BlendState = bs;
		this->BlendFactor[0] = 1;
		this->BlendFactor[1] = 1;
		this->BlendFactor[2] = 1;
		this->BlendFactor[3] = 1;
		this->SampleMask = 0xffffffff;
		this->DirtyBits |= (BS_DIRTY_BIT | OMALL_DIRTY_BIT);
	}
}


void STATEFILTER::OMSetDepthStencilState (ID3D11DepthStencilState *dss, UINT stencilref)
{
	if (this->DepthStencilState != dss || this->StencilRef != stencilref)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->DepthStencilState = dss;
		this->StencilRef = stencilref;
		this->DirtyBits |= (DS_DIRTY_BIT | OMALL_DIRTY_BIT);
	}
}


void STATEFILTER::IASetIndexBuffer (ID3D11Buffer *indexbuffer, DXGI_FORMAT format, UINT offset)
{
	if (this->IndexBuffer != indexbuffer || this->IBFormat != format || this->IBOffset != offset)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->IndexBuffer = indexbuffer;
		this->IBFormat = format;
		this->IBOffset = offset;
		this->DirtyBits |= (IB_DIRTY_BIT | IAALL_DIRTY_BIT);
	}
}


void STATEFILTER::SetConstantBuffer (shaderstate_t *ShaderState, UINT slot, ID3D11Buffer *cbuffer, UINT dirtybit)
{
	if (ShaderState->CBuffer[slot] != cbuffer)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		ShaderState->CBuffer[slot] = cbuffer;
		ShaderState->DirtyBits |= CB_DIRTY_BIT;

		this->AccumulateMultislotState (&ShaderState->CBState, slot, dirtybit);
	}
}


void STATEFILTER::SetShaderResourceView (shaderstate_t *ShaderState, int slot, ID3D11ShaderResourceView *SRV, UINT dirtybit)
{
	if (ShaderState->ShaderResourceView[slot] != SRV)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		ShaderState->ShaderResourceView[slot] = SRV;
		ShaderState->DirtyBits |= SRV_DIRTY_BIT;

		this->AccumulateMultislotState (&ShaderState->SRVState, slot, dirtybit);
	}
}


void STATEFILTER::SetSampler (shaderstate_t *ShaderState, int slot, ID3D11SamplerState *sampler, UINT dirtybit)
{
	if (ShaderState->Sampler[slot] != sampler)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		ShaderState->Sampler[slot] = sampler;
		ShaderState->DirtyBits |= SS_DIRTY_BIT;

		this->AccumulateMultislotState (&ShaderState->SSState, slot, dirtybit);
	}
}


void STATEFILTER::VSSetConstantBuffer (UINT slot, ID3D11Buffer *cbuffer)
{
	this->SetConstantBuffer (&this->VSState, slot, cbuffer, VSALL_DIRTY_BIT);
}


void STATEFILTER::PSSetConstantBuffer (UINT slot, ID3D11Buffer *cbuffer)
{
	this->SetConstantBuffer (&this->PSState, slot, cbuffer, PSALL_DIRTY_BIT);
}


void STATEFILTER::IASetVertexBuffer (UINT slot, ID3D11Buffer *vertexbuffer, UINT stride, UINT offset)
{
	if (this->VertexBuffers[slot] != vertexbuffer || this->VBStrides[slot] != stride || this->VBOffsets[slot] != offset)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->VertexBuffers[slot] = vertexbuffer;
		this->VBStrides[slot] = stride;
		this->VBOffsets[slot] = offset;

		this->AccumulateMultislotState (&this->VBState, slot, (VB_DIRTY_BIT | IAALL_DIRTY_BIT));
	}
}


void STATEFILTER::IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY topol)
{
	if (this->Topol != topol)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->Topol = topol;
		this->DirtyBits |= (PT_DIRTY_BIT | IAALL_DIRTY_BIT);
	}
}


void STATEFILTER::IASetInputLayout (ID3D11InputLayout *layout)
{
	if (this->InputLayout != layout)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->InputLayout = layout;
		this->DirtyBits |= (IL_DIRTY_BIT | IAALL_DIRTY_BIT);
	}
}


void STATEFILTER::VSSetShaderResourceView (int slot, ID3D11ShaderResourceView *SRV)
{
	this->SetShaderResourceView (&this->VSState, slot, SRV, VSALL_DIRTY_BIT);
}


void STATEFILTER::PSSetShaderResourceView (int slot, ID3D11ShaderResourceView *SRV)
{
	this->SetShaderResourceView (&this->PSState, slot, SRV, PSALL_DIRTY_BIT);
}


void STATEFILTER::VSSetTexture (int slot, QTEXTURE *tex)
{
	if (tex)
		tex->VSSetTexture (slot);
	else this->VSSetShaderResourceView (slot, NULL);
}


void STATEFILTER::PSSetTexture (int slot, class QTEXTURE *tex)
{
	if (tex)
		tex->PSSetTexture (slot);
	else this->PSSetShaderResourceView (slot, NULL);
}


void STATEFILTER::VSSetSampler (int slot, ID3D11SamplerState *sampler)
{
	this->SetSampler (&this->VSState, slot, sampler, VSALL_DIRTY_BIT);
}


void STATEFILTER::PSSetSampler (int slot, ID3D11SamplerState *sampler)
{
	this->SetSampler (&this->PSState, slot, sampler, PSALL_DIRTY_BIT);
}


void STATEFILTER::VSSetShader (ID3D11VertexShader *shader)
{
	if (this->VertexShader != shader)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->VertexShader = shader;
		this->DirtyBits |= (VS_DIRTY_BIT | VSALL_DIRTY_BIT);
	}
}


void STATEFILTER::PSSetShader (ID3D11PixelShader *shader)
{
	if (this->PixelShader != shader)
	{
		if (this->Callback && !this->CallbackSuspended) this->Callback ();

		this->PixelShader = shader;
		this->DirtyBits |= (PS_DIRTY_BIT | PSALL_DIRTY_BIT);
	}
}


ID3D11RasterizerState *d3d_RS2DView = NULL;
ID3D11RasterizerState *d3d_RS3DView = NULL;
ID3D11RasterizerState *d3d_RSZFighting = NULL;
ID3D11BlendState *d3d_AlphaBlendEnable = NULL;
ID3D11DepthStencilState *d3d_DepthTestAndWrite = NULL;
ID3D11DepthStencilState *d3d_DepthTestNoWrite = NULL;
ID3D11DepthStencilState *d3d_DisableDepthTest = NULL;
ID3D11DepthStencilState *d3d_ShadowStencil = NULL;

// these samplers are modified by gl_texturemode, anisotropic filtering, picmip, etc etc etc
ID3D11SamplerState *d3d_DefaultSamplerWrap = NULL;
ID3D11SamplerState *d3d_DefaultSamplerClamp = NULL;

// these are generic samplers for use elsewhere
ID3D11SamplerState *d3d_SampleClampLinear = NULL;
ID3D11SamplerState *d3d_SampleWrapLinear = NULL;
ID3D11SamplerState *d3d_SampleClampPoint = NULL;
ID3D11SamplerState *d3d_SampleWrapPoint = NULL;

struct d3d_filtermode_t
{
	char *name;
	D3D11_FILTER filter;
	float maxlod;
};

d3d_filtermode_t d3d_filtermodes[] =
{
	{"GL_NEAREST", D3D11_FILTER_MIN_MAG_MIP_POINT, 0},	//p/n
	{"GL_LINEAR", D3D11_FILTER_MIN_MAG_MIP_LINEAR, 0},	//l/n
	{"GL_NEAREST_MIPMAP_NEAREST", D3D11_FILTER_MIN_MAG_MIP_POINT, FLT_MAX},	// p/p
	{"GL_LINEAR_MIPMAP_NEAREST", D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, FLT_MAX},	//l/p
	{"GL_NEAREST_MIPMAP_LINEAR", D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR, FLT_MAX},	//p/l
	{"GL_LINEAR_MIPMAP_LINEAR", D3D11_FILTER_MIN_MAG_MIP_LINEAR, FLT_MAX}			//l/l
};

d3d_filtermode_t *d3d_TextureMode = &d3d_filtermodes[5];

#define TEXFILTER_POINT		0
#define TEXFILTER_LINEAR	1

#define MIPFILTER_NONE		0
#define MIPFILTER_POINT		1
#define MIPFILTER_LINEAR	2

int D3DState_GetTexfilterMode (void)
{
	if (d3d_TextureMode->filter == D3D11_FILTER_MIN_MAG_MIP_POINT)
		return TEXFILTER_POINT;
	else if (d3d_TextureMode->filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR)
		return TEXFILTER_LINEAR;
	else if (d3d_TextureMode->filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT)
		return TEXFILTER_LINEAR;
	else return TEXFILTER_POINT;
}


int D3DState_GetMipfilterMode (void)
{
	if (!d3d_TextureMode->maxlod)
		return MIPFILTER_NONE;
	else
	{
		if (d3d_TextureMode->filter == D3D11_FILTER_MIN_MAG_MIP_POINT)
			return MIPFILTER_POINT;
		else if (d3d_TextureMode->filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT)
			return MIPFILTER_POINT;
		else if (d3d_TextureMode->filter == D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR)
			return MIPFILTER_LINEAR;
		else return MIPFILTER_LINEAR;
	}
}


void D3DState_UpdateFiltersFromMenu (int texfiltermode, int mipfiltermode)
{
	int filter = mipfiltermode * 2 + texfiltermode;

	if (d3d_TextureMode != &d3d_filtermodes[filter])
	{
		d3d_TextureMode = &d3d_filtermodes[filter];
		D3DState_UpdateDefaultSampler ();
	}
}


void D3DState_TextureMode_f (void)
{
	if (Cmd_Argc () == 1)
	{
		Con_Printf ("Available Filters:\n");

		for (int i = 0; i < 6; i++)
			Con_Printf ("%i: %s\n", i, d3d_filtermodes[i].name);

		Con_Printf ("\nCurrent filter: %s\n", d3d_TextureMode->name);
		return;
	}

	char *desiredmode = Cmd_Argv (1);
	int modenum = desiredmode[0] - '0';

	for (int i = 0; i < 6; i++)
	{
		if (!_stricmp (d3d_filtermodes[i].name, desiredmode) || i == modenum)
		{
			// reset filter and update the sampler state for it
			d3d_TextureMode = &d3d_filtermodes[i];
			D3DState_UpdateDefaultSampler ();
			return;
		}
	}

	Con_Printf ("bad filter name\n");
}


void D3DState_SaveTextureMode (std::ofstream &f)
{
	f << "gl_texturemode " << d3d_TextureMode->name << "\n";
}


cmd_t D3DState_TextureMode_Cmd ("gl_texturemode", D3DState_TextureMode_f);
cvar_t gl_picmip ("gl_picmip", "0", 0, D3DState_CvarSamplerUpdater);
cvar_t gl_softwarequakemipmaps ("gl_softwarequakemipmaps", "0", 0, D3DState_CvarSamplerUpdater);

// consistency with DP and FQ
cvar_t r_anisotropicfilter ("gl_texture_anisotropy", "1", CVAR_ARCHIVE, D3DState_CvarSamplerUpdater);
cvar_alias_t r_anisotropicfilter_alias ("r_anisotropicfilter", &r_anisotropicfilter);
cvar_alias_t gl_anisotropic_filter_alias ("gl_anisotropic_filter", &r_anisotropicfilter);

void D3DState_CreateSampler (ID3D11SamplerState **ss, char *name, D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE mode, UINT anisotropy, FLOAT minlod, FLOAT maxlod)
{
	SAFE_RELEASE (ss[0]);

	D3D11_SAMPLER_DESC sdesc = {
		filter,
		mode,
		mode,
		mode,
		0,
		anisotropy,
		D3D11_COMPARISON_NEVER,
		{0, 0, 0, 0},
		minlod,
		maxlod
	};

	d3d11_Device->CreateSamplerState (&sdesc, ss);
	D3DMisc_SetObjectName (ss[0], name);
}


void D3DState_UpdateDefaultSampler (void)
{
	// this is called during filesystem init so don't let it happen if we don't have a device yet.
	// that's OK because it will be force-called when states start up and we'll have a device then.
	if (!d3d11_Device) return;

	// baseline state
	UINT MaxAnisotropy = 1;

	if (r_anisotropicfilter.integer > 8)
		MaxAnisotropy = 16;
	else if (r_anisotropicfilter.integer > 4)
		MaxAnisotropy = 8;
	else if (r_anisotropicfilter.integer > 2)
		MaxAnisotropy = 4;
	else if (r_anisotropicfilter.integer > 1)
		MaxAnisotropy = 2;
	else MaxAnisotropy = 1;

	FLOAT MinLOD = gl_picmip.value > 0 ? gl_picmip.value : -FLT_MAX;
	FLOAT MaxLOD = d3d_TextureMode->maxlod;
	D3D11_FILTER Filter = (MaxAnisotropy > 1) ? D3D11_FILTER_ANISOTROPIC : d3d_TextureMode->filter;

	if (gl_softwarequakemipmaps.value) MaxLOD = MinLOD + 4;
	if (MaxLOD < MinLOD) MaxLOD = MinLOD;

	D3DState_CreateSampler (&d3d_DefaultSamplerWrap, "d3d_DefaultSamplerWrap", Filter, D3D11_TEXTURE_ADDRESS_WRAP, MaxAnisotropy, MinLOD, MaxLOD);
	D3DState_CreateSampler (&d3d_DefaultSamplerClamp, "d3d_DefaultSamplerClamp", Filter, D3D11_TEXTURE_ADDRESS_CLAMP, MaxAnisotropy, MinLOD, MaxLOD);

	vid.RecalcRefdef = true;
	Con_DPrintf ("Updated sampler states\n");
}


void D3DState_CvarSamplerUpdater (cvar_t *var)
{
	D3DState_UpdateDefaultSampler ();
}


void D3DState_CreateDepthStencilState (ID3D11DepthStencilState **ds, bool enabledepth, bool writedepth, bool enablestencil, char *name)
{
	SAFE_RELEASE (ds[0]);

	extern cvar_t r_draworder;
	D3D11_DEPTH_STENCIL_DESC ddesc;

	// http://msdn.microsoft.com/en-us/library/windows/desktop/bb205120%28v=vs.85%29.aspx
	// someone seems to be lying - if DepthEnable is false then DepthWriteMask will always
	// pickup D3D11_DEPTH_WRITE_MASK_ALL, no matter what you specify...
	ddesc.DepthEnable = enabledepth ? TRUE : FALSE;
	ddesc.DepthWriteMask = writedepth ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;

	// it's important to not switch these during the frame for Hi-Z to be able to work properly
	// disabling depth testing will ensure that all works out OK.
	if (r_draworder.integer)
		ddesc.DepthFunc = D3D11_COMPARISON_GREATER;
	else ddesc.DepthFunc = D3D11_COMPARISON_LESS;

	/*
	if (!enabledepth && !writedepth)
	{
		ddesc.DepthEnable = TRUE;
		ddesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		ddesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	}
	*/

	if (enablestencil)
	{
		ddesc.StencilEnable = TRUE;

		ddesc.StencilReadMask = 0x02;
		ddesc.StencilWriteMask = 0xff;

		ddesc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
		ddesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		ddesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT;
		ddesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;

		ddesc.BackFace.StencilFunc = D3D11_COMPARISON_EQUAL;
		ddesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		ddesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT;
		ddesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	}
	else
	{
		ddesc.StencilEnable = FALSE;

		ddesc.StencilReadMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
		ddesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_READ_MASK;

		ddesc.FrontFace.StencilFunc = D3D11_COMPARISON_NEVER;
		ddesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		ddesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		ddesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;

		ddesc.BackFace.StencilFunc = D3D11_COMPARISON_NEVER;
		ddesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		ddesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		ddesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	}

	d3d11_Device->CreateDepthStencilState (&ddesc, ds);
	D3DMisc_SetObjectName (ds[0], name);
}


void D3DState_CreateDepthStencilStates (void)
{
	D3DState_CreateDepthStencilState (&d3d_DepthTestAndWrite, true, true, false, "d3d_DepthTestAndWrite");
	D3DState_CreateDepthStencilState (&d3d_DepthTestNoWrite, true, false, false, "d3d_DepthTestNoWrite");
	D3DState_CreateDepthStencilState (&d3d_DisableDepthTest, false, false, false, "d3d_DisableDepthTest");
	D3DState_CreateDepthStencilState (&d3d_ShadowStencil, true, false, true, "d3d_ShadowStencil");

	vid.RecalcRefdef = true;
	Con_DPrintf ("Updated depth/stencil states\n");
}


void D3DState_CreateRasterizer (ID3D11RasterizerState **rs, char *name, D3D11_FILL_MODE fill, D3D11_CULL_MODE cull, INT bias, FLOAT biasclamp, FLOAT ssdb)
{
	SAFE_RELEASE (rs[0]);

	D3D11_RASTERIZER_DESC rdesc;

	rdesc.FillMode = fill;
	rdesc.CullMode = cull;
	rdesc.FrontCounterClockwise = FALSE;
	rdesc.DepthBias = bias;
	rdesc.DepthBiasClamp = biasclamp;
	rdesc.SlopeScaledDepthBias = ssdb;
	rdesc.DepthClipEnable = FALSE;
	rdesc.ScissorEnable = FALSE;
	rdesc.MultisampleEnable = FALSE;
	rdesc.AntialiasedLineEnable = FALSE;

	d3d11_Device->CreateRasterizerState (&rdesc, rs);
	D3DMisc_SetObjectName (rs[0], name);
}


#define ZFIGHTING_PARAMS \
	r_depthbias.integer, r_depthbiasclamp.value, r_slopescaleddepthbias.value

void D3DState_CreateRasterizerStates (void)
{
	extern cvar_t r_depthbias;
	extern cvar_t r_depthbiasclamp;
	extern cvar_t r_slopescaleddepthbias;
	extern cvar_t r_wireframe;
	extern bool r_usingdepthbias;

	D3DState_CreateRasterizer (&d3d_RS2DView, "d3d_RS2DView", D3D11_FILL_SOLID, D3D11_CULL_BACK, 0, 0, 0);

	if (r_wireframe.integer)
	{
		D3DState_CreateRasterizer (&d3d_RS3DView, "d3d_RS3DView", D3D11_FILL_WIREFRAME, D3D11_CULL_BACK, 0, 0, 0);
		D3DState_CreateRasterizer (&d3d_RSZFighting, "d3d_RSZFighting", D3D11_FILL_WIREFRAME, D3D11_CULL_BACK, ZFIGHTING_PARAMS);
	}
	else
	{
		D3DState_CreateRasterizer (&d3d_RS3DView, "d3d_RS3DView", D3D11_FILL_SOLID, D3D11_CULL_BACK, 0, 0, 0);
		D3DState_CreateRasterizer (&d3d_RSZFighting, "d3d_RSZFighting", D3D11_FILL_SOLID, D3D11_CULL_BACK, ZFIGHTING_PARAMS);
	}

	r_usingdepthbias = (r_depthbias.integer || r_depthbiasclamp.value || r_slopescaleddepthbias.value);

	vid.RecalcRefdef = true;
	Con_DPrintf ("Updated rasterizer states\n");
}


void D3DState_WireFrameChanged (cvar_t *var)
{
	D3DState_CreateRasterizerStates ();
}


void D3DState_DepthBiasChanged (cvar_t *var)
{
	D3DState_CreateRasterizerStates ();
}


void D3DState_DrawOrderChanged (cvar_t *var)
{
	D3DState_CreateDepthStencilStates ();
}


void D3DState_CreateBlend (ID3D11BlendState **bs, char *name, BOOL enable, D3D11_BLEND src, D3D11_BLEND dst)
{
	SAFE_RELEASE (bs[0]);

	D3D11_BLEND_DESC bdesc;

	bdesc.AlphaToCoverageEnable = FALSE;
	bdesc.IndependentBlendEnable = FALSE;

	bdesc.RenderTarget[0].BlendEnable = enable;

	bdesc.RenderTarget[0].SrcBlend = src;
	bdesc.RenderTarget[0].DestBlend = dst;
	bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;

	bdesc.RenderTarget[0].SrcBlendAlpha = src;
	bdesc.RenderTarget[0].DestBlendAlpha = dst;
	bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

	bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	d3d11_Device->CreateBlendState (&bdesc, bs);
	D3DMisc_SetObjectName (bs[0], name);
}


void D3D11States_Init (void)
{
	// create generic samplers
	D3DState_CreateSampler (&d3d_SampleWrapLinear, "d3d_SampleWrapLinear", D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	D3DState_CreateSampler (&d3d_SampleWrapPoint, "d3d_SampleWrapPoint", D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP);
	D3DState_CreateSampler (&d3d_SampleClampLinear, "d3d_SampleClampLinear", D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
	D3DState_CreateSampler (&d3d_SampleClampPoint, "d3d_SampleClampPoint", D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	// create other user-changable states with their default values
	D3DState_CreateRasterizerStates ();
	D3DState_CreateDepthStencilStates ();
	D3DState_UpdateDefaultSampler ();
	D3DState_CreateBlend (&d3d_AlphaBlendEnable, "d3d_AlphaBlendEnable", TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA);
	vid.RecalcRefdef = true;
}


void D3D11States_Shutdown (void)
{
	SAFE_RELEASE (d3d_SampleClampLinear);
	SAFE_RELEASE (d3d_SampleWrapLinear);
	SAFE_RELEASE (d3d_SampleClampPoint);
	SAFE_RELEASE (d3d_SampleWrapPoint);

	SAFE_RELEASE (d3d_DefaultSamplerWrap);
	SAFE_RELEASE (d3d_DefaultSamplerClamp);
	SAFE_RELEASE (d3d_DepthTestAndWrite);
	SAFE_RELEASE (d3d_DepthTestNoWrite);
	SAFE_RELEASE (d3d_DisableDepthTest);
	SAFE_RELEASE (d3d_ShadowStencil);
	SAFE_RELEASE (d3d_AlphaBlendEnable);
	SAFE_RELEASE (d3d_RS2DView);
	SAFE_RELEASE (d3d_RS3DView);
	SAFE_RELEASE (d3d_RSZFighting);
}


CD3DInitShutdownHandler d3d_StatesHandler ("states", D3D11States_Init, D3D11States_Shutdown);

