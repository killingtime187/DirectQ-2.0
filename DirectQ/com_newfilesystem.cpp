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
#include "modelgen.h"
#include "unzip.h"


// helper to get the file size for reading where we need to read into a dynamically alloced buffer
int COM_GetFileSize (std::ifstream &f)
{
	// store where we were
	int filepos = f.tellg ();

	// go to the end of the file
	f.seekg (0, std::ios::end);

	// read the size
	int filesize = f.tellg ();

	// go back to where we were
	f.seekg (filepos, std::ios::beg);

	// return the size
	return filesize;
}


HANDLE COM_CreateFile (char *filename)
{
	// shared by PAK files and files on disk
	return CreateFile
	(
		filename,
		FILE_READ_DATA,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_NO_RECALL | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL
	);
}


int CQuakeFile::FileSize = 0;

packfile_t *CQuakeFile::FindInPAK (packfile_t *files, int numfiles, char *filename)
{
	assert (files);
	assert (filename);
	assert (numfiles >= 0);

	// to do - we could potentially short-circuit some of this by defining a range to search in based on the first char of the filename
	int imin = 0;
	int imax = numfiles - 1;

	for (;;)
	{
		if (imax < imin) break;

		int imid = (imax + imin) >> 1;
		int comp = _stricmp (files[imid].name, filename);

		if (comp < 0)
			imin = imid + 1;
		else if (comp > 0)
			imax = imid - 1;
		else return &files[imid];
	}

	// not found
	return NULL;
}


CQuakeFile::CQuakeFile (void)
{
	this->ClearFile ();
}


void CQuakeFile::ClearFile (void)
{
	this->fhandle = INVALID_HANDLE_VALUE;
	this->mmhandle = INVALID_HANDLE_VALUE;

	this->mapoffset = 0;
	this->maplength = 0;

	this->filelength = 0;
	this->fileoffset = 0;
	this->filepointer = 0;

	this->filedata = NULL;
	this->mmdata = NULL;

	this->pakfile = false;
}


DWORD CQuakeFile::SetPointer (LONG position, DWORD from)
{
	if (this->filedata)
	{
		// for simplicity we don't support reading from the end
		if (from == FILE_BEGIN)
		{
			this->filedata = ((byte *) this->mmdata) + (this->maplength - this->filelength) + position;
			this->filepointer = position;
			return position;
		}
		else if (from == FILE_CURRENT)
		{
			this->filedata = ((byte *) this->filedata) + position;
			this->filepointer += position;
			return 0;
		}
		else
		{
			Sys_Error ("CQuakeFile::SetPointer : invalid dwMoveMethod");
			return 0;
		}
	}
	else if (this->fhandle)
	{
		this->filepointer = SetFilePointer (this->fhandle, position, NULL, from);
		return this->filepointer;
	}
	else
	{
		Sys_Error ("CQuakeFile::SetPointer without file->filedata or file->fhandle");
		return 0;
	}
}


int CQuakeFile::Read (void *destbuf, int length)
{
	// read and advance the pointer
	if (this->filedata)
	{
		if (this->filepointer + length <= this->filelength)
		{
			Q_MemCpy (destbuf, this->filedata, length);
			this->filedata = ((byte *) this->filedata) + length;
			this->filepointer += length;
			return length;
		}
		else return -1;
	}
	else if (this->fhandle)
	{
		DWORD bytesread = 0;

		if (ReadFile (this->fhandle, destbuf, length, &bytesread, NULL))
		{
			this->filepointer += length;
			return (int) bytesread;
		}
		else return -1;
	}
	else
	{
		Sys_Error ("CQuakeFile::Read without this->filedata or this->fhandle");
		return -1;
	}
}


int CQuakeFile::ReadChar (void)
{
	char c = 0;

	if (this->Read (&c, 1) != -1)
		return c;
	else return -1;
}


void CQuakeFile::Close (void)
{
	if (this->mmdata) UnmapViewOfFile (this->mmdata);

	// PAK files can keep these handles open always; other file types need to close them
	if (!this->pakfile)
	{
		if (this->mmhandle != INVALID_HANDLE_VALUE) CloseHandle (this->mmhandle);
		if (this->fhandle != INVALID_HANDLE_VALUE) CloseHandle (this->fhandle);
	}

	// any further attempts to access the file are errors
	this->ClearFile ();
}


int CQuakeFile::GetLength (void)
{
	return this->filelength;
}


void CQuakeFile::SetInfo (pack_t *pak, packfile_t *packfile)
{
	this->fhandle = pak ? pak->fhandle : this->fhandle;
	this->mmhandle = pak ? pak->mmhandle : INVALID_HANDLE_VALUE;

	this->mmdata = NULL;
	this->filedata = NULL;

	this->fileoffset = packfile ? packfile->filepos : 0;
	this->filelength = packfile ? packfile->filelen : GetFileSize (this->fhandle, NULL);
	this->pakfile = pak ? true : false;
	this->filepointer = 0;

	CQuakeFile::FileSize = this->filelength;
}


bool CQuakeFile::LoadFromPK3 (pk3_t *pk3, char *filename)
{
	// yech, this is nasty shit...
	unzFile uf;
	packfile_t *found;

	// ensure that it's there before we go any further
	if (!(found = this->FindInPAK (pk3->files, pk3->numfiles, filename))) return false;
	if (!(uf = unzOpen (pk3->filename))) return false;

	// instead of a linear walk we find it FAST by just going directly to the file position
	if (unzSetCurrentFileInfoPosition (uf, found->filepos) != UNZ_OK)
	{
		unzClose (uf);
		return false;
	}

	unz_file_info file_info;
	char filename_inzip[MAX_PATH] = {0};

	// something bad happened if these fail
	if (unzOpenCurrentFile (uf) != UNZ_OK) goto fileerror;
	if (unzGetCurrentFileInfo (uf, &file_info, filename_inzip, sizeof (filename_inzip), NULL, 0, NULL, 0) != UNZ_OK) goto fileerror;

	// (note - using scratchbuf is potentially unsafe here as it may be used for other stuff
	// further up the call stack (map list enumeration was using it which caused crashes))
	// it is expected that the caller will manage hunk usage here
	const int maxbytestoread = 65536;
	static byte unztemp[maxbytestoread];

	if (!this->CreateTempFile (filename)) return false;

	for (;;)
	{
		// read in maxbytestoread blocks
		int bytesread = unzReadCurrentFile (uf, unztemp, maxbytestoread);

		if (bytesread == 0) break;
		if (bytesread < 0) goto pk3error;
		if (!this->Write (unztemp, bytesread)) goto pk3error;
	}

	unzCloseCurrentFile (uf);
	unzClose (uf);

	// fixme - get rid of this
	return true;

pk3error:;
	// bad read from pk3
	unzCloseCurrentFile (uf);
	unzClose (uf);
	this->Close ();
	return false;

fileerror:;
	// something bad happened
	unzClose (uf);
	return false;
}


bool CQuakeFile::Open (char *filename, int flags)
{
	// begin with an empty file
	this->ClearFile ();

	if (!filename)
	{
		Con_SafePrintf ("CQuakeFile::Open: filename not set");
		return false;
	}

	// a mod exists that does something evil - it includes a config.cfg and autoexec.cfg in it's pak file that overwrites the player's settings.
	// let's not allow that to happen.
	bool allowpak = true;
	char netpath[MAX_PATH];

	// prevent mods from overwriting player settings
	if (!_stricmp (filename, "config.cfg")) allowpak = false;
	if (!_stricmp (filename, "directq.cfg")) allowpak = false;
	if (!_stricmp (filename, "autoexec.cfg")) allowpak = false;

	for (searchpath_t *search = com_searchpaths; search; search = search->next)
	{
		// refuse to load these files from a PAK
		if ((search->pack || search->pk3) && !allowpak) continue;

		if (search->pack)
		{
			// locate the file
			pack_t *pak = search->pack;
			packfile_t *found = NULL;
			extern SYSTEM_INFO SysInfo;

			if ((found = this->FindInPAK (pak->files, pak->numfiles, filename)) != NULL)
			{
				// fucking allocation granularity rules
				this->mapoffset = (found->filepos / SysInfo.dwAllocationGranularity) * SysInfo.dwAllocationGranularity;
				this->maplength = found->filelen + (found->filepos - this->mapoffset);

				this->SetInfo (pak, found);

				this->mmdata = MapViewOfFile (pak->mmhandle, FILE_MAP_READ, 0, this->mapoffset, this->maplength);
				this->filedata = ((byte *) this->mmdata) + (this->maplength - found->filelen);

				return true;
			}
		}
		else if (search->pk3)
		{
			if (this->LoadFromPK3 (search->pk3, filename))
			{
				// need to reset the file pointer as it will be at eof owing to the file just having been created
				SetFilePointer (this->fhandle, 0, NULL, FILE_BEGIN);
				this->SetInfo ();
				return true;
			}
		}
		else
		{
			// check for a file in the directory tree
			Q_snprintf (netpath, 256, "%s/%s", search->filename, filename);

			if ((this->fhandle = COM_CreateFile (netpath)) != INVALID_HANDLE_VALUE)
			{
				this->SetInfo ();
				return true;
			}
		}
	}

	return false;
}


bool CQuakeFile::ValidateLength (int expectedlength)
{
	if (this->filelength == expectedlength)
		return true;
	else return false;
}


bool CQuakeFile::CreateTempFile (char *filename)
{
	this->ClearFile ();

	char fpath1[MAX_PATH];
	char fpath2[MAX_PATH];

	// get the path to the user's temp folder; normally %USERPROFILE%\Local Settings\temp
	if (!GetTempPath (MAX_PATH, fpath1))
	{
		// oh crap
		return false;
	}

	// ensure it exists
	CreateDirectory (fpath1, NULL);

	// build the second part of the path
	Q_snprintf (fpath2, MAX_PATH, "\\DirectQ\\%s", filename);

	// replace path delims with _ so that files are created directly under %USERPROFILE%\Local Settings\temp
	// skip the first cos we wanna keep that one
	for (int i = 1;; i++)
	{
		if (fpath2[i] == 0) break;
		if (fpath2[i] == '/') fpath2[i] = '_';
		if (fpath2[i] == '\\') fpath2[i] = '_';
	}

	// now build the final name
	strcat (fpath1, fpath2);

	DWORD access = FILE_WRITE_DATA | FILE_READ_DATA;
	DWORD attrib = FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE;

	// create the file - see http://blogs.msdn.com/larryosterman/archive/2004/04/19/116084.aspx for
	// further info on the flags chosen here.
	if ((this->fhandle = CreateFile (fpath1, access, FILE_SHARE_READ, NULL, CREATE_ALWAYS, attrib, NULL)) != INVALID_HANDLE_VALUE)
	{
		this->SetInfo ();
		return true;
	}
	else return false;
}


// i can't call this CreateFile cos MS have #defined that to something else in their headers.  grrrrrr.
bool CQuakeFile::CreateNewFile (char *filename)
{
	this->ClearFile ();

	if ((this->fhandle = CreateFile (filename, FILE_WRITE_DATA, 0, NULL, CREATE_ALWAYS, 0, NULL)) != INVALID_HANDLE_VALUE)
	{
		this->SetInfo ();
		return true;
	}
	else return false;
}


bool CQuakeFile::Write (void *data, int length)
{
	DWORD byteswritten = 0;
	BOOL ret = WriteFile (this->fhandle, data, length, &byteswritten, NULL);

	if (ret && byteswritten == length)
		return true;
	else return false;
}


void *CQuakeFile::CopyAlloc (class CQuakeAllocator *spacebuf)
{
	void *membuf = NULL;

	// use the correct allocator; we can safely fastalloc as we're just overwriting the buffer immediately
	if (spacebuf)
		membuf = spacebuf->FastAlloc (this->filelength + 1);
	else membuf = MainZone->FastAlloc (this->filelength + 1);

	// copy it out so that we can freely modify the data (and close the mapping)
	((byte *) membuf)[this->filelength] = 0;

	// can't use CQuakeFile::Read here because it advances the pointer (which fucks with sound) so just do it direct
	if (this->pakfile)
		Q_MemCpy (membuf, this->filedata, this->filelength);
	else this->Read (membuf, this->filelength);

	CQuakeFile::FileSize = this->filelength;

	return membuf;
}


void CQuakeFile::GetFileTime (char *time)
{
	BY_HANDLE_FILE_INFORMATION FileInfo;

	// retrieve the info
	if (GetFileInformationByHandle (this->fhandle, &FileInfo))
	{
		// use the last write time so that it's always valid for the last save
		// there's no FileTimeToLocalTime - grrrrr...!
		FILETIME lft;
		SYSTEMTIME st;

		FileTimeToLocalFileTime (&FileInfo.ftLastWriteTime, &lft);
		FileTimeToSystemTime (&lft, &st);

		sprintf (time, "%04i/%02i/%02i %02i:%02i", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
	}
	else time[0] = 0;
}


void *CQuakeFile::LoadFile (char *path, class CQuakeAllocator *spacebuf)
{
	CQuakeFile f;

	if (f.Open (path))
	{
		void *membuf = f.CopyAlloc (spacebuf);

		f.Close ();

		return membuf;
	}
	else
	{
		CQuakeFile::FileSize = -1;
		return NULL;
	}
}


int FS_FileCompare (packfile_t *a, packfile_t *b)
{
	return _stricmp (a->name, b->name);
}


pack_t *COM_LoadPackFile (char *packfile)
{
	dpackheader_t header;
	std::ifstream packfp (packfile, std::ios::in | std::ios::binary);

	if (!packfp.is_open ()) return NULL;

	packfp.read ((char *) &header, sizeof (dpackheader_t));

	if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
	{
		Con_SafePrintf ("%s is not a packfile", packfile);
		packfp.close ();
		return NULL;
	}

	int numpackfiles = header.dirlen / sizeof (packfile_t);
	packfile_t *info = (packfile_t *) GameHunk->Alloc (numpackfiles * sizeof (packfile_t));

	packfp.seekg (header.dirofs, std::ios::beg);
	packfp.read ((char *) info, numpackfiles * sizeof (packfile_t));
	packfp.close ();

	// load the directory
	pack_t *pack = (pack_t *) GameHunk->Alloc (sizeof (pack_t));
	Q_strncpy (pack->filename, packfile, 127);
	pack->numfiles = numpackfiles;
	pack->files = info;

	// create the file object and the memory mapping
	pack->fhandle = COM_CreateFile (pack->filename);
	pack->mmhandle = CreateFileMapping (pack->fhandle, NULL, PAGE_READONLY, 0, 0, NULL);

	qsort (pack->files, pack->numfiles, sizeof (packfile_t), (sortfunc_t) FS_FileCompare);

	Con_SafePrintf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}


