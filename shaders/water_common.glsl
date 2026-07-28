//
// water_common.glsl — shading shared by the flat sea plane (water.frag) and the per-cell sim
// surface (water_cell.frag), so the two renderers cannot drift apart visually.
//
// WaterSystemV3 Phase 1: water now draws in its OWN pass after the scene pass, so it can sample
// the scene it is compositing over. That is what turns it from a blue decal into a volume:
//
//   * REFRACTION   — the scene behind the surface, sampled with a normal-driven UV offset.
//   * ABSORPTION   — Beer-Lambert extinction over the real path length through the water
//                    (scene depth minus surface depth), so shallows read pale and depths read
//                    deep blue-green, and grazing views darken naturally.
//   * SOFT SHORE   — alpha fades out as that path length → 0, dissolving the hard waterline.
//   * REAL LIGHT   — sun direction/colour and ambient come from the shared scene UBO, so water
//                    tracks the day/night cycle instead of a hardcoded midday sun.
//
// Requires the includer to declare, before including:
//   - `ubo` (the shared scene UniformBufferObject, set 0 binding 0) — needs view/proj/
//     sunDirection/sunColor/ambientLight
//   - sampler2D refractionTex (set 1, binding 0) — HALF-RES copy of the scene colour
//   - sampler2D sceneDepthTex (set 1, binding 1) — the scene depth buffer
//

// ---------------------------------------------------------------------------------------------
// Depth reconstruction
// ---------------------------------------------------------------------------------------------

// Linear distance along the camera's forward axis for a depth-buffer value, derived from the
// projection matrix itself so it is correct for EITHER clip convention. (This engine builds
// projections with glm::perspective and does NOT define GLM_FORCE_DEPTH_ZERO_TO_ONE, so NDC z is
// the OpenGL [-1,1] form clipped to Vulkan's [0,1] — deriving from P avoids hardcoding that.)
//   d = (P[2][2]*z_v + P[3][2]) / (-z_v)   =>   -z_v = P[3][2] / (d + P[2][2])
float waterLinearDepth(float d, mat4 P) {
    return P[3][2] / (d + P[2][2]);
}

// Camera forward (world space) from the view matrix. view[i][j] is column i, row j; row 2 of the
// rotation is the camera's +Z (backward) axis, so forward is its negation.
vec3 waterCamForward(mat4 V) {
    return normalize(-vec3(V[0][2], V[1][2], V[2][2]));
}

// ---------------------------------------------------------------------------------------------
// Surface normal
// ---------------------------------------------------------------------------------------------

// Animated ripple normal: two crossing directional waves. Phase 2 replaces this with scrolling
// normal-map octaves + distance LOD.
vec3 waterRippleNormal(vec2 p, float t) {
    vec2 d1 = normalize(vec2( 1.0, 0.4));
    vec2 d2 = normalize(vec2(-0.6, 1.0));
    float a1 = 0.06, a2 = 0.04;     // amplitudes
    float f1 = 0.35, f2 = 0.6;      // spatial frequencies
    float s1 = 0.9,  s2 = 1.3;      // temporal speeds

    float phase1 = dot(p, d1) * f1 + t * s1;
    float phase2 = dot(p, d2) * f2 + t * s2;

    float dx = a1 * f1 * d1.x * cos(phase1) + a2 * f2 * d2.x * cos(phase2);
    float dz = a1 * f1 * d1.y * cos(phase1) + a2 * f2 * d2.y * cos(phase2);
    return normalize(vec3(-dx, 1.0, -dz));
}

// FLOWING ripple normal (WaterSystemV3 Phase 3). Same wave pair, but the sample point is ADVECTED
// along the flow direction over time and COMPRESSED across it — so ripples visibly travel
// downstream and stretch into streaks, which is what separates a river from a pond. With
// strength 0 this is bit-identical to the still version above, so calm water is unchanged.
//   flowDir  — normalized horizontal flow (0 if still)
//   strength — 0..1 how hard it's moving
// SEAM DISCIPLINE — read before changing this. flowDir/strength are PER-INSTANCE, i.e. constant
// across a cell's quad and DISCONTINUOUS at cell boundaries. Any operation here that transforms the
// sample coordinate `p` by them tears the wave field at every boundary and the surface reads as a
// checkerboard of tiles. Two rules follow:
//   1. Only TRANSLATE p (a pure advection offset). WaterManager smooths the flow across N4
//      neighbours so the translation varies slowly and the residual step is imperceptible.
//   2. NO anisotropic warp (squash/rotate about the flow axis). That was tried and it tiled badly
//      even with smoothing, because the distortion grows with |p| — far from the origin, a tiny
//      direction difference becomes a large coordinate difference.
// Amplitude/choppiness scaling is safe: it does not move the sample point.
vec3 waterFlowNormal(vec2 p, float t, vec2 flowDir, float strength) {
    // ⚑GROUND: 2.5 world-units/sec at full strength. Fast enough to read as a current at a glance,
    // slow enough that the wave pattern doesn't alias into a strobe at 60 fps.
    const float ADVECT_SPEED = 2.5;
    // CYCLE PERIOD. This is the load-bearing part, not a tuning knob:
    //
    // A naive `p - flowDir * t * speed` offset GROWS WITH TIME, so any per-cell difference in flow
    // grows with it too. After a few minutes of uptime neighbouring cells are phase-shifted by tens
    // of world units and the surface shatters into a checkerboard of tiles — observed live, and it
    // is why smoothing the flow field alone did NOT fix it (a tiny difference still diverges).
    //
    // The fix is the standard flow-map cycle: advect over a bounded window and crossfade between
    // two half-period-offset samples. The offset never exceeds PERIOD * speed (here 7.5 units), so
    // the distortion — and any inter-cell discrepancy — stays bounded forever.
    const float PERIOD = 3.0;
    float ph0 = fract(t / PERIOD);
    float ph1 = fract(t / PERIOD + 0.5);
    float k = ADVECT_SPEED * strength * PERIOD;
    vec3 n0 = waterRippleNormal(p - flowDir * (ph0 * k), t);
    vec3 n1 = waterRippleNormal(p - flowDir * (ph1 * k), t);
    // Weight each sample by how far it is from its own wrap point, so the crossfade hides the reset.
    vec3 n = normalize(mix(n0, n1, abs(2.0 * ph0 - 1.0)));
    // Moving water is choppier: exaggerate the normal's tilt with speed (safe — no coord change).
    n.xz *= (1.0 + 1.2 * strength);
    return normalize(n);
}

// ---------------------------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------------------------

// Sky/sun reflection driven by the SCENE's sun, not a hardcoded one. `toSun` is the direction TO
// the sun (= -ubo.sunDirection, matching voxel.frag's `sunL`).
vec3 waterSkyReflection(vec3 R, vec3 toSun, vec3 sunColor, float ambient) {
    // Daylight factor: 1 with the sun overhead, 0 once it is at/below the horizon. This is what
    // makes water go dark at night — the pre-V3 shader kept a midday glint at midnight.
    float daylight = clamp(toSun.y * 1.5 + 0.15, 0.0, 1.0);

    vec3 dayHorizon = vec3(0.72, 0.82, 0.95);
    vec3 dayZenith  = vec3(0.24, 0.46, 0.80);
    vec3 nightHorizon = vec3(0.05, 0.07, 0.12);
    vec3 nightZenith  = vec3(0.01, 0.02, 0.05);

    float up = clamp(R.y, 0.0, 1.0);
    vec3 horizonCol = mix(nightHorizon, dayHorizon, daylight);
    vec3 zenithCol  = mix(nightZenith,  dayZenith,  daylight);
    vec3 sky = mix(horizonCol, zenithCol, pow(up, 0.6));

    // Sun disc + halo, only while the sun is actually up, tinted by its live colour.
    float sd = pow(max(dot(R, toSun), 0.0), 900.0);   // tight bright disc
    float sg = pow(max(dot(R, toSun), 0.0), 40.0);    // soft halo
    vec3  sunCol = sunColor * daylight;

    return sky * max(ambient, 0.35) + sunCol * (sd * 2.0 + sg * 0.12);
}

// ---------------------------------------------------------------------------------------------
// The surface
// ---------------------------------------------------------------------------------------------

// Beer-Lambert extinction per world unit (≈1 m per voxel). ⚑GROUND: the SHAPE is real — clear
// water absorbs red roughly an order of magnitude faster than blue (Pope & Fry 1997 measure
// ~0.42 /m at 650 nm, ~0.05 /m at 550 nm, ~0.014 /m at 450 nm). Blue is nudged UP from the
// physical value because a voxel world's water bodies are metres deep, not tens of metres, and
// the true blue coefficient would show no gradient at all over a 5-voxel pond.
const vec3 WATER_EXTINCTION = vec3(0.42, 0.09, 0.045);

// In-scattered colour — what the water body itself glows with as the transmitted scene fades out.
const vec3 WATER_SCATTER = vec3(0.04, 0.18, 0.24);

// Path length (world units) over which a shoreline fades from invisible to fully water.
// ⚑GROUND: 0.4 voxel ≈ 40 cm of water — about where a real shore stops reading as wet ground and
// starts reading as water.
const float SHORE_FADE = 0.4;

struct WaterSurfaceInput {
    vec3  worldPos;      // the water surface point being shaded
    vec3  camPos;        // camera world position
    float time;          // seconds
    vec2  screenSize;    // framebuffer size in pixels
    float fragDepthNdc;  // gl_FragCoord.z of this surface fragment
    vec2  fragCoord;     // gl_FragCoord.xy
    float sideFace;      // 0 = top surface, 1 = a vertical side/waterfall face
    float minThickness;  // floor on the water thickness (the sim's own column depth, 0 if unknown)
    vec2  flowDir;       // Phase 3: normalized horizontal flow (0,0 = still)
    float flowStrength;  // Phase 3: 0..1
    float foam;          // Phase 3: 0..1 whitewater amount
    // Phase 2: the surface's own macro normal (Gerstner swell for the sea; +Y for flat water).
    // Ripple detail is layered ON TOP of this rather than replacing it, so the big shape drives
    // reflection/Fresnel while the small waves supply glitter.
    vec3  baseNormal;
};

vec4 shadeWaterSurface(WaterSurfaceInput inp) {
    vec3 detail = waterFlowNormal(inp.worldPos.xz, inp.time, inp.flowDir, inp.flowStrength);
    // Perturb the macro normal by the ripple detail's tilt (detail is around +Y, so its xz IS the
    // tilt). Keeping the two separate is what lets a swell read as a swell at distance while still
    // sparkling up close.
    vec3 N = normalize(inp.baseNormal + vec3(detail.x, 0.0, detail.z));
    vec3 V = normalize(inp.camPos - inp.worldPos);
    vec3 toSun = normalize(-ubo.sunDirection);

    // --- How much water is the view ray looking through? -----------------------------------
    vec2 uv = inp.fragCoord / inp.screenSize;
    float sceneD  = texture(sceneDepthTex, uv).r;
    vec3  fwd     = waterCamForward(ubo.view);
    vec3  rayDir  = normalize(inp.worldPos - inp.camPos);
    float cosA    = max(dot(rayDir, fwd), 1e-4);
    // Convert both from "distance along the forward axis" to "distance along this pixel's ray".
    float sceneT  = waterLinearDepth(sceneD, ubo.proj) / cosA;
    float surfT   = length(inp.worldPos - inp.camPos);
    float thickness = max(sceneT - surfT, 0.0);
    // A vertical face (a waterfall curtain) has no meaningful "scene behind minus surface"
    // reading at grazing angles; the sim's own column depth is the better floor there.
    thickness = max(thickness, inp.minThickness);

    // --- Refraction: the scene behind the surface, displaced by the ripple normal ------------
    // The offset shrinks with distance so far water doesn't wobble absurdly.
    float distortion = 0.035 / (1.0 + surfT * 0.08);
    vec2 refractUV = clamp(uv + N.xz * distortion, vec2(0.001), vec2(0.999));
    // Reject samples that are IN FRONT of the water: without this, geometry standing out of the
    // water (a pier, a character's legs) smears across the surface — the classic refraction halo.
    float refrD = texture(sceneDepthTex, refractUV).r;
    if (refrD < inp.fragDepthNdc) refractUV = uv;   // nearer than us → fall back to the straight view
    vec3 behind = texture(refractionTex, refractUV).rgb;

    // --- Absorption: tint what we see through the water by how far the light travelled -------
    vec3 transmit = exp(-WATER_EXTINCTION * thickness);
    vec3 body = behind * transmit + WATER_SCATTER * (1.0 - transmit);

    // --- Reflection ------------------------------------------------------------------------
    float ndv  = clamp(dot(V, N), 0.0, 1.0);
    float fres = clamp(0.02 + 0.98 * pow(1.0 - ndv, 5.0), 0.0, 1.0);
    vec3  R    = reflect(-V, N);
    vec3  refl = waterSkyReflection(R, toSun, ubo.sunColor, ubo.ambientLight);

    vec3 color = mix(body, refl, fres);

    // Specular glint off the ripples, using the LIVE sun (gone at night, warm at sunset).
    vec3  H = normalize(toSun + V);
    float spec = pow(max(dot(N, H), 0.0), 220.0) * clamp(toSun.y * 2.0, 0.0, 1.0);
    color += ubo.sunColor * spec * 0.8;

    // Side faces are looking THROUGH the body edge-on: drop the sky mirror, keep the volume.
    color = mix(color, body * 0.85, inp.sideFace);

    // WHITEWATER (Phase 3): where the water is moving AND shallow it breaks white over its bed.
    // Streaked along the flow so it reads as motion rather than a static frosting, and lit by the
    // scene so it doesn't glow at night.
    if (inp.foam > 0.001) {
        // Same two rules as waterFlowNormal: translate only, and CYCLE the offset so it cannot grow
        // with time (an unbounded offset tiles the foam exactly the way it tiled the normals).
        float fph = fract(inp.time / 3.0);
        vec2 fp = inp.worldPos.xz - inp.flowDir * (fph * 3.0 * 3.0 * inp.flowStrength);
        float bands = sin(fp.x * 2.7 + fp.y * 1.9) * 0.5 + 0.5;
        float fine  = sin(fp.x * 6.1 - fp.y * 7.3) * 0.5 + 0.5;
        float mask = clamp(bands * 0.7 + fine * 0.5 - 0.35, 0.0, 1.0);
        mask *= 1.0 - abs(2.0 * fph - 1.0) * 0.5;   // fade across the wrap so the reset isn't a pop
        vec3 foamCol = mix(vec3(0.35), ubo.sunColor, clamp(toSun.y * 1.5 + 0.15, 0.0, 1.0))
                     * max(ubo.ambientLight, 0.3);
        color = mix(color, foamCol, clamp(inp.foam * mask, 0.0, 0.85));
    }

    // --- Soft shoreline --------------------------------------------------------------------
    // The hard waterline was the single most "blocky" tell. Fade out where the water thins to
    // nothing; stay fully opaque once there is any real depth (refraction already carries the
    // background through, so full alpha is correct — this is a compositing surface, not a tint).
    float alpha = smoothstep(0.0, SHORE_FADE, thickness);
    alpha = max(alpha, inp.sideFace * 0.85);   // curtains stay visible even where thin
    // Foam is opaque spray: it should stay visible even where the water itself is fading out at a
    // shoreline, which is exactly where a stream's whitewater lives.
    alpha = max(alpha, inp.foam * 0.6);
    return vec4(color, alpha);
}
