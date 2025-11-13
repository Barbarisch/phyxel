#pragma once

#include "Types.h"
#include "core/Subcube.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <vulkan/vulkan.h>

// Bullet Physics forward declarations (global namespace)
class btRigidBody;
class btCollisionShape;
class btCompoundShape;
class btTriangleMesh; // Bullet forward declaration

namespace VulkanCube {

// Forward declarations
namespace Physics {
    class PhysicsWorld;
}

/**
 * Chunk class that manages a 32x32x32 section of cubes
 * Handles cube storage, face generation, and Vulkan buffer management
 */
class Chunk {
    friend class ChunkManager;  // Allow ChunkManager to access private members for cross-chunk culling
    
private:
    std::vector<Cube*> cubes;                      // Pointers to cubes for efficient deletion (32x32x32)
    std::vector<Subcube*> staticSubcubes;          // Static subcubes (part of chunk physics body)
    std::vector<InstanceData> faces;               // Visible faces only (CPU pre-filtered for rendering)
    VkBuffer instanceBuffer = VK_NULL_HANDLE;      // Vulkan buffer for this chunk's face instance data
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
    void* mappedMemory = nullptr;                  // Persistent mapping for updates
    uint32_t numInstances = 0;                     // Variable count based on visible faces
    glm::ivec3 worldOrigin = glm::ivec3(0);        // World-space origin of this chunk
    bool needsUpdate = false;                      // Flag for buffer updates
    
    // NEW: O(1) lookup data structures for optimized hover system
    std::unordered_map<glm::ivec3, Cube*, IVec3Hash> cubeMap;              // O(1) cube lookup by local position
    std::unordered_map<glm::ivec3, 
                      std::unordered_map<glm::ivec3, Subcube*, IVec3Hash>, 
                      IVec3Hash> subcubeMap;                                // O(1) subcube lookup: localPos -> (subcubePos -> subcube)
    std::unordered_map<glm::ivec3, VoxelLocation::Type, IVec3Hash> voxelTypeMap; // O(1) voxel type lookup
    
    // Buffer capacity management
    static constexpr size_t DEFAULT_BUFFER_CAPACITY = 25000;   // Realistic capacity based on testing (8x current usage)
    size_t bufferCapacity = 0;                     // Current allocated buffer capacity
    size_t maxInstancesUsed = 0;                   // Peak usage tracking for analysis
    
    // Vulkan device handles (set by ChunkManager)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    
    // Physics body for static geometry (compound shape made from individual cube collision boxes)
    mutable btRigidBody* chunkPhysicsBody = nullptr;
    mutable btCollisionShape* chunkCollisionShape = nullptr;
    
    // Dirty tracking for smart saves
    bool isDirty = false;                          // Track if chunk has been modified since last save
    mutable btTriangleMesh* chunkTriangleMesh = nullptr; // For BVH triangle mesh shape (option B)
    
    // UNIFIED SPATIAL COLLISION SYSTEM - O(1) operations with spatial optimization
    // Replaces hash map-based system with spatial grid for better performance and simpler architecture
    struct CollisionEntity {
        btCollisionShape* shape;
        enum Type { CUBE, SUBCUBE } type;
        bool isInCompound;                    // Track if shape is managed by Bullet compound
        
        // Spatial data for optimization
        glm::vec3 worldCenter;                // World-space center position
        float boundingRadius;                 // Bounding radius for spatial queries
        
        // Hierarchy data (for subcubes only)
        glm::ivec3 parentChunkPos;           // Parent position within chunk (0-31 range)
        glm::ivec3 subcubeLocalPos;          // Local position within parent (0-2 range)
        
        CollisionEntity(btCollisionShape* s, Type t, const glm::vec3& center, float radius = 0.5f);
        ~CollisionEntity();
        
        // Utility methods
        bool isSubcube() const { return type == SUBCUBE; }
        bool isCube() const { return type == CUBE; }
    };
    
    // Spatial grid for O(1) collision entity management
    class CollisionSpatialGrid {
    public:
        static constexpr int GRID_SIZE = 32;  // Match chunk dimensions (32x32x32)
        static constexpr size_t MAX_ENTITIES_PER_CELL = 27;  // Max subcubes per position
        
        // Core operations - all O(1) or O(k) where k is entities per cell (≤27)
        void addEntity(const glm::ivec3& gridPos, std::shared_ptr<CollisionEntity> entity);
        void removeEntity(const glm::ivec3& gridPos, std::shared_ptr<CollisionEntity> entity);
        void removeAllAt(const glm::ivec3& gridPos);
        std::vector<std::shared_ptr<CollisionEntity>>& getEntitiesAt(const glm::ivec3& gridPos);
        const std::vector<std::shared_ptr<CollisionEntity>>& getEntitiesAt(const glm::ivec3& gridPos) const;
        
        // Batch operations
        void clear();
        void reserve(size_t expectedEntities);
        
        // Performance metrics and debugging
        size_t getTotalEntityCount() const { return totalEntities; }
        size_t getCubeEntityCount() const { return cubeEntities; }
        size_t getSubcubeEntityCount() const { return subcubeEntities; }
        size_t getOccupiedCellCount() const;
        
        // Validation
        bool validateGrid() const;
        void debugPrintStats() const;
        
    private:
        // 3D spatial grid - each cell contains entities at that position
        std::vector<std::shared_ptr<CollisionEntity>> grid[GRID_SIZE][GRID_SIZE][GRID_SIZE];
        
        // Performance counters
        mutable size_t totalEntities = 0;
        mutable size_t cubeEntities = 0;
        mutable size_t subcubeEntities = 0;
        
        // Bounds checking
        bool isValidGridPosition(const glm::ivec3& pos) const {
            return pos.x >= 0 && pos.x < GRID_SIZE &&
                   pos.y >= 0 && pos.y < GRID_SIZE &&
                   pos.z >= 0 && pos.z < GRID_SIZE;
        }
    };
    
    // Replace multiple hash maps with single spatial grid
    CollisionSpatialGrid collisionGrid;
    mutable bool collisionNeedsUpdate = false;                       // Flag for batch collision updates
    bool isInBulkOperation = false;                                   // Flag to prevent neighbor updates during bulk loading operations
    
    // DEBUG: Spatial grid provides O(1) collision tracking and comprehensive metrics
    
    /*
     * COLLISION SYSTEM IMPROVEMENTS SUMMARY:
     * - Fixed double-deletion crashes with reference counting
     * - Individual subcube tracking (no more geometric heuristics)
     * - Memory-safe cleanup with compound shape ownership tracking
     * - Comprehensive debugging and validation infrastructure
     * - Eliminated manual memory management errors
     * 
     * Trade-offs:
     * - ~70% increase in collision tracking memory usage
     * - 20-50% slower individual collision operations
     * - Significantly faster cleanup and no crashes
     */

public:
    // Constructor
    explicit Chunk(const glm::ivec3& origin = glm::ivec3(0));
    
    // Destructor
    ~Chunk();
    
    // Copy constructor and assignment operator (deleted - chunks should not be copied)
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    
    // Move constructor and assignment operator
    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;
    
    // Initialization
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    
    // Basic properties
    glm::ivec3 getWorldOrigin() const { return worldOrigin; }
    size_t getCubeCount() const { return cubes.size(); }
    size_t getStaticSubcubeCount() const { return staticSubcubes.size(); }
    size_t getTotalSubcubeCount() const { return staticSubcubes.size(); }     // Only static subcubes remain in chunks
    uint32_t getNumInstances() const { return numInstances; }
    bool getNeedsUpdate() const { return needsUpdate; }
    void setNeedsUpdate(bool needsUpdate) { this->needsUpdate = needsUpdate; }
    
    // Buffer capacity analysis
    size_t getBufferCapacity() const { return bufferCapacity; }
    size_t getMaxInstancesUsed() const { return maxInstancesUsed; }
    float getBufferUtilization() const { 
        return bufferCapacity > 0 ? float(faces.size()) / float(bufferCapacity) * 100.0f : 0.0f; 
    }
    
    // Cube access
    Cube* getCubeAt(const glm::ivec3& localPos);
    const Cube* getCubeAt(const glm::ivec3& localPos) const;
    Cube* getCubeAtIndex(size_t index);
    const Cube* getCubeAtIndex(size_t index) const;
    
    // Subcube access
    Subcube* getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    const Subcube* getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    std::vector<Subcube*> getSubcubesAt(const glm::ivec3& localPos);
    std::vector<Subcube*> getStaticSubcubesAt(const glm::ivec3& localPos);
    
    // Physics-related subcube access (legacy for transfer process)
    const std::vector<Subcube*>& getStaticSubcubes() const { return staticSubcubes; }
    
    // NEW: O(1) VoxelLocation resolution system for optimized hover detection
    VoxelLocation resolveLocalPosition(const glm::ivec3& localPos) const;
    bool hasVoxelAt(const glm::ivec3& localPos) const;
    bool hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    VoxelLocation::Type getVoxelType(const glm::ivec3& localPos) const;
    
    // NEW: O(1) optimized lookups (replace linear searches)
    Cube* getCubeAtFast(const glm::ivec3& localPos);
    const Cube* getCubeAtFast(const glm::ivec3& localPos) const;
    Subcube* getSubcubeAtFast(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    const Subcube* getSubcubeAtFast(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    std::vector<Subcube*> getSubcubesAtFast(const glm::ivec3& localPos);
    
    // Internal: Maintain hash map consistency
    void updateVoxelMaps(const glm::ivec3& localPos);
    void addToVoxelMaps(const glm::ivec3& localPos, Cube* cube);
    void removeFromVoxelMaps(const glm::ivec3& localPos);
    void addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube);
    void removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    void initializeVoxelMaps();  // Initialize hash maps from existing data
    
    // Cube manipulation
    bool setCubeColor(const glm::ivec3& localPos, const glm::vec3& color);
    bool removeCube(const glm::ivec3& localPos);
    bool addCube(const glm::ivec3& localPos, const glm::vec3& color);
    
    // Subcube manipulation
    bool subdivideAt(const glm::ivec3& localPos);              // Convert cube to 27 static subcubes
    bool addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const glm::vec3& color);
    bool removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    bool clearSubdivisionAt(const glm::ivec3& localPos);       // Remove all subcubes and restore cube
    
    // Physics-related subcube manipulation
    bool breakSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos,  // Move subcube from static to global dynamic system
                     class Physics::PhysicsWorld* physicsWorld = nullptr, 
                     class ChunkManager* chunkManager = nullptr,
                     const glm::vec3& impulseForce = glm::vec3(0.0f));
    
    // Chunk operations
    void populateWithCubes();                      // Fill chunk with 32x32x32 cubes
    void initializeForLoading();                   // Initialize empty chunk for database loading
    void rebuildFaces();                           // Regenerate face data from cubes (intra-chunk culling only)
    
    // Overload for cross-chunk culling: accepts a function to check neighbors in adjacent chunks
    using NeighborLookupFunc = std::function<const Cube*(const glm::ivec3& worldPos)>;
    void rebuildFaces(const NeighborLookupFunc& getNeighborCube);
    
    void updateVulkanBuffer();                     // Update GPU buffer with face data
    
    // Efficient partial updates for hover effects (avoids full rebuild)
    void updateSingleCubeTexture(const glm::ivec3& localPos, uint16_t textureIndex);
    void updateSingleSubcubeTexture(const glm::ivec3& parentLocalPos, const glm::ivec3& subcubePos, uint16_t textureIndex);
    
    // Legacy color functions - TODO: Remove after full texture transition
    // void updateSingleCubeColor(const glm::ivec3& localPos, const glm::vec3& newColor);
    // void updateSingleSubcubeColor(const glm::ivec3& parentLocalPos, const glm::ivec3& subcubePos, const glm::vec3& newColor);
    
    // Dirty tracking for smart saves
    bool getIsDirty() const { return isDirty; }
    void setDirty(bool dirty = true) { isDirty = dirty; }
    void markClean() { isDirty = false; }
    
    // Vulkan buffer management
    void createVulkanBuffer();
    void cleanupVulkanResources();
    void ensureBufferCapacity(size_t requiredInstances);  // Handle buffer reallocation if needed
    
    // Buffer utilization analysis
    void logBufferUtilization() const;
    
    // Physics management
    void setPhysicsWorld(class Physics::PhysicsWorld* world) { physicsWorld = world; }
    void createChunkPhysicsBody();                    // Create compound shape physics body for static geometry
    void updateChunkPhysicsBody();                    // Rebuild physics body when static geometry changes
    void forcePhysicsRebuild();                       // Force immediate compound shape rebuild (bypasses performance optimization)
    void cleanupPhysicsResources();                   // Clean up physics bodies
    class btRigidBody* getChunkPhysicsBody() const { return chunkPhysicsBody; }
    
    // UNIFIED SPATIAL COLLISION SYSTEM - O(1) operations with spatial grid optimization
    void addCollisionEntity(const glm::ivec3& localPos);                       // Add collision entity with spatial tracking
    void removeCollisionEntities(const glm::ivec3& localPos);          // Remove all collision entities at position (O(1))
    void batchUpdateCollisions();                                       // Process collision changes in batch for performance
    void buildInitialCollisionShapes();                               // Build initial collision shapes with spatial grid
    bool hasExposedFaces(const glm::ivec3& localPos) const;           // Check if cube has exposed faces (optimization)
    
    // ENHANCED DEBUG: Spatial grid debugging and validation infrastructure
    void validateCollisionSystem() const;                             // Validate spatial grid consistency and detect issues
    void debugLogSpatialGrid() const;                                 // Log detailed spatial grid information for debugging
    size_t getCollisionEntityCount() const;                           // Get total collision entity count from spatial grid
    size_t getCubeEntityCount() const;                                // Get cube collision entity count
    size_t getSubcubeEntityCount() const;                             // Get subcube collision entity count
    void debugPrintSpatialGridStats() const;                          // Print comprehensive spatial grid performance statistics
    void updateNeighborCollisionShapes(const glm::ivec3& localPos);   // Update collision shapes of neighboring cubes
    void endBulkOperation();                                           // End bulk loading and update all neighbor collision shapes
    
    // Bounding box access for culling
    glm::vec3 getMinBounds() const;
    glm::vec3 getMaxBounds() const;
    
    // Utility functions
    static size_t localToIndex(const glm::ivec3& localPos);
    static glm::ivec3 indexToLocal(size_t index);
    static size_t subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    
    // Access for ChunkManager (friend access or public as needed)
    VkBuffer getInstanceBuffer() const { return instanceBuffer; }
    const std::vector<InstanceData>& getFaces() const { return faces; }
    void* getMappedMemory() const { return mappedMemory; }

private:
    // Collision box structure for physics optimization
    struct CollisionBox {
        glm::vec3 center;
        glm::vec3 halfExtents;
        
        CollisionBox(const glm::vec3& center, const glm::vec3& halfExtents) 
            : center(center), halfExtents(halfExtents) {}
    };
    
    std::vector<CollisionBox> generateMergedCollisionBoxes();  // Generate optimized collision boxes for compound shape
    
private:
    // Physics world reference for physics body creation
    class Physics::PhysicsWorld* physicsWorld = nullptr;
    
    // Helper functions
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool isValidLocalPosition(const glm::ivec3& localPos) const;
};

} // namespace VulkanCube
