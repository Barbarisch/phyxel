#pragma once

#include "scene/CharacterSkeleton.h"
#include "scene/RagdollCharacter.h"
#include "physics/PhysicsWorld.h"
#include "graphics/Animation.h"
#include <vector>
#include <map>
#include <string>
#include <glm/glm.hpp>

class btRigidBody;
class btTypedConstraint;
class btHingeConstraint;
class btConeTwistConstraint;

namespace Phyxel {

namespace Graphics {
class RenderCoordinator;
}

namespace Scene {

/// Per-joint PID controller state for active ragdoll motor control.
struct JointPIDState {
    float integralError = 0.0f;      // Accumulated error (for hinge)
    glm::vec3 integralError3D{0.0f}; // Accumulated error (for cone-twist)
    float previousError = 0.0f;

    void reset() {
        integralError = 0.0f;
        integralError3D = glm::vec3(0.0f);
        previousError = 0.0f;
    }
};

/// Per-joint PID gains (only used in ragdoll/limp recovery mode).
struct JointPIDGains {
    float kp = 20.0f;
    float ki = 0.5f;
    float kd = 5.0f;
    float maxIntegral = 1.0f;
};

/// Configuration for the physics drive mode.
///
/// In normal mode, bone bodies are placed directly at their animation-target
/// positions each physics substep (kinematic pose matching).  The root body is
/// fully kinematic — moved directly by the NPC's AI controller.
/// PID motors are only active during ragdoll-to-animation recovery.
struct PhysicsDriveConfig {
    /// Global motor strength multiplier (only used during ragdoll recovery).
    float motorStrengthScale = 1.0f;

    /// Default per-joint PID gains (only used during ragdoll recovery).
    JointPIDGains defaultJointGains;

    /// Movement speed for kinematic root (units/sec).
    float moveSpeed = 3.0f;

    /// Turn rate for kinematic root (radians/sec).
    float turnRate = 6.0f;

    /// Jump impulse magnitude (temporarily makes root dynamic).
    float jumpImpulse = 200.0f;

    /// Ground detection ray length (downwards from root).
    float groundRayLength = 3.0f;

    /// Height offset: how high root body center should be above ground.
    float rootHeightOffset = 0.0f;

    /// Max angle from vertical before considered "fallen" (radians).
    float fallAngleThreshold = 1.2f;

    /// Time before a fallen character auto-recovers (seconds).
    float fallRecoveryTime = 0.5f;

    /// Safety teleport threshold below ground.
    float safetyTeleportThreshold = 10.0f;

    /// Angular damping for limb bodies (high = less wobble).
    float limbAngularDamping = 0.95f;

    /// Linear damping for limb bodies.
    float limbLinearDamping = 0.8f;

    /// Maximum mass per bone.
    float maxBoneMass = 2.0f;

    /// How strongly limb bodies are pulled toward animation targets (0-1).
    /// 1.0 = snap exactly to animation. 0.0 = pure ragdoll.
    float poseBlendStrength = 1.0f;
};

/// Active ragdoll drive mode: creates Bullet rigid bodies + motorized joints from a
/// CharacterSkeleton, and drives joint motors toward target poses from animation clips.
///
/// This is the "physics mode" half of the dual-mode character system.
/// While AnimatedDriveMode (the current AnimatedVoxelCharacter behavior) positions bones
/// kinematically from keyframes, PhysicsDriveMode creates *dynamic* bodies connected by
/// motorized constraints, and uses PID controllers at each joint to track animation targets.
/// The result is a character that approximately follows authored animations but physically
/// reacts to forces, collisions, and gravity.
class PhysicsDriveMode {
public:
    PhysicsDriveMode(Physics::PhysicsWorld* physicsWorld);
    ~PhysicsDriveMode();

    // Non-copyable
    PhysicsDriveMode(const PhysicsDriveMode&) = delete;
    PhysicsDriveMode& operator=(const PhysicsDriveMode&) = delete;

    /// Build rigid bodies + constraints from a CharacterSkeleton at the given world position.
    /// Returns true on success.
    bool buildFromSkeleton(const CharacterSkeleton& skeleton, const glm::vec3& position);

    /// Tear down all rigid bodies and constraints.
    void destroy();

    /// Whether bodies have been built.
    bool isBuilt() const { return built_; }

    // ---- Per-frame update ----

    /// Set the target pose from an animation clip at the given time.
    /// Call this each frame before update().
    void setTargetPoseFromClip(const AnimationClip& clip, float time);

    /// Main update: drives joint motors toward target pose, runs balance controller.
    /// Should be called each frame (NOT from physics substep — forces are applied via btActionInterface).
    void update(float deltaTime);

    /// Called from within Bullet's substep (via btActionInterface).
    void prePhysicsStep(float deltaTime);

    // ---- Control ----

    /// Apply movement force in the given direction (XZ plane).
    void move(const glm::vec3& direction);

    /// Apply jump impulse (if grounded).
    void jump();

    /// Stop movement (apply braking).
    void stopMovement();

    /// Teleport all bodies to a new position.
    void setPosition(const glm::vec3& pos);

    /// Get root body position.
    glm::vec3 getPosition() const;

    // ---- State queries ----

    bool isGrounded() const { return grounded_; }
    bool hasFallen() const { return fallen_; }

    /// Set motors to zero strength — becomes passive ragdoll.
    void goLimp();

    /// Restore motor strength from config.
    void restoreMotors();

    // ---- Access ----

    PhysicsDriveConfig& config() { return config_; }
    const PhysicsDriveConfig& config() const { return config_; }

    /// Get the parts (for rendering by RenderCoordinator).
    const std::vector<RagdollPart>& getParts() const { return parts_; }
    std::vector<RagdollPart>& getParts() { return parts_; }

    /// Get constraints (for external inspection/debug).
    const std::vector<btTypedConstraint*>& getConstraints() const { return constraints_; }

    /// Get the index of the root body in parts_.
    int getRootIndex() const { return rootIndex_; }

    /// Override PID gains for a specific joint (by child bone ID).
    void setJointGains(int childBoneId, const JointPIDGains& gains);

private:
    // Body creation
    btRigidBody* createBodyForBone(int boneId, const CharacterSkeleton& skel,
                                    const glm::vec3& worldOrigin);
    void createConstraint(const CharacterJointDef& jointDef, const CharacterSkeleton& skel);

    // Pose matching — directly place bone bodies at animation targets
    void matchPose();

    // Ground status
    void checkGroundStatus();

    Physics::PhysicsWorld* physicsWorld_ = nullptr;
    bool built_ = false;

    // Rigid bodies & constraints (owned)
    std::vector<RagdollPart> parts_;
    std::vector<btTypedConstraint*> constraints_;

    // Mapping: bone ID → index in parts_
    std::map<int, int> boneToPartIndex_;
    // Mapping: child bone ID → index in constraints_
    std::map<int, int> boneToConstraintIndex_;

    int rootIndex_ = -1;

    // Per-joint PID state (only used during ragdoll recovery)
    std::map<int, JointPIDState> pidStates_;
    std::map<int, JointPIDGains> jointGainsOverrides_;

    // Target pose (from animation) — bone transforms in model space
    std::map<int, glm::vec3> targetBonePositions_;   // World-space target per bone
    std::map<int, glm::quat> targetBoneRotations_;   // World-space target rotation per bone

    // Stored skeleton data for computing animation poses
    Skeleton bindSkeleton_;               // Copy of skeleton hierarchy / bind pose
    std::map<int, glm::vec3> boneOffsets_; // Center offset per bone
    glm::vec3 worldOrigin_{0.0f};         // Character world origin

    // Current yaw of the character (radians)
    float currentYaw_ = 0.0f;

    // Movement state
    glm::vec3 moveDirection_{0.0f};
    bool isMoving_ = false;
    bool grounded_ = false;
    bool fallen_ = false;
    bool limp_ = false;
    float fallenTimer_ = 0.0f;
    float groundHeight_ = 0.0f;
    bool groundHeightValid_ = false;
    glm::vec3 lastGoodPosition_{0.0f};

    PhysicsDriveConfig config_;

    // Store skeleton info needed at runtime
    std::map<int, CharacterJointDef> jointDefs_;
    std::map<int, JointType> jointTypes_;

    // btActionInterface for Bullet substep callbacks
    class PhysicsDriveAction;
    PhysicsDriveAction* physicsAction_ = nullptr;
};

} // namespace Scene
} // namespace Phyxel
