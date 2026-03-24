#pragma once

#include "scene/Character.h"

namespace Phyxel {
namespace Scene {

class Player; // Forward declaration

class Enemy : public Character {
public:
    Enemy(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos);
    
    void update(float deltaTime) override;
    void setTarget(Player* target) { this->target = target; }

private:
    Player* target = nullptr;
    float speed = 3.0f;
    float detectionRange = 20.0f;
};

} // namespace Scene
} // namespace Phyxel
