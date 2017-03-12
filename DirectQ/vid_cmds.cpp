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
#include <commctrl.h>

#include <vector>

extern std::vector<DXGI_MODE_DESC> d3d_DisplayModes;


void D3DVid_NumModes_f (void)
{
	Con_Printf ("%i video modes available\n", d3d_DisplayModes.size ());
}


void D3DVid_DescribeMode (DXGI_MODE_DESC *mode)
{
	Con_Printf ("%4i x %-4i\n", mode->Width, mode->Height);
}


void D3DVid_DescribeCurrentMode_f (void)
{
	D3DVid_DescribeMode (&d3d_CurrentMode);
}


void D3DVid_DescribeModes_f (void)
{
	for (int i = 0; i < d3d_DisplayModes.size (); i++)
	{
		Con_Printf ("%3i  ", i);
		D3DVid_DescribeMode (&d3d_DisplayModes[i]);
	}
}


void D3DVid_DescribeBackBuffer_f (void)
{
	VIDWin32_GoToNewClientRect ();
	Con_Printf ("%i x %i client rect\n", (vid.ClientRect.right - vid.ClientRect.left), (vid.ClientRect.bottom - vid.ClientRect.top));

	D3D11_TEXTURE2D_DESC desc;
	extern ID3D11Texture2D *d3d11_DepthBuffer;

	if (d3d11_SwapChain)
	{
		ID3D11Texture2D *pRenderTargetTexture = NULL;

		if (SUCCEEDED (d3d11_SwapChain->GetBuffer (0, __uuidof (ID3D11Texture2D), (LPVOID *) &pRenderTargetTexture)))
		{
			pRenderTargetTexture->GetDesc (&desc);
			pRenderTargetTexture->Release ();
			Con_Printf ("%i x %i color buffer\n", desc.Width, desc.Height);
		}
	}

	if (d3d11_DepthBuffer)
	{
		d3d11_DepthBuffer->GetDesc (&desc);
		Con_Printf ("%i x %i depth buffer\n", desc.Width, desc.Height);
	}
}


cmd_t D3DVid_DescribeModes_f_Cmd ("vid_describemodes", D3DVid_DescribeModes_f);
cmd_t D3DVid_NumModes_f_Cmd ("vid_nummodes", D3DVid_NumModes_f);
cmd_t D3DVid_DescribeCurrentMode_f_Cmd ("vid_describecurrentmode", D3DVid_DescribeCurrentMode_f);
cmd_t D3DVid_DescribeBackBuffer_f_Cmd ("vid_describebackbuffer", D3DVid_DescribeBackBuffer_f);

