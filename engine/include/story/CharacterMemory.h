#pragma once

#include "story/StoryTypes.h"
#include "story/CharacterProfile.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Phyxel {
namespace Story {

// ============================================================================
// KnowledgeSource — how the character learned this fact
// ============================================================================

enum class KnowledgeSource {
    Witnessed,   // Saw/heard it directly (high confidence)
    ToldBy,      // Another character told them
    Rumor,       // Heard through the grapevine (low confidence)
    Innate       // Part of their background/starting knowledge
};

std::string knowledgeSourceToString(KnowledgeSource src);
KnowledgeSource knowledgeSourceFromString(const std::string& str);

// ============================================================================
// KnowledgeFact — a single piece of information a character has
// ============================================================================

struct KnowledgeFact {
    std::string factId;         // References a WorldEvent ID or a custom fact key
    std::string summary;        // Natural language summary (for AI context)
    KnowledgeSource source = KnowledgeSource::Innate;
    std::string sourceCharacterId;  // Who told them (if ToldBy/Rumor)
    float confidence = 1.0f;    // 0.0 to 1.0
    float timestamp = 0.0f;     // World time when learned

    // What the character *remembers*, which may differ from what actually happened.
    // Personality distorts perception at witness time.
    nlohmann::json perceivedDetails;
};

void to_json(nlohmann::json& j, const KnowledgeFact& f);
void from_json(const nlohmann::json& j, KnowledgeFact& f);

// ============================================================================
// CharacterMemory — the knowledge store for one character
// ============================================================================

class CharacterMemory {
public:
    /// Witness a world event directly. Personality may distort perception.
    void witness(const WorldEvent& event, const PersonalityTraits& personality);

    /// Hear about a fact from another character. Trust and personality filter the info.
    void hearFrom(const std::string& tellerId, const KnowledgeFact& fact,
                  const PersonalityTraits& listenerPersonality);

    /// Add background/starting knowledge.
    void addInnateKnowledge(const std::string& factId, const std::string& summary,
                            const nlohmann::json& details = nlohmann::json::object());

    /// Add a raw fact directly (for testing or scripted injection).
    void addFact(KnowledgeFact fact);

    // --- Queries ---

    bool knowsAbout(const std::string& factId) const;
    const KnowledgeFact* getFact(const std::string& factId) const;

    /// Get all facts whose factId or summary contains the given substring.
    std::vector<const KnowledgeFact*> getFactsAbout(const std::string& topic) const;

    /// Get the N most recent facts (by timestamp).
    std::vector<const KnowledgeFact*> getRecentFacts(int count) const;

    /// Get all facts.
    const std::unordered_map<std::string, KnowledgeFact>& getAllFacts() const { return m_facts; }

    /// Total fact count.
    size_t factCount() const { return m_facts.size(); }

    /// Build a natural language context summary for AI agents (most recent + high confidence first).
    std::string buildContextSummary(int maxFacts = 20) const;

    /// Decay confidence over time. Rumors fade faster than witnessed events.
    void update(float dt);

    // --- Serialization ---

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::unordered_map<std::string, KnowledgeFact> m_facts;

    /// Personality-based distortion when witnessing events.
    static nlohmann::json distortPerception(const nlohmann::json& realDetails,
                                             const PersonalityTraits& personality);
};

} // namespace Story
} // namespace Phyxel
