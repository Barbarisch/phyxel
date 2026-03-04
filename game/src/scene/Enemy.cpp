#include "scene/Enemy.h"
#include "scene/Player.h"

namespace VulkanCube {
namespace Scene {

Enemy::Enemy(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos)
    : Character(physicsWorld, startPos) {
    debugColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Red for Enemy
}

void Enemy::update(float deltaTime) {
    Character::update(deltaTime);

    if (target) {
        glm::vec3 targetPos = target->getPosition();
        glm::vec3 myPos = getPosition();
        float dist = glm::distance(targetPos, myPos);

        if (dist < detectionRange && dist > 1.0f) {
            glm::vec3 dir = glm::normalize(targetPos - myPos);
            
            // Simple AI: Move towards player
            // Note: walk expects displacement or velocity depending on implementation. 
            // We assumed displacement in Player.cpp, so we do the same here.
            walk(dir * speed * deltaTime);
            
            // Rotate to face player
            float angle = atan2(dir.x, dir.z);
            rotation = glm::angleAxis(angle, glm::vec3(0, 1, 0));
        } else {
            walk(glm::vec3(0.0f));
        }
    }
}

} // namespace Scene
} // namespace VulkanCube
