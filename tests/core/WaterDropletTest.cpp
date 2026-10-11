#include <gtest/gtest.h>

#include "core/DamageSystem.h"
#include "core/water/MovingSolids.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreManager.h"
#include "core/water/WaterDroplets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// docs/WaterCore.md 21.6 / 21.11 - the droplet crown's unit tests (D-T1..D-T4, D-T7, D-T9). Predictions were
// written in 21.6 and 21.11 before S1 was built; each test quotes its own. D-T5 (GPU parity) needs a device and
// lives with the integration tests, not here.

namespace Phyxel::Core::Water {
namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kH = 1.0f / 3.0f;
constexpr float kG = 9.81f;

int floorDiv9(int v) { return v >= 0 ? v / 9 : -((-v + 8) / 9); }

// A stone box: every micro cell outside the inclusive world voxel box [lo, hi] is solid.
MicroStateQuery stoneBox(glm::ivec3 lo, glm::ivec3 hi) {
    return [lo, hi](const glm::ivec3& m) -> int {
        const glm::ivec3 v(floorDiv9(m.x), floorDiv9(m.y), floorDiv9(m.z));
        return (v.x < lo.x || v.y < lo.y || v.z < lo.z || v.x > hi.x || v.y > hi.y || v.z > hi.z) ? 1 : 0;
    };
}

// grid + pool: the quantity the droplets must conserve
double ledger(const WaterCoreManager& m) { return m.totalMass() + m.dropletVolume(); }

// ── D-T1 / D-T4 rig: a scrap thrown upward in a closed 4 m box ──────────────────────────────────────────
// One voxel at (1, 2, 1) filled to f = 0.2 (a 3 x 3 sheet of columns, each sum f 0.6 < 1, air below), kicked
// up at 3 m/s. Nothing supports it: by 21.11 rule 1 it leaves the grid on its first isolated tick.
struct ScrapRun {
    double m0 = 0.0, worstDrift = 0.0, bornAfterFirst = 0.0, born = 0.0, landed = 0.0, refused = 0.0, gridEnd = 0.0;
    float apexY = -1.0f, apexT = 0.0f, lastLandT = -1.0f;
    size_t maxAlive = 0;
};
ScrapRun launchScrap(size_t cap, float seconds) {
    WaterCoreManager mgr(stoneBox({0, 0, 0}, {3, 3, 3}));
    std::string err;
    const int id = mgr.create({0, 0, 0}, {3, 3, 3}, kH, "eulerian", &err, "cpu");
    EXPECT_NE(id, 0) << err;
    if (cap) mgr.setDropletCap(cap);
    mgr.placeBox({1, 2, 1}, {1, 2, 1}, 0.2f, nullptr);
    mgr.addImpulse(glm::vec3(1.5f, 2.5f, 1.5f), 0.6f, 3.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    ScrapRun r; r.m0 = ledger(mgr);
    AvRecord rec;
    const int ticks = static_cast<int>(seconds / kDt + 0.5f);
    bool flying = false;
    for (int k = 0; k < ticks; ++k) {
        mgr.step(id, 1, kDt, &rec);
        r.worstDrift = std::max(r.worstDrift, std::abs(ledger(mgr) - r.m0));
        const auto& st = mgr.dropletStats();
        if (k == 0) r.bornAfterFirst = st.bornM3;
        r.maxAlive = std::max(r.maxAlive, static_cast<size_t>(st.alive));
        for (const glm::vec4& d : mgr.dropletDrawList())
            if (d.y > r.apexY) { r.apexY = d.y; r.apexT = (k + 1) * kDt; }
        const bool now = mgr.dropletVolume() > 0.0;
        if (flying && !now) r.lastLandT = (k + 1) * kDt;
        flying = now;
    }
    const auto& st = mgr.dropletStats();
    r.born = st.bornM3; r.landed = st.landedM3; r.refused = st.refusedM3; r.gridEnd = mgr.totalMass();
    return r;
}

// D-T1 - a scrap launched upward in a closed box (21.6): born on its first isolated tick; all lands within the
// flight time + 0.1 s; grid + pool conserved <= 1e-6 m^3 EVERY tick. The flight time is the measured apex
// (highest droplet, its tick) plus the free fall from there to the floor, sqrt(2 y / g) - drag only lengthens it,
// by < 1 % at these speeds (dropletDrag at 3.7 cm, 4 m/s: 0.12 m/s^2).
// Control: the same box holding rested water - 0 births in 60 s.
TEST(WaterDropletTest, AScrapFliesAndLandsMassExact) {
    const ScrapRun r = launchScrap(0, 3.0f);
    const float predicted = r.apexT + std::sqrt(2.0f * r.apexY / kG);
    std::printf("  scrap %.6f m^3: born after tick 1 %.6f m^3; apex y %.3f m at %.3f s -> predicted landing %.3f s, last landing %.3f s;"
                " born %.6f landed %.6f, grid at end %.6f; worst ledger drift %.2e m^3\n",
                r.m0, r.bornAfterFirst, r.apexY, r.apexT, predicted, r.lastLandT, r.born, r.landed, r.gridEnd, r.worstDrift);
    EXPECT_GE(r.bornAfterFirst, 0.99 * r.m0) << "the scrap must leave the grid on its first tick";
    EXPECT_GT(r.apexY, 2.6f) << "the droplets rise (they carry the scrap's upward velocity)";
    ASSERT_GT(r.lastLandT, 0.0f) << "every droplet must land within the run";
    EXPECT_LE(r.lastLandT, predicted + 0.1f) << "landing later than the flight time";
    EXPECT_GE(r.lastLandT, predicted - 0.1f) << "landing earlier than the flight time (a droplet stopped in mid-air)";
    EXPECT_NEAR(r.landed, r.born, 1e-6);
    EXPECT_NEAR(r.gridEnd, r.m0, 1e-6) << "all of it back in the grid";
    EXPECT_LE(r.worstDrift, 1e-6) << "grid + pool must be conserved every tick";

    // control: rested water is never born
    WaterCoreManager mgr(stoneBox({0, 0, 0}, {3, 3, 3}));
    std::string err;
    const int id = mgr.create({0, 0, 0}, {3, 3, 3}, kH, "eulerian", &err, "cpu");
    ASSERT_NE(id, 0) << err;
    mgr.placeBox({0, 0, 0}, {3, 0, 3}, 1.0f, nullptr);
    AvRecord rec;
    for (int k = 0; k < 3600; ++k) mgr.step(id, 1, kDt, &rec);
    std::printf("  control: rested pond, 60 s: births %ld (%.2e m^3)\n", mgr.dropletStats().births, mgr.dropletStats().bornM3);
    EXPECT_EQ(mgr.dropletStats().births, 0) << "control: still water must never become droplets";
}

// D-T4 - the pool full (cap forced to 10): births refused, refused > 0, the ledger still exact (21.6).
TEST(WaterDropletTest, AFullPoolRefusesAndKeepsTheWater) {
    const ScrapRun r = launchScrap(10, 3.0f);
    std::printf("  cap 10: max alive %zu, born %.6f refused %.6f landed %.6f m^3, worst ledger drift %.2e m^3\n",
                r.maxAlive, r.born, r.refused, r.landed, r.worstDrift);
    EXPECT_LE(r.maxAlive, 10u) << "the cap is a hard bound";
    EXPECT_GT(r.refused, 0.0) << "a birth that does not fit must be refused (and reported)";
    EXPECT_LE(r.worstDrift, 1e-6) << "refused water goes straight back - never dropped";
}

// ── D-T2: the T4 stone entry (the CPU reference: WaterSolver + birthDroplets) ──────────────────────────────
// The 20.8 pond (4 x 3 x 4 m box, water 1.5 m deep at 1/3 m cells, WaterSolidTest's dropStone), a 1/3 m stone
// entering at `speed`. Born water is removed and counted (grid + born is the ledger). `inject`: a scripted scrap -
// ONE cell at f = 0.2, 0.8 m above the pond beside the entry, placed 20 ticks in - the control that the measure sees
// a floating scrap in this rig (the CPU stone throws none of its own at 6 m/s: 21.11, DiagJetParcels). A voxel-sized
// scrap is no use here: a 3 x 3 sheet of columns touching each other is never "isolated" (the draft rule measured).
struct EntryRun { double born = 0.0, worstDrift = 0.0; int scraps = 0, sprays = 0, isoSmallTicks = 0, isoSmall = 0; };
EntryRun stoneEntry(bool droplets, float speed, bool inject) {
    WaterGrid g(GridSpec{glm::ivec3(0), glm::ivec3(12, 9, 12), kH});
    const GridSpec sp = g.spec();
    SolidQuery q = [sp](const glm::ivec3& c) -> Occ {
        return (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= sp.dims.x || c.y >= sp.dims.y || c.z >= sp.dims.z) ? Occ::Solid : Occ::Air;
    };
    g.fillBox({0, 0, 0}, {11, 3, 11}, 1.0f);
    g.fillBox({0, 4, 0}, {11, 4, 11}, 0.5f);
    WaterSolver s(g, q);
    for (int k = 0; k < 60; ++k) s.step(kDt);   // let it rest
    EntryRun r;
    double m0 = g.totalMass();
    const float film = WaterGrid::kSurfaceMinDepth / kH, cx = 6.5f * kH, he = kH / 2.0f, rest = 1.5f;
    MovingSolid b; b.centre = {cx, rest + he + 0.02f, cx}; b.halfExtents = glm::vec3(he); b.velocity = {0.0f, -speed, 0.0f};
    for (int k = 0; k < 180; ++k) {
        if (inject && k == 20) { g.f(3, 7, 3) = 0.2f; s.wake(); m0 = g.totalMass() + r.born; }
        b.centre.y = std::max(he, b.centre.y - speed * kDt);
        if (b.centre.y <= he) b.velocity = glm::vec3(0.0f);
        s.setMovingSolids({b}, kDt);
        s.step(kDt);
        if (droplets) {
            std::vector<DropletBirth> out;
            const BirthReport br = birthDroplets(g, kDt, kG, out);
            r.scraps += br.scraps; r.sprays += br.sprays; r.born += br.volume;
        }
        r.worstDrift = std::max(r.worstDrift, std::abs(g.totalMass() + r.born - m0));
        int iso = 0;
        for (const DetachedRun& run : findDetachedRuns(sp.dims, g.fData().data(), g.sData().data(), film))
            if (run.isolated && run.sumF < 1.0f && run.bottom.y + run.cells < sp.dims.y) ++iso;   // a run reaching the lid is capped, not a scrap
        r.isoSmall += iso; r.isoSmallTicks += iso > 0 ? 1 : 0;
    }
    return r;
}

// D-T2 - the T4 stone entry (21.6): no isolated run with sum f < 1 exists after any tick's birth pass; grid + born
// exact. 21.6 also predicted ">= 1 parcel born during the cavity collapse" with the slab as the control; 21.11 then
// measured that the CPU stone (constant speed, no tumble) throws almost nothing - the slab is the live stone's. So
// births are asserted only at 7.7 m/s (where the spray rule fires), and the control is the scripted scrap.
TEST(WaterDropletTest, TheStoneEntryLeavesNoFloatingScraps) {
    for (float speed : {6.0f, 7.7f}) {
        const EntryRun on = stoneEntry(true, speed, false), off = stoneEntry(false, speed, false);
        const EntryRun onInj = stoneEntry(true, speed, true), offInj = stoneEntry(false, speed, true);
        std::printf("  entry %.1f m/s, births ON : %d scrap runs, %d spray cells, %.6f m^3; isolated scraps after the birth pass %d (in %d ticks);"
                    " worst drift %.2e m^3\n", speed, on.scraps, on.sprays, on.born, on.isoSmall, on.isoSmallTicks, on.worstDrift);
        std::printf("  entry %.1f m/s, births OFF: isolated scraps %d (in %d ticks)\n", speed, off.isoSmall, off.isoSmallTicks);
        std::printf("  + scripted scrap: ON isolated in %d ticks (%d scrap runs born, drift %.2e), OFF isolated in %d ticks\n",
                    onInj.isoSmallTicks, onInj.scraps, onInj.worstDrift, offInj.isoSmallTicks);
        if (speed > 7.0f) EXPECT_GE(on.scraps + on.sprays, 1) << "the 7.7 m/s entry must birth at least one parcel";
        EXPECT_EQ(on.isoSmallTicks, 0) << "a floating scrap survived the birth pass";
        EXPECT_EQ(onInj.isoSmallTicks, 0) << "the scripted scrap survived the birth pass";
        EXPECT_GE(onInj.scraps, 1);
        EXPECT_LE(on.worstDrift, 1e-6);
        EXPECT_LE(onInj.worstDrift, 1e-6);
        EXPECT_GT(offInj.isoSmallTicks, 0) << "control: without births the scripted scrap must be seen floating (else the measure is blind)";
    }
}

// D-T3 - the 19.7 pocket pond (the DiagPockets rig: 2 m of water, a 62 m/s blast 3 m off the east wall, 30 s) then a
// slosh at 3 m/s (21.6): 0 scraps born in the 5 s after the slosh - a pocket's roof rests beside supported water.
// The CPU reference directly (WaterSolver + birthDroplets): born water is removed and counted, grid + born exact.
TEST(WaterDropletTest, PocketsAndASloshAreNotScraps) {
    WaterGrid g(GridSpec{glm::ivec3(0), glm::ivec3(12, 9, 12), kH});
    const GridSpec sp = g.spec();
    SolidQuery q = [sp](const glm::ivec3& c) -> Occ {
        return (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= sp.dims.x || c.y >= sp.dims.y || c.z >= sp.dims.z) ? Occ::Solid : Occ::Air;
    };
    g.fillBox({0, 0, 0}, {11, 5, 11}, 1.0f);
    WaterSolver s(g, q);
    s.addRadialImpulse(glm::vec3(7.0f, 2.0f, 2.0f), 6.0f, DamageSystem::blastSpeed(62.0f), 0.3f);
    for (int k = 0; k < 1800; ++k) s.step(kDt);
    // the rig must hold pockets (19.7's measure: a part-full cell under a wet one), or a pocket's birth is unseeable
    int pockets = 0;
    for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) for (int y = 0; y + 1 < 9; ++y) {
        const float fb = g.f(x, y, z), fa = g.f(x, y + 1, z);
        if (fb < 0.999f && fb > 0.01f && fa > 0.01f) ++pockets;
    }
    s.addImpulse(glm::vec3(3.5f, 1.5f, 2.0f), 1.0f, 3.0f, glm::vec3(-1.0f, 0.0f, 0.0f));
    const double m0 = g.totalMass();
    double born = 0.0, worst = 0.0;
    int scraps = 0, sprays = 0;
    for (int k = 0; k < 300; ++k) {
        s.step(kDt);
        std::vector<DropletBirth> out;
        const BirthReport r = birthDroplets(g, kDt, kG, out);
        scraps += r.scraps; sprays += r.sprays; born += r.volume;
        worst = std::max(worst, std::abs(g.totalMass() + born - m0));
    }
    std::printf("  after the blast (30 s): %d pockets; slosh 3 m/s, 5 s: scrap runs born %d, spray cells %d (%.6f m^3); worst grid + born drift %.2e m^3\n",
                pockets, scraps, sprays, born, worst);
    EXPECT_GT(pockets, 0) << "control: the rig must hold pockets after the blast";
    EXPECT_EQ(scraps, 0) << "a pocket's roof (beside supported water) was born as a scrap";
    EXPECT_LE(worst, 1e-6);
}

// ── grid-level rigs (birthDroplets + DropletPool, no solver) ────────────────────────────────────────────
WaterGrid airGrid(glm::ivec3 origin) {
    WaterGrid g(GridSpec{origin, glm::ivec3(12, 9, 12), kH});
    for (int z = 0; z < 12; ++z) for (int y = 0; y < 9; ++y) for (int x = 0; x < 12; ++x) g.occ(x, y, z) = Occ::Air;
    return g;
}
// a two-cell scrap (sum f 0.55) at world cell `w`, moving (0.7, 2.5, -0.4) m/s
void putParcel(WaterGrid& g, glm::ivec3 w) {
    const glm::ivec3 l = w - g.spec().origin;
    g.f(l.x, l.y, l.z) = 0.3f; g.f(l.x, l.y + 1, l.z) = 0.25f;
    for (int y = l.y; y <= l.y + 1; ++y) {
        g.u(l.x, y, l.z) = g.u(l.x + 1, y, l.z) = 0.7f;
        g.w(l.x, y, l.z) = g.w(l.x, y, l.z + 1) = -0.4f;
    }
    for (int y = l.y; y <= l.y + 2; ++y) g.v(l.x, y, l.z) = 2.5f;
}
std::vector<Droplet> spawnAll(const std::vector<DropletBirth>& births) {
    DropletPool pool;
    for (const DropletBirth& b : births) pool.spawn(b, 1, kH, 1.0f / 9.0f, 7u);
    return pool.droplets();
}

// D-T7 - chunks are invisible (21.5): two volumes whose boxes differ (both straddle the chunk seam at world cell
// 288 = voxel 96) birth the same droplets from the same parcel - count, volume, positions and velocities equal;
// and a parcel at the seam column vs an interior column births the same count, volume and mean velocity.
TEST(WaterDropletTest, BirthsDoNotSeeTheVolumeOrTheChunk) {
    WaterGrid a = airGrid({280, 0, 0}), b = airGrid({285, 0, 3});
    const glm::ivec3 seam(288, 4, 6);
    putParcel(a, seam); putParcel(b, seam);
    std::vector<DropletBirth> ba, bb;
    birthDroplets(a, kDt, kG, ba); birthDroplets(b, kDt, kG, bb);
    ASSERT_EQ(ba.size(), 1u); ASSERT_EQ(bb.size(), 1u);
    EXPECT_EQ(ba[0].volume, bb[0].volume);
    EXPECT_EQ(ba[0].pos, bb[0].pos);
    EXPECT_EQ(ba[0].vel, bb[0].vel);
    const std::vector<Droplet> da = spawnAll(ba), db = spawnAll(bb);
    ASSERT_EQ(da.size(), db.size());
    int moved = 0;
    for (size_t i = 0; i < da.size(); ++i)
        if (glm::length(da[i].pos - db[i].pos) > 1e-5f || glm::length(da[i].vel - db[i].vel) > 1e-5f) ++moved;
    std::printf("  same parcel in two volumes: %zu droplets each, %d differ in position or velocity\n", da.size(), moved);
    EXPECT_EQ(moved, 0) << "the droplets of one parcel depend on which volume's box holds it (the spread hash must be world-cell, 21.5)";

    // seam column vs an interior column of the same volume
    WaterGrid c = airGrid({280, 0, 0});
    putParcel(c, glm::ivec3(284, 4, 6));
    std::vector<DropletBirth> bc;
    birthDroplets(c, kDt, kG, bc);
    ASSERT_EQ(bc.size(), 1u);
    EXPECT_EQ(bc[0].volume, ba[0].volume);
    EXPECT_EQ(bc[0].vel, ba[0].vel);
    EXPECT_NEAR(ba[0].pos.x - bc[0].pos.x, 4.0f * kH, 1e-5f);
    const std::vector<Droplet> dc = spawnAll(bc);
    ASSERT_EQ(dc.size(), da.size());
    glm::dvec3 ma(0.0), mc(0.0); double va = 0.0, vc = 0.0;
    for (const Droplet& d : da) { ma += glm::dvec3(d.vel) * static_cast<double>(d.volume); va += d.volume; }
    for (const Droplet& d : dc) { mc += glm::dvec3(d.vel) * static_cast<double>(d.volume); vc += d.volume; }
    EXPECT_NEAR(va, vc, 1e-9);
    EXPECT_LT(glm::length(ma / va - mc / vc), 1e-5) << "mean droplet velocity = the parcel's, wherever it is";
}

// D-T9 - spray off a fast-rising surface (21.11 rule 2): the top cell of a supported run whose top face carries
// water up at 4 m/s (above v_c = sqrt(2 g h) = 2.56 m/s) emits f (v - v_c) dt / h of a cell; at 2 m/s it emits none.
TEST(WaterDropletTest, ARisingSurfaceSpraysOnlyAboveTheGridLimit) {
    const float vc = std::sqrt(2.0f * kG * kH);
    for (float vUp : {4.0f, 2.0f}) {
        WaterGrid g = airGrid({0, 0, 0});
        g.fillBox({0, 0, 0}, {11, 3, 11}, 1.0f);
        g.fillBox({0, 4, 0}, {11, 4, 11}, 0.5f);
        g.v(6, 5, 6) = vUp;
        const double m0 = g.totalMass();
        std::vector<DropletBirth> out;
        const BirthReport r = birthDroplets(g, kDt, kG, out);
        const double expected = vUp > vc ? 0.5 * (vUp - vc) * kDt / kH * g.cellVolume() : 0.0;
        std::printf("  top face %.1f m/s (v_c %.2f): sprays %d, scraps %d, volume %.3e m^3 (expected %.3e), grid lost %.3e m^3\n",
                    vUp, vc, r.sprays, r.scraps, r.volume, expected, m0 - g.totalMass());
        EXPECT_EQ(r.scraps, 0);
        EXPECT_EQ(r.sprays, vUp > vc ? 1 : 0);
        EXPECT_NEAR(r.volume, expected, 1e-9);
        EXPECT_NEAR(m0 - g.totalMass(), r.volume, 1e-9) << "what the grid lost is what was born";
        if (vUp > vc) { ASSERT_EQ(out.size(), 1u); EXPECT_FLOAT_EQ(out[0].vel.y, vUp); }
    }
}

}  // namespace
}  // namespace Phyxel::Core::Water
