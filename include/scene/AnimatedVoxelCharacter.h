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
        void setPosition(const glm::vec3& pos);
        glm::vec3 getPosition() const;
        
        // Control inputs
        void setControlInput(float forward, float turn);

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
        
        int currentClipIndex = -1;
        float animTime = 0.0f;
        glm::vec3 worldPosition;
        
        // Physics Controller
        btRigidBody* controllerBody = nullptr;
        float currentForwardInput = 0.0f;
        float currentTurnInput = 0.0f;
        float currentYaw = 0.0f;
        
        void createController(const glm::vec3& position);
    };
}
}
