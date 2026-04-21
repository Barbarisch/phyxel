#include "scene/active/ActiveCharacter.h"
#include "graphics/AnimationSystem.h"
#include "input/InputManager.h"
#include "graphics/Camera.h"
#include "utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Phyxel {
namespace Scene {

// ============================================================================
// Construction / Destruction
// ============================================================================

ActiveCharacter::ActiveCharacter(Physics::PhysicsWorld* physicsWorld,
                                  const glm::vec3& position)
    : physicsWorld_(physicsWorld),
      kinematic_(physicsWorld),
      health_(std::make_unique<Core::HealthComponent>(100.0f)),
      spawnPosition_(position) {
    this->position = position;
    pelvisSmooth_  = position + glm::vec3(0, poseConfig_.pelvisHeight, 0);
    kinematic_.setPosition(position);
}

ActiveCharacter::~ActiveCharacter() = default;

// ============================================================================
// Loading
// ============================================================================

bool ActiveCharacter::loadFromAnimFile(const std::string& path) {
    AnimationSystem animSys;
    std::vector<AnimationClip> clips;
    if (!animSys.loadFromFile(path, skeleton_.skeleton, clips, skeleton_.voxelModel)) {
        LOG_ERROR("ActiveCharacter", "Failed to load anim file: " + path);
        return false;
    }
    skeleton_.computeBoneSizes();
    skeleton_.generateJointDefs();
    buildFromSkeleton();
    LOG_INFO("ActiveCharacter", "Loaded from " + path);
    return true;
}

bool ActiveCharacter::loadProcedural(const HumanoidParams& params) {
    skeleton_ = ProceduralSkeleton::humanoid(params);
    buildFromSkeleton();
    LOG_INFO("ActiveCharacter", "Built procedural humanoid");
    return true;
}

bool ActiveCharacter::loadProcedural(const QuadrupedParams& params) {
    skeleton_ = ProceduralSkeleton::quadruped(params);
    buildFromSkeleton();
    LOG_INFO("ActiveCharacter", "Built procedural quadruped");
    return true;
}

bool ActiveCharacter::loadProcedural(const ArachnidParams& params) {
    skeleton_ = ProceduralSkeleton::arachnid(params);
    buildFromSkeleton();
    LOG_INFO("ActiveCharacter", "Built procedural arachnid");
    return true;
}

// ============================================================================
// Build skeleton → kinematic + limbs
// ============================================================================

void ActiveCharacter::buildFromSkeleton() {
    kinematic_.setPosition(spawnPosition_);

    buildLimbSteppers();  // derives pelvisHeight from skeleton
    pelvisSmooth_ = spawnPosition_ + glm::vec3(0, poseConfig_.pelvisHeight, 0);

    cachePoseBonesIds();
    locomotion_.initialize(skeleton_, limbs_);

    for (auto& s : limbs_) {
        s.initialize(spawnPosition_, bodyYaw_, physicsWorld_);
        s.setIgnoreBody(kinematic_.getBody());
    }

    modelLoaded_ = true;
}

// ============================================================================
// Identify leg chains and build LimbSteppers
// ============================================================================

void ActiveCharacter::buildLimbSteppers() {
    limbs_.clear();
    const auto& boneMap = skeleton_.skeleton.boneMap;
    const auto& bones   = skeleton_.skeleton.bones;

    auto findBone = [&](const std::string& name) -> int {
        auto it = boneMap.find(name);
        if (it != boneMap.end()) return it->second;
        // Try with mixamorig: prefix (common Mixamo skeleton naming)
        it = boneMap.find("mixamorig:" + name);
        if (it != boneMap.end()) return it->second;
        return -1;
    };

    // ---- Identify leg chains by skeleton bone names ----
    // We only need bone names to find the chain; ALL spatial data
    // (offsets, lengths, pole vectors) comes from the skeleton itself.
    struct LegChain { std::string upper, lower, foot, side; };
    std::vector<LegChain> legChains;

    if (findBone("LeftUpLeg") >= 0) {
        // Humanoid biped (Mixamo-style naming)
        legChains = {
            {"LeftUpLeg",  "LeftLeg",  "LeftFoot",  "left"},
            {"RightUpLeg", "RightLeg", "RightFoot", "right"},
        };
    } else if (findBone("FrontLeftUpLeg") >= 0) {
        // Quadruped
        legChains = {
            {"FrontLeftUpLeg",  "FrontLeftLeg",  "", "fl"},
            {"FrontRightUpLeg", "FrontRightLeg", "", "fr"},
            {"RearLeftUpLeg",   "RearLeftLeg",   "", "rl"},
            {"RearRightUpLeg",  "RearRightLeg",  "", "rr"},
        };
    } else {
        // Generic fallback: search for Upper*Leg pattern
        for (const auto& [name, id] : boneMap) {
            if (name.find("Upper") != std::string::npos && name.find("Leg") != std::string::npos) {
                std::string lowerName = name.substr(0, name.size() - 5) + "Lower";
                legChains.push_back({name, lowerName, "", name});
            }
        }
    }

    float maxLegReach = 0.0f;

    for (const auto& chain : legChains) {
        int upperId = findBone(chain.upper);
        int lowerId = findBone(chain.lower);
        if (upperId < 0 || lowerId < 0) continue;
        int footId = chain.foot.empty() ? -1 : findBone(chain.foot);

        LimbStepper stepper;
        stepper.name        = chain.side;
        stepper.upperBoneId = upperId;
        stepper.lowerBoneId = lowerId;
        stepper.footBoneId  = footId;

        // ---- Bone lengths from skeleton joint-to-joint distances ----
        if (lowerId < static_cast<int>(bones.size())) {
            float dist = glm::length(bones[lowerId].localPosition);
            if (dist > 0.01f) stepper.upperLength = dist;
        }
        if (footId >= 0 && footId < static_cast<int>(bones.size())) {
            float dist = glm::length(bones[footId].localPosition);
            if (dist > 0.01f) stepper.lowerLength = dist;
        } else if (stepper.upperLength > 0.05f) {
            stepper.lowerLength = stepper.upperLength; // estimate
        }
        if (stepper.upperLength < 0.05f) stepper.upperLength = 0.4f;
        if (stepper.lowerLength < 0.05f) stepper.lowerLength = 0.4f;

        // ---- Rest offset: derived from the hip bone's position in the skeleton ----
        // The upper leg bone's localPosition (relative to its parent, typically Hips)
        // already encodes the lateral and vertical offset of the hip socket. 
        // We use X and Z directly from the skeleton, and Y = -(total leg reach).
        glm::vec3 hipLocal = (upperId < static_cast<int>(bones.size()))
                           ? bones[upperId].localPosition
                           : glm::vec3(0.0f);
        stepper.restOffset = glm::vec3(
            hipLocal.x,                                      // lateral: from skeleton
            -(stepper.upperLength + stepper.lowerLength),    // vertical: full leg reach
            hipLocal.z                                       // fore/aft: from skeleton
        );

        // ---- Pole offset: derived from the knee bone's bind-pose position ----
        // The lower leg bone's localPosition points from hip→knee. The component
        // that is NOT along the main chain axis tells us which way the knee bends.
        // For a humanoid with legs pointing down (-Y), the Z component of the
        // knee direction reveals the forward/backward bend.
        glm::vec3 kneeDir = (lowerId < static_cast<int>(bones.size()))
                          ? bones[lowerId].localPosition
                          : glm::vec3(0, -1, 0);
        if (glm::length(kneeDir) > 0.001f) {
            kneeDir = glm::normalize(kneeDir);
            // Remove the main chain direction (typically -Y for legs pointing down)
            // to isolate the bend direction
            glm::vec3 bendHint = kneeDir - glm::vec3(0, kneeDir.y, 0); // strip vertical component
            if (glm::length(bendHint) > 0.001f) {
                stepper.poleOffset = glm::normalize(bendHint);
            } else {
                // Knee is perfectly inline — default to +Z (forward)
                stepper.poleOffset = glm::vec3(0, 0, 1);
            }
        } else {
            stepper.poleOffset = glm::vec3(0, 0, 1);
        }

        // ---- Local bone axis from skeleton ----
        if (lowerId < static_cast<int>(bones.size())) {
            glm::vec3 childPos = bones[lowerId].localPosition;
            if (glm::length(childPos) > 0.001f)
                stepper.localBoneAxis = glm::normalize(childPos);
        }

        float legReach = stepper.upperLength + stepper.lowerLength;
        if (legReach > maxLegReach) maxLegReach = legReach;

        limbs_.push_back(stepper);
    }

    // ---- pelvisHeight: derived from skeleton root bone ----
    // The root bone (typically Hips) localPosition.y gives the bind-pose
    // pelvis height above the ground plane.
    for (const auto& b : bones) {
        if (b.parentId < 0 && b.localPosition.y > 0.1f) {
            poseConfig_.pelvisHeight = b.localPosition.y;
            break;
        }
    }

    // ---- Ground clearance per leg: ankle height above ground in bind pose ----
    // = pelvisHeight + hipSocket.y_offset - legReach
    for (auto& s : limbs_) {
        glm::vec3 hipLocal = (s.upperBoneId < static_cast<int>(bones.size()))
                           ? bones[s.upperBoneId].localPosition
                           : glm::vec3(0.0f);
        float legReach = s.upperLength + s.lowerLength;
        s.groundClearance = std::max(0.0f,
            poseConfig_.pelvisHeight + hipLocal.y - legReach);
    }

    LOG_INFO_FMT("ActiveCharacter", "Built " << limbs_.size()
        << " limb steppers, pelvisHeight=" << poseConfig_.pelvisHeight
        << " (leg reach=" << maxLegReach
        << ", groundClearance=" << (limbs_.empty() ? 0.0f : limbs_[0].groundClearance) << ")");
}

// ============================================================================
// Cache bone IDs for procedural joints
// ============================================================================

void ActiveCharacter::cachePoseBonesIds() {
    const auto& boneMap = skeleton_.skeleton.boneMap;

    auto findCI = [&](const std::string& key) -> int {
        // Case-insensitive search, also strips common prefixes (e.g., mixamorig:)
        std::string kl = key;
        std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
        for (const auto& [name, id] : boneMap) {
            std::string nl = name;
            std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
            if (nl == kl) return id;
            // Strip prefix (e.g., "mixamorig:spine" → "spine")
            auto colon = nl.find(':');
            if (colon != std::string::npos && nl.substr(colon + 1) == kl)
                return id;
        }
        return -1;
    };

    // Try common Mixamo-style bone names
    boneIdSpine_ = findCI("Spine");
    if (boneIdSpine_ < 0) boneIdSpine_ = findCI("spine");

    boneIdChest_ = findCI("Spine1");
    if (boneIdChest_ < 0) boneIdChest_ = findCI("Spine2");
    if (boneIdChest_ < 0) boneIdChest_ = findCI("Chest");

    boneIdLArm_ = findCI("LeftArm");
    if (boneIdLArm_ < 0) boneIdLArm_ = findCI("Left_arm");

    boneIdRArm_ = findCI("RightArm");
    if (boneIdRArm_ < 0) boneIdRArm_ = findCI("Right_arm");

    boneIdLFore_ = findCI("LeftForeArm");
    if (boneIdLFore_ < 0) boneIdLFore_ = findCI("Left_forearm");

    boneIdRFore_ = findCI("RightForeArm");
    if (boneIdRFore_ < 0) boneIdRFore_ = findCI("Right_forearm");
}

// ============================================================================
// Input
// ============================================================================

void ActiveCharacter::attachInput(Input::InputManager* input, Graphics::Camera* camera) {
    inputManager_ = input;
    camera_       = camera;
}

void ActiveCharacter::setMoveInput(float forward, float strafe, float turn) {
    currentInput_.forward = forward;
    currentInput_.strafe  = strafe;
    currentInput_.turn    = turn;
}
void ActiveCharacter::setJumpInput(bool jump)     { currentInput_.jump   = jump;   }
void ActiveCharacter::setCrouchInput(bool crouch) { currentInput_.crouch = crouch; }
void ActiveCharacter::setSprintInput(bool sprint) { currentInput_.sprint = sprint; }

void ActiveCharacter::processPlayerInput(float /*dt*/) {
    if (!inputManager_ || !controlled_) return;

    currentInput_.forward = 0.0f;
    currentInput_.strafe  = 0.0f;
    if (inputManager_->isKeyPressed(GLFW_KEY_W)) currentInput_.forward += 1.0f;
    if (inputManager_->isKeyPressed(GLFW_KEY_S)) currentInput_.forward -= 1.0f;
    if (inputManager_->isKeyPressed(GLFW_KEY_A)) currentInput_.strafe  -= 1.0f;
    if (inputManager_->isKeyPressed(GLFW_KEY_D)) currentInput_.strafe  += 1.0f;
    currentInput_.turn   = 0.0f; // camera drives yaw for player
    currentInput_.jump   = inputManager_->isKeyPressed(GLFW_KEY_SPACE);
    currentInput_.crouch = inputManager_->isKeyPressed(GLFW_KEY_LEFT_CONTROL);
    currentInput_.sprint = inputManager_->isKeyPressed(GLFW_KEY_LEFT_SHIFT);
}

// ============================================================================
// Entity::update
// ============================================================================

void ActiveCharacter::update(float deltaTime) {
    if (!modelLoaded_) return;

    processPlayerInput(deltaTime);

    // Yaw: player follows camera; AI uses turn input
    if (controlled_ && camera_) {
        glm::vec3 front = camera_->getFront();
        front.y = 0.0f;
        if (glm::length2(front) > 0.0001f) {
            front    = glm::normalize(front);
            bodyYaw_ = std::atan2(front.x, front.z);
        }
    } else {
        bodyYaw_ += currentInput_.turn * locomotion_.config().turnRate * deltaTime;
    }

    bool  isGrounded = kinematic_.isGrounded();
    float vertVel    = kinematic_.getVerticalVelocity();
    glm::vec3 bodyPos = kinematic_.getPosition();

    if (!prevGrounded_ && isGrounded) {
        locomotion_.onLanded(prevVertVel_);

        // Trigger landing squash visual
        float impact = std::abs(prevVertVel_);
        float hardThresh = std::abs(locomotion_.config().hardLandThreshold);
        landImpact_ = glm::clamp(impact / hardThresh, 0.0f, 1.0f);
        landTimer_  = poseConfig_.landSquashDuration;
    }

    // Tick landing timer
    if (landTimer_ > 0.0f)
        landTimer_ = std::max(0.0f, landTimer_ - deltaTime);

    // Update step parameters from locomotion state
    LocomotionState locoState = locomotion_.getState();
    bool isRunning = (locoState == LocomotionState::Sprint ||
                      locoState == LocomotionState::Run);
    for (auto& s : limbs_) {
        s.stepHeight    = isRunning ? poseConfig_.stepHeightRun   : poseConfig_.stepHeightWalk;
        s.stepDuration  = isRunning ? poseConfig_.stepDurationRun : poseConfig_.stepDurationWalk;
        s.stepThreshold = poseConfig_.stepThreshold;
    }
    // Staggered stepping: only one foot moves at a time per pair
    for (size_t i = 0; i < limbs_.size(); i += 2) {
        auto& a = limbs_[i];
        auto& b = (i + 1 < limbs_.size()) ? limbs_[i + 1] : a;
        if (!a.isStepping) a.tryStep(bodyPos, bodyYaw_, b.isStepping, physicsWorld_);
        if (&b != &a && !b.isStepping) b.tryStep(bodyPos, bodyYaw_, a.isStepping, physicsWorld_);
    }
    for (auto& s : limbs_) s.updateStep(deltaTime);

    // LocomotionController is the single speed authority
    LocomotionOutput locoOut = locomotion_.update(
        deltaTime, currentInput_,
        isGrounded, false, vertVel,
        bodyPos, bodyYaw_,
        poseConfig_.walkSpeed, poseConfig_.runSpeed, poseConfig_.sprintSpeed);

    // Drive kinematic capsule from LocomotionController's desired speed
    bool anyMove = (std::abs(currentInput_.forward) > 0.01f || std::abs(currentInput_.strafe) > 0.01f);
    float targetSpeed = anyMove ? locoOut.desiredSpeed : 0.0f;

    glm::vec2 fwdXZ(  std::sin(bodyYaw_), std::cos(bodyYaw_));
    glm::vec2 rightXZ( std::cos(bodyYaw_), -std::sin(bodyYaw_));
    glm::vec2 moveVel = fwdXZ * currentInput_.forward + rightXZ * currentInput_.strafe;
    if (glm::length2(moveVel) > 0.0001f)
        moveVel = glm::normalize(moveVel) * targetSpeed;

    if (currentInput_.jump) kinematic_.requestJump();
    kinematic_.setMoveVelocity(moveVel);

    float walkPhase    = locomotion_.getWalkPhase();
    glm::vec3 vel      = kinematic_.getVelocity();
    float actualSpeedXZ = glm::length(glm::vec2(vel.x, vel.z));
    float speedFraction = (poseConfig_.walkSpeed > 0.001f)
                          ? actualSpeedXZ / poseConfig_.walkSpeed : 0.0f;

    updatePose(deltaTime, walkPhase, speedFraction, isGrounded, vertVel);
    rebuildShapes();

    this->position = bodyPos;
    prevGrounded_  = isGrounded;
    prevVertVel_   = vertVel;
}

// ============================================================================
// Procedural pose
// ============================================================================

void ActiveCharacter::updatePose(float dt, float walkPhase, float speedFraction,
                                  bool grounded, float vertVel) {
    const auto& bones = skeleton_.skeleton.bones;
    if (bones.empty()) return;

    LocomotionState locoState = locomotion_.getState();
    bool isCrouching = (locoState == LocomotionState::Crouch ||
                        locoState == LocomotionState::CrouchWalk);

    // ------------------------------------------------------------------
    // 1. Pelvis spring: follow capsule feet position + pelvisHeight
    // ------------------------------------------------------------------
    glm::vec3 feetPos  = kinematic_.getPosition();
    float effectivePelvisHeight = poseConfig_.pelvisHeight;
    if (isCrouching)
        effectivePelvisHeight *= poseConfig_.crouchPelvisRatio;

    float pelvisTarget = feetPos.y + effectivePelvisHeight;

    // Landing squash: fast-attack (first 30%) + slow-release (last 70%)
    if (landTimer_ > 0.0f && poseConfig_.landSquashDuration > 0.001f) {
        float t = 1.0f - (landTimer_ / poseConfig_.landSquashDuration); // 0→1 over duration
        float envelope;
        if (t < 0.3f)
            envelope = t / 0.3f;          // attack: 0→1
        else
            envelope = 1.0f - ((t - 0.3f) / 0.7f); // release: 1→0
        pelvisTarget -= poseConfig_.landSquashDepth * landImpact_ * envelope;
    }

    float springK      = grounded ? poseConfig_.pelvisSpring : 15.0f; // faster in air
    pelvisSmooth_.x    = feetPos.x;
    pelvisSmooth_.z    = feetPos.z;
    pelvisSmooth_.y   += (pelvisTarget - pelvisSmooth_.y) * std::min(1.0f, dt * springK);

    // ------------------------------------------------------------------
    // 2. Acceleration → lean angles + velocity-based strafe lean
    // ------------------------------------------------------------------
    glm::vec3 velocity = kinematic_.getVelocity();
    glm::vec3 accel    = (dt > 0.001f) ? (velocity - prevVelocity_) / dt : glm::vec3(0.0f);
    prevVelocity_      = velocity;

    // Project acceleration into character-local space (rotate by -bodyYaw_)
    float cy = std::cos(-bodyYaw_), sy = std::sin(-bodyYaw_);
    float aFwd  =  accel.x * sy + accel.z * cy;
    float aSide =  accel.x * cy - accel.z * sy;

    float targetLeanFwd  = glm::clamp(aFwd  * poseConfig_.leanMultiplier,
                                       -poseConfig_.maxLeanDeg, poseConfig_.maxLeanDeg);
    float targetLeanSide = glm::clamp(aSide * poseConfig_.leanMultiplier * 0.5f,
                                       -poseConfig_.maxLeanDeg * 0.5f,
                                        poseConfig_.maxLeanDeg * 0.5f);

    // Velocity-based strafe lean: persists during sustained lateral movement
    float vSide = velocity.x * cy - velocity.z * sy;  // local lateral velocity
    float lateralFrac = (poseConfig_.walkSpeed > 0.001f)
                        ? glm::clamp(vSide / poseConfig_.walkSpeed, -1.0f, 1.0f)
                        : 0.0f;
    targetLeanSide += lateralFrac * poseConfig_.strafeLeanDeg;
    targetLeanSide  = glm::clamp(targetLeanSide,
                                  -poseConfig_.maxLeanDeg, poseConfig_.maxLeanDeg);

    float lerpT      = 1.0f - std::pow(poseConfig_.leanSmoothing, dt * 60.0f);
    smoothLeanFwd_  += (targetLeanFwd  - smoothLeanFwd_)  * lerpT;
    smoothLeanSide_ += (targetLeanSide - smoothLeanSide_) * lerpT;

    // ------------------------------------------------------------------
    // 3. Hip bob (vertical) and hip roll (lateral tilt)
    // ------------------------------------------------------------------
    float speedBlend = glm::clamp(speedFraction - 1.0f, 0.0f, 1.0f); // 0 at walk, 1 at 2× walk
    bool  isMoving   = (glm::length(glm::vec2(velocity.x, velocity.z)) > 0.1f);
    float bobAmp     = isMoving ? poseConfig_.hipBobAmplitude
                                  * (1.0f + speedBlend * (poseConfig_.hipBobRunMult - 1.0f))
                                : 0.0f;
    float bobY       = bobAmp * std::sin(walkPhase * 2.0f);   // two bobs per stride
    float rollRad    = isMoving ? glm::radians(poseConfig_.hipRollDeg) * std::sin(walkPhase)
                                : 0.0f;

    // ------------------------------------------------------------------
    // 4. Build root (pelvis) world transform
    // ------------------------------------------------------------------
    float leanFwdRad  = glm::radians(smoothLeanFwd_);
    float leanSideRad = glm::radians(smoothLeanSide_);

    // Crouch adds a forward hip tilt
    float crouchTiltRad = isCrouching ? glm::radians(poseConfig_.crouchHipTiltDeg) : 0.0f;

    glm::mat4 rootMat = glm::translate(glm::mat4(1.0f),
                                        pelvisSmooth_ + glm::vec3(0, bobY, 0));
    rootMat = glm::rotate(rootMat, bodyYaw_,    glm::vec3(0, 1, 0));
    rootMat = glm::rotate(rootMat, leanFwdRad + crouchTiltRad, glm::vec3(1, 0, 0));
    rootMat = glm::rotate(rootMat, leanSideRad, glm::vec3(0, 0, 1));
    rootMat = glm::rotate(rootMat, rollRad,      glm::vec3(0, 0, 1));

    // ------------------------------------------------------------------
    // 5. Arm swing amplitude
    // ------------------------------------------------------------------
    float armSwingRad = glm::radians(poseConfig_.armSwingDeg
                        * (1.0f + speedBlend * (poseConfig_.armSwingRunMult - 1.0f)));
    float foreBendRad = glm::radians(poseConfig_.forearmBendDeg);

    // ------------------------------------------------------------------
    // 6. FK pass: parent-first traversal
    // ------------------------------------------------------------------
    for (const auto& bone : bones) {
        if (bone.parentId < 0) {
            // Root bone: world transform = rootMat (ignore localPosition for root)
            boneWorldTransforms_[bone.id] = rootMat;
            continue;
        }

        glm::mat4 parentMat = glm::mat4(1.0f);
        if (auto it = boneWorldTransforms_.find(bone.parentId); it != boneWorldTransforms_.end())
            parentMat = it->second;

        // Start from bind-pose local transform
        glm::mat4 localMat = glm::translate(glm::mat4(1.0f), bone.localPosition)
                             * glm::mat4_cast(bone.localRotation);

        // Procedural overrides for specific bone roles
        int id = bone.id;

        if (id == boneIdSpine_) {
            // Spine counter-rotates lean so upper body stays more upright
            float counterRad = -(leanFwdRad + crouchTiltRad) * poseConfig_.spineCounterFrac;
            localMat = localMat * glm::rotate(glm::mat4(1.0f), counterRad, glm::vec3(1, 0, 0));
        } else if (id == boneIdChest_) {
            float counterRad = -(leanFwdRad + crouchTiltRad) * poseConfig_.chestCounterFrac;
            localMat = localMat * glm::rotate(glm::mat4(1.0f), counterRad, glm::vec3(1, 0, 0));
        } else if (id == boneIdLArm_) {
            // Base rest angle brings arm from T-pose to hanging at side
            float restRad = glm::radians(poseConfig_.armRestAngleDeg);
            // Left arm swings opposite phase to right leg (= same phase as left leg → walkPhase + π)
            float swing = armSwingRad * std::sin(walkPhase + static_cast<float>(M_PI));
            localMat = localMat * glm::rotate(glm::mat4(1.0f), restRad + swing, glm::vec3(1, 0, 0));
        } else if (id == boneIdRArm_) {
            float restRad = glm::radians(poseConfig_.armRestAngleDeg);
            // Right arm swings opposite phase to left leg (= same phase as right leg → walkPhase)
            float swing = armSwingRad * std::sin(walkPhase);
            localMat = localMat * glm::rotate(glm::mat4(1.0f), restRad + swing, glm::vec3(1, 0, 0));
        } else if (id == boneIdLFore_ || id == boneIdRFore_) {
            // Elbow bend: rotate around local X (same axis as arm swing)
            localMat = localMat * glm::rotate(glm::mat4(1.0f), foreBendRad, glm::vec3(1, 0, 0));
        }

        boneWorldTransforms_[bone.id] = parentMat * localMat;
    }

    // ------------------------------------------------------------------
    // 7. IK override for leg chains (FK-correction approach)
    //
    // The IK solver produces world-space rotations using a -Y bone axis
    // convention, but the skeleton's bind-pose uses arbitrary local
    // rotations. Applying IK rotations directly would discard the
    // bind-pose frame, causing shape offsets to scatter.
    //
    // Instead, we extract the IK-desired bone DIRECTION, compute a
    // correction quaternion that rotates the FK direction to match,
    // and apply that correction on top of the FK transform. This
    // preserves the bind-pose coordinate frame that shape offsets
    // are defined in.
    // ------------------------------------------------------------------
    std::unordered_set<int> ikBoneIds;
    for (auto& s : limbs_) {
        if (s.upperBoneId < 0 || s.lowerBoneId < 0) continue;

        // Get hip world position from FK result
        auto hipIt = boneWorldTransforms_.find(s.upperBoneId);
        if (hipIt == boneWorldTransforms_.end()) continue;
        glm::vec3 hipPos = glm::vec3(hipIt->second[3]);

        // Pole vector in world space (controls knee/elbow bend direction)
        glm::mat4 yawMat = glm::rotate(glm::mat4(1.0f), bodyYaw_, glm::vec3(0, 1, 0));
        glm::vec3 poleWorld = glm::vec3(yawMat * glm::vec4(s.poleOffset, 0.0f));

        s.solveIK(hipPos, poleWorld);

        // Extract IK desired bone directions (solver uses -Y internally)
        glm::vec3 ikUpperDir = glm::normalize(
            glm::vec3(glm::mat4_cast(s.ikResult.upperRotation) * glm::vec4(0, -1, 0, 0)));
        glm::vec3 kneePos = hipPos + ikUpperDir * s.upperLength;

        glm::vec3 ikLowerDir = glm::normalize(
            glm::vec3(glm::mat4_cast(s.ikResult.lowerRotation) * glm::vec4(0, -1, 0, 0)));

        // --- Upper bone: correct FK rotation toward IK direction ---
        glm::quat fkUpperRot = glm::quat_cast(boneWorldTransforms_[s.upperBoneId]);
        glm::vec3 fkUpperDir = glm::normalize(fkUpperRot * s.localBoneAxis);

        // Correction quaternion: rotate fkDir → ikDir
        float dotU = glm::dot(fkUpperDir, ikUpperDir);
        glm::quat upperCorrection;
        if (dotU > 0.99999f) {
            upperCorrection = glm::quat(1, 0, 0, 0);
        } else if (dotU < -0.99999f) {
            glm::vec3 perp = (std::abs(fkUpperDir.x) < 0.9f)
                ? glm::normalize(glm::cross(fkUpperDir, glm::vec3(1, 0, 0)))
                : glm::normalize(glm::cross(fkUpperDir, glm::vec3(0, 1, 0)));
            upperCorrection = glm::angleAxis(glm::pi<float>(), perp);
        } else {
            glm::vec3 axis = glm::cross(fkUpperDir, ikUpperDir);
            float s2 = std::sqrt((1.0f + dotU) * 2.0f);
            upperCorrection = glm::quat(s2 * 0.5f, axis / s2);
        }

        glm::quat correctedUpperRot = upperCorrection * fkUpperRot;
        glm::mat4 upperMat = glm::mat4_cast(correctedUpperRot);
        upperMat[3] = glm::vec4(hipPos, 1.0f);
        boneWorldTransforms_[s.upperBoneId] = upperMat;

        // --- Lower bone: correct FK rotation toward IK direction ---
        // First re-propagate lower bone FK from corrected upper parent
        {
            const auto& lowerBone = bones[s.lowerBoneId];
            glm::mat4 localMat = glm::translate(glm::mat4(1.0f), lowerBone.localPosition)
                                 * glm::mat4_cast(lowerBone.localRotation);
            boneWorldTransforms_[s.lowerBoneId] = upperMat * localMat;
        }

        glm::quat fkLowerRot = glm::quat_cast(boneWorldTransforms_[s.lowerBoneId]);
        glm::vec3 fkLowerDir = glm::normalize(fkLowerRot * s.localBoneAxis);

        float dotL = glm::dot(fkLowerDir, ikLowerDir);
        glm::quat lowerCorrection;
        if (dotL > 0.99999f) {
            lowerCorrection = glm::quat(1, 0, 0, 0);
        } else if (dotL < -0.99999f) {
            glm::vec3 perp = (std::abs(fkLowerDir.x) < 0.9f)
                ? glm::normalize(glm::cross(fkLowerDir, glm::vec3(1, 0, 0)))
                : glm::normalize(glm::cross(fkLowerDir, glm::vec3(0, 1, 0)));
            lowerCorrection = glm::angleAxis(glm::pi<float>(), perp);
        } else {
            glm::vec3 axis = glm::cross(fkLowerDir, ikLowerDir);
            float s2 = std::sqrt((1.0f + dotL) * 2.0f);
            lowerCorrection = glm::quat(s2 * 0.5f, axis / s2);
        }

        glm::quat correctedLowerRot = lowerCorrection * fkLowerRot;
        glm::mat4 lowerMat = glm::mat4_cast(correctedLowerRot);
        lowerMat[3] = glm::vec4(kneePos, 1.0f);
        boneWorldTransforms_[s.lowerBoneId] = lowerMat;

        ikBoneIds.insert(s.upperBoneId);
        ikBoneIds.insert(s.lowerBoneId);
    }

    // ------------------------------------------------------------------
    // 8. Re-propagate FK for children of IK-overridden bones
    //    (foot, toebase, toe_end chains that depend on updated parents)
    // ------------------------------------------------------------------
    std::unordered_set<int> needsUpdate = ikBoneIds;
    for (const auto& bone : bones) {
        if (bone.parentId < 0) continue;
        if (needsUpdate.count(bone.parentId) == 0) continue;
        if (ikBoneIds.count(bone.id) > 0) continue;  // Don't override IK results

        auto parentIt = boneWorldTransforms_.find(bone.parentId);
        if (parentIt == boneWorldTransforms_.end()) continue;

        glm::mat4 localMat = glm::translate(glm::mat4(1.0f), bone.localPosition)
                             * glm::mat4_cast(bone.localRotation);
        boneWorldTransforms_[bone.id] = parentIt->second * localMat;
        needsUpdate.insert(bone.id);  // Propagate to grandchildren
    }
}

// ============================================================================
// Rebuild visual shapes from bone world transforms
// ============================================================================

void ActiveCharacter::rebuildShapes() {
    shapes_.clear();
    const auto& appearance = skeleton_.appearance;
    const auto& bones = skeleton_.skeleton.bones;

    // ---- Model voxels (toggleable) ----
    if (debugVis_.showVoxels) {
        for (const auto& boneShape : skeleton_.voxelModel.shapes) {
            auto it = boneWorldTransforms_.find(boneShape.boneId);
            if (it == boneWorldTransforms_.end()) continue;

            glm::mat4 shapeMat = it->second
                                 * glm::translate(glm::mat4(1.0f), boneShape.offset);

            glm::vec4 color = appearance.defaultColor;
            if (boneShape.boneId >= 0 && boneShape.boneId < static_cast<int>(bones.size()))
                color = appearance.getColorForBone(bones[boneShape.boneId].name);

            shapes_.push_back({shapeMat, boneShape.size, color});
        }

        // Fallback: if voxelModel has no shapes, draw one box per bone using boneSizes
        if (shapes_.empty()) {
            for (const auto& bone : bones) {
                auto it = boneWorldTransforms_.find(bone.id);
                if (it == boneWorldTransforms_.end()) continue;

                glm::vec3 size{0.1f, 0.2f, 0.1f};
                if (auto sz = skeleton_.boneSizes.find(bone.id); sz != skeleton_.boneSizes.end())
                    size = sz->second;

                glm::vec4 color = appearance.getColorForBone(bone.name);
                shapes_.push_back({it->second, size, color});
            }
        }
    }

    // ---- Bone segments (elongated boxes from parent→child) + joint dots ----
    if (debugVis_.showBones) {
        float jt = debugVis_.boneBoxSize;         // joint dot size
        float thk = debugVis_.boneBoxSize * 0.6f; // bone segment cross-section

        // Build child→parent lookup for coloring leaves differently
        // Draw a segment from each bone's world pos toward every child bone
        // Also draw a small joint dot at each bone origin

        for (const auto& bone : bones) {
            auto it = boneWorldTransforms_.find(bone.id);
            if (it == boneWorldTransforms_.end()) continue;

            glm::vec3 bonePos = glm::vec3(it->second[3]);

            // Joint dot (yellow cube at this bone's origin)
            glm::mat4 dotMat = glm::translate(glm::mat4(1.0f), bonePos);
            shapes_.push_back({dotMat, {jt, jt, jt}, {1.0f, 1.0f, 0.0f, 1.0f}});

            // Segment to parent (if parent exists)
            if (bone.parentId >= 0) {
                auto pit = boneWorldTransforms_.find(bone.parentId);
                if (pit == boneWorldTransforms_.end()) continue;

                glm::vec3 parentPos = glm::vec3(pit->second[3]);
                glm::vec3 dir = bonePos - parentPos;
                float len = glm::length(dir);
                if (len < 0.001f) continue;

                // Build a transform: translate to midpoint, rotate to align Y with dir
                glm::vec3 midpoint = (parentPos + bonePos) * 0.5f;
                glm::vec3 up = dir / len; // normalized direction

                // Compute rotation from default Y-up to the bone direction
                glm::vec3 defaultUp{0, 1, 0};
                glm::quat rot;
                float dot = glm::dot(defaultUp, up);
                if (dot > 0.9999f) {
                    rot = glm::quat(1, 0, 0, 0);
                } else if (dot < -0.9999f) {
                    rot = glm::angleAxis(glm::pi<float>(), glm::vec3(1, 0, 0));
                } else {
                    glm::vec3 axis = glm::normalize(glm::cross(defaultUp, up));
                    rot = glm::angleAxis(std::acos(glm::clamp(dot, -1.0f, 1.0f)), axis);
                }

                glm::mat4 segMat = glm::translate(glm::mat4(1.0f), midpoint)
                                   * glm::mat4_cast(rot);

                // Size: thin in X/Z, length along Y
                glm::vec3 segSize{thk, len, thk};

                // Color by bone depth: spine=orange, limbs=cyan, extremities=white
                glm::vec4 segColor{0.9f, 0.6f, 0.1f, 1.0f}; // default orange
                const std::string& name = bone.name;
                if (name.find("Leg") != std::string::npos || name.find("Foot") != std::string::npos ||
                    name.find("Toe") != std::string::npos || name.find("UpLeg") != std::string::npos)
                    segColor = {0.0f, 0.85f, 0.85f, 1.0f}; // cyan for legs
                else if (name.find("Arm") != std::string::npos || name.find("Hand") != std::string::npos ||
                         name.find("ForeArm") != std::string::npos)
                    segColor = {0.2f, 0.7f, 1.0f, 1.0f}; // blue for arms
                else if (name.find("Spine") != std::string::npos || name.find("Hips") != std::string::npos ||
                         name.find("Neck") != std::string::npos || name.find("Head") != std::string::npos)
                    segColor = {0.9f, 0.6f, 0.1f, 1.0f}; // orange for spine

                shapes_.push_back({segMat, segSize, segColor});
            }
        }
    }

    // ---- Bone local axes (R=X, G=Y, B=Z thin boxes) ----
    if (debugVis_.showBoneAxes) {
        float len = debugVis_.axisLength;
        float thk = debugVis_.axisThickness;

        // Axis indicators: elongated thin box along each local axis
        struct AxisInfo { glm::vec3 offset; glm::vec3 size; glm::vec4 color; };
        AxisInfo axes[3] = {
            {{len * 0.5f, 0, 0}, {len, thk, thk}, {1, 0, 0, 1}},  // X = red
            {{0, len * 0.5f, 0}, {thk, len, thk}, {0, 1, 0, 1}},  // Y = green
            {{0, 0, len * 0.5f}, {thk, thk, len}, {0, 0, 1, 1}},  // Z = blue
        };

        for (const auto& bone : bones) {
            auto it = boneWorldTransforms_.find(bone.id);
            if (it == boneWorldTransforms_.end()) continue;
            const glm::mat4& boneWT = it->second;

            for (const auto& ax : axes) {
                glm::mat4 axisMat = boneWT * glm::translate(glm::mat4(1.0f), ax.offset);
                shapes_.push_back({axisMat, ax.size, ax.color});
            }
        }
    }

    // ---- IK target indicators (foot positions as cyan spheres, step arcs as magenta) ----
    if (debugVis_.showIKTargets) {
        float ts = debugVis_.boneBoxSize * 1.5f;
        glm::vec3 targetSize{ts, ts, ts};

        for (const auto& limb : limbs_) {
            // Current foot position (cyan)
            glm::mat4 footMat = glm::translate(glm::mat4(1.0f), limb.currentFootTarget);
            shapes_.push_back({footMat, targetSize, {0, 1, 1, 1}});

            // Planted foot position (green)
            glm::mat4 plantedMat = glm::translate(glm::mat4(1.0f), limb.footWorldPos);
            shapes_.push_back({plantedMat, targetSize * 0.8f, {0, 0.8f, 0, 1}});

            // Step target (magenta - only while stepping)
            if (limb.isStepping) {
                glm::mat4 stepToMat = glm::translate(glm::mat4(1.0f), limb.stepTo);
                shapes_.push_back({stepToMat, targetSize, {1, 0, 1, 1}});
            }
        }
    }

    // ---- Pole vector indicators (yellow line from knee in pole direction) ----
    if (debugVis_.showPoleVectors) {
        float pvLen = debugVis_.poleVecLength;
        float pvThk = debugVis_.axisThickness;
        glm::mat4 yawMat = glm::rotate(glm::mat4(1.0f), bodyYaw_, glm::vec3(0, 1, 0));

        for (const auto& limb : limbs_) {
            // Find upper bone (hip) and lower bone (knee) positions
            auto upperIt = boneWorldTransforms_.find(limb.upperBoneId);
            auto lowerIt = boneWorldTransforms_.find(limb.lowerBoneId);
            if (upperIt == boneWorldTransforms_.end() || lowerIt == boneWorldTransforms_.end())
                continue;

            glm::vec3 kneePos = glm::vec3(lowerIt->second[3]);

            // Pole vector in world space (same as used in IK solve)
            glm::vec3 poleWorld = glm::normalize(
                glm::vec3(yawMat * glm::vec4(limb.poleOffset, 0.0f)));

            // Draw a line from knee in pole direction (yellow)
            glm::vec3 poleEnd = kneePos + poleWorld * pvLen;
            glm::vec3 midpoint = (kneePos + poleEnd) * 0.5f;

            // Build oriented box along the pole direction
            glm::vec3 dir = poleWorld;
            glm::vec3 up = (std::abs(dir.y) < 0.9f)
                ? glm::vec3(0, 1, 0)
                : glm::vec3(1, 0, 0);
            glm::vec3 right = glm::normalize(glm::cross(dir, up));
            up = glm::normalize(glm::cross(right, dir));

            glm::mat4 poleMat(1.0f);
            poleMat[0] = glm::vec4(right, 0.0f);
            poleMat[1] = glm::vec4(dir, 0.0f);
            poleMat[2] = glm::vec4(up, 0.0f);
            poleMat[3] = glm::vec4(midpoint, 1.0f);
            shapes_.push_back({poleMat, {pvThk, pvLen, pvThk}, {1.0f, 1.0f, 0.0f, 1.0f}});

            // Arrowhead at tip (small cube)
            glm::mat4 tipMat = glm::translate(glm::mat4(1.0f), poleEnd);
            shapes_.push_back({tipMat, {pvThk * 2, pvThk * 2, pvThk * 2}, {1.0f, 0.5f, 0.0f, 1.0f}});
        }
    }
}

// ============================================================================
// Entity interface
// ============================================================================

void ActiveCharacter::render(Graphics::RenderCoordinator* /*renderer*/) {
    // Rendering is handled by RenderCoordinator::renderEntities() via getShapes().
}

void ActiveCharacter::setPosition(const glm::vec3& pos) {
    kinematic_.setPosition(pos);
    pelvisSmooth_ = pos + glm::vec3(0, poseConfig_.pelvisHeight, 0);
    this->position = pos;
}

glm::vec3 ActiveCharacter::getPosition() const {
    return kinematic_.getPosition();
}

} // namespace Scene
} // namespace Phyxel
