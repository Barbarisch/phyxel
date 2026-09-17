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
layout(location = 6) in vec4  vShadowCoord; // biased light-space coord (shadow RECEIVING)
layout(location = 7) in float vWindLean;   // wind debug: lean fraction, 0 upright .. 0.9 at cap
layout(location = 8) in vec4  vShadowCoordNear; // near-cascade coord (fine texels)
layout(location = 9) in vec3  vWorldPos;        // U3.3: camera-relative pos, for point lights
layout(location = 4) in float vSky;            // M4: TRACED sky visibility, computed per blade vertex

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
    // ---- U3.3 prefix ------------------------------------------------------------------------
    // std140 is positional: to READ occupancyBox this shader must declare every field ahead of it,
    // even the ones it never touches. These four sky-body arrays are pure padding here.
    vec4  skyBodyDirRadius[4];
    vec4  skyBodyDisc[4];
    vec4  skyBodyLitDir[4];
    vec4  skyBodyLight[4];
    int   skyBodyCount;
    ivec4 occupancyBox;   // xyz = box min corner (chunk coords), w = 1 when 11/12 are real
} ubo;

#include "occupancy.glsl"   // U3.3 / D15: grass gets the SAME visibility term as stone

// U3.3 — A CAMPFIRE LIGHTS THE GRASS AROUND IT.
//
// This was a REGRESSION, not a pre-existing gap: before M0, the block-light flood reached grass
// through vBlock. M0 deleted the flood and nothing replaced it, so vegetation went sun-and-ambient
// only and a torch in a meadow lit the ground voxels while every blade around it stayed dark.
// Reading the light SSBO puts grass on the same emitter model as everything else -- and with U3.2,
// "a torch" now includes any emissive voxel.
struct PointLightGPU {
    vec4 positionAndRadius;     // xyz = position, w = radius
    vec4 colorAndIntensity;     // xyz = color, w = intensity
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

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;    // 512px albedo class
layout(set = 0, binding = 2) uniform sampler2D      shadowMap;       // sun shadows on grass
layout(set = 0, binding = 9) uniform sampler2D      shadowMapNear;   // near cascade
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;  // 1024px albedo class

layout(location = 0) out vec4 outColor;

void main() {
    // Blade silhouette: triangular taper — full width at base, pinches to a TRUE point at the
    // tip (was 0.92, which left every tip a visibly clipped 8%-wide stub — "rectangular grass").
    float taper = 1.0 - vGrad;
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
    // M4: vSky is TRACED now (in grass.vert, per blade vertex) rather than read from the dead
    // per-instance nibble the flood used to fill. Grass inside a house is finally darker than grass
    // in the open. Tracing per VERTEX rather than per fragment because sky access varies at world
    // scale and a blade is ~0.05-0.1 u wide -- per fragment cost 3.284 ms against 1.240 ms.
    float sky = vSky;
    vec3  ambient = phxAmbientAtmos(vec3(0.0, 1.0, 0.0), sky, ubo.ambientColor);
    vec3  sunTerm = ubo.sunColor * (0.85 * shadowFactor * phxSkyGate(sky));

    // U3.3 — POINT/SPOT LIGHTS ON GRASS, with the same visibility term stone gets.
    //
    // A blade has no meaningful normal (it is a camera-facing cutout card, and the sun term above
    // deliberately avoids per-blade N·L because it makes the field sparkle). So light it as a
    // diffuse receiver facing UP: attenuation and occlusion carry the effect, not orientation.
    // That keeps a campfire's pool of light shaped by geometry rather than by blade facing.
    vec3 lampTerm = vec3(0.0);
    {
        const vec3 up = vec3(0.0, 1.0, 0.0);
        vec3 worldP = vWorldPos + ubo.cameraWorld;
        for (uint i = 0u; i < lights.numPointLights && i < 32u; ++i) {
            vec3  lp     = lights.pointLights[i].positionAndRadius.xyz;
            float radius = lights.pointLights[i].positionAndRadius.w;
            vec3  toL    = lp - vWorldPos;
            float dist   = length(toL);
            if (dist >= radius) continue;
            // Gate BEFORE the march, exactly as voxel.frag does -- this is what keeps the
            // visibility term near-free: almost no fragment actually traces.
            if (phxLightVisibility(worldP, up, lp + ubo.cameraWorld, ubo.occupancyBox) <= 0.0)
                continue;
            float atten = clamp(1.0 - dist / radius, 0.0, 1.0);
            atten *= atten;
            lampTerm += lights.pointLights[i].colorAndIntensity.xyz
                      * lights.pointLights[i].colorAndIntensity.w * atten;
        }
    }

    // vBlock is the M0 placeholder (a constant 0 -- the flood that fed it is gone). The lamp term
    // above is what replaces it, and it is real transport rather than a decayed per-cell field.
    vec3  lit = col * ao * (ambient + sunTerm + lampTerm);

    // ── WIND SHEEN (2026-09-03) ─────────────────────────────────────────────────────────────
    // A gust crossing a real field is seen as a LIGHT band sweeping the grass — bent blades tilt
    // their faces toward the sky/sun and brighten — far more than as silhouette displacement,
    // which is sub-pixel past ~20u. Blades here are lit with a fixed up-normal (no per-blade
    // N·L), so without this term bending changes their shading by exactly nothing and wind is
    // invisible at any distance ("almost impossible to see", user). Brightness rides the lean
    // fraction: upright ~0.9x, bowed in a gust up to ~1.4x — gust waves read as travelling
    // luminance bands at every distance. Quadratic so the static rest-lean (~0.1) barely
    // registers; time-independent at wind 0 (rest lean is static), so the stillness invariant
    // holds. Gated by skylight so cave/indoor grass doesn't glow.
    float sheen = clamp(vWindLean / 0.9, 0.0, 1.0);
    lit *= 1.0 + (0.5 * sheen * sheen - 0.10) * phxSkyGate(vSky);

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
    // D4: grass implements no per-SYSTEM isolation view (modes 3-9). A pass that does not
    // implement a view must render flat dark in it, or it drowns the signal the view exists to
    // show — measured: a mode-5 capture read 62% "lit" with a single light in the scene, because
    // grass and sky ignored the mode entirely. This is the same rule foliage.frag already applies
    // to the grass wind view (mode 2), generalised.
    if (ubo.debugShadowMode >= 3) { outColor = vec4(0.02, 0.02, 0.025, 1.0); return; }
    outColor = vec4(lit, 1.0);
}
