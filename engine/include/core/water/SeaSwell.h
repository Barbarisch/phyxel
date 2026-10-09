#pragma once

#include <glm/glm.hpp>

// WaterCore Phase G (docs/WaterCore.md 18): the sea sheet's swell as a CPU function, so the
// shoreline band's ocean boundary holds the SAME surface the sheet draws beside it. A port of the
// four Gerstner components in shaders/water.vert (wind direction + three spread directions;
// wavelengths lambda, 0.61 lambda, 0.33 lambda, 5 lambda; amplitudes a, 0.52 a, 0.28 a, 0.70 a; deep-water
// phase speed sqrt(g / k)). The sheet displaces vertices horizontally too (the trochoid's swing);
// the band prescribes the HEIGHT at a column, so this returns the vertical sum at the undisplaced
// position - the difference is of order steepness x amplitude (a few cm at 0.45 m) and is what the
// seam test measures. The Nyquist fades the sheet applies per LOD level are 1 at band spacing.
namespace Phyxel::Core::Water {

struct SeaSwellParams {
    float amplitude = 0.0f;   ///< m (the sheet's `amplitude`; 0 = flat, the calm-weather control)
    float wavelength = 14.0f; ///< m
    float windRad = 0.0f;     ///< radians, the sheet's wind direction
};

/// Surface height offset (m) above the still level at world (x, z) and sheet time t (s).
float seaSwellHeight(const SeaSwellParams& p, float x, float z, float t);

/// The band's wavemaker sample: the surface offset and the Airy (linear) orbital velocity at the
/// surface for water of depth `depth` (m): per component u = a w cosh(kd)/sinh(kd) cos(theta)
/// along its direction, w = a omega sin(theta) ... with omega from the deep-water dispersion the
/// sheet uses (c = sqrt(g/k)), so surface and velocity belong to the same wave. `k` is the
/// dominant component's wavenumber for the depth profile.
struct SeaSwellSample { float height = 0.0f; glm::vec2 uSurface{0.0f, 0.0f}; float wSurface = 0.0f; float k = 0.0f; };
SeaSwellSample seaSwellSample(const SeaSwellParams& p, float x, float z, float t, float depth);

/// The four components' (direction, amplitude, wavelength) for tests and tools.
struct SeaSwellComponent { glm::vec2 dir; float amplitude; float wavelength; };
void seaSwellComponents(const SeaSwellParams& p, SeaSwellComponent out[4]);

}  // namespace Phyxel::Core::Water
