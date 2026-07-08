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

// THE MEGA-LASER (Fabien's round-2 ask): every ~30-45s the boss stops, gathers
// energy (converging sparks -- the "particules qui se concentrent"), then fires
// a huge beam that pivots from straight-down and sweeps the lower screen. The
// safe ground is UP, in a top corner (the beam never points above the boss).
// It vaporises escort minions it crosses. Purely simulation-state driven, so it
// stays deterministic in lockstep multiplayer; the beam quads are pushed into
// enFxLib (the LEE-style enemy FX buffer), so no renderer change is needed.
#define LOFB_LASER_OFF			0
#define LOFB_LASER_CHARGING		1
#define LOFB_LASER_FIRING		2

#define LOFB_LASER_FIRST_MS		14000.0f	// first beam, into the fight
#define LOFB_LASER_PERIOD_MS	32000.0f	// then one every ~32s (30-45 window)
#define LOFB_LASER_CHARGE_MS	1500.0f		// telegraph: gather + warning line
#define LOFB_LASER_FIRE_MS		3500.0f		// beam sweep duration
#define LOFB_LASER_SWEEP_AMP	1.15f		// radians off straight-down (~66 deg)
#define LOFB_LASER_SWEEP_CYCLES	1.2f		// sweep speed (faster than 1.3.6, calmer than 1.3.5)
#define LOFB_LASER_HALFWIDTH	(0.12f * SS_H)	// beam half-thickness (pixels)
#define LOFB_LASER_LENGTH		(2.5f  * SS_H)	// beam length (crosses the screen)
#define LOFB_LASER_SPARKS		12			// converging charge sparks
#define LOFB_LASER_TEXT_U		((ushort)(72.0f / 128.0f * SHRT_MAX))	// solid white
#define LOFB_LASER_TEXT_V		((ushort)( 8.0f / 128.0f * SHRT_MAX))	// atlas texel
// Soft round blob (the SHAB/big-shot orb sprite) for sparks + muzzle glow, so
// they read as light rather than hard squares.
#define LOFB_ORB_U				((ushort)(80.0f / 128.0f * SHRT_MAX))
#define LOFB_ORB_V				((ushort)( 0.0f / 128.0f * SHRT_MAX))
#define LOFB_ORB_W				((ushort)(16.0f / 128.0f * SHRT_MAX))
#define LOFB_ORB_H				((ushort)(16.0f / 128.0f * SHRT_MAX))

// Boss HUD state (read by LOFB_GetBossHealthBar, rendered with the score).
static int gBossEnergy = 0;
static int gBossMaxEnergy = 0;
static int gBossHudStamp = -100000;	// simulationTime of the last boss update

// Mega-laser state (single boss -> file statics; all advanced from updateLOFB so
// both lockstep sims evolve them identically). The beam is drawn from the FX
// buffer; collisions read it through LOFB_GetLaserBeam.
static int   gLaserState    = LOFB_LASER_OFF;
static float gLaserCooldown = 0;	// ms until the next beam (while OFF)
static float gLaserTimer    = 0;	// ms left in the current sub-state
static float gLaserCharge   = 0;	// 0..1 charge progress (CHARGING)
static float gLaserDX = 0, gLaserDY = -1;	// beam unit direction (screen space)
static float gLaserOX = 0, gLaserOY = 0;	// beam origin (pixels: ss * SS_W/SS_H)
static float gSwayClock = 0;		// hover-sway time; pauses while the laser is up

// Homing-missile launcher (fired from the arms). Cooldown is a file static like
// the laser -- single boss, advanced deterministically from updateLOFB.
static float gMissileCooldown = 0;
#define LOFB_MISSILE_SPEED	0.85f	// ss units / second
#define LOFB_MISSILE_TURN	2.0f	// max turn rate (rad / second) -- low enough to juke
#define LOFB_MISSILE_TTL	6000	// ms before it fizzles out
#define LOFB_MISSILE_ARM_X	0.24f	// arm offset from the boss centre
#define LOFB_MISSILE_CD		6500.0f	// launch interval (phase 2)
#define LOFB_MISSILE_CD_P3	4500.0f	// launch interval (phase 3)
#define P_MISSILE_HEADING	0		// missile's own parameters[] slot (its heading)

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

// Launch one homing missile from an arm (side = -1 left, +1 right). It spawns as
// a normal ENEMY_MISSILE, so it has HP (shootable) and the shared death path
// gives it an explosion for free. updateLOFBMissile steers it toward the player.
static void LOFB_FireMissile(enemy_t* enemy, float side)
{
	event_t ev;
	event_spawnEnemy_payload_t pl;

	memset(&pl, 0, sizeof(pl));
	pl.type = ENEMY_MISSILE;
	pl.mouvementPatternType = MVMT_STRAIGHT;	// ignored -- the update fn homes
	pl.startPosition[X] = enemy->ss_position[X] + side * LOFB_MISSILE_ARM_X;
	pl.startPosition[Y] = enemy->ss_position[Y] - 0.05f;
	pl.endPosition[X]   = pl.startPosition[X];	pl.endPosition[Y]   = -1.3f;
	pl.controlPoint[X]  = pl.startPosition[X];	pl.controlPoint[Y]  = 0.0f;
	pl.ttl = LOFB_MISSILE_TTL;
	pl.subType = 0;								// NORMAL (energy from enemyTypeEnergy)

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_SPAWN_ENEMY;
	ev.payload = &pl;
	EV_SpawnEnemy(&ev);
}

// A boss homing missile: pops out at its arm, then steers toward the nearest
// player at a capped turn rate (dodgeable). Deterministic (player positions are
// lockstep-synced). Its HP + destruction are handled by COLL_CheckEnemies; this
// only drives motion, a red tint (to read as a missile, not an escort) and TTL.
void updateLOFBMissile(enemy_t* enemy)
{
	float dt = timediff / 1000.0f;
	float ax = 0, ay = -1, best = 1e9f;
	float ang, desired, diff, maxTurn;
	int i;

	// First frame: appear at the spawn (arm) point, heading downward.
	if (enemy->timeCounter == 0)
	{
		enemy->ss_position[X] = enemy->spawn_startPosition[X];
		enemy->ss_position[Y] = enemy->spawn_startPosition[Y];
		enemy->parameters[P_MISSILE_HEADING] = -(float)M_PI / 2.0f;
	}

	// Seek the nearest player.
	for (i = 0; i < numPlayers; i++)
	{
		float dx = players[i].ss_position[X] - enemy->ss_position[X];
		float dy = players[i].ss_position[Y] - enemy->ss_position[Y];
		float d2 = dx * dx + dy * dy;
		if (d2 < best) { best = d2; ax = dx; ay = dy; }
	}
	desired = atan2f(ay, ax);

	// Turn toward the target, but only so fast (leaves room to juke it).
	ang  = enemy->parameters[P_MISSILE_HEADING];
	diff = desired - ang;
	while (diff >  (float)M_PI) diff -= (float)(2 * M_PI);
	while (diff < -(float)M_PI) diff += (float)(2 * M_PI);
	maxTurn = LOFB_MISSILE_TURN * dt;
	if (diff >  maxTurn) diff =  maxTurn;
	if (diff < -maxTurn) diff = -maxTurn;
	ang += diff;
	enemy->parameters[P_MISSILE_HEADING] = ang;

	enemy->ss_position[X] += cosf(ang) * LOFB_MISSILE_SPEED * dt;
	enemy->ss_position[Y] += sinf(ang) * LOFB_MISSILE_SPEED * dt;

	// Point the model along its travel (seen from above -> Z) and tint it red-hot.
	enemy->entity.zAxisRot = ang + (float)M_PI / 2.0f;
	enemy->entity.color[R] = 1.0f;
	enemy->entity.color[G] = 0.35f;
	enemy->entity.color[B] = 0.15f;
	enemy->entity.color[A] = 1.0f;

	// End of life, or gone off the bottom / far to the sides.
	if (enemy->timeCounter >= enemy->ttl ||
		enemy->ss_position[Y] < -1.5f ||
		enemy->ss_position[X] < -1.7f || enemy->ss_position[X] > 1.7f)
		ENE_Release(enemy);
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

// --- Mega-laser ------------------------------------------------------------

// Append one white-texel quad (4 arbitrary corners) to the enemy FX buffer --
// same mechanism LEE uses. Per-vertex alpha (aa..ad, one per corner) lets the
// beam fade smoothly at its edges instead of ending in a hard rectangle.
static void LOFB_PushQuadA(float ax, float ay, float bx, float by,
						   float cx, float cy, float dx, float dy,
						   ubyte r, ubyte g, ubyte b,
						   ubyte aa, ubyte ab, ubyte ac, ubyte ad)
{
	xf_sprite_t* s;
	float px[4], py[4];
	ubyte pa[4];
	int i;

	if (enFxLib.num_vertices + 4 > 4 * MAX_NUM_ENEMY_FX)
		return;

	px[0] = ax; py[0] = ay; pa[0] = aa;
	px[1] = bx; py[1] = by; pa[1] = ab;
	px[2] = cx; py[2] = cy; pa[2] = ac;
	px[3] = dx; py[3] = dy; pa[3] = ad;

	s = &enFxLib.ss_vertices[enFxLib.num_vertices];
	for (i = 0; i < 4; i++)
	{
		s->pos[X]  = (short)px[i];
		s->pos[Y]  = (short)py[i];
		s->text[U] = LOFB_LASER_TEXT_U;
		s->text[V] = LOFB_LASER_TEXT_V;
		s->color[R] = r; s->color[G] = g; s->color[B] = b; s->color[A] = pa[i];
		s++;
	}
	enFxLib.num_vertices += 4;
	enFxLib.num_indices  += 6;
}

// A soft round sprite (the orb texel) centred at (cx,cy), half-size hs. Used for
// the charge sparks and the muzzle glow so they read as light, not squares.
static void LOFB_PushSprite(float cx, float cy, float hs,
							ubyte r, ubyte g, ubyte b, ubyte a)
{
	xf_sprite_t* s;
	short xs[4], ys[4];
	ushort us[4], vs[4];
	int i;

	if (enFxLib.num_vertices + 4 > 4 * MAX_NUM_ENEMY_FX)
		return;

	xs[0] = (short)(cx - hs); ys[0] = (short)(cy + hs); us[0] = LOFB_ORB_U;			vs[0] = LOFB_ORB_V;
	xs[1] = (short)(cx - hs); ys[1] = (short)(cy - hs); us[1] = LOFB_ORB_U;			vs[1] = LOFB_ORB_V + LOFB_ORB_H;
	xs[2] = (short)(cx + hs); ys[2] = (short)(cy - hs); us[2] = LOFB_ORB_U + LOFB_ORB_W; vs[2] = LOFB_ORB_V + LOFB_ORB_H;
	xs[3] = (short)(cx + hs); ys[3] = (short)(cy + hs); us[3] = LOFB_ORB_U + LOFB_ORB_W; vs[3] = LOFB_ORB_V;

	s = &enFxLib.ss_vertices[enFxLib.num_vertices];
	for (i = 0; i < 4; i++)
	{
		s->pos[X]  = xs[i]; s->pos[Y] = ys[i];
		s->text[U] = us[i]; s->text[V] = vs[i];
		s->color[R] = r; s->color[G] = g; s->color[B] = b; s->color[A] = a;
		s++;
	}
	enFxLib.num_vertices += 4;
	enFxLib.num_indices  += 6;
}

// The beam, drawn as a stack of parallel strips across its width so the alpha
// rises to a bright core and fades to nothing at the edges (soft glow), plus a
// gentle fade toward the far tip for depth. `peak` is the core alpha (0..255).
static void LOFB_PushBeamSoft(float ox, float oy, float dx, float dy,
							  float hw, float len, float peak)
{
	static const float frac[7] = {-1.0f, -0.60f, -0.28f, 0.0f, 0.28f, 0.60f, 1.0f};
	static const float aScl[7] = { 0.0f,  0.30f,  0.78f, 1.0f, 0.78f, 0.30f, 0.0f};
	const float tipFade = 0.40f;			// far end melts into the distance
	float px = -dy, py = dx;				// unit perpendicular
	int i;

	for (i = 0; i < 6; i++)
	{
		float p0 = frac[i]   * hw, p1 = frac[i + 1] * hw;
		float a0 = aScl[i]   * peak, a1 = aScl[i + 1] * peak;
		float m  = (fabsf(frac[i]) + fabsf(frac[i + 1])) * 0.5f;	// 0 core .. 1 edge
		ubyte r  = (ubyte)(255.0f - m * 165.0f);	// white core -> cyan edge
		ubyte g  = (ubyte)(255.0f - m *  55.0f);
		ubyte b  = 255;

		float n0x = ox + px * p0,           n0y = oy + py * p0;
		float n1x = ox + px * p1,           n1y = oy + py * p1;
		float f0x = ox + dx * len + px * p0, f0y = oy + dy * len + py * p0;
		float f1x = ox + dx * len + px * p1, f1y = oy + dy * len + py * p1;

		LOFB_PushQuadA(n0x, n0y, n1x, n1y, f1x, f1y, f0x, f0y, r, g, b,
					   (ubyte)a0, (ubyte)a1,
					   (ubyte)(a1 * tipFade), (ubyte)(a0 * tipFade));
	}
}

// A smooth round glow: a fan of wedges, bright at the centre and fading to zero
// alpha at the rim (Gouraud gradient on the flat white texel). Unlike a scaled-up
// 16px orb sprite it has no visible pixels, so the muzzle halo stays clean.
#define LOFB_GLOW_WEDGES 14
static void LOFB_PushGlow(float cx, float cy, float radius,
						  ubyte r, ubyte g, ubyte b, ubyte centerA)
{
	int i;
	for (i = 0; i < LOFB_GLOW_WEDGES; i++)
	{
		float a0 = i       * (float)(2 * M_PI) / LOFB_GLOW_WEDGES;
		float a1 = (i + 1) * (float)(2 * M_PI) / LOFB_GLOW_WEDGES;
		float x0 = cx + cosf(a0) * radius, y0 = cy + sinf(a0) * radius;
		float x1 = cx + cosf(a1) * radius, y1 = cy + sinf(a1) * radius;
		// Degenerate first tri (centre,centre,rim1); the wedge is tri (centre,rim1,rim0).
		LOFB_PushQuadA(cx, cy, cx, cy, x1, y1, x0, y0, r, g, b,
					   centerA, centerA, 0, 0);
	}
}

// Advance the laser timers/state. Direction points straight down while charging
// (telegraph) and sweeps left/right about straight-down while firing.
static void LOFB_UpdateLaser(enemy_t* enemy)
{
	(void)enemy;

	if (gLaserState == LOFB_LASER_OFF)
	{
		gLaserDX = 0; gLaserDY = -1;
		gLaserCooldown -= timediff;
		if (gLaserCooldown <= 0)
		{
			gLaserState  = LOFB_LASER_CHARGING;
			gLaserTimer  = LOFB_LASER_CHARGE_MS;
			gLaserCharge = 0;
			// Throw in an escort wave to be sliced by the beam.
			LOFB_SpawnMinion(-1.0f, 0.70f);
			LOFB_SpawnMinion( 1.0f, 0.70f);
			LOFB_SpawnMinion(-1.0f, 0.35f);
			LOFB_SpawnMinion( 1.0f, 0.35f);
		}
		return;
	}

	if (gLaserState == LOFB_LASER_CHARGING)
	{
		gLaserDX = 0; gLaserDY = -1;
		gLaserTimer -= timediff;
		gLaserCharge = 1.0f - gLaserTimer / LOFB_LASER_CHARGE_MS;
		if (gLaserCharge < 0) gLaserCharge = 0;
		if (gLaserCharge > 1) gLaserCharge = 1;
		if (gLaserTimer <= 0)
		{
			gLaserState  = LOFB_LASER_FIRING;
			gLaserTimer  = LOFB_LASER_FIRE_MS;
			gLaserCharge = 1.0f;
			SND_PlaySound(SND_EXPLOSION);	// beam ignition
		}
		return;
	}

	/* LOFB_LASER_FIRING */
	{
		float frac, sweep, ang;
		gLaserTimer -= timediff;
		frac = 1.0f - gLaserTimer / LOFB_LASER_FIRE_MS;
		if (frac < 0) frac = 0;
		if (frac > 1) frac = 1;
		sweep = LOFB_LASER_SWEEP_AMP * sinf(frac * (float)(2 * M_PI) * LOFB_LASER_SWEEP_CYCLES);
		ang   = -(float)(M_PI / 2.0) + sweep;	// straight-down + sweep
		gLaserDX = cosf(ang);
		gLaserDY = sinf(ang);
		if (gLaserTimer <= 0)
		{
			gLaserState    = LOFB_LASER_OFF;
			gLaserCooldown = LOFB_LASER_PERIOD_MS;
		}
	}
}

// Draw the laser for this frame (charge sparks + telegraph, or the live beam)
// and publish the beam origin for collisions. Boss position is frozen while the
// laser is up (gSwayClock paused), so the pivot is stable.
static void LOFB_EmitLaserFX(enemy_t* enemy)
{
	float ox, oy;

	if (gLaserState == LOFB_LASER_OFF)
		return;

	gLaserOX = ox = enemy->ss_position[X] * SS_W;
	gLaserOY = oy = enemy->ss_position[Y] * SS_H;

	if (gLaserState == LOFB_LASER_CHARGING)
	{
		int i;
		float rad  = (1.0f - gLaserCharge) * (0.55f * SS_H);	// sparks converge inward
		ubyte al   = (ubyte)(150 + gLaserCharge * 105);
		float core = (0.06f + 0.16f * gLaserCharge) * SS_H;	// gathering ball grows

		// A soft ball of energy swelling at the muzzle...
		LOFB_PushGlow(ox, oy, core * 1.9f, 110, 200, 255, (ubyte)(30 + gLaserCharge * 90));
		LOFB_PushGlow(ox, oy, core,        255, 255, 255, (ubyte)(50 + gLaserCharge * 150));

		// ...fed by sparks spiralling inward.
		for (i = 0; i < LOFB_LASER_SPARKS; i++)
		{
			float ang = i * (float)(2 * M_PI) / LOFB_LASER_SPARKS + gLaserCharge * 9.0f;
			float sx  = ox + cosf(ang) * rad;
			float sy  = oy + sinf(ang) * rad;
			float hs  = 0.024f * SS_H * (0.6f + 0.6f * gLaserCharge);
			LOFB_PushSprite(sx, sy, hs, 210, 240, 255, al);
		}
		// Faint, soft warning line where the beam is about to erupt.
		LOFB_PushBeamSoft(ox, oy, gLaserDX, gLaserDY,
						  LOFB_LASER_HALFWIDTH * 0.5f, LOFB_LASER_LENGTH,
						  30.0f + gLaserCharge * 110.0f);
		return;
	}

	/* LOFB_LASER_FIRING: soft-edged beam with a bright core, a subtle energy
	   pulse, and a layered muzzle glow. */
	{
		float pulse = 0.85f + 0.15f * sinf((LOFB_LASER_FIRE_MS - gLaserTimer) * 0.02f);
		LOFB_PushBeamSoft(ox, oy, gLaserDX, gLaserDY,
						  LOFB_LASER_HALFWIDTH, LOFB_LASER_LENGTH, 235.0f * pulse);
		LOFB_PushGlow(ox, oy, 0.36f * SS_H, 110, 200, 255, (ubyte)(120 * pulse));
		LOFB_PushGlow(ox, oy, 0.19f * SS_H, 255, 255, 255, (ubyte)(225 * pulse));
	}
}

// Collision query: fills the beam ray (pixel space) and returns 1 only while the
// beam is actually FIRING (charging is harmless). Read by collisions.c.
int LOFB_GetLaserBeam(float* ox, float* oy, float* dx, float* dy,
					  float* halfWidth, float* length)
{
	if (gLaserState != LOFB_LASER_FIRING)
		return 0;
	*ox = gLaserOX; *oy = gLaserOY;
	*dx = gLaserDX; *dy = gLaserDY;
	*halfWidth = LOFB_LASER_HALFWIDTH;
	*length    = LOFB_LASER_LENGTH;
	return 1;
}

void updateLOFB(enemy_t* enemy)
{
	float t;
	int phase;

	enemy->parameters[P_TIME] += timediff;
	t = enemy->parameters[P_TIME];

	// Publish HP to the HUD. Max HP is captured while ARRIVING (full HP, and the
	// boss is invulnerable then) and frozen for the rest of the fight. Do NOT
	// re-baseline on a time gap: a frame hitch or an app background/resume would
	// otherwise reset the max to the CURRENT (already-reduced) energy, making the
	// bar visibly refill -- which read as an immortal boss.
	if (enemy->state == LOFB_STATE_ARRIVING)
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
			// Fresh fight: reset the laser + hover clock + missile launcher.
			gLaserState      = LOFB_LASER_OFF;
			gLaserCooldown   = LOFB_LASER_FIRST_MS;
			gSwayClock       = 0;
			gMissileCooldown = LOFB_MISSILE_CD;
		}
		return;
	}

	// Phase 1/2/3 by remaining HP. Phase 1 (triple-fan-only) is deliberately
	// short now (top 15%) -- Fabien found it dragged; the varied attacks start
	// almost right away.
	phase = 1;
	if (gBossMaxEnergy > 0)
	{
		if (enemy->energy <= (85 * gBossMaxEnergy) / 100) phase = 2;
		if (enemy->energy <= (40 * gBossMaxEnergy) / 100) phase = 3;
	}

	// The mega-laser runs on its own clock, independent of HP phases, so a beam
	// shows up every ~30-45s whatever the boss's health.
	LOFB_UpdateLaser(enemy);

	// Hover: slow horizontal sweep + a light vertical bob. The sway clock pauses
	// while the laser is up, so the boss braces perfectly still to fire and then
	// resumes its sway seamlessly (no jump).
	if (gLaserState == LOFB_LASER_OFF)
		gSwayClock += timediff;
	enemy->ss_position[X] = sinf(gSwayClock * (float)(2 * M_PI) / LOFB_SWAY_PERIOD_MS) * LOFB_SWAY_HALFWIDTH;
	enemy->ss_position[Y] = LOFB_HOVER_Y + 0.05f * sinf(gSwayClock * (float)(2 * M_PI) / LOFB_BOB_PERIOD_MS);

	// While charging or firing the beam, the boss holds its fire: the laser IS
	// the phase. (Escort minions already on-screen keep coming -- and get fried.)
	if (gLaserState == LOFB_LASER_OFF)
	{
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

		// Attack 3 (phase 2+): FHT escort waves -- three per side now (Fabien
		// wanted twice as many gêneurs).
		if (phase >= 2)
		{
			enemy->parameters[P_MINION_CD] -= timediff;
			if (enemy->parameters[P_MINION_CD] <= 0)
			{
				LOFB_SpawnMinion(-1.0f, 0.90f);
				LOFB_SpawnMinion( 1.0f, 0.90f);
				LOFB_SpawnMinion(-1.0f, 0.60f);
				LOFB_SpawnMinion( 1.0f, 0.60f);
				LOFB_SpawnMinion(-1.0f, 0.30f);
				LOFB_SpawnMinion( 1.0f, 0.30f);
				enemy->parameters[P_MINION_CD] = (phase == 3) ? 5000 : 6500;
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

		// Attack 5 (phase 2+): the arms launch destructible homing missiles --
		// they seek the player but can be shot down (a few hits each).
		if (phase >= 2)
		{
			gMissileCooldown -= timediff;
			if (gMissileCooldown <= 0)
			{
				LOFB_FireMissile(enemy, -1.0f);
				LOFB_FireMissile(enemy,  1.0f);
				gMissileCooldown = (phase == 3) ? LOFB_MISSILE_CD_P3 : LOFB_MISSILE_CD;
			}
		}
	}

	// Draw the laser (charge sparks / live beam) into the enemy FX buffer.
	LOFB_EmitLaserFX(enemy);
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
	gLaserState   = LOFB_LASER_OFF;	// kill any beam in flight
}

int LOFB_GetBossHealthBar(char* out)
{
	int i, n;

	if (gBossMaxEnergy <= 0)
		return 0;
	if (simulationTime - gBossHudStamp > 300 || gBossHudStamp > simulationTime)
		return 0;

	// Round UP so any remaining HP keeps at least one segment lit -- the bar now
	// empties exactly when the boss dies (floor division used to read empty at
	// the last ~5%, so the boss looked dead-but-alive).
	if (gBossEnergy <= 0)
		n = 0;
	else
	{
		n = (gBossEnergy * 20 + gBossMaxEnergy - 1) / gBossMaxEnergy;
		if (n < 1)  n = 1;
		if (n > 20) n = 20;
	}

	memcpy(out, "BOSS ", 5);
	for (i = 0; i < 20; i++)
		out[5 + i] = (i < n) ? '=' : '-';
	out[25] = '\0';
	return 1;
}
