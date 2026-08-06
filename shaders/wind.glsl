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

float windHash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

// Bilinear value noise in [0,1], C1-smooth (Hermite-interpolated lattice).
float windValueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = windHash21(i);
    float b = windHash21(i + vec2(1.0, 0.0));
    float c = windHash21(i + vec2(0.0, 1.0));
    float d = windHash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
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

// `scroll` is the CPU-INTEGRATED field offset (WindSystem::State::scroll), NOT dir*gustSpeed*t.
// Recomputing it from elapsed time multiplies the WANDERING wind direction by uptime, so the whole
// field slews faster the longer the engine runs (0.05 noise cells/s at 10s uptime, 3.2 at 10min).
// No temporal filter can fix that — it is a bulk translation, not high-frequency content.
float windGustAt(vec2 pHash, vec2 scroll, vec2 dir, float gustScale, float windAniso) {
    vec2 q = (pHash - scroll) * gustScale;
    // Rotate into (along-wind, crosswind), stretch crosswind, rotate back. `dir` is unit length.
    float a = windAniso > 0.01 ? windAniso : 1.0;
    vec2  w = vec2(dot(q, dir), dot(q, vec2(-dir.y, dir.x)) / a);
    q = vec2(w.x * dir.x - w.y * dir.y, w.x * dir.y + w.y * dir.x);
    float g = windValueNoise(q) * (1.0 - kWindFineWeight)
            + windValueNoise(q * kWindFineFreq + 17.7) * kWindFineWeight;
    // SHAPE THE GUST. Raw value noise averages ~0.5, so the gust term carries a large DC
    // component: the field sits at half strength and merely modulates around it. Together with a
    // steady base that meant roughly two thirds of the lean was CONSTANT and only a third moved,
    // which is why gusts never read as gusts. Squaring drops the mean to ~1/3 and sharpens the
    // peaks, so the field rests near zero and arrives in distinct swells. Range stays [0,1].
    return g * g;
}

// Isotropic convenience overload (the field's shape before anisotropy existed).
float windGustAt(vec2 pHash, vec2 scroll, vec2 dir, float gustScale) {
    return windGustAt(pHash, scroll, dir, gustScale, 1.0);
}
