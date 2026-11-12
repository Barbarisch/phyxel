#pragma once
// docs/VoxelFoundationPlan.md §4 + F2 — the CPU REFERENCE mesher for the 1/8 m store.
//
// Turns voxels into the faces the renderer would draw, in three forms, so face cost can be measured before
// any GPU code exists, and so the GPU face builder has an exact reference to equal later:
//   * culled  — one unit quad per exposed voxel face (a face whose neighbour across it is air). Hidden
//               faces dropped, nothing merged: the cost of drawing voxels with no merging at all.
//   * greedy  — exposed faces of the same material and direction merged into maximal rectangles per slice,
//               across the WHOLE area (crossing block borders, §3.5a point 4: merging only inside a block
//               would multiply faces on large flat surfaces).
//   * greedy with blockLimit = kBlock — the same, but no quad crosses an 8-voxel block border (what a
//               per-block GPU pass would produce without the cross-block merge).
// Lighting is NOT part of the merge key yet: today's fine merge also splits on per-corner light, so the
// counts here are a LOWER bound on what a lit merge will produce. Stated wherever they are reported.
//
// Every quad lies in a plane perpendicular to `axis` (0 = x, 1 = y, 2 = z); `dir` = +1 faces the +axis side
// of its voxels. `o` is the min voxel of the quad's voxels; it spans `du` voxels along u = (axis+1)%3 and
// `dv` along v = (axis+2)%3.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/voxfound/VoxelGrid.h"

namespace Phyxel::Core::VoxFound {

struct Quad {
    glm::ivec3 o{0};
    uint8_t axis = 0;
    int8_t dir = 1;
    uint16_t du = 1, dv = 1;
    uint8_t material = 0;
};

/// What lies outside the grid. Air: the area is an object in empty space (every outer face is drawn).
/// Skip: the area is a window into a larger world, so faces on its outer boundary are not counted
/// (their neighbour is unknown; in a world they are usually hidden).
enum class Outside { Air, Skip };

std::vector<Quad> meshCulled(const VoxelGrid& g, Outside outside);
/// blockLimit 0 = merge across the whole grid; > 0 = no quad crosses a multiple of blockLimit.
std::vector<Quad> meshGreedy(const VoxelGrid& g, Outside outside, int blockLimit = 0);

/// Exactness check for a merged mesh: every exposed unit face (as meshCulled defines it) is covered by
/// exactly one quad of the same material, and no quad covers anything else. Returns the number of unit
/// faces in error (missing + doubled + extra + wrong material); `why` gets the first few.
size_t coverageErrors(const VoxelGrid& g, Outside outside, const std::vector<Quad>& quads,
                      std::string* why = nullptr);

}  // namespace Phyxel::Core::VoxFound
