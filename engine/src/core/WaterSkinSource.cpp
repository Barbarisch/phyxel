#include "core/WaterSkinSource.h"

#include "core/WorldGenerator.h"
#include "core/WorldForgePlan.h"
#include "core/WorldRecipe.h"
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

uint64_t generatedWaterCacheKey(const WorldGenerator& gen) {
    constexpr uint32_t kSummaryFormat = 1;   // WaterSkin's cache file layout (bump with it)
    const std::string s = gen.makeRecipe().toJson() + "|water " + std::to_string(kGeneratedWaterVersion) + "|format " + std::to_string(kSummaryFormat);
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

}  // namespace Phyxel
