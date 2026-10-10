#include <gtest/gtest.h>

#include "core/water/MovingSolids.h"
#include "core/water/WaterCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// docs/WaterCore.md 20 - moving solids: bodies that take up room in an active volume (M0, the CPU core).
// A body is a pump of exactly the water that no longer fits (20.2). Predictions in each test were
// written in 20.8 before the solver change; the T-numbers are the plan's.

namespace Phyxel::Core::Water {
namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kH = 1.0f / 3.0f;

// A 4 x 3 x 4 m stone box (everything outside the grid is solid) holding water 1.5 m deep: 4 full
// layers and a half layer, so the rest level sits mid-cell (f = 0.5 in the top cell).
struct Pond {
    WaterGrid grid;
    explicit Pond(glm::ivec3 origin = glm::ivec3(0)) : grid(GridSpec{origin, glm::ivec3(12, 9, 12), kH}) {}
    SolidQuery query() const {
        const GridSpec s = grid.spec();
        return [s](const glm::ivec3& c) -> Occ {
            const glm::ivec3 l = c - s.origin;
            return (l.x < 0 || l.y < 0 || l.z < 0 || l.x >= s.dims.x || l.y >= s.dims.y || l.z >= s.dims.z) ? Occ::Solid : Occ::Air;
        };
    }
    void fill() {
        grid.fillBox({0, 0, 0}, {11, 3, 11}, 1.0f);
        grid.fillBox({0, 4, 0}, {11, 4, 11}, 0.5f);
    }
};

// the pond's level away from a body: the mean WATER DEPTH (sum f h) over the columns outside [lo, hi] (cells,
// x and z). Not the drawn surface - that counts air trapped under water as water (19.7's pocket weight), so a
// disturbed pond reads high; the predictions here are volumes.
float levelAway(const WaterGrid& g, int lo, int hi) {
    double sum = 0.0; int n = 0;
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x) {
        if (x >= lo && x <= hi && z >= lo && z <= hi) continue;
        double depth = 0.0;
        for (int y = 0; y < g.ny(); ++y) depth += g.f(x, y, z);
        sum += depth * g.h(); ++n;
    }
    return n ? static_cast<float>(sum / n) : std::numeric_limits<float>::quiet_NaN();
}

// the drawn surface's spread over the columns outside [lo, hi]
float drawnLevelAway(const WaterGrid& g, int lo, int hi) {
    double sum = 0.0; int n = 0;
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x) {
        if (x >= lo && x <= hi && z >= lo && z <= hi) continue;
        const float y = g.surfaceWorldY(x, z);
        if (!std::isnan(y)) { sum += y; ++n; }
    }
    return n ? static_cast<float>(sum / n) : std::numeric_limits<float>::quiet_NaN();
}

float maxFillPlusSolid(const WaterGrid& g) {
    float m = 0.0f;
    for (size_t i = 0; i < g.cellCount(); ++i) m = std::max(m, g.fData()[i] + g.sData()[i]);
    return m;
}

// T10's measure: air TRAPPED under water - sum (1 - f - s) V over cells that are not full while a FULL cell
// (f + s >= 0.999) lies above them in the column. (First written as "some cell above holds f >= 0.5": that
// also counted the free surface straddling two cells - 0.92 under a 0.54 top layer at the drop column,
// the 19.7 terrace split, queue item 3 - as air; it is reported separately by surfaceSplit.)
double airUnderWater(const WaterGrid& g) {
    double air = 0.0;
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x) {
        bool fullAbove = false;
        for (int y = g.ny() - 1; y >= 0; --y) {
            const float fs = g.f(x, y, z) + g.s(x, y, z);
            if (fullAbove && fs < 0.999f) air += (1.0 - fs) * g.cellVolume();
            if (fs >= 0.999f) fullAbove = true;
        }
    }
    return air;
}

// the surface straddling two cells: (1 - f - s) V of a not-full cell directly under a part-full liquid top cell
double surfaceSplit(const WaterGrid& g) {
    double v = 0.0;
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x)
        for (int y = 0; y + 1 < g.ny(); ++y) {
            const float fs = g.f(x, y, z) + g.s(x, y, z), fa = g.f(x, y + 1, z) + g.s(x, y + 1, z);
            const bool topAbove = fa >= 0.5f && fa < 0.999f && (y + 2 >= g.ny() || g.f(x, y + 2, z) < 0.5f);
            if (topAbove && fs < 0.999f && fs >= 0.5f) v += (1.0 - fs) * g.cellVolume();
        }
    return v;
}

// T1 - the raster is exact and continuous: the fractions add up to the body's volume inside the grid,
// and moving the body by 1 mm changes no cell by more than 1 mm x (one face) / V.
TEST(WaterSolidTest, RasterIsExactAndContinuous) {
    Pond p;
    MovingSolid b; b.centre = {1.234f, 1.111f, 2.05f}; b.halfExtents = {0.2f, 0.17f, 0.31f};
    std::vector<float> s, s2; std::vector<uint8_t> fr;
    const SolidRaster r = rasterizeSolids(p.grid, {b}, s, fr);
    const double vol = 8.0 * 0.2 * 0.17 * 0.31;
    EXPECT_EQ(r.bodies, 1);
    EXPECT_NEAR(r.volumeInside, vol, 1e-5);
    MovingSolid b2 = b; b2.centre.x += 0.001f;
    rasterizeSolids(p.grid, {b2}, s2, fr);
    float worst = 0.0f;
    for (size_t i = 0; i < s.size(); ++i) worst = std::max(worst, std::abs(s2[i] - s[i]));
    EXPECT_LE(worst, 0.001f * kH * kH / (kH * kH * kH) + 1e-5f) << "a 1 mm move changed a cell by more than one face's sweep";
    // half outside the grid: only the inside half counts
    MovingSolid out; out.centre = {0.0f, 1.0f, 2.0f}; out.halfExtents = {0.25f, 0.25f, 0.25f};
    EXPECT_NEAR(rasterizeSolids(p.grid, {out}, s, fr).volumeInside, 0.5 * 0.125, 1e-5);
    // degenerate bodies are refused and counted
    MovingSolid bad; bad.centre = {1.0f, NAN, 1.0f}; bad.halfExtents = {0.1f, 0.1f, 0.1f};
    MovingSolid flat; flat.centre = {1.0f, 1.0f, 1.0f}; flat.halfExtents = {0.1f, 0.0f, 0.1f};
    const SolidRaster rb = rasterizeSolids(p.grid, {bad, flat}, s, fr);
    EXPECT_EQ(rb.refused, 2);
    EXPECT_EQ(rb.cells, 0);
}

// T7 - world-aligned, chunk-independent: the same body rasterized into two volumes whose boxes differ
// (both straddle the chunk seam at world x = 96 = cell 288) gives identical s for every shared world cell.
TEST(WaterSolidTest, WorldAlignedRasterAcrossChunkSeam) {
    Pond a(glm::ivec3(280, 0, 0)), b(glm::ivec3(285, 0, 3));
    MovingSolid body; body.centre = {96.05f, 1.2f, 2.3f}; body.halfExtents = {0.4f, 0.3f, 0.35f};
    std::vector<float> sa, sb; std::vector<uint8_t> fr;
    const SolidRaster ra = rasterizeSolids(a.grid, {body}, sa, fr);
    const SolidRaster rb = rasterizeSolids(b.grid, {body}, sb, fr);
    long shared = 0;
    for (int z = 0; z < 12; ++z) for (int y = 0; y < 9; ++y) for (int x = 0; x < 12; ++x) {
        const glm::ivec3 w = a.grid.spec().origin + glm::ivec3(x, y, z);
        const glm::ivec3 lb = w - b.grid.spec().origin;
        if (!b.grid.inBounds(lb.x, lb.y, lb.z)) continue;
        ++shared;
        EXPECT_EQ(sa[a.grid.idx(x, y, z)], sb[b.grid.idx(lb.x, lb.y, lb.z)]) << "cell " << w.x << "," << w.y << "," << w.z;
    }
    EXPECT_GT(shared, 0);
    EXPECT_NEAR(ra.volumeInside, rb.volumeInside, 1e-6) << "the body lies inside both volumes";
}

// T2 - displacement raises the level: a 1 m cube lowered at 0.2 m/s until fully submerged raises the
// level by 1 / 16 m = 6.25 cm (+- 5 mm); mass exact. Red before 20: the level does not move.
TEST(WaterSolidTest, DisplacementRaisesTheLevel) {
    Pond p; p.fill();
    WaterSolver s(p.grid, p.query());
    const double m0 = p.grid.totalMass();
    MovingSolid b; b.centre = {2.0f, 2.1f, 2.0f}; b.halfExtents = glm::vec3(0.5f);
    for (int k = 0; k < 405; ++k) {
        b.centre.y = 2.1f - 0.2f * (k + 1) * kDt;
        b.velocity = {0.0f, -0.2f, 0.0f};
        s.setMovingSolids({b}, kDt);
        s.step(kDt);
    }
    b.velocity = glm::vec3(0.0f);
    for (int k = 0; k < 300; ++k) { s.setMovingSolids({b}, kDt); s.step(kDt); }
    const float level = levelAway(p.grid, 4, 7);
    std::printf("  body centre %.3f m, level away from it %.4f m (rest 1.5000, predicted 1.5625; drawn %.4f), mass %.9f -> %.9f\n",
                b.centre.y, level, drawnLevelAway(p.grid, 4, 7), m0, p.grid.totalMass());
    for (int z = 0; z < 12; ++z) for (int y = 0; y < 9; ++y) for (int x = 0; x < 12; ++x)
        if (p.grid.f(x, y, z) + p.grid.s(x, y, z) > 1.0001f)
            std::printf("    overlap cell (%d,%d,%d): f %.4f s %.4f q %.3e kind %d\n", x, y, z, p.grid.f(x, y, z), p.grid.s(x, y, z), p.grid.q(x, y, z), static_cast<int>(p.grid.kind(x, y, z)));
    EXPECT_NEAR(level, 1.5625f, 0.005f) << "the submerged volume must raise the level by V / A";
    EXPECT_NEAR(p.grid.totalMass(), m0, 1e-5);   // float32 fills over 700 busy ticks (the pump test's tolerance: ~4e-6 of 22 m^3)
    EXPECT_LE(maxFillPlusSolid(p.grid), 1.0f + 1e-4f) << "water overlaps the body";
}

// T3 - the ledger is exact under overflow: a 1 m cube appearing inside full water (spawn) has no rate;
// its excess moves up its columns, nothing is created or destroyed, no fill goes negative, the overlap
// is gone within 30 ticks and the level settles 6.25 cm up.
TEST(WaterSolidTest, SpawnInsideWaterIsLedgerExact) {
    Pond p; p.fill();
    WaterSolver s(p.grid, p.query());
    const double m0 = p.grid.totalMass();
    MovingSolid b; b.centre = {2.0f, 0.75f, 2.0f}; b.halfExtents = glm::vec3(0.5f); b.fresh = true;
    const WaterSolver::SolidsReport rep = s.setMovingSolids({b}, kDt);
    EXPECT_GT(rep.freshCells, 0);
    EXPECT_EQ(rep.rateCells, 0) << "a fresh body gets no rate (no fake splash)";
    s.step(kDt);
    float minF = 1.0f;
    for (float v : p.grid.fData()) minF = std::min(minF, v);
    EXPECT_GE(minF, 0.0f);
    EXPECT_NEAR(p.grid.totalMass(), m0, 1e-6);
    b.fresh = false;
    for (int k = 0; k < 30; ++k) { s.setMovingSolids({b}, kDt); s.step(kDt); }
    EXPECT_LE(maxFillPlusSolid(p.grid), 1.0f + 1e-4f) << "the overlap must be gone within 30 ticks";
    for (int k = 0; k < 300; ++k) { s.setMovingSolids({b}, kDt); s.step(kDt); }
    const float level = levelAway(p.grid, 4, 7);
    std::printf("  spawned cube: level away %.4f m (predicted 1.5625; drawn %.4f, air under water %.4f m^3), mass %.9f -> %.9f\n", level, drawnLevelAway(p.grid, 4, 7), airUnderWater(p.grid), m0, p.grid.totalMass());
    EXPECT_NEAR(level, 1.5625f, 0.005f);
    EXPECT_NEAR(p.grid.totalMass(), m0, 1e-6);
}

struct DropRun { float entryMin = 1e9f, ring1Max = -1e9f, ring2Max = -1e9f, ring2MaxEarly = -1e9f, ringAbs = 0.0f; double m0 = 0, m1 = 0, airBefore = 0, airAfter = 0, splitAfter = 0; };

// One 1/3 m stone (cell (6, *, 6)) dropped at 6 m/s from just above the rested pond to its floor, then
// held there; with `solids` false the body is never given to the solver (the A/B control).
DropRun dropStone(bool solids, int holdTicks, uint32_t disableStages = 0) {
    Pond p; p.fill();
    SolverParams prm; prm.debugDisableStages = disableStages;
    WaterSolver s(p.grid, p.query(), prm);
    for (int k = 0; k < 60; ++k) s.step(kDt);   // let it rest
    DropRun r; r.m0 = p.grid.totalMass(); r.airBefore = airUnderWater(p.grid);
    const float cx = 6.5f * kH, he = kH / 2.0f;
    MovingSolid b; b.centre = {cx, 1.5f + he + 0.02f, cx}; b.halfExtents = glm::vec3(he); b.velocity = {0.0f, -6.0f, 0.0f};
    const float rest = 1.5f;
    for (int k = 0; k < 60 + holdTicks; ++k) {
        b.centre.y = std::max(he, b.centre.y - 6.0f * kDt);
        if (b.centre.y <= he) b.velocity = glm::vec3(0.0f);
        if (solids) s.setMovingSolids({b}, kDt);
        s.step(kDt);
        if (k < 60) {
            const float e = p.grid.surfaceWorldY(6, 6), r1 = p.grid.surfaceWorldY(7, 6), r2 = p.grid.surfaceWorldY(8, 6);
            if (!std::isnan(e)) r.entryMin = std::min(r.entryMin, e - rest);
            if (!std::isnan(r1)) r.ring1Max = std::max(r.ring1Max, r1 - rest);
            if (!std::isnan(r2)) { r.ring2Max = std::max(r.ring2Max, r2 - rest); if (k < 30) r.ring2MaxEarly = std::max(r.ring2MaxEarly, r2 - rest); }
            for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) {
                const float y = p.grid.surfaceWorldY(x, z);
                if (!std::isnan(y)) r.ringAbs = std::max(r.ringAbs, std::abs(y - rest));
            }
        }
    }
    r.m1 = p.grid.totalMass(); r.airAfter = airUnderWater(p.grid); r.splitAfter = surfaceSplit(p.grid);
    return r;
}

// T4 - fast entry makes a crater and a crown (20.8, derived predictions): entry column dips >= 8 cm,
// >= 3 cm at 0.33 m, >= 1.5 cm at 0.67 m within 0.5 s, mass exact. Control: the same drop with the body
// never given to the solver moves the surface < 1 cm anywhere.
TEST(WaterSolidTest, FastEntryMakesACraterAndACrown) {
    const DropRun on = dropStone(true, 0), off = dropStone(false, 0);
    std::printf("  solids ON : entry min %+.3f m, 0.33 m max %+.3f m, 0.67 m max %+.3f m (first 0.5 s %+.3f), largest |dev| %.3f m, mass %.9f -> %.9f\n",
                on.entryMin, on.ring1Max, on.ring2Max, on.ring2MaxEarly, on.ringAbs, on.m0, on.m1);
    std::printf("  solids OFF: largest |dev| %.4f m\n", off.ringAbs);
    EXPECT_LE(on.entryMin, -0.08f) << "the entry column must dip (the cavity)";
    EXPECT_GE(on.ring1Max, 0.03f) << "the crown at 0.33 m";
    EXPECT_GE(on.ring2MaxEarly, 0.015f) << "the wave at 0.67 m within 0.5 s";
    EXPECT_NEAR(on.m1, on.m0, 1e-6);
    EXPECT_LT(off.ringAbs, 0.01f) << "control: no body, no splash";
}

// T10 - the cavity closes: 5 s after the stone lands (held on the floor), the air under water is back to
// its pre-drop value + 10 % of the stone's volume. 19.7 found compaction leaves 443 of 446 pockets for
// 30 s; if the swept cavity stays trapped this is RED and blocks M2.
TEST(WaterSolidTest, TheCavityCloses) {
    const DropRun r = dropStone(true, 300);
    const DropRun ctl = dropStone(true, 300, 64u);   // CONTROL: the wake rule off (stage bit 64)
    const double stone = std::pow(kH, 3.0);
    std::printf("  control (dragged air may not rise out): trapped %.5f m^3 - the measure sees the air the rule removes\n", ctl.airAfter);
    EXPECT_GT(ctl.airAfter, 0.1 * stone) << "control: without the rule the stone's air must stay trapped (else the measure is blind)";
    std::printf("  air trapped under water: before %.5f m^3, 6 s after the drop %.5f m^3 (stone %.5f m^3); surface split across two cells %.5f m^3 (not air - item 3)\n", r.airBefore, r.airAfter, stone, r.splitAfter);
    EXPECT_LE(r.airAfter, r.airBefore + 0.1 * stone) << "the cavity behind the stone stayed trapped";
    EXPECT_NEAR(r.m1, r.m0, 1e-6);
}

// T5 - a resting body is quiet: a stone on the floor of a still pond for 60 s lets the volume sleep;
// mass exact; no water overlaps it; the column above it is not drained into it (compaction capacity);
// the drawn surface is flat to 1 cm.
// The stone is in place before the solver starts, the water it displaces already spread on the top layer,
// so nothing disturbs the pond: the control (DiagPondSleeps) shows this CPU pond sleeps at once when left
// alone but NEVER after even a 0.05 m/s push with no body at all - the 19.6 wall-chatter defect, which a
// disturbing placement would trip and which is not this test's subject.
TEST(WaterSolidTest, ARestingBodyIsQuiet) {
    Pond p; p.fill();
    p.grid.f(6, 0, 6) = 0.0f;   // the stone's cell (a 1/3 m cube on the floor)
    for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) p.grid.f(x, 4, z) += 1.0f / 144.0f;   // its water, on the top layer
    WaterSolver s(p.grid, p.query());
    const double m0 = p.grid.totalMass();
    const float he = kH / 2.0f;
    MovingSolid b; b.centre = {6.5f * kH, he, 6.5f * kH}; b.halfExtents = glm::vec3(he);
    for (int k = 0; k < 3600; ++k) { s.setMovingSolids({b}, kDt); s.step(kDt); }
    float lo = 1e9f, hi = -1e9f;
    for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) { const float y = p.grid.surfaceWorldY(x, z); if (!std::isnan(y)) { lo = std::min(lo, y); hi = std::max(hi, y); } }
    std::printf("  resting stone, 60 s: asleep %d, surface %.4f..%.4f m, cell above the stone f %.4f, max f+s %.5f, mass %.9f -> %.9f\n",
                s.asleep() ? 1 : 0, lo, hi, p.grid.f(6, 1, 6), maxFillPlusSolid(p.grid), m0, p.grid.totalMass());
    EXPECT_TRUE(s.asleep()) << "a resting body must let the pond sleep";
    EXPECT_NEAR(p.grid.totalMass(), m0, 1e-6);
    EXPECT_LE(maxFillPlusSolid(p.grid), 1.0f + 1e-4f);
    EXPECT_GE(p.grid.f(6, 1, 6), 0.99f) << "the column above the stone drained into it";
    EXPECT_LT(hi - lo, 0.01f) << "the drawn surface is not flat";
}

// T6 - the drawn waterline beside a floating body: a 1 m block held half submerged; the drawn surface on
// the columns next to it is within 3 cm of the pond's level away from it.
TEST(WaterSolidTest, DrawnWaterlineBesideAFloatingBody) {
    Pond p; p.fill();
    WaterSolver s(p.grid, p.query());
    for (int k = 0; k < 60; ++k) s.step(kDt);
    MovingSolid b; b.centre = {2.0f, 1.53f, 2.0f}; b.halfExtents = glm::vec3(0.5f); b.fresh = true;
    for (int k = 0; k < 300; ++k) { s.setMovingSolids({b}, kDt); b.fresh = false; s.step(kDt); }
    const float level = levelAway(p.grid, 3, 8);
    float worst = 0.0f;
    for (int z = 4; z <= 7; ++z) for (int x : {3, 8}) {   // the columns either side of the block
        const float y = p.grid.surfaceWorldY(x, z);
        if (!std::isnan(y)) worst = std::max(worst, std::abs(y - level));
    }
    std::printf("  floating block: level away %.4f m, worst beside it %.4f m off\n", level, worst);
    EXPECT_LT(worst, 0.03f);
}


// Diagnostic (M0): where water still overlaps a body, and where the resting stone's pond keeps moving.
TEST(WaterSolidTest, DiagSolidOverlap) {
    Pond p; p.fill();
    WaterSolver s(p.grid, p.query());
    for (int k = 0; k < 60; ++k) s.step(kDt);
    const double m0 = p.grid.totalMass();
    const float he = kH / 2.0f;
    MovingSolid b; b.centre = {6.5f * kH, he, 6.5f * kH}; b.halfExtents = glm::vec3(he); b.fresh = true;
    double dropped = 0.0;
    for (int k = 0; k < 600; ++k) {
        const WaterSolver::SolidsReport rep = s.setMovingSolids({b}, kDt); b.fresh = false;
        std::vector<float> before = p.grid.fData();
        const StepReport r = s.step(kDt);
        dropped += r.residueDropped;
        if (k % 100 == 99) {
            size_t im = 0; float dm = 0.0f;
            for (size_t i = 0; i < before.size(); ++i) { const float d = std::abs(p.grid.fData()[i] - before[i]); if (d > dm) { dm = d; im = i; } }
            const int cx = static_cast<int>(im % 12), cy = static_cast<int>((im / 12) % 9), cz = static_cast<int>(im / 108);
            std::printf("  largest change this tick: cell (%d,%d,%d) %.4f -> %.4f (s %.3f), v above %+.4f below %+.4f\n", cx, cy, cz, before[im], p.grid.f(cx, cy, cz), p.grid.s(cx, cy, cz),
                        p.grid.v(cx, cy + 1, cz), p.grid.v(cx, cy, cz));
        }
        if (k % 100 == 99) std::printf("  t %4.1f s: rateCells %ld rate %.6f m3/s clamped %ld, ke %.3e, maxDf %.2e, quiet %d, mass %+.3e, residue dropped %.3e\n",
            (k + 1) * kDt, rep.rateCells, rep.rate, rep.clamped, r.kineticEnergy, r.maxDeltaF, r.quietTicks, p.grid.totalMass() - m0, dropped);
    }
    for (int z = 0; z < 12; ++z) for (int y = 0; y < 9; ++y) for (int x = 0; x < 12; ++x) {
        const float f = p.grid.f(x, y, z), sv = p.grid.s(x, y, z);
        if (f + sv > 1.0001f || (sv > 0.0f)) std::printf("  cell (%d,%d,%d): f %.4f s %.4f q %.3e kind %d\n", x, y, z, f, sv, p.grid.q(x, y, z), static_cast<int>(p.grid.kind(x, y, z)));
    }
    for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) {
        const float y = p.grid.surfaceWorldY(x, z);
        if (std::abs(y - 1.5f) > 0.01f) {
            std::printf("  column (%d,%d) surface %.4f:", x, z, y);
            for (int yy = 0; yy < 9; ++yy) std::printf(" %.3f/%.2f", p.grid.f(x, yy, z), p.grid.s(x, yy, z));
            std::printf("\n");
        }
    }
}


// Diagnostic (M0, T10): after the stone lands, where the trapped air sits and which gate holds it.
TEST(WaterSolidTest, DiagCavity) {
    Pond p; p.fill();
    WaterSolver s(p.grid, p.query());
    for (int k = 0; k < 60; ++k) s.step(kDt);
    const float cx = 6.5f * kH, he = kH / 2.0f;
    MovingSolid b; b.centre = {cx, 1.5f + he + 0.02f, cx}; b.halfExtents = glm::vec3(he);
    for (int k = 0; k < 360; ++k) {
        b.centre.y = std::max(he, b.centre.y - 6.0f * kDt);
        s.setMovingSolids({b}, kDt);
        const StepReport r = s.step(kDt);
        if (k % 30 == 29) std::printf("  t %.1f s: air under water %.5f m^3, ke %.3e, maxDf %.2e\n", (k + 1) * kDt, airUnderWater(p.grid), r.kineticEnergy, r.maxDeltaF);
    }
    const float vfall = std::sqrt(2.0f * 9.81f * kH);
    for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) {
        bool waterAbove = false; bool printed = false;
        for (int y = 8; y >= 0; --y) {
            const float fs = p.grid.f(x, y, z) + p.grid.s(x, y, z);
            if (waterAbove && fs < 0.999f && !printed) {
                printed = true;
                std::printf("  column (%d,%d):", x, z);
                for (int yy = 0; yy < 9; ++yy) std::printf(" %.2f/%.2f", p.grid.f(x, yy, z), p.grid.s(x, yy, z));
                const float vF = p.grid.v(x, y + 1, z);
                std::printf("  | pocket y %d: v above %+.3f m/s (rising gate %s), |v|dt/h %.4f vs 0.1 cap %.4f\n", y, vF, vF > 0.0f ? "HOLDS" : "open",
                            std::abs(vF) * (kDt) / kH, 0.1f * vfall * kDt / kH);
            }
            if (p.grid.f(x, y, z) >= 0.5f) waterAbove = true;
        }
    }
}


// Diagnostic (M0, T5's control): does this pond sleep within 60 s with NO body - left alone, and after a
// small push? If not, a resting body cannot be expected to let it sleep either.
TEST(WaterSolidTest, DiagPondSleeps) {
    for (int variant = 0; variant < 3; ++variant) {
        Pond p; p.fill();
        WaterSolver s(p.grid, p.query());
        for (int k = 0; k < 60; ++k) s.step(kDt);
        if (variant == 1) s.addImpulse(glm::vec3(2.0f, 1.4f, 2.0f), 0.5f, 0.05f, glm::vec3(0.0f, 1.0f, 0.0f));
        if (variant == 2) s.addImpulse(glm::vec3(2.0f, 1.4f, 2.0f), 0.5f, 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
        int sleptAt = -1;
        for (int k = 0; k < 3600; ++k) { s.step(kDt); if (s.asleep() && sleptAt < 0) sleptAt = k; }
        std::printf("  variant %d (%s): asleep at %s %.1f s\n", variant, variant == 0 ? "left alone" : (variant == 1 ? "push 0.05 m/s" : "push 0.5 m/s"),
                    sleptAt >= 0 ? "" : "NEVER - last", sleptAt >= 0 ? (sleptAt + 1) * kDt : 60.0f);
    }
}

}  // namespace
}  // namespace Phyxel::Core::Water
