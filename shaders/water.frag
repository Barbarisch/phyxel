#version 450
#extension GL_GOOGLE_include_directive : require
//
// water.frag — the flat sea-level plane.
//
// WaterSystemV3 Phase 1: shading moved to water_common.glsl (shared with the per-cell surface),
// and the plane now draws in the post-scene water pass, so it composites the refracted seabed
// with real depth-based absorption instead of tinting with a fake `ndv` ramp. Unlike the per-cell
// surface it has no simulated column depth — the depth buffer is its only thickness source, which
// is exactly right for an implicit ocean: thickness IS seabed distance minus surface distance.
//
layout(location = 0) in vec3  fragWorldPos;
layout(location = 1) in vec3  fragWaveNormal;  // Phase 2: analytic Gerstner normal
layout(location = 2) in float fragWaveFoam;    // Phase 2: crest sharpness 0..1
layout(location = 3) in float fragWavePhase;   // -1 trough .. +1 crest, drives the shore surf
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
layout(set = 1, binding = 2) uniform sampler2D reflectionTex;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 params;     // x = seaLevel, y = quad size, zw unused
    vec4 params2;    // x = screen width, y = screen height, z = reflectionEnabled, w unused
} pc;

#include "water_common.glsl"

void main() {
    WaterSurfaceInput inp;
    inp.worldPos     = fragWorldPos;
    inp.camPos       = pc.camPosTime.xyz;
    inp.time         = pc.camPosTime.w;
    inp.screenSize   = pc.params2.xy;
    inp.fragDepthNdc = gl_FragCoord.z;
    inp.fragCoord    = gl_FragCoord.xy;
    inp.sideFace     = 0.0;   // the plane is always a top surface
    inp.minThickness = 0.0;   // no simulated column here — the depth buffer is the only source
    // The sea has no per-CELL sim flow, but it emphatically HAS a direction: the wind driving the
    // swell. Feeding that in as the flow direction makes the ripple detail and the whitecap pattern
    // travel WITH the waves. Leaving it at zero (the first version) froze the foam into a static
    // world-space pattern that read as painted-on diagonal stripes.
    // ⚑GROUND: strength 0.35 — the surface texture drifts at a fraction of the swell's own phase
    // speed, which is what wind ripples riding a larger wave actually do.
    inp.flowDir      = vec2(cos(pc.params.w), sin(pc.params.w));
    inp.flowStrength = (pc.params.z > 0.0001) ? 0.35 : 0.0;   // no drift on a flattened sea
    // WHITECAPS: foam on the steep faces of the swell, where a real wind sea breaks.
    inp.foam         = fragWaveFoam;
    inp.baseNormal   = normalize(fragWaveNormal);
    // SHORE SURF: waves break at ~2.56x the Gerstner amplitude of depth (H/d = 0.78 with
    // H = 2*amplitude). A flattened sea (amplitude 0) gets no surf, only the waterline rim.
    inp.wavePhase    = fragWavePhase;
    inp.breakDepth   = pc.params.z * 2.56;

    vec4 water = shadeWaterSurface(inp);

    // Dormant: planar scene reflection (re-enable once a correct reflection pass lands —
    // WaterSystemV3 Phase 5 favours screen-space reflection instead).
    if (pc.params2.z > 0.5) {
        vec2 screenUV = clamp(gl_FragCoord.xy / pc.params2.xy, vec2(0.001), vec2(0.999));
        vec3 N = waterRippleNormal(fragWorldPos.xz, pc.camPosTime.w);
        screenUV += N.xz * 0.03;
        screenUV = clamp(screenUV, vec2(0.001), vec2(0.999));
        vec3 V = normalize(pc.camPosTime.xyz - fragWorldPos);
        float ndv  = clamp(dot(V, N), 0.0, 1.0);
        float fres = clamp(0.02 + 0.98 * pow(1.0 - ndv, 5.0), 0.0, 1.0);
        water.rgb = mix(water.rgb, texture(reflectionTex, screenUV).rgb, 0.85 * fres);
    }

    outColor = water;
}
