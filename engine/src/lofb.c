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
 *  lofb.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10-10-25.
 *  Copyright 2010 Memset software Inc. All rights reserved.
 *
 */

// The LOFB is the boss of act 3 ("Water") -- the fight the original 2009/2010
// project ran out of budget for. The scene, the title card (boss.png) and this
// file's scaffolding shipped back then with an empty updateLOFB; this finally
// implements the fight, driven only by simulation state (positions, timers in
// enemy->parameters[], HP) so it stays deterministic in lockstep multiplayer.

#include <string.h>
#include <stdlib.h>	// v5: getenv (the CI-only SHMUP_ARMS_FREEZE switch)

#include "lofb.h"
#include "globals.h"
#include "timer.h"
#include "player.h"
#include "dEngine.h"
#include "fx.h"
#include "sounds.h"
#include "titles.h"
#include "log.h"	// stage 2b: the [boss] probe
#include "event.h"
#include "native_services.h"
#include "enemy_particules.h"
#include "md5.h"		// v5: the arm bones (MD5_GenerateSkin)
#include "quaternion.h"
#include <math.h>	// v3: explicit -- the Xcode prefix header hid the dependency (implicit-declaration class)

// Reused engine services with no public prototype.
extern void emitSHABBullet(enemy_t* enemy, float angle);	// shab.c: one enemy bullet at an angle
extern void EV_SpawnEnemy(event_t* event);					// event.c: spawn from a payload
extern void EV_AutoPilotPls(event_t* event);				// event.c: fly players to rest position
extern void Spawn_EntityParticules(vec2_t ss_position);	// collisions.c: a burst of yellow sparks (round 77: the torn arm and its stump)

// enemy->parameters[] slots: float scratch storage, per enemy. timeCounter is a
// ushort (wraps at ~65s), too small for a boss fight, so time lives here.
#define P_TIME			0	// ms since spawn
#define P_FAN_CD		1	// cooldown: aimed fan burst
#define P_SPIRAL_CD		2	// cooldown: spiral emitter
#define P_SPIRAL_ANGLE	3	// current spiral angle (radians)
#define P_MINION_CD		4	// cooldown: escort wave
#define P_BIGSHOT_CD	5	// cooldown: the big shot

// Tuning.
#define LOFB_ARRIVE_MS			6000.0f
#define LOFB_HOVER_Y			0.55f
#define LOFB_SWAY_HALFWIDTH		0.45f
#define LOFB_SWAY_PERIOD_MS		9000.0f
#define LOFB_BOB_PERIOD_MS		3700.0f
#define LOFB_VICTORY_BONUS		100000

// THE MEGA-LASER (Fabien's round-2 ask): every ~30-45s the boss stops, gathers
// energy (converging sparks -- the "particules qui se concentrent"), then fires
// a huge beam that pivots from straight-down and sweeps the lower screen. The
// safe ground is UP, in a top corner (the beam never points above the boss).
// It vaporises escort minions it crosses. Purely simulation-state driven, so it
// stays deterministic in lockstep multiplayer; the beam quads are pushed into
// enFxLib (the LEE-style enemy FX buffer), so no renderer change is needed.
#define LOFB_LASER_OFF			0
#define LOFB_LASER_CHARGING		1
#define LOFB_LASER_FIRING		2

#define LOFB_LASER_FIRST_MS		14000.0f	// first beam, into the fight
#define LOFB_LASER_PERIOD_MS	32000.0f	// then one every ~32s (30-45 window)
#define LOFB_LASER_CHARGE_MS	2200.0f		// telegraph: longer so the beam is easy to anticipate
#define LOFB_LASER_FIRE_MS		3500.0f		// beam sweep duration
#define LOFB_LASER_SWEEP_AMP	0.62f		// radians off straight-down (~35 deg; was 1.15 / 66 until round 77 --
											// "le gros laser prend trop d'angle": the crook under the arm was inside the sweep)
#define LOFB_LASER_SWEEP_CYCLES	1.2f		// sweep speed (faster than 1.3.6, calmer than 1.3.5)
#define LOFB_LASER_HALFWIDTH	(0.12f * SS_H)	// beam half-thickness (pixels)
#define LOFB_LASER_LENGTH		(2.5f  * SS_H)	// beam length (crosses the screen)
#define LOFB_LASER_SPARKS		24			// converging charge orbs (denser = clearer telegraph)
#define LOFB_LASER_TEXT_U		((ushort)(72.0f / 128.0f * SHRT_MAX))	// solid white
#define LOFB_LASER_TEXT_V		((ushort)( 8.0f / 128.0f * SHRT_MAX))	// atlas texel
// Soft round blob (the SHAB/big-shot orb sprite) for sparks + muzzle glow, so
// they read as light rather than hard squares.
#define LOFB_ORB_U				((ushort)(80.0f / 128.0f * SHRT_MAX))
#define LOFB_ORB_V				((ushort)( 0.0f / 128.0f * SHRT_MAX))
#define LOFB_ORB_W				((ushort)(16.0f / 128.0f * SHRT_MAX))
#define LOFB_ORB_H				((ushort)(16.0f / 128.0f * SHRT_MAX))

// Boss HUD state (read by LOFB_GetBossHealth, rendered with the score).
static int gBossEnergy = 0;
static int gBossMaxEnergy = 0;
static int gBossHudStamp = -100000;	// simulationTime of the last boss update

// v4 stage 2b: the attack ladder as FLAGS. The built-in thresholds set them
// from hpPct every frame (2009+ behaviour, bit for bit); a scene whose rules
// carry bossAttack actions takes the ladder over and only the rules move them.
static int  gLadderScripted = 0;
static char gAttackOn[LOFB_NUM_ATTACKS];
static const char* gAttackNames[LOFB_NUM_ATTACKS] = { "spray", "minions", "bigshot", "missiles", "frenzy" };

static void LOFB_SetAttackFlag(int which, int on)
{
	on = on ? 1 : 0;
	if (which < 0 || which >= LOFB_NUM_ATTACKS || gAttackOn[which] == on)
		return;
	gAttackOn[which] = (char)on;
	if (Log_ProbesEnabled())
		Log_Printf("[boss] t=%d attack %s %s\n", simulationTime, gAttackNames[which], on ? "on" : "off");
}

// Mega-laser state (single boss -> file statics; all advanced from updateLOFB so
// both lockstep sims evolve them identically). The beam is drawn from the FX
// buffer; collisions read it through LOFB_GetLaserBeam.
static int   gLaserState    = LOFB_LASER_OFF;
static float gLaserCooldown = 0;	// ms until the next beam (while OFF)
static float gLaserTimer    = 0;	// ms left in the current sub-state
static float gLaserCharge   = 0;	// 0..1 charge progress (CHARGING)
static float gLaserDX = 0, gLaserDY = -1;	// beam unit direction (screen space)
static float gLaserOX = 0, gLaserOY = 0;	// beam origin (pixels: ss * SS_W/SS_H)
static float gSwayClock = 0;		// hover-sway time; pauses while the laser is up

// Homing-missile launcher (fired from the arms). Cooldown is a file static like
// the laser -- single boss, advanced deterministically from updateLOFB.
static float gMissileCooldown = 0;
static float gSeekerPort  = 1;	// body launch port alternates left/right per seeker
static float gArmShotSide = 1;	// which arm fires the next energy shot (alternates)
#define LOFB_MISSILE_SPEED	0.85f	// ss units / second
#define LOFB_MISSILE_TURN	2.0f	// max turn rate (rad / second) -- low enough to juke
#define LOFB_MISSILE_TTL	6000	// ms before it fizzles out
#define LOFB_MISSILE_ARM_X	0.24f	// arm offset from the boss centre
#define LOFB_MISSILE_CD		4500.0f	// seeker interval (ONE per volley now, from the
									// body -- the old arm pair fired two per 6500ms)
#define P_MISSILE_HEADING	0		// missile's own parameters[] slot (its heading)

// Destructible arms. The two big side arms have their own HP; the BIG ENERGY
// SHOTS (-50% unlock) fire from them, alternating sides -- destroy an arm and
// that side goes quiet, destroy both and the energy shots stop entirely. The
// homing seekers (-75%) come from the BODY instead, so the finale can't be
// pre-empted by early arm kills. State is file-static (single boss).
#define LOFB_ARM_HP			400		// per arm (doubled in multiplayer) -- beefy
									// enough to plausibly live until the -50% unlock
#define LOFB_ARM_OFFX		0.34f	// arm hit-zone offset from the boss centre (ss)
#define LOFB_ARM_OFFY		0.06f	// arms sit just above the body centre
#define LOFB_ARM_RADIUS		0.15f	// arm hit radius (ss)
static short  gArmHP[2]    = {0, 0};	// [0]=left, [1]=right
static int    gArmAlive[2] = {0, 0};
static vec2_t gArmSS[2];				// current arm centres (ss), refreshed each frame
static float  gArmSparkTimer = 0;		// throttle for wreck smoke/sparks
static short  gArmMaxHP     = LOFB_ARM_HP;	// per-arm starting HP (x2 in MP)
static float  gArmFlashMs[2] = {0, 0};	// arm-localised hit-flash countdown (ms)
#define LOFB_ARM_FLASH_MS	130.0f
// Damage owed to the GLOBAL health bar by destroyed arms. The arms' own HP pool
// is separate, but blowing one off carves a visible chunk (8% of max) out of the
// boss bar -- applied from updateLOFB, which holds the enemy pointer.
static int    gArmChunkDmg = 0;

// DEGRADATION (Fabien, 2026-09-17: "de la fumée, il tremble, il rougit, il
// devient plus agressif"). Everything here is a function of the boss's HP and
// of simulationTime, advanced from updateLOFB -- so both lockstep sims see the
// same smoke, the same tint, the same cadences. The shake is applied to the
// entity MATRIX only (enemy.c asks LOFB_GetShakeOffset after it has placed the
// mesh): the hitbox, the arm zones and the muzzles never move with it.
#define LOFB_KICK_MS			110.0f	// a body hit kicks the ship for this long
#define LOFB_SHAKE_BASE			0.012f	// ss units of tremor at 0 HP (from half HP up)
#define LOFB_SHAKE_KICK			0.009f	// extra ss units at the instant of a hit
#define LOFB_SMOKE_FROM			0.40f	// body smoke starts once 40 % is lost
#define LOFB_AGGR_GAIN			0.22f	// cadences shorten by up to 22 % at 0 HP
static short  gLastEnergy   = 0;	// to notice a hit (energy went down this frame)
static float  gHitKickMs    = 0;	// kick countdown
static float  gBodySmokeMs  = 0;	// throttle for the body smoke
static int    gBodySmokeIdx = 0;	// which vent smokes next (three, in turn)

// THE ARMS AS BONES (v5, Fabien's last note: "animer les bras"). The boss mesh
// is rigged in three bones by tools/rig (0 body, 1 armL, 2 armR, pivots at the
// shoulders, a two-weight blend across the shoulder) and loaded DYNAMIC: its
// vertexArray stays in RAM and MD5_GenerateSkin re-skins it from the bone
// array below every frame. Each arm's pose is two angles -- a swing about the
// mesh Y axis (the axis facing the camera: the claw opens and closes in the
// screen plane) and a tilt about the mesh X axis (the arm folds toward or away
// from the camera) -- composed from:
//   idle     a slow +/-6 deg breathing swing, the two arms in mirror;
//   recoil   the arm that fires the big shot snaps back 18 deg and returns
//            over LOFB_ARM_RECOIL_MS;
//   flinch   a bullet on the arm tilts it 12 deg for the hit-flash time;
//   tremor   from half arm HP, a 2 Hz shiver;
//   wreck    a destroyed arm folds 75 deg toward the camera over half a second
//            and hangs there, swinging limp. It is NOT detached: the shoulder
//            blend would stretch to a bone that walked away. The wreck smoke
//            and sparks already mark it.
// Everything is a function of (arm HP, arm death time, last shot, last hit,
// simulationTime): both lockstep sims skin the same boss. Cost: ~1200
// vertices re-skinned per frame on the CPU.
#define LOFB_ARM_RECOIL_MS		300.0f
#define LOFB_ARM_WRECK_FOLD_MS	500.0f
#define LOFB_ARM_IDLE_DEG		6.0f
#define LOFB_ARM_RECOIL_DEG		18.0f
#define LOFB_ARM_FLINCH_DEG		12.0f
#define LOFB_ARM_TREMOR_DEG		1.5f
#define LOFB_ARM_WRECK_DEG		75.0f
static md5_bone_t gPoseBones[3];		// the rest bones, copied from the mesh, with the arms re-oriented
static int        gPoseBonesValid = 0;	// gPoseBones holds a copy of the current mesh's bones
static float      gArmRecoilMs[2] = {0, 0};	// per arm: ms left in the big-shot recoil
static int        gArmDeadAt[2]   = {-1, -1};	// per arm: simulationTime of destruction, -1 alive
static float      gBossSS[2]      = {0, 0};	// the boss's ss position, stamped by updateLOFB (for the arm solids)
static vec3_t     gRestPivot[2];			// the arm bones' rest positions (from the mesh), for the tear-off offsets

// TORN OFF (round 76, the tester: "si un bras est détruit il faudrait
// carrément l'arracher, et des étincelles sortant du corps à l'emplacement du
// bras"). The mesh is now CUT at the shoulder (tools/rig, seam duplicated), so
// an arm bone can leave without dragging a body triangle: over
// LOFB_ARM_TEAR_MS the dead arm tumbles outward and down the screen,
// accelerating, then is parked far below the screen. The stump sparks and
// smokes from the shoulder pivot for the rest of the fight.
#define LOFB_ARM_TEAR_MS		1200.0f
#define LOFB_ARM_TEAR_DROP		60.0f	// mesh units down the screen at the end of the tumble
#define LOFB_ARM_TEAR_OUT		10.0f	// ... and outward
#define LOFB_ARM_PARKED_Z		4000.0f	// where a torn arm waits (off any screen)

// THE PINCER (Fabien's idea, the tester's timing): both live arms swing open
// (the telegraph), then SNAP shut in front of the body, hold, and return. The
// solids ride the bones, so a ship that does not back off is caught. It fires
// right after the mega-laser (the ship is often parked in a crook) and now and
// then at a pseudo-random interval -- never while the laser charges or fires,
// or the crook would be a trap. Pseudo-random = a hash of simulationTime and
// the boss's energy at scheduling: the same in both lockstep sims.
#define LOFB_PINCH_OPEN_MS		400.0f
#define LOFB_PINCH_SNAP_MS		350.0f
#define LOFB_PINCH_HOLD_MS		250.0f
#define LOFB_PINCH_RETURN_MS	600.0f
#define LOFB_PINCH_OPEN_DEG		-18.0f	// wider than rest (negative swing = outward)
#define LOFB_PINCH_SHUT_DEG		45.0f	// the claws swing in under the body (the hinge is at the tube now: a long lever)
#define LOFB_PINCH_AFTER_LASER_MS 300.0f
#define LOFB_PINCH_MIN_GAP_MS	8000
#define LOFB_PINCH_RAND_MS		7000
#define LOFB_PINCH_LASER_GUARD_MS 3000.0f	// no pinch if the laser is due within this
enum { PINCH_OFF = 0, PINCH_OPEN, PINCH_SNAP, PINCH_HOLD, PINCH_RETURN };
static int   gPinchState   = PINCH_OFF;
static float gPinchTimer   = 0;		// ms in the current state
static int   gPinchNextAt  = 0;		// simulationTime of the next random pinch
static int   gPinchRequestAt = -1;	// simulationTime at which a pinch was asked for (after the laser), -1 none
static float gPinchSwing   = 0;		// the swing the pinch adds to both live arms this frame (deg)

// SOLID ARMS (v5, the tester: "avant on pouvait traverser les bras avec le
// vaisseau; maintenant qu'ils bougent il ne faut plus, mais le laser ne doit
// pas atteindre le creux"). Each arm is covered by a set of CIRCLES built once
// from the rigged mesh at rest: the arm's vertices, in bone space (X outward
// from the shoulder, Z down the screen), are binned on a 2 x 3 unit grid and
// every occupied cell becomes a circle (centroid, radius = farthest vertex +
// a margin). Fine enough that the arm's own hollows stay open -- in
// particular THE CROOK: the notch above the forearm against the claw
// (silhouette measured by tools/rig: a shoulder plate x 8..15 spanning z
// -11..+1, a thin forearm x 15..17 at z -3..+1.5, the claw x 17..23 spreading
// down to z +11). The crook sits ABOVE the boss's centre line, which is why a
// beam that only ever points down cannot reach it -- and LOFB_ClampSweep
// makes that a guarantee rather than a coincidence: the sweep is reduced,
// deterministically, whenever the capsule would touch a live arm's crook.
// Every circle is carried by the posed bone, so the danger follows the
// picture. A destroyed arm has no solids and no crook to protect.
#define LOFB_SOLID_MAX		48
#define LOFB_SOLID_CELL_X	2.0f
#define LOFB_SOLID_CELL_Z	3.0f
#define LOFB_SOLID_MARGIN	0.6f	// mesh units added to every circle
// The crook is CARVED, not found: the natural notch above the forearm is 1.9
// units wide and the ship's collision radius is 1.8, so solids that follow
// the mesh to the letter would close the refuge. Cells whose centroid falls
// in the pocket below are left out (the ship may overlap the plate's outer
// corner and the claw's top edge while parked -- the arm is solid everywhere
// else). The pocket: bone x 5..10.5 (mesh 13.5..19), bone z -7..+2 (mesh
// -12.3..-3.3, i.e. ABOVE the forearm, screen-up of the boss's centre line).
// Round 77, the tester on device: the hinge is the TUBE that joins the body
// to the arm (the cut moved to |X| = 5.5, pivot (5.5, 0.44, 1.16), the arm
// = shoulder block + claw), and the crook he hides in is UNDER the arm,
// between the body and the claw -- mesh x 9..16, z 2..9, BELOW the centre
// line, which the old +/-66 degree sweep reached ("il nous kill quand même").
// Two pockets are carved: that one (the crook proper, whose centre the laser
// clamp protects) and the notch above the arm, both in bone space of the new
// pivot. The sweep amplitude drops to 40 degrees so the crook is clear by
// construction; LOFB_ClampSweep stays as the guarantee.
#define LOFB_CROOK_X0		3.5f	// the crook: bone x (outward from the tube)
#define LOFB_CROOK_X1		10.5f
#define LOFB_CROOK_Z0		0.84f	// ... bone z (down the screen)
#define LOFB_CROOK_Z1		7.84f
#define LOFB_UPPER_X0		8.0f	// the notch above the arm
#define LOFB_UPPER_X1		13.5f
#define LOFB_UPPER_Z0		-13.5f
#define LOFB_UPPER_Z1		-4.5f
#define LOFB_CROOK_BX		8.0f	// crook centre, bone space (mesh 13.5, 5.5)
#define LOFB_CROOK_BZ		4.34f
#define LOFB_CROOK_R		3.0f
#define LOFB_SHIP_R_SS		0.035f	// the ship's radius the laser test uses (0.035 * SS_H px), in ss y units
typedef struct { float bx, bz, r; } lofb_solid_t;	// bone space, mesh units, XZ plane
static lofb_solid_t gArmSolid[2][LOFB_SOLID_MAX];
static int          gArmSolidCount[2] = {0, 0};

static void LOFB_BuildArmSolids(const md5_mesh_t* mesh)
{
	int k;
	for (k = 0; k < 2; k++)
	{
		enum { NX = 9, NZ = 9 };
		float sign = (k == 0) ? -1.0f : 1.0f;
		float pz = gPoseBones[1 + k].position[2];
		int   n[NX][NZ]; float sx[NX][NZ], sz[NX][NZ], rr[NX][NZ];
		int i, a, b;
		memset(n, 0, sizeof(n)); memset(sx, 0, sizeof(sx)); memset(sz, 0, sizeof(sz)); memset(rr, 0, sizeof(rr));
		// pass 1: centroids
		for (i = 0; i < mesh->numVertices; i++)
		{
			float bx = sign * mesh->vertexArray[i].pos[0] - gPoseBones[2].position[0];
			float bz = mesh->vertexArray[i].pos[2] - pz;
			if (bx < 0) continue;					// body side of the shoulder
			a = (int)(bx / LOFB_SOLID_CELL_X); b = (int)((bz + 13.5f) / LOFB_SOLID_CELL_Z);
			if (a >= NX) a = NX - 1; if (b < 0) b = 0; if (b >= NZ) b = NZ - 1;
			n[a][b]++; sx[a][b] += bx; sz[a][b] += bz;
		}
		// pass 2: radii
		for (i = 0; i < mesh->numVertices; i++)
		{
			float bx = sign * mesh->vertexArray[i].pos[0] - gPoseBones[2].position[0];
			float bz = mesh->vertexArray[i].pos[2] - pz, dx, dz, d;
			if (bx < 0) continue;
			a = (int)(bx / LOFB_SOLID_CELL_X); b = (int)((bz + 13.5f) / LOFB_SOLID_CELL_Z);
			if (a >= NX) a = NX - 1; if (b < 0) b = 0; if (b >= NZ) b = NZ - 1;
			dx = bx - sx[a][b] / n[a][b]; dz = bz - sz[a][b] / n[a][b];
			d = sqrtf(dx * dx + dz * dz);
			if (d > rr[a][b]) rr[a][b] = d;
		}
		gArmSolidCount[k] = 0;
		for (a = 0; a < NX; a++)
			for (b = 0; b < NZ; b++)
				if (n[a][b] > 0 && gArmSolidCount[k] < LOFB_SOLID_MAX)
				{
					float cx = sx[a][b] / n[a][b], cz = sz[a][b] / n[a][b];
					lofb_solid_t* s;
					if ((cx >= LOFB_CROOK_X0 && cx <= LOFB_CROOK_X1 && cz >= LOFB_CROOK_Z0 && cz <= LOFB_CROOK_Z1) ||
						(cx >= LOFB_UPPER_X0 && cx <= LOFB_UPPER_X1 && cz >= LOFB_UPPER_Z0 && cz <= LOFB_UPPER_Z1))
						continue;	// the crook and the upper notch: carved out, the ship's refuges
					s = &gArmSolid[k][gArmSolidCount[k]++];
					s->bx = cx; s->bz = cz; s->r = rr[a][b] + LOFB_SOLID_MARGIN;
				}
		if (Log_ProbesEnabled())
			Log_Printf("[boss] arm %d: %d solid circles\n", k, gArmSolidCount[k]);
	}
}

// A bone-space point of arm k (bx outward, bz down) to MESH space (x, z),
// through the arm's current pose.
static void LOFB_ArmPointToMesh(int k, float bx, float bz, float* mx, float* mz)
{
	vec3_t in, out;
	float sign = (k == 0) ? -1.0f : 1.0f;
	in[0] = sign * bx; in[1] = 0; in[2] = bz;
	Quat_rotatePoint(gPoseBones[1 + k].orientation, in, out);
	*mx = out[0] + gPoseBones[1 + k].position[0];
	*mz = out[2] + gPoseBones[1 + k].position[2];
}

int LOFB_PlayerHitsArm(float ssX, float ssY)
{
	int k, i;
	float px, pz, shipR;
	if (gBossMaxEnergy <= 0 || simulationTime - gBossHudStamp > 300 || gBossHudStamp > simulationTime)
		return 0;										// no boss on-screen
	if (!gPoseBonesValid || widthAtDistance <= 0 || heightAtDistance <= 0)
		return 0;
	// The ship into the boss's mesh space: ss offsets scaled by the world width
	// and height of one ss unit at the enemies' depth (enemy.c places them with
	// exactly these), mesh Z running DOWN the screen.
	px =  (ssX - gBossSS[X]) * widthAtDistance;
	pz = -(ssY - gBossSS[Y]) * heightAtDistance;
	shipR = LOFB_SHIP_R_SS * heightAtDistance;
	for (k = 0; k < 2; k++)
	{
		if (gArmDeadAt[k] >= 0 || !gArmAlive[k])
			continue;
		for (i = 0; i < gArmSolidCount[k]; i++)
		{
			float mx, mz, dx, dz, reach;
			LOFB_ArmPointToMesh(k, gArmSolid[k][i].bx, gArmSolid[k][i].bz, &mx, &mz);
			dx = px - mx; dz = pz - mz; reach = gArmSolid[k][i].r + shipR;
			if (dx * dx + dz * dz < reach * reach)
				return 1;
		}
	}
	return 0;
}

// Would the beam (origin ox,oy px; unit dir dx,dy) with the ship's margin
// touch a live arm's crook? Same capsule test as collisions.c, with the crook's
// radius added.
static int LOFB_BeamTouchesCrook(float ox, float oy, float dx, float dy)
{
	int k;
	if (!gPoseBonesValid || widthAtDistance <= 0 || heightAtDistance <= 0)
		return 0;
	for (k = 0; k < 2; k++)
	{
		float mx, mz, cx, cy, rx, ry, proj, perp, rpx;
		if (gArmDeadAt[k] >= 0 || !gArmAlive[k])
			continue;
		LOFB_ArmPointToMesh(k, LOFB_CROOK_BX, LOFB_CROOK_BZ, &mx, &mz);
		cx = (gBossSS[X] + mx / widthAtDistance)  * SS_W;
		cy = (gBossSS[Y] - mz / heightAtDistance) * SS_H;
		rpx = LOFB_CROOK_R / heightAtDistance * SS_H;
		rx = cx - ox; ry = cy - oy;
		proj = rx * dx + ry * dy;
		perp = rx * dy - ry * dx; if (perp < 0) perp = -perp;
		if (proj > -0.15f * LOFB_LASER_LENGTH && proj < LOFB_LASER_LENGTH && perp < LOFB_LASER_HALFWIDTH + LOFB_SHIP_R_SS * SS_H + rpx)
			return 1;
	}
	return 0;
}

// The sweep angle the beam may take: the requested one, or the largest
// smaller angle (same sign) whose capsule misses every live crook. Straight
// down always misses (the crooks are above the origin, off to the sides), so
// the bisection has a safe end. Pure function of the boss state.
static float LOFB_ClampSweep(float sweep, float ox, float oy)
{
	float lo = 0, hi = sweep, sgn = (sweep < 0) ? -1.0f : 1.0f, mag = fabsf(sweep);
	float ang, dx, dy;
	int it;
	ang = -(float)(M_PI / 2.0) + sweep; dx = cosf(ang); dy = sinf(ang);
	if (!LOFB_BeamTouchesCrook(ox, oy, dx, dy))
		return sweep;
	hi = mag; lo = 0;
	for (it = 0; it < 10; it++)
	{
		float mid = (lo + hi) * 0.5f;
		ang = -(float)(M_PI / 2.0) + sgn * mid; dx = cosf(ang); dy = sinf(ang);
		if (LOFB_BeamTouchesCrook(ox, oy, dx, dy)) hi = mid; else lo = mid;
	}
	if (Log_ProbesEnabled())
		Log_Printf("[boss] t=%d laser sweep clamped %.3f -> %.3f rad for the crook\n", simulationTime, sweep, sgn * lo);
	return sgn * lo;
}

static void LOFB_QuatAxisAngle(float ax, float ay, float az, float deg, quat4_t q)
{
	float half = deg * (float)M_PI / 360.0f, s = sinf(half);
	q[0] = ax * s; q[1] = ay * s; q[2] = az * s; q[3] = cosf(half);
}

// Pose the two arm bones and re-skin the mesh. Safe on any mesh: a one-joint
// model or a GPU-resident one is left alone.
static void LOFB_PoseArms(enemy_t* enemy)
{
	md5_mesh_t* mesh = enemy->entity.model;
	float t = (float)simulationTime;
	int k;

	if (!mesh || mesh->numBones != 3 || mesh->memLocation == MD5_MEMLOC_VRAM || !mesh->vertexArray)
		return;
	{
		// CI-only A/B switch: SHMUP_ARMS_FREEZE=1 leaves the mesh in its rest
		// skin (the rig loaded, the poses never applied).
		static int freeze = -1;
		if (freeze < 0) freeze = getenv("SHMUP_ARMS_FREEZE") ? 1 : 0;
		if (freeze) return;
	}
	if (!gPoseBonesValid)
	{
		memcpy(gPoseBones, mesh->bones, sizeof(gPoseBones));
		vectorCopy(mesh->bones[1].position, gRestPivot[0]);
		vectorCopy(mesh->bones[2].position, gRestPivot[1]);
		gPoseBonesValid = 1;
		LOFB_BuildArmSolids(mesh);	// from the rest skin the loader left in vertexArray
	}

	for (k = 0; k < 2; k++)
	{
		float mirror = (k == 0) ? 1.0f : -1.0f;	// the left claw opens the other way
		float sign   = (k == 0) ? -1.0f : 1.0f;	// outward along mesh X
		float swing, tilt;
		quat4_t qy, qx;

		vectorCopy(gRestPivot[k], gPoseBones[1 + k].position);	// a live arm turns about its shoulder
		if (gArmRecoilMs[k] > 0) gArmRecoilMs[k] -= timediff;

		if (gArmDeadAt[k] >= 0)
		{
			// TORN OFF: tumble outward and down for LOFB_ARM_TEAR_MS, then park
			// far below the screen. The stump's sparks live in updateLOFB.
			float since = (float)(simulationTime - gArmDeadAt[k]);
			float f = since / LOFB_ARM_TEAR_MS;
			if (f > 1) f = 1;
			gPoseBones[1 + k].position[0] += sign * LOFB_ARM_TEAR_OUT * f;
			gPoseBones[1 + k].position[2] += LOFB_ARM_TEAR_DROP * f * f;		// accelerating fall
			if (since >= LOFB_ARM_TEAR_MS)
				gPoseBones[1 + k].position[2] += LOFB_ARM_PARKED_Z;
			tilt  = 140.0f * f;											// tumbles over
			swing = 70.0f * f;											// and folds inward as it goes
		}
		else
		{
			swing = LOFB_ARM_IDLE_DEG * sinf(t * (float)(2 * M_PI) / 4000.0f + 0.6f * k);
			tilt  = 2.0f * sinf(t * (float)(2 * M_PI) / 2300.0f + 1.1f * k);
			swing += gPinchSwing;											// the pincer, both live arms alike
			if (gArmRecoilMs[k] > 0)
			{
				float r = gArmRecoilMs[k] / LOFB_ARM_RECOIL_MS;	// 1 at the shot, 0 at rest
				swing -= LOFB_ARM_RECOIL_DEG * r * r;
				tilt  += 5.0f * r;
			}
			if (gArmFlashMs[k] > 0)
				tilt += LOFB_ARM_FLINCH_DEG * (gArmFlashMs[k] / LOFB_ARM_FLASH_MS);
			if (gArmAlive[k] && gArmHP[k] <= gArmMaxHP / 2)	// (alive: before the fight gArmHP is stale)
				swing += LOFB_ARM_TREMOR_DEG * sinf(t * 0.01257f + 3.0f * k);	// 2 Hz
		}

		{
			// CI-only calibration: SHMUP_ARMS_TEST=1 forces a constant pose --
			// left arm tilted 12 deg about X, right arm swung 30 deg about Y --
			// so one Simulator frame shows what each axis does on screen.
			static int test = -1;
			if (test < 0) test = getenv("SHMUP_ARMS_TEST") ? 1 : 0;
			if (test) { swing = (k == 1) ? 30.0f : 0.0f; tilt = (k == 0) ? 12.0f : 0.0f; }
		}

		LOFB_QuatAxisAngle(0, 1, 0, swing * mirror, qy);
		LOFB_QuatAxisAngle(1, 0, 0, tilt, qx);
		Quat_multQuat(qy, qx, gPoseBones[1 + k].orientation);

		// [arm] probe (SHMUP_CULL_DEBUG): the pose inputs, once a second per arm.
		if (Log_ProbesEnabled())
		{
			static int lastProbeSec = -1;
			int sec = simulationTime / 1000;
			if (sec != lastProbeSec)
			{
				if (k == 1) lastProbeSec = sec;
				Log_Printf("[arm] t=%d k=%d swing=%.1f tilt=%.1f dead=%d flash=%.0f recoil=%.0f hp=%d/%d alive=%d q=(%.3f %.3f %.3f %.3f) pivot=(%.1f %.1f %.1f)\n",
						   simulationTime, k, swing, tilt, gArmDeadAt[k], gArmFlashMs[k], gArmRecoilMs[k], gArmHP[k], gArmMaxHP, gArmAlive[k],
						   gPoseBones[1 + k].orientation[0], gPoseBones[1 + k].orientation[1], gPoseBones[1 + k].orientation[2], gPoseBones[1 + k].orientation[3],
						   gPoseBones[1 + k].position[0], gPoseBones[1 + k].position[1], gPoseBones[1 + k].position[2]);
			}
		}
	}
	MD5_GenerateSkin(mesh, gPoseBones);
	if (Log_ProbesEnabled())
	{
		// the right claw's tip (the vertex farthest along +X at rest is vertex
		// with the largest x in the first skin; cheap: track it once)
		static int tip = -1;
		if (tip < 0)
		{
			int i; float best = -1e9f;
			for (i = 0; i < mesh->numVertices; i++)
				if (mesh->vertexArray[i].pos[0] > best) { best = mesh->vertexArray[i].pos[0]; tip = i; }
		}
		if ((simulationTime / 1000) != ((simulationTime - (int)timediff) / 1000))
			Log_Printf("[arm] t=%d tipR=(%.2f %.2f %.2f) bones=%d\n", simulationTime,
					   mesh->vertexArray[tip].pos[0], mesh->vertexArray[tip].pos[1], mesh->vertexArray[tip].pos[2], mesh->numBones);
	}
}

static float LOFB_AimAngleFrom(float sx, float sy)
{
	// Angle (screen space) from (sx,sy) toward the nearest player. Player
	// positions are lockstep-synced, so this stays deterministic in multiplayer.
	int i;
	float best = 1e9f;
	float tx = 0, ty = -1;	// fallback: straight down

	for (i = 0; i < numPlayers; i++)
	{
		float dx = players[i].ss_position[X] - sx;
		float dy = players[i].ss_position[Y] - sy;
		float d2 = dx*dx + dy*dy;
		if (d2 < best) { best = d2; tx = dx; ty = dy; }
	}
	return atan2f(ty, tx);
}

static float LOFB_AimAngle(enemy_t* enemy)
{
	return LOFB_AimAngleFrom(enemy->ss_position[X], enemy->ss_position[Y]);
}

static void LOFB_FireFan(enemy_t* enemy, int count, float spread)
{
	int i;
	float aim = LOFB_AimAngle(enemy);
	for (i = 0; i < count; i++)
		emitSHABBullet(enemy, aim + (i - (count - 1) * 0.5f) * spread);
}

static void LOFB_SpawnMinion(float side, float startX)
{
	// One FHT escort diving in from the top; built like a .scene spawnEnemy line.
	event_t ev;
	event_spawnEnemy_payload_t pl;

	memset(&pl, 0, sizeof(pl));
	pl.type = 1;							// FHT
	pl.mouvementPatternType = MVMT_STRAIGHT;
	pl.startPosition[X] = side * startX;	pl.startPosition[Y] = 1.25f;
	pl.endPosition[X]   = -side * 0.3f;		pl.endPosition[Y]   = -1.3f;
	pl.controlPoint[X]  = side * startX;	pl.controlPoint[Y]  = 0.0f;
	pl.ttl = 6000;
	pl.subType = 0;							// NORMAL

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_SPAWN_ENEMY;
	ev.payload = &pl;
	EV_SpawnEnemy(&ev);

	// The end-of-act "Enemy cleared" ratio divides kills by ENE_Precache's count
	// of the SCRIPTED spawns -- reinforcements spawned here at runtime were kills
	// the denominator never knew about, so long fights read over 100%.
	engine.playerStats.numEnemies++;
}

// Launch one homing missile from an arm (side = -1 left, +1 right). It spawns as
// a normal ENEMY_MISSILE, so it has HP (shootable) and the shared death path
// gives it an explosion for free. updateLOFBMissile steers it toward the player.
static void LOFB_FireMissile(enemy_t* enemy, float side)
{
	event_t ev;
	event_spawnEnemy_payload_t pl;

	memset(&pl, 0, sizeof(pl));
	pl.type = ENEMY_MISSILE;
	pl.mouvementPatternType = MVMT_STRAIGHT;	// ignored -- the update fn homes
	pl.startPosition[X] = enemy->ss_position[X] + side * LOFB_MISSILE_ARM_X;
	pl.startPosition[Y] = enemy->ss_position[Y] - 0.05f;
	pl.endPosition[X]   = pl.startPosition[X];	pl.endPosition[Y]   = -1.3f;
	pl.controlPoint[X]  = pl.startPosition[X];	pl.controlPoint[Y]  = 0.0f;
	pl.ttl = LOFB_MISSILE_TTL;
	pl.subType = 0;								// NORMAL (energy from enemyTypeEnergy)

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_SPAWN_ENEMY;
	ev.payload = &pl;
	EV_SpawnEnemy(&ev);

	engine.playerStats.numEnemies++;	// same bookkeeping as LOFB_SpawnMinion
}

// A boss homing missile: pops out at a body launch port, then steers toward the nearest
// player at a capped turn rate (dodgeable). Deterministic (player positions are
// lockstep-synced). Its HP + destruction are handled by COLL_CheckEnemies; this
// only drives motion, a red tint (to read as a missile, not an escort) and TTL.
void updateLOFBMissile(enemy_t* enemy)
{
	float dt = timediff / 1000.0f;
	float ax = 0, ay = -1, best = 1e9f;
	float ang, desired, diff, maxTurn;
	int i;

	// First frame: appear at the spawn (arm) point, heading downward.
	if (enemy->timeCounter == 0)
	{
		enemy->ss_position[X] = enemy->spawn_startPosition[X];
		enemy->ss_position[Y] = enemy->spawn_startPosition[Y];
		enemy->parameters[P_MISSILE_HEADING] = -(float)M_PI / 2.0f;
	}

	// Seek the nearest player.
	for (i = 0; i < numPlayers; i++)
	{
		float dx = players[i].ss_position[X] - enemy->ss_position[X];
		float dy = players[i].ss_position[Y] - enemy->ss_position[Y];
		float d2 = dx * dx + dy * dy;
		if (d2 < best) { best = d2; ax = dx; ay = dy; }
	}
	desired = atan2f(ay, ax);

	// Turn toward the target, but only so fast (leaves room to juke it).
	ang  = enemy->parameters[P_MISSILE_HEADING];
	diff = desired - ang;
	while (diff >  (float)M_PI) diff -= (float)(2 * M_PI);
	while (diff < -(float)M_PI) diff += (float)(2 * M_PI);
	maxTurn = LOFB_MISSILE_TURN * dt;
	if (diff >  maxTurn) diff =  maxTurn;
	if (diff < -maxTurn) diff = -maxTurn;
	ang += diff;

	// Never turn back UPWARD. The escort mesh is only meant to be seen flying
	// down-screen; a U-turn would show its undesigned underside. Clamp to a
	// downward cone (straight-down +/- ~85 deg): if the player slips above it,
	// the missile just keeps descending and exits instead of looping back.
	{
		float down = -(float)M_PI / 2.0f;	// -Y == down the screen
		float d = ang - down;
		while (d >  (float)M_PI) d -= (float)(2 * M_PI);
		while (d < -(float)M_PI) d += (float)(2 * M_PI);
		if (d >  1.48f) d =  1.48f;			// ~85 degrees off straight-down
		if (d < -1.48f) d = -1.48f;
		ang = down + d;
	}

	enemy->parameters[P_MISSILE_HEADING] = ang;

	enemy->ss_position[X] += cosf(ang) * LOFB_MISSILE_SPEED * dt;
	enemy->ss_position[Y] += sinf(ang) * LOFB_MISSILE_SPEED * dt;

	// Point the model along its travel (seen from above -> Z) and tint it red-hot.
	enemy->entity.zAxisRot = ang + (float)M_PI / 2.0f;
	enemy->entity.color[R] = 1.0f;
	enemy->entity.color[G] = 0.35f;
	enemy->entity.color[B] = 0.15f;
	enemy->entity.color[A] = 1.0f;

	// End of life, or gone off the bottom / far to the sides.
	if (enemy->timeCounter >= enemy->ttl ||
		enemy->ss_position[Y] < -1.5f ||
		enemy->ss_position[X] < -1.7f || enemy->ss_position[X] > 1.7f)
		ENE_Release(enemy);
}

// The BIG SHOT: a large, slower orb aimed at the nearest player. It has its OWN
// sprite now: the 48x48 cell region at (32,32) of the bullet atlas (painted at
// 192 px in the 512 atlas by tools/cards/make_bullets.ps1). Until 4.2.12 it
// borrowed the 16 px SHAB orb, drawn thirteen times its size: a bilinear blur
// with the neighbouring cells bleeding a square frame around it -- Fabien's
// "projectile buggé". UVs are atlas fractions, so the atlas size is free.
#define LOFB_BIG_TTL		3600
#define LOFB_BIG_DISTANCE	1.6f
#define LOFB_BIG_SIZE		0.22f
#define LOFB_TEXT_BULLET_U      (32/128.0f*SHRT_MAX)
#define LOFB_TEXT_BULLET_V      (32/128.0f*SHRT_MAX)
#define LOFB_TEXT_BULLET_WIDTH  (48/128.0f*SHRT_MAX)
#define LOFB_TEXT_BULLET_HEIGHT (48/128.0f*SHRT_MAX)
// offX shifts the muzzle sideways: the energy shots fire from the ARMS now
// (offX = +/-LOFB_ARM_OFFX), aimed from the arm's own position.
static void LOFB_FireBigShot(enemy_t* enemy, float offX)
{
	enemy_part_t* bullet;
	float ox = enemy->ss_position[X] + offX;
	float oy = enemy->ss_position[Y] + LOFB_ARM_OFFY;
	float angle = LOFB_AimAngleFrom(ox, oy);

	gArmRecoilMs[offX < 0 ? 0 : 1] = LOFB_ARM_RECOIL_MS;	// v5: the firing arm snaps back

	bullet = ENPAR_GetNextParticule();

	bullet->ttl = LOFB_BIG_TTL;
	bullet->originalTTL = LOFB_BIG_TTL;

	bullet->ss_boudaries[UP]    = bullet->ss_starting_boudaries[UP]    = oy * SS_H + LOFB_BIG_SIZE /2 * SS_H / gVScale;
	bullet->ss_boudaries[DOWN]  = bullet->ss_starting_boudaries[DOWN]  = oy * SS_H - LOFB_BIG_SIZE /2 * SS_H / gVScale;
	bullet->ss_boudaries[LEFT]  = bullet->ss_starting_boudaries[LEFT]  = ox * SS_W - LOFB_BIG_SIZE /2 *SS_H/(float)SS_W * SS_W;
	bullet->ss_boudaries[RIGHT] = bullet->ss_starting_boudaries[RIGHT] = ox * SS_W + LOFB_BIG_SIZE /2 *SS_H/(float)SS_W * SS_W;

	bullet->text[0][U] = LOFB_TEXT_BULLET_U;
	bullet->text[0][V] = LOFB_TEXT_BULLET_V;
	bullet->text[1][U] = LOFB_TEXT_BULLET_U;
	bullet->text[1][V] = LOFB_TEXT_BULLET_V + LOFB_TEXT_BULLET_HEIGHT;
	bullet->text[2][U] = LOFB_TEXT_BULLET_U + LOFB_TEXT_BULLET_WIDTH;
	bullet->text[2][V] = LOFB_TEXT_BULLET_V + LOFB_TEXT_BULLET_HEIGHT;
	bullet->text[3][U] = LOFB_TEXT_BULLET_U + LOFB_TEXT_BULLET_WIDTH;
	bullet->text[3][V] = LOFB_TEXT_BULLET_V;

	bullet->posDiff[X] = cosf(angle) * LOFB_BIG_DISTANCE * SS_H;
	bullet->posDiff[Y] = sinf(angle) * LOFB_BIG_DISTANCE * SS_H;
}

// --- Mega-laser ------------------------------------------------------------

// Append one white-texel quad (4 arbitrary corners) to the enemy FX buffer --
// same mechanism LEE uses. Per-vertex alpha (aa..ad, one per corner) lets the
// beam fade smoothly at its edges instead of ending in a hard rectangle.
static void LOFB_PushQuadA(float ax, float ay, float bx, float by,
						   float cx, float cy, float dx, float dy,
						   ubyte r, ubyte g, ubyte b,
						   ubyte aa, ubyte ab, ubyte ac, ubyte ad)
{
	xf_sprite_t* s;
	float px[4], py[4];
	ubyte pa[4];
	int i;

	if (enFxLib.num_vertices + 4 > 4 * MAX_NUM_ENEMY_FX)
		return;

	px[0] = ax; py[0] = ay; pa[0] = aa;
	px[1] = bx; py[1] = by; pa[1] = ab;
	px[2] = cx; py[2] = cy; pa[2] = ac;
	px[3] = dx; py[3] = dy; pa[3] = ad;

	s = &enFxLib.ss_vertices[enFxLib.num_vertices];
	for (i = 0; i < 4; i++)
	{
		s->pos[X]  = (short)px[i];
		s->pos[Y]  = (short)py[i];
		s->text[U] = LOFB_LASER_TEXT_U;
		s->text[V] = LOFB_LASER_TEXT_V;
		s->color[R] = r; s->color[G] = g; s->color[B] = b; s->color[A] = pa[i];
		s++;
	}
	enFxLib.num_vertices += 4;
	enFxLib.num_indices  += 6;
}

// A soft round sprite (the orb texel) centred at (cx,cy), half-size hs. Used for
// the charge sparks and the muzzle glow so they read as light, not squares.
static void LOFB_PushSprite(float cx, float cy, float hs,
							ubyte r, ubyte g, ubyte b, ubyte a)
{
	xf_sprite_t* s;
	short xs[4], ys[4];
	ushort us[4], vs[4];
	int i;

	if (enFxLib.num_vertices + 4 > 4 * MAX_NUM_ENEMY_FX)
		return;

	xs[0] = (short)(cx - hs); ys[0] = (short)(cy + hs); us[0] = LOFB_ORB_U;			vs[0] = LOFB_ORB_V;
	xs[1] = (short)(cx - hs); ys[1] = (short)(cy - hs); us[1] = LOFB_ORB_U;			vs[1] = LOFB_ORB_V + LOFB_ORB_H;
	xs[2] = (short)(cx + hs); ys[2] = (short)(cy - hs); us[2] = LOFB_ORB_U + LOFB_ORB_W; vs[2] = LOFB_ORB_V + LOFB_ORB_H;
	xs[3] = (short)(cx + hs); ys[3] = (short)(cy + hs); us[3] = LOFB_ORB_U + LOFB_ORB_W; vs[3] = LOFB_ORB_V;

	s = &enFxLib.ss_vertices[enFxLib.num_vertices];
	for (i = 0; i < 4; i++)
	{
		s->pos[X]  = xs[i]; s->pos[Y] = ys[i];
		s->text[U] = us[i]; s->text[V] = vs[i];
		s->color[R] = r; s->color[G] = g; s->color[B] = b; s->color[A] = a;
		s++;
	}
	enFxLib.num_vertices += 4;
	enFxLib.num_indices  += 6;
}

// The beam, drawn as a stack of parallel strips across its width so the alpha
// rises to a bright core and fades to nothing at the edges (soft glow), plus a
// gentle fade toward the far tip for depth. `peak` is the core alpha (0..255).
static void LOFB_PushBeamSoft(float ox, float oy, float dx, float dy,
							  float hw, float len, float peak)
{
	static const float frac[7] = {-1.0f, -0.60f, -0.28f, 0.0f, 0.28f, 0.60f, 1.0f};
	static const float aScl[7] = { 0.0f,  0.30f,  0.78f, 1.0f, 0.78f, 0.30f, 0.0f};
	const float tipFade = 0.40f;			// far end melts into the distance
	float px = -dy, py = dx;				// unit perpendicular
	int i;

	for (i = 0; i < 6; i++)
	{
		float p0 = frac[i]   * hw, p1 = frac[i + 1] * hw;
		float a0 = aScl[i]   * peak, a1 = aScl[i + 1] * peak;
		float m  = (fabsf(frac[i]) + fabsf(frac[i + 1])) * 0.5f;	// 0 core .. 1 edge
		ubyte r  = (ubyte)(255.0f - m * 165.0f);	// white core -> cyan edge
		ubyte g  = (ubyte)(255.0f - m *  55.0f);
		ubyte b  = 255;

		// Rounded base: instead of a straight cut across the origin, each strip's
		// near edge pulls back along -d by the circle's height at its lateral
		// offset -- the six strips then trace a semicircular cap (capsule end).
		float q0 = hw*hw - p0*p0;  if (q0 < 0) q0 = 0;
		float q1 = hw*hw - p1*p1;  if (q1 < 0) q1 = 0;
		float b0 = sqrtf(q0), b1 = sqrtf(q1);

		float n0x = ox - dx * b0 + px * p0,  n0y = oy - dy * b0 + py * p0;
		float n1x = ox - dx * b1 + px * p1,  n1y = oy - dy * b1 + py * p1;
		float f0x = ox + dx * len + px * p0, f0y = oy + dy * len + py * p0;
		float f1x = ox + dx * len + px * p1, f1y = oy + dy * len + py * p1;

		LOFB_PushQuadA(n0x, n0y, n1x, n1y, f1x, f1y, f0x, f0y, r, g, b,
					   (ubyte)a0, (ubyte)a1,
					   (ubyte)(a1 * tipFade), (ubyte)(a0 * tipFade));
	}
}

// A smooth round glow: a fan of wedges, bright at the centre and fading to zero
// alpha at the rim (Gouraud gradient on the flat white texel). Unlike a scaled-up
// 16px orb sprite it has no visible pixels, so the muzzle halo stays clean.
#define LOFB_GLOW_WEDGES 14
static void LOFB_PushGlow(float cx, float cy, float radius,
						  ubyte r, ubyte g, ubyte b, ubyte centerA)
{
	int i;
	for (i = 0; i < LOFB_GLOW_WEDGES; i++)
	{
		float a0 = i       * (float)(2 * M_PI) / LOFB_GLOW_WEDGES;
		float a1 = (i + 1) * (float)(2 * M_PI) / LOFB_GLOW_WEDGES;
		float x0 = cx + cosf(a0) * radius, y0 = cy + sinf(a0) * radius;
		float x1 = cx + cosf(a1) * radius, y1 = cy + sinf(a1) * radius;
		// Degenerate first tri (centre,centre,rim1); the wedge is tri (centre,rim1,rim0).
		LOFB_PushQuadA(cx, cy, cx, cy, x1, y1, x0, y0, r, g, b,
					   centerA, centerA, 0, 0);
	}
}

// Advance the laser timers/state. Direction points straight down while charging
// (telegraph) and sweeps left/right about straight-down while firing.
static void LOFB_UpdateLaser(enemy_t* enemy)
{
	(void)enemy;

	if (gLaserState == LOFB_LASER_OFF)
	{
		gLaserDX = 0; gLaserDY = -1;
		gLaserCooldown -= timediff;
		if (gLaserCooldown <= 0)
		{
			gLaserState  = LOFB_LASER_CHARGING;
			gLaserTimer  = LOFB_LASER_CHARGE_MS;
			gLaserCharge = 0;
			// Throw in an escort wave to be sliced by the beam.
			LOFB_SpawnMinion(-1.0f, 0.70f);
			LOFB_SpawnMinion( 1.0f, 0.70f);
			LOFB_SpawnMinion(-1.0f, 0.35f);
			LOFB_SpawnMinion( 1.0f, 0.35f);
		}
		return;
	}

	if (gLaserState == LOFB_LASER_CHARGING)
	{
		gLaserDX = 0; gLaserDY = -1;
		gLaserTimer -= timediff;
		gLaserCharge = 1.0f - gLaserTimer / LOFB_LASER_CHARGE_MS;
		if (gLaserCharge < 0) gLaserCharge = 0;
		if (gLaserCharge > 1) gLaserCharge = 1;
		if (gLaserTimer <= 0)
		{
			gLaserState  = LOFB_LASER_FIRING;
			gLaserTimer  = LOFB_LASER_FIRE_MS;
			gLaserCharge = 1.0f;
			SND_PlaySound(SND_EXPLOSION);	// beam ignition
		}
		return;
	}

	/* LOFB_LASER_FIRING */
	{
		float frac, sweep, ang;
		gLaserTimer -= timediff;
		frac = 1.0f - gLaserTimer / LOFB_LASER_FIRE_MS;
		if (frac < 0) frac = 0;
		if (frac > 1) frac = 1;
		sweep = LOFB_LASER_SWEEP_AMP * sinf(frac * (float)(2 * M_PI) * LOFB_LASER_SWEEP_CYCLES);
		// v5: never into the crook of a live arm (the ship's refuge).
		sweep = LOFB_ClampSweep(sweep, enemy->ss_position[X] * SS_W, enemy->ss_position[Y] * SS_H);
		ang   = -(float)(M_PI / 2.0) + sweep;	// straight-down + sweep
		gLaserDX = cosf(ang);
		gLaserDY = sinf(ang);
		if (gLaserTimer <= 0)
		{
			gLaserState    = LOFB_LASER_OFF;
			gLaserCooldown = LOFB_LASER_PERIOD_MS;
			gPinchRequestAt = simulationTime + (int)LOFB_PINCH_AFTER_LASER_MS;	// round 76: the pincer follows the beam
		}
	}
}

// Draw the laser for this frame (charge sparks + telegraph, or the live beam)
// and publish the beam origin for collisions. Boss position is frozen while the
// laser is up (gSwayClock paused), so the pivot is stable.
static void LOFB_EmitLaserFX(enemy_t* enemy)
{
	float ox, oy;

	if (gLaserState == LOFB_LASER_OFF)
		return;

	gLaserOX = ox = enemy->ss_position[X] * SS_W;
	gLaserOY = oy = enemy->ss_position[Y] * SS_H;

	if (gLaserState == LOFB_LASER_CHARGING)
	{
		int i;
		float c   = gLaserCharge;
		float tMs = LOFB_LASER_CHARGE_MS - gLaserTimer;	// deterministic FX clock
		// Orbs hang WIDE at first, then RUSH the muzzle (1 - c^2 easing): the
		// inward acceleration is what sells "all the energy just got sucked in".
		float rad  = (1.0f - c * c) * (0.95f * SS_H);
		ubyte al   = (ubyte)(140 + c * 115);
		float core = (0.05f + 0.26f * c) * SS_H;	// gathering ball swells
		float warn = 30.0f + c * c * 170.0f;		// telegraph ramps up hard near the end
		// Over the last quarter the whole gather STROBES -- final unmissable beat.
		float pulse = (c > 0.75f) ? (0.80f + 0.20f * sinf(tMs * 0.045f)) : 1.0f;

		// A soft ball of energy swelling at the muzzle...
		LOFB_PushGlow(ox, oy, core * 1.9f, 200, 90, 230, (ubyte)((40 + c * 120) * pulse));
		LOFB_PushGlow(ox, oy, core,        255, 255, 255, (ubyte)((60 + c * 160) * pulse));

		// ...fed by a swarm of BULLET ORBS (boss-bullet magenta) spiralling inward
		// ever faster, each dragging a bright tail aimed at the muzzle so the
		// direction of flow -- INTO the gun -- reads at a glance.
		for (i = 0; i < LOFB_LASER_SPARKS; i++)
		{
			float ang = i * (float)(2 * M_PI) / LOFB_LASER_SPARKS + c * c * 14.0f;
			float sx  = ox + cosf(ang) * rad;
			float sy  = oy + sinf(ang) * rad;
			float hs  = 0.050f * SS_H * (0.45f + 1.05f * c);
			float tx  = ox - sx, ty = oy - sy;
			float d   = sqrtf(tx * tx + ty * ty);

			LOFB_PushSprite(sx, sy, hs, 255, 90, 220, al);

			// Convergence tail: a thin wedge from the orb toward the muzzle,
			// faint at the orb and bright at the tip (motion streak pointing in).
			if (d > 1.0f)
			{
				float sl, pxp, pyp, wd;
				tx /= d; ty /= d;
				sl = (0.10f + 0.30f * c) * SS_H;
				if (sl > d) sl = d;
				pxp = -ty; pyp = tx;
				wd  = 2.0f + 3.0f * c;
				LOFB_PushQuadA(sx + pxp * wd, sy + pyp * wd,
							   sx - pxp * wd, sy - pyp * wd,
							   sx + tx * sl,  sy + ty * sl,
							   sx + tx * sl,  sy + ty * sl,
							   255, 140, 235,
							   (ubyte)(al / 3), (ubyte)(al / 3), al, al);
			}
		}
		// Bright warning line clearly showing WHERE the beam will erupt -- it ramps
		// up sharply as the charge completes so you can dodge in time.
		LOFB_PushBeamSoft(ox, oy, gLaserDX, gLaserDY,
						  LOFB_LASER_HALFWIDTH * (0.35f + 0.4f * c), LOFB_LASER_LENGTH,
						  warn);
		return;
	}

	/* LOFB_LASER_FIRING: soft-edged beam with a bright core, a subtle energy
	   pulse, and a layered muzzle glow. */
	{
		float pulse = 0.85f + 0.15f * sinf((LOFB_LASER_FIRE_MS - gLaserTimer) * 0.02f);
		LOFB_PushBeamSoft(ox, oy, gLaserDX, gLaserDY,
						  LOFB_LASER_HALFWIDTH, LOFB_LASER_LENGTH, 235.0f * pulse);
		LOFB_PushGlow(ox, oy, 0.36f * SS_H, 110, 200, 255, (ubyte)(120 * pulse));
		LOFB_PushGlow(ox, oy, 0.19f * SS_H, 255, 255, 255, (ubyte)(225 * pulse));
	}
}

// Arm hit feedback: an additive glow flashed over an arm that just took a bullet.
// The boss never full-body flickers (one mesh -> the whole ship lit up, see
// collisions.c), so this is what makes an ARM hit read as "I'm hurting THIS part".
static void LOFB_EmitArmFX(void)
{
	int k;
	for (k = 0; k < 2; k++)
	{
		float f;
		if (!gArmAlive[k] || gArmFlashMs[k] <= 0)
			continue;
		f = gArmFlashMs[k] / LOFB_ARM_FLASH_MS;
		LOFB_PushGlow(gArmSS[k][X] * SS_W, gArmSS[k][Y] * SS_H,
					  LOFB_ARM_RADIUS * 1.5f * SS_H,
					  255, 240, 190, (ubyte)(150 * f));
	}
}

// Collision query: fills the beam ray (pixel space) and returns 1 only while the
// beam is actually FIRING (charging is harmless). Read by collisions.c.
int LOFB_GetLaserBeam(float* ox, float* oy, float* dx, float* dy,
					  float* halfWidth, float* length)
{
	if (gLaserState != LOFB_LASER_FIRING)
		return 0;
	*ox = gLaserOX; *oy = gLaserOY;
	*dx = gLaserDX; *dy = gLaserDY;
	*halfWidth = LOFB_LASER_HALFWIDTH;
	*length    = LOFB_LASER_LENGTH;
	return 1;
}

// Arm hit-zone query for collisions.c: returns 1 (and fills the arm's ss centre +
// radius) only while that arm is alive AND the boss is actively updating.
int LOFB_GetArm(int idx, float* ssx, float* ssy, float* radius)
{
	if (idx < 0 || idx > 1 || !gArmAlive[idx])
		return 0;
	if (gBossMaxEnergy <= 0 || simulationTime - gBossHudStamp > 300 || gBossHudStamp > simulationTime)
		return 0;	// boss not on-screen / not being updated
	*ssx = gArmSS[idx][X];
	*ssy = gArmSS[idx][Y];
	*radius = LOFB_ARM_RADIUS;
	return 1;
}

// Apply damage to an arm; when it drops to 0 the arm is destroyed (big burst +
// smoke) and that side stops launching homing missiles.
void LOFB_DamageArm(int idx, int dmg)
{
	if (idx < 0 || idx > 1 || !gArmAlive[idx])
		return;
	gArmHP[idx] -= dmg;
	gArmFlashMs[idx] = LOFB_ARM_FLASH_MS;	// the ARM (not the whole ship) lights up
	if (gArmHP[idx] <= 0)
	{
		// Make the kill UNMISSABLE -- a triple burst walking off the arm. During
		// a laser phase the screen is busy (orbs, beams, dodging) and playtests
		// misattributed arm deaths ("the laser destroyed its own arms"): the
		// arms only ever take PLAYER bullet damage, so the reward for landing it
		// has to cut through the noise.
		vec2_t p;
		gArmAlive[idx] = 0;
		gArmDeadAt[idx] = simulationTime;	// v5: the arm bone folds and hangs from now on
		p[X] = gArmSS[idx][X];
		p[Y] = gArmSS[idx][Y];
		FX_GetExplosion(p, IMPACT_TYPE_YELLOW, 1.6f, 0);
		p[X] += (idx == 0) ? -0.08f : 0.08f;
		p[Y] += 0.06f;
		FX_GetExplosion(p, IMPACT_TYPE_YELLOW, 1.1f, 0);
		p[Y] -= 0.13f;
		FX_GetExplosion(p, IMPACT_TYPE_YELLOW, 0.8f, 0);
		FX_GetSmoke(p, 0.6f, 0.6f);
		// round 77 ("des flammes, mais j'aimerais des étincelles aussi"): a
		// shower of sparks flying off the tear
		Spawn_EntityParticules(p);
		p[X] -= (idx == 0) ? -0.06f : 0.06f;
		Spawn_EntityParticules(p);
		p[Y] += 0.08f;
		Spawn_EntityParticules(p);
		SND_PlaySound(SND_EXPLOSION);
		gArmChunkDmg += (gBossMaxEnergy * 8) / 100;
	}
}

// The pincer's state machine, advanced once per frame while FIGHTING. Sets
// gPinchSwing for LOFB_PoseArms. See the constants above for the beats.
static float LOFB_EaseIn(float f)  { return f * f * f; }
static float LOFB_EaseOut(float f) { return 1.0f - (1.0f - f) * (1.0f - f); }
static void LOFB_UpdatePinch(enemy_t* enemy)
{
	int liveArms = (gArmAlive[0] && gArmDeadAt[0] < 0) + (gArmAlive[1] && gArmDeadAt[1] < 0);
	int laserBusy = (gLaserState != LOFB_LASER_OFF) || (gLaserCooldown < LOFB_PINCH_LASER_GUARD_MS);

	gPinchSwing = 0;
	if (enemy->state != LOFB_STATE_FIGHTING || liveArms == 0)
	{
		gPinchState = PINCH_OFF;
		return;
	}

	// Start: asked for after the laser, or the random clock -- never with the
	// laser up or imminent.
	if (gPinchState == PINCH_OFF && !laserBusy)
	{
		int wanted = (gPinchRequestAt >= 0 && simulationTime >= gPinchRequestAt) || (simulationTime >= gPinchNextAt);
		if (wanted)
		{
			unsigned h = (unsigned)simulationTime * 2654435761u + (unsigned)enemy->energy * 97u;
			gPinchState = PINCH_OPEN; gPinchTimer = 0;
			gPinchRequestAt = -1;
			gPinchNextAt = simulationTime + LOFB_PINCH_MIN_GAP_MS + (int)((h >> 8) % LOFB_PINCH_RAND_MS);
			if (Log_ProbesEnabled())
				Log_Printf("[boss] t=%d pinch: open (next random at %d)\n", simulationTime, gPinchNextAt);
		}
	}
	// The laser wants the stage: whatever the pinch was doing, it returns.
	if (gPinchState != PINCH_OFF && gPinchState != PINCH_RETURN && gLaserState != LOFB_LASER_OFF)
	{
		gPinchState = PINCH_RETURN; gPinchTimer = 0;
	}

	switch (gPinchState)
	{
	case PINCH_OPEN:
		gPinchTimer += timediff;
		gPinchSwing = LOFB_PINCH_OPEN_DEG * LOFB_EaseOut(gPinchTimer / LOFB_PINCH_OPEN_MS > 1 ? 1 : gPinchTimer / LOFB_PINCH_OPEN_MS);
		if (gPinchTimer >= LOFB_PINCH_OPEN_MS) { gPinchState = PINCH_SNAP; gPinchTimer = 0; SND_PlaySound(SND_EXPLOSION); }
		break;
	case PINCH_SNAP:
		gPinchTimer += timediff;
		{
			float f = gPinchTimer / LOFB_PINCH_SNAP_MS; if (f > 1) f = 1;
			gPinchSwing = LOFB_PINCH_OPEN_DEG + (LOFB_PINCH_SHUT_DEG - LOFB_PINCH_OPEN_DEG) * LOFB_EaseIn(f);
		}
		if (gPinchTimer >= LOFB_PINCH_SNAP_MS) { gPinchState = PINCH_HOLD; gPinchTimer = 0; }
		break;
	case PINCH_HOLD:
		gPinchTimer += timediff;
		gPinchSwing = LOFB_PINCH_SHUT_DEG;
		if (gPinchTimer >= LOFB_PINCH_HOLD_MS) { gPinchState = PINCH_RETURN; gPinchTimer = 0; }
		break;
	case PINCH_RETURN:
		gPinchTimer += timediff;
		{
			float f = gPinchTimer / LOFB_PINCH_RETURN_MS; if (f > 1) f = 1;
			gPinchSwing = LOFB_PINCH_SHUT_DEG * (1.0f - LOFB_EaseOut(f));
		}
		if (gPinchTimer >= LOFB_PINCH_RETURN_MS) { gPinchState = PINCH_OFF; gPinchSwing = 0; }
		break;
	default:
		break;
	}
}

// The hull degrades with the HP LOST (0 pristine .. 1 dead), every frame, in
// both sims alike:
//  - it REDDENS from a quarter lost: white -> a hot, dark red at 0 HP, with a
//    slow throb over the last quarter (entity.color, modulated by the renderer);
//  - it SMOKES from 40 % lost: three vents on the body take turns, the interval
//    closing from 700 to 250 ms and the puffs growing; from 70 % lost every
//    other puff is a spark burst;
//  - a body hit KICKS it (gHitKickMs, read by LOFB_GetShakeOffset) once a
//    quarter is lost, and from half HP a permanent tremor sets in.
// The pool budget: body smoke holds at most four puffs (ttl 1 s), the arms two.
static void LOFB_Degrade(enemy_t* enemy, float lost)
{
	float red, throb;

	// A hit this frame: energy went down. (Arm chunks arrive through energy
	// too -- a blown arm kicks the ship, as it should.)
	if (enemy->energy < gLastEnergy && lost > 0.25f)
		gHitKickMs = LOFB_KICK_MS;
	gLastEnergy = enemy->energy;
	if (gHitKickMs > 0)
		gHitKickMs -= timediff;

	// Tint. red ramps 0..1 over lost 0.25..1.0 (smoothstep); the throb is a
	// pure function of simulationTime, so it cannot drift between sims.
	red = (lost - 0.25f) / 0.75f;
	if (red < 0) red = 0;
	if (red > 1) red = 1;
	red = red * red * (3.0f - 2.0f * red);
	throb = (lost > 0.75f) ? (0.92f + 0.08f * sinf(simulationTime * 0.006f)) : 1.0f;
	enemy->entity.color[R] = 1.0f;
	enemy->entity.color[G] = (1.0f - 0.62f * red) * throb;
	enemy->entity.color[B] = (1.0f - 0.72f * red) * throb;
	enemy->entity.color[A] = 1.0f;

	// Smoke.
	if (lost >= LOFB_SMOKE_FROM)
	{
		float span = (lost - LOFB_SMOKE_FROM) / (1.0f - LOFB_SMOKE_FROM);	// 0..1
		gBodySmokeMs -= timediff;
		if (gBodySmokeMs <= 0)
		{
			static const float vent[3][2] = { {-0.13f, 0.09f}, {0.10f, 0.13f}, {0.00f, 0.00f} };
			vec2_t p;
			p[X] = enemy->ss_position[X] + vent[gBodySmokeIdx][0];
			p[Y] = enemy->ss_position[Y] + vent[gBodySmokeIdx][1];
			if (lost >= 0.70f && (gBodySmokeIdx & 1))
				FX_GetExplosion(p, IMPACT_TYPE_YELLOW, 0.22f + 0.25f * span, 0);
			FX_GetSmoke(p, 0.16f + 0.16f * span, 0.16f + 0.16f * span);	// (FX_GetSmoke nudges p[Y]; p is ours)
			gBodySmokeIdx = (gBodySmokeIdx + 1) % 3;
			gBodySmokeMs  = 700.0f - 450.0f * span;
		}
	}
}

void updateLOFB(enemy_t* enemy)
{
	float t, lost, aggr;
	int hpPct, frenzy;

	enemy->parameters[P_TIME] += timediff;
	t = enemy->parameters[P_TIME];

	// Damage owed by destroyed arms: carve it off the global energy here, where
	// the enemy pointer lives (LOFB_DamageArm only sees the arm). Floored at 1 HP
	// -- the arms are a side objective; the killing blow stays with direct fire.
	if (enemy->state == LOFB_STATE_FIGHTING && gArmChunkDmg > 0)
	{
		int e = enemy->energy - gArmChunkDmg;
		enemy->energy = (short)(e < 1 ? 1 : e);
		gArmChunkDmg = 0;
	}

	// Publish HP to the HUD. Max HP is captured while ARRIVING (full HP, and the
	// boss is invulnerable then) and frozen for the rest of the fight. Do NOT
	// re-baseline on a time gap: a frame hitch or an app background/resume would
	// otherwise reset the max to the CURRENT (already-reduced) energy, making the
	// bar visibly refill -- which read as an immortal boss.
	if (enemy->state == LOFB_STATE_ARRIVING)
		gBossMaxEnergy = enemy->energy;
	gBossEnergy = enemy->energy;
	gBossHudStamp = simulationTime;
	gBossSS[X] = enemy->ss_position[X];	// v5: the arm solids and the crook are placed from here
	gBossSS[Y] = enemy->ss_position[Y];

	// v5: the pincer's clock, then pose the arm bones and re-skin (arriving
	// too: the claws breathe as it descends). Reads the arm state of the
	// PREVIOUS frame, which is fine.
	LOFB_UpdatePinch(enemy);
	LOFB_PoseArms(enemy);

	if (enemy->state == LOFB_STATE_ARRIVING)
	{
		// Ease from the spawn position down to the hover point.
		float f = t / LOFB_ARRIVE_MS;
		if (f > 1) f = 1;
		f = 1 - (1 - f) * (1 - f);	// ease-out

		enemy->ss_position[X] = enemy->spawn_startPosition[X] + f * (enemy->spawn_endPosition[X] - enemy->spawn_startPosition[X]);
		enemy->ss_position[Y] = enemy->spawn_startPosition[Y] + f * (enemy->spawn_endPosition[Y] - enemy->spawn_startPosition[Y]);

		if (t >= LOFB_ARRIVE_MS)
		{
			enemy->state = LOFB_STATE_FIGHTING;
			enemy->parameters[P_FAN_CD]     = 900;	// first volley shortly after arrival
			enemy->parameters[P_SPIRAL_CD]  = 1500;
			enemy->parameters[P_MINION_CD]  = 4000;
			enemy->parameters[P_BIGSHOT_CD] = 7000;
			// Fresh fight: reset the laser + hover clock + missile launcher + arms.
			gLaserState      = LOFB_LASER_OFF;
			gLaserCooldown   = LOFB_LASER_FIRST_MS;
			gSwayClock       = 0;
			gMissileCooldown = LOFB_MISSILE_CD;
			gSeekerPort  = 1;	// fresh fight: same first launch port in both sims
			gArmShotSide = 1;
			gArmMaxHP = (short)(LOFB_ARM_HP * (engine.mode == DE_MODE_MULTIPLAYER ? 2 : 1));
			gArmHP[0] = gArmHP[1] = gArmMaxHP;
			gArmAlive[0] = gArmAlive[1] = 1;
			gArmSparkTimer = 0;
			gArmFlashMs[0] = gArmFlashMs[1] = 0;
			gArmChunkDmg = 0;
			// Fresh fight: pristine hull, arms whole and at rest.
			gLastEnergy   = enemy->energy;
			gHitKickMs    = 0;
			gBodySmokeMs  = 0;
			gBodySmokeIdx = 0;
			gArmRecoilMs[0] = gArmRecoilMs[1] = 0;
			gArmDeadAt[0] = gArmDeadAt[1] = -1;
			gPinchState = PINCH_OFF; gPinchTimer = 0; gPinchSwing = 0;
			gPinchRequestAt = -1;
			gPinchNextAt = simulationTime + 12000;	// the first random pinch, a while into the fight
			enemy->entity.color[R] = enemy->entity.color[G] = enemy->entity.color[B] = enemy->entity.color[A] = 1.0f;
		}
		return;
	}

	// Attack ladder by HP LOST (user-tuned): the fight opens on the aimed fan
	// alone (the original short phase 1), then the attacks join ONE AT A TIME
	// instead of everything arriving together at 85%:
	//   -15%  the rotating spray ("tirs aleatoires")
	//   -25%  escort waves
	//   -50%  the big energy shots
	//   -75%  the red homing seekers (from the body)
	// The last quarter doubles as the frenzy: wider fan, faster cadences.
	hpPct = 100;
	if (gBossMaxEnergy > 0)
		hpPct = (100 * enemy->energy) / gBossMaxEnergy;
	// stage 2b: the built-in ladder sets the flags from hpPct -- unless the
	// scene's rules drive them (then the flags only move when a rule fires).
	if (!gLadderScripted)
	{
		LOFB_SetAttackFlag(LOFB_ATTACK_SPRAY,    hpPct <= 85);
		LOFB_SetAttackFlag(LOFB_ATTACK_MINIONS,  hpPct <= 75);
		LOFB_SetAttackFlag(LOFB_ATTACK_BIGSHOT,  hpPct <= 50);
		LOFB_SetAttackFlag(LOFB_ATTACK_MISSILES, hpPct <= 25);
		LOFB_SetAttackFlag(LOFB_ATTACK_FRENZY,   hpPct <= 25);
	}
	frenzy = gAttackOn[LOFB_ATTACK_FRENZY];

	// Degradation: tint, smoke, kick -- and the cadences below shorten with the
	// damage (aggr 1.0 pristine .. 0.78 at 0 HP), a continuous slope under the
	// ladder's steps, so the frenzy is a floor the fight slides down to, not a
	// switch that flips.
	lost = 1.0f - hpPct / 100.0f;
	if (lost < 0) lost = 0;
	if (lost > 1) lost = 1;
	aggr = 1.0f - LOFB_AGGR_GAIN * lost;
	LOFB_Degrade(enemy, lost);

	// The mega-laser runs on its own clock, independent of HP phases, so a beam
	// shows up every ~30-45s whatever the boss's health.
	LOFB_UpdateLaser(enemy);

	// Hover: slow horizontal sweep + a light vertical bob. The sway clock pauses
	// while the laser is up, so the boss braces perfectly still to fire and then
	// resumes its sway seamlessly (no jump).
	if (gLaserState == LOFB_LASER_OFF)
		gSwayClock += timediff;
	enemy->ss_position[X] = sinf(gSwayClock * (float)(2 * M_PI) / LOFB_SWAY_PERIOD_MS) * LOFB_SWAY_HALFWIDTH;
	enemy->ss_position[Y] = LOFB_HOVER_Y + 0.05f * sinf(gSwayClock * (float)(2 * M_PI) / LOFB_BOB_PERIOD_MS);
	gBossSS[X] = enemy->ss_position[X];	// v5: this frame's position for the arm solids (collisions run after us)
	gBossSS[Y] = enemy->ss_position[Y];

	// Track the two arm hit-zones with the body; tick down the hit flashes; keep
	// destroyed arms smoking/sparking so the wreckage reads (there is no dedicated
	// broken-arm mesh), and let a half-dead arm trail light smoke -- the visible
	// "it's working, keep shooting" progress tell.
	gArmSS[0][X] = enemy->ss_position[X] - LOFB_ARM_OFFX;  gArmSS[0][Y] = enemy->ss_position[Y] + LOFB_ARM_OFFY;
	gArmSS[1][X] = enemy->ss_position[X] + LOFB_ARM_OFFX;  gArmSS[1][Y] = enemy->ss_position[Y] + LOFB_ARM_OFFY;
	if (gArmFlashMs[0] > 0) gArmFlashMs[0] -= timediff;
	if (gArmFlashMs[1] > 0) gArmFlashMs[1] -= timediff;
	{
		int k, needFx = 0;
		for (k = 0; k < 2; k++)
			if (!gArmAlive[k] || gArmHP[k] <= gArmMaxHP / 2)
				needFx = 1;
		if (needFx)
		{
			gArmSparkTimer -= timediff;
			if (gArmSparkTimer <= 0)
			{
				for (k = 0; k < 2; k++)
				{
					vec2_t p;
					p[X] = gArmSS[k][X] + 0.05f * sinf(gSwayClock * 0.03f + k);
					p[Y] = gArmSS[k][Y];
					if (!gArmAlive[k])
					{
						// THE STUMP (round 76): the arm is gone, the body sparks
						// where it was -- at the shoulder pivot, in ss through the
						// same mapping the solids use. A burst and a smoke puff,
						// the spark jittering deterministically off the pivot.
						if (gPoseBonesValid && widthAtDistance > 0 && heightAtDistance > 0)
						{
							p[X] = enemy->ss_position[X] + gRestPivot[k][0] / widthAtDistance  + 0.02f * sinf(gSwayClock * 0.021f + 2.0f * k);
							p[Y] = enemy->ss_position[Y] - gRestPivot[k][2] / heightAtDistance + 0.02f * cosf(gSwayClock * 0.017f + k);
						}
						FX_GetExplosion(p, IMPACT_TYPE_YELLOW, 0.55f, 0);
						Spawn_EntityParticules(p);	// round 77: sparks, not only flames
						p[X] += 0.03f * cosf(gSwayClock * 0.013f + k);
						FX_GetExplosion(p, IMPACT_TYPE_YELLOW, 0.3f, 0);
						FX_GetSmoke(p, 0.28f, 0.28f);
					}
					else if (gArmHP[k] <= gArmMaxHP / 2)
						FX_GetSmoke(p, 0.18f, 0.18f);
				}
				gArmSparkTimer = 320;
			}
		}
	}

	// While charging or firing the beam, the boss holds its fire: the laser IS
	// the phase. (Escort minions already on-screen keep coming -- and get fried.)
	if (gLaserState == LOFB_LASER_OFF)
	{
		// Attack 1: aimed fan at the nearest player (always on; widens in the frenzy).
		enemy->parameters[P_FAN_CD] -= timediff;
		if (enemy->parameters[P_FAN_CD] <= 0)
		{
			LOFB_FireFan(enemy, frenzy ? 5 : 3, 0.22f);
			enemy->parameters[P_FAN_CD] = (frenzy ? 1100.0f : 1500.0f) * aggr;
		}

		// Attack 2 (-15%): twin rotating spray, SHAB-style.
		if (gAttackOn[LOFB_ATTACK_SPRAY])
		{
			enemy->parameters[P_SPIRAL_CD] -= timediff;
			if (enemy->parameters[P_SPIRAL_CD] <= 0)
			{
				float a = enemy->parameters[P_SPIRAL_ANGLE];
				emitSHABBullet(enemy, a);
				emitSHABBullet(enemy, a + (float)M_PI);
				enemy->parameters[P_SPIRAL_ANGLE] = a + 0.75f;
				enemy->parameters[P_SPIRAL_CD] = (frenzy ? 170.0f : 260.0f) * aggr;
			}
		}

		// Attack 3 (-25%): FHT escort waves -- three per side (Fabien wanted
		// twice as many gêneurs).
		if (gAttackOn[LOFB_ATTACK_MINIONS])
		{
			enemy->parameters[P_MINION_CD] -= timediff;
			if (enemy->parameters[P_MINION_CD] <= 0)
			{
				LOFB_SpawnMinion(-1.0f, 0.90f);
				LOFB_SpawnMinion( 1.0f, 0.90f);
				LOFB_SpawnMinion(-1.0f, 0.60f);
				LOFB_SpawnMinion( 1.0f, 0.60f);
				LOFB_SpawnMinion(-1.0f, 0.30f);
				LOFB_SpawnMinion( 1.0f, 0.30f);
				enemy->parameters[P_MINION_CD] = frenzy ? 5000 : 6500;
			}
		}

		// Attack 4 (-50%): THE BIG ENERGY SHOT -- fired from the ARMS, alternating
		// sides. Destroying an arm silences that side; destroying both cancels the
		// attack entirely -- that is the strategic reward for focusing the arms.
		if (gAttackOn[LOFB_ATTACK_BIGSHOT] && (gArmAlive[0] || gArmAlive[1]))
		{
			enemy->parameters[P_BIGSHOT_CD] -= timediff;
			if (enemy->parameters[P_BIGSHOT_CD] <= 0)
			{
				// Alternate arms; if the preferred one is gone, the survivor fires.
				int want = (gArmShotSide < 0) ? 0 : 1;
				if (!gArmAlive[want])
					want = 1 - want;
				LOFB_FireBigShot(enemy, (want == 0) ? -LOFB_ARM_OFFX : LOFB_ARM_OFFX);
				gArmShotSide = -gArmShotSide;
				enemy->parameters[P_BIGSHOT_CD] = (frenzy ? 6000.0f : 8500.0f) * aggr;
			}
		}

		// Attack 5 (-75%): the BODY launches the destructible homing seekers, one
		// per volley from alternating launch ports -- the finale always shows up,
		// arms or no arms.
		if (gAttackOn[LOFB_ATTACK_MISSILES])
		{
			gMissileCooldown -= timediff;
			if (gMissileCooldown <= 0)
			{
				LOFB_FireMissile(enemy, gSeekerPort * 0.5f);
				gSeekerPort = -gSeekerPort;
				gMissileCooldown = LOFB_MISSILE_CD * aggr;
			}
		}
	}

	// Draw the laser (charge sparks / live beam) + arm hit flashes into the enemy
	// FX buffer.
	LOFB_EmitLaserFX(enemy);
	LOFB_EmitArmFX();
}

void LOFB_OnBossDeath(enemy_t* enemy)
{
	// Deterministic pyro burst around the boss's last position.
	static const float off[6][2] = {
		{ 0.00f,  0.00f}, {-0.12f,  0.06f}, { 0.13f,  0.10f},
		{-0.07f, -0.09f}, { 0.09f, -0.06f}, { 0.00f,  0.14f},
	};
	vec2_t p;
	int i;

	for (i = 0; i < 6; i++)
	{
		p[X] = enemy->ss_position[X] + off[i][0];
		p[Y] = enemy->ss_position[Y] + off[i][1];
		FX_GetExplosion(p, IMPACT_TYPE_YELLOW, 1.6f - i * 0.15f, 0);
	}
	FX_GetSmoke(enemy->ss_position, 0.5f, 0.5f);
	SND_PlaySound(SND_EXPLOSION);

	// Victory bonus (each player; both sims add it at the same tick in MP), then
	// FREEZE the score: the win is scored at the killing blow -- leftover bullets
	// mopping up escorts during the victory lap don't count anymore.
	for (i = 0; i < numPlayers; i++)
		players[i].score += (LOFB_VICTORY_BONUS << engine.difficultyLevel);
	Native_UploadScore(P_GetDisplayScore());	// v2 P3: team score in MP
	gScoreLocked = 1;

	// End-of-act choreography, same as the other acts: the ships fly to their
	// rest position and the epilog title shows; when it ends, TITLE_Update calls
	// dEngine_GoToNextScene (which, act 4 being the last act, returns to the menu).
	// Act 4 being the END OF THE GAME, its epilog is the ending card (drawn by
	// TITLE_RenderEndOfGame) and holds long enough to read the run's numbers.
	EV_AutoPilotPls(0);
	TITLE_Show_epilog(16000);

	gBossHudStamp = -100000;	// hide the health bar right away
	gLaserState   = LOFB_LASER_OFF;	// kill any beam in flight
}

int LOFB_GetBossHealth(int* energy, int* maxEnergy)
{
	if (gBossMaxEnergy <= 0)
		return 0;
	if (simulationTime - gBossHudStamp > 300 || gBossHudStamp > simulationTime)
		return 0;

	*energy    = (gBossEnergy < 0) ? 0 : gBossEnergy;
	*maxEnergy = gBossMaxEnergy;
	return 1;
}

// ---------------------------------------------------------------------------
// v4 stage 2b: the ladder's public face (rules.c).

int LOFB_AttackIdByName(const char* name)
{
	int i;
	if (!name)
		return -1;
	for (i = 0; i < LOFB_NUM_ATTACKS; i++)
		if (!strcmp(gAttackNames[i], name))
			return i;
	return -1;
}

void LOFB_UseScriptedLadder(void)
{
	gLadderScripted = 1;
}

void LOFB_SetAttack(int which, int on)
{
	gLadderScripted = 1;
	LOFB_SetAttackFlag(which, on);
}

void LOFB_ResetLadder(void)
{
	gLadderScripted = 0;
	memset(gAttackOn, 0, sizeof(gAttackOn));
	gLastEnergy = 0; gHitKickMs = 0; gBodySmokeMs = 0; gBodySmokeIdx = 0;
	// v5: the mesh is reloaded with the scene -- take the rest bones again.
	gPoseBonesValid = 0;
	gArmRecoilMs[0] = gArmRecoilMs[1] = 0;
	gArmDeadAt[0] = gArmDeadAt[1] = -1;
	gPinchState = PINCH_OFF; gPinchTimer = 0; gPinchSwing = 0; gPinchRequestAt = -1; gPinchNextAt = 0;
}

// Render-only shake, in ss units, added by enemy.c to the boss entity's
// translation AFTER the hitbox and arm zones were placed from ss_position.
// Pure function of (HP, gHitKickMs, simulationTime): a tremor that sets in from
// half HP and grows to LOFB_SHAKE_BASE at 0, plus a kick decaying over
// LOFB_KICK_MS after a body hit. Two incommensurate sines per axis so it
// never reads as a wobble on a fixed path.
void LOFB_GetShakeOffset(const enemy_t* enemy, float* dx, float* dy)
{
	float lost, amp, t;

	*dx = *dy = 0;
	if (!enemy || enemy->type != ENEMY_LOFB || enemy->state != LOFB_STATE_FIGHTING || gBossMaxEnergy <= 0)
		return;

	lost = 1.0f - (float)enemy->energy / (float)gBossMaxEnergy;
	if (lost < 0) lost = 0;
	if (lost > 1) lost = 1;

	amp = 0;
	if (lost > 0.5f)
	{
		float f = (lost - 0.5f) / 0.5f;
		amp = LOFB_SHAKE_BASE * f * f;	// eases in, so half HP is only a shiver
	}
	if (gHitKickMs > 0)
		amp += LOFB_SHAKE_KICK * (gHitKickMs / LOFB_KICK_MS);
	if (amp <= 0)
		return;

	t = (float)simulationTime;
	*dx = amp * sinf(t * 0.071f) * cosf(t * 0.023f);
	*dy = amp * sinf(t * 0.053f + 1.3f) * cosf(t * 0.017f);
}

// The energy the boss will show once its own update has carved off the damage
// owed by a destroyed arm (updateLOFB, top). A rule evaluated between the
// collisions and that update reads this, so a threshold crosses on the same
// frame in both ladders.
int LOFB_EffectiveEnergy(const enemy_t* enemy)
{
	int e = enemy->energy;
	if (enemy->type == ENEMY_LOFB && enemy->state == LOFB_STATE_FIGHTING && gArmChunkDmg > 0)
	{
		e -= gArmChunkDmg;
		if (e < 1) e = 1;
	}
	return e;
}
