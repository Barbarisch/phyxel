// VoxFoundLodBuilderGpuTest — docs/VoxelFoundationPlan.md F2.14e (§4.6 item 7 continued).
//
// The GPU level build (LodBuilderGpu, shaders/voxfound_lod.comp) is accepted only on EXACT equality with the CPU
// reference lodBase + lodDown, every channel (mat, count, surf, surfMat, volMat), every level. Needs a Vulkan
// device: the fixture GTEST_SKIPs without one (never a pass); a failing init on a device is a FAILURE.
//
// Cases: random content (several materials, so votes tie and the neighbour step runs) at a few sizes and levels;
// a thin surface layer over a solid body (the neighbour step decides every top cell); the engine forest with flora at
// the engine's area size (a 32 m chunk + its 1 m margin = 272^3 voxels), levels 1..3; refusals.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "core/WorldGenerator.h"
#include "core/voxfound/LodBuilderGpu.h"
#include "core/voxfound/Mesher.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel;
using namespace Phyxel::Core;
using namespace Phyxel::Core::VoxFound;
using Phyxel::Testing::VulkanTestFixture;

namespace {

class VoxFoundLodBuilderGpu : public VulkanTestFixture {
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
    // GPU levels 1..n vs CPU lodBase + lodDown; returns the number of cells compared.
    size_t same(const VoxelGrid& g, int levels, const std::string& name) {
        std::vector<LodGrid> out;
        std::string err;
        EXPECT_TRUE(gpu.build(g, levels, out, &err)) << name << ": " << err;
        EXPECT_EQ(out.size(), size_t(levels)) << name;
        LodGrid cpu = lodBase(g);
        size_t cells = 0;
        for (int k = 1; k <= levels && size_t(k) <= out.size(); ++k) {
            cpu = lodDown(cpu);
            const LodGrid& gl = out[size_t(k - 1)];
            EXPECT_EQ(gl.level, k) << name;
            EXPECT_EQ(gl.grid.dim, cpu.grid.dim) << name << " level " << k;
            if (gl.grid.dim != cpu.grid.dim) continue;
            size_t diff[5] = {}, first = SIZE_MAX;
            for (size_t i = 0; i < cpu.grid.size(); ++i) {
                const bool d[5] = {gl.grid.mat[i] != cpu.grid.mat[i], gl.count[i] != cpu.count[i],
                                   gl.surf[i] != cpu.surf[i], gl.surfMat[i] != cpu.surfMat[i],
                                   gl.volMat[i] != cpu.volMat[i]};
                for (int c = 0; c < 5; ++c) diff[c] += d[c];
                if (first == SIZE_MAX && (d[0] || d[1] || d[2] || d[3] || d[4])) first = i;
            }
            std::string at;
            if (first != SIZE_MAX) {
                const glm::ivec3 p = cpu.grid.pos(first);
                char b[200];
                std::snprintf(b, sizeof(b), "; first (%d,%d,%d): cpu mat %d count %u surf %u sm %d vm %d / gpu %d %u %u %d %d",
                              p.x, p.y, p.z, cpu.grid.mat[first], cpu.count[first], cpu.surf[first], cpu.surfMat[first],
                              cpu.volMat[first], gl.grid.mat[first], gl.count[first], gl.surf[first],
                              gl.surfMat[first], gl.volMat[first]);
                at = b;
            }
            EXPECT_EQ(diff[0] + diff[1] + diff[2] + diff[3] + diff[4], 0u)
                << name << " level " << k << " mat/count/surf/surfMat/volMat differ: " << diff[0] << "/" << diff[1]
                << "/" << diff[2] << "/" << diff[3] << "/" << diff[4] << at;
            cells += cpu.grid.size();
        }
        return cells;
    }
    LodBuilderGpu gpu;
};

}  // namespace

TEST_F(VoxFoundLodBuilderGpu, RandomContentEqualsTheReference) {
    std::mt19937 rng(42);
    struct Case { glm::ivec3 dim; int levels; float fill; int mats; };
    for (const Case c : {Case{{16, 16, 16}, 4, 0.5f, 3}, Case{{32, 8, 24}, 3, 0.45f, 5}, Case{{2, 2, 2}, 1, 0.5f, 2},
                         Case{{64, 32, 48}, 3, 0.55f, 9}, Case{{40, 40, 40}, 3, 0.5f, 2}}) {
        VoxelGrid g;
        g.resize(c.dim);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        std::uniform_int_distribution<int> m(1, c.mats);
        for (uint8_t& v : g.mat) v = u(rng) < c.fill ? uint8_t(m(rng)) : 0;
        const std::string name = "random " + std::to_string(c.dim.x) + "x" + std::to_string(c.dim.y) + "x" +
                                 std::to_string(c.dim.z);
        EXPECT_GT(same(g, c.levels, name), 0u);
    }
}

TEST_F(VoxFoundLodBuilderGpu, ThinSurfaceTakesTheNeighbourStep) {
    // A dirt (1) body with ONE grass (2) layer on top and some stone (3) tufts. Grass at y 21: the level-3 cell
    // y 16..23 is 6/8 solid and covers the exposed grass itself. Grass at y 24: the cell y 16..23 is all dirt and
    // touches no air (the grass covers it), the cell above holds only the 1/8-thick grass layer (air by count), so
    // the solid cell's material comes from the neighbour step.
    for (int top : {21, 24}) {
        VoxelGrid g;
        g.resize({32, 48, 32});
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) {
                for (int y = 0; y < top; ++y) g.set({x, y, z}, 1);
                g.set({x, top, z}, 2);
                if ((x * 7 + z * 3) % 11 == 0) g.set({x, top + 1, z}, 3);   // some stone tufts
            }
        same(g, 3, "thin surface at y " + std::to_string(top));
    }
}

TEST_F(VoxFoundLodBuilderGpu, EngineForestAreaEqualsTheReference) {
    namespace fs = std::filesystem;
    fs::path root = fs::current_path();
    for (int i = 0; i < 6 && !fs::exists(root / "resources" / "templates_g8"); ++i) root = root.parent_path();
    ASSERT_TRUE(fs::exists(root / "resources" / "templates_g8")) << "repo root not found from " << fs::current_path();
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    gen.loadBiomes((root / "resources" / "biomes.json").string());
    // the engine's area for chunk (255, 0, 3): the chunk plus a 1 m margin (forEachRegionChunk)
    const glm::ivec3 c(255, 0, 3);
    VoxelArea a = buildTerrainArea(gen, c * 32 - 1, glm::ivec3(34));
    const FloraStampStats flora = stampFlora(a, gen, (root / "resources" / "templates_g8" / "nature").string(), 24);
    ASSERT_EQ(a.grid.dim, glm::ivec3(272));
    EXPECT_GT(a.grid.solidCount(), 0u);
    const size_t cells = same(a.grid, 3, "forest chunk area");
    std::printf("[lodbuilder] forest chunk (255,0,3) area 272^3, %zu plants stamped: %zu cells compared, GPU %.3f ms\n",
                flora.stamped, cells, gpu.lastGpuMs());
}

TEST_F(VoxFoundLodBuilderGpu, RefusesWhatItCannotBuild) {
    VoxelGrid g;
    g.resize({12, 8, 8});
    std::vector<LodGrid> out;
    std::string err;
    EXPECT_FALSE(gpu.build(g, 3, out, &err)) << "12 is not divisible by 8";
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(gpu.build(g, 0, out, &err));
    EXPECT_FALSE(gpu.build(g, LodBuilderGpu::kMaxLevels + 1, out, &err));
    g.resize({LodBuilderGpu::kMaxSide + 2, 2, 2});
    EXPECT_FALSE(gpu.build(g, 1, out, &err));
    g.resize({8, 8, 8});
    EXPECT_TRUE(gpu.build(g, 3, out, &err)) << err;   // all air is fine
}
