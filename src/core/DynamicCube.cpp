#include "core/DynamicCube.h"
#include "physics/Material.h"
#include "utils/Logger.h"
#include <btBulletDynamicsCommon.h>
#include <iostream>

namespace VulkanCube {

DynamicCube::DynamicCube() 
    : position(0.0f), color(1.0f), materialName("Default"), broken(false), visible(true), rigidBody(nullptr)
    , physicsPosition(0.0f), physicsRotation(0.0f, 0.0f, 0.0f, 1.0f) {  // Identity quaternion
}

DynamicCube::DynamicCube(const glm::vec3& pos, const glm::vec3& col) 
    : position(pos), color(col), materialName("Default"), broken(false), visible(true), rigidBody(nullptr)
    , physicsPosition(pos), physicsRotation(0.0f, 0.0f, 0.0f, 1.0f) {  // Identity quaternion
}

DynamicCube::DynamicCube(const glm::vec3& pos, const glm::vec3& col, const std::string& materialName) 
    : position(pos), color(col), materialName(materialName), broken(false), visible(true), rigidBody(nullptr)
    , physicsPosition(pos), physicsRotation(0.0f, 0.0f, 0.0f, 1.0f) {  // Identity quaternion
}

DynamicCube::DynamicCube(const DynamicCube& other)
    : position(other.position)
    , color(other.color)
    , materialName(other.materialName)
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
        materialName = other.materialName;
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

void DynamicCube::setMaterial(const std::string& newMaterialName) {
    materialName = newMaterialName;
    applyMaterialProperties();
}

void DynamicCube::applyMaterialProperties() {
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
    
    LOG_DEBUG_FMT("Physics", "[MATERIAL] Applied '" << materialName << "' material properties to dynamic cube");
}

void DynamicCube::applyMaterialProperties(const std::string& newMaterialName) {
    materialName = newMaterialName;
    applyMaterialProperties();
}

glm::vec3 DynamicCube::getEffectiveColor() const {
    // Get material properties for color tinting
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(materialName);
    
    // Apply material color tint
    return color * material.colorTint;
}

} // namespace VulkanCube
