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

struct RoomProgram;   // typology (croft/longhouse/hall_house/...) -> purposed rooms sized by bays

struct RoomLayout {
    std::vector<ProgRoom>   rooms;     ///< tiling partition of the footprint
    std::vector<ProgPortal> portals;   ///< interior doors (a spanning tree) + 1 exterior entrance
};

/// Partition a W×D (cubes) footprint into up to `targetRooms` rooms, each >= minDim per side,
/// connected so every room is reachable from the entrance. Deterministic in `seed`.
RoomLayout generateRoomLayout(int W, int D, int targetRooms, unsigned seed, int minDim = 2);

/// Partition a W×D footprint into the typology's rooms WITH THEIR PURPOSES (service/hall/solar/...),
/// sized proportional to each room's bay allocation, along the longer axis (medieval houses are
/// linear). Rooms span the full width; consecutive rooms get a connecting door; one exterior
/// entrance into an end room. Deterministic (no RNG — bay proportions are fixed). Returns an empty
/// layout if the footprint can't fit every room at >= minDim (caller falls back). This is what makes
/// a generated house a real house — a kitchen-end, a hall, a bedroom — not N identical "living" rooms.
RoomLayout generateRoomLayoutFromProgram(int W, int D, const RoomProgram& typology, int minDim = 2);

/// Fill in rooms for any story that has NO authored rooms. If `typology` is non-null and fits, the
/// GROUND story uses generateRoomLayoutFromProgram (purposed rooms); other empty stories (and the
/// no-typology / doesn't-fit case) fall back to generateRoomLayout. The exterior entrance is added to
/// the ground story only; authored rooms/stairs/portals are left untouched. Deterministic in `seed`.
void autofillRoomLayout(BuildingProgram& program, unsigned seed, const RoomProgram* typology = nullptr);

}  // namespace Core
}  // namespace Phyxel
