#pragma once

#include "physics/PhysicsWorld.h"
#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

namespace Phyxel {
namespace Scene {

// Single physics capsule for character movement.
// Handles position, gravity, velocity-driven XZ movement, and jumping.
// The visual skeleton is driven separately — this body has no rotation.
class KinematicBody {
public:
    struct Config {
        float radius          = 0.25f;   // Capsule radius (metres)
        float height          = 1.6f;    // Total capsule height (cylinder + 2 hemispheres)
        float mass            = 70.0f;   // kg
        float groundRayExtra  = 0.12f;   // Extra ray length below capsule bottom for ground check
        float jumpImpulse     = 6.5f;    // Vertical velocity added on jump (m/s, scaled by sqrt(mass))
        float maxStepHeight   = 0.35f;   // Automatic step-up for small ledges
    };

    explicit KinematicBody(Physics::PhysicsWorld* world, const Config& cfg = {});
    ~KinematicBody();

    KinematicBody(const KinematicBody&) = delete;
    KinematicBody& operator=(const KinematicBody&) = delete;

    // pos is the feet/base position (bottom of capsule).
    void      setPosition(const glm::vec3& feetPos);
    glm::vec3 getPosition() const;   // Returns feet/base position

    // Set desired horizontal velocity (XZ). Vertical velocity is from physics/gravity.
    void setMoveVelocity(const glm::vec2& xzVelocity);
    void requestJump();

    bool      isGrounded()         const { return grounded_; }
    float     getGroundHeight()    const { return groundHeight_; }
    float     getVerticalVelocity()const;
    glm::vec3 getVelocity()        const;

    // Half-height of the entire capsule (cylinder/2 + radius) — useful for pelvis offset.
    float halfHeight() const { return config_.height * 0.5f; }

    Config& config() { return config_; }

    /// Raw Bullet rigid body (for filtering raycasts that should ignore this capsule).
    btRigidBody* getBody() const { return body_; }

private:
    void checkGround();

    Physics::PhysicsWorld* world_       = nullptr;
    btCollisionShape*      shape_       = nullptr;
    btMotionState*         motionState_ = nullptr;
    btRigidBody*           body_        = nullptr;

    Config config_;
    bool   grounded_      = false;
    float  groundHeight_  = 0.0f;
    bool   jumpRequested_ = false;
};

} // namespace Scene
} // namespace Phyxel
