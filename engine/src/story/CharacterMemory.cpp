#include "story/CharacterMemory.h"
#include <algorithm>
#include <sstream>

namespace Phyxel {
namespace Story {

// ============================================================================
// KnowledgeSource conversion
// ============================================================================

std::string knowledgeSourceToString(KnowledgeSource src) {
    switch (src) {
        case KnowledgeSource::Witnessed: return "witnessed";
        case KnowledgeSource::ToldBy:    return "told_by";
        case KnowledgeSource::Rumor:     return "rumor";
        case KnowledgeSource::Innate:    return "innate";
    }
    return "innate";
}

KnowledgeSource knowledgeSourceFromString(const std::string& str) {
    if (str == "witnessed") return KnowledgeSource::Witnessed;
    if (str == "told_by")   return KnowledgeSource::ToldBy;
    if (str == "rumor")     return KnowledgeSource::Rumor;
    return KnowledgeSource::Innate;
}

// ============================================================================
// KnowledgeFact JSON
// ============================================================================

void to_json(nlohmann::json& j, const KnowledgeFact& f) {
    j = nlohmann::json{
        {"factId", f.factId},
        {"summary", f.summary},
        {"source", knowledgeSourceToString(f.source)},
        {"sourceCharacterId", f.sourceCharacterId},
        {"confidence", f.confidence},
        {"timestamp", f.timestamp},
        {"perceivedDetails", f.perceivedDetails}
    };
}

void from_json(const nlohmann::json& j, KnowledgeFact& f) {
    f.factId = j.at("factId").get<std::string>();
    f.summary = j.value("summary", "");
    f.source = knowledgeSourceFromString(j.value("source", "innate"));
    f.sourceCharacterId = j.value("sourceCharacterId", "");
    f.confidence = j.value("confidence", 1.0f);
    f.timestamp = j.value("timestamp", 0.0f);
    f.perceivedDetails = j.value("perceivedDetails", nlohmann::json::object());
}

// ============================================================================
// CharacterMemory — knowledge management
// ============================================================================

void CharacterMemory::witness(const WorldEvent& event, const PersonalityTraits& personality) {
    KnowledgeFact fact;
    fact.factId = event.id;
    fact.summary = event.type + ": " + event.id;
    fact.source = KnowledgeSource::Witnessed;
    fact.confidence = 1.0f;
    fact.timestamp = event.timestamp;
    fact.perceivedDetails = distortPerception(event.details, personality);

    m_facts[fact.factId] = std::move(fact);
}

void CharacterMemory::hearFrom(const std::string& tellerId, const KnowledgeFact& fact,
                                const PersonalityTraits& listenerPersonality) {
    // If already known with higher confidence, don't overwrite
    auto it = m_facts.find(fact.factId);
    if (it != m_facts.end() && it->second.confidence >= fact.confidence)
        return;

    KnowledgeFact heard;
    heard.factId = fact.factId;
    heard.summary = fact.summary;
    heard.source = KnowledgeSource::ToldBy;
    heard.sourceCharacterId = tellerId;
    // Confidence reduction: listener's agreeableness affects how much they trust the info
    // Base: 70% of teller's confidence, +20% if highly agreeable
    heard.confidence = fact.confidence * (0.7f + 0.2f * listenerPersonality.agreeableness);
    heard.timestamp = fact.timestamp;
    heard.perceivedDetails = fact.perceivedDetails; // Inherits teller's distortion

    m_facts[heard.factId] = std::move(heard);
}

void CharacterMemory::addInnateKnowledge(const std::string& factId, const std::string& summary,
                                          const nlohmann::json& details) {
    KnowledgeFact fact;
    fact.factId = factId;
    fact.summary = summary;
    fact.source = KnowledgeSource::Innate;
    fact.confidence = 1.0f;
    fact.timestamp = 0.0f;
    fact.perceivedDetails = details;

    m_facts[fact.factId] = std::move(fact);
}

void CharacterMemory::addFact(KnowledgeFact fact) {
    m_facts[fact.factId] = std::move(fact);
}

// ============================================================================
// CharacterMemory — queries
// ============================================================================

bool CharacterMemory::knowsAbout(const std::string& factId) const {
    return m_facts.count(factId) > 0;
}

const KnowledgeFact* CharacterMemory::getFact(const std::string& factId) const {
    auto it = m_facts.find(factId);
    return (it != m_facts.end()) ? &it->second : nullptr;
}

std::vector<const KnowledgeFact*> CharacterMemory::getFactsAbout(const std::string& topic) const {
    std::vector<const KnowledgeFact*> result;
    for (auto& [id, fact] : m_facts) {
        if (id.find(topic) != std::string::npos || fact.summary.find(topic) != std::string::npos) {
            result.push_back(&fact);
        }
    }
    return result;
}

std::vector<const KnowledgeFact*> CharacterMemory::getRecentFacts(int count) const {
    std::vector<const KnowledgeFact*> all;
    all.reserve(m_facts.size());
    for (auto& [id, fact] : m_facts) {
        all.push_back(&fact);
    }
    // Sort by timestamp descending
    std::sort(all.begin(), all.end(), [](const KnowledgeFact* a, const KnowledgeFact* b) {
        return a->timestamp > b->timestamp;
    });
    if (static_cast<int>(all.size()) > count)
        all.resize(count);
    return all;
}

std::string CharacterMemory::buildContextSummary(int maxFacts) const {
    auto recent = getRecentFacts(maxFacts);

    // Sort by confidence descending (high confidence facts first)
    std::sort(recent.begin(), recent.end(), [](const KnowledgeFact* a, const KnowledgeFact* b) {
        return a->confidence > b->confidence;
    });

    std::ostringstream ss;
    ss << "Known facts (" << recent.size() << "/" << m_facts.size() << "):\n";
    for (auto* fact : recent) {
        ss << "- [" << knowledgeSourceToString(fact->source) << ", confidence="
           << static_cast<int>(fact->confidence * 100) << "%] " << fact->summary << "\n";
    }
    return ss.str();
}

// ============================================================================
// CharacterMemory — update (confidence decay)
// ============================================================================

void CharacterMemory::update(float dt) {
    std::vector<std::string> toRemove;

    for (auto& [id, fact] : m_facts) {
        // Innate and witnessed facts don't decay
        if (fact.source == KnowledgeSource::Innate || fact.source == KnowledgeSource::Witnessed)
            continue;

        // Rumors decay faster than told-by facts
        float decayRate = (fact.source == KnowledgeSource::Rumor) ? 0.02f : 0.005f;
        fact.confidence -= decayRate * dt;

        // Remove facts that have completely faded
        if (fact.confidence <= 0.0f)
            toRemove.push_back(id);
    }

    for (auto& id : toRemove)
        m_facts.erase(id);
}

// ============================================================================
// CharacterMemory — personality distortion
// ============================================================================

nlohmann::json CharacterMemory::distortPerception(const nlohmann::json& realDetails,
                                                    const PersonalityTraits& personality) {
    nlohmann::json distorted = realDetails;

    // High neuroticism: exaggerate danger/threat
    if (personality.neuroticism > 0.7f && distorted.contains("threat_level")) {
        float threat = distorted["threat_level"].get<float>();
        distorted["threat_level"] = std::min(1.0f, threat * (1.0f + personality.neuroticism * 0.5f));
    }

    // High agreeableness: downplay conflict severity
    if (personality.agreeableness > 0.7f && distorted.contains("severity")) {
        float severity = distorted["severity"].get<float>();
        distorted["severity"] = severity * (1.0f - personality.agreeableness * 0.3f);
    }

    // Low openness: miss unusual details
    if (personality.openness < 0.3f) {
        distorted.erase("unusual_detail");
    }

    return distorted;
}

// ============================================================================
// CharacterMemory — serialization
// ============================================================================

nlohmann::json CharacterMemory::toJson() const {
    nlohmann::json j;
    j["facts"] = nlohmann::json::object();
    for (auto& [id, fact] : m_facts) {
        j["facts"][id] = fact;
    }
    return j;
}

void CharacterMemory::fromJson(const nlohmann::json& j) {
    m_facts.clear();
    if (j.contains("facts")) {
        for (auto& [id, val] : j["facts"].items()) {
            m_facts[id] = val.get<KnowledgeFact>();
        }
    }
}

} // namespace Story
} // namespace Phyxel
