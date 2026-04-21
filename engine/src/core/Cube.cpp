#include "core/Cube.h"
#include "core/MaterialRegistry.h"
#include "physics/Material.h"
#include "physics/VoxelRigidBody.h"
#include "utils/Logger.h"

namespace Phyxel {

Cube::Cube()
    : position(0), broken(false), visible(true) {
    initializeBonds();
}

Cube::Cube(const glm::ivec3& pos)
    : position(pos), broken(false), visible(true) {
    initializeBonds();
}

Cube::Cube(const glm::ivec3& pos, const std::string& material)
    : position(pos), materialName(material),
      broken(false), visible(true) {
    initializeBonds();
    physicsPosition = glm::vec3(pos);
}

void Cube::initializeBonds(float defaultStrength) {
    for (auto& bond : bonds) {
        bond = Bond(defaultStrength);
    }
}

bool Cube::hasAnyBrokenBonds() const {
    for (const auto& bond : bonds) {
        if (bond.isBroken) return true;
    }
    return false;
}

int Cube::getNumberOfBrokenBonds() const {
    int count = 0;
    for (const auto& bond : bonds) {
        if (bond.isBroken) count++;
    }
    return count;
}

std::vector<BondDirection> Cube::getBrokenBondDirections() const {
    std::vector<BondDirection> brokenDirections;
    for (int i = 0; i < 6; ++i) {
        if (bonds[i].isBroken) {
            brokenDirections.push_back(static_cast<BondDirection>(i));
        }
    }
    return brokenDirections;
}

BondDirection Cube::getOppositeDirection(BondDirection direction) {
    switch (direction) {
        case BondDirection::POSITIVE_X: return BondDirection::NEGATIVE_X;
        case BondDirection::NEGATIVE_X: return BondDirection::POSITIVE_X;
        case BondDirection::POSITIVE_Y: return BondDirection::NEGATIVE_Y;
        case BondDirection::NEGATIVE_Y: return BondDirection::POSITIVE_Y;
        case BondDirection::POSITIVE_Z: return BondDirection::NEGATIVE_Z;
        case BondDirection::NEGATIVE_Z: return BondDirection::POSITIVE_Z;
        default: return BondDirection::POSITIVE_X; // fallback
    }
}

glm::ivec3 Cube::getDirectionVector(BondDirection direction) {
    switch (direction) {
        case BondDirection::POSITIVE_X: return glm::ivec3(1, 0, 0);
        case BondDirection::NEGATIVE_X: return glm::ivec3(-1, 0, 0);
        case BondDirection::POSITIVE_Y: return glm::ivec3(0, 1, 0);
        case BondDirection::NEGATIVE_Y: return glm::ivec3(0, -1, 0);
        case BondDirection::POSITIVE_Z: return glm::ivec3(0, 0, 1);
        case BondDirection::NEGATIVE_Z: return glm::ivec3(0, 0, -1);
        default: return glm::ivec3(0, 0, 0); // fallback
    }
}

BondDirection Cube::vectorToDirection(const glm::ivec3& vector) {
    if (vector == glm::ivec3(1, 0, 0)) return BondDirection::POSITIVE_X;
    if (vector == glm::ivec3(-1, 0, 0)) return BondDirection::NEGATIVE_X;
    if (vector == glm::ivec3(0, 1, 0)) return BondDirection::POSITIVE_Y;
    if (vector == glm::ivec3(0, -1, 0)) return BondDirection::NEGATIVE_Y;
    if (vector == glm::ivec3(0, 0, 1)) return BondDirection::POSITIVE_Z;
    if (vector == glm::ivec3(0, 0, -1)) return BondDirection::NEGATIVE_Z;
    return BondDirection::POSITIVE_X; // fallback for invalid vectors
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
