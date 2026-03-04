#pragma once
#include "scene/RagdollCharacter.h"
#include "graphics/AnimationSystem.h"
#include "physics/PhysicsWorld.h"
#include <map>
#include <string>
#include <vector>

namespace VulkanCube {
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
        Preview
    };

    class AnimatedVoxelCharacter : public RagdollCharacter {
    public:
        AnimatedVoxelCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position);
        virtual ~AnimatedVoxelCharacter();

        // Load skeleton and animations
        bool loadModel(const std::string& animFile);
        
        // Attach a voxel shape (represented as a box for now) to a bone
        void addVoxelBone(const std::string& boneName, const glm::vec3& size, const glm::vec3& offset, const glm::vec4& color);
        
        void update(float deltaTime) override;
        void render(Graphics::RenderCoordinator* renderer) override; 

        void playAnimation(const std::string& animName);
        std::vector<std::string> getAnimationNames() const;
        void cycleAnimation(bool next);
        void setPosition(const glm::vec3& pos);
        glm::vec3 getPosition() const;
        
        // Control inputs
        void setControlInput(float forward, float turn, float strafe = 0.0f);
        void setSprint(bool sprint);
        void jump();
        void attack();
        void setCrouch(bool crouch);

        // Animation Mapping
        void setAnimationMapping(const std::string& stateName, const std::string& animName);
        std::string getAnimationMapping(const std::string& stateName) const;
        void setAnimationRotationOffset(const std::string& animName, float rotationDegrees);
        void setAnimationPositionOffset(const std::string& animName, const glm::vec3& offset);

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

        std::string stateToString(AnimatedCharacterState state);
        bool jumpRequested = false;
        bool attackRequested = false;
        float stateTimer = 0.0f;

        // Physics Controller
        btRigidBody* controllerBody = nullptr;
        float currentForwardInput = 0.0f;
        float currentTurnInput = 0.0f;
        float currentStrafeInput = 0.0f;
        float currentYaw = 0.0f;
        
        void createController(const glm::vec3& position);
        void updateStateMachine(float deltaTime);
        void configureAnimationFixes();
    };
}
}
