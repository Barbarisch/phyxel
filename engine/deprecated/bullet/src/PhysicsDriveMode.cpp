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
#include <glm/gtc/matrix_transform.hpp>
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
    targetBonePositions_.clear();
    targetBoneRotations_.clear();
    boneOffsets_.clear();
    jointDefs_.clear();
    jointTypes_.clear();
    rootIndex_ = -1;
    built_ = false;
    fallen_ = false;
    fallenTimer_ = 0.0f;
    grounded_ = false;
    groundHeightValid_ = false;
    groundHeight_ = 0.0f;
    limp_ = false;
    currentYaw_ = 0.0f;
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

    // Apply root body settings: make root KINEMATIC — we control it directly
    if (rootIndex_ >= 0 && rootIndex_ < static_cast<int>(parts_.size())) {
        btRigidBody* rootBody = parts_[rootIndex_].rigidBody;
        // Set kinematic: won't be affected by forces/gravity, we set its transform directly
        rootBody->setCollisionFlags(rootBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        rootBody->setActivationState(DISABLE_DEACTIVATION);
    }

    // Make all non-root bodies highly damped so they don't flail
    for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
        if (i == rootIndex_) continue;
        btRigidBody* body = parts_[i].rigidBody;
        if (body) {
            body->setDamping(config_.limbLinearDamping, config_.limbAngularDamping);
        }
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

    // Store skeleton data for computing animation poses at runtime
    bindSkeleton_ = skeleton.skeleton;
    boneOffsets_ = skeleton.boneOffsets;
    worldOrigin_ = position;
    lastGoodPosition_ = position;
    groundHeight_ = position.y - config_.rootHeightOffset;
    groundHeightValid_ = false;

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
    // Cap mass so motors can actually drive the bones
    mass = std::min(mass, config_.maxBoneMass);

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
    body->setDamping(config_.limbLinearDamping, config_.limbAngularDamping);

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
    targetBonePositions_.clear();
    targetBoneRotations_.clear();

    if (bindSkeleton_.bones.empty()) return;

    float tickTime = std::fmod(time * clip.ticksPerSecond * clip.speed, 
                                static_cast<float>(clip.duration));
    if (tickTime < 0.0f) tickTime += static_cast<float>(clip.duration);

    // Build a working copy of the skeleton with animated values
    auto animBones = bindSkeleton_.bones;

    // Apply animation channels to local transforms
    for (const auto& channel : clip.channels) {
        if (channel.boneId < 0 || channel.boneId >= static_cast<int>(animBones.size())) continue;
        auto& bone = animBones[channel.boneId];
        bone.currentRotation = interpolateRotationFromChannel(channel, tickTime);
        // Could also apply position/scale channels here if needed
    }

    // Compute global transforms with the character's current world position and yaw
    glm::vec3 rootPos = getPosition();
    glm::mat4 charTransform = glm::translate(glm::mat4(1.0f), rootPos);
    charTransform = glm::rotate(charTransform, currentYaw_, glm::vec3(0, 1, 0));

    for (auto& bone : animBones) {
        glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.currentPosition) *
                           glm::mat4_cast(bone.currentRotation) *
                           glm::scale(glm::mat4(1.0f), bone.currentScale);
        if (bone.parentId >= 0 && bone.parentId < static_cast<int>(animBones.size())) {
            bone.globalTransform = animBones[bone.parentId].globalTransform * local;
        } else {
            bone.globalTransform = charTransform * local;
        }

        // If this bone has a physics body, store its target world position/rotation
        auto partIt = boneToPartIndex_.find(bone.id);
        if (partIt != boneToPartIndex_.end()) {
            glm::vec3 bonePos = glm::vec3(bone.globalTransform[3]);

            // Add bone offset (center of body vs bone pivot)
            auto offIt = boneOffsets_.find(bone.id);
            if (offIt != boneOffsets_.end()) {
                bonePos += glm::mat3(bone.globalTransform) * offIt->second;
            }

            targetBonePositions_[bone.id] = bonePos;
            targetBoneRotations_[bone.id] = glm::quat_cast(bone.globalTransform);
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
    if (!built_) return;

    checkGroundStatus();

    // ---- Move kinematic root ----
    if (rootIndex_ >= 0 && rootIndex_ < static_cast<int>(parts_.size())) {
        btRigidBody* rootBody = parts_[rootIndex_].rigidBody;
        if (rootBody) {
            btTransform trans;
            rootBody->getMotionState()->getWorldTransform(trans);
            btVector3 pos = trans.getOrigin();

            // Snap to ground height if known
            if (groundHeightValid_) {
                pos.setY(groundHeight_ + config_.rootHeightOffset);
            }

            // Apply movement as direct position change (kinematic)
            if (isMoving_) {
                float speed = config_.moveSpeed * deltaTime;
                pos += btVector3(moveDirection_.x * speed, 0, moveDirection_.z * speed);

                // Smoothly rotate toward movement direction
                float targetYaw = std::atan2(moveDirection_.x, moveDirection_.z);
                float yawDiff = targetYaw - currentYaw_;
                while (yawDiff > 3.14159f) yawDiff -= 6.28318f;
                while (yawDiff < -3.14159f) yawDiff += 6.28318f;
                currentYaw_ += yawDiff * std::min(1.0f, config_.turnRate * deltaTime);
            }

            // Set root transform
            btQuaternion uprightRot;
            uprightRot.setRotation(btVector3(0, 1, 0), currentYaw_);
            trans.setOrigin(pos);
            trans.setRotation(uprightRot);
            rootBody->getMotionState()->setWorldTransform(trans);

            // Update world origin for bone transform computation
            worldOrigin_ = glm::vec3(pos.x(), pos.y(), pos.z());
        }
    }

    // ---- Safety teleport ----
    if (rootIndex_ >= 0 && groundHeightValid_) {
        float currentY = getPosition().y;
        if (currentY < groundHeight_ - config_.safetyTeleportThreshold) {
            setPosition(lastGoodPosition_);
        }
    }

    // Track last good position
    if (grounded_ && rootIndex_ >= 0) {
        lastGoodPosition_ = getPosition();
    }

    // ---- Place limb bodies at animation targets (kinematic pose matching) ----
    if (!limp_) {
        matchPose();
    }
}

// ============================================================================
// Pose matching — directly place limb bodies at animation targets
// ============================================================================

void PhysicsDriveMode::matchPose() {
    float blend = config_.poseBlendStrength;

    for (const auto& [boneId, partIdx] : boneToPartIndex_) {
        if (partIdx == rootIndex_) continue; // Root is kinematic, handled separately

        auto posIt = targetBonePositions_.find(boneId);
        auto rotIt = targetBoneRotations_.find(boneId);
        if (posIt == targetBonePositions_.end()) continue;

        btRigidBody* body = parts_[partIdx].rigidBody;
        if (!body) continue;

        btTransform targetTrans;
        targetTrans.setIdentity();
        targetTrans.setOrigin(btVector3(posIt->second.x, posIt->second.y, posIt->second.z));
        if (rotIt != targetBoneRotations_.end()) {
            const glm::quat& q = rotIt->second;
            targetTrans.setRotation(btQuaternion(q.x, q.y, q.z, q.w));
        }

        if (blend >= 0.99f) {
            // Full kinematic snap — directly set transform, zero velocities
            body->setWorldTransform(targetTrans);
            body->getMotionState()->setWorldTransform(targetTrans);
            body->setLinearVelocity(btVector3(0, 0, 0));
            body->setAngularVelocity(btVector3(0, 0, 0));
        } else {
            // Blend: compute velocity needed to reach target in one step
            btTransform currentTrans;
            body->getMotionState()->getWorldTransform(currentTrans);

            btVector3 posDiff = targetTrans.getOrigin() - currentTrans.getOrigin();
            body->setLinearVelocity(posDiff * blend * 60.0f); // 60 = approx fps

            btQuaternion currentRot = currentTrans.getRotation();
            btQuaternion targetRot = targetTrans.getRotation();
            btQuaternion diffRot = targetRot * currentRot.inverse();
            if (diffRot.w() < 0) diffRot = -diffRot;
            btVector3 axis = diffRot.getAxis();
            float angle = diffRot.getAngle();
            if (angle > SIMD_PI) angle -= SIMD_2_PI;
            body->setAngularVelocity(axis * angle * blend * 60.0f);
        }
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

    // Check if the hit object is NOT one of our own parts
    if (rayCallback.hasHit()) {
        const btCollisionObject* hitObj = rayCallback.m_collisionObject;
        bool hitSelf = false;
        for (const auto& part : parts_) {
            if (part.rigidBody == hitObj) {
                hitSelf = true;
                break;
            }
        }
        if (!hitSelf) {
            grounded_ = true;
            groundHeight_ = rayCallback.m_hitPointWorld.y();
            groundHeightValid_ = true;
        } else {
            grounded_ = false;
        }
    } else {
        grounded_ = false;
    }
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

    // Reset state
    for (auto& [id, state] : pidStates_) { state.reset(); }
    lastGoodPosition_ = pos;
    worldOrigin_ = pos;
    fallen_ = false;
    fallenTimer_ = 0.0f;
    groundHeightValid_ = false;
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

    // Make root body dynamic again so it can fall
    if (rootIndex_ >= 0 && rootIndex_ < static_cast<int>(parts_.size())) {
        btRigidBody* rootBody = parts_[rootIndex_].rigidBody;
        if (rootBody) {
            rootBody->setCollisionFlags(rootBody->getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT);
            btVector3 localInertia;
            float mass = config_.maxBoneMass * 2.0f;
            rootBody->getCollisionShape()->calculateLocalInertia(mass, localInertia);
            rootBody->setMassProps(mass, localInertia);
            rootBody->updateInertiaTensor();
            rootBody->setDamping(0.1f, 0.3f);
        }
    }

    // Reduce damping on all limbs so they ragdoll naturally
    for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
        if (i == rootIndex_) continue;
        if (parts_[i].rigidBody) {
            parts_[i].rigidBody->setDamping(0.1f, 0.3f);
        }
    }

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

    // Restore root to kinematic
    if (rootIndex_ >= 0 && rootIndex_ < static_cast<int>(parts_.size())) {
        btRigidBody* rootBody = parts_[rootIndex_].rigidBody;
        if (rootBody) {
            rootBody->setCollisionFlags(rootBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
            rootBody->setActivationState(DISABLE_DEACTIVATION);
        }
    }

    // Restore high damping on limbs
    for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
        if (i == rootIndex_) continue;
        if (parts_[i].rigidBody) {
            parts_[i].rigidBody->setDamping(config_.limbLinearDamping, config_.limbAngularDamping);
        }
    }
}

void PhysicsDriveMode::setJointGains(int childBoneId, const JointPIDGains& gains) {
    jointGainsOverrides_[childBoneId] = gains;
}

} // namespace Scene
} // namespace Phyxel
