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
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
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
    float skyGate = phxSkyGate(vSky);
    vec3  fill    = phxAmbient(vec3(0.0, 1.0, 0.0), vSky, ubo.ambientLight);
    vec3  sunTerm = ubo.sunColor * (0.7 * shadowFactor * skyGate);

    // Backlit TRANSMISSION: looking toward the sun through foliage, shadowed leaves glow —
    // light scattering through the blade. Strongest at the rim (partially occluded), damped
    // deep inside the canopy (many layers), zero when the leaf is fully sunlit (no contrast).
    vec3  rayDir   = normalize(ubo.sunDirection);              // direction sun rays travel
    vec3  viewDir  = normalize(vWorldPos - ubo.cameraPosition);
    float backlit  = pow(max(dot(viewDir, rayDir), 0.0), 6.0);
    float trans    = backlit * (1.0 - shadowFactor * 0.6) * (0.25 + 0.75 * skyGate) * 0.9;

    vec3 lit = (col * (fill + sunTerm) + col * trans * ubo.sunColor + col * vBlock * 0.5) * vShade;

    // Debug view 2 is the GRASS WIND ramp. Everything that is not grass must go flat and
    // dark, or the shadow-only view underneath drowns the signal it exists to show.
    if (ubo.debugShadowMode == 2) { outColor = vec4(0.05, 0.05, 0.06, 1.0); return; }
    if (ubo.debugShadowMode == 1) { outColor = phxShadowOnly(shadowFactor); return; }
    outColor = vec4(lit, 1.0);
}
