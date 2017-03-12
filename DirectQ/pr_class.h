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


#pragma once
#ifndef PR_CLASS_H
#define PR_CLASS_H

#define GEFV_CACHESIZE	16	// bumped for more cached values
#define	MAX_FIELD_LEN	64

struct gefv_cache
{
	ddef_t	*pcache;
	char	field[MAX_FIELD_LEN];
};

extern gefv_cache gefvCache[];

struct prstack_t
{
	int s;
	dfunction_t *f;
	int hunkmark;
	int *locals;
};

// because each ExecuteProgram gets it's own stack this can be kept small
#define	MAX_STACK_DEPTH		64


class CProgsDat
{
public:
	// default progs def
	dprograms_t		*QC;
	dfunction_t		*Functions;
	char			*Strings;
	ddef_t			*FieldDefs;
	ddef_t			*GlobalDefs;
	dstatement_t	*Statements;
	globalvars_t	*GlobalStruct;
	float			*Globals;			// same as SVProgs->GlobalStruct
	int				EdictSize;	// in bytes
	unsigned short	CRC;

	// string handling
	int StringSize;

	int AllocString (int bufferlength, char **ptr);
	char *GetString (int num);
	int SetString (char *s);

	bool Trace;
	int Argc;

	CProgsDat (void);
	~CProgsDat (void);

	void LoadProgs (char *progsname, cvar_t *overridecvar);

	// execution
	void RunInteraction (edict_t *self, edict_t *other, func_t fnum);
	void PrintStatement (dstatement_t *s);
	void RunError (char *error, ...);

	void Profile (void);
	void StackTrace (void);

	bool	FishHack;
	int		NumFish;

	// the edicts more properly belong to the progs than to the server
	edict_t *Edicts;
	int NumEdicts;
	int MaxEdicts;

	// to do - make this private
	dfunction_t *XFunction;

private:
	// progs execution stack
	// should not be accessed from elsewhere
	prstack_t *Stack;
	int StackDepth;

	int XStatement;

	void ExecuteProgram (func_t fnum);
	int EnterFunction (dfunction_t *f);
	int LeaveFunction (void);
};

extern CProgsDat *SVProgs;

#endif // PR_CLASS_H