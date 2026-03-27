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

/// Per-joint PID gains. Separate from CharacterJointDef so they can be tuned at runtime.
struct JointPIDGains {
    float kp = 30.0f;   // Proportional gain
    float ki = 1.0f;    // Integral gain
    float kd = 5.0f;    // Derivative gain
    float maxIntegral = 1.0f; // Integral windup clamp
};

/// Configuration for the physics drive mode.
struct PhysicsDriveConfig {
    /// Global motor strength multiplier (0.0 = passive ragdoll, 1.0 = normal, >1 = stiff/robotic).
    float motorStrengthScale = 1.0f;

    /// PID gains for balance (keeping root upright).
    float balanceKp = 150.0f;
    float balanceKi = 5.0f;
    float balanceKd = 20.0f;

    /// Default per-joint PID gains (can be overridden per joint).
    JointPIDGains defaultJointGains;

    /// Movement parameters.
    float moveForce = 50.0f;
    float turnStrength = 20.0f;
    float turnDamping = 5.0f;
    float brakingForce = 50.0f;

    /// Jump impulse magnitude.
    float jumpImpulse = 100.0f;

    /// Ground detection ray length (downwards from root).
    float groundRayLength = 2.0f;

    /// Max angle from vertical before considered "fallen" (radians).
    float fallAngleThreshold = 1.2f; // ~70 degrees

    /// Linear damping for all bodies.
    float linearDamping = 0.1f;
    /// Angular damping for all bodies.
    float angularDamping = 0.3f;
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

    // Motor driving
    void driveHingeMotor(btHingeConstraint* hinge, float targetAngle,
                         int childBoneId, float deltaTime);
    void driveConeTwistMotor(btConeTwistConstraint* cone, const glm::quat& targetRotation,
                              int childBoneId, float deltaTime);

    // Balance
    void keepUpright(float deltaTime);
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

    // Per-joint PID state
    std::map<int, JointPIDState> pidStates_;
    // Per-joint PID gains (overrides)
    std::map<int, JointPIDGains> jointGainsOverrides_;

    // Target pose (from animation)
    std::map<int, float> targetHingeAngles_;       // For hinge joints
    std::map<int, glm::quat> targetRotations_;     // For cone-twist joints

    // Balance PID state
    glm::vec3 balanceIntegralError_{0.0f};

    // Movement state
    glm::vec3 moveDirection_{0.0f};
    bool isMoving_ = false;
    bool grounded_ = false;
    bool fallen_ = false;
    bool limp_ = false;

    PhysicsDriveConfig config_;

    // Store skeleton info needed at runtime
    std::map<int, CharacterJointDef> jointDefs_;
    std::map<int, JointType> jointTypes_;  // Quick lookup

    // btActionInterface for Bullet substep callbacks
    class PhysicsDriveAction;
    PhysicsDriveAction* physicsAction_ = nullptr;
};

} // namespace Scene
} // namespace Phyxel
