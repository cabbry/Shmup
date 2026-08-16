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
 *  camera.c
 *  dEngine
 *
 *  Created by fabien sanglard on 15/08/09.
 *  Copyright 2009 Memset software Inc. All rights reserved.
 *
 */

#include <stdlib.h>	// getenv (offline visibility bake switch)
#include "camera.h"
#include "renderer.h"
#include "timer.h"
#include "filesystem.h"
#include "lexer.h"
#include "world.h"
#include "collisions.h"
#include "preproc.h"
#include "vis.h"



camera_t camera;

matrix3x3_t orientationMatrix;

int cameraVisMemSize;

// Infinite decor scroll for the boss act. The baked camera path (the boss act
// borrows act 2's rails) is finite; when it ran out the camera FROZE, so the city
// stopped dead in the middle of the boss fight. Instead we keep drifting forward
// at the path's final velocity so the decor keeps flowing past. Baked visibility
// is frozen at the last frame, so far-ahead tiles fade into the "Water" fog
// rather than emerging fresh -- but the sense of motion (what Fabien asked for)
// is preserved. Requested by the scene itself (gCameraDriftAtEnd, "driftAtEnd: 1")
// so the acts that end on their own script are untouched.
int           gCameraDriftAtEnd = 0;
int           gRuntimeCullMap = 0;	// see camera.h
static vec3_t gCamPrevPos;
static vec3_t gCamDriftVel;			// world units per ms, from the last segment
static int    gCamHavePrev = 0;
// END OF THE RAIL (boss act). The whole flight runs on its shipped BAKED rail
// and baked visibility -- untouched, opening included. Only here, where the rail
// runs out, does the bake stop being usable: it is frozen on its last frame, so
// the decor beyond simply is not in it. Measured on device: the readout drops to
// ONE drawn entity (the sky dome) -- the void the earlier rounds kept fighting.
//
// So this is the one place that switches to LIVE culling (gRuntimeCullMap), and
// with the camera then free of the bake it flies a real patrol: settle, a TRUE
// 180 turn, a long leg back over the city already flown, another turn, and so on.
// Earlier attempts (a pull-back that hovered, then an oscillation) both failed
// because they had to stay inside a frozen visible set; that constraint is gone.
static vec3_t gCamPathStart;			// where the rail BEGAN (bounds the patrol)
static vec3_t gCamEndAnchor;			// where the rail ended
static vec3_t gCamEndAxis;				// the rail's travel direction there (XZ, unit)
static float  gCamEndLeg = 0;			// length of one leg, derived from the flight
static vec3_t gCamEndFrozen[3];			// right/up/forward at the handover, for the blend
static int    gCamEndActive = 0;
static float  gCamEndPhase  = 0;
static float  gCamEndTheta  = 0;		// heading: screen-up direction in the XZ plane
static float  gCamEndTurnFrom = 0, gCamEndTurnTo = 0;
static float  gCamEndSpeed  = 0;		// units per ms, taken from the rail
static int    gCamEndState  = 0;		// 0 settle, 1 turn, 2 cruise
static int    gCamEndLegDir = 1;		// +1 heading away from the city start, -1 back
#define CAM_END_SETTLE_MS	2000.0f		// ease out of the rail's frozen outro pose
#define CAM_END_TURN_MS		4500.0f		// one 180 turn
#define CAM_END_TURN_SPEED	0.35f		// keep arcing through the turn, don't stop dead
#define CAM_END_LEG			30000.0f	// cap on one leg (see gCamEndLeg)
#define CAM_END_LEG_MIN		1200.0f		// ...and a floor, for very short rails
#define CAM_END_FWD_MARGIN	400.0f		// never fly much past the anchor
#define CAM_END_SPEED_FALLBACK	0.24f	// units/ms, if the rail's own speed is unusable
static quat4_t gCamLastQuat;			// last baked orientation (frozen at end)
static quat4_t gCamCalmQuat;			// slow-trailing orientation (lags turns)
static int     gCamHaveCalm = 0;

// The camera basis for a top-down view whose SCREEN-UP points at angle theta in
// the XZ plane (theta 0 = screen-up toward -Z, the rail's own convention). Also
// the direction the camera travels: on screen it always flies "up the screen".
static void CAM_EndBasis(float theta, vec3_t right, vec3_t up, vec3_t forward)
{
	float s = sinf(theta), c = cosf(theta);

	right[0]   =  c;	right[1]   = 0;		right[2]   =  s;
	up[0]      =  s;	up[1]      = 0;		up[2]      = -c;
	forward[0] =  0;	forward[1] = -1;	forward[2] =  0;
}

void CAM_InterpolateFrames(camera_frame_t* currentFrame, camera_frame_t* nextFrame, float interpolationFactor, vec3_t position, quat4_t orientation)
{
	
	//Interpolate position
	vectorLinearInterpolate(currentFrame->position,nextFrame->position,interpolationFactor,position);
	
	
	
	//Interpolate quaternion
	Quat_slerp(currentFrame->orientation, nextFrame->orientation, interpolationFactor, orientation);
	
	
}

void CAM_FreeCameraFrame(camera_frame_t* toDelete)
{
	int i;
	
	if (toDelete->visUpdate.isKey)
	{
		for (i=0; i<  toDelete->visUpdate.numVisSets; i++) 
			free(toDelete->visUpdate.visSets[i].indices);
	}
	else 
	{
		for (i=0; i<  toDelete->visUpdate.numVisSets; i++) 
		{
			free(toDelete->visUpdate.visSets[i].facesToAdd);
			free(toDelete->visUpdate.visSets[i].facesToRemove);
		}
	}

	free(toDelete->visUpdate.visSets);
	free(toDelete);
}

void CAM_ClearAllRemainingCameraVS(void)
{
	camera_frame_t* toDelete;
	
	if (camera.currentFrame == NULL)
		return;
	
	while (camera.currentFrame->next != NULL && camera.currentFrame->next->time <= simulationTime)
	{
		toDelete =  camera.currentFrame;
		camera.currentFrame = camera.currentFrame->next;
		CAM_FreeCameraFrame(toDelete);
	}
}

// ---------------------------------------------------------------------------
// TTB -- the Tokyo Toy Box beat: the camera swings from over the city onto the
// SIDE of the flight corridor, at gameplay height, and looks at it with the
// horizon flat. Ground at the bottom of the screen, sky at the top, the city
// streaming horizontally -- the vertical shooter reads as a side-scroller.
// Then it swings back up.
//
// v1.5.6 shipped this as a pure ROLL around the view axis, on the theory that
// keeping the rail's pose kept the bake aligned. On device it looked like
// turning the phone: the decor rotated on screen (ship untouched -- it is
// billboarded through the view matrix) but the view was still the top-down
// one. The tester's verdict was immediate. A side VIEW needs the camera to
// MOVE: an orbit of the corridor, which is what the 2026-07 prototype did
// ("one-shot horizontal orbit", commands.c still says so).
//
// The orbit, per frame, layered on whatever pose the rail just produced:
//   T = the rail's travel direction (horizontal, low-passed, frozen per beat)
//   S = T x worldUp, the side of the corridor (sign of the angle picks which)
//   position += S * SIDE_DIST * sin(theta) - worldUp * DROP * (1 - cos(theta))
//   basis    -> blends from the rail's toward { forward=-S, up=worldUp }
// At theta=90: camera SIDE_DIST out, DROP down, looking at the corridor with
// world-up as screen-up and screen-right = T, so the decor scrolls LEFT (the
// classic side-scroller reading; "ttbRoll angle -90" flips side + direction).
// Gameplay is untouched: ships/bullets live in screen space, which follows the
// camera basis, and the billboards stay upright on screen (cameraInvRot).
//
// The position leaves the rail, so the baked visibility is invalid for the
// whole beat: live culling switches on while theta != 0 and hands back to the
// bake once upright -- scope the freedom to where the constraint binds, the
// U-turn's lesson. Never fights the end-of-rail patrol, which owns the flag.
// The pose at full deployment, tuned against act 1's REAL city geometry (the
// cityBlue tile spans x +/-271 with towers up to y=319 -- TALLER than the rail
// at 162: the act flies BETWEEN towers). A side camera at gameplay height would
// sit inside the skyline, so the side view stands off past the city's edge and
// slightly above the near band's rooftops, with a gentle downward pitch to keep
// the corridor centered. Tuned visually in the ttb_harness (renders the real
// tile through the real CAM_ApplyTTB -- see reborn.md round 20).
// Pose "B", picked by the user against two alternatives rendered through the
// real geometry (650/y300/14deg and 900/y260/8deg): the big towers frame the
// screen, the canyon reads in depth, the ship stands clear over the rooftops.
#define CAM_TTB_SIDE_DIST	420.0f	// side offset at full deployment (world units)
#define CAM_TTB_DROP		(-218.0f)	// negative = RISE (act1 rail y=162 -> y=380)
#define CAM_TTB_PITCH		(28.0f * (float)M_PI / 180.0f)	// look-down tilt at full deployment
#define CAM_TTB_MAX_RAD		((float)M_PI / 2.0f)	// the event clamps to +/-90

static vec3_t	gTTBAxis     = {0, 0, -1};	// smoothed travel direction T
static int		gTTBAxisGood = 0;			// T has been fed at least once
static int		gTTBFrozen   = 0;			// T frozen for the length of a beat
static vec3_t	gTTBPrevRail;				// rail position last frame (pre-offset)
static int		gTTBHavePrev = 0;

// (Name kept from the roll version -- it is the scene-file API: "ttbRoll".)
void CAM_SetTTBRoll(float angleDegrees, int durationMs)
{
	float target = angleDegrees * (float)M_PI / 180.0f;

	if (target >  CAM_TTB_MAX_RAD) target =  CAM_TTB_MAX_RAD;
	if (target < -CAM_TTB_MAX_RAD) target = -CAM_TTB_MAX_RAD;

	camera.ttbFrom     = camera.ttbAngle;
	camera.ttbTarget   = target;
	camera.ttbPhase    = 0;
	camera.ttbDuration = (durationMs > 0) ? durationMs : 0;

	// Heading off the rail: freeze the travel axis so a wobbly rail segment
	// cannot swing the whole side view around mid-beat. Released when upright.
	if (target != 0.0f)
		gTTBFrozen = 1;

	if (camera.ttbDuration == 0)
		camera.ttbAngle = camera.ttbTarget;
}

// Advance the transition and swing the camera off the pose the rail (or the
// end-of-rail patrol) just produced. camera.position/right/up/forward hold the
// RAIL pose on entry and the TTB pose on exit.
static void CAM_ApplyTTB(void)
{
	float	f, sgn, absAngle, sideDist, drop;
	int		k;
	vec3_t	side, fwd, upv, rightv;
	float	dx, dz, planar;

	// --- Travel axis: estimate from the rail's own motion (pre-offset), low-
	// passed, and frozen while a beat is on. The estimate must be fed BEFORE the
	// offset is applied, or it would measure our own swing.
	if (gTTBHavePrev && !gTTBFrozen)
	{
		dx = camera.position[0] - gTTBPrevRail[0];
		dz = camera.position[2] - gTTBPrevRail[2];
		planar = sqrtf(dx*dx + dz*dz);
		if (planar > 1e-4f)
		{
			dx /= planar; dz /= planar;
			if (!gTTBAxisGood)
			{
				gTTBAxis[0] = dx; gTTBAxis[1] = 0; gTTBAxis[2] = dz;
				gTTBAxisGood = 1;
			}
			else
			{
				// ~5% per tick: steady within a second, deaf to frame noise.
				gTTBAxis[0] += (dx - gTTBAxis[0]) * 0.05f;
				gTTBAxis[2] += (dz - gTTBAxis[2]) * 0.05f;
				planar = sqrtf(gTTBAxis[0]*gTTBAxis[0] + gTTBAxis[2]*gTTBAxis[2]);
				if (planar > 1e-6f) { gTTBAxis[0] /= planar; gTTBAxis[2] /= planar; }
			}
		}
	}
	vectorCopy(camera.position, gTTBPrevRail);
	gTTBHavePrev = 1;

	// --- Advance the scripted transition (smoothstepped).
	if (camera.ttbAngle != camera.ttbTarget)
	{
		camera.ttbPhase += timediff;

		if (camera.ttbDuration <= 0 || camera.ttbPhase >= camera.ttbDuration)
		{
			camera.ttbAngle = camera.ttbTarget;
		}
		else
		{
			f = camera.ttbPhase / (float)camera.ttbDuration;
			f = f * f * (3.0f - 2.0f * f);
			camera.ttbAngle = camera.ttbFrom + (camera.ttbTarget - camera.ttbFrom) * f;
		}
	}

	// Fully upright and no beat pending: release the frozen axis, hand the
	// culling back to the bake (unless the end-of-rail patrol owns it).
	if (camera.ttbAngle == 0.0f && camera.ttbTarget == 0.0f)
		gTTBFrozen = 0;

	if (!gCamEndActive)
		gRuntimeCullMap = (camera.ttbAngle > 0.001f || camera.ttbAngle < -0.001f);

	if (camera.ttbAngle == 0.0f)
		return;

	sgn      = (camera.ttbAngle >= 0) ? 1.0f : -1.0f;
	absAngle = camera.ttbAngle * sgn;

	// S = T x worldUp (horizontal, unit since T is), flipped by the angle's sign.
	side[0] = -gTTBAxis[2] * sgn;
	side[1] = 0;
	side[2] =  gTTBAxis[0] * sgn;

	// --- Position: out to the side, down toward the action, on an elliptic arc
	// that leaves the rail tangentially (sin/1-cos).
	sideDist = CAM_TTB_SIDE_DIST * sinf(absAngle);
	drop     = CAM_TTB_DROP * (1.0f - cosf(absAngle));
	for (k = 0; k < 3; k++)
		camera.position[k] += side[k] * sideDist;
	camera.position[1] -= drop;

	// --- Orientation: blend the rail's basis toward the side-view basis
	// { forward = -S pitched down by CAM_TTB_PITCH, up = worldUp pitched along },
	// then re-orthonormalize. The two are ~90 degrees apart at most, so
	// lerp+normalize cannot degenerate.
	f = absAngle / CAM_TTB_MAX_RAD;
	if (f > 1.0f) f = 1.0f;

	{
		float cp = cosf(CAM_TTB_PITCH), sp = sinf(CAM_TTB_PITCH);
		for (k = 0; k < 3; k++)
		{
			float w      = (k == 1) ? 1.0f : 0.0f;			// world up
			float fwdTgt = -side[k] * cp - w * sp;			// at the corridor, tilted down
			float upTgt  =  w * cp       - side[k] * sp;	// stays perpendicular
			fwd[k] = camera.forward[k] + (fwdTgt - camera.forward[k]) * f;
			upv[k] = camera.up[k]      + (upTgt  - camera.up[k])      * f;
		}
	}
	normalize(fwd);
	// Gram-Schmidt the up against the new forward.
	planar = upv[0]*fwd[0] + upv[1]*fwd[1] + upv[2]*fwd[2];
	for (k = 0; k < 3; k++)
		upv[k] -= fwd[k] * planar;
	normalize(upv);
	// Engine convention (see CAM_EndBasis): right = forward x up.
	rightv[0] = fwd[1]*upv[2] - fwd[2]*upv[1];
	rightv[1] = fwd[2]*upv[0] - fwd[0]*upv[2];
	rightv[2] = fwd[0]*upv[1] - fwd[1]*upv[0];

	vectorCopy(fwd,    camera.forward);
	vectorCopy(upv,    camera.up);
	vectorCopy(rightv, camera.right);
}

void CAM_Update(void)
{
	float			interpolationFactor;
	quat4_t			interpolatedQuaterion;
	matrix3x3_t		interpolatedOrientationMatrix;
	camera_frame_t* nextFrame = 0;
	camera_frame_t* toDelete = 0;
	
	if (!camera.playing)
		return;
	
	
	//Log_Printf("CAM_Update\n");
	//Log_Printf("camera.currentFrame->next=%d\n",camera.currentFrame->next);
	//Log_Printf("camera.currentFrame->next->time=%d\n",camera.currentFrame->next->time);
	//Log_Printf("simulationTime=%d\n",simulationTime);

	while (camera.currentFrame->next != NULL && camera.currentFrame->next->time <= simulationTime)
	{
		//Update vis_set if not already done, take into account key frame_update
		//Log_Printf("Jumping into vis_update().\n");
		VIS_Update();
		
		toDelete = camera.currentFrame;
		camera.currentFrame = camera.currentFrame->next;
		CAM_FreeCameraFrame(toDelete);
	}	
		
	//Log_Printf("frame t=%d.\n",camera.currentFrame->time);
	
	if (camera.currentFrame->next == 0)
	{
		// Path exhausted: hand over to the free patrol (see the block comment on
		// gCamEndAnchor). The baked visibility is unusable from here, so this is
		// where live decor culling takes over -- and with it, a real 180 turn.
		if (gCameraDriftAtEnd && gCamHavePrev)
		{
			vec3_t	right, up, forward;
			float	step, along;

			if (!gCamEndActive)
			{
				float vx = gCamDriftVel[0], vz = gCamDriftVel[2];
				float planar = sqrtf(vx*vx + vz*vz);
				float cx, cz, corridor;

				vectorCopy(camera.position, gCamEndAnchor);
				vectorCopy(camera.right,   gCamEndFrozen[0]);
				vectorCopy(camera.up,      gCamEndFrozen[1]);
				vectorCopy(camera.forward, gCamEndFrozen[2]);

				// The patrol axis is the CORRIDOR the act actually flew (start to
				// end), NOT the last segment's velocity. Act 2's rail finishes on
				// an outro flourish that veers ~3 degrees sideways; steering the
				// return by that tilt drifted the camera >1500 units off the city
				// over a 30000-unit leg -- the far side of the screen went black,
				// then all of it. Measured over the whole flight the same rail is
				// straight to within a fraction of a degree.
				cx = gCamEndAnchor[0] - gCamPathStart[0];
				cz = gCamEndAnchor[2] - gCamPathStart[2];
				corridor = sqrtf(cx*cx + cz*cz);

				if (corridor > 1000.0f)
				{
					gCamEndAxis[0] = cx / corridor;
					gCamEndAxis[1] = 0;
					gCamEndAxis[2] = cz / corridor;
				}
				else if (planar > 1e-8f)		// no usable corridor: last heading
				{
					gCamEndAxis[0] = vx / planar;
					gCamEndAxis[1] = 0;
					gCamEndAxis[2] = vz / planar;
				}
				else
				{
					gCamEndAxis[0] = 0; gCamEndAxis[1] = 0; gCamEndAxis[2] = -1;
				}
				// Heading whose screen-up matches the way the rail was going.
				gCamEndTheta = atan2f(gCamEndAxis[0], -gCamEndAxis[2]);

				gCamEndSpeed = (planar > 1e-8f) ? planar : CAM_END_SPEED_FALLBACK;
				if (gCamEndSpeed < 1e-6f)
					gCamEndSpeed = CAM_END_SPEED_FALLBACK;

				// One leg spans the stretch actually FLOWN, not a fixed distance:
				// the decor only exists where the rail went, so measuring from the
				// rail's own start keeps the patrol inside the city whatever rail
				// this act uses (a fixed 30000 would sail out of a short one).
				gCamEndLeg = (gCamEndAnchor[0] - gCamPathStart[0]) * gCamEndAxis[0]
						   + (gCamEndAnchor[2] - gCamPathStart[2]) * gCamEndAxis[2];
				if (gCamEndLeg < 0)
					gCamEndLeg = -gCamEndLeg;
				gCamEndLeg -= 500.0f;					// stop short of the very start
				if (gCamEndLeg > CAM_END_LEG)  gCamEndLeg = CAM_END_LEG;
				if (gCamEndLeg < CAM_END_LEG_MIN) gCamEndLeg = CAM_END_LEG_MIN;

				gCamEndPhase  = 0;
				gCamEndState  = 0;		// settle first: leave the outro pose gently
				gCamEndLegDir = 1;
				gCamEndActive = 1;

				// The bake ends here; from now on the decor is culled live, which
				// is what lets the camera turn around at all.
				gRuntimeCullMap = 1;
			}

			gCamEndPhase += timediff;

			switch (gCamEndState)
			{
			case 0:		// SETTLE -- keep cruising, ease out of the frozen outro pose
				{
					float f = gCamEndPhase / CAM_END_SETTLE_MS;
					int   k;
					if (f > 1) f = 1;
					CAM_EndBasis(gCamEndTheta, right, up, forward);
					for (k = 0; k < 3; k++)
					{
						right[k]   = gCamEndFrozen[0][k] + (right[k]   - gCamEndFrozen[0][k]) * f;
						up[k]      = gCamEndFrozen[1][k] + (up[k]      - gCamEndFrozen[1][k]) * f;
						forward[k] = gCamEndFrozen[2][k] + (forward[k] - gCamEndFrozen[2][k]) * f;
					}
					normalize(right); normalize(up); normalize(forward);
					step = gCamEndSpeed * timediff;
					if (gCamEndPhase >= CAM_END_SETTLE_MS)
					{
						gCamEndState    = 1;
						gCamEndPhase    = 0;
						gCamEndTurnFrom = gCamEndTheta;
						gCamEndTurnTo   = gCamEndTheta + (float)M_PI;
					}
				}
				break;

			case 1:		// TURN -- a true 180, arcing (not a dead stop)
				{
					float f = gCamEndPhase / CAM_END_TURN_MS;
					float e;
					if (f > 1) f = 1;
					e = 0.5f * (1.0f - cosf((float)M_PI * f));	// ease in and out
					gCamEndTheta = gCamEndTurnFrom + (gCamEndTurnTo - gCamEndTurnFrom) * e;
					CAM_EndBasis(gCamEndTheta, right, up, forward);
					step = gCamEndSpeed * CAM_END_TURN_SPEED * timediff;
					if (gCamEndPhase >= CAM_END_TURN_MS)
					{
						gCamEndState  = 2;
						gCamEndPhase  = 0;
						gCamEndLegDir = -gCamEndLegDir;
					}
				}
				break;

			default:	// CRUISE -- a long leg over the city, then turn again
				CAM_EndBasis(gCamEndTheta, right, up, forward);
				step = gCamEndSpeed * timediff;
				break;
			}

			// Fly up the screen, whichever way the camera is now facing.
			camera.position[0] += up[0] * step;
			camera.position[1] += up[1] * step;
			camera.position[2] += up[2] * step;

			vectorCopy(right,   camera.right);
			vectorCopy(up,      camera.up);
			vectorCopy(forward, camera.forward);

			// Turn around at the ends of the patrol: never much past the anchor
			// (the city stops shortly after it), and never further back than one
			// leg over the stretch already flown.
			if (gCamEndState == 2)
			{
				along = (camera.position[0] - gCamEndAnchor[0]) * gCamEndAxis[0]
					  + (camera.position[2] - gCamEndAnchor[2]) * gCamEndAxis[2];

				if ((gCamEndLegDir > 0 && along >= CAM_END_FWD_MARGIN) ||
					(gCamEndLegDir < 0 && along <= -gCamEndLeg))
				{
					gCamEndState    = 1;
					gCamEndPhase    = 0;
					gCamEndTurnFrom = gCamEndTheta;
					gCamEndTurnTo   = gCamEndTheta + (float)M_PI;
				}
			}
		}

		// The patrol builds its own basis; the TTB beat still applies on top of
		// it, so an act can roll over past the end of its rail too.
		CAM_ApplyTTB();
		return;
	}

	nextFrame = camera.currentFrame->next;


	interpolationFactor = (simulationTime - camera.currentFrame->time) * 1.0 / (nextFrame->time - camera.currentFrame->time);
	//printf("interpo = %.2f\n",interpolationFactor);

	CAM_InterpolateFrames(camera.currentFrame,nextFrame,interpolationFactor, camera.position, interpolatedQuaterion);

	// Remember the per-ms velocity of this (last valid) segment so we can keep
	// drifting once the path ends.
	if (gCamHavePrev && timediff > 0)
	{
		gCamDriftVel[0] = (camera.position[0] - gCamPrevPos[0]) / timediff;
		gCamDriftVel[1] = (camera.position[1] - gCamPrevPos[1]) / timediff;
		gCamDriftVel[2] = (camera.position[2] - gCamPrevPos[2]) / timediff;
	}
	vectorCopy(camera.position, gCamPrevPos);
	gCamHavePrev = 1;

	// Trail a slow, calmed copy of the baked orientation (time constant ~2s):
	// when the path ends mid-turn this still points down the corridor, and the
	// end-of-path patrol eases back to it. Also snapshot the exact current
	// orientation each frame -- at the end it becomes the blend's start point.
	if (!gCamHaveCalm)
	{
		memcpy(gCamCalmQuat, interpolatedQuaterion, sizeof(quat4_t));
		gCamHaveCalm = 1;
	}
	else
	{
		float a = timediff / 2000.0f;
		if (a > 1) a = 1;
		Quat_slerp(gCamCalmQuat, interpolatedQuaterion, a, gCamCalmQuat);
	}
	memcpy(gCamLastQuat, interpolatedQuaterion, sizeof(quat4_t));
		
	//Transforme quat to matrix and set it as orientation
	Quat_ConvertToMat3x3(interpolatedOrientationMatrix, interpolatedQuaterion);
		
	
	//Log_Printf("Camera pos: [%.2f,%.2f,%.2f].\n",camera.position[0],camera.position[1],camera.position[2]);
	//Log_Printf("Camera orientation matrix:\n");
	//matrix_print3x3(interpolatedOrientationMatrix);
	//Log_Printf("Camera orientation quaternion: [%.5f,%.5f,%.5f,%.5f]\n",interpolatedQuaterion[0],interpolatedQuaterion[1],interpolatedQuaterion[2],interpolatedQuaterion[3]);

	
	camera.right[0] = interpolatedOrientationMatrix[0];
	camera.right[1] = interpolatedOrientationMatrix[1];
	camera.right[2] = interpolatedOrientationMatrix[2];
	
	camera.up[0] = interpolatedOrientationMatrix[3];
	camera.up[1] = interpolatedOrientationMatrix[4];
	camera.up[2] = interpolatedOrientationMatrix[5];
	
	
	camera.forward[0] = -interpolatedOrientationMatrix[6];
	camera.forward[1] = -interpolatedOrientationMatrix[7];
	camera.forward[2] = -interpolatedOrientationMatrix[8];

	// TTB: tip the horizon over on top of the pose the rail just produced.
	CAM_ApplyTTB();
}



void CAM_StartPlaying()
{
	camera.playing = 1;
	gCamHavePrev = 0;	// don't carry a stale drift velocity across scenes
	gCamEndActive = 0;	// fresh scene: re-arm the end-of-path patrol
	gRuntimeCullMap = 0;	// ...and go back to the baked visibility until it runs out
	gCamHaveCalm = 0;	// and re-seed the trailing orientation

	// TTB: every act starts upright, whatever the previous one ended on.
	camera.ttbAngle = camera.ttbFrom = camera.ttbTarget = 0.0f;
	camera.ttbPhase = camera.ttbDuration = 0;
}




camera_frame_t* CAM_ReadFrameCP2Binary(filehandle_t* fileHandle)
{
	camera_frame_t* frame;
	world_vis_set_update_t* worldVisSet;
	entity_visset_t* entityVisSet;
	int j;
	
	frame = calloc(1, sizeof(camera_frame_t));
	
	cameraVisMemSize += sizeof(camera_frame_t);
	
	FS_Read(&frame->time, sizeof(frame->time), 1, fileHandle);
	
	//Log_Printf("time = %d:",frame->time);
	
	FS_Read(&frame->position[X], sizeof(float), 1, fileHandle);
	FS_Read(&frame->position[Y], sizeof(float), 1, fileHandle);
	FS_Read(&frame->position[Z], sizeof(float), 1, fileHandle);
	
	FS_Read(&frame->orientation[X], sizeof(float), 1, fileHandle);
	FS_Read(&frame->orientation[Y], sizeof(float), 1, fileHandle);
	FS_Read(&frame->orientation[Z], sizeof(float), 1, fileHandle);
	FS_Read(&frame->orientation[W], sizeof(float), 1, fileHandle);
	
	worldVisSet = &frame->visUpdate ;
	
	FS_Read(&worldVisSet->isKey, sizeof(uchar), 1, fileHandle);
	
	FS_Read(&worldVisSet->numVisSets, sizeof(ushort), 1, fileHandle);
	
	worldVisSet->visSets = calloc(worldVisSet->numVisSets, sizeof(entity_visset_t));
	cameraVisMemSize += worldVisSet->numVisSets * sizeof(entity_visset_t) ;
	
//	Log_Printf("	Reading visSet: isKey=%2d.\n",worldVisSet->isKey);
	
	for(j=0 ; j < worldVisSet->numVisSets ; j++)
	{
		entityVisSet = &worldVisSet->visSets[j];
		
		FS_Read(&entityVisSet->entityId, sizeof(ushort), 1, fileHandle);
		
	
		
		if (worldVisSet->isKey)
		{
		
			FS_Read(&entityVisSet->numIndices, sizeof(ushort), 1, fileHandle);
	
			entityVisSet->indices = calloc(entityVisSet->numIndices, sizeof(ushort));
			cameraVisMemSize += entityVisSet->numIndices * sizeof(ushort);
		
			FS_Read(entityVisSet->indices, sizeof(ushort), entityVisSet->numIndices, fileHandle);
	
			
		}
		else 
		{
			
			
			
			FS_Read(&entityVisSet->numFacesToAdd, sizeof(ushort), 1, fileHandle);
			entityVisSet->facesToAdd = calloc(entityVisSet->numFacesToAdd, sizeof(ushort));
			cameraVisMemSize += entityVisSet->numFacesToAdd * sizeof(ushort);
			FS_Read(entityVisSet->facesToAdd, sizeof(ushort), entityVisSet->numFacesToAdd, fileHandle);
			
			FS_Read(&entityVisSet->numFacesToRemove, sizeof(ushort), 1, fileHandle);
			entityVisSet->facesToRemove = calloc(entityVisSet->numFacesToRemove, sizeof(ushort));
			cameraVisMemSize += entityVisSet->numFacesToRemove * sizeof(ushort);
			FS_Read(entityVisSet->facesToRemove, sizeof(ushort), entityVisSet->numFacesToRemove, fileHandle);

			
			
		}

	}
	
	
	return frame ;
}

camera_frame_t* CAM_ReadFileCP2Binary(char* filename,char prependGameDir)
{
	char* magicNumber = "CP2B" ;
	char  magicCheck[5];
	filehandle_t* fileHandle;
	int num_frames= 0 ;
	int i;
	
	
	camera_frame_t* frame;
	camera_frame_t firstFrame;


	//Open the file but don't load it in memory since it may be really huge..
	fileHandle = FS_OpenFile(filename, "rb");
	
	if (!fileHandle)
	{
		Log_Printf("[CAM_ReadFileCP2Binary] Could not load binary cp2 (%s).\n",filename);
		return 0;
	}
	
	FS_Read(magicCheck, 4, sizeof(char), fileHandle);
	magicCheck[4] = '\0';
	
	if (strcmp(magicNumber, magicCheck))
	{
		Log_Printf("[CAM_ReadFileCP2Binary] Found binary cp2 (%s) but magic number check failed.\n",filename);
		return 0;
	}
	
	FS_Read(&num_frames, sizeof(num_frames), 1, fileHandle);
	
	Log_Printf("[CAM_ReadFileCP2Binary] Found %d frames.\n",num_frames);
	
	frame = &firstFrame ;
	
	for(i=0 ; i < num_frames ; i++)
	{
		//Log_Printf("Reading binary frame %d/%d: t=",i+1,num_frames);
		frame->next = CAM_ReadFrameCP2Binary(fileHandle);
		frame= frame->next;
	}
	
	FS_CloseFile(fileHandle);
	
	return firstFrame.next;
}


void CAM_ExpandCameraWayPoints(camera_frame_t* startFrame,camera_frame_t* endFrame)
{
	camera_frame_t*	newFrame;
	camera_frame_t*	currentFrame;
	int						timeDifference;
	float					interpolationFactor;
	
	int time;
	int extraAccuracyTime;
	
	timeDifference = endFrame->time - startFrame->time;
	
	currentFrame = startFrame;
	
	time = startFrame->time;
	extraAccuracyTime=0;
	
	while (currentFrame->time  < endFrame->time) 
	{
		
		
		newFrame = (camera_frame_t*)calloc(1, sizeof(camera_frame_t));
		
		newFrame->time += currentFrame->time + 16+ (int)extraAccuracyTime ;
		
		
		
		newFrame->next = endFrame;
		currentFrame->next = newFrame;
		
		
		
		//Interpolate position and oritentation for the given time.
		
		interpolationFactor = (newFrame->time - startFrame->time) / (float)timeDifference;
		CAM_InterpolateFrames(startFrame,endFrame,interpolationFactor,newFrame->position,newFrame->orientation);
		
		
		
		currentFrame = newFrame;
		extraAccuracyTime+=0.6666667f;
	}
}

void CAM_LoadPath(void)
{	
	//Check extension
	char* extension;
	char binPath[256];

	cameraVisMemSize = 0;
	
	if (camera.pathFilename[0] == '\0')
	{
		Log_Printf("[CAM_LoadPath] No camera path loaded. Aborting.");
		exit(0);
	}
	
	extension = FS_GetExtensionAddress(camera.pathFilename);

	
	camera.path = NULL;
	
	if (!strcmp(extension, "cp"))
	{
		// TEXT rail. A runtime-culled scene needs no baked visibility, so the
		// rail is just expanded in memory (instant) -- that is how the boss act
		// loads, and why its rail can be edited as text and turn anywhere.
		// Otherwise this is the offline bake path (CI): the visibility set is
		// computed and the .cp2b written to the FS writable dir root, to be
		// committed next to the .cp (2010 wrote to a dead Mac-only path).
		// A runtime text load expands the rail ONLY (no visibility bake): that is
		// seconds instead of a very long grind, but it also means such a rail
		// carries no visible face set -- the decor then shows only where live
		// culling is active. Shipped acts therefore point at a baked .cp2b.
		// SHMUP_BAKE_VIS is the offline path (see bake-boss-rail.yml): compute the
		// visibility set and write the .cp2b out to be committed.
		// NOTE: this used to be keyed on gRuntimeCullMap, which now starts OFF --
		// so every text rail silently took the slow bake at load time.
		if (getenv("SHMUP_BAKE_VIS"))
		{
			PREPROC_SetSkipVis(0);
			memset(binPath, 0, 256);
			sprintf(binPath, "%s.cp2b", FS_GetFilenameOnly(camera.pathFilename));
			camera.path = PREPROC_ConvertCp1Tocp2b(camera.pathFilename,binPath,0);
		}
		else
		{
			PREPROC_SetSkipVis(1);
			camera.path = PREPROC_ConvertCp1Tocp2b(camera.pathFilename,NULL,0);
		}
	}
	else 
	{
		camera.path = CAM_ReadFileCP2Binary(camera.pathFilename,1);
	}

	
	if (camera.path == NULL)
	{
		Log_Printf("[CAM_LoadPath] Could not load camera path properly. Aborting.\n");
		exit(0);
	}
	
	
	camera.currentFrame = camera.path;
	simulationTime = camera.currentFrame->time;

	// Remembered before the frames start being consumed and freed: the end-of-rail
	// patrol sizes its legs from it, so it stays over the stretch actually flown.
	vectorCopy(camera.path->position, gCamPathStart);
	
	Log_Printf("[CAM_LoadPath] found and loaded %s.\n",camera.pathFilename);
//	Log_Printf("Camera path is taking %d kb in main memory.\n",cameraVisMemSize/1024);
	
}




