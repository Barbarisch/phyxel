#include "ai/StoryDirector.h"
#include "utils/Logger.h"

#include <sstream>
#include <algorithm>

namespace Phyxel {
namespace AI {

// ============================================================================
// Construction / Destruction
// ============================================================================

StoryDirector::StoryDirector(GooseBridge* bridge)
    : m_bridge(bridge)
{
    LOG_INFO("AI", "StoryDirector created");
}

StoryDirector::~StoryDirector() {
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool StoryDirector::start(const std::string& recipePath) {
    if (!m_bridge || !m_bridge->isServerRunning()) {
        LOG_ERROR("AI", "StoryDirector: cannot start — goose-server not running");
        return false;
    }

    m_sessionId = m_bridge->createSession("story_director");
    if (m_sessionId.empty()) {
        LOG_ERROR("AI", "StoryDirector: failed to create session");
        return false;
    }

    m_isActive = true;
    m_timeSinceLastUpdate = 0.0f;
    m_gameTime = 0.0f;

    LOG_INFO("AI", "StoryDirector: started with session {}", m_sessionId);
    return true;
}

void StoryDirector::stop() {
    if (!m_isActive) return;

    if (!m_sessionId.empty() && m_bridge) {
        m_bridge->destroySession(m_sessionId);
    }

    m_sessionId.clear();
    m_isActive = false;
    m_pendingEvents.clear();

    LOG_INFO("AI", "StoryDirector: stopped");
}

// ============================================================================
// Game Loop
// ============================================================================

void StoryDirector::update(float deltaTime) {
    if (!m_isActive) return;

    m_gameTime += deltaTime;
    m_timeSinceLastUpdate += deltaTime;

    // Process any commands the director issued from previous responses
    processDirectorCommands();

    // Only prompt the director on the configured interval or when events are pending
    bool hasEvents = !m_pendingEvents.empty();
    bool intervalElapsed = m_timeSinceLastUpdate >= m_updateInterval;

    if (!intervalElapsed && !hasEvents) return;

    m_timeSinceLastUpdate = 0.0f;

    // Build the prompt
    std::ostringstream prompt;
    prompt << "[Story Director Update — Game Time: "
           << static_cast<int>(m_gameTime) << "s]\n\n";

    // Include world summary
    prompt << buildWorldSummary() << "\n";

    // Include pending events
    if (hasEvents) {
        prompt << "## Recent Events\n";
        for (const auto& event : m_pendingEvents) {
            prompt << "- " << event.dump() << "\n";
        }
        m_pendingEvents.clear();
        prompt << "\n";
    }

    prompt << "Based on the current state, decide if any story actions are needed. "
           << "If nothing needs to change right now, simply reply with 'No changes needed.'\n";

    // Send to the director session asynchronously (fire and forget)
    auto future = m_bridge->sendMessage(m_sessionId, prompt.str());
    (void)future;  // Async — results come via command queue

    LOG_DEBUG("AI", "StoryDirector: prompted for update at game time {}s", m_gameTime);
}

// ============================================================================
// Events
// ============================================================================

void StoryDirector::notifyEvent(const std::string& eventType, const json& eventData) {
    json event;
    event["type"] = eventType;
    event["data"] = eventData;
    event["game_time"] = m_gameTime;

    m_pendingEvents.push_back(event);

    LOG_DEBUG("AI", "StoryDirector: received event '{}' (queued, {} pending)",
              eventType, m_pendingEvents.size());
}

void StoryDirector::registerStoryBeat(const StoryBeat& beat) {
    m_storyBeats.push_back(beat);
    LOG_DEBUG("AI", "StoryDirector: registered story beat '{}'", beat.id);
}

bool StoryDirector::isStoryBeatTriggered(const std::string& beatId) const {
    auto it = std::find_if(m_storyBeats.begin(), m_storyBeats.end(),
        [&](const StoryBeat& b) { return b.id == beatId; });
    return it != m_storyBeats.end() && it->triggered;
}

// ============================================================================
// Quest Management
// ============================================================================

void StoryDirector::registerQuest(const std::string& questId,
                                   const std::string& description) {
    QuestState quest;
    quest.questId = questId;
    quest.detail = description;
    m_quests[questId] = quest;

    LOG_INFO("AI", "StoryDirector: registered quest '{}'", questId);
}

std::optional<QuestState> StoryDirector::getQuestState(const std::string& questId) const {
    auto it = m_quests.find(questId);
    if (it == m_quests.end()) return std::nullopt;
    return it->second;
}

void StoryDirector::setQuestState(const std::string& questId, QuestStatus status,
                                   const std::string& detail) {
    auto it = m_quests.find(questId);
    if (it == m_quests.end()) {
        // Auto-register
        QuestState quest;
        quest.questId = questId;
        quest.status = status;
        quest.detail = detail;
        m_quests[questId] = quest;
    } else {
        it->second.status = status;
        if (!detail.empty()) it->second.detail = detail;
    }

    LOG_INFO("AI", "StoryDirector: quest '{}' → status {}",
             questId, static_cast<int>(status));

    if (m_questCallback) {
        m_questCallback(questId, status);
    }
}

// ============================================================================
// NPC Coordination
// ============================================================================

void StoryDirector::setNPCMood(const std::string& entityId,
                                const std::string& mood,
                                const std::string& reason) {
    json payload;
    payload["entity_id"] = entityId;
    payload["mood"] = mood;
    payload["reason"] = reason;

    // Queue a mood change event for the NPC's controller
    notifyEvent("npc_mood_change", payload);

    LOG_DEBUG("AI", "StoryDirector: set NPC '{}' mood to '{}'", entityId, mood);
}

void StoryDirector::sendNPCHint(const std::string& entityId,
                                 const std::string& hint) {
    json payload;
    payload["entity_id"] = entityId;
    payload["hint"] = hint;

    notifyEvent("quest_hint", payload);

    LOG_DEBUG("AI", "StoryDirector: sent hint to NPC '{}': {}", entityId, hint);
}

// ============================================================================
// Internal
// ============================================================================

std::string StoryDirector::buildWorldSummary() const {
    std::ostringstream summary;

    summary << "## Quest Status\n";
    if (m_quests.empty()) {
        summary << "No quests registered.\n";
    } else {
        for (const auto& [id, quest] : m_quests) {
            const char* statusStr = "unknown";
            switch (quest.status) {
                case QuestStatus::NotStarted:        statusStr = "not_started"; break;
                case QuestStatus::Active:            statusStr = "active"; break;
                case QuestStatus::ObjectiveComplete: statusStr = "objective_complete"; break;
                case QuestStatus::Completed:         statusStr = "completed"; break;
                case QuestStatus::Failed:            statusStr = "failed"; break;
            }
            summary << "- [" << statusStr << "] " << id;
            if (!quest.detail.empty()) summary << " — " << quest.detail;
            summary << "\n";
        }
    }

    summary << "\n## Story Beats\n";
    int triggeredCount = 0;
    for (const auto& beat : m_storyBeats) {
        if (beat.triggered) {
            summary << "- [TRIGGERED] " << beat.id << " — " << beat.description << "\n";
            triggeredCount++;
        } else {
            summary << "- [PENDING] " << beat.id << " — " << beat.description << "\n";
        }
    }
    if (m_storyBeats.empty()) {
        summary << "No story beats registered.\n";
    }

    return summary.str();
}

void StoryDirector::processDirectorCommands() {
    // The director's tool calls go through the global AICommandQueue
    // and are processed by the AICharacterManager. Here we additionally
    // watch for quest/event commands that affect our local state.

    // This is handled in AICharacterManager::distributeCommands() for
    // SetQuestStateCommand and TriggerEventCommand. The StoryDirector
    // registers callbacks to react to those.
}

} // namespace AI
} // namespace Phyxel
