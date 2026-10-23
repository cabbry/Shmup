#ifndef DE_SHMUP
#define DE_SHMUP

void Log_Init(void);
int Log_Printf(const char *fmt,...);
// The diagnostic probes ([cull], [scene], [title], [devil], [music]...) exist
// only in the test builds. Elsewhere the gate is the literal 0: each
// "if (Log_ProbesEnabled())" block, its strings included, compiles away, and
// clang reads a macro constant as configuration, not as dead code.
#ifdef SHMUP_CI_HOOKS
int Log_ProbesEnabled(void);	// SHMUP_CULL_DEBUG, read once
#else
#define Log_ProbesEnabled() 0
#endif
  
#endif