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
    // ChunkStreamingManager destructor will handle worldStorage cleanup
}

void ChunkManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
    
    // Setup streaming manager callbacks
    m_streamingManager.setCallbacks(
        // ChunkCreationFunc: Create chunk via existing createChunk method
        [this](const glm::ivec3& origin) { createChunk(origin); },
        // ChunkMapAccessFunc: Access chunk spatial hash map
        [this]() -> auto& { return chunkMap; },
        // ChunkVectorAccessFunc: Access chunk vector
        [this]() -> auto& { return chunks; },
        // DeviceAccessFunc: Get Vulkan device handles
        [this]() { return std::make_pair(device, physicalDevice); }
    );
    
    // Setup dynamic object manager callbacks
    m_dynamicObjectManager.setCallbacks(
        // PhysicsWorldAccessFunc: Access physics world
        [this]() { return physicsWorld; },
        // DynamicSubcubeVectorAccessFunc: Access subcube vector
        [this]() -> auto& { return globalDynamicSubcubes; },
        // DynamicCubeVectorAccessFunc: Access cube vector
        [this]() -> auto& { return globalDynamicCubes; },
        // DynamicMicrocubeVectorAccessFunc: Access microcube vector
        [this]() -> auto& { return globalDynamicMicrocubes; },
        // RebuildFacesFunc: Rebuild faces when objects change
        [this]() { rebuildGlobalDynamicFaces(); }
    );
    
    // Setup face update coordinator callbacks
    m_faceUpdateCoordinator.setCallbacks(
        // DynamicSubcubeVectorAccessFunc: Access subcube vector
        [this]() -> auto& { return globalDynamicSubcubes; },
        // DynamicCubeVectorAccessFunc: Access cube vector
        [this]() -> auto& { return globalDynamicCubes; },
        // DynamicMicrocubeVectorAccessFunc: Access microcube vector
        [this]() -> auto& { return globalDynamicMicrocubes; },
        // FaceDataAccessFunc: Access face data
        [this]() -> auto& { return globalDynamicSubcubeFaces; },
        // ChunkLookupFunc: Get chunk at position
        [this](const glm::ivec3& pos) { return getChunkAt(pos); },
        // MarkChunkDirtyFunc: Mark chunk dirty
        [this](Chunk* chunk) { markChunkDirty(chunk); }
    );
    
    // Setup chunk initializer callbacks
    m_chunkInitializer.setCallbacks(
        // ChunkVectorAccessFunc: Access chunk vector
        [this]() -> auto& { return chunks; },
        // ChunkMapAccessFunc: Access chunk map
        [this]() -> auto& { return chunkMap; },
        // DeviceAccessFunc: Get Vulkan device handles
        [this]() { return std::make_pair(device, physicalDevice); },
        // GetChunkAtCoordFunc: Get chunk at coordinate
        [this](const glm::ivec3& coord) { return getChunkAtCoord(coord); },
        // RebuildChunkWithCullingFunc: Rebuild chunk with cross-chunk culling
        [this](Chunk& chunk) { rebuildChunkFacesWithCrosschunkCulling(chunk); }
    );
}

void ChunkManager::setPhysicsWorld(Physics::PhysicsWorld* physics) {
    physicsWorld = physics;
    LOG_INFO("Chunk", "Physics world set for proper dynamic object cleanup");
}

bool ChunkManager::initializeWorldStorage(const std::string& worldPath) {
    return m_streamingManager.initializeWorldStorage(worldPath);
}

void ChunkManager::updateChunkStreaming() {
    m_streamingManager.updateStreaming(playerPosition, loadDistance, unloadDistance);
}

void ChunkManager::loadChunksAroundPosition(const glm::vec3& position, float radius) {
    m_streamingManager.loadChunksAroundPosition(position, radius);
}

void ChunkManager::unloadDistantChunks(const glm::vec3& position, float radius) {
    m_streamingManager.unloadDistantChunks(position, radius);
}

bool ChunkManager::saveChunk(Chunk* chunk) {
    return m_streamingManager.saveChunk(chunk);
}

bool ChunkManager::saveAllChunks() {
    return m_streamingManager.saveAllChunks();
}

bool ChunkManager::saveDirtyChunks() {
    return m_streamingManager.saveDirtyChunks();
}

bool ChunkManager::loadChunk(const glm::ivec3& chunkCoord) {
    return m_streamingManager.loadChunk(chunkCoord);
}

bool ChunkManager::generateOrLoadChunk(const glm::ivec3& chunkCoord) {
    return m_streamingManager.generateOrLoadChunk(chunkCoord);
}

void ChunkManager::rebuildAllChunkFaces() {
    m_chunkInitializer.rebuildAllChunkFaces();
}

void ChunkManager::initializeAllChunkVoxelMaps() {
    m_chunkInitializer.initializeAllChunkVoxelMaps();
}

void ChunkManager::createChunks(const std::vector<glm::ivec3>& origins) {
    m_chunkInitializer.createChunks(origins);
}

void ChunkManager::createChunk(const glm::ivec3& origin) {
    m_chunkInitializer.createChunk(origin);
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
    m_chunkInitializer.performOcclusionCulling();
    
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
    m_dynamicObjectManager.addGlobalDynamicSubcube(std::move(subcube));
}

void ChunkManager::rebuildGlobalDynamicSubcubeFaces() {
    // Legacy function - now calls the combined function that handles both subcubes and cubes
    rebuildGlobalDynamicFaces();
}

void ChunkManager::updateGlobalDynamicSubcubes(float deltaTime) {
    m_dynamicObjectManager.updateGlobalDynamicSubcubes(deltaTime);
}

void ChunkManager::updateGlobalDynamicSubcubePositions() {
    m_dynamicObjectManager.updateGlobalDynamicSubcubePositions();
}

void ChunkManager::clearAllGlobalDynamicSubcubes() {
    m_dynamicObjectManager.clearAllGlobalDynamicSubcubes();
}

// ===============================================================
// GLOBAL DYNAMIC CUBE MANAGEMENT
// ===============================================================

void ChunkManager::addGlobalDynamicCube(std::unique_ptr<Cube> cube) {
    m_dynamicObjectManager.addGlobalDynamicCube(std::move(cube));
}

void ChunkManager::updateGlobalDynamicCubes(float deltaTime) {
    m_dynamicObjectManager.updateGlobalDynamicCubes(deltaTime);
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
    m_faceUpdateCoordinator.rebuildGlobalDynamicFaces();
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
    m_faceUpdateCoordinator.updateAfterCubeBreak(worldPos);
}

void ChunkManager::updateAfterCubePlace(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateAfterCubePlace(worldPos);
}

void ChunkManager::updateAfterCubeSubdivision(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateAfterCubeSubdivision(worldPos);
}

void ChunkManager::updateAfterSubcubeBreak(const glm::ivec3& parentWorldPos, const glm::ivec3& subcubeLocalPos) {
    m_faceUpdateCoordinator.updateAfterSubcubeBreak(parentWorldPos, subcubeLocalPos);
}

void ChunkManager::updateFacesForPositionChange(const glm::ivec3& worldPos, bool cubeAdded) {
    m_faceUpdateCoordinator.updateFacesForPositionChange(worldPos, cubeAdded);
}

void ChunkManager::updateNeighborFaces(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateNeighborFaces(worldPos);
}

void ChunkManager::updateSingleCubeFaces(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateSingleCubeFaces(worldPos);
}

std::vector<glm::ivec3> ChunkManager::getAffectedNeighborPositions(const glm::ivec3& worldPos) {
    return m_faceUpdateCoordinator.getAffectedNeighborPositions(worldPos);
}

void ChunkManager::updateFacesAtPosition(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateFacesAtPosition(worldPos);
}

} // namespace VulkanCube
