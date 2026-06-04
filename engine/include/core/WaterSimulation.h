#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

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

    // Add (or, with a negative amount, remove) water at a cell; clamped to >= 0.
    void  addWater(int x, int y, int z, float amount);

    // Mark a cell as a source held at a fixed mass: it is re-pinned to `mass` at the
    // start of every step, acting as an infinite reservoir. This models the implicit
    // ocean's boundary (pin edge cells to sea-level mass) and authored springs/rivers.
    // Note: sources inject/remove mass, so total mass is not conserved while any exist.
    void  setSource(int x, int y, int z, float mass);
    void  clearSource(int x, int y, int z);

    float massAt(int x, int y, int z) const;
    float totalMass() const;
    float minMass() const; // for invariant checks (should never go negative)

    // Advance the simulation one tick. `flowSide` damps horizontal equalization
    // (0..1); lower = calmer/slower leveling.
    void step(float flowSide = 1.0f);

private:
    size_t idx(int x, int y, int z) const {
        return static_cast<size_t>(x) + static_cast<size_t>(m_sx) *
               (static_cast<size_t>(y) + static_cast<size_t>(m_sy) * static_cast<size_t>(z));
    }

    int m_sx, m_sy, m_sz;
    std::vector<float>   m_mass;
    std::vector<uint8_t> m_solid;
    std::vector<float>   m_next;   // scratch buffer reused across steps
    std::vector<float>   m_source; // per-cell pinned mass; < 0 means "not a source"
    bool                 m_hasSources = false;
};

} // namespace Core
} // namespace Phyxel
