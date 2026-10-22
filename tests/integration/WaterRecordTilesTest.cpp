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
