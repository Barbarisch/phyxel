#include "core/Cube.h"
#include "core/MaterialRegistry.h"
#include "physics/Material.h"
#include "physics/VoxelRigidBody.h"
#include "utils/Logger.h"

namespace Phyxel {

Cube::Cube()
    : position(0), broken(false), visible(true) {
}

Cube::Cube(const glm::ivec3& pos)
    : position(pos), broken(false), visible(true) {
}

Cube::Cube(const glm::ivec3& pos, const std::string& material)
    : position(pos), materialName(material),
      broken(false), visible(true) {
    physicsPosition = glm::vec3(pos);
}

glm::vec3 Cube::getWorldPosition() const {
    // If dynamic, use smooth physics position
    // If static, convert grid position to world
    return isDynamic() ? physicsPosition : glm::vec3(position);
}

void Cube::setMaterial(const std::string& newMaterialName) {
    materialName = newMaterialName;
    applyMaterialProperties();
}

void Cube::applyMaterialProperties() {
    if (!voxelBody) return;

    const auto& material = Phyxel::Core::MaterialRegistry::instance().getPhysics(materialName);
    voxelBody->friction       = material.friction;
    voxelBody->restitution    = material.restitution;
    voxelBody->linearDamping  = material.linearDamping;
    voxelBody->angularDamping = material.angularDamping;
    if (material.mass > 0.0f) {
        voxelBody->invMass = 1.0f / material.mass;
    }

    LOG_DEBUG_FMT("Physics", "[MATERIAL] Applied '" << materialName << "' to cube voxelBody");
}

void Cube::applyMaterialProperties(const std::string& newMaterialName) {
    materialName = newMaterialName;
    applyMaterialProperties();
}

glm::vec3 Cube::getEffectiveColor() const {
    // Get material properties for color tinting
    const auto& material = Phyxel::Core::MaterialRegistry::instance().getPhysics(materialName);
    
    // Apply material color tint
    return material.colorTint;
}

} // namespace Phyxel
