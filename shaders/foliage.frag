#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // shared ambient / shadow / aerial model

// Leaf card fragment: cutout LEAF-CLUSTER silhouette alpha-tested from the leaf texture's alpha
// channel (baked by tools/leaf_forge.py — chunky voxel-native masks; was a flat math ellipse).
// Colour from the leaf texture × baked skylight/block light. No blending → opaque pass, no OIT
// cost. Sibling of grass.frag.

layout(location = 0) in flat uint vTex;    // leaf texture index (class bit 15 + layer bits 0-14)
layout(location = 1) in vec2  vCard;       // card-plane coords in [-1,1]
layout(location = 2) in float vSky;        // baked skylight 0..1
layout(location = 3) in vec3  vBlock;      // baked block light 0..1/channel
layout(location = 4) in float vShade;      // per-card brightness variation
layout(location = 5) in flat uint vMaskV;  // per-card mask variant (bit0 flipX, bit1 flipY, bit2 swap)
layout(location = 6) in vec4  vShadowCoord; // biased light-space coord (shadow RECEIVING)
layout(location = 7) in vec3  vWorldPos;    // for view-dependent backlit transmission
layout(location = 8) in float vFade;        // radius-edge dither fade (1 = solid)

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
} ubo;

#include "occupancy.glsl"   // U3.3 / D15: leaves get the SAME visibility term as stone

// U3.3 -- a campfire lights the canopy above it. Same regression as grass: the block-light flood
// that used to reach foliage through vBlock was deleted by M0 and nothing replaced it.
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

layout(set = 0, binding = 9) uniform sampler2D shadowMapNear;   // near cascade

layout(location = 0) out vec4 outColor;

// 4x4 ordered Bayer dither (same pattern as far_tree_mesh.frag): stable per-pixel
// threshold, no temporal shimmer.
float bayer4(vec2 p) {
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 ip = ivec2(mod(p, 4.0));
    return (m[ip.x + ip.y * 4] + 0.5) / 16.0;
}

void main() {
    // Radius-edge dissolve (2026-08-06): cards dither out over the last 10% of the foliage
    // radius instead of the old whole-chunk pop at the cutoff.
    if (vFade < bayer4(gl_FragCoord.xy)) discard;

    // Per-card mask orientation: flip/swap the UV so one baked mask yields 8 variants.
    vec2 uv = vCard * 0.5 + 0.5;
    if ((vMaskV & 1u) != 0u) uv.x = 1.0 - uv.x;
    if ((vMaskV & 2u) != 0u) uv.y = 1.0 - uv.y;
    if ((vMaskV & 4u) != 0u) uv = uv.yx;

    // Colour + cutout mask from the leaf texture (class bit 15 selects hi/lo array; layer in low
    // 15 bits). Alpha carries the leaf-forge cluster silhouette — discard the gaps.
    uint cls   = (vTex >> 15) & 1u;
    uint layer = vTex & 0x7FFFu;
    vec4 texel = (cls == 1u) ? texture(textureArrayHi, vec3(uv, float(layer)))
                             : texture(textureArray,   vec3(uv, float(layer)));
    if (texel.a < 0.5) discard;
    vec3 col = texel.rgb;

    // ---- Canopy volume lighting -------------------------------------------------------
    // Shadow + ambient from the SHARED model (lighting.glsl): cards sample the shadow map
    // (which contains the cards themselves), so the sun side of a canopy is lit and the
    // interior falls into dappled shade — the tree lights like a volume, not flat ambient.
    float shadowFactor = phxShadowFast(shadowMap, vShadowCoord, ubo.shadowDepthRange);
    // Near cascade min-compose: fine blade/prop shadows onto near canopies; out-of-volume
    // coords fail phxShadowCoordValid → no-op. vWorldPos is camera-relative, the space the
    // biased matrices expect.
    if (ubo.shadowCascadeNear.x > 0.0)
        shadowFactor = min(shadowFactor,
                           phxShadowFast(shadowMapNear,
                                         ubo.biasedLightSpaceNear * vec4(vWorldPos, 1.0),
                                         ubo.shadowCascadeNear.y));
    float skyGate = phxSkyGate(vSky);
    vec3  fill    = phxAmbientAtmos(vec3(0.0, 1.0, 0.0), vSky, ubo.ambientColor);
    vec3  sunTerm = ubo.sunColor * (0.7 * shadowFactor * skyGate);

    // Backlit TRANSMISSION: looking toward the sun through foliage, shadowed leaves glow —
    // light scattering through the blade. Strongest at the rim (partially occluded), damped
    // deep inside the canopy (many layers), zero when the leaf is fully sunlit (no contrast).
    vec3  rayDir   = normalize(ubo.sunDirection);              // direction sun rays travel
    vec3  viewDir  = normalize(vWorldPos - ubo.cameraPosition);
    float backlit  = pow(max(dot(viewDir, rayDir), 0.0), 6.0);
    float trans    = backlit * (1.0 - shadowFactor * 0.6) * (0.25 + 0.75 * skyGate) * 0.9;

    // U3.3 -- point/spot lights on leaf cards. A card has no single meaningful normal (it is a
    // billboarded quad), so light it as an upward-facing diffuse receiver: attenuation and
    // occlusion shape the result, not card orientation. vBlock is the M0 placeholder (constant 0);
    // this replaces it with real transport.
    vec3 lampTerm = vec3(0.0);
    {
        const vec3 up = vec3(0.0, 1.0, 0.0);
        vec3 worldP = vWorldPos + ubo.cameraWorld;
        for (uint i = 0u; i < lights.numPointLights && i < 32u; ++i) {
            vec3  lp     = lights.pointLights[i].positionAndRadius.xyz;
            float radius = lights.pointLights[i].positionAndRadius.w;
            float dist   = length(lp - vWorldPos);
            if (dist >= radius) continue;
            if (phxLightVisibility(worldP, up, lp + ubo.cameraWorld, ubo.occupancyBox) <= 0.0)
                continue;
            float atten = clamp(1.0 - dist / radius, 0.0, 1.0);
            atten *= atten;
            lampTerm += lights.pointLights[i].colorAndIntensity.xyz
                      * lights.pointLights[i].colorAndIntensity.w * atten;
        }
    }

    vec3 lit = (col * (fill + sunTerm + lampTerm) + col * trans * ubo.sunColor) * vShade;

    // Debug view 2 is the GRASS WIND ramp. Everything that is not grass must go flat and
    // dark, or the shadow-only view underneath drowns the signal it exists to show.
    if (ubo.debugShadowMode == 2) { outColor = vec4(0.05, 0.05, 0.06, 1.0); return; }
    if (ubo.debugShadowMode == 1) { outColor = phxShadowOnly(shadowFactor); return; }
    // D4: same rule as grass — a per-system view this pass does not implement must go flat dark
    // rather than swamp the measurement.
    if (ubo.debugShadowMode >= 3) { outColor = vec4(0.02, 0.02, 0.025, 1.0); return; }
    outColor = vec4(lit, 1.0);
}
