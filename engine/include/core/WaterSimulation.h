#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

// CPU prototype of the voxel water cellular automaton (Phase 2 — see
// docs/WaterSystem.md). A dense grid of per-cell water "mass" in [0, MAX_MASS],
// stepped by simple, mass-conserving flow rules:
//   1. Gravity: a cell pushes mass straight down into the free capacity below.
//   2. Horizontal: a cell donates a damped fraction of its excess to lower
//      same-level neighbors, leveling the surface.
// Solid cells block water and never hold mass. Mass is conserved exactly (every
// transfer is a paired -from/+to), which this prototype exists to validate before
// the rules are ported to a GPU compute shader.
//
// Coordinates: y is up (matching the engine). "Down" is -y.
class WaterSimulation {
public:
    static constexpr float MAX_MASS = 1.0f; // full cell

    WaterSimulation(int sizeX, int sizeY, int sizeZ);

    int sizeX() const { return m_sx; }
    int sizeY() const { return m_sy; }
    int sizeZ() const { return m_sz; }

    bool inBounds(int x, int y, int z) const {
        return x >= 0 && x < m_sx && y >= 0 && y < m_sy && z >= 0 && z < m_sz;
    }

    void  setSolid(int x, int y, int z, bool solid);
    bool  isSolid(int x, int y, int z) const;

    // Channel cells (authored riverbeds) are exempt from evaporation, so water carried
    // along them doesn't fade — an authored river flows its full length. (A binary
    // special case of a per-material flow-resistance scalar; see docs/WaterSystem.md.)
    void  setChannel(int x, int y, int z, bool channel);
    bool  isChannel(int x, int y, int z) const;

    // Add (or, with a negative amount, remove) water at a cell; clamped to >= 0.
    void  addWater(int x, int y, int z, float amount);

    // Mark a cell as a source held at a fixed mass: it is re-pinned to `mass` at the
    // start of every step, acting as an infinite reservoir. This models the implicit
    // ocean's boundary (pin edge cells to sea-level mass) and authored springs/rivers.
    // Note: sources inject/remove mass, so total mass is not conserved while any exist.
    void  setSource(int x, int y, int z, float mass);
    void  clearSource(int x, int y, int z);

    // Ocean seam: flood from `localSeeds` through non-solid cells with y <= seaLevelY
    // and pin each reached cell as a full source — an infinite reservoir that holds sea
    // level, refills when dug, and floods through breaches. Cells unreachable from a
    // seed stay un-pinned, so sealed sub-sea cavities stay dry (connectivity-gating).
    // Clears all existing sources first (the ocean owns the source system for now).
    // Returns the number of cells pinned.
    int fillOcean(const std::vector<glm::ivec3>& localSeeds, int seaLevelY);

    // Generalized WATER TABLE (WaterSystemV2 Phase C — generation feeds water): per-COLUMN water
    // levels instead of one global sea level, so baked lakes fill at their own spill height and the
    // ocean at sea level, in one pass. `levelLocalY(lx, lz)` returns the column's water-surface cell
    // Y in LOCAL grid coords, or INT_MIN for dry land. Seeds: each wet column's SURFACE cell (y ==
    // min(level, sy-1), if open — an island column that is solid there simply doesn't seed; the
    // flood arrives laterally) plus every region side-edge cell at/below its column's level (the
    // water continues beyond the region window). The flood then spreads through open cells with
    // y <= their own column's level and pins each as a full source. Connectivity-gating is
    // preserved at the fine scale: a sealed cavity under a lake stays dry (unreachable from the
    // surface); the coarse-scale question "is this column wet, and how high" is the BAKE's job
    // (priority-flood already solved basin spills globally). Clears all sources first, like
    // fillOcean. Returns the number of cells pinned.
    int fillWaterTable(const std::function<int(int lx, int lz)>& levelLocalY);

    float massAt(int x, int y, int z) const;
    float totalMass() const;
    float minMass() const; // for invariant checks (should never go negative)

    // Raw data access for the GPU backend (upload masks / read back mass). The mass
    // vector is mutable so the GPU stepper can write the readback into it.
    std::vector<float>&         mass()              { return m_mass; }
    const std::vector<float>&   mass()        const { return m_mass; }
    const std::vector<uint8_t>& solidMask()   const { return m_solid; }
    const std::vector<float>&   sourceMask()  const { return m_source; }
    const std::vector<uint8_t>& channelMask() const { return m_channel; }
    bool                        evaporationOn() const { return m_evaporate; }
    int                         cellCount()   const { return m_sx * m_sy * m_sz; }

    // Evaporation sink: when enabled, cells thinner than EVAP_THRESHOLD lose mass each
    // step. This bounds free flow (a source/spill spreads, thins at the frontier, and
    // the thin edge evaporates → finite extent) and dries up thin films, while deep
    // (full) water is spared so ponds persist. Disabled by default so the pure CA is
    // mass-conserving for tests; the live game (WaterManager) turns it on.
    // Toggling wakes the whole field: a settled field can hold thin cells that only
    // became evaporation-eligible by the toggle (they'd otherwise never dry).
    void setEvaporation(bool enabled);
    bool evaporation() const { return m_evaporate; }

    static constexpr float EVAP_THRESHOLD = 0.1f;  // below this depth a cell evaporates
    static constexpr float EVAP_RATE      = 0.01f; // mass lost per step by a thin cell

    // Advance the simulation one tick. `flowSide` damps horizontal equalization
    // (0..1); lower = calmer/slower leveling.
    //
    // SLEEP (perf): a step that moves no mass (every transfer below MIN_FLOW, no source re-pin change,
    // no evaporation) marks the field SETTLED; subsequent steps then return immediately — O(1) instead
    // of the O(cell-count) sweep + double-buffer copy — until a disturbance (addWater / setSolid /
    // setSource / setChannel / fillOcean / shift) wakes it. This is the common case: still water and
    // dry regions cost nothing.
    //
    // ACTIVE SET (perf, per-column): a sweep only visits columns (x,z) whose mass changed since the
    // last sweep, plus their N4 neighbors (P = dirty ∪ N4(dirty)). A cell can only start flowing if
    // its own state or a horizontal neighbor's changed — vertical flow stays in-column — so a column
    // untouched by the last sweep and with untouched neighbors would recompute the exact same
    // below-MIN_FLOW transfers it did before; skipping it cannot change the result. The double-buffer
    // snapshot/write-back is likewise restricted to W = P ∪ N4(P) (every possible flow destination),
    // eliminating the O(box) m_next = m_mass copy. A PARTIALLY-active field (one dripping spring in a
    // big region) now pays O(active columns × height), not O(box).
    void step(float flowSide = 1.0f);

    // True once the field has reached rest (last executed step moved no mass); a disturbance clears it.
    bool settled() const { return m_settled; }
    // Force the field awake — the settle flag no longer reflects reality. Marks EVERY column dirty
    // (the caller wrote cells we cannot attribute). MUST be called by any code that mutates m_mass
    // OUTSIDE step()/the tracked mutators (notably the GPU stepper, which writes mass() directly):
    // otherwise a later CPU step() trusts a stale "settled"/clean-column state and freezes the field.
    void wake();
    // Count of steps that actually ran the sweep (skipped/settled steps don't increment it) — lets
    // tests prove that a settled field stops doing work.
    unsigned long long sweepsRun() const { return m_sweepsRun; }
    // Active-set observability: columns visited by the last executed sweep (the P set), and the
    // total column count — lets tests prove a local disturbance stays local.
    int columnsProcessedLastSweep() const { return m_colsProcessed; }
    int columnCount() const { return m_sx * m_sz; }

    // Translate the whole field by an integer cell `delta`: after the shift, the cell at local p
    // holds what was at local p+delta (so the WORLD content stays put while the grid window moves by
    // delta — the primitive a WaterManager uses to recenter its region on the player). Cells whose
    // source p+delta falls outside the old grid are the newly-exposed frontier: mass 0, not-solid,
    // not-source, not-channel (the manager re-syncs terrain solidity + re-applies ocean/springs on
    // the frontier). Mass is conserved for content that stays in-window; content shifted past an edge
    // is dropped (that lost mass is what the ocean boundary condition later replaces at the frontier).
    void shift(const glm::ivec3& delta);

private:
    size_t idx(int x, int y, int z) const {
        return static_cast<size_t>(x) + static_cast<size_t>(m_sx) *
               (static_cast<size_t>(y) + static_cast<size_t>(m_sy) * static_cast<size_t>(z));
    }
    size_t colIdx(int x, int z) const {
        return static_cast<size_t>(x) + static_cast<size_t>(m_sx) * static_cast<size_t>(z);
    }
    // Mark a column's mass as changed since the last sweep (mutators + in-sweep transfers).
    void markCol(int x, int z) { m_colDirty[colIdx(x, z)] = 1; m_settled = false; }
    void markAllCols(); // bulk disturbance (wake / fillOcean / shift / evaporation toggle)

    int m_sx, m_sy, m_sz;
    std::vector<float>   m_mass;
    std::vector<uint8_t> m_solid;
    std::vector<float>   m_next;   // scratch buffer reused across steps
    std::vector<float>   m_source;  // per-cell pinned mass; < 0 means "not a source"
    std::vector<uint8_t> m_channel; // per-cell: 1 = channel (no evaporation)
    std::vector<uint8_t> m_colDirty;   // per-column (x,z): mass changed since the last sweep
    std::vector<uint8_t> m_colProcess; // scratch: sweep set P = dirty ∪ N4(dirty)
    std::vector<uint8_t> m_colWrite;   // scratch: snapshot/write-back set W = P ∪ N4(P)
    int                  m_colsProcessed = 0; // |P| of the last executed sweep (observability)
    bool                 m_hasSources = false;
    bool                 m_evaporate  = false;
    bool                 m_settled    = false; // last step moved no mass → skip until disturbed
    unsigned long long   m_sweepsRun  = 0;     // steps that ran the full sweep (observability)
};

} // namespace Core
} // namespace Phyxel
