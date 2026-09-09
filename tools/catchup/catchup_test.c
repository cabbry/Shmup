/*
 *  catchup_test.c -- the multiplayer catch-up arithmetic, on a laptop.
 *
 *  Timer_tick advances the simulation by a FIXED ~16.67 ms per RENDERED FRAME.
 *  Deterministic, and right for solo -- but it means a device that drops frames
 *  does not fall behind, it runs SLOW, and two phones at different frame rates
 *  end a level in different moments of it. That is the desync the tester saw
 *  ("la synchro marche mais finit par se desynchroniser en fin de niveau",
 *  2026-09-09). dEngine_CatchUp pays the simulation whatever whole steps wall
 *  time is owed, with the renderer off, and draws only the last one.
 *
 *  The decision is a pure function in engine/src/dEngine.c, between the markers
 *  CATCHUP-ARITHMETIC-BEGIN / -END. The Makefile-less build below EXTRACTS that
 *  block into catchup_arithmetic.inc and includes it, so this tests the shipped
 *  lines rather than a copy of them:
 *
 *    awk '/CATCHUP-ARITHMETIC-BEGIN/{f=1;next} /CATCHUP-ARITHMETIC-END/{f=0} f' \
 *        ../../engine/src/dEngine.c > catchup_arithmetic.inc
 *    zig cc -std=gnu99 catchup_test.c -o catchup_test && ./catchup_test
 */
#include <stdio.h>

#include "catchup_arithmetic.inc"

/* CATCHUP_MAX_STEPS bounds the BURST, because every step beyond the first is
   simulation the player never sees drawn. Two extra steps carry three steps per
   frame, which tracks the wall clock down to this rate; below it the game slows
   again, on purpose -- a phone at 15 fps has lost the argument anyway. */
#define CATCHUP_FLOOR_FPS	20

static int gFailures = 0, gChecks = 0;

static void check(int cond, const char* what, ...)
{
	gChecks++;
	if (cond) return;
	gFailures++;
	printf("  FAIL: %s\n", what);
}

/* A level's worth of frames at `rate` fps. The wall clock is fractional and the
   frame delta is read as whole milliseconds, exactly as E_Sys_Milliseconds()
   hands it over. Returns simulated seconds per wall second. */
static double run(int rate, int fixOn, double levelMs)
{
	double wallF = 0, sim = 0;
	float debt = 0.0f;
	int lastMs = 0;

	while (wallF < levelMs)
	{
		int nowMs, delta, extra;
		wallF += 1000.0 / rate;
		nowMs  = (int)wallF;
		delta  = nowMs - lastMs;
		lastMs = nowMs;
		extra  = fixOn ? dEngine_CatchUpSteps(&debt, delta) : 0;
		sim   += (1 + extra) * (double)CATCHUP_STEP_MS;		/* the frame's own step, plus the catch-up */
	}
	return sim / wallF;
}

int main(void)
{
	static const int rates[] = { 60, 55, 50, 40, 30, 24, 20, 15 };
	const double LEVEL = 140000.0;		/* act 1 is 140 s */
	int i;

	printf("=== the catch-up arithmetic (extracted from dEngine.c) ===\n");

	printf("140 s of a level, one step per rendered frame (the bug):\n");
	for (i = 0; i < 8; i++)
	{
		double r = run(rates[i], 0, LEVEL);
		printf("  %3d fps -> %.3f of real time  (%+.1fs of game time over the level)\n",
		       rates[i], r, (r - 1.0) * LEVEL / 1000.0);
	}

	printf("the same, with the catch-up (the cap covers down to %d fps):\n", CATCHUP_FLOOR_FPS);
	for (i = 0; i < 8; i++)
	{
		double r = run(rates[i], 1, LEVEL);
		printf("  %3d fps -> %.3f of real time  (%+.1fs)%s\n", rates[i], r, (r - 1.0) * LEVEL / 1000.0,
		       rates[i] < CATCHUP_FLOOR_FPS ? "   (below the cap: still slows, on purpose)" : "");
		if (rates[i] >= CATCHUP_FLOOR_FPS)
			check(r > 0.99 && r < 1.01, "a frame rate at or above the floor must not change how fast the game runs");
		else
			check(r > 0.5, "below the floor the game must degrade gently, not stop");
	}

	/* The pair that matters: the tester's two phones. */
	{
		double fast = run(60, 1, LEVEL) * LEVEL / 1000.0;
		double slow = run(50, 1, LEVEL) * LEVEL / 1000.0;
		double gap  = fast - slow;
		printf("two phones at 60 and 50 fps end the level %.1fs of game time apart (was 23.3s)\n",
		       gap < 0 ? -gap : gap);
		check(gap < 1.5 && gap > -1.5, "two frame rates must end a level in the same moment of it");
	}

	/* A long frame is not lag. A scene load, a breakpoint or a return from the
	   background must not be repaid at all -- 180 steps in one frame would look
	   like the world fast-forwarding. */
	{
		float debt = 0.0f;
		check(dEngine_CatchUpSteps(&debt, 3000) == 0, "a 3 s frame must buy no catch-up at all");
		check(debt == 0.0f, "a stall must leave no debt behind");
		debt = 0.0f;
		check(dEngine_CatchUpSteps(&debt, 200) <= CATCHUP_MAX_STEPS, "a long frame must stay under the cap");
		debt = 0.0f;
		check(dEngine_CatchUpSteps(&debt, 16) == 0, "a 60 fps frame must buy nothing");
		check(dEngine_CatchUpSteps(&debt, 17) == 0, "nor must the 17 ms one that follows it");
	}

	printf("=== %d checks, %d failures ===\n", gChecks, gFailures);
	return gFailures ? 1 : 0;
}
