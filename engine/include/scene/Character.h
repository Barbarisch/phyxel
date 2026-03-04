#pragma once

#include "scene/Entity.h"
#include "physics/PhysicsWorld.h"
#include <memory>

namespace VulkanCube {
namespace Scene {

class Character : public Entity {
public:
    Character(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos);
    virtual ~Character();

    void update(float deltaTime) override;
    void render(Graphics::RenderCoordinator* renderer) override;

    // Physics overrides
    void setPosition(const glm::vec3& pos) override;
    glm::vec3 getPosition() const override;

    // Movement controls
    void walk(const glm::vec3& direction);
    void jump();

protected:
    Physics::PhysicsWorld* physicsWorld;
    btKinematicCharacterController* controller;
    btPairCachingGhostObject* ghostObject;
};

} // namespace Scene
} // namespace VulkanCube
