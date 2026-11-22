// docs/VoxelFoundationPlan.md F7.8 -- storeBoxes: the physics occupancy of a BlockStore. Every solid voxel the query
// reaches is covered by exactly one emitted box; no box covers air; a uniform solid block is one 1 m box; the reached
// cells follow VoxelOccupancyGrid::queryAABB's rule (floor of the corners, clamped).
#include <gtest/gtest.h>

#include <random>

#include "core/voxfound/OccupancyFromStore.h"

using Phyxel::Core::VoxFound::BlockStore;
using Phyxel::Core::VoxFound::storeBoxes;

namespace {
struct Box { glm::ivec3 lo, hi; };   // voxel corners (hi exclusive), chunk-local
BlockStore makeStore(uint32_t seed) {
    BlockStore s(glm::ivec3(64));
    std::mt19937 r(seed);
    for (int bx = 0; bx < 8; ++bx)
        for (int by = 0; by < 8; ++by)
            for (int bz = 0; bz < 8; ++bz) {
                const int kind = int(r() % 4);   // air, uniform solid, uniform other, mixed
                const glm::ivec3 b(bx, by, bz);
                if (kind == 1) s.fillBlock(b, 3);
                else if (kind == 2) s.fillBlock(b, 7);
                else if (kind == 3)
                    for (int k = 0; k < 512; ++k)
                        if (r() % 3 == 0) s.set(b * 8 + glm::ivec3(k >> 6, (k >> 3) & 7, k & 7), uint8_t(1 + r() % 5));
            }
    s.collapse();
    return s;
}
}  // namespace

TEST(VoxFoundOccupancyFromStore, EverySolidVoxelCoveredExactlyOnce) {
    const glm::ivec3 origin(64 * 3, -64, 64 * 5);   // world voxels (a negative y chunk too)
    std::mt19937 r(11);
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        const BlockStore s = makeStore(seed);
        for (int q = 0; q < 60; ++q) {
            const glm::vec3 a = (glm::vec3(origin) + glm::vec3(float(r() % 80) - 8, float(r() % 80) - 8,
                                                                float(r() % 80) - 8) + 0.37f) / 8.0f;
            const glm::vec3 ext(float(r() % 30) / 8.0f + 0.05f, float(r() % 30) / 8.0f + 0.05f, float(r() % 30) / 8.0f);
            const glm::vec3 mn = a, mx = a + ext;
            std::vector<Box> boxes;
            size_t wholeBlocks = 0;
            storeBoxes(s, origin, mn, mx, [&](glm::vec3 lo, glm::vec3 hi) {
                const glm::ivec3 l = glm::ivec3(glm::round(lo * 8.0f)) - origin, h = glm::ivec3(glm::round(hi * 8.0f)) - origin;
                if (h - l == glm::ivec3(8)) ++wholeBlocks;
                boxes.push_back({l, h});
            });
            std::vector<int> cover(64 * 64 * 64, 0);
            for (const Box& b : boxes)
                for (int x = b.lo.x; x < b.hi.x; ++x)
                    for (int y = b.lo.y; y < b.hi.y; ++y)
                        for (int z = b.lo.z; z < b.hi.z; ++z) {
                            ASSERT_NE(s.get({x, y, z}), 0) << "a box covers air";
                            ++cover[size_t(z + 64 * (y + 64 * x))];
                        }
            // the reached cells: floor of the corners, clamped (the old grid's rule), when the AABB meets the chunk
            const glm::vec3 o(origin);
            const bool meets = !(mx.x * 8 < o.x || mn.x * 8 > o.x + 64 || mx.y * 8 < o.y || mn.y * 8 > o.y + 64 ||
                                 mx.z * 8 < o.z || mn.z * 8 > o.z + 64);
            auto cl = [](int v) { return std::clamp(v, 0, 63); };
            const glm::ivec3 v0(cl(int(std::floor(mn.x * 8 - o.x))), cl(int(std::floor(mn.y * 8 - o.y))),
                                cl(int(std::floor(mn.z * 8 - o.z))));
            const glm::ivec3 v1(cl(int(std::floor(mx.x * 8 - o.x))), cl(int(std::floor(mx.y * 8 - o.y))),
                                cl(int(std::floor(mx.z * 8 - o.z))));
            for (int x = 0; x < 64; ++x)
                for (int y = 0; y < 64; ++y)
                    for (int z = 0; z < 64; ++z) {
                        const int c = cover[size_t(z + 64 * (y + 64 * x))];
                        const bool reached = meets && x >= v0.x && x <= v1.x && y >= v0.y && y <= v1.y && z >= v0.z &&
                                             z <= v1.z;
                        if (reached && s.get({x, y, z})) ASSERT_EQ(c, 1) << "solid reached voxel not covered once";
                        else ASSERT_LE(c, 1);
                    }
            (void)wholeBlocks;
        }
    }
}

TEST(VoxFoundOccupancyFromStore, UniformBlockIsOneMetreBox) {
    BlockStore s(glm::ivec3(64));
    s.fillBlock({1, 1, 1}, 4);
    s.collapse();
    std::vector<std::pair<glm::vec3, glm::vec3>> out;
    storeBoxes(s, glm::ivec3(0), glm::vec3(1.2f), glm::vec3(1.3f), [&](glm::vec3 a, glm::vec3 b) { out.push_back({a, b}); });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].first, glm::vec3(1.0f));
    EXPECT_EQ(out[0].second, glm::vec3(2.0f));
    out.clear();
    storeBoxes(s, glm::ivec3(0), glm::vec3(20.0f), glm::vec3(21.0f), [&](glm::vec3 a, glm::vec3 b) { out.push_back({a, b}); });
    EXPECT_TRUE(out.empty());   // outside the chunk
}
