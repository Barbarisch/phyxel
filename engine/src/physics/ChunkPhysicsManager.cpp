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

void ChunkPhysicsManager::createChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                                 const StaticSubcubesAccessFunc& getStaticSubcubes,
                                                 const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                                 const IndexToLocalFunc& indexToLocal,
                                                 const CubeAccessFunc& getCube) {
    if (!physicsWorld) {
        LOG_TRACE("ChunkPhysicsManager", "No physics world available");
        return;
    }

    if (chunkPhysicsBody) {
        LOG_TRACE("ChunkPhysicsManager", "Physics body already exists");
        return;
    }

    LOG_TRACE("ChunkPhysicsManager", "Creating compound collision shape");

    // Create compound shape with dynamic AABB tree
    btCompoundShape* chunkCompound = new btCompoundShape(true);
    chunkCollisionShape = chunkCompound;
    
    // Clear existing collision tracking
    collisionGrid.clear();
    
    // Build collision shapes
    buildInitialCollisionShapes(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
    
    // Create static rigid body
    btTransform bodyTransform;
    bodyTransform.setIdentity();
    btDefaultMotionState* motionState = new btDefaultMotionState(bodyTransform);
    btVector3 inertia(0, 0, 0);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motionState, chunkCompound, inertia);
    chunkPhysicsBody = new btRigidBody(rbInfo);
    
    physicsWorld->getWorld()->addRigidBody(chunkPhysicsBody);
    
    LOG_TRACE("ChunkPhysicsManager", "Spatial collision system initialized");
}

void ChunkPhysicsManager::updateChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                                 const StaticSubcubesAccessFunc& getStaticSubcubes,
                                                 const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                                 const IndexToLocalFunc& indexToLocal,
                                                 const CubeAccessFunc& getCube) {
    if (!physicsWorld || !chunkPhysicsBody) return;
    
    // Batch any remaining collision updates
    batchUpdateCollisions(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
    
    // Force physics world to recognize collision shape changes
    if (chunkPhysicsBody) {
        chunkPhysicsBody->activate(true);
        
        btCompoundShape* compound = static_cast<btCompoundShape*>(chunkPhysicsBody->getCollisionShape());
        if (compound) {
            compound->recalculateLocalAabb();
        }
        
        auto* dynamicsWorld = physicsWorld->getWorld();
        if (dynamicsWorld) {
            dynamicsWorld->updateSingleAabb(chunkPhysicsBody);
        }
        
        LOG_TRACE("ChunkPhysicsManager", "Spatial collision updates complete");
    }
}

void ChunkPhysicsManager::forcePhysicsRebuild(const CubesArrayAccessFunc& getCubes,
                                              const StaticSubcubesAccessFunc& getStaticSubcubes,
                                              const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                              const IndexToLocalFunc& indexToLocal,
                                              const CubeAccessFunc& getCube) {
    if (!physicsWorld || !chunkPhysicsBody) return;
    
    LOG_TRACE("ChunkPhysicsManager", "Force rebuilding compound shape");
    
    // Remove existing body from world
    physicsWorld->getWorld()->removeRigidBody(chunkPhysicsBody);
    
    // Clean up existing resources
    if (chunkCollisionShape) {
        delete chunkCollisionShape;
        chunkCollisionShape = nullptr;
    }
    chunkPhysicsBody = nullptr;
    
    // Recreate with updated geometry
    createChunkPhysicsBody(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
    
    LOG_TRACE("ChunkPhysicsManager", "Compound shape rebuilt");
}

void ChunkPhysicsManager::addCollisionEntity(const glm::ivec3& localPos) {
    LOG_TRACE("ChunkPhysicsManager", "addCollisionEntity - placeholder");
}

void ChunkPhysicsManager::addCollisionEntity(const glm::ivec3& localPos,
                                             const CubeAccessFunc& getCube,
                                             const MicrocubesAccessFunc& getMicrocubes,
                                             const StaticSubcubesAccessFunc& getStaticSubcubes) {
    if (!chunkCollisionShape) return;
    
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    
    // Remove any existing collision entities at this position first
    removeCollisionEntities(localPos);
    
    // Check for regular cube first
    const Cube* cube = getCube(localPos);
    if (cube && cube->isVisible() && hasExposedFaces(localPos, getCube)) {
        // Delegate to focused helper function
        createCubeCollisionShape(localPos, compound, getCube);
        
        LOG_TRACE("ChunkPhysicsManager", "Added cube collision entity");
        
    } else {
        // Handle mixed state: process each subcube position individually
        // Some may be subdivided (microcubes), others may be regular subcubes
        
        for (int sx = 0; sx < 3; ++sx) {
            for (int sy = 0; sy < 3; ++sy) {
                for (int sz = 0; sz < 3; ++sz) {
                    glm::ivec3 subcubePos(sx, sy, sz);
                    
                    // Check if this subcube position has microcubes
                    auto microcubes = getMicrocubes(localPos, subcubePos);
                    
                    if (!microcubes.empty()) {
                        // This subcube position is subdivided - create microcube collision shapes
                        LOG_TRACE("ChunkPhysicsManager", "Creating microcube collision entities");
                        
                        // Delegate to helper function for each microcube
                        for (const Microcube* microcube : microcubes) {
                            createMicrocubeCollisionShape(localPos, subcubePos, microcube, compound);
                        }
                    } else {
                        // No microcubes at this position - check for regular subcube
                        auto subcubes = getStaticSubcubes(localPos);
                        
                        // Find the subcube at this specific subcube position
                        for (const Subcube* subcube : subcubes) {
                            if (subcube->getLocalPosition() == subcubePos) {
                                LOG_TRACE("ChunkPhysicsManager", "Creating subcube collision entity");
                                
                                // Delegate to focused helper function - need subcube access lambda
                                auto getSubcube = [&subcubes](const glm::ivec3&, const glm::ivec3& sPos) -> Subcube* {
                                    for (Subcube* s : subcubes) {
                                        if (s->getLocalPosition() == sPos) return s;
                                    }
                                    return nullptr;
                                };
                                createSubcubeCollisionShape(localPos, subcubePos, compound, getSubcube);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

void ChunkPhysicsManager::removeCollisionEntities(const glm::ivec3& localPos) {
    if (!chunkCollisionShape) return;
    
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    
    // Get all entities at this position - O(1) operation
    auto& entities = collisionGrid.getEntitiesAt(localPos);
    if (entities.empty()) return;
    
    LOG_TRACE("ChunkPhysicsManager", "Removing collision entities");
    
    // Remove each entity from the compound shape
    for (auto& entity : entities) {
        btCollisionShape* shapeToRemove = entity->shape;
        bool shapeRemoved = false;
        
        // Remove from compound shape
        for (int i = compound->getNumChildShapes() - 1; i >= 0; i--) {
            if (compound->getChildShape(i) == shapeToRemove) {
                compound->removeChildShapeByIndex(i);
                shapeRemoved = true;
                
                if (entity->isCube()) {
                    LOG_TRACE("ChunkPhysicsManager", "Removed cube entity");
                } else {
                    LOG_TRACE("ChunkPhysicsManager", "Removed subcube entity");
                }
                break;
            }
        }
        
        if (shapeRemoved) {
            // Mark shape as no longer in compound so it can be safely deleted
            entity->isInCompound = false;
            // Manually delete the shape now since we removed it from compound
            delete entity->shape;
            entity->shape = nullptr;
        }
    }
    
    // Remove all entities from spatial grid - O(1) operation
    collisionGrid.removeAllAt(localPos);
    
    // CRITICAL: Immediately update collision geometry after removal
    if (chunkPhysicsBody) {
        // Recalculate the compound shape's AABB after child removal
        compound->recalculateLocalAabb();
        
        // Force immediate physics world synchronization
        if (physicsWorld && physicsWorld->getWorld()) {
            chunkPhysicsBody->activate(true);
            physicsWorld->getWorld()->updateSingleAabb(chunkPhysicsBody);
        }
    }
    
    // Mark collision as needing update
    collisionNeedsUpdate = true;
}

bool ChunkPhysicsManager::hasExposedFaces(const glm::ivec3& localPos, const CubeAccessFunc& getCube) const {
    // Same logic as in generateMergedCollisionBoxes()
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
            return true; // Edge of chunk - exposed
        } else {
            const Cube* neighborCube = getCube(neighborPos);
            if (!neighborCube || !neighborCube->isVisible()) {
                return true; // No occluding neighbor - exposed
            }
        }
    }
    
    return false; // All faces are occluded
}

void ChunkPhysicsManager::batchUpdateCollisions(const CubesArrayAccessFunc& getCubes,
                                                const StaticSubcubesAccessFunc& getStaticSubcubes,
                                                const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                                const IndexToLocalFunc& indexToLocal,
                                                const CubeAccessFunc& getCube) {
    if (!collisionNeedsUpdate) return;
    
    // Only rebuild if we don't have any collision shapes yet
    if (collisionGrid.getTotalEntityCount() == 0 && chunkCollisionShape) {
        buildInitialCollisionShapes(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
    }
    
    collisionNeedsUpdate = false;
}

void ChunkPhysicsManager::buildInitialCollisionShapes(const CubesArrayAccessFunc& getCubes,
                                                      const StaticSubcubesAccessFunc& getStaticSubcubes,
                                                      const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                                      const IndexToLocalFunc& indexToLocal,
                                                      const CubeAccessFunc& getCube) {
    btCompoundShape* compound = static_cast<btCompoundShape*>(chunkCollisionShape);
    if (!compound) return;
    
    // Clear existing spatial grid - shapes auto-delete when entities are destroyed
    collisionGrid.clear();
    
    // Remove all existing children from compound shape
    while (compound->getNumChildShapes() > 0) {
        compound->removeChildShapeByIndex(0);
    }
    
    const auto& cubes = getCubes();
    auto staticSubcubes = getStaticSubcubes(glm::ivec3(0)); // Get all static subcubes
    
    // Reserve space for expected entities
    size_t expectedEntities = cubes.size() + staticSubcubes.size();
    collisionGrid.reserve(expectedEntities);
    
    // Build collision shapes for visible cubes that have exposed faces
    for (size_t i = 0; i < cubes.size(); ++i) {
        const Cube* cube = cubes[i].get();
        
        // Skip deleted cubes (nullptr) or hidden cubes (subdivided)
        if (!cube || !cube->isVisible()) {
            continue;
        }
        
        // Get cube's local position within chunk
        glm::ivec3 localPos = indexToLocal(i);
        
        // Only create collision shape if cube has exposed faces (performance optimization)
        if (hasExposedFaces(localPos, getCube)) {
            glm::vec3 cubeCenter = glm::vec3(chunkOrigin) + glm::vec3(localPos) + glm::vec3(0.5f);
            
            btBoxShape* boxShape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
            boxShape->setMargin(0.01f); // Tighter margin for better collision accuracy
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(btVector3(cubeCenter.x, cubeCenter.y, cubeCenter.z));
            
            compound->addChildShape(transform, boxShape);
            
            // Create collision entity with spatial tracking
            auto entity = std::make_shared<CollisionSpatialGrid::CollisionEntity>(boxShape, CollisionSpatialGrid::CollisionEntity::CUBE, cubeCenter);
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
        glm::ivec3 parentLocalPos = parentPos - chunkOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) {
            continue; // Skip subcubes with invalid parent positions
        }
        
        // Calculate subcube center
        glm::vec3 parentCenter = glm::vec3(chunkOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        glm::vec3 subcubeOffset = (glm::vec3(localPos) - glm::vec3(1.0f)) * (1.0f/3.0f);
        glm::vec3 subcubeCenter = parentCenter + subcubeOffset;
        
        btBoxShape* boxShape = new btBoxShape(btVector3(1.0f/6.0f, 1.0f/6.0f, 1.0f/6.0f));
        boxShape->setMargin(0.005f); // Tighter margin for subcubes
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(subcubeCenter.x, subcubeCenter.y, subcubeCenter.z));
        
        compound->addChildShape(transform, boxShape);
        
        // Create collision entity with spatial and hierarchy data
        auto entity = std::make_shared<CollisionSpatialGrid::CollisionEntity>(boxShape, CollisionSpatialGrid::CollisionEntity::SUBCUBE, subcubeCenter, 1.0f/6.0f);
        entity->isInCompound = true; // Shape is now owned by Bullet compound
        entity->parentChunkPos = parentLocalPos;
        entity->subcubeLocalPos = localPos;
        
        // Add to spatial grid - O(1) operation
        collisionGrid.addEntity(parentLocalPos, entity);
    }
    
    // Build collision shapes for static microcubes with individual tracking
    const auto& staticMicrocubes = getStaticMicrocubes();
    for (const auto& microcube : staticMicrocubes) {
        // Skip broken or hidden microcubes
        if (!microcube || microcube->isBroken() || !microcube->isVisible()) {
            continue;
        }
        
        // Get microcube properties
        glm::ivec3 parentCubePos = microcube->getParentCubePosition();
        glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();
        glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition();
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentLocalPos = parentCubePos - chunkOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentLocalPos.x < 0 || parentLocalPos.x >= 32 ||
            parentLocalPos.y < 0 || parentLocalPos.y >= 32 ||
            parentLocalPos.z < 0 || parentLocalPos.z >= 32) {
            continue;
        }
        
        // Calculate microcube center with two-level hierarchy
        constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;
        constexpr float MICROCUBE_SCALE = 1.0f / 9.0f;
        
        glm::vec3 parentCenter = glm::vec3(chunkOrigin) + glm::vec3(parentLocalPos) + glm::vec3(0.5f);
        glm::vec3 subcubeLocalOffset = glm::vec3(subcubePos) - glm::vec3(1.0f);
        glm::vec3 subcubeOffset = subcubeLocalOffset * SUBCUBE_SCALE;
        glm::vec3 microcubeLocalOffset = glm::vec3(microcubePos) - glm::vec3(1.0f);
        glm::vec3 microcubeOffset = microcubeLocalOffset * MICROCUBE_SCALE;
        glm::vec3 microcubeCenter = parentCenter + subcubeOffset + microcubeOffset;
        
        btBoxShape* boxShape = new btBoxShape(btVector3(1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f));
        boxShape->setMargin(0.002f); // Very tight margin for microcubes
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(microcubeCenter.x, microcubeCenter.y, microcubeCenter.z));
        
        compound->addChildShape(transform, boxShape);
        
        // Create collision entity with spatial and hierarchy data
        auto entity = std::make_shared<CollisionSpatialGrid::CollisionEntity>(boxShape, CollisionSpatialGrid::CollisionEntity::SUBCUBE, microcubeCenter, 1.0f/18.0f);
        entity->isInCompound = true;
        entity->parentChunkPos = parentLocalPos;
        entity->subcubeLocalPos = subcubePos;
        
        // Add to spatial grid - O(1) operation
        collisionGrid.addEntity(parentLocalPos, entity);
    }
    
    LOG_INFO_FMT("ChunkPhysicsManager", "Built initial collision shapes for chunk at (" 
              << chunkOrigin.x << "," << chunkOrigin.y << "," << chunkOrigin.z 
              << "): " << compound->getNumChildShapes() << " shapes");
}

void ChunkPhysicsManager::updateNeighborCollisionShapes(const glm::ivec3& localPos,
                                                        const CubeAccessFunc& getCube,
                                                        const MicrocubesAccessFunc& getMicrocubes,
                                                        const StaticSubcubesAccessFunc& getStaticSubcubes) {
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
        const Cube* neighborCube = getCube(neighborPos);
        if (!neighborCube || !neighborCube->isVisible()) {
            continue;
        }
        
        // Check if this neighbor now has exposed faces
        bool hadCollisionShape = !collisionGrid.getEntitiesAt(neighborPos).empty();
        bool shouldHaveCollisionShape = hasExposedFaces(neighborPos, getCube);
        
        if (!hadCollisionShape && shouldHaveCollisionShape) {
            // Neighbor cube is now exposed - add collision shape
            addCollisionEntity(neighborPos, getCube, getMicrocubes, getStaticSubcubes);
            LOG_TRACE("ChunkPhysicsManager", "Added collision for newly exposed neighbor");
        } else if (hadCollisionShape && !shouldHaveCollisionShape) {
            // Neighbor cube is no longer exposed
            removeCollisionEntities(neighborPos);
            LOG_TRACE("ChunkPhysicsManager", "Removed collision for no longer exposed neighbor");
        }
    }
}

void ChunkPhysicsManager::endBulkOperation(const CubesArrayAccessFunc& getCubes,
                                           const StaticSubcubesAccessFunc& getStaticSubcubes,
                                           const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                           const IndexToLocalFunc& indexToLocal,
                                           const CubeAccessFunc& getCube) {
    if (!m_isInBulkOperation) return;
    
    LOG_TRACE("ChunkPhysicsManager", "Ending bulk operation");
    
    // Turn off bulk operation flag
    m_isInBulkOperation = false;
    
    // Rebuild the entire collision system
    if (chunkCollisionShape) {
        buildInitialCollisionShapes(getCubes, getStaticSubcubes, getStaticMicrocubes, indexToLocal, getCube);
    }
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
    boxShape->setMargin(0.01f);
    
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
    subcubeShape->setMargin(0.005f);
    
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

std::vector<ChunkPhysicsManager::CollisionBox> 
ChunkPhysicsManager::generateMergedCollisionBoxes(const CubeAccessFunc& getCube) {
    LOG_TRACE("ChunkPhysicsManager", "generateMergedCollisionBoxes - placeholder");
    return {};
}

} // namespace Physics
} // namespace VulkanCube
