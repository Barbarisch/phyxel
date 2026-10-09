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

// Must match water_surface.frag's block exactly (one push-constant range, both stages). 96 bytes.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 screen;     // xy = screen size (px), zw unused
} pc;

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out float fragDepth;
layout(location = 2) out float fragSide;
layout(location = 3) out vec3  fragNormal;
layout(location = 4) out float fragFoam;
layout(location = 5) out vec2  fragFlow;

void main() {
    fragWorldPos = inPos;
    fragDepth    = inDepth;
    fragSide     = inSide;
    fragNormal   = inNormal;
    fragFoam     = inFoam;
    fragFlow     = inFlow;
    gl_Position  = pc.viewProj * vec4(inPos, 1.0);
}
