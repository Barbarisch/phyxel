#include "core/Cube.h"
#include "physics/Material.h"
#include "utils/Logger.h"
#include <btBulletDynamicsCommon.h>

namespace Phyxel {

Cube::Cube() 
    : position(0), broken(false), visible(true), rigidBody(nullptr) {
    initializeBonds();
}

Cube::Cube(const glm::ivec3& pos) 
    : position(pos), broken(false), visible(true), rigidBody(nullptr) {
    initializeBonds();
}

Cube::Cube(const glm::ivec3& pos, const std::string& material) 
    : position(pos), materialName(material),
      broken(false), visible(true), rigidBody(nullptr) {
    initializeBonds();
    physicsPosition = glm::vec3(pos); // Initialize physics position to grid position
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
    if (!rigidBody) return;
    
    // Get material properties
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(materialName);
    
    // Apply mass
    btVector3 localInertia(0, 0, 0);
    if (material.mass > 0.0f) {
        rigidBody->getCollisionShape()->calculateLocalInertia(material.mass, localInertia);
    }
    rigidBody->setMassProps(material.mass, localInertia);
    
    // Apply friction and restitution
    rigidBody->setFriction(material.friction);
    rigidBody->setRestitution(material.restitution);
    
    // Apply damping
    rigidBody->setDamping(material.linearDamping, material.angularDamping);
    
    LOG_DEBUG_FMT("Physics", "[MATERIAL] Applied '" << materialName << "' material properties to cube");
}

void Cube::applyMaterialProperties(const std::string& newMaterialName) {
    materialName = newMaterialName;
    applyMaterialProperties();
}

glm::vec3 Cube::getEffectiveColor() const {
    // Get material properties for color tinting
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(materialName);
    
    // Apply material color tint
    return material.colorTint;
}

} // namespace Phyxel
