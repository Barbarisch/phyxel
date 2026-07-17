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

    // Cached per-chunk render flags (recomputed only on rebuildFaces, not per-frame).
    // Let the renderer's mirror / transparency checks be O(visibleChunks) instead of
    // scanning all 32768 cells every frame.
    bool m_hasMirror = false;
    bool m_hasTransparent = false;                 // any cube with material alpha < 0.99
    glm::ivec3 m_firstMirrorLocal{0};              // Local pos of first mirror cube (valid when m_hasMirror)
    void recomputeRenderFlags();                   // Rescan cubes for mirror/transparent materials; updates caches above

    // Occlusion visibility graph (Minecraft-style "cave culling"). m_faceConnect[f]
    // is a bitmask of which of the 6 chunk faces sight can reach from face f through
    // non-opaque cells. Faces: 0=X-,1=X+,2=Y-,3=Y+,4=Z-,5=Z+. Recomputed on
    // rebuildFaces. Default all-connected so an unmeshed chunk is never falsely culled.
    uint8_t m_faceConnect[6] = {0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F};
    void computeVisibilityMask();                  // Flood-fill air components; fill m_faceConnect

public:
    // Constructor
    explicit Chunk(const glm::ivec3& origin = glm::ivec3(0));

    // Occupancy-grid read access (SubcubeCollisionPlan P1 audit + debug dumps).
    const Physics::VoxelOccupancyGrid& getOccupancyGrid() const {
        return physicsManager.getOccupancyGrid();
    }
    
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
    /// True if the chunk holds at least one solid cube/subcube/microcube. The cube
    /// store may be sparse (pushed) or dense (32768 nullptr slots), so size() can't
    /// answer this — scan for a non-null entry (cheap pointer null-checks).
    bool hasAnySolidVoxel() const {
        if (!staticSubcubes.empty() || !staticMicrocubes.empty()) return true;
        for (const auto& c : cubes) if (c) return true;
        return false;
    }

    size_t getStaticSubcubeCount() const { return staticSubcubes.size(); }
    size_t getStaticMicrocubeCount() const { return staticMicrocubes.size(); }
    size_t getTotalSubcubeCount() const { return staticSubcubes.size(); }     // Only static subcubes remain in chunks
    uint32_t getNumInstances() const { return renderManager.getNumInstances(); }
    // Face-direction ranges for bucketed draws (Phase 3) — see ChunkRenderManager::getFaceDirRanges().
    const std::array<uint32_t, 7>& getFaceDirRanges() const { return renderManager.getFaceDirRanges(); }
    bool hasMirrorVoxel() const { return m_hasMirror; }            // Cached; see recomputeRenderFlags()
    bool hasTransparentVoxel() const { return m_hasTransparent; }  // Cached; any cube alpha < 0.99
    // Occlusion graph query: can sight pass from face a to face b through this chunk?
    bool facesConnected(int a, int b) const { return (m_faceConnect[a] >> b) & 1u; }
    glm::ivec3 getFirstMirrorLocal() const { return m_firstMirrorLocal; }
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
    bool removeCube(const glm::ivec3& localPos, bool deferRebuild = false);
    bool addCube(const glm::ivec3& localPos);
    // overwrite=true removes an existing solid cube and places the new one in its place (default
    // false = reject if occupied). See ChunkVoxelManager::addCube.
    bool addCube(const glm::ivec3& localPos, const std::string& material, bool overwrite = false);
    int removeCubesBatch(const std::vector<glm::ivec3>& positions);
    int addCubesBatch(const std::vector<glm::ivec3>& positions, const std::string& material = "");
    
    // Subcube manipulation
    bool subdivideAt(const glm::ivec3& localPos);              // Convert cube to 27 static subcubes
    bool addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const std::string& material = "Default", uint32_t tint = 0xFFFFFFu, uint8_t state = 0);
    bool removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    bool clearSubdivisionAt(const glm::ivec3& localPos);       // Remove all subcubes and restore cube
    int clearCellsBulk(const std::vector<glm::ivec3>& localCells);  // Bulk clear (one storage pass); caller owns occupancy+remesh
    
    // Microcube manipulation
    bool subdivideSubcubeAt(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);  // Convert subcube to 27 microcubes
    bool addMicrocube(const glm::ivec3& parentCubePos, const glm::ivec3& subcubePos, const glm::ivec3& microcubePos, const std::string& material = "Default", uint32_t tint = 0xFFFFFFu, uint8_t state = 0);
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
    
    // Overload for cross-chunk culling: accepts a function to check neighbors in adjacent chunks,
    // plus an optional cross-chunk baked-light lookup for light bleed across chunk boundaries.
    using NeighborLookupFunc = Graphics::ChunkRenderManager::NeighborLookupFunc;
    using NeighborLightFunc  = Graphics::ChunkRenderManager::NeighborLightFunc;
    using BakedLight         = Graphics::ChunkRenderManager::BakedLight;
    // columnOpenMask (optional): 32x32 sky-open grid (x*32+z) precomputed by ChunkManager from
    // the chunks above, so the skylight bake skips the slow per-cell roof probe.
    void rebuildFaces(const NeighborLookupFunc& getNeighborCube,
                      const NeighborLightFunc& getNeighborLight = nullptr,
                      const std::vector<uint8_t>* columnOpenMask = nullptr);

    // Did this chunk's boundary light change on the last rebuild? (drives neighbour re-mesh)
    bool lightBordersChanged() const { return renderManager.lightBordersChanged(); }
    // Read this chunk's baked light at a local cell (for neighbours' bleed). False if not baked.
    bool bakedLightAt(const glm::ivec3& localPos, BakedLight& out) const {
        return renderManager.bakedLightAt(localPos.x, localPos.y, localPos.z, out);
    }
    // World positions of this chunk's state=flaming voxels (fire VFX seeds; see FireEmitterManager).
    const std::vector<glm::vec3>& getFlamingVoxels() const { return renderManager.getFlamingVoxels(); }

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
    /// Register an occupancy grid that was already FILLED off-thread (async streaming:
    /// the worker runs forcePhysicsRebuild — pure CPU; only this registration touches
    /// the dynamics world and must run on the main thread, after setPhysicsWorld()).
    void registerPrebuiltPhysics();
    void updateChunkPhysicsBody();                    // Rebuild physics body when static geometry changes
    void forcePhysicsRebuild();                       // Force immediate compound shape rebuild (bypasses performance optimization)
    void cleanupPhysicsResources();

    // Collision entity management
    void addCollisionEntity(const glm::ivec3& localPos);
    void removeCollisionEntities(const glm::ivec3& localPos);
    void batchUpdateCollisions();
    void setPhysicsBulkMode(bool bulk);
    void buildInitialCollisionShapes();
    bool hasExposedFaces(const glm::ivec3& localPos) const;

    // Collision shape creation helpers (occupancy grid only)
    void createCubeCollisionShape(const glm::ivec3& localPos);
    void createSubcubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos);
    void createMicrocubeCollisionShape(const glm::ivec3& cubePos, const glm::ivec3& subcubePos, const Microcube* microcube);
    
    // ENHANCED DEBUG: Spatial grid debugging and validation infrastructure
    void validateCollisionSystem() const;                             // Validate spatial grid consistency and detect issues
    void debugLogSpatialGrid() const;                                 // Log detailed spatial grid information for debugging
    size_t getCollisionEntityCount() const;                           // Get total collision entity count from spatial grid
    size_t getCubeEntityCount() const;                                // Get cube collision entity count
    size_t getSubcubeEntityCount() const;                             // Get subcube collision entity count
    void debugPrintSpatialGridStats() const;                          // Print comprehensive spatial grid performance statistics
    void updateNeighborCollisionShapes(const glm::ivec3& localPos);   // Update collision shapes of neighboring cubes
    void beginBulkOperation();                                         // Begin bulk loading: skip per-voxel collision adds (rebuilt once by endBulkOperation)
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

    // Grass blade layer (lightweight): parallel per-chunk buffer + instance count.
    VkBuffer getGrassBuffer() const { return renderManager.getGrassBuffer(); }
    uint32_t getGrassCount() const { return renderManager.getGrassCount(); }

    // Foliage leaf-card layer: parallel per-chunk buffer + instance count.
    VkBuffer getFoliageBuffer() const { return renderManager.getFoliageBuffer(); }
    uint32_t getFoliageCount() const { return renderManager.getFoliageCount(); }

private:
    // No private members needed - all moved to subsystems
    
    // Helper functions
    bool isValidLocalPosition(const glm::ivec3& localPos) const;
};

} // namespace Phyxel
