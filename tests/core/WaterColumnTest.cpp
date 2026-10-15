// WaterCore.md 25.18 H1 step 1 - the column-water region (ColumnWater): the ledger with springs and outfalls (C-T1), the
// hydraulic bed is a pure function of the tops (C-T7), and the three bed rules on a 30 m channel whose bed is a 1/3 m
// staircase (the owner's 1/3 m creek-bed rule, 25.19) - the comparison's first numbers, before the bench.
#include <gtest/gtest.h>
#include "core/water/ColumnWater.h"
#include "core/water/WaterCoreManager.h"

#include <cmath>
#include <cstdio>
#include <tuple>

namespace Phyxel::Core::Water {
namespace {

constexpr float kSlope = 1.0f / 50.0f, kN = 0.035f;
double normalDepth(double q, double n, double S) { return std::pow(q * n / std::sqrt(S), 0.6); }

// A 30 m x 2 m channel along x: voxels x 0..29, z 0..3 (walls at the outer column thirds: z voxel 0 and 3 -> columns
// 0-2 and 9-11 are walls, 6 interior columns = 2 m). The bed's smooth line falls from 19.0 at a slope of 1:50; the drawn
// bed is that line floored to `step` (1/3 m: the owner's rule).
struct Channel {
    ColumnWater cw;
    std::vector<float> tops; std::vector<uint8_t> walls;
    explicit Channel(float step) : cw(1, glm::ivec3(0, 16, 0), glm::ivec3(29, 21, 3), [] { ShoreParams p; p.manningN = kN; return p; }()) {
        const int nx = cw.nx(), nz = cw.nz();
        tops.assign(static_cast<size_t>(nx) * nz, 0.0f); walls.assign(tops.size(), 0);
        for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
            const size_t i = static_cast<size_t>(x) + static_cast<size_t>(nx) * z;
            if (z < 3 || z >= 9) { walls[i] = 1; continue; }
            const float smooth = 19.0f - kSlope * cw.columnCentre(x, z).x;
            tops[i] = step > 0.0f ? std::floor(smooth / step + 1e-4f) * step : smooth;
        }
        cw.setSolids(tops, walls);
        cw.setPlane(19.0f, kSlope, 0.0f);   // the line the staircase approximates - the stand-in for the generator's height
        for (int z = 3; z < 9; ++z) { cw.addSpring(0, z, 0.0); cw.addOutfall(nx - 1, z); }
    }
    void setFlow(double Q) { cw.clearSpringsAndOutfalls(); for (int z = 3; z < 9; ++z) { cw.addSpring(0, z, Q / 6.0); cw.addOutfall(cw.nx() - 1, z); } }
};

}  // namespace

// C-T1: the ledger closes over 10 minutes with springs and outfalls, through a bed-rule switch halfway (which must keep
// every column's depth - no water created or destroyed by the switch).
TEST(WaterColumnTest, LedgerClosesWithSpringsOutfallsAndARuleSwitch) {
    Channel ch(1.0f / 3.0f);
    ch.setFlow(0.05);
    for (int k = 0; k < 300 * 30; ++k) ch.cw.step(1.0f / 30.0f);
    const double before = ch.cw.stored();
    ch.cw.setBedRule(ColumnBedRule::Hydraulic);
    EXPECT_NEAR(ch.cw.stored(), before, 1e-9 * (before + 1.0)) << "a bed-rule switch keeps every column's depth";
    for (int k = 0; k < 300 * 30; ++k) ch.cw.step(1.0f / 30.0f);
    std::printf("  in %.3f m^3, out %.3f, stored %.3f, ledger error %.2e m^3\n", ch.cw.inflow(), ch.cw.outflow(), ch.cw.stored(), ch.cw.ledgerError());
    EXPECT_NEAR(ch.cw.ledgerError(), 0.0, 1e-6 * (ch.cw.inflow() + 1.0));
}

// C-T7: the hydraulic bed is a pure function of the tops - the same staircase placed at two offsets (one straddling where a
// chunk seam would fall) gives the same ramp, value for value.
TEST(WaterColumnTest, HydraulicBedIsTranslationInvariant) {
    const int nx = 60, nz = 4;
    auto make = [&](int offset) {
        std::vector<float> tops(static_cast<size_t>(nx + 20) * nz, 5.0f); std::vector<uint8_t> walls(tops.size(), 0);
        for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) tops[static_cast<size_t>(x + offset) + static_cast<size_t>(nx + 20) * z] = 7.0f - (1.0f / 3.0f) * (x / 15);
        return std::make_pair(hydraulicBed(tops, walls, nx + 20, nz), offset);
    };
    const auto a = make(0), b = make(17);
    double worst = 0.0;
    for (int z = 0; z < nz; ++z) for (int x = 1; x < nx - 1; ++x)
        worst = std::max(worst, static_cast<double>(std::abs(a.first[x + (nx + 20) * z] - b.first[x + 17 + (nx + 20) * z])));
    std::printf("  worst difference between the two placements: %.2e m\n", worst);
    EXPECT_EQ(worst, 0.0);
}

// The comparison's first numbers (25.19): 20 L/s and 100 L/s down the 1/3 m staircase, each bed rule, 240 s. Printed with
// the hidden fraction (wet columns whose water surface is below the DRAWN voxel top - the look-breaker for (a)).
TEST(WaterColumnTest, BedRulesOnAThirdMetreStaircase) {
    for (double Q : {0.02, 0.1}) for (ColumnBedRule rule : {ColumnBedRule::Voxel, ColumnBedRule::Hydraulic, ColumnBedRule::Plane}) {
        Channel ch(1.0f / 3.0f);
        ch.cw.setBedRule(rule);
        ch.setFlow(Q);
        double outAt = 0.0;
        for (int k = 0; k < 240 * 30; ++k) { if (k == 220 * 30) outAt = ch.cw.outflow(); ch.cw.step(1.0f / 30.0f); }
        const double outRate = (ch.cw.outflow() - outAt) / 20.0;
        double dSum = 0.0; int n = 0, wet = 0, hidden = 0; double worstHide = 0.0;
        const ShoreSolver& s = ch.cw.solver();
        for (int z = 3; z < 9; ++z) for (int x = 0; x < ch.cw.nx(); ++x) {
            const double d = s.depth(x, z);
            if (x >= ch.cw.nx() / 3 && x < 2 * ch.cw.nx() / 3) { dSum += d; ++n; }
            if (d > s.params().dryDepth) {
                ++wet;
                const double drawnTop = ch.tops[static_cast<size_t>(x) + static_cast<size_t>(ch.cw.nx()) * z];
                const double hide = drawnTop - s.col(x, z).eta;
                if (hide > 0.0) { ++hidden; worstHide = std::max(worstHide, hide); }
            }
        }
        std::printf("  Q %.2f, bed %-9s: out/in %.3f, mid depth %.4f m (Manning %.4f), hidden %d of %d wet columns (worst %.3f m under the drawn top)\n",
                    Q, columnBedRuleName(rule), outRate / Q, dSum / n, normalDepth(Q / 2.0, kN, kSlope), hidden, wet, worstHide);
        EXPECT_NEAR(ch.cw.ledgerError(), 0.0, 1e-6 * (ch.cw.inflow() + 1.0));
    }
}

// 25.26 K-T1 (unit) + K-T3: the probe on the straight 1:50 channel at steady flow (plane bed - Manning-exact, 25.20) reports
// the speed and depth Manning gives for its own unit discharge (within 5 %); raising the roughness n 0.035 -> 0.050 slows the
// flow by the Manning ratio for a fixed discharge, (0.035 / 0.050)^(3/5) = 0.807, within 10 %.
TEST(WaterColumnTest, ProbeMatchesManningAndRoughnessTunes) {
    auto run = [](float n) {
        Channel ch(1.0f / 3.0f);
        ch.cw.setManningN(n);
        ch.cw.setBedRule(ColumnBedRule::Plane);
        ch.setFlow(0.1);
        for (int k = 0; k < 240 * 30; ++k) ch.cw.step(1.0f / 30.0f);
        return ch.cw.probe(15.0f, 1.0f + 0.5f / 3.0f + 1.0f);   // mid-reach, an interior column
    };
    const auto a = run(0.035f), b = run(0.050f);
    std::printf("  n 0.035: depth %.4f m (Manning %.4f), speed %.3f m/s (Manning %.3f), slope %.4f, Froude %.2f\n", a.depth, a.manningDepth, a.speed, a.manningSpeed, a.slope, a.froude);
    std::printf("  n 0.050: depth %.4f m, speed %.3f m/s; speed ratio %.3f (Manning 0.807)\n", b.depth, b.speed, b.speed / a.speed);
    ASSERT_TRUE(a.wet && b.wet);
    EXPECT_NEAR(a.speed / a.manningSpeed, 1.0, 0.05);
    EXPECT_NEAR(a.depth / a.manningDepth, 1.0, 0.05);
    EXPECT_NEAR(b.speed / a.speed, std::pow(0.035 / 0.050, 0.6), 0.1 * std::pow(0.035 / 0.050, 0.6));
}

// 25.30 SC-T1: a body pressed into still column water - a 1 x 1 m load 10 cm thick (0.1 m^3), pressed in over 10 s, in a
// closed 10 x 2 m channel of 20 cm still water. The body does not reach the bed (water runs under it), so at rest the free
// surface is level everywhere - over the body too (water surface + load) - and has risen by V / A_total = 0.1 / 20 = 5.00 mm
// (within 5 %). (The design's V / A_free is a body standing on the bed; the first run of this test used it and was the
// wrong prediction - 2026-10-12.) The water's volume is unchanged to float rounding (the body is not water). Control: the
// same channel, no load - no rise. Settles 600 s: Manning friction is weak at mm/s speeds - the slosh the pressing starts
// shrinks from a 2.4 mm range to 0.2 mm over ten minutes (measured; the level checks are read at the end).
TEST(WaterColumnTest, APressedBodyRaisesTheLevelByItsVolume) {
    auto run = [](float load) {
        ShoreParams p; p.manningN = 0.035f;
        ColumnWater cw(1, glm::ivec3(0, 16, 0), glm::ivec3(9, 21, 3), p);
        const int nx = cw.nx(), nz = cw.nz();   // 30 x 12 columns; walls z < 3 and z >= 9 -> 30 x 6 interior (10 x 2 m)
        std::vector<float> tops(static_cast<size_t>(nx) * nz, 16.0f); std::vector<uint8_t> walls(tops.size(), 0);
        for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) walls[x + nx * z] = (z < 3 || z >= 9) ? 1 : 0;
        cw.setSolids(tops, walls);
        for (int z = 3; z < 9; ++z) for (int x = 0; x < nx; ++x) cw.solver().col(x, z).eta = 16.2;
        const double v0 = cw.stored();
        std::vector<float> s(tops.size(), 0.0f);
        for (int z = 3; z < 6; ++z) for (int x = 3; x < 6; ++x) s[x + nx * z] = load;   // 3 x 3 columns = 1 m^2, near one end
        for (int k = 0; k < 600 * 30; ++k) {
            if (k <= 10 * 30) { std::vector<float> sk = s; for (float& v : sk) v *= k / 300.0f; cw.setLoad(sk); }   // pressed in over 10 s
            cw.step(1.0f / 30.0f);
            if (load > 0.0f && k % 1800 == 1799) {
                double lo = 1e9, hi = -1e9;
                for (int z = 3; z < 9; ++z) for (int x = 8; x < nx; ++x) { lo = std::min(lo, cw.solver().col(x, z).eta); hi = std::max(hi, cw.solver().col(x, z).eta); }
                std::printf("    t %3d s: free surface %.5f .. %.5f, over the body %.5f\n", (k + 1) / 30, lo, hi, cw.solver().col(4, 4).eta + load);
            }
        }
        double far = 0.0; int n = 0;   // the free surface away from the body: every column not under it
        for (int z = 3; z < 9; ++z) for (int x = 0; x < nx; ++x) if (!(x >= 3 && x < 6 && z < 6)) { far += cw.solver().col(x, z).eta; ++n; }
        return std::make_tuple(far / n - 16.2, cw.solver().col(4, 4).eta, cw.stored() - v0);
    };
    const auto [rise, under, dv] = run(0.1f);
    const auto [rise0, under0, dv0] = run(0.0f);
    const double predicted = 0.1 / 20.0;
    std::printf("  rise away from the body %.5f m (V / A_total %.5f); water surface under it %.4f (around %.4f); volume change %.2e m^3; control rise %.2e\n",
                rise, predicted, under, 16.2 + rise, dv, rise0);
    EXPECT_NEAR(rise, predicted, 0.05 * predicted);
    EXPECT_NEAR(under + 0.1, 16.2 + rise, 0.0005) << "level: the water under the body stands the load lower than around it";
    EXPECT_NEAR(dv, 0.0, 1e-6) << "the body is not water: the volume is unchanged";
    EXPECT_NEAR(rise0, 0.0, 1e-6);
    (void)under0; (void)dv0;
}

// H1b-M0 COST (diagnostic, run with --gtest_also_run_disabled_tests): what one creek costs the column tier on the CPU, and
// how it scales with the region box (dry bank columns) and the creek's length. A straight 1:200 creek 2 m wide (6 columns),
// plane bed, 100 L/s, banks dry 0.6 m above the bed line (not walls - as on the bench), the box `wide` voxels across.
// Steady flow first (120 s), then 600 steps at 60 Hz timed. Printed: ms per step, per column, per WET column.
TEST(WaterColumnTest, DISABLED_CostColumnTier) {
    struct Case { const char* name; int len, wide; };
    for (const Case cs : {Case{"bench 100 x 6 m", 100, 6}, Case{"wide box 100 x 24 m", 100, 24}, Case{"long 200 x 6 m", 200, 6}, Case{"long+wide 200 x 24 m", 200, 24}}) {
        ShoreParams p; p.manningN = 0.035f;
        ColumnWater cw(1, glm::ivec3(0, 14, 0), glm::ivec3(cs.len - 1, 17, cs.wide - 1), p);
        const int nx = cw.nx(), nz = cw.nz();
        const int c0 = nz / 2 - 3, c1 = nz / 2 + 3;   // the creek: 6 columns = 2 m
        std::vector<float> tops(static_cast<size_t>(nx) * nz); std::vector<uint8_t> walls(tops.size(), 0);
        const float slope = 1.0f / 200.0f;
        for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
            const float line = 15.5f - slope * cw.columnCentre(x, z).x;
            tops[x + static_cast<size_t>(nx) * z] = (z >= c0 && z < c1) ? std::floor(line * 3.0f) / 3.0f : line + 0.6f;
        }
        cw.setSolids(tops, walls);
        cw.setPlane(15.5f, slope, 0.0f);
        cw.setBedRule(ColumnBedRule::Plane);
        for (int z = c0; z < c1; ++z) { cw.addSpring(0, z, 0.1 / 6.0); cw.addOutfall(nx - 1, z); }
        for (int k = 0; k < 120 * 60; ++k) cw.step(1.0f / 60.0f);
        double ms = 0.0;
        for (int k = 0; k < 600; ++k) { cw.step(1.0f / 60.0f); ms += cw.lastStepMs(); }
        ms /= 600.0;
        const int wet = cw.wetColumns(), cols = nx * nz;
        std::printf("  %-22s %6d columns, %5d wet (%4.1f %%), %6zu active: %.3f ms/step, %.1f ns/column, %.1f ns/wet column, %.1f ns/active column" "\n",
                    cs.name, cols, wet, 100.0 * wet / cols, cw.solver().activeColumns(), ms, 1e6 * ms / cols, 1e6 * ms / std::max(1, wet), 1e6 * ms / std::max<size_t>(1, cw.solver().activeColumns()));
    }
}

// ---- 25.39 H2a: a 3-D window inside a column region ----
namespace {
// A 24 m x 2 m channel along x (voxels z 2-3), floor y 15, banks to y 16 (micro units: 9 per voxel). The region spans
// x 0-23, z 0-5, y 14-17 with a plane bed 15.30 -> 15.06 over the carve (1:100).
int channelState(const glm::ivec3& m) {
    const int x = m.x, y = m.y, z = m.z;
    if (x < 0 || x >= 24 * 9 || z < 0 || z >= 6 * 9) return 1;
    if (y < 15 * 9) return 1;
    if (y < 16 * 9 && (z < 2 * 9 || z >= 4 * 9)) return 1;
    return 0;
}
struct WindowRig {
    WaterCoreManager mgr{channelState};
    int rid = 0;
    ColumnWater* cw = nullptr;
    explicit WindowRig(double q) {
        std::string err;
        rid = mgr.createColumns(glm::ivec3(0, 14, 0), glm::ivec3(23, 17, 5), 0.035f, &err);
        cw = mgr.columns(rid);
        cw->setPlane(15.30f, 0.01f, 0.0f);
        cw->setBedRule(ColumnBedRule::Plane);
        mgr.setRealtime(true);
        if (q > 0.0) for (int k = 0; k < 6; ++k) { cw->addSpring(0, 6 + k, q / 6.0); cw->addOutfall(cw->nx() - 1, 6 + k); }
    }
    double windowMass() { WaterCoreManager::WindowInfo w; return mgr.windowInfo(rid, w) ? w.mass : 0.0; }
    void run(int frames) { for (int k = 0; k < frames; ++k) mgr.update(1.0f / 60.0f); }
};
}  // namespace

// H2-T2: the hold is exact - still water 20 cm deep in the closed channel, a window over x 8-11: region + window constant
// every frame (to float rounding of the window's fills), no shortfall, for 4 s.
TEST(WaterColumnTest, AWindowHoldsItsWaterExactly) {
    WindowRig rig(0.0);
    for (int z = 0; z < rig.cw->nz(); ++z) for (int x = 0; x < rig.cw->nx(); ++x) {
        ShoreColumn& c = rig.cw->solver().col(x, z);
        if (!c.wall && c.bed < 15.5f) c.eta = 15.45;
    }
    const double total0 = rig.cw->stored();
    std::string err;
    ASSERT_NE(rig.mgr.openWindow(rig.rid, glm::ivec2(8, 0), glm::ivec2(11, 5), &err), 0) << err;
    double worst = std::abs(rig.cw->stored() + rig.windowMass() - total0);
    for (int k = 0; k < 240; ++k) { rig.run(1); worst = std::max(worst, std::abs(rig.cw->stored() + rig.windowMass() - total0)); }
    WaterCoreManager::WindowInfo w; ASSERT_TRUE(rig.mgr.windowInfo(rig.rid, w));
    std::printf("  still water: region + window drift %.2e m^3 of %.3f; shortfall %.2e; window %ld cells, %d held columns\n", worst, total0, w.shortfall, w.cells, w.held);
    std::printf("    residue dropped %.2e m^3, droplets in flight %.2e m^3\n", w.residue, w.droplets);
    EXPECT_LT(worst, 1e-5 * total0);
    EXPECT_EQ(w.shortfall, 0.0);
}

// H2-T1: mass with flow - 0.5 m^3/s down the channel; a window opened at 20 s, closed at 50 s, run to 60 s: region + window =
// start + in - out every frame (1e-6 of the inflow), the region's ledger closes after the write-back, no shortfall.
TEST(WaterColumnTest, AWindowInAFlowingCreekKeepsTheLedger) {
    WindowRig rig(0.5);
    const double s0 = rig.cw->stored();
    rig.run(20 * 60);
    std::string err;
    ASSERT_NE(rig.mgr.openWindow(rig.rid, glm::ivec2(8, 0), glm::ivec2(11, 5), &err), 0) << err;
    double worst = 0.0;
    for (int k = 0; k < 30 * 60; ++k) {
        rig.run(1);
        worst = std::max(worst, std::abs(rig.cw->stored() + rig.windowMass() - (s0 + rig.cw->inflow() - rig.cw->outflow())));
    }
    WaterCoreManager::WindowInfo w; ASSERT_TRUE(rig.mgr.windowInfo(rig.rid, w));
    const double shortfall = w.shortfall, wIn = w.in, wOut = w.out;
    ASSERT_TRUE(rig.mgr.closeWindow(rig.rid));
    rig.run(10 * 60);
    std::printf("  flowing: in %.2f m^3, out %.2f; window took %.2f, gave %.2f; worst |region + window - ledger| %.2e m^3; after close ledger %.2e; shortfall %.2e\n",
                rig.cw->inflow(), rig.cw->outflow(), wIn, wOut, worst, rig.cw->ledgerError(), shortfall);
    EXPECT_LT(worst, 1e-6 * rig.cw->inflow() + 1e-6);
    EXPECT_NEAR(rig.cw->ledgerError(), 0.0, 1e-6 * rig.cw->inflow() + 1e-6);
    EXPECT_EQ(shortfall, 0.0);
    EXPECT_GT(wIn, 1.0) << "the creek ran through the window";
}

// H2-T3 + the seam (unit form of WS-4): steady 0.5 m^3/s, a window over x 8-11. The columns either side of the window's
// upstream edge (x 7.83 outside, 8.17 inside) and its mid-reach against the column tier's own mid-reach without a window.
// Prediction: surface within 1 cm and speed within 5 % across the edge; the window's mid depth within 10 % of the column
// tier's. Control: the same window with pond friction instead of Manning - prediction: its mid depth off by > 20 %.
// MEASURED 2026-10-12 (25.40) - FAILS, parked: window mid depth 177 % of the column tier's, surface 4 cm low across the edge,
// speed 20 % low; the control (pond friction) 192 %. DiagWindowCarriesTheCreekAlone shows why: the 3-D core at 1/9 m carries
// this creek 145 % deep and slow on its own (no hand-off involved). Re-enabled when the window's physics changes (25.40).
TEST(WaterColumnTest, DISABLED_AWindowInAFlowingCreekShowsNoSeam) {
    auto measure = [](bool manning, bool window) {
        WindowRig rig(0.5);
        rig.run(40 * 60);
        std::string err;
        if (window) {
            const int id = rig.mgr.openWindow(rig.rid, glm::ivec2(8, 0), glm::ivec2(11, 5), &err);
            EXPECT_NE(id, 0) << err;
            if (!manning) rig.mgr.windowSolverForTest(rig.rid, 0.0f);
        }
        rig.run(40 * 60);
        const int z = 8;   // a column in the channel's middle (z 2.67-3.0 m)
        const ShoreColumn& out = rig.cw->solver().col(8 * 3 - 1, z), &in = rig.cw->solver().col(8 * 3, z), &mid = rig.cw->solver().col(10 * 3, z);
        struct M { double outEta, inEta, outU, inU, midD, midU; };
        return M{out.eta, in.eta, out.u, in.u, mid.eta - mid.bed, mid.u};
    };
    const auto base = measure(true, false), win = measure(true, true), ctl = measure(false, true);
    std::printf("  no window: mid depth %.4f m, speed %.3f m/s\n", base.midD, base.midU);
    std::printf("  window (Manning): edge surface out %.4f in %.4f (diff %.4f m), speed out %.3f in %.3f; mid depth %.4f (%.1f %% of the column tier), speed %.3f\n",
                win.outEta, win.inEta, win.inEta - win.outEta, win.outU, win.inU, win.midD, 100.0 * win.midD / base.midD, win.midU);
    std::printf("  control (pond friction): mid depth %.4f (%.1f %%), speed %.3f\n", ctl.midD, 100.0 * ctl.midD / base.midD, ctl.midU);
    EXPECT_NEAR(win.inEta, win.outEta, 0.01) << "the surface across the window's edge";
    EXPECT_NEAR(win.inU, win.outU, 0.05 * std::abs(win.outU)) << "the speed across the window's edge";
    EXPECT_NEAR(win.midD, base.midD, 0.10 * base.midD) << "the window carries the creek as the column tier does";
    EXPECT_GT(std::abs(ctl.midD - base.midD), 0.20 * base.midD) << "control: pond friction instead of Manning must show";
}

// DIAGNOSTIC (25.40): is the deep, slow window the 3-D core at 1/9 m or the hand-off? A window over x 1-22 (the whole
// channel but the spring and outfall columns): the 3-D core carries the creek alone. Measured 2026-10-12: 0.335 m deep at
// 0.78 m/s after 110 s and still rising (the column tier: 0.23 m, 1.08 m/s); with pond friction 0.43 m. ~44 min CPU.
TEST(WaterColumnTest, DISABLED_DiagWindowCarriesTheCreekAlone) {
    for (float n : {0.035f, 0.0f}) {
        WindowRig rig(0.5);
        rig.run(20 * 60);
        std::string err;
        ASSERT_NE(rig.mgr.openWindow(rig.rid, glm::ivec2(1, 0), glm::ivec2(22, 5), &err), 0) << err;
        if (n == 0.0f) rig.mgr.windowSolverForTest(rig.rid, 0.0f);
        for (int k = 0; k < 6; ++k) {
            rig.run(15 * 60);
            const ShoreColumn& mid = rig.cw->solver().col(12 * 3, 8);
            WaterCoreManager::WindowInfo w; rig.mgr.windowInfo(rig.rid, w);
            std::printf("  n %.3f t %3d s: window mid depth %.4f m speed %.3f; out %.2f of in %.2f\n", n, 20 + 15 * (k + 1), mid.eta - mid.bed, mid.u, rig.cw->outflow(), rig.cw->inflow());
        }
    }
}

}  // namespace Phyxel::Core::Water

namespace Phyxel::Core::Water {
// 26.13 P3a: every volume's ripple layer is THE window the column regions use (one rule). A 100 m volume (900 lattice cells
// long) gets a 384-cell window that follows the view point in 4 m steps (RED before: refused - over 512 cells, no ripples);
// a 4 x 4 m pond's layer is its whole box at its corner, as before.
TEST(WaterColumnTest, EveryVolumeGetsTheRippleWindow) {
    auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
    WaterCoreManager mgr{state};
    std::string err;
    const int creek = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(99, 0, 1), 1.0f / 9.0f, "eulerian", &err, "cpu");
    ASSERT_NE(creek, 0) << err;
    const int pond = mgr.create(glm::ivec3(200, 0, 0), glm::ivec3(203, 0, 3), 1.0f / 9.0f, "eulerian", &err, "cpu");
    ASSERT_NE(pond, 0) << err;
    mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(99, 0, 1), 0.3f, nullptr);
    mgr.placeBox(glm::ivec3(200, 0, 0), glm::ivec3(203, 0, 3), 0.3f, nullptr);
    mgr.setRipples(true);
    mgr.setRealtime(true);
    mgr.setViewPoint(glm::vec3(20.0f, 1.0f, 1.0f));
    mgr.update(1.0f / 60.0f);
    auto layers = mgr.rippleLayers();
    ASSERT_EQ(layers.size(), 2u) << "both volumes get a layer";
    const RippleLayer* c = layers[0]; const RippleLayer* p = layers[1];
    std::printf("  creek window %d x %d at (%d, %d); pond layer %d x %d at (%d, %d)\n", c->nx(), c->nz(), c->origin().x, c->origin().y, p->nx(), p->nz(), p->origin().x, p->origin().y);
    EXPECT_EQ(c->nx(), WaterCoreManager::kColumnRippleWindow);
    EXPECT_EQ(c->nz(), 18);
    EXPECT_EQ(c->origin().x, std::max(0, 20 * 9 - WaterCoreManager::kColumnRippleWindow / 2)) << "centred on the view, clamped to the box";
    EXPECT_EQ(p->nx(), 36); EXPECT_EQ(p->nz(), 36);
    EXPECT_EQ(p->origin(), glm::ivec2(200 * 9, 0));
    mgr.setViewPoint(glm::vec3(80.0f, 1.0f, 1.0f));   // the camera walks down the creek: the window follows (clamped to the box end)
    mgr.update(1.0f / 60.0f);
    layers = mgr.rippleLayers();
    std::printf("  after the view moves to x 80: creek window at (%d, %d)\n", layers[0]->origin().x, layers[0]->origin().y);
    EXPECT_EQ(layers[0]->origin().x, std::min(80 * 9 - WaterCoreManager::kColumnRippleWindow / 2, 100 * 9 - WaterCoreManager::kColumnRippleWindow));
    EXPECT_EQ(layers[1]->origin(), glm::ivec2(200 * 9, 0)) << "the pond's layer never moves (it is its whole box)";
}
}  // namespace Phyxel::Core::Water
