#pragma once
#include "scene/AnimatedVoxelCharacter.h"
#include <vector>

namespace Phyxel {
namespace Scene {

/// IK limb chain descriptor — identifies a two-bone chain in the skeleton.
struct LimbChain {
    int upperBoneId = -1;   ///< Hip or shoulder
    int midBoneId   = -1;   ///< Knee or elbow
    int endBoneId   = -1;   ///< Foot or hand
    float upperLen  = 0.0f; ///< Upper segment length (bind pose)
    float lowerLen  = 0.0f; ///< Lower segment length (bind pose)
};

/// Per-foot IK runtime state.
struct FootIKState {
    glm::vec3 groundTarget{0.0f};   ///< Raycast-determined ground contact point
    float blendWeight = 1.0f;       ///< 0 = pure keyframe, 1 = full IK
    bool grounded = true;           ///< Whether the foot found ground
};

/// IK correction toggles and parameters.
struct IKSettings {
    // Foot IK
    bool footIKEnabled    = true;
    float footIKWeight    = 1.0f;
    float footRaycastUp   = 1.5f;   ///< Raycast origin offset above foot
    float footRaycastDown = 2.5f;   ///< Raycast total distance downward

    // Lean
    bool leanEnabled       = true;
    float leanWeight       = 1.0f;
    float leanMultiplier   = 0.5f;   ///< Degrees of lean per m/s² acceleration
    float leanSmoothSpeed  = 8.0f;
    float spineCounterFrac = 0.55f;  ///< Spine absorbs this fraction of pelvis lean

    // Look-at
    bool lookAtEnabled    = false;
    float lookAtWeight    = 1.0f;
    float lookAtMaxAngle  = 70.0f;   ///< Max horizontal rotation (degrees)
    float lookAtSmoothSpeed = 5.0f;
    float neckWeight      = 0.6f;    ///< Fraction of rotation on neck vs head

    // Hand IK
    bool leftHandIKEnabled  = false;
    bool rightHandIKEnabled = false;
    float handIKWeight      = 1.0f;
};

/// HybridCharacter — AnimatedVoxelCharacter with procedural IK corrections.
///
/// Extends AnimatedVoxelCharacter with foot placement IK, acceleration lean,
/// look-at, and hand IK. Each IK pass is independent and toggleable.
/// Visually identical to AnimatedVoxelCharacter — IK only modifies bone
/// transforms between keyframe sampling and bone body sync.
class HybridCharacter : public AnimatedVoxelCharacter {
public:
    HybridCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position);
    ~HybridCharacter() override = default;

    /// Call after loadModel() to detect limb chains and cache bone IDs for IK.
    void initIK();

    // IK settings — full read/write access for ImGui tuning
    IKSettings& ikSettings() { return ikSettings_; }
    const IKSettings& ikSettings() const { return ikSettings_; }

    // Look-at target
    void setLookTarget(const glm::vec3& target) { lookTarget_ = target; ikSettings_.lookAtEnabled = true; }
    void clearLookTarget() { ikSettings_.lookAtEnabled = false; }

    // Hand IK targets
    void setLeftHandTarget(const glm::vec3& t)  { leftHandTarget_ = t; ikSettings_.leftHandIKEnabled = true; }
    void setRightHandTarget(const glm::vec3& t) { rightHandTarget_ = t; ikSettings_.rightHandIKEnabled = true; }
    void clearLeftHandTarget()  { ikSettings_.leftHandIKEnabled = false; }
    void clearRightHandTarget() { ikSettings_.rightHandIKEnabled = false; }

    // Limb chain info (for debug/ImGui)
    const std::vector<LimbChain>& getLegChains() const { return legChains_; }
    const std::vector<LimbChain>& getArmChains() const { return armChains_; }

protected:
    /// IK injection — called after keyframe sampling + updateGlobalTransforms,
    /// before bone body sync.
    void applyIKCorrections(float deltaTime) override;

private:
    // IK state
    IKSettings ikSettings_;
    std::vector<LimbChain> legChains_;
    std::vector<LimbChain> armChains_;
    std::vector<FootIKState> footIKStates_;  ///< Parallel to legChains_

    // Lean state (smoothed)
    float leanForward_ = 0.0f;
    float leanSide_ = 0.0f;
    glm::vec3 prevVelocity_{0.0f};

    // Look-at
    glm::vec3 lookTarget_{0.0f};
    float currentLookYaw_ = 0.0f;
    float currentLookPitch_ = 0.0f;

    // Hand IK
    glm::vec3 leftHandTarget_{0.0f};
    glm::vec3 rightHandTarget_{0.0f};

    // Bone name → ID cache for IK passes
    int hipsBoneId_ = -1;
    int spineBoneId_ = -1;
    int chestBoneId_ = -1;
    int neckBoneId_ = -1;
    int headBoneId_ = -1;

    // IK passes
    void applyFootIK();
    void applyLean(float deltaTime);
    void applyLookAt(float deltaTime);
    void applyHandIK();

    // Limb chain auto-detection from skeleton bone names
    void detectLimbChains();
    float computeBoneLength(int parentId, int childId) const;
};

} // namespace Scene
} // namespace Phyxel
