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
 *  camera.h
 *  dEngine
 *
 *  Created by fabien sanglard on 15/08/09.
 *  Copyright 2009 Memset software Inc. All rights reserved.
 *
 */

#ifndef DE_CAMERA
#define DE_CAMERA

#include "globals.h"
#include "math.h"
#include "quaternion.h"

typedef struct entity_visset_t
{
	ushort entityId;
	
	//Used for keyframes, full indices update
	ushort numIndices;
	ushort* indices;
	
	//Used for delta update
	ushort numFacesToRemove;
	ushort* facesToRemove;
	ushort numFacesToAdd;
	ushort* facesToAdd;
	
} entity_visset_t;

typedef struct world_vis_set_update_t
{
	char isKey;
	
	ushort numVisSets;
	entity_visset_t* visSets;
	
} world_vis_set_update_t;

typedef struct camera_frame_t
{
	uint time;
	vec3_t position;
	quat4_t orientation;
	
	world_vis_set_update_t visUpdate;
	
	struct camera_frame_t* next;
} camera_frame_t;

typedef struct 
{
	vec3_t position ;
	
	vec3_t forward ;
	vec3_t right ;
	vec3_t up;
		
	float aspect;
	float fov;
	float zNear;
	float zFar;
	
	uchar playing;
	char pathFilename[256];
	
	camera_frame_t* path;
	camera_frame_t* currentFrame;

	// TTB system (homage to *Tokyo Toy Box*): an animated camera ROLL around the
	// view axis, layered on top of whatever pose the rail gives. 0 = the usual
	// top-down view; M_PI/2 = the side view, where the city's vertical axis lies
	// across the screen and the world scrolls sideways -- the vertical shooter
	// reads as a side-scroller for the length of the beat. The ship keeps flying
	// forward the whole time; nothing ever scrolls backwards.
	//
	// A roll, and not a reframing, is what makes this affordable: the camera
	// POSITION never leaves the baked rail, so the visibility bake stays aligned
	// and the act goes back to it untouched once the beat is over.
	float ttbAngle;		// current roll around the view axis (radians)
	float ttbFrom;		// where the current transition started
	float ttbTarget;	// where it is heading
	int   ttbPhase;		// ms elapsed in the transition
	int   ttbDuration;	// ms the transition lasts (0 = snap)

} camera_t;





extern camera_t camera;

// Set to 1 by the scene ("driftAtEnd: 1" in its camera block) to keep the camera
// drifting forward once the baked path ends, instead of freezing the decor. The
// boss act needs it (the fight lasts as long as the boss does, not as long as
// the rail). 0 elsewhere.
extern int gCameraDriftAtEnd;

// Runtime decor culling (boss act). The 2010 pipeline bakes, per camera frame,
// which faces of the city are visible; the renderer then skips any entity the
// bake left empty. That ties the camera to its baked rail: look anywhere else
// (backwards, past the end) and the decor is simply gone -- black. On a tall
// iPhone the renderer ALREADY redraws full meshes (the baked face lists are
// discarded, see RenderEntityF), so the bake only gates entities on/off there.
// With this set, the boss act does that gating with a live per-entity frustum
// test instead, which frees the camera to fly and turn anywhere -- and makes
// the (hours-long) bake unnecessary for that act.
extern int gRuntimeCullMap;

void CAM_Update(void);
void CAM_InitUnitCube(void);
void CAM_LoadPath(void);
void CAM_StartPlaying(void);
void CAM_ClearAllRemainingCameraVS(void);

// TTB: roll the camera to `angleDegrees` around the view axis over `durationMs`
// (0 = snap). 0 = the usual view, 90 = the side view. Scripted from the scene
// file: "at 39000 ttbRoll angle 90 duration 2000".
void CAM_SetTTBRoll(float angleDegrees, int durationMs);

// The TTB billboard pose (column-major, no translation): the "top toward the
// camera, nose up-screen" billboard at angle 0, blending to the side profile
// (nose leading the travel) as the camera swings -- the nose stays in the
// screen plane the whole way (rig-proven; see player.c for the history).
// hullSlim: how much the model's Y axis shrinks at full deployment (0 = none,
// 0.2 = the player ship's 20%). Used by the player AND the enemies, so every
// craft reads correctly in the side view.
void CAM_GetTTBBlend(matrix_t out, float hullSlim);

// Same blend but the deployment stops at fCap (0..1): flat disc-like craft
// (turrets, Devils) hold a 3/4 pose in the side view instead of thinning to
// an unrecognizable blade -- device verdict on the full profile.
void CAM_GetTTBBlendCapped(matrix_t out, float hullSlim, float fCap);

// Rotate a screen-space velocity with the TTB beat (identity upright): what
// was authored "down at the player" fires screen-left in the side view, etc.
// For AUTHORED directions only -- aimed-at-player shots are computed in ss
// space and stay correct in any view untouched.
void CAM_TTBRotateSS(float* dx, float* dy);
#endif