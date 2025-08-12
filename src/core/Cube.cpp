#include "core/Cube.h"

namespace VulkanCube {

Cube::Cube() 
    : position(0), color(1.0f), broken(false), visible(true), rigidBody(nullptr) {
}

Cube::Cube(const glm::ivec3& pos, const glm::vec3& col) 
    : position(pos), color(col), broken(false), visible(true), rigidBody(nullptr) {
}

} // namespace VulkanCube
