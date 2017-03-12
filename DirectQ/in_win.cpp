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
// in_win.c -- windows 95 mouse and joystick code

#include "quakedef.h"
#include "winquake.h"


// mouse variables
cvar_t freelook ("freelook", "1", CVAR_ARCHIVE);
cvar_t m_filter ("m_filter", "0", CVAR_ARCHIVE);

// because when using the standard window class cursor we sometimes come up with the wait cursor instead of the arrow,
// we decide to just manage it ourselves instead
HCURSOR hDefaultCursor = NULL;

extern bool keybind_grab;

void IN_StartupJoystick (void);
void Joy_AdvancedUpdate_f (void);
void CL_BoundViewPitch (float *viewangles);
void ClearAllStates (void);
void IN_StartupXInput (void);

cmd_t joyadvancedupdate ("joyadvancedupdate", Joy_AdvancedUpdate_f);

// raw input specifies up to 5 buttons and we check them all, even on devices with less than 5
const int ri_MouseButtons = 5;

struct ri_buttons_t
{
	int ButtonUp;
	int ButtonDown;
	int StateVal;
};


// raw input mouse state
struct ri_state_t
{
	int NewButtonState;
	int OldButtonState;
	int MouseMoveX;
	int MouseMoveY;
	bool MouseActive;
	ri_buttons_t Buttons[ri_MouseButtons];
};


ri_state_t ri = {
	0, 0, 0, 0, false, {
		{RI_MOUSE_BUTTON_1_UP, RI_MOUSE_BUTTON_1_DOWN, (1 << 0)},
		{RI_MOUSE_BUTTON_2_UP, RI_MOUSE_BUTTON_2_DOWN, (1 << 1)},
		{RI_MOUSE_BUTTON_3_UP, RI_MOUSE_BUTTON_3_DOWN, (1 << 2)},
		{RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_4_DOWN, (1 << 3)},
		{RI_MOUSE_BUTTON_5_UP, RI_MOUSE_BUTTON_5_DOWN, (1 << 4)}
	}
};


/*
========================================================================================================================

						KEYBOARD and MOUSE

========================================================================================================================
*/

/*
===================================================================

KEY MAPPING

moved from gl_vidnt.c
shiftscantokey was unused

===================================================================
*/
byte scantokey[128] = {
	// scancode to quake key table
	// 0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f
	0x00, 0x1b, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x2d, 0x3d, 0x7f, 0x09,		// 0x0
	0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75, 0x69, 0x6f, 0x70, 0x5b, 0x5d, 0x0d, 0x85, 0x61, 0x73,		// 0x1
	0x64, 0x66, 0x67, 0x68, 0x6a, 0x6b, 0x6c, 0x3b, 0x27, 0x60, 0x86, 0x5c, 0x7a, 0x78, 0x63, 0x76,		// 0x2
	0x62, 0x6e, 0x6d, 0x2c, 0x2e, 0x2f, 0x86, 0x2a, 0x84, 0x20, 0x99, 0x87, 0x88, 0x89, 0x8a, 0x8b,		// 0x3
	0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0xff, 0x00, 0x97, 0x80, 0x96, 0x2d, 0x82, 0x35, 0x83, 0x2b, 0x98,		// 0x4
	0x81, 0x95, 0x93, 0x94, 0x00, 0x00, 0x00, 0x91, 0x92, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		// 0x5
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		// 0x6
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00		// 0x7
};



int IN_MapKey (int key)
{
	if (key > 127)
		return 0;

	if (scantokey[key] == 0)
		Con_DPrintf ("key 0x%02x has no translation\n", key);

	return scantokey[key];
}


usercmd_t in_mousecmd = {0, 0, 0};

void IN_MouseMove (usercmd_t *cmd)
{
	if (vid.ActiveApp && !vid.Minimized)
	{
		// for most players these will always be zero
		cmd->forwardmove += in_mousecmd.forwardmove;
		cmd->sidemove += in_mousecmd.sidemove;
		cmd->upmove += in_mousecmd.upmove;
	}

	in_mousecmd.forwardmove = 0;
	in_mousecmd.sidemove = 0;
	in_mousecmd.upmove = 0;
}


void IN_MouseEvent (int ri_NewButtonState)
{
	// always clear even if the mouse is currently inactive because it may have previously been active
	for (int i = 0; i < ri_MouseButtons; i++)
	{
		if ((ri_NewButtonState & (1 << i)) && !(ri.OldButtonState & (1 << i))) Key_Event (K_MOUSE1 + i, true);
		if (!(ri_NewButtonState & (1 << i)) && (ri.OldButtonState & (1 << i))) Key_Event (K_MOUSE1 + i, false);
	}

	ri.OldButtonState = ri_NewButtonState;
}


void IN_ClearMouseState (void)
{
	// flush any pending events and reset everything to 0
	IN_MouseEvent (0);

	ri.NewButtonState = 0;
	ri.MouseMoveX = 0;
	ri.MouseMoveY = 0;
}


void IN_ReadInputMessages (void)
{
	if (!ri.MouseMoveX && !ri.MouseMoveY) return;

	// compensate for different scaling
	float mouse_x = (float) ri.MouseMoveX * sensitivity.value * 1.666f;
	float mouse_y = (float) ri.MouseMoveY * sensitivity.value * 1.666f;

	ri.MouseMoveX = 0;
	ri.MouseMoveY = 0;

	if ((in_strafe.state & 1) || (lookstrafe.value && (freelook.integer || (in_mlook.state & 1))))
		in_mousecmd.sidemove += m_side.value * mouse_x;
	else cl.viewangles[1] -= m_yaw.value * mouse_x;

	if ((freelook.integer || (in_mlook.state & 1)) && !(in_strafe.state & 1))
	{
		cl.viewangles[0] += m_pitch.value * mouse_y;
		CL_BoundViewPitch (cl.viewangles);
	}
	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			in_mousecmd.upmove -= m_forward.value * mouse_y;
		else in_mousecmd.forwardmove -= m_forward.value * mouse_y;
	}
}


void IN_DecodeMouseWheel (int val, int upaction, int downaction)
{
	if (val > 0)
	{
		Key_Event (upaction, true);
		Key_Event (upaction, false);
	}
	else
	{
		Key_Event (downaction, true);
		Key_Event (downaction, false);
	}
}


void IN_ReadRawInput (HRAWINPUT hRawInput)
{
	RAWINPUT ri_Data;
	UINT ri_Size = sizeof (RAWINPUT);

	if (!GetRawInputData (hRawInput, RID_INPUT, &ri_Data, &ri_Size, sizeof (RAWINPUTHEADER))) return;

	if (ri_Data.header.dwType == RIM_TYPEMOUSE)
	{
		// read movement
		ri.MouseMoveX += ri_Data.data.mouse.lLastX;
		ri.MouseMoveY += ri_Data.data.mouse.lLastY;

		// detect wheel
		if (ri_Data.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
			IN_DecodeMouseWheel ((signed int) GET_WHEEL_DELTA_WPARAM(ri_Data.data.mouse.usButtonData), K_MWHEELUP, K_MWHEELDOWN);

		// decode buttons
		for (int i = 0; i < ri_MouseButtons; i++)
		{
			if (ri_Data.data.mouse.usButtonFlags & ri.Buttons[i].ButtonDown) ri.NewButtonState |= ri.Buttons[i].StateVal;
			if (ri_Data.data.mouse.usButtonFlags & ri.Buttons[i].ButtonUp) ri.NewButtonState &= ~ri.Buttons[i].StateVal;
		}

		// run events (even if state is 0 as there may be previous state to be cleared)
		IN_MouseEvent (ri.NewButtonState);
	}
}


void D3DVid_ToggleAltEnter (void);

bool IN_ReadInputMessages (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	extern double last_inputtime;
	double saved = last_inputtime;
	int key = (int) lParam;

	key = (key >> 16) & 255;
	last_inputtime = CHostTimer::realtime;

	switch (Msg)
	{
	case WM_MOUSEWHEEL:
		// in the console or the menu we capture the mousewheel and use it for scrolling
		if (key_dest == key_console)
		{
			IN_DecodeMouseWheel ((signed int) GET_WHEEL_DELTA_WPARAM(wParam), K_PGUP, K_PGDN);
			return true;
		}
		else if (key_dest == key_menu)
		{
			IN_DecodeMouseWheel ((signed int) GET_WHEEL_DELTA_WPARAM(wParam), K_UPARROW, K_DOWNARROW);
			return true;
		}
		else
		{
			last_inputtime = saved;
			return false;
		}

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		// we just discard these messages
		return true;

	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
		Key_Event (IN_MapKey (key), true);
		return true;

	case WM_SYSKEYUP:
		// look for Alt-Enter
		if ((lParam & (1 << 29)) && IN_MapKey (key) == K_ENTER)
		{
			D3DVid_ToggleAltEnter ();
			return true;
		}

		// fall through
	case WM_KEYUP:
		// fire a regular key
		Key_Event (IN_MapKey (key), false);
		return true;

	default:
		last_inputtime = saved;
		return false;
	}
}


/*
===========
Force_CenterView_f
===========
*/
void Force_CenterView_f (void)
{
	cl.viewangles[0] = 0;
}


/*
===========
IN_StartupMouse
===========
*/
void IN_StartupMouse (void)
{
	// set initial mouse state
	ri.NewButtonState = 0;
	ri.OldButtonState = 0;
	ri.MouseMoveX = 0;
	ri.MouseMoveY = 0;
	ri.MouseActive = false;
}


/*
===========
IN_Init
===========
*/
cmd_t fcv_cmd ("force_centerview", Force_CenterView_f);
cmd_t v_centerview_cmd ("centerview", Force_CenterView_f);

void IN_Init (void)
{
	hDefaultCursor = LoadCursor (NULL, IDC_ARROW);

	IN_StartupMouse ();
	IN_StartupJoystick ();
	IN_StartupXInput ();
}


void IN_UnacquireMouse (void)
{
	if (ri.MouseActive)
	{
		IN_ClearMouseState ();

		RAWINPUTDEVICE ri_Mouse = {
			1,
			2,
			RIDEV_REMOVE,
			NULL
		};

		if (!RegisterRawInputDevices (&ri_Mouse, 1, sizeof (RAWINPUTDEVICE)))
			Con_Printf ("Failed to unregister Raw Input mouse device\n");
		else
		{
			ClipCursor (NULL);
			while (ShowCursor (TRUE) < 0);
			SetCursor (hDefaultCursor);
			ri.MouseActive = false;
		}
	}
}


void IN_AcquireMouse (void)
{
	if (!ri.MouseActive)
	{
		IN_ClearMouseState ();
		VIDWin32_GoToNewClientRect ();

		// never acquire the mouse but do update the rest of our stuff
		if (COM_CheckParm ("-nomouse")) return;

		RAWINPUTDEVICE ri_Mouse = {
			1,
			2,
			RIDEV_NOLEGACY,
			vid.Window
		};

		if (!RegisterRawInputDevices (&ri_Mouse, 1, sizeof (RAWINPUTDEVICE)))
			Con_Printf ("Failed to register Raw Input mouse device\n");
		else
		{
			// to do - clip/etc
			SetCursor (NULL);
			ClipCursor (&vid.ClientRect);
			while (ShowCursor (FALSE) >= 0);
			ri.MouseActive = true;
		}
	}
}


/*
===================
IN_SetMouseState

sets the correct mouse state for the current view; if the required state is already set it just does nothing;
called once per frame before beginning to render
===================
*/
void IN_SetMouseState (bool fullscreen)
{
	if (keybind_grab)
	{
		// non-negotiable, always capture and hide the mouse
		IN_AcquireMouse ();
	}
	else if (!vid.ActiveApp || vid.Minimized)
	{
		// give the mouse back to the user
		IN_UnacquireMouse ();
	}
	else if (key_dest == key_game || fullscreen || cl.maxclients > 1)
	{
		// non-negotiable, always capture and hide the mouse
		IN_AcquireMouse ();
	}
	else
	{
		// here we release the mouse back to the OS
		IN_UnacquireMouse ();
	}
}


/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown (void)
{
	SetCursor (NULL);
	CloseHandle (hDefaultCursor);
	IN_UnacquireMouse ();
}

