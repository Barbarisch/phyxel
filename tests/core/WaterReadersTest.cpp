// WaterCore 52 W4.1: the readers of water (a CPU rigid body's buoyancy, the camera's underwater test) read a column's RUNS from the
// new water (WaterCoreManager::columnRuns: the awake volumes, else the record) by the wade rule (A2e): a point reads the run it is
// in, never a pond above it. These are the pure rules; the wiring is proven live (tools/water_w4_readers_live.py).
#include <gtest/gtest.h>
#include "core/water/WaterReaders.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreManager.h"

#include <map>
#include <vector>

using namespace Phyxel::Core::Water;

namespace {
// the chunk spans, stood in for (WaterRunReadTest's)
struct Record {
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
ColumnRun span(float b, float t) { ColumnRun r; r.bottomY = b; r.topY = t; r.mass = t - b; return r; }
// the host's reader (Application::readerColumn, the old water off): columnRuns -> the runs
ColumnRunsFn readerOf(const WaterCoreManager& m) {
    return [&m](int wx, int wz, WaterRunsAt& r) {
        WaterCoreManager::ColumnLevels L;
        if (!m.columnRuns(wx, wz, L) || L.n <= 0) return false;
        r.n = std::min(L.n, 2);
        for (int k = 0; k < 2; ++k) { r.top[k] = L.top[k]; r.bottom[k] = L.bottom[k]; }
        r.flow = L.flow;
        return true;
    };
}
// the whole region at once: the same spans read straight from the map (no chunk column, no cache)
ColumnRunsFn wholeRegionOf(const std::map<std::pair<int, int>, std::vector<ColumnRun>>& cols) {
    return [&cols](int wx, int wz, WaterRunsAt& r) {
        const auto it = cols.find({wx, wz});
        if (it == cols.end() || it->second.empty()) return false;
        const ColumnRun& top = it->second.back();   // one run per column in this test
        r.n = 1; r.top[0] = top.topY; r.bottom[0] = top.bottomY;
        return true;
    };
}
// one column everywhere: a pond 20..21 over a cave pool 10..10.5 (runs highest first, as ColumnLevels)
ColumnRunsFn pondOverCave() {
    return [](int, int, WaterRunsAt& r) { r.n = 2; r.top[0] = 21.0f; r.bottom[0] = 20.0f; r.top[1] = 10.5f; r.bottom[1] = 10.0f; return true; };
}
ColumnRunsFn dry() { return [](int, int, WaterRunsAt&) { return false; }; }
}  // namespace

TEST(WaterReaders, APointReadsTheRunItIsIn) {
    const auto f = pondOverCave();
    WaterPointSample s = sampleWaterAt(f, glm::vec3(0.5f, 20.5f, 0.5f));
    EXPECT_TRUE(s.inWater); EXPECT_NEAR(s.surfaceY, 21.0f, 1e-6f); EXPECT_NEAR(s.depthBelow, 0.5f, 1e-6f);
    s = sampleWaterAt(f, glm::vec3(0.5f, 10.25f, 0.5f));
    EXPECT_TRUE(s.inWater); EXPECT_NEAR(s.surfaceY, 10.5f, 1e-6f) << "the cave pool, not the pond above it";
    EXPECT_FALSE(sampleWaterAt(f, glm::vec3(0.5f, 15.0f, 0.5f)).inWater) << "a dry cave under a pond reads dry";
    EXPECT_FALSE(sampleWaterAt(f, glm::vec3(0.5f, 22.0f, 0.5f)).inWater) << "above the pond";
    EXPECT_FALSE(sampleWaterAt(f, glm::vec3(0.5f, 5.0f, 0.5f)).inWater) << "under every run";
    EXPECT_FALSE(sampleWaterAt(dry(), glm::vec3(0.5f, 20.5f, 0.5f)).inWater) << "no water in the column";
}

TEST(WaterReaders, ABoxHalfUnderTheSurfaceIsHalfSubmerged) {
    const auto f = pondOverCave();
    EXPECT_NEAR(submergedFractionFromRuns(f, glm::vec3(0.0f, 20.5f, 0.0f), glm::vec3(1.0f, 21.5f, 1.0f)), 0.5f, 1e-5f);
    EXPECT_NEAR(submergedFractionFromRuns(f, glm::vec3(0.0f, 20.2f, 0.0f), glm::vec3(1.0f, 20.7f, 1.0f)), 1.0f, 1e-5f) << "all under";
    EXPECT_EQ(submergedFractionFromRuns(f, glm::vec3(0.0f, 21.5f, 0.0f), glm::vec3(1.0f, 22.5f, 1.0f)), 0.0f) << "above the water";
    EXPECT_EQ(submergedFractionFromRuns(f, glm::vec3(0.0f, 14.0f, 0.0f), glm::vec3(1.0f, 15.0f, 1.0f)), 0.0f) << "in the dry cave";
    EXPECT_NEAR(submergedFractionFromRuns(f, glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(1.0f, 11.0f, 1.0f)), 0.5f, 1e-5f) << "the cave pool";
    EXPECT_EQ(submergedFractionFromRuns(dry(), glm::vec3(0.0f, 20.5f, 0.0f), glm::vec3(1.0f, 21.5f, 1.0f)), 0.0f);
}

TEST(WaterReaders, ABoxOverAShoreAveragesItsFivePoints) {
    // the old rule kept (WaterManager::submergedFraction): the centre and four corners at the box floor + 0.05, each its own column
    const ColumnRunsFn shore = [](int wx, int, WaterRunsAt& r) {
        if (wx >= 0) return false;   // x >= 0 is the bank
        r.n = 1; r.top[0] = 21.0f; r.bottom[0] = 19.0f; return true;
    };
    // the box x -0.5..0.5: two corners (x -0.5) in water, the centre (x 0) and two corners (x 0.5) on the bank -> 2 of 5 at 0.5
    EXPECT_NEAR(submergedFractionFromRuns(shore, glm::vec3(-0.5f, 20.5f, 0.0f), glm::vec3(0.5f, 21.5f, 1.0f)), 0.2f, 1e-5f);
}

// Chunks must not be visible (FeatureDesignKeys): the record is read per 32 x 32 chunk column (recordCol, cached), so a box across a
// chunk column's border - at +-32 and 0, negative coordinates included - reads exactly what the same spans read whole-region give.
// The lake's level varies by column so a border read from the wrong column shows. Control: the whole-region map with one side's
// spans removed must differ (the comparison can see a missing chunk column).
TEST(WaterReaders, ABoxAcrossAChunkBorderReadsTheSameWater) {
    WaterCoreManager mgr([](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; });
    Record rec; rec.install(mgr);
    for (int z = -40; z < 40; ++z) for (int x = -40; x < 40; ++x)
        rec.cols[{x, z}] = {span(15.0f, 16.2f + 0.01f * static_cast<float>((x * 7 + z * 13) & 31))};
    const ColumnRunsFn chunked = readerOf(mgr), whole = wholeRegionOf(rec.cols);
    const float xs[3] = {-32.0f, 0.0f, 32.0f}, zs[3] = {-32.0f, 0.0f, 32.0f};
    int boxes = 0;
    for (float bx : xs) for (float bz : zs) for (float off : {0.0f, 0.37f}) {
        const glm::vec3 mn(bx - 0.6f + off, 15.9f, bz - 0.6f + off), mx(bx + 0.6f + off, 16.9f, bz + 0.6f + off);
        const float a = submergedFractionFromRuns(chunked, mn, mx), b = submergedFractionFromRuns(whole, mn, mx);
        EXPECT_EQ(a, b) << "box at " << bx << "," << bz << " +" << off;
        EXPECT_GT(a, 0.0f) << "the box is in the lake: a 0 would make the comparison empty";
        ++boxes;
    }
    EXPECT_EQ(boxes, 18);
    // control: the east side of x = 32 missing from the whole-region read -> the border box differs
    auto cut = rec.cols;
    for (auto it = cut.begin(); it != cut.end();) it = it->first.first >= 32 ? cut.erase(it) : std::next(it);
    const ColumnRunsFn wholeCut = wholeRegionOf(cut);
    const glm::vec3 mn(31.4f, 15.9f, -0.6f), mx(32.6f, 16.9f, 0.6f);
    EXPECT_NE(submergedFractionFromRuns(chunked, mn, mx), submergedFractionFromRuns(wholeCut, mn, mx));
}

TEST(WaterReaders, TheCameraDepthIsTheRunItIsIn) {
    const auto f = pondOverCave();
    EXPECT_NEAR(cameraDepthFromRuns(f, glm::vec3(0.5f, 20.0f + 0.25f, 0.5f)), 0.75f, 1e-6f);
    EXPECT_EQ(cameraDepthFromRuns(f, glm::vec3(0.5f, 15.0f, 0.5f)), 0.0f) << "a dry cave under a pond is not underwater";
    EXPECT_EQ(cameraDepthFromRuns(f, glm::vec3(0.5f, 30.0f, 0.5f)), 0.0f);
}
