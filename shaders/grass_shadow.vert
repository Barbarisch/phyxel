#version 450
// ===========================================================================================
// GENERATED SIBLING of grass.vert - GRASS SHADOW CASTER.
// DO NOT EDIT BY HAND. Regenerate with tools/regen_grass_shadow.py whenever grass.vert changes.
// Differs from grass.vert in exactly TWO ways:
//   1. kShadowWidthGate 1.0 instead of 0.0 -> the blade turns broadside to the sun and its width
//      is multiplied by pc.shadowWidthScale (a blade edge-on to the light would cast nothing).
//   2. projects with ubo.lightSpaceMatrix instead of ubo.viewProj.
// Blade placement, wind and density-LOD math MUST stay identical or a blade's shadow detaches
// from the blade casting it (same contract as foliage.vert <-> foliage_shadow.vert).
// ===========================================================================================
#extension GL_GOOGLE_include_directive : require
#include "wind.glsl"

// Lightweight grass blades. ONE instance per grass-topped voxel (GrassInstanceData); this shader
// procedurally fans it into `bladesPerVoxel` blades using gl_VertexIndex (6 verts/blade, no
// vertex buffer). Two silhouettes (pc.bladeStyle): BOXY voxel-aesthetic (default) = thin
// elongated crisp RECTANGLE, rest height quantized to the 1/9-voxel microcube grid; SMOOTH
// legacy = ribbon tapering to a point. Both share the SAME smooth wind motion — the shared
// procedural gust field (wind.glsl, fed by the CPU WindSystem via push constants); the boxy
// look is silhouette-only, never quantized motion (quantized offsets read as janky popping).
// Sprout-in growth uses ubo.elapsedTime; distance fade uses ubo.cameraPosition.
// See GrassRenderPipeline / docs/VegetationWindPlan.md.

layout(location = 0) in uint inPacked;   // bits: 0-4 x |5-9 y |10-14 z |15-18 sky |19-22 R |23-26 G |27-30 B
layout(location = 1) in uint inTex;      // low16 = grass top-face texture index (colour source)
                                         // bits 16-19 = edge-ness: grassy-neighbour count 0-8 (C4 taper)

// Full set-0 UBO (must match UniformBufferObject in vulkan/VulkanDevice.h). We only read view/proj,
// cameraPosition and the trailing elapsedTime, but the layout up to them must be declared.
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
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;          // proj*view, precombined once per frame on CPU
    mat4 biasedLightSpace;  // shadow bias * lightSpaceMatrix, precombined on CPU
    vec3 cameraWorld;       // true camera world position (camera-relative rendering)
    int  debugShadowMode;   // shadow-only debug view
    float shadowDepthRange; // world-unit light-volume depth span
    // Grass interaction displacers (VegetationWindPlan Phase 4 v1): characters within the
    // grass radius, uploaded per frame by RenderCoordinator via VulkanDevice::setGrassDisplacers.
    // xyz = CAMERA-RELATIVE feet position (same space as rootWorld below), w = push radius.
    vec4  grassDisplacers[16];
    vec4  grassDisplacersAux[16];   // x = strength envelope 0..1 (eased attack/release on CPU)
    ivec4 grassDisplacerMeta;   // x = active displacer count (0 = feature entirely inert)
} ubo;

layout(push_constant) uniform PushConstants {
    vec3  chunkBaseOffset;   // chunk world origin
    float bladeHeight;
    float windStrength;      // master wind amplitude (multiplies the shared field)
    float radius;            // grass drawn only within this distance of the camera
    float fadeRange;         // world units of height fade-out before the radius edge
    float growDuration;      // seconds for the sprout-in ramp
    uint  bladesPerVoxel;
    // Shared wind field (WindSystem writes grass + foliage identically each frame).
    float windDirX;
    float windDirZ;
    float windBase;          // steady bend strength
    float gustAmp;           // gust amplitude on top of base
    float gustScale;         // gust spatial frequency (1/world units)
    float gustSpeed;         // gust front travel speed (world units/s)
    uint  bladeStyle;        // 0 = smooth tapered ribbon, 1 = boxy rectangle (default)
    // Camera-relative rendering: chunkBaseOffset above is (world - camera); these carry the
    // exact ABSOLUTE chunk origin for the hash/clump/wind-phase seeds (must never be relative
    // or blades re-roll as the camera moves).
    float absBaseX;
    float absBaseY;
    float absBaseZ;
    float pushStrength;      // displacer bend amplitude (0 disables interaction response)
    // Density LOD (GrassRenderPipeline::widthCompensation): distant chunks draw fewer blades by
    // SHORTENING the draw, so the survivors widen to hold ground coverage. 1.0 in the near field.
    float widthScale;
    // How much wider than the real blade the SHADOW proxy is drawn (1.0 = same width).
    // Only the SHADOW variant reads it; the visible pass gates it to 1.0.
    float shadowWidthScale;
} pc;

layout(location = 0) out flat uint vTex;   // grass texture index
layout(location = 1) out vec2  vUV;        // colour-sample UV into the grass tile
layout(location = 2) out float vGrad;      // 0 at blade base .. 1 at tip (silhouette + AO)
layout(location = 3) out float vSide;      // -1..1 across blade width (silhouette taper)
layout(location = 4) out float vSky;       // baked skylight 0..1
layout(location = 5) out vec3  vBlock;     // baked block light 0..1/channel
layout(location = 6) out vec4  vShadowCoord; // biased light-space coord (shadow RECEIVING)

// Cheap hash -> [0,1)
float hash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

// Smooth bilinear value noise in [0,1] — the MEADOW field. Sampled at a large spatial
// period so grass height/density drift over tens of voxels while staying locally uniform
// (user-set look: "height varies over large distance, short distance is uniform & dense").
float vnoise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    // Decode voxel local position + baked light.
    uint packed = inPacked;
    float lx = float(packed & 0x1Fu);
    float ly = float((packed >> 5) & 0x1Fu);
    float lz = float((packed >> 10) & 0x1Fu);
    vSky      = float((packed >> 15) & 0xFu) / 15.0;
    vBlock    = vec3(float((packed >> 19) & 0xFu),
                     float((packed >> 23) & 0xFu),
                     float((packed >> 27) & 0xFu)) / 15.0;
    vTex = inTex & 0xFFFFu;

    // Min corner of the voxel's top face, CAMERA-RELATIVE (all position math below).
    vec3 cellBase = pc.chunkBaseOffset + vec3(lx, ly + 1.0, lz);
    // Hash-domain coordinates: ABSOLUTE world cell (exact integers via pc.absBase*), WRAPPED
    // to a 2048-unit period before any hashing/phase math. Raw far coords break float
    // precision inside hash21 (fract(400000 * 127.1) has no fractional bits), and RELATIVE
    // coords would re-roll every blade as the camera moves. absBase + local is integer-exact,
    // so mod() is exact; the 2km repeat is imperceptible.
    vec3 cellHash = mod(vec3(pc.absBaseX, pc.absBaseY, pc.absBaseZ) + vec3(lx, ly + 1.0, lz), 2048.0);

    // Segmented blades (Phase 3-lite): each blade is SEGMENTS stacked quads, so the v^bendExp
    // displacement profile below renders as an actual CURVE — blades bow under wind and around
    // characters instead of shearing as one rigid rectangle (single-quad blades read stiff).
    const int SEGMENTS = 3;
    const int VERTS_PER_BLADE = SEGMENTS * 6;   // must match vertsPerBlade in GrassRenderPipeline.cpp
    int blade  = gl_VertexIndex / VERTS_PER_BLADE;
    int rem    = gl_VertexIndex - blade * VERTS_PER_BLADE;
    int segIdx = rem / 6;
    int corner = rem - segIdx * 6;

    // Group blades into a few tight TUFTS per voxel (clumps) rather than scattering them evenly —
    // even spacing reads as isolated spikes; clustered blades read as grass. Each clump has a hashed
    // center in the cell; blades jitter within a small radius of it.
    const int BLADES_PER_CLUMP = 7;
    int clumpId      = blade / BLADES_PER_CLUMP;
    int bladeInClump = blade - clumpId * BLADES_PER_CLUMP;

    // MEADOW field: one smooth low-frequency noise drives BOTH blade height and coverage, so tall
    // lush zones and shorter zones drift across the field while any few-meter neighborhood stays
    // uniform and dense. (The old per-5-voxel/per-2-voxel hash patches made short-scale holes +
    // per-blade height chaos — the exact opposite of the wanted look.)
    // The DOMINANT octave is deliberately long (~72 voxels): the point is height varying across
    // a FIELD, read while walking over it, not per-voxel roughness. The 26-voxel octave only
    // keeps the gradient from looking like a rendered gradient; anything shorter than that reads
    // as noise and undoes the smoothness.
    float meadow = vnoise2(cellHash.xz * (1.0 / 72.0)) * 0.70
                 + vnoise2(cellHash.xz * (1.0 / 26.0) + 41.7) * 0.30;

    // Coverage: dense EVERYWHERE. The meadow field must not punch holes — the look being chased is
    // a continuous field, so coverage stays above the highest clump threshold across the whole
    // meadow range and only the very shortest zones thin at all.
    float coverage  = 0.88 + 0.22 * meadow;
    int   numClumps = (int(pc.bladesPerVoxel) + BLADES_PER_CLUMP - 1) / BLADES_PER_CLUMP;
    float clumpFrac = (float(clumpId) + 0.5) / float(max(numClumps, 1));
    float keep      = step(0.18 + 0.42 * clumpFrac, coverage);

    // Clump center spans the FULL [0,1]^2 top face. A center margin here (early versions used
    // 0.18..0.82) starves every voxel edge of roots — each cube grows an isolated middle island
    // and the voxel grid shows through as bare seam lines. Edge-to-edge centers let adjacent
    // cells' tufts meet, so coverage reads as one continuous meadow.
    vec2 cseed = vec2(cellHash.x * 3.17 + cellHash.z * 7.71 + float(clumpId) * 13.1,
                      cellHash.z * 2.39 - cellHash.x * 5.11 + float(clumpId) * 7.31);
    vec2 clumpCenter = vec2(0.02 + 0.96 * hash21(cseed),
                            0.02 + 0.96 * hash21(cseed + 5.27));

    // Per-blade hash (seeded on clump + blade-in-clump), used for jitter/height/yaw/stagger.
    vec2 seed = cseed + float(bladeInClump) * 2.73;
    float h0 = hash21(seed);
    float h1 = hash21(seed + 11.7);
    float h2 = hash21(seed + 23.3);
    float h3 = hash21(seed + 41.9);

    // Blade root = clump center + small jitter (tight tuft radius). Clamp keeps roots ON this
    // voxel's top face (an overhanging root floats in mid-air at a terrain step-down edge).
    vec2 jitter = (vec2(h0, h1) - 0.5) * 0.16;
    vec2 root2  = clamp(clumpCenter + jitter, vec2(0.005), vec2(0.995));
    vec3 rootWorld = cellBase + vec3(root2.x, 0.0, root2.y);

    // Quad corners (2 tris): (u in {0,1}, v in {0,1} within THIS segment); v then maps to the
    // blade-length fraction so consecutive segments share their boundary rows seamlessly.
    vec2 quad[6] = vec2[6](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));
    vec2 q = quad[corner];
    float uCentered = q.x - 0.5;   // -0.5..0.5 across width
    float v = (float(segIdx) + q.y) / float(SEGMENTS);   // 0 base .. 1 tip along the whole blade
    bool boxy = (pc.bladeStyle == 1u);

    vGrad = v;
    // Silhouette is the ONLY style difference: boxy = crisp full rectangle (vSide 0 defeats the
    // frag taper discard); smooth = ribbon tapering to a point.
    vSide = boxy ? 0.0 : uCentered * 2.0;

    // Blade height: the smooth meadow field sets the LOCAL stand height (0.45x in cropped zones
    // up to 1.85x in lush zones, drifting over ~72 voxels); per-blade jitter is deliberately
    // TINY (±6%) so neighboring blades read as one even stand whose height changes with the
    // field, not as random spikes. Widening the field range while keeping per-blade jitter small
    // is what makes the variation read as terrain-scale rather than as noise. Boxy blades still
    // quantize the REST height to whole 1/9-voxel microcube steps — a STATIC voxel-grid trait;
    // motion below stays smooth.
    float heightMul = mix(0.45, 1.85, smoothstep(0.06, 0.94, meadow));

    // EDGE TAPER (world-look C4): the mesher bakes each voxel's count of grass-topped
    // horizontal neighbours (0-8) into inTex bits 16-19; interior voxels (8/8) keep full
    // height and boundary voxels shorten, so a lawn ends in a low fringe against dirt, sand,
    // stone or a drop instead of a hard green cliff. Cross-chunk neighbours are baked as
    // grassy, so this can NEVER differ across a chunk boundary in open meadow (no seams —
    // the per-chunk-artifact lesson of the density LOD). The floor is deliberately well
    // above 0: bare edge voxels should read as short grass, not as missing grass.
    float edgeFrac  = float((inTex >> 16) & 0xFu) / 8.0;
    float edgeTaper = mix(0.40, 1.0, smoothstep(0.10, 0.90, edgeFrac));

    float H = pc.bladeHeight * heightMul * edgeTaper * (0.94 + 0.12 * h2);
    if (boxy) H = max(round(H * 9.0), 2.0) / 9.0;

    // Sprout-in growth: staggered start per blade, then held at full height.
    float plant = h0 * pc.growDuration * 0.6;
    float grow  = clamp((ubo.elapsedTime - plant) / max(pc.growDuration, 0.001), 0.0, 1.0);
    H *= grow;

    float dist = length(ubo.cameraPosition - rootWorld);

    // ── DENSITY LOD, PER BLADE AND CONTINUOUS ──────────────────────────────────────────────────
    // This USED to be decided per-chunk on the CPU (bladesForDistance picked a band from the chunk
    // CENTRE). That is what made the meadow look disjointed: two adjacent chunks whose centres fell
    // in different bands drew different densities, so the chunk boundary showed as a hard seam
    // running through open field. Density now depends ONLY on this blade's own world distance, so
    // it is identical either side of any voxel or chunk boundary by construction — there is no
    // per-chunk quantity left to disagree about.
    //
    // The CPU still shortens the draw, but only as a CONSERVATIVE upper bound computed from the
    // chunk's NEAREST corner, so it can never drop a clump this test would have kept.
    // THE CURVE IS 1/(1 + k*t^2), NOT a smoothstep, and the shape is a cost constraint rather than
    // an aesthetic one. Grass instances grow with the AREA of the disc, so blades-per-band grows
    // linearly with r; holding total vertex cost bounded needs density to fall about as 1/r^2.
    // A smoothstep ramp looks gentler but is far too flat in the mid-field: measured at 126 blades
    // / 224u it came to 117M verts/frame against a 22.8M predecessor. k=60 lands at ~29.9M while
    // keeping ~54 blades at 34u, which is what "much denser" has to mean up close.
    // Recompute the budget before touching k, radius or bladesPerVoxel — see bladesForDistance.
    // Shape: FULL density out to t0, then 1/(1 + k*u^2) where u is the remapped remainder.
    // The flat near band exists because a bare 1/(1+k t^2) was already 32% down by 20 units, which
    // reads as "dense at my feet, thinner just ahead" — the opposite of a field. Past t0 the
    // 1/r^2-ish falloff is a COST constraint: blades-per-band grows linearly with r, so anything
    // gentler makes the total unbounded (a smoothstep ramp measured 117M verts/frame here).
    const float kNearBand = 0.15;    // full density inside this fraction of the radius
    const float kFalloff  = 140.0;
    float tNorm       = clamp(dist / max(pc.radius, 0.001), 0.0, 1.0);
    float u           = max(0.0, tNorm - kNearBand) / (1.0 - kNearBand);
    float densityFrac = max(1.0 / (1.0 + kFalloff * u * u), 1.0 / 18.0);
    // Soft edge: clumps near the threshold fade out over a band instead of popping. Without this
    // the seam is gone but a moving camera still sees clumps blink in and out.
    float lodKeep     = 1.0 - smoothstep(densityFrac - 0.14, densityFrac, clumpFrac);

    // Distance fade at the radius edge. smoothstep, not linear: a linear collapse reads as a
    // visible circular "mowing line" tracking the camera. fadeRange is deliberately large so the
    // meadow thins out gradually rather than ending.
    float fade = 1.0 - smoothstep(pc.radius - pc.fadeRange, pc.radius, dist);

    H *= fade * keep * lodKeep;

    // Interaction (VegetationWindPlan Phase 4 v1): characters push nearby blades radially
    // outward with a squared falloff, and trodden blades flatten (height squash) instead of
    // just leaning. STATELESS — a blade springs back the frame its displacer moves away.
    // Zero displacers leaves every value below bit-identical to the non-interactive path
    // (pushXZ = 0, tread = 0), preserving the wind-0 stillness invariant.
    vec2  pushXZ = vec2(0.0);
    float tread  = 0.0;
    if (ubo.grassDisplacerMeta.x > 0 && H > 0.0) {
        for (int i = 0; i < ubo.grassDisplacerMeta.x; ++i) {
            vec4  d      = ubo.grassDisplacers[i];   // xyz camera-relative feet pos, w radius
            vec2  delta  = rootWorld.xz - d.xz;
            float distXZ = length(delta);
            // Vertical gate: only blades near the displacer's feet level respond (grass on a
            // ledge 2u above/below a walking character must not move).
            float vGate = 1.0 - clamp(abs(rootWorld.y - d.y) * 0.5, 0.0, 1.0);
            float t     = clamp(1.0 - distXZ / max(d.w, 0.001), 0.0, 1.0) * vGate;
            if (t <= 0.0) continue;
            // Dead-center roots get a stable hashed direction instead of a zero-length one.
            vec2 outDir = (distXZ > 0.02) ? delta / distXZ
                                          : normalize(vec2(h0, h1) - 0.5 + vec2(0.001, 0.0));
            // Strength envelope (CPU-eased attack/release) — grass eases back up behind a
            // character instead of popping upright the frame the displacer leaves.
            float env = ubo.grassDisplacersAux[i].x;
            pushXZ += outDir * (t * t * env);
            tread   = max(tread, t * t * env);
        }
        pushXZ *= pc.pushStrength;
        tread  *= clamp(pc.pushStrength, 0.0, 1.0);
        H      *= 1.0 - 0.30 * tread;   // trodden grass flattens toward the ground
    }

    // Horizontal blade orientation (yaw), width offset across the blade. Blades are SKINNY so a
    // dense tuft reads as many separate strands rather than one solid blob — at this density a
    // wide blade just occludes its neighbours and the whole stand flattens into a mat.
    // SHADOW-PASS GATE. The generated grass_shadow.vert rewrites this to 1.0; the visible pass
    // keeps 0.0, so nothing below can alter the blade you actually see.
    const float kShadowWidthGate = 1.0;   // SHADOW PASS: face the sun + widen the proxy

    float yaw = h3 * 6.2831853;
    vec2 dir = vec2(cos(yaw), sin(yaw));
    // SHADOW PASS: turn the blade BROADSIDE to the sun. Blade yaw is a per-blade hash, so a blade
    // that happens to point along the light projects ZERO width and casts nothing at all — how
    // much shadow a blade threw depended on its hash rather than its size, which also made the
    // single-blade rig measure a different thing every time the blade moved. Facing the light
    // gives every blade its full projected width: consistent, and the largest honest footprint.
    // Degenerate when the sun is straight overhead (xz vanishes) — keep the hashed yaw there.
    vec2 sunH = ubo.sunDirection.xz;
    if (kShadowWidthGate > 0.5 && dot(sunH, sunH) > 1e-6) {
        vec2 s = normalize(sunH);
        dir = vec2(-s.y, s.x);   // width axis perpendicular to the light => flat face to the sun
    }
    // Width compensation is now driven by the PER-BLADE densityFrac, not the per-chunk push
    // constant — same reason as the density itself: anything per-chunk reintroduces the seam.
    // (pc.widthScale is left in the block for the CPU-side conservative path and A/B work.)
    // Blades widened 2.5x (was 0.016/0.014): at the old width a blade was ~0.6 screen px and
    // leaned entirely on the sub-pixel floor below to stay visible at all.
    // pc.widthScale is the RUNTIME width knob (Params::bladeWidthScale, default 1.0 = as
    // authored). Width is the variable that decides whether a blade exceeds a shadow-map
    // texel, so it must be sweepable — POST /api/debug/grass {"bladeWidth": N}.
    float bladeWidth = (boxy ? 0.040 : 0.036) * min(inversesqrt(max(densityFrac, 0.02)), 2.6)
                     * max(pc.widthScale, 0.001);
    // SHADOW PASS: cast a slightly WIDER shadow than the blade — a multiple of the blade's real
    // width, NOT a shadow-texel clamp. The clamp it replaces stamped a blade 3.5 texels wide,
    // which at a 420u shadow distance is 0.50u against a ~0.10u blade: a shadow ~5x wider than
    // its caster, which read as a fat mushy smudge rather than as a blade.
    // Consequence: shadow width now tracks the blade, so once a blade falls under one shadow-map
    // texel it stops casting rather than smearing. Fixing THAT needs a near cascade (finer texels
    // close to the camera), not a wider blade.
    bladeWidth *= mix(1.0, pc.shadowWidthScale, kShadowWidthGate);
    // SUB-PIXEL FLOOR. A 0.016-unit blade is far under a pixel by ~100 units out, and a sub-pixel
    // quad doesn't get quieter with distance — it flickers as it drifts on and off sample points
    // (the known grass speckle, RenderOptimization.md:513). Holding blades at roughly a pixel
    // trades a hair of far-field accuracy for a stable horizon. The constant is ~1 px at the
    // reference 45-degree fovY / 900 px config; it is well below bladeWidth in the near field, so
    // max() leaves close-up grass exactly as authored.
    bladeWidth = max(bladeWidth, dist * 0.0011);
    vec3 widthOffset = vec3(dir.x, 0.0, dir.y) * (uCentered * bladeWidth);

    // Shared procedural wind (wind.glsl): the blade bends downwind by the local gust-field
    // strength — gust fronts travel across the whole field coherently instead of each blade
    // running its own sine. Hash-domain coords (precision footgun, see cellHash above).
    // TUNING (anti-jitter): the stiffness lag and flutter are deliberately SMALL — a wide lag
    // spread or fast random-phase flutter desynchronizes neighboring blades back into the
    // "randomish" shimmer this system exists to kill.
    vec2 wd = vec2(pc.windDirX, pc.windDirZ);
    float stiffness = h2;
    // ⚑ ANTI-JITTER (user: "wind movement is far far too jittery", 2026-08-01). Everything that
    // varies PER BLADE desynchronises neighbours, and desynchronised neighbours read as shimmer
    // rather than as wind. A field moves as a FIELD: the coherent travelling gust supplies the
    // motion, and per-blade variation exists only to stop it looking like a rigid sheet.
    // response spread 1.2-0.8 -> 1.08-0.94, lag 0.06 -> 0.02.
    float response  = mix(1.08, 0.94, stiffness);
    float lag       = stiffness * 0.02;
    // Trodden blades are pinned underfoot — they stop waving instead of thrashing while flat.
    float windDamp = 1.0 - 0.6 * tread;
    // INERTIA: grass doesn't snap to the instantaneous gust — low-pass the field with three
    // time-lagged taps (~0.5 s box filter) so fronts arrive as smooth swells, not jitter.
    // Travelling-front realism survives (the taps scroll with the same field); high-frequency
    // content is what gets removed.
    // Widened from a ~0.5 s to a ~1.0 s box filter (4 taps): the longer the low-pass, the more of
    // the gust field's high-frequency content is removed, and high-frequency content IS the jitter.
    vec2  gp = cellHash.xz + root2;
    float tg = ubo.elapsedTime - lag;
    float gust = (windGustAt(gp, tg,        wd, pc.gustScale, pc.gustSpeed)
                + windGustAt(gp, tg - 0.33, wd, pc.gustScale, pc.gustSpeed)
                + windGustAt(gp, tg - 0.66, wd, pc.gustScale, pc.gustSpeed)
                + windGustAt(gp, tg - 1.00, wd, pc.gustScale, pc.gustSpeed)) * 0.25;
    float bend = (pc.windBase + pc.gustAmp * gust) * response * pc.windStrength * windDamp;

    // Gentle slow flutter perpendicular to the wind, amplitude ∝ local gust strength — calm air
    // means calm grass (windBase+gustAmp are both 0 at speed 0, so everything below is exactly 0).
    // Flutter is the single worst jitter source and is now much gentler. The per-blade random
    // phase (h3 * 2pi) is what made adjacent blades flutter in opposite directions — the classic
    // "boiling grass" look. The phase is now DOMINANTLY SPATIAL and low-frequency, so neighbours
    // agree and the flutter reads as a slow ripple crossing the field. Frequency 1.9 -> 0.6 Hz,
    // amplitude 0.055 -> 0.018, per-blade phase contribution cut to a fifth.
    float phase   = (cellHash.x + root2.x) * 0.55 + (cellHash.z + root2.y) * 0.43
                  + h3 * 1.2566371;   // 0.2 * 2pi
    float flutter = sin(ubo.elapsedTime * 0.6 + phase) * 0.018
                  * (pc.gustAmp * gust + 0.15 * pc.windBase) * pc.windStrength * windDamp;
    // Bend profile: base stays planted, tip displaces most. bendExp is the per-blade FLEX —
    // soft blades (low h2) yield along their whole length, stiff blades hold their base and
    // give mostly at the tip. With SEGMENTS rows this renders as a visible arc, not a shear.
    // Displacer push composes with wind here so the tip-drop length preservation below applies
    // to BOTH: a pushed blade bows over and hugs the ground, it doesn't stretch sideways.
    float bendExp = mix(1.6, 2.4, stiffness);
    float profile = pow(v, bendExp);
    vec2 swayDir = wd * bend + vec2(-wd.y, wd.x) * flutter + pushXZ;
    float swayMag = length(swayDir);
    if (swayMag > 1.4) swayDir *= 1.4 / swayMag;   // total-bend clamp (wind + push composed)
    vec3 windOffset = vec3(swayDir.x, 0.0, swayDir.y) * (profile * H * 2.0);

    vec3 worldPos = rootWorld + widthOffset + windOffset;
    worldPos.y   += v * H;
    // Approximate length preservation: drop the tip as it displaces laterally so strong gusts
    // read as the blade BENDING over, not stretching sideways.
    float lat = length(windOffset.xz);
    worldPos.y -= 0.4 * lat * lat / max(H, 0.001);

    // Colour-sample UV: a stable per-blade point in the grass tile (subtle per-blade variation).
    // Hash-domain coords again — fract() of a raw far coord is quantized.
    vUV = fract(vec2(cellHash.x + root2.x, cellHash.z + root2.y) * 0.5 + vec2(h0, h1) * 0.3);

    // Shadow RECEIVING (location 6): grass was lit by baked ambient ONLY — no sun term and no
    // shadow lookup — so it stayed uniformly bright inside every shadow and read as a flat
    // carpet under trees. worldPos is camera-relative here, which is the space
    // biasedLightSpace expects (same as static_voxel.vert).
    vShadowCoord = ubo.biasedLightSpace * vec4(worldPos, 1.0);

    gl_Position = ubo.lightSpaceMatrix * vec4(worldPos, 1.0);
}
