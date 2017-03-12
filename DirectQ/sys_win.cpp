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
// sys_win.c -- Win32 system interface code

#include "quakedef.h"
#include "winquake.h"
#include "resource.h"
#include <shlobj.h>
#include <dwmapi.h>

#pragma comment (lib, "dwmapi.lib")


QUAKESYSTEM Sys;

HICON hAppIcon = NULL;

SYSTEM_INFO SysInfo;
void AllowAccessibilityShortcutKeys (bool bAllowKeys);


int QUAKESYSTEM::LoadResourceData (int resourceid, void **resbuf)
{
	// per MSDN, UnlockResource is obsolete and does nothing any more.  There is
	// no way to free the memory used by a resource after you're finished with it.
	// If you ask me this is kinda fucked, but what do I know?  We'll just leak it.
	if (resbuf)
	{
		HRSRC hResInfo = FindResource (NULL, MAKEINTRESOURCE (resourceid), RT_RCDATA);

		if (hResInfo)
		{
			HGLOBAL hResData = LoadResource (NULL, hResInfo);

			if (hResData)
			{
				resbuf[0] = (byte *) LockResource (hResData);
				return SizeofResource (NULL, hResInfo);
			}
		}
	}

	Sys_Error ("QUAKESYSTEM::LoadResourceData : failed to load resource id %i\n", resourceid);
	return 0;
}


// we need this a lot so define it just once
HRESULT hr = S_OK;

#define PAUSE_SLEEP		50				// sleep time on pause or minimization
#define NOT_FOCUS_SLEEP	20				// sleep time when not focus

bool	WinNT = false;

static HANDLE	hFile;
static HANDLE	heventParent;
static HANDLE	heventChild;


bool Sys_FileExists (char *path)
{
	std::ifstream f (path, std::ios::in | std::ios::binary);

	if (f.is_open ())
	{
		f.close ();
		return true;
	}
	else return false;
}


/*
==================
Sys_mkdir

A better Sys_mkdir.

Uses the Windows API instead of direct.h for better compatibility.
Doesn't need com_gamedir included in the path to make.
Will make all elements of a deeply nested path.
==================
*/
void Sys_mkdir (char *path)
{
	char fullpath[256];

	// if a full absolute path is given we just copy it out, otherwise we build from the gamedir
	if (path[1] == ':' || (path[0] == '\\' && path[1] == '\\'))
		Q_strncpy (fullpath, path, 255);
	else Q_snprintf (fullpath, 255, "%s/%s", com_gamedir, path);

	for (int i = 0;; i++)
	{
		if (!fullpath[i]) break;

		if (fullpath[i] == '/' || fullpath[i] == '\\')
		{
			// correct seperator
			fullpath[i] = '\\';

			if (i > 3)
			{
				// make all elements of the path
				fullpath[i] = 0;
				CreateDirectory (fullpath, NULL);
				fullpath[i] = '\\';
			}
		}
	}

	// final path
	CreateDirectory (fullpath, NULL);
}


void Sys_CacheLineSize (void)
{
	// Quake's default memory allocator assumes a cache-line of 16 so that's what we'll use too
	Sys.CacheLineSize = 16;

	SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *) scratchbuf;
	DWORD buffer_size = 0;

	GetLogicalProcessorInformation (NULL, &buffer_size);
	GetLogicalProcessorInformation (&buffer[0], &buffer_size);

	for (int i = 0; i != buffer_size / sizeof (SYSTEM_LOGICAL_PROCESSOR_INFORMATION); i++)
	{
		if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1)
		{
			Sys.CacheLineSize = buffer[i].Cache.LineSize;
			break;
		}
	}

	// mask for rounding up to a power
	Sys.CacheLineMask = Sys.CacheLineSize - 1;
}


void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];
	static int	in_sys_error0 = 0;
	static int	in_sys_error1 = 0;
	static int	in_sys_error2 = 0;
	static int	in_sys_error3 = 0;

	if (!in_sys_error3)
	{
		in_sys_error3 = 1;
	}

	va_start (argptr, error);
	_vsnprintf (text, 1024, error, argptr);
	va_end (argptr);

	QC_DebugOutput ("Sys_Error: %s", text);

	// switch to windowed so the message box is visible, unless we already tried that and failed
	if (!in_sys_error0)
	{
		in_sys_error0 = 1;
		MessageBox (vid.Window, text, "Quake Error", MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
	}
	else MessageBox (vid.Window, text, "Double Quake Error", MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);

	if (!in_sys_error1)
	{
		in_sys_error1 = 1;
		Host_Shutdown ();
	}

	// shut down QHOST hooks if necessary
	if (!in_sys_error2)
	{
		in_sys_error2 = 1;
	}

	exit (666);
}


// fixme - this now hogs an entire core.  we should use an event in Sys_DoubleTime to signal the thread instead...
double sys_doubletime = 0;
volatile bool sys_quittimer = false;
HANDLE hTimerThread = INVALID_HANDLE_VALUE;
HANDLE hTimerBeginEvent = INVALID_HANDLE_VALUE;
HANDLE hTimerEndEvent = INVALID_HANDLE_VALUE;

void Sys_Quit (int ExitCode)
{
	// exit the timer thread cleanly
	if (hTimerThread != INVALID_HANDLE_VALUE)
	{
		sys_quittimer = true;
		Sys_DoubleTime ();
	}

	Host_Shutdown ();
	AllowAccessibilityShortcutKeys (true);
	exit (ExitCode);
}


double Sys_GetDoubleTime (void)
{
	static __int64 qpcaccum;
	static __int64 qpcprev;
	static __int64 qpcfreq;
	static bool firstcall = true;

	__int64 qpccurr;

	if (firstcall)
	{
		QueryPerformanceFrequency ((LARGE_INTEGER *) &qpcfreq);
		QueryPerformanceCounter ((LARGE_INTEGER *) &qpcprev);
		firstcall = false;
		qpcaccum = 0;
		return 0;
	}

	QueryPerformanceCounter ((LARGE_INTEGER *) &qpccurr);

	if (qpccurr < qpcprev)
		qpcaccum += 0;
	else qpcaccum += (qpccurr - qpcprev);

	qpcprev = qpccurr;

	return (double) qpcaccum / (double) qpcfreq;
}


DWORD WINAPI Sys_TimerThread (LPVOID lpThreadParameter)
{
	// get an initial time
	sys_doubletime = Sys_GetDoubleTime ();

	for (;;)
	{
		// now wait until the engine requests a time
		WaitForSingleObject (hTimerBeginEvent, 1);

		// exit the thread cleanly if needed
		if (sys_quittimer) break;

		// and get the time
		sys_doubletime = Sys_GetDoubleTime ();

		// and signal to the calling thread that the time has been gotten
		ResetEvent (hTimerBeginEvent);
		SetEvent (hTimerEndEvent);
	}

	return 0;
}


double Sys_DoubleTime (void)
{
	if (hTimerThread == INVALID_HANDLE_VALUE)
	{
		// single-threaded timer
		return Sys_GetDoubleTime ();
	}
	else
	{
		// multi-threaded timer
		ResetEvent (hTimerEndEvent);
		SetEvent (hTimerBeginEvent);

		// make sure that we've got a time update here
		WaitForSingleObject (hTimerEndEvent, 1);

		return sys_doubletime;
	}
}


void Sys_SendKeyEvents (void)
{
	MSG msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
	{
		// http://msdn.microsoft.com/en-us/library/windows/desktop/ms644936%28v=vs.85%29.aspx
		int ret = (int) GetMessage (&msg, NULL, 0, 0);

		if (!ret)
			Sys_Quit (msg.wParam);
		else if (ret == -1)
			Sys_Error ("Sys_SendKeyEvents : GetMessage failed");
		else
		{
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
}


/*
==================
WinMain
==================
*/
char		*argv[MAX_NUM_ARGVS];
static char	*empty_string = "";


STICKYKEYS StartupStickyKeys = {sizeof (STICKYKEYS), 0};
TOGGLEKEYS StartupToggleKeys = {sizeof (TOGGLEKEYS), 0};
FILTERKEYS StartupFilterKeys = {sizeof (FILTERKEYS), 0};


void AllowAccessibilityShortcutKeys (bool bAllowKeys)
{
	if (bAllowKeys)
	{
		// Restore StickyKeys/etc to original state
		// (note that this function is called "allow", not "enable"; if they were previously
		// disabled it will put them back that way too, it doesn't force them to be enabled.)
		SystemParametersInfo (SPI_SETSTICKYKEYS, sizeof (STICKYKEYS), &StartupStickyKeys, 0);
		SystemParametersInfo (SPI_SETTOGGLEKEYS, sizeof (TOGGLEKEYS), &StartupToggleKeys, 0);
		SystemParametersInfo (SPI_SETFILTERKEYS, sizeof (FILTERKEYS), &StartupFilterKeys, 0);
	}
	else
	{
		// Disable StickyKeys/etc shortcuts but if the accessibility feature is on,
		// then leave the settings alone as its probably being usefully used
		STICKYKEYS skOff = StartupStickyKeys;

		if ((skOff.dwFlags & SKF_STICKYKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			skOff.dwFlags &= ~SKF_HOTKEYACTIVE;
			skOff.dwFlags &= ~SKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETSTICKYKEYS, sizeof (STICKYKEYS), &skOff, 0);
		}

		TOGGLEKEYS tkOff = StartupToggleKeys;

		if ((tkOff.dwFlags & TKF_TOGGLEKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			tkOff.dwFlags &= ~TKF_HOTKEYACTIVE;
			tkOff.dwFlags &= ~TKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETTOGGLEKEYS, sizeof (TOGGLEKEYS), &tkOff, 0);
		}

		FILTERKEYS fkOff = StartupFilterKeys;

		if ((fkOff.dwFlags & FKF_FILTERKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			fkOff.dwFlags &= ~FKF_HOTKEYACTIVE;
			fkOff.dwFlags &= ~FKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETFILTERKEYS, sizeof (FILTERKEYS), &fkOff, 0);
		}
	}
}


void Sys_NotifyIcon (DWORD message, char *title, char *text)
{
	NOTIFYICONDATA Q_nid;
	extern char dequake[];

	memset (&Q_nid, 0, sizeof (NOTIFYICONDATA));

	Q_nid.cbSize = sizeof (NOTIFYICONDATA);
	Q_nid.hWnd = vid.Window;
	Q_nid.uID = 666;
	Q_nid.uFlags = NIF_ICON;
	Q_nid.hIcon = hAppIcon;

	if (title && text)
	{
		Q_nid.uFlags |= NIF_INFO;
		Q_nid.dwInfoFlags = NIIF_INFO;
		Q_nid.uTimeout = 5000;
		Q_strncpy (Q_nid.szInfoTitle, title, sizeof (Q_nid.szInfoTitle) - 1);

		for (int i = 0; i < sizeof (Q_nid.szInfo) - 1; i++)
		{
			if (!text[i]) break;

			Q_nid.szInfo[i] = dequake[text[i] & 127];
			Q_nid.szInfo[i + 1] = 0;
		}
	}

	Shell_NotifyIcon (message, &Q_nid);
}


int MapKey (int key);
void ClearAllStates (void);
LONG CDAudio_MessageHandler (WPARAM wParam, LPARAM lParam);

void CDAudio_Pause (void);
void CDAudio_Resume (void);

void D3DVid_ToggleFullscreen (bool toggleon);

void AppActivate (BOOL fActive, BOOL minimize, BOOL notify)
{
	vid.ActiveApp = fActive;
	vid.Minimized = minimize;

	if (fActive)
	{
		vid.block_drawing = false;

		// do this first as the api calls might affect the other stuff
		if (vid.isfullscreen)
		{
			if (vid.canalttab && vid.wassuspended)
			{
				vid.wassuspended = false;

				// ensure that the window is shown at at the top of the z order
				ShowWindow (vid.Window, SW_SHOWNORMAL);
				SetForegroundWindow (vid.Window);
			}
		}

		D3DVid_ToggleFullscreen (true);
		ClearAllStates ();
		IN_SetMouseState (vid.isfullscreen);

		// restore everything else
		CDAudio_Resume ();
		AllowAccessibilityShortcutKeys (false);

		// needed to reestablish the correct viewports
		vid.RecalcRefdef = true;

		Sys_NotifyIcon (NIM_DELETE);
	}
	else
	{
		D3DVid_ToggleFullscreen (false);
		ClearAllStates ();
		IN_SetMouseState (vid.isfullscreen);
		CDAudio_Pause ();
		S_ClearBuffer ();
		vid.block_drawing = true;
		AllowAccessibilityShortcutKeys (true);

		if (vid.isfullscreen)
		{
			if (vid.canalttab)
			{
				vid.wassuspended = true;
			}
		}

		if (notify) Sys_NotifyIcon (NIM_ADD);
	}
}


cvar_t sys_sleeptime ("sys_sleeptime", 0.0f, CVAR_ARCHIVE);
cvar_t sys_sleep ("sys_sleep", 0.0f, CVAR_ARCHIVE);
double last_inputtime = 0;

bool IN_ReadInputMessages (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
void IN_ReadRawInput (HRAWINPUT hRawInput);

void D3DVid_HandleResize (void);
void D3DVid_UpdateOutput (void);

/* main window procedure */
LRESULT WINAPI MainWndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int fActive, fMinimized;

	// check for input messages
	if (IN_ReadInputMessages (hWnd, Msg, wParam, lParam)) return 0;

	// to do - moved to a different monitor???
	switch (Msg)
	{
	case WM_DISPLAYCHANGE:
		// something else changed the display mode
		return 0;

	case WM_INPUT_DEVICE_CHANGE:
		// a mouse has been added or removed
		return 0;

	case WM_POWERBROADCAST:
		// a power management event has occurred
		return 0;

	case WM_THEMECHANGED:
		// the windows theme has changed
		return 0;

	case WM_INPUT:
		// http://msdn.microsoft.com/en-us/library/windows/desktop/ms645590%28v=vs.85%29.aspx
		if (wParam == RIM_INPUT)
			IN_ReadRawInput ((HRAWINPUT) lParam);

		DefWindowProc (hWnd, Msg, wParam, lParam);
		return 0;

	case WM_MOVE:
		D3DVid_UpdateOutput ();
		VIDWin32_GoToNewClientRect ();
		return 0;

	case WM_SIZING:
		vid.block_drawing = true;
		return 0;

	case WM_SIZE:
		D3DVid_UpdateOutput ();

		if (wParam == SIZE_RESTORED)
		{
			D3DVid_HandleResize ();
			return 0;
		}
		else break;

	case WM_CREATE:
		VIDWin32_GoToNewClientRect ();
		return 0;

	case WM_ERASEBKGND:
		// the first time through here we want to erase the background as it's the black background from startup
		// subsequent times we discard this message
		if (vid.initialized)
		{
			// treachery!!! see your MSDN!
			return 1;
		}
		else
		{
			vid.initialized = true;
			break;
		}

	case WM_SYSCHAR: return 0;

	case WM_SYSCOMMAND:
		switch (wParam & ~0x0F)
		{
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
			// prevent from happening
			return 0;

		default:
			return DefWindowProc (hWnd, Msg, wParam, lParam);
		}

	case WM_CLOSE:
		if (MessageBox (vid.Window, "Are you sure you want to quit?", "Confirm Exit", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
			Sys_Quit (0);

		return 0;

	case WM_ACTIVATE:
		fActive = LOWORD (wParam);
		fMinimized = (BOOL) HIWORD (wParam);
		AppActivate (!(fActive == WA_INACTIVE), fMinimized, TRUE);

		// fix the leftover Alt from any Alt-Tab or the like that switched us away
		ClearAllStates ();

		return 0;

	case WM_DESTROY:
		PostQuitMessage (0);
		return 0;

	case MM_MCINOTIFY:
		return CDAudio_MessageHandler (wParam, lParam);

	default:
		break;
	}

	// pass all unhandled messages to DefWindowProc
	return DefWindowProc (hWnd, Msg, wParam, lParam);
}


void Host_Frame (void);
void GetCrashReason (LPEXCEPTION_POINTERS ep);

const char *GetExceptionCodeInfo (UINT code)
{
	switch (code)
	{
	case EXCEPTION_ACCESS_VIOLATION: return "The thread tried to read from or write to a virtual address for which it does not have the appropriate access.";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.";
	case EXCEPTION_BREAKPOINT: return "A breakpoint was encountered.";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on.";
	case EXCEPTION_FLT_DENORMAL_OPERAND: return "One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "The thread tried to divide a floating-point value by a floating-point divisor of zero.";
	case EXCEPTION_FLT_INEXACT_RESULT: return "The result of a floating-point operation cannot be represented exactly as a decimal fraction.";
	case EXCEPTION_FLT_INVALID_OPERATION: return "This exception represents any floating-point exception not included in this list.";
	case EXCEPTION_FLT_OVERFLOW: return "The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.";
	case EXCEPTION_FLT_STACK_CHECK: return "The stack overflowed or underflowed as the result of a floating-point operation.";
	case EXCEPTION_FLT_UNDERFLOW: return "The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return "The thread tried to execute an invalid instruction.";
	case EXCEPTION_IN_PAGE_ERROR: return "The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return "The thread tried to divide an integer value by an integer divisor of zero.";
	case EXCEPTION_INT_OVERFLOW: return "The result of an integer operation caused a carry out of the most significant bit of the result.";
	case EXCEPTION_INVALID_DISPOSITION: return "An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "The thread tried to continue execution after a noncontinuable exception occurred.";
	case EXCEPTION_PRIV_INSTRUCTION: return "The thread tried to execute an instruction whose operation is not allowed in the current machine mode.";
	case EXCEPTION_SINGLE_STEP: return "A trace trap or other single-instruction mechanism signaled that one instruction has been executed.";
	case EXCEPTION_STACK_OVERFLOW: return "The thread used up its stack.";
	default: return "An unknown exception occurred";
	}
}


// fixme - run shutdown through here (or else consolidate the restoration stuff in a separate function)
LONG WINAPI TildeDirectQ (LPEXCEPTION_POINTERS toast)
{
	MessageBox (
		NULL,
		GetExceptionCodeInfo (toast->ExceptionRecord->ExceptionCode),
		"An error has occurred",
		MB_OK | MB_ICONSTOP
	);

	// down she goes
	return EXCEPTION_EXECUTE_HANDLER;
}


bool CheckKnownContent (char *mask);

bool IsQuakeDir (char *path)
{
	char *basedir = host_parms.basedir;

	// check for known files that indicate a gamedir
	if (CheckKnownContent (va ("%s/id1/pak0.pak", path))) return true;
	if (CheckKnownContent (va ("%s/id1/config.cfg", path))) return true;
	if (CheckKnownContent (va ("%s/id1/autoexec.cfg", path))) return true;
	if (CheckKnownContent (va ("%s/id1/progs.dat", path))) return true;
	if (CheckKnownContent (va ("%s/id1/gfx.wad", path))) return true;

	// some gamedirs just have maps or models, or may have weirdly named paks
	if (CheckKnownContent (va ("%s/id1/maps/*.bsp", basedir, path))) return true;
	if (CheckKnownContent (va ("%s/id1/progs/*.mdl", basedir, path))) return true;
	if (CheckKnownContent (va ("%s/id1/*.pak", basedir, path))) return true;
	if (CheckKnownContent (va ("%s/id1/*.pk3", basedir, path))) return true;

	// nope
	return false;
}


void Sys_ValidateDirectX (int first, int last, char *base)
{
	for (int i = first; i <= last; i++)
	{
		char d3ddllname[64];
		HMODULE d3ddll = NULL;

		sprintf (d3ddllname, "%s%i.dll", base, i);

		if ((d3ddll = LoadLibrary (d3ddllname)) != NULL)
			FreeLibrary (d3ddll);
		else Sys_Error ("Incomplete DirectX installation - could not load \"%s\"", d3ddllname);
	}
}


DWORD NumberOfSetBits (DWORD x)
{
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    x = (x + (x >> 4)) & 0x0F0F0F0F;
    x = x + (x >> 8);
    x = x + (x >> 16);

    return x & 0x0000003F;
}


int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	InitCommonControls ();

	// clear the video definition
	memset (&vid, 0, sizeof (viddef_t));

	DWORD dwProcessMask = 0;
	DWORD dwSystemMask = 0;

	// let's mask the process correctly for multithreading
	GetProcessAffinityMask (GetCurrentProcess (), &dwProcessMask, &dwSystemMask);
	SetProcessAffinityMask (GetCurrentProcess (), dwSystemMask);

	// count the number of bits in the mask instead to ensure that we're really on a multiple CPU system
	if (NumberOfSetBits (dwSystemMask) > 1)
	{
		// run our timer on a dedicated thread so that QPC will always be consistent
		hTimerThread = CreateThread (NULL, 0, Sys_TimerThread, NULL, CREATE_SUSPENDED, NULL);
		hTimerBeginEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
		hTimerEndEvent = CreateEvent (NULL, TRUE, FALSE, NULL);

		// pick a CPU to run it on (this loop is expected to always exit, and we deliberately don't set an upper limit for to cover
		// the day when we have 32-core systems and the user forces an affinity of cores 28 and 31, or some crap like that
		for (int i = 0; ; i++)
		{
			if (dwSystemMask & (1 << i))
			{
				SetThreadAffinityMask (hTimerThread, (1 << i));
				break;
			}
		}

		// and now start it up and fire the event so that we'll have a valid first call
		SetThreadPriority (hTimerThread, THREAD_PRIORITY_NORMAL);
		ResumeThread (hTimerThread);
		Sys_DoubleTime ();
	}

	// get COM support up-front
	CoInitialize (NULL);

	// init memory pools
	// these need to be up as early as possible as other things in the startup use them
	// moved to classinit.cpp to ensure that they definitely come up before anything else
	Sys_CacheLineSize ();
	// Heap_Init ();

	// this is to protect us against people who just download DLLs from random sites instead of using the proper installer
	Sys_ValidateDirectX (24, 43, "d3dx9_");
	Sys_ValidateDirectX (33, 43, "D3DCompiler_");
	Sys_ValidateDirectX (33, 43, "d3dx10_");
	Sys_ValidateDirectX (42, 43, "d3dx11_");

	char cwd[MAX_PATH];

	// attempt to get the path that the executable is in
	// if it doesn't work we just don't do it
	// this replaces the previous scan-based method and i no longer support just putting the DirectQ exe anywhere
	if (GetModuleFileName (NULL, cwd, MAX_PATH - 1))
	{
		for (int i = strlen (cwd); i; i--)
		{
			if (cwd[i] == '/' || cwd[i] == '\\')
			{
				if (!_stricmp (&cwd[i + 1], "glquake.exe"))
				{
					MessageBox (
						NULL,
						"Your OS and video driver may have compatibility fixes for \"GLQuake.exe\". "
						"These are not needed and may cause trouble for DirectQ. "
						"You should not rename DirectQ to \"GLQuake.exe\".",
						"Warning",
						MB_OK | MB_SETFOREGROUND | MB_ICONEXCLAMATION
					);
				}

				cwd[i] = 0;
				break;
			}
		}

		// attempt to set that path as the current working directory
		SetCurrentDirectory (cwd);
	}

	if (!GetCurrentDirectory (sizeof (cwd), cwd))
		Sys_Error ("Couldn't determine current directory");

	// validate that Quake actually exists in this directory and display something more meaningful than the gfx.wad message
	// done after heap_init as it uses va
	if (!IsQuakeDir (cwd))
	{
		char *msg = (char *) scratchbuf;

		sprintf (msg, "Could not find Quake at\n%s\n\nPlease make sure that you have DirectQ.exe\nin the folder containing your Quake game files", cwd);
		MessageBox (NULL, msg, "Could not find Quake", MB_OK | MB_ICONSTOP);
		return -1;
	}

	hAppIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_APPICON));

	// this is useless for error diagnosis but at least restores gamma on a crash
	SetUnhandledExceptionFilter (TildeDirectQ);

	// in case we ever need it for anything...
	GetSystemInfo (&SysInfo);

	OSVERSIONINFO vinfo = {sizeof (vinfo)};

	if (!GetVersionEx (&vinfo))
	{
		// if we couldn't get it we pop the warning but still let it run
		vinfo.dwMajorVersion = 0;
		vinfo.dwPlatformId = 0;
	}

	// we officially support v6 and above of Windows
	if (vinfo.dwMajorVersion < 6)
	{
		int mret = MessageBox
		(
			NULL,
			"!!! UNSUPPORTED !!!\n\nThis software may run on your Operating System\nbut is NOT officially supported.\n\nCertain pre-requisites are needed.\nNow might be a good time to read the readme.\n\nClick OK if you are sure you want to continue...",
			"Warning",
			MB_OKCANCEL | MB_ICONWARNING
		);

		if (mret == IDCANCEL) return 666;
	}

	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
		WinNT = true;
	else WinNT = false;

	quakeparms_t parms;

	parms.basedir = cwd;
	parms.cachedir = NULL;

	parms.argc = 1;
	argv[0] = empty_string;

	// parse the command-line into args
	while (*lpCmdLine && (parms.argc < MAX_NUM_ARGVS))
	{
		while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
			lpCmdLine++;

		if (*lpCmdLine)
		{
			argv[parms.argc] = lpCmdLine;
			parms.argc++;

			while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
				lpCmdLine++;

			if (*lpCmdLine)
			{
				*lpCmdLine = 0;
				lpCmdLine++;
			}
		}
	}

	parms.argv = argv;

	COM_InitArgv (parms.argc, parms.argv);

	parms.argc = com_argc;
	parms.argv = com_argv;

	// Save the current sticky/toggle/filter key settings so they can be restored later
	SystemParametersInfo (SPI_GETSTICKYKEYS, sizeof (STICKYKEYS), &StartupStickyKeys, 0);
	SystemParametersInfo (SPI_GETTOGGLEKEYS, sizeof (TOGGLEKEYS), &StartupToggleKeys, 0);
	SystemParametersInfo (SPI_GETFILTERKEYS, sizeof (FILTERKEYS), &StartupFilterKeys, 0);

	// Disable when full screen
	AllowAccessibilityShortcutKeys (false);

	// force an initial refdef calculation
	vid.RecalcRefdef = true;

	Host_Init (&parms);

	for (;;)
	{
		// note - a normal frame needs to be run even if paused otherwise we'll never be able to unpause!!!
		if (cl.paused)
			Sleep (PAUSE_SLEEP);
		else if (!vid.ActiveApp || vid.Minimized || vid.block_drawing)
			Sleep (NOT_FOCUS_SLEEP);
		else if (sys_sleeptime.value > 0 && !cls.demoplayback)
		{
			// start sleeping if we go idle (unless we're in a demo in which case we take the full CPU)
			if (CHostTimer::realtime > last_inputtime + (sys_sleeptime.value * 8))
				Sleep (10);
			else if (CHostTimer::realtime > last_inputtime + (sys_sleeptime.value * 4))
				Sleep (5);
			else if (CHostTimer::realtime > last_inputtime + (sys_sleeptime.value * 2))
				Sleep (2);
			else if (CHostTimer::realtime > last_inputtime + sys_sleeptime.value)
				Sleep (1);
		}

		Host_Frame ();

		// sleep if desired (mainly for CPU saving on mobile devices/etc
		if (sys_sleep.integer > 0 && !cls.timedemo)
			Sleep (sys_sleep.integer);
		else YieldProcessor ();
	}

	// never reached
	return 0;
}

