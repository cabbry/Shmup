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
 *  lofb.h
 *  dEngine
 *
 *  Created by fabien sanglard on 10-10-25.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#ifndef DE_LOFB
#define DE_LOFB

#include "enemy.h"

// The LOFB is THE BOSS (act 3, "Water"). The 2010 code shipped with an empty
// updateLOFB and a scaffolded state machine; the fight is now implemented.

// Boss states (public: enemy.c parks the hitbox off-screen while ARRIVING, so
// the boss can't take damage until its first lateral move).
#define LOFB_STATE_ARRIVING	0
#define LOFB_STATE_FIGHTING	1

void updateLOFB(enemy_t* enemy);

// Per-frame update for a boss homing missile (ENEMY_MISSILE). Seeks the nearest
// player at a limited turn rate (so it can be juked) and can be shot down -- it
// spawns as a normal enemy so COLL_CheckEnemies already handles its HP + death.
void updateLOFBMissile(enemy_t* enemy);

// Called from the enemy-death path in collisions.c when the boss's energy
// reaches 0: victory pyrotechnics, score bonus, then the end-of-act
// choreography (autopilot + epilog; the epilog's end advances the scene).
void LOFB_OnBossDeath(enemy_t* enemy);

// Fills out (>= 32 bytes) with the "BOSS ====----" health bar and returns 1
// while a boss fight is active, 0 otherwise. Rendered from the HUD text block.
int LOFB_GetBossHealthBar(char* out);

// Mega-laser collision query. When the beam is actively FIRING, returns 1 and
// fills the beam ray in sprite pixel space: origin (ox,oy), unit direction
// (dx,dy), half-thickness and length. Returns 0 otherwise (idle or just the
// harmless charge/telegraph). Used by collisions.c to hit players and minions.
int LOFB_GetLaserBeam(float* ox, float* oy, float* dx, float* dy,
					  float* halfWidth, float* length);

// Destructible arms. LOFB_GetArm returns 1 (and fills the arm's ss centre +
// radius) while arm idx (0=left,1=right) is alive and the boss is on-screen.
// LOFB_DamageArm applies bullet damage; the arm is destroyed at 0 HP and its
// side stops firing homing missiles. Both used by collisions.c.
int  LOFB_GetArm(int idx, float* ssx, float* ssy, float* radius);
void LOFB_DamageArm(int idx, int dmg);

#endif