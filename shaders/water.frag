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
// Water-layer per-column basin levels (water-layer P1) — see water.vert.
layout(set = 1, binding = 3) uniform sampler2D hydroLevelTex;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 params;     // x = seaLevel, y = hydro grid originX, z = wave amplitude, w = wind dir (rad)
    vec4 params2;    // x = screen width, y = screen height, z = reflectionEnabled, w = wave length
    vec4 params3;    // x = core spacing, y = core half-extent, z = hydro originZ, w = hydro invCellSize (0 = flat)
} pc;

// Same lookup as water.vert (kept in sync by hand — the include is frag-only).
//
// Water Appearance v4 W1: the texture is now RGBA — R = level, G = wave energy (read by the vertex
// stage), B = turbidity, A = roughness. The profile is fetched PER PIXEL here, exactly like the
// basin level already was, rather than interpolated from the vertex stage: the texture is NEAREST
// because basins are piecewise-constant, and a varying would smear one body's profile across the
// divide into its neighbour.
//
// FALLBACKS ARE THE NEUTRAL PROFILE (turbidity 0, roughness 1) — flat-sea mode, outside the baked
// region, and dry columns all take today's look exactly. A world with no hydrology bake has no
// water bodies, so it has no per-body profile by construction; that is correct, not a gap.
// ⚑`noWater` KILLS A PHANTOM SEA (docs/WaterAsWorldData.md). "Not wet" was collapsing to "return
// sea level", so a column the bake calls DRY still got a sheet drawn at y = seaLevel: an infinite,
// edgeless, camera-following plane sitting UNDER the entire landscape, visible whenever you got
// below the terrain or looked where terrain was not drawn. The only thing hiding it was the
// depth-buffer dry-land gate — and that gate cannot fire where there is no depth.
//
// "NOT WET" IS THREE DIFFERENT THINGS and only one of them means there is no water:
//   * flat-sea mode (no bake at all) -> implicit sea; authored worlds depend on it.  KEEP.
//   * outside the baked region       -> the open ocean beyond the grid's reach.      KEEP.
//   * DRY COLUMN INSIDE THE BAKE     -> land. No water here at all.                  DISCARD.
// Conflating the third case with the first two is what put an ocean under the world.
float basinLevelAt(vec2 worldXZ, out float turbidity, out float roughness, out float noWater) {
    turbidity = 0.0;
    roughness = 1.0;
    noWater   = 0.0;
    float invCell = pc.params3.w;
    if (invCell <= 0.0) return pc.params.x;                  // flat-sea mode: implicit sea
    vec2 cellF = (worldXZ - vec2(pc.params.y, pc.params3.z)) * invCell;
    ivec2 sz = textureSize(hydroLevelTex, 0);
    if (cellF.x < 0.0 || cellF.y < 0.0 || cellF.x >= float(sz.x) || cellF.y >= float(sz.y))
        return pc.params.x;                                  // off-grid: open ocean
    vec4 t = texelFetch(hydroLevelTex, ivec2(cellF), 0);
    if (t.r < -1e5) { noWater = 1.0; return pc.params.x; }   // dry land inside the bake: NO water
    turbidity = t.b;
    roughness = t.a;
    return t.r;
}

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
    //
    // FLOOR OF 2.5 VOXELS — the criterion alone is not enough here. Once the swell was scaled to
    // this world (amplitude 0.30) the physical break depth came out at 0.77 voxels, i.e. narrower
    // than a single block, so the surf existed but was far too thin to see and the shore looked
    // exactly as bare as before. Terrain here is quantised to whole voxels, so a surf zone has to be
    // several voxels deep to land on more than one step of the seabed.
    inp.wavePhase    = fragWavePhase;
    inp.breakDepth   = max(pc.params.z * 2.56, 2.5);
    // Ground above the UNDISTURBED rest level is dry land: a wave crest must never be drawn
    // climbing it, however high the swell happens to lift the sheet there. Re-sampled PER PIXEL
    // (not taken from the vertex) so the stretched wall quads at basin rims gate against their
    // own column's level and vanish — a divide's terrain sits above both basins' levels by
    // definition (water-layer P1).
    float noWaterHere;
    inp.restLevelY   = basinLevelAt(fragWorldPos.xz, inp.turbidity, inp.roughness, noWaterHere);
    // Dry land inside the baked region has NO water, so nothing is drawn — full stop, and without
    // consulting the depth buffer. This is the terrain deciding, which is the whole governing rule:
    // the bake says this column holds nothing, so nothing is rendered, whether or not any geometry
    // happens to be in front of it this frame.
    if (noWaterHere > 0.5) discard;
    // SSR (v4 W4). params2.z used to be `reflectionEnabled` for a PLANAR reflection branch that was
    // permanently dormant (RenderCoordinator hardcoded it off because the shared mirror pass is
    // broken). The flag is now the SSR toggle — same plumbing, a reflection that actually works on
    // displaced water. viewProj is the water's own ABSOLUTE-space matrix (ubo.viewProj is
    // camera-relative and would march the ray in the wrong frame).
    inp.viewProj     = pc.viewProj;
    inp.ssr          = pc.params2.z;

    // RIM-WALL KILL (water-layer P1). Where adjacent clipmap vertices land in basins at
    // different levels (lake rim, lake→dry falloff), the connecting quad is a vertical wall
    // of "water" spanning the two levels. The depth-based dry-land gate inside the shade
    // function only catches wall pixels with terrain behind them; against sky — or when the
    // wall passes near the camera and fills the whole frame — it never fires. But every
    // real water pixel satisfies a property no wall pixel does: its surface lies within one
    // wave excursion of ITS OWN column's rest level. Wall fragments interpolate Y across
    // the two basins' levels, so almost all of them land far outside their column's band
    // (an above-only test was not enough: the half of the wall hanging over the HIGH basin's
    // columns sits below that basin's level and survived it). Max Gerstner excursion =
    // amp * (1 + .52 + .28 + .70) = 2.5 * amp, + 0.5 slack for the lake amp scale.
    float maxCrest = pc.params.z * 2.5 + 0.5;
    if (abs(fragWorldPos.y - inp.restLevelY) > maxCrest) discard;

    // The planar-reflection branch that used to live here is GONE (v4 W4). It sampled
    // `reflectionTex` from the shared mirror pass, was permanently disabled, and would have been
    // wrong on Gerstner-displaced water anyway (planar reflection assumes a flat mirror plane).
    // Screen-space reflection replaces it inside shadeWaterSurface, where it composes correctly
    // with Fresnel and the ripple normal instead of being blended over the finished colour.
    outColor = shadeWaterSurface(inp);
}
