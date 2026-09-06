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
 *  sound_av.m  --  the effects backend on AVAudioEngine (round 40)
 *
 *  Replaces sound_openAL.c on iOS, behind the same three-function backend
 *  (sound_backend.h). The OpenAL backend had one source per effect, gain 0.5,
 *  and a retrigger restarted the source; this one has one AVAudioPlayerNode
 *  per effect, volume 0.5, and a retrigger stops the node before scheduling
 *  the buffer again. The WAVs (8-bit mono 22050 Hz) are converted once, at
 *  upload, into float32 PCM buffers.
 *
 *  The [snd] probe is printed after every play, exactly as OpenAL printed it:
 *  the audio smoke's signature of the sequence must not change.
 */

#import <AVFoundation/AVFoundation.h>

#include "sound_backend.h"
#include "dEngine.h"
#include "log.h"
#include "timer.h"

static AVAudioEngine*     gEngine;
static AVAudioPlayerNode* gNodes[NUM_SOURCES];
static AVAudioPCMBuffer*  gBuffers[NUM_SOURCES];
static int                gReady;

// One audio session for the effects and the soundtrack: playback category,
// as the 2009 AudioSession code set (kAudioSessionCategory_MediaPlayback),
// so the game is heard with the ring switch on silent, like before.
void SND_AV_EnsureSession(void)
{
	static int done = 0;
	NSError* err = nil;
	if (done)
		return;
	done = 1;
	AVAudioSession* session = [AVAudioSession sharedInstance];
	if (![session setCategory:AVAudioSessionCategoryPlayback error:&err])
		Log_Printf("[snd] session category failed: %s\n", [[err localizedDescription] UTF8String]);
	err = nil;
	if (![session setActive:YES error:&err])
		Log_Printf("[snd] session activate failed: %s\n", [[err localizedDescription] UTF8String]);
}

static int SND_AV_Start(void)
{
	NSError* err = nil;
	if (!gEngine)
		return 0;
	if (gEngine.isRunning)
		return 1;
	if (![gEngine startAndReturnError:&err])
	{
		Log_Printf("[snd] engine start failed: %s\n", [[err localizedDescription] UTF8String]);
		return 0;
	}
	return 1;
}

void SND_BACKEND_Init(void)
{
	int i;
	SND_AV_EnsureSession();
	gEngine = [[AVAudioEngine alloc] init];
	for (i = 0; i < NUM_SOURCES; i++)
	{
		gNodes[i] = [[AVAudioPlayerNode alloc] init];
		[gEngine attachNode:gNodes[i]];
		gNodes[i].volume = 0.5f;	// AL_GAIN 0.5 in the OpenAL backend
	}
	gReady = 1;
	Log_Printf("[snd] backend AVAudioEngine, %d player nodes.\n", NUM_SOURCES);
}

void SND_BACKEND_Upload(sound_t* sound, int soundID)
{
	int channels, bytesPerSample, frames, c, i;
	AVAudioFormat* fmt;
	AVAudioPCMBuffer* buf;

	if (!gReady || soundID < 0 || soundID >= NUM_SOURCES || !sound->data)
	{
		Log_Printf("[snd] upload of sound %d skipped.\n", soundID);
		return;
	}

	channels       = (sound->format == SND_FORMAT_STEREO16 || sound->format == SND_FORMAT_STEREO8) ? 2 : 1;
	bytesPerSample = (sound->format == SND_FORMAT_STEREO16 || sound->format == SND_FORMAT_MONO16)  ? 2 : 1;
	frames         = sound->size / (bytesPerSample * channels);

	fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:(double)sound->metaData.sample_rate channels:(AVAudioChannelCount)channels];
	buf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:(AVAudioFrameCount)frames];
	if (!fmt || !buf)
	{
		Log_Printf("[snd] buffer for sound %d could not be created.\n", soundID);
		return;
	}
	buf.frameLength = (AVAudioFrameCount)frames;

	// Interleaved integer PCM -> per-channel float32 in [-1,1].
	for (c = 0; c < channels; c++)
	{
		float* dst = buf.floatChannelData[c];
		if (bytesPerSample == 1)
		{
			const unsigned char* src = sound->data;
			for (i = 0; i < frames; i++)
				dst[i] = ((int)src[i * channels + c] - 128) / 128.0f;
		}
		else
		{
			const short* src = (const short*)sound->data;
			for (i = 0; i < frames; i++)
				dst[i] = src[i * channels + c] / 32768.0f;
		}
	}

	gBuffers[soundID] = buf;
	[gEngine connect:gNodes[soundID] to:gEngine.mainMixerNode format:fmt];
	Log_Printf("[snd] buffer for SOUNDID%d: %d frames, %d ch, %ld Hz\n", soundID, frames, channels, (long)sound->metaData.sample_rate);
}

void SND_BACKEND_Play(int sndId)
{
	AVAudioPlayerNode* node;

	if (!engine.soundEnabled)
		return;
	if (!gReady || sndId < 0 || sndId >= NUM_SOURCES || !gBuffers[sndId])
		return;
	if (!SND_AV_Start())
		return;

	// A retrigger restarts the sound, as replaying an OpenAL source did. Done
	// by INTERRUPTING the buffer in flight, never by stopping the node: on
	// device (3.1.0) -[AVAudioPlayerNode stop] per shot -- plasma fires every
	// 83 ms -- stalled the game loop until the render thread answered ("act 2
	// lags as if the sounds slowed the game") and clicked at every restart.
	node = gNodes[sndId];
	if (!node.isPlaying)
		[node play];
	[node scheduleBuffer:gBuffers[sndId] atTime:nil options:AVAudioPlayerNodeBufferInterrupts completionHandler:nil];

	// Audio bench: the same line the OpenAL backend printed -- the smoke's
	// signature of the sequence is the parity contract.
	if (Log_ProbesEnabled())
	{
		static const char* names[] = { "plasma", "explosion", "ghost_launch", "enemy_shot" };
		Log_Printf("[snd] t=%d play %d %s\n", simulationTime, sndId, (sndId >= 0 && sndId < 4) ? names[sndId] : "?");
	}
}
