#include "core/Subcube.h"

namespace VulkanCube {

Subcube::Subcube() 
    : position(0), localPosition(0), scale(SUBCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

Subcube::Subcube(const glm::ivec3& pos) 
    : position(pos), localPosition(0), scale(SUBCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

Subcube::Subcube(const glm::ivec3& pos, const glm::ivec3& localPos) 
    : position(pos), localPosition(localPos), scale(SUBCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

glm::vec3 Subcube::getWorldPosition() const {
    // Calculate the actual world position by adding the local offset to the parent cube position
    // Each subcube is offset by its local position * scale within the parent cube space
    glm::vec3 parentWorldPos = glm::vec3(position);
    glm::vec3 localOffset = glm::vec3(localPosition) * scale;
    
    // No centering offset needed - subcubes should align with parent cube corner
    // For a 3x3x3 grid: (0,0,0) at corner, (1,1,1) at (0.333,0.333,0.333), (2,2,2) at (0.667,0.667,0.667)
    
    return parentWorldPos + localOffset;
}

} // namespace VulkanCube
