#include "core/ChunkManager.h"
#include <stdexcept>
#include <cstring>
#include <random>
#include <iostream>

namespace VulkanCube {

void ChunkManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
}

void ChunkManager::createChunks(const std::vector<glm::ivec3>& origins) {
    chunks.clear();
    chunks.reserve(origins.size());
    
    for (const auto& origin : origins) {
        chunks.emplace_back(origin);
        Chunk& chunk = chunks.back();
        
        // Fill chunk with 32x32x32 cubes at relative positions
        populateChunk(chunk);
        
        // Create Vulkan buffer for this chunk
        createChunkBuffer(chunk);
    }
}

void ChunkManager::createChunk(const glm::ivec3& origin) {
    chunks.emplace_back(origin);
    Chunk& chunk = chunks.back();
    
    populateChunk(chunk);
    createChunkBuffer(chunk);
}

void ChunkManager::populateChunk(Chunk& chunk) {
    chunk.cubes.clear();
    chunk.cubes.reserve(chunk.numInstances);
    
    // Random number generator for colors
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
    
    // Create 32x32x32 grid of cubes with relative positions (0-31)
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            for (int z = 0; z < 32; ++z) {
                InstanceData instance;
                
                // Pack relative position using existing packing logic (5 bits each)
                // This maintains compatibility with your vertex shader
                instance.packedData = (x & 0x1F) | ((y & 0x1F) << 5) | ((z & 0x1F) << 10);
                
                // Face mask will be set by performOcclusionCulling() - start with all faces visible
                // This ensures cubes are visible until occlusion culling runs
                instance.packedData |= (0x3F << 15); // All faces visible initially
                
                // Random color for each cube
                instance.color = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                
                chunk.cubes.push_back(instance);
            }
        }
    }
    
    chunk.numInstances = static_cast<uint32_t>(chunk.cubes.size());
}

void ChunkManager::createChunkBuffer(Chunk& chunk) {
    if (device == VK_NULL_HANDLE) {
        throw std::runtime_error("ChunkManager not initialized with Vulkan device!");
    }
    
    VkDeviceSize bufferSize = sizeof(InstanceData) * chunk.cubes.size();
    
    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunk.instanceBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create chunk instance buffer!");
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, chunk.instanceBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &chunk.instanceMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate chunk instance buffer memory!");
    }
    
    vkBindBufferMemory(device, chunk.instanceBuffer, chunk.instanceMemory, 0);
    
    // Map memory persistently for easy updates
    vkMapMemory(device, chunk.instanceMemory, 0, bufferSize, 0, &chunk.mappedMemory);
    
    // Copy initial data
    memcpy(chunk.mappedMemory, chunk.cubes.data(), bufferSize);
}

void ChunkManager::updateChunk(size_t chunkIndex) {
    if (chunkIndex >= chunks.size()) return;
    
    Chunk& chunk = chunks[chunkIndex];
    if (chunk.needsUpdate && chunk.mappedMemory) {
        VkDeviceSize bufferSize = sizeof(InstanceData) * chunk.cubes.size();
        memcpy(chunk.mappedMemory, chunk.cubes.data(), bufferSize);
        chunk.needsUpdate = false;
    }
}

Chunk* ChunkManager::getChunkAt(const glm::ivec3& worldPos) {
    // Convert world position to chunk coordinates
    glm::ivec3 chunkCoord = glm::ivec3(
        worldPos.x / 32,
        worldPos.y / 32,
        worldPos.z / 32
    );
    
    // Find chunk with matching origin
    for (auto& chunk : chunks) {
        if (chunk.worldOrigin == chunkCoord * 32) {
            return &chunk;
        }
    }
    
    return nullptr;
}

uint32_t ChunkManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type!");
}

void ChunkManager::cleanup() {
    for (auto& chunk : chunks) {
        if (chunk.mappedMemory) {
            vkUnmapMemory(device, chunk.instanceMemory);
            chunk.mappedMemory = nullptr;
        }
        if (chunk.instanceBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, chunk.instanceBuffer, nullptr);
            chunk.instanceBuffer = VK_NULL_HANDLE;
        }
        if (chunk.instanceMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, chunk.instanceMemory, nullptr);
            chunk.instanceMemory = VK_NULL_HANDLE;
        }
    }
    chunks.clear();
}

uint32_t ChunkManager::calculateCubeFaceMask(int x, int y, int z) const {
    // DEPRECATED: This function is no longer used
    // Face masks are now calculated by performOcclusionCulling() using calculateOcclusionFaceMask()
    // which handles cross-chunk occlusion properly
    
    // Return all faces visible (for backward compatibility)
    return 0x3F;
    
    // Previous face culling logic (disabled):
    /*
    uint32_t faceMask = 0;
    
    // Front face (+Z): visible if at edge (z == 31) or no cube in front
    if (z == 31) {
        faceMask |= (1 << 0);  // Front face visible
    }
    
    // Back face (-Z): visible if at edge (z == 0) or no cube behind
    if (z == 0) {
        faceMask |= (1 << 1);  // Back face visible
    }
    
    // Right face (+X): visible if at edge (x == 31) or no cube to right
    if (x == 31) {
        faceMask |= (1 << 2);  // Right face visible
    }
    
    // Left face (-X): visible if at edge (x == 0) or no cube to left
    if (x == 0) {
        faceMask |= (1 << 3);  // Left face visible
    }
    
    // Top face (+Y): visible if at edge (y == 31) or no cube above
    if (y == 31) {
        faceMask |= (1 << 4);  // Top face visible
    }
    
    // Bottom face (-Y): visible if at edge (y == 0) or no cube below
    if (y == 0) {
        faceMask |= (1 << 5);  // Bottom face visible
    }
    
    return faceMask;
    */
}

void ChunkManager::calculateChunkFaceCulling() {
    // Recalculate face masks for all chunks
    for (auto& chunk : chunks) {
        for (size_t i = 0; i < chunk.cubes.size(); ++i) {
            // Extract position from packed data
            uint32_t x = (chunk.cubes[i].packedData >> 0) & 0x1F;
            uint32_t y = (chunk.cubes[i].packedData >> 5) & 0x1F;
            uint32_t z = (chunk.cubes[i].packedData >> 10) & 0x1F;
            
            // Calculate new face mask
            uint32_t newFaceMask = calculateCubeFaceMask(x, y, z);
            
            // Update packed data with new face mask
            chunk.cubes[i].packedData = (x & 0x1F) | ((y & 0x1F) << 5) | ((z & 0x1F) << 10) | (newFaceMask << 15);
        }
        
        // Update GPU buffer with new data
        if (chunk.mappedMemory) {
            VkDeviceSize bufferSize = sizeof(InstanceData) * chunk.cubes.size();
            memcpy(chunk.mappedMemory, chunk.cubes.data(), bufferSize);
        }
    }
}

ChunkManager::ChunkStats ChunkManager::getPerformanceStats() const {
    ChunkStats stats;
    
    for (const auto& chunk : chunks) {
        stats.totalCubes += chunk.numInstances;
        
        for (const auto& cube : chunk.cubes) {
            // Extract face mask
            uint32_t faceMask = (cube.packedData >> 15) & 0x3F;
            
            // Count visible faces
            int visibleFaces = 0;
            for (int i = 0; i < 6; ++i) {
                if (faceMask & (1 << i)) {
                    visibleFaces++;
                }
            }
            
            stats.totalVisibleFaces += visibleFaces;
            stats.totalHiddenFaces += (6 - visibleFaces);
            
            // Calculate vertices (6 vertices per visible face, for triangle rendering)
            stats.totalVertices += visibleFaces * 6;
            
            // Count occlusion types
            if (visibleFaces == 0) {
                stats.fullyOccludedCubes++;
            } else if (visibleFaces < 6) {
                stats.partiallyOccludedCubes++;
            }
        }
    }
    
    return stats;
}

bool ChunkManager::isCubeAt(const glm::ivec3& worldPosition) const {
    // Convert world position to chunk coordinates
    glm::ivec3 chunkCoord = glm::ivec3(
        worldPosition.x / 32,
        worldPosition.y / 32,
        worldPosition.z / 32
    );
    glm::ivec3 chunkOrigin = chunkCoord * 32;
    
    // Find the chunk containing this world position
    for (const auto& chunk : chunks) {
        if (chunk.worldOrigin == chunkOrigin) {
            // Calculate relative position within chunk
            glm::ivec3 relativePos = worldPosition - chunkOrigin;
            
            // Check if relative position is valid (0-31 in each dimension)
            if (relativePos.x >= 0 && relativePos.x < 32 &&
                relativePos.y >= 0 && relativePos.y < 32 &&
                relativePos.z >= 0 && relativePos.z < 32) {
                
                // For full chunks, assume all positions have cubes
                // (This could be optimized with a 3D bitmap if chunks become sparse)
                return true;
            }
            break;
        }
    }
    
    return false; // No chunk found or position outside chunk bounds
}

uint32_t ChunkManager::calculateOcclusionFaceMask(const glm::ivec3& chunkOrigin, int relativeX, int relativeY, int relativeZ) const {
    // Calculate world position for this cube
    glm::ivec3 worldPos = chunkOrigin + glm::ivec3(relativeX, relativeY, relativeZ);
    
    // Check each face direction for adjacent cubes (including cross-chunk)
    uint32_t faceMask = 0;
    
    // Front face (+Z): visible if no cube at (x, y, z+1)
    if (!isCubeAt(worldPos + glm::ivec3(0, 0, 1))) {
        faceMask |= (1 << 0);  // Front face visible
    }
    
    // Back face (-Z): visible if no cube at (x, y, z-1)
    if (!isCubeAt(worldPos + glm::ivec3(0, 0, -1))) {
        faceMask |= (1 << 1);  // Back face visible
    }
    
    // Right face (+X): visible if no cube at (x+1, y, z)
    if (!isCubeAt(worldPos + glm::ivec3(1, 0, 0))) {
        faceMask |= (1 << 2);  // Right face visible
    }
    
    // Left face (-X): visible if no cube at (x-1, y, z)
    if (!isCubeAt(worldPos + glm::ivec3(-1, 0, 0))) {
        faceMask |= (1 << 3);  // Left face visible
    }
    
    // Top face (+Y): visible if no cube at (x, y+1, z)
    if (!isCubeAt(worldPos + glm::ivec3(0, 1, 0))) {
        faceMask |= (1 << 4);  // Top face visible
    }
    
    // Bottom face (-Y): visible if no cube at (x, y-1, z)
    if (!isCubeAt(worldPos + glm::ivec3(0, -1, 0))) {
        faceMask |= (1 << 5);  // Bottom face visible
    }
    
    return faceMask;
}

void ChunkManager::performOcclusionCulling() {
    std::cout << "[DEBUG] Performing cross-chunk occlusion culling..." << std::endl;
    
    int totalCubes = 0;
    int fullyOccludedCubes = 0;
    int partiallyOccludedCubes = 0;
    int totalHiddenFaces = 0;
    
    // Process each chunk
    for (auto& chunk : chunks) {
        totalCubes += chunk.numInstances;
        
        // Update face masks for all cubes in this chunk based on cross-chunk neighbors
        for (size_t i = 0; i < chunk.cubes.size(); ++i) {
            // Extract relative position from packed data
            uint32_t x = (chunk.cubes[i].packedData >> 0) & 0x1F;
            uint32_t y = (chunk.cubes[i].packedData >> 5) & 0x1F;
            uint32_t z = (chunk.cubes[i].packedData >> 10) & 0x1F;
            
            // Calculate new face mask with cross-chunk occlusion culling
            uint32_t newFaceMask = calculateOcclusionFaceMask(chunk.worldOrigin, x, y, z);
            
            // Count visible faces for statistics
            int visibleFaces = 0;
            for (int face = 0; face < 6; ++face) {
                if (newFaceMask & (1 << face)) {
                    visibleFaces++;
                }
            }
            
            // Update statistics
            if (visibleFaces == 0) {
                fullyOccludedCubes++;
                totalHiddenFaces += 6;
            } else if (visibleFaces < 6) {
                partiallyOccludedCubes++;
                totalHiddenFaces += (6 - visibleFaces);
            }
            
            // Update packed data with new face mask
            chunk.cubes[i].packedData = (x & 0x1F) | ((y & 0x1F) << 5) | ((z & 0x1F) << 10) | (newFaceMask << 15);
        }
        
        // Update GPU buffer with new face mask data
        if (chunk.mappedMemory) {
            VkDeviceSize bufferSize = sizeof(InstanceData) * chunk.cubes.size();
            memcpy(chunk.mappedMemory, chunk.cubes.data(), bufferSize);
        }
    }
    
    std::cout << "[DEBUG] Occlusion culling complete: " << totalCubes << " total cubes, " 
              << fullyOccludedCubes << " fully occluded, " 
              << partiallyOccludedCubes << " partially occluded, "
              << totalHiddenFaces << " hidden faces" << std::endl;
}

} // namespace VulkanCube
