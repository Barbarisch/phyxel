#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

namespace Phyxel {
class ChunkManager;
namespace AI {

/// Spatial reasoning for combat AI: what can see what, and where you can stand
/// so it cannot.
///
/// The line-of-sight walk used to be copied inline into whichever behavior
/// needed it (PatrolBehavior's perception wiring), which meant cover reasoning
/// had nowhere to live. Centralising it here gives every behavior — and any
/// game-registered BT action — the same notion of visibility, so "take cover"
/// means the same thing to a spearman, a wizard and a squad leader.
///
/// Everything is a pure query against the voxel world; no state, no ownership.
class TacticalSpace {
public:
    /// True when nothing solid blocks the segment. Steps the voxel grid at
    /// `step` units (0.5 = half a voxel: small enough not to tunnel through a
    /// one-voxel wall, large enough to stay cheap in a 400-body battle).
    static bool hasLineOfSight(ChunkManager& cm, const glm::vec3& from,
                               const glm::vec3& to, float step = 0.5f);

    /// Eye height used for sight tests — heads see, feet do not. A ray cast
    /// from the origin would be blocked by the ground itself on any slope.
    static constexpr float kEyeHeight = 1.6f;

    /// Convenience: LOS between two standing positions, at eye height.
    static bool canSee(ChunkManager& cm, const glm::vec3& fromFeet,
                       const glm::vec3& toFeet) {
        const glm::vec3 up(0.0f, kEyeHeight, 0.0f);
        return hasLineOfSight(cm, fromFeet + up, toFeet + up);
    }

    /// A cover spot: somewhere to stand that breaks line of sight to a threat.
    struct CoverSpot {
        glm::vec3 position{0.0f};
        float     score = 0.0f;   ///< higher is better (closer, more concealed)
        bool      found = false;
    };

    /// Search for a position near `origin` that hides the actor from `threat`.
    ///
    /// Samples a ring of candidates around the actor, keeps those that (a) are
    /// standable, (b) break LOS to the threat, and (c) do not require walking
    /// THROUGH the threat, then scores by travel distance so a fighter ducks
    /// behind the nearest rock rather than sprinting across the field.
    /// `preferAwayFrom` biases the search to the side away from the threat.
    static CoverSpot findCover(ChunkManager& cm, const glm::vec3& origin,
                               const glm::vec3& threat, float searchRadius = 12.0f,
                               int rings = 3, int samplesPerRing = 12);

    /// True when `pos` is a spot a character can stand: solid ground beneath,
    /// clear space for a body above it.
    static bool isStandable(ChunkManager& cm, const glm::vec3& pos,
                            float bodyHeight = 2.0f);

    /// The ground height at (x,z) by probing downward from `fromY`, or
    /// `fromY` when nothing solid is found within `maxDrop`.
    static float groundHeight(ChunkManager& cm, float x, float z,
                              float fromY, float maxDrop = 24.0f);
};

} // namespace AI
} // namespace Phyxel
