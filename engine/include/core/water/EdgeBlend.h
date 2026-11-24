#pragma once
// WaterCore.md 58.11 - THE EDGE BLEND (items 2 / 4 / 5): a live volume's edge against the record is two models of one water (the
// volume's physics, the record's stored level); no rule inside the volume makes them agree. The record's DRAWN surface near water a
// live volume draws is lifted toward it: w(d) x (the volume's top there - the record's top), w = 1 at d = 0, 0 at M metres. Draw
// only: the record's stored water never moves. water_surface.vert's edgeBlendLift is this function line for line (WaterEdgeBlendTest
// pins the rules here).
//
// A profile (58.11 second cut - the first published four box SIDES and missed a patch's INNER boundary, where its awake water meets
// its own recorded tiles inside the box): one live volume's box as a 1 m grid, each cell the volume's drawn top there (the mean of
// the sub-columns it draws in that metre) or kEdgeDry where it draws nothing (dry, or a recorded tile the record draws).
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Phyxel { namespace Core { namespace Water {

constexpr int kEdgeBoxes = 8;
constexpr int kEdgeGridMax = 96;               ///< a box side at most (cells of 1 m) - a reach / patch / band is <= 72
constexpr float kEdgeDry = -1.0e30f;
constexpr float kEdgeBlendMDefault = 3.0f;
constexpr int kEdgeSearch = 4;                 ///< cells searched each way (covers M <= 4 m)

struct EdgeProfile {
    glm::ivec2 lo{0};                          ///< the box's first cell, world metres (x, z)
    int nx = 0, nz = 0;                        ///< cells (<= kEdgeGridMax)
    std::vector<float> tops;                   ///< nx x nz, index x + nx z; kEdgeDry = not drawn by the volume
    float at(int x, int z) const { return (x < 0 || z < 0 || x >= nx || z >= nz) ? kEdgeDry : tops[static_cast<size_t>(x + nx * z)]; }
};

/// The lift of a record vertex at world (x, y, z) (y = the record's own drawn top): toward the nearest cell a live volume draws,
/// within M metres (the distance to the cell's square, 0 inside it). Several volumes: the nearest cell of any.
inline float edgeBlendLift(const glm::vec3& v, const std::vector<EdgeProfile>& boxes, float M) {
    if (M <= 0.0f) return 0.0f;
    float best = M, lift = 0.0f;
    for (const EdgeProfile& p : boxes) {
        const float lx = v.x - static_cast<float>(p.lo.x), lz = v.z - static_cast<float>(p.lo.y);
        if (lx < -M || lz < -M || lx > static_cast<float>(p.nx) + M || lz > static_cast<float>(p.nz) + M) continue;
        const int cx = static_cast<int>(std::floor(lx)), cz = static_cast<int>(std::floor(lz));
        for (int dz = -kEdgeSearch; dz <= kEdgeSearch; ++dz) for (int dx = -kEdgeSearch; dx <= kEdgeSearch; ++dx) {
            const float top = p.at(cx + dx, cz + dz);
            if (top <= kEdgeDry * 0.5f) continue;
            const float qx = std::clamp(lx, static_cast<float>(cx + dx), static_cast<float>(cx + dx + 1));
            const float qz = std::clamp(lz, static_cast<float>(cz + dz), static_cast<float>(cz + dz + 1));
            const float d = std::hypot(lx - qx, lz - qz);
            if (d >= best) continue;
            const float t = std::clamp(d / M, 0.0f, 1.0f), w = 1.0f - t * t * (3.0f - 2.0f * t);
            best = d; lift = w * (top - v.y);
            if (d <= 0.0f) return lift;   // inside a drawn cell: the volume's own level
        }
    }
    return lift;
}

}}}  // namespace Phyxel::Core::Water
