#include "core/DynamicCube.h"
#include <iostream>

namespace VulkanCube {

DynamicCube::DynamicCube() 
    : position(0.0f), color(1.0f), broken(false), visible(true), rigidBody(nullptr) {
}

DynamicCube::DynamicCube(const glm::vec3& pos, const glm::vec3& col) 
    : position(pos), color(col), broken(false), visible(true), rigidBody(nullptr) {
}

DynamicCube::DynamicCube(const DynamicCube& other)
    : position(other.position)
    , color(other.color)
    , lifetime(other.lifetime)
    , broken(other.broken)
    , visible(other.visible)
    , rigidBody(nullptr) // Don't copy physics body - handle separately
    , physicsPosition(other.physicsPosition)
    , physicsRotation(other.physicsRotation) {
}

DynamicCube& DynamicCube::operator=(const DynamicCube& other) {
    if (this != &other) {
        position = other.position;
        color = other.color;
        lifetime = other.lifetime;
        broken = other.broken;
        visible = other.visible;
        rigidBody = nullptr; // Don't copy physics body - handle separately
        physicsPosition = other.physicsPosition;
        physicsRotation = other.physicsRotation;
    }
    return *this;
}

glm::vec3 DynamicCube::getWorldPosition() const {
    // For dynamic cubes, the world position is the same as physics position
    // since they're not constrained to the grid
    return physicsPosition;
}

} // namespace VulkanCube
