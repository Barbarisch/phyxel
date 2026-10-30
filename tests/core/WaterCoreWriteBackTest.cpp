#include <gtest/gtest.h>

#include "core/water/WaterCore.h"
#include "core/water/WaterSurfaceMesh.h"

#include <cmath>
#include <functional>
#include <vector>

// WaterCore Phase D slice D1 (docs/WaterCore.md 16.2): an active volume writes itself back to the
// world as RUNS per voxel column, a pure function of its cells, mass-exact by definition; the
// inverse seeds a volume from runs. These are the grid-only tests (no chunks, no engine); the
// chunk clipping has its own file (WaterSpanWriteBackTest) and the live rows are S11.

namespace Phyxel::Core::Water {
namespace {

constexpr float kDt = 1.0f / 60.0f;

struct Tank {
    WaterGrid grid;
    Tank(int nx, int ny, int nz, float h) : grid(GridSpec{glm::ivec3(0), glm::ivec3(nx, ny, nz), h}) {}
    SolidQuery query() const {
        const GridSpec s = grid.spec();
        return [s](const glm::ivec3& cw) -> Occ {
            const glm::ivec3 c = cw - s.origin;
            if (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= s.dims.x || c.y >= s.dims.y || c.z >= s.dims.z) return Occ::Solid;
            return Occ::Air;
        };
    }
};

/// The fixture of 16.7: a 2x2-voxel box at h = 1/3 (per = 3, 4 voxel layers). The WEST voxel
/// columns hold a pool under a shelf (layers 0-4 full, 5 at 0.4), a dry layer 6 (the shelf), and
/// water on the shelf (7-8 full, 9 at 0.25): TWO runs. The EAST columns hold one run (0-9 full,
/// 10 at 0.5).
Tank shelfTank() {
    Tank t(6, 12, 6, 1.0f / 3.0f);
    for (int gz = 0; gz < 6; ++gz) {
        for (int gx = 0; gx < 3; ++gx) {
            for (int gy = 0; gy <= 4; ++gy) t.grid.f(gx, gy, gz) = 1.0f;
            t.grid.f(gx, 5, gz) = 0.4f;
            t.grid.f(gx, 7, gz) = 1.0f; t.grid.f(gx, 8, gz) = 1.0f; t.grid.f(gx, 9, gz) = 0.25f;
        }
        for (int gx = 3; gx < 6; ++gx) {
            for (int gy = 0; gy <= 9; ++gy) t.grid.f(gx, gy, gz) = 1.0f;
            t.grid.f(gx, 10, gz) = 0.5f;
        }
    }
    return t;
}

const ColumnRuns* column(const std::vector<ColumnRuns>& v, int x, int z) {
    for (const auto& c : v) if (c.x == x && c.z == z) return &c;
    return nullptr;
}

TEST(WaterCoreWriteBackTest, ColumnRunsAreMassExact) {
    Tank t = shelfTank();
    std::vector<ColumnRuns> runs;
    const WriteBackStats st = columnRunsFromGrid(t.grid, nullptr, runs);
    ASSERT_EQ(st.columns, 4);
    EXPECT_EQ(st.runs, 2 * 2 + 1 * 2) << "two runs per west column (under + on the shelf), one per east";
    EXPECT_EQ(st.heldColumns, 0);
    // mass-exact: the sum of run depths × 1 m² IS the grid's mass
    EXPECT_NEAR(st.mass, t.grid.totalMass(), 1e-6 * st.mass);   // float32 fills: 0.4f and 0.25f are not exact
    EXPECT_NEAR(st.mass, 2 * (1.8 + 0.75) + 2 * 3.5, 1e-6 * 12.1);
    const ColumnRuns* w = column(runs, 0, 1);
    ASSERT_NE(w, nullptr);
    ASSERT_EQ(w->runs.size(), 2u);
    EXPECT_NEAR(w->runs[0].bottomY, 0.0f, 1e-6f);
    EXPECT_NEAR(w->runs[0].topY, 1.8f, 1e-5f) << "5 full layers + 0.4, at 1/3 m";
    EXPECT_NEAR(w->runs[1].bottomY, 7.0f / 3.0f, 1e-5f) << "the run on the shelf starts at layer 7";
    EXPECT_NEAR(w->runs[1].topY, 7.0f / 3.0f + 2.25f / 3.0f, 1e-5f);
    EXPECT_NEAR(w->runs[1].mass, 0.75, 1e-6);
    const ColumnRuns* e = column(runs, 1, 0);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(e->runs.size(), 1u);
    EXPECT_NEAR(e->runs[0].topY, 3.5f, 1e-5f);
    // full interiors: the geometric surface and the mass top agree exactly, no spread
    EXPECT_NEAR(st.surfaceVsMassMmMax, 0.0f, 1e-3f);
    EXPECT_NEAR(st.spreadMmMax, 0.0f, 1e-3f);
}

TEST(WaterCoreWriteBackTest, AnInteriorLayerShortOfFullIsMeasuredNotHidden) {
    // S11's "spans equal the surface +- 1 mm" is a NUMBER the write-back reports: an interior layer
    // at 0.9 under a full surface moves the mass top down by 0.1 x h = 33 mm while the geometric
    // surface stays - the record says 33.3, not "ok".
    Tank t = shelfTank();
    for (int gz = 0; gz < 6; ++gz) for (int gx = 0; gx < 3; ++gx) t.grid.f(gx, 2, gz) = 0.9f;
    std::vector<ColumnRuns> runs;
    const WriteBackStats st = columnRunsFromGrid(t.grid, nullptr, runs);
    EXPECT_NEAR(st.surfaceVsMassMmMax, 100.0f / 3.0f, 0.05f);
    EXPECT_NEAR(st.mass, t.grid.totalMass(), 1e-6) << "the mass top is still the truth";
    EXPECT_NEAR(column(runs, 0, 0)->runs[0].topY, 1.8f - 0.1f / 3.0f, 1e-5f);
}

TEST(WaterCoreWriteBackTest, SubMillimetreLayersFoldIntoTheTopRun) {
    // a 0.5 mm film (f = 0.0015 at 1/3 m) in transit above a full layer, and a column holding
    // ONLY such a film: the first is stored mass-exact (one run, no second run), the second is
    // counted as residue - nothing is dropped silently (R3 Small lost 2.9e-3 m^3, 2026-10-09)
    Tank t(3, 6, 3, 1.0f / 3.0f);
    for (int gz = 0; gz < 3; ++gz) for (int gx = 0; gx < 3; ++gx) { t.grid.f(gx, 0, gz) = 1.0f; t.grid.f(gx, 2, gz) = 0.0015f; }
    Tank u(3, 6, 3, 1.0f / 3.0f);
    for (int gz = 0; gz < 3; ++gz) for (int gx = 0; gx < 3; ++gx) u.grid.f(gx, 3, gz) = 0.0015f;
    std::vector<ColumnRuns> runs;
    WriteBackStats st = columnRunsFromGrid(t.grid, nullptr, runs);
    ASSERT_EQ(runs.size(), 1u);
    ASSERT_EQ(runs[0].runs.size(), 1u) << "the film is not a run of its own";
    EXPECT_NEAR(st.mass, t.grid.totalMass(), 1e-7 * t.grid.totalMass()) << "mass exact including the film (float32 top arithmetic)";
    EXPECT_NEAR(runs[0].runs[0].topY, (1.0f + 0.0015f) / 3.0f, 1e-6f);
    EXPECT_EQ(st.thinDropped, 0.0);
    st = columnRunsFromGrid(u.grid, nullptr, runs);
    EXPECT_EQ(st.runs, 0);
    EXPECT_NEAR(st.thinDropped, u.grid.totalMass(), 1e-9) << "residue is counted, never silently lost";
}

TEST(WaterCoreWriteBackTest, AFilmOnlyColumnJoinsTheNearestRunSoASleepLosesNothing) {
    // A11 (44.8): a splash leaves film on dry columns beside the pond; the write-back dropped it (0.3-0.5 L a splash, 44.7).
    // Four voxel columns in a row at h = 1/3: x = 0 and x = 2 hold a pond (one full layer), x = 1 and x = 3 only a 0.5 mm film.
    // Predicted: no residue; x = 1's film joins x = 0 (equidistant from 0 and 2: the tie goes to the lower x), x = 3's joins x = 2;
    // the written mass is the grid's mass. RED before A11: thinDropped = both films, st.mass short by them.
    Tank t(12, 6, 3, 1.0f / 3.0f);
    for (int gz = 0; gz < 3; ++gz) for (int gx = 0; gx < 12; ++gx) {
        const int vx = gx / 3;
        if (vx == 0 || vx == 2) t.grid.f(gx, 0, gz) = 1.0f;
        else t.grid.f(gx, 2, gz) = 0.0015f;
    }
    const double film = 9.0 * 0.0015 / 27.0;   // one column's film: 9 sub-columns x f x h^3
    std::vector<ColumnRuns> runs;
    WriteBackStats st = columnRunsFromGrid(t.grid, nullptr, runs);
    EXPECT_EQ(st.thinDropped, 0.0) << "nothing left as residue while a column has a run";
    EXPECT_EQ(st.filmColumns, 2);
    EXPECT_NEAR(st.filmJoined, 2.0 * film, 1e-9);
    EXPECT_NEAR(st.mass, t.grid.totalMass(), 1e-7 * t.grid.totalMass()) << "the sleep is mass-exact";
    EXPECT_NEAR(column(runs, 0, 0)->runs[0].topY, (1.0f + 0.0015f) / 3.0f, 1e-6f) << "x = 1's film joined x = 0 (the tie: lower x)";
    EXPECT_NEAR(column(runs, 2, 0)->runs[0].topY, (1.0f + 0.0015f) / 3.0f, 1e-6f) << "x = 3's film joined x = 2 (nearest)";
    EXPECT_TRUE(column(runs, 1, 0)->runs.empty()) << "a film column gets no run of its own";
    EXPECT_NEAR(st.filmJoinMaxM, 1.0f, 1e-6f);
    // a held column is never a destination: hold x = 0 and x = 1's film goes to x = 2 (one column farther)
    st = columnRunsFromGrid(t.grid, [](int x, int) { return x == 0; }, runs);
    EXPECT_EQ(st.thinDropped, 0.0);
    EXPECT_NEAR(column(runs, 2, 0)->runs[0].topY, (1.0f + 2.0f * 0.0015f) / 3.0f, 1e-6f) << "both films at x = 2";
    EXPECT_NEAR(st.mass + st.heldMass, t.grid.totalMass(), 1e-7 * t.grid.totalMass());
}

TEST(WaterCoreWriteBackTest, HeldColumnsAreReportedNotWritten) {
    Tank t = shelfTank();
    std::vector<ColumnRuns> runs;
    const WriteBackStats st = columnRunsFromGrid(t.grid, [](int x, int) { return x == 1; }, runs);
    EXPECT_EQ(st.columns, 2) << "the two west columns are written";
    EXPECT_EQ(st.heldColumns, 2);
    EXPECT_NEAR(st.mass, 2 * (1.8 + 0.75), 1e-6);
    EXPECT_NEAR(st.heldMass, 7.0, 1e-6) << "held water is counted, never dropped";
    const ColumnRuns* e = column(runs, 1, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(e->held);
    EXPECT_TRUE(e->runs.empty());
    EXPECT_NEAR(e->heldMass, 3.5, 1e-6);
}

TEST(WaterCoreWriteBackTest, SeedIsTheExactInverse) {
    Tank src = shelfTank();
    std::vector<ColumnRuns> runs;
    columnRunsFromGrid(src.grid, nullptr, runs);
    Tank dst(6, 12, 6, 1.0f / 3.0f);
    dst.grid.u(1, 1, 1) = 3.0f;   // must be cleared: a seeded volume starts at rest
    const double placed = seedGridFromRuns(dst.grid, runs);
    EXPECT_NEAR(placed, src.grid.totalMass(), 1e-6);
    float maxDiff = 0.0f;
    for (size_t i = 0; i < src.grid.fData().size(); ++i) maxDiff = std::max(maxDiff, std::abs(src.grid.fData()[i] - dst.grid.fData()[i]));
    EXPECT_LT(maxDiff, 1e-6f) << "uniform columns round-trip cell for cell";
    EXPECT_EQ(dst.grid.u(1, 1, 1), 0.0f);
}

// §27.8 (W1 step 0, measured live: rig C woke 2.2222 m^3 into 1,620 rock cells, the surface ~6 cm low). The record is per 1 m
// column; the cells are 1/9 m. A 1/9 m step INSIDE a voxel column (Stone microcubes under 5 of its 9 sub-columns along x, the
// floor layer) with still water 1 m deep over it: sleep -> wake must put no water in the rock, keep the books exact, and give
// back the same cells (still water is level, so filling the column's air cells from the bottom is the shape it had).
// Two voxel columns: the west one stepped, the east one flat (the control - it must round-trip as before).
TEST(WaterCoreWriteBackTest, SeedOnASteppedBedPutsNoWaterInRock) {
    const float h = 1.0f / 9.0f;
    Tank src(18, 18, 9, h);
    auto rock = [](int gx, int gy) { return gy == 0 && gx < 5; };   // the step: west column, sub-columns 0-4 along x, floor layer
    for (int gz = 0; gz < 9; ++gz) for (int gy = 0; gy < 18; ++gy) for (int gx = 0; gx < 18; ++gx) {
        if (rock(gx, gy)) { src.grid.occ(gx, gy, gz) = Occ::Solid; continue; }
        if (gy < 9) src.grid.f(gx, gy, gz) = 1.0f;   // still water to 1 m, in the air cells only (placeBox's rule)
    }
    std::vector<ColumnRuns> runs;
    const WriteBackStats st = columnRunsFromGrid(src.grid, nullptr, runs);
    ASSERT_NEAR(st.mass, src.grid.totalMass(), 1e-6);

    Tank dst(18, 18, 9, h);
    for (int gz = 0; gz < 9; ++gz) for (int gy = 0; gy < 18; ++gy) for (int gx = 0; gx < 18; ++gx)
        if (rock(gx, gy)) dst.grid.occ(gx, gy, gz) = Occ::Solid;
    const double placed = seedGridFromRuns(dst.grid, runs);
    double inRock = 0.0; long rockCells = 0; float maxDiff = 0.0f;
    const double h3 = static_cast<double>(h) * h * h;
    for (int gz = 0; gz < 9; ++gz) for (int gy = 0; gy < 18; ++gy) for (int gx = 0; gx < 18; ++gx) {
        const float f = dst.grid.f(gx, gy, gz);
        if (rock(gx, gy) && f > 0.0f) { inRock += f * h3; ++rockCells; }
        maxDiff = std::max(maxDiff, std::abs(f - src.grid.f(gx, gy, gz)));
    }
    EXPECT_EQ(rockCells, 0) << inRock << " m^3 woken into " << rockCells << " rock cells (the seed must fill air cells only)";
    EXPECT_NEAR(placed, st.mass, 1e-6) << "the books: every m^3 of the record is placed";
    EXPECT_NEAR(dst.grid.totalMass(), src.grid.totalMass(), 1e-6);
    EXPECT_LT(maxDiff, 1e-5f) << "still water over a step comes back cell for cell";
    RecordProperty("in_rock_m3", std::to_string(inRock));
}

// §27 W1a (the owner, 2026-10-20: "go with A"): RECORDED water is drawn with the pond surface built from the record, so a pond
// going to sleep changes nothing on screen. The record's field must equal the field the awake water drew at rest - per
// sub-column the same runs, tops, bottoms and ground - on the stepped bed (the hard case: the record is per 1 m column).
TEST(WaterCoreWriteBackTest, TheRecordDrawsTheSurfaceTheWaterHad) {
    const float h = 1.0f / 9.0f;
    Tank src(18, 18, 9, h);
    auto rock = [](int gx, int gy) { return gy == 0 && gx < 5; };
    for (int gz = 0; gz < 9; ++gz) for (int gy = 0; gy < 18; ++gy) for (int gx = 0; gx < 18; ++gx) {
        if (rock(gx, gy)) { src.grid.occ(gx, gy, gz) = Occ::Solid; continue; }
        if (gy < 9) src.grid.f(gx, gy, gz) = 1.0f;
        if (gy == 9) src.grid.f(gx, gy, gz) = 0.37f;   // a part-full top cell: the level is not on a cell boundary
    }
    WaterSurfaceField awake;
    extractSurfaceField(src.grid, awake);
    std::vector<ColumnRuns> runs;
    columnRunsFromGrid(src.grid, nullptr, runs);

    Tank scratch(18, 18, 9, h);   // the tile's grid: only its occupancy is known (the world's), no water
    for (int gz = 0; gz < 9; ++gz) for (int gx = 0; gx < 18; ++gx) for (int gy = 0; gy < 18; ++gy) if (rock(gx, gy)) scratch.grid.occ(gx, gy, gz) = Occ::Solid;
    WaterSurfaceField rec;
    recordSurfaceField(scratch.grid, runs, rec);
    ASSERT_EQ(rec.nx, awake.nx); ASSERT_EQ(rec.nz, awake.nz); ASSERT_EQ(rec.origin, awake.origin);
    float worst = 0.0f; int runDiff = 0;
    for (size_t i = 0; i < awake.cols.size(); ++i) {
        const SurfaceColumn& a = awake.cols[i]; const SurfaceColumn& b = rec.cols[i];
        if (a.runs != b.runs) { ++runDiff; continue; }
        for (int k = 0; k < static_cast<int>(a.runs); ++k) worst = std::max({worst, std::abs(a.top[k] - b.top[k]), std::abs(a.bottom[k] - b.bottom[k])});
        if (a.solidTopY > -1e29f || b.solidTopY > -1e29f) worst = std::max(worst, std::abs(a.solidTopY - b.solidTopY));
    }
    EXPECT_EQ(runDiff, 0) << "sub-columns whose run count differs between the awake field and the record's";
    EXPECT_LT(worst, 1e-4f) << "m: the record's surface vs the awake one, per sub-column (rule 4: < 1 mm)";
    RecordProperty("worst_m", std::to_string(worst));
}

TEST(WaterCoreWriteBackTest, ReloadAtDifferentCellSizeKeepsColumnMass) {
    // 14.3: spans are cell-size-free. Runs written at 1/3 m seed a 1 m and a 1/9 m volume over the
    // same voxel box; every voxel column's mass is identical.
    Tank src = shelfTank();
    std::vector<ColumnRuns> runs;
    columnRunsFromGrid(src.grid, nullptr, runs);
    const auto ref = src.grid.massPerVoxelColumn();
    for (float h : {1.0f, 1.0f / 9.0f}) {
        const int per = static_cast<int>(std::lround(1.0f / h));
        Tank dst(2 * per, 4 * per, 2 * per, h);
        seedGridFromRuns(dst.grid, runs);
        const auto got = dst.grid.massPerVoxelColumn();
        ASSERT_EQ(got.size(), ref.size()) << "h = " << h;
        for (const auto& [col, m] : ref) {
            bool found = false;
            for (const auto& [c2, m2] : got) if (c2 == col) { EXPECT_NEAR(m2, m, 1e-6) << "column " << col.x << "," << col.y << " at h = " << h; found = true; }
            EXPECT_TRUE(found);
        }
    }
}

TEST(WaterCoreWriteBackTest, ReWakeFromRunsChangesNothing) {
    // 5.2: a dam break settles, is written back, is woken from its runs; 60 more ticks change
    // nothing (mass exact, cells within the quiet threshold), and the second write-back is the
    // first. The measured numbers are reported, not assumed.
    Tank t(9, 9, 9, 1.0f / 3.0f);
    t.grid.fillBox(glm::ivec3(3, 3, 3), glm::ivec3(5, 5, 5), 1.0f);   // a 1 m block dropped into the empty tank: splash, spread, settle
    WaterSolver s(t.grid, t.query());
    int ticks = 0;
    while (!s.asleep() && ticks < 6000) { s.step(kDt); ++ticks; }
    ASSERT_TRUE(s.asleep()) << "the reference solver must rest a dropped block in a 3 m tank within 100 s";
    std::vector<ColumnRuns> runs;
    const WriteBackStats st = columnRunsFromGrid(t.grid, nullptr, runs);
    EXPECT_NEAR(st.mass, t.grid.totalMass(), 1e-6);
    EXPECT_LT(st.surfaceVsMassMmMax, 1.0f) << "S11: the rested interior is full to 1 mm";
    EXPECT_LT(st.spreadMmMax, 1.0f) << "S11: the rested surface is flat across a voxel column to 1 mm";

    Tank w(9, 9, 9, 1.0f / 3.0f);
    const double seeded = seedGridFromRuns(w.grid, runs);
    EXPECT_NEAR(seeded, st.mass, 1e-6);
    const std::vector<float> f0 = w.grid.fData();
    WaterSolver s2(w.grid, w.query());
    for (int i = 0; i < 60; ++i) s2.step(kDt);
    EXPECT_NEAR(w.grid.totalMass(), seeded, 1e-9 * seeded * 60 + 1e-9);
    float maxDf = 0.0f;
    for (size_t i = 0; i < f0.size(); ++i) maxDf = std::max(maxDf, std::abs(f0[i] - w.grid.fData()[i]));
    EXPECT_LT(maxDf, 1e-3f) << "a woken rest state is a rest state";
    std::vector<ColumnRuns> runs2;
    columnRunsFromGrid(w.grid, nullptr, runs2);
    ASSERT_EQ(runs2.size(), runs.size());
    for (size_t i = 0; i < runs.size(); ++i) {
        ASSERT_EQ(runs2[i].runs.size(), runs[i].runs.size()) << "column " << runs[i].x << "," << runs[i].z;
        for (size_t k = 0; k < runs[i].runs.size(); ++k) EXPECT_NEAR(runs2[i].runs[k].topY, runs[i].runs[k].topY, 1e-3f);
    }
    RecordProperty("rest_ticks", ticks);
    RecordProperty("surface_vs_mass_mm", st.surfaceVsMassMmMax);
    RecordProperty("spread_mm", st.spreadMmMax);
}


}  // namespace
}  // namespace Phyxel::Core::Water
