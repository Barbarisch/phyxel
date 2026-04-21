#pragma once

#include "Types.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "graphics/ChunkRenderBuffer.h"
#include "graphics/ChunkRenderManager.h"
#include "physics/ChunkPhysicsManager.h"
#include "core/ChunkVoxelManager.h"
#include "core/ChunkVoxelBreaker.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <vulkan/vulkan.h>

namespace Phyxel {

// Forward declarations
namespace Physics {
    class PhysicsWorld;
}

/**
 * Chunk class that manages a 32x32x32 section of cubes
 * 
 * REFACTORING STATUS:
 * ✓ Phase 1 Complete: Rendering extracted to ChunkRenderManager (~328 lines)
 * ✓ Phase 2 Complete: Physics extracted to ChunkPhysicsManager (~833 lines)
 * ✓ Phase 3 Complete: Voxel management extracted to ChunkVoxelManager (~616 lines)
 *   - All voxel hierarchy operations (cubes, subcubes, microcubes)
 *   - Hash map management for O(1) lookups
 *   - Subdivision logic and voxel type resolution
 *   - Callback pattern for clean separation
 * ✓ Phase 21 Complete: Voxel breaking extracted to ChunkVoxelBreaker (~120 lines)
 *   - breakSubcube logic for static→dynamic conversion
 *   - Physics body creation and force application
 *   - Global dynamic object transfer
 * 
 * Size Reduction:
 * - Original: 2,444 lines
 * - Phase 1&2: 1,611 lines (-833 lines, -34%)
 * - Phase 3: 995 lines (-616 lines from Phase 2, -1,449 total, -59%)
 * - Phase 21: 876 lines (-120 lines from Phase 3, -1,568 total, -64%)
 * 
 * Current responsibilities:
 * - Cube storage and voxel hierarchy (cubes, subcubes, microcubes)
 * - Coordinate with subsystem managers (rendering, physics)
 * - Voxel manipulation operations (add, remove, subdivide)
 * - Cross-chunk coordination via ChunkManager
 */
class Chunk {
    friend class ChunkManager;  // Allow ChunkManager to access private members for cross-chunk culling
    
private:
    // CRITICAL: cubes vector is INDEXED by position, not a dynamic list!
    // Index formula: z + y*32 + x*32*32 (see localToIndex())
    // Always use getCubeAt(localPos) for O(1) lookup, never linear search!
    std::vector<std::unique_ptr<Cube>> cubes;                      // Pointers to cubes for efficient deletion (32x32x32)
    std::vector<std::unique_ptr<Subcube>> staticSubcubes;          // Static subcubes (part of chunk physics body)
    std::vector<std::unique_ptr<Microcube>> staticMicrocubes;      // Static microcubes (finest subdivision level)
    glm::ivec3 worldOrigin = glm::ivec3(0);        // World-space origin of this chunk
    
    // Subsystem managers
    Graphics::ChunkRenderManager renderManager;    // Manages face generation and Vulkan buffers
    Physics::ChunkPhysicsManager physicsManager;   // Manages collision shapes and physics bodies
    ChunkVoxelManager voxelManager;                // Manages voxel hierarchy and hash maps
    ChunkVoxelBreaker voxelBreaker;                // Manages breaking voxels to dynamic physics objects
    
    // Vulkan device handles (set by ChunkManager)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    
    // Dirty tracking for smart saves
    bool isDirty = false;                          // Track if chunk has been modified since last save

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
    size_t getStaticMicrocubeCount() const { return staticMicrocubes.size(); }
    size_t getTotalSubcubeCount() const { return staticSubcubes.size(); }     // Only static subcubes remain in chunks
    uint32_t getNumInstances() const { return renderManager.getNumInstances(); }
    bool getNeedsUpdate() const { return renderManager.getNeedsUpdate(); }
    void setNeedsUpdate(bool needsUpdate) { renderManager.setNeedsUpdate(needsUpdate); }
    
    // Buffer capacity analysis
    size_t getBufferCapacity() const { return renderManager.getBufferCapacity(); }
    size_t getMaxInstancesUsed() const { return renderManager.getMaxInstancesUsed(); }
    float getBufferUtilization() const { return renderManager.getBufferUtilization(); }
    
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
    
    // Microcube access
    Microcube* getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos);
    const Microcube* getMicrocubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos) const;
    std::vector<Microcube*> getMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);
    
    // Physics-related subcube access (legacy for transfer process)
    const std::vector<std::unique_ptr<Subcube>>& getStaticSubcubes() const { return staticSubcubes; }
    const std::vector<std::unique_ptr<Microcube>>& getStaticMicrocubes() const { return staticMicrocubes; }
    
    // NEW: O(1) VoxelLocation resolution system for optimized hover detection
    VoxelLocation resolveLocalPosition(const glm::ivec3& localPos) const;
    bool hasVoxelAt(const glm::ivec3& localPos) const;
    bool hasSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    VoxelLocation::Type getVoxelType(const glm::ivec3& localPos) const;
    
    // NEW: O(1) optimized lookups (replace linear searches)
    Cube* getCubeAtFast(const glm::ivec3& localPos);
    const Cube* getCubeAtFast(const glm::ivec3& localPos) const;
    
    // Internal: Maintain hash map consistency
    void updateVoxelMaps(const glm::ivec3& localPos);
    void addToVoxelMaps(const glm::ivec3& localPos, Cube* cube);
    void removeFromVoxelMaps(const glm::ivec3& localPos);
    void addSubcubeToMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos, Subcube* subcube);
    void removeSubcubeFromMaps(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    void addMicrocubeToMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, Microcube* microcube);
    void removeMicrocubeFromMaps(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos);
    void initializeVoxelMaps();  // Initialize hash maps from existing data
    
    // Cube manipulation
    bool removeCube(const glm::ivec3& localPos);
    bool addCube(const glm::ivec3& localPos);
    bool addCube(const glm::ivec3& localPos, const std::string& material);
    int removeCubesBatch(const std::vector<glm::ivec3>& positions);
    int addCubesBatch(const std::vector<glm::ivec3>& positions, const std::string& material = "");
    
    // Subcube manipulation
    bool subdivideAt(const glm::ivec3& localPos);              // Convert cube to 27 static subcubes
    bool addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const std::string& material = "Default");
    bool removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    bool clearSubdivisionAt(const glm::ivec3& localPos);       // Remove all subcubes and restore cube
    
    // Microcube manipulation
    bool subdivideSubcubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);  // Convert subcube to 27 microcubes
    bool addMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, const std::string& material = "Default");
    bool removeMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos);
    bool clearMicrocubesAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);  // Remove all microcubes at subcube position (leaves empty space)
    
    // Physics-related subcube manipulation
    bool breakSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos,  // Move subcube from static to global dynamic system
                     class Physics::PhysicsWorld* physicsWorld = nullptr, 
                     class ChunkManager* chunkManager = nullptr,
                     const glm::vec3& impulseForce = glm::vec3(0.0f));
    
    // Chunk operations
    void clearAll();                                   // Bulk clear: remove all voxels, rebuild once
    void populateWithCubes();                      // Fill chunk with 32x32x32 cubes
    void initializeForLoading();                   // Initialize empty chunk for database loading
    void rebuildFaces();                           // Regenerate face data from cubes (intra-chunk culling only)
    
    // Overload for cross-chunk culling: accepts a function to check neighbors in adjacent chunks
    using NeighborLookupFunc = Graphics::ChunkRenderManager::NeighborLookupFunc;
    void rebuildFaces(const NeighborLookupFunc& getNeighborCube);
    
    void updateVulkanBuffer();                     // Update GPU buffer with face data
    
    // Efficient partial updates for hover effects (avoids full rebuild)
    void updateSingleCubeTexture(const glm::ivec3& localPos, uint16_t textureIndex);
    void updateSingleSubcubeTexture(const glm::ivec3& parentLocalPos, const glm::ivec3& subcubePos, uint16_t textureIndex);
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
    void setPhysicsWorld(class Physics::PhysicsWorld* world);
    void createChunkPhysicsBody();                    // Create compound shape physics body for static geometry
    void updateChunkPhysicsBody();                    // Rebuild physics body when static geometry changes
    void forcePhysicsRebuild();                       // Force immediate compound shape rebuild (bypasses performance optimization)
    void cleanupPhysicsResources();                   // Clean up physics bodies
    class btRigidBody* getChunkPhysicsBody() const;
    
    // UNIFIED SPATIAL COLLISION SYSTEM - O(1) operations with spatial grid optimization
    void addCollisionEntity(const glm::ivec3& localPos);                       // Add collision entity with spatial tracking
    void removeCollisionEntities(const glm::ivec3& localPos);          // Remove all collision entities at position (O(1))
    void batchUpdateCollisions();                                       // Process collision changes in batch for performance
    void setPhysicsBulkMode(bool bulk);                               // Suppress per-voxel collision rebuilds; call batchUpdateCollisions when done
    void buildInitialCollisionShapes();                               // Build initial collision shapes with spatial grid
    bool hasExposedFaces(const glm::ivec3& localPos) const;           // Check if cube has exposed faces (optimization)
    
    // Collision shape creation helpers - focused single-purpose functions
    void createCubeCollisionShape(const glm::ivec3& localPos, class btCompoundShape* compound);           // Create collision for full cube
    void createSubcubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, class btCompoundShape* compound);  // Create collision for one subcube
    void createMicrocubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const Microcube* microcube, class btCompoundShape* compound);  // Create collision for one microcube
    
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
    VkBuffer getInstanceBuffer() const { return renderManager.getInstanceBuffer(); }
    const std::vector<InstanceData>& getFaces() const { return renderManager.getFaces(); }
    void* getMappedMemory() const { return renderManager.getMappedMemory(); }

private:
    // No private members needed - all moved to subsystems
    
    // Helper functions
    bool isValidLocalPosition(const glm::ivec3& localPos) const;
    std::vector<Physics::ChunkPhysicsManager::CollisionBox> generateMergedCollisionBoxes();  // Generate optimized collision boxes for compound shape
};

} // namespace Phyxel
