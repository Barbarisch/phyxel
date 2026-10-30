#pragma once
// docs/WaterCore.md 45 (A12) - water at rest looks alive: the REST MOTION, an appearance layer over every pond surface (awake and
// recorded alike - the pond surface draws both, 27.11). It moves no water and changes no record. A breeze's cat's-paws: three
// travelling capillary-gravity components along the wind, under a patchy envelope that drifts with the wind and comes and goes.
//
// FACETED on the world's 1/9 m lattice (the ripple layer's own look, 22): the slope is constant over each 1/9 m square - the
// height at the square centres, central differences, exactly RippleLayer's facet mode in water_surface.frag. A pure function of
// the WORLD square index and time: no chunk, no volume box, no layer origin (45.1 item 2).
//
// This header is the C++ MIRROR of the shader's `restSlope` (water_surface.frag) - the same formula, line for line, so the tests
// can hold the screen to it (WaterRestMotionTest, and the on-screen tap, debug 10). Change both together.

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace Phyxel::Core::Water {

struct WaterRestMotion {
    static constexpr float kPitch = 1.0f / 9.0f;           ///< m - the facet: the microcube
    static constexpr float kAmpMmDefault = 0.0f;           ///< off in the first build (45.1: the look is the owner's - the clip first)
    static constexpr float kAmpMmMax = 5.0f;               ///< above ~5 mm a 1/9 m facet tilts past ~0.1 and reads as chop
    static constexpr float kWindDegDefault = 30.0f;        ///< the direction the wind blows TOWARD, degrees from +x (toward +z at 90)
    static constexpr float kWindMpsDefault = 2.5f;         ///< a light breeze (Beaufort 2)
    static constexpr float kWindMpsMax = 10.0f;
    static constexpr float kWindFullMps = 2.0f;            ///< m/s: the amplitude reaches its value at this wind (calm = a mirror)
    static constexpr float kPatchMDefault = 6.0f, kPatchMMin = 1.0f, kPatchMMax = 30.0f;   ///< the cat's-paw size
    static constexpr float kEvolveS = 8.0f;                ///< s - how long a patch takes to come or go
    static constexpr float kPatchDrift = 0.3f;             ///< the patches drift at this fraction of the wind speed
    /// The envelope's floor: between the patches the water keeps this share of the motion. Without it a 4 x 4 m pond inside one
    /// glassy 6 m patch was dead still for seconds (the first live tap, 45.2) - the owner's complaint, back.
    static constexpr float kEnvelopeFloor = 0.3f;
    static constexpr int kComponents = 3;

    struct Params {
        float ampM = kAmpMmDefault * 1e-3f;                ///< m, the tallest crest (0 = off)
        float windDeg = kWindDegDefault, windMps = kWindMpsDefault, patchM = kPatchMDefault;
    };

    // the three components: angle off the wind (rad), wavelength (m), share of the amplitude
    static constexpr float kAngle[kComponents] = {0.0f, 0.5f, -0.4f};
    static constexpr float kLambda[kComponents] = {0.85f, 0.5f, 0.3f};
    static constexpr float kShare[kComponents] = {0.55f, 0.3f, 0.15f};
    static constexpr float kOffset[kComponents] = {0.0f, 1.7f, 4.1f};

    /// Deep-water capillary-gravity phase speed (m/s) of wavelength l (m): c^2 = g l / 2 pi + 2 pi sigma / (rho l)
    static float phaseSpeed(float l) {
        const float g = 9.81f, sigma = 0.0728f, rho = 1000.0f, tau = 6.2831853f;
        return std::sqrt(g * l / tau + tau * sigma / (rho * l));
    }

    /// Integer hash of a lattice point (the shader's `restHash`): uint32 arithmetic, wraps identically on both sides.
    static uint32_t hash(int x, int y, int z) {
        uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ static_cast<uint32_t>(z) * 83492791u;
        h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
        return h;
    }
    static float rnd(int x, int y, int z) { return static_cast<float>(hash(x, y, z) & 0xFFFFFFu) / 16777216.0f; }
    static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
    /// Value noise in 3-D (trilinear over the lattice's corners, smoothstep weights), 0..1.
    static float noise3(float x, float y, float z) {
        const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
        const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
        const float tx = smooth(x - fx), ty = smooth(y - fy), tz = smooth(z - fz);
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        const float c00 = lerp(rnd(ix, iy, iz), rnd(ix + 1, iy, iz), tx), c10 = lerp(rnd(ix, iy + 1, iz), rnd(ix + 1, iy + 1, iz), tx);
        const float c01 = lerp(rnd(ix, iy, iz + 1), rnd(ix + 1, iy, iz + 1), tx), c11 = lerp(rnd(ix, iy + 1, iz + 1), rnd(ix + 1, iy + 1, iz + 1), tx);
        return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
    }
    static float smoothstep(float e0, float e1, float x) { const float t = std::fmin(std::fmax((x - e0) / (e1 - e0), 0.0f), 1.0f); return smooth(t); }

    /// The patch envelope at world xz (m), time t (s): 0 (glassy) .. 1 (a full cat's-paw).
    static float envelope(const Params& p, float x, float z, float t) {
        const float a = p.windDeg * 0.017453293f;
        const float drift = kPatchDrift * p.windMps * t;
        const float u = (x - std::cos(a) * drift) / p.patchM, v = (z - std::sin(a) * drift) / p.patchM;
        return kEnvelopeFloor + (1.0f - kEnvelopeFloor) * smoothstep(0.35f, 0.75f, noise3(u, v, t / kEvolveS));
    }

    /// The rest height (m) at world xz, time t.
    static float height(const Params& p, float x, float z, float t) {
        if (p.ampM <= 0.0f) return 0.0f;
        const float calm = std::fmin(p.windMps / kWindFullMps, 1.0f);
        if (calm <= 0.0f) return 0.0f;
        const float a = p.windDeg * 0.017453293f;
        float h = 0.0f;
        for (int k = 0; k < kComponents; ++k) {
            const float dx = std::cos(a + kAngle[k]), dz = std::sin(a + kAngle[k]);
            const float kw = 6.2831853f / kLambda[k];
            h += kShare[k] * std::sin(kw * (x * dx + z * dz - phaseSpeed(kLambda[k]) * t) + kOffset[k]);
        }
        return p.ampM * calm * envelope(p, x, z, t) * h;
    }

    /// The facet slope (dh/dx, dh/dz) of the 1/9 m square (ix, iz) (world lattice: square i spans [i/9, (i+1)/9)), at time t.
    static glm::vec2 slope(const Params& p, int ix, int iz, float t) {
        const float cx = (static_cast<float>(ix) + 0.5f) * kPitch, cz = (static_cast<float>(iz) + 0.5f) * kPitch;
        const float sx = (height(p, cx + kPitch, cz, t) - height(p, cx - kPitch, cz, t)) * (0.5f / kPitch);
        const float sz = (height(p, cx, cz + kPitch, t) - height(p, cx, cz - kPitch, t)) * (0.5f / kPitch);
        return glm::vec2(sx, sz);
    }
    /// The square a world point lies in.
    static glm::ivec2 square(float x, float z) { return glm::ivec2(static_cast<int>(std::floor(x * 9.0f)), static_cast<int>(std::floor(z * 9.0f))); }   // x 9, as the shader
};

}  // namespace Phyxel::Core::Water
