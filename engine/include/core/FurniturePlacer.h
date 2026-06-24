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

class FurniturePlacer {
public:
    /// Furnish every room in `story`. `origin` = structure world origin (room rects
    /// are local to it); `floorY` = world Y of the walkable floor (pieces sit here).
    static std::vector<FurniturePlacement> furnish(const ProgStory& story,
                                                   const glm::ivec3& origin, int floorY);

    /// Rotation so a piece backed against a wall faces INTO the room, given the
    /// INWARD normal (pointing from the wall toward the room centre).
    static int facingIntoRoom(int inwardDx, int inwardDz);
};

} // namespace Core
} // namespace Phyxel
