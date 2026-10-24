#version 450
#extension GL_GOOGLE_include_directive : require
//
// water_surface_pull.vert — WaterCore 29 S1: the simulated water's surface drawn straight from the device (docs/WaterCore.md 29).
// No vertex buffer: each vertex is pulled from the volume's dense surface field and the corners / meta / extras list that
// wc_surface_mesh.comp made in the water's own call (water_surface_pull.glsl - the same function the parity test expands).
//   firstInstance 0: every column's first-run top quad - vertex v of column v / 6 (a column with no run: a degenerate triangle)
//   firstInstance 1: the extras list - instance i + 1 is entry i (a top of a higher run or a side face), vertex v its corner
// The outputs are water_surface.vert's exactly, so water_surface.frag (and the look) is untouched.
//
#define WSP_SET 2
#define WSP_B_SURF 0
#define WSP_B_CORNERS 1
#define WSP_B_META 2
#define WSP_MESH_QUAL readonly
#define WSP_SURF_QUAL readonly
#include "water_surface_pull.glsl"
layout(std430, set = 2, binding = 3) readonly buffer WspList { uint wspList[]; };

// Must match water_surface.frag's block for the first 128 bytes (one range, both stages); the vertex stage's own range follows.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime;
    vec4 screen;
    vec4 look0;
    vec4 look1;
    ivec4 field;     // origin x, origin z (cells), nx, nz
    vec4 field2;     // h, the ripple layer + 1 (0 = none), -, -
} pc;

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out float fragDepth;
layout(location = 2) out float fragSide;
layout(location = 3) out vec3  fragNormal;
layout(location = 4) out float fragFoam;
layout(location = 5) out vec2  fragFlow;
layout(location = 6) flat out float fragRipple;

const int kCorner[6] = int[6](0, 1, 2, 0, 2, 3);

void main() {
    WspGeo g; g.ox = pc.field.x; g.oz = pc.field.y; g.nx = pc.field.z; g.nz = pc.field.w; g.h = pc.field2.x;
    uint c; int r, face, v;
    if (gl_InstanceIndex == 0) { c = uint(gl_VertexIndex) / 6u; v = gl_VertexIndex % 6; r = 0; face = 0; }
    else {
        const uint e = wspList[gl_InstanceIndex - 1];
        c = e & 0xFFFFFFu; r = int((e >> 24) & 3u); face = int((e >> 26) & 7u); v = gl_VertexIndex;
    }
    vec3 p[4]; vec3 nrm; float depth, side, foam; vec2 flow;
    const bool ok = c < uint(g.nx) * uint(g.nz) && wspQuad(g, c, r, face, p, nrm, depth, side, foam, flow);
    const vec3 pos = p[kCorner[v]];
    fragWorldPos = pos;
    fragDepth    = depth;
    fragSide     = side;
    fragNormal   = nrm;
    fragFoam     = foam;
    fragFlow     = flow;
    fragRipple   = pc.field2.y;
    gl_Position  = ok ? pc.viewProj * vec4(pos, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);   // no quad: outside the clip volume (culled)
}
