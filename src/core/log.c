#include "log.h"
#include "ci_hooks.h"
#include "filesystem.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "target.h"

//Logging to stdout with printf in Android doesn't work, we need to use __android_log_vprint
#if defined (SHMUP_TARGET_ANDROID)
    #include <android/log.h>	// round 90: declared, the NDK's clang refuses the implicit declaration
    #define vprintf(x,y) __android_log_vprint(4,"net.fabiensanglard",x,y)
#endif

filehandle_t* logFile_Handle;


//Macro to control where to log.
#define LOG_TO_CONSOLE 1

// One shared gate for the diagnostic probes ([cull], [scene], [title],
// [devil], [replay]): they only speak when SHMUP_CULL_DEBUG is set (the CI
// smoke sets it; a player's device never does). Cached once -- getenv is not
// free and some probes sit near per-frame code.
#ifdef SHMUP_CI_HOOKS
int Log_ProbesEnabled(void)
{
	static int flag = -1;
	if (flag < 0)
		flag = CI_GETENV("SHMUP_CULL_DEBUG") ? 1 : 0;
	return flag;
}
#endif

// File logging, off unless SHMUP_LOG_FILE is set in the environment. It writes
// shmup_log.txt in the FS writable dir (Documents on iOS) and flushes every
// line, so the log survives a crash and can be pulled off a device or a CI
// Simulator container -- the console channel alone proved unreliable to capture
// (simctl's pty capture came back empty on some runs, leaving us blind).
#ifdef SHMUP_CI_HOOKS
static int logToFile = 0;
#else
#define logToFile 0	// no log file outside the test builds, and no code for one
#endif


void Log_Init(void)
{
#ifdef SHMUP_CI_HOOKS
	logToFile = (CI_GETENV("SHMUP_LOG_FILE") != NULL);
#endif

	if (logToFile)
		logFile_Handle = FS_OpenFile("shmup_log.txt","w");

	Log_Printf("\n\n");

}


int Log_Printf(const char *fmt,...){

    va_list ap;

#if !defined (SHMUP_TARGET_ANDROID)
	// A va_list can only be walked once, hence one va_start per consumer.
	if (logToFile && logFile_Handle)
	{
		va_start(ap, fmt);
		vfprintf(logFile_Handle->hFile, fmt, ap);
		va_end(ap);
		fflush(logFile_Handle->hFile);
	}
#endif

	if (LOG_TO_CONSOLE){
		va_start(ap, fmt);
		vprintf(fmt,ap);
		va_end(ap);
		fflush(stdout);
	}

    return 0;
}