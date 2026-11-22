// docs/VoxelFoundationPlan.md F7.13 X1 -- centre sampling partitions a cube at every level.
#include <gtest/gtest.h>

#include <map>
#include <tuple>

#include "core/voxfound/PlacementToVoxels.h"

using Phyxel::Core::VoxFound::placementVoxels;

namespace {
std::map<std::tuple<int, int, int>, int> countAll(glm::ivec3 cube, int level) {
    std::map<std::tuple<int, int, int>, int> n;
    std::vector<glm::ivec3> out;
    const int subs = level >= 1 ? 3 : 1, micros = level >= 2 ? 3 : 1;
    for (int sx = 0; sx < subs; ++sx)
        for (int sy = 0; sy < subs; ++sy)
            for (int sz = 0; sz < subs; ++sz)
                for (int mx = 0; mx < micros; ++mx)
                    for (int my = 0; my < micros; ++my)
                        for (int mz = 0; mz < micros; ++mz) {
                            out.clear();
                            placementVoxels(cube, level, {sx, sy, sz}, {mx, my, mz}, out);
                            for (const auto& v : out) ++n[{v.x, v.y, v.z}];
                        }
    return n;
}
}  // namespace

TEST(VoxFoundPlacementToVoxels, EveryLevelPartitionsTheCube) {
    for (const glm::ivec3 cube : {glm::ivec3(0), glm::ivec3(8132, 27, 68), glm::ivec3(-5, -1, -9)}) {
        for (int level = 0; level <= 2; ++level) {
            const auto n = countAll(cube, level);
            ASSERT_EQ(n.size(), 512u) << "level " << level;
            for (const auto& [v, c] : n) {
                ASSERT_EQ(c, 1) << "voxel covered " << c << " times, level " << level;
                const glm::ivec3 p(std::get<0>(v), std::get<1>(v), std::get<2>(v));
                ASSERT_TRUE(glm::all(glm::greaterThanEqual(p, cube * 8)) && glm::all(glm::lessThan(p, cube * 8 + 8)));
            }
        }
    }
}

TEST(VoxFoundPlacementToVoxels, LoneSizes) {
    std::vector<glm::ivec3> out;
    placementVoxels({0, 0, 0}, 0, {}, {}, out);
    EXPECT_EQ(out.size(), 512u);
    for (int s = 0; s < 3; ++s) {
        out.clear();
        placementVoxels({0, 0, 0}, 1, {s, s, s}, {}, out);
        EXPECT_TRUE(out.size() == 8u || out.size() == 27u) << out.size();   // 2 or 3 voxels per axis
    }
}
