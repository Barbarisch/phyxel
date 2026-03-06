#include "utils/CoordinateUtils.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Utils {

glm::ivec3 CoordinateUtils::worldToChunkCoord(const glm::ivec3& worldPos) {
    // Proper floor division that handles negative numbers correctly
    // For positive numbers: x / 32
    // For negative numbers: (x - 31) / 32
    // This ensures consistent behavior across negative boundaries
    glm::ivec3 chunk;
    chunk.x = worldPos.x >= 0 ? worldPos.x / CHUNK_SIZE : (worldPos.x - (CHUNK_SIZE - 1)) / CHUNK_SIZE;
    chunk.y = worldPos.y >= 0 ? worldPos.y / CHUNK_SIZE : (worldPos.y - (CHUNK_SIZE - 1)) / CHUNK_SIZE;
    chunk.z = worldPos.z >= 0 ? worldPos.z / CHUNK_SIZE : (worldPos.z - (CHUNK_SIZE - 1)) / CHUNK_SIZE;
    return chunk;
}

glm::ivec3 CoordinateUtils::worldToLocalCoord(const glm::ivec3& worldPos) {
    // Proper modulo that handles negative numbers correctly
    // This ensures local coordinates are always in range [0, CHUNK_SIZE)
    glm::ivec3 local;
    local.x = ((worldPos.x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    local.y = ((worldPos.y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    local.z = ((worldPos.z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    return local;
}

glm::ivec3 CoordinateUtils::chunkCoordToOrigin(const glm::ivec3& chunkCoord) {
    // Simple multiplication: chunk coordinates to world origin
    return chunkCoord * CHUNK_SIZE;
}

glm::ivec3 CoordinateUtils::localToWorld(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
    // Combine chunk origin with local offset
    return chunkCoordToOrigin(chunkCoord) + localPos;
}

bool CoordinateUtils::isValidLocalCoord(const glm::ivec3& localPos) {
    return localPos.x >= 0 && localPos.x < CHUNK_SIZE &&
           localPos.y >= 0 && localPos.y < CHUNK_SIZE &&
           localPos.z >= 0 && localPos.z < CHUNK_SIZE;
}

} // namespace Utils
} // namespace Phyxel
