// WaterCore 37 (plan A2e, 32.2 item 5) - a body reads the water run it is IN, not the column's top. The debris fallback (the table
// law, solver_integrate.comp) read one number per world column - the highest run's top - so a body in a DRY cave or room under a
// pond read as under the pond's water: a wood block there would rise through the air to the ceiling (read from the code - never run,
// owner 2026-10-25: nothing that floats up is ever run; live red with a STONE instead: a stone in a dry room
// under the Small bench pond fell 64 % slower than in the control room, a2e_red.json). The tile now carries the column's two
// highest runs (top, bottom) and the shader picks phxWaterRunSurface's run; WaterCoreManager::columnRuns feeds it.
#include <gtest/gtest.h>

#include "core/DebrisRuntime.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreManager.h"

#include <algorithm>
#include <map>
#include <vector>

using namespace Phyxel::Core::Water;
using Phyxel::DebrisRuntime;
namespace DS = Phyxel::DebrisShared;

namespace {

struct Record {   // the chunk spans, stood in for (WaterRecordSeamTest's)
    std::map<std::pair<int, int>, std::vector<ColumnRun>> cols;
    void install(WaterCoreManager& mgr) {
        mgr.setSpanIo([](const std::vector<ColumnRuns>&, float, float, long* u, long* c) { if (u) *u = 0; if (c) *c = 0; },
            [this](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                out.clear();
                for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                    const auto it = cols.find({x, z});
                    if (it == cols.end()) continue;
                    ColumnRuns c; c.x = x; c.z = z; c.runs = it->second; out.push_back(std::move(c));
                }
            });
        mgr.setSpanRevisionQuery([] { return uint64_t(1); });
    }
};
ColumnRun run(float b, float t) { ColumnRun r; r.bottomY = b; r.topY = t; r.mass = t - b; return r; }

// the editor's adapter (Application.cpp, setVolumeWater): columnRuns -> the tile's runs
DebrisRuntime::WaterRunsFn runsFrom(const WaterCoreManager& m) {
    return [&m](int wx, int wz, DebrisRuntime::WaterRuns& r) {
        WaterCoreManager::ColumnLevels lv;
        if (!m.columnRuns(wx, wz, lv)) return false;
        r = DebrisRuntime::WaterRuns{};
        r.top1 = lv.top[0]; r.bottom1 = lv.bottom[0]; r.flow = lv.flow;
        if (lv.n > 1) { r.top2 = lv.top[1]; r.bottom2 = lv.bottom[1]; }
        r.more = static_cast<uint32_t>(lv.more);
        return true;
    };
}

struct Tiles {   // one chunk column's tile behind a directory at min chunk (0, 0)
    std::vector<uint32_t> dir, cells;
    Tiles(const DebrisRuntime::WaterRunsFn& fn, int cx, int cz) {
        dir.assign(static_cast<size_t>(DS::WATER_DIR_CHUNKS) * DS::WATER_DIR_CHUNKS, DS::WATER_TILE_NONE);
        if (DebrisRuntime::buildWaterTile(cx, cz, fn, false, 0.0f, cells)) dir[static_cast<size_t>(cz * DS::WATER_DIR_CHUNKS + cx)] = 0;
    }
    float at(int x, int z, float bodyTop) const { return DebrisRuntime::sampleWaterTiles(x, z, glm::ivec2(0), dir, cells, false, 0.0f, nullptr, bodyTop); }
};

}  // namespace

// The chunk rule and the outcome pin together: over four chunk columns' seams (negative coordinates included), with stacked runs,
// touching runs (they merge) and holes, the top run IS columnWater's surface wherever that is wet, and dry where it is dry; the
// second run is the highest run separated from the top one by air.
TEST(WaterRunRead, TheTopRunIsColumnWatersSurface) {
    WaterCoreManager mgr([](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; });
    Record rec; rec.install(mgr);
    for (int z = -40; z < 40; ++z) for (int x = -40; x < 40; ++x) {
        const unsigned h = static_cast<unsigned>((x * 73856093) ^ (z * 19349663));
        if (h % 11 == 0) continue;                                            // a dry column
        const float lvl = 16.0f + 0.001f * static_cast<float>(h & 511);
        std::vector<ColumnRun> r;
        if (h % 7 == 0) r.push_back(run(8.0f, 9.5f));                         // a cave pool, an air gap above it
        if (h % 13 == 0) r.push_back(run(3.0f, 4.0f));                        // a third run lower still
        if (h % 5 == 0) { r.push_back(run(12.0f, 14.0f)); r.push_back(run(14.0f, lvl)); }   // two runs touching: ONE body of water
        else r.push_back(run(15.0f, lvl));
        std::sort(r.begin(), r.end(), [](const ColumnRun& a, const ColumnRun& b) { return a.bottomY < b.bottomY; });
        rec.cols[{x, z}] = r;
    }
    long wet = 0, stacked = 0, mismatched = 0;
    for (int z = -48; z < 48; ++z) for (int x = -48; x < 48; ++x) {
        float s = 0.0f; glm::vec2 fl;
        const bool w = mgr.columnWater(x, z, s, fl);
        WaterCoreManager::ColumnLevels lv;
        const bool wr = mgr.columnRuns(x, z, lv);
        if (w != wr || (w && lv.top[0] != s)) { ++mismatched; continue; }
        if (!w) continue;
        ++wet;
        const unsigned h = static_cast<unsigned>((x * 73856093) ^ (z * 19349663));
        const float bottom1 = (h % 5 == 0) ? 12.0f : 15.0f;                   // the touching runs merged
        EXPECT_FLOAT_EQ(lv.bottom[0], bottom1) << x << "," << z;
        const int below = (h % 7 == 0) + (h % 13 == 0);
        EXPECT_EQ(lv.n, 1 + std::min(below, 1)) << x << "," << z;
        if (h % 7 == 0) { ++stacked; EXPECT_FLOAT_EQ(lv.top[1], 9.5f); EXPECT_FLOAT_EQ(lv.bottom[1], 8.0f); }
        else if (h % 13 == 0) { EXPECT_FLOAT_EQ(lv.top[1], 4.0f); }
        EXPECT_EQ(lv.more, (h % 7 == 0 && h % 13 == 0) ? 1 : 0) << x << "," << z;
    }
    std::printf("  %ld wet columns (%ld with a cave pool), %ld mismatched with columnWater\n", wet, stacked, mismatched);
    EXPECT_EQ(mismatched, 0) << "columnRuns' top run is not columnWater's surface";
    EXPECT_GT(stacked, 50);
}

// The defect itself, through the tile and the shader's own selection rule (phxWaterRunSurface, its CPU mirror sampleWaterTiles):
// a pond over a dry room over a cave pool. RED with the selection disabled (top run always): the dry room read 16.5.
TEST(WaterRunRead, ADryRoomUnderAPondReadsDry) {
    WaterCoreManager mgr([](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; });
    Record rec; rec.install(mgr);
    // x 10..11: the pond (bed y 15) over a dry room (y 10..13) over rock; x 12..13: the same with a cave pool (y 6..7) under the
    // room's floor
    for (int z = 10; z < 14; ++z) for (int x = 10; x < 14; ++x)
        rec.cols[{x, z}] = x < 12 ? std::vector<ColumnRun>{run(15.0f, 16.5f)} : std::vector<ColumnRun>{run(6.0f, 7.0f), run(15.0f, 16.5f)};
    const Tiles t(runsFrom(mgr), 0, 0);
    ASSERT_FALSE(t.cells.empty());
    for (int x : {11, 13}) {
        EXPECT_FLOAT_EQ(t.at(x, 11, 16.2f), 16.5f) << x << ": a body in the pond";
        EXPECT_FLOAT_EQ(t.at(x, 11, 15.3f), 16.5f) << x << ": a body resting on the pond's bed";
        EXPECT_FLOAT_EQ(t.at(x, 11, 18.0f), 16.5f) << x << ": a body above the pond (falling in)";
        EXPECT_FLOAT_EQ(t.at(x, 11, 3.0e38f), 16.5f) << x << ": no body height - the column's top (sampleWaterTiles' default)";
    }
    EXPECT_EQ(t.at(11, 11, 12.9f), DS::WATER_DRY) << "a body in the DRY room under the pond read the pond's water";
    EXPECT_EQ(t.at(11, 11, 10.4f), DS::WATER_DRY) << "a body on the dry room's floor";
    // with a pool under the room: a body in the room is ABOVE that pool - it reads the pool's surface below it (no buoyancy until it
    // reaches it, as a body above a pond), never the pond's over the ceiling
    EXPECT_FLOAT_EQ(t.at(13, 11, 12.9f), 7.0f) << "a body in the room over the cave pool";
    EXPECT_FLOAT_EQ(t.at(13, 11, 6.8f), 7.0f) << "a body in the cave pool";
    EXPECT_EQ(t.at(13, 11, 5.5f), DS::WATER_DRY) << "under every run";
    EXPECT_EQ(t.at(20, 20, 16.0f), DS::WATER_DRY);
}

// The old water and the sea have no bottoms: one run reaching down, read at every height - their law exactly as before 37.
TEST(WaterRunRead, ASourceWithNoBottomReadsItsLevelAtEveryHeight) {
    const DebrisRuntime::WaterColumnFn flat = [](int, int, float& s, glm::vec2& f) { s = 16.5f; f = glm::vec2(0.25f, 0.0f); return true; };
    const Tiles t(DebrisRuntime::runsOf(flat), 0, 0);
    for (float top : {30.0f, 16.6f, 12.0f, -50.0f}) EXPECT_FLOAT_EQ(t.at(5, 5, top), 16.5f) << "body top " << top;
    glm::vec2 fl(0.0f);
    DebrisRuntime::sampleWaterTiles(5, 5, glm::ivec2(0), t.dir, t.cells, false, 0.0f, &fl, 12.0f);
    EXPECT_FLOAT_EQ(fl.x, 0.25f) << "the top run's flow";
}
