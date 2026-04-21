#pragma once

#include "physics/PhysicsWorld.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace Phyxel {
namespace Scene {

/// Result of a two-bone analytic IK solve.
struct TwoBoneIKResult {
    glm::quat upperRotation{1, 0, 0, 0};  ///< World-space rotation for the upper bone
    glm::quat lowerRotation{1, 0, 0, 0};  ///< World-space rotation for the lower bone
    bool reachable = true;                 ///< False if target was out of reach (clamped)
};

/// Analytic two-bone IK (law of cosines) in 3D.
///
/// Solves the classic two-link chain (upper arm/thigh → lower arm/shin) to
/// reach a target foot/hand position. A pole vector controls which way the
/// middle joint (knee/elbow) bends.
///
/// @param rootPos   World position of the upper bone pivot
/// @param upperLen  Length of the upper bone segment
/// @param lowerLen  Length of the lower bone segment
/// @param target    Desired end-effector (foot/hand) world position
/// @param poleVec   Direction the knee/elbow should point (world space)
/// @param chainUp   The "up" direction for the chain plane (usually world Y or Z)
TwoBoneIKResult solveTwoBoneIK(const glm::vec3& rootPos,
                                 float upperLen,
                                 float lowerLen,
                                 const glm::vec3& target,
                                 const glm::vec3& poleVec);

/// Manages the foot placement and stepping behaviour for one limb.
///
/// Each tick:
///   1. Check if the planted foot has strayed too far from its rest offset.
///   2. If yes (and the paired leg is not mid-step), raycast to find terrain
///      and trigger a new step.
///   3. Arc the foot smoothly from its current position to the new target.
///   4. Solve two-bone IK to produce target bone rotations for the PD controller.
struct LimbStepper {
    // Bone IDs in the skeleton
    int upperBoneId = -1;  ///< Hip / shoulder
    int lowerBoneId = -1;  ///< Knee / elbow
    int footBoneId  = -1;  ///< Ankle / wrist (end effector)

    std::string name;      ///< Debug label (e.g. "LeftLeg", "RightLeg")

    // Geometry
    float upperLength = 0.5f;
    float lowerLength = 0.5f;
    glm::vec3 restOffset{0.0f};   ///< Ideal foot position relative to body root (local XZ)
    glm::vec3 poleOffset{0.0f};   ///< Direction the knee should bend (local space)
    glm::vec3 localBoneAxis{0.0f, 1.0f, 0.0f}; ///< Bone-local axis pointing toward child (derived from skeleton)

    // Stepping parameters
    float stepThreshold = 0.30f;  ///< Distance that triggers a new step
    float stepDuration  = 0.16f;  ///< Time for one step arc (seconds)
    float stepHeight    = 0.22f;  ///< Arc apex height above the ground
    float raycastOffset = 1.5f;   ///< How far above rest position to start the ground raycast
    float groundClearance = 0.0f;  ///< Ankle height above ground in bind pose (from skeleton)

    // Runtime state
    glm::vec3 footWorldPos{0.0f};  ///< Currently planted foot position
    glm::vec3 stepFrom{0.0f};
    glm::vec3 stepTo{0.0f};
    float stepProgress = 1.0f;    ///< 0→1; 1.0 = foot fully planted
    bool isStepping = false;

    // IK output this frame (world-space rotations for upper and lower bones)
    TwoBoneIKResult ikResult;
    glm::vec3 currentFootTarget{0.0f};  ///< Animated foot position this frame (after arc)

    /// Place the foot at its rest position on first spawn.
    void initialize(const glm::vec3& bodyPos, float bodyYaw,
                    Physics::PhysicsWorld* physicsWorld);

    /// Decide whether to take a step; trigger one if needed.
    /// @param otherLegStepping  True if the paired leg is mid-step (stagger constraint).
    /// @return True if a step was just triggered.
    bool tryStep(const glm::vec3& bodyPos, float bodyYaw,
                 bool otherLegStepping,
                 Physics::PhysicsWorld* physicsWorld);

    /// Advance the step arc animation. Call every frame.
    void updateStep(float dt);

    /// Compute currentFootTarget and solve IK given the current hip world position.
    void solveIK(const glm::vec3& hipWorldPos, const glm::vec3& poleWorldDir);

    /// Set the collision object to ignore in ground raycasts (the character's own capsule).
    void setIgnoreBody(btRigidBody* body) { ignoreBody_ = body; }

private:
    /// Raycast downward from above restOffset to find terrain height.
    /// Returns true and sets hitPos on success.
    bool castToGround(const glm::vec3& bodyPos, float bodyYaw,
                      Physics::PhysicsWorld* physicsWorld,
                      glm::vec3& hitPos) const;

    btRigidBody* ignoreBody_ = nullptr;  ///< Capsule body to ignore in raycasts
};

} // namespace Scene
} // namespace Phyxel
