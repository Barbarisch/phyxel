// DebrisWaterTilesTest - DebrisInteractionPlan Phase 6c, the design check's chunk-independence test
// (FloraMarginTest shape): GPU debris reads water through per-chunk-column TILES; looking a column
// up through the directory + tiles (sampleWaterTiles - the CPU mirror of solver_integrate's
// waterSurface(), same phxWaterDirIndex / phxWaterCellIndex) must equal the source evaluated
// column-by-column, ACROSS chunk seams and at negative coordinates. Chunking must not change the
// water a body floats on.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/DebrisRuntime.h"
#include "solver_shared.h"

using Phyxel::DebrisRuntime;
namespace DS = Phyxel::DebrisShared;

namespace {
// A synthetic water world: a sloping surface with dry holes and a current, defined for every
// world column (negative ones included).
bool source(int x, int z, float& surf, glm::vec2& flow) {
    if (((x * 7 + z * 13) % 11 + 11) % 11 == 0) return false;   // dry islands
    surf = 12.0f + 0.03125f * static_cast<float>(x) - 0.0625f * static_cast<float>(z);
    flow = glm::vec2(0.25f * static_cast<float>(x % 5), -0.5f * static_cast<float>(z % 3));
    return true;
}

struct Window {
    glm::ivec2 minChunk;
    std::vector<uint32_t> dir, cells;
};

// Build the window the way DebrisRuntime::updateWater does: one tile per chunk column, the
// background ones omitted.
Window buildWindow(glm::ivec2 minChunk, int chunksX, int chunksZ, bool sea, float seaY,
                   const DebrisRuntime::WaterColumnFn& fn) {
    Window w;
    w.minChunk = minChunk;
    w.dir.assign(static_cast<size_t>(DS::WATER_DIR_CHUNKS) * DS::WATER_DIR_CHUNKS, DS::WATER_TILE_NONE);
    uint32_t tiles = 0;
    for (int cz = 0; cz < chunksZ; ++cz)
        for (int cx = 0; cx < chunksX; ++cx) {
            std::vector<uint32_t> t;
            if (!DebrisRuntime::buildWaterTile(minChunk.x + cx, minChunk.y + cz, fn, sea, seaY, t)) continue;
            w.dir[cz * DS::WATER_DIR_CHUNKS + cx] = tiles++;
            w.cells.insert(w.cells.end(), t.begin(), t.end());
        }
    return w;
}
}  // namespace

TEST(DebrisWaterTiles, TiledLookupEqualsTheColumnSourceAcrossSeamsAndNegativeCoords) {
    // Window starting at chunk (-3, -2): columns x -96.., z -64..; build 6 x 5 chunk columns.
    const Window w = buildWindow({-3, -2}, 6, 5, false, 0.0f, source);
    int checked = 0, wet = 0;
    for (int x = -96; x < 96; ++x)
        for (int z = -64; z < 96; ++z) {
            float s = 0.0f; glm::vec2 f(0.0f);
            const bool isWet = source(x, z, s, f);
            glm::vec2 got(0.0f);
            const float surf = DebrisRuntime::sampleWaterTiles(x, z, w.minChunk, w.dir, w.cells, false, 0.0f, &got);
            if (isWet) {
                ASSERT_EQ(surf, s) << "column (" << x << "," << z << ")";
                ASSERT_NEAR(got.x, f.x, 2e-3f) << "flow x at (" << x << "," << z << ")";
                ASSERT_NEAR(got.y, f.y, 2e-3f) << "flow z at (" << x << "," << z << ")";
                ++wet;
            } else {
                ASSERT_EQ(surf, DS::WATER_DRY) << "dry column (" << x << "," << z << ") read wet";
            }
            ++checked;
        }
    EXPECT_GT(wet, checked / 2);
    // Both sides of every seam were exercised: x = -1|0, x = 31|32, z = -1|0, z = -33|-32.
}

TEST(DebrisWaterTiles, BackgroundTilesAreOmittedAndReadBackAsTheBackground) {
    // A flat implicit sea everywhere: every tile is pure background - no tile, sea level read back.
    const float seaY = 16.0f;
    auto flatSea = [seaY](int, int, float& s, glm::vec2& f) { s = seaY; f = glm::vec2(0.0f); return true; };
    const Window sea = buildWindow({0, 0}, 4, 4, true, seaY, flatSea);
    EXPECT_TRUE(sea.cells.empty()) << "a pure-sea window needs no tiles";
    EXPECT_EQ(DebrisRuntime::sampleWaterTiles(5, 7, sea.minChunk, sea.dir, sea.cells, true, seaY), seaY);

    // A dry island in the sea DIFFERS from the background, so its tile is kept and reads dry.
    auto island = [seaY](int x, int z, float& s, glm::vec2& f) {
        f = glm::vec2(0.0f);
        if (x >= 10 && x < 14 && z >= 10 && z < 14) return false;
        s = seaY; return true;
    };
    const Window isl = buildWindow({0, 0}, 4, 4, true, seaY, island);
    EXPECT_EQ(isl.cells.size(), static_cast<size_t>(DS::WATER_TILE_CELLS) * DS::WATER_TILE_CELLS * 2u)
        << "exactly one tile (the island's chunk column)";
    EXPECT_EQ(DebrisRuntime::sampleWaterTiles(11, 12, isl.minChunk, isl.dir, isl.cells, true, seaY), DS::WATER_DRY);
    EXPECT_EQ(DebrisRuntime::sampleWaterTiles(9, 12, isl.minChunk, isl.dir, isl.cells, true, seaY), seaY);

    // Outside the window: the background (dry here), never garbage.
    const Window w = buildWindow({0, 0}, 1, 1, false, 0.0f, source);
    EXPECT_EQ(DebrisRuntime::sampleWaterTiles(-1, 0, w.minChunk, w.dir, w.cells, false, 0.0f), DS::WATER_DRY);
    EXPECT_EQ(DebrisRuntime::sampleWaterTiles(32 * DS::WATER_DIR_CHUNKS, 0, w.minChunk, w.dir, w.cells, false, 0.0f),
              DS::WATER_DRY);
}
