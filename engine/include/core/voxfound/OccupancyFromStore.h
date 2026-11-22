#pragma once
// docs/VoxelFoundationPlan.md section 7 step 1 (queryAABB -> OccupiedBox), F7.8: the physics occupancy of a chunk
// answered from its BlockStore. The cells a query reaches follow VoxelOccupancyGrid::queryAABB's rule on 1/8 m voxels
// (floor of the AABB's corners, clamped to the chunk); a uniform solid 8^3 block is ONE 1 m box (the old cube), a mixed
// block emits its solid voxels as runs along z inside the block (at most 64 boxes per block). Every material != 0 is
// solid (WorldReads' rule). emit(lo, hi) takes the box's corners in metres. Header-only, no physics dependency.

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "core/voxfound/StorageLab.h"

namespace Phyxel::Core::VoxFound {

template <class Emit>
void storeBoxes(const BlockStore& s, glm::ivec3 originVoxel, glm::vec3 mn, glm::vec3 mx, Emit&& emit) {
    const glm::ivec3 dim = s.dim();
    constexpr float kVpm = 8.0f;
    const glm::vec3 o = glm::vec3(originVoxel);
    // broad reject: the AABB misses the chunk (touching counts, as the old grid)
    if (mx.x * kVpm < o.x || mn.x * kVpm > o.x + dim.x || mx.y * kVpm < o.y || mn.y * kVpm > o.y + dim.y ||
        mx.z * kVpm < o.z || mn.z * kVpm > o.z + dim.z)
        return;
    auto cl = [](int v, int hi) { return std::clamp(v, 0, hi); };
    const glm::ivec3 v0(cl(int(std::floor(mn.x * kVpm - o.x)), dim.x - 1), cl(int(std::floor(mn.y * kVpm - o.y)), dim.y - 1),
                        cl(int(std::floor(mn.z * kVpm - o.z)), dim.z - 1));
    const glm::ivec3 v1(cl(int(std::floor(mx.x * kVpm - o.x)), dim.x - 1), cl(int(std::floor(mx.y * kVpm - o.y)), dim.y - 1),
                        cl(int(std::floor(mx.z * kVpm - o.z)), dim.z - 1));
    const glm::ivec3 b0 = v0 / 8, b1 = v1 / 8;
    auto metres = [&](glm::ivec3 v) { return (o + glm::vec3(v)) / kVpm; };
    for (int bx = b0.x; bx <= b1.x; ++bx)
        for (int by = b0.y; by <= b1.y; ++by)
            for (int bz = b0.z; bz <= b1.z; ++bz) {
                const glm::ivec3 b(bx, by, bz);
                uint8_t m = 0;
                if (s.uniformBlock(b, m)) {
                    if (m) emit(metres(b * 8), metres(b * 8 + 8));
                    continue;
                }
                const glm::ivec3 lo = glm::max(v0, b * 8), hi = glm::min(v1, b * 8 + 7);
                for (int x = lo.x; x <= hi.x; ++x)
                    for (int y = lo.y; y <= hi.y; ++y) {
                        int z = lo.z;
                        while (z <= hi.z) {
                            if (!s.get({x, y, z})) {
                                ++z;
                                continue;
                            }
                            const int z0 = z;
                            while (z <= hi.z && s.get({x, y, z})) ++z;
                            emit(metres({x, y, z0}), metres({x + 1, y + 1, z}));
                        }
                    }
            }
}

}  // namespace Phyxel::Core::VoxFound
