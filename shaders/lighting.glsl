// ============================================================================================
// lighting.glsl — THE single source of truth for scene lighting.
//
// WHY THIS EXISTS. The ambient model, the shadow lookup and the aerial-perspective curve used
// to be copy-pasted across voxel.frag, grass.frag, foliage.frag, far_terrain.frag and
// far_tree_mesh.frag — five hand-synced implementations with five sets of constants. Every
// lighting change had to be applied five times, and in practice it never was:
//   * grass.frag simply had NO shadow lookup and NO sun term for its entire life — it was
//     ambient-only, which is why grass stayed bright inside shadows and read as a flat carpet;
//   * the shadow bias diverged (0.0006 / 0.002 / 0.0025) with no shared reasoning;
//   * an ambient rebalance applied to voxel.frag alone crushed static voxels to black while
//     leaving vegetation untouched — the desync made visible.
// Consolidating means a lighting change is ONE edit that cannot silently miss a pass.
//
// Sibling of wind.glsl (the same include pattern, resolved relative to this file).
//
// CONTRACT: everything here is a pure function. Nothing reads a global, a UBO or a sampler
// implicitly — samplers and UBO values arrive as parameters — so any shader can include this
// regardless of its own binding layout.
// ============================================================================================

#ifndef PHYXEL_LIGHTING_GLSL
#define PHYXEL_LIGHTING_GLSL

// ---- Tunables. CHANGE THEM HERE AND NOWHERE ELSE. ------------------------------------------
// The hue split (cool from the sky dome, warm from ground bounce) is what reads as natural
// daylight. Both tints average ~1.0 on purpose: they must re-COLOR the fill, never dim it.
// Tints averaging 0.4-0.8 silently cut fill by up to 60%, which was invisible while shadows
// were broken and turned every shaded surface black the moment shadows started working.
const vec3  kSkyTint      = vec3(0.82, 0.94, 1.16);   // cool daylight dome (up-facing)
const vec3  kGroundTint   = vec3(1.06, 0.94, 0.78);   // warm earth/foliage bounce (down-facing)
const float kSkyFill      = 0.50;   // sky ambient as a fraction — FILL, never the key light
const float kAmbientFloor = 0.05;   // keeps sealed cells off pure black before block light

// Aerial perspective. Identical in every pass or the near/far handoff becomes a colour wall.
const float kHazeDensity  = 0.00052;
const float kHazeMax      = 0.72;
const vec3  kHazeColor    = vec3(0.66, 0.76, 0.92);

// Shadow bias, authored in WORLD UNITS and divided by the light volume's depth span at use.
// It used to be a raw normalized-depth constant, which meant its physical size scaled with the
// shadow distance: 0.26 u at a 40 u distance but 0.85 u at 420 — taller than a grass blade, so
// every blade shadow was rejected. MEASURED on the single-blade rig: with the bias pinned to a
// world size, blades cast at 40/80/160/420 alike; with the old constant, only at 40.
const float kBiasWorld        = 0.06;   // solid geometry (normal-offset in the vert does the rest)
const float kBiasSlopeWorld   = 0.18;   // extra for grazing-lit surfaces
// Vegetation sits IN the shadow map with no back-face trick -> needs more headroom.
const float kBiasFoliageWorld = 0.14;

// ---- Ambient --------------------------------------------------------------------------------
/// Hemispherical sky/ground ambient fill for a surface.
///   N               surface normal (world)
///   skyLight        baked skylight 0..1 for this fragment
///   ambientStrength ubo.ambientLight (day/night master)
/// Returns the multiplier to apply to albedo. Gated by a squared skylight curve so interiors
/// fall off fast and stay dramatically dimmer than open sky.
vec3 phxAmbient(vec3 N, float skyLight, float ambientStrength) {
    float skyCurve = skyLight * skyLight;
    float fill     = ambientStrength * skyCurve * kSkyFill;
    vec3  tint     = mix(kGroundTint, kSkyTint, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    return fill * tint + kAmbientFloor;
}

/// The sun's sky-access gate. Surfaces with no sky exposure receive no direct sun.
float phxSkyGate(float skyLight) { return skyLight * skyLight; }

// ---- Shadow bias ----------------------------------------------------------------------------
/// depthRange = ubo.shadowDepthRange (world-unit span of the fitted light volume).
float phxShadowBias(float ndl, float depthRange) {
    float g = clamp(1.0 - ndl, 0.0, 1.0);
    return (kBiasWorld + kBiasSlopeWorld * g * g) / max(depthRange, 1e-3);
}

// ---- Shadow sampling ------------------------------------------------------------------------
// 16-sample Poisson disk, shared by both filters below.
const vec2 kPoisson16[16] = vec2[](
    vec2(-0.94201624,  -0.39906216), vec2( 0.94558609,  -0.76890725),
    vec2(-0.094184101, -0.92938870), vec2( 0.34495938,   0.29387760),
    vec2(-0.91588581,   0.45771432), vec2(-0.81544232,  -0.87912464),
    vec2(-0.38277543,   0.27676845), vec2( 0.97484398,   0.75648379),
    vec2( 0.44323325,  -0.97511554), vec2( 0.53742981,  -0.47373420),
    vec2(-0.26496911,  -0.41893023), vec2( 0.79197514,   0.19090188),
    vec2(-0.24188840,   0.99706507), vec2(-0.81409955,   0.91437590),
    vec2( 0.19984126,   0.78641367), vec2( 0.14383161,  -0.14100790));

/// Per-pixel rotation (interleaved gradient noise). A FIXED disk repeats its pattern across the
/// screen and reads as banding once the filter is wide enough to matter; rotating per pixel
/// turns that into fine dither.
mat2 phxDitherRotation(vec2 fragCoord) {
    float ign = fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
    float a = ign * 6.2831853;
    float c = cos(a), s = sin(a);
    return mat2(c, s, -s, c);
}

/// Dissolve the shadow term over the outer 12% of the map's UV footprint. Snapping to
/// fully-lit at the border drew a visible LINE across the ground at the shadow distance.
float phxShadowBorderFade(vec2 uv) {
    vec2 e = min(uv, 1.0 - uv);
    return clamp(min(e.x, e.y) / 0.12, 0.0, 1.0);
}

/// Is this coord inside the shadow volume at all?
bool phxShadowCoordValid(vec4 c) {
    return c.w > 0.0 && c.z > -1.0 && c.z < 1.0;
}

/// CONTACT-HARDENING shadows (PCSS). A blocker search estimates occluder distance and the
/// filter widens with occluder->receiver separation, so shadows are sharp where objects meet
/// the ground and soften with height. A constant-width filter is the single most "CG" thing
/// about a shadow. kPenumbraScale is a LOOK constant (an exaggerated sun disc — the true 0.53
/// degree sun resolves to ~1 texel and reads aliased-hard).
float phxShadowPCSS(sampler2D shadowMap, vec4 shadowCoord, float ndl, vec2 fragCoord,
                    float depthRange) {
    if (!phxShadowCoordValid(shadowCoord)) return 1.0;
    vec2  texel = 1.0 / textureSize(shadowMap, 0);
    float bias  = phxShadowBias(ndl, depthRange);
    mat2  rot   = phxDitherRotation(fragCoord);

    const float kSearchTexels  = 4.0;
    const float kPenumbraScale = 600.0;
    const float kMinTexels     = 1.5;    // contact: stays sharp where objects meet
    const float kMaxTexels     = 14.0;   // far from the occluder: soft

    float blockerSum = 0.0;
    int   blockerCount = 0;
    for (int i = 0; i < 8; i++) {
        vec2 o = rot * kPoisson16[i * 2] * texel * kSearchTexels;
        float d = texture(shadowMap, shadowCoord.xy + o).r;
        if (shadowCoord.z - bias > d) { blockerSum += d; blockerCount++; }
    }
    if (blockerCount == 0) return 1.0;   // fully lit: skip the filter entirely

    float avgBlocker = blockerSum / float(blockerCount);
    float penumbra = clamp((shadowCoord.z - avgBlocker) * kPenumbraScale, kMinTexels, kMaxTexels);

    float sum = 0.0;
    for (int i = 0; i < 16; i++) {
        vec2 o = rot * kPoisson16[i] * texel * penumbra;
        if (shadowCoord.z - bias > texture(shadowMap, shadowCoord.xy + o).r) sum += 1.0;
    }
    float f = 1.0 - sum * 0.0625;
    return mix(1.0, f, phxShadowBorderFade(shadowCoord.xy));
}

/// CHEAP 4-tap shadow for vegetation (grass blades / leaf cards are numerous and each covers
/// few pixels, so the PCSS blocker search is not worth its cost per fragment). Same bias
/// policy and same border fade as PCSS — only the filter differs.
float phxShadowFast(sampler2D shadowMap, vec4 shadowCoord, float depthRange) {
    if (!phxShadowCoordValid(shadowCoord)) return 1.0;
    vec2 texel = 1.0 / textureSize(shadowMap, 0);
    float kBiasFoliage = kBiasFoliageWorld / max(depthRange, 1e-3);
    float sum = 0.0;
    sum += (shadowCoord.z - kBiasFoliage > texture(shadowMap, shadowCoord.xy + vec2(-0.7, -0.7) * texel).r) ? 1.0 : 0.0;
    sum += (shadowCoord.z - kBiasFoliage > texture(shadowMap, shadowCoord.xy + vec2( 0.7, -0.7) * texel).r) ? 1.0 : 0.0;
    sum += (shadowCoord.z - kBiasFoliage > texture(shadowMap, shadowCoord.xy + vec2(-0.7,  0.7) * texel).r) ? 1.0 : 0.0;
    sum += (shadowCoord.z - kBiasFoliage > texture(shadowMap, shadowCoord.xy + vec2( 0.7,  0.7) * texel).r) ? 1.0 : 0.0;
    float f = 1.0 - sum * 0.25;
    return mix(1.0, f, phxShadowBorderFade(shadowCoord.xy));
}

// ---- Shadow-only debug view -----------------------------------------------------------------
/// Strip albedo, ambient, block light and haze; show ONLY the shadow term. White = fully lit,
/// black = fully shadowed, greys = penumbra. Enabled per frame via ubo.debugShadowMode
/// (POST /api/debug/shadow {"mode":1}).
///
/// WHY IT EXISTS: thin casters are effectively unreadable against textured, lit ground — a
/// grass blade's shadow is a hairline that albedo detail and ambient completely mask, which is
/// why "does grass cast?" resisted several rounds of screenshot inspection. Every engine ships
/// an equivalent (Unreal's Lighting Only / shadow buffer views). A slight grey floor keeps
/// full shadow distinguishable from a black background.
vec4 phxShadowOnly(float shadowFactor) {
    return vec4(vec3(mix(0.04, 1.0, clamp(shadowFactor, 0.0, 1.0))), 1.0);
}

// ---- Aerial perspective ---------------------------------------------------------------------
/// Exponential single-scatter haze toward a daylight horizon, warm-tinted looking sunward.
/// Distance is what sells scale: far ridges sit in atmosphere instead of rendering paint-flat.
///   camToFrag  vector from the CAMERA to the fragment, world scale. Note the near pass renders
///              camera-relative (so the fragment position IS this vector) while the far passes
///              are world-space (so it is worldPos - cameraWorld). Getting that wrong measures
///              distance from the world origin and washes distant content white.
vec3 phxAerialPerspective(vec3 color, vec3 camToFrag, vec3 sunDir, vec3 sunColor) {
    float dist = length(camToFrag);
    float haze = 1.0 - exp(-dist * kHazeDensity);
    float sunAmt = pow(max(dot(camToFrag / max(dist, 1.0), normalize(-sunDir)), 0.0), 8.0);
    vec3  hazeCol = mix(kHazeColor, sunColor * 0.85, sunAmt * 0.5);
    return mix(color, hazeCol, haze * kHazeMax);
}

#endif // PHYXEL_LIGHTING_GLSL
