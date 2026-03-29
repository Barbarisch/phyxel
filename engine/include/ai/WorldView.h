#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace AI {

// ============================================================================
// Belief — subjective "fact" an NPC holds (may differ from reality)
// ============================================================================

struct Belief {
    std::string key;            ///< e.g. "blacksmith_location", "king_is_alive"
    std::string value;          ///< e.g. "market_square", "true"
    float confidence = 1.0f;    ///< 0.0 to 1.0
    float timestamp = 0.0f;     ///< Game time when last updated

    nlohmann::json toJson() const;
    static Belief fromJson(const nlohmann::json& j);
};

// ============================================================================
// Opinion — how an NPC feels about a topic, entity, or concept
// ============================================================================

struct Opinion {
    std::string subject;        ///< What/who the opinion is about (entity ID, faction, concept)
    float sentiment = 0.0f;    ///< -1.0 (hate) to 1.0 (love)
    std::string reason;         ///< Short description why
    float timestamp = 0.0f;

    nlohmann::json toJson() const;
    static Opinion fromJson(const nlohmann::json& j);
};

// ============================================================================
// Observation — something the NPC witnessed or was told
// ============================================================================

struct Observation {
    std::string eventId;        ///< Unique event identifier
    std::string description;    ///< What happened
    std::string location;       ///< Where it happened (location ID)
    float timestamp = 0.0f;     ///< When it happened
    bool firsthand = true;      ///< Saw it vs was told

    nlohmann::json toJson() const;
    static Observation fromJson(const nlohmann::json& j);
};

// ============================================================================
// WorldView — per-NPC subjective model of the world
// ============================================================================

/// Each NPC has their own WorldView. Two NPCs may hold contradictory beliefs
/// about the same key. Beliefs decay in confidence over time. Observations
/// accumulate and can be shared through gossip (creating secondhand beliefs
/// in other NPCs' WorldViews).
class WorldView {
public:
    // --- Beliefs ---
    void setBelief(const std::string& key, const std::string& value, float confidence = 1.0f, float timestamp = 0.0f);
    const Belief* getBelief(const std::string& key) const;
    bool hasBelief(const std::string& key) const;
    void removeBelief(const std::string& key);
    const std::unordered_map<std::string, Belief>& getAllBeliefs() const { return m_beliefs; }

    // --- Opinions ---
    void setOpinion(const std::string& subject, float sentiment, const std::string& reason = "",
                    float timestamp = 0.0f);
    const Opinion* getOpinion(const std::string& subject) const;
    float getSentiment(const std::string& subject) const; ///< Returns 0 if no opinion
    void removeOpinion(const std::string& subject);
    const std::unordered_map<std::string, Opinion>& getAllOpinions() const { return m_opinions; }

    // --- Observations ---
    void addObservation(const Observation& obs);
    const std::vector<Observation>& getObservations() const { return m_observations; }
    std::vector<const Observation*> getObservationsAt(const std::string& locationId) const;
    std::vector<const Observation*> getRecentObservations(int count) const;

    /// Decay belief confidence over time. Very low confidence beliefs are removed.
    void update(float deltaHours);

    /// Build a context string for AI/LLM use summarizing this NPC's worldview.
    std::string buildContextSummary(int maxBeliefs = 10, int maxOpinions = 10,
                                     int maxObservations = 5) const;

    /// Total item count across all categories.
    size_t size() const;

    // --- Serialization ---
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::unordered_map<std::string, Belief> m_beliefs;
    std::unordered_map<std::string, Opinion> m_opinions;
    std::vector<Observation> m_observations;

    static constexpr float CONFIDENCE_DECAY_RATE = 0.005f;   ///< Per game hour
    static constexpr float CONFIDENCE_REMOVE_THRESHOLD = 0.05f;
    static constexpr int MAX_OBSERVATIONS = 100;
};

} // namespace AI
} // namespace Phyxel
