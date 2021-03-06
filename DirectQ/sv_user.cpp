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
// sv_user.c -- server code for moving users

#include "quakedef.h"

edict_t	*sv_player;

extern	cvar_t	sv_friction;
cvar_t	sv_edgefriction ("edgefriction", "2");
extern	cvar_t	sv_stopspeed;

static QMATRIX userav;

vec3_t	wishdir;
float wishspeed;

// world
float	*angles;
float	*origin;
float	*velocity;

bool	onground;

usercmd_t	cmd;

cvar_t	sv_idealpitchscale ("sv_idealpitchscale", "0.8");


/*
===============
SV_SetIdealPitch
===============
*/
#define	MAX_FORWARD	6
void SV_SetIdealPitch (void)
{
	float	angleval, sinval, cosval;
	trace_t	tr;
	vec3_t	top, bottom;
	float	z[MAX_FORWARD];
	int		i, j;
	int		step, dir, steps;

	if (!((int) sv_player->v.flags & FL_ONGROUND))
		return;

	angleval = sv_player->v.angles[1] * D3DX_PI * 2 / 360;
	sinval = sin (angleval);
	cosval = cos (angleval);

	for (i = 0; i < MAX_FORWARD; i++)
	{
		top[0] = sv_player->v.origin[0] + cosval * (i + 3) * 12;
		top[1] = sv_player->v.origin[1] + sinval * (i + 3) * 12;
		top[2] = sv_player->v.origin[2] + sv_player->v.view_ofs[2];

		bottom[0] = top[0];
		bottom[1] = top[1];
		bottom[2] = top[2] - 160;

		tr = SV_Move (top, vec3_origin, vec3_origin, bottom, MOVE_NOMONSTERS, sv_player);

		if (tr.allsolid)
			return;	// looking at a wall, leave ideal the way is was

		if (tr.fraction == 1)
			return;	// near a dropoff

		z[i] = top[2] + tr.fraction * (bottom[2] - top[2]);
	}

	dir = 0;
	steps = 0;

	for (j = 1; j < i; j++)
	{
		step = z[j] - z[j-1];

		if (step > -ON_EPSILON && step < ON_EPSILON)
			continue;

		if (dir && (step - dir > ON_EPSILON || step - dir < -ON_EPSILON))
			return;		// mixed changes

		steps++;
		dir = step;
	}

	if (!dir)
	{
		sv_player->v.idealpitch = 0;
		return;
	}

	if (steps < 2)
		return;

	sv_player->v.idealpitch = -dir * sv_idealpitchscale.value;
}


/*
==================
SV_UserFriction

==================
*/
void SV_UserFriction (double frametime)
{
	float	*vel;
	float	speed, newspeed, control;
	vec3_t	start, stop;
	float	friction;
	trace_t	trace;

	vel = velocity;

	speed = sqrt (vel[0] * vel[0] + vel[1] * vel[1]);

	if (!speed)
		return;

	// if the leading edge is over a dropoff, increase friction
	start[0] = stop[0] = origin[0] + vel[0] / speed * 16;
	start[1] = stop[1] = origin[1] + vel[1] / speed * 16;
	start[2] = origin[2] + sv_player->v.mins[2];
	stop[2] = start[2] - 34;

	trace = SV_Move (start, vec3_origin, vec3_origin, stop, MOVE_NOMONSTERS, sv_player);

	if (trace.fraction == 1.0)
		friction = sv_friction.value * sv_edgefriction.value;
	else friction = sv_friction.value;

	// apply friction
	control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
	newspeed = speed - frametime * control * friction;

	if (newspeed < 0)
		newspeed = 0;

	newspeed /= speed;

	vel[0] = vel[0] * newspeed;
	vel[1] = vel[1] * newspeed;
	vel[2] = vel[2] * newspeed;
}


/*
==============
SV_Accelerate
==============
*/
cvar_t	sv_maxspeed ("sv_maxspeed", "320", CVAR_SERVER);
cvar_t	sv_accelerate ("sv_accelerate", "10");


void SV_Accelerate (double frametime)
{
	int			i;
	float		addspeed, accelspeed, currentspeed;

	currentspeed = Vector3Dot (velocity, wishdir);
	addspeed = wishspeed - currentspeed;

	if (addspeed <= 0)
		return;

	accelspeed = sv_accelerate.value * frametime * wishspeed;

	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i = 0; i < 3; i++)
		velocity[i] += accelspeed * wishdir[i];
}

void SV_AirAccelerate (vec3_t wishveloc, double frametime)
{
	int			i;
	float		addspeed, wishspd, accelspeed, currentspeed;

	wishspd = Vector3Normalize (wishveloc);

	if (wishspd > 30)
		wishspd = 30;

	currentspeed = Vector3Dot (velocity, wishveloc);
	addspeed = wishspd - currentspeed;

	if (addspeed <= 0)
		return;

	//	accelspeed = sv_accelerate.value * frametime;
	accelspeed = sv_accelerate.value * wishspeed * frametime;

	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i = 0; i < 3; i++)
		velocity[i] += accelspeed * wishveloc[i];
}


void DropPunchAngle (double frametime)
{
	float	len;

	len = Vector3Normalize (sv_player->v.punchangle);
	len -= 10 * frametime;

	if (len < 0)
		len = 0;

	Vector3Scale (sv_player->v.punchangle, sv_player->v.punchangle, len);
}

/*
===================
SV_WaterMove

===================
*/
void SV_WaterMove (double frametime)
{
	int		i;
	vec3_t	wishvel;
	float	speed, newspeed, addspeed, accelspeed;

	// user intentions
	userav.AngleVectors (sv_player->v.v_angle);

	for (i = 0; i < 3; i++)
		wishvel[i] = userav.fw[i] * cmd.forwardmove + userav.rt[i] * cmd.sidemove;

	if (!cmd.forwardmove && !cmd.sidemove && !cmd.upmove)
		wishvel[2] -= 60;		// drift towards bottom
	else wishvel[2] += cmd.upmove;

	wishspeed = Vector3Length (wishvel);

	if (wishspeed > sv_maxspeed.value)
	{
		Vector3Scale (wishvel, wishvel, sv_maxspeed.value / wishspeed);
		wishspeed = sv_maxspeed.value;
	}

	wishspeed *= 0.7f;

	// water friction
	speed = Vector3Length (velocity);

	if (speed)
	{
		newspeed = speed - frametime * speed * sv_friction.value;

		if (newspeed < 0)
			newspeed = 0;

		Vector3Scale (velocity, velocity, newspeed / speed);
	}
	else newspeed = 0;

	// water acceleration
	if (!wishspeed)
		return;

	addspeed = wishspeed - newspeed;

	if (addspeed <= 0)
		return;

	Vector3Normalize (wishvel);
	accelspeed = sv_accelerate.value * wishspeed * frametime;

	if (accelspeed > addspeed)
		accelspeed = addspeed;

	for (i = 0; i < 3; i++)
		velocity[i] += accelspeed * wishvel[i];
}

void SV_WaterJump (void)
{
	if (sv.time > sv_player->v.teleport_time || !sv_player->v.waterlevel)
	{
		sv_player->v.flags = (int) sv_player->v.flags & ~FL_WATERJUMP;
		sv_player->v.teleport_time = 0;
	}

	sv_player->v.velocity[0] = sv_player->v.movedir[0];
	sv_player->v.velocity[1] = sv_player->v.movedir[1];
}


/*
===================
SV_AirMove

===================
*/
void SV_AirMove (double frametime)
{
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;

	userav.AngleVectors (sv_player->v.angles);

	fmove = cmd.forwardmove;
	smove = cmd.sidemove;

	// hack to not let you back into teleporter
	if (sv.time < sv_player->v.teleport_time && fmove < 0)
		fmove = 0;

	for (i = 0; i < 3; i++)
		wishvel[i] = userav.fw[i] * fmove + userav.rt[i] * smove;

	if ((int) sv_player->v.movetype != MOVETYPE_WALK)
		wishvel[2] = cmd.upmove;
	else
		wishvel[2] = 0;

	Vector3Copy (wishdir, wishvel);
	wishspeed = Vector3Normalize (wishdir);

	if (wishspeed > sv_maxspeed.value)
	{
		Vector3Scale (wishvel, wishvel, sv_maxspeed.value / wishspeed);
		wishspeed = sv_maxspeed.value;
	}

	if (sv_player->v.movetype == MOVETYPE_NOCLIP)
	{
		// noclip
		Vector3Copy (velocity, wishvel);
	}
	else if (onground)
	{
		SV_UserFriction (frametime);
		SV_Accelerate (frametime);
	}
	else
	{
		// not on ground, so little effect on velocity
		SV_AirAccelerate (wishvel, frametime);
	}
}


/*
===================
SV_NoclipMove -- johnfitz

new, alternate noclip. old noclip is still handled in SV_AirMove
===================
*/
void SV_NoClipMove (void)
{
	userav.AngleVectors (sv_player->v.v_angle);

	// ugh - client/server boundary crossing
	velocity[0] = userav.fw[0] * cmd.forwardmove + userav.rt[0] * cmd.sidemove;
	velocity[1] = userav.fw[1] * cmd.forwardmove + userav.rt[1] * cmd.sidemove;
	velocity[2] = userav.fw[2] * cmd.forwardmove + userav.rt[2] * cmd.sidemove;

	// doubled to match running speed
	velocity[2] += cmd.upmove * 2;

	if (Vector3Length (velocity) > sv_maxspeed.value)
	{
		Vector3Normalize (velocity);
		Vector3Scale (velocity, velocity, sv_maxspeed.value);
	}
}


/*
===================
SV_ClientThink

the move fields specify an intended velocity in pix/sec
the angle fields specify an exact angular motion in degrees
===================
*/
void SV_ClientThink (double frametime)
{
	vec3_t		v_angle;

	if (sv_player->v.movetype == MOVETYPE_NONE)
		return;

	onground = ((int) sv_player->v.flags & FL_ONGROUND) ? true : false;

	origin = sv_player->v.origin;
	velocity = sv_player->v.velocity;

	DropPunchAngle (frametime);

	// if dead, behave differently
	if (sv_player->v.health <= 0)
		return;

	// angles
	// show 1/3 the pitch angle and all the roll angle
	cmd = host_client->cmd;
	angles = sv_player->v.angles;

	Vector3Add (v_angle, sv_player->v.v_angle, sv_player->v.punchangle);
	angles[2] = V_CalcRoll (sv_player->v.angles, sv_player->v.velocity) * 4;

	if (!sv_player->v.fixangle)
	{
		angles[0] = -v_angle[0] / 3;
		angles[1] = v_angle[1];
	}

	// Avoid annoying waterjumps in noclip
	if (sv_player->v.movetype == MOVETYPE_NOCLIP)
		sv_player->v.waterlevel = 0;

	if ((int) sv_player->v.flags & FL_WATERJUMP)
	{
		SV_WaterJump ();
		return;
	}

	// walk
	if (sv_player->v.movetype == MOVETYPE_NOCLIP)
		SV_NoClipMove ();
	else if (sv_player->v.waterlevel >= 2)
		SV_WaterMove (frametime);
	else SV_AirMove (frametime);
}


/*
===================
SV_ReadClientMove
===================
*/
void SV_ReadClientMove (usercmd_t *move)
{
	int		i;
	vec3_t	angle;
	int		bits;

	// read ping time
	host_client->ping_times[host_client->num_pings % NUM_PING_TIMES] = sv.time - MSG_ReadFloat ();
	host_client->num_pings++;

	// read current angles
	if (host_client->netconnection->mod == MOD_PROQUAKE && sv.Protocol == PROTOCOL_VERSION_NQ)
	{
		for (i = 0; i < 3; i++)
			angle[i] = MSG_ReadProQuakeAngle ();
	}
	else if (sv.Protocol == PROTOCOL_VERSION_FITZ || sv.Protocol == PROTOCOL_VERSION_RMQ)
	{
		for (i = 0; i < 3; i++)
			angle[i] = MSG_ReadAngle16 (sv.Protocol, sv.PrototcolFlags);
	}
	else
	{
		for (i = 0; i < 3; i++)
			angle[i] = MSG_ReadAngle (sv.Protocol, sv.PrototcolFlags);
	}

	Vector3Copy (host_client->edict->v.v_angle, angle);

	// read movement
	move->forwardmove = MSG_ReadShort ();
	move->sidemove = MSG_ReadShort ();
	move->upmove = MSG_ReadShort ();

	// read buttons
	bits = MSG_ReadByte ();
	host_client->edict->v.button0 = bits & 1;
	host_client->edict->v.button2 = (bits & 2) >> 1;

	// read impulse
	i = MSG_ReadByte ();

	if (i) host_client->edict->v.impulse = i;
}

/*
===================
SV_ReadClientMessage

Returns false if the client should be killed
===================
*/
bool SV_ReadClientMessage (void)
{
	int		ret;
	int		cl_cmd;
	char	*s;

	do
	{
nextmsg:
		ret = NET_GetMessage (host_client->netconnection);

		if (ret == -1) return false;
		if (!ret) return true;

		MSG_BeginReading ();

		for (;;)
		{
			if (!host_client->active) return false;	// a command caused an error
			if (msg_badread) return false;

			cl_cmd = MSG_ReadChar ();

			switch (cl_cmd)
			{
			case -1:
				goto nextmsg;		// end of message

			default:
				Con_Printf ("SV_ReadClientMessage: unknown command %i\n", cmd);
				return false;

			case clc_nop:
				break;

			case clc_stringcmd:
				s = MSG_ReadString ();

				if (host_client->privileged)
					ret = 2;
				else ret = 0;

				if (_strnicmp (s, "status", 6) == 0)
					ret = 1;
				else if (_strnicmp (s, "god", 3) == 0)
					ret = 1;
				else if (_strnicmp (s, "notarget", 8) == 0)
					ret = 1;
				else if (_strnicmp (s, "fly", 3) == 0)
					ret = 1;
				else if (_strnicmp (s, "name", 4) == 0)
					ret = 1;
				else if (_strnicmp (s, "noclip", 6) == 0)
					ret = 1;
				else if (_strnicmp (s, "say", 3) == 0)
					ret = 1;
				else if (_strnicmp (s, "say_team", 8) == 0)
					ret = 1;
				else if (_strnicmp (s, "tell", 4) == 0)
					ret = 1;
				else if (_strnicmp (s, "color", 5) == 0)
					ret = 1;
				else if (_strnicmp (s, "kill", 4) == 0)
					ret = 1;
				else if (_strnicmp (s, "pause", 5) == 0)
					ret = 1;
				else if (_strnicmp (s, "spawn", 5) == 0)
					ret = 1;
				else if (_strnicmp (s, "begin", 5) == 0)
					ret = 1;
				else if (_strnicmp (s, "prespawn", 8) == 0)
					ret = 1;
				else if (_strnicmp (s, "kick", 4) == 0)
					ret = 1;
				else if (_strnicmp (s, "ping", 4) == 0)
					ret = 1;
				else if (_strnicmp (s, "give", 4) == 0)
					ret = 1;
				else if (_strnicmp (s, "ban", 3) == 0)
					ret = 1;

				if (ret == 2)
					Cbuf_InsertText (s);
				else if (ret == 1)
					Cmd_ExecuteString (s, src_client);
				else Con_DPrintf ("%s tried to %s\n", host_client->name, s);

				break;

			case clc_disconnect:
				return false;

			case clc_move:
				SV_ReadClientMove (&host_client->cmd);
				break;
			}
		}
	} while (ret == 1);

	return true;
}



/*
==================
SV_RunClients
==================
*/
void SV_RunClients (float frametime)
{
	int i;

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
	{
		if (!host_client->active)
			continue;

		sv_player = host_client->edict;

		if (!SV_ReadClientMessage ())
		{
			SV_DropClient (false);	// client misbehaved...
			continue;
		}

		if (!host_client->spawned)
		{
			// clear client movement until a new packet is received
			memset (&host_client->cmd, 0, sizeof (host_client->cmd));
			continue;
		}

		// always pause in single player if in console or menus
		if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
			SV_ClientThink (frametime);
	}
}

