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
 *  music_mci.c -- the soundtrack on MCI (round 87, Windows).
 *
 *  Behind the same five functions of music.h as music_av.m. Windows plays
 *  an MP3 through the Media Control Interface with a handful of command
 *  strings and no library. Semantics kept from the AV player: cue at
 *  startAt seconds, loop when the scene names no alternate, hand over to the
 *  alternate when the track ends (polled from the frame tick: MCI notifies
 *  through a window message we would rather not depend on), pause keeps the
 *  position, stop discards the player.
 *
 *  The [music] probes are the audio bench's: same events, same position.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include "music.h"
#include "dEngine.h"
#include "log.h"
#include "timer.h"

#define ALIAS "shmupmusic"

static char gTrack[512];		// playing now
static char gOther[512];		// the alternate, or empty
static int  gOpen, gPaused, gLoop;
static int  gNextPollMs;

static int MCI(const char* cmd, char* out, int outLen)
{
	MCIERROR e = mciSendStringA(cmd, out, (UINT)outLen, NULL);
	if (e)
	{
		char msg[256];
		mciGetErrorStringA(e, msg, sizeof(msg));
		Log_Printf("[music] '%s' failed: %s\n", cmd, msg);
		return 0;
	}
	return 1;
}

static long SND_WallMs(void) { return (long)GetTickCount64(); }

static double SND_MusicPosition(void)
{
	char out[64] = "";
	if (!gOpen) return -1.0;
	if (!MCI("status " ALIAS " position", out, sizeof(out))) return -1.0;
	return atol(out) / 1000.0;
}

static int MusicOpen(const char* path)
{
	char cmd[1200];
	if (!path || !path[0])
		return 0;
	snprintf(cmd, sizeof(cmd), "open \"%s\" type mpegvideo alias " ALIAS, path);
	if (!MCI(cmd, NULL, 0))
		return 0;
	MCI("set " ALIAS " time format milliseconds", NULL, 0);
	return 1;
}

static void MusicClose(void)
{
	if (gOpen)
		MCI("close " ALIAS, NULL, 0);
	gOpen = 0;
}

static void MusicPlay(unsigned int fromMs)
{
	char cmd[128];
	if (!gOpen) return;
	snprintf(cmd, sizeof(cmd), "play " ALIAS " from %u%s", fromMs, gLoop ? " repeat" : "");
	MCI(cmd, NULL, 0);
}

void SND_MusicProbeTick(void)
{
	static int frames = 0;
	int now;
	if (!engine.musicEnabled || !gOpen)
		return;
	// The hand-over: when the track has an alternate and stops, the other starts.
	now = E_Sys_Milliseconds();
	if (gOther[0] && !gPaused && now >= gNextPollMs)
	{
		char mode[64] = "";
		gNextPollMs = now + 500;
		if (MCI("status " ALIAS " mode", mode, sizeof(mode)) && strcmp(mode, "stopped") == 0)
		{
			char done[512];
			strcpy(done, gTrack); strcpy(gTrack, gOther); strcpy(gOther, done);
			MusicClose();
			gOpen = MusicOpen(gTrack);
			MusicPlay(0);
			if (Log_ProbesEnabled())
				Log_Printf("[music] t=%d wall=%ld handover -> %s\n", simulationTime, SND_WallMs(), gTrack);
		}
	}
	if (!Log_ProbesEnabled())
		return;
	if (++frames % 300 != 0)
		return;
	Log_Printf("[music] t=%d wall=%ld pos=%.2f\n", simulationTime, SND_WallMs(), SND_MusicPosition());
}

void SND_InitSoundTrack(char* filename, unsigned int startAt)
{
	if (!engine.musicEnabled)
		return;
	MusicClose();
	strncpy(gTrack, filename ? filename : "", sizeof(gTrack) - 1);
	strncpy(gOther, engine.musicAlternate, sizeof(gOther) - 1);
	gLoop = (gOther[0] == 0);
	gOpen = MusicOpen(gTrack);
	gPaused = 0;
	if (!gOpen)
		return;
	{
		char cmd[128];
		snprintf(cmd, sizeof(cmd), "seek " ALIAS " to %u", startAt * 1000u);
		MCI(cmd, NULL, 0);
	}
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld init %s at=%u%s\n", simulationTime, SND_WallMs(), gTrack, startAt,
		           gOther[0] ? " (+alternate)" : " (looping)");
}

void SND_StartSoundTrack(void)
{
	char out[64] = "";
	if (!engine.musicEnabled)
	{
		printf("[SND_StartSoundTrack] cancelled.\n");
		return;
	}
	if (!gOpen) return;
	MCI("status " ALIAS " position", out, sizeof(out));
	MusicPlay((unsigned)atol(out));
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
	if (gOpen) MCI("stop " ALIAS, NULL, 0);
	MusicClose();
	gOther[0] = 0;			// v4.1.1: the waiting theme goes with it
	gPaused = 1;
}

void SND_PauseSoundTrack(void)
{
	if (!engine.musicEnabled)
	{
		printf("[SND_PauseSoundTrack] cancelled.\n");
		return;
	}
	if (gPaused || !gOpen)
		return;
	MCI("pause " ALIAS, NULL, 0);
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
	if (!gPaused || !gOpen)
		return;
	// "resume" is not honoured by every MCI driver; play from the kept position is.
	{
		char out[64] = "";
		MCI("status " ALIAS " position", out, sizeof(out));
		MusicPlay((unsigned)atol(out));
	}
	gPaused = 0;
	if (Log_ProbesEnabled())
		Log_Printf("[music] t=%d wall=%ld resume pos=%.2f\n", simulationTime, SND_WallMs(), SND_MusicPosition());
}
