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
 *  sounds.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-06-06.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */


#include "sounds.h"
#include <limits.h>
#include "dEngine.h"
#include "timer.h"
#include "sound_backend.h"


sound_t sounds[8];





void SND_Load(char* filename,int soundID)
{
    
    sound_t* sound = &sounds[soundID] ;
	
	if (!LoadWavInfo(filename, &sound->data, &sound->metaData ))
			Log_Printf("[SND_Load] Unable to load sound: '%s'.\n",filename);
	
	Log_Printf("[SND_Load] Loaded sound: %s sample_rate=%ld ",filename,sound->metaData.sample_rate);
	
	// Calculate buffer size
	sound->size = (int)(sound->metaData.samples * sound->metaData.sample_size * sound->metaData.channels);
	
	if( sound->metaData.sample_size == 2 )
	{
		if( sound->metaData.channels == 2 )
		{
			sound->format = SND_FORMAT_STEREO16;
			Log_Printf("format=AL_FORMAT_STEREO16.\n");
		}
		else
		{
			sound->format = SND_FORMAT_MONO16;
			Log_Printf("format=AL_FORMAT_MONO16.\n");			
		}
	}
	else if( sound->metaData.sample_size == 1 )
	{
		if( sound->metaData.channels == 2 )
		{
			sound->format = SND_FORMAT_STEREO8;
			Log_Printf("format=AL_FORMAT_STEREO8.\n");			
		}
		else
		{
			sound->format = SND_FORMAT_MONO8;
			Log_Printf("format=AL_FORMAT_MONO8.\n");
		}
	}
	//else
	//	Log_Printf("format=UNKNOWN.\n");
	sound->lastTimePlayed = INT_MIN;
	
	SND_BACKEND_Upload(sound,soundID);
	
	
	free(sound->data);
	sound->data=0;
    
    Log_Printf("Sound %s has been loaded (%d bytes).\n",filename,sound->size);
}




void SND_LoadsSoundLibrary(void )
{
	SND_Load("data/sfx/plasma.wav", SND_PLASMA);
	SND_Load("data/sfx/explosionShort.wav", SND_EXPLOSION);
	SND_Load("data/sfx/ghostLauch.wav", SND_GHOST_LAUNCH);
	SND_Load("data/sfx/enemy_shot.wav", SND_ENEMY_SHOT);
	SND_Load("data/sfx/siren.wav", SND_SIREN);	// v5: two rising wails, 2.4 s, synthesised (tools/rig/README notes the recipe)
}




int SND_Init(void)
{
	
	Log_Printf("[SND_Init] Initalizing sound system...\n");
	
		

	SND_BACKEND_Init();
	
	SND_LoadsSoundLibrary();
    
	return 1;

}


//char currentChannel=0;
void SND_PlaySound(int sndId)
{
    sound_t* sound;
    
    sound = &sounds[sndId];
    
    sound->lastTimePlayed = simulationTime;
    
    SND_BACKEND_Play(sndId);
    
}




