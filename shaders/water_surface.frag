#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"   // shared lighting model
//
// water_surface.frag — WaterCore Phase F (docs/WaterCore.md 17): shading of the simulated water's
// own surface mesh. All the shading lives in water_common.glsl, shared with the sea sheet, so the
// two cannot drift apart; this stage supplies the per-fragment inputs: the mesh's own normal (the
// height field's shape IS the water's shape - a wake, a splash ring, a slosh are geometry, not a
// ripple texture), the run's thickness as the absorption floor, and the top/side flag.
//
// Outputs LINEAR HDR; the one tone map lives in post_process.frag (see water_cell.frag's note).
//
layout(location = 0) in vec3  fragWorldPos;
layout(location = 1) in float fragDepth;
layout(location = 2) in float fragSide;
layout(location = 3) in vec3  fragNormal;
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
    vec3  cameraPosition;
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
} ubo;

layout(set = 1, binding = 0) uniform sampler2D refractionTex;
layout(set = 1, binding = 1) uniform sampler2D sceneDepthTex;

// Must match water_surface.vert's block exactly. 96 bytes.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime;
    vec4 screen;
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
    inp.minThickness = fragDepth;          // the solver knows how deep this run is
    inp.flowDir      = vec2(0.0);          // F2: surface velocity from the solver
    inp.flowStrength = 0.0;
    inp.foam         = 0.0;                // F2: divergence of the surface velocity
    // The mesh normal is the water's real shape. A lateral face is a vertical wall of water; its
    // normal points out of the water.
    vec3 n = normalize(fragNormal);
    if (dot(n, pc.camPosTime.xyz - fragWorldPos) < 0.0) n = -n;   // face the viewer (an underwater camera sees the underside)
    const int dbg = int(pc.screen.z + 0.5);   // water_render_core {debug: 0..5}
    if (dbg == 1) { outColor = vec4(n * 0.5 + 0.5, 1.0); return; }   // the mesh normal as colour: shape only
    inp.baseNormal   = n;
    inp.wavePhase    = 0.0;
    inp.breakDepth   = 0.0;
    inp.restLevelY   = -1e9;               // the solver decides the level per sub-column
    inp.turbidity    = 0.0;                // neutral profile (per-body profiles arrive with F2)
    inp.roughness    = 0.35;                // fine ripple amplitude: a pond is calmer than the sea's shipped detail (a judgement, recorded in 17.3)
    inp.viewProj     = pc.viewProj;
    inp.ssr          = 0.0;
    inp.debugMode    = dbg;
    inp.shoreFoam    = 0.0;                 // no shoreline model on simulated water: its edge IS the mesh; foam comes from the solver (F2)

    outColor = shadeWaterSurface(inp);
}
