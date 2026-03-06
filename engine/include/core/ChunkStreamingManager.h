#pragma once

#include "Types.h"
#include "Chunk.h"
#include <glm/glm.hpp>
#include <functional>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Phyxel {
    class WorldStorage; // Forward declaration
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

    ChunkStreamingManager() = default;
    ~ChunkStreamingManager();

    // Callback setup
    void setCallbacks(
        ChunkCreationFunc createChunkFunc,
        ChunkMapAccessFunc getChunkMapFunc,
        ChunkVectorAccessFunc getChunksFunc,
        DeviceAccessFunc getDevicesFunc
    );

    // World storage management
    bool initializeWorldStorage(const std::string& worldPath);

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

    // World storage
    WorldStorage* worldStorage = nullptr;

    // Helper methods
    Chunk* getChunkAtCoord(const glm::ivec3& chunkCoord);
};

} // namespace Phyxel
