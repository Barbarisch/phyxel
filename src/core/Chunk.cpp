#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "physics/CollisionSpatialGrid.h"
#include "utils/Logger.h"
#include <stdexcept>
#include <cstring>
#include <random>
#include <iostream>
#include <iomanip>
#include <unordered_set>

// Bullet Physics includes
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>

namespace VulkanCube {

// TEMPORARY COMPATIBILITY MACROS - will be removed once physics extraction is complete
// These macros allow existing code to access physics manager members without massive refactoring
#define physicsWorld (physicsManager.getPhysicsWorldRef())
#define chunkPhysicsBody (physicsManager.getChunkPhysicsBodyRef())
#define chunkCollisionShape (physicsManager.getChunkCollisionShapeRef())
#define chunkTriangleMesh (physicsManager.getChunkTriangleMeshRef())
#define collisionGrid (physicsManager.getCollisionGrid())
#define collisionNeedsUpdate (physicsManager.getCollisionNeedsUpdateRef())

Chunk::Chunk(const glm::ivec3& origin) 
    : worldOrigin(origin) {
    cubes.reserve(32 * 32 * 32);              // Reserve space for all possible cubes
    staticSubcubes.reserve(1000);             // Reserve reasonable space for static subcubes
}

Chunk::~Chunk() {
    // Delete all cube pointers to free memory
    for (Cube* cube : cubes) {
        delete cube;
    }
    cubes.clear();
    
    // Delete all static subcube pointers to free memory
    for (Subcube* subcube : staticSubcubes) {
        delete subcube;
    }
    staticSubcubes.clear();
    
    cleanupVulkanResources();
    cleanupPhysicsResources();
}

Chunk::Chunk(Chunk&& other) noexcept
    : cubes(std::move(other.cubes))
    , staticSubcubes(std::move(other.staticSubcubes))
    , worldOrigin(other.worldOrigin)
    , renderManager(std::move(other.renderManager))
    , physicsManager(std::move(other.physicsManager))
    , device(other.device)
    , physicalDevice(other.physicalDevice) {
    
    // Reset other object's device handles
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        cleanupVulkanResources();
        cleanupPhysicsResources();
        
        // Move data
        cubes = std::move(other.cubes);
        staticSubcubes = std::move(other.staticSubcubes);
        worldOrigin = other.worldOrigin;
        renderManager = std::move(other.renderManager);
        physicsManager = std::move(other.physicsManager);
        device = other.device;
        physicalDevice = other.physicalDevice;
        
        // Reset other object's device handles
        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
    }
    return *this;
}

void Chunk::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
    // Initialize renderManager with device handles
    renderManager.initialize(dev, physDev);
    // Initialize physicsManager with chunk origin
    physicsManager.initialize(nullptr, worldOrigin); // physicsWorld set separately via setPhysicsWorld
}

Cube* Chunk::getCubeAt(const glm::ivec3& localPos) {
    if (!isValidLocalPosition(localPos)) return nullptr;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return nullptr;
    
    // Return the pointer (which could be nullptr for deleted cubes)
    return cubes[index];
}

const Cube* Chunk::getCubeAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return nullptr;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return nullptr;
    
    // Return the pointer (which could be nullptr for deleted cubes)
    return cubes[index];
}

Cube* Chunk::getCubeAtIndex(size_t index) {
    if (index >= cubes.size()) return nullptr;
    return cubes[index];
}

const Cube* Chunk::getCubeAtIndex(size_t index) const {
    if (index >= cubes.size()) return nullptr;
    return cubes[index];
}

bool Chunk::setCubeColor(const glm::ivec3& localPos, const glm::vec3& color) {
    Cube* cube = getCubeAt(localPos);
    if (!cube) return false;
    
    cube->setColor(color);
    renderManager.setNeedsUpdate(true);
    
    // std::cout << "[CHUNK] Set cube color at local pos: (" 
    //           << localPos.x << "," << localPos.y << "," << localPos.z 
    //           << ") to: (" << color.x << "," << color.y << "," << color.z << ")" << std::endl;
    
    return true;
}

bool Chunk::removeCube(const glm::ivec3& localPos) {
    if (!isValidLocalPosition(localPos)) return false;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size() || !cubes[index]) return false; // No cube exists at this position
    
    // Actually delete the cube from memory
    delete cubes[index];
    cubes[index] = nullptr; // Mark as deleted
    
    // Update hash maps to reflect removal
    removeFromVoxelMaps(localPos);
    
    // IMPROVED: Remove collision shape with proper memory management
    removeCollisionEntities(localPos);
    
    // CRITICAL: Update collision shapes of neighboring cubes that might now be exposed
    updateNeighborCollisionShapes(localPos);
    
    // Mark chunk as dirty for smart saving
    setDirty(true);
    LOG_DEBUG_FMT("Chunk", "Removed cube at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
              << ") - Chunk now DIRTY for save");
    
    // Immediately rebuild faces to remove the cube from GPU buffer
    rebuildFaces();
    updateVulkanBuffer();
    
    return true;
}

bool Chunk::addCube(const glm::ivec3& localPos, const glm::vec3& color) {
    if (!isValidLocalPosition(localPos)) return false;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return false;
    
    // If cube already exists, just update its color
    if (cubes[index]) {
        cubes[index]->setColor(color);
        cubes[index]->setOriginalColor(color); // Update original color too
        cubes[index]->setBroken(false);
        // Update hash maps for existing cube
        addToVoxelMaps(localPos, cubes[index]);
    } else {
        // Create new cube
        cubes[index] = new Cube(localPos, color);
        cubes[index]->setOriginalColor(color); // Ensure original color is set
        // Update hash maps for new cube
        addToVoxelMaps(localPos, cubes[index]);
    }
    
    // Mark chunk as dirty for smart saving
    setDirty(true);
    
    // IMPROVED: Add collision shape with reference counting
    addCollisionEntity(localPos);
    
    // CRITICAL: Only update neighbors during individual operations, not bulk loading
    if (!physicsManager.isInBulkOperation()) {
        updateNeighborCollisionShapes(localPos);
    }
    
    renderManager.setNeedsUpdate(true);
    
    // std::cout << "[CHUNK] Added/restored cube at local pos: (" 
    //           << localPos.x << "," << localPos.y << "," << localPos.z 
    //           << ") with color: (" << color.x << "," << color.y << "," << color.z << ")" << std::endl;
    
    return true;
}

void Chunk::populateWithCubes() {
    cubes.clear();
    
    // Random number generator for colors
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
    
    // Create logical cubes (32x32x32 grid)
    // CRITICAL: Loop order determines index formula in localToIndex()
    // X-major order (X outermost, Z innermost) requires Z-minor indexing: z + y*32 + x*1024
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            for (int z = 0; z < 32; ++z) {
                Cube* cube = new Cube();
                cube->setPosition(glm::ivec3(x, y, z));  // Local position within chunk (for 5-bit packing efficiency)
                glm::vec3 cubeColor = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                cube->setColor(cubeColor);
                cube->setOriginalColor(cubeColor); // Store original color
                cubes.push_back(cube);
            }
        }
    }
    
    // Mark chunk as dirty since it has new content
    setDirty(true);
    
    // Initialize hash maps for O(1) lookups
    initializeVoxelMaps();
    
    // std::cout << "[CHUNK] Populated chunk at origin (" 
    //           << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
    //           << ") with " << cubes.size() << " cubes" << std::endl;
}

void Chunk::initializeForLoading() {
    cubes.clear();
    
    // Initialize sparse cube storage (32x32x32 array with nullptr entries)
    cubes.resize(32 * 32 * 32, nullptr);
    
    // Clear any existing subcubes
    for (Subcube* subcube : staticSubcubes) {
        delete subcube;
    }
    staticSubcubes.clear();
    
    // Clear any existing microcubes
    for (Microcube* microcube : staticMicrocubes) {
        delete microcube;
    }
    staticMicrocubes.clear();
    
    // Set bulk operation flag to prevent neighbor collision updates during loading
    physicsManager.setInBulkOperation(true);
    
    LOG_DEBUG_FMT("Chunk", "Initialized chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z 
              << ") for database loading");
}

void Chunk::rebuildFaces() {
    // Call the cross-chunk version without a neighbor lookup function
    // This will only do intra-chunk culling
    rebuildFaces(nullptr);
}

void Chunk::rebuildFaces(const NeighborLookupFunc& getNeighborCube) {
    // Delegate to render manager
    renderManager.rebuildAllFaces(cubes, staticSubcubes, staticMicrocubes, worldOrigin, getNeighborCube);
}

void Chunk::updateVulkanBuffer() {
    renderManager.updateVulkanBuffer();
}

void Chunk::updateSingleCubeTexture(const glm::ivec3& localPos, uint16_t textureIndex) {
    if (!isValidLocalPosition(localPos)) return;
    renderManager.updateSingleCubeTexture(localPos, textureIndex, cubes);
}

void Chunk::updateSingleSubcubeTexture(const glm::ivec3& parentLocalPos, const glm::ivec3& subcubePos, uint16_t textureIndex) {
    if (!isValidLocalPosition(parentLocalPos)) return;
    renderManager.updateSingleSubcubeTexture(parentLocalPos, subcubePos, textureIndex, staticSubcubes, worldOrigin);
}

void Chunk::createVulkanBuffer() {
    renderManager.createVulkanBuffer();
}

void Chunk::cleanupVulkanResources() {
    renderManager.cleanupVulkanResources();
}

void Chunk::ensureBufferCapacity(size_t requiredInstances) {
    renderManager.ensureBufferCapacity(requiredInstances);
}

void Chunk::logBufferUtilization() const {
    renderManager.logBufferUtilization();
}

size_t Chunk::localToIndex(const glm::ivec3& localPos) {
    // CRITICAL: This must match the loop order in populateWithCubes()
    // X-major order (X outermost, Z innermost): z + y*32 + x*1024
    return localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
}

glm::ivec3 Chunk::indexToLocal(size_t index) {
    // Reverse the localToIndex calculation
    int x = index / (32 * 32);
    int y = (index % (32 * 32)) / 32;
    int z = index % 32;
    return glm::ivec3(x, y, z);
}

glm::vec3 Chunk::getMinBounds() const {
    // Convert world origin (ivec3) to world position (vec3) for bounding box
    return glm::vec3(worldOrigin);
}

glm::vec3 Chunk::getMaxBounds() const {
    // Chunk spans 32x32x32 units from worldOrigin to worldOrigin + (31,31,31)
    // For bounding box calculations, we want worldOrigin + (32,32,32) as max bounds
    return glm::vec3(worldOrigin) + glm::vec3(32.0f, 32.0f, 32.0f);
}

// =============================================================================
// SUBCUBE MANAGEMENT METHODS
// =============================================================================

Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    // Search in static subcubes only
    for (Subcube* subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube;
        }
    }
    return nullptr;
}

const Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    // Search in static subcubes only
    for (const Subcube* subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube;
        }
    }
    return nullptr;
}

std::vector<Subcube*> Chunk::getSubcubesAt(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    // Collect from static subcubes only
    for (Subcube* subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube);
        }
    }
    return result;
}

std::vector<Subcube*> Chunk::getStaticSubcubesAt(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    for (Subcube* subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube);
        }
    }
    return result;
}

// =============================================================================
// Microcube Access Functions
// =============================================================================

Microcube* Chunk::getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
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

const Microcube* Chunk::getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) const {
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

std::vector<Microcube*> Chunk::getMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    std::vector<Microcube*> result;
    
    // Use hash map lookup instead of linear search - O(1) instead of O(n)
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            // Found the subcube level, now collect all microcubes (0-27)
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
// NEW: O(1) VoxelLocation resolution system for optimized hover detection
// =============================================================================

VoxelLocation Chunk::resolveLocalPosition(const glm::ivec3& localPos) const {
    // Quick bounds check
    if (!isValidLocalPosition(localPos)) {
        return VoxelLocation();
    }
    
    // O(1) voxel type lookup
    auto typeIt = voxelTypeMap.find(localPos);
    if (typeIt == voxelTypeMap.end()) {
        return VoxelLocation(); // No voxel at this position
    }
    
    glm::ivec3 worldPos = worldOrigin + localPos;
    VoxelLocation::Type type = typeIt->second;
    
    if (type == VoxelLocation::SUBDIVIDED) {
        VoxelLocation location;
        location.type = VoxelLocation::SUBDIVIDED;
        location.chunk = const_cast<Chunk*>(this);
        location.localPos = localPos;
        location.worldPos = worldPos;
        location.subcubePos = glm::ivec3(-1);
        return location;
    } else if (type == VoxelLocation::CUBE) {
        VoxelLocation location;
        location.type = VoxelLocation::CUBE;
        location.chunk = const_cast<Chunk*>(this);
        location.localPos = localPos;
        location.worldPos = worldPos;
        location.subcubePos = glm::ivec3(-1);
        return location;
    }
    
    return VoxelLocation();
}

bool Chunk::hasVoxelAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return false;
    return voxelTypeMap.find(localPos) != voxelTypeMap.end();
}

bool Chunk::hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    auto parentIt = subcubeMap.find(localPos);
    if (parentIt == subcubeMap.end()) return false;
    
    return parentIt->second.find(subcubePos) != parentIt->second.end();
}

VoxelLocation::Type Chunk::getVoxelType(const glm::ivec3& localPos) const {
    auto it = voxelTypeMap.find(localPos);
    return (it != voxelTypeMap.end()) ? it->second : VoxelLocation::EMPTY;
}

// O(1) optimized lookups (replace linear searches)
Cube* Chunk::getCubeAtFast(const glm::ivec3& localPos) {
    auto it = cubeMap.find(localPos);
    return (it != cubeMap.end()) ? it->second : nullptr;
}

const Cube* Chunk::getCubeAtFast(const glm::ivec3& localPos) const {
    auto it = cubeMap.find(localPos);
    return (it != cubeMap.end()) ? it->second : nullptr;
}

// Internal: Maintain hash map consistency
void Chunk::updateVoxelMaps(const glm::ivec3& localPos) {
    // Check what exists at this position using legacy arrays
    Cube* cube = getCubeAt(localPos); // Use legacy method
    auto subcubes = getSubcubesAt(localPos); // Use legacy method
    
    if (!subcubes.empty()) {
        // Position has subcubes - mark as subdivided
        voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
        
        // Update subcube map
        subcubeMap[localPos].clear();
        for (Subcube* subcube : subcubes) {
            if (subcube) {
                subcubeMap[localPos][subcube->getLocalPosition()] = subcube;
            }
        }
        
        // Update cube map
        if (cube) {
            cubeMap[localPos] = cube;
        } else {
            cubeMap.erase(localPos);
        }
    } else if (cube) {
        // Position has cube but no subcubes
        voxelTypeMap[localPos] = VoxelLocation::CUBE;
        cubeMap[localPos] = cube;
        subcubeMap.erase(localPos); // Remove any leftover subcube data
    } else {
        // Position is empty
        voxelTypeMap.erase(localPos);
        cubeMap.erase(localPos);
        subcubeMap.erase(localPos);
    }
}

void Chunk::addToVoxelMaps(const glm::ivec3& localPos, Cube* cube) {
    if (cube) {
        cubeMap[localPos] = cube;
        
        // Check if we have subcubes at this position
        auto subcubes = getSubcubesAt(localPos);
        if (!subcubes.empty()) {
            voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
        } else {
            voxelTypeMap[localPos] = VoxelLocation::CUBE;
        }
    }
}

void Chunk::removeFromVoxelMaps(const glm::ivec3& localPos) {
    cubeMap.erase(localPos);
    
    // Check if we still have subcubes at this position
    auto subcubes = getSubcubesAt(localPos);
    if (!subcubes.empty()) {
        voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
    } else {
        voxelTypeMap.erase(localPos);
        subcubeMap.erase(localPos);
    }
}

void Chunk::addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube) {
    if (subcube) {
        subcubeMap[localPos][subcubePos] = subcube;
        voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
    }
}

void Chunk::removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    auto parentIt = subcubeMap.find(localPos);
    if (parentIt != subcubeMap.end()) {
        parentIt->second.erase(subcubePos);
        
        // If no subcubes left at this position, just remove the subcubeMap entry
        // The caller (removeSubcube) will handle voxelTypeMap updates after cube restoration
        if (parentIt->second.empty()) {
            subcubeMap.erase(localPos);
            LOG_TRACE_FMT("Chunk", "Removed subcubeMap entry at (" << localPos.x << "," << localPos.y << "," << localPos.z << ") - no subcubes remain");
        }
    }
}

void Chunk::addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube) {
    if (microcube) {
        microcubeMap[cubePos][subcubePos][microcubePos] = microcube;
        // Note: voxelTypeMap is already set to SUBDIVIDED by the parent subdivision
    }
}

void Chunk::removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    auto cubeIt = microcubeMap.find(cubePos);
    if (cubeIt != microcubeMap.end()) {
        auto subcubeIt = cubeIt->second.find(subcubePos);
        if (subcubeIt != cubeIt->second.end()) {
            subcubeIt->second.erase(microcubePos);
            
            if (subcubeIt->second.empty()) {
                cubeIt->second.erase(subcubePos);
                LOG_TRACE_FMT("Chunk", "Removed microcubeMap entry at cube(" << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                          << ") subcube(" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ") - no microcubes remain");
            }
            
            if (cubeIt->second.empty()) {
                microcubeMap.erase(cubePos);
            }
        }
    }
}

void Chunk::initializeVoxelMaps() {
    // Clear existing maps
    cubeMap.clear();
    subcubeMap.clear();
    microcubeMap.clear();
    voxelTypeMap.clear();
    
    LOG_DEBUG_FMT("Chunk", "Initializing voxel maps for chunk at origin (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")");
    
    // Build cube map from existing vector data
    for (size_t i = 0; i < cubes.size(); ++i) {
        if (cubes[i]) {
            glm::ivec3 localPos = indexToLocal(i);
            cubeMap[localPos] = cubes[i];
            voxelTypeMap[localPos] = VoxelLocation::CUBE;
        }
    }
    
    // Build subcube map from existing vector data
    for (Subcube* subcube : staticSubcubes) {
        if (subcube) {
            glm::ivec3 parentWorldPos = subcube->getPosition();
            glm::ivec3 localPos = parentWorldPos - worldOrigin;
            glm::ivec3 subcubePos = subcube->getLocalPosition();
            
            // Validate that this subcube belongs to this chunk
            if (isValidLocalPosition(localPos)) {
                subcubeMap[localPos][subcubePos] = subcube;
                voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
            }
        }
    }
    
    // Build microcube map from existing vector data
    for (Microcube* microcube : staticMicrocubes) {
        if (microcube) {
            glm::ivec3 parentWorldPos = microcube->getParentCubePosition();
            glm::ivec3 localPos = parentWorldPos - worldOrigin;
            glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();
            glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();
            
            // Validate that this microcube belongs to this chunk
            if (isValidLocalPosition(localPos)) {
                microcubeMap[localPos][subcubePos][microcubePos] = microcube;
                voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
            }
        }
    }
    
    LOG_DEBUG_FMT("Chunk", "Voxel maps initialized: " 
              << cubeMap.size() << " cubes, "
              << subcubeMap.size() << " subdivided positions, "
              << microcubeMap.size() << " microcube positions, "
              << voxelTypeMap.size() << " total voxels");
}

bool Chunk::subdivideAt(const glm::ivec3& localPos) {
    // Check if position is valid
    if (!isValidLocalPosition(localPos)) return false;
    
    // Get the cube at this position
    Cube* cube = getCubeAt(localPos);
    if (!cube) return false;
    
    // Check if already subdivided
    auto existingSubcubes = getSubcubesAt(localPos);
    if (!existingSubcubes.empty()) return false; // Already subdivided
    
    // Create 27 subcubes (3x3x3) with random colors for clear visual distinction
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    // Use random colors for subcubes instead of parent-based colors
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.2f, 1.0f); // Avoid very dark colors
    
    // std::cout << "[CHUNK] Creating 27 subcubes with random colors..." << std::endl;
    
    int colorIndex = 0;
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                glm::ivec3 subcubeLocalPos(x, y, z);
                
                // Generate completely random color for each subcube
                glm::vec3 subcubeColor = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                
                // std::cout << "[CHUNK]   Subcube " << colorIndex << " at (" << x << "," << y << "," << z 
                //           << ") color: (" << subcubeColor.x << "," << subcubeColor.y << "," << subcubeColor.z << ")" << std::endl;
                
                Subcube* newSubcube = new Subcube(parentWorldPos, subcubeColor, subcubeLocalPos);
                staticSubcubes.push_back(newSubcube); // Add to static subcubes list
                
                // CRITICAL: Update hash maps for each subcube for O(1) hover detection
                addSubcubeToMaps(localPos, subcubeLocalPos, newSubcube);
                
                colorIndex++;
            }
        }
    }
    
    // CRITICAL ARCHITECTURAL CHANGE: Delete the parent cube completely (don't just hide it)
    // Remove from cubeMap and cubes vector
    cubeMap.erase(localPos);
    size_t cubeIndex = localToIndex(localPos);
    if (cubeIndex < cubes.size() && cubes[cubeIndex] == cube) {
        delete cube;  // Delete the cube object
        cubes[cubeIndex] = nullptr;  // Clear the vector entry
        LOG_DEBUG_FMT("Chunk", "Completely removed parent cube at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - replaced by 27 subcubes");
    }
    
    // CRITICAL: Update voxelTypeMap to mark position as subdivided for proper hover detection
    voxelTypeMap[localPos] = VoxelLocation::SUBDIVIDED;
    
    // Debug: Verify the cube is actually hidden and subdivided
    // std::cout << "[CHUNK] Parent cube hidden. isVisible() = " << cube->isVisible() 
    //           << ", isSubdivided() = " << cube->isSubdivided() << std::endl;
    
    // Mark for update and as dirty for database persistence
    renderManager.setNeedsUpdate(true);
    setDirty(true);
    
    // std::cout << "[CHUNK] Subdivided cube at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
    //           << ") into 27 subcubes with " << staticSubcubes.size() << " static subcubes in chunk" << std::endl;
    
    return true;
}

bool Chunk::addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const glm::vec3& color) {
    // Check if position is valid
    if (!isValidLocalPosition(parentPos)) return false;
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return false;
    
    // Check if subcube already exists
    if (getSubcubeAt(parentPos, subcubePos)) return false;
    
    // Create new subcube (add to static list by default)
    glm::ivec3 parentWorldPos = worldOrigin + parentPos;
    Subcube* newSubcube = new Subcube(parentWorldPos, color, subcubePos);
    staticSubcubes.push_back(newSubcube);
    
    // Update hash maps
    addSubcubeToMaps(parentPos, subcubePos, newSubcube);
    
    // IMPROVED: Update collision shape with memory safety
    addCollisionEntity(parentPos);
    
    // Mark for update and as dirty for database persistence
    renderManager.setNeedsUpdate(true);
    setDirty(true);
    
    return true;
}

bool Chunk::removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    // Try to find and remove from static subcubes
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        Subcube* subcube = *it;
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            delete subcube;
            staticSubcubes.erase(it);
            
            // Update hash maps BEFORE checking remaining subcubes
            removeSubcubeFromMaps(parentPos, subcubePos);
            
            // CRITICAL FIX: Check if any subcubes remain at this parent position
            std::vector<Subcube*> remainingSubcubes = getStaticSubcubesAt(parentPos);
            
            if (remainingSubcubes.empty()) {
                // No more subcubes at this position - remove collision shape entirely
                LOG_TRACE_FMT("Chunk", "No subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - removing collision shape");
                removeCollisionEntities(parentPos);
                
                // ARCHITECTURAL CHANGE: Do NOT restore parent cube - leave position empty
                // The parent cube was deleted during subdivision, so position should become empty
                voxelTypeMap.erase(parentPos);
                LOG_DEBUG_FMT("Chunk", "[VOXEL MAP] Position now empty at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - all subcubes removed, no cube to restore");
            } else {
                // Still have subcubes - update collision shape to reflect remaining subcubes
                LOG_DEBUG_FMT("Chunk", "[COLLISION] " << remainingSubcubes.size() 
                          << " subcubes remain at parent pos (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - updating collision shape");
                // Remove old shape and add new one to reflect current subcube configuration
                removeCollisionEntities(parentPos);
                addCollisionEntity(parentPos);
                
                // CRITICAL: Ensure voxelTypeMap shows SUBDIVIDED since subcubes remain
                voxelTypeMap[parentPos] = VoxelLocation::SUBDIVIDED;
                LOG_DEBUG_FMT("Chunk", "[VOXEL MAP] Maintained SUBDIVIDED type at (" 
                          << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                          << ") - " << remainingSubcubes.size() << " subcubes remain");
            }
            
            renderManager.setNeedsUpdate(true);
            setDirty(true);
            return true;
        }
    }
    
    return false; // Subcube not found
}

bool Chunk::clearSubdivisionAt(const glm::ivec3& localPos) {
    // Check if position is valid
    if (!isValidLocalPosition(localPos)) return false;
    
    // Remove all static subcubes at this position
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    auto it = staticSubcubes.begin();
    bool removedAny = false;
    
    while (it != staticSubcubes.end()) {
        Subcube* subcube = *it;
        if (subcube && subcube->getPosition() == parentWorldPos) {
            delete subcube;
            it = staticSubcubes.erase(it);
            removedAny = true;
        } else {
            ++it;
        }
    }
    
    // ARCHITECTURAL CHANGE: Don't try to restore parent cube - it was deleted during subdivision
    // Clear the subdivision state from data structures
    subcubeMap.erase(localPos);
    voxelTypeMap.erase(localPos);
    
    if (removedAny) {
        LOG_DEBUG_FMT("Chunk", "[CHUNK] Cleared subdivision at local pos (" << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") - position now empty");
        renderManager.setNeedsUpdate(true);
    }
    
    return removedAny;
}

size_t Chunk::subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    // Calculate a unique index for subcube identification
    size_t parentIndex = localToIndex(parentPos);
    size_t subcubeOffset = subcubePos.x + subcubePos.y * 3 + subcubePos.z * 9; // 0-26
    return parentIndex * 27 + subcubeOffset;
}

bool Chunk::isValidLocalPosition(const glm::ivec3& localPos) const {
    return localPos.x >= 0 && localPos.x < 32 &&
           localPos.y >= 0 && localPos.y < 32 &&
           localPos.z >= 0 && localPos.z < 32;
}

// =============================================================================
// PHYSICS-RELATED METHODS
// =============================================================================

#undef physicsWorld  // Temporary: parameter name conflicts with macro
bool Chunk::breakSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, 
                        Physics::PhysicsWorld* physicsWorld, ChunkManager* chunkManager, const glm::vec3& impulseForce) {
#define physicsWorld (physicsManager.getPhysicsWorldRef())  // Restore macro
    LOG_DEBUG_FMT("Chunk", "[SUBCUBE BREAKING] Attempting to break subcube at parent (" 
                  << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                  << ") - searching through " << staticSubcubes.size() << " static subcubes");
    
    // Find the subcube in static list
    auto it = staticSubcubes.begin();
    while (it != staticSubcubes.end()) {
        Subcube* subcube = *it;
        if (subcube && 
            subcube->getPosition() == worldOrigin + parentPos && 
            subcube->getLocalPosition() == subcubePos) {
            
            // Store subcube data before removal
            glm::vec3 worldPos = subcube->getWorldPosition();
            glm::vec3 originalColor = subcube->getOriginalColor();
            bool isVisible = subcube->isVisible();
            float lifetime = subcube->getLifetime();
            
            // CRITICAL: Use proper removeSubcube to update all data structures (voxelTypeMap, subcubeMap, etc.)
            LOG_DEBUG("Chunk", "[INCREMENTAL] BEFORE: Using proper subcube removal to update all data structures");
            bool removed = removeSubcube(parentPos, subcubePos);
            if (!removed) {
                LOG_ERROR("Chunk", "[ERROR] Failed to remove subcube from data structures");
                return false;
            }
            // NEW: Fast collision update (already done in removeSubcube)
            batchUpdateCollisions();
            LOG_DEBUG("Chunk", "[INCREMENTAL] AFTER: All data structures updated and collision shape updated incrementally");
            
            // Create new dynamic subcube for physics (since original was removed)
            auto dynamicSubcube = std::make_unique<Subcube>(
                worldOrigin + parentPos, 
                originalColor, 
                subcubePos
            );
            
            // Set properties from stored data
            dynamicSubcube->setOriginalColor(originalColor);
            dynamicSubcube->setVisible(isVisible);
            dynamicSubcube->setLifetime(lifetime);
            dynamicSubcube->breakApart(); // Mark as broken
            
            // Create physics body for dynamic subcube if physics world is available
            if (physicsWorld) {
                // COORDINATE FIX: Static subcubes use corner-based coordinates, physics uses center-based
                glm::vec3 subcubeCornerPos = worldPos; // Corner position (matches static subcubes)
                glm::vec3 subcubeSize(1.0f / 3.0f); // Match visual subcube size
                glm::vec3 physicsCenterPos = subcubeCornerPos + (subcubeSize * 0.5f); // Physics center position
                
                // Create dynamic physics body at center position
                btRigidBody* rigidBody = physicsWorld->createBreakawaCube(physicsCenterPos, subcubeSize, 0.5f); // 0.5kg mass
                dynamicSubcube->setRigidBody(rigidBody);
                dynamicSubcube->setPhysicsPosition(physicsCenterPos);
                
                // NO FORCES APPLIED - subcubes break gently with gravity only
                LOG_DEBUG("Chunk", "[SUBCUBE PHYSICS] Created physics body for subcube (no forces applied - gravity only)");
                
                // Enable gravity for natural falling behavior
                if (rigidBody) {
                    rigidBody->setGravity(btVector3(0, -9.81f, 0));
                }
            }
            
            // Transfer the dynamic subcube directly to global system
            if (chunkManager) {
                // The dynamicSubcube is already properly configured, just transfer it
                chunkManager->addGlobalDynamicSubcube(std::move(dynamicSubcube));
                
                LOG_DEBUG("Chunk", "[GLOBAL TRANSFER] Moved broken subcube directly to global dynamic system (safe transfer)");
            } else {
                LOG_ERROR("Chunk", "[ERROR] No ChunkManager provided - cannot transfer to global system");
            }
            
            // Note: No need to delete subcube - it was already properly removed by removeSubcube()
            
            // Rebuild static faces only (no more dynamic faces in chunks)
            rebuildFaces();
            renderManager.setNeedsUpdate(true);
            
            LOG_DEBUG_FMT("Chunk", "[CHUNK] Broke subcube at parent pos (" 
                      << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                      << ") subcube pos (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                      << ") - transferred to global system safely");
            return true;
        }
        ++it;
    }
    
    LOG_ERROR_FMT("Chunk", "[SUBCUBE BREAKING] Subcube NOT FOUND in static list at parent (" 
                  << parentPos.x << "," << parentPos.y << "," << parentPos.z 
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z << ")");
    
    // Debug: Check if this position has microcubes instead
    auto microcubes = getMicrocubesAt(parentPos, subcubePos);
    if (!microcubes.empty()) {
        LOG_ERROR_FMT("Chunk", "[SUBCUBE BREAKING] DEBUG: Found " << microcubes.size() 
                      << " microcubes at this subcube position - subcube may have been subdivided");
    }
    
    // Debug: Check all subcubes at this parent position
    auto allSubcubes = getSubcubesAt(parentPos);
    LOG_ERROR_FMT("Chunk", "[SUBCUBE BREAKING] DEBUG: Total subcubes at parent position: " << allSubcubes.size());
    for (auto* sc : allSubcubes) {
        if (sc) {
            glm::ivec3 scPos = sc->getLocalPosition();
            LOG_ERROR_FMT("Chunk", "[SUBCUBE BREAKING] DEBUG:   - Subcube at (" 
                          << scPos.x << "," << scPos.y << "," << scPos.z << ")");
        }
    }
    
    return false; // Subcube not found in static list
}

// =============================================================================
// Physics Management - Delegated to ChunkPhysicsManager
// =============================================================================

void Chunk::setPhysicsWorld(Physics::PhysicsWorld* world) {
    physicsManager.setPhysicsWorld(world);
}

btRigidBody* Chunk::getChunkPhysicsBody() const {
    return physicsManager.getChunkPhysicsBody();
}

void Chunk::validateCollisionSystem() const {
    physicsManager.validateCollisionSystem();
}

void Chunk::debugLogSpatialGrid() const {
    physicsManager.debugLogSpatialGrid();
}

size_t Chunk::getCollisionEntityCount() const {
    return physicsManager.getCollisionEntityCount();
}

size_t Chunk::getCubeEntityCount() const {
    return physicsManager.getCubeEntityCount();
}

size_t Chunk::getSubcubeEntityCount() const {
    return physicsManager.getSubcubeEntityCount();
}

void Chunk::debugPrintSpatialGridStats() const {
    physicsManager.debugPrintSpatialGridStats();
}

// NOTE: Physics body creation and collision methods still directly access physics members
// These will be fully extracted in Phase 2 completion:
// - Move createChunkPhysicsBody implementation to ChunkPhysicsManager
// - Move updateChunkPhysicsBody implementation to ChunkPhysicsManager  
// - Move addCollisionEntity/removeCollisionEntities implementations
// - Move buildInitialCollisionShapes implementation
// - Move updateNeighborCollisionShapes implementation
// - Remove compatibility macros once all methods are extracted

void Chunk::createChunkPhysicsBody() {
    if (!physicsWorld) {
        LOG_DEBUG("Chunk", "[CHUNK] No physics world available for chunk physics body creation");
        return;
    }

    if (chunkPhysicsBody) {
        LOG_DEBUG("Chunk", "[CHUNK] Chunk physics body already exists, skipping creation");
        return;
    }

    LOG_DEBUG_FMT("Chunk", "[CHUNK] Creating optimized compound collision shape for chunk at (" 
              << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z << ")");

    // Create compound shape with dynamic AABB tree for efficient culling
    btCompoundShape* chunkCompound = new btCompoundShape(true);
    chunkCollisionShape = chunkCompound;
    
    // Clear existing collision tracking (in case of rebuild) - shapes auto-delete with shared_ptr
    collisionGrid.clear();
    
    // IMPROVED: Build collision shapes with reference-counted tracking
    buildInitialCollisionShapes();
    
    // Create static rigid body
    btTransform bodyTransform;
    bodyTransform.setIdentity();
    btDefaultMotionState* motionState = new btDefaultMotionState(bodyTransform);
    btVector3 inertia(0, 0, 0);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motionState, chunkCompound, inertia);
    chunkPhysicsBody = new btRigidBody(rbInfo);
    
    physicsWorld->getWorld()->addRigidBody(chunkPhysicsBody);
    
    LOG_DEBUG_FMT("Chunk", "[CHUNK] Spatial collision system initialized with " << collisionGrid.getTotalEntityCount() 
              << " tracked entities (" << collisionGrid.getCubeEntityCount() << " cubes, " 
              << collisionGrid.getSubcubeEntityCount() << " subcubes)");
}

void Chunk::updateChunkPhysicsBody() {
    if (!physicsWorld || !chunkPhysicsBody) return;
    
    // IMPROVED: Batch any remaining collision updates efficiently
    batchUpdateCollisions();
    
    // CRITICAL: Force physics world to recognize collision shape changes
    if (chunkPhysicsBody) {
        // Activate the physics body to ensure collision detection updates
        chunkPhysicsBody->activate(true);
        
        // IMPORTANT: Recalculate AABB for the compound shape after collision changes
        // This ensures the physics world immediately recognizes the new collision geometry
        btCompoundShape* compound = static_cast<btCompoundShape*>(chunkPhysicsBody->getCollisionShape());
        if (compound) {
            compound->recalculateLocalAabb();
        }
        
        // Force the physics world to update the broadphase AABB for this body
        // This prevents the brief moment where old collision data might be cached
        auto* dynamicsWorld = physicsWorld->getWorld();
        if (dynamicsWorld) {
            dynamicsWorld->updateSingleAabb(chunkPhysicsBody);
        }
        
        LOG_DEBUG_FMT("Chunk", "[INCREMENTAL] Spatial collision updates complete with physics sync - maintaining " 
                  << collisionGrid.getTotalEntityCount() << " entities (" << collisionGrid.getCubeEntityCount() 
                  << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes)");
    } else {
        LOG_DEBUG("Chunk", "[INCREMENTAL] No collision updates needed");
    }
}

void Chunk::forcePhysicsRebuild() {
    if (!physicsWorld || !chunkPhysicsBody) return;
    
    LOG_DEBUG("Chunk", "[COMPOUND SHAPE] Force rebuilding compound shape to remove static collision");
    
    // Remove existing body from world
    physicsWorld->getWorld()->removeRigidBody(chunkPhysicsBody);
    
    // Clean up existing resources
    if (chunkCollisionShape) {
        delete chunkCollisionShape;
        chunkCollisionShape = nullptr;
    }
    chunkPhysicsBody = nullptr;
    
    // Recreate with updated geometry (this will exclude the broken subcube)
    createChunkPhysicsBody();
    
    LOG_DEBUG("Chunk", "[COMPOUND SHAPE] Compound shape rebuilt - static collision removed");
}

void Chunk::cleanupPhysicsResources() {
    // Clean up spatial collision grid - entities auto-delete when shared_ptrs are destroyed
    LOG_DEBUG_FMT("Chunk", "[COLLISION CLEANUP] Before cleanup: " << collisionGrid.getTotalEntityCount() << " total entities ("
              << collisionGrid.getCubeEntityCount() << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes)");
    
    // Clear spatial grid - O(1) operation that releases all entity references
    collisionGrid.clear();
    collisionNeedsUpdate = false;
    
    LOG_DEBUG_FMT("Chunk", "[COLLISION CLEANUP] After cleanup: " << collisionGrid.getTotalEntityCount() << " total entities ("
              << collisionGrid.getCubeEntityCount() << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes)");
    
    if (chunkPhysicsBody) {
        LOG_DEBUG("Chunk", "[CHUNK] Cleaning up chunk physics body");
        chunkPhysicsBody = nullptr;
    }
    
    if (chunkCollisionShape) {
        LOG_DEBUG("Chunk", "[CHUNK] Cleaning up chunk collision shape");
        chunkCollisionShape = nullptr;
    }
}

// ============================================================================
// COLLISION SHAPE CREATION HELPERS - Delegated to ChunkPhysicsManager
// ============================================================================
// These wrappers provide cube access to the physics manager

void Chunk::createCubeCollisionShape(const glm::ivec3& localPos, btCompoundShape* compound) {
    // Delegate to physicsManager with cube access lambda
    auto getCube = [this](const glm::ivec3& pos) -> Cube* {
        return this->getCubeAt(pos);
    };
    physicsManager.createCubeCollisionShape(localPos, compound, getCube);
}

void Chunk::createSubcubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, btCompoundShape* compound) {
    // Delegate to physicsManager with subcube access lambda
    auto getSubcube = [this](const glm::ivec3& cPos, const glm::ivec3& sPos) -> Subcube* {
        return this->getSubcubeAt(cPos, sPos);
    };
    physicsManager.createSubcubeCollisionShape(cubePos, subcubePos, compound, getSubcube);
}

void Chunk::createMicrocubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, 
                                         const Microcube* microcube, btCompoundShape* compound) {
    // Delegate to physicsManager
    physicsManager.createMicrocubeCollisionShape(cubePos, subcubePos, microcube, compound);
}

// ============================================================================
// COLLISION ENTITY MANAGEMENT
// ============================================================================

// IMPROVED collision system - memory-safe reference-counted shapes with individual subcube tracking
// This method replaces the old system that used nullptr placeholders and geometric distance heuristics
// Now provides proper individual tracking for each collision shape with automatic memory management
void Chunk::addCollisionEntity(const glm::ivec3& localPos) {
    // Delegate to physics manager with callbacks for accessing chunk data
    physicsManager.addCollisionEntity(
        localPos,
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); },
        [this](const glm::ivec3& pos, const glm::ivec3& subPos) { return getMicrocubesAt(pos, subPos); },
        [this](const glm::ivec3& pos) { return getStaticSubcubesAt(pos); }
    );
}

void Chunk::removeCollisionEntities(const glm::ivec3& localPos) {
    // Delegate to physics manager
    physicsManager.removeCollisionEntities(localPos);
}

void Chunk::batchUpdateCollisions() {
    if (!collisionNeedsUpdate) return;
    
    // Only rebuild if we don't have any collision shapes yet
    if (collisionGrid.getTotalEntityCount() == 0 && chunkCollisionShape) {
        buildInitialCollisionShapes();
    }
    
    collisionNeedsUpdate = false;
}

// Helper method to check if a cube has exposed faces (for collision optimization)
bool Chunk::hasExposedFaces(const glm::ivec3& localPos) const {
    // Delegate to physics manager with callback for accessing cubes
    return physicsManager.hasExposedFaces(
        localPos,
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); }
    );
}

void Chunk::buildInitialCollisionShapes() {
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    if (!compound) return;
    
    // Clear existing spatial grid - shapes auto-delete when entities are destroyed
    collisionGrid.clear();
    
    // Remove all existing children from compound shape
    while (compound->getNumChildShapes() > 0) {
        compound->removeChildShapeByIndex(0);
    }
    
    // Reserve space for expected entities
    size_t expectedEntities = cubes.size() + staticSubcubes.size();
    collisionGrid.reserve(expectedEntities);
    
    // Build collision shapes for visible cubes that have exposed faces
    for (size_t i = 0; i < cubes.size(); ++i) {
        const Cube* cube = cubes[i];
        
        // Skip deleted cubes (nullptr) or hidden cubes (subdivided)
        if (!cube || !cube->isVisible()) {
            continue;
        }
        
        // Get cube's local position within chunk
        glm::ivec3 localPos = indexToLocal(i);
        
        // Only create collision shape if cube has exposed faces (performance optimization)
        if (hasExposedFaces(localPos)) {
            glm::vec3 cubeCenter = glm::vec3(worldOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
            
            btBoxShape* boxShape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(btVector3(cubeCenter.x, cubeCenter.y, cubeCenter.z));
            
            compound->addChildShape(transform, boxShape);
            
            // Create collision entity with spatial tracking
            auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(boxShape, Physics::CollisionSpatialGrid::CollisionEntity::CUBE, cubeCenter);
            entity->isInCompound = true; // Shape is now owned by Bullet compound
            
            // Add to spatial grid - O(1) operation
            collisionGrid.addEntity(localPos, entity);
        }
    }
    
    // Build collision shapes for static subcubes with individual tracking
    for (const Subcube* subcube : staticSubcubes) {
        // Skip broken or hidden subcubes
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) {
            continue;
        }
        
        // Get subcube properties
        glm::ivec3 parentPos = subcube->getPosition();     // Parent cube's world position
        glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis within parent
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentLocalPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) {
            continue; // Skip subcubes with invalid parent positions
        }
        
        // Calculate subcube center
        glm::vec3 parentCenter = glm::vec3(worldOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        glm::vec3 subcubeOffset = (glm::vec3(localPos) - glm::vec3(1.0f)) * (1.0f/3.0f);
        glm::vec3 subcubeCenter = parentCenter + subcubeOffset;
        
        btBoxShape* boxShape = new btBoxShape(btVector3(1.0f/6.0f, 1.0f/6.0f, 1.0f/6.0f));
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(subcubeCenter.x, subcubeCenter.y, subcubeCenter.z));
        
        compound->addChildShape(transform, boxShape);
        
        // Create collision entity with spatial and hierarchy data
        auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(boxShape, Physics::CollisionSpatialGrid::CollisionEntity::SUBCUBE, subcubeCenter, 1.0f/6.0f);
        entity->isInCompound = true; // Shape is now owned by Bullet compound
        entity->parentChunkPos = parentLocalPos;
        entity->subcubeLocalPos = localPos;
        
        // Add to spatial grid - O(1) operation
        collisionGrid.addEntity(parentLocalPos, entity);
    }
    
    // Build collision shapes for static microcubes with individual tracking
    for (const Microcube* microcube : staticMicrocubes) {
        // Skip broken or hidden microcubes
        if (!microcube || microcube->isBroken() || !microcube->isVisible()) {
            continue;
        }
        
        // Get microcube properties
        glm::ivec3 parentCubePos = microcube->getParentCubePosition();        // Parent cube's world position
        glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();         // 0-2 for subcube within cube
        glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();     // 0-2 for microcube within subcube
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentLocalPos = parentCubePos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) {
            continue; // Skip microcubes with invalid parent positions
        }
        
        // Calculate microcube center with two-level hierarchy (consistent with addCollisionEntity)
        constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;
        constexpr float MICROCUBE_SCALE = 1.0f / 9.0f;
        
        glm::vec3 parentCenter = glm::vec3(worldOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        
        // Convert subcube position from 0-2 range to centered offsets
        glm::vec3 subcubeLocalOffset = glm::vec3(subcubePos) - glm::vec3(1.0f);
        glm::vec3 subcubeOffset = subcubeLocalOffset * SUBCUBE_SCALE;
        
        // Convert microcube position from 0-2 range to centered offsets within subcube
        glm::vec3 microcubeLocalOffset = glm::vec3(microcubePos) - glm::vec3(1.0f);
        glm::vec3 microcubeOffset = microcubeLocalOffset * MICROCUBE_SCALE;
        
        glm::vec3 microcubeCenter = parentCenter + subcubeOffset + microcubeOffset;
        
        // DEBUG: Log initial static microcube collision creation  
        LOG_INFO_FMT("Chunk", "[INITIAL STATIC COLLISION] Microcube at cube (" << parentLocalPos.x << "," << parentLocalPos.y << "," << parentLocalPos.z
                  << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
                  << ") micro (" << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
        LOG_INFO_FMT("Chunk", "[INITIAL STATIC COLLISION] Center: (" << microcubeCenter.x << "," << microcubeCenter.y << "," << microcubeCenter.z << ") Half-extents: " << (1.0f/18.0f));
        
        btBoxShape* boxShape = new btBoxShape(btVector3(1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f));
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(microcubeCenter.x, microcubeCenter.y, microcubeCenter.z));
        
        compound->addChildShape(transform, boxShape);
        
        // Create collision entity with spatial and hierarchy data
        auto entity = std::make_shared<Physics::CollisionSpatialGrid::CollisionEntity>(boxShape, Physics::CollisionSpatialGrid::CollisionEntity::SUBCUBE, microcubeCenter, 1.0f/18.0f);
        entity->isInCompound = true; // Shape is now owned by Bullet compound
        entity->parentChunkPos = parentLocalPos;
        entity->subcubeLocalPos = subcubePos; // Store parent subcube position
        
        // Add to spatial grid - O(1) operation
        collisionGrid.addEntity(parentLocalPos, entity);
    }
    
    LOG_DEBUG_FMT("Chunk", "[COLLISION] Built " << collisionGrid.getTotalEntityCount() << " initial collision entities ("
              << collisionGrid.getCubeEntityCount() << " cubes, " << collisionGrid.getSubcubeEntityCount() << " subcubes, "
              << staticMicrocubes.size() << " microcubes) with spatial grid optimization");
}

void Chunk::updateNeighborCollisionShapes(const glm::ivec3& localPos) {
    // Check all 6 neighboring positions that might now be exposed
    glm::ivec3 neighbors[6] = {
        localPos + glm::ivec3(0, 0, 1),   // front (+Z)
        localPos + glm::ivec3(0, 0, -1),  // back (-Z)
        localPos + glm::ivec3(1, 0, 0),   // right (+X)
        localPos + glm::ivec3(-1, 0, 0),  // left (-X)
        localPos + glm::ivec3(0, 1, 0),   // top (+Y)
        localPos + glm::ivec3(0, -1, 0)   // bottom (-Y)
    };
    
    for (int i = 0; i < 6; ++i) {
        glm::ivec3 neighborPos = neighbors[i];
        
        // Skip neighbors outside chunk bounds
        if (neighborPos.x < 0 || neighborPos.x >= 32 ||
            neighborPos.y < 0 || neighborPos.y >= 32 ||
            neighborPos.z < 0 || neighborPos.z >= 32) {
            continue;
        }
        
        // Check if neighbor cube exists and is visible
        const Cube* neighborCube = getCubeAt(neighborPos);
        if (!neighborCube || !neighborCube->isVisible()) {
            continue; // No cube to update
        }
        
        // Check if this neighbor now has exposed faces (due to the removal)
        bool hadCollisionShape = !collisionGrid.getEntitiesAt(neighborPos).empty();
        bool shouldHaveCollisionShape = hasExposedFaces(neighborPos);
        
        if (!hadCollisionShape && shouldHaveCollisionShape) {
            // Neighbor cube is now exposed - add collision shape
            addCollisionEntity(neighborPos);
            LOG_TRACE_FMT("Chunk", "[NEIGHBOR] Added collision shape for newly exposed cube at (" 
                      << neighborPos.x << "," << neighborPos.y << "," << neighborPos.z << ")");
        } else if (hadCollisionShape && !shouldHaveCollisionShape) {
            // Neighbor cube is no longer exposed (shouldn't happen when removing, but handle it)
            removeCollisionEntities(neighborPos);
            LOG_TRACE_FMT("Chunk", "[NEIGHBOR] Removed collision shape for no longer exposed cube at (" 
                      << neighborPos.x << "," << neighborPos.y << "," << neighborPos.z << ")");
        }
        // If hadCollisionShape && shouldHaveCollisionShape, no change needed
        // If !hadCollisionShape && !shouldHaveCollisionShape, no change needed
    }
}

void Chunk::endBulkOperation() {
    if (!physicsManager.isInBulkOperation()) return;
    
    LOG_DEBUG("Chunk", "[CHUNK] Ending bulk operation - building complete collision system");
    
    // Turn off bulk operation flag
    physicsManager.setInBulkOperation(false);
    
    // Now rebuild the entire collision system properly
    if (chunkCollisionShape) {
        buildInitialCollisionShapes();
    }
}

std::vector<Physics::ChunkPhysicsManager::CollisionBox> Chunk::generateMergedCollisionBoxes() {
    std::vector<Physics::ChunkPhysicsManager::CollisionBox> boxes;
    
    // OPTIMIZED APPROACH: Only create collision shapes for cubes with exposed faces
    // This dramatically reduces collision complexity from ~32K to typically <1K collision boxes
    LOG_DEBUG("Chunk", "[COLLISION] Building collision shapes only for exposed cubes (huge optimization!)");
    
    // =========================================================================
    // PHASE 1: Process regular cubes (only those with exposed faces)
    // =========================================================================
    for (size_t i = 0; i < cubes.size(); ++i) {
        const Cube* cube = cubes[i];
        
        // Skip deleted cubes (nullptr) or hidden cubes (subdivided)
        if (!cube || !cube->isVisible()) {
            continue;
        }
        
        // Get cube's local position within chunk
        glm::ivec3 localPos = indexToLocal(i);
        
        // CRITICAL OPTIMIZATION: Only create collision shape if cube has exposed faces
        bool hasExposedFace = false;
        
        // Check all 6 faces for exposure (same logic as rebuildFaces)
        glm::ivec3 neighbors[6] = {
            localPos + glm::ivec3(0, 0, 1),   // front (+Z)
            localPos + glm::ivec3(0, 0, -1),  // back (-Z)
            localPos + glm::ivec3(1, 0, 0),   // right (+X)
            localPos + glm::ivec3(-1, 0, 0),  // left (-X)
            localPos + glm::ivec3(0, 1, 0),   // top (+Y)
            localPos + glm::ivec3(0, -1, 0)   // bottom (-Y)
        };
        
        for (int faceID = 0; faceID < 6; ++faceID) {
            glm::ivec3 neighborPos = neighbors[faceID];
            
            // Face is exposed if neighbor is outside chunk bounds OR if no visible cube at neighbor position
            if (neighborPos.x < 0 || neighborPos.x >= 32 ||
                neighborPos.y < 0 || neighborPos.y >= 32 ||
                neighborPos.z < 0 || neighborPos.z >= 32) {
                hasExposedFace = true; // Edge of chunk
                break;
            } else {
                const Cube* neighborCube = getCubeAt(neighborPos);
                if (!neighborCube || !neighborCube->isVisible()) {
                    hasExposedFace = true; // No occluding neighbor
                    break;
                }
            }
        }
        
        // Only create collision box for cubes with at least one exposed face
        if (hasExposedFace) {
            glm::vec3 cubeCenter = glm::vec3(worldOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
            glm::vec3 cubeHalfExtents(0.5f);
            boxes.emplace_back(cubeCenter, cubeHalfExtents);
        }
    }
    
    // =========================================================================
    // PHASE 2: Process static subcubes (only those with exposed faces)
    // =========================================================================
    for (const Subcube* subcube : staticSubcubes) {
        // Skip broken or hidden subcubes
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) {
            continue;
        }
        
        // Get subcube properties
        glm::ivec3 parentPos = subcube->getPosition();     // Parent cube's world position
        glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis within parent
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentLocalPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) {
            continue; // Skip subcubes with invalid parent positions
        }
        
        // OPTIMIZATION: For subdivided cubes, we could also check for exposed faces,
        // but since subcubes are typically created when a cube is broken/interacted with,
        // they're more likely to need collision detection. Keep all subcubes for now.
        
        // Calculate subcube world center position
        glm::vec3 parentCenter = glm::vec3(worldOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        glm::vec3 subcubeOffset = (glm::vec3(localPos) - glm::vec3(1.0f)) * (1.0f/3.0f);
        glm::vec3 subcubeCenter = parentCenter + subcubeOffset;
        glm::vec3 subcubeHalfExtents(1.0f/6.0f); // 1/3 cube size -> 1/6 half-extents
        
        boxes.emplace_back(subcubeCenter, subcubeHalfExtents);
    }
    
    LOG_DEBUG_FMT("Chunk", "[COLLISION] MASSIVE OPTIMIZATION: Generated only " << boxes.size() 
              << " collision boxes (was ~" << cubes.size() << " before)");
    LOG_DEBUG_FMT("Chunk", "[COLLISION] Performance improvement: " 
              << (cubes.size() > 0 ? (100.0f * (cubes.size() - boxes.size()) / cubes.size()) : 0.0f) 
              << "% reduction in collision shapes!");
    
    return boxes;
}

// =============================================================================
// Microcube Manipulation Functions (Phase 3 - Placeholders for now)
// =============================================================================

bool Chunk::subdivideSubcubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    // Check if position is valid
    if (!isValidLocalPosition(cubePos)) return false;
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return false;
    
    // Get the subcube at this position
    Subcube* subcube = getSubcubeAt(cubePos, subcubePos);
    if (!subcube) return false;
    
    // Check if already subdivided into microcubes
    auto existingMicrocubes = getMicrocubesAt(cubePos, subcubePos);
    if (!existingMicrocubes.empty()) return false; // Already subdivided
    
    // Create 27 microcubes (3x3x3) with random colors for clear visual distinction
    glm::ivec3 parentWorldPos = worldOrigin + cubePos;
    
    // Use random colors for microcubes
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f); // Brighter colors for visibility
    
    int colorIndex = 0;
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 3; ++z) {
                glm::ivec3 microcubeLocalPos(x, y, z);
                
                // Generate random color for each microcube
                glm::vec3 microcubeColor = glm::vec3(
                    colorDist(gen),
                    colorDist(gen),
                    colorDist(gen)
                );
                
                Microcube* newMicrocube = new Microcube(parentWorldPos, microcubeColor, subcubePos, microcubeLocalPos);
                staticMicrocubes.push_back(newMicrocube);
                
                // Update hash maps for O(1) hover detection
                addMicrocubeToMaps(cubePos, subcubePos, microcubeLocalPos, newMicrocube);
                
                colorIndex++;
            }
        }
    }
    
    // Delete the parent subcube completely
    removeSubcubeFromMaps(cubePos, subcubePos);
    
    // Remove from staticSubcubes vector
    for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
        if (*it == subcube) {
            delete subcube;
            staticSubcubes.erase(it);
            LOG_DEBUG_FMT("Chunk", "Completely removed parent subcube at cube (" 
                      << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                      << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
                      << ") - replaced by 27 microcubes");
            break;
        }
    }
    
    // CRITICAL: Update collision shape to create microcube collision entities
    LOG_DEBUG_FMT("Chunk", "[COLLISION] Creating collision shapes for 27 new microcubes at cube (" 
              << cubePos.x << "," << cubePos.y << "," << cubePos.z << ")");
    removeCollisionEntities(cubePos);  // Remove old subcube collision
    addCollisionEntity(cubePos);       // Add new microcube collision
    
    // Mark for update and as dirty for database persistence
    renderManager.setNeedsUpdate(true);
    setDirty(true);
    
    return true;
}

bool Chunk::addMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, const glm::vec3& color) {
    // Check if cube position is valid
    if (!isValidLocalPosition(parentCubePos)) return false;
    
    // Check if subcube position is valid (0-2 for each axis)
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return false;
    
    // Check if microcube position is valid (0-2 for each axis)
    if (microcubePos.x < 0 || microcubePos.x >= 3 || 
        microcubePos.y < 0 || microcubePos.y >= 3 || 
        microcubePos.z < 0 || microcubePos.z >= 3) return false;
    
    // Check if microcube already exists
    if (getMicrocubeAt(parentCubePos, subcubePos, microcubePos)) return false;
    
    // CRITICAL FIX: Check if this is the first microcube at this subcube position
    // If parent subcube exists, we need to remove it (defensive fix for database corruption)
    auto existingMicrocubes = getMicrocubesAt(parentCubePos, subcubePos);
    if (existingMicrocubes.empty()) {
        // This is the first microcube at this subcube position
        // Check if parent subcube exists and remove it if found
        Subcube* parentSubcube = getSubcubeAt(parentCubePos, subcubePos);
        if (parentSubcube) {
            LOG_WARN_FMT("Chunk", "[DATA INTEGRITY] Found parent subcube when adding first microcube at cube (" 
                      << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                      << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
                      << ") - removing parent (possible database corruption)");
            
            // Remove from maps and vector
            removeSubcubeFromMaps(parentCubePos, subcubePos);
            for (auto it = staticSubcubes.begin(); it != staticSubcubes.end(); ++it) {
                if (*it == parentSubcube) {
                    delete parentSubcube;
                    staticSubcubes.erase(it);
                    break;
                }
            }
        }
    }
    
    // Create new microcube (constructor: parentCubePos, color, subcubeLocalPos, microcubeLocalPos)
    glm::ivec3 parentWorldPos = worldOrigin + parentCubePos;
    Microcube* newMicrocube = new Microcube(parentWorldPos, color, subcubePos, microcubePos);
    staticMicrocubes.push_back(newMicrocube);
    
    // Update hash maps
    addMicrocubeToMaps(parentCubePos, subcubePos, microcubePos, newMicrocube);
    
    // Mark for update and as dirty for database persistence
    renderManager.setNeedsUpdate(true);
    setDirty(true);
    
    return true;
}

bool Chunk::removeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    LOG_INFO_FMT("Chunk", "[REMOVE MICROCUBE] Called for cube (" << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
              << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z
              << ") micro (" << microcubePos.x << "," << microcubePos.y << "," << microcubePos.z << ")");
    
    // Try to find and remove from static microcubes
    for (auto it = staticMicrocubes.begin(); it != staticMicrocubes.end(); ++it) {
        Microcube* microcube = *it;
        if (microcube && 
            microcube->getParentCubePosition() == (worldOrigin + parentCubePos) &&
            microcube->getSubcubeLocalPosition() == subcubePos &&
            microcube->getMicrocubeLocalPosition() == microcubePos) {
            
            LOG_INFO("Chunk", "[REMOVE MICROCUBE] Found microcube to remove");
            
            // Remove from hash maps
            removeMicrocubeFromMaps(parentCubePos, subcubePos, microcubePos);
            
            // Delete and remove from vector
            delete microcube;
            staticMicrocubes.erase(it);
            
            LOG_INFO("Chunk", "[REMOVE MICROCUBE] Checking for remaining microcubes");
            
            // CRITICAL: Update collision shape to reflect microcube removal
            // Check if any microcubes remain at this parent position
            bool hasMicrocubes = false;
            for (int sx = 0; sx < 3; ++sx) {
                for (int sy = 0; sy < 3; ++sy) {
                    for (int sz = 0; sz < 3; ++sz) {
                        glm::ivec3 checkSubcubePos(sx, sy, sz);
                        auto remainingMicros = getMicrocubesAt(parentCubePos, checkSubcubePos);
                        if (!remainingMicros.empty()) {
                            hasMicrocubes = true;
                            break;
                        }
                    }
                    if (hasMicrocubes) break;
                }
                if (hasMicrocubes) break;
            }
            
            if (hasMicrocubes) {
                // Still have microcubes - update collision shape to reflect remaining microcubes
                LOG_INFO_FMT("Chunk", "[COLLISION] Microcubes remain at parent pos (" 
                          << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                          << ") - updating collision shape");
                removeCollisionEntities(parentCubePos);
                addCollisionEntity(parentCubePos);
            } else {
                // No more microcubes - but check if subcubes still exist
                auto remainingSubcubes = getStaticSubcubesAt(parentCubePos);
                if (!remainingSubcubes.empty()) {
                    LOG_INFO_FMT("Chunk", "[VOXEL MAP] No microcubes remain but " << remainingSubcubes.size() 
                              << " subcubes still exist at (" << parentCubePos.x << "," << parentCubePos.y 
                              << "," << parentCubePos.z << ") - keeping SUBDIVIDED state and updating collision");
                    removeCollisionEntities(parentCubePos);
                    addCollisionEntity(parentCubePos);
                    // voxelTypeMap stays as SUBDIVIDED - DO NOT ERASE
                } else {
                    // No microcubes AND no subcubes - completely empty position
                    LOG_INFO_FMT("Chunk", "[COLLISION] No microcubes or subcubes remain at parent pos (" 
                              << parentCubePos.x << "," << parentCubePos.y << "," << parentCubePos.z 
                              << ") - removing collision shape and voxel type entry");
                    removeCollisionEntities(parentCubePos);
                    voxelTypeMap.erase(parentCubePos);
                }
            }
            
            // Mark for update
            renderManager.setNeedsUpdate(true);
            setDirty(true);
            
            return true;
        }
    }
    
    return false; // Microcube not found
}

bool Chunk::clearMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    auto microcubes = getMicrocubesAt(cubePos, subcubePos);
    if (microcubes.empty()) return false; // No microcubes to clear
    
    LOG_INFO_FMT("Chunk", "[CLEAR MICROCUBES] Removing all microcubes at cube (" 
              << cubePos.x << "," << cubePos.y << "," << cubePos.z 
              << ") subcube (" << subcubePos.x << "," << subcubePos.y << "," << subcubePos.z 
              << ") - leaving empty space (no parent restoration)");
    
    // Remove all microcubes at this subcube position
    for (auto it = staticMicrocubes.begin(); it != staticMicrocubes.end(); ) {
        Microcube* microcube = *it;
        if (microcube && 
            microcube->getParentCubePosition() == (worldOrigin + cubePos) &&
            microcube->getSubcubeLocalPosition() == subcubePos) {
            
            // Remove from hash maps
            removeMicrocubeFromMaps(cubePos, subcubePos, microcube->getMicrocubeLocalPosition());
            
            // Delete and remove from vector
            delete microcube;
            it = staticMicrocubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Update collision to reflect removal
    // Check if any microcubes remain at the parent cube position
    bool hasMicrocubes = false;
    for (int sx = 0; sx < 3; ++sx) {
        for (int sy = 0; sy < 3; ++sy) {
            for (int sz = 0; sz < 3; ++sz) {
                glm::ivec3 checkSubcubePos(sx, sy, sz);
                auto remainingMicros = getMicrocubesAt(cubePos, checkSubcubePos);
                if (!remainingMicros.empty()) {
                    hasMicrocubes = true;
                    break;
                }
            }
            if (hasMicrocubes) break;
        }
        if (hasMicrocubes) break;
    }
    
    if (hasMicrocubes) {
        // Still have microcubes at other subcube positions - update collision
        removeCollisionEntities(cubePos);
        addCollisionEntity(cubePos);
    } else {
        // No more microcubes - but check if subcubes still exist
        auto remainingSubcubes = getStaticSubcubesAt(cubePos);
        if (!remainingSubcubes.empty()) {
            LOG_INFO_FMT("Chunk", "[VOXEL MAP] No microcubes remain but " << remainingSubcubes.size() 
                      << " subcubes still exist at (" << cubePos.x << "," << cubePos.y 
                      << "," << cubePos.z << ") - keeping SUBDIVIDED state and updating collision");
            removeCollisionEntities(cubePos);
            addCollisionEntity(cubePos);
            // voxelTypeMap stays as SUBDIVIDED - DO NOT ERASE
        } else {
            // No microcubes AND no subcubes - completely empty position
            LOG_INFO_FMT("Chunk", "[CLEAR MICROCUBES] No microcubes or subcubes remain at (" 
                      << cubePos.x << "," << cubePos.y << "," << cubePos.z 
                      << ") - removing collision and voxel type entry");
            removeCollisionEntities(cubePos);
            voxelTypeMap.erase(cubePos);
        }
    }
    
    // Mark for update
    renderManager.setNeedsUpdate(true);
    setDirty(true);
    
    return true;
}

} // namespace VulkanCube
