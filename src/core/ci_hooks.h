#ifndef DE_CI_HOOKS
#define DE_CI_HOOKS

// The CI smoke tests need switches the camera cannot press: invulnerability
// for an unattended ship, autofire, a replayed scene, a forced menu. They were
// read straight from getenv(), which put them -- and their names -- inside
// every build, the App Store one included. App Review read that as
// functionality hidden from the reviewer (guideline 5.6) and refused 5.0.11.
//
// Now a single compile-time switch decides. SHMUP_CI_HOOKS is defined ONLY on
// the xcodebuild command line of the smoke workflows; no build defines it by
// default, so a shipping binary holds neither the lookups nor the literals:
// CI_GETENV folds to a null pointer and the compiler drops the string with it.
//
// Rule for anything added later: a test-only switch goes through CI_GETENV,
// never through getenv(). testflight.yml greps the archived binary for
// "SHMUP_" and fails if a name survives.

#ifdef SHMUP_CI_HOOKS
	#include <stdlib.h>
	#define CI_GETENV(name)	getenv(name)
#else
	#define CI_GETENV(name)	((char*)0)
#endif

#endif
