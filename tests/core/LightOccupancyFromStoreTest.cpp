// LightOccupancyFromStoreTest -- docs/VoxelFoundationPlan.md F7.3b O1 (prediction docs/evidence/voxfound_f7_prediction.txt):
// the light occupancy built from the new store equals the rule on every cube, and through the packed pool (the
// layout the shaders read) every micro cell equals WorldReads::occupiedMicro -- what blocks light is what NPCs read.

#include <gtest/gtest.h>

#include <cstdio>

#include "core/voxfound/LightOccupancyFromStore.h"
#include "core/voxfound/WorldReads.h"
#include "graphics/VoxelLightOccupancy.h"

using namespace Phyxel;
using Phyxel::Core::VoxFound::BlockStore;
using Phyxel::Core::VoxFound::WorldReads;
using Phyxel::Graphics::ChunkLightOccupancy;

namespace {
uint32_t h3(int x, int y, int z) {
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}
// mixed content in cubes [0, 8)^3 of one chunk: air, uniform, full detailed, one voxel, slabs, random half, holes
void fill(BlockStore& s) {
    for (int cx = 0; cx < 8; ++cx)
        for (int cy = 0; cy < 8; ++cy)
            for (int cz = 0; cz < 8; ++cz) {
                const uint32_t h = h3(cx, cy, cz);
                const int kind = int(h % 7), k = 1 + int((h >> 8) % 7);
                const glm::ivec3 o(cx * 8, cy * 8, cz * 8);
                for (int x = 0; x < 8; ++x)
                    for (int y = 0; y < 8; ++y)
                        for (int z = 0; z < 8; ++z) {
                            const glm::ivec3 v = o + glm::ivec3(x, y, z);
                            bool on = false;
                            switch (kind) {
                                case 1: case 2: on = true; break;
                                case 3: on = x == int(h >> 4) % 8 && y == int(h >> 7) % 8 && z == int(h >> 10) % 8; break;
                                case 4: on = y < k; break;
                                case 5: on = (h3(v.x, v.y, v.z) & 1) != 0; break;
                                case 6: on = !(x == 3 && y == 4 && z == 5); break;
                            }
                            if (on) s.set(v, uint8_t(1 + kind % 3));
                        }
                if (kind == 1) s.fillBlock({cx, cy, cz}, 2);   // uniform entry
            }
}
}  // namespace

TEST(LightOccupancyFromStore, EqualsTheRuleAndTheReadsSampling) {
    BlockStore s(glm::ivec3(256));
    fill(s);
    const glm::ivec3 chunk(-1, 0, 2);   // negative coordinates
    const ChunkLightOccupancy occ = Core::VoxFound::lightOccupancyOf(s);
    // per cube: solid = 512 solid voxels, mixed = some, nothing = none (or a mixed block whose micro samples are all air)
    size_t bad = 0, solid = 0, mixed = 0;
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
            for (int z = 0; z < 32; ++z) {
                const int n = s.blockSolidCount({x, y, z}), ci = ChunkLightOccupancy::cubeIndex({x, y, z});
                const bool wantSolid = n == 512, isMixed = occ.cubeIsMixed(ci);
                bad += occ.cubeIsSolid(ci) != wantSolid;
                bad += isMixed && (n == 0 || n == 512);
                bad += (n > 0 && n < 512 && !isMixed && occ.cubeIsSolid(ci));
                solid += wantSolid;
                mixed += isMixed;
            }
    for (size_t i = 1; i < occ.mixedCubeIdx.size(); ++i) bad += occ.mixedCubeIdx[i] <= occ.mixedCubeIdx[i - 1];
    // through the packed pool vs WorldReads::occupiedMicro (the same store), every micro cell of the content
    const WorldReads r([&](glm::ivec3 c) { return c == chunk ? &s : nullptr; }, {chunk});
    const auto pool = Graphics::packOccupancyPool({{chunk * 32, occ}}, chunk - glm::ivec3(2, 0, 2));
    size_t microBad = 0, microSolid = 0, checked = 0;
    for (int mx = 0; mx < 72; ++mx)
        for (int my = 0; my < 72; ++my)
            for (int mz = 0; mz < 72; ++mz) {
                const glm::ivec3 m = chunk * 32 * 9 + glm::ivec3(mx, my, mz);
                const bool want = r.occupiedMicro(m), got = Graphics::packedPoolSolidAt(pool, m);
                microBad += want != got;
                microSolid += want;
                ++checked;
            }
    std::printf("[LightOccupancyFromStore] cubes: %zu solid, %zu mixed, %zu wrong; micro: %zu checked (%zu solid), %zu "
                "differ from WorldReads::occupiedMicro\n", solid, mixed, bad, checked, microSolid, microBad);
    EXPECT_EQ(bad, 0u);
    EXPECT_EQ(microBad, 0u);
    EXPECT_GT(solid, 0u);
    EXPECT_GT(mixed, 0u);
    EXPECT_GT(microSolid, 0u);
}
