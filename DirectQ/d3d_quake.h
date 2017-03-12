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


extern D3D_FEATURE_LEVEL d3d11_FeatureLevel;

void D3DMain_SetupProjection (float fovx, float fovy, float zn, float zf, float depthhackscale = 1.0f);
void D3DVid_FlushStates (void);

extern ID3D11SamplerState *d3d_SampleClampLinear;
extern ID3D11SamplerState *d3d_SampleWrapLinear;
extern ID3D11SamplerState *d3d_SampleClampPoint;
extern ID3D11SamplerState *d3d_SampleWrapPoint;

float *D3DMisc_GetColorFromRGBA (byte *rgba);
void D3DMisc_DrawCommon (UINT VertexCount = 0, UINT StartVertexLocation = 0);
void D3DMisc_DrawIndexedCommon (UINT IndexCount = 0, UINT StartIndexLocation = 0, INT BaseVertexLocation = 0);
void D3DMisc_DrawInstancedCommon (UINT VertexCountPerInstance = 0, UINT InstanceCount = 0, UINT StartVertexLocation = 0, UINT StartInstanceLocation = 0);
void D3DMisc_DrawIndexedInstancedCommon (UINT IndexCountPerInstance = 0, UINT InstanceCount = 0, UINT StartIndexLocation = 0, INT BaseVertexLocation = 0, UINT StartInstanceLocation = 0);
bool D3DMisc_OverridePS (void);

extern DXGI_SAMPLE_DESC d3d11_SampleDesc;

extern cvar_t r_wireframe;
extern cvar_t r_drawflat;
extern cvar_t r_showdepth;

extern ID3D11PixelShader *d3d_WireFramePixelShader;
extern ID3D11PixelShader *d3d_DrawFlatPixelShader;
extern ID3D11PixelShader *d3d_ShowDepthPixelShader;

// fucking quake weenie fog
extern float RealFogDensity;
extern float *RealFogColor;
extern float SkyFogDensity;

// helper for to combat stupidity
D3D11_BOX *D3DMisc_Box (int left, int right, int top = 0, int bottom = 1, int front = 0, int back = 1);

// object lifetime management - this enables the debug layer to track what was and wasn't destroyed
void D3DMisc_SetObjectName (ID3D11DeviceChild *pObject, char *name);

// video
extern IDXGISwapChain *d3d11_SwapChain;
extern ID3D11Device *d3d11_Device;
extern ID3D11DeviceContext *d3d11_Context;

bool D3DVid_IsCreated (void);


extern ID3D11RenderTargetView *d3d11_RenderTargetView;
extern ID3D11DepthStencilView *d3d11_DepthStencilView;

void D3DState_CreateSampler (ID3D11SamplerState **ss, char *name, D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE mode, UINT anisotropy = 1, FLOAT minlod = -FLT_MAX, FLOAT maxlod = FLT_MAX);
void D3DState_CreateBlend (ID3D11BlendState **bs, char *name, BOOL enable, D3D11_BLEND src, D3D11_BLEND dst);
void D3DState_CreateRasterizer (ID3D11RasterizerState **rs, char *name, D3D11_FILL_MODE fill, D3D11_CULL_MODE cull, INT bias, FLOAT biasclamp, FLOAT ssdb);

#define VH_INIT				1
#define VH_SHUTDOWN			2

void D3DVid_RunHandlers (int mode);

typedef void (*d3d11func_t) (void);

class CD3DInitShutdownHandler
{
public:
	CD3DInitShutdownHandler (
		char *name = NULL,
		d3d11func_t oninit = NULL,
		d3d11func_t onshutdown = NULL
	);
};


// textures
class QTEXTURE
{
public:
	QTEXTURE (void);
	QTEXTURE (char *_identifier, int _width, int _height, int _flags, byte *_hash);
	~QTEXTURE (void);

	void Release (void);
	void SetObjectNames (void);
	void SetObjectNames (char *texname, char *srvname);
	bool MatchWith (char *_identifier, int _width, int _height, int _flags, byte *_hash);
	void SetUnused ();
	void AddFlag (int flag);
	void DelFlag (int flag);
	bool HasFlag (int flag);
	bool ShouldFlush (void);
	void Reuse (void);
	bool HasTexture (void);
	void GetTextureDesc (D3D11_TEXTURE2D_DESC *desc);
	void SubImageFrom (UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch);
	void SubImageTo (ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, UINT SrcSubresource, const D3D11_BOX *pSrcBox);
	void SubImageFrom (UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox);

	void Upload (byte *data, int width, int height, int flags, unsigned int *palette);
	void CreateTextureAndSRV (D3D11_TEXTURE2D_DESC *desc, D3D11_SUBRESOURCE_DATA *srd);
	void CreateDefaultUAV (ID3D11UnorderedAccessView **uav);
	void CreateRenderTarget (ID3D11RenderTargetView **rtv);
	void FromMemory (void *data, int size, int flags);
	bool FromMemory (void *data, int size, D3DX11_IMAGE_LOAD_INFO *ili, int flags);
	void FromStaging (ID3D11Texture2D *pTexture, int flags);
	void FromResource (int resourceid, int flags);
	void CopyFrom (ID3D11Texture2D *src);

	void VSSetTexture (int slot);
	void PSSetTexture (int slot);

	void RGBA32FromLowestMip (byte *dest);

	bool GetMapping (D3D11_MAPPED_SUBRESOURCE *mr, int sr);
	void Unmap (int sr);

	void GenerateMips (void);
	static D3D11_TEXTURE2D_DESC *MakeTextureDesc (int width, int height, int flags);
	static D3DX11_IMAGE_LOAD_INFO *ImageLoadInfo ();
	static D3D11_SUBRESOURCE_DATA *BuildMipLevels (byte *data, int width, int height, int flags);
	static void SetMipData (D3D11_SUBRESOURCE_DATA *srd, void *data, int pitch);
	static unsigned int *ToRGBA (byte *data, int width, int height, unsigned int *palette);
	bool CreateExternal (byte *data, int type, int flags);
	bool LoadExternal (char *filename, char **paths, int flags);
	bool TryLoadExternal (char **_paths);
	bool TryLoadNative (byte *_data);
	static QTEXTURE *Load (char *_identifier, int _width, int _height, byte *_data, int _flags, char **_paths = NULL);
	static void Flush (void);
	static bool HasLuma (byte *_data, int _width, int _height, int _flags = 0);

	static QTEXTURE WhiteTexture;
	static QTEXTURE GreyTexture;
	static QTEXTURE BlackTexture;

private:
	void InitData (char *_identifier, int _width, int _height, int _flags, byte *_hash);
	char identifier[64];
	int width;
	int height;
	int flags;
	byte hash[16];
	int LastUsage;

	ID3D11Texture2D *Texture2D;
	ID3D11ShaderResourceView *SRV;
};

QTEXTURE *D3DRTT_CopyScreen (void);

// images
void D3DImage_Compress4to3WithSwapToBGRandGamma (byte *rgba, byte *bgr, int width, int height, byte *gammatable);
void D3DImage_Compress4to3WithGamma (byte *bgra, byte *bgr, int width, int height, byte *gammatable);
void D3DImage_CompressRowPitch (unsigned *data, int width, int height, int pitch);
void D3DImage_MipMap (byte *in, byte *out, int width, int height);
void D3DImage_Resample (unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight);
void D3DImage_AlphaEdgeFix (byte *data, int width, int height);
void D3DImage_AlphaMask (byte *data, int size);

void D3DImage_RotateTexelsInPlace (unsigned int *texels, int size);
void D3DImage_RotateTexels (unsigned int *texels, int width, int height);
void D3DImage_MirrorTexels (unsigned int *texels, int width, int height);
void D3DImage_FlipTexels (unsigned int *texels, int width, int height);
void D3DImage_AlignCubeFace (unsigned int *texels, int width, int height, int face);

#define CM_R	1
#define CM_G	2
#define CM_B	4
#define CM_A	8

void D3DImage_Blur (unsigned char *data, int width, int height, int radius, int channelmask);
void D3DImage_Power (unsigned char *data, int width, int height, int power, int channelmask);

// buffers
class QBUFFERFACTORY
{
public:
	void CreateConstantBuffer (UINT size, ID3D11Buffer **buf, char *name, void *initdata = NULL);
	void CreateVertexBuffer (UINT vertexsize, UINT numverts, ID3D11Buffer **buf, char *name, void *initdata = NULL);
	void CreateIndexBuffer (UINT indexsize, UINT numindexes, ID3D11Buffer **buf, char *name, void *initdata = NULL);
	void CreateInstanceBuffer (UINT vertexsize, UINT numverts, ID3D11Buffer **buf, char *name);
	void CreateGenericBuffer (D3D11_USAGE usage, UINT bindflags, UINT access, UINT size, void *initdata, ID3D11Buffer **buf, char *name);
};


extern QBUFFERFACTORY BufferFactory;


struct shaderdefine_t
{
	int Flag;
	D3D10_SHADER_MACRO Define;
};

// encodes a #define in a shaderdefine_t struct
#define ENCODE_DEFINE(def, val) {def, {#def, val}}


// shaders
class QSHADERFACTORY
{
public:
	QSHADERFACTORY (int resourceid);
	~QSHADERFACTORY ();

	// all of these should call RegisterResource which adds the resource to a list so that it can be auto-Released at shutdown without having to worry about doing so in each module
	void CreateVertexShader (ID3D11VertexShader **vs, char *entrypoint, const D3D10_SHADER_MACRO *defines = NULL);
	void CreatePixelShader (ID3D11PixelShader **ps, char *entrypoint, const D3D10_SHADER_MACRO *defines = NULL);
	void CreateComputeShader (ID3D11ComputeShader **cs, char *entrypoint, const D3D10_SHADER_MACRO *defines = NULL);
	void CreateGeometryShader (ID3D11GeometryShader **gs, char *entrypoint, const D3D10_SHADER_MACRO *defines = NULL);
	void CreateInputLayout (ID3D11InputLayout **lo, char *loname, D3D11_INPUT_ELEMENT_DESC *lodesc, int loitems);

	// encodes the flags into an array of D3D10_SHADER_MACRO from a master list of defines
	D3D10_SHADER_MACRO *EncodeDefines (const shaderdefine_t *shaderdefines, int numshaderdefines, int flags);

private:
	HRESULT CompileGeneric (const char *entrypoint, const char *profile, const D3D10_SHADER_MACRO *defines = NULL);

	char *ShaderText;
	ID3D10Blob *ShaderBlob;
};


#define MAKELAYOUTELEMENT(name, index, fmt, slot, steprate) \
{ \
	name, \
	index, \
	fmt, \
	slot, \
	D3D11_APPEND_ALIGNED_ELEMENT, \
	((steprate > 0) ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA), \
	steprate \
}

#define LAYOUTPARAMS(lo) lo, ARRAYLENGTH (lo)

// state
extern ID3D11RasterizerState *d3d_RS2DView;
extern ID3D11RasterizerState *d3d_RS3DView;
extern ID3D11BlendState *d3d_AlphaBlendEnable;
extern ID3D11DepthStencilState *d3d_DepthTestAndWrite;
extern ID3D11DepthStencilState *d3d_DepthTestNoWrite;
extern ID3D11DepthStencilState *d3d_DisableDepthTest;
extern ID3D11DepthStencilState *d3d_ShadowStencil;

void D3DState_UpdateDefaultSampler (void);
void D3DState_CvarSamplerUpdater (cvar_t *var);
extern ID3D11SamplerState *d3d_DefaultSamplerWrap;
extern ID3D11SamplerState *d3d_DefaultSamplerClamp;

// scene
class QVIEWPORT : public D3D11_VIEWPORT
{
public:
	QVIEWPORT (float x, float y, float w, float h, float zn, float zf);
};


struct d3d_multislot_t
{
	int MinSlot;
	int MaxSlot;
};


#define RS_DIRTY_BIT		(1 << 0)
#define DS_DIRTY_BIT		(1 << 1)
#define BS_DIRTY_BIT		(1 << 2)
#define IB_DIRTY_BIT		(1 << 3)
#define IL_DIRTY_BIT		(1 << 4)
#define PT_DIRTY_BIT		(1 << 5)
#define VB_DIRTY_BIT		(1 << 6)
#define VS_DIRTY_BIT		(1 << 7)
#define PS_DIRTY_BIT		(1 << 8)
#define VP_DIRTY_BIT		(1 << 9)
#define RT_DIRTY_BIT		(1 << 10)

#define IAALL_DIRTY_BIT		(1 << 27)
#define VSALL_DIRTY_BIT		(1 << 28)
#define RSALL_DIRTY_BIT		(1 << 29)
#define PSALL_DIRTY_BIT		(1 << 30)
#define OMALL_DIRTY_BIT		(1 << 31)

// per-shader states
#define SRV_DIRTY_BIT		(1 << 0)
#define SS_DIRTY_BIT		(1 << 1)
#define CB_DIRTY_BIT		(1 << 2)

struct shaderstate_t
{
	ID3D11ShaderResourceView *ShaderResourceView[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
	ID3D11SamplerState *Sampler[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
	ID3D11Buffer *CBuffer[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];

	d3d_multislot_t SRVState;
	d3d_multislot_t SSState;
	d3d_multislot_t CBState;

	UINT DirtyBits;
};


// dynamic state
// this is defined as a class so that we can create multiple state filters in the event of ever needing to use them in deferred contexts
class STATEFILTER
{
public:
	STATEFILTER (void);

	void IASetIndexBuffer (ID3D11Buffer *indexbuffer, DXGI_FORMAT format, UINT offset);
    void IASetVertexBuffer (UINT slot, ID3D11Buffer *vertexbuffer, UINT stride, UINT offset);
	void IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY topol);
	void IASetInputLayout (ID3D11InputLayout *layout);

    void VSSetConstantBuffer (UINT slot, ID3D11Buffer *cbuffer);
    void PSSetConstantBuffer (UINT slot, ID3D11Buffer *cbuffer);

	void VSSetTexture (int slot, class QTEXTURE *tex);
	void PSSetTexture (int slot, class QTEXTURE *tex);

	void VSSetShaderResourceView (int slot, ID3D11ShaderResourceView *SRV);
	void PSSetShaderResourceView (int slot, ID3D11ShaderResourceView *SRV);

	void VSSetSampler (int slot, ID3D11SamplerState *sampler);
	void PSSetSampler (int slot, ID3D11SamplerState *sampler);

	void VSSetShader (ID3D11VertexShader *shader);
	void PSSetShader (ID3D11PixelShader *shader);

	void OMSetDepthStencilState (ID3D11DepthStencilState *dss, UINT stencilref = 0);
	void OMSetBlendState (ID3D11BlendState *bs, float bf[4], UINT samplemask = 0xffffffff);
	void OMSetBlendState (ID3D11BlendState *bs);
	void RSSetState (ID3D11RasterizerState *rs);

	void SetOnChangeCallback (xcommand_t callback);
	void ClearState (void);

	void SuspendCallback (void);
	void ResumeCallback (void);

	void SynchronizeState (void);

	// no GS so just using a single viewport here
	void RSSetViewport (D3D11_VIEWPORT *vp);

	// not using MRT so just a single rtv here
	void OMSetRenderTargets (ID3D11RenderTargetView *rtv = NULL, ID3D11DepthStencilView *dsv = NULL);

private:
	void IACheckDirty (void);
	void VSCheckDirty (shaderstate_t *ShaderState);
	void RSCheckDirty (void);
	void PSCheckDirty (shaderstate_t *ShaderState);
	void OMCheckDirty (void);
	void ClearShaderState (shaderstate_t *ShaderState);

	void SetConstantBuffer (shaderstate_t *ShaderState, UINT slot, ID3D11Buffer *cbuffer, UINT dirtybit);
	void SetShaderResourceView (shaderstate_t *ShaderState, int slot, ID3D11ShaderResourceView *SRV, UINT dirtybit);
	void SetSampler (shaderstate_t *ShaderState, int slot, ID3D11SamplerState *sampler, UINT dirtybit);

	void AccumulateMultislotState (d3d_multislot_t *slotstate, UINT slot, UINT dirtybit);
	void ResetMultislotState (d3d_multislot_t *slotstate);

	ID3D11RenderTargetView *RTV;
	ID3D11DepthStencilView *DSV;

	D3D11_VIEWPORT Viewport;

	ID3D11DepthStencilState *DepthStencilState;
	UINT StencilRef;

	ID3D11BlendState *BlendState;
	float BlendFactor[4];
	UINT SampleMask;

	ID3D11RasterizerState *RasterizerState;

	ID3D11VertexShader *VertexShader;
	ID3D11PixelShader *PixelShader;

	shaderstate_t VSState;
	shaderstate_t PSState;

	ID3D11InputLayout *InputLayout;
	D3D11_PRIMITIVE_TOPOLOGY Topol;

	ID3D11Buffer *VertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	UINT VBStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	UINT VBOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	d3d_multislot_t VBState;

	ID3D11Buffer *IndexBuffer;
	DXGI_FORMAT IBFormat;
	UINT IBOffset;

	xcommand_t Callback;
	bool CallbackSuspended;

	UINT DirtyBits;
};


extern STATEFILTER *d3d11_State;


// palette hackery
struct palettedef_t
{
	unsigned int luma11[256];
	unsigned int standard11[256];
	int darkindex;
};

extern palettedef_t d3d_QuakePalette;


// this is our matrix interface now
extern QMATRIX d3d_WorldMatrix;
extern QMATRIX d3d_ModelViewProjMatrix;

extern HRESULT hr;

extern cvar_t gl_fullbrights;
extern cvar_t r_overbright;

// crap from the old glquake.h
#define ALIAS_BASE_SIZE_RATIO (1.0 / 11.0)
#define BACKFACE_EPSILON	0.01

// screen size info
extern	refdef_t	r_refdef;
extern	texture_t	*r_notexture_mip;

extern	cvar_t	r_norefresh;
extern	cvar_t	r_drawentities;
extern	cvar_t	r_drawworld;
extern	cvar_t	r_drawviewmodel;
extern	cvar_t	r_speeds;
extern	cvar_t	r_waterwarp;
extern	cvar_t	r_lightmap;
extern	cvar_t	r_fullbright;
extern	cvar_t	r_shadows;
extern	cvar_t	r_wateralpha;
extern	cvar_t	r_dynamic;
extern	cvar_t	r_novis;

extern	cvar_t	gl_cull;
extern	cvar_t	gl_smoothmodels;
extern	cvar_t	gl_affinemodels;
extern	cvar_t	gl_polyblend;
extern	cvar_t	gl_nocolors;
extern	cvar_t	gl_doubleeyes;


// video
bool D3DVid_BeginRendering (void);
void D3DVid_EndRendering (void);

extern DXGI_MODE_DESC d3d_CurrentMode;


// textures
#define IMAGE_MIPMAP		(1 << 1)
#define IMAGE_ALPHA			(1 << 2)
#define IMAGE_32BIT			(1 << 3)
#define IMAGE_PRESERVE		(1 << 4)
#define IMAGE_LIQUID		(1 << 5)
#define IMAGE_BSP			(1 << 6)
#define IMAGE_ALIAS			(1 << 7)
#define IMAGE_SPRITE		(1 << 8)
#define IMAGE_LUMA			(1 << 9)
#define IMAGE_EXTERNAL		(1 << 10)
#define IMAGE_NOEXTERN		(1 << 11)
#define IMAGE_HALFLIFE		(1 << 12)
#define IMAGE_PADDABLE		(1 << 14)
#define IMAGE_PADDED		(1 << 15)
#define IMAGE_SYSMEM		(1 << 16)
#define IMAGE_FENCE			(1 << 18)
#define IMAGE_SCALE2X		(1 << 19)
#define IMAGE_SKYBOX		(1 << 20)
#define IMAGE_ALPHAMASK		(1 << 21)
#define IMAGE_STAGING		(1 << 22)
#define IMAGE_UPDATE		(1 << 23)
#define IMAGE_READWRITE		(1 << 24)
#define IMAGE_EXTERNONLY	(1 << 25)
#define IMAGE_IQM			(1 << 26)

int D3DImage_PowerOf2Size (int size);

// state changes
void D3DDraw_Begin2D (void);

struct d3d_renderdef_t
{
	int presentcount;
	int framecount;
	int visframecount;
	int entframecount;
	float fps;

	// r_speeds counts
	int	brush_polys;
	int alias_polys;
	int numdrawprim;
	int numnode;
	int numleaf;
	int numdlight;

	bool rebuildworld;

	mleaf_t *viewleaf;
	mleaf_t *oldviewleaf;
	int *lastgoodcontents;

	// if ent->relinkframe == this the ent is not added (because it already was)
	int relinkframe;

	entity_t worldentity;

	// models who's RegistrationSequence is == this have been touched on this map load
	int RegistrationSequence;
};

extern d3d_renderdef_t d3d_RenderDef;

void D3DAlpha_AddToList (entity_t *ent);
void D3DAlpha_AddToList (struct emitter_t *particle);
void D3DAlpha_RenderList (void);
void D3DAlpha_AddToList (msurface_t *surf, entity_t *ent, float *midpoint);

bool R_CullBox (cullinfo_t *ci);
bool R_CullSphere (float *center, float radius, int clipflags);
int R_PlaneSide (cullinfo_t *ci, mplane_t *p);

void D3DSurf_DrawTextureChain (texture_t *tex);

#define LIGHTMAP_SIZE		128

extern cvar_t vid_maximumframelatency;
extern cvar_t r_detailtextures;

extern ID3D11ShaderResourceView *d3d_NoiseSRV;


class QEDICTLIST
{
public:
	entity_t **Edicts;
	int NumEdicts;

	QEDICTLIST (void);
	~QEDICTLIST (void);
	void BeginFrame (void);
	void AddEntity (entity_t *ent);
};


extern QEDICTLIST d3d_AliasEdicts;
extern QEDICTLIST d3d_BrushEdicts;
extern QEDICTLIST d3d_MergeEdicts;
extern QEDICTLIST d3d_IQMEdicts;


// shared dynamic vertex buffer for all dynamic objects
struct QINSTANCE
{
	static ID3D11Buffer *VertexBuffer;
	static int BufferMax;
	static int MapOffset;
};


