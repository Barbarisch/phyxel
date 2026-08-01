#include "core/HydrologyMap.h"

#include "core/PriorityFlood.h"

#include <cmath>

namespace Phyxel {

HydrologyMap::HydrologyMap(const HeightFunc& heightAt, float originX, float originZ,
                           int cellsX, int cellsZ, float cellSize, float seaLevel)
    : m_originX(originX), m_originZ(originZ), m_cellSize(cellSize > 0.0f ? cellSize : 1.0f),
      m_seaLevel(seaLevel), m_cellsX(cellsX > 0 ? cellsX : 0), m_cellsZ(cellsZ > 0 ? cellsZ : 0) {
    if (m_cellsX <= 0 || m_cellsZ <= 0 || !heightAt) return;

    // Sample the coarse base height into a grid (cell (i,j) at its world corner).
    std::vector<float> base(static_cast<size_t>(m_cellsX) * m_cellsZ);
    for (int j = 0; j < m_cellsZ; ++j)
        for (int i = 0; i < m_cellsX; ++i) {
            const float h = heightAt(m_originX + i * m_cellSize, m_originZ + j * m_cellSize);
            base[static_cast<size_t>(j) * m_cellsX + i] = h;
            if (h < m_minTerrain) m_minTerrain = h;
        }

    // Depression-fill with the ocean as an outlet: inland basins fill only to their spill.
    std::vector<float> filled = PriorityFlood::fill(base, m_cellsX, m_cellsZ, seaLevel);

    // Resolve the water surface per cell: ocean at sea level; inland lake at the filled level; else dry.
    m_waterLevel.assign(base.size(), NO_WATER);
    for (size_t i = 0; i < base.size(); ++i) {
        if (base[i] <= m_seaLevel)
            m_waterLevel[i] = m_seaLevel;                         // ocean (or sub-sea basin)
        else if (filled[i] > base[i] + 1e-4f)
            m_waterLevel[i] = filled[i];                          // inland lake at its flat spill level
        // else: dry land (NO_WATER)
    }
}

float HydrologyMap::waterLevelAt(float worldX, float worldZ) const {
    if (m_cellsX <= 0 || m_cellsZ <= 0) return NO_WATER;
    // Containing cell (piecewise-constant, so a lake's surface stays exactly flat within a basin;
    // the fine shoreline comes from the per-column terrain-vs-level test, not this coarse grid).
    int i = static_cast<int>(std::floor((worldX - m_originX) / m_cellSize));
    int j = static_cast<int>(std::floor((worldZ - m_originZ) / m_cellSize));
    if (i < 0 || j < 0 || i >= m_cellsX || j >= m_cellsZ) return NO_WATER;  // outside the baked region
    return m_waterLevel[static_cast<size_t>(j) * m_cellsX + i];
}

}  // namespace Phyxel
