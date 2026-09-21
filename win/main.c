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
 *  win/main.c -- SHMUP Reborn on Windows (round 87).
 *
 *  What EAGLView.m and dEngineAppDelegate.m do on iOS, on a Win32 window
 *  with a WGL context: the data root and writable folder, the settings,
 *  dEngine_Init, the GL backend, then a frame per vsync -- messages, the
 *  mouse as the finger (a drag is a swipe), dEngine_HostFrame between the
 *  backend's brackets, SwapBuffers. No SDL, no library beyond the OS.
 *
 *  Usage: ShmupReborn.exe [--size WxH] [--scene N] [--data <folder that holds data/>]
 *    The window opens portrait at the aspect of an iPhone; the data root is
 *    found by walking up from the executable to a folder holding
 *    data/data/config.cfg (the repository root), unless --data or RD says.
 */
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>

#include "dEngine.h"
#include "renderer.h"
#include "renderer_gl.h"
#include "io_interface.h"
#include "menu.h"
#include "music.h"
#include "timer.h"
#include "titles.h"
#include "log.h"
#include "native_services.h"

void WIN_LoadSettings(void);	// native_win.c
int  WIN_SavePNG(const char* path, int w, int h, const unsigned char* rgbaBottomUp);	// native_win.c
void SND_MusicProbeTick(void);	// music_mci.c

// SHMUP_SHOTS="4,10,20" (seconds after launch) or --shots: the frame is read
// back and written to <writable dir>/shot_<s>.png. The camera of the port,
// what simctl io screenshot was to the Simulator smokes.
static int   gClientW, gClientH;	// defined below
static int   gShotAt[16], gShotCount = 0, gShotNext = 0;
static DWORD gLaunchTick;

static void ParseShots(const char* list)
{
	const char* p = list;
	gShotCount = 0;
	while (p && *p && gShotCount < 16)
	{
		gShotAt[gShotCount++] = atoi(p);
		p = strchr(p, ',');
		if (p) p++;
	}
}

static void TakeShotIfDue(void)
{
	char path[MAX_PATH * 2];
	unsigned char* px;
	int elapsed;
	if (gShotNext >= gShotCount || gClientW <= 0 || gClientH <= 0)
		return;
	elapsed = (int)((GetTickCount() - gLaunchTick) / 1000);
	if (elapsed < gShotAt[gShotNext])
		return;
	px = (unsigned char*)malloc((size_t)gClientW * gClientH * 4);
	if (!px) return;
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadBuffer(GL_BACK);
	glReadPixels(0, 0, gClientW, gClientH, GL_RGBA, GL_UNSIGNED_BYTE, px);
	snprintf(path, sizeof(path), "%s/shot_%d.png", getenv("WD") ? getenv("WD") : ".", gShotAt[gShotNext]);
	printf("[shot] t=%d -> %s (%s)\n", simulationTime, path, WIN_SavePNG(path, gClientW, gClientH, px) ? "ok" : "FAILED");
	free(px);
	gShotNext++;
}

// Timer_tick advances the simulation by a FIXED 16.67 ms per rendered frame
// (the lockstep's determinism), so the loop must run at 60 Hz whatever the
// swap interval does: a deadline per frame on the performance counter,
// Sleep to a millisecond short of it, spin the rest. CADisplayLink, by hand.
static LARGE_INTEGER gQpcFreq, gNextFrame;

static void PaceFrame(void)
{
	LARGE_INTEGER now;
	const LONGLONG period = gQpcFreq.QuadPart / 60;
	if (gNextFrame.QuadPart == 0)
	{
		QueryPerformanceCounter(&gNextFrame);
		gNextFrame.QuadPart += period;
		return;
	}
	QueryPerformanceCounter(&now);
	if (gNextFrame.QuadPart - now.QuadPart > gQpcFreq.QuadPart / 4)
		gNextFrame = now;	// the clock jumped (a pause, a drag of the window): do not catch up
	while (now.QuadPart < gNextFrame.QuadPart)
	{
		LONGLONG left = gNextFrame.QuadPart - now.QuadPart;
		if (left > gQpcFreq.QuadPart / 500)	// more than 2 ms: sleep a millisecond
			Sleep(1);
		QueryPerformanceCounter(&now);
	}
	gNextFrame.QuadPart += period;
	if (now.QuadPart - gNextFrame.QuadPart > period * 4)
		gNextFrame = now;	// hopelessly late (a stall): restart the cadence from here
}

// [wall]: the scene changes dated in wall milliseconds since launch, and the
// rendered frame rate every five seconds -- the port's own honesty check
// against the fixed simulation step (60 frames must be 1000 ms of sim).
static void WallProbe(void)
{
	static int lastScene = -2, frames = 0;
	static DWORD lastReport = 0;
	DWORD now = GetTickCount();
	frames++;
	if (engine.sceneId != lastScene)
	{
		printf("[wall] +%lu ms: scene %d (sim t=%d)\n", (unsigned long)(now - gLaunchTick), engine.sceneId, simulationTime);
		lastScene = engine.sceneId;
	}
	if (lastReport == 0) lastReport = now;
	if (now - lastReport >= 5000)
	{
		printf("[wall] +%lu ms: %.1f fps, sim t=%d\n", (unsigned long)(now - gLaunchTick), frames * 1000.0f / (now - lastReport), simulationTime);
		frames = 0;
		lastReport = now;
	}
}

static HWND  gWnd;
static HDC   gDC;
static HGLRC gRC;
static int   gClientW = 0, gClientH = 0;
static int   gDragging = 0;
static POINT gLastMouse;
static int   gPaused = 0;
static int   gQuit = 0;
static int   gEngineUp = 0;

typedef BOOL (WINAPI *PFN_wglSwapIntervalEXT)(int);

static void* GetGLProc(const char* name)
{
	void* p = (void*)wglGetProcAddress(name);
	if (!p)
	{
		static HMODULE gl = NULL;
		if (!gl) gl = LoadLibraryA("opengl32.dll");
		p = gl ? (void*)GetProcAddress(gl, name) : NULL;
	}
	return p;
}

// ---------------------------------------------------------------------------
//  Folders: RD (the folder holding data/), WD (the writable one)
// ---------------------------------------------------------------------------
static int HasData(const char* dir)
{
	char probe[MAX_PATH * 2];
	snprintf(probe, sizeof(probe), "%s/data/data/config.cfg", dir);
	return GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES;
}

static void FindDataRoot(char* rd, int len, const char* override)
{
	char exe[MAX_PATH], dir[MAX_PATH];
	int i;
	if (override && override[0])
	{
		snprintf(rd, len, "%s/data", override);
		return;
	}
	if (getenv("RD") && getenv("RD")[0])
	{
		snprintf(rd, len, "%s", getenv("RD"));
		return;
	}
	GetModuleFileNameA(NULL, exe, sizeof(exe));
	for (i = 0; exe[i]; i++) if (exe[i] == '\\') exe[i] = '/';
	strncpy(dir, exe, sizeof(dir) - 1);
	for (i = 0; i < 6; i++)
	{
		char* slash = strrchr(dir, '/');
		if (!slash) break;
		*slash = 0;
		if (HasData(dir))
		{
			snprintf(rd, len, "%s/data", dir);
			return;
		}
	}
	// last resort: the current directory
	snprintf(rd, len, "%s", HasData(".") ? "./data" : "data");
}

static void FindWritableDir(char* wd, int len)
{
	char base[MAX_PATH];
	if (getenv("WD") && getenv("WD")[0])
	{
		snprintf(wd, len, "%s", getenv("WD"));
		return;
	}
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base)))
	{
		int i;
		for (i = 0; base[i]; i++) if (base[i] == '\\') base[i] = '/';
		snprintf(wd, len, "%s/ShmupReborn", base);
	}
	else
		snprintf(wd, len, ".");
	CreateDirectoryA(wd, NULL);
}

// ---------------------------------------------------------------------------
//  The finger
// ---------------------------------------------------------------------------
static void PushTouch(int type, int x, int y, int px, int py)
{
	io_event_s ev;
	ev.type = type;
	ev.position[X] = (float)x;
	ev.position[Y] = (float)y;
	ev.previousPosition[X] = (float)px;
	ev.previousPosition[Y] = (float)py;
	IO_PushEvent(&ev);
}

// The keyboard as a finger: while an arrow (or WASD / ZQSD) is held, a
// synthetic drag moves at a steady pace from the screen's centre, which is
// exactly what the swipe control reads. Released, the finger lifts. The
// virtual pad (Others > Custom) stays a mouse affair.
static int   gKeyDir[4];			// left, right, up, down
static int   gKeyTouch = 0;
static POINT gKeyPos;

static int KeyIndex(WPARAM vk)
{
	switch (vk)
	{
		case VK_LEFT:  case 'A': case 'Q': return 0;
		case VK_RIGHT: case 'D':           return 1;
		case VK_UP:    case 'W': case 'Z': return 2;
		case VK_DOWN:  case 'S':           return 3;
	}
	return -1;
}

static void KeyboardFinger(void)
{
	int dx = (gKeyDir[1] - gKeyDir[0]), dy = (gKeyDir[3] - gKeyDir[2]);
	int step = gClientH / 120;		// about 10 px per frame on a 1183 px tall window: 600 px/s
	if (step < 4) step = 4;
	if (!dx && !dy)
	{
		if (gKeyTouch)
		{
			PushTouch(IO_EVENT_ENDED, gKeyPos.x, gKeyPos.y, gKeyPos.x, gKeyPos.y);
			gKeyTouch = 0;
		}
		return;
	}
	if (gDragging)
		return;						// the mouse has the finger
	if (!gKeyTouch || gKeyPos.x + dx * step < 8 || gKeyPos.x + dx * step > gClientW - 8 ||
	    gKeyPos.y + dy * step < 8 || gKeyPos.y + dy * step > gClientH - 8)
	{
		// (re)plant the finger at the centre: the swipe control is relative,
		// so the ship does not move on a plant, only on the drag that follows
		if (gKeyTouch)
			PushTouch(IO_EVENT_ENDED, gKeyPos.x, gKeyPos.y, gKeyPos.x, gKeyPos.y);
		gKeyPos.x = gClientW / 2; gKeyPos.y = gClientH / 2;
		PushTouch(IO_EVENT_BEGAN, gKeyPos.x, gKeyPos.y, gKeyPos.x, gKeyPos.y);
		gKeyTouch = 1;
	}
	PushTouch(IO_EVENT_MOVED, gKeyPos.x + dx * step, gKeyPos.y + dy * step, gKeyPos.x, gKeyPos.y);
	gKeyPos.x += dx * step; gKeyPos.y += dy * step;
}

// The tutorial / demo BACK button, as EAGLView hit-tests it.
static int BackButtonHit(int x, int y)
{
	float fx, fy;
	if (!(SCENE_IS(SCENE_KIND_DEMO) || SCENE_IS(SCENE_KIND_TUTORIAL)) || TITLE_IsShowing())
		return 0;
	if (gClientW <= 0 || gClientH <= 0) return 0;
	fx = x / (float)gClientW;
	fy = y / (float)gClientH;
	return (fx > 0.32f && fx < 0.68f && fy > 0.17f && fy < 0.29f);
}

static void OnResize(int w, int h)
{
	if (w <= 0 || h <= 0) return;
	gClientW = w; gClientH = h;
	renderer.glBuffersDimensions[WIDTH]  = w;
	renderer.glBuffersDimensions[HEIGHT] = h;
	if (gEngineUp)
	{
		GLR_Resize(w, h);
		SRC_OnResizeScreen(w, h);
		IO_Init();
	}
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_CLOSE:
			gQuit = 1;
			return 0;
		case WM_SIZE:
			OnResize(LOWORD(lParam), HIWORD(lParam));
			return 0;
		case WM_ACTIVATE:
			if (gEngineUp)
			{
				if (LOWORD(wParam) == WA_INACTIVE) { if (!gPaused) { dEngine_Pause(); SND_PauseSoundTrack(); gPaused = 1; } }
				else if (gPaused) { SND_ResumeSoundTrack(); dEngine_Resume(); gPaused = 0; }
			}
			return 0;
		case WM_LBUTTONDOWN:
		{
			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
			if (!gEngineUp) return 0;
			if (BackButtonHit(x, y))
			{
				MENU_Set(MENU_HOME);
				dEngine_RequireSceneId(0);
				return 0;
			}
			SetCapture(hWnd);
			gDragging = 1;
			gLastMouse.x = x; gLastMouse.y = y;
			PushTouch(IO_EVENT_BEGAN, x, y, x, y);
			return 0;
		}
		case WM_MOUSEMOVE:
			if (gDragging && gEngineUp)
			{
				int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
				if (x != gLastMouse.x || y != gLastMouse.y)
				{
					PushTouch(IO_EVENT_MOVED, x, y, gLastMouse.x, gLastMouse.y);
					gLastMouse.x = x; gLastMouse.y = y;
				}
			}
			return 0;
		case WM_LBUTTONUP:
			if (gDragging && gEngineUp)
			{
				int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
				gDragging = 0;
				ReleaseCapture();
				PushTouch(IO_EVENT_ENDED, x, y, gLastMouse.x, gLastMouse.y);
			}
			return 0;
		case WM_KEYUP:
		{
			int k = KeyIndex(wParam);
			if (k >= 0) gKeyDir[k] = 0;
			return 0;
		}
		case WM_KEYDOWN:
		{
			int k = KeyIndex(wParam);
			if (k >= 0) { gKeyDir[k] = 1; return 0; }
		}
			if (wParam == VK_ESCAPE && gEngineUp)
			{
				// Back to the home menu, as the five-finger touch does on iOS.
				if (engine.sceneId != 0 || engine.requiredSceneId != 0)
				{
					MENU_Set(MENU_HOME);
					dEngine_RequireSceneId(0);
				}
				else
					gQuit = 1;
			}
			return 0;
	}
	return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static int CreateGLWindow(int w, int h)
{
	WNDCLASSA wc;
	RECT r;
	PIXELFORMATDESCRIPTOR pfd;
	int pf, sx, sy;
	DWORD style = WS_OVERLAPPEDWINDOW;

	memset(&wc, 0, sizeof(wc));
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = "ShmupReborn";
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	if (!RegisterClassA(&wc))
		return 0;

	r.left = 0; r.top = 0; r.right = w; r.bottom = h;
	AdjustWindowRect(&r, style, FALSE);
	sx = (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2;
	sy = (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2;
	if (sy < 0) sy = 0;
	gWnd = CreateWindowA("ShmupReborn", "SHMUP Reborn", style, sx, sy, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
	if (!gWnd)
		return 0;
	gDC = GetDC(gWnd);

	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 0;
	pf = ChoosePixelFormat(gDC, &pfd);
	if (!pf || !SetPixelFormat(gDC, pf, &pfd))
		return 0;
	gRC = wglCreateContext(gDC);
	if (!gRC || !wglMakeCurrent(gDC, gRC))
		return 0;
	{
		PFN_wglSwapIntervalEXT swapInterval = (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
		// No vsync: PaceFrame is the 60 Hz clock (a 120 Hz monitor must not
		// double the simulation, and vsync plus a software deadline would
		// halve it); the compositor keeps windowed frames whole anyway.
		if (swapInterval) swapInterval(0);
	}
	GetClientRect(gWnd, &r);
	gClientW = r.right - r.left; gClientH = r.bottom - r.top;
	return 1;
}

int main(int argc, char** argv)
{
	char rd[MAX_PATH * 2], wd[MAX_PATH * 2], env[MAX_PATH * 2 + 8];
	const char* dataOverride = NULL;
	int winW = 0, winH = 0, scene = -1, i;
	setvbuf(stdout, NULL, _IONBF, 0);	// the log is read live, or from a redirected file

	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--size") && i + 1 < argc)        { sscanf(argv[++i], "%dx%d", &winW, &winH); }
		else if (!strcmp(argv[i], "--scene") && i + 1 < argc)  { scene = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--data") && i + 1 < argc)   { dataOverride = argv[++i]; }
		else if (!strcmp(argv[i], "--shots") && i + 1 < argc)  { ParseShots(argv[++i]); }
	}
	if (gShotCount == 0 && getenv("SHMUP_SHOTS"))
		ParseShots(getenv("SHMUP_SHOTS"));
	gLaunchTick = GetTickCount();
	QueryPerformanceFrequency(&gQpcFreq);
	SetProcessDPIAware();	// the surface in real pixels, not a stretched bitmap on a scaled desktop
	if (winW <= 0 || winH <= 0)
	{
		// portrait, an iPhone 15 Pro's aspect (1179x2556), 85 % of the work area
		RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
		winH = (int)((wa.bottom - wa.top) * 0.85f);
		winW = (int)(winH * 1179.0f / 2556.0f);
	}

	FindDataRoot(rd, sizeof(rd), dataOverride);
	FindWritableDir(wd, sizeof(wd));
	snprintf(env, sizeof(env), "RD=%s", rd); _putenv(env);
	snprintf(env, sizeof(env), "WD=%s", wd); _putenv(env);
	printf("SHMUP Reborn %s -- data root %s, writable %s\n", Native_GetVersionString(), rd, wd);
	if (!HasData(rd[0] ? rd : ".") && GetFileAttributesA(rd) == INVALID_FILE_ATTRIBUTES)
		printf("warning: no data folder found at %s (use --data <repo root>)\n", rd);
	if (scene >= 0)
	{
		snprintf(env, sizeof(env), "SHMUP_BAKE_SCENE=%d", scene); _putenv(env);
	}

	timeBeginPeriod(1);
	if (!CreateGLWindow(winW, winH))
	{
		MessageBoxA(NULL, "Could not create an OpenGL window.", "SHMUP Reborn", MB_ICONERROR);
		return 1;
	}

	// The engine's surface, in pixels, BEFORE dEngine_Init (the menus place
	// their titles from it).
	renderer.glBuffersDimensions[WIDTH]  = gClientW;
	renderer.glBuffersDimensions[HEIGHT] = gClientH;
	renderer.materialQuality = MATERIAL_QUALITY_HIGH;
	renderer.safeInsetTopPx = 0;
	WIN_LoadSettings();
	dEngine_Init();
	if (!GLR_Create(GetGLProc, gClientW, gClientH))
	{
		MessageBoxA(NULL, "This OpenGL driver has no shader support (OpenGL 2.0 needed).", "SHMUP Reborn", MB_ICONERROR);
		return 1;
	}
	dEngine_InitDisplaySystem(GL_RENDERER);
	renderer.props |= PROP_FOG;
	IO_Init();
	gEngineUp = 1;
	ShowWindow(gWnd, SW_SHOW);

	while (!gQuit)
	{
		MSG m;
		while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE))
		{
			if (m.message == WM_QUIT) gQuit = 1;
			TranslateMessage(&m);
			DispatchMessageA(&m);
		}
		if (gQuit) break;
		if (gPaused)
		{
			Sleep(50);
			continue;
		}
		KeyboardFinger();
		GLR_BeginFrame();
		dEngine_HostFrame();
		GLR_EndFrame();
		TakeShotIfDue();	// before the swap: the back buffer holds this frame
		SwapBuffers(gDC);
		PaceFrame();
		WallProbe();
		SND_MusicProbeTick();
	}

	SND_StopSoundTrack();
	wglMakeCurrent(NULL, NULL);
	if (gRC) wglDeleteContext(gRC);
	if (gWnd) DestroyWindow(gWnd);
	timeEndPeriod(1);
	return 0;
}
