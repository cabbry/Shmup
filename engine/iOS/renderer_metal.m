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
 *  renderer_metal.m -- v3 stage 3: the Metal backend.
 *
 *  A 1:1 port of renderer_fixed.c's passes onto Metal. The scene-walking
 *  logic (sky domes as a skybox, the boss cameo, the crossing stars, the
 *  live cull and its [cull] probe, the ghost/flicker enemy draws) is kept
 *  verbatim so the two backends can be measured against the same trace while
 *  they coexist; when the OpenGL backend retires, this file is the renderer.
 *
 *  The fixed pipeline, emulated: one light (GL's default material: ambient
 *  0.2, diffuse 0.8, global ambient 0.2, no colour material), GL_LINEAR fog
 *  evaluated per vertex, texture environments REPLACE / MODULATE / ADD, the
 *  two blend modes the game ever used (alpha, additive), depth test + write
 *  as separate switches, back-face culling for ships and enemies only.
 *
 *  Coordinates: the engine's matrices are OpenGL's (column-major, clip z in
 *  [-1,1]); a fixed remap maps clip z to Metal's [0,1] on the way into the
 *  uniforms. Texture rows are uploaded top-down as the loaders produce them --
 *  OpenGL read the same memory the same way (row 0 <-> v=0), so every UV in
 *  the game samples the same texel on both backends.
 */

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <simd/simd.h>

#include "config.h"
#include "renderer_metal.h"
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
#include <math.h>

// ---------------------------------------------------------------------------
//  Uniforms -- ONE struct for every draw, shared with the MSL source below.
// ---------------------------------------------------------------------------
typedef struct
{
	matrix_float4x4 mvp;
	matrix_float4x4 mv;
	matrix_float4x4 normalM;		// inverse-transpose of mv's 3x3, padded
	vector_float4   color;			// glColor
	vector_float4   lightPosEye;
	vector_float4   lightAmbient;
	vector_float4   lightDiffuse;
	vector_float4   lightSpecular;
	vector_float4   matSpecular;	// rgb = material specular, w = shininess
	vector_float4   fogColor;
	vector_float4   params;			// x fogStart, y fogEnd, z constAtt, w linAtt
	vector_int4     flags;			// x lighting, y texEnv (0 REPLACE 1 MODULATE 2 ADD), z fog, w textured
} MtlUniforms;

static const char* kShaderSource =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct U {\n"
"  float4x4 mvp; float4x4 mv; float4x4 normalM;\n"
"  float4 color; float4 lightPosEye; float4 lightAmbient; float4 lightDiffuse; float4 lightSpecular;\n"
"  float4 matSpecular; float4 fogColor; float4 params; int4 flags;\n"
"};\n"
"struct VOut { float4 pos [[position]]; float2 uv; float4 color; float fogF; };\n"
"\n"
"static float4 gl_light(float4 eye, float3 n, constant U& u) {\n"
"  // OpenGL ES 1.1 fixed lighting, one light, default material\n"
"  const float3 matAmb = float3(0.2); const float3 matDif = float3(0.8); const float3 globAmb = float3(0.2);\n"
"  float3 L = u.lightPosEye.xyz - eye.xyz; float d = length(L); L = L / max(d, 1e-5);\n"
"  float att = 1.0 / max(u.params.z + u.params.w * d, 1e-5);\n"
"  float ndl = max(dot(n, L), 0.0);\n"
"  float3 c = globAmb * matAmb + att * (u.lightAmbient.rgb * matAmb + ndl * u.lightDiffuse.rgb * matDif);\n"
"  if (ndl > 0.0) {\n"
"    float3 V = normalize(-eye.xyz); float3 H = normalize(L + V);\n"
"    float s = pow(max(dot(n, H), 0.0), max(u.matSpecular.w, 1e-3));\n"
"    c += att * s * u.lightSpecular.rgb * u.matSpecular.rgb;\n"
"  }\n"
"  return float4(clamp(c, 0.0, 1.0), 1.0);\n"
"}\n"
"static float gl_fog(float4 eye, constant U& u) {\n"
"  if (u.flags.z == 0) return 1.0;\n"
"  float z = -eye.z;\n"
"  return clamp((u.params.y - z) / max(u.params.y - u.params.x, 1e-5), 0.0, 1.0);\n"
"}\n"
"\n"
"// --- 3D: vertex_t (pos float3 @0, normal short3 normalized @12, uv short2 normalized @18)\n"
"struct VIn3D { float3 pos [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; };\n"
"vertex VOut vs3d(VIn3D in [[stage_in]], constant U& u [[buffer(1)]]) {\n"
"  VOut o; float4 p = float4(in.pos, 1.0); float4 eye = u.mv * p;\n"
"  o.pos = u.mvp * p; o.uv = in.uv;\n"
"  if (u.flags.x != 0) { float3 n = normalize((u.normalM * float4(in.normal, 0.0)).xyz); o.color = gl_light(eye, n, u); }\n"
"  else o.color = u.color;\n"
"  o.fogF = gl_fog(eye, u);\n"
"  return o;\n"
"}\n"
"// --- 3D flat: float3 pos @0, uchar4 colour @12 (the crossing stars)\n"
"struct VInStar { float3 pos [[attribute(0)]]; float4 color [[attribute(1)]]; };\n"
"vertex VOut vsStar(VInStar in [[stage_in]], constant U& u [[buffer(1)]]) {\n"
"  VOut o; float4 p = float4(in.pos, 1.0); o.pos = u.mvp * p; o.uv = float2(0.0); o.color = in.color * u.color; o.fogF = 1.0; return o;\n"
"}\n"
"// --- 2D: xf_colorless_sprite_t (pos short2 @0, uv short2 normalized @4)\n"
"struct VIn2D { short2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };\n"
"vertex VOut vs2d(VIn2D in [[stage_in]], constant U& u [[buffer(1)]]) {\n"
"  VOut o; o.pos = u.mvp * float4(float(in.pos.x), float(in.pos.y), 0.0, 1.0); o.uv = in.uv; o.color = u.color; o.fogF = 1.0; return o;\n"
"}\n"
"// --- 2D coloured: xf_sprite_t (pos short2 @0, uv short2 normalized @4, colour uchar4 normalized @8)\n"
"struct VIn2DC { short2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 color [[attribute(2)]]; };\n"
"vertex VOut vs2dc(VIn2DC in [[stage_in]], constant U& u [[buffer(1)]]) {\n"
"  VOut o; o.pos = u.mvp * float4(float(in.pos.x), float(in.pos.y), 0.0, 1.0); o.uv = in.uv; o.color = in.color * u.color; o.fogF = 1.0; return o;\n"
"}\n"
"// --- 2D textureless: xf_textureless_sprite_t (pos short2 @0, colour uchar4 normalized @4)\n"
"struct VIn2DT { short2 pos [[attribute(0)]]; float4 color [[attribute(1)]]; };\n"
"vertex VOut vs2dt(VIn2DT in [[stage_in]], constant U& u [[buffer(1)]]) {\n"
"  VOut o; o.pos = u.mvp * float4(float(in.pos.x), float(in.pos.y), 0.0, 1.0); o.uv = float2(0.0); o.color = in.color * u.color; o.fogF = 1.0; return o;\n"
"}\n"
"\n"
"fragment float4 fsMain(VOut in [[stage_in]], constant U& u [[buffer(1)]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]]) {\n"
"  float4 c = in.color;\n"
"  if (u.flags.w != 0) {\n"
"    float4 t = tex.sample(s, in.uv);\n"
"    if (u.flags.y == 0) c = t;                                                    // GL_REPLACE\n"
"    else if (u.flags.y == 1) c = t * in.color;                                    // GL_MODULATE\n"
"    else c = float4(min(t.rgb + in.color.rgb, float3(1.0)), t.a * in.color.a);    // GL_ADD\n"
"  }\n"
"  if (u.flags.z != 0) c.rgb = mix(u.fogColor.rgb, c.rgb, in.fogF);\n"
"  return c;\n"
"}\n";

// ---------------------------------------------------------------------------
//  Device objects and per-frame state
// ---------------------------------------------------------------------------
static id<MTLDevice>        gDevice;
static id<MTLCommandQueue>  gQueue;
static CAMetalLayer*        gLayer;
static id<MTLLibrary>       gLibrary;
static id<MTLTexture>       gDepthTex;
static id<MTLSamplerState>  gSampler;
static id<MTLDepthStencilState> gDepthStates[4];	// bit0 test, bit1 write

static id<CAMetalDrawable>          gDrawable;
static id<MTLCommandBuffer>         gCmdBuf;
static id<MTLRenderCommandEncoder>  gEncoder;
static int                          gInFrame = 0;

// Resource tables: the engine keeps uint ids, we keep the objects.
static NSMutableArray<id>*  gTextures;		// index = textureId - 1 (NSNull when freed)
static NSMutableArray<id>*  gBuffers;		// index = bufferId  - 1

// Dynamic geometry: a 3-deep ring of shared buffers, bump-allocated per frame.
#define RING_COUNT 3
#define RING_SIZE  (4 * 1024 * 1024)
static id<MTLBuffer> gRing[RING_COUNT];
static int           gRingIndex = 0;
static NSUInteger    gRingOffset = 0;
// One frame in flight per ring slot: BeginFrame waits for the slot the GPU
// finished three frames ago before the CPU writes into it again.
static dispatch_semaphore_t gFrameSem;

// Pipeline cache: vertex kind x blend mode.
enum { VK_3D = 0, VK_STAR, VK_2D, VK_2DC, VK_2DT, VK_COUNT };
enum { BL_NONE = 0, BL_ALPHA, BL_ADD, BL_COUNT };
static id<MTLRenderPipelineState> gPSO[VK_COUNT][BL_COUNT];

// The GL state we emulate.
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

// A stale id would silently draw with someone else's texture: -1 means unset.
static int lastTextureIdM = -1;

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------
static matrix_float4x4 M4(const matrix_t m)
{
	matrix_float4x4 r;
	memcpy(&r, m, sizeof(float) * 16);	// both column-major
	return r;
}

// GL clip z in [-1,1] -> Metal [0,1]: z' = 0.5 z + 0.5 w.
static matrix_float4x4 GLToMetalClip(matrix_float4x4 p)
{
	matrix_float4x4 remap = matrix_identity_float4x4;
	remap.columns[2].z = 0.5f;
	remap.columns[3].z = 0.5f;
	return matrix_multiply(remap, p);
}

static matrix_float4x4 NormalMatrix(matrix_float4x4 mv)
{
	matrix_float3x3 m3 = (matrix_float3x3){ mv.columns[0].xyz, mv.columns[1].xyz, mv.columns[2].xyz };
	matrix_float3x3 n  = simd_transpose(simd_inverse(m3));
	matrix_float4x4 r  = matrix_identity_float4x4;
	r.columns[0] = (vector_float4){ n.columns[0].x, n.columns[0].y, n.columns[0].z, 0 };
	r.columns[1] = (vector_float4){ n.columns[1].x, n.columns[1].y, n.columns[1].z, 0 };
	r.columns[2] = (vector_float4){ n.columns[2].x, n.columns[2].y, n.columns[2].z, 0 };
	return r;
}

static void* RingAlloc(NSUInteger bytes, id<MTLBuffer>* outBuf, NSUInteger* outOffset)
{
	bytes = (bytes + 15) & ~(NSUInteger)15;
	if (gRingOffset + bytes > RING_SIZE)
	{
		// The frame outgrew the ring (never in practice: the whole game's
		// dynamic geometry is a few hundred KB). Fall back to a transient buffer.
		id<MTLBuffer> b = [gDevice newBufferWithLength:bytes options:MTLResourceStorageModeShared];
		*outBuf = b; *outOffset = 0;
		return b.contents;
	}
	*outBuf = gRing[gRingIndex];
	*outOffset = gRingOffset;
	gRingOffset += bytes;
	return (char*)gRing[gRingIndex].contents + *outOffset;
}

static id<MTLTexture> TextureFor(unsigned textureId)
{
	if (textureId == 0 || textureId > gTextures.count)
		return nil;
	id t = gTextures[textureId - 1];
	return (t == [NSNull null]) ? nil : (id<MTLTexture>)t;
}

static id<MTLBuffer> BufferFor(unsigned bufferId)
{
	if (bufferId == 0 || bufferId > gBuffers.count)
		return nil;
	id b = gBuffers[bufferId - 1];
	return (b == [NSNull null]) ? nil : (id<MTLBuffer>)b;
}

static MTLVertexDescriptor* VertexDescriptorFor(int kind)
{
	MTLVertexDescriptor* d = [MTLVertexDescriptor vertexDescriptor];
	switch (kind)
	{
		case VK_3D:
			d.attributes[0].format = MTLVertexFormatFloat3;            d.attributes[0].offset = 0;  d.attributes[0].bufferIndex = 0;
			d.attributes[1].format = MTLVertexFormatShort3Normalized;  d.attributes[1].offset = 12; d.attributes[1].bufferIndex = 0;
			d.attributes[2].format = MTLVertexFormatShort2Normalized;  d.attributes[2].offset = 18; d.attributes[2].bufferIndex = 0;
			d.layouts[0].stride = sizeof(vertex_t);
			break;
		case VK_STAR:
			d.attributes[0].format = MTLVertexFormatFloat3;            d.attributes[0].offset = 0;  d.attributes[0].bufferIndex = 0;
			d.attributes[1].format = MTLVertexFormatUChar4Normalized;  d.attributes[1].offset = 12; d.attributes[1].bufferIndex = 0;
			d.layouts[0].stride = 16;
			break;
		case VK_2D:
			d.attributes[0].format = MTLVertexFormatShort2;            d.attributes[0].offset = 0;  d.attributes[0].bufferIndex = 0;
			d.attributes[1].format = MTLVertexFormatShort2Normalized;  d.attributes[1].offset = 4;  d.attributes[1].bufferIndex = 0;
			d.layouts[0].stride = sizeof(xf_colorless_sprite_t);
			break;
		case VK_2DC:
			d.attributes[0].format = MTLVertexFormatShort2;            d.attributes[0].offset = 0;  d.attributes[0].bufferIndex = 0;
			d.attributes[1].format = MTLVertexFormatShort2Normalized;  d.attributes[1].offset = 4;  d.attributes[1].bufferIndex = 0;
			d.attributes[2].format = MTLVertexFormatUChar4Normalized;  d.attributes[2].offset = 8;  d.attributes[2].bufferIndex = 0;
			d.layouts[0].stride = sizeof(xf_sprite_t);
			break;
		case VK_2DT:
			d.attributes[0].format = MTLVertexFormatShort2;            d.attributes[0].offset = 0;  d.attributes[0].bufferIndex = 0;
			d.attributes[1].format = MTLVertexFormatUChar4Normalized;  d.attributes[1].offset = 4;  d.attributes[1].bufferIndex = 0;
			d.layouts[0].stride = sizeof(xf_textureless_sprite_t);
			break;
	}
	d.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
	return d;
}

static id<MTLRenderPipelineState> PSOFor(int kind, int blend)
{
	if (gPSO[kind][blend])
		return gPSO[kind][blend];

	static const char* vsNames[VK_COUNT] = { "vs3d", "vsStar", "vs2d", "vs2dc", "vs2dt" };
	MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
	pd.vertexFunction   = [gLibrary newFunctionWithName:[NSString stringWithUTF8String:vsNames[kind]]];
	pd.fragmentFunction = [gLibrary newFunctionWithName:@"fsMain"];
	pd.vertexDescriptor = VertexDescriptorFor(kind);
	pd.colorAttachments[0].pixelFormat = gLayer.pixelFormat;
	pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
	if (blend != BL_NONE)
	{
		MTLRenderPipelineColorAttachmentDescriptor* ca = pd.colorAttachments[0];
		ca.blendingEnabled = YES;
		ca.rgbBlendOperation = MTLBlendOperationAdd;
		ca.alphaBlendOperation = MTLBlendOperationAdd;
		ca.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
		ca.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
		if (blend == BL_ALPHA)
		{
			ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;		// GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
			ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		}
		else
		{
			ca.destinationRGBBlendFactor = MTLBlendFactorOne;						// GL_SRC_ALPHA, GL_ONE
			ca.destinationAlphaBlendFactor = MTLBlendFactorOne;
		}
	}
	NSError* err = nil;
	gPSO[kind][blend] = [gDevice newRenderPipelineStateWithDescriptor:pd error:&err];
	if (!gPSO[kind][blend])
		Log_Printf("[Metal] pipeline %d/%d failed: %s\n", kind, blend, err.localizedDescription.UTF8String);
	return gPSO[kind][blend];
}

static void FillUniforms(MtlUniforms* u, int kind)
{
	matrix_float4x4 proj = GLToMetalClip(M4(sProj));
	matrix_float4x4 mv   = M4(sMV);
	u->mvp = matrix_multiply(proj, mv);
	u->mv  = mv;
	u->normalM = (kind == VK_3D && sLighting) ? NormalMatrix(mv) : matrix_identity_float4x4;
	u->color = (vector_float4){ sColor[0], sColor[1], sColor[2], sColor[3] };
	// GL transforms the light position by the modelview current at glLight
	// time -- the view matrix, since SetupLighting runs right after the camera.
	{
		vector_float4 lp = (vector_float4){ light.position[0], light.position[1], light.position[2], light.position[3] };
		u->lightPosEye = matrix_multiply(mv, lp);
	}
	u->lightAmbient  = (vector_float4){ light.ambient[0], light.ambient[1], light.ambient[2], light.ambient[3] };
	u->lightDiffuse  = (vector_float4){ light.diffuse[0], light.diffuse[1], light.diffuse[2], light.diffuse[3] };
	u->lightSpecular = (vector_float4){ light.specula[0], light.specula[1], light.specula[2], light.specula[3] };
	u->matSpecular   = (vector_float4){ 0, 0, 0, 0 };	// filled per entity
	u->fogColor      = (vector_float4){ renderer.fogColor[0], renderer.fogColor[1], renderer.fogColor[2], 1 };
	u->params        = (vector_float4){ (float)renderer.fogStartAt, (float)renderer.fogStopAt, light.constantAttenuation, light.linearAttenuation };
	// "Textured" only if a texture is actually bound: sampling nothing is undefined.
	u->flags         = (vector_int4){ (kind == VK_3D) ? sLighting : 0, sTexEnv, (kind == VK_3D) ? sFog : 0, (sTextured && TextureFor(sTexture) != nil) ? 1 : 0 };
}

// Bind everything a draw needs from the emulated state. Called per draw: the
// encoder may have been restarted mid-frame by a readback, so nothing is
// assumed to persist.
static BOOL BindForDraw(int kind, const MtlUniforms* u)
{
	if (!gEncoder)
		return NO;
	id<MTLRenderPipelineState> pso = PSOFor(kind, sBlend);
	if (!pso)
		return NO;
	[gEncoder setRenderPipelineState:pso];
	[gEncoder setDepthStencilState:gDepthStates[(sDepthTest ? 1 : 0) | (sDepthWrite ? 2 : 0)]];
	[gEncoder setCullMode:(sCullBack && kind == VK_3D) ? MTLCullModeBack : MTLCullModeNone];
	[gEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
	[gEncoder setVertexBytes:u length:sizeof(MtlUniforms) atIndex:1];
	[gEncoder setFragmentBytes:u length:sizeof(MtlUniforms) atIndex:1];
	if (u->flags.w)
	{
		[gEncoder setFragmentTexture:TextureFor(sTexture) atIndex:0];
		[gEncoder setFragmentSamplerState:gSampler atIndex:0];
	}
	return YES;
}

// Indexed draw from client memory (the GL glDrawElements idiom).
static void DrawIndexedClient(int kind, const void* vertices, NSUInteger vertexBytes,
                              const ushort* indices, NSUInteger numIndices, MTLPrimitiveType prim)
{
	MtlUniforms u;
	id<MTLBuffer> vb, ib; NSUInteger vo, io;
	if (numIndices == 0 || !vertices || !indices)
		return;
	FillUniforms(&u, kind);
	if (!BindForDraw(kind, &u))
		return;
	memcpy(RingAlloc(vertexBytes, &vb, &vo), vertices, vertexBytes);
	memcpy(RingAlloc(numIndices * sizeof(ushort), &ib, &io), indices, numIndices * sizeof(ushort));
	[gEncoder setVertexBuffer:vb offset:vo atIndex:0];
	[gEncoder drawIndexedPrimitives:prim indexCount:numIndices indexType:MTLIndexTypeUInt16 indexBuffer:ib indexBufferOffset:io];
}

static void DrawArraysClient(int kind, const void* vertices, NSUInteger vertexBytes, NSUInteger numVertices, MTLPrimitiveType prim)
{
	MtlUniforms u;
	id<MTLBuffer> vb; NSUInteger vo;
	if (numVertices == 0 || !vertices)
		return;
	FillUniforms(&u, kind);
	if (!BindForDraw(kind, &u))
		return;
	memcpy(RingAlloc(vertexBytes, &vb, &vo), vertices, vertexBytes);
	[gEncoder setVertexBuffer:vb offset:vo atIndex:0];
	[gEncoder drawPrimitives:prim vertexStart:0 vertexCount:numVertices];
}

// ---------------------------------------------------------------------------
//  Frame: render pass management
// ---------------------------------------------------------------------------
static void BeginEncoder(BOOL clear)
{
	MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = gDrawable.texture;
	rp.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	if (engine.fogEnabled)
		rp.colorAttachments[0].clearColor = MTLClearColorMake(renderer.fogColor[0], renderer.fogColor[1], renderer.fogColor[2], 1.0);
	else
		rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1.0);
	rp.depthAttachment.texture = gDepthTex;
	rp.depthAttachment.loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
	rp.depthAttachment.storeAction = MTLStoreActionStore;
	rp.depthAttachment.clearDepth = 1.0;
	gEncoder = [gCmdBuf renderCommandEncoderWithDescriptor:rp];

	// The viewport the engine computed (letterboxing lives in renderer.c).
	// GL's origin is bottom-left, Metal's top-left: flip y.
	{
		MTLViewport vp;
		vp.originX = renderer.viewPortDimensions[VP_X];
		vp.originY = sPixelH - renderer.viewPortDimensions[VP_Y] - renderer.viewPortDimensions[VP_HEIGHT];
		vp.width   = renderer.viewPortDimensions[VP_WIDTH];
		vp.height  = renderer.viewPortDimensions[VP_HEIGHT];
		vp.znear = 0.0; vp.zfar = 1.0;
		if (vp.width <= 0 || vp.height <= 0) { vp.originX = 0; vp.originY = 0; vp.width = sPixelW; vp.height = sPixelH; }
		[gEncoder setViewport:vp];
	}
}

static void EndEncoder(void)
{
	if (gEncoder)
	{
		[gEncoder endEncoding];
		gEncoder = nil;
	}
}

// Read back a rectangle of the CURRENT frame, GL-style coordinates (origin
// bottom-left, y up) and GL_RGBA byte order. Ends the pass, waits for the GPU,
// then reopens the pass with LOAD so drawing continues where it stopped. Only
// the CI probe and screenshots pay for this.
static void ReadPixelsGL(int x, int y, int w, int h, uchar* out)
{
	if (!gInFrame || !gDrawable)
	{
		memset(out, 0, (size_t)w * h * 4);
		return;
	}
	EndEncoder();

	// Clip to the drawable.
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x + w > sPixelW) w = sPixelW - x;
	if (y + h > sPixelH) h = sPixelH - y;
	if (w <= 0 || h <= 0) { memset(out, 0, 0); return; }

	int metalY = sPixelH - y - h;	// GL y up -> Metal rows top-down
	id<MTLBuffer> staging = [gDevice newBufferWithLength:(NSUInteger)w * h * 4 options:MTLResourceStorageModeShared];
	id<MTLBlitCommandEncoder> blit = [gCmdBuf blitCommandEncoder];
	[blit copyFromTexture:gDrawable.texture sourceSlice:0 sourceLevel:0
	         sourceOrigin:MTLOriginMake(x, metalY, 0) sourceSize:MTLSizeMake(w, h, 1)
	             toBuffer:staging destinationOffset:0 destinationBytesPerRow:(NSUInteger)w * 4 destinationBytesPerImage:(NSUInteger)w * h * 4];
	[blit endEncoding];
	[gCmdBuf commit];
	[gCmdBuf waitUntilCompleted];

	// BGRA rows top-down -> RGBA rows bottom-up (what glReadPixels returned).
	{
		const uchar* src = staging.contents;
		int row, col;
		for (row = 0; row < h; row++)
		{
			const uchar* s = src + (size_t)row * w * 4;
			uchar* d = out + (size_t)(h - 1 - row) * w * 4;
			for (col = 0; col < w; col++, s += 4, d += 4)
			{
				d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
			}
		}
	}

	// Resume the frame on a fresh command buffer, loading what was drawn.
	gCmdBuf = [gQueue commandBuffer];
	BeginEncoder(NO);
}

int MTL_Create(void* caMetalLayer, int pixelWidth, int pixelHeight)
{
	CAMetalLayer* layer = (__bridge CAMetalLayer*)caMetalLayer;
	NSError* err = nil;
	int i;

	gDevice = MTLCreateSystemDefaultDevice();
	if (!gDevice)
	{
		Log_Printf("[Metal] no device.\n");
		return 0;
	}
	gQueue = [gDevice newCommandQueue];
	gLayer = layer;
	gLayer.device = gDevice;
	gLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	gLayer.framebufferOnly = NO;			// the [cull] probe and screenshots read the drawable back
	gLayer.opaque = YES;

	gLibrary = [gDevice newLibraryWithSource:[NSString stringWithUTF8String:kShaderSource] options:nil error:&err];
	if (!gLibrary)
	{
		Log_Printf("[Metal] shader compile failed: %s\n", err.localizedDescription.UTF8String);
		return 0;
	}

	{
		MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
		sd.minFilter = MTLSamplerMinMagFilterLinear;			// GL_LINEAR_MIPMAP_NEAREST
		sd.magFilter = MTLSamplerMinMagFilterLinear;			// GL_LINEAR
		sd.mipFilter = MTLSamplerMipFilterNearest;
		sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
		sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
		gSampler = [gDevice newSamplerStateWithDescriptor:sd];
	}
	for (i = 0; i < 4; i++)
	{
		MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
		dd.depthCompareFunction = (i & 1) ? MTLCompareFunctionLess : MTLCompareFunctionAlways;
		dd.depthWriteEnabled = (i & 2) ? YES : NO;
		gDepthStates[i] = [gDevice newDepthStencilStateWithDescriptor:dd];
	}
	gFrameSem = dispatch_semaphore_create(RING_COUNT);
	for (i = 0; i < RING_COUNT; i++)
		gRing[i] = [gDevice newBufferWithLength:RING_SIZE options:MTLResourceStorageModeShared];

	gTextures = [NSMutableArray new];
	gBuffers  = [NSMutableArray new];

	MTL_Resize(pixelWidth, pixelHeight);
	Log_Printf("[Metal] backend up on %s, %dx%d.\n", gDevice.name.UTF8String, pixelWidth, pixelHeight);
	return 1;
}

void MTL_Resize(int pixelWidth, int pixelHeight)
{
	if (pixelWidth <= 0 || pixelHeight <= 0)
		return;
	sPixelW = pixelWidth;
	sPixelH = pixelHeight;
	gLayer.drawableSize = CGSizeMake(pixelWidth, pixelHeight);

	MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
	                                                                              width:pixelWidth height:pixelHeight mipmapped:NO];
	td.usage = MTLTextureUsageRenderTarget;
	td.storageMode = MTLStorageModePrivate;
	gDepthTex = [gDevice newTextureWithDescriptor:td];
}

void MTL_BeginFrame(void)
{
	if (gInFrame)
		return;
	dispatch_semaphore_wait(gFrameSem, DISPATCH_TIME_FOREVER);
	gDrawable = [gLayer nextDrawable];
	if (!gDrawable)
	{
		dispatch_semaphore_signal(gFrameSem);
		return;
	}
	gRingIndex = (gRingIndex + 1) % RING_COUNT;
	gRingOffset = 0;
	gCmdBuf = [gQueue commandBuffer];
	gInFrame = 1;
	BeginEncoder(YES);
}

void MTL_EndFrame(void)
{
	if (!gInFrame)
		return;
	EndEncoder();
	[gCmdBuf presentDrawable:gDrawable];
	[gCmdBuf addCompletedHandler:^(id<MTLCommandBuffer> cb) { (void)cb; dispatch_semaphore_signal(gFrameSem); }];
	[gCmdBuf commit];
	gDrawable = nil;
	gCmdBuf = nil;
	gInFrame = 0;
}

// ---------------------------------------------------------------------------
//  The renderer_t entry points
// ---------------------------------------------------------------------------
static void Set3DM(void)
{
	// The clear happened at BeginFrame (Metal clears on pass creation); the
	// colour was chosen the same way Set3DF chose it: fog or black.
	sDepthTest = 1; sDepthWrite = 1;
	sBlend = BL_NONE; renderer.isBlending = 0;
	sLighting = light.enabled ? 1 : 0;
	sTextured = 1;
	sTexEnv = 1;			// GL_MODULATE
	sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
	sFog = 0;
	sCullBack = 0;
}

static void Set2DM(void)
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
	for (i = 0; i < 16; i++) sMV[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	sCullBack = 0;
	sFog = 0;
	sLighting = 0;
	sDepthTest = 0; sDepthWrite = 0;
	sTextured = 1;
}

static void StopRenditionM(void)
{
	lastTextureIdM = -1;
}

static void SetTextureM(unsigned int textureId)
{
	if ((int)textureId == lastTextureIdM)
		return;
	sTexture = textureId;
	STATS_AddTexSwitch();
	lastTextureIdM = (int)textureId;
}

static void UpLoadTextureToGPUM(texture_t* texture)
{
	id<MTLTexture> tex = nil;
	int i;

	if (!texture || !texture->data || texture->textureId != 0)
		return;

	if (texture->format == TEXTURE_GL_RGB || texture->format == TEXTURE_GL_RGBA)
	{
		// The loaders hand us 4 bytes per pixel either way (CGBitmapContext with
		// kCGImageAlphaNoneSkipLast for RGB). Metal has no RGB8: upload RGBA8,
		// and for RGB force the skipped byte to opaque.
		MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
		                                                                              width:texture->width height:texture->height mipmapped:YES];
		td.usage = MTLTextureUsageShaderRead;
		tex = [gDevice newTextureWithDescriptor:td];
		if (texture->format == TEXTURE_GL_RGB)
		{
			uchar* px = texture->data[0];
			size_t n = (size_t)texture->width * texture->height;
			size_t k;
			for (k = 0; k < n; k++)
				px[k * 4 + 3] = 255;
		}
		[tex replaceRegion:MTLRegionMake2D(0, 0, texture->width, texture->height) mipmapLevel:0
		         withBytes:texture->data[0] bytesPerRow:(NSUInteger)texture->width * 4];
		free(texture->data[0]);
		texture->data[0] = 0;

		// GL_GENERATE_MIPMAP: build the chain now, on the GPU.
		{
			id<MTLCommandBuffer> cb = [gQueue commandBuffer];
			id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
			[blit generateMipmapsForTexture:tex];
			[blit endEncoding];
			[cb commit];
		}
	}
	else
	{
		// PVRTC, mip chain supplied by the file. GL enums -> Metal formats.
		MTLPixelFormat pf;
		switch (texture->format)
		{
			case 0x8C00: pf = MTLPixelFormatPVRTC_RGB_4BPP;  break;	// GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG
			case 0x8C01: pf = MTLPixelFormatPVRTC_RGB_2BPP;  break;
			case 0x8C02: pf = MTLPixelFormatPVRTC_RGBA_4BPP; break;
			case 0x8C03: pf = MTLPixelFormatPVRTC_RGBA_2BPP; break;
			default:
				Log_Printf("[Metal] unknown texture format 0x%x for %s\n", texture->format, texture->path);
				return;
		}
		{
			int levels = texture->numMipmaps > 0 ? texture->numMipmaps : 1;
			MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf
			                                                                              width:texture->width height:texture->height mipmapped:(levels > 1)];
			td.mipmapLevelCount = levels;
			td.usage = MTLTextureUsageShaderRead;
			tex = [gDevice newTextureWithDescriptor:td];
			{
				uint w = texture->width, h = texture->height;
				for (i = 0; i < levels; i++)
				{
					if (w < 1) w = 1;
					if (h < 1) h = 1;
					[tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:i withBytes:texture->data[i] bytesPerRow:0];
					free(texture->data[i]);
					texture->data[i] = 0;
					w /= 2; h /= 2;
				}
			}
		}
	}

	[gTextures addObject:tex];
	texture->textureId = (uint)gTextures.count;	// 1-based

	free(texture->dataLength);
	texture->dataLength = 0;
	free(texture->data);
	texture->data = 0;
	texture->memLocation = TEXT_MEM_LOC_VRAM;
	if (texture->file != NULL)
		FS_CloseFile(texture->file);
}

static void FreeGPUTextureM(texture_t* texture)
{
	if (texture->textureId > 0 && texture->textureId <= gTextures.count)
		gTextures[texture->textureId - 1] = [NSNull null];
	texture->textureId = 0;
}

static uint UploadVerticesToGPUM(void* vertices, uint mem_size)
{
	id<MTLBuffer> b = [gDevice newBufferWithBytes:vertices length:mem_size options:MTLResourceStorageModeShared];
	[gBuffers addObject:b];
	return (uint)gBuffers.count;
}

static void FreeGPUBufferM(uint bufferId)
{
	if (bufferId > 0 && bufferId <= gBuffers.count)
		gBuffers[bufferId - 1] = [NSNull null];
}

static void UpLoadEntityToGPUM(entity_t* entity)
{
	md5_mesh_t* mesh;
	if (entity == NULL || entity->model == NULL)
		return;
	if (entity->model->memLocation == MD5_MEMLOC_VRAM)
		return;
	mesh = entity->model;
	mesh->vboId = UploadVerticesToGPUM(mesh->vertexArray, mesh->numVertices * sizeof(vertex_t));
#ifndef GENERATE_VIDEO
	free(mesh->vertexArray);
	mesh->vertexArray = 0;
#endif
	mesh->memLocation = MD5_MEMLOC_VRAM;
}

// RenderEntityF, on Metal: the entity's matrix, its material's texture and
// specular, the model's vertex buffer, and the index list the fixed renderer
// would have used (baked subset on a 2:3 screen, full mesh otherwise).
static void RenderEntityM(entity_t* entity)
{
	MtlUniforms u;
	matrix_t mv;
	id<MTLBuffer> vb = nil; NSUInteger vo = 0;
	const ushort* idx; NSUInteger nidx;
	id<MTLBuffer> ib; NSUInteger io;

	if (!entity || !entity->model)
		return;

	// glPushMatrix; glMultMatrixf(entity->matrix)
	matrix_multiply(sMV, entity->matrix, mv);
	{
		matrix_t savedMV;
		memcpy(savedMV, sMV, sizeof(matrix_t));
		memcpy(sMV, mv, sizeof(matrix_t));
		FillUniforms(&u, VK_3D);
		memcpy(sMV, savedMV, sizeof(matrix_t));
	}
	if (entity->material)
	{
		u.matSpecular = (vector_float4){ entity->material->specularColor[0], entity->material->specularColor[1],
		                                 entity->material->specularColor[2], entity->material->shininess };
		if (sTextured)
			SetTextureM(entity->material->textures[TEXTURE_DIFFUSE].textureId);	// through the cache: the 2D passes compare against it
	}
	if (!BindForDraw(VK_3D, &u))
		return;

	if (entity->model->memLocation == MD5_MEMLOC_VRAM)
	{
		vb = BufferFor(entity->model->vboId);
		if (!vb)
			return;
	}
	else
	{
		NSUInteger bytes = entity->model->numVertices * sizeof(vertex_t);
		memcpy(RingAlloc(bytes, &vb, &vo), entity->model->vertexArray, bytes);
	}
	[gEncoder setVertexBuffer:vb offset:vo atIndex:0];

	if (entity->usage == ENT_PARTIAL_DRAW && renderer.vScale <= 1.0f && !gRuntimeCullMap)
	{
		idx = entity->indices; nidx = entity->numIndices;
	}
	else
	{
		idx = entity->model->indices; nidx = entity->model->numIndices;
	}
	if (nidx == 0 || !idx)
		return;
	memcpy(RingAlloc(nidx * sizeof(ushort), &ib, &io), idx, nidx * sizeof(ushort));
	[gEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:nidx indexType:MTLIndexTypeUInt16 indexBuffer:ib indexBufferOffset:io];
	STATS_AddTriangles((int)nidx / 3);
}

static void SetupCameraM(void)
{
	vec3_t vLookat;
	vectorAdd(camera.position, camera.forward, vLookat);
	gluLookAt(camera.position, vLookat, camera.up, sMV);
}

#define CAMEO_T0		50000
#define CAMEO_T1		64000
#define CAMEO_SCALE		6.0f
#define CAMEO_DEPTH		1600.0f

// Verbatim port of RenderTTBBossCameoF: see renderer_fixed.c for the story.
static void RenderTTBBossCameoM(void)
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
		RenderEntityM(&cameo);
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

// Verbatim port of RenderTTBStarsF.
static void RenderTTBStarsM(void)
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
	DrawArraysClient(VK_STAR, v, sizeof(ttbstar_vertex_t) * nV, nV, MTLPrimitiveTypeTriangle);
	STATS_AddTriangles(nV / 3);
	sTextured = 1;
	sBlend = BL_NONE;
}

// RenderEntitiesF, on Metal. The scene walk is the fixed renderer's, line for
// line; only the GL calls became state flips and RenderEntityM draws.
static void RenderEntitiesM(void)
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
	SetupCameraM();

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

			RenderEntityM(entity);

			if (gRuntimeCullMap)
			{
				entity->matrix[12] = savedTx;
				entity->matrix[14] = savedTz;
			}
		}
	}

	RenderTTBBossCameoM();
	if (gRuntimeCullMap)
		RenderTTBStarsM();

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

		RenderEntityM(entity);
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

	sFog = 0;			// GL left fog enabled for ships too? No: the fixed path keeps
						// GL_FOG on through the players/enemies -- mirror that.
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
			RenderEntityM(&players[i].entity);
			sBlend = BL_NONE;
			sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
			sTexEnv = 0;			// GL_REPLACE
		}
		else
			RenderEntityM(&players[i].entity);
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
			RenderEntityM(entity);
			enemy->shouldFlicker = 0;
			sTexEnv = 1;
		}
		else
		{
			int ghost = entity->color[A] < 0.999f;
			if (ghost)
				sBlend = BL_ALPHA;
			sColor[0] = entity->color[R]; sColor[1] = entity->color[G]; sColor[2] = entity->color[B]; sColor[3] = entity->color[A];
			RenderEntityM(entity);
			if (ghost)
				sBlend = BL_NONE;
		}
		enemy = enemy->next;
	}
	sColor[0] = sColor[1] = sColor[2] = sColor[3] = 1;
	sFog = 0;
}

static void RenderStringM(xf_colorless_sprite_t* vertices, ushort* indices, uint numIndices)
{
	// Text vertices are laid out sequentially; the highest index tells how many.
	uint i, maxIdx = 0;
	for (i = 0; i < numIndices; i++)
		if (indices[i] > maxIdx) maxIdx = indices[i];
	DrawIndexedClient(VK_2D, vertices, (NSUInteger)(maxIdx + 1) * sizeof(xf_colorless_sprite_t), indices, numIndices, MTLPrimitiveTypeTriangle);
	STATS_AddTriangles((int)numIndices / 3);
}

static void GetColorBufferM(uchar* data)
{
	ReadPixelsGL(0, 0, renderer.glBuffersDimensions[WIDTH], renderer.glBuffersDimensions[HEIGHT], data);
}

static void RenderColorlessSpritesM(xf_colorless_sprite_t* vertices, ushort numIndices, ushort* indices)
{
	uint i, maxIdx = 0;
	for (i = 0; i < numIndices; i++)
		if (indices[i] > maxIdx) maxIdx = indices[i];
	DrawIndexedClient(VK_2D, vertices, (NSUInteger)(maxIdx + 1) * sizeof(xf_colorless_sprite_t), indices, numIndices, MTLPrimitiveTypeTriangle);
	STATS_AddTriangles(numIndices / 2);
}

static void RenderColoredSprites(xf_sprite_t* vertices, int numIndices, ushort* indices)
{
	int i, maxIdx = 0;
	for (i = 0; i < numIndices; i++)
		if (indices[i] > maxIdx) maxIdx = indices[i];
	DrawIndexedClient(VK_2DC, vertices, (NSUInteger)(maxIdx + 1) * sizeof(xf_sprite_t), indices, numIndices, MTLPrimitiveTypeTriangle);
	STATS_AddTriangles(numIndices / 3);
}

static void RenderTexturelessSpritesM(xf_textureless_sprite_t* vertices, ushort numIndices, ushort* indices)
{
	uint i, maxIdx = 0;
	for (i = 0; i < numIndices; i++)
		if (indices[i] > maxIdx) maxIdx = indices[i];
	sTextured = 0;
	DrawIndexedClient(VK_2DT, vertices, (NSUInteger)(maxIdx + 1) * sizeof(xf_textureless_sprite_t), indices, numIndices, MTLPrimitiveTypeTriangle);
	sTextured = 1;
	STATS_AddTriangles(numIndices / 3);
}

static void RenderPlayersBulletsM(void)
{
	sBlend = BL_ADD;			// GL_SRC_ALPHA, GL_ONE
	sTexEnv = 0;				// GL_REPLACE
	SetTextureM(bulletConfig.bulletTexture.textureId);
	{
		int i, maxIdx = 0;
		for (i = 0; i < numPBulletsIndices; i++)
			if (bulletIndices[i] > maxIdx) maxIdx = bulletIndices[i];
		DrawIndexedClient(VK_2D, pBulletVertices, (NSUInteger)(maxIdx + 1) * sizeof(xf_colorless_sprite_t), bulletIndices, numPBulletsIndices, MTLPrimitiveTypeTriangle);
		STATS_AddTriangles(numPBulletsIndices/3);
	}
	{
		int i, maxIdx = 0;
		for (i = 0; i < partLib.num_indices; i++)
			if (partLib.indices[i] > maxIdx) maxIdx = partLib.indices[i];
		DrawIndexedClient(VK_2D, partLib.ss_vertices, (NSUInteger)(maxIdx + 1) * sizeof(xf_colorless_sprite_t), partLib.indices, partLib.num_indices, MTLPrimitiveTypeTriangle);
		STATS_AddTriangles(partLib.num_indices/3);
	}
}

static void RenderFXSpritesM(void)
{
	int i, j;

	sBlend = BL_ADD;			// GL_SRC_ALPHA, GL_ONE (smoke, ghosts, particles)
	SetTextureM(smokeTexture.textureId);
	if (numSmokeIndices != 0)
	{
		int maxIdx = 0;
		for (i = 0; i < numSmokeIndices; i++)
			if (smokeIndices[i] > maxIdx) maxIdx = smokeIndices[i];
		DrawIndexedClient(VK_2D, smokeVertices, (NSUInteger)(maxIdx + 1) * sizeof(xf_colorless_sprite_t), smokeIndices, numSmokeIndices, MTLPrimitiveTypeTriangle);
		STATS_AddTriangles(numSmokeIndices/3);
	}

	SetTextureM(ghostTexture.textureId);
	for(i=0 ; i <numPlayers ; i++)
	{
		for (j=0; j< GHOSTS_NUM; j++)
		{
			if (players[i].ghosts[j].timeCounter >= GHOST_TTL_MS)
				continue;
			DrawArraysClient(VK_2D, &players[i].ghosts[j].wayPoints[players[i].ghosts[j].startVertexArray],
			                 (NSUInteger)players[i].ghosts[j].lengthVertexArray * sizeof(xf_colorless_sprite_t),
			                 players[i].ghosts[j].lengthVertexArray, MTLPrimitiveTypeTriangleStrip);
			STATS_AddTriangles((players[i].ghosts[j].lengthVertexArray/2));
		}
	}

	sTexEnv = 1;				// GL_MODULATE
	if (numParticulesIndices != 0)
	{
		SetTextureM(bulletConfig.bulletTexture.textureId);
		RenderColoredSprites(particuleVertices, numParticulesIndices, particuleIndices);
	}

	sBlend = BL_ALPHA;			// GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
	if (numExplosionIndices != 0)
	{
		SetTextureM(explosionTexture.textureId);
		RenderColoredSprites(explosionVertices, numExplosionIndices, explosionIndices);
	}

	SetTextureM(bulletConfig.bulletTexture.textureId);
	RenderColoredSprites(enFxLib.ss_vertices, enFxLib.num_indices, enFxLib.indices);
}

static void DrawControlsM(void)
{
	if (engine.controlMode == CONTROL_MODE_SWIP)
		return;
	sTextured = 0;
	{
		int i, maxIdx = 0;
		for (i = 0; i < controlNumIndices; i++)
			if (controlIndices[i] > maxIdx) maxIdx = controlIndices[i];
		DrawIndexedClient(VK_2DT, controlVertices, (NSUInteger)(maxIdx + 1) * sizeof(xf_textureless_sprite_t), controlIndices, controlNumIndices, MTLPrimitiveTypeTriangleStrip);
		STATS_AddTriangles(controlNumIndices/2);
	}
	sTextured = 1;
}

static void StartCleanFrameM(void)
{
	// Nothing to do: the texture matrix is the Short2Normalized attribute, and
	// the clear belongs to the pass.
}

static void FadeScreenM(float alpha)
{
	fadeVertices[0].color[A] = alpha * 255;
	fadeVertices[1].color[A] = alpha * 255;
	fadeVertices[2].color[A] = alpha * 255;
	fadeVertices[3].color[A] = alpha * 255;
	sTextured = 0;
	DrawIndexedClient(VK_2DT, fadeVertices, sizeof(fadeVertices), fadeIndices, 6, MTLPrimitiveTypeTriangle);
	sTextured = 1;
	STATS_AddTriangles(6/2);
}

static void SetMaterialTextureBlendingM(char modulate)
{
	sTexEnv = modulate ? 1 : 0;
}

static void SetTransparencyM(float alpha)
{
	sColor[0] = sColor[1] = sColor[2] = 1;
	sColor[3] = alpha;
}

static int IsTextureCompressionSupportedM(int type)
{
	return (TEXTURE_FORMAT_PVRTC & type) ? TEXTURE_FORMAT_PVRTC : 0;
}

static void RefreshViewPortM(void)
{
	// The viewport is applied per render pass from renderer.viewPortDimensions.
}

void initMetalRenderer(renderer_t* r)
{
	r->type = METAL_RENDERER;
	r->props = 0;

	r->Set3D = Set3DM;
	r->StopRendition = StopRenditionM;
	r->SetTexture = SetTextureM;
	r->RenderEntities = RenderEntitiesM;
	r->UpLoadTextureToGpu = UpLoadTextureToGPUM;
	r->UpLoadEntityToGPU = UpLoadEntityToGPUM;
	r->Set2D = Set2DM;
	r->RenderPlayersBullets = RenderPlayersBulletsM;
	r->RenderString = RenderStringM;
	r->GetColorBuffer = GetColorBufferM;
	r->RenderFXSprites = RenderFXSpritesM;
	r->DrawControls = DrawControlsM;
	r->FreeGPUTexture = FreeGPUTextureM;
	r->FreeGPUBuffer = FreeGPUBufferM;
	r->UploadVerticesToGPU = UploadVerticesToGPUM;
	r->StartCleanFrame = StartCleanFrameM;
	r->RenderColorlessSprites = RenderColorlessSpritesM;
	r->RenderTexturelessSprites = RenderTexturelessSpritesM;
	r->FadeScreen = FadeScreenM;
	r->SetMaterialTextureBlending = SetMaterialTextureBlendingM;
	r->SetTransparency = SetTransparencyM;
	r->IsTextureCompressionSupported = IsTextureCompressionSupportedM;
	r->RefreshViewPort = RefreshViewPortM;
}
