// WaterCore §27 W1b - frozen tiles sleep into the record and wake from it (docs/WaterCore.md 27.10). Needs a Vulkan device: the
// fixture GTEST_SKIPs without one.
//
// A 32 x 16 m pond 1 m deep (4 x 2 tiles of 8 m, world-aligned at the origin) on a GPU volume with the partial wake, recording on
// (a tile records after 0.5 s frozen). The record is an in-memory stand-in for the chunk spans (the same SpanWriter / SpanReader
// the engine installs). Predictions written before the first run:
//   - placed still, every tile records: 8 tiles in the record, the volume's cells empty, the record holding the pond (512 m^3);
//   - THE BOOKS: the volume's water + the record's = what was placed, at every tick, within 5e-4 m^3 (float spans: 1e-6 relative);
//   - a touch in tile (0, 0) seeds it - only it - from the record: its surface at the still level (1.000 m) within 1 mm, the
//     record debited by what came back, the books still exact; at rest it records again within 2 s;
//   - a push near tile (1, 0) wakes the tiles ahead of its waves (wake ahead); no recorded tile's cells ever hold water.
// Red control: the recorded columns left OPEN (WaterCoreGpu::s_recordedOpenForTest - the halo stores them as air, not frozen):
// the woken tile's water pours into its recorded neighbour, so cells there hold water the record also holds. With the wall: 0.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreGpu.h"
#include "core/water/WaterSurfaceMesh.h"
#include "core/water/WaterCoreManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

using namespace Phyxel::Core::Water;
using Phyxel::Testing::VulkanTestFixture;

namespace {

// the chunk spans, stood in for: per world voxel column, its runs (world Y)
struct FakeRecord {
    std::map<std::pair<int, int>, std::vector<ColumnRun>> cols;
    double total() const {
        double m = 0.0;
        for (const auto& [k, v] : cols) for (const auto& r : v) m += static_cast<double>(r.topY - r.bottomY);
        return m;
    }
    void install(WaterCoreManager& mgr) {
        mgr.setSpanIo(
            [this](const std::vector<ColumnRuns>& runs, float yLo, float yHi, long* unwritten, long* chunks) {
                for (const ColumnRuns& c : runs) {
                    if (c.held) continue;
                    auto& v = cols[{c.x, c.z}];
                    v.erase(std::remove_if(v.begin(), v.end(), [&](const ColumnRun& r) { return r.topY > yLo && r.bottomY < yHi; }), v.end());
                    for (const ColumnRun& r : c.runs) v.push_back(r);
                }
                if (unwritten) *unwritten = 0;
                if (chunks) *chunks = 1;
            },
            [this](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                out.clear();
                const float yLo = static_cast<float>(lo.y), yHi = static_cast<float>(hi.y + 1);
                for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                    const auto it = cols.find({x, z});
                    if (it == cols.end()) continue;
                    ColumnRuns c; c.x = x; c.z = z;
                    for (ColumnRun r : it->second) {
                        r.bottomY = std::max(r.bottomY, yLo); r.topY = std::min(r.topY, yHi);
                        if (r.topY <= r.bottomY) continue;
                        r.mass = static_cast<double>(r.topY - r.bottomY);
                        c.runs.push_back(r);
                    }
                    if (!c.runs.empty()) out.push_back(std::move(c));
                }
            });
    }
};

struct TileRun {
    int recordedAfterSettle = 0, woken = 0, recordedAtEnd = 0;
    double massAfterSettle = 0.0, recordAfterSettle = 0.0;
    double booksWorst = 0.0;          // max |cells + record - placed| over every tick (m^3)
    double neighbourCellsM3 = 0.0;    // water in the cells of tile (1, 0) while it is recorded (m^3) - must be 0 with the wall
    float wokenLevelWorst = 0.0f;     // max |top - 1.000| over tile (0, 0)'s woken columns, 30 ticks after the wake (m)
    double recordMsMax = 0.0, wakeMsMax = 0.0, placed = 0.0, residueAtSettle = 0.0;
    int recordedAfterTouch = 0, wokenByTouch = 0;
    bool ok = false;
};

}  // namespace

class WaterRecordTilesTest : public VulkanTestFixture {
public:
    TileRun run(bool open, bool recording = true) {
        TileRun out;
        auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
        WaterCoreManager mgr(state);
        std::string err;
        if (!mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) { ADD_FAILURE() << err; return out; }
        mgr.setDroplets(false);
        FakeRecord record; record.install(mgr);
        const float afterBefore = WaterCoreManager::s_recordAfterS;
        WaterCoreManager::s_recordAfterS = 0.5f;
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(31, 1, 15), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        if (!id) { ADD_FAILURE() << err; return out; }
        mgr.setAutoSleep(id, false);
        if (!mgr.setPartialWake(id, true) || !mgr.setRecordTiles(id, recording)) { ADD_FAILURE() << "partial wake / recording refused"; return out; }
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(31, 0, 15), 1.0f, nullptr);
        const double placed = mgr.totalMass();   // what the cells hold once placed (float fills: 511.9993, not 512)
        out.placed = placed;
        AvRecord rec;
        const float dt = 1.0f / 60.0f;
        auto books = [&]() {
            const AvRecord* r = mgr.find(id);
            const double cells = mgr.totalMass();
            const double b = std::abs(cells + (r ? r->residueDropped : 0.0) + record.total() - placed);
            out.booksWorst = std::max(out.booksWorst, b);
            if (r) { out.recordMsMax = std::max(out.recordMsMax, r->lastRecordMs); out.wakeMsMax = std::max(out.wakeMsMax, r->lastWakeMs); }
        };
        for (int k = 0; k < 300; ++k) { mgr.step(id, 1, dt, &rec); books(); }
        const AvRecord* r = mgr.find(id);
        out.recordedAfterSettle = r ? r->recordedTiles : -1;
        out.massAfterSettle = mgr.totalMass();
        out.recordAfterSettle = record.total();
        out.residueAtSettle = r ? r->residueDropped : 0.0;
        // the wake: a touch at the middle of tile (0, 0) - into recorded water: the tile is seeded from the record, at rest
        mgr.addImpulse(glm::vec3(4.0f, 0.9f, 4.0f), 1.0f / 3.0f, 1e-4f, glm::vec3(0.0f, -1.0f, 0.0f));
        for (int k = 0; k < 10; ++k) { mgr.step(id, 1, dt, &rec); books(); }
        {   // the woken tile's surface, at the still level
            const WaterSurfaceField& f = *mgr.surfaceFields()[0];
            for (int z = 0; z < 72 && z < f.nz; ++z) for (int x = 0; x < 72 && x < f.nx; ++x) {
                const SurfaceColumn& c = f.at(x, z);
                const int runs = std::min(static_cast<int>(c.runs + 0.5f), kSurfaceMaxRuns);
                if (runs > 0) out.wokenLevelWorst = std::max(out.wokenLevelWorst, std::abs(c.top[runs - 1] - 1.0f));
            }
        }
        for (int k = 0; k < 120; ++k) { mgr.step(id, 1, dt, &rec); books(); }   // the woken tile at rest: frozen, it records again
        r = mgr.find(id);
        out.recordedAfterTouch = r ? r->recordedTiles : -1;
        out.wokenByTouch = r ? static_cast<int>(r->tilesWoken) : -1;
        // then a real push in tile (0, 0), recorded again by now: the push wakes it first (into cells - a push into the record's empty
        // cells was lost), its waves run at the recorded tile (1, 0) 3.5 m away - walled, the moving water's margin wakes it before
        // they arrive (wake ahead); open (the control), the water pours into it while it is still in the record
        WaterCoreGpu::s_recordedOpenForTest = open;
        mgr.addImpulse(glm::vec3(4.0f, 0.9f, 4.0f), 0.5f, 1.0f, glm::vec3(0.0f, -1.0f, 0.0f));
        for (int k = 0; k < 110; ++k) {
            mgr.step(id, 1, dt, &rec); books();
            r = mgr.find(id);
            // tile (1, 0) = voxels x 8-15, z 0-7: while it is in the record its cells must hold nothing
            const bool t10Recorded = mgr.columnWaterRecordedForTest(id, 8, 0);
            if (t10Recorded) {
                double sum = 0.0;
                for (int vz = 0; vz < 8; ++vz) for (int vx = 8; vx < 16; ++vx) { double d = 0.0; if (mgr.columnMassDepth(vx, vz, d)) sum += d; }
                out.neighbourCellsM3 = std::max(out.neighbourCellsM3, sum);
            }
        }
        WaterCoreGpu::s_recordedOpenForTest = false;
        r = mgr.find(id);
        out.woken = r ? static_cast<int>(r->tilesWoken) : -1;
        // 5 s more under the books: this walled box does not settle a push inside a test's time (recording OFF, 225-328 columns still
        // moving 15 s on - its walls reflect, 26.29), so how many tiles have recorded by then is reported, not gated
        for (int k = 0; k < 300; ++k) { mgr.step(id, 1, dt, &rec); books(); }
        r = mgr.find(id);
        out.recordedAtEnd = r ? r->recordedTiles : -1;
        if (r) std::printf("    (%s) at the end: awake %d, frozen %d, moving %d columns, asleep %d, bricks %d\n", !recording ? "no recording" : (open ? "open" : "wall"), r->awakeColumns, r->frozenColumns, r->movingColumns, int(r->asleep), r->gpuBricks);
        WaterCoreManager::s_recordAfterS = afterBefore;
        out.ok = true;
        return out;
    }
};

TEST_F(WaterRecordTilesTest, FrozenTilesRecordAndWakeWithTheBooksExact) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const TileRun w = run(false);
    const TileRun o = run(true);
    if (std::getenv("W1B_BASELINE")) run(false, false);   // diagnostic: the same pushes with recording off (prints its end state)
    ASSERT_TRUE(w.ok && o.ok);
    std::printf("  WALL: placed %.4f m3, residue at settle %.2e; after settle %d tiles recorded, cells %.4f m3, record %.4f m3; woken %d; level worst %.2f mm; neighbour cells %.4f m3; "
                "touch woke %d, recorded again %d; recorded at end %d; books worst %.2e m3; record %.1f ms, wake %.1f ms max\n",
                w.placed, w.residueAtSettle, w.recordedAfterSettle, w.massAfterSettle, w.recordAfterSettle, w.woken, 1000.0f * w.wokenLevelWorst, w.neighbourCellsM3,
                w.wokenByTouch, w.recordedAfterTouch, w.recordedAtEnd, w.booksWorst, w.recordMsMax, w.wakeMsMax);
    std::printf("  OPEN (red control): woken %d; neighbour cells %.4f m3; books worst %.2e m3\n", o.woken, o.neighbourCellsM3, o.booksWorst);
    EXPECT_EQ(w.recordedAfterSettle, 8) << "every tile of a still pond records";
    EXPECT_LT(w.massAfterSettle, 1e-3) << "m3 left in cells once every tile is recorded";
    EXPECT_NEAR(w.recordAfterSettle + w.residueAtSettle, w.placed, 5e-4) << "the record holds what was placed (less the films the residue sweep dropped, counted)";
    EXPECT_LT(w.booksWorst, 5e-4) << "cells + record = placed, every tick";
    EXPECT_EQ(w.wokenByTouch, 1) << "the touch seeded tile (0, 0) from the record - and only it (a seed is at rest, not motion)";
    EXPECT_LT(w.wokenLevelWorst, 1e-3f) << "the woken tile at the still level";
    EXPECT_EQ(w.recordedAfterTouch, 8) << "the woken tile, at rest, records again";
    EXPECT_GE(w.woken, 3) << "the touch, the push, and the push's waves woke the tile ahead of them (wake ahead)";
    EXPECT_LT(w.neighbourCellsM3, 1e-6) << "a recorded tile's cells hold no water (the wall)";
    EXPECT_GT(o.neighbourCellsM3, 1e-3) << "the red control: with the wall open the woken water pours into the recorded tile";
}
