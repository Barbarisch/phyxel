// WaterCoreTest — docs/WaterCore.md §15.2: the §4.4 correctness rules made executable, on
// synthetic grids inside ONE active volume. Written RED FIRST against the Phase B strawman stub
// (gravity + naive flux, no solids, no projection, no rest): every physical test here must fail
// on it and pass on the real tick. Predictions are stated in each test before the run.
#include <gtest/gtest.h>
#include "core/water/WaterCore.h"
#include <cmath>
#include <cstdio>
#include <random>

using namespace Phyxel::Core::Water;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr double kG = 9.81;

/// A closed tank: everything outside the grid is Solid; inside is Air unless `extra` says so.
struct Tank {
    WaterGrid grid;
    std::function<Occ(const glm::ivec3&)> extra;   // grid-LOCAL cell -> occupancy override (Air = none)
    Tank(int nx, int ny, int nz, float h = 1.0f)
        : grid(GridSpec{glm::ivec3(0), glm::ivec3(nx, ny, nz), h}) {}
    SolidQuery query() const {
        const GridSpec s = grid.spec();
        auto ex = extra;
        return [s, ex](const glm::ivec3& cw) -> Occ {
            const glm::ivec3 c = cw - s.origin;
            if (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= s.dims.x || c.y >= s.dims.y || c.z >= s.dims.z) return Occ::Solid;
            return ex ? ex(c) : Occ::Air;
        };
    }
};

double runTicks(WaterSolver& s, int ticks, float dt = kDt) {
    double t = 0.0;
    for (int i = 0; i < ticks; ++i) { s.step(dt); t += dt; }
    return t;
}

float maxSpeedOnGrid(WaterGrid& g) {
    float m = 0.0f;
    for (float x : g.uData()) m = std::max(m, std::abs(x));
    for (float x : g.vData()) m = std::max(m, std::abs(x));
    for (float x : g.wData()) m = std::max(m, std::abs(x));
    return m;
}

int frontX(const WaterGrid& g, int z, double minColumnMass) {
    int front = -1;
    for (int x = 0; x < g.nx(); ++x)
        if (g.columnMass(x, z) > minColumnMass) front = x;
    return front;
}

} // namespace

// P1 — mass is the truth: transport never creates or destroys fill, whatever the velocity field.
TEST(WaterCoreTest, ConservationUnderArbitraryVelocity) {
    Tank t(16, 8, 16);
    t.grid.fillBox({0, 0, 0}, {15, 2, 15}, 1.0f);
    t.grid.fillBox({4, 3, 4}, {11, 4, 11}, 0.5f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(-2.0f, 2.0f);
    for (float& x : t.grid.uData()) x = U(rng);
    for (float& x : t.grid.vData()) x = U(rng);
    for (float& x : t.grid.wData()) x = U(rng);
    SolverParams p; p.gravity = 0.0f;
    WaterSolver s(t.grid, t.query(), p);
    const double m0 = t.grid.totalMass();
    runTicks(s, 1000);
    // Prediction: |delta m| / m0 <= 1e-4 after 1000 ticks (float rounding only).
    EXPECT_NEAR(t.grid.totalMass(), m0, 1e-4 * m0) << "transport minted or destroyed mass";
    for (float fv : t.grid.fData()) { EXPECT_GE(fv, -1e-6f); EXPECT_LE(fv, 1.0f + 1e-5f); }
}

// P2/P7 — a resting pool under gravity does not move and its surface does not change.
TEST(WaterCoreTest, StillWaterStaysStill) {
    Tank t(8, 8, 8);
    t.grid.fillBox({0, 0, 0}, {7, 3, 7}, 1.0f);
    WaterSolver s(t.grid, t.query());
    runTicks(s, 100);
    // Prediction: max |v| < 1e-6 m/s and every column surface exactly 4.0 after 100 ticks.
    EXPECT_LT(maxSpeedOnGrid(t.grid), 1e-6f) << "still water acquired velocity";
    for (int z = 0; z < 8; ++z) for (int x = 0; x < 8; ++x)
        EXPECT_NEAR(t.grid.surfaceWorldY(x, z), 4.0f, 1e-5f);
    EXPECT_NEAR(t.grid.totalMass(), 8.0 * 8.0 * 4.0, 1e-6);
}

// P2 — the pressure in a resting column is hydrostatic: p(depth d) = rho g d (rho = 1).
TEST(WaterCoreTest, HydrostaticPressure) {
    Tank t(4, 12, 4);
    t.grid.fillBox({0, 0, 0}, {3, 9, 3}, 1.0f);          // 10 deep, surface at y = 10
    WaterSolver s(t.grid, t.query());
    runTicks(s, 5);
    // Prediction: at cell y (centre depth 10 - y - 0.5) p = g * depth within 1 %.
    for (int y = 0; y < 9; ++y) {
        const double depth = 10.0 - (y + 0.5);
        EXPECT_NEAR(s.pressure(1, y, 1), kG * depth, 0.01 * kG * depth) << "y=" << y;
    }
}

// P5 — a solid slab with no hole admits no water below it, however long the run.
TEST(WaterCoreTest, SealedCavityGainsNothing) {
    Tank t(8, 10, 8);
    t.extra = [](const glm::ivec3& c) { return c.y == 4 ? Occ::Solid : Occ::Air; };   // full slab at y = 4
    t.grid.fillBox({0, 5, 0}, {7, 8, 7}, 1.0f);           // water above the slab
    WaterSolver s(t.grid, t.query());
    const double m0 = t.grid.totalMass();
    runTicks(s, 300);
    double below = 0.0;
    for (int z = 0; z < 8; ++z) for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) below += t.grid.f(x, y, z);
    // Prediction: exactly 0 below the slab; total unchanged.
    EXPECT_EQ(below, 0.0) << "water leaked through a solid slab";
    EXPECT_NEAR(t.grid.totalMass(), m0, 1e-6);
}

// A 1-cell hole in the floor drains at Torricelli's rate with the sharp-orifice coefficient 0.62.
TEST(WaterCoreTest, OpenHoleDrainsAtTorricelli) {
    Tank t(10, 18, 10);
    // floor slab at y = 5 with one hole at (4,5,4); the region below is a sink that empties itself
    t.extra = [](const glm::ivec3& c) { return (c.y == 5 && !(c.x == 4 && c.z == 4)) ? Occ::Solid : Occ::Air; };
    t.grid.fillBox({0, 6, 0}, {9, 15, 9}, 1.0f);          // head h0 = 10 over the floor
    WaterSolver s(t.grid, t.query());
    s.addSource({4, 0, 4}, -1000.0f);                     // bottomless sink under the hole
    const double area = 100.0, hole = 1.0, h0 = 10.0;
    // Prediction (Torricelli): dh/dt = -(cd*A_h/A_t) sqrt(2 g h); time for h to fall from 10 to 5:
    // t = (sqrt(h0) - sqrt(5)) * 2 / (cd * A_h / A_t * sqrt(2g)). The hole is a 1-cell opening in
    // a 1-cell-thick slab = a SHORT TUBE (L/D = 1), not a sharp-edged thin-plate orifice: cd ~ 0.8
    // (Idelchik; a sharp orifice would be 0.62). t_half = 26.1 s. Measured head = mass above the
    // floor / area. Tolerance 20 %.
    const double cd = 0.8;
    const double tHalf = (std::sqrt(h0) - std::sqrt(5.0)) * 2.0 / (cd * hole / area * std::sqrt(2.0 * kG));
    double elapsed = 0.0, head = h0;
    while (head > 5.0 && elapsed < 3.0 * tHalf) { s.step(kDt); elapsed += kDt;
        double above = 0.0;
        for (int z = 0; z < 10; ++z) for (int y = 6; y < 18; ++y) for (int x = 0; x < 10; ++x) above += t.grid.f(x, y, z);
        head = above / area; }
    EXPECT_NEAR(elapsed, tHalf, 0.2 * tHalf) << "drain time to half head off by more than 20 %";
}

// P5 — no flux across a Solid face, at every resolution the engine ships.
TEST(WaterCoreTest, SolidFacesCarryNoFlux) {
    for (float h : {1.0f, 1.0f / 3.0f, 1.0f / 9.0f}) {
        Tank t(9, 9, 9, h);
        t.extra = [](const glm::ivec3& c) { return (c.x >= 3 && c.x <= 5 && c.z >= 3 && c.z <= 5 && c.y <= 6) ? Occ::Solid : Occ::Air; }; // pillar
        t.grid.fillBox({0, 0, 0}, {8, 4, 8}, 1.0f);
        for (float& x : t.grid.uData()) x = 1.0f;         // push everything +x into the pillar
        WaterSolver s(t.grid, t.query());
        runTicks(s, 60);
        double inPillar = 0.0;
        for (int z = 3; z <= 5; ++z) for (int y = 0; y <= 6; ++y) for (int x = 3; x <= 5; ++x) inPillar += t.grid.f(x, y, z);
        EXPECT_EQ(inPillar, 0.0) << "water entered a solid pillar at h=" << h;
        // the faces of the pillar carry zero normal velocity
        for (int z = 3; z <= 5; ++z) for (int y = 0; y <= 6; ++y) { EXPECT_EQ(t.grid.u(3, y, z), 0.0f); EXPECT_EQ(t.grid.u(6, y, z), 0.0f); }
    }
}

// Unknown occupancy is a hold wall this tick and releases, mass intact, when it becomes known.
TEST(WaterCoreTest, HoldFacesCarryNoFluxAndRelease) {
    Tank t(16, 6, 4);
    bool known = false;
    t.extra = [&known](const glm::ivec3& c) { return (!known && c.x >= 8) ? Occ::Unknown : Occ::Air; };
    t.grid.fillBox({0, 0, 0}, {5, 3, 3}, 1.0f);
    WaterSolver s(t.grid, t.query());
    const double m0 = t.grid.totalMass();
    runTicks(s, 120);
    double beyond = 0.0;
    for (int z = 0; z < 4; ++z) for (int y = 0; y < 6; ++y) for (int x = 8; x < 16; ++x) beyond += t.grid.f(x, y, z);
    EXPECT_EQ(beyond, 0.0) << "water flowed into unknown ground";
    known = true;
    s.refreshSolids();
    s.wake();
    runTicks(s, 240);
    beyond = 0.0;
    for (int z = 0; z < 4; ++z) for (int y = 0; y < 6; ++y) for (int x = 8; x < 16; ++x) beyond += t.grid.f(x, y, z);
    EXPECT_GT(beyond, 1.0) << "water did not resume after the hold released";
    EXPECT_NEAR(t.grid.totalMass(), m0, 1e-4 * m0);
}

// CFL: the substep count is computed from max|v|, dt, the fraction and h — stated, not discovered.
TEST(WaterCoreTest, SubstepCountMatchesCFL) {
    for (float h : {1.0f, 1.0f / 9.0f}) {
        Tank t(6, 6, 6, h);
        t.grid.fillBox({0, 0, 0}, {5, 2, 5}, 1.0f);
        for (float& x : t.grid.uData()) x = 5.0f;
        SolverParams p; p.gravity = 0.0f;
        WaterSolver s(t.grid, t.query(), p);
        const StepReport r = s.step(kDt);
        const int expected = std::max(1, static_cast<int>(std::ceil(5.0 * kDt / (p.cflFraction * h))));
        EXPECT_EQ(r.substeps, expected) << "h=" << h;
    }
}

// P7 — bit-deterministic: two identical runs produce identical fill arrays.
TEST(WaterCoreTest, Deterministic) {
    auto run = [] {
        Tank t(12, 8, 12);
        t.grid.fillBox({0, 0, 0}, {5, 5, 11}, 1.0f);
        WaterSolver s(t.grid, t.query());
        runTicks(s, 120);
        return t.grid.fData();
    };
    const auto a = run(), b = run();
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) ASSERT_EQ(a[i], b[i]) << "cell " << i;
}

// P3 — resolution changes motion detail, never mass: a 1/3 grid aggregated per voxel column equals
// the same water on a 1-voxel grid.
TEST(WaterCoreTest, MassPerColumnInvariantAcrossResolution) {
    WaterGrid coarse(GridSpec{glm::ivec3(0), glm::ivec3(4, 4, 4), 1.0f});
    WaterGrid fine(GridSpec{glm::ivec3(0), glm::ivec3(12, 12, 12), 1.0f / 3.0f});
    coarse.fillBox({0, 0, 0}, {3, 1, 3}, 1.0f); coarse.fillBox({1, 2, 1}, {2, 2, 2}, 0.5f);
    fine.fillBox({0, 0, 0}, {11, 5, 11}, 1.0f); fine.fillBox({3, 6, 3}, {8, 8, 8}, 0.5f);
    // fill fractions are float; 1/27-volume cells accumulate ~1e-7 relative rounding per cell
    EXPECT_NEAR(coarse.totalMass(), fine.totalMass(), 1e-5 * coarse.totalMass());
    const auto cm = coarse.massPerVoxelColumn(), fm = fine.massPerVoxelColumn();
    for (const auto& c : cm) {
        bool found = false;
        for (const auto& f : fm) if (f.first == c.first) { EXPECT_NEAR(f.second, c.second, 1e-5 * std::max(1.0, c.second)) << c.first.x << "," << c.first.y; found = true; }
        EXPECT_TRUE(found);
    }
}

// Chunk independence, solver form: a 2-cell margin and a 10-cell margin give the same interior.
TEST(WaterCoreTest, SubBoxIdenticalToWholeBox) {
    auto run = [](int margin) {
        const int nx = 20 + 2 * margin, ny = 8 + margin, nz = 6 + 2 * margin;
        Tank t(nx, ny, nz);
        // walls of the small box live INSIDE the big box as Solid so the water sees the same room
        t.extra = [margin, nx, ny, nz](const glm::ivec3& c) {
            const bool inRoom = c.x >= margin && c.x < nx - margin && c.z >= margin && c.z < nz - margin && c.y < ny - margin;
            return inRoom ? Occ::Air : Occ::Solid; };
        t.grid.fillBox({margin, 0, margin}, {margin + 5, 2, nz - margin - 1}, 1.0f);
        WaterSolver s(t.grid, t.query());
        runTicks(s, 90);
        std::vector<float> room;
        for (int z = margin; z < nz - margin; ++z) for (int y = 0; y < ny - margin; ++y) for (int x = margin; x < nx - margin; ++x) room.push_back(t.grid.f(x, y, z));
        return room;
    };
    const auto a = run(2), b = run(10);
    ASSERT_EQ(a.size(), b.size());
    // identical up to the pressure solve's own convergence (pcgTolerance 1e-6 relative)
    for (size_t i = 0; i < a.size(); ++i) EXPECT_NEAR(a[i], b[i], 1e-5f) << "cell " << i;
}

// A submerged pump in a FULL box keeps delivering: the outlet is an inflow boundary (div u = q in
// the projection), the surface above rises by transport, and compaction must not undo a rising
// face. Red first (0 of 4 m^3 delivered: gravity was added before the advection, and compaction
// pulled each substep's rise straight back down), green at 0.5 m^3/s (2026-10-08, S2 Small).
TEST(WaterCoreTest, SubmergedPumpDelivers) {
    Tank t(6, 8, 1);
    t.grid.fillBox({0, 0, 0}, {5, 2, 0}, 1.0f);            // 18 m^3, surface at y = 3
    WaterSolver s(t.grid, t.query());
    s.addSource({3, 1, 0}, 0.5f);                           // 0.5 m^3/s two cells under the surface
    runTicks(s, 8 * 60);
    const auto& src = s.sources()[0];
    EXPECT_NEAR(src.placedTotal, 4.0, 0.1) << "the pump must deliver its rate into a full box";
    EXPECT_NEAR(t.grid.totalMass(), 18.0 + src.placedTotal, 1e-5) << "delivered water is in the grid";   // float32 fills summed over 48 cells x 480 ticks: ~4e-6 of 22 m^3
    // the pump carries what the outlet could not take (SourceSpec::pending) and places it as room
    // appears; the backlog is bounded to one second of rate by design
    EXPECT_LT(src.pending, 0.5) << "the owed backlog stays under one second of rate";
}

// Discharge over a broad-crested sill converges on the weir law at BOTH resolutions. Reservoir
// 15 m x 3 m, 4 m deep over a 3 m sill (1 m head), sill 5 m long, free fall beyond. Prediction:
// Q = 1.705 b H^1.5 (SI, critical flow on the crest; Henderson 1966), integrated with the reservoir
// drawdown (area 45 m^2) - 23.8 m^3 leaves in 8 s. Red first at h = 1/3: 18.9 m^3 (-20 %) while
// h = 1 gave 25.4 (+7 %) - compaction was squashing the sloping free surface of the streaming
// sheet cell by cell (bisected with SolverParams::debugDisableStages, 2026-10-08); gated on cell
// speed the two resolutions read +3 % and -2 %. Gate: within 10 % of the weir integral.
static double weirReservoirLoss(float h) {
    const int per = static_cast<int>(std::lround(1.0f / h));
    const int nx = 30 * per, ny = 7 * per, nz = 3 * per;
    Tank t(nx, ny, nz, h);
    t.extra = [per](const glm::ivec3& c) {   // the sill block x 15..19 m, y 0..2 m
        return (c.x >= 15 * per && c.x < 20 * per && c.y < 3 * per) ? Occ::Solid : Occ::Air; };
    t.grid.fillBox({0, 0, 0}, {15 * per - 1, 4 * per - 1, nz - 1}, 1.0f);   // 15 x 4 x 3 = 180 m^3
    WaterSolver s(t.grid, t.query());
    runTicks(s, 8 * 60);
    double m = 0.0;
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < 15 * per; ++x) m += t.grid.f(x, y, z);
    return 180.0 - m * h * h * h;
}
static double weirIntegral(double seconds) {
    double H = 1.0, out = 0.0;
    for (int k = 0; k < static_cast<int>(seconds / kDt); ++k) { const double q = 1.705 * 3.0 * std::pow(std::max(H, 0.0), 1.5); out += q * kDt; H -= q * kDt / 45.0; }
    return out;
}
TEST(WaterCoreTest, WeirDischargeMatchesAtBothResolutions) {
    const double weir = weirIntegral(8.0);
    const double coarse = weirReservoirLoss(1.0f), fine = weirReservoirLoss(1.0f / 3.0f);
    EXPECT_NEAR(coarse, weir, 0.10 * weir) << "h = 1 reservoir loss vs the weir integral";
    EXPECT_NEAR(fine, weir, 0.10 * weir) << "h = 1/3 reservoir loss vs the weir integral";
}

// The solver-only S3: a dam break front runs at the Ritter speed on a dry bed (frictionless bound).
TEST(WaterCoreTest, DamBreakFrontWithinRitter) {
    Tank t(60, 6, 3);
    t.grid.fillBox({0, 0, 0}, {9, 2, 2}, 1.0f);           // block x 0..9, h0 = 3
    WaterSolver s(t.grid, t.query());
    const double tRun = 2.0;
    runTicks(s, static_cast<int>(tRun / kDt));
    // Prediction (Ritter 1892, dry bed): the depth profile is h(x,t) = (2 sqrt(g h0) - x/t)^2 / 9g,
    // so the TIP (h -> 0) runs at 2 sqrt(g h0) = 10.85 m/s but the point where the depth is d_th
    // runs at 2 sqrt(g h0) - 3 sqrt(g d_th). A fill-fraction grid whose pressure domain is "cell
    // centre submerged" cannot carry a tongue thinner than HALF A CELL, so the resolvable front is
    // the d_th = 0.5 h contour: at h = 1, d_th = 0.5 m, speed 10.85 - 6.64 = 4.21 m/s,
    // x(2 s) = 10 + 8.4 = 18.4 cells. Require >= 85 % of that travel: >= 17.1 -> 17.
    // (The first form of this test asked for the 0.1 m contour on a 1 m grid - a quarter of a
    // cell - and measured 18 cells, i.e. the 0.5 m contour of the exact solution, 2026-10-08.)
    const double dth = 0.5 * t.grid.h();
    const double vth = ritterFrontSpeed(3.0) - 3.0 * std::sqrt(kG * dth);
    const double expected = 10.0 + 0.85 * vth * tRun;
    EXPECT_GE(frontX(t.grid, 1, dth), static_cast<int>(expected)) << "front slower than 85 % of Ritter at depth " << dth;
    EXPECT_NEAR(t.grid.totalMass(), 10.0 * 3.0 * 3.0, 1e-3);
}

// The same dam break at 1/3 resolution resolves a thinner tongue (d_th = 1/6 m) and must still be
// within 85 % of Ritter at that contour: the front converges toward the tip as cells shrink.
TEST(WaterCoreTest, DamBreakFrontConvergesWithResolution) {
    const float h = 1.0f / 3.0f;
    Tank t(120, 15, 3, h);                                 // 40 m x 5 m x 1 m channel
    t.grid.fillBox({0, 0, 0}, {29, 8, 2}, 1.0f);           // block 10 m long, h0 = 3 m
    // A 120-cell-long domain needs more Jacobi-CG iterations than a 60-cell one (condition number
    // grows with L^2); the default cap must not silently under-solve the pressure.
    SolverParams p; p.pcgMaxIters = 4000;
    WaterSolver s(t.grid, t.query(), p);
    const double tRun = 1.0;
    int maxIters = 0; double worstResidual = 0.0;
    for (int i = 0; i < static_cast<int>(tRun / kDt); ++i) {
        const StepReport r = s.step(kDt);
        maxIters = std::max(maxIters, r.pcgIterations);
        worstResidual = std::max(worstResidual, r.pcgResidual);
    }
    EXPECT_LT(worstResidual, 1e-5) << "pressure solve did not converge (max iterations per tick " << maxIters << ")";
    const double dth = 0.5 * h;                            // 0.167 m contour
    const double vth = ritterFrontSpeed(3.0) - 3.0 * std::sqrt(kG * dth);   // 10.85 - 3.84 = 7.0 m/s
    const double expectedMetres = 10.0 + 0.85 * vth * tRun;                  // 15.96 m
    const double frontMetres = (frontX(t.grid, 1, dth * h * h /* column mass of a d_th-deep 1-cell column */) + 1) * h;
    if (frontMetres < expectedMetres) {
        // Diagnostic: measured depth profile vs Ritter's h(x,t) = (2 sqrt(g h0) - (x-10)/t)^2 / 9g
        std::printf("  x[m]  depth[m]  ritter[m]\n");
        for (int x = 24; x < 120; x += 3) {
            const double xm = (x + 0.5) * h;
            const double depth = t.grid.columnMass(x, 1) / (h * h);
            const double xi = (xm - 10.0) / tRun;
            const double c = ritterFrontSpeed(3.0);
            const double rit = xi < -std::sqrt(kG * 3.0) ? 3.0 : (xi > c ? 0.0 : (c - xi) * (c - xi) / (9.0 * kG));
            std::printf("  %5.2f  %7.3f  %7.3f\n", xm, depth, rit);
        }
    }
    EXPECT_GE(frontMetres, expectedMetres) << "fine-grid front slower than 85 % of Ritter at depth " << dth;
    EXPECT_NEAR(t.grid.totalMass(), 10.0 * 3.0 * 1.0, 1e-3);
}

// After the front reaches the far wall, the wall column piles up above the still level: a reflection.
TEST(WaterCoreTest, WallCrestReflects) {
    Tank t(40, 8, 3);
    t.grid.fillBox({0, 0, 0}, {9, 2, 2}, 1.0f);
    WaterSolver s(t.grid, t.query());
    const double still = 10.0 * 3.0 / 40.0;               // 0.75 m when flat
    double wallPeak = 0.0, frontDepthAtArrival = 0.0;
    bool arrived = false;
    for (int i = 0; i < 600; ++i) {
        s.step(kDt);
        const double wallCol = t.grid.columnMass(39, 1) / 1.0;   // depth in m (area 1)
        if (!arrived && wallCol > 0.05) { arrived = true; frontDepthAtArrival = t.grid.columnMass(37, 1); }
        if (arrived) wallPeak = std::max(wallPeak, wallCol);
    }
    ASSERT_TRUE(arrived) << "front never reached the wall";
    // Prediction: wall peak >= still + 0.8 * incident depth (the surge piles up, it does not merely fill).
    EXPECT_GE(wallPeak, still + 0.8 * frontDepthAtArrival) << "no reflected crest at the wall";
}
