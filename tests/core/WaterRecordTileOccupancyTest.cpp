// WaterCore §57.7 - a record tile's occupancy is read a cube at a time (docs/WaterCore.md 57.7). CPU only: a manager with a span
// reader, no device.
//
// 57.6 found a 250 ms frame each time a lake's patch goes back to the record; 57.7 split it by the sleep.* laps (live, 2 / 2 runs,
// no other phyxel process): 180 ms of it is buildRecordTilesNow - the ~36 tiles (8 m) over the 40 x 40 m patch, built at once. A
// tile's scratch grid read its occupancy from the world ONE MICRO CELL AT A TIME (72 x 72 x ~36 = ~187,000 calls a tile) - the
// volume's own occupancy refresh has asked a 1 m cube once since 51.3 (uniform cubes - air, solid, not resident - are most of any
// tile) and went cell by cell only through a mixed cube; the record tile never got that path.
// Prediction, written before the fix: a 40 x 40 m lake 2 m deep over a flat bed with nine mixed cubes (a micro-stepped patch),
//   - the drawn record fields are IDENTICAL with the cube path and with every cell asked alone (s_cubeOccupancyOffForTest - the
//     reference, and the red control: today's cost);
//   - per tile built, the cube path asks <= 5 % of the per-cell calls (cube calls + the mixed cubes' cells: ~256 + 729 a mixed
//     cube, against ~187,000). RED before the fix: the shipped path IS the per-cell one (ratio 1.0).
#include <gtest/gtest.h>
#include "core/water/WaterCoreManager.h"
#include "core/water/WaterSurfaceMesh.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

using namespace Phyxel::Core::Water;

namespace {

constexpr int kPer = 9;   // micro cells per voxel

// the bed: solid below y 0; in x 10..12, z 10..12 the cube row y 0 is micro-stepped (solid below m.x % 9) - nine mixed cubes
int bed(const glm::ivec3& m) {
    if (m.y < 0) return 1;
    const int vx = m.x >= 0 ? m.x / kPer : -((-m.x + kPer - 1) / kPer), vz = m.z >= 0 ? m.z / kPer : -((-m.z + kPer - 1) / kPer);
    if (m.y < kPer && vx >= 10 && vx <= 12 && vz >= 10 && vz <= 12) return m.y < ((m.x % kPer + kPer) % kPer) ? 1 : 0;
    return 0;
}

struct Lake {
    void install(WaterCoreManager& mgr) {
        mgr.setSpanIo(
            [](const std::vector<ColumnRuns>&, float, float, long* unwritten, long* chunks) { if (unwritten) *unwritten = 0; if (chunks) *chunks = 0; },
            [](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                out.clear();
                for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                    if (x < 0 || x >= 40 || z < 0 || z >= 40) continue;
                    ColumnRun r; r.bottomY = std::max(0.0f, static_cast<float>(lo.y)); r.topY = std::min(2.0f, static_cast<float>(hi.y + 1));
                    if (r.topY <= r.bottomY) continue;
                    r.mass = static_cast<double>(r.topY - r.bottomY);
                    ColumnRuns c; c.x = x; c.z = z; c.runs.push_back(r);
                    out.push_back(std::move(c));
                }
            });
    }
};

struct Built { std::map<int, std::vector<SurfaceColumn>> fields; long cellCalls = 0, cubeCalls = 0; int tiles = 0; };

Built build(bool perCell) {
    const bool cubeOffBefore = WaterCoreManager::s_cubeOccupancyOffForTest, recBefore = WaterCoreManager::s_recordDraw.on;
    const float drawMBefore = WaterCoreManager::s_locality.drawM;
    WaterCoreManager::s_cubeOccupancyOffForTest = perCell;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_locality.drawM = 48.0f;
    Built b;
    std::atomic<long> cells{0}, cubes{0};
    {
        WaterCoreManager mgr([&cells](const glm::ivec3& m) { ++cells; return bed(m); });
        mgr.setCubeStateQuery([&cubes](const glm::ivec3& c) {
            ++cubes;
            int first = -1;
            for (int z = 0; z < kPer; ++z) for (int y = 0; y < kPer; ++y) for (int x = 0; x < kPer; ++x) {
                const int s = bed(c * kPer + glm::ivec3(x, y, z));
                if (first < 0) first = s; else if (s != first) return 3;
            }
            return first;
        });
        Lake lake; lake.install(mgr);
        mgr.setViewPoint(glm::vec3(20.0f, 6.0f, 20.0f));
        for (int i = 0; i < 400; ++i) {
            const auto& fs = mgr.surfaceFields();
            b.tiles += mgr.recordDrawStats().builtLastCall;
            if (mgr.recordDrawStats().pending == 0 && i > 0) {
                for (const WaterSurfaceField* f : fs)
                    if (f->source & WaterCoreManager::kRecordDrawSourceBit) b.fields[f->source] = f->cols;
                break;
            }
        }
    }
    b.cellCalls = cells; b.cubeCalls = cubes;
    WaterCoreManager::s_cubeOccupancyOffForTest = cubeOffBefore; WaterCoreManager::s_recordDraw.on = recBefore; WaterCoreManager::s_locality.drawM = drawMBefore;
    return b;
}

}  // namespace

TEST(WaterRecordTileOccupancyTest, ARecordTileAsksTheWorldACubeAtATime) {
    const Built ref = build(true);    // every cell asked alone: the reference, and today's cost
    const Built cub = build(false);   // shipped
    const double refPer = ref.tiles ? double(ref.cellCalls + ref.cubeCalls) / ref.tiles : 0.0;
    const double cubPer = cub.tiles ? double(cub.cellCalls + cub.cubeCalls) / cub.tiles : 0.0;
    std::printf("[57.7] per cell: %d tiles built, %ld cell + %ld cube calls (%.0f a tile) | shipped: %d tiles, %ld cell + %ld cube calls (%.0f a tile, %.3fx)\n",
                ref.tiles, ref.cellCalls, ref.cubeCalls, refPer, cub.tiles, cub.cellCalls, cub.cubeCalls, cubPer, refPer > 0 ? cubPer / refPer : 0.0);
    ASSERT_GT(ref.tiles, 20) << "CONTROL: the lake's record tiles were built";
    ASSERT_EQ(ref.fields.size(), cub.fields.size());
    ASSERT_GT(ref.fields.size(), 20u);
    long differ = 0, cols = 0, wet = 0;
    for (const auto& [src, rc] : ref.fields) {
        const auto it = cub.fields.find(src);
        ASSERT_TRUE(it != cub.fields.end()) << "a tile drawn by one path only";
        ASSERT_EQ(rc.size(), it->second.size());
        for (size_t i = 0; i < rc.size(); ++i) {
            ++cols; if (rc[i].runs > 0.5f) ++wet;
            if (std::memcmp(&rc[i], &it->second[i], sizeof(SurfaceColumn)) != 0) ++differ;
        }
    }
    std::printf("[57.7] %ld columns compared (%ld wet), %ld differ\n", cols, wet, differ);
    EXPECT_GT(wet, cols / 4) << "CONTROL: the tiles drew the lake";
    EXPECT_EQ(differ, 0) << "the cube path drew a different surface";
    EXPECT_LE(cubPer, 0.05 * refPer) << "a record tile still asks the world cell by cell";
}
