#version 450
#extension GL_GOOGLE_include_directive : require
//
// water_cell.frag — per-cell water surface shading (the CPU sim's actual field).
//
// WaterSystemV3 Phase 1: all the shading lives in water_common.glsl, shared with the flat sea
// plane, so the two renderers cannot drift apart. This shader's only job is to supply the
// per-cell inputs — notably the sim's own column depth, which is a better thickness floor than
// the depth buffer for a thin film or a vertical waterfall curtain seen edge-on.
//
layout(location = 0) in vec3  fragWorldPos;
layout(location = 1) in float fragColumnDepth; // water column depth in cells (from the sim)
layout(location = 2) in float fragSide;        // 0 = top face, 1 = vertical side face
layout(location = 3) in vec4  fragFlow;        // xy = flow dir, z = strength, w = foam (Phase 3)
layout(location = 0) out vec4 outColor;

// Shared scene UBO — declared as a std140 PREFIX (only the fields we use, in order).
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
} ubo;

layout(set = 1, binding = 0) uniform sampler2D refractionTex;
layout(set = 1, binding = 1) uniform sampler2D sceneDepthTex;
// Ripple/disturbance heightfield (small-scale plan Phase 3) — same texture the vertex stage
// displaces by; here its gradient tilts the shading normal so rings catch the light.
layout(set = 1, binding = 2) uniform sampler2D rippleTex;

// Must match water_cell.vert's block exactly (one push-constant range, both stages). 112 bytes.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 screen;     // xy = screen size (px), zw unused
    vec4 ripple;     // ripple window: xy = origin (world XZ), z = 1/windowSize, w = amplitude
} pc;

#include "water_common.glsl"

void main() {
    WaterSurfaceInput inp;
    inp.worldPos     = fragWorldPos;
    inp.camPos       = pc.camPosTime.xyz;
    inp.time         = pc.camPosTime.w;
    inp.screenSize   = pc.screen.xy;
    inp.fragDepthNdc = gl_FragCoord.z;
    inp.fragCoord    = gl_FragCoord.xy;
    inp.sideFace     = fragSide;
    // The sim knows exactly how deep this column is; the depth buffer only knows what is behind
    // the surface along the view ray. Take whichever reads as more water.
    inp.minThickness = fragColumnDepth;
    // Flow shading (Phase 3): the sim's flow proxy, carried per instance.
    inp.flowDir      = fragFlow.xy;
    inp.flowStrength = fragFlow.z;
    inp.foam         = fragFlow.w;
    // Per-cell water has no Gerstner swell — its macro shape is the sloped quad the vertex shader
    // already built from the sim's per-corner heights, so the shading normal starts flat. The
    // ripple field's gradient then tilts it (a height OFFSET sampled isotropically by world XZ —
    // not an advection; see the water_common.glsl footguns).
    vec3 rippleN = vec3(0.0, 1.0, 0.0);
    {
        vec2 uv = (fragWorldPos.xz - pc.ripple.xy) * pc.ripple.z;
        if (uv.x > 0.0 && uv.y > 0.0 && uv.x < 1.0 && uv.y < 1.0) {
            float texel = 1.0 / float(textureSize(rippleTex, 0).x);
            float texelWorld = texel / pc.ripple.z;   // one texel in world units
            float hx = texture(rippleTex, uv + vec2(texel, 0.0)).r
                     - texture(rippleTex, uv - vec2(texel, 0.0)).r;
            float hz = texture(rippleTex, uv + vec2(0.0, texel)).r
                     - texture(rippleTex, uv - vec2(0.0, texel)).r;
            // Central difference over 2 texels; the 2.0 boost makes rings read at ripple scale.
            float g = pc.ripple.w * 2.0 / (2.0 * texelWorld);
            rippleN = normalize(vec3(-hx * g, 1.0, -hz * g));
        }
    }
    inp.baseNormal   = rippleN;
    // No swell here, so no breaking surf — but a lake or river still gets the waterline rim, which
    // is what stops its edge from simply dissolving into the bank.
    inp.wavePhase    = 0.0;
    inp.breakDepth   = 0.0;
    // Per-cell water has no single rest level — the sim decides where its surface is, per column —
    // so the dry-land gate does not apply here.
    inp.restLevelY   = -1e9;
    // PER-BODY PROFILE: pinned to NEUTRAL (Water Appearance v4 W1). Rivers, creeks, ponds and
    // splashes are explicitly OUT OF SCOPE for v4 — its coverage decision is oceans + lakes, which
    // are drawn by the sea clipmap. Neutral is defined as "exactly today's look", so this renderer
    // must come out pixel-identical; that is a test, not an assumption. When flowing water gets its
    // own profile, this is where it arrives (per-instance, alongside fragFlow).
    inp.turbidity    = 0.0;
    inp.roughness    = 1.0;
    // SSR is OFF for cell water in v1 (v4 W4): the coverage decision is oceans + lakes, which the
    // sea clipmap draws. Rivers and creeks are narrow, close-range and often overhung, which is the
    // worst case for a screen-space march (most rays leave the screen or hit the bank), so they keep
    // the sky reflection until there is a reason and a measurement to change it.
    inp.viewProj     = pc.viewProj;
    inp.ssr          = 0.0;

    outColor = shadeWaterSurface(inp);
}
