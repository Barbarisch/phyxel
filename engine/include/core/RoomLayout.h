#pragma once

// ============================================================================
// RoomLayout — generate_room_layout (placer #05). Auto-partitions a building footprint into
// rooms instead of hand-authoring them, so a town's buildings can have interiors at scale.
//
// BSP partition => rooms TILE the footprint (no gaps, no overlap), each >= a min usable size;
// a spanning tree of interior doors => every room reachable; one exterior entrance on the
// perimeter. Deterministic in `seed`. Pure — gated by BuildingHarness (floors L2 + rooms L3)
// and BuildingProgramValidator (no overlap, reachable, sizes).
// ============================================================================

#include <vector>

#include "core/BuildingProgram.h"

namespace Phyxel {
namespace Core {

struct RoomLayout {
    std::vector<ProgRoom>   rooms;     ///< tiling partition of the footprint
    std::vector<ProgPortal> portals;   ///< interior doors (a spanning tree) + 1 exterior entrance
};

/// Partition a W×D (cubes) footprint into up to `targetRooms` rooms, each >= minDim per side,
/// connected so every room is reachable from the entrance. Deterministic in `seed`.
RoomLayout generateRoomLayout(int W, int D, int targetRooms, unsigned seed, int minDim = 2);

/// Fill in rooms for any story that has NO authored rooms, using generateRoomLayout over the
/// program's footprint (the exterior entrance is added to the ground story only; authored rooms,
/// stairs, and portals are left untouched). Deterministic in `seed`. This is what the build handler
/// calls so a program need not hand-author interiors. No-op if the footprint is unset.
void autofillRoomLayout(BuildingProgram& program, unsigned seed);

}  // namespace Core
}  // namespace Phyxel
