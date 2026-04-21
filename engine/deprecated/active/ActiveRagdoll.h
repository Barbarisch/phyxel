#pragma once

#include "scene/CharacterSkeleton.h"
#include "scene/RagdollCharacter.h"
#include "physics/PhysicsWorld.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <btBulletDynamicsCommon.h>
#include <vector>
#include <map>
#include <string>

namespace Phyxel {
namespace Scene {

/// PD gains for a single joint/body.
struct PDGains {
    float kp       = 100.0f;   ///< Proportional (stiffness)
    float kd       = 10.0f;    ///< Derivative   (damping)
    float maxTorque = 200.0f;  ///< Clamp on output torque magnitude
};

/// Configuration for the ActiveRagdoll system.
struct ActiveRagdollConfig {
    // Root body movement (velocity-driven forces)
    float moveAcceleration  = 80.0f;   ///< Horizontal force gain (scales by mass)
    float heightGain        = 60.0f;   ///< Vertical spring gain for ground following
    float heightDamping     = 10.0f;   ///< Vertical velocity damping
    float rootHeightOffset  = 0.9f;    ///< How high root body sits above ground surface
    float groundRayLength   = 3.0f;    ///< Downward raycast length for ground detection
    float jumpImpulse       = 8.0f;    ///< Vertical impulse magnitude on jump (scales by mass)

    // Upright stabilization torque on the root body
    float uprightKp = 600.0f;
    float uprightKd =  60.0f;

    // Limb body physics
    float limbLinearDamping  = 0.5f;
    float limbAngularDamping = 0.7f;
    float rootLinearDamping  = 0.3f;
    float rootAngularDamping = 0.5f;

    // Fall detection
    float fallAngleThreshold = 1.0f;   ///< Radians from vertical before "fallen"
    float fallRecoveryTime   = 0.8f;   ///< Seconds before fallen state is confirmed

    // Per-joint-group PD gains (matched by bone name substring)
    PDGains spinePD    = {250.0f, 25.0f, 500.0f};
    PDGains hipPD      = {180.0f, 18.0f, 350.0f};
    PDGains kneePD     = {140.0f, 14.0f, 250.0f};
    PDGains shoulderPD = {120.0f, 12.0f, 200.0f};
    PDGains elbowPD    = { 90.0f,  9.0f, 130.0f};
    PDGains neckPD     = { 80.0f,  8.0f, 100.0f};
    PDGains defaultPD  = {120.0f, 12.0f, 220.0f};
};

/// True active ragdoll: all bodies are dynamic rigid bodies connected by
/// limit-only constraints. PD torques are applied every physics substep to
/// drive each bone toward a caller-supplied target world-space rotation.
///
/// Key difference from PhysicsDriveMode: bodies are NEVER teleported.
/// Movement and pose tracking are achieved purely through forces and torques,
/// so the character physically reacts to collisions, pushes, and terrain.
class ActiveRagdoll {
public:
    explicit ActiveRagdoll(Physics::PhysicsWorld* physicsWorld);
    ~ActiveRagdoll();

    ActiveRagdoll(const ActiveRagdoll&) = delete;
    ActiveRagdoll& operator=(const ActiveRagdoll&) = delete;

    // ---- Build / teardown ----

    /// Build rigid bodies and constraints from a CharacterSkeleton at position.
    bool buildFromSkeleton(const CharacterSkeleton& skeleton, const glm::vec3& position);

    /// Remove all bodies and constraints from the world.
    void destroy();

    bool isBuilt() const { return built_; }

    // ---- Target pose ----

    /// Set world-space target rotations for all bones (from LocomotionController).
    /// Call before the physics step each frame.
    void setTargetPose(const std::map<int, glm::quat>& worldRotations);

    // ---- Movement intent (set by LocomotionController) ----

    void setMoveDirection(const glm::vec3& dir);  ///< XZ, need not be normalized
    void setDesiredSpeed(float speed);
    void setDesiredYaw(float yaw);                ///< Radians, world-space
    void requestJump();
    void goLimp();       ///< Disable all torques — passive ragdoll (death, stun)
    void restoreControl();

    // ---- State queries ----

    bool  isGrounded()  const { return grounded_; }
    bool  hasFallen()   const { return fallen_; }
    bool  isLimp()      const { return limp_; }
    float getTiltAngle() const { return tiltAngle_; }
    float getGroundHeight() const { return groundHeight_; }
    float getVerticalVelocity() const;

    glm::vec3 getCenterOfMass() const;
    glm::vec3 getPosition()     const;  ///< Root body world position
    glm::quat getRotation()     const;  ///< Root body world rotation
    void      setPosition(const glm::vec3& pos);

    /// Get all parts for rendering.
    const std::vector<RagdollPart>& getParts() const { return parts_; }
    int getRootIndex() const { return rootIndex_; }

    ActiveRagdollConfig& config()             { return config_; }
    const ActiveRagdollConfig& config() const { return config_; }

    /// Override PD gains for a specific bone by ID.
    void setPDGains(int boneId, const PDGains& gains);

    /// Substep callback (called via btActionInterface — do not call manually).
    void prePhysicsStep(float dt);

private:
    // Build helpers
    btRigidBody* createBodyForBone(int boneId, const CharacterSkeleton& skel,
                                    const glm::vec3& pivotWorldPos);
    void createConstraint(const CharacterJointDef& jointDef);

    // Returns appropriate PDGains for a bone given its name
    PDGains gainsForBone(const std::string& boneName) const;

    // Per-substep workers
    void applyPDTorques();
    void applyRootMovement(float dt);
    void applyUprightTorque();
    void checkGroundStatus();
    void updateTiltAngle();

    // Apply PD torque to drive body toward targetWorldRot
    static void applyPDTorqueToBody(btRigidBody* body,
                                     const glm::quat& targetWorldRot,
                                     const PDGains& gains);

    Physics::PhysicsWorld* physicsWorld_ = nullptr;
    bool built_   = false;
    bool limp_    = false;
    bool grounded_ = false;
    bool fallen_   = false;
    float fallenTimer_       = 0.0f;
    float tiltAngle_         = 0.0f;
    float groundHeight_      = 0.0f;
    bool  groundHeightValid_ = false;

    std::vector<RagdollPart>        parts_;
    std::vector<btTypedConstraint*> constraints_;
    int rootIndex_ = -1;

    std::map<int, int>      boneToPartIndex_;
    std::map<int, glm::quat> targetRotations_;   ///< World-space target per bone
    std::map<int, PDGains>  pdGainsOverrides_;
    std::map<int, JointType> jointTypes_;

    // Movement
    glm::vec3 moveDirection_{0.0f};
    float desiredSpeed_  = 0.0f;
    float desiredYaw_    = 0.0f;
    float currentYaw_    = 0.0f;
    bool  jumpRequested_ = false;

    ActiveRagdollConfig config_;

    // btActionInterface wrapper for substep callbacks
    class Action : public btActionInterface {
        ActiveRagdoll* owner_;
    public:
        explicit Action(ActiveRagdoll* o) : owner_(o) {}
        void updateAction(btCollisionWorld*, btScalar dt) override {
            owner_->prePhysicsStep(static_cast<float>(dt));
        }
        void debugDraw(btIDebugDraw*) override {}
    };
    Action* action_ = nullptr;
};

} // namespace Scene
} // namespace Phyxel
