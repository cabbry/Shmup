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
 *  player.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-02-02.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#include "player.h"
#include <stdlib.h>	// getenv: the SHMUP_AUTOFIRE CI probe
#include "renderer.h"
#include "camera.h"
#include "timer.h"
#include "limits.h"
#include "enemy.h"
#include "lofb.h"
#include <math.h>
#include "sounds.h"
#include "menu.h"
#include "netchannel.h"
#include "dEngine.h"
#include "event.h"
#include "enemy_particules.h"
#include "renderer.h"
#include "native_services.h"
#include "titles.h"

//WARNING...if THIS IS CHANGED
unsigned char numPlayerRespawn[] = {PLAYER_NUM_LIVES,3,1};

// Solo ship selection (Others -> Ship). Index into gShipPaths, applied in P_LoadPlayer
// for single-player only; multiplayer keeps the level's distinct model0/model1 so the
// two players stay recognizable. Index 0 = the default ship (same as the config).
int gShipChoice = 0;
const char* gShipPaths[NUM_SHIP_CHOICES] = {
	"data/models/players/p1.obj.md5mesh",	// choice 0 falls back to the level's model anyway
	"data/models/players/p2.obj.md5mesh",	// choice 1 = Ship 2
	// Choice 2 = Ship 3, Fabien's third hull, resurrected in v2. It ships as its
	// OWN mesh file: hpp.obj.md5mesh is the model the title screen orbits, and
	// rescaling it in place (x2.8, to match p1/p2's wingspan) blew the intro up
	// until the camera flew through the hull. Same art, same "hpp" material,
	// two scales, two files.
	"data/models/players/hpp_ship.obj.md5mesh",
	"data/models/players/p1.obj.md5mesh",	// choice 3 = GHOST: the classic hull under a
											// spectral translucent veil (see gShipTints)
};

// v2: per-ship hull tint, applied wherever a ship model is (re)loaded. Ships
// 1-3 fly opaque; the GHOST is the player-side twin of the ghost Devil -- the
// same spectral blue-white, alpha'd. 0.55: clearly translucent, still
// trackable in a bullet storm. The renderer blends any player whose entity
// alpha sits below 1 (same per-draw mechanism as the enemy pass).
static const float gShipTints[NUM_SHIP_CHOICES][4] = {
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 0.85f, 0.95f, 1.0f, 0.55f },
};

static void P_ApplyShipTint(int playerId, int shipChoice)
{
	const float* t = gShipTints[(shipChoice > 0 && shipChoice < NUM_SHIP_CHOICES) ? shipChoice : 0];
	players[playerId].entity.color[0] = t[0];
	players[playerId].entity.color[1] = t[1];
	players[playerId].entity.color[2] = t[2];
	players[playerId].entity.color[3] = t[3];
}

// Diagnostic: basename of the ship model actually loaded for the solo player, shown
// in-game so we can confirm the Custom ship choice is applied (vs. the art just looking alike).
char gLoadedShipDebug[64] = "";

// Solo bullet colour (Others -> Ship). Selects the colour COLUMN of the bullet atlas
// (spritesBullets.png); the player index already does this in multiplayer, so in solo
// we substitute the chosen column. Applied in P_PrepareBulletSprites.
int gBulletColor = 0;

// Multiplayer per-player loadout, synced during the match handshake (netchannel.c
// NET_StorePeerLoadout): each slot holds that player's Custom ship / bullet-colour
// column. Defaults = the classic multiplayer look (P1 ship + red, P2 ship + blue).
// If both players picked the same colour, player two's is shifted deterministically
// on both ends so the two players' shots stay distinguishable.
int gMPShipChoice[MAX_NUM_PLAYERS]  = { 0, 1, 2, 3 };	// default: seat index
int gMPBulletColor[MAX_NUM_PLAYERS] = { 0, 1, 2, 3 };

// Set when the act-3 boss dies: the run is scored at the killing blow, so all
// score gains stop during the victory lap. Cleared on the next scene load.
int gScoreLocked = 0;

#define SHOW_POINTER_DURATION 5000

uchar numPlayers;
uchar controlledPlayer;
player_t players[MAX_NUM_PLAYERS];

diverSpriteLib_t diverSpriteLib;

uchar entitiesAttachedToCamera=0;
static uchar playersWereAttached=0;	// prolog (never attached) vs outro (detached after flight)

player_bullet_config_t bulletConfig;


//Variable storing players bullet AND firing flash (in front of the player ship)
// Vertices needed is number_of_players * number_of_bullets * 4 +  number_of_players * 4 = number_of_players * (number_of_bullets*4 +4)
// Indices needed is number_of_players * number_of_bullets * 6 + number_of_players * 6 =   number_of_players * (number_of_bullets * 6 + 6)
unsigned short bulletIndices[(MAX_PLAYER_BULLETS * 6 + 12)*MAX_NUM_PLAYERS];
xf_colorless_sprite_t pBulletVertices[(MAX_PLAYER_BULLETS*4+8)*MAX_NUM_PLAYERS];
int numPBulletsIndices=0;





texture_t ghostTexture;

#define POINTER_TEXT_PATH "data/titles/divers.png"
texture_t pointersTexture;

void P_AttachToCamera(matrix_t globalMatrix)
{
	
	
	vec4_t ws_playerPos;
	vec4_t ss_playerPos;
	int i;
	entity_t* playerEntity;
	plan_t cameraFront;
	player_t* player;
			   
	
	//Need to check the screen space position
	
	for( i =0 ; i <numPlayers ; i++)
	{
		player = &players[i] ;
		playerEntity = &player->entity;
		
		ws_playerPos[X] = playerEntity->matrix[12];
		ws_playerPos[Y] = playerEntity->matrix[13];
		ws_playerPos[Z] = playerEntity->matrix[14];
		ws_playerPos[W] = 1;
		
		matrix_multiplyVertexByMatrix(ws_playerPos,globalMatrix,ss_playerPos);
		
		ss_playerPos[X] /= ss_playerPos[W] ;
		ss_playerPos[Y] /= ss_playerPos[W] ;
		
		
		player->ss_position[X] = ss_playerPos[X] ;
		player->ss_position[Y] = ss_playerPos[Y] ;
		
		//ViewPort
		//player->ss_position[X] *= renderWidth;
		//player->ss_position[Y] *= renderHeight;
		
		//printf("[P_AttachToCamera] player[%d] ssPos[X]=%.2f, ssPos[Y]=%.2f\n",i,player->ss_position[X],player->ss_position[Y]);
		
		players[i].showPointer = SHOW_POINTER_DURATION ;
	}
	
	//Also need to generate enemies ss_coordinates
	
	
	
	
	//Need to init distanceFromCamera, pixelWidthAtDistance and pixelHeightAtDistance
	cameraFront.normal[X] = camera.forward[X];
	cameraFront.normal[Y] = camera.forward[Y];
	cameraFront.normal[Z] = camera.forward[Z];
	cameraFront.d = - DotProduct(cameraFront.normal,camera.position);
					   
	distanceZFromCamera = DotProduct(cameraFront.normal,ws_playerPos) + cameraFront.d;

	distanceZFromCamera = fabsf(distanceZFromCamera);
//	if (distanceZFromCamera < 0)
//		distanceZFromCamera *= -1;
	
	heightAtDistance = tanf(camera.fov * DEG_TO_RAD / 2.0) ;
	heightAtDistance *= distanceZFromCamera ;
	//pixelHeightAtDistance /=renderHeight; 
	widthAtDistance = heightAtDistance * camera.aspect;
	
	//printf("distanceZFromCamera=%.2f.\n",distanceZFromCamera);
	//printf("pixelHeightAtDistance=%.2f.\n",heightAtDistance);
	//printf("pixelWidthAtDistance=%.2f.\n",widthAtDistance);

	entitiesAttachedToCamera= 1;
	playersWereAttached = 1;
}

void P_DetachToCamera(void)
{
	int i;
	
	entitiesAttachedToCamera= 0;
	
	for (i=0; i < numPlayers; i++) {
		players[i].autopilot.enabled = 0;
		players[i].invulnerableFor = 0;
		players[i].shouldDraw = 1;
	}
}

void P_ResetPlayer(int i)
{
	ghost_t* ghost;
	int j,k;
	player_t* player;
	
	player = &players[i];
	
	
	player->invulFlickering = 0;
	player->invulnerableFor = 0;
	player->shouldDraw = 1;
	
	
	player->nextBulletFireTime = 0 ;
	player->nextGhostFireTime = 0;
	
	player->lastBulletType = i;
	
	player->firingUpTo = 0;
	
	player->showPointer = 0;
	player->autopilot.enabled = 0;
	player->deathPending = 0;		// v2.0.9: no ruling outstanding on a fresh hull
	player->deathPendingSince = 0;
	
	for (j=0; j < MAX_PLAYER_BULLETS; j++)
	{
		player->bullets[j].energy = BULLET_DEFAULT_ENERGY;
		player->bullets[j].expirationTime = 0;
		// Park the quad offscreen too: at a timer reset (multiplayer match start)
		// simulationTime briefly equals the 0 expiration time, which used to flash
		// the stale bullet pool at its old positions for a frame.
		player->bullets[j].ss_boudaries[UP]    = 4*SS_H;
		player->bullets[j].ss_boudaries[DOWN]  = 4*SS_H;
		player->bullets[j].ss_boudaries[LEFT]  = 4*SS_W;
		player->bullets[j].ss_boudaries[RIGHT] = 4*SS_W;
	}
	
	
	for (k=0; k < GHOSTS_NUM; k++) 
	{
		ghost = &player->ghosts[k];
		ghost->timeCounter = GHOST_TTL_MS+1;
	}
	
	
	//Reset stats
	engine.playerStats.bulletsFired[i] = 0;
	engine.playerStats.bulletsHit[i] = 0;
	engine.playerStats.enemyDestroyed[i] = 0;
}

void PL_ResetPlayersScore(void)
{
	int i;

	for (i=0; i < MAX_NUM_PLAYERS; i++)
	{
		players[i].score = 0;
	}
}

// v2 P3: ONE TEAM SCORE in multiplayer (user decision). Internally every player
// still accumulates his own counter (lockstep-deterministic, no new state on
// the wire); the HUD, the game-over card and the Game Center upload show the
// SUM. Solo shows the player's own score, unchanged.
uint P_GetDisplayScore(void)
{
	if (engine.mode == DE_MODE_MULTIPLAYER && numPlayers >= 2)
	{
		uint total = 0;
		int i;
		for (i = 0; i < numPlayers && i < MAX_NUM_PLAYERS; i++)
			total += players[i].score;
		return total;
	}
	return players[controlledPlayer].score;
}

// v2 P3: the formation, generalized. Seats 0-1 fly the classic front pair
// (x = -/+0.5, the exact 2010 formula); seats 2-3 tuck in BEHIND and BETWEEN
// them (x = -/+0.25, lower on screen) -- two staggered rows, en quinconce.
float P_FormationX(int playerId)
{
	float col = (playerId & 1) ? 0.5f : -0.5f;	// 0.5f*(playerId-0.5f)*2.f, bit-exact at 0/1
	return (playerId < 2) ? col : col * 0.5f;
}

float P_FormationY(int playerId)
{
	return (playerId < 2) ? -0.0f : -0.35f;
}

// v2 P3: aiming helper for enemies -- the nearest ship still in play (parked
// or RIP'd ships have shouldDraw 0). Deterministic in lockstep: every peer
// evaluates the same positions at the same tick. Falls back to player 0, the
// 2010 behavior, so solo is bit-identical.
int P_NearestAlivePlayer(float ssX, float ssY)
{
	int i, best = 0;
	float bestD = 1e30f;
	for (i = 0; i < numPlayers && i < MAX_NUM_PLAYERS; i++)
	{
		float dx, dy, d;
		if (!players[i].shouldDraw)
			continue;
		dx = players[i].ss_position[X] - ssX;
		dy = players[i].ss_position[Y] - ssY;
		d = dx*dx + dy*dy;
		if (d < bestD) { bestD = d; best = i; }
	}
	return best;
}

void P_LoadPlayer(int playerIdToLoad)
{
	
	entity_t*	currentEntity ;
	player_t* player;
	
	
	

	player = &players[playerIdToLoad];
	player->playerId = playerIdToLoad;
	currentEntity = &players[playerIdToLoad].entity ;
	currentEntity->model = (md5_mesh_t*)calloc(1,sizeof(md5_mesh_t)) ;

	// Solo ship selection: load the chosen ship in single-player; multiplayer keeps the
	// level's distinct model0/model1 so the two players stay recognizable.
	{
		const char* modelToLoad = players[playerIdToLoad].modelPath;
		if (engine.mode == DE_MODE_SINGLEPLAYER && gShipChoice > 0 && gShipChoice < NUM_SHIP_CHOICES)
			modelToLoad = gShipPaths[gShipChoice];
		ENT_LoadEntity(currentEntity, modelToLoad, ENT_FULL_DRAW);
		P_ApplyShipTint(playerIdToLoad,
			(engine.mode == DE_MODE_SINGLEPLAYER) ? gShipChoice : 0);

		// Diagnostic: record the basename of what player 0 (the solo ship) actually loaded.
		if (playerIdToLoad == 0)
		{
			const char* base = modelToLoad;
			const char* s;
			for (s = modelToLoad; *s; s++) if (*s == '/') base = s + 1;
			snprintf(gLoadedShipDebug, sizeof(gLoadedShipDebug), "SHIP c%d %s", gShipChoice, base);
		}
	}
	

	currentEntity->model->memStatic = 1;
	currentEntity->material->textures[TEXTURE_DIFFUSE].memStatic= 1;
	currentEntity->material->textures[TEXTURE_BUMP].memStatic= 1;
	currentEntity->material->textures[TEXTURE_SPECULAR].memStatic= 1;
	
	//We need to mark everything in this entity as DO_NOT_FREE
	P_ResetPlayer(playerIdToLoad);
}

// Re-point the player entities' models to the chosen ships. Called on each scene
// load, AFTER the level config has set modelPath. Solo: player 0 gets gShipChoice.
// Multiplayer: EACH player gets his own Custom choice (gMPShipChoice, exchanged
// during the handshake in netchannel.c). Cheap and leak-free: ENT_LoadEntity caches
// meshes by name, so switching just swaps the entity's model pointer.
void P_ReloadShip(void)
{
	int i;
	int last = (engine.mode == DE_MODE_MULTIPLAYER) ? numPlayers-1 : 0;	// v2 P0: was a literal 1

	for (i = 0; i <= last; i++)
	{
		const char* modelToLoad = players[i].modelPath;
		int tintChoice = 0;

		if (engine.mode == DE_MODE_MULTIPLAYER)
		{
			// Any valid choice applies (0 = the P1 ship, a real pick).
			if (gMPShipChoice[i] >= 0 && gMPShipChoice[i] < NUM_SHIP_CHOICES)
			{
				modelToLoad = gShipPaths[gMPShipChoice[i]];
				tintChoice  = gMPShipChoice[i];
			}
		}
		else if (gShipChoice > 0 && gShipChoice < NUM_SHIP_CHOICES)
		{
			modelToLoad = gShipPaths[gShipChoice];
			tintChoice  = gShipChoice;
		}

		if (!modelToLoad || !modelToLoad[0])
			continue;

		ENT_LoadEntity(&players[i].entity, modelToLoad, ENT_FULL_DRAW);
		P_ApplyShipTint(i, tintChoice);
	}
}



void P_ResetPlayers(void)
{
	int i;
	
	for (i=0; i < MAX_NUM_PLAYERS ; i++)
		P_ResetPlayer(i);

	// LAN co-op "second chance" (user design, formalizing a loved accident):
	// the scene reset already resurrects a dead ship at the next act -- but
	// with the shared life pool dry, the FIRST death of the new act ended
	// the match for both. Gift ONE life to the pool at level entry when it
	// is empty: the revived duo restarts cleanly, and duos still holding
	// lives are untouched. STRICTLY == 0 (code review): the pool sits at 0
	// while the match is alive-but-dry, and at -1/-1 once GAME OVER was
	// declared (score uploaded, menu queued) -- a <= would resurrect a LOST
	// match into a zombie run whenever a scene load races the game-over
	// events (double death within 5s of the epilog, or the resume path).
	// Deterministic: both lockstep peers run this same reset at scene load.
	// Solo is never touched.
	if (engine.mode == DE_MODE_MULTIPLAYER && numPlayers >= 2)
	{
		int i2, poolDry = 1;
		for (i2 = 0; i2 < numPlayers && i2 < MAX_NUM_PLAYERS; i2++)
			if (players[i2].respawnCounter != 0)
				poolDry = 0;
		if (poolDry)
			for (i2 = 0; i2 < numPlayers && i2 < MAX_NUM_PLAYERS; i2++)
				players[i2].respawnCounter = 1;	// v2 P3: same rule, N-way mirror
	}

	entitiesAttachedToCamera = 0;
	playersWereAttached = 0;	// the next detached stretch is a PROLOG again
	engine.playerStats.numEnemies = 0;
}





#define PLAYER_LIVE_COUNT_HEIGHT 0.12
#define PLAYER_LIVE_COUNT_WIDTH -0.12
#define PLAYER_LIVE_COUNT_SPACING -0.05
#define PLAYER_LIVE_COUNT_START_X  0.96f

// v2: where the single multiplayer life icon's RIGHT edge sits, in SS_W units.
// Left of the classic row's 0.96 so the "x12" counter fits beside it: the icon
// is 0.12 wide, the number is three glyphs of (size * SS_W / 40) each, and the
// pair still has to end inside the screen.
#define MP_LIVES_ICON_RIGHT_X      0.72f	// nearer the edge (user feedback); "x12" still ends 13px inside
#define MP_LIVES_FONT_SIZE         2.2f
#define PLAYER_LIVE_COUNT_START_Y  1.48f


#define PLAYER_LIVE_COUNT_TEXT_START_U (0 / 256*SHRT_MAX)
#define PLAYER_LIVE_COUNT_TEXT_START_V (88  / 128.0f*SHRT_MAX)
#define PLAYER_LIVE_COUNT_TEXT_WIDTH (48/256.0f*SHRT_MAX)
#define PLAYER_LIVE_COUNT_TEXT_HEIGHT (40/128.0f*SHRT_MAX)


#define SCORE_POS_X -300
#define SCORE_POS_Y 460
#define SCORE_FONT_SIZE 2.7f
#define SCORE_FORMAT "SCORE:%7u"
void P_InitPlayers(void)
{
	int j;
    int numBulletSpriteVertices;
	
	numPlayers = 1;
	controlledPlayer = 0;

	{
		int i;
		for (i = 0; i < MAX_NUM_PLAYERS; i++)
			P_LoadPlayer(i);
	}
	
			
	
	
	
	//Loading bulletSprites
	TEX_MakeStaticAvailable(&bulletConfig.bulletTexture);
	
	TEX_MakeStaticAvailable(&ghostTexture);
	
	
//	pointersTexture.path = calloc(sizeof(char), strlen(POINTER_TEXT_PATH)+1);
	strcpy(pointersTexture.path,POINTER_TEXT_PATH);
	TEX_MakeStaticAvailable(&pointersTexture);
	
	bulletConfig.distPerLifepsan = SS_H*2  ; 	
	
	
	bulletConfig.halfHeight = SS_H*2 * bulletConfig.heightRatio / 2 ;
	bulletConfig.halfWidth = bulletConfig.halfHeight * bulletConfig.widthRatio ;

	bulletConfig.ss_deltaX = SS_W * bulletConfig.screenSpaceXDeltaRatio;
	bulletConfig.ss_deltaY = SS_H * bulletConfig.screenSpaceYDeltaRatio;
	
	bulletConfig.flashScreenSpaceXDelta = 2*SS_W * bulletConfig.flashScreenSpaceXDeltaRatio;
	bulletConfig.flashScreenSpaceYDelta = 2*SS_H* bulletConfig.flashScreenSpaceYDeltaRatio;
	
	bulletConfig.msBetweenBullets = (2*SS_H*bulletConfig.heightRatio) * (bulletConfig.ttl/(2.0f*SS_H));

	bulletConfig.flashHeight =		(SS_H			      * bulletConfig.flashHeightRatio);
	bulletConfig.flashHalfWidth  = (bulletConfig.flashHeight  * bulletConfig.flashWidthRatio);
	
	
	
	//Also prepare bullets indices
	numBulletSpriteVertices = 0 ;
	
	// ...for every quad the pool can hold: MAX_PLAYER_BULLETS bullets + TWO
	// muzzle-flash quads (one per gun) per player. (v2 P3: the 2010 loop only
	// indexed one flash quad per player, so the last flash of the last player
	// drew with stale indices.)
	for (j=0; j < (MAX_PLAYER_BULLETS*MAX_NUM_PLAYERS * 6 + MAX_NUM_PLAYERS*12); j+=6,numBulletSpriteVertices+=4)
	{
		bulletIndices[j+0] = numBulletSpriteVertices+0;
		bulletIndices[j+1] = numBulletSpriteVertices+1;
		bulletIndices[j+2] = numBulletSpriteVertices+3;
		bulletIndices[j+3] = numBulletSpriteVertices+3;
		bulletIndices[j+4] = numBulletSpriteVertices+1;
		bulletIndices[j+5] = numBulletSpriteVertices+2;
	}
	

	
	
	
	//Init player life count sprites
	 
	 
	 //Prepare the number of remaining lives sprites here

	
	 
	 
	
}





void P_FireBullet(player_t* player,float deltaX, float deltaY)
{
	
	vec2_t spawningPos ;
	
	bullet_t* bullet;
		
	bullet = &player->bullets[player->nextBulletSlotIndice];
	player->nextBulletSlotIndice++;
	player->nextBulletSlotIndice = (MAX_PLAYER_BULLETS-1) & player->nextBulletSlotIndice;
	
	
	
	// TTB: the twin-gun spawn offsets live in ship-local space (right, up);
	// rotate them with the beat so the guns fire from the nose side, not from
	// "above the ship" while the ship is in profile. The gun SPREAD also
	// tightens with the beat (device verdict on 192: rotated to vertical it
	// read too far apart -- one shot above the ship, one below). Upright
	// (sin 0 / cos 1) this is the original sum exactly.
	{
		float ts = sinf(camera.ttbAngle), tc = cosf(camera.ttbAngle);
		float spread = deltaX * (1.0f - 0.6f * fabsf(ts));
		spawningPos[X] = player->ss_position[X]*SS_W + spread * tc + deltaY * ts;
		spawningPos[Y] = player->ss_position[Y]*SS_H - spread * ts + deltaY * tc;
	}
	
	bullet->spawnedY = spawningPos[Y];
	bullet->spawnedX = spawningPos[X];

	bullet->spawnedTime = simulationTime;
	
	//Generate ss_boudaries
	bullet->ss_boudaries[UP]   =  spawningPos[Y] + bulletConfig.halfHeight;
	bullet->ss_boudaries[DOWN] =  spawningPos[Y] - bulletConfig.halfHeight;
	bullet->ss_boudaries[LEFT] =  spawningPos[X] - bulletConfig.halfWidth;
	bullet->ss_boudaries[RIGHT]=  spawningPos[X] + bulletConfig.halfWidth;
	
	bullet->energy = BULLET_DEFAULT_ENERGY;
	
	bullet->type = player->lastBulletType++ ;
	player->lastBulletType &= 3;
	
    
	
	player->firingUpTo = simulationTime+ bulletConfig.msBetweenBullets;
    
    bullet->expirationTime = simulationTime + bulletConfig.ttl ;
	 
    
  // Log_Printf("newbslot=%u\n",player->nextBulletSlotIndice);
}

void P_FireOneBullet(player_t* player)
{
	if (player->autopilot.enabled) // dead / respawning / game over -> no firing
		return;
	if ( player->nextBulletFireTime > simulationTime)
		return;
	
	SND_PlaySound(SND_PLASMA);
	
	P_FireBullet(player,0,0);
	
	player->nextBulletFireTime = simulationTime + bulletConfig.msBetweenBullets ;
	player->lastBulletType = ++player->lastBulletType ;
	player->lastBulletType = player->lastBulletType % 2;
	
}

void P_FireTwoBullet(player_t* player)
{
	if (player->autopilot.enabled) // dead / respawning / game over -> no firing
		return;
	if ( player->nextBulletFireTime > simulationTime)
		return;
	
	engine.playerStats.bulletsFired[player->playerId] +=2;
	
	SND_PlaySound(SND_PLASMA);
	
	P_FireBullet(player,bulletConfig.ss_deltaX ,bulletConfig.ss_deltaY);
	P_FireBullet(player,-bulletConfig.ss_deltaX,bulletConfig.ss_deltaY);
	
	player->nextBulletFireTime = simulationTime + bulletConfig.msBetweenBullets ;
	player->lastBulletType = ++player->lastBulletType ;
	player->lastBulletType = player->lastBulletType % 2;
	
}

//Warning this matrix is declared as row major: <-- Shit !! This line was actually useful 4 month later !!!! You are good fab !!!
/*
		1	0	0	0
		0	0  -1	0
		0   1	0	0
		0	0	0	1
 */
matrix_t fromAboveRotation = {1 , 0  , 0 , 0,
							  0 , 0  , 1 , 0,
							  0 , -1 , 0 , 0,
							  0 , 0  , 0 , 1,} ; 

void P_Update(void)
{
	int i,j;
	entity_t* playerEntity;
	player_t* player;
	bullet_t* bullet;
	
	matrix_t viewMatrix;
	
	vec3_t translationForwardTransform;
	vec3_t translationRightTransform;
	vec3_t translationUpTransform;
	vec3_t translationTransform;
	
	vec3_t vLookat;
	
	
	float t;
	short bulletHeight;
	
		
	vectorAdd(camera.position,camera.forward,vLookat);	
	gluLookAt(camera.position, vLookat, camera.up, viewMatrix);
	
	// Building transpose of camera rotation
	cameraInvRot[0] = viewMatrix[0]; 	cameraInvRot[4] = viewMatrix[1]; 	cameraInvRot[8] = viewMatrix[2];	cameraInvRot[12] = 0;
	cameraInvRot[1] = viewMatrix[4];	cameraInvRot[5] = viewMatrix[5];	cameraInvRot[9] = viewMatrix[6];	cameraInvRot[13] = 0;
	cameraInvRot[2] = viewMatrix[8];	cameraInvRot[6] = viewMatrix[9];	cameraInvRot[10] = viewMatrix[10];	cameraInvRot[14] = 0;
	cameraInvRot[3] = 0;				cameraInvRot[7] = 0;				cameraInvRot[11] = 0;				cameraInvRot[15] = 1;

	
	//players[0].ss_position[Y] +=0.01 ;
	//players[1].ss_position[X] +=0.01 ;
	
	

	
	//Need to update entity matrix to stick to camera
	for( i =0 ; i <numPlayers ; i++)
	{
		player = &players[i] ;
		playerEntity = &player->entity;
		
		if (entitiesAttachedToCamera) 
		{
			players[i].showPointer -= timediff;
			// Passive "survival" score: only while the player is actually alive and
			// in control. Autopilot stays enabled through the death/respawn fly-in
			// and (timeCounter=2000000) the whole GAME OVER screen, so this also
			// stops the score from ticking up after dying / on game over.
			if (!player->autopilot.enabled)
				if (!gScoreLocked)
					players[i].score += (timediff >> 1 << engine.difficultyLevel) * 2;
			
			// CI probe: SHMUP_AUTOFIRE=1 makes the idle Simulator ship fire
			// continuously, so bullets, impacts and FX sprite passes run every
			// frame and the smoke's dome/sky luma assertions sample a REAL
			// frame mix -- the client-state lottery (builds 207/208) never
			// reproduced on an idle run because no polluting pass ever drew.
			{
				static int autofire = -1;
				if (autofire < 0)
				{
					char* e = getenv("SHMUP_AUTOFIRE");
					autofire = (e && e[0] == '1') ? 1 : 0;
				}
				// singleplayer-only: firing from LOCAL env state inside the
				// sim would desync lockstep peers (code review)
				if (autofire && engine.mode == DE_MODE_SINGLEPLAYER)
					P_FireTwoBullet(player);
			}

			if (player->autopilot.enabled)
			{
				t = player->autopilot.timeCounter/player->autopilot.originalTime;

				//printf("t=%.2f\n",t);
				//printf("player->autopilot.timeCounter=%d\n",player->autopilot.timeCounter);
				//printf("player->autopilot.enabled =%d\n",(player->autopilot.timeCounter >0));
				t *= t;   
				player->ss_position[X] = player->autopilot.end_ss_position[X] + t * player->autopilot.diff_ss_position[X];
				player->ss_position[Y] = player->autopilot.end_ss_position[Y] + t * player->autopilot.diff_ss_position[Y];
				P_UpdateSSBoundaries(i);
				
				player->autopilot.timeCounter -= timediff;
				//printf("player->autopilot.timeCounter=%d.\n",player->autopilot.timeCounter);
				player->autopilot.enabled = (player->autopilot.timeCounter > 0) ;
				
				
			}
			
			
			if (players[i].invulnerableFor > 0)
			{
				players[i].invulFlickering += timediff;
				players[i].shouldDraw =players[i].invulFlickering & 128 ; //Fickering every 128ms
				
				players[i].invulnerableFor -= timediff;
				if (players[i].invulnerableFor <= 0)
						players[i].shouldDraw = 1;
			}
			
			
			//UPDATE MATRIX

			//Rotation part
			//
			// TTB: during the side-view beat the ship rolls to its PROFILE (nose
			// leading the travel, i.e. toward the screen side the decor flows
			// from) -- the usual top-down billboard would show the ship's top to
			// a side camera. The blend follows the camera's own swing fraction
			// (camera.ttbAngle), so both moves land together; f stays 0 on every
			// act without a ttbRoll and this is the original matrix product.
			{
				float f = fabsf(camera.ttbAngle) / ((float)M_PI * 0.5f);

				if (f <= 0.0001f)
				{
					matrix_multiply(cameraInvRot, fromAboveRotation, playerEntity->matrix);
				}
				else
				{
					// The blend lives in camera.c now (CAM_GetTTBBlend) so the
					// enemies share the exact same pose; derivation history in
					// camera.c and the git log.
					matrix_t blended;
					CAM_GetTTBBlend(blended, 0.2f);
					matrix_multiply(cameraInvRot, blended, playerEntity->matrix);
				}
			}
			
			//Translation part
			vectorScale(camera.forward,distanceZFromCamera,translationForwardTransform);
			vectorScale(camera.right, player->ss_position[X] * widthAtDistance ,translationRightTransform);
			vectorScale(camera.up   , player->ss_position[Y] * heightAtDistance,translationUpTransform);
			
			
			
			vectorCopy(camera.position,									translationTransform) ;
			vectorAdd(translationTransform,translationForwardTransform,	translationTransform) ;
			vectorAdd(translationTransform,translationRightTransform,	translationTransform) ;
			vectorAdd(translationTransform,translationUpTransform,		translationTransform) ;
			
			
			playerEntity->matrix[12] = translationTransform[X] ;
			playerEntity->matrix[13] = translationTransform[Y] ;
			playerEntity->matrix[14] = translationTransform[Z] ;
			
			
			//END UPDATE MATRIX	
			
			//Also update bullets
			//
			// TTB: bullets travel along the beat's rotated axis -- straight up
			// the screen when upright (sin 0 / cos 1: the original math to the
			// bit), toward the nose (screen-right, or left for angle < 0) in the
			// side view. camera.ttbAngle is simulation-driven, so both lockstep
			// peers rotate identically.
			{
				float ttbSin = sinf(camera.ttbAngle);
				float ttbCos = cosf(camera.ttbAngle);

				for(j=0; j < MAX_PLAYER_BULLETS ; j++)
				{
					float dist;
					short bulletX;

					if (player->bullets[j].expirationTime <= simulationTime )	// <=: exp 0 at a timer reset (t=0) means expired
						continue;

					bullet = &player->bullets[j];

					t = (simulationTime - bullet->spawnedTime)/ (float)bulletConfig.ttl ;
					dist = t * bulletConfig.distPerLifepsan;

					bulletHeight = bullet->spawnedY + dist * ttbCos;
					bulletX      = bullet->spawnedX + dist * ttbSin;

					// AABB of the rotated capsule (exact upright, conservative
					// mid-swing); the sprite is drawn rotated from its center,
					// slimmed on its cross axis with the beat (see the render).
					{
						float aC = fabsf(ttbCos), aS = fabsf(ttbSin);
						float slimW = bulletConfig.halfWidth * (1.0f - 0.5f * aS);
						float bh = bulletConfig.halfHeight * aC + slimW * aS;
						float bw = slimW * aC + bulletConfig.halfHeight * aS;

						bullet->ss_boudaries[UP]    = bulletHeight + bh;
						bullet->ss_boudaries[DOWN]  = bulletHeight - bh;
						bullet->ss_boudaries[LEFT]  = bulletX - bw;
						bullet->ss_boudaries[RIGHT] = bulletX + bw;
					}
				}
			}
			
			
			// Update ghosts
			P_UpdateGhosts(player);
			
			
			
			
		}
		else 
		{
			//Ship is not yet behaving as in 2D screenspace, we need to have it follow a predetermined path
			
			//Update translation
			//playerEntity->matrix[12] += translationTransform[X] ;
			//playerEntity->matrix[13] += translationTransform[Y] ;
			
			//playerEntity->matrix[14] = -0.24f * simulationTime + players[i].spawnWorldPosition[Z] ;
			//
			// The outro rush must OUTRUN the camera. 2010's flat -0.24 assumed
			// a rail that has ended (acts 1/2 detach ~2s before their rail runs
			// out, camera nearly still); act 3 detaches mid-cruise with the
			// camera at ~0.44 u/ms -- FASTER than the ship's escape, so the
			// camera overtook it and the ship slid off the bottom ("le vaisseau
			// a disparu", builds 205/206). Escape = the camera's own forward
			// speed plus the 2010 constant: the ship recedes at the same
			// RELATIVE pace in every act. Deterministic (camera is sim-driven),
			// and one measurement shared per frame by both lockstep players.
			// The PROLOG keeps the 2010 look untouched (the camera catching up
			// to the drifting ships IS the intro); only the OUTRO compensates.
			if (!playersWereAttached)
				playerEntity->matrix[14] += -0.24f * timediff ;
			else
			{
				// Camera-owned, scene-safe velocity (code review: the private
				// static tracker here carried another scene's coordinates and
				// timestamps across Timer_resetTime -- one-frame teleports at
				// later outros). Clamped so a sparse rail keyframe can't spike
				// the escape for a frame; only forward (-Z) cruise counts.
				float camVelZ = CAM_GetDriftVelZ();
				if (camVelZ > 0)
					camVelZ = 0;
				if (camVelZ < -0.6f)
					camVelZ = -0.6f;
				playerEntity->matrix[14] += (camVelZ - 0.24f) * timediff ;
			}
			//240 units per 1000ms, relative to the camera in the outro
			
			//printf("t= %d: tdiff:%d p pos=%.2f,%.2f,%.2f.\n",simulationTime,timediff,playerEntity->matrix[12],playerEntity->matrix[13],playerEntity->matrix[14]);
			//Update roll if necessary
			
		}

				
		
	}

	
	
	
}

#define NUM_INDICE_POINTER_PER_PLAYER 18
#define NUM_VERTICE_POINTER_PER_PLAYER 10
vec2short_t pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER * MAX_NUM_PLAYERS];
xf_colorless_sprite_t pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER * MAX_NUM_PLAYERS];
ushort pointerSprIndices[NUM_INDICE_POINTER_PER_PLAYER*MAX_NUM_PLAYERS] = { 1, 0, 2,  0, 2, 3,  4, 5, 6,    5, 6, 7,    6,7,8   , 8, 7, 9,       
								11,10,12, 10,12,13, 14,15,16,   15,16,17,   16,17,18   ,18,17,19
							    };
ushort  numSprIndices;

void P_CreatePointerCoordinates(void)
{
	vec2short_t start,end;
	vec2_t dir;

	/*
	 
	 
	 
	 
	 0_______________3
	 |              |
	 |   Circle     |
	 |              |
	 |         5    |
	 |      4 / \   |
	 |        \  \  |		 
	 |         \  \ |        
	 -----------\----        
	 1           \  2         
	              \  \        
	               \  \       
	                \  \      
	                 \  \7____________________________9
	                  \________________________________|
	                   6                              8	 
	 
	 
	 */
	
	
	// Round circle over player 1 and player 2
	//0  3
	//1  2
	pointerSprVertices[0].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+0].text[X] = 0;
	pointerSprVertices[0].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+0].text[Y] = 0;
	pointerSprVertices[1].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+1].text[X] = 0;
	pointerSprVertices[1].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+1].text[Y] = 64/(float)128*SHRT_MAX;
	pointerSprVertices[2].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+2].text[X] = 64/(float)256*SHRT_MAX;
	pointerSprVertices[2].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+2].text[Y] = 64/(float)128*SHRT_MAX;
	pointerSprVertices[3].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+3].text[X] = 64/(float)256*SHRT_MAX;
	pointerSprVertices[3].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+3].text[Y] = 0;
	
	pointerdeltaSprVertices[0][X] = pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+0][X]= -0.2 * SS_W;
	pointerdeltaSprVertices[0][Y] = pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+0][Y]=  0.2 * 0.6666 * SS_H;
	pointerdeltaSprVertices[1][X] = pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+1][X]= -0.2 * SS_W;
	pointerdeltaSprVertices[1][Y] = pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+1][Y]= -0.2 * 0.6666* SS_H;
	pointerdeltaSprVertices[2][X] = pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+2][X]=  0.2 * SS_W;
	pointerdeltaSprVertices[2][Y] = pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+2][Y]= -0.2 * 0.6666* SS_H;
	pointerdeltaSprVertices[3][X] = pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+3][X]=  0.2 * SS_W;
	pointerdeltaSprVertices[3][Y] = pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+3][Y]=  0.2 * 0.6666* SS_H;
	
	

#define UNDERLINE_WIDTH 0.008f
	//Underline part 1
	//0  3
	//1  2	
	
	start[X] = 0;
	start[Y] = 0;
	end[X] = 0.3 * SS_W;
	end[Y] =-0.2 * 0.6666* SS_H;
	dir[X] = end[X] - start[X];
	dir[Y] = end[Y] - start[Y];
	normalize2(dir);
	
	pointerdeltaSprVertices[4][X]=   dir[Y] * SS_W * UNDERLINE_WIDTH;
	pointerdeltaSprVertices[4][Y]=  - dir[X] * SS_H * UNDERLINE_WIDTH;
	pointerdeltaSprVertices[5][X]= -  dir[Y] * SS_W * UNDERLINE_WIDTH;
	pointerdeltaSprVertices[5][Y]=  dir[X] * SS_H * UNDERLINE_WIDTH;
	pointerdeltaSprVertices[6][X]=  0.3 * SS_W		        + dir[Y] * SS_W * UNDERLINE_WIDTH;
	pointerdeltaSprVertices[6][Y]= -0.2 * 0.6666* SS_H     - dir[X] * SS_H * UNDERLINE_WIDTH;
	pointerdeltaSprVertices[7][X]=  0.3 * SS_W				- dir[Y] * SS_W * UNDERLINE_WIDTH;
	pointerdeltaSprVertices[7][Y]=  -0.2 * 0.6666* SS_H   + dir[X] * SS_H * UNDERLINE_WIDTH;
	
	//Player2 is different
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+4][X]= -pointerdeltaSprVertices[4][X];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+4][Y]= -pointerdeltaSprVertices[4][Y];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+5][X]= -pointerdeltaSprVertices[5][X];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+5][Y]= -pointerdeltaSprVertices[5][Y];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+6][X]= -pointerdeltaSprVertices[6][X];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+6][Y]= -pointerdeltaSprVertices[6][Y];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+7][X]= -pointerdeltaSprVertices[7][X];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+7][Y]= -pointerdeltaSprVertices[7][Y];
	
	pointerSprVertices[4].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+4].text[X] = 59/(float)256*SHRT_MAX ;
	pointerSprVertices[4].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+4].text[Y] =  36 /(float)128*SHRT_MAX ;
	pointerSprVertices[7].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+7].text[X] =  59/(float)256*SHRT_MAX ; 
	pointerSprVertices[7].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+7].text[Y] =  37 /(float)128*SHRT_MAX ; 
	pointerSprVertices[5].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+5].text[X] =  60/(float)256*SHRT_MAX ; 
	pointerSprVertices[5].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+5].text[Y] =  37 /(float)128*SHRT_MAX ;
	pointerSprVertices[6].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+6].text[X] =  60/(float)256*SHRT_MAX ; 
	pointerSprVertices[6].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+6].text[Y] =  36 /(float)128*SHRT_MAX ;
	
	
	//Underline part 2 
	//0  3
	//1  2	
	pointerdeltaSprVertices[8][X] = pointerdeltaSprVertices[6][X] + 0.43 * SS_W;
	pointerdeltaSprVertices[8][Y] = pointerdeltaSprVertices[6][Y];
	pointerdeltaSprVertices[9][X] = pointerdeltaSprVertices[8][X];
	pointerdeltaSprVertices[9][Y] = pointerdeltaSprVertices[7][Y];

	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+8][X]=  -pointerdeltaSprVertices[8][X];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+8][Y]=  -pointerdeltaSprVertices[8][Y];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+9][X]=  -pointerdeltaSprVertices[9][X];
	pointerdeltaSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+9][Y]=  -pointerdeltaSprVertices[9][Y];
	
	
	
	pointerSprVertices[8].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+8].text[X] =  60/(float)256*SHRT_MAX ; 
	pointerSprVertices[8].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+8].text[Y] =  37 /(float)128*SHRT_MAX ;

	pointerSprVertices[9].text[X] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+9].text[X] =  60/(float)256*SHRT_MAX ;
	pointerSprVertices[9].text[Y] = pointerSprVertices[NUM_VERTICE_POINTER_PER_PLAYER+9].text[Y] =  36 /(float)128*SHRT_MAX ;

	// v2 P3: seats 2/3 reuse the hand-built seat 0/1 art verbatim (seat 3 gets
	// the mirrored player-two layout, like seat 1). The static index initializer
	// only covers two seats' worth of absolute vertex ids -- the extra seats'
	// blocks are generated here from the same base pattern, +10 per seat.
	{
		static const ushort basePattern[NUM_INDICE_POINTER_PER_PLAYER] =
			{ 1,0,2, 0,2,3, 4,5,6, 5,6,7, 6,7,8, 8,7,9 };
		int s, k;
		for (s = 2; s < MAX_NUM_PLAYERS; s++)
		{
			int parent = s - 2;
			for (k = 0; k < NUM_VERTICE_POINTER_PER_PLAYER; k++)
			{
				pointerdeltaSprVertices[s*NUM_VERTICE_POINTER_PER_PLAYER + k][X] = pointerdeltaSprVertices[parent*NUM_VERTICE_POINTER_PER_PLAYER + k][X];
				pointerdeltaSprVertices[s*NUM_VERTICE_POINTER_PER_PLAYER + k][Y] = pointerdeltaSprVertices[parent*NUM_VERTICE_POINTER_PER_PLAYER + k][Y];
				pointerSprVertices[s*NUM_VERTICE_POINTER_PER_PLAYER + k].text[X] = pointerSprVertices[parent*NUM_VERTICE_POINTER_PER_PLAYER + k].text[X];
				pointerSprVertices[s*NUM_VERTICE_POINTER_PER_PLAYER + k].text[Y] = pointerSprVertices[parent*NUM_VERTICE_POINTER_PER_PLAYER + k].text[Y];
			}
			for (k = 0; k < NUM_INDICE_POINTER_PER_PLAYER; k++)
				pointerSprIndices[s*NUM_INDICE_POINTER_PER_PLAYER + k] = basePattern[k] + s*NUM_VERTICE_POINTER_PER_PLAYER;
		}
	}
}

// Update position of the pointer to be above the player's ship
void P_PreparePointerSprites(void)
{
	int i,j;
	
	numSprIndices = 0;
	
	for (i=0; i < numPlayers; i++) 
	{
		if (players[i].showPointer <=0)
			continue;
		
		//Only need to update the vertices coordiantes as the texture one have been generated already.
		for (j=0;  j < NUM_VERTICE_POINTER_PER_PLAYER; j++) {
			pointerSprVertices[j+i*NUM_VERTICE_POINTER_PER_PLAYER].pos[X] = pointerdeltaSprVertices[j+i*NUM_VERTICE_POINTER_PER_PLAYER][X] + players[i].ss_position[X] * SS_W ;
			pointerSprVertices[j+i*NUM_VERTICE_POINTER_PER_PLAYER].pos[Y] = pointerdeltaSprVertices[j+i*NUM_VERTICE_POINTER_PER_PLAYER][Y] / renderer.vScale + players[i].ss_position[Y] * SS_H ;

		}
		numSprIndices+= 24;
		
	}
}

// v2 P3: all four seats named; the label offsets are hand-tuned per SIDE, and
// seats 2/3 reuse their parent side's pointer art (P_CreatePointerCoordinates),
// so they reuse its offsets too.
char* playersNames[MAX_NUM_PLAYERS] = {"Player 1","Player 2","Player 3","Player 4"};
float playerDelta[MAX_NUM_PLAYERS][2] = {
	/*p1*/{110,-14},   // raised the label so it sits just above the white underline
	/*p2*/{-200,98},
	/*p3*/{110,-14},
	/*p4*/{-200,98}
};

// Boss health bar (act 3): a REAL graphical bar -- light frame, dark back,
// bright red fill draining right-to-left -- drawn as untextured colored quads.
// The previous text-glyph bars ('='/'-', then 'I'/blank segments) kept reading
// as invisible or static on device; solid quads can't be missed and the fill
// drains smoothly with every hit instead of in 20 coarse steps.
#define BOSS_BAR_LEFT		(-186)
#define BOSS_BAR_RIGHT		( 284)
#define BOSS_BAR_BORDER		3
#define BOSS_BAR_LABEL_X	(-244)

static void P_SetBarQuad(xf_textureless_sprite_t* q, short l, short r, short top, short bot,
						 uchar topRed, uchar topGreen, uchar topBlue,
						 uchar botRed, uchar botGreen, uchar botBlue, uchar alpha)
{
	// 0 2
	// 1 3   (same corner layout / index pattern as the text quads)
	q[0].pos[X] = l; q[0].pos[Y] = top;
	q[1].pos[X] = l; q[1].pos[Y] = bot;
	q[2].pos[X] = r; q[2].pos[Y] = top;
	q[3].pos[X] = r; q[3].pos[Y] = bot;
	q[0].color[R] = topRed; q[0].color[G] = topGreen; q[0].color[B] = topBlue; q[0].color[A] = alpha;
	q[2].color[R] = topRed; q[2].color[G] = topGreen; q[2].color[B] = topBlue; q[2].color[A] = alpha;
	q[1].color[R] = botRed; q[1].color[G] = botGreen; q[1].color[B] = botBlue; q[1].color[A] = alpha;
	q[3].color[R] = botRed; q[3].color[G] = botGreen; q[3].color[B] = botBlue; q[3].color[A] = alpha;
}

static void P_RenderBossHealthBar(int hp, int maxHp, short barY)
{
	static xf_textureless_sprite_t quads[12];
	static ushort indices[18] = { 0,1,2, 1,3,2,  4,5,6, 5,7,6,  8,9,10, 9,11,10 };
	short halfH, fillRight;
	ushort numIndices;

	// Same vertical un-stretch as the text glyphs, so the bar keeps the same
	// physical thickness on a tall screen as on a 2:3 one.
	halfH = (short)(9.0f / (renderer.vScale > 0.0f ? renderer.vScale : 1.0f));
	if (halfH < 5)
		halfH = 5;

	// Light frame + near-black inner back: the SPENT part of the bar stays
	// visible, so "how much is left" always has a fixed reference frame.
	P_SetBarQuad(&quads[0],
				 BOSS_BAR_LEFT - BOSS_BAR_BORDER, BOSS_BAR_RIGHT + BOSS_BAR_BORDER,
				 (short)(barY + halfH + BOSS_BAR_BORDER), (short)(barY - halfH - BOSS_BAR_BORDER),
				 235, 235, 240,  235, 235, 240,  210);
	P_SetBarQuad(&quads[4],
				 BOSS_BAR_LEFT, BOSS_BAR_RIGHT,
				 (short)(barY + halfH), (short)(barY - halfH),
				 12, 12, 18,  12, 12, 18,  230);
	numIndices = 12;

	// Fill width rounds UP so the bar only reads empty when the boss is
	// actually dead (same rule as the old segmented bar).
	if (hp > 0 && maxHp > 0)
	{
		int w = ((BOSS_BAR_RIGHT - BOSS_BAR_LEFT) * hp + maxHp - 1) / maxHp;
		if (w < 2)
			w = 2;
		if (w > BOSS_BAR_RIGHT - BOSS_BAR_LEFT)
			w = BOSS_BAR_RIGHT - BOSS_BAR_LEFT;
		fillRight = (short)(BOSS_BAR_LEFT + w);

		// Bright-to-dark vertical gradient so the fill reads as a lit bar.
		P_SetBarQuad(&quads[8],
					 BOSS_BAR_LEFT, fillRight,
					 (short)(barY + halfH), (short)(barY - halfH),
					 255, 84, 64,  168, 16, 16,  255);
		numIndices = 18;
	}

	renderer.RenderTexturelessSprites(quads, numIndices, indices);
}

char stringScore[64];
void PL_RenderPlayerPointers(void)
{
	xf_colorless_sprite_t* spriteVertice;
	int i;
	
	if (!entitiesAttachedToCamera)
		return;

	// The end-of-game card owns the frame: no score row, no lives, no leftovers
	// from the fight on top of it.
	if (TITLE_IsEndOfGameScreen())
		return;

	spriteVertice = &diverSpriteLib.vertices[diverSpriteLib.numVertices];

	// Lives row: align with the score (same iOS safe-area anchor) and squash the
	// icon height by vScale so they stay square on a tall screen (unscaled ortho).
	float livesAnchorY = SS_H - renderer.safeInsetTopPx * ((2.0f * SS_H) / (float)renderer.glBuffersDimensions[HEIGHT]) - 30.0f;
	float livesHalfH   = PLAYER_LIVE_COUNT_HEIGHT * SS_W / ((renderer.vScale > 0.0f ? renderer.vScale : 1.0f) * 2.0f);
	float livesTop = livesAnchorY + livesHalfH;
	float livesBot = livesAnchorY - livesHalfH;

	// v2 P3: the lives read as ONE icon + an "xN" counter (drawn in the text
	// pass below). Born for multiplayer, whose shared pool of 12 would march
	// an icon row across the whole screen (and overflow the 36-vertex buffer
	// this row shares with the on-screen buttons); solo adopted it on the
	// next build's feedback -- one display, one habit. The icon shows even at
	// zero: it used to vanish on the last death while the "x0" stayed behind,
	// orphaned (user feedback) -- the pair lives and dies together.
	int livesIcons = 1;
	float livesStartX = MP_LIVES_ICON_RIGHT_X;

	for (i=0; i < livesIcons; i++)
	{
		spriteVertice->pos[X] = (livesStartX + (PLAYER_LIVE_COUNT_SPACING+ PLAYER_LIVE_COUNT_WIDTH) * i) * SS_W;
		spriteVertice->pos[Y] = livesTop;
		spriteVertice->text[U] = PLAYER_LIVE_COUNT_TEXT_START_U;
		spriteVertice->text[V] = PLAYER_LIVE_COUNT_TEXT_START_V;
		//spriteVertice->color[R] =  spriteVertice->color[G] =  spriteVertice->color[B] =  spriteVertice->color[A] = 255; 
		spriteVertice++;
		
		spriteVertice->pos[X] = (livesStartX + (PLAYER_LIVE_COUNT_SPACING+ PLAYER_LIVE_COUNT_WIDTH) * i ) * SS_W;
		spriteVertice->pos[Y] = livesBot;
		spriteVertice->text[U] = PLAYER_LIVE_COUNT_TEXT_START_U;
		spriteVertice->text[V] = PLAYER_LIVE_COUNT_TEXT_START_V + PLAYER_LIVE_COUNT_TEXT_HEIGHT;
		// spriteVertice->color[R] =  spriteVertice->color[G] =  spriteVertice->color[B] =  spriteVertice->color[A] = 255;
		spriteVertice++;
		
		
		spriteVertice->pos[X] = (livesStartX + (PLAYER_LIVE_COUNT_SPACING+ PLAYER_LIVE_COUNT_WIDTH) * i + PLAYER_LIVE_COUNT_WIDTH) * SS_W;
		spriteVertice->pos[Y] = livesBot;
		spriteVertice->text[U] = PLAYER_LIVE_COUNT_TEXT_START_U + PLAYER_LIVE_COUNT_TEXT_WIDTH;
		spriteVertice->text[V] = PLAYER_LIVE_COUNT_TEXT_START_V + PLAYER_LIVE_COUNT_TEXT_HEIGHT;
		// spriteVertice->color[R] =  spriteVertice->color[G] =  spriteVertice->color[B] =  spriteVertice->color[A] = 255;
		spriteVertice++;
		
		
		spriteVertice->pos[X] = (livesStartX + (PLAYER_LIVE_COUNT_SPACING+ PLAYER_LIVE_COUNT_WIDTH) * i + PLAYER_LIVE_COUNT_WIDTH) * SS_W;
		spriteVertice->pos[Y] = livesTop;
		spriteVertice->text[U] = PLAYER_LIVE_COUNT_TEXT_START_U + PLAYER_LIVE_COUNT_TEXT_WIDTH;
		spriteVertice->text[V] = PLAYER_LIVE_COUNT_TEXT_START_V;
		// spriteVertice->color[R] =  spriteVertice->color[G] =  spriteVertice->color[B] =  spriteVertice->color[A] = 255;
		spriteVertice++;
		
		diverSpriteLib.numVertices+= 4;
		diverSpriteLib.numIndices+=6;
	}
	
	renderer.SetTexture(pointersTexture.textureId);
	
	renderer.RenderColorlessSprites(diverSpriteLib.vertices,diverSpriteLib.numIndices,enFxLib.indices);
	
	
	
	//Pointers
	for (i=0 ; i < numPlayers ; i++)
	{
		if (players[i].showPointer <= 0)
			continue;
		
		renderer.RenderColorlessSprites(pointerSprVertices,NUM_INDICE_POINTER_PER_PLAYER,pointerSprIndices+i*NUM_INDICE_POINTER_PER_PLAYER);
	
		
	}
	
	for (i=0 ; i < numPlayers ; i++)
	{
		if (players[i].showPointer <= 0)
			continue;
		
		SCR_StartConvertText();
		SCR_ConvertTextToVertices(playersNames[i],2.2f,players[i].ss_boudaries[LEFT]+playerDelta[i][X],players[i].ss_boudaries[DOWN]+playerDelta[i][Y],TEXT_NOT_CENTERED);
		SCR_RenderText();
	}
	

	
	//Also render highscore (v2 P3: the TEAM score in multiplayer)
	sprintf(stringScore,SCORE_FORMAT,P_GetDisplayScore());
	SCR_StartConvertText();
	// Anchor the score just below the iOS safe area (status bar / notch / Dynamic
	// Island). On a 2:3 device (vScale=1, safeInsetTopPx=0) this lands at ~the
	// legacy SCORE_POS_Y, so other platforms are unaffected.
	{
		float orthoPerPx = (2.0f * SS_H) / (float)renderer.glBuffersDimensions[HEIGHT];
		short scoreY = (short)(SS_H - renderer.safeInsetTopPx * orthoPerPx - 30.0f);
		int bossHp = 0, bossMaxHp = 0, bossFight;
		short bossBarY = 0;
		SCR_ConvertTextToVertices(stringScore,SCORE_FONT_SIZE,SCORE_POS_X,scoreY,TEXT_NOT_CENTERED);
		// v2 P3: the shared-pool counter, immediately to the RIGHT of the
		// single life icon (all modes -- solo shows "x3" the same way). The
		// first glyph's quad reaches half a glyph LEFT of the x we pass, so
		// clear the icon by that much plus a small gap; same maths as
		// SCR_ConvertTextToVertices, so it holds if the size changes.
		{
			char livesStr[8];
			int pool = players[controlledPlayer].respawnCounter;
			short glyphHalf = (short)(MP_LIVES_FONT_SIZE * SS_W / 40);
			short livesTextX = (short)(MP_LIVES_ICON_RIGHT_X * SS_W) + glyphHalf + 6;
			if (pool < 0) pool = 0;
			sprintf(livesStr, "x%d", pool);
			SCR_ConvertTextToVertices(livesStr, MP_LIVES_FONT_SIZE, livesTextX, scoreY, TEXT_NOT_CENTERED);
		}
		// Tutorial (scenes 14 = swipe, 15 = virtual pad) and Demo (scene 13): a
		// BACK button at the top-centre to leave. Hit-tested in EAGLView.
		if (engine.sceneId == 13 || engine.sceneId == 14 || engine.sceneId == 15)
			SCR_ConvertTextToVertices("[ BACK ]",SCORE_FONT_SIZE,0,(short)(scoreY - 100),TEXT_CENTERED);
		// Boss health bar (act 3), just under the score line while the fight is
		// on. Only the "BOSS" label is font text (letters are proven on-screen);
		// the bar itself is drawn after the text pass as solid colored quads.
		bossFight = LOFB_GetBossHealth(&bossHp, &bossMaxHp);
		if (bossFight)
		{
			bossBarY = (short)(scoreY - 52);
			SCR_ConvertTextToVertices("BOSS", 1.8f, BOSS_BAR_LABEL_X, bossBarY, TEXT_CENTERED);
		}
		// (The temporary act-3 on-screen diagnostic lived here through rounds
		// 12-14; removed once the U-turn patrol was confirmed on device.)
		SCR_RenderText();
		if (bossFight)
			P_RenderBossHealthBar(bossHp, bossMaxHp, bossBarY);
	}
}


void P_PrepareBulletSprites(void)
{
	int i,j;
	short flashY;
	short leftFlashX;
	short rightFlashX;
	float flashInterpolation;
	
	xf_colorless_sprite_t* bulSprite;
	
	bullet_t* bullet;
	player_t* player;
	
	bulSprite = pBulletVertices;
	numPBulletsIndices = 0;
	
	
	for(i=0 ; i < numPlayers ; i++)
	{
		int colorCol;
		player = &players[i] ;

		// Solo bullet colour: pick the chosen atlas colour column; multiplayer keeps the
		// player-index colours so the two players' shots stay distinguishable.
			// Solo: the chosen Custom colour. Multiplayer: each player's SYNCED Custom
		// colour (defaults to the classic red/blue = the player index).
		colorCol = (engine.mode == DE_MODE_SINGLEPLAYER) ? gBulletColor : gMPBulletColor[i];	// v2 P3: was i&1, discarded seats 2/3

		//Check if the player is currently firing and spawn a flash if so.
		//Suppressed while the world is frozen (timediff 0) so the muzzle flash
		//doesn't stay lit on-screen during the resume countdown.
		if (timediff && player->firingUpTo >= simulationTime)
		{
			// TTB: the two muzzle flames are authored in ship-local space
			// (right = gun spread, up = flame length) and rotated with the
			// beat, like the bullets -- v1.5.8 left them pointing up-screen
			// while the ship was in profile, so the shots seemed to spawn
			// above the ship. Upright this is the original layout exactly.
			float ts = sinf(camera.ttbAngle), tc = cosf(camera.ttbAngle);
			float shipX = player->ss_position[X] * SS_W;
			float shipY = player->ss_position[Y] * SS_H;
			float fh, fw;
			int   gun;

			flashInterpolation = 1- (player->firingUpTo - simulationTime) / (float)bulletConfig.msBetweenBullets;
			fh = bulletConfig.flashHeight * flashInterpolation / gVScale;
			// Device verdict on 191: the nose fire is FAR too wide in the side
			// view. Slim the flame's cross axis with the beat, harder than the
			// bullets (45% left at 90 degrees); untouched upright.
			fw = bulletConfig.flashHalfWidth * (1.0f - 0.55f * fabsf(ts));

			for (gun = 0; gun < 2; gun++)
			{
				// local corner offsets (a = right, b = up), original order;
				// the gun spread tightens with the beat, like the bullets'.
				float spread = bulletConfig.flashScreenSpaceXDelta * (1.0f - 0.6f * fabsf(ts));
				float gx = (gun == 0) ? -spread : spread;
				float a[4], b[4];
				int   c;
				short texU[4], texV[4];

				a[0] = gx - fw;	b[0] = bulletConfig.flashScreenSpaceYDelta;
				a[1] = gx - fw;	b[1] = bulletConfig.flashScreenSpaceYDelta + fh;
				a[2] = gx + fw;	b[2] = bulletConfig.flashScreenSpaceYDelta + fh;
				a[3] = gx + fw;	b[3] = bulletConfig.flashScreenSpaceYDelta;

				// v2 P3: the atlas only holds TWO flash columns (80->104->128px);
				// seats 2/3 reuse their parent side's column (i&1) -- a raw i
				// would walk past the atlas edge and wrap the short UV negative.
				{
				int flashCol = i & 1;
				texU[0] = (80.0f/128*SHRT_MAX) + flashCol*(24.0f/128*SHRT_MAX);
				texV[0] = (64.0f/128*SHRT_MAX) + (32.0f/128*SHRT_MAX) + player->lastBulletType*(32.0f/128*SHRT_MAX);
				texU[1] = (80.0f/128*SHRT_MAX) + flashCol*(24.0f/128*SHRT_MAX);
				texV[1] = (64.0f/128*SHRT_MAX) + player->lastBulletType*(32.0f/128*SHRT_MAX);
				texU[2] = (80.0f/128*SHRT_MAX) + flashCol*(24.0f/128*SHRT_MAX) + (24.0f/128*SHRT_MAX);
				texV[2] = (64.0f/128*SHRT_MAX) + player->lastBulletType*(32.0f/128*SHRT_MAX);
				texU[3] = (80.0f/128*SHRT_MAX) + flashCol*(24.0f/128*SHRT_MAX) + (24.0f/128*SHRT_MAX);
				texV[3] = (64.0f/128*SHRT_MAX) + (32.0f/128*SHRT_MAX) + player->lastBulletType*(32.0f/128*SHRT_MAX);
				}

				for (c = 0; c < 4; c++)
				{
					bulSprite->pos[X] = shipX + a[c] * tc + b[c] * ts;
					bulSprite->pos[Y] = shipY - a[c] * ts + b[c] * tc;
					bulSprite->text[X] = texU[c];
					bulSprite->text[Y] = texV[c];
					bulSprite++;
				}
			}

			numPBulletsIndices += 12;


		}
		
      //  char bulletdiagnostic[MAX_PLAYER_BULLETS+1+3];
      //  bulletdiagnostic[0] = 'p';
      //  bulletdiagnostic[1] = '0'+i;
      //  bulletdiagnostic[2] = '-';
      //  bulletdiagnostic[19]= '\0';
        
		// Check if there is any bullets to render.
		for(j=0 ; j < MAX_PLAYER_BULLETS ; j++)
		{
			bullet = &player->bullets[j];
			
		//	bulletdiagnostic[j+3] = '0';
			if (bullet->expirationTime <= simulationTime)	// <=: exp 0 at a timer reset (t=0) means expired
				continue;
		//	bulletdiagnostic[j+3] = '1';
            
            
		    bullet->type++;
			bullet->type = bullet->type & 3;

			// TTB: the capsule sprite is drawn rotated by the beat's angle from
			// its center (long axis along the travel), so a side-view bullet is
			// a horizontal streak, not a sideways-sliding vertical one. Upright
			// (sin 0 / cos 1) this is the original axis-aligned quad exactly.
			{
				float cx = (bullet->ss_boudaries[LEFT] + bullet->ss_boudaries[RIGHT]) * 0.5f;
				float cy = (bullet->ss_boudaries[UP]   + bullet->ss_boudaries[DOWN])  * 0.5f;
				float ts = sinf(camera.ttbAngle), tc = cosf(camera.ttbAngle);
				// The capsule is nearly as wide as it is long (widthRatio 0.15
				// vs heightRatio 0.18) -- rotated on its side it read FAT on
				// device ("trop épais, il faut les affiner"). Slim the cross
				// axis with the beat: full width upright, half width at 90.
				float slim  = 1.0f - 0.5f * fabsf(ts);
				float dirX  = ts * bulletConfig.halfHeight, dirY  = tc * bulletConfig.halfHeight;
				float perpX = tc * bulletConfig.halfWidth * slim,  perpY = -ts * bulletConfig.halfWidth * slim;

				bulSprite->pos[X] = cx - perpX - dirX;
				bulSprite->pos[Y] = cy - perpY - dirY;
				bulSprite->text[X] = colorCol*(16.0f/128*SHRT_MAX) ;
				bulSprite->text[Y] = bullet->type*(32.0f/128*SHRT_MAX) + 32.0f/128*SHRT_MAX;
				bulSprite++;

				bulSprite->pos[X] = cx - perpX + dirX;
				bulSprite->pos[Y] = cy - perpY + dirY;
				bulSprite->text[X] = colorCol*(16.0f/128*SHRT_MAX);
				bulSprite->text[Y] = bullet->type*(32.0f/128*SHRT_MAX) ;
				bulSprite++;

				bulSprite->pos[X] = cx + perpX + dirX;
				bulSprite->pos[Y] = cy + perpY + dirY;
				bulSprite->text[X] = colorCol*(16.0f/128*SHRT_MAX) + 16.0f/128*SHRT_MAX;
				bulSprite->text[Y] = bullet->type*(32.0f/128*SHRT_MAX);
				bulSprite++;

				bulSprite->pos[X] = cx + perpX - dirX;
				bulSprite->pos[Y] = cy + perpY - dirY;
				bulSprite->text[X] = colorCol*(16.0f/128*SHRT_MAX) + (16.0f/128*SHRT_MAX);
				bulSprite->text[Y] = bullet->type*(32.0f/128*SHRT_MAX)+32.0f/128*SHRT_MAX;
				bulSprite++;
			}
			
			

			
			numPBulletsIndices += 6;
		}
        
        //printf("bulletDiag='%s'",bulletdiagnostic);
       //Log_Printf("0=%d 1=%d\n",player->bullets[0].expirationTime,player->bullets[1].expirationTime);
	}
}



float ghostDirection[GHOSTS_NUM][2] = 
{
	{0.5,0.86},
	{0.7,0.7},
	{0.86,0.5},
	{0.96,0.25},
	
	{-0.5,0.86},
	{-0.7,0.7},
	{-0.86,0.5},
	{-0.96,0.25},	
};


#define GHOST_DEF_ROT 0.25
float ghostDefaultRotation[GHOSTS_NUM] = 
{
	GHOST_DEF_ROT*1.2f,
	GHOST_DEF_ROT*1.1f,
	GHOST_DEF_ROT,
	GHOST_DEF_ROT*1.9f,
	
	-GHOST_DEF_ROT*1.2f,
	-GHOST_DEF_ROT*1.1f,
	-GHOST_DEF_ROT,
	-GHOST_DEF_ROT*1.9f,	
};



void P_FireGhosts(player_t* player)
{
	int i;
	ghost_t* ghost;
	
	
	if (simulationTime < player->nextGhostFireTime)
		return;
	
	engine.playerStats.bulletsFired[player->playerId] +=8;
	
	SND_PlaySound(SND_GHOST_LAUNCH);
	
	player->nextGhostFireTime = simulationTime + MS_BETWEEN_GHOST;
	
	for (i=0; i < GHOSTS_NUM; i++) 
	{
		
		ghost  = &player->ghosts[i];
	
		ghost->target = 0;
		ghost->lastTimeSimulated = simulationTime;	
		ghost->timeCounter = 0;
		ghost->energy = 5;
		ghost->head = 0;
		ghost->startVertexArray = 0;
		ghost->lengthVertexArray = 0;
	
		vector2Copy(player->ss_position,ghost->ss_position);
		
		ghost->short_ss_position[X] = ghost->ss_position[X] * SS_W;
		ghost->short_ss_position[Y] = ghost->ss_position[Y] * SS_H;		
	
		// TTB: the charged fan is authored screen-up; rotate it with the beat
		// so the volley leads the nose in the side view, like the bullets.
		// Upright (sin 0 / cos 1) this is vector2Copy(ghostDirection[i], ...).
		{
			float ts = sinf(camera.ttbAngle), tc = cosf(camera.ttbAngle);
			ghost->ss_direction[X] =  ghostDirection[i][X] * tc + ghostDirection[i][Y] * ts;
			ghost->ss_direction[Y] = -ghostDirection[i][X] * ts + ghostDirection[i][Y] * tc;
		}

	}

}

void P_UpdateGhosts(player_t* player)
{
	ghost_t* ghost;
	vec2_t ss_normal;
	enemy_t* target = 0;
	float tmpXdirection;
	int i;
	float ghostRotation;
	vec2_t vecEnemy;
	
	target = ENE_GetFirstEnemy();
	
	for (i=0; i < GHOSTS_NUM; i++) 
	{
		
		ghost = &player->ghosts[i];
	
		if (ghost->timeCounter >= GHOST_TTL_MS)
			return;
	
		ghost->timeCounter += timediff;
	
		// No retarget|| (ghost->target != 0 && ghost->target->energy <= 0)))
		if (ghost->timeCounter > GHOST_FREE_TIME_MS && ghost->timeCounter < GHOST_AUTO_AIM_TIME_LIMIT_MS && ghost->target == NULL )
		{
			if (target != NULL)
			{
				while (1) 
				{
					//Only pick target visible on screen
					if (fabs(target->ss_boudaries[UP]) < SS_H || fabs(target->ss_boudaries[DOWN])  > SS_H)
					{
						ghost->target = target;
						ghost->targetUniqueId = target->uniqueId;
						break; 
					}
					
					//No next, aborting target search
					if (target->next == NULL)
						break;  
					
					target = target->next;
				}
					   
					
				//printf("ghost target type=%d , ss_pos[%.2f,%.2f].\n",ghost->target->type,target->ss_position[X],target->ss_position[Y]);
				if (target->next != NULL)
					target = target->next;
			}
		}
		
		//Need to simulate by step GHOST_DELTA_T_MS starting last simulated time	
		while (ghost->lastTimeSimulated + GHOST_DELTA_T_MS < simulationTime) 
		{
		
			ss_normal[X] = - ghost->ss_direction[Y] * GHOST_HALFWIDTH;
			// De-stretch the ghost ribbon's vertical half-width on a tall screen.
			ss_normal[Y] =   ghost->ss_direction[X] * GHOST_HALFWIDTH / gVScale;
		
			//Write left pos in vertex array
			ghost->wayPoints[ghost->head].pos[X] = (ghost->ss_position[X] + ss_normal[X] ) * SS_W;
			ghost->wayPoints[ghost->head].pos[Y] = (ghost->ss_position[Y] + ss_normal[Y] ) * SS_H;
			ghost->head++;
		
			//Write right pos in vertex array
			ghost->wayPoints[ghost->head].pos[X] = (ghost->ss_position[X] - ss_normal[X] ) * SS_W;
			ghost->wayPoints[ghost->head].pos[Y] = (ghost->ss_position[Y] - ss_normal[Y] ) * SS_H;
			ghost->head++;
		
			/*
			if(ghost->target != 0)
			{
				printf("t= %d ghost pos= [%.2f,%.2f].\n",ghost->lastTimeSimulated,ghost->ss_position[X],ghost->ss_position[Y]);
				printf("t= %d ghost dire= [%.2f,%.2f].\n",ghost->lastTimeSimulated,ghost->ss_direction[X],ghost->ss_direction[Y]);
				printf("t= %d target pos= [%.2f,%.2f].\n",ghost->lastTimeSimulated,ghost->target->ss_position[X],ghost->target->ss_position[Y]);
			}
			*/
			//UPDATE POSITION
			ghost->ss_position[X] += GHOST_DELTA_T_MS * GHOST_SPEED_SS_MS * ghost->ss_direction[X];
			ghost->ss_position[Y] += GHOST_DELTA_T_MS * GHOST_SPEED_SS_MS * ghost->ss_direction[Y];

			
			if (ghost->energy > 0 )
			{
			
				//UPDATE DIRECTION (make sure direction remain normalized
				if (ghost->timeCounter < GHOST_FREE_TIME_MS )
				{
					tmpXdirection = ghost->ss_direction[X];
					ghost->ss_direction[X] =  cosf(GHOST_ROT_RAD_PER_DELTA*ghostDefaultRotation[i])*ghost->ss_direction[X]    - sinf(GHOST_ROT_RAD_PER_DELTA*ghostDefaultRotation[i])*ghost->ss_direction[Y];
					ghost->ss_direction[Y] =  sinf(GHOST_ROT_RAD_PER_DELTA*ghostDefaultRotation[i])*tmpXdirection			+ cosf(GHOST_ROT_RAD_PER_DELTA*ghostDefaultRotation[i])*ghost->ss_direction[Y];
				}
				else 
				if (ghost->timeCounter < GHOST_AUTO_AIM_TIME_LIMIT_MS)
				{
					if (ghost->target != 0 && ghost->targetUniqueId == ghost->target->uniqueId && ghost->target->energy > 0 )
					{
					
						//c=a-b
						vector2Subtract(ghost->target->ss_position,ghost->ss_position,vecEnemy);
						//Auto aim
						ghostRotation =  atan2(
											   vecEnemy[X]*-ghost->ss_direction[Y] + vecEnemy[Y]*ghost->ss_direction[X],
												vecEnemy[X]* ghost->ss_direction[X] + vecEnemy[Y]*ghost->ss_direction[Y]  
												 
												);					
						//ghostRotation -= atan2(ghost->ss_direction[Y],ghost->ss_direction[X]);
						
						if (ghostRotation >= 0)
						{
							if (ghostRotation > GHOST_ROT_RAD_PER_DELTA*2 )
								ghostRotation = GHOST_ROT_RAD_PER_DELTA;
						}
						else 
						{
							if (ghostRotation < -(GHOST_ROT_RAD_PER_DELTA*2) )
								ghostRotation = -(GHOST_ROT_RAD_PER_DELTA*2);
						}
					
						//printf("t= %d, gid=%d rotation = %.2f\n",ghost->lastTimeSimulated,i,ghostRotation);
					
					
						tmpXdirection = ghost->ss_direction[X];
						ghost->ss_direction[X] =  cosf(ghostRotation)*ghost->ss_direction[X] - sinf(ghostRotation)*ghost->ss_direction[Y];
						ghost->ss_direction[Y] =  sinf(ghostRotation)*tmpXdirection			 + cosf(ghostRotation)*ghost->ss_direction[Y];
					
					}
				}
				else 
				{
					// Increase speed slowly
					// Also increase tail length
				}
			}

			//	printf("Simulated ghost t=%d x=%.2f y=%.2f.\n",ghost->lastTimeSimulated,ghost->ss_position[X],ghost->ss_position[Y]);
			ghost->lastTimeSimulated += GHOST_DELTA_T_MS;
		
		}
		
		ghost->short_ss_position[X] = ghost->ss_position[X] * SS_W;
		ghost->short_ss_position[Y] = ghost->ss_position[Y] * SS_H;
	}
	
}

void P_PrepareGhostSprites(void)
{
	player_t*		player;
	ghost_t*		ghost;
	int i,j;
	short textureY;
	short stepUShort;
	

	xf_colorless_sprite_t* vertex;
	
	for (i=0; i < numPlayers; i++) 
	{
		player = &players[i];
		
		for (j=0; j < GHOSTS_NUM; j++) 
		{
			ghost = &player->ghosts[j];
		
			if (ghost->timeCounter >= GHOST_TTL_MS)
				continue;
		
			if (ghost->head  - GHOST_TAIL_VERTICES < 0)
			{
				ghost->startVertexArray = 0;
				ghost->lengthVertexArray = ghost->head   ;
			}
			else 
			{
				ghost->startVertexArray = (ghost->head - GHOST_TAIL_VERTICES) ;
				ghost->lengthVertexArray = GHOST_TAIL_VERTICES;
			}
		
			if (ghost->lengthVertexArray == 0)
				continue;
		
		

			stepUShort = SHRT_MAX/(ghost->lengthVertexArray/2);
			
		
			//printf("Ghost has %d vertices stepUShort=%d , stepUByte=%d.\n",ghost->lengthVertexArray,stepUShort,stepUByte);
		
			textureY = SHRT_MAX;
			
		
			vertex = &ghost->wayPoints[ghost->startVertexArray];
			while (vertex < &ghost->wayPoints[ghost->head]) 
			{
			
				//Need to update texture coordinate
				// v2 P3: two ghost-trail columns in the texture; seats 2/3
				// reuse their parent side's ((i&1) -- raw i sampled outside).
				vertex->text[X] = (i & 1) * SHRT_MAX/2;
				vertex->text[Y] = textureY;
				vertex++;

				//Need to update texture coordinate
				vertex->text[X] = (i & 1) * SHRT_MAX/2 + SHRT_MAX/2;
				vertex->text[Y] = textureY;
				vertex++;

				textureY -= stepUShort;
			
			}
		}
	}
		
	
}



// A hull was hit. Solo: the death happens here and now. Multiplayer: nobody
// rules on a death alone anymore -- see NET_PlayerHit (netchannel.c): the host
// applies it and broadcasts the order, everyone else applies the order. Before
// v2.0.9 each device applied its own death first and heard about the others
// later, so two deaths within one latency at a pool of 2 left a DIFFERENT
// ship alive on each screen -- the review's Failure C, now impossible by
// construction: one authority, one order.
void P_Die(uchar playerId)
{
	if (engine.mode == DE_MODE_MULTIPLAYER && NET_DeathAuthority())
	{
		NET_PlayerHit(playerId);
		return;
	}
	P_ApplyDeath(playerId);
}

// The death itself: FX, the shared pool, respawn or RIP, game over. No network
// in here -- in MP it runs on every device in the HOST's order.
void P_ApplyDeath(uchar playerId)
{

	event_t* event;
	event_req_menu_t* eventReqMenu;
	event_req_scene_t* eventReqScene;

	// Already out of lives (RIP: parked off-screen, shouldDraw 0): ignore further
	// "deaths". Stray bullets could hit the parked corpse, push the shared life
	// counter below zero and end the multiplayer match while the OTHER player was
	// still alive. Both lockstep sims skip these the same way, so MP stays in sync.
	if (players[playerId].respawnCounter <= 0 && players[playerId].shouldDraw == 0)
		return;

	// Player collided with the enemy
	// Spawn explosin, smoke and particules
	FX_GetExplosion(players[playerId].ss_position,IMPACT_TYPE_YELLOW,1,0);
    Spawn_EntityParticules(players[playerId].ss_position);
    FX_GetSmoke(players[playerId].ss_position, 0.3, 0.3);
	SND_PlaySound(SND_EXPLOSION);

	// Expire this player's in-flight bullets AND ghosts so they stop hitting
	// enemies (and adding to the score) during the death / respawn animation.
	{
		int j;
		for (j = 0; j < MAX_PLAYER_BULLETS; j++)
			players[playerId].bullets[j].expirationTime = 0;
		for (j = 0; j < GHOSTS_NUM; j++)
			players[playerId].ghosts[j].timeCounter = GHOST_TTL_MS + 1;
	}

	players[playerId].respawnCounter--;

	// Multiplayer: SHARED life pool. Mirror the count onto both players so they
	// draw from the same lives, run out together (finish ~at the same time), and
	// the "both players out" game-over (checked below) fires for both at once --
	// otherwise the match never ends while one player still has lives. Deterministic
	// across peers: lockstep replays the same deaths in the same order.
	if (engine.mode == DE_MODE_MULTIPLAYER)
	{
		int p;
		for (p = 0; p < numPlayers && p < MAX_NUM_PLAYERS; p++)	// v2 P3: N-way mirror
			players[p].respawnCounter = players[playerId].respawnCounter;
	}


	players[playerId].deathPending = 0;	// v2.0.9: whatever was pending, this IS the ruling
	
	players[playerId].invulnerableFor = PLAYER_INVUL_TIME_MS;
	
    // Nasty wrap-around bug here. If we allow respawnCounter to reache zero it will grap around to MAX_UCHAR...
    // This only showed up on android because char are unsigned char....while they were signed char on other
    // compilers used for macosx, windows and ios.
    
    //... or even become the maximun negative value depending if the plaftform treat char as signed or unsigned.
//	if (players[playerId].respawnCounter >= 0)
	if (players[playerId].respawnCounter  > 0)
	{
		//printf("RESPAWN branch lives=%d\n",players[playerId].lives);
		players[playerId].invulFlickering = 0;


		// Set player's position out of screen
		players[playerId].ss_position[X] = P_FormationX(playerId);	// v2 P3: 4-seat formation
		players[playerId].ss_position[Y] = -1.4;


		players[playerId].autopilot.enabled = 1;

		players[playerId].autopilot.end_ss_position[X] = P_FormationX(playerId);
		players[playerId].autopilot.end_ss_position[Y] = P_FormationY(playerId);
		
		players[playerId].autopilot.diff_ss_position[X] = players[playerId].ss_position[X] - players[playerId].autopilot.end_ss_position[X];
		players[playerId].autopilot.diff_ss_position[Y] = players[playerId].ss_position[Y] - players[playerId].autopilot.end_ss_position[Y];
		
		players[playerId].autopilot.timeCounter = PLAYER_RESPAWN_REPLACMENT;
		players[playerId].autopilot.originalTime = PLAYER_RESPAWN_REPLACMENT;
		
		players[playerId].showPointer = SHOW_POINTER_DURATION;
		
	}
	else 
	{
      	//printf("RIP branch lives=%d\n",players[playerId].lives);
		// Set player's position out of screen
		players[playerId].ss_position[X] = P_FormationX(playerId);	// v2 P3: 4-seat formation
		players[playerId].ss_position[Y] = -1.4;


		players[playerId].autopilot.enabled = 1;

		players[playerId].autopilot.end_ss_position[X] = P_FormationX(playerId);
		players[playerId].autopilot.end_ss_position[Y] = -1.4f;
		
		players[playerId].autopilot.diff_ss_position[X] = 0;
		players[playerId].autopilot.diff_ss_position[Y] = 0;
		
		players[playerId].autopilot.timeCounter = 2000000;
		players[playerId].shouldDraw = 0;
		
		
		// v2 P3: the pool is mirrored, so in MP "everyone is out" is simply "the
		// pool is below zero" -- checked on all seats for belt-and-braces (a
		// parked seat's counter is mirrored like any other).
		{
		int everyoneOut = 1;
		int p;
		for (p = 0; p < numPlayers && p < MAX_NUM_PLAYERS; p++)
			if (players[p].respawnCounter >= 0)
				everyoneOut = 0;

		if (((numPlayers == 1) && (playerId == controlledPlayer))     ||
			((numPlayers >= 2) && everyoneOut)
           )
		{

            MENU_SetGameOverScore(P_GetDisplayScore());
            MENU_Set(MENU_GAMEOVER);


			Native_UploadScore(P_GetDisplayScore());
			
			players[playerId].invulnerableFor = 500000;
			
            
            //Request scene 0 and menu 0 for within 3 seconds from now
			event = calloc(1, sizeof(event_t));
			event->type = EV_REQUEST_MENU;
			event->time = simulationTime + 5000;
			eventReqMenu = (event_req_menu_t*)calloc(1,sizeof(event_req_menu_t));
			eventReqMenu->menuId = MENU_HOME;
			event->payload = eventReqMenu;
			EV_AddEvent(event);
			
			event= calloc(1, sizeof(event_t));
			event->type = EV_REQUEST_SCENE;
			event->time = simulationTime + 5000;
			eventReqScene = (event_req_scene_t*)calloc(1,sizeof(event_req_scene_t));
			eventReqScene->sceneId = 0;
			event->payload = eventReqScene;
			EV_AddEvent(event);



		}
		}

	}


	P_UpdateSSBoundaries(playerId);
    
}


void P_UpdateSSBoundaries(uchar pId)
{
	players[pId].ss_boudaries[UP] = (players[pId].ss_position[Y] + 0.03)*SS_H;
	players[pId].ss_boudaries[DOWN] = (players[pId].ss_position[Y] - 0.03)*SS_H;
	players[pId].ss_boudaries[LEFT] = (players[pId].ss_position[X] - 0.04)*SS_W; 
	players[pId].ss_boudaries[RIGHT] = (players[pId].ss_position[X] + 0.04)*SS_W;
	
}

