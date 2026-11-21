#pragma once
// docs/VoxelFoundationPlan.md F6.1 -- MatSim's occupancy on the world's 1/8 m lattice, read from a VoxFound grid.
//
// A voxel with material != 0 is solid; a voxel outside the grid is UNKNOWN (MatSim refuses a box that contains
// unknown occupancy, so a box must lie inside the grid). World voxel w maps to grid voxel w - origin.
// The returned function holds a REFERENCE to the grid: the grid must outlive every use of it.

#include <glm/glm.hpp>

#include "core/matsim/MatSimCore.h"
#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

inline MatSim::MicroSolidFn matSimOccupancy(const VoxelGrid& g, glm::ivec3 origin = glm::ivec3(0)) {
    return [&g, origin](int x, int y, int z) {
        const glm::ivec3 p = glm::ivec3(x, y, z) - origin;
        if (!g.in(p)) return MatSim::Occ::Unknown;
        return g.mat[g.idx(p)] != 0 ? MatSim::Occ::Solid : MatSim::Occ::Empty;
    };
}

}  // namespace Phyxel::Core::VoxFound
