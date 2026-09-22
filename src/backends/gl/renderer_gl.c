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
 *  renderer_gl.c -- the desktop OpenGL backend (round 87, the Windows port).
 *
 *  A 1:1 port of renderer_metal.m: the scene walk, the boss cameo, the
 *  crossing stars, the live cull and its [cull] probe, the ghost/flicker
 *  enemy draws are the Metal file's line for line; only the encoder calls
 *  became GL state and draws. Read the two side by side.
 *
 *  The fixed pipeline, emulated in GLSL 1.20 exactly as the MSL does it: one
 *  light (GL's default material: ambient 0.2, diffuse 0.8, global ambient
 *  0.2), GL_LINEAR fog per vertex, texture environments REPLACE / MODULATE /
 *  ADD, the two blend modes (alpha, additive), depth test and write as
 *  separate switches, back-face culling for ships and enemies only.
 *
 *  Coordinates: the engine's matrices ARE OpenGL's, so nothing is remapped:
 *  clip z stays in [-1,1], the viewport origin is bottom-left as renderer.c
 *  computed it, glReadPixels returns rows bottom-up as the probes expect, and
 *  texture rows are uploaded top-down as the loaders produce them, row 0 at
 *  v = 0 -- the very memory layout OpenGL ES read in 2009.
 *
 *  Client-side vertex arrays (GL 2.1, buffer 0 bound) replace the Metal ring
 *  buffer: a draw hands its pointer straight to glVertexAttribPointer. Only
 *  static meshes live in VBOs. Texture and buffer ids are GL names.
 */

#include "config.h"
#include "renderer_gl.h"
#include "dEngine.h"
#include "camera.h"
#include "target.h"
#include "stats.h"
#include "collisions.h"
#include "world.h"
#include "player.h"
#include "enemy.h"
#include "timer.h"
#include "fx.h"
#include "commands.h"
#include "enemy_particules.h"
#include "entities.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

// Round 90: the same file serves OpenGL ES 2.0 on Android. The entry points
// are linked directly (libGLESv2), the shaders lose their version line and
// gain a precision qualifier, and the two desktop-only calls (the fixed
// pipeline of the cursor outline, glClearDepth) have ES spellings.
#if defined(SHMUP_TARGET_ANDROID) || defined(__ANDROID__)
#define GLR_ES2 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#define GLSL_VS_PREAMBLE ""
#define GLSL_FS_PREAMBLE "precision mediump float;\n"
#define pglCreateShader            glCreateShader
#define pglShaderSource            glShaderSource
#define pglCompileShader           glCompileShader
#define pglGetShaderiv             glGetShaderiv
#define pglGetShaderInfoLog        glGetShaderInfoLog
#define pglCreateProgram           glCreateProgram
#define pglAttachShader            glAttachShader
#define pglBindAttribLocation      glBindAttribLocation
#define pglLinkProgram             glLinkProgram
#define pglGetProgramiv            glGetProgramiv
#define pglGetProgramInfoLog       glGetProgramInfoLog
#define pglUseProgram              glUseProgram
#define pglGetUniformLocation      glGetUniformLocation
#define pglUniform1i               glUniform1i
#define pglUniform4iv              glUniform4iv
#define pglUniform4fv              glUniform4fv
#define pglUniformMatrix4fv        glUniformMatrix4fv
#define pglEnableVertexAttribArray glEnableVertexAttribArray
#define pglDisableVertexAttribArray glDisableVertexAttribArray
#define pglVertexAttribPointer     glVertexAttribPointer
#define pglGenBuffers              glGenBuffers
#define pglDeleteBuffers           glDeleteBuffers
#define pglBindBuffer              glBindBuffer
#define pglBufferData              glBufferData
#define pglGenerateMipmap          glGenerateMipmap
#define pglActiveTexture           glActiveTexture
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP    0x8191
#endif
#else
#define GLSL_VS_PREAMBLE "#version 120\n"
#define GLSL_FS_PREAMBLE "#version 120\n"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

// ---------------------------------------------------------------------------
//  The GL 2.x entry points, resolved at GLR_Create (opengl32.dll exports 1.1)
// ---------------------------------------------------------------------------
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#define GL_FRAGMENT_SHADER    0x8B30
#define GL_VERTEX_SHADER      0x8B31
#define GL_COMPILE_STATUS     0x8B81
#define GL_LINK_STATUS        0x8B82
#define GL_INFO_LOG_LENGTH    0x8B84
#define GL_ARRAY_BUFFER       0x8892
#define GL_STATIC_DRAW        0x88E4
#define GL_CLAMP_TO_EDGE      0x812F
#define GL_GENERATE_MIPMAP    0x8191
#define GL_TEXTURE0           0x84C0

typedef GLuint (APIENTRYP PFN_glCreateShader)(GLenum);
typedef void   (APIENTRYP PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (APIENTRYP PFN_glCompileShader)(GLuint);
typedef void   (APIENTRYP PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRYP PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (APIENTRYP PFN_glCreateProgram)(void);
typedef void   (APIENTRYP PFN_glAttachShader)(GLuint, GLuint);
typedef void   (APIENTRYP PFN_glBindAttribLocation)(GLuint, GLuint, const GLchar*);
typedef void   (APIENTRYP PFN_glLinkProgram)(GLuint);
typedef void   (APIENTRYP PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRYP PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (APIENTRYP PFN_glUseProgram)(GLuint);
typedef GLint  (APIENTRYP PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void   (APIENTRYP PFN_glUniform1i)(GLint, GLint);
typedef void   (APIENTRYP PFN_glUniform4iv)(GLint, GLsizei, const GLint*);
typedef void   (APIENTRYP PFN_glUniform4fv)(GLint, GLsizei, const GLfloat*);
typedef void   (APIENTRYP PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (APIENTRYP PFN_glEnableVertexAttribArray)(GLuint);
typedef void   (APIENTRYP PFN_glDisableVertexAttribArray)(GLuint);
typedef void   (APIENTRYP PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (APIENTRYP PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void   (APIENTRYP PFN_glDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRYP PFN_glBindBuffer)(GLenum, GLuint);
typedef void   (APIENTRYP PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void   (APIENTRYP PFN_glGenerateMipmap)(GLenum);
typedef void   (APIENTRYP PFN_glActiveTexture)(GLenum);

static PFN_glCreateShader            pglCreateShader;
static PFN_glShaderSource            pglShaderSource;
static PFN_glCompileShader           pglCompileShader;
static PFN_glGetShaderiv             pglGetShaderiv;
static PFN_glGetShaderInfoLog        pglGetShaderInfoLog;
static PFN_glCreateProgram           pglCreateProgram;
static PFN_glAttachShader            pglAttachShader;
static PFN_glBindAttribLocation      pglBindAttribLocation;
static PFN_glLinkProgram             pglLinkProgram;
static PFN_glGetProgramiv            pglGetProgramiv;
static PFN_glGetProgramInfoLog       pglGetProgramInfoLog;
static PFN_glUseProgram              pglUseProgram;
static PFN_glGetUniformLocation      pglGetUniformLocation;
static PFN_glUniform1i               pglUniform1i;
static PFN_glUniform4iv              pglUniform4iv;
static PFN_glUniform4fv              pglUniform4fv;
static PFN_glUniformMatrix4fv        pglUniformMatrix4fv;
static PFN_glEnableVertexAttribArray pglEnableVertexAttribArray;
static PFN_glDisableVertexAttribArray pglDisableVertexAttribArray;
static PFN_glVertexAttribPointer     pglVertexAttribPointer;
static PFN_glGenBuffers              pglGenBuffers;
static PFN_glDeleteBuffers           pglDeleteBuffers;
static PFN_glBindBuffer              pglBindBuffer;
static PFN_glBufferData              pglBufferData;
static PFN_glGenerateMipmap          pglGenerateMipmap;		// GL 3.0 / EXT_framebuffer_object; NULL -> GL_GENERATE_MIPMAP
static PFN_glActiveTexture           pglActiveTexture;
#endif	// desktop GL

// ---------------------------------------------------------------------------
//  The programs: one vertex shader per vertex kind, one fragment shader.
//  Attribute slots: 0 pos, 1 normal, 2 uv, 3 colour.
// ---------------------------------------------------------------------------
enum { VK_3D = 0, VK_STAR, VK_2D, VK_2DC, VK_2DT, VK_COUNT };
enum { BL_NONE = 0, BL_ALPHA, BL_ADD };
enum { A_POS = 0, A_NORMAL = 1, A_UV = 2, A_COLOR = 3 };

static const char* kCommonVS =
GLSL_VS_PREAMBLE
"uniform mat4 uMVP; uniform mat4 uMV; uniform mat4 uNormalM;\n"
"uniform vec4 uColor; uniform vec4 uLightPosEye; uniform vec4 uLightAmbient; uniform vec4 uLightDiffuse; uniform vec4 uLightSpecular;\n"
"uniform vec4 uMatSpecular; uniform vec4 uParams; uniform ivec4 uFlags;\n"
"varying vec2 vUV; varying vec4 vColor; varying float vFogF;\n"
"vec4 gl_light(vec4 eye, vec3 n) {\n"
"  const vec3 matAmb = vec3(0.2); const vec3 matDif = vec3(0.8); const vec3 globAmb = vec3(0.2);\n"
"  vec3 L = uLightPosEye.xyz - eye.xyz; float d = length(L); L = L / max(d, 1e-5);\n"
"  float att = 1.0 / max(uParams.z + uParams.w * d, 1e-5);\n"
"  float ndl = max(dot(n, L), 0.0);\n"
"  vec3 c = globAmb * matAmb + att * (uLightAmbient.rgb * matAmb + ndl * uLightDiffuse.rgb * matDif);\n"
"  if (ndl > 0.0) {\n"
"    vec3 V = normalize(-eye.xyz); vec3 H = normalize(L + V);\n"
"    float s = pow(max(dot(n, H), 0.0), max(uMatSpecular.w, 1e-3));\n"
"    c += att * s * uLightSpecular.rgb * uMatSpecular.rgb;\n"
"  }\n"
"  return vec4(clamp(c, 0.0, 1.0), 1.0);\n"
"}\n"
"float gl_fog(vec4 eye) {\n"
"  if (uFlags.z == 0) return 1.0;\n"
"  float z = -eye.z;\n"
"  return clamp((uParams.y - z) / max(uParams.y - uParams.x, 1e-5), 0.0, 1.0);\n"
"}\n";

static const char* kVS[VK_COUNT] = {
// 3D: vertex_t (pos float3, normal short3 normalized, uv short2 normalized)
"attribute vec3 aPos; attribute vec3 aNormal; attribute vec2 aUV;\n"
"void main() { vec4 p = vec4(aPos, 1.0); vec4 eye = uMV * p; gl_Position = uMVP * p; vUV = aUV;\n"
"  if (uFlags.x != 0) { vec3 n = normalize((uNormalM * vec4(aNormal, 0.0)).xyz); vColor = gl_light(eye, n); } else vColor = uColor;\n"
"  vFogF = gl_fog(eye); }\n",
// stars: float3 pos, uchar4 colour
"attribute vec3 aPos; attribute vec4 aColor;\n"
"void main() { gl_Position = uMVP * vec4(aPos, 1.0); vUV = vec2(0.0); vColor = aColor * uColor; vFogF = 1.0; }\n",
// 2D: short2 pos, short2 uv normalized
"attribute vec2 aPos; attribute vec2 aUV;\n"
"void main() { gl_Position = uMVP * vec4(aPos, 0.0, 1.0); vUV = aUV; vColor = uColor; vFogF = 1.0; }\n",
// 2D coloured
"attribute vec2 aPos; attribute vec2 aUV; attribute vec4 aColor;\n"
"void main() { gl_Position = uMVP * vec4(aPos, 0.0, 1.0); vUV = aUV; vColor = aColor * uColor; vFogF = 1.0; }\n",
// 2D textureless
"attribute vec2 aPos; attribute vec4 aColor;\n"
"void main() { gl_Position = uMVP * vec4(aPos, 0.0, 1.0); vUV = vec2(0.0); vColor = aColor * uColor; vFogF = 1.0; }\n",
};

static const char* kFS =
GLSL_FS_PREAMBLE
"uniform sampler2D uTex; uniform vec4 uFogColor; uniform ivec4 uFlags;\n"
"varying vec2 vUV; varying vec4 vColor; varying float vFogF;\n"
"void main() {\n"
"  vec4 c = vColor;\n"
"  if (uFlags.w != 0) {\n"
"    vec4 t = texture2D(uTex, vUV);\n"
"    if (uFlags.y == 0) c = t;\n"                                                   // GL_REPLACE
"    else if (uFlags.y == 1) c = t * vColor;\n"                                     // GL_MODULATE
"    else c = vec4(min(t.rgb + vColor.rgb, vec3(1.0)), t.a * vColor.a);\n"          // GL_ADD
"  }\n"
"  if (uFlags.z != 0) c.rgb = mix(uFogColor.rgb, c.rgb, vFogF);\n"
"  gl_FragColor = c;\n"
"}\n";

typedef struct
{
	GLuint prog;
	GLint uMVP, uMV, uNormalM, uColor, uLightPosEye, uLightAmbient, uLightDiffuse, uLightSpecular;
	GLint uMatSpecular, uFogColor, uParams, uFlags, uTex;
} glr_program_t;

static glr_program_t gProg[VK_COUNT];

// The uniforms of one draw -- the Metal file's MtlUniforms, in plain floats.
typedef struct
{
	matrix_t mvp, mv, normalM;
	float color[4], lightPosEye[4], lightAmbient[4], lightDiffuse[4], lightSpecular[4];
	float matSpecular[4], fogColor[4], params[4];
	int   flags[4];		// x lighting, y texEnv (0 REPLACE 1 MODULATE 2 ADD), z fog, w textured
} glr_uniforms_t;

// The GL state we emulate (the same names as the Metal file).
static int      sBlend = BL_NONE;
static int      sDepthTest = 0, sDepthWrite = 1;
static int      sCullBack = 0;
static int      sTexEnv = 1;			// 1 = MODULATE
static int      sTextured = 1;
static int      sLighting = 0;
static int      sFog = 0;
static float    sColor[4] = {1,1,1,1};
static unsigned sTexture = 0;			// bound textureId (0 = none)
static matrix_t sProj, sMV;
static int      sPixelW = 0, sPixelH = 0;
static int      sSurfX = 0, sSurfY = 0, sSurfW = 0, sSurfH = 0;	// the engine's surface in the window (round 89)
static int      gCreated = 0;

static int lastTextureIdG = -1;

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------
static void matrix_copy16(matrix_t dst, const matrix_t src) { memcpy(dst, src, sizeof(matrix_t)); }

// inverse-transpose of mv's 3x3, padded into a 4x4 (column-major)
static void NormalMatrix(const matrix_t mv, matrix_t out)
{
	float a = mv[0], b = mv[1], c = mv[2];
	float d = mv[4], e = mv[5], f = mv[6];
	float g = mv[8], h = mv[9], i = mv[10];
	float det = a*(e*i - f*h) - d*(b*i - c*h) + g*(b*f - c*e);
	float inv[9];
	int k;
	if (fabsf(det) < 1e-12f) det = 1e-12f;
	// inverse (row-major of the 3x3 whose columns are (a,b,c),(d,e,f),(g,h,i))
	inv[0] =  (e*i - f*h) / det; inv[1] = -(b*i - c*h) / det; inv[2] =  (b*f - c*e) / det;
	inv[3] = -(d*i - f*g) / det; inv[4] =  (a*i - c*g) / det; inv[5] = -(a*f - c*d) / det;
	inv[6] =  (d*h - e*g) / det; inv[7] = -(a*h - b*g) / det; inv[8] =  (a*e - b*d) / det;
	// transpose of the inverse, back into column-major 4x4: out[col*4 + row]
	for (k = 0; k < 16; k++) out[k] = 0;
	out[0] = inv[0]; out[1] = inv[3]; out[2] = inv[6];
	out[4] = inv[1]; out[5] = inv[4]; out[6] = inv[7];
	out[8] = inv[2]; out[9] = inv[5]; out[10] = inv[8];
	out[15] = 1;
}

static void MulMV4(const matrix_t m, const float v[4], float out[4])
{
	int r;
	for (r = 0; r < 4; r++)
		out[r] = m[0*4+r]*v[0] + m[1*4+r]*v[1] + m[2*4+r]*v[2] + m[3*4+r]*v[3];
}

static GLuint CompileShader(GLenum type, const char* a, const char* b)
{
	const GLchar* src[2] = { a, b };
	GLuint s = pglCreateShader(type);
	GLint ok = 0;
	pglShaderSource(s, b ? 2 : 1, src, NULL);
	pglCompileShader(s);
	pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[2048]; GLsizei n = 0;
		pglGetShaderInfoLog(s, sizeof(log), &n, log);
		Log_Printf("[GL] shader compile failed: %.*s\n", (int)n, log);
		return 0;
	}
	return s;
}

static int BuildPrograms(void)
{
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFS, NULL);
	int k;
	if (!fs) return 0;
	for (k = 0; k < VK_COUNT; k++)
	{
		glr_program_t* p = &gProg[k];
		GLuint vs = CompileShader(GL_VERTEX_SHADER, kCommonVS, kVS[k]);
		GLint ok = 0;
		if (!vs) return 0;
		p->prog = pglCreateProgram();
		pglAttachShader(p->prog, vs);
		pglAttachShader(p->prog, fs);
		pglBindAttribLocation(p->prog, A_POS, "aPos");
		pglBindAttribLocation(p->prog, A_NORMAL, "aNormal");
		pglBindAttribLocation(p->prog, A_UV, "aUV");
		pglBindAttribLocation(p->prog, A_COLOR, "aColor");
		pglLinkProgram(p->prog);
		pglGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			char log[2048]; GLsizei n = 0;
			pglGetProgramInfoLog(p->prog, sizeof(log), &n, log);
			Log_Printf("[GL] program %d link failed: %.*s\n", k, (int)n, log);
			return 0;
		}
		p->uMVP = pglGetUniformLocation(p->prog, "uMVP");
		p->uMV = pglGetUniformLocation(p->prog, "uMV");
		p->uNormalM = pglGetUniformLocation(p->prog, "uNormalM");
		p->uColor = pglGetUniformLocation(p->prog, "uColor");
		p->uLightPosEye = pglGetUniformLocation(p->prog, "uLightPosEye");
		p->uLightAmbient = pglGetUniformLocation(p->prog, "uLightAmbient");
		p->uLightDiffuse = pglGetUniformLocation(p->prog, "uLightDiffuse");
		p->uLightSpecular = pglGetUniformLocation(p->prog, "uLightSpecular");
		p->uMatSpecular = pglGetUniformLocation(p->prog, "uMatSpecular");
		p->uFogColor = pglGetUniformLocation(p->prog, "uFogColor");
		p->uParams = pglGetUniformLocation(p->prog, "uParams");
		p->uFlags = pglGetUniformLocation(p->prog, "uFlags");
		p->uTex = pglGetUniformLocation(p->prog, "uTex");
	}
	return 1;
}

static void FillUniforms(glr_uniforms_t* u, int kind)
{
	matrix_multiply(sProj, sMV, u->mvp);
	matrix_copy16(u->mv, sMV);
	if (kind == VK_3D && sLighting)
		NormalMatrix(sMV, u->normalM);
	else
		matrixLoadIdentity(u->normalM);
	memcpy(u->color, sColor, sizeof(sColor));
	// GL transforms the light position by the modelview current at glLight
	// time -- the view matrix, since SetupLighting runs right after the camera.
	{
		float lp[4] = { light.position[0], light.position[1], light.position[2], light.position[3] };
		MulMV4(sMV, lp, u->lightPosEye);
	}
	memcpy(u->lightAmbient, light.ambient, sizeof(float) * 4);
	memcpy(u->lightDiffuse, light.diffuse, sizeof(float) * 4);
	memcpy(u->lightSpecular, light.specula, sizeof(float) * 4);
	u->matSpecular[0] = u->matSpecular[1] = u->matSpecular[2] = u->matSpecular[3] = 0;	// filled per entity
	u->fogColor[0] = renderer.fogColor[0]; u->fogColor[1] = renderer.fogColor[1]; u->fogColor[2] = renderer.fogColor[2]; u->fogColor[3] = 1;
	u->params[0] = (float)renderer.fogStartAt; u->params[1] = (float)renderer.fogStopAt;
	u->params[2] = light.constantAttenuation; u->params[3] = light.linearAttenuation;
	u->flags[0] = (kind == VK_3D) ? sLighting : 0;
	u->flags[1] = sTexEnv;
	u->flags[2] = (kind == VK_3D) ? sFog : 0;
	// "Textured" only if a texture is actually bound: sampling nothing is undefined.
	u->flags[3] = (sTextured && sTexture != 0) ? 1 : 0;
}

// Bind everything a draw needs from the emulated state.
static int BindForDraw(int kind, const glr_uniforms_t* u)
{
	const glr_program_t* p = &gProg[kind];
	if (!gCreated || !p->prog)
		return 0;
	pglUseProgram(p->prog);
	pglUniformMatrix4fv(p->uMVP, 1, GL_FALSE, u->mvp);
	pglUniformMatrix4fv(p->uMV, 1, GL_FALSE, u->mv);
	pglUniformMatrix4fv(p->uNormalM, 1, GL_FALSE, u->normalM);
	pglUniform4fv(p->uColor, 1, u->color);
	pglUniform4fv(p->uLightPosEye, 1, u->lightPosEye);
	pglUniform4fv(p->uLightAmbient, 1, u->lightAmbient);
	pglUniform4fv(p->uLightDiffuse, 1, u->lightDiffuse);
	pglUniform4fv(p->uLightSpecular, 1, u->lightSpecular);
	pglUniform4fv(p->uMatSpecular, 1, u->matSpecular);
	pglUniform4fv(p->uFogColor, 1, u->fogColor);
	pglUniform4fv(p->uParams, 1, u->params);
	pglUniform4iv(p->uFlags, 1, u->flags);
	pglUniform1i(p->uTex, 0);

	if (sDepthTest) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); } else glDisable(GL_DEPTH_TEST);
	glDepthMask(sDepthWrite ? GL_TRUE : GL_FALSE);
	if (sBlend == BL_NONE) glDisable(GL_BLEND);
	else
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, sBlend == BL_ALPHA ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE);
	}
	if (sCullBack && kind == VK_3D) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); }
	else glDisable(GL_CULL_FACE);

	if (u->flags[3])
	{
		if (pglActiveTexture) pglActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, sTexture);
	}
	return 1;
}

// Attribute pointers for one vertex kind over a base pointer (client memory,
// or an offset into the bound VBO when base is NULL-relative).
static void SetAttribs(int kind, const char* base)
{
	pglDisableVertexAttribArray(A_NORMAL);
	pglDisableVertexAttribArray(A_UV);
	pglDisableVertexAttribArray(A_COLOR);
	pglEnableVertexAttribArray(A_POS);
	switch (kind)
	{
		case VK_3D:
			pglVertexAttribPointer(A_POS, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), base + offsetof(vertex_t, pos));
			pglEnableVertexAttribArray(A_NORMAL);
			pglVertexAttribPointer(A_NORMAL, 3, GL_SHORT, GL_TRUE, sizeof(vertex_t), base + offsetof(vertex_t, normal));
			pglEnableVertexAttribArray(A_UV);
			pglVertexAttribPointer(A_UV, 2, GL_SHORT, GL_TRUE, sizeof(vertex_t), base + offsetof(vertex_t, text));
			break;
		case VK_STAR:
			pglVertexAttribPointer(A_POS, 3, GL_FLOAT, GL_FALSE, 16, base);
			pglEnableVertexAttribArray(A_COLOR);
			pglVertexAttribPointer(A_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, base + 12);
			break;
		case VK_2D:
			pglVertexAttribPointer(A_POS, 2, GL_SHORT, GL_FALSE, sizeof(xf_colorless_sprite_t), base + offsetof(xf_colorless_sprite_t, pos));
			pglEnableVertexAttribArray(A_UV);
			pglVertexAttribPointer(A_UV, 2, GL_SHORT, GL_TRUE, sizeof(xf_colorless_sprite_t), base + offsetof(xf_colorless_sprite_t, text));
			break;
		case VK_2DC:
			pglVertexAttribPointer(A_POS, 2, GL_SHORT, GL_FALSE, sizeof(xf_sprite_t), base + offsetof(xf_sprite_t, pos));
			pglEnableVertexAttribArray(A_UV);
			pglVertexAttribPointer(A_UV, 2, GL_SHORT, GL_TRUE, sizeof(xf_sprite_t), base + offsetof(xf_sprite_t, text));
			pglEnableVertexAttribArray(A_COLOR);
			pglVertexAttribPointer(A_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(xf_sprite_t), base + offsetof(xf_sprite_t, color));
			break;
		case VK_2DT:
			pglVertexAttribPointer(A_POS, 2, GL_SHORT, GL_FALSE, sizeof(xf_textureless_sprite_t), base + offsetof(xf_textureless_sprite_t, pos));
			pglEnableVertexAttribArray(A_COLOR);
			pglVertexAttribPointer(A_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(xf_textureless_sprite_t), base + offsetof(xf_textureless_sprite_t, color));
			break;
	}
}

// Indexed draw from client memory (the GL glDrawElements idiom, literally).
static void DrawIndexedClient(int kind, const void* vertices, const ushort* indices, unsigned numIndices, GLenum prim)
{
	glr_uniforms_t u;
	if (numIndices == 0 || !vertices || !indices)
		return;
	FillUniforms(&u, kind);
	if (!BindForDraw(kind, &u))
		return;
	pglBindBuffer(GL_ARRAY_BUFFER, 0);
	SetAttribs(kind, (const char*)vertices);
	glDrawElements(prim, (GLsizei)numIndices, GL_UNSIGNED_SHORT, indices);
}

static void DrawArraysClient(int kind, const void* vertices, unsigned numVertices, GLenum prim)
{
	glr_uniforms_t u;
	if (numVertices == 0 || !vertices)
		return;
	FillUniforms(&u, kind);
	if (!BindForDraw(kind, &u))
		return;
	pglBindBuffer(GL_ARRAY_BUFFER, 0);
	SetAttribs(kind, (const char*)vertices);
	glDrawArrays(prim, 0, (GLsizei)numVertices);
}

// The viewport the engine computed (letterboxing lives in renderer.c); GL's
// origin is bottom-left, like the engine's.
static void ApplyViewport(void)
{
	int x = renderer.viewPortDimensions[VP_X], y = renderer.viewPortDimensions[VP_Y];
	int w = renderer.viewPortDimensions[VP_WIDTH], h = renderer.viewPortDimensions[VP_HEIGHT];
	if (w <= 0 || h <= 0) { x = 0; y = 0; w = sSurfW; h = sSurfH; }
	glViewport(sSurfX + x, sSurfY + y, w, h);
}

// Read back a rectangle of the CURRENT frame, in SURFACE coordinates (GL
// style, origin bottom-left) and GL_RGBA: glReadPixels, offset by the surface.
static void ReadPixelsGL(int x, int y, int w, int h, uchar* out)
{
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x + w > sSurfW) w = sSurfW - x;
	if (y + h > sSurfH) h = sSurfH - y;
	if (w <= 0 || h <= 0) return;
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(sSurfX + x, sSurfY + y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

void GLR_SetSurface(int x, int y, int w, int h)
{
	sSurfX = x; sSurfY = y; sSurfW = w; sSurfH = h;
}

void GLR_DrawRectOutline(float x, float y, float w, float h, float r, float g, float b, float a, float width)
{
#ifdef GLR_ES2
	// no fixed pipeline on ES 2, and no keyboard on a phone: nothing to draw
	(void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a; (void)width;
	return;
#else
	if (!gCreated) return;
	pglUseProgram(0);
	pglBindBuffer(GL_ARRAY_BUFFER, 0);
	pglDisableVertexAttribArray(A_POS); pglDisableVertexAttribArray(A_NORMAL);
	pglDisableVertexAttribArray(A_UV);  pglDisableVertexAttribArray(A_COLOR);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glViewport(sSurfX, sSurfY, sSurfW, sSurfH);
	glMatrixMode(GL_PROJECTION); glLoadIdentity();
	glOrtho(0, sSurfW, 0, sSurfH, -1, 1);
	glMatrixMode(GL_MODELVIEW); glLoadIdentity();
	glLineWidth(width);
	glColor4f(r, g, b, a);
	glBegin(GL_LINE_LOOP);
	glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
	glEnd();
	glLineWidth(1.0f);
	glEnable(GL_TEXTURE_2D);
	lastTextureIdG = -1;
#endif
}

// ---------------------------------------------------------------------------
//  Creation, resize, frame brackets
// ---------------------------------------------------------------------------
#define LOAD(name) do { p##name = (PFN_##name)getProc(#name); } while (0)

int GLR_Create(glr_getproc_t getProc, int pixelWidth, int pixelHeight)
{
	const char* version;
#ifndef GLR_ES2
	LOAD(glCreateShader); LOAD(glShaderSource); LOAD(glCompileShader); LOAD(glGetShaderiv); LOAD(glGetShaderInfoLog);
	LOAD(glCreateProgram); LOAD(glAttachShader); LOAD(glBindAttribLocation); LOAD(glLinkProgram); LOAD(glGetProgramiv);
	LOAD(glGetProgramInfoLog); LOAD(glUseProgram); LOAD(glGetUniformLocation); LOAD(glUniform1i); LOAD(glUniform4iv);
	LOAD(glUniform4fv); LOAD(glUniformMatrix4fv); LOAD(glEnableVertexAttribArray); LOAD(glDisableVertexAttribArray);
	LOAD(glVertexAttribPointer); LOAD(glGenBuffers); LOAD(glDeleteBuffers); LOAD(glBindBuffer); LOAD(glBufferData);
	LOAD(glActiveTexture);
	pglGenerateMipmap = (PFN_glGenerateMipmap)getProc("glGenerateMipmap");
	if (!pglGenerateMipmap)
		pglGenerateMipmap = (PFN_glGenerateMipmap)getProc("glGenerateMipmapEXT");

	if (!pglCreateShader || !pglShaderSource || !pglCompileShader || !pglGetShaderiv || !pglGetShaderInfoLog ||
	    !pglCreateProgram || !pglAttachShader || !pglBindAttribLocation || !pglLinkProgram || !pglGetProgramiv ||
	    !pglGetProgramInfoLog || !pglUseProgram || !pglGetUniformLocation || !pglUniform1i || !pglUniform4iv ||
	    !pglUniform4fv || !pglUniformMatrix4fv || !pglEnableVertexAttribArray || !pglDisableVertexAttribArray ||
	    !pglVertexAttribPointer || !pglGenBuffers || !pglDeleteBuffers || !pglBindBuffer || !pglBufferData)
	{
		Log_Printf("[GL] this context has no OpenGL 2.0 entry points.\n");
		return 0;
	}
	glEnable(GL_TEXTURE_2D);
#else
	(void)getProc;		// ES 2: the entry points are the library's
#endif
	version = (const char*)glGetString(GL_VERSION);
	if (!BuildPrograms())
		return 0;
	glDisable(GL_DITHER);
	GLR_Resize(pixelWidth, pixelHeight);
	if (sSurfW == 0) GLR_SetSurface(0, 0, pixelWidth, pixelHeight);
	gCreated = 1;
	Log_Printf("[GL] backend up on %s / %s, GL %s, %dx%d.\n",
	           (const char*)glGetString(GL_VENDOR), (const char*)glGetString(GL_RENDERER), version ? version : "?", pixelWidth, pixelHeight);
	return 1;
}

void GLR_Resize(int pixelWidth, int pixelHeight)
{
	if (pixelWidth <= 0 || pixelHeight <= 0)
		return;
	sPixelW = pixelWidth;
	sPixelH = pixelHeight;
}

void GLR_BeginFrame(void)
{
	if (!gCreated)
		return;
	// The whole window black -- the bands around the surface -- then the
	// surface in the colour Set3DF chose: fog or black.
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, sPixelW, sPixelH);
	glClearColor(0, 0, 0, 1.0f);
#ifdef GLR_ES2
	glClearDepthf(1.0f);
#else
	glClearDepth(1.0);
#endif
	glDepthMask(GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (engine.fogEnabled && sSurfW > 0 && sSurfH > 0)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(sSurfX, sSurfY, sSurfW, sSurfH);
		glClearColor(renderer.fogColor[0], renderer.fogColor[1], renderer.fogColor[2], 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
	}
	ApplyViewport();
}

void GLR_EndFrame(void)
{
	// SwapBuffers belongs to the platform.
}

// ---------------------------------------------------------------------------
//  The renderer_t entry points
// ---------------------------------------------------------------------------
static void Set3DG(void)
{
	sDepthTest = 1; sDepthWrite = 1;
	sBlend = BL_NONE; renderer.isBlending = 0;
	sLighting = light.enabled ? 1 : 0;
	sTextured = 1;
	sTexEnv = 1;			// GL_MODULATE
	sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
	sFog = 0;
	sCullBack = 0;
	ApplyViewport();
}

static void Set2DG(void)
{
	matrix_t ortho;
	int i;
	sBlend = BL_ALPHA;
	for (i = 0; i < 16; i++) ortho[i] = 0;
	// glOrthof(-SS_W, SS_W, -SS_H, SS_H, -1, 1), column-major
	ortho[0]  = 1.0f / SS_W;
	ortho[5]  = 1.0f / SS_H;
	ortho[10] = -1.0f;
	ortho[15] = 1.0f;
	memcpy(sProj, ortho, sizeof(matrix_t));
	matrixLoadIdentity(sMV);
	sCullBack = 0;
	sFog = 0;
	sLighting = 0;
	sDepthTest = 0; sDepthWrite = 0;
	sTextured = 1;
}

static void StopRenditionG(void)
{
	lastTextureIdG = -1;
}

static void SetTextureG(unsigned int textureId)
{
	if ((int)textureId == lastTextureIdG)
		return;
	sTexture = textureId;
	STATS_AddTexSwitch();
	lastTextureIdG = (int)textureId;
}

static void UpLoadTextureToGPUG(texture_t* texture)
{
	GLuint tex = 0;

	if (!texture || !texture->data || texture->textureId != 0)
		return;

	if (texture->format == TEXTURE_GL_RGB || texture->format == TEXTURE_GL_RGBA)
	{
		// The loaders hand us 4 bytes per pixel either way; for RGB force the
		// fourth byte to opaque, as the Metal backend does.
		if (texture->format == TEXTURE_GL_RGB)
		{
			uchar* px = texture->data[0];
			size_t n = (size_t)texture->width * texture->height, k;
			for (k = 0; k < n; k++)
				px[k * 4 + 3] = 255;
		}
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (!pglGenerateMipmap)
			glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);	// GL 1.4: built at upload
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)texture->width, (GLsizei)texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture->data[0]);
		if (pglGenerateMipmap)
			pglGenerateMipmap(GL_TEXTURE_2D);
		free(texture->data[0]);
		texture->data[0] = 0;
	}
	else
	{
		// PVRTC never reaches a desktop GPU: IsTextureCompressionSupported says
		// no, and the loader takes the PNG folder instead.
		Log_Printf("[GL] unsupported texture format 0x%x for %s\n", texture->format, texture->path);
		return;
	}

	texture->textureId = tex;

	free(texture->dataLength);
	texture->dataLength = 0;
	free(texture->data);
	texture->data = 0;
	texture->memLocation = TEXT_MEM_LOC_VRAM;
	if (texture->file != NULL)
		FS_CloseFile(texture->file);
}

static void FreeGPUTextureG(texture_t* texture)
{
	if (texture->textureId > 0)
	{
		GLuint t = texture->textureId;
		glDeleteTextures(1, &t);
	}
	texture->textureId = 0;
	lastTextureIdG = -1;
}

static uint UploadVerticesToGPUG(void* vertices, uint mem_size)
{
	GLuint b = 0;
	pglGenBuffers(1, &b);
	pglBindBuffer(GL_ARRAY_BUFFER, b);
	pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)mem_size, vertices, GL_STATIC_DRAW);
	pglBindBuffer(GL_ARRAY_BUFFER, 0);
	return (uint)b;
}

static void FreeGPUBufferG(uint bufferId)
{
	if (bufferId > 0)
	{
		GLuint b = bufferId;
		pglDeleteBuffers(1, &b);
	}
}

static void UpLoadEntityToGPUG(entity_t* entity)
{
	md5_mesh_t* mesh;
	if (entity == NULL || entity->model == NULL)
		return;
	if (entity->model->memLocation == MD5_MEMLOC_VRAM)
		return;
	mesh = entity->model;
	mesh->vboId = UploadVerticesToGPUG(mesh->vertexArray, mesh->numVertices * sizeof(vertex_t));
#ifndef GENERATE_VIDEO
	free(mesh->vertexArray);
	mesh->vertexArray = 0;
#endif
	mesh->memLocation = MD5_MEMLOC_VRAM;
}

// RenderEntityM, on GL: the entity's matrix, its material's texture and
// specular, the model's vertex buffer (or its RAM skin for a dynamic mesh),
// and the index list the fixed renderer would have used.
static void RenderEntityG(entity_t* entity)
{
	glr_uniforms_t u;
	matrix_t mv;
	const ushort* idx; unsigned nidx;

	if (!entity || !entity->model)
		return;

	// glPushMatrix; glMultMatrixf(entity->matrix)
	matrix_multiply(sMV, entity->matrix, mv);
	{
		matrix_t savedMV;
		matrix_copy16(savedMV, sMV);
		matrix_copy16(sMV, mv);
		FillUniforms(&u, VK_3D);
		matrix_copy16(sMV, savedMV);
	}
	if (entity->material)
	{
		u.matSpecular[0] = entity->material->specularColor[0]; u.matSpecular[1] = entity->material->specularColor[1];
		u.matSpecular[2] = entity->material->specularColor[2]; u.matSpecular[3] = entity->material->shininess;
		if (sTextured)
		{
			SetTextureG(entity->material->textures[TEXTURE_DIFFUSE].textureId);	// through the cache: the 2D passes compare against it
			u.flags[3] = (sTextured && sTexture != 0) ? 1 : 0;
		}
	}
	if (!BindForDraw(VK_3D, &u))
		return;

	if (entity->model->memLocation == MD5_MEMLOC_VRAM)
	{
		if (!entity->model->vboId)
			return;
		pglBindBuffer(GL_ARRAY_BUFFER, entity->model->vboId);
		SetAttribs(VK_3D, (const char*)0);
	}
	else
	{
		if (!entity->model->vertexArray)
			return;
		pglBindBuffer(GL_ARRAY_BUFFER, 0);
		SetAttribs(VK_3D, (const char*)entity->model->vertexArray);
	}

	if (entity->usage == ENT_PARTIAL_DRAW && renderer.vScale <= 1.0f && !gRuntimeCullMap)
	{
		idx = entity->indices; nidx = entity->numIndices;
	}
	else
	{
		idx = entity->model->indices; nidx = entity->model->numIndices;
	}
	if (nidx == 0 || !idx)
	{
		pglBindBuffer(GL_ARRAY_BUFFER, 0);
		return;
	}
	glDrawElements(GL_TRIANGLES, (GLsizei)nidx, GL_UNSIGNED_SHORT, idx);
	pglBindBuffer(GL_ARRAY_BUFFER, 0);
	STATS_AddTriangles((int)nidx / 3);
}

static void SetupCameraG(void)
{
	vec3_t vLookat;
	vectorAdd(camera.position, camera.forward, vLookat);
	gluLookAt(camera.position, vLookat, camera.up, sMV);
}

#define CAMEO_T0		50000
#define CAMEO_T1		64000
#define CAMEO_SCALE		6.0f
#define CAMEO_DEPTH		1600.0f

// Verbatim port of RenderTTBBossCameoM: see renderer_metal.m for the story.
static void RenderTTBBossCameoG(void)
{
	static entity_t cameo;
	static int      cameoState = 0;
	static int      cameoGen = -1;
	static const matrix_t cameoFromAbove = {1,0,0,0,  0,0,1,0,  0,-1,0,0,  0,0,0,1};
	matrix_t pose;
	float u, vx, vy, cameoAlpha;
	int k;

	if (cameoGen != ENT_CacheGeneration())
	{
		cameoState = 0;
		cameo.model = NULL;
		cameoGen = ENT_CacheGeneration();
	}
	if (engine.sceneId != 3)
		return;
	if (cameoState == 0)
		cameoState = ENT_LoadEntity(&cameo, "data/models/enemies/lofb.obj.md5mesh", ENT_FULL_DRAW) ? 1 : -1;
	if (cameoState < 0 || cameo.model == NULL)
		return;
	if (fabsf(camera.ttbAngle) < 0.95f * (float)M_PI * 0.5f)
		return;
	if (simulationTime < CAMEO_T0 || simulationTime > CAMEO_T1)
		return;

	u = (simulationTime - CAMEO_T0) / (float)(CAMEO_T1 - CAMEO_T0);
	{
		float cross = u < 0.72f ? u / 0.72f : 1.0f;
		float dive  = u > 0.72f ? (u - 0.72f) / 0.28f : 0.0f;
		float breathe = 1.0f + 0.05f * sinf(u * 4.0f * (float)M_PI + 1.3f);
		dive = dive * dive;
		vx = 380.0f - cross * 530.0f;
		vy = 700.0f + 25.0f * sinf(u * 4.0f * (float)M_PI) - dive * 1100.0f;
		matrix_multiply(cameraInvRot, cameoFromAbove, pose);
		for (k = 0; k < 12; k++)
			pose[k] *= CAMEO_SCALE * breathe;
		cameoAlpha = 1.0f - dive;
	}
	pose[12] = camera.position[0] + camera.right[0]*vx + camera.up[0]*vy + camera.forward[0]*CAMEO_DEPTH;
	pose[13] = camera.position[1] + camera.right[1]*vx + camera.up[1]*vy + camera.forward[1]*CAMEO_DEPTH;
	pose[14] = camera.position[2] + camera.right[2]*vx + camera.up[2]*vy + camera.forward[2]*CAMEO_DEPTH;
	pose[15] = 1;
	for (k = 0; k < 16; k++)
		cameo.matrix[k] = pose[k];

	{
		static const int strikes[] = { 51200, 51420, 55600, 55780, 59300, 62600, 62790 };
		float L = 0;
		for (k = 0; k < (int)(sizeof(strikes)/sizeof(strikes[0])); k++)
		{
			int dt = simulationTime - strikes[k];
			if (dt >= 0 && dt < 600)
			{
				float e = expf(-dt / 120.0f);
				if (e > L) L = e;
			}
		}
		sColor[0] = 0.10f + (0.96f - 0.10f) * L;
		sColor[1] = 0.09f + (0.97f - 0.09f) * L;
		sColor[2] = 0.16f + (1.00f - 0.16f) * L;
		sColor[3] = cameoAlpha;
	}
	sBlend = BL_ALPHA;
	sTextured = 0;			// a flat silhouette: glDisable(GL_TEXTURE_2D)
	{
		int savedLighting = sLighting;
		sLighting = 0;
		RenderEntityG(&cameo);
		sLighting = savedLighting;
	}
	sTextured = 1;
	sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
	sBlend = BL_NONE;
}

typedef struct ttbstar_vertex_t
{
	float pos[3];
	uchar color[4];
} ttbstar_vertex_t;

// Verbatim port of RenderTTBStarsM.
static void RenderTTBStarsG(void)
{
	static const float star[9][5] = {
		{ 640.0f, 0.045f,    40.0f, 30.0f, 1.2f },
		{ 480.0f, 0.040f,   270.0f, 30.0f, 1.2f },
		{ 350.0f, 0.048f,   130.0f, 30.0f, 1.2f },
		{ 200.0f, 0.042f,   520.0f, 30.0f, 1.2f },
		{ 560.0f, 0.085f,   350.0f, 50.0f, 1.6f },
		{ 300.0f, 0.080f,    80.0f, 50.0f, 1.6f },
		{ 430.0f, 0.090f,   470.0f, 50.0f, 1.6f },
		{ 610.0f, 0.150f,   200.0f, 80.0f, 2.2f },
		{ 250.0f, 0.135f,   580.0f, 80.0f, 2.2f },
	};
	#define TTBSTAR_SPAN  620.0f
	#define TTBSTAR_DEPTH 1000.0f
	ttbstar_vertex_t v[9 * 12];
	int nV = 0;
	float f, fade;
	int i, k;

	f = fabsf(camera.ttbAngle) / ((float)M_PI * 0.5f);
	if (f <= 0.6f)
		return;
	fade = (f - 0.6f) / 0.4f;
	if (fade > 1.0f) fade = 1.0f;

	for (i = 0; i < 9; i++)
	{
		float x = 310.0f - fmodf(star[i][2] + star[i][1] * simulationTime, TTBSTAR_SPAN);
		float y = star[i][0];
		float trail = star[i][3], half = star[i][4];
		float qx[12] = { x-2.5f, x-2.5f, x+2.5f,  x+2.5f, x-2.5f, x+2.5f,
						 x,      x,      x+trail, x+trail, x,     x+trail };
		float qy[12] = { y-2.0f, y+2.0f, y+2.0f,  y-2.0f, y-2.0f, y+2.0f,
						 y-half, y+half, y+half,  y-half, y-half, y+half };
		static const int headA[6] = {0,1,2, 2,3,0};
		static const int tail0[6] = {0,1,2, 2,3,0};

		for (k = 0; k < 6; k++)
		{
			ttbstar_vertex_t* o = &v[nV++];
			int c = headA[k];
			float cq[4][2] = { {qx[0],qy[0]},{qx[1],qy[1]},{qx[2],qy[2]},{qx[3],qy[3]} };
			o->pos[0] = camera.position[0] + camera.right[0]*cq[c][0] + camera.up[0]*cq[c][1] + camera.forward[0]*TTBSTAR_DEPTH;
			o->pos[1] = camera.position[1] + camera.right[1]*cq[c][0] + camera.up[1]*cq[c][1] + camera.forward[1]*TTBSTAR_DEPTH;
			o->pos[2] = camera.position[2] + camera.right[2]*cq[c][0] + camera.up[2]*cq[c][1] + camera.forward[2]*TTBSTAR_DEPTH;
			o->color[0] = 255; o->color[1] = 255; o->color[2] = 255;
			o->color[3] = (uchar)(230 * fade);
		}
		for (k = 0; k < 6; k++)
		{
			ttbstar_vertex_t* o = &v[nV++];
			int c = tail0[k];
			float cq[4][2] = { {qx[6],qy[6]},{qx[7],qy[7]},{qx[8],qy[8]},{qx[9],qy[9]} };
			int atTail = (c == 2 || c == 3);
			o->pos[0] = camera.position[0] + camera.right[0]*cq[c][0] + camera.up[0]*cq[c][1] + camera.forward[0]*TTBSTAR_DEPTH;
			o->pos[1] = camera.position[1] + camera.right[1]*cq[c][0] + camera.up[1]*cq[c][1] + camera.forward[1]*TTBSTAR_DEPTH;
			o->pos[2] = camera.position[2] + camera.right[2]*cq[c][0] + camera.up[2]*cq[c][1] + camera.forward[2]*TTBSTAR_DEPTH;
			o->color[0] = 225; o->color[1] = 232; o->color[2] = 255;
			o->color[3] = atTail ? 0 : (uchar)(150 * fade);
		}
	}

	sBlend = BL_ADD;
	sTextured = 0;
	DrawArraysClient(VK_STAR, v, (unsigned)nV, GL_TRIANGLES);
	STATS_AddTriangles(nV / 3);
	sTextured = 1;
	sBlend = BL_NONE;
}

// RenderEntitiesM, on GL. The scene walk is the fixed renderer's, line for
// line; only the GL calls became state flips and RenderEntityG draws.
static void RenderEntitiesG(void)
{
	int i;
	entity_t* entity;
	enemy_t* enemy;
	frustrum_t	cullFrustrum;
	matrix_t	cullView, cullProj, cullPV;
	vec3_t		cullLookat;
	int			cullDrawn = 0;
	static int	cullDebug = -1;
	static int	cullLogTick = 0;
	static char	cullIds[160] = "";
	static int	cullTris = 0;
	static int	decorLuma = -1;
	static int	skyLuma = -1;

	gluPerspective(camera.fov, camera.aspect, camera.zNear, camera.zFar, sProj);
	SetupCameraG();

	if (gRuntimeCullMap)
	{
		vectorAdd(camera.position,camera.forward,cullLookat);
		gluLookAt(camera.position, cullLookat, camera.up, cullView);
		gluPerspective(camera.fov + 4, camera.aspect, camera.zNear, camera.zFar, cullProj);
		matrix_multiply(cullProj,cullView,cullPV);
		COLL_GenerateFrustrum(cullPV,cullFrustrum);
	}

	sCullBack = 0;
	sFog = 0;
	sTexEnv = 0;			// GL_REPLACE for the decor
	if (gRuntimeCullMap)
		sDepthWrite = 0;	// the domes are a skybox: no depth writes

	{
		int nearestDome = -1;
		if (gRuntimeCullMap && numBackgroundEntities > 1)
		{
			float bestD2 = 0;
			for(i=0; i < numBackgroundEntities; i++)
			{
				float cxd = 0, czd = 0, d2;
				int   c;
				for (c = 0; c < 8; c++)
				{
					cxd += map[i].worldSpacebbox[c][0];
					czd += map[i].worldSpacebbox[c][2];
				}
				cxd = cxd * 0.125f - camera.position[0];
				czd = czd * 0.125f - camera.position[2];
				d2  = cxd*cxd + czd*czd;
				if (nearestDome < 0 || d2 < bestD2)
				{
					bestD2 = d2;
					nearestDome = i;
				}
			}
		}

		for(i=0; i < numBackgroundEntities; i++)
		{
			float savedTx = 0, savedTz = 0;
			entity = &map[i];
			if (gRuntimeCullMap)
			{
				if (nearestDome >= 0 && i != nearestDome)
					continue;
				savedTx = entity->matrix[12];
				savedTz = entity->matrix[14];
				entity->matrix[12] = camera.position[0];
				entity->matrix[14] = camera.position[2];
				cullDrawn++;
				if (cullDebug == 1)
				{
					char idbuf[16];
					sprintf(idbuf, "%d,", i);
					if (strlen(cullIds) + strlen(idbuf) < sizeof(cullIds))
						strcat(cullIds, idbuf);
					cullTris += entity->model->numIndices / 3;
				}
			}
			else if (entity->numIndices == 0)
				continue;

			RenderEntityG(entity);

			if (gRuntimeCullMap)
			{
				entity->matrix[12] = savedTx;
				entity->matrix[14] = savedTz;
			}
		}
	}

	RenderTTBBossCameoG();
	if (gRuntimeCullMap)
		RenderTTBStarsG();

	if (gRuntimeCullMap)
	{
		sDepthWrite = 1;
		if (cullDebug == 1 && cullLogTick + 1 >= 60)
		{
			uchar spx[16*16*4];
			int   sk, ssum = 0;
			ReadPixelsGL(renderer.glBuffersDimensions[WIDTH]/2 - 8, renderer.glBuffersDimensions[HEIGHT]/2 - 8, 16, 16, spx);
			for (sk = 0; sk < 16*16; sk++)
				ssum += spx[sk*4] + spx[sk*4+1] + spx[sk*4+2];
			skyLuma = ssum / (16*16*3);
		}
	}

	if (engine.fogEnabled && (renderer.props & PROP_FOG) == PROP_FOG )
		sFog = 1;

	for(i=numBackgroundEntities; i < num_map_entities; i++)
	{
		entity = &map[i];
		if (gRuntimeCullMap)
		{
			if (COLL_CheckBoxAgainstFrustrum(entity->worldSpacebbox,cullFrustrum) == INT_OUT)
				continue;
			cullDrawn++;
			if (cullDebug == 1)
			{
				char idbuf[16];
				sprintf(idbuf, "%d,", i);
				if (strlen(cullIds) + strlen(idbuf) < sizeof(cullIds))
					strcat(cullIds, idbuf);
				cullTris += entity->model->numIndices / 3;
			}
		}
		else if (entity->numIndices == 0)
			continue;
		else
			cullDrawn++;

		RenderEntityG(entity);
	}
	sCullBack = 1;			// glEnable(GL_CULL_FACE): ships and enemies are culled

	if (cullDebug < 0)
		cullDebug = getenv("SHMUP_CULL_DEBUG") ? 1 : 0;
	if (cullDebug || gRuntimeCullMap)
	{
		if (cullDebug && ++cullLogTick >= 60)
		{
			uchar px[16*16*4];
			int   k, sum = 0;
			int   topLuma;
			int   cx = renderer.glBuffersDimensions[WIDTH] / 2 - 8;
			int   cy = renderer.glBuffersDimensions[HEIGHT] / 2 - 8;
			ReadPixelsGL(cx, cy, 16, 16, px);
			for (k = 0; k < 16*16; k++)
				sum += px[k*4] + px[k*4+1] + px[k*4+2];
			decorLuma = sum / (16*16*3);

			ReadPixelsGL(cx, renderer.glBuffersDimensions[HEIGHT] * 4 / 5, 16, 16, px);
			sum = 0;
			for (k = 0; k < 16*16; k++)
				sum += px[k*4] + px[k*4+1] + px[k*4+2];
			topLuma = sum / (16*16*3);

			cullLogTick = 0;
			Log_Printf("[cull] scene=%d live=%d t=%d pos=(%.0f,%.0f,%.0f) fwd=(%.2f,%.2f,%.2f) up=(%.2f,%.2f,%.2f) drew %d/%d ids=%s tris=%d sky=%d luma=%d top=%d p0=(%.2f,%.2f,ap%d)\n",
					   engine.sceneId, gRuntimeCullMap,
					   simulationTime,
					   camera.position[0], camera.position[1], camera.position[2],
					   camera.forward[0], camera.forward[1], camera.forward[2],
					   camera.up[0], camera.up[1], camera.up[2],
					   cullDrawn, num_map_entities, cullIds, cullTris, skyLuma, decorLuma, topLuma,
					   players[0].ss_position[X], players[0].ss_position[Y], players[0].autopilot.enabled);
		}
		if (cullDebug)
		{
			cullIds[0] = '\0';
			cullTris = 0;
		}
	}

	sFog = 0;
	if (engine.fogEnabled && (renderer.props & PROP_FOG) == PROP_FOG)
		sFog = 1;

	sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
	for (i=0 ; i < numPlayers; i++)
	{
		if (!players[i].shouldDraw)
			continue;
		if (players[i].entity.color[3] > 0.001f && players[i].entity.color[3] < 0.999f)
		{
			sTexEnv = 1;			// GL_MODULATE
			sBlend = BL_ALPHA;
			sColor[0] = players[i].entity.color[0]; sColor[1] = players[i].entity.color[1];
			sColor[2] = players[i].entity.color[2]; sColor[3] = players[i].entity.color[3];
			RenderEntityG(&players[i].entity);
			sBlend = BL_NONE;
			sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
			sTexEnv = 0;			// GL_REPLACE
		}
		else
			RenderEntityG(&players[i].entity);
	}

	sTexEnv = 1;			// GL_MODULATE for the enemies
	enemy = ENE_GetFirstEnemy();
	while (enemy != NULL)
	{
		entity = &enemy->entity;
		if (enemy->shouldFlicker)
		{
			sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
			sTexEnv = 2;		// GL_ADD
			RenderEntityG(entity);
			enemy->shouldFlicker = 0;
			sTexEnv = 1;
		}
		else
		{
			int ghost = entity->color[A] < 0.999f;
			if (ghost)
				sBlend = BL_ALPHA;
			sColor[0] = entity->color[R]; sColor[1] = entity->color[G]; sColor[2] = entity->color[B]; sColor[3] = entity->color[A];
			RenderEntityG(entity);
			if (ghost)
				sBlend = BL_NONE;
		}
		enemy = enemy->next;
	}
	sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
	sFog = 0;
}

static void RenderStringG(xf_colorless_sprite_t* vertices, ushort* indices, uint numIndices)
{
	DrawIndexedClient(VK_2D, vertices, indices, numIndices, GL_TRIANGLES);
	STATS_AddTriangles((int)numIndices / 3);
}

static void GetColorBufferG(uchar* data)
{
	ReadPixelsGL(0, 0, renderer.glBuffersDimensions[WIDTH], renderer.glBuffersDimensions[HEIGHT], data);
}

static void RenderColorlessSpritesG(xf_colorless_sprite_t* vertices, ushort numIndices, ushort* indices)
{
	DrawIndexedClient(VK_2D, vertices, indices, numIndices, GL_TRIANGLES);
	STATS_AddTriangles(numIndices / 2);
}

static void RenderColoredSprites(xf_sprite_t* vertices, int numIndices, ushort* indices)
{
	DrawIndexedClient(VK_2DC, vertices, indices, (unsigned)numIndices, GL_TRIANGLES);
	STATS_AddTriangles(numIndices / 3);
}

static void RenderTexturelessSpritesG(xf_textureless_sprite_t* vertices, ushort numIndices, ushort* indices)
{
	sTextured = 0;
	DrawIndexedClient(VK_2DT, vertices, indices, numIndices, GL_TRIANGLES);
	sTextured = 1;
	STATS_AddTriangles(numIndices / 3);
}

static void RenderPlayersBulletsG(void)
{
	sBlend = BL_ADD;			// GL_SRC_ALPHA, GL_ONE
	sTexEnv = 0;				// GL_REPLACE
	SetTextureG(bulletConfig.bulletTexture.textureId);
	DrawIndexedClient(VK_2D, pBulletVertices, bulletIndices, (unsigned)numPBulletsIndices, GL_TRIANGLES);
	STATS_AddTriangles(numPBulletsIndices/3);
	DrawIndexedClient(VK_2D, partLib.ss_vertices, partLib.indices, (unsigned)partLib.num_indices, GL_TRIANGLES);
	STATS_AddTriangles(partLib.num_indices/3);
}

static void RenderFXSpritesG(void)
{
	int i, j;

	sBlend = BL_ADD;			// GL_SRC_ALPHA, GL_ONE (smoke, ghosts, particles)
	SetTextureG(smokeTexture.textureId);
	if (numSmokeIndices != 0)
	{
		DrawIndexedClient(VK_2D, smokeVertices, smokeIndices, (unsigned)numSmokeIndices, GL_TRIANGLES);
		STATS_AddTriangles(numSmokeIndices/3);
	}

	SetTextureG(ghostTexture.textureId);
	for(i=0 ; i <numPlayers ; i++)
	{
		for (j=0; j< GHOSTS_NUM; j++)
		{
			if (players[i].ghosts[j].timeCounter >= GHOST_TTL_MS)
				continue;
			DrawArraysClient(VK_2D, &players[i].ghosts[j].wayPoints[players[i].ghosts[j].startVertexArray],
			                 (unsigned)players[i].ghosts[j].lengthVertexArray, GL_TRIANGLE_STRIP);
			STATS_AddTriangles((players[i].ghosts[j].lengthVertexArray/2));
		}
	}

	sTexEnv = 1;				// GL_MODULATE
	if (numParticulesIndices != 0)
	{
		SetTextureG(bulletConfig.bulletTexture.textureId);
		RenderColoredSprites(particuleVertices, numParticulesIndices, particuleIndices);
	}

	sBlend = BL_ALPHA;			// GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
	if (numExplosionIndices != 0)
	{
		SetTextureG(explosionTexture.textureId);
		RenderColoredSprites(explosionVertices, numExplosionIndices, explosionIndices);
	}

	SetTextureG(bulletConfig.bulletTexture.textureId);
	RenderColoredSprites(enFxLib.ss_vertices, enFxLib.num_indices, enFxLib.indices);
}

static void DrawControlsG(void)
{
	if (engine.controlMode == CONTROL_MODE_SWIP)
		return;
	sTextured = 0;
	DrawIndexedClient(VK_2DT, controlVertices, controlIndices, (unsigned)controlNumIndices, GL_TRIANGLE_STRIP);
	STATS_AddTriangles(controlNumIndices/2);
	sTextured = 1;
}

static void StartCleanFrameG(void)
{
	// Nothing to do: the clear belongs to GLR_BeginFrame.
}

static void FadeScreenG(float alpha)
{
	fadeVertices[0].color[A] = alpha * 255;
	fadeVertices[1].color[A] = alpha * 255;
	fadeVertices[2].color[A] = alpha * 255;
	fadeVertices[3].color[A] = alpha * 255;
	sTextured = 0;
	DrawIndexedClient(VK_2DT, fadeVertices, fadeIndices, 6, GL_TRIANGLES);
	sTextured = 1;
	STATS_AddTriangles(6/2);
}

static void SetMaterialTextureBlendingG(char modulate)
{
	sTexEnv = modulate ? 1 : 0;
}

static void SetTransparencyG(float alpha)
{
	sColor[0] = sColor[1] = sColor[2] = 1;
	sColor[3] = alpha;
}

static int IsTextureCompressionSupportedG(int type)
{
	(void)type;
	return 0;	// no PVRTC on a desktop GPU: the loaders take the PNG folder
}

// Round 41: a horizontal clip band for 2D drawing, in SS units. The 2D ortho
// maps y in [-SS_H, SS_H] onto the whole viewport height whatever the screen,
// so the pixel rows follow directly; x spans the full viewport. GL's rows
// count from the bottom, like the engine's viewport.
static void SetScissorG(int enable, short yTopSS, short yBottomSS)
{
	int vpX = renderer.viewPortDimensions[VP_X];
	int vpY = renderer.viewPortDimensions[VP_Y];
	int vpW = renderer.viewPortDimensions[VP_WIDTH];
	int vpH = renderer.viewPortDimensions[VP_HEIGHT];
	int bottom, top;
	if (!gCreated)
		return;
	if (vpW <= 0 || vpH <= 0) { vpX = 0; vpY = 0; vpW = sSurfW; vpH = sSurfH; }
	if (!enable || yTopSS <= yBottomSS)
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}
	bottom = vpY + (int)((yBottomSS + SS_H) * (long)vpH / (2 * SS_H));
	top    = vpY + (int)((yTopSS    + SS_H) * (long)vpH / (2 * SS_H));
	if (bottom < 0) bottom = 0;
	if (top > sSurfH) top = sSurfH;
	if (top <= bottom) { bottom = 0; top = 1; }
	glEnable(GL_SCISSOR_TEST);
	glScissor(sSurfX + (vpX < 0 ? 0 : vpX), sSurfY + bottom, vpW, top - bottom);
}

static void RefreshViewPortG(void)
{
	ApplyViewport();
}

void initGLRenderer(renderer_t* r)
{
	r->type = GL_RENDERER;
	r->props = 0;

	r->Set3D = Set3DG;
	r->StopRendition = StopRenditionG;
	r->SetTexture = SetTextureG;
	r->RenderEntities = RenderEntitiesG;
	r->UpLoadTextureToGpu = UpLoadTextureToGPUG;
	r->UpLoadEntityToGPU = UpLoadEntityToGPUG;
	r->Set2D = Set2DG;
	r->RenderPlayersBullets = RenderPlayersBulletsG;
	r->RenderString = RenderStringG;
	r->GetColorBuffer = GetColorBufferG;
	r->RenderFXSprites = RenderFXSpritesG;
	r->DrawControls = DrawControlsG;
	r->FreeGPUTexture = FreeGPUTextureG;
	r->FreeGPUBuffer = FreeGPUBufferG;
	r->UploadVerticesToGPU = UploadVerticesToGPUG;
	r->StartCleanFrame = StartCleanFrameG;
	r->RenderColorlessSprites = RenderColorlessSpritesG;
	r->RenderTexturelessSprites = RenderTexturelessSpritesG;
	r->FadeScreen = FadeScreenG;
	r->SetMaterialTextureBlending = SetMaterialTextureBlendingG;
	r->SetTransparency = SetTransparencyG;
	r->IsTextureCompressionSupported = IsTextureCompressionSupportedG;
	r->RefreshViewPort = RefreshViewPortG;
	r->SetScissor = SetScissorG;
}
