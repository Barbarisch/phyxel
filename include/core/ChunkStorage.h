#pragma once

#include "Types.h"
#include "core/Subcube.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace VulkanCube {

/**
 * @brief Manages voxel storage and organization within a 32x32x32 chunk
 * Handles cube/subcube storage, indexing, and spatial queries
 * Extracted from Chunk class for better separation of concerns
 */
class ChunkStorage {
public:
    ChunkStorage();
    ~ChunkStorage();

    // Core voxel management - extracted from Chunk
    void addCube(const glm::ivec3& localPos, std::unique_ptr<Cube> cube);
    void addCube(Cube* cube);  // For compatibility with existing push_back pattern
    bool addCubeWithColor(const glm::ivec3& localPos, const glm::vec3& color);  // Helper for existing interface
    bool removeCube(const glm::ivec3& localPos);
    Cube* getCubeAt(const glm::ivec3& localPos) const;
    
    // Subcube management - extracted from Chunk  
    void addStaticSubcube(std::unique_ptr<Subcube> subcube);
    void addStaticSubcube(Subcube* subcube);  // Raw pointer overload for compatibility
    bool removeStaticSubcube(const glm::ivec3& parentPos, const glm::ivec3& localPos);
    std::vector<Subcube*> getStaticSubcubesAt(const glm::ivec3& localPos) const;
    Subcube* getSubcubeAt(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) const;
    
    // Spatial queries - extracted from Chunk
    bool isValidLocalPosition(const glm::ivec3& localPos) const;
    size_t localToIndex(const glm::ivec3& localPos) const;
    glm::ivec3 indexToLocal(size_t index) const;
    
    // Voxel type management - extracted from Chunk
    VoxelLocation::Type getVoxelType(const glm::ivec3& localPos) const;
    void setVoxelType(const glm::ivec3& localPos, VoxelLocation::Type type);
    bool hasVoxelType(const glm::ivec3& localPos) const;
    void clearVoxelType(const glm::ivec3& localPos);
    
    // Container access - extracted from Chunk
    const std::vector<Cube*>& getCubes() const { return cubes; }
    const std::vector<Subcube*>& getStaticSubcubes() const { return staticSubcubes; }
    std::vector<Cube*>& getCubes() { return cubes; }
    std::vector<Subcube*>& getStaticSubcubes() { return staticSubcubes; }
    
    // Direct container modification (for bulk operations)
    void reserveCubes(size_t capacity) { cubes.reserve(capacity); }
    void reserveSubcubes(size_t capacity) { staticSubcubes.reserve(capacity); }
    void resizeCubes(size_t size) { cubes.resize(size, nullptr); }
    
    // Statistics and validation - extracted from Chunk
    size_t getCubeCount() const;
    size_t getSubcubeCount() const;
    size_t getVisibleCubeCount() const;
    size_t getVisibleSubcubeCount() const;
    void clear();
    
    // Bulk operations for performance
    void clearCubes();
    void clearSubcubes();
    void clearVoxelTypes();

private:
    // Storage containers - moved from Chunk
    std::vector<Cube*> cubes;                      // 32x32x32 cube storage
    std::vector<Subcube*> staticSubcubes;          // Static subcube storage
    
    // O(1) lookup optimizations - moved from Chunk
    std::unordered_map<glm::ivec3, Cube*, IVec3Hash> cubeMap;
    std::unordered_map<glm::ivec3, 
                      std::unordered_map<glm::ivec3, Subcube*, IVec3Hash>, 
                      IVec3Hash> subcubeMap;
    std::unordered_map<glm::ivec3, VoxelLocation::Type, IVec3Hash> voxelTypeMap;
    
    // Helper methods
    void updateCubeMap(const glm::ivec3& localPos, Cube* cube);
    void updateSubcubeMap(const glm::ivec3& parentPos, const glm::ivec3& localPos, Subcube* subcube);
};

} // namespace VulkanCube