#pragma once
// docs/VoxelFoundationPlan.md §6.7 "Rest" + D6 -- the rest / bake-in hybrid (F5.4).
//
// A body stays a body while it is near the player or recently moved. When the body budget is exceeded, bodies that
// have been asleep long enough and are outside the keep radius are baked back into the world grid, farthest first.
// Baking is a bijection body voxel -> world cell (conservation exact: material, part and damage carried):
//   rigid    -- the rotation is within rigidSnapDeg of one of the 24 axis rotations: the piece bakes with that rotation,
//               its shape exact, shifted by at most half a voxel per axis plus the snap;
//   resample -- otherwise each voxel takes the nearest free cell to its true centre (its own cell first, contested
//               cells to the nearest voxel; the rest take free cells within `reach` by a global greedy on distance).
// A voxel with nowhere to go refuses the whole body (it stays a body; counted). Prediction:
// docs/evidence/voxfound_f54_prediction.txt.
//
// Frames as BodySim: the sim's world grid, world voxel (i, j, k) spans [i, i + 1) * voxelM metres.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/BodySim.h"
#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

struct BakeParams {
    double rigidSnapDeg = 10.0;               // rotation within this of an axis rotation bakes rigid
    int reach = 2;                            // resample: free cells within this many voxels of the true centre
    // F5.5c: the other-body cell test as a neighbour cell set -- each cell near the plan tested once, scattered from
    // the near bodies' voxels (the same predicate; plans unchanged). false = F5.4's test per candidate (the A/B).
    bool cellSet = true;
};

struct BakeCell {
    glm::ivec3 cell{0};                       // world voxel
    uint32_t src = 0;                         // body grid index
    double shiftM = 0.0;                      // |cell centre - the voxel's true centre| at the body's pose
};

struct BakePlan {
    bool ok = false;
    bool rigid = false;
    std::string why;                          // refused: why
    std::vector<BakeCell> cells;              // one per body voxel, ascending src
    double maxShiftM = 0.0, meanShiftM = 0.0;
    double snapDeg = 0.0;                     // angle to the nearest axis rotation
};

/// The centre of body grid voxel p at the body's current pose (sim world metres).
glm::dvec3 bodyVoxelCentre(const SimBody& b, glm::ivec3 p);

/// Where body `self`'s voxels go in `world`. Cells must be air and not inside any other body (skip[i] != 0 excludes
/// body i from that test; null = none skipped).
BakePlan planBake(const std::vector<SimBody>& bodies, size_t self, const VoxelGrid& world,
                  const std::vector<uint8_t>* skip = nullptr, BakeParams p = {});

/// Writes the plan's cells (material, part, damage; fixed 0). Returns how many cells were solid before (must be 0).
size_t applyBake(VoxelGrid& world, const SimBody& b, const BakePlan& plan);

struct RestParams {
    size_t budget = 256;                      // bodies kept before baking starts (tier knob)
    double keepRadiusM = 16.0;                // never bake within this of the player (tier knob)
    int restTicks = 60;                       // asleep at least this long ("recently moved" otherwise)
    size_t cap = 0;                           // F5.5c: at most this many bakes per pass (0 = no cap; tier knob)
    BakeParams bake;
};

struct RestStats {
    size_t considered = 0;                    // bodies when the pass ran
    size_t over = 0;                          // bodies above the budget
    size_t baked = 0, rigid = 0, refused = 0;
    size_t keptNear = 0, keptAwake = 0, keptRecent = 0;  // over budget but not eligible, by reason
    size_t woken = 0;                         // bodies woken because a body under / beside them baked
    size_t overwritten = 0;                   // solid cells written over (must be 0)
    size_t voxels = 0;                        // voxels baked
    double maxShiftM = 0.0, sumShiftM = 0.0;
    double ms = 0.0;
    double chooseMs = 0.0, gridMs = 0.0, planMs = 0.0, applyMs = 0.0, wakeMs = 0.0, removeMs = 0.0;   // F5.5c: ms's parts
};

/// The eligible bodies in bake order (farthest from the player first, ties by index); counts the ineligible ones
/// that are over the budget by reason. Empty when bodies <= budget.
std::vector<size_t> chooseBakes(const std::vector<SimBody>& bodies, glm::dvec3 player, const RestParams& p,
                                RestStats* st);

/// One rest pass: choose, plan and apply in order until bodies <= budget (a refused body is skipped), wake the
/// bodies touching each baked one, remove the baked bodies from the sim. `world` must be the grid `sim` was built
/// on. Returns the baked bodies' indices before removal, in bake order; plans optional.
std::vector<size_t> restPass(BodySimRef& sim, VoxelGrid& world, glm::dvec3 player, const RestParams& p,
                             RestStats* st, std::vector<BakePlan>* plans = nullptr);

}  // namespace Phyxel::Core::VoxFound
