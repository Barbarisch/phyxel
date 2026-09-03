// Shared procedural wind field — included by grass.vert, foliage.vert, foliage_shadow.vert.
// Include with:
//   #extension GL_GOOGLE_include_directive : require
//   #include "wind.glsl"
//
// The CPU WindSystem (graphics/WindSystem.{h,cpp}) owns global wind state (direction drift,
// base strength, gust shape) and writes the same scalars into every consumer's push constants
// each frame; this file evaluates the actual gust field analytically per vertex. windGustAt()
// is 2-octave value noise scrolled ALONG the wind direction, so gust fronts visibly travel
// across the field at gustSpeed world-units/second.
//
// IMPORTANT: pHash must be hash-domain coordinates (world position wrapped mod 2048) — raw
// far-from-origin world coords lose all fractional precision inside the lattice hash (the
// documented grass.vert footgun). The 2048-unit repeat only shifts gust phase at the seam;
// direction and average strength are identical on both sides, so it is imperceptible.

// Integer avalanche hash of a LATTICE point (the inputs are whole-number floats by
// construction — every call site passes floor()ed coordinates). Replaces the classic
// fract(p*[127.1,311.7])-style float hash (2026-09-03): in float32 that hash's quality decays
// with |input| (fract() of p*311.7 keeps ever fewer significant bits across the wrapped
// domain) and its failures CORRELATE along lattice rows/columns — visible in the mode-3
// field map as faint straight creases ~one broad-octave cell apart, i.e. yet another
// straight-line source. Integer hashing is magnitude-immune and platform-deterministic
// (same construction as WindSystem::hash1 on the CPU).
float windHash21(vec2 p) {
    uvec2 u = uvec2(ivec2(p) + 0x8000);
    uint h = u.x * 0x9E3779B9u ^ u.y * 0x85EBCA6Bu;
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x846CA68Bu;
    h ^= h >> 16;
    return float(h & 0xFFFFFFu) / 16777216.0;
}

// GRADIENT (Perlin-style) noise in ~[0,1], C2-smooth (quintic fade).
// ⚑VALUE NOISE WAS THE STRAIGHT-LINE FACTORY (2026-09-03, found the moment the mode-3 field
//  map existed — user: "i see straight lines all over the debug view"). Bilinear value noise
//  has its extrema exactly ON the lattice lines and its cubic fade is only C1, so every cell
//  boundary is a curvature crease that the eye reads as a straight line (Mach banding); the
//  anisotropic stretch then rotates and elongates that straight lattice structure across the
//  whole field — which is why gust fronts kept reading as ruled lines through TWO rounds of
//  domain warping applied upstream of it. Gradient noise zeroes on the lattice, peaks
//  mid-cell, and the quintic fade is C2: no creases to warp away in the first place.
vec2 windGrad2(vec2 i) {
    float a = windHash21(i) * 6.2831853;
    return vec2(cos(a), sin(a));
}

float windValueNoise(vec2 p) {   // name kept for the call sites; the primitive is gradient noise
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);   // quintic: C2 across cell edges
    float a = dot(windGrad2(i),                   f);
    float b = dot(windGrad2(i + vec2(1.0, 0.0)),  f - vec2(1.0, 0.0));
    float c = dot(windGrad2(i + vec2(0.0, 1.0)),  f - vec2(0.0, 1.0));
    float d = dot(windGrad2(i + vec2(1.0, 1.0)),  f - vec2(1.0, 1.0));
    float g = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);   // ~[-0.71, 0.71]
    return clamp(0.5 + 0.70 * g, 0.0, 1.0);               // -> [0,1], mean 0.5 like before
}

// Local gust factor in [0,1] at a point: broad fronts scrolled downwind, plus a gentle finer
// octave. Multiply by gustAmp and add the steady base to get the local bend strength:
//   bend = (windBase + gustAmp * windGustAt(...)) * masterStrength
//
// ⚑ANISOTROPY IS WHAT MAKES IT READ AS WAVES. Isotropic noise scrolled along a direction gives
// travelling BLOBS — you see lumps pass by, not wind crossing a field. Real gusts ("cat's paws")
// are bands stretched CROSSWIND and narrow along-wind, sweeping downwind. So the sample point is
// rotated into wind-aligned coordinates and the crosswind axis is DIVIDED by windAniso, which
// stretches features across the wind without changing how quickly they pass you.
//
// MEASURED (tools/wind_field_probe.py, which evaluates this exact function on the CPU):
//   isotropic (windAniso 1, gustScale 0.044): along 12u, cross 11u  -> anisotropy 0.92x
//   shipped   (windAniso 5, gustScale 0.018): along 31u, cross 107u -> anisotropy 3.40x
// Front travel measures 20.0u over 3s against 21u expected (corr 1.00) either way — the scrolling
// was always correct; only the SHAPE was wrong.
//
// The fine octave was 2.3x frequency at 0.35 weight, i.e. ~10u structure at a third of the
// amplitude. At that scale it is spatial patchiness rather than gust shape, and no amount of
// temporal filtering removes it (it is not a temporal signal). Now 1.8x at 0.15.
const float kWindFineFreq   = 1.8;
const float kWindFineWeight = 0.15;

// ⚑DOMAIN WARP — what bends straight fronts into WAVES (2026-09-03, user: "wind blowing grass
// ... is a straight line ... I expect it more in a wave shape"). Anisotropy alone stretches
// isotropic noise with a pure affine transform, and iso-contours of smoothly stretched
// single-octave noise are near-straight ruled lines — the anisotropy that made fronts read as
// bands also made every band a ruler. The fix is the industry-standard one: perturb the sample
// position with a second, lower-frequency noise BEFORE evaluating the band noise. The warp is
// applied in wind-aligned coordinates (offset the ALONG-wind coordinate by a noise sampled
// dominantly along the CROSSWIND coordinate), so each front's edge meanders along its own
// length; both coordinates already include the scroll, so meanders travel WITH the field and
// inherit the accepted 2048-wrap phase seam — no new seam class, no new push constants.
//
// MEASURED (tools/wind_field_probe.py — the CPU mirror of this file; keep them in sync):
//   front straightness R (orientation concentration of the gust-band power spectrum,
//   1.0 = perfectly straight parallel bands):  before 0.956  ->  shipped 0.859
//   front travel over 3 s: 8.0u against 8u expected (corr 1.00) both before and after —
//   the warp bends the fronts without breaking their coherence or their travel.
// Units are noise cells (1 cell = 1/gustScale ~ 22u along-wind at the shipped 0.045).
// Amp 1.3 -> 0.7 with the gradient-noise primitive (2026-09-03): the heavy warp was
// compensating for value noise's lattice straightness; on gradient noise it over-shredded
// the field into ~9u ripples (measured: along-wind correlation 9u vs the intended ~15-20u).
const float kWindWarpAmp       = 0.7;   // meander amplitude: edge wanders up to ~±8u along-wind
const float kWindWarpCrossFreq = 1.3;   // meander features every ~85u along the front
const float kWindWarpAlongFreq = 0.35;  // slow along-wind drift: successive fronts differ in shape
// The fine octave gets its OWN lower anisotropy. Inheriting the full windAniso stretched the
// fine detail 5x crosswind too, so it STRIPED ALONG the bands and reinforced the ruled look
// instead of roughening the edges. 2.0 (vs 5.0) measured R 0.859 vs 0.944 with the warp alone —
// the de-striped fine octave does much of the visible edge-breaking.
const float kWindFineAniso     = 2.0;

// ⚑BROAD SWELL LAYER — what gives a gust BODY (2026-09-03, user: even warped, the visible gust
// was "a straight line ... so thin it is almost impossible to see"). The g*g shaping squeezes
// all visible motion into the narrow CREST of each noise swell, so at any moment only a thin
// stripe of the field is bent — a thin stripe of smooth noise reads as a line regardless of how
// wavy its edge is. A third octave ~3x larger than the crest scale, summed IN before the
// shaping, lifts whole regions of the field together: gusts arrive as broad rolling waves with
// the crest/fine detail riding on top, and wide areas are visibly bent at once instead of one
// hairline. Lower anisotropy (2.0) keeps the swells blobby-elongated — at the full windAniso a
// 65u swell would stretch past 300u crosswind and turn back into a ruled band.
const float kWindBroadFreq   = 0.35;  // swell features ~3x the crest scale (~65-95u along-wind)
const float kWindBroadWeight = 0.40;  // weight of the swell (crest 0.45 + fine 0.15 take the rest)
const float kWindBroadAniso  = 2.0;

// `scroll` is the CPU-INTEGRATED field offset (WindSystem::State::scroll), NOT dir*gustSpeed*t.
// Recomputing it from elapsed time multiplies the WANDERING wind direction by uptime, so the whole
// field slews faster the longer the engine runs (0.05 noise cells/s at 10s uptime, 3.2 at 10min).
// No temporal filter can fix that — it is a bulk translation, not high-frequency content.
// Extended form: also returns the local front-meander value in [-0.5, 0.5] — the warp noise
// itself. grass.vert reuses it to rotate the sway heading (grass fans where the front bulges),
// which couples direction variation to edge waviness for free: it is already computed here.
float windGustAtEx(vec2 pHash, vec2 scroll, vec2 dir, float gustScale, float windAniso,
                   out float meander) {
    vec2 q = (pHash - scroll) * gustScale;
    // Rotate into (along-wind, crosswind), stretch crosswind. `dir` is unit length.
    float a = windAniso > 0.01 ? windAniso : 1.0;
    vec2  w = vec2(dot(q, dir), dot(q, vec2(-dir.y, dir.x)) / a);
    // DOMAIN WARP (see the constants above): meander the front line by offsetting the
    // along-wind coordinate with a noise that varies along the crosswind coordinate.
    meander = windValueNoise(vec2(w.y * kWindWarpCrossFreq + 53.1,
                                  w.x * kWindWarpAlongFreq + 91.7)) - 0.5;
    w.x += meander * kWindWarpAmp;
    // Crest octave: rotate back before sampling so the noise lattice stays off the wind axes
    // (lattice-aligned value noise has visible axis bias — straight lines again, by another door).
    vec2 qc = vec2(w.x * dir.x - w.y * dir.y, w.x * dir.y + w.y * dir.x);
    float g = windValueNoise(qc) * 0.45;
    // Fine octave at its OWN lower anisotropy: re-expand the crosswind axis from the coarse
    // stretch (w.y carries cross/a) to cross/kWindFineAniso, rotate back, sample. Inherits the
    // warp through w.x, so the fine detail rides the meandered fronts rather than cutting
    // straight across them.
    vec2 wf = vec2(w.x, w.y * (a / kWindFineAniso));
    vec2 qf = vec2(wf.x * dir.x - wf.y * dir.y, wf.x * dir.y + wf.y * dir.x);
    g += windValueNoise(qf * kWindFineFreq + 17.7) * kWindFineWeight;
    // Broad swell (see kWindBroad* above): summed BEFORE the shaping so wide regions carry the
    // wave's body and the shaping widens the visible gust instead of thinning it to a crest line.
    vec2 wb = vec2(w.x * kWindBroadFreq, w.y * (a / kWindBroadAniso) * kWindBroadFreq);
    vec2 qb = vec2(wb.x * dir.x - wb.y * dir.y, wb.x * dir.y + wb.y * dir.x);
    g += windValueNoise(qb + 7.7) * kWindBroadWeight;
    // SHAPE THE GUST. Raw value noise averages ~0.5, so the gust term carries a large DC
    // component: the field sits at half strength and merely modulates around it. Together with a
    // steady base that meant roughly two thirds of the lean was CONSTANT and only a third moved,
    // which is why gusts never read as gusts. Squaring drops the mean to ~1/3 and sharpens the
    // peaks, so the field rests near zero and arrives in distinct swells. Range stays [0,1].
    return g * g;
}

float windGustAt(vec2 pHash, vec2 scroll, vec2 dir, float gustScale, float windAniso) {
    float meander;
    return windGustAtEx(pHash, scroll, dir, gustScale, windAniso, meander);
}

// Isotropic convenience overload (the field's shape before anisotropy existed).
float windGustAt(vec2 pHash, vec2 scroll, vec2 dir, float gustScale) {
    return windGustAt(pHash, scroll, dir, gustScale, 1.0);
}
