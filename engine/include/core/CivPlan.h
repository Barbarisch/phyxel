#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "core/WorldForgeParams.h"

namespace Phyxel {

class MacroSheet;

// ── M3a: the civ plan — hundreds of sites over the whole continent (docs/WorldForgeV2Plan.md M3,
// D18 / D19 / D20) ──────────────────────────────────────────────────────────────────────────────
//
// Sites are chosen at MACRO resolution (the 512 u sheet: base height, continentalness, the
// erosion bake's drainage area + filled surface + receiver graph, and the raw climate planes),
// so the suitability field covers the 102 km continent that the ±16 km meso box does not (D18).
// Five tiers (city, town, village, hamlet, outpost) nest by Christaller's central-place ladder:
// a tier's sites keep its spacing from every site of its tier or higher, and the per-tier TARGET
// is derived from that spacing and the continent's land area — density ≈ 1 / (0.866·s²) per
// tier, cumulative — never a hand-typed count (D19; §10 F4: the √3 ladder 62/36/21/12/7 km,
// Domesday's ≈ 10 places per 100 km² as the ceiling). Suitability per cell (Emilien bells and
// Veloren's weighted-resource score, §10): flat ground (slope against the tier's cap), water
// near but not in (a macro river, basin or the sea within a few cells), food (temperate, moist
// climate), forest (moisture), a mining band, trade (coast or a large river), biome hostility
// (the treeline/snow rule on the lapse-corrected temperature). Water, basin and sub-sea cells are
// never sites. The pick is greedy per tier from the city down, strictly by score with index ties,
// so the plan is a pure function of (sheet, params, seed). Pins that break a hard rule are
// REFUSED with the reason, never nudged. History, growth and ruins are M3b (D20).
class CivPlan {
public:
    enum Tier : int { City = 0, Town = 1, Village = 2, Hamlet = 3, Outpost = 4, kTiers = 5 };
    static const char* tierName(int tier);
    static int tierFromName(const std::string& name);   // -1 if unknown

    struct Site {
        int   id = 0;
        int   cell = -1;             // row-major macro cell
        glm::vec2 pos{0.0f};         // the cell centre, world XZ
        int   tier = Outpost;
        float score = 0.0f;
        // the parts (for the map overlay and the tests)
        float slope = 0.0f, waterCells = 0.0f, food = 0.0f, forest = 0.0f, mining = 0.0f, trade = 0.0f, hostility = 1.0f;
        bool  pinned = false;
    };

    using ClimateFn = std::function<void(float worldX, float worldZ, float& tempRaw, float& moistRaw)>;
    struct Inputs {
        const MacroSheet* macro = nullptr;
        float    seaLevel = 16.0f;
        uint32_t seed = 0;
        ClimateFn climate;                 // the sheet's raw climate planes at a world position
        float    macroRiverAreaM2 = 5.0e5f; // a macro cell is a river once its drainage reaches this (0.5 km², DESIGN)
    };

    static std::shared_ptr<const CivPlan> bake(const Inputs& in, const WorldForgeParams& params);

    const std::vector<Site>& sites() const { return m_sites; }
    int   target(int tier) const { return m_target[tier]; }     // derived per-tier target
    int   count(int tier) const { return m_count[tier]; }
    float landKm2() const { return m_landKm2; }
    float spacingCells(int tier) const { return m_spacingCells[tier]; }
    const std::vector<std::string>& refusals() const { return m_refusals; }
    float bakeMs() const { return m_bakeMs; }
    nlohmann::json toJson() const;

private:
    std::vector<Site> m_sites;
    int   m_target[kTiers] = {0, 0, 0, 0, 0};
    int   m_count[kTiers] = {0, 0, 0, 0, 0};
    float m_spacingCells[kTiers] = {0, 0, 0, 0, 0};
    float m_landKm2 = 0.0f;
    float m_bakeMs = 0.0f;
    std::vector<std::string> m_refusals;
};

}  // namespace Phyxel
