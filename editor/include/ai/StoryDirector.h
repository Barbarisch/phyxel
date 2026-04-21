#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <nlohmann/json.hpp>

#include "ai/GooseBridge.h"

namespace Phyxel {
namespace AI {

using json = nlohmann::json;

// ============================================================================
// Quest State
// ============================================================================

enum class QuestStatus {
    NotStarted,
    Active,
    ObjectiveComplete,
    Completed,
    Failed
};

struct QuestState {
    std::string questId;
    QuestStatus status = QuestStatus::NotStarted;
    std::string detail;
    std::vector<std::string> completedObjectives;
    float progressPercent = 0.0f;
};

// ============================================================================
// Story Beat
// ============================================================================

/// A discrete narrative event that the Story Director can trigger.
struct StoryBeat {
    std::string id;
    std::string description;
    std::string triggerCondition;  // JSON expression or simple key
    bool triggered = false;
    float timestamp = 0.0f;       // Game time when triggered
};

// ============================================================================
// StoryDirector
//
// High-level narrative orchestration agent. Sits above individual NPC
// controllers and manages:
//   - Quest state tracking and progression
//   - Story beat triggering based on game state
//   - NPC mood/disposition coordination
//   - World event orchestration
//
// The StoryDirector runs as its own Goose agent session using the
// story_director.yaml recipe. It is prompted periodically or on
// significant game events.
// ============================================================================

class StoryDirector {
public:
    explicit StoryDirector(GooseBridge* bridge);
    ~StoryDirector();

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// Start the Story Director agent session.
    /// Uses the story director recipe.
    bool start(const std::string& recipePath = "resources/ai/stories/story_director.yaml");

    /// Stop the Story Director.
    void stop();

    /// Check if running.
    bool isActive() const { return m_isActive; }

    // ========================================================================
    // Game Loop
    // ========================================================================

    /// Update the story director. Should be called periodically (not every frame).
    /// Recommended: every 5-10 seconds, or on significant events.
    void update(float deltaTime);

    /// Set the update interval (how often the director is prompted).
    void setUpdateInterval(float seconds) { m_updateInterval = seconds; }

    // ========================================================================
    // Event System
    // ========================================================================

    /// Notify the story director of a significant game event.
    /// The director will consider this in its next deliberation.
    void notifyEvent(const std::string& eventType, const json& eventData = {});

    /// Register a story beat that can be triggered by the director.
    void registerStoryBeat(const StoryBeat& beat);

    /// Check if a story beat has been triggered.
    bool isStoryBeatTriggered(const std::string& beatId) const;

    // ========================================================================
    // Quest Management
    // ========================================================================

    /// Register a quest that the director can manage.
    void registerQuest(const std::string& questId,
                       const std::string& description);

    /// Get quest state.
    std::optional<QuestState> getQuestState(const std::string& questId) const;

    /// Get all quests.
    const std::unordered_map<std::string, QuestState>& getAllQuests() const {
        return m_quests;
    }

    /// Manually set quest state (for initialization or external triggers).
    void setQuestState(const std::string& questId, QuestStatus status,
                       const std::string& detail = "");

    // ========================================================================
    // NPC Coordination
    // ========================================================================

    /// Set an NPC's mood/disposition via the story system.
    /// This triggers an event that the NPC's AI controller can react to.
    void setNPCMood(const std::string& entityId,
                    const std::string& mood,
                    const std::string& reason = "");

    /// Send a narrative hint to an NPC (e.g., "mention the missing sword").
    void sendNPCHint(const std::string& entityId,
                     const std::string& hint);

    // ========================================================================
    // Callbacks
    // ========================================================================

    /// Called when a quest state changes (from AI or external).
    using QuestCallback = std::function<void(const std::string& questId,
                                             QuestStatus newStatus)>;
    void setQuestCallback(QuestCallback callback) { m_questCallback = std::move(callback); }

    /// Called when a story beat triggers.
    using StoryBeatCallback = std::function<void(const std::string& beatId)>;
    void setStoryBeatCallback(StoryBeatCallback callback) {
        m_storyBeatCallback = std::move(callback);
    }

    /// Called when the director triggers a world event.
    using WorldEventCallback = std::function<void(const std::string& eventName,
                                                   const json& payload)>;
    void setWorldEventCallback(WorldEventCallback callback) {
        m_worldEventCallback = std::move(callback);
    }

private:
    /// Build a summary of current game state for the director prompt.
    std::string buildWorldSummary() const;

    /// Process commands from the director's response.
    void processDirectorCommands();

    GooseBridge* m_bridge;
    std::string m_sessionId;
    bool m_isActive = false;

    // Timing
    float m_updateInterval = 10.0f;  // Prompt director every N seconds
    float m_timeSinceLastUpdate = 0.0f;
    float m_gameTime = 0.0f;

    // State
    std::unordered_map<std::string, QuestState> m_quests;
    std::vector<StoryBeat> m_storyBeats;
    std::vector<json> m_pendingEvents;  // Events queued for next director prompt

    // Callbacks
    QuestCallback m_questCallback;
    StoryBeatCallback m_storyBeatCallback;
    WorldEventCallback m_worldEventCallback;
};

} // namespace AI
} // namespace Phyxel
