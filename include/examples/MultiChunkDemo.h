#pragma once

#include "core/ChunkManager.h"
#include <glm/glm.hpp>
#include <vector>

namespace VulkanCube {

// Example showing how to easily create multiple chunks for rendering
class MultiChunkDemo {
public:
    static std::vector<glm::ivec3> createLinearChunks(int numChunks) {
        std::vector<glm::ivec3> origins;
        origins.reserve(numChunks);
        
        // Create chunks in a line along X-axis
        for (int i = 0; i < numChunks; ++i) {
            origins.emplace_back(i * 32, 0, 0); // Each chunk is 32 units apart
        }
        
        return origins;
    }
    
    static std::vector<glm::ivec3> createGridChunks(int width, int height) {
        std::vector<glm::ivec3> origins;
        origins.reserve(width * height);
        
        // Create chunks in a 2D grid (width x height)
        for (int x = 0; x < width; ++x) {
            for (int z = 0; z < height; ++z) {
                origins.emplace_back(x * 32, 0, z * 32);
            }
        }
        
        return origins;
    }
    
    static std::vector<glm::ivec3> create3DGridChunks(int width, int height, int depth) {
        std::vector<glm::ivec3> origins;
        origins.reserve(width * height * depth);
        
        // Create chunks in a 3D grid (width x height x depth)
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                for (int z = 0; z < depth; ++z) {
                    origins.emplace_back(x * 32, y * 32, z * 32);
                }
            }
        }
        
        return origins;
    }
};

} // namespace VulkanCube
