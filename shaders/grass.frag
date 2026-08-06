#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // shared ambient / shadow / aerial model

// Grass blade fragment: cutout (alpha-tested via discard) tapered blade silhouette, colour derived
// from the voxel's grass-top texture, shaded by baked skylight + block light. No blending → renders
// in the opaque pass with no OIT sort cost. See GrassRenderPipeline / the grass plan.

layout(location = 0) in flat uint vTex;    // grass texture index (class bit 15 + layer bits 0-14)
layout(location = 1) in vec2  vUV;         // colour-sample UV
layout(location = 2) in float vGrad;       // 0 base .. 1 tip
layout(location = 3) in float vSide;       // -1..1 across blade width
layout(location = 4) in float vSky;        // baked skylight 0..1
layout(location = 5) in vec3  vBlock;      // baked block light 0..1/channel
layout(location = 6) in vec4  vShadowCoord; // biased light-space coord (shadow RECEIVING)
layout(location = 7) in float vWindLean;   // wind debug: lean fraction, 0 upright .. 0.9 at cap
layout(location = 8) in vec4  vShadowCoordNear; // near-cascade coord (fine texels)

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
} ubo;

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;    // 512px albedo class
layout(set = 0, binding = 2) uniform sampler2D      shadowMap;       // sun shadows on grass
layout(set = 0, binding = 9) uniform sampler2D      shadowMapNear;   // near cascade
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;  // 1024px albedo class

layout(location = 0) out vec4 outColor;

void main() {
    // Blade silhouette: triangular taper — full width at base, pinches to a point at the tip.
    float taper = 1.0 - vGrad * 0.92;
    if (abs(vSide) > taper) discard;

    // Colour from the grass-top texture (class bit 15 selects hi/lo array; layer in low 15 bits).
    uint cls   = (vTex >> 15) & 1u;
    uint layer = vTex & 0x7FFFu;
    vec3 col = (cls == 1u) ? texture(textureArrayHi, vec3(vUV, float(layer))).rgb
                           : texture(textureArray,   vec3(vUV, float(layer))).rgb;

    // Blade-scale self-shadowing, APPROXIMATED. Grass genuinely casts into the shadow map
    // (189 grass chunks/frame at the reference pose), but a blade is ~0.05-0.1 world units
    // wide while a shadow texel at the 420 u draw distance is ~0.125 u — a blade is SUB-TEXEL,
    // so blade-on-blade shadows cannot resolve there and the map only captures clump-scale
    // occlusion. Real engines hit the same wall and answer it with a dedicated near cascade;
    // until that exists, this vertical gradient stands in for it: deep shade at the sward
    // floor where blades occlude each other, full light at the tips. Cheap, stable, and it
    // reads as depth instead of the flat carpet the old 0.5 floor gave.
    float ao = mix(0.28, 1.10, vGrad * vGrad);

    // Shadow + sun from the SHARED model (lighting.glsl). Grass had NO shadow lookup and
    // NO sun term at all before 2026-08-02 — it was ambient-only, which is why it stayed
    // bright inside shadows and read as a flat carpet. Blades are thin and face every
    // direction, so the fill uses an up normal and the sun a flat wrap rather than a hard
    // per-blade N-dot-L, which would just make the field sparkle.
    float shadowFactor = phxShadowFast(shadowMap, vShadowCoord, ubo.shadowDepthRange);
    // Near cascade: min-compose (union of shadows). This is where blade-on-blade and
    // object-on-blade shadows actually resolve — the mid map's texel is 1.4 blades wide.
    if (ubo.shadowCascadeNear.x > 0.0)
        shadowFactor = min(shadowFactor,
                           phxShadowFast(shadowMapNear, vShadowCoordNear,
                                         ubo.shadowCascadeNear.y));
    vec3  ambient = phxAmbient(vec3(0.0, 1.0, 0.0), vSky, ubo.ambientLight);
    vec3  sunTerm = ubo.sunColor * (0.85 * shadowFactor * phxSkyGate(vSky));
    vec3  lit = col * ao * (ambient + sunTerm) + col * vBlock * 0.5;

    // ── WIND DEBUG VIEW (POST /api/debug/shadow {"mode":2}) ─────────────────────────────────
    // Colours every blade by how hard the wind is pushing it RIGHT NOW, so a passing gust reads as
    // a coloured band sweeping the field and a dead field reads as uniformly dark. Added because
    // "is the wind moving?" was costing whole debugging rounds to answer by staring at pixels —
    // and once produced a confidently wrong answer.
    //   near-black = upright/still · blue = slight · green = moderate · yellow/red = at the cap
    // Ramp is on the LEAN FRACTION (sin of the lean angle), so it is comparable between blades of
    // different heights rather than being dominated by tall ones.
    if (ubo.debugShadowMode == 2) {
        // sqrt expands the LOW end. Linear against the 0.9 cap put the entire still-to-peak
        // range of ordinary wind inside the first ramp segment, so everything read as one flat
        // blue — true, but useless. sqrt keeps the cap meaningful while making gentle wind legible.
        float x = sqrt(clamp(vWindLean / 0.9, 0.0, 1.0));
        vec3 c = mix(vec3(0.02, 0.02, 0.06), vec3(0.0, 0.35, 1.0), smoothstep(0.00, 0.25, x));
        c = mix(c, vec3(0.0, 1.0, 0.25), smoothstep(0.25, 0.55, x));
        c = mix(c, vec3(1.0, 0.95, 0.0), smoothstep(0.55, 0.80, x));
        c = mix(c, vec3(1.0, 0.10, 0.0), smoothstep(0.80, 1.00, x));
        outColor = vec4(c, 1.0);
        return;
    }
    if (ubo.debugShadowMode == 1) { outColor = phxShadowOnly(shadowFactor); return; }
    outColor = vec4(lit, 1.0);
}
