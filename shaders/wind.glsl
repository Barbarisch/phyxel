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

// Local gust factor in [0,1] at a point: slow broad fronts + a finer mid octave, both scrolled
// downwind. Multiply by gustAmp and add the steady base to get the local bend strength:
//   bend = (windBase + gustAmp * windGustAt(...)) * masterStrength
float windGustAt(vec2 pHash, float t, vec2 dir, float gustScale, float gustSpeed) {
    vec2 q = (pHash - dir * (gustSpeed * t)) * gustScale;
    return windValueNoise(q) * 0.65 + windValueNoise(q * 2.3 + 17.7) * 0.35;
}
