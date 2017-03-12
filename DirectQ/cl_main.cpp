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
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "d3d_model.h"
#include "cl_fx.h"
#include "particles.h"


cvar_t	cl_web_download	("cl_web_download", "1");
cvar_t	cl_web_download_url	("cl_web_download_url", "http://bigfoot.quake1.net/"); // the quakeone.com link is dead //"http://downloads.quakeone.com/");

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	cl_name ("_cl_name", "player", CVAR_ARCHIVE);
cvar_t	cl_color ("_cl_color", "0", CVAR_ARCHIVE);

cvar_t	cl_shownet ("cl_shownet", "0");	// can be 0, 1, or 2
cvar_t	cl_nolerp ("cl_nolerp", "0");

cvar_t	cl_autoaim ("cl_autoaim", "0");
cvar_t	lookspring ("lookspring", "0", CVAR_ARCHIVE);
cvar_t	lookstrafe ("lookstrafe", "0", CVAR_ARCHIVE);
cvar_t	sensitivity ("sensitivity", "3", CVAR_ARCHIVE);

cvar_t	m_pitch ("m_pitch", "0.022", CVAR_ARCHIVE);
cvar_t	m_yaw ("m_yaw", "0.022", CVAR_ARCHIVE);
cvar_t	m_forward ("m_forward", "0", CVAR_ARCHIVE);
cvar_t	m_side ("m_side", "0.8", CVAR_ARCHIVE);

// proquake nat fix
cvar_t	cl_natfix ("cl_natfix", "1");

client_static_t	cls;
client_state_t	cl;

// FIXME: put these on hunk?
// consider it done ;)
#define BASE_ENTITIES	512

// visedicts now belong to the render
void D3D_AddVisEdict (entity_t *ent);
void D3D_BeginVisedicts (void);

// save and restore anything that needs it while wiping the cl struct
void CL_ClearCLStruct (void)
{
	client_state_t *oldcl = (client_state_t *) _alloca (sizeof (client_state_t));

	Q_MemCpy (oldcl, &cl, sizeof (client_state_t));
	memset (&cl, 0, sizeof (client_state_t));

	// now restore the stuff we want to persist between servers
	cl.death_location[0] = oldcl->death_location[0];
	cl.death_location[1] = oldcl->death_location[1];
	cl.death_location[2] = oldcl->death_location[2];
}


/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	// the if (!sv.active) Host_ClearMemory () call was moved to CL_ParseServerInfo so that it's valid to use MainHunk in there
	// wipe the entire cl structure
	CL_ClearCLStruct ();

	SZ_Clear (&cls.message);

	cl.teamscores = (teamscore_t *) MainHunk->Alloc (sizeof (teamscore_t) * 16);

	// clear down anything that was allocated one-time-only at startup
	memset (cls.dlights, 0, MAX_DLIGHTS * sizeof (dlight_t));
	memset (cls.lightstyles, 0, MAX_LIGHTSTYLES * sizeof (lightstyle_t));

	// wipe the spawn times for dlights
	for (int i = 0; i < MAX_DLIGHTS; i++)
	{
		cls.dlights[i].die = -1;
		cls.dlights[i].spawntime = -1;
	}

	// allocate space for the first 512 entities - also clears the array
	// the remainder are left at NULL and allocated on-demand if they are ever needed
	for (int i = 0; i < MAX_EDICTS; i++)
	{
		if (i < BASE_ENTITIES)
		{
			cls.entities[i] = (entity_t *) MainHunk->Alloc (sizeof (entity_t));
			cls.entities[i]->entnum = i;
		}
		else cls.entities[i] = NULL;
	}

	// ensure that effects get spawned on the first client frame
	cl.nexteffecttime = -1;

	// start with a valid view entity number as otherwise we will try to read the world and we'll have an invalid entity!!!
	cl.viewentity = 1;
}


/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void SHOWLMP_clear (void);

void CL_Disconnect (void)
{
	// stop sounds (especially looping!)
	S_StopAllSounds (true);

	SHOWLMP_clear ();

	// We have to shut down webdownloading first
	if (cls.download.web)
	{
		cls.download.disconnect = true;
		return;
	}

	// NULL the worldmodel so that rendering won't happen
	// (this is crap)
	cl.worldmodel = NULL;

	// stop all intermissions (the intermission screen prints the map name so
	// this is important for preventing a crash)
	cl.intermission = 0;

	// remove all center prints
	SCR_ClearCenterString ();

	// clear the map from the title bar
	UpdateTitlebarText ();

	// and tell it that we're not running a map either
	cls.maprunning = false;

	// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);

		cls.state = ca_disconnected;

		if (sv.active)
			Host_ShutdownServer (false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.signon = 0;
}

void CL_Disconnect_f (void)
{
	if (cls.download.web)
	{
		cls.download.disconnect = true;
		return;
	}

	CL_Disconnect ();

	if (sv.active)
		Host_ShutdownServer (false);
}


/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_KeepaliveMessage (bool showmsg = true);

void CL_EstablishConnection (char *host)
{
	if (cls.demoplayback) return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);

	if (!cls.netcon) Host_Error ("CL_Connect: connect failed\n");

	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing

	if (cl_natfix.integer) MSG_WriteByte (&cls.message, clc_nop); // ProQuake NAT Fix
}


/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		Sleep(100); // KT: why does this hack work ???
		MSG_WriteString (&cls.message, "prespawn");
		break;

	case 2:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("name \"%s\"\n", cl_name.string));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("color %i %i\n", ((int) cl_color.value) >> 4, ((int) cl_color.value) & 15));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("spawn %s", cls.spawnparms));
		break;

	case 3:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		break;

	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates
		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	// this causes lockups when switching between demos in warpspasm.
	// let's just not do it. ;)
	SCR_BeginLoadingPlaque ();

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;

		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			SCR_EndLoadingPlaque ();
			return;
		}
	}

	Q_snprintf (str, 1024, "playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}


/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	int i;

	for (i = 0; i < cl.num_entities; i++)
	{
		entity_t *ent = cls.entities[i];

		Con_Printf ("%3i:", i);

		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}

		Con_Printf
		(
			"%s:%3i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n",
			ent->model->name,
			ent->frame,
			ent->origin[0],
			ent->origin[1],
			ent->origin[2],
			ent->angles[0],
			ent->angles[1],
			ent->angles[2]
		);
	}
}


cvar_t cl_itembobheight ("cl_itembobheight", 0.0f);
cvar_t cl_itembobspeed ("cl_itembobspeed", 0.5f);
cvar_t cl_itemrotatespeed ("cl_itemrotatespeed", 100.0f);

float CL_LerpAngle (float oldangle, float newangle, float lerpfrac)
{
	float delta = newangle - oldangle;

	// interpolate along the shortest path
	if (delta > 180) delta -= 360; else if (delta < -180) delta += 360;

	// fixme - don't linearly interpolate this!!!!
	return oldangle + lerpfrac * delta;
}


float CL_LerpOrigin (entity_t *ent, float oldorigin, float neworigin, float lerpfrac)
{
	float delta = neworigin - oldorigin;

	// if delta is large assume a teleport and don't lerp
	if (delta > 100 || delta < -100)
	{
		ent->lerpflags |= LERP_RESETORIGIN;
		return neworigin;
	}
	else return oldorigin + lerpfrac * delta;
}


bool CL_ShouldOriginLerp (entity_t *ent)
{
	if (ent->curr.msg_origin[0] != ent->prev.msg_origin[0]) return false;
	if (ent->curr.msg_origin[1] != ent->prev.msg_origin[1]) return false;

	return true;
}


void CL_LerpEntity (entity_t *ent)
{
	if (ent->forcelink)
	{
		// the entity was not updated in the last message so move to the final spot
		Vector3Copy (ent->origin, ent->curr.msg_origin);
		Vector3Copy (ent->angles, ent->curr.msg_angles);
		ent->lerpflags |= LERP_RESETALL;
	}
	else if (ent->lerpflags & LERP_MOVESTEP)
	{
		// horizontal (step) movement doesn't lerp
		ent->origin[0] = ent->curr.msg_origin[0];
		ent->origin[1] = ent->curr.msg_origin[1];

		// vertical (non-step) movement does
		if (CL_ShouldOriginLerp (ent))
			ent->origin[2] = CL_LerpOrigin (ent, ent->prev.msg_origin[2], ent->curr.msg_origin[2], cl.lerpfrac);
		else ent->origin[2] = ent->curr.msg_origin[2];

		// just move angles to the final spot
		Vector3Copy (ent->angles, ent->curr.msg_angles);
	}
	else
	{
		// lerp origin
		ent->origin[0] = CL_LerpOrigin (ent, ent->prev.msg_origin[0], ent->curr.msg_origin[0], cl.lerpfrac);
		ent->origin[1] = CL_LerpOrigin (ent, ent->prev.msg_origin[1], ent->curr.msg_origin[1], cl.lerpfrac);
		ent->origin[2] = CL_LerpOrigin (ent, ent->prev.msg_origin[2], ent->curr.msg_origin[2], cl.lerpfrac);

		// always lerp angles (how much of this is game changing???)
		ent->angles[0] = CL_LerpAngle (ent->prev.msg_angles[0], ent->curr.msg_angles[0], cl.lerpfrac);
		ent->angles[1] = CL_LerpAngle (ent->prev.msg_angles[1], ent->curr.msg_angles[1], cl.lerpfrac);
		ent->angles[2] = CL_LerpAngle (ent->prev.msg_angles[2], ent->curr.msg_angles[2], cl.lerpfrac);
	}

	// rotate binary objects locally
	if (ent->model->flags & EF_ROTATE)
	{
		// allow bouncing items for those who like them
		if (cl_itembobheight.value > 0.0f)
			ent->origin[2] += (cos ((cl.time + ent->entnum) * cl_itembobspeed.value * (2.0f * D3DX_PI)) + 1.0f) * 0.5f * cl_itembobheight.value;

		// bugfix - a rotating backpack spawned from a dead player gets the same angles as the player
		// if it was spawned when the player is not upright (e.g. killed by a rocket or similar) and
		// it inherits the players entity_t struct
		ent->angles[0] = 0;
		ent->angles[1] = cl.bobjrotate;
		ent->angles[2] = 0;
	}

	// the entity links are updated now
	ent->forcelink = false;
}


void CL_AddVisedicts (void)
{
	for (int i = 1; i < cl.num_entities; i++)
	{
		entity_t *ent = cls.entities[i];

		if (!ent) continue;
		if (ent->curr.msgtime != cl.mtime[0]) ent->model = NULL;
		if (!ent->model) continue;

		if (ent->effects)
		{
			// these need to spawn from here as they are per-frame dynamic effects
			if (ent->effects & EF_MUZZLEFLASH) CL_MuzzleFlash (ent, i);
			if (ent->effects & EF_BRIGHTLIGHT) CL_BrightLight (ent, i);
			if (ent->effects & EF_DIMLIGHT) CL_DimLight (ent, i);

			// this is now animated on the GPU so it needs to be emitted every frame
			if (ent->effects & EF_BRIGHTFIELD) ParticleSystem.EntityParticles (ent);
		}

		// check for timed effects
		if (cl.time >= cl.nexteffecttime)
		{
			if (ent->model->flags)
			{
				if (ent->model->flags & EF_WIZARDTRAIL)
					CL_WizardTrail (ent, i);
				else if (ent->model->flags & EF_KNIGHTTRAIL)
					CL_KnightTrail (ent, i);
				else if (ent->model->flags & EF_ROCKET)
					CL_RocketTrail (ent, i);
				else if (ent->model->flags & EF_VORETRAIL)
					CL_VoreTrail (ent, i);
				else if (ent->model->flags & EF_GIB)
					ParticleSystem.RocketTrail (ent->oldorg, ent->origin, RT_GIB);
				else if (ent->model->flags & EF_ZOMGIB)
					ParticleSystem.RocketTrail (ent->oldorg, ent->origin, RT_ZOMGIB);
				else if (ent->model->flags & EF_GRENADE)
					ParticleSystem.RocketTrail (ent->oldorg, ent->origin, RT_GRENADE);
			}

			// and update the old origin
			Vector3Copy (ent->oldorg, ent->origin);
		}

		extern bool chase_nodraw;

		// chasecam test
		if (i == cl.viewentity && !chase_active.value) continue;
		if (i == cl.viewentity && chase_nodraw) continue;

		// the entity is a visedict now
		D3D_AddVisEdict (ent);
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
void CL_LerpPoint (void)
{
	// dropped packet, or start of demo
	if (cl.mtime[0] - cl.mtime[1] > 0.1)
		cl.mtime[1] = cl.mtime[0] - 0.1;

	if (cl.time > cl.mtime[0] || cl.mtime[0] == cl.mtime[1] || cls.timedemo)
	{
		cl.time = cl.mtime[0];
		cl.lerpfrac = 1;
	}
	else if (cl.time < cl.mtime[1])
	{
		cl.time = cl.mtime[1];
		cl.lerpfrac = 0;
	}
	else if (cl_nolerp.value)
		cl.lerpfrac = 1;
	else cl.lerpfrac = (cl.time - cl.mtime[1]) / (cl.mtime[0] - cl.mtime[1]);
}


/*
===============
CL_UpdateClient

Read all incoming data from the server
===============
*/
void CL_BeginNotifyString (void);
void CL_EndNotifyString (void);

void CL_UpdateClient (double frametime)
{
	if (cls.state != ca_connected) return;

	// time is always advanced even if we're not reading from the server
	cl.time += frametime;

	CL_BeginNotifyString ();

	do
	{
		int ret = CL_GetMessage ();

		if (ret == -1) Host_Error ("CL_UpdateClient: lost server connection");
		if (!ret) break;

		cl.lastrecievedmessage = CHostTimer::realtime;
		CL_ParseServerMessage ();
	} while (cls.state == ca_connected);

	if (cl_shownet.value) Con_Printf ("\n");

	// clear the network message so that the next frame will get nothing
	SZ_Clear (&net_message);

	CL_EndNotifyString ();

	// get fractional update time
	CL_LerpPoint ();

	// interpolate player info
	for (int i = 0; i < 3; i++)
	{
		// added punchangle lerping
		cl.velocity[i] = cl.mvelocity[1][i] + cl.lerpfrac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);
		cl.punchangle[i] = cl.mpunchangle[1][i] + cl.lerpfrac * (cl.mpunchangle[0][i] - cl.mpunchangle[1][i]);
	}

	if (cls.demoplayback)
	{
		// interpolate the angles
		cl.viewangles[0] = CL_LerpAngle (cl.mviewangles[1][0], cl.mviewangles[0][0], cl.lerpfrac);
		cl.viewangles[1] = CL_LerpAngle (cl.mviewangles[1][1], cl.mviewangles[0][1], cl.lerpfrac);
		cl.viewangles[2] = CL_LerpAngle (cl.mviewangles[1][2], cl.mviewangles[0][2], cl.lerpfrac);
	}

	// interpolate entities - the viewent never uses MOVETYPE_STEP and this is explicitly enforced
	cl.bobjrotate = anglemod (cl_itemrotatespeed.value * cl.time);
	cls.entities[cl.viewentity]->lerpflags &= ~LERP_MOVESTEP;

	for (int i = 1; i < cl.num_entities; i++)
	{
		entity_t *ent = cls.entities[i];

		if (!ent) continue;

		// if the object wasn't included in the last packet, remove it
		// (intentionally falls through to next condition)
		if (ent->curr.msgtime != cl.mtime[0]) ent->model = NULL;

		// doesn't have a model
		if (!ent->model)
		{
			// clear it's interpolation data too
			ent->lerpflags |= LERP_RESETALL;
			continue;
		}

		// add an entity to this model
		ent->model->numents++;

		CL_LerpEntity (ent);
	}
}


void CL_PrepEntitiesForRendering (void)
{
	if (cls.state == ca_connected)
	{
		// reset visedicts count and structs
		D3D_BeginVisedicts ();

		// entity states are always brought up to date, even if not reading from the server
		CL_AddVisedicts ();
		CL_UpdateTEnts ();

		// set time for the next set of effects to fire
		if (cl.time >= cl.nexteffecttime)
		{
			if (cl_effectrate.value > 0)
				cl.nexteffecttime = cl.time + (1.0 / cl_effectrate.value);
			else cl.nexteffecttime = cl.time;
		}
	}

	// the view-related stuff needs to be brought up to date here so that it's properly in sync with the rest of the client update
	// and it's deferred to this point in time so that we can properly check the chasecam against other entities
	V_RenderView ();
}


/*
=================
CL_Init
=================
*/
cmd_t CL_PrintEntities_f_Cmd ("entities", CL_PrintEntities_f);
cmd_t CL_Disconnect_f_Cmd ("disconnect", CL_Disconnect_f);
cmd_t CL_Record_f_Cmd ("record", CL_Record_f);
cmd_t CL_Stop_f_Cmd ("stop", CL_Stop_f);
cmd_t CL_PlayDemo_f_Cmd ("playdemo", CL_PlayDemo_f);
cmd_t CL_TimeDemo_f_Cmd ("timedemo", CL_TimeDemo_f);


void CL_Init (void)
{
	CL_InitFX ();

	SZ_Alloc (&cls.message, 1024);

	// allocated here because (1) it's just a poxy array of pointers, so no big deal, and (2) it's
	// referenced during a changelevel (?only when there's no intermission?) so it needs to be in
	// the persistent heap, and (3) the full thing is allocated each map anyway, so why not shift
	// the overhead (small as it is) to one time only.
	cls.entities = (entity_t **) MainZone->Alloc (MAX_EDICTS * sizeof (entity_t *));

	// these need to persist between map changes
	cls.dlights = (dlight_t *) MainZone->Alloc (MAX_DLIGHTS * sizeof (dlight_t));
	cls.lightstyles = (lightstyle_t *) MainZone->Alloc (MAX_LIGHTSTYLES * sizeof (lightstyle_t));

	CL_InitInput ();
}


