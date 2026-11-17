// VoxFoundFractureGpuTest -- docs/VoxelFoundationPlan.md F4.3 (the F4 row: "Then the GPU port, equal to the
// reference"; §8: GPU = CPU reference, GPU ms per break at 10 / 100 / 1,000 pieces).
//
// FractureGpu (shaders/voxfound_fracture.comp) is accepted only on EXACT equality with fractureReference in every
// channel: cut, damage, piece, pieceAnchored, pieceSize, pieceKind. Needs a Vulkan device: the fixture GTEST_SKIPs
// without one (never a pass); a failing init on a device is a FAILURE.
//
// Cases: every §8 rig at the F4.1 energies; 300 random hits (the F4.1 stress generator) plus each repeated on its
// damaged grid; a large stone wall (256 x 128 x 4 voxels, base fixed; hand-built shape, like the §8 rigs) swept in
// energy for the cost per break against loose pieces. Prediction: docs/evidence/voxfound_f43_prediction.txt.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "core/voxfound/FractureGpu.h"
#include "core/voxfound/FractureLab.h"

using namespace Phyxel;
using namespace Phyxel::Core::VoxFound;
using Phyxel::Testing::VulkanTestFixture;

namespace {

class VoxFoundFractureGpu : public VulkanTestFixture {
protected:
    void SetUp() override {
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable() || device == VK_NULL_HANDLE) GTEST_SKIP() << "no Vulkan device";
        std::string err;
        ASSERT_TRUE(gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    }
    void TearDown() override {
        gpu.shutdown();
        VulkanTestFixture::TearDown();
    }
    // GPU vs CPU reference on one hit; returns true when every channel is equal.
    bool same(const VoxelGrid& g, const Hit& hit, const std::vector<FractureMaterial>& mats, const std::string& name,
              FractureResult* gpuOut = nullptr) {
        const FractureResult cpu = fractureReference(g, hit, mats);
        FractureResult gr;
        std::string err;
        const bool ok = gpu.fracture(g, hit, mats, gr, &err);
        EXPECT_TRUE(ok) << name << ": " << err;
        if (!ok) return false;
        size_t dCut = 0, dDmg = 0, dPiece = 0, first = SIZE_MAX;
        for (size_t i = 0; i < g.size(); ++i) {
            const bool a = cpu.cut[i] != gr.cut[i], b = cpu.damage[i] != gr.damage[i], c = cpu.piece[i] != gr.piece[i];
            dCut += a; dDmg += b; dPiece += c;
            if (first == SIZE_MAX && (a || b || c)) first = i;
        }
        const bool tables = cpu.pieceAnchored == gr.pieceAnchored && cpu.pieceSize == gr.pieceSize &&
                            cpu.pieceKind == gr.pieceKind;
        std::string at;
        if (first != SIZE_MAX) {
            const glm::ivec3 p = g.pos(first);
            char buf[200];
            std::snprintf(buf, sizeof(buf), "; first (%d,%d,%d): cpu cut %u dmg %u piece %u / gpu %u %u %u", p.x, p.y,
                          p.z, cpu.cut[first], cpu.damage[first], cpu.piece[first], gr.cut[first], gr.damage[first],
                          gr.piece[first]);
            at = buf;
        }
        EXPECT_EQ(dCut + dDmg + dPiece, 0u) << name << " cut/damage/piece differ: " << dCut << "/" << dDmg << "/"
                                            << dPiece << at;
        EXPECT_TRUE(tables) << name << " piece tables differ (" << cpu.pieceSize.size() - 1 << " vs "
                            << gr.pieceSize.size() - 1 << " pieces)";
        if (gpuOut) *gpuOut = std::move(gr);
        return dCut + dDmg + dPiece == 0 && tables;
    }
    FractureGpu gpu;
};

Rig rigByIndex(int k) {
    switch (k) {
        case 0: return rigStoneWall({12, 8, 0}, 1.0f);
        case 1: return rigBrickWall(1.0f);
        case 2: return rigBeam(1.0f);
        case 3: return rigGlassPane(1.0f);
        case 4: return rigColumnLintel(1.0f);
        default: return rigChair(1.0f);
    }
}

}  // namespace

TEST_F(VoxFoundFractureGpu, EveryLabRigEqualsTheReference) {
    std::vector<Rig> rigs;
    for (float e : {0.5f, 1.5f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f, 20.0f, 30.0f, 50.0f, 80.0f})
        rigs.push_back(rigStoneWall({12, 8, 0}, e));
    rigs.push_back(rigStoneWall({22, 14, 0}, 5.0f));
    rigs.push_back(rigBrickWall(8.0f));
    rigs.push_back(rigBrickWall(30.0f));
    rigs.push_back(rigBeam(8.0f));
    rigs.push_back(rigGlassPane(20.0f));
    rigs.push_back(rigColumnLintel(30.0f));
    rigs.push_back(rigChair(8.0f));
    int equal = 0;
    for (const Rig& r : rigs) {
        if (same(r.grid, r.hit, r.mats, r.name + " E " + std::to_string(r.hit.energy))) ++equal;
        std::printf("[fracture gpu] %-12s E %7.1f: %.3f ms device, %d rounds, %d submits\n", r.name.c_str(),
                    r.hit.energy, gpu.lastGpuMs(), gpu.lastRounds(), gpu.lastSubmits());
    }
    std::printf("[fracture gpu] rigs: %d / %zu equal in every channel\n", equal, rigs.size());
}

TEST_F(VoxFoundFractureGpu, RandomHitsAndRepeatedBlowsEqualTheReference) {
    // the F4.1 stress generator (VoxFoundFractureLabTest), same seed
    std::mt19937_64 rng(4101);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    int equal = 0, total = 0;
    for (int h = 0; h < 300; ++h) {
        Rig rig = rigByIndex(h % 6);
        size_t i = 0;
        do { i = size_t(U(rng) * double(rig.grid.size())) % rig.grid.size(); } while (!rig.grid.mat[i]);
        const int axis = int(U(rng) * 3) % 3;
        glm::vec3 dir(0.0f);
        dir[axis] = U(rng) < 0.5 ? -1.0f : 1.0f;
        const double eot = 0.3 * std::pow(100.0 / 0.3, U(rng));
        rig.hit = {glm::vec3(rig.grid.pos(i)) + 0.5f, dir, float(eot) * rig.mats[rig.grid.mat[i]].toughness, rng()};
        const std::string name = rig.name + " hit " + std::to_string(h);
        FractureResult first;
        ++total;
        if (same(rig.grid, rig.hit, rig.mats, name, &first)) ++equal;
        // the same blow on the damaged grid (progressive damage reads the damage channel)
        VoxelGrid g2 = rig.grid;
        applyDamage(g2, first);
        ++total;
        if (same(g2, rig.hit, rig.mats, name + " repeated")) ++equal;
    }
    std::printf("[fracture gpu] random: %d / %d hits equal in every channel\n", equal, total);
}

TEST_F(VoxFoundFractureGpu, CostPerBreakOnALargeWall) {
    // a 32 x 16 x 0.5 m stone wall at 1/8 m (hand-built shape), base fixed, hit at its centre
    Rig rig;
    rig.name = "large_wall";
    rig.grid.resize({256, 128, 4});
    rig.grid.voxelM = 0.125f;
    rig.mats = labMaterials();
    for (int x = 0; x < 256; ++x)
        for (int y = 0; y < 128; ++y)
            for (int z = 0; z < 4; ++z) rig.grid.set({x, y, z}, kStone, 0, y == 0);
    std::printf("[fracture gpu] large wall 256 x 128 x 4 = %zu voxels\n", rig.grid.solidCount());
    for (float e : {8.0f, 20.0f, 50.0f, 150.0f, 400.0f, 1000.0f, 2500.0f}) {
        rig.hit = {{128, 64, 0}, {0, 0, 1}, e * rig.mats[kStone].toughness, 777};
        const auto t0 = std::chrono::steady_clock::now();
        const FractureResult cpu = fractureReference(rig.grid, rig.hit, rig.mats);
        const double cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        FractureResult gr;
        std::string err;
        double gpuMs = 1e30, hostMs = 1e30;
        int rounds = 0;
        for (int rep = 0; rep < 3; ++rep) {
            const auto t1 = std::chrono::steady_clock::now();
            ASSERT_TRUE(gpu.fracture(rig.grid, rig.hit, rig.mats, gr, &err)) << err;
            hostMs = std::min(hostMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
            gpuMs = std::min(gpuMs, gpu.lastGpuMs());
            rounds = gpu.lastRounds();
        }
        EXPECT_EQ(cpu.cut, gr.cut) << "E/T " << e;
        EXPECT_EQ(cpu.damage, gr.damage) << "E/T " << e;
        EXPECT_EQ(cpu.piece, gr.piece) << "E/T " << e;
        EXPECT_EQ(cpu.pieceKind, gr.pieceKind) << "E/T " << e;
        size_t loose = 0, bodies = 0;
        for (size_t id = 1; id < cpu.pieceSize.size(); ++id)
            if (!cpu.pieceAnchored[id]) { ++loose; bodies += cpu.pieceKind[id] == PieceKind::Body; }
        size_t cuts = 0;
        for (uint8_t c : cpu.cut) cuts += size_t((c & 1) + ((c >> 1) & 1) + ((c >> 2) & 1));
        std::printf("[fracture gpu] large wall E/T %6.0f: %6zu cuts, %5zu loose pieces (%4zu bodies) | GPU %.3f ms "
                    "device (%d rounds), %.2f ms host | CPU reference %.2f ms\n",
                    e, cuts, loose, bodies, gpuMs, rounds, hostMs, cpuMs);
    }
}
