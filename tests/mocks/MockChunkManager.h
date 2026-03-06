/**
 * MockChunkManager - Test double for ChunkManager
 * 
 * Provides a lightweight mock implementation for testing components
 * that depend on ChunkManager without requiring full world initialization.
 * 
 * Since ChunkManager is not designed for inheritance (no virtual methods),
 * we create a standalone mock that mimics only the interface needed for testing.
 * Tests pass this to raycaster using a wrapper function.
 */

#pragma once

#include "core/Types.h"
#include "core/IChunkManager.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <functional>
#include <iostream>

namespace Phyxel {

// Hash function for glm::ivec3
struct Vec3Hash {
    std::size_t operator()(const glm::ivec3& v) const {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
    }
};

/**
 * Mock ChunkManager for testing
 * 
 * Mimics ChunkManager::resolveGlobalPosition() behavior using a simple hash map.
 * This is NOT a subclass of ChunkManager - it's a test double.
 */
class MockChunkManager : public IChunkManager {
public:
    MockChunkManager() = default;
    ~MockChunkManager() = default;

    /**
     * Add a cube voxel at world position
     */
    void addCube(const glm::ivec3& worldPos) {
        VoxelLocation loc;
        loc.worldPos = worldPos;
        loc.type = VoxelLocation::CUBE;
        loc.chunk = (Chunk*)1; // Dummy non-null pointer to satisfy isValid()
        voxels[worldPos] = loc;
    }
    
    /**
     * Add a subdivided voxel at world position
     */
    void addSubdivided(const glm::ivec3& worldPos) {
        VoxelLocation loc;
        loc.worldPos = worldPos;
        loc.type = VoxelLocation::SUBDIVIDED;
        loc.chunk = (Chunk*)1; // Dummy non-null pointer to satisfy isValid()
        voxels[worldPos] = loc;
    }
    
    /**
     * Remove voxel at position
     */
    void removeVoxel(const glm::ivec3& worldPos) {
        voxels.erase(worldPos);
    }
    
    /**
     * Clear all voxels
     */
    void clear() {
        voxels.clear();
    }
    
    /**
     * Resolve global position (mimics ChunkManager interface)
     */
    VoxelLocation resolveGlobalPosition(const glm::ivec3& worldPos) const override {
        auto it = voxels.find(worldPos);
        if (it != voxels.end()) {
            return it->second;
        }
        return VoxelLocation(); // Invalid location
    }
    
    /**
     * Check if voxel exists at position
     */
    bool hasVoxelAt(const glm::ivec3& worldPos) const {
        return voxels.find(worldPos) != voxels.end();
    }
    
    /**
     * Get voxel count
     */
    size_t getVoxelCount() const {
        return voxels.size();
    }

private:
    std::unordered_map<glm::ivec3, VoxelLocation, Vec3Hash> voxels;
};

} // namespace Phyxel
