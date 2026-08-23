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
 *  r_fixed_renderer.c
 *  dEngine
 *
 *  Created by fabien sanglard on 09/08/09.
 *  Copyright 2009 Memset software Inc. All rights reserved.
 *
 */

#include "config.h"

//#define RENDER_COLL_BOXEX


#include "renderer_fixed.h"
#include "dEngine.h"
#include "camera.h"


#include "target.h"
#if defined(SHMUP_TARGET_WINDOWS)
	#include "GLES/gl.h"
#elif defined(SHMUP_TARGET_MACOSX)
    #include "OpenGL/gl.h"
    #define glOrthof glOrtho
    #define glFogx glFogf
#elif defined(SHMUP_TARGET_IOS)
	#include <OpenGLES/ES1/gl.h>
#elif defined(ANDROID)
    #include <GLES/gl.h>
#elif defined(LINUX)
    #include <GL/gl.h>
    #define glOrthof glOrtho
    #define glFogx glFogf
#endif


#include <stdlib.h>
#include "stats.h"
#include "collisions.h"	// live decor culling (frustum vs entity bbox)
#include "world.h"
#include "player.h"
#include "enemy.h"
#include "timer.h"
#include <limits.h>
#include "fx.h"
#include "commands.h"
#include "enemy_particules.h"

static matrix_t projectionMatrix;
static matrix_t modelViewMatrix;
matrix_t textureMatrix = { 1.0f/32767,       0,0,0,
                           0,          1.0f/32767,0,0,0,0,1,0,0,0,0,1};	//Unpacking matrix since texture coordinates are normalized in a short instead of a float.
unsigned int lastTextureId;



int supportedCompressionFormatF;


void SCR_CheckErrors(char* step, char* details)
{
	GLenum err = glGetError();
	switch (err) {
		case GL_INVALID_ENUM:Log_Printf("Error GL_INVALID_ENUM %s, %s\n", step,details); break;
		case GL_INVALID_VALUE:Log_Printf("Error GL_INVALID_VALUE  %s, %s\n", step,details); break;
		case GL_INVALID_OPERATION:Log_Printf("Error GL_INVALID_OPERATION  %s, %s\n", step,details); break;				
		case GL_OUT_OF_MEMORY:Log_Printf("Error GL_OUT_OF_MEMORY  %s, %s\n", step,details); break;			
		case GL_NO_ERROR: break;
		default :Log_Printf("Error UNKNOWN  %s, %s\n", step,details);break;
	}
}

void SetupCameraF(void)
{
	vec3_t vLookat;
	
	vectorAdd(camera.position,camera.forward,vLookat);
	
	gluLookAt(camera.position, vLookat, camera.up, modelViewMatrix);
	
	//Log_Printf("t=%d, up=[%.2f,%.2f,%.2f]\n",simulationTime,camera.up[X],camera.up[Y],camera.up[Z]);
	
	glLoadMatrixf(modelViewMatrix);
}


void SetupLightingF(void)
{
	glLightfv(GL_LIGHT0, GL_POSITION, light.position);
	glLightfv(GL_LIGHT0, GL_AMBIENT, light.ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, light.diffuse);
	glLightfv(GL_LIGHT0, GL_SPECULAR, light.specula);
	
	glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, light.constantAttenuation);
	glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, light.linearAttenuation);
}


void Set2DF(void)
{
	//printf("[Set2DF] glClear to be removed.\n");
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	glEnable(GL_BLEND);
	//glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
	// Unscaled 2:3 box: this keeps the 2D overlay (bullets, ghost, flash, enemy
	// bullets) aligned with the 3D ship/enemies, which map ss_position [-1,1] to
	// the full screen. Sprites stretch vertically by vScale on a tall screen, but
	// text is un-stretched in SCR_ConvertTextToVertices.
	glOrthof(-SS_W, SS_W, -SS_H, SS_H, -1, 1);
	
	
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
	
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	

	glDisable(GL_LIGHTING);

	
	glDisable(GL_DEPTH_TEST);
	
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	
	glBindBuffer(GL_ARRAY_BUFFER,0);
	
}



void Set3DF(void)
{
	// Clear to the scene's fog colour, not black: any pixel no geometry reaches
	// (the sliver between the city's silhouette and the sky dome's skirt in the
	// TTB side view) then reads as more fog instead of a hard black band.
	if (engine.fogEnabled)
		glClearColor(renderer.fogColor[0], renderer.fogColor[1], renderer.fogColor[2], 1.0f);
	else
		glClearColor(0, 0, 0, 1.0f);

	//SCrew the iPod 2nd generation !! It seems that color gets cleaned anyway.....with PINK !!?!?!
	glClear (GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	//glClear (GL_DEPTH_BUFFER_BIT); 
	
	glEnable(GL_DEPTH_TEST);
	
	
	glDisable(GL_BLEND);
	renderer.isBlending = 0;
	
	
	
	glEnableClientState (GL_NORMAL_ARRAY);
	
	if (light.enabled)
	{
		glEnable(GL_LIGHTING);
		glEnable(GL_LIGHT0);
	}
	
	glEnable(GL_TEXTURE_2D);
	glShadeModel(GL_SMOOTH);
	
	
	
	
	
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	
	

	//glDisable ( GL_COLOR_MATERIAL ) ;

	glBindBuffer(GL_ARRAY_BUFFER,0);
	
	glColor4f(1, 1, 1, 1);
}

void StopRenditionF(void)
{
	lastTextureId = -1;
}


void UpLoadTextureToGPUF(texture_t* texture)
{
	int i,mipMapDiv;
	
	if (!texture || !texture->data || texture->textureId != 0)
		return;
	
	glGenTextures(1, &texture->textureId);
	glBindTexture(GL_TEXTURE_2D, texture->textureId);
	
		
	if (texture->format == TEXTURE_GL_RGB ||texture->format == TEXTURE_GL_RGBA)
	{
		glTexParameterf(GL_TEXTURE_2D,GL_GENERATE_MIPMAP, GL_TRUE);
		
        if(texture->format == TEXTURE_GL_RGBA)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture->data[0]);
        }
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture->width, texture->height, 0, GL_RGB, GL_UNSIGNED_BYTE, texture->data[0]);

		free(texture->data[0]);
		texture->data[0] = 0;
	}
	else
	{
		glTexParameterf(GL_TEXTURE_2D,GL_GENERATE_MIPMAP, GL_FALSE);
		
		glCompressedTexImage2D(GL_TEXTURE_2D, 0, texture->format, texture->width,texture-> height, 0, texture->dataLength[0], texture->data[0]);
		//Log_Printf("Uploading mipmapp %d w=%d, h=%d, size=%d\n",0,texture->width,texture-> height,texture->dataLength[0]);
		
		mipMapDiv = 2;
		for (i=1; i < texture->numMipmaps; i++,mipMapDiv*=2) 
		{
			glCompressedTexImage2D(GL_TEXTURE_2D, i, texture->format, texture->width/mipMapDiv,texture-> height/mipMapDiv, 0, texture->dataLength[i], texture->data[i]);
		//	Log_Printf("Uploading mipmapp %d w=%d, h=%d, size=%d\n",i,texture->width/mipMapDiv,texture-> height/mipMapDiv,texture->dataLength[i]);
			free(texture->data[i]);
			texture->data[i] = 0;
		}
	}
	
	
	//Using mipMapping to reduce bandwidth consumption
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	
	SCR_CheckErrors("Loading texture",texture->path);
	
	free(texture->dataLength); 
	texture->dataLength = 0;
	free(texture->data);	
	texture->data = 0;
	
	texture->memLocation = TEXT_MEM_LOC_VRAM;
	
	if (texture->file != NULL)
		FS_CloseFile(texture->file);
}

//This is just a debug fiunction
short collisionBoxes[8];
ushort collisionBoxesIndices[6] = {0,1,2,0,2,3};
void RenderCollisionBoxes(void)
{
	int i,j;
	enemy_t* enemy;
	xf_colorless_sprite_t* enemyBullet;
	float alpha = 0.3;
	
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);
	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisable(GL_CULL_FACE);
	glBlendFunc(GL_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glColor4f(1, 1, 1, alpha);
	
	//PLAYER
	for (i=0; i < numPlayers; i++) {
		collisionBoxes[1] =  players[i].ss_boudaries[UP];
		collisionBoxes[0] =  players[i].ss_boudaries[LEFT];
		
		collisionBoxes[3] = players[i].ss_boudaries[DOWN];
		collisionBoxes[2] = players[i].ss_boudaries[LEFT];
		
		collisionBoxes[5] = players[i].ss_boudaries[DOWN];
		collisionBoxes[4] = players[i].ss_boudaries[RIGHT];

		collisionBoxes[7] = players[i].ss_boudaries[UP];
		collisionBoxes[6] = players[i].ss_boudaries[RIGHT];

		
		glVertexPointer (2, GL_SHORT,0,collisionBoxes);
		glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, collisionBoxesIndices);	
	}
	
	//ENEMIES
	glColor4f(1, 0, 0, alpha);
	enemy = ENE_GetFirstEnemy();
	while (enemy != NULL) {
		
		collisionBoxes[1] = enemy->ss_boudaries[UP];
		collisionBoxes[0] = enemy->ss_boudaries[LEFT];
		
		collisionBoxes[3] = enemy->ss_boudaries[DOWN];
		collisionBoxes[2] = enemy->ss_boudaries[LEFT];
		
		collisionBoxes[5] = enemy->ss_boudaries[DOWN];
		collisionBoxes[4] = enemy->ss_boudaries[RIGHT];
		
		collisionBoxes[7] = enemy->ss_boudaries[UP];
		collisionBoxes[6] = enemy->ss_boudaries[RIGHT];
		
		
		glVertexPointer (2, GL_SHORT,0,collisionBoxes);
		glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, collisionBoxesIndices);	
		
		enemy = enemy->next;
	}
	
	//PLAYER BULLETS
	glColor4f(0, 0, 1, alpha);
	for (i=0; i < numPlayers; i++) 
	{
		for (j=0; j < MAX_PLAYER_BULLETS; j++) 
		{
			if (players[i].bullets[j].expirationTime <= simulationTime)	// <=: exp 0 at a timer reset (t=0) means expired
				continue;
			
			collisionBoxes[1] = players[i].bullets[j].ss_boudaries[UP];
			collisionBoxes[0] = players[i].bullets[j].ss_boudaries[LEFT];
			
			collisionBoxes[3] = players[i].bullets[j].ss_boudaries[DOWN];
			collisionBoxes[2] = players[i].bullets[j].ss_boudaries[LEFT];
			
			collisionBoxes[5] = players[i].bullets[j].ss_boudaries[DOWN];
			collisionBoxes[4] = players[i].bullets[j].ss_boudaries[RIGHT];
			
			collisionBoxes[7] = players[i].bullets[j].ss_boudaries[UP];
			collisionBoxes[6] = players[i].bullets[j].ss_boudaries[RIGHT];
			
			glVertexPointer (2, GL_SHORT,0,collisionBoxes);
			glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, collisionBoxesIndices);	
		}
	}
	
	//ENEMY BULLETS
	glColor4f(1, 0, 1, alpha);
	enemyBullet = partLib.ss_vertices;
	
	i = 0;
	while( i < partLib.numParticules) 
	{
		collisionBoxes[0] =  enemyBullet->pos[X];
		collisionBoxes[1] =  enemyBullet->pos[Y];
		
		collisionBoxes[2] = enemyBullet->pos[X];
		collisionBoxes[3] = enemyBullet[1].pos[Y];
		
		collisionBoxes[4] = enemyBullet[2].pos[X];
		collisionBoxes[5] = enemyBullet[1].pos[Y];
		
		collisionBoxes[6] = enemyBullet[2].pos[X];
		collisionBoxes[7] = enemyBullet->pos[Y];
		
		
		
		glVertexPointer (2, GL_SHORT,0,collisionBoxes);
		glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, collisionBoxesIndices);	
		
		enemyBullet+=4;
		i++;
	}
	
	
	//glDisable(GL_BLEND);
	glBlendFunc(GL_ALPHA, GL_ONE);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnable(GL_TEXTURE_2D);
	glColor4f(1, 1, 1, 1);
	
	if (light.enabled)
		glEnable(GL_LIGHTING);
}


void SetTextureF(unsigned int textureId)
{
	if (lastTextureId == textureId)
		return;
	
	glBindTexture(GL_TEXTURE_2D, textureId);
	STATS_AddTexSwitch();
	
	lastTextureId = textureId;
}





void RenderNormalsF(md5_mesh_t* currentMesh)
{
	
	float normalsVertex[20000];
	float* normalVertex;
	int j;
	
	float vScale = 3;
	
	glDisable(GL_TEXTURE_2D);

	glDisable(GL_LIGHTING);

	glDisableClientState (GL_NORMAL_ARRAY);
	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	
	
	normalVertex = normalsVertex;
	for(j=0; j < currentMesh->numVertices ; j++)
	{
		*normalVertex++ = currentMesh->vertexArray[j].pos[0];
		*normalVertex++ = currentMesh->vertexArray[j].pos[1];
		*normalVertex++ = currentMesh->vertexArray[j].pos[2];
		
		*normalVertex++ = currentMesh->vertexArray[j].pos[0] + currentMesh->vertexArray[j].normal[0]/DE_SHRT_MAX*vScale;
		*normalVertex++ = currentMesh->vertexArray[j].pos[1] + currentMesh->vertexArray[j].normal[1]/DE_SHRT_MAX*vScale;
		*normalVertex++ = currentMesh->vertexArray[j].pos[2] + currentMesh->vertexArray[j].normal[2]/DE_SHRT_MAX*vScale;
	}
	glColor4f(1, 0, 0, 1);
	glVertexPointer (3, GL_FLOAT,0,normalsVertex);
	glDrawArrays(GL_LINES, 0, currentMesh->numVertices * 2);
	
	
	#ifdef TANGENT_ENABLED
	normalVertex = normalsVertex;
	for(j=0; j < currentMesh->numVertices ; j++)
	{
		*normalVertex++ = currentMesh->vertexArray[j].pos[0];
		*normalVertex++ = currentMesh->vertexArray[j].pos[1];
		*normalVertex++ = currentMesh->vertexArray[j].pos[2];
		
		*normalVertex++ = currentMesh->vertexArray[j].pos[0] + currentMesh->vertexArray[j].tangent[0]/DE_SHRT_MAX*vScale;
		*normalVertex++ = currentMesh->vertexArray[j].pos[1] + currentMesh->vertexArray[j].tangent[1]/DE_SHRT_MAX*vScale;
		*normalVertex++ = currentMesh->vertexArray[j].pos[2] + currentMesh->vertexArray[j].tangent[2]/DE_SHRT_MAX*vScale;
	}
	glColor4f(0, 0, 1, 1);
	glVertexPointer (3, GL_FLOAT,0,normalsVertex);
	glDrawArrays(GL_LINES, 0, currentMesh->numVertices * 2);
	#endif
	
	glColor4f(1, 1, 1, 1);
	glEnable(GL_TEXTURE_2D);
	if (light.enabled)		
		glEnable(GL_LIGHTING);

	glEnableClientState(GL_NORMAL_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

}



#define TRACE_RENDITION 0
//int traceRenderEntity = 0;
static void RenderEntityF(entity_t* entity)
{

//	Log_Printf("RenderEntityF Player1=%p\n",players[0].entity.material);
//	Log_Printf("RenderEntityF Player2=%p\n",players[1].entity.material);		
	
	glPushMatrix();
	
	//if (traceRenderEntity)
	{
		//entity->matrix[13] = 110;
	//	Log_Printf("[RenderEntityF] entity id=%d\n",entity->uid);
	//	Log_Printf("[RenderEntityF] entity pos=[%.2f,%.2f,%.2f,%.2f]\n",entity->matrix[12],entity->matrix[13],entity->matrix[14],entity->matrix[15]);
		//matrix_print(entity->matrix);
		
	}
	
	glMultMatrixf(entity->matrix);
	
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, entity->material->shininess);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, entity->material->specularColor);
	SetTextureF(entity->material->textures[TEXTURE_DIFFUSE].textureId);
	
	//Disabling blending for now
	/*
	if (entity->material->hasAlpha )
	{
		if (!renderer.isBlending)
		{
			renderer.isBlending = 1;
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			STATS_AddBlendingSwitch();
		}
	}
	else
	{
		if (renderer.isBlending)
		{
			renderer.isBlending = 0;
			glDisable(GL_BLEND);
			STATS_AddBlendingSwitch();
		}
	}
	*/
	
		
	if (entity->model->memLocation == MD5_MEMLOC_VRAM)
	{

		glBindBuffer(GL_ARRAY_BUFFER, entity->model->vboId);
		
        //This is very likely not 64bits friendly if the GPU copies stuff as it is presented.
        
		//glVertexPointer (3, GL_FLOAT, sizeof(vertex_t), (char *)NULL + VERTEX_T_DELTA_TO_POS);	
		//glNormalPointer(GL_SHORT, sizeof(vertex_t), (char *)NULL + VERTEX_T_DELTA_TO_NORMAL);
		//glTexCoordPointer (2, GL_SHORT, sizeof(vertex_t), (char *)NULL + VERTEX_T_DELTA_TO_TEXT);
        
        glVertexPointer  (3, GL_FLOAT, sizeof(vertex_t), (char *)( (char *)(&entity->model->vertexArray->pos)    - ((char*)&entity->model->vertexArray->pos)) );	
		glNormalPointer  (   GL_SHORT, sizeof(vertex_t), (char *)((char *)(&entity->model->vertexArray->normal) - ((char*)&entity->model->vertexArray->pos))  );
		glTexCoordPointer(2, GL_SHORT, sizeof(vertex_t), (char *)((char *)(&entity->model->vertexArray->text)   - ((char*)&entity->model->vertexArray->pos))  );
        
        
    }
	else 
	{
		glTexCoordPointer (2, GL_SHORT, sizeof(vertex_t), entity->model->vertexArray->text);	
		glVertexPointer (3, GL_FLOAT, sizeof(vertex_t), entity->model->vertexArray->pos);
		glNormalPointer(GL_SHORT, sizeof(vertex_t), entity->model->vertexArray->normal);
	}

    
    
	// The per-frame visibility set (entity->indices) was baked offline for the
	// original 2:3 frustum. On a tall screen the vertical FOV is widened
	// (renderer.vScale > 1), so the trimmed face set leaves black gaps at the
	// top/bottom edges. For an entity the vis system still considers on-screen
	// (the caller skips numIndices==0), draw its FULL mesh so those edge faces
	// are present. Fully off-screen entities are still skipped by the caller, so
	// we never pay for far-away level sections. On a non-stretched view
	// (vScale==1: 2:3 / iPad) the baked set matches the frustum, so keep using it.
	if (entity->usage == ENT_PARTIAL_DRAW && renderer.vScale <= 1.0f && !gRuntimeCullMap)
	{
		glDrawElements (GL_TRIANGLES, entity->numIndices, GL_UNSIGNED_SHORT, entity->indices);
		STATS_AddTriangles(entity->numIndices/3);
	}
	else
	{
		glDrawElements (GL_TRIANGLES, entity->model->numIndices, GL_UNSIGNED_SHORT, entity->model->indices);
		STATS_AddTriangles(entity->model->numIndices/3);
	}

	
	
	
	
	
	
	
	
	//RenderNormalsF(entity->model);
	
	glPopMatrix();
}



void SetTransparencyF(float alpha)
{
	glColor4f(1, 1, 1, alpha);

}


// THE BOSS CAMEO (user's idea, 2026-08-23): during act III's side view, the
// LOFB glides once across the dusk sky as a distant dark silhouette -- the
// player SEES what waits in act IV and cannot touch it. Pure scenery, like
// the crossing stars: drawn between the dome and the city without depth
// writes (buildings pass in front), position a pure function of
// simulationTime (lockstep-safe), dark-tinted via GL_MODULATE. Face-on pose
// (the iconic crab shape), not the profile -- edge-on its 46-unit wingspan
// would vanish into a sliver.
#define CAMEO_T0		50000	// enters (sim ms) -- mid side-view
#define CAMEO_T1		64000	// gone
#define CAMEO_SCALE		4.5f	// model is 45.7 wide -> ~205 units
#define CAMEO_DEPTH		1600.0f	// behind the crossing stars (they fly at 1000)

static void RenderTTBBossCameoF(void)
{
	static entity_t cameo;
	static int      cameoState = 0;	// 0 unloaded, 1 ready, -1 failed
	// fromAboveRotation, the billboard the whole game uses (column-major).
	static const matrix_t cameoFromAbove = {1,0,0,0,  0,0,1,0,  0,-1,0,0,  0,0,0,1};
	matrix_t pose;
	float u, vx, vy;
	int k;

	if (engine.sceneId != 3)
		return;

	// Preload on act III's first rendered frame (a hiccup during the prolog is
	// invisible; one at t=50s mid-flight would not be). The model cache makes
	// re-entry after a replay free.
	if (cameoState == 0)
		cameoState = ENT_LoadEntity(&cameo, "data/models/enemies/lofb.obj.md5mesh", ENT_FULL_DRAW) ? 1 : -1;
	if (cameoState < 0 || cameo.model == NULL)
		return;

	if (fabsf(camera.ttbAngle) < 0.95f * (float)M_PI * 0.5f)
		return;
	if (simulationTime < CAMEO_T0 || simulationTime > CAMEO_T1)
		return;

	// One slow, level crossing right-to-left with a faint bob.
	u  = (simulationTime - CAMEO_T0) / (float)(CAMEO_T1 - CAMEO_T0);
	vx = 380.0f - u * 760.0f;
	vy = 430.0f + 25.0f * sinf(u * 2.0f * (float)M_PI);

	matrix_multiply(cameraInvRot, cameoFromAbove, pose);
	for (k = 0; k < 12; k++)
		pose[k] *= CAMEO_SCALE;	// columns 0..2 (their w stays 0)
	pose[12] = camera.position[0] + camera.right[0]*vx + camera.up[0]*vy + camera.forward[0]*CAMEO_DEPTH;
	pose[13] = camera.position[1] + camera.right[1]*vx + camera.up[1]*vy + camera.forward[1]*CAMEO_DEPTH;
	pose[14] = camera.position[2] + camera.right[2]*vx + camera.up[2]*vy + camera.forward[2]*CAMEO_DEPTH;
	pose[15] = 1;
	for (k = 0; k < 16; k++)
		cameo.matrix[k] = pose[k];

	// Dark silhouette against the dusk: modulate the texture way down.
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glColor4f(0.11f, 0.10f, 0.17f, 1.0f);
	RenderEntityF(&cameo);
	glColor4f(1, 1, 1, 1);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}


// TTB side view: small stars crossing the sky right-to-left with a short
// horizontal trail (user-validated SVG mock, 2026-08-17). Drawn between the
// sky dome and the city, without depth writes, so the buildings cover them --
// they only live in the sky. Purely a function of simulationTime (no state),
// so both lockstep peers render the same frame and replays stay stable.
// Fades in over the last part of the camera swing; invisible upright, so no
// other act is touched.
typedef struct ttbstar_vertex_t
{
	float pos[3];
	uchar color[4];
} ttbstar_vertex_t;

static void RenderTTBStarsF(void)
{
	// 3 parallax layers: y (view-space up), units/ms, phase, trail length,
	// half thickness. Speeds ~ mock: far crosses in ~12s, near in ~5s.
	static const float star[9][5] = {
		//   y     speed   phase  trail  half
		{ 640.0f, 0.045f,    40.0f, 30.0f, 1.2f },
		{ 480.0f, 0.040f,   270.0f, 30.0f, 1.2f },
		{ 350.0f, 0.048f,   130.0f, 30.0f, 1.2f },
		{ 200.0f, 0.042f,   520.0f, 30.0f, 1.2f },
		{ 560.0f, 0.085f,   350.0f, 50.0f, 1.6f },
		{ 300.0f, 0.080f,    80.0f, 50.0f, 1.6f },
		{ 430.0f, 0.090f,   470.0f, 50.0f, 1.6f },
		{ 610.0f, 0.150f,   200.0f, 80.0f, 2.2f },
		{ 250.0f, 0.135f,   580.0f, 80.0f, 2.2f },
	};
	// From +310 to -310 in view-space x: the visible half width at depth 1000
	// is ~240 on a tall phone, so heads are born and die off screen.
	#define TTBSTAR_SPAN  620.0f
	#define TTBSTAR_DEPTH 1000.0f

	ttbstar_vertex_t v[9 * 12];
	int nV = 0;
	float f, fade;
	int i, k;

	f = fabsf(camera.ttbAngle) / ((float)M_PI * 0.5f);
	if (f <= 0.6f)
		return;
	fade = (f - 0.6f) / 0.4f;
	if (fade > 1.0f) fade = 1.0f;

	for (i = 0; i < 9; i++)
	{
		float x = 310.0f - fmodf(star[i][2] + star[i][1] * simulationTime, TTBSTAR_SPAN);
		float y = star[i][0];
		float trail = star[i][3], half = star[i][4];
		// corners in (view-x, view-y): head quad then trail quad; the trail
		// stretches BEHIND the motion (+x side), fading to nothing.
		float qx[12] = { x-2.5f, x-2.5f, x+2.5f,  x+2.5f, x-2.5f, x+2.5f,
						 x,      x,      x+trail, x+trail, x,     x+trail };
		float qy[12] = { y-2.0f, y+2.0f, y+2.0f,  y-2.0f, y-2.0f, y+2.0f,
						 y-half, y+half, y+half,  y-half, y-half, y+half };
		// tri order: (0,1,2)(3,4,5) head, (6,7,8)(9,10,11) trail
		static const int headA[6] = {0,1,2, 2,3,0};
		static const int tail0[6] = {0,1,2, 2,3,0};

		for (k = 0; k < 6; k++)
		{
			ttbstar_vertex_t* o = &v[nV++];
			int c = headA[k];
			float cq[4][2] = { {qx[0],qy[0]},{qx[1],qy[1]},{qx[2],qy[2]},{qx[3],qy[3]} };
			o->pos[0] = camera.position[0] + camera.right[0]*cq[c][0] + camera.up[0]*cq[c][1] + camera.forward[0]*TTBSTAR_DEPTH;
			o->pos[1] = camera.position[1] + camera.right[1]*cq[c][0] + camera.up[1]*cq[c][1] + camera.forward[1]*TTBSTAR_DEPTH;
			o->pos[2] = camera.position[2] + camera.right[2]*cq[c][0] + camera.up[2]*cq[c][1] + camera.forward[2]*TTBSTAR_DEPTH;
			o->color[0] = 255; o->color[1] = 255; o->color[2] = 255;
			o->color[3] = (uchar)(230 * fade);
		}
		for (k = 0; k < 6; k++)
		{
			ttbstar_vertex_t* o = &v[nV++];
			int c = tail0[k];
			float cq[4][2] = { {qx[6],qy[6]},{qx[7],qy[7]},{qx[8],qy[8]},{qx[9],qy[9]} };
			int atTail = (c == 2 || c == 3);	// the +trail corners fade out
			o->pos[0] = camera.position[0] + camera.right[0]*cq[c][0] + camera.up[0]*cq[c][1] + camera.forward[0]*TTBSTAR_DEPTH;
			o->pos[1] = camera.position[1] + camera.right[1]*cq[c][0] + camera.up[1]*cq[c][1] + camera.forward[1]*TTBSTAR_DEPTH;
			o->pos[2] = camera.position[2] + camera.right[2]*cq[c][0] + camera.up[2]*cq[c][1] + camera.forward[2]*TTBSTAR_DEPTH;
			o->color[0] = 225; o->color[1] = 232; o->color[2] = 255;
			o->color[3] = atTail ? 0 : (uchar)(150 * fade);
		}
	}

	// Same state discipline as RenderTexturelessSpritesF, plus additive blend
	// (stars glow over the dome) and an unbound VBO (the dome draw may have
	// left one bound).
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glEnableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(3, GL_FLOAT, sizeof(ttbstar_vertex_t), v[0].pos);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ttbstar_vertex_t), v[0].color);
	glDrawArrays(GL_TRIANGLES, 0, nV);
	STATS_AddTriangles(nV / 3);

	glDisableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
}


void RenderEntitiesF(void)
{
	
	
	
	
	int i;
	entity_t* entity;
	enemy_t* enemy;

	// Live decor culling (boss act -- see gRuntimeCullMap in camera.h).
	frustrum_t	cullFrustrum;
	matrix_t	cullView, cullProj, cullPV;
	vec3_t		cullLookat;
	int			cullDrawn = 0;
	// Diagnostic counter, off unless SHMUP_CULL_DEBUG is set: reports how much
	// decor the live cull keeps. The only way to check the boss act headlessly
	// (a CI Simulator screenshot does not capture the GL layer).
	static int	cullDebug = -1;
	static int	cullLogTick = 0;
	static char	cullIds[160] = "";	// which entities survived the cull
	static int	cullTris = 0;		// and how much geometry that actually is
	static int	decorLuma = -1;		// mean brightness after the city is drawn
	static int	skyLuma = -1;		// ...and before it: equal means the sky hides the city

	//Log_Printf("Starting rendering frame, t=%d.\n",simulationTime);


	
	
	glMatrixMode(GL_PROJECTION);
	gluPerspective(camera.fov, camera.aspect,camera.zNear, camera.zFar, projectionMatrix);
	glLoadMatrixf(projectionMatrix);
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	
	SetupCameraF();

	// Build this frame's world-space frustum once, from the camera the scene is
	// actually using. A few degrees wider than the real view so an entity is
	// admitted just BEFORE it slides on screen (the offline bake used the same
	// margin), otherwise buildings pop in at the edges.
	if (gRuntimeCullMap)
	{
		vectorAdd(camera.position,camera.forward,cullLookat);
		gluLookAt(camera.position, cullLookat, camera.up, cullView);
		gluPerspective(camera.fov + 4, camera.aspect, camera.zNear, camera.zFar, cullProj);
		matrix_multiply(cullProj,cullView,cullPV);
		COLL_GenerateFrustrum(cullPV,cullFrustrum);
	}

	if (light.enabled)
		SetupLightingF();

	
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	
	//traceRenderEntity = 0;
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	// The background entities are the three sky domes, and they ENCLOSE the
	// camera: their lower half sits between the camera and the city. The 2010
	// bake hid them whenever the view pointed down (their faces dropped out of
	// the visible set), but a per-entity frustum test can never reject a volume
	// that contains the viewpoint -- so under live culling they were drawn every
	// frame, unfogged, and painted a near-black night sky OVER the whole city.
	// That was the black act 3. Treat them as a proper skybox instead: no depth
	// writes, so whatever is drawn afterwards (the city) always wins.
	if (gRuntimeCullMap)
		glDepthMask(GL_FALSE);

	// Under live culling, draw ONLY the dome nearest to the camera. The domes
	// overlap along the corridor and are drawn without depth writes, so with
	// all of them submitted the LAST one painted wins per pixel -- and a far
	// dome's near-black rim would wipe the sky the near dome just drew (the
	// "blue, then black" sky of the first TTB side view on device). The baked
	// path never had the problem: the bake dropped far domes' faces.
	{
		int nearestDome = -1;

		if (gRuntimeCullMap && numBackgroundEntities > 1)
		{
			float bestD2 = 0;
			for(i=0; i < numBackgroundEntities; i++)
			{
				// bbox_t is the 8 world-space corners: centroid = their mean.
				float cxd = 0, czd = 0, d2;
				int   c;
				for (c = 0; c < 8; c++)
				{
					cxd += map[i].worldSpacebbox[c][0];
					czd += map[i].worldSpacebbox[c][2];
				}
				cxd = cxd * 0.125f - camera.position[0];
				czd = czd * 0.125f - camera.position[2];
				d2  = cxd*cxd + czd*czd;
				if (nearestDome < 0 || d2 < bestD2)
				{
					bestD2 = d2;
					nearestDome = i;
				}
			}
		}

	for(i=0; i < numBackgroundEntities; i++)
	{
		float savedTx = 0, savedTz = 0;
		entity = &map[i];

		if (gRuntimeCullMap)
		{
			if (nearestDome >= 0 && i != nearestDome)
				continue;

			// A REAL skybox: the dome FOLLOWS the camera on x/z for this draw.
			// The domes are flattened caps ~10500 wide, spaced 20000 apart; a
			// side-view camera spends half its time near a rim, where it looks
			// clean OVER the cap's far bulge -- a silhouette hole between the
			// horizon and the ceiling that no texture can paint (the smoke
			// trace read top=8, the clear colour, with the dome drawn). From
			// the CENTER the cap covers every upward direction continuously.
			// Display-only (the matrix is restored), so lockstep is untouched;
			// and a camera-centred dome always intersects the frustum, so the
			// per-entity test is moot for it.
			savedTx = entity->matrix[12];
			savedTz = entity->matrix[14];
			entity->matrix[12] = camera.position[0];
			entity->matrix[14] = camera.position[2];

			cullDrawn++;
			if (cullDebug == 1)
			{
				char idbuf[16];
				sprintf(idbuf, "%d,", i);
				if (strlen(cullIds) + strlen(idbuf) < sizeof(cullIds))
					strcat(cullIds, idbuf);
				cullTris += entity->model->numIndices / 3;
			}
		}
		else if (entity->numIndices == 0)
			continue;

		RenderEntityF(entity);

		if (gRuntimeCullMap)
		{
			// undo the skybox follow -- the entity table is engine state
			entity->matrix[12] = savedTx;
			entity->matrix[14] = savedTz;
		}
	}
	}	// nearestDome scope

	// The boss cameo then the crossing stars, between the dome and the city:
	// still no depth writes here, so the buildings drawn next cover them --
	// and the stars, drawn after, pass in FRONT of the distant silhouette.
	// The cameo call is unconditional so its PRELOAD runs on act III's first
	// frame; its own gates keep it from drawing outside the side view (where
	// gRuntimeCullMap is necessarily on, the angle being ~90).
	RenderTTBBossCameoF();
	if (gRuntimeCullMap)
		RenderTTBStarsF();

	if (gRuntimeCullMap)
	{
		glDepthMask(GL_TRUE);

		// Sky-only brightness, sampled BEFORE the city is drawn. Compared with
		// the final number below it says whether the city actually reached the
		// screen -- the single-sample probe could not tell city from sky, and
		// read the dome while reporting "the decor renders fine".
		if (cullDebug == 1 && cullLogTick + 1 >= 60)
		{
			uchar spx[16*16*4];
			int   sk, ssum = 0;
			glReadPixels(renderer.glBuffersDimensions[WIDTH]/2 - 8,
						 renderer.glBuffersDimensions[HEIGHT]/2 - 8,
						 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, spx);
			for (sk = 0; sk < 16*16; sk++)
				ssum += spx[sk*4] + spx[sk*4+1] + spx[sk*4+2];
			skyLuma = ssum / (16*16*3);
		}
	}

	if (engine.fogEnabled && (renderer.props & PROP_FOG) == PROP_FOG )
	{
		glEnable(GL_FOG);
		glFogx(GL_FOG_MODE, GL_LINEAR);						// Fog Mode
		glFogfv(GL_FOG_COLOR,renderer.fogColor);			// Set Fog Color
		glFogf(GL_FOG_DENSITY, renderer.fogDensity);		// How Dense Will The Fog Be (not used if GL_FOG_MODE == GL_LINEAR )
		glHint(GL_FOG_HINT, GL_FASTEST);					// Fog Hint Value
		glFogf(GL_FOG_START, renderer.fogStartAt);			// Fog Start Depth
		glFogf(GL_FOG_END, renderer.fogStopAt);				// Fog End Depth
	}
	
	
	
	for(i=numBackgroundEntities; i < num_map_entities; i++)
	{
		entity = &map[i];

		if (gRuntimeCullMap)
		{
			if (COLL_CheckBoxAgainstFrustrum(entity->worldSpacebbox,cullFrustrum) == INT_OUT)
				continue;
			cullDrawn++;
			if (cullDebug == 1)
			{
				char idbuf[16];
				sprintf(idbuf, "%d,", i);
				if (strlen(cullIds) + strlen(idbuf) < sizeof(cullIds))
					strcat(cullIds, idbuf);
				cullTris += entity->model->numIndices / 3;
			}
		}
		else if (entity->numIndices == 0)
			continue;
		else
			cullDrawn++;	// count the baked path too, for the on-screen readout

		RenderEntityF(entity);
	}
	glEnable(GL_CULL_FACE);

	// (The on-device act-3 readout sampled here during rounds 12-14; removed
	// once the U-turn patrol was confirmed. The env-gated CI trace below stays:
	// the smoke test's assertions read it.)

	// Runs for ANY scene when the env is set, not just the live-culled boss act:
	// the only way to judge "act 3 is too dark" is to measure a shipped act that
	// looks right (act 2 flies this very city on its baked rail) and compare.
	if (cullDebug < 0)
		cullDebug = getenv("SHMUP_CULL_DEBUG") ? 1 : 0;
	if (cullDebug || gRuntimeCullMap)
	{
		if (cullDebug && ++cullLogTick >= 60)
		{
			// Is the DECOR actually on screen? Sampled here, after the map loops
			// and before the sprites/HUD, so it measures the city alone. Counting
			// entities and triangles proved not to answer this: a submitted draw
			// can still put nothing on screen. Simulator screenshots do not
			// capture the GL layer, so this number is the only honest witness.
			uchar px[16*16*4];
			int   k, sum = 0;
			int   topLuma;
			int   cx = renderer.glBuffersDimensions[WIDTH] / 2 - 8;
			int   cy = renderer.glBuffersDimensions[HEIGHT] / 2 - 8;
			glReadPixels(cx, cy, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, px);
			for (k = 0; k < 16*16; k++)
				sum += px[k*4] + px[k*4+1] + px[k*4+2];
			decorLuma = sum / (16*16*3);

			// The UPPER quarter of the screen: where the TTB side view's sky
			// band lives (GL y grows upward). Black there = the dome's skirt
			// bug; the dusk dome should read ~30-90.
			glReadPixels(cx, renderer.glBuffersDimensions[HEIGHT] * 4 / 5, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, px);
			sum = 0;
			for (k = 0; k < 16*16; k++)
				sum += px[k*4] + px[k*4+1] + px[k*4+2];
			topLuma = sum / (16*16*3);

			cullLogTick = 0;
			Log_Printf("[cull] scene=%d live=%d t=%d pos=(%.0f,%.0f,%.0f) fwd=(%.2f,%.2f,%.2f) up=(%.2f,%.2f,%.2f) drew %d/%d ids=%s tris=%d sky=%d luma=%d top=%d\n",
					   engine.sceneId, gRuntimeCullMap,
					   simulationTime,
					   camera.position[0], camera.position[1], camera.position[2],
					   camera.forward[0], camera.forward[1], camera.forward[2],
					   camera.up[0], camera.up[1], camera.up[2],
					   cullDrawn, num_map_entities, cullIds, cullTris, skyLuma, decorLuma, topLuma);
			// Which entities survived matters more than how many: ids 0..2 are the
			// sky domes (numBackgroundEntities), so "only sky" means the camera is
			// pointed away from the city -- a black screen with a valid cull.
		}
		if (cullDebug)
		{
			cullIds[0] = '\0';
			cullTris = 0;
		}
	}
	
	
	
	
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	for (i=0 ; i < numPlayers; i++) 
	{
		//Log_Printf("player[%d].shouldDraw=%d\n",i,players[i].shouldDraw);
		if (players[i].shouldDraw)
			RenderEntityF(&players[i].entity);
	}
	
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	
	//glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	//traceRenderEntity=1;
	enemy = ENE_GetFirstEnemy();
	while (enemy != NULL) 
	{
		entity = &enemy->entity;
		
		
		
		if (enemy->shouldFlicker)
		{
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			RenderEntityF(entity);
			enemy->shouldFlicker = 0;
			glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		}
		else
		{
			// The Devil's ghost costume (and any future translucent enemy):
			// alpha < 1 turns blending on for this one draw. GL_MODULATE is
			// already the enemy pass's texenv, so the color's alpha reaches
			// the blender as-is.
			int ghost = entity->color[A] < 0.999f;
			if (ghost)
			{
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			glColor4f(entity->color[R], entity->color[G], entity->color[B], entity->color[A]);
			RenderEntityF(entity);
			if (ghost)
				glDisable(GL_BLEND);
		}

		
		enemy = enemy->next;
	} 
	glColor4f(1, 1, 1, 1);
}

void RenderStringF(xf_colorless_sprite_t* vertices,ushort* indices, uint numIndices)
{
	glVertexPointer (2, GL_SHORT, sizeof(xf_colorless_sprite_t), vertices->pos);	
	glTexCoordPointer (2, GL_SHORT,sizeof(xf_colorless_sprite_t), vertices->text);	
	glDrawElements (GL_TRIANGLES, numIndices, GL_UNSIGNED_SHORT,indices);
	STATS_AddTriangles(numIndices/3);
}

void GetColorBufferF(uchar* data)
{
	glReadPixels(0,0,renderer.glBuffersDimensions[WIDTH],renderer.glBuffersDimensions[HEIGHT],GL_RGBA, GL_UNSIGNED_BYTE,data);
}

void UpLoadEntityToGPUF(entity_t* entity)
{
	md5_mesh_t* mesh;

	if (entity == NULL || entity->model == NULL)
	{
		Log_Printf("Entity was NULL: No vertices to upload.\n");
		return;
	}
	
	if (entity->model->memLocation == MD5_MEMLOC_VRAM)
		return;		
	
	
	mesh = entity->model;
		
	glGenBuffers(1, &mesh->vboId);
	glBindBuffer(GL_ARRAY_BUFFER, mesh->vboId);
	glBufferData(GL_ARRAY_BUFFER, mesh->numVertices * sizeof(vertex_t), mesh->vertexArray, GL_STATIC_DRAW);
	
    
//#define GENERATE_VIDEO	
#ifndef GENERATE_VIDEO	
	free(mesh->vertexArray);
	mesh->vertexArray = 0;
#else
	Log_Printf("Warning, not freeing mesh after GPU upload.\n");
#endif
	
	mesh->memLocation = MD5_MEMLOC_VRAM;
	
	SCR_CheckErrors("UpLoadVerticesToGPUF","");
	
}

void RenderPlayersBulletsF(void)
{
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);	
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);	
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	
	SetTextureF(bulletConfig.bulletTexture.textureId);
	
	//Player bullets
	glVertexPointer(  2, GL_SHORT,  sizeof(xf_colorless_sprite_t), pBulletVertices->pos);
	glTexCoordPointer(2, GL_SHORT,  sizeof(xf_colorless_sprite_t), pBulletVertices->text);			
	glDrawElements (GL_TRIANGLES, numPBulletsIndices, GL_UNSIGNED_SHORT,bulletIndices);
	STATS_AddTriangles(numPBulletsIndices/3);
	
	//Also render enemy bullets
//	glColor4f(1, 1, 1, 1);
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);	
	glVertexPointer(  2, GL_SHORT,  sizeof(xf_colorless_sprite_t), partLib.ss_vertices[0].pos);
	glTexCoordPointer(2, GL_SHORT,  sizeof(xf_colorless_sprite_t), partLib.ss_vertices[0].text);			
	glDrawElements (GL_TRIANGLES, partLib.num_indices, GL_UNSIGNED_SHORT,partLib.indices);
	STATS_AddTriangles(partLib.num_indices/3);
}

void RenderFXSpritesF(void)
{
	int i,j;
	
	/*
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
	SetTextureF(smokeTexture.textureId);
	if (numSmokeIndices != 0)
	{
		glVertexPointer(  2, GL_SHORT,  sizeof(xf_colorless_sprite_t), smokeVertices->pos);
		glTexCoordPointer(2, GL_SHORT,  sizeof(xf_colorless_sprite_t), smokeVertices->text);
		//glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_sprite_t), smokeVertices->color);
		glDrawElements (GL_TRIANGLES, numSmokeIndices, GL_UNSIGNED_SHORT,smokeIndices);
		STATS_AddTriangles(numSmokeIndices/3);
	}
	
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	 */
	
	
	
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	
	SetTextureF(smokeTexture.textureId);
	if (numSmokeIndices != 0)
	{
		glVertexPointer(  2, GL_SHORT,  sizeof(xf_colorless_sprite_t), smokeVertices->pos);
		glTexCoordPointer(2, GL_SHORT,  sizeof(xf_colorless_sprite_t), smokeVertices->text);
		//glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_sprite_t), smokeVertices->color);
		glDrawElements (GL_TRIANGLES, numSmokeIndices, GL_UNSIGNED_SHORT,smokeIndices);
		STATS_AddTriangles(numSmokeIndices/3);
	}
	
	
	
	SetTextureF(ghostTexture.textureId);
	for(i=0 ; i <numPlayers ; i++)
	{
		/*
		if (i==0)
			glColor4f(0.8f, 0.8f, 1.0f, 0.9f);
		else {
			glColor4f(1.0f, 0.4f, 0.4f, 0.9f);
		}
*/
		
		
		for (j=0; j< GHOSTS_NUM; j++) 
		{
			if (players[i].ghosts[j].timeCounter >= GHOST_TTL_MS)
				continue;
			
			
			//vertices = &players[i].ghosts[j].wayPoints[players[i].ghosts[j].startVertexArray];
			glVertexPointer(  2, GL_SHORT,  sizeof(xf_colorless_sprite_t), players[i].ghosts[j].wayPoints[players[i].ghosts[j].startVertexArray].pos);
			glTexCoordPointer(2, GL_SHORT,  sizeof(xf_colorless_sprite_t), players[i].ghosts[j].wayPoints[players[i].ghosts[j].startVertexArray].text);
			//glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_sprite_t), vertices->color);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, players[i].ghosts[j].lengthVertexArray);
			STATS_AddTriangles((players[i].ghosts[j].lengthVertexArray/2));
		}
	}

	
	

	glEnableClientState(GL_COLOR_ARRAY);
	
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	
	//Render all particules
	if (numParticulesIndices != 0)
	{
		
		SetTextureF(bulletConfig.bulletTexture.textureId);
		glVertexPointer(  2, GL_SHORT,  sizeof(xf_sprite_t), particuleVertices->pos);
		glTexCoordPointer(2, GL_SHORT,  sizeof(xf_sprite_t), particuleVertices->text);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_sprite_t), particuleVertices->color);
		glDrawElements (GL_TRIANGLES, numParticulesIndices, GL_UNSIGNED_SHORT,particuleIndices);
		STATS_AddTriangles(numParticulesIndices/3);
	}
	
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	
	
	
	//Render all explosions 
	if (numExplosionIndices != 0)
	{
		SetTextureF(explosionTexture.textureId);
		glVertexPointer(  2, GL_SHORT,  sizeof(xf_sprite_t), explosionVertices->pos);
		glTexCoordPointer(2, GL_SHORT,  sizeof(xf_sprite_t), explosionVertices->text);
		//Log_Printf("REMOVE COLOR INDICES EXPLOSIONS RenderFXSpritesF !!!! \n");
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_sprite_t), explosionVertices->color);
		glDrawElements (GL_TRIANGLES, numExplosionIndices, GL_UNSIGNED_SHORT,explosionIndices);
		STATS_AddTriangles(numExplosionIndices/3);
	}
	
	
	//Render enemy FXs
	SetTextureF(bulletConfig.bulletTexture.textureId);
	glVertexPointer(  2, GL_SHORT,  sizeof(xf_sprite_t), enFxLib.ss_vertices[0].pos);
	glTexCoordPointer(2, GL_SHORT,  sizeof(xf_sprite_t), enFxLib.ss_vertices[0].text);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_sprite_t), enFxLib.ss_vertices[0].color);
	glDrawElements (GL_TRIANGLES, enFxLib.num_indices, GL_UNSIGNED_SHORT,enFxLib.indices);
	STATS_AddTriangles(enFxLib.num_indices/3);
	//Log_Printf("enFxLib.num_indices=%d\n",enFxLib.num_indices);
	
	
	
#ifdef RENDER_COLL_BOXEX	
	RenderCollisionBoxes();
#endif
	
	//glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	
	
	
	glDisableClientState(GL_COLOR_ARRAY);

	
	
}

void DrawControlsF(void)
{
	if (engine.controlMode == CONTROL_MODE_SWIP)
		return;
	
	glEnableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisable(GL_TEXTURE_2D);
	
	
	//glBindBuffer(GL_ARRAY_BUFFER, controlVBOId);

	
	glVertexPointer (2, GL_SHORT, sizeof(xf_textureless_sprite_t),controlVertices[0].pos);// (char *)NULL + 0);	
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_textureless_sprite_t),controlVertices[0].color);// (char *)NULL + 4);
	glDrawElements (GL_TRIANGLE_STRIP, controlNumIndices, GL_UNSIGNED_SHORT,controlIndices);
	STATS_AddTriangles(controlNumIndices/2);
		
	glDisableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	
	
	
}


void FreeGPUTextureF(texture_t* texture)
{
	glDeleteTextures(1, &texture->textureId);
	texture->textureId = 0;
}


void FreeGPUBufferF(uint bufferId)
{
	glDeleteBuffers(1,&bufferId);
	
}

uint UploadVerticesToGPUF(void* vertices, uint mem_size)
{
	uint vboId;
	
	glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBufferData(GL_ARRAY_BUFFER, mem_size, vertices, GL_STATIC_DRAW);
	
	return vboId;
}

void StartCleanFrameF(void)
{
	//glClear(GL_COLOR_BUFFER_BIT );
	glEnable(GL_TEXTURE_2D);
	glMatrixMode(GL_TEXTURE);
	glLoadMatrixf(textureMatrix);
	
	//This is disabled in set3D
	//glEnable ( GL_COLOR_MATERIAL ) ;
}

void RenderColorlessSpritesF(xf_colorless_sprite_t* vertices, ushort numIndices, ushort* indices)
{
	glVertexPointer(  2, GL_SHORT,  sizeof(xf_colorless_sprite_t), vertices->pos);
	glTexCoordPointer(2, GL_SHORT,  sizeof(xf_colorless_sprite_t), vertices->text);
	glDrawElements (GL_TRIANGLES, numIndices, GL_UNSIGNED_SHORT,indices);
	STATS_AddTriangles(numIndices/2);
}




void RenderTexturelessSpritesF(xf_textureless_sprite_t* vertices, ushort numIndices, ushort* indices)
{
	glDisable(GL_TEXTURE_2D);
	glEnableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(  2, GL_SHORT,  sizeof(xf_textureless_sprite_t), vertices->pos);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_textureless_sprite_t), vertices->color);
	glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_SHORT, indices);
	STATS_AddTriangles(numIndices/3);

	glDisableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnable(GL_TEXTURE_2D);
}


void FadeScreenF(float alpha)
{

	fadeVertices[0].color[A] = alpha * 255;
	fadeVertices[1].color[A] = alpha * 255;
	fadeVertices[2].color[A] = alpha * 255;
	fadeVertices[3].color[A] = alpha * 255;
	
	
	glDisable(GL_TEXTURE_2D);
	glEnableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	
	glVertexPointer(  2, GL_SHORT,  sizeof(xf_textureless_sprite_t), fadeVertices[0].pos);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(xf_textureless_sprite_t), fadeVertices[0].color);
	glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT,fadeIndices);
	STATS_AddTriangles(6/2);
	
	
	glDisableClientState(GL_COLOR_ARRAY);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnable(GL_TEXTURE_2D);
	 
	
	
}

void SetMaterialTextureBlendingF(char modulate)
{
	if (modulate)
		glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	else {
		glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}

}

int IsTextureCompressionSupportedF(int type){
    return supportedCompressionFormatF & type;
}

void RefreshViewPortF()
{
    glViewport(renderer.viewPortDimensions[VP_X],
			   renderer.viewPortDimensions[VP_Y], 
			   renderer.viewPortDimensions[VP_WIDTH], 
			   renderer.viewPortDimensions[VP_HEIGHT]);
}

void initFixedRenderer(renderer_t* renderer)
{
	GLenum err;
	char *extensionsList ;
    
	//Log_Printf("[initFixedRenderer] has a nnnasty hack");
	
	renderer->type = GL_11_RENDERER ;
	
	//renderer->supportBumpMapping = 0;
	renderer->props = 0;
	
	
	
	renderer->Set3D = Set3DF;
	renderer->StopRendition = StopRenditionF;
	renderer->SetTexture = SetTextureF;
	renderer->RenderEntities = RenderEntitiesF;
	renderer->UpLoadTextureToGpu = UpLoadTextureToGPUF;
	renderer->UpLoadEntityToGPU = UpLoadEntityToGPUF;
	renderer->Set2D = Set2DF;
	renderer->RenderPlayersBullets = RenderPlayersBulletsF ;
	renderer->RenderString = RenderStringF;
	renderer->GetColorBuffer = GetColorBufferF;
	
	renderer->RenderFXSprites = RenderFXSpritesF;
	renderer->DrawControls = DrawControlsF;
	
	renderer->FreeGPUTexture = FreeGPUTextureF;
	renderer->FreeGPUBuffer = FreeGPUBufferF;
	
	renderer->UploadVerticesToGPU = UploadVerticesToGPUF;
	renderer->StartCleanFrame = StartCleanFrameF;
	renderer->RenderColorlessSprites = RenderColorlessSpritesF;
	renderer->RenderTexturelessSprites = RenderTexturelessSpritesF;
	renderer->FadeScreen = FadeScreenF;
	renderer->SetMaterialTextureBlending = SetMaterialTextureBlendingF;
	renderer->SetTransparency = SetTransparencyF;
	renderer->IsTextureCompressionSupported = IsTextureCompressionSupportedF;
    renderer->RefreshViewPort = RefreshViewPortF;
	
	glViewport(renderer->viewPortDimensions[VP_X],
			   renderer->viewPortDimensions[VP_Y], 
			   renderer->viewPortDimensions[VP_WIDTH], 
			   renderer->viewPortDimensions[VP_HEIGHT]);
	
	
	
	glEnable(GL_TEXTURE_2D);

		
	
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	
	
	glEnableClientState (GL_VERTEX_ARRAY);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	
	
	
	glClearColor(0, 0, 0,1.0f);
	glColor4f(1.0f, 1.0f, 1.0f,1.0f);
	
	glMatrixMode(GL_TEXTURE);
	glLoadMatrixf(textureMatrix);
		
    
    
    //We need to check what texture compression method is supported.
    extensionsList = (char *) glGetString(GL_EXTENSIONS);
    if (strstr(extensionsList,"GL_IMG_texture_compression_pvrtc"))
        supportedCompressionFormatF |= TEXTURE_FORMAT_PVRTC ;
        
        
    
    
	err = glGetError();
	if (err != GL_NO_ERROR)
		Log_Printf("Error initing 1.1: glError: 0x%04X", err);
}
