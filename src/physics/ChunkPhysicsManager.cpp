#include "physics/ChunkPhysicsManager.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include <btBulletDynamicsCommon.h>
#include <algorithm>

namespace VulkanCube {
namespace Physics {

ChunkPhysicsManager::ChunkPhysicsManager()
    : chunkPhysicsBody(nullptr)
    , chunkCollisionShape(nullptr)
    , chunkTriangleMesh(nullptr)
    , collisionNeedsUpdate(false)
    , m_isInBulkOperation(false)
    , physicsWorld(nullptr)
    , chunkOrigin(0)
{
}

ChunkPhysicsManager::~ChunkPhysicsManager() {
    cleanupPhysicsResources();
}

ChunkPhysicsManager::ChunkPhysicsManager(ChunkPhysicsManager&& other) noexcept
    : chunkPhysicsBody(other.chunkPhysicsBody)
    , chunkCollisionShape(other.chunkCollisionShape)
    , chunkTriangleMesh(other.chunkTriangleMesh)
    , collisionGrid(std::move(other.collisionGrid))
    , collisionNeedsUpdate(other.collisionNeedsUpdate)
    , m_isInBulkOperation(other.m_isInBulkOperation)
    , physicsWorld(other.physicsWorld)
    , chunkOrigin(other.chunkOrigin)
{
    // Nullify moved-from object
    other.chunkPhysicsBody = nullptr;
    other.chunkCollisionShape = nullptr;
    other.chunkTriangleMesh = nullptr;
    other.physicsWorld = nullptr;
}

ChunkPhysicsManager& ChunkPhysicsManager::operator=(ChunkPhysicsManager&& other) noexcept {
    if (this != &other) {
        // Clean up existing resources
        cleanupPhysicsResources();
        
        // Move data
        chunkPhysicsBody = other.chunkPhysicsBody;
        chunkCollisionShape = other.chunkCollisionShape;
        chunkTriangleMesh = other.chunkTriangleMesh;
        collisionGrid = std::move(other.collisionGrid);
        collisionNeedsUpdate = other.collisionNeedsUpdate;
        m_isInBulkOperation = other.m_isInBulkOperation;
        physicsWorld = other.physicsWorld;
        chunkOrigin = other.chunkOrigin;
        
        // Nullify moved-from object
        other.chunkPhysicsBody = nullptr;
        other.chunkCollisionShape = nullptr;
        other.chunkTriangleMesh = nullptr;
        other.physicsWorld = nullptr;
    }
    return *this;
}

void ChunkPhysicsManager::initialize(PhysicsWorld* world, const glm::ivec3& origin) {
    physicsWorld = world;
    chunkOrigin = origin;
    LOG_TRACE("ChunkPhysicsManager", "Initialized for chunk");
}

void ChunkPhysicsManager::cleanupPhysicsResources() {
    // Remove physics body from world if it exists
    if (chunkPhysicsBody && physicsWorld) {
        physicsWorld->getWorld()->removeRigidBody(chunkPhysicsBody);
    }
    
    // Delete physics body
    if (chunkPhysicsBody) {
        delete chunkPhysicsBody->getMotionState();
        delete chunkPhysicsBody;
        chunkPhysicsBody = nullptr;
    }
    
    // Delete collision shape
    if (chunkCollisionShape) {
        delete chunkCollisionShape;
        chunkCollisionShape = nullptr;
    }
    
    // Delete triangle mesh
    if (chunkTriangleMesh) {
        delete chunkTriangleMesh;
        chunkTriangleMesh = nullptr;
    }
}

// Placeholder implementations - will be filled in as methods are extracted from Chunk.cpp

void ChunkPhysicsManager::createChunkPhysicsBody() {
    LOG_TRACE("ChunkPhysicsManager", "createChunkPhysicsBody - placeholder");
}

void ChunkPhysicsManager::updateChunkPhysicsBody() {
    LOG_TRACE("ChunkPhysicsManager", "updateChunkPhysicsBody - placeholder");
}

void ChunkPhysicsManager::forcePhysicsRebuild() {
    LOG_TRACE("ChunkPhysicsManager", "forcePhysicsRebuild - placeholder");
}

void ChunkPhysicsManager::addCollisionEntity(const glm::ivec3& localPos) {
    LOG_TRACE("ChunkPhysicsManager", "addCollisionEntity - placeholder");
}

void ChunkPhysicsManager::removeCollisionEntities(const glm::ivec3& localPos) {
    LOG_TRACE("ChunkPhysicsManager", "removeCollisionEntities - placeholder");
}

void ChunkPhysicsManager::batchUpdateCollisions() {
    LOG_TRACE("ChunkPhysicsManager", "batchUpdateCollisions - placeholder");
}

void ChunkPhysicsManager::buildInitialCollisionShapes() {
    LOG_TRACE("ChunkPhysicsManager", "buildInitialCollisionShapes - placeholder");
}

void ChunkPhysicsManager::updateNeighborCollisionShapes(const glm::ivec3& localPos) {
    LOG_TRACE("ChunkPhysicsManager", "updateNeighborCollisionShapes - placeholder");
}

void ChunkPhysicsManager::endBulkOperation() {
    LOG_TRACE("ChunkPhysicsManager", "endBulkOperation - placeholder");
}

void ChunkPhysicsManager::validateCollisionSystem() const {
    LOG_TRACE("ChunkPhysicsManager", "validateCollisionSystem - placeholder");
}

void ChunkPhysicsManager::debugLogSpatialGrid() const {
    LOG_TRACE("ChunkPhysicsManager", "debugLogSpatialGrid - placeholder");
}

size_t ChunkPhysicsManager::getCollisionEntityCount() const {
    return collisionGrid.getTotalEntityCount();
}

size_t ChunkPhysicsManager::getCubeEntityCount() const {
    return collisionGrid.getCubeEntityCount();
}

size_t ChunkPhysicsManager::getSubcubeEntityCount() const {
    return collisionGrid.getSubcubeEntityCount();
}

void ChunkPhysicsManager::debugPrintSpatialGridStats() const {
    LOG_TRACE("ChunkPhysicsManager", "debugPrintSpatialGridStats - placeholder");
}

void ChunkPhysicsManager::createCubeCollisionShape(const glm::ivec3& localPos, 
                                                    btCompoundShape* compound,
                                                    const CubeAccessFunc& getCube) {
    // PERFORMANCE: This method is called thousands of times during chunk initialization.
    // Any inefficiency here (e.g., linear search) will cause severe performance degradation.
    
    // Calculate center position in world space
    glm::vec3 shapeCenter = glm::vec3(chunkOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
    
    // Create box shape: full cube is 1.0 units, so half-extents are 0.5
    btBoxShape* boxShape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
    
    // Position the shape in the compound
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(shapeCenter.x, shapeCenter.y, shapeCenter.z));
    compound->addChildShape(transform, boxShape);
    
    // Create collision entity for tracking
    auto entity = std::make_shared<CollisionSpatialGrid::CollisionEntity>(boxShape, CollisionSpatialGrid::CollisionEntity::CUBE, shapeCenter);
    entity->isInCompound = true; // Shape is now owned by Bullet compound
    
    // Add to spatial grid for O(1) lookups
    collisionGrid.addEntity(localPos, entity);
    
    LOG_TRACE("ChunkPhysicsManager", "Created cube collision shape");
}

void ChunkPhysicsManager::createSubcubeCollisionShape(const glm::ivec3& cubePos, 
                                                       const glm::ivec3& subcubePos,
                                                       btCompoundShape* compound,
                                                       const SubcubeAccessFunc& getSubcube) {
    // Calculate subcube center: offset from cube center by subcube position
    // Subcube positions are (0,0,0) to (2,2,2), so we offset by -1 to center them
    constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;
    glm::vec3 subcubeLocalOffset = glm::vec3(subcubePos) - glm::vec3(1.0f);
    glm::vec3 subcubeOffset = subcubeLocalOffset * SUBCUBE_SCALE;
    glm::vec3 subcubeCenter = glm::vec3(chunkOrigin) + glm::vec3(cubePos) + glm::vec3(0.5f) + subcubeOffset;
    
    // Create box shape: subcube is 1/3 cube size, so half-extents are 1/6
    btBoxShape* subcubeShape = new btBoxShape(btVector3(1.0f/6.0f, 1.0f/6.0f, 1.0f/6.0f));
    
    // Position the shape in the compound
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(subcubeCenter.x, subcubeCenter.y, subcubeCenter.z));
    compound->addChildShape(transform, subcubeShape);
    
    // Create collision entity with hierarchy tracking
    auto entity = std::make_shared<CollisionSpatialGrid::CollisionEntity>(subcubeShape, CollisionSpatialGrid::CollisionEntity::SUBCUBE, subcubeCenter, 1.0f/6.0f);
    entity->isInCompound = true;
    entity->parentChunkPos = cubePos;
    entity->subcubeLocalPos = subcubePos;
    
    // Add to spatial grid
    collisionGrid.addEntity(cubePos, entity);
    
    LOG_TRACE("ChunkPhysicsManager", "Created subcube collision shape");
}

void ChunkPhysicsManager::createMicrocubeCollisionShape(const glm::ivec3& cubePos,
                                                         const glm::ivec3& subcubePos,
                                                         const Microcube* microcube,
                                                         btCompoundShape* compound) {
    // Calculate microcube center: two-level hierarchy (cube -> subcube -> microcube)
    constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;
    constexpr float MICROCUBE_SCALE = 1.0f / 9.0f;
    
    // First, offset from cube center to subcube center
    glm::vec3 subcubeLocalOffset = glm::vec3(subcubePos) - glm::vec3(1.0f);
    glm::vec3 subcubeOffset = subcubeLocalOffset * SUBCUBE_SCALE;
    
    // Then, offset from subcube center to microcube center
    glm::vec3 microcubeLocalOffset = glm::vec3(microcube->getMicrocubeLocalPosition()) - glm::vec3(1.0f);
    glm::vec3 microcubeOffset = microcubeLocalOffset * MICROCUBE_SCALE;
    
    glm::vec3 microcubeCenter = glm::vec3(chunkOrigin) + glm::vec3(cubePos) + glm::vec3(0.5f) + subcubeOffset + microcubeOffset;
    
    // Create box shape: microcube is 1/9 cube size, so half-extents are 1/18
    btBoxShape* microcubeShape = new btBoxShape(btVector3(1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f));
    microcubeShape->setMargin(0.002f);
    
    // Position the shape in the compound
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(microcubeCenter.x, microcubeCenter.y, microcubeCenter.z));
    compound->addChildShape(transform, microcubeShape);
    
    // Create collision entity with full hierarchy tracking
    auto entity = std::make_shared<CollisionSpatialGrid::CollisionEntity>(microcubeShape, CollisionSpatialGrid::CollisionEntity::SUBCUBE, microcubeCenter, 1.0f/18.0f);
    entity->isInCompound = true;
    entity->parentChunkPos = cubePos;
    entity->subcubeLocalPos = subcubePos;
    
    // Add to spatial grid
    collisionGrid.addEntity(cubePos, entity);
    
    LOG_TRACE("ChunkPhysicsManager", "Created microcube collision shape");
}

bool ChunkPhysicsManager::hasExposedFaces(const glm::ivec3& localPos, 
                                           const CubeAccessFunc& getCube) const {
    // To be implemented - placeholder returns true
    return true;
}

std::vector<ChunkPhysicsManager::CollisionBox> 
ChunkPhysicsManager::generateMergedCollisionBoxes(const CubeAccessFunc& getCube) {
    LOG_TRACE("ChunkPhysicsManager", "generateMergedCollisionBoxes - placeholder");
    return {};
}

} // namespace Physics
} // namespace VulkanCube
