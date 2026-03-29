#include "ai/WorldView.h"
#include <algorithm>
#include <sstream>

namespace Phyxel {
namespace AI {

// ============================================================================
// Belief serialization
// ============================================================================

nlohmann::json Belief::toJson() const {
    return {{"key", key}, {"value", value}, {"confidence", confidence}, {"timestamp", timestamp}};
}

Belief Belief::fromJson(const nlohmann::json& j) {
    Belief b;
    b.key = j.value("key", "");
    b.value = j.value("value", "");
    b.confidence = j.value("confidence", 1.0f);
    b.timestamp = j.value("timestamp", 0.0f);
    return b;
}

// ============================================================================
// Opinion serialization
// ============================================================================

nlohmann::json Opinion::toJson() const {
    return {{"subject", subject}, {"sentiment", sentiment}, {"reason", reason}, {"timestamp", timestamp}};
}

Opinion Opinion::fromJson(const nlohmann::json& j) {
    Opinion o;
    o.subject = j.value("subject", "");
    o.sentiment = j.value("sentiment", 0.0f);
    o.reason = j.value("reason", "");
    o.timestamp = j.value("timestamp", 0.0f);
    return o;
}

// ============================================================================
// Observation serialization
// ============================================================================

nlohmann::json Observation::toJson() const {
    return {{"eventId", eventId}, {"description", description}, {"location", location},
            {"timestamp", timestamp}, {"firsthand", firsthand}};
}

Observation Observation::fromJson(const nlohmann::json& j) {
    Observation o;
    o.eventId = j.value("eventId", "");
    o.description = j.value("description", "");
    o.location = j.value("location", "");
    o.timestamp = j.value("timestamp", 0.0f);
    o.firsthand = j.value("firsthand", true);
    return o;
}

// ============================================================================
// WorldView — Beliefs
// ============================================================================

void WorldView::setBelief(const std::string& key, const std::string& value,
                           float confidence, float timestamp) {
    m_beliefs[key] = Belief{key, value, confidence, timestamp};
}

const Belief* WorldView::getBelief(const std::string& key) const {
    auto it = m_beliefs.find(key);
    return it != m_beliefs.end() ? &it->second : nullptr;
}

bool WorldView::hasBelief(const std::string& key) const {
    return m_beliefs.count(key) > 0;
}

void WorldView::removeBelief(const std::string& key) {
    m_beliefs.erase(key);
}

// ============================================================================
// WorldView — Opinions
// ============================================================================

void WorldView::setOpinion(const std::string& subject, float sentiment,
                            const std::string& reason, float timestamp) {
    m_opinions[subject] = Opinion{subject, sentiment, reason, timestamp};
}

const Opinion* WorldView::getOpinion(const std::string& subject) const {
    auto it = m_opinions.find(subject);
    return it != m_opinions.end() ? &it->second : nullptr;
}

float WorldView::getSentiment(const std::string& subject) const {
    auto it = m_opinions.find(subject);
    return it != m_opinions.end() ? it->second.sentiment : 0.0f;
}

void WorldView::removeOpinion(const std::string& subject) {
    m_opinions.erase(subject);
}

// ============================================================================
// WorldView — Observations
// ============================================================================

void WorldView::addObservation(const Observation& obs) {
    m_observations.push_back(obs);
    // Trim oldest if over limit
    if (static_cast<int>(m_observations.size()) > MAX_OBSERVATIONS) {
        m_observations.erase(m_observations.begin());
    }
}

std::vector<const Observation*> WorldView::getObservationsAt(const std::string& locationId) const {
    std::vector<const Observation*> result;
    for (const auto& obs : m_observations) {
        if (obs.location == locationId) result.push_back(&obs);
    }
    return result;
}

std::vector<const Observation*> WorldView::getRecentObservations(int count) const {
    std::vector<const Observation*> result;
    int start = std::max(0, static_cast<int>(m_observations.size()) - count);
    for (int i = start; i < static_cast<int>(m_observations.size()); ++i) {
        result.push_back(&m_observations[i]);
    }
    return result;
}

// ============================================================================
// WorldView — Update / Decay
// ============================================================================

void WorldView::update(float deltaHours) {
    // Decay belief confidence
    for (auto it = m_beliefs.begin(); it != m_beliefs.end(); ) {
        it->second.confidence -= CONFIDENCE_DECAY_RATE * deltaHours;
        if (it->second.confidence < CONFIDENCE_REMOVE_THRESHOLD) {
            it = m_beliefs.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// WorldView — Context Summary
// ============================================================================

std::string WorldView::buildContextSummary(int maxBeliefs, int maxOpinions,
                                            int maxObservations) const {
    std::ostringstream ss;

    // Beliefs (highest confidence first)
    if (!m_beliefs.empty()) {
        std::vector<const Belief*> sorted;
        for (const auto& [k, b] : m_beliefs) sorted.push_back(&b);
        std::sort(sorted.begin(), sorted.end(),
                  [](const Belief* a, const Belief* b) { return a->confidence > b->confidence; });
        ss << "Beliefs:\n";
        int count = 0;
        for (const auto* b : sorted) {
            if (count >= maxBeliefs) break;
            ss << "- " << b->key << " = " << b->value
               << " (confidence: " << static_cast<int>(b->confidence * 100) << "%)\n";
            ++count;
        }
    }

    // Opinions (strongest first)
    if (!m_opinions.empty()) {
        std::vector<const Opinion*> sorted;
        for (const auto& [k, o] : m_opinions) sorted.push_back(&o);
        std::sort(sorted.begin(), sorted.end(),
                  [](const Opinion* a, const Opinion* b) {
                      return std::abs(a->sentiment) > std::abs(b->sentiment);
                  });
        ss << "Opinions:\n";
        int count = 0;
        for (const auto* o : sorted) {
            if (count >= maxOpinions) break;
            ss << "- " << o->subject << ": "
               << (o->sentiment > 0 ? "positive" : o->sentiment < 0 ? "negative" : "neutral")
               << " (" << o->sentiment << ")";
            if (!o->reason.empty()) ss << " — " << o->reason;
            ss << "\n";
            ++count;
        }
    }

    // Observations (most recent first)
    if (!m_observations.empty()) {
        ss << "Recent observations:\n";
        int count = 0;
        for (int i = static_cast<int>(m_observations.size()) - 1; i >= 0 && count < maxObservations; --i, ++count) {
            const auto& obs = m_observations[i];
            ss << "- " << obs.description;
            if (!obs.location.empty()) ss << " at " << obs.location;
            ss << (obs.firsthand ? " (seen)" : " (heard)") << "\n";
        }
    }

    return ss.str();
}

// ============================================================================
// WorldView — Size / Serialization
// ============================================================================

size_t WorldView::size() const {
    return m_beliefs.size() + m_opinions.size() + m_observations.size();
}

nlohmann::json WorldView::toJson() const {
    nlohmann::json j;
    nlohmann::json beliefsArr = nlohmann::json::array();
    for (const auto& [k, b] : m_beliefs) beliefsArr.push_back(b.toJson());
    j["beliefs"] = beliefsArr;

    nlohmann::json opinionsArr = nlohmann::json::array();
    for (const auto& [k, o] : m_opinions) opinionsArr.push_back(o.toJson());
    j["opinions"] = opinionsArr;

    nlohmann::json obsArr = nlohmann::json::array();
    for (const auto& obs : m_observations) obsArr.push_back(obs.toJson());
    j["observations"] = obsArr;

    return j;
}

void WorldView::fromJson(const nlohmann::json& j) {
    m_beliefs.clear();
    m_opinions.clear();
    m_observations.clear();

    if (j.contains("beliefs") && j["beliefs"].is_array()) {
        for (const auto& item : j["beliefs"]) {
            auto b = Belief::fromJson(item);
            m_beliefs[b.key] = b;
        }
    }
    if (j.contains("opinions") && j["opinions"].is_array()) {
        for (const auto& item : j["opinions"]) {
            auto o = Opinion::fromJson(item);
            m_opinions[o.subject] = o;
        }
    }
    if (j.contains("observations") && j["observations"].is_array()) {
        for (const auto& item : j["observations"]) {
            m_observations.push_back(Observation::fromJson(item));
        }
    }
}

} // namespace AI
} // namespace Phyxel
