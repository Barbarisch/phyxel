#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // shared ambient / shadow / aerial model

// VOXEL-FOUNDATION LEAF MASS (docs/VoxelFoundationPlan.md F2.14f). The coarse leaf mass that leaf sprigs hand off to
// (F2.14c) is drawn with its OWN pipeline (voxfound_face[_mdi].vert + this) and shaded EXACTLY as foliage.frag shades a
// leaf card -- the card texture is a cutout sprite, and drawing it on solid faces through voxel.frag gave a darker,
// tiled canopy (F2.14c, top_down luma 108 -> 75). Its own pipeline, not a branch in voxel.frag: that branch measured
// ~+0.5-1 ms on every preview fragment at the ground poses (register pressure on the shared shader; F2.14f).
// Albedo = the leaf texture's opaque average (16 taps at mip 1, the texels the cards' alpha cutout keeps); light =
// probe ambient as an up-facing receiver + sun 0.7 x shadow x sun gate (no N.L: a card has no meaningful normal) +
// backlit transmission + lamps, times the mean card shade 0.91 (foliage.vert 0.82 + 0.18 h); no aerial haze (cards
// have none). Keeps voxel.frag's LOD discard (F2.14) first.

layout(location = 0) in flat uint textureIndex;
layout(location = 2) in vec4 shadowCoord;
layout(location = 3) in flat uint flags;
layout(location = 5) in vec3 inWorldPos;   // camera-relative

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;    // 512px albedo class
layout(set = 0, binding = 2) uniform sampler2D      shadowMap;       // canopy self-shadowing
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;  // 1024px albedo class

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
    float emissiveMultiplier;
    vec3 cameraPosition;
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;
    mat4 biasedLightSpace;
    vec3 cameraWorld;
    int  debugShadowMode;   // 1 = shadow-only debug view (see lighting.glsl phxShadowOnly)
    float shadowDepthRange; // world-unit depth span of the light volume (bias normalization)
    vec4  grassDisplacers[16];      // declared only to reach the cascade fields below
    vec4  grassDisplacersAux[16];
    ivec4 grassDisplacerMeta;
    mat4 biasedLightSpaceNear;      // near shadow cascade (docs/NearShadowCascade.md)
    vec4 shadowCascadeNear;         // x = range end (0 = off), y = near depthRange
    mat4 lightSpaceMatrixNear;      // (prefix padding to reach the atmosphere fields below)
    mat4 biasedLightSpaceFar;
    vec4 shadowCascadeFar;
    mat4 lightSpaceMatrixFar;
    // ---- Atmosphere-derived lighting + exposure (2026-08-10) --------------------------------
    // The sky is a physical scattering model now, so these come from the SAME transmittance as the
    // sun's disc and colour. exposure converts radiance to something a display can show at all.
    vec3 ambientColor;
    vec3 hazeHorizonColor;
    vec3 hazeZenithColor;
    vec3 moonDirection;
    vec3 moonColor;
    float exposure;
    int   tonemapCurve;
    // ---- U3.3 prefix: std140 is positional, so occupancyBox needs every field ahead of it ----
    vec4  skyBodyDirRadius[4];
    vec4  skyBodyDisc[4];
    vec4  skyBodyLitDir[4];
    vec4  skyBodyLight[4];
    int   skyBodyCount;
    ivec4 occupancyBox;   // xyz = box min corner (chunk coords), w = 1 when 11/12 are real
    vec4  giProbeGrid;    // probe field: xyz = probe (0,0,0) world position, w = spacing
} ubo;

#include "occupancy.glsl"   // U3.3 / D15: leaves get the SAME visibility term as stone
#include "gi_field.glsl"    // THE ambient term (probe field); G-141: no receiver traces its own sky

// U3.3 -- a campfire lights the canopy above it. Same regression as grass: the block-light flood
// that used to reach foliage through vBlock was deleted by M0 and nothing replaced it.
struct PointLightGPU {
    vec4 positionAndRadius;
    vec4 colorAndIntensity;
    vec4 extentAndPad;          // x = emitter half-extent (G-157), yzw = padding
};
struct SpotLightGPU {
    vec4 positionAndRadius;
    vec4 directionAndInnerCone;
    vec4 colorAndIntensity;
    vec4 outerConeAndPadding;
};
layout(std430, set = 0, binding = 3) readonly buffer LightBuffer {
    uint numPointLights;
    uint numSpotLights;
    uint _pad0;
    uint _pad1;
    PointLightGPU pointLights[32];
    SpotLightGPU spotLights[16];
} lights;

layout(set = 0, binding = 9) uniform sampler2D shadowMapNear;   // near cascade

layout(location = 0) out vec4 outColor;

void main() {
    if ((flags & (1u << 23u)) != 0u) {   // voxel.frag's LOD discard (F2.14), verbatim
        const float lodC = float(flags >> 24u) / 8.0 - 16.0;
        const int lodLevel = int((flags >> 16u) & 0xFu), lodMax = int((flags >> 20u) & 7u);
        const float lodL = log2(max(length(inWorldPos), 1e-3)) + lodC;
        const ivec2 bp = ivec2(gl_FragCoord.xy) & 3;
        const int bayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
        const float lodU = (float(bayer[bp.y * 4 + bp.x]) + 0.5) / 16.0;
        const float lodD = 0.5 * (lodU - 0.5);
        const int lodLo = clamp(int(floor(lodL - 0.25 + lodD)), 0, lodMax);
        const int lodHi = clamp(int(floor(lodL + 0.25 + lodD)), 0, lodMax);
        if (lodLevel < lodLo || lodLevel > lodHi) discard;
    }
    const uint cls = (textureIndex >> 15) & 1u;
    const float layer = float(textureIndex & 0x7FFFu);
    vec3 sum = vec3(0.0);
    float n = 0.0;
    for (int i = 0; i < 16; ++i) {
        const vec2 uv = (vec2(float(i & 3), float(i >> 2)) + 0.5) / 4.0;
        const vec4 t = (cls == 1u) ? textureLod(textureArrayHi, vec3(uv, layer), 1.0)
                                   : textureLod(textureArray, vec3(uv, layer), 1.0);
        if (t.a >= 0.5) { sum += t.rgb; n += 1.0; }
    }
    const vec3 col = n > 0.0 ? sum / n : vec3(0.0);
    // foliage.frag's card lighting, line for line (vWorldPos -> inWorldPos, vShadowCoord -> shadowCoord, vShade -> 0.91)
    float shadowFactor = phxShadowFast(shadowMap, shadowCoord, ubo.shadowDepthRange);
    if (ubo.shadowCascadeNear.x > 0.0)
        shadowFactor = min(shadowFactor, phxShadowFast(shadowMapNear, ubo.biasedLightSpaceNear * vec4(inWorldPos, 1.0),
                                                       ubo.shadowCascadeNear.y));
    const vec3 up = vec3(0.0, 1.0, 0.0);
    vec3  fill    = phxAmbient(inWorldPos + ubo.cameraWorld, up, ubo.occupancyBox, ubo.giProbeGrid, ubo.ambientColor);
    float skyAcc  = phxSkyAccessOf(fill, up, ubo.ambientColor);
    vec3  sunTerm = ubo.sunColor * (0.7 * shadowFactor * phxSunGate(skyAcc, shadowCoord));
    vec3  rayDir  = normalize(ubo.sunDirection);
    vec3  viewDir = normalize(inWorldPos - ubo.cameraPosition);
    float backlit = pow(max(dot(viewDir, rayDir), 0.0), 6.0);
    float trans   = backlit * (1.0 - shadowFactor * 0.6) * (0.25 + 0.75 * phxSunGate(skyAcc, shadowCoord)) * 0.9;
    vec3 lampTerm = vec3(0.0);
    {
        vec3 worldP = inWorldPos + ubo.cameraWorld;
        for (uint i = 0u; i < lights.numPointLights && i < 32u; ++i) {
            vec3  lp     = lights.pointLights[i].positionAndRadius.xyz;
            float radius = lights.pointLights[i].positionAndRadius.w;
            float dist   = length(lp - inWorldPos);
            if (dist >= radius) continue;
            if (phxLightVisibility(worldP, up, lp + ubo.cameraWorld, lights.pointLights[i].extentAndPad.x,
                                   ubo.occupancyBox) <= 0.0)
                continue;
            float atten = clamp(1.0 - dist / radius, 0.0, 1.0);
            atten *= atten;
            lampTerm += lights.pointLights[i].colorAndIntensity.xyz * lights.pointLights[i].colorAndIntensity.w * atten;
        }
    }
    vec3 lit = (col * (fill + sunTerm + lampTerm) + col * trans * ubo.sunColor) * 0.91;
    if (ubo.debugShadowMode == 2) { outColor = vec4(0.05, 0.05, 0.06, 1.0); return; }   // foliage.frag's rules
    if (ubo.debugShadowMode == 1) { outColor = phxShadowOnly(shadowFactor); return; }
    if (ubo.debugShadowMode >= 3) { outColor = vec4(0.02, 0.02, 0.025, 1.0); return; }
    outColor = vec4(lit, 1.0);
}
