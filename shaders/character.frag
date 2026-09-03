#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // THE shared lighting model — see the note below

// ⚠️ THIS SHADER USED ITS OWN LIGHTING AND IT SHOWED. It carried a private kSkyFill (0.35 against
// the world's model), no ambient floor, no hemisphere tint, no aerial perspective, and -- once the
// atmosphere landed -- NO TONE MAP AT ALL. That last one is the serious part: the world is exposed
// and tone-mapped while characters were not, so a character stood in a scene rendered on a
// completely different response curve and blew out against it.
//
// Now on the shared model: phxAmbientAtmos for the fill (so shadows on a character go cool with the
// sky, like everything else). Tone mapping is NOT done here -- there is one tone map for the
// whole frame, in post_process.frag, applied after compositing. Output linear HDR.
//
// STILL DIVERGENT, deliberately deferred: this pass samples only the MID shadow cascade, where
// voxel.frag min-composes near + mid. Characters therefore miss the fine near-cascade shadows.
// Fixing that needs the binding-9 near map wired into this pipeline.

layout(location = 0) in vec4 fragColor;    // .a = per-character opacity
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragBakedLight; // x = skylight (0..1), yzw = block RGB (0..1)

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
    mat4 viewProj;          // proj*view, precombined once per frame on CPU
    mat4 biasedLightSpace;  // shadow bias * lightSpaceMatrix, precombined on CPU
    // Prefix padding to reach the atmosphere fields below. These are not read here; they must be
    // declared so the std140 offsets match the C++ UniformBufferObject.
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
    // Atmosphere-derived lighting + exposure.
    vec3  ambientColor;
    vec3  hazeHorizonColor;
    vec3  hazeZenithColor;
    vec3  moonDirection;
    vec3  moonColor;
    float exposure;
    int   tonemapCurve;
    // U2: prefix padding to reach occupancyBox. Not otherwise read here; the std140 offsets must
    // match the C++ UniformBufferObject exactly or occupancyBox lands on the wrong bytes.
    vec4  skyBodyDirRadius[4];
    vec4  skyBodyDisc[4];
    vec4  skyBodyLitDir[4];
    vec4  skyBodyLight[4];
    int   skyBodyCount;
    ivec4 occupancyBox;   // xyz = box min corner (chunk coords), w = bitfield (see occupancy.glsl)
} ubo;

#include "occupancy.glsl"   // U2 / D14: the same visibility term voxel.frag uses

// Point light (32 bytes, std430)
struct PointLightGPU {
    vec4 positionAndRadius;     // xyz = position, w = radius
    vec4 colorAndIntensity;     // xyz = color, w = intensity
};

// Spot light (64 bytes, std430)
struct SpotLightGPU {
    vec4 positionAndRadius;     // xyz = position, w = radius
    vec4 directionAndInnerCone; // xyz = direction, w = innerCone
    vec4 colorAndIntensity;     // xyz = color, w = intensity
    vec4 outerConeAndPadding;   // x = outerCone, yzw = padding
};

layout(std430, set = 0, binding = 3) readonly buffer LightBuffer {
    uint numPointLights;
    uint numSpotLights;
    uint _pad0;
    uint _pad1;
    PointLightGPU pointLights[32];
    SpotLightGPU spotLights[16];
} lights;

// Sun shadow map (shared set-0 binding 2, same as voxel.frag). Characters now RECEIVE
// sun shadows so a character standing in a building's shadow is darkened like the world.
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
// U1: the NEAR cascade, which this pass previously did not sample at all.
layout(set = 0, binding = 9) uniform sampler2D shadowMapNear;
// (the private 16-tap poissonDisk that used to live here is gone — lighting.glsl owns the filter)

layout(location = 0) out vec4 outColor;

float calcAttenuation(float d, float radius) {
    float linear = 4.5 / radius;
    float quadratic = 75.0 / (radius * radius);
    float atten = 1.0 / (1.0 + linear * d + quadratic * d * d);
    float falloff = clamp(1.0 - (d / radius), 0.0, 1.0);
    return atten * falloff;
}

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(-ubo.sunDirection);
    vec3 viewDir = normalize(ubo.cameraPosition - fragWorldPos);
    float diff = max(dot(normal, lightDir), 0.0);

    // Blinn-Phong specular (sun)
    float sunSpec = 0.0;
    if (diff > 0.0) {
        vec3 halfVec = normalize(lightDir + viewDir);
        sunSpec = pow(max(dot(normal, halfVec), 0.0), 32.0) * 0.3;
    }

    // Baked light field (Phase 4): characters react to the same skylight + block light
    // as the world, so they darken in sealed rooms and pick up glow/spell light. Mirrors
    // voxel.frag: sky is a FILL (kSkyFill), sun is the KEY gated by skylight, block adds on top.
    float sky        = fragBakedLight.x;
    vec3  blockColor = fragBakedLight.yzw;
    float skyCurve   = sky * sky;
    // U1: a private `const float kSkyFill = 0.35;` sat here, left over from before this pass moved
    // to phxAmbientAtmos. It had no readers and contradicted the shared model's own kSkyFill.
    // Deleted rather than left to be "restored" by a later reader.

    // U1 — SHARED shadow model. This pass used to run its own 16-tap PCF with a hardcoded
    // `kShadowBias = 0.0009`, a RAW NORMALIZED-DEPTH constant. lighting.glsl documents that exact
    // policy as the bug it was created to fix: a raw constant's PHYSICAL size scales with the
    // shadow distance (0.26 u at 40 u, 0.85 u at 420 u — taller than a grass blade), so the bias
    // meant something different at every cascade. phxShadowBias authors it in WORLD units and
    // divides by the fitted volume's depth span, so it means the same thing everywhere.
    //
    // It also sampled the MID map only, while grass and foliage have min-composed the NEAR cascade
    // since 2026-08-06 — characters were the last receiver still missing fine contact shadows.
    vec4 shadowCoord = ubo.biasedLightSpace * vec4(fragWorldPos, 1.0);
    float shadowFactor = phxShadowPCSS(shadowMap, shadowCoord, diff, gl_FragCoord.xy,
                                       ubo.shadowDepthRange);
    if (ubo.shadowCascadeNear.x > 0.0) {
        vec4 nearCoord = ubo.biasedLightSpaceNear * vec4(fragWorldPos, 1.0);
        shadowFactor = min(shadowFactor,
                           phxShadowPCSS(shadowMapNear, nearCoord, diff, gl_FragCoord.xy,
                                         ubo.shadowCascadeNear.y));
    }

    // Shared hemispheric fill driven by the atmosphere, so a character's shaded side goes cool
    // with the sky exactly as the world's does.
    vec3 ambient = phxAmbientAtmos(normal, sky, ubo.ambientColor);
    vec3 finalLight = ambient + (diff + sunSpec) * ubo.sunColor * skyCurve * shadowFactor;
    // Moonlight, matching voxel.frag: unshadowed (the cascades are fitted to the sun), and
    // gated by sky access. Without it a character is black on a moonlit night while the ground
    // around them is lit.
    if (ubo.moonColor.b > 0.0) {
        float moonNdl = max(dot(normal, normalize(-ubo.moonDirection)), 0.0);
        finalLight += ubo.moonColor * moonNdl * skyCurve;
    }
    finalLight += blockColor * blockColor; // omnidirectional warm/colored fill from baked block light

    // D4 (prerequisite for U2's gate): characters had NO debug views at all, so the one thing the
    // M2 gate needs to see on a character — its forward point/spot contribution in isolation —
    // was unmeasurable. Accumulated separately here and emitted under mode 5, matching voxel.frag.
    vec3 dbgForward = vec3(0.0);

    // Point lights
    for (uint i = 0u; i < lights.numPointLights && i < 32u; i++) {
        vec3 lightPos = lights.pointLights[i].positionAndRadius.xyz;
        float radius = lights.pointLights[i].positionAndRadius.w;
        vec3 lightColor = lights.pointLights[i].colorAndIntensity.xyz;
        float intensity = lights.pointLights[i].colorAndIntensity.w;

        vec3 toLight = lightPos - fragWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            float ndotl = max(dot(normal, ldir), 0.0);
            // U2 / D14: a lantern sealed inside a stone room used to light a character standing
            // OUTSIDE it, because this loop had no visibility term at all while voxel.frag did.
            // `normal` is the geometric normal here — characters are not normal-mapped — so it is
            // safe to use as the ray-origin offset.
            if (ndotl > 0.0 &&
                phxLightVisibility(fragWorldPos + ubo.cameraWorld, normal,
                                   lightPos + ubo.cameraWorld, ubo.occupancyBox) > 0.0) {
                float atten = calcAttenuation(dist, radius);
                vec3 h = normalize(ldir + viewDir);
                float pSpec = pow(max(dot(normal, h), 0.0), 32.0) * 0.3;
                vec3 contrib = lightColor * intensity * (ndotl + pSpec) * atten;
                finalLight += contrib;
                dbgForward += contrib;
            }
        }
    }

    // Spot lights
    for (uint i = 0u; i < lights.numSpotLights && i < 16u; i++) {
        vec3 lightPos = lights.spotLights[i].positionAndRadius.xyz;
        float radius = lights.spotLights[i].positionAndRadius.w;
        vec3 spotDir = normalize(lights.spotLights[i].directionAndInnerCone.xyz);
        float innerCone = lights.spotLights[i].directionAndInnerCone.w;
        vec3 lightColor = lights.spotLights[i].colorAndIntensity.xyz;
        float intensity = lights.spotLights[i].colorAndIntensity.w;
        float outerCone = lights.spotLights[i].outerConeAndPadding.x;

        vec3 toLight = lightPos - fragWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            float ndotl = max(dot(normal, ldir), 0.0);
            float theta = dot(-ldir, spotDir);
            float spotFactor = smoothstep(outerCone, innerCone, theta);
            // Trace only inside the cone and on facing surfaces — outside either the contribution
            // is already zero and a march would be pure cost.
            if (ndotl > 0.0 && spotFactor > 0.0 &&
                phxLightVisibility(fragWorldPos + ubo.cameraWorld, normal,
                                   lightPos + ubo.cameraWorld, ubo.occupancyBox) > 0.0) {
                float atten = calcAttenuation(dist, radius);
                vec3 h = normalize(ldir + viewDir);
                float sSpec = pow(max(dot(normal, h), 0.0), 32.0) * 0.3;
                vec3 contrib = lightColor * intensity * (ndotl + sSpec) * atten * spotFactor;
                finalLight += contrib;
                dbgForward += contrib;
            }
        }
    }

    // Alpha reaches here from CharacterInstanceData.color.a (see
    // RagdollCharacter::setRenderAlpha). It only becomes visible when the
    // character is drawn through the blend-enabled translucent pipeline;
    // opaque characters carry a = 1 and are unaffected.
    // Debug views, matching voxel.frag's numbering so a capture means the same thing on every
    // surface. Mode 5 is THE M2/U2 gate view: the forward point/spot term alone.
    if (ubo.debugShadowMode == 1) { outColor = phxShadowOnly(shadowFactor); return; }
    if (ubo.debugShadowMode == 2) { outColor = vec4(0.05, 0.05, 0.06, 1.0); return; }
    if (ubo.debugShadowMode == 5) { outColor = vec4(dbgForward, 1.0); return; }
    // Mode 8 — OCCUPANCY BINDING HEALTH, same colours as voxel.frag's mode 8. This is the view
    // that answers "did the std140 prefix in THIS shader actually reach ubo.occupancyBox", which
    // is the whole risk when a shader declares a long prefix to reach a trailing field: a wrong
    // offset reads neighbouring bytes and the visibility term silently disables itself.
    //   blue = occupancy not readable here (w bit0 clear) — misaligned, or genuinely absent
    //   red  = readable, and this fragment's own cell reads SOLID
    //   green= readable, and its cell reads empty (normal for a character standing in air)
    if (ubo.debugShadowMode == 8) {
        if ((ubo.occupancyBox.w & 1) == 0) { outColor = vec4(0.0, 0.0, 1.0, 1.0); return; }
        vec3 probe = fragWorldPos + ubo.cameraWorld - normal * (0.5 / 9.0);
        bool solid = phxOccupancySolid(ivec3(floor(probe * 9.0)), ubo.occupancyBox);
        outColor = solid ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
        return;
    }

    outColor = vec4(fragColor.rgb * finalLight, fragColor.a);
}
