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
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {
namespace Water {

struct WaterLocality {
    static constexpr float kDrawMDefault = 48.0f;              ///< 27.10: recorded water is drawn within this (m); pinned
    static constexpr int kRippleWindowCellsDefault = 384;      ///< 25.29: 42.7 m at 1/9 m; pinned
    static constexpr float kDrawMMax = 256.0f;                 ///< water_av_record_draw's range: the record cache's reach
    static constexpr int kRippleWindowCellsMin = 9, kRippleWindowCellsMax = 512;   ///< one voxel; the renderer's atlas side

    /// L3 (41): a GPU pond that sleeps within memoryM of the viewer (horizontal, to its box) keeps its device buffers for its next
    /// wake, up to memoryCapMb in all. Cost only: a wake on kept memory makes the same volume, cell for cell (WaterKeptMemory).
    static constexpr float kMemoryMDefault = 48.0f;            ///< = kDrawMDefault: a pond you can see keeps its memory; pinned
    static constexpr float kMemoryCapMbDefault = 256.0f;       ///< ~3 of 41.1's 8 m pits or ~20 small ponds; pinned
    static constexpr float kMemoryMMax = 1024.0f;              ///< the occupancy box's span (kDirChunksX x 32 m)
    static constexpr float kMemoryCapMbMax = 4096.0f;          ///< a bound on what the water may hold back from the rest of the device

    /// L2 (43): ripple detail by distance. ON: the ripples' tilt is scaled by detail(d, rippleM - rippleFadeM, rippleM), d the
    /// horizontal distance from the viewer (a pure function of world position and the viewer); a pond beyond rippleM has no layer;
    /// a layer's window side is derived from rippleM (rippleWindowSide); slots go to the nearest waiting pond. OFF: exactly as
    /// before L2 - rippleWindowCells, no fade, first-come slots. ON by default since 2026-10-29 (the owner, on the fly-away clip:
    /// no visible difference; "Turn it on").
    static constexpr bool kRippleReachByDefault = true;
    static constexpr float kRippleMDefault = 21.0f;            ///< the default window's half: today's reach in a big pond; pinned
    static constexpr float kRippleFadeMDefault = 8.0f;         ///< full detail to 13 m; pinned
    /// rippleWindowSide(24) = 2 (216 + 40) = 512, the renderer's atlas side: beyond 24 m a point the fade shows could lie outside
    /// the simulated window (the invariant WaterLocalityTest walks), so rippleM is clamped here
    static constexpr float kRippleMMax = 24.0f;
    static constexpr float kRippleDropM = 4.0f;                ///< a layer is dropped beyond rippleM + this (made within rippleM); cost only
    static constexpr int kRippleMoveCells = 36;                ///< a window moves when its wanted origin is more than this off (4 m)
    /// the window's margin past the reach: kRippleMoveCells of hysteresis, 1 for the viewer's floor, 2 of absorbing rim
    /// (RippleLayer) and the facets' neighbour read (water_surface.frag) - the derivation is in 43.2, the walk in the unit test
    static constexpr int kRippleWindowMarginCells = 40;

    float drawM = kDrawMDefault;
    int rippleWindowCells = kRippleWindowCellsDefault;
    bool rippleReach = kRippleReachByDefault;
    float rippleM = kRippleMDefault;
    float rippleFadeM = kRippleFadeMDefault;
    float memoryM = kMemoryMDefault;
    float memoryCapMb = kMemoryCapMbDefault;
    bool testKeepMemoryOff = false;   ///< 41 TEST: free at sleep as before L3 (the control); never saved, echoed
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

    /// L2 (43.2): a ripple window's side (1/9 m lattice cells) with the reach on - every point within rippleM of the viewer is
    /// inside it after any move. `margin` is the test's handle (the red: without the hysteresis term).
    static int rippleWindowSide(float rippleM, int margin = kRippleWindowMarginCells) {
        const int reach = static_cast<int>(std::ceil(9.0f * std::clamp(rippleM, 0.0f, kRippleMMax)));
        return std::min(2 * (reach + margin), kRippleWindowCellsMax);
    }
    /// The window placement (one rule for the manager and the test): the origin a window of lnx x lnz cells over the box [minVoxel,
    /// maxVoxel] takes, given where it is (`cur`, `has` = it exists) and the viewer. Off (`reachRule` false, exactly as before L2):
    /// the wanted origin is centred on the viewer and clamped to the box, and the window moves when that is more than
    /// kRippleMoveCells off. With the reach on, the move compares the UNCLAMPED wanted origin: near a box edge the clamped one can
    /// sit kRippleMoveCells off the viewer's side, which uncovers the reach (found by the walk, 43.4).
    static glm::ivec2 rippleWindowOrigin(bool has, const glm::ivec2& cur, const glm::vec3& viewer, const glm::ivec3& minVoxel,
                                         const glm::ivec3& maxVoxel, int lnx, int lnz, bool reachRule) {
        const glm::ivec2 lo(minVoxel.x * 9, minVoxel.z * 9);
        const glm::ivec2 hi((maxVoxel.x + 1) * 9 - lnx, (maxVoxel.z + 1) * 9 - lnz);
        const glm::ivec2 raw(static_cast<int>(std::floor(viewer.x * 9.0f)) - lnx / 2, static_cast<int>(std::floor(viewer.z * 9.0f)) - lnz / 2);
        const glm::ivec2 want = glm::clamp(raw, lo, glm::max(lo, hi));
        if (!has) return want;
        const glm::ivec2 off = reachRule ? raw - cur : want - cur;
        if (std::abs(off.x) > kRippleMoveCells || std::abs(off.y) > kRippleMoveCells) return want;
        return cur;
    }
    /// L2 (43.2): which waiting ponds take the free ripple slots first - nearest (horizontal, to the box) first, ties by the box's
    /// position (lo x, z, y), never the list's order. Returns indices into `boxes` in that order.
    static std::vector<size_t> nearestFirst(const std::vector<std::pair<glm::ivec3, glm::ivec3>>& boxes, const glm::vec3& viewer) {
        std::vector<size_t> o(boxes.size());
        std::vector<float> d(boxes.size());
        for (size_t i = 0; i < boxes.size(); ++i) {
            o[i] = i;
            d[i] = horizontalDistanceToBox(viewer, glm::vec2(boxes[i].first.x, boxes[i].first.z), glm::vec2(boxes[i].second.x + 1, boxes[i].second.z + 1));
        }
        std::sort(o.begin(), o.end(), [&](size_t a, size_t b) {
            if (d[a] != d[b]) return d[a] < d[b];
            const glm::ivec3 &p = boxes[a].first, &q = boxes[b].first;
            if (p.x != q.x) return p.x < q.x;
            if (p.z != q.z) return p.z < q.z;
            return p.y < q.y;
        });
        return o;
    }

    /// L3 (41): one kept set, as the release rule sees it
    struct KeptBox { glm::ivec3 lo{0}, hi{0}; unsigned long long heldBytes = 0; };
    /// L3 (41): which kept sets to release (indices into `kept`, ascending): every one whose box is beyond memoryM, then, while the
    /// rest hold more than the cap, the farthest first. Equal distances are ordered by the box's own position (lo x, z, y), never by
    /// the list's order, so which pond keeps its memory never depends on the order ponds slept in.
    static std::vector<size_t> keptToRelease(const std::vector<KeptBox>& kept, const glm::vec3& viewer, float memoryM, float capMb) {
        std::vector<size_t> out, keep;
        std::vector<float> d(kept.size());
        for (size_t i = 0; i < kept.size(); ++i) {
            d[i] = horizontalDistanceToBox(viewer, glm::vec2(kept[i].lo.x, kept[i].lo.z), glm::vec2(kept[i].hi.x + 1, kept[i].hi.z + 1));
            if (memoryM <= 0.0f || d[i] > memoryM) out.push_back(i); else keep.push_back(i);
        }
        // nearest first; ties by position
        std::sort(keep.begin(), keep.end(), [&](size_t a, size_t b) {
            if (d[a] != d[b]) return d[a] < d[b];
            const glm::ivec3 &p = kept[a].lo, &q = kept[b].lo;
            if (p.x != q.x) return p.x < q.x;
            if (p.z != q.z) return p.z < q.z;
            return p.y < q.y;
        });
        const double cap = static_cast<double>(std::max(0.0f, capMb)) * 1024.0 * 1024.0;
        double held = 0.0;
        for (size_t i : keep) {
            if (held + static_cast<double>(kept[i].heldBytes) <= cap) held += static_cast<double>(kept[i].heldBytes);
            else out.push_back(i);   // over the cap: this one and (by the order) every farther one that does not fit
        }
        std::sort(out.begin(), out.end());
        return out;
    }
};

}  // namespace Water
}  // namespace Core
}  // namespace Phyxel
