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
 *  music_av.m  --  the soundtrack on AVAudioPlayer (round 40)
 *
 *  Replaces the AudioQueue player (AQ.mm / AQPlayer.mm, Apple's 2009 sample
 *  code with its AudioSession calls) behind the same five functions of
 *  music.h. Semantics kept from the queue: cue at startAt seconds, no loop,
 *  pause keeps the position, stop discards the player, and the engine only
 *  calls init when the track actually changes (v2: the four acts share one
 *  file and the music plays on across them).
 *
 *  The [music] probes are the audio bench's: same events, and a position
 *  that must advance at wall-clock rate from the cue.
 */

#import <AVFoundation/AVFoundation.h>

#include "music.h"
#include "dEngine.h"
#include "log.h"
#include "timer.h"

void SND_AV_EnsureSession(void);	// sound_av.m

static AVAudioPlayer* gMusic;
static int            gPaused;

static long SND_WallMs(void)
{
	return (long)(CFAbsoluteTimeGetCurrent() * 1000.0);
}

static double SND_MusicPosition(void)
{
	return gMusic ? gMusic.currentTime : -1.0;
}

// Called once per frame from the view: a position sample every ~5 s of game.
void SND_MusicProbeTick(void)
{
	static int frames = 0;
	if (!Log_ProbesEnabled() || !engine.musicEnabled || !gMusic)
		return;
	if (++frames % 300 != 0)
		return;
	Log_Printf("[music] t=%d wall=%ld pos=%.2f\n", simulationTime, SND_WallMs(), SND_MusicPosition());
}

void SND_InitSoundTrack(char* filename, unsigned int startAt)
{
	NSError* err = nil;
	NSURL* url;

	if (!engine.musicEnabled)
		return;

	SND_AV_EnsureSession();
	url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:filename]];
	gMusic = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&err];
	if (!gMusic)
	{
		Log_Printf("[music] cannot open %s: %s\n", filename, [[err localizedDescription] UTF8String]);
		return;
	}
	gMusic.numberOfLoops = 0;
	[gMusic prepareToPlay];
	gMusic.currentTime = (NSTimeInterval)startAt;
	gPaused = 0;
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld init %s at=%u\n", simulationTime, SND_WallMs(), filename, startAt);
}

void SND_StartSoundTrack(void)
{
	if (!engine.musicEnabled)
	{
		printf("[SND_StartSoundTrack] cancelled.\n");
		return;
	}
	[gMusic play];
	gPaused = 0;
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld start pos=%.2f\n", simulationTime, SND_WallMs(), SND_MusicPosition());
}

void SND_StopSoundTrack(void)
{
	if (!engine.musicEnabled)
	{
		printf("[SND_StopSoundTrack] cancelled.\n");
		return;
	}
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld stop pos=%.2f\n", simulationTime, SND_WallMs(), SND_MusicPosition());
	[gMusic stop];
	gMusic = nil;
	gPaused = 1;
}

void SND_PauseSoundTrack(void)
{
	if (!engine.musicEnabled)
	{
		printf("[SND_PauseSoundTrack] cancelled.\n");
		return;
	}
	if (gPaused)
		return;
	[gMusic pause];
	gPaused = 1;
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld pause pos=%.2f\n", simulationTime, SND_WallMs(), SND_MusicPosition());
}

void SND_ResumeSoundTrack(void)
{
	if (!engine.musicEnabled)
	{
		printf("[SND_ResumeSoundTrack] cancelled.\n");
		return;
	}
	if (!gPaused || !gMusic)
		return;
	[gMusic play];
	gPaused = 0;
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld resume pos=%.2f\n", simulationTime, SND_WallMs(), SND_MusicPosition());
}
