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
 *  enemy.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-02-09.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#include "globals.h"
#include "enemy.h"
#include "camera.h"
#include "timer.h"
#include "event.h"
#include "renderer.h"
#include "sounds.h"
#include "enemy_particules.h"
#include "dEngine.h"
#include "player.h"
#include "lee.h"
#include "lofb.h"
#include "shab.h"
#include "fht.h"
#include "tha.h"

//Warning this matrix is declared as row major: <-- Shit !! This line was actually useful 4 month later !!!! You are good fab !!!
/*
 1	0	0	0
 0	0  -1	0
 0   1	0	0
 0	0	0	1
 */
matrix_t enemyFromAboveRotation = 
{1 , 0  , 0 , 0,
	0 , 0  , 1 , 0,
	0 , -1 , 0 , 0,
	0 , 0  , 0 , 1,} ; 

enemy_t rootEnemy;

enemy_t enemies[MAX_NUM_ENEMIES];

uchar numFreeEnemies;
enemy_t* freeEnemies[MAX_NUM_ENEMIES];




void ENE_Mem_Init(void)
{
	int i;
	
	for (i=0; i < MAX_NUM_ENEMIES; i++) 
		freeEnemies[i] = &enemies[i];

	numFreeEnemies = MAX_NUM_ENEMIES;
	
	
	rootEnemy.next = NULL;
	rootEnemy.prev = NULL;
}


void ENE_Reset(void)
{
	memset(enemies,0,sizeof(enemies));
	memset(&rootEnemy,0,sizeof(rootEnemy));
	ENE_Mem_Init();
}


enemy_t dummyEnemy;
int uniqueIdGenerator=0;
enemy_t* ENE_Get(void)
{
	enemy_t* enemy;
	
	if (numFreeEnemies == 0)
	{
		Log_Printf("Enemy pool exhausted (%d). Aborting.\n",MAX_NUM_ENEMIES);
		return &dummyEnemy;
	}
	
	numFreeEnemies--;
	enemy = freeEnemies[numFreeEnemies];
	
	//Insert after rootEnemy
	enemy->next = rootEnemy.next;
	
	if (enemy->next != NULL)
		enemy->next->prev = enemy;
	
	enemy->uniqueId = uniqueIdGenerator++;
	enemy->entity.uid = enemy->uniqueId;
	enemy->state = 0;
	
	enemy->prev = &rootEnemy;
	rootEnemy.next = enemy;
	
	
	
	return enemy;
}

int ENE_GetNumEnemies(void)
{
	return MAX_NUM_ENEMIES - numFreeEnemies ;
}

void ENE_Release(enemy_t* enemy)
{
	//No Need to free enemy/entities ressources 
	// Even enemy->entity.indices is not allocated because enemies are fullDraw and GPU resident
	enemy->uniqueId = 0;
	enemy->entity.uid = 0;
	
	freeEnemies[numFreeEnemies] = enemy;
	numFreeEnemies++;
	
	if (enemy->prev != NULL)
		enemy->prev->next = enemy->next;
	
	if (enemy->next != NULL)
		enemy->next->prev = enemy->prev;
	

	
}


enemy_t* ENE_GetFirstEnemy(void)
{
	return rootEnemy.next;
}


void ENE_ReleaseAll(void)
{
	enemy_t* enemy;
	enemy_t* toRelease;
	
	enemy = ENE_GetFirstEnemy();
	
	while (enemy != NULL) 
	{
		toRelease = enemy;
		enemy = enemy->next;
		ENE_Release(toRelease);
	}
}



entity_t dummy;

void ENE_Precache(void)
{
	event_t* precacheEvent;
	event_spawnEnemy_payload_t* eventEnemyPayload;
	
	engine.playerStats.numEnemies=0;
	
	precacheEvent = &events;
	
	while (precacheEvent != NULL)
	{
		if (precacheEvent->type == EV_SPAWN_ENEMY)
		{
			engine.playerStats.numEnemies++;
			//Log_Printf("precache t=%denemy count %f.\n",precacheEvent->time,engine.playerStats.numEnemies);
			eventEnemyPayload = precacheEvent->payload;
			//Log_Printf("Precaching entity: %s.\n",enemyTypePath[eventEnemyPayload->type]);
			ENT_LoadEntity(&dummy, enemyTypePath[eventEnemyPayload->type],ENT_FULL_DRAW);
		}
		precacheEvent = precacheEvent->next;
	}
	

}


void ENE_AttachToCamera(matrix_t globalMatrix)
{
	vec4_t ws_enemyPos;
	vec4_t ss_enemyPos;
	enemy_t* enemy;
	
	enemy = ENE_GetFirstEnemy();
	
	while (enemy != NULL)
	{
		//enemy->entity.matrix[14] += -0.24f * timediff ;
		
		ws_enemyPos[X] = enemy->entity.matrix[12];
		ws_enemyPos[Y] = enemy->entity.matrix[13];
		ws_enemyPos[Z] = enemy->entity.matrix[14];
		ws_enemyPos[W] = 1;
		
		
		
		matrix_multiplyVertexByMatrix(ws_enemyPos,globalMatrix,ss_enemyPos);
		
		
		enemy->ss_position[X] = ss_enemyPos[X] / ss_enemyPos[W] ;
		enemy->ss_position[Y] = ss_enemyPos[Y] / ss_enemyPos[W] ;
		
		enemy= enemy->next;
	}
	
}

void ENE_UpdateSSBoundaries(enemy_t* enemy)
{
	// The boss (LOFB) is far bigger on screen than regular enemies; give it a
	// hitbox to match, so player bullets connect where the ship visually is.
	float r = (enemy->type == ENEMY_LOFB) ? 0.26f : 0.1f;

	// While the boss is still descending into position it is UNTOUCHABLE: park
	// its hitbox off-screen so early shots pass through (damage starts with its
	// first lateral move, when the fight actually begins).
	if (enemy->type == ENEMY_LOFB && enemy->state == LOFB_STATE_ARRIVING)
	{
		enemy->ss_boudaries[UP]   = 4*SS_H;
		enemy->ss_boudaries[DOWN] = 4*SS_H;
		enemy->ss_boudaries[LEFT] = 4*SS_W;
		enemy->ss_boudaries[RIGHT]= 4*SS_W;
		return;
	}

	enemy->ss_boudaries[UP]   =  (enemy->ss_position[Y] + r)*SS_H ;
	enemy->ss_boudaries[DOWN] =  (enemy->ss_position[Y] - r)*SS_H;
	enemy->ss_boudaries[LEFT] =  (enemy->ss_position[X] - r)*SS_W;
	enemy->ss_boudaries[RIGHT]=  (enemy->ss_position[X] + r)*SS_W;

}


void ENE_Update(void)
{
	
	enemy_t* enemy;
	entity_t* entity;
	matrix_t tmp;
	//matrix_t tmp2;
	
	/*
	matrix_t rollMatrix;
	matrix_t yawMatrix;
	matrix_t pitchMatrix;
*/
	matrix_t eulerMatrix;
	
	vec3_t translationForwardTransform;
	vec3_t translationRightTransform;
	vec3_t translationUpTransform;
	vec3_t translationTransform;	
	
	
	eulerMatrix[3] = 0;
	eulerMatrix[7] = 0;
	eulerMatrix[11] = 0;
	eulerMatrix[15] = 1;
	
	enemy = ENE_GetFirstEnemy();
	
	if (!entitiesAttachedToCamera)
	{
		while (enemy != NULL)
		{
			entity = &enemy->entity;
			if (!entitiesAttachedToCamera)
			{
				//memcpy(entity->matrix,enemyFromAboveRotation,16*sizeof(float));
				entity->matrix[14] += -0.24f * timediff ;
				enemy = enemy->next;
			}	
		}
	}
	
	else 
	{
		while (enemy != NULL)
		{
			entity = &enemy->entity;

			enemy->updateFunction(enemy);		
		
			//Update ss_boundaries for collision detection
			ENE_UpdateSSBoundaries(enemy);
		
		
			eulerMatrix[0] = cosf(entity->yAxisRot) * cosf(entity->zAxisRot) - sinf(entity->yAxisRot)*sinf(entity->xAxisRot)*sinf(entity->zAxisRot);
			eulerMatrix[1] = sinf(entity->yAxisRot) * cosf(entity->zAxisRot) + cosf(entity->yAxisRot)*sinf(entity->xAxisRot)*sinf(entity->zAxisRot);
			eulerMatrix[2] = -cosf(entity->xAxisRot) * sinf(entity->zAxisRot) ;
		
			eulerMatrix[4] = -sinf(entity->yAxisRot) * cosf(entity->xAxisRot) ;
			eulerMatrix[5] = cosf(entity->yAxisRot) * cosf(entity->xAxisRot) ;
			eulerMatrix[6] = sinf(entity->xAxisRot);
		
		
			eulerMatrix[8] = cosf(entity->yAxisRot) * sinf(entity->zAxisRot) + sinf(entity->yAxisRot)*sinf(entity->xAxisRot)*cosf(entity->zAxisRot);
			eulerMatrix[9] = sinf(entity->yAxisRot) * sinf(entity->zAxisRot) - cosf(entity->yAxisRot)*sinf(entity->xAxisRot)*cosf(entity->zAxisRot);
			eulerMatrix[10] = cosf(entity->xAxisRot) * cosf(entity->zAxisRot) ;
		
		
			// cameraInvRot * Rz * Rx * Ry * (fromAbove blended toward the TTB
			// profile). Upright the blend IS enemyFromAboveRotation bit-exact;
			// during the side view every craft tips to its profile like the
			// player does (enemies face the player, so the same arc that sends
			// the ship's nose screen-right sends theirs screen-left -- they
			// cross right-to-left). Their authored spins (eulerMatrix) apply in
			// view space on top, so spinners keep spinning face-on.
			{
				static matrix_t ttbEnemyBlend;
				static int      ttbBlendStamp = -1;
				// one build per frame, shared by every enemy
				if (ttbBlendStamp != simulationTime)
				{
					CAM_GetTTBBlend(ttbEnemyBlend, 0.0f);
					ttbBlendStamp = simulationTime;
				}
				matrix_multiply(eulerMatrix, ttbEnemyBlend, tmp);
			}
			matrix_multiply(cameraInvRot,tmp,entity->matrix);
				
		
			//Translation part
			vectorScale(camera.forward,distanceZFromCamera,translationForwardTransform);
			vectorScale(camera.right, enemy->ss_position[X] * widthAtDistance ,translationRightTransform);
			vectorScale(camera.up   , enemy->ss_position[Y] * heightAtDistance,translationUpTransform);
	
			vectorCopy(camera.position,									translationTransform) ;
			vectorAdd(translationTransform,translationForwardTransform,	translationTransform) ;
			vectorAdd(translationTransform,translationRightTransform,	translationTransform) ;
			vectorAdd(translationTransform,translationUpTransform,		translationTransform) ;
	
	
			entity->matrix[12] = translationTransform[X] ;
			entity->matrix[13] = translationTransform[Y] ;
			entity->matrix[14] = translationTransform[Z] ;
		
			enemy->timeCounter += timediff;
		
			enemy = enemy->next;
		}	
		
	}
}


#define DEVIL_TTR  700.0f
#define DEVIL_TIME_ONSCREEN (DEVIL_TTR + 4000)
// THE DEVIL'S ARMORY (2026). The enemy particle pool renders in the same
// pass as the player's bullets, with the player's bullet ATLAS bound
// (renderer_fixed.c: SetTextureF(bulletConfig.bulletTexture)) -- so a Devil
// can fire SHAB's red ball, THA's teardrop, or even steal the player's own
// yellow shot with nothing but texture coordinates. 128x128 atlas cells:
#define DEVIL_RED_U      (80/128.0f*SHRT_MAX)	// SHAB's classic red ball, 16x16
#define DEVIL_RED_V      (0/128.0f*SHRT_MAX)
#define DEVIL_RED_W      (16/128.0f*SHRT_MAX)
#define DEVIL_RED_H      (16/128.0f*SHRT_MAX)
#define DEVIL_THA_U      (16/128.0f*SHRT_MAX)	// THA's teardrop, 16x32, row 0
#define DEVIL_THA_V      (0/128.0f*SHRT_MAX)
#define DEVIL_THA_W      (16/128.0f*SHRT_MAX)
#define DEVIL_THA_H      (32/128.0f*SHRT_MAX)
#define DEVIL_YELLOW_U   (48/128.0f*SHRT_MAX)	// the player's bullet, colour column 3
#define DEVIL_YELLOW_V   (0/128.0f*SHRT_MAX)
#define DEVIL_YELLOW_W   (16/128.0f*SHRT_MAX)
#define DEVIL_YELLOW_H   (32/128.0f*SHRT_MAX)

#define DEVIL_BULLET_TTL   2600
#define DEVIL_BULLET_SIZE  0.055f

// One Devil bullet. offX/offY (ss units) displace the muzzle from the hull,
// dirX/dirY is the authored direction, speed = screens travelled over a ttl.
// Both the offset and the velocity rotate with the TTB beat, like every
// authored (non-aimed) enemy pattern in the act.
static void emitDevilBullet(enemy_t* enemy, float offX, float offY,
                            float dirX, float dirY, float speed,
                            float u, float v, float w, float h)
{
	enemy_part_t* bullet;
	float ox = offX * SS_H;
	float oy = offY * SS_H;
	float vx = dirX * speed * SS_H;
	float vy = dirY * speed * SS_H;
	float cx, cy;

	CAM_TTBRotateSS(&ox, &oy);
	CAM_TTBRotateSS(&vx, &vy);

	cx = enemy->ss_position[X] * SS_W + ox;
	cy = enemy->ss_position[Y] * SS_H + oy;

	bullet = ENPAR_GetNextParticule();
	bullet->ttl = DEVIL_BULLET_TTL;
	bullet->originalTTL = DEVIL_BULLET_TTL;

	bullet->ss_boudaries[UP]    = bullet->ss_starting_boudaries[UP]    = cy + DEVIL_BULLET_SIZE/2 * SS_H / gVScale;
	bullet->ss_boudaries[DOWN]  = bullet->ss_starting_boudaries[DOWN]  = cy - DEVIL_BULLET_SIZE/2 * SS_H / gVScale;
	bullet->ss_boudaries[LEFT]  = bullet->ss_starting_boudaries[LEFT]  = cx - DEVIL_BULLET_SIZE/2 * SS_H;
	bullet->ss_boudaries[RIGHT] = bullet->ss_starting_boudaries[RIGHT] = cx + DEVIL_BULLET_SIZE/2 * SS_H;

	// 0 3
	// 1 2
	bullet->text[0][U] = u;      bullet->text[0][V] = v;
	bullet->text[1][U] = u;      bullet->text[1][V] = v + h;
	bullet->text[2][U] = u + w;  bullet->text[2][V] = v + h;
	bullet->text[3][U] = u + w;  bullet->text[3][V] = v;

	bullet->posDiff[X] = vx;
	bullet->posDiff[Y] = vy;
}

void updateHAB(enemy_t* enemy)
{
	float f;
	int costume = (int)enemy->parameters[PARAMETER_HAB_COSTUME];

	// Device verdict on 1.6.5: the GHOST is too transparent -- "on ne le
	// voit a peine". Make it SHIMMER: the veil breathes between a readable
	// haze and a bright spectral flash (~every 1.3s) -- pure function of
	// simulationTime, lockstep-safe. Runs from the first frame of the
	// barrel-roll in, so the eye catches it entering. The anthracite keeps
	// its static stealth coat: that one read fine on device.
	if (costume == 2)
	{
		float s = 0.5f + 0.5f * sinf(simulationTime * 0.005f);
		s = s * s;
		enemy->entity.color[A] = 0.42f + s * 0.33f;
	}

	if (enemy->timeCounter < DEVIL_TTR)
	{

		f = enemy->timeCounter / DEVIL_TTR ;

		f= MIN(f,1);

		enemy->ss_position[X] = enemy->spawn_startPosition[X] + f*(enemy->spawn_endPosition[X] - enemy->spawn_startPosition[X]);
		enemy->ss_position[Y] = enemy->spawn_startPosition[Y] + f*(enemy->spawn_endPosition[Y] - enemy->spawn_startPosition[Y]);

		//As a devil is seen from above, the roll is actually a rotatiom around Y.
		enemy->entity.zAxisRot = (1-f)*enemy->entity.zAxisRot;


		return;
	}




	//ON screen and battle
	enemy->entity.zAxisRot = 0 ;

	// The costume picks the weapon (stashed by event.c). Silent during the
	// barrel-rolls; the guns run only while parked.
	if (enemy->timeCounter < DEVIL_TIME_ONSCREEN)
	{
		switch (costume) {
		case 1:
			// ANTHRACITE -- THE LASSO: a dense stream of THA teardrops
			// whose aim swings like a whip (+-31 degrees around straight
			// ahead, ~1.4s period); in flight the chain of bullets reads
			// as one undulating rope.
			if (simulationTime - enemy->lastTimeFired >= 130)
			{
				float ang = -M_PI/2 + 0.55f * sinf(enemy->timeCounter * 0.0045f);
				emitDevilBullet(enemy, 0, 0, cosf(ang), sinf(ang), 1.6f,
				                DEVIL_THA_U, DEVIL_THA_V, DEVIL_THA_W, DEVIL_THA_H);
				enemy->lastTimeFired = simulationTime;
			}
			break;

		case 2:
			// GHOST -- THE RIPPLES: every 1.4s a slow ring of the
			// player's own yellow bullets expands from the hull, like a
			// stone dropped in water (3 waves over the 4s window).
			if (simulationTime - enemy->lastTimeFired >= 1400)
			{
				int i;
				for (i = 0; i < 14; i++)
				{
					float ang = i * (2*M_PI/14);
					emitDevilBullet(enemy, 0, 0, cosf(ang), sinf(ang), 0.75f,
					                DEVIL_YELLOW_U, DEVIL_YELLOW_V, DEVIL_YELLOW_W, DEVIL_YELLOW_H);
				}
				enemy->lastTimeFired = simulationTime;
			}
			break;

		default:
			// ORIGINAL -- THE TRIDENT: three continuous straight streams
			// of classic red balls, one per prong.
			if (simulationTime - enemy->lastTimeFired >= 210)
			{
				emitDevilBullet(enemy, -0.10f, -0.04f, 0, -1, 2.0f,
				                DEVIL_RED_U, DEVIL_RED_V, DEVIL_RED_W, DEVIL_RED_H);
				emitDevilBullet(enemy,  0.00f, -0.07f, 0, -1, 2.0f,
				                DEVIL_RED_U, DEVIL_RED_V, DEVIL_RED_W, DEVIL_RED_H);
				emitDevilBullet(enemy,  0.10f, -0.04f, 0, -1, 2.0f,
				                DEVIL_RED_U, DEVIL_RED_V, DEVIL_RED_W, DEVIL_RED_H);
				enemy->lastTimeFired = simulationTime;
			}
			break;
		}
	}



	if (enemy->timeCounter > DEVIL_TIME_ONSCREEN)
	{
		f = (enemy->timeCounter - DEVIL_TIME_ONSCREEN) / DEVIL_TTR ;
		
		f= MIN(f,1);
		
		enemy->ss_position[X] = enemy->spawn_endPosition[X] + f*(enemy->spawn_startPosition[X] - enemy->spawn_endPosition[X]);
		enemy->ss_position[Y] = enemy->spawn_endPosition[Y] + f*(enemy->spawn_startPosition[Y] - enemy->spawn_endPosition[Y]);
		
		//As a devil is seen from above, the roll is actually a rotatiom around Y.
		enemy->entity.zAxisRot = f*enemy->entity.zAxisRot ;

	
		
	}
	
	if (enemy->timeCounter >= enemy->ttl)
		ENE_Release(enemy);
	
	
}






char* enemyTypePath[] = 
{
	"data/models/enemies/hab.obj.md5mesh",
	"data/models/enemies/fht.obj.md5mesh",
	"data/models/enemies/lee.obj.md5mesh",
	"data/models/enemies/shab.obj.md5mesh",
	"data/models/enemies/lofb.obj.md5mesh",
	"data/models/enemies/tha.obj.md5mesh",
	"data/models/enemies/fht.obj.md5mesh",	// ENEMY_MISSILE: no missile mesh exists, reuse the small FHT (tinted red in-flight)
};


updateFunction_t enemyTypeUpdateFct[] =
{
	updateHAB,
	updateFHT,
	updateLEE,
	updateSHAB,
	updateLOFB,
	updateTHA,
	updateLOFBMissile	// ENEMY_MISSILE
};


ushort enemyTypeEnergy[] =
{
	10,
	1,
	12,
	30,
	2500,	// LOFB, the act-3 boss (a player bullet deals 1) -- doubled to 5000 in multiplayer
	30,
	4		// ENEMY_MISSILE: a few bullets shoots it down (8 in multiplayer)
};

uint enemyScore[] =
{
	10000,
	2000,
	3000,
	10000,
	200000,
	10000,
	3000	// ENEMY_MISSILE
};
