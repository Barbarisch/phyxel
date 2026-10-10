#pragma once
// docs/WaterCore.md 20 - moving solids: bodies that take up room in an active volume.
//
// A body in water is a pump of exactly the water that no longer fits (20.2): per cell, its volume
// fraction s comes from the exact overlap of the body's box with the cell's box, and the cell must shed
// max(0, f + s - 1) of water over the frame the bodies cover. This file is the first half - the
// raster. It is a pure function of the bodies' world poses and the grid's world-aligned lattice, so
// the same body gives the same fraction in every volume that holds a cell (chunks never enter;
// WaterSolidTest.WorldAlignedRasterAcrossChunkSeam pins it).

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Phyxel::Core::Water {

class WaterGrid;

struct MovingSolid {
    glm::vec3 centre{0.0f};        ///< world units
    glm::vec3 halfExtents{0.0f};   ///< world units; rotation is ignored in v1 - the equal-volume axis-aligned box (volume exact)
    glm::vec3 velocity{0.0f};      ///< m/s (reported only: the rate comes from the fill, 20.2)
    bool fresh = false;            ///< appeared this frame (spawn, teleport): NO rate - a column ledger moves the water instead (20.7: no fake splash)
    uint64_t id = 0;               ///< identity across frames (0 = none): lets WaterCoreManager hold a near-still body's pose (kSolidHoldDistance)
};

struct SolidRaster {
    int bodies = 0;                ///< bodies rasterized
    int refused = 0;               ///< NaN / non-positive size (counted, never rasterized)
    long cells = 0;                ///< cells with s > 0
    double volumeInside = 0.0;     ///< m^3: sum s V after clipping (s <= 1, 0 under static solids, nothing outside the grid)
};

/// Exact box-overlap volume fraction of every cell: s = sum over bodies of overlap / V_cell, clipped to
/// 1 (overlapping bodies), 0 in static-solid cells (a body inside rock displaces no water). `s` and
/// `fresh` are resized to the grid's cell count; fresh[i] = 1 where any fresh body overlaps cell i.
SolidRaster rasterizeSolids(const WaterGrid& g, const std::vector<MovingSolid>& bodies,
                            std::vector<float>& s, std::vector<uint8_t>& fresh);

} // namespace Phyxel::Core::Water
