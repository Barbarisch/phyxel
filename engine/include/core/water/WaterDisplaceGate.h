// WaterCore §44 (A10) step 2 - who shoves the water. A moving wet body either DISPLACES its volume (rasterized into the water: the
// heave, the thrown water, the bow wave) or only ripples (the ripple layer draws its rings; the push still floats it). The owner
// (2026-10-29): displacement "carefully gated ... controlled in how it is used". One setting at every tier (44.3 item 4: waves move
// other floaters, so a tier that changed who shoves would change outcomes).
//
// The rule, a pure function of the bodies this frame and each body's state last frame:
//  - a STROKE starts when a body moves faster than the on speed (relative to the water) and ends when it slows to the off speed
//    (the editor's hysteresis, kDisplaceOnSpeed / kDisplaceOffSpeed);
//  - at its start the stroke is decided once - shove, or ripple only - and kept to its end: a body never starts or stops shoving
//    mid-stroke (no water jumping out of or back into where it is);
//  - it may shove only if it is at least minHalfM in its largest half-extent (a 1/9 m block is one or two cells - its shove is
//    invisible and its ripples already draw it) and the budget has room;
//  - strokes starting in the same frame are admitted by score - the water they move per second, volume x speed relative to the
//    water - largest first, ties by id (never by the list's order); the bodies already shoving count against the budget first.
#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {
namespace Water {

struct WaterDisplaceGate {
    static constexpr float kMinHalfMDefault = 1.0f / 6.0f;   ///< a 1/3 m subcube and larger shove; pinned
    static constexpr int kBudgetDefault = 32;                 ///< bodies shoving at once; pinned
    static constexpr int kBudgetMax = 1024;                   ///< a shoving body must hold a push window (A2g) - the window cap
    static constexpr float kMinHalfMMax = 1.0f;
    float minHalfM = kMinHalfMDefault;   ///< m, the body's largest half-extent component; 0 = every size
    int budget = kBudgetDefault;         ///< 0 = none shove (as displacement off)

    enum State : uint8_t { Idle = 0, Shoving = 1, RippleOnly = 2 };   ///< RippleOnly: this stroke was refused (size or budget)
    struct Body { uint64_t id = 0; glm::vec3 halfExtents{0.0f}; float relSpeed = 0.0f; };
    struct Result {
        std::unordered_map<uint64_t, State> state;   ///< every body this frame (feed back as `prev` next frame)
        int kept = 0, admitted = 0, refusedSize = 0, refusedBudget = 0, rippleOnly = 0;
        bool shoving(uint64_t id) const { const auto it = state.find(id); return it != state.end() && it->second == Shoving; }
    };
    Result select(const std::vector<Body>& bodies, const std::unordered_map<uint64_t, State>& prev, float onSpeed, float offSpeed) const {
        Result r;
        std::vector<const Body*> entrants;
        int held = 0;
        for (const Body& b : bodies) {
            const auto it = prev.find(b.id);
            const State was = it != prev.end() ? it->second : Idle;
            if (was != Idle && b.relSpeed > offSpeed) {   // the stroke goes on as it was decided
                r.state[b.id] = was;
                if (was == Shoving) { ++r.kept; ++held; } else ++r.rippleOnly;
                continue;
            }
            r.state[b.id] = Idle;
            if (b.relSpeed <= onSpeed) continue;   // not moving fast enough to start a stroke
            const float half = std::max({b.halfExtents.x, b.halfExtents.y, b.halfExtents.z});
            if (half + 1e-5f < minHalfM) { r.state[b.id] = RippleOnly; ++r.refusedSize; continue; }   // 1e-5: 1/6 through float
            entrants.push_back(&b);
        }
        auto score = [](const Body& b) { return 8.0f * b.halfExtents.x * b.halfExtents.y * b.halfExtents.z * b.relSpeed; };
        std::sort(entrants.begin(), entrants.end(), [&](const Body* a, const Body* b) {
            const float sa = score(*a), sb = score(*b);
            if (sa != sb) return sa > sb;
            return a->id < b->id;
        });
        for (const Body* b : entrants) {
            if (held < budget) { r.state[b->id] = Shoving; ++r.admitted; ++held; }
            else { r.state[b->id] = RippleOnly; ++r.refusedBudget; }
        }
        return r;
    }
};

}  // namespace Water
}  // namespace Core
}  // namespace Phyxel
