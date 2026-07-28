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

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 screen;     // xy = screen size (px), zw unused
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
    // already built from the sim's per-corner heights, so the shading normal starts flat.
    inp.baseNormal   = vec3(0.0, 1.0, 0.0);

    outColor = shadeWaterSurface(inp);
}
