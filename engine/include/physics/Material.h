#pragma once

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Phyxel {
namespace Physics {

/**
 * @brief Material properties for physics simulation
 * 
 * Defines physical properties that affect how objects behave in the physics world.
 * Different materials can have vastly different behaviors (bouncy ball vs heavy metal).
 */
struct MaterialProperties {
    // Core physics properties
    float mass = 1.0f;                    // Mass in kg
    float friction = 0.5f;                // Surface friction (0.0 = slippery, 1.0 = grippy)
    float restitution = 0.3f;             // Bounciness (0.0 = no bounce, 1.0 = perfect bounce)
    float linearDamping = 0.1f;           // Air resistance for linear motion
    float angularDamping = 0.1f;          // Air resistance for rotation
    
    // Breaking/impulse properties
    float breakForceMultiplier = 1.0f;    // Multiplier for initial break force
    float angularVelocityScale = 1.0f;    // Scale for random tumbling
    float bondStrength = 0.5f;            // Inter-voxel bond strength (0=fragile, 1=unbreakable)
    
    // Visual properties (optional)
    glm::vec3 colorTint = glm::vec3(1.0f); // Color multiplier for material
    float metallic = 0.0f;                // Metallic appearance (future use)
    float roughness = 0.5f;               // Surface roughness (future use)
    
    // Identification
    std::string name = "Default";
    std::string description = "Standard material";
};

/**
 * @brief Material manager for predefined and custom materials
 */
class MaterialManager {
public:
    MaterialManager();
    
    // Predefined material access
    const MaterialProperties& getMaterial(const std::string& name) const;
    bool hasMaterial(const std::string& name) const;
    
    // Custom material management
    void addMaterial(const std::string& name, const MaterialProperties& properties);
    void removeMaterial(const std::string& name);
    
    // Utility methods
    std::vector<std::string> getAllMaterialNames() const;
    void printMaterialInfo(const std::string& name) const;
    void printAllMaterials() const;
    
    // Quick access to common materials
    static const MaterialProperties& getWood();
    static const MaterialProperties& getMetal();
    static const MaterialProperties& getGlass();
    static const MaterialProperties& getRubber();
    static const MaterialProperties& getStone();
    static const MaterialProperties& getIce();
    static const MaterialProperties& getCork();
    static const MaterialProperties& getDefault();

private:
    std::unordered_map<std::string, MaterialProperties> materials;
    void initializePredefinedMaterials();
};

} // namespace Physics
} // namespace Phyxel
