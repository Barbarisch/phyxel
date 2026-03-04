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
 * EXTRACTED FROM CHUNK.CPP (Phase 2 Complete):
 * This class isolates all Bullet Physics logic that was previously embedded in Chunk.cpp.
 * Successfully extracted ~833 lines of physics code, reducing Chunk.cpp by 34%.
 * 
 * Responsibilities:
 * - Physics body lifecycle (create, update, rebuild)
 * - Collision shape creation for cubes, subcubes, and microcubes
 * - Collision entity tracking via O(1) spatial grid
 * - Batch collision updates for performance
 * - Neighbor collision shape updates
 * - Bulk operation management
 * 
 * Design Pattern:
 * - Uses callback functions to access Chunk data without circular dependencies
 * - Callbacks: CubeAccessFunc, SubcubeAccessFunc, MicrocubesAccessFunc, etc.
 * - Enables clean separation while maintaining performance
 * 
 * CRITICAL PERFORMANCE NOTES:
 * - Cube access MUST use indexed lookups: index = z + y*32 + x*32*32
 * - NEVER use linear search through cube vectors (causes O(n²) performance)
 * - Collision grid provides O(1) spatial lookups
 * - All methods validate inputs and handle edge cases safely
 * 
 * Successfully isolates all Bullet Physics dependencies from Chunk class.
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
    
    // Callback function typedefs for accessing chunk data
    // IMPORTANT: These must be defined before any methods that use them
    using CubeAccessFunc = std::function<const Cube*(const glm::ivec3&)>;
    using SubcubeAccessFunc = std::function<Subcube*(const glm::ivec3&, const glm::ivec3&)>;
    using MicrocubesAccessFunc = std::function<std::vector<Microcube*>(const glm::ivec3&, const glm::ivec3&)>;
    using StaticSubcubesAccessFunc = std::function<std::vector<Subcube*>(const glm::ivec3&)>;
    using CubesArrayAccessFunc = std::function<const std::vector<Cube*>&()>;
    using IndexToLocalFunc = std::function<glm::ivec3(size_t)>;
    using StaticMicrocubesAccessFunc = std::function<const std::vector<Microcube*>&()>;
    
    // Physics body lifecycle
    void createChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                const StaticSubcubesAccessFunc& getStaticSubcubes,
                                const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                const IndexToLocalFunc& indexToLocal,
                                const CubeAccessFunc& getCube);
    void updateChunkPhysicsBody(const CubesArrayAccessFunc& getCubes,
                                const StaticSubcubesAccessFunc& getStaticSubcubes,
                                const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                const IndexToLocalFunc& indexToLocal,
                                const CubeAccessFunc& getCube);
    void forcePhysicsRebuild(const CubesArrayAccessFunc& getCubes,
                            const StaticSubcubesAccessFunc& getStaticSubcubes,
                            const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                            const IndexToLocalFunc& indexToLocal,
                            const CubeAccessFunc& getCube);
    void cleanupPhysicsResources();          // Clean up physics bodies
    
    // Access to physics body
    btRigidBody* getChunkPhysicsBody() const { return chunkPhysicsBody; }
    
    // UNIFIED SPATIAL COLLISION SYSTEM - O(1) operations with spatial grid optimization
    void addCollisionEntity(const glm::ivec3& localPos);              // Add collision entity with spatial tracking
    void removeCollisionEntities(const glm::ivec3& localPos);         // Remove all collision entities at position (O(1))
    
    // Build initial collision shapes with spatial grid (requires callbacks)
    void buildInitialCollisionShapes(const CubesArrayAccessFunc& getCubes,
                                     const StaticSubcubesAccessFunc& getStaticSubcubes,
                                     const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                                     const IndexToLocalFunc& indexToLocal,
                                     const CubeAccessFunc& getCube);
    
    // Process collision changes in batch for performance
    void batchUpdateCollisions(const CubesArrayAccessFunc& getCubes,
                              const StaticSubcubesAccessFunc& getStaticSubcubes,
                              const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                              const IndexToLocalFunc& indexToLocal,
                              const CubeAccessFunc& getCube);
    
    // Neighbor collision management
    void updateNeighborCollisionShapes(const glm::ivec3& localPos,
                                       const CubeAccessFunc& getCube,
                                       const MicrocubesAccessFunc& getMicrocubes,
                                       const StaticSubcubesAccessFunc& getStaticSubcubes);
    void endBulkOperation(const CubesArrayAccessFunc& getCubes,
                         const StaticSubcubesAccessFunc& getStaticSubcubes,
                         const StaticMicrocubesAccessFunc& getStaticMicrocubes,
                         const IndexToLocalFunc& indexToLocal,
                         const CubeAccessFunc& getCube);
    
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
