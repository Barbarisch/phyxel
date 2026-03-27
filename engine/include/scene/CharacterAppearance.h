#pragma once
#include <glm/glm.hpp>
#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Scene {

/// Defines the visual appearance of a character (colors + proportions).
/// Used by AnimatedVoxelCharacter to color bone regions and scale body parts.
struct CharacterAppearance {
    // Per-region colors
    glm::vec4 skinColor   {1.0f, 0.8f, 0.6f, 1.0f};   // Head, hands
    glm::vec4 torsoColor  {0.8f, 0.2f, 0.2f, 1.0f};   // Spine, torso, hips
    glm::vec4 armColor    {0.2f, 0.6f, 1.0f, 1.0f};   // Arms
    glm::vec4 legColor    {0.2f, 0.2f, 0.8f, 1.0f};   // Legs, feet
    glm::vec4 defaultColor{0.7f, 0.7f, 0.7f, 1.0f};   // Anything unmatched

    // Proportion scales (1.0 = default)
    float heightScale = 1.0f;   // Overall height multiplier
    float bulkScale   = 1.0f;   // Width/thickness multiplier
    float headScale   = 1.0f;   // Head size multiplier

    // Per-limb proportion scales (1.0 = default, stacks with overall scales)
    float armLengthScale   = 1.0f;   // Arm bone length
    float legLengthScale   = 1.0f;   // Leg bone length
    float torsoLengthScale = 1.0f;   // Spine/torso length
    float shoulderWidthScale = 1.0f; // Shoulder breadth

    /// Get the color for a bone based on its name (case-insensitive matching).
    glm::vec4 getColorForBone(const std::string& boneName) const;

    /// Returns the default appearance (matches the original hardcoded colors).
    static CharacterAppearance defaultAppearance();

    /// Generate a deterministic appearance from NPC name and optional role.
    static CharacterAppearance generateFromSeed(const std::string& name, const std::string& role = "");

    /// Parse appearance from JSON. Missing fields use defaults.
    static CharacterAppearance fromJson(const nlohmann::json& j);

    /// Serialize to JSON.
    nlohmann::json toJson() const;
};

} // namespace Scene
} // namespace Phyxel
