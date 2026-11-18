// WaterCore §55.4 (W6.2a): the shore band's follow search (ShoreReach.h nearestShorePoint) is a pure function of world position and
// the record - the design check's chunk-independence test: the nearest shore point for 40 viewers is the SAME with the record read
// whole or chunk by chunk (runs split at the y = 0 and y = 32 chunk borders) and fed shuffled. The record: a sea along a wavy
// coastline with a LAKE behind the beach (water that is not the sea: never a shore point, never "dry"). Control: with the chunk
// holding a viewer's point missing, the answer changes (the comparison can see a change).
#include <gtest/gtest.h>
#include "core/water/ShoreReach.h"
#include "core/water/WaterCoreManager.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <map>
#include <memory>
#include <random>

using namespace Phyxel::Core::Water;

namespace {
float coast(int x) { return 20.0f + 9.0f * std::sin(0.08f * static_cast<float>(x)); }
bool inLake(int x, int z) { const float dx = static_cast<float>(x) - 10.0f, dz = static_cast<float>(z) - 52.0f; return dx * dx + dz * dz < 64.0f; }
// the sea: water [bed, 33.5] where z < coast(x), deeper offshore (the run crosses the y = 32 chunk border: the chunk reader splits it);
// the lake: water [26, 31] in a disc behind the beach
std::vector<ColumnRuns> wholeRecord(int x0, int x1, int z0, int z1) {
    std::vector<ColumnRuns> out;
    for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
        const float c = coast(x), zc = static_cast<float>(z) + 0.5f;
        ColumnRuns col; col.x = x; col.z = z; ColumnRun r;
        if (zc < c) { r.bottomY = 33.5f - std::min(0.25f * (c - zc), 12.0f) - 0.5f; r.topY = 33.5f; }
        else if (inLake(x, z)) { r.bottomY = 26.0f; r.topY = 31.0f; }
        else continue;
        r.mass = r.topY - r.bottomY; col.runs.push_back(r); out.push_back(col);
    }
    return out;
}
std::vector<ColumnRuns> chunkedRecord(const std::vector<ColumnRuns>& whole, int skipChunkX = INT_MIN, int skipChunkZ = INT_MIN) {
    std::vector<ColumnRuns> out;
    for (const ColumnRuns& c : whole) {
        if (riverFloorDiv(c.x, 32) == skipChunkX && riverFloorDiv(c.z, 32) == skipChunkZ) continue;
        for (const ColumnRun& r : c.runs)
            for (int cy = riverFloorDiv(static_cast<int>(std::floor(r.bottomY)), 32); cy * 32 < r.topY; ++cy) {
                ColumnRuns p; p.x = c.x; p.z = c.z;
                ColumnRun q; q.bottomY = std::max(r.bottomY, cy * 32.0f); q.topY = std::min(r.topY, cy * 32.0f + 32.0f);
                if (q.topY <= q.bottomY) continue;
                q.mass = q.topY - q.bottomY; p.runs.push_back(q); out.push_back(p);
            }
    }
    return out;
}
RecordColumnFn lookup(const std::vector<ColumnRuns>& rec) {
    auto col = std::make_shared<std::map<std::pair<int, int>, std::pair<float, float>>>(joinColumnRuns(rec, -1e9f, 1e9f));
    return RecordColumnFn([col](int wx, int wz, float& t, float& b) {
        const auto it = col->find({wx, wz});
        if (it == col->end()) return false;
        t = it->second.first; b = it->second.second; return true;
    });
}
// the body rule: the lake's water is not the sea (in the engine: bodyAt -> the body table's class)
bool isSea(int x, int z, float) { return !inLake(x, z); }
bool eq(const ShorePoint& a, const ShorePoint& b) { return a.found == b.found && (!a.found || (a.wx == b.wx && a.wz == b.wz && a.top == b.top && a.bottom == b.bottom)); }
}  // namespace

TEST(ShoreReachTest, TheNearestShorePointIsTheSameHoweverTheRecordIsRead) {
    const auto whole = wholeRecord(-80, 80, -40, 80);
    const RecordColumnFn wholeFn = lookup(whole);
    auto chunked = chunkedRecord(whole);
    std::mt19937 rng(13);
    std::shuffle(chunked.begin(), chunked.end(), rng);
    const RecordColumnFn chunkFn = lookup(chunked);
    int found = 0, differ = 0, notShore = 0;
    ShorePoint probe;
    for (int k = 0; k < 40; ++k) {
        const glm::vec2 v(-60.0f + 3.1f * static_cast<float>(k), -10.0f + 5.3f * static_cast<float>(k % 13));
        const ShorePoint a = nearestShorePoint(v, 24.0f, wholeFn, isSea), b = nearestShorePoint(v, 24.0f, chunkFn, isSea);
        if (!eq(a, b)) ++differ;
        if (!a.found) continue;
        ++found; if (!probe.found) probe = a;
        EXPECT_EQ(a.wx % 2, 0); EXPECT_EQ(a.wz % 2, 0); EXPECT_LE(a.distM, 24.0f);
        // what it found IS a shore column: sea water, a dry column within 4 m, never the lake
        float t, bo; bool dryNear = false;
        for (int dz = -4; dz <= 4 && !dryNear; ++dz) for (int dx = -4; dx <= 4; ++dx) if (!wholeFn(a.wx + dx, a.wz + dz, t, bo)) { dryNear = true; break; }
        if (!wholeFn(a.wx, a.wz, t, bo) || inLake(a.wx, a.wz) || !dryNear) ++notShore;
    }
    // the lake alone (a viewer at its middle, radius inside the lake + its shore): no sea there - none
    const ShorePoint lake = nearestShorePoint(glm::vec2(10.5f, 52.5f), 6.0f, wholeFn, isSea);
    // deep offshore (z -40, the coast >= 11 m away... the radius 6 m sees only open sea): none
    const ShorePoint open = nearestShorePoint(glm::vec2(0.5f, -30.5f), 6.0f, wholeFn, isSea);
    // the control: the chunk holding the first found point removed - the same viewer reads another point (or none)
    ShorePoint ref, ctl;
    if (probe.found) {
        const glm::vec2 pv(static_cast<float>(probe.wx) + 0.5f, static_cast<float>(probe.wz) + 0.5f);
        ref = nearestShorePoint(pv, 24.0f, wholeFn, isSea);
        ctl = nearestShorePoint(pv, 24.0f, lookup(chunkedRecord(whole, riverFloorDiv(probe.wx, 32), riverFloorDiv(probe.wz, 32))), isSea);
    }
    std::printf("  40 viewers: %d found a shore point (%d not a shore column), %d differ read chunked + shuffled; control (the point's chunk missing): (%d, %d) -> %s (%d, %d); lake: %s; open sea: %s\n",
                found, notShore, differ, ref.wx, ref.wz, ctl.found ? "found" : "none", ctl.wx, ctl.wz, lake.found ? "FOUND" : "none", open.found ? "FOUND" : "none");
    EXPECT_GT(found, 20);
    EXPECT_EQ(notShore, 0);
    EXPECT_EQ(differ, 0);
    EXPECT_TRUE(ref.found);
    EXPECT_EQ(ref.distM, 0.0f) << "a viewer on a shore lattice column reads that column";
    EXPECT_FALSE(eq(ctl, ref)) << "the control: a record missing the point's chunk must change the answer";
    EXPECT_FALSE(lake.found) << "a lake is not the sea";
    EXPECT_FALSE(open.found) << "open sea with no dry column near is not a shore";
}

// 55.4 (W6.2a): the shore band's follow is ON by default (it does nothing in a world with no ocean body) - a pinned contract:
// flipping it updates this in the same commit
TEST(ShoreReachTest, ShoreFollowIsOnByDefault) {
    EXPECT_TRUE(WaterCoreManager::ShoreFollow{}.on);
    WaterCoreManager mgr{[](const glm::ivec3&) { return 0; }};
    EXPECT_TRUE(mgr.shoreFollow().on);
    mgr.followShore(glm::vec3(0.0f), 0.0);
    EXPECT_FALSE(mgr.shoreFollowState().seaPresent) << "no ocean body: nothing followed";
    EXPECT_EQ(mgr.shoreFollowState().id, 0);
}
