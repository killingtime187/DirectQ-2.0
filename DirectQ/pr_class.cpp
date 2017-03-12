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
#include "pr_class.h"

// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  start
cvar_t	pr_builtin_find ("pr_builtin_find", "0", CVAR_INTERNAL);
cvar_t	pr_builtin_remap ("pr_builtin_remap", "0", CVAR_INTERNAL);
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  end


// swimmonster_start_go
// swimmonster_start
dfunction_t *ED_FindFunction (char *name);

char *pr_opnames[] =
{
	"DONE", "MUL_F", "MUL_V", "MUL_FV", "MUL_VF", "DIV", "ADD_F", "ADD_V", "SUB_F", "SUB_V", "EQ_F", "EQ_V", "EQ_S",
	"EQ_E", "EQ_FNC", "NE_F", "NE_V", "NE_S", "NE_E", "NE_FNC", "LE", "GE", "LT", "GT", "INDIRECT", "INDIRECT",
	"INDIRECT", "INDIRECT", "INDIRECT", "INDIRECT", "ADDRESS", "STORE_F", "STORE_V", "STORE_S", "STORE_ENT",
	"STORE_FLD", "STORE_FNC", "STOREP_F", "STOREP_V", "STOREP_S", "STOREP_ENT", "STOREP_FLD", "STOREP_FNC",
	"RETURN", "NOT_F", "NOT_V", "NOT_S", "NOT_ENT", "NOT_FNC", "IF", "IFNOT", "CALL0", "CALL1", "CALL2", "CALL3",
	"CALL4", "CALL5", "CALL6", "CALL7", "CALL8", "STATE", "GOTO", "AND", "OR", "BITAND", "BITOR"
};


char *PR_GlobalString (int ofs);
char *PR_GlobalStringNoContents (int ofs);


void FindEdictFieldOffsets (void);


CProgsDat::~CProgsDat (void)
{
	this->QC = NULL;
}


CProgsDat::CProgsDat (void)
{
	// set up the stack (any attempt to access the stack outside of RunInteraction is a crash case and we need to know about it)
	this->Stack = NULL;
	this->StackDepth = 0;

	// ExecuteProgram explicitly checks for this->QC non-NULL now so here we set it to NULL
	this->QC = NULL;
	this->Functions = NULL;
	this->Strings = NULL;
	this->GlobalDefs = NULL;
	this->FieldDefs = NULL;
	this->Statements = NULL;
	this->Globals = NULL;
	this->GlobalStruct = NULL;
	this->EdictSize = 0;
	this->CRC = 0;
	this->XFunction = NULL;
	this->XStatement = 0;
	this->Trace = false;
	this->Argc = 0;
	this->FishHack = false;
	this->NumFish = 0;
	this->Edicts = NULL;
	this->NumEdicts = 0;
	this->MaxEdicts = 0;
}


byte idprogs[] = {12, 62, 243, 98, 93, 245, 136, 53, 54, 248, 218, 66, 199, 139, 103, 58};

void CProgsDat::LoadProgs (char *progsname, cvar_t *overridecvar)
{
	// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  start
	int 	j;
	int		funcno;
	char	*funcname;
	byte	progshash[16];
	// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  end

	// this can't be in a constructor yet because we need the instance already created in order to do
	// FindEdictFieldOffsets without crashing.  When we get everything on the server gone to OOP we'll
	// do it right.
	// flush the non-C variable lookup cache
	for (int i = 0; i < GEFV_CACHESIZE; i++) gefvCache[i].field[0] = 0;

	CRC_Init (&this->CRC);

	// the old heap corruption errors are now gone.  yayyyy!
	if (overridecvar) this->QC = (dprograms_t *) CQuakeFile::LoadFile (overridecvar->string, MainHunk);

	if (!this->QC)
	{
		if ((this->QC = (dprograms_t *) CQuakeFile::LoadFile ("progs.dat", MainHunk)) == NULL)
		{
			Host_Error ("CProgsDat::CProgsDat: couldn't load progs.dat");
			return;
		}
	}

	// CRC the progs
	for (int i = 0; i < CQuakeFile::FileSize; i++)
		CRC_ProcessByte (&this->CRC, ((byte *) this->QC)[i]);

	this->FishHack = false;
	this->NumFish = 0;
	COM_HashData (progshash, this->QC, CQuakeFile::FileSize);

	if (!memcmp (progshash, idprogs, 16))
	{
		this->FishHack = true;
		Con_DPrintf ("hacking fish for ID progs\n");
	}

	if (this->QC->version != PROG_VERSION) Host_Error ("progs.dat has wrong version number (%i should be %i)", this->QC->version, PROG_VERSION);
	if (this->QC->crc != PROGHEADER_CRC) Host_Error ("progs.dat system vars have been modified, progdefs.h is out of date");

	// the old heap corruption errors are now gone.  whee.
	this->Functions = (dfunction_t *) ((byte *) this->QC + this->QC->ofs_functions);
	this->Strings = (char *) ((byte *) this->QC + this->QC->ofs_strings);
	this->GlobalDefs = (ddef_t *) ((byte *) this->QC + this->QC->ofs_globaldefs);
	this->FieldDefs = (ddef_t *) ((byte *) this->QC + this->QC->ofs_fielddefs);
	this->Statements = (dstatement_t *) ((byte *) this->QC + this->QC->ofs_statements);
	this->Globals = (float *) ((byte *) this->QC + this->QC->ofs_globals);

	// this just points at this->Globals (per comment above on it's declaration)
	this->GlobalStruct = (globalvars_t *) this->Globals;
	this->EdictSize = this->QC->entityfields * 4 + sizeof (edict_t) - sizeof (entvars_t);

	// init the string table
	this->StringSize = this->QC->numstrings;

	// woah fuck me this set the initial string pointing at a stack location!
	// i don't think it matters much anymore as it's no longer necessary to prime the strings at load time
	static char *initstring = "";
	this->SetString (initstring);

	// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  start
	// initialize function numbers for PROGS.DAT
	pr_numbuiltins = 0;
	pr_builtins = NULL;

	if (pr_builtin_remap.value)
	{
		// remove all previous assigned function numbers
		for (j = 1; j < pr_ebfs_numbuiltins; j++)
			pr_ebfs_builtins[j].funcno = 0;
	}
	else
	{
		// use default function numbers
		for (j = 1; j < pr_ebfs_numbuiltins; j++)
		{
			pr_ebfs_builtins[j].funcno = pr_ebfs_builtins[j].default_funcno;

			// determine highest builtin number (when NOT remapped)
			if (pr_ebfs_builtins[j].funcno > pr_numbuiltins) pr_numbuiltins = pr_ebfs_builtins[j].funcno;
		}
	}
	// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  end

	for (int i = 0; i < this->QC->numfunctions; i++)
	{
		// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  start
		if (pr_builtin_remap.value)
		{
			if (this->Functions[i].first_statement < 0)	// builtin function
			{
				funcno = -this->Functions[i].first_statement;
				funcname = SVProgs->GetString (this->Functions[i].s_name);

				// search function name
				for (j = 1; j < pr_ebfs_numbuiltins; j++)
					if (!(_stricmp (funcname, pr_ebfs_builtins[j].funcname)))
						break;	// found

				if (j < pr_ebfs_numbuiltins)	// found
					pr_ebfs_builtins[j].funcno = funcno;
				else Con_DPrintf ("Can not assign builtin number #%i to %s - function unknown\n", funcno, funcname);
			}
		}

		// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  end
	}

	// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  start
	if (pr_builtin_remap.value)
	{
		// check for unassigned functions and try to assign their default function number
		for (int i = 1; i < pr_ebfs_numbuiltins; i++)
		{
			if ((!pr_ebfs_builtins[i].funcno) && (pr_ebfs_builtins[i].default_funcno))	// unassigned and has a default number
			{
				// check if default number is already assigned to another function
				for (j = 1; j < pr_ebfs_numbuiltins; j++)
					if (pr_ebfs_builtins[j].funcno == pr_ebfs_builtins[i].default_funcno)
						break;	// number already assigned to another builtin function

				if (j < pr_ebfs_numbuiltins)	// already assigned
				{
					Con_DPrintf
					(
						"Can not assign default builtin number #%i to %s - number is already assigned to %s\n",
						pr_ebfs_builtins[i].default_funcno, pr_ebfs_builtins[i].funcname, pr_ebfs_builtins[j].funcname
					);
				}
				else pr_ebfs_builtins[i].funcno = pr_ebfs_builtins[i].default_funcno;
			}

			// determine highest builtin number (when remapped)
			if (pr_ebfs_builtins[i].funcno > pr_numbuiltins) pr_numbuiltins = pr_ebfs_builtins[i].funcno;
		}
	}

	pr_numbuiltins++;

	// allocate and initialize builtin list for execution time
	pr_builtins = (builtin_t *) MainHunk->Alloc (pr_numbuiltins * sizeof (builtin_t));

	for (int i = 0; i < pr_numbuiltins; i++)
		pr_builtins[i] = pr_ebfs_builtins[0].function;

	// create builtin list for execution time and set cvars accordingly
	pr_builtin_find.Set (0.0f);
	pr_checkextension.Set (0.0f);

	for (j = 1; j < pr_ebfs_numbuiltins; j++)
	{
		if (pr_ebfs_builtins[j].funcno)	// only put assigned functions into builtin list
			pr_builtins[pr_ebfs_builtins[j].funcno] = pr_ebfs_builtins[j].function;

		if (pr_ebfs_builtins[j].default_funcno == PR_DEFAULT_FUNCNO_BUILTIN_FIND)
			pr_builtin_find.Set (pr_ebfs_builtins[j].funcno);

		// 2001-10-20 Extension System by Lord Havoc/Maddes (DP compatibility)  start
		if (pr_ebfs_builtins[j].default_funcno == PR_DEFAULT_FUNCNO_EXTENSION_FIND)
			pr_checkextension.Set (pr_ebfs_builtins[j].funcno);

		// 2001-10-20 Extension System by Lord Havoc/Maddes (DP compatibility)  end
	}
	// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes/Firestorm  end

	FindEdictFieldOffsets ();
}


void CProgsDat::RunInteraction (edict_t *self, edict_t *other, func_t fnum)
{
	if (this->GlobalStruct)
	{
		// get temp mark at start of execution
		int hunkmark = TempHunk->GetLowMark ();

		// save out the old stack so that we can restore it when done
		prstack_t *oldstack = this->Stack;
		int olddepth = this->StackDepth;

		// go to a new stack (needs a memset 0 so it's local pointers are NULLed properly)
		this->Stack = (prstack_t *) TempHunk->Alloc (MAX_STACK_DEPTH * sizeof (prstack_t));
		this->StackDepth = 0;

		// only set up stuff that has values
		if (self) this->GlobalStruct->self = EdictToProg (self);
		if (other) this->GlobalStruct->other = EdictToProg (other);
		if (fnum) this->ExecuteProgram (fnum);

		// restore the old stack
		this->Stack = oldstack;
		this->StackDepth = olddepth;

		// definitively release any stack local memory
		// (this will be reclaimed at the end of each frame anyway)
		TempHunk->FreeToLowMark (hunkmark);
	}
}


void CProgsDat::ExecuteProgram (func_t fnum)
{
	// ha!
	if (!this->QC) return;

	edict_t *ed = NULL;
	eval_t *ptr;

	if (!fnum || fnum < 0 || fnum >= this->QC->numfunctions)
	{
		if (this->GlobalStruct->self)
			ED_Print (ProgToEdict (this->GlobalStruct->self));

		if (!fnum)
			Host_Error ("CProgsDat::ExecuteProgram: NULL function");
		else if (fnum < 0)
			Host_Error ("CProgsDat::ExecuteProgram: fnum < 0");
		else Host_Error ("CProgsDat::ExecuteProgram: fnum >= this->QC->numfunctions");
	}

	dfunction_t *f = &this->Functions[fnum];
	int runaway = 5000000;
	dfunction_t *newf = NULL;

	this->Trace = false;

	int exitdepth = this->StackDepth;
	int s = this->EnterFunction (f);

	for (;;)
	{
		s++;	// next statement

		if (s >= this->QC->numstatements) Host_Error ("CProgsDat::ExecuteProgram: s >= this->QC->numstatements");

		this->XStatement = s;
		this->XFunction->profile++;

		dstatement_t *st = &this->Statements[s];

		eval_t *a = (eval_t *) &this->Globals[st->a];
		eval_t *b = (eval_t *) &this->Globals[st->b];
		eval_t *c = (eval_t *) &this->Globals[st->c];

		if (!--runaway) this->RunError ("runaway loop error %d");
		if (this->Trace) this->PrintStatement (st);

		switch (st->op)
		{
		case OP_ADD_F:
			c->_float = a->_float + b->_float;
			break;

		case OP_ADD_V:
			Vector3Add (c->vector, a->vector, b->vector);
			break;

		case OP_SUB_F:
			c->_float = a->_float - b->_float;
			break;

		case OP_SUB_V:
			Vector3Subtract (c->vector, a->vector, b->vector);
			break;

		case OP_MUL_F:
			c->_float = a->_float * b->_float;
			break;

		case OP_MUL_V:
			c->_float = Vector3Dot (a->vector, b->vector);
			break;

		case OP_MUL_FV:
			Vector3Scale (c->vector, b->vector, a->_float);
			break;

		case OP_MUL_VF:
			Vector3Scale (c->vector, a->vector, b->_float);
			break;

		case OP_DIV_F:
			c->_float = a->_float / b->_float;
			break;

		case OP_BITAND:
			c->_float = (int) a->_float & (int) b->_float;
			break;

		case OP_BITOR:
			c->_float = (int) a->_float | (int) b->_float;
			break;

		case OP_GE:
			c->_float = a->_float >= b->_float;
			break;

		case OP_LE:
			c->_float = a->_float <= b->_float;
			break;

		case OP_GT:
			c->_float = a->_float > b->_float;
			break;

		case OP_LT:
			c->_float = a->_float < b->_float;
			break;

		case OP_AND:
			c->_float = a->_float && b->_float;
			break;

		case OP_OR:
			c->_float = a->_float || b->_float;
			break;

		case OP_NOT_F:
			c->_float = !a->_float;
			break;

		case OP_NOT_V:
			c->_float = !a->vector[0] && !a->vector[1] && !a->vector[2];
			break;

		case OP_NOT_S:
			c->_float = !a->string || !(SVProgs->GetString (a->string))[0];
			break;

		case OP_NOT_FNC:
			c->_float = !a->function;
			break;

		case OP_NOT_ENT:
			c->_float = (ProgToEdict (a->edict) == this->Edicts);
			break;

		case OP_EQ_F:
			c->_float = a->_float == b->_float;
			break;

		case OP_EQ_V:
			c->_float = Vector3Compare (a->vector, b->vector) ? 1 : 0;
			break;

		case OP_EQ_S:
			c->_float = !strcmp (SVProgs->GetString (a->string), SVProgs->GetString (b->string));
			break;

		case OP_EQ_E:
			c->_float = a->_int == b->_int;
			break;

		case OP_EQ_FNC:
			c->_float = a->function == b->function;
			break;

		case OP_NE_F:
			c->_float = a->_float != b->_float;
			break;

		case OP_NE_V:
			c->_float = Vector3Compare (a->vector, b->vector) ? 0 : 1;
			break;

		case OP_NE_S:
			c->_float = strcmp (SVProgs->GetString (a->string), SVProgs->GetString (b->string));
			break;

		case OP_NE_E:
			c->_float = a->_int != b->_int;
			break;

		case OP_NE_FNC:
			c->_float = a->function != b->function;
			break;

		case OP_STORE_V:
			Vector3Copy (b->vector, a->vector);
			break;

		case OP_STORE_F:
		case OP_STORE_ENT:
		case OP_STORE_FLD:		// integers
		case OP_STORE_S:
		case OP_STORE_FNC:		// pointers
			b->_int = a->_int;
			break;

		case OP_STOREP_F:
		case OP_STOREP_ENT:
		case OP_STOREP_FLD:		// integers
		case OP_STOREP_S:
		case OP_STOREP_FNC:		// pointers
			ptr = (eval_t *) ((byte *) this->Edicts + b->_int);
			ptr->_int = a->_int;
			break;

		case OP_STOREP_V:
			ptr = (eval_t *) ((byte *) this->Edicts + b->_int);
			Vector3Copy (ptr->vector, a->vector);
			break;

		case OP_ADDRESS:
			ed = ProgToEdict (a->edict);

			if (ed == (edict_t *) this->Edicts && sv.state == ss_active)
				this->RunError ("CProgsDat::ExecuteProgram: assignment to world entity");

			c->_int = (byte *) ((int *) &ed->v + b->_int) - (byte *) this->Edicts;
			break;

		case OP_LOAD_F:
		case OP_LOAD_FLD:
		case OP_LOAD_ENT:
		case OP_LOAD_S:
		case OP_LOAD_FNC:
			ed = ProgToEdict (a->edict);
			a = (eval_t *) ((int *) &ed->v + b->_int);
			c->_int = a->_int;
			break;

		case OP_LOAD_V:
			ed = ProgToEdict (a->edict);
			a = (eval_t *) ((int *) &ed->v + b->_int);
			Vector3Copy (c->vector, a->vector);
			break;

			//==================

		case OP_IFNOT:
			if (!a->_int)
				s += (signed short) st->b - 1;	// offset the s++

			break;

		case OP_IF:
			if (a->_int)
				s += (signed short) st->b - 1;	// offset the s++

			break;

		case OP_GOTO:
			s += (signed short) st->a - 1;	// offset the s++
			break;

		case OP_CALL0:
		case OP_CALL1:
		case OP_CALL2:
		case OP_CALL3:
		case OP_CALL4:
		case OP_CALL5:
		case OP_CALL6:
		case OP_CALL7:
		case OP_CALL8:
			this->Argc = st->op - OP_CALL0;

			if (!a->function)
			{
				if ((st - 1)->op == OP_LOAD_FNC) // OK?
					ED_Print (ed); // Print owner edict, if any
				else if (this->GlobalStruct->self)
					ED_Print (ProgToEdict (this->GlobalStruct->self));

				this->RunError ("PR_ExecuteProgram2: NULL function");
			}

			newf = &this->Functions[a->function];

			if (newf->first_statement < 0)
			{
				// negative statements are built in functions
				int i = -newf->first_statement;

				if (i >= pr_numbuiltins)
					this->RunError ("CProgsDat::ExecuteProgram: bad builtin call number (%d, max = %d)", i, pr_numbuiltins);

				pr_builtins[i] ();
				break;
			}

			s = this->EnterFunction (newf);
			break;

		case OP_DONE:
		case OP_RETURN:
			Vector3Copy (&this->Globals[OFS_RETURN], &this->Globals[st->a]);
			s = this->LeaveFunction ();

			// all done
			if (this->StackDepth == exitdepth)
				return;

			break;

		case OP_STATE:
			ed = ProgToEdict (this->GlobalStruct->self);
			ed->v.nextthink = this->GlobalStruct->time + 0.1;

			if (a->_float != ed->v.frame)
				ed->v.frame = a->_float;

			ed->v.think = b->function;
			break;

		default:
			this->RunError ("CProgsDat::ExecuteProgram: bad opcode %i", st->op);
		}
	}
}


int CProgsDat::EnterFunction (dfunction_t *f)
{
	if (!this->Stack) this->RunError ("CProgsDat::EnterFunction: called with NULL stack");

	// get the current local stack
	prstack_t *stack = this->Stack + this->StackDepth;

	if ((++this->StackDepth) >= MAX_STACK_DEPTH) this->RunError ("CProgsDat::EnterFunction: stack overflow (%d, max = %d)", this->StackDepth, MAX_STACK_DEPTH - 1);

	stack->s = this->XStatement;
	stack->f = this->XFunction;

	// new memory management for stack
	// tested with Quoth, hipnotic, marcher, bastion, 400 kniggits
	if (f->locals)
	{
		stack->hunkmark = TempHunk->GetLowMark ();
		stack->locals = (int *) TempHunk->FastAlloc (f->locals * sizeof (int));

		for (int i = 0, o = f->parm_start; i < f->locals; i++)
			stack->locals[i] = ((int *) this->Globals)[o++];

		// Con_Printf ("pushed %i locals\n", f->locals);
	}
	else
	{
		stack->hunkmark = 0;
		stack->locals = NULL;
	}

	// the new stack gets no saved locals (yet) (because it's new!)
	this->Stack[this->StackDepth].locals = NULL;
	this->Stack[this->StackDepth].hunkmark = 0;

	// Con_Printf ("stack at %i\n", this->StackDepth);

	// copy parameters
	for (int i = 0, o = f->parm_start; i < f->numparms; i++)
		for (int j = 0, k = i * 3; j < f->parm_size[i]; j++, k++)
			((int *) this->Globals)[o++] = ((int *) this->Globals)[OFS_PARM0 + k];

	this->XFunction = f;

	return f->first_statement - 1;	// offset the s++
}


int CProgsDat::LeaveFunction (void)
{
	if (!this->Stack) this->RunError ("CProgsDat::LeaveFunction: called with NULL stack");
	if (this->StackDepth <= 0) Host_Error ("CProgsDat::LeaveFunction: prog stack underflow");

	// up stack
	this->StackDepth--;

	// restore locals from the stack
	prstack_t *stack = this->Stack + this->StackDepth;

	if (stack->locals)
	{
		int c = this->XFunction->locals;

		for (int i = 0, o = this->XFunction->parm_start; i < c; i++)
			((int *) this->Globals)[o++] = stack->locals[i];

		TempHunk->FreeToLowMark (stack->hunkmark);
		stack->locals = NULL;
	}

	this->XFunction = stack->f;

	return stack->s;
}


void CProgsDat::PrintStatement (dstatement_t *s)
{
	if ((unsigned) s->op < ARRAYLENGTH (pr_opnames))
		Con_SafePrintf ("%-10s ", pr_opnames[s->op]);

	if (s->op == OP_IF || s->op == OP_IFNOT)
		Con_SafePrintf ("%sbranch %i", PR_GlobalString (s->a), s->b);
	else if (s->op == OP_GOTO)
		Con_SafePrintf ("branch %i", s->a);
	else if ((unsigned) (s->op - OP_STORE_F) < 6)
	{
		Con_SafePrintf ("%s", PR_GlobalString (s->a));
		Con_SafePrintf ("%s", PR_GlobalStringNoContents (s->b));
	}
	else
	{
		if (s->a) Con_SafePrintf ("%s", PR_GlobalString (s->a));
		if (s->b) Con_SafePrintf ("%s", PR_GlobalString (s->b));
		if (s->c) Con_SafePrintf ("%s", PR_GlobalStringNoContents (s->c));
	}

	Con_SafePrintf ("\n");
}


void CProgsDat::RunError (char *error, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr, error);
	_vsnprintf (string, 1024, error, argptr);
	va_end (argptr);

	this->PrintStatement (this->Statements + this->XStatement);
	this->StackTrace ();
	Con_Printf ("%s\n", string);

	// dump the stack so host_error can shutdown functions
	this->StackDepth = 0;

	QC_DebugOutput ("CProgsDat::RunError: %s", string);
	Host_Error ("Program error");
}


void CProgsDat::Profile (void)
{
	int num = 0;
	dfunction_t *best = NULL;

	do
	{
		int max = 0;

		for (int i = 0; i < this->QC->numfunctions; i++)
		{
			dfunction_t *f = &this->Functions[i];

			if (f->profile > max)
			{
				max = f->profile;
				best = f;
			}
		}

		if (best)
		{
			if (num < 10)
				Con_Printf ("%7i %s\n", best->profile, SVProgs->GetString (best->s_name));

			num++;
			best->profile = 0;
		}
	} while (best);
}


void CProgsDat::StackTrace (void)
{
	dfunction_t	*f;
	int			i;

	if (this->StackDepth == 0 || !this->Stack)
	{
		Con_Printf ("<NO STACK>\n");
		return;
	}

	if (this->StackDepth > MAX_STACK_DEPTH) this->StackDepth = MAX_STACK_DEPTH;

	this->Stack[this->StackDepth].s = this->XStatement;
	this->Stack[this->StackDepth].f = this->XFunction;

	for (i = this->StackDepth; i >= 0; i--)
	{
		f = this->Stack[i].f;

		if (f)
		{
			Con_Printf
			(
				"%12s : %s statement %i\n",
				SVProgs->GetString (f->s_file),
				SVProgs->GetString (f->s_name),
				this->Stack[i].s - f->first_statement
			);
		}
		else Con_Printf ("<NO FUNCTION>\n");
	}
}


// =====================================================================================================================
// STRINGS

// because of the way QC packs string pointers into ints we can just use the pointers directly instead.
// this is not 64-bit safe as it assumes a pointer is 32-bits, but that's a "feature" of the VM anyway.
// special case is a string that lies within the string table; if that happens we need to offset into
// the table instead of accessing the pointer directly.  this *IS* safe because strings used elsewhere
// won't share the same memory locations as the string table in the process address space.  we don't
// bother fixing this up at load time because function names, globaldefs and random junk in the globals
// table could also be pointing into the string table (the last one happens in Nehahra and crashes).

// let's handle string encode/decode in a manner that doesn't make small children cry
union strencode_t
{
	char *str;
	int num;
};


int PR_EncodeString (char *str)
{
	strencode_t enc;
	enc.str = str;
	return enc.num;
}


char *PR_DecodeString (int num)
{
	strencode_t enc;
	enc.num = num;
	return enc.str;
}


int CProgsDat::AllocString (int size, char **ptr)
{
	if (!size)
		return 0;
	else if (ptr)
	{
		ptr[0] = (char *) MainHunk->Alloc (size);
		return PR_EncodeString (ptr[0]);
	}
	else return 0;
}


char *CProgsDat::GetString (int num)
{
	if (!num)
	{
		// same as this->Strings[0] and a general sanity check...
		static char *nostring = "";
		return nostring;
	}
	else if (num >= 0 && num < this->StringSize)
		return this->Strings + num;
	else return PR_DecodeString (num);
}


int CProgsDat::SetString (char *s)
{
	if (!s)
		return 0;
	else if (s >= this->Strings && s <= this->Strings + this->StringSize - 2)
		return (int) (s - this->Strings);
	else return PR_EncodeString (s);
}

