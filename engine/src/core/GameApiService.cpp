#include "core/GameApiService.h"

#include "core/APICommandQueue.h"
#include "core/CommandRegistry.h"
#include "core/EngineAPIServer.h"
#include "core/EngineRuntime.h"
#include "core/GameSettings.h"     // Core::stringToKey
#include "core/NPCManager.h"
#include "core/NavGrid.h"
#include "core/AStarPathfinder.h"
#include "core/TriggerSystem.h"
#include "core/EntityRegistry.h"
#include "core/CombatDirector.h"
#include "core/CombatAISystem.h"
#include "core/PlayerTurnController.h"
#include "core/DiceSystem.h"
#include "core/CharacterSheet.h"
#include "core/Inventory.h"
#include "core/SceneManager.h"
#include "core/SceneDefinition.h"
#include "graphics/RenderCoordinator.h"
#include "graphics/Camera.h"
#include "input/InputManager.h"
#include "ui/GameScreen.h"
#include "ui/UISystem.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Logger.h"

#include <GLFW/glfw3.h>
#include "stb_image_write.h"
#include <cctype>
#include <chrono>
#include <filesystem>

namespace Phyxel {
namespace Core {

using nlohmann::json;

GameApiService::GameApiService() = default;
GameApiService::~GameApiService() { stop(); }

bool GameApiService::isRunning() const { return server_ && server_->isRunning(); }
int  GameApiService::port() const { return server_ ? server_->getPort() : 0; }

static const char* screenStateStr(UI::ScreenState s) {
    using S = UI::ScreenState;
    switch (s) {
        case S::Playing:          return "playing";
        case S::Paused:           return "paused";
        case S::MainMenu:         return "menu";
        case S::Inventory:        return "inventory";
        case S::Settings:         return "settings";
        case S::KeybindingRebind: return "settings";
        case S::Intro:            return "intro";
        case S::Loading:          return "loading";
        case S::Victory:          return "victory";
        case S::Credits:          return "credits";
    }
    return "unknown";
}

bool GameApiService::start(int port) {
    if (isRunning()) return false;
    if (!EngineAPIServer::isPortAvailable(port)) {
        LOG_WARN("GameApiService", "API port {} unavailable — test API not started", port);
        return false;
    }
    queue_ = std::make_unique<APICommandQueue>();
    registry_ = std::make_unique<CommandRegistry>();
    server_ = std::make_unique<EngineAPIServer>(queue_.get(), port);

    // --- Read-only handlers (run on the HTTP thread; must not mutate) ---------
    server_->setWorldStateHandler([this]() -> json {
        json entities = entityRegistry ? entityRegistry->toJson() : json::array();
        // Guarantee the player is present (harness looks for id == "player").
        bool hasPlayer = false;
        for (const auto& e : entities)
            if (e.value("id", std::string()) == "player") { hasPlayer = true; break; }
        if (!hasPlayer && playerProvider) {
            if (auto* p = playerProvider()) {
                const glm::vec3 pos = p->getPosition();
                entities.push_back({{"id", "player"}, {"type", "animated"},
                                    {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}}});
            }
        }
        json state = {{"entities", entities}, {"entity_count", entities.size()}};
        // Report the REAL rig-driven camera, not InputManager's free-cam copy —
        // a rig (third_person/overhead) repositions Graphics::Camera every frame
        // and never writes back to InputManager, so the copy goes stale the
        // moment gameplay cameras engage. (Found by the BG3 tactical-camera
        // probe: the API showed the boot pose through an entire rig swap.)
        if (runtime && runtime->getCamera()) {
            auto* cam = runtime->getCamera();
            const glm::vec3 c = cam->getPosition();
            state["camera"] = {{"position", {{"x", c.x}, {"y", c.y}, {"z", c.z}}},
                               {"yaw", cam->getYaw()}, {"pitch", cam->getPitch()}};
        } else if (runtime && runtime->getInputManager()) {
            auto* im = runtime->getInputManager();
            const glm::vec3 c = im->getCameraPosition();
            state["camera"] = {{"position", {{"x", c.x}, {"y", c.y}, {"z", c.z}}},
                               {"yaw", im->getYaw()}, {"pitch", im->getPitch()}};
        }
        return state;
    });

    server_->setEngineTimingHandler([this]() -> json {
        double fps = 0.0;
        if (runtime && runtime->getPerformanceMonitor()) {
            const auto& ft = runtime->getPerformanceMonitor()->getCurrentFrameTiming();
            if (ft.cpuFrameTime > 0.0) fps = 1000.0 / ft.cpuFrameTime;
            return {{"fps", fps}, {"cpuFrameTime", ft.cpuFrameTime}};
        }
        return {{"fps", fps}};
    });

    // /api/rpg/<action> (incl. combat/*) — bounce through the command queue so
    // the handlers run on the game-loop thread via pump(), same as every other
    // command. (The editor's rpg handler runs on the HTTP thread and must queue
    // player intents itself; here the queue does that uniformly.)
    server_->setRpgHandler([this](const std::string& action, const json& params) -> json {
        return server_->queueAndWait(action, params);
    });

    registerCommands();
    if (!server_->start()) {
        LOG_ERROR("GameApiService", "EngineAPIServer failed to start on port {}", port);
        server_.reset(); queue_.reset(); registry_.reset();
        return false;
    }
    LOG_INFO("GameApiService", "Standalone test API listening on 127.0.0.1:{} ({})",
             port, projectName.empty() ? "game" : projectName.c_str());
    return true;
}

void GameApiService::registerCommands() {
    auto& reg = *registry_;

    reg.on("get_render_stats", [this](const APICommand&, json& r) {
        if (!renderCoordinator) { r = {{"error", "RenderCoordinator not available"}}; return; }
        const auto& s = renderCoordinator->getLastFrameStats();
        r = {{"visible_chunk_count", s.visibleChunkCount},
             {"total_visible_faces", s.totalVisibleFaces},
             {"far_tiles_drawn", s.farTilesDrawn}};
    });

    reg.on("get_player_state", [this](const APICommand&, json& r) {
        Scene::AnimatedVoxelCharacter* ch = playerProvider ? playerProvider() : nullptr;
        if (!ch) { r = {{"success", false}, {"error", "No player character"}}; return; }
        const glm::vec3 p = ch->getPosition();
        const glm::vec3 v = ch->getControllerVelocity();
        r = {{"success", true},
             {"position", {{"x", p.x}, {"y", p.y}, {"z", p.z}}},
             {"velocity", {{"x", v.x}, {"y", v.y}, {"z", v.z}}},
             {"grounded", ch->isGrounded()},
             {"facing_yaw", ch->getYaw()},   // radians; model faces +Z at yaw 0
             {"state", ch->stateToString(ch->getAnimationState())}};
    });

    reg.on("list_triggers", [this](const APICommand&, json& r) {
        if (!triggers) { r = {{"error", "TriggerSystem not available"}}; return; }
        r = {{"success", true}, {"triggers", triggers->listTriggers()}};
    });

    reg.on("fire_trigger", [this](const APICommand& cmd, json& r) {
        if (!triggers) { r = {{"error", "TriggerSystem not available"}}; return; }
        const std::string id = cmd.params.value("id", "");
        const std::string evt = cmd.params.value("event", "");
        if (!id.empty()) {
            json list = triggers->listTriggers();
            const json* found = nullptr;
            for (const auto& t : list)
                if (t.value("id", std::string()) == id) { found = &t; break; }
            if (!found) { r = {{"success", false}, {"error", "No trigger with id '" + id + "'"}}; return; }
            json executed = json::array();
            if (found->contains("then") && (*found)["then"].is_array())
                for (const auto& a : (*found)["then"]) {
                    triggers->executeHostAction(a, id);
                    executed.push_back(a.value("type", std::string("?")));
                }
            r = {{"success", true}, {"id", id}, {"mode", "direct"}, {"executed", executed}};
            return;
        }
        if (!evt.empty()) {
            triggers->onEvent(evt, cmd.params.value("data", json::object()));
            r = {{"success", true}, {"event", evt}, {"mode", "event"}};
            return;
        }
        r = {{"success", false}, {"error", "Provide 'id' or 'event'"}};
    });

    // --- Turn-based combat (POST /api/rpg/combat/<action>) -------------------
    // Runs on the game-loop thread (pump()), so player intents apply directly —
    // no pending-intent mutex (contrast: editor Application.cpp rpg handler).
    reg.on("combat/state", [this](const APICommand&, json& r) {
        if (!combatDirector) { r = {{"error", "combat not available"}}; return; }
        r = {{"mode",           combatModeToString(combatDirector->mode())},
             {"in_combat",      combatDirector->inCombat()},
             {"active",         combatDirector->initiative().isCombatActive()},
             {"round",          combatDirector->currentRound()},
             {"current_entity", combatDirector->currentEntityId()},
             {"player_turn",    combatDirector->isPlayerTurn()},
             {"turn_order",     combatDirector->initiative().toJson()}};
    });

    reg.on("combat/start", [this](const APICommand& cmd, json& r) {
        if (!combatDirector) { r = {{"error", "combat not available"}}; return; }
        std::vector<CombatDirector::Combatant> combatants;
        if (cmd.params.contains("participants") && cmd.params["participants"].is_array())
            for (const auto& p : cmd.params["participants"]) {
                std::string eid = p.value("entity_id", "");
                if (eid.empty()) continue;
                CombatDirector::Combatant c;
                c.entityId        = eid;
                c.isPlayerSide    = p.value("player_side", false);
                c.initiativeBonus = p.value("initiative_bonus", 0);
                c.speed           = p.value("speed", 30);
                combatants.push_back(c);
            }
        if (combatDirector->inCombat()) combatDirector->endEncounter();
        DiceSystem dice;
        combatDirector->beginEncounter(combatants, dice);
        r = {{"ok", true}, {"state", combatDirector->toJson()}};
    });

    reg.on("combat/player_move", [this](const APICommand& cmd, json& r) {
        if (!playerTurn) { r = {{"error", "combat not available"}}; return; }
        glm::vec3 pt(cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f),
                     cmd.params.value("z", 0.0f));
        r = {{"ok", playerTurn->requestMove(pt)}};
    });

    reg.on("combat/player_attack", [this](const APICommand& cmd, json& r) {
        if (!playerTurn) { r = {{"error", "combat not available"}}; return; }
        const std::string tid = cmd.params.value("target_id", "");
        playerTurn->setSelectedTarget(tid);
        r = {{"ok", playerTurn->requestAttack(tid)}};
    });

    reg.on("combat/end_turn", [this](const APICommand&, json& r) {
        if (!playerTurn) { r = {{"error", "combat not available"}}; return; }
        playerTurn->endTurn();
        r = {{"ok", true}};
    });

    reg.on("combat/next_turn", [this](const APICommand&, json& r) {
        if (!combatDirector) { r = {{"error", "combat not available"}}; return; }
        if (!combatDirector->inCombat()) { r = {{"error", "no active combat"}}; return; }
        std::string next = combatDirector->advanceTurn();
        r = {{"ok", true}, {"next_entity", next}, {"round", combatDirector->currentRound()}};
    });

    reg.on("combat/end", [this](const APICommand&, json& r) {
        if (!combatDirector) { r = {{"error", "combat not available"}}; return; }
        combatDirector->endEncounter();
        r = {{"ok", true}};
    });

    reg.on("combat/set_mode", [this](const APICommand& cmd, json& r) {
        if (!combatDirector) { r = {{"error", "combat not available"}}; return; }
        combatDirector->setMode(combatModeFromString(cmd.params.value("mode", "real_time")));
        r = {{"ok", true}, {"mode", combatModeToString(combatDirector->mode())}};
    });

    // GET/POST /api/rpg/sheet — the player's character sheet (progression:
    // XP, level, classes, HP). Null until the host wires a sheet.
    reg.on("sheet", [this](const APICommand&, json& r) {
        if (!playerSheet) { r = {{"error", "character sheet not available"}}; return; }
        r = {{"success", true}, {"sheet", playerSheet->toJson()}};
    });

    // GET/POST /api/rpg/inventory — the player's inventory (loot/persistence).
    reg.on("inventory", [this](const APICommand&, json& r) {
        if (!inventory) { r = {{"error", "inventory not available"}}; return; }
        r = {{"success", true}, {"inventory", inventory->toJson()}};
    });

    // POST /api/rpg/ui_scroll {x, y, delta} — wheel input at a screen point
    // (delta > 0 = wheel up). Drives the same UISystem::handleScroll the
    // shipped game's real wheel uses.
    reg.on("ui_scroll", [this](const APICommand& cmd, json& r) {
        auto* ui = renderCoordinator ? renderCoordinator->getUISystem() : nullptr;
        if (!ui) { r = {{"error", "UISystem not available"}}; return; }
        const bool consumed = ui->handleScroll(
            {cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f)},
            cmd.params.value("delta", 0.0f));
        r = {{"ok", true}, {"consumed", consumed}};
    });

    // GET /api/screenshot — capture the current frame to screenshots/<ts>.png.
    // The pixel-verification unlock for shipped games: probes can now PROVE
    // rendering claims (HUD panels, text centering/clipping, menu animation =
    // two captures that differ) instead of stopping at "providers are live".
    reg.on("capture_screenshot", [this](const APICommand&, json& r) {
        if (!renderCoordinator) { r = {{"error", "RenderCoordinator not available"}}; return; }
        auto pixels = renderCoordinator->captureScreenshot();
        if (pixels.empty()) { r = {{"error", "Screenshot capture failed"}}; return; }
        const glm::uvec2 wh = renderCoordinator->getSwapChainSize();
        std::error_code ec;
        std::filesystem::create_directories("screenshots", ec);
        const auto now = std::chrono::system_clock::now();
        const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch()).count();
        const std::string path = "screenshots/shot_" + std::to_string(ms) + ".png";
        if (!stbi_write_png(path.c_str(), static_cast<int>(wh.x), static_cast<int>(wh.y),
                            4, pixels.data(), static_cast<int>(wh.x) * 4)) {
            r = {{"error", "Failed to write PNG"}};
            return;
        }
        r = {{"success", true}, {"path", path}, {"width", wh.x}, {"height", wh.y}};
    });

    // Click-to-act: resolve a SCREEN click into attack/move — the same
    // PlayerTurnController::requestPickAt the shipped game's LMB uses, so a
    // probe clicking the rat exercises the player's real path.
    reg.on("combat/player_pick", [this](const APICommand& cmd, json& r) {
        if (!playerTurn || !runtime) { r = {{"error", "combat not available"}}; return; }
        auto* cam = runtime->getCamera();
        auto* rc  = renderCoordinator;
        if (!cam || !rc) { r = {{"error", "camera not available"}}; return; }
        const glm::uvec2 vp = rc->getSwapChainSize();
        float groundY = cmd.params.value("ground_y", -10000.0f);
        if (groundY <= -9999.0f) {
            if (auto* p = playerProvider ? playerProvider() : nullptr)
                groundY = p->getPosition().y;
            else groundY = 0.0f;
        }
        const char* resolved = playerTurn->requestPickAt(
            *cam, {cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f)},
            {static_cast<float>(vp.x), static_cast<float>(vp.y)}, groundY);
        r = {{"ok", true}, {"resolved", resolved}};
    });

    reg.on("combat/screen_of", [this](const APICommand& cmd, json& r) {
        if (!playerTurn || !runtime) { r = {{"error", "combat not available"}}; return; }
        auto* cam = runtime->getCamera();
        auto* rc  = renderCoordinator;
        if (!cam || !rc) { r = {{"error", "camera not available"}}; return; }
        const glm::uvec2 vp = rc->getSwapChainSize();
        glm::vec2 px;
        if (!playerTurn->screenOf(*cam, cmd.params.value("entity_id", ""),
                                  {static_cast<float>(vp.x), static_cast<float>(vp.y)}, px)) {
            r = {{"ok", false}, {"error", "entity unknown or off-screen"}};
            return;
        }
        r = {{"ok", true}, {"x", px.x}, {"y", px.y}};
    });

    reg.on("combat/targeting_info", [this](const APICommand& cmd, json& r) {
        if (!playerTurn) { r = {{"error", "combat not available"}}; return; }
        const std::string tid = cmd.params.value("target_id", "");
        r = {{"target_id",    tid},
             {"attack_bonus", playerTurn->attackBonus()},
             {"target_ac",    playerTurn->targetAC(tid)},
             {"hit_chance",   playerTurn->hitChanceVs(tid)},
             {"distance",     playerTurn->distanceTo(tid)},
             {"in_reach",     playerTurn->inReachOf(tid)}};
    });

    reg.on("inject_input", [this](const APICommand& cmd, json& r) {
        auto* im = runtime ? runtime->getInputManager() : nullptr;
        if (!im) { r = {{"error", "InputManager not available"}}; return; }
        if (cmd.params.value("release_all", false)) {
            im->releaseAllInjected();
            r = {{"success", true}, {"released", true}, {"active_injections", im->injectedCount()}};
            return;
        }
        const float hold = cmd.params.value("hold", 0.1f);
        json injected = json::array(), unresolved = json::array();
        if (cmd.params.contains("keys") && cmd.params["keys"].is_array())
            for (const auto& k : cmd.params["keys"]) {
                if (!k.is_string()) continue;
                const std::string name = k.get<std::string>();
                int key = Core::stringToKey(name);
                if (key == GLFW_KEY_UNKNOWN) key = im->getActionKey(name);
                if (key != GLFW_KEY_UNKNOWN) { im->injectKey(key, hold); injected.push_back(name); }
                else unresolved.push_back(name);
            }
        if (cmd.params.contains("mouse") && cmd.params["mouse"].is_array())
            for (const auto& m : cmd.params["mouse"]) {
                if (!m.is_string()) continue;
                std::string up = m.get<std::string>();
                for (auto& c : up) c = static_cast<char>(::toupper(c));
                int btn = (up == "LEFT") ? GLFW_MOUSE_BUTTON_LEFT
                        : (up == "RIGHT") ? GLFW_MOUSE_BUTTON_RIGHT
                        : (up == "MIDDLE") ? GLFW_MOUSE_BUTTON_MIDDLE : -1;
                if (btn >= 0) { im->injectMouseButton(btn, hold); injected.push_back("Mouse" + up); }
                else unresolved.push_back(m.get<std::string>());
            }
        r = {{"success", true}, {"injected", injected}, {"hold", hold},
             {"active_injections", im->injectedCount()}};
        if (!unresolved.empty()) r["unresolved"] = unresolved;
    });

    reg.on("get_screen_state", [this](const APICommand&, json& r) {
        // The standalone shell owns a REAL GameScreen — report its actual state
        // (victory/credits/menu observable, unlike the editor's synthesized proxy).
        std::string sceneId, sceneType = "world";
        bool transitioning = false;
        auto* sm = runtime ? runtime->getSceneManager() : nullptr;
        if (sm && !sm->getActiveSceneId().empty()) {
            sceneId = sm->getActiveSceneId();
            transitioning = sm->isTransitioning();
            if (const auto* s = sm->getActiveScene()) {
                using ST = Core::SceneType;
                sceneType = (s->sceneType == ST::Menu) ? "menu"
                          : (s->sceneType == ST::Cutscene) ? "cutscene" : "world";
            }
        }
        json menus = json::array();
        if (renderCoordinator && renderCoordinator->getUISystem())
            for (auto& [name, visible] : renderCoordinator->getUISystem()->getScreenList())
                if (visible && name.rfind("hud_", 0) != 0) menus.push_back(name);
        std::string s = screen ? screenStateStr(screen->getState())
                               : (transitioning ? "loading" : "playing");
        r = {{"success", true}, {"screen", s}, {"scene_id", sceneId}, {"scene_type", sceneType},
             {"transitioning", transitioning}, {"visible_menus", menus}, {"source", "shell"}};
    });

    reg.on("ui_click", [this](const APICommand& cmd, json& r) {
        auto* ui = renderCoordinator ? renderCoordinator->getUISystem() : nullptr;
        if (!ui) { r = {{"error", "UISystem not available"}}; return; }
        float x = cmd.params.value("x", 0.0f), y = cmd.params.value("y", 0.0f);
        bool consumed = ui->injectClick(glm::vec2(x, y));  // click a menu/HUD button
        r = {{"success", true}, {"consumed", consumed}, {"x", x}, {"y", y}};
    });

    reg.on("navgrid_cell", [this](const APICommand& cmd, json& r) {
        if (!npcManager) { r = {{"error", "NPCManager not available"}}; return; }
        // Lazily build the NavGrid on first use (the world is loaded by now, and
        // pump() runs this on the game-loop thread so buildNavGrid is safe here).
        if (!npcManager->getNavGrid()) npcManager->buildNavGrid();
        if (!npcManager->getNavGrid()) { r = {{"error", "NavGrid unavailable"}}; return; }
        int x = cmd.params.value("x", 0), z = cmd.params.value("z", 0);
        const auto* cell = npcManager->getNavGrid()->getCell(x, z);
        if (cell) r = {{"x", cell->x}, {"z", cell->z}, {"walkable", cell->walkable},
                       {"surfaceY", cell->surfaceY}, {"nearWall", cell->nearWall}};
        else      r = {{"x", x}, {"z", z}, {"walkable", false}, {"message", "Cell not in grid"}};
    });

    reg.on("navgrid_path", [this](const APICommand& cmd, json& r) {
        if (!npcManager) { r = {{"error", "NPCManager not available"}}; return; }
        if (!npcManager->getNavGrid()) npcManager->buildNavGrid();  // lazy build (see navgrid_cell)
        if (!npcManager->getPathfinder()) { r = {{"error", "Pathfinder not available"}}; return; }
        int x1 = cmd.params.value("x1", 0), z1 = cmd.params.value("z1", 0);
        int x2 = cmd.params.value("x2", 0), z2 = cmd.params.value("z2", 0);
        auto res = npcManager->getPathfinder()->findPath(
            glm::vec3(x1 + 0.5f, 0.0f, z1 + 0.5f), glm::vec3(x2 + 0.5f, 0.0f, z2 + 0.5f));
        json wps = json::array();
        for (const auto& w : res.waypoints) wps.push_back({{"x", w.x}, {"y", w.y}, {"z", w.z}});
        r = {{"found", res.found}, {"waypoints", wps}, {"nodesExpanded", res.nodesExpanded}};
    });

    reg.on("project_info", [this](const APICommand&, json& r) {
        // A standalone has no source-project dir; report the game identity so a
        // harness can confirm it's driving the intended build.
        r = {{"standalone", true}, {"game", projectName},
             {"project_dir", projectName}};
    });
}

void GameApiService::pump() {
    if (!queue_ || !registry_) return;
    std::vector<APICommand> cmds;
    queue_->drainCommands(cmds);
    for (auto& cmd : cmds) {
        json response;
        try {
            if (!registry_->dispatch(cmd, response))
                response = {{"error", "unknown action: " + cmd.action}};
        } catch (const std::exception& e) {
            response = {{"error", std::string("handler threw: ") + e.what()}};
        }
        if (cmd.onComplete) cmd.onComplete(response);
    }
}

void GameApiService::stop() {
    if (server_) { server_->stop(); server_.reset(); }
    queue_.reset();
    registry_.reset();
}

} // namespace Core
} // namespace Phyxel
