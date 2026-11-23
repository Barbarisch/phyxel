#pragma once
// docs/VoxelFoundationPlan.md F6.2 -- liquid and powder living in the world (the CPU reference).
//
// The world's VoxelGrid holds the truth: grid.fill (one MatSim cell per open voxel, FULL 256 at 1/8 m). WorldFluid
// steps it with MatSim on the 1/8 m lattice (F6.1) inside a box around the fill: the fill's bounding box + a margin,
// clamped to the grid. A box face is a wall to MatSim, so before every tick, fill within `growWithin` voxels of a box
// face that is not the grid's face grows the box (the state written back, the box re-created around it with the tick
// carried over). The result does not depend on the box (prediction W2: equal to one box over the whole grid).
// Written back into grid.fill after every step() call.

#include <cmath>
#include <tuple>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include <memory>
#include <utility>
#include <vector>

#include "core/matsim/MatSimCore.h"
#include "core/matsim/MatSimParticles.h"
#include "core/voxfound/Bodies.h"
#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

class WorldFluid {
public:
    struct Params {
        int margin = 16;      // voxels around the fill's bounding box (two 8^3 blocks)
        int growWithin = 4;   // fill this close to a box face (not the grid's) grows the box
        bool grow = true;     // tests only: false = the box never grows (its faces act as walls; the red)
        MatSim::Config cfg;   // lattice is forced to 8
        /// F6.4: the hybrid -- each tick MatSim's lab frame over the box: cell water about to move is woken into PBF
        /// particles, stepped (1/60 s), settled back into cells, then the cells tick. The box also grows for particles.
        bool hybrid = false;
        MatSim::ParticleConfig particles;   // hybrid: 0 settleSpeed / wakeHeadCells take the lab's 0.35 m/s / 1 cell
        bool hybridStub = false;            // tests only: the flag set, the frame not run (the red)
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
    /// F6.3: many fills at once into EMPTY open voxels (one box re-made, the pressure state carried). All or nothing.
    bool addFill(const std::vector<std::pair<glm::ivec3, MatSim::Cell>>& cells, std::string* err = nullptr);

    /// Per-material units in the world: grid.fill outside the box + the box's own ledger (trapped contents included).
    MatSim::Ledger ledger() const;
    uint64_t tick() const { return tick_; }
    int grows() const { return grows_; }
    /// F6.4: particles in flight, cells woken into particles and particles settled back (since attach)
    size_t particles() const { return parts_ ? parts_->particles().size() : 0; }
    /// F8.2: the box's state written into grid.fill now (the ledger still counts it once: box, not grid, inside it)
    void flush() { writeBack(); }
    /// F8.2: the particles in flight as (grid voxel, material, units) -- for a settle that ends the fluid
    std::vector<std::tuple<glm::ivec3, uint8_t, uint32_t>> particleCells() const {
        std::vector<std::tuple<glm::ivec3, uint8_t, uint32_t>> out;
        if (parts_)
            for (const auto& q : parts_->particles())
                out.emplace_back(glm::ivec3(int(std::floor(q.x)), int(std::floor(q.y)), int(std::floor(q.z))), q.mat, q.units);
        return out;
    }
    uint64_t woken() const { return woken_; }
    uint64_t settled() const { return settled_; }
    /// F6.4: particles outside the box at the end of any tick (since attach; must stay 0)
    uint64_t particlesOutside() const { return outside_; }
    bool boxActive() const { return have_; }
    glm::ivec3 boxLo() const { return lo_; }
    glm::ivec3 boxSize() const { return size_; }

private:
    bool fillBounds(glm::ivec3& lo, glm::ivec3& hi) const;   // inclusive voxel bounds of grid.fill; false = none
    bool makeBox(bool keepOld = false);                     // from grid.fill; false = no fill (no box)
    void regrow(bool writeFirst = true);                    // a bigger box, the pressure state carried over
    void writeBack();
    bool nearOpenFace() const;
    bool particleBounds(glm::ivec3& lo, glm::ivec3& hi) const;   // F6.4: voxels the particles are in

    VoxelGrid* g_ = nullptr;
    Params p_;
    MatSim::MatSimCore core_;
    bool have_ = false;
    glm::ivec3 lo_{0}, size_{0};
    uint64_t tick_ = 0;
    int grows_ = 0;
    std::unique_ptr<MatSim::MatSimParticles> parts_;   // F6.4 (hybrid)
    uint64_t woken_ = 0, settled_ = 0, outside_ = 0;
};

/// F6.3: crumbs are powder -- every crumb's voxel in wf's grid (air after F5.1's extraction) gets one full cell of its material's
/// rubble (MatSim::rubbleOf). Refuses (nothing written) a crumb over a solid voxel, an unknown material, or a voxel
/// that already holds fill.
bool crumbsToPowder(const std::vector<Crumb>& crumbs, WorldFluid& wf, std::string* err = nullptr);

}  // namespace Phyxel::Core::VoxFound
