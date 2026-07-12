#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "graphics/ChunkUpdatePerf.h"   // B0 diagnostic timers (docs/ChunkUpdateHitchPlan.md)
#include <chrono>
#include "core/WorldStorage.h"
#include "core/GpuParticlePhysics.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <stdexcept>
#include <cstring>
#include <random>
#include <iostream>
#include <algorithm>  // for std::find
#include <set>        // for std::set in selective updates

namespace Phyxel {

ChunkManager::~ChunkManager() {
    cleanup();
    // ChunkStreamingManager destructor will handle worldStorage cleanup
}

void ChunkManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
    
    // Setup streaming manager callbacks
    m_streamingManager.setCallbacks(
        // ChunkCreationFunc: Create chunk via existing createChunk method
        [this](const glm::ivec3& origin) { createChunk(origin); },
        // ChunkMapAccessFunc: Access chunk spatial hash map
        [this]() -> auto& { return chunkMap; },
        // ChunkVectorAccessFunc: Access chunk vector
        [this]() -> auto& { return chunks; },
        // DeviceAccessFunc: Get Vulkan device handles
        [this]() { return std::make_pair(device, physicalDevice); }
    );
    // Occupancy grid: update all voxels in a 32³ chunk whenever it is streamed in at runtime
    m_streamingManager.setOnChunkLoaded([this](const glm::ivec3& origin) {
        syncChunkToOccupancy(origin);
    });
    // Phase 1 — generation wire: streamed chunks are filled by the configured world
    // generator (when enabled) instead of the legacy random fill.
    m_streamingManager.setGenerationCallback([this](Chunk& chunk, const glm::ivec3& chunkCoord) {
        if (m_streamingGenerationEnabled && m_worldGenerator) {
            auto t0 = std::chrono::steady_clock::now();
            m_worldGenerator->generateChunk(chunk, chunkCoord);
            auto t1 = std::chrono::steady_clock::now();
            // Decorate the freshly generated terrain with biome flora (clipped to this chunk).
            // Only newly generated chunks are decorated — DB-loaded chunks keep their saved
            // state. The decorator is wired by the editor (it owns the template manager).
            if (m_floraDecorator) m_floraDecorator(chunk, chunkCoord);
            auto t2 = std::chrono::steady_clock::now();
            const double genMs   = std::chrono::duration<double, std::milli>(t1 - t0).count();
            const double floraMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
            if (genMs + floraMs > 40.0) {
                LOG_WARN("ChunkStreaming", "Slow streamed generation at ({},{},{}): terrain={}ms flora={}ms",
                         chunkCoord.x, chunkCoord.y, chunkCoord.z, genMs, floraMs);
            }
        } else {
            chunk.populateWithCubes();
        }
    });
    // Finalize a chunk streamed in at runtime: (1) register static collision — mirrors
    // buildAllChunkPhysics() but for a single chunk; (2) build its faces with cross-chunk
    // culling and re-cull its neighbours — the streaming load path only creates the Vulkan
    // buffer, so this is the per-chunk counterpart to rebuildAllChunkFaces(). Eviction
    // teardown is automatic (the Chunk destructor unregisters its grid and frees its buffer).
    m_streamingManager.setOnChunkStreamedIn([this](Chunk& chunk) {
        // Air chunks occlude nothing, collide with nothing and mesh to nothing — skip
        // finalize entirely. Most streamed chunks at flight altitude ARE pure air (the
        // load sphere spans ~10 vertical chunk bands, terrain occupies ~2), so this
        // cuts the remesh flood (and its ~50ms-per-chunk drain frames) by that factor.
        if (!chunk.hasAnySolidVoxel()) return;
        if (physicsWorld) {
            chunk.setPhysicsWorld(physicsWorld);
            chunk.createChunkPhysicsBody();
        }
        // Build the O(1) voxel hash maps so hover/raycast (the Properties panel) resolves
        // voxels in this chunk. DB-loaded streamed chunks don't get the bulk
        // initializeAllChunkVoxelMaps() pass, so they'd otherwise be invisible to mouse-over.
        chunk.initializeVoxelMaps();
        // Meshing (self + the 26 neighbours whose faces toward this chunk changed) goes
        // through the budgeted DirtyChunkTracker instead of running synchronously here.
        // The synchronous form did up to 27 full remeshes (skylight + blocklight BFS each)
        // PER STREAMED CHUNK per pump = multi-second frame hitches while flying. The
        // dirty queue dedupes shared neighbours and spreads the same work across frames
        // (kDirtyChunkBudgetMs); chunks pop in a few frames later instead of stalling the
        // frame. Physics + voxel maps above stay synchronous (cheap, and collision must
        // exist the moment the chunk does). markChunkForRemesh, NOT markChunkDirty: only
        // the render mesh is stale — the DB-dirty flag would make eviction re-save every
        // touched neighbour to SQLite (mass save stalls). Freshly GENERATED chunks are
        // already DB-dirty via addCube, so persistence is unaffected.
        markChunkForRemesh(&chunk);
        // Only the 6 FACE-adjacent neighbours: cross-chunk culling and border-light
        // seeding both sample the 6 face directions only, so diagonal/corner neighbours
        // are unaffected by this chunk's arrival (the old 26-neighbour sweep quadrupled
        // the remesh queue for nothing). IDLE tier: a neighbour re-cull only removes
        // now-hidden boundary faces + refreshes border light — cosmetic, never holes —
        // so it waits for a quiet frame instead of competing with must-have meshes
        // (each full remesh is ~50ms in Debug; 7 per streamed chunk was the moving-
        // camera FPS drop).
        static const glm::ivec3 kFaceDirs[6] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        glm::ivec3 cc = Utils::CoordinateUtils::worldToChunkCoord(chunk.getWorldOrigin());
        for (const glm::ivec3& d : kFaceDirs) {
            if (Chunk* adj = getChunkAtCoord(cc + d))
                markChunkForRemeshIdle(adj);
        }
    });

    // Async worker wiring (generation snapshot + main-thread finalize). Wired
    // UNCONDITIONALLY: streaming worlds get a generator via the snapshot; DB-only
    // worlds get a null snapshot and the worker runs in pure DB-load mode for the
    // stream-in boot backlog (docs/LargeWorldScalePlan.md Phase 2). The finalize
    // (grid registration + remesh marks) is world-type-independent.
    m_streamingManager.setAsyncGeneration(
        [this]() -> std::unique_ptr<WorldGenerator> {
            if (!m_streamingGenerationEnabled || !m_worldGenerator) return nullptr;
            // Snapshot AFTER the loader applied recipe/params (lazy: first pump).
            return std::make_unique<WorldGenerator>(*m_worldGenerator);
        },
        [this](Chunk& chunk, const glm::ivec3& chunkCoord) {
            // Flora normally runs on the WORKER (setWorkerFloraDecorator); the
            // main-thread decorator is only a fallback when no worker decorator is
            // wired — and only for freshly GENERATED chunks (still DB-dirty from
            // addCube). DB-loaded chunks were saved WITH their flora and arrive
            // markClean'd; decorating them again would double the flora.
            const bool wasGenerated = chunk.getIsDirty();
            if (wasGenerated && !m_hasWorkerFlora && m_floraDecorator) m_floraDecorator(chunk, chunkCoord);
            // Mark pristine: generated terrain + flora regenerate deterministically,
            // so only player edits should hit the DB.
            chunk.markClean();
            if (!chunk.hasAnySolidVoxel()) return;  // pure air: nothing to collide/mesh
            if (physicsWorld) {
                chunk.setPhysicsWorld(physicsWorld);
                chunk.registerPrebuiltPhysics();
            }
            markChunkForRemesh(&chunk);
            // Neighbour re-culls are cosmetic (overdraw + border light, never
            // holes) — idle tier, so they only run in quiet frames.
            static const glm::ivec3 kFaceDirs2[6] = {
                {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            glm::ivec3 fc = Utils::CoordinateUtils::worldToChunkCoord(chunk.getWorldOrigin());
            for (const glm::ivec3& d : kFaceDirs2) {
                if (Chunk* adj = getChunkAtCoord(fc + d))
                    markChunkForRemeshIdle(adj);
            }
        });

    // Setup dynamic object manager callbacks
    m_dynamicObjectManager.setCallbacks(
        // PhysicsWorldAccessFunc: Access physics world
        [this]() { return physicsWorld; },
        // DynamicSubcubeVectorAccessFunc: Access subcube vector
        [this]() -> auto& { return globalDynamicSubcubes; },
        // DynamicCubeVectorAccessFunc: Access cube vector
        [this]() -> auto& { return globalDynamicCubes; },
        // DynamicMicrocubeVectorAccessFunc: Access microcube vector
        [this]() -> auto& { return globalDynamicMicrocubes; },
        // RebuildFacesFunc: Rebuild faces when objects change
        [this]() { rebuildGlobalDynamicFaces(); },
        // VoxelQuerySystem: For debris collision
        &m_voxelQuerySystem
    );
    
    // Setup face update coordinator callbacks
    m_faceUpdateCoordinator.setCallbacks(
        // DynamicSubcubeVectorAccessFunc: Access subcube vector
        [this]() -> auto& { return globalDynamicSubcubes; },
        // DynamicCubeVectorAccessFunc: Access cube vector
        [this]() -> auto& { return globalDynamicCubes; },
        // DynamicMicrocubeVectorAccessFunc: Access microcube vector
        [this]() -> auto& { return globalDynamicMicrocubes; },
        // FaceDataAccessFunc: Access face data
        [this]() -> auto& { return globalDynamicSubcubeFaces; },
        // ChunkLookupFunc: Get chunk at position
        [this](const glm::ivec3& pos) { return getChunkAt(pos); },
        // MarkChunkDirtyFunc: Mark chunk dirty
        [this](Chunk* chunk) { markChunkDirty(chunk); }
    );
    
    // Setup chunk initializer callbacks
    m_chunkInitializer.setCallbacks(
        // ChunkVectorAccessFunc: Access chunk vector
        [this]() -> auto& { return chunks; },
        // ChunkMapAccessFunc: Access chunk map
        [this]() -> auto& { return chunkMap; },
        // DeviceAccessFunc: Get Vulkan device handles
        [this]() { return std::make_pair(device, physicalDevice); },
        // GetChunkAtCoordFunc: Get chunk at coordinate
        [this](const glm::ivec3& coord) { return getChunkAtCoord(coord); },
        // RebuildChunkWithCullingFunc: Rebuild chunk with cross-chunk culling
        [this](Chunk& chunk) { rebuildChunkFacesWithCrosschunkCulling(chunk); },
        // PhysicsWorldAccessFunc: Access physics world
        [this]() { return physicsWorld; }
    );
    
    // Setup dirty chunk tracker callbacks
    m_dirtyChunkTracker.setCallbacks(
        // ChunkVectorAccessFunc: Access chunk vector
        [this]() -> auto& { return chunks; },
        // UpdateChunkFunc: Update single chunk
        [this](size_t index) { updateChunk(index); },
        // GetChunkIndexFunc: Get chunk index from pointer
        [this](Chunk* chunk) { return getChunkIndex(chunk); }
    );
    
    // Configure voxel query system callbacks
    m_voxelQuerySystem.setCallbacks(
        // ChunkMapAccessFunc: Access chunk map
        [this]() -> auto& { return chunkMap; },
        // ChunkVectorAccessFunc: Access chunk vector
        [this]() -> auto& { return chunks; }
    );
    
    // Configure voxel modification system callbacks
    m_voxelModificationSystem.setCallbacks(
        // GetChunkFunc: Get chunk at world position
        [this](const glm::ivec3& worldPos) { return m_voxelQuerySystem.getChunkAtFast(worldPos); },
        // MarkChunkDirtyFunc: Mark chunk for GPU update
        [this](Chunk* chunk) { markChunkDirty(chunk); },
        // UpdateAfterCubeBreakFunc: Update faces after cube removal
        [this](const glm::ivec3& worldPos) { updateAfterCubeBreak(worldPos); },
        // UpdateAfterCubePlaceFunc: Update faces after cube placement
        [this](const glm::ivec3& worldPos) { updateAfterCubePlace(worldPos); }
    );
}

void ChunkManager::setPhysicsWorld(Physics::PhysicsWorld* physics) {
    physicsWorld = physics;
    LOG_INFO("Chunk", "Physics world set for proper dynamic object cleanup");
}

bool ChunkManager::initializeWorldStorage(const std::string& worldPath) {
    return m_streamingManager.initializeWorldStorage(worldPath);
}

void ChunkManager::disconnectWorldStorage() {
    m_streamingManager.disconnectWorldStorage();
}

void ChunkManager::configureStreamingGeneration(bool enabled, WorldGenerator::GenerationType type, uint32_t seed) {
    // Reconfiguring (world switch): stop any running async worker so its private
    // generator snapshot can't outlive the world it belongs to.
    m_streamingManager.stopAsyncGeneration();
    m_streamingGenerationEnabled = enabled;
    if (enabled) {
        m_worldGenerator = std::make_unique<WorldGenerator>(type, seed);
    }
    // When streaming generation is on, chunks finalize (collision + faces) as they stream
    // in/out at runtime; off otherwise so bulk DB loads aren't double-processed.
    m_streamingManager.setPerChunkPhysics(enabled);
    // Bound per-pump main-thread work (nearest-first): with async generation this caps
    // synchronous DB loads + finished-chunk finalizes per pump, not generation itself
    // (that runs on the worker).
    m_streamingManager.setMaxChunksPerUpdate(enabled ? 2 : 0);

    // Async worker wiring (snapshot + finalize) is set once in setupCallbacks();
    // the snapshot lambda returns null while streaming generation is disabled.
    LOG_INFO_FMT("Chunk", "Streaming generation " << (enabled ? "ENABLED" : "disabled")
                 << " (type=" << static_cast<int>(type) << ", seed=" << seed
                 << ", asyncGen=" << (enabled ? 1 : 0) << ")");
}

void ChunkManager::updateChunkStreaming() {
    m_streamingManager.updateStreaming(playerPosition, loadDistance, unloadDistance);
}

void ChunkManager::loadChunksAroundPosition(const glm::vec3& position, float radius) {
    m_streamingManager.loadChunksAroundPosition(position, radius);
}

void ChunkManager::unloadDistantChunks(const glm::vec3& position, float radius) {
    m_streamingManager.unloadDistantChunks(position, radius);
}

bool ChunkManager::saveChunk(Chunk* chunk) {
    return m_streamingManager.saveChunk(chunk);
}

bool ChunkManager::saveAllChunks() {
    return m_streamingManager.saveAllChunks();
}

bool ChunkManager::saveDirtyChunks() {
    return m_streamingManager.saveDirtyChunks();
}

bool ChunkManager::loadChunk(const glm::ivec3& chunkCoord) {
    return m_streamingManager.loadChunk(chunkCoord);
}

std::vector<glm::ivec3> ChunkManager::loadAllChunksFromDatabase() {
    return m_streamingManager.loadAllChunksFromDatabase();
}

std::vector<glm::ivec3> ChunkManager::loadChunksNearAndDeferRest(const glm::vec3& anchor) {
    // Streaming worlds don't defer: far DB chunks load on approach via the pump.
    return m_streamingManager.loadChunksNearAndDeferRest(anchor, loadDistance,
                                                         !m_streamingGenerationEnabled);
}

void ChunkManager::pumpDeferredDbLoads(const glm::vec3& position) {
    m_streamingManager.pumpDeferredDbLoads(position);
}

bool ChunkManager::hasDeferredDbLoads() const {
    return m_streamingManager.hasDeferredDbLoads();
}

bool ChunkManager::generateOrLoadChunk(const glm::ivec3& chunkCoord) {
    return m_streamingManager.generateOrLoadChunk(chunkCoord);
}

void ChunkManager::rebuildAllChunkFaces() {
    m_chunkInitializer.rebuildAllChunkFaces();
}

void ChunkManager::rebuildAllChunkLighting() {
    // Force every chunk through the proper cross-chunk bake (rebuildChunkFacesWithCrosschunkCulling
    // + GPU upload), e.g. after toggling smooth lighting. Mark all dirty, then drain a few times so
    // cross-chunk light bleed converges.
    for (size_t i = 0; i < chunks.size(); ++i) {
        chunks[i]->setNeedsUpdate(true);
        m_dirtyChunkTracker.markChunkDirty(i);
    }
    for (int pass = 0; pass < 4 && m_dirtyChunkTracker.hasDirty(); ++pass) {
        m_dirtyChunkTracker.updateDirtyChunks();  // unlimited budget: drain the whole list
    }
}

void ChunkManager::buildAllChunkPhysics() {
    m_chunkInitializer.buildAllChunkPhysics();
}

void ChunkManager::buildChunkPhysicsInRegion(const glm::ivec3& minWorld, const glm::ivec3& maxWorld) {
    if (!physicsWorld) return;
    // floor-divide world cube -> chunk coord (negative-safe)
    auto fd = [](int a) { int q = a / 32, r = a % 32; if (r != 0 && (r < 0)) --q; return q; };
    const glm::ivec3 cMin(fd(minWorld.x), fd(minWorld.y), fd(minWorld.z));
    const glm::ivec3 cMax(fd(maxWorld.x), fd(maxWorld.y), fd(maxWorld.z));
    int n = 0;
    for (auto& [coord, chunk] : chunkMap) {
        if (!chunk) continue;
        if (coord.x < cMin.x || coord.x > cMax.x || coord.y < cMin.y || coord.y > cMax.y ||
            coord.z < cMin.z || coord.z > cMax.z)
            continue;
        chunk->setPhysicsWorld(physicsWorld);
        chunk->createChunkPhysicsBody();   // same per-chunk registration the streaming path uses
        ++n;
    }
    LOG_DEBUG_FMT("Chunk", "regional physics rebuild: " << n << " chunks in ["
                  << cMin.x << "," << cMin.y << "," << cMin.z << "]..[" << cMax.x << ","
                  << cMax.y << "," << cMax.z << "]");
}

void ChunkManager::initializeAllChunkVoxelMaps() {
    m_chunkInitializer.initializeAllChunkVoxelMaps();
}

void ChunkManager::createChunks(const std::vector<glm::ivec3>& origins) {
    m_chunkInitializer.createChunks(origins);
}

void ChunkManager::createChunk(const glm::ivec3& origin, bool populate) {
    m_chunkInitializer.createChunk(origin, populate);
}

void ChunkManager::updateChunk(size_t chunkIndex) {
    if (chunkIndex >= chunks.size()) return;
    
    Chunk* chunk = chunks[chunkIndex].get();
    if (chunk->getNeedsUpdate()) {
        LOG_TRACE_FMT("Chunk", "Updating chunk " << chunkIndex << " with " << chunk->getTotalSubcubeCount() << " subcubes");

        // Use cross-chunk culling method to maintain proper face occlusion across chunk boundaries
        rebuildChunkFacesWithCrosschunkCulling(*chunk);
        // Update GPU buffer with new face data
        chunk->updateVulkanBuffer();
        chunk->setNeedsUpdate(false);
    }
}

void ChunkManager::updateDirtyChunks() {
    m_dirtyChunkTracker.updateDirtyChunks();
}

void ChunkManager::updateDirtyChunks(double budgetMs) {
    // B0: time the whole per-frame chunk-update phase ONLY when it actually does work, so avg/max
    // reflect real update frames (not the mostly-idle frames). See docs/ChunkUpdateHitchPlan.md.
    if (m_dirtyChunkTracker.hasDirty()) {
        Graphics::ScopedChunkPerf _perf(Graphics::ChunkPerfPhase::DirtyUpdateTotal);
        m_dirtyChunkTracker.updateDirtyChunks(budgetMs);
    } else {
        m_dirtyChunkTracker.updateDirtyChunks(budgetMs);
    }
}

void ChunkManager::updateAllChunks() {
    // DEPRECATED: This method is inefficient for large worlds
    // It's kept for backward compatibility but updateDirtyChunks() should be used instead
    
    // Collect all chunks that actually need updating to avoid unnecessary work
    m_dirtyChunkTracker.clearDirtyChunkList();
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i]->getNeedsUpdate()) {
            m_dirtyChunkTracker.markChunkDirty(i);
        }
    }
    
    if (m_dirtyChunkTracker.hasDirty()) {
        m_dirtyChunkTracker.updateDirtyChunks();
    }
}

void ChunkManager::rebuildChunkFaces(Chunk& chunk) {
    // Use cross-chunk culling method to maintain proper face occlusion across chunk boundaries
    rebuildChunkFacesWithCrosschunkCulling(chunk);
    chunk.setNeedsUpdate(true);  // Mark for GPU buffer update
}

void ChunkManager::rebuildChunkFacesWithCrosschunkCulling(Chunk& chunk) {
    // Provide a neighbor lookup function that can check cubes in adjacent chunks.
    // PERF: caches the last-resolved chunk. The skylight bake's columnOpenAbove probe walks
    // ~96 cells straight up per column (×1024 columns = ~98k calls), all hitting the same few
    // vertical chunks consecutively — without this memo each call did a chunk-map hash lookup,
    // which dominated rebuild time (~43ms of a ~50ms chunk rebuild). The memo collapses it to
    // one lookup per chunk transition (getCubeAt itself is an O(1) array index).
    bool ncValid = false;
    glm::ivec3 ncCoord(0);
    Chunk* ncChunk = nullptr;
    auto getNeighborCube = [this, ncValid, ncCoord, ncChunk](const glm::ivec3& worldPos) mutable -> const Cube* {
        glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
        if (!ncValid || chunkCoord != ncCoord) {
            ncValid = true;
            ncCoord = chunkCoord;
            ncChunk = getChunkAtCoord(chunkCoord);
        }
        if (ncChunk) return ncChunk->getCubeAt(worldToLocalCoord(worldPos));
        return nullptr;
    };

    // Cross-chunk baked-light lookup: lets the bake read a neighbour chunk's already-baked
    // sky/block light so light bleeds across chunk boundaries (no seams).
    auto getNeighborLight = [this, &chunk](const glm::ivec3& worldPos, Chunk::BakedLight& out) -> bool {
        Chunk* neighborChunk = getChunkAtCoord(worldToChunkCoord(worldPos));
        if (!neighborChunk || neighborChunk == &chunk) return false;
        return neighborChunk->bakedLightAt(worldToLocalCoord(worldPos), out);
    };

    // Precompute the skylight roof mask: for each of the 32x32 columns, is it open to the sky
    // above this chunk? The skylight bake needs this to seed sky columns. Doing it here (where we
    // can resolve whole neighbour chunks directly) replaces the bake's ~98k per-cell roof probe:
    // we resolve the up-to-3 chunks ABOVE once and scan only the ones that exist & are non-empty,
    // marking roofed columns. Absent/empty chunks above (the common surface case) cost nothing.
    std::vector<uint8_t> columnOpen(32 * 32, 1);  // 1 = open to sky (index x*32+z)
    {
        const glm::ivec3 cc = worldToChunkCoord(chunk.getWorldOrigin());
        constexpr int kProbeChunks = 3;  // 3*32 = 96 cells, matches the old kSkyProbeHeight
        // Resolve the up-to-3 chunks above ONCE (no per-cell hash lookups). Open terrain has none
        // → mask stays all-open at ~zero cost. Then probe each column directly with an early break
        // on the first solid (cheap array indexing, no coord conversion / lambda like the old probe).
        Chunk* above[kProbeChunks] = {nullptr, nullptr, nullptr};
        bool anyAbove = false;
        for (int i = 0; i < kProbeChunks; ++i) {
            Chunk* a = getChunkAtCoord(cc + glm::ivec3(0, i + 1, 0));
            if (a && a->getCubeCount() > 0) { above[i] = a; anyAbove = true; }
        }
        if (anyAbove) {
            for (int x = 0; x < 32; ++x) {
                for (int z = 0; z < 32; ++z) {
                    bool roofed = false;
                    for (int ci = 0; ci < kProbeChunks && !roofed; ++ci) {
                        Chunk* a = above[ci];
                        if (!a) continue;
                        for (int y = 0; y < 32; ++y) {
                            const Cube* c = a->getCubeAtIndex(static_cast<size_t>(z + y * 32 + x * 1024));
                            if (c && c->isVisible()) { roofed = true; break; }
                        }
                    }
                    if (roofed) columnOpen[x * 32 + z] = 0;
                }
            }
        }
    }

    // Call rebuildFaces with cross-chunk culling + light bleed + precomputed roof mask
    chunk.rebuildFaces(getNeighborCube, getNeighborLight, &columnOpen);

    // If this chunk's boundary light changed, its neighbours' border-seeded light is now stale —
    // re-mesh them so the bleed propagates. Gated on "actually changed", so this ripple converges
    // (light is monotonic and capped) rather than looping forever.
    // IDLE-tier remesh, NOT markChunkDirty: (1) the neighbours' VOXEL data is unchanged —
    // the DB-dirty flag made the streaming evictor re-save every light-rippled chunk to
    // SQLite (~200ms each, one per pump = the moving-camera stutter after everything else
    // went async); (2) a stale border light is cosmetic and converges in quiet frames,
    // it must not compete with must-run meshes of newly arrived chunks.
    if (chunk.lightBordersChanged()) {
        glm::ivec3 cc = worldToChunkCoord(chunk.getWorldOrigin());
        const glm::ivec3 dirs[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const auto& d : dirs) {
            Chunk* nb = getChunkAtCoord(cc + d);
            if (nb && nb != &chunk) markChunkForRemeshIdle(nb);
        }
    }
}

void ChunkManager::setGpuParticlePhysics(GpuParticlePhysics* gpp) {
    m_gpuParticles = gpp;
    // Wire the debris light sampler so GPU particle debris is lit by the baked light field
    // (sampled per particle at spawn). Returns sky/blockRGB each 0..15 (matches the nibble packing).
    if (gpp) {
        gpp->setLightSampler([this](const glm::vec3& wp) -> glm::vec4 {
            auto bl = sampleBakedLight(glm::ivec3(glm::floor(wp)));
            return glm::vec4(bl.sky, bl.r, bl.g, bl.b);
        });
    }
}

Graphics::ChunkRenderManager::BakedLight ChunkManager::sampleBakedLight(const glm::ivec3& worldPos) const {
    Chunk::BakedLight out;
    const Chunk* c = getChunkAtCoord(worldToChunkCoord(worldPos));
    if (c && c->bakedLightAt(worldToLocalCoord(worldPos), out)) return out;
    // No loaded chunk here → treat as open sky (outdoor), no block light.
    out.sky = 15; out.r = out.g = out.b = 0;
    return out;
}

// ===============================================================
// OPTIMIZED O(1) CHUNK AND CUBE LOOKUP FUNCTIONS
// ===============================================================

Chunk* ChunkManager::getChunkAtCoord(const glm::ivec3& chunkCoord) {
    return m_voxelQuerySystem.getChunkAtCoord(chunkCoord);
}

const Chunk* ChunkManager::getChunkAtCoord(const glm::ivec3& chunkCoord) const {
    return m_voxelQuerySystem.getChunkAtCoord(chunkCoord);
}

Chunk* ChunkManager::getChunkAtFast(const glm::ivec3& worldPos) {
    return m_voxelQuerySystem.getChunkAtFast(worldPos);
}

Cube* ChunkManager::getCubeAtFast(const glm::ivec3& worldPos) {
    return m_voxelQuerySystem.getCubeAtFast(worldPos);
}

bool ChunkManager::removeCubeFast(const glm::ivec3& worldPos) {
    return m_voxelModificationSystem.removeCubeFast(worldPos);
}

bool ChunkManager::addCubeFast(const glm::ivec3& worldPos) {
    return m_voxelModificationSystem.addCubeFast(worldPos);
}

// ===============================================================
// LEGACY FUNCTIONS (kept for backward compatibility)
// ===============================================================

Chunk* ChunkManager::getChunkAt(const glm::ivec3& worldPos) {
    return m_voxelQuerySystem.getChunkAt(worldPos);
}

bool ChunkManager::clearChunk(const glm::ivec3& chunkCoord) {
    Chunk* chunk = getChunkAtCoord(chunkCoord);
    if (!chunk) return false;
    chunk->clearAll();
    return true;
}

Cube* ChunkManager::getCubeAt(const glm::ivec3& worldPos) {
    return m_voxelQuerySystem.getCubeAt(worldPos);
}

// =============================================================================
// NEW: O(1) VoxelLocation resolution system for optimized hover detection
// =============================================================================

VoxelLocation ChunkManager::resolveGlobalPosition(const glm::ivec3& worldPos) const {
    return m_voxelQuerySystem.resolveGlobalPosition(worldPos);
}

VoxelLocation ChunkManager::resolveGlobalPositionWithSubcube(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) const {
    return m_voxelQuerySystem.resolveGlobalPositionWithSubcube(worldPos, subcubePos);
}

bool ChunkManager::hasVoxelAt(const glm::ivec3& worldPos) const {
    return m_voxelQuerySystem.hasVoxelAt(worldPos);
}

VoxelLocation::Type ChunkManager::getVoxelTypeAt(const glm::ivec3& worldPos) const {
    return m_voxelQuerySystem.getVoxelTypeAt(worldPos);
}

bool ChunkManager::removeCube(const glm::ivec3& worldPos) {
    return m_voxelModificationSystem.removeCube(worldPos);
}

void ChunkManager::ensureChunkAt(const glm::ivec3& worldPos) {
    glm::ivec3 chunkCoord = worldToChunkCoord(worldPos);
    if (!getChunkAtCoord(chunkCoord)) {
        glm::ivec3 origin = chunkCoordToOrigin(chunkCoord);
        // Create empty chunk (populate=false)
        createChunk(origin, false);
        LOG_INFO_FMT("ChunkManager", "Created new chunk at (" << origin.x << ", " << origin.y
                     << ", " << origin.z << ") for placement");
    }
}

bool ChunkManager::addCube(const glm::ivec3& worldPos) {
    ensureChunkAt(worldPos);  // chunk must exist before adding a cube
    return m_voxelModificationSystem.addCube(worldPos);
}

Subcube* ChunkManager::getSubcubeAt(const glm::ivec3& worldPos, const glm::ivec3& subcubePos) {
    return m_voxelQuerySystem.getSubcubeAt(worldPos, subcubePos);
}

uint32_t ChunkManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type!");
}

void ChunkManager::cleanup() {
    for (auto& chunk : chunks) {
        // The Chunk class now handles its own cleanup
        // No need to manually clean up Vulkan resources
    }
    chunks.clear();
    chunkMap.clear();  // Clear the spatial hash map
}

void ChunkManager::calculateChunkFaceCulling() {
    // NOTE: This function is no longer needed with CPU pre-filtering
    // Face culling is now performed during chunk population in populateChunk()
    // which calls calculateOcclusionFaceMask() for each cube
    
    LOG_DEBUG("Chunk", "calculateChunkFaceCulling: No longer needed with CPU pre-filtering");
}

ChunkManager::ChunkStats ChunkManager::getPerformanceStats() const {
    ChunkStats stats;
    
    for (const auto& chunk : chunks) {
        // Each face instance represents one visible face
        stats.totalVisibleFaces += chunk->getNumInstances();
        
        // Calculate vertices (4 vertices per face for quad rendering)
        stats.totalVertices += chunk->getNumInstances() * 4;
        
        // Count cubes and estimate hidden faces
        uint32_t totalPossibleCubes = 32 * 32 * 32;
        stats.totalCubes += totalPossibleCubes;
        
        // Estimate hidden faces (total possible faces - visible faces)
        uint32_t totalPossibleFaces = totalPossibleCubes * 6;
        if (chunk->getNumInstances() < totalPossibleFaces) {
            stats.totalHiddenFaces += (totalPossibleFaces - chunk->getNumInstances());
        }
        
        // Count occlusion types based on average visible faces per cube.
        // This is an approximation since we don't track individual cubes anymore.
        // The estimate is loop-invariant across a chunk's cells, so compute it O(1)
        // instead of iterating all 32768 cells every frame — this was a ~5ms/frame
        // hot path (same brute-force class as the old per-frame mirror scan).
        float avgVisibleFacesPerCube = float(chunk->getNumInstances()) / float(totalPossibleCubes);
        if (avgVisibleFacesPerCube == 0.0f) {
            stats.fullyOccludedCubes += totalPossibleCubes;
        } else if (avgVisibleFacesPerCube < 6.0f) {
            stats.partiallyOccludedCubes += totalPossibleCubes;
        }
    }
    
    return stats;
}

bool ChunkManager::isCubeAt(const glm::ivec3& worldPosition) const {
    // Convert world position to chunk coordinates
    glm::ivec3 chunkCoord = glm::ivec3(
        worldPosition.x / 32,
        worldPosition.y / 32,
        worldPosition.z / 32
    );
    glm::ivec3 chunkOrigin = chunkCoord * 32;
    
    // Find the chunk containing this world position
    for (const auto& chunk : chunks) {
        if (chunk->getWorldOrigin() == chunkOrigin) {
            // Calculate relative position within chunk
            glm::ivec3 relativePos = worldPosition - chunkOrigin;
            
            // Check if relative position is valid (0-31 in each dimension)
            if (relativePos.x >= 0 && relativePos.x < 32 &&
                relativePos.y >= 0 && relativePos.y < 32 &&
                relativePos.z >= 0 && relativePos.z < 32) {
                
                // Check if there's actually a cube at this position
                const Cube* cube = chunk->getCubeAt(relativePos);
                return cube != nullptr && cube->isVisible();
            }
            break;
        }
    }
    
    return false; // No chunk found or position outside chunk bounds
}

uint32_t ChunkManager::calculateOcclusionFaceMask(const glm::ivec3& chunkOrigin, int relativeX, int relativeY, int relativeZ) const {
    // Calculate world position for this cube
    glm::ivec3 worldPos = chunkOrigin + glm::ivec3(relativeX, relativeY, relativeZ);
    
    // Check each face direction for adjacent cubes (including cross-chunk)
    uint32_t faceMask = 0;
    
    // Front face (+Z): visible if no cube at (x, y, z+1)
    if (!isCubeAt(worldPos + glm::ivec3(0, 0, 1))) {
        faceMask |= (1 << 0);  // Front face visible
    }
    
    // Back face (-Z): visible if no cube at (x, y, z-1)
    if (!isCubeAt(worldPos + glm::ivec3(0, 0, -1))) {
        faceMask |= (1 << 1);  // Back face visible
    }
    
    // Right face (+X): visible if no cube at (x+1, y, z)
    if (!isCubeAt(worldPos + glm::ivec3(1, 0, 0))) {
        faceMask |= (1 << 2);  // Right face visible
    }
    
    // Left face (-X): visible if no cube at (x-1, y, z)
    if (!isCubeAt(worldPos + glm::ivec3(-1, 0, 0))) {
        faceMask |= (1 << 3);  // Left face visible
    }
    
    // Top face (+Y): visible if no cube at (x, y+1, z)
    if (!isCubeAt(worldPos + glm::ivec3(0, 1, 0))) {
        faceMask |= (1 << 4);  // Top face visible
    }
    
    // Bottom face (-Y): visible if no cube at (x, y-1, z)
    if (!isCubeAt(worldPos + glm::ivec3(0, -1, 0))) {
        faceMask |= (1 << 5);  // Bottom face visible
    }
    
    return faceMask;
}

void ChunkManager::performOcclusionCulling() {
    m_chunkInitializer.performOcclusionCulling();
    
    // Calculate statistics
    ChunkStats stats = getPerformanceStats();
    LOG_DEBUG_FMT("Chunk", "Occlusion culling complete: " << stats.totalCubes << " total cubes, " 
              << stats.totalVisibleFaces << " visible faces, "
              << stats.totalHiddenFaces << " hidden faces");
}

// ===============================================================
// DIRTY CHUNK TRACKING OPTIMIZATION
// ===============================================================

void ChunkManager::markChunkDirty(size_t chunkIndex) {
    m_dirtyChunkTracker.markChunkDirty(chunkIndex);
}

void ChunkManager::markChunkDirty(Chunk* chunk) {
    m_dirtyChunkTracker.markChunkDirty(chunk);
}

void ChunkManager::markChunkForRemesh(Chunk* chunk) {
    m_dirtyChunkTracker.markChunkForRemesh(chunk);
}

void ChunkManager::markChunkForRemeshIdle(Chunk* chunk) {
    m_dirtyChunkTracker.markChunkForRemeshIdle(chunk);
}

void ChunkManager::clearDirtyChunkList() {
    m_dirtyChunkTracker.clearDirtyChunkList();
}

void ChunkManager::updateSmoothedFps(float deltaTime) {
    if (deltaTime > 0.0f) {
        float instantFps = 1.0f / deltaTime;
        // Exponential moving average (alpha ~0.05 = smooth over ~20 frames)
        m_smoothedFps = m_smoothedFps * 0.95f + instantFps * 0.05f;
    }
}

void ChunkManager::addGlobalDynamicSubcube(std::unique_ptr<Subcube> subcube) {
    if (!subcube) return;
    m_dynamicObjectManager.addGlobalDynamicSubcube(std::move(subcube));
}

void ChunkManager::rebuildGlobalDynamicSubcubeFaces() {
    // Legacy function - now calls the combined function that handles both subcubes and cubes
    rebuildGlobalDynamicFaces();
}

void ChunkManager::updateGlobalDynamicSubcubes(float deltaTime) {
    m_dynamicObjectManager.updateGlobalDynamicSubcubes(deltaTime);
}

void ChunkManager::updateGlobalDynamicSubcubePositions() {
    m_dynamicObjectManager.updateGlobalDynamicSubcubePositions();
}

void ChunkManager::clearAllGlobalDynamicSubcubes() {
    m_dynamicObjectManager.clearAllGlobalDynamicSubcubes();
}

// ===============================================================
// GLOBAL DYNAMIC CUBE MANAGEMENT
// ===============================================================

void ChunkManager::addGlobalDynamicCube(std::unique_ptr<Cube> cube) {
    if (!cube) return;
    m_dynamicObjectManager.addGlobalDynamicCube(std::move(cube));
}

void ChunkManager::updateGlobalDynamicCubes(float deltaTime) {
    m_dynamicObjectManager.updateGlobalDynamicCubes(deltaTime);
}

void ChunkManager::updateGlobalDynamicCubePositions() {
    m_dynamicObjectManager.updateGlobalDynamicCubePositions();
}

void ChunkManager::clearAllGlobalDynamicCubes() {
    m_dynamicObjectManager.clearAllGlobalDynamicCubes();
}

// ===============================================================
// GLOBAL DYNAMIC MICROCUBE MANAGEMENT
// ===============================================================

void ChunkManager::addGlobalDynamicMicrocube(std::unique_ptr<Microcube> microcube) {
    if (!microcube) return;
    globalDynamicMicrocubes.push_back(std::move(microcube));
    rebuildGlobalDynamicFaces();
}

void ChunkManager::updateGlobalDynamicMicrocubes(float deltaTime) {
    // Update lifetimes and remove expired microcubes
    auto it = globalDynamicMicrocubes.begin();
    size_t removedCount = 0;
    
    while (it != globalDynamicMicrocubes.end()) {
        (*it)->updateLifetime(deltaTime);
        
        if ((*it)->hasExpired()) {
            removedCount++;
            // Note: The unique_ptr destructor will automatically clean up the microcube
            it = globalDynamicMicrocubes.erase(it);
        } else {
            ++it;
        }
    }
    
    // Rebuild faces if any microcubes were removed
    if (removedCount > 0) {
        LOG_DEBUG_FMT("ChunkManager", "[MICROCUBE] Removed " << removedCount << " expired dynamic microcubes (lifetime ended)");
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::updateGlobalDynamicMicrocubePositions() {
    bool transformsChanged = false;

    for (auto& microcube : globalDynamicMicrocubes) {
        if (!microcube) continue;
        if (auto* vb = microcube->getVoxelBody()) {
            if (vb->isAsleep) continue;
            glm::vec3 newWorldPos = vb->position;
            const glm::quat& q = vb->orientation;
            microcube->setPhysicsPosition(newWorldPos);
            microcube->setPhysicsRotation(glm::vec4(q.x, q.y, q.z, q.w));
            transformsChanged = true;
        }
    }

    if (transformsChanged) {
        rebuildGlobalDynamicFaces();
    }
}

void ChunkManager::clearAllGlobalDynamicMicrocubes() {
    LOG_DEBUG_FMT("ChunkManager", "[MICROCUBE] Clearing all " << globalDynamicMicrocubes.size() << " global dynamic microcubes");
    globalDynamicMicrocubes.clear();
}

// ===============================================================
// COMBINED DYNAMIC OBJECT MANAGEMENT (SUBCUBES + CUBES)
// ===============================================================

void ChunkManager::rebuildGlobalDynamicFaces() {
    m_faceUpdateCoordinator.rebuildGlobalDynamicFaces();
}

size_t ChunkManager::getChunkIndex(const Chunk* chunk) const {
    // Find the index of a chunk in the chunks vector
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].get() == chunk) {
            return i;
        }
    }
    return SIZE_MAX; // Invalid index if not found
}

// ========================================================================
// EFFICIENT SELECTIVE UPDATE SYSTEM
// ========================================================================

void ChunkManager::updateAfterCubeBreak(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateAfterCubeBreak(worldPos);
    if (m_gpuParticles) m_gpuParticles->setOccupied(worldPos.x, worldPos.y, worldPos.z, false);
    if (m_voxelOccupancyCallback) m_voxelOccupancyCallback(worldPos.x, worldPos.y, worldPos.z, false);
}

void ChunkManager::updateAfterCubePlace(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateAfterCubePlace(worldPos);
    if (m_gpuParticles) m_gpuParticles->setOccupied(worldPos.x, worldPos.y, worldPos.z, true);
    if (m_voxelOccupancyCallback) m_voxelOccupancyCallback(worldPos.x, worldPos.y, worldPos.z, true);
}

void ChunkManager::rebuildOccupancyFromChunks() {
    if (!m_gpuParticles) return;
    m_gpuParticles->clearOccupancy();
    for (const auto& chunkPtr : chunks) {
        const glm::ivec3 origin = chunkPtr->getWorldOrigin();
        for (int lx = 0; lx < 32; ++lx) {
            for (int ly = 0; ly < 32; ++ly) {
                for (int lz = 0; lz < 32; ++lz) {
                    glm::ivec3 world(origin.x + lx, origin.y + ly, origin.z + lz);
                    if (hasVoxelAt(world))
                        m_gpuParticles->setOccupied(world.x, world.y, world.z, true);
                }
            }
        }
    }
    LOG_INFO_FMT("ChunkManager", "Occupancy grid rebuilt from " << chunks.size() << " chunks");
}

void ChunkManager::updateOccupancyVoxel(int worldX, int worldY, int worldZ, bool solid) {
    if (m_gpuParticles) m_gpuParticles->setOccupied(worldX, worldY, worldZ, solid);
    if (m_voxelOccupancyCallback) m_voxelOccupancyCallback(worldX, worldY, worldZ, solid);
}

void ChunkManager::syncChunkToOccupancy(const glm::ivec3& chunkWorldOrigin) {
    if (!m_gpuParticles && !m_voxelOccupancyCallback) return;
    // Direct map lookup + dense-array cube reads. The previous form linear-scanned the chunk
    // vector, then made 32k GLOBAL hasVoxelAt queries (chunk lookup + hash each) — ~26ms per
    // streamed chunk, in the pump, on the main thread.
    Chunk* chunk = getChunkAtCoord(Utils::CoordinateUtils::worldToChunkCoord(chunkWorldOrigin));
    if (!chunk) return;
    for (int lx = 0; lx < 32; ++lx) {
        for (int ly = 0; ly < 32; ++ly) {
            for (int lz = 0; lz < 32; ++lz) {
                if (!chunk->getCubeAt(glm::ivec3(lx, ly, lz))) continue;
                const int wx = chunkWorldOrigin.x + lx, wy = chunkWorldOrigin.y + ly,
                          wz = chunkWorldOrigin.z + lz;
                if (m_gpuParticles) m_gpuParticles->setOccupied(wx, wy, wz, true);
                // Water-sim solidity (setSolidWorld bounds-rejects out-of-region cells cheaply).
                if (m_voxelOccupancyCallback) m_voxelOccupancyCallback(wx, wy, wz, true);
            }
        }
    }
}

void ChunkManager::updateAfterCubeSubdivision(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateAfterCubeSubdivision(worldPos);
}

void ChunkManager::updateAfterSubcubeBreak(const glm::ivec3& parentWorldPos, const glm::ivec3& subcubeLocalPos) {
    m_faceUpdateCoordinator.updateAfterSubcubeBreak(parentWorldPos, subcubeLocalPos);
}

void ChunkManager::updateFacesForPositionChange(const glm::ivec3& worldPos, bool cubeAdded) {
    m_faceUpdateCoordinator.updateFacesForPositionChange(worldPos, cubeAdded);
}

void ChunkManager::updateNeighborFaces(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateNeighborFaces(worldPos);
}

void ChunkManager::updateSingleCubeFaces(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateSingleCubeFaces(worldPos);
}

std::vector<glm::ivec3> ChunkManager::getAffectedNeighborPositions(const glm::ivec3& worldPos) {
    return m_faceUpdateCoordinator.getAffectedNeighborPositions(worldPos);
}

void ChunkManager::updateFacesAtPosition(const glm::ivec3& worldPos) {
    m_faceUpdateCoordinator.updateFacesAtPosition(worldPos);
}

} // namespace Phyxel
