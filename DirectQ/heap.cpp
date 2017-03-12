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


byte *scratchbuf = NULL;

int TotalSize = 0;
int TotalPeak = 0;
int TotalReserved = 0;

/*
========================================================================================================================

		MEMCPY REPLACEMENT

	This is good for using with buffer objects; it guarantees that we won't get a read from the buffer
	so it is safe.  Also ~50% faster than CRT memcpy and on balance only marginally slower than
	http://www.cs.virginia.edu/stream/FTP/Contrib/AMD/memcpy_amd.cpp

========================================================================================================================
*/

#pragma optimize ("", off)
void *Q_MemCpy (void *dst, const void *src, size_t count)
{
	__asm
	{
		mov			esi, dword ptr [src]
		mov			edi, dword ptr [dst]

		cmp			dword ptr [count], 64
		jl			TryCopyQWord32

CopyQWord64:
		movq		mm0, [esi]
		movq		mm1, [esi + 8]
		movq		mm2, [esi + 16]
		movq		mm3, [esi + 24]
		movq		mm4, [esi + 32]
		movq		mm5, [esi + 40]
		movq		mm6, [esi + 48]
		movq		mm7, [esi + 56]
		add			esi, 64

		movntq		[edi], mm0
		movntq		[edi + 8], mm1
		movntq		[edi + 16], mm2
		movntq		[edi + 24], mm3
		movntq		[edi + 32], mm4
		movntq		[edi + 40], mm5
		movntq		[edi + 48], mm6
		movntq		[edi + 56], mm7
		add			edi, 64

		sub			dword ptr [count], 64
		cmp			dword ptr [count], 64
		jge			CopyQWord64

TryCopyQWord32:
		cmp			dword ptr [count], 32
		jl			TryCopyQWord16

CopyQWord32:
		movq		mm0, [esi]
		movq		mm1, [esi + 8]
		movq		mm2, [esi + 16]
		movq		mm3, [esi + 24]
		add			esi, 32

		movntq		[edi], mm0
		movntq		[edi + 8], mm1
		movntq		[edi + 16], mm2
		movntq		[edi + 24], mm3
		add			edi, 32

		sub			dword ptr [count], 32
		cmp			dword ptr [count], 32
		jge			CopyQWord32

TryCopyQWord16:
		cmp			dword ptr [count], 16
		jl			TryCopyQWord8

CopyQWord16:
		movq		mm0, [esi]
		movq		mm1, [esi + 8]
		add			esi, 16

		movntq		[edi], mm0
		movntq		[edi + 8], mm1
		add			edi, 16

		sub			dword ptr [count], 16
		cmp			dword ptr [count], 16
		jge			CopyQWord16

TryCopyQWord8:
		cmp			dword ptr [count], 8
		jl			TryCopyDWord

CopyQWord8:
		movq		mm0, [esi]
		add			esi, 8

		movntq		[edi], mm0
		add			edi, 8

		sub			dword ptr [count], 8
		cmp			dword ptr [count], 8
		jge			CopyQWord8

TryCopyDWord:
		cmp			dword ptr [count], 3
		jle			TryCopyWord

		mov			ecx, dword ptr [count]
		shr			ecx, 2
		mov			eax, ecx
		rep movsd

		shl			eax, 2
		sub			dword ptr [count], eax

TryCopyWord:
		cmp			dword ptr [count], 1
		jle			TryCopyByte

		movsw

		sub			dword ptr [count], 2

TryCopyByte:
		cmp			dword ptr [count], 0
		je			CopyDone

		movsb

CopyDone:
		emms
		sfence
		mov			eax, [dst]
	}
}
#pragma optimize ("", on)


/*
========================================================================================================================

		ZONE MEMORY

========================================================================================================================
*/

#define HEAP_MAGIC 0x35012560


CQuakeZone::CQuakeZone (void)
{
	// prevent this->EnsureHeap from exploding
	this->hHeap = NULL;

	// create it
	this->EnsureHeap ();
}


void *CQuakeZone::AllocCommon (int size, DWORD allocflag)
{
	this->EnsureHeap ();
	assert (size > 0);

	int *buf = (int *) HeapAlloc (this->hHeap, allocflag, size + sizeof (int) * 2);

	assert (buf);

	// mark as no-execute; not critical so fail it silently
	// note that HeapAlloc uses VirtualAlloc behind the scenes, so this is valid
	DWORD dwdummy = 0;
	VirtualProtect (buf, size, PAGE_READWRITE, &dwdummy);

	buf[0] = HEAP_MAGIC;
	buf[1] = size;

	this->Size += size;

	if (this->Size > this->Peak) this->Peak = this->Size;

	TotalSize += size;

	if (TotalSize > TotalPeak) TotalPeak = TotalSize;

	return (buf + 2);
}


void *CQuakeZone::FastAlloc (int size)
{
	return this->AllocCommon (size, 0);
}


void *CQuakeZone::Alloc (int size)
{
	return this->AllocCommon (size, HEAP_ZERO_MEMORY);
}


void CQuakeZone::Free (void *data)
{
	if (!this->hHeap) return;

	if (!data) return;

	int *buf = (int *) data;
	buf -= 2;

	assert (buf[0] == HEAP_MAGIC);

	// this should never happen but let's protect release builds anyway...
	if (buf[0] != HEAP_MAGIC) return;

	this->Size -= buf[1];
	TotalSize -= buf[1];

	BOOL blah = HeapFree (this->hHeap, 0, buf);
	assert (blah);
}


void CQuakeZone::Compact (void)
{
	HeapCompact (this->hHeap, 0);
}


void CQuakeZone::EnsureHeap (void)
{
	if (!this->hHeap)
	{
		this->hHeap = HeapCreate (0, 0x10000, 0);
		assert (this->hHeap);

		this->Size = 0;
		this->Peak = 0;
	}
}


void CQuakeZone::Discard (void)
{
	if (this->hHeap)
	{
		TotalSize -= this->Size;
		this->Size = 0;
		HeapDestroy (this->hHeap);
		this->hHeap = NULL;
	}
}


CQuakeZone::~CQuakeZone (void)
{
	this->Discard ();
}


float CQuakeZone::GetSizeMB (void)
{
	return (((float) this->Size) / 1024.0f) / 1024.0f;
}


/*
========================================================================================================================

		HUNK MEMORY

========================================================================================================================
*/

CQuakeHunk::CQuakeHunk (int maxsizemb)
{
	// sizes in KB
	this->MaxSize = maxsizemb * 1024 * 1024;
	this->LowMark = 0;
	this->HighMark = 0;

	TotalReserved += this->MaxSize;

	// reserve the full block but do not commit it yet
	this->BasePtr = (byte *) VirtualAlloc (NULL, this->MaxSize, MEM_RESERVE, PAGE_NOACCESS);

	if (!this->BasePtr)
		Sys_Error ("CQuakeHunk::CQuakeHunk - VirtualAlloc failed on memory pool");

	// commit an initial block
	this->Initialize ();
}


CQuakeHunk::~CQuakeHunk (void)
{
#pragma warning(suppress : 6250)
	VirtualFree (this->BasePtr, this->MaxSize, MEM_DECOMMIT);
	VirtualFree (this->BasePtr, 0, MEM_RELEASE);
	TotalSize -= this->LowMark;
	TotalReserved -= this->MaxSize;
}


int CQuakeHunk::GetLowMark (void)
{
	return this->LowMark;
}

void CQuakeHunk::FreeToLowMark (int mark)
{
	TotalSize -= (this->LowMark - mark);
	this->LowMark = mark;
}

float CQuakeHunk::GetSizeMB (void)
{
	return (((float) this->LowMark) / 1024.0f) / 1024.0f;
}


void *CQuakeHunk::FastAlloc (int size)
{
	// ensure that the buffer pointer is always 16-aligned
	size = (size + 15) & ~15;

	if (this->LowMark + size >= this->MaxSize)
	{
		Sys_Error ("CQuakeHunk::Alloc - overflow on \"%s\" memory pool", this->Name);
		return NULL;
	}

	// size might be > the extra alloc size
	if ((this->LowMark + size) > this->HighMark)
	{
		// round to 1MB boundaries
		this->HighMark = (this->LowMark + size + 0xfffff) & ~0xfffff;

		// this will walk over a previously committed region.  i might fix it...
		if (!VirtualAlloc (this->BasePtr + this->LowMark, this->HighMark - this->LowMark, MEM_COMMIT, PAGE_READWRITE))
		{
			Sys_Error ("CQuakeHunk::Alloc - VirtualAlloc failed for \"%s\" memory pool", this->Name);
			return NULL;
		}
	}

	// fix up pointers and return what we got
	byte *buf = this->BasePtr + this->LowMark;
	this->LowMark += size;

	TotalSize += size;

	if (TotalSize > TotalPeak) TotalPeak = TotalSize;

	return buf;
}


void CQuakeHunk::UnlockScratch (void)
{
	// because allocating as scratch is DANGEROUS we much tell Quake that we know what we're doing by
	// explicitly unlocking it before each and every alloc - it will automatically lock again during the alloc
	this->ScratchLocked = false;
}


void *CQuakeHunk::ScratchAlloc (int size)
{
	// because allocating as scratch is DANGEROUS we must tell Quake that we know what we're doing by
	// explicitly unlocking it before each and every alloc - it will automatically lock again during the alloc
	if (this->ScratchLocked)
	{
		Sys_Error ("CQuakeHunk::ScratchAlloc called without CQuakeHunk::UnlockScratch");
		return NULL;
	}

	// same as fast alloc only it doesn't advance the low mark
	// only use for REALLY temp allocations...................
	int mark = this->LowMark;
	byte *buf = (byte *) this->FastAlloc (size);

	// this is the DANGEROUS bit - reset the low mark so that the memory is
	// immediately reusable by the next alloc.  also lock scratch for safety
	this->FreeToLowMark (mark);
	this->ScratchLocked = true;

	return buf;
}


void *CQuakeHunk::Alloc (int size)
{
	byte *buf = (byte *) this->FastAlloc (size);

	memset (buf, 0, size);

	return buf;
}

void CQuakeHunk::Free (void)
{
	// just NULL everything out - we can't do a full decommit as memory in a hunk may be accessed after the hunk is freed (original Quake bug)
	memset (this->BasePtr, 0, this->LowMark);
	TotalSize -= this->LowMark;
	this->LowMark = 0;
}


void CQuakeHunk::Initialize (void)
{
	// commit an initial page of 64k
	VirtualAlloc (this->BasePtr, 0x10000, MEM_COMMIT, PAGE_READWRITE);

	this->LowMark = 0;
	this->HighMark = 0x10000;
	this->ScratchLocked = true;
}


/*
========================================================================================================================

		INITIALIZATION

========================================================================================================================
*/

CQuakeHunk *TempHunk = NULL;
CQuakeZone *MainZone = NULL;
CQuakeHunk *MainHunk = NULL;
CQuakeHunk *GameHunk = NULL;


void Heap_Init (void)
{
	// init the pools we want to keep around all the time
	if (!TempHunk) TempHunk = new CQuakeHunk (256);
	if (!MainHunk) MainHunk = new CQuakeHunk (256);
	if (!GameHunk) GameHunk = new CQuakeHunk (64);
	if (!MainZone) MainZone = new CQuakeZone ();

	// take a chunk of memory for use by temporary loading functions and other doo-dahs
	if (!scratchbuf) scratchbuf = (byte *) MainZone->Alloc (SCRATCHBUF_SIZE);
}

