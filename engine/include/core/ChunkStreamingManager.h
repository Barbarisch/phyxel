#pragma once

#include "Types.h"
#include "Chunk.h"
#include <glm/glm.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Phyxel {
    class WorldStorage;   // Forward declaration
    class WorldGenerator; // Forward declaration (async generation worker)
}

namespace Phyxel {

// Custom hash function for glm::ivec3 to use as key in unordered_map
struct ChunkCoordHash {
    std::size_t operator()(const glm::ivec3& coord) const {
        // Combine X, Y, Z coordinates into a single hash
        // Using prime numbers to reduce hash collisions
        return std::hash<int>()(coord.x) ^ 
               (std::hash<int>()(coord.y) << 1) ^ 
               (std::hash<int>()(coord.z) << 2);
    }
};

/**
 * ChunkStreamingManager - Handles chunk loading, unloading, and world persistence
 * 
 * EXTRACTED FROM CHUNKMANAGER.CPP (November 2025)
 * Original ChunkManager: 1,414 lines → 1,201 lines after extraction
 * ChunkStreamingManager: 313 lines total (103 header + 210 implementation)
 * Reduction: -213 lines (-15%) from ChunkManager
 * 
 * PURPOSE:
 * Manages the lifecycle of chunks in an infinite voxel world:
 * - Dynamic loading of chunks based on player position
 * - Automatic unloading of distant chunks to save memory
 * - Persistent world storage via WorldStorage integration
 * - Chunk generation fallback when storage doesn't exist
 * 
 * RESPONSIBILITIES:
 * 1. Chunk Streaming: Load/unload chunks based on player proximity
 * 2. World Persistence: Save/load chunks to/from database
 * 3. Chunk Generation: Generate new chunks when not in storage
 * 4. Coordinate Conversion: World ↔ Chunk coordinate transformations
 * 
 * DESIGN PATTERN:
 * Uses callbacks to access ChunkManager's internal state without tight coupling:
 * - ChunkCreationFunc: Create new chunks in ChunkManager
 * - ChunkMapAccessFunc: Access chunk spatial hash map
 * - ChunkVectorAccessFunc: Access chunk vector for iteration
 * - DeviceAccessFunc: Get Vulkan device handles for chunk initialization
 * 
 * USAGE:
 * ChunkStreamingManager streamingManager;
 * streamingManager.setCallbacks(
 *     [this](const glm::ivec3& origin) { createChunk(origin); },
 *     [this]() -> auto& { return chunkMap; },
 *     [this]() -> auto& { return chunks; },
 *     [this]() { return std::make_pair(device, physicalDevice); }
 * );
 * streamingManager.initializeWorldStorage("worlds/default.db");
 * streamingManager.updateStreaming(playerPosition, loadDistance, unloadDistance);
 */
class ChunkStreamingManager {
public:
    // Callback types for accessing ChunkManager state
    using ChunkCreationFunc = std::function<void(const glm::ivec3& origin)>;
    using ChunkMapAccessFunc = std::function<std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash>&()>;
    using ChunkVectorAccessFunc = std::function<std::vector<std::unique_ptr<Chunk>>&()>;
    using DeviceAccessFunc = std::function<std::pair<VkDevice, VkPhysicalDevice>()>;
    /// Called after each chunk is added (loaded or generated). Argument is the chunk's world origin.
    using OnChunkLoadedFunc = std::function<void(const glm::ivec3& chunkOrigin)>;
    /// Fills a freshly-created streamed chunk with content. Arguments are the chunk and
    /// its chunk coordinate. If unset, falls back to Chunk::populateWithCubes() (legacy
    /// random fill). This is the Phase-1 generation wire.
    using GenerateChunkFunc = std::function<void(Chunk& chunk, const glm::ivec3& chunkCoord)>;
    /// Finalizes a chunk that just streamed in at runtime: registers static collision
    /// (the per-chunk counterpart to ChunkManager::buildAllChunkPhysics()) AND builds its
    /// faces with cross-chunk culling (re-culling neighbours), since the streaming load
    /// path itself only creates the Vulkan buffer. If unset, no-op. Eviction teardown is
    /// automatic: the Chunk destructor unregisters its grid and frees its buffer.
    using OnChunkStreamedInFunc = std::function<void(Chunk& chunk)>;

    // Out-of-line (cpp): members include unique_ptr<WorldGenerator>, which is only
    // forward-declared here — inline ctor/dtor would instantiate its deleter on an
    // incomplete type in every including TU.
    ChunkStreamingManager();
    ~ChunkStreamingManager();

    // Callback setup
    void setCallbacks(
        ChunkCreationFunc createChunkFunc,
        ChunkMapAccessFunc getChunkMapFunc,
        ChunkVectorAccessFunc getChunksFunc,
        DeviceAccessFunc getDevicesFunc
    );
    /// Optional: called after every chunk load/generate (for height map, etc.)
    void setOnChunkLoaded(OnChunkLoadedFunc cb) { m_onChunkLoaded = std::move(cb); }
    /// Optional: how to fill a freshly-generated streamed chunk (the generation wire).
    void setGenerationCallback(GenerateChunkFunc cb) { m_generateChunk = std::move(cb); }
    /// Optional: how to finalize a chunk as it streams in (collision + faces). See above.
    void setOnChunkStreamedIn(OnChunkStreamedInFunc cb) { m_onChunkStreamedIn = std::move(cb); }
    /// When true, chunks are finalized (collision + faces) as they stream in at runtime.
    /// Leave OFF during the initial bulk DB load (buildAllChunkPhysics + rebuildAllChunkFaces
    /// handle that pass) so grids are not registered twice. Default off.
    void setPerChunkPhysics(bool enabled) { m_perChunkPhysics = enabled; }
    /// Cap on how many new chunks may be generated per updateStreaming() call (nearest
    /// first) so a single frame's pump cannot generate a whole sphere and hitch.
    /// 0 = unlimited (legacy behavior). Default 0.
    void setMaxChunksPerUpdate(int n) { m_maxChunksPerUpdate = n; }

    // ---- Async generation (Phase 1c) -------------------------------------------------
    /// Returns a PRIVATE WorldGenerator copy for the worker thread (the live streaming
    /// generator must never be shared across threads — sampleColumn is non-const).
    /// Called lazily on the first pump, i.e. after the game-definition loader has
    /// finished applying recipe/params. Return null to keep generation synchronous.
    using GeneratorSnapshotFunc = std::function<std::unique_ptr<WorldGenerator>()>;
    /// Main-thread finalize for a worker-generated chunk AFTER it was initialized,
    /// buffered and inserted: physics attach + prebuilt-grid registration,
    /// pristine-clean, budgeted remesh marks.
    using FinalizeGeneratedChunkFunc = std::function<void(Chunk& chunk, const glm::ivec3& chunkCoord)>;
    /// Flora decoration on the WORKER thread, using the worker's private generator.
    /// Must only read shared state (the preloaded template library) and write into the
    /// passed chunk — ObjectTemplateManager::decorateChunk satisfies this. Flora was
    /// the dominant main-thread cost after generation moved off-thread (dense-forest
    /// chunks: 450-625ms of template stamping each).
    using WorkerDecorateFunc = std::function<void(Chunk& chunk, const glm::ivec3& chunkCoord,
                                                  WorldGenerator& workerGenerator)>;
    void setWorkerFloraDecorator(WorkerDecorateFunc cb) { m_workerDecorate = std::move(cb); }

    /// Enable async generation: terrain fill + voxel maps + occupancy-grid FILL run on
    /// a dedicated worker thread (chunks arrive a few pumps later instead of stalling
    /// the frame); DB loads and everything Vulkan/physics-world stay on the main
    /// thread. The worker starts lazily on the first pump after a snapshot succeeds.
    void setAsyncGeneration(GeneratorSnapshotFunc snapshotFn, FinalizeGeneratedChunkFunc finalizeFn) {
        m_generatorSnapshot = std::move(snapshotFn);
        m_finalizeGenerated = std::move(finalizeFn);
    }
    /// Join the worker and drop queued/finished work (world switch, shutdown).
    void stopAsyncGeneration();

    // World storage management
    bool initializeWorldStorage(const std::string& worldPath);
    void disconnectWorldStorage();

    // Streaming update (call every frame)
    void updateStreaming(const glm::vec3& playerPosition, float loadDistance, float unloadDistance);

    // Manual chunk operations
    void loadChunksAroundPosition(const glm::vec3& position, float radius);
    void unloadDistantChunks(const glm::vec3& position, float radius);
    std::vector<glm::ivec3> loadAllChunksFromDatabase();
    bool generateOrLoadChunk(const glm::ivec3& chunkCoord);
    bool loadChunk(const glm::ivec3& chunkCoord);

    // World persistence
    bool saveChunk(Chunk* chunk);
    bool saveAllChunks();
    bool saveDirtyChunks();

    // Accessors
    WorldStorage* getWorldStorage() const { return worldStorage; }

private:
    // Callback functions
    ChunkCreationFunc m_createChunk;
    ChunkMapAccessFunc m_getChunkMap;
    ChunkVectorAccessFunc m_getChunks;
    DeviceAccessFunc m_getDevices;
    OnChunkLoadedFunc m_onChunkLoaded;
    GenerateChunkFunc m_generateChunk;
    OnChunkStreamedInFunc m_onChunkStreamedIn;
    bool m_perChunkPhysics = false;
    int m_maxChunksPerUpdate = 0;

    // Chunks evicted last pump, held one more pump before their Vulkan buffers are freed,
    // so no in-flight frame is still referencing them (frame-deferred deletion). See
    // unloadDistantChunks().
    std::vector<std::unique_ptr<Chunk>> m_pendingDeletion;

    // World storage. All SQLite access is serialized by m_storageMutex: the async
    // worker LOADS chunk rows off-thread (a DB chunk load is ~150-400ms in Debug —
    // it was the moving-camera stall in DB-saved regions), while the main thread
    // still saves (dirty evictions, save_world) and reads meta. SQLite is
    // serialized-mode safe, but WorldStorage keeps statement state, so we guard
    // explicitly.
    WorldStorage* worldStorage = nullptr;
    std::mutex m_storageMutex;

    // ---- Async generation worker state (Phase 1c) ----
    GeneratorSnapshotFunc m_generatorSnapshot;
    FinalizeGeneratedChunkFunc m_finalizeGenerated;
    WorkerDecorateFunc m_workerDecorate;
    std::unique_ptr<WorldGenerator> m_workerGenerator;  // owned/used by the worker only
    std::thread m_genWorker;
    std::mutex m_genRequestMutex;
    std::condition_variable m_genCv;
    std::deque<glm::ivec3> m_genRequests;               // guarded by m_genRequestMutex
    std::mutex m_genResultMutex;
    std::vector<std::unique_ptr<Chunk>> m_genResults;   // guarded by m_genResultMutex
    std::unordered_set<glm::ivec3, ChunkCoordHash> m_genPending;  // main thread only
    std::vector<glm::ivec3> m_genFailed;                // coords whose build threw (guarded by m_genResultMutex)
    std::atomic<bool> m_stopGen{false};

    // Disposal worker: owns the off-thread destruction of evicted chunk husks. A
    // JOINABLE thread (stopped in stopAsyncGeneration/dtor), NOT detached — a detached
    // thread still freeing memory during process/CRT teardown dies silently with no
    // crash dump, which is exactly the failure mode we cannot debug.
    std::thread m_disposalThread;
    std::mutex m_disposalMutex;
    std::condition_variable m_disposalCv;
    std::vector<std::unique_ptr<Chunk>> m_disposalQueue;  // guarded by m_disposalMutex
    void disposalLoop();

    void maybeStartGenWorker();
    void genWorkerLoop();
    /// Main thread: initialize + buffer + insert + finalize worker-built chunks
    /// (bounded per pump; drops results that were superseded or flown past).
    void drainGeneratedChunks(const glm::vec3& position, float unloadRadius);
    bool asyncGenerationActive() const { return m_genWorker.joinable(); }

    // Helper methods
    Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord);
};

} // namespace Phyxel
