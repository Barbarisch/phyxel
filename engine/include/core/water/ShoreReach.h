#pragma once
// WaterCore §55.4 (W6.2a): the shore band near the player - the pure part. Where the band goes: the nearest SHORE point to the
// viewer, a SEA column of the record (its body is the ocean) with a dry column within `innerM` of it, sampled on a world-aligned
// lattice - a pure function of world position and the record, not of chunks (ShoreReachTest: the record read whole or chunk by
// chunk and shuffled gives the same point). The follow trigger's search, as nearestRiverPoint is the river's (RiverReach.h).
#include "core/water/RiverReach.h"
#include <functional>

namespace Phyxel { namespace Core { namespace Water {

/// Whether the record's water at world column (wx, wz) with surface `top` (world m) belongs to the sea (the manager: bodyAt ->
/// the body table's class "ocean"; a test: its own rule).
using RecordSeaFn = std::function<bool(int wx, int wz, float top)>;

struct ShorePoint {
    bool found = false; int wx = 0, wz = 0; float top = 0.0f, bottom = 0.0f; float distM = 0.0f;
    glm::vec2 landward{0.0f};   ///< unit: the mean direction from the point to the dry columns within innerM (where the swash runs)
};

/// The nearest shore column to the viewer within `radiusM` (horizontal), on the `latticeM` world lattice (columns wx, wz multiples
/// of it). A shore column: the record holds SEA water there, and some world column within `innerM` (Chebyshev, whole metres) holds
/// no water. Ties: the lower z, then x (lattice order).
inline ShorePoint nearestShorePoint(const glm::vec2& viewer, float radiusM, const RecordColumnFn& column, const RecordSeaFn& isSea,
                                    int innerM = 4, int latticeM = 2) {
    ShorePoint best;
    if (!column || !isSea || radiusM <= 0.0f || latticeM < 1 || innerM < 1) return best;
    const int L = latticeM;
    const int i0 = riverFloorDiv(static_cast<int>(std::floor(viewer.x - radiusM)), L), i1 = riverFloorDiv(static_cast<int>(std::ceil(viewer.x + radiusM)), L) + 1;
    const int j0 = riverFloorDiv(static_cast<int>(std::floor(viewer.y - radiusM)), L), j1 = riverFloorDiv(static_cast<int>(std::ceil(viewer.y + radiusM)), L) + 1;
    float bestD2 = radiusM * radiusM;
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
        const int wx = i * L, wz = j * L;
        const float dx = static_cast<float>(wx) + 0.5f - viewer.x, dz = static_cast<float>(wz) + 0.5f - viewer.y;
        const float d2 = dx * dx + dz * dz;
        if (d2 > bestD2 || (best.found && d2 == bestD2)) continue;   // lattice order: an equal one later loses the tie
        float top = 0.0f, bottom = 0.0f;
        if (!column(wx, wz, top, bottom) || top <= bottom || !isSea(wx, wz, top)) continue;
        glm::vec2 land(0.0f); int dry = 0;   // the columns within innerM holding no water: the beach (a lake's water is not dry)
        for (int oz = -innerM; oz <= innerM; ++oz) for (int ox = -innerM; ox <= innerM; ++ox) {
            float t2, b2;
            if (!column(wx + ox, wz + oz, t2, b2) || t2 <= b2) { land += glm::vec2(static_cast<float>(ox), static_cast<float>(oz)); ++dry; }
        }
        if (dry == 0) continue;
        best.found = true; best.wx = wx; best.wz = wz; best.top = top; best.bottom = bottom; bestD2 = d2;
        best.landward = glm::length(land) > 1e-6f ? glm::normalize(land) : glm::vec2(0.0f);
    }
    if (best.found) best.distM = std::sqrt(bestD2);
    return best;
}

}}} // namespace Phyxel::Core::Water
