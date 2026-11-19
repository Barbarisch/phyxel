// WaterCore §51.4 S3 - the far skin (docs/WaterCore.md 51.4). CPU only: a manager with a span reader, no device.
//
// Recorded water is drawn within s_locality.drawM of the viewer by the record tiles (8 m, the pond surface); beyond it, by the
// SKIN: coarse surface fields on clipmap rings (4, 8, 16... m cells) built from a per-chunk-column water summary.
// Predictions, written before the skin was built:
//   - TheFarHalfOfALakeIsDrawn: a 160 x 160 m lake 1 m deep (32-aligned, across x = 0 and z = 0), the view 10 m off its south
//     shore, drawM 48: every 2 m point of the lake drawn by exactly one of the record tiles or the skin - 0 holes, 0 doubles - the
//     skin at the lake's level (1.000) within 1 mm. RED before the skin: every point beyond ~48 m is a hole.
//   - TheSkinCostsTheSameForAnyFarLake: a 3 km and an 8 km lake (both past far_m in every lake direction), the view the same
//     distance off the south shore: skin columns within 1.1x; a still camera rebuilds nothing.
//   - Controls: a dry world draws 0 skin columns; a lake entirely within drawM draws 0 skin columns.
//   - TheCoarseShoreStaysOnTheShore: the bench's 150 m lake (straight shores NOT on any cell lattice: x 4..153, z 2..151), the
//     view where the clip's wide pose is: every 1 m point within 30 m of the lake - 0 points of the lake undrawn, 0 points off it
//     drawn by the skin. Red control (s_wholeCellsForTest, the first build's rule: a cell drawn whole when half its area is wet):
//     points off the lake drawn (the live still showed water over the kerb). A round lake (r 70 m): its overhang and holes printed.
//   - TheSummaryIsTheSamePerChunkAndWhole (equality (i)): an irregular lake across chunk seams, summarized by the manager chunk
//     column by chunk column vs one whole-region pass - every 4 m cell identical.
#include <gtest/gtest.h>
#include "core/water/WaterCoreManager.h"
#include "core/water/WaterSurfaceMesh.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using namespace Phyxel::Core::Water;

namespace {

// an analytic record: wet(x, z) -> the run [0, top(x, z)) - no map of millions of columns
struct AnalyticRecord {
    std::function<bool(int, int)> wet;
    std::function<float(int, int)> top = [](int, int) { return 1.0f; };
    long reads = 0;
    void install(WaterCoreManager& mgr) {
        mgr.setSpanIo(
            [](const std::vector<ColumnRuns>&, float, float, long* unwritten, long* chunks) { if (unwritten) *unwritten = 0; if (chunks) *chunks = 0; },
            [this](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                ++reads;
                out.clear();
                const float yLo = static_cast<float>(lo.y), yHi = static_cast<float>(hi.y + 1);
                for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                    if (!wet(x, z)) continue;
                    ColumnRun r; r.bottomY = std::max(0.0f, yLo); r.topY = std::min(top(x, z), yHi);
                    if (r.topY <= r.bottomY) continue;
                    r.mass = static_cast<double>(r.topY - r.bottomY);
                    ColumnRuns c; c.x = x; c.z = z; c.runs.push_back(r);
                    out.push_back(std::move(c));
                }
            });
    }
};

struct SkinSettingsGuard {
    float drawM = WaterCoreManager::s_locality.drawM;
    WaterCoreManager::SkinSettings skin = WaterCoreManager::s_skin;
    bool rec = WaterCoreManager::s_recordDraw.on;
    ~SkinSettingsGuard() { WaterCoreManager::s_locality.drawM = drawM; WaterCoreManager::s_skin = skin; WaterCoreManager::s_recordDraw.on = rec; }
};

int ground(const glm::ivec3& m) { return m.y < 0 ? 1 : 0; }

// every record tile and every summary built: ask until nothing is pending
void settle(WaterCoreManager& mgr) {
    for (int i = 0; i < 4000; ++i) {
        mgr.surfaceFields();
        mgr.skinFields();
        if (mgr.recordDrawStats().pending == 0 && mgr.skinStats().pending == 0) return;
    }
    ADD_FAILURE() << "never settled: record pending " << mgr.recordDrawStats().pending << ", skin pending " << mgr.skinStats().pending;
}

long skinWetColumns(WaterCoreManager& mgr) {
    long n = 0;
    for (const WaterSurfaceField* f : mgr.skinFields())
        for (const SurfaceColumn& c : f->cols) if (c.runs > 0.5f) ++n;
    return n;
}

}  // namespace

// 51.4g: the owner's call (2026-11-11, from the fly-out clip) - the skin ships ON; a default change updates this test in the same commit
TEST(WaterSkinTest, TheSkinShipsOn) {
    EXPECT_TRUE(WaterSkin::Settings{}.on);
    EXPECT_EQ(WaterSkin::Settings{}.farM, 1024.0f);
    EXPECT_EQ(WaterSkin::Settings{}.baseCellM, 4);
}

TEST(WaterSkinTest, TheFarHalfOfALakeIsDrawn) {
    SkinSettingsGuard guard;
    WaterCoreManager::s_locality.drawM = 48.0f;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = true;
    WaterCoreManager mgr(ground);
    const int x0 = -64, x1 = 96, z0 = -64, z1 = 96;   // [x0, x1) x [z0, z1): 160 m, 32-aligned
    AnalyticRecord rec; rec.wet = [&](int x, int z) { return x >= x0 && x < x1 && z >= z0 && z < z1; };
    rec.install(mgr);
    mgr.setViewPoint(glm::vec3(16.0f, 6.0f, static_cast<float>(z0) - 10.0f));
    struct Count { int points = 0, holes = 0, doubles = 0, bySkin = 0, byRecord = 0; float levelWorst = 0.0f; };
    auto count = [&]() {
        Count c;
        for (int z = z0 + 1; z < z1; z += 2) for (int x = x0 + 1; x < x1; x += 2) {
            ++c.points;
            float rt = 0.0f, st = 0.0f; bool recorded = false;
            const bool r = mgr.drawnAtWorld(x + 0.0f, z + 0.0f, rt, recorded);
            int claims = 0;
            const bool s = mgr.skinDrawnAtWorld(x + 0.0f, z + 0.0f, st, &claims);
            if (!r && !s) ++c.holes;
            if ((r && s) || claims > 1) ++c.doubles;
            if (r) ++c.byRecord;
            if (s) { ++c.bySkin; c.levelWorst = std::max(c.levelWorst, std::abs(st - 1.0f)); }
        }
        return c;
    };
    // the red control: the skin off (today's drawing) - every point beyond the record tiles is a hole
    WaterCoreManager::s_skin.on = false;
    settle(mgr);
    const Count off = count();
    EXPECT_EQ(skinWetColumns(mgr), 0);
    WaterCoreManager::s_skin.on = true;
    settle(mgr);
    const Count on = count();
    const auto& st = mgr.skinStats();
    std::printf("[skin] off: %d points: record %d, holes %d\n", off.points, off.byRecord, off.holes);
    std::printf("[skin] on:  %d points: record %d, skin %d, holes %d, doubles %d; skin level worst %.5f m; levels %d, fields %d, wet cells %d\n",
                on.points, on.byRecord, on.bySkin, on.holes, on.doubles, on.levelWorst, st.levels, st.fields, st.wetCells);
    EXPECT_GT(off.holes, off.points / 2);   // the control measures the defect
    EXPECT_EQ(on.holes, 0);
    EXPECT_EQ(on.doubles, 0);
    EXPECT_EQ(on.byRecord, off.byRecord);   // the skin never changes what the record tiles draw
    EXPECT_GT(on.bySkin, on.points / 2);
    EXPECT_LE(on.levelWorst, 1e-3f);
}

TEST(WaterSkinTest, TheSkinCostsTheSameForAnyFarLake) {
    SkinSettingsGuard guard;
    WaterCoreManager::s_locality.drawM = 48.0f;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = true;
    WaterCoreManager::s_skin.farM = 1024.0f;
    struct Out { long cols = 0; int levels = 0, fields = 0; double settleMs = 0.0, stillMs = 0.0; int stillRebuilt = 0, stillSummaries = 0; double moveMean = 0.0, moveMax = 0.0; long moveRebuilt = 0; };
    auto run = [&](int half) {
        Out o;
        WaterCoreManager mgr(ground);
        AnalyticRecord rec; rec.wet = [half](int x, int z) { return x >= -half && x < half && z >= -half && z < half; };
        rec.install(mgr);
        mgr.setViewPoint(glm::vec3(0.0f, 6.0f, static_cast<float>(-half) - 10.0f));
        const auto t0 = std::chrono::steady_clock::now();
        settle(mgr);
        o.settleMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        o.cols = skinWetColumns(mgr);
        const auto t1 = std::chrono::steady_clock::now();
        mgr.skinFields();
        o.stillMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        o.stillRebuilt = mgr.skinStats().fieldsRebuiltLastCall; o.stillSummaries = mgr.skinStats().summariesBuiltLastCall;
        o.levels = mgr.skinStats().levels; o.fields = mgr.skinStats().fields;
        // flying along the shore at 0.5 m a call (30 m/s at 60 fps): the summaries keep up, the levels rebuild only at their steps
        double sum = 0.0;
        for (int i = 1; i <= 200; ++i) {
            mgr.setViewPoint(glm::vec3(0.5f * static_cast<float>(i), 6.0f, static_cast<float>(-half) - 10.0f));
            const auto t2 = std::chrono::steady_clock::now();
            mgr.skinFields();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
            sum += ms; o.moveMax = std::max(o.moveMax, ms);
            o.moveRebuilt += mgr.skinStats().fieldsRebuiltLastCall;
        }
        o.moveMean = sum / 200.0;
        return o;
    };
    const Out a = run(1536), b = run(4096);   // 3 km and 8 km
    std::printf("[skin] 3 km: %ld cols, %d levels, %d fields, settle %.1f ms, still call %.3f ms (%d fields, %d summaries rebuilt)\n",
                a.cols, a.levels, a.fields, a.settleMs, a.stillMs, a.stillRebuilt, a.stillSummaries);
    std::printf("[skin] 8 km: %ld cols, %d levels, %d fields, settle %.1f ms, still call %.3f ms (%d fields, %d summaries rebuilt)\n",
                b.cols, b.levels, b.fields, b.settleMs, b.stillMs, b.stillRebuilt, b.stillSummaries);
    std::printf("[skin] moving 0.5 m a call, 200 calls: 3 km mean %.3f ms max %.3f ms (%ld fields rebuilt); 8 km mean %.3f ms max %.3f ms (%ld)\n",
                a.moveMean, a.moveMax, a.moveRebuilt, b.moveMean, b.moveMax, b.moveRebuilt);
    EXPECT_LT(a.moveMean, 1.0); EXPECT_LT(b.moveMean, 1.0);
    ASSERT_GT(a.cols, 0);
    const double ratio = static_cast<double>(std::max(a.cols, b.cols)) / static_cast<double>(std::min(a.cols, b.cols));
    EXPECT_LE(ratio, 1.1);
    const long bound = static_cast<long>(b.levels) * WaterCoreManager::s_skin.ringCells * WaterCoreManager::s_skin.ringCells;
    EXPECT_LE(b.cols, bound);
    EXPECT_EQ(a.stillRebuilt, 0); EXPECT_EQ(b.stillRebuilt, 0);
    EXPECT_EQ(a.stillSummaries, 0); EXPECT_EQ(b.stillSummaries, 0);
}

TEST(WaterSkinTest, ControlsDryWorldAndANearLakeDrawNoSkin) {
    SkinSettingsGuard guard;
    WaterCoreManager::s_locality.drawM = 48.0f;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = true;
    {   // dry
        WaterCoreManager mgr(ground);
        AnalyticRecord rec; rec.wet = [](int, int) { return false; };
        rec.install(mgr);
        mgr.setViewPoint(glm::vec3(0.0f, 6.0f, 0.0f));
        settle(mgr);
        EXPECT_EQ(skinWetColumns(mgr), 0);
    }
    {   // a 32 x 32 m lake round the viewer: the record tiles draw all of it
        WaterCoreManager mgr(ground);
        AnalyticRecord rec; rec.wet = [](int x, int z) { return x >= -16 && x < 16 && z >= -16 && z < 16; };
        rec.install(mgr);
        mgr.setViewPoint(glm::vec3(0.0f, 6.0f, 0.0f));
        settle(mgr);
        EXPECT_EQ(skinWetColumns(mgr), 0);
        EXPECT_GT(mgr.recordDrawStats().wetTiles, 0);
    }
}

TEST(WaterSkinTest, TheSummaryIsTheSamePerChunkAndWhole) {
    SkinSettingsGuard guard;
    WaterCoreManager::s_locality.drawM = 8.0f;
    WaterCoreManager::s_skin.on = true;
    WaterCoreManager::s_skin.farM = 256.0f;
    WaterCoreManager mgr(ground);
    // an irregular lake across the x = 0 / z = 0 chunk seams (and the 32 m ones), its level varying by column
    AnalyticRecord rec;
    rec.wet = [](int x, int z) { const float dx = x + 0.5f - 3.0f, dz = z + 0.5f - 5.0f; return dx * dx + dz * dz < 37.0f * 37.0f; };
    rec.top = [](int x, int z) { return 1.0f + 0.01f * static_cast<float>(((x % 7) + 7) % 7) + 0.001f * static_cast<float>(((z % 5) + 5) % 5); };
    rec.install(mgr);
    mgr.setViewPoint(glm::vec3(3.0f, 6.0f, 5.0f));
    settle(mgr);
    // the whole region in one read and one pass
    std::vector<ColumnRuns> runs;
    std::map<std::pair<int, int>, SkinCell> whole;
    // the same vertical band the manager reads
    mgr.skinReaderForTest()(glm::ivec3(-64, WaterCoreManager::kSkinYLo, -64), glm::ivec3(63, WaterCoreManager::kSkinYHi, 63), runs);
    summarizeSkinCells(runs, whole);
    int cells = 0, differ = 0, missing = 0;
    for (int cz = -16; cz < 16; ++cz) for (int cx = -16; cx < 16; ++cx) {
        SkinCell m;
        const bool have = mgr.skinCellForTest(cx, cz, m);
        const auto it = whole.find({cx, cz});
        const SkinCell w = it == whole.end() ? SkinCell{} : it->second;
        if (!have) { if (w.wet) ++missing; continue; }
        ++cells;
        if (!(m == w)) ++differ;
    }
    std::printf("[skin] equality: %d cells compared, %d differ, %d wet cells missing (whole pass: %zu cells)\n", cells, differ, missing, whole.size());
    EXPECT_GT(cells, 300);
    EXPECT_EQ(differ, 0);
    EXPECT_EQ(missing, 0);
}

TEST(WaterSkinTest, TheCoarseShoreStaysOnTheShore) {
    SkinSettingsGuard guard;
    struct WholeGuard { ~WholeGuard() { WaterSkin::s_wholeCellsForTest = false; } } whole;
    WaterCoreManager::s_locality.drawM = 48.0f;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = true;
    struct Count { int lake = 0, holes = 0, off = 0, overhang = 0; float worstM = 0.0f; };
    auto run = [&](std::function<bool(int, int)> wet, std::function<float(float, float)> distOut, bool wholeCells) {
        WaterSkin::s_wholeCellsForTest = wholeCells;
        WaterCoreManager mgr(ground);
        AnalyticRecord rec; rec.wet = wet; rec.install(mgr);
        mgr.setViewPoint(glm::vec3(79.0f, 6.0f, 172.0f));   // the clip's wide pose, over the water's height band
        settle(mgr);
        Count c;
        for (int z = -30; z < 182; ++z) for (int x = -30; x < 184; ++x) {
            const float px = x + 0.5f, pz = z + 0.5f;
            float t = 0.0f; bool recd = false; int claims = 0;
            const bool r = mgr.drawnAtWorld(px, pz, t, recd), sk = mgr.skinDrawnAtWorld(px, pz, t, &claims);
            if (wet(x, z)) { ++c.lake; if (!r && !sk) ++c.holes; }
            else { ++c.off; if (sk) { ++c.overhang; c.worstM = std::max(c.worstM, distOut(px, pz)); } }
        }
        return c;
    };
    auto square = [](int x, int z) { return x >= 4 && x < 154 && z >= 2 && z < 152; };
    auto squareOut = [](float x, float z) { return std::max({4.0f - x, x - 154.0f, 2.0f - z, z - 152.0f, 0.0f}); };
    const Count red = run(square, squareOut, true), sq = run(square, squareOut, false);
    std::printf("[skin] straight shores, whole cells (red control): %d lake points, %d holes; %d off-lake points drawn, worst %.1f m out\n",
                red.lake, red.holes, red.overhang, red.worstM);
    std::printf("[skin] straight shores, wet rectangle:             %d lake points, %d holes; %d off-lake points drawn, worst %.1f m out\n",
                sq.lake, sq.holes, sq.overhang, sq.worstM);
    EXPECT_GT(red.overhang, 0);   // the control measures the defect
    EXPECT_EQ(sq.overhang, 0);
    EXPECT_EQ(sq.holes, 0);
    auto round = [](int x, int z) { const float dx = x + 0.5f - 79.0f, dz = z + 0.5f - 77.0f; return dx * dx + dz * dz < 70.0f * 70.0f; };
    auto roundOut = [](float x, float z) { return std::max(0.0f, std::sqrt((x - 79.0f) * (x - 79.0f) + (z - 77.0f) * (z - 77.0f)) - 70.0f); };
    const Count rd = run(round, roundOut, false), rdWhole = run(round, roundOut, true);
    WaterSkin::s_rectShoreForTest = true;   // 51.6: S3's shore (the rectangle alone), for comparison
    const Count rdRect = run(round, roundOut, false);
    WaterSkin::s_rectShoreForTest = false;
    std::printf("[skin] round lake r 70, wet rectangle: %d lake points, %d holes (%.2f %%); %d off-lake points drawn (%.2f %% of the lake), worst %.1f m out\n",
                rdRect.lake, rdRect.holes, 100.0 * rdRect.holes / rdRect.lake, rdRect.overhang, 100.0 * rdRect.overhang / rdRect.lake, rdRect.worstM);
    std::printf("[skin] round lake r 70, sub-cells:     %d lake points, %d holes (%.2f %%); %d off-lake points drawn (%.2f %% of the lake), worst %.1f m out\n",
                rd.lake, rd.holes, 100.0 * rd.holes / rd.lake, rd.overhang, 100.0 * rd.overhang / rd.lake, rd.worstM);
    std::printf("[skin] round lake r 70, whole cells:   %d lake points, %d holes (%.2f %%); %d off-lake points drawn (%.2f %% of the lake), worst %.1f m out\n",
                rdWhole.lake, rdWhole.holes, 100.0 * rdWhole.holes / rdWhole.lake, rdWhole.overhang, 100.0 * rdWhole.overhang / rdWhole.lake, rdWhole.worstM);
}

// 51.4c: a skin cell is flat at its own level. Live on a generated world a river in a gorge beside water 30 m higher drew walls of
// water: the mesh's corner averaging joined cells whose runs overlapped within one (coarse) cell height. A stepped river (its level
// falling 1 m every 8 m) beside a lake 30 m higher, far from the view: every top vertex of every skin quad at its own cell's level.
TEST(WaterSkinTest, ASkinCellIsFlatAtItsOwnLevel) {
    SkinSettingsGuard guard;
    WaterCoreManager::s_locality.drawM = 48.0f;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = true;
    WaterCoreManager mgr(ground);
    AnalyticRecord rec;
    rec.wet = [](int x, int z) { return (x >= 100 && x < 116 && z >= -200 && z < 200) || (x >= 116 && x < 180 && z >= -40 && z < 40); };
    rec.top = [](int x, int z) { return x >= 116 ? 31.0f : 1.0f + static_cast<float>(((z + 200) / 8) % 30); };
    rec.install(mgr);
    mgr.setViewPoint(glm::vec3(0.0f, 20.0f, 0.0f));
    settle(mgr);
    struct Off { ~Off() { g_skinCornersAveragedForTest = false; } } off;
    auto measure = [&](bool averaged, long& quads, long& offLevel, float& worst) {
        g_skinCornersAveragedForTest = averaged;
        quads = 0; offLevel = 0; worst = 0.0f;
        for (const WaterSurfaceField* f : mgr.skinFields()) {
            WaterSurfaceMesh m; buildWaterSurfaceMesh(*f, m);
            for (size_t q = 0; q + 3 < m.vertices.size(); q += 4) {
                if (m.vertices[q].side != 0.0f) continue;
                ++quads;
                // the quad's own cell: its centre
                const glm::vec3 c = 0.25f * (m.vertices[q].pos + m.vertices[q + 1].pos + m.vertices[q + 2].pos + m.vertices[q + 3].pos);
                const int cx = static_cast<int>(std::floor(c.x / f->h)) - f->origin.x, cz = static_cast<int>(std::floor(c.z / f->h)) - f->origin.z;
                const float own = f->at(cx, cz).top[0];
                for (int k = 0; k < 4; ++k) { const float d = std::abs(m.vertices[q + k].pos.y - own); worst = std::max(worst, d); if (d > 1e-4f) { ++offLevel; break; } }
            }
        }
    };
    long rq, ro, quads, offLevel; float rw, worst;
    measure(true, rq, ro, rw);
    measure(false, quads, offLevel, worst);
    std::printf("[skin] averaged corners (red control): %ld top quads, %ld with a corner off their cell's level (worst %.2f m)\n", rq, ro, rw);
    std::printf("[skin] flat cells:                     %ld top quads, %ld with a corner off their cell's level (worst %.2f m)\n", quads, offLevel, worst);
    EXPECT_GT(ro, 0);   // the control measures the defect
    EXPECT_GT(quads, 50);
    EXPECT_EQ(offLevel, 0);
}

// 51.4d: a record tile over DEEP water wakes only its top into the scratch grid. Live (WaterBench_RiverCarve) tiles over ~60 m of
// generated water cost 309-513 ms a call (4 tiles): the 1/9 m scratch held the water's whole height (72 x 558 x 72 cells, an
// occupancy ask each). Prediction: a 60 m-deep lake's record tiles - clipped to the top 2 m, the run bottoms restored from the record -
// draw the same field (every run's top within 0.1 mm, its bottom within 0.1 mm, the same run counts) at under a fifth of the build
// time. Control: a 1 m pond (nothing to clip) is the same and costs the same.
TEST(WaterSkinTest, ADeepRecordTileWakesOnlyItsTop) {
    SkinSettingsGuard guard;
    struct G { ~G() { WaterCoreManager::s_recordTileWholeDepthForTest = false; } } g;
    WaterCoreManager::s_locality.drawM = 24.0f;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = false;
    auto run = [&](float bottom, float top, bool whole, double& ms, std::map<int, std::vector<SurfaceColumn>>& out) {
        WaterCoreManager::s_recordTileWholeDepthForTest = whole;
        auto state = [bottom](const glm::ivec3& m) { return m.y < static_cast<int>(bottom) * 9 ? 1 : 0; };
        WaterCoreManager mgr(state);
        AnalyticRecord rec;
        rec.wet = [](int x, int z) { return x >= -40 && x < 40 && z >= -40 && z < 40; };
        rec.top = [top](int, int) { return top; };
        rec.install(mgr);
        // the reader's runs start at y 0: the bed is `bottom`
        mgr.setSpanIo([](const std::vector<ColumnRuns>&, float, float, long* u, long* c) { if (u) *u = 0; if (c) *c = 0; },
                      [&rec, bottom](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& o) {
                          o.clear();
                          for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                              if (!rec.wet(x, z)) continue;
                              ColumnRun r; r.bottomY = std::max(bottom, static_cast<float>(lo.y)); r.topY = std::min(rec.top(x, z), static_cast<float>(hi.y + 1));
                              if (r.topY <= r.bottomY) continue;
                              r.mass = r.topY - r.bottomY;
                              ColumnRuns c; c.x = x; c.z = z; c.runs.push_back(r); o.push_back(std::move(c));
                          }
                      });
        mgr.setViewPoint(glm::vec3(0.0f, top + 4.0f, 0.0f));
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 400; ++i) { mgr.surfaceFields(); if (mgr.recordDrawStats().pending == 0) break; }
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        out.clear();
        for (const WaterSurfaceField* f : mgr.surfaceFields()) out[f->source] = f->cols;
    };
    struct Cmp { long cols = 0, differ = 0; float worstTop = 0.0f, worstBottom = 0.0f; };
    auto compare = [](const std::map<int, std::vector<SurfaceColumn>>& a, const std::map<int, std::vector<SurfaceColumn>>& b) {
        Cmp c;
        if (a.size() != b.size()) { c.differ = 1 << 30; return c; }
        for (const auto& [src, ca] : a) {
            const auto it = b.find(src);
            if (it == b.end() || it->second.size() != ca.size()) { ++c.differ; continue; }
            for (size_t i = 0; i < ca.size(); ++i) {
                const SurfaceColumn &p = ca[i], &q = it->second[i];
                ++c.cols;
                bool bad = p.runs != q.runs;
                for (int r = 0; r < static_cast<int>(p.runs + 0.5f) && r < kSurfaceMaxRuns; ++r) {
                    c.worstTop = std::max(c.worstTop, std::abs(p.top[r] - q.top[r])); c.worstBottom = std::max(c.worstBottom, std::abs(p.bottom[r] - q.bottom[r]));
                    bad |= std::abs(p.top[r] - q.top[r]) > 1e-4f || std::abs(p.bottom[r] - q.bottom[r]) > 1e-4f;
                }
                c.differ += bad;
            }
        }
        return c;
    };
    std::map<int, std::vector<SurfaceColumn>> deepWhole, deepTop, pondWhole, pondTop;
    double msDeepWhole, msDeepTop, msPondWhole, msPondTop;
    run(0.0f, 60.25f, true, msDeepWhole, deepWhole);
    run(0.0f, 60.25f, false, msDeepTop, deepTop);
    run(0.0f, 1.25f, true, msPondWhole, pondWhole);
    run(0.0f, 1.25f, false, msPondTop, pondTop);
    const Cmp deep = compare(deepWhole, deepTop), pond = compare(pondWhole, pondTop);
    std::printf("[record] 60 m deep: %zu tiles, whole depth %.0f ms, top only %.0f ms; %ld columns, %ld differ (worst top %.6f, bottom %.6f m)\n",
                deepTop.size(), msDeepWhole, msDeepTop, deep.cols, deep.differ, deep.worstTop, deep.worstBottom);
    std::printf("[record] 1 m pond:  %zu tiles, whole depth %.0f ms, top only %.0f ms; %ld columns, %ld differ\n",
                pondTop.size(), msPondWhole, msPondTop, pond.cols, pond.differ);
    EXPECT_GT(deep.cols, 1000);
    EXPECT_EQ(deep.differ, 0);
    EXPECT_EQ(pond.differ, 0);
    EXPECT_LT(msDeepTop, msDeepWhole / 5.0);
}

// 51.5 S4 - the swaps in motion (docs/WaterCore.md 51.5). A round lake (r 300 m, level 1.0, centred on the origin); the camera 5 m
// above it flies from 700 m out to the centre and back at 40 m/s, 60 frames a second, asking the record tiles and the skin ONCE a
// frame (the budgets as shipped - no settle between frames). Each frame:
//   - HOLES: lake points (2 m grid within drawM + 24 m of the camera) that neither the record tiles nor the skin draw;
//   - DOUBLES: lake points both draw;
//   - FLIPS: points of a 2 m grid in the shore band (|r - 300| <= 8 m) and the seam window whose drawn state or level changed since
//     the last frame (the lake is at rest: any change is a swap), weighted by the screen pixels a 2 m square covers there
//     (1080 rows over a 70 degree field; the grazing factor 5 / distance).
// Predictions (written before measuring): holes > 0 today - a record tile entering range is masked out of the skin at once and drawn
// only on its build turn (4 a call); flips > 0 at the shore wherever a level boundary or the record/skin seam passes it.
namespace {
struct FlyStats { long motionHoleFrames = 0, motionHolesMax = 0; float holeInlandMax = 0.0f; double skinMsMax = 0.0; long fieldsRebuiltMax = 0; long frames = 0, holeFrames = 0, holesMax = 0, holesSum = 0, doublesMax = 0, flipFrames = 0, flipsSum = 0; double pxMax = 0.0, pxSum = 0.0; long bigPopFrames = 0;
                  double pxByKind[9] = {}; double pxByDist[4] = {}; };   // kind = from * 3 + to (0 none, 1 record, 2 skin); dist <48, <100, <300, more
FlyStats flyInAndOut(float speed, float drawM, bool verbose, bool settleEach = false, bool redMask = false, bool rectShore = false) {
    SkinSettingsGuard guard;
    struct RectGuard { explicit RectGuard(bool on) { WaterSkin::s_rectShoreForTest = on; } ~RectGuard() { WaterSkin::s_rectShoreForTest = false; } } rectGuard(rectShore);
    WaterCoreManager::s_locality.drawM = drawM;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = true;
    WaterCoreManager::s_skin.farM = 1024.0f;
    WaterCoreManager mgr(ground);
    const float R = 300.0f;
    AnalyticRecord rec;
    rec.wet = [&](int x, int z) { const float fx = x + 0.5f, fz = z + 0.5f; return fx * fx + fz * fz < R * R; };
    rec.install(mgr);
    // the reference: the same flight settled every frame - a MOTION hole is a point it draws and the flight does not
    WaterCoreManager ref(ground);
    rec.install(ref);
    const float eye = 6.0f, startZ = 700.0f;
    mgr.setViewPoint(glm::vec3(0.0f, eye, startZ));
    settle(mgr);
    const float step = speed / 60.0f;
    const int legFrames = static_cast<int>(startZ / step);
    std::map<std::pair<int, int>, std::pair<float, int>> prev;   // point -> drawn level (or -1e30 not drawn), its drawer
    FlyStats st;
    const double pxPerSr = std::pow(1080.0 / (70.0 * 3.14159265 / 180.0), 2.0);
    for (int fr = 0; fr <= 2 * legFrames; ++fr) {
        const float z = fr <= legFrames ? startZ - step * fr : step * (fr - legFrames);
        const glm::vec3 view(0.0f, eye, z);
        mgr.setViewPoint(view);
        if (settleEach) settle(mgr);
        if (!settleEach) { ref.setViewPoint(view); settle(ref); }   // the reference always masks what draws (51.5)
        mgr.surfaceFields();
        WaterSkin::s_maskInRangeForTest = redMask;   // the red control: the S3 mask, on the flight only
        { const auto t0 = std::chrono::steady_clock::now(); mgr.skinFields();
          st.skinMsMax = std::max(st.skinMsMax, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); }
        WaterSkin::s_maskInRangeForTest = false;
        st.fieldsRebuiltMax = std::max(st.fieldsRebuiltMax, static_cast<long>(mgr.skinStats().fieldsRebuiltLastCall));
        long holes = 0, doubles = 0, flips = 0, motionHoles = 0; double px = 0.0;
        std::map<std::pair<int, int>, std::pair<float, int>> cur;
        auto sample = [&](int x, int zz, bool holeCheck) {
            const float fx = x + 1.0f, fz = zz + 1.0f;
            float rt = 0.0f, stp = 0.0f; bool recorded = false; int claims = 0;
            const bool r = mgr.drawnAtWorld(fx, fz, rt, recorded);
            const bool s = mgr.skinDrawnAtWorld(fx, fz, stp, &claims);
            const bool wet = fx * fx + fz * fz < R * R;
            if (holeCheck && wet && !r && !s) {
                ++holes;
                st.holeInlandMax = std::max(st.holeInlandMax, R - std::sqrt(fx * fx + fz * fz));   // how far inside the shore
                if (!settleEach) {
                    float a = 0.0f; bool b = false;
                    const bool rr = ref.drawnAtWorld(fx, fz, a, b), rs = ref.skinDrawnAtWorld(fx, fz, a);
                    if (rr || rs) ++motionHoles;
                }
            }
            if (holeCheck && wet && ((r && s) || claims > 1)) ++doubles;
            const float lvl = r ? rt : (s ? stp : -1e30f);
            const auto key = std::make_pair(x, zz);
            if (cur.count(key)) return;
            const int src = r ? 1 : (s ? 2 : 0);
            cur[key] = {lvl, src};
            const auto it = prev.find(key);
            if (it != prev.end() && std::abs(it->second.first - lvl) > 1e-3f) {
                ++flips;
                const float dx = fx - view.x, dz = fz - view.z;
                const float d2 = dx * dx + dz * dz + eye * eye, d = std::sqrt(d2);
                const double p = 4.0 * (eye - 1.0f) / d / d2 * pxPerSr;
                px += p;
                if (fr > 0) {
                    st.pxByKind[it->second.second * 3 + src] += p;
                    const float hd = std::sqrt(dx * dx + dz * dz);
                    st.pxByDist[hd < 48.0f ? 0 : hd < 100.0f ? 1 : hd < 300.0f ? 2 : 3] += p;
                }
            }
        };
        const int w = static_cast<int>(drawM) + 24;
        const int cx = static_cast<int>(std::floor(view.x / 2.0f)) * 2, cz = static_cast<int>(std::floor(view.z / 2.0f)) * 2;
        for (int zz = cz - w; zz <= cz + w; zz += 2) for (int x = cx - w; x <= cx + w; x += 2) sample(x, zz, true);
        for (int zz = -312; zz <= 312; zz += 2) for (int x = -312; x <= 312; x += 2) {
            const float rr = std::sqrt((x + 1.0f) * (x + 1.0f) + (zz + 1.0f) * (zz + 1.0f));
            if (std::abs(rr - R) <= 8.0f) sample(x, zz, false);
        }
        if (fr > 0) {
            ++st.frames;
            if (holes) ++st.holeFrames;
            if (motionHoles) ++st.motionHoleFrames;
            st.motionHolesMax = std::max(st.motionHolesMax, motionHoles);
            st.holesMax = std::max(st.holesMax, holes); st.holesSum += holes;
            st.doublesMax = std::max(st.doublesMax, doubles);
            if (flips) ++st.flipFrames;
            st.flipsSum += flips; st.pxSum += px; st.pxMax = std::max(st.pxMax, px);
            if (px > 16.0) ++st.bigPopFrames;
            if (verbose && (holes || px > 16.0))
                std::printf("[fly] frame %d z %.1f: holes %ld doubles %ld flips %ld (%.0f px); record pending %d\n", fr, z, holes, doubles, flips, px, mgr.recordDrawStats().pending);
        }
        prev.swap(cur);
    }
    std::printf("[fly] pop px by swap: none->record %.0f, none->skin %.0f, record->none %.0f, record->skin %.0f, skin->none %.0f, skin->record %.0f, skin->skin %.0f; by distance <48 %.0f, 48-100 %.0f, 100-300 %.0f, >300 %.0f\n",
                st.pxByKind[1], st.pxByKind[2], st.pxByKind[3], st.pxByKind[5], st.pxByKind[6], st.pxByKind[7], st.pxByKind[8], st.pxByDist[0], st.pxByDist[1], st.pxByDist[2], st.pxByDist[3]);
    std::printf("[fly] motion holes in %ld frames (max %ld); holes at most %.1f m inside the shore\n", st.motionHoleFrames, st.motionHolesMax, st.holeInlandMax);
    std::printf("[fly] %.0f m/s drawM %.0f: %ld frames; holes in %ld (max %ld, sum %ld); doubles max %ld; flips in %ld frames (sum %ld, %.0f px; worst frame %.0f px; frames over 16 px %ld); skin call max %.2f ms, fields rebuilt max %ld%s\n",
                speed, drawM, st.frames, st.holeFrames, st.holesMax, st.holesSum, st.doublesMax, st.flipFrames, st.flipsSum, st.pxSum, st.pxMax, st.bigPopFrames,
                st.skinMsMax, st.fieldsRebuiltMax, settleEach ? " (settled each frame)" : "");
    return st;
}
}  // namespace

// 51.5 S4 step 1: every lake point drawn by exactly one of the record tiles or the skin in EVERY frame of the flight. Red before
// (the skin masked the tiles in range, drawn only on their build turn): holes in 1,115 of 2,100 frames at 40 m/s, max 76 points.
// Control: the same flight settled each frame has none (the metric measures motion, not the static seam).
TEST(WaterSkinTest, TheFlyInAndOutHasNoHoles) {
    const FlyStats red = flyInAndOut(40.0f, 48.0f, false, false, true);   // the red control: the S3 mask on the flight
    EXPECT_GT(red.motionHoleFrames, 100);
    for (float speed : {40.0f, 120.0f}) {
        const FlyStats st = flyInAndOut(speed, 48.0f, false);
        EXPECT_EQ(st.motionHoleFrames, 0) << speed << " m/s";
        EXPECT_EQ(st.doublesMax, 0) << speed << " m/s";
    }
}

// 51.6 S4 step 2: the skin's shore by sub-cells (a quarter of the cell's side, each wet when half its columns are, clipped to the
// wet rectangle). Prediction (before measuring): the pops at the seam (48-100 m: the 4 m rectangle's overhang appearing and going
// as record tiles hand over) fall to ~0 - level 0's sub-cells ARE the columns; the 128 m level step's pops fall with the 2 m shore.
// Red control (s_rectShoreForTest, S3's shore: the rectangle alone): the 51.5 numbers.
// MEASURED: the seam's pops 1,646 -> 0 px (the first prediction held); the level steps' 774 -> 776 px (the second did NOT - a 1 m
// shore swapped for a 2 m one changes as much as a 4 m rectangle for an 8 m one); the flight 2,729 -> 965 px, worst frame 345 -> 64.
TEST(WaterSkinTest, TheShoreSwapsAreSmall) {
    const FlyStats red = flyInAndOut(40.0f, 48.0f, false, false, false, true);
    const FlyStats st = flyInAndOut(40.0f, 48.0f, false);
    EXPECT_EQ(st.motionHoleFrames, 0);
    EXPECT_EQ(st.doublesMax, 0);
    EXPECT_GT(red.pxByDist[1], 1000.0);              // the control measures the seam's pops
    EXPECT_LT(st.pxByDist[1], 0.01 * red.pxByDist[1]);   // the seam: none
    EXPECT_LT(st.pxMax, 0.5 * red.pxMax);
}

// W6.1 (WaterCore §55.1): THE SEA DRAWN AS FAR AS THE LAND. Live (55.0) the skin's reach ended at its far_m (1 km) while the far terrain
// drew seabed to 2 km, and a farther reach costs a generator flood per never-loaded chunk column (~40 ms - 166 s to fill 1 km cold,
// 51.16). Now every never-loaded column first gets a SAMPLED summary (served first, cheap) and only those within gen_m the generator's.
// The rig (scaled down so the analytic sources stay fast): far_m 1024, gen_m 512, resident chunk columns within 200 m (the record),
// an analytic world - land (no water) for x < 300 and on an island x 600..800, |z| < 100 (past gen_m: the sampled zone); sea (bed
// 10, level 16) elsewhere. Both sources answer from the same functions; every boundary on the 4 m lattice, so they agree exactly.
// Predictions, written before the run: every 32 m sea point within far_m - 150 drawn by exactly one of the record tiles or the skin,
// at 16 within 1 mm; 0 island points drawn by the skin (the control: the sampled source draws no water over land); 0 generator jobs
// for a column past gen_m, > 0 sampled ones. RED control (today's path - no sampled source): generator jobs past gen_m > 0 (each one
// ~40 ms live). Then the viewer moves 300 m: columns entering gen_m swap sampled -> generated (upgrades > 0) and no point of the swap
// band (gen_m +- 100 m of the new view) that was drawn is ever undrawn in a frame between.
namespace {
struct SeaWorld {
    static bool sea(int x, int z) { return x >= 300 && !(x >= 600 && x < 800 && z > -100 && z < 100); }
    static void runs(int x0, int z0, int step, std::vector<ColumnRuns>& out) {   // step 1: every column; 4: one per cell, x16
        out.clear();
        for (int z = z0; z < z0 + 32; z += step) for (int x = x0; x < x0 + 32; x += step) {
            if (!sea(x + step / 2, z + step / 2)) continue;
            for (int dz = 0; dz < step; ++dz) for (int dx = 0; dx < step; ++dx) {
                ColumnRuns c; c.x = x + dx; c.z = z + dz;
                ColumnRun r; r.bottomY = 10.0f; r.topY = 16.0f; r.mass = 6.0; c.runs.push_back(r);
                out.push_back(std::move(c));
            }
        }
    }
};
struct JobLog {
    std::mutex mu;
    std::vector<glm::ivec2> gen, sampled;
    float farthest(const std::vector<glm::ivec2>& v, glm::vec2 view) {
        float worst = 0.0f;
        for (const glm::ivec2& c : v) {
            const glm::vec2 lo(c.x * 32.0f, c.y * 32.0f), hi = lo + glm::vec2(32.0f);
            const glm::vec2 q = glm::clamp(view, lo, hi);
            worst = std::max(worst, glm::length(q - view));
        }
        return worst;
    }
};
glm::vec2 vcOf(const glm::vec3& v) { return glm::vec2(std::floor(v.x / 32.0f) * 32.0f + 16.0f, std::floor(v.z / 32.0f) * 32.0f + 16.0f); }
void settleSea(WaterCoreManager& mgr) {
    for (int i = 0; i < 20000; ++i) {
        mgr.surfaceFields(); mgr.skinFields();
        if (mgr.recordDrawStats().pending == 0 && mgr.skinStats().pending == 0 && mgr.skinStats().genPending == 0) {
            mgr.skinFields();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ADD_FAILURE() << "never settled: skin pending " << mgr.skinStats().pending << ", generator " << mgr.skinStats().genPending;
}
}  // namespace

TEST(WaterSkinTest, TheSeaIsDrawnAsFarAsTheLand) {
    SkinSettingsGuard guard;
    WaterCoreManager::s_locality.drawM = 48.0f;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = true;
    WaterCoreManager::s_skin.farM = 1024.0f;
    WaterCoreManager::s_skin.genM = 512.0f;
    WaterCoreManager::s_skin.generatorThreads = 2;
    glm::vec3 view(0.0f, 30.0f, 0.0f);
    auto make = [&](WaterCoreManager& mgr, AnalyticRecord& rec, JobLog& log, bool sampledSource) {
        rec.wet = [](int x, int z) { return SeaWorld::sea(x, z); };
        rec.top = [](int, int) { return 16.0f; };
        rec.install(mgr);
        mgr.setRecordResidency([&view] { return uint64_t(1 + static_cast<uint64_t>(view.x + 100000.0f)); }, [&view](int cx, int cz) {   // the stamp moves with the viewer
            const float dx = cx * 32.0f + 16.0f - view.x, dz = cz * 32.0f + 16.0f - view.z;
            return uint64_t(dx * dx + dz * dz < 200.0f * 200.0f ? 1 : 0);
        });
        mgr.setViewPoint(view);
        WaterSkin::GeneratorFactory gen = [&log]() -> WaterSkin::GeneratorSource {
            return [&log](const glm::ivec2& c, std::vector<ColumnRuns>& out) {
                SeaWorld::runs(c.x * 32, c.y * 32, 1, out);
                std::lock_guard<std::mutex> lk(log.mu); log.gen.push_back(c);
            };
        };
        WaterSkin::GeneratorFactory smp = [&log]() -> WaterSkin::GeneratorSource {
            return [&log](const glm::ivec2& c, std::vector<ColumnRuns>& out) {
                SeaWorld::runs(c.x * 32, c.y * 32, 4, out);
                std::lock_guard<std::mutex> lk(log.mu); log.sampled.push_back(c);
            };
        };
        mgr.setSkinGeneratorSource(gen, sampledSource ? smp : WaterSkin::GeneratorFactory{});
    };
    // the red control: today's path (no sampled source) - every never-loaded column within the reach is a generator flood
    {
        WaterCoreManager red(ground); AnalyticRecord rec; JobLog log;
        make(red, rec, log, false);
        settleSea(red);
        long past = 0;
        for (const glm::ivec2& c : log.gen) if (log.farthest({c}, vcOf(view)) > 512.0f) ++past;
        std::printf("[sea] RED (no sampled source): %zu generator jobs, %ld of them past gen_m 512 m\n", log.gen.size(), past);
        EXPECT_GT(past, 100);
        red.setSkinGeneratorSource(nullptr);
    }
    WaterCoreManager mgr(ground); AnalyticRecord rec; JobLog log;
    make(mgr, rec, log, true);
    settleSea(mgr);
    struct Count { int points = 0, holes = 0, doubles = 0, bySkin = 0, island = 0, islandDrawn = 0; float levelWorst = 0.0f; };
    auto count = [&](float radius) {
        Count c;
        for (int z = -1024; z <= 1024; z += 32) for (int x = -1024; x <= 1024; x += 32) {
            const float px = x + 0.5f, pz = z + 0.5f;
            if (glm::length(glm::vec2(px - view.x, pz - view.z)) > radius) continue;
            float rt = 0.0f, st = 0.0f; bool recorded = false; int claims = 0;
            const bool r = mgr.drawnAtWorld(px, pz, rt, recorded);
            const bool s = mgr.skinDrawnAtWorld(px, pz, st, &claims);
            if (!SeaWorld::sea(x, z)) { if (x >= 600 && x < 800 && z > -100 && z < 100) { ++c.island; c.islandDrawn += s; } continue; }
            ++c.points;
            if (!r && !s) ++c.holes;
            if ((r && s) || claims > 1) ++c.doubles;
            if (s) { ++c.bySkin; c.levelWorst = std::max(c.levelWorst, std::abs(st - 16.0f)); }
        }
        return c;
    };
    const Count on = count(1024.0f - 150.0f);
    const auto& st = mgr.skinStats();
    const float genFar = log.farthest(log.gen, vcOf(view));   // the skin measures from the viewer's chunk column centre
    std::printf("[sea] %d sea points: skin %d, holes %d, doubles %d, level worst %.5f; island %d points, %d drawn by the skin\n",
                on.points, on.bySkin, on.holes, on.doubles, on.levelWorst, on.island, on.islandDrawn);
    std::printf("[sea] jobs: generator %zu (farthest column %.0f m), sampled %zu; held %d (sampled %d), upgrades %ld; levels %d\n",
                log.gen.size(), genFar, log.sampled.size(), st.genHeld, st.sampledHeld, st.upgrades, st.levels);
    EXPECT_EQ(on.holes, 0);
    EXPECT_EQ(on.doubles, 0);
    EXPECT_GT(on.bySkin, on.points / 2);
    EXPECT_LE(on.levelWorst, 1e-3f);
    EXPECT_GT(on.island, 10);
    EXPECT_EQ(on.islandDrawn, 0);
    EXPECT_LE(genFar, 512.0f);
    EXPECT_GT(st.sampledHeld, 100);
    // the swap: the viewer moves 300 m along +x; the band gen_m +- 100 m of the new view is watched every frame
    std::vector<glm::vec2> band;
    for (int z = -1024; z <= 1024; z += 16) for (int x = -1024; x <= 1024; x += 16) {
        const glm::vec2 p(x + 0.5f, z + 0.5f);
        const float d = glm::length(p - glm::vec2(300.0f, 0.0f));
        float t = 0.0f;
        if (SeaWorld::sea(x, z) && d > 412.0f && d < 612.0f && glm::length(p) < 1024.0f - 150.0f && mgr.skinDrawnAtWorld(p.x, p.y, t)) band.push_back(p);
    }
    view = glm::vec3(300.0f, 30.0f, 0.0f);
    mgr.setViewPoint(view);
    const long upgrades0 = st.upgrades;
    long holeFrames = 0, holesWorst = 0, frames = 0;
    for (int i = 0; i < 20000; ++i) {
        mgr.surfaceFields(); mgr.skinFields(); ++frames;
        long holes = 0;
        for (const glm::vec2& p : band) { float t = 0.0f, rt = 0.0f; bool rc = false; if (!mgr.skinDrawnAtWorld(p.x, p.y, t) && !mgr.drawnAtWorld(p.x, p.y, rt, rc)) ++holes; }
        if (holes) { ++holeFrames; holesWorst = std::max(holesWorst, holes); }
        if (mgr.recordDrawStats().pending == 0 && st.pending == 0 && st.genPending == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("[sea] moved 300 m: %zu band points watched over %ld frames - %ld frames with a hole (worst %ld); upgrades %ld\n",
                band.size(), frames, holeFrames, holesWorst, st.upgrades - upgrades0);
    EXPECT_GT(band.size(), 50u);
    EXPECT_EQ(holeFrames, 0);
    EXPECT_GT(st.upgrades - upgrades0, 10);
    mgr.setSkinGeneratorSource(nullptr);
}

// §56 (the sign-off sweep, found filming A4): ONE write by the core - a pond recorded, a volume slept or removed - re-read every
// far-skin summary in reach: on a world with no streaming generator every column within far_m counts as resident, so ~58,000 summaries
// at 4 km, 32 a frame, 16-19 ms of main-thread CPU a frame for ~30 s (WaterBench_Small, live). The record tiles had the fix since
// 29 S4 (a tile the write did not touch stays up to date); the skin now has the same rule, per chunk column.
// Prediction: a 4 x 4 m pond recorded 200 m off a 3 km lake, all summaries built first - the one chunk column the pond touches is
// re-read (at least 1, at most 2), nothing left pending after the first call; the red (the old rule, s_skinAllStaleForTest)
// re-reads every summary the settle built.
TEST(WaterSkinTest, ACoreWriteReReadsOnlyTheColumnsItTouched) {
    SkinSettingsGuard guard;
    WaterCoreManager::s_locality.drawM = 48.0f;
    WaterCoreManager::s_recordDraw.on = true;
    WaterCoreManager::s_skin.on = true;
    WaterCoreManager::s_skin.farM = 1024.0f;
    struct Out { long summaries = 0, reread = 0; int firstPending = 0, calls = 0; };
    auto run = [](bool red) {
        Out o;
        WaterCoreManager::s_skinAllStaleForTest = red;
        WaterCoreManager mgr(ground);
        const int half = 1536;
        uint64_t rev = 1;
        std::map<std::pair<int, int>, float> written;   // what the core wrote: column -> its top (0 = cleared)
        auto wet = [half](int x, int z) { return x >= -half && x < half && z >= -half && z < half; };
        mgr.setSpanIo(
            [&](const std::vector<ColumnRuns>& runs, float, float, long* unwritten, long* chunks) {
                for (const auto& c : runs) written[{c.x, c.z}] = c.runs.empty() ? 0.0f : c.runs.back().topY;
                ++rev;
                if (unwritten) *unwritten = 0;
                if (chunks) *chunks = 1;
            },
            [&](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                out.clear();
                const float yLo = std::max(0.0f, static_cast<float>(lo.y)), yHi = static_cast<float>(hi.y + 1);
                for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                    float top = wet(x, z) ? 1.0f : 0.0f;
                    if (const auto it = written.find({x, z}); it != written.end()) top = it->second;
                    ColumnRun r; r.bottomY = yLo; r.topY = std::min(top, yHi);
                    if (r.topY <= r.bottomY) continue;
                    r.mass = static_cast<double>(r.topY - r.bottomY);
                    ColumnRuns c; c.x = x; c.z = z; c.runs.push_back(r);
                    out.push_back(std::move(c));
                }
            });
        mgr.setSpanRevisionQuery([&rev] { return rev; });
        mgr.setViewPoint(glm::vec3(0.0f, 6.0f, static_cast<float>(-half) - 10.0f));
        settle(mgr);
        o.summaries = mgr.skinStats().summaries;
        // the pond: 4 x 4 m on dry land, inside one chunk column (x 64..67, z -1760..-1757)
        const int pz = -half - 224;
        mgr.recordLake(glm::ivec3(64, 0, pz), glm::ivec3(67, 0, pz + 3), 0.0f, 1.0f, WaterCoreManager::SleptVolume{});
        mgr.skinFields();
        o.reread = mgr.skinStats().summariesBuiltLastCall;
        o.firstPending = mgr.skinStats().pending;
        o.calls = 1;
        while (mgr.skinStats().pending > 0 && o.calls < 4000) { mgr.skinFields(); o.reread += mgr.skinStats().summariesBuiltLastCall; ++o.calls; }
        WaterCoreManager::s_skinAllStaleForTest = false;
        return o;
    };
    const Out g = run(false), r = run(true);
    std::printf("[skin 56] green: %ld summaries; the write re-read %ld (first call pending %d, %d calls)\n",
                g.summaries, g.reread, g.firstPending, g.calls);
    std::printf("[skin 56] red (every summary stale on a write): re-read %ld over %d calls\n", r.reread, r.calls);
    EXPECT_GT(r.reread, r.summaries / 2);   // the control measures the defect
    EXPECT_GE(g.reread, 1);                 // the touched column is read again (it holds the pond now)
    EXPECT_LE(g.reread, 2);
    EXPECT_EQ(g.firstPending, 0);
}
