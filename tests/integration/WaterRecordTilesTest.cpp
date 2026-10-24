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
        // W1B_DIAG_X0: the box's first voxel along x (the bench pond starts at x 2 - its first tile is PARTIAL, 6 of 8 m)
        const int x0 = std::getenv("W1B_DIAG_X0") ? std::atoi(std::getenv("W1B_DIAG_X0")) : 0;
        const int id = mgr.create(glm::ivec3(x0, 0, 0), glm::ivec3(x0 + 45, 1, 27), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_TRUE(id) << err;
        mgr.setAutoSleep(id, false);
        mgr.setPartialWake(id, true); mgr.setRecordTiles(id, rec);
        mgr.placeBox(glm::ivec3(x0, 0, 0), glm::ivec3(x0 + 45, 0, 27), 1.0f, nullptr);
        AvRecord r;
        const int perSecond = static_cast<int>(std::lround(1.0f / dt));
        // W1B_DIAG_REALTIME=1: the live path - realtime update() (async calls, the once-a-second mirror), not step()
        const bool live = std::getenv("W1B_DIAG_REALTIME") != nullptr;
        mgr.setRealtime(live);
        // W1B_DIAG_JITTER=1: each tick's length drawn from [0.6, 1.4] x dt (a live frame's dt varies; deterministic sequence)
        const bool jitter = std::getenv("W1B_DIAG_JITTER") != nullptr;
        uint32_t lcg = 12345u;
        auto nextDt = [&]() { if (!jitter) return dt; lcg = lcg * 1664525u + 1013904223u; return dt * (0.6f + 0.8f * static_cast<float>(lcg >> 8) / 16777216.0f); };
        auto tick = [&]() { const float d = nextDt(); if (live) { mgr.update(d); const AvRecord* a = mgr.find(id); if (a) r = *a; } else mgr.step(id, 1, d, &r); };
        for (int k = 0; k < 4 * perSecond; ++k) tick();
        mgr.addImpulse(glm::vec3(static_cast<float>(x0) + 10.0f, 0.9f, 20.0f), 0.5f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
        std::printf("  recording %s, dt %.4f, %s, box x0 %d:\n", rec ? "ON " : "OFF", dt, live ? "realtime update" : "step", x0);
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

// 27.15/27.16: the DRAW hand-over between a volume and the record, both ways, every tick. The 32 x 16 m pond above, the record
// drawn (view at its middle); after every tick the drawn fields are asked, at each tile's centre and 0.3 m either side of every
// inner tile boundary (30 points), whether ANY field draws water there (a HOLE if none), and how many recorded-tile columns a
// volume's field draws as dry (a WALL: the live water beside them walls them off from its surface to the bed). Going in: placed,
// every tile records. Waking: a touch seeds tile (0, 0), then a push moves its water beside its recorded neighbours.
// Prediction: 0 hole ticks and 0 wall ticks once the pond is first drawn. Red controls (WaterCoreManager::s_handoverRevertForTest,
// one fix undone each): 1 = recordTile clears to dry (27.16), 2 = no full surface after a seed (A), 4 = no seed patch (B),
// 8 = recorded columns unmarked (C).
namespace {
struct Handover { int holeTicks = 0, wallTicks = 0, wallColsMax = 0, recordedIn = 0, woken = 0, firstDrawnTick = -1; bool ok = false; };
}
static Handover runHandover(VulkanTestFixture& fx, VkDevice device, VkPhysicalDevice pd, VkQueue queue, uint32_t qf, int revert, bool live) {
    (void)fx;
    Handover out;
    WaterCoreManager::s_handoverRevertForTest = revert;
    const float afterBefore = WaterCoreManager::s_recordAfterS;
    const auto drawBefore = WaterCoreManager::s_recordDraw;
    struct Restore { float a; WaterCoreManager::RecordDraw d; ~Restore() { WaterCoreManager::s_handoverRevertForTest = 0; WaterCoreManager::s_recordAfterS = a; WaterCoreManager::s_recordDraw = d; } } restore{afterBefore, drawBefore};
    WaterCoreManager::s_recordAfterS = 0.5f;
    WaterCoreManager::s_recordDraw.on = true; WaterCoreManager::s_recordDraw.radiusM = 48.0f;
    auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
    WaterCoreManager mgr(state);
    std::string err;
    if (!mgr.initGpu(device, pd, queue, qf, "shaders", &err)) { ADD_FAILURE() << err; return out; }
    mgr.setDroplets(false);
    mgr.setViewPoint(glm::vec3(16.0f, 2.0f, 8.0f));
    FakeRecord record; record.install(mgr);
    const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(31, 1, 15), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
    if (!id) { ADD_FAILURE() << err; return out; }
    mgr.setAutoSleep(id, false);
    if (!mgr.setPartialWake(id, true) || !mgr.setRecordTiles(id, true)) { ADD_FAILURE() << "partial wake / recording refused"; return out; }
    mgr.setRealtime(live);   // live: update() - async calls, a frame between a call's submission and its collection
    std::vector<glm::vec2> pts;
    for (int tz = 0; tz < 2; ++tz) for (int tx = 0; tx < 4; ++tx) pts.push_back({tx * 8 + 4.05f, tz * 8 + 4.05f});
    for (float ex : {8.0f, 16.0f, 24.0f}) for (float z : {4.05f, 12.05f}) for (float d : {-0.3f, 0.3f}) pts.push_back({ex + d, z});
    for (float x : {4.05f, 12.05f, 20.05f, 28.05f}) for (float d : {-0.3f, 0.3f}) pts.push_back({x, 8.0f + d});
    mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(31, 0, 15), 1.0f, nullptr);
    AvRecord rec;
    const float dt = live ? 1.0f / 240.0f : 1.0f / 60.0f;
    const int perS = live ? 240 : 60;
    int tick = 0;
    auto adv = [&]() { if (live) mgr.update(dt); else mgr.step(id, 1, dt, &rec); };
    auto check = [&]() {
        mgr.surfaceFields();
        bool hole = false;
        for (const glm::vec2& p : pts) { float top = 0.0f; bool r = false; if (!mgr.drawnAtWorld(p.x, p.y, top, r)) hole = true; }
        if (out.firstDrawnTick < 0) { if (!hole) out.firstDrawnTick = tick; else return; }
        const int walls = mgr.dryRecordedColumns();
        if (hole) ++out.holeTicks;
        if (walls > 0) { ++out.wallTicks; out.wallColsMax = std::max(out.wallColsMax, walls); }
    };
    for (; tick < 5 * perS; ++tick) { adv(); check(); }
    const AvRecord* r = mgr.find(id);
    out.recordedIn = r ? r->recordedTiles : -1;
    mgr.addImpulse(glm::vec3(4.0f, 0.9f, 4.0f), 1.0f / 3.0f, 1e-4f, glm::vec3(0.0f, -1.0f, 0.0f));   // a touch: tile (0, 0) seeded
    for (int k = 0; k < 2 * perS; ++k, ++tick) { adv(); check(); }
    mgr.addImpulse(glm::vec3(4.0f, 0.9f, 4.0f), 0.5f, 1.0f, glm::vec3(0.0f, -1.0f, 0.0f));         // a push beside recorded tiles
    for (int k = 0; k < 5 * perS; ++k, ++tick) { adv(); check(); }
    r = mgr.find(id);
    out.woken = r ? static_cast<int>(r->tilesWoken) : -1;
    out.ok = true;
    return out;
}

TEST_F(WaterRecordTilesTest, DrawHandoverHasNoHolesOrWallsEitherWay) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const char* names[] = {"shipped", "27.16 dry on record", "A no full surface", "B no seed patch", "C unmarked", "A + B"};
    const int bits[] = {0, 1, 2, 4, 8, 6};
    Handover h[6];
    const bool live = std::getenv("W1C_HANDOVER_STEP") == nullptr;   // default: the live path (update); =1: step()
    for (int i = 0; i < 6; ++i) {
        h[i] = runHandover(*this, device, physicalDevice, queue, queueFamilyIndex, bits[i], live);
        ASSERT_TRUE(h[i].ok);
        std::printf("  %s %-20s first drawn tick %d; in the record %d; woken %d; HOLE ticks %d; WALL ticks %d (worst %d columns)\n", live ? "update" : "step  ", names[i],
                    h[i].firstDrawnTick, h[i].recordedIn, h[i].woken, h[i].holeTicks, h[i].wallTicks, h[i].wallColsMax);
    }
    EXPECT_GE(h[0].firstDrawnTick, 0) << "the pond was never drawn whole";
    EXPECT_EQ(h[0].recordedIn, 8) << "going in: every tile records (the rig exercises the hand-over)";
    EXPECT_GE(h[0].woken, 2) << "waking: the touch and the push woke tiles (the rig exercises the hand-over)";
    EXPECT_EQ(h[0].holeTicks, 0) << "a tick where no field draws water at a point of the pond";
    EXPECT_EQ(h[0].wallTicks, 0) << "a tick where a volume draws recorded columns as dry (walls)";
    // the red controls - each must show its defect, or the probe does not see it (first run, update path: 1,378 wall ticks;
    // 1 hole tick; 1,680 wall ticks; A + B 665 hole ticks. A alone shows nothing: B's seed patch writes the seed into the field and a
    // partial read keeps it - A is the fix for the frozen columns, the patch for the one stale read; both stay)
    if (live) {
        EXPECT_GT(h[1].wallTicks, 0) << "red control 27.16 (recordTile clears to dry) went unseen";
        EXPECT_GT(h[3].holeTicks, 0) << "red control B (no seed patch) went unseen";
        EXPECT_GT(h[4].wallTicks, 0) << "red control C (recorded columns unmarked) went unseen";
        EXPECT_GT(h[5].holeTicks, 10 * std::max(1, h[3].holeTicks)) << "red control A + B (no full surface after a seed) went unseen";
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

// TallCellsRecordAndWakeWithTheBooksExact (27.22 TC4): a 32 x 16 m pond 2 m deep - tall cells ON (the default band, 0.5 m) - with
// recording: every tile records (the record reads each tall cell back as its column), a touch seeds tile (0, 0) back (its columns
// collapsed into their tall cells), a push wakes and the waves run; cells + record = placed every tick. RED CONTROL: the seed not
// collapsed (s_seedSkipsTallCollapseForTest) - its water under the floor lands in bricks that read the ground.
TEST_F(WaterRecordTilesTest, TallCellsRecordAndWakeWithTheBooksExact) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    for (int red = 0; red < 2; ++red) {
        WaterCoreGpu::s_seedSkipsTallCollapseForTest = red != 0;
        auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
        WaterCoreManager mgr(state);
        std::string err;
        ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        mgr.setDroplets(false);
        FakeRecord record; record.install(mgr);
        const float afterBefore = WaterCoreManager::s_recordAfterS;
        WaterCoreManager::s_recordAfterS = 0.5f;
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(31, 2, 15), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_TRUE(id) << err;
        mgr.setAutoSleep(id, false);
        mgr.setTallBand(id, 0.5f);
        ASSERT_TRUE(mgr.setPartialWake(id, true) && mgr.setRecordTiles(id, true));
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(31, 1, 15), 1.0f, nullptr);
        const double placed = mgr.totalMass();
        AvRecord rec;
        const float dt = 1.0f / 60.0f;
        double booksWorst = 0.0; bool tallSeen = false;
        auto books = [&]() {
            const AvRecord* r = mgr.find(id);
            const double b = std::abs(mgr.totalMass() + (r ? r->residueDropped : 0.0) + record.total() - placed);
            booksWorst = std::max(booksWorst, b);
            tallSeen = tallSeen || (r && r->tallActive);
        };
        for (int k = 0; k < 360; ++k) { mgr.step(id, 1, dt, &rec); books(); }
        const AvRecord* r = mgr.find(id);
        const int recordedAfterSettle = r ? r->recordedTiles : -1;
        const double recordAfterSettle = record.total();
        mgr.addImpulse(glm::vec3(4.0f, 1.9f, 4.0f), 1.0f / 3.0f, 1e-4f, glm::vec3(0.0f, -1.0f, 0.0f));   // the touch: tile (0, 0) seeded
        for (int k = 0; k < 60; ++k) { mgr.step(id, 1, dt, &rec); books(); }
        mgr.addImpulse(glm::vec3(4.0f, 1.9f, 4.0f), 0.5f, 1.0f, glm::vec3(0.0f, -1.0f, 0.0f));          // the push
        for (int k = 0; k < 300; ++k) { mgr.step(id, 1, dt, &rec); books(); }
        r = mgr.find(id);
        std::printf("  %s placed %.4f m3; tall seen %d; recorded after settle %d (record %.4f m3); woken %ld; books worst %.2e m3; seed unplaced %.4f m3; at the end tall %d (%s)\n",
                    red ? "RED (seed not collapsed):" : "tall + record:", placed, int(tallSeen), recordedAfterSettle, recordAfterSettle, r ? r->tilesWoken : -1L, booksWorst,
                    r ? r->seedUnplacedM3 : -1.0, r ? int(r->tallActive) : -1, r ? r->tallReason.c_str() : "");
        if (!red) {
            EXPECT_TRUE(tallSeen) << "the 2 m pond made tall cells";
            EXPECT_EQ(recordedAfterSettle, 8) << "every tile of the still pond records";
            EXPECT_NEAR(recordAfterSettle, placed, 1e-3) << "the record holds the tall cells' water too";
            EXPECT_LT(booksWorst, 1e-3) << "cells + record = placed, every tick";
        } else {
            EXPECT_GT(booksWorst, 1.0) << "the red control must lose the seeded water under the floor";
        }
        WaterCoreManager::s_recordAfterS = afterBefore;
    }
    WaterCoreGpu::s_seedSkipsTallCollapseForTest = false;
}

// RemovingAVolumeNeverLosesWater (27.25, red first): a 32 x 16 m pond with recording - at rest every tile records, a push wakes the
// tiles round it, and the volume is removed while some tiles are awake and some recorded. keep: the record then holds exactly the
// water placed (the awake tiles written back, the recorded ones untouched); discard: the box is dry and the reported discard is the
// water placed. RED CONTROL: the raw free (destroy) keeps the recorded tiles and deletes the awake ones' water.
TEST_F(WaterRecordTilesTest, RemovingAVolumeNeverLosesWater) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    struct Out { double placed = 0, record = 0, reported = 0; int recordedAtRemove = 0, awakeAtRemove = 0; bool ok = false; };
    auto run = [&](int mode) {   // 0 keep, 1 discard, 2 the raw destroy (red), 3 keep, then a volume seeded from the record discarded
        Out o;
        auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
        WaterCoreManager mgr(state);
        std::string err;
        if (!mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) { ADD_FAILURE() << err; return o; }
        mgr.setDroplets(false);
        FakeRecord record; record.install(mgr);
        const float afterBefore = WaterCoreManager::s_recordAfterS;
        WaterCoreManager::s_recordAfterS = 0.5f;
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(31, 1, 15), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        if (!id) { ADD_FAILURE() << err; WaterCoreManager::s_recordAfterS = afterBefore; return o; }
        mgr.setAutoSleep(id, false);
        EXPECT_TRUE(mgr.setPartialWake(id, true) && mgr.setRecordTiles(id, true));
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(31, 0, 15), 1.0f, nullptr);
        o.placed = mgr.totalMass();
        AvRecord rec;
        for (int k = 0; k < 300; ++k) mgr.step(id, 1, 1.0f / 60.0f, &rec);
        mgr.addImpulse(glm::vec3(4.0f, 0.9f, 4.0f), 0.5f, 3.0f, glm::vec3(0.0f, -1.0f, 0.0f));
        for (int k = 0; k < 20; ++k) mgr.step(id, 1, 1.0f / 60.0f, &rec);
        if (const AvRecord* r = mgr.find(id)) { o.recordedAtRemove = r->recordedTiles; o.awakeAtRemove = r->awakeColumns; }
        if (mode == 2) { o.ok = mgr.destroy(id); }
        else if (mode == 3) {   // the seeded volume: its spans stay under it while it lives - a discard counts them once
            o.ok = mgr.remove(id, false).ok;
            const int id2 = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(31, 1, 15), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
            mgr.seedFromSpans(id2);
            for (int k = 0; k < 5; ++k) mgr.step(id2, 1, 1.0f / 60.0f, &rec);
            const auto res = mgr.remove(id2, true); o.ok = o.ok && res.ok; o.reported = res.discardedM3;
        }
        else { const auto res = mgr.remove(id, mode == 1); o.ok = res.ok; o.reported = mode == 1 ? res.discardedM3 : res.keptM3; if (!res.ok) ADD_FAILURE() << res.error; }
        o.record = record.total();
        WaterCoreManager::s_recordAfterS = afterBefore;
        return o;
    };
    const Out keep = run(0), drop = run(1), raw = run(2), seeded = run(3);
    std::printf("  at the remove: %d tiles recorded, %d columns awake | keep: record %.4f of %.4f placed (reported kept %.4f) | discard: record %.4f, "
                "reported %.4f | RAW destroy: record %.4f of %.4f\n", keep.recordedAtRemove, keep.awakeAtRemove, keep.record, keep.placed, keep.reported,
                drop.record, drop.reported, raw.record, raw.placed);
    EXPECT_GT(keep.recordedAtRemove, 0) << "some tiles recorded";
    EXPECT_GT(keep.awakeAtRemove, 0) << "and some awake - the case that lost water";
    EXPECT_TRUE(keep.ok && drop.ok && raw.ok);
    EXPECT_NEAR(keep.record, keep.placed, 1e-2) << "keep: nothing lost";
    EXPECT_NEAR(keep.reported, keep.placed, 1e-2);
    EXPECT_NEAR(drop.record, 0.0, 1e-9) << "discard: the box is dry";
    EXPECT_NEAR(drop.reported, drop.placed, 1e-2) << "and the discard is counted";
    std::printf("  seeded then discarded: record %.4f, reported %.4f of %.4f\n", seeded.record, seeded.reported, seeded.placed);
    EXPECT_NEAR(seeded.record, 0.0, 1e-9);
    EXPECT_NEAR(seeded.reported, seeded.placed, 1e-2) << "a seeded volume's discard counts its water once";
    EXPECT_LT(raw.record, raw.placed - 1.0) << "RED CONTROL: the raw free deletes the awake tiles' water";
}

// TheWaterMovesTheSameAtAnyFrameRate (27.26, red first): live, a tick was a frame - and the solver's dissipation is per step, so a
// 30 fps frame let a pushed pond slosh far longer in SECONDS than 60 fps did (the 60 x 60 m lake never settled live, 27.24). With
// the fixed water tick (1/60 s from an accumulator) and the freeze / sleep tests in time, the same 40 x 24 m pond and stone driven
// through update() (the live path: realtime, async) at frames of 1/30, 1/60 and 1/144 s have the same awake patch after the same
// seconds. RED CONTROL: frames of 1/30 s without the fixed tick stay awake.
TEST_F(WaterRecordTilesTest, TheWaterMovesTheSameAtAnyFrameRate) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    struct Out { int peak = 0; std::vector<int> awakeAt; };   // awake columns at 6 / 12 / 18 / 24 s after the stone
    auto run = [&](float frame, bool fixedTick) {
        Out o;
        const float tick0 = WaterCoreManager::s_fixedTickS;
        WaterCoreManager::s_fixedTickS = fixedTick ? 1.0f / 60.0f : 0.0f;
        auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
        WaterCoreManager mgr(state);
        std::string err;
        if (!mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) { ADD_FAILURE() << err; return o; }
        mgr.setDroplets(false);
        mgr.setRealtime(true);
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(39, 1, 23), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        if (!id) { ADD_FAILURE() << err; return o; }
        mgr.setAutoSleep(id, false);
        EXPECT_TRUE(mgr.setPartialWake(id, true));
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(39, 0, 23), 1.0f, nullptr);
        double t = 0.0;
        while (t < 3.0) { mgr.update(frame); t += frame; }
        mgr.addImpulse(glm::vec3(20.0f, 0.9f, 12.0f), 0.5f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
        double next = 6.0; t = 0.0;
        while (t < 24.0 + 1e-6) {
            mgr.update(frame); t += frame;
            const AvRecord* r = mgr.find(id);
            if (!r) continue;
            o.peak = std::max(o.peak, r->awakeColumns);
            if (t >= next - 1e-6) { o.awakeAt.push_back(r->asleep ? 0 : r->awakeColumns); next += 6.0; }
        }
        WaterCoreManager::s_fixedTickS = tick0;
        return o;
    };
    const Out a = run(1.0f / 60.0f, true), b = run(1.0f / 30.0f, true), c = run(1.0f / 144.0f, true), red = run(1.0f / 30.0f, false);
    auto line = [](const char* name, const Out& o) {
        std::printf("  %-28s peak %4d | awake at 6 / 12 / 18 / 24 s:", name, o.peak);
        for (int v : o.awakeAt) std::printf(" %4d", v);
        std::printf("\n");
    };
    line("frames 1/60 s, fixed tick", a); line("frames 1/30 s, fixed tick", b); line("frames 1/144 s, fixed tick", c); line("frames 1/30 s, NO fixed tick", red);
    ASSERT_EQ(a.awakeAt.size(), 4u); ASSERT_EQ(b.awakeAt.size(), 4u); ASSERT_EQ(c.awakeAt.size(), 4u); ASSERT_EQ(red.awakeAt.size(), 4u);
    const int settled = std::max(50, a.awakeAt[3] * 2);
    EXPECT_LE(b.awakeAt[3], settled) << "a slow frame settles as 60 fps does";
    EXPECT_LE(c.awakeAt[3], settled) << "a fast frame too";
    EXPECT_GT(red.awakeAt[3], 3 * std::max(b.awakeAt[3], 20)) << "RED CONTROL: a tick per 1/30 s frame stays awake";
}

// DIAGNOSTIC (run by name): the dry squares of the owner's lake clip (27.26 ft_look) in a test. The bench lake - 60 x 60 m, 2 m deep,
// the box x 4-63, z 2-61 (partial tiles at its edges, as live), recording on (2 s), partial wake, the live path (update()) at a slow
// frame's dt; the view where the clip's camera was. After every LAKE_EVERY-th frame every 1 m of the lake is asked whether ANY drawn
// field holds water there (drawnAtWorld) - a HOLE if none - split by whether the column is in the record (the record's draw failed)
// or not (the volume's). Env: LAKE_DT (default 1/33 s), LAKE_TALL (band m, default the engine's), LAKE_RADIUS (record draw m,
// default 120), LAKE_SECONDS (after the push, default 30), LAKE_EVERY (frames, default 4).
TEST_F(WaterRecordTilesTest, DISABLED_DiagLakeHoles) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    auto env = [](const char* n, double d) { const char* v = std::getenv(n); return v ? std::atof(v) : d; };
    const float dt = static_cast<float>(env("LAKE_DT", 1.0 / 33.0));
    const float tall = static_cast<float>(env("LAKE_TALL", WaterCoreManager::s_tallBandDefault));
    const float radius = static_cast<float>(env("LAKE_RADIUS", 120.0));
    const int every = static_cast<int>(env("LAKE_EVERY", 4));
    const float seconds = static_cast<float>(env("LAKE_SECONDS", 30.0));
    const auto drawBefore = WaterCoreManager::s_recordDraw;
    WaterCoreManager::s_recordDraw.on = true; WaterCoreManager::s_recordDraw.radiusM = radius;
    auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
    WaterCoreManager mgr(state);
    std::string err;
    ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    mgr.setDroplets(false);
    FakeRecord record; record.install(mgr);
    mgr.setRealtime(true);
    mgr.setViewPoint(glm::vec3(34.0f, 57.0f, 95.0f));
    const int X0 = 4, Z0 = 2, L = 60;
    const int id = mgr.create(glm::ivec3(X0, 0, Z0), glm::ivec3(X0 + L - 1, 2, Z0 + L - 1), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
    ASSERT_TRUE(id) << err;
    mgr.setAutoSleep(id, false);
    ASSERT_TRUE(mgr.setPartialWake(id, true) && mgr.setRecordTiles(id, true) && mgr.setTallBand(id, tall));
    mgr.placeBox(glm::ivec3(X0, 0, Z0), glm::ivec3(X0 + L - 1, 1, Z0 + L - 1), 1.0f, nullptr);
    const int perS = static_cast<int>(std::lround(1.0f / dt));
    int holeFrames = 0, worst = 0, worstRec = 0, worstLive = 0, frames = 0, printed = 0;
    std::map<std::pair<int, int>, int> tileHoleFrames;
    auto probe = [&](float t) {
        mgr.surfaceFields();
        int rec = 0, live = 0;
        std::map<std::pair<int, int>, int> tiles;
        for (int z = Z0; z < Z0 + L; ++z) for (int x = X0; x < X0 + L; ++x) {
            float top = 0.0f; bool recorded = false;
            if (mgr.drawnAtWorld(x + 0.55f, z + 0.55f, top, recorded)) continue;
            (recorded ? rec : live)++;
            ++tiles[{x >> 3, z >> 3}];
        }
        ++frames;
        if (rec + live == 0) return;
        ++holeFrames;
        for (const auto& [k, n] : tiles) ++tileHoleFrames[k];
        if (rec + live > worst) { worst = rec + live; worstRec = rec; worstLive = live; }
        if (printed < 25) {
            ++printed;
            const AvRecord* r = mgr.find(id);
            std::printf("    t %5.2f s HOLE: %4d m^2 in the record, %4d m^2 not | tiles:", t, rec, live);
            for (const auto& [k, n] : tiles) std::printf(" (%d,%d)%s:%d", k.first, k.second, mgr.columnWaterRecordedForTest(id, k.first * 8 + 4, k.second * 8 + 4) ? "R" : "L", n);
            const auto& rs = mgr.recordDrawStats();
            std::printf(" | recorded %d woken %ld | draw tiles %d wet %d pending %d\n", r ? r->recordedTiles : -1, r ? r->tilesWoken : -1L, rs.tiles, rs.wetTiles, rs.pending);
        }
    };
    for (int k = 0; k < 10 * perS; ++k) { mgr.update(dt); if (k % every == 0) probe(k * dt); }
    const AvRecord* r = mgr.find(id);
    std::printf("  dt %.4f tall %.2f radius %.0f: before the push recorded %d tiles, tall %d; hole frames so far %d\n", dt, tall, radius, r ? r->recordedTiles : -1, r ? int(r->tallActive) : -1, holeFrames);
    mgr.addImpulse(glm::vec3(X0 + 30.0f, 1.9f, Z0 + 30.0f), 0.5f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
    for (int k = 0; k < static_cast<int>(seconds * perS); ++k) { mgr.update(dt); if (k % every == 0) probe(10.0f + k * dt); }
    r = mgr.find(id);
    std::printf("  RESULT dt %.4f tall %.2f radius %.0f: hole frames %d of %d probed; worst %d m^2 (%d recorded, %d live); tiles holed %zu; end recorded %d woken %ld awake %d\n",
                dt, tall, radius, holeFrames, frames, worst, worstRec, worstLive, tileHoleFrames.size(), r ? r->recordedTiles : -1, r ? r->tilesWoken : -1L, r ? r->awakeColumns : -1);
    WaterCoreManager::s_recordDraw = drawBefore;
}

// 29 S1 THE DEVICE MESH IS THE HOST MESH (red first). A 32 x 16 m box, water placed over x 0-23 m only (its edge against dry ground:
// side faces), a stone slab 1 m up over x 4-7 m / z 4-7 m with water under and over it (columns of two runs: their tops and walls are
// the extras list), recording on (tiles record at rest and wake when the push reaches them: the record's marks, the seeds), the
// partial wake (the surface pass covers the awake columns; the mesh pass them and a ring). Every 10 ticks: the device mesh, expanded
// through the vertex shader's own function, against buildWaterSurfaceMesh of the host's field, as multisets of quads.
// Prediction: 0 unmatched quads at every check, positions within 1e-4 m, the rest within 1e-3. RED CONTROL: the mesh pass without its
// ring (s_meshNoRingForTest) - a frozen column beside the awake region keeps stale corners: unmatched quads after the push.
static long tallBad = 0;   // 29 S2: checks where the device's tall-floor numbers differ from the host field's scan
static WaterCoreManager::MeshParity runDeviceMesh(VulkanTestFixture& fx, VkDevice device, VkPhysicalDevice pd, VkQueue queue, uint32_t qf, bool noRing, bool damBreak,
                                                  long& checks, long& checksBad, long& sideMax, long& multiMax, long& recordedMax, long& wokenEnd) {
    (void)fx;
    WaterCoreManager::MeshParity worst;
    const bool gpuBefore = WaterCoreGpu::s_gpuSurface;
    const float afterBefore = WaterCoreManager::s_recordAfterS;
    const auto drawBefore = WaterCoreManager::s_recordDraw;
    struct Restore { bool g; float a; WaterCoreManager::RecordDraw d; ~Restore() { WaterCoreGpu::s_gpuSurface = g; WaterCoreGpu::s_meshNoRingForTest = false; WaterCoreManager::s_recordAfterS = a; WaterCoreManager::s_recordDraw = d; } } restore{gpuBefore, afterBefore, drawBefore};
    WaterCoreGpu::s_gpuSurface = true;
    WaterCoreGpu::s_meshNoRingForTest = noRing;
    WaterCoreManager::s_recordAfterS = 0.5f;
    WaterCoreManager::s_recordDraw.on = true; WaterCoreManager::s_recordDraw.radiusM = 48.0f;
    // the ground, and the slab: micro cells y 9 (1.00-1.11 m) over x 36-71, z 36-71
    auto state = [](const glm::ivec3& m) -> int {
        if (m.y < 0) return 1;
        if (m.y == 9 && m.x >= 36 && m.x < 72 && m.z >= 36 && m.z < 72) return 1;
        return 0;
    };
    WaterCoreManager mgr(state);
    std::string err;
    if (!mgr.initGpu(device, pd, queue, qf, "shaders", &err)) { ADD_FAILURE() << err; return worst; }
    mgr.setDroplets(false);
    mgr.setViewPoint(glm::vec3(16.0f, 2.0f, 8.0f));
    FakeRecord record; record.install(mgr);
    const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(31, 2, 15), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
    if (!id) { ADD_FAILURE() << err; return worst; }
    mgr.setAutoSleep(id, false);
    if (!mgr.setPartialWake(id, true) || !mgr.setRecordTiles(id, true)) { ADD_FAILURE() << "partial wake / recording refused"; return worst; }
    mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(damBreak ? 23 : 31, 1, 15), 1.0f, nullptr);   // the dam break: its edge runs over dry ground (side faces)
    AvRecord rec;
    auto check = [&](int k) {
        const WaterCoreManager::MeshParity p = mgr.surfaceMeshParityForTest(id);
        if (!p.ok) { ADD_FAILURE() << "parity read failed at tick " << k; return; }
        ++checks;
        if (p.unmatched > 0 || p.worstPos > 1e-4f || p.worstOther > 1e-3f) {
            ++checksBad;
            if (checksBad <= 3) std::printf("    tick %d: host %ld quads, device %ld, unmatched %ld, worst pos %.2e, other %.2e\n", k, p.hostQuads, p.deviceQuads, p.unmatched, p.worstPos, p.worstOther);
        }
        if (p.unmatched > worst.unmatched) { worst.unmatched = p.unmatched; worst.hostQuads = p.hostQuads; worst.deviceQuads = p.deviceQuads; }
        worst.worstPos = std::max(worst.worstPos, p.worstPos); worst.worstOther = std::max(worst.worstOther, p.worstOther);
        sideMax = std::max(sideMax, p.sideQuads);
        multiMax = std::max(multiMax, p.hostQuads - 32L * 9 * 16 * 9);   // more quads than one top a column: the walls and the second runs
        if (const AvRecord* r = mgr.find(id)) recordedMax = std::max<long>(recordedMax, r->recordedTiles);
        float dm = 0.0f, hm = 0.0f; double dw = 0.0, hw = 0.0;   // 29 S2: the tall floor's numbers from the device
        if (!mgr.tallStatsParityForTest(id, dm, hm, dw, hw)) { ADD_FAILURE() << "tall stats read failed at tick " << k; return; }
        const bool minOk = (std::isinf(dm) && std::isinf(hm)) || std::abs(dm - hm) <= 1e-5f;
        if (!minOk || std::abs(dw - hw) > 1e-3 * std::max(1.0, hw)) {
            ++tallBad;
            if (tallBad <= 3) std::printf("    tick %d tall stats: lowest top device %.6f host %.6f, water device %.4f host %.4f\n", k, dm, hm, dw, hw);
        }
    };
    int k = 0;
    for (; k < 360; ++k) { mgr.step(id, 1, 1.0f / 60.0f, &rec); mgr.surfaceFields(); if (k % 10 == 9) check(k); }
    mgr.addImpulse(glm::vec3(10.0f, 1.9f, 8.0f), 0.5f, 3.0f, glm::vec3(0.0f, -1.0f, 0.0f));   // a push among the recorded tiles
    for (; k < 900; ++k) { mgr.step(id, 1, 1.0f / 60.0f, &rec); mgr.surfaceFields(); if (k % 10 == 9) check(k); }
    if (const AvRecord* r = mgr.find(id)) wokenEnd = r->tilesWoken;
    worst.ok = true;
    return worst;
}

TEST_F(WaterRecordTilesTest, TheDeviceMeshIsTheHostMesh) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    tallBad = 0;
    // A: the dam break - water running over dry ground (side faces, every column awake)
    long checks = 0, bad = 0, sideMax = 0, multiMax = 0, recordedMax = 0, woken = 0;
    const auto dam = runDeviceMesh(*this, device, physicalDevice, queue, queueFamilyIndex, false, true, checks, bad, sideMax, multiMax, recordedMax, woken);
    std::printf("  A dam break: %ld checks, %ld off; worst unmatched %ld, positions %.2e m, other %.2e | side quads up to %ld, extra quads up to %ld\n",
                checks, bad, dam.unmatched, dam.worstPos, dam.worstOther, sideMax, multiMax);
    // B: the pond - at rest every tile records (the marks), the push wakes some (the seeds) and leaves frozen columns beside awake ones
    long pChecks = 0, pBad = 0, s1 = 0, m1 = 0, recorded = 0, wokenP = 0;
    const auto pond = runDeviceMesh(*this, device, physicalDevice, queue, queueFamilyIndex, false, false, pChecks, pBad, s1, m1, recorded, wokenP);
    std::printf("  B pond: %ld checks, %ld off; worst unmatched %ld, positions %.2e m, other %.2e | tiles in the record up to %ld, woken %ld, extra quads up to %ld\n",
                pChecks, pBad, pond.unmatched, pond.worstPos, pond.worstOther, recorded, wokenP, m1);
    long rChecks = 0, rBad = 0, s2 = 0, m2 = 0, r2 = 0, w2 = 0;
    const auto red = runDeviceMesh(*this, device, physicalDevice, queue, queueFamilyIndex, true, false, rChecks, rBad, s2, m2, r2, w2);
    std::printf("  RED (B without the ring): %ld checks, %ld off; worst unmatched %ld\n", rChecks, rBad, red.unmatched);
    ASSERT_TRUE(dam.ok && pond.ok && red.ok);
    EXPECT_GT(checks + pChecks, 160);
    EXPECT_GT(sideMax, 0) << "A has side faces";
    EXPECT_GT(recorded, 0) << "B: tiles recorded (the record's marks were exercised)";
    EXPECT_GT(wokenP, 0) << "B: tiles woke (the seeds were exercised)";
    EXPECT_EQ(bad, 0) << "A: the device mesh differs from the host's";
    EXPECT_EQ(pBad, 0) << "B: the device mesh differs from the host's";
    EXPECT_GT(rBad, 0) << "RED CONTROL: without the ring the frozen columns beside the awake region kept stale corners - unseen";
    const long tallShipped = tallBad;
    tallBad = 0;
    WaterCoreGpu::s_tallAggNoFullForTest = true;   // RED: the brick-column numbers never redone whole - a recorded tile's water stays counted
    long tc = 0, tb = 0, s3 = 0, m3 = 0, r3 = 0, w3 = 0;
    runDeviceMesh(*this, device, physicalDevice, queue, queueFamilyIndex, false, false, tc, tb, s3, m3, r3, w3);
    WaterCoreGpu::s_tallAggNoFullForTest = false;
    const long tallRed = tallBad;
    tallBad = tallShipped;
    std::printf("  tall-floor numbers (lowest wet top, water) device vs host scan: %ld checks off | RED (never redone whole): %ld off\n", tallShipped, tallRed);
    EXPECT_GT(tallRed, 0) << "RED CONTROL: the tall-floor numbers without the whole pass went unseen";
    EXPECT_EQ(tallBad, 0) << "29 S2: the device's tall-floor numbers differ from the host field's";
}

// 29 S3 THE RESIDENCY ROUND THE AWAKE WATER IS THE WHOLE RESIDENCY (red first). A 40 x 24 m pond 1 m deep, partial wake on, a push at
// one end: the waves wake part of it and the rest stays frozen. Every call, with WaterCoreGpu::s_residencyCheckForTest, the residency
// recomputed only round the awake columns (dilated by the rings a call's reach crosses) is compared, entry by entry (absent / ground /
// stored), with the residency over every stored brick. Prediction: 0 differing entries over every call, the restricted path taken on
// most calls. RED CONTROL: nothing recomputed, every entry kept (s_residencyCopyAllForTest) - the water's own residency changes differ. (Measured: the dilation itself is not needed here - the partial wake's 2-column margin separates the moving water from the frozen columns; it stays as a margin of safety.)
static void runResidency(VkDevice device, VkPhysicalDevice pd, VkQueue queue, uint32_t qf, bool noDilate, long& mismatches, long& checked, long& restricted, long& calls) {
    WaterCoreGpu::s_residencyCheckForTest = true; WaterCoreGpu::s_residencyCopyAllForTest = noDilate;
    WaterCoreGpu::s_residencyMismatchForTest = 0; WaterCoreGpu::s_residencyCallsCheckedForTest = 0; WaterCoreGpu::s_residencyRestrictedCallsForTest = 0;
    struct Restore { ~Restore() { WaterCoreGpu::s_residencyCheckForTest = false; WaterCoreGpu::s_residencyCopyAllForTest = false; } } restore;
    auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
    WaterCoreManager mgr(state);
    std::string err;
    if (!mgr.initGpu(device, pd, queue, qf, "shaders", &err)) { ADD_FAILURE() << err; return; }
    mgr.setDroplets(false);
    const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(39, 1, 23), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
    if (!id) { ADD_FAILURE() << err; return; }
    mgr.setAutoSleep(id, false);
    EXPECT_TRUE(mgr.setPartialWake(id, true));
    mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(39, 0, 23), 1.0f, nullptr);
    AvRecord rec;
    for (int k = 0; k < 120; ++k) { mgr.step(id, 1, 1.0f / 60.0f, &rec); ++calls; }
    mgr.addImpulse(glm::vec3(4.0f, 0.9f, 12.0f), 0.5f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
    for (int k = 0; k < 600; ++k) { mgr.step(id, 1, 1.0f / 60.0f, &rec); ++calls; }
    mismatches = WaterCoreGpu::s_residencyMismatchForTest; checked = WaterCoreGpu::s_residencyCallsCheckedForTest; restricted = WaterCoreGpu::s_residencyRestrictedCallsForTest;
}

TEST_F(WaterRecordTilesTest, TheResidencyRoundTheAwakeWaterIsTheWholeResidency) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    long mm = 0, ck = 0, rs = 0, calls = 0, rmm = 0, rck = 0, rrs = 0, rcalls = 0;
    runResidency(device, physicalDevice, queue, queueFamilyIndex, false, mm, ck, rs, calls);
    runResidency(device, physicalDevice, queue, queueFamilyIndex, true, rmm, rck, rrs, rcalls);
    std::printf("  shipped: %ld calls, the restricted residency on %ld, checked %ld, entries differing %ld | RED (nothing recomputed): checked %ld, differing %ld\n",
                calls, rs, ck, mm, rck, rmm);
    EXPECT_GT(rs, calls / 2) << "the restricted path ran (frozen columns beside awake ones)";
    EXPECT_EQ(mm, 0) << "the residency round the awake water differs from the whole residency";
    EXPECT_GT(rmm, 0) << "RED CONTROL: with nothing recomputed the residency changes went unseen";
}

// DiagLakeRecorded (27.23 W1e step 0): the 60 x 60 m lake (2 m deep, 3 m box) with the partial wake and RECORDING on - the lake at
// rest records tile by tile (its bricks freed), a stone wakes the tiles round it, waves, freezes, records again. Per second: the
// call's mean GPU ms, awake / frozen columns, recorded tiles, stored bricks, the books. LAKE_QS = the freeze test's scale.
TEST_F(WaterRecordTilesTest, DISABLED_DiagLakeRecorded) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    if (const char* q = std::getenv("LAKE_QS")) WaterCoreGpu::s_columnQuietScale = static_cast<float>(std::atof(q));
    auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
    WaterCoreManager mgr(state);
    std::string err;
    ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    mgr.setDroplets(false);
    FakeRecord record; record.install(mgr);
    const float dtTick = std::getenv("LAKE_DT") ? static_cast<float>(std::atof(std::getenv("LAKE_DT"))) : 1.0f / 60.0f;   // a live frame's dt
    const bool realtime = std::getenv("LAKE_RT") != nullptr;   // the live path: async calls (collected next frame), update()
    if (realtime) mgr.setRealtime(true);   // before create: a volume takes its async mode at create
    const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(59, 2, 59), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
    ASSERT_TRUE(id) << err;
    mgr.setAutoSleep(id, false);
    ASSERT_TRUE(mgr.setPartialWake(id, true) && mgr.setRecordTiles(id, true));
    mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(59, 1, 59), 1.0f, nullptr);
    const double placed = mgr.totalMass();
    std::printf("  quiet scale %.1f, record after %.1f s, placed %.4f m^3\n", WaterCoreGpu::s_columnQuietScale, WaterCoreManager::s_recordAfterS, placed);
    AvRecord rec;
    double msSum = 0.0, booksWorst = 0.0; int msN = 0;
    for (int k = 1; k <= 60 * 31; ++k) {   // k counts 1/60 s only at the default dt
        if (k == 60 * 10) mgr.addImpulse(glm::vec3(30.0f, 1.9f, 30.0f), 0.5f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
        if (realtime) mgr.update(dtTick); else mgr.step(id, 1, dtTick, &rec);
        const AvRecord* r = mgr.find(id);
        if (r) { msSum += r->gpuDeviceMs; ++msN; }
        if (k % 60 || !r) continue;
        booksWorst = std::max(booksWorst, std::abs(mgr.totalMass() + r->residueDropped + record.total() - placed));   // once a second: totalMass reads the volume back
        std::printf("  t %4.1f s mean %6.2f ms | awake %4d frozen %4d recorded tiles %3d bricks %6d cached %d reduce slots %6d | tall %d | books worst %.5f m^3\n",
                    k / 60.0, msN ? msSum / msN : 0.0, r->awakeColumns, r->frozenColumns, r->recordedTiles, r->gpuBricks, int(r->gpuReduceCached), r->gpuReduceSlots,
                    int(r->tallActive), booksWorst);
        msSum = 0.0; msN = 0;
        if (k == 60 * 24) WaterCoreManager::setKernelTiming(true, true);
        if (k == 60 * 30) {
            long calls = 0; auto st = WaterCoreManager::kernelTiming(calls);
            std::sort(st.begin(), st.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
            std::printf("      stages (%ld calls, ms a call):", calls);
            for (const auto& e : st) if (calls && e.second / calls > 0.02) std::printf(" %s %.2f", e.first.c_str(), e.second / calls);
            std::printf(" | dispatches %d\n", r->gpuDispatches);
            WaterCoreManager::setKernelTiming(false, true);
        }
    }
    WaterCoreGpu::s_columnQuietScale = WaterCoreGpu::kColumnQuietScaleDefault;
}
