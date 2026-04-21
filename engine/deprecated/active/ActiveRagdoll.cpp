#include "scene/active/ActiveRagdoll.h"
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
#include <cctype>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Phyxel {
namespace Scene {

// ============================================================================
// Helpers
// ============================================================================

static glm::quat btQuatToGlm(const btQuaternion& q) {
    return glm::quat(q.w(), q.x(), q.y(), q.z());
}

static btQuaternion glmQuatToBt(const glm::quat& q) {
    return btQuaternion(q.x, q.y, q.z, q.w);
}

static std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ============================================================================
// Construction / Destruction
// ============================================================================

ActiveRagdoll::ActiveRagdoll(Physics::PhysicsWorld* physicsWorld)
    : physicsWorld_(physicsWorld) {}

ActiveRagdoll::~ActiveRagdoll() {
    destroy();
}

void ActiveRagdoll::destroy() {
    if (action_ && physicsWorld_) {
        physicsWorld_->getWorld()->removeAction(action_);
        delete action_;
        action_ = nullptr;
    }

    for (auto* c : constraints_) {
        if (physicsWorld_) physicsWorld_->removeConstraint(c);
        delete c;
    }
    constraints_.clear();

    for (auto& p : parts_) {
        if (p.rigidBody && physicsWorld_)
            physicsWorld_->removeCube(p.rigidBody);
    }
    parts_.clear();

    boneToPartIndex_.clear();
    targetRotations_.clear();
    pdGainsOverrides_.clear();
    jointTypes_.clear();
    rootIndex_        = -1;
    built_            = false;
    limp_             = false;
    grounded_         = false;
    fallen_           = false;
    fallenTimer_      = 0.0f;
    tiltAngle_        = 0.0f;
    groundHeightValid_= false;
}

// ============================================================================
// Build from CharacterSkeleton
// ============================================================================

bool ActiveRagdoll::buildFromSkeleton(const CharacterSkeleton& skeleton,
                                       const glm::vec3& position) {
    if (built_) destroy();
    if (!physicsWorld_) return false;

    auto physicsBoneIds = skeleton.getPhysicsBoneIds();
    if (physicsBoneIds.empty()) return false;

    // --- Pre-compute bone world positions by traversing the hierarchy ---
    // (globalTransform is not set on procedural skeletons; localPosition is)
    std::map<int, glm::vec3> boneWorldPos;
    for (const auto& bone : skeleton.skeleton.bones) {
        if (bone.parentId < 0) {
            boneWorldPos[bone.id] = position + bone.localPosition;
        } else {
            glm::vec3 parentPos = boneWorldPos.count(bone.parentId)
                                  ? boneWorldPos.at(bone.parentId)
                                  : position;
            boneWorldPos[bone.id] = parentPos + bone.localPosition;
        }
    }

    // --- Create rigid bodies ---
    for (int boneId : physicsBoneIds) {
        glm::vec3 pivot = boneWorldPos.count(boneId) ? boneWorldPos.at(boneId) : position;
        btRigidBody* body = createBodyForBone(boneId, skeleton, pivot);
        if (!body) continue;

        const auto& bone = skeleton.skeleton.bones[boneId];
        glm::vec4 color = skeleton.appearance.getColorForBone(bone.name);

        // Body center offset from pivot (used to adjust visual offsets below)
        glm::vec3 boneOffset(0.0f);
        if (auto it = skeleton.boneOffsets.find(boneId); it != skeleton.boneOffsets.end())
            boneOffset = it->second;

        int partIdx = static_cast<int>(parts_.size());
        boneToPartIndex_[boneId] = partIdx;

        // Use per-shape voxel model entries if available; fall back to bounding box
        bool addedShapes = false;
        for (const auto& shape : skeleton.voxelModel.shapes) {
            if (shape.boneId != boneId) continue;
            // Render offset is relative to the physics body center (pivot + boneOffset)
            glm::vec3 renderOffset = shape.offset - boneOffset;
            parts_.push_back({body, shape.size, color, bone.name, renderOffset});
            addedShapes = true;
        }
        if (!addedShapes) {
            // Fallback: one box at the bone center
            glm::vec3 size(0.1f);
            if (auto it = skeleton.boneSizes.find(boneId); it != skeleton.boneSizes.end())
                size = it->second;
            parts_.push_back({body, size, color, bone.name, glm::vec3(0.0f)});
        }
    }

    // Find root bone (no parent, or first physics bone whose parent is not in the physics set)
    for (int boneId : physicsBoneIds) {
        const auto& bone = skeleton.skeleton.bones[boneId];
        bool parentInPhysics = (bone.parentId >= 0 &&
                                 boneToPartIndex_.count(bone.parentId));
        if (!parentInPhysics) {
            rootIndex_ = boneToPartIndex_[boneId];
            break;
        }
    }
    if (rootIndex_ < 0 && !parts_.empty()) rootIndex_ = 0;

    // Root body: lower damping, full angular freedom for upright stabilization
    if (rootIndex_ >= 0) {
        btRigidBody* root = parts_[rootIndex_].rigidBody;
        root->setDamping(config_.rootLinearDamping, config_.rootAngularDamping);
        root->setActivationState(DISABLE_DEACTIVATION);
        // Full angular freedom — upright torque needs unrestricted X/Z response
        root->setAngularFactor(btVector3(1.0f, 1.0f, 1.0f));
    }

    // --- Create constraints (limit-only — no motors) ---
    for (const auto& [childBoneId, jointDef] : skeleton.jointDefs) {
        if (!boneToPartIndex_.count(jointDef.parentBoneId)) continue;
        if (!boneToPartIndex_.count(childBoneId)) continue;
        createConstraint(jointDef);
        jointTypes_[childBoneId] = jointDef.type;
    }

    // Register substep action
    action_ = new Action(this);
    physicsWorld_->getWorld()->addAction(action_);

    // Initialize yaw from skeleton
    currentYaw_ = 0.0f;

    built_ = true;
    LOG_INFO_FMT("ActiveRagdoll", "Built " << parts_.size()
        << " bodies, " << constraints_.size() << " constraints");
    return true;
}

// ============================================================================
// Create rigid body for one bone
// ============================================================================

btRigidBody* ActiveRagdoll::createBodyForBone(int boneId,
                                               const CharacterSkeleton& skel,
                                               const glm::vec3& pivotWorldPos) {
    glm::vec3 size(0.1f);
    if (auto it = skel.boneSizes.find(boneId); it != skel.boneSizes.end())
        size = it->second;

    float mass = 1.0f;
    if (auto it = skel.boneMasses.find(boneId); it != skel.boneMasses.end())
        mass = std::min(it->second, 3.0f);

    // Shift from bone pivot to body center of mass (center of bounding box of all shapes)
    glm::vec3 boneWorldPos = pivotWorldPos;
    if (auto it = skel.boneOffsets.find(boneId); it != skel.boneOffsets.end())
        boneWorldPos += it->second;

    Physics::PhysicsWorld::CubeCreationParams params;
    params.position        = boneWorldPos;
    params.size            = size;
    params.mass            = mass;
    params.friction        = 0.5f;
    params.restitution     = 0.1f;
    params.linearDamping   = config_.limbLinearDamping;
    params.angularDamping  = config_.limbAngularDamping;
    params.deactivationTime = 99999.0f; // never sleep

    btRigidBody* body = physicsWorld_->createCubeInternal(params);
    if (!body) return nullptr;
    body->setActivationState(DISABLE_DEACTIVATION);
    body->setUserPointer(reinterpret_cast<void*>(1)); // mark as character part
    return body;
}

// ============================================================================
// Create constraint between two bones (limits only, no motors)
// ============================================================================

void ActiveRagdoll::createConstraint(const CharacterJointDef& jointDef) {
    btRigidBody* parentBody = parts_[boneToPartIndex_[jointDef.parentBoneId]].rigidBody;
    btRigidBody* childBody  = parts_[boneToPartIndex_[jointDef.childBoneId]].rigidBody;
    if (!parentBody || !childBody) return;

    btTypedConstraint* constraint = nullptr;

    switch (jointDef.type) {
    case JointType::Hinge: {
        btTransform frameA, frameB;
        frameA.setIdentity();
        frameB.setIdentity();

        // Align hinge axis: rotate local frame so Bullet's hinge axis (Z)
        // matches the desired hinge axis
        glm::vec3 axis = glm::normalize(jointDef.hingeAxis);
        btQuaternion rotZtoAxis;
        btVector3 btAxis(axis.x, axis.y, axis.z);
        btVector3 zAxis(0, 0, 1);
        btVector3 cross = zAxis.cross(btAxis);
        float dot = zAxis.dot(btAxis);
        if (cross.length2() > 0.0001f) {
            float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
            cross.normalize();
            rotZtoAxis.setRotation(cross, angle);
        } else if (dot < 0) {
            rotZtoAxis.setRotation(btVector3(1, 0, 0), SIMD_PI);
        } else {
            rotZtoAxis = btQuaternion::getIdentity();
        }
        frameA.setRotation(rotZtoAxis);
        frameB.setRotation(rotZtoAxis);
        frameA.setOrigin(btVector3(jointDef.parentAnchor.x, jointDef.parentAnchor.y, jointDef.parentAnchor.z));
        frameB.setOrigin(btVector3(jointDef.childAnchor.x,  jointDef.childAnchor.y,  jointDef.childAnchor.z));

        auto* hinge = new btHingeConstraint(*parentBody, *childBody, frameA, frameB);
        // Soft limits so PD torques can drive through them during transitions
        hinge->setLimit(jointDef.hingeLimitLow, jointDef.hingeLimitHigh,
                        0.9f, 0.3f, 1.0f); // softness, biasFactor, relaxationFactor
        constraint = hinge;
        break;
    }
    case JointType::ConeTwist: {
        btTransform frameA, frameB;
        frameA.setIdentity();
        frameB.setIdentity();
        // ConeTwist cone axis is X; rotate X→Y (90° around Z)
        btQuaternion rotXtoY;
        rotXtoY.setRotation(btVector3(0, 0, 1), SIMD_PI * 0.5f);
        frameA.setRotation(rotXtoY);
        frameB.setRotation(rotXtoY);
        frameA.setOrigin(btVector3(jointDef.parentAnchor.x, jointDef.parentAnchor.y, jointDef.parentAnchor.z));
        frameB.setOrigin(btVector3(jointDef.childAnchor.x,  jointDef.childAnchor.y,  jointDef.childAnchor.z));

        auto* cone = new btConeTwistConstraint(*parentBody, *childBody, frameA, frameB);
        cone->setLimit(jointDef.swingSpan1, jointDef.swingSpan2, jointDef.twistSpan,
                       0.9f, 0.3f, 1.0f);
        constraint = cone;
        break;
    }
    case JointType::Fixed: {
        btTransform frameA, frameB;
        frameA.setIdentity();
        frameB.setIdentity();
        frameA.setOrigin(btVector3(jointDef.parentAnchor.x, jointDef.parentAnchor.y, jointDef.parentAnchor.z));
        frameB.setOrigin(btVector3(jointDef.childAnchor.x,  jointDef.childAnchor.y,  jointDef.childAnchor.z));
        constraint = new btFixedConstraint(*parentBody, *childBody, frameA, frameB);
        break;
    }
    }

    if (constraint) {
        physicsWorld_->addConstraint(constraint, true);
        constraints_.push_back(constraint);
    }
}

// ============================================================================
// Target pose
// ============================================================================

void ActiveRagdoll::setTargetPose(const std::map<int, glm::quat>& worldRotations) {
    targetRotations_ = worldRotations;
}

// ============================================================================
// Movement intent
// ============================================================================

void ActiveRagdoll::setMoveDirection(const glm::vec3& dir) {
    float len = glm::length(dir);
    moveDirection_ = (len > 0.01f) ? dir / len : glm::vec3(0.0f);
    moveDirection_.y = 0.0f;
}

void ActiveRagdoll::setDesiredSpeed(float speed)  { desiredSpeed_ = speed; }
void ActiveRagdoll::setDesiredYaw(float yaw)       { desiredYaw_   = yaw;   }
void ActiveRagdoll::requestJump()                  { jumpRequested_ = true;  }

void ActiveRagdoll::goLimp() {
    limp_ = true;
    // Reduce damping so all bodies ragdoll naturally
    for (auto& p : parts_) {
        if (p.rigidBody) p.rigidBody->setDamping(0.05f, 0.1f);
    }
}

void ActiveRagdoll::restoreControl() {
    limp_ = false;
    // Restore damping
    for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
        auto& p = parts_[i];
        if (!p.rigidBody) continue;
        if (i == rootIndex_) {
            p.rigidBody->setDamping(config_.rootLinearDamping, config_.rootAngularDamping);
        } else {
            p.rigidBody->setDamping(config_.limbLinearDamping, config_.limbAngularDamping);
        }
        p.rigidBody->activate();
    }
    fallen_       = false;
    fallenTimer_  = 0.0f;
}

// ============================================================================
// State queries
// ============================================================================

float ActiveRagdoll::getVerticalVelocity() const {
    if (rootIndex_ < 0 || rootIndex_ >= static_cast<int>(parts_.size())) return 0.0f;
    auto* body = parts_[rootIndex_].rigidBody;
    if (!body) return 0.0f;
    return body->getLinearVelocity().y();
}

glm::vec3 ActiveRagdoll::getCenterOfMass() const {
    if (parts_.empty()) return glm::vec3(0.0f);
    glm::vec3 sum(0.0f);
    float totalMass = 0.0f;
    for (const auto& p : parts_) {
        if (!p.rigidBody) continue;
        float m = (p.rigidBody->getInvMass() > 0.0001f)
                  ? 1.0f / p.rigidBody->getInvMass() : 0.0f;
        btVector3 pos = p.rigidBody->getCenterOfMassPosition();
        sum += glm::vec3(pos.x(), pos.y(), pos.z()) * m;
        totalMass += m;
    }
    return (totalMass > 0.0f) ? sum / totalMass : glm::vec3(0.0f);
}

glm::vec3 ActiveRagdoll::getPosition() const {
    if (rootIndex_ < 0 || rootIndex_ >= static_cast<int>(parts_.size()))
        return glm::vec3(0.0f);
    auto* body = parts_[rootIndex_].rigidBody;
    if (!body) return glm::vec3(0.0f);
    btVector3 p = body->getCenterOfMassPosition();
    return glm::vec3(p.x(), p.y(), p.z());
}

glm::quat ActiveRagdoll::getRotation() const {
    if (rootIndex_ < 0 || rootIndex_ >= static_cast<int>(parts_.size()))
        return glm::quat(1, 0, 0, 0);
    auto* body = parts_[rootIndex_].rigidBody;
    if (!body) return glm::quat(1, 0, 0, 0);
    btQuaternion q = body->getWorldTransform().getRotation();
    return btQuatToGlm(q);
}

void ActiveRagdoll::setPosition(const glm::vec3& pos) {
    if (parts_.empty()) return;
    glm::vec3 current = getPosition();
    glm::vec3 diff    = pos - current;
    btVector3 btDiff(diff.x, diff.y, diff.z);

    for (auto& p : parts_) {
        if (!p.rigidBody) continue;
        btTransform trans = p.rigidBody->getWorldTransform();
        trans.setOrigin(trans.getOrigin() + btDiff);
        p.rigidBody->setWorldTransform(trans);
        p.rigidBody->getMotionState()->setWorldTransform(trans);
        p.rigidBody->setLinearVelocity(btVector3(0, 0, 0));
        p.rigidBody->setAngularVelocity(btVector3(0, 0, 0));
        p.rigidBody->activate();
    }
    groundHeightValid_ = false;
    fallen_            = false;
    fallenTimer_       = 0.0f;
}

void ActiveRagdoll::setPDGains(int boneId, const PDGains& gains) {
    pdGainsOverrides_[boneId] = gains;
}

// ============================================================================
// Gain selection by bone name
// ============================================================================

PDGains ActiveRagdoll::gainsForBone(const std::string& boneName) const {
    std::string lower = toLower(boneName);
    if (lower.find("spine") != std::string::npos || lower.find("chest") != std::string::npos)
        return config_.spinePD;
    if (lower.find("hip") != std::string::npos || lower.find("upleg") != std::string::npos)
        return config_.hipPD;
    if (lower.find("knee") != std::string::npos || lower.find("leg") != std::string::npos)
        return config_.kneePD;
    if (lower.find("shoulder") != std::string::npos)
        return config_.shoulderPD;
    if (lower.find("arm") != std::string::npos || lower.find("elbow") != std::string::npos
        || lower.find("forearm") != std::string::npos)
        return config_.elbowPD;
    if (lower.find("neck") != std::string::npos || lower.find("head") != std::string::npos)
        return config_.neckPD;
    return config_.defaultPD;
}

// ============================================================================
// Pre-physics substep (called via btActionInterface)
// ============================================================================

void ActiveRagdoll::prePhysicsStep(float dt) {
    if (!built_) return;

    checkGroundStatus();
    updateTiltAngle();

    // Detect fall
    if (tiltAngle_ > config_.fallAngleThreshold) {
        fallenTimer_ += dt;
        if (fallenTimer_ >= config_.fallRecoveryTime) {
            fallen_ = true;
        }
    } else {
        fallenTimer_ = 0.0f;
        if (grounded_) fallen_ = false;
    }

    if (!limp_) {
        applyRootMovement(dt);
        applyUprightTorque();
        applyPDTorques();
    }
}

// ============================================================================
// Ground detection (raycast from root body downward)
// ============================================================================

void ActiveRagdoll::checkGroundStatus() {
    if (rootIndex_ < 0) { grounded_ = false; return; }
    btRigidBody* root = parts_[rootIndex_].rigidBody;
    if (!root) { grounded_ = false; return; }

    btVector3 from = root->getCenterOfMassPosition();
    btVector3 to   = from - btVector3(0, config_.groundRayLength, 0);

    btCollisionWorld::ClosestRayResultCallback cb(from, to);
    physicsWorld_->getWorld()->rayTest(from, to, cb);

    if (cb.hasHit()) {
        // Ignore self
        bool hitSelf = false;
        for (const auto& p : parts_) {
            if (p.rigidBody == cb.m_collisionObject) { hitSelf = true; break; }
        }
        if (!hitSelf) {
            grounded_         = true;
            groundHeight_     = cb.m_hitPointWorld.y();
            groundHeightValid_ = true;
            return;
        }
    }
    grounded_ = false;
}

// ============================================================================
// Update tilt angle (angle between root body's local Y and world Y)
// ============================================================================

void ActiveRagdoll::updateTiltAngle() {
    if (rootIndex_ < 0) { tiltAngle_ = 0.0f; return; }
    btRigidBody* root = parts_[rootIndex_].rigidBody;
    if (!root) { tiltAngle_ = 0.0f; return; }

    btVector3 localUp = root->getWorldTransform().getBasis().getColumn(1);
    btVector3 worldUp(0, 1, 0);
    float dot = std::clamp(localUp.dot(worldUp), -1.0f, 1.0f);
    tiltAngle_ = std::acos(dot);
}

// ============================================================================
// Root movement — velocity-driven forces + height spring
// ============================================================================

void ActiveRagdoll::applyRootMovement(float dt) {
    if (rootIndex_ < 0) return;
    btRigidBody* root = parts_[rootIndex_].rigidBody;
    if (!root) return;

    float invMass = root->getInvMass();
    if (invMass < 0.00001f) return; // static body
    float mass = 1.0f / invMass;

    btVector3 currentVel = root->getLinearVelocity();

    // --- Horizontal movement: drive toward desired velocity ---
    btVector3 desiredHoriz(moveDirection_.x * desiredSpeed_, 0.0f, moveDirection_.z * desiredSpeed_);
    btVector3 horizError = desiredHoriz - btVector3(currentVel.x(), 0.0f, currentVel.z());
    btVector3 horizForce = horizError * (config_.moveAcceleration * mass);
    root->applyCentralForce(horizForce);

    // --- Vertical: spring toward groundHeight + offset ---
    if (groundHeightValid_) {
        float targetY   = groundHeight_ + config_.rootHeightOffset;
        float heightErr = targetY - root->getCenterOfMassPosition().y();
        float heightVel = currentVel.y();
        float vertForce = mass * (config_.heightGain * heightErr
                                 - config_.heightDamping * heightVel);
        root->applyCentralForce(btVector3(0, vertForce, 0));
    }

    // --- Yaw: torque the root toward desiredYaw_ ---
    float yawDiff = desiredYaw_ - currentYaw_;
    while (yawDiff >  static_cast<float>(M_PI)) yawDiff -= 2.0f * static_cast<float>(M_PI);
    while (yawDiff < -static_cast<float>(M_PI)) yawDiff += 2.0f * static_cast<float>(M_PI);
    float turnTorqueY = yawDiff * 30.0f
                       - root->getAngularVelocity().y() * 5.0f;
    root->applyTorque(btVector3(0, turnTorqueY, 0));
    // Track current yaw
    btVector3 fwd = root->getWorldTransform().getBasis().getColumn(2);
    currentYaw_ = std::atan2(fwd.x(), fwd.z());

    // --- Jump ---
    if (jumpRequested_ && grounded_) {
        root->applyCentralImpulse(btVector3(0, config_.jumpImpulse * mass, 0));
        jumpRequested_ = false;
    }
    (void)dt;
}

// ============================================================================
// Upright stabilization torque on root body
// ============================================================================

void ActiveRagdoll::applyUprightTorque() {
    if (rootIndex_ < 0) return;
    btRigidBody* root = parts_[rootIndex_].rigidBody;
    if (!root) return;

    btVector3 localUp = root->getWorldTransform().getBasis().getColumn(1);
    btVector3 worldUp(0, 1, 0);

    btVector3 torqueAxis = localUp.cross(worldUp);
    float sinAngle = torqueAxis.length();
    float cosAngle = localUp.dot(worldUp);
    float angle    = std::atan2(sinAngle, cosAngle);

    if (sinAngle > 0.001f) {
        torqueAxis /= sinAngle;
        btVector3 angVel = root->getAngularVelocity();
        btVector3 torque = torqueAxis * (config_.uprightKp * angle)
                          - angVel * config_.uprightKd;
        root->applyTorque(torque);
    }
}

// ============================================================================
// PD torques — drive each limb body toward its target world rotation
// ============================================================================

void ActiveRagdoll::applyPDTorques() {
    for (const auto& [boneId, partIdx] : boneToPartIndex_) {
        if (partIdx == rootIndex_) continue; // Root is handled separately

        auto rotIt = targetRotations_.find(boneId);
        if (rotIt == targetRotations_.end()) continue;

        btRigidBody* body = parts_[partIdx].rigidBody;
        if (!body) continue;

        // Per-bone gain override, or automatic selection by name
        PDGains gains = config_.defaultPD;
        if (auto it = pdGainsOverrides_.find(boneId); it != pdGainsOverrides_.end()) {
            gains = it->second;
        } else {
            gains = gainsForBone(parts_[partIdx].name);
        }

        applyPDTorqueToBody(body, rotIt->second, gains);
    }
}

// ============================================================================
// Static helper: PD torque toward targetWorldRot
// ============================================================================

void ActiveRagdoll::applyPDTorqueToBody(btRigidBody* body,
                                          const glm::quat& targetWorldRot,
                                          const PDGains& gains) {
    // Current world rotation
    btQuaternion curBt  = body->getWorldTransform().getRotation();
    glm::quat current   = btQuatToGlm(curBt);

    // Error: shortest path from current to target
    glm::quat error = targetWorldRot * glm::inverse(current);
    if (error.w < 0.0f) error = -error; // ensure shortest arc

    // Decompose to axis-angle
    float halfAngle = std::acos(std::clamp(error.w, -1.0f, 1.0f));
    float angle     = 2.0f * halfAngle;
    float sinHalf   = std::sin(halfAngle);

    glm::vec3 axis;
    if (sinHalf > 0.001f) {
        axis = glm::vec3(error.x, error.y, error.z) / sinHalf;
    } else {
        // Angle ≈ 0 — no torque needed
        return;
    }

    // Current angular velocity
    btVector3 angVelBt = body->getAngularVelocity();
    glm::vec3 angVel(angVelBt.x(), angVelBt.y(), angVelBt.z());

    // PD torque
    glm::vec3 torque = gains.kp * axis * angle - gains.kd * angVel;

    // Clamp magnitude
    float mag = glm::length(torque);
    if (mag > gains.maxTorque) torque *= (gains.maxTorque / mag);

    body->applyTorque(btVector3(torque.x, torque.y, torque.z));
    body->activate();
}

} // namespace Scene
} // namespace Phyxel
