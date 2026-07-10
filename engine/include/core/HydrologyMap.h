#pragma once

#include <functional>
#include <vector>

namespace Phyxel {

// ── Baked hydrology over a bounded coarse region (docs/TerrainGenerationV2.md P2.2) ───────────
//
// Samples a coarse base-height function into a cellsX×cellsZ grid, runs Priority-Flood with a
// sea-level outlet, and stores the flat WATER-SURFACE level at each cell: the ocean at/below sea
// level, inland lakes above it (each filled to its spill, flat, with a free rim). This is the
// terrain-side hydrology DATA — the water runtime (docs/WaterSystemV2.md) renders/simulates it;
// generation reads it to know where water sits. Built once (a "bounded backing" per §1); pure +
// deterministic.
//
// Design note: the flood runs on the LAYER-0 coarse base height (large-scale basins). Layer-1 relief
// that pokes above a lake's flat level reads as islands/shore — a column is under water only where
// its actual surface Y is below waterLevelAt(x,z). Water level is piecewise-constant per cell (flat
// within a basin by construction of Priority-Flood); the fine shoreline emerges from terrain vs level.
class HydrologyMap {
public:
    // Coarse base elevation (world Y) at a world (x,z). Must be pure/deterministic.
    using HeightFunc = std::function<float(float worldX, float worldZ)>;

    static constexpr float NO_WATER = -1e30f;  // sentinel: this column is dry land

    HydrologyMap() = default;
    // Bakes the region [originX, originX + cellsX*cellSize) × [originZ, originZ + cellsZ*cellSize).
    HydrologyMap(const HeightFunc& heightAt, float originX, float originZ,
                 int cellsX, int cellsZ, float cellSize, float seaLevel);

    // Flat water-surface Y at a world column, or NO_WATER if dry / outside the baked region.
    float waterLevelAt(float worldX, float worldZ) const;
    bool hasWater(float worldX, float worldZ) const { return waterLevelAt(worldX, worldZ) > NO_WATER * 0.5f; }

    int cellsX() const { return m_cellsX; }
    int cellsZ() const { return m_cellsZ; }
    float seaLevel() const { return m_seaLevel; }

private:
    float m_originX = 0.0f, m_originZ = 0.0f, m_cellSize = 1.0f, m_seaLevel = 0.0f;
    int m_cellsX = 0, m_cellsZ = 0;
    std::vector<float> m_waterLevel;  // per cell; NO_WATER = dry
};

}  // namespace Phyxel
