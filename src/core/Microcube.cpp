#include "core/Microcube.h"

namespace VulkanCube {

Microcube::Microcube() 
    : parentCubePosition(0), 
      subcubeLocalPosition(0), microcubeLocalPosition(0), 
      scale(MICROCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

Microcube::Microcube(const glm::ivec3& parentCubePos) 
    : parentCubePosition(parentCubePos), 
      subcubeLocalPosition(0), microcubeLocalPosition(0), 
      scale(MICROCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

Microcube::Microcube(const glm::ivec3& parentCubePos, 
                     const glm::ivec3& subcubeLocalPos, const glm::ivec3& microcubeLocalPos) 
    : parentCubePosition(parentCubePos), 
      subcubeLocalPosition(subcubeLocalPos), microcubeLocalPosition(microcubeLocalPos), 
      scale(MICROCUBE_SCALE), broken(false), visible(true), rigidBody(nullptr) {
}

glm::vec3 Microcube::getWorldPosition() const {
    // Calculate the actual world position with two-level hierarchy:
    // 1. Parent cube position (world coordinates)
    // 2. + Subcube offset within cube (localPosition * subcube_scale)
    // 3. + Microcube offset within subcube (localPosition * microcube_scale)
    
    constexpr float SUBCUBE_SCALE = 1.0f / 3.0f;
    
    glm::vec3 parentWorldPos = glm::vec3(parentCubePosition);
    glm::vec3 subcubeOffset = glm::vec3(subcubeLocalPosition) * SUBCUBE_SCALE;
    glm::vec3 microcubeOffset = glm::vec3(microcubeLocalPosition) * MICROCUBE_SCALE;
    
    return parentWorldPos + subcubeOffset + microcubeOffset;
}

} // namespace VulkanCube
