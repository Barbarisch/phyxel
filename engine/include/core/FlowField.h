#pragma once

#include <functional>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {

// ── Flow accumulation over a bounded coarse region (docs/TerrainGenerationV2.md P2.3) ─────────
//
// Depression-fills a coarse height field WITH flow directions (Priority-Flood+FlowDirs), then
// accumulates the number of upstream cells draining through each cell down the drainage network.
// accum[c] = 1 + (all cells whose flow reaches c) — so large values trace out valleys and river
// channels, and the mouth of a basin carries its whole catchment. This is the terrain-side river
// signal; a later step thresholds accum → river cells and assigns Strahler order → width/depth.
// Pure/deterministic; a bounded backing built once and read locally.
class FlowField {
public:
    using HeightFunc = std::function<float(float worldX, float worldZ)>;

    // Channel-initiation threshold: a cell is a river once its upstream drainage exceeds this many
    // cells. Grounded default ≈ 0.1 km² (98 cells at 32 m/cell) — the lower-mid of Montgomery &
    // Dietrich (1992)'s perennial-stream range (49–977 cells), a humid-temperate analog. A DESIGN
    // knob (drainage density is climate-dependent); also note the pure cell-count test is a
    // simplification of the real area×slope² criterion (docs/TerrainGenerationV2.md §P2).
    static constexpr int kDefaultRiverThresholdCells = 98;

    FlowField() = default;
    FlowField(const HeightFunc& heightAt, float originX, float originZ,
              int cellsX, int cellsZ, float cellSize, float seaLevel,
              int riverThreshold = kDefaultRiverThresholdCells);

    // Upstream drainage count at a world column (>= 1 inside the baked region, 0 outside).
    // Piecewise-constant per cell.
    int accumAt(float worldX, float worldZ) const;

    // Strahler stream order at a world column: 0 = not a river, 1 = headwater, +1 where two equal
    // orders meet. Drives channel width/depth. 0 outside the region.
    int orderAt(float worldX, float worldZ) const;
    int maxOrder() const { return m_maxOrder; }

    // Strahler order over a drainage graph, exposed for direct testing. downstream[c] = the cell c
    // drains into (self = sink); accum[c] = upstream cell count; a cell is a river iff accum>threshold.
    // Returns order[] (0 for non-river). Cells must be processable upstream-first by ascending accum.
    static std::vector<int> computeStrahler(const std::vector<int>& downstream,
                                            const std::vector<int>& accum, int threshold);

    // Grounded channel geometry by Strahler order (docs/TerrainGenerationV2.md §P2; Doll et al.
    // NC Coastal-Plain hydraulic geometry). Width 2/3/5/8/14/22 voxels → half-width below; carve
    // depth 0/0/1/1/1/2 voxels — orders 1–2 are SUB-VOXEL so they do NOT carve a bed (surface-only).
    static float channelHalfWidth(int order);
    static float channelDepth(int order);

    // River carve at a world column: rivers are coarse cells, so each is modeled as a segment from its
    // centre to its downstream river cell's centre; a column carves if within half-width of the nearest
    // such segment, with a parabolic bed (deepest at the centreline). Returns the deepest (max-order)
    // hit. hit=false where no order≥3 channel is within range.
    struct ChannelHit {
        bool hit = false;
        int order = 0;
        float depth = 0.0f;   // parabolic carve depth at this column (voxels)
    };
    ChannelHit channelAt(float worldX, float worldZ) const;

    // DOWNHILL FLOW DIRECTION at a world column (WaterSystemV3 Phase 3): the normalized world-XZ
    // direction from this cell's centre toward the cell it drains into. Returns (0,0) at a sink,
    // outside the region, or where the drainage target is the cell itself. This is the water's
    // travel direction along the drainage network — the CA cannot derive it for a river, because a
    // baked river is pinned full end-to-end and therefore performs no transfers (see
    // WaterManager::setRiverQuery), so the direction has to come from the bake.
    glm::vec2 flowDirAt(float worldX, float worldZ) const;

    // Nearest order≥3 channel to a world column, for VALLEY shaping (wider than the channel itself):
    // the distance (world units) to the closest channel centreline segment and that channel's order,
    // scanning cells within `searchRadius`. dist is huge and order 0 if none is in range. The
    // generator attenuates Layer-1 relief toward the centreline so rivers seat in a smooth valley
    // floor, not a thin slot buried by relief roughness. (docs/TerrainGenerationV2.md §P2)
    struct NearestChannel {
        float dist = 1e30f;
        int   order = 0;
    };
    // minOrder gates which channels count. Default 3 preserves the valley-shaping caller:
    // orders 1-2 are sub-voxel and cut no valley. Pass 1 to find CREEKS - they carry water even
    // though they carve nothing, and this was one of FOUR places that silently excluded them
    // (the others: channelAt's order>=3 test, WaterManager's 0.5 seeding threshold, and
    // WorldGenerator setting riverOrder only from channelAt).
    NearestChannel nearestChannel(float worldX, float worldZ, float searchRadius,
                                  int minOrder = 3) const;

    // Per-segment channel test (the geometry, exposed for direct testing): a point p carves iff
    // order>=3 (orders 1-2 sub-voxel → no bed) AND p is within half-width of segment a→b, with a
    // parabolic bed (full depth at the centreline, 0 at the edge). channelAt applies this per river cell.
    static ChannelHit segmentChannel(float px, float pz, float ax, float az, float bx, float bz, int order);

    int maxAccum() const { return m_maxAccum; }
    int cellsX() const { return m_cellsX; }
    int cellsZ() const { return m_cellsZ; }
    // Baked region geometry (world units), so callers/tests can derive cell centres without
    // mirroring the generator's region constants.
    float originX() const { return m_originX; }
    float originZ() const { return m_originZ; }
    float cellSize() const { return m_cellSize; }
    // True iff the accumulation topo-pass released every cell (no cycle in the drainage graph). A
    // cycle would silently under-count upstream area, so this is a guard against future changes to
    // the steepest-descent/flat-fallback direction logic.
    bool drainageComplete() const { return m_released == static_cast<size_t>(m_cellsX) * m_cellsZ; }

private:
    float m_originX = 0.0f, m_originZ = 0.0f, m_cellSize = 1.0f;
    int m_cellsX = 0, m_cellsZ = 0;
    std::vector<int> m_accum;   // per cell; upstream drainage count
    std::vector<int> m_order;   // per cell; Strahler order (0 = not a river)
    std::vector<int> m_downstream;  // per cell; steepest-descent drainage target (self = sink)
    int m_maxAccum = 0;
    int m_maxOrder = 0;
    size_t m_released = 0;      // cells processed by the Kahn pass (== cell count iff acyclic)
};

}  // namespace Phyxel
