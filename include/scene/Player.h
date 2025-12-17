#pragma once

#include "scene/Character.h"
#include "input/InputManager.h"
#include "graphics/Camera.h"

namespace VulkanCube {
namespace Scene {

class Player : public Character {
public:
    Player(Physics::PhysicsWorld* physicsWorld, Input::InputManager* inputManager, Graphics::Camera* camera, const glm::vec3& startPos);
    
    void update(float deltaTime) override;

    // Camera getters for the Application to use
    float getPitch() const { return pitch; }
    float getYaw() const { return yaw; }

private:
    Input::InputManager* inputManager;
    Graphics::Camera* camera;
    float speed = 5.0f;
    float mouseSensitivity = 0.1f;
    
    // Camera control
    float pitch = 0.0f;
    float yaw = -90.0f;
};

} // namespace Scene
} // namespace VulkanCube
