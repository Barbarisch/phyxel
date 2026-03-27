#include "scene/VoxelCharacter.h"
#include "utils/Logger.h"

#include <algorithm>
#include <iostream>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Phyxel {
namespace Scene {

// ============================================================================
// Construction / Destruction
// ============================================================================

VoxelCharacter::VoxelCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position)
    : RagdollCharacter(physicsWorld, position),
      worldPosition_(position) {
    createController(position);
}

VoxelCharacter::~VoxelCharacter() {
    // Destroy physics drive before base class cleans up its parts
    physicsDrive_.reset();

    boneBodies_.clear();

    if (controllerBody_) {
        physicsWorld->removeCube(controllerBody_);
        controllerBody_ = nullptr;
    }
}

// ============================================================================
// Controller body (capsule-like box for animated mode ground collision)
// ============================================================================

void VoxelCharacter::createController(const glm::vec3& position) {
    glm::vec3 size(0.85f, 1.9f, 0.85f);
    controllerBody_ = physicsWorld->createCube(position + glm::vec3(0, 0.9f, 0), size, 60.0f);
    controllerBody_->setUserPointer(this);
    controllerBody_->setAngularFactor(btVector3(0, 0, 0));
    controllerBody_->setFriction(0.0f);
    controllerBody_->setRestitution(0.0f);
    controllerBody_->setActivationState(DISABLE_DEACTIVATION);
}

// ============================================================================
// Loading
// ============================================================================

bool VoxelCharacter::loadModel(const std::string& animFile) {
    Skeleton tempSkel;
    VoxelModel tempModel;
    if (!animSystem_.loadFromFile(animFile, tempSkel, clips_, tempModel)) {
        return false;
    }

    characterSkeleton_.skeleton = tempSkel;
    characterSkeleton_.voxelModel = tempModel;
    characterSkeleton_.appearance = appearance_;
    characterSkeleton_.computeBoneSizes();
    characterSkeleton_.generateJointDefs();

    LOG_INFO_FMT("VoxelCharacter", "Loaded " << clips_.size() << " animations from " << animFile);

    buildAnimatedBodies();
    modelLoaded_ = true;
    return true;
}

// ============================================================================
// Appearance
// ============================================================================

void VoxelCharacter::setAppearance(const CharacterAppearance& appearance) {
    appearance_ = appearance;
    characterSkeleton_.appearance = appearance;
}

void VoxelCharacter::recolorFromAppearance() {
    if (driveMode_ == DriveMode::Animated) {
        for (auto& part : parts) {
            if (part.color.a > 0.0f) {
                part.color = appearance_.getColorForBone(part.name);
            }
        }
    } else if (physicsDrive_) {
        for (auto& part : physicsDrive_->getParts()) {
            if (part.color.a > 0.0f) {
                part.color = appearance_.getColorForBone(part.name);
            }
        }
    }
}

// ============================================================================
// Build kinematic bodies for animated mode
// ============================================================================

void VoxelCharacter::buildAnimatedBodies() {
    // Clear any existing animated bodies
    for (auto& pair : boneBodies_) {
        if (pair.second) physicsWorld->removeCube(pair.second);
    }
    boneBodies_.clear();
    boneOffsets_.clear();
    parts.clear();

    const auto& skel = characterSkeleton_.skeleton;
    const auto& voxModel = characterSkeleton_.voxelModel;
    BoneFilterConfig filter = characterSkeleton_.boneFilter;

    if (voxModel.shapes.empty()) {
        // Auto-generate from skeleton hierarchy (same logic as AnimatedVoxelCharacter)
        auto childrenMap = characterSkeleton_.buildChildrenMap();

        for (const auto& bone : skel.bones) {
            if (filter.shouldSkip(bone.name)) continue;

            glm::vec3 targetVector(0.0f);
            bool hasChild = !childrenMap[bone.id].empty();

            if (hasChild) {
                int targetChildId = -1;
                if (childrenMap[bone.id].size() > 1) {
                    for (int childId : childrenMap[bone.id]) {
                        if (skel.bones[childId].name.find("Spine") != std::string::npos) {
                            targetChildId = childId;
                            break;
                        }
                    }
                }
                if (targetChildId >= 0) {
                    targetVector = skel.bones[targetChildId].localPosition;
                } else {
                    for (int childId : childrenMap[bone.id]) {
                        targetVector += skel.bones[childId].localPosition;
                    }
                    targetVector /= static_cast<float>(childrenMap[bone.id].size());
                }
            }

            float len = glm::length(targetVector);
            if (len < 0.01f) len = 0.1f;

            glm::vec3 size(0.1f);
            glm::vec3 offset(0.0f);

            if (!hasChild) {
                size = glm::vec3(0.05f);
            } else {
                offset = targetVector * 0.5f;
                glm::vec3 absDir = glm::abs(targetVector);
                float thickness = glm::clamp(len * 0.25f, 0.05f, 0.15f);

                std::string lower = bone.name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find("spine") != std::string::npos ||
                    lower.find("head") != std::string::npos ||
                    lower.find("hip") != std::string::npos) {
                    thickness = glm::clamp(len * 0.6f, 0.15f, 0.3f);
                }

                if (absDir.x >= absDir.y && absDir.x >= absDir.z)
                    size = glm::vec3(len, thickness, thickness);
                else if (absDir.y >= absDir.x && absDir.y >= absDir.z)
                    size = glm::vec3(thickness, len, thickness);
                else
                    size = glm::vec3(thickness, thickness, len);
            }

            // Apply appearance proportions
            std::string nameLower = bone.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find("head") != std::string::npos) {
                size *= appearance_.headScale;
            } else {
                glm::vec3 absDir2 = glm::abs(offset);
                if (absDir2.y >= absDir2.x && absDir2.y >= absDir2.z) {
                    size.x *= appearance_.bulkScale;
                    size.y *= appearance_.heightScale;
                    size.z *= appearance_.bulkScale;
                } else {
                    size *= appearance_.bulkScale;
                }
            }
            offset.y *= appearance_.heightScale;

            glm::vec4 color = appearance_.getColorForBone(bone.name);
            addVoxelBone(bone.name, size, offset, color);
        }
    } else {
        // Build from voxel model shapes (same logic as AnimatedVoxelCharacter)
        std::map<int, std::vector<BoneShape>> shapesByBone;
        for (const auto& shape : voxModel.shapes) {
            if (shape.boneId >= 0 && shape.boneId < static_cast<int>(skel.bones.size())) {
                shapesByBone[shape.boneId].push_back(shape);
            }
        }

        for (auto& [boneId, shapes] : shapesByBone) {
            const std::string& boneName = skel.bones[boneId].name;
            if (filter.shouldSkip(boneName)) continue;

            glm::vec3 minPt(1e9f), maxPt(-1e9f);
            for (const auto& shape : shapes) {
                glm::vec3 half = shape.size * 0.5f;
                minPt = glm::min(minPt, shape.offset - half);
                maxPt = glm::max(maxPt, shape.offset + half);
            }
            glm::vec3 totalSize = glm::max(maxPt - minPt, glm::vec3(0.05f));
            glm::vec3 centerOffset = (minPt + maxPt) * 0.5f;

            std::string nameLower = boneName;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find("head") != std::string::npos) {
                totalSize *= appearance_.headScale;
                centerOffset *= appearance_.headScale;
            } else {
                totalSize.x *= appearance_.bulkScale;
                totalSize.y *= appearance_.heightScale;
                totalSize.z *= appearance_.bulkScale;
                centerOffset.y *= appearance_.heightScale;
            }

            // Create physics body (invisible bounding box)
            addVoxelBone(boneName, totalSize, centerOffset, glm::vec4(0, 0, 0, 0));

            // Replace bounding box visual with individual shape visuals
            if (boneBodies_.count(boneId)) {
                btRigidBody* body = boneBodies_[boneId];
                if (!parts.empty()) parts.pop_back(); // Remove bounding box visual

                for (const auto& shape : shapes) {
                    glm::vec3 relOffset = shape.offset - centerOffset;
                    glm::vec4 color = appearance_.getColorForBone(boneName);
                    glm::vec3 scaledSize = shape.size;

                    if (nameLower.find("head") != std::string::npos) {
                        scaledSize *= appearance_.headScale;
                        relOffset *= appearance_.headScale;
                    } else {
                        scaledSize.x *= appearance_.bulkScale;
                        scaledSize.y *= appearance_.heightScale;
                        scaledSize.z *= appearance_.bulkScale;
                        relOffset.y *= appearance_.heightScale;
                    }

                    parts.push_back({body, scaledSize, color, boneName, relOffset});
                }
            }
        }
    }
}

void VoxelCharacter::addVoxelBone(const std::string& boneName, const glm::vec3& size,
                                   const glm::vec3& offset, const glm::vec4& color) {
    const auto& skel = characterSkeleton_.skeleton;
    auto it = skel.boneMap.find(boneName);
    if (it == skel.boneMap.end()) return;

    int boneId = it->second;

    btRigidBody* body = physicsWorld->createStaticCube(worldPosition_, size);
    body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
    body->setActivationState(DISABLE_DEACTIVATION);

    boneBodies_[boneId] = body;
    boneOffsets_[boneId] = offset;

    // Ignore collision with controller
    if (controllerBody_) {
        body->setIgnoreCollisionCheck(controllerBody_, true);
        controllerBody_->setIgnoreCollisionCheck(body, true);
    }

    // Ignore collision with other bones
    for (auto& [id, otherBody] : boneBodies_) {
        if (otherBody != body) {
            body->setIgnoreCollisionCheck(otherBody, true);
            otherBody->setIgnoreCollisionCheck(body, true);
        }
    }

    parts.push_back({body, size, color, boneName});
}

// ============================================================================
// Drive Mode Switching
// ============================================================================

void VoxelCharacter::setDriveMode(DriveMode mode) {
    if (mode == driveMode_) return;
    if (!modelLoaded_) return;

    DriveMode oldMode = driveMode_;
    driveMode_ = mode;

    switch (mode) {
    case DriveMode::Animated:
        transitionToAnimated();
        break;
    case DriveMode::Physics:
        transitionToPhysics();
        break;
    case DriveMode::PassiveRagdoll:
        transitionToPassiveRagdoll();
        break;
    }

    LOG_INFO_FMT("VoxelCharacter", "Drive mode: " << static_cast<int>(oldMode)
                 << " -> " << static_cast<int>(mode));
}

void VoxelCharacter::transitionToAnimated() {
    // Tear down physics drive if it exists
    if (physicsDrive_) {
        // Snapshot position before destroying
        glm::vec3 pos = physicsDrive_->getPosition();
        physicsDrive_->destroy();
        physicsDrive_.reset();

        // Rebuild animated kinematic bodies
        buildAnimatedBodies();
        worldPosition_ = pos;

        // Reposition controller
        if (controllerBody_) {
            float halfHeight = 0.9f;
            if (controllerBody_->getCollisionShape()->getShapeType() == BOX_SHAPE_PROXYTYPE) {
                auto* box = static_cast<const btBoxShape*>(controllerBody_->getCollisionShape());
                halfHeight = box->getHalfExtentsWithMargin().y();
            }
            btTransform trans;
            trans.setIdentity();
            trans.setOrigin(btVector3(pos.x, pos.y + halfHeight, pos.z));
            controllerBody_->setWorldTransform(trans);
            controllerBody_->getMotionState()->setWorldTransform(trans);
            controllerBody_->setLinearVelocity(btVector3(0, 0, 0));
        }

        // Re-enable controller collisions
        controllerBody_->setCollisionFlags(
            controllerBody_->getCollisionFlags() & ~btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }
}

void VoxelCharacter::transitionToPhysics() {
    // Snapshot current position
    glm::vec3 pos = worldPosition_;

    // Clear animated mode bodies
    for (auto& [id, body] : boneBodies_) {
        if (body) physicsWorld->removeCube(body);
    }
    boneBodies_.clear();
    boneOffsets_.clear();
    parts.clear();

    // Disable controller collisions (physics drive handles its own bodies)
    if (controllerBody_) {
        controllerBody_->setCollisionFlags(
            controllerBody_->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
        controllerBody_->setLinearVelocity(btVector3(0, 0, 0));
    }

    // Create physics drive
    physicsDrive_ = std::make_unique<PhysicsDriveMode>(physicsWorld);
    physicsDrive_->buildFromSkeleton(characterSkeleton_, pos);

    // If we have a current animation, set it as the initial target
    if (currentClipIndex_ >= 0 && currentClipIndex_ < static_cast<int>(clips_.size())) {
        physicsDrive_->setTargetPoseFromClip(clips_[currentClipIndex_], animTime_);
    }
}

void VoxelCharacter::transitionToPassiveRagdoll() {
    if (!physicsDrive_) {
        // Need to create physics bodies first, then go limp
        transitionToPhysics();
    }
    if (physicsDrive_) {
        physicsDrive_->goLimp();
    }
}

// ============================================================================
// Entity Interface
// ============================================================================

void VoxelCharacter::update(float deltaTime) {
    switch (driveMode_) {
    case DriveMode::Animated:
        updateAnimated(deltaTime);
        break;
    case DriveMode::Physics:
    case DriveMode::PassiveRagdoll:
        if (physicsDrive_) {
            // Update animation target for physics motors
            if (driveMode_ == DriveMode::Physics &&
                currentClipIndex_ >= 0 && currentClipIndex_ < static_cast<int>(clips_.size())) {
                animTime_ += deltaTime;
                physicsDrive_->setTargetPoseFromClip(clips_[currentClipIndex_], animTime_);
            }
            physicsDrive_->update(deltaTime);
        }
        break;
    }
}

void VoxelCharacter::render(Graphics::RenderCoordinator* /*renderer*/) {
    // Rendering handled by RenderCoordinator via getParts()
}

void VoxelCharacter::setPosition(const glm::vec3& pos) {
    worldPosition_ = pos;
    if (driveMode_ == DriveMode::Animated) {
        if (controllerBody_) {
            float halfHeight = 0.9f;
            if (controllerBody_->getCollisionShape()->getShapeType() == BOX_SHAPE_PROXYTYPE) {
                auto* box = static_cast<const btBoxShape*>(controllerBody_->getCollisionShape());
                halfHeight = box->getHalfExtentsWithMargin().y();
            }
            btTransform trans;
            trans.setIdentity();
            trans.setOrigin(btVector3(pos.x, pos.y + halfHeight, pos.z));
            controllerBody_->setWorldTransform(trans);
            controllerBody_->getMotionState()->setWorldTransform(trans);
            controllerBody_->setLinearVelocity(btVector3(0, 0, 0));
            controllerBody_->setAngularVelocity(btVector3(0, 0, 0));
        }
    } else if (physicsDrive_) {
        physicsDrive_->setPosition(pos);
    }
}

glm::vec3 VoxelCharacter::getPosition() const {
    if (driveMode_ == DriveMode::Animated) {
        return worldPosition_;
    } else if (physicsDrive_) {
        return physicsDrive_->getPosition();
    }
    return worldPosition_;
}

void VoxelCharacter::setMoveVelocity(const glm::vec3& velocity) {
    if (driveMode_ == DriveMode::Animated && controllerBody_) {
        btVector3 currentVel = controllerBody_->getLinearVelocity();
        controllerBody_->setLinearVelocity(btVector3(velocity.x, currentVel.y(), velocity.z));
        controllerBody_->activate(true);
    } else if (physicsDrive_) {
        physicsDrive_->move(velocity);
    }
}

// ============================================================================
// Animation Control
// ============================================================================

void VoxelCharacter::playAnimation(const std::string& animName) {
    if (currentClipIndex_ >= 0 && currentClipIndex_ < static_cast<int>(clips_.size()) &&
        clips_[currentClipIndex_].name == animName) {
        return; // Already playing
    }

    int newIdx = -1;
    for (size_t i = 0; i < clips_.size(); ++i) {
        if (clips_[i].name == animName) {
            newIdx = static_cast<int>(i);
            break;
        }
    }

    if (newIdx >= 0) {
        previousClipIndex_ = currentClipIndex_;
        previousAnimTime_ = animTime_;
        currentClipIndex_ = newIdx;
        animTime_ = 0.0f;
        isBlending_ = (previousClipIndex_ >= 0);
        blendFactor_ = 0.0f;
    }
}

std::vector<std::string> VoxelCharacter::getAnimationNames() const {
    std::vector<std::string> names;
    names.reserve(clips_.size());
    for (const auto& clip : clips_) names.push_back(clip.name);
    return names;
}

void VoxelCharacter::cycleAnimation(bool next) {
    if (clips_.empty()) return;
    int idx = currentClipIndex_;
    if (next) {
        idx++;
        if (idx >= static_cast<int>(clips_.size())) idx = 0;
    } else {
        idx--;
        if (idx < 0) idx = static_cast<int>(clips_.size()) - 1;
    }
    charState_ = CharState::Preview;
    playAnimation(clips_[idx].name);
}

void VoxelCharacter::setAnimationMapping(const std::string& stateName, const std::string& animName) {
    animationMapping_[stateName] = animName;
}

std::string VoxelCharacter::getAnimationMapping(const std::string& stateName) const {
    auto it = animationMapping_.find(stateName);
    return (it != animationMapping_.end()) ? it->second : "";
}

// ============================================================================
// Character Control
// ============================================================================

void VoxelCharacter::setControlInput(float forward, float turn, float strafe) {
    forwardInput_ = forward;
    turnInput_ = turn;
    strafeInput_ = strafe;

    // In physics mode, translate inputs to movement direction
    if (driveMode_ != DriveMode::Animated && physicsDrive_) {
        float yaw = currentYaw_;
        glm::vec3 fwd(-std::sin(yaw), 0, -std::cos(yaw));
        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        glm::vec3 dir = fwd * forward - right * strafe;
        if (glm::length(dir) > 0.01f) {
            physicsDrive_->move(dir);
        } else {
            physicsDrive_->stopMovement();
        }
    }
}

void VoxelCharacter::setSprint(bool sprint) { isSprinting_ = sprint; }

void VoxelCharacter::jump() {
    if (driveMode_ == DriveMode::Animated) {
        jumpRequested_ = true;
    } else if (physicsDrive_) {
        physicsDrive_->jump();
    }
}

void VoxelCharacter::attack() { attackRequested_ = true; }
void VoxelCharacter::setCrouch(bool crouch) { isCrouching_ = crouch; }

// ============================================================================
// State Machine (Animated mode)
// ============================================================================

std::string VoxelCharacter::charStateToString(CharState state) const {
    switch (state) {
    case CharState::Idle: return "Idle";
    case CharState::Walk: return "Walk";
    case CharState::Run: return "Run";
    case CharState::Jump: return "Jump";
    case CharState::Fall: return "Fall";
    case CharState::Land: return "Land";
    case CharState::Crouch: return "Crouch";
    case CharState::CrouchIdle: return "CrouchIdle";
    case CharState::CrouchWalk: return "CrouchWalk";
    case CharState::StandUp: return "StandUp";
    case CharState::Attack: return "Attack";
    case CharState::Preview: return "Preview";
    default: return "Idle";
    }
}

void VoxelCharacter::updateStateMachine(float deltaTime) {
    CharState prev = charState_;
    stateTimer_ += deltaTime;

    float currentAnimDuration = 0.0f;
    if (currentClipIndex_ >= 0 && currentClipIndex_ < static_cast<int>(clips_.size())) {
        currentAnimDuration = clips_[currentClipIndex_].duration;
    }

    float verticalVel = 0.0f;
    if (controllerBody_) verticalVel = controllerBody_->getLinearVelocity().y();

    switch (charState_) {
    case CharState::Idle:
    case CharState::Walk:
    case CharState::Run:
    case CharState::Crouch:
    case CharState::CrouchIdle:
    case CharState::CrouchWalk:
        // Priority: jump > attack > fall > movement
        if (jumpRequested_) {
            charState_ = CharState::Jump;
            stateTimer_ = 0.0f;
            jumpRequested_ = false;
            if (controllerBody_) {
                btVector3 vel = controllerBody_->getLinearVelocity();
                controllerBody_->setLinearVelocity(btVector3(vel.x(), 7.0f, vel.z()));
            }
        } else if (attackRequested_) {
            charState_ = CharState::Attack;
            stateTimer_ = 0.0f;
            attackRequested_ = false;
        } else if (verticalVel < -5.0f) {
            charState_ = CharState::Fall;
            stateTimer_ = 0.0f;
        } else if (isCrouching_) {
            if (std::abs(forwardInput_) > 0.01f) {
                charState_ = CharState::CrouchWalk;
            } else if (charState_ != CharState::Crouch && charState_ != CharState::CrouchIdle) {
                charState_ = CharState::Crouch;
                stateTimer_ = 0.0f;
            } else if (charState_ == CharState::Crouch && currentAnimDuration > 0.0f && stateTimer_ >= currentAnimDuration) {
                charState_ = CharState::CrouchIdle;
            }
        } else {
            // Stand up from crouch
            if (charState_ == CharState::Crouch || charState_ == CharState::CrouchIdle || charState_ == CharState::CrouchWalk) {
                charState_ = CharState::StandUp;
                stateTimer_ = 0.0f;
                break;
            }
            // Movement
            if (std::abs(forwardInput_) > 0.6f) {
                charState_ = CharState::Run;
            } else if (std::abs(forwardInput_) > 0.01f || std::abs(strafeInput_) > 0.1f) {
                charState_ = CharState::Walk;
            } else {
                charState_ = CharState::Idle;
            }
        }
        break;

    case CharState::StandUp:
        if (currentAnimDuration > 0.0f && stateTimer_ >= currentAnimDuration) charState_ = CharState::Idle;
        else if (std::abs(forwardInput_) > 0.1f) charState_ = CharState::Walk;
        break;

    case CharState::Jump:
        if (verticalVel < -2.0f) charState_ = CharState::Fall;
        else if (std::abs(verticalVel) < 0.01f && stateTimer_ > 0.5f) charState_ = CharState::Idle;
        break;

    case CharState::Fall:
        if (std::abs(verticalVel) < 0.1f) { charState_ = CharState::Land; stateTimer_ = 0.0f; }
        break;

    case CharState::Land:
        if (currentAnimDuration > 0.0f && stateTimer_ >= currentAnimDuration) charState_ = CharState::Idle;
        if (std::abs(forwardInput_) > 0.1f) charState_ = CharState::Walk;
        break;

    case CharState::Attack:
        if (currentAnimDuration > 0.0f && stateTimer_ >= currentAnimDuration) charState_ = CharState::Idle;
        break;

    case CharState::Preview:
        if (std::abs(forwardInput_) > 0.01f || std::abs(strafeInput_) > 0.01f ||
            jumpRequested_ || attackRequested_ || isCrouching_) {
            charState_ = CharState::Idle;
        }
        break;
    }

    if (charState_ != prev) {
        LOG_DEBUG_FMT("VoxelCharacter", "State: " << charStateToString(prev) << " -> " << charStateToString(charState_));
    }
}

// ============================================================================
// Animated Mode Update
// ============================================================================

void VoxelCharacter::updateAnimated(float deltaTime) {
    if (!controllerBody_) return;

    // Handle rotation
    currentYaw_ -= turnInput_ * 2.0f * deltaTime;

    updateStateMachine(deltaTime);

    // Movement speed
    float moveSpeed = 0.0f;
    if (currentClipIndex_ >= 0 && currentClipIndex_ < static_cast<int>(clips_.size())) {
        float animSpeed = clips_[currentClipIndex_].speed;
        if (animSpeed > 0.1f) moveSpeed = animSpeed;
    }

    switch (charState_) {
    case CharState::Walk: if (moveSpeed < 0.1f) moveSpeed = 2.0f; break;
    case CharState::Run:
        if (moveSpeed < 0.1f) moveSpeed = 5.0f;
        if (std::abs(forwardInput_) > 0.9f) moveSpeed = 8.0f;
        break;
    case CharState::CrouchWalk: moveSpeed = 1.5f; break;
    case CharState::Jump:
    case CharState::Fall:
        if (moveSpeed < 0.1f) moveSpeed = 4.0f;
        moveSpeed *= 0.8f;
        break;
    default: moveSpeed = 0.0f; break;
    }

    // Direction
    glm::vec3 fwd(-std::sin(currentYaw_), 0, -std::cos(currentYaw_));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    float inputDir = (forwardInput_ > 0.01f) ? 1.0f : ((forwardInput_ < -0.01f) ? -1.0f : 0.0f);
    float strafeDir = (strafeInput_ > 0.01f) ? 1.0f : ((strafeInput_ < -0.01f) ? -1.0f : 0.0f);

    glm::vec3 moveDir = fwd * inputDir - right * strafeDir;
    if (glm::length(moveDir) > 0.001f) moveDir = glm::normalize(moveDir);
    glm::vec3 moveVel = moveDir * moveSpeed;

    btVector3 currentVel = controllerBody_->getLinearVelocity();
    controllerBody_->setLinearVelocity(btVector3(moveVel.x, currentVel.y(), moveVel.z));

    // Update position from physics
    btTransform trans;
    controllerBody_->getMotionState()->getWorldTransform(trans);
    btVector3 cPos = trans.getOrigin();
    float halfHeight = 0.9f;
    if (controllerBody_->getCollisionShape()->getShapeType() == BOX_SHAPE_PROXYTYPE) {
        auto* box = static_cast<const btBoxShape*>(controllerBody_->getCollisionShape());
        halfHeight = box->getHalfExtentsWithMargin().y();
    }
    worldPosition_ = glm::vec3(cPos.x(), cPos.y() - halfHeight, cPos.z());

    // Animation selection
    std::string targetAnim = "idle";
    std::string stateKey = charStateToString(charState_);
    auto mapIt = animationMapping_.find(stateKey);
    if (mapIt != animationMapping_.end()) {
        targetAnim = mapIt->second;
    } else {
        switch (charState_) {
        case CharState::Idle: targetAnim = "idle"; break;
        case CharState::Walk: targetAnim = "walk"; break;
        case CharState::Run: targetAnim = isSprinting_ ? "fast_run" : "run"; break;
        case CharState::Jump: targetAnim = "jump"; break;
        case CharState::Fall: targetAnim = "jump_down"; break;
        case CharState::Land: targetAnim = "landing"; break;
        case CharState::Crouch:
        case CharState::CrouchIdle: targetAnim = "standing_to_crouched"; break;
        case CharState::CrouchWalk: targetAnim = "crouched_walking"; break;
        case CharState::StandUp: targetAnim = "crouch_to_stand"; break;
        case CharState::Attack: targetAnim = "attack"; break;
        case CharState::Preview: targetAnim = ""; break;
        }
    }

    if (charState_ != CharState::Preview && !targetAnim.empty()) {
        // Find target clip (case-insensitive)
        int targetIndex = -1;
        std::string targetLower = targetAnim;
        std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), ::tolower);
        for (size_t i = 0; i < clips_.size(); ++i) {
            std::string clipLower = clips_[i].name;
            std::transform(clipLower.begin(), clipLower.end(), clipLower.begin(), ::tolower);
            if (clipLower == targetLower) { targetIndex = static_cast<int>(i); break; }
        }

        if (targetIndex >= 0 && targetIndex != currentClipIndex_) {
            previousClipIndex_ = currentClipIndex_;
            previousAnimTime_ = animTime_;
            currentClipIndex_ = targetIndex;
            animTime_ = 0.0f;
            isBlending_ = (previousClipIndex_ >= 0);
            blendFactor_ = 0.0f;
        }
    }

    // Update animation
    auto& skel = characterSkeleton_.skeleton;
    if (currentClipIndex_ >= 0 && currentClipIndex_ < static_cast<int>(clips_.size())) {
        animTime_ += deltaTime;

        bool loop = (charState_ != CharState::Attack &&
                     charState_ != CharState::Jump &&
                     charState_ != CharState::Crouch &&
                     charState_ != CharState::CrouchIdle);
        if (!loop && animTime_ > clips_[currentClipIndex_].duration) {
            animTime_ = clips_[currentClipIndex_].duration;
        }
        if (charState_ == CharState::CrouchIdle) {
            animTime_ = clips_[currentClipIndex_].duration;
        }

        if (isBlending_ && previousClipIndex_ >= 0 && previousClipIndex_ < static_cast<int>(clips_.size())) {
            blendFactor_ += deltaTime / blendDuration_;
            if (blendFactor_ >= 1.0f) {
                blendFactor_ = 1.0f;
                isBlending_ = false;
                animSystem_.updateAnimation(skel, clips_[currentClipIndex_], animTime_, loop);
            } else {
                animSystem_.blendAnimation(skel,
                    clips_[previousClipIndex_], previousAnimTime_, true,
                    clips_[currentClipIndex_], animTime_, loop,
                    blendFactor_);
            }
        } else {
            animSystem_.updateAnimation(skel, clips_[currentClipIndex_], animTime_, loop);
        }
    }

    animSystem_.updateGlobalTransforms(skel);
    updateBoneTransforms();
}

// ============================================================================
// Update kinematic bone positions from animation
// ============================================================================

void VoxelCharacter::updateBoneTransforms() {
    const auto& skel = characterSkeleton_.skeleton;

    for (auto& [boneId, body] : boneBodies_) {
        const auto& bone = skel.bones[boneId];

        glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), worldPosition_);
        modelMatrix = glm::rotate(modelMatrix, currentYaw_, glm::vec3(0, 1, 0));

        // Apply animation rotation offset
        float animRotation = 0.0f;
        if (currentClipIndex_ >= 0 && currentClipIndex_ < static_cast<int>(clips_.size())) {
            auto rotIt = animationRotationOffsets_.find(clips_[currentClipIndex_].name);
            if (rotIt != animationRotationOffsets_.end()) animRotation = rotIt->second;
        }
        if (animRotation != 0.0f) {
            modelMatrix = glm::rotate(modelMatrix, glm::radians(animRotation), glm::vec3(0, 1, 0));
        }

        glm::mat4 finalTransform = modelMatrix * bone.globalTransform;
        finalTransform = glm::translate(finalTransform, boneOffsets_[boneId]);

        glm::vec3 pos = glm::vec3(finalTransform[3]);
        glm::quat rot = glm::quat_cast(finalTransform);

        btTransform trans;
        trans.setOrigin(btVector3(pos.x, pos.y, pos.z));
        trans.setRotation(btQuaternion(rot.x, rot.y, rot.z, rot.w));
        body->getMotionState()->setWorldTransform(trans);
    }
}

// ============================================================================
// Override getParts to expose the right parts depending on mode
// ============================================================================

// Note: In animated mode, this->parts (from RagdollCharacter) has the bone parts.
// In physics mode, PhysicsDriveMode manages its own parts. The RenderCoordinator
// needs to see the right set. We handle this by keeping this->parts populated
// correctly in each mode, OR by overriding getParts if RagdollCharacter has one.
// Since RagdollCharacter::getParts() returns const ref to `parts`, and physics mode
// clears our `parts`, we need the renderer to check PhysicsDriveMode's parts too.
// For now, in physics mode we just redirect getPosition() to PhysicsDriveMode
// and let the caller query physicsDrive_->getParts() separately, OR we keep parts
// in sync (simpler: just reference physicsDrive parts as our own).

} // namespace Scene
} // namespace Phyxel
