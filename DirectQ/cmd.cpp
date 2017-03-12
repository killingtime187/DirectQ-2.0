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
// cmd.c -- Quake script command processing module

#include "quakedef.h"
#include "location.h"

// forward declaration of alias types for addition to completion lists
struct cmdalias_t
{
	struct cmdalias_t	*next;
	char	*name;
	char	*value;
};

cmdalias_t	*cmd_alias = NULL;

extern cvar_t	*cvar_vars;
extern cvar_alias_t *cvar_alias_vars;

char *Cmd_DeCrapifyAliasValue (cmdalias_t *a)
{
	char *aliasbuf = (char *) scratchbuf;

	strcpy (aliasbuf, a->value);

	for (int i = strlen (aliasbuf); i; i--)
	{
		if ((aliasbuf[i] & 127) <= 32)
			aliasbuf[i] = 0;
		else break;
	}

	return aliasbuf;
}


void Cmd_WriteAlias (std::ofstream &f)
{
	for (cmdalias_t *a = cmd_alias; a; a = a->next)
	{
		f << "alias \"" << a->name << "\" \"" << Cmd_DeCrapifyAliasValue (a) << "\"\n";
	}
}


void AliasList_f (void)
{
	for (cmdalias_t *a = cmd_alias; a; a = a->next)
	{
		Con_Printf ("alias \"%s\" \"%s\"\n", a->name, Cmd_DeCrapifyAliasValue (a));
	}
}


// possible commands to execute
static cmd_t *cmd_functions = NULL;

void Cmd_Inc_f (void)
{
	cvar_t *var = NULL;

	switch (Cmd_Argc ())
	{
	default:
	case 1:
		Con_Printf ("%s <cvar> [amount] : increment cvar\n", Cmd_Argv (0));
		break;
	case 2:
		if ((var = cvar_t::FindVar (Cmd_Argv (1))) != NULL)
			var->Set (var->value + 1);
		break;
	case 3:
		if ((var = cvar_t::FindVar (Cmd_Argv (1))) != NULL)
			var->Set (var->value + atof (Cmd_Argv (2)));
		break;
	}
}


void Cmd_Dec_f (void)
{
	cvar_t *var = NULL;

	switch (Cmd_Argc ())
	{
	default:
	case 1:
		Con_Printf ("%s <cvar> [amount] : increment cvar\n", Cmd_Argv (0));
		break;
	case 2:
		if ((var = cvar_t::FindVar (Cmd_Argv (1))) != NULL)
			var->Set (var->value - 1);
		break;
	case 3:
		if ((var = cvar_t::FindVar (Cmd_Argv (1))) != NULL)
			var->Set (var->value - atof (Cmd_Argv (2)));
		break;
	}
}


cmd_t Cmd_Inc_Cmd1 ("inc", Cmd_Inc_f);
cmd_t Cmd_Inc_Cmd2 ("cvaradd", Cmd_Inc_f);	// from rage

// ... and why not ...
cmd_t Cmd_Dec_Cmd1 ("dec", Cmd_Dec_f);
cmd_t Cmd_Dec_Cmd2 ("cvarsubtract", Cmd_Dec_f);

// this dummy function exists so that no command will have a NULL function
void Cmd_NullFunction (void) {}

/*
=============================================================================

						COMMAND AUTOCOMPLETION

		This is now used for actual command execution too...

=============================================================================
*/

struct complist_t
{
	// note - !!!if the order here is changed then the struct inits below also need to be changed!!!
	// search for every occurance of bsearch and do the necessary...
	char *name;
	cmdalias_t *alias;
	cmd_t *cmd;
	cvar_t *var;
};

complist_t *complist = NULL;

// numbers of cvars and cmds
int numcomplist = 0;

int CmdCvarCompareFunc (const void *a, const void *b)
{
	complist_t *cc1 = (complist_t *) a;
	complist_t *cc2 = (complist_t *) b;

	return _stricmp (cc1->name, cc2->name);
}


void Cmd_BuildCompletionList (void)
{
	// nothing to start with
	numcomplist = 0;

	// count the number of cvars and cmds we have
	for (cvar_t *var = cvar_vars; var; var = var->next) numcomplist++;
	for (cmd_t *cmd = cmd_functions; cmd; cmd = cmd->next) numcomplist++;
	for (cmdalias_t *ali = cmd_alias; ali; ali = ali->next) numcomplist++;
	for (cvar_alias_t *ali = cvar_alias_vars; ali; ali = ali->next) numcomplist++;

	// alloc space for the completion list (add some overshoot here; we need 1 to NULL terminate the list)
	// place in zone so that we can rebuild the list if we ever need to.
	if (complist) HeapFree (GetProcessHeap (), 0, complist);

	complist = (complist_t *) HeapAlloc (GetProcessHeap (), 0, (numcomplist + 1) * sizeof (complist_t));

	// current item we're working on
	complist_t *complistcurrent = complist;

	// write in cvars
	for (cvar_t *var = cvar_vars; var; var = var->next, complistcurrent++)
	{
		complistcurrent->name = var->name;
		complistcurrent->alias = NULL;
		complistcurrent->cmd = NULL;
		complistcurrent->var = var;
	}

	// write in alias cvars
	for (cvar_alias_t *ali = cvar_alias_vars; ali; ali = ali->next, complistcurrent++)
	{
		complistcurrent->name = ali->name;
		complistcurrent->alias = NULL;
		complistcurrent->cmd = NULL;
		complistcurrent->var = ali->var;
	}

	// write in cmds
	for (cmd_t *cmd = cmd_functions; cmd; cmd = cmd->next, complistcurrent++)
	{
		// replace NULL function commands with a dummy that does nothing
		if (!cmd->function)
			cmd->function = Cmd_NullFunction;

		complistcurrent->name = cmd->name;
		complistcurrent->alias = NULL;
		complistcurrent->cmd = cmd;
		complistcurrent->var = NULL;
	}

	// write in aliases
	for (cmdalias_t *als = cmd_alias; als; als = als->next, complistcurrent++)
	{
		complistcurrent->name = als->name;
		complistcurrent->alias = als;
		complistcurrent->cmd = NULL;
		complistcurrent->var = NULL;
	}

	// sort before termination
	qsort ((void *) complist, numcomplist, sizeof (complist_t), CmdCvarCompareFunc);

	// terminate the list
	complistcurrent->name = NULL;
	complistcurrent->alias = NULL;
	complistcurrent->cmd = NULL;
	complistcurrent->var = NULL;
}


void Key_PrintMatch (char *cmd);

int Cmd_Match (char *partial, int matchcycle, bool conout)
{
	complist_t *clist = NULL;
	int len = strlen (partial);
	int nummatches;
	char currentmatch[128];

	if (!len) return 0;

	if (conout) Con_Printf ("]%s\n", partial);

	for (clist = complist, nummatches = 0; clist->name && (clist->cmd || clist->var || clist->alias); clist++)
	{
		// skip nehahra if we're not running nehahra
		if (clist->var && !nehahra && (clist->var->usage & CVAR_NEHAHRA)) continue;

		assert (clist->name);
		assert ((clist->alias || clist->cmd || clist->var));

		if (!_strnicmp (partial, clist->name, len))
		{
			if (conout)
			{
				if (clist->cmd)
					Con_Printf ("  (cmd) ");
				else if (clist->var)
					Con_Printf (" (cvar) ");
				else if (clist->alias)
					Con_Printf ("(alias) ");
				else Con_Printf ("  (bad) ");

				if (clist->var)
					Con_Printf ("%s (value \"%s\") (default \"%s\")\n", clist->name, clist->var->string, clist->var->defaultvalue);
				else if (clist->alias)
					Con_Printf ("%s (value \"%s\")\n", clist->name, clist->alias->value);
				else Con_Printf ("%s\n", clist->name);
			}

			// copy to the current position in the cycle
			if (nummatches == matchcycle)
				Q_strncpy (currentmatch, clist->name, 127);

			nummatches++;
		}
	}

	if (!nummatches)
	{
		// note - the full list is pretty huge - over 400 lines - so it's worse than useless putting it on-screen
		Con_Printf ("Could not match \"%s\"\n", partial);
		return 0;
	}

	// fill the command buffer with the current cycle position
	Key_PrintMatch (currentmatch);

	// return the number of matches
	return nummatches;
}


cvar_t *Cmd_FindCvar (char *name)
{
	complist_t key = {name, NULL, NULL, NULL};
	complist_t *clist = (complist_t *) bsearch (&key, complist, numcomplist, sizeof (complist_t), CmdCvarCompareFunc);

	if (!clist)
		return NULL;
	else if (clist->var)
	{
		// skip nehahra cvars if we're not running nehahra
		if (!nehahra && (clist->var->usage & CVAR_NEHAHRA)) return NULL;

		return clist->var;
	}
	else return NULL;
}


//=============================================================================

void CmdCvarList (bool dumpcmd, bool dumpvar)
{
	if (Cmd_Argc () == 1)
	{
		for (int i = 0; i < numcomplist; i++)
		{
			if (complist[i].cmd && dumpcmd) Con_Printf ("%s\n", complist[i].name);
			if (complist[i].var && dumpvar) Con_Printf ("%s\n", complist[i].name);
		}

		Con_Printf ("Use \"%s <filename>\" to dump to file\n", Cmd_Argv (0));
		return;
	}

	// because I just know somebody's gonna do this...
	char filename[1025] = {0};

	for (int i = 1; i < Cmd_Argc (); i++)
	{
		// prevent a buffer overrun here
		if ((strlen (filename) + strlen (Cmd_Argv (i)) + 2) >= MAX_PATH) break;

		strcat (filename, Cmd_Argv (i));
		strcat (filename, " ");
	}

	for (int i = strlen (filename) - 1; i; i--)
	{
		if (filename[i] == ' ')
		{
			filename[i] = 0;
			break;
		}
	}

	std::ofstream f (filename);

	if (f.is_open ())
	{
		Con_Printf ("Dumping %s to \"%s\"... ", Cmd_Argv (0), filename);

		for (int i = 0; i < numcomplist; i++)
		{
			if (complist[i].cmd && dumpcmd) f << complist[i].name << "\n";
			if (complist[i].var && dumpvar) f << complist[i].name << "\n";
		}

		Con_Printf ("done\n");
		f.close ();
	}
	else Con_Printf ("Couldn't create \"%s\"\n", filename);
}


void CmdList_f (void) {CmdCvarList (true, false);}
void CvarList_f (void) {CmdCvarList (false, true);}
void CmdCvarList_f (void) {CmdCvarList (true, true);}

cmd_t CmdList_Cmd ("cmdlist", CmdList_f);
cmd_t AliasList_Cmd ("aliaslist", AliasList_f);
cmd_t CvarList_Cmd ("cvarlist", CvarList_f);
cmd_t CmdCvarList_Cmd ("cmdcvarlist", CmdCvarList_f);

//=============================================================================

void Cmd_ForwardToServer (void);

// if this is false we suppress all commands (except "exec") and all output
extern bool full_initialized;

bool	cmd_wait;

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5; +attack; wait; -attack; impulse 2"
============
*/
void Cmd_Wait_f (void)
{
	cmd_wait = true;
}


/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

sizebuf_t	cmd_text;

/*
============
Cbuf_Init
============
*/
void Cbuf_Init (void)
{
	// space for commands and script files
	// take 64 k
	SZ_Alloc (&cmd_text, 0x10000);
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText (char *text)
{
	if (text && text[0])
	{
		int l = strlen (text);

		if (cmd_text.cursize + l >= cmd_text.maxsize)
		{
			Con_Printf ("Cbuf_AddText: overflow\n");
			return;
		}

		SZ_Write (&cmd_text, text, strlen (text));
	}
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
FIXME: actually change the command buffer to do less copying
============
*/
void Cbuf_InsertText (char *text)
{
	char *temp = NULL;

	// copy off any commands still remaining in the exec buffer
	int templen = cmd_text.cursize;
	int hunkmark = TempHunk->GetLowMark ();

	if (templen)
	{
		temp = (char *) TempHunk->FastAlloc (templen + 1);
		Q_MemCpy (temp, cmd_text.data, templen);
		SZ_Clear (&cmd_text);
	}

	// add the entire text of the file
	Cbuf_AddText (text);

	// add the copied off data
	if (templen) SZ_Write (&cmd_text, temp, templen);

	TempHunk->FreeToLowMark (hunkmark);
}


/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute (void)
{
	int		i;
	char	*text;
	char	line[1024];
	int		quotes;

	while (cmd_text.cursize)
	{
		// find a \n or ; line break
		text = (char *) cmd_text.data;

		quotes = 0;

		for (i = 0; i < cmd_text.cursize; i++)
		{
			if (text[i] == '"') quotes++;

			// don't break if inside a quoted string
			if (!(quotes & 1) &&  text[i] == ';') break;
			if (text[i] == '\n') break;
		}

		Q_MemCpy (line, text, i);
		line[i] = 0;

		// delete the text from the command buffer and move remaining commands down
		// this is necessary because commands (exec, alias) can insert data at the
		// beginning of the text buffer
		if (i == cmd_text.cursize)
			cmd_text.cursize = 0;
		else
		{
			i++;
			cmd_text.cursize -= i;
			Q_MemCpy (text, text + i, cmd_text.cursize);
		}

		// execute the command line
		Cmd_ExecuteString (line, src_command);

		if (cmd_wait)
		{
			// skip out while text still remains in buffer, leaving it
			// for next frame
			cmd_wait = false;
			break;
		}
	}
}


/*
==============================================================================

						SCRIPT COMMANDS

==============================================================================
*/

/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void Cmd_StuffCmds_f (void)
{
	int		i, j;
	int		s;
	char	*text, *build, c;
	int		hunkmark = TempHunk->GetLowMark ();

	if (Cmd_Argc () != 1)
	{
		Con_Printf ("stuffcmds : execute command line parameters\n");
		return;
	}

	// build the combined string to parse from
	s = 0;

	for (i = 1; i < com_argc; i++)
	{
		// NEXTSTEP nulls out -NXHost
		if (!com_argv[i]) continue;

		s += strlen (com_argv[i]) + 1;
	}

	if (!s) return;

	text = (char *) TempHunk->FastAlloc (s + 1);
	text[0] = 0;

	for (i = 1; i < com_argc; i++)
	{
		// NEXTSTEP nulls out -NXHost
		if (!com_argv[i]) continue;

		strcat (text, com_argv[i]);

		if (i != com_argc - 1) strcat (text, " ");
	}

	// pull out the commands
	build = (char *) TempHunk->FastAlloc (s + 1);
	build[0] = 0;

	for (i = 0; i < s - 1; i++)
	{
		if (text[i] == '+')
		{
			i++;

			for (j = i; (text[j] != '+') && (text[j] != '-') && (text[j] != 0); j++);

			c = text[j];
			text[j] = 0;

			strcat (build, text + i);
			strcat (build, "\n");
			text[j] = c;
			i = j - 1;
		}
	}

	if (build[0]) Cbuf_InsertText (build);

	TempHunk->FreeToLowMark (hunkmark);
}


// changed to use temphunk because heap crashes on vcpp2010
void Cmd_ExecFile (char *name)
{
	int hunkmark = TempHunk->GetLowMark ();
	char *cfgfile = (char *) TempHunk->FastAlloc (strlen (name) + 10);	// making some extra room here...
	strcpy (cfgfile, name);
	char *f = (char *) CQuakeFile::LoadFile (cfgfile, TempHunk);

	if (!f)
	{
		// i hate it when i forget to add ".cfg" to an exec command, so i fixed it
		COM_DefaultExtension (cfgfile, ".cfg");
		f = (char *) CQuakeFile::LoadFile (cfgfile, TempHunk);

		if ((f = (char *) CQuakeFile::LoadFile (cfgfile, TempHunk)) == NULL)
		{
			Con_SafePrintf ("couldn't exec \"%s\"\n", cfgfile);
			return;
		}
	}

	Con_SafePrintf ("execing \"%s\"\n", cfgfile);

	// fix if a config file isn't \n terminated
	Cbuf_InsertText (va ("//%s\n", cfgfile));
	Cbuf_InsertText (f);
	Cbuf_InsertText ("\n");
	TempHunk->FreeToLowMark (hunkmark);
}


/*
===============
Cmd_Exec_f
===============
*/
void Cmd_Exec_f (void)
{
	if (Cmd_Argc () < 2)
	{
		Con_SafePrintf ("exec <filename> : execute a script file\n");
		return;
	}

	for (int i = 1; i < Cmd_Argc (); i++)
	{
		// add directq.cfg to keep peaceful engine co-habitation
		if (!_stricmp (Cmd_Argv (i), "config.cfg")) Cmd_ExecFile ("directq.cfg");

		Cmd_ExecFile (Cmd_Argv (i));
	}
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void Cmd_Echo_f (void)
{
	int		i;

	for (i = 1; i < Cmd_Argc (); i++)
		Con_Printf ("%s ", Cmd_Argv (i));

	Con_Printf ("\n");
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly; seperated)
===============
*/

void Cmd_Alias_f (void)
{
	cmdalias_t	*a = NULL;
	char		cmd[1024];
	int			i, c;
	char		*s;

	if (Cmd_Argc () == 1)
	{
		Con_Printf ("Current alias commands:\n");

		for (a = cmd_alias; a; a = a->next)
			Con_Printf ("\"%s\" : \"%s\"\n", a->name, a->value);

		return;
	}

	s = Cmd_Argv (1);

	// try to find it first so that we can access it quickly for printing/etc
	complist_t key = {s, NULL, NULL, NULL};
	complist_t *clist = (complist_t *) bsearch (&key, complist, numcomplist, sizeof (complist_t), CmdCvarCompareFunc);

	if (Cmd_Argc () == 2)
	{
		if (clist)
		{
			// protect us oh lord from stupid programmer errors
			assert (clist->name);
			assert ((clist->alias || clist->cmd || clist->var));

			if (clist->alias)
				Con_Printf ("\"%s\" : \"%s\"\n", clist->alias->name, clist->alias->value);
			else if (clist->cmd)
				Con_Printf ("\"%s\" is a command\n", s);
			else if (clist->var)
				Con_Printf ("\"%s\" is a cvar\n", s);
		}
		else Con_Printf ("alias \"%s\" is not found\n", s);

		return;
	}

	if (clist)
	{
		// protect us oh lord from stupid programmer errors
		assert (clist->name);
		assert ((clist->alias || clist->cmd || clist->var));

		if (clist->alias)
		{
			// if the alias already exists we reuse it, just free the value
			HeapFree (GetProcessHeap (), 0, clist->alias->value);
			clist->alias->value = NULL;
			a = clist->alias;
		}
		else if (clist->cmd)
		{
			Con_Printf ("\"%s\" is already a command\n", s);
			return;
		}
		else if (clist->var)
		{
			Con_Printf ("\"%s\" is already a cvar\n", s);
			return;
		}
	}
	else
	{
		// create a new alias
		a = (cmdalias_t *) HeapAlloc (GetProcessHeap (), 0, sizeof (cmdalias_t));

		// this is a safe strcpy cos we define the dest size ourselves
		a->name = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (s) + 1);
		strcpy (a->name, s);

		// link it into the alias list
		a->next = cmd_alias;
		cmd_alias = a;
	}

	// ensure that we haven't missed anything or been stomped
	if (!a) return;

	// start out with a null string
	cmd[0] = 0;

	// copy the rest of the command line
	c = Cmd_Argc ();

	for (i = 2; i < c; i++)
	{
		strcat (cmd, Cmd_Argv (i));

		if (i != c)
			strcat (cmd, " ");
	}

	strcat (cmd, "\n");

	a->value = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (cmd) + 1);
	strcpy (a->value, cmd);

	// rebuild the autocomplete list
	Cmd_BuildCompletionList ();
}

void Cmd_Unalias_f (void)
{
	cmdalias_t	*a, *prev;

	switch (Cmd_Argc ())
	{
	default:
	case 1:
		Con_Printf ("unalias <name> : delete alias\n");
		break;

	case 2:
		if (!cmd_alias) return;

		for (prev = a = cmd_alias; a; a = a->next)
		{
			if (!a) break;

			if (!strcmp (Cmd_Argv (1), a->name))
			{
				prev->next = a->next;
				HeapFree (GetProcessHeap (), 0, a->value);
				HeapFree (GetProcessHeap (), 0, a->name);
				HeapFree (GetProcessHeap (), 0, a);

				// this is a dubious construct but it seems to run safe enough despite that...
				// oh well.  live fast, die young.
#pragma warning(suppress: 6001)
				prev = a;

				// rebuild the autocomplete list
				Cmd_BuildCompletionList ();

				return;
			}

			prev = a;
		}

		break;
	}
}

void Cmd_ClearAlias_f (void)
{
	cmdalias_t	*blah;

	while (cmd_alias)
	{
		blah = cmd_alias->next;
		HeapFree (GetProcessHeap (), 0, cmd_alias->value);
		HeapFree (GetProcessHeap (), 0, cmd_alias->name);
		HeapFree (GetProcessHeap (), 0, cmd_alias);
		cmd_alias = blah;
	}

	cmd_alias = NULL;

	// rebuild the autocomplete list
	Cmd_BuildCompletionList ();
}


cmd_t unaliasall_cmd ("unaliasall", Cmd_ClearAlias_f);
cmd_t unalias_cmd ("unalias", Cmd_Unalias_f);

/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

#define	MAX_ARGS		80

static	int			cmd_argc;
static	char		*cmd_argv[MAX_ARGS];
static	char		*cmd_null_string = "";
static	char		*cmd_args = NULL;

cmd_source_t	cmd_source;


/*
============
Cmd_Init
============
*/
cmd_t Cmd_StuffCmds_Cmd ("stuffcmds", Cmd_StuffCmds_f);
cmd_t Cmd_Exec_Cmd ("exec", Cmd_Exec_f);
cmd_t Cmd_Echo_Cmd ("echo", Cmd_Echo_f);
cmd_t Cmd_Alias_Cmd ("alias", Cmd_Alias_f);
cmd_t Cmd_ForwardToServer_Cmd ("cmd", Cmd_ForwardToServer);
cmd_t Cmd_Wait_Cmd ("wait", Cmd_Wait_f);

void Cmd_Init (void)
{
	// all our cvars and cmds are up now, so we build the sorted autocomplete list
	// this can be dynamically rebuilt at run time; e.g. if a new alias is added
	Cmd_BuildCompletionList ();
}


/*
============
Cmd_Argc
============
*/
int Cmd_Argc (void)
{
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
char *Cmd_Argv (int arg)
{
	if ((unsigned) arg >= MAX_ARGS) return cmd_null_string;
	if ((unsigned) arg >= cmd_argc) return cmd_null_string;

	return cmd_argv[arg];
}


/*
============
Cmd_Args
============
*/
char *Cmd_Args (void)
{
	return cmd_args;
}


/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
============
*/
void Cmd_TokenizeString (char *text)
{
	// the maximum com_token is 1024 so the command buffer will never be larger than this
	static char argbuf[MAX_ARGS * (1024 + 1)];
	char *currarg = argbuf;

	cmd_argc = 0;
	cmd_args = NULL;

	for (;;)
	{
		// skip whitespace up to a /n
		while (*text && *text <= ' ' && *text != '\n')
			text++;

		if (*text == '\n')
		{
			// a newline seperates commands in the buffer
			text++;
			break;
		}

		if (!*text) return;
		if (cmd_argc == 1) cmd_args = text;
		if (!(text = COM_Parse (text))) return;

		if (cmd_argc < MAX_ARGS)
		{
			cmd_argv[cmd_argc] = currarg;
			strcpy (cmd_argv[cmd_argc], com_token);
			currarg += strlen (com_token) + 1;
			cmd_argc++;
		}
	}
}


/*
============
Cmd_Add
============
*/
void Cmd_Add (cmd_t *newcmd)
{
	// fail if the command is a variable name
	if (cvar_t::VariableString (newcmd->name)[0]) return;

	// fail if the command already exists
	for (cmd_t *cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strcmp (newcmd->name, cmd->name))
		{
			// silent fail
			return;
		}
	}

	if (!newcmd->function)
		newcmd->function = Cmd_NullFunction;

	// link in
	newcmd->next = cmd_functions;
	cmd_functions = newcmd;
}


/*
============
Cmd_Exists
============
*/
bool Cmd_Exists (char *cmd_name)
{
	cmd_t	*cmd;

	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strcmp (cmd_name, cmd->name))
			return true;
	}

	return false;
}


/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
void Cmd_ExecuteString (char *text, cmd_source_t src)
{
	cmd_source = src;
	Cmd_TokenizeString (text);

	// execute the command line
	// check for tokens
	if (!Cmd_Argc ()) return;

	// run a binary search for faster comparison
	// reuse the autocomplete list for this as it's already a sorted array
	complist_t key = {cmd_argv[0], NULL, NULL, NULL};
	complist_t *clist = (complist_t *) bsearch (&key, complist, numcomplist, sizeof (complist_t), CmdCvarCompareFunc);

	if (!clist)
	{
		// only complain if we're up fully
		if (full_initialized)
			Con_Printf ("Unknown command \"%s\"\n", Cmd_Argv (0));

		return;
	}
	else
	{
		assert (clist->name);
		assert ((clist->alias || clist->cmd || clist->var));

		if (clist->cmd)
		{
			if (full_initialized)
			{
				// execute normally
				clist->cmd->function ();
				return;
			}
			else
			{
				if (!_stricmp (clist->cmd->name, "exec"))
				{
					// allow exec commands before everything comes up as they can call
					// into other configs which also store cvars
					clist->cmd->function ();
					return;
				}
			}
		}
		else if (clist->alias)
		{
			Cbuf_InsertText (clist->alias->value);
			return;
		}
		else if (clist->var)
		{
			// skip nehahra cvars if we're not running nehahra
			if (!nehahra && (clist->var->usage & CVAR_NEHAHRA))
			{
				Con_Printf ("Unknown command \"%s\"\n", clist->var->name);
				return;
			}

			// perform a variable print or set
			if (Cmd_Argc () == 1)
				Con_Printf ("\"%s\" is \"%s\" (default \"%s\")\n", clist->var->name, clist->var->string, clist->var->defaultvalue);
			else clist->var->Set (Cmd_Argv (1));
		}
	}
}


/*
===================
Cmd_ForwardToServer

Sends the entire command line over to the server
===================
*/
// JPG - added these for %r formatting
cvar_t	pq_needrl ("pq_needrl", "I need RL", CVAR_ARCHIVE);
cvar_t	pq_haverl ("pq_haverl", "I have RL", CVAR_ARCHIVE);
cvar_t	pq_needrox ("pq_needrox", "I need rockets", CVAR_ARCHIVE);

// JPG - added these for %p formatting
cvar_t	pq_quad ("pq_quad", "quad", CVAR_ARCHIVE);
cvar_t	pq_pent ("pq_pent", "pent", CVAR_ARCHIVE);
cvar_t	pq_ring ("pq_ring", "eyes", CVAR_ARCHIVE);

// JPG 3.00 - added these for %w formatting
cvar_t	pq_weapons ("pq_weapons", "SSG:NG:SNG:GL:RL:LG", CVAR_ARCHIVE);
cvar_t	pq_noweapons ("pq_noweapons", "no weapons", CVAR_ARCHIVE);

void Cmd_ForwardToServer (void)
{
	if (cls.state != ca_connected)
	{
		Con_Printf ("Can't \"%s\", not connected\n", Cmd_Argv (0));
		return;
	}

	// not really connected
	if (cls.demoplayback) return;

	MSG_WriteByte (&cls.message, clc_stringcmd);

	// bypass proquake messaging for SP games
	//if (cl.maxclients > 1)
	//{
		char *src, *dst, buff[128];			// JPG - used for say/say_team formatting

		// JPG - handle say separately for formatting
		if ((!_stricmp (Cmd_Argv (0), "say") || !_stricmp (Cmd_Argv (0), "say_team")) && Cmd_Argc () > 1)
		{
			SZ_Print (&cls.message, Cmd_Argv (0));
			SZ_Print (&cls.message, " ");

			src = Cmd_Args ();
			dst = buff;

			while (*src && dst - buff < 100)
			{
				if (*src == '%')
				{
					// mh - made this case-insensitive
					switch (*++src)
					{
					case 'H':
					case 'h':
						dst += sprintf (dst, "%d", cl.stats[STAT_HEALTH]);
						break;

					case 'A':
					case 'a':
						dst += sprintf (dst, "%d", cl.stats[STAT_ARMOR]);
						break;

					case 'R':
					case 'r':
						if (cl.stats[STAT_HEALTH] > 0 && (cl.items & IT_ROCKET_LAUNCHER))
						{
							if (cl.stats[STAT_ROCKETS] < 5)
								dst += sprintf (dst, "%s", pq_needrox.string);
							else dst += sprintf (dst, "%s", pq_haverl.string);
						}
						else dst += sprintf (dst, "%s", pq_needrl.string);

						break;

					case 'L':
					case 'l':
						dst += sprintf (dst, "%s", LOC_GetLocation (cls.entities[cl.viewentity]->origin));
						break;

					case 'D':
					case 'd':
						dst += sprintf (dst, "%s", LOC_GetLocation (cl.death_location));
						break;

					case 'C':
					case 'c':
						dst += sprintf (dst, "%d", cl.stats[STAT_CELLS]);
						break;

					case 'X':
					case 'x':
						dst += sprintf (dst, "%d", cl.stats[STAT_ROCKETS]);
						break;

					case 'P':
					case 'p':
						if (cl.stats[STAT_HEALTH] > 0)
						{
							if (cl.items & IT_QUAD)
							{
								dst += sprintf (dst, "%s", pq_quad.string);

								if (cl.items & (IT_INVULNERABILITY | IT_INVISIBILITY)) *dst++ = ',';
							}

							if (cl.items & IT_INVULNERABILITY)
							{
								dst += sprintf (dst, "%s", pq_pent.string);

								if (cl.items & IT_INVISIBILITY) *dst++ = ',';
							}

							if (cl.items & IT_INVISIBILITY) dst += sprintf (dst, "%s", pq_ring.string);
						}

						break;

					case 'W':
					case 'w':	// JPG 3.00
					{
						int first = 1;
						int item;
						char *ch = pq_weapons.string;

						if (cl.stats[STAT_HEALTH] > 0)
						{
							for (item = IT_SUPER_SHOTGUN; item <= IT_LIGHTNING; item *= 2)
							{
								if (*ch != ':' && (cl.items & item))
								{
									if (!first) *dst++ = ',';

									first = 0;

									while (*ch && *ch != ':')
										*dst++ = *ch++;
								}

								// ??? should be ch++ ????
								while (*ch && *ch != ':') ch++;

								if (*ch) ch++;
								if (!*ch) break;
							}
						}

						if (first) dst += sprintf (dst, "%s", pq_noweapons.string);
					}

					break;

					case '%':
						*dst++ = '%';
						break;

					case 'T':
					case 't':
						{
							int minutes, seconds;

							if ((cl.minutes || cl.seconds) && cl.seconds < 128)
							{
								int match_time;

								if (cl.match_pause_time)
									match_time = ceil (60.0 * cl.minutes + cl.seconds - (cl.match_pause_time - cl.last_match_time));
								else match_time = ceil (60.0 * cl.minutes + cl.seconds - (cl.time - cl.last_match_time));

								minutes = match_time / 60;
								seconds = match_time - 60 * minutes;
							}
							else
							{
								seconds = (int) cl.time;
								minutes = (int) (cl.time / 60);
								seconds -= minutes * 60;

								minutes &= 511;
							}

							dst += sprintf (dst, "%d:%02d", minutes, seconds);
						}

						break;

					default:
						*dst++ = '%';
						*dst++ = *src;
						break;
					}

					if (*src) src++;
				}
				else *dst++ = *src++;
			}

			*dst = 0;
			SZ_Print (&cls.message, buff);
			return;
		}
	//}

	if (_stricmp (Cmd_Argv (0), "cmd") != 0)
	{
		SZ_Print (&cls.message, Cmd_Argv (0));
		SZ_Print (&cls.message, " ");
	}

	if (Cmd_Argc () > 1)
		SZ_Print (&cls.message, Cmd_Args ());
	else SZ_Print (&cls.message, "\n");
}


/*
================
Cmd_CheckParm

Returns the position (1 to argc-1) in the command's argument list
where the given parameter apears, or 0 if not present
================
*/

int Cmd_CheckParm (char *parm)
{
	if (!parm)
	{
		Con_DPrintf ("Cmd_CheckParm: NULL\n");
		return 0;
	}

	for (int i = 1; i < Cmd_Argc (); i++)
		if (!_stricmp (parm, Cmd_Argv (i)))
			return i;

	return 0;
}


cmd_t::cmd_t (char *cmdname, xcommand_t cmdcmd)
{
	this->name = (char *) HeapAlloc (GetProcessHeap (), 0, strlen (cmdname) + 1);

	strcpy (this->name, cmdname);

	if (cmdcmd)
		this->function = cmdcmd;
	else this->function = Cmd_NullFunction;

	// just add it
	Cmd_Add (this);
}


void Cmd_AddCommand (char *cmdname, xcommand_t cmdcmd)
{
	// allow adding commands explicitly within functions too
	cmd_t *addCommand = new cmd_t (cmdname, cmdcmd);
}

