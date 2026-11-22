// docs/VoxelFoundationPlan.md F7.3b -- see LightOccupancyFromStore.h.
#include "core/voxfound/LightOccupancyFromStore.h"

namespace Phyxel::Core::VoxFound {

using Graphics::ChunkLightOccupancy;

Graphics::ChunkLightOccupancy lightOccupancyOf(const BlockStore& s) {
    ChunkLightOccupancy out;
    auto setBit = [](std::array<uint32_t, ChunkLightOccupancy::kCubeWords>& w, int i) {
        w[size_t(i) >> 5] |= 1u << (i & 31);
    };
    // the voxel (0..7) under micro cell m's centre (0..8) inside one block: floor((m + 0.5) / 9 * 8)
    int under[9];
    for (int m = 0; m < 9; ++m) under[m] = (16 * m + 8) / 18;
    std::array<uint32_t, ChunkLightOccupancy::kMicroWordsPerCube> micro{};
    for (int x = 0; x < 32; ++x)   // x, y, z order: ascending cube index (z + y*32 + x*1024), as the shader requires
        for (int y = 0; y < 32; ++y)
            for (int z = 0; z < 32; ++z) {
                const glm::ivec3 b(x, y, z);
                const int n = s.blockSolidCount(b);
                if (n == 0) continue;
                const int ci = ChunkLightOccupancy::cubeIndex(b);
                if (n == 512) {
                    setBit(out.solid, ci);
                    continue;
                }
                micro.fill(0u);
                bool any = false;
                const glm::ivec3 o = b * 8;
                for (int mx = 0; mx < 9; ++mx)
                    for (int my = 0; my < 9; ++my)
                        for (int mz = 0; mz < 9; ++mz)
                            if (s.at(o + glm::ivec3(under[mx], under[my], under[mz]))) {
                                const int bit = ChunkLightOccupancy::microIndex({mx, my, mz});
                                micro[size_t(bit) >> 5] |= 1u << (bit & 31);
                                any = true;
                            }
                if (!any) continue;
                setBit(out.mixed, ci);
                out.mixedCubeIdx.push_back(uint32_t(ci));
                out.microWords.insert(out.microWords.end(), micro.begin(), micro.end());
            }
    return out;
}

}  // namespace Phyxel::Core::VoxFound
