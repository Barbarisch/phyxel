#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Classification of a world location.
enum class LocationType {
    Home,
    Work,
    Tavern,
    Market,
    Temple,
    Farm,
    GuardPost,
    Wilderness,
    Custom
};

/// A named location in the world with a position, radius, and type.
struct Location {
    std::string id;
    std::string name;
    glm::vec3 position{0.0f};
    float radius = 3.0f;          ///< Arrival radius — NPC is "at" this location within this distance
    LocationType type = LocationType::Custom;

    nlohmann::json toJson() const;
    static Location fromJson(const nlohmann::json& j);
    static LocationType typeFromString(const std::string& s);
    static std::string typeToString(LocationType t);
};

/// Central registry of named world locations.
/// Supports spatial queries and lookup by ID/type.
class LocationRegistry {
public:
    LocationRegistry() = default;

    /// Register a new location. Overwrites if ID already exists.
    void addLocation(const Location& loc);

    /// Remove a location by ID. Returns true if removed.
    bool removeLocation(const std::string& id);

    /// Get a location by ID, or nullptr if not found.
    const Location* getLocation(const std::string& id) const;

    /// Get all locations of a given type.
    std::vector<const Location*> getLocationsByType(LocationType type) const;

    /// Get the nearest location to a position.
    const Location* getNearestLocation(const glm::vec3& position) const;

    /// Get all locations within a given radius of a position.
    std::vector<const Location*> getLocationsNear(const glm::vec3& position, float radius) const;

    /// Get the location that contains the given position (within its radius), or nullptr.
    const Location* getLocationAt(const glm::vec3& position) const;

    /// Get all registered locations.
    const std::unordered_map<std::string, Location>& getAllLocations() const { return m_locations; }

    /// Number of registered locations.
    size_t size() const { return m_locations.size(); }

    /// Clear all locations.
    void clear() { m_locations.clear(); }

    /// Serialize all locations to JSON.
    nlohmann::json toJson() const;

    /// Load locations from a JSON array.
    void fromJson(const nlohmann::json& arr);

private:
    std::unordered_map<std::string, Location> m_locations;
};

} // namespace Core
} // namespace Phyxel
