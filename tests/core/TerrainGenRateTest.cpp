// docs/VoxelFoundationPlan.md F7.9a -- the generation rate levers are exact: buildTerrainArea (one generator lookup per
// metre key) equals the per-voxel reference; forEachRegionChunk on worker threads hands out the same chunks, in the same
// order, with the same palette, as one thread. Timings are printed (the live gate measures the rate).
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>

#include "core/WorldGenerator.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel;
using namespace Phyxel::Core::VoxFound;

namespace {
std::string repoRoot() {
    namespace fs = std::filesystem;
    fs::path p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        if (fs::exists(p / "resources" / "biomes.json") && fs::exists(p / "resources" / "templates_g8")) return p.string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}
double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
}  // namespace

TEST(VoxFoundTerrainGenRate, FastAreaEqualsReference) {
    struct P { unsigned seed; glm::ivec3 o; };
    const P places[] = {{7u, {8126, -6, 63}}, {7u, {512, -6, -256}}, {12345u, {-1024, 0, 768}}};
    for (const P& p : places) {
        WorldGenerator gen(WorldGenerator::GenerationType::Perlin, p.seed);
        const float h = gen.sampleSurface(p.o.x + 17, p.o.z + 17).bedHeight;
        const glm::ivec3 o(p.o.x, int(std::floor(h)) - 20, p.o.z);
        auto t0 = std::chrono::steady_clock::now();
        const VoxelArea ref = buildTerrainAreaReference(gen, o, glm::ivec3(34));
        const double refMs = msSince(t0);
        t0 = std::chrono::steady_clock::now();
        const VoxelArea fast = buildTerrainArea(gen, o, glm::ivec3(34));
        const double fastMs = msSince(t0);
        std::printf("[f79a] seed %u at (%d,%d,%d): reference %.1f ms, fast %.1f ms\n", p.seed, o.x, o.y, o.z, refMs, fastMs);
        ASSERT_EQ(fast.palette, ref.palette);
        ASSERT_EQ(fast.grid.dim, ref.grid.dim);
        ASSERT_TRUE(fast.grid.mat == ref.grid.mat) << "voxels differ";
    }
}

TEST(VoxFoundTerrainGenRate, WorkerThreadsEqualOneThread) {
    const std::string root = repoRoot();
    const std::string flora = root.empty() ? std::string() : root + "/resources/templates_g8/nature";
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    struct Got { glm::ivec3 chunk; std::vector<uint8_t> mat; std::vector<std::string> pal; };
    auto run = [&](int threads, double& ms) {
        std::vector<Got> got;
        const auto t0 = std::chrono::steady_clock::now();
        forEachRegionChunk(gen, {254, 0, 3}, {255, 0, 4}, root.empty() ? nullptr : &flora,
                           [&](const RegionChunk& rc, const std::vector<std::string>& pal) {
                               got.push_back({rc.chunk, rc.grid.mat, pal});
                           },
                           nullptr, threads);
        ms = msSince(t0);
        return got;
    };
    double ms1 = 0, ms4 = 0;
    const auto a = run(1, ms1);
    const auto b = run(4, ms4);
    std::printf("[f79a] 4 chunks (flora %s): 1 thread %.0f ms, 4 threads %.0f ms\n", root.empty() ? "off" : "on", ms1, ms4);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].chunk, b[i].chunk);
        EXPECT_EQ(a[i].pal, b[i].pal);
        EXPECT_TRUE(a[i].mat == b[i].mat) << "chunk " << i << " differs";
    }
}
