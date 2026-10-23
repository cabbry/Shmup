#ifndef DE_CI_HOOKS
#define DE_CI_HOOKS

// Test switches for the simulator smoke tests: an unattended ship that cannot
// die, autofire, a scene to boot into or replay, a menu to open. They exist
// only in builds compiled with SHMUP_CI_HOOKS, which the smoke and screenshot
// workflows pass on the xcodebuild line and nothing else does.
//
// Each switch is a whole block inside #ifdef SHMUP_CI_HOOKS -- the lookup, what
// it does, and its log lines -- so a build without the flag holds none of it.
// CI_GETENV has no definition outside those builds on purpose: a test switch
// written anywhere else does not compile.
//
// The diagnostic probes follow the same rule through Log_ProbesEnabled (log.h).
// testflight.yml checks the archived app for what must not be in it.

#ifdef SHMUP_CI_HOOKS
	#include <stdlib.h>
	#define CI_GETENV(name)	getenv(name)
#endif

#endif
