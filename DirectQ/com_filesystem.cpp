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

#include "quakedef.h"
#include "unzip.h"
#include "modelgen.h"
#include <shlwapi.h>
#pragma comment (lib, "shlwapi.lib")


int COM_ListSortFunc (const void *a, const void *b);

void Host_WriteConfiguration (void);
void COM_UnloadGameObjects (void);
void COM_LoadGameObjects (void);
void COM_LoadGame (char *gamename);

/*
============
COM_SkipPath
============
*/
char *COM_SkipPath (char *pathname)
{
	char    *last;

	last = pathname;

	while (*pathname)
	{
		if (*pathname == '/')
			last = pathname + 1;

		pathname++;
	}

	return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (char *in, char *out)
{
	while (*in && *in != '.')
		*out++ = *in++;

	*out = 0;
}

/*
============
COM_FileExtension
============
*/
char *COM_FileExtension (char *in)
{
	static char exten[8];
	int             i;

	while (*in && *in != '.')
		in++;

	if (!*in)
		return "";

	in++;

	for (i = 0; i < 7 && *in; i++, in++)
		exten[i] = *in;

	exten[i] = 0;
	return exten;
}

/*
============
COM_FileBase
============
*/
void COM_FileBase (char *in, char *out)
{
	char *s, *s2;

	s = in + strlen (in) - 1;

	while (s != in && *s != '.')
		s--;

	for (s2 = s; s2 != in && *s2 && *s2 != '/'; s2--);

	if (s - s2 < 2)
		strcpy (out, "?model?");
	else
	{
		s--;
		Q_strncpy (out, s2 + 1, s - s2);
		out[s-s2] = 0;
	}
}


/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension (char *path, char *extension)
{
	char    *src;
	
	// if path doesn't have a .EXT, append extension
	// (extension should include the .)
	src = path + strlen (path) - 1;

	while (*src != '/' && src != path)
	{
		if (*src == '.')
			return;                 // it has an extension

		src--;
	}

	strcat (path, extension);
}


/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

int		com_fileoffset;
bool	com_fileinpack = false;

char    com_gamedir[1025];
char	com_gamename[1025];

searchpath_t    *com_searchpaths = NULL;

void COM_ShutdownFileSystem (void)
{
	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		if (!search->pack) continue;

		if (search->pack->mmhandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle (search->pack->mmhandle);
			search->pack->mmhandle = INVALID_HANDLE_VALUE;
		}

		if (search->pack->fhandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle (search->pack->fhandle);
			search->pack->fhandle = INVALID_HANDLE_VALUE;
		}
	}

	com_searchpaths = NULL;
}


/*
============
COM_Path_f

============
*/
void COM_Path_f (void)
{
	Con_Printf ("Current search path:\n");

	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
			Con_Printf ("%s (%i files)\n", search->pack->filename, search->pack->numfiles);
		else Con_Printf ("%s\n", search->filename);
	}
}


/*
============
COM_CreatePath

Only used for CopyFile
============
*/
void COM_CreatePath (char *path)
{
	for (char *ofs = path + 1; *ofs; ofs++)
	{
		if (*ofs == '/')
		{
			// create the directory
			*ofs = 0;
			Sys_mkdir (path);
			*ofs = '/';
		}
	}
}


bool SortCompare (char *left, char *right)
{
	if (_stricmp (left, right) < 0)
		return true;
	else return false;
}


bool CheckExists (char **fl, char *mapname)
{
	for (int i = 0;; i++)
	{
		// end of list
		if (!fl[i]) return false;
		if (!_stricmp (fl[i], mapname)) return true;
	}

	// never reached
}


int COM_BuildContentList (char ***FileList, char *basedir, char *filetype, int flags)
{
	char **fl = FileList[0];
	int len = 0;

	if (!fl)
	{
		// we never know how much we need, so alloc enough for 256k items
		// at this stage they're only pointers so we can afford to do this.  if it becomes a problem
		// we might make a linked list then copy from that into an array and do it all in the Zone.
		FileList[0] = (char **) scratchbuf;

		// need to reset the pointer as it will have changed (fl is no longer NULL)
		fl = FileList[0];
		fl[0] = NULL;
	}
	else
	{
		// appending to a list so find the current length and build from there
		for (int i = 0;; i++)
		{
			if (!fl[i]) break;

			len++;
		}
	}

	int dirlen = strlen (basedir);
	int typelen = strlen (filetype);

	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		// prevent overflow
		if ((len + 1) >= 0x40000) break;

		if (search->pack && !(flags & NO_PAK_CONTENT))
		{
			pack_t *pak = search->pack;

			for (int i = 0; i < pak->numfiles; i++)
			{
				int filelen = strlen (pak->files[i].name);
				if (filelen < typelen + dirlen) continue;
				if (_strnicmp (pak->files[i].name, basedir, dirlen)) continue;
				if (_stricmp (&pak->files[i].name[filelen - typelen], filetype)) continue;
				if (CheckExists (fl, &pak->files[i].name[dirlen])) continue;

				fl[len] = (char *) TempHunk->FastAlloc (strlen (&pak->files[i].name[dirlen]) + 1);
				strcpy (fl[len++], &pak->files[i].name[dirlen]);
				fl[len] = NULL;
			}
		}
		else if (search->pk3 && !(flags & NO_PAK_CONTENT))
		{
			pk3_t *pak = search->pk3;

			for (int i = 0; i < pak->numfiles; i++)
			{
				int filelen = strlen (pak->files[i].name);

				if (filelen < typelen + dirlen) continue;
				if (_strnicmp (pak->files[i].name, basedir, dirlen)) continue;
				if (_stricmp (&pak->files[i].name[filelen - typelen], filetype)) continue;
				if (CheckExists (fl, &pak->files[i].name[dirlen])) continue;

				fl[len] = (char *) TempHunk->FastAlloc (strlen (&pak->files[i].name[dirlen]) + 1);
				strcpy (fl[len++], &pak->files[i].name[dirlen]);
				fl[len] = NULL;
			}
		}
		else if (!(flags & NO_FS_CONTENT))
		{
			WIN32_FIND_DATA FindFileData;
			HANDLE hFind = INVALID_HANDLE_VALUE;
			char find_filter[MAX_PATH];

			Q_snprintf (find_filter, 260, "%s/%s*%s", search->filename, basedir, filetype);

			for (int i = 0;; i++)
			{
				if (find_filter[i] == 0) break;
				if (find_filter[i] == '/') find_filter[i] = '\\';
			}

			hFind = FindFirstFile (find_filter, &FindFileData);

			if (hFind == INVALID_HANDLE_VALUE)
			{
				// found no files
				FindClose (hFind);
				continue;
			}

			do
			{
				// not interested
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
				if (CheckExists (fl, FindFileData.cFileName)) continue;

				if (flags & PREPEND_PATH)
				{
					int itemlen = strlen (FindFileData.cFileName) + strlen (search->filename) + strlen (basedir) + 3;
					fl[len] = (char *) TempHunk->FastAlloc (itemlen);
					sprintf (fl[len++], "%s\\%s%s", search->filename, basedir, FindFileData.cFileName);
				}
				else
				{
					fl[len] = (char *) TempHunk->FastAlloc (strlen (FindFileData.cFileName) + 1);
					strcpy (fl[len++], FindFileData.cFileName);
				}

				fl[len] = NULL;
			} while (FindNextFile (hFind, &FindFileData));

			// done
			FindClose (hFind);
		}
	}

	// sort the list unless there is no list or we've specified not to sort it
	if (len && !(flags & NO_SORT_RESULT)) qsort (fl, len, sizeof (char *), COM_ListSortFunc);

	if (len)
	{
		// move from scratch to zone as actually attempting to access these files may use scratch which will crash the engine
		FileList[0] = (char **) TempHunk->FastAlloc (len * sizeof (char **));

#pragma warning(suppress : 6385) // Analysis error not accounting for full range of memcpy targets.
		Q_MemCpy (FileList[0], scratchbuf, len * sizeof (char **));
	}

	// return how many we got
	return len;
}


// runtime toggleable com_multiuser can put the engine into an infinite loop if cfg settings
// send it ping-ponging back and forth between on and off so we do it as a cmdline instead
void COM_CheckMultiUser (void);

void COM_InitFilesystem (void)
{
	COM_CheckMultiUser ();

	// check for expansion packs
	// (these are only checked at startup as the player might want to switch them off during gameplay; otherwise
	// they would be enforced on always)
	if (COM_CheckParm ("-rogue")) com_loadrogue = true;
	if (COM_CheckParm ("-hipnotic")) com_loadhipnotic = true;
	if (COM_CheckParm ("-quoth")) com_loadquoth = true;
	if (COM_CheckParm ("-nehahra")) com_loadnehahra = true;

	// -game <gamedir>
	// adds gamedir as an override game
	int i = COM_CheckParm ("-game");

	// load the specified game
	if (i && i < com_argc - 1)
		COM_LoadGame (com_argv[i + 1]);
	else COM_LoadGame (NULL);
}


// if we're going to allow players to set content locations we really should also validate them
bool COM_ValidateContentFolderCvar (cvar_t *var)
{
	if (!var->string)
	{
		Con_Printf ("%s is invalid : name does not exist\n", var->name);
		return false;
	}

	// this is a valid path as it means we're in the root of our game folder
	if (!var->string[0]) return true;

	if ((var->string[0] == '/' || var->string[0] == '\\') && !var->string[1])
	{
		// this is a valid path and is replaced by ''
		var->Set ("");
		return true;
	}

	if (strlen (var->string) > 64)
	{
		Con_Printf ("%s is invalid : name is too long\n", var->name);
		return false;
	}

	if (var->string[0] == '/' || var->string[0] == '\\')
	{
		Con_Printf ("%s is invalid : cannot back up beyond %s\n", var->name, com_gamedir);
		return false;
	}

	// copy it off so that we can safely modify it if need be
	char tempname[256];

	strcpy (tempname, var->string);

	// remove trailing /
	for (int i = 0;; i++)
	{
		// end of path
		if (!tempname[i]) break;

		if ((tempname[i] == '/' || tempname[i] == '\\') && !tempname[i + 1])
		{
			tempname[i] = 0;
			break;
		}
	}

	// \ / : * ? " < > | are all invalid in a name
	for (int i = 0;; i++)
	{
		// end of path
		if (!tempname[i]) break;

		// a folder separator is allowed at the end of the path
		if ((tempname[i] == '/' || tempname[i] == '\\') && !tempname[i + 1]) break;

		if (tempname[i] == '.' && tempname[i + 1] == '.')
		{
			Con_Printf ("%s is invalid : relative paths are not allowed\n", var->name);
			return false;
		}

		switch (tempname[i])
		{
		case ' ':
			Con_Printf ("%s is invalid : paths with spaces are not allowed\n", var->name);
			return false;

		case '\\':
		case '/':
		case ':':
		case '*':
		case '?':
		case '"':
		case '<':
		case '>':
		case '|':
			Con_Printf ("%s is invalid : contains \\ / : * ? \" < > or | \n", var->name);
			return false;

		default: break;
		}
	}

	// attempt to create the directory - CreateDirectory will fail if the directory already exists
	if (!PathIsDirectory (va ("%s/%s", com_gamedir, tempname)))
	{
		if (!CreateDirectory (va ("%s/%s", com_gamedir, tempname), NULL))
		{
			Con_Printf ("%s is invalid : failed to create directory\n", var->name);
			return false;
		}
	}

	// attempt to create a file in it; the user must have rw access to the directory
	HANDLE hf = CreateFile
	(
		va ("%s/%s/tempfile.tmp", com_gamedir, tempname),
		FILE_WRITE_DATA | FILE_READ_DATA,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
		NULL
	);

	if (hf == INVALID_HANDLE_VALUE)
	{
		Con_Printf ("%s is invalid : failed to create file\n", var->name);
		return false;
	}

	CloseHandle (hf);

	// path is valid now; we need a trailing / so add one
	var->Set (va ("%s/", tempname));
	return true;
}


void COM_ValidateUserSettableDir (cvar_t *var)
{
	if (!COM_ValidateContentFolderCvar (var))
	{
		Con_Printf ("Resetting to default \"%s\"\n", var->defaultvalue);
		var->Set (var->defaultvalue);
	}
}


void COM_ValidatePaths (char **paths)
{
	// PathIsDirectory
	for (int i = 0; ; i++)
	{
		if (!paths[i]) break;
		if (paths[i][0] == '*') continue;

		// so that we don't need to text this every time
		int len = strlen (paths[i]);
		bool found = false;

		for (searchpath_t *search = com_searchpaths; search; search = search->next)
		{
			if (search->pack)
			{
				pack_t *pak = search->pack;

				for (int j = 0; j < pak->numfiles; j++)
				{
					if (!_strnicmp (pak->files[j].name, paths[i], len))
					{
						found = true;
						break;
					}
				}
			}
			else if (search->pk3)
			{
				pk3_t *pak = search->pk3;

				for (int j = 0; j < pak->numfiles; j++)
				{
					if (!_strnicmp (pak->files[j].name, paths[i], len))
					{
						found = true;
						break;
					}
				}
			}
			else if (PathIsDirectory (va ("%s/%s", search->filename, paths[i])))
				found = true;

			if (found) break;
		}

		if (!found) paths[i][0] = '*';
	}
}


