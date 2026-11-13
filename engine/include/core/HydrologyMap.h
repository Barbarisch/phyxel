#pragma once

#include <functional>
#include <vector>

namespace Phyxel {

class FlowField;

// ── Baked hydrology over a bounded coarse region (docs/TerrainGenerationV2.md P2.2) ───────────
//
// Samples a coarse base-height function into a cellsX×cellsZ grid, runs Priority-Flood with a
// sea-level outlet, and stores the flat WATER-SURFACE level at each cell: the ocean at/below sea
// level, inland lakes above it (each filled to its spill, flat, with a free rim). This is the
// terrain-side hydrology DATA — the water runtime (docs/Water.md) renders/simulates it;
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
    // WaterCore Phase D4 (WaterRethink 8.8 1b): the Priority-Flood depression-filled elevation of
    // the cell, never below sea level (a sub-sea cell is sea-connected). A column may join a lake
    // flood only where this is >= the lake's level: downstream of the outlet it is lower (the
    // water drains there by Priority-Flood's own definition), so a perched lake can no longer
    // drown the valley below it. NO_WATER outside the baked region.
    float filledAt(float worldX, float worldZ) const;
    bool hasWater(float worldX, float worldZ) const { return waterLevelAt(worldX, worldZ) > NO_WATER * 0.5f; }

    int cellsX() const { return m_cellsX; }
    int cellsZ() const { return m_cellsZ; }
    float seaLevel() const { return m_seaLevel; }
    // Grid geometry + raw levels (row-major, index = z*cellsX + x; NO_WATER = dry) — the WATER
    // LAYER export surface: the renderer uploads this grid as a texture so every basin draws at
    // its own level out to the horizon (water-layer P1). Immutable after the bake.
    float originX()  const { return m_originX; }
    float originZ()  const { return m_originZ; }
    float cellSize() const { return m_cellSize; }
    const std::vector<float>& levels() const { return m_waterLevel; }

    // Lowest sampled terrain over the baked grid. If it sits ABOVE sea level the Priority-Flood
    // had no ocean outlet anywhere — the whole region is one closed basin filling to its spill,
    // which puts lakes on hillsides. That is a world/sea-level CONFIGURATION problem, not a bake
    // defect; expose the fact so callers can warn loudly instead of someone diagnosing the
    // symptom downstream (docs/Water.md §2e).
    float minTerrain() const { return m_minTerrain; }
    bool hasOutlet() const { return m_minTerrain <= m_seaLevel; }

    // WaterCore.md 51.12 (the D4 gap of 2026-10-09): A CARVED CHANNEL DRAINS THE BASIN IT CROSSES. The flood above runs on the
    // coarse height; the generator then carves order >= 3 rivers (valley shaping + the channel) through it, cutting an outlet
    // the coarse rim never had - RiverCarve's lake stood at 107.8 beside a carved river whose ground is 26 (an 82 m wall). For
    // each inland body (4-connected wet cells at one level above sea) with an order >= 3 cell, the D8 chain from that cell is
    // followed to the body's last cell (the exit cell); `exitSurface(exitCell)` gives the carved river's water surface there.
    // A body with no channel cell of its own but one among its cells' 8 neighbours takes the lowest `exitSurface` of those
    // (the valley cuts the rim beside it). NO_WATER = not found: the body is left as it is and counted unresolved. A lower surface becomes the body's level:
    // each of its cells stays wet at it where the coarse height is below it, else goes dry; filled follows. Only ever lowers.
    // Pure + deterministic (one pass in index order over the immutable flow field). Must run before WaterBodyIndex is built.
    using CellSurfaceFunc = std::function<float(int cell)>;
    void drainCarvedBasins(const FlowField& flow, const HeightFunc& heightAt, const CellSurfaceFunc& exitSurface);
    int basinsDrained() const { return m_basinsDrained; }
    int basinsUnresolved() const { return m_basinsUnresolved; }

private:
    float m_originX = 0.0f, m_originZ = 0.0f, m_cellSize = 1.0f, m_seaLevel = 0.0f;
    float m_minTerrain = 1e30f;       // lowest sampled terrain (1e30 = nothing baked)
    int m_cellsX = 0, m_cellsZ = 0;
    int m_basinsDrained = 0, m_basinsUnresolved = 0;   // 51.12
    std::vector<float> m_waterLevel;  // per cell; NO_WATER = dry
    std::vector<float> m_filled;      // per cell; max(depression-filled elevation, sea level)
};

}  // namespace Phyxel
