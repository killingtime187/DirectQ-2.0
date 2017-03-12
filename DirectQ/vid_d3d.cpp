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

// essential reading
// http://msdn.microsoft.com/en-us/library/windows/desktop/ee417025%28v=vs.85%29.aspx
// http://msdn.microsoft.com/en-us/library/windows/desktop/bb205075%28v=vs.85%29.aspx


#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

#include "winquake.h"
#include <commctrl.h>

#include <dwmapi.h>
#include <vector>

#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "DXGI.lib")
#pragma comment (lib, "D3DX10.lib")
#pragma comment (lib, "d3dx11.lib")

// NVIDIA crap
_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

// global video state
viddef_t vid;

// this ensures that our framebuffer is always created with consistent multisampling levels
DXGI_SAMPLE_DESC d3d11_SampleDesc = {1, 0};

void AppActivate (BOOL fActive, BOOL minimize, BOOL notify);
void Draw_BrightPass (void);

IDXGISwapChain *d3d11_SwapChain = NULL;
ID3D11Device *d3d11_Device = NULL;
ID3D11DeviceContext *d3d11_Context = NULL;

int d3d_LastWindowedMode = 0;
int d3d_LastFullscreenMode = 0;

// default to the lowest
D3D_FEATURE_LEVEL d3d11_FeatureLevel = D3D_FEATURE_LEVEL_10_0;

ID3D11RenderTargetView *d3d11_RenderTargetView = NULL;
ID3D11Texture2D *d3d11_DepthBuffer = NULL;
ID3D11DepthStencilView *d3d11_DepthStencilView = NULL;

DXGI_ADAPTER_DESC d3d11_AdapterDesc;
DXGI_OUTPUT_DESC d3d11_OutputDesc;


/*
==============================================================================================================================

	WINDOW SYSTEM

==============================================================================================================================
*/

void VIDWin32_SetActiveGamma (cvar_t *var);

cvar_t		v_gamma ("gamma", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		r_gamma ("r_gamma", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		g_gamma ("g_gamma", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		b_gamma ("b_gamma", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);

cvar_t		v_contrast ("contrast", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		r_contrast ("r_contrast", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		g_contrast ("g_contrast", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);
cvar_t		b_contrast ("b_contrast", "1", CVAR_ARCHIVE, VIDWin32_SetActiveGamma);


void VIDWin32_GoToNewClientRect (void)
{
	GetClientRect (vid.Window, &vid.ClientRect);

	// this hackery works because of the way the RECT struct is laid out
	ClientToScreen (vid.Window, (POINT *) &vid.ClientRect.left);
	ClientToScreen (vid.Window, (POINT *) &vid.ClientRect.right);
}


void VIDWin32_CenterWindow (void)
{
	if (vid.isfullscreen) return;

	// get the size of the desktop working area and the window
	if (vid.Window && SystemParametersInfo (SPI_GETWORKAREA, 0, &vid.WorkArea, 0))
	{
		if (GetWindowRect (vid.Window, &vid.WindowRect))
		{
			// center it properly in the working area (don't assume that top and left are 0!!!)
			SetWindowPos (
				vid.Window,
				HWND_TOP,
				vid.WorkArea.left + ((vid.WorkArea.right - vid.WorkArea.left) - (vid.WindowRect.right - vid.WindowRect.left)) / 2,
				vid.WorkArea.top + ((vid.WorkArea.bottom - vid.WorkArea.top) - (vid.WindowRect.bottom - vid.WindowRect.top)) / 2,
				0,
				0,
				SWP_NOSIZE | SWP_SHOWWINDOW | SWP_DEFERERASE | SWP_NOCOPYBITS
			);
		}
	}
}


void VIDWin32_SetActiveGamma (cvar_t *var)
{
	// update brightpass
	vid.brightpass = false;

	if (v_gamma.value != 1.0f) vid.brightpass = true;
	if (r_gamma.value != 1.0f) vid.brightpass = true;
	if (g_gamma.value != 1.0f) vid.brightpass = true;
	if (b_gamma.value != 1.0f) vid.brightpass = true;

	if (v_contrast.value != 1.0f) vid.brightpass = true;
	if (r_contrast.value != 1.0f) vid.brightpass = true;
	if (g_contrast.value != 1.0f) vid.brightpass = true;
	if (b_contrast.value != 1.0f) vid.brightpass = true;
}


/*
==============================================================================================================================

	UTILITY

==============================================================================================================================
*/

void VID_DebugPrintf (char *format, ...)
{
#ifdef _DEBUG
	va_list argptr;
	char *string = (char *) _alloca (1024);

	// make the buffer safe
	va_start (argptr, format);
	_vsnprintf (string, 1023, format, argptr);
	va_end (argptr);

	OutputDebugString (string);
#endif
}


bool D3DVid_IsCreated (void)
{
	if (!d3d11_Device) return false;
	if (!d3d11_Context) return false;
	if (!d3d11_SwapChain) return false;

	return true;
}


void D3DVid_UpdateOutput (void)
{
}


void D3DVid_ClearScreenAndPresent (void)
{
	if (d3d11_Context && d3d11_RenderTargetView)
	{
		d3d11_Context->OMSetRenderTargets (1, &d3d11_RenderTargetView, NULL);
		d3d11_Context->ClearRenderTargetView (d3d11_RenderTargetView, D3DXVECTOR4 (0, 0, 0, 0));
	}

	if (d3d11_SwapChain) d3d11_SwapChain->Present (0, 0);
}


/*
==============================================================================================================================

	VIDEO OPTIONS

==============================================================================================================================
*/

void D3DVID_SetDeviceOptions (cvar_t *var);
void D3DVID_SetCPUOptimizations (cvar_t *var);

void D3DVid_QueueRestart (cvar_t *var)
{
	// rather than restart immediately we notify the renderer that it will need to restart as soon as it comes up
	// this should hopefully fix a lot of crap during startup (and generally speed startup up a LOT)
	vid.queuerestart = true;
}


void D3DVid_UpdateSyncInterval (cvar_t *var)
{
	if (var->integer < 0)
		vid.syncinterval = 0;
	else if (var->integer > 4)
		vid.syncinterval = 4;
	else vid.syncinterval = var->integer;
}

cvar_t vid_mode ("vid_mode", "0", CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t gl_finish ("gl_finish", 0.0f);
cvar_t vid_fullscreen ("vid_fullscreen", 0.0f, CVAR_ARCHIVE, D3DVid_QueueRestart);
cvar_t vid_vsync ("vid_vsync", "0", CVAR_ARCHIVE, D3DVid_UpdateSyncInterval);
cvar_t vid_maximumframelatency ("vid_maximumframelatency", "3", CVAR_ARCHIVE, D3DVID_SetDeviceOptions);
cvar_t vid_gputhreadpriority ("vid_gputhreadpriority", "0", CVAR_ARCHIVE, D3DVID_SetDeviceOptions);
cvar_t vid_d3dxoptimizations ("vid_d3dxoptimizations", "1", CVAR_ARCHIVE, D3DVID_SetCPUOptimizations);

bool suppressdevoptionschange = false;

void D3DVID_GetDeviceOptions (void)
{
	if (d3d11_Device)
	{
		IDXGIDevice1 *pDXGIDevice = NULL;

		if (SUCCEEDED (d3d11_Device->QueryInterface (__uuidof (IDXGIDevice1), (void **) &pDXGIDevice)))
		{
			INT priority;
			UINT latency;

			suppressdevoptionschange = true;

			if (SUCCEEDED (pDXGIDevice->GetGPUThreadPriority (&priority))) vid_gputhreadpriority.Set (priority);
			if (SUCCEEDED (pDXGIDevice->GetMaximumFrameLatency (&latency))) vid_maximumframelatency.Set (latency);

			suppressdevoptionschange = false;
			pDXGIDevice->Release ();
		}
	}
}


void D3DVID_SetDeviceOptions (cvar_t *var)
{
	if (d3d11_Device && !suppressdevoptionschange)
	{
		IDXGIDevice1 *pDXGIDevice = NULL;

		if (SUCCEEDED (d3d11_Device->QueryInterface (__uuidof (IDXGIDevice1), (void **) &pDXGIDevice)))
		{
			if (vid_gputhreadpriority.integer < -7)
				pDXGIDevice->SetGPUThreadPriority (-7);
			if (vid_gputhreadpriority.integer > 7)
				pDXGIDevice->SetGPUThreadPriority (7);
			else pDXGIDevice->SetGPUThreadPriority (vid_gputhreadpriority.integer);

			if (vid_maximumframelatency.integer < 1)
				pDXGIDevice->SetMaximumFrameLatency (1);
			else if (vid_maximumframelatency.integer > 16)
				pDXGIDevice->SetMaximumFrameLatency (16);
			else pDXGIDevice->SetMaximumFrameLatency (vid_maximumframelatency.integer);

			pDXGIDevice->Release ();
		}
	}
}


void D3DVID_SetCPUOptimizations (cvar_t *var)
{
	if (var->integer)
		D3DXCpuOptimizations (TRUE);
	else D3DXCpuOptimizations (FALSE);
}


/*
==============================================================================================================================

	VIDEO MODES LIST

==============================================================================================================================
*/

DXGI_MODE_DESC d3d_CurrentMode;
std::vector<DXGI_MODE_DESC> d3d_DisplayModes;

int D3DVid_FindModeForValue (int val)
{
	if (val < 0)
		return d3d_DisplayModes.size () - 1;
	else if (val >= d3d_DisplayModes.size ())
		return 0;
	else return val;
}


int VIDD3D_TryEnumerateModes (IDXGIOutput *output, DXGI_MODE_DESC *ModeList, DXGI_FORMAT fmt)
{
	UINT NumModes = 0;
	const UINT EnumFlags = (DXGI_ENUM_MODES_SCALING | DXGI_ENUM_MODES_INTERLACED);

	// get modes on this adapter (note this is called twice per design) - note that the first time ModeList must be NULL or it will return 0 modes
	if (SUCCEEDED (output->GetDisplayModeList (fmt, EnumFlags, &NumModes, NULL)))
	{
		if (SUCCEEDED (output->GetDisplayModeList (fmt, EnumFlags, &NumModes, ModeList)))
			return NumModes;
		else return 0;
	}
	else return 0;
}


void VIDD3D_EnumerateModes (void)
{
	IDXGIFactory1 *pFactory = NULL;
	IDXGIOutput *d3d11_Output = NULL;
	IDXGIAdapter *d3d11_Adapter = NULL;
	UINT NumModes = 0;
	DXGI_MODE_DESC *ModeList = (DXGI_MODE_DESC *) scratchbuf;

	// fixme - enum these properly
	if (FAILED (CreateDXGIFactory1 (__uuidof (IDXGIFactory1), (void **) &pFactory)))
		Sys_Error ("VIDD3D_EnumerateModes : CreateDXGIFactory1 failed");

	if ((pFactory->EnumAdapters (0, &d3d11_Adapter)) == DXGI_ERROR_NOT_FOUND)
		Sys_Error ("VIDD3D_EnumerateModes : IDXGIFactory failed to enumerate Adapter 0");

	if ((d3d11_Adapter->EnumOutputs (0, &d3d11_Output)) == DXGI_ERROR_NOT_FOUND)
		Sys_Error ("VIDD3D_EnumerateModes : IDXGIFactory failed to enumerate Outputs on Adapter 0");

	// some Intels may need BGRA
	if ((NumModes = VIDD3D_TryEnumerateModes (d3d11_Output, ModeList, DXGI_FORMAT_R8G8B8A8_UNORM)) == 0)
		if ((NumModes = VIDD3D_TryEnumerateModes (d3d11_Output, ModeList, DXGI_FORMAT_B8G8R8A8_UNORM)) == 0)
			Sys_Error ("VIDD3D_EnumerateModes : Failed to enumerate any 32bpp modes!");

	// copy them out
	for (int i = 0; i < NumModes; i++)
	{
		if (ModeList[i].Width < 640) continue;
		if (ModeList[i].Height < 480) continue;

		d3d_DisplayModes.push_back (ModeList[i]);
	}

	d3d11_Output->GetDesc (&d3d11_OutputDesc);
	d3d11_Adapter->GetDesc (&d3d11_AdapterDesc);

	d3d11_Output->Release ();
	d3d11_Adapter->Release ();
	pFactory->Release ();
}


/*
==============================================================================================================================

	VIDEO MODE HANDLING

==============================================================================================================================
*/

void D3DRTT_Resize (void);

void D3DVid_FlushStates (void)
{
	if (d3d11_State) d3d11_State->ClearState ();
	if (d3d11_Context) d3d11_Context->ClearState ();
	if (d3d11_Context) d3d11_Context->Flush ();
}


void D3DVid_SetFullScreen (BOOL fullscreen)
{
	if (!d3d11_SwapChain) return;

	if (fullscreen)
		VID_DebugPrintf ("Going fullscreen");
	else VID_DebugPrintf ("Going windowed");

	DXGI_SWAP_CHAIN_DESC sd;

	if (SUCCEEDED (d3d11_SwapChain->GetDesc (&sd)))
		VID_DebugPrintf (" from %ix%i\n", sd.BufferDesc.Width, sd.BufferDesc.Height);
	else VID_DebugPrintf ("\n");

	for (;;)
	{
		// let DXGI choose the appropriate target
		hr = d3d11_SwapChain->SetFullscreenState (fullscreen, NULL);

		if (hr == S_OK) break;
		if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) break;
		if (hr == DXGI_STATUS_MODE_CHANGE_IN_PROGRESS) continue;
		if (FAILED (hr)) break;
	}

	// warning C4800: 'BOOL' : forcing value to bool 'true' or 'false' (performance warning) - MADNESS!!!
	vid.isfullscreen = fullscreen ? true : false;
}


void D3DVid_ReleaseFrameBuffer (void)
{
	D3DVid_FlushStates ();

	SAFE_RELEASE (d3d11_RenderTargetView);
	SAFE_RELEASE (d3d11_DepthBuffer);
	SAFE_RELEASE (d3d11_DepthStencilView);
}


void D3DVid_CreateFrameBuffer (DXGI_SWAP_CHAIN_DESC *sd)
{
	D3DVid_ReleaseFrameBuffer ();

	// this is also used for video restarting and resizing
	// Create the render target view
	ID3D11Texture2D *pRenderTargetTexture = NULL;

	if (SUCCEEDED (d3d11_SwapChain->GetBuffer (0, __uuidof (ID3D11Texture2D), (LPVOID *) &pRenderTargetTexture)))
	{
		if (FAILED (d3d11_Device->CreateRenderTargetView (pRenderTargetTexture, NULL, &d3d11_RenderTargetView)))
		{
			pRenderTargetTexture->Release ();
			Sys_Error ("D3DVid_CreateFrameBuffer : CreateRenderTargetView failed");
		}
		else pRenderTargetTexture->Release ();
	}
	else Sys_Error ("D3DVid_CreateFrameBuffer : Failed to get back buffer");

	D3DMisc_SetObjectName (d3d11_RenderTargetView, "d3d11_RenderTargetView");

	// create a texture for use as the depth buffer
	// always using DXGI_FORMAT_D24_UNORM_S8_UINT because r_shadows 1 mode needs it
	CD3D11_TEXTURE2D_DESC descDepth (
		DXGI_FORMAT_D24_UNORM_S8_UINT,
		sd->BufferDesc.Width,
		sd->BufferDesc.Height,
		1,
		1,
		D3D11_BIND_DEPTH_STENCIL,
		D3D11_USAGE_DEFAULT,
		0,
		sd->SampleDesc.Count,
		sd->SampleDesc.Quality,
		0
	);

	d3d11_Device->CreateTexture2D (&descDepth, NULL, &d3d11_DepthBuffer);
	d3d11_Device->CreateDepthStencilView (d3d11_DepthBuffer, NULL, &d3d11_DepthStencilView);

	// sync up dimensions
	Q_MemCpy (&d3d_CurrentMode, &sd->BufferDesc, sizeof (DXGI_MODE_DESC));

	// recreate anything that depends on the current framebuffer size (rendertargets/etc)
	D3DRTT_Resize ();
}


void D3DVid_HandleResize (void)
{
	// this should only ever be called in response to a WM_SIZE and never directly by any other code
	if (!D3DVid_IsCreated ()) return;

	// get the current swap chain desc to keep everything nice and valid
	DXGI_SWAP_CHAIN_DESC sd;

	if (SUCCEEDED (d3d11_SwapChain->GetDesc (&sd)))
	{
		// these will be properly recreated when the next frame runs
		D3DVid_ReleaseFrameBuffer ();

		VID_DebugPrintf ("ResizeBuffers\n");
		d3d11_SwapChain->ResizeBuffers (0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
		IN_SetMouseState (vid_fullscreen.value ? true : false);

		d3d11_SwapChain->GetDesc (&sd);
		VID_DebugPrintf ("SwapChain at %ix%i\n", sd.BufferDesc.Width, sd.BufferDesc.Height);

		vid.RecalcRefdef = true;
	}

	vid.block_drawing = false;
}


void D3DVid_ResizeTarget (DXGI_MODE_DESC *mode)
{
	VID_DebugPrintf ("ResizeTarget to %ix%i\n", mode->Width, mode->Height);

	for (;;)
	{
		hr = d3d11_SwapChain->ResizeTarget (mode);

		if (hr == S_OK) break;
		if (hr == DXGI_STATUS_MODE_CHANGE_IN_PROGRESS) continue;
		if (FAILED (hr)) break;
	}
}


void D3DVid_RunQueuedRestart (void)
{
	int modenum = D3DVid_FindModeForValue (vid_mode.value);
	DXGI_MODE_DESC *mode = &d3d_DisplayModes[modenum];
	DXGI_SWAP_CHAIN_DESC sd;

	if (SUCCEEDED (d3d11_SwapChain->GetDesc (&sd)))
	{
		// if the mode hasn't changed then unqueue the restart and get out
		if (!memcmp (mode, &sd.BufferDesc, sizeof (DXGI_MODE_DESC)))
		{
			vid.queuerestart = false;
			return;
		}
	}

	BOOL fullscreenstate = FALSE;
	IDXGIOutput *outputtarget = NULL;

	if (SUCCEEDED (d3d11_SwapChain->GetFullscreenState (&fullscreenstate, &outputtarget)))
	{
		// clear and present to get a valid screen
		D3DVid_ClearScreenAndPresent ();

		// suppress screen updates for the duration of the switch
		vid.block_drawing = true;

		// these will be properly recreated when the next frame runs
		D3DVid_ReleaseFrameBuffer ();

		// call the appropriate mode change routine
		if (vid_fullscreen.value && !fullscreenstate)
		{
			D3DVid_SetFullScreen (TRUE);
			D3DVid_ResizeTarget (mode);
		}
		else if (!vid_fullscreen.value && fullscreenstate)
		{
			D3DVid_SetFullScreen (FALSE);
			D3DVid_ResizeTarget (mode);
		}
		else D3DVid_ResizeTarget (mode);

		// ensure that the cvars are properly synced
		vid_mode.Set (modenum);
		vid_fullscreen.Set (vid_fullscreen.value ? 1.0f : 0.0f);

		// if we've selected this windowed mode then this becomes the new
		// windowed mode to switch to on Alt-Enter (likewise for fullscreen)
		if (vid_fullscreen.integer)
			d3d_LastFullscreenMode = modenum;
		else d3d_LastWindowedMode = modenum;

		// and disable the switch flag
		vid.block_drawing = false;
		vid.RecalcRefdef = true;
	}

	SAFE_RELEASE (outputtarget);

	vid.queuerestart = false;
}


void D3DVid_ToggleFullscreen (bool toggleon)
{
	if (!D3DVid_IsCreated ()) return;

	static int vid_oldfullscreen = vid_fullscreen.integer;

	if (toggleon && vid_oldfullscreen)
	{
		// go back to the modes we switched away from
		vid_mode.Set (d3d_LastFullscreenMode);
		vid_fullscreen.Set (1.0f);
		ShowWindow (vid.Window, SW_RESTORE);
	}
	else if (vid_fullscreen.integer)
	{
		// store out the old fullscreen state so that we know to switch back
		vid_oldfullscreen = vid_fullscreen.integer;

		// go back to the last valid windowed mode
		vid_mode.Set (d3d_LastWindowedMode);
		vid_fullscreen.Set (0.0f);
		ShowWindow (vid.Window, SW_MINIMIZE);
	}

	// switching away will suspend the renderer so we must run the restart manually (if one was queued)
	if (vid.queuerestart) D3DVid_RunQueuedRestart ();
}


void D3DVid_ToggleAltEnter (void)
{
	BOOL fullscreenstate = FALSE;
	IDXGIOutput *outputtarget = NULL;

	if (SUCCEEDED (d3d11_SwapChain->GetFullscreenState (&fullscreenstate, &outputtarget)))
	{
		if (fullscreenstate)
		{
			// if it started in a windowed mode then go to it; otherwise go to the
			// last valid windowed mode that was set; otherwise go to mode 0 windowed
			vid_mode.Set (d3d_LastWindowedMode);
			vid_fullscreen.Set (0.0f);
		}
		else
		{
			// this goes to the last good fullscreen mode used
			vid_mode.Set (d3d_LastFullscreenMode);
			vid_fullscreen.Set (1);
		}

		SAFE_RELEASE (outputtarget);
	}
}


/*
==============================================================================================================================

	MODE CHANGE HANDLERS

==============================================================================================================================
*/

// i could never get this working with an std::vector and i'm more inclined to spend my time doing productive things
// than fighting against C++ pretties
#define D3D11_MAX_HANDLERS	1024

struct d3d_initshutdownhandler_t
{
	char name[64];
	d3d11func_t OnInit;
	d3d11func_t OnShutdown;
};

d3d_initshutdownhandler_t d3d11_InitShutdownHandler[D3D11_MAX_HANDLERS];
int d3d11_NumHandlers = 0;

// do nothing function to avoid runtime checks
void D3DHander_NullFunction (void) {}

CD3DInitShutdownHandler::CD3DInitShutdownHandler (char *name, d3d11func_t oninit, d3d11func_t onshutdown)
{
	if (name)
		strcpy (d3d11_InitShutdownHandler[d3d11_NumHandlers].name, name);
	else strcpy (d3d11_InitShutdownHandler[d3d11_NumHandlers].name, "unnamed");

	if (oninit)
		d3d11_InitShutdownHandler[d3d11_NumHandlers].OnInit = oninit;
	else d3d11_InitShutdownHandler[d3d11_NumHandlers].OnInit = D3DHander_NullFunction;

	if (onshutdown)
		d3d11_InitShutdownHandler[d3d11_NumHandlers].OnShutdown = onshutdown;
	else d3d11_InitShutdownHandler[d3d11_NumHandlers].OnShutdown = D3DHander_NullFunction;

	d3d11_NumHandlers++;
}


void D3DVid_RunHandlers (int mode)
{
	// force a restart for syncing modes/etc up
	vid.queuerestart = true;

	if (mode & VH_SHUTDOWN)
	{
		for (int i = 0; i < d3d11_NumHandlers; i++)
			d3d11_InitShutdownHandler[i].OnShutdown ();

		// sync everything up before we go down
		D3DVid_ReleaseFrameBuffer ();
	}

	if (mode & VH_INIT)
	{
		for (int i = 0; i < d3d11_NumHandlers; i++)
			d3d11_InitShutdownHandler[i].OnInit ();

		// we lose our render targets after bringing everything down so we set them again
		D3DVid_FlushStates ();
	}

	D3DVID_SetCPUOptimizations (&vid_d3dxoptimizations);
	vid.RecalcRefdef = true;
}


/*
==============================================================================================================================

	VIDEO STARTUP

==============================================================================================================================
*/

// for building the menu after video comes up
void Menu_VideoBuild (void);
void ClearAllStates (void);

HRESULT D3DVid_AttemptDeviceCreation (DXGI_SWAP_CHAIN_DESC *sd, D3D_DRIVER_TYPE DriverType)
{
	if (COM_CheckParm ("-ref")) DriverType = D3D_DRIVER_TYPE_REFERENCE;
	if (COM_CheckParm ("-warp")) DriverType = D3D_DRIVER_TYPE_WARP;

	D3D_FEATURE_LEVEL featureLevelsIn[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};

	hr = D3D11CreateDeviceAndSwapChain (
		NULL,									// default adapter (required NULL: see D3D11CreateDevice remarks in SDK)
		DriverType,								// create a hardware device
		NULL,									// no software device
		D3D11_CREATE_DEVICE_SINGLETHREADED,		// single-threaded application
		featureLevelsIn,						// let d3d select the feature level to use
		ARRAYLENGTH (featureLevelsIn),			// number of feature levels
		D3D11_SDK_VERSION,						// sdk version
		sd,										// description of swap chain
		&d3d11_SwapChain,						// swap chain object
		&d3d11_Device,							// device object
		&d3d11_FeatureLevel,					// output feature level
		&d3d11_Context							// immediate context object
	);

	if (SUCCEEDED (hr))
	{
		char wctombbuf[128];

		wcstombs (wctombbuf, d3d11_AdapterDesc.Description, 128);
		Con_SafePrintf ("Initialized Direct3D on %s\n", wctombbuf);

		if (DriverType == D3D_DRIVER_TYPE_HARDWARE)
			Con_SafePrintf ("  Using driver type D3D_DRIVER_TYPE_HARDWARE\n");
		else if (DriverType == D3D_DRIVER_TYPE_REFERENCE)
			Con_SafePrintf ("  Using driver type D3D_DRIVER_TYPE_REFERENCE\n");
		else Con_SafePrintf ("  Using driver type D3D_DRIVER_TYPE_WARP\n");

		if (d3d11_FeatureLevel == D3D_FEATURE_LEVEL_11_0)
			Con_SafePrintf ("  Using feature level D3D_FEATURE_LEVEL_11_0\n");
		else if (d3d11_FeatureLevel == D3D_FEATURE_LEVEL_10_1)
			Con_SafePrintf ("  Using feature level D3D_FEATURE_LEVEL_10_1\n");
		else Con_SafePrintf ("  Using feature level D3D_FEATURE_LEVEL_10_0\n");

		Con_SafePrintf ("  Dedicated Video Memory: %i mb\n", (d3d11_AdapterDesc.DedicatedVideoMemory >> 20));
		Con_SafePrintf ("  Dedicated System Memory: %i mb\n", (d3d11_AdapterDesc.DedicatedSystemMemory >> 20));
		Con_SafePrintf ("  Shared System Memory: %i mb\n", (d3d11_AdapterDesc.SharedSystemMemory >> 20));

		Con_SafePrintf ("\n");
	}

	return hr;
}


void D3DVid_InitVideo (HWND hWnd, DXGI_MODE_DESC *mode)
{
	DXGI_SWAP_CHAIN_DESC sd;

	ZeroMemory (&sd, sizeof (sd));

	// make certain that we have a properly enumerated mode here
	Q_MemCpy (&sd.BufferDesc, mode, sizeof (DXGI_MODE_DESC));

	// fill in everything else
	sd.BufferCount = 1;	// number of backbuffers, the documentation is misleading here
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = d3d11_SampleDesc.Count;
	sd.SampleDesc.Quality = d3d11_SampleDesc.Quality;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	sd.Windowed = vid_fullscreen.value ? FALSE : TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	if (FAILED (D3DVid_AttemptDeviceCreation (&sd, D3D_DRIVER_TYPE_HARDWARE)))
	{
		if (FAILED (D3DVid_AttemptDeviceCreation (&sd, D3D_DRIVER_TYPE_WARP)))
		{
			Sys_Error ("D3DVid_InitVideo : Failed to init D3D");
			return;
		}
	}

	// now we disable stuff because alt-enter is currently broken
	IDXGIFactory1 *pFactory = NULL;

	if (SUCCEEDED (d3d11_SwapChain->GetParent (__uuidof (IDXGIFactory1), (void **) &pFactory)))
	{
		pFactory->MakeWindowAssociation (vid.Window, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
		pFactory->Release ();
	}

	D3DVid_UpdateOutput ();
	D3DVID_GetDeviceOptions ();
	d3d11_SwapChain->GetDesc (&sd);
	D3DVid_CreateFrameBuffer (&sd);

	// clear and present to get a valid screen
	D3DVid_ClearScreenAndPresent ();

	// call all of our init handlers
	D3DVID_SetDeviceOptions (NULL);

	if (!QWAD2::LoadPalette ()) Sys_Error ("Could not locate Quake on your computer\nFailed to load palette.lmp");
	if (!gfxwad.Load ("gfx.wad")) Sys_Error ("Could not locate Quake on your computer\nFailed to load gfx.wad");

	D3DVid_RunHandlers (VH_INIT);

	// force an immediate recalc of the refdef and disable any queued restarts
	vid.RecalcRefdef = true;
	vid.queuerestart = false;
}


void D3DVid_ShutdownVideo (void)
{
	// we're going down so we need to revert to windowed mode before we can destroy the swap chain
	// http://msdn.microsoft.com/en-us/library/windows/desktop/bb205075%28v=vs.85%29.aspx#Care_and_Feeding_of_the_Swap_Chain
	D3DVid_ClearScreenAndPresent ();
	D3DVid_SetFullScreen (FALSE);

	// take down all of our handlers
	D3DVid_RunHandlers (VH_SHUTDOWN);

	D3DVid_ReleaseFrameBuffer ();

	SAFE_RELEASE (d3d11_Context);
	SAFE_RELEASE (d3d11_SwapChain);
	SAFE_RELEASE (d3d11_Device);

	AppActivate (FALSE, FALSE, FALSE);
}


void VIDD3D_CreateWindow (DXGI_MODE_DESC *mode)
{
	// video isn't initialized until we say that it is
	vid.initialized = false;

	WNDCLASS wc = {
		0,
		MainWndProc,
		0,
		0,
		GetModuleHandle (NULL),
		hAppIcon,
		NULL,
		(HBRUSH) GetStockObject (BLACK_BRUSH),
		NULL,
		D3D_WINDOW_CLASS_NAME
	};

	if (!RegisterClass (&wc))
		Sys_Error ("D3D_CreateWindowClass: Failed to register Window Class");

	vid.WindowRect.left = 0;
	vid.WindowRect.right = mode->Width;
	vid.WindowRect.top = 0;
	vid.WindowRect.bottom = mode->Height;

	AdjustWindowRectEx (&vid.WindowRect, WindowStyle, FALSE, ExWindowStyle);
	SystemParametersInfo (SPI_GETWORKAREA, 0, &vid.WorkArea, 0);

	// create our default window that we're going to use with everything
	// this is created in it's initially centered position based on the current mode with and height
	vid.Window = CreateWindowEx (
		ExWindowStyle,
		D3D_WINDOW_CLASS_NAME,
		"DirectQ Release "DIRECTQ_VERSION,
		WindowStyle,
		vid.WorkArea.left + ((vid.WorkArea.right - vid.WorkArea.left) - (vid.WindowRect.right - vid.WindowRect.left)) / 2,
		vid.WorkArea.top + ((vid.WorkArea.bottom - vid.WorkArea.top) - (vid.WindowRect.bottom - vid.WindowRect.top)) / 2,
		(vid.WindowRect.right - vid.WindowRect.left),
		(vid.WindowRect.bottom - vid.WindowRect.top),
		NULL,
		NULL,
		GetModuleHandle (NULL),
		NULL
	);

	if (!vid.Window)
		Sys_Error ("Couldn't create window");

	ShowWindow (vid.Window, SW_SHOWDEFAULT);
	UpdateWindow (vid.Window);

	// update the client rect
	VIDWin32_GoToNewClientRect ();

	// create the mode and activate input
	SendMessage (vid.Window, WM_SETICON, (WPARAM) TRUE, (LPARAM) hAppIcon);
	SendMessage (vid.Window, WM_SETICON, (WPARAM) FALSE, (LPARAM) hAppIcon);

	// now run a message loop to bring things up to date
	Sys_SendKeyEvents ();
}


void VIDD3D_Init (void)
{
	// ensure
	memset (&d3d_RenderDef, 0, sizeof (d3d_renderdef_t));

	vid.canalttab = true;

	// enumerate available modes
	VIDD3D_EnumerateModes ();

	// set up the mode we'll start in
	if (vid_mode.integer < 0 || vid_mode.integer >= d3d_DisplayModes.size ())
	{
		// go back to safe mode
		vid_mode.Set (0.0f);
		vid_fullscreen.Set (0.0f);
		vid.queuerestart = false;
	}

	// ensure that it comes from a properly enumerated mode
	Q_MemCpy (&d3d_CurrentMode, &d3d_DisplayModes[vid_mode.integer], sizeof (DXGI_MODE_DESC));

	// if these are specified on the cmdline then we set them to non-zero and look for a matching mode
	int findwidth = (COM_CheckParm ("-width")) ? atoi (com_argv[COM_CheckParm ("-width") + 1]) : 0;
	int findheight = (COM_CheckParm ("-height")) ? atoi (com_argv[COM_CheckParm ("-height") + 1]) : 0;

	if (findwidth || findheight)
	{
		for (int i = 0; i < d3d_DisplayModes.size (); i++)
		{
			// if we're looking for the mode and we don't match then this isn't it
			if (findwidth && d3d_DisplayModes[i].Width != findwidth) continue;
			if (findheight && d3d_DisplayModes[i].Height != findheight) continue;

			// allow the override (ensure that it's a properly enumerated mode)
			Q_MemCpy (&d3d_CurrentMode, &d3d_DisplayModes[vid_mode.integer], sizeof (DXGI_MODE_DESC));

			// and this is the mode we want
			vid_mode.Set (i);
			break;
		}
	}

	// enable command-line override - this must happen after the mode find so that it can override any overridden refresh rate properly
	if (COM_CheckParm ("-window")) vid_fullscreen.Set (0.0f);

	if (COM_CheckParm ("-safe"))
	{
		// reset all cvars back to safe defaults and bring up in minimal windowed mode
		vid_mode.Set (0.0f);
		vid_fullscreen.Set (0.0f);

		// ensure that it's a properly enumerated mode
		Q_MemCpy (&d3d_CurrentMode, &d3d_DisplayModes[0], sizeof (DXGI_MODE_DESC));
	}

	Con_SafePrintf ("\n");

	// set the selected video mode
	// suspend stuff that could mess us up while creating the window
	bool temp = scr_disabled_for_loading;

	Host_DisableForLoading (true);
	CDAudio_Pause ();

	// and now create the main window that we'll use for everything else
	VIDD3D_CreateWindow (&d3d_CurrentMode);

	// set default mouse states
	IN_SetMouseState (vid_fullscreen.integer ? true : false);

	// set internal active flags otherwise we'll get 40 FPS and no mouse!!!
	AppActivate (TRUE, FALSE, FALSE);

	// now initialize direct 3d on the window
	D3DVid_InitVideo (vid.Window, &d3d_CurrentMode);

	// the video modes menu build is deferred until we know what kind of device we have
	Menu_VideoBuild ();

	// if it started in a windowed mode store that as the mode to switch to on Alt-Enter
	if (vid_fullscreen.integer)
	{
		d3d_LastFullscreenMode = vid_mode.integer;
		d3d_LastWindowedMode = 0;
	}
	else
	{
		d3d_LastFullscreenMode = D3DVid_FindModeForValue (-1);
		d3d_LastWindowedMode = vid_mode.integer;
	}

	// now resume the messy-uppy stuff
	CDAudio_Resume ();
	Host_DisableForLoading (temp);

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	// begin at 1 so that any newly allocated model_t will be 0 and therefore must
	// be explicitly set to be valid
	d3d_RenderDef.RegistrationSequence = 1;
}


/*
==============================================================================================================================

	MAIN SCENE

==============================================================================================================================
*/

bool D3DVid_BeginRendering (void)
{
	if (!D3DVid_IsCreated ()) return false;

	if (vid.occluded)
	{
		if ((d3d11_SwapChain->Present (0, DXGI_PRESENT_TEST)) != S_OK)
			return false;
		else
		{
			d3d11_SwapChain->Present (0, DXGI_PRESENT_RESTART);
			vid.occluded = false;
			vid.RecalcRefdef = true;
		}
	}

	if (vid.queuerestart)
	{
		D3DVid_RunQueuedRestart ();
		return false;
	}

	if (!d3d11_RenderTargetView || !d3d11_DepthBuffer || !d3d11_DepthStencilView)
	{
		DXGI_SWAP_CHAIN_DESC sd;

		if (SUCCEEDED (d3d11_SwapChain->GetDesc (&sd)))
		{
			D3DVid_CreateFrameBuffer (&sd);

			// ensure that everything is created
			d3d11_Context->Flush ();

			// and finish setting up it's styles/etc
			VIDWin32_CenterWindow ();
		}

		// recache/recalc everything
		D3DVid_FlushStates ();
		vid.RecalcRefdef = true;
		return false;
	}

	return true;
}


void D3DVid_EndRendering (void)
{
	if (!D3DVid_IsCreated ()) return;

	// apply gamma/contrast if needed
	Draw_BrightPass ();

	// present to screen and sync if requested by the user
	// sometimes we want to skip the present (e.g if we're taking a screenshot)
	if (!vid.nopresent)
	{
		if (cls.timedemo || cls.timerefresh)
			hr = d3d11_SwapChain->Present (0, 0);
		else hr = d3d11_SwapChain->Present (vid.syncinterval, 0);

		if (hr == DXGI_STATUS_OCCLUDED)
		{
			vid.occluded = true;
			return;
		}

		if (gl_finish.value) d3d11_Context->Flush ();
	}
	else vid.nopresent = false;

	// counter for FPS
	// this needs to be always incremented otherwise mapshots and/or screenshots will temporarily glitch
	// as merged bmodels flash off.  the fps counter will be slightly out for 0.25 seconds but that's a small cost
	d3d_RenderDef.presentcount++;
	d3d_RenderDef.framecount++;
}


