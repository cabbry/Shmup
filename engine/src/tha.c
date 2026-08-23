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
 *  tha.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-11-11.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#include "tha.h"
#include "camera.h"	// CAM_TTBRotateSS: the curtain's direction follows the beat
#include "player.h"
#include "enemy_particules.h"
#include "timer.h"
#include "sounds.h"

#define THA_TTR  700.0f
#define THA_TIME_ONSCREEN (THA_TTR + enemy->ttl)


void THA_Unspawning(enemy_t* enemy)
{
	float f;
	
	
	f = (enemy->timeCounter - THA_TIME_ONSCREEN) / THA_TTR ;
	
	f=MIN(f,1);
	f= sinf(f*2*M_PI/4);
	
	enemy->ss_position[X] = enemy->spawn_endPosition[X] + f*(enemy->spawn_startPosition[X] - enemy->spawn_endPosition[X]);
	enemy->ss_position[Y] = enemy->spawn_endPosition[Y] + f*(enemy->spawn_startPosition[Y] - enemy->spawn_endPosition[Y]);
	
	//As a devil is seen from above, the roll is actually a rotatiom around Y.
	enemy->entity.zAxisRot = f*enemy->spawn_Z_AxisRot;
	
	if (enemy->timeCounter >= enemy->ttl)
		ENE_Release(enemy);
}

#define THA_BULLET_TTL 600


#define THA_TEXT_BULLET_U (16/128.0f*SHRT_MAX)
#define THA_TEXT_BULLET_V (0/128.0f*SHRT_MAX)
#define THA_TEXT_BULLET_WIDTH (16/128.0f*SHRT_MAX)
#define THA_TEXT_BULLET_HEIGHT (32/128.0f*SHRT_MAX)

#define THA_BULLET_HEIGHT  (86/1.4f)
#define THA_BULLET_WIDTH 15

void THA_FireBullet(float ssPosX, float ssPosY,enemy_t* enemy)
{
	enemy_part_t* bullet;
	vec2_t random ;
	vec2_t ss_position;	
	
	ss_position[X] = ssPosX;
	ss_position[Y] = ssPosY;
	
	bullet = ENPAR_GetNextParticule();
	
	bullet->ttl = THA_BULLET_TTL;
	bullet->originalTTL = THA_BULLET_TTL;
	
	
	// 0 3
	// 1 2
	
	// TTB: the curtain's direction AND the drop sprite follow the beat. The
	// quad is an axis-aligned rect, so in the side view its long side must go
	// horizontal and the texture take a quarter turn, head still leading
	// (device verdict on 197: velocity rotated but "les balles sont
	// verticales"). Upright the mapping below is the 2010 code bit-exact.
	{
		float travelX = 0, travelY = enemy->parameters[PARAMETER_THA_FIRING_DIRECTION];
		ushort uvBase[4][2];
		int shift, k;
		float vb;

		CAM_TTBRotateSS(&travelX, &travelY);

		vb = (enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE ]*THA_TEXT_BULLET_HEIGHT);

		// 2010 corner UVs (screen: 0=TL 1=BL 2=BR 3=TR). Validated upright
		// look for a downward curtain: the low-V texture edge is the head.
		uvBase[0][U] = THA_TEXT_BULLET_U + THA_TEXT_BULLET_WIDTH;	uvBase[0][V] = vb + THA_TEXT_BULLET_HEIGHT;
		uvBase[1][U] = THA_TEXT_BULLET_U + THA_TEXT_BULLET_WIDTH;	uvBase[1][V] = vb;
		uvBase[2][U] = THA_TEXT_BULLET_U;							uvBase[2][V] = vb;
		uvBase[3][U] = THA_TEXT_BULLET_U;							uvBase[3][V] = vb + THA_TEXT_BULLET_HEIGHT;

		if (travelX < -0.707f || travelX > 0.707f)
		{
			// long side horizontal; head (low-V corners) toward the travel
			bullet->ss_boudaries[UP]   = bullet->ss_starting_boudaries[UP]   = ssPosY*SS_H + THA_BULLET_WIDTH / gVScale;
			bullet->ss_boudaries[DOWN] = bullet->ss_starting_boudaries[DOWN] = ssPosY*SS_H - THA_BULLET_WIDTH / gVScale;
			bullet->ss_boudaries[LEFT] = bullet->ss_starting_boudaries[LEFT] = ssPosX*SS_W - THA_BULLET_HEIGHT;
			bullet->ss_boudaries[RIGHT]= bullet->ss_starting_boudaries[RIGHT]= ssPosX*SS_W + THA_BULLET_HEIGHT;
			shift = (travelX < 0) ? 3 : 1;	// quarter turn CW / CCW
		}
		else
		{
			bullet->ss_boudaries[UP]   = bullet->ss_starting_boudaries[UP]   = ssPosY*SS_H + THA_BULLET_HEIGHT / gVScale;
			bullet->ss_boudaries[DOWN] = bullet->ss_starting_boudaries[DOWN] = ssPosY*SS_H - THA_BULLET_HEIGHT / gVScale;
			bullet->ss_boudaries[LEFT] = bullet->ss_starting_boudaries[LEFT] = ssPosX*SS_W - THA_BULLET_WIDTH;
			bullet->ss_boudaries[RIGHT]= bullet->ss_starting_boudaries[RIGHT]= ssPosX*SS_W + THA_BULLET_WIDTH;
			shift = 0;
		}

		for (k = 0; k < 4; k++)
		{
			bullet->text[(k+shift)&3][U] = uvBase[k][U];
			bullet->text[(k+shift)&3][V] = uvBase[k][V];
		}

		bullet->posDiff[X] = travelX * 2*SS_H;
		bullet->posDiff[Y] = travelY * 2*SS_H;
	}
	
	//Log_Printf("enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE]=%.2f\n",enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE]);
	//Log_Printf("(int)enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE]=%d\n",   (int)enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE]     );
	//Log_Printf("(int)enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE]+1=%d\n",((int)(enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE])+1));
	//Log_Printf("((int)(enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE])+1) & 3=%d\n",((int)(enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE])+1) & 3);
	//Log_Printf("(float)(((int)(enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE])+1) & 3)=%.2f\n",(float)(((int)(enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE])+1) & 3));
	enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE] = (int)(enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE]+1) & 3;
	//Log_Printf("POST enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE]=%.2f\n",enemy->parameters[PARAMETER_THA_LAST_BULLET_TYPE]);
	
	
	
	random[X] = (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1);
	random[Y] = enemy->parameters[PARAMETER_THA_FIRING_DIRECTION];
	normalize2(random);
	FX_GetParticule(enemy->ss_position,random,0.015*PARTICULE_SIZE_GLOBAL,PARTICULE_TRAVEL_DIST*0.3f,PARTICULE_TYPE_EXPLOSION, PARTICULE_COLOR_RED,3);
	
	
}


void THA_Living(enemy_t* enemy)
{
	//float f;
	
	//printf("[THA] simulationTime -  enemy->lastTimeFired  = %d\n",simulationTime -  enemy->lastTimeFired );
	if (enemy->timeCounter < THA_TTR+enemy->parameters[PARAMETER_THA_FIRING_TIME] && 
		 enemy->timeCounter >THA_TTR + 1000 &&
		simulationTime -  enemy->lastTimeFired > 70)
	{
		THA_FireBullet(enemy->ss_position[X]-0.05,enemy->ss_position[Y],enemy);
		THA_FireBullet(enemy->ss_position[X]+0.05,enemy->ss_position[Y],enemy);
			SND_PlaySound(SND_ENEMY_SHOT);
		enemy->lastTimeFired = simulationTime;
	}
	
	// Upright the balayeur drifts screen-right under its falling curtain
	// (2010 behavior, bit-exact). In the side view it CLIMBS the screen while
	// the curtain sweeps left -- the side-scroller reading of the same move
	// (device verdict on 197: parked on a lane, "il ne balaye pas").
	{
		float qx = 0, qy = -1;
		CAM_TTBRotateSS(&qx, &qy);
		if (qx < -0.707f || qx > 0.707f)
			enemy->ss_position[Y] += timediff * 0.0004 ;
		else
			enemy->ss_position[X] += timediff * 0.0004 ;
	}
//	enemy->ss_position[X] += timediff * enemy->parameters[THA_DELTA_Y];
	
	
	if (enemy->timeCounter > THA_TIME_ONSCREEN)
		enemy->updateFunction =  THA_Unspawning;
}


void THA_Spawning(enemy_t* enemy)
{
	float f;
	
	f = enemy->timeCounter / THA_TTR ;
	f= MIN(f,1);
	f= sinf(f*2*M_PI/4);	
	
	enemy->ss_position[X] = enemy->spawn_startPosition[X] + f*(enemy->spawn_endPosition[X] - enemy->spawn_startPosition[X]);
	enemy->ss_position[Y] = enemy->spawn_startPosition[Y] + f*(enemy->spawn_endPosition[Y] - enemy->spawn_startPosition[Y]);
		
	//As a devil is seen from above, the roll is actually a rotatiom around Y.
	enemy->entity.zAxisRot = (1-f)*enemy->spawn_Z_AxisRot;
		
		
	if (f >= 1)
	{
		enemy->entity.zAxisRot = 0 ;
		enemy->updateFunction =  THA_Living;
	}
	
}



void updateTHA(enemy_t* enemy)
{

	enemy->updateFunction =  THA_Spawning;

//	printf("enemy->parameters[PARAMETER_THA_FIRING_DIRECTION]=%.2f\n",enemy->parameters[PARAMETER_THA_FIRING_DIRECTION]);
//	printf("enemy->parameters[PARAMETER_THA_FIRING_DIRECTION-1]=%.2f\n",enemy->parameters[PARAMETER_THA_FIRING_DIRECTION]-1);
//	printf("enemy->parameters[PARAMETER_THA_FIRING_DIRECTION]-1)/2=%.2f\n",(enemy->parameters[PARAMETER_THA_FIRING_DIRECTION]-1)/2);	
//	printf("enemy->parameters[PARAMETER_THA_FIRING_DIRECTION]-1)/2* M_PI=%.2f\n",(enemy->parameters[PARAMETER_THA_FIRING_DIRECTION]-1)/2* M_PI);
	enemy->entity.yAxisRot = (1-(enemy->parameters[PARAMETER_THA_FIRING_DIRECTION]-1)/2)* M_PI;
	
	THA_Spawning(enemy);
}

