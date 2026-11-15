#pragma once
// WaterCore 52 W4.1: the readers of water - a CPU rigid body's buoyancy (VoxelDynamicsWorld::setWaterQuery), the camera's underwater
// test (RenderCoordinator) - read a column's RUNS from the new water (WaterCoreManager::columnRuns: the awake volumes' fields, else the
// record's spans) by the wade rule (48.1, A2e): a point reads the highest run whose bottom is at or below it, and is in water only
// under that run's top - a dry cave under a pond reads dry, never the pond above it. Pure functions; the host supplies the column.
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel::Core::Water {

/// One column's water, highest run first (as WaterCoreManager::ColumnLevels); flow in m/s (x, z).
struct WaterRunsAt {
    int n = 0;
    float top[2] = {-1e30f, -1e30f};
    float bottom[2] = {-1e30f, -1e30f};
    glm::vec2 flow{0.0f};
};
/// (world voxel column x, z) -> false = no water there.
using ColumnRunsFn = std::function<bool(int, int, WaterRunsAt&)>;

struct WaterPointSample {
    bool inWater = false;
    float surfaceY = 0.0f;     ///< the top of the run the point is in
    float depthBelow = 0.0f;   ///< surfaceY - p.y (0 out of water)
    glm::vec2 flow{0.0f};
};

/// The water a point is in (its column by floor - 48.5 item 2: truncation reads the wrong column west / north of 0).
inline WaterPointSample sampleWaterAt(const ColumnRunsFn& column, const glm::vec3& p) {
    WaterPointSample s;
    WaterRunsAt r;
    if (!column || !column(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.z)), r)) return s;
    for (int k = 0; k < std::min(r.n, 2); ++k) {
        if (r.bottom[k] > p.y) continue;   // a run above the point (a pond over a cave)
        if (p.y > r.top[k]) return s;      // the point's own run is below it: in the air over the water
        s.inWater = true; s.surfaceY = r.top[k]; s.depthBelow = r.top[k] - p.y; s.flow = r.flow;
        return s;
    }
    return s;
}

/// The old rule kept (WaterManager::submergedFraction): the box's centre and four corners, each sampled just above the box floor
/// (+0.05, so the lookup lands in the water the box stands in, not the solid below); each wet point adds (surface - floor) / height
/// clamped to [0, 1]; the mean of the five.
inline float submergedFractionFromRuns(const ColumnRunsFn& column, const glm::vec3& mn, const glm::vec3& mx) {
    const float height = std::max(mx.y - mn.y, 1e-4f);
    const glm::vec2 c(0.5f * (mn.x + mx.x), 0.5f * (mn.z + mx.z));
    const glm::vec2 pts[5] = {c, {mn.x, mn.z}, {mx.x, mn.z}, {mn.x, mx.z}, {mx.x, mx.z}};
    float sum = 0.0f;
    for (const glm::vec2& q : pts) {
        const WaterPointSample s = sampleWaterAt(column, glm::vec3(q.x, mn.y + 0.05f, q.y));
        if (s.inWater) sum += std::clamp((s.surfaceY - mn.y) / height, 0.0f, 1.0f);
    }
    return sum / 5.0f;
}

/// The camera's depth under the surface of the water it is in (0 when it is not in water).
inline float cameraDepthFromRuns(const ColumnRunsFn& column, const glm::vec3& eye) {
    return sampleWaterAt(column, eye).depthBelow;
}

/// The L2 bake-vs-terrain rim-leak check (water_validate; moved from the deleted WaterManager::validateTable, 52 W4.3b). levelAt(x, z):
/// the bake's water level in that column (<= -1e29 dry); surfaceAt(x, z): its top solid voxel (INT_MIN unloaded). A RIM column is
/// baked-dry next to a baked-wet loaded neighbour (N4); it LEAKS when its surface is below floor(the neighbour's level).
struct TableValidation {
    int columns = 0, unloaded = 0, wet = 0, rim = 0, rimLeaks = 0;
    float worstLeakDepth = 0.0f;
    glm::ivec2 worstLeakAt{0, 0};
};
inline TableValidation validateWaterTable(const std::function<float(int, int)>& levelAt, const std::function<int(int, int)>& surfaceAt,
                                          const glm::ivec2& lo, const glm::ivec2& hi) {
    constexpr float kDry = -1e29f;
    TableValidation v;
    const int w = hi.x - lo.x + 1, d = hi.y - lo.y + 1;
    if (w <= 0 || d <= 0) return v;
    std::vector<float> level(static_cast<size_t>(w) * d);
    std::vector<int> surf(static_cast<size_t>(w) * d);
    auto at = [w](int ix, int iz) { return static_cast<size_t>(ix) + static_cast<size_t>(w) * iz; };
    for (int iz = 0; iz < d; ++iz) for (int ix = 0; ix < w; ++ix) {
        level[at(ix, iz)] = levelAt(lo.x + ix, lo.y + iz);
        surf[at(ix, iz)] = surfaceAt(lo.x + ix, lo.y + iz);
    }
    static const int NX[4] = {1, -1, 0, 0}, NZ[4] = {0, 0, 1, -1};
    for (int iz = 0; iz < d; ++iz) for (int ix = 0; ix < w; ++ix) {
        if (surf[at(ix, iz)] == INT_MIN) { ++v.unloaded; continue; }
        ++v.columns;
        if (level[at(ix, iz)] > kDry) { ++v.wet; continue; }
        float adj = kDry;
        for (int k = 0; k < 4; ++k) {
            const int nx = ix + NX[k], nz = iz + NZ[k];
            if (nx < 0 || nx >= w || nz < 0 || nz >= d || surf[at(nx, nz)] == INT_MIN) continue;
            adj = std::max(adj, level[at(nx, nz)]);
        }
        if (adj <= kDry) continue;
        ++v.rim;
        const float leak = std::floor(adj) - static_cast<float>(surf[at(ix, iz)]);
        if (leak > 0.0f) {
            ++v.rimLeaks;
            if (leak > v.worstLeakDepth) { v.worstLeakDepth = leak; v.worstLeakAt = glm::ivec2(lo.x + ix, lo.y + iz); }
        }
    }
    return v;
}

}  // namespace Phyxel::Core::Water
