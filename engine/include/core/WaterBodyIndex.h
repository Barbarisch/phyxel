#pragma once

#include "core/HydrologyMap.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Phyxel {

// ── Water BODY identity over the hydrology bake (tangible-water Phase A) ──────────────────────
//
// The bake (HydrologyMap) answers "how high is the water in this column"; this answers "WHICH
// body is that, and what kind". Connected-component labeling over the wet bake cells — two wet
// 4-neighbors belong to the same body iff their levels match (Priority-Flood makes every basin
// flat, so level equality IS the basin test; two touching basins at different spills correctly
// split). Downstream consumers key everything on the body:
//   - finite vs infinite (scoopable ponds vs dig-flood lakes/ocean) — the class;
//   - per-body level deltas in the persistent water store (one float per body, because a settled
//     body's surface is flat — the sparse representation that makes scooping a 30k-column pond
//     one record instead of 30k);
//   - wave energy proportional to body size (body-aware look).
// Pure + deterministic; built once beside the bake and memoized with it (worker-copy-safe).
class WaterBodyIndex {
public:
    enum class Class : uint8_t {
        Ocean,  // touches the bake boundary at ~sea level — always infinite
        Lake,   // inland, area >= kInfiniteMinCells — infinite reservoir
        Pond,   // inland, smaller — FINITE: scoopable, level drops and persists
    };

    struct Body {
        int32_t    id = -1;
        Class      cls = Class::Pond;
        int        areaCells = 0;
        float      level = 0.0f;        // flat surface world Y (the basin's spill)
        float      volumeEst = 0.0f;    // Σ (level − terrain) · cellSize² over the body's cells
        glm::ivec2 bboxMin{0}, bboxMax{0};  // inclusive, in CELL coordinates
    };

    // Bodies at/above this many bake cells are infinite (LAKE). One bake cell is 128 u, so 4
    // cells ≈ a 250 m+ body — the user's "large enough to be an infinite source" line.
    static constexpr int kInfiniteMinCells = 4;
    // Two wet neighbors are the same body iff |levelA − levelB| <= this (basins are flat).
    static constexpr float kLevelEps = 1e-3f;
    // A boundary-touching body is the OCEAN iff its level is within this of the bake sea level.
    static constexpr float kOceanLevelEps = 1.0f;

    WaterBodyIndex() = default;
    // heightAt: the SAME coarse height function the bake flooded (needed for volumeEst — the
    // bake does not retain it, which is why this is built alongside the bake, not later).
    WaterBodyIndex(const HydrologyMap& hydro, const HydrologyMap::HeightFunc& heightAt);

    // Body id at a world column; -1 = dry or outside the baked region.
    int32_t bodyIdAt(float worldX, float worldZ) const;
    // Body record at a world column; nullptr = dry/outside.
    const Body* bodyAt(float worldX, float worldZ) const;
    const Body* body(int32_t id) const {
        return (id >= 0 && static_cast<size_t>(id) < m_bodies.size()) ? &m_bodies[id] : nullptr;
    }
    const std::vector<Body>& bodies() const { return m_bodies; }

private:
    float m_originX = 0.0f, m_originZ = 0.0f, m_cellSize = 1.0f;
    int m_cellsX = 0, m_cellsZ = 0;
    std::vector<int32_t> m_cellBody;  // per cell (row-major z*cellsX+x); -1 = dry
    std::vector<Body>    m_bodies;
};

}  // namespace Phyxel
