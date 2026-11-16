#include "core/WaterSkinSource.h"

#include "core/WorldGenerator.h"
#include "core/WorldForgePlan.h"
#include "core/HydrologyMap.h"
#include "core/WorldRecipe.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace Phyxel {

void generatedWaterRuns(WorldGenerator& gen, const glm::ivec2& colChunk, std::vector<Core::Water::ColumnRuns>& out) {
    out.clear();
    if (!gen.hydrology()) return;   // generateChunk writes spans only with the bake
    const auto ws = gen.waterSpansForChunkColumn(colChunk);
    const WorldForgePlan* wf = gen.worldForge();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) {
            const size_t i = static_cast<size_t>(x) * 32 + z;   // ColumnSample order
            if (!ws->has[i]) continue;
            const int wx = colChunk.x * 32 + x, wz = colChunk.y * 32 + z;
            if (wf && wf->bridgeAt(static_cast<float>(wx), static_cast<float>(wz)).pier) continue;   // generateChunk's rule
            const float lo = static_cast<float>(ws->spans[i].bottomY), hi = ws->spans[i].topY;
            if (hi <= lo) continue;
            Core::Water::ColumnRuns c; c.x = wx; c.z = wz;
            Core::Water::ColumnRun r; r.bottomY = lo; r.topY = hi; r.mass = static_cast<double>(hi - lo);
            c.runs.push_back(r);
            out.push_back(std::move(c));
        }
}

namespace { constexpr float kSampledReach = 256.0f; }   // = WorldGenerator.cpp kWaterExtentSteps (the flood's step bound), in m

void sampledWaterRuns(WorldGenerator& gen, const glm::ivec2& colChunk, std::vector<Core::Water::ColumnRuns>& out) {
    out.clear();
    const HydrologyMap* hydro = gen.hydrology();
    if (!hydro) return;
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i) {
            const int x0 = colChunk.x * 32 + i * 4, z0 = colChunk.y * 32 + j * 4;
            const int sx = x0 + 2, sz = z0 + 2;
            const float px = static_cast<float>(sx) + 0.5f, pz = static_cast<float>(sz) + 0.5f;
            const WorldGenerator::ColumnSample cs = gen.sampleSurface(sx, sz);
            if (cs.riverOrder >= 3) continue;   // a carved channel holds its river span (not drawn here), never a lake's level
            const float bed = static_cast<float>(cs.surfaceY + 1);
            // generation's flood (floodBodiesOverGrid) spreads each wet bake cell's level up to kSampledReach columns through ground
            // below it, the nearest body winning, and stops where the depression-filled height is under the level (downhill of a
            // spill). Here: the level of the NEAREST wet bake cell within that reach whose level is above this ground - no path
            // check (a pit behind a ridge reads wet: test 2 counts it)
            const float cell = hydro->cellSize();
            const int ci = static_cast<int>(std::floor((px - hydro->originX()) / cell)), cj = static_cast<int>(std::floor((pz - hydro->originZ()) / cell));
            const int rc = static_cast<int>(std::ceil(kSampledReach / cell));
            float level = HydrologyMap::NO_WATER, best = 1e30f;
            for (int dj = -rc; dj <= rc; ++dj) for (int di = -rc; di <= rc; ++di) {
                const int x = ci + di, z = cj + dj;
                if (x < 0 || z < 0 || x >= hydro->cellsX() || z >= hydro->cellsZ()) continue;
                const float l = hydro->levels()[static_cast<size_t>(z) * hydro->cellsX() + x];
                if (l <= HydrologyMap::NO_WATER * 0.5f || l <= bed) continue;
                const float lx = hydro->originX() + x * cell, lz = hydro->originZ() + z * cell;
                const float qx = std::clamp(px, lx, lx + cell), qz = std::clamp(pz, lz, lz + cell);
                const float d2 = (qx - px) * (qx - px) + (qz - pz) * (qz - pz);
                if (d2 > kSampledReach * kSampledReach || d2 >= best) continue;
                best = d2; level = l;
            }
            if (level <= HydrologyMap::NO_WATER * 0.5f) continue;
            if (hydro->filledAt(px, pz) < level - 1e-3f) continue;   // downhill of the body's spill: it drains there
            for (int dz = 0; dz < 4; ++dz)
                for (int dx = 0; dx < 4; ++dx) {
                    Core::Water::ColumnRuns c; c.x = x0 + dx; c.z = z0 + dz;
                    Core::Water::ColumnRun r; r.bottomY = bed; r.topY = level; r.mass = static_cast<double>(level - bed);
                    c.runs.push_back(r);
                    out.push_back(std::move(c));
                }
        }
}

uint64_t generatedWaterCacheKey(const WorldGenerator& gen) {
    constexpr uint32_t kSummaryFormat = 1;   // WaterSkin's cache file layout (bump with it)
    const std::string s = gen.makeRecipe().toJson() + "|water " + std::to_string(kGeneratedWaterVersion) + "|format " + std::to_string(kSummaryFormat);
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

}  // namespace Phyxel
