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
    int wokenHalfSecondAfterPush = 0;   // tiles woken in the 0.5 s after the push - the wake may not outrun the waves
    std::vector<double> keAfterPush;    // every 10 ticks after the push (diagnostic)
    double sentinelMax = 0.0;           // the ground sentinel's largest |f|/|u|/|v|/|w| after the push - must stay 0 (every recorded brick reads it)
    bool ok = false;
};

}  // namespace

class WaterRecordTilesTest : public VulkanTestFixture {
public:
    TileRun run(bool open, bool recording = true, bool seedSkipsFPrev = false) {
        WaterCoreGpu::s_seedSkipsFPrevForTest = seedSkipsFPrev;
        struct Restore { ~Restore() { WaterCoreGpu::s_seedSkipsFPrevForTest = false; } } restore;
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
        const long wokenBeforePush = r ? r->tilesWoken : 0;
        for (int k = 0; k < 110; ++k) {
            mgr.step(id, 1, dt, &rec); books();
            r = mgr.find(id);
            if (k == 29 && r) out.wokenHalfSecondAfterPush = static_cast<int>(r->tilesWoken - wokenBeforePush);
            if (r && (k % 10 == 9)) {
                out.keAfterPush.push_back(rec.kineticEnergy);
                WaterCoreManager::Census cs;
                if (mgr.census(id, cs)) for (int q = 0; q < 4; ++q) out.sentinelMax = std::max(out.sentinelMax, static_cast<double>(cs.sentinel[q]));
            }
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

// DIAGNOSTIC (run by name): the live bench's numbers in a test - a 46 x 28 m pond 1 m deep (6 x 4 tiles at the origin, walled by its
// box), droplets ON, ticks of 1/400 s (the bench steps one tick a frame at ~400 fps), a strength-4 push at (10, 20); kinetic energy
// and moving columns every 0.5 s for 20 s, recording ON and OFF. Live, recording ON took the energy UP 25x over 4-19 s after the
// push (OFF decays) and left ~1,400 columns "moving" at 8e-5 J. W1B_DIAG_DT overrides the tick (s); W1B_DIAG_NOMARGIN=1: recorded
// tiles are woken only by writes.
TEST_F(WaterRecordTilesTest, DISABLED_DiagEnergyAfterAPushOnTheBenchPond) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const float dt = std::getenv("W1B_DIAG_DT") ? static_cast<float>(std::atof(std::getenv("W1B_DIAG_DT"))) : 1.0f / 400.0f;
    for (bool rec : {false, true}) {
        auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
        WaterCoreManager mgr(state);
        std::string err;
        ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        mgr.setDroplets(true);
        FakeRecord record; record.install(mgr);
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(45, 1, 27), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_TRUE(id) << err;
        mgr.setAutoSleep(id, false);
        mgr.setPartialWake(id, true); mgr.setRecordTiles(id, rec);
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(45, 0, 27), 1.0f, nullptr);
        AvRecord r;
        const int perSecond = static_cast<int>(std::lround(1.0f / dt));
        // W1B_DIAG_REALTIME=1: the live path - realtime update() (async calls, the once-a-second mirror), not step()
        const bool live = std::getenv("W1B_DIAG_REALTIME") != nullptr;
        mgr.setRealtime(live);
        auto tick = [&]() { if (live) { mgr.update(dt); const AvRecord* a = mgr.find(id); if (a) r = *a; } else mgr.step(id, 1, dt, &r); };
        for (int k = 0; k < 4 * perSecond; ++k) tick();
        mgr.addImpulse(glm::vec3(10.0f, 0.9f, 20.0f), 0.5f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
        std::printf("  recording %s, dt %.4f, %s:\n", rec ? "ON " : "OFF", dt, live ? "realtime update" : "step");
        for (int k = 1; k <= 20 * perSecond; ++k) {
            tick();
            if (k % (perSecond / 2) == 0) {
                const AvRecord* a = mgr.find(id);
                std::printf("    %5.1fs KE %.2e moving %4d awake %4d in record %2d woken %2ld\n", k * dt, r.kineticEnergy, a ? a->movingColumns : -1, a ? a->awakeColumns : -1,
                            a ? a->recordedTiles : -1, a ? a->tilesWoken : -1L);
            }
        }
    }
}

TEST_F(WaterRecordTilesTest, FrozenTilesRecordAndWakeWithTheBooksExact) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const TileRun w = run(false);
    const TileRun o = run(true);
    const TileRun c = run(false, true, true);   // red control of the wake-speed bound: the seed leaves fPrev at 0 (the live cascade)
    if (std::getenv("W1B_BASELINE")) {   // diagnostic: the same pushes with recording off - its kinetic energy beside the walled run's
        const TileRun b = run(false, false);
        std::printf("  KE after the push every 10 ticks, recording ON :"); for (double k : w.keAfterPush) std::printf(" %.2e", k); std::printf("\n");
        std::printf("  KE after the push every 10 ticks, recording OFF:"); for (double k : b.keAfterPush) std::printf(" %.2e", k); std::printf("\n");
    }
    ASSERT_TRUE(w.ok && o.ok && c.ok);
    std::printf("  SEED fPrev 0 (red control): woken 0.5 s after the push %d, woken in all %d\n", c.wokenHalfSecondAfterPush, c.woken);
    EXPECT_GT(c.wokenHalfSecondAfterPush, 3) << "the control must cascade - or the bound does not see a wake that outruns the waves";
    std::printf("  WALL: placed %.4f m3, residue at settle %.2e; after settle %d tiles recorded, cells %.4f m3, record %.4f m3; woken %d; level worst %.2f mm; neighbour cells %.4f m3; "
                "touch woke %d, recorded again %d; woken 0.5 s after the push %d; recorded at end %d; books worst %.2e m3; record %.1f ms, wake %.1f ms max\n",
                w.placed, w.residueAtSettle, w.recordedAfterSettle, w.massAfterSettle, w.recordAfterSettle, w.woken, 1000.0f * w.wokenLevelWorst, w.neighbourCellsM3,
                w.wokenByTouch, w.recordedAfterTouch, w.wokenHalfSecondAfterPush, w.recordedAtEnd, w.booksWorst, w.recordMsMax, w.wakeMsMax);
    std::printf("  OPEN (red control): woken %d; neighbour cells %.4f m3; books worst %.2e m3\n", o.woken, o.neighbourCellsM3, o.booksWorst);
    EXPECT_EQ(w.recordedAfterSettle, 8) << "every tile of a still pond records";
    EXPECT_LT(w.massAfterSettle, 1e-3) << "m3 left in cells once every tile is recorded";
    EXPECT_NEAR(w.recordAfterSettle + w.residueAtSettle, w.placed, 5e-4) << "the record holds what was placed (less the films the residue sweep dropped, counted)";
    EXPECT_LT(w.booksWorst, 5e-4) << "cells + record = placed, every tick";
    EXPECT_EQ(w.wokenByTouch, 1) << "the touch seeded tile (0, 0) from the record - and only it (a seed is at rest, not motion)";
    EXPECT_LT(w.wokenLevelWorst, 1e-3f) << "the woken tile at the still level";
    EXPECT_EQ(w.recordedAfterTouch, 8) << "the woken tile, at rest, records again";
    EXPECT_GE(w.woken, 3) << "the touch, the push, and the push's waves woke the tile ahead of them (wake ahead)";
    // live, a seeded tile read |df| = 1 every call (its fPrev was never written: frozen bricks are not stepped) - moving, so its
    // margin woke its neighbours, and theirs: all 24 tiles of the bench pond in under 2 s. In 0.5 s the splash reaches the pushed
    // tile's neighbours at most - tile (0, 0) is a corner of the 4 x 2 tiles, so the pushed tile and its 3 neighbours: 4 (first
    // run: 4; the control: all 8)
    EXPECT_LE(w.wokenHalfSecondAfterPush, 4) << "tiles woken 0.5 s after the push - the wake ran past the pushed tile's neighbours";
    EXPECT_LT(w.neighbourCellsM3, 1e-6) << "a recorded tile's cells hold no water (the wall)";
    std::printf("  ground sentinel after the push: max %.3e (must be 0)\n", w.sentinelMax);
    EXPECT_EQ(w.sentinelMax, 0.0) << "something wrote into the ground sentinel - every recorded brick reads that value";
    EXPECT_GT(o.neighbourCellsM3, 1e-3) << "the red control: with the wall open the woken water pours into the recorded tile";
}
