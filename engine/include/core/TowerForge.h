#pragma once

// ============================================================================
// TowerForge — a mural tower that WORKS, not a drum-shaped pile of voxels.
//
// The first corner towers were solid masses with a decorative cap. This plans
// the real thing: a hollow shaft, a spiral stair you can actually climb, floors
// to arrive at, a door to enter by, arrow loops to shoot from, and a fighting
// deck on top.
//
// WHAT "CLIMBABLE" MEANS HERE, precisely: the engine's agent (AgentBox) steps up
// at most 4 micro and needs 16 micro of headroom. A full-cube step is 9 micro —
// unclimbable — so the treads are SUBCUBE plates rising 3 micro each. That is not
// a detail; it is the difference between a stair and a decorative ramp, and it is
// asserted rather than assumed (TowerForgeTest walks a TraversalProbe from the
// doorway to the top chamber).
//
// Everything is planned in coordinates RELATIVE to the tower's bounding box
// (cube x/z from its min corner) and to its base (micro y, 0 = the base cube's
// floor), so the planner is pure and the same plan can be stamped anywhere.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/BuildingProgram.h"   // Rect

namespace Phyxel {
namespace Core {

/// A vertical run of solid wall in one cube column, in micro-y. Openings (the door,
/// the arrow loops) are already subtracted, so a column may appear more than once.
struct TowerWallRun {
    int cx = 0, cz = 0;        ///< cube offset inside the tower's bounding box
    int fromMicroY = 0;        ///< inclusive
    int toMicroY = 0;          ///< exclusive
};

/// A thin horizontal plate filling one cube's footprint — a stair tread or a floor slab.
struct TowerPlate {
    int cx = 0, cz = 0;
    int yMicro = 0;            ///< the plate's bottom
    int thicknessMicro = 3;    ///< one subcube layer
    bool tread = false;        ///< true = stair tread, false = floor slab
};

struct TowerSpec {
    std::string shape = "round";     ///< "round" | "square"
    int heightCubes = 10;            ///< wall height above the base
    int storeyCubes = 3;             ///< floor-to-floor
    int wallThicknessCubes = 1;      ///< the rim
    bool arrowLoops = true;
    bool battlements = true;         ///< merlons on the rim top (the caller stamps these)
    char doorSide = 'S';             ///< which side the doorway faces: S|N|W|E
};

struct TowerPlan {
    bool ok = false;
    std::string refusal;
    std::vector<TowerWallRun> walls;
    std::vector<TowerPlate>   plates;      ///< floors + treads
    std::vector<glm::ivec3>   loopCells;   ///< arrow-loop cube cells (cx, cubeY, cz)
    glm::ivec3 doorFeetMicro{0};           ///< an agent's feet just inside the doorway
    glm::ivec3 topFeetMicro{0};            ///< the top chamber floor — the climb's goal
    int floorCount = 0;
};

/// Plan a working tower inside `bbox` (cube footprint). Refuses — with a reason — when the
/// footprint is too small to hold a wall, a stair and a room at once, rather than emitting
/// something that looks like a tower and cannot be used.
TowerPlan planTower(const Rect& bbox, const TowerSpec& spec);

} // namespace Core
} // namespace Phyxel
