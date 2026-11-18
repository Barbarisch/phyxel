#pragma once

#include "core/water/SeaSwell.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// WaterCore §55.5 (W6.2b): THE SWELL DRAWN on the record's sea, so the shore band's moving water meets the sea beside it with no
// step (the owner, 2026-11-18: "that water movement needs to blend into greater body of water"). Appearance only: the record is not
// moved. The drawn height is the band's own function (seaSwellColumn, stokesReturn false) at the same world point and clock, its
// depth limit read from D(x, z) - the sea's still depth box-filtered over the 3 x 3 voxel columns round a point and bilinear between
// voxel-column centres: a pure function of world position and the record, so two record tiles sharing a corner (and the band's ring
// beside them) evaluate the same number. D is quantised (1/32 m, 0..255) - the vertex's spare float carries it - and the band's ring
// uses the same quantised value.
namespace Phyxel::Core::Water {

/// The sea's still depth per voxel column over a window (0 = not sea, or dry).
struct SeaDepthGrid {
    int x0 = 0, z0 = 0, nx = 0, nz = 0;   ///< voxel columns [x0, x0 + nx) x [z0, z0 + nz)
    std::vector<float> d;
    float raw(int X, int Z) const {
        X -= x0; Z -= z0;
        if (X < 0 || Z < 0 || X >= nx || Z >= nz) return 0.0f;
        return d[static_cast<size_t>(X) + static_cast<size_t>(nx) * static_cast<size_t>(Z)];
    }
    /// the 3 x 3 box mean round voxel column (X, Z) - fixed summation order (bit-identical wherever it is evaluated)
    float boxed(int X, int Z) const {
        float s = 0.0f;
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) s += raw(X + dx, Z + dz);
        return s / 9.0f;
    }
    /// D at world (x, z): bilinear between voxel-column centres of the box means
    float at(float x, float z) const {
        const float fx = x - 0.5f, fz = z - 0.5f;
        const int X = static_cast<int>(std::floor(fx)), Z = static_cast<int>(std::floor(fz));
        const float tx = fx - static_cast<float>(X), tz = fz - static_cast<float>(Z);
        const float a = boxed(X, Z) + (boxed(X + 1, Z) - boxed(X, Z)) * tx;
        const float b = boxed(X, Z + 1) + (boxed(X + 1, Z + 1) - boxed(X, Z + 1)) * tx;
        return a + (b - a) * tz;
    }
};

/// The window a point set needs: D at any point of voxel columns [lo, hi] reads raw columns lo - 2 .. hi + 2
constexpr int kSeaDrawHaloVoxels = 2;
constexpr float kSeaDepthQStep = 1.0f / 32.0f;   ///< m; 255 steps = 7.97 m (the 0.45 m swell is whole from ~2.9 m)

inline uint32_t seaDepthQ(float D) {
    if (!(D > 0.0f)) return 0u;
    const long q = std::lround(D / kSeaDepthQStep);
    return static_cast<uint32_t>(std::clamp(q, 0L, 255L));
}
inline float seaDepthFromQ(uint32_t q) { return static_cast<float>(q) * kSeaDepthQStep; }

inline int seaFloorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/// A drawn corner's quantised depth: micro-lattice corner (cx, cz) (cells of 1/per m). 0 at a WATERLINE - any voxel column touching
/// the corner is not sea (its water edge stays at the still level: a crest raised there would stand over a flush shore with no face)
inline uint32_t seaCornerQ(const SeaDepthGrid& g, int cx, int cz, int per) {
    const int X = seaFloorDiv(cx, per), Z = seaFloorDiv(cz, per);
    const int X0 = (cx - X * per == 0) ? X - 1 : X, Z0 = (cz - Z * per == 0) ? Z - 1 : Z;
    for (int zz = Z0; zz <= Z; ++zz) for (int xx = X0; xx <= X; ++xx) if (!(g.raw(xx, zz) > 0.0f)) return 0u;
    const float inv = 1.0f / static_cast<float>(per);
    return seaDepthQ(g.at(static_cast<float>(cx) * inv, static_cast<float>(cz) * inv));
}

/// The draw's knobs (the renderer's; water_sea_draw). Defaults pinned by ShoreReachTest.SeaSwellIsDrawnByDefault.
struct SeaDrawParams {
    bool on = true;
    float reachM = 48.0f;   ///< m from the viewer (horizontal): the record tiles' own reach (WaterLocality::kDrawMDefault) - the skin beyond is flat
    float fadeM = 8.0f;     ///< m: the displacement fades to 0 over [reach - fade, reach]
};

/// the distance fade (GLSL smoothstep's formula)
inline float seaDrawFade(float dist, const SeaDrawParams& p) {
    if (!p.on) return 0.0f;
    const float e0 = p.reachM - p.fadeM, e1 = p.reachM;
    if (e1 <= e0) return dist < e1 ? 1.0f : 0.0f;
    const float t = std::clamp((dist - e0) / (e1 - e0), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

/// The drawn height above the still level at world (x, z), time t, for a corner of quantised depth q (water_surface.vert's
/// seaDrawnOffsetG, rule for rule)
inline float seaDrawnOffset(const SeaSwellParams& p, float x, float z, float t, uint32_t q, float fade) {
    if (q == 0u || fade <= 0.0f || p.amplitude <= 0.0f) return 0.0f;
    return fade * seaSwellColumn(p, x, z, t, seaDepthFromQ(q), false).height;
}

}  // namespace Phyxel::Core::Water
