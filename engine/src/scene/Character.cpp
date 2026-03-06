#include "scene/Character.h"

namespace Phyxel {
namespace Scene {

Character::Character(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos) 
    : physicsWorld(physicsWorld) {
    
    // Create physics representation
    // Radius 0.5, Height 1.8 (standard humanoid)
    ghostObject = physicsWorld->createCharacterGhostObject(startPos, 0.5f, 1.8f);
    controller = physicsWorld->createCharacterController(ghostObject);
    
    position = startPos;
}

Character::~Character() {
    if (physicsWorld) {
        physicsWorld->removeCharacter(controller);
    }
}

void Character::update(float deltaTime) {
    // Sync position from physics to entity
    btTransform trans = ghostObject->getWorldTransform();
    btVector3 origin = trans.getOrigin();
    position = glm::vec3(origin.x(), origin.y(), origin.z());
    
    // Rotation is usually handled by gameplay logic, not physics controller (which is axis aligned usually)
}

void Character::render(Graphics::RenderCoordinator* renderer) {
    // TODO: Submit to renderer
}

void Character::setPosition(const glm::vec3& pos) {
    position = pos;
    btTransform trans = ghostObject->getWorldTransform();
    trans.setOrigin(btVector3(pos.x, pos.y, pos.z));
    ghostObject->setWorldTransform(trans);
}

glm::vec3 Character::getPosition() const {
    btTransform trans = ghostObject->getWorldTransform();
    btVector3 origin = trans.getOrigin();
    return glm::vec3(origin.x(), origin.y(), origin.z());
}

void Character::walk(const glm::vec3& direction) {
    if (controller) {
        controller->setWalkDirection(btVector3(direction.x, direction.y, direction.z));
    }
}

void Character::jump() {
    if (controller) {
        controller->jump();
    }
}

} // namespace Scene
} // namespace Phyxel
