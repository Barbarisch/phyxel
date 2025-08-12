#include "core/Subcube.h"

namespace VulkanCube {

Subcube::Subcube() 
    : position(0), color(1.0f), localPosition(0), scale(SUBCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

Subcube::Subcube(const glm::ivec3& pos, const glm::vec3& col) 
    : position(pos), color(col), localPosition(0), scale(SUBCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

Subcube::Subcube(const glm::ivec3& pos, const glm::vec3& col, const glm::ivec3& localPos) 
    : position(pos), color(col), localPosition(localPos), scale(SUBCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

glm::vec3 Subcube::getWorldPosition() const {
    // Calculate the actual world position by adding the local offset to the parent cube position
    // Each subcube is offset by its local position * scale within the parent cube space
    glm::vec3 parentWorldPos = glm::vec3(position);
    glm::vec3 localOffset = glm::vec3(localPosition) * scale;
    
    // Adjust the offset to center the 3x3x3 grid within the parent cube
    // The center of the 3x3x3 grid should align with the center of the parent cube
    glm::vec3 centeringOffset = glm::vec3(-scale, -scale, -scale); // Move back by 1 subcube unit
    
    return parentWorldPos + localOffset + centeringOffset;
}

} // namespace VulkanCube
