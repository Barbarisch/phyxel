#include "scene/Player.h"

namespace VulkanCube {
namespace Scene {

Player::Player(Physics::PhysicsWorld* physicsWorld, Input::InputManager* inputManager, const glm::vec3& startPos)
    : Character(physicsWorld, startPos), inputManager(inputManager) {
}

void Player::update(float deltaTime) {
    Character::update(deltaTime);

    if (!inputManager) return;

    // Handle movement
    glm::vec3 direction(0.0f);
    
    // Get camera front vector but flatten it to XZ plane for movement
    glm::vec3 front = inputManager->getCameraFront();
    front.y = 0;
    if (glm::length(front) > 0.001f) {
        front = glm::normalize(front);
    } else {
        front = glm::vec3(0, 0, -1);
    }
    
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));

    if (inputManager->isKeyPressed(GLFW_KEY_W)) direction += front;
    if (inputManager->isKeyPressed(GLFW_KEY_S)) direction -= front;
    if (inputManager->isKeyPressed(GLFW_KEY_A)) direction -= right;
    if (inputManager->isKeyPressed(GLFW_KEY_D)) direction += right;

    if (glm::length(direction) > 0.0f) {
        direction = glm::normalize(direction);
        walk(direction * speed * deltaTime);
    } else {
        walk(glm::vec3(0.0f));
    }

    if (inputManager->isKeyPressed(GLFW_KEY_SPACE)) {
        jump();
    }
}

} // namespace Scene
} // namespace VulkanCube
