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
layout(location = 4) in float fragFoam;   // G2
layout(location = 5) in vec2  fragFlow;   // G2
layout(location = 6) flat in float fragRipple;   // 22: ripple layer + 1 (0 = none)
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
    // 21.3: std140 is positional - the lit in-scatter needs occupancyBox / giProbeGrid (gi_field.glsl)
    vec4  skyBodyDirRadius[4];
    vec4  skyBodyDisc[4];
    vec4  skyBodyLitDir[4];
    vec4  skyBodyLight[4];
    int   skyBodyCount;
    ivec4 occupancyBox;
    vec4  giProbeGrid;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D refractionTex;
layout(set = 1, binding = 1) uniform sampler2D sceneDepthTex;
// 22 (WaterCore.md 22.4): the ripple layers - per layer geo = (world origin x, z, 1/pitch, -), meta = (offset, nx, nz,
// smooth); heights in metres, row-major per layer. L2 (43.2): fade = (viewer x, viewer z, near, far) - the ripples' weight by the
// HORIZONTAL distance to the viewer, a pure function of world position (far < 0 = none, weight 1).
// A12 (45): rest0 = the rest motion's (amplitude m (0 = off), wind direction rad (toward), wind m/s, patch m); rest1 = (reach far m
// (< 0 = none), its fade m, -, -).
layout(std430, set = 1, binding = 2) readonly buffer Ripples { vec4 fade; vec4 rest0; vec4 rest1; vec4 geo[8]; ivec4 meta[8]; float rh[]; } rip;
// WaterLocality::detail: 1 at or under near, 0 at or over far, a smoothstep between (far <= near: a hard reach at near)
float rippleWeight(vec2 xz) {
    const vec4 f = rip.fade;
    if (f.w < 0.0) return 1.0;
    const float d = length(xz - f.xy);
    if (d <= f.z) return 1.0;
    if (d >= f.w) return 0.0;
    const float t = (d - f.z) / (f.w - f.z);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}
float rippleAt(int L, int x, int z) {
    const ivec4 m = rip.meta[L];
    x = clamp(x, 0, m.y - 1); z = clamp(z, 0, m.z - 1);
    return rip.rh[m.x + x + m.y * z];
}
// The ripple slope (dr/dx, dr/dz) and height at world xz. FACETS (the owner's microcube look, default): the slope of
// the 1/9 m square the point lies in - constant over the square. Smooth (A/B): the bilinear surface's gradient.
vec3 rippleSample(int L, vec2 xz) {
    const vec4 g = rip.geo[L]; const ivec4 m = rip.meta[L];
    if (m.y <= 0) return vec3(0.0);
    const vec2 c = (xz - g.xy) * g.z;
    if (m.w == 0) {
        const ivec2 i = ivec2(floor(c));
        if (i.x < 0 || i.y < 0 || i.x >= m.y || i.y >= m.z) return vec3(0.0);
        const float dx = (rippleAt(L, i.x + 1, i.y) - rippleAt(L, i.x - 1, i.y)) * 0.5 * g.z;
        const float dz = (rippleAt(L, i.x, i.y + 1) - rippleAt(L, i.x, i.y - 1)) * 0.5 * g.z;
        return vec3(dx, dz, rippleAt(L, i.x, i.y));
    }
    const vec2 p = c - 0.5; const ivec2 i = ivec2(floor(p)); const vec2 t = p - vec2(i);
    const float h00 = rippleAt(L, i.x, i.y), h10 = rippleAt(L, i.x + 1, i.y), h01 = rippleAt(L, i.x, i.y + 1), h11 = rippleAt(L, i.x + 1, i.y + 1);
    const float dx = mix(h10 - h00, h11 - h01, t.y) * g.z, dz = mix(h01 - h00, h11 - h10, t.x) * g.z;
    return vec3(dx, dz, mix(mix(h00, h10, t.x), mix(h01, h11, t.x), t.y));
}

// A12 (WaterCore.md 45) - the REST MOTION: a breeze's cat's-paws on water at rest, FACETED on the world's 1/9 m lattice (the slope
// of the square the point lies in, as the ripples' facets). A pure function of the world square and time. MIRRORED line for line by
// engine/include/core/water/WaterRestMotion.h (the tests hold the screen to it) - change both together.
const float kRestPitch = 1.0 / 9.0;
const float kRestAngle[3] = float[3](0.0, 0.5, -0.4);
const float kRestLambda[3] = float[3](0.85, 0.5, 0.3);
const float kRestShare[3] = float[3](0.55, 0.3, 0.15);
const float kRestOffset[3] = float[3](0.0, 1.7, 4.1);
uint restHash(int x, int y, int z) {
    uint h = uint(x) * 73856093u ^ uint(y) * 19349663u ^ uint(z) * 83492791u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
float restRnd(int x, int y, int z) { return float(restHash(x, y, z) & 0xFFFFFFu) / 16777216.0; }
float restNoise(vec3 p) {
    const vec3 f = floor(p); const ivec3 i = ivec3(f);
    vec3 t = p - f; t = t * t * (3.0 - 2.0 * t);
    const float c00 = mix(restRnd(i.x, i.y, i.z), restRnd(i.x + 1, i.y, i.z), t.x), c10 = mix(restRnd(i.x, i.y + 1, i.z), restRnd(i.x + 1, i.y + 1, i.z), t.x);
    const float c01 = mix(restRnd(i.x, i.y, i.z + 1), restRnd(i.x + 1, i.y, i.z + 1), t.x), c11 = mix(restRnd(i.x, i.y + 1, i.z + 1), restRnd(i.x + 1, i.y + 1, i.z + 1), t.x);
    return mix(mix(c00, c10, t.y), mix(c01, c11, t.y), t.z);
}
float restPhaseSpeed(float l) { return sqrt(9.81 * l / 6.2831853 + 6.2831853 * 0.0728 / (1000.0 * l)); }
float restEnvelope(vec2 xz, float t) {
    const float a = rip.rest0.y, drift = 0.3 * rip.rest0.z * t;
    const vec2 uv = (xz - vec2(cos(a), sin(a)) * drift) / rip.rest0.w;
    return 0.3 + 0.7 * smoothstep(0.35, 0.75, restNoise(vec3(uv, t / 8.0)));   // kEnvelopeFloor 0.3: between patches, a little motion
}
float restHeight(vec2 xz, float t) {
    const float calm = min(rip.rest0.z / 2.0, 1.0);
    if (calm <= 0.0) return 0.0;
    const float a = rip.rest0.y;
    float h = 0.0;
    for (int k = 0; k < 3; ++k) {
        const vec2 d = vec2(cos(a + kRestAngle[k]), sin(a + kRestAngle[k]));
        const float kw = 6.2831853 / kRestLambda[k];
        h += kRestShare[k] * sin(kw * (xz.x * d.x + xz.y * d.y - restPhaseSpeed(kRestLambda[k]) * t) + kRestOffset[k]);
    }
    return rip.rest0.x * calm * restEnvelope(xz, t) * h;
}
// the facet slope (dh/dx, dh/dz) of the 1/9 m square the point lies in
vec2 restSlope(vec2 xz, float t) {
    const ivec2 i = ivec2(floor(xz * 9.0));
    const vec2 c = (vec2(i) + 0.5) * kRestPitch;
    const float sx = (restHeight(c + vec2(kRestPitch, 0.0), t) - restHeight(c - vec2(kRestPitch, 0.0), t)) * (0.5 / kRestPitch);
    const float sz = (restHeight(c + vec2(0.0, kRestPitch), t) - restHeight(c - vec2(0.0, kRestPitch), t)) * (0.5 / kRestPitch);
    return vec2(sx, sz);
}

// Must match water_surface.vert's block exactly. 128 bytes.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime;
    vec4 screen;
    vec4 look0;      // G3: tint.rgb (x < 0 = unset), clarity m (0 = derived)
    vec4 look1;      // G3: turbidity (< 0 = derived), roughness (< 0 = derived); z: 1 = 22.12 horizon guard OFF; w: default clarity m (25.26)
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
    inp.sideFace     = fragSide > 1.5 ? 0.0 : fragSide;   // 21: side 2 = a droplet cube
    inp.droplet      = fragSide > 1.5 ? 1.0 : 0.0;
    inp.minThickness = fragDepth;          // the solver knows how deep this run is
    // G2: the solver's surface velocity drives the ripple advection and the whitewater streaks, its
    // foam field (bore fronts, convergence, the swash tongue) the whitewater itself.
    const float speed = length(fragFlow);
    inp.flowDir      = speed > 1e-3 ? fragFlow / speed : vec2(0.0);
    inp.flowStrength = clamp(speed / 2.0, 0.0, 1.0);   // 2 m/s = full streaking
    inp.foam         = clamp(fragFoam, 0.0, 1.0);
    // The mesh normal is the water's real shape. A lateral face is a vertical wall of water; its
    // normal points out of the water.
    vec3 n = normalize(fragNormal);
    if (dot(n, pc.camPosTime.xyz - fragWorldPos) < 0.0) n = -n;   // face the viewer (an underwater camera sees the underside)
    const int dbg = int(pc.screen.z + 0.5);   // water_render_core {debug: 0..5}
    // 22: the ripple layer tilts the top surface (not side faces, not droplet cubes)
    const int rl = int(fragRipple + 0.5) - 1;
    vec3 rs = vec3(0.0);
    if (rl >= 0 && rl < 8 && fragSide < 0.5) {
        rs = rippleSample(rl, fragWorldPos.xz) * rippleWeight(fragWorldPos.xz);   // L2: tilt and height faded alike
        if (n.y > 0.0) n = normalize(n + vec3(-rs.x, 0.0, -rs.y));
    }
    // A12 (45): the rest motion on every top face (awake and recorded water alike; not side faces, not droplets). Its weight: the
    // facet's size on screen (gone under ~1.5 px, as waterRippleNormal's octaves - a facet smaller than a pixel only sparkles) and
    // the reach (rest1.x > 0: an L1 detail fade over rest1.y m). Off (rest0.x = 0) the normal is untouched, bit for bit.
    const float restPx = max(length(dFdx(fragWorldPos)), length(dFdy(fragWorldPos)));   // derivatives in uniform control flow
    vec2 rsl = vec2(0.0); float restH = 0.0;
    if (rip.rest0.x > 0.0 && fragSide < 0.5) {
        float w = smoothstep(1.5, 4.0, kRestPitch / max(restPx, 1e-5));
        if (rip.rest1.x > 0.0) w *= 1.0 - smoothstep(rip.rest1.x - rip.rest1.y, rip.rest1.x, length(fragWorldPos.xz - pc.camPosTime.xz));
        if (w > 0.0) {
            rsl = restSlope(fragWorldPos.xz, pc.camPosTime.w) * w;
            if (dbg == 10) restH = restHeight((vec2(ivec2(floor(fragWorldPos.xz * 9.0))) + 0.5) * kRestPitch, pc.camPosTime.w) * w;
        }
    }
    if (n.y > 0.0 && (rsl.x != 0.0 || rsl.y != 0.0)) n = normalize(n + vec3(-rsl.x, 0.0, -rsl.y));
    if (dbg == 7) { outColor = vec4(vec3(clamp(0.5 + rs.z * 25.0, 0.0, 1.0)), 1.0); return; }   // 22 tap: ripple height, +-2 cm = black / white (after L2's fade)
    // A12 taps: 10 = the rest height at the facet's centre, +-2 mm = black / white; 11 = the rest facet's slope, 0..0.1 = black..white
    if (dbg == 10) { outColor = vec4(vec3(clamp(0.5 + restH * 250.0, 0.0, 1.0)), 1.0); return; }
    if (dbg == 11) { outColor = vec4(vec3(clamp(length(rsl) * 10.0, 0.0, 1.0)), 1.0); return; }
    // L2 (43.3 item 6): the faded slope's magnitude, 0..1 = slope 0..0.5 (the pattern's uniform tilt reads as one grey per distance;
    // 8 is water_common.glsl's reflected-ray tap)
    if (dbg == 9) { outColor = vec4(vec3(clamp(length(rs.xy) * 2.0, 0.0, 1.0)), 1.0); return; }
    if (dbg == 1) { outColor = vec4(n * 0.5 + 0.5, 1.0); return; }   // the mesh normal as colour: shape only
    if (dbg == 6) { outColor = vec4(inp.foam, inp.flowStrength, 0.0, 1.0); return; }   // G2 tap: foam (red) and flow strength (green) as fed by the solver
    inp.baseNormal   = n;
    inp.wavePhase    = 0.0;
    inp.breakDepth   = 0.0;
    inp.restLevelY   = -1e9;               // the solver decides the level per sub-column
    // G3: the body's look (one draw per field). Unset knobs keep the derived values below.
    inp.turbidity    = pc.look1.x >= 0.0 ? pc.look1.x : 0.0;
    inp.roughness    = (pc.look1.y >= 0.0 ? pc.look1.y : 0.35) + 0.45 * inp.flowStrength;   // base: a pond is calmer than the sea's shipped detail (17.3); moving water is choppier (G2)
    inp.tint         = max(pc.look0.rgb, vec3(0.0));
    inp.tintSet      = pc.look0.r >= 0.0 ? 1.0 : 0.0;
    // 25.26 M: a body's own clarity wins; else the simulated water's DEFAULT (look1.w, `water_render_core {default_clarity}`,
    // 5 m Secchi - slightly murky, the owner 2026-10-12); 0 there = the derived Pope & Fry clear water (~10.8 m), the old look
    inp.clarity      = pc.look0.a > 0.0 ? pc.look0.a : max(pc.look1.w, 0.0);
    inp.viewProj     = pc.viewProj;
    inp.ssr          = 0.0;
    inp.debugMode    = dbg;
    inp.horizonGuard = pc.look1.z > 0.5 ? 0.0 : 1.0;   // 22.12 B2: water_render_core {horizon_guard: false} sets look1.z = 1 (A/B only)
    inp.shoreFoam    = 0.0;                 // no shoreline model on simulated water: its edge IS the mesh; foam comes from the solver (F2)
    inp.scatterLit   = pc.screen.w > 0.5 ? 0.0 : 1.0;   // 21.3: water_render_core {scatter: "legacy"} sets w = 1 (A/B only)

    outColor = shadeWaterSurface(inp);
}
