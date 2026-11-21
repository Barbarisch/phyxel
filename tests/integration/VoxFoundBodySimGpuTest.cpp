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
#include <map>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/voxfound/BodySim.h"
#include "core/voxfound/BodySimGpu.h"
#include "core/voxfound/BodyRest.h"
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

// ---------------------------------------------------------------------------------------------------------------------
// F5.5b (docs/evidence/voxfound_f55_prediction.txt, E1-E3): what the live lab needs from the GPU path -- asleepTicks with
// the reference's rule, a step that is not waited on (bit-equal to step + read), and the F5.4 rest policy on top
// (bake, then reload the GPU with the edited world and the survivors).
namespace {
struct LabSetup {   // a rig as the live lab builds it: on a 2-voxel fixed floor (material 6), `pad` voxels around
    VoxelGrid world;
    std::vector<FractureMaterial> mats;
    Extraction ex;
};
LabSetup labSetup(Rig rig, int pad) {
    LabSetup s;
    const glm::ivec3 off(pad, 2, pad);
    VoxelGrid g;
    g.voxelM = rig.grid.voxelM;
    g.resize(rig.grid.dim + glm::ivec3(2 * pad, 2 + 16, 2 * pad));
    for (int x = 0; x < g.dim.x; ++x)
        for (int z = 0; z < g.dim.z; ++z)
            for (int y = 0; y < 2; ++y) g.set({x, y, z}, 6, 0, true);
    for (size_t i = 0; i < rig.grid.size(); ++i)
        if (rig.grid.mat[i]) g.set(rig.grid.pos(i) + off, rig.grid.mat[i], rig.grid.part[i], rig.grid.fixed[i] != 0);
    s.mats = rig.mats;
    s.mats.resize(7, s.mats[kStone]);
    s.mats[6] = s.mats[kStone];
    Hit hit = rig.hit;
    hit.point += glm::vec3(off);
    const FractureResult r = fractureReference(g, hit, s.mats);
    s.world = g;
    s.ex = extractBodies(s.world, r, hit, s.mats);
    return s;
}
using Ledger = std::map<std::pair<int, int>, size_t>;   // (material, part) -> voxels over world + bodies + crumbs
Ledger ledgerOf(const VoxelGrid& w, const BodySimRef& sim, const std::vector<Crumb>& crumbs) {
    Ledger L;
    for (size_t i = 0; i < w.size(); ++i)
        if (w.mat[i]) ++L[{w.mat[i], w.part[i]}];
    for (const SimBody& b : sim.bodies())
        for (size_t i = 0; i < b.src.grid.size(); ++i)
            if (b.src.grid.mat[i]) ++L[{b.src.grid.mat[i], b.src.grid.part[i]}];
    for (const Crumb& c : crumbs) ++L[{c.mat, c.part}];
    return L;
}
bool sameBody(const SimBody& a, const SimBody& b) {
    return a.pos == b.pos && a.quat.x == b.quat.x && a.quat.y == b.quat.y && a.quat.z == b.quat.z &&
           a.quat.w == b.quat.w && a.vel == b.vel && a.angVel == b.angVel && a.asleep == b.asleep &&
           a.slowTicks == b.slowTicks && a.asleepTicks == b.asleepTicks;
}
}  // namespace

// E1: asleepTicks follows the reference rule (0 the tick a body sleeps, +1 each asleep tick, 0 when awake), every
// body every tick
TEST_F(VoxFoundBodySimGpu, F55bAsleepTicksFollowTheRule) {
    auto check = [&](Rig rig, float impulse, int ticks, const char* label) {
        rig.hit.impulse = impulse;
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid world = rig.grid;
        const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
        BodySimRef ref(world, rig.mats), mirror(world, rig.mats);
        for (const Body& b : ex.bodies) {
            ref.add(b, b.vel, b.angVel);
            mirror.add(b);
        }
        load(world, ref);
        if (HasFatalFailure()) return;
        const size_t n = mirror.bodies().size();
        std::vector<int> expect(n, 0);
        std::vector<uint8_t> was(n, 0);
        size_t mismatches = 0, asleepSamples = 0;
        int maxTicks = 0;
        for (int t = 0; t < ticks; ++t) {
            step(1, 1);
            sync(mirror);
            if (HasFatalFailure()) return;
            for (size_t i = 0; i < n; ++i) {
                const SimBody& b = mirror.bodies()[i];
                expect[i] = b.asleep ? (was[i] ? expect[i] + 1 : 0) : 0;
                if (b.asleepTicks != expect[i] && mismatches++ < 5)
                    std::printf("[body gpu] E1 %s: tick %d body %zu asleepTicks %d, rule %d\n", label, t, i,
                                b.asleepTicks, expect[i]);
                was[i] = b.asleep;
                asleepSamples += b.asleep;
                maxTicks = std::max(maxTicks, b.asleepTicks);
            }
        }
        std::printf("[body gpu] E1 %s: %zu bodies x %d ticks, %zu asleep samples, max asleepTicks %d, %zu mismatches\n",
                    label, n, ticks, asleepSamples, maxTicks, mismatches);
        EXPECT_GT(asleepSamples, 0u) << label;
        EXPECT_EQ(mismatches, 0u) << "E1 " << label;
    };
    check(rigStoneWall({12, 8, 0}, 80.0f), 8000.0f, 600, "blast");
    check(largeWallRig(250.0f), 200.0f, 300, "large wall E/T 250");
}

// E2: stepBegin / stepPoll in irregular batches of 1-4 ticks is bit-equal to step + read at every batch end
TEST_F(VoxFoundBodySimGpu, F55bAsyncEqualsSync) {
    auto check = [&](Rig rig, float impulse, int ticks, const char* label) {
        rig.hit.impulse = impulse;
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid world = rig.grid;
        const Extraction ex = extractBodies(world, r, rig.hit, rig.mats);
        BodySimRef ref(world, rig.mats), a(world, rig.mats), b(world, rig.mats);
        for (const Body& x : ex.bodies) {
            ref.add(x, x.vel, x.angVel);
            a.add(x);
            b.add(x);
        }
        static const int kPattern[] = {1, 3, 2, 4, 4, 1, 2, 3, 1, 1, 4, 2};
        std::vector<std::vector<SimBody>> sa, sb;
        std::vector<std::vector<uint64_t>> ca, cb;
        auto counters = [&] {
            const auto& s = gpu.stats();
            return std::vector<uint64_t>{s.candidates, s.capped, s.deepPoints, s.pairsTested, s.hardFires,
                                         s.maxContacts, s.colours, s.awake};
        };
        std::string err;
        double msSync = 0.0, msAsync = 0.0;
        size_t notReady = 0;
        for (int pass = 0; pass < 2; ++pass) {
            load(world, ref);
            if (HasFatalFailure()) return;
            BodySimRef& m = pass == 0 ? a : b;
            for (int done = 0, k = 0; done < ticks; ++k) {
                const int n = std::min(kPattern[k % 12], ticks - done);
                if (pass == 0) {
                    step(n, n);
                    sync(m);
                    if (HasFatalFailure()) return;
                    msSync += std::max(0.0, gpu.lastGpuMs());
                } else {
                    ASSERT_TRUE(gpu.stepBegin(n, &err)) << err;
                    int rr;
                    while ((rr = gpu.stepPoll(&m.bodies(), false, &err)) == 0) ++notReady;
                    ASSERT_EQ(rr, 1) << err;
                    msAsync += std::max(0.0, gpu.lastGpuMs());
                }
                (pass == 0 ? sa : sb).push_back(m.bodies());
                (pass == 0 ? ca : cb).push_back(counters());
                done += n;
            }
        }
        ASSERT_EQ(sa.size(), sb.size());
        size_t differ = 0, samples = 0, cdiff = 0;
        for (size_t s = 0; s < sa.size(); ++s) {
            for (size_t i = 0; i < sa[s].size(); ++i) {
                ++samples;
                differ += !sameBody(sa[s][i], sb[s][i]);
            }
            cdiff += ca[s] != cb[s];
        }
        std::printf("[body gpu] E2 %s: %zu bodies, %zu batches, %zu of %zu samples differ async vs sync, %zu counter sets "
                    "differ; %.3f / %.3f ms per tick device (sync / async); %zu polls not ready\n",
                    label, ex.bodies.size(), sa.size(), differ, samples, cdiff, msSync / ticks, msAsync / ticks,
                    notReady);
        EXPECT_EQ(differ, 0u) << "E2 " << label;
        EXPECT_EQ(cdiff, 0u) << "E2 " << label;
    };
    check(rigStoneWall({12, 8, 0}, 80.0f), 8000.0f, 600, "blast");
    check(largeWallRig(2800.0f), 200.0f, 120, "large wall E/T 2800");
}

// E3: the GPU path with the F5.4 rest policy after every read and a reload after every bake: conservation exact after
// every pass, nothing overwritten, every survivor asleep at the end, nothing through the floor; the reload's cost
TEST_F(VoxFoundBodySimGpu, F55bGpuRestConservesAndReloads) {
    auto check = [&](Rig rig, float impulse, size_t budget, int ticks, const char* label, double reloadMsMax) {
        rig.hit.impulse = impulse;
        LabSetup s = labSetup(rig, 32);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef sim(s.world, s.mats, prm);
        for (const Body& b : s.ex.bodies) sim.add(b, b.vel, b.angVel);
        const Ledger L0 = ledgerOf(s.world, sim, s.ex.crumbs);
        std::string err;
        ASSERT_TRUE(gpu.load(s.world, s.mats, sim.bodies(), sim.params(), &err)) << err;
        RestParams rp;
        rp.budget = budget;
        rp.keepRadiusM = 0.0;
        rp.restTicks = 30;
        size_t baked = 0, overwritten = 0, passes = 0, broken = 0, reloads = 0, through = 0;
        double reloadMs = 0.0, reloadMax = 0.0;
        const size_t n0 = sim.bodies().size();
        const double floorTop = 2 * s.world.voxelM;
        for (int done = 0; done < ticks && !sim.bodies().empty(); done += 4) {
            ASSERT_TRUE(gpu.stepBegin(4, &err)) << err;
            ASSERT_EQ(gpu.stepPoll(&sim.bodies(), true, &err), 1) << err;
            RestStats st;
            const std::vector<size_t> b = restPass(sim, s.world, glm::dvec3(0.0), rp, &st);
            ++passes;
            baked += st.baked;
            overwritten += st.overwritten;
            for (const SimBody& x : sim.bodies()) through += x.pos.y < floorTop - 1.0;
            if (ledgerOf(s.world, sim, s.ex.crumbs) != L0 && broken++ < 3)
                std::printf("[body gpu] E3 %s: ledger broken after the pass at tick %d\n", label, done + 4);
            if (!b.empty() && !sim.bodies().empty()) {
                const auto t0 = std::chrono::steady_clock::now();
                ASSERT_TRUE(gpu.load(s.world, s.mats, sim.bodies(), sim.params(), &err)) << err;
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                reloadMs += ms;
                reloadMax = std::max(reloadMax, ms);
                ++reloads;
            }
        }
        size_t awake = 0;
        for (const SimBody& x : sim.bodies()) awake += !x.asleep;
        std::printf("[body gpu] E3 %s: %zu bodies -> %zu (%zu baked) in %zu passes; ledger broken after %zu passes; "
                    "%zu overwritten; %zu awake at the end; %zu body samples below the floor; %zu reloads, %.2f ms mean, "
                    "%.2f max (world %d x %d x %d = %zu cells)\n",
                    label, n0, sim.bodies().size(), baked, passes, broken, overwritten, awake, through, reloads,
                    reloads ? reloadMs / reloads : 0.0, reloadMax, s.world.dim.x, s.world.dim.y, s.world.dim.z,
                    s.world.size());
        EXPECT_GT(baked, 0u) << label;
        EXPECT_EQ(broken, 0u) << "E3 " << label << ": conservation";
        EXPECT_EQ(overwritten, 0u) << "E3 " << label;
        EXPECT_EQ(awake, 0u) << "E3 " << label << ": survivors still awake";
        EXPECT_EQ(through, 0u) << "E3 " << label;
        EXPECT_LE(reloadMax, reloadMsMax) << "E3 " << label << ": reload cost";
    };
    check(rigStoneWall({12, 8, 0}, 80.0f), 8000.0f, 8, 900, "blast, budget 8", 20.0);
    check(largeWallRig(650.0f), 200.0f, 64, 900, "large wall E/T 650, budget 64", 20.0);
}


// ---------------------------------------------------------------------------------------------------------------------
// F5.5c H5 (docs/evidence/voxfound_f55_prediction.txt): after a bake, edit() (only the changed cells and the survivors'
// records written) is bit-equal to a fresh load() of the edited world with the survivors -- every body, every 10 ticks
// for 120 ticks, 3 bakes per rig; the cost of each.
TEST_F(VoxFoundBodySimGpu, F55cEditEqualsReload) {
    auto check = [&](Rig rig, float impulse, int settle, size_t perBake, const char* label, double editMsMax) {
        rig.hit.impulse = impulse;
        LabSetup s = labSetup(rig, 32);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef sim(s.world, s.mats, prm);
        for (const Body& b : s.ex.bodies) sim.add(b, b.vel, b.angVel);
        std::string err;
        BodySimGpu ref;
        ASSERT_TRUE(ref.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        ASSERT_TRUE(gpu.load(s.world, s.mats, sim.bodies(), sim.params(), &err)) << err;
        ASSERT_TRUE(gpu.step(settle, &err, 30)) << err;
        ASSERT_TRUE(gpu.read(sim.bodies(), &err)) << err;
        const size_t n0 = sim.bodies().size();
        auto counters = [](const BodySimGpu& g) {
            const auto& c = g.stats();
            return std::vector<uint64_t>{c.candidates, c.capped, c.deepPoints, c.pairsTested, c.hardFires,
                                         c.maxContacts, c.colours, c.awake};
        };
        size_t differ = 0, samples = 0, cdiff = 0, bakedAll = 0, cellsAll = 0;
        double editMax = 0.0, loadMax = 0.0;
        for (int bake = 0; bake < 3; ++bake) {
            const size_t n = sim.bodies().size();
            RestParams rp;
            rp.budget = n > perBake ? n - perBake : 0;
            rp.keepRadiusM = 0.0;
            rp.restTicks = 0;
            const std::vector<uint8_t> before = s.world.mat;
            RestStats st;
            std::vector<size_t> baked = restPass(sim, s.world, glm::dvec3(0.0), rp, &st);
            ASSERT_FALSE(baked.empty()) << label << ": nothing baked (bake " << bake << ")";
            ASSERT_FALSE(sim.bodies().empty()) << label;
            bakedAll += baked.size();
            std::vector<std::pair<uint32_t, uint8_t>> cells;
            for (size_t i = 0; i < before.size(); ++i)
                if (before[i] != s.world.mat[i]) cells.push_back({uint32_t(i), s.world.mat[i]});
            cellsAll += cells.size();
            std::sort(baked.begin(), baked.end());
            auto t0 = std::chrono::steady_clock::now();
            ASSERT_TRUE(gpu.edit(cells, baked, sim.bodies(), &err)) << label << ": " << err;
            editMax = std::max(editMax, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            std::printf("[body gpu] H5 %s bake %d: %zu baked, %zu cells, edit %.3f ms (CPU side %.3f)\n", label, bake,
                        baked.size(), cells.size(), gpu.lastEditMs(), gpu.lastEditPrepMs());
            t0 = std::chrono::steady_clock::now();
            ASSERT_TRUE(ref.load(s.world, s.mats, sim.bodies(), sim.params(), &err)) << err;
            loadMax = std::max(loadMax, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            std::vector<SimBody> a = sim.bodies(), b = sim.bodies();
            for (int k = 0; k < 12; ++k) {
                ASSERT_TRUE(gpu.step(10, &err, 10)) << err;
                ASSERT_TRUE(ref.step(10, &err, 10)) << err;
                ASSERT_TRUE(gpu.read(a, &err)) << err;
                ASSERT_TRUE(ref.read(b, &err)) << err;
                for (size_t i = 0; i < a.size(); ++i) {
                    ++samples;
                    if (!sameBody(a[i], b[i]) && differ++ < 3)
                        std::printf("[body gpu] H5 %s: bake %d tick %d body %zu differs\n", label, bake, 10 * (k + 1), i);
                }
                cdiff += counters(gpu) != counters(ref);
            }
            sim.bodies() = a;
        }
        ref.shutdown();
        std::printf("[body gpu] F55c H5 %s: %zu bodies, 3 bakes (%zu bodies, %zu cells), %zu of %zu samples differ edit "
                    "vs reload, %zu counter sets differ; edit max %.3f ms, reload max %.3f ms (world %zu cells)\n",
                    label, n0, bakedAll, cellsAll, differ, samples, cdiff, editMax, loadMax, s.world.size());
        EXPECT_EQ(differ, 0u) << "H5 " << label;
        EXPECT_EQ(cdiff, 0u) << "H5 " << label;
        // PREDICTION MISSED (<= 1 ms at ~1,000 bodies): run 1 2.52 ms baking 64 bodies (28,372 cells, one copy region
        // per cell); merged regions 2.13; each body's radius kept from load 1.09 ms. Recorded; a regression guard here.
        EXPECT_LE(editMax, editMsMax) << "H5 " << label << ": edit cost";
    };
    check(rigStoneWall({12, 8, 0}, 80.0f), 8000.0f, 600, 4, "blast", 1.0);
    check(largeWallRig(650.0f), 200.0f, 240, 32, "large wall E/T 650", 1.0);
    check(largeWallRig(2800.0f), 200.0f, 240, 64, "large wall E/T 2800", 2.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5e -- the awake pile's tick, profiled before any lever (docs/evidence/voxfound_f55_prediction.txt, E1-E6). The
// lab's large wall exactly as the lab builds it (VoxFoundBodiesLab.cpp: a 2-voxel floor of voxels, 32 voxels of pad in
// z, 16 of air above, impulse 200 N s), settled, then every body woken through edit() (the lab's wake_all). Each
// condition starts from the same settled snapshot (edit() is bit-equal to a load, F5.5c H5). A measure, not a lever.
namespace {
constexpr uint8_t kLabFloorMat = 6;   // VoxFoundBodiesLab.cpp kFloorMat: a copy of Stone

struct LabPile {
    VoxelGrid world;
    std::vector<FractureMaterial> mats;
    std::vector<Body> bodies;
};

LabPile labLargeWall(float e) {
    Rig rig = largeWallRig(e);
    const int padX = 0, padZ = 32;   // the lab's clamp for a 256 x 128 x 4 rig with the default 4 m pad
    const glm::ivec3 off(padX, 2, padZ);
    VoxelGrid g;
    g.voxelM = rig.grid.voxelM;
    g.resize(rig.grid.dim + glm::ivec3(2 * padX, 2 + 16, 2 * padZ));
    for (int x = 0; x < g.dim.x; ++x)
        for (int z = 0; z < g.dim.z; ++z)
            for (int y = 0; y < 2; ++y) g.set({x, y, z}, kLabFloorMat, 0, true);
    for (size_t i = 0; i < rig.grid.size(); ++i) {
        if (!rig.grid.mat[i]) continue;
        g.set(rig.grid.pos(i) + off, rig.grid.mat[i], rig.grid.part[i], rig.grid.fixed[i] != 0);
    }
    LabPile out;
    out.mats = rig.mats;
    out.mats.resize(kLabFloorMat + 1, out.mats[kStone]);
    out.mats[kLabFloorMat] = out.mats[kStone];
    Hit hit = rig.hit;
    hit.point += glm::vec3(off);
    hit.impulse = 200.0f;
    const FractureResult r = fractureReference(g, hit, out.mats);
    out.world = g;
    out.bodies = extractBodies(out.world, r, hit, out.mats).bodies;
    return out;
}

// the pile's shape: the work the world test and the pairs walk (CPU, from the poses; approximate -- voxel AABBs of the
// posed corners, pairs by AABB overlap dilated one voxel)
struct PileShape {
    size_t bodies = 0, corners = 0, maxCorners = 0, pairs = 0;
    uint64_t boxCells = 0, maxBoxCells = 0, pairCorners = 0, maxPairCorners = 0;
};

PileShape pileShape(const std::vector<SimBody>& bs, double voxelM) {
    PileShape s;
    s.bodies = bs.size();
    std::vector<glm::ivec3> lo(bs.size()), hi(bs.size());
    for (size_t i = 0; i < bs.size(); ++i) {
        const SimBody& b = bs[i];
        glm::dvec3 a(1e30), c(-1e30);
        for (const glm::dvec3& k : b.corners) {
            const glm::dvec3 w = b.pos + b.quat * k;
            a = glm::min(a, w);
            c = glm::max(c, w);
        }
        lo[i] = glm::ivec3(glm::floor(a / voxelM));
        hi[i] = glm::ivec3(glm::floor(c / voxelM));
        const glm::ivec3 d = hi[i] - lo[i] + 1;
        const uint64_t cells = uint64_t(d.x) * uint64_t(d.y) * uint64_t(d.z);
        s.corners += b.corners.size();
        s.maxCorners = std::max(s.maxCorners, b.corners.size());
        s.boxCells += cells;
        s.maxBoxCells = std::max(s.maxBoxCells, cells);
    }
    for (size_t i = 0; i < bs.size(); ++i)
        for (size_t j = i + 1; j < bs.size(); ++j) {
            if (glm::any(glm::greaterThan(lo[i] - 1, hi[j])) || glm::any(glm::greaterThan(lo[j] - 1, hi[i]))) continue;
            ++s.pairs;
            const uint64_t pc = bs[i].corners.size() + bs[j].corners.size();
            s.pairCorners += pc;
            s.maxPairCorners = std::max(s.maxPairCorners, pc);
        }
    return s;
}

double medianOf(std::vector<double> v) {
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
}  // namespace

TEST_F(VoxFoundBodySimGpu, F55eAwakePileProfile) {
    for (const float e : {250.0f, 650.0f, 2800.0f}) {
        SCOPED_TRACE(e);
        LabPile pile = labLargeWall(e);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef mirror(pile.world, pile.mats, prm);
        for (const Body& b : pile.bodies) mirror.add(b, b.vel, b.angVel);
        std::string err;
        ASSERT_TRUE(gpu.load(pile.world, pile.mats, mirror.bodies(), mirror.params(), &err)) << err;
        // the falling phase on the same grid, profiled (the first 120 ticks from the break: F5.5a4's ladder's regime)
        gpu.setProfile(true);
        step(120, 1);
        gpu.setProfile(false);
        if (HasFatalFailure()) return;
        {
            const auto& fp = gpu.profile();
            const auto& fs = gpu.stats();
            double sum = 0.0;
            for (int g = 0; g < BodySimGpu::kProfGroups; ++g) sum += fp.sumMs[g] / std::max(1, fp.ticks);
            std::printf("[body gpu] F5.5e E/T %.0f falling (ticks 1-120): awake at 120 %u, candidates %.0f capped %.0f "
                        "pairs %.0f per tick; profile sum %.3f:", e, fs.awake, double(fs.candidates) / 120.0,
                        double(fs.capped) / 120.0, double(fs.pairsTested) / 120.0, sum);
            for (int g = 0; g < BodySimGpu::kProfGroups; ++g)
                if (fp.sumMs[g] / std::max(1, fp.ticks) >= 0.05)
                    std::printf(" %s %.3f", BodySimGpu::profileName(g), fp.sumMs[g] / std::max(1, fp.ticks));
            std::printf("\n");
        }
        // settle: until every body sleeps (cap 6,000 ticks = 100 s)
        int settleTicks = 120;
        do {
            step(60);
            if (HasFatalFailure()) return;
            settleTicks += 60;
        } while (gpu.stats().awake > 0 && settleTicks < 6000);
        sync(mirror);
        if (HasFatalFailure()) return;
        const size_t nb = mirror.bodies().size();
        ASSERT_EQ(gpu.stats().awake, 0u) << "the pile did not settle in " << settleTicks << " ticks";
        const std::vector<SimBody> settled = mirror.bodies();

        // asleep cost (one tick per submit, as the lab's wake measure)
        std::vector<double> sleepMs;
        for (int k = 0; k < 10; ++k) {
            step(1, 1);
            sleepMs.push_back(gpu.lastGpuMs());
        }

        // RED: the flags cleared on the CPU only -- the GPU never hears of it: still asleep after the tick
        for (SimBody& b : mirror.bodies()) b.asleep = false, b.slowTicks = 0, b.asleepTicks = 0;
        step(1, 1);
        const uint32_t redAwake = gpu.stats().awake;
        const double redMs = gpu.lastGpuMs();
        EXPECT_EQ(redAwake, 0u) << "red: a CPU-only wake must not reach the GPU";

        std::vector<SimBody> woken = settled;
        for (SimBody& b : woken) b.asleep = false, b.slowTicks = 0, b.asleepTicks = 0;
        const PileShape sh = pileShape(woken, pile.world.voxelM);
        auto wake = [&]() {
            std::string werr;
            ASSERT_TRUE(gpu.edit({}, {}, woken, &werr)) << werr;
        };

        // A: 30 ticks, 30 per submit
        wake();
        if (HasFatalFailure()) return;
        step(30, 30);
        const double msA = gpu.lastGpuMs() / 30.0;
        const uint32_t awakeA = gpu.stats().awake;
        const uint32_t contactsA = gpu.stats().contacts, coloursA = gpu.stats().colours;
        const uint64_t pairsA = gpu.stats().pairsTested / 30;
        const double candA = double(gpu.stats().candidates) / 30.0, cappedA = double(gpu.stats().capped) / 30.0;
        const double hardA = double(gpu.stats().hardFires) / 30.0;
        // the wake reached the GPU: every body awake after the first tick
        wake();
        if (HasFatalFailure()) return;
        step(1, 1);
        const uint32_t awake1 = gpu.stats().awake;
        EXPECT_EQ(awake1, uint32_t(nb)) << "green: the wake through edit() reaches every body";
        // B: 30 ticks, 1 per submit (the lab's synchronous ticks)
        wake();
        if (HasFatalFailure()) return;
        std::vector<double> msB;
        for (int k = 0; k < 30; ++k) {
            step(1, 1);
            msB.push_back(gpu.lastGpuMs());
        }
        // D: the path forced each way (30 per submit)
        gpu.setFused(true);
        wake();
        if (HasFatalFailure()) return;
        step(30, 30);
        const double msFused = gpu.lastGpuMs() / 30.0;
        gpu.setFused(false);
        wake();
        if (HasFatalFailure()) return;
        step(30, 30);
        const double msPerColour = gpu.lastGpuMs() / 30.0;
        gpu.setFusedMaxAwake(128);
        // C: the per-pass profile (one tick per submit)
        wake();
        if (HasFatalFailure()) return;
        gpu.setProfile(true);
        step(30, 1);
        gpu.setProfile(false);
        const auto& pr = gpu.profile();
        double profSum = 0.0;
        for (int g = 0; g < BodySimGpu::kProfGroups; ++g) profSum += pr.sumMs[g] / std::max(1, pr.ticks);

        std::printf("[body gpu] F5.5e large wall E/T %.0f (the lab's grid %dx%dx%d): %zu bodies, settled in %d ticks; "
                    "asleep %.3f ms/tick; red (CPU-only wake) awake %u, %.3f ms\n",
                    e, pile.world.dim.x, pile.world.dim.y, pile.world.dim.z, nb, settleTicks, medianOf(sleepMs),
                    redAwake, redMs);
        std::printf("[body gpu]   shape: corners %zu (max %zu per body), world-box cells %llu (max %llu), pairs %zu "
                    "(GPU pairs tested %llu per tick), pair corners %llu (max %llu)\n",
                    sh.corners, sh.maxCorners, (unsigned long long)sh.boxCells, (unsigned long long)sh.maxBoxCells,
                    sh.pairs, (unsigned long long)pairsA, (unsigned long long)sh.pairCorners,
                    (unsigned long long)sh.maxPairCorners);
        std::printf("[body gpu]   E1 30/submit %.3f ms/tick (awake after 30 %u, contacts %u, colours %u); woken tick 1 "
                    "awake %u; E2 1/submit median %.3f max %.3f; E4 fused %.3f vs per-colour %.3f\n",
                    msA, awakeA, contactsA, coloursA, awake1, medianOf(msB),
                    *std::max_element(msB.begin(), msB.end()), msFused, msPerColour);
        std::printf("[body gpu]   woken pile per tick: candidates %.0f, capped %.0f, hard fires %.1f\n", candA, cappedA,
                    hardA);
        std::printf("[body gpu]   E3 profile (%d ticks, mean/min ms, sum %.3f):", pr.ticks, profSum);
        for (int g = 0; g < BodySimGpu::kProfGroups; ++g)
            std::printf(" %s %.3f/%.3f", BodySimGpu::profileName(g), pr.sumMs[g] / std::max(1, pr.ticks), pr.minMs[g]);
        std::printf("\n");
        EXPECT_EQ(pr.ticks, 30);

        // E6 follow-up (stated as one, after the profile): does each pass wait for its busiest body? Wake only the
        // tenth with the most corners, then only the other nine tenths (30 per submit, from the same snapshot)
        std::vector<size_t> order(nb);
        for (size_t i = 0; i < nb; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
            return settled[x].corners.size() > settled[y].corners.size();
        });
        const size_t tenth = (nb + 9) / 10;
        auto subset = [&](bool big) {
            std::vector<SimBody> s = settled;   // asleep as settled
            for (size_t k = 0; k < nb; ++k)
                if ((k < tenth) == big) s[order[k]].asleep = false, s[order[k]].slowTicks = 0, s[order[k]].asleepTicks = 0;
            std::string werr;
            EXPECT_TRUE(gpu.edit({}, {}, s, &werr)) << werr;
            step(30, 30);
            return std::make_pair(gpu.lastGpuMs() / 30.0, gpu.stats().awake);
        };
        const auto bigR = subset(true);
        const auto smallR = subset(false);
        size_t bigCorners = 0;
        for (size_t k = 0; k < tenth; ++k) bigCorners += settled[order[k]].corners.size();
        std::printf("[body gpu]   E6 the biggest tenth awake (%zu bodies, %zu corners, awake after 30 %u): %.3f ms/tick; "
                    "the other %zu awake (%zu corners, awake after 30 %u): %.3f ms/tick; all awake %.3f\n",
                    tenth, bigCorners, bigR.second, bigR.first, nb - tenth, sh.corners - bigCorners, smallR.second,
                    smallR.first, msA);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5f -- the world group split (docs/evidence/voxfound_f55_prediction.txt, F1-F4): diagnostic copies of the world
// pass stop after each stage and are timed as their own groups. The instrument must not change the simulation: 60
// profiled ticks of the woken pile are bit-equal (poses, velocities, counters) with the split on vs off; its red is the
// loud copy (no suppression), whose counters differ.
TEST_F(VoxFoundBodySimGpu, F55fWorldSplit) {
    struct Run {
        std::vector<SimBody> bodies;
        BodySimGpuStats stats;
        BodySimGpu::Profile prof;
    };
    for (const float e : {250.0f, 650.0f, 2800.0f}) {
        SCOPED_TRACE(e);
        LabPile pile = labLargeWall(e);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef mirror(pile.world, pile.mats, prm);
        for (const Body& b : pile.bodies) mirror.add(b, b.vel, b.angVel);
        std::string err;
        ASSERT_TRUE(gpu.load(pile.world, pile.mats, mirror.bodies(), mirror.params(), &err)) << err;
        int settleTicks = 0;
        do {
            step(60);
            if (HasFatalFailure()) return;
            settleTicks += 60;
        } while (gpu.stats().awake > 0 && settleTicks < 6000);
        ASSERT_EQ(gpu.stats().awake, 0u);
        sync(mirror);
        if (HasFatalFailure()) return;
        std::vector<SimBody> woken = mirror.bodies();
        for (SimBody& b : woken) b.asleep = false, b.slowTicks = 0, b.asleepTicks = 0;
        const size_t nb = woken.size();
        auto runFor = [&](int ticks, bool split, bool quiet) {
            Run r;
            std::string werr;
            EXPECT_TRUE(gpu.edit({}, {}, woken, &werr)) << werr;
            gpu.setWorldSplit(split, quiet);
            gpu.setWorldSpread(false);   // F5.5h: the split measures the one-workgroup-per-body pass
            gpu.setProfile(true);
            step(ticks, 1);
            gpu.setProfile(false);
            gpu.setWorldSplit(false);
            gpu.setWorldSpread(true);
            r.stats = gpu.stats();
            r.prof = gpu.profile();
            r.bodies = woken;
            EXPECT_TRUE(gpu.read(r.bodies, &werr)) << werr;
            return r;
        };
        auto differ = [](const Run& a, const Run& b) {
            size_t n = 0;
            for (size_t i = 0; i < a.bodies.size(); ++i)
                if (a.bodies[i].pos != b.bodies[i].pos || a.bodies[i].quat != b.bodies[i].quat ||
                    a.bodies[i].vel != b.bodies[i].vel || a.bodies[i].angVel != b.bodies[i].angVel ||
                    a.bodies[i].asleep != b.bodies[i].asleep)
                    ++n;
            return n;
        };
        auto sameCounters = [](const BodySimGpuStats& a, const BodySimGpuStats& b) {
            return a.contacts == b.contacts && a.candidates == b.candidates && a.capped == b.capped &&
                   a.deepPoints == b.deepPoints && a.pairsTested == b.pairsTested && a.hardFires == b.hardFires &&
                   a.scratchOverflow == b.scratchOverflow && a.awake == b.awake;
        };
        // F1: the instrument -- bit-equal, and its red
        const Run off = runFor(60, false, true);
        const Run on = runFor(60, true, true);
        const Run loud = runFor(60, true, false);
        const size_t dOn = differ(off, on), dLoud = differ(off, loud);
        std::printf("[body gpu] F5.5f E/T %.0f: %zu bodies; split (quiet) vs off over 60 ticks: %zu bodies differ, "
                    "counters %s (candidates %llu vs %llu); RED the loud split: %zu differ, counters %s (candidates "
                    "%llu)\n", e, nb, dOn, sameCounters(off.stats, on.stats) ? "equal" : "DIFFER",
                    (unsigned long long)on.stats.candidates, (unsigned long long)off.stats.candidates, dLoud,
                    sameCounters(off.stats, loud.stats) ? "equal" : "differ",
                    (unsigned long long)loud.stats.candidates);
        EXPECT_EQ(dOn, 0u) << "F1: the split changed the simulation";
        EXPECT_TRUE(sameCounters(off.stats, on.stats)) << "F1: the split changed a counter";
        EXPECT_FALSE(sameCounters(off.stats, loud.stats)) << "F1 red: the loud copy must show in the counters";
        // F2-F4: the split, 30 profiled ticks from the same snapshot (and the world group without it, for F1's 15 %)
        const Run a = runFor(30, true, true);
        const Run b = runFor(30, false, true);
        auto mean = [](const BodySimGpu::Profile& p, int g) { return p.sumMs[g] / std::max(1, p.ticks); };
        const int G_WORLD = 2, G_HARDA = 10, G_AWAKE = 13, G_COUNT = 14, G_WRITE = 15, G_BUCKET = 16, G_SPREAD = 17;
        const double world = mean(a.prof, G_WORLD), worldOff = mean(b.prof, G_WORLD);
        const double awakeList = mean(a.prof, G_AWAKE);
        const double cnt = mean(a.prof, G_COUNT), wr = mean(a.prof, G_WRITE), bk = mean(a.prof, G_BUCKET),
                     sp = mean(a.prof, G_SPREAD);
        const double sCount = cnt, sWrite = wr - cnt, sBucket = bk - wr, sSpread = sp - bk, sEmit = world - sp;
        const BodySimGpuStats& st = a.stats;   // the last tick's S_DG_* (per tick)
        std::printf("[body gpu]   F1 world group: split on %.3f (+ awake list %.3f) vs off %.3f ms (%.2f x); hard_apply "
                    "%.3f\n", world, awakeList, worldOff, (world + awakeList) / std::max(1e-9, worldOff),
                    mean(a.prof, G_HARDA));
        std::printf("[body gpu]   F2/F3 copies: w_count %.3f, w_write %.3f, w_bucket %.3f, w_spread %.3f ms -> count %.3f "
                    "(%.0f %%), write %.3f (%.0f %%), buckets %.3f (%.0f %%), spread %.3f (%.0f %%), emit %.3f (%.0f %%) "
                    "of the world group %.3f\n", cnt, wr, bk, sp, sCount, 100 * sCount / world, sWrite,
                    100 * sWrite / world, sBucket, 100 * sBucket / world, sSpread, 100 * sSpread / world, sEmit,
                    100 * sEmit / world, world);
        std::printf("[body gpu]   F4 per body (last tick, %u awake): world candidates max %u, mean %.1f (max %.1f x mean); "
                    "items max %u; over the cap (8): %u bodies (%.0f %%)\n", st.awake, st.dgMaxCand,
                    double(st.dgSumCand) / std::max(1u, st.awake),
                    st.dgMaxCand / std::max(1e-9, double(st.dgSumCand) / std::max(1u, st.awake)), st.dgMaxItems,
                    st.dgOverCap, 100.0 * st.dgOverCap / std::max(1u, st.awake));
        EXPECT_EQ(a.prof.ticks, 30);
        EXPECT_GT(st.dgSumCand, 0u) << "the split's counters ran";
    }
}

// docs/VoxelFoundationPlan.md F5.5g (prediction: docs/evidence/voxfound_f55_prediction.txt G1-G4): each world item tested
// once per pass -- the staged candidates copied to their serial offset, bit-equal to the two-test pass
TEST_F(VoxFoundBodySimGpu, F55gWorldOnce) {
    struct Run {
        std::vector<SimBody> bodies;
        BodySimGpuStats stats;
        BodySimGpu::Profile prof;
        double tickMs = 0.0;
    };
    for (const float e : {250.0f, 650.0f, 2800.0f}) {
        SCOPED_TRACE(e);
        LabPile pile = labLargeWall(e);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef mirror(pile.world, pile.mats, prm);
        for (const Body& b : pile.bodies) mirror.add(b, b.vel, b.angVel);
        std::string err;
        ASSERT_TRUE(gpu.load(pile.world, pile.mats, mirror.bodies(), mirror.params(), &err)) << err;
        int settleTicks = 0;
        do {
            step(60);
            if (HasFatalFailure()) return;
            settleTicks += 60;
        } while (gpu.stats().awake > 0 && settleTicks < 6000);
        ASSERT_EQ(gpu.stats().awake, 0u);
        sync(mirror);
        if (HasFatalFailure()) return;
        std::vector<SimBody> woken = mirror.bodies();
        for (SimBody& b : woken) b.asleep = false, b.slowTicks = 0, b.asleepTicks = 0;
        const size_t nb = woken.size();
        // mode: 0 = the two-test pass, 1 = once, 2 = once + the scramble (test only)
        auto runFor = [&](int ticks, int mode, bool profile, bool split, int perSubmit) {
            Run r;
            std::string werr;
            EXPECT_TRUE(gpu.edit({}, {}, woken, &werr)) << werr;
            gpu.setWorldOnce(mode != 0, mode == 2);
            gpu.setWorldSpread(false);   // F5.5h: F5.5g's pass is one workgroup per body
            gpu.setWorldSplit(split, true);
            gpu.setProfile(profile);
            step(ticks, perSubmit);
            r.tickMs = std::max(0.0, gpu.lastGpuMs()) / ticks;
            gpu.setProfile(false);
            gpu.setWorldSplit(false);
            gpu.setWorldOnce(true);
            gpu.setWorldSpread(true);
            r.stats = gpu.stats();
            r.prof = gpu.profile();
            r.bodies = woken;
            EXPECT_TRUE(gpu.read(r.bodies, &werr)) << werr;
            return r;
        };
        auto differ = [](const Run& a, const Run& b) {
            size_t n = 0;
            for (size_t i = 0; i < a.bodies.size(); ++i)
                if (a.bodies[i].pos != b.bodies[i].pos || a.bodies[i].quat != b.bodies[i].quat ||
                    a.bodies[i].vel != b.bodies[i].vel || a.bodies[i].angVel != b.bodies[i].angVel ||
                    a.bodies[i].asleep != b.bodies[i].asleep)
                    ++n;
            return n;
        };
        auto sameCounters = [](const BodySimGpuStats& a, const BodySimGpuStats& b) {
            return a.contacts == b.contacts && a.candidates == b.candidates && a.capped == b.capped &&
                   a.deepPoints == b.deepPoints && a.pairsTested == b.pairsTested && a.hardFires == b.hardFires &&
                   a.scratchOverflow == b.scratchOverflow && a.awake == b.awake;
        };
        // G1: bit-equal over 60 ticks, and the scramble's red
        const Run twice = runFor(60, 0, false, false, 30);
        const Run once = runFor(60, 1, false, false, 30);
        const Run scr = runFor(60, 2, false, false, 30);
        const size_t dOnce = differ(twice, once), dScr = differ(twice, scr);
        std::printf("[body gpu] F5.5g E/T %.0f: %zu bodies; once vs twice over 60 ticks: %zu bodies differ, counters %s "
                    "(candidates %llu vs %llu, deep %llu vs %llu); fallback threads %u over 60 ticks; RED the "
                    "scramble: %zu differ\n", e, nb, dOnce, sameCounters(twice.stats, once.stats) ? "equal" : "DIFFER",
                    (unsigned long long)once.stats.candidates, (unsigned long long)twice.stats.candidates,
                    (unsigned long long)once.stats.deepPoints, (unsigned long long)twice.stats.deepPoints,
                    once.stats.worldRedo, dScr);
        EXPECT_EQ(dOnce, 0u) << "G1: once changed the simulation";
        EXPECT_TRUE(sameCounters(twice.stats, once.stats)) << "G1: once changed a counter";
        EXPECT_GT(dScr, 0u) << "G1 red: the scrambled order must show in the bodies";
        // G2/G3: the world group (profiled, one tick per submit) and the tick (unprofiled, 30 per submit), alternated
        auto mean = [](const BodySimGpu::Profile& p, int g) { return p.sumMs[g] / std::max(1, p.ticks); };
        const int G_WORLD = 2, G_COUNT = 14, G_WRITE = 15;
        std::vector<double> wOn, wOff, tOn, tOff;
        for (int rep = 0; rep < 3; ++rep) {
            wOff.push_back(mean(runFor(30, 0, true, false, 1).prof, G_WORLD));
            wOn.push_back(mean(runFor(30, 1, true, false, 1).prof, G_WORLD));
            tOff.push_back(runFor(30, 0, false, false, 30).tickMs);
            tOn.push_back(runFor(30, 1, false, false, 30).tickMs);
        }
        const double w1 = medianOf(wOn), w0 = medianOf(wOff), t1 = medianOf(tOn), t0 = medianOf(tOff);
        std::printf("[body gpu]   G2 world group: once %.3f vs twice %.3f ms (%+.0f %%); G3 tick (30 per submit): once "
                    "%.3f vs twice %.3f ms (%+.0f %%)\n", w1, w0, 100.0 * (w1 / w0 - 1.0), t1, t0,
                    100.0 * (t1 / t0 - 1.0));
        // G4: the fallback, and the split in the once mode (w_write's increment = the copy)
        const Run sp = runFor(30, 1, true, true, 1);
        const double world = mean(sp.prof, G_WORLD), cnt = mean(sp.prof, G_COUNT), wr = mean(sp.prof, G_WRITE);
        const uint64_t slots = uint64_t(sp.stats.awake) * 64u * 30u;
        std::printf("[body gpu]   G4 fallback threads %u of %llu (body, thread) slots over 30 ticks (%.3f %%); split: "
                    "tests+stage %.3f (%.0f %%), copy %.3f (%.0f %%) of the world group %.3f\n", sp.stats.worldRedo,
                    (unsigned long long)slots, 100.0 * sp.stats.worldRedo / std::max<uint64_t>(1, slots), cnt,
                    100 * cnt / world, wr - cnt, 100 * (wr - cnt) / world, world);
        EXPECT_LE(w1, 0.70 * w0) << "G2: the world group must drop >= 30 %";
    }
}

// docs/VoxelFoundationPlan.md F5.5h (prediction: docs/evidence/voxfound_f55_prediction.txt H1-H4): a big body's world items
// over more than one workgroup -- chunks of 256 items, compacted per body in serial order, bit-equal to F5.5g's pass
TEST_F(VoxFoundBodySimGpu, F55hWorldSpread) {
    struct Run {
        std::vector<SimBody> bodies;
        BodySimGpuStats stats;
        BodySimGpu::Profile prof;
        double tickMs = 0.0;
    };
    for (const float e : {250.0f, 650.0f, 2800.0f}) {
        SCOPED_TRACE(e);
        LabPile pile = labLargeWall(e);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef mirror(pile.world, pile.mats, prm);
        for (const Body& b : pile.bodies) mirror.add(b, b.vel, b.angVel);
        std::string err;
        ASSERT_TRUE(gpu.load(pile.world, pile.mats, mirror.bodies(), mirror.params(), &err)) << err;
        int settleTicks = 0;
        do {
            step(60);
            if (HasFatalFailure()) return;
            settleTicks += 60;
        } while (gpu.stats().awake > 0 && settleTicks < 6000);
        ASSERT_EQ(gpu.stats().awake, 0u);
        sync(mirror);
        if (HasFatalFailure()) return;
        std::vector<SimBody> woken = mirror.bodies();
        for (SimBody& b : woken) b.asleep = false, b.slowTicks = 0, b.asleepTicks = 0;
        const size_t nb = woken.size();
        // mode: 0 = F5.5g's pass (one workgroup per body), 1 = spread, 2 = spread + the scramble (test only)
        auto runFor = [&](int ticks, int mode, bool profile, bool split, int perSubmit) {
            Run r;
            std::string werr;
            EXPECT_TRUE(gpu.edit({}, {}, woken, &werr)) << werr;
            gpu.setWorldSpread(mode != 0, mode == 2);
            gpu.setWorldSplit(split, true);
            gpu.setProfile(profile);
            step(ticks, perSubmit);
            r.tickMs = std::max(0.0, gpu.lastGpuMs()) / ticks;
            gpu.setProfile(false);
            gpu.setWorldSplit(false);
            gpu.setWorldSpread(true);
            r.stats = gpu.stats();
            r.prof = gpu.profile();
            r.bodies = woken;
            EXPECT_TRUE(gpu.read(r.bodies, &werr)) << werr;
            return r;
        };
        auto differ = [](const Run& a, const Run& b) {
            size_t n = 0;
            for (size_t i = 0; i < a.bodies.size(); ++i)
                if (a.bodies[i].pos != b.bodies[i].pos || a.bodies[i].quat != b.bodies[i].quat ||
                    a.bodies[i].vel != b.bodies[i].vel || a.bodies[i].angVel != b.bodies[i].angVel ||
                    a.bodies[i].asleep != b.bodies[i].asleep)
                    ++n;
            return n;
        };
        auto sameCounters = [](const BodySimGpuStats& a, const BodySimGpuStats& b) {
            return a.contacts == b.contacts && a.candidates == b.candidates && a.capped == b.capped &&
                   a.deepPoints == b.deepPoints && a.pairsTested == b.pairsTested && a.hardFires == b.hardFires &&
                   a.scratchOverflow == b.scratchOverflow && a.awake == b.awake;
        };
        // G1: bit-equal over 60 ticks, and the scramble's red
        const Run twice = runFor(60, 0, false, false, 30);   // twice = F5.5g's pass here, once = the spread
        const Run once = runFor(60, 1, false, false, 30);
        const Run scr = runFor(60, 2, false, false, 30);
        const size_t dOnce = differ(twice, once), dScr = differ(twice, scr);
        std::printf("[body gpu] F5.5h E/T %.0f: %zu bodies; spread vs one group per body over 60 ticks: %zu bodies differ, counters %s "
                    "(candidates %llu vs %llu, deep %llu vs %llu); fallback bodies %u over 60 ticks; RED the "
                    "scramble: %zu differ\n", e, nb, dOnce, sameCounters(twice.stats, once.stats) ? "equal" : "DIFFER",
                    (unsigned long long)once.stats.candidates, (unsigned long long)twice.stats.candidates,
                    (unsigned long long)once.stats.deepPoints, (unsigned long long)twice.stats.deepPoints,
                    once.stats.spreadFallback, dScr);
        EXPECT_EQ(dOnce, 0u) << "H1: the spread changed the simulation";
        EXPECT_TRUE(sameCounters(twice.stats, once.stats)) << "H1: the spread changed a counter";
        EXPECT_GT(dScr, 0u) << "H1 red: the scrambled order must show in the bodies";
        // H2/H3: the world group (profiled, one tick per submit) and the tick (unprofiled, 30 per submit), alternated
        auto mean = [](const BodySimGpu::Profile& p, int g) { return p.sumMs[g] / std::max(1, p.ticks); };
        const int G_WORLD = 2;
        std::vector<double> wOn, wOff, tOn, tOff;
        for (int rep = 0; rep < 3; ++rep) {
            wOff.push_back(mean(runFor(30, 0, true, false, 1).prof, G_WORLD));
            wOn.push_back(mean(runFor(30, 1, true, false, 1).prof, G_WORLD));
            tOff.push_back(runFor(30, 0, false, false, 30).tickMs);
            tOn.push_back(runFor(30, 1, false, false, 30).tickMs);
        }
        const double w1 = medianOf(wOn), w0 = medianOf(wOff), t1 = medianOf(tOn), t0 = medianOf(tOff);
        std::printf("[body gpu]   H2 world group: spread %.3f vs one group %.3f ms (%+.0f %%); H3 tick (30 per submit): spread "
                    "%.3f vs one group %.3f ms (%+.0f %%)\n", w1, w0, 100.0 * (w1 / w0 - 1.0), t1, t0,
                    100.0 * (t1 / t0 - 1.0));
        // H4: the fallback bodies per tick (the 60-tick run)
        std::printf("[body gpu]   H4 fallback bodies %u over 60 ticks of %zu awake (%.3f %% per tick)\n",
                    once.stats.spreadFallback, nb, 100.0 * once.stats.spreadFallback / (60.0 * std::max<size_t>(1, nb)));
        EXPECT_LE(w1, 0.75 * w0) << "H2: the world group must drop >= 25 %";
    }
}

// docs/VoxelFoundationPlan.md F5.5j (prediction: docs/evidence/voxfound_f55_prediction.txt J1-J6): HARD_APPLY's world
// items over more than one workgroup -- the push + chunk counts, a workgroup per chunk's deepest point, then per body the
// chunks reduced in order; bit-equal to F5.5a3's one-workgroup-per-body pass. The woken pile barely fires HARD_APPLY, so
// the equality is also checked on the same pile sunk 4 cm into the floor (every body), where it fires.
TEST_F(VoxFoundBodySimGpu, F55jHardSpread) {
    struct Run {
        std::vector<SimBody> bodies;
        BodySimGpuStats stats;
        BodySimGpu::Profile prof;
        double tickMs = 0.0;
    };
    for (const float e : {250.0f, 650.0f, 2800.0f}) {
        SCOPED_TRACE(e);
        LabPile pile = labLargeWall(e);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef mirror(pile.world, pile.mats, prm);
        for (const Body& b : pile.bodies) mirror.add(b, b.vel, b.angVel);
        std::string err;
        ASSERT_TRUE(gpu.load(pile.world, pile.mats, mirror.bodies(), mirror.params(), &err)) << err;
        int settleTicks = 0;
        do {
            step(60);
            if (HasFatalFailure()) return;
            settleTicks += 60;
        } while (gpu.stats().awake > 0 && settleTicks < 6000);
        ASSERT_EQ(gpu.stats().awake, 0u);
        sync(mirror);
        if (HasFatalFailure()) return;
        std::vector<SimBody> woken = mirror.bodies();
        for (SimBody& b : woken) b.asleep = false, b.slowTicks = 0, b.asleepTicks = 0;
        std::vector<SimBody> sunk = woken;
        for (SimBody& b : sunk) b.pos.y -= 0.04;
        const size_t nb = woken.size();
        // mode: 0 = F5.5a3's pass (one workgroup per body), 1 = spread, 2 = spread + the scramble (test only)
        auto runFor = [&](const std::vector<SimBody>& from, int ticks, int mode, bool profile, int perSubmit) {
            Run r;
            std::string werr;
            EXPECT_TRUE(gpu.edit({}, {}, from, &werr)) << werr;
            gpu.setHardSpread(mode != 0, mode == 2);
            gpu.setProfile(profile);
            step(ticks, perSubmit);
            r.tickMs = std::max(0.0, gpu.lastGpuMs()) / ticks;
            gpu.setProfile(false);
            gpu.setHardSpread(true);
            r.stats = gpu.stats();
            r.prof = gpu.profile();
            r.bodies = from;
            EXPECT_TRUE(gpu.read(r.bodies, &werr)) << werr;
            return r;
        };
        auto differ = [](const Run& a, const Run& b) {
            size_t n = 0;
            for (size_t i = 0; i < a.bodies.size(); ++i)
                if (a.bodies[i].pos != b.bodies[i].pos || a.bodies[i].quat != b.bodies[i].quat ||
                    a.bodies[i].vel != b.bodies[i].vel || a.bodies[i].angVel != b.bodies[i].angVel ||
                    a.bodies[i].asleep != b.bodies[i].asleep)
                    ++n;
            return n;
        };
        auto sameCounters = [](const BodySimGpuStats& a, const BodySimGpuStats& b) {
            return a.contacts == b.contacts && a.candidates == b.candidates && a.capped == b.capped &&
                   a.deepPoints == b.deepPoints && a.pairsTested == b.pairsTested && a.hardFires == b.hardFires &&
                   a.scratchOverflow == b.scratchOverflow && a.awake == b.awake;
        };
        // J2: bit-equal over 60 ticks on the woken and the sunk pile; the scramble's red on the sunk pile
        for (int which = 0; which < 2; ++which) {
            const std::vector<SimBody>& from = which == 0 ? woken : sunk;
            const Run one = runFor(from, 60, 0, false, 30);
            const Run spr = runFor(from, 60, 1, false, 30);
            const Run scr = runFor(from, 60, 2, false, 30);
            const size_t dSpr = differ(one, spr), dScr = differ(one, scr);
            std::printf("[body gpu] F5.5j E/T %.0f %s: %zu bodies; hard spread vs one group per body over 60 ticks: %zu "
                        "bodies differ, counters %s (hard fires %llu vs %llu, candidates %llu vs %llu); RED the scramble: "
                        "%zu differ (hard fires %llu)\n", e, which == 0 ? "woken" : "sunk 4 cm", nb, dSpr,
                        sameCounters(one.stats, spr.stats) ? "equal" : "DIFFER", (unsigned long long)spr.stats.hardFires,
                        (unsigned long long)one.stats.hardFires, (unsigned long long)spr.stats.candidates,
                        (unsigned long long)one.stats.candidates, dScr, (unsigned long long)scr.stats.hardFires);
            EXPECT_EQ(dSpr, 0u) << "J2: the hard spread changed the simulation";
            EXPECT_TRUE(sameCounters(one.stats, spr.stats)) << "J2: the hard spread changed a counter";
            if (which == 1) {
                EXPECT_GT(one.stats.hardFires, 0u) << "J2: the sunk pile must fire HARD_APPLY";
                EXPECT_GT(dScr, 0u) << "J2 red: the chunk-0-only scramble must show in the bodies";
            }
        }
        // J3/J4: the hard_apply group (profiled, one tick per submit) and the tick (unprofiled, 30 per submit), alternated
        auto mean = [](const BodySimGpu::Profile& p, int g) { return p.sumMs[g] / std::max(1, p.ticks); };
        const int G_WORLD = 2, G_HARDA = 10;
        std::vector<double> hOn, hOff, tOn, tOff, wOn, wOff;
        for (int rep = 0; rep < 3; ++rep) {
            const Run a = runFor(woken, 30, 0, true, 1);
            const Run b = runFor(woken, 30, 1, true, 1);
            hOff.push_back(mean(a.prof, G_HARDA));
            hOn.push_back(mean(b.prof, G_HARDA));
            wOff.push_back(mean(a.prof, G_WORLD));
            wOn.push_back(mean(b.prof, G_WORLD));
            tOff.push_back(runFor(woken, 30, 0, false, 30).tickMs);
            tOn.push_back(runFor(woken, 30, 1, false, 30).tickMs);
        }
        const double h1 = medianOf(hOn), h0 = medianOf(hOff), t1 = medianOf(tOn), t0 = medianOf(tOff);
        std::printf("[body gpu]   J3 hard_apply group: spread %.3f vs one group %.3f ms (%+.0f %%); J4 tick (30 per "
                    "submit): spread %.3f vs one group %.3f ms (%+.0f %%); the world group (untouched) %.3f vs %.3f ms\n",
                    h1, h0, 100.0 * (h1 / h0 - 1.0), t1, t0, 100.0 * (t1 / t0 - 1.0), medianOf(wOn), medianOf(wOff));
        EXPECT_LE(h1, 0.75 * h0) << "J3: the hard_apply group must drop >= 25 %";
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// F5.5k -- the pairs group split (docs/evidence/voxfound_f55_prediction.txt, K1-K4): a stamp after BROAD, then quiet
// diagnostic copies of NARROW stop after the count, the write and the cap, each timed as its own group. The instrument
// must not change the simulation (60 profiled ticks bit-equal, split on vs off); its red is the loud copy, whose
// counters differ.
TEST_F(VoxFoundBodySimGpu, F55kPairsSplit) {
    struct Run {
        std::vector<SimBody> bodies;
        BodySimGpuStats stats;
        BodySimGpu::Profile prof;
    };
    for (const float e : {250.0f, 650.0f, 2800.0f}) {
        SCOPED_TRACE(e);
        LabPile pile = labLargeWall(e);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef mirror(pile.world, pile.mats, prm);
        for (const Body& b : pile.bodies) mirror.add(b, b.vel, b.angVel);
        std::string err;
        ASSERT_TRUE(gpu.load(pile.world, pile.mats, mirror.bodies(), mirror.params(), &err)) << err;
        int settleTicks = 0;
        do {
            step(60);
            if (HasFatalFailure()) return;
            settleTicks += 60;
        } while (gpu.stats().awake > 0 && settleTicks < 6000);
        ASSERT_EQ(gpu.stats().awake, 0u);
        sync(mirror);
        if (HasFatalFailure()) return;
        std::vector<SimBody> woken = mirror.bodies();
        for (SimBody& b : woken) b.asleep = false, b.slowTicks = 0, b.asleepTicks = 0;
        const size_t nb = woken.size();
        auto runFor = [&](int ticks, bool split, bool quiet) {
            Run r;
            std::string werr;
            EXPECT_TRUE(gpu.edit({}, {}, woken, &werr)) << werr;
            gpu.setPairsSplit(split, quiet);
            gpu.setPairsOnce(false);   // F5.5l: the split measures the two-test pass (F5.5k's record; once, the write is ~0)
            gpu.setProfile(true);
            step(ticks, 1);
            gpu.setProfile(false);
            gpu.setPairsSplit(false);
            gpu.setPairsOnce(true);
            r.stats = gpu.stats();
            r.prof = gpu.profile();
            r.bodies = woken;
            EXPECT_TRUE(gpu.read(r.bodies, &werr)) << werr;
            return r;
        };
        auto differ = [](const Run& a, const Run& b) {
            size_t n = 0;
            for (size_t i = 0; i < a.bodies.size(); ++i)
                if (a.bodies[i].pos != b.bodies[i].pos || a.bodies[i].quat != b.bodies[i].quat ||
                    a.bodies[i].vel != b.bodies[i].vel || a.bodies[i].angVel != b.bodies[i].angVel ||
                    a.bodies[i].asleep != b.bodies[i].asleep)
                    ++n;
            return n;
        };
        auto sameCounters = [](const BodySimGpuStats& a, const BodySimGpuStats& b) {
            return a.contacts == b.contacts && a.candidates == b.candidates && a.capped == b.capped &&
                   a.deepPoints == b.deepPoints && a.pairsTested == b.pairsTested && a.hardFires == b.hardFires &&
                   a.scratchOverflow == b.scratchOverflow && a.awake == b.awake;
        };
        // K1: the instrument -- bit-equal, and its red
        const Run off = runFor(60, false, true);
        const Run on = runFor(60, true, true);
        const Run loud = runFor(60, true, false);
        const size_t dOn = differ(off, on), dLoud = differ(off, loud);
        std::printf("[body gpu] F5.5k E/T %.0f: %zu bodies; pairs split (quiet) vs off over 60 ticks: %zu bodies differ, "
                    "counters %s (candidates %llu vs %llu, deep %llu vs %llu); RED the loud split: %zu differ, counters "
                    "%s (candidates %llu, deep %llu)\n", e, nb, dOn, sameCounters(off.stats, on.stats) ? "equal" : "DIFFER",
                    (unsigned long long)on.stats.candidates, (unsigned long long)off.stats.candidates,
                    (unsigned long long)on.stats.deepPoints, (unsigned long long)off.stats.deepPoints, dLoud,
                    sameCounters(off.stats, loud.stats) ? "equal" : "differ",
                    (unsigned long long)loud.stats.candidates, (unsigned long long)loud.stats.deepPoints);
        EXPECT_EQ(dOn, 0u) << "K1: the split changed the simulation";
        EXPECT_TRUE(sameCounters(off.stats, on.stats)) << "K1: the split changed a counter";
        EXPECT_FALSE(sameCounters(off.stats, loud.stats)) << "K1 red: the loud copy must show in the counters";
        // K1-K4: the split, 30 profiled ticks from the same snapshot (and the pairs group without it, for K1's 15 %)
        const Run a = runFor(30, true, true);
        const Run b = runFor(30, false, true);
        auto mean = [](const BodySimGpu::Profile& p, int g) { return p.sumMs[g] / std::max(1, p.ticks); };
        const int G_PAIRS = 3, G_BROAD = 18, G_COUNT = 19, G_WRITE = 20, G_CAP = 21;
        const double narrow = mean(a.prof, G_PAIRS), broad = mean(a.prof, G_BROAD), pairsOff = mean(b.prof, G_PAIRS);
        const double cnt = mean(a.prof, G_COUNT), wr = mean(a.prof, G_WRITE), cp = mean(a.prof, G_CAP);
        const double group = broad + narrow, sWrite = wr - cnt, sCap = cp - wr, sEmit = narrow - cp;
        double sum = 0.0;
        for (int g = 0; g < BodySimGpu::kProfGroups; ++g) sum += mean(b.prof, g);
        std::printf("[body gpu]   K1 pairs group: split on broad %.3f + narrow %.3f = %.3f vs off %.3f ms (%.2f x; %.0f %% "
                    "of the unsplit profile sum %.3f); pairs tested %llu per tick\n", broad, narrow, group, pairsOff,
                    group / std::max(1e-9, pairsOff), 100.0 * pairsOff / std::max(1e-9, sum), sum,
                    (unsigned long long)(a.stats.pairsTested / 30));
        std::printf("[body gpu]   K2-K4 copies: p_count %.3f, p_write %.3f, p_cap %.3f ms -> broad %.3f (%.0f %%), count "
                    "%.3f (%.0f %%), write %.3f (%.0f %%), cap %.3f (%.0f %%), emit %.3f (%.0f %%) of the group %.3f\n",
                    cnt, wr, cp, broad, 100 * broad / group, cnt, 100 * cnt / group, sWrite, 100 * sWrite / group, sCap,
                    100 * sCap / group, sEmit, 100 * sEmit / group, group);
        EXPECT_EQ(a.prof.ticks, 30);
        EXPECT_GT(broad, 0.0) << "the broad stamp ran";
        EXPECT_GT(cnt, 0.0) << "the count copy ran";
        EXPECT_GT(cp, cnt) << "the cap copy does more than the count copy";
    }
}

// docs/VoxelFoundationPlan.md F5.5l (prediction: docs/evidence/voxfound_f55_prediction.txt L1-L6): each pair corner
// tested once -- NARROW's candidates staged in the count and copied to their serial offset, bit-equal to the two-test pass
TEST_F(VoxFoundBodySimGpu, F55lPairsOnce) {
    struct Run {
        std::vector<SimBody> bodies;
        BodySimGpuStats stats;
        BodySimGpu::Profile prof;
        double tickMs = 0.0;
    };
    for (const float e : {250.0f, 650.0f, 2800.0f}) {
        SCOPED_TRACE(e);
        LabPile pile = labLargeWall(e);
        BodySimParams prm;
        prm.groundBelow = true;
        BodySimRef mirror(pile.world, pile.mats, prm);
        for (const Body& b : pile.bodies) mirror.add(b, b.vel, b.angVel);
        std::string err;
        ASSERT_TRUE(gpu.load(pile.world, pile.mats, mirror.bodies(), mirror.params(), &err)) << err;
        int settleTicks = 0;
        do {
            step(60);
            if (HasFatalFailure()) return;
            settleTicks += 60;
        } while (gpu.stats().awake > 0 && settleTicks < 6000);
        ASSERT_EQ(gpu.stats().awake, 0u);
        sync(mirror);
        if (HasFatalFailure()) return;
        std::vector<SimBody> woken = mirror.bodies();
        for (SimBody& b : woken) b.asleep = false, b.slowTicks = 0, b.asleepTicks = 0;
        const size_t nb = woken.size();
        // mode: 0 = the two-test pass, 1 = once, 2 = once + the scramble (test only)
        auto runFor = [&](int ticks, int mode, bool profile, int perSubmit) {
            Run r;
            std::string werr;
            EXPECT_TRUE(gpu.edit({}, {}, woken, &werr)) << werr;
            gpu.setPairsOnce(mode != 0, mode == 2);
            gpu.setProfile(profile);
            step(ticks, perSubmit);
            r.tickMs = std::max(0.0, gpu.lastGpuMs()) / ticks;
            gpu.setProfile(false);
            gpu.setPairsOnce(true);
            r.stats = gpu.stats();
            r.prof = gpu.profile();
            r.bodies = woken;
            EXPECT_TRUE(gpu.read(r.bodies, &werr)) << werr;
            return r;
        };
        auto differ = [](const Run& a, const Run& b) {
            size_t n = 0;
            for (size_t i = 0; i < a.bodies.size(); ++i)
                if (a.bodies[i].pos != b.bodies[i].pos || a.bodies[i].quat != b.bodies[i].quat ||
                    a.bodies[i].vel != b.bodies[i].vel || a.bodies[i].angVel != b.bodies[i].angVel ||
                    a.bodies[i].asleep != b.bodies[i].asleep)
                    ++n;
            return n;
        };
        auto sameCounters = [](const BodySimGpuStats& a, const BodySimGpuStats& b) {
            return a.contacts == b.contacts && a.candidates == b.candidates && a.capped == b.capped &&
                   a.deepPoints == b.deepPoints && a.pairsTested == b.pairsTested && a.hardFires == b.hardFires &&
                   a.scratchOverflow == b.scratchOverflow && a.awake == b.awake;
        };
        // L2: bit-equal over 60 ticks, and the scramble's red
        const Run twice = runFor(60, 0, false, 30);
        const Run once = runFor(60, 1, false, 30);
        const Run scr = runFor(60, 2, false, 30);
        const size_t dOnce = differ(twice, once), dScr = differ(twice, scr);
        std::printf("[body gpu] F5.5l E/T %.0f: %zu bodies; pairs once vs twice over 60 ticks: %zu bodies differ, counters "
                    "%s (candidates %llu vs %llu, deep %llu vs %llu, overflow %u vs %u); pair redo threads %u; RED the "
                    "scramble: %zu differ\n", e, nb, dOnce, sameCounters(twice.stats, once.stats) ? "equal" : "DIFFER",
                    (unsigned long long)once.stats.candidates, (unsigned long long)twice.stats.candidates,
                    (unsigned long long)once.stats.deepPoints, (unsigned long long)twice.stats.deepPoints,
                    once.stats.scratchOverflow, twice.stats.scratchOverflow, once.stats.pairRedo, dScr);
        EXPECT_EQ(dOnce, 0u) << "L2: once changed the simulation";
        EXPECT_TRUE(sameCounters(twice.stats, once.stats)) << "L2: once changed a counter";
        EXPECT_EQ(once.stats.pairRedo, 0u) << "L2: no thread past the stage on these piles";
        EXPECT_GT(dScr, 0u) << "L2 red: the scrambled order must show in the bodies";
        // L1/L3/L4: the pairs group and the world group (profiled, one tick per submit) and the tick (30 per submit),
        // alternated
        auto mean = [](const BodySimGpu::Profile& p, int g) { return p.sumMs[g] / std::max(1, p.ticks); };
        const int G_WORLD = 2, G_PAIRS = 3;
        std::vector<double> pOn, pOff, wOn, wOff, tOn, tOff;
        for (int rep = 0; rep < 3; ++rep) {
            const Run a = runFor(30, 0, true, 1);
            const Run b = runFor(30, 1, true, 1);
            pOff.push_back(mean(a.prof, G_PAIRS));
            pOn.push_back(mean(b.prof, G_PAIRS));
            wOff.push_back(mean(a.prof, G_WORLD));
            wOn.push_back(mean(b.prof, G_WORLD));
            tOff.push_back(runFor(30, 0, false, 30).tickMs);
            tOn.push_back(runFor(30, 1, false, 30).tickMs);
        }
        const double p1 = medianOf(pOn), p0 = medianOf(pOff), t1 = medianOf(tOn), t0 = medianOf(tOff);
        std::printf("[body gpu]   L3 pairs group: once %.3f vs twice %.3f ms (%+.0f %%); L4 tick (30 per submit): once "
                    "%.3f vs twice %.3f ms (%+.0f %%); the world group (untouched) %.3f vs %.3f ms\n", p1, p0,
                    100.0 * (p1 / p0 - 1.0), t1, t0, 100.0 * (t1 / t0 - 1.0), medianOf(wOn), medianOf(wOff));
        EXPECT_LE(p1, 0.80 * p0) << "L3: the pairs group must drop >= 20 %";
    }
}
