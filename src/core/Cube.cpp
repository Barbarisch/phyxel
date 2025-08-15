#include "core/Cube.h"
#include "core/Subcube.h"
#include <algorithm>

namespace VulkanCube {

Cube::Cube() 
    : position(0), color(1.0f), originalColor(1.0f), broken(false), visible(true), rigidBody(nullptr) {
}

Cube::Cube(const glm::ivec3& pos, const glm::vec3& col) 
    : position(pos), color(col), originalColor(col), broken(false), visible(true), rigidBody(nullptr) {
}

void Cube::clearSubcubes() {
    for (Subcube* subcube : subcubes) {
        delete subcube;
    }
    subcubes.clear();
}

void Cube::removeSubcube(Subcube* subcube) {
    auto it = std::find(subcubes.begin(), subcubes.end(), subcube);
    if (it != subcubes.end()) {
        subcubes.erase(it);
    }
}

Subcube* Cube::getSubcubeAt(const glm::ivec3& localPos) {
    for (Subcube* subcube : subcubes) {
        if (subcube && subcube->getLocalPosition() == localPos) {
            return subcube;
        }
    }
    return nullptr;
}

const Subcube* Cube::getSubcubeAt(const glm::ivec3& localPos) const {
    for (const Subcube* subcube : subcubes) {
        if (subcube && subcube->getLocalPosition() == localPos) {
            return subcube;
        }
    }
    return nullptr;
}

} // namespace VulkanCube
