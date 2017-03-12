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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "d3d_model.h"
#include "winquake.h"
#include "d3d_quake.h"
#include "pr_class.h"

#include <setjmp.h>


jmp_buf host_abortserver;

void COM_ShutdownFileSystem (void);

/*

A server can allways be started, even if the system started out as a client
to a remote system.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t host_parms;

bool	host_initialized;		// true if into command execution

client_t	*host_client;			// current client

cvar_t	sys_ticrate ("sys_ticrate", "0.05");

cvar_t	fraglimit ("fraglimit", "0", CVAR_SERVER);
cvar_t	timelimit ("timelimit", "0", CVAR_SERVER);
cvar_t	teamplay ("teamplay", "0", CVAR_SERVER);

cvar_t	samelevel ("samelevel", "0");
cvar_t	noexit ("noexit", "0", CVAR_SERVER);

cvar_t	developer ("developer", "0");

cvar_t	skill ("skill", "1", CVAR_INTERNAL);						// 0 - 3
cvar_t	deathmatch ("deathmatch", "0");			// 0, 1, or 2
cvar_t	coop ("coop", "0");			// 0 or 1

cvar_t	pausable ("pausable", "1");

cvar_t	temp1 ("temp1", "0");
cvar_t	temp2 ("temp2", "0");
cvar_t	temp3 ("temp3", "0");
cvar_t	temp4 ("temp4", "0");

// reserve space for all clients.  these need to be kept in static memory so that the addresses
// of member variables remain valid during level transitions/etc
client_t host_svsclients[MAX_SCOREBOARD];

void Host_SafeWipeClient (client_t *client)
{
	// copy out anything that uses a pointer in the client_t struct
	byte *msgbuf = client->msgbuf;
	float *ping_times = client->ping_times;
	float *spawn_parms = client->spawn_parms;

	// wipe the contents of what we copied out
	if (msgbuf) memset (msgbuf, 0, MAX_MSGLEN);
	if (ping_times) memset (ping_times, 0, sizeof (float) * NUM_PING_TIMES);
	if (spawn_parms) memset (spawn_parms, 0, sizeof (float) * NUM_SPAWN_PARMS);

	// now we can safely wipe the struct
	memset (client, 0, sizeof (client_t));

	// and now we restore what we copied out
	client->msgbuf = msgbuf;
	client->ping_times = ping_times;
	client->spawn_parms = spawn_parms;
}


void Host_InitClients (int numclients)
{
	client_t *client = host_svsclients;

	for (int i = 0; i < MAX_SCOREBOARD; i++, client++)
	{
		// safely wipe the client
		Host_SafeWipeClient (client);

		if (i < numclients)
		{
			if (!client->ping_times)
				client->ping_times = (float *) MainZone->Alloc (sizeof (float) * NUM_PING_TIMES);
			else memset (client->ping_times, 0, sizeof (float) * NUM_PING_TIMES);

			if (!client->spawn_parms)
				client->spawn_parms = (float *) MainZone->Alloc (sizeof (float) * NUM_SPAWN_PARMS);
			else memset (client->spawn_parms, 0, sizeof (float) * NUM_SPAWN_PARMS);

			// this is going to be an active client so set up memory/etc for it
			if (!client->msgbuf)
				client->msgbuf = (byte *) MainZone->Alloc (MAX_MSGLEN);
			else memset (client->msgbuf, 0, MAX_MSGLEN);

			// set up the new message buffer correctly (this is just harmless paranoia as it's also done in SV_ConnectClient)
			client->message.data = client->msgbuf;
		}
		else
		{
			// this is an inactive client so release memory/etc
			if (client->msgbuf)
			{
				MainZone->Free (client->msgbuf);
				client->msgbuf = NULL;
			}

			if (client->ping_times)
			{
				MainZone->Free (client->ping_times);
				client->ping_times = NULL;
			}

			if (client->spawn_parms)
			{
				MainZone->Free (client->spawn_parms);
				client->spawn_parms = NULL;
			}
		}
	}
}


/*
================
Host_EndGame
================
*/
void Host_EndGame (char *message, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr, message);
	_vsnprintf (string, 1024, message, argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n", string);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.demonum != -1)
		CL_NextDemo ();
	else CL_Disconnect ();

	longjmp (host_abortserver, 1);
}


/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	static	bool inerror = false;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");

	inerror = true;

	SCR_EndLoadingPlaque ();		// reenable screen updates

	va_start (argptr, error);
	_vsnprintf (string, 1024, error, argptr);
	va_end (argptr);

	Con_Printf ("Host_Error: %s\n", string);
	QC_DebugOutput ("Host_Error: %s\n", string);

	if (sv.active)
		Host_ShutdownServer (false);

	CL_Disconnect ();
	cls.demonum = -1;

	inerror = false;

	longjmp (host_abortserver, 1);
}


/*
================
Host_FindMaxClients
================
*/
void Host_FindMaxClients (void)
{
	int		i;

	svs.maxclients = 1;

	// initially disconnected
	cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");

	// check for a listen server
	if (i)
	{
		if (i != (com_argc - 1))
			svs.maxclients = atoi (com_argv[i + 1]);
		else svs.maxclients = MAX_SCOREBOARD;
	}

	// don't let silly values go in
	if (svs.maxclients < 1)
		svs.maxclients = 1;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	// allocate space for the initial clients
	svs.clients = host_svsclients;
	Host_InitClients (svs.maxclients);

	// if we request more than 1 client we set the appropriate game mode
	if (svs.maxclients > 1)
		deathmatch.Set (1.0);
	else deathmatch.Set (0.0);
}


/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Host_InitCommands ();
	Host_FindMaxClients ();
}


/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars
===============
*/
void Key_HistoryFlush (void);
void Key_WriteBindings (std::ofstream &f);
void Cmd_WriteAlias (std::ofstream &f);
void D3DState_SaveTextureMode (std::ofstream &f);

void Host_WriteConfiguration (void)
{
	if (host_initialized)
	{
		std::ofstream cfgfile (va ("%s/directq.cfg", com_gamedir));

		if (cfgfile.is_open ())
		{
			Key_WriteBindings (cfgfile);
			cvar_t::WriteVariables (cfgfile);
			Cmd_WriteAlias (cfgfile);
			D3DState_SaveTextureMode (cfgfile);
			cfgfile.close ();
			Con_SafePrintf ("Wrote directq.cfg\n");
		}

		Key_HistoryFlush ();
	}
}


cmd_t Host_WriteConfiguration_Cmd ("Host_WriteConfiguration", Host_WriteConfiguration);

/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		string[2048];

	va_start (argptr, fmt);
	_vsnprintf (string, 2048, fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	int			i;

	va_start (argptr, fmt);
	_vsnprintf (string, 1023, fmt, argptr);
	va_end (argptr);

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
	}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (client_t *client, char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr, fmt);
	_vsnprintf (string, 1023, fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, string);
}


/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (bool crash)
{
	int		saveSelf;
	int		i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			saveSelf = SVProgs->GlobalStruct->self;
			SVProgs->RunInteraction (host_client->edict, NULL, SVProgs->GlobalStruct->ClientDisconnect);
			SVProgs->GlobalStruct->self = saveSelf;
		}
	}

	// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

	// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	// send notification to all clients
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->active)
			continue;

		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer (bool crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	char		message[4];

	if (!sv.active)
		return;

	sv.active = false;

	// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

	// flush any pending messages - like the score!!!
	double dStart = Sys_DoubleTime ();

	do
	{
		count = 0;

		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage (host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
					count++;
				}
			}
		}

		if ((Sys_DoubleTime () - dStart) > 3) break;
	} while (count);

	// make sure all the clients know we're disconnecting
	buf.data = (byte *) message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte (&buf, svc_disconnect);
	count = NET_SendToAll (&buf, 5);

	if (count)
		Con_Printf ("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		if (host_client->active)
			SV_DropClient (crash);

	// safely wipe the client_t structs
	for (i = 0; i < svs.maxclients; i++)
		Host_SafeWipeClient (&svs.clients[i]);

	// clear structures
	memset (&sv, 0, sizeof (sv));
}


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void CL_ClearCLStruct (void);

void Host_ClearMemory (void)
{
	// clear anything that needs to be cleared specifically
	S_StopAllSounds (true);
	Mod_ClearAll ();

	cls.signon = 0;

	// decommit all sounds
	S_ClearSounds ();

	// decommit all memory
	TempHunk->Free ();
	MainHunk->Free ();

	// and defrag the process heap
	HeapCompact (GetProcessHeap (), 0);

	// wipe the client and server structs
	memset (&sv, 0, sizeof (sv));
	CL_ClearCLStruct ();
}


//============================================================================


cvar_t host_maxfps ("host_maxfps", 72.0f, CVAR_ARCHIVE | CVAR_SERVER);
cvar_t sv_maxfps ("sv_maxfps", 72.0f, CVAR_SERVER);

cvar_alias_t cl_maxfps ("cl_maxfps", &host_maxfps);
cvar_alias_t pq_maxfps ("pq_maxfps", &host_maxfps);

void CL_SendLagMove (void);
void SCR_SetTimeout (float timeout);


// stated intention
// host_maxfps should only affect the renderer
// everything else should run at either full tilt or at the scaled back server time
// decoupled timers becomes the only mode
cvar_t host_framerate ("host_framerate", "0");
cvar_t host_timescale ("host_timescale", "0");
cvar_t host_speeds ("host_speeds", "0");			// set for running times

#define FRAME_DELTA (1.0 / sv_maxfps.value)

double CHostTimer::realtime = 0;

CHostTimer::CHostTimer (void)
{
	this->next = this->last = 0;
	this->delta = 0;
	this->runframe = false;
}

void CHostTimer::Reset (void)
{
	this->next = this->last = CHostTimer::realtime;
	this->delta = 0;
	this->runframe = true;
}

void CHostTimer::Tick (double standarddelta, double idledelta, bool userdelta)
{
	if (CHostTimer::realtime > this->next)
	{
		this->delta = CHostTimer::realtime - this->last;
		this->last = CHostTimer::realtime;

		if (this->delta < 0)
		{
			// prevent frames with negative delta time
			this->runframe = false;
			this->delta = 0.0;
		}
		else
		{
			double nextdelta = 0;

			// ugh; gnarly code
			if (cls.download.web && idledelta > 0)
				nextdelta = 0;
			else if ((cls.timedemo || vid.syncinterval) && idledelta > 0)
				nextdelta = 0;
			else if (!cls.maprunning && !cls.demoplayback && idledelta > 0)
				nextdelta = idledelta;
			else if (userdelta)
			{
				// expected behaviour is that 0 is uncapped; precedent: Q3A
				if (host_maxfps.value > 0)
				{
					if (host_maxfps.value > 1000)
						nextdelta = 0;
					else if (host_maxfps.value < 10)
						nextdelta = 0.1;
					else nextdelta = (1.0 / (double) host_maxfps.value);
				}
				else nextdelta = 0;
			}
			else nextdelta = standarddelta;

			// don't allow too-long frames
			if (this->delta > 0.1) this->delta = 0.1;

			// allow players to scale the delta time for demo slo-mo, etc
			if (host_framerate.value > 0) this->delta = host_framerate.value;
			if (host_timescale.value > 0) this->delta *= host_timescale.value;

			this->next += nextdelta;
			this->runframe = true;
		}
	}
	else
	{
		this->runframe = false;
		this->delta = 0.0;
	}
}


static CHostTimer host_servertime;
static CHostTimer host_clienttime;
static CHostTimer host_rendertime;


void Host_ResetTimers (void)
{
	host_servertime.Reset ();
	host_clienttime.Reset ();
	host_rendertime.Reset ();
}


void IN_ReadInputMessages (void);
void IN_ReadJoystickMessages (void);


void Host_SpeedsTime (double *time)
{
	if (host_speeds.value)
		time[0] = Sys_DoubleTime ();
	else time[0] = 0;
}


void Host_Frame (void)
{
	// release any temp memory used this frame (CL_UpdateTEnts and others depend on this)
	TempHunk->FreeToLowMark (0);

	if (sv_maxfps.value < 10) sv_maxfps.Set (10.0f);

	double hs_startframe = 0;
	double hs_startserver = 0;
	double hs_startrenderer = 0;
	double hs_startsound = 0;
	double hs_endframe = 0;

	// something bad happened, or the server disconnected
	if (setjmp (host_abortserver))
	{
		// memory may have been allocated which won't get free'ed until the next frame, so free it now
		TempHunk->FreeToLowMark (0);
		return;
	}

	// keep the random time dependent
	// (unless we're in a demo - see srand call in cl_demo - in which case we keep random effects consistent across playbacks)
	if (!cls.demoplayback) Q_fastrand ();

	// decide if we're going to run a frame
	// this is a properly accumulating timer as an offset from the time the engine started at to keep things steady
	CHostTimer::realtime = Sys_DoubleTime ();

	Host_SpeedsTime (&hs_startframe);

	// run our timers
	// the server normally ticks at FRAME_DELTA intervals and has no scaled-back tick
	// the client normally ticks at 250fps and may scale back to FRAME_DELTA intervals
	// the renderer ticks at FRAME_DELTA intervals in both normal and scaled-back mode but may be overridden by host_maxfps
	host_servertime.Tick (FRAME_DELTA, 0, false);
	host_clienttime.Tick (0.004, FRAME_DELTA, false);
	host_rendertime.Tick (FRAME_DELTA, FRAME_DELTA, (!cl.paused && (cl.maxclients > 1 || key_dest == key_game)));

	// get new events (deferred to here so that they'll have the correct value of realtime)
	Sys_SendKeyEvents ();

	// read input and update keystates, moves, etc
	if (!cls.demoplayback)
	{
		IN_ReadInputMessages ();
		IN_ReadJoystickMessages ();
		IN_Commands ();
	}

	Host_SpeedsTime (&hs_startserver);

	if ((sv.active && host_clienttime.runframe) || (!sv.active && host_servertime.runframe))
	{
		Cbuf_Execute ();
		NET_Poll ();
	}

	if (sv.active && (host_servertime.runframe))
	{
		CL_SendCmd (host_servertime.delta);
		SV_UpdateServer (host_servertime.delta);
	}
	else if (!sv.active && (host_servertime.runframe))
		CL_SendCmd (host_servertime.delta);

	Host_SpeedsTime (&hs_startrenderer);

	// the client must be updated every frame; if there is nothing in the message buffer then the client won't update
	CL_UpdateClient (host_clienttime.delta);

	// only update the display if a render frame should run (keeps fps counter behaving as expected)
	if (host_rendertime.runframe) SCR_UpdateScreen ();

	Host_SpeedsTime (&hs_startsound);

	// because sounds are CPU intensive we only update them at the same rate as the server
	if (host_servertime.runframe)
	{
		// update sounds
		if (cls.signon == SIGNON_CONNECTED)
			S_Update (r_refdef.vieworigin, r_viewvectors.fw, r_viewvectors.rt, r_viewvectors.up);
		else S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

		// finish sound
		CDAudio_Update ();
		MediaPlayer_Update ();
	}

	Host_SpeedsTime (&hs_endframe);

	if (host_speeds.value && key_dest == key_game)
	{
		// because we run so fast we need to measure in microseconds
		Con_Printf ("init %i  server %i  renderer %i  sound %i\n",
			(int) ((hs_startserver - hs_startframe) * 1000000.0),
			(int) ((hs_startrenderer - hs_startserver) * 1000000.0),
			(int) ((hs_startsound - hs_startrenderer) * 1000000.0),
			(int) ((hs_endframe - hs_startsound) * 1000000.0));
	}
}


//============================================================================

/*
====================
Host_Init
====================
*/
void VIDD3D_Init (void);
bool full_initialized = false;
void Menu_CommonInit (void);
void PR_InitBuiltIns (void);
void Menu_MapsPopulate (void);
void Menu_DemoPopulate (void);
void Menu_LoadAvailableSkyboxes (void);
void SCR_QuakeIsLoading (int stage, int maxstage);


void Host_Init (quakeparms_t *parms)
{
	// hold back some things until we're fully up
	full_initialized = false;

	Q_MemCpy (&host_parms, parms, sizeof (quakeparms_t));

	com_argc = parms->argc;
	com_argv = parms->argv;

	// initially seed the generator
	Q_randseed ((int) (Sys_DoubleTime () * 1000000.0));

	Cbuf_Init ();
	Cmd_Init ();
	V_Init ();
	Chase_Init ();
	COM_Init (parms->basedir);

	// as soon as the filesystem comes up we want to load the configs so that cvars will have correct
	// values before we proceed with anything else.  this is possible to do as we've made our cvars
	// self-registering, so we don't need to worry about subsequent registrations or cvars that don't exist.
	COM_ExecQuakeRC ();

	// execute immediately rather than deferring
	Cbuf_Execute ();

	Host_InitLocal ();

	if (!gfxwad.Load ("gfx.wad")) Sys_Error ("Host_Init : Could not locate Quake on your computer\nFailed to load gfx.wad");

	Key_Init ();
	Con_Init ();
	Menu_CommonInit ();
	Menu_MapsPopulate ();
	Menu_DemoPopulate ();
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();
	IPLog_Init ();	// JPG 1.05 - ip address logging

	Con_SafePrintf ("Exe: "__TIME__" "__DATE__"\n");

	VIDD3D_Init ();

	R_Init (); SCR_QuakeIsLoading (1, 6);
	S_Init (); SCR_QuakeIsLoading (2, 6);
	MediaPlayer_Init (); SCR_QuakeIsLoading (3, 6);
	CDAudio_Init (); SCR_QuakeIsLoading (4, 6);
	CL_Init (); SCR_QuakeIsLoading (5, 6);
	IN_Init (); SCR_QuakeIsLoading (6, 6);

	// everythings up now
	full_initialized = true;

	COM_ExecQuakeRC ();

	// anything allocated after this point will be cleared between maps
	host_initialized = true;

	// cvars are now initialized
	cvar_t::initialized = true;

	UpdateTitlebarText ();
}


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_ShutdownGame (void)
{
	// keep Con_Printf from trying to update the screen
	Host_DisableForLoading (true);

	Host_WriteConfiguration ();
	IPLog_WriteLog ();	// JPG 1.05 - ip loggging

	CDAudio_Shutdown ();
	MediaPlayer_Shutdown ();
	NET_Shutdown ();
	S_Shutdown();
	IN_Shutdown ();
	D3DVid_ShutdownVideo ();
	COM_ShutdownFileSystem ();
}


void Host_Shutdown (void)
{
	static bool isdown = false;

	if (isdown) return;

	isdown = true;

	Host_ShutdownGame ();
}


void Host_DisableForLoading (bool disable)
{
	if (disable)
	{
		S_ClearBuffer ();
		S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);
	}

	scr_disabled_for_loading = disable;
}


