#pragma once

// ============================================================================
// BuildingProgram — the SEMANTIC, LLM-authorable building spec (level 3 of the
// Structure Generation v2 stack; docs/structure-generation/StructureGenerationV2.md).
//
// "What the building is": function, footprint, per-story rooms / portals /
// stairs / fixtures, substructure choice (slab | crawlspace | basement), roof,
// and a style id. The LLM authors ONLY this; everything physical (thickness,
// foundations, roofs, voxels) is derived deterministically into an AssemblyPlan
// and realized by the engine. The pre-build validator (BuildingProgramValidator)
// proves a program is legal before any voxel exists.
//
// Coordinate convention (matches v1 + StructureGenerator local space):
//   X = width, Y = up, Z = depth. rect = [x, z, w, d] (XZ footprint, min corner
//   + size). portal pos = [x, z] (min corner of the opening along its wall).
// ============================================================================

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// XZ footprint: min corner (x,z) + size (w,d). Serializes as [x,z,w,d].
struct Rect {
    int x = 0, z = 0, w = 0, d = 0;

    int x1() const { return x + w; }   ///< one past the max X
    int z1() const { return z + d; }   ///< one past the max Z

    static Rect fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const { return nlohmann::json::array({x, z, w, d}); }
};

struct ProgRoom {
    std::string id;
    Rect        rect;
    std::string purpose = "generic";
    std::string floorMat;

    static ProgRoom fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct ProgPortal {
    std::string a, b;            ///< room ids, or the literal "exterior"
    int         px = 0, pz = 0;  ///< min corner of the opening along its wall
    int         width = 2, height = 3;
    std::string kind = "door";   ///< door | arch | window
    bool        lockable = false;
    std::string key;             ///< required item id to unlock (door only)

    bool passable() const { return kind == "door" || kind == "arch"; }

    static ProgPortal fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct ProgStair {
    int         fromStory = 0, toStory = 1;
    Rect        rect;
    std::string kind = "straight";       ///< straight | spiral (legacy descriptor)
    std::string form = "switchback";     ///< stair GEOMETRY form: switchback | straight
                                         ///< (StairPlanner). Default folds to fit + clears headroom.

    static ProgStair fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct ProgFixture {
    std::string type;            ///< archetype / fixture type (chair_dining, table, bed, ...)
    Rect        rect;
    std::string facing = "south";
    std::string room;            ///< owning room id (optional)

    static ProgFixture fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct ProgStory {
    int                     height = 4;   ///< interior clear height in cubes
    std::vector<ProgRoom>   rooms;
    std::vector<ProgPortal> portals;
    std::vector<ProgStair>  stairs;
    std::vector<ProgFixture> fixtures;

    static ProgStory fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

struct BuildingProgram {
    std::string name;
    std::string style;                  ///< StyleProfile id (e.g. "timber_cottage")
    std::string function = "house";     ///< house | shop | church | tavern | tower | ...
    int         footprintW = 0;         ///< max bounding extent (NOT a fill target)
    int         footprintD = 0;
    std::string substructure = "slab";  ///< slab | crawlspace | basement
    std::string roofStyle;              ///< optional override; empty = use the style's roof
    std::string typology;               ///< RoomProgram id (croft|longhouse|hall_house|manor_hall); ""=skip room gate
    std::vector<ProgStory> stories;

    static BuildingProgram fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

} // namespace Core
} // namespace Phyxel
