#include "core/ChunkVoxelManager.h"
#include "core/Cube.h"
#include "utils/Logger.h"
#include <random>

namespace Phyxel {

// =============================================================================
// COORDINATE UTILITIES
// =============================================================================
// These functions convert 3D coordinates to unique indices for hash map storage

/**
 * Convert subcube position to unique flat index
 * 
 * INDEXING STRATEGY:
 * Each chunk has 32x32x32 possible cube positions (32,768 cubes max)
 * Each cube can be subdivided into 3x3x3 subcubes (27 subcubes per cube)
 * Total possible subcubes: 32,768 * 27 = 884,736
 * 
 * CALCULATION:
 * 1. parentIndex = z + y*32 + x*32*32 (cube's flat index in chunk)
 * 2. subcubeOffset = sx + sy*3 + sz*9 (subcube's offset within parent, 0-26)
 * 3. finalIndex = parentIndex * 27 + subcubeOffset
 * 
 * WHY UNIQUE?
 * Each parent cube occupies a distinct range of 27 indices:
 * - Cube (0,0,0): indices 0-26
 * - Cube (0,0,1): indices 27-53
 * - Cube (1,0,0): indices 884,736 - 884,762
 * No overlap possible!
 * 
 * @param parentPos Local position of parent cube (0-31, 0-31, 0-31)
 * @param subcubePos Local position within parent (0-2, 0-2, 0-2)
 * @return Unique flat index (0 to 884,735)
 */
size_t ChunkVoxelManager::subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    // Parent cube's flat index in chunk (0 to 32,767)
    size_t parentIndex = parentPos.z + parentPos.y * 32 + parentPos.x * 32 * 32;
    
    // Subcube's offset within parent cube (0 to 26)
    size_t subcubeOffset = subcubePos.x + subcubePos.y * 3 + subcubePos.z * 9;
    
    // Final unique index combining both
    return parentIndex * 27 + subcubeOffset;
}

void ChunkVoxelManager::setCallbacks(
    CubesVectorAccessFunc getCubes,
    SubcubesVectorAccessFunc getStaticSubcubes,
    MicrocubesVectorAccessFunc getStaticMicrocubes,
    WorldOriginAccessFunc getWorldOrigin,
    SetDirtyFunc setDirty,
    SetNeedsUpdateFunc setNeedsUpdate,
    RebuildFacesFunc rebuildFaces,
    AddCollisionFunc addCollision,
    RemoveCollisionFunc removeCollision,
    UpdateNeighborCollisionsFunc updateNeighborCollisions,
    IsInBulkOperationFunc isInBulkOperation,
    std::function<void()> updateVulkanBuffer
) {
    m_getCubes = std::move(getCubes);
    m_getStaticSubcubes = std::move(getStaticSubcubes);
    m_getStaticMicrocubes = std::move(getStaticMicrocubes);
    m_getWorldOrigin = std::move(getWorldOrigin);
    m_setDirty = std::move(setDirty);
    m_setNeedsUpdate = std::move(setNeedsUpdate);
    m_rebuildFaces = std::move(rebuildFaces);
    m_addCollision = std::move(addCollision);
    m_removeCollision = std::move(removeCollision);
    m_updateNeighborCollisions = std::move(updateNeighborCollisions);
    m_isInBulkOperation = std::move(isInBulkOperation);
    m_updateVulkanBuffer = std::move(updateVulkanBuffer);
}

// =============================================================================
// BULK OPERATIONS
// =============================================================================

void ChunkVoxelManager::clearAllVoxels() {
    cubeMap.clear();
    subcubeMap.clear();
    microcubeMap.clear();
    voxelTypeMap.clear();
}

// =============================================================================
// HASH MAP MANAGEMENT
// =============================================================================
// These functions maintain the hash maps that enable O(1) voxel lookups
// 
// DATA STRUCTURES:
// - cubeMap: { localPos -> Cube* }                    // Single-level: cube positions
// - subcubeMap: { cubePos -> { subcubePos -> Subcube* } }  // Two-level: cube then subcube
// - microcubeMap: { cubePos -> { subcubePos -> { microPos -> Microcube* } } }  // Three-level
// - voxelTypeMap: { localPos -> VoxelType }          // Fast type checking (CUBE or SUBDIVIDED)
// 
// WHY HASH MAPS?
// Without hash maps, finding a voxel requires O(n) linear search through vectors.
// With hash maps, we get O(1) constant-time lookup by position.
// 
// CONSISTENCY:
// ALL add/remove operations MUST update BOTH the vector AND the hash maps.
// Stale hash map entries cause crashes (dangling pointers) or incorrect rendering.
// 
// VOXEL TYPE MAP:
// Stores whether a position contains a CUBE (single voxel) or SUBDIVIDED (subcubes/microcubes).
// This enables instant voxel type checking without scanning subcube/microcube maps.

/**
 * Add cube to hash maps (both cubeMap and voxelTypeMap)
 * Call this whenever adding a cube to the cubes vector
 */
void ChunkVoxelManager::addToVoxelMaps(const glm::ivec3& localPos, Cube* cube) {
    if (cube) {
        cubeMap[localPos] = cube;                      // O(1) position -> cube lookup
        voxelTypeMap[localPos] = VoxelLocation::CUBE;  // Mark as containing a cube
    }
}

/**
 * Remove cube from hash maps
 * Call this whenever removing a cube from the cubes vector
 * 
 * CRITICAL: Also removes voxelTypeMap entry to prevent stale lookups
 */
void ChunkVoxelManager::removeFromVoxelMaps(const glm::ivec3& localPos) {
    cubeMap.erase(localPos);       // Remove position -> cube mapping
    voxelTypeMap.erase(localPos);  // Remove type information
}

/**
 * Add subcube to two-level hash map
 * Call this whenever adding a subcube to staticSubcubes vector
 * 
 * STRUCTURE: subcubeMap[cubePos][subcubePos] = Subcube*
 * - First level: parent cube position
 * - Second level: subcube position within parent (0-2, 0-2, 0-2)
 * 
 * VOXEL TYPE: Marks position as SUBDIVIDED (contains subcubes, not a solid cube)
 */
void ChunkVoxelManager::addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube) {
    if (subcube) {
        subcubeMap[localPos][subcubePos] = subcube;           // Two-level lookup
        voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;   // Mark as subdivided
    }
}

/**
 * Remove subcube from two-level hash map with automatic cleanup
 * 
 * CLEANUP LOGIC:
 * If removing the last subcube at a cube position, erase the entire parent entry.
 * This prevents empty hash map entries from accumulating (memory leak).
 * 
 * Example:
 * - Before: subcubeMap[10,5,3] = { (0,0,0)->Subcube1, (1,0,0)->Subcube2 }
 * - Remove (0,0,0): subcubeMap[10,5,3] = { (1,0,0)->Subcube2 }
 * - Remove (1,0,0): subcubeMap[10,5,3] is ERASED entirely (empty map)
 */
void ChunkVoxelManager::removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    auto it = subcubeMap.find(localPos);
    if (it != subcubeMap.end()) {
        it->second.erase(subcubePos);  // Remove specific subcube
        if (it->second.empty()) {
            subcubeMap.erase(localPos);  // Cleanup: remove empty parent entry
        }
    }
}

/**
 * Add microcube to three-level hash map
 * 
 * STRUCTURE: microcubeMap[cubePos][subcubePos][microcubePos] = Microcube*
 * - First level: parent cube position in chunk
 * - Second level: subcube position within cube (0-2, 0-2, 0-2)
 * - Third level: microcube position within subcube (0-2, 0-2, 0-2)
 * 
 * WHY THREE LEVELS?
 * Microcubes are 1/9 scale (1/3 * 1/3), requiring triple hierarchy:
 * Chunk -> Cube (32x32x32) -> Subcube (3x3x3) -> Microcube (3x3x3)
 * 
 * TOTAL CAPACITY:
 * 32,768 cubes * 27 subcubes/cube * 27 microcubes/subcube = 23,887,872 microcubes max!
 */
void ChunkVoxelManager::addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube) {
    if (microcube) {
        microcubeMap[cubePos][subcubePos][microcubePos] = microcube;  // Three-level lookup
        voxelTypeMap[cubePos] = VoxelLocation::SUBDIVIDED;            // Mark cube as subdivided
    }
}

/**
 * Remove microcube from three-level hash map with cascading cleanup
 * 
 * CLEANUP LOGIC (cascading):
 * 1. Erase microcube from innermost map
 * 2. If subcube map becomes empty, erase entire subcube entry
 * 3. If cube map becomes empty, erase entire cube entry
 * 
 * This prevents memory leaks from empty nested maps accumulating.
 * 
 * Example:
 * microcubeMap[5,5,5][1,1,1][0,0,0] exists
 * After removal: entire chain is cleaned if it was the last microcube
 */
void ChunkVoxelManager::removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            subcubeIt->second.erase(microcubePos);  // Level 3: erase microcube
            if (subcubeIt->second.empty()) {
                cubeIt->second.erase(subcubePos);   // Level 2: cleanup empty subcube map
            }
        }
        if (cubeIt->second.empty()) {
            microcubeMap.erase(cubePos);            // Level 1: cleanup empty cube map
        }
    }
}

/**
 * Update voxelTypeMap after structural changes at a position
 * 
 * SMART TYPE DETECTION:
 * Determines what currently exists at a position and updates voxelTypeMap accordingly:
 * 1. If cube exists -> type = CUBE
 * 2. Else if subcubes exist -> type = SUBDIVIDED
 * 3. Else if microcubes exist -> type = SUBDIVIDED
 * 4. Else nothing exists -> erase entry
 * 
 * WHY NEEDED?
 * After removing voxels, we may transition from SUBDIVIDED -> empty.
 * Example: Remove last subcube at position -> should erase voxelTypeMap entry.
 * 
 * WHEN TO CALL:
 * After any operation that changes voxel hierarchy (remove cube, remove all subcubes, etc.)
 */
void ChunkVoxelManager::updateVoxelMaps(const glm::ivec3& localPos) {
    // Get the cube at this position (if any)
    Cube* cube = getCubeHelper(localPos);
    
    // Update the maps based on what exists at this position
    if (cube) {
        addToVoxelMaps(localPos, cube);
    } else {
        // Check if subcubes exist
        auto subcubeIt = subcubeMap.find(localPos);
        if (subcubeIt != subcubeMap.end() && !subcubeIt->second.empty()) {
            voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
        } else {
            // Check if microcubes exist
            auto microIt = microcubeMap.find(localPos);
            if (microIt != microcubeMap.end() && !microIt->second.empty()) {
                voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
            } else {
                // Nothing exists at this position
                voxelTypeMap.erase(localPos);
            }
        }
    }
}

/**
 * Initialize all hash maps from voxel vectors
 * 
 * WHEN TO CALL:
 * - After loading chunk from disk (WorldStorage)
 * - After major structural changes requiring full rebuild
 * - During chunk initialization
 * 
 * ALGORITHM:
 * 1. Clear all existing hash maps (fresh start)
 * 2. Iterate through cubes vector, populate cubeMap and voxelTypeMap
 * 3. Iterate through staticSubcubes vector, populate subcubeMap
 * 4. Iterate through staticMicrocubes vector, populate microcubeMap
 * 
 * PERFORMANCE:
 * O(n) where n = total voxels, but only called occasionally (not every frame)
 * Typical: ~100-1000 voxels per chunk, takes <1ms
 * 
 * COORDINATE CONVERSION:
 * Subcubes/microcubes store world positions, but hash maps use local positions.
 * Conversion: localPos = worldPos - worldOrigin
 */
void ChunkVoxelManager::initializeVoxelMaps() {
    // Clear existing maps (fresh start - prevents stale entries)
    cubeMap.clear();
    subcubeMap.clear();
    microcubeMap.clear();
    voxelTypeMap.clear();
    
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    
    // BUILD CUBE MAP:
    // Iterate through cubes vector and populate hash map
    auto& cubes = m_getCubes();
    for (size_t i = 0; i < cubes.size(); ++i) {
        Cube* cube = cubes[i].get();
        if (cube) {
            glm::ivec3 localPos = cube->getPosition();  // Already in local coordinates
            cubeMap[localPos] = cube;
            voxelTypeMap[localPos] = VoxelLocation::CUBE;
        }
    }
    
    // Build subcubeMap from static subcubes
    auto& staticSubcubes = m_getStaticSubcubes();
    for (const auto& subcube : staticSubcubes) {
        if (subcube) {
            glm::ivec3 parentWorldPos = subcube->getPosition();
            glm::ivec3 localPos = parentWorldPos - worldOrigin;
            glm::ivec3 subcubePos = subcube->getLocalPosition();
            subcubeMap[localPos][subcubePos] = subcube.get();
            voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
        }
    }
    
    // Build microcubeMap from static microcubes
    auto& staticMicrocubes = m_getStaticMicrocubes();
    for (const auto& microcube : staticMicrocubes) {
        if (microcube) {
            glm::ivec3 parentWorldPos = microcube->getParentCubePosition();
            glm::ivec3 cubePos = parentWorldPos - worldOrigin;
            glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();
            glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();
            microcubeMap[cubePos][subcubePos][microcubePos] = microcube.get();
            voxelTypeMap[cubePos] = VoxelLocation::SUBDIVIDED;
        }
    }
    
    LOG_DEBUG_FMT("ChunkVoxelManager", "Initialized voxel maps: " 
              << cubeMap.size() << " cubes, " 
              << subcubeMap.size() << " subdivided positions, "
              << microcubeMap.size() << " microcube positions");
}

// =============================================================================
// Voxel location resolution
// =============================================================================

VoxelLocation ChunkVoxelManager::resolveLocalPosition(
    const glm::ivec3& localPos
) const {
    VoxelLocation location;
    
    // Check voxelTypeMap first for O(1) lookup
    auto typeIt = voxelTypeMap.find(localPos);
    if (typeIt != voxelTypeMap.end()) {
        location.type = typeIt->second;
        // Note: VoxelLocation doesn't store cube pointer - caller must use getCubeAt if needed
    } else {
        location.type = VoxelLocation::EMPTY;
    }
    
    return location;
}

bool ChunkVoxelManager::hasVoxelAt(const glm::ivec3& localPos) const {
    return voxelTypeMap.find(localPos) != voxelTypeMap.end();
}

bool ChunkVoxelManager::hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    auto it = subcubeMap.find(localPos);
    if (it != subcubeMap.end()) {
        return it->second.find(subcubePos) != it->second.end();
    }
    return false;
}

VoxelLocation::Type ChunkVoxelManager::getVoxelType(const glm::ivec3& localPos) const {
    auto it = voxelTypeMap.find(localPos);
    if (it != voxelTypeMap.end()) {
        return it->second;
    }
    return VoxelLocation::EMPTY;
}

Cube* ChunkVoxelManager::getCubeAtFast(const glm::ivec3& localPos) {
    auto it = cubeMap.find(localPos);
    return (it != cubeMap.end()) ? it->second : nullptr;
}

const Cube* ChunkVoxelManager::getCubeAtFast(const glm::ivec3& localPos) const {
    auto it = cubeMap.find(localPos);
    return (it != cubeMap.end()) ? it->second : nullptr;
}

// =============================================================================
// Helper functions for voxel access
// =============================================================================

Cube* ChunkVoxelManager::getCubeHelper(const glm::ivec3& localPos) const {
    // First try fast lookup
    auto it = cubeMap.find(localPos);
    if (it != cubeMap.end()) {
        return it->second;
    }
    
    // Fallback: indexed access
    size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
    auto& cubes = m_getCubes();
    if (index >= cubes.size()) return nullptr;
    return cubes[index].get();
}

Subcube* ChunkVoxelManager::getSubcubeHelper(
    const glm::ivec3& localPos, 
    const glm::ivec3& subcubePos
) const {
    // Try hash map lookup first
    auto it = subcubeMap.find(localPos);
    if (it != subcubeMap.end()) {
        auto subcubeIt = it->second.find(subcubePos);
        if (subcubeIt != it->second.end()) {
            return subcubeIt->second;
        }
    }
    
    // Fallback: linear search (slower, but handles inconsistent state)
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    auto& staticSubcubes = m_getStaticSubcubes();
    for (const auto& subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube.get();
        }
    }
    return nullptr;
}

std::vector<Subcube*> ChunkVoxelManager::getSubcubesHelper(
    const glm::ivec3& localPos
) const {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + localPos;
    
    auto& staticSubcubes = m_getStaticSubcubes();
    for (const auto& subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube.get());
        }
    }
    return result;
}

Microcube* ChunkVoxelManager::getMicrocubeHelper(
    const glm::ivec3& cubePos, 
    const glm::ivec3& subcubePos, 
    const glm::ivec3& microcubePos
) const {
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            auto microcubeIt = subcubeIt->second.find(microcubePos);
            if (microcubeIt != subcubeIt->second.end()) {
                return microcubeIt->second;
            }
        }
    }
    return nullptr;
}

std::vector<Microcube*> ChunkVoxelManager::getMicrocubesHelper(
    const glm::ivec3& cubePos, 
    const glm::ivec3& subcubePos
) const {
    std::vector<Microcube*> result;
    
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            for (const auto& microcubePair : subcubeIt->second) {
                if (microcubePair.second) {
                    result.push_back(microcubePair.second);
                }
            }
        }
    }
    
    return result;
}

// =============================================================================
// Cube operations
// =============================================================================

bool ChunkVoxelManager::addCube(
    const glm::ivec3& localPos
) {
    return addCube(localPos, "");
}

bool ChunkVoxelManager::addCube(
    const glm::ivec3& localPos, 
    const std::string& material
) {
    // Validate position
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return false;
    }
    
    size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
    auto& cubes = m_getCubes();
    
    // Ensure cubes vector is properly sized (32x32x32 = 32768 elements)
    if (cubes.size() < 32 * 32 * 32) {
        cubes.resize(32 * 32 * 32);
    }
    
    // If cube already exists, just ensure it's not broken
    if (cubes[index]) {
        cubes[index]->setBroken(false);
        if (!material.empty()) {
            cubes[index]->setMaterial(material);
        }
        // Update hash maps for existing cube
        addToVoxelMaps(localPos, cubes[index].get());
    } else {
        // Create new cube
        if (!material.empty()) {
            cubes[index] = std::make_unique<Cube>(localPos, material);
        } else {
            cubes[index] = std::make_unique<Cube>(localPos);
        }
        // Update hash maps for new cube
        addToVoxelMaps(localPos, cubes[index].get());
    }
    
    // Mark chunk as dirty for smart saving
    m_setDirty(true);
    
    // Add collision shape with reference counting
    m_addCollision(localPos);
    
    // Only update neighbors during individual operations, not bulk loading
    if (!m_isInBulkOperation()) {
        m_updateNeighborCollisions(localPos);
    }
    
    m_setNeedsUpdate(true);
    
    return true;
}

bool ChunkVoxelManager::removeCube(
    const glm::ivec3& localPos
) {
    // Validate position
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return false;
    }
    
    size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
    auto& cubes = m_getCubes();
    if (index >= cubes.size() || !cubes[index]) return false;
    
    // Delete the cube from memory
    cubes[index].reset();
    
    // Update hash maps to reflect removal
    removeFromVoxelMaps(localPos);
    
    // Remove collision shape with proper memory management
    m_removeCollision(localPos);
    
    // Update collision shapes of neighboring cubes that might now be exposed
    m_updateNeighborCollisions(localPos);
    
    // Mark chunk as dirty for smart saving
    m_setDirty(true);
    LOG_DEBUG_FMT("ChunkVoxelManager", "Removed cube at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
              << ") - Chunk now DIRTY for save");
    
    // Immediately rebuild faces to remove the cube from GPU buffer
    m_rebuildFaces();
    m_updateVulkanBuffer();
    
    return true;
}

int ChunkVoxelManager::removeCubesBatch(const std::vector<glm::ivec3>& positions) {
    auto& cubes = m_getCubes();
    int removed = 0;

    for (const auto& localPos : positions) {
        if (localPos.x < 0 || localPos.x >= 32 ||
            localPos.y < 0 || localPos.y >= 32 ||
            localPos.z < 0 || localPos.z >= 32) {
            continue;
        }
        size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
        if (index >= cubes.size() || !cubes[index]) continue;

        cubes[index].reset();
        removeFromVoxelMaps(localPos);
        m_removeCollision(localPos);
        ++removed;
    }

    if (removed > 0) {
        m_setDirty(true);
        m_rebuildFaces();
        m_updateVulkanBuffer();
    }
    return removed;
}

int ChunkVoxelManager::addCubesBatch(const std::vector<glm::ivec3>& positions, const std::string& material) {
    auto& cubes = m_getCubes();
    int added = 0;

    if (cubes.size() < 32 * 32 * 32) {
        cubes.resize(32 * 32 * 32);
    }

    for (const auto& localPos : positions) {
        if (localPos.x < 0 || localPos.x >= 32 ||
            localPos.y < 0 || localPos.y >= 32 ||
            localPos.z < 0 || localPos.z >= 32) {
            continue;
        }
        size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
        if (cubes[index]) {
            cubes[index]->setBroken(false);
            if (!material.empty()) cubes[index]->setMaterial(material);
        } else {
            if (!material.empty()) {
                cubes[index] = std::make_unique<Cube>(localPos, material);
            } else {
                cubes[index] = std::make_unique<Cube>(localPos);
            }
        }
        addToVoxelMaps(localPos, cubes[index].get());
        m_addCollision(localPos);
        ++added;
    }

    if (added > 0) {
        m_setDirty(true);
        m_setNeedsUpdate(true);
        m_rebuildFaces();
        m_updateVulkanBuffer();
    }
    return added;
}

// =============================================================================
// Subcube operations
// =============================================================================

bool ChunkVoxelManager::subdivideAt(
    const glm::ivec3& localPos
) {
    // Check if position is valid
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return false;
    }
    
    // Get the cube at this position
    Cube* cube = getCubeHelper(localPos);
    if (!cube) return false;
    
    // Check if already subdivided
    auto existingSubcubes = getSubcubesHelper(localPos);
    if (!existingSubcubes.empty()) return false;
    
    // Inherit material from parent cube
    std::string material = cube->getMaterialName();
    
    // Create 27 subcubes (3x3x3)
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    auto& staticSubcubes = m_getStaticSubcubes();
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                glm::ivec3 subcubeLocalPos(x, y, z);
                
                auto newSubcube = std::make_unique<Subcube>(parentWorldPos, subcubeLocalPos, material);
                Subcube* rawPtr = newSubcube.get();
                staticSubcubes.push_back(std::move(newSubcube));
                
                // Update hash maps for each subcube
                addSubcubeToMaps(localPos, subcubeLocalPos, rawPtr);
            }
        }
    }
    
    // Delete the parent cube completely
    cubeMap.erase(localPos);
    size_t cubeIndex = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
    auto& cubes = m_getCubes();
    if (cubeIndex < cubes.size() && cubes[cubeIndex].get() == cube) {
        cubes[cubeIndex].reset();
        LOG_DEBUG_FMT("ChunkVoxelManager", "Completely removed parent cube at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - replaced by 27 subcubes");
    }
    
    // Update voxelTypeMap to mark position as subdivided
    voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
    
    // Mark for update and as dirty
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

bool ChunkVoxelManager::addSubcube(
    const glm::ivec3& parentPos,
    const glm::ivec3& subcubePos,
    const std::string& material
) {
    // Check if position is valid
    if (parentPos.x < 0 || parentPos.x >= 32 ||
        parentPos.y < 0 || parentPos.y >= 32 ||
        parentPos.z < 0 || parentPos.z >= 32) {
        LOG_DEBUG("VoxelManager", "addSubcube FAIL: parentPos(%d,%d,%d) out of bounds",
                  parentPos.x, parentPos.y, parentPos.z);
        return false;
    }
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) {
        LOG_DEBUG("VoxelManager", "addSubcube FAIL: subcubePos(%d,%d,%d) out of bounds",
                  subcubePos.x, subcubePos.y, subcubePos.z);
        return false;
    }
    
    // Check if subcube already exists
    if (getSubcubeHelper(parentPos, subcubePos)) {
        LOG_DEBUG("VoxelManager", "addSubcube FAIL: subcube already exists at parent(%d,%d,%d) sub(%d,%d,%d)",
                  parentPos.x, parentPos.y, parentPos.z, subcubePos.x, subcubePos.y, subcubePos.z);
        return false;
    }
    
    // Create new subcube
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + parentPos;
    auto newSubcube = std::make_unique<Subcube>(parentWorldPos, subcubePos, material);
    Subcube* rawPtr = newSubcube.get();
    auto& staticSubcubes = m_getStaticSubcubes();
    staticSubcubes.push_back(std::move(newSubcube));
    
    // Update hash maps
    addSubcubeToMaps(parentPos, subcubePos, rawPtr);
    
    // Update collision shape
    m_addCollision(parentPos);
    
    // Mark for update and as dirty
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

bool ChunkVoxelManager::removeSubcube(
    const glm::ivec3& parentPos,
    const glm::ivec3& subcubePos
) {
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    auto& staticSubcubes = m_getStaticSubcubes();
    
    // Try to find and remove from static subcubes
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        Subcube* subcube = it->get();
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            staticSubcubes.erase(it);
            
            // Update hash maps BEFORE checking remaining subcubes
            removeSubcubeFromMaps(parentPos, subcubePos);
            
            // Check if any subcubes remain at this parent position
            std::vector<Subcube*> remainingSubcubes = getSubcubesHelper(parentPos);
            
            if (remainingSubcubes.empty()) {
                // No more subcubes - remove collision shape entirely
                LOG_TRACE_FMT("ChunkVoxelManager", "No subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - removing collision shape");
                m_removeCollision(parentPos);
                
                // Position becomes empty
                voxelTypeMap.erase(parentPos);
                LOG_DEBUG_FMT("ChunkVoxelManager", "[VOXEL MAP] Position now empty at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - all subcubes removed");
            } else {
                // Still have subcubes - update collision shape
                LOG_DEBUG_FMT("ChunkVoxelManager", "[COLLISION] " << remainingSubcubes.size() 
                          << " subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - updating collision shape");
                m_removeCollision(parentPos);
                m_addCollision(parentPos);
                
                // Ensure voxelTypeMap shows SUBDIVIDED
                voxelTypeMap[parentPos] = VoxelLocation::SUBDIVIDED;
                LOG_DEBUG_FMT("ChunkVoxelManager", "[VOXEL MAP] Maintained SUBDIVIDED type at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - " << remainingSubcubes.size() << " subcubes remain");
            }
            
            m_setNeedsUpdate(true);
            m_setDirty(true);
            return true;
        }
    }
    
    return false;
}

bool ChunkVoxelManager::clearSubdivisionAt(
    const glm::ivec3& localPos
) {
    // Check if position is valid
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return false;
    }
    
    // Remove all static subcubes at this position
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + localPos;
    auto& staticSubcubes = m_getStaticSubcubes();
    auto it = staticSubcubes.begin();
    bool removedAny = false;
    
    while (it != staticSubcubes.end()) {
        auto& subcube = *it;
        if (subcube && subcube->getPosition() == parentWorldPos) {
            it = staticSubcubes.erase(it);
            removedAny = true;
        } else {
            ++it;
        }
    }
    
    // Remove all static microcubes at this cube position
    auto& staticMicrocubes = m_getStaticMicrocubes();
    auto mit = staticMicrocubes.begin();
    while (mit != staticMicrocubes.end()) {
        auto& microcube = *mit;
        if (microcube && microcube->getParentCubePosition() == parentWorldPos) {
            mit = staticMicrocubes.erase(mit);
            removedAny = true;
        } else {
            ++mit;
        }
    }
    
    // Clear the subdivision state from data structures
    subcubeMap.erase(localPos);
    microcubeMap.erase(localPos);
    voxelTypeMap.erase(localPos);
    
    if (removedAny) {
        m_removeCollision(localPos);
        LOG_DEBUG_FMT("ChunkVoxelManager", "Cleared subdivision at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - position now empty");
        m_setNeedsUpdate(true);
        m_setDirty(true);
    }
    
    return removedAny;
}

// =============================================================================
// Microcube operations
// =============================================================================

bool ChunkVoxelManager::subdivideSubcubeAt(
    const glm::ivec3& cubePos,
    const glm::ivec3& subcubePos
) {
    // Check if position is valid
    if (cubePos.x < 0 || cubePos.x >= 32 ||
        cubePos.y < 0 || cubePos.y >= 32 ||
        cubePos.z < 0 || cubePos.z >= 32) {
        return false;
    }
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) {
        return false;
    }
    
    // Get the subcube at this position
    Subcube* subcube = getSubcubeHelper(cubePos, subcubePos);
    if (!subcube) return false;
    
    // Check if already subdivided into microcubes
    auto existingMicrocubes = getMicrocubesHelper(cubePos, subcubePos);
    if (!existingMicrocubes.empty()) return false;
    
    // Inherit material from parent subcube
    std::string material = subcube->getMaterialName();
    
    // Create 27 microcubes (3x3x3)
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + cubePos;
    
    auto& staticMicrocubes = m_getStaticMicrocubes();
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                glm::ivec3 microcubeLocalPos(x, y, z);
                
                auto newMicrocube = std::make_unique<Microcube>(parentWorldPos, subcubePos, microcubeLocalPos, material);
                Microcube* rawPtr = newMicrocube.get();
                staticMicrocubes.push_back(std::move(newMicrocube));
                
                // Update hash maps for O(1) hover detection
                addMicrocubeToMaps(cubePos, subcubePos, microcubeLocalPos, rawPtr);
            }
        }
    }
    
    // Delete the parent subcube completely
    removeSubcubeFromMaps(cubePos, subcubePos);
    
    // Remove from staticSubcubes vector
    auto& staticSubcubes = m_getStaticSubcubes();
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        if (it->get() == subcube) {
            staticSubcubes.erase(it);
            LOG_DEBUG_FMT("ChunkVoxelManager", "Completely removed parent subcube at cube (" 
                      << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                      << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
                      << ") - replaced by 27 microcubes");
            break;
        }
    }
    
    // Update collision shape to create microcube collision entities
    LOG_DEBUG_FMT("ChunkVoxelManager", "[COLLISION] Creating collision shapes for 27 new microcubes at cube (" 
              << cubePos.x << "," << cubePos.y << "," << cubePos.z << ")");
    m_removeCollision(cubePos);
    m_addCollision(cubePos);
    
    // Mark for update and as dirty
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

bool ChunkVoxelManager::addMicrocube(
    const glm::ivec3& parentCubePos,
    const glm::ivec3& subcubePos,
    const glm::ivec3& microcubePos,
    const std::string& material
) {
    // Validate positions
    if (parentCubePos.x < 0 || parentCubePos.x >= 32 ||
        parentCubePos.y < 0 || parentCubePos.y >= 32 ||
        parentCubePos.z < 0 || parentCubePos.z >= 32) {
        return false;
    }
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) {
        return false;
    }
    if (microcubePos.x < 0 || microcubePos.x >= 3 || 
        microcubePos.y < 0 || microcubePos.y >= 3 || 
        microcubePos.z < 0 || microcubePos.z >= 3) {
        return false;
    }
    
    // Check if microcube already exists
    if (getMicrocubeHelper(parentCubePos, subcubePos, microcubePos)) {
        return false;
    }
    
    // Check if this is the first microcube at this subcube position
    auto existingMicrocubes = getMicrocubesHelper(parentCubePos, subcubePos);
    if (existingMicrocubes.empty()) {
        // Check if parent subcube exists and remove it if found
        Subcube* parentSubcube = getSubcubeHelper(parentCubePos, subcubePos);
        if (parentSubcube) {
            LOG_WARN_FMT("ChunkVoxelManager", "[DATA INTEGRITY] Found parent subcube when adding first microcube at cube (" 
                      << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                      << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                      << ") - removing parent (possible database corruption)");
            
            // Remove from maps and vector
            removeSubcubeFromMaps(parentCubePos, subcubePos);
            auto& staticSubcubes = m_getStaticSubcubes();
            for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
                if (it->get() == parentSubcube) {
                    staticSubcubes.erase(it);
                    break;
                }
            }
        }
    }
    
    // Create new microcube
    glm::ivec3 parentWorldPos = m_getWorldOrigin() + parentCubePos;
    auto newMicrocube = std::make_unique<Microcube>(parentWorldPos, subcubePos, microcubePos, material);
    Microcube* rawPtr = newMicrocube.get();
    auto& staticMicrocubes = m_getStaticMicrocubes();
    staticMicrocubes.push_back(std::move(newMicrocube));
    
    // Update hash maps
    addMicrocubeToMaps(parentCubePos, subcubePos, microcubePos, rawPtr);
    
    // Update collision shape for this parent cube position
    m_removeCollision(parentCubePos);
    m_addCollision(parentCubePos);
    
    // Mark for update and as dirty
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

bool ChunkVoxelManager::removeMicrocube(
    const glm::ivec3& parentCubePos,
    const glm::ivec3& subcubePos,
    const glm::ivec3& microcubePos
) {
    LOG_INFO_FMT("ChunkVoxelManager", "[REMOVE MICROCUBE] Called for cube (" << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
              << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
              << ") micro (" << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
    
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    auto& staticMicrocubes = m_getStaticMicrocubes();
    
    // Try to find and remove from static microcubes
    for (auto it = staticMicrocubes.begin(); it != staticMicrocubes.end(); ++it) {
        Microcube* microcube = it->get();
        if (microcube && 
            microcube->getParentCubePosition() == (worldOrigin + parentCubePos) &&
            microcube->getSubcubeLocalPosition() == subcubePos &&
            microcube->getMicrocubeLocalPosition() == microcubePos) {
            
            LOG_INFO("ChunkVoxelManager", "[REMOVE MICROCUBE] Found microcube to remove");
            
            // Remove from hash maps
            removeMicrocubeFromMaps(parentCubePos, subcubePos, microcubePos);
            
            // Remove from vector (unique_ptr auto-deletes)
            staticMicrocubes.erase(it);
            
            LOG_INFO("ChunkVoxelManager", "[REMOVE MICROCUBE] Checking for remaining microcubes");
            
            // Check if any microcubes remain at this parent position (O(1) map lookup)
            bool hasMicrocubes = microcubeMap.count(parentCubePos) > 0;
            
            if (hasMicrocubes) {
                // Still have microcubes - update collision shape
                LOG_INFO_FMT("ChunkVoxelManager", "[COLLISION] Microcubes remain at parent pos (" 
                          << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                          << ") - updating collision shape");
                m_removeCollision(parentCubePos);
                m_addCollision(parentCubePos);
            } else {
                // No more microcubes - but check if subcubes still exist
                auto remainingSubcubes = getSubcubesHelper(parentCubePos);
                if (!remainingSubcubes.empty()) {
                    LOG_INFO_FMT("ChunkVoxelManager", "[VOXEL MAP] No microcubes remain but " << remainingSubcubes.size() 
                              << " subcubes still exist at (" << parentCubePos.x << "," << parentCubePos.y 
                              << "," << parentCubePos.z << ") - keeping SUBDIVIDED state and updating collision");
                    m_removeCollision(parentCubePos);
                    m_addCollision(parentCubePos);
                } else {
                    // No microcubes AND no subcubes - completely empty position
                    LOG_INFO_FMT("ChunkVoxelManager", "[COLLISION] No microcubes or subcubes remain at parent pos (" 
                              << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                              << ") - removing collision shape and voxel type entry");
                    m_removeCollision(parentCubePos);
                    voxelTypeMap.erase(parentCubePos);
                }
            }
            
            // Mark for update
            m_setNeedsUpdate(true);
            m_setDirty(true);
            
            return true;
        }
    }
    
    return false;
}

bool ChunkVoxelManager::clearMicrocubesAt(
    const glm::ivec3& cubePos,
    const glm::ivec3& subcubePos
) {
    auto microcubes = getMicrocubesHelper(cubePos, subcubePos);
    if (microcubes.empty()) return false;
    
    LOG_INFO_FMT("ChunkVoxelManager", "[CLEAR MICROCUBES] Removing all microcubes at cube (" 
              << cubePos.x << "," << cubePos.y << "," << cubePos.z 
              << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
              << ") - leaving empty space");
    
    glm::ivec3 worldOrigin = m_getWorldOrigin();
    auto& staticMicrocubes = m_getStaticMicrocubes();
    
    // Remove all microcubes at this subcube position
    for (auto it = staticMicrocubes.begin(); it != staticMicrocubes.end(); ) {
        auto& microcube = *it;
        if (microcube && 
            microcube->getParentCubePosition() == (worldOrigin + cubePos) &&
            microcube->getSubcubeLocalPosition() == subcubePos) {
            
            // Remove from hash maps
            removeMicrocubeFromMaps(cubePos, subcubePos, microcube->getMicrocubeLocalPosition());
            
            // Remove from vector (unique_ptr auto-deletes)
            it = staticMicrocubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Check if any microcubes remain at the parent cube position (O(1) map lookup)
    bool hasMicrocubes = microcubeMap.count(cubePos) > 0;
    
    if (hasMicrocubes) {
        // Still have microcubes at other subcube positions - update collision
        m_removeCollision(cubePos);
        m_addCollision(cubePos);
    } else {
        // No more microcubes - but check if subcubes still exist
        auto remainingSubcubes = getSubcubesHelper(cubePos);
        if (!remainingSubcubes.empty()) {
            LOG_INFO_FMT("ChunkVoxelManager", "[VOXEL MAP] No microcubes remain but " << remainingSubcubes.size() 
                      << " subcubes still exist at (" << cubePos.x << "," << cubePos.y 
                      << "," << cubePos.z << ") - keeping SUBDIVIDED state and updating collision");
            m_removeCollision(cubePos);
            m_addCollision(cubePos);
        } else {
            // No microcubes AND no subcubes - completely empty position
            LOG_INFO_FMT("ChunkVoxelManager", "[CLEAR MICROCUBES] No microcubes or subcubes remain at (" 
                      << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                      << ") - removing collision and voxel type entry");
            m_removeCollision(cubePos);
            voxelTypeMap.erase(cubePos);
        }
    }
    
    // Mark for update
    m_setNeedsUpdate(true);
    m_setDirty(true);
    
    return true;
}

} // namespace Phyxel
