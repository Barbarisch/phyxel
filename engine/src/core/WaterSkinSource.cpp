#include "core/WaterSkinSource.h"

#include "core/WorldGenerator.h"
#include "core/WorldForgePlan.h"

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

}  // namespace Phyxel
