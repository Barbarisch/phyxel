#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel {

// Forward-declare Skeleton (defined in Animation.h)
struct Skeleton;

namespace Scene {

/// Morphology types for character customization.
enum class MorphologyType {
    Humanoid,    ///< Bipedal human-like (Mixamo rigs: head, arm, leg, spine)
    Quadruped,   ///< Four-legged animal (wolf: pelvis, chest, paw, tail)
    Arachnid,    ///< Eight-legged (spider: root, thorax, abdomen, leg1-4)
    Dragon,      ///< Winged quadruped (dragon: pelvis, wing, neck_1-4, tail)
    Unknown      ///< Unrecognized skeleton — falls back to Humanoid behavior
};

/// Detect morphology from a skeleton's bone names.
MorphologyType detectMorphology(const Skeleton& skeleton);
MorphologyType detectMorphology(const std::vector<std::string>& boneNames);

/// Defines the visual appearance of a character (colors + proportions).
/// Used by AnimatedVoxelCharacter to color bone regions and scale body parts.
///
/// The color/proportion system works for all morphology types:
/// - Humanoid: 5 regions (skin, torso, arm, leg, default)
/// - Quadruped: 5 regions (fur_primary, fur_secondary, face, paw, default)
/// - Arachnid: 5 regions (carapace, abdomen, leg, fang, default)
/// - Dragon: 5 regions (scales_primary, scales_secondary, wing, belly, default)
///
/// The same struct fields are reused with different semantic meaning per morphology.
/// getColorForBone() dispatches to the correct mapping based on the stored morphology.
struct CharacterAppearance {
    // ---- Morphology ----
    MorphologyType morphology = MorphologyType::Unknown;

    // ---- Color regions ----
    // These 5 slots are semantically named per morphology type.
    // For Humanoid:  region1=skin,  region2=torso, region3=arm,  region4=leg,     default
    // For Quadruped: region1=face,  region2=fur_primary, region3=fur_secondary, region4=paw, default
    // For Arachnid:  region1=fang,  region2=carapace,  region3=abdomen, region4=leg, default
    // For Dragon:    region1=belly, region2=scales_primary, region3=scales_secondary, region4=wing, default
    glm::vec4 skinColor   {1.0f, 0.8f, 0.6f, 1.0f};   // region1
    glm::vec4 torsoColor  {0.8f, 0.2f, 0.2f, 1.0f};   // region2
    glm::vec4 armColor    {0.2f, 0.6f, 1.0f, 1.0f};   // region3
    glm::vec4 legColor    {0.2f, 0.2f, 0.8f, 1.0f};   // region4
    glm::vec4 defaultColor{0.7f, 0.7f, 0.7f, 1.0f};   // catch-all

    // ---- Proportion scales (1.0 = default) ----
    float heightScale = 1.0f;   // Overall height multiplier
    float bulkScale   = 1.0f;   // Width/thickness multiplier
    float headScale   = 1.0f;   // Head size multiplier

    // Per-limb proportion scales (humanoid-specific, stacks with overall scales)
    float armLengthScale   = 1.0f;   // Arm bone length
    float legLengthScale   = 1.0f;   // Leg bone length
    float torsoLengthScale = 1.0f;   // Spine/torso length
    float shoulderWidthScale = 1.0f; // Shoulder breadth

    // Creature-specific proportion scales
    float tailLengthScale = 1.0f;   // Tail length (quadruped, dragon)
    float wingSpanScale   = 1.0f;   // Wing span (dragon)
    float neckLengthScale = 1.0f;   // Neck length (dragon, quadruped)

    /// Get the color for a bone based on its name, using morphology-aware mapping.
    glm::vec4 getColorForBone(const std::string& boneName) const;

    /// Returns the default appearance (matches the original hardcoded colors).
    static CharacterAppearance defaultAppearance();

    /// Generate a deterministic appearance from NPC name and optional role.
    /// For creature morphologies, roles like "alpha", "juvenile", "elder",
    /// "albino", "runt" are supported.
    static CharacterAppearance generateFromSeed(const std::string& name,
                                                 const std::string& role = "",
                                                 MorphologyType morphology = MorphologyType::Humanoid);

    /// Parse appearance from JSON. Missing fields use defaults.
    static CharacterAppearance fromJson(const nlohmann::json& j);

    /// Serialize to JSON.
    nlohmann::json toJson() const;
};

} // namespace Scene
} // namespace Phyxel
