#pragma once

#include <vector>

namespace Phyxel {

class HydrologyMap;
class WaterBodyIndex;

// ── M2e-2: the coast field (docs/WorldForgeV2Plan.md M2e; D17: shapes, no water) ───────────────
//
// The signed distance from the shoreline, baked once over the hydrology grid: the OCEAN bodies'
// cells (WaterBodyIndex class Ocean) are the sea; every other cell is land. A land cell holds the
// chamfer distance (world units, 8-neighbour 1/√2 weights) to the nearest sea cell centre, a sea
// cell minus the distance to the nearest land cell centre, and the bilinear read between cell
// centres crosses zero at the shoreline. Pure, immutable, shared by every generator copy — a
// column's coast shaping is a function of world position alone.
class CoastField {
public:
    CoastField() = default;
    CoastField(const HydrologyMap& hydro, const WaterBodyIndex& bodies);

    bool  hasOcean() const { return m_seaCells > 0; }
    int   seaCells() const { return m_seaCells; }
    // > 0 on land (distance to the sea), < 0 at sea (distance to land); kFar when there is no
    // ocean in the box or the column is outside it.
    float signedDistAt(float worldX, float worldZ) const;
    static constexpr float kFar = 1e30f;

private:
    float m_originX = 0.0f, m_originZ = 0.0f, m_cellSize = 1.0f;
    int   m_cellsX = 0, m_cellsZ = 0, m_seaCells = 0;
    std::vector<float> m_dist;   // signed, per cell centre
};

}  // namespace Phyxel
