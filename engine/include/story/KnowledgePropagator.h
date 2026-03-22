#pragma once

#include "story/StoryTypes.h"
#include "story/CharacterProfile.h"
#include "story/CharacterMemory.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Story {

// ============================================================================
// KnowledgePropagator — determines how information flows between characters
//
// Three propagation channels:
//   1. Witnessing: characters near an event learn about it directly
//   2. Telling: characters share knowledge during dialogue (filtered by trust)
//   3. Rumors: background knowledge spread between characters at the same location
// ============================================================================

/// Callback to get a character's world position. Returns false if character not found.
using PositionLookup = std::function<bool(const std::string& characterId, glm::vec3& outPosition)>;

class KnowledgePropagator {
public:
    /// Set the callback for looking up character positions in the game world.
    void setPositionLookup(PositionLookup lookup) { m_positionLookup = std::move(lookup); }

    // === Channel 1: Witnessing ===

    /// Given a world event, determine which characters witness it and update their memories.
    /// Returns the IDs of characters who witnessed the event.
    std::vector<std::string> propagateWitness(
        const WorldEvent& event,
        const std::unordered_map<std::string, CharacterProfile>& characters,
        std::unordered_map<std::string, CharacterMemory>& memories);

    // === Channel 2: Telling (during dialogue) ===

    /// Speaker shares knowledge with listener based on trust, extraversion, and fact importance.
    /// Returns number of facts shared.
    int propagateDialogue(
        const std::string& speakerId,
        const std::string& listenerId,
        const CharacterProfile& speakerProfile,
        const CharacterProfile& listenerProfile,
        const CharacterMemory& speakerMemory,
        CharacterMemory& listenerMemory,
        int maxFacts = 5);

    // === Channel 3: Rumors (background spread) ===

    /// Spread rumors between characters that are near each other.
    /// Characters within `proximityRadius` of each other may share knowledge.
    /// Called periodically (e.g. every few seconds of game time).
    void propagateRumors(
        float dt,
        float proximityRadius,
        const std::unordered_map<std::string, CharacterProfile>& characters,
        std::unordered_map<std::string, CharacterMemory>& memories);

    // === Configuration ===

    /// Base chance per second that a character spreads a rumor to a nearby character.
    /// Multiplied by speaker's extraversion. Default: 0.1 (10% per second at extraversion=1.0)
    void setRumorBaseChance(float chance) { m_rumorBaseChance = chance; }
    float getRumorBaseChance() const { return m_rumorBaseChance; }

    /// Minimum importance for a fact to be shared as a rumor. Default: 0.3
    void setRumorImportanceThreshold(float threshold) { m_rumorImportanceThreshold = threshold; }

    /// Minimum trust required for a speaker to share a fact during dialogue. Default: -0.5
    /// (characters share with almost anyone unless they're deeply distrusted)
    void setDialogueTrustThreshold(float threshold) { m_dialogueTrustThreshold = threshold; }

private:
    PositionLookup m_positionLookup;
    float m_rumorBaseChance = 0.1f;
    float m_rumorImportanceThreshold = 0.3f;
    float m_dialogueTrustThreshold = -0.5f;

    /// Determine which characters can see/hear the event based on position and radius.
    std::vector<std::string> findWitnesses(
        const WorldEvent& event,
        const std::unordered_map<std::string, CharacterProfile>& characters);

    /// Select which facts a speaker would share, based on personality and relationship.
    std::vector<const KnowledgeFact*> selectFactsToShare(
        const CharacterProfile& speakerProfile,
        const CharacterProfile& listenerProfile,
        const CharacterMemory& speakerMemory,
        int maxFacts);

    /// Spread a single rumor from speaker to listener if chance passes.
    void spreadRumorBetween(
        const CharacterProfile& speakerProfile,
        const CharacterProfile& listenerProfile,
        const CharacterMemory& speakerMemory,
        CharacterMemory& listenerMemory,
        float chance);

    /// Simple deterministic hash for rumor selection (avoids <random> dependency).
    static size_t hashCombine(size_t a, size_t b);
};

} // namespace Story
} // namespace Phyxel
