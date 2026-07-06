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
 *  menu.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-06-19.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

#include "menu.h"
#include "renderer.h"
#include <limits.h>
#include "dEngine.h"
#include "music.h"
#include "timer.h"
#include "netchannel.h"
#include "event.h"
#include "native_services.h"
#include "target.h"

#define HOME_ATLAS "/data/menu/homeAtlas.png"

menu_screen_t menuScreens[10];
int currentMenuId;

texture_t textureAtlas;



void MENU_FreeRessources(void)
{
	TEX_UnloadTexture(&textureAtlas);
	
}


void MENU_LoadRessources(void)
{
	TEX_MakeStaticAvailable(&textureAtlas);
 
}

signed char MENU_Get(void)
{
	return currentMenuId;
}

// pos and dimensions are in screen space coordinate (-renderH/W, + renderH/W)
// text is in [0,1]
void MENU_CreateImage(menu_screen_t* screen, vec2_t pos, vec2_t dimensions, vec2_t textPos, vec2_t textDim)
{
	menu_image_t* image;
	
	image = &screen->images[screen->numImages];
	
	image->vertices[0].pos[X] = pos[X] - dimensions[WIDTH ]/2;
	image->vertices[0].pos[Y] = pos[Y] + dimensions[HEIGHT]/2 ;
	image->vertices[0].text[X] = textPos[U]*SHRT_MAX;
	image->vertices[0].text[Y] = textPos[V]*SHRT_MAX;
	
	image->vertices[1].pos[X] = pos[X] - dimensions[WIDTH ]/2;
	image->vertices[1].pos[Y] = pos[Y] - dimensions[HEIGHT]/2 ;
	image->vertices[1].text[X] = textPos[U]*SHRT_MAX;
	image->vertices[1].text[Y] = (textPos[V]+textDim[HEIGHT])*SHRT_MAX;
	
	image->vertices[2].pos[X] = pos[X] + dimensions[WIDTH ]/2;
	image->vertices[2].pos[Y] = pos[Y] + dimensions[HEIGHT]/2 ;
	image->vertices[2].text[X] = (textPos[U]+textDim[WIDTH] )*SHRT_MAX;
	image->vertices[2].text[Y] = textPos[V]					 *SHRT_MAX;
	
	image->vertices[3].pos[X] = pos[X] + dimensions[WIDTH ]/2;
	image->vertices[3].pos[Y] = pos[Y] - dimensions[HEIGHT]/2 ;
	image->vertices[3].text[X] = (textPos[U]	+textDim[WIDTH])*SHRT_MAX;
	image->vertices[3].text[Y] = (textPos[V]+textDim[HEIGHT])*SHRT_MAX;
	
	screen->numImages++;
}

void MENU_CreateImageWithVerticalText(menu_screen_t* screen, vec2_t pos, vec2_t dimensions,vec2_t textPos, vec2_t textDim)
{
	vec2short_t tmp;
	menu_image_t* image;
	MENU_CreateImage(screen,pos,dimensions,textPos,textDim);
	image = &screen->images[screen->numImages-1];
	
	//Rotate point -90 degres
	/*
	 
	 0 2   -> 1 0
	 1 3      3 2
	 */
	
	vector2Copy(image->vertices[0].text,tmp);
	
	vector2Copy(image->vertices[1].text,image->vertices[0].text);
	vector2Copy(image->vertices[3].text,image->vertices[1].text);
	vector2Copy(image->vertices[2].text,image->vertices[3].text);
	vector2Copy(tmp                    ,image->vertices[2].text);
	
	
}

void MENU_CreateText(menu_screen_t* screen, short posX, short posY, float size,uchar centerStyle,char* text)
{
	menu_text_t* menu;
	
	menu = &screen->texts[screen->numTexts];
	
	menu->text = calloc(256, sizeof(char));
	strcpy(menu->text,text);
	menu->font_size = size;
	menu->textPos[X] = posX;
	menu->textPos[Y] = posY;

	menu->centerStyle = centerStyle;
	
	screen->numTexts++;
}

#define BUTTON_TEXT_X_COO (0/(float)512*SHRT_MAX)
#define BUTTON_TEXT_Y_COO (104/(float)512*SHRT_MAX)
#define BUTTON_TEXT_WIDTH (159/(float)512*SHRT_MAX)
#define BUTTON_TEXT_HEIGHT (64/(float)512*SHRT_MAX)
void MENU_CreateButtonWithTag(menu_screen_t* screen,char* text,float fontSize, buttonAction action,void* tag,buttonUpdate update,
					   vec2short_t pos, vec2short_t dimensions
					   )
{
	int i;
	menu_button_t* button;
	
	button = &screen->buttons[screen->numButtons];
	
	button->text = text;
	button->tag = tag;
	button->update = update;
	button->action = action;
	button->touch = &screen->touches[screen->numButtons];
	button->actionTriggered = 0;
	button->font_size = fontSize;
	
	
	button->upVertices[0].pos[X] = pos[X] - dimensions[WIDTH]/2;
	button->upVertices[0].pos[Y] = pos[Y] + dimensions[HEIGHT]/2;
	button->upVertices[0].text[X] = BUTTON_TEXT_X_COO;
	button->upVertices[0].text[Y] = BUTTON_TEXT_Y_COO;
	
	button->upVertices[1].pos[X] = pos[X]-dimensions[WIDTH]/2;
	button->upVertices[1].pos[Y] = pos[Y] - dimensions[HEIGHT]/2;
	button->upVertices[1].text[X] = BUTTON_TEXT_X_COO;
	button->upVertices[1].text[Y] = BUTTON_TEXT_Y_COO + BUTTON_TEXT_HEIGHT;
	
	button->upVertices[2].pos[X] = pos[X] + dimensions[WIDTH]/2;
	button->upVertices[2].pos[Y] = pos[Y] + dimensions[HEIGHT]/2;
	button->upVertices[2].text[X] = BUTTON_TEXT_X_COO + BUTTON_TEXT_WIDTH;
	button->upVertices[2].text[Y] = BUTTON_TEXT_Y_COO;
	
	button->upVertices[3].pos[X] = pos[X] + dimensions[WIDTH]/2;
	button->upVertices[3].pos[Y] = pos[Y] - dimensions[HEIGHT]/2;
	button->upVertices[3].text[X] = BUTTON_TEXT_X_COO + BUTTON_TEXT_WIDTH;
	button->upVertices[3].text[Y] = BUTTON_TEXT_Y_COO + BUTTON_TEXT_HEIGHT;	
	
	for (i=0 ; i < 4; i++) 
	{
		button->downVertices[i].pos[X] = button->upVertices[i].pos[X];
		button->downVertices[i].pos[Y] = button->upVertices[i].pos[Y];
		button->downVertices[i].text[X] = button->upVertices[i].text[X] + BUTTON_TEXT_WIDTH;
		button->downVertices[i].text[Y] = button->upVertices[i].text[Y];
	}
	
	//Moving from a center 0,0 coordinate system to an upper left 0,0 coordinate system
	button->touch->iphone_coo_SysPos[X] = (pos[X] + SS_W) / 2 ;
	button->touch->iphone_coo_SysPos[Y] = SS_H - (pos[Y] + SS_H)/2 ;
	button->touch->down = 0;
	
	button->textPos[X] = pos[X];
	button->textPos[Y] = pos[Y];
	
	screen->numButtons++;
}

void MENU_CreateButton(menu_screen_t* screen,char* text,float fontSize, buttonAction action,buttonUpdate update,
					   vec2short_t pos, vec2short_t dimensions
					   )
{
	MENU_CreateButtonWithTag(screen,text,fontSize,action,NULL,update,pos,dimensions);
}

void Action_GoToTutorial(void* tag)
{
	int i;
	
	PL_ResetPlayersScore();
	for(i=0 ; i < MAX_NUM_PLAYERS ; i++)
		players[i].respawnCounter = numPlayerRespawn[DIFFICULTY_NORMAL];
	
	if (engine.controlMode == CONTROL_MODE_SWIP)
	{	
		dEngine_RequireSceneId(14);
		MENU_Set(MENU_NONE);
	}
	else 
	{
		dEngine_RequireSceneId(15);
		MENU_Set(MENU_NONE);
	}

		
}


void Action_ReplayAct(void* tag)
{
	menu_button_t* button;
	int actToPlay;
	int* buttonPressed = (int*)tag;
	
	button = &menuScreens[currentMenuId].buttons[*buttonPressed];
	
	actToPlay = '0' - button->text[5];
	
	engine.playback.play = 1;
	
	dEngine_RequireSceneId(actToPlay);
//	engine.requiredSceneId = actToPlay;
	MENU_Set(MENU_NONE);	
	//Use lastButtonPressed
}

void Action_UploadAct(void* tag)
{
	Native_UploadFileTo((char*)tag);
}


char replayList[10][256];
int numReplayers=0;
void MENU_UpdateReplayList(void)
{
	int i;
	menu_screen_t* screen;
	vec2short_t buttonPos;
	vec2short_t buttonDim;
	int* buttonId;
	int actId;
	screen = &menuScreens[MENU_REPLAY];
	
	numReplayers = Native_RetrieveListOf(replayList);
	
	//Free all texts char* for buttons
	Log_Printf("[MENU_UpdateReplayList] MEMORY LEAK HERE !!\n");
	/*
	if (screen->numButtons > 2 )
	{
		for (i=2; i< screen->numButtons ; i++) {
			free(screen->buttons[i].text);
		}
		screen->numButtons = 2;
	}
	*/
	
	//Check if the name should be changed
	/*
	if (engine.playback.record)
		strcpy(screen->buttons[1].text,"Disable Recording");
	else
		strcpy(screen->buttons[1].text,"Enable Recording");
	*/
	
	buttonPos[X] = 0; 
	buttonPos[Y] = (SS_COO_SYST_HEIGHT - 30);
	buttonDim[WIDTH] = (120 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	
	for (i=0; i <numReplayers; i++) 
	{
		buttonId = calloc(1, sizeof(int));
		*buttonId = i;
		
		buttonPos[Y] = SS_COO_SYST_HEIGHT - 200 -  i * 100; 
		buttonPos[X] = 150; 

		
		actId = (replayList[i][0] -48)*10 + (replayList[i][1] -48)  ;
		Log_Printf("actId=%d\n",actId);
		if (actId >= engine.numScenes)
			continue;
		
		//MENU_CreateText(screen,-40, buttonPos[Y],TEXT_NOT_CENTERED,engine.scenes[actId].name);
		
		
		MENU_CreateButtonWithTag(screen,"Replay", 3, Action_ReplayAct, buttonId ,NULL, buttonPos, buttonDim);

		buttonPos[Y] += 50; 
		buttonPos[X] = 150 ;
		MENU_CreateButtonWithTag(screen,"Upload", 3, Action_UploadAct,replayList[i],NULL, buttonPos, buttonDim);

		

	}
}

void Action_ChangeReplayRecordingState(void* tag)
{
	engine.playback.record = !engine.playback.record;
	Log_Printf("[Action_ChangeReplayRecordingState] engine.playback.record=%d\n",engine.playback.record);
	MENU_UpdateReplayList();
}



void MENU_Set(signed char menuId)
{
	int i;
	menu_screen_t* currentMenu;
	
	//Log_Printf("MENU_Set(%d)\n",menuId);
	
	
	if (currentMenuId != -1)
	{
		
		currentMenu = &menuScreens[currentMenuId];
		
		//Need to reset button stats for next time the menu is displayed
		for (i=0 ; i < currentMenu->numButtons; i++) 
		{
			currentMenu->buttons[i].actionTriggered = 0;
			currentMenu->buttons[i].touch->down= 0;
		}
	}
	
	engine.menuVisible = (menuId != MENU_NONE);
	
	
	currentMenuId = menuId;
	if (currentMenuId != MENU_NONE)
	{
		SCR_SetFadeFullScreen();
		currentMenu = &menuScreens[currentMenuId];
		currentMenu->alpha = 0;
	}
	
	
	if (menuId == MENU_NONE)
	{
		MENU_FreeRessources();
		return;
	}
	else 
	{
		MENU_LoadRessources();
	}
	
	
	
	
	
}

void Action_PlayDemo(void* tag)
{
	int i;
	char* actId = (char*)tag;
	
	PL_ResetPlayersScore();
	for(i=0 ; i < MAX_NUM_PLAYERS ; i++)
		players[i].respawnCounter = numPlayerRespawn[DIFFICULTY_NORMAL];
	
	engine.difficultyLevel = DIFFICULTY_NORMAL;
	
	dEngine_RequireSceneId(*actId);
	MENU_Set(MENU_NONE);
    
}

void Action_GoToAct(void* tag)
{
	char* actId = (char*)tag;
	dEngine_RequireSceneId(*actId);
	MENU_Set(MENU_NONE);
}


// Difficulty picked on the SELECT_DIFFICULTY screen, applied when an act is
// chosen on the SELECT_ACT screen that follows.
static uchar gPickedDifficulty = 1;

void Action_startNewGame(void* tag)
{
	int i;
	uchar difficultyLevel;

	difficultyLevel = *(char*)tag;

	MENU_Set(MENU_NONE);
	dEngine_RequireSceneId(1);

	//Parameter set the difficulty level

	engine.mode = DE_MODE_SINGLEPLAYER;
	PL_ResetPlayersScore();
	numPlayers = 1;


	for(i=0 ; i < MAX_NUM_PLAYERS ; i++)
		players[i].respawnCounter = numPlayerRespawn[difficultyLevel];

	engine.difficultyLevel = difficultyLevel;
}

// New Game flow: pick a difficulty, then pick the starting act (full lives
// either way). Act select makes practicing an act -- and reaching the act-3
// boss -- possible without clearing the whole game in one run. Acts are only
// selectable once they have been REACHED in play (gHighestActReached).
static const char* actRoman[] = { "", "I", "II", "III" };

// texts[1] of the act-select screen is its status line.
static void MENU_UpdateActLockStatus(int lockedActTried)
{
	char* line = menuScreens[MENU_SELECT_ACT].texts[1].text;

	if (lockedActTried > 1)
		sprintf(line, "Locked - finish Act %s first", actRoman[lockedActTried - 1]);
	else if (gHighestActReached >= 2)
		sprintf(line, "Unlocked up to Act %s", actRoman[gHighestActReached]);
	else
		line[0] = '\0';
}

void Action_PickDifficulty(void* tag)
{
	gPickedDifficulty = *(char*)tag;
	MENU_UpdateActLockStatus(0);
	MENU_Set(MENU_SELECT_ACT);
}

void Action_startNewGameAtAct(void* tag)
{
	int i;
	int sceneId = *(char*)tag;	// 1..3 = Act I..III

	// Progression gate: the act must have been reached in play at least once.
	if (sceneId > gHighestActReached)
	{
		MENU_UpdateActLockStatus(sceneId);
		MENU_ClearButtonStates();
		return;
	}

	MENU_Set(MENU_NONE);
	dEngine_RequireSceneId(sceneId);

	engine.mode = DE_MODE_SINGLEPLAYER;
	PL_ResetPlayersScore();
	numPlayers = 1;

	for(i=0 ; i < MAX_NUM_PLAYERS ; i++)
		players[i].respawnCounter = numPlayerRespawn[gPickedDifficulty];

	engine.difficultyLevel = gPickedDifficulty;
}

void Action_SpecifyDifficulty(void* tag)
{
	MENU_Set(MENU_SELECT_DIFFICULTY);
}

void Action_startMultiplayerGame(void* tag)
{
	
}

void Action_PreGoToGameCenter(void* tag)
{
	MENU_Set(MENU_OTHERS);
	
	if (engine.gameCenterEnabled)
		Action_ShowGameCenter(tag);
	else 
	{
		Native_LoginGameCenter();
	}

}

void Action_ConfigureMultiplayer(void* tag)
{
	int i;

	MENU_Set(MENU_MULTIPLAYER);
	engine.mode = DE_MODE_MULTIPLAYER;
	NET_Init();
	PL_ResetPlayersScore();

	// Shared life pool in multiplayer: 2 players' worth (3+3=6), since both players
	// draw from the same pool (mirrored on each death in P_Die).
	for(i=0 ; i < MAX_NUM_PLAYERS ; i++)
		players[i].respawnCounter = 2 * numPlayerRespawn[DIFFICULTY_NORMAL];

	engine.difficultyLevel = DIFFICULTY_NORMAL;
}

#ifdef __APPLE__
// Online multiplayer over Game Center (GKMatch): same as the LAN setup, but the
// transport is GameKit instead of Bonjour/UDP and matchmaking is driven by Apple.
void Action_ConfigureOnlineMultiplayer(void* tag)
{
	int i;

	MENU_Set(MENU_MULTIPLAYER);
	engine.mode = DE_MODE_MULTIPLAYER;
	NET_Init();								// resets net (transport defaults to LAN)...
	net.transport = NET_TRANSPORT_GAMECENTER;	// ...then switch this session to online
	PL_ResetPlayersScore();

	// Shared life pool in multiplayer: 2 players' worth (3+3=6), mirrored on death.
	for(i=0 ; i < MAX_NUM_PLAYERS ; i++)
		players[i].respawnCounter = 2 * numPlayerRespawn[DIFFICULTY_NORMAL];

	engine.difficultyLevel = DIFFICULTY_NORMAL;

	sprintf(MENU_GetMultiplayerTextLine(0), "Finding an opponent...");
	Native_StartOnlineMatchmaking();		// presents the Game Center matchmaker UI
}
#endif

#ifdef SHMUP_TARGET_ANDROID  
#include "native_URL.h"
void Action_GoBuyFullVersion(void* tag)
{
    goToURL("market://details?id=net.fabiensanglard.shmup");
}
#endif

void replayLastGame(void){}
void doNothing(void){}

void Action_ShowCreditsMenu(void* tag)
{
	MENU_Set(MENU_CREDITS);
}

void Action_ShowHomeMenu(void* tag)
{
	MENU_Set(MENU_HOME);
}

void Action_ShowOthersMenu(void* tag)
{
	MENU_Set(MENU_OTHERS);
}

// Reflect the current ship + bullet-colour choice in the Custom screen's status line
// (texts[1]). Default is Ship 1 / Red (gShipChoice = gBulletColor = 0).
static const char* customColorNames[NUM_BULLET_COLORS] = { "Red", "Blue", "Invisible", "Yellow" };
static void MENU_UpdateCustomSelection(void)
{
	const char* col = (gBulletColor >= 0 && gBulletColor < NUM_BULLET_COLORS) ? customColorNames[gBulletColor] : "?";
	int ship = (gShipChoice >= 0 && gShipChoice < NUM_SHIP_CHOICES) ? gShipChoice + 1 : 1;
	sprintf(menuScreens[MENU_SELECT_SHIP].texts[1].text, "Ship %d  -  %s", ship, col);
}

void Action_ShowShipMenu(void* tag)
{
	MENU_UpdateCustomSelection();	// reflect the current choice when entering
	MENU_Set(MENU_SELECT_SHIP);
}

void Action_SelectShip(void* tag)
{
	// tag points to the chosen ship index. STAY on the Custom screen (you leave via
	// Back) and refresh the status line so the current selection is visible.
	int choice = *(int*)tag;
	if (choice >= 0 && choice < NUM_SHIP_CHOICES)
		gShipChoice = choice;
	MENU_UpdateCustomSelection();
	MENU_ClearButtonStates();	// don't leave the just-pressed button highlighted
#ifdef __APPLE__
	Native_SaveLoadout(gShipChoice, gBulletColor);	// persist across restarts
#endif
}

void Action_SelectBulletColor(void* tag)
{
	// tag points to the chosen bullet-colour column. STAY on the Custom screen.
	int choice = *(int*)tag;
	if (choice >= 0 && choice < NUM_BULLET_COLORS)
		gBulletColor = choice;
	MENU_UpdateCustomSelection();
	MENU_ClearButtonStates();	// don't leave the just-pressed button highlighted
#ifdef __APPLE__
	Native_SaveLoadout(gShipChoice, gBulletColor);	// persist across restarts
#endif
}


void Action_GoToReplayScreen(void* tag)
{
	MENU_Set(MENU_REPLAY);
}


void Action_BackToHome(void* tag)
{
	NET_Free();
	MENU_Set(MENU_HOME);
}

void Action_BackToOptions(void* tag)
{
	NET_Free();
	MENU_Set(MENU_OTHERS);
}

void Action_BackToHomeAfterGameOver(void* tag)
{
	
	
	NET_Free();
	MENU_Set(MENU_HOME);
	dEngine_RequireSceneId(0);
	
}


char* MENU_GetMultiplayerTextLine(int i)
{
	return menuScreens[MENU_MULTIPLAYER].texts[i].text;
}

void MENU_SetGameOverScore(unsigned int score)
{
	// texts[0] of the game-over screen is the score line (created in MENU_Init).
	sprintf(menuScreens[MENU_GAMEOVER].texts[0].text, "SCORE: %u", score);
}


//  0  2
//  1  3
char menuCreated = 0;
void MENU_Init(void)
{
	char* recordUpdatableString;
	menu_screen_t* currentMenu;
	
	vec2_t pos ;
	vec2_t dimensions;
	vec2_t textPos;
	vec2_t textDim;
	char* actId;
	char* difficultyLevel;
	
	vec2short_t buttonPos;
	vec2short_t buttonDim;
	
	if (menuCreated)
		return;
	
	Log_Printf("[Menu System] Initializing...\n");
	
	memset(menuScreens,0,sizeof(menuScreens));
	
//	textureAtlas.path = calloc(strlen(HOME_ATLAS)+1, sizeof(char));
	strcpy(textureAtlas.path,HOME_ATLAS );
	
	
	currentMenu = &menuScreens[MENU_HOME];	

	// TITLE IMAGE
	pos[X] = 0 ; 
	pos[Y] = SS_COO_SYST_HEIGHT - 120 - renderer.safeInsetTopPx * (2.0f * SS_H / (float)renderer.glBuffersDimensions[HEIGHT]) - 55;
	dimensions[WIDTH]  =  261 *2.2;
	dimensions[HEIGHT] =  102 *2.2;
	textPos[X] =  0/(float)512;
	textPos[Y] =  5/(float)512;
	textDim[WIDTH] = 261 /(float)512;
	textDim[HEIGHT]= 100 /(float)512;
	MENU_CreateImage(currentMenu,pos,dimensions,textPos,textDim);
	
	// TITLE KANJI IMAGE
	pos[X] = 0 ; 
	pos[Y] = SS_COO_SYST_HEIGHT - 350 ;
	dimensions[WIDTH] = 159*0.75 ;
	dimensions[HEIGHT] = 305*0.75;
	textPos[X] = 0 ; 
	textPos[Y] = 207/(float)512 ;
	textDim[WIDTH] = 159/(float)512 ; 
	textDim[HEIGHT] =305/(float)512 ;
	//MENU_CreateImage(menu_screen_t* screen, vec2_t pos, vec2_t dimensions, vec2_t text[4])
	MENU_CreateImage(currentMenu,pos,dimensions,textPos,textDim);
	
	buttonPos[X] = 0;; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 270);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "New Game", 3, Action_SpecifyDifficulty,NULL, buttonPos, buttonDim);

	
	
	
	
	buttonPos[X] = 160 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 120);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Others", 3, Action_ShowOthersMenu,NULL, buttonPos, buttonDim);

	buttonPos[X] = -160 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 120);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	actId = calloc(1, sizeof(char));
	MENU_CreateButton(currentMenu, "Tutorial", 3, Action_GoToTutorial,NULL, buttonPos, buttonDim);

	
	
	currentMenu = &menuScreens[MENU_CREDITS];
	
	// CREDIT TITLE IMAGE
	pos[X] = 0 ; 
	pos[Y] = ((SS_COO_SYST_HEIGHT - 140)) - renderer.safeInsetTopPx * (2.0f * SS_H / (float)renderer.glBuffersDimensions[HEIGHT]) - 55 ;
	dimensions[WIDTH] = 261*2.1; 
	dimensions[HEIGHT] = 104*2.1;
	textPos[X] = 251/(float)512 ; 
	textPos[Y] = 407/(float)512 ;
	textDim[WIDTH] = 261/(float)512 ; 
	textDim[HEIGHT] =104/(float)512 ;
	
	MENU_CreateImage(currentMenu,pos,dimensions,textPos,textDim);
	
	
	buttonPos[X] = 0 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 100);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Back", 3, Action_ShowOthersMenu,NULL, buttonPos, buttonDim);

	MENU_CreateText(currentMenu,0,200,2.2f,TEXT_CENTERED,  "Producer:     Fabien Sanglard"); 
	MENU_CreateText(currentMenu,0,150,2.2f,TEXT_CENTERED,  "Game engine:  Fabien Sanglard"); 
	MENU_CreateText(currentMenu,0,100,2.2f,TEXT_CENTERED,  "Graphics:     Fabien Sanglard"); 
	MENU_CreateText(currentMenu,0, 50,2.2f,TEXT_CENTERED,  "Music:            Future Crew"); 
	
	pos[X] = 0 ; 
	pos[Y] = 3 ;
	dimensions[WIDTH] = 240*2.0; 
	dimensions[HEIGHT] = 7*2.1;
	textPos[X] = 7/(float)512 ; 
	textPos[Y] = 85/(float)512 ;
	textDim[WIDTH] = 251/(float)512 ; 
	textDim[HEIGHT] =6/(float)512 ;
	//MENU_CreateImage(menu_screen_t* screen, vec2_t pos, vec2_t dimensions, vec2_t text[4])
	MENU_CreateImage(currentMenu,pos,dimensions,textPos,textDim);
	
	
	MENU_CreateText(currentMenu,0, -20,2.2f,TEXT_CENTERED, "Artists:          Phil Walker");
	MENU_CreateText(currentMenu,0, -60,2.2f,TEXT_CENTERED, "                  Mike Jensen");
	MENU_CreateText(currentMenu,0, -100,2.2f,TEXT_CENTERED,"                 Sean Weisman");	
	
	pos[X] = 0 ; 
	pos[Y] =  -133;
	dimensions[WIDTH] = 240*2.0; 
	dimensions[HEIGHT] = 7*2.1;
	textPos[X] = 7/(float)512 ; 
	textPos[Y] = 85/(float)512 ;
	textDim[WIDTH] = 251/(float)512 ; 
	textDim[HEIGHT] =6/(float)512 ;
	
	MENU_CreateImage(currentMenu,pos,dimensions,textPos,textDim);
	
	MENU_CreateText(currentMenu,0, -170,2.2f,TEXT_CENTERED,"Special Thanks:     Soojin Yi");			
	MENU_CreateText(currentMenu,0, -220,2.2f,TEXT_CENTERED,"                Jeremy Vernet");	
	MENU_CreateText(currentMenu,0, -270,2.2f,TEXT_CENTERED,"            Aurelien Sanglard");


		
	

	
	
	currentMenu = &menuScreens[MENU_MULTIPLAYER];
	
	MENU_CreateText(currentMenu,0,200,2,TEXT_CENTERED,"");
	MENU_CreateText(currentMenu,0,150,2,TEXT_CENTERED,"");
	MENU_CreateText(currentMenu,0,100,2,TEXT_CENTERED,"");
	MENU_CreateText(currentMenu,0, 50,2,TEXT_CENTERED,"");
	MENU_CreateText(currentMenu,0,  0,2,TEXT_CENTERED,"");
	MENU_CreateText(currentMenu,0,-50,2,TEXT_CENTERED,"");
	
	pos[X] = 0 ; 
	pos[Y] = ((SS_COO_SYST_HEIGHT - 140)) - renderer.safeInsetTopPx * (2.0f * SS_H / (float)renderer.glBuffersDimensions[HEIGHT]) - 55 ;
	dimensions[WIDTH] = 261*2.1; 
	dimensions[HEIGHT] = 104*2.1;
	textPos[X] = 321/(float)512 ; 
	textPos[Y] = 162/(float)512 ;
	textDim[WIDTH] = 86/(float)512 ; 
	textDim[HEIGHT] =249/(float)512 ;
	MENU_CreateImageWithVerticalText(currentMenu,pos,dimensions,textPos,textDim);

	
	buttonPos[X] = 0 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 120);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Back", 3, Action_BackToOptions,NULL, buttonPos, buttonDim);
	
	buttonPos[X] = 0 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT - 120);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "New Game", 3, Action_startMultiplayerGame,NULL, buttonPos, buttonDim);
	
	
	
	
	currentMenu = &menuScreens[MENU_GAMEOVER];
	currentMenu->numImages = 0;
	currentMenu->numTexts = 0;
	currentMenu->numButtons = 0;
	
	
	
	
	buttonPos[X] = 0 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT+ 120);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Back", 3, Action_BackToHomeAfterGameOver,NULL, buttonPos, buttonDim);
	
	pos[X] = 0 ; 
	pos[Y] = (SS_COO_SYST_HEIGHT - 180) - renderer.safeInsetTopPx * (2.0f * SS_H / (float)renderer.glBuffersDimensions[HEIGHT]) - 55 ;
	dimensions[WIDTH] = 261*2.2 ; 
	dimensions[HEIGHT] = 154*2.2;
	textPos[X] = 176/(float)512 ; 
	textPos[Y] = 168  /(float)512 ;
	textDim[WIDTH] = 144/(float)512 ; 
	textDim[HEIGHT] =247/(float)512 ;
	MENU_CreateImageWithVerticalText(currentMenu,pos,dimensions,textPos,textDim);

	// Final score line (texts[0]); filled in by MENU_SetGameOverScore() from
	// P_Die when the player runs out of lives.
	MENU_CreateText(currentMenu, 0, -150, 3.0f, TEXT_CENTERED, "SCORE: 0");


	currentMenu = &menuScreens[MENU_REPLAY];
	currentMenu->numImages = 0;
	currentMenu->numTexts = 0;
	currentMenu->numButtons = 0;
	
	buttonPos[X] = 0 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 90);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Back", 3, Action_ShowHomeMenu,NULL, buttonPos, buttonDim);
	
	buttonPos[X] = 0 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 260);
	buttonDim[WIDTH] = (220 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	recordUpdatableString = calloc(21, sizeof(char));
	//MENU_CreateButton(currentMenu, recordUpdatableString, 2.5f, Action_ChangeReplayRecordingState,MENU_UpdateReplayList, buttonPos, buttonDim);
	
	
	
	currentMenu = &menuScreens[MENU_OTHERS];
	
	
	pos[X] = 0 ; 
	pos[Y] = ((SS_COO_SYST_HEIGHT - 140)) - renderer.safeInsetTopPx * (2.0f * SS_H / (float)renderer.glBuffersDimensions[HEIGHT]) - 55 ;
	dimensions[WIDTH] = 261*2.1; 
	dimensions[HEIGHT] = 104*2.1;
	textPos[X] = 421/(float)512 ; 
	textPos[Y] = 186/(float)512 ;
	textDim[WIDTH] = 80/(float)512 ; 
	textDim[HEIGHT] =216/(float)512 ;
	MENU_CreateImageWithVerticalText(currentMenu,pos,dimensions,textPos,textDim);
	
	
	buttonPos[X] = -160;
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 510);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	actId = calloc(1, sizeof(char));
	*actId = 13 ;
	MENU_CreateButtonWithTag(currentMenu, "Demo", 3, Action_PlayDemo,actId,NULL, buttonPos, buttonDim);
	
	buttonPos[X] = -160 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 380);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Credits", 3, Action_ShowCreditsMenu,NULL, buttonPos, buttonDim);
	
	
#ifndef SHMUP_TARGET_ANDROID
	// Multiplayer: LAN peer-to-peer over Bonjour/DNS-SD. Modern iOS gates that
	// behind the Local Network permission, declared in the Info.plist
	// (NSLocalNetworkUsageDescription + NSBonjourServices = _DodgeServer._udp).
	buttonPos[X] = -160 ;
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 250);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Local Network", 2.0f, Action_ConfigureMultiplayer,NULL, buttonPos, buttonDim);
#endif

#ifdef __APPLE__
	// Online multiplayer over Game Center (GKMatch): matchmaking + NAT traversal
	// handled by Apple, so it plays beyond the LAN. Both players must be signed
	// into Game Center.
	buttonPos[X] = 160 ;
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 250);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Online", 3, Action_ConfigureOnlineMultiplayer,NULL, buttonPos, buttonDim);
#endif

	// Game Center "High Scores" leaderboard viewer.
	buttonPos[X] = 160 ;
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 380);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Scores", 3, Action_ShowGameCenter,NULL, buttonPos, buttonDim);

	// Solo ship + bullet-colour selection (top-right).
	buttonPos[X] = 160 ;
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 510);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Custom", 3, Action_ShowShipMenu,NULL, buttonPos, buttonDim);
//
//	if (engine.gameCenterPossible)
//    {
//        buttonPos[X] = 160 ; 
//        buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 370);
//        buttonDim[WIDTH] = (159 * 2);
//        buttonDim[HEIGHT] = 64 * 2;
//        MENU_CreateButton(currentMenu, "GameCenter", 3, Action_PreGoToGameCenter,NULL, buttonPos, buttonDim);
//    }
    
//On Android and limited edition we have a button to help go to the game.    

	
	
	buttonPos[X] = 160 ; 
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 120);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	actId = calloc(1, sizeof(char));
	*actId = 15 ;
	MENU_CreateButton(currentMenu, "Back", 3, Action_ShowHomeMenu,NULL, buttonPos, buttonDim);

	buttonPos[X] = -160 ; 
	buttonPos[Y] = (-SS_H + 220);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	//MENU_CreateButton(currentMenu, "Replays", 3, Action_GoToReplayScreen,MENU_UpdateReplayList, buttonPos, buttonDim);
	
	
	currentMenu = &menuScreens[MENU_SELECT_DIFFICULTY];
	// CREDIT TITLE IMAGE
	pos[X] = 0 ; 
	pos[Y] = ((SS_COO_SYST_HEIGHT - 140)) - renderer.safeInsetTopPx * (2.0f * SS_H / (float)renderer.glBuffersDimensions[HEIGHT]) - 55 ;
	dimensions[WIDTH] = 261*2.1; 
	dimensions[HEIGHT] = 104*2.1;
	textPos[X] = 271/(float)512 ; 
	textPos[Y] = 0/(float)512 ;
	textDim[WIDTH] = 240/(float)512 ; 
	textDim[HEIGHT] =96/(float)512 ;	
	MENU_CreateImage(currentMenu,pos,dimensions,textPos,textDim);
	
	// Difficulty buttons pulled up so the bottom Back button (standard slot below) has
	// its own row and no longer overlaps Insane.
	buttonPos[X] = 0 ;
	buttonPos[Y] = (SS_H - 360);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	difficultyLevel = calloc(1, sizeof(int));
	*difficultyLevel = DIFFICULTY_EASY;
	MENU_CreateButtonWithTag(currentMenu, "Easy", 3, Action_PickDifficulty,difficultyLevel,NULL, buttonPos, buttonDim);

	buttonPos[X] = 0 ;
	buttonPos[Y] = (SS_H - 510);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	difficultyLevel = calloc(1, sizeof(int));
	*difficultyLevel = DIFFICULTY_NORMAL;
	MENU_CreateButtonWithTag(currentMenu, "Normal", 3, Action_PickDifficulty,difficultyLevel,NULL, buttonPos, buttonDim);

	buttonPos[X] =  0 ;
	buttonPos[Y] = (SS_H - 660);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	difficultyLevel = calloc(1, sizeof(int));
	*difficultyLevel = DIFFICULTY_INSANE;
	MENU_CreateButtonWithTag(currentMenu, "Insane", 3, Action_PickDifficulty,difficultyLevel,NULL, buttonPos, buttonDim);

	// Back to the main menu (so New Game -> difficulty selection is escapable).
	buttonPos[X] = 0 ;
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 120);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Back", 3, Action_ShowHomeMenu,NULL, buttonPos, buttonDim);


	// --- Act select (solo), after the difficulty pick: start at any act with full
	// lives. Practicing an act (and reaching the act-3 boss) no longer requires
	// clearing the whole game in one run.
	currentMenu = &menuScreens[MENU_SELECT_ACT];

	MENU_CreateText(currentMenu, 0, (SS_H - 140), 3.0f, TEXT_CENTERED, "SELECT ACT");
	// texts[1]: lock/progress status line (filled by MENU_UpdateActLockStatus).
	MENU_CreateText(currentMenu, 0, (SS_H - 230), 2.0f, TEXT_CENTERED, "");

	buttonPos[X] = 0 ;
	buttonPos[Y] = (SS_H - 360);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	actId = calloc(1, sizeof(char));
	*actId = 1;
	MENU_CreateButtonWithTag(currentMenu, "Act I", 3, Action_startNewGameAtAct,actId,NULL, buttonPos, buttonDim);

	buttonPos[X] = 0 ;
	buttonPos[Y] = (SS_H - 510);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	actId = calloc(1, sizeof(char));
	*actId = 2;
	MENU_CreateButtonWithTag(currentMenu, "Act II", 3, Action_startNewGameAtAct,actId,NULL, buttonPos, buttonDim);

	buttonPos[X] = 0 ;
	buttonPos[Y] = (SS_H - 660);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	actId = calloc(1, sizeof(char));
	*actId = 3;
	MENU_CreateButtonWithTag(currentMenu, "Act III", 3, Action_startNewGameAtAct,actId,NULL, buttonPos, buttonDim);

	buttonPos[X] = 0 ;
	buttonPos[Y] = (-SS_COO_SYST_HEIGHT + 120);
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Back", 3, Action_SpecifyDifficulty,NULL, buttonPos, buttonDim);


	// --- Loadout menu (solo: Others -> Ship): pick ship (left) + bullet colour (right) ---
	currentMenu = &menuScreens[MENU_SELECT_SHIP];

	MENU_CreateText(currentMenu, 0, (SS_H - 140), 3.0f, TEXT_CENTERED, "CUSTOM");
	// Status line (texts[1]) showing the current selection, updated on each pick.
	MENU_CreateText(currentMenu, 0, (SS_H - 240), 2.2f, TEXT_CENTERED, "Ship 1  -  Red");

	{
		static const char* shipLabels[NUM_SHIP_CHOICES]   = { "Ship 1", "Ship 2" };
		// Confirmed on device: column 0 = red (default), 1 = blue, 2 = invisible (kept as a
		// stealth option), 3 = yellow.
		static const char* colorLabels[NUM_BULLET_COLORS] = { "Red", "Blue", "Invisible", "Yellow" };
		int k;

		// Ships (left column)
		for (k = 0; k < NUM_SHIP_CHOICES; k++)
		{
			int* t = calloc(1, sizeof(int));
			*t = k;
			buttonPos[X] = -160 ;
			buttonPos[Y] = (SS_H - 320) - k*150;
			buttonDim[WIDTH] = (159 * 2);
			buttonDim[HEIGHT] = 64 * 2;
			MENU_CreateButtonWithTag(currentMenu, shipLabels[k], 3, Action_SelectShip, t, NULL, buttonPos, buttonDim);
		}

		// Bullet colours (right column)
		for (k = 0; k < NUM_BULLET_COLORS; k++)
		{
			int* t = calloc(1, sizeof(int));
			*t = k;
			buttonPos[X] = 160 ;
			buttonPos[Y] = (SS_H - 320) - k*150;
			buttonDim[WIDTH] = (159 * 2);
			buttonDim[HEIGHT] = 64 * 2;
			MENU_CreateButtonWithTag(currentMenu, colorLabels[k], 3, Action_SelectBulletColor, t, NULL, buttonPos, buttonDim);
		}
	}

	// Back sits in the empty bottom-LEFT cell, on the same row as the 4th colour
	// (Yellow, right), so it no longer overlaps it.
	buttonPos[X] = -160 ;
	buttonPos[Y] = (SS_H - 320) - 3*150;
	buttonDim[WIDTH] = (159 * 2);
	buttonDim[HEIGHT] = 64 * 2;
	MENU_CreateButton(currentMenu, "Back", 3, Action_ShowOthersMenu, NULL, buttonPos, buttonDim);


	menuCreated = 1;
	currentMenuId = -1;

	Log_Printf("[Menu System] Initialized.\n");
}




xf_colorless_sprite_t menuVertices[256];
ushort menuIndices[256*6];

xf_colorless_sprite_t* vertice;
ushort* indices;
ushort numIndices;
ushort numVertices;

void MENU_Render(void)
{

	menu_image_t* image;
	menu_button_t* button;
	menu_text_t* text;
	int i;
	menu_screen_t* currentMenu;
	
	currentMenu = &menuScreens[currentMenuId];
	
	vertice = menuVertices;
	indices = menuIndices;
	numIndices = 0;
	numVertices = 0;
	
	renderer.StartCleanFrame();
	
	renderer.Set2D();
	
	//First draw a fading over back scree
	renderer.FadeScreen(0.40);
	
	renderer.SetMaterialTextureBlending(1);
	renderer.SetTransparency(currentMenu->alpha);
	
	if (currentMenu->alpha < 1)
		currentMenu->alpha += FADING_IN_TIME_PER_MS * timediff;
	
	
	//First draw all images in the menu
	//Log_Printf("Menu has %d images.\n",currentMenu->numImages);
	for (i=0; i < currentMenu->numImages; i++) 
	{
		image = &currentMenu->images[i];
		memcpy(vertice,image->vertices,4 * sizeof(xf_colorless_sprite_t));
		
		indices[0] = numVertices+0;
		indices[1] = numVertices+1;
		indices[2] = numVertices+2;
		indices[3] = numVertices+1;
		indices[4] = numVertices+2;
		indices[5] = numVertices+3;
		
		vertice += 4;
		indices += 6;
		numIndices+= 6;
		numVertices += 4;
	}
	
	
	
	//Then draw all buttons images in the menu
	//Log_Printf("Menu has %d buttons.\n",currentMenu->numButtons);
	for (i=0; i < currentMenu->numButtons; i++) 
	{
		button = &currentMenu->buttons[i];
		if (button->touch->down)
		{
			//Log_Printf("Down.\n");
			memcpy(vertice,button->downVertices,4 * sizeof(xf_colorless_sprite_t));
		}
		else
		{
			//Log_Printf("Down.\n");
			memcpy(vertice,button->upVertices,4 * sizeof(xf_colorless_sprite_t));
		}
		indices[0] = numVertices+0;
		indices[1] = numVertices+1;
		indices[2] = numVertices+2;
		indices[3] = numVertices+1;
		indices[4] = numVertices+2;
		indices[5] = numVertices+3;
		
		vertice += 4;
		indices += 6;
		numIndices+= 6;
		numVertices += 4;
	}
	
	
	//Ready draw everything.
	renderer.SetTexture(textureAtlas.textureId);
	renderer.RenderColorlessSprites(menuVertices,numIndices,menuIndices);
	
	vertice = menuVertices;
	indices = menuIndices;
	numIndices = 0;
	numVertices = 0;
	
	SCR_StartConvertText();
	
	//Now draw all texts (button + real text).
	for (i=0; i < currentMenu->numTexts; i++) 
	{
		text = &currentMenu->texts[i] ;
		SCR_ConvertTextToVertices(text->text,text->font_size,text->textPos[X],text->textPos[Y],text->centerStyle);
	}
	
	for (i=0; i < currentMenu->numButtons; i++) 
	{
		button = &currentMenu->buttons[i];
		SCR_ConvertTextToVertices(button->text,button->font_size,button->textPos[X],button->textPos[Y],TEXT_CENTERED);
	}

	SCR_RenderText();
	
}

void MENU_HandleTouches(void)
{
	int i;
	menu_button_t* button;
	menu_screen_t* currentMenu;
	
	currentMenu = &menuScreens[currentMenuId];
	
	for (i=0; i < currentMenu->numButtons; i++) 
	{
		button = &currentMenu->buttons[i];
		if (button->touch->down && !button->actionTriggered)
		{
			
			button->actionTriggered = 1;
			if (button->update)
				button->update();
			
			button->action(button->tag);
		}
	}
	
}

void MENU_ClearButtonStates(void)
{
	// Reset the current menu's button press/highlight state. Used after a modal
	// (e.g. the Game Center sheet) returns WITHOUT switching menus, so the button
	// that opened it doesn't stay stuck highlighted -- and can be tapped again
	// (clearing actionTriggered too).
	int i;
	menu_screen_t* currentMenu = &menuScreens[currentMenuId];
	for (i=0; i < currentMenu->numButtons; i++)
	{
		currentMenu->buttons[i].actionTriggered = 0;
		currentMenu->buttons[i].touch->down = 0;
	}
}

touch_t* MENU_GetCurrentButtonTouches(void)
{
	return menuScreens[currentMenuId].touches;
}

int MENU_GetNumButtonsTouches(void)
{
	return menuScreens[currentMenuId].numButtons;
}
