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
    chunkMap.clear();  // Clear the spatial hash map
    chunks.reserve(origins.size());
    
    for (const auto& origin : origins) {
        chunks.emplace_back(origin);
        Chunk& chunk = chunks.back();
        
        // Add to spatial hash map for O(1) lookup
        glm::ivec3 chunkCoord = worldToChunkCoord(origin);
        chunkMap[chunkCoord] = &chunk;
        
        // Fill chunk with 32x32x32 cubes at relative positions
        populateChunk(chunk);
        
        // Create Vulkan buffer for this chunk
        createChunkBuffer(chunk);
    }
}

void ChunkManager::createChunk(const glm::ivec3& origin) {
    chunks.emplace_back(origin);
    Chunk& chunk = chunks.back();
    
    // Add to spatial hash map for O(1) lookup
    glm::ivec3 chunkCoord = worldToChunkCoord(origin);
    chunkMap[chunkCoord] = &chunk;
    
    populateChunk(chunk);
    createChunkBuffer(chunk);
}

void ChunkManager::populateChunk(Chunk& chunk) {
    chunk.cubes.clear();
    chunk.faces.clear();
    
    // Random number generator for colors
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
    
    // Step 1: Create logical cubes (32x32x32 grid)
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            for (int z = 0; z < 32; ++z) {
                Cube cube;
                cube.position = glm::ivec3(x, y, z);  // Relative position within chunk
                cube.color = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                chunk.cubes.push_back(cube);
            }
        }
    }
    
    // Step 2: Generate faces from cubes (only for visible faces)
    for (size_t cubeIndex = 0; cubeIndex < chunk.cubes.size(); ++cubeIndex) {
        const Cube& cube = chunk.cubes[cubeIndex];
        
        // Calculate which faces are visible using cross-chunk occlusion
        uint32_t faceMask = calculateOcclusionFaceMask(chunk.worldOrigin, cube.position.x, cube.position.y, cube.position.z);
        
        // Generate instance data for each visible face
        // Face IDs: 0=front(+Z), 1=back(-Z), 2=right(+X), 3=left(-X), 4=top(+Y), 5=bottom(-Y)
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceMask & (1u << faceID)) {
                InstanceData faceInstance;
                
                // Pack cube position (5 bits each) and face ID (3 bits)
                // Bit layout: [0-4]=x, [5-9]=y, [10-14]=z, [15-17]=faceID, [18-31]=future
                faceInstance.packedData = (cube.position.x & 0x1F) | ((cube.position.y & 0x1F) << 5) | 
                                         ((cube.position.z & 0x1F) << 10) | ((faceID & 0x7) << 15);
                
                faceInstance.color = cube.color;
                chunk.faces.push_back(faceInstance);
            }
        }
    }
    
    chunk.numInstances = static_cast<uint32_t>(chunk.faces.size());
    std::cout << "[DEBUG] Chunk at (" << chunk.worldOrigin.x << "," << chunk.worldOrigin.y << "," << chunk.worldOrigin.z 
              << ") created " << chunk.cubes.size() << " cubes, " << chunk.numInstances << " visible faces" << std::endl;
}

void ChunkManager::createChunkBuffer(Chunk& chunk) {
    if (device == VK_NULL_HANDLE) {
        throw std::runtime_error("ChunkManager not initialized with Vulkan device!");
    }
    
    VkDeviceSize bufferSize = sizeof(InstanceData) * chunk.faces.size();
    
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
    memcpy(chunk.mappedMemory, chunk.faces.data(), bufferSize);
}

void ChunkManager::updateChunk(size_t chunkIndex) {
    if (chunkIndex >= chunks.size()) return;
    
    Chunk& chunk = chunks[chunkIndex];
    if (chunk.needsUpdate && chunk.mappedMemory) {
        // Rebuild faces to reflect any cube color changes
        rebuildChunkFaces(chunk);
        
        // Copy updated face data to GPU memory
        VkDeviceSize bufferSize = sizeof(InstanceData) * chunk.faces.size();
        memcpy(chunk.mappedMemory, chunk.faces.data(), bufferSize);
        chunk.needsUpdate = false;
    }
}

void ChunkManager::updateAllChunks() {
    for (size_t i = 0; i < chunks.size(); ++i) {
        updateChunk(i);
    }
}

void ChunkManager::rebuildChunkFaces(Chunk& chunk) {
    chunk.faces.clear();
    
    // Generate faces from existing cubes (only for visible faces)
    for (size_t cubeIndex = 0; cubeIndex < chunk.cubes.size(); ++cubeIndex) {
        const Cube& cube = chunk.cubes[cubeIndex];
        
        // Calculate which faces are visible using cross-chunk occlusion
        uint32_t faceMask = calculateOcclusionFaceMask(chunk.worldOrigin, cube.position.x, cube.position.y, cube.position.z);
        
        // Generate instance data for each visible face
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceMask & (1u << faceID)) {
                InstanceData faceInstance;
                
                // Pack cube position (5 bits each) and face ID (3 bits)
                faceInstance.packedData = (cube.position.x & 0x1F) | ((cube.position.y & 0x1F) << 5) | 
                                         ((cube.position.z & 0x1F) << 10) | ((faceID & 0x7) << 15);
                
                faceInstance.color = cube.color;
                chunk.faces.push_back(faceInstance);
            }
        }
    }
    
    chunk.numInstances = static_cast<uint32_t>(chunk.faces.size());
    chunk.needsUpdate = true;  // Mark for GPU buffer update
}

// ===============================================================
// OPTIMIZED O(1) CHUNK AND CUBE LOOKUP FUNCTIONS
// ===============================================================

Chunk* ChunkManager::getChunkAtCoord(const glm::ivec3& chunkCoord) {
    auto it = chunkMap.find(chunkCoord);
    return (it != chunkMap.end()) ? it->second : nullptr;
}

Chunk* ChunkManager::getChunkAtFast(const glm::ivec3& worldPos) {
    glm::ivec3 chunkCoord = ChunkManager::worldToChunkCoord(worldPos);
    return getChunkAtCoord(chunkCoord);
}

Cube* ChunkManager::getCubeAtFast(const glm::ivec3& worldPos) {
    // Step 1: Get chunk using O(1) hash map lookup
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return nullptr;
    
    // Step 2: Calculate local position and index
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    
    // Bounds check
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return nullptr;
    }
    
    // Step 3: O(1) array access using 3D to 1D index conversion
    size_t index = ChunkManager::localToIndex(localPos);
    if (index >= chunk->cubes.size()) return nullptr;
    
    return &chunk->cubes[index];
}

bool ChunkManager::setCubeColorFast(const glm::ivec3& worldPos, const glm::vec3& color) {
    Cube* cube = getCubeAtFast(worldPos);
    if (!cube) return false;
    
    cube->color = color;
    
    // Mark chunk for update
    Chunk* chunk = getChunkAtFast(worldPos);
    if (chunk) {
        chunk->needsUpdate = true;
    }
    
    return true;
}

bool ChunkManager::removeCubeFast(const glm::ivec3& worldPos) {
    Cube* cube = getCubeAtFast(worldPos);
    if (!cube) return false;
    
    // Mark cube as removed (negative red value indicates removal)
    cube->color.r = -1.0f;
    
    // Mark chunk for update and face regeneration
    Chunk* chunk = getChunkAtFast(worldPos);
    if (chunk) {
        chunk->needsUpdate = true;
        // Note: Face regeneration would happen in updateChunk()
    }
    
    return true;
}

bool ChunkManager::addCubeFast(const glm::ivec3& worldPos, const glm::vec3& color) {
    Cube* cube = getCubeAtFast(worldPos);
    if (!cube) return false;  // No chunk exists at this position
    
    // Restore cube (in case it was previously removed)
    cube->color = color;
    
    // Mark chunk for update and face regeneration
    Chunk* chunk = getChunkAtFast(worldPos);
    if (chunk) {
        chunk->needsUpdate = true;
        // Note: Face regeneration would happen in updateChunk()
    }
    
    return true;
}

// ===============================================================
// LEGACY FUNCTIONS (kept for backward compatibility)
// ===============================================================

Chunk* ChunkManager::getChunkAt(const glm::ivec3& worldPos) {
    // Redirect to optimized version
    return getChunkAtFast(worldPos);
}

Cube* ChunkManager::getCubeAt(const glm::ivec3& worldPos) {
    // Redirect to optimized version
    return getCubeAtFast(worldPos);
}

void ChunkManager::setCubeColor(const glm::ivec3& worldPos, const glm::vec3& color) {
    // Redirect to optimized version
    setCubeColorFast(worldPos, color);
}

bool ChunkManager::removeCube(const glm::ivec3& worldPos) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    size_t index = ChunkManager::localToIndex(localPos);
    
    if (index < chunk->cubes.size()) {
        // For now, we'll mark cube as "empty" by setting alpha to 0
        // In a more advanced system, you might use a sparse representation
        chunk->cubes[index].color.r = -1.0f; // Use negative red as "empty" marker
        
        rebuildChunkFaces(*chunk);
        return true;
    }
    
    return false;
}

bool ChunkManager::addCube(const glm::ivec3& worldPos, const glm::vec3& color) {
    Cube* cube = getCubeAt(worldPos);
    if (cube) {
        // Cube already exists, just change color
        cube->color = color;
        
        Chunk* chunk = getChunkAt(worldPos);
        if (chunk) {
            rebuildChunkFaces(*chunk);
        }
        return true;
    }
    
    // For full chunks, all positions already have cubes
    // This would be more complex with sparse chunks
    return false;
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
    chunkMap.clear();  // Clear the spatial hash map
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
    // NOTE: This function is no longer needed with CPU pre-filtering
    // Face culling is now performed during chunk population in populateChunk()
    // which calls calculateOcclusionFaceMask() for each cube
    
    std::cout << "[DEBUG] calculateChunkFaceCulling: No longer needed with CPU pre-filtering" << std::endl;
}

ChunkManager::ChunkStats ChunkManager::getPerformanceStats() const {
    ChunkStats stats;
    
    for (const auto& chunk : chunks) {
        // Each face instance represents one visible face
        stats.totalVisibleFaces += chunk.numInstances;
        
        // Calculate vertices (4 vertices per face for quad rendering)
        stats.totalVertices += chunk.numInstances * 4;
        
        // Count cubes and estimate hidden faces
        uint32_t totalPossibleCubes = 32 * 32 * 32;
        stats.totalCubes += totalPossibleCubes;
        
        // Estimate hidden faces (total possible faces - visible faces)
        uint32_t totalPossibleFaces = totalPossibleCubes * 6;
        if (chunk.numInstances < totalPossibleFaces) {
            stats.totalHiddenFaces += (totalPossibleFaces - chunk.numInstances);
        }
        
        // Count occlusion types based on visible faces per cube
        // This is an approximation since we don't track individual cubes anymore
        for (uint32_t cubeIdx = 0; cubeIdx < totalPossibleCubes; ++cubeIdx) {
            // Rough estimate: assume evenly distributed face visibility
            float avgVisibleFacesPerCube = float(chunk.numInstances) / float(totalPossibleCubes);
            
            if (avgVisibleFacesPerCube == 0.0f) {
                stats.fullyOccludedCubes++;
            } else if (avgVisibleFacesPerCube < 6.0f) {
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
    std::cout << "[DEBUG] Regenerating chunks with updated cross-chunk occlusion culling..." << std::endl;
    
    // With CPU pre-filtering, we need to regenerate all chunk geometry
    // when occlusion relationships change between chunks
    for (auto& chunk : chunks) {
        // Regenerate face instances with updated occlusion data
        populateChunk(chunk);
        
        // Update GPU buffer with new face data
        if (chunk.mappedMemory) {
            VkDeviceSize bufferSize = sizeof(InstanceData) * chunk.faces.size();
            memcpy(chunk.mappedMemory, chunk.faces.data(), bufferSize);
        }
    }
    
    // Calculate statistics
    ChunkStats stats = getPerformanceStats();
    std::cout << "[DEBUG] Occlusion culling complete: " << stats.totalCubes << " total cubes, " 
              << stats.totalVisibleFaces << " visible faces, "
              << stats.totalHiddenFaces << " hidden faces" << std::endl;
}

} // namespace VulkanCube
