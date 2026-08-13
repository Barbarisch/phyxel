#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Phyxel {
namespace Graphics { class RenderCoordinator; }
namespace Scene { class AnimatedVoxelCharacter; }
namespace UI { class GameScreen; }
namespace Core {

class EngineRuntime;
class NPCManager;
class TriggerSystem;
class EntityRegistry;
class APICommandQueue;
class EngineAPIServer;
class CommandRegistry;
class CombatDirector;
class CombatAISystem;
class PlayerTurnController;
class CharacterSheet;

// ============================================================================
// GameApiService — opt-in HTTP API host for STANDALONE (packaged) games.
//
// The same EngineAPIServer the editor runs, but wired against a standalone
// game's OWN subsystems, so an automated harness (the game-production runtime
// validators + adversarial playtest) can drive and observe the REAL shipped
// game — not the editor proxy. This closes the editor-vs-standalone fidelity
// gap (docs/game-production/README.md §6.6): the standalone owns a real
// GameScreen/ScreenState, so a win actually shows the Victory screen (in the
// editor host `show_victory` no-ops), letting win/lose validation reach L4.
//
// SAFETY: dev/test only. The host must gate start() behind an explicit flag
// (e.g. `--test`), and the server binds to localhost. Never enable in a build
// a player runs.
//
// Non-owning by design: the host game assigns the subsystem pointers it has
// (any may stay null — the corresponding handlers then report "not available"
// rather than crash), then calls start(port). pump() MUST run once per frame
// from the game's onUpdate to drain queued commands on the game-loop thread.
// stop() in onShutdown.
//
// Endpoints served (the subset the harness needs): GET /api/status (built-in),
// /api/state, /api/debug/engine_timing, and the queued commands get_render_stats,
// inject_input, get_screen_state, list_triggers, fire_trigger, get_player_state,
// navgrid_cell, navgrid_path, project_info.
// ============================================================================
class GameApiService {
public:
    GameApiService();
    ~GameApiService();
    GameApiService(const GameApiService&) = delete;
    GameApiService& operator=(const GameApiService&) = delete;

    // --- Subsystem wiring: set what you have BEFORE start(); any may be null. ---
    EngineRuntime*               runtime = nullptr;           // InputManager, SceneManager, PerfMonitor, Camera
    Graphics::RenderCoordinator* renderCoordinator = nullptr; // render stats + UISystem menus
    NPCManager*                  npcManager = nullptr;        // NavGrid reachability
    TriggerSystem*               triggers = nullptr;          // list/fire triggers
    UI::GameScreen*              screen = nullptr;            // REAL screen state
    EntityRegistry*              entityRegistry = nullptr;    // /api/state entities
    // The player character can be rebuilt/reassigned across scenes, so resolve it
    // fresh each call rather than caching a pointer.
    std::function<Scene::AnimatedVoxelCharacter*()> playerProvider;
    // Turn-based combat (all-or-nothing trio; null = combat endpoints report
    // "not available"). Commands run on the game-loop thread via pump(), so
    // handlers may call these directly — no intent mutex (unlike the editor's
    // HTTP-thread rpg handler, which must queue intents).
    CombatDirector*       combatDirector = nullptr;
    CombatAISystem*       combatAI = nullptr;
    PlayerTurnController* playerTurn = nullptr;
    CharacterSheet*       playerSheet = nullptr;   // progression: /api/rpg/sheet command
    std::string projectName;  // reported by project_info (identifies the running game)

    // Construct the queue+server, wire handlers, and start listening on `port`.
    // Returns false if already running or the port is unavailable.
    bool start(int port);
    // Drain + dispatch queued commands on the game-loop thread. Call once per
    // frame from onUpdate. No-op if not running.
    void pump();
    void stop();
    bool isRunning() const;
    int  port() const;

private:
    void registerCommands();

    std::unique_ptr<APICommandQueue> queue_;
    std::unique_ptr<EngineAPIServer> server_;
    std::unique_ptr<CommandRegistry> registry_;
};

} // namespace Core
} // namespace Phyxel
