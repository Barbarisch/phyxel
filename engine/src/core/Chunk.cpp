#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include "physics/CollisionSpatialGrid.h"
#include "utils/Logger.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <random>
#include <iomanip>
#include <unordered_set>

// Bullet Physics includes
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>

namespace Phyxel {

Chunk::Chunk(const glm::ivec3& origin) 
    : worldOrigin(origin) {
    cubes.reserve(32 * 32 * 32);              // Reserve space for all possible cubes
    staticSubcubes.reserve(1000);             // Reserve reasonable space for static subcubes
}

Chunk::~Chunk() {
    // unique_ptr vectors auto-delete all owned voxels
    cubes.clear();
    staticSubcubes.clear();
    staticMicrocubes.clear();
    
    cleanupVulkanResources();
    cleanupPhysicsResources();
}

Chunk::Chunk(Chunk&& other) noexcept
    : cubes(std::move(other.cubes))
    , staticSubcubes(std::move(other.staticSubcubes))
    , staticMicrocubes(std::move(other.staticMicrocubes))
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
        staticMicrocubes = std::move(other.staticMicrocubes);
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
    
    // Initialize voxelBreaker with callbacks
    voxelBreaker.setCallbacks(
        [this]() -> std::vector<std::unique_ptr<Subcube>>& { return staticSubcubes; },
        [this](const glm::ivec3& parent, const glm::ivec3& sub) { return removeSubcube(parent, sub); },
        [this]() { rebuildFaces(); },
        [this]() { batchUpdateCollisions(); },
        [this](const glm::ivec3& p, const glm::ivec3& s) { return getMicrocubesAt(p, s); },
        [this](const glm::ivec3& p) { return getSubcubesAt(p); },
        [this](bool v) { renderManager.setNeedsUpdate(v); },
        [this]() -> const glm::ivec3& { return worldOrigin; }
    );

    // Initialize voxelManager with callbacks (stored once, not per-call)
    voxelManager.setCallbacks(
        [this]() -> std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this]() -> std::vector<std::unique_ptr<Subcube>>& { return staticSubcubes; },
        [this]() -> std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this]() -> const glm::ivec3& { return worldOrigin; },
        [this](bool value) { setDirty(value); },
        [this](bool value) { renderManager.setNeedsUpdate(value); },
        [this]() { rebuildFaces(); },
        [this](const glm::ivec3& pos) { addCollisionEntity(pos); },
        [this](const glm::ivec3& pos) { removeCollisionEntities(pos); },
        [this](const glm::ivec3& pos) { updateNeighborCollisionShapes(pos); },
        [this]() { return physicsManager.isInBulkOperation(); },
        [this]() { updateVulkanBuffer(); }
    );
}

Cube* Chunk::getCubeAt(const glm::ivec3& localPos) {
    if (!isValidLocalPosition(localPos)) return nullptr;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return nullptr;
    
    // Return the pointer (which could be nullptr for deleted cubes)
    return cubes[index].get();
}

const Cube* Chunk::getCubeAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return nullptr;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return nullptr;
    
    // Return the pointer (which could be nullptr for deleted cubes)
    return cubes[index].get();
}

Cube* Chunk::getCubeAtIndex(size_t index) {
    if (index >= cubes.size()) return nullptr;
    return cubes[index].get();
}

const Cube* Chunk::getCubeAtIndex(size_t index) const {
    if (index >= cubes.size()) return nullptr;
    return cubes[index].get();
}

bool Chunk::removeCube(const glm::ivec3& localPos) {
    return voxelManager.removeCube(localPos);
}

bool Chunk::addCube(const glm::ivec3& localPos) {
    return voxelManager.addCube(localPos);
}

bool Chunk::addCube(const glm::ivec3& localPos, const std::string& material) {
    return voxelManager.addCube(localPos, material);
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
                auto cube = std::make_unique<Cube>();
                cube->setPosition(glm::ivec3(x, y, z));  // Local position within chunk (for 5-bit packing efficiency)
                cubes.push_back(std::move(cube));
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
    cubes.resize(32 * 32 * 32);  // unique_ptr default-constructs to nullptr
    
    // Clear any existing subcubes (unique_ptr auto-deletes)
    staticSubcubes.clear();
    
    // Clear any existing microcubes (unique_ptr auto-deletes)
    staticMicrocubes.clear();
    
    // Set up minimal no-op callbacks if initialize() hasn't been called
    // (e.g. in unit tests where Vulkan/physics aren't available)
    if (!voxelManager.hasCallbacks()) {
        voxelManager.setCallbacks(
            [this]() -> std::vector<std::unique_ptr<Cube>>& { return cubes; },
            [this]() -> std::vector<std::unique_ptr<Subcube>>& { return staticSubcubes; },
            [this]() -> std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
            [this]() -> const glm::ivec3& { return worldOrigin; },
            [this](bool value) { setDirty(value); },
            [](bool) {},              // setNeedsUpdate - no-op without renderer
            []() {},                  // rebuildFaces - no-op without renderer
            [](const glm::ivec3&) {}, // addCollision - no-op without physics
            [](const glm::ivec3&) {}, // removeCollision - no-op without physics
            [](const glm::ivec3&) {}, // updateNeighborCollisions - no-op without physics
            [this]() { return physicsManager.isInBulkOperation(); },
            []() {}                   // updateVulkanBuffer - no-op without renderer
        );
    }
    
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
    for (const auto& subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube.get();
        }
    }
    return nullptr;
}

const Subcube* Chunk::getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    // Search in static subcubes only
    for (const auto& subcube : staticSubcubes) {
        if (subcube && 
            subcube->getPosition() == worldOrigin + localPos && 
            subcube->getLocalPosition() == subcubePos) {
            return subcube.get();
        }
    }
    return nullptr;
}

std::vector<Subcube*> Chunk::getSubcubesAt(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    // Collect from static subcubes only
    for (const auto& subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube.get());
        }
    }
    return result;
}

std::vector<Subcube*> Chunk::getStaticSubcubesAt(const glm::ivec3& localPos) {
    std::vector<Subcube*> result;
    glm::ivec3 parentWorldPos = worldOrigin + localPos;
    
    for (const auto& subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == parentWorldPos) {
            result.push_back(subcube.get());
        }
    }
    return result;
}

// =============================================================================
// Microcube Access Functions
// =============================================================================

Microcube* Chunk::getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    return const_cast<Microcube*>(voxelManager.getMicrocubeHelper(cubePos, subcubePos, microcubePos));
}

const Microcube* Chunk::getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) const {
    return voxelManager.getMicrocubeHelper(cubePos, subcubePos, microcubePos);
}

std::vector<Microcube*> Chunk::getMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    return voxelManager.getMicrocubesHelper(cubePos, subcubePos);
}

// =============================================================================
// NEW: O(1) VoxelLocation resolution system for optimized hover detection
// =============================================================================

VoxelLocation Chunk::resolveLocalPosition(const glm::ivec3& localPos) const {
    // Quick bounds check
    if (!isValidLocalPosition(localPos)) {
        return VoxelLocation();
    }
    
    // Delegate to voxel manager
    VoxelLocation location = voxelManager.resolveLocalPosition(localPos);
    
    // Fill in chunk-specific fields
    if (location.type != VoxelLocation::EMPTY) {
        location.chunk = const_cast<Chunk*>(this);
        location.localPos = localPos;
        location.worldPos = worldOrigin + localPos;
        // std::cout << "[Chunk::resolveLocalPosition] localPos=(" << localPos.x << "," << localPos.y << "," << localPos.z 
        //           << ") worldOrigin=(" << worldOrigin.x << "," << worldOrigin.y << "," << worldOrigin.z
        //           << ") worldPos=(" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")" << std::endl;
        if (location.subcubePos == glm::ivec3(0)) {
            location.subcubePos = glm::ivec3(-1);
        }
    }
    
    return location;
}

bool Chunk::hasVoxelAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return false;
    return voxelManager.hasVoxelAt(localPos);
}

bool Chunk::hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const {
    return voxelManager.hasSubcubeAt(localPos, subcubePos);
}

VoxelLocation::Type Chunk::getVoxelType(const glm::ivec3& localPos) const {
    return voxelManager.getVoxelType(localPos);
}

// O(1) optimized lookups (replace linear searches)
Cube* Chunk::getCubeAtFast(const glm::ivec3& localPos) {
    return voxelManager.getCubeAtFast(localPos);
}

const Cube* Chunk::getCubeAtFast(const glm::ivec3& localPos) const {
    return voxelManager.getCubeAtFast(localPos);
}

// Internal: Maintain hash map consistency
void Chunk::updateVoxelMaps(const glm::ivec3& localPos) {
    voxelManager.updateVoxelMaps(localPos);
}

void Chunk::addToVoxelMaps(const glm::ivec3& localPos, Cube* cube) {
    voxelManager.addToVoxelMaps(localPos, cube);
}

void Chunk::removeFromVoxelMaps(const glm::ivec3& localPos) {
    voxelManager.removeFromVoxelMaps(localPos);
}

void Chunk::addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube) {
    voxelManager.addSubcubeToMaps(localPos, subcubePos, subcube);
}

void Chunk::removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos) {
    voxelManager.removeSubcubeFromMaps(localPos, subcubePos);
}

void Chunk::addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube) {
    voxelManager.addMicrocubeToMaps(cubePos, subcubePos, microcubePos, microcube);
}

void Chunk::removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    voxelManager.removeMicrocubeFromMaps(cubePos, subcubePos, microcubePos);
}

void Chunk::initializeVoxelMaps() {
    voxelManager.initializeVoxelMaps();
}

bool Chunk::subdivideAt(const glm::ivec3& localPos) {
    return voxelManager.subdivideAt(localPos);
}

bool Chunk::addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const std::string& material) {
    return voxelManager.addSubcube(parentPos, subcubePos, material);
}

bool Chunk::removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    return voxelManager.removeSubcube(parentPos, subcubePos);
}

bool Chunk::clearSubdivisionAt(const glm::ivec3& localPos) {
    return voxelManager.clearSubdivisionAt(localPos);
}

size_t Chunk::subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) {
    return ChunkVoxelManager::subcubeToIndex(parentPos, subcubePos);
}

bool Chunk::isValidLocalPosition(const glm::ivec3& localPos) const {
    return localPos.x >= 0 && localPos.x < 32 &&
           localPos.y >= 0 && localPos.y < 32 &&
           localPos.z >= 0 && localPos.z < 32;
}

// =============================================================================
// PHYSICS-RELATED METHODS
// =============================================================================

bool Chunk::breakSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, 
                        Physics::PhysicsWorld* physicsWorld, ChunkManager* chunkManager, const glm::vec3& impulseForce) {
    return voxelBreaker.breakSubcube(parentPos, subcubePos, physicsWorld, chunkManager, impulseForce);
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
    physicsManager.createChunkPhysicsBody(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); }
    );
}

void Chunk::updateChunkPhysicsBody() {
    physicsManager.updateChunkPhysicsBody(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); }
    );
}

void Chunk::forcePhysicsRebuild() {
    physicsManager.forcePhysicsRebuild(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3&) -> std::vector<Subcube*> {
            std::vector<Subcube*> result;
            result.reserve(staticSubcubes.size());
            for (const auto& s : staticSubcubes) { if (s) result.push_back(s.get()); }
            return result;
        },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); }
    );
}

void Chunk::cleanupPhysicsResources() {
    // Clean up spatial collision grid - entities auto-delete when shared_ptrs are destroyed
    auto& grid = physicsManager.getCollisionGrid();
    LOG_DEBUG_FMT("Chunk", "[COLLISION CLEANUP] Before cleanup: " << grid.getTotalEntityCount() << " total entities ("
              << grid.getCubeEntityCount() << " cubes, " << grid.getSubcubeEntityCount() << " subcubes)");
    
    // Clear spatial grid - O(1) operation that releases all entity references
    grid.clear();
    physicsManager.getCollisionNeedsUpdateRef() = false;
    
    LOG_DEBUG_FMT("Chunk", "[COLLISION CLEANUP] After cleanup: " << grid.getTotalEntityCount() << " total entities ("
              << grid.getCubeEntityCount() << " cubes, " << grid.getSubcubeEntityCount() << " subcubes)");
    
    if (physicsManager.getChunkPhysicsBodyRef()) {
        LOG_DEBUG("Chunk", "[CHUNK] Cleaning up chunk physics body");
        physicsManager.getChunkPhysicsBodyRef() = nullptr;
    }
    
    if (physicsManager.getChunkCollisionShapeRef()) {
        LOG_DEBUG("Chunk", "[CHUNK] Cleaning up chunk collision shape");
        physicsManager.getChunkCollisionShapeRef() = nullptr;
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
    physicsManager.batchUpdateCollisions(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3& pos) { return getStaticSubcubesAt(pos); },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); }
    );
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
    physicsManager.buildInitialCollisionShapes(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3& pos) { return getStaticSubcubesAt(pos); },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); }
    );
}

void Chunk::updateNeighborCollisionShapes(const glm::ivec3& localPos) {
    physicsManager.updateNeighborCollisionShapes(
        localPos,
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); },
        [this](const glm::ivec3& pos, const glm::ivec3& subPos) { return getMicrocubesAt(pos, subPos); },
        [this](const glm::ivec3& pos) { return getStaticSubcubesAt(pos); }
    );
}

void Chunk::endBulkOperation() {
    physicsManager.endBulkOperation(
        [this]() -> const std::vector<std::unique_ptr<Cube>>& { return cubes; },
        [this](const glm::ivec3& pos) { return getStaticSubcubesAt(pos); },
        [this]() -> const std::vector<std::unique_ptr<Microcube>>& { return staticMicrocubes; },
        [this](size_t index) { return indexToLocal(index); },
        [this](const glm::ivec3& pos) -> const Cube* { return getCubeAt(pos); }
    );
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
        const Cube* cube = cubes[i].get();
        
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
    for (const auto& subcube : staticSubcubes) {
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
    return voxelManager.subdivideSubcubeAt(cubePos, subcubePos);
}

bool Chunk::addMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, const std::string& material) {
    return voxelManager.addMicrocube(parentCubePos, subcubePos, microcubePos, material);
}

bool Chunk::removeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) {
    return voxelManager.removeMicrocube(parentCubePos, subcubePos, microcubePos);
}

bool Chunk::clearMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos) {
    return voxelManager.clearMicrocubesAt(cubePos, subcubePos);
}

} // namespace Phyxel
