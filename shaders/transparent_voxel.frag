#version 450
// transparent_voxel.frag — Weighted Blended OIT for transparent voxels (e.g. Glass).
//
// Outputs to two render targets:
//   location 0: accum  (RGBA16F) — weighted color accumulation
//   location 1: reveal (R8_UNORM) — product of (1 - alpha) across all layers
//
// Composite equation (in post_process.frag):
//   finalColor = mix(accum.rgb / accum.a, opaqueColor, reveal)

#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // U1: THE shared ambient / shadow model — glass is not a special case

layout(location = 0) in flat uint textureIndex;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 shadowCoord;
layout(location = 3) in flat uint flags;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec3 inWorldPos;
// U1: static_voxel.vert already emits this; the transparent pass simply never declared it, which
// is why glass had no sky gating at all.
layout(location = 6) in float vSkyLight;
// The exact chunk origin, for seeding anything that must be a pure function of world position (the
// crack). static_voxel.vert emits these for the opaque pass and the OIT pipeline is built with the
// same vertex shader; this pass simply never declared them (GlassTransparency.md §13.12).
layout(location = 10) in flat vec3 vChunkBaseAbs;
layout(location = 11) in flat vec3 vChunkBaseRel;

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
    // U2: prefix padding out to occupancyBox. std140 offsets must match the C++
    // UniformBufferObject EXACTLY or occupancyBox reads the wrong bytes. `cameraWorld` and
    // `ambientColor` become available as a side effect, and U1 needs both.
    mat4  reflectedViewProj;
    float elapsedTime;
    mat4  viewProj;
    mat4  biasedLightSpace;
    vec3  cameraWorld;
    int   debugShadowMode;
    float shadowDepthRange;
    vec4  grassDisplacers[16];
    vec4  grassDisplacersAux[16];
    ivec4 grassDisplacerMeta;
    mat4  biasedLightSpaceNear;
    vec4  shadowCascadeNear;
    mat4  lightSpaceMatrixNear;
    mat4  biasedLightSpaceFar;
    vec4  shadowCascadeFar;
    mat4  lightSpaceMatrixFar;
    vec3  ambientColor;
    vec3  hazeHorizonColor;
    vec3  hazeZenithColor;
    vec3  moonDirection;
    vec3  moonColor;
    float exposure;
    int   tonemapCurve;
    vec4  skyBodyDirRadius[4];
    vec4  skyBodyDisc[4];
    vec4  skyBodyLitDir[4];
    vec4  skyBodyLight[4];
    int   skyBodyCount;
    ivec4 occupancyBox;
    vec4  giProbeGrid;    // probe field: xyz = probe (0,0,0) world position, w = spacing
} ubo;

#include "occupancy.glsl"   // U2 / D14: glass gets the same visibility term as stone
#include "gi_field.glsl"    // THE ambient term (probe field); G-141: no receiver traces its own sky

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;     // class 0 albedo: 512px
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;   // class 1 albedo: 1024px (§13.17)
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
layout(set = 0, binding = 9) uniform sampler2D shadowMapNear;   // U1: the near cascade

struct PointLightGPU {
    vec4 positionAndRadius;
    vec4 colorAndIntensity;
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

layout(std430, set = 0, binding = 4) readonly buffer AtlasUVBuffer {
    uint count512;        // layers in the 512px (class 0) array
    uint fallbackIndex;   // placeholder layer (class 0)
    uint count1024;       // layers in the 1024px (class 1) array
    uint _pad1;
    vec4 textureUVs[];    // per-material props, stride 2 (P5)
} atlasUVs;

#include "voxel_world.glsl"  // SHARED with voxel.frag: world position, atlas select, crackStyle
#include "crack.glsl"        // the SAME fracture field as every opaque material

// MRT outputs
layout(location = 0) out vec4 accumColor;  // OIT accumulation
layout(location = 1) out float revealFactor; // OIT reveal (1 - alpha)

float calcAttenuation(float d, float radius) {
    float linear    = 4.5 / radius;
    float quadratic = 75.0 / (radius * radius);
    float atten     = 1.0 / (1.0 + linear * d + quadratic * d * d);
    float falloff   = clamp(1.0 - (d / radius), 0.0, 1.0);
    return atten * falloff;
}

// U1: the private 16-tap poissonDisk that lived here is gone — lighting.glsl owns the filter and
// its kPoisson16, so there is one disk and one bias policy rather than a copy per pass.

void main() {
    // RE-ENABLED 2026-09-23 (GlassTransparency.md §15). This pass opened with an unconditional
    // `discard` from 7a36910f until now, blamed on an "UNDEFINED layout validation error that
    // corrupts the post-process composite". Measured with validation layers on (§15.8): that error
    // fires identically with this pass disabled -- it is not this pass's -- and enabling it adds no
    // validation message and changes no pixel outside the glass. For four months glass was never
    // blended: it was see-through only where its texture punched cutout holes in the opaque pass.
    // Only process transparent voxels (bit 1 of flags); skip mirror voxels
    if ((flags & 2u) == 0u) discard;
    if ((flags & (1u << 10u)) != 0u) discard;

    // Class-aware sampling, shared with voxel.frag. The old single-array lookup sent every
    // 1024-class material -- Glass included -- to the placeholder checkerboard (§13.17).
    vec4 textureColor = phxSampleAlbedo(textureIndex, texCoord);

    // NO texture-alpha discard here. Coverage is continuous: the material's alpha is the floor and
    // the texture can only ADD coverage (max below). Discarding low-alpha texels would punch holes
    // again -- the cutout behaviour this fix removes.

    float matAlpha = float((flags >> 2u) & 0xFFu) / 255.0;
    float alpha = max(textureColor.a, max(matAlpha, 0.01));

    // Lighting (same as voxel.frag)
    vec3 normal   = normalize(inNormal);
    vec3 lightDir = normalize(-ubo.sunDirection);
    vec3 viewDir  = normalize(ubo.cameraPosition - inWorldPos);

    float diff = max(dot(normal, lightDir), 0.0);
    float sunSpec = 0.0;
    if (diff > 0.0) {
        vec3 halfVec = normalize(lightDir + viewDir);
        sunSpec = pow(max(dot(normal, halfVec), 0.0), 64.0) * 0.3;
    }

    // U1 — SHARED shadow + ambient. This pass ran its own 16-tap PCF with a hardcoded 0.005 raw
    // depth bias and sampled the MID map only, and its ambient was a FLAT `vec3(ubo.ambientLight)`
    // — the legacy 0..1 day/night scalar, not the atmosphere's physical sky radiance the rest of
    // the world has used since 2026-08-10. Glass was therefore lit by a different engine than the
    // stone beside it: no near or far cascade, no hemisphere tint, no sky colour.
    float shadowFactor = phxShadowPCSS(shadowMap, shadowCoord, diff, gl_FragCoord.xy,
                                       ubo.shadowDepthRange);
    if (ubo.shadowCascadeNear.x > 0.0) {
        vec4 nearCoord = ubo.biasedLightSpaceNear * vec4(inWorldPos, 1.0);
        shadowFactor = min(shadowFactor,
                           phxShadowPCSS(shadowMapNear, nearCoord, diff, gl_FragCoord.xy,
                                         ubo.shadowCascadeNear.y));
    }

    // AMBIENT from the probe field, the same term stone gets (gi_field.glsl). Direct sun is the
    // shadow map's answer; the field's enclosure gate only stands in beyond the cascades' coverage.
    vec3  ambient = phxAmbient(inWorldPos + ubo.cameraWorld, normal, ubo.occupancyBox, ubo.giProbeGrid, ubo.ambientColor);
    float skyAcc  = phxSkyAccessOf(ambient, normal, ubo.ambientColor);
    vec3 sunContrib = (diff * ubo.sunColor + sunSpec * ubo.sunColor)
                    * shadowFactor * phxSunGate(skyAcc, shadowCoord);   // G-135
    vec3 finalLight = ambient + sunContrib;

    // Point lights
    for (uint i = 0u; i < lights.numPointLights && i < 32u; i++) {
        vec3 lightPos = lights.pointLights[i].positionAndRadius.xyz;
        float radius  = lights.pointLights[i].positionAndRadius.w;
        vec3 lightColor = lights.pointLights[i].colorAndIntensity.xyz;
        float intensity = lights.pointLights[i].colorAndIntensity.w;
        vec3 toLight = lightPos - inWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            float ndotl = max(dot(normal, ldir), 0.0);
            // U2 / D14: a lantern used to shine straight THROUGH a glass wall, because this loop
            // had no visibility term while voxel.frag did. `normal` is the geometric face normal
            // (this shader does no normal mapping), so it is the correct ray-origin offset.
            if (ndotl > 0.0 &&
                phxLightVisibility(inWorldPos + ubo.cameraWorld, normal,
                                   lightPos + ubo.cameraWorld, ubo.occupancyBox) > 0.0) {
                float atten = calcAttenuation(dist, radius);
                vec3 h = normalize(ldir + viewDir);
                float pSpec = pow(max(dot(normal, h), 0.0), 32.0) * 0.3;
                finalLight += lightColor * intensity * (ndotl + pSpec) * atten;
            }
        }
    }

    vec3 litColor = textureColor.rgb * finalLight;

    // CRACKS ON GLASS (§13.3, decision (c): bright, FROSTED lines). Same field as stone -- same
    // crackField, same world-position seed via the shared helper, this material's crackStyle -- but
    // the opposite tone: a fracture surface in glass scatters light, so a crack reads WHITER and
    // MORE OPAQUE than the clear pane around it, where stone's crack darkens. The damage stage also
    // clouds the whole pane slightly, the transparent analogue of stone's overall darkening.
    float dmg = float((flags >> 11u) & 0xFu) / 15.0;
    if (dmg > 0.0) {
        vec3 worldPosAbs = phxWorldPosAbs(vChunkBaseAbs, inWorldPos, vChunkBaseRel);
        float crack = crackField(worldPosAbs, inNormal, dmg, phxCrackStyleOf(textureIndex));
        vec3 frost = vec3(0.92, 0.95, 0.97) * finalLight;
        litColor = mix(litColor, frost, clamp(crack * 0.9 + dmg * 0.10, 0.0, 1.0));
        alpha    = mix(alpha, 0.85, clamp(crack + dmg * 0.15, 0.0, 1.0));
    }

    // WBOIT weight: higher weight for closer, more opaque fragments
    // Use linear z (view-space) for better weight distribution
    float z = -(ubo.view * vec4(inWorldPos, 1.0)).z;
    float weight = alpha * clamp(0.03 / (1e-5 + pow(z / 200.0, 4.0)), 0.01, 3000.0);

    accumColor   = vec4(litColor * alpha * weight, alpha * weight);
    revealFactor = 1.0 - alpha;
}
