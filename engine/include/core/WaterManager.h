#pragma once

#include "core/RippleField.h"
#include "core/WaterSimulation.h"
#include "core/WorldConstants.h"
#include "vulkan/ComputePipeline.h"
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace Phyxel {

class ChunkManager;
namespace Vulkan { class VulkanDevice; }

namespace Core {

// One renderable water surface quad. Carries a *sloped* top (per-corner world Y,
// averaged from neighbouring columns to remove stair-stepping) and the water column's
// depth so the shader can darken/opacify deep water and fade thin shorelines.
struct WaterSurfaceCell {
    glm::vec4 centerDepth; // xyz = cell-center surface point (y = cellY+fill), w = column depth (cells)
    glm::vec4 corners;     // per-quad-corner world Y: (-x,-z), (+x,-z), (+x,+z), (-x,+z)
    glm::vec4 skirt;       // per-edge side-face bottom world Y: (+x), (-x), (+z), (-z)
    // WaterSystemV3 Phase 3 — flow shading. xy = horizontal flow direction (normalized, 0 if
    // still), z = flow strength 0..1 (how hard it's moving), w = foam 0..1. The shader advects the
    // ripple normals along xy at rate z, so a river reads as moving and a lake stays calm.
    glm::vec4 flow;
};

// Runs the CPU water cellular automaton (WaterSimulation) over a fixed axis-aligned
// region of the live world. Solidity is read from the chunk terrain; the sim is
// stepped at a fixed rate independent of frame rate. This is the CPU integration that
// precedes the GPU compute port + per-cell rendering (see docs/WaterSystem.md).
class WaterManager {
public:
    WaterManager(ChunkManager* chunkManager, const glm::ivec3& origin, const glm::ivec3& dims);

    // Re-read terrain solidity for the whole region from the chunk manager. Call after
    // the world loads and whenever terrain changes (e.g. after destruction).
    void syncSolidsFromChunks();

    // Fixed-timestep stepping; accumulates real time and steps at STEP_HZ.
    void update(float dt);

    // Relocate the simulated region so its local origin becomes `newOrigin` (world cells), keeping
    // m_dims fixed (GPU buffers + the O(dims) sweep budget stay valid). The water field is translated
    // so world content stays put (WaterSimulation::shift), terrain solidity is re-read for the moved
    // window, and channels/ocean/springs are re-projected from their world-space authoring lists.
    // This is WaterSystemV2 Phase A: the region can travel with the player (and, later, to where the
    // procedural generator placed rivers/lakes). Mass is conserved for water that stays in-window.
    void recenter(const glm::ivec3& newOrigin);

    // Keep the region centred on `focusWorld` (e.g. the camera/player), recentering ONLY when the
    // focus has drifted past a dead zone (`hysteresisCells` horizontally; max(4, dims.y/4)
    // vertically) so it doesn't recenter every frame. Vertical following (Phase C2) is what lets
    // inland water work at altitude — river beds/lakes sit wherever the terrain is, and a
    // Y-anchored box only ever covered the sea band. Y is clamped ≥ 0. Returns true if it
    // recentered. Call once per frame with the camera position (WaterSystemV2 A2/C2).
    bool followTo(const glm::vec3& focusWorld, int hysteresisCells);

    // World-space helpers. Amount may be negative to remove water.
    void  placeWater(const glm::vec3& worldPos, float amount);
    float massAtWorld(const glm::vec3& worldPos) const;

    // ── Scoop (tangible-water Phase D) ────────────────────────────────────────────────────────
    // Remove up to `amount` mass from this column, top-down (bucket semantics). Returns what was
    // actually taken. On PINNED water (ocean/lake/river) the next step re-pins — an infinite
    // body refills, by design. On a FINITE body the body's level record drops to the scooped
    // column's new surface, so the loss persists (across recenters, save/load, and out-of-window
    // queries). Scooping is the proof of tangibility: small water is consumable.
    float scoopWater(const glm::vec3& worldPos, float amount);

    // ── Entity-facing water query (small-scale plan Phase 4.1) ────────────────────────────────
    // THE gameplay/physics surface for "am I in water, how deep, which way is it moving" —
    // generalized from the camera's submergence walk so every consumer (fog, buoyancy, wading,
    // splash detection) reads the same facts. Inside the sim region the SIM is authoritative
    // (fill-fraction + sub-voxel-floor aware, connectivity-honest: a sealed dry cavity reads
    // dry); outside it falls back to the baked table when bound, else the implicit sea level
    // when the world has water enabled (setImplicitSea), else dry.
    struct WaterSample {
        bool  inWater = false;    // the queried point sits below a water surface
        float surfaceY = 0.0f;    // world Y of that surface (valid only when inWater)
        float depthBelow = 0.0f;  // metres/voxels of water above the point (0 when dry)
        glm::vec2 flow{0.0f};     // horizontal flow at the point's cell (sim-only; 0 elsewhere)
    };
    WaterSample sampleWater(const glm::vec3& worldPos) const;

    // ── Current velocity (tangible-water Phase E) ─────────────────────────────────────────────
    // Horizontal water-current VELOCITY (m/s, y = 0) at a world point — THE physics/gameplay
    // flow query ("which way is this water carrying things, and how hard"). In-window: the live
    // CA flow proxy scaled to world speed; where the pinned river field shows ~none, the baked
    // kinematic downhill direction at an order-scaled speed (a pinned river performs no
    // transfers, so the proxy alone would read a river as a lake — same substitution the
    // surface shading makes, through the same helper so they can't disagree). Out-of-window:
    // kinematic only. Zero when dry/still. Thread-safe for concurrent reads (immutable bake
    // queries + sim arrays that mutate only on the main thread between physics steps).
    glm::vec3 flowAtWorld(const glm::vec3& worldPos) const;
    // ⚑GROUND: FLOW_FULL (0.15 mass/step — a vigorous channel) maps to 1.5 m/s: brisk stream
    // pace, and the speed the surface shading already treats as "clearly moving".
    static constexpr float kFlowSpeedScale = 10.0f;

    // Fraction of an AABB below the local water surface, for buoyancy/drag. Samples the surface
    // at the footprint's centre + 4 corners and averages the per-column submerged fractions —
    // cheap, monotone, and exact for a flat surface over a uniform column.
    float submergedFraction(const glm::vec3& aabbMin, const glm::vec3& aabbMax) const;

    // Whether an implicit flat sea at seaLevel() exists OUTSIDE the region/table (game.json
    // water.enabled). Off = out-of-region points with no baked table read dry, so a waterless
    // world never reports phantom submersion at y < 16.
    void setImplicitSea(bool on) { m_implicitSea = on; }
    bool implicitSea() const { return m_implicitSea; }

    // Update one cell's solid state (world coords) — wired to voxel break/place so
    // water flows into newly-removed cells on the next step. Cheap; ignores cells
    // outside the region.
    void  setSolidWorld(int worldX, int worldY, int worldZ, bool solid);

    // Sub-voxel floor fraction at a world cell (WaterSystemV3 Phase 4B). Normally derived from the
    // chunk terrain by syncSolidsFromChunks; settable directly for tests and authoring.
    void  setFloorWorld(int worldX, int worldY, int worldZ, float fraction);
    float floorAtWorld(const glm::vec3& worldPos) const;

    // --- Ocean seam (infinite reservoir at sea level) ---
    // Open cells at/below `seaLevel` that are connected to an ocean seed become an
    // infinite reservoir: they hold sea level, refill when dug, and flood through
    // breaches; sealed sub-sea cavities stay dry. (See docs/WaterSystem.md.)
    void  setSeaLevel(float worldY);
    float seaLevel() const { return m_seaLevel; }
    void  addOceanSeed(const glm::vec3& worldPos); // a point the ocean floods out from
    void  clearOcean();                             // remove the ocean (seeds + pins)

    // Ocean as a BOUNDARY CONDITION (WaterSystemV2 Phase A2b) instead of / in addition to point
    // seeds: every region-edge cell at/below sea level (and non-solid) acts as an ocean seed, so the
    // flood re-establishes from the frontier wherever the region moves. This is what lets a following
    // region keep the sea when the player walks away from a point seed (point seeds leave the window
    // and the ocean would otherwise drain). Connectivity-gating still applies — a sealed sub-sea
    // pocket not reachable from an edge stays dry. (An "implicit ocean": everything at/below sea level
    // connected to the region boundary is sea; Phase C refines it with the baked ocean extent.)
    void  setOceanBoundary(bool on);
    bool  oceanBoundary() const { return m_oceanBoundary; }

    // --- Baked WATER TABLE (WaterSystemV2 Phase C: generation feeds water) ---
    // Bind a per-column water-level source (world column → flat water-surface world Y, or any value
    // <= TABLE_DRY for dry land — HydrologyMap::waterLevelAt matches this contract directly). While
    // bound, rebuildOcean derives ALL source pins from the table via fillWaterTable — baked lakes
    // fill at their own spill level and the ocean at sea level, re-derived wherever the region
    // recenters — SUPERSEDING the scalar sea level, point seeds, and the boundary flag (springs are
    // still re-applied on top). Bind nullptr to return to the authored/scalar path. The callback
    // must stay valid for the manager's lifetime and be cheap-ish (called once per column per
    // rebuild).
    static constexpr float TABLE_DRY = -1e29f;
    void setWaterTable(std::function<float(float worldX, float worldZ)> levelAt);
    bool hasWaterTable() const { return static_cast<bool>(m_tableFn); }

    // --- Water BODY identity (tangible-water Phase C) ---
    // Bind "which body is this column, and what kind" (WaterBodyIndex::bodyAt matches). While
    // bound, FINITE bodies (ponds) are EXCLUDED from table pinning: their water is real
    // conserved mass hydrated from `baseline + bodyDelta` — scooping them lowers a per-BODY
    // level record that persists (one float per body: a settled body's surface is flat, so the
    // body delta IS the sparse representation — per-column records cannot express a scooped
    // 30k-column pond without re-minting mass from unrecorded neighbors on every rebuild).
    // Infinite bodies (ocean/lakes) keep pin semantics untouched. Null id (< 0) = no body.
    struct BodyInfo {
        int64_t id = -1;
        bool    finite = false;    // true = pond-class: unpinned, scoopable, delta-tracked
        float   baselineLevel = 0.0f;
        int     areaColumns = 0;   // surface columns (finite bodies) — scoop's level/volume ratio
    };
    void setBodyQuery(std::function<BodyInfo(float worldX, float worldZ)> bodyAt);
    bool hasBodyQuery() const { return static_cast<bool>(m_bodyFn); }
    // Current level delta of a finite body (0 = at baseline; kBodyDry = scooped dry).
    float bodyDelta(int64_t bodyId) const {
        auto it = m_bodyDeltas.find(bodyId);
        return it == m_bodyDeltas.end() ? 0.0f : it->second;
    }
    size_t bodyDeltaCount() const { return m_bodyDeltas.size(); }
    static constexpr float kBodyDry = -1e28f;   // sentinel delta: the body was drained dry

    // --- Baked RIVERS (WaterSystemV2 Phase C2) ---
    // Bind a river-channel CARVE-DEPTH query (world column → carve depth in voxels, 0 = not on a
    // channel — FlowField::channelAt(...).depth matches the contract). While bound, every rebuild
    // channel-tags each river column's BED cell (first open cell above real solid), and columns
    // that are actually RECESSED (depth ≥ 0.5 — the carve dug at least half a voxel) get their bed
    // pinned as a FULL source — the river is an implicit reservoir along its carved course, the
    // same semantics as the ocean/lake pins: the ribbon is full end-to-end and digging the bank
    // makes river water pour out (and re-pin). Non-recessed band edges (parabolic depth → 0) are
    // tagged but NOT pinned — pinning them puts full water ON the bank and guarantees valley
    // flooding (measured: growth got WORSE than the untuned case). Evaporation bounds off-channel
    // spill. (Edge-only frontier inflow was tried first and died into puddles ~5 cells out — thin
    // CA flow attenuates geometrically.) Unloaded columns are skipped; the chunk-stream-in
    // solidity sync re-triggers the rebuild once the bed exists.
    void setRiverQuery(std::function<float(float worldX, float worldZ)> depthAt);
    bool hasRiverQuery() const { return static_cast<bool>(m_riverFn); }

    // Strahler ORDER of the channel at a world column (0 = none) — FlowField::channelAt(...).order
    // matches the contract: the order of the SAME segment hit the depth query reports, NEVER the
    // column's coarse cell order (orderAt) — the two disagree wherever a creek segment crosses a
    // higher-order cell, and mixing them full-pins uncarved creek ground (measured flood).
    // Orders 1-2 are creeks (small-scale plan Phase 2a): their beds get a FRACTIONAL pin
    // clamped to the sim's MIN_HOLD, so a creek is a static sub-voxel ribbon that physically CANNOT
    // sheet sideways (a pinned cell at/below the hold never makes horizontal transfers) — the
    // zero-flood-risk re-opening of the gates the 496cdc10 revert closed. Unbound → every channel
    // is treated as order ≥ 3 (legacy full-pin semantics, keeps existing tests/worlds unchanged).
    void setRiverOrderQuery(std::function<int(float worldX, float worldZ)> orderAt);
    bool hasRiverOrderQuery() const { return static_cast<bool>(m_riverOrderFn); }

    // KINEMATIC river flow direction (WaterSystemV3 Phase 3). A baked river is pinned full along
    // its whole carve, so it performs NO transfers and the CA's flow proxy reads zero — a river
    // would shade like a long thin lake. Bind the bake's downhill direction
    // (FlowField::flowDirAt) and rebuildSurface stamps it onto river surface cells instead.
    //
    // STATE THIS PLAINLY: that is a VISUAL flow over a hydrostatically static field. The water is
    // not advecting; it is being shaded as though it were. Real advection needs CA momentum
    // (docs/WaterSystemV3.md Phase 4). Bind nullptr to disable.
    void setRiverFlowQuery(std::function<glm::vec2(float worldX, float worldZ)> dirAt);
    bool hasRiverFlowQuery() const { return static_cast<bool>(m_riverDirFn); }

    // --- L3 bake-vs-terrain validation (docs/WaterSystemV2.md Phase C) ---
    // The bake decides WHERE water sits (per-column levels); the carved terrain decides whether it
    // is CONTAINED. Where they disagree at a water body's rim — a baked-DRY column whose carved
    // surface sits BELOW an adjacent wet column's level — the CA legitimately levels water into it
    // (pinned→unpinned flow) and the body leaks/spreads beyond its baked extent (observed live:
    // the sea filling bake-dry shoreline flats, region mass rising 6923→9912 over 70 s). This scan
    // quantifies that mismatch over a world-space rect using LOADED terrain only.
    struct TableValidation {
        int columns  = 0;    // columns evaluated (terrain loaded)
        int unloaded = 0;    // columns skipped (no solid found in the scan range)
        int wet      = 0;    // baked-wet columns
        int rim      = 0;    // baked-dry columns N4-adjacent to a wet column
        int rimLeaks = 0;    // rim columns whose surface < the neighbor's water level (leak!)
        float      worstLeakDepth = 0.0f;  // max (neighborLevel − rimSurfaceY)
        glm::ivec2 worstLeakAt{0, 0};      // world x,z of the worst leak
    };
    // Scans [minXZ, maxXZ] inclusive. Requires a bound water table and a ChunkManager; terrain
    // surface = topmost solid via hasVoxelAt, scanned downward from maxScanY.
    TableValidation validateTable(const glm::ivec2& minXZ, const glm::ivec2& maxXZ,
                                  int maxScanY = 200) const;
    // Query the bound table at a world column (TABLE_DRY when dry or no table bound) — debug/tooling
    // surface so the baked water can be probed without chunks being loaded there.
    float tableLevelAt(float worldX, float worldZ) const {
        return m_tableFn ? m_tableFn(worldX, worldZ) : TABLE_DRY;
    }

    // --- Authored sources (springs / river heads) ---
    // A spring is a persistent source pinned to `mass` each step — a continuous supply
    // (a fountain, a river head). Survives ocean re-floods (kept separate from the
    // ocean's pinned cells).
    void  addSpring(const glm::vec3& worldPos, float mass);
    void  clearSprings();

    // --- Channels (authored riverbeds, exempt from evaporation) ---
    void  setChannelWorld(int worldX, int worldY, int worldZ, bool channel);
    void  setChannelRegion(const glm::ivec3& a, const glm::ivec3& b); // inclusive box

    // --- Evaporation (off by default) ---
    // Off: water keeps flowing and volume is conserved (the default — good for
    // draining/leveling between basins). On: thin cells (the frontier of a free spill,
    // films) decay each step, bounding spread and drying shorelines, while deep water
    // and channels persist. Honoured by both the CPU and GPU step (read live each tick).
    void  setEvaporation(bool enabled) { m_sim.setEvaporation(enabled); }
    bool  evaporation() const { return m_sim.evaporation(); }

    // --- Persistence accessors (authoring inputs; the field reconstructs on load) ---
    const std::vector<glm::ivec3>& oceanSeeds()   const { return m_oceanSeeds; }
    const std::vector<glm::ivec3>& channelCells() const { return m_channelCells; }

    // ── Poured-water persistence (water-as-terrain-stage P3) ────────────────────────────────────
    // The bounded region drops water that falls off its frontier when it recenters — hand-poured
    // (unpinned) ponds silently vanished when the player walked away (and the window follows the
    // camera VERTICALLY too, so even looking up could drain a pour). Now:
    //  - recenter() CAPTURES each departing column's unpinned surface level into a sparse
    //    world-keyed override store before the shift;
    //  - rebuildOcean() RESEEDS any stored override that re-enters the window (erasing it as the
    //    water becomes live sim mass again — it will be re-captured if it departs again);
    //  - serializeOverrides()/loadOverrides() round-trip the store through world_meta, and
    //    captureOverridesInWindow() folds still-visible pours in at save time.
    // PINNED water (sea/lakes/rivers/springs) is never captured — it re-derives from the bake.
    // Levels only ever fall between capture and re-capture (mass spreads/evaporates, reseed fills
    // to at most the stored level), so walk-away/walk-back cycles cannot grow water.
    size_t overrideCount() const { return m_overrides.size(); }
    // Serialized water state, v2 (tangible-water Phase C): body-level deltas + the legacy column
    // overrides + the outflow bank, one text blob. loadOverrides also accepts the v1 format
    // (bare "x z level" / "B x z mass" lines) for world_meta["water_overrides"] migration.
    std::string serializeOverrides() const;
    bool loadOverrides(const std::string& data);     // replaces the store; false on parse garbage
    void captureOverridesInWindow();                 // save-time hook (leaves the live water alone)
    static constexpr size_t MAX_OVERRIDES = 65536;   // sparse store cap (arbitrary eviction beyond)

    // ── CA edge outflow (water-as-terrain-stage P4) ─────────────────────────────────────────────
    // With this on, the sim's window edges bleed unpinned water instead of walling it (see
    // WaterSimulation::setEdgeOutflow); the bled mass lands in a world-keyed MASS bank at the
    // column just outside the window and is dropped back as live water when the window reaches
    // it (or persists via serializeOverrides). Capped per column (BANK_CAP_PER_COLUMN) so a
    // drain fed by an infinite pinned reservoir cannot grow the bank forever — beyond the cap
    // the outside world is deemed to have absorbed it. Enabled by the editor for baked worlds.
    void setEdgeOutflow(bool on) { m_sim.setEdgeOutflow(on); }
    bool edgeOutflow() const { return m_sim.edgeOutflow(); }
    float outflowBankTotal() const {
        float t = 0.0f;
        for (const auto& [k, m] : m_outflowBank) t += m;
        return t;
    }
    static constexpr float BANK_CAP_PER_COLUMN = 4.0f;

    // --- GPU backend ---
    // Run the per-tick flow step on a compute shader instead of the CPU. Behaviour is
    // close (gather-formulated, see docs/WaterSystem.md), not bit-identical. Masks
    // (solid/source/channel) and the field round-trip CPU<->GPU each step, so all CPU
    // authoring (ocean flood, springs, place/edit) and rendering keep working unchanged.
    void enableGpu(Vulkan::VulkanDevice* device); // create GPU resources (once)
    void setUseGpu(bool on) { m_useGpu = on && m_gpuReady; }
    bool useGpu() const { return m_useGpu; }
    std::vector<glm::vec4> springsData() const {       // (x, y, z, mass) per spring
        std::vector<glm::vec4> out;
        out.reserve(m_springs.size());
        for (const Spring& s : m_springs)
            out.emplace_back(float(s.cell.x), float(s.cell.y), float(s.cell.z), s.mass);
        return out;
    }

    // ── Ripple / disturbance field (small-scale plan Phase 3) ─────────────────────────────────
    // Local dynamic surface disturbances — impact rings, footstep wakes, splashes. Purely
    // visual (never touches CA mass); ticked in update(), recentred alongside the region in
    // followTo(). Entities inject via addRipple; the renderer samples ripple() by world XZ.
    void addRipple(const glm::vec3& worldPos, float radius, float strength) {
        m_ripple.addImpulse(glm::vec2(worldPos.x, worldPos.z), radius, strength);
    }
    const RippleField& ripple() const { return m_ripple; }

    float totalMass() const { return m_sim.totalMass(); }
    const glm::ivec3& origin() const { return m_origin; }
    const glm::ivec3& dims() const   { return m_dims; }
    const WaterSimulation& sim() const { return m_sim; }
    // Times rebuildSurface() has run (observability): lets tests prove a settled field skips the
    // O(box) surface scan that update() would otherwise pay at STEP_HZ.
    unsigned long long surfaceRebuilds() const { return m_surfaceRebuilds; }

    // Renderable water surface: one WaterSurfaceCell per surface cell (a water cell
    // whose cell above is ~empty) with a smoothed sloped top + column depth. Rebuilt
    // whenever the field changes (step/sync/place).
    const std::vector<WaterSurfaceCell>& surfaceCells() const { return m_surface; }

    // Detected waterfall lips: xyz = world lip point, w = drop height. A side-face edge
    // whose skirt falls >= WATERFALL_MIN_DROP into open air or much-lower water. Consumed
    // by the host to spawn mist/spray. Rebuilt alongside the surface.
    const std::vector<glm::vec4>& waterfalls() const { return m_waterfalls; }

    // Minimum cell mass that renders / is treated as a surface (ignores thin film).
    static constexpr float RENDER_MIN = 0.05f;
    // A skirt edge dropping at least this far (cells) is treated as a waterfall lip.
    static constexpr float WATERFALL_MIN_DROP = 1.5f;
    static constexpr size_t MAX_WATERFALLS = 48; // cap mist emitter points

private:
    bool worldToLocal(const glm::vec3& w, int& lx, int& ly, int& lz) const;
    void rebuildSurface();
    void rebuildOcean(); // re-run the ocean flood-fill from the seeds
    void applySprings(); // (re-)pin authored springs after the ocean clears sources
    void applyRiverInflows(); // channel-tag river beds + pin edge river columns (Phase C2)
    // P3 (poured-water persistence): capture one column's unpinned surface into m_overrides,
    // considering only cells y in [yLo, yHi] (the slice leaving the window); reseed in-window
    // stored overrides as unpinned mass (called by rebuildOcean on both table and authored paths).
    void captureColumnOverride(int lx, int lz, int yLo, int yHi);
    void applyOverrides();
    void drainOutflowToBank();   // P4: move the sim's per-column edge outflow into m_outflowBank
    void applyOutflowBank();     // P4: drop in-window banked mass back as live water
    // Phase C (finite bodies): hydrate in-window finite-body columns to baseline+delta (top-up
    // only, idempotent), consuming any capture-time level observations into m_bodyDeltas.
    void applyFiniteBodies();
    // Baked kinematic channel flow at a world column: normalized downhill direction (zero when
    // off-channel/unbound), Strahler order via orderOut. THE shared source for both the surface
    // shading and flowAtWorld (tangible-water Phase E).
    glm::vec2 kinematicRiverFlow(float wx, float wz, int* orderOut = nullptr) const;
    static uint64_t packColumnKey(int wx, int wz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(wx)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(wz));
    }

    float                   m_seaLevel = kSeaLevelY; // shared default (WorldConstants.h) — must
                                                     // match the sea-plane renderer or they drift
    std::function<float(float, float)> m_tableFn;    // baked water table (Phase C); null = authored path
    // Snapped per-column table levels in LOCAL grid Y (INT_MIN = dry), cached from the last
    // rebuildOcean: the shoreline-snapped grid is the truth about where pinned baked water sits,
    // and rebuildSurface uses it to suppress pinned-at-level cells the water-layer clipmap draws
    // (water-layer P1). Empty when no table is bound.
    std::vector<int>                   m_tableLvlLocal;
    std::function<float(float, float)> m_riverFn;    // river carve depth (Phase C2); null = none
    std::function<int(float, float)>   m_riverOrderFn; // Strahler order (creek pins); null = legacy ≥3
    std::function<glm::vec2(float, float)> m_riverDirFn; // baked downhill dir (V3 P3); null = none
    bool                    m_oceanDirty = false;
    bool                    m_oceanBoundary = false; // seed the ocean from the region edges (Phase A2b)
    bool                    m_implicitSea = false;   // implicit flat sea outside region/table (4.1)
    std::vector<glm::ivec3> m_oceanSeeds; // world-space flood seeds

    struct Spring { glm::ivec3 cell; float mass; };
    std::vector<Spring>     m_springs;      // world-space authored sources
    std::vector<glm::ivec3> m_channelCells; // world-space channel cells (for persistence)
    // P3: packed world column (x<<32|z) → captured unpinned water-surface world Y.
    std::unordered_map<uint64_t, float> m_overrides;
    // P4: packed world column → MASS bled out of the window edge, awaiting redeposit.
    std::unordered_map<uint64_t, float> m_outflowBank;
    // Phase C: body identity + per-BODY level deltas (finite bodies only; kBodyDry = drained).
    std::function<BodyInfo(float, float)> m_bodyFn;
    std::unordered_map<int64_t, float>    m_bodyDeltas;
    std::unordered_map<int64_t, float>    m_bodyObserved;   // per-recenter min observed level
    // Snapped finite-body per-column levels (LOCAL grid Y, INT_MIN dry) + body ids, cached each
    // rebuild — the far-layer suppression grid for UNPINNED at-rest body cells (mirror of
    // m_tableLvlLocal) and the hydration target map.
    std::vector<int>     m_bodyLvlLocal;
    std::vector<int64_t> m_bodyIdLocal;

    // GPU backend state.
    void stepGpu();        // upload field+masks, dispatch flow, read back
    void uploadMasks();    // solid/source/channel CPU -> GPU
    Vulkan::VulkanDevice*  m_vk = nullptr;
    bool                   m_useGpu = false;
    bool                   m_gpuReady = false;
    bool                   m_gpuMasksDirty = true;
    Vulkan::ComputePipeline m_flowPipe;
    VkBuffer       m_bufMassIn = VK_NULL_HANDLE,  m_bufMassOut = VK_NULL_HANDLE;
    VkBuffer       m_bufSolid  = VK_NULL_HANDLE,  m_bufSource = VK_NULL_HANDLE, m_bufChannel = VK_NULL_HANDLE;
    VkDeviceMemory m_memMassIn = VK_NULL_HANDLE,  m_memMassOut = VK_NULL_HANDLE;
    VkDeviceMemory m_memSolid  = VK_NULL_HANDLE,  m_memSource = VK_NULL_HANDLE, m_memChannel = VK_NULL_HANDLE;
    void*          m_mapMassIn = nullptr;  void* m_mapMassOut = nullptr;
    void*          m_mapSolid  = nullptr;  void* m_mapSource  = nullptr; void* m_mapChannel = nullptr;

    ChunkManager*   m_cm;
    glm::ivec3      m_origin;
    glm::ivec3      m_dims;
    WaterSimulation m_sim;
    RippleField     m_ripple;   // visual disturbance field (Phase 3); follows the region focus
    std::vector<WaterSurfaceCell> m_surface;   // cached renderable surface cells
    std::vector<glm::vec4>        m_waterfalls; // mist emitter points (lip xyz, drop w)
    float           m_accum = 0.0f;
    unsigned long long m_surfaceRebuilds = 0;  // observability (see surfaceRebuilds())

    static constexpr float STEP_HZ = 20.0f;
    static constexpr float STEP_DT = 1.0f / STEP_HZ;
    static constexpr int   MAX_STEPS_PER_UPDATE = 4; // cap to avoid spiral-of-death
};

} // namespace Core
} // namespace Phyxel
