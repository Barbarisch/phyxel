#pragma once

#include "scene/RagdollCharacter.h"
#include "physics/PhysicsWorld.h"
#include "scene/Limb.h"
#include "scene/GaitController.h"
#include <vector>
#include <memory>

namespace VulkanCube {
namespace Scene {

struct SpiderLeg {
    int femur = -1;
    int tibia = -1;
    int tarsus = -1;
    btTypedConstraint* hipJoint = nullptr;
    btTypedConstraint* kneeJoint = nullptr;
    btTypedConstraint* ankleJoint = nullptr;
};

class SpiderCharacter : public RagdollCharacter {
public:
    SpiderCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos);
    virtual ~SpiderCharacter();

    void update(float deltaTime) override;

    // Control methods
    void setControlInput(float forward, float turn) override {
        targetForward = forward;
        targetTurn = turn;
    }

private:
    void createSpiderRagdoll(const glm::vec3& startPos);
    
    std::vector<SpiderLeg> legs;
    int bodyIndex = -1;
    int abdomenIndex = -1;

    // Procedural Animation System
    std::vector<std::unique_ptr<Limb>> limbs;
    std::unique_ptr<GaitController> gaitController;

    // Control state
    float targetForward = 0.0f;
    float targetTurn = 0.0f;
    float currentForward = 0.0f;
    float currentTurn = 0.0f;
    
    // Animation state
    float moveSpeed = 5.0f;
};

} // namespace Scene
} // namespace VulkanCube
