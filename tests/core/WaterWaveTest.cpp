#include <gtest/gtest.h>

#include "core/water/WaterCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// Owner 2026-10-11 ("do 1"): where does a mid-cell wave lose its energy? The wave's own amplitude - the whole surface
// projected onto cos(k x) - so ripples and noise do not count; envelope = max |A| over each theoretical period.
// Returns the amplitude kept per period (geometric mean over periods 1..4). Printed only - a bisection.
double modeKept(int n, float a, SolverParams prm, float top, bool print) {
    WaterGrid g(GridSpec{glm::ivec3(0), glm::ivec3(12, 9, 12), kH});
    const GridSpec sp = g.spec();
    SolidQuery q = [sp](const glm::ivec3& c) -> Occ {
        return (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= sp.dims.x || c.y >= sp.dims.y || c.z >= sp.dims.z) ? Occ::Solid : Occ::Air;
    };
    const float L = 4.0f, k = static_cast<float>(n) * 3.14159265f / L, d = 4.0f * kH + top * kH;
    g.fillBox({0, 0, 0}, {11, 3, 11}, 1.0f);
    for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) g.f(x, 4, z) = top + a / kH * std::cos(k * (x + 0.5f) * kH);
    WaterSolver s(g, q, prm);
    const double T = 2.0 * 3.14159265 / std::sqrt(kG * k * std::tanh(k * d));
    const int periods = 5, ticks = static_cast<int>(periods * T / kDt) + 1;
    std::vector<double> env(periods, 0.0);
    double ke = 0.0;
    for (int t = 0; t < ticks; ++t) {
        s.step(kDt);
        double A = 0.0;
        for (int x = 0; x < 12; ++x) {
            double col = 0.0;
            for (int z = 0; z < 12; ++z) col += g.surfaceWorldY(x, z);
            A += (col / 12.0 - d) * std::cos(k * (x + 0.5f) * kH);
        }
        A *= 2.0 / 12.0;
        const int pi = std::min(periods - 1, static_cast<int>((t + 1) * kDt / T));
        env[pi] = std::max(env[pi], std::abs(A));
        if (pi == 0) ke = std::max(ke, g.kineticEnergy());
    }
    const double kept = env[0] > 1e-6 ? std::pow(env[4] / env[0], 1.0 / 4.0) : 0.0;
    if (print) {
        std::printf("    mode %d, top %.2f: envelope per period (cm)", n, top);
        for (double e : env) std::printf(" %.2f", e * 100.0);
        std::printf(" | kept per period %.2f\n", kept);
    }
    return kept;
}

// Is there water ABOVE the wave's top cell (y = 5) - the film compaction then pulls back? A 5 cm wave on a half-full
// top cell never reaches y = 5 (0.5 + 0.15 < 1), so anything there is the transport's smear.
TEST(WaterWaveTest, DiagFilmAboveTheWave) {
    for (uint32_t stages : {0u, 1u}) {
        WaterGrid g(GridSpec{glm::ivec3(0), glm::ivec3(12, 9, 12), kH});
        const GridSpec sp = g.spec();
        SolidQuery q = [sp](const glm::ivec3& c) -> Occ {
            return (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= sp.dims.x || c.y >= sp.dims.y || c.z >= sp.dims.z) ? Occ::Solid : Occ::Air;
        };
        const float k = 2.0f * 3.14159265f / 4.0f;
        g.fillBox({0, 0, 0}, {11, 3, 11}, 1.0f);
        for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) g.f(x, 4, z) = 0.5f + 0.05f / kH * std::cos(k * (x + 0.5f) * kH);
        SolverParams prm; prm.debugDisableStages = stages;
        WaterSolver s(g, q, prm);
        std::printf(" compaction %s: max f at y=5 / y=4 every 0.2 s:", stages ? "OFF" : "on");
        for (int t = 1; t <= 120; ++t) {
            s.step(kDt);
            if (t % 12 == 0) {
                float m5 = 0.0f, m4 = 0.0f;
                for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) { m5 = std::max(m5, g.f(x, 5, z)); m4 = std::max(m4, g.f(x, 4, z)); }
                std::printf(" %.3f/%.2f", m5, m4);
            }
        }
        std::printf("\n");
    }
}

TEST(WaterWaveTest, DiagWaveLossBisect) {
    struct Row { const char* name; SolverParams p; };
    std::vector<Row> rows;
    auto add = [&](const char* nm, auto f) { SolverParams p; f(p); rows.push_back({nm, p}); };
    add("shipped", [](SolverParams&) {});
    add("compaction off (1)", [](SolverParams& p) { p.debugDisableStages = 1; });
    add("thin-film slope off (2)", [](SolverParams& p) { p.debugDisableStages = 2; });
    add("thin-film settle off (4)", [](SolverParams& p) { p.debugDisableStages = 4; });
    add("halo first layer only (8)", [](SolverParams& p) { p.debugDisableStages = 8; });
    add("residue sweep off (16)", [](SolverParams& p) { p.debugDisableStages = 16; });
    add("rest damping off (32)", [](SolverParams& p) { p.debugDisableStages = 32; });
    add("wake rule off (64)", [](SolverParams& p) { p.debugDisableStages = 64; });
    add("all stages off (127)", [](SolverParams& p) { p.debugDisableStages = 127; });
    add("thetaMin 0.01", [](SolverParams& p) { p.thetaMin = 0.01; });
    add("thetaMin 0.3", [](SolverParams& p) { p.thetaMin = 0.3; });
    add("threshold 0.3", [](SolverParams& p) { p.liquidThreshold = 0.3f; });
    add("threshold 0.7", [](SolverParams& p) { p.liquidThreshold = 0.7f; });
    add("thetaMin 0.001", [](SolverParams& p) { p.thetaMin = 0.001; });
    add("thetaMin 0.01 + compaction off", [](SolverParams& p) { p.thetaMin = 0.01; p.debugDisableStages = 1; });
    add("thetaMin 0.01 + rest damping off", [](SolverParams& p) { p.thetaMin = 0.01; p.debugDisableStages = 32; });
    add("thetaMin 0.01 + residue off", [](SolverParams& p) { p.thetaMin = 0.01; p.debugDisableStages = 16; });
    add("thetaMin 0.01 + 1+32", [](SolverParams& p) { p.thetaMin = 0.01; p.debugDisableStages = 33; });
    add("thetaMin 0.01 + all off", [](SolverParams& p) { p.thetaMin = 0.01; p.debugDisableStages = 127; });
    add("thetaMin 0.001 + all off", [](SolverParams& p) { p.thetaMin = 0.001; p.debugDisableStages = 127; });
    for (const auto& r : rows) {
        std::printf(" %s:\n", r.name);
        for (float top : {0.25f, 0.5f, 0.75f}) modeKept(2, 0.05f, r.p, top, true);
    }
}

}  // namespace
}  // namespace Phyxel::Core::Water
