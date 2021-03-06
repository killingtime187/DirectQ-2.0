/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_dma.c -- main control for any streaming sound output device

#include "quakedef.h"
#include "d3d_model.h"
#include "winquake.h"
#include <vector>

extern LPDIRECTSOUNDBUFFER8 ds_SecondaryBuffer8;
extern DWORD ds_SoundBufferSize;


void S_Play (void);
void S_Play2 (void);
void S_PlayVol (void);
void S_SoundList (void);
void S_Update_();
void S_StopAllSounds (bool clear);
void S_StopAllSoundsC (void);

// =======================================================================
// Internal sound data & structures
// =======================================================================

channel_t   channels[MAX_CHANNELS];
int			total_channels;

int			snd_blocked = 0;
static bool	snd_ambient = 1;
bool		snd_initialized = false;

// pointer should go away
volatile dma_t *shm = 0;
volatile dma_t sn;

vec3_t		listener_origin;
vec3_t		listener_forward;
vec3_t		listener_right;
vec3_t		listener_up;

cvar_t sound_nominal_clip_dist ("snd_clipdist", 1500, CVAR_ARCHIVE);

int			soundtime;		// sample PAIRS
int   		paintedtime; 	// sample PAIRS


std::vector<sfx_t *> known_sfx;

sfx_t		*ambient_sfx[NUM_AMBIENTS];

int 		desired_speed = 11025;
int 		desired_bits = 16;

int sound_started = 0;

cvar_t bgmvolume ("bgmvolume", "1", CVAR_ARCHIVE);
cvar_t volume ("volume", "0.7", CVAR_ARCHIVE);

cvar_t nosound ("nosound", "0");
cvar_t precache ("precache", "1");
cvar_t bgmbuffer ("bgmbuffer", "4096");
cvar_t ambient_level ("ambient_level", "0.3", CVAR_ARCHIVE);
cvar_t ambient_fade ("ambient_fade", "100", CVAR_ARCHIVE);
cvar_t snd_noextraupdate ("snd_noextraupdate", "0");
cvar_t snd_show ("snd_show", "0");
cvar_t _snd_mixahead ("_snd_mixahead", "0.1", CVAR_ARCHIVE);


void S_UpdateContentSounds (cvar_t *unused);

cvar_t sfx_water ("sfx_water", "ambience/water1.wav", 0, S_UpdateContentSounds);
cvar_t sfx_wind ("sfx_wind", "ambience/wind2.wav", 0, S_UpdateContentSounds);
cvar_t sfx_slime ("sfx_slime","misc/null.wav", 0, S_UpdateContentSounds);
cvar_t sfx_lava ("sfx_lava","misc/null.wav", 0, S_UpdateContentSounds);


void S_UpdateContentSounds (cvar_t *unused)
{
	S_StopAllSounds (true);

	if (sfx_water.string) {ambient_sfx[AMBIENT_WATER] = S_PrecacheSound (sfx_water.string);}
	if (sfx_wind.string) {ambient_sfx[AMBIENT_SKY] = S_PrecacheSound (sfx_wind.string);}
	if (sfx_slime.string) {ambient_sfx[AMBIENT_SLIME] = S_PrecacheSound (sfx_slime.string);}
	if (sfx_lava.string) {ambient_sfx[AMBIENT_LAVA] = S_PrecacheSound (sfx_lava.string);}
}


// ====================================================================
// User-setable variables
// ====================================================================


void S_AmbientOff (void)
{
	snd_ambient = false;
}


void S_AmbientOn (void)
{
	snd_ambient = true;
}


void S_SoundInfo_f (void)
{
	if (!sound_started || !shm)
	{
		Con_Printf ("sound system not started\n");
		return;
	}

	Con_Printf ("%5d samples\n", shm->samples);
	Con_Printf ("%5d samplepos\n", shm->samplepos);
	Con_Printf ("%5d submission_chunk\n", shm->submission_chunk);
	Con_Printf ("%5d speed\n", shm->speed);
	Con_Printf ("0x%x dma buffer\n", shm->buffer);
	Con_Printf ("%5d total_channels\n", total_channels);
}


/*
================
S_Startup
================
*/

void S_Startup (void)
{
	int	rc;

	if (!snd_initialized)
		return;

	if ((rc = SNDDMA_Init ()) == NULL)
	{
		sound_started = 0;
		return;
	}

	sound_started = 1;
}


/*
================
S_Init
================
*/
cmd_t S_Play_Cmd ("play", S_Play);
cmd_t S_Play2_Cmd ("play2", S_Play2);
cmd_t S_PlayVol_Cmd ("playvol", S_PlayVol);
cmd_t S_StopAllSoundsC_Cmd ("stopsound", S_StopAllSoundsC);
cmd_t S_SoundList_Cmd ("soundlist", S_SoundList);
cmd_t S_SoundInfo_f_Cmd ("soundinfo", S_SoundInfo_f);

void S_Init (void)
{
	// always init this otherwise we'll crash during sound clearing
	S_ClearSounds ();

	if (COM_CheckParm ("-nosound"))
		return;

	Con_Printf ("Sound Initialization\n");
	snd_initialized = true;
	S_Startup ();
	Con_Printf ("Sound sampling rate: %i\n", shm->speed);
	S_StopAllSounds (true);
}


void S_InitAmbients (void)
{
	S_UpdateContentSounds (NULL);
}



// =======================================================================
// Shutdown sound engine
// =======================================================================

void S_Shutdown (void)
{
	if (!sound_started) return;
	if (shm) shm->gamealive = 0;

	shm = 0;
	sound_started = 0;

	SNDDMA_Shutdown();
}


// =======================================================================
// Load a sound
// =======================================================================

/*
==================
S_FindName

==================
*/
sfx_t *S_FindName (char *name)
{
	if (!name)
	{
		Sys_Error ("S_FindName: NULL\n");
		return NULL;
	}

	if (strlen (name) >= MAX_QPATH)
	{
		Sys_Error ("Sound name too long: %s", name);
		return NULL;
	}

	// see if already loaded
	int slot = -1;

	for (int i = 0; i < known_sfx.size (); i++)
	{
		if (!known_sfx[i])
		{
			// this is a reusable slot
			slot = i;
			continue;
		}

		if (!strcmp (known_sfx[i]->name, name))
		{
			known_sfx[i]->sndcache = NULL;
			return known_sfx[i];
		}
	}

	// alloc a new SFX
	sfx_t *sfx = (sfx_t *) MainHunk->Alloc (sizeof (sfx_t));

	if (slot >= 0)
		known_sfx[slot] = sfx;
	else known_sfx.push_back (sfx);

	strcpy (sfx->name, name);
	sfx->sndcache = NULL;

	return sfx;
}


void S_ClearSounds (void)
{
	// on hunk so it doesn't need a free
	// be sure to call this and MainHunk->FreeToLowMark (0) together!!!!
	known_sfx.clear ();
}


/*
==================
S_TouchSound

==================
*/
void S_TouchSound (char *name)
{
	if (!sound_started)
		return;

	sfx_t *sfx = S_FindName (name);
	sfx->sndcache = NULL;
}


/*
==================
S_PrecacheSound

==================
*/
sfx_t *S_PrecacheSound (char *name)
{
	if (!sound_started || nosound.value)
		return NULL;

	// find the name for it and set it's initial cache to null
	sfx_t *sfx = S_FindName (name);
	sfx->sndcache = NULL;

	// cache it in
	if (precache.value)
		S_LoadSound (sfx);

	return sfx;
}


//=============================================================================

/*
=================
SND_PickChannel
=================
*/
channel_t *SND_PickChannel (int entnum, int entchannel)
{
	// Check for replacement sound, or find the best one to replace
	int first_to_die = -1;
	int life_left = 0x7fffffff;

	for (int ch_idx = FIRST_DYNAMIC_CHANNEL; ch_idx < FIRST_STATIC_CHANNEL; ch_idx++)
	{
		// channel 0 never overrides
		if (entchannel != 0 && channels[ch_idx].entnum == entnum && (channels[ch_idx].entchannel == entchannel || entchannel == -1))
		{
			// allways override sound from same entity
			first_to_die = ch_idx;
			break;
		}

		// don't let monster sounds override player sounds
		if (channels[ch_idx].entnum == cl.viewentity && entnum != cl.viewentity && channels[ch_idx].sfx)
			continue;

		if (channels[ch_idx].end - paintedtime < life_left)
		{
			life_left = channels[ch_idx].end - paintedtime;
			first_to_die = ch_idx;
		}
	}

	if (first_to_die == -1)
		return NULL;

	if (channels[first_to_die].sfx)
		channels[first_to_die].sfx = NULL;

	return &channels[first_to_die];
}

/*
=================
SND_Spatialize
=================
*/
void SND_Spatialize (channel_t *ch)
{
	// anything coming from the view entity will allways be full volume
	if (ch->entnum == cl.viewentity || !(ch->dist_mult > 0))
	{
		ch->leftvol = ch->master_vol;
		ch->rightvol = ch->master_vol;
	}
	else
	{
		// base sound origin
		float sound_origin[3] =
		{
			ch->origin[0],
			ch->origin[1],
			ch->origin[2]
		};

		if (ch->entnum > 0 && cls.state == ca_connected)
		{
			entity_t *ent = cls.entities[ch->entnum];

			if (ent && ent->model)
			{
				// update sound origin
				// brush model entities have their origins at 0|0|0 and move relative to that; other models are positioned correctly
				if (ent->model->type == mod_brush)
					D3DMisc_PositionFromBBox (sound_origin, ent->cullinfo.mins, ent->cullinfo.maxs);
				else Vector3Copy (sound_origin, ent->origin);
			}
		}

		// calculate stereo seperation and distance attenuation
		float source_vec[3] =
		{
			sound_origin[0] - listener_origin[0],
			sound_origin[1] - listener_origin[1],
			sound_origin[2] - listener_origin[2]
		};

		float dist = Vector3Normalize (source_vec) * ch->dist_mult;
		float dot = Vector3Dot (listener_right, source_vec);
		float rscale = 1.0f + dot;
		float lscale = 1.0f - dot;

		// add in distance effect
		ch->rightvol = (int) (ch->master_vol * ((1.0f - dist) * rscale));
		ch->leftvol = (int) (ch->master_vol * ((1.0f - dist) * lscale));
	}

	if (ch->rightvol < 0) ch->rightvol = 0;
	if (ch->rightvol > 255) ch->rightvol = 255;
	if (ch->leftvol < 0) ch->leftvol = 0;
	if (ch->leftvol > 255) ch->leftvol = 255;
}


// =======================================================================
// Start a sound effect
// =======================================================================

void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation)
{
	if (!sound_started) return;
	if (!sfx) return;
	if (nosound.value) return;

	int vol = fvol * 255;

	// pick a channel to play on
	channel_t *target_chan = SND_PickChannel (entnum, entchannel);

	// didn't get one
	if (!target_chan) return;

	// spatialize
	memset (target_chan, 0, sizeof (channel_t));

	Vector3Copy (target_chan->origin, origin);

	target_chan->dist_mult = attenuation / sound_nominal_clip_dist.value;
	target_chan->master_vol = vol;
	target_chan->entnum = entnum;
	target_chan->entchannel = entchannel;

	SND_Spatialize (target_chan);

	// not audible at all
	if (target_chan->leftvol < 1 || target_chan->rightvol < 1) return;

	sfxcache_t	*sc = NULL;

	// new channel
	if (!(sc = S_LoadSound (sfx)))
	{
		// couldn't load the sound's data
		target_chan->sfx = NULL;
		return;
	}

	target_chan->sfx = sfx;
	target_chan->pos = 0.0;
	target_chan->end = paintedtime + sc->length;

	// if an identical sound has also been started this frame, offset the pos
	// a bit to keep it from just making the first one louder
	channel_t *check = &channels[FIRST_DYNAMIC_CHANNEL];

	for (int ch_idx = FIRST_DYNAMIC_CHANNEL; ch_idx < FIRST_STATIC_CHANNEL; ch_idx++, check++)
	{
		if (check == target_chan)
			continue;

		if (check->sfx == sfx && !check->pos)
		{
			int skip = Q_fastrand () % (int) (0.1 * shm->speed);

			if (skip >= target_chan->end)
				skip = target_chan->end - 1;

			target_chan->pos += skip;
			target_chan->end -= skip;
			break;
		}
	}
}

void S_StopSound (int entnum, int entchannel)
{
	// bugfix - channels 0 .. FIRST_DYNAMIC_CHANNEL are ambient channels so don't try to stop them
	// this also successfully stops sounds towards the end of the channels list
	for (int i = FIRST_DYNAMIC_CHANNEL; i < FIRST_STATIC_CHANNEL; i++)
	{
		// must be a better way of getting the channel used by an entity...
		if (channels[i].entnum == entnum && channels[i].entchannel == entchannel)
		{
			channels[i].end = 0;
			channels[i].sfx = NULL;
			return;
		}
	}
}


void CDAudio_Stop (void);

void S_StopAllSounds (bool clear)
{
	// stop these as well
	CDAudio_Stop ();

	if (!sound_started)
		return;

	total_channels = FIRST_STATIC_CHANNEL;	// no statics

	for (int i = 0; i < MAX_CHANNELS; i++)
	{
		channels[i].end = 0;
		channels[i].sfx = NULL;
	}

	memset (channels, 0, MAX_CHANNELS * sizeof (channel_t));

	if (clear) S_ClearBuffer ();
}

void S_StopAllSoundsC (void)
{
	S_StopAllSounds (true);
}


void S_ClearBuffer (void)
{
	if (!sound_started || !shm || (!shm->buffer && !ds_SecondaryBuffer8))
		return;

	DWORD	dwSize;
	DWORD	*pData;

	if (!S_GetBufferLock (0, ds_SoundBufferSize, (LPVOID *) &pData, &dwSize, NULL, NULL, 0)) return;

	memset (pData, 0, dwSize);
	ds_SecondaryBuffer8->Unlock (pData, dwSize, NULL, 0);
}


/*
=================
S_StaticSound
=================
*/
void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
	channel_t *ss;
	sfxcache_t *sc;

	if (!sfx) return;

	if (total_channels == MAX_CHANNELS)
	{
		Con_Printf ("total_channels == MAX_CHANNELS\n");
		return;
	}

	ss = &channels[total_channels];
	total_channels++;

	if (!(sc = S_LoadSound (sfx))) return;

	if (sc->loopstart == -1)
	{
		Con_Printf ("Sound %s not looped\n", sfx->name);
		return;
	}

	ss->sfx = sfx;
	Vector3Copy (ss->origin, origin);
	ss->master_vol = vol;
	ss->dist_mult = (attenuation / 64) / sound_nominal_clip_dist.value;
	ss->end = paintedtime + sc->length;

	SND_Spatialize (ss);
}


//=============================================================================

/*
===================
S_UpdateAmbientSounds
===================
*/
void S_UpdateAmbientSounds (double frametime)
{
	// calc ambient sound levels
	if (!snd_ambient) return;
	if (!cl.worldmodel) return;
	if (!cls.maprunning) return;

	mleaf_t *l = Mod_PointInLeaf (listener_origin, cl.worldmodel);

	if (!l || !ambient_level.value)
	{
		for (int ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++)
			channels[ambient_channel].sfx = NULL;

		return;
	}

	for (int ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++)
	{
		channel_t *chan = &channels[ambient_channel];
		float vol = 0;

		chan->sfx = ambient_sfx[ambient_channel];

		// was < 8 but we'll always keep some ambience
		if ((vol = ambient_level.value * l->ambient_sound_level[ambient_channel]) < 0)
			vol = 0;

		// don't adjust volume too fast
		// this is kinda shit because accumulating from fp to int will cause drop-off - can we not do a weighted mix over time instead???
		if (chan->master_vol < vol)
		{
			chan->master_vol += frametime * ambient_fade.value;

			if (chan->master_vol > vol)
				chan->master_vol = vol;
		}
		else if (chan->master_vol > vol)
		{
			chan->master_vol -= frametime * ambient_fade.value;

			if (chan->master_vol < vol)
				chan->master_vol = vol;
		}

		chan->leftvol = chan->rightvol = chan->master_vol;
	}
}


/*
============
S_Update

Called once each time through the main loop
============
*/
struct s_threadcontext_t
{
	double frametime;
	vec3_t origin;
	vec3_t forward;
	vec3_t right;
	vec3_t up;
};


void S_Update (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
	int			j;

	// start at the value of realtime when first called
	static double last_soundtime = CHostTimer::realtime;
	double frametime = CHostTimer::realtime - last_soundtime;

	last_soundtime = CHostTimer::realtime;

	if (!sound_started || (snd_blocked > 0)) return;

	Vector3Copy (listener_origin, origin);
	Vector3Copy (listener_forward, forward);
	Vector3Copy (listener_right, right);
	Vector3Copy (listener_up, up);

	// update general area ambient sound sources
	S_UpdateAmbientSounds (frametime);

	channel_t *combine = NULL;

	// update spatialization for static and dynamic sounds
	channel_t *ch = channels + FIRST_DYNAMIC_CHANNEL;

	for (int i = FIRST_DYNAMIC_CHANNEL; i < total_channels; i++, ch++)
	{
		// no sound in this channel
		if (!ch->sfx) continue;

		// respatialize channel
		SND_Spatialize (ch);

		// no volume
		if (ch->leftvol < 1 || ch->rightvol < 1) continue;

		// try to combine static sounds with a previous channel of the same
		// sound effect so we don't mix five torches every frame
		if (i < FIRST_STATIC_CHANNEL) continue;

		// see if it can just use the last one
		if (combine && combine->sfx == ch->sfx)
		{
			combine->leftvol += ch->leftvol;
			combine->rightvol += ch->rightvol;
			ch->leftvol = ch->rightvol = 0;
			continue;
		}

		// search for one
		combine = channels + FIRST_STATIC_CHANNEL;

		for (j = FIRST_STATIC_CHANNEL; j < i; j++, combine++)
			if (combine->sfx == ch->sfx)
				break;

		if (j == total_channels)
			combine = NULL;
		else
		{
			if (combine != ch)
			{
				combine->leftvol += ch->leftvol;
				combine->rightvol += ch->rightvol;
				ch->leftvol = ch->rightvol = 0;
			}
		}
	}

	// debugging output
	if (snd_show.value)
	{
		int total = 0;
		channel_t *ch = channels;

		for (int i = 0; i < total_channels; i++, ch++)
		{
			if (ch->sfx && (ch->leftvol > 0 || ch->rightvol > 0))
			{
				Con_Printf ("%3i %3i %s\n", ch->leftvol, ch->rightvol, ch->sfx->name);
				total++;
			}
		}

		Con_Printf ("----(%i)----\n", total);
	}

	// mix some sound
	S_Update_ ();
}


void GetSoundtime (void)
{
	int		samplepos;
	static	int		buffers;
	static	int		oldsamplepos;
	int		fullsamples;

	// 2 channels
	fullsamples = shm->samples >> 1;

	// it is possible to miscount buffers if it has wrapped twice between
	// calls to S_Update.  Oh well.
	samplepos = SNDDMA_GetDMAPos ();

	if (samplepos < oldsamplepos)
	{
		buffers++;					// buffer wrapped

		if (paintedtime > 0x40000000)
		{
			// time to chop things off to avoid 32 bit limits
			buffers = 0;
			paintedtime = fullsamples;
			S_StopAllSounds (true);
		}
	}

	oldsamplepos = samplepos;

	// 2 channels
	soundtime = buffers * fullsamples + (samplepos >> 1);
}


void S_ExtraUpdate (void)
{
}


void S_Update_ (void)
{
	unsigned        endtime;
	int				samps;

	if (!sound_started || (snd_blocked > 0)) return;

	// Updates DMA time
	GetSoundtime ();

	// check to make sure that we haven't overshot
	if (paintedtime < soundtime)
	{
		//Con_Printf ("S_Update_ : overflow\n");
		paintedtime = soundtime;
	}

	// mix ahead of current position
	endtime = soundtime + _snd_mixahead.value * shm->speed;

	// 2 channels
	samps = shm->samples >> 1;

	if (endtime - soundtime > samps)
		endtime = soundtime + samps;

	// if the buffer was lost or stopped, restore it and/or restart it
	DWORD dwStatus;

	if (ds_SecondaryBuffer8)
	{
		if (ds_SecondaryBuffer8->GetStatus (&dwStatus) != DD_OK) Con_Printf ("Couldn't get sound buffer status\n");
		if (dwStatus & DSBSTATUS_BUFFERLOST) ds_SecondaryBuffer8->Restore ();
		if (!(dwStatus & DSBSTATUS_PLAYING)) ds_SecondaryBuffer8->Play (0, 0, DSBPLAY_LOOPING);
	}

	S_PaintChannels (endtime);
}

/*
===============================================================================

console functions

===============================================================================
*/

static void S_PlayGen (int *hash, float attn)
{
	int 	i;
	char	name[256];
	sfx_t	*sfx;

	i = 1;

	while (i < Cmd_Argc ())
	{
		if (!strrchr (Cmd_Argv (i), '.'))
		{
			strcpy (name, Cmd_Argv (i));
			strcat (name, ".wav");
		}
		else strcpy (name, Cmd_Argv (i));

		sfx = S_PrecacheSound (name);
		S_StartSound ((*hash)++, 0, sfx, listener_origin, 1.0, attn);
		i++;
	}
}

void S_Play (void)
{
	static int hash = 345;

	S_PlayGen (&hash, 1);
}

void S_Play2 (void)
{
	static int hash = 345;

	S_PlayGen (&hash, 0);
}

void S_PlayVol (void)
{
	static int hash = 543;
	int i;
	float vol;
	char name[256];
	sfx_t	*sfx;

	i = 1;

	while (i < Cmd_Argc ())
	{
		if (!strrchr (Cmd_Argv (i), '.'))
		{
			strcpy (name, Cmd_Argv (i));
			strcat (name, ".wav");
		}
		else strcpy (name, Cmd_Argv (i));

		sfx = S_PrecacheSound (name);
		vol = atof (Cmd_Argv (i + 1));
		S_StartSound (hash++, 0, sfx, listener_origin, vol, 1.0);
		i += 2;
	}
}

void S_SoundList (void)
{
	int		i;
	sfx_t	*sfx;
	sfxcache_t	*sc;
	int		size, total;

	total = 0;

	for (i = 0; i < known_sfx.size (); i++)
	{
		if (!(sfx = known_sfx[i])) continue;
		if (!(sc = sfx->sndcache)) continue;

		size = sc->length * 2 * (sc->stereo + 1);
		total += size;

		if (sc->loopstart >= 0)
			Con_Printf ("L");
		else Con_Printf (" ");

		Con_Printf ("(%2db) %6i : %s\n", 16, size, sfx->name);
	}

	Con_Printf ("Total resident: %i\n", total);
}


void S_LocalSound (char *sound)
{
	int		i;
	sfx_t	*sfx;

	if (nosound.value) return;
	if (!sound_started) return;

	// look for a cached version
	for (i = 0; i < known_sfx.size (); i++)
	{
		if (!(sfx = known_sfx[i])) continue;
		if (!sfx->sndcache) continue;

		// matching name
		if (!_stricmp (sound, sfx->name))
		{
			// play the sound we got
			S_StartSound (cl.viewentity, -1, sfx, vec3_origin, 1, 1);
			return;
		}
	}

	// not cached
	if ((sfx = S_PrecacheSound (sound)) != NULL)
	{
		// play the sound we got
		S_StartSound (cl.viewentity, -1, sfx, vec3_origin, 1, 1);
	}
	else Con_Printf ("S_LocalSound: can't cache %s\n", sound);
}


void S_ClearPrecache (void)
{
}


void S_BeginPrecaching (void)
{
}


void S_EndPrecaching (void)
{
}

