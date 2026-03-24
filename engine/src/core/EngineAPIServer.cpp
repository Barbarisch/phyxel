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

EngineAPIServer::EngineAPIServer(APICommandQueue* commandQueue, int port, JobSystem* jobSystem)
    : m_commandQueue(commandQueue)
    , m_jobSystem(jobSystem)
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
        LOG_WARN("EngineAPIServer", "Server already running on port {}", m_port);
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
        LOG_INFO("EngineAPIServer", "HTTP API server started on port {}", m_port);
    } else {
        LOG_ERROR("EngineAPIServer", "Failed to start HTTP API server on port {}", m_port);
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
    // POST /api/entity/damage — Deal damage to an entity
    // Body: { "id": "npc_01", "amount": 25.0, "source": "player" }
    // ====================================================================
    srv.Post("/api/entity/damage", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("damage_entity", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/entity/heal — Heal an entity
    // Body: { "id": "npc_01", "amount": 30.0 }
    // ====================================================================
    srv.Post("/api/entity/heal", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("heal_entity", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/entity/set_health — Set entity health directly
    // Body: { "id": "npc_01", "health": 50.0 } or { "id": "npc_01", "maxHealth": 200.0 }
    // ====================================================================
    srv.Post("/api/entity/set_health", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("set_entity_health", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/entity/kill — Kill an entity
    // Body: { "id": "npc_01" }
    // ====================================================================
    srv.Post("/api/entity/kill", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("kill_entity", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/entity/revive — Revive a dead entity
    // Body: { "id": "npc_01", "healthPercent": 0.5 }
    // ====================================================================
    srv.Post("/api/entity/revive", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("revive_entity", params);
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
            std::string asyncId = queueAsync("fill_region", params);
            json response = {{"status", "accepted"}, {"async_id", asyncId}, {"action", "fill_region"}};
            res.set_content(response.dump(), "application/json");
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
            std::string asyncId = queueAsync("clear_region", params);
            json response = {{"status", "accepted"}, {"async_id", asyncId}, {"action", "clear_region"}};
            res.set_content(response.dump(), "application/json");
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

    // ====================================================================
    // POST /api/world/generate — Generate terrain for chunk(s)
    // Body: { "type":"Perlin", "seed":42,
    //         "chunks":[{"x":0,"y":0,"z":0}],
    //         "params":{"heightScale":16, "frequency":0.05} }
    // OR:   { "type":"Mountains", "seed":42,
    //         "from":{"x":0,"y":0,"z":0}, "to":{"x":2,"y":0,"z":2} }
    // Returns: { "status": "accepted", "async_id": "123" }
    // Poll GET /api/async/123 for result
    // ====================================================================
    srv.Post("/api/world/generate", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            std::string asyncId = queueAsync("generate_world", params);
            json response = {{"status", "accepted"}, {"async_id", asyncId}, {"action", "generate_world"}};
            res.set_content(response.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // POST /api/template/save — Save a region as a reusable template
    // Body: { "name":"my_tower", "x1":0,"y1":0,"z1":0, "x2":10,"y2":20,"z2":10 }
    // ====================================================================
    srv.Post("/api/template/save", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("save_template", params, 30000);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // NPC MANAGEMENT
    // ====================================================================

    // POST /api/npc/spawn — Spawn an NPC
    srv.Post("/api/npc/spawn", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("spawn_npc", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/npc/remove — Remove an NPC
    srv.Post("/api/npc/remove", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("remove_npc", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/npcs — List all NPCs
    srv.Get("/api/npcs", [this](const httplib::Request&, httplib::Response& res) {
        json result = queueAndWait("list_npcs", json::object());
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/npc/behavior — Set NPC behavior
    srv.Post("/api/npc/behavior", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("set_npc_behavior", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // DIALOGUE & SPEECH BUBBLES
    // ====================================================================

    // POST /api/npc/dialogue — Set dialogue tree on an NPC
    srv.Post("/api/npc/dialogue", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("set_npc_dialogue", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/dialogue/start — Start a dialogue conversation
    srv.Post("/api/dialogue/start", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("start_dialogue", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/dialogue/end — End active dialogue
    srv.Post("/api/dialogue/end", [this](const httplib::Request& req, httplib::Response& res) {
        json result = queueAndWait("end_dialogue", json::object());
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/speech/say — Create a speech bubble above an entity
    srv.Post("/api/speech/say", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("say_bubble", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/dialogue/state — Get current dialogue conversation state
    srv.Get("/api/dialogue/state", [this](const httplib::Request&, httplib::Response& res) {
        json result = queueAndWait("get_dialogue_state", json::object());
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/dialogue/load — Load a dialogue file from resources/dialogues/
    srv.Post("/api/dialogue/load", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("load_dialogue_file", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/dialogue/files — List available dialogue files
    srv.Get("/api/dialogue/files", [this](const httplib::Request&, httplib::Response& res) {
        json result = queueAndWait("list_dialogue_files", json::object());
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/dialogue/advance — Advance dialogue (skip typewriter or next node)
    srv.Post("/api/dialogue/advance", [this](const httplib::Request&, httplib::Response& res) {
        json result = queueAndWait("advance_dialogue", json::object());
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/dialogue/choice — Select a dialogue choice
    srv.Post("/api/dialogue/choice", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("select_dialogue_choice", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ========================================================================
    // Story System Routes
    // ========================================================================

    // GET /api/story/state — Full story engine state (saveState)
    srv.Get("/api/story/state", [this](const httplib::Request&, httplib::Response& res) {
        if (m_storyStateHandler) {
            json result = m_storyStateHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Story system not available"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/story/characters — List all story character IDs
    srv.Get("/api/story/characters", [this](const httplib::Request&, httplib::Response& res) {
        if (m_storyCharacterListHandler) {
            json result = m_storyCharacterListHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Story system not available"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/story/character/:id — Get character detail by ID
    srv.Get(R"(/api/story/character/(\w+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_storyCharacterDetailHandler) {
            std::string id = req.matches[1];
            json result = m_storyCharacterDetailHandler(id);
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Story system not available"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/story/arcs — List all story arcs
    srv.Get("/api/story/arcs", [this](const httplib::Request&, httplib::Response& res) {
        if (m_storyArcListHandler) {
            json result = m_storyArcListHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Story system not available"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/story/arc/:id — Get arc detail by ID
    srv.Get(R"(/api/story/arc/(\w+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (m_storyArcDetailHandler) {
            std::string id = req.matches[1];
            json result = m_storyArcDetailHandler(id);
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Story system not available"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/story/world — World state (factions, locations, variables)
    srv.Get("/api/story/world", [this](const httplib::Request&, httplib::Response& res) {
        if (m_storyWorldHandler) {
            json result = m_storyWorldHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            json err = {{"error", "Story system not available"}};
            res.status = 503;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/story/load — Load world definition from JSON
    srv.Post("/api/story/load", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("story_load_world", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/story/character/add — Add a character
    srv.Post("/api/story/character/add", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("story_add_character", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/story/character/remove — Remove a character
    srv.Post("/api/story/character/remove", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("story_remove_character", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/story/event — Trigger a world event
    srv.Post("/api/story/event", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("story_trigger_event", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/story/arc/add — Add a story arc
    srv.Post("/api/story/arc/add", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("story_add_arc", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/story/variable — Set a world variable
    srv.Post("/api/story/variable", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("story_set_variable", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/story/agency — Set character agency level
    srv.Post("/api/story/agency", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("story_set_agency", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/story/knowledge — Add starting knowledge to a character
    srv.Post("/api/story/knowledge", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("story_add_knowledge", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // GAME DEFINITION (AI Game Development)
    // ====================================================================

    // POST /api/game/load_definition — Load a complete game from a single JSON definition
    // Returns immediately with async_id; poll GET /api/async/:id for result
    srv.Post("/api/game/load_definition", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            std::string asyncId = queueAsync("load_game_definition", params);
            json response = {{"status", "accepted"}, {"async_id", asyncId}, {"action", "load_game_definition"}};
            res.set_content(response.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // GET /api/game/export_definition — Export current game state as a game definition
    srv.Get("/api/game/export_definition", [this](const httplib::Request&, httplib::Response& res) {
        json result = queueAndWait("export_game_definition", json{}, 10000);
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/game/validate_definition — Validate a game definition without loading
    srv.Post("/api/game/validate_definition", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("validate_game_definition", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // POST /api/game/create_npc — Composite: spawn NPC + dialogue + story character in one call
    srv.Post("/api/game/create_npc", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("create_game_npc", params, 30000);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            json err = {{"error", "Invalid JSON"}, {"detail", e.what()}};
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ====================================================================
    // PROJECT BUILD (engine-as-editor workflow)
    // ====================================================================

    // GET /api/project/info — Project metadata (dir, game.json, etc.)
    srv.Get("/api/project/info", [this](const httplib::Request&, httplib::Response& res) {
        json result = queueAndWait("project_info", json::object());
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/project/build — Build the game project via cmake
    // Uses JobSystem if available so the game loop stays responsive.
    // The HTTP thread blocks until the build finishes (up to 5 min).
    srv.Post("/api/project/build", [this](const httplib::Request& req, httplib::Response& res) {
        json params = json::object();
        if (!req.body.empty()) {
            try { params = json::parse(req.body); }
            catch (...) {} // use defaults if body not valid JSON
        }

        if (m_jobSystem) {
            // Submit build as a background job via the main thread
            json submitResult = queueAndWait("job_submit",
                json{{"type", "project_build"}, {"params", params}}, 5000);

            if (!submitResult.value("success", false)) {
                res.set_content(submitResult.dump(), "application/json");
                return;
            }

            uint64_t jobId = submitResult["job_id"].get<uint64_t>();

            // Poll on the HTTP thread until complete (doesn't block game loop)
            for (;;) {
                auto status = m_jobSystem->getJobStatus(jobId);
                if (!status) {
                    res.set_content(json{{"success", false},
                        {"error", "Build job lost"}}.dump(), "application/json");
                    return;
                }
                if (status->state == JobState::Complete ||
                    status->state == JobState::Failed ||
                    status->state == JobState::Cancelled) {
                    res.set_content(status->result.dump(), "application/json");
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        } else {
            // Fallback: synchronous build on game loop thread
            json result = queueAndWait("project_build", params, 300000);
            res.set_content(result.dump(), "application/json");
        }
    });

    // POST /api/project/run — Launch the built game executable
    srv.Post("/api/project/run", [this](const httplib::Request& req, httplib::Response& res) {
        json params = json::object();
        if (!req.body.empty()) {
            try { params = json::parse(req.body); }
            catch (...) {}
        }
        json result = queueAndWait("project_run", params);
        res.set_content(result.dump(), "application/json");
    });

    // GET /api/projects/list — List all discovered + recent projects
    srv.Get("/api/projects/list", [this](const httplib::Request&, httplib::Response& res) {
        json result = queueAndWait("projects_list", json::object());
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/projects/create — Scaffold a new empty project
    srv.Post("/api/projects/create", [this](const httplib::Request& req, httplib::Response& res) {
        json params = json::object();
        if (!req.body.empty()) {
            try { params = json::parse(req.body); }
            catch (...) {}
        }
        json result = queueAndWait("projects_create", params);
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/projects/open — Open/switch to a project
    srv.Post("/api/projects/open", [this](const httplib::Request& req, httplib::Response& res) {
        json params = json::object();
        if (!req.body.empty()) {
            try { params = json::parse(req.body); }
            catch (...) {}
        }
        json result = queueAndWait("projects_open", params);
        res.set_content(result.dump(), "application/json");
    });

    // ====================================================================
    // INVENTORY ENDPOINTS
    // ====================================================================

    // GET /api/inventory — Get inventory state (all slots, hotbar, selected)
    srv.Get("/api/inventory", [this](const httplib::Request&, httplib::Response& res) {
        if (m_inventoryHandler) {
            json result = m_inventoryHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            res.status = 503;
            res.set_content(json{{"error", "Inventory handler not configured"}}.dump(), "application/json");
        }
    });

    // POST /api/inventory/give — Give items to player
    srv.Post("/api/inventory/give", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("inventory_give", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/inventory/take — Remove items from player
    srv.Post("/api/inventory/take", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("inventory_take", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/inventory/select — Set selected hotbar slot
    srv.Post("/api/inventory/select", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("inventory_select", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/inventory/set_slot — Set a specific slot's contents
    srv.Post("/api/inventory/set_slot", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("inventory_set_slot", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/inventory/clear — Clear entire inventory
    srv.Post("/api/inventory/clear", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("inventory_clear", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/inventory/creative — Set creative mode on/off
    srv.Post("/api/inventory/creative", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("inventory_creative", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // ====================================================================
    // DAY/NIGHT CYCLE ENDPOINTS
    // ====================================================================

    // GET /api/daynight — Get current day/night cycle state
    srv.Get("/api/daynight", [this](const httplib::Request&, httplib::Response& res) {
        if (m_dayNightHandler) {
            json result = m_dayNightHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            res.status = 503;
            res.set_content(json{{"error", "Day/night handler not configured"}}.dump(), "application/json");
        }
    });

    // POST /api/daynight/set — Set day/night cycle properties
    srv.Post("/api/daynight/set", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("daynight_set", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // ====================================================================
    // LIGHTING ENDPOINTS
    // ====================================================================

    // GET /api/lights — List all lights and ambient level
    srv.Get("/api/lights", [this](const httplib::Request&, httplib::Response& res) {
        if (m_lightListHandler) {
            json result = m_lightListHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            res.status = 503;
            res.set_content(json{{"error", "Light handler not configured"}}.dump(), "application/json");
        }
    });

    // POST /api/light/point/add — Add a point light
    srv.Post("/api/light/point/add", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("add_point_light", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/light/spot/add — Add a spot light
    srv.Post("/api/light/spot/add", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("add_spot_light", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/light/remove — Remove a light by ID
    srv.Post("/api/light/remove", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("remove_light", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/light/update — Update a light's properties
    srv.Post("/api/light/update", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("update_light", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/ambient — Set ambient light strength
    srv.Post("/api/ambient", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("set_ambient", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // ====================================================================
    // AUDIO ENDPOINTS
    // ====================================================================

    // GET /api/audio/sounds — List available sound files
    srv.Get("/api/audio/sounds", [this](const httplib::Request&, httplib::Response& res) {
        if (m_soundListHandler) {
            json result = m_soundListHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            res.status = 503;
            res.set_content(json{{"error", "Sound handler not configured"}}.dump(), "application/json");
        }
    });

    // POST /api/audio/play — Play a sound (2D or 3D)
    srv.Post("/api/audio/play", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("play_sound", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/audio/volume — Set channel volume
    srv.Post("/api/audio/volume", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("set_volume", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // ====================================================================
    // JOB SYSTEM ENDPOINTS — Async background job management
    // ====================================================================

    // POST /api/job/submit — Submit a new background job
    // Body: {"type": "fill_region", "params": {...}}
    // Returns: {"job_id": 123, "status": "Pending"}
    srv.Post("/api/job/submit", [this](const httplib::Request& req, httplib::Response& res) {
        if (!m_jobSystem) {
            res.status = 503;
            res.set_content(json{{"error", "Job system not available"}}.dump(), "application/json");
            return;
        }

        json params;
        try {
            params = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON body"}}.dump(), "application/json");
            return;
        }

        if (!params.contains("type")) {
            res.status = 400;
            res.set_content(json{{"error", "Missing 'type' field"}}.dump(), "application/json");
            return;
        }

        std::string jobType = params["type"].get<std::string>();
        json jobParams = params.value("params", json::object());

        // Queue a command on the main thread to submit the job
        // (The main thread has access to ChunkManager and other state
        //  needed to construct the job's work function)
        json result = queueAndWait("job_submit", json{{"type", jobType}, {"params", jobParams}}, 5000);
        res.set_content(result.dump(), "application/json");
    });

    // GET /api/job/:id — Get job status/progress
    // Returns: {"job_id": 123, "state": "Running", "progress": 0.45, "message": "Filling voxels..."}
    srv.Get(R"(/api/job/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!m_jobSystem) {
            res.status = 503;
            res.set_content(json{{"error", "Job system not available"}}.dump(), "application/json");
            return;
        }

        uint64_t jobId = std::stoull(req.matches[1].str());
        auto status = m_jobSystem->getJobStatus(jobId);
        if (!status) {
            res.status = 404;
            res.set_content(json{{"error", "Job not found"}, {"job_id", jobId}}.dump(), "application/json");
            return;
        }

        res.set_content(status->toJson().dump(), "application/json");
    });

    // GET /api/jobs — List all active jobs
    // Returns: {"jobs": [{...}, {...}]}
    srv.Get("/api/jobs", [this](const httplib::Request&, httplib::Response& res) {
        if (!m_jobSystem) {
            res.status = 503;
            res.set_content(json{{"error", "Job system not available"}}.dump(), "application/json");
            return;
        }

        auto jobs = m_jobSystem->listJobs();
        json jobArray = json::array();
        for (auto& s : jobs) {
            jobArray.push_back(s.toJson());
        }
        res.set_content(json{{"jobs", jobArray}, {"count", jobArray.size()}}.dump(), "application/json");
    });

    // POST /api/job/:id/cancel — Cancel a running or pending job
    // Returns: {"job_id": 123, "cancelled": true}
    srv.Post(R"(/api/job/(\d+)/cancel)", [this](const httplib::Request& req, httplib::Response& res) {
        if (!m_jobSystem) {
            res.status = 503;
            res.set_content(json{{"error", "Job system not available"}}.dump(), "application/json");
            return;
        }

        uint64_t jobId = std::stoull(req.matches[1].str());
        bool cancelled = m_jobSystem->cancelJob(jobId);
        res.set_content(json{{"job_id", jobId}, {"cancelled", cancelled}}.dump(), "application/json");
    });

    // ====================================================================
    // Custom UI Menu Management
    // ====================================================================

    // GET /api/ui/menus — List all registered UI screens and visibility
    srv.Get("/api/ui/menus", [this](const httplib::Request&, httplib::Response& res) {
        if (m_menuListHandler) {
            json result = m_menuListHandler();
            res.set_content(result.dump(), "application/json");
        } else {
            res.status = 503;
            res.set_content(json{{"error", "Menu list handler not configured"}}.dump(), "application/json");
        }
    });

    // POST /api/ui/menu — Create a menu from JSON definition
    // Body: { "name": "settings", "definition": { ... MenuDefinition JSON ... } }
    srv.Post("/api/ui/menu", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("create_menu", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/ui/menu/show — Show a menu screen
    // Body: { "name": "settings" }
    srv.Post("/api/ui/menu/show", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("show_menu", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/ui/menu/hide — Hide a menu screen
    // Body: { "name": "settings" }
    srv.Post("/api/ui/menu/hide", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("hide_menu", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/ui/menu/toggle — Toggle a menu screen's visibility
    // Body: { "name": "settings" }
    srv.Post("/api/ui/menu/toggle", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("toggle_menu", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/ui/menu/remove — Remove a menu screen
    // Body: { "name": "settings" }
    srv.Post("/api/ui/menu/remove", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json params = json::parse(req.body);
            json result = queueAndWait("remove_menu", params);
            res.set_content(result.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 400;
            res.set_content(json{{"error", "Invalid JSON"}, {"detail", e.what()}}.dump(), "application/json");
        }
    });

    // ====================================================================
    // ASYNC RESULT POLLING
    // ====================================================================

    // GET /api/async/:id — Poll for async command result
    srv.Get(R"(/api/async/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string asyncId = req.matches[1].str();
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        auto it = m_asyncResults.find(asyncId);
        if (it == m_asyncResults.end()) {
            res.status = 404;
            res.set_content(json{{"error", "Async result not found"}, {"async_id", asyncId}}.dump(), "application/json");
        } else if (!it->second.complete) {
            res.set_content(json{{"status", "processing"}, {"async_id", asyncId}}.dump(), "application/json");
        } else {
            json result = it->second.result;
            result["status"] = "complete";
            result["async_id"] = asyncId;
            m_asyncResults.erase(it);
            res.set_content(result.dump(), "application/json");
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

// ============================================================================
// Queue-Async Helper (non-blocking)
// ============================================================================

std::string EngineAPIServer::queueAsync(const std::string& action, const json& params) {
    std::string asyncId = std::to_string(m_nextAsyncId.fetch_add(1));

    {
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        m_asyncResults[asyncId] = AsyncResult{false, {}};
    }

    APICommand cmd;
    cmd.action = action;
    cmd.params = params;
    cmd.requestId = m_nextRequestId.fetch_add(1);
    cmd.onComplete = [this, asyncId](json response) {
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        auto it = m_asyncResults.find(asyncId);
        if (it != m_asyncResults.end()) {
            it->second.complete = true;
            it->second.result = std::move(response);
        }
    };

    m_commandQueue->push(std::move(cmd));
    return asyncId;
}

} // namespace Core
} // namespace Phyxel
