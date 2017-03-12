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
#include <vector>
#include <algorithm>

std::vector<QTEXTURE *> d3d11_Textures;

QTEXTURE QTEXTURE::WhiteTexture;
QTEXTURE QTEXTURE::GreyTexture;
QTEXTURE QTEXTURE::BlackTexture;

byte *D3DImage_LoadTGA (byte *f, int *width, int *height);
byte *D3DImage_LoadPCX (byte *f, int *width, int *height);

//							  0       1      2      3      4      5      6
char *textureextensions[] = {"link", "dds", "tga", "bmp", "png", "jpg", "pcx", NULL};
char *defaultpaths[] = {"textures/", "", NULL};

#define TEXTYPE_LINK	0
#define TEXTYPE_DDS		1
#define TEXTYPE_TGA		2
#define TEXTYPE_BMP		3
#define TEXTYPE_PNG		4
#define TEXTYPE_JPG		5
#define TEXTYPE_PCX		6

cvar_t gl_maxtextureretention ("gl_maxtextureretention", 3, CVAR_ARCHIVE);


void D3D11Texture_Init (void)
{
	byte data[4];

	data[0] = data[1] = data[2] = 255;
	data[3] = 255;

	QTEXTURE::WhiteTexture.Upload (data, 1, 1, IMAGE_32BIT, NULL);

	data[0] = data[1] = data[2] = 128;
	data[3] = 255;

	QTEXTURE::GreyTexture.Upload (data, 1, 1, IMAGE_32BIT, NULL);

	data[0] = data[1] = data[2] = 0;
	data[3] = 255;

	QTEXTURE::BlackTexture.Upload (data, 1, 1, IMAGE_32BIT, NULL);
}


void D3D11Texture_Shutdown (void)
{
	QTEXTURE::WhiteTexture.Release ();
	QTEXTURE::GreyTexture.Release ();
	QTEXTURE::BlackTexture.Release ();

	for (int i = 0; i < d3d11_Textures.size (); i++)
		SAFE_DELETE (d3d11_Textures[i]);

	d3d11_Textures.clear ();
}


CD3DInitShutdownHandler d3d11_TextureHandler ("texture", D3D11Texture_Init, D3D11Texture_Shutdown);


// checksums of textures that need hacking...
struct hashhacker_t
{
	byte hash[16];
	char *idcat;
	char *idcpy;
};

// hardcoded data about textures that got screwed up in id1
hashhacker_t d3d_HashHacks[] =
{
	{{209, 191, 162, 164, 213, 63, 224, 73, 227, 251, 229, 137, 43, 60, 25, 138}, "_cable", NULL},
	{{52, 114, 210, 88, 38, 70, 116, 171, 89, 227, 115, 137, 102, 79, 193, 35}, "_bolt", NULL},
	{{35, 233, 88, 189, 135, 188, 152, 69, 221, 125, 104, 132, 51, 91, 22, 15}, "_arc", NULL},
	{{207, 93, 199, 54, 82, 58, 152, 177, 67, 18, 185, 231, 214, 4, 164, 99}, "_x", NULL},
	{{27, 95, 227, 196, 123, 235, 244, 145, 211, 222, 14, 190, 37, 255, 215, 107}, "_arc", NULL},
	{{47, 119, 108, 18, 244, 34, 166, 42, 207, 217, 179, 201, 114, 166, 199, 35}, "_double", NULL},
	{{199, 119, 111, 184, 133, 111, 68, 52, 169, 1, 239, 142, 2, 233, 192, 15}, "_back", NULL},
	{{60, 183, 222, 11, 163, 158, 222, 195, 124, 161, 201, 158, 242, 30, 134, 28}, "_rune", NULL},
	{{220, 67, 132, 212, 3, 131, 54, 160, 135, 4, 5, 86, 79, 146, 123, 89}, NULL, "sky4_solid"},
	{{163, 123, 35, 117, 154, 146, 68, 92, 141, 70, 253, 212, 187, 18, 112, 149}, NULL, "sky4_alpha"},
	{{196, 173, 196, 177, 19, 221, 134, 159, 208, 159, 158, 4, 108, 57, 10, 108}, NULL, "sky1_solid"},
	{{143, 106, 19, 206, 242, 171, 137, 86, 161, 74, 156, 217, 85, 10, 120, 149}, NULL, "sky1_alpha"},
};

byte ShotgunShells[] = {202, 6, 69, 163, 17, 112, 190, 234, 102, 56, 225, 242, 212, 175, 27, 187};

// this was generated from a word doc on my own PC
// while MD5 collisions are possible, they are sufficiently unlikely in the context of
// a poxy game engine.  this ain't industrial espionage, folks!
byte no_match_hash[] = {0x40, 0xB4, 0x54, 0x7D, 0x9D, 0xDA, 0x9D, 0x0B, 0xCF, 0x42, 0x70, 0xEE, 0xF1, 0x88, 0xBE, 0x99};

// nehahra sends some textures with 0 width and height (eeewww!  more hacks!)
// (these are set to entry 0 so that they don't get misidentified as luma)
// this must be unsigned so that it won't overflow with 32-bit texes
unsigned nulldata[] = {0, 0, 0, 0};


QTEXTURE::QTEXTURE (void)
{
	this->InitData ("", 0, 0, 0, no_match_hash);
}


QTEXTURE::QTEXTURE (char *_identifier, int _width, int _height, int _flags, byte *_hash)
{
	this->InitData (_identifier, _width, _height, _flags, _hash);
}


QTEXTURE::~QTEXTURE (void)
{
	this->Release ();
}


void QTEXTURE::Release (void)
{
	this->identifier[0] = 0;

	SAFE_RELEASE (this->Texture2D);
	SAFE_RELEASE (this->SRV);

	this->SetUnused ();
}


void QTEXTURE::VSSetTexture (int slot)
{
	d3d11_State->VSSetShaderResourceView (slot, this->SRV);
}


void QTEXTURE::PSSetTexture (int slot)
{
	d3d11_State->PSSetShaderResourceView (slot, this->SRV);
}


D3DX11_IMAGE_LOAD_INFO *QTEXTURE::ImageLoadInfo (void)
{
	static D3DX11_IMAGE_LOAD_INFO ili;

	// build an image load info that allows the texture to be mapped and then sucked into a regular texture upload
	ili.Width = D3DX11_DEFAULT;
	ili.Height = D3DX11_DEFAULT;
	ili.Depth = D3DX11_DEFAULT;
	ili.FirstMipLevel = D3DX11_DEFAULT;
	ili.MipLevels = D3DX11_DEFAULT;
	ili.Usage = D3D11_USAGE_STAGING;
	ili.BindFlags = 0;
	ili.CpuAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
	ili.MiscFlags = D3DX11_DEFAULT;
	ili.Format = DXGI_FORMAT_FROM_FILE;
	ili.Filter = D3DX11_DEFAULT;
	ili.MipFilter = D3DX11_DEFAULT;
	ili.pSrcInfo = NULL;

	return &ili;
}


void QTEXTURE::InitData (char *_identifier, int _width, int _height, int _flags, byte *_hash)
{
	if (_identifier)
		strcpy (this->identifier, _identifier);
	else this->identifier[0] = 0;

	this->width = _width;
	this->height = _height;
	this->flags = _flags;
	this->LastUsage = 0;

	Q_MemCpy (this->hash, _hash, 16);

	this->Texture2D = NULL;
	this->SRV = NULL;
}


void QTEXTURE::SetObjectNames (char *texname, char *srvname)
{
	if (!this->identifier[0]) return;
	if (!this->Texture2D) return;
	if (!this->SRV) return;

	D3DMisc_SetObjectName (this->Texture2D, texname);
	D3DMisc_SetObjectName (this->SRV, srvname);
}


void QTEXTURE::SetObjectNames (void)
{
	if (!this->identifier[0]) return;
	if (!this->Texture2D) return;
	if (!this->SRV) return;

	D3DMisc_SetObjectName (this->Texture2D, va ("%s_Texture2D", this->identifier));
	D3DMisc_SetObjectName (this->SRV, va ("%s_SRV", this->identifier));
}


bool QTEXTURE::MatchWith (char *_identifier, int _width, int _height, int _flags, byte *_hash)
{
	if (!_identifier) return false;

	// fixes a bug in red slammer where a frame 0 in an animated texture generated the same checksum as a standard lava texture,
	// causing animation cycles to get messed up.  ideally the texture system would be immune to this but for now it's not...
	if (strcmp (this->identifier, _identifier)) return false;

	if (this->width != _width) return false;
	if (this->height != _height) return false;

	// compare the hash and reuse if it matches
	if (!COM_CheckHash (this->hash, _hash)) return false;

	// check for luma match as the incoming luma will get the same hash as it's base
	// we can't compare flags directly as incoming flags may be changed
	if ((this->flags & IMAGE_LUMA) != (_flags & IMAGE_LUMA)) return false;

	// we have a match now
	return true;
}


void QTEXTURE::SetUnused (void)
{
	Q_MemCpy (this->hash, no_match_hash, 16);
	this->LastUsage = 666;
}


void QTEXTURE::AddFlag (int flag)
{
	this->flags |= flag;
}


void QTEXTURE::DelFlag (int flag)
{
	this->flags &= ~flag;
}


bool QTEXTURE::HasFlag (int flag)
{
	return !!(this->flags & flag);
}


bool QTEXTURE::HasTexture (void)
{
	return !!(this->Texture2D && this->SRV);
}


bool QTEXTURE::ShouldFlush (void)
{
	// all textures just loaded in the cache will have lastusage set to 0
	// incremenent lastusage for types we want to flush.
	if (this->flags & IMAGE_BSP) this->LastUsage++;
	if (this->flags & IMAGE_IQM) this->LastUsage++;
	if (this->flags & IMAGE_ALIAS) this->LastUsage++;
	if (this->flags & IMAGE_SPRITE) this->LastUsage++;

	// always preserve these types irrespective
	if (this->flags & IMAGE_PRESERVE) this->LastUsage = 0;

	if (this->LastUsage > gl_maxtextureretention.value)
		return true;
	else return false;
}


void QTEXTURE::Reuse (void)
{
	this->LastUsage = 0;
}


void QTEXTURE::CopyFrom (ID3D11Texture2D *src)
{
	if (d3d11_Context && this->Texture2D)
	{
		// we need to use CopySubresourceRegion because the target and/or source may be mipped
		d3d11_Context->CopySubresourceRegion (this->Texture2D, 0, 0, 0, 0, src, 0, NULL);
	}
}


void QTEXTURE::GetTextureDesc (D3D11_TEXTURE2D_DESC *desc)
{
	if (this->Texture2D)
	{
		this->Texture2D->GetDesc (desc);
	}
}


void QTEXTURE::SubImageTo (ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
	d3d11_Context->CopySubresourceRegion (pDstResource, DstSubresource, DstX, DstY, DstZ, this->Texture2D, SrcSubresource, pSrcBox);
}


void QTEXTURE::SubImageFrom (UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
	d3d11_Context->CopySubresourceRegion (this->Texture2D, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
}


void QTEXTURE::SubImageFrom (UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch)
{
	d3d11_Context->UpdateSubresource (this->Texture2D, DstSubresource, pDstBox, pSrcData, SrcRowPitch, 0);
}


D3D11_TEXTURE2D_DESC *QTEXTURE::MakeTextureDesc (int width, int height, int flags)
{
	static D3D11_TEXTURE2D_DESC desc;

	memset (&desc, 0, sizeof (D3D11_TEXTURE2D_DESC));

	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = (flags & IMAGE_MIPMAP) ? 0 : 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	if (flags & IMAGE_UPDATE)
	{
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
	}
	else if (flags & IMAGE_READWRITE)
	{
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = (D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ);
	}
	else if (flags & IMAGE_STAGING)
	{
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	}
	else
	{
		desc.Usage = D3D11_USAGE_DEFAULT;//D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
	}

	return &desc;
}


void QTEXTURE::CreateDefaultUAV (ID3D11UnorderedAccessView **uav)
{
	D3D11_TEXTURE2D_DESC texdesc;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavdesc;

	this->Texture2D->GetDesc (&texdesc);

	uavdesc.Format = DXGI_FORMAT_R32_UINT;
	uavdesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
	uavdesc.Texture2DArray.ArraySize = texdesc.ArraySize;
	uavdesc.Texture2DArray.FirstArraySlice = 0;
	uavdesc.Texture2DArray.MipSlice = 0;

	d3d11_Device->CreateUnorderedAccessView (this->Texture2D, NULL, uav);

	if (uav[0])
	{
		uav[0]->GetDesc (&uavdesc);
		uav[0]->GetDesc (&uavdesc);
	}
}


void QTEXTURE::CreateTextureAndSRV (D3D11_TEXTURE2D_DESC *desc, D3D11_SUBRESOURCE_DATA *srd)
{
	hr = d3d11_Device->CreateTexture2D (desc, srd, &this->Texture2D);

	if (desc->BindFlags & D3D11_BIND_SHADER_RESOURCE)
		d3d11_Device->CreateShaderResourceView (this->Texture2D, NULL, &this->SRV);
	else this->SRV = NULL;
}


void QTEXTURE::CreateRenderTarget (ID3D11RenderTargetView **rtv)
{
	d3d11_Device->CreateRenderTargetView (this->Texture2D, NULL, rtv);
}


void QTEXTURE::FromMemory (void *data, int size, int flags)
{
	// to do - handle staging/etc here too...
	D3DX11CreateTextureFromMemory (d3d11_Device,
		data, size, NULL, NULL,
		(ID3D11Resource **) &this->Texture2D,
		NULL);

	if (this->Texture2D)
	{
		D3D11_TEXTURE2D_DESC desc;

		this->Texture2D->GetDesc (&desc);

		if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
			d3d11_Device->CreateShaderResourceView (this->Texture2D, NULL, &this->SRV);
		else this->SRV = NULL;
	}
}


bool QTEXTURE::GetMapping (D3D11_MAPPED_SUBRESOURCE *mr, int sr)
{
	if (!this->Texture2D)
	{
		mr->pData = NULL;
		return false;
	}

	D3D11_TEXTURE2D_DESC desc;
	D3D11_MAP MapType;

	this->Texture2D->GetDesc (&desc);

	if (desc.Usage == D3D11_USAGE_DYNAMIC && desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE)
		MapType = D3D11_MAP_WRITE_DISCARD;
	else if (desc.Usage == D3D11_USAGE_STAGING && desc.CPUAccessFlags == (D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ))
		MapType = D3D11_MAP_READ_WRITE;
	else if (desc.Usage == D3D11_USAGE_STAGING && desc.CPUAccessFlags == D3D11_CPU_ACCESS_READ)
		MapType = D3D11_MAP_READ;
	else if (desc.Usage == D3D11_USAGE_STAGING && desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE)
		MapType = D3D11_MAP_WRITE;
	else
	{
		mr->pData = NULL;
		return false;
	}

	if (SUCCEEDED (d3d11_Context->Map (this->Texture2D, sr, MapType, 0, mr)))
		return true;
	else
	{
		mr->pData = NULL;
		return false;
	}
}


void QTEXTURE::Unmap (int sr)
{
	d3d11_Context->Unmap (this->Texture2D, sr);
}


void QTEXTURE::FromStaging (ID3D11Texture2D *pTexture, int flags)
{
	D3D11_TEXTURE2D_DESC desc;
	D3D11_MAPPED_SUBRESOURCE MappedResource;

	pTexture->GetDesc (&desc);

	if (SUCCEEDED (d3d11_Context->Map (pTexture, 0, D3D11_MAP_READ_WRITE, 0, &MappedResource)))
	{
		D3DImage_CompressRowPitch ((unsigned *) MappedResource.pData, desc.Width, desc.Height, MappedResource.RowPitch >> 2);
		this->Upload ((byte *) MappedResource.pData, desc.Width, desc.Height, this->flags | flags, NULL);
		d3d11_Context->Unmap (pTexture, 0);
	}

	pTexture->Release ();
}


bool QTEXTURE::FromMemory (void *data, int size, D3DX11_IMAGE_LOAD_INFO *ili, int flags)
{
	// this is nasty but is needed for gamma-corrected mipmapping (and maybe some other crap...)
	ID3D11Texture2D *pTexture = NULL;

	D3DX11CreateTextureFromMemory (d3d11_Device,
		data, size, ili, NULL,
		(ID3D11Resource **) &pTexture,
		NULL);

	this->flags = flags;

	if (pTexture)
	{
		this->FromStaging (pTexture, flags);
		return true;
	}
	else return false;
}


void QTEXTURE::FromResource (int resourceid, int flags)
{
	char *data = NULL;
	int len = Sys.LoadResourceData (resourceid, (void **) &data);

	if (data && len > 0)
		this->FromMemory (data, len, flags);
	else Sys_Error ("QTEXTURE::FromResource : Failed to load texture from resource");
}


void QTEXTURE::SetMipData (D3D11_SUBRESOURCE_DATA *srd, void *data, int pitch)
{
	srd->pSysMem = data;
	srd->SysMemPitch = pitch;
	srd->SysMemSlicePitch = 0;
}


void QTEXTURE::GenerateMips (void)
{
	d3d11_Context->GenerateMips (this->SRV);
}


void QTEXTURE::RGBA32FromLowestMip (byte *dest)
{
	ID3D11Texture2D *StagingTexture = NULL;
	D3D11_TEXTURE2D_DESC desc;

	this->Texture2D->GetDesc (&desc);

	if ((desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) && desc.MipLevels > 0)
	{
		if (SUCCEEDED (d3d11_Device->CreateTexture2D (QTEXTURE::MakeTextureDesc (1, 1, IMAGE_READWRITE), NULL, &StagingTexture)))
		{
			D3D11_MAPPED_SUBRESOURCE MappedResource;

			d3d11_Context->CopySubresourceRegion (StagingTexture, 0, 0, 0, 0, this->Texture2D, desc.MipLevels - 1, NULL);

			StagingTexture->GetDesc (&desc);

			if (SUCCEEDED (d3d11_Context->Map (StagingTexture, 0, D3D11_MAP_READ_WRITE, 0, &MappedResource)))
			{
				byte *rgba = (byte *) MappedResource.pData;

				if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
				{
					dest[2] = rgba[0];
					dest[1] = rgba[1];
					dest[0] = rgba[2];
				}
				else
				{
					dest[0] = rgba[0];
					dest[1] = rgba[1];
					dest[2] = rgba[2];
				}

				dest[3] = rgba[3];
			}
			else dest[0] = dest[1] = dest[2] = dest[3] = 255;

			StagingTexture->Release ();
		}
		else dest[0] = dest[1] = dest[2] = dest[3] = 255;
	}
	else dest[0] = dest[1] = dest[2] = dest[3] = 255;
}


D3D11_SUBRESOURCE_DATA *QTEXTURE::BuildMipLevels (byte *data, int width, int height, int flags)
{
	D3D11_SUBRESOURCE_DATA *srd = (D3D11_SUBRESOURCE_DATA *) scratchbuf;

	// build the base level
	QTEXTURE::SetMipData (srd, data, width * 4);

	if (flags & IMAGE_MIPMAP)
	{
		int miplevel = 0;
		byte *mipdata = NULL;

		while (width > 1 || height > 1)
		{
			// we can't mipmap in-place as the texture creation func needs a pointer for each level
			// (making sure we have enough space if we need to resample)
			if ((width & 1) || (height & 1))
			{
				mipdata = (byte *) TempHunk->FastAlloc (((width + 1) & ~1) * ((height + 1) & ~1));
				D3DImage_Resample ((unsigned *) data, width, height, (unsigned *) mipdata, width >> 1, height >> 1);
			}
			else
			{
				mipdata = (byte *) TempHunk->FastAlloc (width * height);
				D3DImage_MipMap (data, mipdata, width, height);
			}

			if ((width = width >> 1) < 1) width = 1;
			if ((height = height >> 1) < 1) height = 1;

			QTEXTURE::SetMipData (&srd[++miplevel], (data = mipdata), width * 4);
		}
	}

	return srd;
}


unsigned int *QTEXTURE::ToRGBA (byte *data, int width, int height, unsigned int *palette)
{
	if (palette)
	{
		int size = (width * height);
		unsigned int *data32 = (unsigned int *) TempHunk->FastAlloc (size * 4);

		if (size & 3)
		{
			for (int i = 0; i < size; i++)
			{
				data32[i] = palette[data[i]];
			}
		}
		else
		{
			byte *in8 = data;
			unsigned *out32 = data32;

			for (int i = 0; i < size; i += 4, in8 += 4, out32 += 4)
			{
				out32[0] = palette[in8[0]];
				out32[1] = palette[in8[1]];
				out32[2] = palette[in8[2]];
				out32[3] = palette[in8[3]];
			}
		}

		return data32;
	}
	else return (unsigned int *) data;
}


void QTEXTURE::Upload (byte *data, int width, int height, int flags, unsigned int *palette)
{
	int HunkMark = TempHunk->GetLowMark ();

	if ((flags & IMAGE_32BIT) && (flags & IMAGE_SPRITE))
	{
		// hack hack hack hack hack
		// some sprite replacements come without alpha channels (marcher, etc) so construct a fake one
		byte *rgba = data;

		for (int i = 0; i < width * height; i++, rgba += 4)
		{
			byte bright = 0;

			// take the brightest of the 3 components
			if (rgba[0] > bright) bright = rgba[0];
			if (rgba[1] > bright) bright = rgba[1];
			if (rgba[2] > bright) bright = rgba[2];

			// if alpha is higher than this then this becomes alpha
			if (rgba[3] > bright) rgba[3] = bright;
		}
	}

	unsigned int *data32 = QTEXTURE::ToRGBA (data, width, height, palette);

	if (width > 2048 || height > 2048)
	{
		// don't load at bigger than 2048x2048
		int newwidth = width > 2048 ? 2048 : width;
		int newheight = height > 2048 ? 2048 : height;
		unsigned *newdata = (unsigned *) TempHunk->FastAlloc (newwidth * newheight * sizeof (unsigned));

		D3DImage_Resample (data32, width, height, newdata, newwidth, newheight);

		data32 = newdata;
		width = newwidth;
		height = newheight;
	}

	if (flags & IMAGE_ALIAS)
	{
		D3DImage_AlphaEdgeFix ((byte *) data32, width, height);
		D3DImage_AlphaEdgeFix ((byte *) data32, width, height);
	}

	if (flags & IMAGE_ALPHA) D3DImage_AlphaEdgeFix ((byte *) data32, width, height);
	if (flags & IMAGE_ALPHAMASK) D3DImage_AlphaMask ((byte *) data32, width * height);

	D3D11_SUBRESOURCE_DATA *srd = QTEXTURE::BuildMipLevels ((byte *) data32, width, height, flags);
	D3D11_TEXTURE2D_DESC *desc = QTEXTURE::MakeTextureDesc (width, height, flags);

	this->CreateTextureAndSRV (desc, srd);

	TempHunk->FreeToLowMark (HunkMark);
}


bool QTEXTURE::HasLuma (byte *_data, int _width, int _height, int _flags)
{
	// check native texture for a luma
	if ((_flags & IMAGE_LUMA) && !(_flags & IMAGE_32BIT))
	{
		for (int i = 0; i < _width * _height; i++)
		{
			// the alpha texel should never flag a luma
			if (_data[i] == 255) continue;
			if (_data[i] > 223) return true;
		}
	}

	return false;
}


bool QTEXTURE::TryLoadExternal (char **_paths)
{
	// explicitly don't load an external texture
	if (this->flags & IMAGE_NOEXTERN) return false;

	// try to load an external texture using the base identifier
	if (this->LoadExternal (this->identifier, _paths, this->flags)) return true;

	// it might yet have the QRP convention so try that
	char *qrpident = (char *) TempHunk->Alloc (256);

	// try the QRP names here
	for (int i = 0; i < ARRAYLENGTH (d3d_HashHacks); i++)
	{
		if (COM_CheckHash (this->hash, d3d_HashHacks[i].hash))
		{
			// don't mess with the original identifier as that's used for cache checks
			strcpy (qrpident, this->identifier);

			if (d3d_HashHacks[i].idcat) strcat (qrpident, d3d_HashHacks[i].idcat);
			if (d3d_HashHacks[i].idcpy) strcpy (qrpident, d3d_HashHacks[i].idcpy);

			break;
		}
	}

	// and now try load it (but only if we got a match for it)
	if (qrpident[0] && this->LoadExternal (qrpident, _paths, this->flags))
		return true;
	else return false;
}


bool QTEXTURE::TryLoadNative (byte *_data)
{
	// if we're specifying that we want external only then we don't load it now
	if (this->flags & IMAGE_EXTERNONLY) return false;

	// test for a native luma texture
	if ((this->flags & IMAGE_LUMA) && !QTEXTURE::HasLuma (_data, this->width, this->height, this->flags)) return false;

	// upload through direct 3d
	if (this->flags & IMAGE_32BIT)
		this->Upload (_data, this->width, this->height, this->flags, NULL);
	else if (this->flags & IMAGE_LUMA)
		this->Upload (_data, this->width, this->height, this->flags, d3d_QuakePalette.luma11);
	else this->Upload (_data, this->width, this->height, this->flags, d3d_QuakePalette.standard11);

	return true;
}


// this has turned a little nasty but it's needed to support Quake weenie-ism and "every case is a special case" crap...
QTEXTURE *QTEXTURE::Load (char *_identifier, int _width, int _height, byte *_data, int _flags, char **_paths)
{
	if ((_flags & IMAGE_EXTERNONLY) && (_flags & IMAGE_NOEXTERN)) return NULL;

	// supply a path to load it from if none was given
	if (!_paths) _paths = defaultpaths;

	// remove any extension we might have
	if (_identifier[strlen (_identifier) - 4] == '.')
	{
		char *newident = (char *) TempHunk->FastAlloc (strlen (_identifier) + 1);

		// remove it now and swap the name
		strcpy (newident, _identifier);
		newident[strlen (newident) - 4] = 0;
		_identifier = newident;
	}

	// nehahra sends some textures with 0 width and height
	if (!_width || !_height || !_data)
	{
		_width = 2;
		_height = 2;
		_data = (byte *) nulldata;
	}

	// nehahra assumes no lumas
	if (nehahra && (_flags & IMAGE_LUMA)) return NULL;

	// take a hash of the image data
	byte _texhash[16];
	int slot = -1;

	COM_HashData (_texhash, _data, _width * _height * ((_flags & IMAGE_32BIT) ? 4 : 1));

	for (int i = 0; i < d3d11_Textures.size (); i++)
	{
		if (!d3d11_Textures[i])
		{
			slot = i;
			continue;
		}

		if (d3d11_Textures[i]->MatchWith (_identifier, _width, _height, _flags, _texhash))
		{
			d3d11_Textures[i]->Reuse ();
			Con_DPrintf ("reused %s%s\n", _identifier, (_flags & IMAGE_LUMA) ? "_luma" : "");
			return d3d11_Textures[i];
		}
	}

	QTEXTURE *tex = new QTEXTURE (_identifier, _width, _height, _flags, _texhash);

	// fix white line at base of shotgun shells box
	if (COM_CheckHash (_texhash, ShotgunShells)) Q_MemCpy (_data, _data + 32 * 31, 32);

#if 1
	// try for external, then fall back on native
	if (!tex->TryLoadExternal (_paths))
	{
		if (!tex->TryLoadNative (_data))
		{
			// both failed (this can happen if we're loading from Draw_LoadPic)
			tex->SetUnused ();
			SAFE_DELETE (tex);
			return NULL;
		}
	}
	else tex->AddFlag (IMAGE_EXTERNAL);
#else
	// for testing the perf crap mindz has been getting; remove from final build
	if (!tex->TryLoadNative (_data))
	{
		// both failed (this can happen if we're loading from Draw_LoadPic)
		tex->SetUnused ();
		SAFE_DELETE (tex);
		return NULL;
	}
#endif

	// now add it to the list only after it's been loaded OK
	if (slot == -1)
	{
		d3d11_Textures.push_back (tex);
		tex = d3d11_Textures[d3d11_Textures.size () - 1];
	}
	else d3d11_Textures[slot] = tex;

	tex->SetObjectNames ();

	return tex;
}


bool QTEXTURE::CreateExternal (byte *data, int type, int flags)
{
	int width, height;
	bool succeeded = false;

	if (!data) return false;

	switch (type)
	{
	case TEXTYPE_LINK:	// link
		// a .link file should never explicitly go through this codepath
		return false;

	case TEXTYPE_DDS: // dds
		// goes up directly, ignores flags
		this->FromMemory (data, CQuakeFile::FileSize, flags);
		succeeded = true;

		break;

		// normally i'd just use stb_image instead of needing different loaders for each image type, but that doesn't support PCX
		// and only supports 8-bit PNG so i don't use it
	case TEXTYPE_TGA:	// tga
		if ((data = D3DImage_LoadTGA (data, &width, &height)) != NULL)
		{
			this->Upload (data, width, height, flags | IMAGE_32BIT, NULL);
			succeeded = true;
		}

		break;

	case TEXTYPE_PCX:	// pcx
		if ((data = D3DImage_LoadPCX (data, &width, &height)) != NULL)
		{
			this->Upload (data, width, height, flags | IMAGE_32BIT, NULL);
			succeeded = true;
		}

		break;

	default:
		succeeded = this->FromMemory (data, CQuakeFile::FileSize, QTEXTURE::ImageLoadInfo (), flags);
		break;
	}

	return succeeded;
}


bool QTEXTURE::LoadExternal (char *filename, char **paths, int flags)
{
	// sanity check
	if (!filename) return false;
	if (!paths) return false;
	if (!paths[0]) return false;

	char namebuf[256];
	char basename[256];
	int hunkmark = TempHunk->GetLowMark ();
	byte *data = NULL;
	bool succeeded = false;

	// copy off the file name so that we can change it safely
	strcpy (basename, filename);

	// identify liquid
	for (int i = 0; ; i++)
	{
		if (!basename[i]) break;
		if (basename[i] == '*') basename[i] = '#';
	}

	for (int i = 0; ; i++)
	{
		// no more paths
		if (!paths[i]) break;

		// tested and confirmed to be an invalid path (doesn't exist in the filesystem)
		if (paths[i][0] == '*') continue;

		for (int j = 0; ; j++)
		{
			if (!textureextensions[j]) break;

			if (flags & IMAGE_LUMA)
				sprintf (namebuf, "%s%s_luma.%s", paths[i], basename, textureextensions[j]);
			else sprintf (namebuf, "%s%s.%s", paths[i], basename, textureextensions[j]);

			if ((data = (byte *) CQuakeFile::LoadFile (namebuf, TempHunk)) == NULL)
			{
				if (flags & IMAGE_LUMA)
					sprintf (namebuf, "%s%s_glow.%s", paths[i], basename, textureextensions[j]);
				else continue;

				if ((data = (byte *) CQuakeFile::LoadFile (namebuf, TempHunk)) == NULL) continue;
			}

			if (j == 0)
			{
				// got a link file so use it instead
				char *linkname = (char *) data;
				int type = TEXTYPE_DDS;

				// weenix loonies
				_strlwr (linkname);

				// COM_LoadFile will 0-termnate a string automatically for us
				if (strstr (linkname, ".tga")) type = TEXTYPE_TGA;
				if (strstr (linkname, ".bmp")) type = TEXTYPE_BMP;
				if (strstr (linkname, ".png")) type = TEXTYPE_PNG;
				if (strstr (linkname, ".jpg")) type = TEXTYPE_JPG;
				if (strstr (linkname, ".pcx")) type = TEXTYPE_PCX;

				// .link assumes the same path
				sprintf (namebuf, "%s%s", paths[i], linkname);

				if ((data = (byte *) CQuakeFile::LoadFile (namebuf, TempHunk)) != NULL)
				{
					Con_DPrintf ("got a file : %s\n", namebuf);
					succeeded = this->CreateExternal (data, type, flags);
					goto done;
				}
			}
			else
			{
				Con_DPrintf ("got a file : %s\n", namebuf);
				succeeded = this->CreateExternal (data, j, flags);
				goto done;
			}
		}
	}

done:;
	TempHunk->FreeToLowMark (hunkmark);
	return succeeded;
}


void QTEXTURE::Flush (void)
{
	int numflush = 0;

	// sanity check - always retain for at least 3 maps (current plus 2 more)
	if (gl_maxtextureretention.value < 3) gl_maxtextureretention.Set (3);

	for (int i = 0, startpos = 0; i < d3d11_Textures.size (); i++)
	{
		// already flushed
		if (d3d11_Textures[i])
		{
			if (d3d11_Textures[i]->ShouldFlush ())
			{
				// if the texture hasn't been used in 4 maps, we flush it
				SAFE_DELETE (d3d11_Textures[i]);
				numflush++;
			}
			else continue;
		}

		// put all free textures at the start of the list so that we can more quickly locate and reuse them
		std::swap (d3d11_Textures[i], d3d11_Textures[startpos]);
		startpos++;
	}

	if (numflush) Con_DPrintf ("Flushed %i textures\n", numflush);
}


