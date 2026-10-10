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
// smooth); heights in metres, row-major per layer.
layout(std430, set = 1, binding = 2) readonly buffer Ripples { vec4 geo[8]; ivec4 meta[8]; float rh[]; } rip;
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

// Must match water_surface.vert's block exactly. 128 bytes.
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime;
    vec4 screen;
    vec4 look0;      // G3: tint.rgb (x < 0 = unset), clarity m (0 = derived)
    vec4 look1;      // G3: turbidity (< 0 = derived), roughness (< 0 = derived)
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
        rs = rippleSample(rl, fragWorldPos.xz);
        if (n.y > 0.0) n = normalize(n + vec3(-rs.x, 0.0, -rs.y));
    }
    if (dbg == 7) { outColor = vec4(vec3(clamp(0.5 + rs.z * 25.0, 0.0, 1.0)), 1.0); return; }   // 22 tap: ripple height, +-2 cm = black / white
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
    inp.clarity      = max(pc.look0.a, 0.0);
    inp.viewProj     = pc.viewProj;
    inp.ssr          = 0.0;
    inp.debugMode    = dbg;
    inp.shoreFoam    = 0.0;                 // no shoreline model on simulated water: its edge IS the mesh; foam comes from the solver (F2)
    inp.scatterLit   = pc.screen.w > 0.5 ? 0.0 : 1.0;   // 21.3: water_render_core {scatter: "legacy"} sets w = 1 (A/B only)

    outColor = shadeWaterSurface(inp);
}
