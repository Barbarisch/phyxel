#pragma once

#include "scene/RagdollCharacter.h"
#include "physics/PhysicsWorld.h"
#include "input/InputManager.h"
#include "graphics/Camera.h"
#include <vector>
#include <memory>
#include <map>

namespace VulkanCube {
namespace Scene {

enum class CharacterState {
    Idle,
    Walking,
    Jumping,
    Falling,
    Stumbling
};

class PhysicsCharacter;

// Action interface to hook into physics steps
class CharacterAction : public btActionInterface {
    PhysicsCharacter* character;
public:
    CharacterAction(PhysicsCharacter* c) : character(c) {}
    void updateAction(btCollisionWorld* collisionWorld, btScalar deltaTimeStep) override;
    void debugDraw(btIDebugDraw* debugDrawer) override {}
};

class PhysicsCharacter : public RagdollCharacter {
    friend class CharacterAction;
public:
    PhysicsCharacter(Physics::PhysicsWorld* physicsWorld, Input::InputManager* inputManager, Graphics::Camera* camera, const glm::vec3& startPos);
    virtual ~PhysicsCharacter();

    void update(float deltaTime) override;
    void updateCamera(); // Call this AFTER physics step
    void render(Graphics::RenderCoordinator* renderer) override;

    // Control
    void setControlActive(bool active) { isControlActive = active; }
    bool isControlled() const { return isControlActive; }

    // Movement controls
    void move(const glm::vec3& direction);
    void jump();
    
    // Look control
    void setLookTarget(const glm::vec3& target);
    void clearLookTarget();

    void reset(const glm::vec3& pos);

    // State queries
    CharacterState getState() const { return state; }
    bool getIsGrounded() const { return isGrounded; }

    // Internal physics step
    void prePhysicsStep(float deltaTime);

private:
    void createRagdoll(const glm::vec3& startPos);
    void updateWalkCycle(float deltaTime);
    void updateHead(float deltaTime);
    void keepUpright(float deltaTime);
    void processInput(float deltaTime);
    void checkGroundStatus();

    Input::InputManager* inputManager;
    Graphics::Camera* camera;
    
    CharacterAction* physicsAction = nullptr;

    // Control variables
    bool isControlActive = false;
    glm::vec3 moveDirection = glm::vec3(0.0f);
    
    // Look control
    bool hasLookTarget = false;
    glm::vec3 lookTarget = glm::vec3(0.0f);

    float walkSpeed = 5.0f;
    float jumpForce = 10.0f;
    float walkCycleTime = 0.0f;
    bool isMoving = false;
    float jumpCooldown = 0.0f;

    // PID Balance Controller
    float kp = 150.0f; // Proportional gain (increased from 80)
    float ki = 5.0f;   // Integral gain
    float kd = 20.0f;  // Derivative gain
    btVector3 integralError = btVector3(0,0,0);

    // State
    CharacterState state = CharacterState::Idle;
    bool isGrounded = false;

    // Body parts indices
    int torsoIndex = -1;
    int headIndex = -1;
    int leftArmIndex = -1;
    int rightArmIndex = -1;
    int leftForearmIndex = -1;
    int rightForearmIndex = -1;
    int leftLegIndex = -1;
    int rightLegIndex = -1;
    int leftShinIndex = -1;
    int rightShinIndex = -1;
    
    // Joint indices for animation
    int leftHipIndex = -1;
    int rightHipIndex = -1;
    int leftKneeIndex = -1;
    int rightKneeIndex = -1;
    int leftElbowIndex = -1;
    int rightElbowIndex = -1;
    int neckIndex = -1;
};

} // namespace Scene
} // namespace VulkanCube
