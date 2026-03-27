#include "scene/PhysicsDriveMode.h"
#include "scene/RagdollCharacter.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Scene {

// ============================================================================
// btActionInterface for Bullet substep callbacks
// ============================================================================

class PhysicsDriveMode::PhysicsDriveAction : public btActionInterface {
    PhysicsDriveMode* drive_;
public:
    explicit PhysicsDriveAction(PhysicsDriveMode* d) : drive_(d) {}
    void updateAction(btCollisionWorld*, btScalar dt) override {
        if (drive_) drive_->prePhysicsStep(static_cast<float>(dt));
    }
    void debugDraw(btIDebugDraw*) override {}
};

// ============================================================================
// Construction / Destruction
// ============================================================================

PhysicsDriveMode::PhysicsDriveMode(Physics::PhysicsWorld* physicsWorld)
    : physicsWorld_(physicsWorld) {}

PhysicsDriveMode::~PhysicsDriveMode() {
    destroy();
}

void PhysicsDriveMode::destroy() {
    // Remove action
    if (physicsAction_ && physicsWorld_) {
        physicsWorld_->getWorld()->removeAction(physicsAction_);
        delete physicsAction_;
        physicsAction_ = nullptr;
    }

    // Remove constraints first
    for (auto* constraint : constraints_) {
        if (physicsWorld_) physicsWorld_->removeConstraint(constraint);
        delete constraint;
    }
    constraints_.clear();

    // Remove rigid bodies
    for (auto& part : parts_) {
        if (part.rigidBody && physicsWorld_) {
            physicsWorld_->removeCube(part.rigidBody);
        }
    }
    parts_.clear();

    boneToPartIndex_.clear();
    boneToConstraintIndex_.clear();
    pidStates_.clear();
    jointGainsOverrides_.clear();
    targetHingeAngles_.clear();
    targetRotations_.clear();
    jointDefs_.clear();
    jointTypes_.clear();
    rootIndex_ = -1;
    built_ = false;
    fallen_ = false;
    grounded_ = false;
    limp_ = false;
}

// ============================================================================
// Build from CharacterSkeleton
// ============================================================================

bool PhysicsDriveMode::buildFromSkeleton(const CharacterSkeleton& skeleton,
                                          const glm::vec3& position) {
    if (built_) destroy();
    if (!physicsWorld_) return false;

    auto physicsBoneIds = skeleton.getPhysicsBoneIds();
    if (physicsBoneIds.empty()) return false;

    // Store joint defs for runtime use
    jointDefs_ = skeleton.jointDefs;

    // --- Create rigid bodies ---
    for (int boneId : physicsBoneIds) {
        btRigidBody* body = createBodyForBone(boneId, skeleton, position);
        if (!body) continue;

        int partIdx = static_cast<int>(parts_.size());
        boneToPartIndex_[boneId] = partIdx;

        // Color from appearance
        const auto& bone = skeleton.skeleton.bones[boneId];
        glm::vec3 rgb = skeleton.appearance.getColorForBone(bone.name);
        glm::vec4 color(rgb.r, rgb.g, rgb.b, 1.0f);

        glm::vec3 size(0.1f);
        auto sizeIt = skeleton.boneSizes.find(boneId);
        if (sizeIt != skeleton.boneSizes.end()) size = sizeIt->second;

        parts_.push_back({body, size, color, bone.name});
    }

    // Find root bone index (first physics bone with no parent in physics set)
    for (int boneId : physicsBoneIds) {
        const auto& bone = skeleton.skeleton.bones[boneId];
        if (bone.parentId < 0) {
            auto it = boneToPartIndex_.find(boneId);
            if (it != boneToPartIndex_.end()) {
                rootIndex_ = it->second;
                break;
            }
        }
    }
    // If no root found (root bone was filtered), pick first bone
    if (rootIndex_ < 0 && !parts_.empty()) {
        rootIndex_ = 0;
    }

    // Apply root body settings: limit angular factor, extra damping
    if (rootIndex_ >= 0 && rootIndex_ < static_cast<int>(parts_.size())) {
        btRigidBody* rootBody = parts_[rootIndex_].rigidBody;
        rootBody->setAngularFactor(btVector3(0, 1, 0)); // Y-only rotation for balance
        rootBody->setDamping(0.5f, 0.5f);
    }

    // --- Create constraints ---
    for (const auto& [childBoneId, jointDef] : skeleton.jointDefs) {
        // Both parent and child must be in the physics set
        if (boneToPartIndex_.find(jointDef.parentBoneId) == boneToPartIndex_.end()) continue;
        if (boneToPartIndex_.find(childBoneId) == boneToPartIndex_.end()) continue;

        createConstraint(jointDef, skeleton);
        jointTypes_[childBoneId] = jointDef.type;
    }

    // Register physics action for substep callbacks
    physicsAction_ = new PhysicsDriveAction(this);
    physicsWorld_->getWorld()->addAction(physicsAction_);

    built_ = true;
    return true;
}

// ============================================================================
// Create rigid body for a single bone
// ============================================================================

btRigidBody* PhysicsDriveMode::createBodyForBone(int boneId,
                                                   const CharacterSkeleton& skel,
                                                   const glm::vec3& worldOrigin) {
    glm::vec3 size(0.1f);
    auto sizeIt = skel.boneSizes.find(boneId);
    if (sizeIt != skel.boneSizes.end()) size = sizeIt->second;

    float mass = 1.0f;
    auto massIt = skel.boneMasses.find(boneId);
    if (massIt != skel.boneMasses.end()) mass = massIt->second;

    // Compute world position from bind pose
    const auto& bone = skel.skeleton.bones[boneId];
    // Use the bone's global transform from skeleton bind pose
    glm::vec3 boneWorldPos = worldOrigin + glm::vec3(bone.globalTransform[3]);

    // Add bone offset (center of the body vs bone pivot)
    auto offIt = skel.boneOffsets.find(boneId);
    if (offIt != skel.boneOffsets.end()) {
        boneWorldPos += offIt->second;
    }

    btRigidBody* body = physicsWorld_->createCube(boneWorldPos, size, mass);
    if (!body) return nullptr;

    body->setUserPointer((void*)1); // Mark as character part
    body->setActivationState(DISABLE_DEACTIVATION);
    body->setDamping(config_.linearDamping, config_.angularDamping);

    return body;
}

// ============================================================================
// Create constraint between two bones
// ============================================================================

void PhysicsDriveMode::createConstraint(const CharacterJointDef& jointDef,
                                         const CharacterSkeleton& skel) {
    auto parentIt = boneToPartIndex_.find(jointDef.parentBoneId);
    auto childIt = boneToPartIndex_.find(jointDef.childBoneId);
    if (parentIt == boneToPartIndex_.end() || childIt == boneToPartIndex_.end()) return;

    btRigidBody* parentBody = parts_[parentIt->second].rigidBody;
    btRigidBody* childBody = parts_[childIt->second].rigidBody;
    if (!parentBody || !childBody) return;

    btTypedConstraint* constraint = nullptr;

    switch (jointDef.type) {
    case JointType::Hinge: {
        btTransform frameA, frameB;
        frameA.setIdentity();
        frameB.setIdentity();

        // Align hinge axis: rotate local frame so the constraint axis matches the desired axis
        // Bullet hinge uses local Z as the hinge axis by default for the frame-based constructor
        btQuaternion rotZtoAxis;
        glm::vec3 axis = glm::normalize(jointDef.hingeAxis);
        // If axis is along Z (default), no rotation needed
        if (std::abs(axis.z - 1.0f) > 0.01f) {
            btVector3 btAxis(axis.x, axis.y, axis.z);
            btVector3 zAxis(0, 0, 1);
            btVector3 cross = zAxis.cross(btAxis);
            float dot = zAxis.dot(btAxis);
            if (cross.length2() > 0.0001f) {
                float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
                cross.normalize();
                rotZtoAxis.setRotation(cross, angle);
            } else if (dot < 0) {
                // Opposite direction — rotate 180 around any perpendicular axis
                rotZtoAxis.setRotation(btVector3(1, 0, 0), SIMD_PI);
            } else {
                rotZtoAxis = btQuaternion::getIdentity();
            }
        } else {
            rotZtoAxis = btQuaternion::getIdentity();
        }
        frameA.setRotation(rotZtoAxis);
        frameB.setRotation(rotZtoAxis);

        frameA.setOrigin(btVector3(jointDef.parentAnchor.x,
                                    jointDef.parentAnchor.y,
                                    jointDef.parentAnchor.z));
        frameB.setOrigin(btVector3(jointDef.childAnchor.x,
                                    jointDef.childAnchor.y,
                                    jointDef.childAnchor.z));

        auto* hinge = new btHingeConstraint(*parentBody, *childBody, frameA, frameB);
        hinge->setLimit(jointDef.hingeLimitLow, jointDef.hingeLimitHigh);
        constraint = hinge;
        break;
    }
    case JointType::ConeTwist: {
        btTransform frameA, frameB;
        frameA.setIdentity();
        frameB.setIdentity();

        // ConeTwist uses X-axis as cone axis. We want "up" (Y) as cone axis typically.
        // Rotate X → Y: 90° around Z
        btQuaternion rotXtoY;
        rotXtoY.setRotation(btVector3(0, 0, 1), SIMD_PI * 0.5f);
        frameA.setRotation(rotXtoY);
        frameB.setRotation(rotXtoY);

        frameA.setOrigin(btVector3(jointDef.parentAnchor.x,
                                    jointDef.parentAnchor.y,
                                    jointDef.parentAnchor.z));
        frameB.setOrigin(btVector3(jointDef.childAnchor.x,
                                    jointDef.childAnchor.y,
                                    jointDef.childAnchor.z));

        auto* cone = new btConeTwistConstraint(*parentBody, *childBody, frameA, frameB);
        cone->setLimit(jointDef.swingSpan1, jointDef.swingSpan2, jointDef.twistSpan);
        constraint = cone;
        break;
    }
    case JointType::Fixed: {
        btTransform frameA, frameB;
        frameA.setIdentity();
        frameB.setIdentity();
        frameA.setOrigin(btVector3(jointDef.parentAnchor.x,
                                    jointDef.parentAnchor.y,
                                    jointDef.parentAnchor.z));
        frameB.setOrigin(btVector3(jointDef.childAnchor.x,
                                    jointDef.childAnchor.y,
                                    jointDef.childAnchor.z));
        auto* fixed = new btFixedConstraint(*parentBody, *childBody, frameA, frameB);
        constraint = fixed;
        break;
    }
    }

    if (constraint) {
        physicsWorld_->addConstraint(constraint, true); // disable collisions between linked bodies
        int idx = static_cast<int>(constraints_.size());
        constraints_.push_back(constraint);
        boneToConstraintIndex_[jointDef.childBoneId] = idx;

        // Initialize PID state
        pidStates_[jointDef.childBoneId] = JointPIDState{};
    }
}

// ============================================================================
// Interpolate rotation from animation channel at given time
// ============================================================================

static glm::quat interpolateRotationFromChannel(const AnimationChannel& channel, float time) {
    if (channel.rotationKeys.empty()) return glm::quat(1, 0, 0, 0);
    if (channel.rotationKeys.size() == 1) return channel.rotationKeys[0].value;

    // Find bracketing keys
    size_t idx = 0;
    for (size_t i = 0; i < channel.rotationKeys.size() - 1; ++i) {
        if (time < channel.rotationKeys[i + 1].time) {
            idx = i;
            break;
        }
        idx = i;
    }

    size_t next = idx + 1;
    if (next >= channel.rotationKeys.size()) return channel.rotationKeys.back().value;

    float t0 = channel.rotationKeys[idx].time;
    float t1 = channel.rotationKeys[next].time;
    float dt = t1 - t0;
    float t = (dt > 0.0001f) ? std::clamp((time - t0) / dt, 0.0f, 1.0f) : 0.0f;

    return glm::slerp(channel.rotationKeys[idx].value, channel.rotationKeys[next].value, t);
}

// ============================================================================
// Set target pose from animation clip
// ============================================================================

void PhysicsDriveMode::setTargetPoseFromClip(const AnimationClip& clip, float time) {
    targetHingeAngles_.clear();
    targetRotations_.clear();

    float tickTime = std::fmod(time * clip.ticksPerSecond * clip.speed, 
                                static_cast<float>(clip.duration));
    if (tickTime < 0.0f) tickTime += static_cast<float>(clip.duration);

    for (const auto& channel : clip.channels) {
        int boneId = channel.boneId;
        // Only care about bones we have joints for
        if (jointDefs_.find(boneId) == jointDefs_.end()) continue;

        auto typeIt = jointTypes_.find(boneId);
        if (typeIt == jointTypes_.end()) continue;

        if (typeIt->second == JointType::Hinge) {
            // For hinge joints, extract rotation around the hinge axis as a single angle
            glm::quat targetRot = interpolateRotationFromChannel(channel, tickTime);
            // Convert to angle around the hinge axis
            const auto& jointDef = jointDefs_[boneId];
            glm::vec3 axis = glm::normalize(jointDef.hingeAxis);
            // Project quaternion onto the hinge axis
            // angle = 2 * atan2(dot(axis, quat.xyz), quat.w)
            float dotVal = glm::dot(axis, glm::vec3(targetRot.x, targetRot.y, targetRot.z));
            float angle = 2.0f * std::atan2(dotVal, targetRot.w);
            // Normalize to -PI..PI
            while (angle > 3.14159f) angle -= 6.28318f;
            while (angle < -3.14159f) angle += 6.28318f;
            targetHingeAngles_[boneId] = angle;
        } else if (typeIt->second == JointType::ConeTwist) {
            glm::quat targetRot = interpolateRotationFromChannel(channel, tickTime);
            targetRotations_[boneId] = targetRot;
        }
    }
}

// ============================================================================
// Update (called each frame, NOT from substep)
// ============================================================================

void PhysicsDriveMode::update(float deltaTime) {
    if (!built_) return;
    (void)deltaTime; // Main work happens in prePhysicsStep via btActionInterface
}

// ============================================================================
// Pre-physics step (called from Bullet substep via btActionInterface)
// ============================================================================

void PhysicsDriveMode::prePhysicsStep(float deltaTime) {
    if (!built_ || limp_) return;

    keepUpright(deltaTime);
    checkGroundStatus();

    // Drive joint motors toward target poses
    float strengthScale = config_.motorStrengthScale;

    for (const auto& [childBoneId, constraintIdx] : boneToConstraintIndex_) {
        if (constraintIdx < 0 || constraintIdx >= static_cast<int>(constraints_.size())) continue;

        auto typeIt = jointTypes_.find(childBoneId);
        if (typeIt == jointTypes_.end()) continue;

        auto defIt = jointDefs_.find(childBoneId);
        if (defIt == jointDefs_.end()) continue;

        btTypedConstraint* constraint = constraints_[constraintIdx];

        if (typeIt->second == JointType::Hinge) {
            auto* hinge = static_cast<btHingeConstraint*>(constraint);
            float targetAngle = 0.0f; // Default: stand straight
            auto targetIt = targetHingeAngles_.find(childBoneId);
            if (targetIt != targetHingeAngles_.end()) targetAngle = targetIt->second;

            driveHingeMotor(hinge, targetAngle, childBoneId, deltaTime);
        } else if (typeIt->second == JointType::ConeTwist) {
            auto* cone = static_cast<btConeTwistConstraint*>(constraint);
            glm::quat targetRot(1, 0, 0, 0); // Default: identity
            auto targetIt = targetRotations_.find(childBoneId);
            if (targetIt != targetRotations_.end()) targetRot = targetIt->second;

            driveConeTwistMotor(cone, targetRot, childBoneId, deltaTime);
        }
        // Fixed joints don't need motor driving
    }

    // Apply movement forces
    if (isMoving_ && rootIndex_ >= 0) {
        btRigidBody* rootBody = parts_[rootIndex_].rigidBody;
        if (rootBody) {
            btVector3 force(moveDirection_.x * config_.moveForce,
                           0,
                           moveDirection_.z * config_.moveForce);
            rootBody->activate();
            rootBody->applyCentralForce(force);

            // Rotate to face movement direction
            float targetYaw = std::atan2(moveDirection_.x, moveDirection_.z);
            btTransform trans;
            rootBody->getMotionState()->getWorldTransform(trans);
            btVector3 forward = trans.getBasis().getColumn(2);
            float currentYaw = std::atan2(forward.x(), forward.z());

            float yawDiff = targetYaw - currentYaw;
            while (yawDiff > 3.14159f) yawDiff -= 6.28318f;
            while (yawDiff < -3.14159f) yawDiff += 6.28318f;

            rootBody->applyTorque(btVector3(
                0,
                yawDiff * config_.turnStrength - rootBody->getAngularVelocity().y() * config_.turnDamping,
                0));
        }
    } else if (grounded_ && rootIndex_ >= 0) {
        // Braking when not moving
        btRigidBody* rootBody = parts_[rootIndex_].rigidBody;
        if (rootBody) {
            btVector3 vel = rootBody->getLinearVelocity();
            rootBody->applyCentralForce(btVector3(-vel.x() * config_.brakingForce,
                                                   0,
                                                   -vel.z() * config_.brakingForce));
        }
    }
}

// ============================================================================
// Hinge motor PID drive
// ============================================================================

void PhysicsDriveMode::driveHingeMotor(btHingeConstraint* hinge, float targetAngle,
                                         int childBoneId, float deltaTime) {
    JointPIDGains gains = config_.defaultJointGains;
    auto overrideIt = jointGainsOverrides_.find(childBoneId);
    if (overrideIt != jointGainsOverrides_.end()) gains = overrideIt->second;

    float currentAngle = hinge->getHingeAngle();
    float error = targetAngle - currentAngle;

    // Get PID state
    auto& pid = pidStates_[childBoneId];

    // Integral
    pid.integralError += error * deltaTime;
    pid.integralError = std::clamp(pid.integralError, -gains.maxIntegral, gains.maxIntegral);

    // Derivative (use angular velocity as negative derivative)
    // For a hinge, we'd ideally read the relative angular velocity around the hinge axis.
    // Approximation: use the error rate
    float derivative = (deltaTime > 0.0001f) ? (error - pid.previousError) / deltaTime : 0.0f;
    pid.previousError = error;

    // PID output → target velocity for the motor
    float output = gains.kp * error + gains.ki * pid.integralError + gains.kd * derivative;

    // Apply motor strength from joint def
    float maxImpulse = 100.0f;
    auto defIt = jointDefs_.find(childBoneId);
    if (defIt != jointDefs_.end()) {
        maxImpulse = defIt->second.motorStrength * config_.motorStrengthScale;
    }

    hinge->enableAngularMotor(true, output, maxImpulse);
}

// ============================================================================
// Cone-twist motor drive (torque-based, not Bullet's built-in motor)
// ============================================================================

void PhysicsDriveMode::driveConeTwistMotor(btConeTwistConstraint* cone,
                                            const glm::quat& targetRotation,
                                            int childBoneId, float deltaTime) {
    // Get the two bodies
    const btRigidBody& bodyA = cone->getRigidBodyA();
    const btRigidBody& bodyB = cone->getRigidBodyB();

    // Get current relative rotation
    btTransform transA, transB;
    bodyA.getMotionState()->getWorldTransform(transA);
    bodyB.getMotionState()->getWorldTransform(transB);

    btQuaternion currentRelRot = transA.getRotation().inverse() * transB.getRotation();
    btQuaternion targetRelRot(targetRotation.x, targetRotation.y, targetRotation.z, targetRotation.w);

    // Compute error quaternion
    btQuaternion errorQuat = targetRelRot * currentRelRot.inverse();
    // Ensure shortest path
    if (errorQuat.w() < 0) errorQuat = -errorQuat;

    btVector3 axis = errorQuat.getAxis();
    float angle = errorQuat.getAngle();
    if (angle > SIMD_PI) angle -= SIMD_2_PI;

    if (angle < 0.001f) return; // Close enough

    JointPIDGains gains = config_.defaultJointGains;
    auto overrideIt = jointGainsOverrides_.find(childBoneId);
    if (overrideIt != jointGainsOverrides_.end()) gains = overrideIt->second;

    // PID
    auto& pid = pidStates_[childBoneId];
    glm::vec3 error3D(axis.x() * angle, axis.y() * angle, axis.z() * angle);

    pid.integralError3D += error3D * deltaTime;
    float intLen = glm::length(pid.integralError3D);
    if (intLen > gains.maxIntegral) {
        pid.integralError3D *= gains.maxIntegral / intLen;
    }

    // Use child body's angular velocity as negative derivative
    btVector3 angVel = bodyB.getAngularVelocity() - bodyA.getAngularVelocity();
    glm::vec3 derivTerm(-angVel.x(), -angVel.y(), -angVel.z());

    glm::vec3 torqueVec = gains.kp * error3D + gains.ki * pid.integralError3D + gains.kd * derivTerm;

    // Clamp torque magnitude
    float maxTorque = 50.0f;
    auto defIt = jointDefs_.find(childBoneId);
    if (defIt != jointDefs_.end()) {
        maxTorque = defIt->second.motorStrength * config_.motorStrengthScale;
    }
    float torqueMag = glm::length(torqueVec);
    if (torqueMag > maxTorque) {
        torqueVec *= maxTorque / torqueMag;
    }

    // Apply torque to child body (and equal/opposite to parent)
    btVector3 btTorque(torqueVec.x, torqueVec.y, torqueVec.z);
    const_cast<btRigidBody&>(bodyB).activate();
    const_cast<btRigidBody&>(bodyB).applyTorque(btTorque);
    const_cast<btRigidBody&>(bodyA).applyTorque(-btTorque);
}

// ============================================================================
// Balance controller (PID to keep root upright)
// ============================================================================

void PhysicsDriveMode::keepUpright(float deltaTime) {
    if (rootIndex_ < 0 || rootIndex_ >= static_cast<int>(parts_.size())) return;

    btRigidBody* body = parts_[rootIndex_].rigidBody;
    if (!body) return;

    btTransform trans;
    body->getMotionState()->getWorldTransform(trans);

    btVector3 up(0, 1, 0);
    btVector3 currentUp = trans.getBasis().getColumn(1);

    btVector3 axis = currentUp.cross(up);
    float angle = currentUp.angle(up);

    // Check if fallen
    fallen_ = (angle > config_.fallAngleThreshold);

    if (angle > 0.01f) {
        // Update integral error (clamped to prevent windup)
        balanceIntegralError_ += glm::vec3(axis.x(), axis.y(), axis.z()) * angle * deltaTime;
        if (glm::length(balanceIntegralError_) > 1.0f) {
            balanceIntegralError_ = glm::normalize(balanceIntegralError_);
        }

        btVector3 torque = axis * (angle * config_.balanceKp)
                          + btVector3(balanceIntegralError_.x, balanceIntegralError_.y, balanceIntegralError_.z) * config_.balanceKi
                          - body->getAngularVelocity() * config_.balanceKd;

        body->activate();
        body->applyTorque(torque);
    } else {
        balanceIntegralError_ *= 0.95f;
    }
}

// ============================================================================
// Ground detection
// ============================================================================

void PhysicsDriveMode::checkGroundStatus() {
    if (rootIndex_ < 0 || rootIndex_ >= static_cast<int>(parts_.size())) {
        grounded_ = false;
        return;
    }

    btRigidBody* body = parts_[rootIndex_].rigidBody;
    if (!body) { grounded_ = false; return; }

    btTransform trans;
    body->getMotionState()->getWorldTransform(trans);
    btVector3 from = trans.getOrigin();
    btVector3 to = from - btVector3(0, config_.groundRayLength, 0);

    btCollisionWorld::ClosestRayResultCallback rayCallback(from, to);
    physicsWorld_->getWorld()->rayTest(from, to, rayCallback);

    grounded_ = rayCallback.hasHit();
}

// ============================================================================
// Control methods
// ============================================================================

void PhysicsDriveMode::move(const glm::vec3& direction) {
    if (glm::length(direction) > 0.01f) {
        moveDirection_ = glm::normalize(direction);
        moveDirection_.y = 0.0f; // XZ plane only
        isMoving_ = true;
    } else {
        isMoving_ = false;
    }
}

void PhysicsDriveMode::jump() {
    if (!grounded_ || rootIndex_ < 0) return;
    btRigidBody* rootBody = parts_[rootIndex_].rigidBody;
    if (rootBody) {
        rootBody->activate();
        rootBody->applyCentralImpulse(btVector3(0, config_.jumpImpulse, 0));
    }
}

void PhysicsDriveMode::stopMovement() {
    isMoving_ = false;
    moveDirection_ = glm::vec3(0.0f);
}

void PhysicsDriveMode::setPosition(const glm::vec3& pos) {
    if (parts_.empty()) return;
    glm::vec3 currentPos = getPosition();
    glm::vec3 diff = pos - currentPos;

    for (auto& part : parts_) {
        if (part.rigidBody) {
            btTransform trans;
            part.rigidBody->getMotionState()->getWorldTransform(trans);
            trans.setOrigin(trans.getOrigin() + btVector3(diff.x, diff.y, diff.z));
            part.rigidBody->setWorldTransform(trans);
            part.rigidBody->getMotionState()->setWorldTransform(trans);
            part.rigidBody->setLinearVelocity(btVector3(0, 0, 0));
            part.rigidBody->setAngularVelocity(btVector3(0, 0, 0));
        }
    }

    // Reset PID states
    balanceIntegralError_ = glm::vec3(0.0f);
    for (auto& [id, state] : pidStates_) { state.reset(); }
}

glm::vec3 PhysicsDriveMode::getPosition() const {
    if (rootIndex_ >= 0 && rootIndex_ < static_cast<int>(parts_.size())) {
        btRigidBody* body = parts_[rootIndex_].rigidBody;
        if (body) {
            btTransform trans;
            body->getMotionState()->getWorldTransform(trans);
            btVector3 pos = trans.getOrigin();
            return glm::vec3(pos.x(), pos.y(), pos.z());
        }
    }
    return glm::vec3(0.0f);
}

// ============================================================================
// Motor control
// ============================================================================

void PhysicsDriveMode::goLimp() {
    limp_ = true;
    // Disable all hinge motors
    for (const auto& [childBoneId, constraintIdx] : boneToConstraintIndex_) {
        if (constraintIdx < 0 || constraintIdx >= static_cast<int>(constraints_.size())) continue;
        auto typeIt = jointTypes_.find(childBoneId);
        if (typeIt != jointTypes_.end() && typeIt->second == JointType::Hinge) {
            auto* hinge = static_cast<btHingeConstraint*>(constraints_[constraintIdx]);
            hinge->enableAngularMotor(false, 0.0f, 0.0f);
        }
    }
}

void PhysicsDriveMode::restoreMotors() {
    limp_ = false;
    // Motors will be re-enabled on the next prePhysicsStep
}

void PhysicsDriveMode::setJointGains(int childBoneId, const JointPIDGains& gains) {
    jointGainsOverrides_[childBoneId] = gains;
}

} // namespace Scene
} // namespace Phyxel
