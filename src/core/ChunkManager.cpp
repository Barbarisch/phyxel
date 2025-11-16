#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/WorldStorage.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <stdexcept>
#include <cstring>
#include <random>
#include <iostream>
#include <algorithm>  // for std::find
#include <set>        // for std::set in selective updates

// Bullet Physics includes
#include <btBulletDynamicsCommon.h>

namespace VulkanCube {

ChunkManager::~ChunkManager() {
    cleanup();
    // Clean up world storage
    delete worldStorage;
    worldStorage = nullptr;
}

void ChunkManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
}

void ChunkManager::setPhysicsWorld(Physics::PhysicsWorld* physics) {
    physicsWorld = physics;
    LOG_INFO("Chunk", "Physics world set for proper dynamic object cleanup");
}

bool ChunkManager::initializeWorldStorage(const std::string& worldPath) {
    worldStorage = new WorldStorage(worldPath);
    if (!worldStorage->initialize()) {
        LOG_ERROR_FMT("Chunk", "Failed to initialize world storage at: " << worldPath);
        delete worldStorage;
        worldStorage = nullptr;
        return false;
    }
    
    LOG_INFO_FMT("Chunk", "World storage initialized: " << worldPath);
    return true;
}

void ChunkManager::updateChunkStreaming() {
    if (!worldStorage) return;
    
    // Load chunks around player
    loadChunksAroundPosition(playerPosition, loadDistance);
    
    // Unload distant chunks
    unloadDistantChunks(playerPosition, unloadDistance);
}

void ChunkManager::loadChunksAroundPosition(const glm::vec3& position, float radius) {
    glm::ivec3 centerChunk = worldToChunkCoord(glm::ivec3(position));
    int chunkRadius = static_cast<int>(std::ceil(radius / 32.0f));
    
    for (int dx = -chunkRadius; dx <= chunkRadius; ++dx) {
        for (int dy = -chunkRadius; dy <= chunkRadius; ++dy) {
            for (int dz = -chunkRadius; dz <= chunkRadius; ++dz) {
                glm::ivec3 chunkCoord = centerChunk + glm::ivec3(dx, dy, dz);
                
                // Check if chunk is within radius
                glm::vec3 chunkCenter = glm::vec3(chunkCoordToOrigin(chunkCoord)) + glm::vec3(16.0f);
                float distance = glm::length(chunkCenter - position);
                
                if (distance <= radius && !getChunkAtCoord(chunkCoord)) {
                    // Try to load chunk from storage, or generate if it doesn't exist
                    generateOrLoadChunk(chunkCoord);
                }
            }
        }
    }
}

void ChunkManager::unloadDistantChunks(const glm::vec3& position, float radius) {
    auto it = chunks.begin();
    while (it != chunks.end()) {
        glm::vec3 chunkCenter = glm::vec3((*it)->getWorldOrigin()) + glm::vec3(16.0f);
        float distance = glm::length(chunkCenter - position);
        
        if (distance > radius) {
            // Save chunk before unloading
            if (worldStorage) {
                saveChunk(it->get());
            }
            
            // Remove from chunk map
            glm::ivec3 chunkCoord = worldToChunkCoord((*it)->getWorldOrigin());
            chunkMap.erase(chunkCoord);
            
            // Erase chunk from vector
            it = chunks.erase(it);
            LOG_DEBUG_FMT("Chunk", "Unloaded distant chunk at: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z);
        } else {
            ++it;
        }
    }
}

bool ChunkManager::saveChunk(Chunk* chunk) {
    if (!worldStorage || !chunk) return false;
    return worldStorage->saveChunk(*chunk);
}

bool ChunkManager::saveAllChunks() {
    if (!worldStorage) return false;
    
    bool allSuccess = true;
    for (const auto& chunk : chunks) {
        if (!worldStorage->saveChunk(*chunk)) {
            allSuccess = false;
        }
    }
    
    LOG_INFO_FMT("Chunk", "Saved " << chunks.size() << " chunks to storage");
    return allSuccess;
}

bool ChunkManager::saveDirtyChunks() {
    if (!worldStorage) return false;
    
    // Build vector of chunk references for dirty saving
    std::vector<std::reference_wrapper<Chunk>> chunkRefs;
    chunkRefs.reserve(chunks.size());
    
    for (const auto& chunk : chunks) {
        chunkRefs.emplace_back(*chunk);
    }
    
    return worldStorage->saveDirtyChunks(chunkRefs);
}

bool ChunkManager::loadChunk(const glm::ivec3& chunkCoord) {
    if (!worldStorage) return false;
    
    // Don't load if chunk already exists
    if (getChunkAtCoord(chunkCoord)) {
        return true;
    }
    
    glm::ivec3 origin = chunkCoordToOrigin(chunkCoord);
    auto chunk = std::make_unique<Chunk>(origin);
    chunk->initialize(device, physicalDevice);
    
    // Initialize chunk for sparse loading from database
    chunk->initializeForLoading();
    
    if (worldStorage->loadChunk(chunkCoord, *chunk)) {
        // Successfully loaded from storage - mark as clean since it's from database
        chunk->markClean();
        
        // DON'T rebuild faces yet - wait until all chunks are loaded
        // chunk->rebuildFaces();  // MOVED to after all chunks loaded
        chunk->createVulkanBuffer();
        
        // Add to map and vector
        chunkMap[chunkCoord] = chunk.get();
        chunks.push_back(std::move(chunk));
        
        LOG_DEBUG_FMT("Chunk", "Loaded chunk from storage: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z);
        return true;
    }
    
    return false;
}

bool ChunkManager::generateOrLoadChunk(const glm::ivec3& chunkCoord) {
    if (!worldStorage) {
        // Fallback: create empty chunk
        createChunk(chunkCoordToOrigin(chunkCoord));
        return true;
    }
    
    // Try to load from storage first
    if (loadChunk(chunkCoord)) {
        return true;
    }
    
    // If not in storage, generate new chunk
    glm::ivec3 origin = chunkCoordToOrigin(chunkCoord);
    auto chunk = std::make_unique<Chunk>(origin);
    chunk->initialize(device, physicalDevice);
    
    // Fallback to old random generation
    chunk->populateWithCubes();
    
    // DON'T rebuild faces yet - wait until all chunks are loaded  
    // chunk->rebuildFaces();  // MOVED to after all chunks loaded
    chunk->createVulkanBuffer();
    
    // Add to map and vector
    chunkMap[chunkCoord] = chunk.get();
    chunks.push_back(std::move(chunk));
    
    // Save to storage immediately
    saveChunk(chunks.back().get());
    
    LOG_DEBUG_FMT("Chunk", "Generated and saved new chunk: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z);
    return true;
}

void ChunkManager::rebuildAllChunkFaces() {
    LOG_INFO("Chunk", "Rebuilding faces for all loaded chunks with cross-chunk culling...");
    
    // First: End bulk operations for all chunks and build proper collision systems
    for (auto& chunk : chunks) {
        chunk->endBulkOperation();
    }
    
    // Second pass: rebuild basic faces for each chunk
    for (auto& chunk : chunks) {
        chunk->rebuildFaces();
    }
    
    // Second pass: perform cross-chunk occlusion culling
    performOcclusionCulling();
    
    // Third pass: update Vulkan buffers with new face data
    for (auto& chunk : chunks) {
        chunk->updateVulkanBuffer();
    }
    
    LOG_INFO_FMT("Chunk", "Face rebuilding complete for " << chunks.size() << " chunks");
}

void ChunkManager::initializeAllChunkVoxelMaps() {
    LOG_INFO_FMT("Chunk", "Initializing voxel hash maps for all " << chunks.size() << " chunks...");
    
    for (auto& chunk : chunks) {
        chunk->initializeVoxelMaps();
    }
    
    LOG_INFO("Chunk", "Completed voxel map initialization for optimized O(1) hover detection");
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
        LOG_DEBUG_FMT("Chunk", "Updating chunk " << chunkIndex << " with " << chunk->getTotalSubcubeCount() << " subcubes");
        
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
    // Provide a neighbor lookup function that can check cubes in adjacent chunks
    auto getNeighborCube = [this, &chunk](const glm::ivec3& worldPos) -> const Cube* {
        glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
        Chunk* neighborChunk = getChunkAtCoord(chunkCoord);
        
        if (neighborChunk) {
            glm::ivec3 localPos = worldToLocalCoord(worldPos);
            return neighborChunk->getCubeAt(localPos);
        }
        
        return nullptr;
    };
    
    // Call rebuildFaces with cross-chunk neighbor lookup enabled
    chunk.rebuildFaces(getNeighborCube);
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
    glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(worldPos);
    return getChunkAtCoord(chunkCoord);
}

Cube* ChunkManager::getCubeAtFast(const glm::ivec3& worldPos) {
    // Step 1: Get chunk using O(1) hash map lookup
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return nullptr;
    
    // Step 2: Calculate local position
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    
    // Step 3: Use Chunk class's getCubeAt method
    return chunk->getCubeAt(localPos);
}

bool ChunkManager::setCubeColorFast(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    bool result = chunk->setCubeColor(localPos, color);
    
    if (result) {
        markChunkDirty(chunk);
    }
    
    return result;
}

bool ChunkManager::removeCubeFast(const glm::ivec3& worldPos) {
    Chunk* chunk = getChunkAtFast(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
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
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
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
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    
    // Color changes should not affect textures - textures and colors are separate properties
    // Note: Currently the system uses textures instead of colors for cube faces
    // If you want to change cube appearance, modify the texture index per face instead
    
    // TODO: If we implement a color tinting system in the future, 
    // we would update color properties here without touching texture indices
}

// =============================================================================
// NEW: O(1) VoxelLocation resolution system for optimized hover detection
// =============================================================================

VoxelLocation ChunkManager::resolveGlobalPosition(const glm::ivec3& worldPos) const {
    // O(1) chunk lookup
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    const Chunk* chunk = getChunkAtCoord(chunkCoord);
    if (!chunk) {
        return VoxelLocation(); // No chunk exists
    }
    
    // O(1) voxel resolution within chunk
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    return chunk->resolveLocalPosition(localPos);
}

VoxelLocation ChunkManager::resolveGlobalPositionWithSubcube(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) const {
    VoxelLocation location = resolveGlobalPosition(worldPos);
    
    if (location.isValid() && location.type == VoxelLocation::SUBDIVIDED) {
        // Verify the specific subcube exists
        if (location.chunk->hasSubcubeAt(location.localPos, subcubePos)) {
            location.subcubePos = subcubePos;
            return location;
        }
    }
    
    return VoxelLocation(); // Subcube doesn't exist
}

bool ChunkManager::hasVoxelAt(const glm::ivec3& worldPos) const {
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    const Chunk* chunk = getChunkAtCoord(chunkCoord);
    if (!chunk) return false;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    return chunk->hasVoxelAt(localPos);
}

VoxelLocation::Type ChunkManager::getVoxelTypeAt(const glm::ivec3& worldPos) const {
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    const Chunk* chunk = getChunkAtCoord(chunkCoord);
    if (!chunk) return VoxelLocation::EMPTY;
    
    glm::ivec3 localPos = worldToLocalCoord(worldPos);
    return chunk->getVoxelType(localPos);
}

bool ChunkManager::removeCube(const glm::ivec3& worldPos) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    
    // Use the Chunk class's removeCube method
    bool result = chunk->removeCube(localPos);
    if (result) {
        // Chunk is now marked dirty and will be saved properly on next save
        // No need for immediate database deletion - saveChunk handles all deletions
        
        // Use efficient selective update instead of full chunk rebuild
        updateAfterCubeBreak(worldPos);
    }
    return result;
}

bool ChunkManager::addCube(const glm::ivec3& worldPos, const glm::vec3& color) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return false;
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    
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
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    return chunk->getSubcubeAt(localPos, subcubePos);
}

void ChunkManager::setSubcubeColorEfficient(const glm::ivec3& worldPos, const glm::ivec3& subcubePos, const glm::vec3& color) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return;
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    std::vector<Subcube*> subcubes = chunk->getSubcubesAt(localPos);
    if (subcubes.empty()) return;
    
    // Color changes should not affect textures - textures and colors are separate properties
    // Note: Currently the system uses textures instead of colors for subcube faces
    // If you want to change subcube appearance, modify the texture index per face instead
    
    // TODO: If we implement a color tinting system in the future, 
    // we would update color properties here without touching texture indices
}

glm::vec3 ChunkManager::getSubcubeColor(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) {
    Chunk* chunk = getChunkAt(worldPos);
    if (!chunk) return glm::vec3(1.0f); // Default white
    
    glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    Subcube* subcube = chunk->getSubcubeAt(localPos, subcubePos);
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
    
    LOG_DEBUG("Chunk", "calculateChunkFaceCulling: No longer needed with CPU pre-filtering");
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
                
                // Check if there's actually a cube at this position
                const Cube* cube = chunk->getCubeAt(relativePos);
                return cube != nullptr && cube->isVisible();
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
    LOG_DEBUG("Chunk", "Regenerating chunks with updated cross-chunk occlusion culling...");
    
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
    LOG_DEBUG_FMT("Chunk", "Occlusion culling complete: " << stats.totalCubes << " total cubes, " 
              << stats.totalVisibleFaces << " visible faces, "
              << stats.totalHiddenFaces << " hidden faces");
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
        LOG_DEBUG_FMT("Chunk", "Adding global dynamic subcube at world position: ("
                  << subcube->getPosition().x << "," << subcube->getPosition().y << "," << subcube->getPosition().z << ")");
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
                LOG_TRACE("Chunk", "Removing expired dynamic subcube physics body");
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
        LOG_DEBUG_FMT("Chunk", "Removed " << removedCount << " expired dynamic subcubes (lifetime ended)");
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::updateGlobalDynamicSubcubePositions() {
    // Update positions and rotations of global dynamic subcubes from their physics bodies
    bool transformsChanged = false;
    static int debugCounter = 0;
    static bool firstUpdate = true;
    
    if (firstUpdate && !globalDynamicSubcubes.empty()) {
        LOG_TRACE("Chunk", "===== FIRST SUBCUBE PHYSICS UPDATE =====");
        LOG_TRACE_FMT("Chunk", "Found " << globalDynamicSubcubes.size() << " dynamic subcubes to track");
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
                
                LOG_TRACE("Chunk", "===== AFTER PHYSICS SIMULATION =====");
                LOG_TRACE_FMT("Chunk", "Physics body final position: (" 
                          << newWorldPos.x << ", " << newWorldPos.y << ", " << newWorldPos.z << ")");
                LOG_TRACE_FMT("Chunk", "Movement from last update: (" 
                          << movement.x << ", " << movement.y << ", " << movement.z << ") magnitude: " << movementMag);
                LOG_TRACE_FMT("Chunk", "Rotation: (" 
                          << newRotation.x << ", " << newRotation.y << ", " << newRotation.z << ", " << newRotation.w << ")");
                LOG_TRACE("Chunk", "===== END SUBCUBE POSITION TRACKING =====");
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
    // Properly clean up physics bodies before clearing the vector
    if (physicsWorld) {
        for (auto& subcube : globalDynamicSubcubes) {
            if (subcube && subcube->getRigidBody()) {
                LOG_TRACE("Chunk", "Cleaning up physics body for subcube during clear");
                physicsWorld->removeCube(subcube->getRigidBody());
                subcube->setRigidBody(nullptr); // Prevent double deletion
            }
        }
    }
    
    LOG_DEBUG_FMT("Chunk", "Clearing all " << globalDynamicSubcubes.size() << " global dynamic subcubes");
    globalDynamicSubcubes.clear();
}

// ===============================================================
// GLOBAL DYNAMIC CUBE MANAGEMENT
// ===============================================================

void ChunkManager::addGlobalDynamicCube(std::unique_ptr<Cube> cube) {
    if (cube) {
        LOG_DEBUG_FMT("ChunkManager", "[CHUNK MANAGER] Adding global dynamic cube at world position: ("
                  << cube->getPosition().x << "," << cube->getPosition().y << "," << cube->getPosition().z << ")");
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
                LOG_DEBUG("ChunkManager", "[CHUNK MANAGER] Removing expired dynamic cube physics body");
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
        LOG_DEBUG_FMT("ChunkManager", "[CHUNK MANAGER] Removed " << removedCount << " expired dynamic cubes (lifetime ended)");
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::updateGlobalDynamicCubePositions() {
    // Update positions and rotations of global dynamic cubes from their physics bodies
    bool transformsChanged = false;
    static int debugCounter = 0;
    static bool firstUpdate = true;
    
    if (firstUpdate && !globalDynamicCubes.empty()) {
        LOG_DEBUG_FMT("ChunkManager", "[POSITION TRACK] ===== FIRST PHYSICS UPDATE =====");
        LOG_DEBUG_FMT("ChunkManager", "[POSITION TRACK] Found " << globalDynamicCubes.size() << " dynamic cubes to track");
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
                
                LOG_DEBUG("ChunkManager", "[POSITION TRACK] ===== AFTER PHYSICS SIMULATION =====");
                LOG_DEBUG_FMT("ChunkManager", "[POSITION TRACK] 6. Physics body final position: (" 
                          << newWorldPos.x << ", " << newWorldPos.y << ", " << newWorldPos.z << ")");
                LOG_DEBUG_FMT("ChunkManager", "[POSITION TRACK] 7. Movement from last update: (" 
                          << movement.x << ", " << movement.y << ", " << movement.z << ") magnitude: " << movementMag);
                LOG_DEBUG_FMT("ChunkManager", "[POSITION TRACK] 8. Rotation: (" 
                          << newRotation.x << ", " << newRotation.y << ", " << newRotation.z << ", " << newRotation.w << ")");
                LOG_DEBUG("ChunkManager", "[POSITION TRACK] ===== END POSITION TRACKING =====");
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
    LOG_DEBUG_FMT("ChunkManager", "[CHUNK MANAGER] Clearing all " << globalDynamicCubes.size() << " global dynamic cubes");
    globalDynamicCubes.clear();
}

// ===============================================================
// GLOBAL DYNAMIC MICROCUBE MANAGEMENT
// ===============================================================

void ChunkManager::addGlobalDynamicMicrocube(std::unique_ptr<Microcube> microcube) {
    if (microcube) {
        LOG_DEBUG_FMT("ChunkManager", "[MICROCUBE] Adding global dynamic microcube at world position: ("
                  << microcube->getWorldPosition().x << "," << microcube->getWorldPosition().y << "," << microcube->getWorldPosition().z << ")");
        globalDynamicMicrocubes.push_back(std::move(microcube));
        rebuildGlobalDynamicFaces();  // Rebuild faces after adding new microcube
    }
}

void ChunkManager::updateGlobalDynamicMicrocubes(float deltaTime) {
    // Update lifetimes and remove expired microcubes
    auto it = globalDynamicMicrocubes.begin();
    size_t removedCount = 0;
    
    while (it != globalDynamicMicrocubes.end()) {
        (*it)->updateLifetime(deltaTime);
        
        if ((*it)->hasExpired()) {
            // Properly remove physics body from physics world
            if (physicsWorld && (*it)->getRigidBody()) {
                LOG_TRACE("ChunkManager", "[MICROCUBE] Removing expired dynamic microcube physics body");
                physicsWorld->removeCube((*it)->getRigidBody());
            }
            
            removedCount++;
            // Note: The unique_ptr destructor will automatically clean up the microcube
            it = globalDynamicMicrocubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Rebuild faces if any microcubes were removed
    if (removedCount > 0) {
        LOG_DEBUG_FMT("ChunkManager", "[MICROCUBE] Removed " << removedCount << " expired dynamic microcubes (lifetime ended)");
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::updateGlobalDynamicMicrocubePositions() {
    // Update positions and rotations of global dynamic microcubes from their physics bodies
    bool transformsChanged = false;
    static int logCounter = 0;
    
    for (auto& microcube : globalDynamicMicrocubes) {
        if (microcube && microcube->getRigidBody()) {
            btRigidBody* body = microcube->getRigidBody();
            btTransform transform = body->getWorldTransform();
            
            // Get the physics world position
            btVector3 btPos = transform.getOrigin();
            glm::vec3 newWorldPos(btPos.x(), btPos.y(), btPos.z());
            
            // Get the physics rotation (quaternion)
            btQuaternion btRot = transform.getRotation();
            glm::vec4 newRotation(btRot.x(), btRot.y(), btRot.z(), btRot.w());
            
            // Log position every 60 frames (~1 second at 60fps) to track falling
            if (logCounter % 60 == 0) {
                btVector3 velocity = body->getLinearVelocity();
                LOG_INFO_FMT("ChunkManager", "[DYNAMIC MICROCUBE] Y=" << newWorldPos.y 
                          << " Active=" << body->isActive()
                          << " VelY=" << velocity.y());
            }
            
            // Store the smooth floating-point physics position and rotation
            microcube->setPhysicsPosition(newWorldPos);
            microcube->setPhysicsRotation(newRotation);
            transformsChanged = true;
        }
    }
    
    logCounter++;
    
    // Rebuild face data if any transforms changed
    if (transformsChanged) {
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::clearAllGlobalDynamicMicrocubes() {
    // Properly clean up physics bodies before clearing the vector
    if (physicsWorld) {
        for (auto& microcube : globalDynamicMicrocubes) {
            if (microcube && microcube->getRigidBody()) {
                LOG_TRACE("ChunkManager", "[MICROCUBE] Cleaning up physics body for microcube during clear");
                physicsWorld->removeCube(microcube->getRigidBody());
                microcube->setRigidBody(nullptr); // Prevent double deletion
            }
        }
    }
    
    LOG_DEBUG_FMT("ChunkManager", "[MICROCUBE] Clearing all " << globalDynamicMicrocubes.size() << " global dynamic microcubes");
    globalDynamicMicrocubes.clear();
}

// ===============================================================
// COMBINED DYNAMIC OBJECT MANAGEMENT (SUBCUBES + CUBES)
// ===============================================================

void ChunkManager::rebuildGlobalDynamicFaces() {
    globalDynamicSubcubeFaces.clear();
    
    static constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;   // Subcubes are 1/3 the size
    static constexpr float CUBE_SCALE = 1.0f;             // Full cubes are full size
    static constexpr float MICROCUBE_SCALE = 1.0f / 9.0f; // Microcubes are 1/9 the size
    
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
            
            faceInstance.textureIndex = TextureConstants::getTextureIndexForFace(faceID);
            faceInstance.faceID = faceID;
            faceInstance.scale = subcube->getScale();
            faceInstance.localPosition = subcube->getLocalPosition(); // Preserve original grid position
            
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
            faceInstance.textureIndex = TextureConstants::getTextureIndexForFace(faceID);
            faceInstance.faceID = faceID;
            faceInstance.scale = cube->getScale(); // 1.0 for full cubes
            faceInstance.localPosition = glm::ivec3(1, 1, 1); // Center position for full cubes
            
            globalDynamicSubcubeFaces.push_back(faceInstance);
        }
    }
    
    // Generate faces for all global dynamic microcubes
    for (const auto& microcube : globalDynamicMicrocubes) {
        if (!microcube->isVisible()) continue;
        
        // For dynamic microcubes, we render all faces (they can be in arbitrary positions)
        for (int faceID = 0; faceID < 6; ++faceID) {
            DynamicSubcubeInstanceData faceInstance; // Using same data structure
            
            // Dynamic microcubes always use physics position and rotation
            faceInstance.worldPosition = microcube->getPhysicsPosition();
            faceInstance.rotation = microcube->getPhysicsRotation();
            // Use placeholder texture for microcubes
            faceInstance.textureIndex = TextureConstants::PLACEHOLDER_TEXTURE_INDEX;
            faceInstance.faceID = faceID;
            faceInstance.scale = microcube->getScale(); // 1/9 for microcubes
            
            // Pack both subcube and microcube positions into localPosition for texture coordinate calculation
            // Bits 0-1: subcube X, Bits 2-3: subcube Y, Bits 4-5: subcube Z
            // Bits 6-7: microcube X, Bits 8-9: microcube Y, Bits 10-11: microcube Z
            glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();
            glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();
            int packed = (subcubePos.x & 0x3) | ((subcubePos.y & 0x3) << 2) | ((subcubePos.z & 0x3) << 4) |
                        ((microcubePos.x & 0x3) << 6) | ((microcubePos.y & 0x3) << 8) | ((microcubePos.z & 0x3) << 10);
            faceInstance.localPosition = glm::ivec3(packed, 0, 0); // Store packed data in X component
            
            globalDynamicSubcubeFaces.push_back(faceInstance);
        }
    }
    
    if (!globalDynamicSubcubeFaces.empty()) {
        //std::cout << "[CHUNK MANAGER] Generated " << globalDynamicSubcubeFaces.size() << " global dynamic faces (subcubes + cubes + microcubes)" << std::endl;
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
    
    LOG_DEBUG_FMT("ChunkManager", "[SELECTIVE UPDATE] Cube broken at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    
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
    
    LOG_DEBUG_FMT("ChunkManager", "[SELECTIVE UPDATE] Cube placed at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    
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
    
    LOG_DEBUG_FMT("ChunkManager", "[SELECTIVE UPDATE] Cube subdivided at world pos (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
    
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
    
    LOG_DEBUG_FMT("ChunkManager", "[SELECTIVE UPDATE] Subcube broken at parent pos (" << parentWorldPos.x << "," << parentWorldPos.y << "," << parentWorldPos.z 
              << ") local (" << subcubeLocalPos.x << "," << subcubeLocalPos.y << "," << subcubeLocalPos.z << ")");
    
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
