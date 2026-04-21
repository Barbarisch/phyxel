#include "scene/active/LocomotionController.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Phyxel {
namespace Scene {

// Convenience: rotation around an axis in radians
static glm::quat rotX(float r) { return glm::angleAxis(r, glm::vec3(1, 0, 0)); }
static glm::quat rotY(float r) { return glm::angleAxis(r, glm::vec3(0, 1, 0)); }
static glm::quat rotZ(float r) { return glm::angleAxis(r, glm::vec3(0, 0, 1)); }

// ============================================================================
// Initialization
// ============================================================================

void LocomotionController::initialize(const CharacterSkeleton& skeleton,
                                       std::vector<LimbStepper>& steppers) {
    steppers_ = &steppers;
    boneMap_  = skeleton.skeleton.boneMap;

    // Set up rest offsets for each limb stepper based on skeleton geometry
    for (auto& s : steppers) {
        // restOffset is the foot's position relative to the body root in local space.
        // We rely on the caller (ActiveCharacter::buildLimbSteppers) to set these up
        // from the skeleton. If they're already set, leave them.
    }

    desiredYaw_   = 0.0f;
    walkPhase_    = 0.0f;
    state_        = LocomotionState::Idle;
    stateTimer_   = 0.0f;
    wasGrounded_  = true;
    prevVertVel_  = 0.0f;
}

// ============================================================================
// State machine
// ============================================================================

void LocomotionController::updateStateMachine(float dt,
                                               const LocomotionInput& input,
                                               bool isGrounded,
                                               bool hasFallen,
                                               float verticalVel) {
    stateTimer_ += dt;
    float inputMag = std::abs(input.forward) + std::abs(input.strafe);
    bool justLanded = (!wasGrounded_ && isGrounded);

    // ---- Ragdoll / get-up recovery ----
    if (state_ == LocomotionState::Ragdoll) {
        if (!hasFallen && isGrounded && stateTimer_ > config_.fallRecoveryTime) {
            state_      = faceDownWhenFell_ ? LocomotionState::GetUpFront
                                            : LocomotionState::GetUpBack;
            stateTimer_ = 0.0f;
        }
        wasGrounded_ = isGrounded;
        prevVertVel_ = verticalVel;
        return;
    }
    if (state_ == LocomotionState::GetUpFront || state_ == LocomotionState::GetUpBack) {
        if (stateTimer_ >= config_.getUpDuration) {
            state_      = LocomotionState::Idle;
            stateTimer_ = 0.0f;
        }
        wasGrounded_ = isGrounded;
        prevVertVel_ = verticalVel;
        return;
    }

    // ---- Fall detection ----
    if (hasFallen) {
        state_      = LocomotionState::Ragdoll;
        stateTimer_ = 0.0f;
        wasGrounded_ = isGrounded;
        prevVertVel_ = verticalVel;
        return;
    }

    // ---- Stumble ----
    if (state_ == LocomotionState::Stumble) {
        if (stateTimer_ >= config_.stumbleDuration) {
            state_      = LocomotionState::Idle;
            stateTimer_ = 0.0f;
        }
        wasGrounded_ = isGrounded;
        prevVertVel_ = verticalVel;
        return;
    }

    // ---- Land ----
    if (state_ == LocomotionState::Land || state_ == LocomotionState::LandHard) {
        float dur = (state_ == LocomotionState::LandHard)
                    ? config_.landHardDuration : config_.landDuration;
        if (stateTimer_ >= dur) {
            state_      = (inputMag > 0.1f) ? LocomotionState::Walk : LocomotionState::Idle;
            stateTimer_ = 0.0f;
        }
        wasGrounded_ = isGrounded;
        prevVertVel_ = verticalVel;
        return;
    }

    // ---- In-air states ----
    if (!isGrounded) {
        if (state_ == LocomotionState::Jump || state_ == LocomotionState::Peak
            || state_ == LocomotionState::Fall) {
            if (verticalVel > -0.5f && verticalVel < 0.5f) {
                state_ = LocomotionState::Peak;
            } else if (verticalVel < config_.fallVelocityThreshold) {
                state_ = LocomotionState::Fall;
            } else if (state_ != LocomotionState::Jump || stateTimer_ > 0.15f) {
                if (verticalVel < 0.0f) state_ = LocomotionState::Fall;
            }
        } else if (state_ != LocomotionState::Jump) {
            // Walked off an edge
            state_      = LocomotionState::Fall;
            stateTimer_ = 0.0f;
        }
        wasGrounded_ = isGrounded;
        prevVertVel_ = verticalVel;
        return;
    }

    // ---- Grounded transitions ----

    // Landing
    if (justLanded) {
        float impact = std::abs(prevVertVel_);
        if (impact >= std::abs(config_.hardLandThreshold)) {
            state_      = LocomotionState::LandHard;
        } else if (state_ == LocomotionState::Fall || state_ == LocomotionState::Jump
                   || state_ == LocomotionState::Peak) {
            state_      = LocomotionState::Land;
        }
        stateTimer_     = 0.0f;
        wasGrounded_    = isGrounded;
        prevVertVel_    = verticalVel;
        return;
    }

    // Jump request
    if (input.jump && isGrounded && state_ != LocomotionState::Crouch) {
        state_      = LocomotionState::Jump;
        stateTimer_ = 0.0f;
        wasGrounded_ = isGrounded;
        prevVertVel_ = verticalVel;
        return;
    }

    // Crouch
    if (input.crouch) {
        if (inputMag > 0.1f) {
            if (state_ != LocomotionState::CrouchWalk) {
                state_      = LocomotionState::CrouchWalk;
                stateTimer_ = 0.0f;
            }
        } else {
            if (state_ != LocomotionState::Crouch) {
                state_      = LocomotionState::Crouch;
                stateTimer_ = 0.0f;
            }
        }
        wasCrouching_ = true;
        wasGrounded_  = isGrounded;
        prevVertVel_  = verticalVel;
        return;
    }

    // StandUp transition from crouch
    if (wasCrouching_ && !input.crouch) {
        state_        = LocomotionState::StandUp;
        stateTimer_   = 0.0f;
        wasCrouching_ = false;
    }
    if (state_ == LocomotionState::StandUp && stateTimer_ > 0.3f) {
        state_      = LocomotionState::Idle;
        stateTimer_ = 0.0f;
    }

    // Movement states
    if (state_ != LocomotionState::StandUp) {
        if (inputMag > 0.1f) {
            if (input.sprint) {
                state_ = LocomotionState::Sprint;
            } else if (input.forward < -0.3f && std::abs(input.strafe) < 0.3f) {
                state_ = LocomotionState::Backpedal;
            } else if (std::abs(input.strafe) > 0.5f && std::abs(input.forward) < 0.3f) {
                state_ = (input.strafe > 0) ? LocomotionState::StrafeRight
                                             : LocomotionState::StrafeLeft;
            } else if (inputMag > 0.7f && !input.sprint) {
                state_ = LocomotionState::Run;
            } else {
                state_ = LocomotionState::Walk;
            }
        } else if (std::abs(input.turn) > 0.5f) {
            state_ = LocomotionState::TurnInPlace;
        } else {
            state_ = LocomotionState::Idle;
        }
    }

    wasGrounded_ = isGrounded;
    prevVertVel_ = verticalVel;
}

// ============================================================================
// Main update
// ============================================================================

LocomotionOutput LocomotionController::update(float dt,
                                               const LocomotionInput& input,
                                               bool isGrounded,
                                               bool hasFallen,
                                               float verticalVelocity,
                                               const glm::vec3& bodyPos,
                                               float bodyYaw,
                                               float walkSpeed,
                                               float runSpeed,
                                               float sprintSpeed) {
    updateStateMachine(dt, input, isGrounded, hasFallen, verticalVelocity);

    // Desired yaw follows input turn rate
    desiredYaw_ += input.turn * config_.turnRate * dt;

    LocomotionOutput out;
    out.state      = state_;
    out.desiredYaw = desiredYaw_;

    generatePose(dt, input, bodyYaw, out, walkSpeed, runSpeed, sprintSpeed);
    updateLimbSteppers(dt, bodyPos, bodyYaw);

    // Fill foot targets from steppers
    if (steppers_) {
        out.footTargets.clear();
        for (const auto& s : *steppers_) {
            out.footTargets.push_back(s.currentFootTarget);
        }
    }

    return out;
}

// ============================================================================
// Pose generation dispatch
// ============================================================================

void LocomotionController::generatePose(float dt, const LocomotionInput& input,
                                         float bodyYaw, LocomotionOutput& out,
                                         float walkSpeed, float runSpeed, float sprintSpeed) {
    // Determine effective speed fraction for walk/run cycle
    float speedFrac = std::max(std::abs(input.forward), std::abs(input.strafe));

    switch (state_) {
    case LocomotionState::Idle:
        walkPhase_ = 0.0f; // Let phase decay to 0 in idle
        generateIdlePose(out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::Walk:
        walkPhase_ += dt * 2.0f * static_cast<float>(M_PI) * 1.5f * speedFrac;
        generateWalkPose(speedFrac, out);
        out.moveDirection = glm::normalize(
            glm::vec3(std::sin(bodyYaw) * input.forward - std::cos(bodyYaw) * input.strafe,
                      0,
                      std::cos(bodyYaw) * input.forward + std::sin(bodyYaw) * input.strafe));
        out.desiredSpeed = walkSpeed * speedFrac;
        break;

    case LocomotionState::Run:
        walkPhase_ += dt * 2.0f * static_cast<float>(M_PI) * 2.5f * speedFrac;
        generateRunPose(speedFrac, out);
        out.moveDirection = glm::normalize(
            glm::vec3(std::sin(bodyYaw) * input.forward - std::cos(bodyYaw) * input.strafe,
                      0,
                      std::cos(bodyYaw) * input.forward + std::sin(bodyYaw) * input.strafe));
        out.desiredSpeed = runSpeed * speedFrac;
        break;

    case LocomotionState::Sprint:
        walkPhase_ += dt * 2.0f * static_cast<float>(M_PI) * 3.5f;
        generateRunPose(1.0f, out);
        out.moveDirection = glm::normalize(glm::vec3(std::sin(bodyYaw), 0, std::cos(bodyYaw)));
        out.desiredSpeed  = sprintSpeed;
        break;

    case LocomotionState::Backpedal:
        walkPhase_ -= dt * 2.0f * static_cast<float>(M_PI) * 1.2f;
        generateWalkPose(-0.5f, out);
        out.moveDirection = -glm::normalize(glm::vec3(std::sin(bodyYaw), 0, std::cos(bodyYaw)));
        out.desiredSpeed  = walkSpeed * config_.backpedalFraction;
        break;

    case LocomotionState::StrafeLeft:
        walkPhase_ += dt * 2.0f * static_cast<float>(M_PI) * 1.3f;
        generateWalkPose(0.6f, out);
        out.moveDirection = glm::normalize(glm::vec3(-std::cos(bodyYaw), 0, std::sin(bodyYaw)));
        out.desiredSpeed  = walkSpeed * config_.strafeFraction;
        break;

    case LocomotionState::StrafeRight:
        walkPhase_ += dt * 2.0f * static_cast<float>(M_PI) * 1.3f;
        generateWalkPose(0.6f, out);
        out.moveDirection = glm::normalize(glm::vec3(std::cos(bodyYaw), 0, -std::sin(bodyYaw)));
        out.desiredSpeed  = walkSpeed * config_.strafeFraction;
        break;

    case LocomotionState::TurnInPlace:
        generateIdlePose(out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::Jump:
        generateJumpPose(8.0f, out); // rising
        out.desiredSpeed = walkSpeed * speedFrac;
        break;

    case LocomotionState::Peak:
        generateJumpPose(0.0f, out);
        out.desiredSpeed = walkSpeed * speedFrac * 0.5f;
        break;

    case LocomotionState::Fall:
        generateFallPose(out);
        out.desiredSpeed = walkSpeed * speedFrac * 0.3f;
        break;

    case LocomotionState::Land:
        generateLandPose(std::min(1.0f, stateTimer_ / config_.landDuration), out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::LandHard:
        generateLandPose(std::min(1.0f, stateTimer_ / config_.landHardDuration), out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::Crouch:
        generateCrouchPose(out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::CrouchWalk:
        walkPhase_ += dt * 2.0f * static_cast<float>(M_PI) * 1.0f;
        generateCrouchWalkPose(out);
        out.moveDirection = glm::normalize(
            glm::vec3(std::sin(bodyYaw) * input.forward, 0, std::cos(bodyYaw) * input.forward));
        out.desiredSpeed = walkSpeed * config_.crouchFraction;
        break;

    case LocomotionState::StandUp:
        generateIdlePose(out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::Stumble:
        generateStumblePose(std::min(1.0f, stateTimer_ / config_.stumbleDuration), out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::Ragdoll:
        generateRagdollPose(out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::GetUpFront:
        generateGetUpPose(std::min(1.0f, stateTimer_ / config_.getUpDuration), true, out);
        out.desiredSpeed = 0.0f;
        break;

    case LocomotionState::GetUpBack:
        generateGetUpPose(std::min(1.0f, stateTimer_ / config_.getUpDuration), false, out);
        out.desiredSpeed = 0.0f;
        break;
    }
}

// ============================================================================
// Limb stepper advancement
// ============================================================================

void LocomotionController::updateLimbSteppers(float dt,
                                               const glm::vec3& bodyPos,
                                               float bodyYaw) {
    if (!steppers_ || steppers_->empty()) return;

    // Advance step animations
    for (auto& s : *steppers_) {
        s.updateStep(dt);
    }

    // Try to trigger new steps (stagger: one limb at a time per pair)
    // Group steppers into pairs (0+1, 2+3, etc.) — left/right of each limb group
    for (size_t i = 0; i < steppers_->size(); i += 2) {
        auto& a = (*steppers_)[i];
        auto& b = (i + 1 < steppers_->size()) ? (*steppers_)[i + 1] : a;

        if (!a.isStepping) a.tryStep(bodyPos, bodyYaw, b.isStepping, nullptr);
        if (!b.isStepping) b.tryStep(bodyPos, bodyYaw, a.isStepping, nullptr);
    }
    // Note: physicsWorld pointer is nullptr here — it gets passed via ActiveCharacter
    // which calls updateLimbSteppers separately with the physicsWorld reference.
}

// ============================================================================
// Pose generators
// ============================================================================

void LocomotionController::generateIdlePose(LocomotionOutput& out) {
    static float idlePhase = 0.0f;
    // Breathing: subtle chest expansion and head sway
    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    float breathe = 0.015f * std::sin(idlePhase);
    emitRot("Chest", rotX(breathe));
    emitRot("Spine", rotX(breathe * 0.5f));
    emitRot("Head",  rotX(-breathe * 0.3f) * rotZ(breathe * 0.2f));

    // Arms hang naturally at sides
    emitRot("LeftArm",  rotZ( 0.15f));
    emitRot("RightArm", rotZ(-0.15f));
    emitRot("LeftForeArm",  rotX(0.08f));
    emitRot("RightForeArm", rotX(0.08f));
}

void LocomotionController::generateWalkPose(float speedFrac, LocomotionOutput& out) {
    float p = walkPhase_;

    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    float swing = 0.40f * speedFrac;
    float knee  = 0.50f * speedFrac;
    float arm   = 0.28f * speedFrac;
    float bob   = 0.03f * speedFrac;

    // Hips: slight lateral tilt with walk bob
    emitRot("Hips", rotZ(bob * std::sin(p * 2.0f)));

    // Legs: opposite phase
    emitRot("LeftUpLeg",  rotX( swing * std::sin(p)));
    emitRot("RightUpLeg", rotX(-swing * std::sin(p)));

    // Knee bend: maximum during forward swing (always positive — knees only bend forward)
    emitRot("LeftLeg",  rotX(knee * std::max(0.0f,  std::sin(p))));
    emitRot("RightLeg", rotX(knee * std::max(0.0f, -std::sin(p))));

    // Foot: flex on plant, extend on push-off (approximate via ankle rotation)
    emitRot("LeftFoot",  rotX(-0.15f * std::sin(p)));
    emitRot("RightFoot", rotX( 0.15f * std::sin(p)));

    // Arms: counter-swing to legs
    emitRot("LeftArm",  rotX(-arm * std::sin(p)) * rotZ(0.12f));
    emitRot("RightArm", rotX( arm * std::sin(p)) * rotZ(-0.12f));
    emitRot("LeftForeArm",  rotX(arm * 0.4f));
    emitRot("RightForeArm", rotX(arm * 0.4f));

    // Spine: slight counter-rotation to hip sway
    emitRot("Spine", rotZ(-bob * 0.5f * std::sin(p * 2.0f)));
    emitRot("Head",  rotY(arm * 0.2f * std::sin(p)));
}

void LocomotionController::generateRunPose(float speedFrac, LocomotionOutput& out) {
    float p = walkPhase_;

    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    float swing = 0.60f * speedFrac;
    float knee  = 0.80f * speedFrac;
    float arm   = 0.50f * speedFrac;
    float lean  = 0.10f * speedFrac; // Forward lean

    emitRot("Hips",  rotX(-lean) * rotZ(0.05f * std::sin(p * 2.0f)));
    emitRot("Spine", rotX(-lean * 0.5f));
    emitRot("Chest", rotX(-lean * 0.3f));

    emitRot("LeftUpLeg",  rotX(swing * std::sin(p)));
    emitRot("RightUpLeg", rotX(-swing * std::sin(p)));
    emitRot("LeftLeg",  rotX(knee * std::max(0.0f,  std::sin(p))));
    emitRot("RightLeg", rotX(knee * std::max(0.0f, -std::sin(p))));
    emitRot("LeftFoot",  rotX(-0.2f * std::sin(p)));
    emitRot("RightFoot", rotX( 0.2f * std::sin(p)));

    emitRot("LeftArm",  rotX(-arm * std::sin(p)) * rotZ(0.08f));
    emitRot("RightArm", rotX( arm * std::sin(p)) * rotZ(-0.08f));
    emitRot("LeftForeArm",  rotX(arm * 0.6f));
    emitRot("RightForeArm", rotX(arm * 0.6f));

    emitRot("Head", rotX(lean * 0.5f));
}

void LocomotionController::generateJumpPose(float vertVel, LocomotionOutput& out) {
    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    bool rising = (vertVel > 0.5f);
    float legExtend = rising ? 0.3f : 0.5f; // Tuck legs during rise, extend for landing
    float armUp     = rising ? 0.2f : 0.0f;

    emitRot("LeftUpLeg",   rotX(-legExtend));
    emitRot("RightUpLeg",  rotX(-legExtend));
    emitRot("LeftLeg",     rotX(legExtend * 0.6f));
    emitRot("RightLeg",    rotX(legExtend * 0.6f));

    emitRot("LeftArm",  rotX(-armUp) * rotZ(0.2f));
    emitRot("RightArm", rotX(-armUp) * rotZ(-0.2f));
    emitRot("LeftForeArm",  rotX(0.2f));
    emitRot("RightForeArm", rotX(0.2f));
    emitRot("Head", rotX(-0.05f));
}

void LocomotionController::generateFallPose(LocomotionOutput& out) {
    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    // Arms out for balance, legs slightly bent
    emitRot("LeftArm",     rotX(-0.2f) * rotZ(0.5f));
    emitRot("RightArm",    rotX(-0.2f) * rotZ(-0.5f));
    emitRot("LeftForeArm", rotX(0.3f));
    emitRot("RightForeArm",rotX(0.3f));

    emitRot("LeftUpLeg",  rotX(-0.3f));
    emitRot("RightUpLeg", rotX(-0.3f));
    emitRot("LeftLeg",    rotX(0.35f));
    emitRot("RightLeg",   rotX(0.35f));
    emitRot("Head",       rotX(-0.1f));
}

void LocomotionController::generateLandPose(float t, LocomotionOutput& out) {
    // t: 0 = impact, 1 = recovered
    // Impact: deep crouch. Recovery: return to idle.
    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    float bend = (1.0f - t) * 0.7f; // crouch depth fades as t→1
    emitRot("Hips",        rotX(-bend * 0.3f));
    emitRot("Spine",       rotX(-bend * 0.2f));
    emitRot("LeftUpLeg",   rotX(bend));
    emitRot("RightUpLeg",  rotX(bend));
    emitRot("LeftLeg",     rotX(bend * 1.2f));
    emitRot("RightLeg",    rotX(bend * 1.2f));

    emitRot("LeftArm",     rotX(-bend * 0.3f) * rotZ(0.15f));
    emitRot("RightArm",    rotX(-bend * 0.3f) * rotZ(-0.15f));
}

void LocomotionController::generateCrouchPose(LocomotionOutput& out) {
    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    emitRot("Hips",       rotX(-0.2f));
    emitRot("Spine",      rotX(-0.15f));
    emitRot("LeftUpLeg",  rotX(0.6f));
    emitRot("RightUpLeg", rotX(0.6f));
    emitRot("LeftLeg",    rotX(0.8f));
    emitRot("RightLeg",   rotX(0.8f));
    emitRot("LeftArm",    rotZ(0.1f));
    emitRot("RightArm",   rotZ(-0.1f));
    emitRot("Head",       rotX(0.1f));
}

void LocomotionController::generateCrouchWalkPose(LocomotionOutput& out) {
    float p = walkPhase_;
    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    float swing = 0.25f;
    emitRot("Hips",       rotX(-0.2f));
    emitRot("Spine",      rotX(-0.15f));
    emitRot("LeftUpLeg",  rotX(0.5f + swing * std::sin(p)));
    emitRot("RightUpLeg", rotX(0.5f - swing * std::sin(p)));
    emitRot("LeftLeg",    rotX(0.6f + swing * std::max(0.0f,  std::sin(p))));
    emitRot("RightLeg",   rotX(0.6f + swing * std::max(0.0f, -std::sin(p))));
}

void LocomotionController::generateStumblePose(float t, LocomotionOutput& out) {
    // t: 0=stumble start, 1=recovering
    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    float wobble = (1.0f - t) * 0.4f * std::sin(t * static_cast<float>(M_PI) * 4.0f);
    emitRot("Hips",        rotZ(wobble));
    emitRot("Spine",       rotZ(-wobble * 0.7f));
    emitRot("LeftArm",     rotX(-0.3f) * rotZ(0.4f));
    emitRot("RightArm",    rotX(-0.3f) * rotZ(-0.4f));
    emitRot("LeftForeArm", rotX(0.3f));
    emitRot("RightForeArm",rotX(0.3f));
}

void LocomotionController::generateRagdollPose(LocomotionOutput& out) {
    // Empty pose — ActiveCharacter sets the ragdoll to limp when this state is active
    out.localTargetPose.clear();
}

void LocomotionController::generateGetUpPose(float t, bool faceDown,
                                              LocomotionOutput& out) {
    // t: 0=lying, 1=standing
    auto emitRot = [&](const std::string& bone, const glm::quat& q) {
        int id = findBone(bone);
        if (id >= 0) out.localTargetPose[id] = q;
    };

    if (faceDown) {
        // Push up: arms extend, hips rise, knees tuck
        float pushAngle = (1.0f - t) * 0.8f;
        emitRot("Hips",       rotX(-pushAngle));
        emitRot("Spine",      rotX(-pushAngle * 0.5f));
        emitRot("LeftUpLeg",  rotX(pushAngle));
        emitRot("RightUpLeg", rotX(pushAngle));
        emitRot("LeftLeg",    rotX(pushAngle * 0.8f));
        emitRot("RightLeg",   rotX(pushAngle * 0.8f));
        emitRot("LeftArm",    rotX(-0.5f * (1.0f - t)) * rotZ(0.2f));
        emitRot("RightArm",   rotX(-0.5f * (1.0f - t)) * rotZ(-0.2f));
    } else {
        // Roll and sit up: simpler lerp toward standing
        float sitAngle = (1.0f - t) * 0.9f;
        emitRot("Hips",       rotX(-sitAngle * 0.5f));
        emitRot("Spine",      rotX(-sitAngle));
        emitRot("LeftUpLeg",  rotX(sitAngle * 0.7f));
        emitRot("RightUpLeg", rotX(sitAngle * 0.7f));
        emitRot("LeftLeg",    rotX(sitAngle * 0.5f));
        emitRot("RightLeg",   rotX(sitAngle * 0.5f));
    }
}

// ============================================================================
// Helpers
// ============================================================================

int LocomotionController::findBone(const std::string& name) const {
    auto it = boneMap_.find(name);
    return (it != boneMap_.end()) ? it->second : -1;
}

void LocomotionController::onLanded(float impactVelocity) {
    impactVelocity_ = impactVelocity;
}

} // namespace Scene
} // namespace Phyxel
