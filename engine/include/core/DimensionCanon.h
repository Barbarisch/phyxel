#pragma once

// ============================================================================
// DimensionCanon — the objective ruler for generated OBJECTS.
//
// Structure Generation v2 (docs/structure-generation/StructureGenerationV2.md), the component/asset
// tier. A sibling of resources/character_design_constraints.json (which anchors
// everything to the 1.751-cube character): per-archetype real-world canonical
// dimensions (converted to cubes; 1 cube ~= 1 m) + tolerance + structural rules
// + required anchors. The AssetValidator measures realized assets against these
// so a "picket fence" that comes out 1.6 m tall FAILS before anyone looks at it.
//
// Storage is intentionally flexible (every numeric field in `values`, every bool
// in `flags`) so new archetypes need only a JSON edit, not a code change.
// Loaded best-effort CWD-relative, like resources/biomes.json.
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Canonical dimensions + rules for one object archetype (e.g. "fence_picket").
struct ArchetypeDims {
    std::string name;
    std::string category;       ///< furniture | fence | gate | fixture | prop | opening
    std::string description;
    std::string source;         ///< citation grounding this archetype's dims; "" = UNSOURCED (flagged)
    double      tolerance = 0.1; ///< default dimensional tolerance in cubes (from "tol")

    std::map<std::string, double> values;  ///< every numeric field (height, seat_top, post_spacing, gate_min_w, ...)
    std::map<std::string, bool>   flags;   ///< boolean fields (solid, ...)
    std::vector<std::string>      anchors; ///< required anchor / interaction-point ids
    std::map<std::string, std::string> valueSources;  ///< per-value provenance (the grounding rule)

    bool hasValueSource(const std::string& key) const {
        return valueSources.find(key) != valueSources.end();
    }

    bool   has(const std::string& key) const { return values.find(key) != values.end(); }
    double value(const std::string& key, double fallback = 0.0) const {
        auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
    bool flag(const std::string& key, bool fallback = false) const {
        auto it = flags.find(key);
        return it == flags.end() ? fallback : it->second;
    }
    bool requiresAnchor(const std::string& id) const {
        for (const auto& a : anchors) if (a == id) return true;
        return false;
    }
};

class DimensionCanonRegistry {
public:
    /// Best-effort load from a JSON file (CWD-relative path, like biomes.json).
    /// Returns false (and leaves existing entries intact) if the file is missing
    /// or malformed.
    bool loadFromFile(const std::string& path);

    /// Parse from an already-loaded JSON value (used by tests; hermetic).
    /// Accepts either a top-level {"archetypes": {...}} object or a flat map of
    /// archetype->record (keys beginning with "_" are treated as comments).
    bool loadFromJson(const nlohmann::json& j);

    const ArchetypeDims* get(const std::string& archetype) const {
        auto it = m_archetypes.find(archetype);
        return it == m_archetypes.end() ? nullptr : &it->second;
    }
    bool contains(const std::string& archetype) const {
        return m_archetypes.find(archetype) != m_archetypes.end();
    }
    std::vector<std::string> archetypes() const {
        std::vector<std::string> names;
        names.reserve(m_archetypes.size());
        for (const auto& [k, _] : m_archetypes) names.push_back(k);
        return names;
    }
    size_t size() const { return m_archetypes.size(); }
    void clear() { m_archetypes.clear(); }

private:
    static ArchetypeDims parseRecord(const std::string& name, const nlohmann::json& rec);

    std::map<std::string, ArchetypeDims> m_archetypes;
};

} // namespace Core
} // namespace Phyxel
