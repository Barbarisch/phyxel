#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <future>
#include <nlohmann/json.hpp>
#include "core/APICommandQueue.h"

namespace Phyxel {
namespace Core {

using json = nlohmann::json;

// ============================================================================
// EngineAPIServer
//
// Lightweight HTTP/JSON API that lets external AI agents (Claude Code, Goose,
// custom scripts, curl) control the running game engine.
//
// Runs on a background thread; all mutations go through APICommandQueue
// and are executed on the main thread during the game loop's update().
//
// Default port: 8090
//
// Endpoints:
//   GET  /api/status            — Server health check
//   GET  /api/entities          — List all registered entities
//   GET  /api/entity/:id        — Get entity details
//   POST /api/entity/spawn      — Spawn a new entity
//   POST /api/entity/:id/move   — Move entity to position
//   POST /api/entity/:id/remove — Remove entity
//   GET  /api/world/voxel       — Query voxel at position
//   POST /api/world/voxel       — Place a voxel
//   POST /api/world/voxel/remove— Remove a voxel
//   POST /api/world/template    — Spawn an object template
//   GET  /api/camera            — Get camera position/orientation
//   POST /api/camera            — Set camera position/orientation
//   POST /api/script            — Execute a Python script snippet
//   GET  /api/state             — Full world state snapshot
//   GET  /api/screenshot        — Capture frame as PNG
//   POST /api/world/fill        — Fill a region with voxels
//   GET  /api/materials         — List available materials
//   GET  /api/world/chunks      — Chunk info (count, bounds)
//   POST /api/entity/update     — Update entity properties
//   GET  /api/world/scan        — Scan a region for voxels
//   POST /api/world/clear       — Clear all voxels in a region
//   POST /api/world/save        — Save world to database
//   GET  /api/events            — Poll game events (cursor-based)
//
// All POST bodies and responses are JSON.
// ============================================================================

class EngineAPIServer {
public:
    /// Create the API server.
    /// @param commandQueue  Shared command queue (owned by Application)
    /// @param port          Port to listen on (default 8090)
    EngineAPIServer(APICommandQueue* commandQueue, int port = 8090);
    ~EngineAPIServer();

    // Non-copyable
    EngineAPIServer(const EngineAPIServer&) = delete;
    EngineAPIServer& operator=(const EngineAPIServer&) = delete;

    /// Start the HTTP server on a background thread.
    /// Returns false if already running or port is unavailable.
    bool start();

    /// Stop the HTTP server and join the background thread.
    void stop();

    /// Check if the server is running.
    bool isRunning() const { return m_running.load(); }

    /// Get the port the server is listening on.
    int getPort() const { return m_port; }

    // ========================================================================
    // Read-only handler registration
    //
    // These handlers are called directly on the HTTP thread (no queuing)
    // because they don't mutate game state. They must be thread-safe.
    // ========================================================================

    /// Handler that returns the entity list JSON.
    using EntityListHandler = std::function<json()>;
    void setEntityListHandler(EntityListHandler handler) { m_entityListHandler = std::move(handler); }

    /// Handler that returns a single entity's JSON by ID.
    using EntityDetailHandler = std::function<json(const std::string& id)>;
    void setEntityDetailHandler(EntityDetailHandler handler) { m_entityDetailHandler = std::move(handler); }

    /// Handler that returns camera state JSON.
    using CameraHandler = std::function<json()>;
    void setCameraHandler(CameraHandler handler) { m_cameraHandler = std::move(handler); }

    /// Handler that returns a voxel at world coordinates.
    using VoxelQueryHandler = std::function<json(int x, int y, int z)>;
    void setVoxelQueryHandler(VoxelQueryHandler handler) { m_voxelQueryHandler = std::move(handler); }

    /// Handler that returns full world state snapshot.
    using WorldStateHandler = std::function<json()>;
    void setWorldStateHandler(WorldStateHandler handler) { m_worldStateHandler = std::move(handler); }

    /// Handler that captures a screenshot and returns PNG bytes.
    /// Called on the main thread (via command queue).
    /// Returns JSON: {"png_base64": "...", "width": N, "height": N}
    using ScreenshotHandler = std::function<json()>;
    void setScreenshotHandler(ScreenshotHandler handler) { m_screenshotHandler = std::move(handler); }

    /// Handler that returns available material names.
    using MaterialListHandler = std::function<json()>;
    void setMaterialListHandler(MaterialListHandler handler) { m_materialListHandler = std::move(handler); }

    /// Handler that returns chunk info (count, positions, bounds).
    using ChunkInfoHandler = std::function<json()>;
    void setChunkInfoHandler(ChunkInfoHandler handler) { m_chunkInfoHandler = std::move(handler); }

    /// Handler that scans a region and returns all voxels within it.
    /// Called on HTTP thread (read-only). Takes min/max world coords.
    using RegionScanHandler = std::function<json(int x1, int y1, int z1, int x2, int y2, int z2)>;
    void setRegionScanHandler(RegionScanHandler handler) { m_regionScanHandler = std::move(handler); }

    /// Handler that polls game events since a cursor. Called on HTTP thread (read-only).
    using EventPollHandler = std::function<json(uint64_t sinceId)>;
    void setEventPollHandler(EventPollHandler handler) { m_eventPollHandler = std::move(handler); }

private:
    void serverThread();
    void setupRoutes();

    /// Helper: queue a command and wait for the game loop to process it.
    /// Returns the JSON response from the game loop handler.
    /// Times out after timeoutMs.
    json queueAndWait(const std::string& action, const json& params, int timeoutMs = 5000);

    APICommandQueue* m_commandQueue;
    int m_port;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shouldStop{false};
    std::thread m_thread;
    std::atomic<uint64_t> m_nextRequestId{1};

    // Read-only handlers (called on HTTP thread)
    EntityListHandler m_entityListHandler;
    EntityDetailHandler m_entityDetailHandler;
    CameraHandler m_cameraHandler;
    VoxelQueryHandler m_voxelQueryHandler;
    WorldStateHandler m_worldStateHandler;
    ScreenshotHandler m_screenshotHandler;
    MaterialListHandler m_materialListHandler;
    ChunkInfoHandler m_chunkInfoHandler;
    RegionScanHandler m_regionScanHandler;
    EventPollHandler m_eventPollHandler;

    // Forward-declared impl to keep httplib out of the header
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Core
} // namespace Phyxel
