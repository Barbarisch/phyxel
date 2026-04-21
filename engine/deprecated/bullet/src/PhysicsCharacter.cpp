#include "scene/PhysicsCharacter.h"
#include "graphics/RenderCoordinator.h"
#include "graphics/RaycastVisualizer.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Phyxel {
namespace Scene {

void CharacterAction::updateAction(btCollisionWorld* collisionWorld, btScalar deltaTimeStep) {
    if (character) {
        character->prePhysicsStep(deltaTimeStep);
    }
}

PhysicsCharacter::PhysicsCharacter(Physics::PhysicsWorld* physicsWorld, Input::InputManager* inputManager, Graphics::Camera* camera, const glm::vec3& startPos)
    : RagdollCharacter(physicsWorld, startPos), inputManager(inputManager), camera(camera) {
    createRagdoll(startPos);
    
    // Register physics action
    physicsAction = new CharacterAction(this);
    physicsWorld->getWorld()->addAction(physicsAction);
}

PhysicsCharacter::~PhysicsCharacter() {
    if (physicsAction) {
        physicsWorld->getWorld()->removeAction(physicsAction);
        delete physicsAction;
        physicsAction = nullptr;
    }
    // Base class handles cleanup of parts and constraints
}

void PhysicsCharacter::createRagdoll(const glm::vec3& startPos) {
    // Define scales (using Subcube scale approx 0.33f)
    float subScale = 1.0f / 3.0f;
    glm::vec3 torsoScale(subScale, subScale * 1.5f, subScale); // Taller torso
    // Split limb scale into upper and lower
    glm::vec3 upperLimbScale(subScale * 0.5f, subScale * 0.7f, subScale * 0.5f);
    glm::vec3 lowerLimbScale(subScale * 0.4f, subScale * 0.7f, subScale * 0.4f);

    // Create Torso
    btRigidBody* torso = physicsWorld->createCube(startPos, torsoScale, 10.0f);
    torso->setUserPointer((void*)1); // Mark as character part (prevent auto-cleanup)
    torso->setActivationState(DISABLE_DEACTIVATION);
    torso->setAngularFactor(btVector3(0, 1, 0)); // Limit rotation to Y axis for now (easier balance)
    torso->setDamping(0.5f, 0.5f); // Add damping to reduce sliding
    parts.push_back({torso, torsoScale, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), "Torso"});
    torsoIndex = 0;

    // Create Head
    glm::vec3 headScale(subScale * 0.8f); // Revert to cube
    glm::vec3 headPos = startPos + glm::vec3(0, torsoScale.y + headScale.y + 0.05f, 0);
    btRigidBody* head = physicsWorld->createCube(headPos, headScale, 2.0f);
    head->setUserPointer((void*)1); // Mark as character part
    parts.push_back({head, headScale, glm::vec4(1.0f, 0.8f, 0.6f, 1.0f), "Head"});
    headIndex = 1;

    // Create Nose (to visualize facing direction)
    glm::vec3 noseScale(subScale * 0.2f, subScale * 0.2f, subScale * 0.2f);
    // Position nose in front of head (Z+)
    glm::vec3 nosePos = headPos + glm::vec3(0, 0, headScale.z * 0.5f + noseScale.z * 0.5f);
    btRigidBody* nose = physicsWorld->createCube(nosePos, noseScale, 0.1f);
    nose->setUserPointer((void*)1);
    parts.push_back({nose, noseScale, glm::vec4(1.0f, 0.5f, 0.5f, 1.0f), "Nose"});
    
    // Attach Nose to Head (Fixed)
    btTransform noseA, noseB;
    noseA.setIdentity(); noseB.setIdentity();
    noseA.setOrigin(btVector3(0, 0, headScale.z * 0.5f + noseScale.z * 0.5f));
    noseB.setOrigin(btVector3(0, 0, 0));
    btFixedConstraint* noseJoint = new btFixedConstraint(*head, *nose, noseA, noseB);
    physicsWorld->addConstraint(noseJoint, true);
    constraints.push_back(noseJoint);

    // Neck Joint (ConeTwist for full head motion)
    btTransform localA, localB;
    localA.setIdentity(); localB.setIdentity();
    
    // Rotate frames so X axis points along Y axis (Up)
    // ConeTwist uses X axis as the cone axis. We want the neck to point Up.
    btQuaternion rotXtoY;
    rotXtoY.setRotation(btVector3(0,0,1), 1.5708f); // 90 degrees around Z
    localA.setRotation(rotXtoY);
    localB.setRotation(rotXtoY);

    localA.setOrigin(btVector3(0, torsoScale.y * 0.5f + 0.025f, 0));
    localB.setOrigin(btVector3(0, -headScale.y * 0.5f - 0.025f, 0));
    
    btConeTwistConstraint* neck = new btConeTwistConstraint(*torso, *head, localA, localB);
    // Limits: setLimit(swingSpan1, swingSpan2, twistSpan)
    // Local X is Up (Yaw axis)
    // Local Z is World Z (Roll axis) -> Swing1
    // Local Y is World -X (Pitch axis) -> Swing2
    
    // We want:
    // Yaw (Twist): +/- 90 deg (1.6 rad)
    // Pitch (Swing1 - along Z): +/- 60 deg (1.0 rad)
    // Roll (Swing2 - along Y): +/- 20 deg (0.35 rad)
    
    neck->setLimit(1.0f, 0.35f, 1.6f); 
    physicsWorld->addConstraint(neck, true);
    constraints.push_back(neck);
    neckIndex = (int)(constraints.size() - 1);

    // Define rotZtoX for hinges (Hips, Knees, Elbows)
    btQuaternion rotZtoX;
    rotZtoX.setRotation(btVector3(0,1,0), 1.5708f);

    // Helper to create limbs
    auto createLimb = [&](const glm::vec3& pos, const glm::vec3& scale, const std::string& name, float mass) -> int {
        btRigidBody* limb = physicsWorld->createCube(pos, scale, mass);
        limb->setUserPointer((void*)1); // Mark as character part
        limb->setFriction(1.5f); // High friction for grip
        parts.push_back({limb, scale, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), name});
        return (int)(parts.size() - 1);
    };

    // --- LEGS ---
    float legOffsetY = -torsoScale.y * 0.5f - upperLimbScale.y * 0.5f - 0.05f;
    float legOffsetX = torsoScale.x * 0.5f;
    
    // Thighs
    leftLegIndex = createLimb(startPos + glm::vec3(-legOffsetX, legOffsetY, 0), upperLimbScale, "LeftThigh", 5.0f);
    rightLegIndex = createLimb(startPos + glm::vec3(legOffsetX, legOffsetY, 0), upperLimbScale, "RightThigh", 5.0f);

    // Shins
    float shinOffsetY = legOffsetY - upperLimbScale.y * 0.5f - lowerLimbScale.y * 0.5f - 0.05f;
    leftShinIndex = createLimb(startPos + glm::vec3(-legOffsetX, shinOffsetY, 0), lowerLimbScale, "LeftShin", 3.0f);
    rightShinIndex = createLimb(startPos + glm::vec3(legOffsetX, shinOffsetY, 0), lowerLimbScale, "RightShin", 3.0f);

    // Hip Joints (Hinge)
    auto createHip = [&](int legIdx, float xDir) -> int {
        localA.setIdentity(); localB.setIdentity();
        localA.setRotation(rotZtoX);
        localB.setRotation(rotZtoX);

        localA.setOrigin(btVector3(xDir * legOffsetX, -torsoScale.y * 0.5f, 0));
        localB.setOrigin(btVector3(0, upperLimbScale.y * 0.5f, 0));
        
        btHingeConstraint* hip = new btHingeConstraint(*torso, *parts[legIdx].rigidBody, localA, localB);
        hip->setLimit(-0.8f, 0.8f);
        physicsWorld->addConstraint(hip, true);
        constraints.push_back(hip);
        return (int)(constraints.size() - 1);
    };

    leftHipIndex = createHip(leftLegIndex, -1.0f);
    rightHipIndex = createHip(rightLegIndex, 1.0f);

    // Knee Joints (Hinge)
    auto createKnee = [&](int thighIdx, int shinIdx) -> int {
        localA.setIdentity(); localB.setIdentity();
        localA.setRotation(rotZtoX);
        localB.setRotation(rotZtoX);

        localA.setOrigin(btVector3(0, -upperLimbScale.y * 0.5f, 0));
        localB.setOrigin(btVector3(0, lowerLimbScale.y * 0.5f, 0));

        btHingeConstraint* knee = new btHingeConstraint(*parts[thighIdx].rigidBody, *parts[shinIdx].rigidBody, localA, localB);
        // Flip limits: allow bending backwards (positive angle)
        // If it was bending the wrong way, we might need negative angles or just flip the range
        // Previous: 0.0 to 1.5. If that was "wrong", maybe we need -1.5 to 0.0?
        // Let's try -1.5 to 0.0 for backward bending if positive was forward.
        // Wait, standard hinge: positive is usually "forward" depending on axis.
        // Let's invert the limits to reverse the allowed bend direction.
        knee->setLimit(-1.5f, 0.0f); 
        physicsWorld->addConstraint(knee, true);
        constraints.push_back(knee);
        return (int)(constraints.size() - 1);
    };

    leftKneeIndex = createKnee(leftLegIndex, leftShinIndex);
    rightKneeIndex = createKnee(rightLegIndex, rightShinIndex);

    // --- ARMS ---
    float armOffsetY = torsoScale.y * 0.3f;
    float armOffsetX = torsoScale.x * 0.5f + upperLimbScale.x * 0.5f + 0.05f;

    // Upper Arms
    leftArmIndex = createLimb(startPos + glm::vec3(-armOffsetX, armOffsetY, 0), upperLimbScale, "LeftUpperArm", 2.0f);
    rightArmIndex = createLimb(startPos + glm::vec3(armOffsetX, armOffsetY, 0), upperLimbScale, "RightUpperArm", 2.0f);

    // Forearms
    float forearmOffsetY = armOffsetY - upperLimbScale.y * 0.5f - lowerLimbScale.y * 0.5f - 0.05f;
    leftForearmIndex = createLimb(startPos + glm::vec3(-armOffsetX, forearmOffsetY, 0), lowerLimbScale, "LeftForearm", 1.5f);
    rightForearmIndex = createLimb(startPos + glm::vec3(armOffsetX, forearmOffsetY, 0), lowerLimbScale, "RightForearm", 1.5f);

    // Shoulder Joints (ConeTwist)
    auto createShoulder = [&](int armIdx, float xDir) {
        localA.setIdentity(); localB.setIdentity();
        localA.setOrigin(btVector3(xDir * (torsoScale.x * 0.5f), armOffsetY, 0));
        localB.setOrigin(btVector3(-xDir * (upperLimbScale.x * 0.5f), upperLimbScale.y * 0.4f, 0)); 
        btConeTwistConstraint* shoulder = new btConeTwistConstraint(*torso, *parts[armIdx].rigidBody, localA, localB);
        shoulder->setLimit(0.5f, 0.5f, 0);
        physicsWorld->addConstraint(shoulder, true);
        constraints.push_back(shoulder);
    };

    createShoulder(leftArmIndex, -1.0f);
    createShoulder(rightArmIndex, 1.0f);

    // Elbow Joints (Hinge)
    auto createElbow = [&](int upperIdx, int lowerIdx) -> int {
        localA.setIdentity(); localB.setIdentity();
        localA.setRotation(rotZtoX);
        localB.setRotation(rotZtoX);

        localA.setOrigin(btVector3(0, -upperLimbScale.y * 0.5f, 0));
        localB.setOrigin(btVector3(0, lowerLimbScale.y * 0.5f, 0));

        btHingeConstraint* elbow = new btHingeConstraint(*parts[upperIdx].rigidBody, *parts[lowerIdx].rigidBody, localA, localB);
        // Flip limits: allow bending forward (positive angle)
        // Previous: -1.5 to 0.0. If that was "wrong", let's try 0.0 to 1.5.
        elbow->setLimit(0.0f, 1.5f); 
        physicsWorld->addConstraint(elbow, true);
        constraints.push_back(elbow);
        return (int)(constraints.size() - 1);
    };

    leftElbowIndex = createElbow(leftArmIndex, leftForearmIndex);
    rightElbowIndex = createElbow(rightArmIndex, rightForearmIndex);
}

void PhysicsCharacter::update(float deltaTime) {
    // Only handle high-level logic here (input, state)
    // Physics forces are now applied in prePhysicsStep via CharacterAction
    
    checkGroundStatus();

    if (isControlActive) {
        processInput(deltaTime);
    } else {
        isMoving = false;
    }
}

void PhysicsCharacter::prePhysicsStep(float deltaTime) {
    // This is called BEFORE every internal physics step
    // Apply forces/torques here for stable simulation
    keepUpright(deltaTime);
    updateHead(deltaTime);
    updateWalkCycle(deltaTime);
}

void PhysicsCharacter::checkGroundStatus() {
    // Simple raycast from torso down
    if (torsoIndex == -1) return;
    
    btRigidBody* body = parts[torsoIndex].rigidBody;
    btTransform trans;
    body->getMotionState()->getWorldTransform(trans);
    btVector3 from = trans.getOrigin();
    btVector3 to = from - btVector3(0, 2.0f, 0); // Increased ray length to 2.0 units to ensure hit

    btCollisionWorld::ClosestRayResultCallback rayCallback(from, to);
    physicsWorld->getWorld()->rayTest(from, to, rayCallback);

    if (rayCallback.hasHit()) {
        isGrounded = true;
    } else {
        isGrounded = false;
    }
    
    // Update state based on ground status
    if (!isGrounded) {
        if (body->getLinearVelocity().y() > 0) {
            state = CharacterState::Jumping;
        } else {
            state = CharacterState::Falling;
        }
    } else if (isMoving) {
        state = CharacterState::Walking;
    } else {
        state = CharacterState::Idle;
    }
}

void PhysicsCharacter::processInput(float deltaTime) {
    if (!inputManager || !camera) return;

    // Handle Camera Rotation
    if (inputManager->isMouseCaptured()) {
        glm::vec2 mouseDelta = inputManager->getMouseDelta();
        camera->processMouseMovement(mouseDelta.x, mouseDelta.y);
    }

    // Movement
    glm::vec3 direction(0.0f);
    if (inputManager->isKeyPressed(GLFW_KEY_W)) direction += camera->getFront();
    if (inputManager->isKeyPressed(GLFW_KEY_S)) direction -= camera->getFront();
    if (inputManager->isKeyPressed(GLFW_KEY_A)) direction -= camera->getRight();
    if (inputManager->isKeyPressed(GLFW_KEY_D)) direction += camera->getRight();

    // Flatten direction to XZ plane
    direction.y = 0;
    if (glm::length(direction) > 0.1f) {
        direction = glm::normalize(direction);
        move(direction);
    } else {
        isMoving = false;
    }

    // Jump
    if (jumpCooldown > 0.0f) jumpCooldown -= deltaTime;
    if (inputManager->isKeyPressed(GLFW_KEY_SPACE) && jumpCooldown <= 0.0f && isGrounded) {
        jump();
        jumpCooldown = 1.0f;
    }

    // Camera follow
    glm::vec3 pos = getPosition();
    // Simple third person follow
    glm::vec3 cameraOffset = -camera->getFront() * 5.0f + glm::vec3(0, 2, 0);
    // We don't set camera position directly here to avoid fighting with other systems, 
    // but we could if we are the active controller.
    // For now, let's just update the camera target if we are active.
    // Camera update moved to updateCamera() to be called after physics step
}

void PhysicsCharacter::updateCamera() {
    if (!camera || !isControlActive) return;
    
    glm::vec3 pos = getPosition();
    camera->updatePositionFromTarget(pos, 1.5f);
}

void PhysicsCharacter::setLookTarget(const glm::vec3& target) {
    lookTarget = target;
    hasLookTarget = true;
}

void PhysicsCharacter::clearLookTarget() {
    hasLookTarget = false;
}

void PhysicsCharacter::updateHead(float deltaTime) {
    if (headIndex == -1 || !camera) return;

    btRigidBody* headBody = parts[headIndex].rigidBody;
    
    // Get current head position
    btTransform trans;
    headBody->getMotionState()->getWorldTransform(trans);
    btVector3 headPos = trans.getOrigin();

    // Determine target direction
    btVector3 targetDir;
    if (hasLookTarget) {
        // Look at specific point
        targetDir = btVector3(lookTarget.x, lookTarget.y, lookTarget.z) - headPos;
    } else {
        // Fallback: Look in camera direction
        glm::vec3 camFront = camera->getFront();
        targetDir = btVector3(camFront.x, camFront.y, camFront.z);
    }
    
    if (targetDir.length2() > 0.0001f) {
        targetDir.normalize();
    } else {
        return; // Zero length vector
    }

    // Assuming Z is forward for the head box
    // We want to align the head's Z axis with targetDir
    // And keep the head's Y axis roughly Up (0,1,0)
    
    // Calculate desired rotation
    btVector3 forward = targetDir;
    btVector3 up(0, 1, 0);
    btVector3 right = up.cross(forward);
    if (right.length2() < 0.0001f) {
        // Gimbal lock case (looking straight up/down)
        right = btVector3(1, 0, 0);
    } else {
        right.normalize();
    }
    up = forward.cross(right).normalized();
    
    // Create basis matrix
    btMatrix3x3 basis(
        right.x(), up.x(), forward.x(),
        right.y(), up.y(), forward.y(),
        right.z(), up.z(), forward.z()
    );
    
    btQuaternion targetRot;
    basis.getRotation(targetRot);
    
    // "Lock" the head by setting angular velocity to reach target in one step
    // This is a kinematic-style control on a dynamic body
    btQuaternion currentRot = trans.getRotation();
    
    // Calculate difference quaternion
    btQuaternion diff = targetRot * currentRot.inverse();
    
    // Convert to axis-angle
    btVector3 axis = diff.getAxis();
    float angle = diff.getAngle();
    
    // Normalize angle to -PI to PI
    if (angle > SIMD_PI) angle -= SIMD_2_PI;
    
    // Apply angular velocity to close the gap instantly (damped slightly for stability)
    // 60.0f is roughly 1 frame at 60fps. Using a high value to "lock" it.
    float timeStep = deltaTime > 0.0f ? deltaTime : 0.016f;
    btVector3 desiredAngVel = axis * (angle / timeStep) * 0.5f; // 0.5 damping factor to prevent explosion
    
    // Limit max velocity to prevent physics explosions
    if (desiredAngVel.length() > 20.0f) desiredAngVel = desiredAngVel.normalized() * 20.0f;
    
    headBody->activate();
    headBody->setAngularVelocity(desiredAngVel);
    
    // Also apply a small torque to help maintain it against constraints
    // headBody->applyTorque(desiredAngVel * 10.0f);
}

void PhysicsCharacter::keepUpright(float deltaTime) {
    if (torsoIndex == -1) return;

    btRigidBody* body = parts[torsoIndex].rigidBody;
    btTransform trans;
    body->getMotionState()->getWorldTransform(trans);
    
    // Calculate upright force (PID controller)
    btVector3 up(0, 1, 0);
    btVector3 currentUp = trans.getBasis().getColumn(1);
    
    btVector3 axis = currentUp.cross(up);
    float angle = currentUp.angle(up);
    
    // Apply torque to correct orientation
    if (angle > 0.01f) {
        // Update integral error (clamped to prevent windup)
        integralError += axis * angle * deltaTime;
        if (integralError.length() > 1.0f) {
            integralError = integralError.normalized();
        }

        // PID formula: Kp*error + Ki*integral + Kd*derivative (derivative is -angularVelocity)
        btVector3 torque = axis * (angle * kp) 
                         + integralError * ki 
                         - body->getAngularVelocity() * kd;
        
        body->activate();
        body->applyTorque(torque);
    } else {
        // Decay integral error when upright
        integralError *= 0.95f;
    }
}

void PhysicsCharacter::updateWalkCycle(float deltaTime) {
    if (!isMoving || !isGrounded) {
        // Reset to standing pose
        walkCycleTime = 0.0f;
        
        // Target 0 angle for hips (standing straight)
        auto resetJoint = [&](int constraintIdx, float targetAngle = 0.0f) {
            if (constraintIdx >= 0 && constraintIdx < constraints.size()) {
                btHingeConstraint* hinge = static_cast<btHingeConstraint*>(constraints[constraintIdx]);
                hinge->enableAngularMotor(true, (targetAngle - hinge->getHingeAngle()) * 5.0f, 100.0f);
            }
        };
        resetJoint(leftHipIndex);
        resetJoint(rightHipIndex);
        resetJoint(leftKneeIndex);
        resetJoint(rightKneeIndex);
        resetJoint(leftElbowIndex, -0.5f); // Slight bend in elbows
        resetJoint(rightElbowIndex, -0.5f);

        // Apply braking force when not moving but grounded
        if (isGrounded && torsoIndex != -1) {
            btRigidBody* body = parts[torsoIndex].rigidBody;
            btVector3 vel = body->getLinearVelocity();
            // Apply counter-force to stop sliding
            body->applyCentralForce(-vel * 50.0f);
        }
        return;
    }

    walkCycleTime += deltaTime * walkSpeed;

    // Improved walk cycle with phases
    // Phase 0-PI: Left leg forward, Right leg back
    // Phase PI-2PI: Right leg forward, Left leg back
    
    float hipAmplitude = 0.8f;
    float leftLegTarget = sin(walkCycleTime) * hipAmplitude;
    float rightLegTarget = sin(walkCycleTime + 3.14159f) * hipAmplitude;

    // Knee animation: Bend when leg is moving forward (swing phase)
    float kneeAmplitude = 1.0f;
    // Max bend when leg is passing through center moving forward.
    // Since we flipped the limits to negative (-1.5 to 0), we need negative targets for bending.
    float leftKneeTarget = -std::max(0.0f, sin(walkCycleTime - 1.0f) * kneeAmplitude); 
    float rightKneeTarget = -std::max(0.0f, sin(walkCycleTime + 3.14159f - 1.0f) * kneeAmplitude);

    // Drive motors
    auto driveJoint = [&](int constraintIdx, float targetAngle, float strength) {
        if (constraintIdx >= 0 && constraintIdx < constraints.size()) {
            btHingeConstraint* hinge = static_cast<btHingeConstraint*>(constraints[constraintIdx]);
            // Target velocity is proportional to error (P-controller for velocity)
            float error = targetAngle - hinge->getHingeAngle();
            float targetVel = error * 10.0f; // Gain
            hinge->enableAngularMotor(true, targetVel, strength);
        }
    };

    driveJoint(leftHipIndex, leftLegTarget, 200.0f);
    driveJoint(rightHipIndex, rightLegTarget, 200.0f);
    driveJoint(leftKneeIndex, leftKneeTarget, 150.0f);
    driveJoint(rightKneeIndex, rightKneeTarget, 150.0f);
}

void PhysicsCharacter::move(const glm::vec3& direction) {
    if (glm::length(direction) > 0.1f) {
        isMoving = true;
        moveDirection = glm::normalize(direction);
        
        // Apply force to torso to move
        if (torsoIndex != -1) {
            btRigidBody* body = parts[torsoIndex].rigidBody;
            
            // Apply movement force relative to mass to be consistent
            float moveForce = 40.0f * body->getInvMass(); // Scale force by mass (approx)
            // Actually getInvMass is 1/mass, so we want force * mass. 
            // But let's just tune the constant. 30.0f was weak.
            btVector3 force(moveDirection.x * 50.0f, 0, moveDirection.z * 50.0f);
            
            body->activate();
            body->applyCentralForce(force);
            
            // Rotate torso to face movement direction using Torque
            float targetYaw = atan2(moveDirection.x, moveDirection.z);
            btQuaternion targetRot;
            targetRot.setRotation(btVector3(0, 1, 0), targetYaw);
            
            btTransform trans;
            body->getMotionState()->getWorldTransform(trans);
            btQuaternion currentRot = trans.getRotation();
            
            // Calculate angle difference
            // Simple approach: look at the angle between forward vectors
            btVector3 currentForward = trans.getBasis().getColumn(2); // Z is forward? Check basis.
            // Assuming Z is forward in local space.
            // Actually let's just use the angle difference around Y.
            
            // Get current yaw
            btVector3 forward = trans.getBasis().getColumn(2);
            float currentYaw = atan2(forward.x(), forward.z());
            
            float yawDiff = targetYaw - currentYaw;
            // Normalize angle to -PI to PI
            while (yawDiff > 3.14159f) yawDiff -= 6.28318f;
            while (yawDiff < -3.14159f) yawDiff += 6.28318f;
            
            // Apply torque to turn
            float turnStrength = 20.0f;
            float turnDamping = 5.0f;
            body->applyTorque(btVector3(0, yawDiff * turnStrength - body->getAngularVelocity().y() * turnDamping, 0));
        }
    } else {
        isMoving = false;
    }
}

void PhysicsCharacter::jump() {
    if (torsoIndex != -1) {
        btRigidBody* body = parts[torsoIndex].rigidBody;
        body->activate();
        body->applyCentralImpulse(btVector3(0, jumpForce * 10.0f, 0));
    }
}

void PhysicsCharacter::reset(const glm::vec3& pos) {
    setPosition(pos);
    integralError = btVector3(0,0,0);
    
    // Reset velocities
    for (auto& part : parts) {
        if (part.rigidBody) {
            part.rigidBody->setLinearVelocity(btVector3(0,0,0));
            part.rigidBody->setAngularVelocity(btVector3(0,0,0));
            part.rigidBody->clearForces();
        }
    }
}

void PhysicsCharacter::render(Graphics::RenderCoordinator* renderer) {
    if (renderer->isRaycastVisualizationEnabled()) {
        auto* visualizer = renderer->getRaycastVisualizer();
        if (visualizer && headIndex != -1 && headIndex < parts.size()) {
            btRigidBody* headBody = parts[headIndex].rigidBody;
            if (headBody) {
                btTransform trans;
                headBody->getMotionState()->getWorldTransform(trans);
                btVector3 pos = trans.getOrigin();
                btVector3 forward = trans.getBasis().getColumn(2); // Z axis is forward
                
                glm::vec3 start(pos.x(), pos.y(), pos.z());
                glm::vec3 end(pos.x() + forward.x() * 2.0f, 
                             pos.y() + forward.y() * 2.0f, 
                             pos.z() + forward.z() * 2.0f);
                
                visualizer->addLine(start, end, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow line

                // Also draw line to target if available
                if (hasLookTarget) {
                    visualizer->addLine(start, lookTarget, glm::vec3(0.0f, 1.0f, 1.0f)); // Cyan line to target
                }
            }
        }
    }
}

} // namespace Scene
} // namespace Phyxel
