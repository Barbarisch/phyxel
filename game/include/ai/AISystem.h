#pragma once

#include <memory>
#include <string>
#include <functional>

#include "ai/GooseBridge.h"
#include "ai/AICharacterComponent.h"
#include "ai/StoryDirector.h"

namespace VulkanCube {

namespace Scene {
    class Entity;
}

namespace AI {

// ============================================================================
// AISystem
//
// Top-level facade that owns and coordinates all AI subsystems:
//   - GooseBridge (goose-server sidecar process + HTTP client)
//   - AICharacterManager (per-NPC AI controllers)
//   - StoryDirector (narrative orchestration)
//
// Usage in Application:
//   aiSystem = std::make_unique<AI::AISystem>();
//   aiSystem->initialize(config);
//   // In game loop:
//   aiSystem->update(deltaTime);
//   // Creating AI NPCs:
//   aiSystem->createAINPC(entity, "guard_01", "resources/recipes/characters/guard.yaml");
// ============================================================================

class AISystem {
public:
    AISystem();
    ~AISystem();

    // Non-copyable
    AISystem(const AISystem&) = delete;
    AISystem& operator=(const AISystem&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// Initialize the AI system. Starts goose-server if configured.
    /// @param config  GooseBridge configuration
    /// @param autoStart  If true, immediately start the goose-server
    /// @return true if initialization succeeded
    bool initialize(const GooseConfig& config = {}, bool autoStart = true);

    /// Shut down the AI system. Stops all agents and the goose-server.
    void shutdown();

    /// Check if the AI system is initialized and the server is running.
    bool isReady() const;

    // ========================================================================
    // Game Loop Integration
    // ========================================================================

    /// Called every frame from the game loop.
    /// Processes AI commands and updates all controllers.
    void update(float deltaTime);

    // ========================================================================
    // NPC Management
    // ========================================================================

    /// Create an AI-controlled NPC.
    /// @param entity     The game entity for this NPC
    /// @param entityId   Unique string ID
    /// @param recipePath Path to character recipe YAML
    /// @param personality  Optional personality description
    /// @return Pointer to the AIController (owned by manager)
    AIController* createAINPC(Scene::Entity* entity,
                               const std::string& entityId,
                               const std::string& recipePath = "",
                               const std::string& personality = "");

    /// Remove an AI NPC.
    void removeAINPC(const std::string& entityId);

    /// Get an NPC's AI controller.
    AIController* getAINPC(const std::string& entityId);

    /// Broadcast a game event to all NPCs.
    void broadcastEvent(const std::string& eventType,
                        const json& eventData = {});

    // ========================================================================
    // Story System
    // ========================================================================

    /// Get the story director (may be null if not started).
    StoryDirector* getStoryDirector() { return m_storyDirector.get(); }

    /// Start the story director with a recipe.
    bool startStoryDirector(const std::string& recipePath =
        "resources/recipes/stories/story_director.yaml");

    /// Stop the story director.
    void stopStoryDirector();

    // ========================================================================
    // Direct Access (for advanced usage)
    // ========================================================================

    /// Get the GooseBridge for direct goose-server communication.
    GooseBridge* getBridge() { return m_bridge.get(); }

    /// Get the character manager.
    AICharacterManager* getCharacterManager() { return m_characterManager.get(); }

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Set the LLM provider/model for future sessions.
    bool setProvider(const std::string& provider, const std::string& model);

    /// Get current config.
    const GooseConfig& getConfig() const;

    // ========================================================================
    // Diagnostics
    // ========================================================================

    struct AIStats {
        bool serverRunning = false;
        size_t activeNPCs = 0;
        bool storyDirectorActive = false;
        int64_t totalInputTokens = 0;
        int64_t totalOutputTokens = 0;
        size_t pendingCommands = 0;
    };

    /// Get current AI system statistics.
    AIStats getStats() const;

private:
    std::unique_ptr<GooseBridge> m_bridge;
    std::unique_ptr<AICharacterManager> m_characterManager;
    std::unique_ptr<StoryDirector> m_storyDirector;
    bool m_initialized = false;
};

} // namespace AI
} // namespace VulkanCube
