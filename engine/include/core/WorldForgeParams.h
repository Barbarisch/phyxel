#pragma once

#include <cstdint>
#include <string>
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
    // ── M3a (docs/WorldForgeV2Plan.md M3, D18/D19): the continent-wide civ plan ──────────────
    // civ: site at macro resolution over the whole continent (the box plan is then seeded by the
    // civ sites inside it and the 3–8 clamp lifts). civSpacingKm: Christaller's √3 ladder per tier
    // city/town/village/hamlet/outpost (§10 F4, 62/36/21/12/7 km); civSlopeCap: rise/run at the
    // macro cell a tier accepts (the M0 road-grade caps 0.05/0.08/0.15 reused — REASONED);
    // civDensityScale: multiplies every tier's DENSITY (the ladder spacings shrink by sqrt(scale);
    // the targets follow from the same formula; 1 = the ladder's own density).
    bool  civ = false;
    float civSpacingKm[5] = {62.0f, 36.0f, 21.0f, 12.0f, 7.0f};
    float civSlopeCap[5]  = {0.05f, 0.05f, 0.08f, 0.15f, 0.15f};
    float civDensityScale = 1.0f;
    std::vector<std::string> pinTiers;  // M3a: per sitePin tier name ("town" ...); empty = by score rank

    bool operator==(const WorldForgeParams& o) const {
        if (civ != o.civ || civDensityScale != o.civDensityScale || pinTiers != o.pinTiers) return false;
        for (int t = 0; t < 5; ++t) if (civSpacingKm[t] != o.civSpacingKm[t] || civSlopeCap[t] != o.civSlopeCap[t]) return false;
        return enabled == o.enabled && version == o.version && siteCount == o.siteCount &&
               regionRadius == o.regionRadius && minSpacing == o.minSpacing &&
               maxSpacing == o.maxSpacing && sitePins == o.sitePins;
    }
    bool operator!=(const WorldForgeParams& o) const { return !(*this == o); }

    /// Boundary clamp (FeatureDesignKeys API rule: clamp at the boundary, echo the result).
    /// Returns the clamped copy; callers diff against the input to report what was clamped.
    WorldForgeParams clamped() const {
        WorldForgeParams p = *this;
        p.siteCount = glm::clamp(p.siteCount, 3, p.civ ? 256 : 8);   // M3a: the civ plan seeds the box
        for (int t = 0; t < 5; ++t) {
            p.civSpacingKm[t] = glm::clamp(p.civSpacingKm[t], 1.0f, 500.0f);
            p.civSlopeCap[t]  = glm::clamp(p.civSlopeCap[t], 0.005f, 1.0f);
        }
        p.civDensityScale = glm::clamp(p.civDensityScale, 0.05f, 10.0f);
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
        if (civ) {
            j["civ"] = true;
            j["civSpacingKm"] = nlohmann::json::array({civSpacingKm[0], civSpacingKm[1], civSpacingKm[2], civSpacingKm[3], civSpacingKm[4]});
            j["civSlopeCap"]  = nlohmann::json::array({civSlopeCap[0], civSlopeCap[1], civSlopeCap[2], civSlopeCap[3], civSlopeCap[4]});
            j["civDensityScale"] = civDensityScale;
        }
        if (!pinTiers.empty()) j["pinTiers"] = pinTiers;
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
        p.civ = j.value("civ", false);
        if (j.contains("civSpacingKm") && j["civSpacingKm"].is_array() && j["civSpacingKm"].size() == 5)
            for (int t = 0; t < 5; ++t) p.civSpacingKm[t] = j["civSpacingKm"][t].get<float>();
        if (j.contains("civSlopeCap") && j["civSlopeCap"].is_array() && j["civSlopeCap"].size() == 5)
            for (int t = 0; t < 5; ++t) p.civSlopeCap[t] = j["civSlopeCap"][t].get<float>();
        p.civDensityScale = j.value("civDensityScale", 1.0f);
        if (j.contains("pinTiers") && j["pinTiers"].is_array())
            for (const auto& t : j["pinTiers"]) p.pinTiers.push_back(t.get<std::string>());
        return p;
    }
};

}  // namespace Phyxel
