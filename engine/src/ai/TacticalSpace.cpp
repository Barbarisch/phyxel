#include "ai/TacticalSpace.h"
#include "core/ChunkManager.h"

#include <algorithm>

namespace Phyxel {
namespace AI {

namespace {
inline glm::ivec3 voxelOf(const glm::vec3& p) {
    return glm::ivec3(static_cast<int>(std::floor(p.x)),
                      static_cast<int>(std::floor(p.y)),
                      static_cast<int>(std::floor(p.z)));
}
} // namespace

bool TacticalSpace::hasLineOfSight(ChunkManager& cm, const glm::vec3& from,
                                   const glm::vec3& to, float step) {
    glm::vec3 dir = to - from;
    const float dist = glm::length(dir);
    if (dist < 0.01f) return true;
    dir /= dist;

    const int steps = static_cast<int>(dist / std::max(0.05f, step));
    for (int i = 1; i < steps; ++i) {
        const glm::vec3 p = from + dir * (step * static_cast<float>(i));
        if (cm.hasVoxelAt(voxelOf(p))) return false;
    }
    return true;
}

float TacticalSpace::groundHeight(ChunkManager& cm, float x, float z,
                                  float fromY, float maxDrop) {
    for (float y = fromY; y > fromY - maxDrop; y -= 1.0f) {
        if (cm.hasVoxelAt(voxelOf(glm::vec3(x, y, z))))
            return std::floor(y) + 1.0f;   // stand on top of that voxel
    }
    return fromY;
}

bool TacticalSpace::isStandable(ChunkManager& cm, const glm::vec3& pos,
                                float bodyHeight) {
    // Ground under the feet...
    if (!cm.hasVoxelAt(voxelOf(pos - glm::vec3(0.0f, 0.5f, 0.0f)))) return false;
    // ...and room for the body.
    const int h = std::max(1, static_cast<int>(bodyHeight));
    for (int i = 0; i < h; ++i)
        if (cm.hasVoxelAt(voxelOf(pos + glm::vec3(0.0f, 0.5f + i, 0.0f)))) return false;
    return true;
}

TacticalSpace::CoverSpot TacticalSpace::findCover(ChunkManager& cm,
                                                  const glm::vec3& origin,
                                                  const glm::vec3& threat,
                                                  float searchRadius,
                                                  int rings, int samplesPerRing) {
    CoverSpot best;

    // Direction AWAY from the threat: candidates on that side are preferred,
    // because retreating behind an obstacle beats advancing past the enemy to
    // reach one.
    glm::vec3 away = origin - threat;
    away.y = 0.0f;
    const float threatDist = glm::length(away);
    if (threatDist > 1e-3f) away /= threatDist;
    else                    away = glm::vec3(1.0f, 0.0f, 0.0f);

    for (int r = 1; r <= rings; ++r) {
        const float radius = searchRadius * (static_cast<float>(r) / rings);
        for (int s = 0; s < samplesPerRing; ++s) {
            const float a = (2.0f * 3.14159265f * s) / samplesPerRing;
            glm::vec3 offset(std::cos(a) * radius, 0.0f, std::sin(a) * radius);

            glm::vec3 cand = origin + offset;
            cand.y = groundHeight(cm, cand.x, cand.z, origin.y + 4.0f);
            if (!isStandable(cm, cand)) continue;

            // The whole point: the threat must NOT be able to see this spot.
            // Pinned by TacticalSpaceTest.OpenGroundOffersNoCover — deleting this
            // line makes that test fail (mutation-checked 2026-08-31). Note the
            // "finds cover behind a wall" case does NOT catch it: with a wall
            // present the scoring picks a covered spot anyway, so the open-ground
            // control is the load-bearing test here.
            if (canSee(cm, cand, threat)) continue;

            // Score: prefer close (less running), prefer the far side of the
            // obstacle from the threat (dot with `away`), lightly prefer not
            // ending up further from the fight than necessary.
            const float travel = glm::length(offset);
            glm::vec3 dirOff = offset;
            const float ol = glm::length(dirOff);
            if (ol > 1e-3f) dirOff /= ol;
            const float sideBonus = glm::dot(dirOff, away);      // -1..1
            const float score = (searchRadius - travel) + sideBonus * 3.0f;

            if (!best.found || score > best.score) {
                best.found = true;
                best.position = cand;
                best.score = score;
            }
        }
    }
    return best;
}

} // namespace AI
} // namespace Phyxel
