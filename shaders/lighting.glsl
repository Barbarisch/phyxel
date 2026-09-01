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

// ---- Atmosphere-driven ambient (phxAmbientAtmos) ----------------------------------------------
// Separate constants from the legacy pair above, because these multiply a PHYSICAL sky radiance
// rather than a 0..1 strength scalar, so the numbers are not comparable and sharing them would be a
// trap. The fill is deliberately near 1.0: the sky's own radiance already carries the right
// magnitude, and scaling it down was how the old model ended up needing a separate brightness knob.
const float kSkyFillAtmos     = 1.00;
// Ground bounce: the sky reflected off the world. 0.30 is a reasonable mid albedo for grass, dirt
// and stone, and it is deliberately NOT re-tinted — the hue comes from the scattering model.
const float kGroundBounce     = 0.30;
// A sealed cell with no block light still gets a whisper of the sky rather than absolute black, at a
// far lower level than the legacy 0.05 flat term because interiors are now genuinely dark (the light
// bake stopped leaking daylight) and this is the only thing standing between a windowless room and
// pure void. Scaled BY the sky colour, so it vanishes at night instead of glowing on forever.
const float kAmbientFloorAtmos = 0.02;

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

/// Hemispherical fill driven by the ATMOSPHERE instead of a constant tint pair. `skyColor` is
/// Atmosphere::skyIrradiance for the current sun, so the fill is cool blue by day, warm at sunset
/// and near-black at night WITHOUT a separate ramp — and shadows inherit the sky's colour, which is
/// the single biggest ingredient in light reading as natural.
///
/// The ground term is the sky bounced off the world: same colour, dimmer and warmer. Nothing here
/// re-tints for taste; the hue comes from the scattering model.
vec3 phxAmbientAtmos(vec3 N, float skyLight, vec3 skyColor) {
    float skyCurve = skyLight * skyLight;
    float up = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ground = skyColor * kGroundBounce;
    return mix(ground, skyColor, up) * (skyCurve * kSkyFillAtmos) + skyColor * kAmbientFloorAtmos;
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
/// `hazeHorizon` / `hazeZenith` are Atmosphere::hazeHorizon/hazeZenith — the SKY's own radiance
/// looking level and looking up. Blending between them by view-direction Y means distant geometry
/// fades into the colour of the sky actually behind it, at every time of day, instead of toward a
/// fixed pale blue that was right at noon and wrong at dusk. It is also what unifies the render
/// tiers: near voxels, far terrain tiles and instanced far trees all inherit the same curve, so the
/// handoff between them reads as air rather than as a colour step.
vec3 phxAerialPerspective(vec3 color, vec3 camToFrag, vec3 sunDir, vec3 sunColor,
                          vec3 hazeHorizon, vec3 hazeZenith) {
    float dist = length(camToFrag);
    vec3  dir  = camToFrag / max(dist, 1e-4);
    float haze = 1.0 - exp(-dist * kHazeDensity);
    float sunAmt = pow(max(dot(dir, normalize(-sunDir)), 0.0), 8.0);
    // Looking up sees the zenith end; looking level or down sees the horizon end.
    vec3 hazeCol = mix(hazeHorizon, hazeZenith, clamp(dir.y, 0.0, 1.0));
    // Forward Mie: looking toward the sun, the haze takes the sun's own colour.
    hazeCol = mix(hazeCol, sunColor * 0.85, sunAmt * 0.5);
    return mix(color, hazeCol, haze * kHazeMax);
}

// ---- Exposure + tone mapping ------------------------------------------------------------------
// WHY THIS LIVES HERE AND WHY IT IS SUDDENLY REQUIRED. The sky became a physical scattering model
// (atmosphere.glsl), and a physical model returns RADIANCE: a noon sky is around 0.02 and a lit
// diffuse surface around 0.1. The flat clear colour it replaced was a display-referred 0.45-0.95.
// Sending radiance straight to an 8-bit display therefore produced a nearly black frame — measured,
// not guessed. Exposure is the unit conversion that makes the model viewable, and a tone curve is
// what stops the parts that ARE bright (the sun's disc, a hearth) from clipping to flat white.
//
// Clipping is not a cosmetic problem. A clipped pixel carries NO shading information, so a frame
// full of them reads as flat no matter how good the lighting behind it is — measured at 29.7% of a
// hearth-lit room's interior before this existed.
//
// AgX, not ACES: ACES desaturates bright colours toward white, which is exactly the "washed out"
// look this work is trying to remove, and it would bleach the warm sun and firelight that the
// atmosphere model works hard to produce. AgX keeps hue into the highlights.
//
// ⚠️ STAGING NOTE. Ideally this runs ONCE in a post-process grade pass, not per scene shader. It is
// here because the editor viewport samples the RAW offscreen scene image and never sees the
// swapchain post-process pass — the documented reason bloom, SSAO and the old Reinhard tonemap were
// all disabled and shipped broken in packaged games. Until that grade pass exists, tonemapping in
// the scene shaders is the only form of it an author can actually SEE. The function lives in one
// file so moving it later is a deletion, not a rewrite.

// AgX log-encoding range, in stops. The ~16.5-stop span is the reference AgX configuration.
const float kAgxMinEv = -12.47393;
const float kAgxMaxEv =   4.026069;

// AgX does its curve in slightly rotated ("inset") primaries and rotates back out afterwards. THAT
// is where the hue preservation comes from: a per-channel sigmoid applied in the render primaries
// skews hues as it compresses (bright orange drifts yellow, bright blue drifts cyan), and the inset
// keeps the three channels from separating. Values are the reference AgX minimal matrices; GLSL mat3
// constructors take COLUMNS.
//
// ⚠️ THESE LITERALS ARE TRANSPOSED ON PURPOSE. The matrices are published ROW-MAJOR (each row sums
// to 1.0, which is what preserves neutrals), but GLSL's mat3(...) constructor consumes COLUMNS. The
// first version of this file listed the published rows directly and so used both matrices
// transposed: grey stopped mapping to grey. Round-tripping 0.18 grey gave (0.176, 0.153, 0.211) --
// blue high, green low -- and every surface in the engine picked up a pale violet cast with lifted
// blacks. The user described it as "everything looks like it's under a blacklight", which is exactly
// right. The comment two lines up already said "constructors take COLUMNS"; the values ignored it.
//
// Each line below is therefore a COLUMN of the published matrix. Invariant, worth a test if this is
// ever touched: mat * vec3(g) must return vec3(g) for any g.
const mat3 kAgxInset = mat3(
    0.8566271533,  0.1373189729,  0.1118982130,   // column 0
    0.0951212405,  0.7612419906,  0.0767994186,   // column 1
    0.0482516061,  0.1014390365,  0.8113023684);  // column 2
const mat3 kAgxOutset = mat3(
     1.1271005818, -0.1413297635, -0.1413297635,  // column 0
    -0.1106066431,  1.1578237022, -0.1106066431,  // column 1
    -0.0164939387, -0.0164939387,  1.2519364066); // column 2

/// 6th-order polynomial fit of the AgX sigmoid — cheaper than the piecewise original and visually
/// indistinguishable across the encoded range.
vec3 phxAgxContrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return  15.5     * x4 * x2
          - 40.14    * x4 * x
          + 31.96    * x4
          -  6.868   * x2 * x
          +  0.4298  * x2
          +  0.1191  * x
          -  0.00232;
}

/// Apply exposure and the selected tone curve, returning a LINEAR value.
///   curve 0 = none (raw linear x exposure — the A/B control, and the old look)
///   curve 1 = AgX
/// ⚠️ Returns LINEAR, not display-encoded. The scene target is linear and the sRGB encode happens in
/// hardware at the swapchain, so AgX's display-referred output is converted back with the 2.2 power
/// at the end. Dropping that step double-gammas the frame and washes it out — the exact bug that
/// once shipped in packaged games.
vec3 phxTonemap(vec3 color, float exposure, int curve) {
    color = max(color * exposure, vec3(0.0));
    if (curve == 0) return color;

    vec3 v = kAgxInset * color;
    v = clamp(log2(max(v, vec3(1e-10))), vec3(kAgxMinEv), vec3(kAgxMaxEv));
    v = (v - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv);
    v = clamp(phxAgxContrast(v), vec3(0.0), vec3(1.0));

    // A modest saturation lift: the sigmoid desaturates slightly by construction and voxel albedo is
    // already flat-ish, so pulling a little colour back keeps materials reading as materials. Kept
    // small deliberately — this is the knob that turns "filmic" into "cartoon".
    float luma = dot(v, vec3(0.2126, 0.7152, 0.0722));
    v = luma + (v - luma) * 1.10;

    v = kAgxOutset * v;
    return pow(max(v, vec3(0.0)), vec3(2.2));   // display-referred -> LINEAR
}

#endif // PHYXEL_LIGHTING_GLSL
