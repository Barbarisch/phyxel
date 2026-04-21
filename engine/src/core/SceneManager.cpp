#include "core/SceneManager.h"
#include "core/GameDefinitionLoader.h"
#include "core/ChunkManager.h"
#include "core/EntityRegistry.h"
#include "core/LocationRegistry.h"
#include "core/GameEventLog.h"
#include "graphics/Camera.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"
#include <chrono>
#include <filesystem>

namespace Phyxel {
namespace Core {

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() = default;

// ============================================================================
// Configuration
// ============================================================================

void SceneManager::loadManifest(const SceneManifest& manifest) {
    manifest_ = manifest;
    resolveScenePaths();
    LOG_INFO("SceneManager", "Loaded manifest with {} scene(s), startScene='{}'",
             manifest_.scenes.size(), manifest_.startScene);
}

void SceneManager::resolveScenePaths() {
    namespace fs = std::filesystem;
    for (auto& scene : manifest_.scenes) {
        std::string dbFile = scene.getWorldDatabaseFilename();
        if (worldsDir_.empty()) {
            scene.resolvedWorldPath = dbFile;
        } else {
            scene.resolvedWorldPath = (fs::path(worldsDir_) / dbFile).string();
        }
    }
}

// ============================================================================
// Scene transitions
// ============================================================================

bool SceneManager::transitionTo(const std::string& sceneId) {
    if (isTransitioning()) {
        LOG_WARN("SceneManager", "Cannot transition — already transitioning");
        return false;
    }

    const auto* target = manifest_.findScene(sceneId);
    if (!target) {
        LOG_ERROR("SceneManager", "Scene '{}' not found in manifest", sceneId);
        return false;
    }

    if (sceneId == activeSceneId_) {
        LOG_WARN("SceneManager", "Already in scene '{}'", sceneId);
        return false;
    }

    targetSceneId_ = sceneId;
    lastResult_ = {};
    lastResult_.fromScene = activeSceneId_;
    lastResult_.toScene = sceneId;

    // If there's an active scene, unload it first. Otherwise go straight to loading.
    if (!activeSceneId_.empty()) {
        state_ = SceneState::Unloading;
        LOG_INFO("SceneManager", "Beginning transition: '{}' -> '{}'", activeSceneId_, sceneId);
    } else {
        state_ = SceneState::Loading;
        LOG_INFO("SceneManager", "Loading initial scene: '{}'", sceneId);
    }

    return true;
}

bool SceneManager::loadStartScene() {
    if (manifest_.startScene.empty()) {
        LOG_ERROR("SceneManager", "No startScene defined in manifest");
        return false;
    }
    return transitionTo(manifest_.startScene);
}

void SceneManager::update(float /*deltaTime*/) {
    // The transition is driven synchronously through the state machine.
    // Each frame we advance one step if needed.
    switch (state_) {
        case SceneState::Idle:
        case SceneState::Ready:
            return; // Nothing to do

        case SceneState::Unloading: {
            const auto* target = manifest_.findScene(targetSceneId_);
            if (callbacks_.setLoadingScreen && target) {
                callbacks_.setLoadingScreen(true, target->name);
            }
            executeUnload();
            state_ = SceneState::Loading;
            break;
        }

        case SceneState::Loading: {
            auto startTime = std::chrono::high_resolution_clock::now();

            const auto* target = manifest_.findScene(targetSceneId_);
            if (callbacks_.setLoadingScreen && target) {
                callbacks_.setLoadingScreen(true, target->name);
            }

            executeLoad();

            auto endTime = std::chrono::high_resolution_clock::now();
            float ms = std::chrono::duration<float, std::milli>(endTime - startTime).count();
            lastResult_.transitionTimeMs = ms;

            if (lastResult_.success) {
                activeSceneId_ = targetSceneId_;
                state_ = SceneState::Ready;

                if (callbacks_.setLoadingScreen) {
                    callbacks_.setLoadingScreen(false, "");
                }
                if (callbacks_.onSceneReady) {
                    callbacks_.onSceneReady(activeSceneId_);
                }

                LOG_INFO("SceneManager", "Scene '{}' loaded in {:.1f}ms",
                         activeSceneId_, ms);
            } else {
                state_ = SceneState::Idle;
                if (callbacks_.setLoadingScreen) {
                    callbacks_.setLoadingScreen(false, "");
                }
                LOG_ERROR("SceneManager", "Failed to load scene '{}': {}",
                          targetSceneId_, lastResult_.error);
            }

            targetSceneId_.clear();
            break;
        }
    }
}

// ============================================================================
// Unload — tear down the current scene's per-scene state
// ============================================================================

void SceneManager::executeUnload() {
    LOG_INFO("SceneManager", "Unloading scene '{}'...", activeSceneId_);

    const auto* scene = manifest_.findScene(activeSceneId_);

    // 1. End any active dialogue before teardown
    if (callbacks_.endDialogue) {
        callbacks_.endDialogue();
    }

    // 2. Fire onExit script
    if (scene && !scene->onExitScript.empty() && callbacks_.runScript) {
        callbacks_.runScript(scene->onExitScript);
    }

    // 3. Clear application-owned entities (NPCs first, then entities)
    if (callbacks_.clearNPCs) {
        callbacks_.clearNPCs();
    }
    if (callbacks_.clearEntities) {
        callbacks_.clearEntities();
    }

    // 4. Clear scene-local story characters (arcs and world state persist)
    if (callbacks_.clearSceneStoryCharacters) {
        callbacks_.clearSceneStoryCharacters();
    }

    // 5. Reset lighting and music
    if (callbacks_.resetLighting) {
        callbacks_.resetLighting();
    }
    if (callbacks_.clearMusic) {
        callbacks_.clearMusic();
    }

    // 6. Clear objectives
    if (callbacks_.clearObjectives) {
        callbacks_.clearObjectives();
    }

    // 7. Clear locations
    if (subsystems_ && subsystems_->locationRegistry) {
        subsystems_->locationRegistry->clear();
    }

    // 8. Save dirty chunks to the current scene's DB, then disconnect
    //    (This is handled by the host via ChunkManager)
    //    The actual chunk unload + DB switch happens in executeLoad()
    //    since we need ChunkManager to save before clearing.

    // 9. Save player position for re-entry
    if (subsystems_ && subsystems_->camera) {
        auto pos = subsystems_->camera->getPosition();
        auto& rs = reentryStates_[activeSceneId_];
        rs.lastPlayerX = pos.x;
        rs.lastPlayerY = pos.y;
        rs.lastPlayerZ = pos.z;
    }

    LOG_INFO("SceneManager", "Scene '{}' unloaded", activeSceneId_);
}

// ============================================================================
// Load — set up the target scene
// ============================================================================

void SceneManager::executeLoad() {
    const auto* scene = manifest_.findScene(targetSceneId_);
    if (!scene) {
        lastResult_.success = false;
        lastResult_.error = "Scene '" + targetSceneId_ + "' not found";
        return;
    }

    if (!subsystems_) {
        lastResult_.success = false;
        lastResult_.error = "No subsystems configured";
        return;
    }

    // 1. Save dirty chunks from previous scene and switch world database
    if (subsystems_->chunkManager) {
        auto* cm = subsystems_->chunkManager;
        cm->saveDirtyChunks();

        // Clear dynamic physics objects before destroying chunks
        cm->clearAllGlobalDynamicSubcubes();
        cm->clearAllGlobalDynamicCubes();
        cm->clearAllGlobalDynamicMicrocubes();

        cm->cleanup();             // Clear all chunk data from memory
        cm->disconnectWorldStorage();

        // Initialize storage with the new scene's DB
        if (!scene->resolvedWorldPath.empty()) {
            if (!cm->initializeWorldStorage(scene->resolvedWorldPath)) {
                lastResult_.success = false;
                lastResult_.error = "Failed to open world database: " + scene->resolvedWorldPath;
                return;
            }

            // If the scene definition has no inline "world" generation,
            // load pre-baked chunks from the database file.
            if (!scene->definition.contains("world")) {
                cm->loadAllChunksFromDatabase();
                cm->rebuildAllChunkFaces();
            }
        }
    }

    // 2. Build the definition to load. Merge playerDefaults if scene doesn't specify player.
    json defToLoad = scene->definition;
    if (!defToLoad.contains("player") && !manifest_.playerDefaults.is_null()) {
        defToLoad["player"] = manifest_.playerDefaults;
    }

    // 3. Load via GameDefinitionLoader (world → structures → player → camera → NPCs → story)
    auto result = GameDefinitionLoader::load(defToLoad, *subsystems_);
    if (!result.success) {
        lastResult_.success = false;
        lastResult_.error = "GameDefinitionLoader failed: " + result.error;
        return;
    }

    // 4. Restore player position if re-entering a visited scene
    auto reIt = reentryStates_.find(targetSceneId_);
    if (reIt != reentryStates_.end() && reIt->second.visited && subsystems_->camera) {
        auto& rs = reIt->second;
        subsystems_->camera->setPosition(glm::vec3(rs.lastPlayerX, rs.lastPlayerY, rs.lastPlayerZ));
        LOG_INFO("SceneManager", "Restored re-entry position ({:.1f}, {:.1f}, {:.1f}) for scene '{}'",
                 rs.lastPlayerX, rs.lastPlayerY, rs.lastPlayerZ, targetSceneId_);
    }

    // 5. Rebuild navigation grid
    if (callbacks_.rebuildNavGrid) {
        callbacks_.rebuildNavGrid();
    }

    // 6. Fire onEnter script
    if (!scene->onEnterScript.empty() && callbacks_.runScript) {
        callbacks_.runScript(scene->onEnterScript);
    }

    // 7. Track re-entry state
    reentryStates_[targetSceneId_].visited = true;

    // 8. Emit scene_loaded event for MCP polling
    if (subsystems_->gameEventLog) {
        subsystems_->gameEventLog->emit("scene_loaded", {
            {"scene_id", targetSceneId_},
            {"scene_name", scene->name},
            {"from_scene", lastResult_.fromScene}
        });
    }

    lastResult_.success = true;
    lastResult_.error.clear();
}

// ============================================================================
// Queries
// ============================================================================

const SceneDefinition* SceneManager::getActiveScene() const {
    return manifest_.findScene(activeSceneId_);
}

std::vector<std::string> SceneManager::getSceneIds() const {
    std::vector<std::string> ids;
    ids.reserve(manifest_.scenes.size());
    for (const auto& s : manifest_.scenes) {
        ids.push_back(s.id);
    }
    return ids;
}

const SceneDefinition* SceneManager::findScene(const std::string& id) const {
    return manifest_.findScene(id);
}

// ============================================================================
// Runtime scene management
// ============================================================================

void SceneManager::addScene(const SceneDefinition& scene) {
    // Resolve the path
    SceneDefinition copy = scene;
    std::string dbFile = copy.getWorldDatabaseFilename();
    if (!worldsDir_.empty()) {
        copy.resolvedWorldPath = (std::filesystem::path(worldsDir_) / dbFile).string();
    } else {
        copy.resolvedWorldPath = dbFile;
    }
    manifest_.scenes.push_back(std::move(copy));
    LOG_INFO("SceneManager", "Added scene '{}' ({} total)", scene.id, manifest_.scenes.size());
}

bool SceneManager::removeScene(const std::string& sceneId) {
    if (sceneId == activeSceneId_) {
        LOG_WARN("SceneManager", "Cannot remove the active scene '{}'", sceneId);
        return false;
    }
    auto it = std::find_if(manifest_.scenes.begin(), manifest_.scenes.end(),
                           [&](const SceneDefinition& s) { return s.id == sceneId; });
    if (it == manifest_.scenes.end()) return false;
    manifest_.scenes.erase(it);
    reentryStates_.erase(sceneId);
    return true;
}

const SceneManager::SceneReentryState* SceneManager::getReentryState(const std::string& sceneId) const {
    auto it = reentryStates_.find(sceneId);
    return (it != reentryStates_.end()) ? &it->second : nullptr;
}

} // namespace Core
} // namespace Phyxel
