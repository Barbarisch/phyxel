#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Phyxel {

class HydrologyMap;
class WaterBodyIndex;

// ── M2c: the basin rim guarantee (docs/WorldForgeV2Plan.md, M2 engineering design) ─────────────
//
// The hydrology bake floods a grid of cell-centre samples, so a basin's spill is the lowest SAMPLED
// saddle; the per-voxel terrain between samples can dip below it, and the water runtime then levels
// water through the notch ("rim leak": a dry rim column whose voxel surface sits below the level
// it borders — WaterManager::validateTable). Measured on the 32 u bake, 2026-10-21: 418 of 543 rim
// columns of the first above-sea lake of the baseline world leaked, worst 19 u (m2b_l4_run.txt).
//
// The guarantee, by construction: a basin's level is the minimum of its coarse spill and its REAL
// rim — the lowest voxel surface over the ring of dry columns bordering its wet cells — and the
// wet cell set is re-derived at the lowered level (cells whose centre sample is now above it dry
// out, which moves the ring inward), iterated to a fixed point. The level only falls; a notch
// lower than the basin floor dries the basin, which is what a basin that is not a basin at voxel
// resolution deserves. A carved outlet channel therefore becomes the lake's level: the outlet bed
// IS the spill.
//
// Cost and sharing: the ring is sampled with the generator's own column sampler (the one truth
// for the voxel surface — 1.9 µs a column), LAZILY per basin on first query and memoized here.
// This object is shared by every copy of a generator (and by the process-wide hydro cache), holds
// no pointer to any generator, and is thread-safe: a basin is computed once under the mutex and
// published through an acquire/release flag. Sea-level bodies and the ocean are never corrected
// (their level is the sea's). While a correction runs, the sampler's own water-level reads (road
// grading, lakebed material) see the RAW bake through the thread-local guard — the voxel surface
// a notch column reports does not depend on it except at a road grading onto a lake, a stated
// second-order case.
class BasinRimCorrector {
public:
    using SurfaceYFn = std::function<int(int worldX, int worldZ)>;   // the generator's surfaceY

    BasinRimCorrector(std::shared_ptr<const HydrologyMap> hydro,
                      std::shared_ptr<const WaterBodyIndex> bodies, float seaLevel);

    // Corrected level of a basin (its coarse spill if nothing is lower). Computes once, lazily.
    float level(int32_t bodyId, const SurfaceYFn& surfaceYAt);
    // Corrected level of the basin at a world column, or HydrologyMap::NO_WATER where the column's
    // cell is dry — including cells that dried under a lowered level. Raw where no basin applies.
    float waterLevelAt(float worldX, float worldZ, const SurfaceYFn& surfaceYAt);

    // True on the thread that is currently sampling a rim: readers return the raw bake.
    static bool inCorrection();

    struct Stats {
        int  bodiesCorrected = 0;   // computed (lazily) so far
        int  bodiesLowered   = 0;   // of those, level moved down
        int  bodiesDried     = 0;   // of those, no wet cell survived
        long ringColumns     = 0;   // voxel surfaces sampled in total
        int  maxIterations   = 0;   // longest fixed-point chain
        float maxDrop        = 0.0f; // largest level drop (u)
    };
    Stats stats() const;

    static constexpr int   kMaxIterations = 16;
    static constexpr float kRimMargin     = 0.9f;   // level = rim top voxel + this: floor(level) == rim surfaceY

private:
    struct State {
        std::atomic<bool> done{false};
        float level = 0.0f;
    };
    std::shared_ptr<const HydrologyMap>   m_hydro;
    std::shared_ptr<const WaterBodyIndex> m_bodies;
    float m_seaLevel;
    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<State>> m_state;
    Stats m_stats;
};

}  // namespace Phyxel
