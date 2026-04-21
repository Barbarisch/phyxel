#pragma once

#include "scene/Entity.h"
#include "scene/CharacterSkeleton.h"
#include "scene/ProceduralSkeleton.h"
#include "scene/active/KinematicBody.h"
#include "scene/active/LimbIK.h"
#include "scene/active/LocomotionController.h"
#include "core/HealthComponent.h"
#include "physics/PhysicsWorld.h"
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace Phyxel {

namespace Input    { class InputManager; }
namespace Graphics { class Camera; class RenderCoordinator; }

namespace Scene {

/// One visual box shape with a complete world-space transform.
/// Produced by ActiveCharacter::rebuildShapes() each frame — no rigid body.
struct ActiveRenderShape {
    glm::mat4 worldTransform{1.0f};  ///< T(worldCenter) * R(worldRotation)
    glm::vec3 size{0.1f};            ///< Full box extents (matches BoneShape::size)
    glm::vec4 color{1.0f};
};

/// Debug visualization toggles for ActiveCharacter.
struct ActiveDebugVis {
    bool showVoxels      = true;   ///< Render the model voxels (the 1116 tiny cubes)
    bool showBones       = false;  ///< Render bones as colored boxes at joint positions
    bool showBoneAxes    = false;  ///< Render local X/Y/Z axes at each bone
    bool showIKTargets   = false;  ///< Render foot target positions and step arcs
    bool showPoleVectors = false;  ///< Render pole vector direction from each knee
    float boneBoxSize    = 0.04f;  ///< Size of bone joint indicator boxes
    float axisLength     = 0.15f;  ///< Length of axis indicator lines
    float axisThickness  = 0.015f; ///< Thickness of axis indicator boxes
    float poleVecLength  = 0.3f;   ///< Length of pole vector indicator line
};

/// Biomechanics-sourced defaults. All tunable at runtime via poseConfig() + ImGui sliders.
/// Sources: Rain World locomotion, Overgrowth movement code, Winter 2009 gait data.
struct PoseBuilderConfig {
    // ---- Character speeds (m/s) ----
    float walkSpeed   = 1.4f;    ///< Comfortable walk — Winter 2009 mean: 1.37 m/s
    float runSpeed    = 3.5f;    ///< Easy run cadence ~150 steps/min
    float sprintSpeed = 6.0f;    ///< Full sprint

    // ---- Foot placement ----
    float stepThreshold    = 0.30f;   ///< Foot drifts this far (m) before triggering a step
    float stepHeightWalk   = 0.10f;   ///< Arc peak height during walk step (m)
    float stepHeightRun    = 0.18f;   ///< Arc peak height during run step (m)
    float stepDurationWalk = 0.22f;   ///< Walk step duration (s) — 100 steps/min
    float stepDurationRun  = 0.15f;   ///< Run step duration (s)  — 150 steps/min

    // ---- Hip / pelvis motion ----
    float hipBobAmplitude = 0.025f;  ///< Vertical pelvis bob amplitude (m) per step
    float hipBobRunMult   = 2.0f;    ///< Bob is this × larger when running
    float hipRollDeg      = 7.0f;    ///< Lateral pelvis roll angle per step (degrees)
    float pelvisSpring    = 10.0f;   ///< Pelvis-follow spring constant (units/s²)
    float pelvisHeight    = 0.85f;   ///< Rest height above feet — auto-derived from skeleton

    // ---- Lean (velocity-based) ----
    float leanMultiplier  = 0.50f;   ///< Degrees of lean per m/s² of acceleration
    float maxLeanDeg      = 12.0f;   ///< Maximum forward lean (degrees)
    float leanSmoothing   = 0.20f;   ///< Smoothing factor: fraction remaining per 1/60 s frame

    // ---- Spine counter-rotation ----
    float spineCounterFrac = 0.55f;  ///< Spine bone absorbs this fraction of pelvis lean
    float chestCounterFrac = 0.30f;  ///< Chest bone absorbs additional fraction

    // ---- Arm pose ----
    float armRestAngleDeg = 80.0f;   ///< Base arm drop from T-pose (degrees, applied around local X)
    float armSwingDeg    = 28.0f;    ///< Peak arm swing angle (degrees)
    float armSwingRunMult = 1.6f;    ///< Swing multiplier during run
    float forearmBendDeg = 18.0f;    ///< Passive forearm/elbow bend (degrees)

    // ---- Jump ----
    float jumpImpulse = 6.5f;        ///< Vertical velocity set on jump (m/s)

    // ---- Crouch ----
    float crouchPelvisRatio = 0.6f;  ///< Pelvis drops to this fraction of standing height
    float crouchHipTiltDeg  = 15.0f; ///< Forward hip tilt when crouching (degrees)

    // ---- Landing squash ----
    float landSquashDepth    = 0.15f; ///< Max pelvis dip on landing (m)
    float landSquashDuration = 0.25f; ///< Duration of landing squash (s)

    // ---- Strafe lean ----
    float strafeLeanDeg = 5.0f;      ///< Body tilt into sustained strafe (degrees)
};

/// Kinematic-capsule + procedural-skeleton character.
///
/// Architecture:
///   KinematicBody        — single Bullet capsule: gravity, collision, jump
///   LocomotionController — state machine, walk phase, step triggers
///   LimbStepper × N      — terrain-adaptive foot placement via raycasts
///   updatePose()         — pelvis spring, lean, hip bob, FK spine/arms, IK legs
///   rebuildShapes()      — emits ActiveRenderShape vector for the renderer each frame
///
/// Bodies are NEVER teleported for visual poses — all transforms come from math.
/// The capsule handles physical collision; shapes are visual only.
class ActiveCharacter : public Entity {
public:
    ActiveCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position);
    ~ActiveCharacter() override;

    ActiveCharacter(const ActiveCharacter&) = delete;
    ActiveCharacter& operator=(const ActiveCharacter&) = delete;

    // ---- Loading ----

    /// Load skeleton and visual shapes from a .anim file (SKELETON + MODEL sections only).
    bool loadFromAnimFile(const std::string& path);

    /// Generate a humanoid skeleton procedurally.
    bool loadProcedural(const HumanoidParams& params = {});

    /// Generate a quadruped skeleton procedurally.
    bool loadProcedural(const QuadrupedParams& params);

    /// Generate an arachnid skeleton procedurally.
    bool loadProcedural(const ArachnidParams& params);

    // ---- Player control ----

    void setControlActive(bool active) { controlled_ = active; }
    bool isControlActive()       const { return controlled_; }
    void attachInput(Input::InputManager* input, Graphics::Camera* camera);

    // ---- NPC / AI input ----

    void setMoveInput(float forward, float strafe, float turn);
    void setJumpInput(bool jump);
    void setCrouchInput(bool crouch);
    void setSprintInput(bool sprint);

    // ---- Entity interface ----

    void      update(float deltaTime) override;
    void      render(Graphics::RenderCoordinator* renderer) override;
    void      setPosition(const glm::vec3& pos) override;
    glm::vec3 getPosition() const override;

    // ---- Health ----

    Core::HealthComponent*       getHealthComponent()       override { return health_.get(); }
    const Core::HealthComponent* getHealthComponent() const override { return health_.get(); }

    // ---- Renderer interface ----

    /// Updated each frame by rebuildShapes(). Pass to RenderCoordinator.
    const std::vector<ActiveRenderShape>& getShapes() const { return shapes_; }

    // ---- Runtime tuning ----

    PoseBuilderConfig&       poseConfig()       { return poseConfig_; }
    const PoseBuilderConfig& poseConfig() const { return poseConfig_; }

    ActiveDebugVis&       debugVis()       { return debugVis_; }
    const ActiveDebugVis& debugVis() const { return debugVis_; }

    LocomotionState getLocomotionState() const { return locomotion_.getState(); }
    KinematicBody&  getKinematic()             { return kinematic_; }

    // ---- Debug data access ----
    const CharacterSkeleton&     getSkeleton() const { return skeleton_; }
    const std::map<int, glm::mat4>& getBoneWorldTransforms() const { return boneWorldTransforms_; }
    const std::vector<LimbStepper>& getLimbs() const { return limbs_; }
    std::vector<LimbStepper>& getLimbs() { return limbs_; }

private:
    // ---- Build helpers ----
    void buildFromSkeleton();
    void buildLimbSteppers();
    void cachePoseBonesIds();

    // ---- Per-frame ----
    void processPlayerInput(float dt);

    /// Compute all bone world-space 4×4 transforms.
    /// FK from pelvis down; IK overrides for leg chains.
    void updatePose(float dt, float walkPhase, float speedFraction,
                    bool grounded, float vertVel);

    /// Walk boneWorldTransforms_ + voxelModel.shapes → shapes_.
    void rebuildShapes();

    // ---- Members ----

    Physics::PhysicsWorld* physicsWorld_;

    KinematicBody          kinematic_;
    CharacterSkeleton      skeleton_;
    LocomotionController   locomotion_;
    std::vector<LimbStepper> limbs_;
    std::unique_ptr<Core::HealthComponent> health_;

    // Per-frame procedural pose
    std::map<int, glm::mat4> boneWorldTransforms_;
    std::vector<ActiveRenderShape> shapes_;
    PoseBuilderConfig poseConfig_;
    ActiveDebugVis    debugVis_;

    // Smooth locomotion state
    glm::vec3 pelvisSmooth_{0.0f};
    float     smoothLeanFwd_  = 0.0f;
    float     smoothLeanSide_ = 0.0f;
    glm::vec3 prevVelocity_{0.0f};
    float     bodyYaw_        = 0.0f;

    // Cached bone IDs for procedural joints (set by cachePoseBoneIds)
    int boneIdSpine_  = -1;   ///< First spine bone
    int boneIdChest_  = -1;   ///< Chest / Spine1
    int boneIdLArm_   = -1;   ///< LeftArm (upper)
    int boneIdRArm_   = -1;   ///< RightArm (upper)
    int boneIdLFore_  = -1;   ///< LeftForeArm
    int boneIdRFore_  = -1;   ///< RightForeArm

    bool controlled_           = false;
    Input::InputManager* inputManager_ = nullptr;
    Graphics::Camera*    camera_       = nullptr;

    LocomotionInput currentInput_{};

    bool modelLoaded_   = false;
    glm::vec3 spawnPosition_{0.0f};
    bool prevGrounded_  = false;
    float prevVertVel_  = 0.0f;

    // Landing squash runtime state
    float landTimer_  = 0.0f;  ///< Counts down from landSquashDuration on land
    float landImpact_ = 0.0f;  ///< 0-1 normalized impact strength
};

} // namespace Scene
} // namespace Phyxel
