#include "core/ChunkManager.h"

void ChunkManager::createChunks(const std::vector<glm::ivec3>& origins) {
    chunks.clear();
    for (const auto& origin : origins) {
        Chunk chunk;
        chunk.worldOrigin = origin;
        // Fill chunk.cubes with 32x32x32 cubes at relative positions
        for (int x = 0; x < 32; ++x) {
            for (int y = 0; y < 32; ++y) {
                for (int z = 0; z < 32; ++z) {
                    InstanceData instance;
                    // Pack relative position (x, y, z) and default face mask
                    instance.packedData = (x & 0x1F) | ((y & 0x1F) << 5) | ((z & 0x1F) << 10);
                    instance.color = glm::vec3(1.0f); // Default color
                    chunk.cubes.push_back(instance);
                }
            }
        }
        chunk.numInstances = static_cast<uint32_t>(chunk.cubes.size());
        chunks.push_back(std::move(chunk));
    }
}
