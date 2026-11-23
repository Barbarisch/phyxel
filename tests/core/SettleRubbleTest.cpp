// docs/VoxelFoundationPlan.md F8.2 -- settleIntoWholeVoxels: exact counts, never over a solid, enclosed pockets escaped.
#include <gtest/gtest.h>

#include "core/voxfound/SettleRubble.h"

using namespace Phyxel::Core::VoxFound;

namespace {
VoxelGrid box(glm::ivec3 d) {
    VoxelGrid g;
    g.resize(d);
    return g;
}
size_t count(const VoxelGrid& g, uint8_t m) {
    size_t n = 0;
    for (uint8_t v : g.mat) n += v == m;
    return n;
}
}  // namespace

TEST(VoxFoundSettleRubble, ExactCountsFullestFirstNeverOverSolids) {
    VoxelGrid g = box({16, 16, 16});
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 4; ++y) g.set({x, y, z}, 1);   // ground (material 1)
    const size_t ground = count(g, 1);
    std::map<uint8_t, uint64_t> units{{2, 5 * 256}, {3, 3 * 256 + 17}};
    std::map<uint8_t, std::map<size_t, uint64_t>> cand;
    cand[2][g.idx({5, 4, 5})] = 200;
    cand[2][g.idx({6, 4, 5})] = 250;   // the fullest
    cand[3][g.idx({1, 2, 1})] = 999;   // a candidate inside the ground: never overwritten
    std::map<size_t, uint8_t> write;
    const SettleStats s = settleIntoWholeVoxels(g, units, cand, write);
    EXPECT_EQ(count(g, 2), 5u);
    EXPECT_EQ(count(g, 3), 3u);
    EXPECT_EQ(count(g, 1), ground) << "no solid overwritten";
    EXPECT_EQ(s.placed, 8u);
    EXPECT_EQ(s.unplaced, 0u);
    EXPECT_EQ(s.remainderUnits, 17u);
    EXPECT_EQ(g.at({6, 4, 5}), 2) << "the fullest candidate first";
    EXPECT_EQ(write.size(), 8u);
}

TEST(VoxFoundSettleRubble, EnclosedPocketDoesNotStrandWhatIsOwed) {
    VoxelGrid g = box({12, 12, 12});
    for (size_t i = 0; i < g.size(); ++i) g.mat[i] = 1;   // solid everywhere ...
    g.set({6, 6, 6}, 0);                                   // ... but a one-voxel pocket
    for (int x = 0; x < 12; ++x)
        for (int z = 0; z < 12; ++z) g.set({x, 11, z}, 0);   // and an open top layer far away
    std::map<uint8_t, uint64_t> units{{4, 10 * 256}};
    std::map<uint8_t, std::map<size_t, uint64_t>> cand;
    cand[4][g.idx({6, 6, 6})] = 256;
    std::map<size_t, uint8_t> write;
    const SettleStats s = settleIntoWholeVoxels(g, units, cand, write);
    EXPECT_EQ(count(g, 4), 10u) << "the pocket + 9 reached through the solids";
    EXPECT_EQ(s.unplaced, 0u);
    EXPECT_EQ(s.flooded, 9u);
    EXPECT_EQ(g.at({6, 6, 6}), 4);
}
