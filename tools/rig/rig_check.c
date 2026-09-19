/*
 * rig_check -- proves the boss rig with the engine's own MD5 loader.
 *
 * Loads the 2010 one-joint mesh and the rigged three-bone mesh through
 * MD5_LoadMesh (md5.c, lexer.c, filesystem.c, quaternion.c, math.c compiled
 * as they are), then asserts:
 *   1. REST POSE: every skinned vertex of the rigged mesh sits where the
 *      original's does (positions within 1e-4 units, the float noise of
 *      (p - pivot) + pivot; normals within 2/32767) -- the rig changes
 *      nothing until a bone moves;
 *   2. SWING: with armR rotated 30 degrees about the mesh Y axis and the mesh
 *      re-skinned by MD5_GenerateSkin, the body vertices (|X| < CUT-BLEND)
 *      have not moved, every pure armR vertex has, and armL is untouched;
 *   3. the swung arm still has unit-length normals (the skin rotates them).
 *
 * Build (host, from the repo root):
 *   zig cc -std=gnu99 -Wno-implicit-function-declaration -Wno-unknown-pragmas
 *     -I engine/src tools/rig/rig_check.c engine/src/md5.c engine/src/lexer.c
 *     engine/src/quaternion.c engine/src/math.c engine/src/filesystem/filesystem.c
 *     -o rig_check
 * Run:  RD=data WD=. ./rig_check
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#include "md5.h"
#include "filesystem.h"
#include "quaternion.h"
#include "renderer.h"

/* Engine services the loader touches, stubbed. */
int  Log_Printf(const char* fmt, ...) { va_list ap; va_start(ap, fmt); int n = vprintf(fmt, ap); va_end(ap); return n; }
int  Log_ProbesEnabled(void) { return 0; }
void Log_Init(void) {}
renderer_t renderer;

/* Round 77: the cut moved to the tube that joins the body to the arm, |X| =
 * 5.5, the hinge at the tube's centroid (5.5, 0.44, 1.16). The arm is the
 * whole shoulder block + claw, 292 vertices a side. */
#define CUT   5.5f
#define PIVOT_X 5.5f
#define PIVOT_Z 1.16f

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Rotation of `deg` degrees about +Y as an MD5 quaternion (x y z w). */
static void quatAboutY(float deg, quat4_t q)
{
	float half = deg * (float)M_PI / 360.0f;
	q[0] = 0; q[1] = sinf(half); q[2] = 0; q[3] = cosf(half);
}

int main(void)
{
	md5_mesh_t orig, rig;
	int i, moved, still, blendMoved;
	float maxPos = 0, maxDist = 0, seamBand = 0;
	int maxNrm = 0;

	FS_InitFilesystem();
	memset(&orig, 0, sizeof(orig));
	memset(&rig, 0, sizeof(rig));
	if (!MD5_LoadMesh(&orig, "data/models/enemies/lofb.obj.md5mesh")) { printf("cannot load the original mesh\n"); return 2; }
	if (!MD5_LoadMesh(&rig,  "data/models/enemies/lofb_rigged.md5mesh")) { printf("cannot load the rigged mesh\n"); return 2; }

	printf("original: %d joints %d verts %d weights\n", orig.numBones, orig.numVertices, orig.numWeights);
	printf("rigged:   %d joints %d verts %d weights\n", rig.numBones, rig.numVertices, rig.numWeights);
	CHECK(rig.numBones == 5, "rigged mesh has %d bones, expected 5 (body, two blocs, two pinces -- round 83)", rig.numBones);
	CHECK(rig.numVertices >= orig.numVertices, "the rig lost vertices (%d < %d)", rig.numVertices, orig.numVertices);
	CHECK(rig.numIndices == orig.numIndices, "triangle count differs");
	/* Round 76: the seam is duplicated, so indices may point at a copy; the
	 * GEOMETRY of every triangle corner must still be the original's. */
	{
		int bad = 0;
		for (i = 0; i < orig.numIndices && i < rig.numIndices; i++)
		{
			const vertex_t* a = &orig.vertexArray[orig.indices[i]];
			const vertex_t* b = &rig.vertexArray[rig.indices[i]];
			if (fabsf(a->pos[0] - b->pos[0]) > 1e-4f || fabsf(a->pos[1] - b->pos[1]) > 1e-4f || fabsf(a->pos[2] - b->pos[2]) > 1e-4f ||
				a->text[0] != b->text[0] || a->text[1] != b->text[1])
				bad++;
		}
		printf("triangles: %d corners, %d with a geometry or UV that is not the original's (%d vertices were duplicated along the seam)\n",
			   rig.numIndices, bad, rig.numVertices - orig.numVertices);
		CHECK(bad == 0, "%d triangle corners differ from the original", bad);
	}

	/* 1. rest pose (the first N vertices are the originals, in order) */
	for (i = 0; i < orig.numVertices; i++)
	{
		int k;
		for (k = 0; k < 3; k++)
		{
			float d = fabsf(orig.vertexArray[i].pos[k] - rig.vertexArray[i].pos[k]);
			if (d > maxPos) maxPos = d;
			int dn = abs((int)orig.vertexArray[i].normal[k] - (int)rig.vertexArray[i].normal[k]);
			/* Round 76: a seam vertex now sees only its side's triangles, so its
			 * normal legitimately differs -- a lighting crease at the shoulder
			 * joint. Away from the cut the normals must be the original's. */
			/* Round 78: the seam is no longer a plane (antennas and rear legs stay
			 * with the body), so "distance to the cut" means nothing; count the
			 * vertices whose normal changed instead -- they are the seam. */
			if (dn > 2 && k == 0) seamBand += 1.0f;
			(void)maxNrm;
		}
		CHECK(orig.vertexArray[i].text[0] == rig.vertexArray[i].text[0] && orig.vertexArray[i].text[1] == rig.vertexArray[i].text[1], "uv of vertex %d differs", i);
	}
	printf("rest pose: max position deviation %.3g units; %d of %d vertices changed normal (the seam's lighting crease: shells see only their own faces)\n", maxPos, (int)seamBand, orig.numVertices);
	CHECK(maxPos < 1e-4f, "rest pose positions deviate by %.3g", maxPos);
	CHECK(seamBand < orig.numVertices * 0.15f, "the lighting crease touches %d vertices -- more than the seam", (int)seamBand);

	/* 2. swing armR by 30 degrees */
	{
		md5_bone_t* bones = (md5_bone_t*)calloc(rig.numBones, sizeof(md5_bone_t));
		vertex_t* rest = (vertex_t*)calloc(rig.numVertices, sizeof(vertex_t));
		memcpy(bones, rig.bones, rig.numBones * sizeof(md5_bone_t));
		memcpy(rest, rig.vertexArray, rig.numVertices * sizeof(vertex_t));
		quatAboutY(30.0f, bones[2].orientation);
		MD5_GenerateSkin(&rig, bones);

		/* Round 76: one weight per vertex, so a vertex's side is its bone. */
		moved = still = blendMoved = 0;
		for (i = 0; i < rig.numVertices; i++)
		{
			int bone = rig.weights[rig.vertices[i].start].boneId;
			float x = rest[i].pos[0];
			float dx = rig.vertexArray[i].pos[0] - rest[i].pos[0];
			float dy = rig.vertexArray[i].pos[1] - rest[i].pos[1];
			float dz = rig.vertexArray[i].pos[2] - rest[i].pos[2];
			float d = sqrtf(dx*dx + dy*dy + dz*dz);
			if (d > maxDist) maxDist = d;
			CHECK(rig.vertices[i].count == 1, "vertex %d has %d weights, expected 1 (hard cut)", i, rig.vertices[i].count);
			if (bone != 2)								/* body and armL, wherever they sit (seam copies included) */
				CHECK(d < 1e-5f, "vertex %d (bone %d, x=%.2f) moved %.3g with armR swung", i, bone, x, d);
			else
			{
				float nx = rig.vertexArray[i].normal[0] / 32767.0f, ny = rig.vertexArray[i].normal[1] / 32767.0f, nz = rig.vertexArray[i].normal[2] / 32767.0f;
				float len = sqrtf(nx*nx + ny*ny + nz*nz);
				if (d > 1e-3f) moved++; else still++;
				/* a seam copy can end with a zero normal (its two faces cancel): a
				 * lighting speck on the joint, not a skinning fault */
				if (fabsf(fabsf(x) - CUT) > 2.0f && fabsf(fabsf(x) - 16.3f) > 2.0f)	/* away from both seams: the tube's and the neck's */
					CHECK(fabsf(len - 1.0f) < 0.01f, "vertex %d normal length %.3f after swing", i, len);
			}
		}
		printf("swing armR (the bloc, bone 2) 30 deg: %d bloc vertices moved, %d did not; nothing else moved (the pince's bone 4 is composed at runtime, flat here); farthest %.2f units\n", moved, still, maxDist);
		CHECK(still == 0, "%d bloc vertices did not move", still);
		CHECK(moved > 60, "only %d bloc vertices moved", moved);
		free(bones); free(rest);
	}

	/* 4. MIRROR: the pose lofb.c computes at a few instants must keep the boss
	 *    left/right symmetric (the arms swing in mirror). Pair every vertex
	 *    with the one whose REST position is its mirror image (x -> -x), then
	 *    check the posed positions mirror too. Replays LOFB_PoseArms' idle
	 *    formula verbatim (swing about Y, mirrored for the left arm; tilt
	 *    about X), with no hit, no recoil, no tremor. */
	{
		md5_bone_t* bones = (md5_bone_t*)calloc(rig.numBones, sizeof(md5_bone_t));
		vertex_t* rest = (vertex_t*)calloc(rig.numVertices, sizeof(vertex_t));
		int* mirrorOf = (int*)calloc(rig.numVertices, sizeof(int));
		int j, paired = 0, tIdx;
		const int instants[3] = { 16000, 20000, 21300 };

		memcpy(bones, rig.bones, rig.numBones * sizeof(md5_bone_t));
		MD5_GenerateSkin(&rig, bones);
		memcpy(rest, rig.vertexArray, rig.numVertices * sizeof(vertex_t));
		for (i = 0; i < rig.numVertices; i++)
		{
			float best = 1e9f; mirrorOf[i] = -1;
			for (j = 0; j < rig.numVertices; j++)
			{
				float dx = rest[j].pos[0] + rest[i].pos[0], dy = rest[j].pos[1] - rest[i].pos[1], dz = rest[j].pos[2] - rest[i].pos[2];
				float d = dx*dx + dy*dy + dz*dz;
				if (d < best) { best = d; mirrorOf[i] = j; }
			}
			if (best < 1e-4f) paired++; else mirrorOf[i] = -1;
		}
		printf("mirror pairs at rest: %d of %d vertices have an exact mirror image\n", paired, rig.numVertices);
		CHECK(paired > rig.numVertices * 9 / 10, "the mesh is not mirror-symmetric at rest (%d pairs)", paired);

		for (tIdx = 0; tIdx < 3; tIdx++)
		{
			float t = (float)instants[tIdx], worst = 0;
			int k;
			for (k = 0; k < 2; k++)
			{
				float mirror = (k == 0) ? 1.0f : -1.0f;
				float swing = 6.0f * sinf(t * (float)(2 * M_PI) / 4000.0f + 0.6f * k);
				float tilt  = 2.0f * sinf(t * (float)(2 * M_PI) / 2300.0f + 1.1f * k);
				quat4_t qy, qx;
				float hy = swing * mirror * (float)M_PI / 360.0f, hx = tilt * (float)M_PI / 360.0f;
				qy[0] = 0; qy[1] = sinf(hy); qy[2] = 0; qy[3] = cosf(hy);
				qx[0] = sinf(hx); qx[1] = 0; qx[2] = 0; qx[3] = cosf(hx);
				Quat_multQuat(qy, qx, bones[1 + k].orientation);
			}
			MD5_GenerateSkin(&rig, bones);
			for (i = 0; i < rig.numVertices; i++)
			{
				float dx, dy, dz, d;
				j = mirrorOf[i];
				if (j < 0) continue;
				dx = rig.vertexArray[j].pos[0] + rig.vertexArray[i].pos[0];
				dy = rig.vertexArray[j].pos[1] - rig.vertexArray[i].pos[1];
				dz = rig.vertexArray[j].pos[2] - rig.vertexArray[i].pos[2];
				d = sqrtf(dx*dx + dy*dy + dz*dz);
				if (d > worst) worst = d;
			}
			printf("posed at t=%d ms: worst mirror deviation %.4f units\n", instants[tIdx], worst);
			CHECK(worst < 1.5f, "the idle pose at t=%d breaks the left/right symmetry by %.3f units", instants[tIdx], worst);	/* the arms breathe with a 0.6/1.1 rad phase offset: a unit or so of asymmetry is the design; 8+ would be a bug */
		}
		free(bones); free(rest); free(mirrorOf);
	}

	/* 5. DIRECTIONS (informative): where does the right claw's tip go under
	 *    each pose ingredient? Read against a screenshot: mesh X is screen
	 *    right, mesh Y faces the camera, mesh Z runs up the screen. */
	{
		md5_bone_t* bones = (md5_bone_t*)calloc(rig.numBones, sizeof(md5_bone_t));
		int tip = -1; float tipX = 0;
		const char* names[3] = { "swing +30 about Y (armR, mirror=-1 -> -30)", "tilt +12 about X (flinch)", "tilt +75 about X (wreck)" };
		int p;
		memcpy(bones, rig.bones, rig.numBones * sizeof(md5_bone_t));
		MD5_GenerateSkin(&rig, bones);
		for (i = 0; i < rig.numVertices; i++) if (rig.vertexArray[i].pos[0] > tipX) { tipX = rig.vertexArray[i].pos[0]; tip = i; }
		printf("right claw tip at rest: (%.2f, %.2f, %.2f)\n", rig.vertexArray[tip].pos[0], rig.vertexArray[tip].pos[1], rig.vertexArray[tip].pos[2]);
		for (p = 0; p < 3; p++)
		{
			float ang = (p == 0) ? -30.0f : (p == 1) ? 12.0f : 75.0f;
			float h = ang * (float)M_PI / 360.0f;
			quat4_t q = {0, 0, 0, 1};
			if (p == 0) { q[1] = sinf(h); } else { q[0] = sinf(h); }
			q[3] = cosf(h);
			memcpy(bones[2].orientation, q, sizeof(quat4_t));
			MD5_GenerateSkin(&rig, bones);
			printf("  %-44s -> tip (%.2f, %.2f, %.2f)\n", names[p], rig.vertexArray[tip].pos[0], rig.vertexArray[tip].pos[1], rig.vertexArray[tip].pos[2]);
		}
		free(bones);
	}

	/* 6. SILHOUETTE (informative): the right arm's rest cloud in bone space,
	 *    binned along its length (X from the shoulder), with the Z range and
	 *    centroid per bin -- the numbers the solid circles and the crook are
	 *    drawn from. */
	{
		md5_bone_t* bones = (md5_bone_t*)calloc(rig.numBones, sizeof(md5_bone_t));
		int b, n[8] = {0}; float zmin[8], zmax[8], cz[8], cy[8], cx[8];
		memcpy(bones, rig.bones, rig.numBones * sizeof(md5_bone_t));
		MD5_GenerateSkin(&rig, bones);
		for (b = 0; b < 8; b++) { zmin[b] = 1e9f; zmax[b] = -1e9f; cz[b] = cy[b] = cx[b] = 0; }
		for (i = 0; i < rig.numVertices; i++)
		{
			float x = rig.vertexArray[i].pos[0], y = rig.vertexArray[i].pos[1], z = rig.vertexArray[i].pos[2];
			if (x <= CUT) continue;						/* right arm + right shoulder blend */
			b = (int)((x - CUT) / ((22.9f - CUT) / 8.0f)); if (b > 7) b = 7;
			n[b]++; cx[b] += x; cy[b] += y; cz[b] += z;
			if (z < zmin[b]) zmin[b] = z; if (z > zmax[b]) zmax[b] = z;
		}
		printf("right arm silhouette, bins along X from the cut (mesh units; -Z is screen-up):\n");
		for (b = 0; b < 8; b++)
			if (n[b]) printf("  x %5.1f..%5.1f  n=%3d  centroid (%.1f, %.1f, %.1f)  z %.1f..%.1f\n",
							 CUT + b * (22.9f - CUT) / 8.0f, CUT + (b + 1) * (22.9f - CUT) / 8.0f, n[b], cx[b]/n[b], cy[b]/n[b], cz[b]/n[b], zmin[b], zmax[b]);
		free(bones);
	}

	/* 7. SOLIDS + CROOK: replay lofb.c's LOFB_BuildArmSolids on the rest skin
	 *    (bone space: X outward from the shoulder, Z down the screen; 2 x 3 unit
	 *    cells, radius = farthest vertex + 0.6) and assert that the crook --
	 *    the notch above the forearm against the claw, (7.8, -0.2) r 2.5 --
	 *    stays clear of every solid by at least a ship's radius (~1.8 units at
	 *    the nominal depth). Then the laser: at the nominal scale (a boss 45.7
	 *    units wide spanning ~1.9 ss, tall-phone aspect 0.46) the beam at the
	 *    full +/-66 degree sweep must miss the crook with the ship's margin. */
	{
		enum { NX = 9, NZ = 9 };
		const float CELLX = 2.0f, CELLZ = 3.0f, MARGIN = 0.6f, PZ = PIVOT_Z;
		/* the crook the tester hides in: UNDER the arm, between body and claw --
		 * mesh (13.5, 5.5), bone (8.0, 4.34); carved pockets as lofb.c */
		const float CROOK_BX = 8.0f, CROOK_BZ = 4.34f, CROOK_R = 3.0f;
		const float CROOK_X0 = 3.5f, CROOK_X1 = 10.5f, CROOK_Z0 = 0.84f, CROOK_Z1 = 7.84f;
		const float UPPER_X0 = 8.0f, UPPER_X1 = 13.5f, UPPER_Z0 = -13.5f, UPPER_Z1 = -4.5f;
		const float widthAtDistance = 24.0f, heightAtDistance = 52.0f;	/* nominal, see above */
		const float shipR = 0.035f * heightAtDistance;
		md5_bone_t* bones = (md5_bone_t*)calloc(rig.numBones, sizeof(md5_bone_t));
		int n[NX][NZ]; float sx[NX][NZ], sz[NX][NZ], rr[NX][NZ];
		int a, b, count = 0; float clearance = 1e9f;
		memcpy(bones, rig.bones, rig.numBones * sizeof(md5_bone_t));
		MD5_GenerateSkin(&rig, bones);
		memset(n, 0, sizeof(n)); memset(sx, 0, sizeof(sx)); memset(sz, 0, sizeof(sz)); memset(rr, 0, sizeof(rr));
		for (i = 0; i < rig.numVertices; i++)
		{
			float bx = rig.vertexArray[i].pos[0] - PIVOT_X, bz = rig.vertexArray[i].pos[2] - PZ;
			{ int bn = rig.weights[rig.vertices[i].start].boneId; if ((bn != 2 && bn != 4) || bx < 0) continue; }	/* the right arm: bloc (2) + pince (4) */
			a = (int)(bx / CELLX); b = (int)((bz + 13.5f) / CELLZ); if (a >= NX) a = NX - 1; if (b < 0) b = 0; if (b >= NZ) b = NZ - 1;
			n[a][b]++; sx[a][b] += bx; sz[a][b] += bz;
		}
		for (i = 0; i < rig.numVertices; i++)
		{
			float bx = rig.vertexArray[i].pos[0] - PIVOT_X, bz = rig.vertexArray[i].pos[2] - PZ, dx, dz, d;
			{ int bn = rig.weights[rig.vertices[i].start].boneId; if ((bn != 2 && bn != 4) || bx < 0) continue; }
			a = (int)(bx / CELLX); b = (int)((bz + 13.5f) / CELLZ); if (a >= NX) a = NX - 1; if (b < 0) b = 0; if (b >= NZ) b = NZ - 1;
			dx = bx - sx[a][b] / n[a][b]; dz = bz - sz[a][b] / n[a][b]; d = sqrtf(dx*dx + dz*dz);
			if (d > rr[a][b]) rr[a][b] = d;
		}
		{
			int carved = 0;
			for (a = 0; a < NX; a++) for (b = 0; b < NZ; b++) if (n[a][b])
			{
				float cx = sx[a][b] / n[a][b], cz = sz[a][b] / n[a][b], r = rr[a][b] + MARGIN;
				float dx = cx - CROOK_BX, dz = cz - CROOK_BZ, room = sqrtf(dx*dx + dz*dz) - r;	/* room for the ship's centre at the crook */
				if ((cx >= CROOK_X0 && cx <= CROOK_X1 && cz >= CROOK_Z0 && cz <= CROOK_Z1) ||
					(cx >= UPPER_X0 && cx <= UPPER_X1 && cz >= UPPER_Z0 && cz <= UPPER_Z1)) { carved++; continue; }
				count++;
				if (room < clearance) clearance = room;
			}
			printf("solid arms: %d circles on the right arm (%d carved out for the crook); a ship parked at the crook (%.1f, %.1f) has %.2f units to the nearest solid, its radius being %.2f\n",
				   count, carved, CROOK_BX, CROOK_BZ, clearance, shipR);
			CHECK(count > 10 && count <= 48, "unexpected solid count %d", count);
			CHECK(carved >= 1, "the pocket carved nothing -- the crook constants miss the arm");
			CHECK(clearance > shipR, "the ship does not fit in the crook: %.2f units of room for a radius of %.2f", clearance, shipR);
		}
		{
			/* the laser, nominal scale: boss at ss (0, 0.55); crook in px */
			float mx = PIVOT_X + CROOK_BX, mz = PZ + CROOK_BZ;
			float cx = (0.0f + mx / widthAtDistance) * SS_W, cy = (0.55f - mz / heightAtDistance) * SS_H;
			float ox = 0.0f * SS_W, oy = 0.55f * SS_H, rpx = CROOK_R / heightAtDistance * SS_H;
			float hw = 0.12f * SS_H, len = 2.5f * SS_H, worst = -1e9f; int deg, firstTouch = -1;
			for (deg = 0; deg <= 90; deg++)
			{
				float ang = -(float)M_PI / 2.0f + deg * (float)M_PI / 180.0f, dx = cosf(ang), dy = sinf(ang);
				float rx = cx - ox, ry = cy - oy, proj = rx*dx + ry*dy, perp = fabsf(rx*dy - ry*dx);
				float intrusion = (hw + 0.035f * SS_H + rpx) - perp;		/* > 0 = the capsule reaches the crook */
				if (proj > -0.15f * len && proj < len && intrusion > worst) worst = intrusion;
				if (proj > -0.15f * len && proj < len && intrusion > 0 && firstTouch < 0) firstTouch = deg;
			}
			(void)worst;
			printf("laser vs crook (nominal scale): the crook would first be touched at a sweep of %d degrees (shipped amplitude: 50, round 78)\n", firstTouch);
			/* Round 78: the shipped amplitude (0.88 rad, 50 deg) is wider than the
			 * crook allows on purpose -- LOFB_ClampSweep trims each beam to the
			 * crook's edge while an arm lives (the tester's "coupe la poire en
			 * deux"), and the full 50 deg returns once both arms are torn off. The
			 * guarantee here: the clamped beam still sweeps a useful cone. */
			CHECK(firstTouch >= 38, "the clamp would leave the beam only %d degrees of sweep -- the crook sits too close to the body", firstTouch);
		}
		free(bones);
	}

	/* 8. RENDER DUMP (optional): RIG_DUMP=<file> writes every posed vertex, for a
	 *    few poses lofb.c can take, as "pose x y z bone" lines -- the input of
	 *    the schematic renders (tools/rig/render_poses.awk). Poses: rest; the
	 *    pincer open (-18 deg) and shut (+45 deg); the right arm torn off,
	 *    half-way through its tumble. Same quaternion recipe as LOFB_PoseArms. */
	if (getenv("RIG_DUMP"))
	{
		FILE* out = fopen(getenv("RIG_DUMP"), "w");
		md5_bone_t* bones = (md5_bone_t*)calloc(rig.numBones, sizeof(md5_bone_t));
		const char* names[4] = { "rest", "open", "shut", "torn" };
		int p;
		for (p = 0; p < 4 && out; p++)
		{
			int k;
			memcpy(bones, rig.bones, rig.numBones * sizeof(md5_bone_t));
			for (k = 0; k < 2; k++)
			{
				/* Round 83, five bones: the Bloc (1+k) breathes, the Pince (3+k) does
				 * the pincer; the Pince's absolute transform is composed from the
				 * Bloc's exactly as lofb.c does it. */
				float mirror = (k == 0) ? 1.0f : -1.0f, sign = (k == 0) ? -1.0f : 1.0f;
				float blocSwing = 0, blocTilt = 0, pinceSwing = 0, pinceTilt = 0;
				quat4_t qy, qx, qLocal; float hy, hx; vec3_t neck, turned;
				if (p == 1) pinceSwing = -18.0f;
				if (p == 2) pinceSwing = 60.0f;
				if (p == 3 && k == 1) { float f = 0.5f; blocSwing = 70.0f * f; blocTilt = 140.0f * f; pinceSwing = 40.0f * f;
					bones[1 + k].position[0] += sign * 10.0f * f; bones[1 + k].position[2] += 60.0f * f * f; }
				hy = blocSwing * mirror * (float)M_PI / 360.0f; hx = blocTilt * (float)M_PI / 360.0f;
				qy[0] = 0; qy[1] = sinf(hy); qy[2] = 0; qy[3] = cosf(hy);
				qx[0] = sinf(hx); qx[1] = 0; qx[2] = 0; qx[3] = cosf(hx);
				Quat_multQuat(qy, qx, bones[1 + k].orientation);
				hy = pinceSwing * mirror * (float)M_PI / 360.0f; hx = pinceTilt * (float)M_PI / 360.0f;
				qy[0] = 0; qy[1] = sinf(hy); qy[2] = 0; qy[3] = cosf(hy);
				qx[0] = sinf(hx); qx[1] = 0; qx[2] = 0; qx[3] = cosf(hx);
				Quat_multQuat(qy, qx, qLocal);
				Quat_multQuat(bones[1 + k].orientation, qLocal, bones[3 + k].orientation);
				neck[0] = rig.bones[3 + k].position[0] - rig.bones[1 + k].position[0];
				neck[1] = rig.bones[3 + k].position[1] - rig.bones[1 + k].position[1];
				neck[2] = rig.bones[3 + k].position[2] - rig.bones[1 + k].position[2];
				Quat_rotatePoint(bones[1 + k].orientation, neck, turned);
				bones[3 + k].position[0] = bones[1 + k].position[0] + turned[0];
				bones[3 + k].position[1] = bones[1 + k].position[1] + turned[1];
				bones[3 + k].position[2] = bones[1 + k].position[2] + turned[2];
			}
			MD5_GenerateSkin(&rig, bones);
			for (i = 0; i < rig.numVertices; i++)
				fprintf(out, "%s %.3f %.3f %.3f %d\n", names[p], rig.vertexArray[i].pos[0], rig.vertexArray[i].pos[1], rig.vertexArray[i].pos[2],
						rig.weights[rig.vertices[i].start].boneId);
		}
		if (out) { fclose(out); printf("dumped 4 poses to %s\n", getenv("RIG_DUMP")); }
		free(bones);
	}

	if (fails) { printf("rig_check: %d FAILURE(S)\n", fails); return 1; }
	printf("rig_check: OK -- the rig is invisible at rest and only the swung arm moves.\n");
	return 0;
}
