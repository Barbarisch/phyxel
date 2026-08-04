#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

// CPU prototype of the voxel water cellular automaton (Phase 2 — see
// docs/Water.md). A dense grid of per-cell water "mass" in [0, MAX_MASS],
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

    // ── SUB-VOXEL FLOOR (WaterSystemV3 Phase 4B) ──────────────────────────────────────────────
    // Fraction of a cell filled from the bottom by sub-voxel terrain (a low subcube/microcube
    // platform). Water in such a cell rests ON that floor rather than at the cell's base, so its
    // rendered surface sits `floor + fill*(1-floor)` up the cell instead of `fill`.
    //
    // RENDER-ONLY, deliberately: the CA's capacity is still a full unit per cell, so a floored cell
    // holds slightly more water than it physically should. Making capacity `1 - floor` is the
    // volumetrically correct version and is a recorded future goal (docs/Water.md Phase 4A)
    // — it rewrites the gravity/compression split, which is the most load-bearing code here.
    void  setFloor(int x, int y, int z, float fraction);
    float floorAt(int x, int y, int z) const;

    // Channel cells (authored riverbeds) are exempt from evaporation, so water carried
    // along them doesn't fade — an authored river flows its full length. (A binary
    // special case of a per-material flow-resistance scalar; see docs/Water.md.)
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
    // Pinned source mass at a cell (< 0 = not a source). Debug/probe surface (water_probe).
    float sourceAt(int x, int y, int z) const;

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

    // ── FLOW PROXY (WaterSystemV3 Phase 3) ────────────────────────────────────────────────────
    // Per-cell horizontal flow, derived for FREE from the transfers step() already computes: every
    // horizontal transfer of `f` mass in direction d contributes f*d to BOTH endpoints, and the
    // result is EMA-smoothed across steps so the renderer gets a stable direction instead of
    // per-tick jitter.
    //
    // HONEST NAMING: this is NOT a velocity in m/s. The CA has no momentum — it is a diffusion
    // rule — so this is "net mass moved per step, and which way", a FLOW PROXY. It is the right
    // input for shading (which way is the water going, and how hard) and the wrong input for
    // physics. Units: mass-fraction per step; a vigorous channel runs ~0.1-0.3.
    //
    // Vertical flow is deliberately NOT tracked: falling water is already identified by
    // WaterManager's waterfall-lip detection, and skipping it keeps this to 8 bytes/cell.
    glm::vec2 flowAt(int x, int y, int z) const;
    // Smoothing factor per step: how fast the reported flow follows the instantaneous transfers.
    // ⚑GROUND: 0.25 gives a ~4-step (0.2 s at 20 Hz) response — fast enough that opening a dam
    // reads as immediate, slow enough that the CA's per-tick lumpiness doesn't strobe the shading.
    static constexpr float FLOW_EMA = 0.25f;

    // ── MOMENTUM (WaterSystemV3 Phase 4) ──────────────────────────────────────────────────────
    // Without this the CA is pure diffusion: a spill spreads outward like paint, equally in every
    // direction, because the horizontal rule only ever moves mass toward the local average. Real
    // water carries inertia — it keeps going the way it was already going, and rounds corners
    // instead of fanning out. Momentum biases WHICH neighbour receives a cell's outflow by how well
    // that direction aligns with the flow the cell already has (the Phase 3 proxy, reused — this
    // costs no new storage).
    //
    // IT REMAINS STRICTLY DISSIPATIVE. The per-neighbour factor is clamped below 0.5, and moving
    // (a-b)*c with c < 0.5 leaves the difference (a-b)*(1-2c) with the SAME SIGN and smaller
    // magnitude. So water can never overshoot the local average, never pump uphill, and the field
    // still converges monotonically — mass conservation and settling are untouched by construction,
    // not just by tuning.
    void  setMomentum(float strength);   // 0 disables; 1 = shipped default
    float momentum() const { return m_momentum; }
    // ⚑GROUND: at full strength an aligned neighbour's leveling factor goes 0.25 -> 0.45 and an
    // opposed one 0.25 -> 0.05, i.e. moving water is ~9x more likely to continue than to reverse.
    // Chosen as the largest bias that stays clear of the 0.5 overshoot bound.
    static constexpr float MOMENTUM_GAIN = 0.8f;
    // Flow magnitude (mass/step) treated as "fully moving" for the bias ramp. Matches the renderer's
    // FLOW_FULL so the shading and the physics agree about what counts as a vigorous current.
    static constexpr float FLOW_FULL = 0.15f;

// Compile-time A/B switch for the flow proxy's own cost — set to 0 to compile the flow work out
// entirely (the field then reads zero everywhere and the FlowProxy* tests fail by design).
// MEASURED with this switch (Release, `--gtest_filter=Water*`, 64x32x64 worst-case active sweep,
// 3 runs each): OFF ~175 us/step, ON ~222 us/step => the proxy costs about +27% of the ACTIVE
// step. It costs nothing when the field is settled (0.002 us/step either way), which is the
// common case in a live world. Kept as a switch so the next perf pass can re-measure cheaply.
#define PHYXEL_WATER_FLOW_ENABLED 1

    // Raw data access for the GPU backend (upload masks / read back mass). The mass
    // vector is mutable so the GPU stepper can write the readback into it.
    std::vector<float>&         mass()              { return m_mass; }
    const std::vector<float>&   mass()        const { return m_mass; }
    const std::vector<uint8_t>& solidMask()   const { return m_solid; }
    const std::vector<float>&   sourceMask()  const { return m_source; }
    const std::vector<uint8_t>& channelMask() const { return m_channel; }
    bool                        evaporationOn() const { return m_evaporate; }
    int                         cellCount()   const { return m_sx * m_sy * m_sz; }

    // ── MIN_HOLD donor gate (small-scale plan Phase 1) ────────────────────────────────────────
    // Minimum working mass a cell must exceed to make HORIZONTAL transfers. Below it, water
    // rests: films, puddles and fractional creek pins stop creeping sideways forever (the
    // missing term that made the 496cdc10 creek fix sheet across a hillside, and made every
    // placed puddle spread thin and evaporate). Gravity and upward pressure are NOT gated —
    // thin water still falls and columns still equalize vertically. Deep bodies are unaffected:
    // once leveling raises every cell above the hold, equalization proceeds exactly as before
    // (a body only stalls when its mass over the reachable area is at or below the hold).
    // NOTE v1 simplification: the hold tests MASS, not effective depth `fill·(1−floor)` —
    // a floored cell holds slightly more than a bare one before it flows.
    // Kept ABOVE EVAP_THRESHOLD so a resting puddle survives evaporation (films below the
    // threshold still dry; the pool itself persists).
    static constexpr float MIN_HOLD_DEFAULT = 0.3f;
    void  setMinHold(float depth);   // wakes the field (a lower hold can release held water)
    float minHold() const { return m_minHold; }

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

    // ── EDGE OUTFLOW (water-as-terrain-stage P4) ──────────────────────────────────────────────
    // When enabled, the outermost XZ ring of columns stops being an invisible WALL for unpinned
    // water: a ring cell holding more than the hold — or a thin layer stacked on deeper water —
    // bleeds up to OUTFLOW_RATE mass per step out of the window, accumulated per column for the
    // owner (WaterManager) to drain into its world-keyed persistence bank. The world continues
    // beyond the window; water reaching the frontier keeps going instead of piling into a wall.
    // Pinned and channel cells are EXEMPT: pins are infinite reservoirs (outflowing a lake edge
    // would mint mass forever), and channel ribbons are held in place by design. Draining a ring
    // column marks it dirty, so leveling keeps feeding the frontier until the body thins to the
    // hold — which is how a spill actually leaves the window. Off by default (legacy walls).
    void setEdgeOutflow(bool on);
    bool edgeOutflow() const { return m_edgeOutflow; }
    // Per-column edge-bleed exemption (tangible-water Phase C): a FINITE body's columns are real
    // conserved mass owned by its body record — the frontier bleed must not siphon a pond that
    // happens to straddle the window ring. Re-derived by the owner on every rebuild.
    void setColumnNoBleed(int lx, int lz, bool noBleed) {
        if (lx >= 0 && lx < m_sx && lz >= 0 && lz < m_sz)
            m_colNoBleed[colIdx(lx, lz)] = noBleed ? 1 : 0;
    }
    // Move the accumulated per-column outflow to the caller: `sink(lx, lz, mass)` per non-empty
    // ring column, cleared afterward. Returns the total mass drained.
    float drainEdgeOutflow(const std::function<void(int lx, int lz, float mass)>& sink);
    static constexpr float OUTFLOW_RATE = 0.25f;   // per column per step; gradual, like a real spill edge

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
    std::vector<float>   m_floor;       // per-cell sub-voxel floor fraction 0..1 (Phase 4B)
    std::vector<glm::vec2> m_flow;      // per-cell EMA-smoothed horizontal flow proxy (see flowAt)
    std::vector<glm::vec2> m_flowAccum; // scratch: this sweep's raw net transfer per cell
    int                  m_colsProcessed = 0; // |P| of the last executed sweep (observability)
    bool                 m_edgeOutflow = false;            // P4: ring columns bleed instead of walling
    std::vector<float>   m_edgeOutflowAccum;               // per-column outflow since the last drain
    std::vector<uint8_t> m_colNoBleed;                     // Phase C: finite-body bleed exemption
    void runEdgeOutflow();                                 // one bleed pass over the ring (in step())
    bool                 m_hasSources = false;
    bool                 m_evaporate  = false;
    float                m_momentum   = 1.0f;  // Phase 4 inertia strength (0 = pure diffusion)
    float                m_minHold    = MIN_HOLD_DEFAULT; // horizontal-flow depth gate (Phase 1)
    bool                 m_settled    = false; // last step moved no mass → skip until disturbed
    unsigned long long   m_sweepsRun  = 0;     // steps that ran the full sweep (observability)
};

} // namespace Core
} // namespace Phyxel
