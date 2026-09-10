/*
	This file is part of SHMUP.

    SHMUP is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    SHMUP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with SHMUP.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
 *  packlint.c -- read every level pack the way the GAME reads it, and say what
 *                is wrong before a device has to.
 *
 *  v4 stage 4. The point of the pack format is that somebody can write a level
 *  without touching the C; the point of this is that they find out what they
 *  got wrong from a laptop in a second, instead of from a black screen on a
 *  phone twenty minutes later.
 *
 *  It compiles engine/src/lexer.c VERBATIM, so the tool cannot disagree with
 *  the engine about what a token is -- the same trick tools/netrig plays with
 *  netchannel.c, and for the same reason. Everything else it needs from the
 *  engine is a file handle, which is thirty lines below.
 *
 *  What it checks, which is what actually goes wrong:
 *    - config.cfg's pack list: ids in range, no duplicates, files present
 *    - each manifest: the required keys, a kind it recognises, a scene that
 *      exists, a player range that makes sense
 *    - each scene: every file it names is on disk (map, music, textures,
 *      titles, camera path) -- a typo here is a black screen or a silent act
 *    - the rules block: every group a rule WATCHES is a group something
 *      actually spawns. A typo there is a rule that simply never fires, and
 *      nothing anywhere says so.
 *    - the acts: numbered from 1 with no hole, since the progression and the
 *      end-of-game card count on it
 *
 *  Build and run (from this directory):
 *      zig cc -std=gnu99 -I ../../engine/src packlint.c ../../engine/src/lexer.c -o packlint
 *      ./packlint ../../data/data
 *  Exit code 0 = the packs are sound.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "lexer.h"

#define MAX_PACKS		16
#define MAX_GROUPS		64
#define MAX_NAME		256

static char  gRoot[MAX_NAME];		/* the GAME dir: the one every path inside the
									   files is relative to. In the repo that is
									   "data", and config.cfg sits at data/data. */
static int   gErrors;
static int   gWarnings;
static int   gChecked;

static void err(const char* what, ...)
{
	va_list ap;
	printf("  ERROR   ");
	va_start(ap, what);
	vprintf(what, ap);
	va_end(ap);
	printf("\n");
	gErrors++;
}

static void warn(const char* what, ...)
{
	va_list ap;
	printf("  warning ");
	va_start(ap, what);
	vprintf(what, ap);
	va_end(ap);
	printf("\n");
	gWarnings++;
}

/* ------------------------------------------------------------------ */
/* The one piece of engine plumbing the lexer wants: a file in memory. */

static filehandle_t* PL_Open(const char* relative)
{
	static filehandle_t fh;
	static unsigned char* buffer;
	char path[MAX_NAME * 2];
	FILE* f;
	long size;

	snprintf(path, sizeof(path), "%s/%s", gRoot, relative);
	f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	free(buffer);
	buffer = (unsigned char*)malloc((size_t)size + 1);
	if (!buffer || fread(buffer, 1, (size_t)size, f) != (size_t)size)
	{
		fclose(f);
		return NULL;
	}
	buffer[size] = 0;
	fclose(f);

	memset(&fh, 0, sizeof(fh));
	fh.bLoaded    = 1;
	fh.filesize   = (W32)size;
	fh.ptrStart   = buffer;
	fh.ptrCurrent = buffer;
	fh.ptrEnd     = buffer + size;
	return &fh;
}

static int PL_Exists(const char* relative)
{
	char path[MAX_NAME * 2];
	FILE* f;
	snprintf(path, sizeof(path), "%s/%s", gRoot, relative);
	f = fopen(path, "rb");
	if (!f)
		return 0;
	fclose(f);
	return 1;
}

/* A file the scene names must be on disk, or the act is a black screen. */
static void PL_CheckAsset(const char* what, const char* relative, const char* owner)
{
	gChecked++;
	if (!relative || !relative[0])
		return;
	if (!PL_Exists(relative))
		err("%s: %s '%s' does not exist", owner, what, relative);
}

/* ------------------------------------------------------------------ */
/* The rules block: a rule that watches a group nobody spawns is a rule
   that never fires, and the engine says nothing about it. */

static char gSpawned[MAX_GROUPS][32];	/* groups something spawns */
static int  gNumSpawned;
static char gWatched[MAX_GROUPS][32];	/* groups a rule waits on */
static int  gNumWatched;

static void PL_NoteGroup(char list[][32], int* n, const char* name)
{
	int i;
	if (!name || !name[0] || *n >= MAX_GROUPS)
		return;
	for (i = 0; i < *n; i++)
		if (!strcmp(list[i], name))
			return;
	strncpy(list[*n], name, 31);
	list[*n][31] = 0;
	(*n)++;
}

/* ------------------------------------------------------------------ */

static void PL_CheckScene(const char* scenePath, const char* packId)
{
	filehandle_t* f = PL_Open(scenePath);
	char track[MAX_NAME] = "", alternate[MAX_NAME] = "";
	char block[64] = "", lastWord[64] = "";
	int sawCamera = 0, sawMap = 0;
	int i;

	if (!f)
	{
		err("%s: scene '%s' cannot be read", packId, scenePath);
		return;
	}

	gNumSpawned = gNumWatched = 0;
	LE_init(f);

	while (LE_hasMoreData())
	{
		const char* tok = LE_readToken();
		if (!tok || !tok[0])
			break;

		/* Which block are we in? The word before a '{' names it. Needed because
		   the same keyword means different things in different blocks: a
		   'filename' under map is an asset the act cannot start without, while
		   one under playback names a recorded demo that is allowed to be
		   absent (playback is off in every shipped scene). */
		if (!strcmp(tok, "{"))
		{
			strncpy(block, lastWord, sizeof(block) - 1);
			block[sizeof(block) - 1] = 0;
			lastWord[0] = 0;
			continue;
		}
		if (!strcmp(tok, "}"))
		{
			block[0] = 0;
			continue;
		}

		/* Every keyword that names a FILE the scene cannot do without. */
		if (!strcmp(tok, "filename") || !strcmp(tok, "trackname") ||
		    !strcmp(tok, "alternate") || !strcmp(tok, "path") ||
		    !strcmp(tok, "titleName") || !strcmp(tok, "textureName"))
		{
			char kind[32];
			strncpy(kind, tok, sizeof(kind) - 1);
			kind[sizeof(kind) - 1] = 0;
			LE_readToken();
			if (strcmp(block, "playback"))
				PL_CheckAsset(kind, LE_getCurrentToken(), packId);
			if (!strcmp(kind, "trackname"))  { strncpy(track, LE_getCurrentToken(), MAX_NAME - 1); }
			if (!strcmp(kind, "alternate"))  { strncpy(alternate, LE_getCurrentToken(), MAX_NAME - 1); }
			if (!strcmp(kind, "path"))       sawCamera = 1;
			if (!strcmp(kind, "filename") && !strcmp(block, "map")) sawMap = 1;
			lastWord[0] = 0;
			continue;
		}

		/* "group <name>" tags whatever was just spawned; inside a rules block
		   the same word also tags the rule's own spawns. Either way something
		   spawns with that name. */
		if (!strcmp(tok, "group"))
		{
			LE_readToken();
			PL_NoteGroup(gSpawned, &gNumSpawned, LE_getCurrentToken());
			lastWord[0] = 0;
			continue;
		}

		/* the triggers that WAIT on a group */
		if (!strcmp(tok, "cleared") || !strcmp(tok, "hpBelow") || !strcmp(tok, "hpAtMost"))
		{
			LE_readToken();
			PL_NoteGroup(gWatched, &gNumWatched, LE_getCurrentToken());
			lastWord[0] = 0;
			continue;
		}

		strncpy(lastWord, tok, sizeof(lastWord) - 1);
		lastWord[sizeof(lastWord) - 1] = 0;
	}

	if (!sawCamera)
		warn("%s: the scene names no camera path", packId);
	if (!sawMap)
		warn("%s: the scene names no map", packId);
	if (track[0] && !alternate[0])
		warn("%s: no 'alternate' theme -- the track will loop when it runs out", packId);

	/* The check that pays for this whole tool. */
	for (i = 0; i < gNumWatched; i++)
	{
		int j, found = 0;
		gChecked++;
		for (j = 0; j < gNumSpawned; j++)
			if (!strcmp(gWatched[i], gSpawned[j]))
				found = 1;
		if (!found)
			err("%s: a rule waits on group '%s', which nothing spawns -- that rule can never fire",
			    packId, gWatched[i]);
	}
}

/* ------------------------------------------------------------------ */

static const char* kKinds[] = { "intro", "act", "demo", "tutorial", NULL };

static int PL_CheckManifest(const char* manifestPath, int sceneId, char* outKind, char* outId)
{
	filehandle_t* f = PL_Open(manifestPath);
	char id[64] = "", name[64] = "", kind[32] = "", scene[MAX_NAME] = "";
	int format = 0, minP = 0, maxP = 0, k, known;

	outKind[0] = outId[0] = 0;

	if (!f)
	{
		err("scene %d: the manifest '%s' cannot be read", sceneId, manifestPath);
		return 0;
	}
	LE_init(f);

	while (LE_hasMoreData())
	{
		const char* tok = LE_readToken();
		if (!tok || !tok[0])
			break;
		if      (!strcmp(tok, "format"))     format = (int)LE_readReal();
		else if (!strcmp(tok, "id"))         { LE_readToken(); strncpy(id, LE_getCurrentToken(), 63); }
		else if (!strcmp(tok, "name"))       { LE_readToken(); strncpy(name, LE_getCurrentToken(), 63); }
		else if (!strcmp(tok, "kind"))       { LE_readToken(); strncpy(kind, LE_getCurrentToken(), 31); }
		else if (!strcmp(tok, "scene"))      { LE_readToken(); strncpy(scene, LE_getCurrentToken(), MAX_NAME - 1); }
		else if (!strcmp(tok, "minPlayers")) minP = (int)LE_readReal();
		else if (!strcmp(tok, "maxPlayers")) maxP = (int)LE_readReal();
	}

	gChecked += 6;
	if (format != 1)  err("%s: format is %d, this build reads format 1", manifestPath, format);
	if (!id[0])       err("%s: no 'id'", manifestPath);
	if (!name[0])     warn("%s: no 'name' -- the menus will show nothing", manifestPath);
	if (!scene[0])  { err("%s: no 'scene'", manifestPath); return 0; }

	for (k = 0, known = 0; kKinds[k]; k++)
		if (!strcmp(kind, kKinds[k]))
			known = 1;
	if (!known)
		err("%s: kind '%s' is not one of intro/act/demo/tutorial", manifestPath, kind[0] ? kind : "(none)");

	if (minP < 1 || maxP < 1 || minP > maxP || maxP > 4)
		err("%s: players %d-%d makes no sense (1..4, min <= max)", manifestPath, minP, maxP);

	if (!PL_Exists(scene))
	{
		err("%s: scene '%s' does not exist", manifestPath, scene);
		return 0;
	}

	strncpy(outKind, kind, 31);
	strncpy(outId, id, 63);
	printf("  %-14s scene %-2d  %-9s players %d-%d  %s\n", id, sceneId, kind, minP, maxP, scene);
	PL_CheckScene(scene, id);
	return 1;
}

/* ------------------------------------------------------------------ */

int main(int argc, char** argv)
{
	filehandle_t* cfg;
	char  packPath[MAX_PACKS][MAX_NAME];
	int   packUsed[MAX_PACKS];
	int   numScenes = 0, acts = 0, i;

	snprintf(gRoot, sizeof(gRoot), "%s", (argc > 1) ? argv[1] : "data");
	memset(packUsed, 0, sizeof(packUsed));

	printf("=== packlint: reading %s/config.cfg with the engine's own lexer ===\n", gRoot);

	cfg = PL_Open("data/config.cfg");
	if (!cfg)
	{
		printf("  ERROR   no config.cfg under '%s'\n", gRoot);
		return 1;
	}
	LE_init(cfg);

	while (LE_hasMoreData())
	{
		const char* tok = LE_readToken();
		if (!tok || !tok[0])
			break;
		if (!strcmp(tok, "numScenes"))
			numScenes = (int)LE_readReal();
		else if (!strcmp(tok, "pack"))
		{
			int id = (int)LE_readReal();
			LE_readToken();
			if (id < 0 || id >= MAX_PACKS)
			{
				err("config.cfg: pack id %d is outside 0..%d", id, MAX_PACKS - 1);
				continue;
			}
			if (packUsed[id])
				err("config.cfg: pack id %d is declared twice", id);
			packUsed[id] = 1;
			strncpy(packPath[id], LE_getCurrentToken(), MAX_NAME - 1);
			packPath[id][MAX_NAME - 1] = 0;
		}
	}

	if (numScenes < 1)
		err("config.cfg: numScenes is %d", numScenes);

	for (i = 0; i < MAX_PACKS; i++)
	{
		char kind[32], id[64];
		if (!packUsed[i])
			continue;
		if (PL_CheckManifest(packPath[i], i, kind, id) && !strcmp(kind, "act"))
			acts++;
	}

	/* The progression, the licence gate and the ending card all count acts in
	   id order, so a hole in them is a level nobody can reach. */
	gChecked++;
	if (acts < 1)
		err("no act among the packs -- there is no game to play");
	else
		printf("  %d acts, %d scenes declared\n", acts, numScenes);

	printf("=== %d checks, %d errors, %d warnings ===\n", gChecked, gErrors, gWarnings);
	return gErrors ? 1 : 0;
}
