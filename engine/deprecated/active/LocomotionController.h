#pragma once

#include "scene/active/LimbIK.h"
#include "scene/CharacterSkeleton.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <string>
#include <vector>

namespace Phyxel {
namespace Scene {

/// All locomotion states. The state machine transitions between these based
/// on input and physics state (grounded, falling speed, tilt angle, etc.).
enum class LocomotionState {
    Idle,
    Walk,
    Run,
    Sprint,
    Jump,
    Peak,        ///< Apex of jump (near-zero vertical velocity)
    Fall,
    Land,        ///< Just landed, absorbing impact
    LandHard,    ///< Heavy landing (high impact velocity)
    Crouch,
    CrouchWalk,
    StandUp,     ///< Transitioning crouch → stand
    StrafeLeft,
    StrafeRight,
    Backpedal,
    TurnInPlace,
    Stumble,     ///< Hit recovery — briefly lose control
    Ragdoll,     ///< Fully limp (fallen and not yet recovered)
    GetUpFront,  ///< Getting up face-down
    GetUpBack,   ///< Getting up face-up
};

/// Input intent, set each frame by player controls or AI.
struct LocomotionInput {
    float forward = 0.0f;  ///< -1 (back) to +1 (forward)
    float strafe  = 0.0f;  ///< -1 (left) to +1 (right)
    float turn    = 0.0f;  ///< -1 to +1 yaw rate input
    bool  jump    = false;
    bool  crouch  = false;
    bool  sprint  = false;
};

/// Per-frame output from the LocomotionController.
struct LocomotionOutput {
    /// Target LOCAL bone rotations (relative to parent bone).
    /// ActiveCharacter does the FK pass to compute world-space rotations
    /// and then hands them to ActiveRagdoll.
    std::map<int, glm::quat> localTargetPose;

    /// Desired foot world positions — one entry per LimbStepper, same order.
    std::vector<glm::vec3> footTargets;

    /// Root movement intent fed to ActiveRagdoll.
    glm::vec3 moveDirection{0.0f};
    float     desiredSpeed = 0.0f;
    float     desiredYaw   = 0.0f;   ///< Radians

    LocomotionState state = LocomotionState::Idle;
};

/// State machine + procedural pose generator.
///
/// Each frame:
///   1. updateStateMachine() — transition between locomotion states.
///   2. generatePose()       — emit target local bone rotations for the new state.
///   3. updateLimbs()        — advance LimbStepper step animations.
///
/// Poses are procedural (sine waves, not keyframes), parameterised by the
/// skeleton's bone IDs so the same code drives humanoids, quadrupeds, etc.
class LocomotionController {
public:
    struct Config {
        // Speed fields removed — PoseBuilderConfig is the single authority.
        // LocomotionController receives speeds via update() parameters.
        float backpedalFraction = 0.5f;  ///< Backpedal speed = walkSpeed * this
        float strafeFraction    = 0.7f;  ///< Strafe speed = walkSpeed / runSpeed blend * this
        float crouchFraction    = 0.45f; ///< Crouch speed = walkSpeed * this
        float turnRate     = 3.5f;   ///< Rad/sec yaw rate (applied to desiredYaw)

        float landDuration     = 0.25f;
        float landHardDuration = 0.55f;
        float getUpDuration    = 1.1f;
        float stumbleDuration  = 0.45f;

        float fallVelocityThreshold = -2.5f;  ///< Vertical velocity that triggers Fall state
        float hardLandThreshold     = -6.0f;  ///< Below this → LandHard
        float fallRecoveryTime      =  0.8f;  ///< Seconds in Ragdoll before trying to get up
    };

    /// Call once after creating the character. Caches bone IDs from the skeleton
    /// and sets up limb stepper rest offsets.
    void initialize(const CharacterSkeleton& skeleton,
                    std::vector<LimbStepper>& steppers);

    /// Main update. Returns the output for this frame.
    /// @param walkSpeed/runSpeed/sprintSpeed from PoseBuilderConfig (single authority).
    LocomotionOutput update(float dt,
                             const LocomotionInput& input,
                             bool isGrounded,
                             bool hasFallen,
                             float verticalVelocity,
                             const glm::vec3& bodyPos,
                             float bodyYaw,
                             float walkSpeed,
                             float runSpeed,
                             float sprintSpeed);

    LocomotionState getState() const { return state_; }
    float getWalkPhase()       const { return walkPhase_; }

    /// Notify the controller of a ground-contact event (called from ActiveCharacter).
    void onLanded(float impactVelocity);

    Config& config()             { return config_; }
    const Config& config() const { return config_; }

private:
    // State machine
    void updateStateMachine(float dt, const LocomotionInput& input,
                             bool isGrounded, bool hasFallen,
                             float verticalVel);

    // Pose generation (outputs to LocomotionOutput::localTargetPose)
    void generatePose(float dt, const LocomotionInput& input,
                      float bodyYaw, LocomotionOutput& out,
                      float walkSpeed, float runSpeed, float sprintSpeed);

    void generateIdlePose(LocomotionOutput& out);
    void generateWalkPose(float speedFraction, LocomotionOutput& out);
    void generateRunPose(float speedFraction, LocomotionOutput& out);
    void generateJumpPose(float vertVel, LocomotionOutput& out);
    void generateFallPose(LocomotionOutput& out);
    void generateLandPose(float t, LocomotionOutput& out);
    void generateCrouchPose(LocomotionOutput& out);
    void generateCrouchWalkPose(LocomotionOutput& out);
    void generateStumblePose(float t, LocomotionOutput& out);
    void generateRagdollPose(LocomotionOutput& out);
    void generateGetUpPose(float t, bool faceDown, LocomotionOutput& out);

    // Limb stepper advancement
    void updateLimbSteppers(float dt, const glm::vec3& bodyPos, float bodyYaw);

    // Helper: look up a bone ID by name
    int findBone(const std::string& name) const;

    // Pointer to the character's limb steppers (owned by ActiveCharacter)
    std::vector<LimbStepper>* steppers_ = nullptr;

    // Bone ID cache (populated by initialize())
    std::map<std::string, int> boneMap_;

    LocomotionState state_     = LocomotionState::Idle;
    Config          config_;

    float stateTimer_    = 0.0f;
    float walkPhase_     = 0.0f;   ///< 0→2π, advances with walk/run speed
    float desiredYaw_    = 0.0f;
    float impactVelocity_ = 0.0f;
    bool  faceDownWhenFell_ = true;
    bool  wasCrouching_    = false;
    bool  wasGrounded_     = true;
    float prevVertVel_     = 0.0f;
};

} // namespace Scene
} // namespace Phyxel
