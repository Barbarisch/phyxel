#pragma once
// docs/VoxelFoundationPlan.md F7.13 (section 7a: thirds / ninths onto the 1/8 m grid) -- the 1/8 m voxels of an old-grid
// placement by CENTRE SAMPLING: a voxel belongs to the cube / subcube / microcube box its centre lies in (half-open
// boxes). Because the boxes of one level tile a cube, its 27 subcubes (or 729 microcubes) give each of the cube's 512
// voxels to exactly one of them -- a partition, nothing doubled or dropped. A lone subcube is 2 or 3 voxels per axis
// (1/3 m = 2.67 voxels), a microcube 0 or 1. Exact integer arithmetic in 1/144 m (centres at (2v + 1) * 9).
// Header-only (VoxFoundPlacementToVoxels tests).

#include <vector>

#include <glm/glm.hpp>

namespace Phyxel::Core::VoxFound {

/// level: 0 cube, 1 subcube (sub = 0..2 per axis), 2 microcube (micro = 0..2 inside the subcube).
inline void placementVoxels(glm::ivec3 cube, int level, glm::ivec3 sub, glm::ivec3 micro, std::vector<glm::ivec3>& out) {
    // box in 1/72 m units: a cube 72, a subcube 24, a microcube 8
    glm::ivec3 lo = cube * 72;
    int size = 72;
    if (level >= 1) {
        lo += sub * 24;
        size = 24;
    }
    if (level >= 2) {
        lo += micro * 8;
        size = 8;
    }
    // voxel v (1/8 m = 9 units) has its centre at 9v + 4.5 units, i.e. (18v + 9) half-units; inside iff
    // 2 lo <= 18 v + 9 < 2 (lo + size)  <=>  ceil((2 lo - 9) / 18) <= v <= floor((2 (lo + size) - 10) / 18)
    auto ceilDiv = [](int a, int b) { return a >= 0 ? (a + b - 1) / b : -((-a) / b); };
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    glm::ivec3 v0, v1;
    for (int k = 0; k < 3; ++k) {
        v0[k] = ceilDiv(2 * lo[k] - 9, 18);
        v1[k] = floorDiv(2 * (lo[k] + size) - 10, 18);
    }
    for (int x = v0.x; x <= v1.x; ++x)
        for (int y = v0.y; y <= v1.y; ++y)
            for (int z = v0.z; z <= v1.z; ++z) out.push_back({x, y, z});
}

}  // namespace Phyxel::Core::VoxFound
