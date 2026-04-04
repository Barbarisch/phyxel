#pragma once
#include "scene/RagdollCharacter.h"
#include "scene/CharacterAppearance.h"
#include "scene/CharacterSkeleton.h"
#include "graphics/AnimationSystem.h"
#include "physics/PhysicsWorld.h"
#include <map>
#include <string>
#include <vector>

namespace Phyxel {
    class ChunkManager;       // Forward declaration for voxel collision queries
    class RaycastVisualizer;  // Forward declaration for F5 debug visualization
}

namespace Phyxel {
namespace Scene {

    struct VoxelBoneMapping {
        std::string boneName;
        glm::vec3 size; // Size of the voxel box for this bone
        glm::vec3 offset; // Offset from bone pivot
        glm::vec4 color;
    };

    enum class AnimatedCharacterState {
        Idle,
        StartWalk,
        Walk,
        Run,
        Jump,
        Fall,
        Land,
        Crouch,
        CrouchIdle,
        CrouchWalk,
        StandUp,
        Attack,
        TurnLeft,
        TurnRight,
        StrafeLeft,
        StrafeRight,
        WalkStrafeLeft,
        WalkStrafeRight,
        BackwardWalk,
        StopWalk,
        StopRun,
        ClimbStairs,
        DescendStairs,
        SitDown,      // playing sit-down transition animation
        SittingIdle,  // looping seated idle
        SitStandUp,   // playing stand-up-from-seat animation
        Preview
    };

    class AnimatedVoxelCharacter : public RagdollCharacter {
    public:
        AnimatedVoxelCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position);
        virtual ~AnimatedVoxelCharacter();

        // Load skeleton and animations from .anim file
        bool loadModel(const std::string& animFile);

        /// Load from in-memory skeleton data (for procedural/template-based characters).
        /// Avoids re-reading .anim files when spawning from a cached template.
        bool loadFromSkeleton(const Phyxel::Skeleton& skel, const Phyxel::VoxelModel& model,
                              const std::vector<Phyxel::AnimationClip>& animations);
        
        // Set character appearance (colors + proportions). Call before loadModel().
        void setAppearance(const CharacterAppearance& appearance);
        const CharacterAppearance& getAppearance() const { return appearance_; }

        // Re-apply appearance colors to all existing parts (use after setAppearance on already-loaded character).
        void recolorFromAppearance();

        /// Rebuild character with new appearance proportions (destroys and recreates physics bodies).
        /// Call setAppearance() first, then this.
        void rebuildWithAppearance(const CharacterAppearance& appearance);
        
        // Attach a voxel shape (represented as a box for now) to a bone
        void addVoxelBone(const std::string& boneName, const glm::vec3& size, const glm::vec3& offset, const glm::vec4& color);
        
        void update(float deltaTime) override;
        void render(Graphics::RenderCoordinator* renderer) override; 

        void playAnimation(const std::string& animName);
        std::vector<std::string> getAnimationNames() const;
        void cycleAnimation(bool next);
        void setPosition(const glm::vec3& pos);
        glm::vec3 getPosition() const;
        float getControllerHalfHeight() const;
        float getControllerHalfWidth() const;

        // Voxel collision: set the chunk manager for per-limb voxel collision queries
        void setChunkManager(Phyxel::ChunkManager* cm) { m_chunkManager = cm; }

        // Debug visualization: wire up the F5 RaycastVisualizer to see segment boxes
        void setRaycastVisualizer(Phyxel::RaycastVisualizer* viz) { m_raycastVisualizer = viz; }

        // Debug query: returns per-segment box info (count, sizes, positions, collision state)
        struct SegmentBoxInfo {
            std::string boneName;
            glm::vec3 halfExtents;
            glm::vec3 position;
            bool isArm;
            bool colliding;
        };
        std::vector<SegmentBoxInfo> getSegmentBoxInfo() const;

        // Animation state queries
        AnimatedCharacterState getAnimationState() const { return currentState; }
        std::string getCurrentClipName() const;
        float getAnimationProgress() const;
        float getAnimationDuration() const;
        float getYaw() const { return currentYaw; }
        glm::vec3 getForwardDirection() const;

        // Configurable blend duration
        void setBlendDuration(float duration) { blendDuration = duration; }
        float getBlendDuration() const { return blendDuration; }

        // Force a specific state machine state
        void setAnimationState(AnimatedCharacterState state);

        // Hot-reload animation clips from file (skeleton/model unchanged)
        bool reloadAnimations(const std::string& animFile);

        // Voxel model access (for anim editor)
        const Phyxel::VoxelModel& getVoxelModel() const { return voxelModel; }
        void setVoxelModel(const Phyxel::VoxelModel& model);

        // Sitting
        /// Move character to sit on a seat whose surface is at seatSurfacePos,
        /// facing facingYaw degrees. Scale-aware: aligns hip bone to seat surface.
        void sitAt(const glm::vec3& seatSurfacePos, float facingYaw);
        /// Begin standing up from seated state.
        void standUp();
        bool isSitting() const { return m_isSitting; }

        // ---- Bone Attachments (weapons, equipment visuals) ----

        /// Attach an extravoxel shape to a named bone (e.g. right_hand).
        /// Returns an attachment ID for later removal.
        int attachToBone(const std::string& boneName, const glm::vec3& size,
                         const glm::vec3& offset, const glm::vec4& color,
                         const std::string& label = "");

        /// Remove a bone attachment by ID.
        void detachFromBone(int attachmentId);

        /// Remove all bone attachments.
        void detachAll();

        /// Check if any attachments exist.
        bool hasAttachments() const { return !m_attachments.empty(); }

        // State string conversion (public)
        static AnimatedCharacterState stringToState(const std::string& str);
        std::string stateToString(AnimatedCharacterState state) const;

        // Configurable step-up height
        void setMaxStepHeight(float height) { m_maxStepHeight = height; }
        float getMaxStepHeight() const { return m_maxStepHeight; }

        /// Set horizontal movement velocity (XZ), preserving vertical velocity (gravity).
        void setMoveVelocity(const glm::vec3& velocity);
        
        // Control inputs
        void setControlInput(float forward, float turn, float strafe = 0.0f);
        void setSprint(bool sprint);
        void jump();
        void attack();
        void setCrouch(bool crouch);

        // ---- Hit Frame Callback (for combat integration) ----

        /// Set the fraction (0.0-1.0) of the attack animation at which the hit triggers.
        void setHitFrameFraction(float fraction) { m_hitFrameFraction = fraction; }
        float getHitFrameFraction() const { return m_hitFrameFraction; }

        /// Set callback fired once per attack when the hit frame is reached.
        using OnHitFrameCallback = std::function<void()>;
        void setOnHitFrame(OnHitFrameCallback cb) { m_onHitFrame = std::move(cb); }

        // Animation Mapping
        void setAnimationMapping(const std::string& stateName, const std::string& animName);
        std::string getAnimationMapping(const std::string& stateName) const;
        void setAnimationRotationOffset(const std::string& animName, float rotationDegrees);
        void setAnimationPositionOffset(const std::string& animName, const glm::vec3& offset);

        // Build a CharacterSkeleton from the currently loaded model.
        // Includes skeleton hierarchy, voxel model, appearance, bone sizes, and auto-generated joint defs.
        CharacterSkeleton buildCharacterSkeleton() const;

        // Get world-space AABBs for each visible bone (for voxel-accurate hit detection).
        struct BoneAABB {
            int boneId;
            std::string boneName;
            glm::vec3 center;
            glm::vec3 halfExtents;
        };
        std::vector<BoneAABB> getBoneAABBs() const;

        // Access the loaded animation clips (for use as motor targets in physics mode)
        const std::vector<Phyxel::AnimationClip>& getAnimationClips() const { return clips; }
        const Phyxel::Skeleton& getSkeleton() const { return skeleton; }

    private:
        Phyxel::Skeleton skeleton;
        std::vector<Phyxel::AnimationClip> clips;
        Phyxel::VoxelModel voxelModel;
        Phyxel::AnimationSystem animSystem;
        
        // Map from Bone ID to the physics body representing it
        // Note: The bodies are also stored in RagdollCharacter::parts for rendering/cleanup
        std::map<int, btRigidBody*> boneBodies; 
        // Map from Bone ID to the visual offset from the bone pivot
        std::map<int, glm::vec3> boneOffsets; 
        
        // Per-animation rotation offsets (to fix bad imports)
        std::map<std::string, float> animationRotationOffsets;
        // Per-animation position offsets (to fix alignment issues)
        std::map<std::string, glm::vec3> animationPositionOffsets;
        // User-defined mapping from State Name to Animation Name
        std::map<std::string, std::string> animationMapping;

        int currentClipIndex = -1;
        float animTime = 0.0f;
        
        // Blending support
        int previousClipIndex = -1;
        float previousAnimTime = 0.0f;
        float blendFactor = 0.0f;
        float blendDuration = 0.2f;
        bool isBlending = false;

        glm::vec3 worldPosition;
        
        // State Machine
        AnimatedCharacterState currentState = AnimatedCharacterState::Idle;
        bool isSprinting = false;
        bool isCrouching = false;

        bool jumpRequested = false;
        bool attackRequested = false;
        float stateTimer = 0.0f;
        bool m_hitFrameFired = false;       // Has the hit frame triggered for current attack?
        float m_hitFrameFraction = 0.4f;    // Default: 40% through attack animation
        OnHitFrameCallback m_onHitFrame;

        // Physics Controller
        btRigidBody* controllerBody = nullptr;
        float currentForwardInput = 0.0f;
        float currentTurnInput = 0.0f;
        float currentStrafeInput = 0.0f;
        float currentYaw = 0.0f;

        // External velocity override (used by NPC patrol behavior)
        glm::vec3 externalVelocity{0.0f};
        bool hasExternalVelocity = false;

        // Step-up detection (blocked-frame gated + velocity impulse)
        float m_maxStepHeight = 1.0f;  // Max obstacle height character can step over (in world units)
        float m_stepCooldown = 0.0f;   // cooldown timer to prevent re-triggering
        glm::vec3 m_prevStepCheckPos{0.0f};  // position from previous frame for blocked detection
        int m_blockedFrames = 0;              // consecutive frames where movement is blocked

        // Step-up debug ring buffer
        struct StepDebugEntry {
            float timestamp = 0.0f;
            float charX = 0.0f, charY = 0.0f, charZ = 0.0f;
            float obstacleHeight = 0.0f;
            float stepHeight = 0.0f;
            std::string result;  // "stepped", "blocked", "cooldown", "no_obstacle"
            int blockedFrames = 0;
        };
        static constexpr size_t STEP_LOG_MAX = 50;
        std::vector<StepDebugEntry> m_stepDebugLog;
        float m_totalTime = 0.0f;  // running clock for timestamps

    public:
        const std::vector<StepDebugEntry>& getStepDebugLog() const { return m_stepDebugLog; }
        void clearStepDebugLog() { m_stepDebugLog.clear(); }
    private:
        
        void createController(const glm::vec3& position);
        void resizeController();  // Resize controller body to match scaled skeleton height
        float computeSkeletonHeight() const;  // Compute Y-extent of scaled skeleton
        void rebuildCompoundShape();  // Build/rebuild compound controller shape from bones
        void updateCompoundTransforms();  // Update compound child transforms from bone positions
        void updateStateMachine(float deltaTime);
        void detectAndApplyStepUp(const glm::vec3& desiredVelocity, float deltaTime, btDynamicsWorld* physicsWorld);
        void configureAnimationFixes();
        void applySkeletonProportions();  // Scale skeleton joints + anim keyframes per appearance
        void buildBodiesFromModel();  // Builds physics + visual bodies from skeleton/voxelModel
        void clearBodies();  // Remove all physics bodies for rebuild
        void resolveBodyPartCollisions();  // Per-limb voxel collision push-out

        // Appearance (colors + proportions)
        CharacterAppearance appearance_;

        // Vertical offset from model-space origin to the lowest bone (feet).
        // Used to align visual feet with the bottom of the controller box.
        float skeletonFootOffset_ = 0.0f;

        // Original unscaled template data (for rebuilding with different proportions)
        Phyxel::Skeleton originalSkeleton_;
        Phyxel::VoxelModel originalVoxelModel_;
        std::vector<Phyxel::AnimationClip> originalClips_;
        bool hasOriginalTemplate_ = false;

        // ---- Bone Attachments ----
        struct BoneAttachment {
            int id;
            int boneId;              // skeleton bone ID to follow
            btRigidBody* body;       // kinematic physics body
            glm::vec3 size;
            glm::vec3 offset;        // offset from bone pivot
            glm::vec4 color;
            std::string label;
        };
        std::vector<BoneAttachment> m_attachments;
        int m_nextAttachmentId = 1;

        // Compound shape for voxel-accurate controller collision
        btCompoundShape* m_compoundShape = nullptr;
        std::vector<btBoxShape*> m_compoundChildShapes;  // Owned child shapes
        std::map<int, int> m_boneToCompoundChild;        // boneId → compound child index
        float m_originalHalfHeight = 0.95f;              // Original box half-height (for movement)
        float m_originalHalfWidth = 0.425f;              // Original box half-width (for movement)

        // Sitting state
        bool m_isSitting = false;
        glm::vec3 m_seatRootPos{0.0f};  // pinned world root position while seated
        float m_seatFacingYaw = 0.0f;

        // Per-limb voxel collision
        Phyxel::ChunkManager* m_chunkManager = nullptr;

        // 8-segment collision boxes — one per major body segment, follow animation pose
        struct SegmentBox {
            std::string boneName;
            int boneId = -1;              // cached skeleton bone ID
            btRigidBody* body = nullptr;
            glm::vec3 halfExtents{0.0f};  // 80% of source bone box half-extents
            bool isArm = false;           // arm segments trigger LimbBlocked FSM interrupt
            bool colliding = false;       // set each frame by checkSegmentVoxelOverlap()
        };
        std::vector<SegmentBox> m_segmentBoxes;
        bool m_limbBlocked = false;  // true this frame if any arm segment overlaps a voxel

        // F5 debug visualization
        Phyxel::RaycastVisualizer* m_raycastVisualizer = nullptr;

        void buildSegmentBoxes();          // called once after buildBodiesFromModel()
        void clearSegmentBoxes();          // called in clearBodies() and destructor
        void updateSegmentBoxes();         // sync segment box transforms to animated pose each frame
        void checkSegmentVoxelOverlap();   // detect arm/limb overlapping world voxels
        void drawSegmentBoxDebug();        // draw wireframe boxes via RaycastVisualizer when F5 on
    };
}
}
