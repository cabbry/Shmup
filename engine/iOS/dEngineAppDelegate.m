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
	/*
	NSLog(@"applicationDidEnterBackground");
	[self stopEngineActivity];
	 */
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
void Action_ShowGameCenter(void* tag) {}
void Native_UploadFileTo(char path[256]) {}
