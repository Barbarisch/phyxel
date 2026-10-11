#include <gtest/gtest.h>

#include "core/water/WaterCore.h"

#include <cmath>
#include <cstdio>
#include <vector>

// Owner feedback 2026-10-10: "the water surface deforms a little slow, making it look thicker than water".
// Measurement (printed, not asserted): standing waves in the 20.8 pond (4 x 4 m box, 1.5 m deep, 1/3 m cells, the
// shipped cell) - their period against linear theory, omega^2 = g k tanh(k d), and their decay per period. Real
// water barely damps a metre-scale wave (viscous decay rate 2 nu k^2 ~ 1e-5 /s); what is lost here is the solver's.

namespace Phyxel::Core::Water {
namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kH = 1.0f / 3.0f;
constexpr float kG = 9.81f;

struct Slosh { double periodSim = 0, periodTheory = 0, decayPerPeriod = 0, firstPeak = 0; int peaks = 0; };

// mode n along x: surface 1.5 m + a cos(n pi x / L) (L = 4 m), the top layer's fill carries it
Slosh slosh(int n, float a, float seconds, SolverParams prm = {}, float top = 0.5f) {
    WaterGrid g(GridSpec{glm::ivec3(0), glm::ivec3(12, 9, 12), kH});
    const GridSpec sp = g.spec();
    SolidQuery q = [sp](const glm::ivec3& c) -> Occ {
        return (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= sp.dims.x || c.y >= sp.dims.y || c.z >= sp.dims.z) ? Occ::Solid : Occ::Air;
    };
    const float L = 4.0f, k = static_cast<float>(n) * 3.14159265f / L, d = 4.0f * kH + top * kH;
    g.fillBox({0, 0, 0}, {11, 3, 11}, 1.0f);
    for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x)
        g.f(x, 4, z) = top + a / kH * std::cos(k * (x + 0.5f) * kH);
    WaterSolver s(g, q, prm);
    // the end column's surface over time; peaks of its deviation give the period and the decay
    std::vector<double> y;
    const int ticks = static_cast<int>(seconds / kDt);
    for (int t = 0; t < ticks; ++t) {
        s.step(kDt);
        double m = 0.0;
        for (int z = 0; z < 12; ++z) m += g.surfaceWorldY(0, z);
        y.push_back(m / 12.0 - d);
    }
    Slosh r;
    r.periodTheory = 2.0 * 3.14159265 / std::sqrt(kG * k * std::tanh(k * d));
    std::vector<std::pair<int, double>> peaks;   // local maxima of the deviation (positive lobes: the column starts high)
    for (size_t i = 1; i + 1 < y.size(); ++i)
        if (y[i] > y[i - 1] && y[i] >= y[i + 1] && y[i] > 0.002) peaks.push_back({static_cast<int>(i), y[i]});
    r.peaks = static_cast<int>(peaks.size());
    if (peaks.size() >= 2) {
        r.firstPeak = peaks[0].second;
        r.periodSim = (peaks.back().first - peaks.front().first) * kDt / (peaks.size() - 1);
        r.decayPerPeriod = std::pow(peaks.back().second / peaks.front().second, 1.0 / (peaks.size() - 1));
    }
    std::printf("  mode %d (lambda %.2f m), a %.0f cm: period %.3f s (theory %.3f s, x%.2f), amplitude kept per period %.2f over %d peaks (first %.3f m)\n",
                n, 2.0 * L / n, a * 100.0, r.periodSim, r.periodTheory, r.periodSim > 0 ? r.periodSim / r.periodTheory : 0.0, r.decayPerPeriod, r.peaks, r.firstPeak);
    // the first 1.5 s, every 0.1 s
    std::printf("    end column deviation (cm), every 0.1 s:");
    for (size_t i = 5; i < y.size() && i < 90; i += 6) std::printf(" %+.1f", y[i] * 100.0);
    std::printf("\n");
    return r;
}

TEST(WaterWaveTest, DiagStandingWavePeriodAndDecay) {
    std::printf(" shipped solver:\n");
    for (int n : {1, 2, 3, 4}) slosh(n, 0.05f, 8.0f);
    std::printf(" rest damping OFF (stage bit 32):\n");
    SolverParams off; off.debugDisableStages = 32;
    for (int n : {1, 2, 3, 4}) slosh(n, 0.05f, 8.0f, off);
    for (float top : {0.25f, 0.75f}) {
        std::printf(" rest level at %.2f of the top cell (shipped solver):\n", top);
        for (int n : {2, 4}) slosh(n, 0.05f, 8.0f, {}, top);
    }
}

}  // namespace
}  // namespace Phyxel::Core::Water
