// docs/VoxelFoundationPlan.md F7.1 -- see WorldReads.h.
#include "core/voxfound/WorldReads.h"

namespace Phyxel::Core::VoxFound {

namespace {
int wrDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
glm::ivec3 wrDiv(glm::ivec3 a, int b) { return {wrDiv(a.x, b), wrDiv(a.y, b), wrDiv(a.z, b)}; }
constexpr int kBlocksPerChunk = WorldReads::kChunkVoxels / 8;   // 32: one block per old cube
}  // namespace

VoxelLocation::Type WorldReads::typeAt(glm::ivec3 cube) const {
    const glm::ivec3 c = wrDiv(cube, kBlocksPerChunk);
    const BlockStore* s = m_storeOf(c);
    if (!s) return VoxelLocation::EMPTY;
    const int n = s->blockSolidCount(cube - c * kBlocksPerChunk);
    return n == 0 ? VoxelLocation::EMPTY : n == 512 ? VoxelLocation::CUBE : VoxelLocation::SUBDIVIDED;
}

float WorldReads::subVoxelFloor(glm::ivec3 cube) const {
    const glm::ivec3 c = wrDiv(cube, kBlocksPerChunk);
    const BlockStore* s = m_storeOf(c);
    if (!s) return 0.0f;
    const glm::ivec3 b = cube - c * kBlocksPerChunk;
    const int n = s->blockSolidCount(b);
    if (n == 0) return 0.0f;
    if (n == 512) return -1.0f;
    const glm::ivec3 o = b * 8;
    int fullLayers = 0;
    for (int y = 0; y < 8; ++y) {
        int inLayer = 0;
        for (int x = 0; x < 8; ++x)
            for (int z = 0; z < 8; ++z) inLayer += s->at(o + glm::ivec3(x, y, z)) != 0;
        if (inLayer == 64) ++fullLayers;
        else break;
    }
    if (fullLayers == 0) return -1.0f;        // no floor at all
    if (n != fullLayers * 64) return -1.0f;   // content above the floor layers: a wall, not a platform
    return float(fullLayers) / 8.0f;
}

uint8_t WorldReads::voxel(glm::ivec3 v) const {
    const glm::ivec3 c = wrDiv(v, kChunkVoxels);
    const BlockStore* s = m_storeOf(c);
    return s ? s->at(v - c * kChunkVoxels) : 0;
}

bool WorldReads::occupiedMicro(glm::ivec3 micro) const {
    // the voxel under the micro cell's centre: floor((m + 0.5) / 9 * 8) = floor((16 m + 8) / 18), exactly in integers
    auto v = [](int m) { return wrDiv(16 * m + 8, 18); };
    return voxel({v(micro.x), v(micro.y), v(micro.z)}) != 0;
}

}  // namespace Phyxel::Core::VoxFound
