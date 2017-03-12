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

#include "menu_common.h"

extern std::vector<DXGI_MODE_DESC> d3d_DisplayModes;

char *menu_anisotropicmodes[] = {"Off", "2x", "4x", "8x", "16x", NULL};
int menu_anisonum = 0;

extern cvar_t vid_mode;
extern cvar_t vid_fullscreen;
extern cvar_t r_anisotropicfilter;
extern cvar_t vid_vsync;
extern cvar_t vid_maximumframelatency;
extern cvar_t vid_gputhreadpriority;
extern cvar_t vid_d3dxoptimizations;

extern cvar_t v_gamma;
extern cvar_t r_gamma;
extern cvar_t g_gamma;
extern cvar_t b_gamma;
extern cvar_t v_contrast;

int D3DState_GetTexfilterMode (void);
int D3DState_GetMipfilterMode (void);
void D3DState_UpdateFiltersFromMenu (int texfiltermode, int mipfiltermode);

char *filtermodes[] = {"None", "Point", "Linear", NULL};

int texfiltermode = 0;
int mipfiltermode = 0;

// dummy cvars for temp stuff
int dummy_fullscreen;

int menu_modenum = 0;
char **menu_modelist = NULL;
char **menu_reflist = NULL;
char **menu_scanlist = NULL;
char **menu_scalelist = NULL;

#define TAG_VIDMODEAPPLY		1
#define TAG_WINDOWED_ENABLE		256
#define TAG_FULLSCREEN_ENABLE	512
#define TAG_FULLSCREEN_OPTIONS	1024
#define TAG_ANISOTROPIC_OFF		4096

void D3DVid_DescribeMode (DXGI_MODE_DESC *mode);

void VID_ApplyModeChange (void)
{
	// these just signal a change is going at happen at the next screen update so it's safe to set them together
	vid_mode.Set (menu_modenum);
	vid_fullscreen.Set (dummy_fullscreen);

	// position the selection back up one as the "apply" option is no longer valid
	menu_Video.Key (K_UPARROW);
}


int Menu_VideoCustomDraw (int y)
{
	if (dummy_fullscreen)
		menu_Video.EnableMenuOptions (TAG_FULLSCREEN_OPTIONS);
	else menu_Video.DisableMenuOptions (TAG_FULLSCREEN_OPTIONS);

	if ((menu_modenum != vid_mode.integer) || (vid_fullscreen.integer != dummy_fullscreen))
		menu_Video.EnableMenuOptions (TAG_VIDMODEAPPLY);
	else menu_Video.DisableMenuOptions (TAG_VIDMODEAPPLY);

	D3DState_UpdateFiltersFromMenu (texfiltermode, mipfiltermode);

	// welcome OpenGL weenies!
	// this is how video hardware actually works.
	int anisotropy = 0;

	if (menu_anisonum == 0)
	{
		anisotropy = 1;
		menu_Video.EnableMenuOptions (TAG_ANISOTROPIC_OFF);
	}
	else
	{
		if (menu_anisonum == 1)
			anisotropy = 2;
		else if (menu_anisonum == 2)
			anisotropy = 4;
		else if (menu_anisonum == 3)
			anisotropy = 8;
		else anisotropy = 16;

		menu_Video.DisableMenuOptions (TAG_ANISOTROPIC_OFF);
	}

	if (anisotropy != r_anisotropicfilter.integer)
		r_anisotropicfilter.Set (anisotropy);

	return y;
}


void Menu_VideoCustomEnter (void)
{
	// decode the video mode and set currently selected stuff
	menu_modenum = vid_mode.integer;
	dummy_fullscreen = !!vid_fullscreen.integer;

	texfiltermode = D3DState_GetTexfilterMode ();
	mipfiltermode = D3DState_GetMipfilterMode ();

	if (r_anisotropicfilter.value > 8)
		menu_anisonum = 4;
	else if (r_anisotropicfilter.value > 4)
		menu_anisonum = 3;
	else if (r_anisotropicfilter.value > 2)
		menu_anisonum = 2;
	else if (r_anisotropicfilter.value > 1)
		menu_anisonum = 1;
	else menu_anisonum = 0;
}


static char *scanorderlist[] =
{
	"Unspecified",
	"Progressive",
	"Upper Field First",
	"Lower Field First"
};


static char *scalinglist[] =
{
	"Unspecified",
	"Centered",
	"Stretched"
};


void Menu_VideoBuild (void)
{
	// add the enumerated display modes to the menu
	menu_modelist = (char **) MainZone->Alloc ((d3d_DisplayModes.size () + 1) * sizeof (char *));
	menu_reflist = (char **) MainZone->Alloc ((d3d_DisplayModes.size () + 1) * sizeof (char *));
	menu_scanlist = (char **) MainZone->Alloc ((d3d_DisplayModes.size () + 1) * sizeof (char *));
	menu_scalelist = (char **) MainZone->Alloc ((d3d_DisplayModes.size () + 1) * sizeof (char *));

	for (int i = 0; i < d3d_DisplayModes.size (); i++)
	{
		menu_modelist[i] = (char *) MainZone->Alloc (32);
		sprintf (menu_modelist[i], "%i x %i", d3d_DisplayModes[i].Width, d3d_DisplayModes[i].Height);

		menu_scanlist[i] = scanorderlist[d3d_DisplayModes[i].ScanlineOrdering];
		menu_scalelist[i] = scalinglist[d3d_DisplayModes[i].Scaling];
		
		if (d3d_DisplayModes[i].RefreshRate.Denominator == 1)
		{
			menu_reflist[i] = (char *) MainZone->Alloc (16);
			sprintf (menu_reflist[i], "%i hz", d3d_DisplayModes[i].RefreshRate.Numerator);
		}
		else
		{
			menu_reflist[i] = (char *) MainZone->Alloc (16);
			sprintf (menu_reflist[i], "%0.2f hz", (float) d3d_DisplayModes[i].RefreshRate.Numerator / (float) d3d_DisplayModes[i].RefreshRate.Denominator);
		}

		// NULL-term the lists
		menu_modelist[i + 1] = NULL;
		menu_reflist[i + 1] = NULL;
		menu_scanlist[i + 1] = NULL;
		menu_scalelist[i + 1] = NULL;
	}

	menu_Video.AddOption (new CQMenuCustomDraw (Menu_VideoCustomDraw));
	menu_Video.AddOption (new CQMenuSpinControl ("Video Mode", &menu_modenum, &menu_modelist));
	menu_Video.AddOption (TAG_FULLSCREEN_OPTIONS, new CQMenuSpinControl ("Refresh Rate", &menu_modenum, &menu_reflist));
	menu_Video.AddOption (TAG_FULLSCREEN_OPTIONS, new CQMenuSpinControl ("Scanline Order", &menu_modenum, &menu_scanlist));
	menu_Video.AddOption (TAG_FULLSCREEN_OPTIONS, new CQMenuSpinControl ("Scaling", &menu_modenum, &menu_scalelist));
	menu_Video.AddOption (new CQMenuIntegerToggle ("Fullscreen", &dummy_fullscreen, 0, 1));

	menu_Video.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Video.AddOption (TAG_VIDMODEAPPLY, new CQMenuCommand ("Apply Video Mode Change", VID_ApplyModeChange));

	// add the rest of the options to ensure that they;re kept in order
	menu_Video.AddOption (new CQMenuSpacer ());
	menu_Video.AddOption (new CQMenuTitle ("Configure Video Options"));

	menu_Video.AddOption (new CQMenuCvarToggle ("Vertical Sync", &vid_vsync));
	menu_Video.AddOption (new CQMenuCvarSlider ("Prerendered Frames", &vid_maximumframelatency, 1, 16, 1));
	menu_Video.AddOption (new CQMenuCvarSlider ("GPU Thread Priority", &vid_gputhreadpriority, -7, 7, 1));
	menu_Video.AddOption (new CQMenuCvarToggle ("D3DX Optimizations", &vid_d3dxoptimizations));

	// these are added after the gammaworks check
	menu_Video.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Video.AddOption (new CQMenuCvarSlider ("Brightness", &v_gamma, 1.75f, 0.25f, 0.05f));
	menu_Video.AddOption (new CQMenuCvarSlider ("Contrast", &v_contrast, 0.25f, 1.75f, 0.05f));

	menu_Video.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Video.AddOption (TAG_ANISOTROPIC_OFF, new CQMenuSpinControl ("Texture Filter", &texfiltermode, &filtermodes[1]));
	menu_Video.AddOption (TAG_ANISOTROPIC_OFF, new CQMenuSpinControl ("Mipmap Filter", &mipfiltermode, filtermodes));
	menu_Video.AddOption (new CQMenuSpinControl ("Anisotropic Filter", &menu_anisonum, menu_anisotropicmodes));
}


