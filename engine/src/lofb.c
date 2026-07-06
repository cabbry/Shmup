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
 *  lofb.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-10-25.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

// The LOFB is the boss of act 3 ("Water") -- the fight the original 2009/2010
// project ran out of budget for. The scene, the title card (boss.png) and this
// file's scaffolding shipped back then with an empty updateLOFB; this finally
// implements the fight, driven only by simulation state (positions, timers in
// enemy->parameters[], HP) so it stays deterministic in lockstep multiplayer.

#include <string.h>

#include "lofb.h"
#include "globals.h"
#include "timer.h"
#include "player.h"
#include "dEngine.h"
#include "fx.h"
#include "sounds.h"
#include "titles.h"
#include "event.h"
#include "native_services.h"
#include "enemy_particules.h"

// Reused engine services with no public prototype.
extern void emitSHABBullet(enemy_t* enemy, float angle);	// shab.c: one enemy bullet at an angle
extern void EV_SpawnEnemy(event_t* event);					// event.c: spawn from a payload
extern void EV_AutoPilotPls(event_t* event);				// event.c: fly players to rest position

// enemy->parameters[] slots: float scratch storage, per enemy. timeCounter is a
// ushort (wraps at ~65s), too small for a boss fight, so time lives here.
#define P_TIME			0	// ms since spawn
#define P_FAN_CD		1	// cooldown: aimed fan burst
#define P_SPIRAL_CD		2	// cooldown: spiral emitter
#define P_SPIRAL_ANGLE	3	// current spiral angle (radians)
#define P_MINION_CD		4	// cooldown: escort wave
#define P_BIGSHOT_CD	5	// cooldown: the big shot

// Tuning.
#define LOFB_ARRIVE_MS			6000.0f
#define LOFB_HOVER_Y			0.55f
#define LOFB_SWAY_HALFWIDTH		0.45f
#define LOFB_SWAY_PERIOD_MS		9000.0f
#define LOFB_BOB_PERIOD_MS		3700.0f
#define LOFB_VICTORY_BONUS		100000

// Boss HUD state (read by LOFB_GetBossHealthBar, rendered with the score).
static int gBossEnergy = 0;
static int gBossMaxEnergy = 0;
static int gBossHudStamp = -100000;	// simulationTime of the last boss update

static float LOFB_AimAngle(enemy_t* enemy)
{
	// Angle (screen space) from the boss toward the nearest player. Player
	// positions are lockstep-synced, so this stays deterministic in multiplayer.
	int i;
	float best = 1e9f;
	float tx = 0, ty = -1;	// fallback: straight down

	for (i = 0; i < numPlayers; i++)
	{
		float dx = players[i].ss_position[X] - enemy->ss_position[X];
		float dy = players[i].ss_position[Y] - enemy->ss_position[Y];
		float d2 = dx*dx + dy*dy;
		if (d2 < best) { best = d2; tx = dx; ty = dy; }
	}
	return atan2f(ty, tx);
}

static void LOFB_FireFan(enemy_t* enemy, int count, float spread)
{
	int i;
	float aim = LOFB_AimAngle(enemy);
	for (i = 0; i < count; i++)
		emitSHABBullet(enemy, aim + (i - (count - 1) * 0.5f) * spread);
}

static void LOFB_SpawnMinion(float side, float startX)
{
	// One FHT escort diving in from the top; built like a .scene spawnEnemy line.
	event_t ev;
	event_spawnEnemy_payload_t pl;

	memset(&pl, 0, sizeof(pl));
	pl.type = 1;							// FHT
	pl.mouvementPatternType = MVMT_STRAIGHT;
	pl.startPosition[X] = side * startX;	pl.startPosition[Y] = 1.25f;
	pl.endPosition[X]   = -side * 0.3f;		pl.endPosition[Y]   = -1.3f;
	pl.controlPoint[X]  = side * startX;	pl.controlPoint[Y]  = 0.0f;
	pl.ttl = 6000;
	pl.subType = 0;							// NORMAL

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_SPAWN_ENEMY;
	ev.payload = &pl;
	EV_SpawnEnemy(&ev);
}

// The BIG SHOT: a large, slower orb aimed at the nearest player (same atlas
// sprite as the SHAB/boss bullets, four times the size).
#define LOFB_BIG_TTL		3600
#define LOFB_BIG_DISTANCE	1.6f
#define LOFB_BIG_SIZE		0.22f
#define LOFB_TEXT_BULLET_U      (80/128.0f*SHRT_MAX)
#define LOFB_TEXT_BULLET_V      (0/128.0f*SHRT_MAX)
#define LOFB_TEXT_BULLET_WIDTH  (16/128.0f*SHRT_MAX)
#define LOFB_TEXT_BULLET_HEIGHT (16/128.0f*SHRT_MAX)
static void LOFB_FireBigShot(enemy_t* enemy)
{
	enemy_part_t* bullet;
	float angle = LOFB_AimAngle(enemy);

	bullet = ENPAR_GetNextParticule();

	bullet->ttl = LOFB_BIG_TTL;
	bullet->originalTTL = LOFB_BIG_TTL;

	bullet->ss_boudaries[UP]    = bullet->ss_starting_boudaries[UP]    = enemy->ss_position[Y] * SS_H + LOFB_BIG_SIZE /2 * SS_H / gVScale;
	bullet->ss_boudaries[DOWN]  = bullet->ss_starting_boudaries[DOWN]  = enemy->ss_position[Y] * SS_H - LOFB_BIG_SIZE /2 * SS_H / gVScale;
	bullet->ss_boudaries[LEFT]  = bullet->ss_starting_boudaries[LEFT]  = enemy->ss_position[X] * SS_W - LOFB_BIG_SIZE /2 *SS_H/(float)SS_W * SS_W;
	bullet->ss_boudaries[RIGHT] = bullet->ss_starting_boudaries[RIGHT] = enemy->ss_position[X] * SS_W + LOFB_BIG_SIZE /2 *SS_H/(float)SS_W * SS_W;

	bullet->text[0][U] = LOFB_TEXT_BULLET_U;
	bullet->text[0][V] = LOFB_TEXT_BULLET_V;
	bullet->text[1][U] = LOFB_TEXT_BULLET_U;
	bullet->text[1][V] = LOFB_TEXT_BULLET_V + LOFB_TEXT_BULLET_HEIGHT;
	bullet->text[2][U] = LOFB_TEXT_BULLET_U + LOFB_TEXT_BULLET_WIDTH;
	bullet->text[2][V] = LOFB_TEXT_BULLET_V + LOFB_TEXT_BULLET_HEIGHT;
	bullet->text[3][U] = LOFB_TEXT_BULLET_U + LOFB_TEXT_BULLET_WIDTH;
	bullet->text[3][V] = LOFB_TEXT_BULLET_V;

	bullet->posDiff[X] = cosf(angle) * LOFB_BIG_DISTANCE * SS_H;
	bullet->posDiff[Y] = sinf(angle) * LOFB_BIG_DISTANCE * SS_H;
}

void updateLOFB(enemy_t* enemy)
{
	float t;
	int phase;

	enemy->parameters[P_TIME] += timediff;
	t = enemy->parameters[P_TIME];

	// Publish HP to the HUD. A stale stamp (fresh fight after a scene change)
	// re-baselines the max.
	if (simulationTime - gBossHudStamp > 1000 || gBossHudStamp > simulationTime)
		gBossMaxEnergy = enemy->energy;
	gBossEnergy = enemy->energy;
	gBossHudStamp = simulationTime;

	if (enemy->state == LOFB_STATE_ARRIVING)
	{
		// Ease from the spawn position down to the hover point.
		float f = t / LOFB_ARRIVE_MS;
		if (f > 1) f = 1;
		f = 1 - (1 - f) * (1 - f);	// ease-out

		enemy->ss_position[X] = enemy->spawn_startPosition[X] + f * (enemy->spawn_endPosition[X] - enemy->spawn_startPosition[X]);
		enemy->ss_position[Y] = enemy->spawn_startPosition[Y] + f * (enemy->spawn_endPosition[Y] - enemy->spawn_startPosition[Y]);

		if (t >= LOFB_ARRIVE_MS)
		{
			enemy->state = LOFB_STATE_FIGHTING;
			enemy->parameters[P_FAN_CD]     = 900;	// first volley shortly after arrival
			enemy->parameters[P_SPIRAL_CD]  = 1500;
			enemy->parameters[P_MINION_CD]  = 4000;
			enemy->parameters[P_BIGSHOT_CD] = 7000;
		}
		return;
	}

	// FIGHTING. Time base is fight-relative so the sway starts at sin(0)=0 --
	// continuous with the arrival's end position (using total time made the boss
	// visibly JUMP sideways on its first lateral move).
	t -= LOFB_ARRIVE_MS;

	// Phase 1/2/3 by remaining HP (>2/3, >1/3, below).
	phase = 1;
	if (gBossMaxEnergy > 0)
	{
		if (enemy->energy <= (2 * gBossMaxEnergy) / 3) phase = 2;
		if (enemy->energy <= gBossMaxEnergy / 3)       phase = 3;
	}

	// Hover: slow horizontal sweep + a light vertical bob.
	enemy->ss_position[X] = sinf(t * (float)(2 * M_PI) / LOFB_SWAY_PERIOD_MS) * LOFB_SWAY_HALFWIDTH;
	enemy->ss_position[Y] = LOFB_HOVER_Y + 0.05f * sinf(t * (float)(2 * M_PI) / LOFB_BOB_PERIOD_MS);

	// Attack 1: aimed fan at the nearest player (always on; widens in phase 3).
	enemy->parameters[P_FAN_CD] -= timediff;
	if (enemy->parameters[P_FAN_CD] <= 0)
	{
		LOFB_FireFan(enemy, phase == 3 ? 5 : 3, 0.22f);
		enemy->parameters[P_FAN_CD] = 1700 - phase * 200;	// 1500 / 1300 / 1100 ms
	}

	// Attack 2 (phase 2+): twin rotating spiral, SHAB-style.
	if (phase >= 2)
	{
		enemy->parameters[P_SPIRAL_CD] -= timediff;
		if (enemy->parameters[P_SPIRAL_CD] <= 0)
		{
			float a = enemy->parameters[P_SPIRAL_ANGLE];
			emitSHABBullet(enemy, a);
			emitSHABBullet(enemy, a + (float)M_PI);
			enemy->parameters[P_SPIRAL_ANGLE] = a + 0.75f;
			enemy->parameters[P_SPIRAL_CD] = (phase == 3) ? 170 : 260;
		}
	}

	// Attack 3 (phase 2+): FHT escort waves, two per side.
	if (phase >= 2)
	{
		enemy->parameters[P_MINION_CD] -= timediff;
		if (enemy->parameters[P_MINION_CD] <= 0)
		{
			LOFB_SpawnMinion(-1.0f, 0.9f);
			LOFB_SpawnMinion( 1.0f, 0.9f);
			LOFB_SpawnMinion(-1.0f, 0.55f);
			LOFB_SpawnMinion( 1.0f, 0.55f);
			enemy->parameters[P_MINION_CD] = (phase == 3) ? 6500 : 9000;
		}
	}

	// Attack 4 (phase 2+): THE BIG SHOT -- a large, slower aimed orb.
	if (phase >= 2)
	{
		enemy->parameters[P_BIGSHOT_CD] -= timediff;
		if (enemy->parameters[P_BIGSHOT_CD] <= 0)
		{
			LOFB_FireBigShot(enemy);
			enemy->parameters[P_BIGSHOT_CD] = (phase == 3) ? 6000 : 8500;
		}
	}
}

void LOFB_OnBossDeath(enemy_t* enemy)
{
	// Deterministic pyro burst around the boss's last position.
	static const float off[6][2] = {
		{ 0.00f,  0.00f}, {-0.12f,  0.06f}, { 0.13f,  0.10f},
		{-0.07f, -0.09f}, { 0.09f, -0.06f}, { 0.00f,  0.14f},
	};
	vec2_t p;
	int i;

	for (i = 0; i < 6; i++)
	{
		p[X] = enemy->ss_position[X] + off[i][0];
		p[Y] = enemy->ss_position[Y] + off[i][1];
		FX_GetExplosion(p, IMPACT_TYPE_YELLOW, 1.6f - i * 0.15f, 0);
	}
	FX_GetSmoke(enemy->ss_position, 0.5f, 0.5f);
	SND_PlaySound(SND_EXPLOSION);

	// Victory bonus (each player; both sims add it at the same tick in MP), then
	// FREEZE the score: the win is scored at the killing blow -- leftover bullets
	// mopping up escorts during the victory lap don't count anymore.
	for (i = 0; i < numPlayers; i++)
		players[i].score += (LOFB_VICTORY_BONUS << engine.difficultyLevel);
	Native_UploadScore(players[controlledPlayer].score);
	gScoreLocked = 1;

	// End-of-act choreography, same as the other acts: the ships fly to their
	// rest position and the epilog title shows; when it ends, TITLE_Update calls
	// dEngine_GoToNextScene (which, act 3 being the last act, returns to the menu).
	EV_AutoPilotPls(0);
	TITLE_Show_epilog(7000);

	gBossHudStamp = -100000;	// hide the health bar right away
}

int LOFB_GetBossHealthBar(char* out)
{
	int i, n;

	if (gBossMaxEnergy <= 0)
		return 0;
	if (simulationTime - gBossHudStamp > 300 || gBossHudStamp > simulationTime)
		return 0;

	n = (gBossEnergy * 20) / gBossMaxEnergy;
	if (n < 0)  n = 0;
	if (n > 20) n = 20;

	memcpy(out, "BOSS ", 5);
	for (i = 0; i < 20; i++)
		out[5 + i] = (i < n) ? '=' : '-';
	out[25] = '\0';
	return 1;
}
