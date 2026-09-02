#version 450
#extension GL_GOOGLE_include_directive : require
#include "wind.glsl"
#include "grass_sites.glsl"   // progressive blue-noise lattice (tools/gen_grass_site_order.py)

// Lightweight grass blades. ONE instance per grass-topped voxel (GrassInstanceData); this shader
// procedurally fans it into `bladesPerVoxel` blades using gl_VertexIndex (SEGMENTS stacked
// quads per blade, no vertex buffer). Two silhouettes (pc.bladeStyle): SMOOTH (default since
// 2026-08-21, user call) = ribbon tapering to a point; BOXY voxel-aesthetic = thin elongated
// crisp RECTANGLE, rest height quantized to the 1/9-voxel microcube grid (available via
// /api/debug/grass bladeStyle). Both share the SAME smooth wind motion — the shared
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
    // Near shadow cascade (docs/NearShadowCascade.md) — receivers min-compose both maps.
    mat4 biasedLightSpaceNear;
    vec4 shadowCascadeNear;     // x = range end (0 = off), y = near depthRange
    mat4 lightSpaceMatrixNear;  // raw near matrix (caster pass only)
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
    float windScrollX;       // CPU-integrated gust-field offset (NOT dir*gustSpeed*time)
    float windScrollZ;
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
    // ── MEADOW HEIGHT FIELD (POST /api/debug/grass) ────────────────────────────────────────
    // The plain-scale height modifier: a two-octave value noise in ABSOLUTE world space, so it
    // is identical either side of any voxel or chunk boundary by construction. Periods are in
    // world units — meadowScale is the one that decides how large a "plain" reads as.
    float meadowScale;         // dominant octave period, world units (bigger = broader zones)
    float meadowDetailScale;   // detail octave period, world units
    float meadowDetailWeight;  // 0..1 blend of the detail octave (dominant takes the remainder)
    float heightMin;           // height multiplier in the most cropped zones
    float heightMax;           // height multiplier in the lushest zones
    // ── EDGE TAPER ────────────────────────────────────────────────────────────────────────
    float edgeTaperFloor;      // height multiplier at a fully-exposed edge (0 = bald, 1 = off)
    float edgeTaperCurve;      // >1 keeps full height further in then falls fast; <1 eases out
    // Gust-front anisotropy: how many times longer a front is CROSSWIND than along-wind.
    // 1 = isotropic blobs (the old field); higher = bands sweeping across the field.
    float windAniso;
    float flutterFreq;       // local blade quiver, Hz (NOT the gust travel speed)
} pc;

layout(location = 0) out flat uint vTex;   // grass texture index
layout(location = 1) out vec2  vUV;        // colour-sample UV into the grass tile
layout(location = 2) out float vGrad;      // 0 at blade base .. 1 at tip (silhouette + AO)
layout(location = 3) out float vSide;      // -1..1 across blade width (silhouette taper)
layout(location = 4) out float vSky;       // baked skylight 0..1
layout(location = 6) out vec4  vShadowCoord; // biased light-space coord (shadow RECEIVING)
// WIND DEBUG (ubo.debugShadowMode == 2): how far the wind is pushing THIS vertex, as a fraction
// of its own arc length — i.e. sin(lean angle), 0 = upright, 0.9 = at the lean cap. Published
// always (a few bytes of interpolant) rather than behind a compile flag, because the whole point
// is to answer "is the wind actually doing anything" without a rebuild.
layout(location = 7) out float vWindLean;
layout(location = 8) out vec4 vShadowCoordNear;   // near-cascade receiving coord
// U3.3: point/spot lights need a position to attenuate from. Camera-relative, matching the
// convention voxel.frag uses (add ubo.cameraWorld to get true world).
layout(location = 9) out vec3 vWorldPos;

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

// LATTICE-PERIODIC value noise, period `rep` lattice cells. The hash domain wraps every 2048
// world units (see cellHash below) — and mod() puts a wrap line THROUGH THE WORLD ORIGIN — so any
// field meant to be seam-free must tile with that wrap. Wrapping the LATTICE coordinates before
// hashing makes the noise itself periodic; the caller passes rep = 2048 / worldPeriod, which is
// only seamless when worldPeriod divides 2048 (the shipped meadow/patch periods do — keep it so).
float vnoise2p(vec2 p, float rep) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(mod(i,                  vec2(rep)));
    float b = hash21(mod(i + vec2(1.0, 0.0), vec2(rep)));
    float c = hash21(mod(i + vec2(0.0, 1.0), vec2(rep)));
    float d = hash21(mod(i + vec2(1.0, 1.0), vec2(rep)));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    // Decode voxel local position + baked light.
    uint packed = inPacked;
    float lx = float(packed & 0x1Fu);
    float ly = float((packed >> 5) & 0x1Fu);
    float lz = float((packed >> 10) & 0x1Fu);
    vSky      = float((packed >> 15) & 0xFu) / 15.0;
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
    const int SEGMENTS = 4;
    const int VERTS_PER_BLADE = SEGMENTS * 6;   // must match vertsPerBlade in GrassRenderPipeline.cpp
    int blade  = gl_VertexIndex / VERTS_PER_BLADE;
    int rem    = gl_VertexIndex - blade * VERTS_PER_BLADE;
    int segIdx = rem / 6;
    int corner = rem - segIdx * 6;

    // ── PLACEMENT: ONE BLADE PER LATTICE CELL, NEVER OVERLAPPING ──────────────────────────────
    // Blades used to be grouped into tufts of 7 jittering inside a FIXED +/-0.08u box regardless of
    // blade width, and each voxel clamped its roots into its own [0.005,0.995] independently. Both
    // guaranteed overlap: 7 blades of width w in a 0.16 box collide for any w > ~0.023, and a root
    // at 0.995 sits 0.01u from its neighbour's at 0.005 across the voxel border. Measured on the
    // live engine: 28 blades resolved to 10 distinguishable regions, 112 blades to 8 — blade pixels
    // grew 29x while visible structure saturated (docs/evidence/pack_before.json).
    //
    // Now each blade owns one cell of a WORLD-ALIGNED 16x16 lattice (grass_sites.glsl). Because the
    // lattice is aligned to the voxel grid, cell centres tile continuously across voxel AND chunk
    // borders — the cross-border case needs no special handling at all, which is what makes the
    // guarantee total rather than per-voxel.
    //
    // ⚑The ordering is PROGRESSIVE (blue-noise): every prefix is well-spread. That is load-bearing,
    //  not decoration — the LOD thins the field by drawing only the first N blades, so each prefix
    //  is a distribution that actually ships at some distance. It also preserves the stability
    //  contract for free: survivors keep their cells, so shortening the draw disturbs nothing.
    //
    // ⚑THE FORMER TUFTING IS DELETED ON PURPOSE. The comment that used to live here argued "even
    //  spacing reads as isolated spikes; clustered blades read as grass". Overruled by direct
    //  observation (user, 2026-08-05): tufting left the voxel face mostly bare with a few dense
    //  bunches, and the call was "more spread out with less grouping should be the default". If
    //  spread-out ever does read as spiky, the lever is MORE BLADES, not re-clustering them.
    uint  siteIdx  = uint(blade) & (kGrassGrid * kGrassGrid - 1u);
    vec2  cellCtr  = grassSiteCentre(siteIdx);

    // Per-blade hash, seeded on (cell, blade). Drives height/yaw/stagger/wind — and, in h4/h5,
    // the jitter. Jitter gets its OWN slots so that shrinking the jitter radius does not also
    // change per-blade colour (vUV reads h0/h1). (Computed BEFORE the meadow block: the field is
    // sampled at the blade ROOT, so the root must exist first.)
    vec2 seed = vec2(cellHash.x * 3.17 + cellHash.z * 7.71 + float(blade) * 13.1,
                     cellHash.z * 2.39 - cellHash.x * 5.11 + float(blade) * 7.31);
    float h0 = hash21(seed);
    float h1 = hash21(seed + 11.7);
    float h2 = hash21(seed + 23.3);
    float h3 = hash21(seed + 41.9);
    float h4 = hash21(seed + 57.1);
    float h5 = hash21(seed + 71.3);

    // JITTER, bounded so the non-overlap guarantee survives it. The lattice alone would be a
    // visible grid; jitter breaks both the lattice and the 1-unit tiling repeat (every voxel uses
    // the same 256-cell pattern). The budget is whatever spacing is left over after the blade's
    // own width:
    //     sepGuaranteed(N) = kGrassSeqSep/sqrt(N)      (CONTINUOUS envelope — see below)
    //     jitterRadius     = (sepGuaranteed - width/kPack) / 2
    // At the counts under consideration this is generous (+/-0.044u against a 0.0625u pitch at 30
    // blades), so blades wander most of a cell and no lattice is visible.
    //
    // ⚑CONTINUOUS ENVELOPE, NEVER THE PER-N STAIRCASE. The real minSep is a staircase (it drops at
    //  each refinement level of the sequence; measured worst point is N=129, right after 128). Two
    //  ADJACENT voxels can keep different N — one at 128, one at 129 — and the staircase would let
    //  the sparser one assume a separation its neighbour does not honour. They are neighbours in
    //  world space, so that is an overlap. Same bug class as the chunk seam.
    // Jitter is a FIXED FRACTION of the guaranteed spacing, not a function of blade width. Deriving
    // it from the width would be circular — width depends on distance, distance on the root, the
    // root on the jitter. Fixing the fraction here and CLAMPING THE WIDTH to whatever spacing is
    // left (see kMaxPackWidth below) breaks the cycle and is provably safe: the two constants are
    // the only inputs, and both are compile-time.
    const float kPackMargin = 0.95;
    const float kJitterFrac = 0.25;    // of sepGuard; leaves 0.475*sepGuard for the blade width
    float sepGuard  = kGrassSeqSep * inversesqrt(float(max(pc.bladesPerVoxel, 1u)));
    float jitterRad = kJitterFrac * sepGuard;
    // Bound the jitter VECTOR, not each axis: a per-axis bound of r admits a diagonal of r*sqrt(2)
    // and would silently spend more of the budget than it claims.
    vec2  jitter    = (vec2(h4, h5) - 0.5) * (2.0 * jitterRad * 0.70710678);
    // NO clamp to [0.005,0.995]: the lattice already keeps every root inside its own cell, and the
    // clamp was itself the cross-voxel overlap mechanism (it pulled roots onto the shared border).
    vec2 root2  = cellCtr + jitter;
    vec3 rootWorld = cellBase + vec3(root2.x, 0.0, root2.y);
    // The wrapped ABSOLUTE root position — the domain every world-space field below samples.
    // Fields sample the ROOT, never the voxel corner: a corner-sampled field is piecewise
    // constant per voxel, and the resulting height plateaus made the voxel/chunk grid readable
    // in the grass (user, 2026-08-21; pinned by tests/graphics/GrassMeadowSeamTest.cpp).
    vec2 fieldP = cellHash.xz + root2;

    // MEADOW field: one smooth low-frequency noise drives BOTH blade height and coverage, so tall
    // lush zones and shorter zones drift across the field while any few-meter neighborhood stays
    // locally even. (The old per-5-voxel/per-2-voxel hash patches made short-scale holes +
    // per-blade height chaos — the exact opposite of the wanted look.)
    // The DOMINANT octave is deliberately long (~64 voxels): the point is height varying across
    // a FIELD, read while walking over it, not per-voxel roughness. The ~32-voxel octave only
    // keeps the gradient from looking like a rendered gradient; anything shorter than that reads
    // as noise and undoes the smoothness.
    // Periods are world units and MUST DIVIDE 2048 (the hash-domain wrap) or the field seams at
    // every wrap line — vnoise2p tiles the lattice to make divisor periods exactly seamless.
    // Guard against a zero/negative scale from the API turning the field into a constant (or NaN).
    float mScale  = max(pc.meadowScale, 1.0);
    float mDetail = max(pc.meadowDetailScale, 1.0);
    float mW      = clamp(pc.meadowDetailWeight, 0.0, 1.0);
    float meadow = vnoise2p(fieldP / mScale, 2048.0 / mScale)  * (1.0 - mW)
                 + vnoise2p(fieldP / mDetail + 41.7, 2048.0 / mDetail) * mW;

    // PATCH field: mid-frequency (~4-voxel) coverage variation, sampled at the root like
    // everything else. Two jobs (user, 2026-08-21): (1) grass density gets natural patchiness
    // instead of reading evenly planted; (2) each voxel keeps a DIFFERENT subset of the shared
    // 256-site sequence, which kills the "same constellation tiles every voxel" repeat that the
    // lattice alone produces. This is a CONTINUOUS field, deliberately not tufting — clustered
    // blades were tried and overruled (2026-08-05); patchiness must come from smooth world-space
    // fields, never discrete clumps.
    float patchy = vnoise2p(fieldP * 0.25 + 17.3, 512.0);   // ("patch" is a GLSL keyword)

    // Coverage maps through the keep threshold below (0.18 + 0.42*rank): mean ~0.53 keeps ~83%
    // of blades, lush cells keep all, the sparsest patches still keep ~50% — patchy, NEVER bald.
    float coverage = 0.39 + 0.10 * meadow + 0.18 * patchy;
    // RANK of this blade in the progressive ordering, 0..1. The LOD and the coverage both thin
    // from the tail of the sequence, which the blue-noise prefix property keeps well-spread.
    float rankFrac = (float(blade) + 0.5) / float(max(pc.bladesPerVoxel, 1u));
    // Soft threshold (was a hard step): blades near a patch edge SHORTEN instead of vanishing,
    // so patches end in fringes rather than contour lines.
    float thr  = 0.18 + 0.42 * rankFrac;
    float keep = smoothstep(thr - 0.06, thr + 0.02, coverage);

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
    float heightMul = mix(pc.heightMin, pc.heightMax, smoothstep(0.06, 0.94, meadow));

    // EDGE TAPER (world-look C4): the mesher bakes each voxel's count of grass-topped
    // horizontal neighbours (0-8) into inTex bits 16-19; interior voxels (8/8) keep full
    // height and boundary voxels shorten, so a lawn ends in a low fringe against dirt, sand,
    // stone or a drop instead of a hard green cliff. Cross-chunk neighbours are baked as
    // grassy, so this can NEVER differ across a chunk boundary in open meadow (no seams —
    // the per-chunk-artifact lesson of the density LOD). The floor is deliberately well
    // above 0: bare edge voxels should read as short grass, not as missing grass.
    float edgeFrac  = float((inTex >> 16) & 0xFu) / 8.0;
    // edgeTaperCurve reshapes the ramp without moving its endpoints: >1 holds full height further
    // toward the edge then drops sharply, <1 starts falling early and eases out.
    float edgeRamp  = pow(smoothstep(0.10, 0.90, edgeFrac), max(pc.edgeTaperCurve, 0.01));
    float edgeTaper = mix(clamp(pc.edgeTaperFloor, 0.0, 1.0), 1.0, edgeRamp);

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
    float lodKeep     = 1.0 - smoothstep(densityFrac - 0.14, densityFrac, rankFrac);

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
    const float kShadowWidthGate = 0.0;

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
    // Smooth widened 0.036 → 0.042 (2026-08-21): the true-point taper halves a blade's
    // silhouette area vs the rectangle, so the pointy default needs a wider base to hold the
    // same ground coverage. 0.042 stays under the packing clamp at the shipped 55 blades/voxel
    // (maxPackWidth 0.0432) so the clamp never trims a near-field blade.
    float bladeWidth = (boxy ? 0.040 : 0.042) * min(inversesqrt(max(densityFrac, 0.02)), 2.6)
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

    // ── THE PACKING CLAMP — what actually makes non-overlap unconditional ─────────────────────
    // A blade may never be wider than the spacing left after jitter, or two blades whose roots are
    // exactly sepGuard apart would touch. Everything above is a REQUEST; this is the bound.
    //
    // sepGuard - 2*jitterRad = sepGuard*(1 - 2*kJitterFrac) = 0.475*sepGuard at kJitterFrac 0.25.
    //
    // ⚑This is also the far-field guard, and it is the reason the guarantee survives distance at
    //  all. The sub-pixel floor above is a SCREEN-space width: in world units it grows without
    //  bound (0.246u at the 224u radius edge), so it eventually exceeds any fixed spacing. Density
    //  falloff thins the field as distance grows, which RAISES sepGuard... but only until the 1/18
    //  density floor stops it, at which point the floor keeps growing and would overrun the
    //  spacing. Clamping here trims the blade instead of letting it overlap. Where density
    //  compensation is active the two are exactly balanced (width ~ 1/sqrt(df), spacing ~
    //  1/sqrt(N*df) — the df cancels), so this clamp is inert through the whole mid-field.
    //
    // The bound is scaled by shadowWidthScale in the shadow pass so the proxy stays its intended
    // multiple of the (clamped) visible blade. The guarantee is a statement about VISIBLE blades:
    // overlapping shadow casters union harmlessly in the depth buffer.
    float maxPackWidth = kPackMargin * sepGuard * (1.0 - 2.0 * kJitterFrac);
    bladeWidth = min(bladeWidth, maxPackWidth * mix(1.0, pc.shadowWidthScale, kShadowWidthGate));

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
    // Trodden blades are pinned underfoot — they stop waving instead of thrashing while flat.
    float windDamp = 1.0 - 0.6 * tread;
    // ⚑THE 4-TAP BOX LOW-PASS THAT USED TO BE HERE IS GONE — IT WAS DOING NOTHING.
    // It averaged the field at t, t-0.33, t-0.66 and t-1.00 to "remove jitter", at 4x the gust
    // evaluations (8 value-noise calls per vertex, on every grass AND foliage vertex).
    // MEASURED with tools/wind_field_probe.py, which evaluates windGustAt exactly on the CPU:
    // the temporal signal at a fixed point carries only 0.2% of its energy above 0.5 Hz BEFORE
    // filtering. There was no high-frequency content to remove; the filter cost 75% of the wind
    // ALU and changed peak-to-peak by 0.02. Whatever fixed the original "far too jittery" report,
    // it was the flutter and response-spread reductions made at the same time, not this.
    // The jitter was SPATIAL, not temporal — small isotropic blobs (12u across) rather than
    // field-scale fronts. That is fixed in windGustAt by anisotropy, where it actually lives.
    float gust = windGustAt(fieldP, vec2(pc.windScrollX, pc.windScrollZ), wd,
                            pc.gustScale, pc.windAniso);
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
    float flutter = sin(ubo.elapsedTime * max(pc.flutterFreq, 0.0) * 6.2831853 + phase) * 0.018
                  * (pc.gustAmp * gust + 0.15 * pc.windBase) * pc.windStrength * windDamp;
    // REST BEND (user, 2026-08-21: blades should not stand perfectly straight): a small STATIC
    // per-blade lean in a hashed random direction, composed with the wind exactly like a
    // constant breeze so it rides the same profile/clamp/length-preservation below. Static ⇒
    // time-independent ⇒ the wind-0 bit-identical stillness invariant (WindSystemTest) holds.
    // Magnitude 0.025–0.075 in sway units ≈ a 3–9° arc at rest (leanSin ≈ 2× sway at the tip).
    float restYaw = hash21(seed + 89.7) * 6.2831853;
    float restMag = 0.025 + 0.050 * hash21(seed + 103.9);
    vec2  restSway = vec2(cos(restYaw), sin(restYaw)) * restMag;

    // Bend profile: base stays planted, tip displaces most. bendExp is the per-blade FLEX —
    // soft blades (low h2) yield along their whole length, stiff blades hold their base and
    // give mostly at the tip. With SEGMENTS rows this renders as a visible arc, not a shear.
    // Low end 1.6 → 1.3 (2026-08-21): soft blades now curl along more of their length, so a
    // bent blade reads as a CURVE rather than a leaning line.
    // Displacer push composes with wind here so the tip-drop length preservation below applies
    // to BOTH: a pushed blade bows over and hugs the ground, it doesn't stretch sideways.
    float bendExp = mix(1.3, 2.4, stiffness);
    float profile = pow(v, bendExp);
    vec2 swayDir = wd * bend + vec2(-wd.y, wd.x) * flutter + pushXZ + restSway;
    float swayMag = length(swayDir);
    if (swayMag > 1.4) swayDir *= 1.4 / swayMag;   // total-bend clamp (wind + push composed)
    vec3 windOffset = vec3(swayDir.x, 0.0, swayDir.y) * (profile * H * 2.0);

    // ── LENGTH PRESERVATION: EXACT CHORD GEOMETRY, NOT A QUADRATIC FUDGE ─────────────────────
    // A point at arc length L = v*H along the blade, displaced `lat` from the vertical axis, sits
    // at height sqrt(L^2 - lat^2). Bounded by construction: the tip can NEVER drop below its own
    // root, however hard the wind blows.
    //
    // ⚑WHAT THIS REPLACES, AND WHY IT MATTERS: `worldPos.y -= 0.4 * lat*lat / H` was unbounded
    //  and grew quadratically. The sway clamp above allows |swayDir| up to 1.4, and windOffset
    //  scales it by profile*H*2.0, so lat reaches 2.8*H — giving a drop of 0.4*(2.8H)^2/H = 3.14H
    //  against a blade only H tall. The tip finished 2.1*H BELOW its own root: grass sinking
    //  through the ground and out the underside of the slab under strong wind (user, 2026-08-05).
    //  Invisible at the shipped windStrength 0.13 (drop ~0.03u); it appeared the moment wind was
    //  turned up. A bound that only holds at one setting is not a bound.
    // MAXIMUM LEAN. Not just "don't stretch" (lat <= armLen) — that permits lat == armLen, which
    // puts the tip at height ZERO, i.e. the blade lying flat IN the ground plane it grows from.
    // Coplanar with the surface reads as grass pointing into the ground, which is what the old
    // unbounded drop looked like even after it stopped going below the root.
    //
    // The underlying problem is that the bend magnitude was never physical: the sway clamp allows
    // |swayDir| <= 1.4 and windOffset scales it by profile*H*2.0, so lateral displacement could
    // reach 2.8x the blade's own arc length. Nothing bends 2.8 times its own length.
    //
    // 0.90 * armLen is a 64-degree lean from vertical, leaving the tip at sqrt(1-0.81) = 43% of
    // its height: bent right over in a hard gust, but never flat and never coplanar. Real grass
    // flattened by a storm still rests ON the surface, not in it.
    const float kMaxLeanSin = 0.90;
    float armLen = v * H;                     // arc length from the root to this vertex
    float maxLat = kMaxLeanSin * armLen;
    vec2  latXZ  = windOffset.xz;
    float lat    = length(latXZ);
    if (lat > maxLat) { latXZ *= maxLat / max(lat, 1e-6); lat = maxLat; }

    vec3 worldPos = rootWorld + widthOffset + vec3(latXZ.x, 0.0, latXZ.y);
    worldPos.y   += sqrt(max(armLen * armLen - lat * lat, 0.0));

    // Lean fraction for the wind debug view: 0 upright .. kMaxLeanSin at the cap. Taken AFTER the
    // clamp so it shows what the blade actually does, not what was requested.
    vWindLean = (armLen > 1e-5) ? lat / armLen : 0.0;

    // Colour-sample UV: a stable per-blade point in the grass tile (subtle per-blade variation).
    // Hash-domain coords again — fract() of a raw far coord is quantized.
    vUV = fract(vec2(cellHash.x + root2.x, cellHash.z + root2.y) * 0.5 + vec2(h0, h1) * 0.3);

    // Shadow RECEIVING (location 6): grass was lit by baked ambient ONLY — no sun term and no
    // shadow lookup — so it stayed uniformly bright inside every shadow and read as a flat
    // carpet under trees. worldPos is camera-relative here, which is the space
    // biasedLightSpace expects (same as static_voxel.vert).
    vShadowCoord = ubo.biasedLightSpace * vec4(worldPos, 1.0);
    // Near-cascade coord (location 8): the fine map that actually resolves blade-on-blade
    // shadows. Out-of-volume coords fail phxShadowCoordValid in the frag → min() no-op.
    vShadowCoordNear = ubo.biasedLightSpaceNear * vec4(worldPos, 1.0);

    vWorldPos   = worldPos;   // U3.3: camera-relative, for the point-light loop in grass.frag
    gl_Position = ubo.viewProj * vec4(worldPos, 1.0);
}
