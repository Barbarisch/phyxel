#pragma once
#include "scene/RagdollCharacter.h"
#include "scene/CharacterAppearance.h"
#include "scene/CharacterSkeleton.h"
#include "scene/VoxelContactProbe.h"
#include "graphics/AnimationSystem.h"
#include "physics/PhysicsWorld.h"
#include <map>
#include <string>
#include <vector>
#include <optional>
#include <unordered_set>

namespace Phyxel {
    class ChunkManager;       // Forward declaration for voxel collision queries
    class RaycastVisualizer;  // Forward declaration for F5 debug visualization
    class GpuParticlePhysics; // Forward declaration for derez GPU particle spawning
}

namespace Phyxel {
namespace Scene {

    struct VoxelBoneMapping {
        std::string boneName;
        glm::vec3 size; // Size of the voxel box for this bone
        glm::vec3 offset; // Offset from bone pivot
        glm::vec4 color;
    };

    enum class DerezPattern {
        Wave,       // voxels fall from feet upward
        Periphery,  // extremities crumble inward toward center
        Random,     // random staggered scatter
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

        // Animation playback control (for editor scrubbing / pause)
        void setAnimationPaused(bool paused) {
            m_animPaused = paused;
            if (paused) m_stairDriveActive = false;
        }
        bool isAnimationPaused() const { return m_animPaused; }

        /// Arm the stair Y-drive for exactly one clip pass (called by "Test Step Down").
        /// Without this, the drive is inactive in Preview so normal playback is unaffected.
        void activateStairDrive() { m_stairDriveActive = true; m_stairDriveNeedsInit = true; }
        void seekAnimation(float normalizedTime); // 0-1, snaps pose immediately
        // Seek to a specific clip by index AND evaluate bones immediately (for editor scrubbing)
        void seekToClip(int clipIndex, float normalizedTime);
        float getAnimationTime() const { return animTime; }
        void forceState(AnimatedCharacterState state) { currentState = state; }
        void resetStateTimer() { stateTimer = 0.0f; }

        // Warp preview: combine spatial + temporal warp for editor preview.
        // extraOffsetY   = testHeight - authoredFallDist (spatial: root bone Y at t=0).
        // warpScale      = testHeight / authoredFallDist (temporal: air phase stretch).
        // takeoffEnd     = normalized time when takeoff ends (air phase begins).
        // contactFrameNorm = normalized time when feet touch ground (air phase ends).
        void setWarpPreview(bool active,
                            float extraOffsetY     = 0.0f,
                            float warpScale        = 1.0f,
                            float takeoffEnd       = 0.1f,
                            float contactFrameNorm = 0.85f) {
            m_warpPreviewActive   = active;
            m_warpPreviewExtraY   = extraOffsetY;
            m_warpPreviewScale    = warpScale;
            m_warpPreviewTakeoffN = takeoffEnd;
            m_warpPreviewContactN = contactFrameNorm;
        }
        bool  isWarpPreviewActive() const { return m_warpPreviewActive; }
        float getWarpPreviewExtraY() const { return m_warpPreviewExtraY; }

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
        /// Begin the sit-down sequence.
        /// seatAnchorPos  – world-space seat anchor; per-state foot offsets are relative to this.
        /// facingYaw      – facing direction (radians) for the whole sitting sequence.
        void sitAt(const glm::vec3& seatAnchorPos, float facingYaw,
                   const glm::vec3& sitDownOffset = glm::vec3(0.0f),
                   const glm::vec3& sittingIdleOffset = glm::vec3(0.0f),
                   const glm::vec3& sitStandUpOffset = glm::vec3(0.0f),
                   float sitBlendDuration = 0.0f,
                   float seatHeightOffset = 0.0f);
        /// Begin standing up from seated state.
        void standUp();
        bool isSitting() const { return m_isSitting; }

        /// Refresh the cached sit offsets while the character is already sitting.
        /// Used by the interaction editor when a per-character profile override
        /// is written mid-session: without this the next physics tick snaps
        /// the character back to the stale anchor position.
        void refreshSitOffsets(const glm::vec3& sitDownOffset,
                               const glm::vec3& sittingIdleOffset,
                               const glm::vec3& sitStandUpOffset,
                               float sitBlendDuration,
                               float seatHeightOffset);

        /// Teleport to destinationPos, suppress kinematic movement, play clipName to completion,
        /// then resume normal movement. The clip's t=0 pose visually hides the teleport.
        void playAnchoredAnimation(const glm::vec3& destinationPos, float facingYaw,
                                   const std::string& clipName);
        bool isPlayingAnchoredAnimation() const { return m_isAnchoredAnim; }

        /// Teleport to startPos and play clipName from t=0 with normal physics (gravity, no position freeze).
        /// Used by the anim editor to test fall/jump clips with an elevated start position.
        void playClipFromPosition(const glm::vec3& startPos, float facingYaw,
                                  const std::string& clipName);

        /// Freeze kinematic physics (gravity + ground snap) so the character stays
        /// exactly where setPosition() puts it. Used by the anim editor.
        void setKinematicFrozen(bool frozen) { m_kinFrozen = frozen; if (frozen) m_kinVelocity = glm::vec3(0.0f); }
        bool isKinematicFrozen() const { return m_kinFrozen; }
        void setPlaybackSpeed(float s) { m_playbackSpeed = std::max(0.01f, s); }
        float getPlaybackSpeed() const { return m_playbackSpeed; }
        glm::vec3 getSeatSurfacePos() const { return m_seatSurfacePos; }

        /// Sample a bone's local position from a clip at a given time. Returns
        /// glm::vec3(0) if the clip or bone has no position keyframes. Used
        /// internally to compute per-clip Hips anchor offsets for sit/stand.
        glm::vec3 sampleClipBonePos(int clipIndex, int boneIndex, float time) const;

        /// Position to use for camera follow / interaction queries. Equals
        /// worldPosition normally, but while sitting it returns the visible
        /// Hips XZ (worldPosition + rotated currentHipsLocal) so the camera
        /// doesn't jump when worldPosition snaps at sit/idle/stand clip
        /// boundaries. Y is kept as worldPosition.y (feet height).
        glm::vec3 getCameraTrackPosition() const;

        /// Interaction archetype (e.g. "humanoid_normal"). Parsed from .anim "# archetype:" header.
        const std::string& getArchetype() const { return m_archetype; }
        void setArchetype(const std::string& archetype) { m_archetype = archetype; }

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
        void jump() override;
        void attack();
        void setCrouch(bool crouch);

        // ---- Derez (falling-apart disintegration) ----

        /// Begin staggered voxel detachment. The character remains in the scene during the
        /// effect; voxels fall off one by one until the character is fully derezed.
        /// @param gpu       GPU particle system to spawn falling voxels into
        /// @param duration  Total time (seconds) from first voxel to last
        /// @param pattern   Detachment order (Wave=feet-up, Periphery=tips-in, Random)
        void beginDerez(Phyxel::GpuParticlePhysics* gpu, float duration = 2.0f,
                        DerezPattern pattern = DerezPattern::Wave);

        bool isDerezzing()    const;
        bool isFullyDerezed() const;

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

        /// Evaluate a named animation clip at a specific normalized time [0,1] and return
        /// world-space bone AABBs — without disturbing the character's live playback state.
        /// overrideWorldPos is the foot-anchor position to use during evaluation (matches the
        /// per-state sitDownOffset / sittingIdleOffset / sitStandUpOffset conventions).
        /// Returns empty vector if the clip is not found.
        std::vector<BoneAABB> sampleBoneAABBsAtTime(const std::string& clipName,
                                                     float normalizedTime,
                                                     const glm::vec3& overrideWorldPos);

        // Access the loaded animation clips (for use as motor targets in physics mode)
        const std::vector<Phyxel::AnimationClip>& getAnimationClips() const { return clips; }
        std::vector<Phyxel::AnimationClip>& getAnimationClipsMut() { return clips; }
        const Phyxel::Skeleton& getSkeleton() const { return skeleton; }

        // ---- Motion trace (per-frame ring buffer for slide/teleport detection) ----
        // Recorded at the END of every update() call. Used by the interaction pipeline
        // to verify that worldPosition does not jump unexpectedly during sit/stand
        // transitions. Includes enough state to attribute any jump to a specific gate
        // (m_isSitting, isBlending, currentClipIndex).
        struct MotionTraceEntry {
            float     totalTime;       // m_totalTime when this entry was recorded
            float     deltaTime;       // deltaTime passed to update()
            glm::vec3 worldPos;        // worldPosition AFTER update()
            glm::vec3 hipsLocal;       // Hips bone localPosition (model space)
            int       state;           // currentState (AnimatedCharacterState as int)
            bool      isSitting;       // m_isSitting flag
            bool      isBlending;      // isBlending flag
            int       clipIndex;       // currentClipIndex
            float     animTime;        // animTime at record
        };

        const std::vector<MotionTraceEntry>& getMotionTrace() const { return m_motionTrace; }
        void clearMotionTrace() { m_motionTrace.clear(); }
        void setMotionTraceCapacity(size_t cap) { m_motionTraceCapacity = cap; }
        size_t motionTraceCapacity() const { return m_motionTraceCapacity; }

        // Get the physics controller body's linear velocity (for GPU particle collision)
        glm::vec3 getControllerVelocity() const;

    public:
        void setFootIKEnabled(bool enabled) { m_footIKEnabled = enabled; m_footIKBlend = 0.0f; }
        bool isFootIKEnabled() const { return m_footIKEnabled; }
        void resetFootLocks(); // clear all foot lock state — call before starting a new stair test

    protected:
        /// Called after keyframe sampling + updateGlobalTransforms, before bone body sync.
        /// Base implementation runs foot IK; override in subclass to add extra corrections.
        virtual void applyIKCorrections(float deltaTime);

    private:
        // ---- Foot IK ----
        struct FootIKBones {
            int upLegId = -1;
            int legId   = -1;
            int footId  = -1;
        };
        FootIKBones m_leftFoot;
        FootIKBones m_rightFoot;
        int   m_ikHipBoneId      = -1;    // pelvis/hip bone for body adjustment during IK
        bool  m_footIKEnabled    = true;
        bool  m_footIKCacheReady = false;
        float m_footIKBlend      = 0.0f;  // 0=off, 1=fully applied (ramped each frame)

        // Foot lock state — used during stair clips to pin feet to step surfaces.
        // When a foot contacts a surface, its world Y is recorded here and held by IK
        // while the body continues descending. lockBlend ramps 0→1 to ease in the lock.
        struct FootLockState {
            bool  active    = false;
            float lockedY   = 0.0f;
            float lockBlend = 0.0f;  // 0→1 ease-in after lock engages
        };
        FootLockState m_leftFootLock;
        FootLockState m_rightFootLock;
        int   m_ikClipIndex    = -1;    // tracks clip changes to reset foot locks
        float m_ikPrevAnimTime = 0.0f;  // tracks animation time for loop-wrap detection

        /// Populate m_leftFoot / m_rightFoot bone IDs from skeleton.boneMap.
        void resolveFootBoneIds();

        /// Analytical 2-bone IK (law of cosines). Modifies upLeg and leg currentRotation.
        /// targetWorld is the desired foot world-space position; invModel converts it to model space.
        /// blend ∈ [0,1] lerps between the animated foot pos and the IK target.
        void applyTwoBoneIK(int upLegId, int legId, int footId,
                            const glm::mat4& invModel, const glm::vec3& targetWorld,
                            float blend);

        // Protected accessors for subclasses (e.g. HybridCharacter IK)
        Phyxel::Skeleton& getSkeletonMut() { return skeleton; }
        Phyxel::AnimationSystem& getAnimSystemMut() { return animSystem; }

        const glm::vec3& getWorldPositionRef() const { return worldPosition; }
        float getSkeletonFootOffset() const { return skeletonFootOffset_; }
        float getCurrentYaw() const { return currentYaw; }
        Phyxel::ChunkManager* getChunkManagerPtr() const { return m_chunkManager; }

    private:
        Phyxel::Skeleton skeleton;
        std::vector<Phyxel::AnimationClip> clips;
        Phyxel::VoxelModel voxelModel;
        Phyxel::AnimationSystem animSystem;
        
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

        // One-shot dedup of "clip missing Speed" warnings emitted from the
        // movement step. Keyed by clip name.
        std::unordered_set<std::string> m_warnedSpeedFallback;
        
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

        // Kinematic controller state (replaces Bullet controllerBody)
        glm::vec3 m_kinVelocity{0.0f};
        bool      m_kinGrounded = false;
        float currentForwardInput = 0.0f;
        float currentTurnInput = 0.0f;
        float currentStrafeInput = 0.0f;
        float currentYaw = 0.0f;

        // Frame-coherent voxel-contact snapshot. Refreshed once per update().
        // Read by edge-teeter / climb-up / climb-down FSM consumers, and by
        // the MCP /api/character/contact debug route.
        CharacterContact m_lastContact;

        // Phase M5 — Ledge mantle. When active, resolveKinematicMovement
        // overrides gravity and drives the capsule along a fixed timeline
        // from (startXZ, startY) to (endXZ, endY) over m_mantleDuration.
        bool      m_mantleActive   = false;
        float     m_mantleTime     = 0.0f;
        float     m_mantleDuration = 0.7f;
        glm::vec3 m_mantleStart{0.0f};
        glm::vec3 m_mantleEnd{0.0f};
        // True when an active Preview state was entered by an override
        // (auto-mantle / ladder), so the FSM knows to auto-return to Idle
        // when the override clears. False for asset-editor Preview.
        bool      m_previewOwnedByOverride = false;
    public:
        // Returns true if a mantle was successfully started.
        bool beginMantle(const glm::vec3& start, const glm::vec3& end,
                         float durationSec = 0.7f);
        bool isMantleActive() const { return m_mantleActive; }

        // Phase M7 — Ladder climb. Continuous (not timeline-based): the
        // character locks XZ to the rail centreline, gravity is suspended,
        // and m_ladderInput.y drives vertical velocity. Caller drives the
        // climb each tick with setLadderInput(); detach via endLadderClimb().
        bool beginLadderClimb(const glm::vec3& railXZ_topY,
                              float bottomY,
                              float climbSpeed = 1.5f);
        void setLadderInput(float vertical);   // +1 = up, -1 = down, 0 = hold
        void endLadderClimb();
        bool isOnLadder() const { return m_ladderActive; }
        glm::vec2 getLadderRailXZ() const { return glm::vec2(m_ladderRailX, m_ladderRailZ); }

        // Public hook so the API server can simulate a player-style jump for
        // FSM-wire tests (Phase M5/M6 integration smokes).
        void requestJump() { jumpRequested = true; }
    private:
        // M7 ladder state
        bool  m_ladderActive   = false;
        float m_ladderRailX    = 0.0f;
        float m_ladderRailZ    = 0.0f;
        float m_ladderTopY     = 0.0f;
        float m_ladderBottomY  = 0.0f;
        float m_ladderSpeed    = 1.5f;
        float m_ladderInput    = 0.0f;
    private:

        // Phase M2 — Edge teeter: when enabled and the character is idle near
        // a voxel-column edge, apply a small visual pelvis offset away from the
        // edge so the pose visibly "shrinks back" from the drop. Default off
        // (opt-in via MCP set_teeter_blend) to keep existing visuals unchanged.
        bool  m_enableTeeterBlend  = false;
        float m_teeterAmount       = 0.0f;   // 0..1, lerped each frame
        glm::vec2 m_teeterDirXZ    = {0.0f, 0.0f};  // unit XZ direction away from edge
    public:
        const CharacterContact& getLastContact() const { return m_lastContact; }
        void  setTeeterBlendEnabled(bool on) { m_enableTeeterBlend = on; if (!on) { m_teeterAmount = 0.0f; } }
        bool  isTeeterBlendEnabled() const { return m_enableTeeterBlend; }
        float getTeeterAmount() const { return m_teeterAmount; }
        glm::vec2 getTeeterDirXZ() const { return m_teeterDirXZ; }
    private:

        // External velocity override (used by NPC patrol behavior)
        glm::vec3 externalVelocity{0.0f};
        bool hasExternalVelocity = false;

        // Terrain step-glide (smooth step-up / step-down over small voxel obstacles)
        float m_maxStepHeight    = 4.0f / 9.0f;  // Max obstacle height to glide over (4/9 voxel unit ≈ 2 subcubes)
        float m_stepGlideSpeed   = 4.0f;          // Y units/s for smooth ascent or descent
        float m_stepGlideTargetY = -1.0e30f;      // Active glide target Y (-1e30 = inactive)

        // Visual body spring — the skeleton renders from m_visualBodyY rather than
        // worldPosition.y directly. This smooths out instant capsule snaps (step-up,
        // ground snap) so the legs have time to react via per-foot IK.
        float m_visualBodyY    = 0.0f;
        float m_visualBodyVel  = 0.0f;
        bool  m_visualBodyInit = false;

        // Legacy step-up fields (kept to avoid breaking debug log / slider code)
        float     m_stepCooldown      = 0.0f;
        glm::vec3 m_prevStepCheckPos{0.0f};
        int       m_blockedFrames     = 0;

        // Foot position cache for swing-phase detection in terrain IK
        glm::vec3 m_prevLeftFootWorld{0.0f};
        glm::vec3 m_prevRightFootWorld{0.0f};
        bool      m_footPosValid = false;

        // Per-foot obstacle stepping — independent per leg, activates only when the
        // surface beneath a foot is 1/9 to 4/9 above the current foot Y.
        // Flat terrain correction is ~0, which is below the 1/9 minimum → never fires.
        struct FootStepState {
            bool  active  = false;  // step target is latched
            float targetY = 0.0f;   // world Y of detected step surface
            float blend   = 0.0f;   // 0→1 ease-in, ramps down on swing/deactivation
        };
        FootStepState m_leftStep;
        FootStepState m_rightStep;
        // When a step-up glide fires, this records the obstacle top Y and a
        // countdown timer so IK persists for a full natural-looking window even
        // though the capsule glide itself completes in ~1-2 frames.
        float m_stepIKObstacleY   = -1.0e30f;
        float m_stepIKTimer       = 0.0f;
        float m_stepIKOriginalH   = 0.0f;  // >0 = step-up height; <0 = step-down height
        bool  m_stepIKInitialized = false; // true once stay-foot has been determined
        bool  m_stepIKLeftIsStay  = false; // which foot gets the per-step correction

        // Root motion state
        float m_prevAnimTime = 0.0f;          // animTime from previous frame (loop-wrap detection)
        glm::vec3 m_prevRootPos{0.0f};        // root bone animated position from previous frame (delta base)
        bool m_yRootMotionActive = false;     // true when current clip extracts Y root motion (suppresses gravity)
        int m_prevClipIndex = -1;             // clip index from last frame (detects clip transitions)
        bool  m_stairDriveActive    = false;   // armed by activateStairDrive(); auto-clears after one pass
        bool  m_stairDriveNeedsInit = false;   // true on first frame after arm: captures world start
        glm::vec3 m_stairDriveWorldStart{0.0f}; // worldPosition when drive was armed

        // Anchored one-shot animation
        bool m_isAnchoredAnim = false;        // true while playing an anchored clip
        glm::vec3 m_anchorPos{0.0f};          // worldPosition frozen here during anchored playback
        float m_anchorYaw = 0.0f;             // yaw frozen during anchored playback
        int m_anchorClipIndex = -1;           // clip index of the anchored animation

        bool m_kinFrozen = false;             // suppresses gravity + ground snap (anim editor use)
        float m_playbackSpeed      = 1.0f;   // animation time multiplier (editor slow-mo)
        bool  m_animPaused         = false;   // freeze animTime tick (editor scrubbing)
        bool  m_warpPreviewActive  = false;
        float m_warpPreviewExtraY  = 0.0f;   // spatial: root bone Y offset at t=0
        float m_warpPreviewScale   = 1.0f;   // temporal: air phase stretch (actual/authored dist)
        float m_warpPreviewTakeoffN= 0.1f;   // normalized time where takeoff ends
        float m_warpPreviewContactN= 0.85f;  // normalized time where landing begins

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
        void resizeController();
        float computeSkeletonHeight() const;
        void rebuildCompoundShape();       // no-op: compound was for Bullet terrain, replaced by occupancy grid
        void updateCompoundTransforms();   // no-op
        void resolveKinematicMovement(float dt);
        void updateStateMachine(float deltaTime);
        void detectAndApplyStepUp(const glm::vec3& desiredVelocity, float deltaTime);
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
            int boneId;
            glm::vec3 size;
            glm::vec3 offset;
            glm::vec4 color;
            std::string label;
            glm::vec3 worldPos{0.0f};
            glm::quat worldRot{1, 0, 0, 0};
        };
        std::vector<BoneAttachment> m_attachments;
        int m_nextAttachmentId = 1;

        float m_originalHalfHeight = 0.95f;  // controller half-height (kinematic)
        float m_originalHalfWidth  = 0.25f;  // controller half-width  (kinematic)

        // Interaction archetype (parsed from .anim "# archetype:" header, default "humanoid_normal")
        std::string m_archetype = "humanoid_normal";

        // Sitting state
        bool m_isSitting = false;
        glm::vec3 m_seatSurfacePos{0.0f};  // seat anchor position (with height offset applied)
        float m_seatFacingYaw = 0.0f;
        int m_hipBoneIndex = -1;            // cached index of the hip bone
        float m_bindPoseHipHeight = 0.8f;   // bind-pose hip height (fallback)

        // Per-sit-state foot snap offsets (world-space, from InteractionPoint)
        glm::vec3 m_sitDownOffset{0.0f};
        glm::vec3 m_sittingIdleOffset{0.0f};
        glm::vec3 m_sitStandUpOffset{0.0f};
        float m_sitBlendDuration = 0.0f;    // crossfade duration for sit transitions
        float m_lastSeatHeightOffset = 0.0f; // last seatHeightOffset baked into m_seatSurfacePos.y

        // ---- Per-clip Hips reference offsets (Mixamo sit chain compensation) ----
        // The three sit clips were authored with different model-space origins:
        //   stand_to_sit ends with Hips at z=-0.471 (leaned back in chair)
        //   sitting_idle starts with Hips at z=+0.035 (upright on seat)
        //   sit_to_stand starts with Hips at z=+0.033, ends at z=+0.510 (forward)
        // To make the visible Hips world position continuous across clip boundaries,
        // each sit state shifts worldPosition by its reference Hips so that
        //   worldPosition + rotateByYaw(hipsRef) == seat anchor
        // at the clip's reference frame. Sampled once in sitAt() from the clip data.
        glm::vec3 m_hipsRef_sitDown{0.0f};     // hips XZ at END of stand_to_sit
        glm::vec3 m_hipsRef_sittingIdle{0.0f}; // hips XZ at START of sitting_idle
        glm::vec3 m_hipsRef_sitStandUp{0.0f};  // hips XZ at START of sit_to_stand

        // When true, the next clip-change should be a hard cut (blendDuration=0).
        // Defense-in-depth in case the sit-cycle exit triggers a clip switch from
        // the normal-path animation selection rather than the sit-block path.
        bool m_pendingClipSnap = false;

        // Motion trace ring buffer — see public MotionTraceEntry
        std::vector<MotionTraceEntry> m_motionTrace;
        size_t m_motionTraceCapacity = 1024;

        // Per-limb voxel collision
        Phyxel::ChunkManager* m_chunkManager = nullptr;

        // 8-segment collision boxes — one per major body segment, follow animation pose
        struct SegmentBox {
            std::string boneName;
            int boneId = -1;
            glm::vec3 center{0.0f};           // world-space center, updated each frame
            glm::vec3 halfExtents{0.0f};      // bind-pose local half-extents (source of truth)
            glm::vec3 worldHalfExtents{0.0f}; // AABB refit each frame from rotated corners
            bool isArm = false;
            bool colliding = false;
        };
        std::vector<SegmentBox> m_segmentBoxes;
        bool m_limbBlocked = false;  // true this frame if any arm segment overlaps a voxel

        // F5 debug visualization
        Phyxel::RaycastVisualizer* m_raycastVisualizer = nullptr;

        // ---- Derez state ----
        struct DerezEntry {
            glm::vec3 worldPos;
            glm::vec3 scale;
            glm::vec4 color;
            float     detachTime;
            size_t    partIndex;  // index into parts[] at snapshot time (used for active=false)
        };
        struct DerezState {
            std::vector<DerezEntry> queue;   // sorted ascending by detachTime
            size_t  nextIdx  = 0;
            float   elapsed  = 0.0f;
            float   duration = 2.0f;
            bool    active   = false;
        };
        std::optional<DerezState>    m_derezState;
        Phyxel::GpuParticlePhysics*  m_gpuPhysics = nullptr;

        void buildSegmentBoxes();          // called once after buildBodiesFromModel()
        void clearSegmentBoxes();          // called in clearBodies() and destructor
        void updateSegmentBoxes();         // sync segment box transforms to animated pose each frame
        void checkSegmentVoxelOverlap();   // detect arm/limb overlapping world voxels
        void drawSegmentBoxDebug();        // draw wireframe boxes via RaycastVisualizer when F5 on
    };
}
}
