// WaterCore §30.0b / §40 (L1) - one viewer distance for every water system. Locality to the viewer decides COST and DETAIL, never
// an outcome: what wakes water is a body touching it (A2), wherever the viewer is.
//
// v1 (40.1 item 1) keeps each existing distance's own shape, so nothing drawn changes when it lands: the record's draw tiles test
// the HORIZONTAL distance to a tile's nearest point (drawM, was RecordDraw::radiusM), and a creek's ripple window is a SQUARE of
// rippleWindowCells lattice cells centred on the viewer (was kColumnRippleWindow). A 3D or round reach would change what is drawn:
// a later change, shown to the owner. L3's memory reach and L4's wake pace join here when they are built.
#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {
namespace Water {

struct WaterLocality {
    static constexpr float kDrawMDefault = 48.0f;              ///< 27.10: recorded water is drawn within this (m); pinned
    static constexpr int kRippleWindowCellsDefault = 384;      ///< 25.29: 42.7 m at 1/9 m; pinned
    static constexpr float kDrawMMax = 256.0f;                 ///< water_av_record_draw's range: the record cache's reach
    static constexpr int kRippleWindowCellsMin = 9, kRippleWindowCellsMax = 512;   ///< one voxel; the renderer's atlas side

    float drawM = kDrawMDefault;
    int rippleWindowCells = kRippleWindowCellsDefault;
    /// 40.1 item 5 TEST: the water's viewer without moving the camera (the camera also moves the occupancy box, streaming and the
    /// capture). Never saved; echoed by water_locality.
    bool testViewerOn = false;
    glm::vec3 testViewer{0.0f};

    /// The horizontal distance from `viewer` to the nearest point of the world box [lo, hi] (x, z) - 0 inside. The conservative
    /// bound a whole body asks for (as bladesForDistance uses a chunk's nearest corner): cost may be bounded by it, never a look.
    static float horizontalDistanceToBox(const glm::vec3& viewer, const glm::vec2& lo, const glm::vec2& hi) {
        return std::sqrt(horizontalDistanceSqToBox(viewer, lo, hi));
    }
    /// the same, squared - the record's draw test compares it with drawM^2 (bit for bit the arithmetic it used before L1)
    static float horizontalDistanceSqToBox(const glm::vec3& viewer, const glm::vec2& lo, const glm::vec2& hi) {
        const float dx = std::clamp(viewer.x, lo.x, hi.x) - viewer.x, dz = std::clamp(viewer.z, lo.y, hi.y) - viewer.z;
        return dx * dx + dz * dz;
    }
    /// The horizontal distance to a world position - what a per-column look asks for (a pure function of world position).
    static float horizontalDistance(const glm::vec3& viewer, const glm::vec2& p) {
        const float dx = p.x - viewer.x, dz = p.y - viewer.z;
        return std::sqrt(dx * dx + dz * dz);
    }
    /// 1 at or under `nearM`, 0 at or over `farM`, a smoothstep between (no step anywhere: a fade, not a band edge). far <= near
    /// is a hard reach at near.
    static float detail(float d, float nearM, float farM) {
        if (d <= nearM) return 1.0f;
        if (d >= farM) return 0.0f;
        const float t = (d - nearM) / (farM - nearM);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }
};

}  // namespace Water
}  // namespace Core
}  // namespace Phyxel
