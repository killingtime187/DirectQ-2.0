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

// screen.c -- master for refresh, status bar, console, chat, notify, etc


#include "quakedef.h"
#include "d3d_model.h"
#include "d3d_quake.h"

void R_RenderView (void);
void Menu_PrintCenterWhite (int cy, char *str);
void Menu_PrintWhite (int cx, int cy, char *str);

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions


syncronous draw mode or async
One off screen buffer, with updates either copied or xblited


async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint ()
SlowPrint ()
Screen_Update ();
Con_Printf ();

net
turn off messages option

the refresh is allways rendered, unless the console is full screen


console is:
	notify lines
	half
	full


*/


bool scr_drawloading = false;
int scr_loadmaxscreens = 0;


void SCR_RefdefCvarChange (cvar_t *blah)
{
	vid.RecalcRefdef = true;
}


float		scr_con_current;

cvar_t		scr_showcoords ("scr_showcoords", "0");

// timeout when loading plaque is up
#define SCR_DEFTIMEOUT 60
float		scr_timeout;

float		oldscreensize, oldfov, oldconscale, oldhudbgfill, oldsbaralpha;

extern cvar_t gl_conscale;
extern cvar_t scr_sbarscale;
extern cvar_t scr_menuscale;
extern cvar_t scr_conscale;

cvar_t		scr_viewsize ("viewsize", 100, CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t		scr_fov ("fov", 90, 0, SCR_RefdefCvarChange);	// 10 - 170
cvar_t		scr_fovcompat ("fov_compatible", 0.0f, CVAR_ARCHIVE, SCR_RefdefCvarChange);
cvar_t		scr_conspeed ("scr_conspeed", 3000);
cvar_t		scr_centertime ("scr_centertime", 2);
cvar_t		scr_centerlog ("scr_centerlog", 1, CVAR_ARCHIVE);
cvar_t		scr_showram ("showram", 1);
cvar_t		scr_showturtle ("showturtle", "0");
cvar_t		scr_showpause ("showpause", 1);
cvar_t		scr_printspeed ("scr_printspeed", 10);

cvar_t scr_shotnamebase ("scr_shotnamebase", "Quake", CVAR_ARCHIVE);
cvar_t scr_screenshotdir ("scr_screenshotdir", "screenshot/", CVAR_ARCHIVE, COM_ValidateUserSettableDir);

cvar_t	r_automapshot ("r_automapshot", "0", CVAR_ARCHIVE);

// darkplaces
cvar_t scr_screenshot_gammaboost ("scr_screenshot_gammaboost", "1", CVAR_ARCHIVE);

// sanity - it's not a gamma boost, it's a gamma *modification*
cvar_alias_t scr_screenshot_gamma ("scr_screenshot_gamma", &scr_screenshot_gammaboost);

extern	cvar_t	crosshair;

bool	scr_initialized;		// ready to draw

qpic_t		*scr_ram;
qpic_t		*scr_net;
qpic_t		*scr_turtle;

int			clearconsole;
int			clearnotify;

bool	scr_disabled_for_loading;
bool	scr_drawmapshot;
float	saved_viewsize = 0;

float		scr_disabled_time;


bool scr_capturedepth = false;
void SCR_ScreenShot_f (void);
void SCR_CaptureDepth_f (void);
void HUD_DrawHUD (void);


void Scr_InitPics (void)
{
	scr_ram = Draw_LoadPic ("ram");
	scr_net = Draw_LoadPic ("net");
	scr_turtle = Draw_LoadPic ("turtle");
	scr_initialized = true;
}


/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[1024];
float		ScrCenterTimeStart;	// for slow victory printing
float		ScrCenterTimeOff;
int			scr_center_lines;
int			scr_erase_lines;
int			scr_erase_center;
int			scr_center_width;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (char *str)
{
	// the server sometimes sends a blank centerstring for to clear a previous centerstring
	if (!str || !str[0])
	{
		// an empty print is sometimes used to explicitly clear the previous centerprint
		SCR_ClearCenterString ();
		return;
	}

	// only log if the previous centerprint has already been cleared (cl.time for timedemo compat)
	if (scr_centerlog.integer && !cl.intermission && ScrCenterTimeOff <= cl.time)
	{
		static char scr_lastcenterstring[1024] = {0};

		// don't log duplicates - can't use scr_centerstring here because this breaks if it's cleared
		if (strcmp (str, scr_lastcenterstring))
		{
			Con_SilentPrintf ("\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n");
			Con_SilentPrintf ("\n%s\n\n", str);
			Con_SilentPrintf ("\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");

			// ensure (required as a standard Con_Printf immediately following a centerprint will screw up the notify lines)
			Con_ClearNotify ();

			// copy back
			Q_strncpy (scr_lastcenterstring, str, sizeof (scr_lastcenterstring) - 1);
		}
	}

	Q_strncpy (scr_centerstring, str, sizeof (scr_centerstring) - 1);

	// use cl.time for timedemo compatibility
	ScrCenterTimeOff = cl.time + scr_centertime.value;
	ScrCenterTimeStart = cl.time;

	// count the number of lines for centering
	scr_center_lines = 1;
	scr_center_width = 0;
	int widthcount = 0;

	while (*str)
	{
		if (*str == '\n')
		{
			scr_center_lines++;

			if (widthcount > scr_center_width) scr_center_width = widthcount;

			widthcount = 0;
		}

		str++;
		widthcount++;
	}

	// single line
	if (widthcount > scr_center_width) scr_center_width = widthcount;
}


int finaley = 0;

// some of sandy's e4 messages are offset strangely so we use this to center them properly
// (fixme - will this break qc menu hacks?)
void Menu_PrintCenterWhite (int cy, char *str);

void SCR_DrawCenterString (void)
{
	char	*start;
	int		l;
	int		j;
	int		x, y;
	int		remaining;

	// the finale prints the characters one at a time
	// needs to use cl.time for demos
	if (cl.intermission)
		remaining = scr_printspeed.value * (cl.time - ScrCenterTimeStart);
	else remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	finaley = y = (vid.currsize.height - (scr_center_lines * 10)) / 3;

#if 0
	// some of sandy's e4 messages are offset strangely so we use this to center them properly
	// (fixme - will this break qc menu hacks?)
	for (int i = 0; ; i++)
	{
		if (!scr_centerstring[i])
		{
			Menu_PrintCenterWhite (y, start);
			break;
		}

		if (scr_centerstring[i] == '\n')
		{
			scr_centerstring[i] = 0;
			Menu_PrintCenterWhite (y, start);
			scr_centerstring[i] = '\n';
			start = &scr_centerstring[i + 1];
			y += 10;
		}
	}
#else
	do
	{
		// scan the width of the line
		for (l = 0; l < 70; l++)
			if (start[l] == '\n' || !start[l])
				break;

		x = (vid.currsize.width - l * 8) / 2;

		for (j = 0; j < l; j++, x += 8)
		{
			Draw_Character (x, y, start[j]);

			if (!remaining--)
				return;
		}

		y += 10;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;

		// skip the \n
		start++;
#pragma warning (disable: 4127)
		// suppress conditional expression is constant so that we can use this construct
	} while (1);
#pragma warning (1: 4127)
#endif
}

void SCR_CheckDrawCenterString (void)
{
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	if (scr_drawloading)
	{
		SCR_ClearCenterString ();
		return;
	}

	// bug - this will potentially print the last seen centerprint during the end intermission!!!
	// (cl.time for timedemo compat)
	if (ScrCenterTimeOff <= cl.time && !cl.intermission)
	{
		SCR_ClearCenterString ();
		return;
	}

	if (key_dest != key_game)
	{
		// ensure it's off
		SCR_ClearCenterString ();
		return;
	}

	// should never happen
	if (!scr_centerstring[0])
	{
		SCR_ClearCenterString ();
		return;
	}

	D3DDraw_SetRect (0, 0, vid.sbarsize.width, vid.sbarsize.height);
	SCR_DrawCenterString ();
}


void SCR_ClearCenterString (void)
{
	scr_centerstring[0] = 0;
	ScrCenterTimeOff = -1;
}


//=============================================================================

/*
====================
SCR_CalcFovY
====================
*/
float SCR_CalcFovX (float fov_y, float width, float height)
{
	float   a;
	float   y;

	// bound, don't crash
	if (fov_y < 1) fov_y = 1;
	if (fov_y > 179) fov_y = 179;

	y = height / tan (fov_y / 360 * D3DX_PI);
	a = atan (width / y);
	a = a * 360 / D3DX_PI;

	return a;
}


float SCR_CalcFovY (float fov_x, float width, float height)
{
	float   a;
	float   x;

	// bound, don't crash
	if (fov_x < 1) fov_x = 1;
	if (fov_x > 179) fov_x = 179;

	x = width / tan (fov_x / 360 * D3DX_PI);
	a = atan (height / x);
	a = a * 360 / D3DX_PI;

	return a;
}


/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
extern cvar_t scr_sbaralpha;

void SCR_CalcGUIScaleFactor (cvar_t *var, sizedef_t *sizedef, sizedef_t *basesize, sizedef_t *sizeclamp)
{
	sizedef->width = (float) basesize->width / var->value;
	sizedef->height = (float) basesize->height / var->value;

	if (sizedef->width < sizeclamp->width || sizedef->height < sizeclamp->height)
	{
		sizedef->width = sizeclamp->width;
		sizedef->height = sizeclamp->height;
	}

	if (sizedef->width > d3d_CurrentMode.Width || sizedef->height > d3d_CurrentMode.Height)
	{
		sizedef->width = d3d_CurrentMode.Width;
		sizedef->height = d3d_CurrentMode.Height;
	}
}


void SCR_SetFOV (float *fovx, float *fovy, float fovvar, int width, int height, bool guncalc)
{
	float aspect = (float) height / (float) width;

#define BASELINE_W	640.0f
#define BASELINE_H	432.0f

	// http://www.gamedev.net/topic/431111-perspective-math-calculating-horisontal-fov-from-vertical/
	// horizontalFov = atan( tan(verticalFov) * aspectratio )
	// verticalFov = atan( tan(horizontalFov) / aspectratio )
	if ((scr_fovcompat.integer || aspect > (BASELINE_H / BASELINE_W)) && !guncalc)
	{
		// use the same calculation as GLQuake did
		// (horizontal is constant, vertical varies)
		fovx[0] = fovvar;
		fovy[0] = SCR_CalcFovY (fovx[0], width, height);
	}
	else
	{
		// alternate calculation (vertical is constant, horizontal varies)
		// consistent with http://www.emsai.net/projects/widescreen/fovcalc/
		// note that the gun always uses this calculation irrespective of the aspect)
		fovy[0] = SCR_CalcFovY (fovvar, BASELINE_W, BASELINE_H);
		fovx[0] = SCR_CalcFovX (fovy[0], width, height);
	}
}


extern cvar_t cl_sbar;

static void SCR_CalcRefdef (void)
{
	if (!D3DVid_IsCreated ()) return;

	// don't need a recalc
	vid.RecalcRefdef = false;

	// ensure that everything gets synced up nicely
	D3DVid_FlushStates ();

	// rebuild world surfaces
	d3d_RenderDef.rebuildworld = true;

	// bound viewsize
	if (scr_viewsize.value < 100) scr_viewsize.Set (100);
	if (scr_viewsize.value > 120) scr_viewsize.Set (120);

	// bound field of view
	if (scr_fov.value < 10) scr_fov.Set (10);
	if (scr_fov.value > 170) scr_fov.Set (170);

	// conditions for switching off the HUD - viewsize 120 always switches it off, period
	if (cl.intermission || scr_viewsize.value > 110 || cl.stats[STAT_HEALTH] < 1)
		sb_lines = 0;
	else if (scr_viewsize.value > 100)
		sb_lines = 24;
	else sb_lines = 48;

	// only the classic hud uses lines
	if (cl_sbar.integer) sb_lines = 0;

	// bound console scale
	if (gl_conscale.value < 0) gl_conscale.Set (0.0f);
	if (gl_conscale.value > 1) gl_conscale.Set (1.0f);
	if (scr_sbarscale.value < 1) scr_sbarscale.Set (1.0f);
	if (scr_menuscale.value < 1) scr_menuscale.Set (1.0f);
	if (scr_conscale.value < 1) scr_conscale.Set (1.0f);

	// adjust a basesize.width and basesize.height to match the mode aspect
	// they should be the same aspect as the mode, with width never less than 640 and height never less than 480
	sizedef_t basesize = {(480 * d3d_CurrentMode.Width) / d3d_CurrentMode.Height, 480};

	// bring it up to 640
	if (basesize.width < 640)
	{
		basesize.width = 640;
		basesize.height = basesize.width * d3d_CurrentMode.Height / d3d_CurrentMode.Width;
	}

	// clamp
	if (basesize.width > d3d_CurrentMode.Width) basesize.width = d3d_CurrentMode.Width;
	if (basesize.height > d3d_CurrentMode.Height) basesize.height = d3d_CurrentMode.Height;

	// set width and height from our gl_conscale cvar
	sizedef_t fullsize =
	{
		(d3d_CurrentMode.Width - basesize.width) * gl_conscale.value + basesize.width,
		(d3d_CurrentMode.Height - basesize.height) * gl_conscale.value + basesize.height
	};

	// eval our GUI scale factors
	SCR_CalcGUIScaleFactor (&scr_sbarscale, &vid.sbarsize, &fullsize, &basesize);
	SCR_CalcGUIScaleFactor (&scr_menuscale, &vid.menusize, &fullsize, &basesize);
	SCR_CalcGUIScaleFactor (&scr_conscale, &vid.consize, &fullsize, &basesize);

	// calc our sbar lines portion
	vid.sbar_lines = sb_lines;
	vid.sbar_lines *= d3d_CurrentMode.Height;
	vid.sbar_lines /= vid.sbarsize.height;

	// and finally calc our 3D refresh size
	vid.ref3dsize.width = d3d_CurrentMode.Width;
	vid.ref3dsize.height = d3d_CurrentMode.Height - vid.sbar_lines;

	SCR_SetFOV (&vid.fov_x, &vid.fov_y, scr_fov.value, vid.ref3dsize.width, vid.ref3dsize.height, false);
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	scr_viewsize.Set (scr_viewsize.value + 10);
	vid.RecalcRefdef = true;
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	scr_viewsize.Set (scr_viewsize.value - 10);
	vid.RecalcRefdef = true;
}


//============================================================================

/*
==================
SCR_Init
==================
*/
cmd_t SCR_ScreenShot_f_Cmd ("screenshot", SCR_ScreenShot_f);
cmd_t SCR_CaptureDepth_f_Cmd ("capturedepth", SCR_CaptureDepth_f);
cmd_t SCR_SizeUp_f_Cmd ("sizeup", SCR_SizeUp_f);
cmd_t SCR_SizeDown_f_Cmd ("sizedown", SCR_SizeDown_f);


/*
==============
SCR_DrawTurtle
==============
*/
void SCR_DrawTurtle (void)
{
	static double turtletime = 0;
	static int	count = 0;
	double frametime = CHostTimer::realtime - turtletime;

	turtletime = CHostTimer::realtime;

	if (!scr_showturtle.value)
		return;

	if (frametime < 0.1f)
	{
		count = 0;
		return;
	}

	count++;

	if (count < 3)
		return;

	D3DDraw_SetRect (0, 0, vid.sbarsize.width, vid.sbarsize.height);
	Draw_Pic (vid.currsize.width - 132, 4, scr_turtle, 1, true);
}

/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (void)
{
	if (CHostTimer::realtime - cl.lastrecievedmessage < 0.3f) return;
	if (cls.demoplayback) return;
	if (sv.active) return;

	D3DDraw_SetRect (0, 0, vid.sbarsize.width, vid.sbarsize.height);
	Draw_Pic (vid.currsize.width - 168, 4, scr_net, 1, true);
}

/*
==============
DrawPause
==============
*/
void SCR_DrawPause (void)
{
	extern qpic_t *gfx_pause_lmp;

	if (!scr_showpause.value) return;
	if (!cl.paused) return;

	D3DDraw_SetRect (0, 0, vid.sbarsize.width, vid.sbarsize.height);
	Draw_Pic ((vid.currsize.width - gfx_pause_lmp->width) / 2, (vid.currsize.height - 48 - gfx_pause_lmp->height) / 2, gfx_pause_lmp, 1, true);
}


/*
==================
SCR_SetUpToDrawConsole
==================
*/
void SCR_SetUpToDrawConsole (void)
{
	// console timings are based on a delta between current CHostTimer::realtime and previous CHostTimer::realtime
	// so that they can be independent of changes made to host_framerate or host_timescale.
	// scr_conlines and scr_con_current are now percentages of the full screen size instead
	// of line counts so that they are independent of resolution.  the old architecture was evil.

	float scr_conlines = 0;
	static double oldcontime = CHostTimer::realtime;
	double frametime = CHostTimer::realtime - oldcontime;

	oldcontime = CHostTimer::realtime;

	Con_CheckResize ();

	if (cls.signon != SIGNON_CONNECTED || !cls.maprunning)
	{
		// full screen
		scr_con_current = 100;
		con_notifylines = 0;
		return;
	}

	// scr_conlines is the number we want to draw
	if (key_dest == key_console)
		scr_conlines = 50;	// half screen
	else scr_conlines = 0;	// none visible

	if (scr_conlines < scr_con_current)
	{
		// going up
		scr_con_current -= scr_conspeed.value * frametime * 0.1f;

		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;
	}
	else if (scr_conlines > scr_con_current)
	{
		// going down
		scr_con_current += scr_conspeed.value * frametime * 0.1f;

		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}

	if (scr_con_current < 0) scr_con_current = 0;
	if (scr_con_current > 100) scr_con_current = 100;

	con_notifylines = 0;
}


/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	if (scr_con_current > 0)
	{
		D3DDraw_SetRect (0, 0, vid.consize.width, vid.consize.height);
		Con_DrawConsole (((float) vid.consize.height * scr_con_current) / 100.0f, true);
		clearconsole = 0;
	}
	else
	{
		if (key_dest == key_game || key_dest == key_message)
			Con_DrawNotify ();	// only draw notify in game
	}
}


void SCR_WriteDataToTGA (char *name, byte *buffer, int width, int height, int bpp)
{
	if (bpp == 8) bpp = 1;
	if (bpp == 24) bpp = 3;
	if (bpp == 32) bpp = 4;

	if (!(bpp == 1 || bpp == 3 || bpp == 4)) return;

	byte header[18];

	memset (header, 0, 18);
	header[2] = 2;		// uncompressed type
	header[12] = width & 255;
	header[13] = width >> 8;
	header[14] = height & 255;
	header[15] = height >> 8;
	header[16] = bpp << 3;	// pixel size
	header[17] = 0x20;	// flip

	std::ofstream f (name, std::ios::out | std::ios::binary);

	if (f.is_open ())
	{
		f.write ((char *) header, 18);
		f.write ((char *) buffer, width * height * bpp);
		f.close ();
	}
}


/*
==============================================================================

						SCREEN SHOTS

==============================================================================
*/


byte *SCR_GetScreenData (D3D11_TEXTURE2D_DESC *desc, float gamma, bool mapshot)
{
	byte gammatab[256];
	byte *buffer = NULL;

	// build a gamma table
	if (gamma == 1.0f)
	{
		for (int i = 0; i < 256; i++)
			gammatab[i] = i;
	}
	else
	{
		for (int i = 0; i < 256; i++)
		{
			float f = pow ((i + 1) / 256.0f, gamma);
			float inf = f * 255 + 0.5;

			if (inf < 0) inf = 0;
			if (inf > 255) inf = 255;

			gammatab[i] = inf;
		}
	}

	ID3D11Texture2D *scr_backbuffer = NULL;
	ID3D11Texture2D *scr_copy = NULL;

	if (SUCCEEDED (d3d11_SwapChain->GetBuffer (0, __uuidof (ID3D11Texture2D), (LPVOID *) &scr_backbuffer)))
	{
		// ensure that it matches
		scr_backbuffer->GetDesc (desc);

		// set the stuff we want to differ
		desc->Usage = D3D11_USAGE_STAGING;
		desc->BindFlags = 0;
		desc->CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;

		// fuckety fuck, where's my duck?
		// come back stretchrect, all is forgiven!!!
		D3D11_BOX shotbox = {0, 0, 0, desc->Width, desc->Height, 1};

		if (mapshot)
		{
			desc->Height -= vid.sbar_lines;
			shotbox.bottom -= vid.sbar_lines;

			if (desc->Width > desc->Height)
			{
				shotbox.left = (desc->Width - desc->Height) >> 1;
				shotbox.right = shotbox.left + desc->Height;
				desc->Width = desc->Height;
			}
			else
			{
				shotbox.top = (desc->Height - desc->Width) >> 1;
				shotbox.bottom = shotbox.top + desc->Width;
				desc->Height = desc->Width;
			}
		}

		if (SUCCEEDED (d3d11_Device->CreateTexture2D (desc, NULL, &scr_copy)))
		{
			d3d11_Context->CopySubresourceRegion (scr_copy, 0, 0, 0, 0, scr_backbuffer, 0, &shotbox);
			d3d11_Context->Flush ();

			D3D11_MAPPED_SUBRESOURCE MappedResource;

			if (SUCCEEDED (d3d11_Context->Map (scr_copy, 0, D3D11_MAP_READ_WRITE, 0, &MappedResource)))
			{
				// set first so that mapshots can change it
				byte *rgba = (byte *) MappedResource.pData;

				// discard the row pitch
				D3DImage_CompressRowPitch ((unsigned *) rgba, desc->Width, desc->Height, MappedResource.RowPitch >> 2);

				// change for mapshots
				if (mapshot && (desc->Width != 128 || desc->Height != 128))
				{
					// come back StretchRect, all is forgiven!!!
					byte *mapdata = (byte *) TempHunk->FastAlloc (128 * 128 * 4);

					// this is FINE for this particular purpose.  it WORKS.  mapshots don't need ultra-high resampling ANYWAY.
					D3DImage_Resample ((unsigned *) rgba, desc->Width, desc->Height, (unsigned *) mapdata, 128, 128);

					rgba = mapdata;
					desc->Width = 128;
					desc->Height = 128;
				}

				// and this is the buffer we'll use
				buffer = (byte *) TempHunk->FastAlloc (desc->Width * desc->Height * 3);

				// compress to 3 component, swap to BGR and apply gamma
				if (desc->Format == DXGI_FORMAT_R8G8B8A8_UNORM)
					D3DImage_Compress4to3WithSwapToBGRandGamma (rgba, buffer, desc->Width, desc->Height, gammatab);
				else if (desc->Format == DXGI_FORMAT_B8G8R8A8_UNORM)
					D3DImage_Compress4to3WithGamma (rgba, buffer, desc->Width, desc->Height, gammatab);
				else memset (buffer, 0, desc->Width * desc->Height * 3);

				d3d11_Context->Unmap (scr_copy, 0);
			}
			else Con_Printf ("SCR_GetScreenData : failed to map scratch texture\n");

			scr_copy->Release ();
		}
		else Con_Printf ("SCR_GetScreenData : failed to create scratch texture\n");

		scr_backbuffer->Release ();
	}
	else Con_Printf ("SCR_GetScreenData : failed to get backbuffer\n");

	return buffer;
}


/*
==================
SCR_ScreenShot_f
==================
*/
void SCR_ScreenShot_f (void)
{
	if (!COM_ValidateContentFolderCvar (&scr_screenshotdir)) return;

	// clear the sound buffer as this can take some time
	S_ClearBuffer ();

	char		checkname[MAX_PATH];
	int			i;

	// we only support TGA in d3d11
	if (Cmd_Argc () > 1)
	{
		// specify the name
		Q_snprintf (checkname, 128, "%s/%s%s.tga", com_gamedir, scr_screenshotdir.string, Cmd_Argv (1));
	}
	else
	{
		// find a file name to save it to
		for (i = 0; i <= 9999; i++)
		{
			Q_snprintf (checkname, 128, "%s/%s%s%04i.tga", com_gamedir, scr_screenshotdir.string, scr_shotnamebase.string, i);

			// file doesn't exist (fixme - replace this with our fs table checker)
			if (!Sys_FileExists (checkname)) break;
		}

		if (i == 10000)
		{
			Con_Printf ("SCR_ScreenShot_f: 9999 Screenshots exceeded.\n");
			return;
		}
	}

	// run a screen refresh
	// suppress present to overdraw the current back buffer and prevent screen flicker while this is happening
	vid.nopresent = true;
	vid.nobrightpass = true;
	d3d_RenderDef.rebuildworld = true;
	SCR_UpdateScreen ();

	byte *buffer = NULL;
	int hunkmark = TempHunk->GetLowMark ();
	D3D11_TEXTURE2D_DESC desc;

	// didn't get it for some reason
	if (!(buffer = SCR_GetScreenData (&desc, scr_screenshot_gammaboost.value, false)))
	{
		TempHunk->FreeToLowMark (hunkmark);
		Con_Printf ("SCR_Screenshot_f : failed\n");
		return;
	}

	SCR_WriteDataToTGA (checkname, buffer, desc.Width, desc.Height, 24);

	TempHunk->FreeToLowMark (hunkmark);

	// report
	Con_Printf ("Wrote %s\n", checkname);
}


void Draw_InvalidateMapshot (void);


void SCR_Mapshot_f (char *shotname, bool report, bool overwrite)
{
	// clear the sound buffer as this can take some time
	S_ClearBuffer ();

	char workingname[1025];

	// copy the name out so that we can safely modify it
	Q_strncpy (workingname, shotname, 255);

	// ensure that we have some kind of extension on it - anything will do
	COM_DefaultExtension (workingname, ".blah");

	// now put the correct extension on it
	for (int c = strlen (workingname) - 1; c; c--)
	{
		if (workingname[c] == '.')
		{
			strcpy (&workingname[c + 1], "tga");
			break;
		}
	}

	if (!overwrite)
	{
		// check does it exist
		if (Sys_FileExists (workingname))
		{
			Con_DPrintf ("SCR_Mapshot_f : Overwrite of \"%s\" prevented\n", workingname);
			return;
		}
	}

	// go into mapshot mode and do a screen refresh to get rid of any UI/etc
	// suppress present to overdraw the current back buffer and prevent screen flicker while this is happening
	scr_drawmapshot = true;
	vid.nopresent = true;
	vid.nobrightpass = true;
	d3d_RenderDef.rebuildworld = true;
	SCR_UpdateScreen ();
	scr_drawmapshot = false;

	byte *buffer = NULL;
	int hunkmark = TempHunk->GetLowMark ();
	D3D11_TEXTURE2D_DESC desc;

	// didn't get it for some reason
	if (!(buffer = SCR_GetScreenData (&desc, 1.0f, true)))
	{
		Con_Printf ("SCR_Mapshot_f : failed\n");
		return;
	}

	SCR_WriteDataToTGA (workingname, buffer, desc.Width, desc.Height, 24);

	TempHunk->FreeToLowMark (hunkmark);

	// invalidate the cached mapshot
	Draw_InvalidateMapshot ();
}


void SCR_Mapshot_cmd (void)
{
	if (!cls.maprunning) return;
	if (cls.state != ca_connected) return;

	// first ensure we have a "maps" directory
	CreateDirectory (va ("%s/maps", com_gamedir), NULL);

	// now take the mapshot; this is user initiated so always report and overwrite
	SCR_Mapshot_f (va ("%s/%s", com_gamedir, cl.worldmodel->name), true, true);
}


cmd_t Mapshot_Cmd ("mapshot", SCR_Mapshot_cmd);


void SCR_CaptureDepth_f (void)
{
	// this is just a debugging aid to help check that we're writing to depth when we should and not writing when we shouldn't
	if (!scr_capturedepth)
	{
		scr_capturedepth = true;
		return;
	}

	extern ID3D11Texture2D *d3d11_DepthBuffer;

	if (d3d11_DepthBuffer)
	{
		D3D11_TEXTURE2D_DESC descDepth;
		ID3D11Texture2D *d3d11_CaptureDepth;

		d3d11_DepthBuffer->GetDesc (&descDepth);

		descDepth.Usage = D3D11_USAGE_STAGING;
		descDepth.BindFlags = 0;
		descDepth.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;

		d3d11_Device->CreateTexture2D (&descDepth, NULL, &d3d11_CaptureDepth);

		if (d3d11_CaptureDepth)
		{
			d3d11_Context->CopyResource (d3d11_CaptureDepth, d3d11_DepthBuffer);

			D3D11_MAPPED_SUBRESOURCE MappedResource;

			if (SUCCEEDED (d3d11_Context->Map (d3d11_CaptureDepth, 0, D3D11_MAP_READ_WRITE, 0, &MappedResource)))
			{
				D3DImage_CompressRowPitch ((unsigned *) MappedResource.pData, descDepth.Width, descDepth.Height, MappedResource.RowPitch >> 2);
				SCR_WriteDataToTGA ("depth.tga", (byte *) MappedResource.pData, descDepth.Width, descDepth.Height, 32);
				d3d11_Context->Unmap (d3d11_CaptureDepth, 0);
			}

			SAFE_RELEASE (d3d11_CaptureDepth);
		}
	}

	scr_capturedepth = false;
}


//=============================================================================


/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_SetTimeout (float timeout)
{
	scr_timeout = timeout;
}


void SCR_DrawLoading (void)
{
	if (!scr_drawloading) return;

	extern qpic_t *gfx_loading_lmp;

	D3DDraw_SetRect (0, 0, vid.sbarsize.width, vid.sbarsize.height);
	Draw_Pic ((vid.currsize.width - gfx_loading_lmp->width) / 2, (vid.currsize.height - 48 - gfx_loading_lmp->height) / 2, gfx_loading_lmp, 1, true);
}


void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true);

	if (cls.state != ca_connected) return;
	if (cls.signon != SIGNON_CONNECTED) return;

	// redraw with no console and no center text
	Con_ClearNotify ();
	SCR_ClearCenterString ();
	scr_con_current = 0;

	scr_drawloading = true;
	scr_loadmaxscreens = 0;
	d3d_RenderDef.rebuildworld = true;
	SCR_UpdateScreen ();
	scr_drawloading = false;

	Host_DisableForLoading (true);
	scr_disabled_time = CHostTimer::realtime;
	SCR_SetTimeout (SCR_DEFTIMEOUT);
}


/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque (void)
{
	scr_drawloading = false;
	Host_DisableForLoading (false);
	Con_ClearNotify ();
}

//=============================================================================

char *mbpromts[] =
{
	// convert to orange text
	"Y\345\363  N\357",
	"O\313",
	"O\313  C\341\356\343\345\354",
	"R\345\364\362\371  C\341\356\343\345\354",
	NULL
};


char scr_notifytext[2048];
char scr_notifycaption[80];
int scr_notifyflags = 0;
bool scr_modalmessage = false;

void SCR_DrawNotifyString (char *text, char *caption, int flags)
{
	int		y;

	int hunkmark = TempHunk->GetLowMark ();
	char *lines[64] = {NULL};
	int scr_modallines = 0;
	char *textbuf = (char *) TempHunk->FastAlloc (strlen (text) + 1);
	strcpy (textbuf, text);

	lines[0] = textbuf;

	// count the number of lines
	for (int i = 0;; i++)
	{
		// end
		if (textbuf[i] == 0) break;

		// add a line
		if (textbuf[i] == '\n')
		{
			scr_modallines++;

			// this is to catch a \n\0 case
			if (textbuf[i + 1]) lines[scr_modallines] = &textbuf[i + 1];

			textbuf[i] = 0;
		}
	}

	int maxline = 0;

	for (int i = 0;; i++)
	{
		if (!lines[i]) break;
		if (strlen (lines[i]) > maxline) maxline = strlen (lines[i]);
	}

	// caption might be longer...
	if (strlen (caption) > maxline) maxline = strlen (caption);

	// adjust positioning
	y = (vid.currsize.height - ((scr_modallines + 5) * 10)) / 3;

	// background
	Draw_TextBox ((vid.currsize.width - (maxline * 8)) / 2 - 16, y - 12, maxline * 8 + 16, (scr_modallines + 5) * 10 - 5);
	Draw_TextBox ((vid.currsize.width - (maxline * 8)) / 2 - 16, y - 12, maxline * 8 + 16, 15);

	// draw caption
	Draw_String ((vid.currsize.width - (strlen (caption) * 8)) / 2, y, caption);

	y += 20;

	for (int i = 0;; i++)
	{
		if (!lines[i]) break;

		for (int s = 0; s < strlen (lines[i]); s++)
			lines[i][s] += 128;

		Draw_String ((vid.currsize.width - strlen (lines[i]) * 8) / 2, y, lines[i]);
		y += 10;
	}

	// draw prompt
	char *prompt = NULL;

	if (flags == MB_YESNO)
		prompt = mbpromts[0];
	else if (flags == MB_OK)
		prompt = mbpromts[1];
	else if (flags == MB_OKCANCEL)
		prompt = mbpromts[2];
	else if (flags == MB_RETRYCANCEL)
		prompt = mbpromts[3];
	else prompt = mbpromts[1];

	if (prompt) Draw_String ((vid.currsize.width - strlen (prompt) * 8) / 2, y + 5, prompt);

	TempHunk->FreeToLowMark (hunkmark);
}


/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.
==================
*/
bool SCR_ModalMessage (char *text, char *caption, int flags)
{
	// prevent being called recursively
	if (scr_modalmessage) return false;

	Q_strncpy (scr_notifytext, text, 2047);
	Q_strncpy (scr_notifycaption, caption, 79);
	scr_notifyflags = flags;

	// so dma doesn't loop current sound
//	S_ClearBuffer ();

	bool key_accept = false;

	// force a screen update
//	scr_modalmessage = true;
//	d3d_RenderDef.rebuildworld = true;
//	SCR_UpdateScreen ();
//	scr_modalmessage = false;

	do
	{
		S_ClearBuffer ();
		scr_modalmessage = true;
		d3d_RenderDef.rebuildworld = true;
		SCR_UpdateScreen ();
		scr_modalmessage = false;

		key_count = -1;	// wait for a key down and up
		key_lastpress = 0;	// clear last pressed key
		Sys_SendKeyEvents ();

		// this was trying to be too clever...
		//if (key_lastpress == K_ENTER) {key_accept = true; break;}
		//if (key_lastpress == K_ESCAPE) {key_accept = false; break;}

		// allow ESC key to cancel for options that have a cancel
		if (flags == MB_OK)
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
		}
		else if (flags == MB_YESNO)
		{
			if (key_lastpress == 'y' || key_lastpress == 'Y') {key_accept = true; break;}
			if (key_lastpress == 'n' || key_lastpress == 'N') {key_accept = false; break;}
			if (key_lastpress == K_ESCAPE) {key_accept = false; break;}
		}
		else if (flags == MB_OKCANCEL)
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
			if (key_lastpress == 'c' || key_lastpress == 'C') {key_accept = false; break;}
			if (key_lastpress == K_ESCAPE) {key_accept = false; break;}
		}
		else if (flags == MB_RETRYCANCEL)
		{
			if (key_lastpress == 'r' || key_lastpress == 'R') {key_accept = true; break;}
			if (key_lastpress == 'c' || key_lastpress == 'C') {key_accept = false; break;}
			if (key_lastpress == K_ESCAPE) {key_accept = false; break;}
		}
		else
		{
			if (key_lastpress == 'o' || key_lastpress == 'O') {key_accept = true; break;}
		}

		// let the game sleep while waiting for a keypress
		Sleep (1);
#pragma warning (disable: 4127)
		// suppress conditional expression is constant so that we can use this construct
	} while (1);
#pragma warning (1: 4127)

	return key_accept;
}


//=============================================================================


/*
==================
SCR_UpdateScreen

==================
*/
void M_Draw (void);
void HUD_IntermissionOverlay (void);
void HUD_FinaleOverlay (int y);
void SHOWLMP_drawall (void);

void D3DDraw_End2D (void);

extern int r_speedstime;


void SCR_UpdateFPS (void)
{
	static float oldtime = 0;
	static int r_oldframecount = 0;

	float time = CHostTimer::realtime - oldtime;

	if (time < 0)
	{
		oldtime = CHostTimer::realtime;
		r_oldframecount = d3d_RenderDef.presentcount;
		return;
	}

	if (time > 0.25f) // update value every 1/4 second
	{
		d3d_RenderDef.fps = (float) (d3d_RenderDef.presentcount - r_oldframecount) / time;
		oldtime = CHostTimer::realtime;
		r_oldframecount = d3d_RenderDef.presentcount;
	}
}


void SCR_UpdateScreen (void)
{
	// release all temp hunk memory before the screen update begins
	TempHunk->FreeToLowMark (0);

	// update the mouse state
	IN_SetMouseState (vid.isfullscreen);

	if (vid.block_drawing) return;

	if (scr_disabled_for_loading)
	{
		if ((++scr_loadmaxscreens) > 10)
		{
			SCR_EndLoadingPlaque ();
			Host_DisableForLoading (false);
		}

		if (CHostTimer::realtime - scr_disabled_time > scr_timeout)
		{
			SCR_EndLoadingPlaque ();
			Host_DisableForLoading (false);

			if (scr_timeout >= SCR_DEFTIMEOUT) Con_Printf ("load failed.\n");
		}
		else return;
	}

	// not initialized yet
	if (!D3DVid_IsCreated ()) return;
	if (!scr_initialized || !con_initialized) return;

	// begin rendering; get the size of the refresh window and set up for the render
	if (!D3DVid_BeginRendering ())
	{
		Con_DPrintf ("Skipped frame %i\n", d3d_RenderDef.framecount);
		return;
	}

	// double start = Sys_DoubleTime ();

	// determine size of refresh window
	if (vid.RecalcRefdef) SCR_CalcRefdef ();

	SCR_SetUpToDrawConsole ();

	// only update the 3d refresh if we have a map running and a 3d refresh to update
	if (cls.maprunning && cl.worldmodel && cls.signon == SIGNON_CONNECTED)
	{
		CL_PrepEntitiesForRendering ();
		R_RenderView ();
	}

	D3DDraw_Begin2D ();

	if (scr_drawloading)
	{
		SCR_DrawLoading ();
	}
	else if (cl.intermission == 1 && key_dest == key_game && cls.maprunning)
	{
		HUD_IntermissionOverlay ();
	}
	else if (cl.intermission == 2 && key_dest == key_game && cls.maprunning)
	{
		finaley = 16;
		SCR_CheckDrawCenterString ();
		HUD_FinaleOverlay (finaley);
	}
	else if (!scr_drawmapshot)
	{
		SCR_DrawNet ();
		SCR_DrawTurtle ();
		SCR_DrawPause ();

		if (cls.maprunning)
		{
			SCR_CheckDrawCenterString ();
			HUD_DrawHUD ();
			SCR_DrawConsole ();
			SHOWLMP_drawall ();

			if (r_speeds.value)
			{
				D3DDraw_SetRect (0, 0, vid.sbarsize.width, vid.sbarsize.height);
				Draw_String (vid.currsize.width - 100, 20, va ("%5i ms", r_speedstime));
				Draw_String (vid.currsize.width - 100, 30, va ("%5i surf", d3d_RenderDef.brush_polys));
				Draw_String (vid.currsize.width - 100, 40, va ("%5i mdl", d3d_RenderDef.alias_polys));
				Draw_String (vid.currsize.width - 100, 50, va ("%5i dlight", d3d_RenderDef.numdlight));
				Draw_String (vid.currsize.width - 100, 60, va ("%5i draw", d3d_RenderDef.numdrawprim));
			}

			if (scr_showcoords.integer)
			{
				D3DDraw_SetRect (0, 0, vid.sbarsize.width, vid.sbarsize.height);
				Draw_String (10, 10, va ("%0.3f %0.3f %0.3f", r_refdef.vieworigin[0], r_refdef.vieworigin[1], r_refdef.vieworigin[2]));
			}
		}
		else
		{
			SCR_ClearCenterString ();
			SCR_DrawConsole ();
		}

		M_Draw ();
	}

	if (scr_modalmessage)
	{
		// ensure that we have a valid size selected
		D3DDraw_SetRect (0, 0, vid.sbarsize.width, vid.sbarsize.height);
		SCR_DrawNotifyString (scr_notifytext, scr_notifycaption, scr_notifyflags);
	}

	d3d_RenderDef.numdrawprim = 0;
	d3d_RenderDef.numdlight = 0;

	SCR_UpdateFPS ();
	D3DDraw_End2D ();

	if (scr_capturedepth)
	{
		SCR_CaptureDepth_f ();
		scr_capturedepth = false;
	}

	D3DVid_EndRendering ();

	// Con_Printf ("frame in %fms\n", (float) ((Sys_DoubleTime () - start) * 1000.0));

	// take a mapshot on entry to the map
	// unless we're already in mapshot mode, in which case we'll have an infinite loop!!!
	if (r_automapshot.value && d3d_RenderDef.framecount == 5 && !scr_drawmapshot && cls.maprunning)
	{
		// first ensure we have a "maps" directory
		CreateDirectory (va ("%s/maps", com_gamedir), NULL);

		// now take the mapshot; don't overwrite if one is already there
		SCR_Mapshot_f (va ("%s/%s", com_gamedir, cl.worldmodel->name), false, false);
	}

	if (cls.signon == SIGNON_CONNECTED)
	{
		// particle updating has been moved back to draw time to preserve cache friendliness
		CL_DecayLights ();
	}
}


void SCR_DrawSlider (int x, int y, int width, int stage, int maxstage)
{
	// stage goes from 1 to maxstage inclusive
	// width should really be a multiple of 8
	// slider left
	Draw_Character (x, y, 128);

	// slider body
	for (int i = 16; i < width; i += 8)
		Draw_Character (x + i - 8, y, 129);

	// slider right
	Draw_Character (x + width - 8, y, 130);

	// slider position
	x = (int) ((float) (width - 24) * (((100.0f / (float) (maxstage - 1)) * (float) (stage - 1)) / 100.0f)) + x + 8;

	Draw_Character (x, y, 131);
}


void SCR_QuakeIsLoading (int stage, int maxstage)
{
	// pretend we're fullscreen because we definitely want to hide the mouse
	IN_SetMouseState (true);
	SCR_CalcRefdef ();

	if (D3DVid_BeginRendering ())
	{
		int hunkmark = TempHunk->GetLowMark ();

		D3DDraw_Begin2D ();
		D3DDraw_SetRect (0, 0, vid.consize.width, vid.consize.height);

		Draw_ConsoleBackground (100);

		extern qpic_t *gfx_loading_lmp;

		int x = (vid.currsize.width - gfx_loading_lmp->width) / 2;
		int y = (vid.currsize.height - 48 - gfx_loading_lmp->height) / 2;

		Draw_Pic (x, y, gfx_loading_lmp, 1, true);

		SCR_DrawSlider (x + 8, y + gfx_loading_lmp->height + 8, gfx_loading_lmp->width - 16, stage, maxstage);

		D3DDraw_End2D ();
		D3DVid_EndRendering ();

		TempHunk->FreeToLowMark (hunkmark);
	}
}

