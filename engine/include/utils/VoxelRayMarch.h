#pragma once

// Header-only 3D DDA (Amanatides & Woo) over unit cubes. Shared by the camera
// rig's wall collision (graphics) and click-to-move's ground pick (core) so the
// two never disagree about what "the first solid cube along a ray" means.

#include <glm/glm.hpp>
#include <cmath>
#include <functional>

namespace Phyxel {
namespace Utils {

struct VoxelRayHit {
    bool       hit = false;
    float      t = 0.0f;          ///< distance along the (unit) direction to the entry face
    glm::ivec3 cube{0};           ///< the solid cube
    int        enteredAxis = -1;  ///< 0/1/2 = x/y/z face crossed to enter it (-1 = started inside)
};

/// March from `origin` along unit `dir` up to `maxDist`; `solid(cube)` says whether a
/// cube is occupied. Returns the first solid cube (or hit=false).
inline VoxelRayHit marchVoxels(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                               const std::function<bool(const glm::ivec3&)>& solid) {
    VoxelRayHit out;
    if (!solid) return out;
    glm::ivec3 cell(static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)),
                    static_cast<int>(std::floor(origin.z)));
    glm::ivec3 step;
    glm::vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(dir[i]) < 1e-9f) { step[i] = 0; tMax[i] = 1e30f; tDelta[i] = 1e30f; continue; }
        step[i] = dir[i] > 0.0f ? 1 : -1;
        const float boundary = dir[i] > 0.0f ? static_cast<float>(cell[i] + 1) : static_cast<float>(cell[i]);
        tMax[i] = (boundary - origin[i]) / dir[i];
        tDelta[i] = 1.0f / std::abs(dir[i]);
    }
    float t = 0.0f;
    int enteredAxis = -1;
    for (int iter = 0; iter < 4096 && t <= maxDist; ++iter) {
        if (solid(cell)) {
            out.hit = true; out.t = t; out.cube = cell; out.enteredAxis = enteredAxis;
            return out;
        }
        int axis = 0;
        if (tMax.y < tMax.x) axis = (tMax.z < tMax.y) ? 2 : 1;
        else                 axis = (tMax.z < tMax.x) ? 2 : 0;
        t = tMax[axis];
        tMax[axis] += tDelta[axis];
        cell[axis] += step[axis];
        enteredAxis = axis;
    }
    return out;
}

} // namespace Utils
} // namespace Phyxel
