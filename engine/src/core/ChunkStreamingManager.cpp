#include "core/ChunkStreamingManager.h"
#include "core/WorldStorage.h"
#include "utils/Logger.h"

namespace Phyxel {

ChunkStreamingManager::ChunkStreamingManager() = default;

ChunkStreamingManager::~ChunkStreamingManager() {
    if (worldStorage) {
        worldStorage->close();
        delete worldStorage;
        worldStorage = nullptr;
    }
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

} // namespace Phyxel
