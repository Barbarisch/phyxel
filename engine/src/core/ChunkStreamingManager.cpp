#include "core/ChunkStreamingManager.h"
#include "core/WorldStorage.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <utility>

namespace Phyxel {

ChunkStreamingManager::~ChunkStreamingManager() {
    // Clean up world storage
    delete worldStorage;
    worldStorage = nullptr;
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
    if (worldStorage) {
        worldStorage->close();
        delete worldStorage;
        worldStorage = nullptr;
        LOG_INFO("ChunkStreaming", "World storage disconnected");
    }
}

void ChunkStreamingManager::updateStreaming(const glm::vec3& playerPosition, float loadDistance, float unloadDistance) {
    if (!worldStorage) return;
    
    // Load chunks around player
    loadChunksAroundPosition(playerPosition, loadDistance);
    
    // Unload distant chunks
    unloadDistantChunks(playerPosition, unloadDistance);
}

void ChunkStreamingManager::loadChunksAroundPosition(const glm::vec3& position, float radius) {
    glm::ivec3 centerChunk = Utils::CoordinateUtils::worldToChunkCoord(glm::ivec3(position));
    int chunkRadius = static_cast<int>(std::ceil(radius / 32.0f));

    // Collect missing chunks within radius, paired with their distance to the player.
    std::vector<std::pair<float, glm::ivec3>> missing;
    for (int dx = -chunkRadius; dx <= chunkRadius; ++dx) {
        for (int dy = -chunkRadius; dy <= chunkRadius; ++dy) {
            for (int dz = -chunkRadius; dz <= chunkRadius; ++dz) {
                glm::ivec3 chunkCoord = centerChunk + glm::ivec3(dx, dy, dz);

                glm::vec3 chunkCenter = glm::vec3(Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord)) + glm::vec3(16.0f);
                float distance = glm::length(chunkCenter - position);

                if (distance <= radius && !getChunkAtCoord(chunkCoord)) {
                    missing.emplace_back(distance, chunkCoord);
                }
            }
        }
    }

    // Nearest first, so a capped update fills the area around the player before the edges.
    std::sort(missing.begin(), missing.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

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
        m_pendingDeletion.clear();  // Chunk destructors free buffers + unregister occupancy grids
    }

    int evicted = 0;
    auto it = chunks.begin();
    while (it != chunks.end()) {
        glm::vec3 chunkCenter = glm::vec3((*it)->getWorldOrigin()) + glm::vec3(16.0f);
        float distance = glm::length(chunkCenter - position);

        // Bound evictions per pump (same cap as generation) so churn stays smooth; distant
        // stragglers are cleaned up over the next few pumps.
        bool capReached = (m_maxChunksPerUpdate > 0 && evicted >= m_maxChunksPerUpdate);
        if (distance > radius && !capReached) {
            // Save chunk before unloading
            if (worldStorage) {
                saveChunk(it->get());
            }

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
}

bool ChunkStreamingManager::saveChunk(Chunk* chunk) {
    if (!worldStorage || !chunk) return false;
    return worldStorage->saveChunk(*chunk);
}

bool ChunkStreamingManager::saveAllChunks() {
    if (!worldStorage) return false;
    
    auto& chunks = m_getChunks();
    bool allSuccess = true;
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
    
    if (worldStorage->loadChunk(chunkCoord, *chunk)) {
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
    
    glm::ivec3 origin = Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    auto chunk = std::make_unique<Chunk>(origin);
    chunk->initialize(device, physicalDevice);
    
    // Fill the chunk: use the configured world generator if one is wired (the Phase-1
    // generation wire), otherwise fall back to the legacy random fill.
    if (m_generateChunk) {
        m_generateChunk(*chunk, chunkCoord);
    } else {
        chunk->populateWithCubes();
    }

    // DON'T rebuild faces yet - wait until all chunks are loaded
    chunk->createVulkanBuffer();

    // Add to map and vector
    auto& chunkMap = m_getChunkMap();
    auto& chunks = m_getChunks();
    chunkMap[chunkCoord] = chunk.get();
    glm::ivec3 genOrigin = Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    chunks.push_back(std::move(chunk));
    // Finalize the freshly streamed-in chunk (collision + faces) so characters don't fall
    // through and it renders correctly. Gated so the initial bulk load (which calls
    // buildAllChunkPhysics + rebuildAllChunkFaces afterward) doesn't double-process.
    if (m_perChunkPhysics && m_onChunkStreamedIn) m_onChunkStreamedIn(*chunks.back());
    if (m_onChunkLoaded) m_onChunkLoaded(genOrigin);

    // Save to storage immediately
    saveChunk(chunks.back().get());
    
    LOG_DEBUG_FMT("ChunkStreaming", "Generated and saved new chunk: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z);
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
