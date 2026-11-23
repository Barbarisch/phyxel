#pragma once
// WaterCore.md 58.8 (item 6) - THE FAR RIPPLE: past a ripple layer's window and its distance fade, the water was glass (the owner:
// "some cheap method to approximate things so that there isnt such drastic changes from ripples to still"). A procedural slope,
// derived from the simulated layer's own roughness, drawn where the simulated one fades - detail fading by screen footprint, never
// motion. water_surface.frag's farRippleSlope() is this function line for line (pinned by WaterFarRippleTest's replica checks).
//
// Bands: the layer's RMS slope measured with central differences at spacing s cells (2, 8, 32 x 1/9 m), one octave each at
// wavelength kFarLambda[k]. Each octave = kFarTrains directional sine trains, fixed directions (incommensurate angles - no shared
// period, no cell borders, so no tiling to read) at deep-water phase speed. Measured PER AXIS: a train (amplitude a, wavenumber k,
// direction d) has x-slope RMS a k |d.x| / sqrt(2); N trains spread round the circle sum to a k sqrt(N / 4) - so a = S sqrt(4 / N) / k
// makes the octave's per-axis slope RMS the band's S (WaterFarRippleTest measures it).
#include <glm/glm.hpp>
#include <cmath>
#include <vector>

namespace Phyxel { namespace Core { namespace Water {

constexpr int kFarBands = 3;
constexpr int kFarTrains = 8;
constexpr int kFarSpacing[kFarBands] = {2, 8, 32};                 ///< cells (x 1/9 m) - the band's difference spacing
constexpr float kFarLambda[kFarBands] = {0.45f, 1.8f, 7.1f};       ///< m - each octave's wavelength (~ 2 x the spacing in metres)
constexpr float kFarAngle0[kFarBands] = {0.37f, 1.91f, 4.27f};     ///< rad - each octave's first direction; the others step 2.399963 (golden angle)
constexpr float kFarPhase[kFarTrains] = {0.0f, 2.1f, 4.4f, 1.3f, 5.7f, 3.2f, 0.9f, 2.6f};  ///< rad - per train
/// per train: its wavelength x this (irrational spread round the octave's) - one shared wavelength realigned the trains every ~54 m
/// (autocorrelation 0.64 there, WaterFarRippleTest); a train's slope share a k is unchanged, so the octave's roughness is too
constexpr float kFarSpread[kFarTrains] = {0.7745967f, 0.8366600f, 0.9055385f, 0.9746794f, 1.0488088f, 1.1180340f, 1.2041595f, 1.2884099f};   // sqrt(0.6, 0.7, ...)
constexpr float kFarPixelsPerWave = 4.0f;                          ///< an octave whose wavelength is under this many pixels draws nothing

/// The octaves' slopes for a layer's heights (row-major nx x nz, pitch 1/9 m), sampled every `stride` cells (cost bound). The bands
/// (the per-axis RMS central-difference slope at each spacing) give the SHAPE of the spectrum; they overlap (a smooth field reads
/// alike at every spacing), so they are scaled so the octaves together carry exactly the layer's per-axis slope RMS at the drawn
/// scale (spacing 1: the shader's own cell slope) - the roughness the eye sees on both sides of the edge. A still layer: 0.
inline glm::vec3 farBandSlopes(const std::vector<float>& h, int nx, int nz, float pitch, int stride = 4) {
    auto rms = [&](int s) {
        double sum = 0.0; long n = 0;
        for (int z = s; z < nz - s; z += stride) for (int x = s; x < nx - s; x += stride) {
            const double dx = (h[static_cast<size_t>(x + s + nx * z)] - h[static_cast<size_t>(x - s + nx * z)]) / (2.0 * s * pitch);
            const double dz = (h[static_cast<size_t>(x + nx * (z + s))] - h[static_cast<size_t>(x + nx * (z - s))]) / (2.0 * s * pitch);
            sum += 0.5 * (dx * dx + dz * dz); ++n;   // per axis
        }
        return n ? std::sqrt(sum / n) : 0.0;
    };
    glm::dvec3 band(rms(kFarSpacing[0]), rms(kFarSpacing[1]), rms(kFarSpacing[2]));
    const double total = rms(1), norm = glm::length(band);
    if (!(norm > 0.0) || !(total > 0.0)) return glm::vec3(0.0f);
    return glm::vec3(band * (total / norm));
}

/// The far ripple's slope (d/dx, d/dz) at world xz and time t, the bands' slopes S, `pixelM` = the world size of a pixel here.
inline glm::vec2 farRippleSlope(glm::vec2 xz, float t, glm::vec3 S, float pixelM) {
    glm::vec2 slope(0.0f);
    for (int b = 0; b < kFarBands; ++b) {
        if (S[b] <= 0.0f) continue;
        const float lambda = kFarLambda[b];
        const float vis = glm::clamp(lambda / (kFarPixelsPerWave * std::max(pixelM, 1e-6f)) - 1.0f, 0.0f, 1.0f);   // detail fade
        if (vis <= 0.0f) continue;
        for (int i = 0; i < kFarTrains; ++i) {
            const float k = 6.2831853f / (lambda * kFarSpread[i]), w = std::sqrt(9.81f * k);
            const float a = S[b] * std::sqrt(4.0f / kFarTrains) / k;
            const float ang = kFarAngle0[b] + 2.399963f * static_cast<float>(i);
            const glm::vec2 d(std::cos(ang), std::sin(ang));
            const float ph = k * glm::dot(d, xz) - w * t + kFarPhase[i];
            slope += vis * a * k * std::cos(ph) * d;
        }
    }
    return slope;
}

}}}  // namespace Phyxel::Core::Water
