#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // shared ambient / shadow / aerial model

// Far-terrain LOD tile shading: same texture atlas as near terrain (tiled once per
// world unit via a planar world-space projection, so the material density matches the
// real chunks it continues), simple sky-ambient + directional sun (far terrain is by
// construction open to the sky: skylight = full). No shadow map, no point lights, no
// baked light — distance makes those invisible anyway.

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in flat uint vTex;
layout(location = 2) in flat uint vFace;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
    float emissiveMultiplier;
    vec3 cameraPosition;    // NOT the live camera in the far passes (camera-relative
                            // rendering) — use cameraWorld below for world-space math
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;
    mat4 biasedLightSpace;
    vec3 cameraWorld;       // TRUE world camera (haze bug: cameraPosition measured from
                            // the ORIGIN here — LOD content washed white 1km+ out)
    int  debugShadowMode;
    float shadowDepthRange;
    vec4  grassDisplacers[16];      // declared only to reach the far-cascade fields below
    vec4  grassDisplacersAux[16];
    ivec4 grassDisplacerMeta;
    mat4 biasedLightSpaceNear;
    vec4 shadowCascadeNear;
    mat4 lightSpaceMatrixNear;
    mat4 biasedLightSpaceFar;       // FAR shadow cascade: the LOD band finally receives
    vec4 shadowCascadeFar;          // x = range end (0 = off), y = far depthRange
} ubo;

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;     // class 0 albedo: 512px
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;   // class 1 albedo: 1024px
layout(set = 0, binding = 10) uniform sampler2D shadowMapFar;         // far shadow cascade

// Must match the AtlasUVBuffer declaration in voxel.frag (same set-0 descriptor set).
layout(std430, set = 0, binding = 4) readonly buffer AtlasUVBuffer {
    uint count512;
    uint fallbackIndex;
    uint count1024;
    uint _pad1;
    vec4 textureUVs[];
} atlasUVs;

layout(location = 0) out vec4 outColor;

// Face IDs: 0=+Z, 1=-Z, 2=+X, 3=-X, 4=+Y, 5=-Y (matches static_voxel.vert).
vec3 faceNormal(uint f) {
    if (f == 0u) return vec3(0, 0, 1);
    if (f == 1u) return vec3(0, 0, -1);
    if (f == 2u) return vec3(1, 0, 0);
    if (f == 3u) return vec3(-1, 0, 0);
    if (f == 4u) return vec3(0, 1, 0);
    return vec3(0, -1, 0);
}

// Planar world-space projection per face axis (same mapping as voxel.frag worldFaceUV):
// integer part = world cell, so the texture tiles once per world unit.
vec2 worldFaceUV(vec3 wp, uint f) {
    if (f >= 4u) return wp.xz;               // top/bottom
    if (f == 2u || f == 3u) return wp.zy;    // +/-X walls
    return wp.xy;                            // +/-Z walls
}

void main() {
    uint cls   = (vTex >> 15) & 1u;
    uint layer = vTex & 0x7FFFu;
    uint count = (cls == 1u) ? atlasUVs.count1024 : atlasUVs.count512;
    bool fb    = (vTex == 0xFFFFu || layer >= count);
    float L    = fb ? float(atlasUVs.fallbackIndex) : float(layer);

    vec2 uv = worldFaceUV(vWorldPos, vFace);
    vec4 albedo = fb || cls == 0u ? texture(textureArray,   vec3(uv, L))
                                  : texture(textureArrayHi, vec3(uv, L));

    // Lighting: the SHARED model (lighting.glsl). Far terrain is open to the sky by
    // construction, so skylight = 1. FAR shadow cascade (2026-08-06): distant hills shade
    // their own valleys and distant forests darken the ground under them — before this the
    // whole LOD band rendered flat-lit past the mid map's 420 u.
    vec3 N = faceNormal(vFace);
    vec3 sunL = normalize(-ubo.sunDirection);
    float ndl = max(dot(N, sunL), 0.0);
    float shadowF = 1.0;
    if (ubo.shadowCascadeFar.x > 0.0)
        shadowF = phxShadowFast(shadowMapFar,
                                ubo.biasedLightSpaceFar *
                                    vec4(vWorldPos - ubo.cameraWorld, 1.0),
                                ubo.shadowCascadeFar.y);
    vec3 color = phxAmbient(N, 1.0, ubo.ambientLight) * albedo.rgb
               + ndl * shadowF * ubo.sunColor * albedo.rgb;
    if (ubo.debugShadowMode == 1) { outColor = phxShadowOnly(shadowF); return; }
    color = phxAerialPerspective(color, vWorldPos - ubo.cameraWorld,
                                 ubo.sunDirection, ubo.sunColor);
    outColor = vec4(color, 1.0);
}
