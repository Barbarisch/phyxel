#pragma once

#include <algorithm>
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
    // M2d-4 (D36 / D37): the generator's two world-space callbacks for a version-2 bake —
    // `toWorld`: the WORLD position a BAKE-space channel point lands on (the meander warp inverted;
    // it reads this bake's own local width field, hence `self`); `naturalAt`: the natural EMITTED
    // ground at a world column (the biome height blend and the coast applied). Empty on a
    // version-1 bake: the profile keeps the cell-centre point sample and no envelope is baked.
    // `hint` is the previous station's world point along the same line (or the point itself): the
    // warp can fold where its gradient exceeds 1, so the inversion continues along ONE branch.
    struct WorldFns {
        std::function<glm::vec2(float bakeX, float bakeZ, const FlowField& self, glm::vec2 hint)> toWorld;
        std::function<float(float worldX, float worldZ)> naturalAt;
        // the forward warp (world -> bake) the sampler applies before every channel query, so the
        // perch rule reads each envelope exactly as the sampler will at that world column
        std::function<glm::vec2(float worldX, float worldZ, const FlowField& self)> warp;
    };

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
              bool areaLaw = false,
              const WorldFns& fns = {});
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

    // ── M2d-2: junction fillets (areaLaw bakes only; docs/WorldForgeV2Plan.md M2d, §10 F4f) ─────
    // A D8 grid joins tributaries at 0° or 90°. At every confluence the TRIBUTARY's last segment
    // (its cell centre → the junction centre) becomes a quadratic Bézier whose end tangent meets
    // the main stem's downstream direction at Howard's angle cos θ = S_main / S_trib (ESurf
    // 13:219 §2.1.1; slopes from the longitudinal bed profile over kFilletSlopeCells), clamped to
    // the observed band of network means [kJunctionMinDeg, kJunctionMaxDeg] (45–72°, Seybold 2017
    // and Hooshyar 2017 via ESurf §1). The main stem's own segments stay straight. Every channel
    // query (channelAt, nearestChannel, the profile) reads the fillet polyline for such a cell.
    static constexpr float kJunctionMinDeg = 45.0f, kJunctionMaxDeg = 72.0f;
    static constexpr int   kFilletSlopeCells = 3;
    static constexpr int   kFilletSegments = 8;
    // The channel centreline of the cell at a world column, in BAKE space (before the meander
    // warp): 2 points for a straight segment, kFilletSegments + 1 for a fillet; empty off the
    // network or at a sink.
    std::vector<glm::vec2> centrelineAt(float worldX, float worldZ) const;
    int filletCount() const { return static_cast<int>(m_fillets.size()); }

    // ── M2d-3: the local channel width field (areaLaw bakes only) ────────────────────────────
    // Per cell the largest half-width of any river cell within kLocalWidthRadius cells, box-blurred
    // over the same radius, read bilinearly between cell centres: a SMOOTH field (no seam when the
    // meander warp's wavelength follows it), ~0 away from every channel. In world units.
    static constexpr int kLocalWidthRadius = 2;
    float localHalfWidthAt(float worldX, float worldZ) const;

    // ── M2d-4: the valley SECTION beside carved channels (areaLaw bakes only; D36 / D37) ────────
    // The land beside a carved channel may not stand above the ENVELOPE
    //     rim(projection) + max(0, dist − halfWidth × kValleyWidthMul) × kValleySideSlope
    // — a flat alluvial floor at the bankfull rim (the longitudinal profile + the law depth, at the
    // column's projection on the channel) across kValleyWidthMul channel half-widths, then a
    // valley side rising at the hillslope bound; the natural ground wins wherever it is lower
    // (floodplain dips stay) or itself steeper (a cliff the mountain has stays). This replaces the
    // version-1 FRACTIONAL relief attenuation (relief × smoothstep(dist / valleyHalf)), which on
    // 100–300 u of mountain relief made walls of 10–64 u per column and, with the profile sampled
    // from the full relief at the cell centre, rivers on 100–250 u embankments (m2d4_red_green.txt).
    // kValleyWidthMul: GROUNDED range (Rosgen 1994 entrenchment 2.2×, Williams 1986 belt ≈ 5.2–6.2×)
    // — a labelled tunable, the floor's extent. kValleySideSlope 0.70 (35°): REASONED — the top of
    // the 30 ± 5° threshold-hillslope band (Frontiers Earth Sci. 2021, 10.3389/feart.2021.684365)
    // and equal to the scree repose the road earthworks use (WorldForgePlan::kReposeSlope).
    static constexpr float kValleyWidthMul   = 5.0f;
    static constexpr float kValleySideSlope  = 0.70f;
    // A source is not entered into a cell (nor expanded past it) once its lower bound stands this far
    // above the cell's sampled natural: the natural inside a 32 u cell is taken to lie within 24 u
    // below its centre sample (≈ a 1:1 slope over the half-diagonal); where the ground is steeper the
    // column keeps its natural, a wall the mountain has. DESIGN. The valley side ends at the crest:
    // an envelope never cuts into the next valley.
    static constexpr float kEnvNaturalMargin = 24.0f;
    // The envelope at a WORLD column (unwarped: the envelope is baked in world space — every
    // source's channel polyline is inverse-warped once at bake time, so distances, the floor and the
    // side are world units and the side is <= kValleySideSlope per world u by construction; GREEN 4
    // measured 2.2 u per column with the envelope in bake space, and a stretch estimate (GREEN 5)
    // widened downstream floors over upstream reaches). The lowest over the sources listed for the
    // column's cell, or 1e30 where no carved, profiled channel reaches. Pure per column.
    // (warpedX, warpedZ) is the column's meander-warped position (the carve's query point): the RIM
    // at a column is read as the carve reads it — the profile at the bake-space projection of the
    // warped point — so the floor beside a channel is exactly its bed's rim there (a station-nearest
    // rim on the world polyline differed from the carve by several u where the warp shears angles,
    // GREEN 9's lips); the floor's extent and the side use the world polyline's distance.
    float valleyEnvelopeAt(float worldX, float worldZ, float warpedX, float warpedZ) const;
    bool  hasValleySourcesAt(float worldX, float worldZ) const;   // the column's cell lists a source
    // Diagnostics: the listed source cells at a column and one source's own envelope there.
    std::vector<int> valleySourcesAt(float worldX, float worldZ) const;
    struct EnvSample { float env = 1e30f, perp = 1e30f, rim = 0.0f, valleyHalf = 0.0f; };
    EnvSample envelopeSample(int sourceCell, float worldX, float worldZ, float warpedX, float warpedZ) const;
    bool  isFoldedSource(int sourceCell) const;   // the meander warp folds the channel of this cell (diagnostics)
    // The source's channel polyline in WORLD space (the branch the envelope was baked around); null
    // off the network. Diagnostics: where the warp folds, the carve shows the channel on more than
    // one branch and only this one carries the valley.
    const std::vector<glm::vec2>* sourceWorldPolyline(int sourceCell) const;
    float floorHalfOf(int sourceCell) const;      // the floor half-width the envelope uses for this source (diagnostics)
    // The flat floor's half-width shrinks as the bed steepens: a floodplain is a feature of a gentle
    // reach; at a bed slope of kValleySideSlope or more the valley is a V (the side rises from the
    // channel edge). Without it a reach's floor reached under the banks of the reach upstream of it
    // across a D8 bend wherever the bed dropped more than the side could climb (GREEN 7/8).
    // REASONED (floodplain width falls with valley slope — confinement); kValleyWidthMul stays the
    // maximum, the labelled tunable.
    static float floorHalfFor(float halfWidth, float bedSlope) {
        return halfWidth * kValleyWidthMul * std::clamp(1.0f - bedSlope / kValleySideSlope, 0.0f, 1.0f);
    }
    // The section rule itself, exposed for the unit test: the ground beside a channel.
    static float valleySectionY(float natural, float rim, float dist, float valleyHalf) {
        return std::min(natural, rim + std::max(0.0f, dist - valleyHalf) * kValleySideSlope);
    }
    int valleySourceCount() const { return static_cast<int>(m_envPool.size()); }   // tests / diagnostics
    // M2d-4 residual: what the perch rounds did to the profile (an instrument for the bake log and
    // the tests): rounds run, reaches lowered per round, the cumulative lowering of a reach's base
    // against the D37 footprint minimum it started from.
    struct PerchStats {
        int rounds = 0;
        std::vector<int> changed;        // per round: reaches whose base moved
        float maxLowering = 0.0f;        // the deepest cumulative lowering of one reach (u)
        float sumLowering = 0.0f;        // over all reaches (u)
        int loweredOver8 = 0;            // reaches lowered by more than 8 u in total
        int reaches = 0;                 // carved reaches outside basins the rule looked at
        long long columns = 0;           // world columns the band floods visited (all rounds)
        long long banks = 0;             // bank columns the envelope was read at (all rounds)
        int overflow = 0;                // reaches whose band flood hit its column cap (a fold)
        float worstDeficitLeft = 0.0f;   // the last round's largest bank deficit (rim above the land beside it), u
        struct Deep { int cell = -1; float lowering = 0.0f; int lastSrc = -1; float lastDeficit = 0.0f; int rounds = 0; };
        std::vector<Deep> deepest;       // the five reaches lowered most, with the source that last drove each
    };
    const PerchStats& perchStats() const { return m_perch; }

    // ── M2e: spring SITES (areaLaw bakes only; D17: data, no water) ─────────────────────────
    // Where the network gathers water: a creek HEAD (a channel cell with no channel inflow,
    // outside a coarse basin), a BASIN FEED (the last channel cell before a coarse basin: where a
    // stream enters a lake) and a BASIN OUTLET (the basin cell whose receiver leaves the basin:
    // where the lake drains). catchmentCells is the cell's accumulated drainage (incl. macro
    // inflows); the generator turns it into a discharge with the world's runoff.
    struct Spring {
        int   cell = -1;
        float x = 0.0f, z = 0.0f;      // the cell centre, bake space
        int   kind = 0;                // 0 head, 1 basin feed, 2 basin outlet
        int   catchmentCells = 0;
    };
    const std::vector<Spring>& springs() const { return m_springs; }

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
        float bedSlope = 0.0f;    // M2d-3: the profile's fall per unit length along the hit segment (>= 0)
        int   cell = -1;          // M2d-3: the row-major bake cell whose channel was hit (tests, diagnostics)
        float t = 0.0f;           // M2d-4: the hit's projection parameter along the cell polyline (0 = the cell's vertex, 1 = downstream)
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
    // M2d-2: per cell the index of its fillet polyline (−1 = straight segment).
    std::vector<int> m_filletOf;
    std::vector<std::vector<glm::vec2>> m_fillets;
    std::vector<float> m_localHalfW;   // M2d-3: blurred local half-width per cell
    // M2d-4: per cell, the singly-linked list (pool indices) of carved channel cells whose valley
    // envelope can win somewhere inside the cell (−1 = none).
    struct EnvEntry { int src; int next; };
    std::vector<int>      m_envHead;
    std::vector<EnvEntry> m_envPool;
    // per source cell: its channel polyline in WORLD space (up to 9 stations) and the rim at each
    std::vector<int>                    m_envPolyIdx;   // per cell, −1 = not a source
    std::vector<std::vector<glm::vec2>> m_envPolys;
    std::vector<std::vector<float>>     m_envRims;
    std::vector<float>                  m_envFloorHalf;   // per source: the floor's half-width (shrinks with the bed slope)
    std::vector<uint8_t>                m_envFolded;      // per source: its world polyline reverses or collapses (the warp folds)
    PerchStats                          m_perch;
    EnvSample envelopeOf(int rc, float worldX, float worldZ, float warpedX, float warpedZ) const;
    std::vector<Spring> m_springs;     // M2e: spring sites
    // The polyline a cell's channel follows (straight or fillet), used by every query.
    const std::vector<glm::vec2>& cellPolyline(int rc, std::vector<glm::vec2>& scratch) const;
    int m_maxAccum = 0;
    int m_maxOrder = 0;
    size_t m_released = 0;      // cells processed by the Kahn pass (== cell count iff acyclic)
};

}  // namespace Phyxel
