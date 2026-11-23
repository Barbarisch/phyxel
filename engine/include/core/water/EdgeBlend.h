#pragma once
// WaterCore.md 58.11 - THE EDGE BLEND (items 2 / 4 / 5): a live volume's edge against the record is two models of one water (the
// volume's physics, the record's stored level); no rule inside the volume makes them agree. The record's DRAWN surface near a live
// box is lifted toward the volume's drawn edge: w(d) x (edge top - the record's top), w = 1 at the box edge, 0 at M metres. Draw only:
// the record's stored water never moves. water_surface.vert's edgeBlendLift is this function line for line (WaterEdgeBlendTest pins
// the rules here).
//
// A profile: one live box (world metres, x/z), its four sides sampled every metre (kEdgeSamplesMax at most a side), each sample the
// volume's drawn top at its outermost wet column there (kEdgeDry = no water: no lift toward it).
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Phyxel { namespace Core { namespace Water {

constexpr int kEdgeBoxes = 8;
constexpr int kEdgeSamplesMax = 96;            ///< a side's samples (1 m apart) - a 96 m box side at most (a reach / patch is <= 72)
constexpr float kEdgeDry = -1.0e30f;
constexpr float kEdgeBlendMDefault = 3.0f;

struct EdgeProfile {
    glm::vec2 lo{0.0f}, hi{0.0f};              ///< the box, world m (x, z), hi exclusive
    // side 0: z = lo.z (samples along x), 1: z = hi.z (along x), 2: x = lo.x (along z), 3: x = hi.x (along z)
    float tops[4][kEdgeSamplesMax];
    int n[4] = {0, 0, 0, 0};
    EdgeProfile() { for (auto& s : tops) for (float& t : s) t = kEdgeDry; }
};

/// The volume's drawn top along side s at the arc coordinate a (metres from the side's start), linear between samples; kEdgeDry
/// where either sample is dry.
inline float edgeTopAt(const EdgeProfile& p, int s, float a) {
    const int n = p.n[s];
    if (n <= 0) return kEdgeDry;
    const float c = std::clamp(a - 0.5f, 0.0f, static_cast<float>(n - 1));
    const int i = std::min(static_cast<int>(c), n - 1), j = std::min(i + 1, n - 1);
    const float t0 = p.tops[s][i], t1 = p.tops[s][j];
    if (t0 <= kEdgeDry * 0.5f || t1 <= kEdgeDry * 0.5f) return t0 > kEdgeDry * 0.5f ? t0 : t1;
    return t0 + (t1 - t0) * (c - static_cast<float>(i));
}

/// The lift of a record vertex at world (x, y, z) (y = the record's own drawn top): toward the nearest live box side within M metres.
/// Inside a box: 0 (the volume draws there). Several boxes: the nearest one's.
inline float edgeBlendLift(const glm::vec3& v, const std::vector<EdgeProfile>& boxes, float M) {
    if (M <= 0.0f) return 0.0f;
    float best = M, lift = 0.0f;
    for (const EdgeProfile& p : boxes) {
        const bool inX = v.x >= p.lo.x && v.x <= p.hi.x, inZ = v.z >= p.lo.y && v.z <= p.hi.y;
        if (inX && inZ) return 0.0f;
        // the nearest point on the box's outline and the side it lies on
        const float cx = std::clamp(v.x, p.lo.x, p.hi.x), cz = std::clamp(v.z, p.lo.y, p.hi.y);
        const float d = std::hypot(v.x - cx, v.z - cz);
        if (d >= best) continue;
        int s; float a;
        if (inX) { s = v.z < p.lo.y ? 0 : 1; a = cx - p.lo.x; }
        else if (inZ) { s = v.x < p.lo.x ? 2 : 3; a = cz - p.lo.y; }
        else { s = v.z < p.lo.y ? 0 : 1; a = cx - p.lo.x; }   // a corner: the x side's end sample
        const float top = edgeTopAt(p, s, a);
        if (top <= kEdgeDry * 0.5f) continue;
        const float t = std::clamp(d / M, 0.0f, 1.0f), w = 1.0f - t * t * (3.0f - 2.0f * t);
        best = d; lift = w * (top - v.y);
    }
    return lift;
}

}}}  // namespace Phyxel::Core::Water
