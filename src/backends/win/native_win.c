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
 *  native_win.c -- the native services on Windows (round 87).
 *
 *  What dEngineAppDelegate.m and EAGLView.m give the engine on iOS, minus
 *  what has no Windows counterpart (Game Center, GameKit matches): the PNG
 *  decoder (Windows Imaging Component, premultiplied RGBA like CoreGraphics
 *  produced in 2009), the persisted settings and loadout (a key=value file
 *  in the writable folder instead of NSUserDefaults), the language, the
 *  version string, the replay listing. No library beyond the OS.
 */
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native_services.h"
#include "ItextureLoader.h"
#include "texture.h"
#include "filesystem.h"
#include "dEngine.h"
#include "player.h"
#include "log.h"

#ifndef SHMUP_VERSION
#define SHMUP_VERSION "dev"
#endif

extern char* FS_GameWritableDir(void);	// filesystem.c, declared where EAGLView.m declared it

// ---------------------------------------------------------------------------
//  PNG through WIC. The GUIDs are spelled out so no uuid import library is needed.
// ---------------------------------------------------------------------------
static const GUID kCLSID_WICImagingFactory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID kIID_IWICImagingFactory  = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
static const GUID kIID_IWICPixelFormatInfo2 = { 0xa9db33a2, 0xaf5f, 0x43c7, { 0xb6, 0x79, 0x74, 0xf5, 0x98, 0x4b, 0x5a, 0xa4 } };
static const GUID kGUID_WICPixelFormat32bppPRGBA = { 0x3cc4a650, 0xa527, 0x4d37, { 0xa9, 0x16, 0x31, 0x42, 0xc7, 0xeb, 0xed, 0xba } };

static IWICImagingFactory* gWIC;

static IWICImagingFactory* WIC(void)
{
	if (!gWIC)
	{
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if (FAILED(CoCreateInstance(&kCLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &kIID_IWICImagingFactory, (void**)&gWIC)))
			gWIC = NULL;
	}
	return gWIC;
}

void loadNativePNG(texture_t* text)
{
	char path[MAX_OSPATH * 2];
	wchar_t wpath[MAX_OSPATH * 2];
	IWICImagingFactory* f = WIC();
	IWICBitmapDecoder* dec = NULL;
	IWICBitmapFrameDecode* frame = NULL;
	IWICFormatConverter* conv = NULL;
	IWICComponentInfo* cinfo = NULL;
	IWICPixelFormatInfo2* pinfo = NULL;
	WICPixelFormatGUID pf;
	BOOL hasAlpha = TRUE;
	UINT w = 0, h = 0;

	text->file = NULL;
	snprintf(path, sizeof(path), "%s/%s", FS_Gamedir(), text->path);
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
	if (!f || FAILED(IWICImagingFactory_CreateDecoderFromFilename(f, wpath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
	{
		Log_Printf("[PNG Loader] could not load: %s\n", path);
		return;
	}
	if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)))
		goto done;
	IWICBitmapFrameDecode_GetSize(frame, &w, &h);
	if (SUCCEEDED(IWICBitmapFrameDecode_GetPixelFormat(frame, &pf)) &&
	    SUCCEEDED(IWICImagingFactory_CreateComponentInfo(f, &pf, &cinfo)) &&
	    SUCCEEDED(IWICComponentInfo_QueryInterface(cinfo, &kIID_IWICPixelFormatInfo2, (void**)&pinfo)))
		IWICPixelFormatInfo2_SupportsTransparency(pinfo, &hasAlpha);
	if (FAILED(IWICImagingFactory_CreateFormatConverter(f, &conv)))
		goto done;
	// Premultiplied RGBA: what kCGImageAlphaPremultipliedLast gave the GL path in 2009.
	if (FAILED(IWICFormatConverter_Initialize(conv, (IWICBitmapSource*)frame, &kGUID_WICPixelFormat32bppPRGBA,
	                                          WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)))
		goto done;

	text->width = w;
	text->height = h;
	text->bpp = hasAlpha ? 32 : 24;
	text->format = hasAlpha ? TEXTURE_GL_RGBA : TEXTURE_GL_RGB;
	text->numMipmaps = 1;
	text->data    = (ubyte**)calloc(1, sizeof(ubyte*));
	text->data[0] = (ubyte*)calloc((size_t)w * h * 4, sizeof(ubyte));
	text->dataLength = 0;
	if (FAILED(IWICBitmapSource_CopyPixels((IWICBitmapSource*)conv, NULL, w * 4, w * h * 4, text->data[0])))
	{
		Log_Printf("[PNG Loader] decode failed: %s\n", path);
		free(text->data[0]); free(text->data); text->data = NULL;
		text->format = 0;
	}
done:
	if (pinfo) IWICPixelFormatInfo2_Release(pinfo);
	if (cinfo) IWICComponentInfo_Release(cinfo);
	if (conv)  IWICFormatConverter_Release(conv);
	if (frame) IWICBitmapFrameDecode_Release(frame);
	if (dec)   IWICBitmapDecoder_Release(dec);
}

// A PNG from RGBA rows as glReadPixels hands them (bottom-up). The probe of
// the Windows port: SHMUP_SHOTS names the seconds at which main.c calls it.
static const GUID kGUID_ContainerFormatPng = { 0x1b7cfaf4, 0x713f, 0x473c, { 0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf } };
static const GUID kGUID_WICPixelFormat32bppRGBA = { 0xf5c7ad2d, 0x6a8d, 0x43dd, { 0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9 } };

int WIN_SavePNG(const char* path, int w, int h, const unsigned char* rgbaBottomUp)
{
	wchar_t wpath[MAX_OSPATH * 2];
	IWICImagingFactory* f = WIC();
	IWICStream* stream = NULL;
	IWICBitmapEncoder* enc = NULL;
	IWICBitmapFrameEncode* frame = NULL;
	IPropertyBag2* props = NULL;
	WICPixelFormatGUID pf = kGUID_WICPixelFormat32bppRGBA;
	unsigned char* rows;
	int y, ok = 0;

	if (!f || w <= 0 || h <= 0)
		return 0;
	rows = (unsigned char*)malloc((size_t)w * h * 4);
	if (!rows)
		return 0;
	for (y = 0; y < h; y++)
		memcpy(rows + (size_t)y * w * 4, rgbaBottomUp + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])));
	if (FAILED(IWICImagingFactory_CreateStream(f, &stream))) goto done;
	if (FAILED(IWICStream_InitializeFromFilename(stream, wpath, GENERIC_WRITE))) goto done;
	if (FAILED(IWICImagingFactory_CreateEncoder(f, &kGUID_ContainerFormatPng, NULL, &enc))) goto done;
	if (FAILED(IWICBitmapEncoder_Initialize(enc, (IStream*)stream, WICBitmapEncoderNoCache))) goto done;
	if (FAILED(IWICBitmapEncoder_CreateNewFrame(enc, &frame, &props))) goto done;
	if (FAILED(IWICBitmapFrameEncode_Initialize(frame, props))) goto done;
	if (FAILED(IWICBitmapFrameEncode_SetSize(frame, (UINT)w, (UINT)h))) goto done;
	if (FAILED(IWICBitmapFrameEncode_SetPixelFormat(frame, &pf))) goto done;
	if (FAILED(IWICBitmapFrameEncode_WritePixels(frame, (UINT)h, (UINT)w * 4, (UINT)w * h * 4, rows))) goto done;
	if (FAILED(IWICBitmapFrameEncode_Commit(frame))) goto done;
	if (FAILED(IWICBitmapEncoder_Commit(enc))) goto done;
	ok = 1;
done:
	if (props)  IPropertyBag2_Release(props);
	if (frame)  IWICBitmapFrameEncode_Release(frame);
	if (enc)    IWICBitmapEncoder_Release(enc);
	if (stream) IWICStream_Release(stream);
	free(rows);
	return ok;
}

// ---------------------------------------------------------------------------
//  Settings: <writable dir>/settings.cfg, key=value per line. What
//  checkEngineSettings reads from NSUserDefaults, and what Native_Save*
//  writes back.
// ---------------------------------------------------------------------------
static void SettingsPath(char* out, int len)
{
	snprintf(out, len, "%s/settings.cfg", FS_GameWritableDir());
}

static void SaveSettings(void)
{
	char path[MAX_OSPATH * 2];
	FILE* f;
	SettingsPath(path, sizeof(path));
	f = fopen(path, "w");
	if (!f)
	{
		Log_Printf("[settings] cannot write %s\n", path);
		return;
	}
	fprintf(f, "sound=%d\nmusic=%d\ncontrol=%d\nship=%d\ncolor=%d\nhighestAct=%d\nlayout=2\n",
	        engine.soundEnabled, engine.musicEnabled, engine.controlMode, gShipChoice, gBulletColor, gHighestActReached);
	fclose(f);
}

// Called by the platform before dEngine_Init (the menus read these), after
// the RD/WD environment is set.
void WIN_LoadSettings(void)
{
	char path[MAX_OSPATH * 2], line[256];
	FILE* f;
	engine.soundEnabled = 1;
	engine.musicEnabled = 1;
	engine.controlMode = CONTROL_MODE_SWIP;
	engine.gameCenterEnabled = 0;
	engine.gameCenterPossible = 0;
	engine.licenseType = 1;
	snprintf(path, sizeof(path), "%s/settings.cfg", getenv("WD") ? getenv("WD") : ".");
	f = fopen(path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f))
	{
		int v = atoi(strchr(line, '=') ? strchr(line, '=') + 1 : "0");
		if      (!strncmp(line, "sound=", 6))      engine.soundEnabled = v ? 1 : 0;
		else if (!strncmp(line, "music=", 6))      engine.musicEnabled = v ? 1 : 0;
		else if (!strncmp(line, "control=", 8))    engine.controlMode = (uchar)v;
		else if (!strncmp(line, "ship=", 5))       gShipChoice = v;
		else if (!strncmp(line, "color=", 6))      gBulletColor = v;
		else if (!strncmp(line, "highestAct=", 11)) gHighestActReached = v;
	}
	fclose(f);
	if (gShipChoice  < 0 || gShipChoice  >= NUM_SHIP_CHOICES)  gShipChoice  = 0;
	if (gBulletColor < 0 || gBulletColor >= NUM_BULLET_COLORS) gBulletColor = 0;
	if (gHighestActReached < 1) gHighestActReached = 1;
	if (gHighestActReached > 5) gHighestActReached = 5;	// 5 playable acts, as on iOS
	Log_Printf("[settings] sound=%d music=%d control=%d ship=%d color=%d highestAct=%d\n",
	           engine.soundEnabled, engine.musicEnabled, engine.controlMode, gShipChoice, gBulletColor, gHighestActReached);
}

void WIN_SaveSettings(void) { SaveSettings(); }

void Native_SaveLoadout(int ship, int color)
{
	gShipChoice = ship;
	gBulletColor = color;
	SaveSettings();
}

void Native_SaveProgress(int highestAct)
{
	gHighestActReached = highestAct;
	SaveSettings();
}

// ---------------------------------------------------------------------------
//  The rest of native_services.h
// ---------------------------------------------------------------------------
int Native_RetrieveListOf(char replayList[10][256])
{
	char pattern[MAX_OSPATH * 2];
	WIN32_FIND_DATAA fd;
	HANDLE h;
	int n = 0;
	snprintf(pattern, sizeof(pattern), "%s/*.io", FS_GameWritableDir());
	h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	do
	{
		if (n < 10)
			strncpy(replayList[n++], fd.cFileName, 255);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return n;
}

void Native_UploadFileTo(char path[256]) { (void)path; }
void Action_ShowGameCenter(void* tag) { (void)tag; }
void Native_UploadScore(uint score) { Log_Printf("[score] %u (no leaderboard on this platform)\n", score); }
void Native_LoginGameCenter(void) {}
void Native_StartOnlineMatchmaking(int partySize) { (void)partySize; }
void Native_CancelOnlineMatchmaking(void) {}
void Native_GKSendData(const void* data, int len, int reliable) { (void)data; (void)len; (void)reliable; }

int Native_IsFrenchLanguage(void)
{
	return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH ? 1 : 0;
}

const char* Native_GetVersionString(void)
{
	static char buf[32] = "";
	if (buf[0] == 0)
		snprintf(buf, sizeof(buf), "v%s", SHMUP_VERSION);
	return buf;
}
