#pragma once

// ============================================================================
// TraversalProbe — a character-sized box stepped through a voxel world in a
// SIMULATED (kinematic) fashion to test whether a layout is actually traversable.
// No physics engine, no input, no render loop — deterministic and unit-testable.
//
// The agent is an AABB (footprint half-width + standing height) with a max auto
// step-up, matching the engine character (AnimatedVoxelCharacter: half-width
// 0.25 m, ~1.75 m tall, step-up 4/9 m). It is the honest substitute for "walk the
// character up and see if it works": move the box, check it collides / has
// head-room / can step up, and answer "can it get from A to B?".
//
// Reusable for stair traversal, room-layout reachability, doorway passability, etc.
// Units: micro = 1/9 m. Footprint collision is sampled at the box centre + 4
// corners (a rough rectangle, per design) — adequate for >= subcube-scale geometry.
// ============================================================================

#include <functional>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

struct AgentBox {
    int halfWidthMicro = 2;    ///< ~0.25 m footprint half-extent (m_originalHalfWidth)
    int heightMicro    = 16;   ///< ~1.75 m standing height (clearance the box needs above its feet)
    int maxStepUpMicro = 4;    ///< ~0.44 m auto step-up (m_maxStepHeight = 4/9 m)
};

class TraversalProbe {
public:
    /// occupied(x,y,z) -> true if that micro cell is solid.
    TraversalProbe(std::function<bool(int, int, int)> occupied, AgentBox box)
        : m_occ(std::move(occupied)), m_box(box) {}

    /// The box with feet at (fx,fy,fz) overlaps no solid (collision + head-room).
    bool fits(int fx, int fy, int fz) const;
    /// Solid exists directly under the footprint (something to stand on).
    bool supported(int fx, int fy, int fz) const;
    /// Drop the feet from fy to the nearest supported, fitting level (>= minY).
    /// Returns the resting feet-y, or INT_MIN if the box would fall out of bounds.
    int  settle(int fx, int fy, int fz, int minY) const;

    /// Can the agent walk — 1-micro steps in +/-x and +/-z, auto step-up up to
    /// maxStepUpMicro, gravity settle — from `start` feet to ANY feet position inside
    /// [goalLo,goalHi], staying within [boundLo,boundHi]? Deterministic BFS.
    bool reachable(glm::ivec3 start, glm::ivec3 goalLo, glm::ivec3 goalHi,
                   glm::ivec3 boundLo, glm::ivec3 boundHi) const;

    /// Every feet-position the agent can walk to from `start` within [boundLo,boundHi] —
    /// the same stepping rule as reachable(), run to exhaustion. Returned in deterministic
    /// BFS order; empty if `start` doesn't settle.
    ///
    /// This exists so a FAILED reachability check can be LOCATED instead of merely
    /// reported: flood from both ends and the two sets' closest approach is the pinch.
    /// A bool answer tells you a town is broken; this tells you where.
    std::vector<glm::ivec3> flood(glm::ivec3 start, glm::ivec3 boundLo, glm::ivec3 boundHi) const;

private:
    /// Shared BFS core for reachable()/flood(). Walks the same step/step-up/settle rule.
    /// If `goalLo` is non-null the search STOPS on entering [goalLo,goalHi] and sets
    /// `*hitGoal`. If `out` is non-null every visited feet-position is appended.
    void bfs(glm::ivec3 start, glm::ivec3 boundLo, glm::ivec3 boundHi,
             const glm::ivec3* goalLo, const glm::ivec3* goalHi,
             bool* hitGoal, std::vector<glm::ivec3>* out) const;

    std::function<bool(int, int, int)> m_occ;
    AgentBox m_box;
};

}  // namespace Core
}  // namespace Phyxel
