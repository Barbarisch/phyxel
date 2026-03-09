#include "core/EngineAPIServer.h"

// cpp-httplib for the HTTP server
#include <httplib.h>

#include <iostream>
#include <chrono>
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

// ============================================================================
// Implementation detail — holds the httplib::Server
// ============================================================================

struct EngineAPIServer::Impl {
    httplib::Server server;
};

// ============================================================================
// Construction / Destruction
// ============================================================================

EngineAPIServer::EngineAPIServer(APICommandQueue* commandQueue, int port)
    : m_commandQueue(commandQueue)
    , m_port(port)
    , m_impl(std::make_unique<Impl>())
{
}

EngineAPIServer::~EngineAPIServer() {
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool EngineAPIServer::start() {
    if (m_running.load()) {
        LOG_WARN("EngineAPIServer", "Server already running on port " << m_port);
        return false;
    }

    setupRoutes();
    m_shouldStop.store(false);

    m_thread = std::thread([this]() { serverThread(); });

    // Wait briefly for the server to start listening
    for (int i = 0; i < 50; ++i) {
        if (m_running.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (m_running.load()) {
        LOG_INFO("EngineAPIServer", "HTTP API server started on port " << m_port);
    } else {
        LOG_ERROR("EngineAPIServer", "Failed to start HTTP API server on port " << m_port);
    }
    return m_running.load();
}

void EngineAPIServer::stop() {
    if (!m_running.load()) return;

    m_shouldStop.store(true);
    m_impl->server.stop();

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_running.store(false);
    LOG_INFO("EngineAPIServer", "HTTP API server stopped");
}

void EngineAPIServer::serverThread() {
    m_running.store(true);
    m_impl->server.listen("0.0.0.0", m_port);
    m_running.store(false);
}

// ============================================================================
// Route Setup
// ============================================================================

void EngineAPIServer::setupRoutes() {
    auto& srv = m_impl->server;

    // CORS headers for browser-based tools
    srv.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization"}
    });

    // OPTIONS preflight handler
    srv.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // ====================================================================
    // GET /api/status — Health check
    // ====================================================================
    srv.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        json response = {
            {"status", "ok"},
            {"engine", "phyxel"},
            {"api_version", "1.0"},
            {"port", m_port}
        };
        res.set_content(response.dump(), "application/json");
    });

    // ====================================================================
    // GET /api/entities — List all entities
    // ====================================================================
    srv.Get("/api/entities", [this](const httplib::Request&, httplib::Response& res) {
        if (m_entityListHandler) {
            json result = m_entityListHandler();
            json response = {{"entities", result}};
            res.set_content(response.dump(), "application/json");
        } else {
            json err = {{"error", "Entity list handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/entity/:id — Get entity details
    // ====================================================================
    srv.Get("/api/entity/(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        if (id.empty()) {
            json err = {{"error", "Entity ID required"}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            return;
        }

        if (m_entityDetailHandler) {
            json result = m_entityDetailHandler(id);
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Entity detail handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/entity/spawn — Spawn a new entity
    // Body: { "type": "physics"|"spider"|"animated", "position": {"x":0,"y":20,"z":0},
    //         "id": "npc_01" (optional), "animFile": "..." (for animated) }
    // ====================================================================
    srv.Post("/api/entity/spawn", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("spawn_entity", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/entity/move — Move an entity
    // Body: { "id": "npc_01", "position": {"x":10,"y":20,"z":30} }
    // ====================================================================
    srv.Post("/api/entity/move", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("move_entity", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/entity/remove — Remove an entity
    // Body: { "id": "npc_01" }
    // ====================================================================
    srv.Post("/api/entity/remove", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("remove_entity", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/world/voxel?x=0&y=0&z=0 — Query voxel at position
    // ====================================================================
    srv.Get("/api/world/voxel", [this](const httplib::Request& req, httplib::Response& res) {
        if (!m_voxelQueryHandler) {
            json err = {{"error", "Voxel query handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
            return;
        }

        int x = 0, y = 0, z = 0;
        if (req.has_param("x")) x = std::stoi(req.get_param_value("x"));
        if (req.has_param("y")) y = std::stoi(req.get_param_value("y"));
        if (req.has_param("z")) z = std::stoi(req.get_param_value("z"));

        json result = m_voxelQueryHandler(x, y, z);
        res.set_content(result.dump(), "application/json");
    });

    // ====================================================================
    // POST /api/world/voxel — Place a voxel
    // Body: { "x": 0, "y": 0, "z": 0, "material": "stone" (optional) }
    // ====================================================================
    srv.Post("/api/world/voxel", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("place_voxel", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/world/voxel/remove — Remove a voxel
    // Body: { "x": 0, "y": 0, "z": 0 }
    // ====================================================================
    srv.Post("/api/world/voxel/remove", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("remove_voxel", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/world/template — Spawn an object template
    // Body: { "name": "castle", "position": {"x":0,"y":0,"z":0}, "static": true }
    // ====================================================================
    srv.Post("/api/world/template", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("spawn_template", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/camera — Get camera state
    // ====================================================================
    srv.Get("/api/camera", [this](const httplib::Request&, httplib::Response& res) {
        if (m_cameraHandler) {
            json result = m_cameraHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Camera handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/camera — Set camera position/orientation
    // Body: { "position": {"x":0,"y":20,"z":0}, "yaw": -90.0, "pitch": -20.0 }
    // ====================================================================
    srv.Post("/api/camera", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("set_camera", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/script — Execute a Python script snippet
    // Body: { "code": "phyxel.get_app().create_physics_character(0, 20, 0)" }
    // ====================================================================
    srv.Post("/api/script", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("run_script", params, 10000); // longer timeout for scripts
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/state — Full world state snapshot
    // ====================================================================
    srv.Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        if (m_worldStateHandler) {
            json result = m_worldStateHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "World state handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/screenshot — Capture current frame as PNG file
    // Returns: { "path": "screenshots/screenshot_XXX.png", "width": N, "height": N }
    // ====================================================================
    srv.Get("/api/screenshot", [this](const httplib::Request&, httplib::Response& res) {
        json result = queueAndWait("capture_screenshot", json::object(), 10000);
        if (result.contains("error")) {
            res.status = 500;
        }
        res.set_content(result.dump(), "application/json");
    });

    // ====================================================================
    // POST /api/world/voxel/batch — Place multiple voxels at once
    // Body: { "voxels": [{"x":0,"y":0,"z":0,"material":"stone"}, ...] }
    // ====================================================================
    srv.Post("/api/world/voxel/batch", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("place_voxels_batch", params, 30000); // long timeout for batches
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/templates — List available object templates
    // ====================================================================
    srv.Get("/api/templates", [this](const httplib::Request&, httplib::Response& res) {
        // This is a read-only query that gets processed on game loop
        json result = queueAndWait("list_templates", json::object());
        res.set_content(result.dump(), "application/json");
    });

    // ====================================================================
    // POST /api/world/fill — Fill a 3D region with voxels
    // Body: { "x1":0,"y1":0,"z1":0, "x2":10,"y2":5,"z2":10, "material":"Stone" }
    // ====================================================================
    srv.Post("/api/world/fill", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("fill_region", params, 60000); // long timeout for large fills
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/materials — List available material names
    // ====================================================================
    srv.Get("/api/materials", [this](const httplib::Request&, httplib::Response& res) {
        if (m_materialListHandler) {
            json result = m_materialListHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Material list handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/world/chunks — Chunk information
    // ====================================================================
    srv.Get("/api/world/chunks", [this](const httplib::Request&, httplib::Response& res) {
        if (m_chunkInfoHandler) {
            json result = m_chunkInfoHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Chunk info handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/world/scan — Scan a region for voxels
    // Query: ?x1=0&y1=0&z1=0&x2=10&y2=10&z2=10
    // ====================================================================
    srv.Get("/api/world/scan", [this](const httplib::Request& req, httplib::Response& res) {
        if (!m_regionScanHandler) {
            json err = {{"error", "Region scan handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
            return;
        }
        int x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
        if (req.has_param("x1")) x1 = std::stoi(req.get_param_value("x1"));
        if (req.has_param("y1")) y1 = std::stoi(req.get_param_value("y1"));
        if (req.has_param("z1")) z1 = std::stoi(req.get_param_value("z1"));
        if (req.has_param("x2")) x2 = std::stoi(req.get_param_value("x2"));
        if (req.has_param("y2")) y2 = std::stoi(req.get_param_value("y2"));
        if (req.has_param("z2")) z2 = std::stoi(req.get_param_value("z2"));
        json result = m_regionScanHandler(x1, y1, z1, x2, y2, z2);
        res.set_content(result.dump(), "application/json");
    });

    // ====================================================================
    // POST /api/world/clear — Clear all voxels in a region
    // Body: { "x1":0,"y1":0,"z1":0, "x2":10,"y2":5,"z2":10 }
    // ====================================================================
    srv.Post("/api/world/clear", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("clear_region", params, 60000);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/world/save — Save dirty chunks to database
    // Body: { "all": false }  (optional, default saves only dirty chunks)
    // ====================================================================
    srv.Post("/api/world/save", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::object();
            if (!req.body.empty()) {
                params = json::parse(req.body);
            }
            json result = queueAndWait("save_world", params, 30000);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/events — Poll game events (cursor-based)
    // Query: ?since=42  (returns events with id > 42)
    // ====================================================================
    srv.Get("/api/events", [this](const httplib::Request& req, httplib::Response& res) {
        if (!m_eventPollHandler) {
            json err = {{"error", "Event poll handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
            return;
        }
        uint64_t sinceId = 0;
        if (req.has_param("since")) {
            sinceId = std::stoull(req.get_param_value("since"));
        }
        json result = m_eventPollHandler(sinceId);
        res.set_content(result.dump(), "application/json");
    });

    // ====================================================================
    // POST /api/entity/update — Update entity properties
    // Body: { "id":"npc_01", "position":{"x":0,"y":0,"z":0},
    //         "rotation":{"w":1,"x":0,"y":0,"z":0},
    //         "scale":{"x":1,"y":1,"z":1},
    //         "debugColor":{"r":1,"g":0,"b":0,"a":1} }
    // ====================================================================
    srv.Post("/api/entity/update", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("update_entity", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/snapshot/create — Create a named snapshot of a region
    // Body: { "name":"before_castle", "x1":0,"y1":0,"z1":0, "x2":31,"y2":31,"z2":31 }
    // ====================================================================
    srv.Post("/api/snapshot/create", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("create_snapshot", params, 30000);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/snapshot/restore — Restore a named snapshot (undo)
    // Body: { "name":"before_castle" }
    // ====================================================================
    srv.Post("/api/snapshot/restore", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("restore_snapshot", params, 60000);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // DELETE /api/snapshot — Delete a named snapshot
    // Body: { "name":"before_castle" }
    // ====================================================================
    srv.Post("/api/snapshot/delete", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("delete_snapshot", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/snapshots — List all snapshots (read-only, no voxel data)
    // ====================================================================
    srv.Get("/api/snapshots", [this](const httplib::Request&, httplib::Response& res) {
        if (m_snapshotListHandler) {
            json result = m_snapshotListHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Snapshot list handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/clipboard/copy — Copy a region into the clipboard
    // Body: { "x1":0,"y1":0,"z1":0, "x2":15,"y2":15,"z2":15 }
    // ====================================================================
    srv.Post("/api/clipboard/copy", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("copy_region", params, 30000);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/clipboard/paste — Paste clipboard at a new position
    // Body: { "x":50,"y":0,"z":50, "rotate":0 }
    // rotate: 0, 90, 180, 270 degrees clockwise around Y
    // ====================================================================
    srv.Post("/api/clipboard/paste", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("paste_region", params, 60000);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GET /api/clipboard — Get clipboard info (read-only)
    // ====================================================================
    srv.Get("/api/clipboard", [this](const httplib::Request&, httplib::Response& res) {
        if (m_clipboardInfoHandler) {
            json result = m_clipboardInfoHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Clipboard info handler not configured"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });
}

// ============================================================================
// Queue-and-Wait Helper
// ============================================================================

json EngineAPIServer::queueAndWait(const std::string& action, const json& params, int timeoutMs) {
    auto promise = std::make_shared<std::promise<json>>();
    auto future = promise->get_future();

    APICommand cmd;
    cmd.action = action;
    cmd.params = params;
    cmd.requestId = m_nextRequestId.fetch_add(1);
    cmd.onComplete = [promise](json response) {
        promise->set_value(std::move(response));
    };

    m_commandQueue->push(std::move(cmd));

    // Wait for the game loop to process the command
    auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));
    if (status == std::future_status::timeout) {
        return json{{"error", "Request timed out waiting for game loop"},
                    {"action", action},
                    {"timeout_ms", timeoutMs}};
    }

    return future.get();
}

} // namespace Core
} // namespace Phyxel
