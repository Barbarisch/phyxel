#include "core/LocationRegistry.h"
#include <algorithm>
#include <limits>

namespace Phyxel {
namespace Core {

// ============================================================================
// Location
// ============================================================================

LocationType Location::typeFromString(const std::string& s) {
    if (s == "Home")       return LocationType::Home;
    if (s == "Work")       return LocationType::Work;
    if (s == "Tavern")     return LocationType::Tavern;
    if (s == "Market")     return LocationType::Market;
    if (s == "Temple")     return LocationType::Temple;
    if (s == "Farm")       return LocationType::Farm;
    if (s == "GuardPost")  return LocationType::GuardPost;
    if (s == "Wilderness") return LocationType::Wilderness;
    return LocationType::Custom;
}

std::string Location::typeToString(LocationType t) {
    switch (t) {
        case LocationType::Home:       return "Home";
        case LocationType::Work:       return "Work";
        case LocationType::Tavern:     return "Tavern";
        case LocationType::Market:     return "Market";
        case LocationType::Temple:     return "Temple";
        case LocationType::Farm:       return "Farm";
        case LocationType::GuardPost:  return "GuardPost";
        case LocationType::Wilderness: return "Wilderness";
        case LocationType::Custom:     return "Custom";
    }
    return "Custom";
}

nlohmann::json Location::toJson() const {
    return {
        {"id", id},
        {"name", name},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}}},
        {"radius", radius},
        {"type", typeToString(type)}
    };
}

Location Location::fromJson(const nlohmann::json& j) {
    Location loc;
    loc.id = j.value("id", "");
    loc.name = j.value("name", loc.id);
    if (j.contains("position")) {
        loc.position.x = j["position"].value("x", 0.0f);
        loc.position.y = j["position"].value("y", 0.0f);
        loc.position.z = j["position"].value("z", 0.0f);
    } else {
        loc.position.x = j.value("x", 0.0f);
        loc.position.y = j.value("y", 0.0f);
        loc.position.z = j.value("z", 0.0f);
    }
    loc.radius = j.value("radius", 3.0f);
    loc.type = typeFromString(j.value("type", "Custom"));
    return loc;
}

// ============================================================================
// LocationRegistry
// ============================================================================

void LocationRegistry::addLocation(const Location& loc) {
    m_locations[loc.id] = loc;
}

bool LocationRegistry::removeLocation(const std::string& id) {
    return m_locations.erase(id) > 0;
}

const Location* LocationRegistry::getLocation(const std::string& id) const {
    auto it = m_locations.find(id);
    return it != m_locations.end() ? &it->second : nullptr;
}

std::vector<const Location*> LocationRegistry::getLocationsByType(LocationType type) const {
    std::vector<const Location*> result;
    for (const auto& [id, loc] : m_locations) {
        if (loc.type == type) {
            result.push_back(&loc);
        }
    }
    return result;
}

const Location* LocationRegistry::getNearestLocation(const glm::vec3& position) const {
    const Location* nearest = nullptr;
    float bestDist = std::numeric_limits<float>::max();
    for (const auto& [id, loc] : m_locations) {
        float dist = glm::length(position - loc.position);
        if (dist < bestDist) {
            bestDist = dist;
            nearest = &loc;
        }
    }
    return nearest;
}

std::vector<const Location*> LocationRegistry::getLocationsNear(const glm::vec3& position, float radius) const {
    std::vector<const Location*> result;
    float radiusSq = radius * radius;
    for (const auto& [id, loc] : m_locations) {
        float distSq = glm::dot(position - loc.position, position - loc.position);
        if (distSq <= radiusSq) {
            result.push_back(&loc);
        }
    }
    return result;
}

const Location* LocationRegistry::getLocationAt(const glm::vec3& position) const {
    for (const auto& [id, loc] : m_locations) {
        float dist = glm::length(position - loc.position);
        if (dist <= loc.radius) {
            return &loc;
        }
    }
    return nullptr;
}

nlohmann::json LocationRegistry::toJson() const {
    auto arr = nlohmann::json::array();
    for (const auto& [id, loc] : m_locations) {
        arr.push_back(loc.toJson());
    }
    return arr;
}

void LocationRegistry::fromJson(const nlohmann::json& arr) {
    for (const auto& j : arr) {
        auto loc = Location::fromJson(j);
        if (!loc.id.empty()) {
            m_locations[loc.id] = std::move(loc);
        }
    }
}

} // namespace Core
} // namespace Phyxel
