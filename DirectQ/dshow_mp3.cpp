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
#include "winquake.h"

/*
================================================================================================================================
MCI interface
-------------
can we integrate this with the cdaudio code and have a single interface to rule them all? (preferable)

mciSendString ("open track02.mp3 type mpegvideo alias song1", NULL, 0, 0); - what about waves???
mciSendString ("set song1 video off", NULL, 0, 0);	// in case the file contains video info
mciSendString ("play song1 repeat", NULL, 0, 0);
mciSendString ("pause song1", NULL, 0, 0);
mciSendString ("resume song1", NULL, 0, 0);
mciSendString ("stop song1", NULL, 0, 0);
mciSendString ("close song1", NULL, 0, 0);

mciSendString ("setaudio song1 volume to factor", NULL, 0, 0);

for pak or pk3 extract to a temp dir and play from that; set to delete on close
what scale is used for setaudio volume???

public bool SetVolume (int volume)
{
	if (volume >= 0 && volume <= 1000)
	{
		Pcommand = "setaudio MediaFile volume to " + volume.ToString();
		error = mciSendString (Pcommand, null, 0, IntPtr.Zero);
		return true;
	}
	else return false;
}

exponential decibel crap???
================================================================================================================================
*/

#define QMCISONGDEVICE "weenixloonies"

#define MUSICFORMAT_NONE	0
#define MUSICFORMAT_MCI		1
#define MUSICFORMAT_OGG		2

int MusicFormat = MUSICFORMAT_NONE;

void MediaPlayer_Init (void)
{
	MusicFormat = MUSICFORMAT_NONE;
}


void MediaPlayer_Shutdown (void)
{
	// stop everything
	MediaPlayer_Stop ();
}


void MediaPlayer_Update (void)
{
}


void MediaPlayer_Stop (void)
{
	if (MusicFormat == MUSICFORMAT_MCI)
	{
		mciSendString ("stop "QMCISONGDEVICE, NULL, 0, 0);
		mciSendString ("close "QMCISONGDEVICE, NULL, 0, 0);
	}

	MusicFormat = MUSICFORMAT_NONE;
}


void MediaPlayer_Pause (void)
{
	if (MusicFormat == MUSICFORMAT_MCI)
		mciSendString ("pause "QMCISONGDEVICE, NULL, 0, 0);
}


void MediaPlayer_Resume (void)
{
	if (MusicFormat == MUSICFORMAT_MCI)
		mciSendString ("resume "QMCISONGDEVICE, NULL, 0, 0);
}


void MediaPlayer_ChangeVolume (void)
{
	if (MusicFormat == MUSICFORMAT_MCI)
	{
		int s_volume = (int) (bgmvolume.value * 1000.0f);

		if (s_volume < 0) s_volume = 0;
		if (s_volume > 1000) s_volume = 1000;

		// yayy - linear scale!
		mciSendString (va ("setaudio "QMCISONGDEVICE" volume to %i", s_volume), NULL, 0, 0);
	}
}


bool FindMediaFile (char *subdir, int track, bool looping)
{
	char **foundtracks = NULL;
	bool mediaplaying = false;

	// welcome to the world of char ***!  we want to be able to play all types of tracks here
	// don't sort the result so that the tracks will appear in the order of the specified gamedirs
	int hunkmark = TempHunk->GetLowMark ();
	int listlen = COM_BuildContentList (&foundtracks, subdir, ".*", NO_PAK_CONTENT | PREPEND_PATH | NO_SORT_RESULT);

	MusicFormat = MUSICFORMAT_NONE;

	if (listlen)
	{
		// we need to walk the entire list anyway to free memory so may as well search for the specified file here too
		// COM_BuildContentList returns files in alphabetical order so we just take the correct number
		for (int i = 0; i < listlen; i++)
		{
			// quake tracks are 2-based because the first track on the CD is the data track and CD tracks are 1-based
			if (i == (track - 2) && !mediaplaying)
			{
				char *ext = NULL;
				bool goodext = false;

				for (int j = strlen (foundtracks[i]); j; j--)
				{
					if (foundtracks[i][j] == '/') break;
					if (foundtracks[i][j] == '\\') break;

					if (foundtracks[i][j] == '.')
					{
						ext = &foundtracks[i][j + 1];
						break;
					}
				}

				// protect us oh jesus from shite codecs
				if (ext)
				{
					if (!_stricmp (ext, "wav")) goodext = true;
					if (!_stricmp (ext, "wma")) goodext = true;
					if (!_stricmp (ext, "mp3")) goodext = true;
				}

				// attempt to play it
				if (goodext)
				{
					// 0 if successful - needs filename in quotes as it may contain spaces
					if (!mciSendString (va ("open \"%s\" type mpegvideo alias "QMCISONGDEVICE, foundtracks[i]), NULL, 0, 0))
					{
						// must be set before the volume change otherwise it won't change!!!
						MusicFormat = MUSICFORMAT_MCI;

						// do an initial volume set
						MediaPlayer_ChangeVolume ();

						// and attempt to play it
						mciSendString ("set "QMCISONGDEVICE" video off", NULL, 0, 0);	// in case the file contains video info
						mciSendString (va ("play "QMCISONGDEVICE"%s", looping ? " repeat" : ""), NULL, 0, 0);

						mediaplaying = true;
					}
				}

				break;
			}
		}
	}

	TempHunk->FreeToLowMark (hunkmark);

	// return if the media is playing
	return mediaplaying;
}


bool MediaPlayer_Play (int track, bool looping)
{
	// stop any previous tracks
	MediaPlayer_Stop ();

	// every other ID game that uses music tracks in the filesystem has them in music, so it's
	// a reasonable assumption that that's what users will be expecting...
	if (FindMediaFile ("music/", track, looping)) return true;

	// darkplaces on the other hand does something *completely* different.  standards?  who needs 'em!
	if (FindMediaFile ("sound/cdtracks/", track, looping)) return true;

	return false;
}


