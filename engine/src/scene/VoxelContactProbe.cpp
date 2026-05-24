#include "scene/VoxelContactProbe.h"

#include "physics/VoxelDynamicsWorld.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Scene {

namespace {

constexpr float kNoGround = -1e8f;

inline float probeColumn(const Physics::VoxelDynamicsWorld& world,
                         float x, float topY, float halfW, float depth) {
    return world.findGroundY({x, topY, 0.0f /*z unused below*/}, halfW, depth);
}

inline float probeColumnXYZ(const Physics::VoxelDynamicsWorld& world,
                            float x, float y, float z,
                            float halfW, float depth) {
    return world.findGroundY({x, y, z}, halfW, depth);
}

// Snap a unit XZ direction to the closest cardinal axis (+X, -X, +Z, -Z).
// The probe operates on whole-cube neighbours, so cardinal alignment keeps
// the result stable as the character turns smoothly.
inline glm::vec3 snapCardinalXZ(const glm::vec3& v) {
    float ax = std::abs(v.x);
    float az = std::abs(v.z);
    if (ax >= az) return {v.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
    return {0.0f, 0.0f, v.z >= 0.0f ? 1.0f : -1.0f};
}

// Walk a forward column upward from a starting Y to find the top of the
// contiguous solid face the character is leaning into.
// Returns -1 if the start cell is not solid.
inline float scanFaceTop(const Physics::VoxelDynamicsWorld& world,
                         float x, float startY, float z, float halfW) {
    constexpr float kStep = 1.0f;
    constexpr float kProbeDepth = 0.25f;
    // First confirm the start cell is solid: a ground sample at startY+kStep
    // searching down kStep should return >= startY-kStep.
    float y = startY;
    for (int i = 0; i < 8; ++i) {
        float g = probeColumnXYZ(world, x, y + kStep, z, halfW, kProbeDepth);
        if (g <= kNoGround) return y;  // no solid at this level
        y = g + kStep;
    }
    return y;
}

} // anonymous namespace


CharacterContact sampleVoxelContact(const Physics::VoxelDynamicsWorld& world,
                                    const glm::vec3& feetPos,
                                    const glm::vec3& facingDirIn,
                                    const glm::vec3& halfExtents,
                                    const ContactProbeParams& params) {
    CharacterContact c;

    const float halfW = (params.lateralProbeXZ > 0.0f)
                      ? params.lateralProbeXZ
                      : std::max(halfExtents.x, 0.05f);
    const float halfH = std::max(halfExtents.y, 0.1f);

    // --- 1) Ground sample directly below feet (reuses existing path). ---
    float gY = world.findGroundY(feetPos, halfW, halfH * 2.0f + 1.0f);
    c.groundFound = (gY > kNoGround);
    c.groundY     = c.groundFound ? gY : feetPos.y;

    // --- 2) Lateral edge probes. Use cardinal-snapped forward axis so the
    //        four directions are stable as the character turns. ---
    glm::vec3 fwdRaw = facingDirIn;
    fwdRaw.y = 0.0f;
    float fl = std::sqrt(fwdRaw.x * fwdRaw.x + fwdRaw.z * fwdRaw.z);
    if (fl > 1e-4f) fwdRaw /= fl;
    else            fwdRaw = glm::vec3(0, 0, 1);
    const glm::vec3 fwd  = snapCardinalXZ(fwdRaw);
    const glm::vec3 right(-fwd.z, 0.0f, fwd.x);   // 90° CW (in left-handed XZ view)

    const float lateral = halfW + 0.05f;
    auto colDelta = [&](const glm::vec3& dir) -> float {
        glm::vec3 p = feetPos + dir * lateral;
        float g = world.findGroundY(p, halfW * 0.5f, halfH * 2.0f + 1.0f);
        if (g <= kNoGround) return 1e30f; // open air → very large drop
        return c.groundY - g;             // positive = drop, negative = step-up
    };

    float dF = colDelta(fwd);
    float dB = colDelta(-fwd);
    float dL = colDelta(-right);
    float dR = colDelta( right);

    // Pick the largest drop among the four cardinals.
    EdgeDir bestDir = EdgeDir::None;
    float   bestDrop = 0.0f;
    auto consider = [&](EdgeDir d, float drop) {
        if (drop > bestDrop) { bestDrop = drop; bestDir = d; }
    };
    consider(EdgeDir::Forward, dF);
    consider(EdgeDir::Back,    dB);
    consider(EdgeDir::Left,    dL);
    consider(EdgeDir::Right,   dR);

    // Threshold: only flag an edge if drop ≥ minLedgeDrop OR ≥ ~half cube.
    const float edgeFlagDrop = std::min(params.minLedgeDrop, 0.4f);
    if (bestDrop >= edgeFlagDrop) {
        c.nearEdgeDir = bestDir;
        c.edgeDrop    = (bestDrop > 1e20f) ? 1e6f : bestDrop;
    }

    // --- 3) Forward face: probe along the forward axis at chest height. ---
    // We do this as a sequence of column probes rather than a true ray, since
    // findGroundY is the only primitive we have. This is sufficient at cube
    // granularity.
    {
        const float chestY = c.groundY + halfH * 1.2f;  // ~1.1m for 0.95m halfH
        const float maxDist = std::max(params.forwardRange, lateral + 0.1f);
        const int   steps   = std::max(2, int(std::ceil(maxDist / 0.25f)));
        const float ds      = maxDist / float(steps);

        for (int i = 1; i <= steps; ++i) {
            glm::vec3 p = feetPos + fwd * (i * ds);
            // Find the top of any solid column at this point.
            float gAtChest = world.findGroundY({p.x, chestY + 0.5f, p.z},
                                               halfW * 0.5f, chestY + 0.5f - c.groundY);
            if (gAtChest > kNoGround && gAtChest > c.groundY + 0.1f) {
                // We hit a vertical face — record it.
                c.forwardHit       = true;
                c.forwardHitPoint  = glm::vec3(p.x, chestY, p.z);
                c.forwardFaceNormal = -fwd;             // outward toward the character
                c.forwardFaceYMin  = c.groundY;         // approximate floor of the face
                c.forwardFaceYMax  = gAtChest;
                c.climbAnchorXZ    = glm::vec3(p.x, 0.0f, p.z);

                // Headroom: scan upward from the face top for empty air.
                float top = gAtChest;
                float headTopProbe = world.findGroundY(
                    {p.x, top + params.headroomRequired + 0.5f, p.z},
                    halfW * 0.5f, params.headroomRequired + 0.5f);
                c.forwardHeadroomAbove =
                    (headTopProbe > kNoGround) ? (headTopProbe - top) : 1e6f;

                // Depth clear: sample one cube further along fwd at top+0.5.
                glm::vec3 behind = p + fwd * 1.0f;
                float behindFloor = world.findGroundY(
                    {behind.x, top + 1.0f, behind.z}, halfW * 0.5f, 1.5f);
                c.forwardDepthClear =
                    (behindFloor > kNoGround && std::abs(behindFloor - top) < 0.2f)
                        ? 1.0f
                        : 0.0f;
                break;
            }
        }
    }

    // --- 4) Climb classifier (cube-grain, runtime). ---
    if (c.forwardHit) {
        const float faceHeight = c.forwardFaceYMax - c.groundY;
        const bool  withinReach = faceHeight > 0.1f &&
                                  faceHeight <= params.maxClimbHeight;
        const bool  enoughHead  = c.forwardHeadroomAbove >= params.headroomRequired;
        const bool  enoughDepth = c.forwardDepthClear >= params.depthClearMin;
        if (withinReach && enoughHead && enoughDepth) {
            c.climb      = ClimbFeature::LedgeUp;
            c.climbGrabY = c.forwardFaceYMax;
            c.climbTopY  = c.forwardFaceYMax;
        }
    } else if (c.nearEdgeDir == EdgeDir::Forward &&
               c.edgeDrop >= params.minLedgeDrop &&
               c.edgeDrop < 1e5f) {
        c.climb     = ClimbFeature::LedgeDown;
        c.climbTopY = c.groundY;
        c.climbGrabY = c.groundY - c.edgeDrop;
        c.climbAnchorXZ = feetPos + fwd * (halfW + 0.05f);
    }

    // Ladder classification is deferred to Phase M7 (asset-driven).
    // PushKind classification is deferred to Phase M3.

    return c;
}

// ---------------------------------------------------------------------------
// Stringification helpers (used by the JSON route + tests).
// ---------------------------------------------------------------------------

const char* toString(EdgeDir d) {
    switch (d) {
        case EdgeDir::None:    return "none";
        case EdgeDir::Forward: return "forward";
        case EdgeDir::Back:    return "back";
        case EdgeDir::Left:    return "left";
        case EdgeDir::Right:   return "right";
    }
    return "none";
}

const char* toString(ClimbFeature c) {
    switch (c) {
        case ClimbFeature::None:      return "none";
        case ClimbFeature::LedgeUp:   return "ledge_up";
        case ClimbFeature::LedgeDown: return "ledge_down";
        case ClimbFeature::Ladder:    return "ladder";
    }
    return "none";
}

const char* toString(CharacterContact::PushKind k) {
    switch (k) {
        case CharacterContact::PushKind::None:          return "none";
        case CharacterContact::PushKind::DynamicObj:    return "dynamic_obj";
        case CharacterContact::PushKind::KinematicPart: return "kinematic_part";
    }
    return "none";
}

} // namespace Scene
} // namespace Phyxel
