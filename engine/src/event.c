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
 *  event.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-02-11.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#include <limits.h>
#include "event.h"
#include "timer.h"
#include "math.h"
#include "lexer.h"
#include "player.h"
#include "enemy.h"
#include "dEngine.h"
#include "titles.h"
#include "text.h"
#include "menu.h"
#include "camera.h"
#include "netchannel.h"	// v4.0.6: the menu stage ends a multiplayer session
#include "rules.h"	// v4 stage 2: RULES_NoteSpawn
#include <string.h>
#include "lee.h"
#include "fht.h"
#include "shab.h"
#include "tha.h"
#include "native_services.h"
#include "enemy_particules.h"

void EV_StopPlayback(event_t* event)
{	
	engine.playback.play = 0;
}


void EV_RootEvent(event_t* event)
{

}
	
void EV_AttachToCamera(event_t* event)
{
	matrix_t viewMatrix;
	matrix_t projectionMatrix;
	matrix_t globalMatrix;
	vec3_t vLookat;
	
	
	gluPerspective(camera.fov, camera.aspect,camera.zNear, camera.zFar, projectionMatrix);
	vectorAdd(camera.position,camera.forward,vLookat);	
	gluLookAt(camera.position, vLookat, camera.up, viewMatrix);
	matrix_multiply(projectionMatrix, viewMatrix, globalMatrix);
	
	Log_Printf("[EV_AttachToCamera]\n");
	P_AttachToCamera(globalMatrix);
	ENE_AttachToCamera(globalMatrix);
	
	engine.controlVisible = 1;
}

void EV_DetachCamera(event_t* event)
{
	P_DetachToCamera();
	
	engine.controlVisible = 0;
}



void EV_SpawnEnemy(event_t* event)
{
	// enemyPayload;
	event_spawnEnemy_payload_t* eventPayload;
	enemy_t* enemy;
	
	
	
	eventPayload = (event_spawnEnemy_payload_t*)event->payload;

	//spawn a devil
	enemy = ENE_Get();
	
	
	
	ENT_LoadEntity(&enemy->entity, enemyTypePath[eventPayload->type],ENT_FULL_DRAW);

	enemy->type = eventPayload->type  ;
	enemy->timeCounter = 0;

	
	vector2Copy(eventPayload->startPosition,enemy->ss_position);
	ENE_UpdateSSBoundaries(enemy);
	
	vector2Copy(eventPayload->startPosition,enemy->spawn_startPosition);
	vector2Copy(eventPayload->endPosition,enemy->spawn_endPosition);
	vector2Copy(eventPayload->controlPoint,enemy->spawn_controlPoint);
	enemy->spawn_Z_AxisRot = eventPayload->zAxisRot;
	
	
	//Log_Printf("Angle = %.2f\n",enemy->spawn_angle);
	

	enemy->entity.xAxisRot = eventPayload->xAxisRot;
	enemy->entity.yAxisRot = eventPayload->yAxisRot;
	enemy->entity.zAxisRot = eventPayload->zAxisRot;

	
	enemy->updateFunction = enemyTypeUpdateFct[enemy->type];
	enemy->entity.mouvementPatternType = eventPayload->mouvementPatternType;
	
	enemy->energy = enemyTypeEnergy[enemy->type] ;
	enemy->score = enemyScore[enemy->type] << engine.difficultyLevel;
	enemy->shouldFlicker = 0;
	
	//Enemy are spawned outside the screen always !!
	enemy->ss_position[X] = 2;
	enemy->ss_position[Y] = 2;
	
	enemy->ss_boudaries[UP] = SS_H;
	enemy->ss_boudaries[DOWN] = SS_H;
	enemy->ss_boudaries[RIGHT] = SS_W;
	enemy->ss_boudaries[LEFT] = SS_W;
	
	matrixLoadIdentity(enemy->entity.matrix);
	enemy->entity.matrix[12]=0;
	enemy->entity.matrix[13]=110;
	enemy->entity.matrix[14]=-412;
	
	enemy->ttl = eventPayload->ttl;
	enemy->fttl = eventPayload->ttl; 
	
	memcpy(enemy->parameters,eventPayload->parameters,sizeof(enemy->parameters));
	
	switch (eventPayload->subType) {
		case ENEMY_SUBTYPE_NORMAL:
			enemy->entity.color[R] = 1;
			enemy->entity.color[G] = 1;
			enemy->entity.color[B] = 1;
			enemy->entity.color[A] = 1;	
			break;

		case ENEMY_SUBTYPE_HAAARD:
			enemy->entity.color[R] = 0.85f;
			enemy->entity.color[G] = 0.85f;
			enemy->entity.color[B] = 1.0f;
			enemy->entity.color[A] = 1;	
			enemy->energy *= 5;
			break;

		case ENEMY_SUBTYPE_IMPOSSIBLE:
			enemy->entity.color[R] = 0.2f;
			enemy->entity.color[G] = 0.2f;
			enemy->entity.color[B] = 0.2f;
			enemy->entity.color[A] = 1;	
			enemy->energy *= 40;
			break;
		case ENEMY_SUBTYPE_WEAK:
			enemy->entity.color[R] = 1;
			enemy->entity.color[G] = 1;
			enemy->entity.color[B] = 1;
			enemy->entity.color[A] = 1;
			enemy->energy = 1;

			break;
		default:
			break;
	}

	// THE DEVIL'S THREE COSTUMES (act III). For ENEMY_HAB -- Fabien's hidden
	// enemy, first spawned in 2026 -- the subType picks the SKIN, not the
	// difficulty: 0 = the resurrected original silver, 1 = the anthracite
	// stealth (the texture darkens under GL_MODULATE, red stripes survive as
	// embers), 2 = the translucent ghost (alpha < 1: the enemy pass blends it).
	// The costume also picks the weapon: updateHAB reads it back from the
	// scratch parameters. Energy is a flat elite pool, not the type's base
	// (10) nor the generic subType multipliers above -- a Devil stays on
	// screen ~4s and must survive focused fire (the MP doubling comes after).
	if (eventPayload->type == ENEMY_HAB)
	{
		enemy->energy = 55;	// 80 outlived the fun on device ("un peu trop de vie")
		enemy->parameters[PARAMETER_HAB_COSTUME] = (float)eventPayload->subType;
		// runtime proof for the smoke log: every Devil spawn, timestamped
		// (device report of a missing V5 ghost -- the parse was clean, so
		// the truth has to come from the running engine)
		if (Log_ProbesEnabled())
			Log_Printf("[devil] t=%d costume=%d at %.2f,%.2f\n",
				simulationTime, eventPayload->subType,
				enemy->ss_position[X], enemy->ss_position[Y]);
		switch (eventPayload->subType) {
			case 1:		// anthracite
				enemy->entity.color[R] = 0.30f;
				enemy->entity.color[G] = 0.32f;
				enemy->entity.color[B] = 0.42f;
				enemy->entity.color[A] = 1;
				break;
			case 2:		// ghost -- 0.30 was unreadable on device;
						// updateHAB shimmers alpha 0.42..0.75 from here
				enemy->entity.color[R] = 0.85f;
				enemy->entity.color[G] = 0.95f;
				enemy->entity.color[B] = 1.0f;
				enemy->entity.color[A] = 0.42f;
				break;
			default:	// the original, as decoded from the 2009 .pvr
				enemy->entity.color[R] = 1;
				enemy->entity.color[G] = 1;
				enemy->entity.color[B] = 1;
				enemy->entity.color[A] = 1;
				break;
		}
	}

	// In multiplayer N ships fire (~N times the DPS), so enemies felt too easy
	// with solo HP. Scale their energy by the ship count to keep the challenge
	// comparable (v2 P3: was a flat x2 for the 2-player mode -- identical at 2).
	// Applied identically on every device (same events, same mode/numPlayers)
	// so it stays deterministic. One-shot WEAK enemies (energy 1) are left alone.
	if (engine.mode == DE_MODE_MULTIPLAYER && enemy->energy > 1 && numPlayers >= 2)
		enemy->energy *= numPlayers;

	// v4 stage 2: the enemy joins its group and the rules take note (energy
	// budget for hpBelow, "seen" for cleared).
	strncpy(enemy->group, eventPayload->group, sizeof(enemy->group) - 1);
	enemy->group[sizeof(enemy->group) - 1] = 0;
	RULES_NoteSpawn(enemy);
	if (Log_ProbesEnabled() && enemy->group[0])
		Log_Printf("[spawn] t=%d type=%d sub=%d group=%s energy=%d ttl=%d\n",
			simulationTime, enemy->type, eventPayload->subType, enemy->group, enemy->energy, enemy->ttl);
	

	
}

void EV_SpawnText(event_t* event)
{
	event_text_payload_t* payload;
	
	
	payload = (event_text_payload_t*)event->payload;
	
	DYN_TEXT_AddText(payload->ss_start_pos, payload->ss_end_pos, payload->duration,payload->size, payload->text);
	
}

void EV_DisplayStats(event_t* event)
{
	
}

void EV_ShowProlog(event_t* event)
{
	//Log_Printf("EV_ShowProlog()\n");
	event_title_payload_t* pl;
	pl = event->payload;
	TITLE_Show_prolog(pl->duration);
}

void EV_ShowEpilog(event_t* event)
{
	event_title_payload_t* pl;
	pl = event->payload;
	// outro diagnostics (205: the act never reached scene 4 in the smoke)
	if (Log_ProbesEnabled())
		Log_Printf("[title] epilog event fired t=%d dur=%d\n", simulationTime, pl->duration);
	TITLE_Show_epilog(pl->duration);
}

void EV_MaskStats(event_t* event)
{
	
}

void EV_RequestScene(event_t* event)
{
	
	event_req_scene_t* payload;
	
	payload = event->payload;
	
	
	
	dEngine_RequireSceneId(payload->sceneId);

	// v4.0.6 -- leaving for the MENU STAGE ends a multiplayer session. GAME OVER
	// used to drop both devices back to the menu with the match still live:
	// NET_RUNNING, mode still MULTIPLAYER, numPlayers still 2. The peers kept
	// streaming at each other behind the menu, and the next thing that started a
	// scene started it as a two-player game with nobody having asked -- the
	// tester's "la partie a redemarre toute seule en partie a 2 alors que nous
	// etions dans les menus". The end-of-GAME path (dEngine_GoToNextScene) has
	// always torn down here; game over never did. Both sims reach this on the
	// same simulation tick, so each side tears down deterministically -- and it
	// runs from EV_Update, never from inside NET_Receive's drain loop.
	if (SCENE_KIND(payload->sceneId) == SCENE_KIND_INTRO && engine.mode == DE_MODE_MULTIPLAYER)
	{
		Log_Printf("[EV_RequestScene] back to the menu stage: ending the multiplayer session.\n");
		NET_Free();
		numPlayers = 1;
		controlledPlayer = 0;
		engine.mode = DE_MODE_SINGLEPLAYER;
	}
	
	Log_Printf("[EV_RequestScene] engine.requiredSceneId =%d.\n",engine.requiredSceneId );
}

void EV_RequestMenu(event_t* event)
{
	
	event_req_menu_t* payload;
	
	payload = event->payload;
	
	MENU_Set(payload->menuId);
    
   
}

void EV_AutoPilotPls(event_t* event)
{
	int i;
	
	
	for (i=0; i < numPlayers; i++) 
	{
		players[i].autopilot.enabled = 1;
		players[i].autopilot.timeCounter  = PLAYER_ENDLEVEL_REPLACMENT;
		players[i].autopilot.originalTime = PLAYER_ENDLEVEL_REPLACMENT;
		players[i].autopilot.holdAtEnd    = 1;	// 4.0.2: and stay there under the card (every act, like the boss act)
		// v2 P3: the end-of-level rest formation. 0.6*P_FormationX(i) is
		// bit-exact with the 2010 (i-0.5)*2*0.3 for seats 0/1, and pulls
		// seats 2/3 into the inner staggered pair instead of off-screen.
		players[i].autopilot.end_ss_position[X] = 0.6f * P_FormationX(i);

		players[i].autopilot.end_ss_position[Y] = -0.3f + ((i < 2) ? 0.0f : -0.2f);
		//Log_Printf("player %d end_ss_position[%.2f,%.2f]\n",players[i].autopilot.end_ss_position[X],players[i].autopilot.end_ss_position[Y]);
		players[i].autopilot.diff_ss_position[X] = players[i].ss_position[X] - players[i].autopilot.end_ss_position[X];
		players[i].autopilot.diff_ss_position[Y] = players[i].ss_position[Y] - players[i].autopilot.end_ss_position[Y];
		
	}
	
}

void EV_SaveScore(event_t* event)
{
	Native_UploadScore(P_GetDisplayScore());	// v2 P3: team score in MP
}


void EV_LimitedEdition_Action(event_t* event)
{
	enemy_t* enemy;
	event_t* toDelete;
	vec2short_t ss_start_pos;
	vec2short_t ss_end_pos;
	event_req_scene_t* payloadScene;
	event_req_menu_t* payloadMenu;
	
	
	
	
	// Destroy all enemies
	enemy = ENE_GetFirstEnemy();
	while (enemy) 
	{
		// spawn an explosion
		FX_GetExplosion(enemy->ss_position,IMPACT_TYPE_YELLOW,1,0);
		//enemy->ss_position[X] += (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1) * 0.1 ;
		//enemy->ss_position[Y] += (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1) * 0.1 ;
		//FX_GetExplosion(enemy);
		
		
		
		Spawn_EntityParticules(enemy->ss_position);
		// spwan smoke
		
		FX_GetSmoke(enemy->ss_position, 0.3, 0.3);
		enemy->ss_position[X] += (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1) * 0.1 ;
		enemy->ss_position[Y] += (rand() - (RAND_MAX >> 1)) / (float)(RAND_MAX >> 1) * 0.1 ;
		FX_GetSmoke(enemy->ss_position, 0.2, 0.2);
		
		ENE_Release(enemy);
		
		SND_PlaySound(SND_EXPLOSION);
		
		enemy= enemy->next;
	}
	
	
	// Remove all bullets
	partLib.numParticules = 0;
	
	// Autopilot & Move to position
	EV_AutoPilotPls(event);
	
	 
	 
	/// TEXT VERIFIED GOOD
	// Display text
	ss_start_pos[X] = ss_end_pos[X] = 0 ;
	ss_end_pos[Y] = ss_start_pos[Y] = 150;
	DYN_TEXT_AddText(ss_start_pos, ss_end_pos, 10000,2.5f,"Thanks for trying:");

	ss_start_pos[X] = ss_end_pos[X] = 0 ;
	ss_end_pos[Y] = ss_start_pos[Y] = 80;
	DYN_TEXT_AddText(ss_start_pos, ss_end_pos, 10000,2.5f,"\"Shmup Lite\"");
	
	
	ss_start_pos[X] = ss_end_pos[X] = 0 ;
	ss_end_pos[Y] = ss_start_pos[Y] = -10;
	DYN_TEXT_AddText(ss_start_pos, ss_end_pos, 10000,2.5f,"Check out the full version.");
	
	
	
	
	//Add a return to main menu even set at simulationTime+10000
	payloadScene = calloc(1, sizeof(event_req_scene_t));
	payloadScene->sceneId = 0;
	event = calloc(1, sizeof(event_t));
	event->time = simulationTime+10000;
	event->type = EV_REQUEST_SCENE;
	event->payload = payloadScene;
	EV_AddEvent(event);
	
	payloadMenu = calloc(1, sizeof(event_req_menu_t));
	payloadMenu->menuId = MENU_HOME;
	event = calloc(1, sizeof(event_t));
	event->time = simulationTime+10000;
	event->type = EV_REQUEST_MENU;
	event->payload = payloadMenu;
	EV_AddEvent(event);	
	
	
	
	//Remove all futur enemy spawning events.
	event = EV_GetNextEvent();
	
	while (event != NULL) 
	{			
		while (event->next != NULL && event->next->type == EV_SPAWN_ENEMY)
		{
			//Log_Printf("[dEngine_JumpInTime] Cleaning EV_SPAWN_ENEMY events t=%d.\n",event->next->time);
			
			toDelete = event->next;
			event->next = event->next->next;
			
			free(toDelete->payload);
			free(toDelete);
		}
		
		
		event = event->next;
	}
	 
}

void EV_ClearTitle(event_t* event){
    TITLE_Clear();
}

// TTB beat: hand the scripted roll over to the camera (see camera.h).
void EV_TTBRoll(event_t* event)
{
	event_ttb_payload_t* payload = (event_ttb_payload_t*)event->payload;

	if (payload == NULL)
		return;

	CAM_SetTTBRoll(payload->angleDegrees, payload->duration);
}

typedef void (*eventProcessor_ft)(event_t*) ;


event_t events;
event_t* nextEvent = NULL;

eventProcessor_ft eventToFunction[32] = 
{
	EV_RootEvent,
	EV_AttachToCamera,
	EV_DetachCamera,
	EV_SpawnEnemy,
	EV_DisplayStats,
	EV_MaskStats,
	EV_ShowProlog,
	EV_ShowEpilog,
	EV_RequestScene,
	EV_SpawnText,
	EV_StopPlayback,
	EV_RequestMenu,
	EV_AutoPilotPls,
	EV_SaveScore,
	EV_LimitedEdition_Action,
    EV_ClearTitle,
	EV_TTBRoll
};



void EV_InitForScene(void)
{
	events.time = 0;
	events.next = NULL;
	events.type = EV_ROOT;
	events.payload = NULL;
	
	nextEvent = &events;
	
	Log_Printf("EV_InitForScene\n");
}

event_t*  EV_GetNextEvent(void)
{
	return nextEvent;
}
void EV_AddEvent(event_t* event)
{
	event_t* cEvent;
	event_t* tmp;

	// v4 stage 2: a rule may fire after the timeline is exhausted (nextEvent
	// NULL); the event becomes the new head instead of dereferencing NULL.
	if (nextEvent == NULL)
	{
		event->next = NULL;
		nextEvent = event;
		return;
	}

	// ... and an event EARLIER than the head goes in front of it. The 2009
	// insertion only ever looked past the head, which was fine for a timeline
	// read in order; a rule firing mid-level while the camera's detach waits
	// at 140 s had its spawns filed BEHIND that detach -- never to fire (the
	// bench: "fire w2" printed, no w2 ship ever spawned).
	if (event->time < nextEvent->time)
	{
		event->next = nextEvent;
		nextEvent = event;
		return;
	}

	cEvent = nextEvent;
	
	//Search
	while (cEvent->next != NULL && cEvent->next->time <= event->time) {
		cEvent = cEvent->next;
	}
	
	
	// Insert
	if (cEvent->next == NULL)
		cEvent->next = event;
	else 
	{
		tmp = cEvent->next;
		cEvent->next = event;
		event->next = tmp;
	}

}

// v4 stage 2: spawns of that group still waiting in the timeline (or scheduled
// by a rule). "cleared" must wait for them: the first ship of a wave can die
// the tick it spawns, before its sisters exist -- the bench caught exactly
// that (w2 fired 50 ms into w1).
int EV_PendingSpawnsInGroup(const char* group)
{
	event_t* e;
	int n = 0;
	if (!group || !group[0])
		return 0;
	for (e = nextEvent; e != NULL; e = e->next)
	{
		if (e->type == EV_SPAWN_ENEMY && e->payload)
		{
			event_spawnEnemy_payload_t* p = (event_spawnEnemy_payload_t*)e->payload;
			if (!strcmp(p->group, group))
				n++;
		}
	}
	return n;
}

void EV_Update(void)
{
	event_t* toDelete;
	
	//if (nextEvent != NULL)
	//	Log_Printf("next event t=%d.\n",nextEvent->time);
	
	while (nextEvent != NULL && nextEvent->time < simulationTime) 
	{
		//Log_Printf("Triggering event t=%d type: %d.\n",nextEvent->time,nextEvent->type);
		eventToFunction[nextEvent->type](nextEvent);
		
		toDelete = nextEvent;
		
		nextEvent = nextEvent->next;
		
		
		
		if (toDelete != &events)
		{
		   free(toDelete->payload);
		   free(toDelete);
		}
		 
		//? Freeing nextevent ?
	}
}

void EV_CleanAllRemainingEvents(void)
{
	event_t* toDelete;
	
	while (nextEvent != NULL) 
	{
		toDelete = nextEvent;
		nextEvent = nextEvent->next;
		
		if (toDelete != &events)
		{
			free(toDelete->payload);
			free(toDelete);
		}
	}
	
	
}

// v4 stage 2: the two spawn grammars as functions, so the rules block speaks
// the same language as the enemies block. The lexer sits just after
// "spawnEnemyWave circle": read the wave and fill up to maxOut payloads.
//   circle enemyNum <n> enemyType <t> percentageInvulnerable <p> angleOffset <deg> subType <s>
int EV_ParseCircleWave(event_spawnEnemy_payload_t* out, int maxOut, float ttl)
{
	int   numEnemies, enemyType, i, n = 0;
	float percentageInvulnerable, angleoffset;
	uchar defaultSubType;

	//enemyNum
	LE_readToken();
	numEnemies = LE_readReal();

	//enemyType
	LE_readToken();
	enemyType = LE_readReal();

	//percentageInvulnerable
	LE_readToken();
	percentageInvulnerable = LE_readReal()/100.0f;

	LE_readToken();
	angleoffset  = LE_readReal() * 2*M_PI/360 ;

	LE_readToken();
	defaultSubType = LE_readReal();

	for (i = 0; i < numEnemies && n < maxOut; i++, n++)
	{
		event_spawnEnemy_payload_t* eventPayload = &out[n];
		memset(eventPayload, 0, sizeof(*eventPayload));
		eventPayload->type = enemyType;
		eventPayload->xAxisRot = 0;
		eventPayload->yAxisRot = 0;
		eventPayload->zAxisRot = 0;
		//This is ugly
		eventPayload->startPosition[X] = (angleoffset+2*M_PI)/numEnemies*i;
		eventPayload->mouvementPatternType = MVMT_CIRCLE;
		eventPayload->ttl = ttl;
		eventPayload->subType = (i/(float)numEnemies < percentageInvulnerable)? ENEMY_SUBTYPE_IMPOSSIBLE : defaultSubType ;
	}
	return n;
}

// The lexer sits just after "spawnEnemy": mouvement, its parameters, enemyType,
// startPos, endPos, controlPoint, the three rotations, subType, and the
// per-type parameters. Verbatim the 2009 reading order.
void EV_ParseSpawnParams(event_spawnEnemy_payload_t* eventPayload)
{
				//mouvement
				LE_readToken();
				eventPayload->mouvementPatternType = LE_readReal();
				
				
				 
				 
				 
				switch (eventPayload->mouvementPatternType) {
					case MVMT_X_SIN:
						LE_readToken();
						eventPayload->parameters[PARAMETER_FHT_X_POS] = LE_readReal();
						
						LE_readToken();
						eventPayload->parameters[PARAMETER_FHT_X_WIDTH] = LE_readReal();
						break;
						
					case MVMT_CIRCLE:
						//startAngle 0    fireFrequency 100
						LE_readToken();
						eventPayload->parameters[PARAMETER_LEE_START_ANGLE] = 2*M_PI/360 * LE_readReal();
					//	Log_Printf("eventPayload->parameters[PARAMETER_LEE_START_ANGLE]=%.2f\n",eventPayload->parameters[PARAMETER_LEE_START_ANGLE]);
						LE_readToken();
						eventPayload->parameters[PARAMETER_LEE_FIRE_FREQUENCY] = LE_readReal();
						
						
						
					default:
						break;
				}
				
				
				//enemyType
				LE_readToken();
				eventPayload->type = LE_readReal();
				
				
				//startPos
				LE_readToken(); 
				eventPayload->startPosition[X] = LE_readReal();
				eventPayload->startPosition[Y] = LE_readReal();

				//endPos
				LE_readToken();
				eventPayload->endPosition[X] = LE_readReal();
				eventPayload->endPosition[Y] = LE_readReal();

				//controlPoint
				LE_readToken();
				eventPayload->controlPoint[X] = LE_readReal();
				eventPayload->controlPoint[Y] = LE_readReal();
				
				//Initial roll
				LE_readToken();
				eventPayload->zAxisRot = 2*M_PI/360 *  LE_readReal();

				//Initial pitch
				LE_readToken();
				eventPayload->xAxisRot = 2*M_PI/360 *  LE_readReal();

				
				//Initial yaw
				LE_readToken();
				eventPayload->yAxisRot = 2*M_PI/360 *  LE_readReal();
				
				//subType
				LE_readToken();
				eventPayload->subType = LE_readReal();
				
				
				
				switch (eventPayload->type) {
					case ENEMY_LEE:
						eventPayload->parameters[PARAMETER_LEE_FIRING_TYPE] = LE_readReal();
						
						LE_readToken();						
						eventPayload->parameters[PARAMETER_LEE_BULLET_SPEED_FACTOR]= LE_readReal();												
						break;
						
					case ENEMY_SHAB:
						LE_readToken();
						eventPayload->parameters[PARAMETER_SHAB_FIRING_ANGLE1]    = 2*M_PI/360 *LE_readReal();
						LE_readToken();
						eventPayload->parameters[PARAMETER_SHAB_FIRING_ANGLE2]    = 2*M_PI/360 *LE_readReal();
						LE_readToken();
						eventPayload->parameters[PARAMETER_SHAB_FIRING_NUM_THREAD]= LE_readReal();						
						LE_readToken();
						eventPayload->parameters[PARAMETER_SHAB_FIRING_ROT_ANGLE] = 2*M_PI/360 *LE_readReal();												
						break;
						
					case ENEMY_THA :
						//fireDirection
						LE_readToken();
						eventPayload->parameters[PARAMETER_THA_FIRING_DIRECTION] = LE_readReal();
						
						//firingTime
						LE_readToken();
						eventPayload->parameters[PARAMETER_THA_FIRING_TIME] = LE_readReal();
						
					default:
						break;
				}
}

void EV_ReadEnemiesEvents(void)
{
	event_t*                     event;
	event_spawnEnemy_payload_t*  eventPayload;
	int                          at = 0;
	int                          i;
	int                          time=0;
	float                        ttl = 0;
	// v4 stage 2: a circle wave is parsed into this scratch, then turned into events;
	// "group <name>" after a spawn line tags what that line created.
	event_spawnEnemy_payload_t   waveTmp[64];
	int                          numWave;
	event_spawnEnemy_payload_t*  lastPayloads[64];
	int                          numLast = 0;
	
	nextEvent = &events;

	LE_readToken() ; //{

	LE_readToken(); 	//at or }
	while (LE_hasMoreData() && strcmp(LE_getCurrentToken(), "}")) 
	{
		if (!strcmp("settime", LE_getCurrentToken()))
		{
			time = LE_readReal();
			//Log_Printf("settime=%d.\n",time);
		}
		else
		if (!strcmp("addtime", LE_getCurrentToken())) 
		{
			time += LE_readReal();
		}
		if (!strcmp("setttl", LE_getCurrentToken()))
		{
			ttl = LE_readReal();
			//Log_Printf("ttl=%.2f.\n",ttl);
		}		
		else if (!strcmp("at", LE_getCurrentToken()))
		{
			at = time + LE_readReal();
		
			//Log_Printf("Fount enemy at %d.\n",at);
		
			LE_readToken();
		

			//at  50000 spawnEnemyWave circle enemyNum 16 enemyType 1
			if (!strcmp("spawnEnemyWave", LE_getCurrentToken()))
			{
				LE_readToken();
			
				if (!strcmp("circle", LE_getCurrentToken()))
				{
					// v4 stage 2: the wave grammar lives in EV_ParseCircleWave (shared with the rules)
					numWave = EV_ParseCircleWave(waveTmp, 64, ttl);
					numLast = 0;
					for (i = 0; i < numWave; i++)
					{
						event = calloc(1, sizeof(event_t));
						event->time = at;
						event->type = EV_SPAWN_ENEMY;
						eventPayload = calloc(1, sizeof(event_spawnEnemy_payload_t));
						memcpy(eventPayload, &waveTmp[i], sizeof(event_spawnEnemy_payload_t));
						event->payload = eventPayload;
						if (numLast < 64)
							lastPayloads[numLast++] = eventPayload;
						EV_AddEvent(event);
					}
				}
			}	
			else
			//at 0 spawnEnemy enemyType 3 startPos -1 -1 endPos -0.5 0.5 controlPoint -1 1 initialRoll 90
			if (!strcmp("spawnEnemy", LE_getCurrentToken()))
			{
				event = calloc(1, sizeof(event_t));
				event->time = at;
				event->type = EV_SPAWN_ENEMY;
				eventPayload = calloc(1, sizeof(event_spawnEnemy_payload_t));
				event->payload = eventPayload;
				
				eventPayload->ttl =  ttl;
				
				// v4 stage 2: the spawn grammar lives in EV_ParseSpawnParams (shared with the rules)
				EV_ParseSpawnParams(eventPayload);
				numLast = 0;
				lastPayloads[numLast++] = eventPayload;
				
				
				
				
				
				EV_AddEvent(event);
			}
			//Log_Printf("t=%d enemyType=%d\n",event->time,eventPayload->type);
		}
		else if (!strcmp("group", LE_getCurrentToken()))
		{
			// v4 stage 2: "group <name>" after a spawn line tags what that line spawned
			LE_readToken();
			for (i = 0; i < numLast; i++)
			{
				strncpy(lastPayloads[i]->group, LE_getCurrentToken(), sizeof(lastPayloads[i]->group) - 1);
				lastPayloads[i]->group[sizeof(lastPayloads[i]->group) - 1] = 0;
			}
		}

		LE_readToken(); 
	}
	

}


	
void EV_ReadTextsEvents(void)
{
	event_t* event;
	event_text_payload_t* payload;
	int currentTime=0;
	
	LE_readToken() ; //{
	
	LE_readToken(); 	//at or }
	while (LE_hasMoreData() && strcmp(LE_getCurrentToken(), "}")) 
	{
		if (!strcmp("at", LE_getCurrentToken()))
		{
			event = (event_t*)calloc(1, sizeof(event_t));
			event->time = currentTime + LE_readReal();
			event->type = EV_SPAWN_TEXT;
			payload = (event_text_payload_t*)calloc(1, sizeof(event_text_payload_t));
			event->payload = payload;
			
			//at 0000 display -Welcome_To_"Shump"_tutorial-	 size 2	for 2000 starting 0   0 ending  0 0
			LE_readToken(); //display
			LE_readToken();
			strReplace(LE_getCurrentToken(),'_',' ');
			strcpy(payload->text,LE_getCurrentToken());
			
			
			LE_readToken();	// size
			payload->size = LE_readReal();
			
			LE_readToken();	// for
			payload->duration = LE_readReal();
			
			LE_readToken(); // starting
			payload->ss_start_pos[X] = LE_readReal() ;
			payload->ss_start_pos[Y] = LE_readReal() ;
			
			LE_readToken(); // direction
			payload->ss_end_pos[X] = LE_readReal() ;
			payload->ss_end_pos[Y] = LE_readReal() ;
			
			
			
			
			//Acquired event
		//	Log_Printf("[EV_ReadTextsEvents] at %d: %s\n",event->time,payload->text);
			EV_AddEvent(event);
		}
		else
		if (!strcmp("settime",LE_getCurrentToken())){
			currentTime = LE_readReal();
		}

		LE_readToken(); 
	}
}
