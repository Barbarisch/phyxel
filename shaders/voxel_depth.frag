#version 450
#extension GL_GOOGLE_include_directive : require

// voxel_depth.frag — DEPTH PREPASS for static voxel faces (docs/PerfProgram2026-09.md, P-DP).
//
// Runs with the same static_voxel.vert, the same instance ranges and the same raster state as the
// shaded pass, with colour writes OFF and depth writes ON. The shaded pass (voxel.frag) then runs
// with depth writes OFF and an or-equal test, so each pixel is shaded only by its front-most
// fragment, instead of by every fragment that happened to arrive while it was still in front.
//
// Its ONLY job is to decide which fragments exist, exactly as voxel.frag does: a face skipped by its
// flags (transparent -> OIT pass, mirror -> mirror pass) or a cutout texel (alpha < PHX_CUTOUT_ALPHA)
// must NOT write depth, or it would hide what lies behind it. Both shaders get that decision from
// voxel_surface.glsl, so it cannot drift.

layout(location = 0) in flat uint textureIndex;
layout(location = 1) in vec2 texCoord;
layout(location = 3) in flat uint flags;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec3 inWorldPos;
layout(location = 10) in flat vec3 vChunkBaseAbs;
layout(location = 11) in flat vec3 vChunkBaseRel;

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;     // class 0 albedo: 512px
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;   // class 1 albedo: 1024px

layout(std430, set = 0, binding = 4) readonly buffer AtlasUVBuffer {
    uint count512;
    uint fallbackIndex;
    uint count1024;
    uint _pad1;
    vec4 textureUVs[];
} atlasUVs;

#include "voxel_world.glsl"
#include "voxel_surface.glsl"

void main() {
    if (phxStaticFaceSkippedByFlags(flags)) discard;

    bool varied = ((flags >> 15u) & 1u) != 0u;
    vec3 worldPosAbs = phxWorldPosAbs(vChunkBaseAbs, inWorldPos, vChunkBaseRel);
    vec2 suv, gx, gy;
    int  rotStep;
    bool flipped;
    phxVoxelSampleCoords(texCoord, varied, worldPosAbs, inNormal, suv, gx, gy, rotStep, flipped);
    if (phxSampleVoxelAlbedoGrad(textureIndex, suv, gx, gy).a < PHX_CUTOUT_ALPHA) discard;
    // No colour output: the pipeline's colour write mask is 0. Depth comes from the rasteriser.
}
