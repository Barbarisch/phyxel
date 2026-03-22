#pragma once

#include "story/StoryTypes.h"
#include "story/CharacterProfile.h"
#include "story/CharacterMemory.h"
#include "story/EventBus.h"
#include "story/KnowledgePropagator.h"
#include "story/StoryDirector.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Phyxel {
namespace Story {

// ============================================================================
// StoryEngine — facade for the story system
//
// Game developers interact with this class to define worlds, add characters,
// manage knowledge, and run the story system.
// ============================================================================

class StoryEngine {
public:
    StoryEngine() = default;
    ~StoryEngine() = default;

    // Non-copyable
    StoryEngine(const StoryEngine&) = delete;
    StoryEngine& operator=(const StoryEngine&) = delete;

    // === World Setup ===

    /// Initialize with a world state (factions, locations, variables).
    void defineWorld(WorldState state);

    /// Get the current world state.
    WorldState& getWorldState() { return m_worldState; }
    const WorldState& getWorldState() const { return m_worldState; }

    /// Add or replace a character profile.
    void addCharacter(CharacterProfile profile);

    /// Remove a character.
    bool removeCharacter(const std::string& characterId);

    /// Get a character profile (null if not found).
    const CharacterProfile* getCharacter(const std::string& id) const;
    CharacterProfile* getCharacterMut(const std::string& id);

    /// Get all character IDs.
    std::vector<std::string> getCharacterIds() const;

    /// Give a character starting/background knowledge.
    void addStartingKnowledge(const std::string& characterId,
                              const std::string& factId,
                              const std::string& summary,
                              const nlohmann::json& details = nlohmann::json::object());

    /// Get a character's memory (null if character not found).
    const CharacterMemory* getCharacterMemory(const std::string& characterId) const;
    CharacterMemory* getCharacterMemoryMut(const std::string& characterId);

    // === Runtime ===

    /// Advance world time and update character systems (emotion decay, memory decay).
    void update(float dt);

    /// Trigger a world event. Records it, broadcasts via EventBus, and propagates to witnesses.
    void triggerEvent(WorldEvent event);

    /// Share knowledge between two characters (during dialogue).
    /// Returns the number of facts transferred.
    int shareKnowledge(const std::string& speakerId, const std::string& listenerId, int maxFacts = 5);

    /// Spread rumors between nearby characters. Call periodically.
    void spreadRumors(float dt, float proximityRadius = 32.0f);

    /// Access the event bus for custom subscriptions.
    EventBus& getEventBus() { return m_eventBus; }
    const EventBus& getEventBus() const { return m_eventBus; }

    /// Access the knowledge propagator for configuration.
    KnowledgePropagator& getPropagator() { return m_propagator; }
    const KnowledgePropagator& getPropagator() const { return m_propagator; }

    /// Access the story director for arc management.
    StoryDirector& getDirector() { return m_director; }
    const StoryDirector& getDirector() const { return m_director; }

    /// Add a story arc and optionally activate it.
    void addStoryArc(StoryArc arc, bool activate = true);

    /// Get a character's current emotional state.
    const EmotionalState* getCharacterEmotion(const std::string& characterId) const;

    /// Modify a character's goal priority at runtime.
    bool setGoalPriority(const std::string& characterId, const std::string& goalId, float priority);

    /// Modify a character's agency level at runtime.
    bool setAgencyLevel(const std::string& characterId, AgencyLevel level);

    // === Serialization ===

    nlohmann::json saveState() const;
    void loadState(const nlohmann::json& state);

private:
    WorldState m_worldState;
    std::unordered_map<std::string, CharacterProfile> m_characters;
    std::unordered_map<std::string, CharacterMemory> m_memories;
    EventBus m_eventBus;
    KnowledgePropagator m_propagator;
    StoryDirector m_director;
};

} // namespace Story
} // namespace Phyxel
