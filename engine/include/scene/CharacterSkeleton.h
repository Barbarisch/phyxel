#pragma once

#include "graphics/Animation.h"
#include "scene/CharacterAppearance.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>

namespace Phyxel {
namespace Scene {

/// Type of physics constraint connecting two bones.
enum class JointType {
    Hinge,       // Single-axis rotation (knee, elbow)
    ConeTwist,   // Cone of rotation + twist (neck, shoulder, hip)
    Fixed        // Rigid attachment (nose to head, accessories)
};

/// Defines the physics constraint between a parent bone and child bone.
struct CharacterJointDef {
    int parentBoneId = -1;
    int childBoneId  = -1;

    JointType type = JointType::Hinge;

    // Hinge parameters
    float hingeLimitLow  = -1.5f;  // radians
    float hingeLimitHigh =  1.5f;

    // ConeTwist parameters
    float swingSpan1 = 0.5f;   // radians (cone half-angle 1)
    float swingSpan2 = 0.5f;   // radians (cone half-angle 2)
    float twistSpan  = 0.5f;   // radians

    // Motor parameters (for active ragdoll in Phase 3)
    float motorStrength = 50.0f;  // max impulse / torque
    float motorDamping  = 0.5f;

    // Anchor offsets in each body's local space
    glm::vec3 parentAnchor{0.0f}; // Offset in parent bone's local space
    glm::vec3 childAnchor{0.0f};  // Offset in child bone's local space

    // Local frame rotation for hinge axis alignment
    // Default: hinge axis along local X (Bullet convention for btHingeConstraint)
    glm::vec3 hingeAxis{0.0f, 0.0f, 1.0f}; // Axis in local space
};

/// Defines which bones to skip during physics body creation
/// (small bones like fingers, toes, eyes that are too small for physics).
struct BoneFilterConfig {
    std::vector<std::string> skipPatterns = {
        "thumb", "index", "middle", "ring", "pinky",
        "eye", "toe", "end"
    };

    bool shouldSkip(const std::string& boneName) const;
};

/// Complete definition of a character's body plan.
/// Combines skeleton hierarchy, visual model, appearance, and joint constraints.
/// This is the shared representation that both Animated and Physics drive modes consume.
struct CharacterSkeleton {
    Skeleton skeleton;             // Bone hierarchy + bind pose
    VoxelModel voxelModel;         // Visual shapes per bone
    CharacterAppearance appearance; // Colors + proportions
    BoneFilterConfig boneFilter;    // Which bones to skip for physics

    /// Joint definitions between bone pairs.
    /// Key: child bone ID → joint connecting it to its parent.
    std::map<int, CharacterJointDef> jointDefs;

    /// Computed body sizes for each bone (for physics body creation).
    /// Key: bone ID → half-extents for a box shape.
    std::map<int, glm::vec3> boneSizes;
    /// Computed body offsets for each bone (center offset from bone pivot).
    std::map<int, glm::vec3> boneOffsets;
    /// Mass per bone (computed from volume × density).
    std::map<int, float> boneMasses;

    /// Load from a .anim file. Populates skeleton, voxelModel, and auto-generates
    /// jointDefs + boneSizes from the skeleton hierarchy.
    bool loadFromAnimFile(const std::string& filePath);

    /// Auto-generate joint definitions from skeleton hierarchy using heuristics
    /// (shoulder=cone-twist, elbow=hinge, hip=cone-twist, knee=hinge, etc.).
    /// Called automatically by loadFromAnimFile(). Can be called again after
    /// modifying the skeleton.
    void generateJointDefs();

    /// Compute bone sizes and offsets from skeleton hierarchy (bone-to-child vectors).
    /// Called automatically by loadFromAnimFile().
    void computeBoneSizes();

    /// Get the list of bone IDs that pass the filter (used for physics body creation).
    std::vector<int> getPhysicsBoneIds() const;

    /// Build the children map (parent ID → list of child IDs).
    std::map<int, std::vector<int>> buildChildrenMap() const;
};

} // namespace Scene
} // namespace Phyxel
