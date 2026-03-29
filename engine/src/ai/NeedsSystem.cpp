#include "ai/NeedsSystem.h"
#include <algorithm>
#include <limits>

namespace Phyxel {
namespace AI {

// ============================================================================
// NeedType string conversions
// ============================================================================

std::string needTypeToString(NeedType type) {
    switch (type) {
        case NeedType::Hunger:        return "Hunger";
        case NeedType::Rest:          return "Rest";
        case NeedType::Social:        return "Social";
        case NeedType::Safety:        return "Safety";
        case NeedType::Entertainment: return "Entertainment";
        case NeedType::Comfort:       return "Comfort";
    }
    return "Hunger";
}

NeedType needTypeFromString(const std::string& str) {
    if (str == "Hunger")        return NeedType::Hunger;
    if (str == "Rest")          return NeedType::Rest;
    if (str == "Social")        return NeedType::Social;
    if (str == "Safety")        return NeedType::Safety;
    if (str == "Entertainment") return NeedType::Entertainment;
    if (str == "Comfort")       return NeedType::Comfort;
    return NeedType::Hunger;
}

// ============================================================================
// Need
// ============================================================================

nlohmann::json Need::toJson() const {
    return {
        {"type", needTypeToString(type)},
        {"value", value},
        {"decayRate", decayRate},
        {"urgencyThreshold", urgencyThreshold}
    };
}

Need Need::fromJson(const nlohmann::json& j) {
    Need n;
    n.type = needTypeFromString(j.value("type", "Hunger"));
    n.value = j.value("value", 100.0f);
    n.decayRate = j.value("decayRate", 1.0f);
    n.urgencyThreshold = j.value("urgencyThreshold", 30.0f);
    return n;
}

// ============================================================================
// NeedsSystem
// ============================================================================

NeedsSystem::NeedsSystem() {
    initDefaults();
}

void NeedsSystem::initDefaults() {
    m_needs.clear();
    // Default needs with different decay rates
    setNeed({NeedType::Hunger,        80.0f, 2.0f,  30.0f});  // Gets hungry relatively fast
    setNeed({NeedType::Rest,          90.0f, 1.5f,  25.0f});  // Tired after a long day
    setNeed({NeedType::Social,        70.0f, 0.8f,  35.0f});  // Slowly gets lonely
    setNeed({NeedType::Safety,       100.0f, 0.0f,  40.0f});  // Only drops from events, not time
    setNeed({NeedType::Entertainment, 60.0f, 0.5f,  20.0f});  // Boredom creeps in slowly
    setNeed({NeedType::Comfort,       85.0f, 0.3f,  25.0f});  // Slight decay
}

void NeedsSystem::setNeed(const Need& need) {
    m_needs[need.type] = need;
}

void NeedsSystem::removeNeed(NeedType type) {
    m_needs.erase(type);
}

const Need* NeedsSystem::getNeed(NeedType type) const {
    auto it = m_needs.find(type);
    return it != m_needs.end() ? &it->second : nullptr;
}

Need* NeedsSystem::getNeedMut(NeedType type) {
    auto it = m_needs.find(type);
    return it != m_needs.end() ? &it->second : nullptr;
}

void NeedsSystem::update(float deltaHours) {
    for (auto& [type, need] : m_needs) {
        need.value -= need.decayRate * deltaHours;
        need.value = std::max(0.0f, need.value);
    }
}

void NeedsSystem::fulfill(NeedType type, float amount) {
    auto it = m_needs.find(type);
    if (it != m_needs.end()) {
        it->second.value = std::min(100.0f, it->second.value + amount);
    }
}

const Need* NeedsSystem::getMostUrgent() const {
    const Need* most = nullptr;
    float lowest = std::numeric_limits<float>::max();
    for (const auto& [type, need] : m_needs) {
        if (need.value < lowest) {
            lowest = need.value;
            most = &need;
        }
    }
    return most;
}

std::vector<const Need*> NeedsSystem::getUrgentNeeds() const {
    std::vector<const Need*> urgent;
    for (const auto& [type, need] : m_needs) {
        if (need.isUrgent()) {
            urgent.push_back(&need);
        }
    }
    return urgent;
}

std::vector<std::pair<NeedType, float>> NeedsSystem::getFulfillmentForActivity(const std::string& activity) {
    // Map activity names (from Schedule ActivityType) to which needs they fulfill
    if (activity == "Eat") {
        return {{NeedType::Hunger, 40.0f}, {NeedType::Comfort, 10.0f}};
    }
    if (activity == "Sleep") {
        return {{NeedType::Rest, 60.0f}, {NeedType::Comfort, 20.0f}};
    }
    if (activity == "Socialize") {
        return {{NeedType::Social, 30.0f}, {NeedType::Entertainment, 15.0f}};
    }
    if (activity == "Worship") {
        return {{NeedType::Comfort, 25.0f}, {NeedType::Social, 10.0f}};
    }
    if (activity == "Shop") {
        return {{NeedType::Entertainment, 10.0f}};
    }
    if (activity == "Guard" || activity == "Patrol") {
        return {{NeedType::Safety, 5.0f}};
    }
    if (activity == "Train") {
        return {{NeedType::Entertainment, 20.0f}, {NeedType::Safety, 10.0f}};
    }
    if (activity == "Work") {
        return {{NeedType::Comfort, -5.0f}};  // Work slightly drains comfort
    }
    // Wander gives a tiny entertainment boost
    if (activity == "Wander") {
        return {{NeedType::Entertainment, 5.0f}};
    }
    return {};
}

nlohmann::json NeedsSystem::toJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [type, need] : m_needs) {
        arr.push_back(need.toJson());
    }
    return arr;
}

void NeedsSystem::fromJson(const nlohmann::json& j) {
    m_needs.clear();
    if (j.is_array()) {
        for (const auto& item : j) {
            Need n = Need::fromJson(item);
            m_needs[n.type] = n;
        }
    }
}

} // namespace AI
} // namespace Phyxel
