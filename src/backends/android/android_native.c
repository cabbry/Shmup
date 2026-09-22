
// music.h placeholders
/*
void SND_InitSoundTrack(char* filename,unsigned int startAt){}
void SND_StartSoundTrack(void){}
void SND_StopSoundTrack(void){}
void SND_PauseSoundTrack(void){}
void SND_ResumeSoundTrack(void){}
*/


// native_service.h
int  Native_RetrieveListOf(char replayList[10][256]){ return 0;}
void Native_UploadFileTo(char path[256]){}
void Action_ShowGameCenter(void* tag){}
void Native_UploadScore(unsigned int score){}
void Native_LoginGameCenter(void){}

// Round 90: what the core has learnt to ask since 2012, answered on Android.
// No Game Center, no GameKit; the language from the activity's
// configuration, the version from the build, the settings (loadout and
// progress) in a key=value file under the app's internal storage.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/dEngine.h"
#include "../../core/player.h"

#ifndef SHMUP_VERSION
#define SHMUP_VERSION "dev"
#endif

int  gAndroidFrench = 0;					// set by android_main from AConfiguration
char gAndroidWritableDir[512] = "";			// the activity's internalDataPath

void Native_StartOnlineMatchmaking(int partySize) { (void)partySize; }
void Native_CancelOnlineMatchmaking(void) {}
void Native_GKSendData(const void* data, int len, int reliable) { (void)data; (void)len; (void)reliable; }
int  Native_IsFrenchLanguage(void) { return gAndroidFrench; }

const char* Native_GetVersionString(void)
{
	static char buf[32] = "";
	if (buf[0] == 0)
		snprintf(buf, sizeof(buf), "v%s", SHMUP_VERSION);
	return buf;
}

char* FS_GameWritableDir(void) { return gAndroidWritableDir; }

static void AND_SaveSettings(void)
{
	char path[600];
	FILE* f;
	if (!gAndroidWritableDir[0]) return;
	snprintf(path, sizeof(path), "%s/settings.cfg", gAndroidWritableDir);
	f = fopen(path, "w");
	if (!f) { Log_Printf("[settings] cannot write %s\n", path); return; }
	fprintf(f, "sound=%d\nmusic=%d\ncontrol=%d\nship=%d\ncolor=%d\nhighestAct=%d\n",
	        engine.soundEnabled, engine.musicEnabled, engine.controlMode, gShipChoice, gBulletColor, gHighestActReached);
	fclose(f);
}

// Called by android_main before dEngine_Init, once the writable folder is known.
void AND_LoadSettings(void)
{
	char path[600], line[256];
	FILE* f;
	engine.soundEnabled = 1;
	engine.musicEnabled = 1;
	engine.controlMode = CONTROL_MODE_SWIP;
	if (!gAndroidWritableDir[0]) return;
	snprintf(path, sizeof(path), "%s/settings.cfg", gAndroidWritableDir);
	f = fopen(path, "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f))
	{
		int v = atoi(strchr(line, '=') ? strchr(line, '=') + 1 : "0");
		if      (!strncmp(line, "sound=", 6))       engine.soundEnabled = v ? 1 : 0;
		else if (!strncmp(line, "music=", 6))       engine.musicEnabled = v ? 1 : 0;
		else if (!strncmp(line, "control=", 8))     engine.controlMode = (uchar)v;
		else if (!strncmp(line, "ship=", 5))        gShipChoice = v;
		else if (!strncmp(line, "color=", 6))       gBulletColor = v;
		else if (!strncmp(line, "highestAct=", 11)) gHighestActReached = v;
	}
	fclose(f);
	if (gShipChoice  < 0 || gShipChoice  >= NUM_SHIP_CHOICES)  gShipChoice  = 0;
	if (gBulletColor < 0 || gBulletColor >= NUM_BULLET_COLORS) gBulletColor = 0;
	if (gHighestActReached < 1) gHighestActReached = 1;
	if (gHighestActReached > 5) gHighestActReached = 5;
}

void Native_SaveLoadout(int ship, int color) { gShipChoice = ship; gBulletColor = color; AND_SaveSettings(); }
void Native_SaveProgress(int highestAct)   { gHighestActReached = highestAct; AND_SaveSettings(); }


//ITextureloader.h
#include "../../core/texture.h"
#include "../../third_party/libpng/png.h"
#include "../../core/filesystem.h"
#include "../../core/log.h"

filehandle_t* file;

void png_zip_read(png_structp png_ptr, png_bytep data, png_size_t length)
{
  FS_Read(data,1, length,file);
}

void abort_textureLoading_(const char * s, char* param)
{

  Log_Printf(s, param);
  exit(0);
}

void loadNativePNG(texture_t* tmpTex)
{
	png_structp     png_ptr;
	png_infop       info_ptr;
	unsigned int    width;
	unsigned int    height;
	int             i;

	int             bit_depth;
	int             color_type ;
	png_size_t      rowbytes;
	png_bytep       *row_pointers;
	png_byte header[8];


/*
 * char realPath[1024];
  memset(realPath, 0, 1024);
  strcat(realPath, FS_Gamedir());
  if (tmpTex->path[0] != '/')
	strcat(realPath, "/");
  strcat(realPath, tmpTex->path);
*/
  tmpTex->format = TEXTURE_TYPE_UNKNOWN ;

  file = FS_OpenFile(tmpTex->path, "rb");

  //LOGI("[Android Main] Opening %s", realPath);

	if ( !file  )
		abort_textureLoading_("[read_png_file] Could not open file '%s'\n",tmpTex->path);

	FS_Read(header,1, 8,file);

	if (png_sig_cmp(header, 0, 8) != 0 )
		abort_textureLoading_("[read_png_file] File is not recognized as a PNG file.\n", tmpTex->path);

	// initialize
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (png_ptr == NULL)
		abort_textureLoading_("[read_png_file] png_create_read_struct failed","");

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
		abort_textureLoading_("[read_png_file] png_create_info_struct failed","");

	if (setjmp(png_jmpbuf(png_ptr)))
		abort_textureLoading_("[read_png_file] Error during init_io","");

	png_set_read_fn(png_ptr, NULL, png_zip_read);
	png_set_sig_bytes(png_ptr, 8);

	png_read_info(png_ptr, info_ptr);

  //Retrieve metadata and transfer to structure bean tmpTex
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

	tmpTex->width = width;
	tmpTex->height =  height;

	// Set up some transforms.
	/*if (color_type & PNG_COLOR_MASK_ALPHA) {
		png_set_strip_alpha(png_ptr);
	}*/
	if (bit_depth > 8) {
		png_set_strip_16(png_ptr);
	}
	if (color_type == PNG_COLOR_TYPE_GRAY ||
		color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(png_ptr);
	}
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png_ptr);
	}

	// Update the png info struct.
	png_read_update_info(png_ptr, info_ptr);

	// Rowsize in bytes.
	rowbytes = png_get_rowbytes(png_ptr, info_ptr);

  	tmpTex->bpp = rowbytes / width;
	if (tmpTex->bpp == 4)
		tmpTex->format = TEXTURE_GL_RGBA;
	else
		tmpTex->format = TEXTURE_GL_RGB;

	Log_Printf("DEBUG: For %s, bpp: %i, color_type: %i, bit_depth: %i", tmpTex->path, tmpTex->bpp, color_type, bit_depth);
  //Since PNG can only store one image there is only one mipmap, allocated an array of one
  tmpTex->numMipmaps = 1;
  tmpTex->data = malloc(sizeof(uchar*));
	if ((tmpTex->data[0] = (uchar*)malloc(rowbytes * height))==NULL)
  {
	//Oops texture won't be able to hold the result :(, cleanup LIBPNG internal state and return;
	free(tmpTex->data);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	return;
	}

  //Next we need to send to libpng an array of pointer, let's point to tmpTex->data[0]
	if ((row_pointers = (png_bytepp)malloc(height*sizeof(png_bytep))) == NULL)
  {
	// Oops looks like we won't have enough RAM to allocate an array of pointer....
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		free(tmpTex->data );
		tmpTex->data  = NULL;
	return;
	}

  //FCS: Hm, it looks like we are flipping the image vertically.
  //     Since iOS did not do it, we may have to not to that. If result is
  //     messed up, just swap to:   row_pointers[             i] = ....
	for (i = 0;  i < height;  ++i)
		//row_pointers[height - 1 - i] = tmpTex->data[0]  + i * rowbytes;
	row_pointers[             i] = tmpTex->data[0]  + i*rowbytes;


  //Decompressing PNG to RAW where row_pointers are pointing (tmpTex->data[0])
	png_read_image(png_ptr, row_pointers);

  //Last but not least:


	// Free LIBPNG internal state.
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

  //Free the decompression buffer
  free(row_pointers);

  FS_CloseFile(file);
}

int Native_IsFrenchLanguage(void) { return 0; }	// v2: menu localization (EN on Android for now)
const char* Native_GetVersionString(void) { return "v?"; }	// v5: no bundle to read here
