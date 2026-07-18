#pragma once

#include "Types.h"
#include "IChunkManager.h"
#include "Chunk.h"
#include "Cube.h"
#include "utils/CoordinateUtils.h"
#include "core/ChunkStreamingManager.h"
#include "core/WorldGenerator.h"
#include "core/DynamicObjectManager.h"
#include "core/FaceUpdateCoordinator.h"
#include "core/ChunkInitializer.h"
#include "core/DirtyChunkTracker.h"
#include "core/ChunkVoxelQuerySystem.h"
#include "core/ChunkVoxelModificationSystem.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <functional>

namespace Physics {
    class PhysicsWorld;
}

namespace Phyxel {
    class WorldStorage; // Forward declaration
    class GpuParticlePhysics; // Forward declaration for height map updates
}

namespace Phyxel {

// Manages all chunks in the world for scalable multi-chunk rendering
// 
// REFACTORING STATUS
// Phase 1 - ChunkStreamingManager extraction COMPLETE
// Phase 2 - DynamicObjectManager extraction COMPLETE
// Phase 3 - FaceUpdateCoordinator extraction COMPLETE
// Phase 4 - ChunkInitializer extraction COMPLETE
// Phase 5 - DirtyChunkTracker extraction COMPLETE
// Phase 6 - ChunkVoxelQuerySystem extraction COMPLETE
// Phase 7 - ChunkVoxelModificationSystem extraction COMPLETE
// Phase 20 - Removed duplicate physics update code (delegated to DynamicObjectManager) COMPLETE
// Original: 1,414 lines → Current: 604 lines (-810 lines, -57%)
// 
class ChunkManager : public IChunkManager {
public:
    std::vector<std::unique_ptr<Chunk>> chunks;

    // Access the world database (for per-world recipe/metadata). May be null if the world
    // has no persistent storage configured.
    WorldStorage* getWorldStorage() const { return m_streamingManager.getWorldStorage(); }

    // Global dynamic subcube management (not tied to specific chunks)
    std::vector<std::unique_ptr<Subcube>> globalDynamicSubcubes;
    
    // Global dynamic cube management (not tied to specific chunks)
    std::vector<std::unique_ptr<Cube>> globalDynamicCubes;
    
    // Global dynamic microcube management (not tied to specific chunks)
    std::vector<std::unique_ptr<Microcube>> globalDynamicMicrocubes;
    
    // Spatial hash map for O(1) chunk lookup by chunk coordinates
    std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash> chunkMap;
    
    // Vulkan device and memory management
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    
    // Physics world for proper cleanup of dynamic objects
    Physics::PhysicsWorld* physicsWorld = nullptr;

    // GPU particle physics — receives height map updates when voxels change
    GpuParticlePhysics* m_gpuParticles = nullptr;
    std::function<void(int, int, int, bool)> m_voxelOccupancyCallback;
    
    // Chunk streaming manager (handles chunk loading/unloading/saving)
    ChunkStreamingManager m_streamingManager;
    
    // Dynamic object manager (handles global dynamic subcubes/cubes/microcubes)
    DynamicObjectManager m_dynamicObjectManager;
    
    // Face update coordinator (handles face rebuilding and updates)
    FaceUpdateCoordinator m_faceUpdateCoordinator;
    
    // Chunk initializer (handles chunk creation and world generation)
    ChunkInitializer m_chunkInitializer;
    
    // Dirty chunk tracker (handles selective chunk updates)
    DirtyChunkTracker m_dirtyChunkTracker;
    
    // Voxel query system (handles chunk/cube/subcube lookups)
    ChunkVoxelQuerySystem m_voxelQuerySystem;
    
    // Voxel modification system (handles add/remove/update operations)
    ChunkVoxelModificationSystem m_voxelModificationSystem;
    
    // Chunk streaming settings (default view distance; a game.json loadRadius overrides per world)
    float loadDistance = 256.0f;   // Distance to load chunks (8 chunks; >= render distance so streamed chunks are loaded before they'd render)
    float unloadDistance = 352.0f; // Distance to unload chunks (11 chunks * 32 units)
    glm::vec3 playerPosition = glm::vec3(0.0f); // Player position for streaming

    // Streaming world generation (Phase 1: the generation wire). When enabled, chunks
    // streamed in by ChunkStreamingManager are filled by this generator instead of the
    // legacy random fill, and register/unregister collision per-chunk on stream-in/out.
    // Opt-in; default off so existing pre-baked / bulk-loaded worlds are unaffected.
    std::unique_ptr<WorldGenerator> m_worldGenerator;
    std::function<void(Chunk&, const glm::ivec3&)> m_floraDecorator;
    bool m_hasWorkerFlora = false;
    bool m_streamingGenerationEnabled = false;

    // Hybrid physics routing: FPS-based Bullet vs GPU fallback
    uint32_t m_frameBreakCount = 0;
    static constexpr uint32_t MAX_BULLET_BREAKS_PER_FRAME = 8;
    static constexpr float    GPU_FALLBACK_FPS_THRESHOLD  = 30.0f;
    float m_smoothedFps = 60.0f;  // Exponentially smoothed FPS estimate
    void updateSmoothedFps(float deltaTime);
    void resetFrameBreakCounter() { m_frameBreakCount = 0; }
    
    ChunkManager() = default;
    ChunkManager(const ChunkManager&) = delete;
    ChunkManager& operator=(const ChunkManager&) = delete;
    ~ChunkManager(); // Destructor needs to be defined in .cpp file due to unique_ptr with forward declaration
    
    // Initialize with Vulkan device handles
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    
    // Set physics world for proper cleanup of dynamic objects
    void setPhysicsWorld(Physics::PhysicsWorld* physics);

    // Set GPU particle system for occupancy grid updates (also wires its debris light sampler)
    void setGpuParticlePhysics(GpuParticlePhysics* gpp);

    // Notified (worldX, worldY, worldZ, solid) whenever a single voxel's occupancy
    // changes (break/place/occupancy update). Used to keep the water sim's solid mask
    // in sync so water flows into newly-removed cells.
    void setVoxelOccupancyCallback(std::function<void(int, int, int, bool)> cb) {
        m_voxelOccupancyCallback = std::move(cb);
    }

    // Update a single voxel's occupancy bit in the GPU grid.
    void updateOccupancyVoxel(int worldX, int worldY, int worldZ, bool solid);

    // Bulk-populate the GPU 3D occupancy grid from all currently loaded chunks.
    // Call once after GpuParticlePhysics is initialized and all startup chunks are loaded.
    void rebuildOccupancyFromChunks();

    // Push one loaded chunk's solid voxels to every occupancy consumer: the GPU particle grid
    // AND the per-voxel occupancy callback (the water sim's solidity mask). Called when a chunk
    // streams in at runtime — without the callback half, water flooded where terrain later loads
    // stayed inside it until the next region recenter re-read solidity (the stale-solid window).
    // No-op if the chunk isn't currently loaded.
    void syncChunkToOccupancy(const glm::ivec3& chunkWorldOrigin);
    
    // World storage management
    bool initializeWorldStorage(const std::string& worldPath);
    void disconnectWorldStorage();
    void setPlayerPosition(const glm::vec3& position) { playerPosition = position; }

    // Enable/disable streaming world generation and configure the generator. Pass
    // enabled=true with a generation type + seed to make streamed-in chunks generate
    // procedural terrain (and register collision per-chunk). Opt-in; default off.
    void configureStreamingGeneration(bool enabled,
                                      WorldGenerator::GenerationType type = WorldGenerator::GenerationType::Perlin,
                                      uint32_t seed = 0);
    bool isStreamingGenerationEnabled() const { return m_streamingGenerationEnabled; }
    // The generator used for streamed chunks (valid after configureStreamingGeneration(true)).
    // Exposed so callers can tune TerrainParams; null when streaming generation is off.
    WorldGenerator* getStreamingGenerator() { return m_worldGenerator.get(); }

    // Flora decoration for streamed chunks. Wired by the editor (which owns the template
    // manager); invoked after a newly generated chunk's terrain is filled, before faces/physics.
    void setFloraDecorator(std::function<void(Chunk&, const glm::ivec3&)> cb) {
        m_floraDecorator = std::move(cb);
    }
    // Worker-thread flora decoration (async generation): same stamping, but running on
    // the generation worker with ITS private generator. When set, the main-thread
    // m_floraDecorator is skipped for async-generated chunks. The callback must only
    // read shared state (preloaded template library) and write into the passed chunk.
    void setWorkerFloraDecorator(ChunkStreamingManager::WorkerDecorateFunc cb) {
        m_hasWorkerFlora = static_cast<bool>(cb);
        m_streamingManager.setWorkerFloraDecorator(std::move(cb));
    }

    // Chunk streaming for infinite worlds
    void updateChunkStreaming(); // Call every frame to load/unload chunks based on player position
    void loadChunksAroundPosition(const glm::vec3& position, float radius);
    void unloadDistantChunks(const glm::vec3& position, float radius);
    
    // World persistence
    bool saveChunk(Chunk* chunk);
    bool saveAllChunks();
    bool saveDirtyChunks();  // Save only chunks that have been modified
    bool loadChunk(const glm::ivec3& chunkCoord);
    std::vector<glm::ivec3> loadAllChunksFromDatabase();  // Load all chunks that exist in the database
    bool generateOrLoadChunk(const glm::ivec3& chunkCoord); // Generate if doesn't exist, load if it does

    // Stream-in boot (docs/LargeWorldScalePlan.md Phase 2): load only the DB chunks
    // near the anchor now; the rest arrive in the background (DB-only worlds) or on
    // approach (streaming worlds). Pump every frame — no-op once drained.
    std::vector<glm::ivec3> loadChunksNearAndDeferRest(const glm::vec3& anchor);
    void pumpDeferredDbLoads(const glm::vec3& position);
    bool hasDeferredDbLoads() const;
    
    // Post-loading face rebuilding (call after all chunks are loaded)
    void rebuildAllChunkFaces(); // Rebuild faces for all chunks with proper cross-chunk culling
    void rebuildAllChunkLighting(); // Re-bake + re-upload every chunk via the cross-chunk path (use after a global lighting-mode change)
    void buildAllChunkPhysics(); // Build collision + register occupancy grids for all chunks (call after bulk DB load)
    // [no-frozen-engine] touched-chunk counterpart: rebuild collision ONLY for chunks
    // intersecting [minWorld, maxWorld] (world cube coords). buildAllChunkPhysics is
    // O(all chunks) and cost 17-64 s PER BUILDING at settlement scale (~140 chunks);
    // per-build/regional work must never pay the whole world.
    void buildChunkPhysicsInRegion(const glm::ivec3& minWorld, const glm::ivec3& maxWorld);

    // Initialize hash maps for all existing chunks (call after loading chunks)
    void initializeAllChunkVoxelMaps();
    
    // Create multiple chunks at specified world origins
    void createChunks(const std::vector<glm::ivec3>& origins);
    
    // Create a single chunk at specified origin (populate=true uses world generator, populate=false creates empty chunk)
    void createChunk(const glm::ivec3& origin, bool populate = true);
    
    // Update chunk data (for dynamic content)
    void updateChunk(size_t chunkIndex);
    
    // OPTIMIZED: Update only chunks that have been modified (O(dirty) instead of O(all))
    // Call this every frame - it's efficient and only processes changed chunks
    void updateDirtyChunks();
    // Budgeted variant: spread a large dirty backlog (e.g. async world-gen) over
    // frames so the mesh+GPU commit never stalls for seconds. budgetMs<=0 = drain all.
    void updateDirtyChunks(double budgetMs);
    
    // DEPRECATED: Update all chunks (inefficient for large worlds)
    void updateAllChunks();
    
    // Face culling and rebuilding
    void calculateChunkFaceCulling();
    void rebuildGlobalDynamicSubcubeFaces();
    
    // Rebuild faces from cubes (call after modifying cubes)
    void rebuildChunkFaces(Chunk& chunk);
    
    // Rebuild faces with cross-chunk occlusion culling
    void rebuildChunkFacesWithCrosschunkCulling(Chunk& chunk);
    // Phase 4.4: all six neighbour boundary layers visible-solid? (chunk known uniform-solid)
    bool isChunkCapped(const Chunk& chunk);
    
    // Get chunk at world position (for adding/removing cubes)
    Chunk* getChunkAt(const glm::ivec3& worldPos);

    // Bulk clear all voxels in a chunk (instant, single rebuild)
    bool clearChunk(const glm::ivec3& chunkCoord);

    // Cube manipulation helpers
    Cube* getCubeAt(const glm::ivec3& worldPos);          // Get cube at world position
    bool removeCube(const glm::ivec3& worldPos);          // Returns true if cube was removed
    bool addCube(const glm::ivec3& worldPos);
    bool addCubeWithMaterial(const glm::ivec3& worldPos, const std::string& material);  // add cube with a specific material
    /// Create the owning chunk for worldPos if it doesn't exist yet (empty, populate=false).
    /// Lets material/subcube/microcube placement cross vertical chunk seams without pre-gen.
    void ensureChunkAt(const glm::ivec3& worldPos);
    
    // Subcube manipulation helpers
    Subcube* getSubcubeAt(const glm::ivec3& worldPos, const glm::ivec3& subcubePos); // Get subcube at position
    
    // Global dynamic subcube management
    void addGlobalDynamicSubcube(std::unique_ptr<Subcube> subcube);
    void updateGlobalDynamicSubcubes(float deltaTime);  // Update timers and cleanup expired ones
    void updateGlobalDynamicSubcubePositions();  // Update positions from physics bodies
    void clearAllGlobalDynamicSubcubes();
    void rebuildGlobalDynamicFaces();  // Rebuild global dynamic faces
    const std::vector<std::unique_ptr<Subcube>>& getGlobalDynamicSubcubes() const { return globalDynamicSubcubes; }
    size_t getGlobalDynamicSubcubeCount() const { return globalDynamicSubcubes.size(); }
    
    // Global dynamic cube management
    void addGlobalDynamicCube(std::unique_ptr<Cube> cube);
    void updateGlobalDynamicCubes(float deltaTime);  // Update timers and cleanup expired ones
    void updateGlobalDynamicCubePositions();  // Update positions from physics bodies
    void clearAllGlobalDynamicCubes();
    const std::vector<std::unique_ptr<Cube>>& getGlobalDynamicCubes() const { return globalDynamicCubes; }
    size_t getGlobalDynamicCubeCount() const { return globalDynamicCubes.size(); }
    
    // Global dynamic microcube management
    void addGlobalDynamicMicrocube(std::unique_ptr<Microcube> microcube);
    void updateGlobalDynamicMicrocubes(float deltaTime);  // Update timers and cleanup expired ones
    void updateGlobalDynamicMicrocubePositions();  // Update positions from physics bodies
    void clearAllGlobalDynamicMicrocubes();
    const std::vector<std::unique_ptr<Microcube>>& getGlobalDynamicMicrocubes() const { return globalDynamicMicrocubes; }
    size_t getGlobalDynamicMicrocubeCount() const { return globalDynamicMicrocubes.size(); }
    
    // Combined dynamic object management - face data access
    const std::vector<DynamicSubcubeInstanceData>& getGlobalDynamicSubcubeFaces() const { return globalDynamicSubcubeFaces; }
    
    // Dynamic object face data for rendering (both subcubes and full cubes)
    std::vector<DynamicSubcubeInstanceData> globalDynamicSubcubeFaces;
    
    // Convert between coordinate systems (forwarded to Utils::CoordinateUtils)
    static glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos) { 
        return Utils::CoordinateUtils::worldToChunkCoord(worldPos);
    }
    static glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos) { 
        return Utils::CoordinateUtils::worldToLocalCoord(worldPos);
    }
    static glm::ivec3 chunkCoordToOrigin(const glm::ivec3& chunkCoord) { 
        return Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    }
    
    // Fast O(1) chunk lookup functions
    Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord);      // Get chunk by chunk coordinates
    const Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord) const; // Const version
    Chunk* getChunkAtFast(const glm::ivec3& worldPos);        // Fast O(1) world position lookup

    // Sample the baked per-voxel light (skylight + RGB block light) at a WORLD cell, for
    // lighting dynamic objects (characters, furniture, debris) that aren't baked into the
    // static chunk mesh. Returns full sky / no block light when the cell has no loaded chunk,
    // so objects outside the world default to lit (outdoor) rather than black.
    Graphics::ChunkRenderManager::BakedLight sampleBakedLight(const glm::ivec3& worldPos) const;
    
    // Fast O(1) cube lookup functions
    Cube* getCubeAtFast(const glm::ivec3& worldPos);          // Fast O(1) cube lookup
    bool removeCubeFast(const glm::ivec3& worldPos);          // Fast cube removal
    bool addCubeFast(const glm::ivec3& worldPos);  // Fast cube addition
    
    // NEW: O(1) VoxelLocation resolution system for optimized hover detection
    VoxelLocation resolveGlobalPosition(const glm::ivec3& worldPos) const override;
    VoxelLocation resolveGlobalPositionWithSubcube(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) const;
    bool hasVoxelAt(const glm::ivec3& worldPos) const;
    VoxelLocation::Type getVoxelTypeAt(const glm::ivec3& worldPos) const;
    
    // Perform occlusion culling across chunks (check cube neighbors across chunk boundaries)
    void performOcclusionCulling();
    
    // Get performance statistics for all chunks
    struct ChunkStats {
        uint32_t totalCubes = 0;
        uint32_t totalVertices = 0;
        uint32_t totalVisibleFaces = 0;
        uint32_t totalHiddenFaces = 0;
        uint32_t fullyOccludedCubes = 0;
        uint32_t partiallyOccludedCubes = 0;
    };
    ChunkStats getPerformanceStats() const;
    
    // Dirty chunk tracking for performance optimization
    void markChunkDirty(size_t chunkIndex);
    void markChunkDirty(Chunk* chunk);               // Overload for chunk pointer
    // Queue a budgeted re-mesh WITHOUT the DB-dirty flag (voxel data unchanged —
    // e.g. neighbour re-cull after a chunk streams in). See DirtyChunkTracker.
    void markChunkForRemesh(Chunk* chunk);
    // Low-priority variant: processed only when the main dirty queue is empty
    // (cosmetic neighbour re-culls — skipping never creates holes).
    void markChunkForRemeshIdle(Chunk* chunk);
    void clearDirtyChunkList();
    size_t getChunkIndex(const Chunk* chunk) const;  // Helper to find chunk index from pointer
    
    // ========================================================================
    // EFFICIENT SELECTIVE UPDATE SYSTEM
    // ========================================================================
    
    // Event-specific efficient update methods
    void updateAfterCubeBreak(const glm::ivec3& worldPos);        // Updates only affected faces when cube is broken
    void updateAfterCubePlace(const glm::ivec3& worldPos);        // Updates only affected faces when cube is placed  
    void updateAfterCubeSubdivision(const glm::ivec3& worldPos);  // Updates when cube is subdivided into subcubes
    void updateAfterSubcubeBreak(const glm::ivec3& parentWorldPos, const glm::ivec3& subcubeLocalPos); // Updates when subcube breaks
    
    // Core selective update methods
    void updateFacesForPositionChange(const glm::ivec3& worldPos, bool cubeAdded); // Central method for position-based updates
    void updateNeighborFaces(const glm::ivec3& worldPos);          // Updates faces of up to 6 neighboring cubes
    void updateSingleCubeFaces(const glm::ivec3& worldPos);        // Updates faces of single cube only
    
    // Cross-chunk boundary helpers
    std::vector<glm::ivec3> getAffectedNeighborPositions(const glm::ivec3& worldPos); // Get all 6 neighbor positions (may span chunks)
    void updateFacesAtPosition(const glm::ivec3& worldPos);        // Update faces for cube at specific position
    
    // Cleanup all resources
    void cleanup();

    // Thread-safe chunk access mutex for background job system.
    // Background jobs acquire write lock for batch voxel operations.
    // Main thread's updateDirtyChunks acquires read lock when rebuilding faces.
    // Individual methods do NOT lock internally to avoid deadlocks in callback chains.
    mutable std::shared_mutex m_chunkAccessMutex;

    // Lock helpers for background job system
    std::unique_lock<std::shared_mutex> acquireWriteLock() { return std::unique_lock(m_chunkAccessMutex); }
    std::shared_lock<std::shared_mutex> acquireReadLock() const { return std::shared_lock(m_chunkAccessMutex); }
    
private:
    // Memory management helper
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    // Cross-chunk occlusion culling helpers
    bool isCubeAt(const glm::ivec3& worldPosition) const;
    uint32_t calculateOcclusionFaceMask(const glm::ivec3& chunkOrigin, int relativeX, int relativeY, int relativeZ) const;
};

} // namespace Phyxel
