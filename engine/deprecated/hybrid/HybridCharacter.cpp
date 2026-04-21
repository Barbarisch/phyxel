#include "scene/HybridCharacter.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

namespace Phyxel {
namespace Scene {

// ============================================================================
// Construction
// ============================================================================

HybridCharacter::HybridCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position)
    : AnimatedVoxelCharacter(physicsWorld, position) {
}

// ============================================================================
// IK Initialization (call after loadModel)
// ============================================================================

void HybridCharacter::initIK() {
    detectLimbChains();

    // Cache key bone IDs
    const auto& skel = getSkeleton();
    auto findBone = [&](const std::string& suffix) -> int {
        for (const auto& bone : skel.bones) {
            if (bone.name.find(suffix) != std::string::npos) return bone.id;
        }
        return -1;
    };
    hipsBoneId_  = findBone("Hips");
    spineBoneId_ = findBone("Spine");
    chestBoneId_ = findBone("Spine2");
    if (chestBoneId_ < 0) chestBoneId_ = findBone("Spine1");
    neckBoneId_  = findBone("Neck");
    headBoneId_  = findBone("Head");
}

// ============================================================================
// IK Injection (called by AnimatedVoxelCharacter::update after global transforms)
// ============================================================================

void HybridCharacter::applyIKCorrections(float deltaTime) {
    applyFootIK();
    applyLean(deltaTime);
    applyLookAt(deltaTime);
    applyHandIK();

    // Re-compute global transforms after IK modifications
    getAnimSystemMut().updateGlobalTransforms(getSkeletonMut());
}

// ============================================================================
// Limb Chain Detection
// ============================================================================

void HybridCharacter::detectLimbChains() {
    legChains_.clear();
    armChains_.clear();

    const auto& skel = getSkeleton();
    auto findBoneId = [&](const std::string& name) -> int {
        auto it = skel.boneMap.find(name);
        return (it != skel.boneMap.end()) ? it->second : -1;
    };

    // Leg chains: UpLeg -> Leg -> Foot
    for (const std::string& side : {"Left", "Right"}) {
        std::string prefix = "mixamorig:" + side;
        int hip  = findBoneId(prefix + "UpLeg");
        int knee = findBoneId(prefix + "Leg");
        int foot = findBoneId(prefix + "Foot");

        if (hip >= 0 && knee >= 0 && foot >= 0) {
            LimbChain chain;
            chain.upperBoneId = hip;
            chain.midBoneId   = knee;
            chain.endBoneId   = foot;
            chain.upperLen    = computeBoneLength(hip, knee);
            chain.lowerLen    = computeBoneLength(knee, foot);
            legChains_.push_back(chain);
            footIKStates_.push_back(FootIKState{});

            LOG_INFO("HybridCharacter", "Detected {} leg chain: upper={:.3f} lower={:.3f}",
                     side, chain.upperLen, chain.lowerLen);
        }
    }

    // Arm chains: Arm -> ForeArm -> Hand
    for (const std::string& side : {"Left", "Right"}) {
        std::string prefix = "mixamorig:" + side;
        int shoulder = findBoneId(prefix + "Arm");
        int elbow    = findBoneId(prefix + "ForeArm");
        int hand     = findBoneId(prefix + "Hand");

        if (shoulder >= 0 && elbow >= 0 && hand >= 0) {
            LimbChain chain;
            chain.upperBoneId = shoulder;
            chain.midBoneId   = elbow;
            chain.endBoneId   = hand;
            chain.upperLen    = computeBoneLength(shoulder, elbow);
            chain.lowerLen    = computeBoneLength(elbow, hand);
            armChains_.push_back(chain);

            LOG_INFO("HybridCharacter", "Detected {} arm chain: upper={:.3f} lower={:.3f}",
                     side, chain.upperLen, chain.lowerLen);
        }
    }
}

float HybridCharacter::computeBoneLength(int parentId, int childId) const {
    const auto& skel = getSkeleton();
    if (parentId < 0 || childId < 0 ||
        parentId >= static_cast<int>(skel.bones.size()) ||
        childId >= static_cast<int>(skel.bones.size())) {
        return 0.4f;
    }
    float len = glm::length(skel.bones[childId].localPosition);
    return (len > 0.01f) ? len : 0.4f;
}

// ============================================================================
// Skeleton-aware Two-Bone IK — v3 (robust, clamped, normalized)
//
// Works in MODEL SPACE to avoid world/local conversion issues.
// All quaternions are normalized to prevent scale contamination.
// Delta rotations are angle-clamped to prevent catastrophic overshoots.
// ============================================================================

/// Maximum angle (radians) any single IK delta can rotate a bone.
static constexpr float kMaxIKDeltaRad = glm::radians(45.0f);

/// Maximum distance (meters) between keyframed foot and IK target before IK is skipped.
static constexpr float kMaxFootDisplacement = 0.5f;

/// Get the MODEL-SPACE position of a bone (translation column of globalTransform).
static glm::vec3 getBoneModelPos(const Bone& bone) {
    return glm::vec3(bone.globalTransform[3]);
}

/// Get the WORLD-SPACE position of a bone given model matrix.
static glm::vec3 getBoneWorldPos(const Bone& bone, const glm::mat4& modelMatrix) {
    return glm::vec3(modelMatrix * glm::vec4(getBoneModelPos(bone), 1.0f));
}

/// Get the accumulated model-space rotation for a bone (normalized, scale-safe).
static glm::quat getBoneModelRot(const Bone& bone) {
    return glm::normalize(glm::quat_cast(bone.globalTransform));
}

/// Get the model-space rotation of a bone's parent (identity for root bones).
static glm::quat getParentModelRot(const Skeleton& skel, int boneId) {
    int pid = skel.bones[boneId].parentId;
    if (pid < 0) return glm::quat(1, 0, 0, 0);
    return getBoneModelRot(skel.bones[pid]);
}

/// Compute a unit quaternion rotating direction `from` to direction `to`.
/// Both inputs must be unit vectors. Returns identity if parallel.
static glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to) {
    float d = glm::dot(from, to);
    if (d > 0.9999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.9999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), from);
        if (glm::length2(axis) < 1e-6f)
            axis = glm::cross(glm::vec3(0, 1, 0), from);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    glm::vec3 axis = glm::cross(from, to);
    float s = std::sqrt((1.0f + d) * 2.0f);
    return glm::normalize(glm::quat(s * 0.5f, axis / s));
}

/// Clamp a quaternion's angular magnitude. If the rotation angle exceeds maxRad,
/// reduce it to maxRad while preserving the rotation axis.
static glm::quat clampQuatAngle(const glm::quat& q, float maxRad) {
    float angle = 2.0f * std::acos(glm::clamp(q.w, -1.0f, 1.0f));
    if (angle <= maxRad) return q;
    if (angle < 1e-6f) return glm::quat(1, 0, 0, 0);
    // Reduce to maxRad while keeping the axis
    float halfMax = maxRad * 0.5f;
    float sinHalf = std::sin(halfMax);
    float sinOrig = std::sin(angle * 0.5f);
    if (sinOrig < 1e-6f) return glm::quat(1, 0, 0, 0);
    float scale = sinHalf / sinOrig;
    return glm::normalize(glm::quat(std::cos(halfMax), q.x * scale, q.y * scale, q.z * scale));
}

/// Solve two-bone IK for a chain (upper, mid, end).
/// Works entirely in MODEL SPACE. Modifies upper and mid bone currentRotation.
///
/// All internal rotations are normalized. Delta rotations are angle-clamped
/// to prevent catastrophic overshoots.
static void solveTwoBoneIK_v3(
    Skeleton& skel,
    const LimbChain& chain,
    const glm::vec3& targetModel,   // target position in MODEL space
    const glm::vec3& poleModel,     // pole target in MODEL space
    float weight,
    float maxDeltaRad = kMaxIKDeltaRad)
{
    if (weight < 0.001f) return;

    Bone& upperBone = skel.bones[chain.upperBoneId];
    Bone& midBone   = skel.bones[chain.midBoneId];
    const Bone& endBone = skel.bones[chain.endBoneId];

    // 1. Current joint positions in model space
    glm::vec3 rootPos = getBoneModelPos(upperBone);
    glm::vec3 midPos  = getBoneModelPos(midBone);
    glm::vec3 endPos  = getBoneModelPos(endBone);

    float upperLen = glm::distance(rootPos, midPos);
    float lowerLen = glm::distance(midPos, endPos);
    if (upperLen < 0.001f || lowerLen < 0.001f) return;

    float totalLen = upperLen + lowerLen;
    glm::vec3 rootToTarget = targetModel - rootPos;
    float targetDist = glm::length(rootToTarget);
    if (targetDist < 0.001f) return;

    // Clamp reach (prevent full extension which is unstable)
    targetDist = glm::min(targetDist, totalLen * 0.999f);
    // Also ensure minimum reach
    targetDist = glm::max(targetDist, std::abs(upperLen - lowerLen) * 1.001f);

    // 2. Law of cosines → angle at root
    float cosRoot = (upperLen * upperLen + targetDist * targetDist - lowerLen * lowerLen)
                    / (2.0f * upperLen * targetDist);
    cosRoot = glm::clamp(cosRoot, -1.0f, 1.0f);
    float angleRoot = std::acos(cosRoot);

    // 3. Build target direction and bend plane
    glm::vec3 chainDir = rootToTarget / targetDist;

    // Pole vector projected onto plane perpendicular to chain
    glm::vec3 poleDelta = poleModel - rootPos;
    glm::vec3 poleOnPlane = poleDelta - chainDir * glm::dot(poleDelta, chainDir);
    float poleLen = glm::length(poleOnPlane);
    glm::vec3 bendDir;
    if (poleLen > 0.001f) {
        bendDir = poleOnPlane / poleLen;
    } else {
        glm::vec3 cd = midPos - rootPos;
        glm::vec3 proj = cd - chainDir * glm::dot(cd, chainDir);
        float projLen = glm::length(proj);
        bendDir = (projLen > 0.001f) ? proj / projLen : glm::vec3(0, 0, 1);
    }

    // Desired mid position
    glm::vec3 desiredMidDir = chainDir * std::cos(angleRoot) + bendDir * std::sin(angleRoot);
    glm::vec3 desiredMidPos = rootPos + desiredMidDir * upperLen;

    // 4. Upper bone: rotate from current to desired knee direction (MODEL space)
    glm::vec3 curUpperDir = glm::normalize(midPos - rootPos);
    glm::vec3 desUpperDir = glm::normalize(desiredMidPos - rootPos);
    glm::quat deltaUpper_model = rotationBetween(curUpperDir, desUpperDir);
    deltaUpper_model = clampQuatAngle(deltaUpper_model, maxDeltaRad);

    // Convert model-space delta to upper bone's local space:
    //   localDelta = inv(parentModelRot) * delta_model * parentModelRot
    glm::quat parentRot = getParentModelRot(skel, chain.upperBoneId);
    glm::quat invParent = glm::inverse(parentRot);
    glm::quat localDeltaUpper = glm::normalize(invParent * deltaUpper_model * parentRot);

    // Apply with weight (slerp from identity)
    glm::quat newUpperLocal = glm::normalize(
        glm::slerp(glm::quat(1,0,0,0), localDeltaUpper, weight) * upperBone.currentRotation);
    upperBone.currentRotation = newUpperLocal;

    // 5. Recompute downstream positions after upper rotation
    //    Everything below upper bone rotates by deltaUpper around rootPos
    glm::quat effectiveDelta = glm::slerp(glm::quat(1,0,0,0), deltaUpper_model, weight);
    glm::vec3 newMidPos = rootPos + effectiveDelta * (midPos - rootPos);
    glm::vec3 newEndPos = rootPos + effectiveDelta * (endPos - rootPos);

    // 6. Mid bone: rotate lower segment from current to desired direction (MODEL space)
    glm::vec3 curLowerDir = glm::normalize(newEndPos - newMidPos);
    glm::vec3 desLowerDir = targetModel - desiredMidPos;
    float desLowerLen = glm::length(desLowerDir);
    if (desLowerLen < 0.001f) return;
    desLowerDir /= desLowerLen;

    glm::quat deltaLower_model = rotationBetween(curLowerDir, desLowerDir);
    deltaLower_model = clampQuatAngle(deltaLower_model, maxDeltaRad);

    // Mid bone's parent model-space rotation = upper bone's NEW model-space rotation
    glm::quat upperModelRot = glm::normalize(parentRot * newUpperLocal);
    glm::quat invUpperModel = glm::inverse(upperModelRot);
    glm::quat localDeltaLower = glm::normalize(invUpperModel * deltaLower_model * upperModelRot);

    glm::quat newMidLocal = glm::normalize(
        glm::slerp(glm::quat(1,0,0,0), localDeltaLower, weight) * midBone.currentRotation);
    midBone.currentRotation = newMidLocal;
}

/// Bullet raycast callback that excludes a specific collision object (self-hit prevention).
struct ExcludeBodyRayCallback : public btCollisionWorld::ClosestRayResultCallback {
    const btCollisionObject* excludeBody;
    ExcludeBodyRayCallback(const btVector3& from, const btVector3& to, const btCollisionObject* exclude)
        : btCollisionWorld::ClosestRayResultCallback(from, to), excludeBody(exclude) {}
    bool needsCollision(btBroadphaseProxy* proxy0) const override {
        if (proxy0->m_clientObject == excludeBody) return false;
        return ClosestRayResultCallback::needsCollision(proxy0);
    }
};

// ============================================================================
// IK Passes
// ============================================================================

void HybridCharacter::applyFootIK() {
    if (!ikSettings_.footIKEnabled || legChains_.empty()) return;
    if (!physicsWorld) return;

    // Reduce foot IK weight during locomotion to avoid fighting walk/run animations.
    // Full IK during idle; reduced during movement (feet lift naturally in animations).
    float stateWeight = 1.0f;
    auto state = getAnimationState();
    switch (state) {
        case AnimatedCharacterState::Walk:
        case AnimatedCharacterState::StartWalk:
        case AnimatedCharacterState::StopWalk:
        case AnimatedCharacterState::BackwardWalk:
        case AnimatedCharacterState::CrouchWalk:
        case AnimatedCharacterState::WalkStrafeLeft:
        case AnimatedCharacterState::WalkStrafeRight:
        case AnimatedCharacterState::ClimbStairs:
        case AnimatedCharacterState::DescendStairs:
            stateWeight = 0.15f;
            break;
        case AnimatedCharacterState::Run:
        case AnimatedCharacterState::StopRun:
        case AnimatedCharacterState::StrafeLeft:
        case AnimatedCharacterState::StrafeRight:
            stateWeight = 0.0f;  // Disable entirely during run — animations handle it
            break;
        case AnimatedCharacterState::Jump:
        case AnimatedCharacterState::Fall:
            stateWeight = 0.0f;  // No ground IK while airborne
            break;
        case AnimatedCharacterState::Land:
            stateWeight = 0.5f;
            break;
        default:
            stateWeight = 1.0f;  // Idle, Crouch, SitDown, etc. — full IK
            break;
    }
    if (stateWeight < 0.001f) return;

    auto& skel = getSkeletonMut();
    auto& animSys = getAnimSystemMut();
    const glm::vec3& worldPos = getWorldPositionRef();
    float footOffset = getSkeletonFootOffset();
    float yaw = getCurrentYaw();

    // Ensure global transforms are current
    animSys.updateGlobalTransforms(skel);

    glm::vec3 visualOrigin = worldPos - glm::vec3(0.0f, footOffset, 0.0f);
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), visualOrigin);
    modelMatrix = glm::rotate(modelMatrix, yaw, glm::vec3(0, 1, 0));
    glm::mat4 invModelMatrix = glm::inverse(modelMatrix);

    // One-time diagnostic logging
    static bool diagDone = false;
    bool doDiag = !diagDone;
    if (doDiag) diagDone = true;

    if (doDiag) {
        LOG_INFO("HybridIK", "=== FOOT IK DIAGNOSTIC (first frame) ===");
        LOG_INFO("HybridIK", "worldPos=({:.3f},{:.3f},{:.3f}) footOffset={:.3f} yaw={:.3f}",
                 worldPos.x, worldPos.y, worldPos.z, footOffset, yaw);
        LOG_INFO("HybridIK", "visualOrigin=({:.3f},{:.3f},{:.3f})",
                 visualOrigin.x, visualOrigin.y, visualOrigin.z);
    }

    float pelvisAdjust = 0.0f;
    int adjustCount = 0;

    for (size_t i = 0; i < legChains_.size(); ++i) {
        const LimbChain& chain = legChains_[i];
        FootIKState& state = footIKStates_[i];

        if (chain.endBoneId < 0 || chain.endBoneId >= static_cast<int>(skel.bones.size()))
            continue;

        // Get keyframed foot world position and hip world position
        glm::vec3 footWorld = getBoneWorldPos(skel.bones[chain.endBoneId], modelMatrix);
        glm::vec3 hipWorld  = getBoneWorldPos(skel.bones[chain.upperBoneId], modelMatrix);

        if (doDiag) {
            glm::vec3 kneeWorld = getBoneWorldPos(skel.bones[chain.midBoneId], modelMatrix);
            LOG_INFO("HybridIK", "Leg[{}]: hip=({:.3f},{:.3f},{:.3f}) knee=({:.3f},{:.3f},{:.3f}) foot=({:.3f},{:.3f},{:.3f})",
                     i, hipWorld.x, hipWorld.y, hipWorld.z,
                     kneeWorld.x, kneeWorld.y, kneeWorld.z,
                     footWorld.x, footWorld.y, footWorld.z);
            float chainLen = glm::distance(hipWorld, kneeWorld) + glm::distance(kneeWorld, footWorld);
            LOG_INFO("HybridIK", "  upperLen={:.3f} lowerLen={:.3f} chainLen={:.3f}",
                     glm::distance(hipWorld, kneeWorld), glm::distance(kneeWorld, footWorld), chainLen);
        }

        // Raycast down from above foot — EXCLUDE controller body to prevent self-hits
        glm::vec3 rayFrom = footWorld + glm::vec3(0, ikSettings_.footRaycastUp, 0);
        glm::vec3 rayTo   = footWorld - glm::vec3(0, ikSettings_.footRaycastDown, 0);

        btVector3 from(rayFrom.x, rayFrom.y, rayFrom.z);
        btVector3 to(rayTo.x, rayTo.y, rayTo.z);

        btRigidBody* ctrl = getControllerBody();
        ExcludeBodyRayCallback callback(from, to, ctrl);
        if (ctrl) {
            callback.m_collisionFilterGroup = ctrl->getBroadphaseProxy()->m_collisionFilterGroup;
            callback.m_collisionFilterMask = ctrl->getBroadphaseProxy()->m_collisionFilterMask;
        }

        physicsWorld->getWorld()->rayTest(from, to, callback);

        if (callback.hasHit()) {
            btVector3 hitPt = callback.m_hitPointWorld;
            state.groundTarget = glm::vec3(hitPt.x(), hitPt.y(), hitPt.z());
            state.grounded = true;

            float footDelta = state.groundTarget.y - footWorld.y;
            pelvisAdjust += footDelta;
            adjustCount++;

            float displacement = glm::distance(state.groundTarget, footWorld);

            if (doDiag) {
                LOG_INFO("HybridIK", "  rayHit=({:.3f},{:.3f},{:.3f}) footDelta={:.3f} displacement={:.3f}",
                         state.groundTarget.x, state.groundTarget.y, state.groundTarget.z,
                         footDelta, displacement);
            }

            // Only apply two-bone IK if the displacement is reasonable
            if (displacement < kMaxFootDisplacement) {
                // Convert target and pole to MODEL SPACE for the v3 solver
                glm::vec3 targetModel = glm::vec3(invModelMatrix * glm::vec4(state.groundTarget, 1.0f));

                // Pole vector: knees bend forward (character facing direction)
                glm::vec3 fwd = glm::vec3(std::sin(yaw), 0, std::cos(yaw));
                glm::vec3 poleWorld = hipWorld + fwd * 2.0f;
                glm::vec3 poleModel = glm::vec3(invModelMatrix * glm::vec4(poleWorld, 1.0f));

                float weight = ikSettings_.footIKWeight * state.blendWeight * stateWeight;
                solveTwoBoneIK_v3(skel, chain, targetModel, poleModel, weight);

                // Re-compute global transforms after each leg
                animSys.updateGlobalTransforms(skel);
            } else if (doDiag) {
                LOG_INFO("HybridIK", "  SKIPPED two-bone IK: displacement {:.3f} > max {:.3f}",
                         displacement, kMaxFootDisplacement);
            }
        } else {
            state.grounded = false;
            if (doDiag) {
                LOG_INFO("HybridIK", "  NO raycast hit for leg[{}]", i);
            }
        }
    }

    // Adjust pelvis height
    if (adjustCount > 0 && hipsBoneId_ >= 0) {
        float avgAdjust = pelvisAdjust / static_cast<float>(adjustCount);
        if (avgAdjust < 0.0f) {
            Bone& hips = skel.bones[hipsBoneId_];
            hips.currentPosition.y += avgAdjust * ikSettings_.footIKWeight * stateWeight;
        }
    }

    if (doDiag) {
        LOG_INFO("HybridIK", "=== END FOOT IK DIAGNOSTIC ===");
    }
}

void HybridCharacter::applyLean(float deltaTime) {
    if (!ikSettings_.leanEnabled) return;
    if (spineBoneId_ < 0) return;

    auto& skel = getSkeletonMut();
    float yaw = getCurrentYaw();

    glm::vec3 velocity = getControllerVelocity();
    glm::vec3 accel = (deltaTime > 0.001f) ? (velocity - prevVelocity_) / deltaTime : glm::vec3(0.0f);
    prevVelocity_ = velocity;

    // Project acceleration into character-local space
    float cy = std::cos(-yaw), sy = std::sin(-yaw);
    float aFwd  = accel.x * sy + accel.z * cy;
    float aSide = accel.x * cy - accel.z * sy;

    float maxLean = 15.0f;
    float targetFwd  = glm::clamp(aFwd  * ikSettings_.leanMultiplier, -maxLean, maxLean);
    float targetSide = glm::clamp(aSide * ikSettings_.leanMultiplier * 0.5f,
                                   -maxLean * 0.5f, maxLean * 0.5f);

    float lerpT = 1.0f - std::exp(-ikSettings_.leanSmoothSpeed * deltaTime);
    leanForward_ += (targetFwd - leanForward_) * lerpT;
    leanSide_    += (targetSide - leanSide_) * lerpT;

    float weight = ikSettings_.leanWeight;
    if (weight < 0.001f) return;

    float fwdRad  = glm::radians(leanForward_ * weight);
    float sideRad = glm::radians(leanSide_ * weight);

    Bone& spine = skel.bones[spineBoneId_];
    glm::quat leanRot = glm::angleAxis(fwdRad, glm::vec3(1, 0, 0)) *
                         glm::angleAxis(sideRad, glm::vec3(0, 0, 1));
    spine.currentRotation = spine.currentRotation * leanRot;

    if (chestBoneId_ >= 0) {
        float counterFwd  = -fwdRad  * ikSettings_.spineCounterFrac;
        float counterSide = -sideRad * ikSettings_.spineCounterFrac;
        glm::quat counterRot = glm::angleAxis(counterFwd, glm::vec3(1, 0, 0)) *
                                glm::angleAxis(counterSide, glm::vec3(0, 0, 1));
        Bone& chest = skel.bones[chestBoneId_];
        chest.currentRotation = chest.currentRotation * counterRot;
    }
}

void HybridCharacter::applyLookAt(float deltaTime) {
    if (!ikSettings_.lookAtEnabled) return;
    if (headBoneId_ < 0) return;

    auto& skel = getSkeletonMut();
    auto& animSys = getAnimSystemMut();
    const glm::vec3& worldPos = getWorldPositionRef();
    float footOffset = getSkeletonFootOffset();
    float yaw = getCurrentYaw();

    animSys.updateGlobalTransforms(skel);

    glm::vec3 visualOrigin = worldPos - glm::vec3(0.0f, footOffset, 0.0f);
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), visualOrigin);
    modelMatrix = glm::rotate(modelMatrix, yaw, glm::vec3(0, 1, 0));

    const Bone& headBone = skel.bones[headBoneId_];
    glm::vec4 headLocal = headBone.globalTransform * glm::vec4(0, 0, 0, 1);
    glm::vec3 headWorld = glm::vec3(modelMatrix * headLocal);

    glm::vec3 toTarget = lookTarget_ - headWorld;
    float dist = glm::length(toTarget);
    if (dist < 0.01f) return;
    toTarget = toTarget / dist;

    float targetYaw = std::atan2(toTarget.x, toTarget.z) - yaw;
    while (targetYaw > glm::pi<float>()) targetYaw -= 2.0f * glm::pi<float>();
    while (targetYaw < -glm::pi<float>()) targetYaw += 2.0f * glm::pi<float>();

    float targetPitch = std::asin(glm::clamp(toTarget.y, -1.0f, 1.0f));

    float maxRad = glm::radians(ikSettings_.lookAtMaxAngle);
    targetYaw = glm::clamp(targetYaw, -maxRad, maxRad);
    targetPitch = glm::clamp(targetPitch, -maxRad * 0.5f, maxRad * 0.5f);

    float lerpT = 1.0f - std::exp(-ikSettings_.lookAtSmoothSpeed * deltaTime);
    currentLookYaw_   += (targetYaw - currentLookYaw_) * lerpT;
    currentLookPitch_ += (targetPitch - currentLookPitch_) * lerpT;

    float weight = ikSettings_.lookAtWeight;
    float neckFrac = ikSettings_.neckWeight;
    float headFrac = 1.0f - neckFrac;

    if (neckBoneId_ >= 0) {
        Bone& neck = skel.bones[neckBoneId_];
        glm::quat neckRot = glm::angleAxis(currentLookYaw_ * neckFrac * weight, glm::vec3(0, 1, 0)) *
                             glm::angleAxis(currentLookPitch_ * neckFrac * weight, glm::vec3(1, 0, 0));
        neck.currentRotation = neck.currentRotation * neckRot;
    }

    {
        Bone& head = skel.bones[headBoneId_];
        glm::quat headRot = glm::angleAxis(currentLookYaw_ * headFrac * weight, glm::vec3(0, 1, 0)) *
                             glm::angleAxis(currentLookPitch_ * headFrac * weight, glm::vec3(1, 0, 0));
        head.currentRotation = head.currentRotation * headRot;
    }
}

void HybridCharacter::applyHandIK() {
    if (armChains_.empty()) return;

    auto& skel = getSkeletonMut();
    auto& animSys = getAnimSystemMut();
    const glm::vec3& worldPos = getWorldPositionRef();
    float footOffset = getSkeletonFootOffset();
    float yaw = getCurrentYaw();

    glm::vec3 visualOrigin = worldPos - glm::vec3(0.0f, footOffset, 0.0f);
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), visualOrigin);
    modelMatrix = glm::rotate(modelMatrix, yaw, glm::vec3(0, 1, 0));
    glm::mat4 invModelMatrix = glm::inverse(modelMatrix);

    // Pole target for elbows: behind the character
    glm::vec3 back = -glm::vec3(std::sin(yaw), 0, std::cos(yaw));

    // Left hand
    if (ikSettings_.leftHandIKEnabled && armChains_.size() >= 1) {
        const LimbChain& chain = armChains_[0];
        animSys.updateGlobalTransforms(skel);

        glm::vec3 shoulderWorld = getBoneWorldPos(skel.bones[chain.upperBoneId], modelMatrix);
        glm::vec3 poleWorld = shoulderWorld + back * 2.0f;

        // Convert to model space
        glm::vec3 targetModel = glm::vec3(invModelMatrix * glm::vec4(leftHandTarget_, 1.0f));
        glm::vec3 poleModel = glm::vec3(invModelMatrix * glm::vec4(poleWorld, 1.0f));

        solveTwoBoneIK_v3(skel, chain, targetModel, poleModel, ikSettings_.handIKWeight);
    }

    // Right hand
    if (ikSettings_.rightHandIKEnabled && armChains_.size() >= 2) {
        const LimbChain& chain = armChains_[1];
        animSys.updateGlobalTransforms(skel);

        glm::vec3 shoulderWorld = getBoneWorldPos(skel.bones[chain.upperBoneId], modelMatrix);
        glm::vec3 poleWorld = shoulderWorld + back * 2.0f;

        // Convert to model space
        glm::vec3 targetModel = glm::vec3(invModelMatrix * glm::vec4(rightHandTarget_, 1.0f));
        glm::vec3 poleModel = glm::vec3(invModelMatrix * glm::vec4(poleWorld, 1.0f));

        solveTwoBoneIK_v3(skel, chain, targetModel, poleModel, ikSettings_.handIKWeight);
    }
}

} // namespace Scene
} // namespace Phyxel
