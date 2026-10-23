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

// ---- The menus --------------------------------------------------------------
// A tap selects the button whose drawn rectangle holds the finger (the closest
// one if two do); off every rectangle, the closest button within 64 units, as
// in 2009 -- a wide button used to answer only around its centre.
static int IO_MenuButtonAt(float tx, float ty, const touch_t* set, int numButton)
{
	int		i, best = -1, inRect = 0;
	int		bestDist = 64 * 64;
	int		dist;
	short	x0, y0, x1, y1;
	float	sx = 2 * tx - SS_W;		// touch space -> screen space
	float	sy = SS_H - 2 * ty;

	for (i = 0; i < numButton; i++)
	{
		dist = SQUARE(set[i].iphone_coo_SysPos[X] - tx) + SQUARE(set[i].iphone_coo_SysPos[Y] - ty);
		if (MENU_GetButtonRect(i, &x0, &y0, &x1, &y1) && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1)
		{
			if (!inRect || dist < bestDist)
			{
				inRect = 1;
				bestDist = dist;
				best = i;
			}
		}
		else if (!inRect && dist < bestDist)
		{
			bestDist = dist;
			best = i;
		}
	}
	return best;
}

// A menu button acts when a finger pressed it AND lifted on it, both while
// that menu was up. A release alone used to be enough: a finger already down
// when the pause menu opened (steering, the other hand on "II") could lift
// on Quit and end the game.
static int			gMenuArmed = -1;
static signed char	gMenuArmedOn = -1;

// ---- The virtual pad --------------------------------------------------------
// A pad button is held from the moment a finger lands on it until that finger
// lifts (or the system cancels it), wherever it slides in between: the stick
// keeps steering, fire keeps firing. Events carry no finger identity, so a
// finger is known by where its last event left it -- a MOVED event's previous
// position, an ENDED event's position. The special shot is one press, one shot.
static char  gPadHeld[NUM_BUTTONS];
static float gPadLastPos[NUM_BUTTONS][2];

void IO_ReleasePad(void)
{
	memset(gPadHeld, 0, sizeof(gPadHeld));
}

static float IO_Clamp1(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }

static void IO_PadTouch(const io_event_s* e)
{
	int		i, button = -1;
	float	d, bestD = 64 * 64;
	const float* at = (e->type == IO_EVENT_MOVED) ? e->previousPosition : e->position;

	if (e->type == IO_EVENT_BEGAN)
	{
		for (i = 0; i < NUM_BUTTONS; i++)
		{
			d = SQUARE(touches[i].iphone_coo_SysPos[X] - e->position[X]) + SQUARE(touches[i].iphone_coo_SysPos[Y] - e->position[Y]);
			if (d < bestD)
			{
				bestD = d;
				button = i;
			}
		}
		if (button < 0)
			return;
		gPadHeld[button] = 1;
	}
	else
	{
		for (i = 0; i < NUM_BUTTONS; i++)
		{
			if (!gPadHeld[i])
				continue;
			d = SQUARE(gPadLastPos[i][X] - at[X]) + SQUARE(gPadLastPos[i][Y] - at[Y]);
			if (button < 0 || d < bestD)
			{
				bestD = d;
				button = i;
			}
		}
		if (button < 0)
			return;
		if (e->type == IO_EVENT_ENDED)
		{
			gPadHeld[button] = 0;
			if (button != BUTTON_GHOST)		// the special shot's press has already fired
			{
				touches[button].down = 0;
				touches[button].dist[X] = 0;
				touches[button].dist[Y] = 0;
			}
			return;
		}
		if (button == BUTTON_GHOST)
			return;		// sliding on the special button fires nothing more
	}

	gPadLastPos[button][X] = e->position[X];
	gPadLastPos[button][Y] = e->position[Y];
	touches[button].down = 1;
	touches[button].dist[X] = IO_Clamp1((e->position[X] - touches[button].iphone_coo_SysPos[X]) / touches[button].iphone_size);
	touches[button].dist[Y] = IO_Clamp1((touches[button].iphone_coo_SysPos[Y] - e->position[Y]) / touches[button].iphone_size);
}

void IO_PushEvent(io_event_s* event_in){
	
	int		button;

	//Perform a defensive copy
	io_event_s event = *event_in;

	if (!engine.menuVisible && IO_HudTouch(&event))
		return;

	//Transforming from whatever screen resolution we have to the original iPHone 320*480
	event.position[X] = ( event.position[X] - renderer.viewPortDimensions[VP_X] ) * commScale[X] ;
	event.position[Y] = ( event.position[Y] - renderer.viewPortDimensions[VP_Y] ) * commScale[Y] ;
	if (event.type == IO_EVENT_MOVED)
	{
		event.previousPosition[X] = ( event.previousPosition[X] - renderer.viewPortDimensions[VP_X] ) * commScale[X] ;
		event.previousPosition[Y] = ( event.previousPosition[Y] - renderer.viewPortDimensions[VP_Y] ) * commScale[Y] ;
	}

	if (engine.menuVisible)
	{
		// Round 41: on a scrolling menu a vertical drag scrolls it, and its release is not a tap.
		if (MENU_ScrollTouch(event.type, event.position[Y]))
			return;

		button = IO_MenuButtonAt(event.position[X], event.position[Y], MENU_GetCurrentButtonTouches(), MENU_GetNumButtonsTouches());
		if (event.type == IO_EVENT_BEGAN)
		{
			gMenuArmed = button;
			gMenuArmedOn = MENU_Get();
		}
		else if (event.type == IO_EVENT_ENDED)
		{
			if (button >= 0 && button == gMenuArmed && MENU_Get() == gMenuArmedOn)
				MENU_GetCurrentButtonTouches()[button].down = 1;
			gMenuArmed = -1;
		}
	}
	else if (engine.controlMode == CONTROL_MODE_VIRT_PAD)
	{
		IO_PadTouch(&event);
	}
	else
	{
			if (event.type == IO_EVENT_ENDED) 
			{
				//if (touchCount == 1) //Last finger ended
					touches[BUTTON_FIRE].down = 0;
			}
			else 
			{
			 
				
			
				if (event.type == IO_EVENT_MOVED) 
				{
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