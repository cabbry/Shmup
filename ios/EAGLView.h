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
//  EAGLView.h
//  dEngine
//
//  Created by fabien sanglard on 09/08/09.
//  Copyright Memset software Inc 2009. All rights reserved.
//

#import <UIKit/UIKit.h>

#include "../src/ItextureLoader.h"
#include "../src/globals.h"



/*
The game's one view. Since v3 (round 37) its layer is a CAMetalLayer driven by
engine/iOS/renderer_metal.m; the EAGL surface, the OpenGL ES 1.1 and 2.0
renderers and the OpenGLES framework are gone. The class keeps its 2009 name
because the two nibs (MainWindow.xib, MainWindow-iPad.xib) instantiate it by
that name and the app delegate outlet is typed with it.
*/
@interface EAGLView : UIView {

@private

	BOOL animating;
	BOOL displayLinkSupported;
    NSTimer *animationTimer;
	id displayLink;
    NSInteger animationFrameInterval;
}

@property (readonly, nonatomic, getter=isAnimating) BOOL animating;
@property (nonatomic) NSInteger animationFrameInterval;

- (void)startAnimation;
- (void)stopAnimation;
- (void) checkEngineSettings;
- (void)drawView:(id)sender;
- (void) loadTexture:(texture_t*) tmpTex;

@end
