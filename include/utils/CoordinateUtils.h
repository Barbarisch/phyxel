#pragma once

#include <glm/glm.hpp>

namespace VulkanCube {
namespace Utils {

/**
 * @brief Utility class for coordinate system conversions
 * 
 * Phyxel uses a hierarchical coordinate system:
 * - World coordinates: Global 3D integer positions
 * - Chunk coordinates: World divided into 32x32x32 chunks
 * - Local coordinates: Position within a chunk (0-31 in each axis)
 * 
 * This class provides static methods for converting between these systems,
 * handling negative coordinates correctly with proper floor division.
 */
class CoordinateUtils {
public:
    // Chunk size constant (32x32x32 cubes per chunk)
    static constexpr int CHUNK_SIZE = 32;

    /**
     * @brief Convert world position to chunk coordinates
     * @param worldPos World position (can be negative)
     * @return Chunk coordinates using floor division
     * 
     * Examples:
     *   worldPos (0,0,0) -> chunk (0,0,0)
     *   worldPos (31,31,31) -> chunk (0,0,0)
     *   worldPos (32,32,32) -> chunk (1,1,1)
     *   worldPos (-1,-1,-1) -> chunk (-1,-1,-1)
     *   worldPos (-32,-32,-32) -> chunk (-1,-1,-1)
     */
    static glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos);

    /**
     * @brief Convert world position to local coordinates within chunk
     * @param worldPos World position (can be negative)
     * @return Local position within chunk (0-31 in each axis)
     * 
     * Examples:
     *   worldPos (0,0,0) -> local (0,0,0)
     *   worldPos (31,31,31) -> local (31,31,31)
     *   worldPos (32,32,32) -> local (0,0,0)
     *   worldPos (-1,-1,-1) -> local (31,31,31)
     */
    static glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos);

    /**
     * @brief Convert chunk coordinates to world origin position
     * @param chunkCoord Chunk coordinates
     * @return World position of chunk origin (minimum corner)
     * 
     * Example:
     *   chunk (0,0,0) -> worldOrigin (0,0,0)
     *   chunk (1,1,1) -> worldOrigin (32,32,32)
     *   chunk (-1,-1,-1) -> worldOrigin (-32,-32,-32)
     */
    static glm::ivec3 chunkCoordToOrigin(const glm::ivec3& chunkCoord);

    /**
     * @brief Convert local coordinates and chunk to world position
     * @param chunkCoord Chunk coordinates
     * @param localPos Local position within chunk (0-31)
     * @return World position
     */
    static glm::ivec3 localToWorld(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);

    /**
     * @brief Check if local coordinates are valid (within 0-31 range)
     * @param localPos Local coordinates to validate
     * @return true if all components are in [0, CHUNK_SIZE)
     */
    static bool isValidLocalCoord(const glm::ivec3& localPos);
};

} // namespace Utils
} // namespace VulkanCube
