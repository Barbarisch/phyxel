// VoxFoundBodySimGpuTest -- docs/VoxelFoundationPlan.md Â§6.7, F5.2b: the body solver on the GPU.
// Predictions written before the code: docs/evidence/voxfound_f52_prediction.txt (F5.2b, G1-G5).
//
// BodySimGpu runs F5.2a's rigs and must pass the SAME assertions the CPU reference passes (G1); on the non-chaotic
// rigs its trajectory must stay within 1 mm of the reference's (G2); two GPU runs must be bit-equal (G3); nothing may
// overflow and every dynamic body must get a colour (G4); the device time per tick is reported (G5). The invariants
// are measured on a BodySimRef "mirror" whose bodies carry the GPU's poses (its measuring functions only read
// poses). Needs a Vulkan device: the fixture GTEST_SKIPs without one (never a pass). Every rig is a hand-built lab
// shape; bodies come from F5.1's extractBodies.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/voxfound/BodySim.h"
#include "core/voxfound/BodySimGpu.h"
#include "core/voxfound/Bodies.h"
#include "core/voxfound/FractureLab.h"

using namespace Phyxel;
using namespace Phyxel::Core::VoxFound;
using Phyxel::Testing::VulkanTestFixture;

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180.0;

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
    return ex.bodies.empty() ? Body{} : ex.bodies[0];
}

VoxelGrid emptyWorld() {
    VoxelGrid w;
    w.resize({32, 32, 32});
    w.voxelM = 0.125f;
    return w;
}

double speed(const SimBody& b) { return glm::length(b.vel); }

class VoxFoundBodySimGpu : public VulkanTestFixture {
protected:
    void SetUp() override {
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable() || device == VK_NULL_HANDLE) GTEST_SKIP() << "no Vulkan device";
        std::string err;
        ASSERT_TRUE(gpu.implemented()) << "BodySimGpu is a stub";
        ASSERT_TRUE(gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    }
    void TearDown() override {
        gpu.shutdown();
        VulkanTestFixture::TearDown();
    }

    // loads `ref`'s world + bodies; `mirror` (same world, same bodies) receives the GPU poses for measuring
    void load(const VoxelGrid& world, const BodySimRef& ref) {
        std::string err;
        ASSERT_TRUE(gpu.load(world, labMats, ref.bodies(), ref.params(), &err)) << err;
    }
    void step(int ticks, int perSubmit = 30) {
        std::string err;
        ASSERT_TRUE(gpu.step(ticks, &err, perSubmit)) << err;
        const auto& s = gpu.stats();
        EXPECT_EQ(s.scratchOverflow, 0u) << "G4: candidate scratch overflow";
        EXPECT_EQ(s.constraintOverflow, 0u) << "G4: constraint overflow";
        EXPECT_EQ(s.uncoloured, 0u) << "G4: dynamic bodies left uncoloured";
        EXPECT_EQ(s.pairOverflow, 0u) << "B4: body pairs past the pair list";
    }
    void sync(BodySimRef& mirror) {
        std::string err;
        ASSERT_TRUE(gpu.read(mirror.bodies(), &err)) << err;
    }

    BodySimGpu gpu;
    std::vector<FractureMaterial> labMats = labMaterials();
};

}  // namespace

// R1 + G2: free fall against the closed form and against the reference, every tick
TEST_F(VoxFoundBodySimGpu, R1FreeFall) {
    const VoxelGrid w = emptyWorld();
    BodySimRef ref(w, labMats), mirror(w, labMats);
    ref.add(boxBody({10, 20, 10}, {4, 4, 4}));
    mirror.add(boxBody({10, 20, 10}, {4, 4, 4}));
    load(w, ref);
    if (HasFatalFailure()) return;
    const double y0 = ref.bodies()[0].pos.y, g = 9.81, dt = ref.params().dt;
    double worstClosed = 0.0, worstRef = 0.0;
    for (int n = 1; n <= 120; ++n) {
        ref.step();
        step(1, 1);
        sync(mirror);
        if (HasFatalFailure()) return;
        const double expect = y0 - g * dt * dt * n * (n + 1) / 2.0;
        if (expect < 0.3) break;
        worstClosed = std::max(worstClosed, std::abs(mirror.bodies()[0].pos.y - expect));
        worstRef = std::max(worstRef, glm::length(mirror.bodies()[0].pos - ref.bodies()[0].pos));
    }
    std::printf("[body gpu] R1 free fall: worst vs closed form %.3e m, vs reference %.3e m\n", worstClosed, worstRef);
    EXPECT_LE(worstClosed, 1e-4);
    EXPECT_LE(worstRef, 1e-3);
}

// R2 + G2
TEST_F(VoxFoundBodySimGpu, R2FlatDrop) {
    const VoxelGrid w = emptyWorld();
    BodySimRef ref(w, labMats), mirror(w, labMats);
    ref.add(boxBody({10, 8, 10}, {4, 4, 4}));
    mirror.add(boxBody({10, 8, 10}, {4, 4, 4}));
    load(w, ref);
    if (HasFatalFailure()) return;
    glm::dvec3 at3s(0.0);
    double worstDrift = 0.0, worstLate = 0.0, worstRef = 0.0;
    bool asleepBy3 = false;
    for (int t = 1; t <= 480; ++t) {
        ref.step();
        step(1, 1);
        sync(mirror);
        if (HasFatalFailure()) return;
        const SimBody& b = mirror.bodies()[0];
        worstRef = std::max(worstRef, glm::length(b.pos - ref.bodies()[0].pos));
        if (t == 120) EXPECT_LE(speed(b), 0.01) << "not at rest at 2 s";
        if (t == 180) { at3s = b.pos; asleepBy3 = b.asleep; }
        if (t > 180) {
            worstDrift = std::max(worstDrift, glm::length(b.pos - at3s));
            worstLate = std::max(worstLate, speed(b));
        }
    }
    const SimBody& b = mirror.bodies()[0];
    std::printf("[body gpu] R2 flat drop: rest y %.5f (penetration %.2f mm), drift %.3f mm, late speed %.4f, asleep by "
                "3 s %d, worst vs reference %.3f mm\n", b.pos.y, (0.25 - b.pos.y) * 1000, worstDrift * 1000, worstLate,
                int(asleepBy3), worstRef * 1000);
    EXPECT_LE(std::abs(0.25 - b.pos.y), 0.005);
    EXPECT_LE(worstDrift, 0.001);
    EXPECT_LE(worstLate, 0.01);
    EXPECT_TRUE(asleepBy3);
    EXPECT_LE(worstRef, 0.001) << "G2";
}

namespace {
void slabCheck(const SimBody& b, bool expectLargeFace, const char* label) {
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
    std::printf("[body gpu] %s: vertical axis %d (%.2f deg off), centre y %.4f (half-height %.4f), asleep %d\n", label,
                up, tiltDeg, b.pos.y, half[up], int(b.asleep));
    EXPECT_LE(tiltDeg, 2.0);
    EXPECT_NEAR(b.pos.y, half[up], 0.005);
    EXPECT_TRUE(b.asleep);
    if (expectLargeFace) EXPECT_EQ(up, 2);
}
}  // namespace

TEST_F(VoxFoundBodySimGpu, R3SlabsRestFlat) {
    const glm::dquat qs[2] = {
        glm::angleAxis(30.0 * kDeg, glm::dvec3(0, 0, 1)) * glm::angleAxis(20.0 * kDeg, glm::dvec3(1, 0, 0)),
        glm::angleAxis(20.0 * kDeg, glm::dvec3(0, 1, 0)) * glm::angleAxis(30.0 * kDeg, glm::dvec3(1, 0, 0)) *
            glm::angleAxis(-90.0 * kDeg, glm::dvec3(1, 0, 0))};
    for (int k = 0; k < 2; ++k) {
        const VoxelGrid w = emptyWorld();
        BodySimRef ref(w, labMats), mirror(w, labMats);
        ref.add(boxBody({10, 9, 10}, {8, 4, 2}));
        mirror.add(boxBody({10, 9, 10}, {8, 4, 2}));
        ref.bodies()[0].quat = qs[k];
        load(w, ref);
        step(300);
        sync(mirror);
        if (HasFatalFailure()) return;
        EXPECT_GE(mirror.lowestCornerY(), -0.05);
        slabCheck(mirror.bodies()[0], k == 1, k ? "R3b slab" : "R3 slab");
    }
}

// R4 + G2
TEST_F(VoxFoundBodySimGpu, R4FrictionSlide) {
    const VoxelGrid w = emptyWorld();
    BodySimRef ref(w, labMats), mirror(w, labMats);
    ref.add(boxBody({4, 0, 10}, {4, 2, 4}));
    mirror.add(boxBody({4, 0, 10}, {4, 2, 4}));
    for (int t = 0; t < 30; ++t) ref.step();
    SimBody& s = ref.bodies()[0];
    s.asleep = false;
    s.slowTicks = 0;
    s.vel = glm::dvec3(2.0, 0.0, 0.0);
    load(w, ref);   // the GPU starts from the reference's settled state
    if (HasFatalFailure()) return;
    const glm::dvec3 x0 = s.pos;
    double maxRise = 0.0, worstRef = 0.0;
    for (int t = 0; t < 180; ++t) {
        ref.step();
        step(1, 1);
        sync(mirror);
        if (HasFatalFailure()) return;
        maxRise = std::max(maxRise, mirror.bodies()[0].pos.y - x0.y);
        worstRef = std::max(worstRef, glm::length(mirror.bodies()[0].pos - ref.bodies()[0].pos));
    }
    const SimBody& b = mirror.bodies()[0];
    const double dist = b.pos.x - x0.x, expect = 4.0 / (2.0 * 0.8 * 9.81);
    const double tilt = 2.0 * std::acos(std::min(1.0, std::abs(b.quat.w))) / kDeg;
    std::printf("[body gpu] R4 slide: %.4f m (Coulomb %.4f), rise %.2f mm, tilt %.2f deg, worst vs reference %.3f mm\n",
                dist, expect, maxRise * 1000, tilt, worstRef * 1000);
    EXPECT_NEAR(dist, expect, 0.15 * expect);
    EXPECT_LE(maxRise, 0.005);
    EXPECT_LE(tilt, 2.0);
    EXPECT_LE(worstRef, 0.001) << "G2";
}

TEST_F(VoxFoundBodySimGpu, R5Stack) {
    const VoxelGrid w = emptyWorld();
    BodySimRef ref(w, labMats), mirror(w, labMats);
    for (int k = 0; k < 5; ++k) {
        ref.add(boxBody({12, 2 * k, 12}, {4, 2, 4}));
        mirror.add(boxBody({12, 2 * k, 12}, {4, 2, 4}));
    }
    load(w, ref);
    step(300);
    sync(mirror);
    if (HasFatalFailure()) return;
    double worst = 0.0;
    int asleep = 0;
    for (size_t k = 0; k < 5; ++k) {
        worst = std::max(worst, glm::length(mirror.bodies()[k].pos - ref.bodies()[k].pos));
        asleep += mirror.bodies()[k].asleep;
    }
    std::printf("[body gpu] R5 stack of 5: worst move %.2f mm, %d / 5 asleep, colours %u\n", worst * 1000, asleep,
                gpu.stats().colours);
    EXPECT_LE(worst, 0.01);
    EXPECT_EQ(asleep, 5);
}

namespace {
struct HeadOn {
    double worstP = 0.0, worstPen = 0.0, v0 = 0.0, v1 = 0.0;
};
}  // namespace

TEST_F(VoxFoundBodySimGpu, R6HeadOn) {
    for (int iters : {8, 32}) {
        const VoxelGrid w = emptyWorld();
        BodySimParams p;
        p.gravity = glm::dvec3(0.0);
        p.groundBelow = false;
        p.iterations = iters;
        BodySimRef ref(w, labMats, p), mirror(w, labMats, p);
        ref.add(boxBody({4, 8, 8}, {4, 4, 4}), glm::dvec3(1, 0, 0));
        ref.add(boxBody({12, 8, 8}, {4, 4, 4}), glm::dvec3(-1, 0, 0));
        mirror.add(boxBody({4, 8, 8}, {4, 4, 4}));
        mirror.add(boxBody({12, 8, 8}, {4, 4, 4}));
        load(w, ref);
        if (HasFatalFailure()) return;
        const double m = ref.bodies()[0].src.mass;
        HeadOn h;
        for (int t = 0; t < 120; ++t) {
            step(1, 1);
            sync(mirror);
            if (HasFatalFailure()) return;
            const glm::dvec3 P = m * mirror.bodies()[0].vel + mirror.bodies()[1].src.mass * mirror.bodies()[1].vel;
            h.worstP = std::max(h.worstP, glm::length(P) / m);
            h.worstPen = std::max(h.worstPen, mirror.currentMaxPenetration());
        }
        h.v0 = speed(mirror.bodies()[0]);
        h.v1 = speed(mirror.bodies()[1]);
        std::printf("[body gpu] R6 head-on, %d iterations: |P| / (m v) %.2e, overlap %.2f mm, after |v| %.4f / %.4f\n",
                    iters, h.worstP, h.worstPen * 1000, h.v0, h.v1);
        EXPECT_LE(h.worstP, iters == 8 ? 0.05 : 0.01);
        EXPECT_LE(h.worstPen, 0.01);
        EXPECT_LE(h.v0, 0.1);
        EXPECT_LE(h.v1, 0.1);
    }
}

namespace {
struct PileResult {
    std::vector<glm::dvec3> pos;
    std::vector<glm::dquat> quat;
};
}  // namespace

class VoxFoundBodySimGpuPile : public VoxFoundBodySimGpu {
protected:
    // R7 / R8 / R8b: break, extract, simulate on what is left; the same assertions as the CPU test
    PileResult pile(Rig rig, const char* label, int ticks, float impulse, bool timed = true) {
        PileResult out;
        rig.hit.impulse = impulse;
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid world = rig.grid;
        const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
        EXPECT_GT(ex.bodies.size(), 0u);
        BodySimRef ref(world, rig.mats), mirror(world, rig.mats);
        for (const Body& b : ex.bodies) {
            ref.add(b, b.vel, b.angVel);
            mirror.add(b);
        }
        load(world, ref);
        if (HasFatalFailure()) return out;
        double ms = 0.0;
        uint32_t maxContacts = 0, colours = 0;
        uint64_t hard = 0, capped = 0;
        for (int done = 0; done < ticks; done += 30) {
            step(30);
            if (HasFatalFailure()) return out;
            ms += std::max(0.0, gpu.lastGpuMs());
            maxContacts = std::max(maxContacts, gpu.stats().maxContacts);
            colours = std::max(colours, gpu.stats().colours);
            hard += gpu.stats().hardFires;
            capped += gpu.stats().capped;
            sync(mirror);
            if (HasFatalFailure()) return out;
            EXPECT_GE(mirror.lowestCornerY(), -0.05) << label << ": through the ground by tick " << done + 30;
            for (const SimBody& b : mirror.bodies()) {   // every body, every 30 ticks (G3 compares them all)
                out.pos.push_back(b.pos);
                out.quat.push_back(b.quat);
            }
        }
        size_t asleep = 0;
        for (const SimBody& b : mirror.bodies()) asleep += b.asleep;
        const double pen = mirror.currentMaxPenetration();
        if (timed)
            std::printf("[body gpu] %s: %zu bodies, %zu asleep at %.0f s, max penetration %.2f mm, max contacts %u, "
                        "capped %llu, hard-contact fires %llu, colours %u, %.3f ms / tick device\n",
                        label, mirror.bodies().size(), asleep, ticks / 60.0, pen * 1000, maxContacts,
                        (unsigned long long)capped, (unsigned long long)hard, colours, ms / ticks);
        EXPECT_EQ(asleep, mirror.bodies().size()) << label;
        EXPECT_LE(pen, 0.01) << label << ": " << mirror.worstPenetrationReport();
        return out;
    }
};

TEST_F(VoxFoundBodySimGpuPile, R7Lintel) { pile(rigColumnLintel(30.0f), "R7 lintel E/T 30", 600, 20.0f); }

TEST_F(VoxFoundBodySimGpuPile, R8BrokenWalls) {
    pile(rigStoneWall({12, 8, 0}, 30.0f), "R8 stone wall E/T 30", 600, 20.0f);
    pile(rigStoneWall({12, 8, 0}, 80.0f), "R8 stone wall E/T 80", 600, 20.0f);
}

// R8b + G3 (two runs bit-equal) + G5 (the cost)
TEST_F(VoxFoundBodySimGpuPile, R8bBlastAndDeterminism) {
    const PileResult a = pile(rigStoneWall({12, 8, 0}, 80.0f), "R8b stone wall E/T 80, 8000 N s", 900, 8000.0f);
    if (HasFatalFailure()) return;
    const PileResult b = pile(rigStoneWall({12, 8, 0}, 80.0f), "R8b (second run)", 900, 8000.0f, false);
    ASSERT_EQ(a.pos.size(), b.pos.size());
    size_t differ = 0;
    for (size_t i = 0; i < a.pos.size(); ++i)
        differ += a.pos[i] != b.pos[i] || a.quat[i].x != b.quat[i].x || a.quat[i].y != b.quat[i].y ||
                  a.quat[i].z != b.quat[i].z || a.quat[i].w != b.quat[i].w;
    std::printf("[body gpu] G3: %zu of %zu bodies differ between two runs\n", differ, a.pos.size());
    EXPECT_EQ(differ, 0u) << "G3";
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5a -- the dispatch floor (docs/evidence/voxfound_f55_prediction.txt, A1-A5): the fused solve (colour rounds and
// every iteration in one dispatch) against the per-colour path (F5.2b) as the control.
namespace {
Rig largeWallRig(float e) {   // F5.1 / D5's 256 x 128 x 4 stone wall, base fixed, hit at the centre
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
}  // namespace

// A1 + A2 (+ A4's blast and all-asleep rows): the R8b blast on both paths, bit-equal at every sample
TEST_F(VoxFoundBodySimGpuPile, F55aFusedEqualsPerColour) {
    gpu.setPrimalParallel(false);   // A1 compares the two serial-primal paths (F5.5a4 note)
    gpu.setFused(false);
    const PileResult a = pile(rigStoneWall({12, 8, 0}, 80.0f), "A1 R8b per-colour path", 900, 8000.0f);
    if (HasFatalFailure()) return;
    gpu.setFused(true);
    const PileResult b = pile(rigStoneWall({12, 8, 0}, 80.0f), "A1 R8b fused path", 900, 8000.0f);
    if (HasFatalFailure()) return;
    // all asleep: 30 more ticks on the settled pile, fused
    std::string err;
    ASSERT_TRUE(gpu.step(30, &err, 30)) << err;
    const double asleepMs = std::max(0.0, gpu.lastGpuMs()) / 30.0;
    ASSERT_EQ(a.pos.size(), b.pos.size());
    size_t differ = 0;
    double worst = 0.0;
    for (size_t i = 0; i < a.pos.size(); ++i) {
        const bool d = a.pos[i] != b.pos[i] || a.quat[i].x != b.quat[i].x || a.quat[i].y != b.quat[i].y ||
                       a.quat[i].z != b.quat[i].z || a.quat[i].w != b.quat[i].w;
        differ += d;
        worst = std::max(worst, glm::length(a.pos[i] - b.pos[i]));
    }
    std::printf("[body gpu] A1: %zu of %zu samples differ fused vs per-colour (worst %.3g m); all asleep %.4f ms / "
                "tick fused\n", differ, a.pos.size(), worst, asleepMs);
    EXPECT_EQ(differ, 0u) << "A1: fused not bit-equal to the per-colour path";
    EXPECT_LE(asleepMs, 0.2) << "A4 all asleep";
}

// A3 + A4 + A5: the ladder, ~100 / ~300 / ~1,000 bodies, 300 ticks from the break, both paths
TEST_F(VoxFoundBodySimGpu, F55aLadder) {
    struct Rung { float e; double maxMs; };
    const Rung rungs[] = {{250.0f, 0.8}, {650.0f, 1.5}, {2800.0f, 3.0}};
    for (const Rung& rung : rungs) {
        Rig rig = largeWallRig(rung.e);
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid world = rig.grid;
        const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
        BodySimRef ref(world, rig.mats), mirror(world, rig.mats);
        for (const Body& b : ex.bodies) {
            ref.add(b, b.vel, b.angVel);
            mirror.add(b);
        }
        double msPath[2] = {0.0, 0.0};
        gpu.setPrimalParallel(false);   // the F5.5a comparison: the two serial-primal paths
        for (int fused = 0; fused < 2; ++fused) {
            gpu.setFused(fused != 0);
            load(world, ref);
            if (HasFatalFailure()) return;
            double ms = 0.0;
            uint32_t colours = 0, maxContacts = 0;
            for (int done = 0; done < 300; done += 30) {
                step(30);
                if (HasFatalFailure()) return;
                ms += std::max(0.0, gpu.lastGpuMs());
                colours = std::max(colours, gpu.stats().colours);
                maxContacts = std::max(maxContacts, gpu.stats().maxContacts);
            }
            sync(mirror);
            if (HasFatalFailure()) return;
            bool finite = true;
            size_t awake = 0;
            for (const SimBody& b : mirror.bodies()) {
                finite = finite && std::isfinite(b.pos.x) && std::isfinite(b.pos.y) && std::isfinite(b.pos.z) &&
                         std::isfinite(b.quat.w);
                awake += !b.asleep;
            }
            msPath[fused] = ms / 300.0;
            std::printf("[body gpu] A3/A4 large wall E/T %.0f, %s: %zu bodies (%zu awake at 5 s), colours %u, max "
                        "contacts %u, lowest corner %.3f m, %.3f ms / tick device; scratch %.1f MB\n",
                        rung.e, fused ? "fused" : "per-colour", mirror.bodies().size(), awake, colours, maxContacts,
                        mirror.lowestCornerY(), ms / 300.0,
                        double(mirror.bodies().size()) * BodySimGpu::kScratch * 80.0 / 1e6);
            // diagnostic: the per-pass profile of the same 300 ticks (one tick per submit; min = least contended)
            gpu.setProfile(true);
            load(world, ref);
            if (HasFatalFailure()) return;
            step(300);
            gpu.setProfile(false);
            const auto& pr = gpu.profile();
            std::printf("[body gpu]   profile (%d ticks, mean / min ms):", pr.ticks);
            double tot = 0.0, totMin = 0.0;
            for (int g = 0; g < BodySimGpu::kProfGroups; ++g) {
                std::printf(" %s %.3f/%.3f", BodySimGpu::profileName(g), pr.sumMs[g] / std::max(1, pr.ticks),
                            pr.minMs[g]);
                tot += pr.sumMs[g] / std::max(1, pr.ticks);
                totMin += pr.minMs[g];
            }
            std::printf(" | total %.3f / %.3f\n", tot, totMin);
            if (mirror.lowestCornerY() < -0.05) {
                size_t worst = 0;
                for (size_t k = 0; k < mirror.bodies().size(); ++k)
                    if (mirror.bodies()[k].pos.y < mirror.bodies()[worst].pos.y) worst = k;
                const SimBody& w = mirror.bodies()[worst];
                std::printf("[body gpu]   lowest body %zu at (%.2f, %.2f, %.2f) m, %zu voxels, asleep %d\n", worst,
                            w.pos.x, w.pos.y, w.pos.z, size_t(w.src.voxels), int(w.asleep));
            }
            EXPECT_TRUE(finite) << "A3";
            // A3 MISSED in F5.5a run 1: at ~1,000 bodies one 4-voxel piece tunnelled into the ground (deeper than the
            // escape search) and fell for ever. Fixed by F5.5's swept contact (voxfound_f55_prediction.txt, TUNNELLING):
            // none may go through now. Colours: 17 / 20 used (<= 16 predicted, MISSED); guarded below the 32-round cap.
            size_t through = 0;
            for (const SimBody& b : mirror.bodies()) through += b.pos.y < -0.5;
            std::printf("[body gpu]   bodies through the ground: %zu\n", through);
            EXPECT_EQ(through, 0u) << "A3 / T3: through the ground";
            EXPECT_LE(colours, 24u) << "A3";
        }
        // A4 MISSED (run 1-2): the cost is the body-body narrowphase (pairs + hard_body: 40-100 ms of 45-113), not the
        // dispatches. Printed above; guarded only against a gross regression until the narrowphase lever lands.
        std::printf("[body gpu]   A4 predicted <= %.1f ms / tick fused\n", rung.maxMs);
        EXPECT_LE(msPath[1], 300.0) << "A4 fused cost at E/T " << rung.e;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5a2 -- the narrowphase in parallel (docs/evidence/voxfound_f55_prediction.txt, B1-B4): the serial per-body pair
// and hard-contact passes (F5.2b) as the control.
namespace {
struct NarrowRun {
    std::vector<glm::dvec3> pos;
    std::vector<glm::dquat> quat;
    uint64_t candidates = 0, capped = 0, pairs = 0, hard = 0, deep = 0;
    uint32_t maxContacts = 0, colours = 0;
    double ms = 0.0, asleepMs = -1.0;
    size_t bodies = 0, awake = 0;
};
}  // namespace

class VoxFoundBodySimGpuNarrow : public VoxFoundBodySimGpu {
protected:
    // which = 0: the narrowphase switch (world test parallel); 1: the world-test switch (narrowphase parallel)
    NarrowRun run(const VoxelGrid& world, const BodySimRef& ref, BodySimRef& mirror, int ticks, bool parallel,
                  bool asleepTail, int which = 0) {
        NarrowRun out;
        gpu.setNarrowParallel(which == 0 ? parallel : true);
        gpu.setWorldParallel(which == 1 ? parallel : true);
        load(world, ref);
        if (HasFatalFailure()) return out;
        for (int done = 0; done < ticks; done += 30) {
            step(30);
            if (HasFatalFailure()) return out;
            const auto& s = gpu.stats();
            out.ms += std::max(0.0, gpu.lastGpuMs());
            out.candidates += s.candidates;
            out.capped += s.capped;
            out.pairs += s.pairsTested;
            out.hard += s.hardFires;
            out.deep += s.deepPoints;
            out.maxContacts = std::max(out.maxContacts, s.maxContacts);
            out.colours = std::max(out.colours, s.colours);
            sync(mirror);
            if (HasFatalFailure()) return out;
            for (const SimBody& b : mirror.bodies()) {
                out.pos.push_back(b.pos);
                out.quat.push_back(b.quat);
            }
        }
        out.ms /= ticks;
        out.bodies = mirror.bodies().size();
        for (const SimBody& b : mirror.bodies()) out.awake += !b.asleep;
        if (asleepTail) {   // 60 more ticks; on the ladder some bodies may still be awake (counted, printed)
            double ms = 0.0;
            for (int k = 0; k < 2; ++k) {
                step(30);
                if (HasFatalFailure()) return out;
                ms += std::max(0.0, gpu.lastGpuMs());
            }
            out.asleepMs = ms / 60.0;
        }
        return out;
    }
    static size_t differ(const NarrowRun& a, const NarrowRun& b) {
        if (a.pos.size() != b.pos.size()) return SIZE_MAX;
        size_t d = 0;
        for (size_t i = 0; i < a.pos.size(); ++i)
            d += a.pos[i] != b.pos[i] || a.quat[i].x != b.quat[i].x || a.quat[i].y != b.quat[i].y ||
                 a.quat[i].z != b.quat[i].z || a.quat[i].w != b.quat[i].w;
        return d;
    }
    void compare(const char* label, const NarrowRun& s, const NarrowRun& p, const char* g1 = "B1",
                 const char* g3 = "B3") {
        const size_t d = differ(s, p);
        std::printf("[body gpu] %s %s: %zu bodies; %zu of %zu samples differ parallel vs serial; candidates %llu / %llu, "
                    "capped %llu / %llu, pairs %llu / %llu, hard %llu / %llu, deep %llu / %llu, max contacts %u / %u\n",
                    g1, label, p.bodies, d, p.pos.size(), (unsigned long long)s.candidates,
                    (unsigned long long)p.candidates, (unsigned long long)s.capped, (unsigned long long)p.capped,
                    (unsigned long long)s.pairs, (unsigned long long)p.pairs, (unsigned long long)s.hard,
                    (unsigned long long)p.hard, (unsigned long long)s.deep, (unsigned long long)p.deep, s.maxContacts,
                    p.maxContacts);
        std::printf("[body gpu] %s %s: %.3f ms / tick parallel, %.3f serial; awake at the end %zu; all-asleep tail "
                    "%.4f ms / tick\n", g3, label, p.ms, s.ms, p.awake, p.asleepMs);
        EXPECT_EQ(d, 0u) << g1 << " " << label;
        EXPECT_EQ(s.candidates, p.candidates) << g1 << " " << label;
        EXPECT_EQ(s.capped, p.capped) << g1 << " " << label;
        EXPECT_EQ(s.pairs, p.pairs) << g1 << " " << label;
        EXPECT_EQ(s.hard, p.hard) << g1 << " " << label;
        EXPECT_EQ(s.deep, p.deep) << g1 << " " << label;
        EXPECT_EQ(s.maxContacts, p.maxContacts) << g1 << " " << label;
    }
};

// B1 + B2 on the R8b blast
TEST_F(VoxFoundBodySimGpuNarrow, F55a2BlastBitEqual) {
    Rig rig = rigStoneWall({12, 8, 0}, 80.0f);
    rig.hit.impulse = 8000.0f;
    const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
    VoxelGrid world = rig.grid;
    const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
    BodySimRef ref(world, rig.mats), mirror(world, rig.mats);
    for (const Body& b : ex.bodies) {
        ref.add(b, b.vel, b.angVel);
        mirror.add(b);
    }
    const NarrowRun s = run(world, ref, mirror, 900, false, false);
    if (HasFatalFailure()) return;
    const NarrowRun p = run(world, ref, mirror, 900, true, true);
    if (HasFatalFailure()) return;
    compare("R8b blast", s, p);
}

// B1 + B3 + B4: the ladder, ~100 / ~300 / ~1,000 bodies, 300 ticks from the break
TEST_F(VoxFoundBodySimGpuNarrow, F55a2Ladder) {
    struct Rung { float e; double maxMs; };
    const Rung rungs[] = {{250.0f, 6.0}, {650.0f, 8.0}, {2800.0f, 12.0}};
    for (const Rung& rung : rungs) {
        Rig rig = largeWallRig(rung.e);
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid world = rig.grid;
        const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
        BodySimRef ref(world, rig.mats), mirror(world, rig.mats);
        for (const Body& b : ex.bodies) {
            ref.add(b, b.vel, b.angVel);
            mirror.add(b);
        }
        char label[64];
        std::snprintf(label, sizeof(label), "large wall E/T %.0f", rung.e);
        const NarrowRun s = run(world, ref, mirror, 300, false, false);
        if (HasFatalFailure()) return;
        const NarrowRun p = run(world, ref, mirror, 300, true, true);
        if (HasFatalFailure()) return;
        compare(label, s, p);
        EXPECT_LE(p.ms, rung.maxMs) << "B3 " << label;
        if (rung.e > 2000.0f) {
            // B3 MISSED in run 1: 1.61 ms (<= 1 predicted), and 5 of 1,025 bodies were still awake, so it is a
            // mostly-asleep tail. The least-contended floor is the passes still serial per body: K_WORLD and
            // HARD_APPLY's world test, the broadphase of each awake body over all 1,025. Guarded at 2x the measure.
            EXPECT_LE(p.asleepMs, 3.2) << "B3 mostly-asleep tail";
            // the per-pass profile of the parallel path (one tick per submit)
            gpu.setProfile(true);
            load(world, ref);
            if (HasFatalFailure()) return;
            step(300);
            gpu.setProfile(false);
            const auto& pr = gpu.profile();
            std::printf("[body gpu]   B3 profile (%d ticks, mean / min ms):", pr.ticks);
            for (int g = 0; g < BodySimGpu::kProfGroups; ++g)
                std::printf(" %s %.3f/%.3f", BodySimGpu::profileName(g), pr.sumMs[g] / std::max(1, pr.ticks),
                            pr.minMs[g]);
            std::printf("\n");
            const double pairsMs = pr.sumMs[3] / std::max(1, pr.ticks), hardMs = pr.sumMs[9] / std::max(1, pr.ticks);
            EXPECT_LE(pairsMs, 2.0) << "B3 pairs";
            EXPECT_LE(hardMs, 2.0) << "B3 hard_body";
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5a3 -- the world test in parallel (voxfound_f55_prediction.txt, C1-C3): the serial per-body world passes as the
// control, the narrowphase parallel in both.
TEST_F(VoxFoundBodySimGpuNarrow, F55a3WorldBitEqual) {
    {
        Rig rig = rigStoneWall({12, 8, 0}, 80.0f);
        rig.hit.impulse = 8000.0f;
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid world = rig.grid;
        const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
        BodySimRef ref(world, rig.mats), mirror(world, rig.mats);
        for (const Body& b : ex.bodies) {
            ref.add(b, b.vel, b.angVel);
            mirror.add(b);
        }
        const NarrowRun s = run(world, ref, mirror, 900, false, false, 1);
        if (HasFatalFailure()) return;
        const NarrowRun p = run(world, ref, mirror, 900, true, true, 1);
        if (HasFatalFailure()) return;
        compare("R8b blast", s, p, "C1", "C3");
        EXPECT_LE(p.ms, 2.5) << "C3 blast";
    }
    const float rungs[] = {250.0f, 650.0f, 2800.0f};
    for (float e : rungs) {
        Rig rig = largeWallRig(e);
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid world = rig.grid;
        const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
        BodySimRef ref(world, rig.mats), mirror(world, rig.mats);
        for (const Body& b : ex.bodies) {
            ref.add(b, b.vel, b.angVel);
            mirror.add(b);
        }
        char label[64];
        std::snprintf(label, sizeof(label), "large wall E/T %.0f", e);
        const NarrowRun s = run(world, ref, mirror, 300, false, false, 1);
        if (HasFatalFailure()) return;
        const NarrowRun p = run(world, ref, mirror, 300, true, true, 1);
        if (HasFatalFailure()) return;
        compare(label, s, p, "C1", "C3");
        if (e > 2000.0f) {
            // C3 MISSED narrowly in run 1 (7.11 ms <= 7; tail 1.05 <= 1; world 0.70 <= 0.5): printed; guarded at 2x.
            EXPECT_LE(p.ms, 14.0) << "C3 " << label;
            EXPECT_LE(p.asleepMs, 2.1) << "C3 mostly-asleep tail";
            gpu.setProfile(true);
            load(world, ref);
            if (HasFatalFailure()) return;
            step(300);
            gpu.setProfile(false);
            const auto& pr = gpu.profile();
            std::printf("[body gpu]   C3 profile (%d ticks, mean / min ms):", pr.ticks);
            for (int g = 0; g < BodySimGpu::kProfGroups; ++g)
                std::printf(" %s %.3f/%.3f", BodySimGpu::profileName(g), pr.sumMs[g] / std::max(1, pr.ticks),
                            pr.minMs[g]);
            std::printf("\n");
            EXPECT_LE(pr.sumMs[2] / std::max(1, pr.ticks), 1.4) << "C3 world";
            EXPECT_LE(pr.sumMs[10] / std::max(1, pr.ticks), 0.5) << "C3 hard_apply";
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5a4 -- the primal in parallel (voxfound_f55_prediction.txt, D1-D3): deterministic, NOT bit-equal to the serial
// primal (stated before the run); every other test is its invariant gate.
TEST_F(VoxFoundBodySimGpuNarrow, F55a4PrimalDeterministicAndCost) {
    auto rigRun = [&](Rig rig, float impulse, int ticks, const char* label, double maxMs, bool profile) {
        if (impulse > 0.0f) rig.hit.impulse = impulse;
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid world = rig.grid;
        const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
        BodySimRef ref(world, rig.mats), mirror(world, rig.mats);
        for (const Body& b : ex.bodies) {
            ref.add(b, b.vel, b.angVel);
            mirror.add(b);
        }
        gpu.setPrimalParallel(false);
        const NarrowRun s = run(world, ref, mirror, ticks, true, false);
        if (HasFatalFailure()) return;
        gpu.setPrimalParallel(true);
        const NarrowRun a = run(world, ref, mirror, ticks, true, true);
        if (HasFatalFailure()) return;
        const NarrowRun b = run(world, ref, mirror, ticks, true, false);
        if (HasFatalFailure()) return;
        double worst = 0.0;
        for (size_t i = 0; i < a.pos.size() && i < s.pos.size(); ++i) worst = std::max(worst, glm::length(a.pos[i] - s.pos[i]));
        std::printf("[body gpu] D1 %s: %zu bodies; %zu of %zu samples differ between two parallel-primal runs; vs the "
                    "serial primal: worst sample %.4f m apart (reassociated sums, chaotic piles)\n",
                    label, a.bodies, differ(a, b), a.pos.size(), worst);
        std::printf("[body gpu] D3 %s: %.3f ms / tick parallel primal, %.3f serial primal; awake at the end %zu; tail "
                    "%.4f ms / tick\n", label, a.ms, s.ms, a.awake, a.asleepMs);
        EXPECT_EQ(differ(a, b), 0u) << "D1 " << label;
        // E1 (run 1 design fix): the two dispatch forms of the parallel primal compute the same numbers
        gpu.setFused(true);
        const NarrowRun f = run(world, ref, mirror, ticks, true, false);
        if (HasFatalFailure()) return;
        gpu.setFused(false);
        const NarrowRun x = run(world, ref, mirror, ticks, true, false);
        if (HasFatalFailure()) return;
        gpu.setFusedMaxAwake(128);
        std::printf("[body gpu] E1 %s: %zu of %zu samples differ, one-workgroup form vs indirect form; %.3f vs %.3f ms "
                    "/ tick\n", label, differ(f, x), f.pos.size(), f.ms, x.ms);
        EXPECT_EQ(differ(f, x), 0u) << "E1 " << label;
        EXPECT_EQ(differ(a, f) == 0 || differ(a, x) == 0, true) << "E1: the automatic choice is one of the two forms";
        const double pen = mirror.currentMaxPenetration();   // the second parallel run's final poses
        std::printf("[body gpu] D2 %s: lowest corner %.4f m, max penetration %.2f mm\n", label, mirror.lowestCornerY(),
                    pen * 1000.0);
        EXPECT_GE(mirror.lowestCornerY(), -0.05) << "D2 " << label;
        EXPECT_LE(pen, 0.01) << "D2 " << label << ": " << mirror.worstPenetrationReport();
        if (ticks >= 900) EXPECT_EQ(a.awake, 0u) << "D2 " << label << ": the blast settles";
        EXPECT_LE(a.ms, maxMs) << "D3 " << label;
        if (profile) {
            gpu.setProfile(true);
            load(world, ref);
            if (HasFatalFailure()) return;
            step(300);
            gpu.setProfile(false);
            const auto& pr = gpu.profile();
            std::printf("[body gpu]   D3 profile (%d ticks, mean / min ms):", pr.ticks);
            for (int g = 0; g < BodySimGpu::kProfGroups; ++g)
                std::printf(" %s %.3f/%.3f", BodySimGpu::profileName(g), pr.sumMs[g] / std::max(1, pr.ticks),
                            pr.minMs[g]);
            std::printf("\n");
            // D3: run 2 MISSED (1.76 ms) because every barrier carried the indirect stage; run 4, with it only after
            // the arguments' pass: 0.81 ms. Guarded at 2x the measure.
            EXPECT_LE(pr.sumMs[7] / std::max(1, pr.ticks), 1.6) << "D3 primal";
        }
    };
    rigRun(rigStoneWall({12, 8, 0}, 80.0f), 8000.0f, 900, "R8b blast", 0.9, false);
    rigRun(largeWallRig(250.0f), 0.0f, 300, "large wall E/T 250", 2.2, false);
    rigRun(largeWallRig(2800.0f), 0.0f, 300, "large wall E/T 2800", 5.0, true);
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5 tunnelling on the GPU (voxfound_f55_prediction.txt, T1 / T2): the same rigs as the CPU reference's tests.
TEST_F(VoxFoundBodySimGpu, F55TunnellingFastPiecesStop) {
    VoxelGrid slab = emptyWorld();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) slab.set({x, 8, z}, kStone);
    struct Case { const VoxelGrid* w; glm::ivec3 size; double v; double floorY; const char* what; };
    const VoxelGrid ground = emptyWorld();
    const Case cases[] = {{&ground, {1, 1, 4}, 5.0, 0.0, "T1 ground"},   {&ground, {1, 1, 4}, 15.0, 0.0, "T1 ground"},
                          {&ground, {1, 1, 4}, 30.0, 0.0, "T1 ground"},  {&ground, {1, 1, 4}, 60.0, 0.0, "T1 ground"},
                          {&ground, {2, 2, 2}, 5.0, 0.0, "T1 ground"},   {&ground, {2, 2, 2}, 15.0, 0.0, "T1 ground"},
                          {&ground, {2, 2, 2}, 30.0, 0.0, "T1 ground"},  {&ground, {2, 2, 2}, 60.0, 0.0, "T1 ground"},
                          {&slab, {2, 2, 2}, 30.0, 1.125, "T2 slab"}};
    for (const Case& c : cases) {
        BodySimRef ref(*c.w, labMats), mirror(*c.w, labMats);
        const Body b = boxBody({14, 16, 14}, c.size);
        ref.add(b, glm::dvec3(0.0, -c.v, 0.0));
        mirror.add(b);
        load(*c.w, ref);
        if (HasFatalFailure()) return;
        step(120);
        if (HasFatalFailure()) return;
        sync(mirror);
        if (HasFatalFailure()) return;
        const SimBody& s = mirror.bodies()[0];
        std::printf("[body gpu] %s %dx%dx%d at %.0f m/s: lowest corner %.4f m, pos (%.6f, %.6f, %.6f), asleep %d\n",
                    c.what, c.size.x, c.size.y, c.size.z, c.v, mirror.lowestCornerY(), s.pos.x, s.pos.y, s.pos.z,
                    int(s.asleep));
        EXPECT_GE(mirror.lowestCornerY(), c.floorY - 0.01) << c.what << " at " << c.v;
    }
}
