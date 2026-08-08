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

// ---- M6 CIRCULATION GRAMMAR ------------------------------------------------
// How a room may be ENTERED, which is what makes a floor plan usable rather than
// merely connected. The rule a real building obeys: you reach a PRIVATE room from
// circulation or a public room — never by walking through someone else's bedroom.
//
// Per-PURPOSE engine data (the passRank/mountFor pattern); room_program.json may
// override per room with "access".
enum class AccessClass {
    Public,       ///< the shared rooms of the house: hall, taproom, salesroom, living
    Circulation,  ///< exists to be walked through: passage, landing, gallery, screens
    Private,      ///< a person's own room: bedchamber, solar, chamber
    Service,      ///< working/storage space: kitchen, byre, store, buttery, pantry
};

/// Access class for a free-text room purpose (substring-matched like the
/// furnishing recipes' canonicalPurpose). Unknown purposes read as Public — the
/// permissive default, so an unclassified room never fabricates a violation.
AccessClass accessClassFor(const std::string& purpose);
const char* accessClassName(AccessClass a);

/// One room in a typology, sized by the structural bays it occupies.
struct RoomSpec {
    std::string id;
    std::string purpose;
    double      bays = 1.0;
};

/// Window-opening generation rule for a typology (JSON key "windows"). ABSENT or
/// invalid => the typology generates NO windows: sizes/densities are grounded facts
/// that must be declared (with sources) in room_program.json — the engine invents
/// no defaults.
struct WindowSpec {
    int    width = 0;            ///< opening width in cubes
    int    height = 0;           ///< opening height in cubes
    double perBay = 0.0;         ///< windows per structural bay on each qualifying wall
    std::string walls = "long";  ///< "long" (walls parallel to the long axis) | "all"
    std::string infill = "shuttered";  ///< "shuttered" | "glass" — the GROUNDED default is
                                 ///< shuttered: "openings are SHUTTERED AIR, never Glass —
                                 ///< glazing unaffordable for ordinary households before
                                 ///< Elizabeth I (1558+)" (croft windows.size source). A
                                 ///< typology declares "glass" only with its own citation.
    bool valid() const { return width > 0 && height > 0 && perBay > 0.0; }
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
    std::string wealthTier = "humble";       ///< "humble" | "middling" | "high" — drives the
                                             ///< tier-filtered furnishing recipes (status, the
                                             ///< real medieval driver of household goods)

    std::string entrance;                    ///< exterior-door wall rule: "long_wall" (cross-passage
                                             ///< dwellings) | "gable" (street-frontage shops) |
                                             ///< "" = legacy perimeter-first placement
    std::vector<std::string> entranceBetween; ///< the two room ids the cross-passage sits between
                                             ///< (e.g. longhouse ["hall","byre"]); empty = room 0
    bool entranceOpposed = false;            ///< cross-passage opposed door pair (front + back walls)
    WindowSpec windows;                      ///< window rule; invalid (default) = no windows

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
