// WaterCore.md 25.18 H1 step 1 - the column-water region (ColumnWater): the ledger with springs and outfalls (C-T1), the
// hydraulic bed is a pure function of the tops (C-T7), and the three bed rules on a 30 m channel whose bed is a 1/3 m
// staircase (the owner's 1/3 m creek-bed rule, 25.19) - the comparison's first numbers, before the bench.
#include <gtest/gtest.h>
#include "core/water/ColumnWater.h"

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

}  // namespace Phyxel::Core::Water
