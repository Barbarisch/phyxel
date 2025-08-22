#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include <stdexcept>
#include <cstring>
#include <random>
#include <iostream>
#include <algorithm>  // for std::find
#include <set>        // for std::set in selective updates

// Bullet Physics includes
#include <btBulletDynamicsCommon.h>

namespace VulkanCube {

void ChunkManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
}

void ChunkManager::setPhysicsWorld(Physics::PhysicsWorld* physics) {
    physicsWorld = physics;
    std::cout << "[CHUNK MANAGER] Physics world set for proper dynamic object cleanup" << std::endl;
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
        std::cout << "[CHUNK_MANAGER] Updating chunk " << chunkIndex << " with " << chunk->getTotalSubcubeCount() << " subcubes" << std::endl;
        
        // Use cross-chunk culling method to maintain proper face occlusion across chunk boundaries
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
    // Use cross-chunk culling method to maintain proper face occlusion across chunk boundaries
    rebuildChunkFacesWithCrosschunkCulling(chunk);
    chunk.setNeedsUpdate(true);  // Mark for GPU buffer update
}

void ChunkManager::rebuildChunkFacesWithCrosschunkCulling(Chunk& chunk) {
    chunk.faces.clear();
    
    // Get chunk's world origin for cross-chunk lookups
    glm::ivec3 chunkOrigin = chunk.getWorldOrigin();
    
    // Face culling with cross-chunk adjacency checks
    for (size_t cubeIndex = 0; cubeIndex < chunk.cubes.size(); ++cubeIndex) {
        const Cube* cube = chunk.cubes[cubeIndex];
        
        // Skip deleted cubes (nullptr) or hidden cubes (subdivided)
        if (!cube || !cube->isVisible()) continue;
        
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
                if (neighborCube && neighborCube->isVisible()) {
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
                    if (neighborCube && neighborCube->isVisible()) {
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
                // Convert world position to chunk-relative position
                glm::ivec3 cubeChunkPos = cubePos - chunkOrigin;
                uint32_t subcubeData = 0; // Regular cube: subcube_flag=0, rest=0
                
                faceInstance.packedData = (cubeChunkPos.x & 0x1F) | ((cubeChunkPos.y & 0x1F) << 5) | 
                                         ((cubeChunkPos.z & 0x1F) << 10) | ((faceID & 0x7) << 15) |
                                         (subcubeData << 18);
                
                faceInstance.color = cube->getColor();
                chunk.faces.push_back(faceInstance);
            }
        }
    }
    
    // ========================================================================
    // PHASE 2: Process static subcubes (from subdivided cubes)
    // ========================================================================
    const auto& staticSubcubes = chunk.getStaticSubcubes();
    for (const Subcube* subcube : staticSubcubes) {
        // Skip broken or hidden subcubes (broken subcubes should be in dynamic list)
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) {
            continue;
        }
        
        // Get subcube properties
        glm::ivec3 parentPos = subcube->getPosition();     // Parent cube's world position
        glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis within parent
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentChunkPos = parentPos - chunkOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentChunkPos.x < 0 || parentChunkPos.x >= 32 ||
            parentChunkPos.y < 0 || parentChunkPos.y >= 32 ||
            parentChunkPos.z < 0 || parentChunkPos.z >= 32) {
            continue; // Skip subcubes with invalid parent positions
        }
        
        // Calculate which faces are visible by checking adjacent subcubes/cubes
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // For subcubes, we need more sophisticated occlusion culling:
        // - Check against other subcubes in the same parent cube
        // - Check against neighboring cubes/subcubes
        // For now, simplified: assume all subcube faces are visible (we can optimize later)
        
        // Generate instance data for each visible face of the subcube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack parent cube position (5 bits each), face ID (3 bits), and subcube data
                // Bit layout: [0-4]=parent_x, [5-9]=parent_y, [10-14]=parent_z, [15-17]=faceID, 
                //             [18]=subcube_flag(1), [19-20]=local_x, [21-22]=local_y, [23-24]=local_z, [25-31]=reserved
                uint32_t subcubeData = (1 << 0) |                           // subcube_flag = 1
                                      ((localPos.x & 0x3) << 1) |          // local_x (2 bits)
                                      ((localPos.y & 0x3) << 3) |          // local_y (2 bits)
                                      ((localPos.z & 0x3) << 5);           // local_z (2 bits)
                
                faceInstance.packedData = (parentChunkPos.x & 0x1F) | ((parentChunkPos.y & 0x1F) << 5) | 
                                         ((parentChunkPos.z & 0x1F) << 10) | ((faceID & 0x7) << 15) |
                                         (subcubeData << 18);
                
                faceInstance.color = subcube->getColor();
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

const Chunk* ChunkManager::getChunkAtCoord(const glm::ivec3& chunkCoord) const {
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
        // Use efficient selective update instead of full chunk rebuild
        updateAfterCubeBreak(worldPos);
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
        // Use efficient selective update instead of full chunk rebuild
        updateAfterCubePlace(worldPos);
    }
    return result;
}

Subcube* ChunkManager::getSubcubeAt(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return nullptr;
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    Cube* cube = chunk->getCubeAt(localPos);
    if (!cube || !cube->isSubdivided()) return nullptr;
    
    return cube->getSubcubeAt(subcubePos);
}

void ChunkManager::setSubcubeColorEfficient(const glm::ivec3& worldPos, const glm::ivec3& subcubePos, const glm::vec3& color) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return;
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    Cube* cube = chunk->getCubeAt(localPos);
    if (!cube || !cube->isSubdivided()) return;
    
    // Use the new efficient partial update method (similar to regular cubes)
    chunk->updateSingleSubcubeColor(localPos, subcubePos, color);
}

glm::vec3 ChunkManager::getSubcubeColor(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return glm::vec3(1.0f); // Default white
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    Cube* cube = chunk->getCubeAt(localPos);
    if (!cube || !cube->isSubdivided()) return glm::vec3(1.0f);
    
    Subcube* subcube = cube->getSubcubeAt(subcubePos);
    if (subcube) {
        return subcube->getColor();
    }
    return glm::vec3(1.0f); // Default white
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

void ChunkManager::addGlobalDynamicSubcube(std::unique_ptr<Subcube> subcube) {
    if (subcube) {
        std::cout << "[CHUNK MANAGER] Adding global dynamic subcube at world position: ("
                  << subcube->getPosition().x << "," << subcube->getPosition().y << "," << subcube->getPosition().z << ")" << std::endl;
        globalDynamicSubcubes.push_back(std::move(subcube));
        rebuildGlobalDynamicFaces();  // Rebuild faces after adding new subcube
    }
}

void ChunkManager::rebuildGlobalDynamicSubcubeFaces() {
    // Legacy function - now calls the combined function that handles both subcubes and cubes
    rebuildGlobalDynamicFaces();
}

void ChunkManager::updateGlobalDynamicSubcubes(float deltaTime) {
    // Update lifetimes and remove expired subcubes
    auto it = globalDynamicSubcubes.begin();
    size_t removedCount = 0;
    
    while (it != globalDynamicSubcubes.end()) {
        (*it)->updateLifetime(deltaTime);
        
        if ((*it)->hasExpired()) {
            // Properly remove physics body from physics world
            if (physicsWorld && (*it)->getRigidBody()) {
                std::cout << "[CHUNK MANAGER] Removing expired dynamic subcube physics body" << std::endl;
                physicsWorld->removeCube((*it)->getRigidBody());
            }
            
            removedCount++;
            // Note: The unique_ptr destructor will automatically clean up the subcube
            it = globalDynamicSubcubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Rebuild faces if any subcubes were removed
    if (removedCount > 0) {
        std::cout << "[CHUNK MANAGER] Removed " << removedCount << " expired dynamic subcubes (lifetime ended)" << std::endl;
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::updateGlobalDynamicSubcubePositions() {
    // Update positions and rotations of global dynamic subcubes from their physics bodies
    bool transformsChanged = false;
    static int debugCounter = 0;
    static bool firstUpdate = true;
    
    if (firstUpdate && !globalDynamicSubcubes.empty()) {
        std::cout << "[SUBCUBE POSITION] ===== FIRST SUBCUBE PHYSICS UPDATE =====" << std::endl;
        std::cout << "[SUBCUBE POSITION] Found " << globalDynamicSubcubes.size() << " dynamic subcubes to track" << std::endl;
        firstUpdate = false;
    }
    
    for (auto& subcube : globalDynamicSubcubes) {
        if (subcube && subcube->getRigidBody()) {
            btRigidBody* body = subcube->getRigidBody();
            btTransform transform = body->getWorldTransform();
            
            // Get current stored position before update
            glm::vec3 oldStoredPos = subcube->getPhysicsPosition();
            
            // Get the physics world position
            btVector3 btPos = transform.getOrigin();
            glm::vec3 newWorldPos(btPos.x(), btPos.y(), btPos.z());
            
            // Get the physics rotation (quaternion)
            btQuaternion btRot = transform.getRotation();
            glm::vec4 newRotation(btRot.x(), btRot.y(), btRot.z(), btRot.w());
            
            // Store the smooth floating-point physics position and rotation
            subcube->setPhysicsPosition(newWorldPos);
            subcube->setPhysicsRotation(newRotation);
            transformsChanged = true;
            
            // Enhanced position tracking - show movement from initial spawn position every 60 frames
            if (debugCounter % 60 == 0) {
                glm::vec3 movement = newWorldPos - oldStoredPos;
                float movementMag = glm::length(movement);
                
                std::cout << "[SUBCUBE POSITION] ===== AFTER PHYSICS SIMULATION =====" << std::endl;
                std::cout << "[SUBCUBE POSITION] 8. Physics body final position: (" 
                          << newWorldPos.x << ", " << newWorldPos.y << ", " << newWorldPos.z << ")" << std::endl;
                std::cout << "[SUBCUBE POSITION] 9. Movement from last update: (" 
                          << movement.x << ", " << movement.y << ", " << movement.z << ") magnitude: " << movementMag << std::endl;
                std::cout << "[SUBCUBE POSITION] 10. Rotation: (" 
                          << newRotation.x << ", " << newRotation.y << ", " << newRotation.z << ", " << newRotation.w << ")" << std::endl;
                std::cout << "[SUBCUBE POSITION] ===== END SUBCUBE POSITION TRACKING =====" << std::endl;
                break; // Only log first subcube
            }
        }
    }
    
    debugCounter++;
    
    // Rebuild face data if any transforms changed
    if (transformsChanged) {
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::clearAllGlobalDynamicSubcubes() {
    //std::cout << "[CHUNK MANAGER] Clearing all " << globalDynamicSubcubes.size() << " global dynamic subcubes" << std::endl;
    globalDynamicSubcubes.clear();
}

// ===============================================================
// GLOBAL DYNAMIC CUBE MANAGEMENT
// ===============================================================

void ChunkManager::addGlobalDynamicCube(std::unique_ptr<DynamicCube> cube) {
    if (cube) {
        std::cout << "[CHUNK MANAGER] Adding global dynamic cube at world position: ("
                  << cube->getPosition().x << "," << cube->getPosition().y << "," << cube->getPosition().z << ")" << std::endl;
        globalDynamicCubes.push_back(std::move(cube));
        rebuildGlobalDynamicFaces();  // Rebuild faces after adding new cube
    }
}

void ChunkManager::updateGlobalDynamicCubes(float deltaTime) {
    // Update lifetimes and remove expired cubes
    auto it = globalDynamicCubes.begin();
    size_t removedCount = 0;
    
    while (it != globalDynamicCubes.end()) {
        (*it)->updateLifetime(deltaTime);
        
        if ((*it)->hasExpired()) {
            // Properly remove physics body from physics world
            if (physicsWorld && (*it)->getRigidBody()) {
                std::cout << "[CHUNK MANAGER] Removing expired dynamic cube physics body" << std::endl;
                physicsWorld->removeCube((*it)->getRigidBody());
            }
            
            removedCount++;
            // Note: The unique_ptr destructor will automatically clean up the cube
            it = globalDynamicCubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Rebuild faces if any cubes were removed
    if (removedCount > 0) {
        std::cout << "[CHUNK MANAGER] Removed " << removedCount << " expired dynamic cubes (lifetime ended)" << std::endl;
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::updateGlobalDynamicCubePositions() {
    // Update positions and rotations of global dynamic cubes from their physics bodies
    bool transformsChanged = false;
    static int debugCounter = 0;
    static bool firstUpdate = true;
    
    if (firstUpdate && !globalDynamicCubes.empty()) {
        std::cout << "[POSITION TRACK] ===== FIRST PHYSICS UPDATE =====" << std::endl;
        std::cout << "[POSITION TRACK] Found " << globalDynamicCubes.size() << " dynamic cubes to track" << std::endl;
        firstUpdate = false;
    }
    
    for (auto& cube : globalDynamicCubes) {
        if (cube && cube->getRigidBody()) {
            btRigidBody* body = cube->getRigidBody();
            btTransform transform = body->getWorldTransform();
            
            // Get current stored position before update
            glm::vec3 oldStoredPos = cube->getPhysicsPosition();
            
            // Get the physics world position
            btVector3 btPos = transform.getOrigin();
            glm::vec3 newWorldPos(btPos.x(), btPos.y(), btPos.z());
            
            // Get the physics rotation (quaternion)
            btQuaternion btRot = transform.getRotation();
            glm::vec4 newRotation(btRot.x(), btRot.y(), btRot.z(), btRot.w());
            
            // Store the smooth floating-point physics position and rotation
            cube->setPhysicsPosition(newWorldPos);
            cube->setPhysicsRotation(newRotation);
            transformsChanged = true;
            
            // Enhanced position tracking - show movement from initial spawn position
            if (debugCounter % 60 == 0) {
                glm::vec3 movement = newWorldPos - oldStoredPos;
                float movementMag = glm::length(movement);
                
                std::cout << "[POSITION TRACK] ===== AFTER PHYSICS SIMULATION =====" << std::endl;
                std::cout << "[POSITION TRACK] 6. Physics body final position: (" 
                          << newWorldPos.x << ", " << newWorldPos.y << ", " << newWorldPos.z << ")" << std::endl;
                std::cout << "[POSITION TRACK] 7. Movement from last update: (" 
                          << movement.x << ", " << movement.y << ", " << movement.z << ") magnitude: " << movementMag << std::endl;
                std::cout << "[POSITION TRACK] 8. Rotation: (" 
                          << newRotation.x << ", " << newRotation.y << ", " << newRotation.z << ", " << newRotation.w << ")" << std::endl;
                std::cout << "[POSITION TRACK] ===== END POSITION TRACKING =====" << std::endl;
                break; // Only log first cube
            }
        }
    }
    
    debugCounter++;
    
    // Rebuild face data if any transforms changed
    if (transformsChanged) {
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::clearAllGlobalDynamicCubes() {
    std::cout << "[CHUNK MANAGER] Clearing all " << globalDynamicCubes.size() << " global dynamic cubes" << std::endl;
    globalDynamicCubes.clear();
}

// ===============================================================
// COMBINED DYNAMIC OBJECT MANAGEMENT (SUBCUBES + CUBES)
// ===============================================================

void ChunkManager::rebuildGlobalDynamicFaces() {
    globalDynamicSubcubeFaces.clear();
    
    static constexpr float SUBCUBE_SCALE = 1.0f / 3.0f; // Subcubes are 1/3 the size
    static constexpr float CUBE_SCALE = 1.0f;           // Full cubes are full size
    
    // Generate faces for all global dynamic subcubes
    for (const auto& subcube : globalDynamicSubcubes) {
        if (!subcube->isVisible()) continue;
        
        // For dynamic subcubes, we render all faces (they can be in arbitrary positions)
        for (int faceID = 0; faceID < 6; ++faceID) {
            DynamicSubcubeInstanceData faceInstance;
            
            // Use smooth physics position for dynamic subcubes, fallback to grid position for static
            if (subcube->isDynamic()) {
                faceInstance.worldPosition = subcube->getPhysicsPosition();
                faceInstance.rotation = subcube->getPhysicsRotation(); // Get rotation from physics
            } else {
                faceInstance.worldPosition = glm::vec3(subcube->getPosition()) + glm::vec3(subcube->getLocalPosition()) * SUBCUBE_SCALE;
                faceInstance.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // Identity quaternion for static subcubes
            }
            
            faceInstance.color = subcube->getColor();
            faceInstance.faceID = faceID;
            faceInstance.scale = subcube->getScale();
            
            globalDynamicSubcubeFaces.push_back(faceInstance);
        }
    }
    
    // Generate faces for all global dynamic cubes
    for (const auto& cube : globalDynamicCubes) {
        if (!cube->isVisible()) continue;
        
        // For dynamic cubes, we render all faces (they can be in arbitrary positions)
        for (int faceID = 0; faceID < 6; ++faceID) {
            DynamicSubcubeInstanceData faceInstance; // Using same data structure as subcubes
            
            // Dynamic cubes always use physics position and rotation
            faceInstance.worldPosition = cube->getPhysicsPosition();
            faceInstance.rotation = cube->getPhysicsRotation();
            faceInstance.color = cube->getColor();
            faceInstance.faceID = faceID;
            faceInstance.scale = cube->getScale(); // 1.0 for full cubes
            
            globalDynamicSubcubeFaces.push_back(faceInstance);
        }
    }
    
    if (!globalDynamicSubcubeFaces.empty()) {
        //std::cout << "[CHUNK MANAGER] Generated " << globalDynamicSubcubeFaces.size() << " global dynamic faces (subcubes + cubes)" << std::endl;
    }
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

// ========================================================================
// EFFICIENT SELECTIVE UPDATE SYSTEM
// ========================================================================

void ChunkManager::updateAfterCubeBreak(const glm::ivec3& worldPos) {
    // When a cube is broken (removed), we need to:
    // 1. Remove faces of the broken cube (already done by removeCube)
    // 2. Update faces of neighboring cubes that may now be exposed
    
    std::cout << "[SELECTIVE UPDATE] Cube broken at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")" << std::endl;
    
    // Get the chunk containing the broken cube
    Chunk* primaryChunk = getChunkAt(worldPos);
    if (primaryChunk) {
        markChunkDirty(primaryChunk);
    }
    
    // Check if any neighbors are in different chunks and mark those dirty too
    std::vector<glm::ivec3> neighborPositions = getAffectedNeighborPositions(worldPos);
    std::set<Chunk*> affectedChunks;
    
    for (const glm::ivec3& neighborPos : neighborPositions) {
        Chunk* neighborChunk = getChunkAt(neighborPos);
        if (neighborChunk && neighborChunk != primaryChunk) {
            affectedChunks.insert(neighborChunk);
        }
    }
    
    // Mark all affected neighbor chunks dirty
    for (Chunk* chunk : affectedChunks) {
        markChunkDirty(chunk);
    }
}

void ChunkManager::updateAfterCubePlace(const glm::ivec3& worldPos) {
    // When a cube is placed (added), we need to:
    // 1. Generate faces for the new cube (based on neighbors)
    // 2. Update faces of neighboring cubes that may now be hidden
    
    std::cout << "[SELECTIVE UPDATE] Cube placed at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")" << std::endl;
    
    // Get the chunk containing the placed cube
    Chunk* primaryChunk = getChunkAt(worldPos);
    if (primaryChunk) {
        markChunkDirty(primaryChunk);
    }
    
    // Check if any neighbors are in different chunks and mark those dirty too
    std::vector<glm::ivec3> neighborPositions = getAffectedNeighborPositions(worldPos);
    std::set<Chunk*> affectedChunks;
    
    for (const glm::ivec3& neighborPos : neighborPositions) {
        Chunk* neighborChunk = getChunkAt(neighborPos);
        if (neighborChunk && neighborChunk != primaryChunk) {
            affectedChunks.insert(neighborChunk);
        }
    }
    
    // Mark all affected neighbor chunks dirty
    for (Chunk* chunk : affectedChunks) {
        markChunkDirty(chunk);
    }
}

void ChunkManager::updateAfterCubeSubdivision(const glm::ivec3& worldPos) {
    // When a cube is subdivided, we need to:
    // 1. Hide original cube faces (cube becomes invisible)
    // 2. Generate subcube faces (8 or 27 subcubes with their own faces)
    // 3. Update faces of neighboring cubes (original cube is now hidden)
    
    std::cout << "[SELECTIVE UPDATE] Cube subdivided at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")" << std::endl;
    
    // Get the chunk containing the subdivided cube
    Chunk* primaryChunk = getChunkAt(worldPos);
    if (primaryChunk) {
        markChunkDirty(primaryChunk);
    }
    
    // Check if any neighbors are in different chunks and mark those dirty too
    std::vector<glm::ivec3> neighborPositions = getAffectedNeighborPositions(worldPos);
    std::set<Chunk*> affectedChunks;
    
    for (const glm::ivec3& neighborPos : neighborPositions) {
        Chunk* neighborChunk = getChunkAt(neighborPos);
        if (neighborChunk && neighborChunk != primaryChunk) {
            affectedChunks.insert(neighborChunk);
        }
    }
    
    // Mark all affected neighbor chunks dirty
    for (Chunk* chunk : affectedChunks) {
        markChunkDirty(chunk);
    }
}

void ChunkManager::updateAfterSubcubeBreak(const glm::ivec3& parentWorldPos, const glm::ivec3& subcubeLocalPos) {
    // When a subcube breaks (moves from static to dynamic), we need to:
    // 1. Remove the subcube's faces from static rendering
    // 2. Update faces of neighboring subcubes in the same parent cube
    // 3. Add the subcube to dynamic rendering system
    
    std::cout << "[SELECTIVE UPDATE] Subcube broken at parent pos (" << parentWorldPos.x << "," << parentWorldPos.y << "," << parentWorldPos.z 
              << ") local (" << subcubeLocalPos.x << "," << subcubeLocalPos.y << "," << subcubeLocalPos.z << ")" << std::endl;
    
    // For subcube breaking, only update the chunk containing the parent cube
    Chunk* chunk = getChunkAt(parentWorldPos);
    if (chunk) {
        markChunkDirty(chunk);
    }
}

void ChunkManager::updateFacesForPositionChange(const glm::ivec3& worldPos, bool cubeAdded) {
    // Central method that handles face updates when a cube is added or removed
    // This affects the cube at worldPos and its up to 6 neighbors
    
    // Update faces for the cube at the changed position
    updateSingleCubeFaces(worldPos);
    
    // Update faces for all neighboring cubes (up to 6 neighbors)
    updateNeighborFaces(worldPos);
}

void ChunkManager::updateNeighborFaces(const glm::ivec3& worldPos) {
    // Get all 6 neighboring positions
    std::vector<glm::ivec3> neighborPositions = getAffectedNeighborPositions(worldPos);
    
    // Update faces for each neighbor position
    for (const glm::ivec3& neighborPos : neighborPositions) {
        updateFacesAtPosition(neighborPos);
    }
}

void ChunkManager::updateSingleCubeFaces(const glm::ivec3& worldPos) {
    // Update faces only for the cube at the specified position
    updateFacesAtPosition(worldPos);
}

std::vector<glm::ivec3> ChunkManager::getAffectedNeighborPositions(const glm::ivec3& worldPos) {
    // Return all 6 neighbor positions (may span multiple chunks)
    std::vector<glm::ivec3> neighbors;
    neighbors.reserve(6);
    
    // Face directions: front(+Z), back(-Z), right(+X), left(-X), top(+Y), bottom(-Y)
    neighbors.push_back(worldPos + glm::ivec3(0, 0, 1));   // front (+Z)
    neighbors.push_back(worldPos + glm::ivec3(0, 0, -1));  // back (-Z)  
    neighbors.push_back(worldPos + glm::ivec3(1, 0, 0));   // right (+X)
    neighbors.push_back(worldPos + glm::ivec3(-1, 0, 0));  // left (-X)
    neighbors.push_back(worldPos + glm::ivec3(0, 1, 0));   // top (+Y)
    neighbors.push_back(worldPos + glm::ivec3(0, -1, 0));  // bottom (-Y)
    
    return neighbors;
}

void ChunkManager::updateFacesAtPosition(const glm::ivec3& worldPos) {
    // Update faces for a single cube at the specified world position
    // This may be in any chunk, so we need to find the right chunk first
    
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) {
        return; // Position is outside loaded chunks
    }
    
    // For now, use the simple approach: mark the chunk dirty and let the update system handle it
    // TODO: Implement true single-cube face updates to avoid rebuilding entire chunks
    markChunkDirty(chunk);
}

} // namespace VulkanCube
