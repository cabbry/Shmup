#include "io_interface.h"
#include "menu.h"
#include "dEngine.h"
#include "timer.h"
#include "player.h"

int lastTouchBegan=0;

#define SQUARE(X) ((X)*(X))


float commScale[2]; 

void IO_Init(void){
	commScale[X] = SS_W/ (float)renderer.viewPortDimensions[VP_WIDTH];
	commScale[Y] = SS_H/ (float)renderer.viewPortDimensions[VP_HEIGHT];
}

// ---- The HUD's buttons: pause during an act, [ BACK ] in the demo and the ---
// tutorials. Drawn by player.c, hit-tested HERE, on every platform, from the
// very position the HUD draws them at (P_PauseButtonPos / P_BackButtonPos).
// A tap is a press and a release on the same button; everything in between is
// swallowed, so the press neither fires nor steers the ship.
#define HUD_NONE  0
#define HUD_PAUSE 1
#define HUD_BACK  2
static int gHudPressed = HUD_NONE;

static float IO_Abs(float v) { return v < 0 ? -v : v; }

// tx, ty: touch space (0..SS_W, 0..SS_H, top-down), as the menus use.
// SS -> touch space is MENU_CreateButtonWithTag's conversion.
static int IO_HudButtonAt(float tx, float ty)
{
	short x, y;
	if (P_PauseButtonShown())
	{
		P_PauseButtonPos(&x, &y);
		if (IO_Abs(tx - (x + SS_W) / 2.0f) < 24 && IO_Abs(ty - (SS_H - (y + SS_H) / 2.0f)) < 24)
			return HUD_PAUSE;
	}
	if (P_BackButtonShown())
	{
		P_BackButtonPos(&x, &y);
		if (IO_Abs(tx - (x + SS_W) / 2.0f) < 56 && IO_Abs(ty - (SS_H - (y + SS_H) / 2.0f)) < 26)
			return HUD_BACK;
	}
	return HUD_NONE;
}

// 1 when the event belongs to a HUD button (and is consumed).
static int IO_HudTouch(const io_event_s* e)
{
	float tx = (e->position[X] - renderer.viewPortDimensions[VP_X]) * commScale[X];
	float ty = (e->position[Y] - renderer.viewPortDimensions[VP_Y]) * commScale[Y];
	int pressed;

	if (e->type == IO_EVENT_BEGAN)
	{
		gHudPressed = IO_HudButtonAt(tx, ty);
		return gHudPressed != HUD_NONE;
	}
	if (gHudPressed == HUD_NONE)
		return 0;
	if (e->type == IO_EVENT_ENDED)
	{
		pressed = gHudPressed;
		gHudPressed = HUD_NONE;
		if (IO_HudButtonAt(tx, ty) == pressed)
		{
			if (pressed == HUD_PAUSE)
				dEngine_PauseByPlayer();
			else
			{
				MENU_Set(MENU_HOME);
				dEngine_RequireSceneId(0);
			}
		}
	}
	return 1;
}

void IO_PushEvent(io_event_s* event_in){
	


	
	
	touch_t *touch, *t2;
	int		minDist = 64 * 64  ;	// allow up to 64 unit moves to be drags
	int		minIndex = -1;

	//Button analysis variables
	int     i;
	int     dist;

	//Buttons in shmup
	int numButton;
	touch_t* currentTouchSet;

	//Perform a defensive copy
	io_event_s event = *event_in;

	if (!engine.menuVisible && IO_HudTouch(&event))
		return;



	if (engine.menuVisible)
	{
		numButton = MENU_GetNumButtonsTouches();
		currentTouchSet = MENU_GetCurrentButtonTouches();
	}
	else 
	{
		numButton = NUM_BUTTONS;
		currentTouchSet = touches;
	}
	
	if (engine.menuVisible || engine.controlMode == CONTROL_MODE_VIRT_PAD)
	{
	
       // CGPoint touchLocation = [myTouch locationInView:nil];
		
		//Transforming from whatever screen resolution we have to the original iPHone 320*480
		event.position[X] = ( event.position[X] - renderer.viewPortDimensions[VP_X] ) * commScale[X] ;//* renderer.resolution ;
		event.position[Y] = ( event.position[Y] - renderer.viewPortDimensions[VP_Y] ) * commScale[Y] ;//* renderer.resolution;


		// Round 41: on a scrolling menu a vertical drag scrolls it, and its release is not a tap.
		if (engine.menuVisible && MENU_ScrollTouch(event.type, event.position[Y]))
			return;

		// find which one it is closest to
		t2 = currentTouchSet;
		for (i  = 0 ; i < numButton ; i++ ) 
		{
			
			dist = SQUARE( t2->iphone_coo_SysPos[X] - event.position[X] )  + SQUARE( t2->iphone_coo_SysPos[Y] - event.position[Y] ) ;
			
			
			if ( dist < minDist ) {
				minDist = dist;
				minIndex = i;
				touch = t2;
			}
		//	printf("button:%d x=%hd, y =%.hd. dist=%.2f, minDist=%.2f\n",i,t2->iphone_coo_SysPos[X],t2->iphone_coo_SysPos[Y],(float)dist,(float)minDist);
			t2++;
		}
		
		if ( minIndex != -1 ) 
		{
			//printf("HIT ! %d.\n",minIndex);
			if (event.type == IO_EVENT_ENDED) 
			{
				//touch->down = 0;
				//printf("%d UP\n",minIndex);
                touch->down = 1;
				touch->dist[X] = MIN(1,(event.position[X] - touches[minIndex].iphone_coo_SysPos[X])/touches[minIndex].iphone_size);
				touch->dist[Y] = MIN(1,(touches[minIndex].iphone_coo_SysPos[Y] - event.position[Y])/touches[minIndex].iphone_size);
			}
			else 
			{
				if (event.type == IO_EVENT_BEGAN) 
				{
				
                }
			}
		}
	
	}
	else
	{
		
		
		
			//CGPoint touchLocation = [myTouch locationInView:nil];
			//CGPoint prevTouchLocation = [myTouch previousLocationInView:nil];
			
			//Transforming from whatever screen resolution we have to the original iPHone 320*480
			event.position[X] = ( event.position[X]- renderer.viewPortDimensions[VP_X] ) * commScale[X] ;//* renderer.resolution ;
			event.position[Y] = ( event.position[Y]- renderer.viewPortDimensions[VP_Y] ) * commScale[Y] ;//* renderer.resolution;
			
			
			
			
			if (event.type == IO_EVENT_ENDED) 
			{
				//if (touchCount == 1) //Last finger ended
					touches[BUTTON_FIRE].down = 0;
			}
			else 
			{
			 
				
			
				if (event.type == IO_EVENT_MOVED) 
				{
                    
                    event.previousPosition[X] = ( event.previousPosition[X]- renderer.viewPortDimensions[VP_X] ) * commScale[X] ;//* renderer.resolution ;
                    event.previousPosition[Y] = ( event.previousPosition[Y]- renderer.viewPortDimensions[VP_Y] ) * commScale[Y] ;//* renderer.resolution;
                    
                    
					//printf("m\n");
					touches[BUTTON_MOVE].down = 1;
                    //Used to be +80 for X and -80 for Y
					touches[BUTTON_MOVE].dist[X] = (event.position[X] - event.previousPosition[X])*80/(float)320;
					touches[BUTTON_MOVE].dist[Y] = (event.position[Y] - event.previousPosition[Y])*-80/(float)480;
					
				}
				if (event.type == IO_EVENT_BEGAN)
				{
					int currTime = E_Sys_Milliseconds();
					if (currTime-lastTouchBegan < 200)
						touches[BUTTON_GHOST].down = 1;
					
					lastTouchBegan = currTime ;
					
					touches[BUTTON_FIRE].down = 1;
				}
				
				
			}
			
		
	}
	
	
	
	

}