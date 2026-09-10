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

// v4.1.1 -- TWO themes, handed back and forth. The four acts share one track
// and it simply plays ON from act to act (a same-track scene change does not
// re-cue it), so it RAN OUT somewhere in act 3 or 4 and left the rest of the
// run in silence -- "j'arrive a un moment ou il n'y a pas de musique"
// (2026-09-09). When a track ends, the scene's `alternate` takes over and then
// hands back. Both players are built at scene load and kept prepared, so the
// hand-over costs the game thread a -[play] and nothing else: opening an audio
// file inside a frame is what round 51 was about. With no alternate declared
// the track simply loops, which is the old behaviour minus the silence.
static AVAudioPlayer* gMusic;			// playing now
static AVAudioPlayer* gMusicOther;		// prepared, waiting its turn
static int            gPaused;

@interface ShmupMusicHandover : NSObject <AVAudioPlayerDelegate>
@end

static ShmupMusicHandover* gHandover;

void SND_MusicHandOver(void);			// below

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


// A track reached its end. Swap to the other one if there is one, otherwise
// start this one again -- either way the run never goes quiet.
void SND_MusicHandOver(void)
{
	if (!engine.musicEnabled)
		return;
	if (gMusicOther)
	{
		AVAudioPlayer* done = gMusic;
		gMusic = gMusicOther;
		gMusicOther = done;
	}
	if (!gMusic)
		return;
	gMusic.currentTime = 0;
	[gMusic play];
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld handover -> %s\n", simulationTime, SND_WallMs(),
		           [[[gMusic.url lastPathComponent] description] UTF8String]);
}

@implementation ShmupMusicHandover
- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player successfully:(BOOL)flag
{
	(void)player; (void)flag;
	SND_MusicHandOver();
}
@end

// Build a prepared player for `path`, or nil. Load time only -- never a frame.
static AVAudioPlayer* SND_MusicOpen(const char* path)
{
	NSError* err = nil;
	NSURL* url;
	AVAudioPlayer* p;

	if (!path || !path[0])
		return nil;
	url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
	p = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&err];
	if (!p)
	{
		Log_Printf("[music] cannot open %s: %s\n", path, [[err localizedDescription] UTF8String]);
		return nil;
	}
	p.numberOfLoops = 0;			// one pass, then the delegate hands over
	p.delegate = gHandover;
	[p prepareToPlay];
	return p;
}
void SND_InitSoundTrack(char* filename, unsigned int startAt)
{
	if (!engine.musicEnabled)
		return;

	SND_AV_EnsureSession();
	if (!gHandover)
		gHandover = [[ShmupMusicHandover alloc] init];

	gMusic      = SND_MusicOpen(filename);
	gMusicOther = SND_MusicOpen(engine.musicAlternate);	// nil when the scene names none
	if (!gMusic)
		return;

	// No alternate: loop instead, so the act cannot outlast its music either way.
	gMusic.numberOfLoops = gMusicOther ? 0 : -1;
	gMusic.currentTime = (NSTimeInterval)startAt;
	gPaused = 0;
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld init %s at=%u%s\n", simulationTime, SND_WallMs(), filename, startAt,
		           gMusicOther ? " (+alternate)" : " (looping)");
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
	gMusicOther = nil;			// v4.1.1: the waiting theme goes with it
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
