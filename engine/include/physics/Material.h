#pragma once

#include <glm/glm.hpp>
#include <string>

namespace Phyxel {
namespace Physics {

/**
 * @brief Material properties for physics simulation
 *
 * Defines physical properties that affect how objects behave in the physics world.
 * Different materials can have vastly different behaviors (bouncy ball vs heavy metal).
 *
 * Note: Physics data is now loaded from materials.json via MaterialRegistry.
 * This struct remains for compatibility with existing physics code.
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

} // namespace Physics
} // namespace Phyxel
