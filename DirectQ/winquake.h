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

// winquake.h: Win32-specific Quake header file

// in case winquake.h is ever included before quakedef.h anywhere
#include "versions.h"

#ifndef __WINQUAKE_H
#define __WINQUAKE_H

#define WM_MOUSEWHEEL                   0x020A

extern HRESULT hr;

#include <ddraw.h>

void S_ClearSounds (void);

extern bool	WinNT;

extern bool	winsock_lib_initialized;

extern bool	mouseinitialized;

extern HANDLE	hinput, houtput;

void VID_SetDefaultMode (void);

// these are needed in vidnt and sys_win
LRESULT CALLBACK MainWndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

#define D3D_WINDOW_CLASS_NAME "DirectQ Application"

#define WindowStyle (WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_BORDER | WS_DLGFRAME | WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX)
#define ExWindowStyle (WS_EX_WINDOWEDGE)

void VIDWin32_GoToNewClientRect (void);

#endif
