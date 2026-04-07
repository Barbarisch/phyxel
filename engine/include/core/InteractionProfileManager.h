#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

/// Per-archetype tuning offsets for a specific interaction point on a specific asset.
struct InteractionProfile {
    glm::vec3 sitDownOffset{0.0f};
    glm::vec3 sittingIdleOffset{0.0f};
    glm::vec3 sitStandUpOffset{0.0f};
    float sitBlendDuration = 0.0f;
    float seatHeightOffset = 0.0f;
};

/// Manages interaction profiles: per-(archetype, templateName, pointId) tuning data.
/// Profiles live in JSON files under resources/interactions/<archetype>.json.
class InteractionProfileManager {
public:
    /// Set the base directory for profile JSON files (e.g. "resources/interactions/").
    void setBasePath(const std::string& path) { m_basePath = path; }
    const std::string& getBasePath() const { return m_basePath; }

    /// Load all profiles for an archetype from its JSON file. Returns false if file missing.
    bool loadArchetype(const std::string& archetype);

    /// Save all profiles for an archetype to its JSON file.
    bool saveArchetype(const std::string& archetype) const;

    /// Get a specific profile, or nullptr if none exists.
    const InteractionProfile* getProfile(const std::string& archetype,
                                          const std::string& templateName,
                                          const std::string& pointId) const;

    /// Set (create or overwrite) a specific profile.
    void setProfile(const std::string& archetype,
                    const std::string& templateName,
                    const std::string& pointId,
                    const InteractionProfile& profile);

    /// Check if a profile exists.
    bool hasProfile(const std::string& archetype,
                    const std::string& templateName,
                    const std::string& pointId) const;

    /// Get all template→pointId→profile mappings for an archetype (for UI editing).
    using PointMap = std::unordered_map<std::string, InteractionProfile>;
    using TemplateMap = std::unordered_map<std::string, PointMap>;
    TemplateMap& getMutableProfiles(const std::string& archetype);

    /// Get list of all archetypes that have been loaded or modified.
    std::vector<std::string> getLoadedArchetypes() const;

private:
    std::string m_basePath = "resources/interactions/";
    // archetype → templateName → pointId → profile
    std::unordered_map<std::string, TemplateMap> m_profiles;
};

} // namespace Core
} // namespace Phyxel
