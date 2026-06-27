#pragma once

// ============================================================================
// RoomProgram — period-grounded room/building sizing (resources/room_program.json).
//
// Structure Generation v2, the building-tier ruler for ROOMS (the analog of
// DimensionCanon for objects). MEDIEVAL frame: sizes derive from the BAY (the
// structural module) + house typology (croft / longhouse / hall_house /
// manor_hall), NOT from modern residential area tables. Units = cubes (1 m).
// Every value carries per-value provenance in `sources` (the grounding rule);
// the loader flags any program lacking citations.
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// One room in a typology, sized by the structural bays it occupies.
struct RoomSpec {
    std::string id;
    std::string purpose;
    double      bays = 1.0;
};

/// A period house typology and how it is sized.
struct RoomProgram {
    std::string name;
    std::string description;
    std::string source;                      ///< primary citation; "" = flagged UNSOURCED

    double widthMin = 0.0, widthMax = 0.0;   ///< building width range in cubes (frame/cruck span)
    double bayLength = 0.0;                  ///< structural bay length in cubes
    int    bays = 0;                         ///< total bay count
    double proportionMin = 0.0, proportionMax = 0.0;  ///< length:width sanity bounds (0 = unset)

    int    stories = 1;                      ///< number of stories the typology generates (>=1)
    std::string upperPurpose;                ///< room purpose for AUTO-generated upper floors
                                             ///< (e.g. "bedchamber" = inn guest chambers); ""=generic

    std::vector<RoomSpec> rooms;             ///< GROUND-floor rooms + their bay allocation
    std::map<std::string, std::string> sources;  ///< per-value provenance

    bool hasSource(const std::string& key) const { return sources.find(key) != sources.end(); }
};

class RoomProgramRegistry {
public:
    bool loadFromFile(const std::string& path);
    bool loadFromJson(const nlohmann::json& j);   // {"programs": {...}} or a flat map

    const RoomProgram* get(const std::string& name) const {
        auto it = m_programs.find(name);
        return it == m_programs.end() ? nullptr : &it->second;
    }
    bool contains(const std::string& name) const { return m_programs.count(name) > 0; }
    std::vector<std::string> programs() const {
        std::vector<std::string> names;
        for (const auto& [k, _] : m_programs) names.push_back(k);
        return names;
    }
    size_t size() const { return m_programs.size(); }
    void clear() { m_programs.clear(); }

    /// Coarse DEFAULT typology for a building function — a fallback heuristic used only when a
    /// BuildingProgram does NOT declare its own `typology`. It is NOT a sourced fact (status, not
    /// function, truly drives medieval typology); the brief-driven path (period+function+status)
    /// supersedes it. Returns "" for non-dwelling functions (skip the room gate).
    static std::string defaultTypologyForFunction(const std::string& function);

private:
    static RoomProgram parse(const std::string& name, const nlohmann::json& rec);

    std::map<std::string, RoomProgram> m_programs;
};

} // namespace Core
} // namespace Phyxel
