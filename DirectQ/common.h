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

// comndef.h  -- general definitions

#if !defined BYTE_DEFINED
typedef unsigned char 		byte;
#define BYTE_DEFINED 1
#endif

extern bool com_rmq;
size_t Q_strncpy (char *dst, const char *src, size_t siz);


//============================================================================

struct sizebuf_t
{
	bool	allowoverflow;	// if false, do a Sys_Error
	bool	overflowed;		// set to true if the buffer size failed
	byte	*data;
	int		maxsize;
	int		cursize;
};

void SZ_Init (sizebuf_t *buf, void *data, int len);
void SZ_Alloc (sizebuf_t *buf, int startsize);
void SZ_Clear (sizebuf_t *buf);
void *SZ_GetSpace (sizebuf_t *buf, int length, char *caller);
void SZ_Write (sizebuf_t *buf, void *data, int length);
void SZ_Print (sizebuf_t *buf, char *data);	// strcats onto the sizebuf

//============================================================================

// this needs to be here so that pretty much everything else can compile OK...
struct link_t
{
	link_t *prev;
	link_t *next;
};


// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define	STRUCT_FROM_LINK(l,t,m) ((t *)((byte *)l - (int)&(((t *)0)->m)))

//============================================================================

void MSG_WriteChar (sizebuf_t *sb, int c);
void MSG_WriteByte (sizebuf_t *sb, int c);
void MSG_WriteShort (sizebuf_t *sb, int c);
void MSG_WriteLong (sizebuf_t *sb, int c);
void MSG_WriteFloat (sizebuf_t *sb, float f);
void MSG_WriteString (sizebuf_t *sb, char *s);


void MSG_WriteCoord (sizebuf_t *sb, float f, int protocol, unsigned flags);
void MSG_WriteAngle (sizebuf_t *sb, float f, int protocol, unsigned flags, int angleindex);
float MSG_ReadCoord (int protocol, unsigned flags);
float MSG_ReadAngle (int protocol, unsigned flags);
void MSG_WriteAngle16 (sizebuf_t *sb, float f, int protocol, unsigned flags);
float MSG_ReadAngle16 (int protocol, unsigned flags);
void MSG_WriteProQuakeAngle (sizebuf_t *sb, float f);
float MSG_ReadProQuakeAngle (void);

void MSG_WriteAngle8_Old (sizebuf_t *sb, float f);
float MSG_ReadAngle8_Old (void);

extern	int			msg_readcount;
extern	bool	msg_badread;		// set if a read goes beyond end of message

void MSG_BeginReading (void);
int MSG_ReadChar (void);
int MSG_ReadByte (void);
int MSG_ReadShort (void);
int MSG_ReadLong (void);
float MSG_ReadFloat (void);
char *MSG_ReadString (void);

//============================================================================

extern	char		com_token[1024];
extern	bool	com_eof;

#define COM_PARSE_TOKEN		false
#define COM_PARSE_LINE		true

char *COM_Parse (char *data, bool parsefullline = false);


extern	int		com_argc;
extern	char	**com_argv;

int COM_CheckParm (char *parm);
void COM_Init (char *path);
void COM_InitArgv (int argc, char **argv);

char *COM_SkipPath (char *pathname);
void COM_StripExtension (char *in, char *out);
void COM_FileBase (char *in, char *out);
void COM_DefaultExtension (char *path, char *extension);

char	*va (char *format, ...);
// does a varargs printf into a temp buffer


//============================================================================

struct cache_user_s;

// common.h doesn't know what MAX_PATH is
extern char com_gamedir[];
extern char	com_gamename[];

void COM_ExecQuakeRC (void);

extern bool		standard_quake, rogue, hipnotic, quoth, nehahra;

void COM_HashData (byte *hash, const void *data, int size);
#define COM_CheckHash(h1, h2) !(memcmp ((h1), (h2), 16))

void COM_SortStringList (char **stringlist, bool ascending);

#define COM_MAXGAMES 256

extern int com_numgames;
extern char *com_games[];

#define NO_PAK_CONTENT	1
#define NO_FS_CONTENT	2
#define PREPEND_PATH	4
#define NO_SORT_RESULT	8

// finding content
int COM_BuildContentList (char ***FileList, char *basedir, char *filetype, int flags = 0);
bool COM_StringContains (char *str1, char *str2);
bool COM_FindExtension (char *filename, char *ext);


struct packfile_t
{
	// keep this the same as the on-disk version so that we can use the same memory for both
	char    name[56];
	int     filepos, filelen;
};

struct pack_t
{
	char filename[MAX_PATH];
	HANDLE fhandle;
	HANDLE mmhandle;
	int numfiles;
	packfile_t *files;
};


struct pk3_t
{
	char			filename[MAX_PATH];
	int             numfiles;
	packfile_t      *files;
};


// new memory-mapped filesystem
class CQuakeFile
{
public:
	CQuakeFile (void);
	void Close (void);

	bool ValidateLength (int expectedlength);
	bool Open (char *filename, int flags = 0);
	int Read (void *destbuf, int length);
	int ReadChar (void);
	int GetLength (void);
	DWORD SetPointer (LONG position, DWORD from);

	bool CreateTempFile (char *filename);
	bool CreateNewFile (char *filename);
	bool Write (void *data, int length);
	void GetFileTime (char *time);

	static void *LoadFile (char *path, class CQuakeAllocator *spacebuf = NULL);
	void *CopyAlloc (class CQuakeAllocator *spacebuf = NULL);

	static int FileSize;

private:
	void SetInfo (pack_t *pak = NULL, packfile_t *packfile = NULL);
	void ClearFile (void);
	bool LoadFromPK3 (pk3_t *pk3, char *filename);
	packfile_t *FindInPAK (packfile_t *files, int numfiles, char *filename);

	// for file mappings
	int mapoffset;
	int maplength;

	// handle to the file object
	HANDLE fhandle;

	// handle to the memory mapping object
	HANDLE mmhandle;

	// true if this file comes from a PAK file, in which case we only need to umap the view and can retain the handles on close
	// (the handles must be closed when shutting down or switching games) - the pointers should be NULLed in either case
	bool pakfile;

	// offset and length for the file; prevents us from having to use com_file* globals
	int fileoffset;
	int filelength;

	int filepointer;

	// pointer returned by MapViewOfFile which may be different to the actual file data pointer owing to granularity rules bullshit
	// needed for unmapping the file
	void *mmdata;

	// actual file data pointer
	void *filedata;
};


struct dpackheader_t
{
	char    id[4];
	int             dirofs;
	int             dirlen;
};

#define MAX_FILES_IN_PACK       2048

struct searchpath_t
{
	char    filename[MAX_PATH];
	pack_t  *pack;          // only one of filename / pack will be used
	pk3_t *pk3;
	searchpath_t *next;
};

extern searchpath_t    *com_searchpaths;

bool COM_ValidateContentFolderCvar (class cvar_t *var);
void COM_ValidateUserSettableDir (class cvar_t *var);
void COM_ValidatePaths (char **paths);

char *COM_ShiftTextColor (char *str);
int COM_GetFileSize (std::ifstream &f);

extern bool com_loadquoth;
extern bool com_loadrogue;
extern bool com_loadhipnotic;
extern bool com_loadnehahra;

