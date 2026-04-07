#include "core/ChunkStreamingManager.h"
#include "core/WorldStorage.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include <cmath>

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
    
    for (int dx = -chunkRadius; dx <= chunkRadius; ++dx) {
        for (int dy = -chunkRadius; dy <= chunkRadius; ++dy) {
            for (int dz = -chunkRadius; dz <= chunkRadius; ++dz) {
                glm::ivec3 chunkCoord = centerChunk + glm::ivec3(dx, dy, dz);
                
                // Check if chunk is within radius
                glm::vec3 chunkCenter = glm::vec3(Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord)) + glm::vec3(16.0f);
                float distance = glm::length(chunkCenter - position);
                
                if (distance <= radius && !getChunkAtCoord(chunkCoord)) {
                    // Try to load chunk from storage, or generate if it doesn't exist
                    generateOrLoadChunk(chunkCoord);
                }
            }
        }
    }
}

void ChunkStreamingManager::unloadDistantChunks(const glm::vec3& position, float radius) {
    auto& chunks = m_getChunks();
    auto& chunkMap = m_getChunkMap();
    
    auto it = chunks.begin();
    while (it != chunks.end()) {
        glm::vec3 chunkCenter = glm::vec3((*it)->getWorldOrigin()) + glm::vec3(16.0f);
        float distance = glm::length(chunkCenter - position);
        
        if (distance > radius) {
            // Save chunk before unloading
            if (worldStorage) {
                saveChunk(it->get());
            }
            
            // Remove from chunk map
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord((*it)->getWorldOrigin());
            chunkMap.erase(chunkCoord);
            
            // Erase chunk from vector
            it = chunks.erase(it);
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
    
    // Fallback to old random generation
    chunk->populateWithCubes();
    
    // DON'T rebuild faces yet - wait until all chunks are loaded  
    chunk->createVulkanBuffer();
    
    // Add to map and vector
    auto& chunkMap = m_getChunkMap();
    auto& chunks = m_getChunks();
    chunkMap[chunkCoord] = chunk.get();
    glm::ivec3 genOrigin = Utils::CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    chunks.push_back(std::move(chunk));
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
