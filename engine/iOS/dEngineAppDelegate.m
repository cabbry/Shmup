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
//
//  dEngineAppDelegate.m
//  dEngine
//
//  Created by fabien sanglard on 09/08/09.
//  Copyright Memset software Inc 2009. All rights reserved.
//

#import "dEngineAppDelegate.h"
#import "EAGLView.h"
#import "camera.h"
#include "commands.h"
#include "dEngine.h"
#include "netchannel.h"
#include "menu.h"
#include "music.h"
#include "native_services.h"   // declares Native_UploadScore et al. (we define the stubs below)

#include <sys/types.h>
#include <sys/sysctl.h>

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <GameKit/GameKit.h>

dEngineAppDelegate* this=nil;
UIViewController* vc=nil;

// Online multiplayer (GKMatch). gMatch is the live match; gMatchStarted guards
// the one-shot role election / handoff to the engine.
static GKMatch* gMatch = nil;
static BOOL     gMatchStarted = NO;



@implementation dEngineAppDelegate

@synthesize window;
@synthesize glView;


- (void) stopEngineActivity
{
	Native_UploadScore(players[controlledPlayer].score);
	dEngine_Pause();
	[glView stopAnimation];
}

- (void) applicationDidFinishLaunching:(UIApplication *)application
{
	NSLog(@"applicationDidFinishLaunching");
	[[UIApplication sharedApplication] setStatusBarHidden:YES];
	[UIApplication sharedApplication].idleTimerDisabled = YES;

    if (vc == nil)
    {
        vc = [UIViewController new];

    }
    [self.window setRootViewController:vc];

    // Sign the player into Game Center (for the online leaderboard, and the
    // foundation for GameKit real-time multiplayer later). iOS presents its own
    // sign-in sheet via the handler.
    [GKLocalPlayer local].authenticateHandler = ^(UIViewController *gcVC, NSError *error) {
        if (gcVC) {
            [vc presentViewController:gcVC animated:YES completion:nil];
        } else if ([GKLocalPlayer local].isAuthenticated) {
            NSLog(@"[GameCenter] authenticated as %@", [GKLocalPlayer local].displayName);
        } else {
            NSLog(@"[GameCenter] not authenticated: %@", error);
        }
    };
}

- (void) applicationWillResignActive:(UIApplication *)application
{
	NSLog(@"applicationWillResignActive");
	// Freeze the in-progress game instead of tearing it down to the menu:
	// pause (not stop) the music queue so it can resume cleanly with its buffers
	// intact, and halt the render loop. All game state is kept.
	SND_PauseSoundTrack();
	[glView stopAnimation];
}

- (void) applicationDidBecomeActive:(UIApplication *)application
{
	NSLog(@"applicationDidBecomeActive");
	this = self;
	vc.view = [this glView];

	[glView checkEngineSettings];

	// Keep the game (do NOT reset). Resume the music queue from where it was
	// paused, then arm the 3-2-1-SHMUP countdown if a game was in progress.
	SND_ResumeSoundTrack();
	dEngine_ResumeGame();

	[glView startAnimation];
}

- (void)applicationWillTerminate:(UIApplication *)application
{
	NSLog(@"applicationWillTerminate");
	[self stopEngineActivity];

}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
	// A live 2-player game can't be paused (the peer keeps playing), so backgrounding
	// desyncs it -- on return you'd see a frozen screen while the other player moved on.
	// So when the app is really backgrounded during a multiplayer match, end the match
	// cleanly and go back to the menu instead of resuming out of sync. (Single-player
	// still pauses + 3-2-1 resumes, handled in applicationDidBecomeActive.)
	if (engine.mode == DE_MODE_MULTIPLAYER && NET_IsRunning())
	{
		NET_Free();
		MENU_Set(MENU_HOME);
		dEngine_RequireSceneId(0);
	}
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
	/*
	NSLog(@"applicationWillEnterForeground");
	engineDelegate = self;

	[glView startAnimation];
	 dEngine_Resume();
	*/
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application
{
	printf("*****************************************************\n");
	printf("******[applicationDidReceiveMemoryWarning] WARNING***\n");
	printf("*****************************************************\n");
}

- (void)dealloc {
	[window release];
	[glView release];
	[super dealloc];
}

// Dismiss the Game Center leaderboard sheet when the player taps Done.
- (void)gameCenterViewControllerDidFinish:(GKGameCenterViewController *)gameCenterViewController {
	[gameCenterViewController dismissViewControllerAnimated:YES completion:nil];
	// Clear the "Scores" button's stuck highlight (we never switched menus).
	MENU_ClearButtonStates();
}

#pragma mark - Online multiplayer (GKMatch)

// The matchmaker found a match: keep it, become its delegate, and try to start
// (it may already be fully connected).
- (void)matchmakerViewController:(GKMatchmakerViewController *)viewController didFindMatch:(GKMatch *)match {
	[viewController dismissViewControllerAnimated:YES completion:nil];
	if (gMatch != match) {
		[gMatch release];
		gMatch = [match retain];
	}
	gMatch.delegate = this;
	[self tryStartMatch];
}

// The player backed out of matchmaking.
- (void)matchmakerViewControllerWasCancelled:(GKMatchmakerViewController *)viewController {
	[viewController dismissViewControllerAnimated:YES completion:nil];
	NET_AbortOnlineMatch();
}

- (void)matchmakerViewController:(GKMatchmakerViewController *)viewController didFailWithError:(NSError *)error {
	NSLog(@"[GKMatch] matchmaking failed: %@", error);
	[viewController dismissViewControllerAnimated:YES completion:nil];
	NET_AbortOnlineMatch();
}

// A peer connected or dropped.
- (void)match:(GKMatch *)match player:(GKPlayer *)player didChangeConnectionState:(GKPlayerConnectionState)state {
	if (match != gMatch) return;
	if (state == GKPlayerStateDisconnected) {
		gMatchStarted = NO;
		if (NET_IsRunning())
			NET_OnPeerLost();		// mid-match: clean scene reset + "other player left" notice
		else
			NET_AbortOnlineMatch();	// still matchmaking: just back out to the menu
		return;
	}
	[self tryStartMatch];
}

// A packet arrived from the peer: hand the raw bytes straight to the engine.
- (void)match:(GKMatch *)match didReceiveData:(NSData *)data fromRemotePlayer:(GKPlayer *)player {
	if (match != gMatch) return;
	NET_OnNetworkData(data.bytes, (int)data.length);
}

// Elect a deterministic role and start exactly once, after every expected player
// is connected. Both ends run the same comparison (lowest gamePlayerID wins the
// SERVER seat), so they agree on who is Player One without any negotiation.
- (void)tryStartMatch {
	if (gMatchStarted || gMatch == nil) return;
	if (gMatch.expectedPlayerCount != 0) return;	// still waiting for the peer

	NSString* myId = [GKLocalPlayer local].gamePlayerID;
	NSString* peerId = nil;
	for (GKPlayer* p in gMatch.players) { peerId = p.gamePlayerID; break; }	// 2-player: a single peer

	BOOL isServer = (peerId == nil) || ([myId compare:peerId] == NSOrderedAscending);

	gMatchStarted = YES;
	NET_StartOnlineMatch(isServer ? 1 : 0);
}

@end


#pragma mark - Native services (platform stubs)

/*
 The engine (event.c, menu.c, player.c) calls these C entry points through
 native_services.h. Every other platform backend (win32, linux, macOS,
 Android) implements them as no-ops -- see e.g. engine/win32/native.c.

 The original iOS implementations were removed during modernization:
   - Game Center (GKLeaderboardViewController / GKScore) : APIs removed/
     deprecated in current SDKs, and the feature was already disabled at
     runtime (engine.gameCenterEnabled is forced to 0).
   - "Buy the full version" cross-promo (UIAlertView)    : deprecated, and
     the linked App Store page no longer exists.
   - Replay telemetry upload to fabiensanglard.net       : dead endpoint,
     and plain-HTTP POSTs are blocked by App Transport Security.

 Native_RetrieveListOf (the replay file listing) is still implemented for
 real in EAGLView.m -- it uses NSFileManager and needs no modernization.
*/

void Native_UploadScore(uint score) {
	if (![GKLocalPlayer local].isAuthenticated)
		return;
	if (@available(iOS 14.0, *)) {
		[GKLeaderboard submitScore:(NSInteger)score
		                   context:0
		                    player:[GKLocalPlayer local]
		            leaderboardIDs:@[@"shmup.highscores"]
		         completionHandler:^(NSError * _Nullable error) {
			if (error) NSLog(@"[GameCenter] submitScore error: %@", error);
			else       NSLog(@"[GameCenter] score %u submitted", score);
		}];
	}
}
void Native_LoginGameCenter(void) {}
void Action_ShowGameCenter(void* tag) {
	if (!this || !vc) return;
	if (![GKLocalPlayer local].isAuthenticated) return;

	GKGameCenterViewController* gcvc;
	if (@available(iOS 14.0, *)) {
		gcvc = [[GKGameCenterViewController alloc] initWithLeaderboardID:@"shmup.highscores"
		                                                    playerScope:GKLeaderboardPlayerScopeGlobal
		                                                      timeScope:GKLeaderboardTimeScopeAllTime];
	} else {
		gcvc = [[GKGameCenterViewController alloc] init];
		gcvc.viewState = GKGameCenterViewControllerStateLeaderboards;
	}
	gcvc.gameCenterDelegate = this;
	[vc presentViewController:gcvc animated:YES completion:nil];
	[gcvc release];
}
void Native_UploadFileTo(char path[256]) {}

// Persist the solo loadout (ship + bullet colour) in NSUserDefaults; restored at
// launch in EAGLView's checkEngineSettings.
void Native_SaveLoadout(int ship, int color) {
	NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
	[d setInteger:ship  forKey:@"ShipChoice"];
	[d setInteger:color forKey:@"BulletColor"];
	[d synchronize];
}

// Persist the furthest act ever reached (gates the act-select screen); restored
// at launch in EAGLView's checkEngineSettings.
void Native_SaveProgress(int highestAct) {
	NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
	[d setInteger:highestAct forKey:@"HighestAct"];
	[d synchronize];
}

#pragma mark - Online multiplayer bridge (engine -> GameKit)

// Present Apple's matchmaker UI (invite a friend, or auto-match an opponent).
void Native_StartOnlineMatchmaking(void) {
	if (!this || !vc) return;
	if (![GKLocalPlayer local].isAuthenticated) {
		// Not signed into Game Center: cannot matchmake -- bounce back to the menu.
		NET_AbortOnlineMatch();
		return;
	}

	Native_CancelOnlineMatchmaking();	// drop any stale match first

	GKMatchRequest* req = [[GKMatchRequest alloc] init];
	req.minPlayers = 2;
	req.maxPlayers = 2;

	GKMatchmakerViewController* mmvc = [[GKMatchmakerViewController alloc] initWithMatchRequest:req];
	if (mmvc == nil) {
		// Matchmaking unavailable (e.g. Game Center restricted): back out cleanly.
		[req release];
		NET_AbortOnlineMatch();
		return;
	}
	mmvc.matchmakerDelegate = this;
	[vc presentViewController:mmvc animated:YES completion:nil];

	[mmvc release];
	[req release];
}

// Tear down the live match (also called from NET_Free when an online session ends).
void Native_CancelOnlineMatchmaking(void) {
	GKMatch* m = gMatch;
	gMatch = nil;				// nil first so re-entrant delegate callbacks bail out
	gMatchStarted = NO;
	m.delegate = nil;
	[m disconnect];
	[m release];
}

// Send a packet to the peer. Setup/death packets go reliable; per-frame runtime
// deltas go unreliable (the caller decides via the reliable flag).
void Native_GKSendData(const void* data, int len, int reliable) {
	if (gMatch == nil || data == NULL || len <= 0) return;
	NSData* d = [NSData dataWithBytes:data length:(NSUInteger)len];
	NSError* err = nil;
	[gMatch sendDataToAllPlayers:d
	               withDataMode:(reliable ? GKMatchSendDataReliable : GKMatchSendDataUnreliable)
	                      error:&err];
	if (err) NSLog(@"[GKMatch] send error: %@", err);
}
