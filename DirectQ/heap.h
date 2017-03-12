
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

// 1 MB buffer for general short-lived allocations
extern byte *scratchbuf;
#define SCRATCHBUF_SIZE 0x100000

// interface
void Heap_Init (void);

// this just allows a CQuakeAllocator to be passed as an arg to any function that requires an allocator
class CQuakeAllocator
{
public:
	virtual void *Alloc (int size) = 0;
	virtual void *FastAlloc (int size) = 0;
};


class CQuakeHunk : public CQuakeAllocator
{
public:
	CQuakeHunk (int maxsizemb);
	~CQuakeHunk (void);
	void *Alloc (int size);
	void *FastAlloc (int size);
	void *ScratchAlloc (int size = SCRATCHBUF_SIZE);
	void UnlockScratch (void);
	void Free (void);
	float GetSizeMB (void);

	int GetLowMark (void);
	void FreeToLowMark (int mark);

private:
	void Initialize (void);
	int MaxSize;	// maximum memory reserved by this buffer (converted to bytes in constructor)
	int LowMark;	// current memory pointer position
	int HighMark;	// size of all committed memory so far
	bool ScratchLocked;

	char Name[64];

	byte *BasePtr;
};


class CQuakeZone : public CQuakeAllocator
{
public:
	CQuakeZone (void);
	~CQuakeZone (void);
	void *Alloc (int size);
	void *FastAlloc (int size);
	void Free (void *data);
	void Compact (void);
	void Discard (void);
	float GetSizeMB (void);

private:
	void *AllocCommon (int size, DWORD allocflag);
	void EnsureHeap (void);
	HANDLE hHeap;
	int Size;
	int Peak;
};


// space buffers
extern CQuakeHunk *TempHunk;
extern CQuakeHunk *GameHunk;
extern CQuakeZone *MainZone;
extern CQuakeHunk *MainHunk;

// memcpy replacement
void *Q_MemCpy (void *dst, const void *src, size_t count);


