#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {

/// World-scale planning parameters for the WorldForge (docs/WorldForge.md): how many
/// settlements to site, over what region, at what spacing. Persisted per world in the
/// recipe (world.db world_meta["recipe"], key "worldforge") so a world's plan is
/// reproducible from its DB alone. Header-only (WorldRecipe pattern) so the engine glob
/// needs no reconfigure. Absent recipe key → disabled → zero behavior change for legacy
/// worlds (pinned by WorldForgeRecipeTest.LegacyRecipeDisabledByteIdentical).
struct WorldForgeParams {
    bool enabled = false;
    int version = 1;
    int siteCount = 5;            // settlements to site; clamped 3..8 (V1 region scale)
    float regionRadius = 2048.0f; // world units from the hydrology-region centre; clamped 512..8192
                                  // (stays inside the ±16384 hydrology bake box)
    float minSpacing = 400.0f;    // hard minimum between site centres (world units)
    float maxSpacing = 1500.0f;   // soft: a candidate farther than this from EVERY picked site
                                  // is score-halved (keeps the region connected, not scattered)
    std::vector<glm::ivec2> sitePins;  // optional user-pinned site centres, seated verbatim first

    bool operator==(const WorldForgeParams& o) const {
        return enabled == o.enabled && version == o.version && siteCount == o.siteCount &&
               regionRadius == o.regionRadius && minSpacing == o.minSpacing &&
               maxSpacing == o.maxSpacing && sitePins == o.sitePins;
    }
    bool operator!=(const WorldForgeParams& o) const { return !(*this == o); }

    /// Boundary clamp (FeatureDesignKeys API rule: clamp at the boundary, echo the result).
    /// Returns the clamped copy; callers diff against the input to report what was clamped.
    WorldForgeParams clamped() const {
        WorldForgeParams p = *this;
        p.siteCount = glm::clamp(p.siteCount, 3, 8);
        p.regionRadius = glm::clamp(p.regionRadius, 512.0f, 8192.0f);
        p.minSpacing = glm::clamp(p.minSpacing, 64.0f, 2.0f * p.regionRadius);
        p.maxSpacing = glm::clamp(p.maxSpacing, p.minSpacing, 4.0f * p.regionRadius);
        return p;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["enabled"] = enabled;
        j["version"] = version;
        j["siteCount"] = siteCount;
        j["regionRadius"] = regionRadius;
        j["minSpacing"] = minSpacing;
        j["maxSpacing"] = maxSpacing;
        if (!sitePins.empty()) {
            nlohmann::json pins = nlohmann::json::array();
            for (const auto& p : sitePins) pins.push_back({{"x", p.x}, {"z", p.y}});
            j["sitePins"] = pins;
        }
        return j;
    }

    static WorldForgeParams fromJson(const nlohmann::json& j) {
        WorldForgeParams p;
        if (!j.is_object()) return p;
        p.enabled = j.value("enabled", false);
        p.version = j.value("version", 1);
        p.siteCount = j.value("siteCount", 5);
        p.regionRadius = j.value("regionRadius", 2048.0f);
        p.minSpacing = j.value("minSpacing", 400.0f);
        p.maxSpacing = j.value("maxSpacing", 1500.0f);
        if (j.contains("sitePins") && j["sitePins"].is_array())
            for (const auto& pin : j["sitePins"])
                p.sitePins.push_back({pin.value("x", 0), pin.value("z", 0)});
        return p;
    }
};

}  // namespace Phyxel
