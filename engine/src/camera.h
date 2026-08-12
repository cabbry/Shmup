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

	// TTB system: animated camera roll. 0 = normal top-down view, M_PI = flipped 180.
	float flipAngle;   // current roll around the view axis (radians)
	float flipTarget;  // flipAngle eases toward this (0 or M_PI)

} camera_t;





extern camera_t camera;

// Set to 1 (by dEngine, for the boss act) to keep the camera drifting forward
// once the baked path ends, instead of freezing the decor. 0 elsewhere.
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
void CAM_ToggleFlip(void); // TTB: toggle the 180 camera flip
#endif