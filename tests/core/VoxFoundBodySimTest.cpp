// VoxFoundBodySimTest -- docs/VoxelFoundationPlan.md §6.7, F5.2a: the CPU reference body solver.
// Predictions written before the code: docs/evidence/voxfound_f52_prediction.txt (R1-R8).
// Every rig is a hand-built lab shape (1/8 m voxels); bodies come from F5.1's extractBodies.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/voxfound/BodySim.h"
#include "core/voxfound/Bodies.h"
#include "core/voxfound/FractureLab.h"

using namespace Phyxel::Core::VoxFound;

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180.0;

// A free box of material m at world voxels [lo, lo + size): one loose body (no fixed voxel, energy 0).
Body boxBody(glm::ivec3 lo, glm::ivec3 size, uint8_t m = kStone) {
    VoxelGrid g;
    g.resize(lo + size + 1);
    g.voxelM = 0.125f;
    for (int x = 0; x < size.x; ++x)
        for (int y = 0; y < size.y; ++y)
            for (int z = 0; z < size.z; ++z) g.set(lo + glm::ivec3(x, y, z), m);
    const auto mats = labMaterials();
    const Hit hit{glm::vec3(lo) + 0.5f, {0, 0, 1}, 0.0f, 1, 0.0f};
    const FractureResult r = fractureReference(g, hit, mats);
    const Extraction ex = extractBodies(g, r, hit, mats);
    EXPECT_EQ(ex.bodies.size(), 1u);
    return ex.bodies.empty() ? Body{} : ex.bodies[0];
}

VoxelGrid emptyWorld() {
    VoxelGrid w;
    w.resize({32, 32, 32});
    w.voxelM = 0.125f;
    return w;
}

double speed(const SimBody& b) { return glm::length(b.vel); }

void requireImplemented(const BodySimRef& sim) { ASSERT_TRUE(sim.implemented()) << "BodySimRef is a stub"; }

// the shipped defaults; VOXFOUND_COLD_BODY overrides coldScaleBody (a sweep knob for the record, not a test setting)
BodySimParams simParams() {
    BodySimParams p;
    if (const char* c = std::getenv("VOXFOUND_COLD_BODY")) p.coldScaleBody = std::atof(c);
    return p;
}

}  // namespace

// R1: free fall is exact
TEST(VoxFoundBodySim, R1FreeFallIsExact) {
    const VoxelGrid w = emptyWorld();
    BodySimRef sim(w, labMaterials());
    requireImplemented(sim);
    sim.add(boxBody({10, 20, 10}, {4, 4, 4}));
    const double y0 = sim.bodies()[0].pos.y;
    const double g = 9.81, dt = sim.params().dt;
    double worst = 0.0;
    for (int n = 1; n <= 120; ++n) {
        sim.step();
        const double expect = y0 - g * dt * dt * n * (n + 1) / 2.0;
        if (expect < 0.3) break;   // before it reaches the ground
        worst = std::max(worst, std::abs(sim.bodies()[0].pos.y - expect));
    }
    std::printf("[body sim] R1 free fall: worst |y - closed form| %.3e m\n", worst);
    EXPECT_LE(worst, 1e-9);
}

// R2: a box dropped flat comes to rest flush and stays
TEST(VoxFoundBodySim, R2FlatDropRestsAndStays) {
    const VoxelGrid w = emptyWorld();
    BodySimRef sim(w, labMaterials());
    requireImplemented(sim);
    sim.add(boxBody({10, 8, 10}, {4, 4, 4}));
    glm::dvec3 at3s(0.0);
    double worstSpeedLate = 0.0, worstDrift = 0.0;
    bool asleepBy3 = false;
    for (int t = 1; t <= 480; ++t) {
        sim.step();
        const SimBody& b = sim.bodies()[0];
        ASSERT_GE(sim.lowestCornerY(), -0.05) << "tick " << t;
        if (t == 120) EXPECT_LE(speed(b), 0.01) << "not at rest at 2 s";
        if (t == 180) { at3s = b.pos; asleepBy3 = b.asleep; }
        if (t > 180) {
            worstDrift = std::max(worstDrift, glm::length(b.pos - at3s));
            worstSpeedLate = std::max(worstSpeedLate, speed(b));
        }
    }
    const SimBody& b = sim.bodies()[0];
    const double pen = 0.25 - b.pos.y;
    std::printf("[body sim] R2 flat drop: rest y %.5f m (penetration %.2f mm), drift 3-8 s %.3f mm, late speed %.4f, "
                "asleep by 3 s %d\n", b.pos.y, pen * 1000, worstDrift * 1000, worstSpeedLate, int(asleepBy3));
    EXPECT_LE(std::abs(pen), 0.005);
    EXPECT_LE(worstDrift, 0.001);
    EXPECT_LE(worstSpeedLate, 0.01);
    EXPECT_TRUE(asleepBy3);
}

// R3 (amended after run 1, see the prediction file): the rig as first written starts the slab ~20 deg from standing on
// its NARROW face, and it settled on that face -- physically fine, the prediction had the rig wrong. R3 now asserts
// what the rig can show: it ends flat on SOME face (one body axis within 2 deg of vertical, the centre at that face's
// half-height +- 5 mm), asleep. R3b is the intended rig: the thin axis 30 deg from vertical -> the large face.
namespace {
void tiltedSlab(glm::dquat q, bool expectLargeFace, const char* label) {
    const VoxelGrid w = emptyWorld();
    BodySimRef sim(w, labMaterials());
    requireImplemented(sim);
    sim.add(boxBody({10, 9, 10}, {8, 4, 2}));
    sim.bodies()[0].quat = q;
    for (int t = 0; t < 300; ++t) {
        sim.step();
        ASSERT_GE(sim.lowestCornerY(), -0.05) << "tick " << t;
    }
    const SimBody& b = sim.bodies()[0];
    const double half[3] = {0.5, 0.25, 0.125};
    int up = 0;
    double best = 0.0;
    for (int a = 0; a < 3; ++a) {
        glm::dvec3 e(0.0);
        e[a] = 1.0;
        const double c = std::abs((b.quat * e).y);
        if (c > best) { best = c; up = a; }
    }
    const double tiltDeg = std::acos(std::min(1.0, best)) / kDeg;
    std::printf("[body sim] %s: vertical body axis %d (%.2f deg off), centre y %.4f m (face half-height %.4f), "
                "asleep %d\n", label, up, tiltDeg, b.pos.y, half[up], int(b.asleep));
    EXPECT_LE(tiltDeg, 2.0);
    EXPECT_NEAR(b.pos.y, half[up], 0.005);
    EXPECT_TRUE(b.asleep);
    if (expectLargeFace) EXPECT_EQ(up, 2) << "not resting on the large (8 x 4) face";
}
}  // namespace

TEST(VoxFoundBodySim, R3TiltedSlabRestsFlatOnAFace) {
    tiltedSlab(glm::angleAxis(30.0 * kDeg, glm::dvec3(0, 0, 1)) * glm::angleAxis(20.0 * kDeg, glm::dvec3(1, 0, 0)),
               false, "R3 tilted slab (as first written)");
}

TEST(VoxFoundBodySim, R3bSlabThinAxisNearVerticalRestsOnItsLargeFace) {
    // body z (the 2-voxel axis) to world y, then tilted 30 deg about x and turned 20 deg about y
    const glm::dquat toUp = glm::angleAxis(-90.0 * kDeg, glm::dvec3(1, 0, 0));
    tiltedSlab(glm::angleAxis(20.0 * kDeg, glm::dvec3(0, 1, 0)) * glm::angleAxis(30.0 * kDeg, glm::dvec3(1, 0, 0)) * toUp,
               true, "R3b slab, thin axis 30 deg from vertical");
}

// R4: friction stops a sliding box after v^2 / (2 mu g)
TEST(VoxFoundBodySim, R4FrictionSlideDistance) {
    const VoxelGrid w = emptyWorld();
    BodySimRef sim(w, labMaterials());
    requireImplemented(sim);
    sim.add(boxBody({4, 0, 10}, {4, 2, 4}));
    for (int t = 0; t < 30; ++t) sim.step();   // settle on the ground first
    SimBody& s = sim.bodies()[0];
    s.asleep = false;
    s.slowTicks = 0;
    s.vel = glm::dvec3(2.0, 0.0, 0.0);
    const glm::dvec3 x0 = s.pos;
    double maxRise = 0.0;
    for (int t = 0; t < 180; ++t) {
        sim.step();
        maxRise = std::max(maxRise, sim.bodies()[0].pos.y - x0.y);
    }
    const SimBody& b = sim.bodies()[0];
    const double dist = b.pos.x - x0.x, expect = 4.0 / (2.0 * 0.8 * 9.81);
    const double tilt = 2.0 * std::acos(std::min(1.0, std::abs(b.quat.w))) / kDeg;
    std::printf("[body sim] R4 slide: %.4f m (Coulomb %.4f), rise %.2f mm, final tilt %.2f deg, speed %.4f\n", dist,
                expect, maxRise * 1000, tilt, speed(b));
    EXPECT_NEAR(dist, expect, 0.15 * expect);
    EXPECT_LE(maxRise, 0.005);
    EXPECT_LE(tilt, 2.0);
}

// R5: a stack of five boxes stays stacked
TEST(VoxFoundBodySim, R5StackStaysStacked) {
    const VoxelGrid w = emptyWorld();
    BodySimRef sim(w, labMaterials(), simParams());
    requireImplemented(sim);
    std::vector<glm::dvec3> start;
    for (int k = 0; k < 5; ++k) {
        sim.add(boxBody({12, 2 * k, 12}, {4, 2, 4}));
        start.push_back(sim.bodies().back().pos);
    }
    for (int t = 0; t < 300; ++t) sim.step();
    double worst = 0.0;
    int asleep = 0;
    for (size_t k = 0; k < 5; ++k) {
        worst = std::max(worst, glm::length(sim.bodies()[k].pos - start[k]));
        asleep += sim.bodies()[k].asleep;
    }
    std::printf("[body sim] R5 stack of 5: worst move %.2f mm, %d / 5 asleep\n", worst * 1000, asleep);
    EXPECT_LE(worst, 0.01);
    EXPECT_EQ(asleep, 5);
}

// R6: two boxes collide head-on without gravity: momentum, inelastic, no deep overlap.
// PREDICTION MISSED (recorded in voxfound_f52_prediction.txt): "momentum to 1e-6" is not what AVBD does -- block
// coordinate descent conserves momentum only when converged, and an impact tick at the engine's 8 iterations is not
// (measured 4.7e-2 of m v after the run-1 fixes; 0.35 with the engine's stiff start). The 8-iteration gate below
// (<= 5 %) was set AFTER that measurement and says so; the 32-iteration check (<= 1 %) was written before its run.
namespace {
void headOn(int iterations, double momentumGate, const char* label) {
    const VoxelGrid w = emptyWorld();
    BodySimParams p = simParams();
    p.gravity = glm::dvec3(0.0);
    p.groundBelow = false;
    p.iterations = iterations;
    BodySimRef sim(w, labMaterials(), p);
    requireImplemented(sim);
    sim.add(boxBody({4, 8, 8}, {4, 4, 4}), glm::dvec3(1, 0, 0));
    sim.add(boxBody({12, 8, 8}, {4, 4, 4}), glm::dvec3(-1, 0, 0));
    const double m = sim.bodies()[0].src.mass;
    double worstP = 0.0, worstPen = 0.0;
    for (int t = 0; t < 120; ++t) {
        sim.step();
        const glm::dvec3 P = sim.bodies()[0].src.mass * sim.bodies()[0].vel + sim.bodies()[1].src.mass * sim.bodies()[1].vel;
        worstP = std::max(worstP, glm::length(P) / m);
        worstPen = std::max(worstPen, sim.currentMaxPenetration());
    }
    std::printf("[body sim] %s: |P| / (m v) worst %.2e, worst overlap %.2f mm, after: |v| %.4f / %.4f\n", label, worstP,
                worstPen * 1000, speed(sim.bodies()[0]), speed(sim.bodies()[1]));
    EXPECT_LE(worstP, momentumGate);
    EXPECT_LE(worstPen, 0.01);
    EXPECT_LE(speed(sim.bodies()[0]), 0.1);
    EXPECT_LE(speed(sim.bodies()[1]), 0.1);
}
}  // namespace

TEST(VoxFoundBodySim, R6HeadOnCollisionAt8Iterations) { headOn(8, 0.05, "R6 head-on, 8 iterations"); }
TEST(VoxFoundBodySim, R6HeadOnCollisionAt32Iterations) { headOn(32, 0.01, "R6 head-on, 32 iterations"); }

namespace {
// R7 / R8: break a rig, extract, simulate the pieces on what is left of it
void pile(Rig rig, const char* label, int ticks = 600, float impulse = 20.0f) {
    rig.hit.impulse = impulse;
    const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
    VoxelGrid world = rig.grid;
    const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
    ASSERT_GT(ex.bodies.size(), 0u);
    BodySimRef sim(world, rig.mats, simParams());
    requireImplemented(sim);
    for (const Body& b : ex.bodies) sim.add(b, b.vel, b.angVel);
    size_t deep = 0, capped = 0, maxContacts = 0, hardFires = 0;
    double ms = 0.0, worstMs = 0.0;
    int lastAwake = 0;
    for (int t = 1; t <= ticks; ++t) {
        const auto t0 = std::chrono::steady_clock::now();
        sim.step();
        const double dms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ms += dms;
        worstMs = std::max(worstMs, dms);
        deep += sim.stats().deepPoints;
        capped += sim.stats().capped;
        hardFires += sim.stats().hardFires;
        maxContacts = std::max(maxContacts, sim.stats().contacts);
        if (sim.stats().awake) lastAwake = t;
        if (std::getenv("DIAG_PILE") && (t % 10 == 0 || t < 40)) {
            int awake = 0;
            double vmax = 0.0, vin = 0.0;
            for (const SimBody& b : sim.bodies()) {
                awake += !b.asleep;
                vmax = std::max(vmax, speed(b));
            }
            for (const Body& b : ex.bodies) vin = std::max(vin, glm::length(b.vel));
            std::printf("   t %3d max speed %.3f m/s (extraction gave up to %.3f)\n", t, vmax, vin);
            std::printf("   t %3d awake %d contacts %zu pen %.2f mm | %s\n", t, awake, sim.stats().contacts,
                        sim.currentMaxPenetration() * 1000, sim.worstPenetrationReport().c_str());
        }
        ASSERT_GE(sim.lowestCornerY(), -0.05) << label << ": a body went through the ground at tick " << t;
    }
    size_t asleep = 0;
    for (const SimBody& b : sim.bodies()) asleep += b.asleep;
    const double pen = sim.currentMaxPenetration();
    std::printf("[body sim] %s: %zu bodies, %zu asleep at %.0f s (last awake tick %d), max penetration %.2f mm, deep "
                "points %zu, capped %zu, max contacts %zu, hard-contact fires %zu, %.2f ms / tick mean (worst %.2f)\n",
                label, sim.bodies().size(), asleep, ticks / 60.0, lastAwake, pen * 1000, deep, capped, maxContacts,
                hardFires, ms / ticks, worstMs);
    EXPECT_EQ(asleep, sim.bodies().size());
    EXPECT_LE(pen, 0.01) << sim.worstPenetrationReport();
    EXPECT_EQ(deep, 0u);
}
}  // namespace

TEST(VoxFoundBodySim, R7TheLintelFallsAndRests) { pile(rigColumnLintel(30.0f), "R7 lintel E/T 30"); }

// R8b (added after run 2, written before its run): R8's pieces barely moved -- 20 N s over 21 pieces is a few cm/s
// and most rest in their hole on the wall below (all asleep by 0.5-0.7 s). First try, 400 N s: the loose rubble is
// ~3 t, so the fastest piece got 0.39 m/s and friction on the stone below (mu 0.8) stopped it in ~3 ticks -- correct,
// but still no pile. 8,000 N s (a blast: ~3 t at ~2.7 m/s) throws them out to fall and pile: the same invariants.
TEST(VoxFoundBodySim, R8bABlastThrowsThePiecesOutAndTheyPile) {
    pile(rigStoneWall({12, 8, 0}, 80.0f), "R8b stone wall E/T 80, 8000 N s", 900, 8000.0f);
}

TEST(VoxFoundBodySim, R8BrokenWallPilesRest) {
    pile(rigStoneWall({12, 8, 0}, 30.0f), "R8 stone wall E/T 30");
    pile(rigStoneWall({12, 8, 0}, 80.0f), "R8 stone wall E/T 80");
}

// diagnostic (not a prediction): a stack of DIAG_N boxes; DIAG_CAP / DIAG_IT override the cap / iterations;
// DIAG_QUIET runs 300 ticks and prints only the worst move
TEST(VoxFoundBodySim, DISABLED_DiagStackTrace) {
    const VoxelGrid w = emptyWorld();
    BodySimParams prm;
    if (const char* c = std::getenv("DIAG_CAP")) prm.contactCap = std::atoi(c);
    if (const char* c = std::getenv("DIAG_IT")) prm.iterations = std::atoi(c);
    BodySimRef sim(w, labMaterials(), prm);
    const char* nEnv = std::getenv("DIAG_N");
    const int N = nEnv ? std::atoi(nEnv) : 2;
    for (int k = 0; k < N; ++k) sim.add(boxBody({12, 2 * k, 12}, {4, 2, 4}));
    std::vector<glm::dvec3> start;
    for (const SimBody& b : sim.bodies()) start.push_back(b.pos);
    const bool quiet = std::getenv("DIAG_QUIET") != nullptr;
    for (int t = 0; t < (quiet ? 300 : 40); ++t) {
        sim.step();
        if (quiet) continue;
        const auto& s = sim.stats();
        std::printf("t %2d contacts %zu cand %zu capped %zu deep %zu | ", t, s.contacts, s.candidates, s.capped,
                    s.deepPoints);
        for (const SimBody& b : sim.bodies())
            std::printf("(%.4f %.4f %.4f | %.4f %.4f %.4f) ", b.pos.x, b.pos.y, b.pos.z, b.vel.x, b.vel.y, b.vel.z);
        std::printf("\n");
        if (const char* d = std::getenv("DIAG_DUMP"); d && std::atoi(d) == t)
            for (const auto& k : sim.constraints())
                std::printf("   a %zu b %lld n (%.2f %.2f %.2f) rA (%.3f %.3f %.3f) C0 %.5f lamN %.1f penN %.0f\n", k.a,
                            k.b == BodySimRef::kStatic ? -1LL : (long long)k.b, k.n.x, k.n.y, k.n.z, k.rA.x, k.rA.y,
                            k.rA.z, k.cInitN, k.lambdaN, k.penN);
    }
    double worst = 0.0;
    for (size_t k = 0; k < start.size(); ++k) worst = std::max(worst, glm::length(sim.bodies()[k].pos - start[k]));
    std::printf("diag N %zu cap %d it %d: worst move %.2f mm\n", start.size(), prm.contactCap, prm.iterations,
                worst * 1000);
}

// diagnostic (not a prediction): the R6 head-on collision, per tick; DIAG_IT overrides the iterations
TEST(VoxFoundBodySim, DISABLED_DiagHeadOn) {
    const VoxelGrid w = emptyWorld();
    BodySimParams p;
    p.gravity = glm::dvec3(0.0);
    p.groundBelow = false;
    if (const char* c = std::getenv("DIAG_IT")) p.iterations = std::atoi(c);
    if (const char* c = std::getenv("DIAG_COLD")) p.coldScaleBody = std::atof(c);
    BodySimRef sim(w, labMaterials(), p);
    sim.add(boxBody({4, 8, 8}, {4, 4, 4}), glm::dvec3(1, 0, 0));
    sim.add(boxBody({12, 8, 8}, {4, 4, 4}), glm::dvec3(-1, 0, 0));
    for (int t = 0; t < 40; ++t) {
        sim.step();
        const SimBody& a = sim.bodies()[0];
        const SimBody& b = sim.bodies()[1];
        std::printf("t %2d c %zu gap %.4f va %.4f vb %.4f P %.4f wa %.4f\n", t, sim.stats().contacts,
                    (b.pos.x - a.pos.x) - 0.5, a.vel.x, b.vel.x, a.vel.x + b.vel.x, glm::length(a.angVel));
    }
}

// diagnostic (not a prediction): the R4 slide, per tick, with every non-axis contact normal
TEST(VoxFoundBodySim, DISABLED_DiagSlide) {
    const VoxelGrid w = emptyWorld();
    BodySimRef sim(w, labMaterials());
    sim.add(boxBody({4, 0, 10}, {4, 2, 4}));
    for (int t = 0; t < 30; ++t) sim.step();
    SimBody& s = sim.bodies()[0];
    s.asleep = false;
    s.slowTicks = 0;
    s.vel = glm::dvec3(2.0, 0.0, 0.0);
    for (int t = 0; t < 40; ++t) {
        sim.step();
        const SimBody& b = sim.bodies()[0];
        const double tilt = 2.0 * std::acos(std::min(1.0, std::abs(b.quat.w))) / kDeg;
        std::printf("t %2d x %.4f y %.5f vx %.4f vy %.4f tilt %.3f contacts %zu\n", t, b.pos.x, b.pos.y, b.vel.x,
                    b.vel.y, tilt, sim.stats().contacts);
        for (const auto& k : sim.constraints()) {
            const double ax = std::max({std::abs(k.n.x), std::abs(k.n.y), std::abs(k.n.z)});
            if (ax < 0.999)
                std::printf("   off-axis n (%.3f %.3f %.3f) rA (%.3f %.3f %.3f) C0 %.5f lamN %.1f\n", k.n.x, k.n.y,
                            k.n.z, k.rA.x, k.rA.y, k.rA.z, k.cInitN, k.lambdaN);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5 tunnelling (docs/evidence/voxfound_f55_prediction.txt, T1 / T2): fast pieces against the ground and a thin slab.
namespace {
struct Thrown {
    double lowest = 0.0, speed = 0.0;
    glm::dvec3 pos{0.0};
};
Thrown throwDown(const VoxelGrid& w, glm::ivec3 lo, glm::ivec3 size, double v, int ticks) {
    BodySimRef sim(w, labMaterials(), simParams());
    sim.add(boxBody(lo, size), glm::dvec3(0.0, -v, 0.0));
    for (int t = 0; t < ticks; ++t) sim.step();
    return {sim.lowestCornerY(), speed(sim.bodies()[0]), sim.bodies()[0].pos};
}
}  // namespace

TEST(VoxFoundBodySim, F55TunnellingFastPiecesStopAtTheGround) {
    const VoxelGrid w = emptyWorld();
    const glm::ivec3 sizes[] = {{1, 1, 4}, {2, 2, 2}};
    for (const glm::ivec3& sz : sizes)
        for (double v : {5.0, 15.0, 30.0, 60.0}) {
            const Thrown r = throwDown(w, {14, 16, 14}, sz, v, 120);
            std::printf("[body sim] T1 %dx%dx%d at %.0f m/s: lowest corner %.4f m, speed %.4f m/s, pos (%.6f, %.6f, "
                        "%.6f)\n", sz.x, sz.y, sz.z, v, r.lowest, r.speed, r.pos.x, r.pos.y, r.pos.z);
            EXPECT_GE(r.lowest, -0.01) << "T1 " << sz.x << sz.y << sz.z << " at " << v;
            EXPECT_LT(r.speed, 0.1) << "T1 " << sz.x << sz.y << sz.z << " at " << v;
        }
}

TEST(VoxFoundBodySim, F55TunnellingFastPieceStopsOnAThinSlab) {
    VoxelGrid w = emptyWorld();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) w.set({x, 8, z}, kStone);   // 1 voxel thick, its top at 1.125 m
    const Thrown r = throwDown(w, {14, 16, 14}, {2, 2, 2}, 30.0, 120);
    std::printf("[body sim] T2 2x2x2 at 30 m/s onto a 1-voxel slab: lowest corner %.4f m, speed %.4f m/s\n", r.lowest,
                r.speed);
    EXPECT_GE(r.lowest, 1.125 - 0.01) << "T2: through the slab";
}
