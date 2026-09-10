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
 *  per effect, volume 0.5, and a retrigger INTERRUPTS the buffer in flight.
 *  The WAVs (8-bit mono 22050 Hz) are converted once, at upload, into float32
 *  PCM buffers.
 *
 *  THE GAME THREAD NEVER ASKS THE ENGINE A QUESTION (round 51). Every
 *  AVAudioEngine property -- isRunning, a node's isPlaying -- takes the
 *  engine's lock, and that lock is held while CoreAudio reconfigures itself.
 *  Press the volume buttons and the audio system does exactly that, so the
 *  three lock acquisitions this path used to make per shot turned into the
 *  frame hitch the tester saw as "monter ou descendre le son fait ramer le
 *  jeu" (2026-09-09). The answers are mirrored in plain C now, kept true by
 *  the AVAudioEngine and AVAudioSession notifications, and the only engine
 *  call left in the hot path is the scheduleBuffer that IS the work.
 *
 *  The [snd] probe is printed for every request, before anything about the
 *  audio device can change the answer: the audio smoke's signature is the
 *  sequence the GAME asked for, and that contract does not move.
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

// The sim time each effect was last scheduled at -- one schedule per effect
// per frame is all that can be heard (see SND_BACKEND_Play). -1 = never.
static int                gLastScheduled[NUM_SOURCES];

// Plain-C mirrors of what the game thread would otherwise have to ASK the
// engine. gEngineRunning is set by a successful start and cleared by the
// notifications below; a node, once played, is never stopped, so gNodeStarted
// stays true until the engine is rebuilt under us.
static volatile int       gEngineRunning;
static int                gNodeStarted[NUM_SOURCES];

// Engine (re)building happens off the game thread, and not more than once a
// second if it keeps failing.
static dispatch_queue_t   gEngineQueue;
static volatile int       gRebuildPending;
static int                gNextRebuildMs;
#define SND_REBUILD_COOLDOWN_MS	1000

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

// Reconnect every loaded effect and start the engine. After a configuration
// change the graph's connections are gone, so they are remade from the
// buffers' own formats. Runs on gEngineQueue (or, once, at load time) -- never
// inside a frame.
static void SND_AV_RebuildNow(const char* why)
{
	NSError* err = nil;
	int i;
	int startedAt;

	if (!gEngine)
		return;

	// Timed, and printed unconditionally: this is the one operation that holds
	// the engine lock, and how long it holds it is the difference between a
	// glitch nobody hears and a frame the player sees drop. A device log now
	// says whether pressing the volume buttons provokes one at all, and what
	// it costs -- which is the measurement that was missing when this came
	// back as "monter le son fait ramer le jeu".
	startedAt = E_Sys_Milliseconds();

	[[AVAudioSession sharedInstance] setActive:YES error:NULL];

	for (i = 0; i < NUM_SOURCES; i++)
		if (gBuffers[i])
			[gEngine connect:gNodes[i] to:gEngine.mainMixerNode format:gBuffers[i].format];

	if ([gEngine startAndReturnError:&err])
	{
		for (i = 0; i < NUM_SOURCES; i++)
		{
			gNodeStarted[i] = 0;		// the graph is new: every node has to be played again
			gLastScheduled[i] = -1;
		}
		gEngineRunning = 1;
		Log_Printf("[snd] engine running again (%s) in %d ms.\n", why, E_Sys_Milliseconds() - startedAt);
	}
	else
	{
		gEngineRunning = 0;
		Log_Printf("[snd] engine restart failed (%s): %s\n", why,
		           [[err localizedDescription] UTF8String]);
	}
}

// Ask for a rebuild from the game thread: costs one integer compare and an
// enqueue, and never waits for the audio system.
static void SND_AV_RequestRebuild(const char* why)
{
	int now;

	if (!gEngineQueue || gRebuildPending)
		return;
	now = E_Sys_Milliseconds();
	if (now < gNextRebuildMs)
		return;						// a failing engine must not be retried every shot
	gNextRebuildMs = now + SND_REBUILD_COOLDOWN_MS;
	gRebuildPending = 1;
	dispatch_async(gEngineQueue, ^{
		SND_AV_RebuildNow(why);
		gRebuildPending = 0;
	});
}

void SND_BACKEND_Init(void)
{
	int i;
	NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];

	SND_AV_EnsureSession();
	gEngine = [[AVAudioEngine alloc] init];
	gEngineQueue = dispatch_queue_create("shmup.audio", DISPATCH_QUEUE_SERIAL);
	for (i = 0; i < NUM_SOURCES; i++)
	{
		gNodes[i] = [[AVAudioPlayerNode alloc] init];
		[gEngine attachNode:gNodes[i]];
		gNodes[i].volume = 0.5f;	// AL_GAIN 0.5 in the OpenAL backend
		gLastScheduled[i] = -1;
	}

	// The three ways CoreAudio can pull the graph out from under us. Each one
	// only sets a flag and queues the rebuild: the notification may well be
	// delivered ON the game thread, so nothing heavy happens here.
	[nc addObserverForName:AVAudioEngineConfigurationChangeNotification
	                object:gEngine
	                 queue:nil
	            usingBlock:^(NSNotification* n) {
		(void)n;
		gEngineRunning = 0;			// the connections are gone with it
		SND_AV_RequestRebuild("configuration change");
	}];
	[nc addObserverForName:AVAudioSessionInterruptionNotification
	                object:[AVAudioSession sharedInstance]
	                 queue:nil
	            usingBlock:^(NSNotification* n) {
		NSNumber* type = [n.userInfo objectForKey:AVAudioSessionInterruptionTypeKey];
		if ([type unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan)
			gEngineRunning = 0;
		else
			SND_AV_RequestRebuild("interruption ended");
	}];
	[nc addObserverForName:AVAudioSessionRouteChangeNotification
	                object:[AVAudioSession sharedInstance]
	                 queue:nil
	            usingBlock:^(NSNotification* n) {
		(void)n;
		SND_AV_RequestRebuild("route change");
	}];

	gReady = 1;
	Log_Printf("[snd] backend AVAudioEngine, %d player nodes.\n", NUM_SOURCES);
}

void SND_BACKEND_Upload(sound_t* sound, int soundID)
{
	int channels, bytesPerSample, frames, c, i;
	AVAudioFormat* fmt;
	AVAudioPCMBuffer* buf;

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

	// Load time, not frame time: start the engine here rather than on the first
	// shot, so no gunshot is ever the thing that waits for CoreAudio.
	if (!gEngineRunning)
		SND_AV_RebuildNow("first upload");
}

void SND_BACKEND_Play(int sndId)
{
	AVAudioPlayerNode* node;

	if (!engine.soundEnabled)
		return;
	if (!gReady || sndId < 0 || sndId >= NUM_SOURCES || !gBuffers[sndId])
		return;

	// The bench's contract is the sequence the GAME asked for, so it is stamped
	// before anything about the state of the audio device can change the answer.
	if (Log_ProbesEnabled())
	{
		static const char* names[] = { "plasma", "explosion", "ghost_launch", "enemy_shot" };
		Log_Printf("[snd] t=%d play %d %s\n", simulationTime, sndId, (sndId >= 0 && sndId < 4) ? names[sndId] : "?");
	}

	// The graph is down (a route change, an interruption, the volume HUD's own
	// reconfiguration). Queue the repair and drop this one shot: a sound missed
	// inside a hundred-millisecond audio glitch is invisible, where waiting for
	// the engine on the game thread is a hitch you can see.
	if (!gEngineRunning)
	{
		SND_AV_RequestRebuild("silent engine");
		return;
	}

	// ...AND DROP IT WHILE A REBUILD IS IN FLIGHT. This is the half round 51
	// missed. It took the engine's QUESTIONS off the game thread -- isRunning,
	// isPlaying -- but scheduleBuffer takes the very same engine lock, and
	// SND_AV_RebuildNow holds that lock on the audio queue for the whole of
	// setActive + N connects + start. A route change (which is what pressing
	// the volume buttons can provoke) therefore still stalled the next shot
	// for the length of a rebuild: "monter le son fait a nouveau ramer le
	// jeu" (2026-09-10). gEngineRunning does not cover it -- a route-change
	// rebuild never clears it, so the game thread walked straight into the
	// held lock. One integer read closes it: while the queue is rebuilding,
	// the game thread does not touch the engine at all.
	if (gRebuildPending)
		return;

	// ONE schedule per effect per frame. Retriggering INTERRUPTS the buffer in
	// flight, so when the same effect is asked for several times in one tick
	// only the LAST one is ever heard: the others are pure cost on the game
	// thread. On the act-1 bench 545 of 2453 plays (22 %) are same-effect-
	// same-tick, peaking at 13 audio calls in a single frame when a wave dies
	// together -- the end-of-level storm. Skipping them cannot change what
	// comes out of the speaker.
	if (gLastScheduled[sndId] == simulationTime && gNodeStarted[sndId])
		return;
	gLastScheduled[sndId] = simulationTime;

	node = gNodes[sndId];
	if (!gNodeStarted[sndId])
	{
		[node play];				// once per node per graph; never stopped after that
		gNodeStarted[sndId] = 1;
	}
	// Never -[AVAudioPlayerNode stop] per shot: on device (3.1.0) that stalled
	// the game loop until the render thread answered ("act 2 lags as if the
	// sounds slowed the game") and clicked at every restart.
	[node scheduleBuffer:gBuffers[sndId] atTime:nil options:AVAudioPlayerNodeBufferInterrupts completionHandler:nil];
}
