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
// common.c -- misc functions used in client and server

#include "quakedef.h"
#include "unzip.h"

// used for generating md5 hashes
#include <wincrypt.h>

int Q_snprintf (char *buffer, size_t size, const char *format, ...)
{
	va_list args;
	size_t result;

	va_start (args, format);
	result = _vsnprintf (buffer, size, format, args);
	va_end (args);

	if (result < 0 || (size_t) result >= size)
	{
		buffer[size - 1] = '\0';
		return -1;
	}

	return result;
}


size_t Q_strncpy (char *dst, const char *src, size_t siz)
{
	register char *d = dst;
	register const char *s = src;
	register size_t n = siz;

	if (n != 0 && --n != 0)
	{
		do
		{
			if ((*d++ = *s++) == 0)
				break;
		} while (--n != 0);
	}

	if (n == 0)
	{
		if (siz != 0)
			*d = '\0';
		while (*s++)
			;
	}

	return (s - src - 1);
}


static bool listsortascorder = false;

int COM_ListSortFunc (const void *a, const void *b)
{
	if (listsortascorder)
	{
		char *a1 = *((char **) a);
		char *b1 = *((char **) b);
		return strcmp (a1, b1);
	}
	else
	{
		char *a1 = *((char **) b);
		char *b1 = *((char **) a);
		return strcmp (a1, b1);
	}
}

// sort a null terminated string list
void COM_SortStringList (char **stringlist, bool ascending)
{
	int listlen;

	// find the length of the list
	for (listlen = 0;; listlen++)
		if (!stringlist[listlen]) break;

	listsortascorder = ascending;
	qsort (stringlist, listlen, sizeof (char *), COM_ListSortFunc);
}


// MD5 hashes
extern "C" void MD5_Checksum (unsigned char *data, int dataLen, unsigned char *checksum);

void COM_HashData (byte *hash, const void *data, int size)
{
	MD5_Checksum ((unsigned char *) data, size, hash);
}


#define NUM_SAFE_ARGVS  7

static char     *largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static char     *argvdummy = " ";

static char     *safeargvs[NUM_SAFE_ARGVS] =
{"-stdvid", "-nolan", "-nosound", "-nocdaudio", "-nojoy", "-nomouse", "-dibonly"};

cvar_t  registered ("registered", "0");
cvar_t  cmdline ("cmdline", "0", CVAR_SERVER);

int             static_registered = 1;  // only for startup check, then set

bool		msg_suppress_1 = 0;

void COM_InitFilesystem (void);

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT              339
#define PAK0_CRC                32981

char	com_token[1024];
int		com_argc;
char	**com_argv;

#define CMDLINE_LENGTH	256
char	com_cmdline[CMDLINE_LENGTH];

bool		standard_quake = true, rogue = false, hipnotic = false, quoth = false, nehahra = false;


/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter appears, or 0 if not present
================
*/
int COM_CheckParm (char *parm)
{
	int             i;

	for (i = 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;               // NEXTSTEP sometimes clears appkit vars.

		if (!strcmp (parm, com_argv[i]))
			return i;
	}

	return 0;
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
void COM_CheckRegistered (void)
{
	CQuakeFile f;
	unsigned short check[128];

	// allow this in shareware too
	cmdline.Set (com_cmdline);
	static_registered = 0;

	if (!f.Open ("gfx/pop.lmp"))
	{
		Con_SafePrintf ("Playing shareware version.\n");
		return;
	}

	if (!f.ValidateLength (sizeof (check)))
	{
		Con_SafePrintf ("Corrupted pop.lmp file - reverting to shareware.");
		return;
	}

	f.Read (check, sizeof (check));
	f.Close ();

	// generate a hash of the pop.lmp data
	byte pophash[16];
	byte realpop[] = {11, 131, 239, 192, 65, 30, 123, 93, 203, 147, 122, 30, 66, 173, 55, 227};
	COM_HashData (pophash, check, sizeof (check));

	if (!COM_CheckHash (pophash, realpop))
	{
		Con_SafePrintf ("Corrupted pop.lmp file - reverting to shareware.");
		return;
	}

	registered.Set (1);
	static_registered = 1;
	Con_SafePrintf ("Playing registered version.\n");
}


void COM_Path_f (void);


/*
================
COM_InitArgv
================
*/
void COM_InitArgv (int argc, char **argv)
{
	bool        safe;
	int             i, j, n;

	// reconstitute the command line for the cmdline externally visible cvar
	n = 0;

	for (j = 0; (j < MAX_NUM_ARGVS) && (j < argc); j++)
	{
		i = 0;

		while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i])
			com_cmdline[n++] = argv[j][i++];

		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else break;
	}

	com_cmdline[n] = 0;

	safe = false;

	for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++)
	{
		largv[com_argc] = argv[com_argc];

		if (!strcmp ("-safe", argv[com_argc]))
			safe = true;
	}

	if (safe)
	{
		// force all the safe-mode switches. Note that we reserved extra space in
		// case we need to add these, so we don't need an overflow check
		for (i = 0; i < NUM_SAFE_ARGVS; i++)
		{
			largv[com_argc] = safeargvs[i];
			com_argc++;
		}
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;
}


/*
================
COM_Init
================
*/
cmd_t COM_Path_f_Cmd ("path", COM_Path_f);

void COM_Init (char *basedir)
{
	COM_InitFilesystem ();
	COM_CheckRegistered ();
}


/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
============
*/
char *va (char *format, ...)
{
	// because we recycle the temp hunk each frame we can just pull memory as required from it without needing
	// to do any buffer cycling or other crap like that.  this may become a problem if we get into 10s of 1000s
	// of va calls per-frame, but if that happens then we've got bigger problems.
	va_list argptr;
	char *string = (char *) TempHunk->FastAlloc (1024);

	// make the buffer safe
	va_start (argptr, format);
	_vsnprintf (string, 1023, format, argptr);
	va_end (argptr);

	return string;
}


bool COM_FindExtension (char *filename, char *ext)
{
	int fl = strlen (filename);
	int el = strlen (ext);

	if (el >= fl) return false;

	for (int i = 0;; i++)
	{
		if (!filename[i]) break;
		if (!_stricmp (&filename[i], ext)) return true;
	}

	return false;
}


// case-insensitive strstr replacement
bool COM_StringContains (char *str1, char *str2)
{
	// sanity check args
	if (!str1) return false;
	if (!str2) return false;

	// OK, perf-wise it sucks, but - hey! - it doesn't really matter for the circumstances it's used in.
	for (int i = 0;; i++)
	{
		if (!str1[i]) break;
		if (!_strnicmp (&str1[i], str2, strlen (str2))) return true;
	}

	// not found
	return false;
}


char *COM_ShiftTextColor (char *str)
{
	for (int i = 0;; i++)
	{
		if (!str[i]) break;

		str[i] = (str[i] + 128) & 255;
	}

	return str;
}

