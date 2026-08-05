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

public:
    // ── WATER SPAN TYPE — water as chunk-resident world data (docs/Water.md §2 layer 1) ──────
    // Defined up top so the storage member below can use it; the accessors live with the other
    // public API further down. One contiguous run of water in one of this chunk's columns,
    // CLIPPED to this chunk's vertical range, in chunk-LOCAL coordinates (blobs stay
    // position-independent, like the voxel sections). A span crossing a vertical chunk border
    // is stored in BOTH chunks, each holding its clip (top == 32.0 = continues above).
    // `top` is float on purpose: the CA's fractional surface fill round-trips without
    // quantising (a lake at 148.9 stays 148.9).
    struct WaterSpanLocal {
        uint8_t x = 0, z = 0;      // local column, 0..31
        float   bottom = 0.0f;     // local Y of the span's base (rests on terrain or chunk floor)
        float   top = 0.0f;        // local Y of the surface / clip ceiling; valid iff top > bottom
    };

private:
    // CRITICAL: cubes vector is INDEXED by position, not a dynamic list!
    // Index formula: z + y*32 + x*32*32 (see localToIndex())
    // Always use getCubeAt(localPos) for O(1) lookup, never linear search!
    std::vector<std::unique_ptr<Cube>> cubes;                      // Pointers to cubes for efficient deletion (32x32x32)
    std::vector<std::unique_ptr<Subcube>> staticSubcubes;          // Static subcubes (part of chunk physics body)
    std::vector<std::unique_ptr<Microcube>> staticMicrocubes;      // Static microcubes (finest subdivision level)
    std::vector<WaterSpanLocal> waterSpans;                        // Water as world data — see setWaterSpans
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

    // Sealed state (Phase 4.4) — written only by ChunkManager (friend) during seal evaluation.
    bool m_sealed = false;

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

    // ── WATER SPANS accessors (type defined at the top of the class) ─────────────────────────
    //
    // ⚑WHY CHUNK-RESIDENT: water placement used to be re-derived at draw time from whichever
    // source the renderer had (implicit sea / coarse 128 m bake / camera-local sim cells), which
    // is how water was drawn through hillsides (606/606 rim-leak columns) and inside solid rock.
    // Spans make "where water is" WORLD DATA: written by generation, persisted with the chunk
    // blob, read by everything else. The renderer's job becomes drawing what is there.
    //
    /// Replace this chunk's water spans. Callers pass spans sorted by (x,z); the setter asserts
    /// order in debug rather than re-sorting, so an unsorted producer is a caught bug, not a cost.
    void setWaterSpans(std::vector<WaterSpanLocal> spans);
    const std::vector<WaterSpanLocal>& getWaterSpans() const { return waterSpans; }
    void clearWaterSpans() { waterSpans.clear(); }
    
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
    /// Number of SOLID cube voxels (4.2b: answered by the palette store in O(1)). Note this
    /// used to return the raw slot-array size (32768 for any loaded chunk) — every caller
    /// actually wanted "how many cubes are there", which this now truthfully is.
    size_t getCubeCount() const { return voxelManager.getVoxelStore().solidCount(); }
    /// True if the chunk holds at least one solid cube/subcube/microcube. O(1): cube presence
    /// is the store's maintained count (every solid cube voxel is in the store, materialized
    /// or not).
    bool hasAnySolidVoxel() const {
        return voxelManager.getVoxelStore().solidCount() > 0 ||
               !staticSubcubes.empty() || !staticMicrocubes.empty();
    }
    /// Hybrid read: is there a VISIBLE solid cube at this local cell? Overlay Cube wins if
    /// materialized, else the store answers. Never allocates — this is the probe the mesher's
    /// cross-chunk border culling and the skylight roof scan use.
    bool visibleSolidCubeAt(const glm::ivec3& localPos) const {
        if (localPos.x < 0 || localPos.x >= 32 || localPos.y < 0 || localPos.y >= 32 ||
            localPos.z < 0 || localPos.z >= 32) return false;   // out-of-range must not alias
        return visibleSolidCubeAtIndex(localToIndex(localPos));
    }
    bool visibleSolidCubeAtIndex(size_t index) const {
        if (index < cubes.size() && cubes[index]) return cubes[index]->isVisible();
        return voxelManager.getVoxelStore().visible(index);
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
    /// Sealed (Phase 4.4): uniform-solid chunk capped on all six sides by solid neighbour
    /// boundary layers. Sealed chunks skip meshing, occlude fully, and are unregistered from
    /// the physics grid list until an edit unseals them. Evaluated by ChunkManager's managed
    /// rebuild (classification needs neighbour data).
    bool isSealed() const { return m_sealed; }
    glm::ivec3 getFirstMirrorLocal() const { return m_firstMirrorLocal; }
    bool getNeedsUpdate() const { return renderManager.getNeedsUpdate(); }
    void setNeedsUpdate(bool needsUpdate) { renderManager.setNeedsUpdate(needsUpdate); }
    
    // Buffer capacity analysis
    size_t getBufferCapacity() const { return renderManager.getBufferCapacity(); }
    size_t getMaxInstancesUsed() const { return renderManager.getMaxInstancesUsed(); }
    float getBufferUtilization() const { return renderManager.getBufferUtilization(); }
    
    // Cube access. getCubeAt MATERIALIZES since 4.2b: a store-only solid voxel gets its heap
    // Cube allocated on first access (so the existing Cube* callers keep working); air returns
    // nullptr. Use hasVoxelAt/getVoxelType/visibleSolidCubeAt for presence — those never
    // allocate. getCubeAtIndex is the RAW overlay read: nullptr for air AND for store-only
    // voxels (used by scan paths that pair it with getVoxelStore()).
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
    // Sub-voxel floor height as a fraction of the voxel, or negative if it must be treated as
    // fully solid. See ChunkVoxelManager::subVoxelFloor (WaterSystemV3 Phase 4B).
    float subVoxelFloor(const glm::ivec3& localPos) const;
    VoxelLocation::Type getVoxelType(const glm::ivec3& localPos) const;
    
    // NEW: O(1) optimized lookups (replace linear searches)
    Cube* getCubeAtFast(const glm::ivec3& localPos);
    const Cube* getCubeAtFast(const glm::ivec3& localPos) const;
    
    // Re-read one voxel from its Cube into the palette store (Phase 4.2a). Call after mutating a
    // Cube's material/visible outside add/removeCube — see ChunkVoxelManager::syncStoreAt.
    void syncVoxelStoreAt(const glm::ivec3& localPos);

    // Set a solid cube voxel's visible flag. Writes the palette store (and any materialized
    // overlay Cube) — the legacy row-per-voxel DB load uses this to hide interior voxels
    // without forcing a Cube allocation. No-op if the cell holds no solid cube.
    void setCubeVisible(const glm::ivec3& localPos, bool visible);

    // Number of heap Cube objects currently alive in this chunk (non-null `cubes` slots).
    // Phase 4.2b's observable: static terrain must keep this at ~0 (voxels live in the palette
    // store; a Cube is materialized only when a caller needs per-voxel physics/damage state).
    size_t materializedCubeCount() const {
        size_t n = 0;
        for (const auto& c : cubes) if (c) ++n;
        return n;
    }

    // Palette-compressed static voxel state (Phase 4.2a mirror of `cubes`; authority flips in
    // 4.2b). Scan-heavy readers (mesher, occupancy) should move to this.
    const ChunkVoxelStore& getVoxelStore() const { return voxelManager.getVoxelStore(); }

    // Internal: Maintain hash map consistency (subdivided voxels only — the cube-keyed
    // updateVoxelMaps/addToVoxelMaps/removeFromVoxelMaps trio went away with the dense
    // cubeMap/voxelTypeMap in Phase 4.1; cube presence/type derive from `cubes` on read).
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
    // Fill every cell with one material — O(1) uniform store write, no Cube allocations
    // (Phase 4.4; used by the blob decoder's whole-chunk-run fast path and populateWithCubes).
    void fillAllCubes(const std::string& material);
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

    /// C4/C5: the LOD level this chunk's mesh is currently built at. 0 = full detail (the normal
    /// fine mesh); N >= 1 = coarse LOD-cell mesh with 2^N-cube cells.
    int  getLodLevel() const { return m_lodLevel; }
    // NOTE: there is deliberately NO setLodLevel(). The level is recorded by whichever call
    // BUILT the mesh -- setLodFaces(faces, level) or rebuildFaces() -- so the tracked level and
    // the resident mesh cannot disagree. A standalone setter would let a caller record a level
    // without building the matching mesh, which is precisely the bug fixed on 2026-07-30
    // (updateChunkLod then saw "already correct" and skipped every chunk).

    /// C4 — swap this chunk's faces for a coarse LOD-cell mesh (docs/ContinuousLodPlan.md).
    /// Caller supplies faces from Core::LodChunkMesh::buildForLevel; call updateVulkanBuffer()
    /// afterwards. Return to full detail with rebuildFaces() + updateVulkanBuffer().
    /// `level` is RECORDED here rather than left to the caller. Callers previously had to
    /// remember a separate setLodLevel(), and POST /api/debug/lod_level did not — so a chunk's
    /// tracked level said "2" while its mesh was the fine one. updateChunkLod() then compared
    /// wanted-vs-current, saw "already correct", and skipped every chunk: distance LOD silently
    /// did nothing while chunks_by_level reported success. Taking the level as an argument makes
    /// that desync unrepresentable. Pinned by LodChunkMeshTest.LodLevelTracksTheMeshActuallyBuilt.
    void setLodFaces(std::vector<InstanceData>&& lodFaces, int level) {
        renderManager.setFacesFromLod(std::move(lodFaces));
        m_lodLevel = level;
    }

private:
    int m_lodLevel = 0;
public:
    
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
    // Phase 4.3 A2: bind offset (arena span offset; 0 in legacy mode).
    VkDeviceSize getInstanceBindOffset() const { return renderManager.getFaceBindOffset(); }

    // Grass blade layer (lightweight): parallel per-chunk buffer + instance count.
    VkBuffer getGrassBuffer() const { return renderManager.getGrassBuffer(); }
    uint32_t getGrassCount() const { return renderManager.getGrassCount(); }
    VkDeviceSize getGrassBindOffset() const { return renderManager.getGrassBindOffset(); }
    const std::vector<GrassInstanceData>& getGrassInstances() const { return renderManager.getGrassInstances(); }

    // Foliage leaf-card layer: parallel per-chunk buffer + instance count.
    VkBuffer getFoliageBuffer() const { return renderManager.getFoliageBuffer(); }
    uint32_t getFoliageCount() const { return renderManager.getFoliageCount(); }
    VkDeviceSize getFoliageBindOffset() const { return renderManager.getFoliageBindOffset(); }

private:
    // Helper functions
    bool isValidLocalPosition(const glm::ivec3& localPos) const;
    // Bind voxelManager's callbacks to THIS object (initialize + both move operations).
    void wireVoxelManagerCallbacks();

    // ── Phase 4.4 seal state (ChunkManager drives classification via friendship) ──
    // Enter the sealed state: no mesh, fully occluding, physics grid out of the query list.
    void applySealedRenderState();
    // Uniform-air fast path: no mesh, sight passes freely (occlusion graph all-connected).
    void applyAirRenderState();

public:
    // Leave the sealed state (any voxel mutation on this chunk, or a NEIGHBOUR's boundary edit
    // exposing our face — ChunkVoxelModificationSystem::unsealExposedNeighbors). Re-registers
    // the physics grid IMMEDIATELY — a player digging into a sealed chunk needs collision this
    // tick, not when the queued remesh runs. Idempotent and cheap when not sealed.
    void unsealForEdit();

    // U1a: a collidable (neither uniform-air nor sealed) chunk must have its occupancy grid in
    // the broadphase query set. Called on the full-mesh classification branch to reverse a prior
    // air/sealed unregister (an air chunk that gained content, a broken neighbour cap).
    // Idempotent — registerGrid dedups in O(1) — so it is safe to call every rebuild.
    void ensurePhysicsRegistered();

private:
};

} // namespace Phyxel
