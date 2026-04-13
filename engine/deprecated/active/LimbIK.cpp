#include "scene/active/LimbIK.h"
#include <btBulletDynamicsCommon.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Phyxel {
namespace Scene {

// ============================================================================
// Two-bone analytic IK (law of cosines)
// ============================================================================

TwoBoneIKResult solveTwoBoneIK(const glm::vec3& rootPos,
                                 float upperLen,
                                 float lowerLen,
                                 const glm::vec3& target,
                                 const glm::vec3& poleVec) {
    TwoBoneIKResult result;

    glm::vec3 toTarget = target - rootPos;
    float dist = glm::length(toTarget);
    float maxReach = upperLen + lowerLen;
    float minReach = std::abs(upperLen - lowerLen);

    // Clamp to reachable range
    if (dist > maxReach) {
        toTarget = glm::normalize(toTarget) * maxReach * 0.9999f;
        dist     = maxReach * 0.9999f;
        result.reachable = false;
    } else if (dist < minReach + 0.001f) {
        dist = minReach + 0.001f;
        result.reachable = false;
    }

    // Law of cosines: angle at root (upper bone)
    // cos(A) = (L1² + d² - L2²) / (2 * L1 * d)
    float cosA = (upperLen * upperLen + dist * dist - lowerLen * lowerLen)
                 / (2.0f * upperLen * dist);
    cosA = std::clamp(cosA, -1.0f, 1.0f);
    float angleA = std::acos(cosA);  // rotation at hip/shoulder

    // cos(B) = (L1² + L2² - d²) / (2 * L1 * L2)
    float cosB = (upperLen * upperLen + lowerLen * lowerLen - dist * dist)
                 / (2.0f * upperLen * lowerLen);
    cosB = std::clamp(cosB, -1.0f, 1.0f);
    float angleB = static_cast<float>(M_PI) - std::acos(cosB); // knee/elbow bend angle

    // Compute the chain plane using the pole vector
    glm::vec3 chainDir = (dist > 0.001f) ? (toTarget / dist) : glm::vec3(0, -1, 0);

    // Project pole vector out of chainDir to get the bend axis
    glm::vec3 pole = glm::normalize(poleVec);
    glm::vec3 perp = pole - glm::dot(pole, chainDir) * chainDir;
    float perpLen  = glm::length(perp);
    if (perpLen < 0.001f) {
        // Pole is parallel to chain — pick an arbitrary perpendicular
        glm::vec3 arbitrary = (std::abs(chainDir.y) < 0.9f)
                              ? glm::vec3(0, 1, 0)
                              : glm::vec3(1, 0, 0);
        perp = glm::normalize(glm::cross(chainDir, arbitrary));
    } else {
        perp = perp / perpLen;
    }

    // Bend axis = perpendicular to both chain and desired knee direction.
    // perp IS the direction the knee should go; the rotation axis that swings
    // chainDir toward that direction is cross(chainDir, perp).
    glm::vec3 bendAxis = glm::normalize(glm::cross(chainDir, perp));

    // Upper bone direction: rotate chainDir by angleA around bend axis
    glm::quat upperRot = glm::angleAxis(angleA, bendAxis);
    glm::vec3 upperDir = glm::normalize(glm::vec3(upperRot * glm::vec4(chainDir, 0.0f)));

    // The upper bone points from rootPos toward (rootPos + upperDir * upperLen)
    // Build a rotation that aligns local -Y (bone tip direction) with upperDir
    // Convention: bone's local -Y points from pivot toward child
    glm::vec3 boneAxis = glm::vec3(0, -1, 0);
    glm::vec3 rotAxis  = glm::cross(boneAxis, upperDir);
    float rotAxisLen   = glm::length(rotAxis);
    if (rotAxisLen > 0.001f) {
        float angle = std::acos(std::clamp(glm::dot(boneAxis, upperDir), -1.0f, 1.0f));
        result.upperRotation = glm::angleAxis(angle, rotAxis / rotAxisLen);
    } else if (glm::dot(boneAxis, upperDir) < 0) {
        result.upperRotation = glm::angleAxis(static_cast<float>(M_PI), glm::vec3(1, 0, 0));
    } else {
        result.upperRotation = glm::quat(1, 0, 0, 0);
    }

    // Lower bone: points from knee toward foot
    glm::vec3 kneePos  = rootPos + upperDir * upperLen;
    glm::vec3 lowerDir = glm::normalize(target - kneePos);

    rotAxis = glm::cross(boneAxis, lowerDir);
    rotAxisLen = glm::length(rotAxis);
    if (rotAxisLen > 0.001f) {
        float angle = std::acos(std::clamp(glm::dot(boneAxis, lowerDir), -1.0f, 1.0f));
        result.lowerRotation = glm::angleAxis(angle, rotAxis / rotAxisLen);
    } else if (glm::dot(boneAxis, lowerDir) < 0) {
        result.lowerRotation = glm::angleAxis(static_cast<float>(M_PI), glm::vec3(1, 0, 0));
    } else {
        result.lowerRotation = glm::quat(1, 0, 0, 0);
    }

    (void)angleB; // used implicitly through geometry
    return result;
}

// ============================================================================
// LimbStepper::initialize
// ============================================================================

void LimbStepper::initialize(const glm::vec3& bodyPos, float bodyYaw,
                               Physics::PhysicsWorld* physicsWorld) {
    glm::vec3 hitPos;
    if (castToGround(bodyPos, bodyYaw, physicsWorld, hitPos)) {
        footWorldPos   = hitPos;
        currentFootTarget = hitPos;
    } else {
        // Fallback: place foot below body at approximate rest
        glm::mat4 yawMat = glm::rotate(glm::mat4(1.0f), bodyYaw, glm::vec3(0, 1, 0));
        footWorldPos = bodyPos + glm::vec3(yawMat * glm::vec4(restOffset, 0.0f));
        footWorldPos.y = bodyPos.y - 1.0f;
        currentFootTarget = footWorldPos;
    }
    stepProgress = 1.0f;
    isStepping   = false;
}

// ============================================================================
// LimbStepper::tryStep
// ============================================================================

bool LimbStepper::tryStep(const glm::vec3& bodyPos, float bodyYaw,
                           bool otherLegStepping,
                           Physics::PhysicsWorld* physicsWorld) {
    if (isStepping) return false;    // Already mid-step
    if (otherLegStepping) return false; // Stagger: only one foot at a time

    // Check if the planted foot has drifted too far from its ideal rest position
    glm::mat4 yawMat = glm::rotate(glm::mat4(1.0f), bodyYaw, glm::vec3(0, 1, 0));
    glm::vec3 idealPos = bodyPos + glm::vec3(yawMat * glm::vec4(restOffset, 0.0f));
    idealPos.y = footWorldPos.y; // Only check XZ distance for trigger

    float dist = glm::length(glm::vec2(footWorldPos.x - idealPos.x,
                                        footWorldPos.z - idealPos.z));
    if (dist < stepThreshold) return false;

    // Raycast to find the new foot target
    glm::vec3 hitPos;
    if (!castToGround(bodyPos, bodyYaw, physicsWorld, hitPos)) return false;

    // Trigger step
    stepFrom     = footWorldPos;
    stepTo       = hitPos;
    stepProgress = 0.0f;
    isStepping   = true;
    return true;
}

// ============================================================================
// LimbStepper::updateStep
// ============================================================================

void LimbStepper::updateStep(float dt) {
    if (!isStepping) return;

    stepProgress += dt / stepDuration;
    if (stepProgress >= 1.0f) {
        stepProgress  = 1.0f;
        isStepping    = false;
        footWorldPos  = stepTo;
        currentFootTarget = stepTo;
        return;
    }

    // Arc interpolation: lerp XZ, add a sine-based height lift
    float t = stepProgress;
    glm::vec3 lerped = stepFrom + t * (stepTo - stepFrom);
    float arcY = std::sin(t * static_cast<float>(M_PI)) * stepHeight;
    currentFootTarget = glm::vec3(lerped.x, lerped.y + arcY, lerped.z);
}

// ============================================================================
// LimbStepper::solveIK
// ============================================================================

void LimbStepper::solveIK(const glm::vec3& hipWorldPos, const glm::vec3& poleWorldDir) {
    ikResult = solveTwoBoneIK(hipWorldPos, upperLength, lowerLength,
                               currentFootTarget, poleWorldDir);
}

// ============================================================================
// LimbStepper::castToGround
// ============================================================================

bool LimbStepper::castToGround(const glm::vec3& bodyPos, float bodyYaw,
                                 Physics::PhysicsWorld* physicsWorld,
                                 glm::vec3& hitPos) const {
    if (!physicsWorld) return false;

    glm::mat4 yawMat = glm::rotate(glm::mat4(1.0f), bodyYaw, glm::vec3(0, 1, 0));
    glm::vec3 idealXZ = bodyPos + glm::vec3(yawMat * glm::vec4(restOffset, 0.0f));

    btVector3 from(idealXZ.x, idealXZ.y + raycastOffset, idealXZ.z);
    btVector3 to  (idealXZ.x, idealXZ.y - 3.0f,          idealXZ.z);

    btCollisionWorld::AllHitsRayResultCallback cb(from, to);
    physicsWorld->getWorld()->rayTest(from, to, cb);

    if (cb.hasHit()) {
        // Find closest hit that is not the character's own capsule
        float bestFrac = 2.0f;
        int bestIdx = -1;
        for (int i = 0; i < cb.m_collisionObjects.size(); ++i) {
            if (ignoreBody_ && cb.m_collisionObjects[i] == ignoreBody_)
                continue;
            if (cb.m_hitFractions[i] < bestFrac) {
                bestFrac = cb.m_hitFractions[i];
                bestIdx = i;
            }
        }
        if (bestIdx >= 0) {
            hitPos = glm::vec3(cb.m_hitPointWorld[bestIdx].x(),
                               cb.m_hitPointWorld[bestIdx].y() + groundClearance,
                               cb.m_hitPointWorld[bestIdx].z());
            return true;
        }
    }
    return false;
}

} // namespace Scene
} // namespace Phyxel
