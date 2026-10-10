#version 450
//
// water_surface.vert — WaterCore Phase F (docs/WaterCore.md 17): the simulated water's own surface
// mesh, built on the CPU from the volume's surface field (WaterSurfaceMesh.cpp) at the volume's
// own cell resolution. Vertices arrive in ABSOLUTE world space with their face normal, the run's
// thickness and a top/side flag; this stage only projects them.
//
layout(location = 0) in vec3  inPos;      // world
layout(location = 1) in float inDepth;    // the run's thickness (m)
layout(location = 2) in vec3  inNormal;   // world
layout(location = 3) in float inSide;     // 0 = top face, 1 = lateral face
layout(location = 4) in float inFoam;     // G2: 0..1 whitewater from the solver
layout(location = 5) in vec2  inFlow;     // G2: surface velocity x, z (m/s) from the solver
layout(location = 6) in float inRipple;   // 22: the field's ripple layer + 1 (0 = none)

// Must match water_surface.frag's block exactly (one push-constant range, both stages). 128 bytes.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 screen;     // xy = screen size (px), z = debug tap, w = 1 for the pre-21.3 constant in-scatter (A/B only)
    vec4 look0;      // G3: tint.rgb (x < 0 = unset), clarity m (0 = derived)
    vec4 look1;      // G3: turbidity (< 0 = derived), roughness (< 0 = derived)
} pc;

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out float fragDepth;
layout(location = 2) out float fragSide;
layout(location = 3) out vec3  fragNormal;
layout(location = 4) out float fragFoam;
layout(location = 5) out vec2  fragFlow;
layout(location = 6) flat out float fragRipple;

void main() {
    fragWorldPos = inPos;
    fragDepth    = inDepth;
    fragSide     = inSide;
    fragNormal   = inNormal;
    fragFoam     = inFoam;
    fragFlow     = inFlow;
    fragRipple   = inRipple;
    gl_Position  = pc.viewProj * vec4(inPos, 1.0);
}
