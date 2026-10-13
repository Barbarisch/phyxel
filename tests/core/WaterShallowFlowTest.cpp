// WaterCore.md 26 P1 - the pond model carries a creek. The pond solver ALONE (WaterSolver, CPU reference) on a straight
// channel at 1/9 m cells: 16 m x 1 m, 0.67 m tall, a flat floor in a TILTED FRAME (gravityX = g S - the slope without the
// 1/9 m staircase a sloped bed of cells would be). Inflow: pumps across the inlet; outlet: the prescribed columns hold
// Manning's normal depth. A solver that carries the creek holds the normal depth and speed all along; excess depth upstream
// is the defect (25.40: 145 % with the whole engine stack). Mid-reach = x 6-10 m.
#include <gtest/gtest.h>
#include "core/water/WaterCore.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace Phyxel::Core::Water {
namespace {

constexpr float kH = 1.0f / 9.0f, kS = 0.01f, kN = 0.035f, kG = 9.81f;
constexpr int kNy = 6, kNz = 9;

struct Opts {
    double q = 0.15;               // m^2/s (unit discharge)
    float bedManning = kN;         // 0 = pond friction (pondFriction below)
    float pondFriction = 0.0f;
    uint32_t disable = 0;          // SolverParams::debugDisableStages
    float liquidThreshold = 0.5f;
    float cfl = 0.5f;
    float seconds = 40.0f;
    bool reverse = false;          // the same channel flowing toward -x (pumps at the far end, the outlet at x 0)
    int nx = 432;                  // 48 m: the middle third is clear of the inlet's acceleration and the outlet's backwater (16 m was not - 26 P1)
};
struct Result { double dn, un, depth, speed, ms, flux, pumped, drained, slope; };

Result runChannel(const Opts& o) {
    const int nx = o.nx;
    const double dn = std::pow(o.q * kN / std::sqrt(kS), 0.6), un = o.q / dn;   // Manning, wide channel
    // the OUTLET is a free overfall: the channel's floor (2 cells) ends 2 m before the box's end and the water drops into a pit
    // where it is taken out - a hydraulic control, not a wall (the first outlet held the depth against the box's end wall and
    // its backwater reached the measured third: the surface rose toward it, 26 P1)
    constexpr int kFloor = 2, kPit = 18;
    const int ny = kNy + kFloor;
    auto floorAt = [&](int x) { const bool pit = o.reverse ? x < kPit : x >= nx - kPit; return pit ? 0 : kFloor; };
    GridSpec spec; spec.origin = glm::ivec3(0); spec.dims = glm::ivec3(nx, ny, kNz); spec.h = kH;
    WaterGrid g(spec);
    SolidQuery solids = [&](const glm::ivec3& c) { return (g.inBounds(c.x, c.y, c.z) && c.y >= floorAt(c.x)) ? Occ::Air : Occ::Solid; };
    SolverParams p;
    p.gravityX = (o.reverse ? -1.0f : 1.0f) * kG * kS; p.bedManning = o.bedManning; p.pondFriction = o.pondFriction;
    p.debugDisableStages = o.disable; p.liquidThreshold = o.liquidThreshold; p.cflFraction = o.cfl;
    WaterSolver s(g, solids, p);
    // start at the normal state: fills to dn, u = un
    const float cells = static_cast<float>(dn / kH);
    for (int z = 0; z < kNz; ++z) for (int x = 0; x < nx; ++x) for (int y = 0; y < ny; ++y) {
        if (y < floorAt(x) || floorAt(x) == 0) continue;   // the pit starts empty
        g.f(x, y, z) = std::clamp(cells - static_cast<float>(y - kFloor), 0.0f, 1.0f);
        const float u0 = static_cast<float>(o.reverse ? -un : un);
        if (g.f(x, y, z) > 0.0f) { g.u(x, y, z) = u0; g.u(x + 1, y, z) = u0; }
    }
    // inflow: pumps over the inlet column's wet cells; outlet: the last column held at the normal depth
    const int wetCells = static_cast<int>(std::ceil(cells));
    const float rate = static_cast<float>(o.q * kNz * kH / (kNz * wetCells));
    const int inX = o.reverse ? nx - 1 : 0, outX = o.reverse ? 0 : nx - 1;
    for (int z = 0; z < kNz; ++z) for (int y = 0; y < wetCells; ++y) s.addSource(glm::ivec3(inX, kFloor + y, z), rate);
    for (int z = 0; z < kNz; ++z) s.setBoundary(glm::ivec2(outX, z), 0.05f * kH);   // the pit's far column: drained to a film
    const auto t0 = std::chrono::steady_clock::now();
    const int ticks = static_cast<int>(o.seconds * 60.0f);
    double placed0 = 0.0, drained = 0.0;
    for (int k = 0; k < ticks; ++k) {
        if (k == ticks / 2) { placed0 = 0.0; for (const auto& sp : s.sources()) placed0 += sp.placedTotal; drained = 0.0; }
        const StepReport rep = s.step(1.0f / 60.0f);
        if (k >= ticks / 2) drained -= rep.boundaryExchange;   // + = taken out at the outlet
    }
    double placed1 = 0.0; for (const auto& sp : s.sources()) placed1 += sp.placedTotal;
    const double half = o.seconds / 2.0;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / ticks;
    double d = 0.0, m = 0.0, mu = 0.0, flux = 0.0; int n = 0;
    for (int x = nx / 3; x < 2 * nx / 3; ++x) for (int z = 0; z < kNz; ++z) {
        double col = 0.0;
        for (int y = 0; y < ny; ++y) {
            const float f = g.f(x, y, z);
            col += f;
            m += f; mu += f * 0.5 * (g.u(x, y, z) + g.u(x + 1, y, z));
            const float fd = g.u(x, y, z) > 0.0f ? (x > 0 ? g.f(x - 1, y, z) : f) : f;   // the donor's fill carries the face's flux (the face's donor)
            flux += fd * g.u(x, y, z) * kH;   // m^2/s per sub-column (summed over z, then / kNz below)
        }
        d += col * kH; ++n;
    }
    const int nxs = 2 * nx / 3 - nx / 3;
    // the surface's slope over the measured third (a flat surface = the normal state; the rig's own check)
    auto colDepth = [&](int x) { double d = 0.0; for (int z = 0; z < kNz; ++z) for (int y = 0; y < ny; ++y) d += g.f(x, y, z); return d * kH / kNz; };
    const double slopeMeasured = (colDepth(2 * nx / 3 - 1) - colDepth(nx / 3)) / ((2 * nx / 3 - 1 - nx / 3) * kH);
    const double sgn = o.reverse ? -1.0 : 1.0;
    return Result{dn, un, d / n, m > 0.0 ? sgn * mu / m : 0.0, ms, sgn * flux / (nxs * kNz), (placed1 - placed0) / half / (kNz * kH), drained / half / (kNz * kH), slopeMeasured};
}

void print(const char* name, const Result& r) {
    // what Manning's law gives for the depth the solver ended at - a speed below it is drag beyond the bed's
    const double uAtDepth = std::pow(r.depth, 2.0 / 3.0) * std::sqrt(kS) / kN;
    std::printf("  %-34s depth %.4f m (%.0f %% of %.4f), speed %.3f (%.0f %% of %.3f; Manning at this depth %.3f); flux %.3f, pumped %.3f, drained %.3f m^2/s; surface slope over the third %+.4f; %.2f ms/tick\n", name, r.depth, 100.0 * r.depth / r.dn, r.dn,
                r.speed, 100.0 * r.speed / r.un, r.un, uAtDepth, r.flux, r.pumped, r.drained, r.slope, r.ms);
}

}  // namespace

// DIAGNOSTIC (26 P1): the baseline and one variable at a time. Run with --gtest_also_run_disabled_tests.
// DIAGNOSTIC (26 P1): the momentum budget at steady flow - the mean along-channel velocity of the mid-reach's water faces
// (faces with water on both sides) after each stage of each substep, over the last 10 s of an 80 s run. At steady state the
// changes sum to zero: gravity adds g S dt, the bed takes it back - any other stage that takes is the defect.
// DIAGNOSTIC (26 P1): where does mass go? A 4 x 4 m tank at 1/9 m, water to 0.5 m with a tilted surface (a slosh), 10 s;
// the grid's mass after each stage of each substep, the changes summed per stage. Exact stages sum to ~0 (float).
TEST(WaterShallowFlowTest, DISABLED_DiagMassByStage) {
    for (bool seq : {true, false}) {
        EulerianTransport::s_allFacesAtOnce = !seq;
        GridSpec spec; spec.origin = glm::ivec3(0); spec.dims = glm::ivec3(36, 9, 36); spec.h = kH;
        WaterGrid g(spec);
        SolidQuery solids = [&](const glm::ivec3& c) { return g.inBounds(c.x, c.y, c.z) ? Occ::Air : Occ::Solid; };
        WaterSolver s(g, solids, SolverParams{});
        for (int z = 0; z < 36; ++z) for (int x = 0; x < 36; ++x) {
            const float top = 4.5f + 2.0f * (x - 18) / 18.0f;   // cells
            for (int y = 0; y < 9; ++y) g.f(x, y, z) = std::clamp(top - static_cast<float>(y), 0.0f, 1.0f);
        }
        std::vector<std::string> names; std::vector<double> dm; double prev = g.totalMass();
        s.stageProbeForTest = [&](const char* st) {
            const double m = g.totalMass();
            if (std::string(st) != "start") {
                size_t i = 0; while (i < names.size() && names[i] != st) ++i;
                if (i == names.size()) { names.push_back(st); dm.push_back(0.0); }
                dm[i] += m - prev;
            }
            prev = m;
        };
        const double m0 = g.totalMass();
        double between = 0.0;   // changes outside the substeps (the residue sweep, boundaries)
        for (int k = 0; k < 600; ++k) { const double a = g.totalMass(); s.step(1.0f / 60.0f); (void)a; }
        std::printf("  %s: mass %.9f -> %.9f (%+.2e m^3)\n", seq ? "face by face (old)" : "all at once (new)", m0, g.totalMass(), g.totalMass() - m0);
        for (size_t i = 0; i < names.size(); ++i) if (std::abs(dm[i]) > 1e-12) std::printf("    %-16s %+.3e m^3\n", names[i].c_str(), dm[i]);
        (void)between;
    }
    EulerianTransport::s_allFacesAtOnce = false;
}

TEST(WaterShallowFlowTest, DISABLED_DiagMomentumBudget) {
    constexpr int kNx = 144;
    EulerianTransport::s_allFacesAtOnce = std::getenv("P1_NEW_TRANSPORT") != nullptr;   // the new rule, for comparison
    const double q = 0.15, dn = std::pow(q * kN / std::sqrt(kS), 0.6), un = q / dn;
    GridSpec spec; spec.origin = glm::ivec3(0); spec.dims = glm::ivec3(kNx, kNy, kNz); spec.h = kH;
    WaterGrid g(spec);
    SolidQuery solids = [&](const glm::ivec3& c) { return g.inBounds(c.x, c.y, c.z) ? Occ::Air : Occ::Solid; };
    SolverParams p; p.gravityX = kG * kS; p.bedManning = kN; p.pondFriction = 0.0f;
    WaterSolver s(g, solids, p);
    const float cells = static_cast<float>(dn / kH);
    for (int z = 0; z < kNz; ++z) for (int x = 0; x < kNx; ++x) for (int y = 0; y < kNy; ++y) {
        g.f(x, y, z) = std::clamp(cells - static_cast<float>(y), 0.0f, 1.0f);
        if (g.f(x, y, z) > 0.0f) { g.u(x, y, z) = static_cast<float>(un); g.u(x + 1, y, z) = static_cast<float>(un); }
    }
    const int wetCells = static_cast<int>(std::ceil(cells));
    const float rate = static_cast<float>(q * kH / wetCells);
    for (int z = 0; z < kNz; ++z) for (int y = 0; y < wetCells; ++y) s.addSource(glm::ivec3(0, y, z), rate);
    for (int z = 0; z < kNz; ++z) { BoundarySpec& b = s.setBoundary(glm::ivec2(kNx - 1, z), static_cast<float>(dn)); b.uSurface = glm::vec2(static_cast<float>(un), 0.0f); }
    // the mid-reach's water faces, mean u, per row too (bottom row, the rest, the top partial row)
    auto meanU = [&](int rowSel) {
        double sum = 0.0; long n = 0;
        for (int x = kNx * 3 / 8; x < kNx * 5 / 8; ++x) for (int z = 0; z < kNz; ++z) for (int y = 0; y < kNy; ++y) {
            const float fl = g.f(x - 1, y, z), fr = g.f(x, y, z);
            if (fl <= 0.0f || fr <= 0.0f) continue;
            const bool top = std::min(fl, fr) < 0.999f;
            if (rowSel == 1 && y != 0) continue;
            if (rowSel == 2 && !top) continue;
            sum += g.u(x, y, z); ++n;
        }
        return n ? sum / n : 0.0;
    };
    std::vector<std::string> names; std::vector<double> dAll, dBot, dTop; double prevA = 0, prevB = 0, prevT = 0; bool on = false; long samples = 0;
    s.stageProbeForTest = [&](const char* st) {
        const double a = meanU(0), b = meanU(1), t = meanU(2);
        if (on && std::string(st) != "start") {
            size_t i = 0; while (i < names.size() && names[i] != st) ++i;
            if (i == names.size()) { names.push_back(st); dAll.push_back(0); dBot.push_back(0); dTop.push_back(0); }
            dAll[i] += a - prevA; dBot[i] += b - prevB; dTop[i] += t - prevT;
            if (std::string(st) == "friction") ++samples;
        }
        prevA = a; prevB = b; prevT = t;
    };
    for (int k = 0; k < 80 * 60; ++k) { on = k >= 70 * 60; s.step(1.0f / 60.0f); }
    {   // the depth along the channel (every 1 m), z-averaged
        std::printf("  depth along x (m):");
        for (int x = 4; x < kNx; x += 9) { double d = 0.0; for (int z = 0; z < kNz; ++z) for (int y = 0; y < kNy; ++y) d += g.f(x, y, z); std::printf(" %.3f", d * kH / kNz); }
        std::printf("\n");
    }
    std::printf("  mean u of the mid-reach's water faces %.3f m/s (bottom row %.3f, top partial row %.3f); Manning speed %.3f\n", meanU(0), meanU(1), meanU(2), un);
    std::printf("  per-substep change of the mean (mm/s), %ld substeps:\n", samples);
    for (size_t i = 0; i < names.size(); ++i)
        std::printf("    %-16s all %+8.3f   bottom row %+8.3f   top row %+8.3f\n", names[i].c_str(), 1000.0 * dAll[i] / samples, 1000.0 * dBot[i] / samples, 1000.0 * dTop[i] / samples);
}

// 26 P1 GATE: the pond model carries a creek - the 48 m channel at q 0.40 m^2/s (normal depth 0.307 m, ~2.8 cells at 1/9 m),
// 60 s: with the all-faces-at-once transport the middle third's depth and speed within 5 % of Manning's, its surface flat, and
// the flux the same as what is pumped in. RED (measured before the fix): the face-by-face rule runs 114 % deep at q 0.15 on
// this rig with the velocity field implying 0.185 m^2/s where 0.150 moves (DiagSimultaneousTransport).
TEST(WaterShallowFlowTest, APondModelCarriesACreek) {
    EulerianTransport::s_allFacesAtOnce = true;
    Opts o; o.q = 0.40; o.seconds = 60.0f;
    const Result r = runChannel(o);
    EulerianTransport::s_allFacesAtOnce = false;
    print("all faces at once, q 0.40", r);
    EXPECT_NEAR(r.depth / r.dn, 1.0, 0.05);
    EXPECT_NEAR(r.speed / r.un, 1.0, 0.05);
    EXPECT_NEAR(r.flux, r.pumped, 0.03 * r.pumped);
    EXPECT_LT(std::abs(r.slope), 0.002) << "the measured third is uniform flow";
}

TEST(WaterShallowFlowTest, DISABLED_DiagBothFlows) {
    for (double q : {0.15, 0.40}) for (bool sim : {false, true}) {
        EulerianTransport::s_allFacesAtOnce = sim;
        Opts o; o.q = q; o.seconds = 60.0f;
        char nm[64]; std::snprintf(nm, sizeof nm, "%s q %.2f", sim ? "new" : "old", q);
        print(nm, runChannel(o));
    }
    EulerianTransport::s_allFacesAtOnce = false;
}

TEST(WaterShallowFlowTest, DISABLED_DiagSimultaneousTransport) {
    for (bool sim : {false, true}) {
        EulerianTransport::s_allFacesAtOnce = sim; EulerianTransport::s_overfullForTest = 0.0;
        Opts o; o.q = 0.15; o.seconds = 80.0f;
        print(sim ? "faces at once (no receiver cap)" : "faces in order (today)", runChannel(o));
        if (sim) std::printf("    parked over full %.2e cells\n", EulerianTransport::s_overfullForTest);
    }
    EulerianTransport::s_allFacesAtOnce = false;
}

TEST(WaterShallowFlowTest, DISABLED_DiagDirection) {
    for (bool rev : {false, true}) { Opts o; o.q = 0.15; o.seconds = 80.0f; o.reverse = rev; print(rev ? "flowing toward -x" : "flowing toward +x", runChannel(o)); }
}

TEST(WaterShallowFlowTest, DISABLED_DiagSteadiness) {
    for (float t : {20.0f, 40.0f, 80.0f, 160.0f}) { Opts o; o.q = 0.15; o.seconds = t; char nm[64]; std::snprintf(nm, sizeof nm, "baseline at %.0f s", t); print(nm, runChannel(o)); }
}

TEST(WaterShallowFlowTest, DISABLED_DiagWhatSlowsAShallowCreek) {
    for (double q : {0.15, 0.40}) {
        std::printf(" q %.2f m^2/s (normal depth %.3f m = %.1f cells)\n", q, std::pow(q * kN / std::sqrt(kS), 0.6), std::pow(q * kN / std::sqrt(kS), 0.6) / kH);
        Opts base; base.q = q;
        print("baseline (Manning at the bed)", runChannel(base));
        { Opts o = base; o.bedManning = 0.0f; print("no friction at all", runChannel(o)); }
        { Opts o = base; o.disable = 1; print("compaction off", runChannel(o)); }
        { Opts o = base; o.disable = 2 | 4; print("thin-film stages off", runChannel(o)); }
        { Opts o = base; o.disable = 32; print("rest damping off", runChannel(o)); }
        { Opts o = base; o.disable = 16; print("residue sweep off", runChannel(o)); }
        { Opts o = base; o.cfl = 0.25f; print("half the substep (cfl 0.25)", runChannel(o)); }
    }
}

}  // namespace Phyxel::Core::Water
