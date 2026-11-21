#pragma once
// docs/VoxelFoundationPlan.md F6.2 -- liquid and powder living in the world (the CPU reference).
//
// The world's VoxelGrid holds the truth: grid.fill (one MatSim cell per open voxel, FULL 256 at 1/8 m). WorldFluid
// steps it with MatSim on the 1/8 m lattice (F6.1) inside a box around the fill: the fill's bounding box + a margin,
// clamped to the grid. A box face is a wall to MatSim, so before every tick, fill within `growWithin` voxels of a box
// face that is not the grid's face grows the box (the state written back, the box re-created around it with the tick
// carried over). The result does not depend on the box (prediction W2: equal to one box over the whole grid).
// Written back into grid.fill after every step() call.

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "core/matsim/MatSimCore.h"
#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

class WorldFluid {
public:
    struct Params {
        int margin = 16;      // voxels around the fill's bounding box (two 8^3 blocks)
        int growWithin = 4;   // fill this close to a box face (not the grid's) grows the box
        bool grow = true;     // tests only: false = the box never grows (its faces act as walls; the red)
        MatSim::Config cfg;   // lattice is forced to 8
    };

    /// Validates grid.fill (empty is fine): its size, fill only in open voxels, units 1..FULL, a liquid or powder
    /// material. Refused = nothing attached.
    bool attach(VoxelGrid* grid, std::string* err = nullptr, Params p = {});
    bool attached() const { return g_ != nullptr; }

    /// n ticks, one at a time (the growth check before each); grid.fill is current when it returns.
    MatSim::StepStats step(int n);
    /// The grid's solid voxels changed (a wall broken or built): the box re-reads them (MatSim's resolidify).
    void wallsChanged();
    /// Puts fluid in an open voxel (units 0 = empties it). Refuses a solid voxel, units > FULL, a bad material.
    bool setFill(glm::ivec3 v, uint8_t material, uint32_t units, std::string* err = nullptr);

    /// Per-material units in the world: grid.fill outside the box + the box's own ledger (trapped contents included).
    MatSim::Ledger ledger() const;
    uint64_t tick() const { return tick_; }
    int grows() const { return grows_; }
    bool boxActive() const { return have_; }
    glm::ivec3 boxLo() const { return lo_; }
    glm::ivec3 boxSize() const { return size_; }

private:
    bool fillBounds(glm::ivec3& lo, glm::ivec3& hi) const;   // inclusive voxel bounds of grid.fill; false = none
    bool makeBox(bool keepOld = false);                     // from grid.fill; false = no fill (no box)
    void regrow();                                          // a bigger box, the pressure state carried over
    void writeBack();
    bool nearOpenFace() const;

    VoxelGrid* g_ = nullptr;
    Params p_;
    MatSim::MatSimCore core_;
    bool have_ = false;
    glm::ivec3 lo_{0}, size_{0};
    uint64_t tick_ = 0;
    int grows_ = 0;
};

}  // namespace Phyxel::Core::VoxFound
