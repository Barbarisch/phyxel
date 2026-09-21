#include "core/GameApiService.h"
#include "ui/DialogueSystem.h"

#include "core/APICommandQueue.h"
#include "core/CommandRegistry.h"
#include "core/EngineAPIServer.h"
#include "core/EngineRuntime.h"
#include "ui/WindowManager.h"
#include <GLFW/glfw3.h>
#include "core/GameSettings.h"     // Core::stringToKey
#include "core/NPCManager.h"
#include "core/NavGrid.h"
#include "core/AStarPathfinder.h"
#include "core/TriggerSystem.h"
#include "core/EntityRegistry.h"
#include "core/CombatDirector.h"
#include "core/CombatAISystem.h"
#include "core/PlayerTurnController.h"
#include "core/ClickToMove.h"
#include "core/DiceSystem.h"
#include "core/CharacterSheet.h"
#include "core/SpellcasterComponent.h"
#include "core/HealthComponent.h"
#include "core/CombatSystem.h"
#include "core/DamageTypes.h"
#include "core/CombatLog.h"
#include "core/AudioSystem.h"
#include "core/AmbienceDirector.h"
#include "core/AssetManager.h"
#include "scene/Entity.h"
#include <map>
#include <array>
#include "core/Inventory.h"
#include "core/SceneManager.h"
#include "core/SceneDefinition.h"
#include "graphics/RenderCoordinator.h"
#include "graphics/Camera.h"
#include "input/InputManager.h"
#include "ui/GameScreen.h"
#include "ui/UISystem.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/NPCEntity.h"
#include "scene/behaviors/CombatBehavior.h"
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
        case S::Character:        return "character";
        case S::Settings:         return "settings";
        case S::KeybindingRebind: return "settings";
        case S::Intro:            return "intro";
        case S::Loading:          return "loading";
        case S::Victory:          return "victory";
        case S::Credits:          return "credits";
        case S::GameOver:         return "game_over";
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
                                    {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}},
                                    {"yaw", p->getYaw()},                 // logical facing (rad)
                                    {"strafe_lean", p->getStrafeLean()}}); // drawn-body lean (rad, G-118)
            }
        }
        json state = {{"entities", entities}, {"entity_count", entities.size()}};
        // The player's facing + drawn-body lean (G-118) - not part of the registry's entity
        // JSON, so the harness can verify the run-strafe lean numerically.
        if (playerProvider) {
            if (auto* p = playerProvider())
                state["player"] = {{"yaw", p->getYaw()}, {"strafe_lean", p->getStrafeLean()}};
        }
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
             {"far_tiles_drawn", s.farTilesDrawn},
             // G-147: is the camera's own character currently suppressed because the camera sits
             // inside it? Exposed so "the body no longer clips in first person" is an assertion a
             // harness can make, rather than a human squinting at a screenshot.
             {"camera_owner_hidden", renderCoordinator->cameraOwnerHidden()},
             {"characters_drawn_main", renderCoordinator->getCharacterRenderStats().drawnMain}};
    });

    // Audio observability: listener pose + pool counts, straight from the real
    // AudioSystem (getListenerPosition reads back from miniaudio, not a cache).
    // This is what makes "the listener follows the camera in a SHIPPED game"
    // falsifiable — before the EngineRuntime::endFrame() wiring, packaged games
    // never updated the listener and had no way to even observe that.
    reg.on("get_audio_state", [this](const APICommand&, json& r) {
        Core::AudioSystem* audio = runtime ? runtime->getAudioSystem() : nullptr;
        if (!audio) { r = {{"error", "AudioSystem not available"}}; return; }
        const glm::vec3 lp = audio->getListenerPosition();
        r = {{"success", true},
             {"listener", {{"x", lp.x}, {"y", lp.y}, {"z", lp.z}}},
             {"active_sounds", audio->activeSoundCount()},
             {"pooled_sounds", audio->pooledSoundCount()},
             {"active_loops", audio->activeLoopCount()}};
        if (auto* amb = runtime->getAmbienceDirector()) {
            r["ambience"] = {{"context", amb->activeContext()},
                             {"crossfades", amb->crossfadeCount()}};
        }
    });

    // Fire a sound through the real AudioSystem (2D, or 3D when x/y/z given) so
    // a harness can exercise playback + pool behavior in the shipped build.
    reg.on("play_sound", [this](const APICommand& cmd, json& r) {
        Core::AudioSystem* audio = runtime ? runtime->getAudioSystem() : nullptr;
        if (!audio) { r = {{"error", "AudioSystem not available"}}; return; }
        const std::string file = cmd.params.value("file", "");
        if (file.empty()) { r = {{"error", "Missing 'file' field"}}; return; }
        const std::string path = Core::AssetManager::instance().resolveSound(file);
        const float volume = cmd.params.value("volume", 1.0f);
        if (cmd.params.contains("x") && cmd.params.contains("y") && cmd.params.contains("z")) {
            glm::vec3 pos(cmd.params["x"].get<float>(), cmd.params["y"].get<float>(),
                          cmd.params["z"].get<float>());
            audio->playSound3D(path, pos, AudioChannel::SFX, volume);
            r = {{"success", true}, {"mode", "3D"}, {"file", file}};
        } else {
            audio->playSound(path, AudioChannel::SFX, volume);
            r = {{"success", true}, {"mode", "2D"}, {"file", file}};
        }
        r["active_sounds"] = audio->activeSoundCount();
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
        if (auto* hc = ch->getHealthComponent()) {
            r["health"]     = hc->getHealth();
            r["max_health"] = hc->getMaxHealth();
        }
    });

    // POST /api/rpg/entity_health {id} — HP of ANY entity. Damage was
    // previously invisible to a harness: /api/state carries no HP and
    // get_player_state had none either, so "did that spell actually hurt
    // anyone" could only be inferred from logs.
    reg.on("entity_health", [this](const APICommand& cmd, json& r) {
        if (!entityRegistry) { r = {{"error", "EntityRegistry not available"}}; return; }
        const std::string id = cmd.params.value("id", "");
        Scene::Entity* e = id.empty() ? nullptr : entityRegistry->getEntity(id);
        if (!e) { r = {{"error", "unknown entity"}, {"id", id}}; return; }
        auto* hc = e->getHealthComponent();
        if (!hc) { r = {{"id", id}, {"has_health", false}}; return; }
        r = {{"id", id}, {"has_health", true},
             {"health", hc->getHealth()}, {"max_health", hc->getMaxHealth()},
             {"alive", hc->isAlive()}};
    });

    // POST /api/rpg/dialogue_state — what the player sees in the dialogue box: state,
    // speaker, node id, full text, and the VISIBLE choices (after conditions) with their
    // indices. A harness choosing options by blind index picked the wrong branch the moment
    // a gate changed (Ravenmere run 12: "tracks" instead of "relic") — G-47.
    reg.on("dialogue_state", [this](const APICommand&, json& r) {
        if (!dialogueSystem) { r = {{"error", "DialogueSystem not available"}}; return; }
        const auto st = dialogueSystem->getState();
        const char* stName = "inactive";
        switch (st) {
            case UI::DialogueState::Typing:               stName = "typing"; break;
            case UI::DialogueState::WaitingForInput:      stName = "waiting_for_input"; break;
            case UI::DialogueState::ChoiceSelection:      stName = "choice_selection"; break;
            case UI::DialogueState::AITextInput:          stName = "ai_text_input"; break;
            case UI::DialogueState::AIWaitingForResponse: stName = "ai_waiting"; break;
            default: break;
        }
        json choices = json::array();
        int idx = 0;
        for (const auto& c : dialogueSystem->getAvailableChoices())
            choices.push_back({{"index", idx++}, {"text", c.text}, {"target", c.targetNodeId},
                               {"skill_check", c.skillCheckJson.is_object()}});
        r = {{"active", dialogueSystem->isActive()}, {"state", stName},
             {"speaker", dialogueSystem->getCurrentSpeaker()}, {"node", dialogueSystem->getCurrentNodeId()},
             {"text", dialogueSystem->getCurrentText()}, {"choices", choices}};
    });

    // The load-time self-check (WorldHealth, WalkabilityGateAndPlaytestLoop layer B):
    // reachable anchors from the spawn + terrain under the spawn, as of the last scene ready.
    // POST /api/rpg/ui_lint - every child a visible HUD panel would cut off and every
    // pair of visible panels that overlap ({defects:[...], count}). The playtest
    // harness asserts count == 0 on each screen it visits.
    // POST /api/rpg/walk_to {x,y,z,standoff?} - click-to-move by API: the player walks
    // the NavGraph route to the point (what a mouse click on the ground does), so a
    // harness no longer steers with injected keys. Reply = whether a route exists.
    // POST /api/rpg/walk_status - {active, result, goal, walked, waypoints, next}.
    reg.on("walk_to", [this](const APICommand& cmd, json& r) {
        if (!clickToMove) { r = {{"error", "no click-to-move walker"}}; return; }
        const glm::vec3 goal(cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f),
                             cmd.params.value("z", 0.0f));
        const float standoff = cmd.params.value("standoff", 0.0f);
        const bool ok = clickToMove->requestWalkTo(goal, standoff);
        r = {{"success", ok}, {"result", ClickToMove::resultName(clickToMove->lastResult())},
             {"waypoints", clickToMove->waypoints().size()}};
    });
    // POST /api/rpg/pointer_click {x,y} - run the host's left-click handling at a pixel
    // (HUD widgets first, then NPC / ground click-to-move). The L4 for click-to-move.
    reg.on("pointer_click", [this](const APICommand& cmd, json& r) {
        if (!pointerClickProvider) { r = {{"error", "no pointer click provider"}}; return; }
        r = pointerClickProvider(cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f),
                                 cmd.params.value("button", std::string("left")));
        if (!r.contains("success")) r["success"] = true;
    });
    // POST /api/rpg/window_resize {w,h} - resize the game window (runs on the game loop).
    reg.on("window_resize", [this](const APICommand& cmd, json& r) {
        auto* wm = runtime ? runtime->getWindowManager() : nullptr;
        if (!wm || !wm->getHandle()) { r = {{"error", "no window"}}; return; }
        const int w = cmd.params.value("w", 1280), h = cmd.params.value("h", 720);
        glfwSetWindowSize(wm->getHandle(), w, h);
        r = {{"success", true}, {"w", w}, {"h", h}};
    });
    reg.on("walk_status", [this](const APICommand&, json& r) {
        if (!clickToMove) { r = {{"error", "no click-to-move walker"}}; return; }
        json wps = json::array();
        for (const auto& w : clickToMove->waypoints()) wps.push_back({{"x", w.x}, {"y", w.y}, {"z", w.z}});
        const auto& g = clickToMove->goal();
        r = {{"success", true}, {"active", clickToMove->active()},
             {"result", ClickToMove::resultName(clickToMove->lastResult())},
             {"goal", {{"x", g.x}, {"y", g.y}, {"z", g.z}}}, {"walked", clickToMove->walked()},
             {"waypoints", wps}, {"next", clickToMove->nextWaypoint()}};
    });
    reg.on("ui_lint", [this](const APICommand&, json& r) {
        if (!uiLintProvider) { r = {{"error", "no UI lint provider"}}; return; }
        json d = uiLintProvider();
        r = {{"success", true}, {"defects", d}, {"count", d.is_array() ? d.size() : 0}};
    });
    reg.on("world_health", [this](const APICommand&, json& r) {
        if (!worldHealthProvider) { r = {{"error", "no WorldHealth provider"}}; return; }
        json rep = worldHealthProvider();
        if (rep.is_null()) { r = {{"error", "no WorldHealth report yet (no world scene ready)"}}; return; }
        r = rep;
        r["success"] = true;
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

    // POST /api/rpg/combat/player_cast {spell_id, target_id} — cast on the
    // player's turn through PlayerTurnController::castSpell (budget spend,
    // cantrip scaling, save/attack-roll resolution, AoE, release-frame damage
    // via the host's cast executor). Same funnel a spell hotbar will use.
    reg.on("combat/player_cast", [this](const APICommand& cmd, json& r) {
        if (!playerTurn) { r = {{"error", "combat not available"}}; return; }
        const std::string spellId  = cmd.params.value("spell_id", "");
        const std::string targetId = cmd.params.value("target_id", "");
        if (spellId.empty()) { r = {{"error", "spell_id required"}}; return; }
        // Report WHY a refused cast was refused (no slots / not prepared /
        // action spent) — a bare false made "out of slots" look like a bug.
        const std::string blocked = playerTurn->castBlockedReason(spellId);
        const bool cast = playerTurn->castSpell(spellId, targetId);
        r = {{"ok", true}, {"cast", cast}};
        if (!cast && !blocked.empty()) r["blocked"] = blocked;
    });

    // POST /api/rpg/spellbook — the caster's live spell state: derived DC /
    // attack bonus, per-level slots, cantrips + prepared spells with the
    // castable reason for each. The observable behind slot enforcement.
    reg.on("spellbook", [this](const APICommand&, json& r) {
        if (!playerTurn) { r = {{"error", "combat not available"}}; return; }
        r = {{"save_dc", playerTurn->effectiveSaveDC()},
             {"spell_attack_bonus", playerTurn->effectiveSpellAttackBonus()}};
        auto* sc = playerTurn->spellcaster();
        if (!sc) { r["bound"] = false; return; }
        r["bound"] = true;
        r["casting_class"] = sc->castingClassId();
        json slots = json::array();
        for (int lvl = 1; lvl <= SpellSlots::MAX_SPELL_LEVEL; ++lvl) {
            const int mx = sc->slots().maximum[lvl - 1];
            if (mx > 0) slots.push_back({{"level", lvl},
                                         {"remaining", sc->slots().remaining[lvl - 1]},
                                         {"maximum", mx}});
        }
        r["slots"] = slots;
        json known = json::array();
        for (const auto& id : sc->cantrips())
            known.push_back({{"id", id}, {"cantrip", true},
                             {"blocked", playerTurn->castBlockedReason(id)}});
        for (const auto& ks : sc->knownSpells())
            known.push_back({{"id", ks.spellId}, {"cantrip", false},
                             {"prepared", ks.prepared},
                             {"blocked", playerTurn->castBlockedReason(ks.spellId)}});
        r["spells"] = known;
    });

    // POST /api/rpg/entity_damage {id, amount} — apply damage directly.
    // Test-harness affordance: AI reactions that only trigger in a narrow HP
    // window (a healer's threshold, a morale break) cannot be tested by hoping
    // the dice land there. This sets up the CONDITION deterministically; what
    // is under test is the AI's RESPONSE to it.
    reg.on("entity_damage", [this](const APICommand& cmd, json& r) {
        if (!entityRegistry) { r = {{"error", "EntityRegistry not available"}}; return; }
        const std::string id = cmd.params.value("id", "");
        Scene::Entity* e = id.empty() ? nullptr : entityRegistry->getEntity(id);
        if (!e) { r = {{"error", "unknown entity"}, {"id", id}}; return; }
        auto* hc = e->getHealthComponent();
        if (!hc) { r = {{"error", "entity has no health"}, {"id", id}}; return; }
        const float amount = cmd.params.value("amount", 0.0f);
        if (amount > 0.0f) {
            // Route through the FUNNEL, not hc->takeDamage: the funnel is what
            // raises death events, removes the combatant, and resolves the
            // encounter. Damaging the component directly left enemies at 0 HP
            // but "alive" to the CombatDirector, so the fight never ended
            // (measured — it wedged a whole probe run).
            if (combatSystem)
                combatSystem->applyDamage(e, id, amount, "test_api", DamageType::Physical);
            else
                hc->takeDamage(amount);
        }
        r = {{"id", id}, {"applied", amount},
             {"health", hc->getHealth()}, {"max_health", hc->getMaxHealth()},
             {"alive", hc->isAlive()}, {"via_funnel", combatSystem != nullptr}};
    });

    // POST /api/rpg/set_camera {x,y,z,yaw,pitch,detach} — park the camera at a
    // fixed pose (detach=true) or hand it back to the gameplay rig
    // (detach=false). The standalone had NO camera control at all, so a
    // harness could only photograph whatever was over the player's shoulder —
    // and an empty frame looked identical to a scene that failed to render.
    reg.on("set_camera", [this](const APICommand& cmd, json& r) {
        if (!cameraControl) { r = {{"error", "camera control not available"}}; return; }
        const bool detach = cmd.params.value("detach", true);
        cameraControl(detach,
                      glm::vec3(cmd.params.value("x", 0.0f),
                                cmd.params.value("y", 0.0f),
                                cmd.params.value("z", 0.0f)),
                      cmd.params.value("yaw", 0.0f),
                      cmd.params.value("pitch", 0.0f));
        r = {{"ok", true}, {"detached", detach}};
    });

    // POST /api/rpg/battle_stats — live roll-up of a REAL-TIME battle: who is
    // alive per faction, total/remaining HP, and the frame cost. The observable
    // for large-scale sims, where reading 40 individual entities per poll is
    // both slow and unreadable.
    reg.on("battle_stats", [this](const APICommand&, json& r) {
        if (!entityRegistry) { r = {{"error", "EntityRegistry not available"}}; return; }
        // faction -> {alive, dead, hp, max_hp}
        std::map<std::string, std::array<double, 4>> byFaction;
        int totalAlive = 0, totalDead = 0;
        for (const char* type : {"animated", "npc"}) {
            for (const auto& [id, e] : entityRegistry->getEntitiesByType(type)) {
                if (!e) continue;
                auto* hc = e->getHealthComponent();
                const std::string f = e->faction().empty() ? "(unaligned)" : e->faction();
                auto& slot = byFaction[f];
                const bool alive = hc && hc->isAlive();
                if (alive) { slot[0] += 1; ++totalAlive; } else { slot[1] += 1; ++totalDead; }
                if (hc) { slot[2] += hc->getHealth(); slot[3] += hc->getMaxHealth(); }
            }
        }
        json factions = json::array();
        for (const auto& [name, s] : byFaction)
            factions.push_back({{"faction", name}, {"alive", (int)s[0]}, {"dead", (int)s[1]},
                                {"hp", s[2]}, {"max_hp", s[3]}});
        r = {{"factions", factions}, {"alive", totalAlive}, {"dead", totalDead},
             {"combatants", totalAlive + totalDead}};
        if (runtime) {
            const float dt = runtime->getLastDeltaTime();
            r["frame_ms"] = dt * 1000.0f;
            r["fps"]      = dt > 0.0f ? 1.0f / dt : 0.0f;
        }
    });

    // POST /api/rpg/combat/log {since, limit} — the AI DECISION log: why each
    // combatant did what it did (targets weighed, tactic that fired, what was
    // rejected and on what grounds, roll outcomes). Separate from the engine
    // log on purpose. Poll with the returned next_index.
    reg.on("combat/log", [](const APICommand& cmd, json& r) {
        r = CombatLog::instance().toJson(cmd.params.value("since", 0u),
                                         cmd.params.value("limit", 200u));
    });

    // POST /api/rpg/tactics — what the combatants are actually DOING, per
    // faction: the distribution of tactical intents (engage / cover / flank /
    // hold / fall_back) across every live melee fighter. Without this,
    // "they take cover now" is an unfalsifiable claim about an invisible
    // state — this is the measurement that can come back all-"engage" and
    // prove the tactical layer never fired.
    reg.on("tactics", [this](const APICommand&, json& r) {
        if (!npcManager) { r = {{"error", "NPCManager not available"}}; return; }
        // faction -> intent -> count
        std::map<std::string, std::map<std::string, int>> byFaction;
        // Cumulative decision tallies per faction: cover taken / denied, orders
        // obeyed / ignored. The instantaneous intent census undercounts cover
        // badly (it is a transit state), so these carry the real signal.
        std::map<std::string, std::array<int, 4>> tallies;
        int tactical = 0, total = 0;
        npcManager->forEachNPC([&](Scene::NPCEntity& npc) {
            auto* cb = dynamic_cast<Scene::CombatBehavior*>(npc.getBehavior());
            if (!cb) return;
            const std::string f = npc.faction().empty() ? "(unaligned)" : npc.faction();
            // Tally the DEAD too — a soldier who took cover and then fell still
            // took cover, and dropping them would bias the count toward whoever
            // is winning.
            auto& t = tallies[f];
            t[0] += cb->coverTaken();    t[1] += cb->coverDenied();
            t[2] += cb->ordersObeyed();  t[3] += cb->ordersIgnored();

            auto* hc = npc.getHealthComponent();
            if (!hc || !hc->isAlive()) return;
            const std::string intent = cb->intentName();
            ++byFaction[f][intent];
            ++total;
            if (intent != "engage") ++tactical;
        });
        json factions = json::array();
        int coverTotal = 0, orderTotal = 0;
        for (const auto& [name, t] : tallies) {
            json counts = json::object();
            auto it = byFaction.find(name);
            if (it != byFaction.end())
                for (const auto& [intent, n] : it->second) counts[intent] = n;
            coverTotal += t[0];
            orderTotal += t[2];
            factions.push_back({{"faction", name}, {"intents", counts},
                                {"cover_taken", t[0]}, {"cover_denied", t[1]},
                                {"orders_obeyed", t[2]}, {"orders_ignored", t[3]}});
        }
        r = {{"factions", factions}, {"melee_alive", total},
             {"tactical", tactical},
             {"cover_taken", coverTotal}, {"orders_obeyed", orderTotal},
             {"tactical_fraction", total > 0 ? (double)tactical / total : 0.0}};
    });

    // POST /api/rpg/combat/log_clear — reset the decision log.
    reg.on("combat/log_clear", [](const APICommand&, json& r) {
        CombatLog::instance().clear();
        r = {{"ok", true}};
    });

    // POST /api/rpg/combat/ai_plan {entity_id} — what this NPC's tactical
    // profile would choose right now vs what a plain nearest-foe AI would.
    // When the two differ, the profile is provably doing the choosing.
    reg.on("combat/ai_plan", [this](const APICommand& cmd, json& r) {
        if (!combatAI) { r = {{"error", "combat AI not available"}}; return; }
        const std::string id = cmd.params.value("entity_id", "");
        if (id.empty()) { r = {{"error", "entity_id required"}}; return; }
        const auto p = combatAI->planFor(id);
        r = {{"entity_id", id},
             {"target_by_priority", p.targetByPriority},
             {"nearest", p.nearest},
             {"priority", p.priority},
             {"preferred_range_feet", p.preferredRangeFeet},
             {"flee_below_hp", p.fleeBelowHpFrac},
             {"heal_ally_below", p.healAllyBelowFrac},
             {"wounded_ally", p.woundedAlly}};
    });

    // POST /api/rpg/long_rest — restore all spell slots (the authoring hook is
    // the long_rest trigger action; this is its test-API twin).
    reg.on("long_rest", [this](const APICommand&, json& r) {
        auto* sc = playerTurn ? playerTurn->spellcaster() : nullptr;
        if (!sc) { r = {{"error", "no spellcaster bound"}}; return; }
        sc->onLongRest();
        r = {{"ok", true}, {"slots_remaining", sc->slots().totalRemaining()}};
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
        const glm::vec2 px{cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f)};
        const glm::vec2 vps{static_cast<float>(vp.x), static_cast<float>(vp.y)};
        const auto pick = playerTurn->resolvePick(*cam, px, vps, groundY);   // what the click sees
        const char* resolved = playerTurn->requestPickAt(*cam, px, vps, groundY);
        r = {{"ok", true}, {"resolved", resolved},
             {"point", {{"x", pick.point.x}, {"y", pick.point.y}, {"z", pick.point.z}}},
             {"target", pick.targetId}};
    });

    reg.on("combat/screen_of", [this](const APICommand& cmd, json& r) {
        if (!playerTurn || !runtime) { r = {{"error", "combat not available"}}; return; }
        auto* cam = runtime->getCamera();
        auto* rc  = renderCoordinator;
        if (!cam || !rc) { r = {{"error", "camera not available"}}; return; }
        const glm::uvec2 vp = rc->getSwapChainSize();
        glm::vec2 px;
        // y_offset (m above the feet) picks the point: 0 = feet, 0.9 = chest (default), 1.8 = head.
        if (!playerTurn->screenOf(*cam, cmd.params.value("entity_id", ""),
                                  {static_cast<float>(vp.x), static_cast<float>(vp.y)}, px,
                                  cmd.params.value("y_offset", 0.9f))) {
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
        // "mouse_move": [dx, dy] - a cursor delta through the same handler the OS cursor
        // uses (integrates into look only while the mouse is captured, i.e. a drag).
        if (cmd.params.contains("mouse_move") && cmd.params["mouse_move"].is_array() &&
            cmd.params["mouse_move"].size() >= 2) {
            double mx = 0.0, my = 0.0;
            im->getCurrentMousePosition(mx, my);
            im->handleMouseMove(mx + cmd.params["mouse_move"][0].get<double>(),
                                my + cmd.params["mouse_move"][1].get<double>());
            injected.push_back("MouseMove");
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

    // G-148: move the pointer without clicking and read back the tooltip that appears.
    // An automated run has no physical mouse, so without this the hover text is
    // unverifiable except by a human squinting at a screenshot.
    reg.on("ui_hover", [this](const APICommand& cmd, json& r) {
        auto* ui = renderCoordinator ? renderCoordinator->getUISystem() : nullptr;
        if (!ui) { r = {{"error", "UISystem not available"}}; return; }
        // No coordinates = READ the live hover state instead of injecting one. That is how
        // a harness checks what the REAL cursor is pointing at, which is the only way to
        // show the engine drew the tooltip rather than the harness supplying it.
        if (!cmd.params.contains("x")) {
            const std::string& live = ui->hoverTooltip();
            r = {{"success", true}, {"source", "live"},
                 {"tooltip", live}, {"has_tooltip", !live.empty()}};
            return;
        }
        float x = cmd.params.value("x", 0.0f), y = cmd.params.value("y", 0.0f);
        std::string tip = ui->injectHover(glm::vec2(x, y));
        r = {{"success", true}, {"x", x}, {"y", y}, {"source", "injected"},
             {"tooltip", tip}, {"has_tooltip", !tip.empty()}};
    });

    // G-150: a whole press-move-release drag. Points are window px, as ui_click takes.
    // Returns what actually happened so a harness asserts it instead of reading a
    // screenshot: whether anything was picked up, whether a target took it, and what
    // the payload was.
    reg.on("ui_drag", [this](const APICommand& cmd, json& r) {
        auto* ui = renderCoordinator ? renderCoordinator->getUISystem() : nullptr;
        if (!ui) { r = {{"error", "UISystem not available"}}; return; }
        const float fx = cmd.params.value("from_x", 0.0f), fy = cmd.params.value("from_y", 0.0f);
        const float tx = cmd.params.value("to_x", 0.0f),   ty = cmd.params.value("to_y", 0.0f);
        const auto res = ui->injectDrag(glm::vec2(fx, fy), glm::vec2(tx, ty));
        r = {{"success", true}, {"picked", res.picked}, {"dropped", res.dropped},
             {"payload", res.payload},
             {"from", {{"x", fx}, {"y", fy}}}, {"to", {{"x", tx}, {"y", ty}}}};
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

    // The 3D NavGraph route - what NPCs actually walk (the navgrid_path below is the
    // legacy 2.5D grid the harness used to steer with; it reads the topmost voxel as the
    // floor and cannot enter a building - Ravenmere G-60). Same shape as the editor's.
    reg.on("navgraph_path", [this](const APICommand& cmd, json& r) {
        if (!npcManager) { r = {{"error", "NPCManager not available"}}; return; }
        if (!npcManager->getNavGraph()) npcManager->buildNavGrid();
        auto* graph = npcManager->getNavGraph();
        if (!graph) { r = {{"error", "NavGraph not available"}}; return; }
        const float x1 = cmd.params.value("x1", 0.0f), y1 = cmd.params.value("y1", 17.0f);
        const float z1 = cmd.params.value("z1", 0.0f);
        const float x2 = cmd.params.value("x2", 0.0f), y2 = cmd.params.value("y2", 17.0f);
        const float z2 = cmd.params.value("z2", 0.0f);
        NavAgentProfile agent;
        auto result = graph->findPath(glm::vec3(x1, y1, z1), glm::vec3(x2, y2, z2), agent);
        json wps = json::array();
        if (result.found) {
            const auto smooth = result.waypoints.size() > 2
                ? graph->smoothWaypoints(result.waypoints, agent) : result.waypoints;
            for (const auto& w : smooth) wps.push_back({{"x", w.x}, {"y", w.y}, {"z", w.z}});
        }
        r = {{"found", result.found}, {"waypoints", wps}, {"nodesExpanded", result.nodesExpanded}};
    });

    reg.on("navgrid_path", [this](const APICommand& cmd, json& r) {
        // RETIRED as an oracle (WalkabilityGateAndPlaytestLoop increment 4, G-60): the 2.5D
        // NavGrid read the TOPMOST voxel as the floor and could not enter a building. The
        // command name stays for old harnesses but is answered by the sub-cube NavGraph -
        // the structure NPCs actually walk - and says so.
        if (!npcManager) { r = {{"error", "NPCManager not available"}}; return; }
        if (!npcManager->getNavGraph()) npcManager->buildNavGrid();
        auto* graph = npcManager->getNavGraph();
        if (!graph) { r = {{"error", "NavGraph not available"}}; return; }
        const float x1 = cmd.params.value("x1", 0.0f), y1 = cmd.params.value("y1", 17.0f);
        const float z1 = cmd.params.value("z1", 0.0f);
        const float x2 = cmd.params.value("x2", 0.0f), y2 = cmd.params.value("y2", 17.0f);
        const float z2 = cmd.params.value("z2", 0.0f);
        NavAgentProfile agent;
        auto result = graph->findPath(glm::vec3(x1, y1, z1), glm::vec3(x2, y2, z2), agent);
        json wps = json::array();
        if (result.found) {
            const auto smooth = result.waypoints.size() > 2
                ? graph->smoothWaypoints(result.waypoints, agent) : result.waypoints;
            for (const auto& w : smooth) wps.push_back({{"x", w.x}, {"y", w.y}, {"z", w.z}});
        }
        r = {{"found", result.found}, {"waypoints", wps}, {"nodesExpanded", result.nodesExpanded},
             {"legacy", true}, {"backed_by", "navgraph"}};
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
