// WaterCore.md 26.20 - why the pond model runs deeper than the column model on a stepped bed (26.15: 6 % at q 0.2 m^3/s, 10 %
// at 0.5 on the creek bench's 1/9 m carve). The creek bench's bed in a straight channel, both models ALONE on the same ground:
// flat reaches (22.2 m) with drops (1/9 m) between - 1:200 overall - Manning n 0.035 at the bed, frictionless side walls (the
// "wide" Manning), a free overfall into a pit at the end. Five reaches; measured over reaches 1-3 (reach 0 holds the inlet's
// acceleration, reach 4 the overfall's drawdown). The one variable is chosen by environment (all diagnostics, DISABLED_):
//   SB_Q      unit discharge, m^2/s (0.1 = the bench's q 0.2 on 2 m; 0.25 = its q 0.5)        default 0.1
//   SB_DROP   the drop between reaches, m (0 = a smooth bed: the whole slope as a tilted frame)  default 1/9
//   SB_REACH  reach length, m (the drop / the reach = the slope the steps carry)               default 22.222
//   SB_TILT   1 = the slope the steps do not carry is a tilted frame (gravityX); 0 = none        default 1
//   SB_H      the pond's cells per metre (9 = 1/9 m, 18 = 1/18 m)                              default 9
//   SB_COLSPEED  1 = the pond's bed Manning from the column's depth-mean speed (26.20 candidate) default 0
//   SB_HC     the column model's columns per metre (3 = the bench's 1/3 m pitch, 9)            default 3
//   SB_T      seconds                                                                        default 300
#include <gtest/gtest.h>
#include "core/water/WaterCore.h"
#include "core/water/ShoreSolver.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace Phyxel::Core::Water {
namespace {

constexpr double kS = 0.005, kN = 0.035, kG = 9.81, kW = 1.0 / 3.0;   // slope, Manning n, gravity, channel width (m)
constexpr int kReaches = 5;

double env(const char* k, double d) { const char* v = std::getenv(k); return v ? std::atof(v) : d; }

struct Rig {
    double q = env("SB_Q", 0.1), drop = env("SB_DROP", 1.0 / 9.0), reach = env("SB_REACH", 200.0 / 9.0), seconds = env("SB_T", 300.0);
    bool tilt = env("SB_TILT", 1.0) != 0.0;
    double vc = env("SB_VC", -1.0);   // 26.21 volume correction time (s); < 0 = the shipped default, 0 = off
    double sSteps() const { return drop / reach; }
    double sTilt() const { return tilt ? kS - sSteps() : 0.0; }
    double dn() const { return std::pow(q * kN / std::sqrt(kS), 0.6); }   // Manning, wide, on the overall slope
    double length() const { return kReaches * reach; }
    double pit() const { return 2.0; }
    /// bed height (m) above the channel's lowest floor at distance x along it; the pit beyond the channel's end is lower by
    /// `pitDepth`
    double bed(double x) const {
        if (x >= length()) return -0.25;
        const int r = std::min(kReaches - 1, static_cast<int>(std::floor(x / reach)));
        return drop * (kReaches - 1 - r);
    }
};

struct Profile {
    std::vector<double> depth;   // per metre along the channel (z-averaged, the water over the local bed)
    double reachMean[kReaches] = {};
    double mid = 0.0;            // reaches 1-3
    double inflow = 0.0, outflow = 0.0, storedRate = 0.0;   // m^2/s over the last third of the run
    double ms = 0.0;
    double buried = 1.0, lift = 0.0;   // 26.20 air: mean fill of the cells with water above them; the water's top above its amount (m)
};

void report(const char* name, const Rig& rig, const Profile& p) {
    std::printf("  %-44s mid (reaches 1-3) %.4f m = %.3f x Manning (%.4f); reaches", name, p.mid, p.mid / rig.dn(), rig.dn());
    for (double d : p.reachMean) std::printf(" %.4f", d);
    std::printf("; in %.4f out %.4f stored %+.5f m^2/s; %.1f ms/tick\n", p.inflow, p.outflow, p.storedRate, p.ms);
    std::printf("    depth every 1 m over reaches 1-2:");
    const int a = static_cast<int>(rig.reach), b = static_cast<int>(3 * rig.reach);
    for (int x = a; x < b && x < static_cast<int>(p.depth.size()); ++x) std::printf(" %.3f", p.depth[static_cast<size_t>(x)]);
    std::printf("\n");
}

void finish(const Rig& rig, Profile& p, const std::vector<double>& depthAt, double cellLen) {
    p.depth.assign(static_cast<size_t>(rig.length()) + 1, 0.0);
    std::vector<double> sum(p.depth.size(), 0.0), n(p.depth.size(), 0.0);
    double rs[kReaches] = {}, rn[kReaches] = {};
    for (size_t i = 0; i < depthAt.size(); ++i) {
        const double x = (static_cast<double>(i) + 0.5) * cellLen;
        if (x >= rig.length()) continue;
        const size_t m = static_cast<size_t>(x);
        sum[m] += depthAt[i]; n[m] += 1.0;
        const int r = std::min(kReaches - 1, static_cast<int>(x / rig.reach));
        rs[r] += depthAt[i]; rn[r] += 1.0;
    }
    for (size_t m = 0; m < p.depth.size(); ++m) p.depth[m] = n[m] > 0 ? sum[m] / n[m] : 0.0;
    for (int r = 0; r < kReaches; ++r) p.reachMean[r] = rn[r] > 0 ? rs[r] / rn[r] : 0.0;
    p.mid = (rs[1] + rs[2] + rs[3]) / (rn[1] + rn[2] + rn[3]);
}

// The pond model (WaterSolver, CPU reference, all faces at once - the default).
Profile runPond(const Rig& rig) {
    const int per = static_cast<int>(env("SB_H", 9.0));
    const float h = 1.0f / static_cast<float>(per);
    WaterSolver::s_bedManningColumnSpeedForTest = env("SB_COLSPEED", 0.0) != 0.0;
    const int nx = static_cast<int>(std::lround((rig.length() + rig.pit()) / h)), nz = std::max(1, static_cast<int>(std::lround(kW / h)));
    const int floorCells = 3;   // the pit's 0.25 m drop + a cell
    auto bedCells = [&](int x) { return floorCells + static_cast<int>(std::lround(rig.bed((x + 0.5) * h) / h)); };   // first air cell
    const int topBed = bedCells(0);
    const int ny = topBed + static_cast<int>(std::ceil(1.6 * rig.dn() / h)) + 4;
    GridSpec spec; spec.origin = glm::ivec3(0); spec.dims = glm::ivec3(nx, ny, nz); spec.h = h;
    WaterGrid g(spec);
    std::vector<int> bc(static_cast<size_t>(nx)); for (int x = 0; x < nx; ++x) bc[static_cast<size_t>(x)] = bedCells(x);
    SolidQuery solids = [&](const glm::ivec3& c) { return (g.inBounds(c.x, c.y, c.z) && c.y >= bc[static_cast<size_t>(c.x)]) ? Occ::Air : Occ::Solid; };
    SolverParams p; p.gravityX = static_cast<float>(kG * rig.sTilt()); p.bedManning = static_cast<float>(kN); p.pondFriction = 0.0f;
    if (std::getenv("SB_VCFILM")) WaterSolver::s_volumeCorrectionUnderFilm = env("SB_VCFILM", 0.0) != 0.0;
    if (rig.vc >= 0.0) p.volumeCorrectionTime = static_cast<float>(rig.vc);   // 26.21: else the shipped default
    WaterSolver s(g, solids, p);
    const double dn = rig.dn(), un = rig.q / dn;
    for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
        if ((x + 0.5) * h >= rig.length()) continue;   // the pit starts empty
        const int b = bc[static_cast<size_t>(x)];
        for (int y = b; y < ny; ++y) {
            g.f(x, y, z) = std::clamp(static_cast<float>(dn / h) - static_cast<float>(y - b), 0.0f, 1.0f);
            if (g.f(x, y, z) > 0.0f) { g.u(x, y, z) = static_cast<float>(un); g.u(x + 1, y, z) = static_cast<float>(un); }
        }
    }
    const int wetCells = static_cast<int>(std::ceil(dn / h));
    const float rate = static_cast<float>(rig.q * nz * h / (nz * wetCells));   // per cell: q x (one cell's width) spread over the wet cells
    for (int z = 0; z < nz; ++z) for (int y = 0; y < wetCells; ++y) s.addSource(glm::ivec3(0, topBed + y, z), rate);
    for (int z = 0; z < nz; ++z) s.setBoundary(glm::ivec2(nx - 1, z), static_cast<float>(0.05 * h));
    const int ticks = static_cast<int>(rig.seconds * 60.0), from = ticks * 2 / 3;
    double placed0 = 0.0, out = 0.0, mass0 = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < ticks; ++k) {
        if (k == from) { for (const auto& sp : s.sources()) placed0 += sp.placedTotal; out = 0.0; mass0 = g.totalMass(); }
        const StepReport rep = s.step(1.0f / 60.0f);
        if (k >= from) out -= rep.boundaryExchange;
    }
    Profile pr;
    pr.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / ticks;
    double placed1 = 0.0; for (const auto& sp : s.sources()) placed1 += sp.placedTotal;
    const double span = (ticks - from) / 60.0, width = nz * h;
    pr.inflow = (placed1 - placed0) / span / width; pr.outflow = out / span / width; pr.storedRate = (g.totalMass() - mass0) / span / width;
    std::vector<double> depthAt(static_cast<size_t>(nx), 0.0);
    for (int x = 0; x < nx; ++x) { double d = 0.0; for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) d += g.f(x, y, z); depthAt[static_cast<size_t>(x)] = d * h / nz; }
    finish(rig, pr, depthAt, h);
    {   // 26.20: the air INSIDE the water over reaches 1-3 - cells with water above them should be full; the top of the water
        // (the highest wet cell's base + its fill, what a surface reads) against the water's amount
        double fillBelow = 0.0, nBelow = 0.0, lift = 0.0, nCol = 0.0;
        for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
            const double xc = (x + 0.5) * h;
            if (xc < rig.reach || xc >= 4 * rig.reach) continue;
            int top = -1; double mass = 0.0;
            for (int y = 0; y < ny; ++y) { if (g.f(x, y, z) > 1e-4f) top = y; mass += g.f(x, y, z); }
            if (top < 0) continue;
            for (int y = bc[static_cast<size_t>(x)]; y < top; ++y) { fillBelow += g.f(x, y, z); nBelow += 1.0; }
            lift += ((top - bc[static_cast<size_t>(x)]) + g.f(x, top, z) - mass) * h; nCol += 1.0;
        }
        std::printf("    air in the water (reaches 1-3): cells with water above them %.4f full on average; the water's top %.4f m above its amount\n",
                    nBelow > 0 ? fillBelow / nBelow : 0.0, nCol > 0 ? lift / nCol : 0.0);
        pr.buried = nBelow > 0 ? fillBelow / nBelow : 1.0; pr.lift = nCol > 0 ? lift / nCol : 0.0;
        if (env("SB_AIRDIAG", 0.0) != 0.0) {   // 26.21: split the part-full cells by what lies above them; one column's fills
            double sl = 0, nl = 0, ss = 0, ns = 0;
            for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
                const double xc = (x + 0.5) * h;
                if (xc < rig.reach || xc >= 4 * rig.reach) continue;
                for (int y = bc[static_cast<size_t>(x)]; y + 1 < ny; ++y) {
                    const float fa = g.f(x, y + 1, z);
                    if (fa <= 1e-4f) continue;
                    if (fa >= p.liquidThreshold) { sl += g.f(x, y, z); nl += 1; } else { ss += g.f(x, y, z); ns += 1; }
                }
            }
            std::printf("    under a liquid cell: %.0f cells, %.4f full; under a thin cell (< %.2f): %.0f cells, %.4f full\n",
                        nl, nl > 0 ? sl / nl : 0.0, p.liquidThreshold, ns, ns > 0 ? ss / ns : 0.0);
            const int xm = static_cast<int>(2.5 * rig.reach / h), zm = nz / 2;
            std::printf("    column x %.1f m bottom up:", (xm + 0.5) * h);
            for (int y = bc[static_cast<size_t>(xm)]; y < ny && g.f(xm, y, zm) > 1e-5f; ++y) std::printf(" %.3f", g.f(xm, y, zm));
            std::printf("\n");
        }
    }
    WaterSolver::s_bedManningColumnSpeedForTest = false;
    return pr;
}

// The column model (ShoreSolver - what the retired ColumnWater stepped, 26.27: springs add depth, outfalls drain to the bed).
Profile runColumn(const Rig& rig) {
    const int per = static_cast<int>(env("SB_HC", 3.0));
    const float hc = 1.0f / static_cast<float>(per);
    const int nx = static_cast<int>(std::lround((rig.length() + rig.pit()) / hc)), nz = std::max(1, static_cast<int>(std::lround(kW / hc)));
    ShoreParams sp; sp.manningN = static_cast<float>(kN);
    ShoreSolver s(glm::vec2(0.0f), nx, nz, hc, sp);
    // the column model has no tilted frame: the slope the steps do not carry is a smooth ramp in its bed (on a smooth ramp the
    // column model is Manning-exact, 25.16)
    const double dn = rig.dn(), un = rig.q / dn;
    for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
        const double xc = (x + 0.5) * hc;
        ShoreColumn& c = s.col(x, z);
        c.bed = static_cast<float>(rig.bed(xc) + (xc < rig.length() ? rig.sTilt() * (rig.length() - xc) : 0.0));
        c.eta = xc < rig.length() ? c.bed + dn : c.bed;
        c.u = xc < rig.length() ? static_cast<float>(un) : 0.0f;
    }
    const int ticks = static_cast<int>(rig.seconds * 60.0), from = ticks * 2 / 3;
    double in = 0.0, out = 0.0, mass0 = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < ticks; ++k) {
        if (k == from) { in = out = 0.0; mass0 = s.totalMass(); }
        const float dt = 1.0f / 60.0f;
        for (int z = 0; z < nz; ++z) { s.col(0, z).eta += rig.q * dt / hc; if (k >= from) in += rig.q * hc * dt; }
        s.step(dt);
        for (int z = 0; z < nz; ++z) {
            ShoreColumn& c = s.col(nx - 1, z);
            const double d = c.eta - c.bed;
            if (d > 0.0) { if (k >= from) out += d * hc * hc; c.eta = c.bed; c.u = c.w = 0.0f; }
        }
    }
    Profile pr;
    pr.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / ticks;
    const double span = (ticks - from) / 60.0, width = nz * hc;
    pr.inflow = in / span / width; pr.outflow = out / span / width; pr.storedRate = (s.totalMass() - mass0) / span / width;
    std::vector<double> depthAt(static_cast<size_t>(nx), 0.0);
    for (int x = 0; x < nx; ++x) { double d = 0.0; for (int z = 0; z < nz; ++z) d += std::max(0.0, s.depth(x, z)); depthAt[static_cast<size_t>(x)] = d / nz; }
    finish(rig, pr, depthAt, hc);
    return pr;
}

std::string label(const char* model, const Rig& rig) {
    char b[160];
    std::snprintf(b, sizeof b, "%s q %.2f drop %.3f reach %.1f%s%s", model, rig.q, rig.drop, rig.reach, rig.tilt ? " +tilt" : "",
                  env("SB_COLSPEED", 0.0) != 0.0 && model[0] == 'p' ? " colspeed" : "");
    return b;
}

}  // namespace

TEST(WaterSteppedBedTest, DISABLED_DiagPond) {
    const Rig rig; char nm[200]; std::snprintf(nm, sizeof nm, "%s h 1/%d", label("pond", rig).c_str(), static_cast<int>(env("SB_H", 9.0)));
    report(nm, rig, runPond(rig));
}

// 26.21: moving water holds no air. A short smooth channel (five 2 m reaches, the slope a tilted frame, 1/9 m cells, 60 s):
// the cells with water above them are full and the water's top sits on its amount. Red before the volume correction (26.20
// measured the long rig at 0.92 full, the top 9 mm high; this rig reads 0.79 full and 29 mm without it).
TEST(WaterSteppedBedTest, MovingWaterHoldsNoAir) {
    Rig rig; rig.q = 0.1; rig.drop = 0.0; rig.reach = 2.0; rig.seconds = 60.0; rig.tilt = true;
    const Profile pr = runPond(rig);
    report("pond q 0.10 smooth 10 m h 1/9", rig, pr);
    // >= 0.98, not the 0.99 predicted: SolverParams::volumeCorrectionBand leaves a deficit under 2 % alone (the wave rows, 26.21)
    EXPECT_GE(pr.buried, 0.98) << "cells with water above them should be full";
    EXPECT_LE(pr.lift, 0.0025) << "the water's top should sit on its amount (m) - within the 2 % band (2.2 mm at 1/9 m)";
    // the water itself is unchanged: the same channel without the correction (a 10 m channel is not long enough for Manning's
    // uniform depth - the outfall draws it down - so the control is the channel itself)
    Rig off = rig; off.vc = 0.0;
    const Profile pr0 = runPond(off);
    report("  control: volume correction off", off, pr0);
    EXPECT_GT(pr0.lift, 0.01) << "the control should show the air (else the rig no longer makes it)";
    EXPECT_NEAR(pr.mid / pr0.mid, 1.0, 0.02) << "the water's depth should not change";
}

TEST(WaterSteppedBedTest, DISABLED_DiagColumn) {
    const Rig rig; char nm[200]; std::snprintf(nm, sizeof nm, "%s hc 1/%d", label("column", rig).c_str(), static_cast<int>(env("SB_HC", 3.0)));
    report(nm, rig, runColumn(rig));
}

// 26.21 diagnostic: the submerged pump (WaterCoreTest.SubmergedPumpDelivers' rig: 6 x 8 x 1 cells of 1 m, 18 m^3, 0.5 m^3/s
// two cells under the surface, 8 s) - the water's total after each stage of the step, against what the pump placed. Every
// stage but the source should move the total by nothing.
TEST(WaterSteppedBedTest, DISABLED_DiagPumpMassByStage) {
    GridSpec spec; spec.origin = glm::ivec3(0); spec.dims = glm::ivec3(6, 8, 1); spec.h = 1.0f;
    WaterGrid g(spec);
    g.fillBox({0, 0, 0}, {5, 2, 0}, 1.0f);
    SolidQuery solids = [&](const glm::ivec3& c) { return g.inBounds(c.x, c.y, c.z) ? Occ::Air : Occ::Solid; };
    SolverParams p; if (env("SB_VC", -1.0) >= 0.0) p.volumeCorrectionTime = static_cast<float>(env("SB_VC", 0.0));
    WaterSolver s(g, solids, p);
    s.addSource({3, 1, 0}, 0.5f);
    std::vector<std::string> names; std::vector<double> d; double prev = g.totalMass(), residue = 0.0;
    s.stageProbeForTest = [&](const char* st) {
        const double m = g.totalMass();
        size_t i = 0; while (i < names.size() && names[i] != st) ++i;
        if (i == names.size()) { names.push_back(st); d.push_back(0.0); }
        d[i] += m - prev; prev = m;
    };
    for (int k = 0; k < 8 * 60; ++k) {
        const StepReport r = s.step(1.0f / 60.0f); residue += r.residueDropped;
        const double m = g.totalMass();
        size_t i = 0; while (i < names.size() && names[i] != "(after step)") ++i;
        if (i == names.size()) { names.push_back("(after step)"); d.push_back(0.0); }
        d[i] += m - prev; prev = m;
    }
    std::printf("  vc %.2f s: placed %.6f, grid %.6f - 18 = %.6f, drift %+.3e, residue dropped %.3e, \n", p.volumeCorrectionTime,
                s.sources()[0].placedTotal, g.totalMass(), g.totalMass() - 18.0, g.totalMass() - 18.0 - s.sources()[0].placedTotal, residue);
    for (size_t i = 0; i < names.size(); ++i) std::printf("    %-20s %+.6e m^3\n", names[i].c_str(), d[i]);
}

}  // namespace Phyxel::Core::Water
