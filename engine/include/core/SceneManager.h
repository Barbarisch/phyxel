#pragma once

#include "core/SceneDefinition.h"
#include "core/GameDefinitionLoader.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace Phyxel {
namespace Core {

/// State of the SceneManager's transition state machine.
enum class SceneState {
    Idle,           ///< No scene loaded or steady state with active scene
    Unloading,      ///< Currently unloading the active scene
    Loading,        ///< Currently loading the target scene
    Ready           ///< Scene loaded and running
};

/// Callbacks that the SceneManager uses to communicate with the host
/// (Application or standalone game). These decouple the SceneManager
/// from editor-specific state like the entity vector and player pointer.
struct SceneCallbacks {
    /// Called during unload to clear all application-owned entities.
    /// Should clear entity vectors, null player pointers, etc.
    std::function<void()> clearEntities;

    /// Called during unload to end any active dialogue.
    std::function<void()> endDialogue;

    /// Called during unload to remove all NPCs.
    std::function<void()> clearNPCs;

    /// Called during unload to clear scene-local story characters
    /// (but NOT global arcs or world state).
    std::function<void()> clearSceneStoryCharacters;

    /// Called during unload to reset lighting/day-night to defaults.
    std::function<void()> resetLighting;

    /// Called during unload to stop and clear music playlist.
    std::function<void()> clearMusic;

    /// Called during unload to clear objectives.
    std::function<void()> clearObjectives;

    /// Called when the loading screen should be shown/hidden.
    std::function<void(bool show, const std::string& sceneName)> setLoadingScreen;

    /// Called after loading completes to rebuild navigation grids.
    std::function<void()> rebuildNavGrid;

    /// Called to execute a Python script by path.
    std::function<void(const std::string& scriptPath)> runScript;

    /// Called when a scene transition completes (for GameCallbacks notification).
    std::function<void(const std::string& sceneId)> onSceneReady;
};

/// Result of a scene transition attempt.
struct SceneTransitionResult {
    bool success = false;
    std::string error;
    std::string fromScene;
    std::string toScene;
    float transitionTimeMs = 0.0f;
};

// ============================================================================
// SceneManager
//
// Engine-level orchestrator for multi-scene games. Manages the scene registry,
// active scene, and transition lifecycle (unload → loading screen → load).
//
// The StoryEngine and player state (health, inventory, equipment) persist
// across scene transitions. Per-scene state (chunks, entities, NPCs, lights,
// objectives, locations) is destroyed and recreated during transitions.
//
// Lives in EngineRuntime so standalone games can use it too.
// ============================================================================
class SceneManager {
public:
    SceneManager();
    ~SceneManager();

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Load a scene manifest (parsed from game.json with "scenes" key).
    /// Registers all scenes but does not load any yet.
    void loadManifest(const SceneManifest& manifest);

    /// Set the worlds directory for resolving scene database paths.
    void setWorldsDir(const std::string& worldsDir) { worldsDir_ = worldsDir; }

    /// Set the subsystems pointer used for GameDefinitionLoader.
    void setSubsystems(GameSubsystems* subsystems) { subsystems_ = subsystems; }

    /// Set callbacks for host-specific operations.
    void setCallbacks(const SceneCallbacks& callbacks) { callbacks_ = callbacks; }

    // ========================================================================
    // Scene transitions
    // ========================================================================

    /// Begin a transition to the named scene. Returns false if the scene ID
    /// is unknown or a transition is already in progress.
    bool transitionTo(const std::string& sceneId);

    /// Load the manifest's start scene. Convenience for initial startup.
    bool loadStartScene();

    /// Drive the transition state machine. Call once per frame.
    void update(float deltaTime);

    // ========================================================================
    // Queries
    // ========================================================================

    /// Current state of the transition state machine.
    SceneState getState() const { return state_; }

    /// True if a transition is in progress (Unloading or Loading).
    bool isTransitioning() const {
        return state_ == SceneState::Unloading || state_ == SceneState::Loading;
    }

    /// ID of the currently active scene (empty if none).
    const std::string& getActiveSceneId() const { return activeSceneId_; }

    /// The active scene definition, or nullptr if none.
    const SceneDefinition* getActiveScene() const;

    /// All registered scene IDs.
    std::vector<std::string> getSceneIds() const;

    /// Find a scene by ID. Returns nullptr if not found.
    const SceneDefinition* findScene(const std::string& id) const;

    /// The loaded manifest.
    const SceneManifest& getManifest() const { return manifest_; }

    /// True if a manifest has been loaded.
    bool hasManifest() const { return !manifest_.scenes.empty(); }

    /// Result of the most recent transition.
    const SceneTransitionResult& getLastTransitionResult() const { return lastResult_; }

    // ========================================================================
    // Runtime scene management
    // ========================================================================

    /// Add a new scene to the manifest at runtime.
    void addScene(const SceneDefinition& scene);

    /// Remove a scene from the manifest. Cannot remove the active scene.
    bool removeScene(const std::string& sceneId);

    /// Get a mutable reference to the manifest for editing.
    SceneManifest& getManifestMutable() { return manifest_; }

    /// Per-scene re-entry data: last player position when leaving a scene.
    struct SceneReentryState {
        bool visited = false;
        float lastPlayerX = 0, lastPlayerY = 0, lastPlayerZ = 0;
    };

    /// Get re-entry state for a scene.
    const SceneReentryState* getReentryState(const std::string& sceneId) const;

private:
    // Transition implementation
    void executeUnload();
    void executeLoad();
    void resolveScenePaths();

    // State
    SceneManifest manifest_;
    std::string activeSceneId_;
    std::string targetSceneId_;
    SceneState state_ = SceneState::Idle;
    SceneTransitionResult lastResult_;

    // Configuration
    std::string worldsDir_;
    GameSubsystems* subsystems_ = nullptr;
    SceneCallbacks callbacks_;

    // Per-scene re-entry tracking
    std::unordered_map<std::string, SceneReentryState> reentryStates_;
};

} // namespace Core
} // namespace Phyxel
