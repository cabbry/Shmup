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
 *  commands.c
 *  dEngine
 *
 *  Created by fabien sanglard on 09-10-09.
 *  Copyright 2009 Memset software Inc. All rights reserved.
 *
 */

#include "commands.h"
#include "player.h"
#include "camera.h"
#include "limits.h"
#include "timer.h"
#include "filesystem.h"
#include "dEngine.h"
#include "titles.h"

command_buffer_t commandsBuffers[MAX_NUM_PLAYERS];

command_t* currentRecordCommand;
command_t* lastRecordCommand;

#define NUM_RECORD_FRAMES (240*60*2)
command_t recordCommands[NUM_RECORD_FRAMES];

command_t toSend;

touch_t touches[NUM_BUTTONS];











// ---------------------------------------------------------------------------
// v4.1.2 -- HOW HARD THE PERIODIC RESYNC PULLS.
//
// Each device streams its own ship's ABSOLUTE position every 300 ms so the
// peers can correct the drift the delta stream leaves behind. The correction
// used to close a flat TENTH of the error, which is far too gentle: the error
// decays by 0.9 every 300 ms, so a ship a fifth of the screen out of place
// needs six seconds to come back -- and if drift builds faster than that, it
// never does. It just sits there, wrong, which is what the tester saw on the
// second iPhone.
//
// The pull now depends on how wrong the position is, which is the thing the
// flat tenth ignored:
//   - a hair out         leave it alone; correcting jitter only makes jitter
//   - plainly out        close a third of it, so it is back within a second
//   - somewhere else     put it where it belongs, and take the jump
//
// Positions are screen-normalised: X and Y run -1..1 across the view, so the
// dead zone is about a hundredth of the screen and the snap about an eighth.
// This only ever moves a REMOTE ship, whose position is network-driven and
// never simulated -- each device tests collisions against its OWN hull only,
// so pulling harder here cannot change what happens to anyone.
/* --- RESYNC-ARITHMETIC-BEGIN --- */
#define RESYNC_DEADZONE	0.02f	/* under this, the ship is where it should be */
#define RESYNC_SNAP		0.25f	/* over this, it is not drift: it is wrong */
#define RESYNC_GAIN		0.35f	/* how much of the rest to close per correction */

static float COM_ResyncStep(float current, float target)
{
	float err = target - current;
	float mag = (err < 0) ? -err : err;

	if (mag <= RESYNC_DEADZONE)
		return current;
	if (mag >= RESYNC_SNAP)
		return target;
	return current + err * RESYNC_GAIN;
}
/* --- RESYNC-ARITHMETIC-END --- */
void COM_Init(void)
{
	int i;

	memset(touches, 0, sizeof(touches));

	//Need to invalidate bufferCommand numCmd=0 ?
	for(i = 0 ; i < numPlayers ; i++)
		commandsBuffers[i].numCommands = 0;
	
	memset(&toSend,0, sizeof(command_t));
	
	
	COM_ResetTouchesBuffer();
}



void COM_ConvertLocalTouchsToCommands(void)
{
	//Convert current touch state into a command that goes in commandsBuffers[controllerPlayer]
	command_t* command ;
	
	if (title_mode == MODE_EPILOG)
	{
		return;
	}
	
	command = &commandsBuffers[controlledPlayer].cmds[commandsBuffers[controlledPlayer].numCommands++];
	memset(command, 0, sizeof(command_t));
	
	command->type = NET_RTM_COMMAND;
	command->playerId = controlledPlayer;
	command->time = simulationTime ;
	
	if (touches[BUTTON_MOVE].down && !players[controlledPlayer].autopilot.enabled)
	{
		// 1:1 finger tracking. dist already equals the finger delta in pixels
		// * 80 / viewportDim; dividing by 40 turns that into the matching
		// ss_position delta (ss spans [-1,1] = 2 across the viewport), so the
		// ship moves exactly as far as the finger, independent of screen size,
		// FOV and frame-rate.
		command->delta[X] = touches[BUTTON_MOVE].dist[X] / 40.0f;
		command->delta[Y] = touches[BUTTON_MOVE].dist[Y] / 40.0f;

		// (TTB is now a one-shot horizontal orbit that returns to normal, so the
		// controls need no axis inversion.)
	}
	
	
	if (touches[BUTTON_FIRE].down)
	{
		command->buttons |= BUTTON_FIRE_PRESSED;
	}
	
	if (touches[BUTTON_GHOST].down)
	{
		command->buttons |= BUTTON_GHOST_PRESSED; 
	}
	
	//if(command->time != simulationTime)
	//	commandsBuffers[controlledPlayer].numCommands = 0;
	//Need to copy cmd to send to netchannel.c
	memcpy(&toSend, command, sizeof(command_t));
	
	// A swipe is a delta, consumed once.
	touches[BUTTON_MOVE].down = 0;
	touches[BUTTON_MOVE].dist[X] = 0;
	touches[BUTTON_MOVE].dist[Y] = 0;

	touches[BUTTON_GHOST].down = 0;
}


#define FINGER_SPRITE_U_ORIGIN ((64/256.0f)*SHRT_MAX)
#define FINGER_SPRITE_V_ORIGIN ((64/128.0f)*SHRT_MAX)
#define FINGER_SPRITE_WIDTH ((64/256.0f)*SHRT_MAX)
#define FINGER_SPRITE_HEIGHT ((64/128.0f)*SHRT_MAX)


vec2_t showFingerStartPos;
#define FINGER_SPRITE_SIZE 64
void COM_PrepareFingerSprites(command_t* command)
{

	xf_colorless_sprite_t* spriteVertice;
	
	spriteVertice = &diverSpriteLib.vertices[diverSpriteLib.numVertices];

	if ((command->buttons & BUTTON_FIRE_PRESSED) == 0 &&
		(command->buttons & BUTTON_GHOST_PRESSED) == 0 &&
		 command->delta[X] == 0 &&
		 command->delta[Y] == 0)
	{
		showFingerStartPos[X] =  0.5;
		showFingerStartPos[Y] = -0.5;
		return;
	}
	showFingerStartPos[X] += command->delta[X]/2;///10.0f*320;
	showFingerStartPos[Y] += command->delta[Y]/2;///10.0f*480;
	/*
	 0 3
	 1 2
	 */
	spriteVertice->pos[X] = showFingerStartPos[X] * SS_W -FINGER_SPRITE_SIZE ;
	spriteVertice->pos[Y] = showFingerStartPos[Y] * SS_H +FINGER_SPRITE_SIZE ;
	spriteVertice->text[U] = FINGER_SPRITE_U_ORIGIN;
	spriteVertice->text[V] = FINGER_SPRITE_V_ORIGIN;
	spriteVertice++;
	
	spriteVertice->pos[X] = showFingerStartPos[X] * SS_W -FINGER_SPRITE_SIZE ;
	spriteVertice->pos[Y] = showFingerStartPos[Y] * SS_H -FINGER_SPRITE_SIZE ;
	spriteVertice->text[U] = FINGER_SPRITE_U_ORIGIN;
	spriteVertice->text[V] = FINGER_SPRITE_V_ORIGIN + FINGER_SPRITE_HEIGHT;
	spriteVertice++;
	
	
	spriteVertice->pos[X] = showFingerStartPos[X] * SS_W +FINGER_SPRITE_SIZE ;
	spriteVertice->pos[Y] = showFingerStartPos[Y] * SS_H -FINGER_SPRITE_SIZE ;
	spriteVertice->text[U] = FINGER_SPRITE_U_ORIGIN +FINGER_SPRITE_WIDTH ;
	spriteVertice->text[V] = FINGER_SPRITE_V_ORIGIN +FINGER_SPRITE_HEIGHT ;
	spriteVertice++;
	
	
	spriteVertice->pos[X] = showFingerStartPos[X] * SS_W +FINGER_SPRITE_SIZE ;
	spriteVertice->pos[Y] = showFingerStartPos[Y] * SS_H +FINGER_SPRITE_SIZE ;
	spriteVertice->text[U] = FINGER_SPRITE_U_ORIGIN+FINGER_SPRITE_WIDTH;
	spriteVertice->text[V] = FINGER_SPRITE_V_ORIGIN;
	
	diverSpriteLib.numVertices+=4;
	diverSpriteLib.numIndices+=6;
}

void COM_ExecCommand(command_t* command)
{
	player_t* player;
	uchar	pId = command->playerId;
	player = &players[pId];
	
	
	
	switch (command->type) {
		case NET_RTM_COMMAND :
			
			if (players[pId].respawnCounter < 0 )
				return;
			
			//Log_Printf("engine.showFingers=%d\n",engine.showFingers);
			
			if (engine.showFingers)
				COM_PrepareFingerSprites(command);
			
			//Check the commands
			if ((command->buttons & BUTTON_FIRE_PRESSED) == BUTTON_FIRE_PRESSED)
			{
				
				P_FireTwoBullet(player);
			}
			
			//Check the commands
			if ((command->buttons & BUTTON_GHOST_PRESSED) == BUTTON_GHOST_PRESSED) 
			{
				
				P_FireGhosts(player);
			}
			
			if (player->autopilot.enabled)
				return;
			
			
			
			player->ss_position[X] += command->delta[X];
			player->ss_position[Y] += command->delta[Y];
			
			if (player->ss_position[X] < -1 ) player->ss_position[X] = -1;
			if (player->ss_position[X] > 1 ) player->ss_position[X] = 1;
			
			if (player->ss_position[Y] < -1 ) player->ss_position[Y] = -1;
			if (player->ss_position[Y] > 1 ) player->ss_position[Y] = 1;
			P_UpdateSSBoundaries(pId);
			
		
			
			break;
			
		case NET_RTM_ABS_UPDATE :
			
			
			if (player->autopilot.enabled)
				return;
			
			player->ss_position[X] = COM_ResyncStep(player->ss_position[X], command->delta[X]);
			player->ss_position[Y] = COM_ResyncStep(player->ss_position[Y], command->delta[Y]);
			P_UpdateSSBoundaries(pId);
			
			break;
		
		case NET_RTM_DIED :
			P_Die(command->playerId);
			break;

		default:
			break;
	}
	
	
}

void COM_Update(void)
{
	int i;
	int numCmds;
	command_t* command;
	
	if (!entitiesAttachedToCamera)
		return;
	
	COM_ConvertLocalTouchsToCommands();
	
	//If playing we need to play the commands in the buffer
	if (engine.playback.play)
	{
		//printf("Next command t=%u.\n",currentRecordCommand->time);
		while (lastRecordCommand > currentRecordCommand && currentRecordCommand->time <= simulationTime) 
		{
			//TO REMOVE This is only here to record a multiplayer game
			memcpy(&toSend, currentRecordCommand, sizeof(command_t));
			//TO REMOVE
			COM_ExecCommand(currentRecordCommand);
			currentRecordCommand++;
		}
	}
	else 
	{
		for (i=0; i < numPlayers; i++) 
		{
			for (numCmds=0; numCmds < commandsBuffers[i].numCommands; numCmds++) 
			{
				command = &commandsBuffers[i].cmds[numCmds];
				if (command->time <= simulationTime )
					COM_ExecCommand(command);
			}
			
			
		}
	}

	//Need to invalidate bufferCommand numCmd=0
	//Empty command buffer
	for(i = 0 ; i < numPlayers ; i++)
	{
		
		commandsBuffers[i].numCommands = 0;
	}
}


void COM_ClearBuffers(void)
{
	int i;
	
	//Need to invalidate bufferCommand numCmd=0
	//Empty command buffer
	for(i = 0 ; i < numPlayers ; i++)
	{
		
		commandsBuffers[i].numCommands = 0;
	}
	
	touches[BUTTON_MOVE].down=0;
	touches[BUTTON_FIRE].down=0;
	touches[BUTTON_GHOST].down=0;
}

void COM_StartScene(void)
{
	filehandle_t* ioFileHandle;
	static char filename[1024];
	if (engine.playback.play)
	{
		
		Log_Printf("[COM_StartScene] PLAYING BACK VIDEO: ");
		
		filename[0] = '\0';
		strcat(filename,"/data/commandRecord/");
		strcat(filename,engine.playback.filename);
		ioFileHandle = FS_OpenFile(filename,"rb"); 
		FS_UploadToRAM(ioFileHandle);
		
		if (!ioFileHandle)
		{
			engine.playback.play = 0;
			//FS_CloseFile(ioFileHandle);
			Log_Printf("[COM_StartScene] Cannot start playback: io file missing.\n");
			return;
		}
		
		numPlayers = *((int*)ioFileHandle->ptrStart);
		
		Log_Printf("Found %d players in this playback.\n",numPlayers);
		
		//recordCommands = calloc(NUM_RECORD_FRAMES, sizeof(command_t));
		currentRecordCommand = recordCommands;
		memcpy(recordCommands, ioFileHandle->ptrStart+4, sizeof(command_t)*NUM_RECORD_FRAMES);
		//recordCommands = (command_t*)(ioFileHandle->ptrStart+1);
		lastRecordCommand = currentRecordCommand + NUM_RECORD_FRAMES-1 ;
		
		
		
		FS_CloseFile(ioFileHandle);
	}
}

void COM_EndtScene(void)
{
	
	//free(recordCommands);
	//recordCommands = 0;
}


void COM_ResetTouchesBuffer(void)
{
	int i;
	
	for (i=0; i < NUM_BUTTONS; i++)
	{
		touches[i].down = 0;
		touches[i].dist[X] = 0;
		touches[i].dist[Y] = 0;
	}
}


