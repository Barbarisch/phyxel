#include "core/ChunkManager.h"
#include <stdexcept>
#include <cstring>
#include <random>
#include <iostream>
#include <algorithm>  // for std::find

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
        auto chunk = std::make_unique<Chunk>(origin);
        
        // Initialize with Vulkan device
        chunk->initialize(device, physicalDevice);
        
        // Add to spatial hash map for O(1) lookup
        glm::ivec3 chunkCoord = worldToChunkCoord(origin);
        chunkMap[chunkCoord] = chunk.get();
        
        // Fill chunk with 32x32x32 cubes at relative positions
        chunk->populateWithCubes();
        
        // Generate face instances from cubes (initial pass - no cross-chunk culling yet)
        chunk->rebuildFaces();
        
        // Create Vulkan buffer for this chunk
        chunk->createVulkanBuffer();
        
        chunks.push_back(std::move(chunk));
    }
    
    // Second pass: Rebuild all chunks with cross-chunk culling now that all chunks exist
    for (auto& chunk : chunks) {
        rebuildChunkFacesWithCrosschunkCulling(*chunk);
        chunk->updateVulkanBuffer();  // Update the GPU buffer with new face data
    }
}

void ChunkManager::createChunk(const glm::ivec3& origin) {
    auto chunk = std::make_unique<Chunk>(origin);
    
    // Initialize with Vulkan device
    chunk->initialize(device, physicalDevice);
    
    // Add to spatial hash map for O(1) lookup
    glm::ivec3 chunkCoord = worldToChunkCoord(origin);
    chunkMap[chunkCoord] = chunk.get();
    
    chunk->populateWithCubes();
    
    // Generate face instances from cubes (initial pass)
    chunk->rebuildFaces();
    
    chunk->createVulkanBuffer();
    
    chunks.push_back(std::move(chunk));
    
    // Update cross-chunk culling for this chunk and its neighbors
    Chunk* newChunk = chunks.back().get();
    rebuildChunkFacesWithCrosschunkCulling(*newChunk);
    newChunk->updateVulkanBuffer();
    
    // Also update adjacent chunks to account for the new chunk
    glm::ivec3 chunkCoordOfNew = worldToChunkCoord(origin);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue; // Skip the new chunk itself
                
                glm::ivec3 adjacentCoord = chunkCoordOfNew + glm::ivec3(dx, dy, dz);
                Chunk* adjacentChunk = getChunkAtCoord(adjacentCoord);
                if (adjacentChunk) {
                    rebuildChunkFacesWithCrosschunkCulling(*adjacentChunk);
                    adjacentChunk->updateVulkanBuffer();
                }
            }
        }
    }
}

void ChunkManager::updateChunk(size_t chunkIndex) {
    if (chunkIndex >= chunks.size()) return;
    
    Chunk* chunk = chunks[chunkIndex].get();
    if (chunk->getNeedsUpdate()) {
        // Rebuild faces with cross-chunk culling to reflect any cube color changes
        rebuildChunkFacesWithCrosschunkCulling(*chunk);
        
        // Update GPU buffer with new face data
        chunk->updateVulkanBuffer();
        chunk->setNeedsUpdate(false);
    }
}

void ChunkManager::updateDirtyChunks() {
    // Early exit if no chunks need updating
    if (!hasDirtyChunks || dirtyChunkIndices.empty()) {
        return;
    }
    
    // Update only the chunks that have been marked as dirty
    for (size_t chunkIndex : dirtyChunkIndices) {
        if (chunkIndex < chunks.size()) {
            updateChunk(chunkIndex);
        }
    }
    
    // Clear the dirty list after updating
    clearDirtyChunkList();
}

void ChunkManager::updateAllChunks() {
    // DEPRECATED: This method is inefficient for large worlds
    // It's kept for backward compatibility but updateDirtyChunks() should be used instead
    
    // Collect all chunks that actually need updating to avoid unnecessary work
    dirtyChunkIndices.clear();
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i]->getNeedsUpdate()) {
            dirtyChunkIndices.push_back(i);
        }
    }
    
    if (!dirtyChunkIndices.empty()) {
        hasDirtyChunks = true;
        updateDirtyChunks();
    }
}

void ChunkManager::rebuildChunkFaces(Chunk& chunk) {
    // The Chunk class now handles face rebuilding internally
    chunk.rebuildFaces();
    chunk.setNeedsUpdate(true);  // Mark for GPU buffer update
}

void ChunkManager::rebuildChunkFacesWithCrosschunkCulling(Chunk& chunk) {
    chunk.faces.clear();
    
    // Get chunk's world origin for cross-chunk lookups
    glm::ivec3 chunkOrigin = chunk.getWorldOrigin();
    
    // Face culling with cross-chunk adjacency checks
    for (size_t cubeIndex = 0; cubeIndex < chunk.cubes.size(); ++cubeIndex) {
        const Cube* cube = chunk.cubes[cubeIndex];
        
        // Skip deleted cubes (nullptr)
        if (!cube) continue;
        
        // Calculate which faces are visible by checking adjacent positions
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // Face directions: 0=front(+Z), 1=back(-Z), 2=right(+X), 3=left(-X), 4=top(+Y), 5=bottom(-Y)
        glm::ivec3 cubePos = cube->getPosition();
        glm::ivec3 localNeighbors[6] = {
            cubePos + glm::ivec3(0, 0, 1),   // front (+Z)
            cubePos + glm::ivec3(0, 0, -1),  // back (-Z)
            cubePos + glm::ivec3(1, 0, 0),   // right (+X)
            cubePos + glm::ivec3(-1, 0, 0),  // left (-X)
            cubePos + glm::ivec3(0, 1, 0),   // top (+Y)
            cubePos + glm::ivec3(0, -1, 0)   // bottom (-Y)
        };
        
        // Check each face for occlusion by adjacent cubes
        for (int faceID = 0; faceID < 6; ++faceID) {
            glm::ivec3 neighborLocalPos = localNeighbors[faceID];
            glm::ivec3 neighborWorldPos = chunkOrigin + neighborLocalPos;
            
            // Check if neighbor position is within current chunk bounds
            if (neighborLocalPos.x >= 0 && neighborLocalPos.x < 32 &&
                neighborLocalPos.y >= 0 && neighborLocalPos.y < 32 &&
                neighborLocalPos.z >= 0 && neighborLocalPos.z < 32) {
                
                // Neighbor is within same chunk - check directly
                const Cube* neighborCube = chunk.getCubeAt(neighborLocalPos);
                if (neighborCube && neighborCube->getColor().r >= 0.0f) {
                    faceVisible[faceID] = false;
                }
            } else {
                // Neighbor is outside chunk bounds - check in adjacent chunk
                glm::ivec3 neighborChunkCoord = worldToChunkCoord(neighborWorldPos);
                Chunk* neighborChunk = getChunkAtCoord(neighborChunkCoord);
                
                if (neighborChunk) {
                    glm::ivec3 neighborLocalInAdjacentChunk = worldToLocalCoord(neighborWorldPos);
                    
                    // Debug output for boundary cubes
                    if ((cubePos.x == 0 || cubePos.x == 31 || 
                         cubePos.y == 0 || cubePos.y == 31 || 
                         cubePos.z == 0 || cubePos.z == 31) && 
                        cubeIndex < 10) { // Only log first few boundary cubes to avoid spam
                        // std::cout << "[DEBUG] Boundary cube check: chunk origin(" 
                        //           << chunkOrigin.x << "," << chunkOrigin.y << "," << chunkOrigin.z 
                        //           << ") cube local(" << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                        //           << ") face " << faceID << " neighbor world(" << neighborWorldPos.x << "," << neighborWorldPos.y << "," << neighborWorldPos.z
                        //           << ") neighbor chunk coord(" << neighborChunkCoord.x << "," << neighborChunkCoord.y << "," << neighborChunkCoord.z
                        //           << ") neighbor local(" << neighborLocalInAdjacentChunk.x << "," << neighborLocalInAdjacentChunk.y << "," << neighborLocalInAdjacentChunk.z 
                        //           << ") neighbor chunk origin(" << neighborChunk->getWorldOrigin().x << "," << neighborChunk->getWorldOrigin().y << "," << neighborChunk->getWorldOrigin().z << ")" << std::endl;
                    }
                    
                    const Cube* neighborCube = neighborChunk->getCubeAt(neighborLocalInAdjacentChunk);
                    if (neighborCube) {
                        faceVisible[faceID] = false;
                        
                        // Debug successful culling
                        // if ((cubePos.x == 0 || cubePos.x == 31 || 
                        //      cubePos.y == 0 || cubePos.y == 31 || 
                        //      cubePos.z == 0 || cubePos.z == 31) && 
                        //     cubeIndex < 5) {
                        //     std::cout << "[DEBUG] Successfully culled face " << faceID << " for boundary cube" << std::endl;
                        // }
                    }
                } else {
                    // Debug when no adjacent chunk is found - this is correct behavior for world edges
                    // if ((cubePos.x == 0 || cubePos.x == 31 || 
                    //      cubePos.y == 0 || cubePos.y == 31 || 
                    //      cubePos.z == 0 || cubePos.z == 31) && 
                    //     cubeIndex < 5) {
                    //     std::cout << "[DEBUG] No adjacent chunk found for neighbor world(" 
                    //               << neighborWorldPos.x << "," << neighborWorldPos.y << "," << neighborWorldPos.z 
                    //               << ") chunk coord(" << neighborChunkCoord.x << "," << neighborChunkCoord.y << "," << neighborChunkCoord.z 
                    //               << ") - face remains visible (world edge)" << std::endl;
                    // }
                }
                // If no adjacent chunk exists, face remains visible (edge of world)
            }
        }
        
        // Generate instance data for each visible face
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack cube position (5 bits each) and face ID (3 bits)
                faceInstance.packedData = (cubePos.x & 0x1F) | ((cubePos.y & 0x1F) << 5) | 
                                         ((cubePos.z & 0x1F) << 10) | ((faceID & 0x7) << 15);
                
                faceInstance.color = cube->getColor();
                chunk.faces.push_back(faceInstance);
            }
        }
    }
    
    chunk.numInstances = static_cast<uint32_t>(chunk.faces.size());
    chunk.setNeedsUpdate(true);
    
    // std::cout << "[CHUNKMANAGER] Rebuilt faces with cross-chunk culling for chunk at origin (" 
    //           << chunkOrigin.x << "," << chunkOrigin.y << "," << chunkOrigin.z 
    //           << "), generated " << chunk.numInstances << " visible faces" << std::endl;
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
    
    // Step 2: Calculate local position
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    
    // Step 3: Use Chunk class's getCubeAt method
    return chunk->getCubeAt(localPos);
}

bool ChunkManager::setCubeColorFast(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    bool result = chunk->setCubeColor(localPos, color);
    
    if (result) {
        markChunkDirty(chunk);
    }
    
    return result;
}

bool ChunkManager::removeCubeFast(const glm::ivec3& worldPos) {
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    bool result = chunk->removeCube(localPos);
    
    if (result) {
        markChunkDirty(chunk);
        // Note: Face regeneration would happen in updateChunk()
    }
    
    return result;
}

bool ChunkManager::addCubeFast(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return false;  // No chunk exists at this position
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    bool result = chunk->addCube(localPos, color);
    
    if (!result) {
        // If addCube failed, try to set color of existing cube
        result = chunk->setCubeColor(localPos, color);
    }
    
    if (result) {
        markChunkDirty(chunk);
        // Note: Face regeneration would happen in updateChunk()
    }
    
    return result;
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

void ChunkManager::setCubeColorEfficient(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return;
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    
    // Use the new efficient partial update method (no markChunkDirty needed)
    chunk->updateSingleCubeColor(localPos, color);
}

bool ChunkManager::removeCube(const glm::ivec3& worldPos) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    
    // Use the Chunk class's removeCube method
    bool result = chunk->removeCube(localPos);
    if (result) {
        rebuildChunkFaces(*chunk);
    }
    return result;
}

bool ChunkManager::addCube(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    
    // Use the Chunk class's addCube or setCubeColor method
    bool result = chunk->addCube(localPos, color);
    if (!result) {
        // If addCube failed, try to update existing cube color
        result = chunk->setCubeColor(localPos, color);
    }
    
    if (result) {
        rebuildChunkFaces(*chunk);
    }
    return result;
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
        // The Chunk class now handles its own cleanup
        // No need to manually clean up Vulkan resources
    }
    chunks.clear();
    chunkMap.clear();  // Clear the spatial hash map
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
        stats.totalVisibleFaces += chunk->getNumInstances();
        
        // Calculate vertices (4 vertices per face for quad rendering)
        stats.totalVertices += chunk->getNumInstances() * 4;
        
        // Count cubes and estimate hidden faces
        uint32_t totalPossibleCubes = 32 * 32 * 32;
        stats.totalCubes += totalPossibleCubes;
        
        // Estimate hidden faces (total possible faces - visible faces)
        uint32_t totalPossibleFaces = totalPossibleCubes * 6;
        if (chunk->getNumInstances() < totalPossibleFaces) {
            stats.totalHiddenFaces += (totalPossibleFaces - chunk->getNumInstances());
        }
        
        // Count occlusion types based on visible faces per cube
        // This is an approximation since we don't track individual cubes anymore
        for (uint32_t cubeIdx = 0; cubeIdx < totalPossibleCubes; ++cubeIdx) {
            // Rough estimate: assume evenly distributed face visibility
            float avgVisibleFacesPerCube = float(chunk->getNumInstances()) / float(totalPossibleCubes);
            
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
        if (chunk->getWorldOrigin() == chunkOrigin) {
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
        // Regenerate face instances with cross-chunk occlusion culling
        rebuildChunkFacesWithCrosschunkCulling(*chunk);
        
        // Update GPU buffer with new face data
        chunk->updateVulkanBuffer();
    }
    
    // Calculate statistics
    ChunkStats stats = getPerformanceStats();
    std::cout << "[DEBUG] Occlusion culling complete: " << stats.totalCubes << " total cubes, " 
              << stats.totalVisibleFaces << " visible faces, "
              << stats.totalHiddenFaces << " hidden faces" << std::endl;
}

// ===============================================================
// DIRTY CHUNK TRACKING OPTIMIZATION
// ===============================================================

void ChunkManager::markChunkDirty(size_t chunkIndex) {
    if (chunkIndex >= chunks.size()) return;
    
    // Mark the chunk as needing update
    chunks[chunkIndex]->setNeedsUpdate(true);
    
    // Add to dirty list if not already present
    if (std::find(dirtyChunkIndices.begin(), dirtyChunkIndices.end(), chunkIndex) == dirtyChunkIndices.end()) {
        dirtyChunkIndices.push_back(chunkIndex);
        hasDirtyChunks = true;
    }
}

void ChunkManager::markChunkDirty(Chunk* chunk) {
    if (!chunk) return;
    
    size_t chunkIndex = getChunkIndex(chunk);
    if (chunkIndex != SIZE_MAX) {
        markChunkDirty(chunkIndex);
    }
}

void ChunkManager::clearDirtyChunkList() {
    dirtyChunkIndices.clear();
    hasDirtyChunks = false;
}

size_t ChunkManager::getChunkIndex(const Chunk* chunk) const {
    // Find the index of a chunk in the chunks vector
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].get() == chunk) {
            return i;
        }
    }
    return SIZE_MAX; // Invalid index if not found
}

} // namespace VulkanCube
