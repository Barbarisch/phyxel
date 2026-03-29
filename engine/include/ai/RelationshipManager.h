#pragma once

#include "story/CharacterProfile.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace AI {

/// Type of social interaction that modifies relationships.
enum class InteractionType {
    Greeting,       ///< Friendly hello
    Conversation,   ///< Extended chat
    Trade,          ///< Successful trade
    Gift,           ///< Gave something
    Insult,         ///< Verbal hostility
    Attack,         ///< Physical violence
    Help,           ///< Aided in danger
    Betray,         ///< Broke trust
    Gossip,         ///< Shared rumors (can be positive or negative)
    Witnessed       ///< Saw an event (assault, theft, etc.)
};

std::string interactionTypeToString(InteractionType type);
InteractionType interactionTypeFromString(const std::string& str);

/// Callback for relationship change events.
using RelationshipChangeCallback = std::function<void(
    const std::string& characterA, const std::string& characterB,
    const Story::Relationship& newRelationship, InteractionType cause)>;

/// Manages pairwise NPC-to-NPC and NPC-to-player relationships.
/// Wraps the existing Story::Relationship struct with efficient lookups
/// and event-driven modification.
class RelationshipManager {
public:
    /// Get the relationship from character A toward character B.
    /// Returns a default (neutral) relationship if none exists.
    Story::Relationship getRelationship(const std::string& fromId, const std::string& toId) const;

    /// Set the full relationship from A to B.
    void setRelationship(const std::string& fromId, const std::string& toId,
                         const Story::Relationship& rel);

    /// Check if a relationship exists.
    bool hasRelationship(const std::string& fromId, const std::string& toId) const;

    /// Get all relationships for a character (everyone they have opinions about).
    std::vector<std::pair<std::string, Story::Relationship>>
    getRelationshipsFor(const std::string& characterId) const;

    /// Get characters who have opinions about the given target.
    std::vector<std::pair<std::string, Story::Relationship>>
    getRelationshipsAbout(const std::string& targetId) const;

    /// Apply a social interaction effect. Adjusts relationship values based on interaction type.
    void applyInteraction(const std::string& fromId, const std::string& toId,
                          InteractionType type, float intensity = 1.0f);

    /// Apply a faction-wide reputation change. All members' trust shifts toward the actor.
    void applyFactionReputation(const std::string& actorId,
                                const std::string& factionId,
                                float trustDelta,
                                const std::vector<std::string>& factionMembers);

    /// Decay relationships slightly over time (toward neutral).
    /// Call with delta game hours.
    void update(float deltaHours);

    /// Register a callback for relationship changes.
    void onRelationshipChanged(RelationshipChangeCallback callback);

    /// Remove all relationships involving a character.
    void removeCharacter(const std::string& characterId);

    /// Get average disposition (trust+affection+respect-fear) from A toward B.
    float getDisposition(const std::string& fromId, const std::string& toId) const;

    /// Number of tracked relationships.
    size_t size() const;

    /// Serialize.
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    /// Key: "fromId->toId"
    static std::string makeKey(const std::string& from, const std::string& to) {
        return from + "->" + to;
    }

    void notifyChange(const std::string& fromId, const std::string& toId,
                      const Story::Relationship& rel, InteractionType cause);

    std::unordered_map<std::string, Story::Relationship> m_relationships;
    std::vector<RelationshipChangeCallback> m_callbacks;
    float m_decayRate = 0.01f;  ///< Per-hour drift toward neutral
};

} // namespace AI
} // namespace Phyxel
