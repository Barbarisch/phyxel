#pragma once

// ============================================================================
// SettlementProgram — period-grounded settlement morphology presets
// (resources/settlement_program.json).
//
// The settlement-tier analog of RoomProgram: eras -> tiers (hamlet / village /
// town / city), each tier ONE data preset of the shared layout algorithm family
// (Forge rule 4: one algorithm, many presets — era/tier variation is DATA, not
// new C++). Units = cubes (1 m). Every value carries provenance in `sources`;
// the loader flags unsourced tiers. UNKNOWN era/tier resolves to nullptr — the
// caller must surface that as an error, never silently default (the era hook
// stays honest).
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Street-network preset for a tier. main_width == 0 => no formal street (cluster form).
struct StreetSpec {
    int         mainWidth = 0;   ///< main-street paved width (cubes)
    int         laneWidth = 0;   ///< secondary-lane paved width (cubes)
    std::string material = "Dirt";  ///< paving material (must exist in materials.json)
    bool        backLanes = false;  ///< burgage back lanes behind the plot rows (town+)
};

/// Plot (toft) sizing. Frontage is ALWAYS derived from the assigned typology's grounded
/// width ("from_typology" — the burgage principle); only depth + spacing are tier data.
struct PlotSpec {
    int depthMin = 10;  ///< toft depth back from the street (cubes)
    int depthMax = 14;
    int sideGap = 1;    ///< gap between adjacent plots along the street (0 = contiguous burgage row)
};

/// Yard between the street edge and the building front (also the side margins inside the plot).
struct SetbackSpec {
    int min = 1;
    int max = 2;
};

/// Public/communal features a tier provides (assets land in later phases).
struct PublicSpec {
    bool well = false;
    int  marketW = 0;  ///< market square dims; 0 = no square
    int  marketD = 0;
};

/// One settlement tier: a complete, grounded preset for the layout algorithm.
struct SettlementTierPreset {
    std::string era;         ///< owning era key (e.g. "medieval")
    std::string name;        ///< tier key (e.g. "village")
    std::string description;
    std::string morphology;  ///< "cluster" | "main_street" | "semi_organic"

    int buildingsMin = 3;
    int buildingsMax = 8;

    StreetSpec  street;
    PlotSpec    plot;
    SetbackSpec setback;

    /// typology -> weight for the deterministic per-plot draw. Keys must exist in
    /// room_program.json (referential integrity, gated by SettlementProgramTest).
    std::map<std::string, int> typologyWeights;

    /// City tier: the shop-heavy palette drawn INSIDE the core ring around the market square
    /// (trades cluster on the market place — the burgage-rent gradient). Empty = no ring split.
    std::map<std::string, int> coreTypologyWeights;
    int coreRing = 0;      ///< core-ring radius (cubes) around the square centre; 0 = unused
    int blocksMin = 18;    ///< secondary-street spacing range (cubes) — the jittered city blocks
    int blocksMax = 30;

    PublicSpec pub;

    std::map<std::string, std::string> sources;  ///< per-value provenance (grounding rule)

    bool hasSource(const std::string& key) const { return sources.find(key) != sources.end(); }
};

class SettlementProgramRegistry {
public:
    bool loadFromFile(const std::string& path);
    bool loadFromJson(const nlohmann::json& j);  // {"eras": {era: {"tiers": {tier: {...}}}}}

    /// Resolve (era, tier) -> preset, or nullptr if EITHER is unknown. The caller surfaces
    /// nullptr as an error listing known eras/tiers — never a silent default.
    const SettlementTierPreset* get(const std::string& era, const std::string& tier) const;

    std::vector<std::string> eras() const;
    std::vector<std::string> tiers(const std::string& era) const;
    size_t size() const { return m_tiers.size(); }
    void clear() { m_tiers.clear(); }

private:
    static SettlementTierPreset parse(const std::string& era, const std::string& tier,
                                      const nlohmann::json& rec);

    std::map<std::string, SettlementTierPreset> m_tiers;  ///< key = era + "/" + tier
};

} // namespace Core
} // namespace Phyxel
