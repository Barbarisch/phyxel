#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
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
    void setEvaporation(bool enabled) { m_evaporate = enabled; }
    bool evaporation() const { return m_evaporate; }

    static constexpr float EVAP_THRESHOLD = 0.1f;  // below this depth a cell evaporates
    static constexpr float EVAP_RATE      = 0.01f; // mass lost per step by a thin cell

    // Advance the simulation one tick. `flowSide` damps horizontal equalization
    // (0..1); lower = calmer/slower leveling.
    void step(float flowSide = 1.0f);

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

    int m_sx, m_sy, m_sz;
    std::vector<float>   m_mass;
    std::vector<uint8_t> m_solid;
    std::vector<float>   m_next;   // scratch buffer reused across steps
    std::vector<float>   m_source;  // per-cell pinned mass; < 0 means "not a source"
    std::vector<uint8_t> m_channel; // per-cell: 1 = channel (no evaporation)
    bool                 m_hasSources = false;
    bool                 m_evaporate  = false;
};

} // namespace Core
} // namespace Phyxel
