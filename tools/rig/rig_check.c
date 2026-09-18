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

#define CUT   8.0f
#define BLEND 2.0f
#define PIVOT_X 8.5f

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
	float maxPos = 0, maxDist = 0;
	int maxNrm = 0;

	FS_InitFilesystem();
	memset(&orig, 0, sizeof(orig));
	memset(&rig, 0, sizeof(rig));
	if (!MD5_LoadMesh(&orig, "data/models/enemies/lofb.obj.md5mesh")) { printf("cannot load the original mesh\n"); return 2; }
	if (!MD5_LoadMesh(&rig,  "data/models/enemies/lofb_rigged.md5mesh")) { printf("cannot load the rigged mesh\n"); return 2; }

	printf("original: %d joints %d verts %d weights\n", orig.numBones, orig.numVertices, orig.numWeights);
	printf("rigged:   %d joints %d verts %d weights\n", rig.numBones, rig.numVertices, rig.numWeights);
	CHECK(rig.numBones == 3, "rigged mesh has %d bones, expected 3", rig.numBones);
	CHECK(rig.numVertices == orig.numVertices, "vertex count differs");
	CHECK(rig.numIndices == orig.numIndices, "index count differs");
	for (i = 0; i < orig.numIndices && i < rig.numIndices; i++)
		if (orig.indices[i] != rig.indices[i]) { CHECK(0, "index %d differs", i); break; }

	/* 1. rest pose */
	for (i = 0; i < orig.numVertices; i++)
	{
		int k;
		for (k = 0; k < 3; k++)
		{
			float d = fabsf(orig.vertexArray[i].pos[k] - rig.vertexArray[i].pos[k]);
			if (d > maxPos) maxPos = d;
			int dn = abs((int)orig.vertexArray[i].normal[k] - (int)rig.vertexArray[i].normal[k]);
			if (dn > maxNrm) maxNrm = dn;
		}
		CHECK(orig.vertexArray[i].text[0] == rig.vertexArray[i].text[0] && orig.vertexArray[i].text[1] == rig.vertexArray[i].text[1], "uv of vertex %d differs", i);
	}
	printf("rest pose: max position deviation %.3g units, max normal deviation %d/32767\n", maxPos, maxNrm);
	CHECK(maxPos < 1e-4f, "rest pose positions deviate by %.3g", maxPos);
	CHECK(maxNrm <= 2, "rest pose normals deviate by %d", maxNrm);

	/* 2. swing armR by 30 degrees */
	{
		md5_bone_t* bones = (md5_bone_t*)calloc(rig.numBones, sizeof(md5_bone_t));
		vertex_t* rest = (vertex_t*)calloc(rig.numVertices, sizeof(vertex_t));
		memcpy(bones, rig.bones, rig.numBones * sizeof(md5_bone_t));
		memcpy(rest, rig.vertexArray, rig.numVertices * sizeof(vertex_t));
		quatAboutY(30.0f, bones[2].orientation);
		MD5_GenerateSkin(&rig, bones);

		moved = still = blendMoved = 0;
		for (i = 0; i < rig.numVertices; i++)
		{
			float x = rest[i].pos[0];
			float dx = rig.vertexArray[i].pos[0] - rest[i].pos[0];
			float dy = rig.vertexArray[i].pos[1] - rest[i].pos[1];
			float dz = rig.vertexArray[i].pos[2] - rest[i].pos[2];
			float d = sqrtf(dx*dx + dy*dy + dz*dz);
			if (d > maxDist) maxDist = d;
			if (fabsf(x) < CUT - BLEND || x < 0)		/* body, armL side, and every blended vertex on the left */
				CHECK(d < 1e-5f, "vertex %d (x=%.2f) moved %.3g with armR swung", i, x, d);
			else if (x > CUT + BLEND)					/* pure armR */
			{
				if (d > 1e-3f) moved++; else still++;
			}
			else if (d > 1e-5f) blendMoved++;			/* right shoulder blend */
			if (x > CUT + BLEND)
			{
				float nx = rig.vertexArray[i].normal[0] / 32767.0f, ny = rig.vertexArray[i].normal[1] / 32767.0f, nz = rig.vertexArray[i].normal[2] / 32767.0f;
				float len = sqrtf(nx*nx + ny*ny + nz*nz);
				CHECK(fabsf(len - 1.0f) < 0.01f, "vertex %d normal length %.3f after swing", i, len);
			}
		}
		printf("swing armR 30 deg: %d arm vertices moved, %d did not; %d shoulder vertices moved partially; farthest %.2f units\n", moved, still, blendMoved, maxDist);
		CHECK(still == 0, "%d pure-armR vertices did not move", still);
		CHECK(moved > 100, "only %d armR vertices moved", moved);
		CHECK(blendMoved > 50, "only %d shoulder vertices blended", blendMoved);
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

	if (fails) { printf("rig_check: %d FAILURE(S)\n", fails); return 1; }
	printf("rig_check: OK -- the rig is invisible at rest and only the swung arm moves.\n");
	return 0;
}
