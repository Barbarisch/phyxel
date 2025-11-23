#pragma once

#include "core/Types.h"
#include "physics/CollisionSpatialGrid.h"
#include <glm/glm.hpp>
#include <functional>

// Bullet Physics forward declarations (global namespace)
class btRigidBody;
class btCollisionShape;
class btCompoundShape;
class btTriangleMesh;

namespace VulkanCube {

// Forward declarations
class Cube;
class Subcube;
class Microcube;

namespace Physics {

// Forward declaration
class PhysicsWorld;

/**
 * ChunkPhysicsManager - Manages physics simulation for a single chunk
 * 
 * Responsibilities:
 * - Create and manage chunk physics body (compound shape for static geometry)
 * - Handle collision shape creation for cubes, subcubes, and microcubes
 * - Manage collision entity tracking via spatial grid
 * - Batch collision updates for performance
 * - Neighbor collision shape updates
 * 
 * CRITICAL PERFORMANCE NOTES:
 * - Cube access MUST use indexed lookups: index = z + y*32 + x*32*32
 * - NEVER use linear search through cube vectors (causes O(n²) performance)
 * - Collision grid provides O(1) spatial lookups
 * - Always use callback functions (CubeAccessFunc, SubcubeAccessFunc) for data access
 * 
 * Isolates Bullet Physics dependencies from main Chunk class.
 */
class ChunkPhysicsManager {
public:
    // Constructor
    ChunkPhysicsManager();
    
    // Destructor - cleans up physics resources
    ~ChunkPhysicsManager();
    
    // Delete copy operations (physics bodies shouldn't be copied)
    ChunkPhysicsManager(const ChunkPhysicsManager&) = delete;
    ChunkPhysicsManager& operator=(const ChunkPhysicsManager&) = delete;
    
    // Move operations
    ChunkPhysicsManager(ChunkPhysicsManager&& other) noexcept;
    ChunkPhysicsManager& operator=(ChunkPhysicsManager&& other) noexcept;
    
    // Initialization - sets up physics world reference and chunk origin
    void initialize(PhysicsWorld* world, const glm::ivec3& chunkOrigin);
    
    // Physics world management
    void setPhysicsWorld(PhysicsWorld* world) { physicsWorld = world; }
    PhysicsWorld* getPhysicsWorld() const { return physicsWorld; }
    
    // Physics body lifecycle
    void createChunkPhysicsBody();           // Create compound shape physics body for static geometry
    void updateChunkPhysicsBody();           // Rebuild physics body when static geometry changes
    void forcePhysicsRebuild();              // Force immediate compound shape rebuild (bypasses optimization)
    void cleanupPhysicsResources();          // Clean up physics bodies
    
    // Access to physics body
    btRigidBody* getChunkPhysicsBody() const { return chunkPhysicsBody; }
    
    // UNIFIED SPATIAL COLLISION SYSTEM - O(1) operations with spatial grid optimization
    void addCollisionEntity(const glm::ivec3& localPos);              // Add collision entity with spatial tracking
    void removeCollisionEntities(const glm::ivec3& localPos);         // Remove all collision entities at position (O(1))
    void batchUpdateCollisions();                                      // Process collision changes in batch for performance
    void buildInitialCollisionShapes();                                // Build initial collision shapes with spatial grid
    
    // Neighbor collision management
    void updateNeighborCollisionShapes(const glm::ivec3& localPos);   // Update collision shapes of neighboring cubes
    void endBulkOperation();                                           // End bulk loading and update all neighbor collision shapes
    
    // Bulk operation control
    bool isInBulkOperation() const { return m_isInBulkOperation; }
    void setInBulkOperation(bool inBulk) { m_isInBulkOperation = inBulk; }
    
    // Collision update tracking
    bool getCollisionNeedsUpdate() const { return collisionNeedsUpdate; }
    void setCollisionNeedsUpdate(bool needsUpdate) { collisionNeedsUpdate = needsUpdate; }
    bool& getCollisionNeedsUpdateRef() { return collisionNeedsUpdate; } // For compatibility during migration
    
    // Spatial grid access
    const CollisionSpatialGrid& getCollisionGrid() const { return collisionGrid; }
    CollisionSpatialGrid& getCollisionGrid() { return collisionGrid; }
    
    // TEMPORARY: Direct access to physics members for gradual migration
    // These will be removed once all physics logic is fully extracted
    // TODO: Remove these accessors after extracting all physics methods from Chunk.cpp
    // TODO: Move createChunkPhysicsBody, updateChunkPhysicsBody implementations
    // TODO: Move addCollisionEntity, removeCollisionEntities implementations
    // TODO: Move buildInitialCollisionShapes, batchUpdateCollisions implementations
    btRigidBody*& getChunkPhysicsBodyRef() { return chunkPhysicsBody; }
    btCollisionShape*& getChunkCollisionShapeRef() { return chunkCollisionShape; }
    btTriangleMesh*& getChunkTriangleMeshRef() { return chunkTriangleMesh; }
    Physics::PhysicsWorld*& getPhysicsWorldRef() { return physicsWorld; }
    
    // Const accessors for const methods
    btCollisionShape* const& getChunkCollisionShapeRef() const { return chunkCollisionShape; }
    
    // Debugging and validation
    void validateCollisionSystem() const;                              // Validate spatial grid consistency
    void debugLogSpatialGrid() const;                                  // Log detailed spatial grid information
    size_t getCollisionEntityCount() const;                            // Get total collision entity count
    size_t getCubeEntityCount() const;                                 // Get cube collision entity count
    size_t getSubcubeEntityCount() const;                              // Get subcube collision entity count
    void debugPrintSpatialGridStats() const;                           // Print spatial grid performance statistics
    
    // Collision shape creation helpers - focused single-purpose functions
    // These require access to chunk's cube data, so they take function callbacks
    using CubeAccessFunc = std::function<const Cube*(const glm::ivec3&)>;
    using SubcubeAccessFunc = std::function<Subcube*(const glm::ivec3&, const glm::ivec3&)>;
    using MicrocubesAccessFunc = std::function<std::vector<Microcube*>(const glm::ivec3&, const glm::ivec3&)>;
    using StaticSubcubesAccessFunc = std::function<std::vector<Subcube*>(const glm::ivec3&)>;
    
    void createCubeCollisionShape(const glm::ivec3& localPos, btCompoundShape* compound,
                                  const CubeAccessFunc& getCube);
    void createSubcubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, 
                                     btCompoundShape* compound, const SubcubeAccessFunc& getSubcube);
    void createMicrocubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos,
                                       const Microcube* microcube, btCompoundShape* compound);
    
    // Collision entity management - requires callbacks for voxel data access
    void addCollisionEntity(const glm::ivec3& localPos,
                           const CubeAccessFunc& getCube,
                           const MicrocubesAccessFunc& getMicrocubes,
                           const StaticSubcubesAccessFunc& getStaticSubcubes);
    
    // Helper: Check if cube has exposed faces (optimization)
    bool hasExposedFaces(const glm::ivec3& localPos, const CubeAccessFunc& getCube) const;
    
public:
    // Collision box structure for physics optimization
    struct CollisionBox {
        glm::vec3 center;
        glm::vec3 halfExtents;
        
        CollisionBox(const glm::vec3& center, const glm::vec3& halfExtents) 
            : center(center), halfExtents(halfExtents) {}
    };
    
private:
    // Physics body for static geometry (compound shape made from individual cube collision boxes)
    btRigidBody* chunkPhysicsBody = nullptr;
    btCollisionShape* chunkCollisionShape = nullptr;
    btTriangleMesh* chunkTriangleMesh = nullptr;  // For BVH triangle mesh shape (option B)
    
    // UNIFIED SPATIAL COLLISION SYSTEM - O(1) operations with spatial optimization
    CollisionSpatialGrid collisionGrid;
    bool collisionNeedsUpdate = false;             // Flag for batch collision updates
    bool m_isInBulkOperation = false;              // Flag to prevent neighbor updates during bulk loading
    
    // Physics world reference
    PhysicsWorld* physicsWorld = nullptr;
    
    // Chunk world origin (for physics body positioning)
    glm::ivec3 chunkOrigin = glm::ivec3(0);
    
    // Helper: Generate optimized collision boxes for compound shape
    std::vector<CollisionBox> generateMergedCollisionBoxes(const CubeAccessFunc& getCube);
};

} // namespace Physics
} // namespace VulkanCube
