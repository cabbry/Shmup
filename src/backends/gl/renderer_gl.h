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
 *  renderer_gl.h -- the desktop OpenGL backend (round 87, the Windows port).
 *
 *  A 1:1 port of renderer_metal.m onto OpenGL 2.1 + GLSL 1.20: the same
 *  passes, the same emulated fixed pipeline, the same probes. The platform
 *  owns the window and the GL context; it hands this file a way to resolve
 *  GL entry points (opengl32.dll on Windows only exports 1.1) and calls the
 *  frame brackets around dEngine_HostFrame.
 */
#ifndef DE_RENDERER_GL
#define DE_RENDERER_GL

#include "renderer.h"

#define GL_RENDERER 3	// renderer_t.type for this backend (0/1 were GL ES, 2 is Metal)

typedef void* (*glr_getproc_t)(const char* name);

// Loads the GL 2.x entry points through getProc (falls back to the platform's
// static exports when getProc answers NULL), builds the programs. Requires a
// current context. Returns 0 on failure (no GL 2.0 shaders).
int  GLR_Create(glr_getproc_t getProc, int pixelWidth, int pixelHeight);
void GLR_Resize(int pixelWidth, int pixelHeight);

// The engine's surface inside the window (round 89): the game keeps a
// portrait surface of (w, h) pixels whose bottom-left corner sits at (x, y)
// in GL window coordinates; the rest of the window is the black bands.
// Everything the backend does in window pixels -- viewport, scissor,
// readback -- is offset by it. Default: the whole window.
void GLR_SetSurface(int x, int y, int w, int h);

// A rectangle outline in SURFACE pixels (origin bottom-left), drawn on top of
// the frame: the keyboard's cursor on the menus. Fixed pipeline, no program.
void GLR_DrawRectOutline(float x, float y, float w, float h, float r, float g, float b, float a, float width);

// The frame brackets: clear (fog colour or black, depth 1) and the viewport;
// EndFrame is the platform's SwapBuffers, nothing to do here.
void GLR_BeginFrame(void);
void GLR_EndFrame(void);

void initGLRenderer(renderer_t* renderer);

#endif
