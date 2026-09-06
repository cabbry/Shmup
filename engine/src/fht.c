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
 *  fht.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-11-03.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#include "fht.h"
#include "fx.h"
#include "sounds.h"
#include "camera.h"	// CAM_TTBRotateSS: the tumble axis follows the beat
#include <math.h>	// v3: explicit -- the Xcode prefix header hid the dependency (implicit-declaration class)

//#define FHT_TTL  6000.0f
#define FHT_NUM_ROTATION 3

// TTB, round 4 of the hedgehog spin -- the one proven by spin_rig BEFORE
// shipping. Ground truth from enemy.c's euler formulas: the axis NAMES are
// permuted -- at x=z=0 "yAxisRot" builds a rotation about the matrix Z, and
// "zAxisRot" one about the matrix Y (its middle column stays (0,1,0)).
// - upright, enemy.c composes euler*blend and the classic look is yAxisRot
//   (2010, untouched);
// - side view, enemy.c composes blend34*euler for the FHT, so the euler acts
//   in MODEL space -- and the spin must be about the model's disc axis
//   (model Y, the thin 4.4-unit axis), which the permuted names spell
//   "zAxisRot". Rig-measured: disc normal drifts 0.0 degrees over a full
//   turn (the 3/4 ellipse holds still, the spikes wheel inside it); every
//   other axis/order combination reproduces a device-rejected build
//   (198 loopings / 200 yaw / 201 tilted loopings).
static void FHT_SetSpin(enemy_t* enemy, float spin)
{
	float qx = 0, qy = -1;
	CAM_TTBRotateSS(&qx, &qy);
	if (qx < -0.707f || qx > 0.707f)
	{
		enemy->entity.zAxisRot = spin;
		enemy->entity.yAxisRot = 0;
	}
	else
	{
		enemy->entity.yAxisRot = spin;
		enemy->entity.zAxisRot = 0;
	}
}
//cos (MINE_ROTATION_SPEED_RAD_MS)
//#define MINE_ROTATION_COS 0.999980262
//sin (MINE_ROTATION_SPEED_RAD_MS)
//#define MINE_ROTATION_SIN 0.00628295866

void updateXSin(enemy_t* enemy)
{
	float f;
	float oneMinusF;
	
	f = enemy->timeCounter / enemy->fttl ;	
	oneMinusF = 1 -f;
	
	FHT_SetSpin(enemy, f * FHT_NUM_ROTATION*1.5 * 2.0f * M_PI);
	
	//enemy->ss_position[X] = oneMinusF*oneMinusF * enemy->spawn_startPosition[X] + 2*oneMinusF*f*enemy->spawn_controlPoint[X]+ f*f*enemy->spawn_endPosition[X];
	enemy->ss_position[X] = enemy->parameters[PARAMETER_FHT_X_POS] +  enemy->parameters[PARAMETER_FHT_X_WIDTH]*cos(enemy->spawn_startPosition[X]*M_PI/2 + f * M_PI  * 2*2);
	enemy->ss_position[Y] = oneMinusF*oneMinusF * enemy->spawn_startPosition[Y] + 2*oneMinusF*f*enemy->spawn_controlPoint[Y]+f*f*enemy->spawn_endPosition[Y];
	
	if (enemy->timeCounter > enemy->ttl)
		ENE_Release(enemy);

}

void updateStraight(enemy_t* enemy)
{
	float f;
	float oneMinusF;
	
	
	
	f = enemy->timeCounter / enemy->fttl ;	
	oneMinusF = 1 -f;
	
	FHT_SetSpin(enemy, f * FHT_NUM_ROTATION * 2.0f * M_PI);
	
	//Log_Printf("f=%.2f\n",f);
	
	enemy->ss_position[X] = oneMinusF*oneMinusF * enemy->spawn_startPosition[X] + 2*oneMinusF*f*enemy->spawn_controlPoint[X]+ f*f*enemy->spawn_endPosition[X];
	enemy->ss_position[Y] = oneMinusF*oneMinusF * enemy->spawn_startPosition[Y] + 2*oneMinusF*f*enemy->spawn_controlPoint[Y]+f*f*enemy->spawn_endPosition[Y];
	
	if (enemy->timeCounter > enemy->ttl)
		ENE_Release(enemy);

}


void updateCircle(enemy_t* enemy)
{
	float f;
	float angle;
	//float cosAngle;
	//float sinAngle;
	
	
	f = enemy->timeCounter / enemy->fttl ;
	
	angle = f * FHT_NUM_ROTATION * 2.0f * M_PI;
	
	FHT_SetSpin(enemy, angle);
	
	//cosAngle = cosf(angle);
	//sinAngle = sinf(angle);
	f -= 1;
	enemy->ss_position[X] = f * cosf(enemy->spawn_startPosition[X]+angle) * 1.3f* SS_H / SS_W;
	enemy->ss_position[Y] = f * sinf(enemy->spawn_startPosition[X]+angle) * 1.3f;
	
	//enemy->ss_position[X] = (1-f)*  enemy->spawn_startPosition[X] ;
	//enemy->ss_position[Y] = (1-f)*  enemy->spawn_startPosition[Y] ;
	
	//Log_Printf("ene pso = %.2f, %.2f.\n",enemy->ss_position[X],enemy->ss_position[Y]);
	
	
	
	
	if (enemy->timeCounter > enemy->ttl)
	{
		enemy->ss_position[X] += (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1) * 0.1 ;
		enemy->ss_position[Y] += (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1) * 0.1 ;
		FX_GetExplosion(enemy->ss_position,IMPACT_TYPE_YELLOW,1,0);
		Spawn_EntityParticules(enemy->ss_position);
		FX_GetSmoke(enemy->ss_position, 0.3, 0.3);
		enemy->ss_position[X] += (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1) * 0.2 ;
		enemy->ss_position[Y] += (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1) * 0.2 ;
		FX_GetSmoke(enemy->ss_position, 0.3, 0.3);
		SND_PlaySound(SND_EXPLOSION);
		ENE_Release(enemy);
	}
	
}

void updateFHT(enemy_t* enemy)
{
	switch (enemy->entity.mouvementPatternType) 
	{
		case MVMT_CIRCLE: enemy->updateFunction = updateCircle; break;
		case MVMT_STRAIGHT: enemy->updateFunction = updateStraight; break;
		case MVMT_X_SIN: enemy->updateFunction = updateXSin; break;
		default: break;
	}
	
	enemy->updateFunction(enemy);
}
