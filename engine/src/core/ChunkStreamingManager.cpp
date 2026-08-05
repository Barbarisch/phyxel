#include "core/ChunkStreamingManager.h"
#include "core/WorldStorage.h"
#include "core/WorldGenerator.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <climits>
#include <limits>
#include <vector>
#include <utility>

namespace Phyxel {

ChunkStreamingManager::ChunkStreamingManager() = default;

ChunkStreamingManager::~ChunkStreamingManager() {
    stopAsyncGeneration();
    // Clean up world storage
    delete worldStorage;
    worldStorage = nullptr;
}

void ChunkStreamingManager::stopAsyncGeneration() {
    if (!m_genWorkers.empty() || m_disposalThread.joinable()) {
        m_stopGen = true;
        m_genCv.notify_all();
        m_disposalCv.notify_all();
        for (auto& w : m_genWorkers)
            if (w.joinable()) w.join();
        m_genWorkers.clear();
        if (m_disposalThread.joinable()) m_disposalThread.join();
        m_stopGen = false;
    }
    {
        std::lock_guard<std::mutex> lock(m_genRequestMutex);
        m_genRequests.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_genResultMutex);
        m_genResults.clear();  // Chunk dtors: no Vulkan buffer / grid registration yet — safe
        m_genFailed.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_disposalMutex);
        m_disposalQueue.clear();  // husks: Vulkan/physics already torn down on main
    }
    m_genPending.clear();
    m_workerGenerators.clear();
    // Stream-in boot state belongs to the world being torn down.
    m_dbBacklog.clear();
    m_bootResident.clear();
    m_dbWorkerMode = false;
}

void ChunkStreamingManager::disposalLoop() {
    for (;;) {
        std::vector<std::unique_ptr<Chunk>> batch;
        {
            std::unique_lock<std::mutex> lock(m_disposalMutex);
            m_disposalCv.wait(lock, [this] { return m_stopGen.load() || !m_disposalQueue.empty(); });
            if (m_stopGen && m_disposalQueue.empty()) return;
            batch.swap(m_disposalQueue);
        }
        try {
            batch.clear();  // pure memory frees (~64k+ heap blocks per solid chunk)
        } catch (const std::exception& e) {
            LOG_ERROR("ChunkStreaming", "Disposal worker exception: {}", e.what());
        } catch (...) {
            LOG_ERROR("ChunkStreaming", "Disposal worker: unknown exception");
        }
    }
}

void ChunkStreamingManager::maybeStartGenWorker() {
    if (!m_genWorkers.empty() || !m_finalizeGenerated) return;
    // Snapshot lazily on the first pump: by now the game-definition loader has applied
    // recipe/terrain params to the live generator, so the copies are fully configured.
    // One PRIVATE generator copy per worker (bake memo is copy-safe).
    if (m_generatorSnapshot) {
        for (int i = 0; i < kGenWorkerCount; ++i) {
            auto gen = m_generatorSnapshot();
            if (!gen) break;  // snapshot unavailable (streaming generation disabled)
            m_workerGenerators.push_back(std::move(gen));
        }
    }
    // Pure-DB worker mode (stream-in boot for DB-only worlds): run without
    // generators — the workers only load from storage and drop misses.
    if (m_workerGenerators.empty() && !m_dbWorkerMode) return;
    m_stopGen = false;
    const int workerCount = m_workerGenerators.empty() ? kGenWorkerCount
                                                       : int(m_workerGenerators.size());
    for (int i = 0; i < workerCount; ++i) {
        WorldGenerator* gen = (i < int(m_workerGenerators.size())) ? m_workerGenerators[i].get()
                                                                   : nullptr;
        m_genWorkers.emplace_back([this, gen] { genWorkerLoop(gen); });
    }
    m_disposalThread = std::thread([this] { disposalLoop(); });
    LOG_INFO("ChunkStreaming", "Async chunk-generation ({} worker(s)) + disposal workers started{}",
             workerCount, m_workerGenerators.empty() ? " (pure DB-load mode, no generator)" : "");
}

void ChunkStreamingManager::genWorkerLoop(WorldGenerator* workerGen) {
    for (;;) {
        glm::ivec3 chunkCoord;
        {
            std::unique_lock<std::mutex> lock(m_genRequestMutex);
            m_genCv.wait(lock, [this] { return m_stopGen.load() || !m_genRequests.empty(); });
            if (m_stopGen) return;
            chunkCoord = m_genRequests.front();
            m_genRequests.pop_front();
        }
        // An uncaught exception here would std::terminate the whole process SILENTLY
        // (no crash dump from a worker thread abort) — catch, log, and report the
        // failed coord so the main thread can clear its pending mark (retried on a
        // later pump instead of leaving a permanent hole).
        try {
            // Everything below touches ONLY this detached chunk + the worker's private
            // generator (+ the mutex-guarded world storage): DB load or terrain fill,
            // the O(1) voxel hash maps, and the occupancy-grid FILL (pure CPU). Vulkan
            // buffer, physics-world registration and insertion happen later on the
            // main thread (drainGeneratedChunks).
            glm::ivec3 origin = Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
            auto chunk = std::make_unique<Chunk>(origin);
            chunk->initializeForLoading();  // dense storage + no-op callbacks + grid origin
            // DB-saved chunks (edits!) take priority over regeneration. The load runs
            // HERE, off-thread — a DB chunk load is ~150-400ms in Debug and was the
            // remaining moving-camera stall in previously saved regions.
            bool fromDb = false;
            {
                std::lock_guard<std::mutex> lock(m_storageMutex);
                if (worldStorage) fromDb = worldStorage->loadChunk(chunkCoord, *chunk);
            }
            if (fromDb) {
                chunk->markClean();  // loaded state is by definition persisted
            } else if (workerGen) {
                workerGen->generateChunk(*chunk, chunkCoord);
                // Flora before maps/grid so canopy voxels are included in both. DB-loaded
                // chunks were saved WITH their flora — no re-decoration.
                if (m_workerDecorate) m_workerDecorate(*chunk, chunkCoord, *workerGen);
            } else {
                // Pure-DB mode: a miss means the coord has no saved data — drop it
                // (report via m_genFailed so the pending mark is cleared; DB-only
                // worlds never generate).
                LOG_WARN("ChunkStreaming", "DB-load-only worker: no data for ({},{},{}) — dropped",
                         chunkCoord.x, chunkCoord.y, chunkCoord.z);
                std::lock_guard<std::mutex> lock(m_genResultMutex);
                m_genFailed.push_back(chunkCoord);
                continue;
            }
            chunk->initializeVoxelMaps();
            chunk->forcePhysicsRebuild();   // occupancy-grid fill; registration is main-thread
            {
                std::lock_guard<std::mutex> lock(m_genResultMutex);
                m_genResults.push_back(std::move(chunk));
            }
        } catch (const std::exception& e) {
            LOG_ERROR("ChunkStreaming", "Generation worker exception at ({},{},{}): {}",
                      chunkCoord.x, chunkCoord.y, chunkCoord.z, e.what());
            std::lock_guard<std::mutex> lock(m_genResultMutex);
            m_genFailed.push_back(chunkCoord);
        } catch (...) {
            LOG_ERROR("ChunkStreaming", "Generation worker: unknown exception at ({},{},{})",
                      chunkCoord.x, chunkCoord.y, chunkCoord.z);
            std::lock_guard<std::mutex> lock(m_genResultMutex);
            m_genFailed.push_back(chunkCoord);
        }
    }
}

void ChunkStreamingManager::drainGeneratedChunks(const glm::vec3& position, float unloadRadius) {
    std::vector<std::unique_ptr<Chunk>> ready;
    {
        std::lock_guard<std::mutex> lock(m_genResultMutex);
        // Failed builds: clear their pending mark so the coord is retried later.
        // (Boot-backlog coords are NOT retried — a pure-DB miss stays a miss.)
        for (const glm::ivec3& c : m_genFailed) {
            m_genPending.erase(c);
            m_bootResident.erase(c);
        }
        m_genFailed.clear();
        if (m_genResults.empty()) return;
        const size_t n = (m_maxChunksPerUpdate > 0)
                             ? std::min(m_genResults.size(), size_t(m_maxChunksPerUpdate))
                             : m_genResults.size();
        ready.assign(std::make_move_iterator(m_genResults.begin()),
                     std::make_move_iterator(m_genResults.begin() + n));
        m_genResults.erase(m_genResults.begin(), m_genResults.begin() + n);
    }

    for (auto& chunk : ready) {
        glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(chunk->getWorldOrigin());
        m_genPending.erase(chunkCoord);
        // Boot-backlog chunks become resident regardless of camera distance
        // (DB-only worlds keep full residency and never evict).
        const bool fromBootBacklog = m_bootResident.erase(chunkCoord) > 0;
        if (getChunkAtCoord(chunkCoord)) continue;  // superseded (e.g. DB-loaded meanwhile)
        glm::vec3 center = glm::vec3(chunk->getWorldOrigin()) + glm::vec3(16.0f);
        if (!fromBootBacklog && glm::length(center - position) > unloadRadius) continue;  // flew past it

        auto tsD0 = std::chrono::steady_clock::now();
        auto devices = m_getDevices();
        chunk->initialize(devices.first, devices.second);  // real callbacks; grid data survives
        chunk->createVulkanBuffer();

        auto& chunkMap = m_getChunkMap();
        auto& chunks = m_getChunks();
        chunkMap[chunkCoord] = chunk.get();
        chunks.push_back(std::move(chunk));

        if (m_finalizeGenerated) m_finalizeGenerated(*chunks.back(), chunkCoord);
        if (m_onChunkLoaded) m_onChunkLoaded(Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord));

        auto tsD1 = std::chrono::steady_clock::now();
        const double drainMs = std::chrono::duration<double, std::milli>(tsD1 - tsD0).count();
        if (drainMs > 60.0) {
            LOG_WARN_FMT("ChunkStreaming", "Slow async chunk finalize at (" << chunkCoord.x << ","
                         << chunkCoord.y << "," << chunkCoord.z << "): " << drainMs
                         << "ms (flora-heavy chunk?)");
        }
    }
}

void ChunkStreamingManager::setCallbacks(
    ChunkCreationFunc createChunkFunc,
    ChunkMapAccessFunc getChunkMapFunc,
    ChunkVectorAccessFunc getChunksFunc,
    DeviceAccessFunc getDevicesFunc
) {
    m_createChunk = createChunkFunc;
    m_getChunkMap = getChunkMapFunc;
    m_getChunks = getChunksFunc;
    m_getDevices = getDevicesFunc;
}

bool ChunkStreamingManager::initializeWorldStorage(const std::string& worldPath) {
    // The async worker reads from worldStorage — it must be gone before we swap it.
    stopAsyncGeneration();
    // Close any existing storage before opening a new one
    if (worldStorage) {
        worldStorage->close();
        delete worldStorage;
        worldStorage = nullptr;
    }
    worldStorage = new WorldStorage(worldPath);
    if (!worldStorage->initialize()) {
        LOG_ERROR_FMT("ChunkStreaming", "Failed to initialize world storage at: " << worldPath);
        delete worldStorage;
        worldStorage = nullptr;
        return false;
    }
    
    LOG_INFO_FMT("ChunkStreaming", "World storage initialized: " << worldPath);
    return true;
}

void ChunkStreamingManager::disconnectWorldStorage() {
    // The async worker reads from worldStorage — it must be gone before we delete it.
    stopAsyncGeneration();
    if (worldStorage) {
        worldStorage->close();
        delete worldStorage;
        worldStorage = nullptr;
        LOG_INFO("ChunkStreaming", "World storage disconnected");
    }
}

void ChunkStreamingManager::updateStreaming(const glm::vec3& playerPosition, float loadDistance, float unloadDistance) {
    if (!worldStorage) return;

    // Async generation (Phase 1c): start the worker lazily, then land finished chunks
    // (bounded per pump — Vulkan buffer + insert + grid registration only; terrain fill
    // + flora + maps + grid fill already happened off-thread).
    maybeStartGenWorker();
    auto tsP0 = std::chrono::steady_clock::now();
    if (asyncGenerationActive()) {
        drainGeneratedChunks(playerPosition, unloadDistance);
    }
    auto tsP1 = std::chrono::steady_clock::now();

    // Load chunks around player
    loadChunksAroundPosition(playerPosition, loadDistance);
    auto tsP2 = std::chrono::steady_clock::now();

    // Unload distant chunks
    unloadDistantChunks(playerPosition, unloadDistance);
    auto tsP3 = std::chrono::steady_clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    if (ms(tsP0, tsP3) > 100.0) {
        LOG_WARN_FMT("ChunkStreaming", "Slow pump: drain=" << ms(tsP0, tsP1)
                     << "ms load=" << ms(tsP1, tsP2) << "ms unload=" << ms(tsP2, tsP3) << "ms");
    }
}

void ChunkStreamingManager::pumpLanding(const glm::vec3& position, float unloadRadius) {
    if (!worldStorage) return;
    if (asyncGenerationActive()) drainGeneratedChunks(position, unloadRadius);
}

void ChunkStreamingManager::loadChunksAroundPosition(const glm::vec3& position, float radius) {
    glm::ivec3 centerChunk = Utils::CoordinateUtils::worldToChunkCoord(glm::ivec3(position));
    int chunkRadius = static_cast<int>(std::ceil(radius / 32.0f));
    // Vertical extent is clamped tighter than the horizontal radius: sideways range is
    // what vistas/walking need, but a full sphere at ground level spans ~2*radius+1
    // vertical bands, most of them fully-buried solid chunks (~200ms each to generate:
    // 32k per-cube heap fills) or empty sky. +-2 bands still covers terrain relief and
    // digging near the player (going deeper re-centers the sphere and streams further
    // down on demand). The SURFACE bands below cover what this clamp misses.
    int vRadius = std::min(chunkRadius, 2);

    // View-cone priority: a chunk straight ahead sorts as if ~half its distance, one
    // behind as ~1.8x — the ground the user is LOOKING AT resolves first, and refreshing
    // this priority every pump makes camera movement dynamically reprioritize the fill.
    const glm::vec3 viewDir = (glm::length(m_viewDir) > 0.01f)
                                  ? glm::normalize(m_viewDir) : glm::vec3(0, 0, -1);
    auto priority = [&](const glm::ivec3& chunkCoord) -> float {
        const glm::vec3 center =
            glm::vec3(Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord)) + glm::vec3(16.0f);
        const glm::vec3 to = center - position;
        const float dist = glm::length(to);
        if (dist < 1.0f) return 0.0f;
        const float cosA = glm::dot(to / dist, viewDir);
        return dist * (1.15f - 0.65f * cosA);
    };

    // Surface band per column, cached (terrain is static per world). INT_MIN = unknown.
    auto surfaceBandAt = [&](int wx, int wz) -> int {
        if (!m_surfaceBand) return INT_MIN;
        const glm::ivec2 col(wx >> 5, wz >> 5);   // cache per chunk column
        auto it = m_surfaceBandCache.find(col);
        if (it != m_surfaceBandCache.end()) return it->second;
        const int band = m_surfaceBand(wx, wz);
        m_surfaceBandCache.emplace(col, band);
        return band;
    };

    // Collect missing chunks within radius. Two vertical ranges per column: around the
    // CAMERA band (walking/digging), and around the column's TERRAIN SURFACE band. The
    // camera-only clamp meant an elevated camera (editor flight, zoom-in from altitude)
    // hovered above its own +-2 bands and the ground in plain view NEVER streamed in
    // (measured: 4 visible chunks, flat, for 72s at a y=150 hover over y~65 terrain).
    std::vector<std::pair<float, glm::ivec3>> missing;
    for (int dx = -chunkRadius; dx <= chunkRadius; ++dx) {
        for (int dz = -chunkRadius; dz <= chunkRadius; ++dz) {
            const int cx = centerChunk.x + dx, cz = centerChunk.z + dz;
            int lo[2] = {centerChunk.y - vRadius, 0};
            int hi[2] = {centerChunk.y + vRadius, -1};
            const int sb = surfaceBandAt(cx * 32 + 16, cz * 32 + 16);
            if (sb != INT_MIN) { lo[1] = sb - 1; hi[1] = sb + 1; }
            for (int range = 0; range < 2; ++range) {
                for (int cy = lo[range]; cy <= hi[range]; ++cy) {
                    if (range == 1 && cy >= lo[0] && cy <= hi[0]) continue;   // dedupe
                    const glm::ivec3 chunkCoord(cx, cy, cz);
                    const glm::vec3 chunkCenter =
                        glm::vec3(Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord)) +
                        glm::vec3(16.0f);
                    if (glm::length(chunkCenter - position) <= radius &&
                        !getChunkAtCoord(chunkCoord)) {
                        missing.emplace_back(priority(chunkCoord), chunkCoord);
                    }
                }
            }
        }
    }

    // Best priority first, so a capped update fills what the camera cares about most.
    std::sort(missing.begin(), missing.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    if (asyncGenerationActive()) {
        // Async path: EVERYTHING goes to the worker — it tries the (mutex-guarded) DB
        // load first, so saved/edited chunks are never shadowed by regeneration, and
        // falls back to generation on a miss. NO SQLite on the main thread here: sync
        // DB loads were ~150-400ms each (the moving-camera stall in saved regions).
        // Deep enough that the worker never idles between pumps: post-4.4, uniform
        // (buried/sky) chunks generate in ~1-5ms, so a shallow queue (was 8) starved
        // the worker for most of each pump interval and capped inflow at ~8/pump.
        // Landing is separately bounded (m_maxChunksPerUpdate in drainGeneratedChunks)
        // so a full queue can't hitch the main thread.
        constexpr int kMaxPendingAsync = 48;
        {
            // Dynamic reprioritization: requests still QUEUED from earlier pumps were
            // ordered for where the camera was then — re-sort them for where it is NOW
            // (<= kMaxPendingAsync entries; trivial under the lock). Without this a fast
            // pan keeps burning workers on chunks behind the camera.
            std::lock_guard<std::mutex> lock(m_genRequestMutex);
            std::sort(m_genRequests.begin(), m_genRequests.end(),
                      [&](const glm::ivec3& a, const glm::ivec3& b) {
                          return priority(a) < priority(b);
                      });
        }
        for (const auto& [prio, chunkCoord] : missing) {
            if (m_genPending.count(chunkCoord)) continue;
            if (int(m_genPending.size()) >= kMaxPendingAsync) break;
            m_genPending.insert(chunkCoord);
            {
                std::lock_guard<std::mutex> lock(m_genRequestMutex);
                m_genRequests.push_back(chunkCoord);
            }
            m_genCv.notify_one();
        }
        return;
    }

    int generated = 0;
    for (const auto& [distance, chunkCoord] : missing) {
        // Bound per-call work so one frame's pump can't generate a whole sphere and hitch.
        if (m_maxChunksPerUpdate > 0 && generated >= m_maxChunksPerUpdate) break;
        generateOrLoadChunk(chunkCoord);
        ++generated;
    }
}

void ChunkStreamingManager::unloadDistantChunks(const glm::vec3& position, float radius) {
    auto& chunks = m_getChunks();
    auto& chunkMap = m_getChunkMap();

    // Frame-deferred deletion: destroy chunks evicted on the PREVIOUS pump now. The pump is
    // throttled to once every several render frames (>= frames-in-flight), so by the time we
    // get here the GPU has finished with those chunks' Vulkan buffers — freeing them is safe.
    // Erasing them inline last pump instead would be a use-after-free race against an
    // in-flight frame (intermittent device-lost crash). Stall-free, unlike vkDeviceWaitIdle.
    if (!m_pendingDeletion.empty()) {
        LOG_TRACE_FMT("ChunkStreaming", "Freeing " << m_pendingDeletion.size() << " deferred chunk(s)");
        // Destroying a solid chunk frees ~64k+ heap blocks (32k Cube objects + voxel-map
        // nodes) — hundreds of ms PER CHUNK on the Debug CRT heap, and it was the last
        // main-thread hitch in the pump. Do only the thread-bound teardown here (Vulkan
        // buffer free + occupancy-grid unregister — both idempotent, handles nulled),
        // then hand the husks to the OWNED disposal worker (joinable — a detached
        // thread still freeing during CRT teardown dies silently with no crash dump).
        // If the disposal worker isn't running (sync mode), free inline as before.
        for (auto& chunk : m_pendingDeletion) {
            if (!chunk) continue;
            chunk->cleanupVulkanResources();   // frees + nulls buffers → dtor re-run no-ops
            chunk->cleanupPhysicsResources();  // unregisters the occupancy grid
            chunk->setPhysicsWorld(nullptr);   // dtor's physics cleanup becomes a no-op
        }
        if (m_disposalThread.joinable()) {
            {
                std::lock_guard<std::mutex> lock(m_disposalMutex);
                for (auto& chunk : m_pendingDeletion)
                    m_disposalQueue.push_back(std::move(chunk));
            }
            m_disposalCv.notify_one();
        }
        m_pendingDeletion.clear();
    }

    auto tsU0 = std::chrono::steady_clock::now();
    int evicted = 0, saved = 0;
    double saveMs = 0.0;
    auto it = chunks.begin();
    while (it != chunks.end()) {
        glm::vec3 chunkCenter = glm::vec3((*it)->getWorldOrigin()) + glm::vec3(16.0f);
        float distance = glm::length(chunkCenter - position);

        // Bound evictions per pump (same cap as generation) so churn stays smooth; distant
        // stragglers are cleaned up over the next few pumps.
        bool capReached = (m_maxChunksPerUpdate > 0 && evicted >= m_maxChunksPerUpdate);
        if (distance > radius && !capReached) {
            // Save before unloading — but ONLY chunks with unsaved modifications.
            // Pristine chunks (deterministically generated, or loaded-and-untouched)
            // regenerate/reload identically; unconditionally saving every evicted
            // chunk was a synchronous SQLite write per chunk per pump (frame hitches)
            // and grew the world DB with every flight.
            if (worldStorage && (*it)->getIsDirty()) {
                auto tsS0 = std::chrono::steady_clock::now();
                saveChunk(it->get());
                const double oneSaveMs = std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - tsS0).count();
                saveMs += oneSaveMs;
                ++saved;
                if (oneSaveMs > 100.0) {
                    glm::ivec3 cc = Utils::CoordinateUtils::worldToChunkCoord((*it)->getWorldOrigin());
                    LOG_WARN_FMT("ChunkStreaming", "Dirty evict save (" << cc.x << "," << cc.y << ","
                                 << cc.z << ") took " << oneSaveMs << "ms");
                }
            }

            // Last look at a chunk that may never have touched the DB: the far-LOD handoff
            // (EvictedLodCache) builds its coarse representation here, while voxels and
            // sub/microcubes are still intact.
            if (m_onChunkEvicted) m_onChunkEvicted(**it);

            // Stop rendering/looking it up this frame, but defer the actual destruction
            // (Vulkan free + grid unregister) to next pump so no in-flight frame uses it.
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord((*it)->getWorldOrigin());
            chunkMap.erase(chunkCoord);
            m_pendingDeletion.push_back(std::move(*it));
            it = chunks.erase(it);
            ++evicted;
            LOG_TRACE_FMT("ChunkStreaming", "Unloaded distant chunk at: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z);
        } else {
            ++it;
        }
    }
    const double loopMs = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - tsU0).count();
    if (loopMs > 100.0) {
        LOG_WARN_FMT("ChunkStreaming", "Slow evict loop: total=" << loopMs << "ms evicted=" << evicted
                     << " savedDirty=" << saved << " saveMs=" << saveMs);
    }
}

bool ChunkStreamingManager::saveChunk(Chunk* chunk) {
    if (!worldStorage || !chunk) return false;
    // Serialize against the async worker's off-thread DB loads.
    std::lock_guard<std::mutex> lock(m_storageMutex);
    return worldStorage->saveChunk(*chunk);
}

bool ChunkStreamingManager::saveAllChunks() {
    if (!worldStorage) return false;

    auto& chunks = m_getChunks();
    bool allSuccess = true;
    std::lock_guard<std::mutex> lock(m_storageMutex);  // serialize vs worker DB loads
    for (const auto& chunk : chunks) {
        if (!worldStorage->saveChunk(*chunk)) {
            allSuccess = false;
        }
    }

    LOG_INFO_FMT("ChunkStreaming", "Saved " << chunks.size() << " chunks to storage");
    return allSuccess;
}

bool ChunkStreamingManager::saveDirtyChunks() {
    if (!worldStorage) return false;
    
    auto& chunks = m_getChunks();
    
    // Build vector of chunk references for dirty saving
    std::vector<std::reference_wrapper<Chunk>> chunkRefs;
    chunkRefs.reserve(chunks.size());
    
    int dirtyCount = 0;
    for (const auto& chunk : chunks) {
        chunkRefs.emplace_back(*chunk);
        if (chunk->getIsDirty()) {
            dirtyCount++;
            glm::ivec3 origin = chunk->getWorldOrigin();
            LOG_DEBUG_FMT("ChunkStreaming", "Found dirty chunk at (" << origin.x << "," << origin.y << "," << origin.z << ") pending save");
        }
    }
    
    if (dirtyCount > 0) {
        LOG_INFO_FMT("ChunkStreaming", "Attempting to save " << dirtyCount << " dirty chunks");
    }

    std::lock_guard<std::mutex> lock(m_storageMutex);  // serialize vs worker DB loads
    return worldStorage->saveDirtyChunks(chunkRefs);
}

bool ChunkStreamingManager::loadChunk(const glm::ivec3& chunkCoord) {
    if (!worldStorage) return false;
    
    // Don't load if chunk already exists
    if (getChunkAtCoord(chunkCoord)) {
        return true;
    }
    
    auto devices = m_getDevices();
    VkDevice device = devices.first;
    VkPhysicalDevice physicalDevice = devices.second;
    
    glm::ivec3 origin = Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    auto chunk = std::make_unique<Chunk>(origin);
    chunk->initialize(device, physicalDevice);
    
    // Initialize chunk for sparse loading from database
    chunk->initializeForLoading();

    bool loaded;
    {
        std::lock_guard<std::mutex> lock(m_storageMutex);  // serialize vs worker DB loads
        loaded = worldStorage->loadChunk(chunkCoord, *chunk);
    }
    if (loaded) {
        // Successfully loaded from storage - mark as clean since it's from database
        chunk->markClean();
        
        // DON'T rebuild faces yet - wait until all chunks are loaded
        chunk->createVulkanBuffer();
        
        // Add to map and vector
        auto& chunkMap = m_getChunkMap();
        auto& chunks = m_getChunks();
        chunkMap[chunkCoord] = chunk.get();
        glm::ivec3 origin = chunk->getWorldOrigin();
        chunks.push_back(std::move(chunk));
        // Finalize chunks streamed in from the DB at runtime (collision + faces). Gated off
        // during bulk load (buildAllChunkPhysics + rebuildAllChunkFaces handle that pass).
        if (m_perChunkPhysics && m_onChunkStreamedIn) m_onChunkStreamedIn(*chunks.back());
        if (m_onChunkLoaded) m_onChunkLoaded(origin);

        LOG_DEBUG_FMT("ChunkStreaming", "Loaded chunk from storage: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z);
        return true;
    }

    return false;
}

std::vector<glm::ivec3> ChunkStreamingManager::loadAllChunksFromDatabase() {
    std::vector<glm::ivec3> loadedChunks;
    
    if (!worldStorage) {
        LOG_WARN("ChunkStreaming", "No world storage available - cannot load chunks from database");
        return loadedChunks;
    }
    
    // Get all chunk coordinates from database
    std::vector<glm::ivec3> chunkCoords = worldStorage->getAllChunkCoordinates();
    
    if (chunkCoords.empty()) {
        LOG_INFO("ChunkStreaming", "No chunks found in database");
        return loadedChunks;
    }
    
    LOG_INFO_FMT("ChunkStreaming", "Loading " << chunkCoords.size() << " chunks from database...");
    
    // Load each chunk
    for (const auto& coord : chunkCoords) {
        if (loadChunk(coord)) {
            loadedChunks.push_back(coord);
        } else {
            LOG_WARN_FMT("ChunkStreaming", "Failed to load chunk (" << coord.x << "," << coord.y << "," << coord.z << ") from database");
        }
    }
    
    LOG_INFO_FMT("ChunkStreaming", "Successfully loaded " << loadedChunks.size() << " chunks from database");
    return loadedChunks;
}

std::vector<glm::ivec3> ChunkStreamingManager::loadChunksNearAndDeferRest(
    const glm::vec3& anchor, float nearRadius, bool deferRest) {
    std::vector<glm::ivec3> nearLoaded;
    if (!worldStorage) {
        LOG_WARN("ChunkStreaming", "No world storage available - cannot load chunks from database");
        return nearLoaded;
    }

    std::vector<glm::ivec3> chunkCoords = worldStorage->getAllChunkCoordinates();
    if (chunkCoords.empty()) {
        LOG_INFO("ChunkStreaming", "No chunks found in database");
        return nearLoaded;
    }

    // Sort every DB coord by distance to the boot anchor, then split at nearRadius.
    std::vector<std::pair<float, glm::ivec3>> byDistance;
    byDistance.reserve(chunkCoords.size());
    for (const auto& coord : chunkCoords) {
        glm::vec3 center = glm::vec3(Utils::CoordinateUtils::chunkCoordToOrigin(coord)) + glm::vec3(16.0f);
        byDistance.emplace_back(glm::length(center - anchor), coord);
    }
    std::sort(byDistance.begin(), byDistance.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    m_dbBacklog.clear();
    m_bootResident.clear();
    size_t misses = 0;
    for (const auto& [distance, coord] : byDistance) {
        if (distance <= nearRadius) {
            // Synchronous near-set load: spawn area must be solid before first frame.
            if (loadChunk(coord)) nearLoaded.push_back(coord);
            else ++misses;  // empty chunk record — same as the old load-all path
        } else if (deferRest) {
            m_dbBacklog.push_back(coord);  // already nearest-first
        }
    }
    m_dbWorkerMode = !m_dbBacklog.empty();

    LOG_INFO_FMT("ChunkStreaming", "Stream-in boot: " << nearLoaded.size() << " chunk(s) loaded near anchor ("
                 << misses << " empty), " << m_dbBacklog.size() << " deferred to background"
                 << (deferRest ? "" : " [streaming world: far chunks load on approach]"));
    return nearLoaded;
}

void ChunkStreamingManager::pumpDeferredDbLoads(const glm::vec3& position) {
    if (m_dbBacklog.empty() && m_bootResident.empty()) return;
    if (!worldStorage) { m_dbBacklog.clear(); m_bootResident.clear(); return; }

    maybeStartGenWorker();
    if (!asyncGenerationActive()) return;  // worker failed to start; retry next frame

    // Feed the worker a bounded number of backlog coords at a time so camera-driven
    // requests (streaming worlds) never queue behind hundreds of boot loads.
    constexpr int kMaxBootInFlight = 4;
    int inFlight = static_cast<int>(m_bootResident.size());
    while (!m_dbBacklog.empty() && inFlight < kMaxBootInFlight) {
        glm::ivec3 coord = m_dbBacklog.front();
        m_dbBacklog.pop_front();
        if (getChunkAtCoord(coord) || m_genPending.count(coord)) continue;
        m_genPending.insert(coord);
        m_bootResident.insert(coord);
        {
            std::lock_guard<std::mutex> lock(m_genRequestMutex);
            m_genRequests.push_back(coord);
        }
        m_genCv.notify_one();
        ++inFlight;
    }

    // Land finished chunks. Backlog chunks bypass the distance drop (see
    // drainGeneratedChunks); radius only applies to any interleaved camera requests.
    drainGeneratedChunks(position, std::numeric_limits<float>::max());

    if (m_dbBacklog.empty() && m_bootResident.empty()) {
        LOG_INFO("ChunkStreaming", "Stream-in boot complete: deferred background load finished");
    }
}

bool ChunkStreamingManager::generateOrLoadChunk(const glm::ivec3& chunkCoord) {
    if (!worldStorage) {
        // Fallback: create empty chunk via callback
        m_createChunk(Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord));
        return true;
    }
    
    // Try to load from storage first
    if (loadChunk(chunkCoord)) {
        return true;
    }
    
    // If not in storage, generate new chunk
    auto devices = m_getDevices();
    VkDevice device = devices.first;
    VkPhysicalDevice physicalDevice = devices.second;

    auto tsG0 = std::chrono::steady_clock::now();
    glm::ivec3 origin = Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    auto chunk = std::make_unique<Chunk>(origin);
    chunk->initialize(device, physicalDevice);
    auto tsG1 = std::chrono::steady_clock::now();

    // Fill the chunk: use the configured world generator if one is wired (the Phase-1
    // generation wire), otherwise fall back to the legacy random fill.
    if (m_generateChunk) {
        m_generateChunk(*chunk, chunkCoord);
    } else {
        chunk->populateWithCubes();
    }
    auto tsG2 = std::chrono::steady_clock::now();

    // DON'T rebuild faces yet - wait until all chunks are loaded
    chunk->createVulkanBuffer();
    auto tsG3 = std::chrono::steady_clock::now();

    // Add to map and vector
    auto& chunkMap = m_getChunkMap();
    auto& chunks = m_getChunks();
    chunkMap[chunkCoord] = chunk.get();
    glm::ivec3 genOrigin = Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    chunks.push_back(std::move(chunk));
    // Freshly generated terrain is PRISTINE: generation is deterministic (seed + recipe,
    // persisted in world_meta), so it regenerates bit-identically on demand — persisting
    // it would only bloat the DB and stall the pump (a chunk save is a synchronous
    // SQLite write of up to 32k voxels; saving every generated + evicted chunk was the
    // 1-2.5s frame hitch while flying). addCube set the dirty flag during generation;
    // clear it so only chunks the player actually EDITS get saved (on evict/save_world).
    chunks.back()->markClean();
    // Finalize the freshly streamed-in chunk (collision + faces) so characters don't fall
    // through and it renders correctly. Gated so the initial bulk load (which calls
    // buildAllChunkPhysics + rebuildAllChunkFaces afterward) doesn't double-process.
    auto tsG4 = std::chrono::steady_clock::now();
    if (m_perChunkPhysics && m_onChunkStreamedIn) m_onChunkStreamedIn(*chunks.back());
    auto tsG5 = std::chrono::steady_clock::now();
    if (m_onChunkLoaded) m_onChunkLoaded(genOrigin);
    auto tsG6 = std::chrono::steady_clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    if (ms(tsG0, tsG6) > 60.0) {
        LOG_WARN_FMT("ChunkStreaming", "Slow chunk stream-in at (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
                     << "): ctor+init=" << ms(tsG0, tsG1) << "ms gen=" << ms(tsG1, tsG2)
                     << "ms vkbuf=" << ms(tsG2, tsG3) << "ms finalize=" << ms(tsG4, tsG5)
                     << "ms onLoaded=" << ms(tsG5, tsG6) << "ms");
    }

    LOG_DEBUG_FMT("ChunkStreaming", "Generated new chunk: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z);
    return true;
}

Chunk* ChunkStreamingManager::getChunkAtCoord(const glm::ivec3& chunkCoord) {
    auto& chunkMap = m_getChunkMap();
    auto it = chunkMap.find(chunkCoord);
    if (it != chunkMap.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace Phyxel
