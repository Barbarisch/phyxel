#pragma once

// ============================================================================
// FurniturePlacer — the algorithm that decides furniture placement (Structure
// Generation v2). The point: NOTHING about furniture is hand-authored. Given a
// room's purpose + size + the door/window openings on its walls, this derives:
//   - WHAT furniture the room gets (by purpose),
//   - which WALL each piece backs onto (or the room centre),
//   - the FACING so the piece's front points INTO the room,
//   - CLEARANCE: never on a door wall / in a doorway, never overlapping, on the
//     floor (no floating).
//
// Engine rotation convention (front direction at each rotation):
//   rot 0   -> front +z      rot 90  -> front -x
//   rot 180 -> front -z      rot 270 -> front +x
// So a piece against the MIN-X (left) wall faces +x  => rot 270 (NOT "east").
// ============================================================================

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/BuildingProgram.h"   // ProgStory, ProgRoom, ProgPortal, Rect

namespace Phyxel {
namespace Core {

struct FurniturePlacement {
    std::string type;        ///< fixture type (fireplace, bed, table, counter, chest, bench, ...)
    glm::ivec3  worldPos{0}; ///< template origin in world space, on the floor
    int         rotation = 0;///< 0/90/180/270 — front faces INTO the room
    std::string room;        ///< owning room id
};

/// A fixture's real footprint in CUBES (from the asset library's .metrics.json bounding box).
/// `width` = extent across the front (along the wall it backs onto); `depth` = front-to-back
/// (extends into the room). Default 1×1 keeps a piece a single cell (legacy / unknown asset).
struct Footprint {
    int width = 1;   ///< cubes along the wall
    int depth = 1;   ///< cubes into the room
};

/// The semantic identity of a placed fixture — what's needed to address it later in a session
/// ("rotate the bed", "move the 2nd bedroom's bed"). Carried into the PlacedObject's metadata.
struct FixtureLabel {
    std::string room;            ///< owning room id
    std::string purpose;         ///< that room's purpose (e.g. "bedchamber", "kitchen")
    int         purposeIndex = 0;///< 0-based ordinal among rooms of the SAME purpose (story order):
                                 ///< "2nd bedroom" == purposeIndex 1
    std::string type;            ///< fixture type (e.g. "bed")
};

class FurniturePlacer {
public:
    /// Furnish every room in `story`. `origin` = structure world origin (room rects
    /// are local to it); `floorY` = world Y of the walkable floor (pieces sit here).
    /// `footprints` (type -> real cube footprint, from the asset library) makes placement
    /// FOOTPRINT-AWARE: a piece reserves all the cells it covers; one that won't fit the room or
    /// would overlap another piece / a doorway is relocated to another wall or skipped. Omitted or
    /// missing types default to 1×1 (legacy single-cell behavior).
    static std::vector<FurniturePlacement> furnish(const ProgStory& story,
                                                   const glm::ivec3& origin, int floorY,
                                                   const std::map<std::string, Footprint>& footprints = {});

    /// Scatter small CLUTTER (mugs, bottles) ON a surface (a table top / shelf). `surface` = the
    /// surface footprint in WORLD cells; `topY` = world Y of the surface top (items sit here, not on
    /// the floor); `items` = clutter types to place. Each item gets a DISTINCT cell inside the surface
    /// (no overlap), chosen deterministically from `seed`. If there are more items than cells, only as
    /// many as fit are placed (no overflow). This is the surface-placement path furnish() lacks.
    static std::vector<FurniturePlacement> placeSurfaceClutter(
        const std::string& room, const Rect& surface, int topY,
        const std::vector<std::string>& items, unsigned seed);

    /// Rotation so a piece backed against a wall faces INTO the room, given the
    /// INWARD normal (pointing from the wall toward the room centre).
    static int facingIntoRoom(int inwardDx, int inwardDz);

    /// The furniture a room of this purpose REQUIRES, as fixture-type names (the recipe that
    /// furnish() places). Exposed so the asset-coverage validator can assert each required type
    /// resolves to a real template; uses the SAME purpose-matching as furnish().
    static std::vector<std::string> requiredFurniture(const std::string& purpose);

    /// Canonical purposes the recipe distinguishes. Iterate these to enumerate the FULL furniture
    /// vocabulary (every type the placer can ever emit) — the coverage gate's demand side.
    static std::vector<std::string> knownPurposes();

    /// Label each placement (as returned by furnish(story,...)) with its room's purpose and the
    /// room's ordinal among same-purpose rooms (story order) — the semantic identity used to address
    /// the fixture later. Returned in the SAME order as `placements`.
    static std::vector<FixtureLabel> labelFixtures(const ProgStory& story,
                                                   const std::vector<FurniturePlacement>& placements);

    /// The result of re-placing a single fixture within its room (a session edit). Cell coords are
    /// in the SAME space as the `room` rect passed in (pass a WORLD-space rect -> world cells).
    struct FurnitureEdit {
        bool        ok = false;
        int         x = 0, z = 0;   ///< new cell for the fixture
        int         rotation = 0;   ///< new facing — points INTO the room for wall ops
        std::string error;
    };

    /// Compute a wall-relative move for a fixture currently at (curX,curZ) in `room`, reusing the
    /// same wall-cell + facing-into-room math as furnish() so the moved piece stays usable (backed
    /// onto the wall, facing inward). `op`:
    ///   "wall:north"|"south"|"east"|"west"  -> seat on that wall, centered, facing in
    ///   "opposite_wall"                     -> seat on the wall opposite the one it's currently on
    ///   "center"                            -> room centre
    ///   "rotate"                            -> keep the cell, set rotation = rotationArg
    /// Returns ok=false + error on an unknown op or a degenerate room.
    static FurnitureEdit planEdit(const Rect& room, int curX, int curZ,
                                  const std::string& op, int rotationArg = 0);
};

} // namespace Core
} // namespace Phyxel
