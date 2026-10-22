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
    // M3a (D18 local refinement): civ-seeded pins are macro-cell CENTRES, not owner-chosen columns,
    // so the box plan refines them on its 16 u grid like any scored site (owner pins stay verbatim).
    // Set by the generator when it seeds the box from the civ plan; never serialized.
    bool pinRefine = false;
    // ── M3b (D23): the history ledger. civEpochs = number of 25-year epochs simulated after the
    // founding (0 = no history: the plan is M3a's bit for bit); civShockEpoch = the epoch of the one
    // population shock (−1 = none); civShockFraction = the share of every population index the
    // shock removes (0.45 GROUNDED: England ≈ 4.7–6 M in 1300 → ≈ 2.5–3 M at the 1377 poll tax).
    int   civEpochs = 0;
    int   civShockEpoch = 9;
    float civShockFraction = 0.45f;
    // civAbandonShare: the share of village-and-below sites (the lowest-suitability ones, Beresford's
    // marginal land) the shock empties. 0.20 GROUNDED as a RATE: 2 811 deserted medieval villages of
    // ≈ 13 000 Domesday places (§10 F4g). It is an INPUT — no mechanism derives it (NEEDS-RESEARCH).
    float civAbandonShare = 0.20f;
    // ── M4a (D27): the corridor graph's reuse discount — a routing step onto a cell already on the
    // network costs this fraction of its price (Veloren 1/3, REASONED: no historical source yet);
    // also the box plan's cost multiplier inside the corridor band. 1 = no reuse (V1 behaviour).
    float routeReuseFraction = 1.0f / 3.0f;

    bool operator==(const WorldForgeParams& o) const {
        if (civ != o.civ || civDensityScale != o.civDensityScale || pinTiers != o.pinTiers || pinRefine != o.pinRefine) return false;
        if (civEpochs != o.civEpochs || civShockEpoch != o.civShockEpoch || civShockFraction != o.civShockFraction || civAbandonShare != o.civAbandonShare) return false;
        if (routeReuseFraction != o.routeReuseFraction) return false;
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
        p.civEpochs = glm::clamp(p.civEpochs, 0, 64);                       // M3b: 64 × 25 y = 1600 y, the ledger's bound
        p.civShockEpoch = glm::clamp(p.civShockEpoch, -1, p.civEpochs);     // −1 = no shock; never past the last epoch
        if (p.civShockEpoch == 0 && p.civEpochs > 0) p.civShockEpoch = 1;   // epoch 0 is the founding; the first simulated epoch is 1
        p.civAbandonShare = glm::clamp(p.civAbandonShare, 0.0f, 1.0f);
        p.routeReuseFraction = glm::clamp(p.routeReuseFraction, 0.05f, 1.0f);   // 0 would make every network cell free and A* degenerate
        p.civShockFraction = glm::clamp(p.civShockFraction, 0.0f, 0.9f);   // a 100 % shock would abandon every site
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
            j["civEpochs"] = civEpochs;
            j["civShockEpoch"] = civShockEpoch;
            j["civShockFraction"] = civShockFraction;
            j["civAbandonShare"] = civAbandonShare;
            j["routeReuseFraction"] = routeReuseFraction;
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
        p.civEpochs = j.value("civEpochs", 0);
        p.civShockEpoch = j.value("civShockEpoch", 9);
        p.civShockFraction = j.value("civShockFraction", 0.45f);
        p.civAbandonShare = j.value("civAbandonShare", 0.20f);
        p.routeReuseFraction = j.value("routeReuseFraction", 1.0f / 3.0f);
        if (j.contains("pinTiers") && j["pinTiers"].is_array())
            for (const auto& t : j["pinTiers"]) p.pinTiers.push_back(t.get<std::string>());
        return p;
    }
};

}  // namespace Phyxel
