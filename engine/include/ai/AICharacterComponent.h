#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <glm/glm.hpp>

#include "ai/GooseBridge.h"
#include "ai/AICommandQueue.h"

namespace VulkanCube {

// Forward declarations
namespace Scene {
    class Entity;
    class RagdollCharacter;
}

namespace AI {

// ============================================================================
// Skill Configuration
// ============================================================================

/// Defines a single AI skill (maps to a Goose subagent)
struct SkillConfig {
    std::string name;           // e.g., "combat", "dialog", "patrol"
    std::string recipePath;     // Path to the skill recipe YAML
    bool autoActivate = false;  // Activate when controller starts

    /// Events that trigger this skill
    std::vector<std::string> triggerEvents;
};

// ============================================================================
// AI Character State
// ============================================================================

enum class AIState {
    Idle,           // No AI activity; playing idle behavior
    WaitingForAI,   // Message sent, waiting for AI response
    ExecutingAction, // Executing an AI-commanded action
    Disabled        // AI control is turned off
};

// ============================================================================
// AIController
//
// Wraps any Entity (typically a RagdollCharacter) and connects it to a
// Goose agent session. Manages the character's AI lifecycle:
//
//   1. Creates a goose-server session for the character
//   2. Forwards game events to the session as messages
//   3. Receives AI commands from the SSE stream
//   4. Translates commands into entity actions (movement, dialog, etc.)
//
// Usage:
//   auto controller = std::make_unique<AIController>(&bridge, entity, "guard_01");
//   controller->setRecipe("resources/recipes/characters/guard.yaml");
//   controller->addSkill({"combat", "resources/recipes/skills/combat.yaml"});
//   controller->start();
//
// In game loop:
//   controller->update(deltaTime);  // processes pending commands
//
// Thread-safety: update() must be called from the main thread.
//                All other methods are thread-safe.
// ============================================================================

class AIController {
public:
    /// Create an AI controller for the given entity.
    /// @param bridge   Shared GooseBridge (manages goose-server connection)
    /// @param entity   The entity this controller drives (non-owning)
    /// @param entityId Unique string ID for this entity
    AIController(GooseBridge* bridge,
                 Scene::Entity* entity,
                 const std::string& entityId);

    ~AIController();

    // Non-copyable
    AIController(const AIController&) = delete;
    AIController& operator=(const AIController&) = delete;

    // ========================================================================
    // Configuration (call before start())
    // ========================================================================

    /// Set the character recipe YAML file.
    void setRecipe(const std::string& recipePath);

    /// Set the character's personality prompt (prepended to the recipe).
    void setPersonality(const std::string& personality);

    /// Add a skill to this character.
    void addSkill(const SkillConfig& skill);

    /// Remove a skill by name.
    void removeSkill(const std::string& skillName);

    /// Set the maximum time to wait for an AI response before falling back
    /// to idle behavior (in seconds).
    void setResponseTimeout(float seconds) { m_responseTimeout = seconds; }

    /// Set the minimum interval between AI prompts (rate limiting).
    void setMinPromptInterval(float seconds) { m_minPromptInterval = seconds; }

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// Start the AI controller — creates a goose session and registers skills.
    bool start();

    /// Stop the AI controller — destroys the goose session.
    void stop();

    /// Pause AI control (entity goes to idle behavior).
    void pause();

    /// Resume AI control after pause.
    void resume();

    /// Check if the controller is active.
    bool isActive() const { return m_state != AIState::Disabled; }

    /// Get current AI state.
    AIState getState() const { return m_state; }

    // ========================================================================
    // Game Loop Integration
    // ========================================================================

    /// Called once per frame from the game loop.
    /// Processes any pending AI commands for this entity.
    void update(float deltaTime);

    // ========================================================================
    // Event System
    // ========================================================================

    /// Notify the AI that a game event occurred relevant to this character.
    /// Examples:
    ///   notifyEvent("player_nearby", {{"player_id", "player1"}, {"distance", 5.0}});
    ///   notifyEvent("attacked", {{"attacker_id", "enemy3"}, {"damage", 25}});
    ///   notifyEvent("dialog_started", {{"player_id", "player1"}});
    void notifyEvent(const std::string& eventType, const json& eventData = {});

    /// Send a direct message to the AI (e.g., player dialog input).
    void sendDirectMessage(const std::string& message);

    // ========================================================================
    // State Queries
    // ========================================================================

    /// Get the entity this controller drives.
    Scene::Entity* getEntity() const { return m_entity; }

    /// Get the entity ID.
    const std::string& getEntityId() const { return m_entityId; }

    /// Get the goose session ID (empty if not started).
    const std::string& getSessionId() const { return m_sessionId; }

    /// Get the character's skills.
    const std::vector<SkillConfig>& getSkills() const { return m_skills; }

    /// Get time since last AI response (seconds).
    float getTimeSinceLastResponse() const;

    /// Check if waiting for an AI response.
    bool isWaitingForResponse() const { return m_state == AIState::WaitingForAI; }

    // ========================================================================
    // Action Handlers (customizable)
    // ========================================================================

    /// Custom handler for when the AI says dialog. Defaults to logging.
    using DialogHandler = std::function<void(const std::string& text,
                                              const std::string& emotion)>;
    void setDialogHandler(DialogHandler handler) { m_dialogHandler = std::move(handler); }

    /// Custom handler for when the AI wants to play an animation.
    using AnimationHandler = std::function<void(const std::string& animName, bool loop)>;
    void setAnimationHandler(AnimationHandler handler) { m_animHandler = std::move(handler); }

    /// Custom handler for combat actions.
    using AttackHandler = std::function<void(const std::string& targetId,
                                              const std::string& skillName)>;
    void setAttackHandler(AttackHandler handler) { m_attackHandler = std::move(handler); }

    /// Custom handler for emotes.
    using EmoteHandler = std::function<void(const std::string& emoteType)>;
    void setEmoteHandler(EmoteHandler handler) { m_emoteHandler = std::move(handler); }

    // Allow AICharacterManager to distribute commands directly
    friend class AICharacterManager;

private:
    // ========================================================================
    // Internal: Command Processing
    // ========================================================================

    /// Process a single AI command targeted at this entity.
    void processCommand(const AICommand& cmd);

    /// Execute a move-to command.
    void executeMoveToCommand(const MoveToCommand& cmd);

    /// Execute a say-dialog command.
    void executeDialogCommand(const SayDialogCommand& cmd);

    /// Execute a play-animation command.
    void executeAnimationCommand(const PlayAnimationCommand& cmd);

    /// Execute an attack command.
    void executeAttackCommand(const AttackCommand& cmd);

    /// Execute an emote command.
    void executeEmoteCommand(const EmoteCommand& cmd);

    /// Handle idle behavior while waiting for AI.
    void updateIdleBehavior(float deltaTime);

    // ========================================================================
    // State
    // ========================================================================

    GooseBridge* m_bridge = nullptr;
    Scene::Entity* m_entity = nullptr;
    std::string m_entityId;
    std::string m_sessionId;
    std::string m_recipePath;
    std::string m_personality;

    AIState m_state = AIState::Disabled;

    // Skills
    std::vector<SkillConfig> m_skills;

    // Timing
    float m_responseTimeout = 5.0f;     // Max seconds to wait for AI
    float m_minPromptInterval = 1.0f;   // Min seconds between prompts
    float m_timeSinceLastResponse = 0.0f;
    float m_timeSinceLastPrompt = 0.0f;

    // Movement state
    std::optional<glm::vec3> m_moveTarget;
    float m_moveSpeed = 1.0f;

    // Pending commands for this entity (filtered from the global queue)
    std::vector<AICommand> m_pendingCommands;

    // Pending async reply futures
    std::vector<std::future<bool>> m_pendingReplies;

    // Action handlers
    DialogHandler m_dialogHandler;
    AnimationHandler m_animHandler;
    AttackHandler m_attackHandler;
    EmoteHandler m_emoteHandler;
};

// ============================================================================
// AICharacterManager
//
// Central registry of all AI-controlled characters. Sits between the
// GooseBridge (which handles goose-server comms) and individual AIControllers.
//
// Call update() once per frame from the game loop.
// ============================================================================

class AICharacterManager {
public:
    explicit AICharacterManager(GooseBridge* bridge);
    ~AICharacterManager() = default;

    /// Create an AI controller for an entity.
    /// Returns a non-owning pointer (manager owns the controller).
    AIController* createController(Scene::Entity* entity,
                                    const std::string& entityId,
                                    const std::string& recipePath = "");

    /// Remove an AI controller by entity ID.
    void removeController(const std::string& entityId);

    /// Get a controller by entity ID.
    AIController* getController(const std::string& entityId);

    /// Update all AI controllers (called once per frame).
    void update(float deltaTime);

    /// Broadcast a game event to all AI controllers.
    void broadcastEvent(const std::string& eventType, const json& eventData = {});

    /// Get all active controller entity IDs.
    std::vector<std::string> getActiveEntityIds() const;

    /// Get total number of controllers.
    size_t getControllerCount() const { return m_controllers.size(); }

    /// Start all controllers.
    void startAll();

    /// Stop all controllers.
    void stopAll();

private:
    GooseBridge* m_bridge;
    std::unordered_map<std::string, std::unique_ptr<AIController>> m_controllers;

    /// Drain global command queue and distribute commands to controllers.
    void distributeCommands();
};

} // namespace AI
} // namespace VulkanCube
