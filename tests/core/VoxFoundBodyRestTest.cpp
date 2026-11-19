// VoxFoundBodyRestTest -- docs/VoxelFoundationPlan.md §6.7 "Rest" + D6, F5.4: the rest / bake-in hybrid.
// Predictions written before the code: docs/evidence/voxfound_f54_prediction.txt (R1-R7).
// Every rig is a hand-built lab shape (FractureLab §8); bodies come from F5.1's extractBodies, motion from F5.2a.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/voxfound/BodyRest.h"
#include "core/voxfound/BodySim.h"
#include "core/voxfound/Bodies.h"
#include "core/voxfound/FractureLab.h"

using namespace Phyxel::Core::VoxFound;

namespace {

constexpr uint8_t kFloor = 6;   // a copy of Stone, fixed (the live lab's floor)

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// the rig on a 2-voxel fixed floor with `pad` voxels around it and `air` voxels above (as the live lab)
struct Scene {
    VoxelGrid world;
    std::vector<FractureMaterial> mats;
    Extraction ex;
};

Scene padded(Rig rig, int pad, int air, float impulse) {
    Scene s;
    const glm::ivec3 off(pad, 2, pad);
    VoxelGrid g;
    g.voxelM = rig.grid.voxelM;
    g.resize(rig.grid.dim + glm::ivec3(2 * pad, 2 + air, 2 * pad));
    g.damage.assign(g.size(), 0);
    for (int x = 0; x < g.dim.x; ++x)
        for (int z = 0; z < g.dim.z; ++z)
            for (int y = 0; y < 2; ++y) g.set({x, y, z}, kFloor, 0, true);
    for (size_t i = 0; i < rig.grid.size(); ++i) {
        if (!rig.grid.mat[i]) continue;
        const glm::ivec3 q = rig.grid.pos(i) + off;
        g.set(q, rig.grid.mat[i], rig.grid.part[i], rig.grid.fixed[i] != 0);
        if (!rig.grid.damage.empty()) g.damage[g.idx(q)] = rig.grid.damage[i];
    }
    s.mats = rig.mats;
    s.mats.resize(kFloor + 1, s.mats[kStone]);
    Hit hit = rig.hit;
    hit.point += glm::vec3(off);
    hit.impulse = impulse;
    const FractureResult r = fractureReference(g, hit, s.mats);
    s.world = g;
    s.ex = extractBodies(s.world, r, hit, s.mats);
    return s;
}

Rig largeWall(float e) {
    Rig rig;
    rig.name = "large_wall";
    rig.mats = labMaterials();
    rig.grid.voxelM = 0.125f;
    rig.grid.resize({256, 128, 4});
    for (int x = 0; x < 256; ++x)
        for (int y = 0; y < 128; ++y)
            for (int z = 0; z < 4; ++z) rig.grid.set({x, y, z}, kStone, 0, y == 0);
    rig.hit = {{128, 64, 0}, {0, 0, 1}, e * rig.mats[kStone].toughness, 777, 200.0f};
    return rig;
}

// the conservation ledger: (material, part, damage) -> count over world + bodies + crumbs
using Ledger = std::map<std::tuple<int, int, int>, size_t>;
void addGrid(Ledger& L, const VoxelGrid& g) {
    std::unordered_map<uint64_t, size_t> h;
    for (size_t i = 0; i < g.size(); ++i) {
        if (!g.mat[i]) continue;
        const uint64_t k = (uint64_t(g.mat[i]) << 32) | (uint64_t(g.part[i]) << 8) | (g.damage.empty() ? 0 : g.damage[i]);
        ++h[k];
    }
    for (const auto& [k, n] : h) L[{int(k >> 32), int((k >> 8) & 0xffff), int(k & 0xff)}] += n;
}
Ledger ledger(const VoxelGrid& world, const BodySimRef& sim, const std::vector<Crumb>& crumbs) {
    Ledger L;
    addGrid(L, world);
    for (const SimBody& b : sim.bodies()) addGrid(L, b.src.grid);
    for (const Crumb& c : crumbs) ++L[{c.mat, c.part, c.damage}];
    return L;
}
std::string diff(const Ledger& a, const Ledger& b) {
    std::string out;
    std::map<std::tuple<int, int, int>, long long> d;
    for (const auto& [k, n] : a) d[k] += (long long)n;
    for (const auto& [k, n] : b) d[k] -= (long long)n;
    for (const auto& [k, n] : d)
        if (n) out += "(mat " + std::to_string(std::get<0>(k)) + " part " + std::to_string(std::get<1>(k)) + " dmg " +
                      std::to_string(std::get<2>(k)) + ": " + std::to_string(n) + ") ";
    return out;
}
uint64_t gridHash(const VoxelGrid& g) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (size_t i = 0; i < g.size(); ++i)
        if (g.mat[i]) mix((uint64_t(i) << 24) ^ (uint64_t(g.mat[i]) << 16) ^ g.part[i] ^ (uint64_t(g.damage[i]) << 40));
    return h;
}

// a hand-made body: an L of `n` voxels (stone) at world voxel origin `o`
Body lBody(glm::ivec3 o, const std::vector<FractureMaterial>& mats) {
    Body b;
    b.grid.voxelM = 0.125f;
    b.grid.resize({4, 3, 2});
    b.grid.damage.assign(b.grid.size(), 0);
    uint16_t part = 1;
    for (int x = 0; x < 4; ++x)
        for (int z = 0; z < 2; ++z) b.grid.set({x, 0, z}, kStone, part);
    for (int y = 1; y < 3; ++y)
        for (int z = 0; z < 2; ++z) b.grid.set({0, y, z}, kStone, part);
    b.grid.damage[b.grid.idx({3, 0, 1})] = 7;
    b.origin = o;
    b.voxels = uint32_t(b.grid.solidCount());
    bodyMassBruteForce(b, mats, b.mass, b.com, b.inertia);
    return b;
}

struct BlastRun {
    std::vector<size_t> bakeOrder;          // src piece ids in bake order
    uint64_t worldHash = 0;
    RestStats total;
    size_t plansRigid = 0, plans = 0;
    double maxShift = 0.0, sumShift = 0.0;
    size_t shiftVoxels = 0;
    double maxWokenMove = 0.0;
    size_t left = 0, leftAwake = 0, leftRecent = 0, leftRefused = 0;
    size_t ticksChecked = 0;
    std::map<std::string, size_t> refusedWhy;   // diagnostics (run 1): why bakes were refused, first time per body
    size_t resampleNearAxis = 0;                // resample bakes whose rotation was within the snap (cells taken)
    std::vector<double> snapOfResample;
};

// R1a: the stone wall blast, everything bakes once asleep; the ledger checked after every tick
BlastRun runBlast(bool checkEveryTick) {
    BlastRun out;
    Rig rig = rigStoneWall({12, 8, 0}, 80.0f);
    Scene s = padded(rig, 32, 16, 8000.0f);
    EXPECT_GT(s.ex.bodies.size(), 10u);
    BodySimRef sim(s.world, s.mats, BodySimParams{});
    for (const Body& b : s.ex.bodies) sim.add(b, b.vel, b.angVel);
    const Ledger L0 = ledger(s.world, sim, s.ex.crumbs);
    RestParams rp;
    rp.budget = 0;
    rp.keepRadiusM = 0.0;
    rp.restTicks = 30;
    const glm::dvec3 player(-50.0, 2.0, -50.0);
    std::map<uint32_t, glm::dvec3> wokenAt;   // piece id -> pos when woken by a bake
    for (int t = 0; t < 900; ++t) {
        sim.step();
        for (SimBody& b : sim.bodies()) {
            auto it = wokenAt.find(b.src.pieceId);
            if (it == wokenAt.end()) continue;
            out.maxWokenMove = std::max(out.maxWokenMove, glm::length(b.pos - it->second));
            if (b.asleep) wokenAt.erase(it);
        }
        std::vector<uint8_t> asleepBefore;
        for (const SimBody& b : sim.bodies()) asleepBefore.push_back(b.asleep ? 1 : 0);
        std::vector<SimBody> before = sim.bodies();
        RestStats st;
        std::vector<BakePlan> plans;
        const std::vector<size_t> baked = restPass(sim, s.world, player, rp, &st, &plans);
        for (size_t i : baked) out.bakeOrder.push_back(before[i].src.pieceId);
        for (const BakePlan& p : plans) {
            if (!p.ok) {
                const std::string why = p.why.substr(p.why.find(" has ") == std::string::npos ? 0 : p.why.find(" has "));
                ++out.refusedWhy[why];
                continue;
            }
            if (!p.rigid) {
                out.snapOfResample.push_back(p.snapDeg);
                out.resampleNearAxis += p.snapDeg <= 10.0;
            }
            ++out.plans;
            out.plansRigid += p.rigid;
            out.maxShift = std::max(out.maxShift, p.maxShiftM);
            for (const BakeCell& c : p.cells) out.sumShift += c.shiftM;
            out.shiftVoxels += p.cells.size();
        }
        // bodies woken by this pass (asleep before, awake now): track their motion until they sleep again
        if (st.woken) {
            std::vector<uint8_t> gone(before.size(), 0);
            for (size_t i : baked) gone[i] = 1;
            size_t j = 0;
            for (size_t i = 0; i < before.size(); ++i) {
                if (gone[i]) continue;
                const SimBody& b = sim.bodies()[j++];
                if (asleepBefore[i] && !b.asleep) wokenAt[b.src.pieceId] = b.pos;
            }
        }
        out.total.baked += st.baked;
        out.total.rigid += st.rigid;
        out.total.refused += st.refused;
        out.total.woken += st.woken;
        out.total.overwritten += st.overwritten;
        out.total.voxels += st.voxels;
        if (checkEveryTick || t == 899) {
            const Ledger L = ledger(s.world, sim, s.ex.crumbs);
            EXPECT_EQ(L, L0) << "tick " << t << ": conservation broken: " << diff(L, L0);
            ++out.ticksChecked;
            if (L != L0) break;
        }
    }
    for (const SimBody& b : sim.bodies()) {
        ++out.left;
        if (!b.asleep) ++out.leftAwake;
        else if (b.asleepTicks < rp.restTicks) ++out.leftRecent;
        else ++out.leftRefused;
    }
    out.worldHash = gridHash(s.world);
    return out;
}

}  // namespace

// R1a + R4 + R5: the blast bakes, conservation exact after every tick, the pop's shift measured, deterministic
TEST(VoxFoundBodyRest, R1aBlastBakesWithExactConservation) {
    const auto t0 = std::chrono::steady_clock::now();
    const BlastRun a = runBlast(true);
    if (HasFailure()) return;
    std::printf("[body rest] R1a: %zu ticks checked, %zu bodies baked (%zu rigid), %zu voxels, %zu refused, %zu woken, "
                "%zu overwritten; left %zu (awake %zu, recent %zu, refused %zu); %.0f ms\n",
                a.ticksChecked, a.total.baked, a.total.rigid, a.total.voxels, a.total.refused, a.total.woken,
                a.total.overwritten, a.left, a.leftAwake, a.leftRecent, a.leftRefused, msSince(t0));
    EXPECT_EQ(a.total.overwritten, 0u);
    EXPECT_GT(a.total.baked, 10u) << "the run baked almost nothing (the test would prove nothing)";
    EXPECT_EQ(a.left, a.leftAwake + a.leftRecent + a.leftRefused);
    // R4: the pop's shift
    const double vm = 0.125;
    const double mean = a.shiftVoxels ? a.sumShift / double(a.shiftVoxels) : 0.0;
    const double rigidShare = a.plans ? double(a.plansRigid) / double(a.plans) : 0.0;
    std::printf("[body rest] R4: %zu bakes, %.0f %% rigid; shift mean %.4f m (%.2f vx), max %.4f m (%.2f vx); "
                "woken bodies moved at most %.4f m\n",
                a.plans, 100.0 * rigidShare, mean, mean / vm, a.maxShift, a.maxShift / vm, a.maxWokenMove);
    for (const auto& [why, n] : a.refusedWhy) std::printf("[body rest] R4 refused x%zu: %s\n", n, why.c_str());
    std::string snaps;
    for (double d : a.snapOfResample) snaps += std::to_string(int(std::lround(d))) + " ";
    std::printf("[body rest] R4 resample bakes: %zu within the 10-deg snap (rigid cells were taken); snap angles: %s\n",
                a.resampleNearAxis, snaps.c_str());
    // PREDICTION MISSED (run 1): >= 80 % rigid predicted, 33 % measured -- the pieces come to rest LEANING in the pile
    // (snap angles 14-56 deg), not on a face. Recorded in voxfound_f54_prediction.txt; printed, not asserted.
    EXPECT_GT(a.total.rigid, 0u);
    EXPECT_LE(mean, 0.5 * vm);
    EXPECT_LE(a.maxShift, 1.5 * vm);
    EXPECT_LE(a.maxWokenMove, 1.0 * vm);
    // R5: twice -> the same world and the same bake order
    const BlastRun b = runBlast(false);
    EXPECT_EQ(a.worldHash, b.worldHash);
    EXPECT_EQ(a.bakeOrder, b.bakeOrder);
    std::printf("[body rest] R5: world hash %016llx both runs, %zu bakes in the same order\n",
                (unsigned long long)a.worldHash, a.bakeOrder.size());
}

// R1b: the large wall (~950 bodies), budget 256, conservation after every tick; then a final pass bakes the rest
TEST(VoxFoundBodyRest, R1bLargeWallConservationAtEveryTick) {
    Scene s = padded(largeWall(2500.0f), 32, 16, 200.0f);
    ASSERT_GT(s.ex.bodies.size(), 500u);
    BodySimRef sim(s.world, s.mats, BodySimParams{});
    for (const Body& b : s.ex.bodies) sim.add(b, b.vel, b.angVel);
    const Ledger L0 = ledger(s.world, sim, s.ex.crumbs);
    RestParams rp;
    rp.budget = 256;
    rp.keepRadiusM = 4.0;
    rp.restTicks = 30;
    const glm::dvec3 player = glm::dvec3(s.world.dim) * 0.0625 + glm::dvec3(0.0, 0.0, -2.0);
    RestStats tot;
    double simMs = 0.0, restMs = 0.0, worst = 0.0;
    const int ticks = 200;
    for (int t = 0; t < ticks; ++t) {
        auto t0 = std::chrono::steady_clock::now();
        sim.step();
        simMs += msSince(t0);
        RestStats st;
        t0 = std::chrono::steady_clock::now();
        restPass(sim, s.world, player, rp, &st);
        const double ms = msSince(t0);
        restMs += ms;
        worst = std::max(worst, ms);
        tot.baked += st.baked;
        tot.rigid += st.rigid;
        tot.refused += st.refused;
        tot.overwritten += st.overwritten;
        const Ledger L = ledger(s.world, sim, s.ex.crumbs);
        ASSERT_EQ(L, L0) << "tick " << t << ": conservation broken: " << diff(L, L0);
    }
    // the final pass: everything asleep bakes
    RestParams all = rp;
    all.budget = 0;
    all.keepRadiusM = 0.0;
    all.restTicks = 0;
    RestStats fin;
    restPass(sim, s.world, player, all, &fin);
    ASSERT_EQ(ledger(s.world, sim, s.ex.crumbs), L0) << "the final pass broke conservation";
    EXPECT_EQ(tot.overwritten + fin.overwritten, 0u);
    // red run 1 passed this test against the stub (nothing baked conserves trivially): the bake must happen
    EXPECT_GT(tot.baked + fin.baked, s.ex.bodies.size() / 2) << "almost nothing baked (the test would prove nothing)";
    std::printf("[body rest] R1b: %zu bodies, %d ticks all conserved; baked %zu (%zu rigid, %zu refused) during, %zu "
                "(%zu rigid, %zu refused) in the final pass; %zu bodies left; sim %.1f ms/tick, rest pass %.3f ms/tick "
                "(worst %.2f)\n",
                s.ex.bodies.size(), ticks, tot.baked, tot.rigid, tot.refused, fin.baked, fin.rigid, fin.refused,
                sim.bodies().size(), simMs / ticks, restMs / ticks, worst);
}

// R2: the policy -- farthest eligible first, never near / awake / recent; reasons counted
TEST(VoxFoundBodyRest, R2PolicyIsFarthestEligibleFirst) {
    const auto mats = labMaterials();
    std::vector<SimBody> bodies;
    // 8 bodies along x at distances 2, 4, ..., 16 m from the player at the origin
    for (int i = 0; i < 8; ++i) {
        SimBody s;
        s.src = lBody({0, 0, 0}, mats);
        s.pos = glm::dvec3(2.0 * (i + 1), 0.0, 0.0);
        s.asleep = true;
        s.asleepTicks = 100;
        bodies.push_back(s);
    }
    bodies[7].asleep = false;      // 16 m: awake
    bodies[7].asleepTicks = 0;
    bodies[6].asleepTicks = 10;    // 14 m: asleep but recently moved
    RestParams p;
    p.budget = 5;
    p.keepRadiusM = 5.0;           // 2 m and 4 m are near
    p.restTicks = 60;
    RestStats st;
    std::vector<size_t> order = chooseBakes(bodies, glm::dvec3(0.0), p, &st);
    // over by 3; eligible = 6, 8, 10, 12 m (indices 2..5), farthest first
    EXPECT_EQ(order, (std::vector<size_t>{5, 4, 3, 2})) << "the eligible bodies, farthest first";
    EXPECT_EQ(st.over, 3u);
    EXPECT_EQ(st.keptAwake, 1u);
    EXPECT_EQ(st.keptRecent, 1u);
    EXPECT_EQ(st.keptNear, 2u);
    // at the budget: nothing
    p.budget = 8;
    RestStats st2;
    EXPECT_TRUE(chooseBakes(bodies, glm::dvec3(0.0), p, &st2).empty());
    EXPECT_EQ(st2.over, 0u);
    // nothing eligible: nothing, reasons counted
    p.budget = 0;
    p.keepRadiusM = 100.0;
    RestStats st3;
    EXPECT_TRUE(chooseBakes(bodies, glm::dvec3(0.0), p, &st3).empty());
    EXPECT_EQ(st3.keptNear + st3.keptAwake + st3.keptRecent, 8u);
    // ties by index: two bodies at the same distance
    bodies[3].pos = bodies[4].pos = glm::dvec3(0.0, 0.0, 9.0);
    p.keepRadiusM = 0.0;
    p.budget = 6;
    RestStats st4;
    EXPECT_EQ(chooseBakes(bodies, glm::dvec3(0.0), p, &st4), (std::vector<size_t>{5, 3, 4, 2, 1, 0}));
}

// R3: shape -- rigid at an axis rotation (exact), resample at 30 degrees (count exact, shift bounded)
TEST(VoxFoundBodyRest, R3RigidIsExactAndResampleIsBounded) {
    const auto mats = labMaterials();
    VoxelGrid world;
    world.voxelM = 0.125f;
    world.resize({32, 32, 32});
    world.damage.assign(world.size(), 0);
    const Body L = lBody({10, 10, 10}, mats);
    const double vm = 0.125;
    for (double deg : {90.0, 30.0}) {
        SCOPED_TRACE("turned " + std::to_string(deg) + " deg");
        BodySimRef sim(world, mats, BodySimParams{});
        sim.add(L);
        SimBody& s = sim.bodies()[0];
        s.quat = glm::angleAxis(glm::radians(deg), glm::dvec3(0, 1, 0));
        s.pos += glm::dvec3(0.3, 0.0, 0.7) * vm;
        const BakePlan plan = planBake(sim.bodies(), 0, world);
        ASSERT_TRUE(plan.ok) << plan.why;
        ASSERT_EQ(plan.cells.size(), size_t(L.voxels));
        // a bijection onto distinct cells
        std::vector<glm::ivec3> cells;
        for (const BakeCell& c : plan.cells) cells.push_back(c.cell);
        std::sort(cells.begin(), cells.end(), [](glm::ivec3 a, glm::ivec3 b) {
            return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
        });
        EXPECT_EQ(std::unique(cells.begin(), cells.end()) - cells.begin(), ptrdiff_t(cells.size()));
        // every shift is the true distance
        for (const BakeCell& c : plan.cells) {
            const glm::dvec3 tc = bodyVoxelCentre(s, L.grid.pos(c.src));
            EXPECT_NEAR(c.shiftM, glm::length((glm::dvec3(c.cell) + 0.5) * vm - tc), 1e-12);
        }
        if (deg == 90.0) {
            EXPECT_TRUE(plan.rigid);
            // rigid: cell(p) - cell(q) = R (p - q) exactly for every pair (R = 90 deg about y: (x, y, z) -> (z, y, -x))
            const glm::ivec3 p0 = L.grid.pos(plan.cells[0].src), c0 = plan.cells[0].cell;
            for (const BakeCell& c : plan.cells) {
                const glm::ivec3 d = L.grid.pos(c.src) - p0;
                EXPECT_EQ(c.cell - c0, glm::ivec3(d.z, d.y, -d.x)) << "voxel " << c.src;
            }
            EXPECT_LE(plan.maxShiftM, std::sqrt(3.0) / 2.0 * vm + 1e-12);
        } else {
            EXPECT_FALSE(plan.rigid);
            EXPECT_LE(plan.maxShiftM, 1.5 * vm);
            EXPECT_LE(plan.meanShiftM, 0.6 * vm);
        }
        // apply: materials, parts and damage land; nothing overwritten
        VoxelGrid w = world;
        EXPECT_EQ(applyBake(w, s, plan), 0u);
        EXPECT_EQ(w.count(kStone), size_t(L.voxels));
        size_t damaged = 0;
        for (size_t i = 0; i < w.size(); ++i) damaged += (w.damage[i] == 7) && w.part[i] == 1;
        EXPECT_EQ(damaged, 1u);
        std::printf("[body rest] R3 %.0f deg: %s, shift mean %.2f vx max %.2f vx, snap %.1f deg\n", deg,
                    plan.rigid ? "rigid" : "resample", plan.meanShiftM / vm, plan.maxShiftM / vm, plan.snapDeg);
    }
    // refused: out of the grid, or every cell around solid
    BodySimRef sim(world, mats, BodySimParams{});
    sim.add(L);
    sim.bodies()[0].pos.x = -5.0;
    EXPECT_FALSE(planBake(sim.bodies(), 0, world).ok);
}

// R6: remove keeps a survivor's trajectory bit-equal
TEST(VoxFoundBodyRest, R6RemoveIsClean) {
    const auto mats = labMaterials();
    VoxelGrid world;
    world.voxelM = 0.125f;
    world.resize({64, 32, 64});
    std::vector<glm::dvec3> ref, got;
    for (int pass = 0; pass < 2; ++pass) {
        BodySimRef sim(world, mats, BodySimParams{});
        sim.add(lBody({4, 0, 4}, mats));     // the sleeper, on the ground
        sim.add(lBody({50, 20, 50}, mats));  // the faller, far away
        for (int t = 0; t < 60; ++t) sim.step();
        ASSERT_TRUE(sim.bodies()[0].asleep) << "the sleeper did not sleep";
        if (pass == 1) {
            sim.remove({0});
            ASSERT_EQ(sim.bodies().size(), 1u);
        }
        auto& out = pass == 0 ? ref : got;
        for (int t = 0; t < 120; ++t) {
            sim.step();
            out.push_back(sim.bodies()[pass == 0 ? 1 : 0].pos);
        }
    }
    ASSERT_EQ(ref.size(), got.size());
    for (size_t i = 0; i < ref.size(); ++i) ASSERT_EQ(ref[i], got[i]) << "tick " << i;
}

// R7: cost of baking ~950 bodies (rigid at extraction poses; resample at random rotations); the idle policy pass
TEST(VoxFoundBodyRest, R7Cost) {
    Scene s = padded(largeWall(2500.0f), 32, 16, 200.0f);
    ASSERT_GT(s.ex.bodies.size(), 500u);
    for (int mode = 0; mode < 2; ++mode) {
        VoxelGrid w = s.world;
        BodySimRef sim(w, s.mats, BodySimParams{});
        for (const Body& b : s.ex.bodies) sim.add(b);
        std::mt19937_64 rng(5407);
        std::uniform_real_distribution<double> U(-1.0, 1.0);
        for (SimBody& b : sim.bodies()) {
            b.asleep = true;
            b.asleepTicks = 1000;
            if (mode == 1) {
                b.quat = glm::normalize(glm::dquat(U(rng), U(rng), U(rng), U(rng)));
                b.pos += glm::dvec3(0.0, 0.0, -1.5);  // in front of the wall's footprint (air), on the pad
            }
        }
        RestParams p;
        p.budget = 0;
        p.keepRadiusM = 0.0;
        p.restTicks = 0;
        RestStats st;
        std::vector<BakePlan> plans;
        const auto t0 = std::chrono::steady_clock::now();
        restPass(sim, w, glm::dvec3(0.0), p, &st, &plans);
        const double ms = msSince(t0);
        std::printf("[body rest] R7 %s: %zu bodies, %zu baked (%zu rigid, %zu refused), %zu voxels, %.2f ms\n",
                    mode == 0 ? "extraction poses" : "random rotations", s.ex.bodies.size(), st.baked, st.rigid,
                    st.refused, st.voxels, ms);
        EXPECT_GT(st.baked, s.ex.bodies.size() / 2);
        // PREDICTION MISSED (run 1): <= 10 / <= 40 ms predicted, 38 / 675 ms measured (the other-body cell test per
        // candidate). Recorded; these are regression guards only (lever owed to F5.5).
        EXPECT_LE(ms, mode == 0 ? 120.0 : 2000.0);
    }
    // the idle pass: 950 bodies, all awake, nothing eligible
    VoxelGrid w = s.world;
    BodySimRef sim(w, s.mats, BodySimParams{});
    for (const Body& b : s.ex.bodies) sim.add(b);
    RestParams p;
    p.budget = 256;
    double best = 1e30;
    for (int rep = 0; rep < 20; ++rep) {
        RestStats st;
        const auto t0 = std::chrono::steady_clock::now();
        restPass(sim, w, glm::dvec3(0.0), p, &st);
        best = std::min(best, msSince(t0));
        ASSERT_EQ(st.baked, 0u);
    }
    std::printf("[body rest] R7 idle pass: %.4f ms (best of 20)\n", best);
    EXPECT_LE(best, 0.05);
}
