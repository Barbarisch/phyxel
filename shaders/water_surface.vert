#version 450
#extension GL_GOOGLE_include_directive : require
//
// water_surface.vert — WaterCore Phase F (docs/WaterCore.md 17): the simulated water's own surface
// mesh, built on the CPU from the volume's surface field (WaterSurfaceMesh.cpp) at the volume's
// own cell resolution. Vertices arrive in ABSOLUTE world space with their face normal, the run's
// thickness and a top/side flag; this stage projects them.
// §55.5 (W6.2b): a vertex of the record's SEA (its spare float carries the sea's quantised depth q > 0) is raised by the swell the
// shore band is driven by - the same function (sea_swell.glsl, the band's own), the same clock, depth-limited by the same depth - so
// the band's moving water meets the sea beside it with no step. Appearance only; faded out over [reach - fade, reach] from the viewer.
//
#include "sea_swell.glsl"

layout(location = 0) in vec3  inPos;      // world
layout(location = 1) in float inDepth;    // the run's thickness (m)
layout(location = 2) in vec3  inNormal;   // world
layout(location = 3) in float inSide;     // 0 = top face, 1 = lateral face
layout(location = 4) in float inFoam;     // G2: 0..1 whitewater from the solver
layout(location = 5) in vec2  inFlow;     // G2: surface velocity x, z (m/s) from the solver
layout(location = 6) in float inRipple;   // 22: the field's ripple layer + 1 (0 = none) + 16 x the sea's q (§55.5, waterVertexPad)

// Must match water_surface.frag's block exactly for the first 128 bytes (one range, both stages); §55.5: the vertex stage's own
// range follows (the sea's swell and its draw).
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 screen;     // xy = screen size (px), z = debug tap, w = 1 for the pre-21.3 constant in-scatter (A/B only)
    vec4 look0;      // G3: tint.rgb (x < 0 = unset), clarity m (0 = derived)
    vec4 look1;      // G3: turbidity (< 0 = derived), roughness (< 0 = derived)
    vec4 sea0;       // §55.5: amplitude (m), wavelength (m), wind (rad), the band's clock (s)
    vec4 sea1;       // §55.5: reach (m), fade (m), on (1 / 0), -
} pc;

layout(location = 0) out vec3  fragWorldPos;
layout(location = 1) out float fragDepth;
layout(location = 2) out float fragSide;
layout(location = 3) out vec3  fragNormal;
layout(location = 4) out float fragFoam;
layout(location = 5) out vec2  fragFlow;
layout(location = 6) flat out float fragRipple;

// 58.11 (the edge blend; engine/include/core/water/EdgeBlend.h's edgeBlendLift LINE FOR LINE): the record's top within M metres of a
// live volume's box is lifted toward the volume's drawn edge (w = 1 at the box, 0 at M; draw only). The Ripples buffer's prefix up to
// the edge block (water_surface.frag declares the whole of it).
layout(std430, set = 1, binding = 2) readonly buffer Ripples { vec4 fade; vec4 geo[8]; ivec4 meta[8]; vec4 far[8];
    vec4 edgeParams; vec4 edgeBox[8]; ivec4 edgeN[8]; float edgeTops[3072]; } rip;
const float kEdgeDry = -1.0e30;
float edgeTopAt(int b, int s, float a) {
    const int n = rip.edgeN[b][s];
    if (n <= 0) return kEdgeDry;
    const float c = clamp(a - 0.5, 0.0, float(n - 1));
    const int i = min(int(c), n - 1), j = min(i + 1, n - 1);
    const float t0 = rip.edgeTops[(b * 4 + s) * 96 + i], t1 = rip.edgeTops[(b * 4 + s) * 96 + j];
    if (t0 <= kEdgeDry * 0.5 || t1 <= kEdgeDry * 0.5) return t0 > kEdgeDry * 0.5 ? t0 : t1;
    return t0 + (t1 - t0) * (c - float(i));
}
float edgeBlendLift(vec3 v) {
    const float M = rip.edgeParams.x;
    const int nb = int(rip.edgeParams.y + 0.5);
    if (M <= 0.0 || nb <= 0) return 0.0;
    float best = M, lift = 0.0;
    for (int b = 0; b < nb && b < 8; ++b) {
        const vec2 lo = rip.edgeBox[b].xy, hi = rip.edgeBox[b].zw;
        const bool inX = v.x >= lo.x && v.x <= hi.x, inZ = v.z >= lo.y && v.z <= hi.y;
        if (inX && inZ) return 0.0;
        const float cx = clamp(v.x, lo.x, hi.x), cz = clamp(v.z, lo.y, hi.y);
        const float d = length(vec2(v.x - cx, v.z - cz));
        if (d >= best) continue;
        int s; float a;
        if (inX) { s = v.z < lo.y ? 0 : 1; a = cx - lo.x; }
        else if (inZ) { s = v.x < lo.x ? 2 : 3; a = cz - lo.y; }
        else { s = v.z < lo.y ? 0 : 1; a = cx - lo.x; }
        const float top = edgeTopAt(b, s, a);
        if (top <= kEdgeDry * 0.5) continue;
        const float t = clamp(d / M, 0.0, 1.0), w = 1.0 - t * t * (3.0 - 2.0 * t);
        best = d; lift = w * (top - v.y);
    }
    return lift;
}

// SeaDraw.h seaDrawFade, rule for rule
float seaDrawFadeG(float dist) {
    if (pc.sea1.z < 0.5) return 0.0;
    const float e0 = pc.sea1.x - pc.sea1.y, e1 = pc.sea1.x;
    if (e1 <= e0) return dist < e1 ? 1.0 : 0.0;
    const float t = clamp((dist - e0) / (e1 - e0), 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

// SeaDraw.h seaDrawnOffset, rule for rule
float seaDrawnOffsetG(float x, float z, uint q, float fade) {
    if (q == 0u || fade <= 0.0 || pc.sea0.x <= 0.0) return 0.0;
    float h; vec2 u;
    seaSwellColumnG(pc.sea0.x, pc.sea0.y, pc.sea0.z, x, z, pc.sea0.w, float(q) * (1.0 / 32.0), h, u);
    return fade * h;
}

void main() {
    const uint packed = uint(inRipple + 0.5);
    const uint q = (packed >> 4u) & 255u;
    vec3 p = inPos;
    vec3 n = inNormal;
    if (q != 0u) {
        const float fade = seaDrawFadeG(length(p.xz - pc.camPosTime.xz));
        const float y0 = seaDrawnOffsetG(p.x, p.z, q, fade);
        p.y += y0;
        if (inSide < 0.5 && fade > 0.0) {   // a top face: the swell's slope (the same depth across the step: the vertex's own)
            const float e = 0.05;
            const float dx = (seaDrawnOffsetG(p.x + e, p.z, q, fade) - y0) / e, dz = (seaDrawnOffsetG(p.x, p.z + e, q, fade) - y0) / e;
            n = normalize(vec3(-dx, 1.0, -dz));
        }
    }
    if (inSide < 0.5) p.y += edgeBlendLift(p);   // 58.11: a top face only (a side face's foot stays on the bed)
    fragWorldPos = p;
    fragDepth    = inDepth;
    fragSide     = inSide;
    fragNormal   = n;
    fragFoam     = inFoam;
    fragFlow     = inFlow;
    fragRipple   = float(packed & 15u);
    gl_Position  = pc.viewProj * vec4(p, 1.0);
}
