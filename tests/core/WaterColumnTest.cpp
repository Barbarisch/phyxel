// WaterCore.md 25.18 H1 step 1 - the column-water region (ColumnWater): the ledger with springs and outfalls (C-T1), the
// hydraulic bed is a pure function of the tops (C-T7), and the three bed rules on a 30 m channel whose bed is a 1/3 m
// staircase (the owner's 1/3 m creek-bed rule, 25.19) - the comparison's first numbers, before the bench.
#include <gtest/gtest.h>
#include "core/water/ColumnWater.h"

#include <cmath>
#include <cstdio>

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

}  // namespace Phyxel::Core::Water
