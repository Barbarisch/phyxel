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
// Noise
// ---------------------------------------------------------------------------------------------
//
// WHY THIS EXISTS. Foam used to be drawn by summing three sines. A sum of a few sines is not
// "random-looking" — it is a LATTICE, and the eye reads a lattice as fabric. Measured on the live
// frame (FFT of the water region, strongest non-DC peak over median power): the sine foam scored
// 4.9 MILLION, with peaks at exactly the sines' own wavelengths (2.19 / 1.43 / 0.62 world units
// against the shader's 1.90 / 1.29 / 0.66). That is the reported "patchwork quilt". Adding a
// fourth sine cannot fix it; the pattern has to stop being periodic at all.
//
// Noise, not a texture: no fetch, no atlas slot, and no tiling period to give the game away.
//
// ⚑PRECISION LIMIT: the hash is fed the INTEGER lattice cell, so it stays well-conditioned as long
// as |cell| stays far below fp32's 2^24 exact-integer range — i.e. world coordinates up to ~1e5.
// Beyond that adjacent cells collapse onto the same float and the noise degenerates into bands.
// Do NOT "fix" that with a mod() wrap: that was tried on the voxel orientation hash and produced a
// hard seam at the wrap boundary.
//
// AND SIMPLEX, not value noise. Value noise interpolates a SQUARE lattice, so its cells are aligned
// to world X/Z and a thresholded mask reads as a grid of rectangles — which is exactly what replaced
// the sine lattice on the first attempt at this fix. Measured axis-alignment of the thresholded mask
// (gradient orientation concentrated near an axis; 1.00 = isotropic, Gaussian reference = 1.02):
//
//     value noise, 2 octaves    1.13     8 sin
//     value noise, 4 octaves    1.06    16 sin
//     simplex,     2 octaves    0.98    12 sin   <- chosen: isotropic AND cheaper than value-4
//     simplex,     3 octaves    1.00    18 sin
//
// Simplex tiles the plane with triangles, so there is no axis-aligned cell to see in the first
// place. 12 transcendentals per sample is the cost; see the commit for the measured frame time.
vec2 waterGrad(vec2 cell) {
    vec2 h = fract(sin(vec2(dot(cell, vec2(127.1, 311.7)),
                            dot(cell, vec2(269.5, 183.3)))) * 43758.5453);
    vec2 g = h * 2.0 - 1.0;
    // Guard the degenerate near-zero draw: normalize(0) is NaN, and one NaN fragment propagates
    // through the foam blend into a black pixel.
    return g * inversesqrt(max(dot(g, g), 1e-6));
}

float waterSimplex(vec2 p) {
    const float F2 = 0.3660254;   // (sqrt(3) - 1) / 2
    const float G2 = 0.2113249;   // (3 - sqrt(3)) / 6
    vec2 i  = floor(p + (p.x + p.y) * F2);
    vec2 x0 = p - (i - (i.x + i.y) * G2);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 x1 = x0 - i1 + G2;
    vec2 x2 = x0 - 1.0 + 2.0 * G2;
    vec3 t  = max(0.5 - vec3(dot(x0, x0), dot(x1, x1), dot(x2, x2)), 0.0);
    t = t * t;
    t = t * t;                                     // t^4 falloff
    float n = t.x * dot(waterGrad(i),               x0)
            + t.y * dot(waterGrad(i + i1),          x1)
            + t.z * dot(waterGrad(i + vec2(1.0)),   x2);
    return n * 35.0 + 0.5;                         // ~[0,1], mean 0.5
}

// Two octaves. A deliberate budget, not a shrug: this runs on every water fragment and water can
// cover most of the screen. Two gives a patch size plus a broken edge, which is all foam needs.
// ⚑Measured std is ~0.142, NOT the ~0.29 a uniform [0,1] field would have — interpolated noise
// clusters hard around its mean. Anything using this as a DISPLACEMENT must scale accordingly;
// assuming uniformity is what made the first depth dither 3x too weak to cover a 1-unit step.
float waterFbm(vec2 p) {
    return waterSimplex(p) * 0.65
         + waterSimplex(p * 2.17 + vec2(19.3, 7.1)) * 0.35;
}
const float WATER_FBM_STD = 0.142;

// ---------------------------------------------------------------------------------------------
// Surface normal
// ---------------------------------------------------------------------------------------------

// Fine surface detail: crossing directional ripples layered on top of whatever macro shape the
// surface has (the Gerstner swell at sea, a flat quad on a pond).
//
// THESE MUST BE MUCH FINER THAN THE SWELL. The original values were 18.0 and 10.5 world units — the
// same scale as the swell's own components (14 / 8.5 / 4.6), so the water had exactly ONE band of
// detail and nothing else. That is why it looked smooth and featureless up close while reading as
// nothing but big rolling waves at a distance: there was no small-scale texture to see. Four
// octaves from ~4.5 down to ~0.8 units now cover the near field.
//
// `pixelWorld` is the world size of one screen pixel at this point. Each octave fades out on its
// OWN terms — when its wavelength approaches a few pixels it stops being texture and becomes a
// shimmering sparkle, so it is dropped. A single blanket distance fade was wrong twice over: it
// killed coarse and fine detail together (so distant water went dead flat instead of keeping its
// large-scale shape), and being a fixed radius around the camera it was a visible ring that
// travelled with the viewer.
vec3 waterRippleNormal(vec2 p, float t, float pixelWorld) {
    vec2 d1 = normalize(vec2( 1.0,  0.4));
    vec2 d2 = normalize(vec2(-0.6,  1.0));
    vec2 d3 = normalize(vec2( 0.8, -0.7));
    vec2 d4 = normalize(vec2(-0.3, -1.0));
    // wavelengths ~4.5, ~2.4, ~1.4, ~0.8 world units (f = 2*pi/lambda)
    float f1 = 1.40, f2 = 2.60, f3 = 4.50, f4 = 7.90;
    // AMPLITUDES ARE DELIBERATELY TINY. What matters visually is the SLOPE each octave contributes
    // (a*f), because that is what tilts the normal. The first version summed to 0.151 — over three
    // times the 0.045 of the detail it replaced, at far higher frequency — and the result was a
    // corduroy of micro-ridges over every wave face that looked worse than having no detail at all.
    // These sum to ~0.048: the same gentle sheen as before, but spread across four fine scales
    // instead of sitting at the swell's own scale.
    float a1 = 0.0100, a2 = 0.0055, a3 = 0.0028, a4 = 0.0014;
    float s1 = 1.10, s2 = 1.70, s3 = 2.40, s4 = 3.30;

    // Per-octave visibility: full while the wavelength covers >~7 px, gone under ~2.5 px. Coarse
    // octaves therefore persist far into the distance and only the finest drop out early, which is
    // what keeps far water looking like water rather than a flat sheet.
    float px = max(pixelWorld, 1e-4);
    float k1 = smoothstep(2.5, 7.0, (6.2831853 / f1) / px);
    float k2 = smoothstep(2.5, 7.0, (6.2831853 / f2) / px);
    float k3 = smoothstep(2.5, 7.0, (6.2831853 / f3) / px);
    float k4 = smoothstep(2.5, 7.0, (6.2831853 / f4) / px);

    float p1 = dot(p, d1) * f1 + t * s1;
    float p2 = dot(p, d2) * f2 + t * s2;
    float p3 = dot(p, d3) * f3 + t * s3;
    float p4 = dot(p, d4) * f4 + t * s4;

    float dx = a1 * f1 * d1.x * cos(p1) * k1
             + a2 * f2 * d2.x * cos(p2) * k2
             + a3 * f3 * d3.x * cos(p3) * k3
             + a4 * f4 * d4.x * cos(p4) * k4;
    float dz = a1 * f1 * d1.y * cos(p1) * k1
             + a2 * f2 * d2.y * cos(p2) * k2
             + a3 * f3 * d3.y * cos(p3) * k3
             + a4 * f4 * d4.y * cos(p4) * k4;
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
vec3 waterFlowNormal(vec2 p, float t, vec2 flowDir, float strength, float pixelWorld) {
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
    vec3 n0 = waterRippleNormal(p - flowDir * (ph0 * k), t, pixelWorld);
    vec3 n1 = waterRippleNormal(p - flowDir * (ph1 * k), t, pixelWorld);
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

// A/B probe. Screen-space metrics cannot separate world-aligned voxel terraces from the foam
// noise's own cells whenever the camera is yawed, so attribute artifacts by switching foam OFF
// and re-shooting the identical vantage instead of inferring. Ships at 1.0.
//
// It has already settled two questions that inference got wrong or could not reach:
//   * the shallow-water "patchwork quilt" is 100% this foam layer, NOT seabed voxel terracing;
//   * the pale open ocean from a low vantage is NOT foam — foam moves those pixels by at most
//     4 grey levels out of ~200, and the far band matches the sky reference to within 0.3. It is
//     grazing-angle Fresnel doing the physically correct thing.
const float WATER_FOAM_DEBUG_SCALE = 1.0;

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
    // ── SHORE SURF (Phase 2 leftover) ─────────────────────────────────────────────────────────
    float wavePhase;   // -1 trough .. +1 crest; 0 for water with no swell
    float breakDepth;  // water depth at which waves break (world units); 0 disables surf
    // The UNDISTURBED water level. Ground above it is dry land and must never be covered, however
    // high a wave crest happens to rise over it. Set to -1e9 to disable (per-cell water, whose
    // level is whatever the sim says it is).
    float restLevelY;
    // 1 = the hydrology bake explicitly marks this column DRY (the layer is drawing its
    // seaLevel FALLBACK here, not baked water). 0 for baked-wet columns and for per-cell water.
    float dryLand;
};

vec4 shadeWaterSurface(WaterSurfaceInput inp) {
    // DETAIL LOD. The fine octaves are sub-metre; past a few tens of metres they are smaller than a
    // pixel and stop being texture, becoming a shimmering sparkle that crawls as the camera moves.
    // ⚑GROUND: full detail to 45 units, gone by 220. 45 is roughly where the ~0.8-unit finest
    // octave drops under a couple of pixels at this FOV and resolution; 220 is a long enough ramp
    // that the transition is never a visible ring on the water.
    // World size of one screen pixel here: distance * (2*tan(fovY/2) / screenHeightPx), with the
    // engine's fixed 45-degree vertical FOV. Driving the LOD off this rather than off a fixed radius
    // is what lets each octave retire on its own terms AND removes the camera-centred ring.
    float viewDist = length(inp.worldPos - inp.camPos);
    float pixelWorld = viewDist * (0.828427 / max(inp.screenSize.y, 1.0));
    vec3 detail = waterFlowNormal(inp.worldPos.xz, inp.time, inp.flowDir, inp.flowStrength, pixelWorld);
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

    // VERTICAL water depth, which is a different quantity from `thickness` above and the right one
    // for anything shore-shaped. thickness is the path length ALONG THE VIEW RAY, so at a grazing
    // angle a few centimetres of water reads as metres — a surf band driven by it would balloon
    // across the whole bay as the camera lowered. Reconstructing where the ray actually hits the
    // seabed gives the true depth under this point, which is what decides where a wave breaks.
    float seabedY = inp.camPos.y + rayDir.y * sceneT;
    float verticalDepth = max(inp.worldPos.y - seabedY, 0.0);
    // Is there actually a seabed behind this pixel? At the horizon the ray hits nothing, the depth
    // buffer reads its cleared far value, and the reconstruction above collapses to ~0 depth — which
    // would paint the entire horizon as shoreline foam. Anything at the far plane has no bottom, so
    // it is open water by definition.
    // REVERSE-Z: the cleared "nothing here" depth is 0.0, not 1.0 (DepthConvention.h).
    bool hasSeabed = sceneD > 0.0001;

    // WORLD-LOOK B1 — THE ENDLESS OCEAN UNDER THE WORLD. A bake-DRY column drawing the layer's
    // seaLevel fallback is only ever legitimate when real submerged terrain sits behind it (the
    // near-field shoreline snap band: bake-dry columns whose ground lies below sea level — the
    // depth gate at the bottom of this function then arbitrates per pixel). With NO seabed at
    // all — nothing was drawn behind the sheet: under the world, unloaded regions, past the far
    // tiers — every gate in this function silently skips, and the fallback rendered as an
    // infinite phantom sea beneath everything. That combination has no honest interpretation:
    // kill the fragment. (Baked-WET columns keep drawing against empty depth — the open ocean
    // past the far tiers is exactly that case.)
    if (inp.dryLand > 0.5 && !hasSeabed) discard;

    // --- Refraction: the scene behind the surface, displaced by the ripple normal ------------
    // The offset shrinks with distance so far water doesn't wobble absurdly.
    float distortion = 0.035 / (1.0 + surfT * 0.08);
    vec2 refractUV = clamp(uv + N.xz * distortion, vec2(0.001), vec2(0.999));
    // Reject samples that are IN FRONT of the water: without this, geometry standing out of the
    // water (a pier, a character's legs) smears across the surface — the classic refraction halo.
    float refrD = texture(sceneDepthTex, refractUV).r;
    // REVERSE-Z: nearer means a LARGER depth value, so the "sample is in front of the water" test
    // flips. Left as `<` this would reject everything BEHIND the water instead of everything in
    // front of it — i.e. exactly inverted refraction masking.
    if (refrD > inp.fragDepthNdc) refractUV = uv;   // nearer than us → fall back to the straight view
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

    // ── SHORE SURF ────────────────────────────────────────────────────────────────────────────
    // Before this, a shoreline was ONLY an alpha fade: water silently dissolved into the beach with
    // no waterline and no surf, which is the most obvious "this is not a real coast" tell.
    //
    // ⚑GROUND: waves break when the wave height reaches ~0.78 of the water depth (McCowan's
    // solitary-wave breaking criterion), so with a trough-to-crest height H = 2*amplitude they
    // break once the depth falls below H/0.78 = 2.56*amplitude. `breakDepth` carries that, so the
    // surf zone's width follows the sea state instead of being dialled in by eye.
    // DE-QUANTISE THE DEPTH BEFORE KEYING FOAM OFF IT.
    //
    // The seabed is voxels, so `verticalDepth` is piecewise CONSTANT over each voxel top and jumps a
    // whole unit at every terrace edge. Anything keyed directly to it therefore paints the terraces:
    // one flat foam value per plateau with a hard sawtooth step between them, which reads as a
    // patchwork quilt laid over the shallows (reported 2026-07-29 — see the screenshot in the plan).
    //
    // Perturbing the depth breaks the foam boundary across the steps instead of letting it snap to
    // them. ⚑GROUND: peak-to-peak 1.0 = exactly the quantisation step, so the dither is the same
    // size as the artifact it hides — smaller leaves stairs visible, larger detaches the foam from
    // the real shoreline.
    //
    // ⚑THE DITHER MUST BE FINER THAN THE STEP IT HIDES. The first attempt used two sines of
    // wavelength ~15 and ~20 world units — an order of magnitude COARSER than the 1-unit terrace it
    // was meant to disguise. That does not dither anything; it just repaints the shoreline as broad
    // diagonal bands, and the reported quilt got a second layer. Noise at ~1.2 units is comparable
    // to the step, so the boundary dissolves into a stipple instead of acquiring new structure.
    //
    // The REAL fix is a smooth shore field (docs/WaterPhysicalFeelPlan.md §2) whose distance
    // transform is continuous by construction; this is the cheap stand-in until that exists.
    // Scaled to std 0.40 — a bit under half the 1.0 step, so the boundary dissolves without the
    // foam wandering far enough to leave the real shoreline. Divide by the measured std rather
    // than trusting the raw range: this field's raw spread is a quarter of what "0..1" suggests.
    float wob = (waterFbm(inp.worldPos.xz * 0.83) - 0.5) * (0.40 / WATER_FBM_STD);
    float foamDepth = max(verticalDepth + wob, 0.0);

    float surfFoam = 0.0;
    if (inp.breakDepth > 0.0001 && hasSeabed) {
        // Shoaling: 0 offshore, 1 at the waterline. Squared — cubed collapsed the band to nothing
        // once the swell was scaled down to the voxel world (breakDepth 2.56*0.30 = 0.77 voxels,
        // which is sub-voxel and simply invisible). The band has to be a WIDTH you can see.
        float shoal = 1.0 - smoothstep(0.0, inp.breakDepth, foamDepth);
        shoal *= shoal;
        // A wave breaks on its CREST — the phase gate is what makes this a band running shoreward
        // with each wave rather than a static ring.
        // ⚑The between-crest floor was 0.25 and that was the single biggest reason the shallows
        // read as one white sheet: this seabed is a very gently sloping shelf, so "depth < 2.5"
        // is a huge horizontal area, and a 0.25 floor painted ALL of it permanently. Surf is a
        // travelling line, not a filled zone. 0.06 leaves a faint standing trace of the breaker
        // line — enough to see from a distance — without frosting the bay between waves.
        float crest = smoothstep(-0.35, 0.55, inp.wavePhase);
        surfFoam = shoal * mix(0.06, 1.0, crest);
    }
    // The waterline itself always carries foam, wave or not — the line that makes a coast read as a
    // coast even on flat calm water (and what lakes and rivers get).
    float rim = hasSeabed ? (1.0 - smoothstep(0.0, 0.55, foamDepth)) : 0.0;
    inp.foam = max(inp.foam, max(surfFoam, rim * 0.7));
    inp.foam *= WATER_FOAM_DEBUG_SCALE;   // A/B probe: 0 disables all foam

    // WHITEWATER (Phase 3): where the water is moving AND shallow it breaks white over its bed.
    // Streaked along the flow so it reads as motion rather than a static frosting, and lit by the
    // scene so it doesn't glow at night.
    if (inp.foam > 0.001) {
        // Same two rules as waterFlowNormal: translate only, and CYCLE the offset so it cannot grow
        // with time (an unbounded offset tiles the foam exactly the way it tiled the normals).
        float fph = fract(inp.time / 3.0);
        vec2 fp = inp.worldPos.xz - inp.flowDir * (fph * 3.0 * 3.0 * inp.flowStrength);
        // Simplex, NOT summed sines and NOT value noise. Both were tried and both are lattices:
        // three sines gave a dotted plaid (FFT peaks at exactly the sines' wavelengths), value
        // noise gave axis-aligned rectangles. See the note above waterGrad.
        //
        // TWO SCALES, because one scale is what makes foam read as tiles whatever noise feeds it:
        // the coarse field decides WHERE a patch sits, the fine field tears its edge so the patch
        // has no characteristic size.
        float coarse = waterFbm(fp * 0.32);          // ~3-unit patches
        // The fine octave is sub-metre and turns into crawling sparkle once it drops under a couple
        // of pixels, exactly like the ripple octaves — so retire it on the same terms.
        float fineK = smoothstep(2.5, 7.0, (1.0 / 1.55) / max(pixelWorld, 1e-4));
        float fine  = waterSimplex(fp * 1.55) * fineK;
        float n = coarse * (1.0 - 0.28 * fineK) + fine * 0.28;
        float mask = smoothstep(0.44, 0.74, n);
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

    // ── NO WATER ON DRY LAND ──────────────────────────────────────────────────────────────────
    // The swell displaces the whole sheet uniformly, because the vertex shader has no idea how deep
    // the water beneath each vertex is. Near a shore that means a crest physically rises OVER the
    // beach, and since the ray then hits ground well below the crest, the depth test reads plenty
    // of "water" and happily draws it — a wave climbing a hillside with dry land visible behind it.
    //
    // Ground above the undisturbed water level is land, full stop. Gating on the REST level rather
    // than on the displaced surface is what pins the waterline to the true sea-level contour
    // instead of letting it breathe up and down the slope with every wave.
    // ⚑GROUND: a 0.25-voxel run-up band, so a wave can still visibly wash a little way up a flat
    // beach (swash) without ever climbing a slope.
    if (inp.restLevelY > -1e8 && hasSeabed) {
        alpha *= 1.0 - smoothstep(0.0, 0.25, seabedY - inp.restLevelY);
    }
    return vec4(color, alpha);
}
