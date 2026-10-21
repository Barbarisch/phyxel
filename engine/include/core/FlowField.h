#pragma once

#include <cstdint>
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

    // M2b (docs/WorldForgeV2Plan.md M2 engineering design, D15): upstream drainage that enters the
    // baked box from OUTSIDE it — the macro sheet's drainage area at the border cell nearest the
    // macro river's crossing, in units of this bake's cells. Added to the cell's own count before
    // accumulation, so it rides downstream like any upstream cell. The border row itself is an
    // outlet (drains out), so the generator injects at the first INTERIOR cell.
    struct Inflow {
        int cell = -1;        // row-major cell index
        int extraCells = 0;   // upstream cells to add (macro area / cellSize²)
    };

    FlowField() = default;
    FlowField(const HeightFunc& heightAt, float originX, float originZ,
              int cellsX, int cellsZ, float cellSize, float seaLevel,
              int riverThreshold = kDefaultRiverThresholdCells,
              const std::vector<Inflow>& inflows = {},
              bool areaLaw = false);
    const std::vector<Inflow>& inflows() const { return m_inflows; }

    // ── M2b-2: channel geometry from DRAINAGE AREA (docs/WorldForgeV2Plan.md §10 F4f) ──────────
    // The grounded law, read from the primary pages 2026-10-20: Doll 2004 (NC Coastal Plain,
    // W = 10.97·DA^0.36 ft, D = 1.29·DA^0.30 ft, DA in mi²; valid 0.57–417 km²) converted to
    // W = 2.37·A^0.36 m, D = 0.296·A^0.30 m with A in km²; extrapolated past 417 km² under
    // Leopold & Maddock 1953's downstream b = 0.5 / f = 0.4 (which reproduce Doll's 0.36 through
    // Q ∝ A^0.72). areaLaw = true (v2 recipes) serves width/depth from this law per cell; false
    // (v1, D10) keeps the per-order table below. A channel is a CARVED RIVER when its centreline
    // depth reaches kRiverCarveDepth (the water runtime pins full exactly where the carve is
    // >= 0.5 — one threshold on both sides); shallower channels are CREEKS (the 1/3 u recess +
    // fractional ribbon). Under the table the split is the historical order >= 3.
    static constexpr float kLawWidthCoeffU = 2.37f, kLawWidthExp = 0.36f;
    static constexpr float kLawDepthCoeffU = 0.296f, kLawDepthExp = 0.30f;
    static constexpr float kRiverCarveDepth = 0.5f;
    static float lawHalfWidth(float areaKm2);
    static float lawDepth(float areaKm2);
    bool  areaLaw() const { return m_areaLaw; }
    float maxHalfWidth() const { return m_maxHalfW; }            // over every river cell
    float maxCreekHalfWidth() const { return m_maxCreekHalfW; }  // over every non-carved river cell
    // Per-CELL geometry (piecewise-constant like orderAt): half-width / centreline depth / class.
    float halfWidthAt(float worldX, float worldZ) const;
    float fullDepthAt(float worldX, float worldZ) const;
    bool  carvedRiverAt(float worldX, float worldZ) const;

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
    // NC Coastal-Plain hydraulic geometry). Width 2/3/5/8/14/22 voxels → half-width below; depth
    // 0.33/0.66/1/1/1/2 voxels. Orders 1–2 are CREEKS: sub-voxel WATER depths for the runtime's
    // fractional ribbon pin — they carve no TERRAIN (the generator clamps the surface carve to
    // order ≥ 3) and shape no valley (nearestChannel skips them).
    static float channelHalfWidth(int order);
    static float channelDepth(int order);

    // Channel at a world column: rivers are coarse cells, so each is modeled as a segment from its
    // centre to its downstream river cell's centre; a column is on the channel if within half-width
    // of the nearest such segment, with a parabolic depth profile (deepest at the centreline).
    // Returns the deepest (max-order) hit; hit=false off the network. Orders 1-2 report fractional
    // sub-voxel depths (creek water); consumers that CARVE TERRAIN must gate on order ≥ 3.
    struct ChannelHit {
        bool hit = false;
        int order = 0;
        float depth = 0.0f;       // parabolic carve depth at this column (voxels)
        float halfWidth = 0.0f;   // M2b-2: the hit segment's half-width (u)
        float fullDepth = 0.0f;   // M2b-2: the hit segment's centreline depth (u)
        bool  carved = false;     // M2b-2: carved river (true) or creek (false) — see kRiverCarveDepth
        // M2d-1: the LONGITUDINAL bed profile (areaLaw bakes only) — the channel's bed height at
        // the hit's projection on the segment, non-increasing downstream by construction; valid
        // when hasProfile. inBasin: the segment's own cell lies in a coarse basin (filled > base),
        // where the bed is the basin floor and no carve/fill applies.
        float bedProfile = 0.0f;
        bool  hasProfile = false;
        bool  inBasin = false;
    };
    ChannelHit channelAt(float worldX, float worldZ) const;

    // DOWNHILL FLOW DIRECTION at a world column (WaterSystemV3 Phase 3): the normalized world-XZ
    // direction from this cell's centre toward the cell it drains into. Returns (0,0) at a sink,
    // outside the region, or where the drainage target is the cell itself. This is the water's
    // travel direction along the drainage network — the CA cannot derive it for a river, because a
    // baked river is pinned full end-to-end and therefore performs no transfers (see
    // WaterManager::setRiverQuery), so the direction has to come from the bake.
    glm::vec2 flowDirAt(float worldX, float worldZ) const;

    // Nearest channel to a world column, for VALLEY/SWALE shaping (wider than the channel itself):
    // the distance (world units) to the closest channel centreline segment and that channel's order,
    // scanning cells within `searchRadius`. dist is huge and order 0 if none is in range. The
    // generator attenuates Layer-1 relief toward the centreline so rivers seat in a smooth valley
    // floor, not a thin slot buried by relief roughness. (docs/TerrainGenerationV2.md §P2)
    // `minOrder` (default 3) keeps the historical big-river behavior; the creek swale pass queries
    // with minOrder=1 so orders 1-2 shape a NARROW dip (water-as-terrain-stage P2 — a creek is
    // shaped by the terrain around it, not painted on top of it).
    struct NearestChannel {
        float dist = 1e30f;
        int   order = 0;
        float halfWidth = 0.0f;   // M2b-2: that channel's half-width (u)
        bool  carved = false;     // M2b-2: carved river or creek
    };
    NearestChannel nearestChannel(float worldX, float worldZ, float searchRadius, int minOrder = 3) const;

    // Per-segment channel test (the geometry, exposed for direct testing): a point p is on the
    // channel iff order>=1 AND p is within half-width of segment a→b, with a parabolic depth
    // (full channelDepth at the centreline, 0 at the edge). channelAt applies this per river cell.
    static ChannelHit segmentChannel(float px, float pz, float ax, float az, float bx, float bz, int order);
    // M2b-2: the same test with explicit geometry (the per-cell law or table values).
    static ChannelHit segmentChannel(float px, float pz, float ax, float az, float bx, float bz, int order,
                                     float halfWidth, float fullDepth, bool carved);

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
    std::vector<Inflow> m_inflows;  // M2b: the macro inflows this bake was given (echoed for tests/API)
    std::vector<float> m_halfW;     // M2b-2: per cell half-width (0 off the network)
    std::vector<float> m_fullD;     // M2b-2: per cell centreline depth (0 off the network)
    std::vector<uint8_t> m_carved;  // M2b-2: per cell class (1 = carved river)
    bool  m_areaLaw = false;
    float m_maxHalfW = 0.0f, m_maxCreekHalfW = 0.0f;
    // M2d-1 (areaLaw only): per river cell, the bed height at the cell centre along a profile that
    // never rises downstream — min over the upstream chain of (sampled height − law depth); a
    // coarse basin cell carries its FILLED level (the spill) downstream instead of its floor and is
    // itself left untouched (the basin shape is the basin shape, D17). m_carryY is what a cell
    // hands downstream; m_basin marks the coarse basin cells.
    std::vector<float>   m_bedY, m_carryY;
    std::vector<uint8_t> m_basin;
    int m_maxAccum = 0;
    int m_maxOrder = 0;
    size_t m_released = 0;      // cells processed by the Kahn pass (== cell count iff acyclic)
};

}  // namespace Phyxel
