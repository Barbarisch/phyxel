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
    std::printf("[skin] round lake r 70, wet rectangle: %d lake points, %d holes (%.2f %%); %d off-lake points drawn (%.2f %% of the lake), worst %.1f m out\n",
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
