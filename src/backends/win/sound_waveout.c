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
 *  sound_waveout.c -- the effects backend on waveOut (round 87, Windows).
 *
 *  Behind the same three functions as sound_av.m (sound_backend.h): one
 *  voice per effect, gain 0.5, a retrigger restarts the voice. No library:
 *  a software mixer feeds a waveOut device (22050 Hz, 16-bit mono, the
 *  format the 2009 WAVs are in) from its own thread, eight 256-frame blocks
 *  in flight, about 90 ms of worst-case latency.
 *
 *  The [snd] probe is the same line the AV backend prints, so the audio
 *  smoke's signature reads the same on both.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "sound_backend.h"
#include "dEngine.h"
#include "log.h"
#include "timer.h"

#define OUT_RATE     22050
#define BLOCK_FRAMES 256
#define NUM_BLOCKS   8

typedef struct
{
	short* samples;		// mono, OUT_RATE, 16-bit
	int    length;		// frames
	int    pos;			// -1 = silent
} voice_t;

static voice_t          gVoice[NUM_SOURCES];
static HWAVEOUT         gDevice;
static WAVEHDR          gHdr[NUM_BLOCKS];
static short            gBlock[NUM_BLOCKS][BLOCK_FRAMES];
static HANDLE           gEvent, gThread;
static CRITICAL_SECTION gLock;
static volatile int     gRunning;
static int              gReady;

static void MixBlock(short* out)
{
	int i, v;
	memset(out, 0, BLOCK_FRAMES * sizeof(short));
	EnterCriticalSection(&gLock);
	for (v = 0; v < NUM_SOURCES; v++)
	{
		voice_t* vo = &gVoice[v];
		if (vo->pos < 0 || !vo->samples)
			continue;
		for (i = 0; i < BLOCK_FRAMES && vo->pos < vo->length; i++, vo->pos++)
		{
			int s = out[i] + (vo->samples[vo->pos] >> 1);	// AL_GAIN 0.5, like the other backends
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			out[i] = (short)s;
		}
		if (vo->pos >= vo->length)
			vo->pos = -1;
	}
	LeaveCriticalSection(&gLock);
}

static DWORD WINAPI MixerThread(LPVOID arg)
{
	(void)arg;
	while (gRunning)
	{
		int b;
		WaitForSingleObject(gEvent, 100);
		for (b = 0; b < NUM_BLOCKS; b++)
		{
			if (!(gHdr[b].dwFlags & WHDR_DONE))
				continue;
			MixBlock(gBlock[b]);
			gHdr[b].dwFlags &= ~WHDR_DONE;
			waveOutWrite(gDevice, &gHdr[b], sizeof(WAVEHDR));
		}
	}
	return 0;
}

void SND_BACKEND_Init(void)
{
	WAVEFORMATEX fmt;
	int b;
	MMRESULT r;

	memset(&fmt, 0, sizeof(fmt));
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 1;
	fmt.nSamplesPerSec = OUT_RATE;
	fmt.wBitsPerSample = 16;
	fmt.nBlockAlign = 2;
	fmt.nAvgBytesPerSec = OUT_RATE * 2;

	InitializeCriticalSection(&gLock);
	for (b = 0; b < NUM_SOURCES; b++)
		gVoice[b].pos = -1;

	gEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	r = waveOutOpen(&gDevice, WAVE_MAPPER, &fmt, (DWORD_PTR)gEvent, 0, CALLBACK_EVENT);
	if (r != MMSYSERR_NOERROR)
	{
		Log_Printf("[snd] waveOutOpen failed (%d): no effects.\n", (int)r);
		return;
	}
	for (b = 0; b < NUM_BLOCKS; b++)
	{
		memset(&gHdr[b], 0, sizeof(WAVEHDR));
		gHdr[b].lpData = (LPSTR)gBlock[b];
		gHdr[b].dwBufferLength = BLOCK_FRAMES * sizeof(short);
		waveOutPrepareHeader(gDevice, &gHdr[b], sizeof(WAVEHDR));
		memset(gBlock[b], 0, sizeof(gBlock[b]));
		waveOutWrite(gDevice, &gHdr[b], sizeof(WAVEHDR));	// prime the queue with silence
	}
	gRunning = 1;
	gThread = CreateThread(NULL, 0, MixerThread, NULL, 0, NULL);
	SetThreadPriority(gThread, THREAD_PRIORITY_TIME_CRITICAL);
	gReady = 1;
	Log_Printf("[snd] backend waveOut, %d voices, %d Hz, %d blocks of %d frames.\n", NUM_SOURCES, OUT_RATE, NUM_BLOCKS, BLOCK_FRAMES);
}

void SND_BACKEND_Upload(sound_t* sound, int soundID)
{
	int channels, bytesPerSample, frames, i, outFrames;
	short* dst;
	long rate;

	if (!gReady || soundID < 0 || soundID >= NUM_SOURCES)
		return;
	if (!sound || !sound->data || sound->size <= 0)
	{
		Log_Printf("[snd] sound %d has no data.\n", soundID);
		return;
	}
	channels       = (sound->format == SND_FORMAT_STEREO16 || sound->format == SND_FORMAT_STEREO8) ? 2 : 1;
	bytesPerSample = (sound->format == SND_FORMAT_STEREO16 || sound->format == SND_FORMAT_MONO16)  ? 2 : 1;
	frames         = sound->size / (bytesPerSample * channels);
	rate           = (long)sound->metaData.sample_rate;
	if (rate <= 0) rate = OUT_RATE;
	outFrames = (int)((long long)frames * OUT_RATE / rate);
	dst = (short*)malloc((size_t)outFrames * sizeof(short));
	if (!dst)
		return;
	for (i = 0; i < outFrames; i++)
	{
		int src = (int)((long long)i * rate / OUT_RATE), c, acc = 0;	// nearest neighbour: the WAVs are 22050 already
		if (src >= frames) src = frames - 1;
		for (c = 0; c < channels; c++)
		{
			if (bytesPerSample == 1)
				acc += ((int)sound->data[src * channels + c] - 128) << 8;
			else
				acc += ((const short*)sound->data)[src * channels + c];
		}
		dst[i] = (short)(acc / channels);
	}
	EnterCriticalSection(&gLock);
	free(gVoice[soundID].samples);
	gVoice[soundID].samples = dst;
	gVoice[soundID].length = outFrames;
	gVoice[soundID].pos = -1;
	LeaveCriticalSection(&gLock);
	Log_Printf("[snd] buffer for SOUNDID%d: %d frames, %d ch, %ld Hz\n", soundID, frames, channels, rate);
}

void SND_BACKEND_Play(int sndId)
{
	if (!engine.soundEnabled)
		return;
	if (!gReady || sndId < 0 || sndId >= NUM_SOURCES || !gVoice[sndId].samples)
		return;
	if (Log_ProbesEnabled())
	{
		static const char* names[] = { "plasma", "explosion", "ghost_launch", "enemy_shot" };
		Log_Printf("[snd] t=%d play %d %s\n", simulationTime, sndId, (sndId >= 0 && sndId < 4) ? names[sndId] : "?");
	}
	EnterCriticalSection(&gLock);
	gVoice[sndId].pos = 0;		// a retrigger restarts the voice
	LeaveCriticalSection(&gLock);
}
