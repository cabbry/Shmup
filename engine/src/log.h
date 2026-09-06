#ifndef DE_SHMUP
#define DE_SHMUP

void Log_Init(void);
int Log_Printf(const char *fmt,...);
int Log_ProbesEnabled(void);	// SHMUP_CULL_DEBUG gate for the [cull]/[scene]/[title] probes
  
#endif