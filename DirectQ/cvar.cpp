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
// cvar.c -- dynamic variable tracking

#include "quakedef.h"

cvar_t	*cvar_vars = NULL;
cvar_alias_t *cvar_alias_vars = NULL;

char *cvar_null_string = "";

cvar_t *Cmd_FindCvar (char *name);

bool cvar_t::initialized = false;


cvar_t *cvar_t::GetNextServerRuleVar (char *prevCvarName)
{
	bool findprev = false;
	bool prevfound = false;

	if (prevCvarName && prevCvarName[0])
		findprev = true;
	else prevfound = true;

	for (cvar_t *var = cvar_vars; var; var = var->next)
	{
		if (findprev)
		{
			if (!_stricmp (var->name, prevCvarName))
			{
				prevfound = true;
				continue;
			}
		}

		if (!prevfound) continue;
		if (!(var->usage & CVAR_SERVER)) continue;

		// this is the variable
		return var;
	}

	// not found
	return NULL;
}


/*
============
cvar_t::FindVar

used only for cases where the full completion list can't be relied on to be up yet; use Cmd_FindCvar otherwise
============
*/
cvar_t *cvar_t::FindVar (char *var_name)
{
	// regular cvars
	for (cvar_t *var = cvar_vars; var; var = var->next)
	{
		// skip nehahra cvars
		if (!nehahra && (var->usage & CVAR_NEHAHRA)) continue;

		if (!_stricmp (var_name, var->name))
		{
			return var;
		}
	}

	// alias cvars
	for (cvar_alias_t *var = cvar_alias_vars; var; var = var->next)
	{
		// skip nehahra cvars
		if (!nehahra && (var->var->usage & CVAR_NEHAHRA)) continue;

		if (!_stricmp (var_name, var->name))
		{
			return var->var;
		}
	}

	return NULL;
}


void cvar_t::ResetAll (void)
{
	for (cvar_t *var = cvar_vars; var; var = var->next)
	{
		if (!var->defaultvalue) continue;

		// chuck out any seta flags
		var->usage &= ~CVAR_SETA;
		var->Set (var->defaultvalue);
	}
}


/*
============
cvar_t::VariableValue
============
*/
float cvar_t::VariableValue (char *var_name)
{
	cvar_t	*var;

	var = cvar_t::FindVar (var_name);

	if (!var) return 0;

	return atof (var->string);
}


/*
============
cvar_t::VariableString
============
*/
char *cvar_t::VariableString (char *var_name)
{
	cvar_t *var;

	var = cvar_t::FindVar (var_name);

	if (!var)
		return cvar_null_string;

	return var->string;
}


bool cvar_t::Preset (void)
{
	// reject set attempt
	if (this->usage & CVAR_READONLY)
	{
		Con_Printf ("The cvar \"%s\" is for read-only use only and is not intended to be set by the player\n", this->name);
		return false;
	}

	if ((this->usage & CVAR_SYSTEM) && cvar_t::initialized)
	{
		Con_Printf ("The cvar \"%s\" is for system use only and is not intended to be set by the player\n", this->name);
		return false;
	}

	// it's OK to set the cvar now
	return true;
}


void cvar_t::RConSet (void)
{
	// joe, from ProQuake: rcon (64 doesn't mean anything special,
	// but we need some extra space because NET_MAXMESSAGE == RCON_BUFF_SIZE)
	if (rcon_active && (rcon_message.cursize < rcon_message.maxsize - strlen (this->name) - strlen (this->string) - 64))
	{
		rcon_message.cursize--;
		MSG_WriteString (&rcon_message, va ("\"%s\" set to \"%s\"\n", this->name, this->string));
	}
}


void cvar_t::SetCommon (void)
{
	// don't bug the player
	if (this->usage & CVAR_INTERNAL) return;

	// be friendly and notify the player if this change can't take effect immediately
	if ((this->usage & CVAR_RESTART) && cvar_t::initialized)
		Con_Printf ("You will need to restart DirectQ for this change to take effect\n");

	//if ((this->usage & CVAR_RENDERER) && cvar_t::initialized)
	//	Con_Printf ("You will need to restart the renderer (use \"vid_restart\") for this change to take effect\n");

	if ((this->usage & CVAR_MAP) && (cls.state == ca_connected))
		Con_Printf ("You will need to reload the current map for this change to take effect\n");

	// don't spam this if the server stuffcmds it
	// (this spams for every cvar that ain't archived which is crap, crap, crap, crap)
	//if (!(this->usage & CVAR_ARCHIVE) && cvar_t::initialized && key_dest == key_console)
	//	Con_Printf ("You will need to add %s to your autoexec for this change to persist\n", COM_ShiftTextColor (va ("\"%s\" \"%s\"", this->name, this->string)));

	if (this->callback) this->callback (this);

	if (!(this->usage & CVAR_SERVER)) return;
	if (!sv.active) return;

	// add the name of the person who changed it to the message
	SV_BroadcastPrintf ("\"%s\" was changed to \"%s\" by \"%s\"\n", this->name, this->string, cl_name.string);
}


/*
============
Cvar_Set
============
*/
void cvar_t::Set (float value)
{
	if (!this->Preset ()) return;

	if (this->value != value)
	{
		// store back to the cvar
		this->value = value;

		if (this->value < 0)
			this->integer = (int) (this->value - 0.5f);
		else this->integer = (int) (this->value + 0.5f);

		// copy out the value to a temp buffer
		char valbuf[32];
		Q_snprintf (valbuf, 32, "%g", this->value);

		HeapFree (GetProcessHeap (), 0, this->string);
		this->string = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (valbuf) + 1);

		strcpy (this->string, valbuf);

		this->SetCommon ();
	}

	this->RConSet ();
}


void cvar_t::Set (char *value)
{
	if (!this->Preset ()) return;

	if (strcmp (this->string, value))
	{
		HeapFree (GetProcessHeap (), 0, this->string);
		this->string = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (value) + 1);
		strcpy (this->string, value);
		this->value = atof (this->string);

		if (this->value < 0)
			this->integer = (int) (this->value - 0.5f);
		else this->integer = (int) (this->value + 0.5f);

		this->SetCommon ();
	}

	this->RConSet ();
}


void Cvar_SetA_f (void)
{
	if (Cmd_Argc () != 3)
		return;

	cvar_t *var = cvar_t::FindVar (Cmd_Argv (1));

	if (var)
	{
		var->Set (Cmd_Argv (2));
		var->usage |= (CVAR_ARCHIVE | CVAR_SETA);
	}
}


cmd_t cmd_CvarSetA ("seta", Cvar_SetA_f);

/*
============
Cvar_Register

Adds a freestanding variable to the variable list.
============
*/
void cvar_t::Register (void)
{
	// these should never go through here but let's just be certain
	// (edit - actually it does - recursively - when setting up shadows; see below)
	if (this->usage & CVAR_DUMMY) return;

	// hack to prevent double-definition of nehahra cvars
	bool oldneh = nehahra;
	nehahra = true;

	// first check to see if it has already been defined
	cvar_t *check = cvar_t::FindVar (this->name);

	// silently ignore it
	if (check) return;

	// unhack (note: this is not actually necessary as the game isn't up yet, but for the
	// sake of correctness we do it anyway)
	nehahra = oldneh;

	// check for overlap with a command
	if (Cmd_Exists (this->name)) return;

	// store the value off
	this->value = atof (this->string);
	this->integer = (int) this->value;

	// store out the default value at registration time
	this->defaultvalue = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (this->string) + 1);
	strcpy (this->defaultvalue, this->string);

	// link the variable in
	this->next = cvar_vars;
	cvar_vars = this;
}


/*
============
cvar_t::WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void cvar_t::WriteVariables (std::ofstream &f)
{
	for (cvar_t *var = cvar_vars; var; var = var->next)
	{
		if (var->usage & CVAR_SETA)
		{
			f << "seta " << var->name << " \"" << var->string << "\"\n";
			continue;
		}

		if (var->usage & CVAR_ARCHIVE)
		{
			f << var->name << " \"" << var->string << "\"\n";
			continue;
		}
	}
}


cvar_t::cvar_t (char *cvarname, char *initialval, int useflags, cvarcallback_t cb)
{
	// alloc space
	this->name = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (cvarname) + 1);
	this->string = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (initialval) + 1);

	// copy in the data
	strcpy (this->name, cvarname);
	strcpy (this->string, initialval);
	this->usage = useflags;
	this->defaultvalue = NULL;
	this->callback = cb;

	// self-register the cvar at construction time
	this->Register ();
}


cvar_t::cvar_t (char *cvarname, float initialval, int useflags, cvarcallback_t cb)
{
	// alloc space
	this->name = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (cvarname) + 1);

	// copy out the value to a temp buffer
	char valbuf[32];
	Q_snprintf (valbuf, 32, "%g", initialval);

	// alloc space for the string
	this->string = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (valbuf) + 1);

	// copy in the data
	strcpy (this->name, cvarname);
	strcpy (this->string, valbuf);
	this->usage = useflags;
	this->defaultvalue = NULL;
	this->callback = cb;

	// self-register the cvar at construction time
	this->Register ();
}


cvar_t::cvar_t (void)
{
	// dummy cvar for temp usage; not registered
	this->name = (char *) HeapAlloc (GetProcessHeap (), 0, 2);
	this->string = (char *) HeapAlloc (GetProcessHeap (), 0, 2);

	this->name[0] = 0;
	this->string[0] = 0;
	this->defaultvalue = NULL;
	this->value = 0;
	this->usage = CVAR_DUMMY;
	this->integer = 0;
	this->next = NULL;
	this->callback = NULL;
}


cvar_t::~cvar_t (void)
{
	// protect the zone from overflowing if cvars are declared in function scope
	HeapFree (GetProcessHeap (), 0, this->name);
	HeapFree (GetProcessHeap (), 0, this->string);
}


cvar_alias_t::cvar_alias_t (char *cvarname, cvar_t *cvarvar)
{
	if (!cvarname) return;
	if (!cvarvar) return;

	// these should never go through here but let's just be certain
	// (edit - actually it does - recursively - when setting up shadows; see below)
	if (cvarvar->usage & CVAR_DUMMY) return;

	// hack to prevent double-definition of nehahra cvars
	bool oldneh = nehahra;
	nehahra = true;

	// first check to see if it has already been defined
	if (cvar_t::FindVar (cvarname)) return;

	// unhack (note: this is not actually necessary as the game isn't up yet, but for the
	// sake of correctness we do it anyway)
	nehahra = oldneh;

	// check for overlap with a command
	if (Cmd_Exists (cvarname)) return;

	// alloc space for name
	this->name = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (cvarname) + 1);
	strcpy (this->name, cvarname);

	this->var = cvarvar;

	this->next = cvar_alias_vars;
	cvar_alias_vars = this;
}


void cvar_t::AddVariable (char *cvarname, char *initialval, int useflags, cvarcallback_t cb)
{
	// allow adding cvars explicitly within functions too
	cvar_t *var = new cvar_t (cvarname, initialval, useflags, cb);
}


void cvar_t::AddVariable (char *cvarname, float initialval, int useflags, cvarcallback_t cb)
{
	// allow adding cvars explicitly within functions too
	cvar_t *var = new cvar_t (cvarname, initialval, useflags, cb);
}


void cvar_t::AddVariable (void)
{
	// allow adding cvars explicitly within functions too
	cvar_t *var = new cvar_t ();
}


