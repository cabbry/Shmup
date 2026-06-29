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
//  dEngineAppDelegate.h
//  dEngine
//
//  Created by fabien sanglard on 09/08/09.
//  Copyright Memset software Inc 2009. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <GameKit/GameKit.h>

#define MAX_FPS 1.0f/45.f
#define IDLE_FPS 1.0f/5.f

@class EAGLView;

@interface dEngineAppDelegate : NSObject <UIApplicationDelegate, GKGameCenterControllerDelegate, GKMatchmakerViewControllerDelegate, GKMatchDelegate> {
    UIWindow *window;
    EAGLView *glView;
}

@property (nonatomic, retain) IBOutlet UIWindow *window;
@property (nonatomic, retain) IBOutlet EAGLView *glView;

// Online multiplayer: once GKMatch has every peer connected, elect a role and
// hand off to the engine. Safe to call repeatedly; it only fires once.
- (void)tryStartMatch;

@end

