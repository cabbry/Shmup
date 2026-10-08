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
// default, so a shipping binary holds neither the lookups nor the literals.
//
// Rule for anything added later: a test-only switch goes through CI_GETENV,
// never through getenv(). testflight.yml greps the archived binary for
// "SHMUP_" and fails if a name survives.

#ifdef SHMUP_CI_HOOKS
	#include <stdlib.h>
	#define CI_GETENV(name)	getenv(name)
#else
	// A plain ((char*)0) would be simpler, and wrong: clang then proves the
	// "found" half of every `CI_GETENV(x) ? 1 : 0` dead, and this project
	// builds with -Wunreachable-code -Werror. A static inline is opaque to
	// that check -- it runs on the syntax tree, before inlining -- while the
	// optimiser still drops the call, its unused argument, and the string
	// literal with it. At -O0 the literal survives -- Debug is never shipped,
	// and the guard reads the Release archive. Verified, not assumed.
	static __inline char* ci_no_hook(const char* name) { (void)name; return 0; }
	#define CI_GETENV(name)	ci_no_hook(name)
#endif

#endif
