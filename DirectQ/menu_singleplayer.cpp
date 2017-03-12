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
#include "menu_common.h"
#include "winquake.h"


cvar_t host_savenamebase ("host_savenamebase", "save_", CVAR_ARCHIVE);
extern char *SkillNames[];

extern CQMenu menu_Maps;
extern CQMenu menu_Demo;


/*
========================================================================================================================

					SINGLE PLAYER MENU

========================================================================================================================
*/

void Menu_SPNewGame (void)
{
	if (sv.active)
	{
		if (!SCR_ModalMessage ("Are you sure you want to\nstart a new game?\n", "Confirm New Game", MB_YESNO))
			return;
	}

	key_dest = key_game;

	if (sv.active) Cbuf_AddText ("disconnect\n");

	// ensure cvars are appropriate for SP
	deathmatch.Set (0.0f);
	coop.Set (0.0f);
	teamplay.Set (0.0f);

	// switch back to skill 1
	skill.Set (1.0f);

	Cbuf_AddText ("maxplayers 1\n");

	// different start map (BASTARDS!)
	if (nehahra)
		Cbuf_AddText ("map nehstart\n");
	else Cbuf_AddText ("map start\n");

	Cbuf_Execute ();
}


void Menu_InitSPMenu (void)
{
	extern qpic_t *gfx_sp_menu_lmp;
	extern qpic_t *gfx_ttl_sgl_lmp;

	menu_Singleplayer.AddOption (new CQMenuBanner (&gfx_ttl_sgl_lmp));
	menu_Singleplayer.AddOption (new CQMenuSpacer (DIVIDER_LINE));
	menu_Singleplayer.AddOption (new CQMenuCursorSubMenu (Menu_SPNewGame));
	menu_Singleplayer.AddOption (new CQMenuCursorSubMenu (&menu_Load));
	menu_Singleplayer.AddOption (new CQMenuCursorSubMenu (&menu_Save));
	menu_Singleplayer.AddOption (new CQMenuChunkyPic (&gfx_sp_menu_lmp));
}


/*
========================================================================================================================

					SAVE/LOAD MENUS

========================================================================================================================
*/

extern cvar_t host_savedir;

#define MAX_SAVE_DISPLAY	20
int NumSaves = 0;

bool savelistchanged = true;

CScrollBoxProvider *SaveScrollbox = NULL;
CScrollBoxProvider *LoadScrollbox = NULL;
CScrollBoxProvider *ActiveScrollbox = NULL;

void Menu_SaveLoadOnDraw (int y, int itemnum);
void Menu_SaveLoadOnHover (int initialy, int y, int itemnum);
void Menu_SaveLoadOnEnter (int itemnum);
void Menu_SaveLoadOnDelete (int itemnum);


struct save_game_info_t
{
	char mapname[SAVEGAME_COMMENT_LENGTH + 1];
	char kills[64];
	int skill;
	char time[64];
	char secrets[64];
	char savetime[64];
	char filename[MAX_PATH];
};


// the old version was written before i was really comfortable using COM_Parse and dates back quite a few years
void Menu_ParseSaveInfo (std::ifstream &f, char *filename, save_game_info_t *si)
{
	// blank the save info
	memset (si, 0, sizeof (save_game_info_t));

	if (!filename) return;

	// copy the file name
	strcpy (si->filename, filename);

	// set up intial kills and secrets
	si->kills[0] = '0';
	si->secrets[0] = '0';

	// the version was already checked coming into here so first thing we get is the savegame comment
	// the header plus globalvars should never exceed 32 k
	const int LOADGAME_BUFFER_SIZE = COM_GetFileSize (f);
	int hunkmark = TempHunk->GetLowMark ();
	char *loadbuffer = (char *) TempHunk->FastAlloc (LOADGAME_BUFFER_SIZE);

	// read the comment
	f.getline (loadbuffer, LOADGAME_BUFFER_SIZE);

	// hack out the level name
	for (int i = strlen (loadbuffer); i >= 0; i--)
	{
		// convert '_' back to ' '
		if (loadbuffer[i] == '_') loadbuffer[i] = ' ';

		// null term after map name
		if (!_strnicmp (&loadbuffer[i], "kills:", 6)) loadbuffer[i] = 0;
	}

	// trim trailing spaces
	for (int i = strlen (loadbuffer) - 1; i >= 0; i--)
	{
		if (loadbuffer[i] != ' ')
		{
			loadbuffer[i + 1] = 0;
			break;
		}
	}

	// copy in the map name
	Q_strncpy (si->mapname, loadbuffer, SAVEGAME_COMMENT_LENGTH);

	// these exist to soak up data we skip over
	float fsoak;

	// skip spawn parms
	for (int i = 0; i < NUM_SPAWN_PARMS; i++)
	{
		f >> fsoak;
		f.ignore (LOADGAME_BUFFER_SIZE, '\n');
	}

	// skill is up next
	// read skill as a float, and convert to int
	f >> fsoak;
	f.ignore (LOADGAME_BUFFER_SIZE, '\n');
	si->skill = (int) (fsoak + 0.1);

	// sanity check (in case the save is manually hacked...)
	if (si->skill > 3) si->skill = 3;
	if (si->skill < 0) si->skill = 0;

	// read bsp mapname
	f.getline (loadbuffer, LOADGAME_BUFFER_SIZE);

	if (!si->mapname[0] || si->mapname[0] == 32)
	{
		// not every map has a friendly name
		Q_strncpy (si->mapname, loadbuffer, SAVEGAME_COMMENT_LENGTH);
		si->mapname[22] = 0;
	}

	// now read time as a float
	f >> fsoak;
	f.ignore (LOADGAME_BUFFER_SIZE, '\n');

	// convert fsoak time to real time
	Q_snprintf (si->time, 64, "%02i:%02i", ((int) fsoak) / 60, (int) fsoak - (((int) fsoak) / 60) * 60);

	// skip lightstyles
	for (int i = 0; i < MAX_LIGHTSTYLES; i++)
		f.getline (loadbuffer, LOADGAME_BUFFER_SIZE);

	// init counts
	int num_kills = 0;
	int total_kills = 0;
	int num_secrets = 0;
	int total_secrets = 0;
	char keyname[64];
	char *data = loadbuffer;

	// read in all the edicts so that we can parse globals out of them
	memset (data, 0, LOADGAME_BUFFER_SIZE);
	f.read (data, LOADGAME_BUFFER_SIZE);

	// parse out the opening brace
	if ((data = COM_Parse (data)) != NULL)
	{
		if (com_token[0])
		{
			if (!strcmp (com_token, "{"))
			{
				// parse out the globals (which will be the first edict)
				// unlike doing it for real, we just break on error rather than sys_error
				for (;;)
				{
					// parse key
					if ((data = COM_Parse (data)) == NULL) break;
					if (com_token[0] == '}') break;
					if (!data) break;

					Q_strncpy (keyname, com_token, 63);

					// parse value
					if (!(data = COM_Parse (data))) break;
					if (com_token[0] == '}') break;

					// interpret - these are stored as floats in the save file
					if (!_stricmp (keyname, "total_secrets")) total_secrets = (int) (atof (com_token) + 0.1f);
					if (!_stricmp (keyname, "found_secrets")) num_secrets = (int) (atof (com_token) + 0.1f);
					if (!_stricmp (keyname, "total_monsters")) total_kills = (int) (atof (com_token) + 0.1f);
					if (!_stricmp (keyname, "killed_monsters")) num_kills = (int) (atof (com_token) + 0.1f);
				}
			}
		}
	}

	// write out counts
	if (total_kills)
		Q_snprintf (si->kills, 63, "%i/%i", num_kills, total_kills);
	else Q_snprintf (si->kills, 63, "%i", num_kills);

	if (total_secrets)
		Q_snprintf (si->secrets, 63, "%i/%i", num_secrets, total_secrets);
	else Q_snprintf (si->secrets, 63, "%i", num_secrets);

	// because f came into here already opened
	f.close ();
	TempHunk->FreeToLowMark (hunkmark);

	// now we set up the time and date of the save file for display
	char name2[256];
	CQuakeFile savefile;

	// set up the file name for opening
	Q_snprintf (name2, 255, "%s/%s%s", com_gamedir, host_savedir.string, filename);

	// open it again to get the time it was saved at
	savefile.Open (name2);
	savefile.GetFileTime (si->savetime);
	savefile.Close ();
}


class CSaveInfo
{
public:
	CSaveInfo (std::ifstream &f, char *filename)
	{
		Menu_ParseSaveInfo (f, filename, &this->SaveInfo);
		NumSaves++;
	}

	void UpdateItem (void)
	{
		if (!this->SaveInfo.filename[0]) return;

		// update anything that could change (fixme - also update time when we fix it up above)
		Q_snprintf (this->SaveInfo.mapname, 40, "%s", cl.levelname);

		// remake quake compatibility
		if (cl.stats[STAT_TOTALMONSTERS] == 0)
			Q_snprintf (this->SaveInfo.kills, 64, "%i", cl.stats[STAT_MONSTERS]);
		else Q_snprintf (this->SaveInfo.kills, 64, "%i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);

		// remake quake compatibility
		if (cl.stats[STAT_TOTALSECRETS] == 0)
			Q_snprintf (this->SaveInfo.secrets, 64, "%i", cl.stats[STAT_SECRETS]);
		else Q_snprintf (this->SaveInfo.secrets, 64, "%i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);

		this->SaveInfo.skill = (int) skill.value;

		int seconds = (int) cl.time;
		int minutes = (int) (cl.time / 60);
		seconds -= minutes * 60;

		Q_snprintf (this->SaveInfo.time, 64, "%02i:%02i", minutes, seconds);
	}

	CSaveInfo (void)
	{
		strcpy (this->SaveInfo.mapname, "<< New SaveGame >>");

		// hack - we'll use this to identift this item in the OnHover function
		this->SaveInfo.filename[0] = 0;
		NumSaves++;
	}

	~CSaveInfo ()
	{
		// cascade destructors along the list
		SAFE_DELETE (this->Next);
	}

	save_game_info_t SaveInfo;
	CSaveInfo *Next;
};


CSaveInfo *SaveInfoList = NULL;
CSaveInfo **SaveInfoArray = NULL;
CSaveInfo **ActiveSaveInfoArray = NULL;


void Menu_SaveLoadAddSave (WIN32_FIND_DATA *savefile)
{
	// not interested in these types
	if (savefile->dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) return;
	if (savefile->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return;

	// attempt to open it
	std::ifstream f (va ("%s\\save\\%s", com_gamedir, savefile->cFileName));

	// we don't expect this to fail
	if (f.is_open ())
	{
		int version;

		// the value of this doesn't matter for allocs, it's just important that it's big enough to enable skipping over some whitespace
		const int LOADGAME_BUFFER_SIZE = (1024 * 32);

		f >> std::noskipws;
		f >> version;
		f.ignore (LOADGAME_BUFFER_SIZE, '\n');

		// check the version
		if (version != SAVEGAME_VERSION)
		{
			f.close ();
			return;
		}

		CSaveInfo *si = new CSaveInfo (f, savefile->cFileName);

		// CSaveInfo constructor should close f via Menu_ParseSaveInfo
		if (f.is_open ()) f.close ();

		// chain in reverse order so that most recent will be on top
		si->Next = SaveInfoList;
		SaveInfoList = si;
	}
}


// for autocompletion
char **saveloadlist = NULL;
CQuakeZone *SaveZone = NULL;

void Menu_SaveLoadScanSaves (void)
{
	if (!savelistchanged) return;

	// destroy the previous list
	SAFE_DELETE (SaveInfoList);
	SAFE_DELETE (SaveScrollbox);
	SAFE_DELETE (LoadScrollbox);
	SAFE_DELETE (SaveZone);

	saveloadlist = NULL;
	SaveInfoArray = NULL;
	ActiveSaveInfoArray = NULL;
	ActiveScrollbox = NULL;

	// no saves yet
	NumSaves = 0;

	// create a zone for storing save data
	SaveZone = new CQuakeZone ();

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// look for a file
	hFind = FindFirstFile (va ("%s\\save\\*.sav", com_gamedir), &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
	}
	else
	{
		// add all the items
		do {Menu_SaveLoadAddSave (&FindFileData);} while (FindNextFile (hFind, &FindFileData));

		// close the finder
		FindClose (hFind);
	}

	// add the first item for the new save item
	CSaveInfo *si = new CSaveInfo ();
	si->Next = SaveInfoList;
	SaveInfoList = si;

	// now put them all in an array for easy access
	SaveInfoArray = (CSaveInfo **) SaveZone->Alloc (sizeof (CSaveInfo *) * NumSaves);
	int SaveIndex = 0;
	int slindex = 0;

	// for the autocomplete list - add 1 for null termination
	saveloadlist = (char **) SaveZone->Alloc (sizeof (char *) * (NumSaves + 1));
	saveloadlist[0] = NULL;

	for (si = SaveInfoList; si; si = si->Next)
	{
		SaveInfoArray[SaveIndex] = si;

		// only add games that have a filename
		if (si->SaveInfo.filename[0])
		{
			// add to the null terminated autocompletion list
			saveloadlist[slindex] = (char *) SaveZone->Alloc (strlen (si->SaveInfo.filename) + 1);
			strcpy (saveloadlist[slindex], si->SaveInfo.filename);

			// remove the .sav extension from the list entry
			for (int i = strlen (saveloadlist[slindex]); i; i--)
			{
				if (!_stricmp (&saveloadlist[slindex][i], ".sav"))
				{
					saveloadlist[slindex][i] = 0;
					break;
				}
			}

			saveloadlist[slindex + 1] = NULL;
			slindex++;
		}

		SaveIndex++;
	}

	// the list doesn't come out in ascending order so sort it
	COM_SortStringList (saveloadlist, true);

	// create the scrollbox providers
	// save and load need separate providers as they have slightly different lists
	SaveScrollbox = new CScrollBoxProvider (NumSaves, MAX_SAVE_DISPLAY, 22);
	SaveScrollbox->SetDrawItemCallback (Menu_SaveLoadOnDraw);
	SaveScrollbox->SetHoverItemCallback (Menu_SaveLoadOnHover);
	SaveScrollbox->SetEnterItemCallback (Menu_SaveLoadOnEnter);
	SaveScrollbox->SetDeleteItemCallback (Menu_SaveLoadOnDelete);

	// note that numsaves is *always* guaranteed to be at least 1 as we added the "new savegame" item
	LoadScrollbox = new CScrollBoxProvider (NumSaves - 1, MAX_SAVE_DISPLAY, 22);
	LoadScrollbox->SetDrawItemCallback (Menu_SaveLoadOnDraw);
	LoadScrollbox->SetHoverItemCallback (Menu_SaveLoadOnHover);
	LoadScrollbox->SetEnterItemCallback (Menu_SaveLoadOnEnter);
	LoadScrollbox->SetDeleteItemCallback (Menu_SaveLoadOnDelete);

	// same list now
	savelistchanged = false;
}


void Menu_SaveCustomEnter (void)
{
	if (!sv.active || cl.intermission || svs.maxclients != 1 || cl.stats[STAT_HEALTH] <= 0)
	{
		// can't save
		menu_soundlevel = m_sound_deny;
		return;
	}

	// scan the saves
	Menu_SaveLoadScanSaves ();

	// set the active array to the full list
	ActiveSaveInfoArray = SaveInfoArray;
	ActiveScrollbox = SaveScrollbox;
}


void Menu_LoadCustomEnter (void)
{
	// scan the saves
	Menu_SaveLoadScanSaves ();

	// begin the active array at the second item (skip over the "new savegame" item)
	ActiveSaveInfoArray = &SaveInfoArray[1];
	ActiveScrollbox = LoadScrollbox;

	// check number of saves - if 0, sound a deny
	if (!SaveInfoList) menu_soundlevel = m_sound_deny;
}


void Draw_Mapshot (char *path, char *name, int x, int y);

char *Menu_SaveLoadDecodeName (char *namein)
{
	char *nameout = (char *) scratchbuf;

	strcpy (nameout, namein);

	for (int i = strlen (nameout); i; i--)
	{
		if (nameout[i] == '.')
		{
			nameout[i] = 0;
			break;
		}
	}

	return nameout;
}


void Menu_SaveLoadOnHover (int initialy, int y, int itemnum)
{
	// highlight bar
	Menu_HighlightBar (-174, y, 172);

	// hack - used to identify the "new savegame" item
	if (!ActiveSaveInfoArray[itemnum]->SaveInfo.filename[0])
	{
		// new savegame
		Draw_Mapshot (NULL, NULL, (vid.currsize.width - 320) / 2 + 208, initialy + 8);
		Menu_PrintWhite (220, initialy + 145, "Current Stats");
		Menu_Print (218, initialy + 160, DIVIDER_LINE);
		Menu_Print (220, initialy + 175, "Kills:");
		Menu_Print (220, initialy + 187, "Secrets:");

		// remake quake compatibility
		if (cl.stats[STAT_TOTALMONSTERS] == 0)
			Menu_PrintWhite (220, initialy + 175, va ("         %i", cl.stats[STAT_MONSTERS]));
		else Menu_PrintWhite (220, initialy + 175, va ("         %i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]));

		// remake quake compatibility
		if (cl.stats[STAT_TOTALSECRETS] == 0)
			Menu_PrintWhite (220, initialy + 187, va ("         %i", cl.stats[STAT_SECRETS]));
		else Menu_PrintWhite (220, initialy + 187, va ("         %i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]));

		Menu_Print (220, initialy + 199, "Skill:");
		Menu_PrintWhite (220, initialy + 199, va ("         %s", SkillNames[(int) skill.value]));

		int seconds = (int) (cl.time);
		int minutes = (int) (cl.time / 60);
		seconds -= minutes * 60;

		Menu_Print (220, initialy + 211, "Time:");
		Menu_PrintWhite (220, initialy + 211, va ("         %02i:%02i", minutes, seconds));
	}
	else
	{
		// existing save game
		Draw_Mapshot (host_savedir.string, ActiveSaveInfoArray[itemnum]->SaveInfo.filename, (vid.currsize.width - 320) / 2 + 208, initialy + 8);
		Menu_PrintWhite (220, initialy + 145, "Savegame Info");
		Menu_Print (218, initialy + 160, DIVIDER_LINE);

		Menu_Print (220, initialy + 175, "Name:    ");
		Menu_Print (220, initialy + 187, "Kills:   ");
		Menu_Print (220, initialy + 199, "Secrets: ");
		Menu_Print (220, initialy + 211, "Skill:   ");
		Menu_Print (220, initialy + 223, "Time:    ");

		Menu_PrintWhite (220, initialy + 175, va ("         %s", Menu_SaveLoadDecodeName (ActiveSaveInfoArray[itemnum]->SaveInfo.filename)));
		Menu_PrintWhite (220, initialy + 187, va ("         %s", ActiveSaveInfoArray[itemnum]->SaveInfo.kills));
		Menu_PrintWhite (220, initialy + 199, va ("         %s", ActiveSaveInfoArray[itemnum]->SaveInfo.secrets));
		Menu_PrintWhite (220, initialy + 211, va ("         %s", SkillNames[ActiveSaveInfoArray[itemnum]->SaveInfo.skill]));
		Menu_PrintWhite (220, initialy + 223, va ("         %s", ActiveSaveInfoArray[itemnum]->SaveInfo.time));

		Menu_Print (220, initialy + 235, va ("%s", ActiveSaveInfoArray[itemnum]->SaveInfo.savetime));
	}
}


void Menu_SaveLoadOnDraw (int y, int itemnum)
{
	// draw the item (not every savegame has a map name)
	if (ActiveSaveInfoArray[itemnum]->SaveInfo.mapname[0] == 32)
		Menu_Print (-8, y, ActiveSaveInfoArray[itemnum]->SaveInfo.filename);
	else if (ActiveSaveInfoArray[itemnum]->SaveInfo.mapname[0])
		Menu_Print (-8, y, ActiveSaveInfoArray[itemnum]->SaveInfo.mapname);
	else Menu_Print (-8, y, ActiveSaveInfoArray[itemnum]->SaveInfo.filename);
}


void Draw_InvalidateMapshot (void);
void Host_DoSavegame (char *savename);

void Menu_SaveLoadOnDelete (int itemnum)
{
	// can't delete this one!
	if (m_state == m_save && itemnum == 0)
	{
		SCR_ModalMessage ("You cannot delete\nthis item!\n", "Error", MB_OK);
		return;
	}

	if (SCR_ModalMessage ("Are you sure that you want to\ndelete this save?\n", 
		va ("Delete %s", ActiveSaveInfoArray[itemnum]->SaveInfo.filename),
		MB_YESNO))
	{
		// delete the save
		char delfile[MAX_PATH];

		Q_snprintf (delfile, MAX_PATH, "%s\\save\\%s", com_gamedir, ActiveSaveInfoArray[itemnum]->SaveInfo.filename);

		// change '/' to '\\' to keep paths consistent
		// (it doesn't matter if they're 'wrong', weenix loonies, so long as they're *consistent*
		for (int i = 0;; i++)
		{
			if (!delfile[i]) break;

			if (delfile[i] == '/') delfile[i] = '\\';
		}

		if (!DeleteFile (delfile))
		{
			SCR_UpdateScreen ();
			SCR_ModalMessage ("Delete file failed\n", "Error", MB_OK);
			return;
		}

		// dirty and rescan
		savelistchanged = true;
		Menu_SaveLoadScanSaves ();

		// rerun the enter function
		if (m_state == m_save)
			Menu_SaveCustomEnter ();
		else Menu_LoadCustomEnter ();
	}
}


void Menu_SaveLoadOnEnter (int itemnum)
{
	if (m_state == m_save)
	{
		if (itemnum == 0)
		{
			int i;

			// generate a new save name
			for (i = 0; i < 99999; i++)
			{
				std::ifstream f;

				if (host_savedir.string && host_savedir.string[0])
					f.open (va ("%s/%s%s%05i.sav", com_gamedir, host_savedir.string, host_savenamebase.string, i));
				else f.open (va ("%s/%s%05i.sav", com_gamedir, host_savenamebase.string, i));

				if (!f.is_open ())
				{
					// save to this one
					Host_DoSavegame (va ("%s%05i", host_savenamebase.string, i));

					// dirty the save list
					savelistchanged = true;

					// rescan here and now
					// (note - we should try to position the selection on this item also)
					Menu_SaveLoadScanSaves ();
					break;
				}

				f.close ();
			}

			if (i == 100000)
				Con_Printf ("Menu_SaveLoadOnEnter: Failed to find a free savegame slot\n");

			// exit menus
			m_state = m_none;
			key_dest = key_game;
			return;
		}

		// overwrite save
		// this message was annoying
		//if (Menu_MessageBox (va ("%s\n\nAre you sure that you want to overwrite this save?", ActiveSaveInfoArray[itemnum]->SaveInfo.filename)))
		{
			// save it
			Host_DoSavegame (ActiveSaveInfoArray[itemnum]->SaveInfo.filename);

			// execute the command buffer now so that we have the save file as valid for the next step
			Cbuf_Execute ();

			// rather than dirtying the entire list we will just update the item
			ActiveSaveInfoArray[itemnum]->UpdateItem ();

			// need to invalidate the mapshot for it too!
			Draw_InvalidateMapshot ();

			// exit menus
			m_state = m_none;
			key_dest = key_game;
		}

		return;
	}

	// exit menus
	m_state = m_none;
	key_dest = key_game;

	// to do - check if client status has changed and if so, display a warning
	Cbuf_AddText (va ("load %s\n", ActiveSaveInfoArray[itemnum]->SaveInfo.filename));
}


void Menu_DirtySaveLoadMenu (void)
{
	// intended to be used when a save is issued from the command line or from QC
	// note - we *COULD* pass this a char * of the save name, check it against
	// the array contents and only update if it's a replacement item...
	savelistchanged = true;

	// rescan for autocompletion
	Menu_SaveLoadScanSaves ();
}


int Menu_SaveLoadCustomDraw (int y)
{
	// check for validity, report and get out
	if (m_state == m_save)
	{
		// false if a save cannot be done at this time
		bool validsave = true;

		if (!sv.active)
		{
			Menu_PrintCenter (y + 10, "You cannot save with an inactive local server");
			validsave = false;
		}
		else if (cl.intermission)
		{
			Menu_PrintCenter (y + 10, "You cannot save during an intermission");
			validsave = false;
		}
		else if (svs.maxclients != 1)
		{
			Menu_PrintCenter (y + 10, "You cannot save a multiplayer game");
			validsave = false;
		}
		else if (cl.stats[STAT_HEALTH] <= 0)
		{
			Menu_PrintCenter (y + 10, "You cannot save with a dead player");
			validsave = false;
		}

		if (!validsave)
		{
			Menu_PrintCenter (y + 30, "Press ESC to Exit this Menu");
			return y + 40;
		}
	}
	else
	{
		if (!SaveInfoList || !ActiveSaveInfoArray[0])
		{
			Menu_PrintCenter (y + 10, "There are no Saved Games to Load from");
			Menu_PrintCenter (y + 30, "Press ESC to Exit this Menu");
			return y + 40;
		}
	}

	if (ActiveScrollbox)
		y = ActiveScrollbox->DrawItems ((vid.currsize.width - 320) / 2 - 24, y);

	return y;
}


void Menu_SaveLoadCustomKey (int key)
{
	if (!SaveInfoList) return;
	if (!ActiveScrollbox) return;

	// get what the position is when we come in here
	// uncomment this when we figure out what's going on below
	// int oldcurr = ActiveScrollbox->GetCurrent ();

	switch (key)
	{
	case K_DOWNARROW:
	case K_UPARROW:
		menu_soundlevel = m_sound_nav;

	case K_DEL:
	case K_ENTER:
		// fall through to default here as ActiveScrollbox might go to NULL
		// position don't change anyway so it's cool
		ActiveScrollbox->KeyFunc (key);

	default:
		// position won't change
		return;
	}

	// What WTFery is this?  of course this does nothing, it was told to do nothing just up above.
	// note the fall through to default.  unfortunately i can't remember exactly what i was thinking back then
	// can KeyFunc make a scroll box item go null?  it's ugly code anyway...
	/*
	// hmmm - this seems to do nothing...
	// get what the position is now
	int newcurr = ActiveScrollbox->GetCurrent ();

	// hasn't changed
	if (oldcurr == newcurr) return;

	// fix up the scrollboxes so that the current items track each other
	if (m_state == m_save)
	{
		// load item is one less
		LoadScrollbox->SetCurrent (newcurr - 1);
	}
	else
	{
		// save item is one less
		SaveScrollbox->SetCurrent (newcurr + 1);
	}
	*/
}


void Menu_InitSaveLoadMenu (void)
{
	extern qpic_t *gfx_p_save_lmp;
	extern qpic_t *gfx_p_load_lmp;

	menu_Save.AddOption (new CQMenuBanner (&gfx_p_save_lmp));
	menu_Save.AddOption (new CQMenuTitle ("Save the Current Game"));
	menu_Save.AddOption (new CQMenuCustomEnter (Menu_SaveCustomEnter));
	menu_Save.AddOption (new CQMenuCustomDraw (Menu_SaveLoadCustomDraw));
	menu_Save.AddOption (new CQMenuCustomKey (K_UPARROW, Menu_SaveLoadCustomKey));
	menu_Save.AddOption (new CQMenuCustomKey (K_DOWNARROW, Menu_SaveLoadCustomKey));
	menu_Save.AddOption (new CQMenuCustomKey (K_DEL, Menu_SaveLoadCustomKey));
	menu_Save.AddOption (new CQMenuCustomKey (K_ENTER, Menu_SaveLoadCustomKey));

	menu_Load.AddOption (new CQMenuBanner (&gfx_p_load_lmp));
	menu_Load.AddOption (new CQMenuTitle ("Load a Previously Saved Game"));
	menu_Load.AddOption (new CQMenuCustomEnter (Menu_LoadCustomEnter));
	menu_Load.AddOption (new CQMenuCustomDraw (Menu_SaveLoadCustomDraw));
	menu_Load.AddOption (new CQMenuCustomKey (K_UPARROW, Menu_SaveLoadCustomKey));
	menu_Load.AddOption (new CQMenuCustomKey (K_DOWNARROW, Menu_SaveLoadCustomKey));
	menu_Load.AddOption (new CQMenuCustomKey (K_DEL, Menu_SaveLoadCustomKey));
	menu_Load.AddOption (new CQMenuCustomKey (K_ENTER, Menu_SaveLoadCustomKey));

	// the initial scan at runtime can be slow owing to filesystem first access, so
	// run a pre-scan to speed that up a little...
	Menu_SaveLoadScanSaves ();
}


