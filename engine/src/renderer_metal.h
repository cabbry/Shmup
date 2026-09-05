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
 *  renderer_metal.h -- v3: the Metal backend behind the renderer_t table.
 *
 *  Same 24 entry points the OpenGL ES 1.1 backend binds, implemented on
 *  Metal (engine/iOS/renderer_metal.m). The fixed pipeline is emulated in
 *  two small shaders: one light, GL_LINEAR fog, REPLACE/MODULATE/ADD texture
 *  environments, Gouraud lighting with the GL default material -- exactly
 *  what the 2010 renderer asked of the GPU, nothing more, so the two backends
 *  can be held to the same probes (the [cull] luma trace) during the switch.
 *
 *  Frame hooks for the view (EAGLView.m drives both backends):
 *    MTL_Create(layer, w, h)   once, after the CAMetalLayer exists
 *    MTL_Resize(w, h)          on layout
 *    MTL_BeginFrame()          then dEngine_HostFrame(), then
 *    MTL_EndFrame()            present + commit
 */

#ifndef DE_RENDERER_METAL
#define DE_RENDERER_METAL

#include "renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

void initMetalRenderer(renderer_t* renderer);

int  MTL_Create(void* caMetalLayer, int pixelWidth, int pixelHeight);
void MTL_Resize(int pixelWidth, int pixelHeight);
void MTL_BeginFrame(void);
void MTL_EndFrame(void);

#ifdef __cplusplus
}
#endif

#endif
