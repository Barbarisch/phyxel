#pragma once

// ============================================================================
// StyleProfile — per-style construction knobs for Structure Generation v2.
//
// docs/structure-generation/StructureGenerationV2.md. This is where "wall/assembly thickness is
// configurable per style and material" lives: each style (timber_cottage,
// stone_manor, ...) declares assembly thicknesses (in cubes), per-layer
// materials, trim rules, roof params, ceiling targets, and a foundation
// strategy. Data-driven (resources/structure_styles.json, biomes.json pattern)
// so a new style is a JSON edit, not a code change.
//
// Storage mirrors DimensionCanon: a few first-class fields + flexible sub-maps.
// Loaded best-effort, CWD-relative.
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Construction profile for one architectural style.
struct StyleProfile {
    std::string name;
    std::string description;
    std::string source;                     ///< citation grounding this style's thicknesses/materials; "" = UNSOURCED
    std::string roofStyle  = "gable";       ///< gable | hip | flat
    std::string foundation = "slab";        ///< slab | crawlspace | basement

    std::map<std::string, double>      thickness; ///< assembly type -> thickness in cubes (exterior_wall, interior_wall, foundation_wall, floor, ceiling)
    std::map<std::string, std::string> materials; ///< layer -> material name (structure, cladding, trim, floor, roof, foundation)
    std::map<std::string, double>      trim;      ///< trim rule -> value in cubes (baseboard_height, casing_width, wainscot_height, mullion)
    std::map<std::string, bool>        flags;     ///< quoins, exposed_beams, ...
    std::map<std::string, double>      roof;      ///< pitch (degrees), overhang, ...
    std::map<std::string, double>      ceiling;   ///< target clear heights in cubes (humble, grand)
    std::map<std::string, std::string> sources;   ///< per-value provenance (the grounding rule)

    bool hasSource(const std::string& key) const { return sources.find(key) != sources.end(); }

    double thicknessOf(const std::string& type, double fallback = 0.333) const {
        auto it = thickness.find(type);
        return it == thickness.end() ? fallback : it->second;
    }
    std::string materialOf(const std::string& layer, const std::string& fallback = "Wood") const {
        auto it = materials.find(layer);
        return it == materials.end() ? fallback : it->second;
    }
    double trimOf(const std::string& rule, double fallback = 0.0) const {
        auto it = trim.find(rule);
        return it == trim.end() ? fallback : it->second;
    }
    double roofOf(const std::string& key, double fallback = 0.0) const {
        auto it = roof.find(key);
        return it == roof.end() ? fallback : it->second;
    }
    double ceilingOf(const std::string& key, double fallback = 2.5) const {
        auto it = ceiling.find(key);
        return it == ceiling.end() ? fallback : it->second;
    }
    bool flag(const std::string& key, bool fallback = false) const {
        auto it = flags.find(key);
        return it == flags.end() ? fallback : it->second;
    }
};

class StyleProfileRegistry {
public:
    /// Best-effort load from a JSON file (CWD-relative). Returns false and keeps
    /// existing entries if missing/malformed.
    bool loadFromFile(const std::string& path);
    /// Parse from an already-loaded JSON value. Accepts {"styles": {...}} or a
    /// flat map of style->record (keys starting with "_" are comments).
    bool loadFromJson(const nlohmann::json& j);

    const StyleProfile* get(const std::string& style) const {
        auto it = m_styles.find(style);
        return it == m_styles.end() ? nullptr : &it->second;
    }
    bool contains(const std::string& style) const {
        return m_styles.find(style) != m_styles.end();
    }
    std::vector<std::string> styles() const {
        std::vector<std::string> names;
        names.reserve(m_styles.size());
        for (const auto& [k, _] : m_styles) names.push_back(k);
        return names;
    }
    size_t size() const { return m_styles.size(); }
    void clear() { m_styles.clear(); }

private:
    static StyleProfile parseRecord(const std::string& name, const nlohmann::json& rec);
    static void parseNumberMap(const nlohmann::json& obj, std::map<std::string, double>& out);

    std::map<std::string, StyleProfile> m_styles;
};

} // namespace Core
} // namespace Phyxel
