#pragma once

#include <glm/glm.hpp>
#include <functional>

namespace Phyxel {

// ── Layer 0 of the two-tier terrain architecture (docs/TerrainGenerationV2.md) ──
//
// A low-resolution, whole-world model holding the fields that need large-scale /
// whole-world knowledge: base elevation + climate (and, in later phases, biome IDs +
// hydrology). Layer 1 (WorldGenerator's per-chunk detail) SAMPLES this coarse model and
// adds high-frequency detail on top. The coarse model is the substrate that a hand-drawn
// map and a noise world share — only *how it is filled* differs.
//
// P0 scope (this file): base height + three climate fields, produced by a deterministic,
// PURE source function evaluated at coarse cell corners and bilinearly interpolated
// between them. Sampling is C0-continuous across cell borders (adjacent cells share
// corner samples), order-independent, and thread-safe.
//
// NOT in P0: world.db persistence. The fields recompute deterministically from seed, so
// per the "store what you can't recompute" rule (docs/WorldRecipeAndFlora.md) there is
// nothing to persist yet. Persistence lands when a non-recomputable bake exists — an
// imported drawn map (P4) or the priority-flood hydrology (P2).
//
// THREADING CONTRACT: the async streaming worker deep-copies WorldGenerator (see
// ChunkManager::configureStreamingGeneration). The SourceFunc must therefore be a pure
// function of (cellWorldX, cellWorldZ) that captures only by value (seed, params) — never
// a pointer to a generator instance — so a copied model samples identically and safely.

struct CoarseSample {
    float baseHeight = 0.0f;       // low-frequency continental base elevation (world Y, pre-detail)
    float temperature = 0.5f;      // [0,1]
    float moisture = 0.5f;         // [0,1]
    float continentalness = 0.5f;  // [0,1] land-vs-ocean / large-scale elevation driver
};

class CoarseWorldModel {
public:
    // Evaluates the coarse fields at a coarse cell-CORNER world position. MUST be pure
    // (see threading contract above): deterministic, order-independent, no captured
    // generator instance.
    using SourceFunc = std::function<CoarseSample(float cellWorldX, float cellWorldZ)>;

    // cellSize = world units per coarse cell. 32 = one sample per chunk; 128 = one per
    // 4x4 chunks (cheaper/coarser base, more relief supplied by Layer-1 detail). Must be > 0.
    CoarseWorldModel(SourceFunc source, float cellSize);

    // Bilinearly-interpolated coarse fields at an arbitrary world column. Returns the
    // source value exactly at cell corners; continuous everywhere.
    CoarseSample sample(float worldX, float worldZ) const;

    float cellSize() const { return m_cellSize; }

private:
    SourceFunc m_source;
    float m_cellSize;
};

} // namespace Phyxel
