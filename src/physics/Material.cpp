#include "physics/Material.h"
#include "utils/Logger.h"
#include <iostream>
#include <algorithm>

namespace VulkanCube {
namespace Physics {

MaterialManager::MaterialManager() {
    initializePredefinedMaterials();
}

const MaterialProperties& MaterialManager::getMaterial(const std::string& name) const {
    auto it = materials.find(name);
    if (it != materials.end()) {
        return it->second;
    }
    
    // Return default material if not found
    static MaterialProperties defaultMaterial = getDefault();
    return defaultMaterial;
}

bool MaterialManager::hasMaterial(const std::string& name) const {
    return materials.find(name) != materials.end();
}

void MaterialManager::addMaterial(const std::string& name, const MaterialProperties& properties) {
    materials[name] = properties;
    LOG_INFO_FMT("Physics", "[MATERIAL] Added custom material: " << name);
}

void MaterialManager::removeMaterial(const std::string& name) {
    auto it = materials.find(name);
    if (it != materials.end()) {
        materials.erase(it);
        LOG_INFO_FMT("Physics", "[MATERIAL] Removed material: " << name);
    }
}

std::vector<std::string> MaterialManager::getAllMaterialNames() const {
    std::vector<std::string> names;
    names.reserve(materials.size());
    
    for (const auto& pair : materials) {
        names.push_back(pair.first);
    }
    
    std::sort(names.begin(), names.end());
    return names;
}

void MaterialManager::printMaterialInfo(const std::string& name) const {
    if (!hasMaterial(name)) {
        LOG_WARN_FMT("Physics", "[MATERIAL] Material '" << name << "' not found!");
        return;
    }
    
    const auto& mat = getMaterial(name);
    LOG_INFO_FMT("Physics", "\n=== Material: " << mat.name << " ===");
    LOG_INFO_FMT("Physics", "Description: " << mat.description);
    LOG_INFO_FMT("Physics", "Mass: " << mat.mass << " kg");
    LOG_INFO_FMT("Physics", "Friction: " << mat.friction);
    LOG_INFO_FMT("Physics", "Restitution: " << mat.restitution);
    LOG_INFO_FMT("Physics", "Linear Damping: " << mat.linearDamping);
    LOG_INFO_FMT("Physics", "Angular Damping: " << mat.angularDamping);
    LOG_INFO_FMT("Physics", "Break Force Multiplier: " << mat.breakForceMultiplier);
    LOG_INFO_FMT("Physics", "Angular Velocity Scale: " << mat.angularVelocityScale);
    LOG_INFO_FMT("Physics", "Color Tint: (" << mat.colorTint.x << ", " << mat.colorTint.y << ", " << mat.colorTint.z << ")");
}

void MaterialManager::printAllMaterials() const {
    LOG_INFO("Physics", "\n=== Available Materials ===");
    auto names = getAllMaterialNames();
    for (const auto& name : names) {
        const auto& mat = getMaterial(name);
        LOG_INFO_FMT("Physics", "- " << name << ": " << mat.description);
    }
    LOG_INFO("Physics", "");
}

void MaterialManager::initializePredefinedMaterials() {
    // Wood - Light, medium friction, low bounce
    MaterialProperties wood;
    wood.name = "Wood";
    wood.description = "Light wooden material with natural feel";
    wood.mass = 0.7f;
    wood.friction = 0.6f;
    wood.restitution = 0.2f;
    wood.linearDamping = 0.2f;
    wood.angularDamping = 0.3f;
    wood.breakForceMultiplier = 0.8f;
    wood.angularVelocityScale = 1.2f;
    wood.colorTint = glm::vec3(0.8f, 0.6f, 0.3f); // Brown tint
    materials["Wood"] = wood;
    
    // Metal - Heavy, high friction, low bounce
    MaterialProperties metal;
    metal.name = "Metal";
    metal.description = "Heavy metallic material with high density";
    metal.mass = 4.0f;
    metal.friction = 0.7f;
    metal.restitution = 0.1f;
    metal.linearDamping = 0.05f;
    metal.angularDamping = 0.1f;
    metal.breakForceMultiplier = 1.5f;
    metal.angularVelocityScale = 0.6f;
    metal.colorTint = glm::vec3(0.7f, 0.7f, 0.8f); // Metallic tint
    materials["Metal"] = metal;
    
    // Glass - Medium weight, low friction, medium-high bounce, fragile
    MaterialProperties glass;
    glass.name = "Glass";
    glass.description = "Brittle glass material with smooth surface";
    glass.mass = 1.5f;
    glass.friction = 0.2f;
    glass.restitution = 0.6f;
    glass.linearDamping = 0.1f;
    glass.angularDamping = 0.15f;
    glass.breakForceMultiplier = 0.5f;
    glass.angularVelocityScale = 1.8f;
    glass.colorTint = glm::vec3(0.9f, 0.95f, 1.0f); // Clear bluish tint
    materials["Glass"] = glass;
    
    // Rubber - Light, high friction, very high bounce
    MaterialProperties rubber;
    rubber.name = "Rubber";
    rubber.description = "Bouncy rubber material with high elasticity";
    rubber.mass = 0.5f;
    rubber.friction = 0.9f;
    rubber.restitution = 0.9f;
    rubber.linearDamping = 0.3f;
    rubber.angularDamping = 0.4f;
    rubber.breakForceMultiplier = 0.6f;
    rubber.angularVelocityScale = 2.0f;
    rubber.colorTint = glm::vec3(0.3f, 0.3f, 0.3f); // Dark rubber
    materials["Rubber"] = rubber;
    
    // Stone - Very heavy, high friction, very low bounce
    MaterialProperties stone;
    stone.name = "Stone";
    stone.description = "Dense stone material with rough surface";
    stone.mass = 6.0f;
    stone.friction = 0.8f;
    stone.restitution = 0.05f;
    stone.linearDamping = 0.1f;
    stone.angularDamping = 0.2f;
    stone.breakForceMultiplier = 2.0f;
    stone.angularVelocityScale = 0.4f;
    stone.colorTint = glm::vec3(0.6f, 0.5f, 0.4f); // Stone gray-brown
    materials["Stone"] = stone;
    
    // Ice - Light, very low friction, medium bounce, slippery
    MaterialProperties ice;
    ice.name = "Ice";
    ice.description = "Slippery ice material with low friction";
    ice.mass = 0.9f;
    ice.friction = 0.1f;
    ice.restitution = 0.4f;
    ice.linearDamping = 0.05f;
    ice.angularDamping = 0.05f;
    ice.breakForceMultiplier = 0.7f;
    ice.angularVelocityScale = 2.5f;
    ice.colorTint = glm::vec3(0.8f, 0.9f, 1.0f); // Ice blue
    materials["Ice"] = ice;
    
    // Cork - Very light, medium friction, medium bounce
    MaterialProperties cork;
    cork.name = "Cork";
    cork.description = "Ultra-light cork material that floats";
    cork.mass = 0.2f;
    cork.friction = 0.5f;
    cork.restitution = 0.4f;
    cork.linearDamping = 0.4f;
    cork.angularDamping = 0.5f;
    cork.breakForceMultiplier = 0.3f;
    cork.angularVelocityScale = 3.0f;
    cork.colorTint = glm::vec3(0.8f, 0.7f, 0.5f); // Cork color
    materials["Cork"] = cork;
    
    // Default - Balanced properties
    materials["Default"] = getDefault();
}

// Static material getters for quick access
const MaterialProperties& MaterialManager::getWood() {
    static MaterialProperties wood;
    static bool initialized = false;
    if (!initialized) {
        MaterialManager temp;
        wood = temp.getMaterial("Wood");
        initialized = true;
    }
    return wood;
}

const MaterialProperties& MaterialManager::getMetal() {
    static MaterialProperties metal;
    static bool initialized = false;
    if (!initialized) {
        MaterialManager temp;
        metal = temp.getMaterial("Metal");
        initialized = true;
    }
    return metal;
}

const MaterialProperties& MaterialManager::getGlass() {
    static MaterialProperties glass;
    static bool initialized = false;
    if (!initialized) {
        MaterialManager temp;
        glass = temp.getMaterial("Glass");
        initialized = true;
    }
    return glass;
}

const MaterialProperties& MaterialManager::getRubber() {
    static MaterialProperties rubber;
    static bool initialized = false;
    if (!initialized) {
        MaterialManager temp;
        rubber = temp.getMaterial("Rubber");
        initialized = true;
    }
    return rubber;
}

const MaterialProperties& MaterialManager::getStone() {
    static MaterialProperties stone;
    static bool initialized = false;
    if (!initialized) {
        MaterialManager temp;
        stone = temp.getMaterial("Stone");
        initialized = true;
    }
    return stone;
}

const MaterialProperties& MaterialManager::getIce() {
    static MaterialProperties ice;
    static bool initialized = false;
    if (!initialized) {
        MaterialManager temp;
        ice = temp.getMaterial("Ice");
        initialized = true;
    }
    return ice;
}

const MaterialProperties& MaterialManager::getCork() {
    static MaterialProperties cork;
    static bool initialized = false;
    if (!initialized) {
        MaterialManager temp;
        cork = temp.getMaterial("Cork");
        initialized = true;
    }
    return cork;
}

const MaterialProperties& MaterialManager::getDefault() {
    static MaterialProperties defaultMat;
    static bool initialized = false;
    if (!initialized) {
        defaultMat.name = "Default";
        defaultMat.description = "Balanced default material properties";
        defaultMat.mass = 1.0f;
        defaultMat.friction = 0.5f;
        defaultMat.restitution = 0.3f;
        defaultMat.linearDamping = 0.1f;
        defaultMat.angularDamping = 0.1f;
        defaultMat.breakForceMultiplier = 1.0f;
        defaultMat.angularVelocityScale = 1.0f;
        defaultMat.colorTint = glm::vec3(1.0f);
        initialized = true;
    }
    return defaultMat;
}

} // namespace Physics
} // namespace VulkanCube
