#pragma once

#include "scene/RagdollCharacter.h"
#include "scene/CharacterSkeleton.h"
#include "scene/CharacterAppearance.h"
#include "scene/PhysicsDriveMode.h"
#include "graphics/AnimationSystem.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

namespace Phyxel {
namespace Scene {

/// How the character's bones are driven each frame.
enum class DriveMode {
    Animated,       ///< Kinematic bones positioned from keyframe animation (lightweight NPCs)
    Physics,        ///< Active ragdoll with PID-controlled joints (player, important NPCs)
    PassiveRagdoll  ///< All motors off, gravity only (death, being thrown, unconscious)
};

/// Unified character class that supports both animated (kinematic) and physics
/// (active ragdoll) drive modes using the same skeleton and appearance.
///
/// This merges the capabilities of AnimatedVoxelCharacter and PhysicsCharacter's
/// ragdoll system under one interface. The drive mode can be switched at runtime
/// with smooth transitions:
///   Animated → Physics:  snapshot bone poses → create dynamic bodies → motors track pose
///   Physics → Animated:  snapshot physics pose → switch to kinematic → blend to animation
///   Any → PassiveRagdoll: disable all joint motors, bodies go limp
class VoxelCharacter : public RagdollCharacter {
public:
    VoxelCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position);
    ~VoxelCharacter() override;

    // ---- Loading ----

    /// Load skeleton, voxel model, and animations from a .anim file.
    bool loadModel(const std::string& animFile);

    /// Load from pre-loaded skeleton data (template-based, no file re-read).
    bool loadFromData(const Skeleton& skeleton, const VoxelModel& model,
                      const std::vector<AnimationClip>& clips);

    // ---- Appearance ----

    void setAppearance(const CharacterAppearance& appearance);
    const CharacterAppearance& getAppearance() const { return appearance_; }
    void recolorFromAppearance();

    // ---- Drive mode ----

    /// Get the current drive mode.
    DriveMode getDriveMode() const { return driveMode_; }

    /// Switch drive mode. Transitions capture the current pose so the switch is seamless.
    void setDriveMode(DriveMode mode);

    /// Access the physics drive mode (only valid when in Physics or PassiveRagdoll mode).
    PhysicsDriveMode* getPhysicsDrive() { return physicsDrive_.get(); }
    const PhysicsDriveMode* getPhysicsDrive() const { return physicsDrive_.get(); }

    // ---- Entity interface ----

    void update(float deltaTime) override;
    void render(Graphics::RenderCoordinator* renderer) override;
    void setPosition(const glm::vec3& pos) override;
    glm::vec3 getPosition() const override;
    void setMoveVelocity(const glm::vec3& velocity) override;

    // ---- Animation control (Animated mode) ----

    void playAnimation(const std::string& animName);
    std::vector<std::string> getAnimationNames() const;
    void cycleAnimation(bool next);

    void setAnimationMapping(const std::string& stateName, const std::string& animName);
    std::string getAnimationMapping(const std::string& stateName) const;

    // ---- Character control ----

    void setControlInput(float forward, float turn, float strafe = 0.0f);
    void setSprint(bool sprint);
    void jump();
    void attack();
    void setCrouch(bool crouch);

    // ---- Skeleton access ----

    /// Get the shared CharacterSkeleton (includes hierarchy, shapes, joint defs).
    const CharacterSkeleton& getCharacterSkeleton() const { return characterSkeleton_; }

    /// Access loaded animation clips (for physics mode motor targets).
    const std::vector<AnimationClip>& getAnimationClips() const { return clips_; }
    const Skeleton& getSkeleton() const { return characterSkeleton_.skeleton; }

    // ---- Health (inherited from RagdollCharacter) ----

private:
    // ---- Mode transitions ----
    void transitionToAnimated();
    void transitionToPhysics();
    void transitionToPassiveRagdoll();

    // ---- Animated mode internals ----
    void updateAnimated(float deltaTime);
    void updateStateMachine(float deltaTime);
    void addVoxelBone(const std::string& boneName, const glm::vec3& size,
                      const glm::vec3& offset, const glm::vec4& color);
    void buildAnimatedBodies();
    void updateBoneTransforms();

    // ---- State ----
    DriveMode driveMode_ = DriveMode::Animated;
    CharacterSkeleton characterSkeleton_;
    CharacterAppearance appearance_;
    std::vector<AnimationClip> clips_;
    AnimationSystem animSystem_;
    bool modelLoaded_ = false;

    // Animated mode state
    std::map<int, btRigidBody*> boneBodies_;
    std::map<int, glm::vec3> boneOffsets_;
    int currentClipIndex_ = -1;
    float animTime_ = 0.0f;
    int previousClipIndex_ = -1;
    float previousAnimTime_ = 0.0f;
    float blendFactor_ = 0.0f;
    float blendDuration_ = 0.2f;
    bool isBlending_ = false;
    glm::vec3 worldPosition_;

    // Animation mapping (state name → clip name)
    std::map<std::string, std::string> animationMapping_;
    std::map<std::string, float> animationRotationOffsets_;
    std::map<std::string, glm::vec3> animationPositionOffsets_;

    // State machine
    enum class CharState {
        Idle, Walk, Run, Jump, Fall, Land, Crouch, CrouchIdle,
        CrouchWalk, StandUp, Attack, Preview
    };
    CharState charState_ = CharState::Idle;
    bool isSprinting_ = false;
    bool isCrouching_ = false;
    bool jumpRequested_ = false;
    bool attackRequested_ = false;
    float stateTimer_ = 0.0f;

    // Control inputs
    float forwardInput_ = 0.0f;
    float turnInput_ = 0.0f;
    float strafeInput_ = 0.0f;
    float currentYaw_ = 0.0f;

    // Kinematic controller body (for animated mode ground collision)
    btRigidBody* controllerBody_ = nullptr;
    void createController(const glm::vec3& position);

    // Physics drive mode (created on demand)
    std::unique_ptr<PhysicsDriveMode> physicsDrive_;

    std::string charStateToString(CharState state) const;
};

} // namespace Scene
} // namespace Phyxel
