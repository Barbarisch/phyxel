#include "scene/active/KinematicBody.h"
#include "utils/Logger.h"

#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Scene {

KinematicBody::KinematicBody(Physics::PhysicsWorld* world, const Config& cfg)
    : world_(world), config_(cfg) {

    // btCapsuleShape: radius + cylinderHeight (excluding the two hemispheres)
    float cylinderHeight = std::max(config_.height - 2.0f * config_.radius, 0.01f);
    shape_ = new btCapsuleShape(config_.radius, cylinderHeight);

    btVector3 inertia(0, 0, 0);
    shape_->calculateLocalInertia(config_.mass, inertia);

    btTransform startT;
    startT.setIdentity();
    startT.setOrigin(btVector3(0, 50, 0)); // overwritten by setPosition

    motionState_ = new btDefaultMotionState(startT);

    btRigidBody::btRigidBodyConstructionInfo rbInfo(config_.mass, motionState_, shape_, inertia);
    rbInfo.m_friction        = 0.0f;   // We drive velocity directly — no friction fighting us
    rbInfo.m_restitution     = 0.0f;
    rbInfo.m_linearDamping   = 0.0f;   // No artificial damping — clean velocity control
    rbInfo.m_angularDamping  = 1.0f;   // Fully damp angular — body never rotates

    body_ = new btRigidBody(rbInfo);
    body_->setAngularFactor(btVector3(0, 0, 0));  // Zero angular response
    body_->setActivationState(DISABLE_DEACTIVATION);

    world_->getWorld()->addRigidBody(body_);
    LOG_INFO("KinematicBody", "Capsule created (r=" + std::to_string(config_.radius)
             + " h=" + std::to_string(config_.height) + ")");
}

KinematicBody::~KinematicBody() {
    if (body_) {
        world_->getWorld()->removeRigidBody(body_);
        delete body_;
        body_ = nullptr;
    }
    delete motionState_;
    delete shape_;
}

// feetPos = bottom of capsule. Capsule center = feetPos.y + halfHeight.
void KinematicBody::setPosition(const glm::vec3& feetPos) {
    float centerY = feetPos.y + halfHeight();
    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(feetPos.x, centerY, feetPos.z));
    body_->setWorldTransform(t);
    if (motionState_) motionState_->setWorldTransform(t);

    // Kill horizontal velocity on teleport; keep vertical for continuity
    btVector3 v = body_->getLinearVelocity();
    body_->setLinearVelocity(btVector3(0, v.y(), 0));
    body_->clearForces();
}

// Returns feet position (bottom of capsule).
glm::vec3 KinematicBody::getPosition() const {
    btTransform t;
    motionState_->getWorldTransform(t);
    const btVector3& o = t.getOrigin();
    return glm::vec3(o.x(), o.y() - halfHeight(), o.z());
}

void KinematicBody::setMoveVelocity(const glm::vec2& xzVelocity) {
    btVector3 cur = body_->getLinearVelocity();
    body_->setLinearVelocity(btVector3(xzVelocity.x, cur.y(), xzVelocity.y));

    checkGround();

    if (jumpRequested_ && grounded_) {
        // Direct velocity set for crisp jump feel
        btVector3 v = body_->getLinearVelocity();
        body_->setLinearVelocity(btVector3(v.x(), config_.jumpImpulse, v.z()));
        jumpRequested_ = false;
        grounded_      = false;
    } else {
        jumpRequested_ = false;
    }
}

void KinematicBody::requestJump() {
    jumpRequested_ = true;
}

float KinematicBody::getVerticalVelocity() const {
    return body_->getLinearVelocity().y();
}

glm::vec3 KinematicBody::getVelocity() const {
    btVector3 v = body_->getLinearVelocity();
    return glm::vec3(v.x(), v.y(), v.z());
}

void KinematicBody::checkGround() {
    btTransform t = body_->getWorldTransform();
    btVector3 from = t.getOrigin();
    // Ray from capsule center downward past the bottom hemisphere + extra
    float rayLen = halfHeight() + config_.groundRayExtra;
    btVector3 to = from - btVector3(0, rayLen, 0);

    btCollisionWorld::ClosestRayResultCallback cb(from, to);
    world_->getWorld()->rayTest(from, to, cb);

    if (cb.hasHit() && cb.m_collisionObject != body_) {
        grounded_     = true;
        groundHeight_ = cb.m_hitPointWorld.y();
    } else {
        grounded_ = false;
    }
}

} // namespace Scene
} // namespace Phyxel
