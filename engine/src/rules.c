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
 *  rules.c  --  conditional events (v4 stage 2). See rules.h for the grammar.
 */

#include "rules.h"
#include "event.h"
#include "enemy.h"
#include "dEngine.h"
#include "timer.h"
#include "lexer.h"
#include "log.h"
#include "player.h"
#include "lofb.h"	// stage 2b: the boss ladder as rule actions
#include <string.h>
#include <stdlib.h>

#define RULES_MAX			32
#define RULE_MAX_SPAWNS		32
#define GROUPS_MAX			32
#define RULE_DEFAULT_TTL	6000

typedef enum { RT_NONE = 0, RT_CLEARED, RT_HPBELOW, RT_PLAYERS, RT_AFTER, RT_HPATMOST } rule_trigger_t;

typedef struct rule_t
{
	char name[16];
	rule_trigger_t trigger;
	char group[16];			// the group the trigger watches
	int  value;				// pct for hpBelow/hpAtMost, seats for players, ms for after
	int  delay;				// after <ms>: the spawns land that long after the trigger
	int  period;			// every <ms>: re-fires while the condition holds; 0 = once
	int  lastFire;			// simulation time of the last fire, -1 = never
	int  numSpawns;
	event_spawnEnemy_payload_t spawns[RULE_MAX_SPAWNS];
	char bossAttack[LOFB_NUM_ATTACKS];	// stage 2b: -1 untouched, 0 off, 1 on (applied at fire, no delay)
} rule_t;

typedef struct group_t
{
	char name[16];
	int  seen;				// at least one enemy of the group has spawned
	int  energyMax;			// the most alive energy the group ever held (spawn sums, raised
							// each tick: the boss sets its real HP pool after it spawns)
} group_t;

static rule_t  gRules[RULES_MAX];
static int     gNumRules;
static group_t gGroups[GROUPS_MAX];
static int     gNumGroups;

static const char* triggerNames[] = { "none", "cleared", "hpBelow", "players", "after", "hpAtMost" };

// ---------------------------------------------------------------------------

void RULES_InitForScene(void)
{
	memset(gRules, 0, sizeof(gRules));
	memset(gGroups, 0, sizeof(gGroups));
	gNumRules = 0;
	gNumGroups = 0;
	LOFB_ResetLadder();		// stage 2b: thresholds again until a rule says otherwise
}

static group_t* RULES_FindGroup(const char* name, int create)
{
	int i;
	if (!name || !name[0])
		return NULL;
	for (i = 0; i < gNumGroups; i++)
		if (!strcmp(gGroups[i].name, name))
			return &gGroups[i];
	if (!create || gNumGroups >= GROUPS_MAX)
		return NULL;
	strncpy(gGroups[gNumGroups].name, name, sizeof(gGroups[0].name) - 1);
	return &gGroups[gNumGroups++];
}

void RULES_NoteSpawn(struct enemy_t* enemy)
{
	group_t* g;
	if (!enemy || !enemy->group[0])
		return;
	g = RULES_FindGroup(enemy->group, 1);
	if (!g)
		return;
	g->seen = 1;
	g->energyMax += enemy->energy;
}

int RULES_GroupAlive(const char* group)
{
	int n = 0;
	enemy_t* e;
	if (!group || !group[0])
		return 0;
	for (e = ENE_GetFirstEnemy(); e != NULL; e = e->next)
		if (!strcmp(e->group, group))
			n++;
	return n;
}

static int RULES_GroupEnergy(const char* group)
{
	int energy = 0;
	enemy_t* e;
	for (e = ENE_GetFirstEnemy(); e != NULL; e = e->next)
		if (!strcmp(e->group, group) && e->energy > 0)
			energy += LOFB_EffectiveEnergy(e);
	return energy;
}

int RULES_GroupHpPct(const char* group)
{
	group_t* g = RULES_FindGroup(group, 0);
	if (!g || !g->seen || g->energyMax <= 0)
		return 0;
	// the same integer arithmetic as lofb.c's own hpPct, so a threshold written
	// as a rule fires on the very frame the C ladder used to
	return (100 * RULES_GroupEnergy(group)) / g->energyMax;
}

// The energy budget follows the most a group ever held: the boss spawns with
// its type's base energy and sets the fight's pool while arriving.
static void RULES_TrackEnergy(void)
{
	int i;
	for (i = 0; i < gNumGroups; i++)
	{
		int e = RULES_GroupEnergy(gGroups[i].name);
		if (e > gGroups[i].energyMax)
			gGroups[i].energyMax = e;
	}
}

// ---------------------------------------------------------------------------

static int RULES_Holds(const rule_t* r)
{
	const group_t* g;
	switch (r->trigger)
	{
		case RT_CLEARED:
			// spawned at least once, none alive, none still to come
			g = RULES_FindGroup(r->group, 0);
			return g && g->seen && RULES_GroupAlive(r->group) == 0 && EV_PendingSpawnsInGroup(r->group) == 0;
		case RT_HPBELOW:
			g = RULES_FindGroup(r->group, 0);
			return g && g->seen && RULES_GroupAlive(r->group) > 0 && RULES_GroupHpPct(r->group) < r->value;
		case RT_HPATMOST:
			g = RULES_FindGroup(r->group, 0);
			return g && g->seen && RULES_GroupAlive(r->group) > 0 && RULES_GroupHpPct(r->group) <= r->value;
		case RT_PLAYERS:
			return numPlayers >= r->value;
		case RT_AFTER:
			return simulationTime >= r->value;
		default:
			return 0;
	}
}

static void RULES_Fire(rule_t* r)
{
	int i;
	r->lastFire = simulationTime;
	if (Log_ProbesEnabled())
		Log_Printf("[rule] t=%d fire %s (%s %s %d) spawns=%d delay=%d\n",
			simulationTime, r->name, triggerNames[r->trigger], r->group, r->value, r->numSpawns, r->delay);
	for (i = 0; i < LOFB_NUM_ATTACKS; i++)
		if (r->bossAttack[i] >= 0)
			LOFB_SetAttack(i, r->bossAttack[i]);
	for (i = 0; i < r->numSpawns; i++)
	{
		event_t* event = calloc(1, sizeof(event_t));
		event_spawnEnemy_payload_t* payload = calloc(1, sizeof(event_spawnEnemy_payload_t));
		memcpy(payload, &r->spawns[i], sizeof(event_spawnEnemy_payload_t));
		event->time = simulationTime + r->delay;
		event->type = EV_SPAWN_ENEMY;
		event->payload = payload;
		EV_AddEvent(event);
	}
}

void RULES_Update(void)
{
	int i;
	RULES_TrackEnergy();
	for (i = 0; i < gNumRules; i++)
	{
		rule_t* r = &gRules[i];
		if (r->trigger == RT_NONE)
			continue;
		if (r->lastFire >= 0)
		{
			if (r->period <= 0)
				continue;								// fired once, done
			if (simulationTime - r->lastFire < r->period)
				continue;								// not yet due again
		}
		if (RULES_Holds(r))
			RULES_Fire(r);
	}
}

// ---------------------------------------------------------------------------
// Parsing. The lexer sits on "rules"; the block ends at its "}".

static void RULES_TagSpawns(rule_t* r, const char* group)
{
	int i;
	for (i = 0; i < r->numSpawns; i++)
	{
		strncpy(r->spawns[i].group, group, sizeof(r->spawns[i].group) - 1);
		r->spawns[i].group[sizeof(r->spawns[i].group) - 1] = 0;
	}
}

void RULES_Read(void)
{
	float ttl = RULE_DEFAULT_TTL;

	LE_readToken();		// {
	LE_readToken();
	while (LE_hasMoreData() && strcmp("}", LE_getCurrentToken()))
	{
		if (!strcmp("setttl", LE_getCurrentToken()))
		{
			ttl = LE_readReal();
			LE_readToken();
			continue;
		}
		if (strcmp("rule", LE_getCurrentToken()))
		{
			Log_Printf("[rule] unexpected '%s' in the rules block\n", LE_getCurrentToken());
			LE_readToken();
			continue;
		}
		if (gNumRules >= RULES_MAX)
		{
			Log_Printf("[rule] too many rules (max %d), the rest is ignored\n", RULES_MAX);
			return;
		}
		{
			rule_t* r = &gRules[gNumRules++];
			memset(r, 0, sizeof(*r));
			r->lastFire = -1;
			memset(r->bossAttack, -1, sizeof(r->bossAttack));
			LE_readToken();
			strncpy(r->name, LE_getCurrentToken(), sizeof(r->name) - 1);
			LE_readToken();
			// trigger, modifiers and actions, in any order, until the next rule or the block's end
			while (LE_hasMoreData() && strcmp("rule", LE_getCurrentToken()) && strcmp("}", LE_getCurrentToken()))
			{
				const char* tok = LE_getCurrentToken();
				if (!strcmp("when", tok))
				{
					// sugar: "when cleared w1" reads as "cleared w1"
				}
				else if (!strcmp("cleared", tok))
				{
					LE_readToken();
					strncpy(r->group, LE_getCurrentToken(), sizeof(r->group) - 1);
					r->trigger = RT_CLEARED;
				}
				else if (!strcmp("hpBelow", tok) || !strcmp("hpAtMost", tok))
				{
					rule_trigger_t t = strcmp(tok, "hpBelow") ? RT_HPATMOST : RT_HPBELOW;
					LE_readToken();
					strncpy(r->group, LE_getCurrentToken(), sizeof(r->group) - 1);
					r->value = (int)LE_readReal();
					r->trigger = t;
				}
				else if (!strcmp("bossAttack", tok))
				{
					// stage 2b: bossAttack <spray|minions|bigshot|missiles|frenzy> <on|off>
					int which;
					LE_readToken();
					which = LOFB_AttackIdByName(LE_getCurrentToken());
					LE_readToken();
					if (which >= 0)
					{
						r->bossAttack[which] = (char)(!strcmp("on", LE_getCurrentToken()) ? 1 : 0);
						LOFB_UseScriptedLadder();	// from now on only the rules move the attacks
					}
					else
						Log_Printf("[rule] %s: unknown boss attack\n", r->name);
				}
				else if (!strcmp("players", tok))
				{
					r->value = (int)LE_readReal();
					r->trigger = RT_PLAYERS;
				}
				else if (!strcmp("after", tok))
				{
					int v = (int)LE_readReal();
					if (r->trigger == RT_NONE) { r->trigger = RT_AFTER; r->value = v; }
					else                        r->delay = v;
				}
				else if (!strcmp("every", tok))
				{
					r->period = (int)LE_readReal();
				}
				else if (!strcmp("spawnEnemy", tok))
				{
					if (r->numSpawns < RULE_MAX_SPAWNS)
					{
						event_spawnEnemy_payload_t* p = &r->spawns[r->numSpawns++];
						memset(p, 0, sizeof(*p));
						p->ttl = (int)ttl;
						EV_ParseSpawnParams(p);
					}
					else
						Log_Printf("[rule] %s: too many spawns\n", r->name);
				}
				else if (!strcmp("spawnEnemyWave", tok))
				{
					LE_readToken();		// circle
					r->numSpawns += EV_ParseCircleWave(&r->spawns[r->numSpawns], RULE_MAX_SPAWNS - r->numSpawns, ttl);
				}
				else if (!strcmp("group", tok))
				{
					LE_readToken();
					RULES_TagSpawns(r, LE_getCurrentToken());
				}
				else
				{
					Log_Printf("[rule] %s: unknown word '%s'\n", r->name, tok);
				}
				LE_readToken();
			}
			Log_Printf("[rule] %s: %s %s %d, delay %d, every %d, %d spawns\n",
				r->name, triggerNames[r->trigger], r->group, r->value, r->delay, r->period, r->numSpawns);
		}
	}
}
