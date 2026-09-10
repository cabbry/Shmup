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
 *  rules.h  --  conditional events (v4 stage 2)
 *
 *  The scene's "enemies" and "events" blocks are timelines: everything fires
 *  at a time. A "rules" block adds REACTIONS: a rule watches the simulation
 *  and, when its condition holds, schedules spawns. Enemies carry a GROUP
 *  name so a rule can wait for one to be wiped out or worn down.
 *
 *      rules
 *      {
 *          setttl 6000
 *          rule w2   when cleared w1  after 1500   spawnEnemy <params>  group w2
 *          rule w3   when cleared w2               spawnEnemyWave circle 8 1 0 0 0  group w3
 *          rule late when after 30000               spawnEnemy <params>
 *          rule tick when hpBelow boss 50  every 6000  spawnEnemyWave circle 4 1 0 0 3
 *      }
 *
 *  Triggers: cleared <group> (spawned once, none alive) | hpBelow <group> <pct>
 *  (alive energy of the group under pct% of what it was spawned with) |
 *  players <n> (at least n seats) | after <ms> (the scene clock).
 *  Modifiers: after <ms> (delay between the trigger and the spawns) |
 *  every <ms> (re-fire while the condition holds; default fires once).
 *  Actions: spawnEnemy / spawnEnemyWave circle, the enemies-block grammar;
 *  a trailing group <name> tags what the rule spawns. endAct ends the act:
 *  the ships park and the epilog card follows, the same two events every
 *  hand-authored act schedules by hand -- but at a time only play can know.
 *  It takes no argument and fires at most once per scene; the "after" delay
 *  applies to it like it does to spawns, so "cleared last after 2000 endAct"
 *  gives the player two seconds of clear sky before the card. An act that
 *  ends this way should still declare a timed epilog as a BACKSTOP: one enemy
 *  stuck off-screen must not mean a level that never finishes (the first of
 *  the two to arrive wins -- endAct is a one-shot, and an epilog already
 *  showing swallows the second).
 *
 *  Deterministic by construction: pure functions of the simulation clock and
 *  the enemy list, evaluated once per tick on every peer -- lockstep-safe.
 */

#ifndef DE_RULES
#define DE_RULES

#include "globals.h"

struct enemy_t;

void RULES_InitForScene(void);				// before the scene file is parsed
void RULES_Read(void);						// the lexer sits on "rules"
void RULES_Update(void);					// once per simulation tick, after EV_Update
void RULES_NoteSpawn(struct enemy_t* enemy);	// every spawned enemy passes here (group bookkeeping)
int  RULES_GroupAlive(const char* group);	// enemies of that group alive right now
int  RULES_GroupHpPct(const char* group);	// 100 * alive energy / energy at spawn (0 when never spawned)

#endif
