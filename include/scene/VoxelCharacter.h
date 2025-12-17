#pragma once

#include "scene/Entity.h"
#include "physics/PhysicsWorld.h"
#include "input/InputManager.h"
#include "graphics/Camera.h"
#include <vector>
#include <memory>
#include <map>

namespace VulkanCube {
namespace Scene {

struct VoxelPart {
    btRigidBody* rigidBody;
    glm::vec3 scale;
    glm::vec4 color;
    std::string name;
};

class VoxelCharacter : public Entity {
public:
    VoxelCharacter(Physics::PhysicsWorld* physicsWorld, Input::InputManager* inputManager, Graphics::Camera* camera, const glm::vec3& startPos);
    virtual ~VoxelCharacter();

    void update(float deltaTime) override;
    void render(Graphics::RenderCoordinator* renderer) override;

    // Physics overrides
    void setPosition(const glm::vec3& pos) override;
    glm::vec3 getPosition() const override;

    // Control
    void setControlActive(bool active) { isControlActive = active; }
    bool isControlled() const { return isControlActive; }

    // Movement controls
    void move(const glm::vec3& direction);
    void jump();

    // Getters for parts (useful for debugging or attachments)
    const std::vector<VoxelPart>& getParts() const { return parts; }

private:
    void createRagdoll(const glm::vec3& startPos);
    void updateWalkCycle(float deltaTime);
    void keepUpright(float deltaTime);
    void processInput(float deltaTime);

    Physics::PhysicsWorld* physicsWorld;
    Input::InputManager* inputManager;
    Graphics::Camera* camera;
    
    std::vector<VoxelPart> parts;
    std::vector<btTypedConstraint*> constraints;

    // Control variables
    bool isControlActive = false;
    glm::vec3 moveDirection = glm::vec3(0.0f);
    float walkSpeed = 5.0f;
    float jumpForce = 10.0f;
    float walkCycleTime = 0.0f;
    bool isMoving = false;
    float jumpCooldown = 0.0f;

    // Body parts indices
    int torsoIndex = -1;
    int headIndex = -1;
    int leftArmIndex = -1;
    int rightArmIndex = -1;
    int leftLegIndex = -1;
    int rightLegIndex = -1;
    
    // Joint indices for animation
    int leftHipIndex = -1;
    int rightHipIndex = -1;
    int leftKneeIndex = -1;
    int rightKneeIndex = -1;
};

} // namespace Scene
} // namespace VulkanCube
