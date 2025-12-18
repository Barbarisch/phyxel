#include "scene/PhysicsCharacter.h"
#include "graphics/RenderCoordinator.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace VulkanCube {
namespace Scene {

PhysicsCharacter::PhysicsCharacter(Physics::PhysicsWorld* physicsWorld, Input::InputManager* inputManager, Graphics::Camera* camera, const glm::vec3& startPos)
    : physicsWorld(physicsWorld), inputManager(inputManager), camera(camera) {
    createRagdoll(startPos);
}

PhysicsCharacter::~PhysicsCharacter() {
    // Cleanup constraints
    for (auto* constraint : constraints) {
        physicsWorld->removeConstraint(constraint);
        delete constraint;
    }
    constraints.clear();

    // Cleanup rigid bodies
    for (auto& part : parts) {
        if (part.rigidBody) {
            physicsWorld->removeCube(part.rigidBody);
        }
    }
    parts.clear();
}

void PhysicsCharacter::createRagdoll(const glm::vec3& startPos) {
    // Define scales (using Subcube scale approx 0.33f)
    float subScale = 1.0f / 3.0f;
    glm::vec3 torsoScale(subScale, subScale * 1.5f, subScale); // Taller torso
    glm::vec3 headScale(subScale * 0.8f);
    glm::vec3 limbScale(subScale * 0.5f, subScale * 1.2f, subScale * 0.5f);

    // Create Torso
    btRigidBody* torso = physicsWorld->createCube(startPos, torsoScale, 10.0f);
    torso->setUserPointer((void*)1); // Mark as character part (prevent auto-cleanup)
    torso->setActivationState(DISABLE_DEACTIVATION);
    torso->setAngularFactor(btVector3(0, 1, 0)); // Limit rotation to Y axis for now (easier balance)
    torso->setDamping(0.5f, 0.5f); // Add damping to reduce sliding
    parts.push_back({torso, torsoScale, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), "Torso"});
    torsoIndex = 0;

    // Create Head
    glm::vec3 headPos = startPos + glm::vec3(0, torsoScale.y + headScale.y + 0.05f, 0);
    btRigidBody* head = physicsWorld->createCube(headPos, headScale, 2.0f);
    head->setUserPointer((void*)1); // Mark as character part
    parts.push_back({head, headScale, glm::vec4(1.0f, 0.8f, 0.6f, 1.0f), "Head"});
    headIndex = 1;

    // Neck Joint (Hinge)
    btTransform localA, localB;
    localA.setIdentity(); localB.setIdentity();
    
    // Rotate frames so Z axis points along X axis (for nodding)
    btQuaternion rotZtoX;
    rotZtoX.setRotation(btVector3(0,1,0), 1.5708f); // 90 degrees around Y
    localA.setRotation(rotZtoX);
    localB.setRotation(rotZtoX);

    localA.setOrigin(btVector3(0, torsoScale.y * 0.5f + 0.025f, 0));
    localB.setOrigin(btVector3(0, -headScale.y * 0.5f - 0.025f, 0));
    btHingeConstraint* neck = new btHingeConstraint(*torso, *head, localA, localB);
    neck->setLimit(-0.5f, 0.5f); // Limit head nodding
    physicsWorld->addConstraint(neck, true);
    constraints.push_back(neck);

    // Helper to create limbs
    auto createLimb = [&](const glm::vec3& offset, const std::string& name, float mass) -> int {
        glm::vec3 pos = startPos + offset;
        btRigidBody* limb = physicsWorld->createCube(pos, limbScale, mass);
        limb->setUserPointer((void*)1); // Mark as character part
        limb->setFriction(1.5f); // High friction for grip
        parts.push_back({limb, limbScale, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), name});
        return (int)(parts.size() - 1);
    };

    // Legs
    float legOffsetY = -torsoScale.y * 0.5f - limbScale.y * 0.5f - 0.05f;
    float legOffsetX = torsoScale.x * 0.5f;
    
    leftLegIndex = createLimb(glm::vec3(-legOffsetX, legOffsetY, 0), "LeftLeg", 5.0f);
    rightLegIndex = createLimb(glm::vec3(legOffsetX, legOffsetY, 0), "RightLeg", 5.0f);

    // Hip Joints (Hinge for walking)
    auto createHip = [&](int legIdx, float xDir) -> int {
        localA.setIdentity(); localB.setIdentity();
        
        // Rotate frames so Z axis points along X axis (for walking forward/back)
        btQuaternion rotZtoX;
        rotZtoX.setRotation(btVector3(0,1,0), 1.5708f); // 90 degrees around Y
        localA.setRotation(rotZtoX);
        localB.setRotation(rotZtoX);

        localA.setOrigin(btVector3(xDir * legOffsetX, -torsoScale.y * 0.5f, 0));
        localB.setOrigin(btVector3(0, limbScale.y * 0.5f, 0));
        
        // Use constructor that takes frames (transforms) and axes
        // We want the hinge axis to be X (1,0,0) for walking
        btHingeConstraint* hip = new btHingeConstraint(*torso, *parts[legIdx].rigidBody, localA, localB);
        hip->setLimit(-0.8f, 0.8f);
        physicsWorld->addConstraint(hip, true);
        constraints.push_back(hip);
        return (int)(constraints.size() - 1);
    };

    leftHipIndex = createHip(leftLegIndex, -1.0f);
    rightHipIndex = createHip(rightLegIndex, 1.0f);

    // Arms
    float armOffsetY = torsoScale.y * 0.3f;
    float armOffsetX = torsoScale.x * 0.5f + limbScale.x * 0.5f + 0.05f;

    leftArmIndex = createLimb(glm::vec3(-armOffsetX, armOffsetY, 0), "LeftArm", 2.0f);
    rightArmIndex = createLimb(glm::vec3(armOffsetX, armOffsetY, 0), "RightArm", 2.0f);

    // Shoulder Joints
    auto createShoulder = [&](int armIdx, float xDir) {
        localA.setIdentity(); localB.setIdentity();
        localA.setOrigin(btVector3(xDir * (torsoScale.x * 0.5f), armOffsetY, 0));
        localB.setOrigin(btVector3(-xDir * (limbScale.x * 0.5f), limbScale.y * 0.4f, 0)); // Attach near top of arm
        btConeTwistConstraint* shoulder = new btConeTwistConstraint(*torso, *parts[armIdx].rigidBody, localA, localB);
        shoulder->setLimit(0.5f, 0.5f, 0);
        physicsWorld->addConstraint(shoulder, true);
        constraints.push_back(shoulder);
    };

    createShoulder(leftArmIndex, -1.0f);
    createShoulder(rightArmIndex, 1.0f);
}

void PhysicsCharacter::update(float deltaTime) {
    checkGroundStatus();

    if (isControlActive) {
        processInput(deltaTime);
    } else {
        isMoving = false;
    }

    keepUpright(deltaTime);
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
    camera->updatePositionFromTarget(pos, 1.5f);
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
        auto resetLeg = [&](int constraintIdx) {
            if (constraintIdx >= 0 && constraintIdx < constraints.size()) {
                btHingeConstraint* hinge = static_cast<btHingeConstraint*>(constraints[constraintIdx]);
                hinge->enableAngularMotor(true, (0.0f - hinge->getHingeAngle()) * 5.0f, 100.0f);
            }
        };
        resetLeg(leftHipIndex);
        resetLeg(rightHipIndex);

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

    // Drive motors
    auto driveLeg = [&](int constraintIdx, float targetAngle) {
        if (constraintIdx >= 0 && constraintIdx < constraints.size()) {
            btHingeConstraint* hinge = static_cast<btHingeConstraint*>(constraints[constraintIdx]);
            // Target velocity is proportional to error (P-controller for velocity)
            float error = targetAngle - hinge->getHingeAngle();
            float targetVel = error * 10.0f; // Gain
            float maxImpulse = 200.0f; // Stronger legs
            hinge->enableAngularMotor(true, targetVel, maxImpulse);
        }
    };

    driveLeg(leftHipIndex, leftLegTarget);
    driveLeg(rightHipIndex, rightLegTarget);
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

void PhysicsCharacter::setPosition(const glm::vec3& pos) {
    // Teleport all parts
    glm::vec3 currentPos = getPosition();
    glm::vec3 diff = pos - currentPos;
    
    for (auto& part : parts) {
        if (part.rigidBody) {
            btTransform trans;
            part.rigidBody->getMotionState()->getWorldTransform(trans);
            trans.setOrigin(trans.getOrigin() + btVector3(diff.x, diff.y, diff.z));
            part.rigidBody->setWorldTransform(trans);
            part.rigidBody->setLinearVelocity(btVector3(0,0,0));
            part.rigidBody->setAngularVelocity(btVector3(0,0,0));
        }
    }
}

glm::vec3 PhysicsCharacter::getPosition() const {
    if (torsoIndex != -1 && parts[torsoIndex].rigidBody) {
        btTransform trans;
        parts[torsoIndex].rigidBody->getMotionState()->getWorldTransform(trans);
        btVector3 pos = trans.getOrigin();
        return glm::vec3(pos.x(), pos.y(), pos.z());
    }
    return glm::vec3(0.0f);
}

void PhysicsCharacter::render(Graphics::RenderCoordinator* renderer) {
    // This will be called by RenderCoordinator, but RenderCoordinator needs to be updated 
    // to handle PhysicsCharacter or we need to expose a way to render cubes from here.
    // Since RenderCoordinator calls this, we can't easily call back into RenderCoordinator's private methods.
    // We will rely on RenderCoordinator to cast this Entity to PhysicsCharacter and iterate parts.
}

} // namespace Scene
} // namespace VulkanCube
